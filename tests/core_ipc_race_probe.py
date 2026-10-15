"""Exercise first-frame races with bounded Win32 overlapped pipe operations.

All services, libraries, runtime copies and reports remain in this project.
No sleep is inserted between opening a pipe and writing its first request.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / ".local/core-ipc-race-run/bin"
QT = ROOT / ".tools/qt/6.8.3/msvc2022_64"
ENV = dict(os.environ, PATH=str(QT / "bin") + os.pathsep + os.environ["PATH"],
           QT_PLUGIN_PATH=str(QT / "plugins"), TEMP=str(ROOT / ".cache"), TMP=str(ROOT / ".cache"))
API = ctypes.WinDLL("kernel32", use_last_error=True)
INVALID_HANDLE = ctypes.c_void_p(-1).value
# If a faulty driver does not acknowledge cancellation promptly, retain its
# operation storage until process exit rather than blocking without a deadline.
PENDING_CANCELLATIONS = []


class Overlapped(ctypes.Structure):
    _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD),
                ("hEvent", wintypes.HANDLE)]


API.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                           wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
API.CreateFileW.restype = wintypes.HANDLE
API.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
API.CreateEventW.restype = wintypes.HANDLE
for name in ("ReadFile", "WriteFile"):
    getattr(API, name).argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                   ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(Overlapped)]
    getattr(API, name).restype = wintypes.BOOL
API.GetOverlappedResult.argtypes = [wintypes.HANDLE, ctypes.POINTER(Overlapped),
                                  ctypes.POINTER(wintypes.DWORD), wintypes.BOOL]
API.GetOverlappedResult.restype = wintypes.BOOL
API.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
API.WaitForSingleObject.restype = wintypes.DWORD
API.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
API.WaitNamedPipeW.restype = wintypes.BOOL
API.CancelIoEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(Overlapped)]
API.CancelIoEx.restype = wintypes.BOOL
API.CloseHandle.argtypes = [wintypes.HANDLE]
API.CloseHandle.restype = wintypes.BOOL


class Pipe:
    def __init__(self, name, timeout=8):
        self.buffer = bytearray()
        deadline = time.monotonic() + timeout
        while True:
            self.handle = API.CreateFileW(name, 0xC0000000, 0, None, 3, 0x40000000, None)
            if self.handle != INVALID_HANDLE:
                return
            error = ctypes.get_last_error()
            if error not in (2, 231) or time.monotonic() >= deadline:
                raise OSError(error, "Opening named pipe timed out or failed")
            if error == 231:
                API.WaitNamedPipeW(name, 10)
            else:
                time.sleep(.001)

    def operation(self, write=None, timeout=5):
        data = ctypes.create_string_buffer(write) if write is not None else ctypes.create_string_buffer(65536)
        event = API.CreateEventW(None, True, False, None)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        operation = Overlapped(hEvent=event)
        count = wintypes.DWORD()
        try:
            fn = API.WriteFile if write is not None else API.ReadFile
            complete = fn(self.handle, data, len(write) if write is not None else len(data),
                          ctypes.byref(count), ctypes.byref(operation))
            if not complete:
                error = ctypes.get_last_error()
                if error != 997:
                    raise OSError(error, "Pipe operation failed")
                if API.WaitForSingleObject(event, int(timeout * 1000)) != 0:
                    API.CancelIoEx(self.handle, ctypes.byref(operation))
                    if API.WaitForSingleObject(event, 1000) != 0:
                        PENDING_CANCELLATIONS.append((operation, data, event))
                        event = None
                        self.close()
                    raise TimeoutError("Named-pipe response exceeded its bounded deadline")
                if not API.GetOverlappedResult(self.handle, ctypes.byref(operation), ctypes.byref(count), False):
                    raise ctypes.WinError(ctypes.get_last_error())
            return count.value if write is not None else data.raw[:count.value]
        finally:
            if event:
                API.CloseHandle(event)

    def write(self, data):
        while data:
            written = self.operation(write=data)
            if not written:
                raise EOFError("Pipe closed while writing")
            data = data[written:]

    def read(self):
        deadline = time.monotonic() + 5
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("No complete reply frame within deadline")
            chunk = self.operation(timeout=remaining)
            if not chunk:
                raise EOFError("Pipe closed before reply")
            self.buffer.extend(chunk)
            if len(self.buffer) > 8 * 1024 * 1024:
                raise ValueError("Response exceeds frame limit")
        line, _, rest = self.buffer.partition(b"\n")
        self.buffer = bytearray(rest)
        return json.loads(line)

    def call(self, request_id, method="library.info"):
        self.write(frame(request_id, method))
        reply = self.read()
        assert reply.get("id") == request_id, reply
        assert "result" in reply and "error" not in reply, reply
        return reply["result"]

    def close(self):
        if self.handle != INVALID_HANDLE:
            API.CloseHandle(self.handle)
            self.handle = INVALID_HANDLE

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def frame(request_id, method="library.info"):
    return json.dumps({"id": request_id, "method": method, "params": {}}).encode() + b"\n"


class Service:
    def __init__(self, directory):
        self.name = r"\\.\pipe\imagidock-" + hashlib.sha256(directory.resolve().as_posix().lower().encode()).hexdigest()[:24]
        self.process = subprocess.Popen([str(RUN / "imagidock-service.exe"), "--library", str(directory),
                                         "--helpers", str(ROOT / "helpers"), "--python",
                                         str(ROOT / ".tools/adapters-env/Scripts/python.exe")],
                                        env=ENV, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def close(self):
        try:
            if self.process.poll() is None:
                with Pipe(self.name) as pipe:
                    pipe.call("shutdown", "service.shutdown")
                self.process.wait(timeout=10)
        finally:
            if self.process.poll() is None:
                self.process.kill()
            self.process.communicate(timeout=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cold", type=int, default=100)
    parser.add_argument("--connections", type=int, default=1000)
    parser.add_argument("--workers", type=int, default=16)
    args = parser.parse_args()
    RUN.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "build/bin/imagidock-service.exe", RUN / "imagidock-service.exe")
    (ROOT / "data/tests").mkdir(parents=True, exist_ok=True)
    report = {"state": "running", "cold_starts": 0, "concurrent_connections": 0,
              "workers": args.workers, "per_read_timeout_seconds": 5, "early_disconnects": 0}
    started = time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix="ipc-race-", dir=ROOT / "data/tests") as work:
            work = Path(work)
            for index in range(args.cold):
                service = Service(work / f"cold-{index}")
                try:
                    with Pipe(service.name) as pipe:
                        assert pipe.call(f"cold-{index}")["counts"]["all"] == 0
                        # Frames received together and split across writes must both work.
                        batch = [f"batch-{index}-{item}" for item in range(4)]
                        pipe.write(b"".join(frame(item) for item in batch))
                        replies = [pipe.read() for _ in batch]
                        assert {item["id"] for item in replies} == set(batch), replies
                        assert all("result" in item for item in replies), replies
                        split = frame(f"split-{index}")
                        pipe.write(split[:7])
                        pipe.write(split[7:])
                        assert pipe.read()["id"] == f"split-{index}"
                    report["cold_starts"] += 1
                finally:
                    service.close()
                if (index + 1) % 20 == 0:
                    print(json.dumps({"cold_starts": index + 1}), flush=True)

            service = Service(work / "concurrent")
            try:
                stop = threading.Event()

                def immediate(index):
                    if stop.is_set():
                        return False
                    with Pipe(service.name) as pipe:
                        assert pipe.call(f"parallel-{index}")["counts"]["all"] == 0
                    return True

                pool = ThreadPoolExecutor(max_workers=args.workers)
                futures = [pool.submit(immediate, index) for index in range(args.connections)]
                try:
                    for future in as_completed(futures):
                        report["concurrent_connections"] += int(future.result())
                finally:
                    stop.set()
                    pool.shutdown(wait=True, cancel_futures=True)

                for index in range(100):
                    with Pipe(service.name) as pipe:
                        pipe.write(frame(f"disconnect-{index}"))
                    report["early_disconnects"] += 1
                with Pipe(service.name) as pipe:
                    pipe.write(b"not-json\n" + frame("after-malformed"))
                    replies = [pipe.read(), pipe.read()]
                    assert any(item.get("error", {}).get("code") == "invalid_json" for item in replies)
                    assert any(item.get("id") == "after-malformed" and "result" in item for item in replies)
                    assert pipe.call("after-disconnects")["counts"]["all"] == 0
                report["malformed_then_valid"] = "passed"
            finally:
                service.close()
        report["state"] = "passed"
    except BaseException as error:
        report["state"] = "failed"
        report["error"] = repr(error)
        raise
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (ROOT / "logs/core-ipc-race.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)


if __name__ == "__main__":
    main()
