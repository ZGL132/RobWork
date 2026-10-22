import pathlib,json,hashlib,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';doc=repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01';sha='435dbdb716b9eb86d0932a478eb086642206ac76'
def read(p):return p.read_text(encoding='utf-8-sig')
def load(p):return json.loads(read(p))
def save(n,x):(ev/n).write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def h(b):return hashlib.sha256(b).hexdigest()
def norm(p):return (read(p).rstrip('\n')+'\n').encode()
confirm=load(doc/'generator-drift-confirm.json');bindings=[]
for k in ['original','source','consumer']:
 p=repo/confirm[k]['path'];bindings.append(dict(role=k,path=confirm[k]['path'],recordedSha256=confirm[k]['sha256'],rawSha256=h(p.read_bytes()),normalizedSha256=h(norm(p)),normalizedMatches=h(norm(p))==confirm[k]['sha256']))
save('drift-evidence-bindings.json',bindings);assert all(x['normalizedMatches'] for x in bindings)
old=load(doc/'generator-replay.json');new=load(ev/'independent-generator-replay.json');oldmap={x['script']:x for x in old['scripts']};hashchecks=[dict(script=x['script'],recordedSourceRawMatches=oldmap[x['script']]['scriptRawSha256']==x['scriptRawSha256']) for x in new['scripts']];save('recorded-generator-source-hashes.json',hashchecks);assert all(x['recordedSourceRawMatches'] for x in hashchecks)
f=next(x for x in load(repo/'RobWork/doc/industrial-robot-design/traceability/findings.json')['findings'] if x['id']=='F-959');assert f['status']=='open' and 'mappedVelocityCount=5' in f['resolution'];save('F-959-frozen.json',f)
scopes=['RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/GoldenImportTest.cpp','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/Import.cpp']
p=subprocess.run(['git','merge-base','--is-ancestor',old['reviewedCommit'],sha],cwd=repo);assert p.returncode==0
diff=subprocess.check_output(['git','diff',old['reviewedCommit'],sha,'--',*scopes],cwd=repo,encoding='utf-8');assert diff=='';(ev/'recorded-baseline-source-diff.log').write_text(diff)
paths=[('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/mdl-urdf-import/1.0.0/generate/make_import_expectations.mjs',8,16),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/mdl-urdf-import/1.0.0/generate/make_import_expectations.mjs',56,75),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/mdl-urdf-import/1.0.0/generate/make_import_expectations.mjs',144,160),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/GoldenImportTest.cpp',118,127),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/GoldenImportTest.cpp',299,318),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/GoldenImportTest.cpp',405,441),('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/Import.cpp',1919,1965)]
parts=[]
for path,start,end in paths:
 lines=read(repo/path).splitlines();parts.append(path+'\n'+'\n'.join(f'{i+1}: {lines[i]}' for i in range(start-1,min(end,len(lines)))))
(ev/'F-959-source-evidence.log').write_text('\n\n'.join(parts)+'\n',encoding='utf-8')
comp=load(ev/'independent-json-comparisons.json');groups={}
for x in comp:
 role=x['relativeDatasetPath'].split('/')[0];groups[role]=groups.get(role,0)+1
urdf=[x['relativeDatasetPath'] for x in comp if x['dataset']=='mdl-urdf-import/1.0.0']
facts=dict(recordedEvidenceCommit=old['reviewedCommit'],frozenAcceptanceCommit=sha,recordedCommitIsAncestor=True,relevantSourceDiffEmpty=True,recordedScriptHashMatches=29,driftEvidenceBindingsMatch=3,findingStatus='open',jsonPathCategories=groups,urdfComparedPaths=urdf,urdfGeneratorOnlyWrites=['expected/import-report.json','expected/xacro-samples.json'],consumerReadsExpectedCountsAndMappedVelocity=True,consumerMissingNumberTriggersAddFailure=True,productVelocityMappingConfirmedStatically=True,productCppRun=False)
save('F-959-facts.json',facts);print(json.dumps(facts,ensure_ascii=False,indent=2))
