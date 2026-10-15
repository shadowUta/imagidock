"""Small real named-pipe integration probe; all data stays in the workspace."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
qt = ROOT / ".tools/qt/6.8.3/msvc2022_64"
env = os.environ.copy()
env.update(PATH=str(qt / "bin") + os.pathsep + env["PATH"], QT_PLUGIN_PATH=str(qt / "plugins"), TEMP=str(ROOT / ".cache"), TMP=str(ROOT / ".cache"))
(ROOT / "data/tests").mkdir(parents=True, exist_ok=True)


def main():
    run_bin = ROOT / ".local/core-ipc-run/bin"
    run_bin.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "build/bin/imagidock-service.exe", run_bin / "imagidock-service.exe")
    with tempfile.TemporaryDirectory(prefix="ipc-", dir=ROOT / "data/tests") as work:
        root = Path(work)
        library = root / "library"
        pipe = r"\\.\pipe\imagidock-" + hashlib.sha256(library.as_posix().lower().encode()).hexdigest()[:24]
        proc = subprocess.Popen([str(run_bin / "imagidock-service.exe"), "--library", str(library), "--helpers", str(ROOT / "helpers"), "--python", str(ROOT / ".tools/adapters-env/Scripts/python.exe")], env=env, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stream = None
        try:
            for _ in range(200):
                if proc.poll() is not None:
                    raise RuntimeError(proc.communicate())
                try:
                    stream = open(pipe, "r+b", buffering=0)
                    break
                except OSError:
                    time.sleep(.05)
            if stream is None:
                raise RuntimeError("Service failed to accept a connection")

            def call(method, params=None):
                request = {"id": "probe", "method": method, "params": params or {}}
                stream.write(json.dumps(request).encode() + b"\n")
                response = json.loads(stream.readline())
                if "error" in response:
                    raise RuntimeError(response)
                return response["result"]

            print(json.dumps(call("library.info"), ensure_ascii=False))
            job = call("library.export", {"destination": str(root / "test.imagidock")})
            for _ in range(200):
                status = call("job.get", job)
                if status["state"] not in ("queued", "running"):
                    break
                time.sleep(.05)
            print(json.dumps(status, ensure_ascii=False))
            call("service.shutdown")
            proc.wait(timeout=15)
            return 0 if status["state"] == "completed" else 1
        finally:
            if stream:
                stream.close()
            if proc.poll() is None:
                proc.kill()
            proc.wait()


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
