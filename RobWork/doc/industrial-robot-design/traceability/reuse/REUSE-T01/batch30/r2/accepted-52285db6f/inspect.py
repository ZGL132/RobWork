import json, hashlib, subprocess, pathlib, xml.etree.ElementTree as ET
OUT=pathlib.Path(__file__).resolve().parent
REPO=OUT.parent/'clone'
HEAD='52285db6f0d276742531ecb7e7bcf73db283e563'
BASE='933dc6016'
ORIG='0900751718849863af1190ece61c8ddb1f1cf547'
D='RobWork/doc/industrial-robot-design/'
B=D+'traceability/reuse/REUSE-T01/batch30/'
def git(*args): return subprocess.check_output(['git','-c','core.longpaths=true','-C',str(REPO),*args])
def blob(path,rev=HEAD): return git('show',rev+':'+path)
def jobj(path,rev=HEAD): return json.loads(blob(path,rev))
def sha(data): return hashlib.sha256(data).hexdigest()
def norm(data): return (data.decode('utf-8-sig').replace('\r\n','\n').replace('\r','\n').rstrip('\n')+'\n').encode()
result={'head':git('rev-parse',HEAD).decode().strip(),'base':git('rev-parse',BASE).decode().strip(),'original':ORIG}
errors=[]
old={x['functionId']:x for x in jobj(D+'reuse/functions/core.json',BASE)['functions']}
new={x['functionId']:x for x in jobj(D+'reuse/functions/core.json')['functions']}
changed=[k for k in old if old[k]!=new[k]]
result['matrix']={'added':sorted(set(new)-set(old)),'changed':changed,'count':len(changed),'allPending':all(new[k]['status']==new[k]['decision']=='pending' for k in changed)}
if len(changed)!=31 or result['matrix']['added'] or not result['matrix']['allPending']: errors.append('matrix shape/pending')
bindings=[]
for fid in changed:
 row=new[fid]
 for kind in ['sources','inputs','auditEvidence']:
  for e in row.get(kind,[]):
   data=blob(e['path'])
   if kind=='sources': data=('\n'.join(norm(data).decode().splitlines()[e['startLine']-1:e['endLine']])+'\n').encode()
   actual=sha(norm(data)); ok=actual==e['sha256']
   bindings.append({'functionId':fid,'kind':kind,'path':e['path'],'actual':actual,'expected':e['sha256'],'ok':ok})
   if not ok: errors.append(bindings[-1])
result['bindings']={'count':len(bindings),'failures':[x for x in bindings if not x['ok']],'entries':bindings}
identity='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Identity.cpp'
idhash=sha(norm(blob(identity)))
public=[]
for suffix in ['ContentVersion','ContentIdentity']:
 fid='core.audit-b30.Digest.hash.'+suffix
 entries=[e for e in new[fid]['inputs'] if e['path']==identity]
 preserved=all(e in new[fid]['inputs'] for e in old[fid]['inputs'])
 public.append({'functionId':fid,'directIdentityBindings':entries,'actualIdentitySha256':idhash,'priorInputsPreserved':preserved,'sourcesPreserved':new[fid]['sources']==old[fid]['sources']})
 if len(entries)!=1 or entries[0]['sha256']!=idhash or not preserved or new[fid]['sources']!=old[fid]['sources']: errors.append('public binding '+fid)
