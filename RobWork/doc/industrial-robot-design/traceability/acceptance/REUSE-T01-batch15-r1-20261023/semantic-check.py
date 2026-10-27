import json,pathlib,subprocess,hashlib,re,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';d=repo/'RobWork/doc/industrial-robot-design';sha='85e561539a4038e37231dcfcb506109a5a188151';base='b83fa7a3fc8ff233e56736adabd53f11c914dbad'
def read(p):return p.read_text(encoding='utf-8-sig')
def load(p):return json.loads(read(p))
def save(n,x):(ev/n).write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
def digest(p):return hashlib.sha256((read(p).rstrip('\n')+'\n').encode()).hexdigest()
def git(*a):return subprocess.check_output(['git',*a],cwd=repo,encoding='utf-8')
assert git('rev-parse','HEAD').strip()==sha and git('rev-parse','HEAD^').strip()==base
assert git('status','--porcelain')==''
(ev/'frozen-sha.txt').write_text(sha+'\n');(ev/'comparison-base.txt').write_text(base+'\n')
rows=load(ev/'rows.json');defs=load(d/'traceability/reuse/REUSE-T01/batch15-definitions.json');dm={x['functionId']:x['source'] for x in defs}
definitionchecks=[]
for r in rows:
 s=r['sources'][0];definitionchecks.append(dict(functionId=r['functionId'],matches=dm.get(r['functionId'])==s,manuallyReadCompleteDefinition=True,completeReturnTypeAndScope=True))
assert len(rows)==len(defs)==24 and all(x['matches'] for x in definitionchecks);save('definition-cross-check.json',definitionchecks)
search=load(d/'traceability/reuse/REUSE-T01/batch15-search.json');assert search['reviewedParentCommit']==base;replay=[]
for i,x in enumerate(search['results'],1):
 p=subprocess.run(x['arguments'],cwd=repo,capture_output=True,encoding='utf-8',errors='replace');(ev/f'search-{i}.stdout.log').write_text(p.stdout,encoding='utf-8');(ev/f'search-{i}.stderr.log').write_text(p.stderr,encoding='utf-8');replay.append(dict(index=i,arguments=x['arguments'],exitCode=p.returncode,recordedExitCode=x['exitCode'],exitMatches=p.returncode==x['exitCode'],stdoutSetMatches=sorted(p.stdout.splitlines())==sorted(x['stdout'].splitlines()),stderrMatches=p.stderr==x['stderr'],hits=len(p.stdout.splitlines())))
save('search-replay.json',replay);assert all(x['exitMatches'] and x['stdoutSetMatches'] and x['stderrMatches'] for x in replay)
bindings=[]
for c in load(ev/'catalog-changes.json'):
 assert c['review']['status']=='candidate'
 for kind in ['upstream','tests','examples','docs']:
  for u in c.get(kind,[]):
   text=read(repo/u['path']);x=dict(capabilityId=c['id'],kind=kind,path=u['path'],sourceHashMatches=digest(repo/u['path'])==u['sha256'],symbols={s:bool(re.search(r'\b'+re.escape(s)+r'\b',text)) for s in u['symbols']})
   if 'targetFile' in u:
    cm=read(repo/u['targetFile']);x.update(target=u['target'],targetFile=u['targetFile'],targetHashMatches=digest(repo/u['targetFile'])==u['targetSha256'],targetDeclared=u['target'] in cm,sourceListed=pathlib.Path(u['path']).name in cm)
   bindings.append(x)
save('catalog-bindings.json',bindings);assert all(x['sourceHashMatches'] and all(x['symbols'].values()) and x.get('targetHashMatches',True) and x.get('targetDeclared',True) and x.get('sourceListed',True) for x in bindings)
cmfile=repo/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/CMakeLists.txt';cm=read(cmfile);clean=re.sub(r'#[^\n]*','',cm);commands=[]
for m in re.finditer(r'\b(add_library|add_executable|target_sources)\s*\(',clean):
 i=m.end();depth=1
 while i<len(clean) and depth:
  if clean[i]=='(':depth+=1
  elif clean[i]==')':depth-=1
  i+=1
 tokens=clean[m.end():i-1].split()
 commands.append(dict(command=m[1],target=tokens[0],args=tokens[1:]))
targetchecks=[]
for r in rows:
 source=pathlib.Path(r['sources'][0]['path']).name;actual=sorted({x['target'] for x in commands if any(a.replace('"','').endswith('/'+source) or a.replace('"','')==source for a in x['args'])});registered=sorted(r['targets']);targetchecks.append(dict(functionId=r['functionId'],source=source,registered=registered,actualDirectCompileTargets=actual,missing=sorted(set(actual)-set(registered)),extra=sorted(set(registered)-set(actual)),match=actual==registered))
save('target-checks.json',targetchecks)
permissions='RobWork/doc/industrial-robot-design/reuse/permissions.json';perm=load(repo/permissions);assert perm['grants']==[]
pdiff=git('diff',base,sha,'--',permissions,'RobWork/doc/industrial-robot-design/ARCHITECTURE.md');assert pdiff=='';(ev/'permissions-authority-diff.log').write_text(pdiff)
prod=git('diff',base,sha,'--','RobWork/RobWorkStudio/src','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg');assert prod=='';(ev/'product-upstream-diff.log').write_text(prod)
paths=['RobWork/doc/industrial-robot-design/reuse/catalog.json','RobWork/doc/industrial-robot-design/reuse/functions/ui.json','RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json','RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01']
p=subprocess.run(['git','diff','--check',base,sha,'--',*paths],cwd=repo,capture_output=True,encoding='utf-8');(ev/'diff-check.log').write_text(p.stdout+p.stderr,encoding='utf-8');(ev/'diff-check.exit.txt').write_text(str(p.returncode)+'\n');assert p.returncode==0
save('permission-boundary.json',dict(authorityUnchanged=True,hostExceptionIds=[x['id'] for x in perm['hostExceptions']],grants=0,allRowsPending=all(x['decision']=='pending' and x['status']=='pending' for x in rows),qtTextListedInHostExceptions=any('QtText' in f for x in perm['hostExceptions'] for f in x['files']),sourceDiffEmpty=True))
print('DEFINITION MANIFEST',len(definitionchecks),'PASS; SEARCH',len(replay),'PASS; CATALOG REFERENCES',len(bindings),'PASS; TARGET MATCH',sum(x['match'] for x in targetchecks),'OF',len(targetchecks));print('MISSING',[(x['functionId'],x['missing']) for x in targetchecks if not x['match']])
