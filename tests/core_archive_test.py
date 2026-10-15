"""Lossless library packaging and malicious-path regression tests."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("archive_io", ROOT / "helpers" / "archive_io.py")
archive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / "data" / "tests"
        directory.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="archive-", dir=directory)
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_unicode_lossless_round_trip_and_no_overwrite(self):
        source = self.root / "source"
        (source / "previews").mkdir(parents=True)
        content = bytes(range(256)) * 6000
        (source / "previews" / "法线.png").write_bytes(content)
        (source / "library.sqlite3").write_bytes(b"test database bytes")
        package = self.root / "test.imagidock"
        self.assertEqual(archive.pack(source, package)["files"], 2)
        with self.assertRaises(ValueError):
            archive.pack(source, package)
        output = self.root / "extracted"
        archive.unpack(package, output)
        self.assertEqual((output / "previews" / "法线.png").read_bytes(), content)
        with self.assertRaises(ValueError):
            archive.unpack(package, output)

    def package(self, name, data=b"payload", bad_hash=False):
        package = self.root / "crafted.zip"
        with zipfile.ZipFile(package, "w") as z:
            z.writestr(name, data)
            z.writestr("checksums.json", json.dumps({"version": 1, "files": [{"path": name, "size": len(data), "sha256": "0" * 64 if bad_hash else hashlib.sha256(data).hexdigest()}]}))
        return package

    def test_paths_are_rejected_and_partial_output_removed(self):
        for name in ["../escape.txt", "/absolute", "C:/escape", "dir\\escape", "CON.txt", "a/../../escape", "file. "]:
            with self.subTest(name=name):
                output = self.root / "unpacked"
                with self.assertRaises(ValueError):
                    archive.unpack(self.package(name), output)
                self.assertFalse(output.exists())
                self.assertFalse((self.root / "escape.txt").exists())

    def test_corruption_is_rejected(self):
        output = self.root / "unpacked"
        with self.assertRaises(ValueError):
            archive.unpack(self.package("library.sqlite3", bad_hash=True), output)
        self.assertFalse(output.exists())

    def test_case_collisions_rejected(self):
        package = self.root / "collision.zip"
        with zipfile.ZipFile(package, "w") as z:
            z.writestr("A", b"a")
            z.writestr("a", b"b")
        with self.assertRaises(ValueError):
            archive.unpack(package, self.root / "out")


if __name__ == "__main__":
    unittest.main()
