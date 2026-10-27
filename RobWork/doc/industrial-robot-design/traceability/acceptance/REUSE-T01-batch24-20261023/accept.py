import json, subprocess, hashlib, sys
from pathlib import Path
BASE = Path(__file__).resolve().parent
ROOT = BASE/'repo'
FREEZE='e60d5d3c9a47a24010964295a935d42986a290e1'
PARENT='062522fdeb9215e6a604a11d95cfca99842523db'
D='RobWork/doc/industrial-robot-design/'
S='RobWork/scripts/industrialrobot/'
records=[]
def run(name, argv, expected=0, cwd=ROOT, check=True):
    p=subprocess.run([str(x) for x in argv],cwd=cwd,capture_output=True)
    (BASE/(name+'.stdout.log')).write_bytes(p.stdout)
    (BASE/(name+'.stderr.log')).write_bytes(p.stderr)
    rec={'name':name,'argv':[str(x) for x in argv],'exitCode':p.returncode,'expectedExit':expected,'passed':p.returncode==expected}
    records.append(rec); (BASE/'process-report.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    if check: assert rec['passed'],rec
    return p.stdout.decode('utf-8-sig',errors='replace'),p
def digest(t):return hashlib.sha256((t.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n').encode()).hexdigest()
def read(path):return (ROOT/path).read_text(encoding='utf-8-sig')
def blob(path,rev):return subprocess.check_output(['git','show',rev+':'+path],cwd=ROOT).decode('utf-8-sig')
def audit():
    matrix=D+'reuse/functions/foundation-governance.json'
    current=json.loads(read(matrix))['functions']; old=json.loads(blob(matrix,PARENT))['functions']
    added=[x for x in current if x['functionId'] not in {r['functionId'] for r in old}]
    assert len(added)==14
    assert [x for x in current if x['functionId'] in {r['functionId'] for r in old}]==old
    bindings=0; details=[]
    for row in added:
        assert row['status']==row['decision']=='pending' and row['targets']==[] and row['capabilityId']=='NONE'
        for s in row['sources']:
            text='\n'.join(read(s['path']).splitlines()[s['startLine']-1:s['endLine']])+'\n'
            assert digest(text)==s['sha256'],(row['functionId'],'source')
            assert s['symbol'] in text,(row['functionId'],'literal')
            bindings+=1
        for field in ['inputs','auditEvidence']:
            for b in row[field]:
                assert digest(read(b['path']))==b['sha256'],(row['functionId'],field,b['path'])
                bindings+=1
        details.append({'functionId':row['functionId'],'source':row['sources'],'pending':True})
    cat=json.loads(read(D+'reuse/catalog.json'))
    # Locate the candidate recursively, preserving the schema's actual collection name.
    def find(v):
        if isinstance(v,dict):
            if v.get('id')=='UTIL-THREAD':return v
            for z in v.values():
                found=find(z)
                if found:return found
        if isinstance(v,list):
            for z in v:
                found=find(z)
                if found:return found
    candidate=find(cat); assert candidate
    assert 'candidate' in json.dumps(candidate)
    before=json.loads(blob(D+'reuse/catalog.json',PARENT)); oldcandidate=find(before)
    def statuses(v):
        if isinstance(v,dict):return [(k,z) for k,z in v.items() if k=='status']+[x for z in v.values() for x in statuses(z)]
        if isinstance(v,list):return [x for z in v for x in statuses(z)]
        return []
    assert statuses(candidate)==statuses(oldcandidate)
    assert 'governanceLockBoundary' in json.dumps(candidate)
    changed=subprocess.check_output(['git','diff','--name-only',PARENT,FREEZE],cwd=ROOT).decode().splitlines()
    assert len(changed)==115,len(changed)
    scripts=[p for p in changed if p.startswith(S)]
    assert sorted(scripts)==[S+'pipeline-lock.ps1',S+'test-pipeline-pipeline.ps1']
    forbidden=[p for p in changed if p.startswith(('vcpkg/','RobWork/RobWork/','RobWork/RobWorkStudio/'))]
    assert not forbidden,forbidden
    a22=D+'traceability/acceptance/REUSE-T01-batch22-20261023/'
    manifest=json.loads(read(a22+'acceptance.json'))
    original=Path('C:/Users/zgl18/AppData/Local/Temp/ird-b22-accept-7c3d9afa')
    raw=[]
    archived=[dict(e) for e in manifest['logs']]
    for name in ['acceptance.json','acceptance.md','check-bindings.ps1']:
        archived.append({'path':name,'rawSha256':hashlib.sha256((original/name).read_bytes()).hexdigest()})
    for entry in archived:
        archive=subprocess.check_output(['git','show',FREEZE+':'+a22+entry['path']],cwd=ROOT); source=(original/entry['path']).read_bytes()
        actual=hashlib.sha256(archive).hexdigest(); want=entry['rawSha256']
        assert archive==source and actual==want,entry
        raw.append({'path':entry['path'],'rawSha256':actual,'originalByteEqual':True})
    assert len(raw)==27,len(raw)
    assert manifest['grants']==[] and manifest['fullTaskAcceptance']=='notGranted' and 'FAIL' in manifest['productTestVerdict']
    assert any(x.get('id')=='F-964' and x.get('status')=='open' for x in json.loads(read(D+'traceability/findings.json'))['findings'])
    result={'frozen':FREEZE,'parent':PARENT,'changedPaths':len(changed),'scriptPaths':scripts,'newPendingRows':len(added),'bindingChecks':bindings,'priorRowsUnchanged':len(old),'relations':details,'B22rawBindings':raw,'F964':'open'}
    (BASE/'binding-report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('audit: 14 pending rows; all source/literal/full-file bindings; B22 27 original raw files match',flush=True)
def locktests():
    fixture=BASE/'fixtures';fixture.mkdir()
    clone=fixture/'clone'; wt=fixture/'linked'; bad=fixture/'bad-root'; bad.mkdir(); (bad/'.git').mkdir()
    run('fixture-init',['git','init','--quiet',clone])
    run('fixture-commit',['git','-C',clone,'-c','user.name=Acceptance','-c','user.email=acceptance@example.invalid','-c','commit.gpgsign=false','commit','--quiet','--allow-empty','-m','fixture'])
    run('fixture-worktree',['git','-C',clone,'worktree','add','--quiet','--detach',wt,'HEAD'])
    script=ROOT/(S+'pipeline-lock.ps1'); before=ROOT/(D+'traceability/reuse/REUSE-T01/batch24/lock-before.ps1')
    def call(shell,name,script,repo,action,expected=0,token=None,json_expected=True):
        cmd=[shell,'-NoProfile','-File',script,'-RepoRoot',repo,'-Action',action]
        if token:cmd+=['-Token',token]
        out,p=run(shell+'-'+name,cmd,expected)
        if not json_expected:return out,p
        values=[json.loads(line) for line in out.strip().splitlines() if line.strip()]
        assert values,(shell,name,'missing JSON')
        return values[-1]
    for shell in ['pwsh','powershell']:
        # Original ordinary clone positive, linked worktree false-success symptom.
        first=call(shell,'before-clone-acquire',before,clone,'acquire'); token=first['tickId']; assert token
        assert call(shell,'before-clone-release',before,clone,'release',token=token)['state']=='released'
        out,p=call(shell,'before-worktree-acquire',before,wt,'acquire',json_expected=False)
        assert not out.strip() and b'System.IO' in p.stderr,(shell,'before symptom')
        for kind,repo in [('clone',clone),('worktree',wt)]:
            def act(name,action,expected=0,token=None):return call(shell,kind+'-'+name,script,repo,action,expected,token)
            first=act('acquire','acquire'); token=first['tickId']; assert token
            gitdir=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--absolute-git-dir']).decode().strip())
            lock=gitdir/'ird-pipeline-tick.lock';assert lock.is_file()
            assert act('busy','acquire',3)['tickId']==token
            assert act('wrong-renew','renew',4,'wrong')['state']=='not-owner'
            assert act('wrong-release','release',4,'wrong')['state']=='not-owner'
            assert act('held','status')['tickId']==token
            assert act('renew','renew',token=token)['state']=='renewed'
            assert act('release','release',token=token)['state']=='released';assert not lock.exists()
            assert act('none','status')['state']=='none'
            assert act('absent-renew','renew',1,token)['state']=='no-lock'
            # Expired lease takeover must create a different token and preserve old lock evidence.
            stale=act('stale-acquire','acquire'); oldtoken=stale['tickId']
            payload=json.loads(lock.read_text(encoding='utf-8-sig'));payload['leaseExpiresAt']='2000-01-01T00:00:00+08:00'
            lock.write_text(json.dumps(payload),encoding='utf-8')
            takeover=act('takeover','acquire'); newtoken=takeover['tickId']; assert newtoken!=oldtoken
            assert list(gitdir.glob('ird-pipeline-tick.lock.stale-*'))
            assert act('stale-renew','renew',4,oldtoken)['state']=='not-owner'
            assert act('stale-release','release',4,oldtoken)['state']=='not-owner'
            assert act('new-owner','status')['tickId']==newtoken
            assert act('new-release','release',token=newtoken)['state']=='released'
        for name,repo in [('missing-root',fixture/'missing'),('non-git-root',bad)]:
            out,p=call(shell,name,script,repo,'acquire',1,json_expected=False);assert not out.strip()
        print(shell+': original clone positive/worktree symptom and after clone/worktree lifecycle/fencing/bad roots passed',flush=True)
if __name__=='__main__':
    if sys.argv[1]=='audit':audit()
    else:locktests()
