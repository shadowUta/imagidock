"""Real IPC query benchmark over synthetic 10k/100k metadata, not a UI FPS claim."""
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import sqlite3
import statistics
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
QT = ROOT / ".tools/qt/6.8.3/msvc2022_64"
ENV = dict(os.environ, PATH=str(QT / "bin") + os.pathsep + os.environ["PATH"], QT_PLUGIN_PATH=str(QT / "plugins"), TEMP=str(ROOT / ".cache"), TMP=str(ROOT / ".cache"))
RUN_BIN = ROOT / ".local/core-scale-run/bin"


class Service:
    def __init__(self, library):
        self.process = subprocess.Popen([str(RUN_BIN / "imagidock-service.exe"), "--library", str(library), "--helpers", str(ROOT / "helpers"), "--python", str(ROOT / ".tools/adapters-env/Scripts/python.exe")], env=ENV, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        pipe = r"\\.\pipe\imagidock-" + hashlib.sha256(library.as_posix().lower().encode()).hexdigest()[:24]
        for _ in range(200):
            if self.process.poll() is not None:
                raise RuntimeError(self.process.communicate())
            try:
                self.stream = open(pipe, "r+b", buffering=0)
                self.reader = io.BufferedReader(self.stream, 65536)
                break
            except OSError:
                time.sleep(.05)
        else:
            self.process.kill()
            raise RuntimeError("Pipe unavailable")

    def call(self, method, params=None):
        self.stream.write(json.dumps({"id": "bench", "method": method, "params": params or {}}).encode() + b"\n")
        response = json.loads(self.reader.readline())
        if "error" in response:
            raise RuntimeError(response["error"])
        return response["result"]

    def close(self):
        try:
            self.call("service.shutdown")
            self.stream.close()
            self.process.wait(timeout=15)
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()


def main():
    RUN_BIN.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "build/bin/imagidock-service.exe", RUN_BIN / "imagidock-service.exe")
    (ROOT / "data/tests").mkdir(parents=True, exist_ok=True)
    report = {"scope": "Synthetic metadata; real named-pipe query+JSON+200 result records; existing tiny PNG path reused; not thumbnail decoding or UI FPS", "runs": []}
    with tempfile.TemporaryDirectory(prefix="scale-", dir=ROOT / "data/tests") as work:
        root = Path(work)
        png = root / "fixture.png"
        png.write_bytes(base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII="))
        libroot = root / "library"
        initial = Service(libroot)
        initial.close()
        existing = 0
        for size in (10000, 100000):
            conn = sqlite3.connect(libroot / "library.sqlite3")
            rows, tags = [], []
            for index in range(existing, size):
                aid = str(uuid.uuid5(uuid.NAMESPACE_URL, "imagidock-scale-" + str(index)))
                rows.append((aid, f"Asset {index:06d}.png", "image", "png", png.as_posix(), "synthetic-" + str(index), len(png.read_bytes()), int(png.stat().st_mtime * 1000), str(index), png.as_posix()))
                if index % 3 == 0:
                    tags.append((aid, "角色"))
                if index % 5 == 0:
                    tags.append((aid, "贴图"))
            conn.executemany("INSERT INTO assets(id,name,type,extension,source_path,source_key,size,mtime,imported,thumbnail_path) VALUES(?,?,?,?,?,?,?,?,?,?)", rows)
            conn.executemany("INSERT INTO asset_tags(asset_id,tag) VALUES(?,?)", tags)
            conn.commit()
            conn.close()
            existing = size
            service = Service(libroot)
            try:
                cases = {"first_page": {"limit": 200}, "union_tags": {"tags": ["角色", "贴图"], "types": ["image"], "limit": 200}, "substring": {"query": "Asset 009", "limit": 200}}
                for name, params in cases.items():
                    samples = []
                    total = 0
                    for repeat in range(31):
                        start = time.perf_counter_ns()
                        response = service.call("asset.list", params)
                        samples.append((time.perf_counter_ns() - start) / 1e6)
                        total = response["total"]
                    warm = sorted(samples[1:])
                    entry = {"records": size, "case": name, "matched": total, "first_ms": round(samples[0], 3), "warm_median_ms": round(statistics.median(warm), 3), "warm_p95_ms": round(warm[int(len(warm) * .95) - 1], 3), "warm_max_ms": round(max(warm), 3)}
                    report["runs"].append(entry)
                    print(json.dumps(entry), flush=True)
            finally:
                service.close()
    (ROOT / "logs").mkdir(exist_ok=True)
    (ROOT / "logs/core-scale.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
