"""Create a deterministic file inventory and a release ZIP without local state."""
import hashlib
import json
import os
from pathlib import Path
import zipfile

ROOT=Path(__file__).resolve().parents[1]
PACKAGE=ROOT/'dist/ImagiDock-0.1.0-rc.1-win-x64'
EXCLUDE_ANYWHERE={'__pycache__','.git'}
EXCLUDE_ROOT={'data','cache','logs','.local','.cache','.tools','build','tests'}
EXCLUDE_FILES={'config.local.json','manifest.sha256.json'}
TEXT_SUFFIXES={'.md','.json','.toml','.py','.ps1','.ts','.js','.mjs','.cjs',
               '.map','.cmd','.bat','.sh','.txt','.yaml','.yml','.ini','.cfg',
               '.xml','.csv','.html','.css','.conf','.license'}

def private_spellings(workspace=ROOT, user_profile=None):
    """Match this developer's identity, not generic upstream C:/Users examples."""
    paths={str(workspace)}
    identities=set()
    parts=workspace.parts
    for index,part in enumerate(parts[:-1]):
        if part.casefold()=='users':
            paths.add(str(Path(*parts[:index+2])))
            identities.add(parts[index+1])
            break
    profile=user_profile if user_profile is not None else os.environ.get('USERPROFILE')
    if profile:
        paths.add(str(profile))
        identities.add(Path(profile).name)
    spellings=set(identities)
    for path in paths:
        spellings.update((path,path.replace('\\','/'),path.replace('\\','\\\\')))
    return tuple(value for value in spellings if value)

def contains_private_path(data, spellings):
    # Transform both operands alike: lowercasing UTF-16 bytes on just one side
    # would also modify some non-ASCII low bytes and miss Chinese paths.
    lowered=data.lower()
    return any(value.encode(encoding).lower() in lowered
               for value in spellings for encoding in ('utf-8','utf-16le'))

def main():
    files=[]
    spellings=private_spellings()
    for path in sorted(PACKAGE.rglob('*')):
        rel=path.relative_to(PACKAGE)
        if not path.is_file() or rel.parts[0] in EXCLUDE_ROOT or any(part in EXCLUDE_ANYWHERE for part in rel.parts) or path.name in EXCLUDE_FILES or path.suffix in ('.pyc','.pdb','.log','.sqlite3','.lock'):continue
        if path.suffix.lower() in TEXT_SUFFIXES|{'.exe','.dll','.pyd',''}:
            if contains_private_path(path.read_bytes(),spellings):
                raise RuntimeError('Private developer identity or path in package file: '+str(rel))
        files.append(path)
    manifest={}
    for path in files:
        with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
        manifest[path.relative_to(PACKAGE).as_posix()]={'sha256':digest,'bytes':path.stat().st_size}
    inventory=PACKAGE/'manifest.sha256.json'
    inventory.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    archive=ROOT/'dist/ImagiDock-0.1.0-rc.1-win-x64.zip'
    with zipfile.ZipFile(archive.with_suffix('.zip.partial'),'w',zipfile.ZIP_DEFLATED,compresslevel=6) as output:
        for path in [*files,inventory]:output.write(path,PACKAGE.name+'/'+path.relative_to(PACKAGE).as_posix())
    archive.with_suffix('.zip.partial').replace(archive)
    with archive.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii')
    print(json.dumps({'archive':archive.name,'bytes':archive.stat().st_size,'sha256':digest,'files':len(files)},indent=2))

if __name__=='__main__':main()
