"""Real PSD/SAI texture decoding through the shipping Filament subprocess."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

from PIL import Image, ImageChops, ImageStat
from psd_tools import PSDImage
from adapters_fixtures import sai1_thumbnail, sai2_red
import render_test

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / ".local/tests/adapters-materials"


class MaterialAdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        WORK.mkdir(parents=True, exist_ok=True)
        render_test.WORK = WORK / "model"
        cls.model = render_test.fixtures()
        binary = ROOT / "build/bin/imagidock-render.exe"
        cls.renderer = ROOT / ".local/adapters-run/imagidock-render.exe"
        cls.renderer.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(binary, cls.renderer)
        qt = ROOT / ".tools/qt/6.8.3/msvc2022_64"
        cls.env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_PLUGIN_PATH=str(qt / "plugins"),
                       PATH=str(qt / "bin") + os.pathsep + os.environ["PATH"], TEMP=str(WORK), TMP=str(WORK),
                       PYTHONUTF8="1", PYTHONDONTWRITEBYTECODE="1")

    def render(self, name, texture=None, material="Material 4", ok=True):
        bindings = [] if texture is None else [{"slot": 4, "role": "base_color", "path": str(texture),
                                                "material_name": material, "target_id": "synthetic-texture"}]
        binding_path = WORK / (name + ".bindings.json")
        binding_path.write_text(json.dumps(bindings), encoding="utf-8")
        original = binding_path.read_bytes()
        settings = WORK / (name + ".settings.json")
        settings.write_text(json.dumps({"model_renderer": str(self.renderer), "bindings_path": str(binding_path),
                                       "cache_root": str(WORK)}), encoding="utf-8")
        output = WORK / (name + ".png")
        if not ok:
            output.write_bytes(b"previous-good-cache")
        source_hash = hashlib.sha256(texture.read_bytes()).hexdigest() if texture and texture.exists() else None
        run = subprocess.run([sys.executable, "-B", str(ROOT / "helpers/preview.py"), "--input", str(self.model),
                              "--output", str(output), "--size", "256", "--kind", "model", "--settings", str(settings)],
                             capture_output=True, env=self.env, timeout=150)
        result = json.loads(run.stdout.decode("utf-8"))
        self.assertEqual(result["ok"], ok, result)
        self.assertEqual(run.returncode, 0 if ok else 1, result)
        self.assertEqual(binding_path.read_bytes(), original)
        if source_hash:
            self.assertEqual(hashlib.sha256(texture.read_bytes()).hexdigest(), source_hash)
        if not ok:
            self.assertEqual(output.read_bytes(), b"previous-good-cache")
        return result, output

    def test_psd_psb_sai1_sai2_bind_actual_model(self):
        _, baseline = self.render("unbound")
        for extension in ("psd", "psb", "sai", "sai2"):
            with self.subTest(extension=extension):
                texture = WORK / ("绑定固有色." + extension)
                if extension in ("psd", "psb"):
                    image = Image.new("RGB", (4096 if extension == "psd" else 64, 64), (255, 0, 0))
                    psd = PSDImage.frompil(image)
                    psd._record.header.version = 2 if extension == "psb" else 1
                    psd.save(texture)
                elif extension == "sai":
                    sai1_thumbnail(texture, ROOT / ".tools/sources/libsai")
                else:
                    sai2_red(texture)
                result, output = self.render(extension, texture)
                info = result["metadata"]["binding_previews"][0]
                self.assertEqual(info["slot"], 4)
                self.assertEqual(info["material_name"], "Material 4")
                self.assertLessEqual(max(info["width"], info["height"]), 2048)
                if extension == "psd":
                    self.assertEqual(info["width"], 2048)
                self.assertEqual(result["metadata"]["binding_issues"], [])
                difference = ImageChops.difference(Image.open(baseline).convert("RGB"), Image.open(output).convert("RGB"))
                self.assertGreater(sum(ImageStat.Stat(difference).mean), 0.1)

    def test_binding_name_still_guards_changed_slot(self):
        texture = WORK / "guard.psd"
        PSDImage.frompil(Image.new("RGB", (32, 32), "red")).save(texture)
        result, _ = self.render("old-material-name", texture, material="Old material")
        self.assertEqual(result["metadata"]["binding_issues"][0]["expected"], "Old material")

    def test_bad_and_missing_texture_fail_explicitly(self):
        texture = WORK / "broken.psd"
        texture.write_bytes(b"invalid-psd")
        result, _ = self.render("bad-binding", texture, ok=False)
        self.assertEqual(result["error_kind"], "preview_failed")
        self.assertIn("绑定贴图解码失败", result["error"])
        result, _ = self.render("missing-binding", WORK / "not-present.sai", ok=False)
        self.assertEqual(result["error_kind"], "dependency_missing")


if __name__ == "__main__":
    unittest.main(verbosity=2)
