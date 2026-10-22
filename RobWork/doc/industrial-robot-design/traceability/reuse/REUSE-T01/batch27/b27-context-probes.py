from pathlib import Path
import sys,subprocess,json,hashlib,shutil
root=Path.cwd(); out=root/sys.argv[1]; out.mkdir(parents=True,exist_ok=True)
scripts=root/'RobWork/scripts/industrialrobot'; task='LITE-T01'
rels=['AGENTS.md','RobWork/doc/industrial-robot-design/AGENTS.md','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md','RobWork/doc/industrial-robot-design/REQUIREMENTS.md','RobWork/doc/industrial-robot-design/context/reading-paths.md','RobWork/doc/industrial-robot-design/context/asset-inventory.json','RobWork/doc/industrial-robot-design/context/README.md']
def run(args):
 p=subprocess.run([str(a) for a in args],cwd=root,capture_output=True)
 return p.returncode,p.stdout.decode('utf-8',errors='replace')+p.stderr.decode('utf-8',errors='replace')
results=[]
for shell in ['pwsh','powershell']:
 fixture=out/(shell+'-fixture'); fixture.mkdir(exist_ok=True)
 for rel in rels:
  p=fixture/rel; p.parent.mkdir(parents=True,exist_ok=True); p.write_text('fixture '+rel+'\n',encoding='utf-8')
 base=fixture/'RobWork/doc/industrial-robot-design'; cp=base/'tasks/foundation/LITE-T01.json'; cp.parent.mkdir(parents=True,exist_ok=True)
 cp.write_text(json.dumps(dict(taskId=task,title='上下文包边界',unit='foundation-governance',status='ready',masterWp='WP-A',branch='fixture',dependsOn=[],requirements=['H6'],designRefs=['context/README.md#3'],allowedFiles=[],forbiddenFiles=[],verify=[],acceptance=[]),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 assert run(['git','-C',fixture,'init','-q'])[0]==0
 assert run(['git','-C',fixture,'config','core.autocrlf','false'])[0]==0
 args=[shell,'-NoProfile','-File',scripts/'validate-context-pack.ps1','-RepoRoot',fixture/'RobWork']
 def probe(name,expected,extra=()):
  rc,log=run(args+list(extra)); (out/(shell+'-'+name+'.log')).write_text(log,encoding='utf-8')
  results.append(dict(shell=shell,case=name,expectedExitCode=expected,exitCode=rc,passed=rc==expected))
 probe('all-no-dir',0)
 probe('selected-no-dir',1,['-TaskId',task])
 rc,log=run([shell,'-NoProfile','-File',scripts/'build-context-pack.ps1','-RepoRoot',fixture/'RobWork','-TaskId',task]); (out/(shell+'-build.log')).write_text(log,encoding='utf-8'); assert rc==0,(shell,log)
 pack=base/'context/packs/LITE-T01.md'; original=pack.read_text(encoding='utf-8-sig')
 (out/(shell+'-original-pack.md')).write_bytes(pack.read_bytes())
 probe('fresh',0,['-TaskId',task])
 for name,remove in [('missing-contract','tasks/foundation/LITE-T01.json'),('missing-designref','context/README.md'),('missing-rule','AGENTS.md')]:
  lines=original.splitlines(); modified='\n'.join(x for x in lines if not (x.startswith('| ') and (x.split('|')[1].strip().endswith(remove) if remove!='AGENTS.md' else x.split('|')[1].strip()==remove)))+'\n'
  pack.write_text(modified,encoding='utf-8'); probe(name,1,['-TaskId',task])
 pack.write_text(original,encoding='utf-8'); design=base/'context/README.md'; saved=design.read_bytes(); design.write_bytes(saved+b'changed\n'); probe('changed-designref',2,['-TaskId',task]); design.write_bytes(saved)
 pack.write_text(original+'\n"phase": "implementing"\n',encoding='utf-8'); probe('state-copy',1,['-TaskId',task])
 pack.write_text(original,encoding='utf-8')
report=dict(expected=16,executed=len(results),passed=sum(x['passed'] for x in results),failed=sum(not x['passed'] for x in results),skipped=0,cases=results)
(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2)); sys.exit(bool(report['failed']))
