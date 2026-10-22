import pathlib,subprocess,json,hashlib,concurrent.futures
out=pathlib.Path(__file__).parent; repo=out/'repo'; base='4b9977f096d0b3d58687ecc94013f435e5b0502c'; baseline=out/'baseline-root'
def blob(path): return subprocess.check_output(['git','-C',str(repo),'show',base+':'+path])
paths=['RobWork/scripts/industrialrobot/merge-findings-union.ps1','RobWork/scripts/industrialrobot/pipeline-lock.ps1','RobWork/scripts/industrialrobot/validate-state.ps1','RobWork/doc/industrial-robot-design/traceability/pipeline/state.json']
for p in paths:
 target=baseline/p; target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(blob(p))
results=[]
for shell in ['pwsh','powershell']:
 for case in ['missing-stage','missing-1','missing-2','missing-3']:
  fixture=out/'fixtures-v3'/f'{shell}-{case}'; target=fixture/'RobWork/doc/industrial-robot-design/traceability/findings.json'; original=target.read_bytes()
  before=subprocess.check_output(['git','-C',str(fixture),'ls-files','-u']); expectedStages=0 if case=='missing-stage' else 2
  for version in ['baseline','frozen']:
   script=(baseline if version=='baseline' else repo)/paths[0]; cmd=[shell,'-NoProfile','-File',str(script),'-RepoRoot',str(fixture)]
   p=subprocess.run(cmd,capture_output=True); name=f'{version}-{shell}-{case}'; (out/(name+'.stdout.log')).write_bytes(p.stdout); (out/(name+'.stderr.log')).write_bytes(p.stderr)
   after=subprocess.check_output(['git','-C',str(fixture),'ls-files','-u']); expected=1 if version=='baseline' and shell=='powershell' else 2
   reason='三方暂存条目' in p.stdout.decode('utf-8-sig',errors='replace') if expected==2 else b'NativeCommandError' in p.stderr
   item=dict(kind='direct-missing-stage',version=version,shell=shell,case=case,command=cmd,expectedExit=expected,actualExit=p.returncode,stageCount=len(before.splitlines()),expectedStages=expectedStages,stagesByteForBytePreserved=before==after,worktreeByteForBytePreserved=original==target.read_bytes(),diagnosticMatches=reason,stdout=name+'.stdout.log',stderr=name+'.stderr.log')
   item['passed']=p.returncode==expected and len(before.splitlines())==expectedStages and before==after and original==target.read_bytes() and reason; results.append(item)
def pipeline(shell):
 cmd=[shell,'-NoProfile','-File',str(repo/'RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1'),'-RepoRoot',str(baseline)]
 p=subprocess.run(cmd,cwd=repo,capture_output=True); name='baseline-full-pipeline-'+shell
 (out/(name+'.stdout.log')).write_bytes(p.stdout); (out/(name+'.stderr.log')).write_bytes(p.stderr)
 expected=0 if shell=='pwsh' else 1
 text=(p.stdout+p.stderr).decode('utf-8-sig',errors='replace')
 reason=('union-boundary: missing-stage actual=2' in text) if expected==0 else ('NativeCommandError' in text)
 return dict(kind='full-pipeline-red',shell=shell,command=cmd,expectedExit=expected,actualExit=p.returncode,diagnosticMatches=reason,passed=p.returncode==expected and reason,stdout=name+'.stdout.log',stderr=name+'.stderr.log')
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool: results.extend(pool.map(pipeline,['pwsh','powershell']))
report=dict(passed=sum(r['passed'] for r in results),failed=sum(not r['passed'] for r in results),skipped=0,cases=results)
(out/'red-comparison-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(report,ensure_ascii=False,indent=2))
