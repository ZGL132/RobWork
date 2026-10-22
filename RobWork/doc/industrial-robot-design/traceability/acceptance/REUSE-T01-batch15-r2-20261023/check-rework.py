import json,pathlib,subprocess,hashlib,copy,re,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';d=repo/'RobWork/doc/industrial-robot-design';sha='a703b88225b1e094947b46723706b5be8af5159f';base='a78b41ff29261b17fc8379ee27e4afb1933ab827';original=pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/ird-accept-b15-20261023-011204-150869384');archive=d/'traceability/acceptance/REUSE-T01-batch15-r1-20261023'
def read(p):return p.read_text(encoding='utf-8-sig')
def load(p):return json.loads(read(p))
def save(n,x):(ev/n).write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
def git(*a):return subprocess.check_output(['git',*a],cwd=repo,encoding='utf-8')
def digest(p):return hashlib.sha256((read(p).rstrip('\n')+'\n').encode()).hexdigest()
assert git('rev-parse','HEAD').strip()==sha and git('rev-parse','HEAD^').strip()==base
assert git('status','--porcelain')=='';(ev/'git-status-start.log').write_text('');(ev/'frozen-sha.txt').write_text(sha+'\n');(ev/'comparison-base.txt').write_text(base+'\n')
rework=load(d/'traceability/reuse/REUSE-T01/batch15-rework.json');expected={x['functionId'] for x in load(original/'target-checks.json') if not x['match']};assert len(expected)==5 and set(rework['functions'])==expected and rework['addedTarget']=='sdurws_ird_ui_test' and rework['condition']=='if(TARGET sdurws)' and rework['findingId']=='B15-P2-01' and not rework['grantsChanged'] and not rework['productChanged'] and not rework['fullStagePass']
cmfile=repo/rework['cmake']['path'];cm=read(cmfile);assert digest(cmfile)==rework['cmake']['sha256'];assert re.search(r'if\(TARGET sdurws\)\s+target_sources\(sdurws_ird_ui_test PRIVATE[\s\S]*?plugin/QtTextBillboard\.cpp',cm)
cmLines=cm.splitlines();(ev/'cmake-evidence.log').write_text('\n'.join(f'{i+1}: {cmLines[i]}' for i in range(412,429))+'\n',encoding='utf-8')
current={x['functionId']:x for x in load(d/'reuse/functions/ui.json')['functions']};old=load(original/'rows.json');note='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/host-preview-audit.md';nd=digest(repo/note);checks=[];targets=[]
for before in old:
 r=current[before['functionId']];a=copy.deepcopy(before);b=copy.deepcopy(r)
 for row in [a,b]:
  for x in row['auditEvidence']:
   if x['path']==note:x['sha256']='<updated-note>'
 if r['functionId'] in expected:
  assert sorted(b['targets'])==sorted(a['targets']+['sdurws_ird_ui_test']);a['targets']=b['targets']
 assert a==b,r['functionId']
 refs=[x for x in r['auditEvidence'] if x['path']==note];assert len(refs)==1
 checks.append(dict(functionId=r['functionId'],recordedSha256=refs[0]['sha256'],computedSha256=nd,match=refs[0]['sha256']==nd,decision=r['decision'],status=r['status']))
 actual=next(x for x in load(original/'target-checks.json') if x['functionId']==r['functionId'])['actualDirectCompileTargets'];targets.append(dict(functionId=r['functionId'],actualDirectCompileTargets=actual,registered=r['targets'],match=sorted(actual)==sorted(r['targets'])))
assert len(checks)==24 and all(x['match'] and x['decision']=='pending' and x['status']=='pending' for x in checks) and all(x['match'] for x in targets)
allrefs=[]
for p in (d/'reuse/functions').glob('*.json'):
 for row in load(p)['functions']:
  for kind in ['inputs','auditEvidence']:
   for x in row.get(kind,[]):
    if x['path']==note:allrefs.append(dict(functionId=row['functionId'],kind=kind,match=x['sha256']==nd))
assert len(allrefs)==24 and all(x['match'] for x in allrefs);save('hash-checks.json',checks);save('all-note-references.json',allrefs);save('target-checks.json',targets)
archchecks=[]
for f in sorted(original.iterdir()):
 if not f.is_file():continue
 p=archive/f.name;a=f.read_bytes();b=p.read_bytes() if p.exists() else None;archchecks.append(dict(file=f.name,exists=p.is_file(),rawMatches=a==b,newlineMatches=b is not None and a.replace(b'\r\n',b'\n')==b.replace(b'\r\n',b'\n')))
extras=[f.name for f in archive.iterdir() if f.is_file() and not (original/f.name).exists()];assert len(archchecks)==50 and not extras and all(x['newlineMatches'] for x in archchecks);save('archive-checks.json',dict(files=archchecks,extras=extras));ar=load(archive/'acceptance.json');assert ar['verdict']=='FAIL' and ar['reviewedCommit']=='85e561539a4038e37231dcfcb506109a5a188151' and ar['findings'][0]['id']=='B15-P2-01' and not ar['wholeT01Pass']
perm=load(d/'reuse/permissions.json');assert perm['grants']==[]
pd=git('diff',base,sha,'--','RobWork/RobWorkStudio/src','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg');assert pd=='';(ev/'product-upstream-diff.log').write_text(pd)
originalDiff=git('diff','85e561539a4038e37231dcfcb506109a5a188151',sha,'--',rework['cmake']['path']);assert originalDiff=='';(ev/'original-cmake-diff.log').write_text(originalDiff)
ad=git('diff',base,sha,'--','RobWork/doc/industrial-robot-design/reuse/permissions.json','RobWork/doc/industrial-robot-design/ARCHITECTURE.md');assert ad=='';(ev/'authority-permission-diff.log').write_text(ad)
paths=['RobWork/doc/industrial-robot-design/reuse/functions/ui.json','RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01']
p=subprocess.run(['git','diff','--check',base,sha,'--',*paths],cwd=repo,capture_output=True,encoding='utf-8');assert p.returncode==0;(ev/'diff-check.log').write_text(p.stdout+p.stderr,encoding='utf-8');(ev/'diff-check.exit.txt').write_text(str(p.returncode)+'\n')
facts=dict(reviewedCommit=sha,comparisonBase=base,correctedTargetRows=5,relatedNoteRows=24,noteDigest=nd,hashMismatches=0,targetSetsMatched=24,archiveFiles=50,archiveRawMatches=sum(x['rawMatches'] for x in archchecks),archiveNewlineOnly=sum(x['newlineMatches'] and not x['rawMatches'] for x in archchecks),archiveMismatches=0,originalFailPreserved=True,productAndUpstreamUnchanged=True,authorityAndPermissionsUnchanged=True,originalCMakeUnchanged=True,grants=0,allRowsPending=True);save('facts.json',facts);print(json.dumps(facts,ensure_ascii=False,indent=2))
