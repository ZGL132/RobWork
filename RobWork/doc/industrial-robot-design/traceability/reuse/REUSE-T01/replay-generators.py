import json,shutil,subprocess,hashlib,time
from pathlib import Path
root=Path.cwd();prefix=Path('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden');out=root/'.cache/b16-golden-replay';assert not out.exists();out.mkdir()
rows=[]
for original in sorted(prefix.glob('*/*/generate/*.mjs')):
 dataset=original.parent.parent;copy=out/dataset.relative_to(prefix);shutil.copytree(dataset,copy);script=copy/'generate'/original.name
 r=subprocess.run(['D:/software/Nodev/node.exe',str(script)],capture_output=True,encoding='utf-8',timeout=30)
 compared=[]
 for file in sorted(copy.rglob('*')):
  if file.is_file() and file.suffix=='.json':
   rel=file.relative_to(copy);base=dataset/rel;actual=file.read_bytes();before=base.read_bytes() if base.exists() else b''
   def norm(data):return (data.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n').encode('utf-8')
   compared.append(dict(path=(dataset/rel).as_posix(),existsBefore=base.exists(),originalRawSha256=hashlib.sha256(before).hexdigest(),replayedRawSha256=hashlib.sha256(actual).hexdigest(),rawEqual=before==actual,normalizedEqual=norm(before)==norm(actual)))
 rows.append(dict(script=original.as_posix(),scriptRawSha256=hashlib.sha256(original.read_bytes()).hexdigest(),command=['D:/software/Nodev/node.exe',str(script)],exitCode=r.returncode,stdout=r.stdout,stderr=r.stderr,artifacts=compared))
 print(original.parts[-4],r.returncode,'different',sum(not x['normalizedEqual'] for x in compared))
result=dict(schemaVersion='ird-golden-replay/1',taskId='REUSE-T01',reviewedCommit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),nodeVersion=subprocess.check_output(['D:/software/Nodev/node.exe','--version'],text=True).strip(),isolation=str(out),scripts=rows,counts=dict(executed=len(rows),passed=sum(x['exitCode']==0 for x in rows),failed=sum(x['exitCode']!=0 for x in rows),skipped=0,notRun=0),scope='脚本进程执行/隔离产物对照；非产品C++测试，非数学正确性或全T01 PASS')
(root/'.cache/b16-golden-replay.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
