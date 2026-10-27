import json,pathlib,subprocess,hashlib,copy,sys,re
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';d=repo/'RobWork/doc/industrial-robot-design';base='63e86ba5a633e93c9027165756298d76536166b9';sha='6925608ccbc5c7764dc983f7e7e0276f1f12d55f'
original=pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/ird-accept-b12-20261023-004134-313235924'); archive=d/'traceability/acceptance/REUSE-T01-batch12-r1-20261023'
def read(p):return p.read_text(encoding='utf-8-sig')
def load(p):return json.loads(read(p))
def save(n,x):(ev/n).write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
def git(*a):return subprocess.check_output(['git',*a],cwd=repo,encoding='utf-8')
def digest(p):return hashlib.sha256((read(p).rstrip('\n')+'\n').encode()).hexdigest()
assert git('rev-parse','HEAD').strip()==sha and git('rev-parse','HEAD^').strip()==base
(ev/'frozen-sha.txt').write_text(sha+'\n');(ev/'comparison-base.txt').write_text(base+'\n')
status=git('status','--porcelain');assert status=='';(ev/'git-status-start.log').write_text(status)
oldrows=load(original/'new-rows.json'); assert len(oldrows)==34; current={}
for unit in ['testkit','workflow']:
 for r in load(d/f'reuse/functions/{unit}.json')['functions']:current[r['functionId']]=r
note='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/test-tools-lifecycle-audit.md';newdigest=digest(repo/note);checks=[];semchanges=[]
for old in oldrows:
 r=current[old['functionId']]; refs=[x for x in r['auditEvidence'] if x['path']==note];assert len(refs)==1
 a=copy.deepcopy(old);b=copy.deepcopy(r)
 for row in [a,b]:
  for x in row['auditEvidence']:
   if x['path']==note:x['sha256']='<updated-note>'
 if old['functionId']=='workflow.CloseSupervisor.running':
  semchanges.append(dict(functionId=r['functionId'],old=a.pop('differences'),new=b.pop('differences')))
 assert a==b,old['functionId']
 checks.append(dict(functionId=r['functionId'],recordedSha256=refs[0]['sha256'],computedSha256=newdigest,match=refs[0]['sha256']==newdigest,decision=r['decision'],status=r['status']))
assert all(x['match'] and x['decision']=='pending' and x['status']=='pending' for x in checks)
save('hash-checks.json',checks);save('semantic-change.json',semchanges)
archchecks=[]
for f in sorted(original.iterdir()):
 if not f.is_file():continue
 p=archive/f.name; a=f.read_bytes();b=p.read_bytes() if p.exists() else None
 archchecks.append(dict(file=f.name,exists=p.is_file(),rawMatches=a==b,newlineMatches=b is not None and a.replace(b'\r\n',b'\n')==b.replace(b'\r\n',b'\n')))
extras=[f.name for f in archive.iterdir() if f.is_file() and not (original/f.name).exists()]
save('archive-checks.json',dict(files=archchecks,extraFiles=extras));assert not extras and all(x['newlineMatches'] for x in archchecks)
archreport=load(archive/'acceptance.json');assert archreport['verdict']=='FAIL' and archreport['reviewedCommit']=='d38e4472ec403d9d9f1b7201a0f8e2b4b83f7a0c' and archreport['findings'][0]['id']=='B12-P2-01' and archreport['wholeT01Pass']==False
search=load(d/'traceability/reuse/REUSE-T01/batch12-rework.json');cmd=search['search']['arguments'];assert cmd[3]==r'CreateProcess|JobObject|fork\s*\(|popen|spawn|processrunner|QProcess'
p=subprocess.run(cmd,cwd=repo,capture_output=True,encoding='utf-8');(ev/'rework-search.stdout.log').write_text(p.stdout,encoding='utf-8');(ev/'rework-search.stderr.log').write_text(p.stderr,encoding='utf-8');s=dict(arguments=cmd,exitCode=p.returncode,recordedExitCode=search['search']['exitCode'],stdoutMatches=sorted(p.stdout.splitlines())==sorted(search['search']['stdout'].splitlines()),stderrMatches=p.stderr==search['search']['stderr'],hits=len(p.stdout.splitlines()),correctForkPattern=True);save('search-replay.json',s);assert p.returncode==search['search']['exitCode']==0 and s['stdoutMatches'] and s['stderrMatches']
prod=git('diff',base,sha,'--','RobWork/RobWorkStudio/src','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg');assert prod=='';(ev/'product-upstream-diff.log').write_text(prod)
paths=['RobWork/doc/industrial-robot-design/reuse/functions/testkit.json','RobWork/doc/industrial-robot-design/reuse/functions/workflow.json','RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01']
p=subprocess.run(['git','diff','--check',base,sha,'--',*paths],cwd=repo,capture_output=True,encoding='utf-8');(ev/'scoped-diff-check.log').write_text(p.stdout+p.stderr,encoding='utf-8');(ev/'scoped-diff-check.exit.txt').write_text(str(p.returncode)+'\n');assert p.returncode==0
sources=[]
for rel in ['workflow/src/CloseSupervisor.cpp','workflow/include/sdurws/ird/workflow/CloseSupervisor.hpp']:
 p=repo/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot'/rel
 sources.extend(f'{rel}:{i}: {line}' for i,line in enumerate(read(p).splitlines(),1) if re.search(r'm_running|m_storeClosedObserved|m_abandonDiagObserved|m_gaveUp|历史事实|当前运行态',line))
(ev/'getter-source-evidence.log').write_text('\n'.join(sources)+'\n',encoding='utf-8')
facts=dict(reviewedCommit=sha,comparisonBase=base,relatedRows=34,newNoteDigest=newdigest,failedHashes=0,archiveFiles=len(archchecks),archiveRawMatches=sum(x['rawMatches'] for x in archchecks),archiveNewlineOnly=sum(x['newlineMatches'] and not x['rawMatches'] for x in archchecks),archiveContentMismatches=0,originalFailPreserved=True,searchHits=s['hits'],sourceDiffEmpty=True,scopedDiffCheckExit=0)
save('facts.json',facts);print(json.dumps(facts,ensure_ascii=False,indent=2))
