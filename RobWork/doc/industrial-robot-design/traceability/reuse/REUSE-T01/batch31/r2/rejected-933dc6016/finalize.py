import json,pathlib,hashlib,subprocess,xml.etree.ElementTree as ET
root=pathlib.Path(r'C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-b31-accept-df453223');repo=root/'clone2';head='933dc6016c078b7d674fea68af19c038e0388979'
def sha(b):return hashlib.sha256(b).hexdigest()
results=json.loads((root/'semantic-results.json').read_text(encoding='utf-8'))
search=json.loads(subprocess.check_output(['git','-C',str(repo),'show',head+':RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch31/search.json']))
for r,s in zip(results['searchRecords'],search['searches']):r['exitCode']=s['exit']
ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
tree=ET.parse(root/'build/ird_units_audit.vcxproj');dependencies=[]
for group in tree.findall('.//m:ItemDefinitionGroup',ns):
 if 'Release|x64' not in group.attrib.get('Condition',''):continue
 for item in group.findall('.//m:AdditionalDependencies',ns):
  for p in item.text.split(';'):
   path=pathlib.Path(p)
   if path.is_absolute() and path.is_file():dependencies.append({'path':str(path),'bytes':path.stat().st_size,'rawSha256':sha(path.read_bytes())})
results['actualReleaseLinkDependencies']=dependencies
results['actualExe']={'path':str(root/'build/Release/ird_units_audit.exe'),'rawSha256':sha((root/'build/Release/ird_units_audit.exe').read_bytes())}
protected=['RobWork/RobWorkStudio/src/rwslibs/industrialrobot','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg']
results['protectedSourceDiff']=subprocess.check_output(['git','-C',str(repo),'diff','--name-only','090075171884',head,'--']+protected,text=True).splitlines()
results['cloneStatusAfter']=subprocess.check_output(['git','-C',str(repo),'status','--short'],text=True)
(root/'semantic-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
exits={n:int((root/(n+'.exit.txt')).read_text(encoding='utf-8-sig').strip()) for n in ['configure','build','task','docs','reuse','scope','commit-lint','diff-check','diff-check-cr-at-eol','oracle','independent-oracle','semantic-check']}
report={'taskId':'REUSE-T01','batch':'B31','verdict':'FAIL','reviewedCommit':head,'base':'0900751718849863af1190ece61c8ddb1f1cf547','scopeCount':29,'findingIds':['B31-P2-01'],'gates':exits,'tests':results['tests'],'limits':['仅本批29路径/43新增关系/8刷新关系','不授全REUSE-T01/T02准入','F-966仍open，未修改产品算法','未运行全上游套件、全消费者、GUI、完整集成与跨平台测试','不安装依赖、不执行远程操作'],'independent':True,'sharedTreeWrites':'审计起步误写.cache/b31-accept-location.txt定位文件，已仅清理该自有文件；未改任何共享追踪文件'}
(root/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'verdict':report['verdict'],'gates':exits,'releaseDependencies':len(dependencies),'protectedSourceDiff':results['protectedSourceDiff'],'cloneStatusAfter':results['cloneStatusAfter']},ensure_ascii=False))
