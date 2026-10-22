import pathlib,subprocess,json,concurrent.futures,os,time
out=pathlib.Path(__file__).parent
repo=out/'repo'
sha='4b9977f096d0b3d58687ecc94013f435e5b0502c'
base='d0fcb96f62a863684848161f7b8ecd93ef0be302'
task='RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
script='RobWork/scripts/industrialrobot/'
checks=[('pipeline-ps7',['pwsh','-NoProfile','-File',script+'test-pipeline-pipeline.ps1']),('pipeline-ps51',['powershell','-NoProfile','-File',script+'test-pipeline-pipeline.ps1']),('validate-task',['pwsh','-NoProfile','-File',script+'validate-task.ps1','-TaskFile',task]),('validate-reuse',['pwsh','-NoProfile','-File',script+'validate-reuse.ps1','-TaskFile',task,'-Head',sha]),('validate-docs',['pwsh','-NoProfile','-File',script+'validate-docs.ps1']),('scope-frozen',['pwsh','-NoProfile','-File',script+'verify-task-scope.ps1','-TaskFile',task,'-Base',base,'-Head',sha]),('commit-lint',['pwsh','-NoProfile','-File',script+'commit-lint.ps1','-Range',base+'..'+sha])]
def run(item):
 name,cmd=item
 start=time.time()
 with (out/(name+'.log')).open('wb') as log:
  p=subprocess.run(cmd,cwd=repo,stdout=log,stderr=subprocess.STDOUT)
 (out/(name+'.exit.txt')).write_text(str(p.returncode)+'\n')
 result=dict(name=name,command=cmd,exitCode=p.returncode,seconds=round(time.time()-start,2),log=name+'.log')
 print(json.dumps(result),flush=True)
 return result
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 results=list(pool.map(run,checks))
(out/'checks.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
