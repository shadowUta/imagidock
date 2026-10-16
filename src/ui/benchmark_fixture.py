"""Create an isolated synthetic library for ImagiDock --benchmark.

Only metadata is synthetic. Cached thumbnails are copied from an explicit test
library. This tool never writes to the input library or changes source artwork.
The destination must not already exist. Do not distribute private thumbnails.
"""

import argparse
import hashlib
import json
import pathlib
import shutil
import sqlite3
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template-library", type=pathlib.Path, required=True)
    parser.add_argument("--destination", type=pathlib.Path, required=True)
    parser.add_argument("--count", type=int, choices=(10000, 100000), required=True)
    parser.add_argument("--theme", choices=("ivory", "graphite", "ocean", "mint"), default="graphite")
    args = parser.parse_args()
    template = args.template_library.resolve(strict=True)
    destination = args.destination.resolve()
    if destination.exists():
        parser.error("Destination already exists; choose a new dedicated test folder.")
    source = sqlite3.connect((template / "library.sqlite3").as_uri() + "?mode=ro", uri=True)
    thumbnails = []
    for (value,) in source.execute("SELECT DISTINCT thumbnail_path FROM assets WHERE thumbnail_path<>'' LIMIT 64"):
        path = pathlib.Path(value)
        if path.is_file():
            thumbnails.append(path)
    if not thumbnails:
        parser.error("Prepare thumbnails in the test template library first.")
    destination.mkdir(parents=True)
    cache = destination / "cache" / "thumbs"
    cache.mkdir(parents=True)
    copied = []
    for number, path in enumerate(thumbnails):
        target = cache / f"fixture-{number:03d}.png"
        shutil.copyfile(path, target)
        copied.append(target)
    db = sqlite3.connect(destination / "library.sqlite3")
    source.backup(db)
    source.close()
    for table in ("relations", "previews", "asset_tags", "issues", "jobs", "assets", "directories"):
        db.execute("DELETE FROM " + table)
    db.execute("DELETE FROM roles WHERE builtin=0")
    db.execute("DELETE FROM meta WHERE key NOT IN ('schema_version','model_preview_version')")
    db.executemany("INSERT INTO meta(key,value) VALUES(?,?)", [
        ("library_id", str(uuid.uuid4())), ("revision", "1"),
        ("settings", json.dumps({"theme": args.theme, "card_size": 176, "thumb_memory_mb": 256})),
    ])
    fixture = destination / "fixture-source.png"
    shutil.copyfile(copied[0], fixture)
    stat = fixture.stat()
    rows = ((str(uuid.uuid5(uuid.NAMESPACE_URL, f"imagidock-ui-benchmark-{i}")),
             f"Fixture {i:06d}.png", "image", "png", str(fixture), f"fixture-{i}",
             stat.st_size, int(stat.st_mtime * 1000), str(i), str(copied[i % len(copied)]))
            for i in range(args.count))
    db.executemany("INSERT INTO assets(id,name,type,extension,source_path,source_key,size,mtime,imported,thumbnail_path) VALUES(?,?,?,?,?,?,?,?,?,?)", rows)
    db.commit()
    db.execute("VACUUM")
    db.close()
    receipt = {"records": args.count, "unique_cached_thumbnails": len(copied),
               "scope": "Synthetic metadata; copies of existing cached thumbnails; warm disk cache. Not an import throughput measurement.",
               "source_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest()}
    (destination / "fixture-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(receipt))


if __name__ == "__main__":
    main()
