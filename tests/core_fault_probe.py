"""Abrupt termination recovery and descendant-process cancellation through real IPC."""
import base64
import json
from pathlib import Path
import shutil
import sqlite3
import tempfile
import time
import psutil
import core_scale_probe as harness
from core_scale_probe import ROOT, Service
RUN_BIN = ROOT / ".local/core-fault-run/bin"


def finish(service, jid, timeout=60):
    start = time.monotonic()
    while time.monotonic() - start < timeout:
        state = service.call("job.get", {"job_id": jid})
        if state["state"] not in ("queued", "running"):
            return state
        time.sleep(.05)
    raise RuntimeError("Job timed out")


def main():
    harness.RUN_BIN = RUN_BIN
    RUN_BIN.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "build/bin/imagidock-service.exe", RUN_BIN / "imagidock-service.exe")
    (ROOT / "data/tests").mkdir(parents=True, exist_ok=True)
    report = {}
    with tempfile.TemporaryDirectory(prefix="fault-", dir=ROOT / "data/tests") as work:
        root = Path(work)
        fixtures = root / "sources"
        fixtures.mkdir()
        image = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=")
        for index in range(2000):
            (fixtures / f"source-{index:04d}.png").write_bytes(image)
        library = root / "library"
        service = Service(library)
        job = service.call("asset.import", {"paths": [str(fixtures)], "backup": True})["job_id"]
        for _ in range(400):
            committed = service.call("asset.list", {"limit": 1})["total"]
            if committed >= 5:
                break
            time.sleep(.025)
        assert 5 <= committed < 2000, committed
        service.process.kill()  # This test owns this exact child process.
        service.process.wait(timeout=10)
        service.stream.close()
        with sqlite3.connect(library / "library.sqlite3") as db:
            assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
            initial = db.execute("SELECT COUNT(*) FROM assets").fetchone()[0]
            assert initial < 2000, initial
        db.close()
        service = Service(library)
        try:
            recovered = service.call("job.get", {"job_id": job})
            assert recovered["state"] == "interrupted", recovered
            retry = service.call("job.retry", {"job_id": job})
            completed = finish(service, retry["id"])
            assert completed["state"] == "completed", completed
            assert service.call("asset.list", {"limit": 1})["total"] == 2000
            report["abrupt_termination"] = {"integrity": "ok", "committed_before_kill": initial, "after_retry": 2000, "state": "passed"}
        finally:
            service.close()
        helpers = root / "mock-helpers"
        helpers.mkdir()
        (helpers / "preview.py").write_text('''import argparse, subprocess, sys, time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--output")
a,_=p.parse_known_args()
child=subprocess.Popen([sys.executable,"-c","import time; time.sleep(60)"])
Path(a.output+".pid").write_text(str(child.pid))
time.sleep(60)
''', encoding="utf-8")
        service = Service(root / "cancel-library", helpers)
        child_pid = None
        try:
            imported = finish(service, service.call("asset.import", {"paths": [str(fixtures / "source-0000.png")]})["job_id"])
            aid = imported["result"]["asset_ids"][0]
            preview = service.call("thumbnail.request", {"asset_id": aid})
            for _ in range(200):
                markers = list((root / "cancel-library/cache/thumbs").glob("*.pid"))
                if markers:
                    child_pid = int(markers[0].read_text())
                    break
                time.sleep(.025)
            assert child_pid is not None, service.call("job.get", {"job_id": preview["job_id"]})
            assert psutil.pid_exists(child_pid)
            service.call("job.cancel", {"job_id": preview["job_id"]})
            cancelled = finish(service, preview["job_id"])
            assert cancelled["state"] == "cancelled", cancelled
            for _ in range(100):
                if not psutil.pid_exists(child_pid):
                    break
                time.sleep(.02)
            assert not psutil.pid_exists(child_pid), "Decoder descendant survived cancellation"
            report["process_tree_cancellation"] = {"state": "passed", "descendant_terminated": True}
        finally:
            service.close()
            if child_pid and psutil.pid_exists(child_pid):
                psutil.Process(child_pid).terminate()
    (ROOT / "logs/core-faults.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
