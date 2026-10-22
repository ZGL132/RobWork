import json,subprocess,pathlib,hashlib,sys,collections,re
out=pathlib.Path(sys.argv[1]); repo=out/'repo-copy'; base='7c263e78683a74b5532e6f99e7647a02b6f39ea7'; matrix='RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json'
def norm(t):return t.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n'
def digest(t):return hashlib.sha256(norm(t).encode()).hexdigest()
def read(p):return (repo/p).read_text(encoding='utf-8-sig')
a=json.loads(subprocess.check_output(['git','show',base+':'+matrix],cwd=repo)); b=json.loads(read(matrix)); old={x['functionId']:x for x in a['functions']}; new={x['functionId']:x for x in b['functions']}; added=[x for k,x in new.items() if k not in old]; changed=[x for k,x in new.items() if k in old and x!=old[k]]
errors=[]; checked=collections.Counter(); regions=[]
for f in added+changed:
 if f['status']!='pending' or f['decision']!='pending' or f['targets']!=[] or f['usageRole']!='owner' or f['capabilityId']!='NONE':errors.append(f['functionId']+':authority')
 for s in f['sources']:
  lines=read(s['path']).splitlines(); text='\n'.join(lines[s['startLine']-1:s['endLine']]); checked['regions']+=1
  if digest(text)!=s['sha256'] or s['symbol'] not in text:errors.append(f['functionId']+':source')
  regions.append(dict(id=f['functionId'],path=s['path'],start=s['startLine'],end=s['endLine'],symbol=s['symbol'],requirements=f['requirements'],permissionRef=f['permissionRef']))
 for field in ['inputs','auditEvidence']:
  for x in f.get(field,[]):
   checked[field]+=1
   if digest(read(x['path']))!=x['sha256']:errors.append(f['functionId']+':'+field+':'+x['path'])
all_functions=[]
for p in (repo/'RobWork/doc/industrial-robot-design/reuse/functions').glob('*.json'):all_functions.extend(json.loads(p.read_text(encoding='utf-8-sig'))['functions'])
reports={}; evidence=repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch27'
for rel in ['context-red/report.json','context-green/report.json','index/report.json','lite-green-r4-0/governance-lite-report.json','lite-green-r4-1/governance-lite-report.json']:
 data=json.loads((evidence/rel).read_text(encoding='utf-8-sig')); cases=data['cases']; sums=dict(executed=len(cases),passed=sum(c['passed'] for c in cases),failed=sum(not c['passed'] for c in cases)); reports[rel]=sums
 for key,v in sums.items():
  if data[key]!=v:errors.append(rel+':'+key)
result=dict(added=len(added),changed=len(changed),deleted=len(old.keys()-new.keys()),total=len(all_functions),newAndChanged=regions,checked=dict(checked),allStatus=dict(collections.Counter(x['status'] for x in all_functions)),allDecision=dict(collections.Counter(x['decision'] for x in all_functions)),allRole=dict(collections.Counter(x['usageRole'] for x in all_functions)),allCapability=dict(collections.Counter(x['capabilityId'] for x in all_functions)),targetsNonempty=sum(bool(x['targets']) for x in all_functions),storedReports=reports,errors=errors)
(out/'matrix-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in result.items() if k!='newAndChanged'},ensure_ascii=False,indent=2));print('\n'.join(f"{r['id']} | {pathlib.Path(r['path']).name}:{r['start']}-{r['end']} | {r['symbol']}" for r in regions))
