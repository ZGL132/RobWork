import pathlib,subprocess,json,concurrent.futures,sys
out=pathlib.Path(sys.argv[1]);source=out/'repo-copy'
paths=['RobWork/doc/industrial-robot-design/context','RobWork/doc/industrial-robot-design/tasks','RobWork/doc/industrial-robot-design/units','RobWork/doc/industrial-robot-design/reuse','RobWork/doc/industrial-robot-design/traceability/pipeline','RobWork/doc/industrial-robot-design/traceability/reuse','RobWork/doc/industrial-robot-design/traceability/acceptance','RobWork/scripts','RobWork/.agents','RobWork/RobWork/src','RobWork/RobWorkSim/src','RobWork/RobWork/gtest','RobWork/RobWorkSim/gtest','RobWork/RobWorkStudio/src/rwslibs/industrialrobot']
cases=[('baseline-lf','7c263e78683a74b5532e6f99e7647a02b6f39ea7','false'),('frozen-system-crlf','b428aa566144bdad777dbc14b985a6d90f435d65','true')]
def run(item):
 name,sha,eol=item;repo=out/name
 setup=[['git','clone','--shared','--quiet','--no-checkout',str(source),str(repo)],['git','-C',str(repo),'config','core.autocrlf',eol],['git','-C',str(repo),'config','core.longpaths','true'],['git','-C',str(repo),'sparse-checkout','init','--cone'],['git','-C',str(repo),'sparse-checkout','set']+paths,['git','-C',str(repo),'checkout','--quiet','--detach',sha]]
 with (out/(name+'-setup.log')).open('wb') as log:
  for cmd in setup:
   p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
   if p.returncode:raise RuntimeError((name,cmd,p.returncode))
 cmd=['powershell','-NoProfile','-File',str(repo/'RobWork/scripts/industrialrobot/test-governance-lite.ps1'),'-RepoRoot',str(repo),'-OutputDir',str(out/(name+'-suite'))]
 with (out/(name+'-suite.log')).open('wb') as log:p=subprocess.run(cmd,cwd=repo,stdout=log,stderr=subprocess.STDOUT)
 (out/(name+'-suite.exit.txt')).write_text(str(p.returncode)+'\n');return dict(name=name,commit=sha,coreAutocrlf=eol,command=cmd,exitCode=p.returncode)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor: results=list(executor.map(run,cases))
(out/'eol-comparison.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
