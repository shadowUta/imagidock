"""Reproduce project-local native format, Python and MCP development dependencies."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
CACHE=ROOT/'.cache';CACHE.mkdir(exist_ok=True)
os.environ.update(TEMP=str(CACHE),TMP=str(CACHE),PIP_CACHE_DIR=str(CACHE/'pip'),PYTHONPYCACHEPREFIX=str(CACHE/'pycache'))

def fetch(url,path,digest=None):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists():
        request=urllib.request.Request(url,headers={'User-Agent':'ImagiDock-build'})
        partial=path.with_name(path.name+'.partial')
        with urllib.request.urlopen(request,timeout=120) as source,partial.open('wb') as destination:
            shutil.copyfileobj(source,destination)
        partial.replace(path)
    if digest:
        with path.open('rb') as source: actual=hashlib.file_digest(source,'sha256').hexdigest()
        if actual!=digest:raise RuntimeError(f'Dependency checksum mismatch: {path.name}')

def run(*args):subprocess.run([str(a) for a in args],check=True,cwd=ROOT)

def main():
    if sys.version_info[:2]!=(3,11):raise RuntimeError('The adapter wheel lock requires CPython 3.11 x64.')
    venv=ROOT/'.tools/adapters-env'
    if not (venv/'Scripts/python.exe').exists():run(sys.executable,'-m','venv',venv)
    python=venv/'Scripts/python.exe'
    run(python,'-m','pip','download','--only-binary=:all:','--require-hashes','-r','helpers/requirements-lock.txt','--dest',CACHE/'wheels-adapters')
    run(python,'-m','pip','install','--no-index','--find-links',CACHE/'wheels-adapters','--require-hashes','-r','helpers/requirements-lock.txt')
    lock=json.loads((ROOT/'helpers/portable-lock.json').read_text())
    downloads=ROOT/'.tools/portable-downloads'
    for key in ('python','node'):
        row=lock[key];fetch(row['url'],downloads/row['file'],row['sha256'])
    fetch(lock['python']['digest_source'],downloads/(lock['python']['file']+'.sigstore'))
    fetch(lock['node']['digest_source'],downloads/f"node-v{lock['node']['version']}-SHASUMS256.txt")
    fetch(lock['sai2']['url'],ROOT/'.tools/sai2/sai2topng.exe',lock['sai2']['sha256'])
    source=ROOT/'.tools/sources/libsai'
    if not source.exists():run('git','clone','https://github.com/Wunkolo/libsai.git',source)
    run('git','-C',source,'checkout','--detach','f07ee8878e184eeeded79a5a2ef808bc51a3a7f7')
    npm=shutil.which('npm.cmd') or shutil.which('npm')
    if not npm:raise RuntimeError('Node.js with npm is required for the development build.')
    run(npm,'--prefix','mcp','ci','--ignore-scripts','--cache',CACHE/'npm')
    run(npm,'--prefix','mcp','run','build')

if __name__=='__main__':main()
