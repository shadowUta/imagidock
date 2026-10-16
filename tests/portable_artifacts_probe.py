"""Exercise an explicitly selected portable RC using only its deployed runtimes.

Outputs live under .local/portable-artifacts; process logs and receipts live
under logs. This does not stage, modify, or finish building the package.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / ".local/tests/adapters"


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def clean_environment(work):
    prefixes = ("QT", "QML", "PYTHON", "VIRTUAL_ENV", "CONDA", "NODE", "NPM", "UV_", "PIP_")
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith(prefixes)}
    env.update(PATH=str(Path(os.environ["SystemRoot"]) / "System32"),
               TEMP=str(work), TMP=str(work), TMPDIR=str(work))
    return env


def package_state(package):
    # Access times are excluded: opening package executables is expected.
    return {path.relative_to(package).as_posix(): (path.stat().st_size, path.stat().st_mtime_ns)
            for path in package.rglob("*") if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, help="Already assembled package directory inside the workspace")
    args = parser.parse_args()
    package = Path(args.package).resolve(strict=True)
    if not package.is_dir() or package == ROOT or not package.is_relative_to(ROOT):
        raise ValueError("The package must be a directory inside this workspace")
    python = package / "python/python.exe"
    node = package / "runtime/node/node.exe"
    required = [python, node, package / "helpers/preview.py", package / "mcp/dist/index.js",
                package / "imagidock-service.exe", package / "imagidock-media.exe", package / "imagidock-sai1.exe",
                package / "runtime/sai2/sai2topng.exe", package / "platforms/qoffscreen.dll"]
    for item in required:
        if not item.is_file():
            raise ValueError("Package is not assembled: " + str(item.relative_to(package)))
    run_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    work = ROOT / ".local/portable-artifacts" / run_id
    logs = ROOT / "logs/portable-artifacts" / run_id
    work.mkdir(parents=True)
    logs.mkdir(parents=True)
    env = clean_environment(work)
    before = package_state(package)
    report = {"ok": False, "scope": "Same Windows machine; development PATH and Qt/Python/Node hints removed. Not an independent clean OS.",
              "package": str(package), "work": str(work), "cases": [], "mcp": None,
              "entry_sha256": {str(item.relative_to(package)): sha256(item) for item in required}}

    def run(label, command, timeout=180):
        with (logs / (label + ".stdout.log")).open("wb") as out, (logs / (label + ".stderr.log")).open("wb") as err:
            completed = subprocess.run(command, cwd=work, env=env, stdin=subprocess.DEVNULL,
                                       stdout=out, stderr=err, timeout=timeout,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        raw = (logs / (label + ".stdout.log")).read_text(encoding="utf-8-sig")
        try:
            result = json.loads(raw)
        except ValueError as error:
            raise AssertionError(f"{label} returned invalid JSON; see private process logs") from error
        if completed.returncode or result.get("ok") is False:
            raise AssertionError(f"{label} failed: {result}")
        return result

    try:
        settings = work / "settings.json"
        # Do not set decoder executable paths: this checks auto-discovery of the
        # deployed native/SAl tools rather than borrowing development binaries.
        settings.write_text(json.dumps({"cache_root": str(work)}), encoding="utf-8")
        cases = [
            ("psd", "saved-composite.psd", "image", (19, 11), "saved_composite", (128, 128)),
            ("psb", "saved-composite.psb", "image", (19, 11), "saved_composite", (128, 128)),
            ("sai1", "加密缩略图.sai", "image", (2, 2), "embedded_thumbnail", (0, 255)),
            ("sai2", "alpha.sai2", "image", (17, 9), "saved_composite", (128, 128)),
            ("audio", "tone.wav", "audio", (256, 128), "waveform", (255, 255)),
            ("video", "synthetic.mp4", "video", (256, 128), "video_frame", (255, 255)),
        ]
        inspect = (
            "import json,sys;from PIL import Image;"
            "i=Image.open(sys.argv[1]).convert('RGBA');"
            "print(json.dumps({'size':i.size,'alpha':i.getchannel('A').getextrema(),"
            "'first':i.getpixel((0,0)),'last':i.getpixel((i.width-1,i.height-1)),"
            "'signal':i.getpixel((min(180,i.width-1),min(30,i.height-1)))}))"
        )
        for label, filename, kind, expected_size, preview_kind, alpha in cases:
            source = FIXTURES / filename
            if not source.is_file():
                raise AssertionError("Missing original synthetic fixture: " + filename)
            source_hash = sha256(source)
            output = work / (label + ".png")
            started = time.monotonic()
            decoded = run(label, [str(python), "-B", str(package / "helpers/preview.py"),
                                 "--input", str(source), "--output", str(output), "--kind", kind,
                                 "--size", "256", "--settings", str(settings)])
            assert decoded["ok"] is True and decoded["preview_kind"] == preview_kind, decoded
            assert Path(decoded["path"]).resolve() == output, decoded
            pixels = run(label + "-pixels", [str(python), "-B", "-c", inspect, str(output)], timeout=30)
            assert tuple(pixels["size"]) == expected_size, (label, pixels)
            assert tuple(pixels["alpha"]) == alpha, (label, pixels)
            if label in ("psd", "psb"):
                assert all(abs(a - b) <= 2 for a, b in zip(pixels["first"], (60, 140, 220, 128))), pixels
            elif label == "sai1":
                assert pixels["first"] == [255, 0, 0, 255] and pixels["last"][3] == 0, pixels
            elif label == "sai2":
                assert pixels["first"] == [255, 0, 0, 128], pixels
            elif label == "audio":
                assert decoded["metadata"]["decoder"] == "Qt Multimedia", decoded
                assert decoded["metadata"]["waveform_seconds"] == 2, decoded
                assert pixels["signal"] != [23, 29, 41, 255], pixels
            elif label == "video":
                assert decoded["metadata"]["decoder"] == "Qt Multimedia", decoded
                assert pixels["signal"][2] > 240, pixels
            assert sha256(source) == source_hash, "Original fixture changed"
            report["cases"].append({"name": label, "passed": True, "preview_kind": preview_kind,
                                    "size": pixels["size"], "alpha": pixels["alpha"],
                                    "source_sha256": source_hash, "output_sha256": sha256(output),
                                    "elapsed_ms": round((time.monotonic() - started) * 1000, 3),
                                    "decoder": decoded.get("metadata", {}).get("decoder")})
        report["mcp"] = run("mcp", [str(node), str(ROOT / "tests/portable_mcp_probe.mjs"),
                                     "--package", str(package), "--work", str(work / "mcp"),
                                     "--logs", str(logs / "mcp")], timeout=120)
        assert report["mcp"]["ok"] is True
        report["package_unchanged"] = package_state(package) == before
        assert report["package_unchanged"], "Probe wrote inside the package"
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        report["package_unchanged"] = package_state(package) == before
        text = json.dumps(report, ensure_ascii=False, indent=2)
        (logs / "report.json").write_text(text, encoding="utf-8")
        (ROOT / "logs/portable-artifacts-probe.json").write_text(text, encoding="utf-8")
    print(json.dumps({"ok": report["ok"], "package": package.name, "format_cases": len(report["cases"]),
                      "mcp_assertions": len(report["mcp"]["assertions"]), "package_unchanged": report["package_unchanged"],
                      "scope": report["scope"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
