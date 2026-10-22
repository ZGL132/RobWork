import pathlib,json,collections,subprocess
ev=pathlib.Path(__file__).parent;repo=ev/'repo';design=repo/'RobWork/doc/industrial-robot-design'
inv=json.loads((design/'reuse/inventory.json').read_text(encoding='utf-8-sig'))
rows=[]
for f in sorted((design/'reuse/functions').glob('*.json')):
    j=json.loads(f.read_text(encoding='utf-8-sig'));rows+=j['functions']
src=collections.defaultdict(list);inputs=set()
for r in rows:
    for s in r['sources']:src[s['path']].append({'functionId':r['functionId'],'symbol':s['symbol'],'startLine':s['startLine'],'endLine':s['endLine'],'auditedSections':r.get('auditedSections',[]),'auditBoundary':r.get('auditBoundary','')})
    inputs.update(x['path'] for x in r.get('inputs',[]))
summary=[]
for unit in sorted(set(x['unit'] for x in inv['files'])):
    fs=[f for f in inv['files'] if f['unit']==unit];rr=[r for r in rows if r['ownerUnit']==unit]
    summary.append({'unit':unit,'baselineFiles':len(fs),'roles':dict(collections.Counter(f['role'] for f in fs)),'relations':len(rr),'distinctFunctionIds':len(set(r['functionId'] for r in rr)),'sourceReferencedInventoryFiles':sum(f['path'] in src for f in fs),'inputOnlyInventoryFiles':sum(f['path'] in inputs and f['path'] not in src for f in fs),'unreferencedInventoryFiles':sum(f['path'] not in inputs and f['path'] not in src for f in fs)})
out={'frozenSha':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),'baselineCommit':inv['baselineCommit'],'baselineFiles':len(inv['files']),'relations':len(rows),'distinctFunctionIds':len(set(r['functionId'] for r in rows)),'distinctSourcePaths':len(src),'summaryByUnit':summary,'sourceReferenceIndex':dict(src),'unreferencedFiles':[f for f in inv['files'] if f['path'] not in src],'allFunctionIds':[r['functionId'] for r in rows]}
(ev/'inventory-analysis.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print('Frozen',out['frozenSha'],'baseline',len(inv['files']),'relations',len(rows),'sourcePaths',len(src))
for s in summary: print(s['unit'],s['baselineFiles'],s['relations'],s['sourceReferencedInventoryFiles'],s['inputOnlyInventoryFiles'],s['roles'])
print('SOURCE ROLES',dict(collections.Counter(f['role'] for f in inv['files'] if f['path'] in src)))
