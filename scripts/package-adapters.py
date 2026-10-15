"""Stage portable adapters into an existing application directory in this project.

Run with a development Python, e.g. python scripts/package-adapters.py dist/ImagiDock.
No network, pip installation, global configuration or source artwork is involved.
Native Qt helpers and Qt's multimedia DLLs are deployed by the main package script.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify(path, expected):
    if not Path(path).is_file() or sha256(path) != expected:
        raise RuntimeError(f"Missing or mismatched locked artifact: {Path(path).name}")


def extract(archive, destination, prefix=""):
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            if prefix and not entry.filename.startswith(prefix):
                continue
            name = entry.filename[len(prefix):]
            relative = PurePosixPath(name)
            if relative.is_absolute() or ".." in relative.parts or "\\" in name or ":" in name:
                raise RuntimeError("Unsafe package entry")
            if not name or entry.is_dir():
                continue
            target = destination.joinpath(*relative.parts)
            if not target.resolve().is_relative_to(destination.resolve()):
                raise RuntimeError("Unsafe package extraction target")
            target.parent.mkdir(parents=True, exist_ok=True)
            with package.open(entry) as source, target.open("wb") as output:
                shutil.copyfileobj(source, output)


def copy_tree(source, target, exclusions=()):
    shutil.copytree(source, target, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "config.local.json", *exclusions))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("destination")
    args = parser.parse_args()
    destination = Path(args.destination).resolve()
    if not destination.is_relative_to(ROOT) or destination == ROOT:
        raise RuntimeError("Package destination must be a child of this workspace")
    destination.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / "helpers/portable-lock.json").read_text(encoding="utf-8"))
    downloads = ROOT / ".tools/portable-downloads"
    for key in ("python", "node"):
        verify(downloads / lock[key]["file"], lock[key]["sha256"])
    # Check the official published digest fields as well as the committed lock.
    bundle = json.loads((downloads / (lock["python"]["file"] + ".sigstore")).read_text())
    published = base64.b64decode(bundle["messageSignature"]["messageDigest"]["digest"]).hex()
    if published != lock["python"]["sha256"]:
        raise RuntimeError("Python official Sigstore bundle digest does not match the lock")
    sums = (downloads / "node-v24.19.0-SHASUMS256.txt").read_text()
    if f'{lock["node"]["sha256"]}  {lock["node"]["file"]}' not in sums:
        raise RuntimeError("Node official SHASUMS digest does not match the lock")
    python = destination / "python"
    python.mkdir(exist_ok=True)
    extract(downloads / lock["python"]["file"], python)
    site = python / "Lib/site-packages"
    site.mkdir(parents=True, exist_ok=True)
    wheel_rows = []
    for line in (ROOT / "helpers/requirements-lock.txt").read_text(encoding="utf-8-sig").splitlines():
        match = re.fullmatch(r"([\w-]+)==([\w.]+) --hash=sha256:([a-f0-9]{64})", line)
        if not match:
            continue
        name, version, digest = match.groups()
        # Development-only media fixture generation; native Qt handles release decoding.
        if name == "imageio-ffmpeg":
            continue
        wheels = list((ROOT / ".cache/wheels-adapters").glob(name.replace("-", "_") + "-" + version + "-*.whl"))
        if len(wheels) != 1:
            raise RuntimeError(f"Exactly one locked wheel required for {name}")
        verify(wheels[0], digest)
        extract(wheels[0], site)
        wheel_rows.append({"name": name, "version": version, "sha256": digest, "file": wheels[0].name})
    (python / "python311._pth").write_text("python311.zip\n.\nLib/site-packages\n../helpers\nimport site\n", encoding="utf-8")
    # All required stdlib DLLs and package licenses remain present.
    node = destination / "runtime/node"
    node.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(downloads / lock["node"]["file"]) as package:
        prefix = "node-v24.19.0-win-x64/"
        for name in ("node.exe", "LICENSE", "README.md", "CHANGELOG.md"):
            (node / name).write_bytes(package.read(prefix + name))
    sai = destination / "runtime/sai2"
    sai.mkdir(parents=True, exist_ok=True)
    verify(ROOT / ".tools/sai2/sai2topng.exe", lock["sai2"]["sha256"])
    shutil.copy2(ROOT / ".tools/sai2/sai2topng.exe", sai / "sai2topng.exe")
    shutil.copy2(ROOT / ".tools/sai2/LICENSE", sai / "LICENSE")
    copy_tree(ROOT / "helpers", destination / "helpers", ("*.cpp",))
    copy_tree(ROOT / "mcp/dist", destination / "mcp/dist")
    copy_tree(ROOT / "mcp/node_modules", destination / "mcp/node_modules", ("typescript", "@types", ".bin"))
    # Keep SDK dependency license files. No compiler or local config is required.
    for name in ("package.json", "package-lock.json", "README.md", "codex-config.example.toml"):
        shutil.copy2(ROOT / "mcp" / name, destination / "mcp" / name)
    (destination / "mcp/config.example.json").write_text(json.dumps({"libraries": [{"path": "../data/default"}],
        "service_path": "../imagidock-service.exe", "timeout_ms": 30000}, indent=2), encoding="utf-8")
    copy_tree(ROOT / ".agents/skills/imagidock-assets", destination / ".agents/skills/imagidock-assets")
    notices = destination / "licenses/adapters"
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "helpers/ADAPTER_NOTICES.md", notices)
    shutil.copy2(ROOT / "helpers/portable-lock.json", notices)
    shutil.copy2(ROOT / "helpers/requirements-lock.txt", notices)
    shutil.copy2(downloads / (lock["python"]["file"] + ".sigstore"), notices)
    shutil.copy2(downloads / "node-v24.19.0-SHASUMS256.txt", notices)
    copy_tree(ROOT / "helpers/licenses", notices / "source-licenses")
    # Duplicated index makes license locations easy to inspect without a Python tool.
    license_paths = [str(path.relative_to(destination)).replace("\\", "/") for path in site.rglob("*")
                     if path.is_file() and any(word in path.name.lower() for word in ("license", "copying", "notice"))]
    report = {"format": "imagidock-adapters-package", "version": 1, "runtimes": lock,
              "python_wheels": wheel_rows, "license_files": license_paths,
              "media": "Qt Multimedia native helper; imageio-ffmpeg binary excluded"}
    (notices / "manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    env = os.environ.copy()
    scratch = ROOT / ".local/tests/portable-adapters"
    scratch.mkdir(parents=True, exist_ok=True)
    env.update(TEMP=str(scratch), TMP=str(scratch), PYTHONPYCACHEPREFIX=str(scratch / "pycache"))
    subprocess.run([str(python / "python.exe"), "-B", "-c", "import PIL,psd_tools,psutil,numpy,sai2_thumbnail; print('portable Python imports: OK')"],
                   check=True, cwd=scratch, env=env)
    subprocess.run([str(node / "node.exe"), "--version"], check=True, cwd=scratch, env=env)
    print(json.dumps({"ok": True, "destination": str(destination), "wheels": len(wheel_rows),
                      "license_files": len(license_paths)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
