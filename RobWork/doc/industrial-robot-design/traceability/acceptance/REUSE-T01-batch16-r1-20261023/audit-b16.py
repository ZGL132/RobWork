import pathlib, json, hashlib, subprocess, collections, re
B=pathlib.Path(__file__).parent; R=B/'repo'; D=R/'RobWork/doc/industrial-robot-design'
def load(p): return json.loads(pathlib.Path(p).read_text(encoding='utf-8-sig'))
def write(n,v): (B/n).write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def norm(t): return t.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n'
def digest(t): return hashlib.sha256(norm(t).encode()).hexdigest()
def git(*a): return subprocess.check_output(['git','-C',str(R),*a],encoding='utf-8').strip()
defs=load(D/'traceability/reuse/REUSE-T01/batch16-definitions.json'); ids=[d['functionId'] for d in defs]
matrices={}; oldrows={}; rows={}
for f in (D/'reuse/functions').glob('*.json'):
 m=load(f); matrices[f.stem]=m
 rel=f.relative_to(R).as_posix(); prior=subprocess.run(['git','-C',str(R),'show','HEAD^:'+rel],capture_output=True,encoding='utf-8'); old=json.loads(prior.stdout) if prior.returncode==0 else {'functions':[]}
 for x in old['functions']: oldrows[x['functionId']]=x
 for x in m['functions']: rows[x['functionId']]=x
new=[k for k in rows if k not in oldrows]; changed=[k for k in oldrows if rows.get(k)!=oldrows[k]]
checks=[]; boundaries=[]; targets=[]
for d in defs:
 x=rows[d['functionId']]; s=d['source']; text=(R/s['path']).read_text(encoding='utf-8-sig'); lines=text.splitlines()
 boundaries.append({'functionId':x['functionId'],'sourceMatchesDefinition':x['sources']==[s],'start':lines[s['startLine']-1],'end':lines[s['endLine']-1], 'qualifiedSymbol':s['qualifiedSymbol']})
 for kind in ['sources','inputs','auditEvidence']:
  for b in x.get(kind,[]):
   t=(R/b['path']).read_text(encoding='utf-8-sig')
   if kind=='sources': t='\n'.join(t.splitlines()[b['startLine']-1:b['endLine']])
   actual=digest(t); checks.append({'functionId':x['functionId'],'kind':kind,'path':b['path'],'pass':actual==b['sha256'],'expected':b['sha256'],'actual':actual})
 cmakes=[b['path'] for b in x['inputs'] if b['path'].endswith('CMakeLists.txt')]
 for target in x['targets']:
  matches=[p for p in cmakes if re.search(r'\b'+re.escape(target)+r'\b',(R/p).read_text(encoding='utf-8-sig'))]
  targets.append({'functionId':x['functionId'],'target':target,'cmakeMatches':matches,'targetRelation':x.get('targetRelation','compiled-test-helper')})
write('hash-checks.json',checks); write('definition-checks.json',boundaries); write('target-checks.json',targets)
search=load(D/'traceability/reuse/REUSE-T01/batch16-search.json'); searches=[]
for i,c in enumerate(search['results']):
 p=subprocess.run(c['arguments'],cwd=R,capture_output=True,encoding='utf-8'); out=p.stdout; err=p.stderr
 searches.append({'index':i,'arguments':c['arguments'],'exitCode':p.returncode,'exitMatches':p.returncode==c['exitCode'],'stdoutMatches':out.replace('\r\n','\n')==c['stdout'].replace('\r\n','\n'),'stdoutSetMatches':sorted(out.splitlines())==sorted(c['stdout'].splitlines()),'stderrMatches':err.replace('\r\n','\n')==c['stderr'].replace('\r\n','\n')})
 (B/f'search-{i}.log').write_text(out+err,encoding='utf-8')
