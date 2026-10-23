import pathlib,json,hashlib,subprocess,collections
B=pathlib.Path(__file__).parent;R=B/'repo';D=R/'RobWork/doc/industrial-robot-design';BASE='435dbdb716b9eb86d0932a478eb086642206ac76'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(n,v):(B/n).write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def norm(t):return t.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n'
def h(t):return hashlib.sha256(norm(t).encode()).hexdigest()
def git(*a):return subprocess.check_output(['git','-C',str(R),*a],encoding='utf-8').strip()
defs=load(D/'traceability/reuse/REUSE-T01/batch16-definitions.json');cpp=[x for x in defs if x['source']['path'].endswith('.cpp')]; ids=[x['functionId'] for x in cpp]
rework=load(D/'traceability/reuse/REUSE-T01/batch16-rework.json');allrows={};comparisons=[];bindings=[];hashes=[]
for f in (D/'reuse/functions').glob('*.json'):
 if f.stem not in ['dynamics','modeling','optimization','foundation-governance']:continue
 rel=f.relative_to(R).as_posix();old=json.loads(git('show',BASE+':'+rel));new=load(f)
 oldf={x['functionId']:x for x in old['functions']};newf={x['functionId']:x for x in new['functions']}
 assert len(old['functions'])==len(new['functions']) and set(oldf)==set(newf),f
 for fid,row in newf.items():
  allrows[fid]=row;expected=json.loads(json.dumps(oldf[fid]))
  if fid in ids:
   src=row['sources'][0];path=src['path'];matches=[x for x in row['inputs'] if x['path']==path]
   assert len(matches)==1,(fid,matches)
   expected['inputs'].append(matches[0]);bindings.append({'functionId':fid,'input':matches[0]})
  comparisons.append({'functionId':fid,'onlyExpectedChange':expected==row,'sourcesUnchanged':oldf[fid]['sources']==row['sources'],'changed':oldf[fid]!=row})
 for x in new['functions']:
  if x['functionId'] not in ids:continue
  for kind in ['sources','inputs','auditEvidence']:
   for b in x[kind]:
    t=(R/b['path']).read_text(encoding='utf-8-sig')
    if kind=='sources':t='\n'.join(t.splitlines()[b['startLine']-1:b['endLine']])
    actual=h(t);hashes.append({'functionId':x['functionId'],'kind':kind,'path':b['path'],'expected':b['sha256'],'actual':actual,'pass':b['sha256']==actual})
write('matrix-change-checks.json',comparisons);write('hash-checks.json',hashes);write('binding-checks.json',bindings)
assert sorted(rework['bindings'],key=lambda x:x['functionId'])==sorted(bindings,key=lambda x:x['functionId'])
cases=[('dynamics.StatisticsTest.expectNearRel','constexpr double kGoldenRelTol = 1e-12;','constexpr double kGoldenRelTol = 1e-6;'),('dynamics.StatisticsTest.prismAxialForce','constexpr double kPrismMass = 1.0;','constexpr double kPrismMass = 2.0;'),('dynamics.StatisticsTest.revTorque','constexpr double kRevMass = 0.5;','constexpr double kRevMass = 1.5;'),('dynamics.StatisticsTest.makePrismRevModel','constexpr double kPrismMass = 1.0;','constexpr double kPrismMass = 2.0;'),('modeling.XacroExpandTest.makeBasicSource','<xacro:property name="prefix" value="arm"/>','<xacro:property name="prefix" value="changed"/>')]
probes=[]
for fid,before,after in cases:
 row=allrows[fid];s=row['sources'][0];original=(R/s['path']).read_text(encoding='utf-8-sig');assert original.count(before)==1
 mutant=original.replace(before,after); src=lambda t:'\n'.join(t.splitlines()[s['startLine']-1:s['endLine']]);b=next(x for x in row['inputs'] if x['path']==s['path'])
 probes.append({'functionId':fid,'before':before,'after':after,'sourceSectionHashUnchanged':h(src(original))==h(src(mutant)),'originalFullSourceHashMatched':h(original)==b['sha256'],'mutantFullSourceHashRejected':h(mutant)!=b['sha256'],'execution':'仅内存替换；未写仓库文件，未执行C++或门禁变异运行'})
