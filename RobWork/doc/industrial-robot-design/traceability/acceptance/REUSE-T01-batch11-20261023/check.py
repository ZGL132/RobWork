import json,pathlib,subprocess,hashlib,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';d='RobWork/doc/industrial-robot-design/';base='e0bee2994eee0e8f3299ea5ccf200c354f4454be'
def old(path):return json.loads(subprocess.check_output(['git','show',base+':'+path],cwd=repo).decode('utf-8-sig'))
new=[];checks=[]
for f in (repo/d/'reuse/functions').glob('*.json'):
 rel=f.relative_to(repo).as_posix();a=old(rel);keys={r['functionId'] for r in a['functions']}
 new.extend(r for r in json.loads(f.read_text(encoding='utf-8-sig'))['functions'] if r['functionId'] not in keys)
print('NEW',len(new))
for r in new:
 print(r['functionId'],r['capabilityId'],r['targets'])
 for s in r['sources']:
  lines=(repo/s['path']).read_text(encoding='utf-8-sig').splitlines(keepends=True)
  section=''.join(lines[s['startLine']-1:s['endLine']]);digest=hashlib.sha256(section.encode()).hexdigest()
  checks.append(dict(functionId=r['functionId'],source=s,computed=digest,match=digest==s['sha256'],first=section.splitlines()[0],last=section.splitlines()[-1]))
  print(' ',s['startLine'],s['endLine'],s['qualifiedSymbol'],'HASH',digest==s['sha256'])
 for k in ['inputs','auditEvidence']:
  for x in r.get(k,[]):
   got=hashlib.sha256((repo/x['path']).read_text(encoding='utf-8-sig').rstrip('\n').__add__('\n').encode('utf-8')).hexdigest();checks.append(dict(functionId=r['functionId'],kind=k,path=x['path'],match=got==x['sha256']))
catpath=d+'reuse/catalog.json';c=json.loads((repo/catpath).read_text(encoding='utf-8-sig'));o=old(catpath)
print('CAT TYPES',c.keys())
for k in c:
 if k=='capabilities':
  oldkeys={x.get('id') for x in o[k]};added=[x for x in c[k] if x.get('id') not in oldkeys]
  (ev/'new-catalog.json').write_text(json.dumps(added,ensure_ascii=False,indent=2),encoding='utf-8');print('NEW CAT',[(x.get('id'),x.keys()) for x in added])
(ev/'new-rows.json').write_text(json.dumps(new,ensure_ascii=False,indent=2),encoding='utf-8')
(ev/'bindings.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
print('BINDINGS',len(checks),'FAIL',sum(not x['match'] for x in checks))



