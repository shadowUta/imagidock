"""Verify the shipped ZIP manifest and extract a fresh relocation-test copy."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NAME = 'ImagiDock-0.1.0-rc.1-win-x64'
ARCHIVE = ROOT / 'dist' / (NAME + '.zip')


def main():
    expected = ARCHIVE.with_suffix('.zip.sha256').read_text(encoding='ascii').split()[0]
    with ARCHIVE.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    assert actual == expected, 'Outer archive SHA-256 mismatch'
    destination = ROOT / '.local/release-check' / uuid.uuid4().hex
    with zipfile.ZipFile(ARCHIVE) as archive:
        entries = archive.infolist()
        assert len({entry.filename for entry in entries}) == len(entries), 'Duplicate ZIP path'
        for entry in entries:
            parts = PurePosixPath(entry.filename).parts
            assert parts and parts[0] == NAME and len(parts) > 1, 'Unexpected archive root'
            assert not any(part in ('..', '.') or ':' in part or '\\' in part for part in parts), 'Unsafe path'
            assert not stat.S_ISLNK(entry.external_attr >> 16), 'Symlink in archive'
        manifest_name = NAME + '/manifest.sha256.json'
        manifest = json.loads(archive.read(manifest_name))
        actual_paths = {entry.filename for entry in entries if not entry.is_dir()}
        assert actual_paths == {NAME + '/' + path for path in manifest} | {manifest_name}, 'Manifest/file set mismatch'
        notices = json.loads(archive.read(NAME + '/licenses/native-notice-inventory.json'))
        assert all(manifest.get('licenses/' + record['file'], {}).get('sha256') == record['sha256'] for record in notices), 'Missing or mismatched original license'
        for entry in entries:
            if entry.is_dir():
                continue
            relative = PurePosixPath(entry.filename)
            target = destination.joinpath(*relative.parts)
            assert target.resolve().is_relative_to(destination.resolve()), 'Extraction escaped destination'
            target.parent.mkdir(parents=True, exist_ok=True)
            digest = hashlib.sha256()
            size = 0
            with archive.open(entry) as source, target.open('xb') as output:
                while chunk := source.read(1024 * 1024):
                    digest.update(chunk)
                    size += len(chunk)
                    output.write(chunk)
            if entry.filename != manifest_name:
                record = manifest['/'.join(relative.parts[1:])]
                assert digest.hexdigest() == record['sha256'] and size == record['bytes'], 'Payload mismatch'
    report = {'ok': True, 'files': len(manifest), 'native_notices': len(notices), 'archive_sha256': actual,
              'relocated_package': str(destination / NAME),
              'scope': 'ZIP CRC, exact file set, per-file size/SHA-256, safe fresh extraction'}
    (ROOT / 'logs/release-archive-probe.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