write('freshness-probe.json',probes)
ars=[]; roots={}
for a in rework['archives']:
 arc=R/a['path']; report=load(arc/'acceptance.json'); original=pathlib.Path(report['reportRoot']);roots[a['path']]=original
 actualfiles=sorted(x.relative_to(arc).as_posix() for x in arc.rglob('*') if x.is_file());assert actualfiles==sorted(a['files'])
 for rel in actualfiles:
  f=arc/rel;o=original/rel;raw=o.is_file() and f.read_bytes()==o.read_bytes();nl=False
  if o.is_file() and not raw:
   try:nl=f.read_text(encoding='utf-8-sig').replace('\r\n','\n')==o.read_text(encoding='utf-8-sig').replace('\r\n','\n')
   except UnicodeError:pass
  ars.append({'archive':a['path'],'file':rel,'original':str(o),'rawMatch':raw,'newlineOnly':not raw and nl,'pass':raw or nl})
write('archive-checks.json',ars)
original=load(R/rework['originalAcceptance']);assert original['result']=='FAIL' and original['reviewedCommit']==BASE and original['findings'][0]['id']=='B16-P2-01' and original['findings'][0]['status']=='OPEN'
gen=load(D/'traceability/acceptance/REUSE-T01-batch16-generators-20261023/acceptance.json');assert gen['verdict']=='PASS' and gen['reviewedCommit']==BASE
files=git('diff','--name-only','HEAD^','HEAD').splitlines();(B/'changed-files.log').write_text('\n'.join(files)+'\n',encoding='utf-8');ex=[f for f in files if '/traceability/acceptance/' in f and f.endswith('.log')];(B/'diff-check-exclusions.log').write_text('\n'.join(ex)+'\n',encoding='utf-8')
p=subprocess.run(['git','-C',str(R),'diff','--check','HEAD^','HEAD','--',*[f for f in files if f not in ex]],capture_output=True,encoding='utf-8');(B/'scoped-diff-check.log').write_text(p.stdout+p.stderr,encoding='utf-8');(B/'scoped-diff-check.exit.txt').write_text(str(p.returncode)+'\n')
code=git('diff','--name-only',BASE,'HEAD','--','RobWork/RobWorkStudio/src','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg');(B/'product-upstream-diff.log').write_text(code,encoding='utf-8');status=git('status','--porcelain=v1');(B/'git-status-end.log').write_text(status,encoding='utf-8')
facts={'reviewedCommit':git('rev-parse','HEAD'),'reviewedParentCommit':git('rev-parse','HEAD^'),'comparisonBase':BASE,'affectedRelations':len(cpp),'fullSourceBindings':len(bindings),'distinctSourceFiles':len(set(x['input']['path'] for x in bindings)),'all22Bound':len(cpp)==22 and len(bindings)==22,'bindingManifestMatches':True,'matrixChangeFailures':sum(not x['onlyExpectedChange'] for x in comparisons),'changedMatrixRows':sum(x['changed'] for x in comparisons),'sourcesAllUnchanged':all(x['sourcesUnchanged'] for x in comparisons),'hashChecks':len(hashes),'hashFailures':sum(not x['pass'] for x in hashes),'originalProbeCases':len(probes),'originalProbesNowRejected':sum(x['mutantFullSourceHashRejected'] and x['sourceSectionHashUnchanged'] and x['originalFullSourceHashMatched'] for x in probes),'archiveGroups':len(roots),'archiveFiles':len(ars),'archiveRawMatches':sum(x['rawMatch'] for x in ars),'archiveNewlineOnly':sum(x['newlineOnly'] for x in ars),'archiveMismatches':sum(not x['pass'] for x in ars),'originalFailPreserved':True,'generatorPassPreserved':True,'sourceDiffEmpty':not code,'scopedDiffCheckExit':p.returncode,'clean':not status}
write('facts.json',facts);print(json.dumps(facts,ensure_ascii=False,indent=2))
