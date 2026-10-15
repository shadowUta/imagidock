"""Versioned, streaming library packages. Only explicit output paths are written."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import uuid
import zipfile

CHUNK = 1024 * 1024
MAX_ENTRIES = 1_000_000
MAX_TOTAL = 1024 ** 4


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(CHUNK), b""):
            h.update(block)
    return h.hexdigest()


def pack(source: Path, output: Path) -> dict:
    if output.exists():
        raise ValueError("Output already exists")
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_name(output.name + ".partial-" + uuid.uuid4().hex)
    try:
        entries = []
        with zipfile.ZipFile(partial, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as z:
            for path in sorted(source.rglob("*")):
                if path.is_symlink():
                    raise ValueError("Symlinks are not allowed in a library package")
                if not path.is_file():
                    continue
                name = path.relative_to(source).as_posix()
                if name == "checksums.json":
                    raise ValueError("Reserved package name")
                h = hashlib.sha256()
                size = 0
                with path.open("rb") as inp, z.open(name, "w", force_zip64=True) as out:
                    for block in iter(lambda: inp.read(CHUNK), b""):
                        out.write(block)
                        h.update(block)
                        size += len(block)
                entries.append({"path": name, "size": size, "sha256": h.hexdigest()})
            z.writestr("checksums.json", json.dumps({"version": 1, "files": entries}, ensure_ascii=False).encode("utf-8"))
        # No replacement: preserving existing destination is part of the contract.
        if output.exists():
            raise ValueError("Output appeared while packaging")
        partial.rename(output)
        return {"ok": True, "path": str(output), "files": len(entries)}
    finally:
        partial.unlink(missing_ok=True)


def safe_name(name: str) -> str:
    if "\\" in name or "\x00" in name or ":" in name:
        raise ValueError("Unsafe archive path")
    p = PurePosixPath(name)
    if p.is_absolute() or any(part in ("", ".", "..") or part.endswith((".", " ")) for part in p.parts):
        raise ValueError("Unsafe archive path")
    reserved = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}
    if any(part.split(".")[0].upper() in reserved for part in p.parts):
        raise ValueError("Reserved device path")
    return p.as_posix()


def unpack(source: Path, output: Path) -> dict:
    if output.exists():
        raise ValueError("Output directory must not exist")
    output.mkdir(parents=True)
    success = False
    try:
        with zipfile.ZipFile(source, "r") as z:
            infos = z.infolist()
            if len(infos) > MAX_ENTRIES or sum(i.file_size for i in infos) > MAX_TOTAL:
                raise ValueError("Library package exceeds safety limits")
            names = set()
            for info in infos:
                name = safe_name(info.filename)
                if name.casefold() in names or info.is_dir() or stat.S_ISLNK(info.external_attr >> 16):
                    raise ValueError("Duplicate path, directory entry, or symlink in package")
                names.add(name.casefold())
            manifest_info = z.getinfo("checksums.json")
            if manifest_info.file_size > 256 * 1024 * 1024:
                raise ValueError("Checksum manifest is too large")
            manifest = json.loads(z.read(manifest_info))
            if manifest.get("version") != 1:
                raise ValueError("Unsupported checksum manifest")
            expected = {safe_name(f["path"]): f for f in manifest["files"]}
            if len(expected) != len(manifest["files"]) or set(expected) != {i.filename for i in infos if i.filename != "checksums.json"}:
                raise ValueError("Package manifest and entries differ")
            for name, entry in expected.items():
                info = z.getinfo(name)
                if info.file_size != entry["size"]:
                    raise ValueError("Declared file size mismatch")
                destination = output.joinpath(*PurePosixPath(name).parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                h = hashlib.sha256()
                total = 0
                with z.open(info) as inp, destination.open("xb") as out:
                    for block in iter(lambda: inp.read(CHUNK), b""):
                        total += len(block)
                        if total > info.file_size:
                            raise ValueError("Expanded file exceeds declared size")
                        h.update(block)
                        out.write(block)
                if total != entry["size"] or h.hexdigest() != entry["sha256"]:
                    raise ValueError("File checksum mismatch: " + name)
            success = True
            return {"ok": True, "path": str(output), "files": len(expected)}
    finally:
        if not success:
            shutil.rmtree(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=("pack", "unpack"))
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        operation = pack if args.operation == "pack" else unpack
        result = operation(Path(args.source).resolve(), Path(args.output).resolve())
        print(json.dumps(result, ensure_ascii=False))
        return 0
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
