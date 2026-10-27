import sys,json,hashlib,subprocess,pathlib,xml.etree.ElementTree as ET
root=pathlib.Path(sys.argv[1]); repo=root/'repo'; base='RobWork/doc/industrial-robot-design/'
def load(p):return json.loads((repo/p).read_text(encoding='utf-8-sig'))
def norm(p):return (repo/p).read_text(encoding='utf-8-sig').replace('\r\n','\n').replace('\r','\n')
def digest(s):return hashlib.sha256(s.encode()).hexdigest()
def write(p,v):(root/p).write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
def run(args,out):
 p=subprocess.run(args,cwd=repo,capture_output=True,text=True,encoding='utf-8',errors='replace');(root/out).write_text(p.stdout+p.stderr,encoding='utf-8');return p
assert run(['git','rev-parse','HEAD'],'frozen-sha.txt').stdout.strip()=='daee4f8905587b05cd1bab88bb570b8dc240097c'
defs=load(base+'traceability/reuse/REUSE-T01/batch18-definitions.json'); m=load(base+'reuse/functions/policy.json')['functions']; fmap={f['functionId']:f for f in m}; checks=[]; bindings=[]; reviewed=[]
for d in defs:
 f=fmap[d['functionId']]; s=d['source']; text=norm(s['path']); lines=text.splitlines(keepends=True); section=''.join(lines[s['startLine']-1:s['endLine']]); actual=digest(section)
 checks.append({'functionId':d['functionId'],'range':f"{s['startLine']}-{s['endLine']}",'definitionHash':actual==s['sha256'],'sameSource':s in f['sources'],'pending':f['decision']=='pending' and f['status']=='pending','usageRole':f['usageRole'],'permissionRef':f['permissionRef'],'capabilityId':f['capabilityId']})
 for group in ('inputs','auditEvidence'):
  for x in f.get(group,[]):bindings.append({'functionId':d['functionId'],'group':group,'path':x['path'],'match':digest(norm(x['path']))==x['sha256']})
 reviewed.append(d['functionId']+'\n'+section+'\nDIFFERENCES: '+f['differences'])
write('definition-checks.json',checks);write('binding-checks.json',bindings);(root/'all46-source-review.txt').write_text('\n\n'.join(reviewed),encoding='utf-8')
assert len(defs)==46 and len(set(d['functionId'] for d in defs))==46
assert all(c['definitionHash'] and c['sameSource'] and c['pending'] for c in checks)
assert all(b['match'] for b in bindings)
catalog=load(base+'reuse/catalog.json')['capabilities']; cc=[]
for cap in catalog:
 if cap['id'] not in ['COLLISION','MODEL-BOUNDS']:continue
 for group in ['upstream','tests']:
  for item in cap.get(group,[]):
   cc.append({'capability':cap['id'],'path':item['path'],'match':digest(norm(item['path']))==item['sha256']})
   if 'targetFile' in item:cc.append({'capability':cap['id'],'path':item['targetFile'],'match':digest(norm(item['targetFile']))==item['targetSha256']})
write('candidate-checks.json',cc);assert all(x['match'] for x in cc)
record=load(base+'traceability/reuse/REUSE-T01/probe-r18/environment-artifacts.json'); oldchecks=[]
for x in record['sources']+[record[k] for k in ['xml','report','stdout']]:oldchecks.append({'path':x['path'],'match':digest(norm(x['path']))==x['sha256']})
write('original-evidence-checks.json',oldchecks) # Preserve independent raw/normalized discrepancy for review.
search=load(base+'traceability/reuse/REUSE-T01/batch18-search.json'); sr=[]
for i,x in enumerate(search['results']):
 p=run(x['arguments'],f'search-{i+1}.log');sr.append({'index':i+1,'exit':p.returncode,'recordedExit':x['exitCode'],'stdoutMatch':p.stdout.replace('\r\n','\n')==x['stdout'].replace('\r\n','\n'),'stderrMatch':p.stderr.replace('\r\n','\n')==x['stderr'].replace('\r\n','\n')})
write('search-replay.json',sr)
reports=[]
for label,path in [('original',repo/base/'traceability/reuse/REUSE-T01/probe-r18'),('independent',root)]:
 report=json.loads((path/'ird-test-report.json').read_text(encoding='utf-8-sig')); xml=ET.parse(path/'gtest.xml').getroot();tests=xml.findall('.//testcase');outcomes={t.attrib['classname']+'.'+t.attrib['name']:('failed' if t.find('failure') is not None else 'passed') for t in tests}
 assert len(tests)==2 and sum(t.find('failure') is not None for t in tests)==1
 assert {t['testId']:t['outcome'] for t in report['tests']}==outcomes
 assert report['summary']['passed']==1 and report['summary']['failed']==1 and report['summary']['skipped']==0 and report['summary']['notRun']==0
 exitpath=path/('exit.txt' if label=='original' else 'run.exit.txt');assert exitpath.read_text(encoding='utf-8-sig').strip()=='1'
 reports.append({'label':label,'summary':report['summary'],'xmlTests':outcomes,'exit':1})
write('native-xml-exit-checks.json',reports)
run(['git','diff','HEAD^','HEAD','--','RobWork/RobWorkStudio/src/rwslibs/industrialrobot','RobWork/RobWork','RobWork/RobWorkSim','vcpkg'],'product-upstream-diff.log')
assert not (root/'product-upstream-diff.log').read_text()
run(['git','status','--short'],'git-status-end.log');assert not (root/'git-status-end.log').read_text()
print(f'PASS 46 definitions; {len(bindings)} bindings; {len(cc)} candidate hashes; {len(oldchecks)} old evidence hashes; native/XML/exit 2 records agree; searches {len(sr)} completed')