write('search-replays.json',searches)
roots={
 'REUSE-T01-batch14-r2-20261023':pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/ird-accept-b14-r2-844c099516cf4504831b94667d217371'),
 'REUSE-T01-batch15-r2-20261023':pathlib.Path('C:/Users/zgl18/AppData/Local/Temp/ird-accept-b15-r2-20261023-012138-1947673312'),
}
arcbase=D/'traceability/acceptance'; cov=arcbase/'REUSE-T01-full-coverage-20261023'
for candidate in pathlib.Path('C:/Users/zgl18/AppData/Local/Temp').glob('*coverage*'):
 if (candidate/'coverage.json').is_file() and norm((candidate/'coverage.json').read_text(encoding='utf-8-sig'))==norm((cov/'coverage.json').read_text(encoding='utf-8-sig')):
  roots[cov.name]=candidate; break
archives=[]
for name,original in roots.items():
 for f in (arcbase/name).rglob('*'):
  if not f.is_file():continue
  rel=f.relative_to(arcbase/name); o=original/rel
  raw=o.exists() and o.read_bytes()==f.read_bytes(); nl=False
  if not raw and o.exists():
   try:nl=o.read_text(encoding='utf-8-sig').replace('\r\n','\n')==f.read_text(encoding='utf-8-sig').replace('\r\n','\n')
   except UnicodeError:pass
  archives.append({'archive':name,'file':str(rel),'original':str(o),'rawMatch':raw,'newlineOnly':not raw and nl,'pass':raw or nl})
write('archive-checks.json',archives)
files=git('diff','--name-only','HEAD^','HEAD').splitlines(); (B/'changed-files.log').write_text('\n'.join(files)+'\n',encoding='utf-8')
ex=[x for x in files if '/traceability/acceptance/' in x and (x.endswith('.log') or x.endswith('.frozen.txt'))]
(B/'diff-check-exclusions.log').write_text('\n'.join(ex)+'\n',encoding='utf-8')
included=[x for x in files if x not in ex]
p=subprocess.run(['git','-C',str(R),'diff','--check','HEAD^','HEAD','--',*included],capture_output=True,encoding='utf-8')
(B/'scoped-diff-check.log').write_text(p.stdout+p.stderr,encoding='utf-8'); (B/'scoped-diff-check.exit.txt').write_text(str(p.returncode)+'\n')
code=git('diff','--name-only','HEAD^','HEAD','--','RobWork/RobWorkStudio/src','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg')
(B/'product-upstream-diff.log').write_text(code,encoding='utf-8'); (B/'git-status-end.log').write_text(git('status','--porcelain=v1'),encoding='utf-8')
facts={'reviewedCommit':git('rev-parse','HEAD'),'reviewedParentCommit':git('rev-parse','HEAD^'),'newRelations':len(new),'definitions':len(defs),'newExactlyMatchesDefinitions':set(new)==set(ids),'previousRowsChanged':changed,'unitCounts':dict(collections.Counter(rows[x]['ownerUnit'] for x in ids)),'hashChecks':len(checks),'hashFailures':sum(not x['pass'] for x in checks),'definitionFailures':sum(not x['sourceMatchesDefinition'] for x in boundaries),'allNewPending':all(rows[x]['decision']=='pending' and rows[x]['status']=='pending' for x in ids),'searchReplays':len(searches),'searchFailures':sum(not(x['exitMatches'] and x['stdoutSetMatches'] and x['stderrMatches']) for x in searches),'searchOrderOnly':sum(not x['stdoutMatches'] and x['stdoutSetMatches'] for x in searches),'targetChecks':len(targets),'targetFailures':sum(not x['cmakeMatches'] for x in targets),'archiveGroups':len(roots),'archiveFiles':len(archives),'archiveRawMatches':sum(x['rawMatch'] for x in archives),'archiveNewlineOnly':sum(x['newlineOnly'] for x in archives),'archiveMismatches':sum(not x['pass'] for x in archives),'scopedDiffCheckExit':p.returncode,'sourceDiffEmpty':not code,'clean':not git('status','--porcelain=v1'),'allRelations':sum(len(m['functions']) for m in matrices.values()),'distinctFunctionIds':len(rows),'closedFiles':0}
write('facts.json',facts);print(json.dumps(facts,ensure_ascii=False,indent=2))
