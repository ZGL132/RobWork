from pathlib import Path
import subprocess,json,hashlib,sys
root=Path.cwd(); out=root/'.cache/b27-index'; out.mkdir(parents=True,exist_ok=True)
results=[]
for shell in ['pwsh','powershell']:
 fixture=out/(shell+'-fixture'); base=fixture/'RobWork/doc/industrial-robot-design'; trace=base/'traceability'; (base/'tasks').mkdir(parents=True,exist_ok=True)
 files={'sample/REUSE-T01-crlf.txt':b'a\r\nb\r\nc\n','sample/REUSE-T01-copy.txt':b'a\nb\nc\n','sample/LITE-T01-nul.txt':b'a\x00\r\nb','sample/REUSE-T01-large.bin':b'x'*(512*1024+1),'pipeline/state.json':b'{}\n','findings.json':json.dumps({'findings':[{'id':'F-001','status':'open','source':'traceability/sample/REUSE-T01-crlf.txt#x'}]}).encode()}
 for rel,data in files.items():
  p=trace/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
 for task,status in [('REUSE-T01','done'),('LITE-T01','ready')]:
  (base/'tasks'/f'{task}.json').write_text(json.dumps(dict(taskId=task,status=status))+'\n',encoding='utf-8')
 for args in [['git','-C',fixture,'init','-q'],['git','-C',fixture,'config','core.autocrlf','false'],['git','-C',fixture,'add','-f','--','RobWork']]:
  p=subprocess.run([str(x) for x in args],capture_output=True);assert p.returncode==0,p.stderr
 p=subprocess.run([shell,'-NoProfile','-File',str(root/'RobWork/scripts/industrialrobot/index-evidence.ps1'),'-RepoRoot',str(fixture/'RobWork')],capture_output=True)
 (out/(shell+'.log')).write_bytes(p.stdout+p.stderr)
 if p.returncode!=0: raise RuntimeError(p.stderr)
 index=json.loads((trace/'evidence-index.json').read_text(encoding='utf-8-sig')); rows={x['path']:x for x in index['entries']}
 checks=[('current-open-ref',rows['sample/REUSE-T01-crlf.txt']['lifecycle']=='current'),('historical-done',rows['sample/REUSE-T01-copy.txt']['lifecycle']=='historical'),('unverified-ready',rows['sample/LITE-T01-nul.txt']['lifecycle']=='unverified'),('machine-current',rows['pipeline/state.json']['lifecycle']=='current'),('text-normalized',rows['sample/REUSE-T01-crlf.txt']['sha256']==rows['sample/REUSE-T01-copy.txt']['sha256']==hashlib.sha256(b'a\nb\nc\n').hexdigest()),('binary-nul-raw',rows['sample/LITE-T01-nul.txt']['sha256']==hashlib.sha256(files['sample/LITE-T01-nul.txt']).hexdigest()),('large-unhashed',rows['sample/REUSE-T01-large.bin']['sha256'] is None and index['summary']['shaSkippedLarge']==1),('no-source-mutation',all((trace/rel).read_bytes()==data for rel,data in files.items()))]
 (out/(shell+'-index.json')).write_bytes((trace/'evidence-index.json').read_bytes())
 results += [dict(shell=shell,case=name,passed=ok) for name,ok in checks]
report=dict(executed=len(results),passed=sum(x['passed'] for x in results),failed=sum(not x['passed'] for x in results),skipped=0,cases=results)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2));sys.exit(bool(report['failed']))
