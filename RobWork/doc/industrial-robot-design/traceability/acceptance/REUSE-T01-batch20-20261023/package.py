import pathlib,json,hashlib,zipfile,subprocess
R=pathlib.Path(__file__).parent;repo=R/'repo-isolated';report=json.loads((R/'acceptance.json').read_text());report['validatorBindings']=[{'path':p,'rawSha256':hashlib.sha256((repo/p).read_bytes()).hexdigest()} for p in ['RobWork/scripts/industrialrobot/verify-task.ps1','RobWork/scripts/industrialrobot/validate-task.ps1','RobWork/scripts/industrialrobot/validate-reuse.ps1','RobWork/scripts/industrialrobot/reuse-contract.ps1','RobWork/scripts/industrialrobot/validate-docs.ps1']];report['notIncluded']='B19门禁实现和test-governance-lite未提交修正不在冻结提交；本次不验收它们';(R/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
tracked=subprocess.check_output(['git','status','--porcelain=v1'],cwd=repo,text=True); assert tracked==''
files=sorted(p for p in R.iterdir() if p.is_file() and p.name not in ('artifact-manifest.json','evidence.zip'))
manifest={'excludedDirectories':['repo/','repo-isolated/','build/'],'files':[{'path':p.name,'bytes':p.stat().st_size,'rawSha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files]};(R/'artifact-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
with zipfile.ZipFile(R/'evidence.zip','w',zipfile.ZIP_DEFLATED) as z:
 for p in files+[R/'artifact-manifest.json']: z.write(p,p.name)
with zipfile.ZipFile(R/'evidence.zip') as z:
 assert not any('/' in n or '\\' in n for n in z.namelist())
 for f in manifest['files']: assert hashlib.sha256(z.read(f['path'])).hexdigest()==f['rawSha256']
print(json.dumps({'report':str(R/'acceptance.json'),'markdown':str(R/'acceptance.md'),'archive':str(R/'evidence.zip'),'archiveBytes':(R/'evidence.zip').stat().st_size,'artifacts':len(manifest['files']),'archiveVerified':True,'finalFrozenGitClean':True},ensure_ascii=False))
