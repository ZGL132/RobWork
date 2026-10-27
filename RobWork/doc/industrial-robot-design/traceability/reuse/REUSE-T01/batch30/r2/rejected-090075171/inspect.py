import json,hashlib,subprocess,pathlib,xml.etree.ElementTree as ET
ROOT=pathlib.Path(__file__).resolve().parents[1]; REPO=ROOT/'clone'; OUT=ROOT/'evidence'
HEAD='0900751718849863af1190ece61c8ddb1f1cf547'; BASE='bb283be1dbb6ae62c59d6563e3c7dac0a4ae5103'
D='RobWork/doc/industrial-robot-design/'; B=D+'traceability/reuse/REUSE-T01/batch30/'
def git(*args):return subprocess.check_output(['git','-C',str(REPO),*args])
def blob(p,rev=HEAD):return git('show',rev+':'+p)
def jblob(p,rev=HEAD):return json.loads(blob(p,rev))
def sha(v):return hashlib.sha256(v).hexdigest()
def norm(v):return v.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n'
def digest(v):return sha(norm(v).encode())
result={}; errors=[]
paths=git('diff','--name-only',BASE+'..'+HEAD).decode().splitlines()
result['scope']={'count':len(paths),'paths':paths,'production_upstream_vcpkg_changes':[p for p in paths if not p.startswith(D)]}
old=jblob(D+'reuse/functions/core.json',BASE)['functions']; new=jblob(D+'reuse/functions/core.json')['functions']
oldmap={x['functionId']:x for x in old}; newmap={x['functionId']:x for x in new}
added=[x for x in new if x['functionId'] not in oldmap]; changed=[k for k in oldmap if oldmap[k]!=newmap[k]]
result['matrix']={'added':len(added),'changed_existing':changed,'all_pending':all(x['decision']==x['status']=='pending' for x in added+[newmap[k] for k in changed])}
bindings=[]
for row in added+[newmap[k] for k in changed]:
 for key in ['sources','inputs','auditEvidence']:
  for e in row.get(key,[]):
   data=blob(e['path'])
   if key=='sources': data=('\n'.join(data.decode('utf-8-sig').replace('\r\n','\n').splitlines()[e['startLine']-1:e['endLine']])+'\n').encode()
   ok=digest(data)==e['sha256']; bindings.append({'id':row['functionId'],'kind':key,'path':e['path'],'ok':ok})
   if not ok:errors.append(bindings[-1])
result['bindings']={'count':len(bindings),'failures':[x for x in bindings if not x['ok']]}
manifest=jblob(B+'artifact-manifest.json')['entries']; raw=[]
for e in manifest:
 p=B+e['path']; data=blob(p); checkout=(REPO/p).read_bytes()
 raw.append({'path':p,'blobBytes':len(data),'blobSha256':sha(data),'blobMatch':len(data)==e['bytes'] and sha(data)==e['rawSha256'],'checkoutMatch':len(checkout)==e['bytes'] and sha(checkout)==e['rawSha256'],'blobCRLF':data.count(b'\r\n'),'checkoutCRLF':checkout.count(b'\r\n')})
result['rawManifest']={'count':len(raw),'blobFailures':[x for x in raw if not x['blobMatch']],'checkoutDifferences':[x for x in raw if not x['checkoutMatch']],'entries':raw}
search=jblob(B+'search.json'); reruns=[]
for i,s in enumerate(search['searches']):
 p=subprocess.run(s['args'],cwd=REPO,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 (OUT/f'search-{i+1}.stdout.log').write_bytes(p.stdout);(OUT/f'search-{i+1}.stderr.log').write_bytes(p.stderr)
 reruns.append({'args':s['args'],'exit':p.returncode,'recordedExit':s['exit'],'lines':len(p.stdout.splitlines()),'recordedLines':len(s['stdout'].splitlines())})
result['searchReruns']=reruns
result['searchReadBindings']=[{'path':e['path'],'ok':digest(blob(e['path']))==e['sha256']} for e in search['read']]
result['externalCandidates']=[{'path':e['path'],'ok':sha(pathlib.Path(e['path']).read_bytes())==e['rawSha256'],'bytes':pathlib.Path(e['path']).stat().st_size} for e in search['externalCandidates']]
h=0x6c62272e07bb014262b821756295c58d
for value in bytes(range(32)):h=((h^value)*0x1000000000000000000013b) % (1<<128)
result['fnvOracle']={'hi':f'{h>>64:016x}','lo':f'{h&((1<<64)-1):016x}','fold':f'{(h>>64)^(h&((1<<64)-1)):016x}'}
oracle=jblob(B+'oracle-vectors.json'); lengths=[0,1,55,56,63,64,65,119,120,127,128,129,4097,1000000]
vectors={n:hashlib.sha256(bytes(i%251 for i in range(n))).hexdigest() for n in lengths}
result['hashlibVectors']={'vectors':vectors,'storedMatch':all(vectors[x['length']]==x['sha256'] for x in oracle['shaBoundaryVectors'])}
for name,path in [('stored',REPO/B/'final-run/gtest.xml'),('fresh',OUT/'gtest.xml')]:
 tree=ET.parse(path).getroot();tests=tree.findall('.//testcase');props=tree.findall('.//property');cp={int(x.get('name').removeprefix('cng_sha256_')):x.get('value').removeprefix('cv-') for x in props if x.get('name','').startswith('cng_sha256_')}
 result[name+'Tests']={'total':len(tests),'passed':sum(t.find('failure') is None and t.find('skipped') is None for t in tests),'failed':sum(t.find('failure') is not None for t in tests),'skipped':sum(t.find('skipped') is not None for t in tests),'failedNames':[t.get('name') for t in tests if t.find('failure') is not None],'cngProperties':len(cp),'cngHashlibMatch':cp==vectors}
result['validatorBindings']=[{'path':'RobWork/scripts/industrialrobot/'+p,'rawSha256':sha(blob('RobWork/scripts/industrialrobot/'+p))} for p in ['validate-task.ps1','validate-docs.ps1','validate-reuse.ps1','reuse-contract.ps1','verify-task-scope.ps1','commit-lint.ps1']]
(OUT/'inspection.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ['rawManifest','scope','hashlibVectors','validatorBindings']},ensure_ascii=False,indent=2))
print('raw manifest:',len(raw),'blob failures',len(result['rawManifest']['blobFailures']),'checkout differences',len(result['rawManifest']['checkoutDifferences']))
