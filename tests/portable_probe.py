"""Exercise the staged package with all development PATH/Python/Qt hints removed."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
REQUIRED_UI_ASSERTIONS={
    'maximized_model_framebuffer',
    'model_presents_on_native_window',
    'restored_model_framebuffer',
    'restored_model_presents_on_native_window',
    'detached_model_framebuffer_readback',
    'detached_model_presents_on_native_window',
    'reattached_model_framebuffer_readback',
    'reattached_model_presents_on_native_window',
}

def validate_ui_report(data):
    """Do not accept an older package that only reports successful readback."""
    if not isinstance(data,dict) or data.get('ok') is not True:
        raise RuntimeError('Portable self-test did not report success')
    rows=data.get('assertions')
    if not isinstance(rows,list) or any(not isinstance(row,dict) or not isinstance(row.get('name'),str) for row in rows):
        raise RuntimeError('Portable self-test assertions are malformed')
    assertions={row['name']:row.get('passed') for row in rows}
    if len(assertions)!=len(rows):
        raise RuntimeError('Portable self-test contains duplicate assertion names')
    missing=REQUIRED_UI_ASSERTIONS-assertions.keys()
    if missing:
        raise RuntimeError('Portable binary lacks required native presentation checks: '+', '.join(sorted(missing)))
    failed=[name for name,passed in assertions.items() if passed is not True]
    if failed:
        raise RuntimeError('Portable self-test assertions failed: '+', '.join(failed))

parser=argparse.ArgumentParser()
parser.add_argument('--package',type=Path,default=ROOT/'dist/ImagiDock-0.1.0-rc.1-win-x64')
parser.add_argument('--executable',default='ImagiDock.exe',choices=('ImagiDock.exe','ImagiDock-ui.exe'))
options=parser.parse_args()
PACKAGE=options.package.resolve()
if not PACKAGE.is_relative_to(ROOT):raise ValueError('Test package must stay inside the workspace')
WORK=ROOT/'.local'/('portable-clean-'+str(int(time.time())))
WORK.mkdir(parents=True)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('QT','QML','PYTHON','VIRTUAL_ENV','CONDA','NODE_','NPM'))}
env.update(PATH=str(Path(os.environ['SystemRoot'])/'System32'),TEMP=str(WORK),TMP=str(WORK))
def package_state():
    return {p.relative_to(PACKAGE).as_posix():(p.stat().st_size,p.stat().st_mtime_ns) for p in PACKAGE.rglob('*') if p.is_file()}
before=package_state()
report=WORK/'ui-result.json'
with (WORK/'ui.log').open('wb') as log:
    process=subprocess.Popen([str(PACKAGE/options.executable),'--self-test','--library',str(WORK/'library'),'--test-report',str(report),'--screenshot',str(WORK/'window.png')],cwd=WORK,env=env,stdout=log,stderr=log)
    try: code=process.wait(timeout=120)
    finally:
        # This dedicated library identifies only the service owned by this probe.
        pipe='\\\\.\\pipe\\imagidock-'+hashlib.sha256((WORK/'library').resolve().as_posix().lower().encode()).hexdigest()[:24]
        wait_pipe=ctypes.windll.kernel32.WaitNamedPipeW
        wait_pipe.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32]
        if wait_pipe(pipe,1000):
            try:
                with open(pipe,'r+b',buffering=0) as stream:
                    stream.write(b'{"id":"cleanup","method":"service.shutdown","params":{"cancel_jobs":true}}\n')
            except OSError:pass
        if process.poll() is None:process.kill();process.wait()
if code or not report.exists(): raise RuntimeError(f'Portable UI failed ({code}), see {WORK}')
data=json.loads(report.read_text(encoding='utf-8'))
validate_ui_report(data)
for executable,args in [(PACKAGE/'python/python.exe',['-B','-c','import PIL,psd_tools,numpy,psutil;print("ok")']),
                        (PACKAGE/'runtime/node/node.exe',['--version'])]:
    subprocess.run([str(executable),*args],cwd=WORK,env=env,check=True,capture_output=True,timeout=20)
if package_state()!=before:raise RuntimeError('Portable probe changed the package')
result={'ok':True,'scope':'local Windows machine with development PATH, Python, Node and Qt environment removed; not an independent clean OS','ui_report':data,'package_unchanged':True,'package':str(PACKAGE),'executable':options.executable,'work':str(WORK)}
(ROOT/'logs/portable-probe.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'ok':True,'assertions':len(data.get('assertions',[])),'scope':result['scope']},ensure_ascii=False))
