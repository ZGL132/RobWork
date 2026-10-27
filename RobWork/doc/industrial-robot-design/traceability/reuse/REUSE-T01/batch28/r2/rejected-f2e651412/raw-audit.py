import subprocess,json,pathlib,hashlib,collections,functools
root=pathlib.Path(__file__).parent
repo=root/'clone'; head='f2e65141259cd43ee5e63799514a733908440355'; base='f6d3de37ddbc8375a334b456b058e3d548141484'; design='RobWork/doc/industrial-robot-design/'
@functools.cache
def raw(p,ref=head):return subprocess.check_output(['git','-C',str(repo),'show',ref+':'+p])
def doc(p,ref=head):return json.loads(raw(p,ref))
def norm(b):return b.decode('utf-8-sig').replace('\r\n','\n').replace('\r','\n')
def digest(t):return hashlib.sha256((t.rstrip('\n')+'\n').encode()).hexdigest()
manifest=doc(design+'traceability/reuse/REUSE-T01/batch28/artifact-manifest.json')
manifest_errors=[]
for entry in manifest:
 b=raw(design+'traceability/reuse/REUSE-T01/batch28/'+entry['path'])
 if len(b)!=entry['bytes'] or hashlib.sha256(b).hexdigest()!=entry['rawSha256']:manifest_errors.append(entry['path'])
p=design+'reuse/functions/core.json'; old=doc(p,base)['functions']; new=doc(p)['functions']; added=[r for r in new if r not in old]; deleted=[r['functionId'] for r in old if r not in new]
errors=[]
for r in added:
 for s in r['sources']:
  lines=norm(raw(s['path'])).split('\n'); region='\n'.join(lines[s['startLine']-1:s['endLine']])
  if digest(region)!=s['sha256'] or s['symbol'] not in region: errors.append([r['functionId'],'source',s['path']])
 for kind in ('inputs','auditEvidence'):
  for s in r.get(kind,[]):
   if digest(norm(raw(s['path'])))!=s['sha256']: errors.append([r['functionId'],kind,s['path']])
findpath=design+'traceability/findings.json'; oldfind=doc(findpath,base)['findings']; newfind=doc(findpath)['findings']
oldbytes=raw(findpath,base); newbytes=raw(findpath)
# Preserve raw prefix up to the final historical record's close brace; comma was inserted after it.
last_marker=oldbytes.rfind(b'    }'); raw_prefix_equal=oldbytes[:last_marker+5]==newbytes[:last_marker+5]
result={'head':head,'base':base,'originalManifestCount':len(manifest),'rawManifestErrors':manifest_errors,'priorFunctions':len(old),'currentFunctions':len(new),'addedFunctions':len(added),'deletedOrChangedPriorFunctions':deleted,'bindingErrors':errors,'historicalFindingsSemanticPrefixEqual':newfind[:len(oldfind)]==oldfind,'historicalFindingsRawPrefixEqual':raw_prefix_equal,'newFindings':[r['id'] for r in newfind[len(oldfind):]],'addedDecisions':dict(collections.Counter(r['decision'] for r in added)),'addedStatuses':dict(collections.Counter(r['status'] for r in added)),'capabilities':dict(collections.Counter(r['capabilityId'] for r in added))}
(root/'raw-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'added-rows.json').write_text(json.dumps(added,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'added-rows-summary.txt').write_text('\n'.join(f"{r['functionId']} | {r['decision']} | {r['capabilityId']} | {r.get('differences','')} | {r.get('auditBoundary','')}" for r in added),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))


