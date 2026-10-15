"""Copy-only FBX sidecar backup/restore using the generated local fixture."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import core_scale_probe as harness
from core_fault_probe import finish

ROOT = harness.ROOT


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    harness.RUN_BIN = ROOT / ".local/core-fbx-run/bin"
    harness.RUN_BIN.mkdir(parents=True, exist_ok=True)
    for name in ("imagidock-service.exe", "imagidock-render.exe"):
        shutil.copy2(ROOT / "build/bin" / name, harness.RUN_BIN / name)
    (ROOT / "data/tests").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fbx-", dir=ROOT / "data/tests") as work:
        root = Path(work)
        source = root / "source"
        shutil.copytree(ROOT / ".local/fbx-fixture", source)
        model = source / "有贴图模型.fbx"
        texture = source / "颜色.png"
        hashes = {model.name: sha(model), texture.name: sha(texture)}
        service = harness.Service(root / "library")
        try:
            service.call("library.settings", {"values": {"model_renderer": str(harness.RUN_BIN / "imagidock-render.exe")}})
            imported = finish(service, service.call("asset.import", {"paths": [str(model)], "backup": True})["job_id"])
            assert imported["state"] == "completed", imported
            assert not imported["result"]["errors"], imported
            aid = imported["result"]["asset_ids"][0]
            asset = service.call("asset.get", {"asset_id": aid})
            assert asset["backup_state"] == "ready", asset
            assert len(asset["dependencies"]) >= 2, asset["dependencies"]
            # Remove only this test's copy. The original generation fixture remains intact.
            assert source.resolve().is_relative_to(root.resolve())
            shutil.rmtree(source)
            resolved = service.call("asset.resolve", {"asset_id": aid})
            if "job_id" in resolved:
                job = finish(service, resolved["job_id"])
                assert job["state"] == "completed", job
            resolved = service.call("asset.resolve", {"asset_id": aid})
            restored = Path(resolved["path"])
            assert sha(restored) == hashes[model.name]
            restored_texture = restored.parent / texture.name
            assert sha(restored_texture) == hashes[texture.name]
            process = subprocess.run([str(harness.RUN_BIN / "imagidock-render.exe"), "--inspect", "--input", str(restored)], env=harness.ENV, cwd=ROOT, capture_output=True, check=True, timeout=30)
            inspection = json.loads(process.stdout)
            assert inspection["ok"], inspection
            dependencies = [Path(p) for p in inspection["metadata"]["dependencies"]]
            assert dependencies and all(path.is_file() and path.resolve().is_relative_to(root.resolve()) for path in dependencies), inspection
            report = {"state": "passed", "source_copy_removed": True, "model_sha256_match": True, "texture_sha256_match": True, "restored_inspect_ok": True, "dependencies": len(dependencies), "original_fixture_unchanged": True}
            (ROOT / "logs/core-fbx.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
            print(json.dumps(report, indent=2))
        finally:
            service.close()


if __name__ == "__main__":
    main()
