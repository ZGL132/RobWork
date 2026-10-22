import subprocess,json
from pathlib import Path
root=Path.cwd();out=root/'.cache/b24-lock-green';out.mkdir()
repo=out/'repo';repo.mkdir();wt=out/'linked';report=[]
def run(name,cmd,expected):
    r=subprocess.run(cmd,capture_output=True)
    (out/(name+'.stdout.log')).write_bytes(r.stdout);(out/(name+'.stderr.log')).write_bytes(r.stderr)
    rec=dict(name=name,argv=cmd,exitCode=r.returncode,expectedExit=expected,passed=r.returncode==expected)
    report.append(rec);(out/'report.json').write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode())
    assert rec['passed'],rec
    return r.stdout.decode('utf-8-sig')
for name,cmd in [('init',['git','init','--quiet',str(repo)]),('commit',['git','-C',str(repo),'-c','user.name=IRD fixture','-c','user.email=ird-fixture@example.invalid','-c','commit.gpgsign=false','commit','--quiet','--allow-empty','-m','fixture']),('worktree',['git','-C',str(repo),'worktree','add','--detach',str(wt),'HEAD'])]:run(name,cmd,0)
script=root/'RobWork/scripts/industrialrobot/pipeline-lock.ps1'
for shell in ['pwsh','powershell']:
    cmd=[shell,'-NoProfile','-File',str(script),'-RepoRoot',str(wt)]
    def action(name,a,expected=0,token=None):
        argv=cmd+['-Action',a]
        if token:argv+=['-Token',token]
        text=run(shell+'-'+name,argv,expected)
        if expected==0:assert text.strip(),name
        return json.loads(text.strip().splitlines()[-1])
    first=action('acquire','acquire');token=first['tickId']
    assert action('busy','acquire',3)['state']=='locked'
    assert action('wrong-renew','renew',4,'wrong-owner')['state']=='not-owner'
    assert action('wrong-release','release',4,'wrong-owner')['state']=='not-owner'
    assert action('status-held','status')['tickId']==token
    assert action('renew','renew',0,token)['state']=='renewed'
    assert action('release','release',0,token)['state']=='released'
    assert action('status-none','status')['state']=='none'
    action('absent-renew','renew',1,token)
    run(shell+'-invalid-root',[shell,'-NoProfile','-File',str(script),'-Action','acquire','-RepoRoot',str(out/'missing')],1)
    print(shell,'10 action exit/JSON assertions passed')
