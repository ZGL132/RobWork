import json,pathlib,subprocess,hashlib,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';d='RobWork/doc/industrial-robot-design/';base='b83fa7a3fc8ff233e56736adabd53f11c914dbad';sha='85e561539a4038e37231dcfcb506109a5a188151'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def old(p):return json.loads(subprocess.check_output(['git','show',base+':'+p],cwd=repo,encoding='utf-8-sig'))
def digest(p):return hashlib.sha256((p.read_text(encoding='utf-8-sig').rstrip('\n')+'\n').encode()).hexdigest()
def save(n,x):(ev/n).write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
path=d+'reuse/functions/ui.json';a={r['functionId']:r for r in old(path)['functions']};rows=[];changes=[];bindings=[];defs=[];excerpts=[]
for r in load(repo/path)['functions']:
 if r['functionId'] not in a or r!=a[r['functionId']]:
  rows.append(r);isnew=r['functionId'] not in a;changes.append(dict(functionId=r['functionId'],new=isnew,changedKeys=[] if isnew else [k for k in r if r[k]!=a[r['functionId']].get(k)]))
  print(('NEW' if isnew else 'REFINED'),r['functionId'],r['targets'],r['differences'])
  for s in r['sources']:
   lines=(repo/s['path']).read_text(encoding='utf-8-sig').splitlines(keepends=True);section=''.join(lines[s['startLine']-1:s['endLine']]);got=hashlib.sha256(section.encode()).hexdigest();bindings.append(dict(functionId=r['functionId'],source=s,match=got==s['sha256'],computed=got));defs.append(dict(functionId=r['functionId'],source=s,first=section.splitlines()[0],last=section.splitlines()[-1]));excerpts.append('\n'+r['functionId']+' '+s['qualifiedSymbol']+'\n'+'\n'.join(f'{i+1}: {lines[i].rstrip()}' for i in range(max(0,s['startLine']-3),min(len(lines),s['endLine']+2))))
  for kind in ['inputs','auditEvidence']:
   for x in r.get(kind,[]):bindings.append(dict(functionId=r['functionId'],kind=kind,path=x['path'],match=digest(repo/x['path'])==x['sha256']))
save('rows.json',rows);save('changes.json',changes);save('bindings.json',bindings);save('definition-checks.json',defs);(ev/'source-excerpts.log').write_text('\n'.join(excerpts),encoding='utf-8')
catpath=d+'reuse/catalog.json';c=load(repo/catpath);oc={x['id']:x for x in old(catpath)['capabilities']};cc=[x for x in c['capabilities'] if x['id'] not in oc or x!=oc[x['id']]];save('catalog-changes.json',cc)
print('TOTALS',len(rows),'NEW',sum(x['new'] for x in changes),'REFINED',sum(not x['new'] for x in changes),'BINDINGS',len(bindings),'FAILED',sum(not x['match'] for x in bindings),'CATALOG',[(x['id'],x['review']) for x in cc])
