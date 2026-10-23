import pathlib,subprocess,json,hashlib
out=pathlib.Path(__file__).parent;repo=out/'repo';base='d0fcb96f62a863684848161f7b8ecd93ef0be302';sha='4b9977f096d0b3d58687ecc94013f435e5b0502c'
def blob(path,ref):return subprocess.check_output(['git','-C',str(repo),'show',ref+':'+path])
p='RobWork/scripts/industrialrobot/merge-findings-union.ps1';raw=blob(p,base);baseline=out/'baseline-with-parser-bom.ps1';baseline.write_bytes(b'\xef\xbb\xbf'+raw.removeprefix(b'\xef\xbb\xbf'))
results=[]
for shell in ['pwsh','powershell']:
 cmd=[shell,'-NoProfile','-File',str(baseline),'-RepoRoot',str(out/'fixtures-v3'/f'{shell}-missing-stage')]
 p=subprocess.run(cmd,capture_output=True);(out/f'baseline-missing-stage-{shell}.stdout.log').write_bytes(p.stdout);(out/f'baseline-missing-stage-{shell}.stderr.log').write_bytes(p.stderr)
 results.append(dict(command=cmd,exitCode=p.returncode,baseline=base,parserOnlyBomAdded=True))
(out/'baseline-stage-comparison.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
p='RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json';old=json.loads(blob(p,base));new=json.loads(blob(p,sha));oldmap={r['functionId']:r for r in old['functions']};deltas=[]
def diff(a,b,path):
 if type(a)!=type(b):deltas.append(dict(path=path,before=a,after=b));return
 if isinstance(a,dict):
  for k in a.keys()|b.keys():diff(a.get(k),b.get(k),path+'/'+k)
 elif isinstance(a,list):
  if len(a)!=len(b):deltas.append(dict(path=path+'/length',before=len(a),after=len(b)))
  for i,(aa,bb) in enumerate(zip(a,b)):diff(aa,bb,path+'/'+str(i))
 elif a!=b:deltas.append(dict(path=path,before=a,after=b))
for row in new['functions']:
 if row['functionId'] in oldmap:diff(oldmap[row['functionId']],row,row['functionId'])
(out/'old-row-leaf-deltas.json').write_text(json.dumps(dict(count=len(deltas),allDigestRefreshOnly=all(x['path'].endswith('/sha256') for x in deltas),deltas=deltas),ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(baselineStageComparison=results,oldRowLeafDeltaCount=len(deltas),allDigestRefreshOnly=all(x['path'].endswith('/sha256') for x in deltas)),ensure_ascii=False,indent=2))
