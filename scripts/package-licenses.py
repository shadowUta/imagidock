"""Collect pinned upstream notices and corresponding sources for a portable release.

Run after package-adapters.py and windeployqt. Network is opt-in (--download).
Never reads artwork or packages the development workspace. Only explicit locked
upstream archives, upstream build recipes, and deployed DLL metadata are included.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / ".tools/licenses"
LOCK = ROOT / "docs/license-sources.lock.json"
NOTICE = re.compile(r"(^|[-_.])(licen[cs]e|copying|copyright|notice|authors|patents)([-_.]|$)", re.I)
EXTRA_NAMES = {"readme", "readme.md", "readme.txt", "readme.qt", "readme.chromium", "dep5"}
HEADER_NOTICES = {"mikktspace.h", "mikktspace.c", "pugixml.hpp", "pugiconfig.hpp",
                  "ioapi.h", "unzip.h", "zlib.h", "jmorecfg.h", "stb_image.h",
                  "stb_image_write.h", "sqlite3.h"}
QT_MODULES = ("qtbase", "qtmultimedia", "qtimageformats", "qtsvg", "qttranslations")


def digest(path, algorithm="sha256"):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def inside(path):
    result = Path(path).resolve()
    if result == ROOT or not result.is_relative_to(ROOT):
        raise ValueError("Output must be a child directory of this workspace")
    return result


def verify(item, download=False):
    target = CACHE / item["cache_path"]
    if not target.exists() and download:
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".partial")
        request = urllib.request.Request(item["url"], headers={"User-Agent": "ImagiDock-license-packager/1"})
        with urllib.request.urlopen(request, timeout=120) as source, temporary.open("wb") as output:
            shutil.copyfileobj(source, output, length=1024 * 1024)
        if digest(temporary) != item["sha256"]:
            raise ValueError("Downloaded digest mismatch: " + item["file"])
        os.replace(temporary, target)
    if not target.is_file() or digest(target) != item["sha256"]:
        raise ValueError("Missing or mismatched locked source: " + item["file"])
    if "sha1" in item and digest(target, "sha1") != item["sha1"]:
        raise ValueError("Qt provisioning SHA-1 mismatch")
    return target


def safe_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError("Unsafe upstream archive path")
    return path


def entries(path):
    """Stream files without extracting an upstream archive or following links."""
    if path.suffix == ".zip":
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                if not member.is_dir():
                    yield member.filename, member.file_size, lambda m=member: archive.read(m)
    else:
        with tarfile.open(path, "r|*") as archive:
            for member in archive:
                if member.isfile():
                    yield member.name, member.size, lambda m=member: archive.extractfile(m).read()


def wanted(name):
    parts = PurePosixPath(name).parts
    base = parts[-1].lower()
    # Preserve the original notices even for optional components of an upstream
    # distribution; their presence is not a claim all of them are linked.
    return (bool(NOTICE.search(base)) or "LICENSES" in parts or
            base in EXTRA_NAMES or base in HEADER_NOTICES or base == "qt_attribution.json")


def collect_notices(source, component, destination):
    selected = {}
    references = set()
    for name, size, read in entries(source):
        if not wanted(name) or size > 8 * 1024 * 1024:
            continue
        safe_name(name)
        data = read()
        selected[name] = data
        if name.endswith("qt_attribution.json"):
            # A few upstream Qt attribution descriptions contain literal tabs.
            # Preserve the original bytes; accept those control characters only
            # while locating their explicitly named license attachments.
            values = json.loads(data, strict=False)
            for value in values if isinstance(values, list) else [values]:
                refs = value.get("LicenseFiles", []) + [value.get("LicenseFile", ""), value.get("CopyrightFile", "")]
                for ref in refs:
                    if ref:
                        references.add(posixpath.normpath(posixpath.join(posixpath.dirname(name), ref)))
    missing = references - selected.keys()
    if missing:
        for name, size, read in entries(source):
            if name in missing:
                if size > 8 * 1024 * 1024:
                    raise ValueError("Unbounded referenced notice")
                safe_name(name)
                selected[name] = read()
    unresolved = references - selected.keys()
    if unresolved:
        raise ValueError(f"Missing upstream attribution texts in {component}: {sorted(unresolved)}")
    inventory = []
    for name, data in sorted(selected.items()):
        relative = PurePosixPath(*safe_name(name).parts[1:])
        path = destination / component / str(relative)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        inventory.append({"file": str(PurePosixPath(component) / relative), "sha256": hashlib.sha256(data).hexdigest()})
    return inventory


def ffmpeg_configuration(app):
    libraries = {"avcodec-61.dll": "avcodec", "avformat-61.dll": "avformat", "avutil-59.dll": "avutil",
                 "swresample-5.dll": "swresample", "swscale-8.dll": "swscale"}
    result = {"upstream": "FFmpeg n7.1", "source_changes": "none; official Qt 6.8.3 binary distribution",
              "libraries": []}
    with os.add_dll_directory(str(app)):
        for name, prefix in libraries.items():
            path = app / name
            library = ctypes.CDLL(str(path))
            def call(symbol):
                function = getattr(library, symbol)
                function.restype = ctypes.c_char_p
                return function().decode("utf-8")
            record = {"file": name, "sha256": digest(path), "license": call(prefix + "_license"),
                      "configuration": call(prefix + "_configuration")}
            if prefix == "avutil":
                result["version"] = call("av_version_info")
            if record["license"] != "LGPL version 2.1 or later" or "--enable-gpl" in record["configuration"] or "--enable-nonfree" in record["configuration"]:
                raise ValueError("Unexpected FFmpeg license/build; distribution review required")
            result["libraries"].append(record)
    if result["version"] != "7.1":
        raise ValueError("FFmpeg version does not match locked corresponding source")
    return result


def verify_notice_coverage(licenses):
    must_exist = [
        "qtbase/LICENSES/LGPL-3.0-only.txt", "qtbase/LICENSES/GPL-3.0-only.txt",
        "qtbase/src/3rdparty/libpng/LICENSE", "qtbase/src/3rdparty/libjpeg/ijg-license.txt",
        "qtbase/src/3rdparty/libjpeg/COPYRIGHT.txt", "qtbase/src/3rdparty/pcre2/LICENCE.md",
        "qtbase/src/3rdparty/freetype/LICENSE.txt", "qtbase/src/3rdparty/harfbuzz-ng/COPYING",
        "qtimageformats/src/3rdparty/libtiff/COPYRIGHT", "qtimageformats/src/3rdparty/libwebp/COPYING",
        "filament/third_party/draco/LICENSE", "filament/third_party/basisu/LICENSE",
        "filament/third_party/meshoptimizer/LICENSE", "filament/third_party/stb/LICENSE",
        "filament/third_party/abseil/LICENSE", "filament/third_party/smol-v/LICENSE",
        "ffmpeg/COPYING.LGPLv2.1", "ffmpeg/COPYING.LGPLv3", "assimp/LICENSE",
        "zstd/LICENSE", "libsai/LICENSE", "sai2-parser/LICENSE",
    ]
    missing = [name for name in must_exist if not (licenses / name).is_file()]
    if missing:
        raise ValueError("Release missing required license texts: " + ", ".join(missing))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination")
    parser.add_argument("--download", action="store_true", help="Fetch missing locked upstream archives")
    parser.add_argument("--source-zip", default="dist/third-party-sources/ImagiDock-0.1.0-third-party-sources.zip")
    args = parser.parse_args()
    app = inside(args.destination)
    output = inside(args.source_zip)
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    sources = [(item, verify(item, args.download)) for item in lock["sources"]]
    receipts = [(item, verify(item, args.download)) for item in lock["receipts"]]
    licenses = app / "licenses"
    inventory = []
    for item, path in sources:
        print("Collecting notices: " + item["component"], flush=True)
        inventory.extend(collect_notices(path, item["component"], licenses))
    configuration = licenses / "build-information"
    configuration.mkdir(parents=True, exist_ok=True)
    for item, path in receipts:
        shutil.copyfile(path, configuration / item["file"])
    shutil.copyfile(LOCK, configuration / LOCK.name)
    dump(configuration / "ffmpeg-binaries.json", ffmpeg_configuration(app))
    qt = ROOT / ".tools/qt/6.8.3/msvc2022_64"
    for module in QT_MODULES:
        for source in (qt / "sbom").glob(module + "-6.8.3.*"):
            shutil.copyfile(source, configuration / source.name)
    shutil.copyfile(qt / "mkspecs/qconfig.pri", configuration / "qt-qconfig.pri")
    # Reproduction commands contain no developer machine paths or user data.
    shutil.copyfile(ROOT / "docs/third-party-build.md", configuration / "BUILD.md")
    shutil.copyfile(ROOT / "THIRD_PARTY_NOTICES.md", app / "THIRD_PARTY_NOTICES.md")
    shutil.copyfile(ROOT / "THIRD_PARTY_NOTICES.md", licenses / "README.md")
    verify_notice_coverage(licenses)
    dump(licenses / "native-notice-inventory.json", inventory)
    dump(configuration / "deployed-qt-binaries.json", [
        {"file": str(path.relative_to(app)).replace("\\", "/"), "sha256": digest(path)}
        for path in sorted(app.rglob("*.dll"))
        if path.name.startswith("Qt6") or path.parent.name in {"platforms", "imageformats", "multimedia", "tls", "styles", "iconengines", "networkinformation", "generic"}
    ])
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".partial")
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as bundle:
        for item, path in sources:
            bundle.write(path, "sources/" + item["file"])
        for path in sorted(configuration.iterdir()):
            # This receipt is written after hashing the finished ZIP. A repeat
            # staging run must not embed the previous ZIP's receipt in the new one.
            if path.is_file() and path.name != "corresponding-source-package.json":
                bundle.write(path, "build-information/" + path.name)
        bundle.write(ROOT / "THIRD_PARTY_NOTICES.md", "README.md")
    os.replace(temporary, output)
    receipt = {"file": output.name, "sha256": digest(output), "bytes": output.stat().st_size,
               "source_archives": len(sources), "native_notice_files": len(inventory)}
    dump(output.with_suffix(".json"), receipt)
    dump(configuration / "corresponding-source-package.json", receipt)
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
