import subprocess,json,pathlib,hashlib
root=pathlib.Path(r'C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-b31-accept-df453223')
repo=root/'clone2'; base='0900751718849863af1190ece61c8ddb1f1cf547'; head='933dc6016c078b7d674fea68af19c038e0388979'
def blob(rev,path): return subprocess.check_output(['git','-C',str(repo),'show',f'{rev}:{path}'])
def sha(b): return hashlib.sha256(b).hexdigest()
p='RobWork/doc/industrial-robot-design/reuse/functions/core.json'
before=json.loads(blob(base,p));after=json.loads(blob(head,p))
b={r['functionId']:r for r in before['functions']};a={r['functionId']:r for r in after['functions']}
new=[r for k,r in a.items() if k not in b];changed=[r for k,r in a.items() if k in b and r!=b[k]]
scope=subprocess.check_output(['git','-C',str(repo),'diff','--name-only',base,head],text=True).splitlines()
summary={'base':base,'head':head,'scopeCount':len(scope),'scope':scope,'newCount':len(new),'changedCount':len(changed),'newIds':[r['functionId'] for r in new],'changedIds':[r['functionId'] for r in changed],'pending':all(r['status']=='pending' and r['decision']=='pending' for r in new+changed),'changedRowFields':{r['functionId']:[k for k in set(r)|set(b[r['functionId']]) if r.get(k)!=b[r['functionId']].get(k)] for r in changed}}
batch='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch31'
man=json.loads(blob(head,batch+'/artifact-manifest.json'))
checks=[]
for entry in man['entries']:
 p=batch+'/'+entry['path'];raw=blob(head,p);disk=(repo/p).read_bytes()
 checks.append({'path':entry['path'],'blobMatches':sha(raw)==entry['rawSha256'] and len(raw)==entry['bytes'],'rawMatches':sha(disk)==entry['rawSha256'] and len(disk)==entry['bytes']})
summary['originalManifest']={'count':len(checks),'checks':checks,'allPassed':all(c['blobMatches'] and c['rawMatches'] for c in checks)}
p='RobWork/doc/industrial-robot-design/traceability/findings.json'; old=blob(base,p);now=blob(head,p);prefix=json.loads(blob(head,batch+'/findings-prefix.json'));n=prefix['preservedPrefixBytes']
summary['findingsPrefix']={'bytes':n,'sha256':sha(old[:n]),'expected':prefix['rawSha256'],'equal':old[:n]==now[:n],'basePrefixHashMatches':sha(old[:n])==prefix['rawSha256'],'oldTail':old[n:].decode(),'newTail':now[n:].decode()}
summary['newFindingIds']=[r.get('id') for r in json.loads(now)['findings'] if r not in json.loads(old)['findings']] if isinstance(json.loads(now),dict) and 'findings' in json.loads(now) else 'see tail'
(root/'static-results.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'changed-rows.json').write_text(json.dumps(changed,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k not in ['scope','newIds','findingsPrefix']},ensure_ascii=False,indent=2))
print('PREFIX',summary['findingsPrefix'])
