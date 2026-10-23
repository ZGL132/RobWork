import pathlib,json,subprocess,hashlib,sys,xml.etree.ElementTree as ET
root=pathlib.Path(sys.argv[1]);repo=root/'clone';shared=pathlib.Path('C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork');H='a02785a10eada414533d9789eedac3f5af654a71';B='52285db6f0d276742531ecb7e7bcf73db283e563';O='933dc6016c078b7d674fea68af19c038e0388979';D='RobWork/doc/industrial-robot-design';P=D+'/traceability/reuse/REUSE-T01/batch31';checks=[]
def git(*a):return subprocess.check_output(['git','-c','core.longpaths=true','-C',str(repo),*a])
def blob(p,rev=H):return git('show',rev+':'+p)
def j(p,rev=H):return json.loads(blob(p,rev))
def sha(b):return hashlib.sha256(b).hexdigest()
def norm(b):return (b.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n').encode()
def check(name,ok,detail=None):
 checks.append({'name':name,'passed':bool(ok),'detail':detail})
 if not ok:print('FAIL',name,detail)
def save(n,v):(root/n).write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
check('frozenHead',git('rev-parse','HEAD').decode().strip()==H)
for x in [B,O]:check('ancestor '+x,subprocess.run(['git','-C',str(repo),'merge-base','--is-ancestor',x,H]).returncode==0)
scopes=git('diff','--name-only',B,H).decode().splitlines();save('scope-paths.json',scopes)
sourcePaths=['RobWork/RobWorkStudio/src/rwslibs/industrialrobot','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg',P+'/CMakeLists.txt',P+'/UnitsSuitabilityTest.cpp',P+'/oracle.py',D+'/traceability/reuse/REUSE-T01/CMakeLists.txt']
check('product upstream probe CMake oracle unchanged',not git('diff','--name-only',O,H,'--',*sourcePaths),sourcePaths)
original=git('ls-tree','-r','--name-only',O,'--',P).decode().splitlines();check('all original B31 files preserved',all(blob(p,O)==blob(p) for p in original),len(original))
manifests=[]
for m,count in [(P+'/artifact-manifest.json',23),(P+'/r2/rejected-933dc6016/safe-manifest.json',45),(P+'/r2/artifact-manifest.json',54)]:
 data=j(m);base=m.rsplit('/',1)[0];out=[]
 for e in data['entries']:
  b=blob(base+'/'+e['path']);out.append({'path':e['path'],'bytes':len(b),'rawSha256':sha(b),'passed':len(b)==e['bytes'] and sha(b)==e['rawSha256'],'sharedRawMatchesBlob':(shared/base/e['path']).read_bytes()==b})
 check('manifest entries '+m,len(out)==count and all(e['passed'] for e in out),len(out));check('no manifest self-reference '+m,all(e['path']!=m.rsplit('/',1)[1] for e in out));manifests.append({'path':m,'manifestBytes':len(blob(m)),'manifestRawSha256':sha(blob(m)),'sharedManifestRawMatchesBlob':(shared/m).read_bytes()==blob(m),'entries':out})
save('raw-blob-verification.json',manifests)
s=j(P+'/r2/search.json');expected=['RobWork/doc/'+x for x in ['apidoc','gfx','layout','manual','ressources','sphinx','topics','tutorials']];check('eight real shared doc subdirectories',s['scope']==expected and all((repo/x).is_dir() for x in expected));check('md rst patterns','*.md' in s['args'] and '*.rst' in s['args']);check('no product doc scope',all('industrial-robot-design' not in x for x in s['scope']))
q=subprocess.run(s['args'],cwd=repo,capture_output=True);(root/'upstream-doc-search.stdout.log').write_bytes(q.stdout);(root/'upstream-doc-search.stderr.log').write_bytes(q.stderr);(root/'upstream-doc-search.exit.txt').write_text(str(q.returncode))
def lines(t):return sorted(t.replace('\\','/').replace('\r\n','\n').rstrip('\n').splitlines())
check('recorded doc search reproduced',q.returncode==s['exit']==0 and lines(q.stdout.decode())==lines(s['stdout']) and q.stderr.decode()==s['stderr'],{'exit':q.returncode,'lines':len(q.stdout.splitlines())})
doc='RobWork/doc/sphinx/tutorials/basic.rst';b=blob(doc);rd=s['read'][0];check('read normalized doc sha',rd['path']==doc and rd['sha256']==sha(norm(b)));dl=b.decode('utf-8-sig').replace('\r\n','\n').splitlines();check('basic.rst173 and205', '90*Deg2Rad' in dl[172] and 'uses radians and not degrees' in dl[204]);(root/'basic-read.txt').write_text('\n'.join(f'{i+1}: {dl[i]}' for i in range(160,207)),encoding='utf-8')
rows=j(D+'/reuse/functions/core.json')['functions'];before={x['functionId']:x for x in j(D+'/reuse/functions/core.json',B)['functions']};changed=[r for r in rows if r!=before.get(r['functionId'])];check('51 refreshed rows',len(changed)==51);rowchecks=[]
for r in changed:
 errs=[];prev=before[r['functionId']]
 for src in r['sources']:
  t=blob(src['path']).decode('utf-8-sig').replace('\r\n','\n').split('\n');seg='\n'.join(t[src['startLine']-1:src['endLine']]);
  if sha(norm(seg.encode()))!=src['sha256'] or src['symbol'] not in seg:errs.append('source '+src['path'])
 for group in ['inputs','auditEvidence']:
  for x in r[group]:
   if sha(norm(blob(x['path'])))!=x['sha256']:errs.append(group+' '+x['path'])
 if not any(x['path']==doc and x['sha256']==rd['sha256'] for x in r['inputs']):errs.append('missing doc input')
 if not any(x['path']==P+'/r2/search.json' for x in r['auditEvidence']):errs.append('missing r2 search')
 if not any(x['path']==P+'/r2/verification.json' for x in r['auditEvidence']):errs.append('missing r2 verification')
 if r['status']!='pending' or r['decision']!='pending':errs.append('not pending')
 changedKeys=[k for k in set(r)|set(prev) if r.get(k)!=prev.get(k)]
 if set(changedKeys)-{'search','inputs','auditEvidence'}:errs.append('unexpected semantic change '+str(changedKeys))
 rowchecks.append({'functionId':r['functionId'],'changedKeys':changedKeys,'errors':errs})
check('51 sources inputs evidence pending exact refresh',all(not r['errors'] for r in rowchecks));save('row-verification.json',rowchecks)
c=j(D+'/reuse/catalog.json');old=j(D+'/reuse/catalog.json',B);caps=c.get('capabilities',c.get('entries'));ocaps=old.get('capabilities',old.get('entries'));capchanges=[]
for x,y in zip(caps,ocaps):
 if x!=y:capchanges.append({'id':x['id'],'keys':[k for k in set(x)|set(y) if x.get(k)!=y.get(k)]})
check('catalog only UTIL-UNITS resourceSearch refresh',len(caps)==len(ocaps) and capchanges==[{'id':'UTIL-UNITS','keys':['resourceSearch']}],capchanges)
cap=next(x for x in caps if x['id']=='UTIL-UNITS');save('catalog-unit.json',cap);check('catalog read binding real doc kept out fake docs',P+'/r2/search.json' not in str(cap.get('docs',[])) and not any('RobWork/RobWork/doc' in str(d) for d in cap.get('docs',[])))
check('permissions unchanged',blob(D+'/reuse/permissions.json')==blob(D+'/reuse/permissions.json',B))
find=j(D+'/traceability/findings.json');check('findings unchanged in r2',blob(D+'/traceability/findings.json')==blob(D+'/traceability/findings.json',B))
acc=j(P+'/r2/rejected-933dc6016/acceptance.json');xml=ET.fromstring(blob(P+'/r2/rejected-933dc6016/run/gtest.xml'));cases=list(xml.iter('testcase'));failed=[x.attrib['classname']+'.'+x.attrib['name'] for x in cases if x.find('failure') is not None];skips=[x for x in cases if x.find('skipped') is not None];tests={'total':len(cases),'passed':len(cases)-len(failed)-len(skips),'failed':len(failed),'skipped':len(skips),'notRun':0,'failedNames':failed,'execution':'referenced original independent execution; no C++ rerun'};check('archived independent22 19p3f0s',acc['verdict']=='FAIL' and acc['findingIds']==['B31-P2-01'] and len(cases)==22 and len(failed)==3 and len(skips)==0 and set(failed)==set(acc['tests']['failedNames']));save('referenced-tests.json',tests)
for n in ['task','docs','reuse','scope','commit-lint']:
 e=root/(n+'.exit.txt');check('fresh gate '+n,e.exists() and e.read_text(encoding='utf-8-sig').strip()=='0')
check('clone status clean',not git('status','--short'))
save('checks.json',{'head':H,'base':B,'original':O,'scopeCount':len(scopes),'passed':sum(x['passed'] for x in checks),'failed':sum(not x['passed'] for x in checks),'checks':checks});print(json.dumps({'checks':len(checks),'failed':[x for x in checks if not x['passed']]},ensure_ascii=False))
