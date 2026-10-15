"""Read-only smoke run against user-selected archive artwork. Results stay private."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive")
    parser.add_argument("--blender")
    parser.add_argument("--aseprite")
    args = parser.parse_args()
    archive = Path(args.archive).resolve()
    work = ROOT / ".local/tests/archive-adapters"
    work.mkdir(parents=True, exist_ok=True)
    settings = {"blender_path": args.blender or "", "aseprite_path": args.aseprite or "",
                "cache_root": str(work), "preview_memory_mb": 2048}
    config = work / "settings.json"
    config.write_text(json.dumps(settings), encoding="utf-8")
    desired = {".png": "image", ".wav": "audio", ".blend": "source", ".aseprite": "source", ".glb": "model"}
    picks = {}
    # rg enumerates names without reading artwork contents or modifying archives.
    files = subprocess.check_output(["rg", "--files", str(archive)], text=True, encoding="utf-8").splitlines()
    for value in files:
        path = Path(value)
        extension = path.suffix.lower()
        if extension in desired and extension not in picks:
            if path.stat().st_size < 64 * 1024 * 1024:
                picks[extension] = path
        if len(picks) == len(desired):
            break
    results = []
    for extension, source in picks.items():
        started = time.monotonic()
        before = digest(source)
        output = work / (extension[1:] + ".png")
        process = subprocess.run([sys.executable, "-B", str(ROOT / "helpers/preview.py"), "--input", str(source),
            "--output", str(output), "--kind", desired[extension], "--size", "512", "--settings", str(config)],
            capture_output=True, timeout=150)
        try:
            result = json.loads(process.stdout.decode("utf-8"))
        except ValueError:
            result = {"ok": False, "error": process.stderr.decode("utf-8", "replace")[-2000:]}
        result.update(extension=extension, source=str(source), source_unchanged=(before == digest(source)),
                      source_sha256=before, elapsed_seconds=time.monotonic() - started)
        results.append(result)
        print(json.dumps({key: result.get(key) for key in ("extension", "ok", "source_unchanged", "elapsed_seconds", "error")}), flush=True)
    (work / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0 if len(results) == len(desired) and all(item["ok"] and item["source_unchanged"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
