import subprocess,json
from pathlib import Path
root=Path.cwd();out=root/'.cache/b24-lock-probe';out.mkdir()
repo=out/'repo';repo.mkdir();wt=out/'linked'
commands=[]
for cmd in [['git','init','--quiet',str(repo)],['git','-C',str(repo),'-c','user.name=IRD fixture','-c','user.email=ird-fixture@example.invalid','-c','commit.gpgsign=false','commit','--quiet','--allow-empty','-m','fixture'],['git','-C',str(repo),'worktree','add','--detach',str(wt),'HEAD']]:
    r=subprocess.run(cmd,capture_output=True,encoding='utf-8');commands.append(dict(argv=cmd,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr));assert r.returncode==0
for shell in ['pwsh','powershell']:
    for name,path in [('clone',repo),('worktree',wt)]:
        cmd=[shell,'-NoProfile','-File',str(root/'RobWork/scripts/industrialrobot/pipeline-lock.ps1'),'-Action','acquire','-RepoRoot',str(path)]
        r=subprocess.run(cmd,capture_output=True,encoding='utf-8',errors='replace');commands.append(dict(argv=cmd,exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr))
        (out/f'{shell}-{name}.stdout.log').write_bytes(r.stdout.encode());(out/f'{shell}-{name}.stderr.log').write_bytes(r.stderr.encode())
        print(shell,name,r.returncode)
        if r.returncode==0:
            token=json.loads(r.stdout.strip().splitlines()[-1])['tickId'];release=[shell,'-NoProfile','-File',str(root/'RobWork/scripts/industrialrobot/pipeline-lock.ps1'),'-Action','release','-RepoRoot',str(path),'-Token',token];q=subprocess.run(release,capture_output=True,encoding='utf-8',errors='replace');commands.append(dict(argv=release,exitCode=q.returncode,stdout=q.stdout,stderr=q.stderr));assert q.returncode==0
(out/'report.json').write_bytes((json.dumps(commands,ensure_ascii=False,indent=2)+'\n').encode())
