"""Use the existing Git credential manager; never print or store its secrets."""
import argparse, hashlib, http.client, json, os, subprocess, sys, time, urllib.request, urllib.error, urllib.parse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(); p.add_argument('--create',action='store_true');p.add_argument('--release',action='store_true');p.add_argument('--tag',default='v0.1.0-rc.1');p.add_argument('--notes',type=Path);p.add_argument('--asset',type=Path,action='append',default=[]);a=p.parse_args()
    env=dict(os.environ,GIT_TERMINAL_PROMPT='0',GCM_INTERACTIVE='never')
    r=subprocess.run(['git','-c','credential.interactive=never','credential','fill'],input='protocol=https\nhost=github.com\n\n',text=True,capture_output=True,env=env,cwd=ROOT,timeout=30)
    values=dict(line.split('=',1) for line in r.stdout.splitlines() if '=' in line)
    token=values.get('password') or os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
    if not token:
        print(json.dumps({'authenticated':False,'reason':'No existing GitHub credential is available; interactive sign-in is required.'})); return 2
    def api(path,data=None,method=None):
        req=urllib.request.Request('https://api.github.com'+path,data=json.dumps(data).encode() if data is not None else None,method=method,headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','User-Agent':'ImagiDock-development','X-GitHub-Api-Version':'2022-11-28'})
        with urllib.request.urlopen(req,timeout=30) as stream: return json.load(stream)
    user=api('/user')['login']
    print(json.dumps({'authenticated':True,'login':user}))
    if user.lower()!='shadowuta':
        print(json.dumps({'error':'Account does not match the requested repository owner.'})); return 3
    if a.create:
        try: repo=api('/repos/shadowUta/imagidock')
        except urllib.error.HTTPError as e:
            if e.code!=404: raise
            repo=api('/user/repos',{'name':'imagidock','description':'创想坞 · ImagiDock — native desktop asset library with 3D previews, backups and MCP.','private':False,'auto_init':False})
        if repo['private']: raise RuntimeError('Existing repository is private; refusing to change unrelated visibility.')
        print(json.dumps({'url':repo['html_url'],'private':repo['private']}))
    if a.release:
        if not a.notes:raise RuntimeError('--notes is required for a reviewable release')
        notes=a.notes.resolve()
        if not notes.is_relative_to(ROOT):raise RuntimeError('Release notes must be in the workspace')
        assets=[]
        for file in a.asset:
            file=file.resolve()
            if not file.is_relative_to(ROOT/'dist') or not file.is_file():raise RuntimeError('Release assets must be existing files inside dist')
            if file.stat().st_size>=2*1024**3:raise RuntimeError('Release asset exceeds the GitHub limit')
            assets.append(file)
        by_name={file.name:file for file in assets}
        required={'ImagiDock-0.1.0-rc.1-win-x64.zip','ImagiDock-0.1.0-rc.1-win-x64.zip.sha256','ImagiDock-0.1.0-third-party-sources.zip','ImagiDock-0.1.0-third-party-sources.json'}
        if set(by_name)!=required or len(assets)!=len(required):
            raise RuntimeError('Release requires the application ZIP/checksum and corresponding-source ZIP/receipt together')
        def digest(file):
            with file.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
        app=by_name['ImagiDock-0.1.0-rc.1-win-x64.zip']
        checksum=by_name[app.name+'.sha256'].read_text(encoding='ascii').split()
        if checksum!=[digest(app),app.name]:raise RuntimeError('Application ZIP checksum does not match')
        source=by_name['ImagiDock-0.1.0-third-party-sources.zip']
        receipt=json.loads(by_name['ImagiDock-0.1.0-third-party-sources.json'].read_text(encoding='utf-8'))
        if receipt.get('file')!=source.name or receipt.get('bytes')!=source.stat().st_size or receipt.get('sha256')!=digest(source):
            raise RuntimeError('Corresponding-source ZIP receipt does not match')
        try:release=api('/repos/shadowUta/imagidock/releases/tags/'+urllib.parse.quote(a.tag,safe=''))
        except urllib.error.HTTPError as e:
            if e.code!=404:raise
            # Authenticated release listings include unfinished drafts, allowing
            # an interrupted upload to resume without creating a duplicate.
            release=next((item for item in api('/repos/shadowUta/imagidock/releases?per_page=100') if item['tag_name']==a.tag),None)
            if release is None:
                sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
                release=api('/repos/shadowUta/imagidock/releases',{'tag_name':a.tag,'target_commitish':sha,'name':'创想坞 · ImagiDock '+a.tag,'body':notes.read_text(encoding='utf-8'),'draft':True,'prerelease':True})
        existing={item['name']:item for item in release.get('assets',[])}
        for file in assets:
            with file.open('rb') as stream:local_digest='sha256:'+hashlib.file_digest(stream,'sha256').hexdigest()
            if file.name in existing:
                if existing[file.name]['size']!=file.stat().st_size:raise RuntimeError('Existing release asset differs; refusing to replace it silently')
                if existing[file.name].get('digest')!=local_digest:raise RuntimeError('Existing release asset checksum is unavailable or different; refusing to treat it as verified')
                print(json.dumps({'asset':file.name,'state':'already_uploaded'}));continue
            connection=http.client.HTTPSConnection('uploads.github.com',timeout=300)
            target='/repos/shadowUta/imagidock/releases/'+str(release['id'])+'/assets?name='+urllib.parse.quote(file.name)
            class UploadProgress:
                def __init__(self,stream):self.stream=stream;self.sent=0;self.last=time.monotonic()
                def read(self,size):
                    chunk=self.stream.read(size);self.sent+=len(chunk)
                    if time.monotonic()-self.last>=15:
                        print(json.dumps({'asset':file.name,'sent_bytes':self.sent,'total_bytes':file.stat().st_size,'state':'uploading'}),flush=True);self.last=time.monotonic()
                    return chunk
            with file.open('rb') as stream:
                connection.request('POST',target,body=UploadProgress(stream),headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','User-Agent':'ImagiDock-development','Content-Type':'application/octet-stream','Content-Length':str(file.stat().st_size)})
            response=connection.getresponse();data=response.read();connection.close()
            if response.status!=201:raise RuntimeError('Release upload failed with HTTP '+str(response.status))
            uploaded=json.loads(data)
            if not uploaded.get('digest'):
                uploaded=api('/repos/shadowUta/imagidock/releases/assets/'+str(uploaded['id']))
            if uploaded.get('digest')!=local_digest or uploaded.get('size')!=file.stat().st_size:
                raise RuntimeError('Uploaded asset checksum or size is unavailable or does not match the local file')
            print(json.dumps({'asset':uploaded['name'],'bytes':uploaded['size'],'sha256':local_digest.removeprefix('sha256:'),'remote_digest':uploaded.get('digest'),'state':'uploaded'}),flush=True)
        if release.get('draft'):
            release=api('/repos/shadowUta/imagidock/releases/'+str(release['id']),{'draft':False,'prerelease':True,'body':notes.read_text(encoding='utf-8')},method='PATCH')
        print(json.dumps({'release_url':release['html_url'],'prerelease':release['prerelease'],'draft':release.get('draft',False)}))
    return 0
if __name__=='__main__':
    try: sys.exit(main())
    except Exception as e:
        # Avoid printing Request headers or credential-helper stdout.
        print(json.dumps({'error':type(e).__name__,'message':str(e)})); sys.exit(1)
