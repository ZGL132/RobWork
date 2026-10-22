import pathlib,json,subprocess,hashlib,collections,os
R=pathlib.Path(__file__).parent; repo=R/'repo-isolated'; os.chdir(repo)
def norm(b): return b.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n'
def digest(b): return hashlib.sha256(norm(b).encode()).hexdigest()
def read(p): return json.loads((repo/p).read_text(encoding='utf-8-sig'))
path='RobWork/doc/industrial-robot-design/reuse/functions/project.json'
old=json.loads(subprocess.check_output(['git','show','daee4f8905587b05cd1bab88bb570b8dc240097c:'+path]))['functions']; cur=read(path)['functions']
# Relations may share functionId, compare full old row identity.
added=[r for r in cur if r not in old]; removed=[r for r in old if r not in cur]
checks=[]; view=[]
for row in added:
  fid=row['functionId']; view.append('\n=== '+fid+' ===\n'+json.dumps({k:v for k,v in row.items() if k not in ('inputs','auditEvidence')},ensure_ascii=False,indent=2))
  for k in ('inputs','auditEvidence'):
    for b in row.get(k,[]): checks.append({'functionId':fid,'kind':k,'path':b['path'],'match':digest((repo/b['path']).read_bytes())==b['sha256']})
  for s in row['sources']:
    lines=norm((repo/s['path']).read_bytes()).splitlines(); seg='\n'.join(lines[s['startLine']-1:s['endLine']])+'\n'
    checks.append({'functionId':fid,'kind':'source','path':s['path'],'lines':[s['startLine'],s['endLine']],'match':digest(seg.encode())==s['sha256']})
    view.append('\n'.join(f'{i+1}: {lines[i]}' for i in range(max(0,s['startLine']-4),min(len(lines),s['endLine']+3))))
(R/'all47-source-review.txt').write_text('\n'.join(view),encoding='utf-8')
(R/'binding-checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
defs=read('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch20-definitions.json')
dc=[{'functionId':d['functionId'],'match':any(r['functionId']==d['functionId'] and d['source'] in r['sources'] for r in added)} for d in defs]
(R/'definition-checks.json').write_text(json.dumps(dc,indent=2),encoding='utf-8')
search=[]
for i,s in enumerate(read('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch20-search.json')['results'],1):
  p=subprocess.run(s['arguments'],capture_output=True,encoding='utf-8'); (R/f'search-{i}.log').write_text(p.stdout,encoding='utf-8')
  search.append({'command':s['arguments'],'exitCode':p.returncode,'expectedExit':s['exitCode'],'sortedLinesMatch':sorted(p.stdout.splitlines())==sorted(s['stdout'].splitlines()),'stderr':p.stderr})
(R/'search-replay.json').write_text(json.dumps(search,indent=2),encoding='utf-8')
a17='RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch17-20261023'; manifest=read(a17+'/artifact-manifest.json'); archives=[]
for f in manifest['files']:
  b=(repo/a17/f['path']).read_bytes(); archives.append({'path':a17+'/'+f['path'],'bytes':len(b),'rawMatch':len(b)==f['bytes'] and hashlib.sha256(b).hexdigest()==f['rawSha256']})
(R/'archive-byte-checks.json').write_text(json.dumps(archives,indent=2),encoding='utf-8')
summary={'oldRows':len(old),'newRows':len(cur),'added':len(added),'removedOrChanged':len(removed),'allAddedPending':all(r['decision']==r['status']=='pending' for r in added),'bindingChecks':len(checks),'bindingFailures':[c for c in checks if not c['match']],'definitions':len(defs),'definitionFailures':[c for c in dc if not c['match']],'searches':search,'archiveB17':{'checked':len(archives),'failures':[a for a in archives if not a['rawMatch']]},'statusCounts':dict(collections.Counter(r['status'] for r in added))}
(R/'metadata-audit.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k!='searches'},ensure_ascii=False));print('search replay',all(s['exitCode']==s['expectedExit'] and s['sortedLinesMatch'] for s in search))
