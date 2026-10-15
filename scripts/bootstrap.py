"""Download pinned development dependencies into the project only."""
from pathlib import Path
import concurrent.futures, hashlib, json, os, subprocess, sys, tarfile, urllib.request, zipfile
ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.cache' / 'downloads'
TOOLS = ROOT / '.tools'
LOCK = json.loads((ROOT/'docs'/'native-downloads.lock.json').read_text(encoding='utf-8'))
CACHE.mkdir(parents=True, exist_ok=True)
TOOLS.mkdir(exist_ok=True)
os.environ.update(TEMP=str(ROOT / '.cache'), TMP=str(ROOT / '.cache'), PIP_CACHE_DIR=str(ROOT / '.cache' / 'pip'), PYTHONPYCACHEPREFIX=str(ROOT / '.cache' / 'pycache'))
def fetch(url, name):
    path = CACHE / name
    if not path.exists():
        req = urllib.request.Request(url, headers={'User-Agent':'ImagiDock-build'})
        with urllib.request.urlopen(req, timeout=120) as src, path.with_suffix(path.suffix+'.partial').open('wb') as dst:
            while chunk := src.read(1024*1024): dst.write(chunk)
        path.with_suffix(path.suffix+'.partial').replace(path)
    sha = hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()
    if name not in LOCK or sha != LOCK[name]['sha256']:
        raise RuntimeError(f'Checksum verification failed for {name}; refusing to unpack it')
    print(json.dumps({'name':name,'bytes':path.stat().st_size,'sha256':sha},ensure_ascii=False),flush=True)
    return path, {'url':url,'file':name,'sha256':sha}
def unpack(path,dest):
    dest.mkdir(parents=True,exist_ok=True)
    if path.name.endswith('.zip'):
        with zipfile.ZipFile(path) as z: z.extractall(dest)
    else:
        with tarfile.open(path) as t: t.extractall(dest, filter='data')
def dep(spec):
    name,url,directory=spec
    path,record=fetch(url,name)
    marker=TOOLS / directory / '.unpacked'
    if not marker.exists():
        unpack(path, TOOLS / directory)
        marker.write_text(record['sha256'])
    return name,record
SPECS=[
 ('filament-v1.78.0-windows.tgz','https://github.com/google/filament/releases/download/v1.78.0/filament-v1.78.0-windows.tgz','filament'),
 ('sqlite-amalgamation-3500400.zip','https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip','sqlite'),
 ('zstd-1.5.7.tar.gz','https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz','zstd'),
 ('assimp-v5.4.3.zip','https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.zip','assimp'),
 ('gh_2.79.0_windows_amd64.zip','https://github.com/cli/cli/releases/download/v2.79.0/gh_2.79.0_windows_amd64.zip','gh'),
]
def main():
    lock={}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for name,record in pool.map(dep,SPECS): lock[name]=record
    print('All locked native dependencies verified.', flush=True)
if __name__=='__main__': main()
