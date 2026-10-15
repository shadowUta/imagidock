import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest
import wave

import numpy as np
from PIL import Image, ImageCms, ImageDraw
from psd_tools import PSDImage
from psd_tools.constants import Compression, Resource
from psd_tools.psd.image_resources import ImageResource, VersionInfo
import imageio_ffmpeg

from adapters_fixtures import sai2_file, sai2_red, jssf_thumbnail, sai1_thumbnail

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / ".local/tests/adapters"
WORK.mkdir(parents=True, exist_ok=True)
os.environ.update(TEMP=str(WORK), TMP=str(WORK), PYTHONDONTWRITEBYTECODE="1")


class AdaptersTest(unittest.TestCase):
    def preview(self, source, kind="image", settings=None, ok=True, name=None):
        settings_path = WORK / "settings.json"
        settings_path.write_text(json.dumps(settings or {}), encoding="utf-8")
        output = WORK / ((name or source.name) + ".preview.png")
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        completed = subprocess.run([sys.executable, "-B", str(ROOT / "helpers/preview.py"),
            "--input", str(source), "--output", str(output), "--size", "256", "--kind", kind,
            "--settings", str(settings_path)], capture_output=True, timeout=150)
        response = json.loads(completed.stdout.decode("utf-8"))
        self.assertEqual(response["ok"], ok, response)
        self.assertEqual(completed.returncode, 0 if ok else 1, response)
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before, "source was changed")
        if ok:
            self.assertTrue(output.is_file())
            image = Image.open(output)
            image.load()
            self.assertLessEqual(max(image.size), 256)
            return response, image
        return response

    def test_image_contain_alpha_icc_and_unicode(self):
        source = WORK / "完整横向_中文.png"
        image = Image.new("RGBA", (800, 200), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.rectangle((0, 0, 30, 199), fill=(255, 0, 0, 255))
        draw.rectangle((770, 0, 799, 199), fill=(0, 255, 0, 255))
        profile = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()
        image.save(source, icc_profile=profile)
        result, output = self.preview(source)
        self.assertEqual(output.size, (256, 64))
        self.assertGreater(output.getpixel((0, 20))[0], 240)
        self.assertGreater(output.getpixel((255, 20))[1], 240)
        self.assertEqual(output.getpixel((128, 20))[3], 0)
        self.assertEqual(result["canvas_width"], 800)

    def test_psd_psb_saved_composite_alpha(self):
        # PSD merged RGB stores the image against a white matte, with separate
        # transparency. frompil writes channel bytes directly; supply that matte.
        image = Image.new("RGBA", (19, 11), (157, 197, 237, 128))
        for extension, version in (("psd", 1), ("psb", 2)):
            psd = PSDImage.frompil(image, compression=Compression.RAW)
            psd._record.header.version = version
            source = WORK / ("saved-composite." + extension)
            psd.save(source)
            result, output = self.preview(source, "source")
            self.assertEqual(result["preview_kind"], "saved_composite")
            self.assertEqual(output.size, image.size)
            actual = output.getpixel((2, 2))
            self.assertEqual(actual[3], 128)
            self.assertTrue(all(abs(a-b) <= 2 for a,b in zip(actual[:3], (60,140,220))), actual)

    def test_psd_without_saved_composite_never_reconstructs(self):
        psd = PSDImage.frompil(Image.new("RGB", (10, 12), "red"))
        psd.image_resources[Resource.VERSION_INFO] = ImageResource(key=Resource.VERSION_INFO,
            data=VersionInfo(has_composite=False))
        source = WORK / "no-saved-composite.psd"
        psd.save(source)
        result = self.preview(source, "source", ok=False)
        self.assertIn("合成", result["error"])

    def test_sai2_saved_composite_and_alpha(self):
        for transparent in (False, True):
            source = WORK / ("alpha.sai2" if transparent else "red.sai2")
            sai2_red(source, transparent=transparent)
            result, output = self.preview(source, "source")
            self.assertEqual(result["preview_kind"], "saved_composite")
            self.assertEqual(output.size, (17, 9))
            self.assertEqual(output.getpixel((0, 0)), (255, 0, 0, 128 if transparent else 255))
            self.assertEqual(output.getpixel((16, 8)), output.getpixel((0, 0)))

    def test_sai2_future_version_falls_back_to_bounded_thumbnail(self):
        source = WORK / "future.sai2"
        sai2_file(source, [(b"thum", jssf_thumbnail())], width=4096, height=2048, tag=b"ver9")
        result, image = self.preview(source, "source")
        self.assertEqual(result["preview_kind"], "embedded_thumbnail")
        self.assertEqual(image.size, (8, 8))
        self.assertEqual(result["canvas_width"], 4096)
        self.assertIn("warning", result["metadata"])
        self.assertTrue(all(abs(a-b) <= 3 for a,b in zip(image.getpixel((3,3))[:3], (20,160,240))))

    def test_corrupt_sai2_rejected_and_output_not_replaced(self):
        source = WORK / "broken.sai2"
        source.write_bytes(b"SAI-CANVAS-TYPE0" + bytes(4))
        output = WORK / "broken.sai2.preview.png"
        output.write_bytes(b"last-good-preview")
        self.preview(source, "source", ok=False)
        self.assertEqual(output.read_bytes(), b"last-good-preview")

    def test_audio_waveform_is_derived_from_signal(self):
        source = WORK / "tone.wav"
        samples = np.sin(np.arange(16000) * 2 * np.pi * 440 / 8000) * 24000
        samples[:4000] = 0
        with wave.open(str(source), "wb") as writer:
            writer.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
            writer.writeframes(samples.astype("<i2").tobytes())
        result, image = self.preview(source, "audio")
        self.assertEqual(result["preview_kind"], "waveform")
        self.assertEqual(result["metadata"]["waveform_seconds"], 2)
        self.assertEqual(image.getpixel((5, 30)), (23, 29, 41, 255))
        self.assertNotEqual(image.getpixel((180, 30)), (23, 29, 41, 255))

    def test_video_first_frame(self):
        source = WORK / "synthetic.mp4"
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-nostdin", "-v", "error", "-f", "lavfi",
            "-i", "color=c=blue:s=80x40:d=1", "-c:v", "mpeg4", "-y", str(source)], check=True)
        result, image = self.preview(source, "video")
        self.assertEqual(result["preview_kind"], "video_frame")
        self.assertEqual(image.size, (256, 128))
        self.assertGreater(image.getpixel((5, 5))[2], 240)

    def test_pixel_and_output_limits(self):
        source = WORK / "limits.png"
        Image.new("RGB", (30, 30), "red").save(source)
        self.preview(source, settings={"preview_max_pixels": 100}, ok=False)
        result = self.preview(source, settings={"cache_root": str(WORK / "another-library")}, ok=False)
        self.assertIn("缓存", result["error"])
        completed = subprocess.run([sys.executable, "-B", str(ROOT / "helpers/preview.py"), "--input", str(source),
            "--output", str(source), "--kind", "image"], capture_output=True)
        self.assertIn("覆盖", json.loads(completed.stdout.decode())["error"])

    def test_sai1_encrypted_embedded_thumbnail(self):
        choices = list((ROOT / "build").rglob("imagidock-sai1.exe"))
        if not choices:
            self.skipTest("native SAI1 target not built yet")
        source = WORK / "加密缩略图.sai"
        expected = sai1_thumbnail(source, ROOT / ".tools/sources/libsai")
        result, image = self.preview(source, "source", {"sai1_decoder": str(choices[0])})
        self.assertEqual(result["preview_kind"], "embedded_thumbnail")
        self.assertEqual(result["canvas_width"], 1024)
        self.assertEqual(image.tobytes(), expected)


if __name__ == "__main__":
    unittest.main(verbosity=2)