result['publicHashBindings']=public
result['preservedAll31Sources']=all(old[k]['sources']==new[k]['sources'] for k in changed)
result['preservedAll31Inputs']=all(all(e in new[k]['inputs'] for e in old[k]['inputs']) for k in changed)
if not result['preservedAll31Sources'] or not result['preservedAll31Inputs']: errors.append('prior binding removed')
raw=[]
for label,root,manifest,expected in [('original37',B,'artifact-manifest.json',37),('rejected57',B+'r2/rejected-090075171/','archive-manifest.json',57),('r2original66',B+'r2/','artifact-manifest.json',66)]:
 entries=jobj(root+manifest)['entries']; rows=[]
 for e in entries:
  data=blob(root+e['path']); ok=len(data)==e['bytes'] and sha(data)==e['rawSha256']
  sameOriginal=(data==blob(root+e['path'],ORIG)) if label=='original37' else None
  rows.append({'path':root+e['path'],'bytes':len(data),'rawSha256':sha(data),'manifestMatch':ok,'sameAsOriginalB30':sameOriginal})
  if not ok or sameOriginal is False: errors.append('raw mismatch '+root+e['path'])
 if len(rows)!=expected: errors.append('manifest count '+label)
 raw.append({'label':label,'manifest':root+manifest,'count':len(rows),'entries':rows})
result['rawManifests']=raw
paths=jobj(B+'r2/verification.json')['sourceComparison']['paths']
sourceChanges=git('diff','--name-only',ORIG+'..'+HEAD,'--',*paths).decode().splitlines()
result['sourceComparison']={'paths':paths,'changed':sourceChanges}
if sourceChanges: errors.append('source/probe/cmake changed')
result['originalManifestUnchanged']=blob(B+'artifact-manifest.json')==blob(B+'artifact-manifest.json',ORIG)
if not result['originalManifestUnchanged']: errors.append('original manifest changed')
result['scopePaths']=git('diff','--name-only',BASE+'..'+HEAD).decode().splitlines()
result['tests']={}
lengths=[0,1,55,56,63,64,65,119,120,127,128,129,4097,1000000]
vectors={n:hashlib.sha256(bytes(i%251 for i in range(n))).hexdigest() for n in lengths}
for label,path in [('original',B+'final-run/gtest.xml'),('originalIndependent',B+'r2/rejected-090075171/gtest.xml')]:
 root=ET.fromstring(blob(path)); tests=root.findall('.//testcase'); props=root.findall('.//property')
 cng={int(x.get('name').removeprefix('cng_sha256_')):x.get('value').removeprefix('cv-') for x in props if x.get('name','').startswith('cng_sha256_')}
 value={'total':len(tests),'passed':sum(t.find('failure') is None and t.find('skipped') is None for t in tests),'failed':sum(t.find('failure') is not None for t in tests),'skipped':sum(t.find('skipped') is not None for t in tests),'failedNames':[t.get('name') for t in tests if t.find('failure') is not None],'cngCount':len(cng),'hashlibMatch':cng==vectors}
 result['tests'][label]=value
 if (value['total'],value['passed'],value['failed'],value['skipped'],value['cngCount'],value['hashlibMatch'])!=(13,11,2,0,14,True): errors.append('original test inconsistency '+label)
result['searchReruns']=[]
for i,s in enumerate(jobj(B+'r2/search.json')['searches']):
 p=subprocess.run(s['args'],cwd=REPO,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 (OUT/f'search-{i+1}.stdout.log').write_bytes(p.stdout); (OUT/f'search-{i+1}.stderr.log').write_bytes(p.stderr)
 entry={'args':s['args'],'exit':p.returncode,'recordedExit':s['exit'],'output':p.stdout.decode('utf-8'),'stderr':p.stderr.decode('utf-8')}
 result['searchReruns'].append(entry)
 if p.returncode!=s['exit']: errors.append('search exit '+str(i+1))
h=0x6c62272e07bb014262b821756295c58d
for value in bytes(range(32)): h=((h^value)*0x1000000000000000000013b) % (1<<128)
result['fnvOracleFold']=f'{(h>>64)^(h&((1<<64)-1)):016x}'
result['errors']=errors
(OUT/'inspection.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ['bindings','rawManifests','scopePaths']},ensure_ascii=False,indent=2))
print('bindings',len(bindings),'raw counts',[x['count'] for x in raw],'errors',len(errors))
raise SystemExit(1 if errors else 0)
