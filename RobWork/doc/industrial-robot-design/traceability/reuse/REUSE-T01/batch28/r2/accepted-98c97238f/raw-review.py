import subprocess,json,pathlib,hashlib,functools,collections,re,xml.etree.ElementTree as ET
root=pathlib.Path(__file__).parent; repo=root/'clone'
head='98c97238fa3c34aad51330b3b196439844badb69'; base='fd194d51b586085e26adeae4a73b72a06cb5ce21'; rejected='f2e65141259cd43ee5e63799514a733908440355'; originalbase='f6d3de37ddbc8375a334b456b058e3d548141484'
design='RobWork/doc/industrial-robot-design/'; batch=design+'traceability/reuse/REUSE-T01/batch28/'
@functools.cache
def raw(p,ref=head):return subprocess.check_output(['git','-C',str(repo),'show',ref+':'+p])
def doc(p,ref=head):return json.loads(raw(p,ref))
def norm(b):return b.decode('utf-8-sig').replace('\r\n','\n').replace('\r','\n')
def digest(t):return hashlib.sha256((t.rstrip('\n')+'\n').encode()).hexdigest()
def changed(a,b,paths=[]):return subprocess.check_output(['git','-C',str(repo),'diff','--name-only',a,b,'--']+paths).decode().splitlines()
errors=[]; manifests=[]
for mp,entries,prefix in [(batch+'artifact-manifest.json',None,batch),(batch+'r2/artifact-manifest.json',None,batch+'r2/'),(batch+'r2/rejected-f2e651412/safe-manifest.json','artifacts',batch+'r2/rejected-f2e651412/')]:
 m=doc(mp); items=m[entries] if entries else m
 for e in items:
  b=raw(prefix+e['path'])
  if len(b)!=e['bytes'] or hashlib.sha256(b).hexdigest()!=e['rawSha256']:errors.append(['manifest',mp,e['path']])
 manifests.append({'path':mp,'count':len(items)})
 # Compare current raw Git blobs to original frozen Git blobs for historical B28 evidence.
 if mp==batch+'artifact-manifest.json':
  for e in items:
   if raw(prefix+e['path'])!=raw(prefix+e['path'],rejected):errors.append(['historical-b28-raw',e['path']])
p=design+'reuse/functions/core.json'; old=doc(p,originalbase)['functions']; prior=doc(p,base)['functions']; new=doc(p)['functions']; added=new[len(old):]
prior_unchanged=new[:len(old)]==old
if not prior_unchanged:errors.append(['original-11-rows-changed'])
rows=[]
for r in added:
 item={'functionId':r['functionId'],'permissionRef':r['permissionRef'],'inputs':len(r['inputs']),'sources':[]}
 if r['decision']!='pending' or r['status']!='pending':errors.append([r['functionId'],'not pending'])
 for s in r['sources']:
  lines=norm(raw(s['path'])).split('\n'); region='\n'.join(lines[s['startLine']-1:s['endLine']])
  if digest(region)!=s['sha256'] or s['symbol'] not in region:errors.append([r['functionId'],'source-binding',s['path']])
  # Retain complete bound region for independent human semantic review.
  item['sources'].append({**s,'region':region})
 for kind in ('inputs','auditEvidence'):
  for s in r.get(kind,[]):
   if digest(norm(raw(s['path'])))!=s['sha256']:errors.append([r['functionId'],kind,s['path']])
 macro=r.get('expandedSymbols')
 if 'IdMacro' in r['functionId']:
  if len(macro or [])!=(18 if 'value-comparisons' in r['functionId'] else 6):errors.append([r['functionId'],'expanded-symbol-count'])
  item['expandedSymbols']=macro
 rows.append(item)
findp=design+'traceability/findings.json'; oldfind=doc(findp,originalbase)['findings']; newfind=doc(findp)['findings']; f965=[r for r in newfind if r['id']=='F-965'][0]
oldbytes=raw(findp,originalbase); last=oldbytes.rfind(b'    }'); historical_raw=oldbytes[:last+5]==raw(findp)[:last+5]
if not historical_raw:errors.append(['historical-findings-raw-prefix'])
sourcepaths=['RobWork/RobWorkStudio/src/rwslibs/industrialrobot','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg',batch+'CMakeLists.txt',batch+'CoreIdentitySuitabilityTest.cpp']
sourcechanges=changed(rejected,head,sourcepaths)
if sourcechanges:errors.append(['source-changes',sourcechanges])
results=[]
for run in ['core','probe']:
 prefix=batch+'r2/rejected-f2e651412/'+run+'/'
 e=ET.fromstring(raw(prefix+'gtest.xml')); tests=e.findall('.//testcase'); fails=[t.get('classname')+'.'+t.get('name') for t in tests if t.find('failure') is not None]; skips=[t for t in tests if t.find('skipped') is not None]
 results.append({'run':run,'total':len(tests),'passed':len(tests)-len(fails)-len(skips),'failed':len(fails),'skipped':len(skips),'failedTests':fails,'recordedExit':norm(raw(prefix+'exit.txt')).strip()})
result={'head':head,'base':base,'rejected':rejected,'changedPaths':len(changed(base,head)),'manifests':manifests,'original11Unchanged':prior_unchanged,'rows':len(added),'inputBindingsBefore':sum(len(r['inputs']) for r in prior[len(old):]),'inputBindingsAfter':sum(len(r['inputs']) for r in added),'historicalFindingsSemanticPrefix':newfind[:len(oldfind)]==oldfind,'historicalFindingsRawPrefix':historical_raw,'f965':f965,'sourceChangesFromRejected':sourcechanges,'recordedIndependentTests':results,'permissionCounts':dict(collections.Counter(r['permissionRef'] for r in added)),'errors':errors}
(root/'raw-review.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8'); (root/'row-review.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'row-review.txt').write_text('\n\n'.join(r['functionId']+' '+r['sources'][0]['qualifiedSymbol']+'\n'+r['sources'][0]['region'] for r in rows),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))

