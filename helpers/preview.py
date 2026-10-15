"""Read-only, isolated ImagiDock preview adapter. stdout is one JSON object.

The supervisor enforces elapsed time and aggregate resident memory for the
worker and its subprocesses. All temporary files stay beside the cache output.
"""
from __future__ import annotations

import argparse
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import warnings

VERSION = "imagidock-preview-1.0.1"
ROOT = Path(__file__).resolve().parent.parent


def emit(result):
    print(json.dumps(result, ensure_ascii=False), flush=True)


def bounded_int(settings, key, default, low, high):
    return min(high, max(low, int(settings.get(key, default))))


def tool_path(settings, key, default=None):
    value = settings.get(key) or default
    if not value or not Path(value).is_file():
        raise ValueError(f"未配置或找不到 {key}；可在设置中指定工具，或关联高清预览图")
    return str(Path(value).resolve())


def bundled_tool(name, *extras):
    candidates = [ROOT / name, ROOT / "build/bin" / name, ROOT / "build/Release" / name, *extras]
    return next((candidate for candidate in candidates if Path(candidate).is_file()), candidates[0])


def run_tool(arguments, cwd, timeout=100, env=None):
    # All callers supply an argument list. Logs cannot contaminate JSON stdout.
    with tempfile.TemporaryFile(dir=cwd) as log:
        completed = subprocess.run(arguments, stdin=subprocess.DEVNULL, stdout=log,
                                   stderr=log, cwd=cwd, timeout=timeout, env=env,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        log.seek(0, 2)
        length = log.tell()
        log.seek(max(0, length - 1024 * 1024))
        message = log.read().decode("utf-8", "replace").strip()
    if completed.returncode:
        raise ValueError(f"预览工具失败 ({completed.returncode}): {message[-2000:]}")
    return message


def ffmpeg_path(settings):
    if settings.get("ffmpeg_path"):
        return tool_path(settings, "ffmpeg_path")
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


def save_contained(image, path, size):
    from PIL import Image, ImageOps
    image = ImageOps.exif_transpose(image)
    canvas = image.size
    image = image.convert("RGBA")
    image.thumbnail((size, size), Image.Resampling.LANCZOS)
    image.save(path, "PNG", optimize=False)
    return {"width": image.width, "height": image.height,
            "canvas_width": canvas[0], "canvas_height": canvas[1]}


def read_image(path, max_pixels, apply_icc=True):
    from PIL import Image, ImageCms
    image = Image.open(path)
    if image.width * image.height > max_pixels:
        raise ValueError("图片超过像素上限；可关联较小的预览图")
    image.seek(0)
    image.load()
    profile = image.info.get("icc_profile")
    warning = None
    if profile and apply_icc:
        try:
            alpha = image.getchannel("A") if "A" in image.getbands() else None
            color = image.convert("RGB") if image.mode in ("RGBA", "P", "LA") else image
            image = ImageCms.profileToProfile(color, ImageCms.ImageCmsProfile(io.BytesIO(profile)),
                                             ImageCms.createProfile("sRGB"), outputMode="RGB")
            if alpha is not None:
                image.putalpha(alpha)
        except (ValueError, OSError, ImageCms.PyCMSError) as error:
            warning = "ICC无法转换，按原通道显示: " + str(error)
    return image, warning


def psd_preview(path, output, size, max_pixels, apply_icc=True):
    from psd_tools import PSDImage
    # topil uses the file's actual saved merged data. Never call composite().
    psd = PSDImage.open(path, max_alloc_bytes=768 * 1024 * 1024)
    metadata = {"decoder": "psd-tools", "layers": len(psd), "mode": str(psd.color_mode),
                "depth": psd.depth, "saved_composite": psd.has_preview()}
    image = None
    reason = None
    if psd.width * psd.height <= max_pixels and psd.has_preview():
        try:
            image = psd.topil(apply_icc=apply_icc)
        except Exception as error:
            reason = "保存的合成画面无法读取: " + str(error)
    else:
        reason = "画布超过预览像素上限" if psd.width * psd.height > max_pixels else "没有保存的合成画面"
    preview_kind = "saved_composite"
    if image is None and psd.has_thumbnail():
        image = psd.thumbnail()
        preview_kind = "embedded_thumbnail"
        metadata["warning"] = reason or "使用内嵌低清预览"
    if image is None:
        raise ValueError((reason or "没有保存的合成画面") + "；请在 Photoshop 保存兼容合成图，或关联 PNG/PSD 预览")
    result = save_contained(image, output, size)
    result.update(canvas_width=psd.width, canvas_height=psd.height,
                  preview_kind=preview_kind, metadata=metadata)
    return result


def sai2_preview(path, output, size, settings, work, max_pixels):
    from PIL import Image
    from sai2_thumbnail import read_header, extract_thumbnail
    header = read_header(path)
    metadata = {"decoder": "sai2-parser v2.0.0", "experimental": True,
                "format_tag": header["format_tag"]}
    error = None
    intermediate = work / "sai2-composite.png"
    try:
        if header["width"] * header["height"] > max_pixels:
            raise ValueError("SAI2画布超过像素上限")
        if header["format_tag"] not in ("norm", "ver1"):
            raise ValueError("尚未支持的SAI2版本")
        decoder = tool_path(settings, "sai2_decoder", bundled_tool("sai2topng.exe", ROOT / "runtime/sai2/sai2topng.exe", ROOT / ".tools/sai2/sai2topng.exe"))
        run_tool([decoder, str(path), str(intermediate)], work)
        image = Image.open(intermediate)
        image.load()
        kind = "saved_composite"
    except Exception as exception:
        error = str(exception)
        image = extract_thumbnail(path)
        if image is None:
            raise ValueError(error + "；无可用内嵌预览，可关联高清PNG/PSD") from exception
        kind = "embedded_thumbnail"
        metadata["warning"] = error + "；使用内嵌低清预览"
    result = save_contained(image, output, size)
    result.update(canvas_width=header["width"], canvas_height=header["height"],
                  preview_kind=kind, metadata=metadata)
    return result


def sai1_preview(path, output, size, settings, work):
    from PIL import Image
    decoder = tool_path(settings, "sai1_decoder", bundled_tool("imagidock-sai1.exe"))
    intermediate = work / "sai1-thumbnail.png"
    response = run_tool([decoder, str(path), str(intermediate)], work)
    info = {}
    try:
        info = json.loads(response.splitlines()[-1])
    except (ValueError, IndexError):
        pass
    image = Image.open(intermediate)
    image.load()
    result = save_contained(image, output, size)
    result.update(preview_kind="embedded_thumbnail", metadata={"decoder": "libsai",
                  "warning": "SAI 1 内嵌低清预览；可以关联高清PNG/PSD", "preview_width": image.width,
                  "preview_height": image.height})
    for key in ("canvas_width", "canvas_height"):
        if info.get(key):
            result[key] = info[key]
    return result


def audio_preview(path, output, size, settings, work):
    native = media_preview(path, output, size, settings, work, "audio")
    if native is not None:
        return native
    import numpy as np
    from PIL import Image, ImageDraw
    pcm = work / "waveform.s16le"
    # 2 hours at 8 kHz mono is <=115.2 MB, independent of source bitrate.
    seconds = bounded_int(settings, "waveform_max_seconds", 7200, 1, 7200)
    run_tool([ffmpeg_path(settings), "-nostdin", "-v", "error", "-i", str(path),
              "-map", "0:a:0", "-vn", "-t", str(seconds), "-ac", "1", "-ar", "8000",
              "-f", "s16le", "-y", str(pcm)], work)
    if not pcm.exists() or pcm.stat().st_size == 0:
        raise ValueError("音频没有可解码采样")
    values = np.memmap(pcm, mode="r", dtype="<i2")
    width, height = size, max(64, size // 2)
    image = Image.new("RGBA", (width, height), (23, 29, 41, 255))
    draw = ImageDraw.Draw(image)
    mid = height // 2
    draw.line((0, mid, width, mid), fill=(54, 67, 86, 255))
    for x in range(width):
        start, end = x * len(values) // width, (x + 1) * len(values) // width
        if end <= start:
            continue
        block = values[start:end].astype(np.int32)
        low = int(block.min()) / 32768
        high = int(block.max()) / 32768
        draw.line((x, mid - high * (mid - 5), x, mid - low * (mid - 5)), fill=(105, 191, 223, 255))
    sample_count = len(values)
    del values
    image.save(output, "PNG")
    return {"width": width, "height": height, "preview_kind": "waveform",
            "metadata": {"sample_rate": 8000, "waveform_seconds": sample_count / 8000,
                         "waveform_truncated": sample_count >= seconds * 8000}}


def video_preview(path, output, size, settings, work):
    native = media_preview(path, output, size, settings, work, "video")
    if native is not None:
        return native
    from PIL import Image
    intermediate = work / "first-frame.png"
    run_tool([ffmpeg_path(settings), "-nostdin", "-v", "error", "-i", str(path), "-map", "0:v:0",
              "-frames:v", "1", "-an", "-vf", f"scale={size}:{size}:force_original_aspect_ratio=decrease",
              "-y", str(intermediate)], work)
    image = Image.open(intermediate)
    image.load()
    result = save_contained(image, output, size)
    result.update(preview_kind="video_frame", metadata={"frame": "first_decodable"})
    return result


def media_preview(path, output, size, settings, work, kind):
    default = bundled_tool("imagidock-media.exe")
    program = settings.get("media_decoder") or str(default)
    if not Path(program).is_file():
        return None
    environment = os.environ.copy()
    environment["QT_QPA_PLATFORM"] = "offscreen"
    text = run_tool([str(program), kind, str(path), str(output), str(size)], work, env=environment)
    # Qt backend diagnostics are captured as well; the helper's last line is JSON.
    for line in reversed(text.splitlines()):
        if line.startswith("{"):
            result = json.loads(line)
            if not result.get("ok"):
                raise ValueError(result.get("error", "媒体解码失败"))
            return result
    raise ValueError("媒体工具未返回有效结果")


class BindingError(ValueError):
    def __init__(self, kind, message):
        super().__init__(message)
        self.kind = kind


def prepare_material_bindings(settings, work):
    """Supply the native renderer with verified PNGs, including PSD and SAI.

    Originals and the service's binding snapshot remain untouched. The outer
    worker's memory/time/process-tree limits also cover all nested decoders.
    Data textures bypass ICC transforms; base colour/emission use sRGB.
    """
    source_list = settings.get("bindings", [])
    if settings.get("bindings_path"):
        source_path = Path(settings["bindings_path"])
        if source_path.stat().st_size > 4 * 1024 * 1024:
            raise BindingError("preview_failed", "材质绑定配置超过大小上限")
        source_list = json.loads(source_path.read_text(encoding="utf-8-sig"))
    if not isinstance(source_list, list) or len(source_list) > 1024:
        raise BindingError("preview_failed", "材质绑定配置无效或数量超过上限")
    if not source_list:
        return None, []
    prepared, metadata = [], []
    max_pixels = bounded_int(settings, "preview_max_pixels", 64_000_000, 1, 64_000_000)
    max_file = bounded_int(settings, "preview_max_file_mb", 2048, 1, 4096) * 1024 * 1024
    for index, original in enumerate(source_list):
        if not isinstance(original, dict):
            raise BindingError("preview_failed", "材质绑定条目无效")
        binding = dict(original)
        path = Path(binding.get("path") or "").resolve()
        context = f"槽位 {binding.get('slot', 1)} / {binding.get('role', '')} / {path.name}"
        if not path.is_file():
            raise BindingError("dependency_missing", "绑定贴图丢失: " + context)
        if path.stat().st_size > max_file:
            raise BindingError("preview_failed", "绑定贴图超过预览大小上限: " + context)
        directory = work / f"binding-{index}"
        directory.mkdir()
        output = directory / "texture.png"
        color = binding.get("role") in ("base_color", "emissive")
        try:
            if path.suffix.lower() in (".psd", ".psb"):
                info = psd_preview(path, output, 2048, max_pixels, apply_icc=color)
            elif path.suffix.lower() == ".sai":
                info = sai1_preview(path, output, 2048, settings, directory)
            elif path.suffix.lower() == ".sai2":
                info = sai2_preview(path, output, 2048, settings, directory, max_pixels)
            else:
                image, warning = read_image(path, max_pixels, apply_icc=color)
                info = save_contained(image, output, 2048)
                info.update(preview_kind="image", metadata={"warning": warning} if warning else {})
        except Exception as error:
            raise BindingError("preview_failed", "绑定贴图解码失败: " + context + "；" + str(error)) from error
        binding["path"] = str(output)
        prepared.append(binding)
        metadata.append({"target_id": binding.get("target_id", ""), "slot": binding.get("slot", 1),
                         "role": binding.get("role", ""), "material_name": binding.get("material_name", ""),
                         "width": info["width"], "height": info["height"],
                         "preview_kind": info["preview_kind"], **info.get("metadata", {})})
    target = work / "material-bindings.json"
    target.write_text(json.dumps(prepared, ensure_ascii=False), encoding="utf-8")
    return target, metadata


def external_preview(path, output, size, kind, settings, work):
    from PIL import Image
    intermediate = work / "external-preview.png"
    extension = path.suffix.lower()
    metadata = {}
    if extension == ".blend":
        program = tool_path(settings, "blender_path")
        run_tool([program, "--background", "--factory-startup", "--disable-autoexec", "--python-exit-code", "1", str(path),
                  "--python", str(Path(__file__).with_name("blender_preview.py")), "--",
                  str(intermediate), str(size)], work)
        preview_kind = "blender_render"
    elif extension in (".ase", ".aseprite"):
        program = tool_path(settings, "aseprite_path")
        run_tool([program, "--batch", str(path), "--frame-range", "0,0", "--save-as", str(intermediate)], work)
        preview_kind = "aseprite_frame"
    elif kind == "model":
        program = tool_path(settings, "model_renderer", bundled_tool("imagidock-render.exe"))
        command = [program, "--input", str(path), "--output", str(intermediate), "--size", str(size)]
        bindings, binding_previews = prepare_material_bindings(settings, work)
        if bindings:
            command += ["--bindings", str(bindings)]
            metadata["binding_previews"] = binding_previews
        response = run_tool(command, work)
        for line in reversed(response.splitlines()):
            if line.startswith("{"):
                try:
                    rendered = json.loads(line)
                    metadata.update(rendered.get("metadata", rendered))
                except (ValueError, TypeError):
                    pass
                break
        preview_kind = "model_snapshot"
    else:
        raise ValueError("没有配置此源格式的预览工具；文件仍可索引和备份")
    image = Image.open(intermediate)
    image.load()
    result = save_contained(image, output, size)
    result.update(preview_kind=preview_kind, metadata=metadata)
    return result


def worker(args, settings):
    from PIL import Image
    max_pixels = bounded_int(settings, "preview_max_pixels", 64_000_000, 1, 64_000_000)
    Image.MAX_IMAGE_PIXELS = max_pixels
    warnings.simplefilter("error", Image.DecompressionBombWarning)
    source, output = Path(args.input).resolve(), Path(args.output).resolve()
    work = output.parent
    extension = source.suffix.lower()
    if extension in (".psd", ".psb"):
        result = psd_preview(source, output, args.size, max_pixels)
    elif extension == ".sai2":
        result = sai2_preview(source, output, args.size, settings, work, max_pixels)
    elif extension == ".sai":
        result = sai1_preview(source, output, args.size, settings, work)
    elif args.kind == "audio":
        result = audio_preview(source, output, args.size, settings, work)
    elif args.kind == "video":
        result = video_preview(source, output, args.size, settings, work)
    elif args.kind in ("source", "model"):
        result = external_preview(source, output, args.size, args.kind, settings, work)
    else:
        image, warning = read_image(source, max_pixels)
        result = save_contained(image, output, args.size)
        result.update(preview_kind="image", metadata={"warning": warning} if warning else {})
    result.update(ok=True, path=str(output), decoder_version=VERSION)
    return result


def stop_tree(process):
    import psutil
    try:
        parent = psutil.Process(process.pid)
        children = parent.children(recursive=True)
        for child in reversed(children):
            try:
                child.kill()
            except psutil.Error:
                pass
        parent.kill()
        psutil.wait_procs(children + [parent], timeout=3)
    except psutil.Error:
        pass


def supervisor(args, settings):
    import psutil
    source, output = Path(args.input).resolve(), Path(args.output).resolve()
    if not source.is_file():
        raise ValueError("源文件不存在")
    if source == output:
        raise ValueError("预览输出不能覆盖源文件")
    if source.stat().st_size > bounded_int(settings, "preview_max_file_mb", 2048, 1, 4096) * 1024 * 1024:
        raise ValueError("源文件超过预览大小上限；可关联较小的预览图")
    if settings.get("cache_root"):
        root = Path(settings["cache_root"]).resolve()
        if not output.is_relative_to(root):
            raise ValueError("预览输出必须位于当前库缓存目录")
    output.parent.mkdir(parents=True, exist_ok=True)
    seconds = bounded_int(settings, "preview_timeout_seconds", 120, 1, 600)
    memory = bounded_int(settings, "preview_memory_mb", 1024, 64, 4096) * 1024 * 1024
    with tempfile.TemporaryDirectory(prefix=".preview-", dir=output.parent) as scratch:
        work = Path(scratch)
        env = os.environ.copy()
        env.update(TEMP=scratch, TMP=scratch, TMPDIR=scratch, PYTHONPYCACHEPREFIX=str(work / "pycache"),
                   PYTHONIOENCODING="utf-8", MPLCONFIGDIR=str(work / "mpl"),
                   BLENDER_USER_CONFIG=str(work / "blender-config"), BLENDER_USER_SCRIPTS=str(work / "blender-scripts"),
                   ASEPRITE_USER_FOLDER=str(work / "aseprite-config"))
        settings_path = work / "settings.json"
        settings_path.write_text(json.dumps(settings), encoding="utf-8")
        stage = work / "preview.png"
        with (work / "result.txt").open("wb") as out, (work / "errors.txt").open("wb") as err:
            command = [sys.executable, "-B", str(Path(__file__).resolve()), "--worker", "--input", str(source),
                       "--output", str(stage), "--size", str(args.size), "--kind", args.kind, "--settings", str(settings_path)]
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=out, stderr=err, cwd=work,
                                       env=env, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            start = time.monotonic()
            failure = None
            while process.poll() is None:
                try:
                    parent = psutil.Process(process.pid)
                    resident = sum(p.memory_info().rss for p in [parent] + parent.children(recursive=True) if p.is_running())
                    if resident > memory:
                        failure = "预览任务超过内存上限"
                except psutil.Error:
                    pass
                if time.monotonic() - start > seconds:
                    failure = "预览任务超时"
                if failure:
                    stop_tree(process)
                    break
                time.sleep(0.08)
            process.wait()
        if failure:
            raise ValueError(failure + "；可以重试或关联预览图")
        if (work / "result.txt").stat().st_size > 2 * 1024 * 1024:
            raise ValueError("解码器元数据超过大小上限")
        raw = (work / "result.txt").read_bytes()
        try:
            result = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeError) as error:
            raise ValueError("解码进程失败或返回无效结果") from error
        if not result.get("ok"):
            return result
        if not stage.is_file() or stage.stat().st_size > 64 * 1024 * 1024:
            raise ValueError("无效的预览输出")
        os.replace(stage, output)
        result["path"] = str(output)
        return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--size", type=int, default=512, choices=(64, 128, 256, 512, 1024, 2048))
    parser.add_argument("--kind", choices=("image", "source", "model", "audio", "video"), required=True)
    parser.add_argument("--settings")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        settings = json.loads(Path(args.settings).read_text(encoding="utf-8-sig")) if args.settings else {}
        result = worker(args, settings) if args.worker else supervisor(args, settings)
        emit(result)
        return 0 if result.get("ok") else 1
    except Exception as error:
        emit({"ok": False, "error": str(error), "error_kind": getattr(error, "kind", "preview_decode"), "decoder_version": VERSION})
        return 1


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    raise SystemExit(main())
