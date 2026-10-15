"""Use the existing Git credential manager; never print or store its secrets."""
import argparse, json, os, subprocess, sys, urllib.request, urllib.error
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(); p.add_argument('--create',action='store_true'); a=p.parse_args()
    env=dict(os.environ,GIT_TERMINAL_PROMPT='0',GCM_INTERACTIVE='never')
    r=subprocess.run(['git','-c','credential.interactive=never','credential','fill'],input='protocol=https\nhost=github.com\n\n',text=True,capture_output=True,env=env,cwd=ROOT,timeout=30)
    values=dict(line.split('=',1) for line in r.stdout.splitlines() if '=' in line)
    token=values.get('password') or os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
    if not token:
        print(json.dumps({'authenticated':False,'reason':'No existing GitHub credential is available; interactive sign-in is required.'})); return 2
    def api(path,data=None):
        req=urllib.request.Request('https://api.github.com'+path,data=json.dumps(data).encode() if data is not None else None,headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','User-Agent':'ImagiDock-development','X-GitHub-Api-Version':'2022-11-28'})
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
    return 0
if __name__=='__main__':
    try: sys.exit(main())
    except Exception as e:
        # Avoid printing Request headers or credential-helper stdout.
        print(json.dumps({'error':type(e).__name__,'message':str(e)})); sys.exit(1)
