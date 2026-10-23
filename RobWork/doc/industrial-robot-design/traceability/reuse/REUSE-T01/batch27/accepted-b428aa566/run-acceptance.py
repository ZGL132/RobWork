import json,subprocess,concurrent.futures,pathlib,time,sys
out=pathlib.Path(sys.argv[1]); repo=out/'repo-copy'; script=repo/'RobWork/scripts/industrialrobot'; task=repo/'RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
commands=[('lite-ps7',['pwsh','-NoProfile','-File',str(script/'test-governance-lite.ps1'),'-RepoRoot',str(repo),'-OutputDir',str(out/'lite-ps7')]),('lite-ps51',['powershell','-NoProfile','-File',str(script/'test-governance-lite.ps1'),'-RepoRoot',str(repo),'-OutputDir',str(out/'lite-ps51')]),('task',['pwsh','-NoProfile','-File',str(script/'validate-task.ps1'),'-TaskFile',str(task)]),('docs',['pwsh','-NoProfile','-File',str(script/'validate-docs.ps1')]),('frozen-reuse',['pwsh','-NoProfile','-File',str(script/'validate-reuse.ps1'),'-TaskFile',str(task),'-Head','b428aa566144bdad777dbc14b985a6d90f435d65']),('scope',['pwsh','-NoProfile','-File',str(script/'verify-task-scope.ps1'),'-TaskFile',str(task),'-Base','7c263e78683a74b5532e6f99e7647a02b6f39ea7','-Head','b428aa566144bdad777dbc14b985a6d90f435d65']),('commit-lint',['pwsh','-NoProfile','-File',str(script/'commit-lint.ps1'),'-Range','7c263e78683a74b5532e6f99e7647a02b6f39ea7..b428aa566144bdad777dbc14b985a6d90f435d65']),('diff-check',['git','diff','--check','7c263e78683a74b5532e6f99e7647a02b6f39ea7','b428aa566144bdad777dbc14b985a6d90f435d65'])]
def run(item):
 name,cmd=item; start=time.time()
 with (out/(name+'.log')).open('wb') as log:p=subprocess.run(cmd,cwd=repo,stdout=log,stderr=subprocess.STDOUT)
 (out/(name+'.exit.txt')).write_text(str(p.returncode)+'\n')
 result=dict(name=name,command=cmd,exitCode=p.returncode,elapsedSeconds=round(time.time()-start,2),log=str(out/(name+'.log')))
 print(json.dumps(result),flush=True); return result
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor: results=list(executor.map(run,commands))
(out/'commands.json').write_text(json.dumps(results,indent=2)+'\n')
