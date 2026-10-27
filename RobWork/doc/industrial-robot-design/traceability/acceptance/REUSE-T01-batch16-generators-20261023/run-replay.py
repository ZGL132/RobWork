import pathlib,json,hashlib,subprocess,sys,shutil,difflib,os
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';sha='435dbdb716b9eb86d0932a478eb086642206ac76';base='a703b88225b1e094947b46723706b5be8af5159f';prefix=pathlib.Path('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden');golden=repo/prefix;doc=repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01';node=pathlib.Path('D:/software/Nodev/node.exe')
def read(p):return p.read_text(encoding='utf-8-sig')
def load(p):return json.loads(read(p))
def save(n,x):(ev/n).write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def h(b):return hashlib.sha256(b).hexdigest()
def norm(b):return (b.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')+'\n').encode()
def snapshot():return {p.relative_to(repo).as_posix():h(p.read_bytes()) for p in sorted(golden.rglob('*')) if p.is_file()}
def git(*a):return subprocess.check_output(['git',*a],cwd=repo,encoding='utf-8')
assert git('rev-parse','HEAD').strip()==sha and git('rev-parse','HEAD^').strip()==base and git('status','--porcelain')==''
(ev/'frozen-sha.txt').write_text(sha+'\n');(ev/'comparison-base.txt').write_text(base+'\n');(ev/'git-status-start.log').write_text('')
scripts=sorted(golden.glob('*/*/generate/*.mjs'));datasets={p.parent.parent for p in scripts};assert len(scripts)==len(datasets)==29
before=snapshot();save('source-before-hashes.json',before)
nodeVersion=subprocess.check_output([str(node),'--version'],encoding='utf-8').strip();save('runtime.json',dict(node=str(node),nodeVersion=nodeVersion,nodeRawSha256=h(node.read_bytes()),python=sys.executable))
assert not (repo/'.cache').exists();(repo/'.cache').mkdir();assert not (repo/'.cache/b16-golden-replay').exists()
command=[sys.executable,str(doc/'replay-generators.py')];save('driver-command.json',dict(cwd=str(repo),preparation='创建空的 .cache 父目录；冻结脚本out.mkdir不创建父级，原脚本未改。',arguments=command))
driver=subprocess.run(command,cwd=repo,capture_output=True,encoding='utf-8',timeout=180)
(ev/'replay-driver.stdout.log').write_text(driver.stdout,encoding='utf-8');(ev/'replay-driver.stderr.log').write_text(driver.stderr,encoding='utf-8');(ev/'replay-driver.exit.txt').write_text(str(driver.returncode)+'\n');assert driver.returncode==0
result=load(repo/'.cache/b16-golden-replay.json');save('independent-generator-replay.json',result);assert result['reviewedCommit']==sha and result['counts']==dict(executed=29,passed=29,failed=0,skipped=0,notRun=0)
old=load(doc/'generator-replay.json');oldMap={x['script']:x for x in old['scripts']};assert len(oldMap)==29
comparisons=[];cross=[]
for i,r in enumerate(result['scripts'],1):
 original=repo/r['script'];dataset=original.parent.parent;copy=repo/'.cache/b16-golden-replay'/dataset.relative_to(golden)
 assert r['exitCode']==0 and r['scriptRawSha256']==h(original.read_bytes()) and len(r['command'])==2 and pathlib.Path(r['command'][0])==node
 (ev/f'generator-{i:02}.stdout.log').write_text(r['stdout'],encoding='utf-8');(ev/f'generator-{i:02}.stderr.log').write_text(r['stderr'],encoding='utf-8')
 for src in sorted(dataset.rglob('*.json')):
  target=copy/src.relative_to(dataset);assert target.is_file();a=src.read_bytes();b=target.read_bytes();match=norm(a)==norm(b)
  recorded=next(x for x in r['artifacts'] if x['path']==src.relative_to(repo).as_posix());assert recorded['normalizedEqual']==match and recorded['rawEqual']==(a==b)
  comparisons.append(dict(dataset=dataset.relative_to(golden).as_posix(),path=src.relative_to(repo).as_posix(),relativeDatasetPath=src.relative_to(dataset).as_posix(),originalRawSha256=h(a),replayedRawSha256=h(b),rawEqual=a==b,normalizedEqual=match))
 assert len(r['artifacts'])==len(list(dataset.rglob('*.json')))==len(list(copy.rglob('*.json')))
 oldr=oldMap[r['script']];cross.append(dict(script=r['script'],exitMatches=r['exitCode']==oldr['exitCode'],artifactPathsMatch=sorted(x['path'] for x in r['artifacts'])==sorted(x['path'] for x in oldr['artifacts']),consistencyOutcomesMatch={x['path']:x['normalizedEqual'] for x in r['artifacts']}=={x['path']:x['normalizedEqual'] for x in oldr['artifacts']}))
assert len(comparisons)==91;different=[x for x in comparisons if not x['normalizedEqual']];assert len(different)==1 and different[0]['relativeDatasetPath']=='expected/import-report.json'
assert all(all(x[k] for k in ['exitMatches','artifactPathsMatch','consistencyOutcomesMatch']) for x in cross)
save('independent-json-comparisons.json',comparisons);save('recorded-replay-cross-check.json',cross)
dataset=golden/'mdl-urdf-import/1.0.0';first=repo/'.cache/b16-golden-replay/mdl-urdf-import/1.0.0';second=ev/'second-confirm-dataset';assert not second.exists();shutil.copytree(dataset,second)
secondCommand=[str(node),str(second/'generate/make_import_expectations.mjs')];r2=subprocess.run(secondCommand,cwd=second,capture_output=True,encoding='utf-8',timeout=30);assert r2.returncode==0
(ev/'second-confirm.stdout.log').write_text(r2.stdout,encoding='utf-8');(ev/'second-confirm.stderr.log').write_text(r2.stderr,encoding='utf-8');(ev/'second-confirm.exit.txt').write_text(str(r2.returncode)+'\n')
firstReport=(first/'expected/import-report.json').read_bytes();secondReport=(second/'expected/import-report.json').read_bytes();originalReport=(dataset/'expected/import-report.json').read_bytes();assert firstReport==secondReport
originalJson=json.loads(originalReport);generatedJson=json.loads(secondReport);assert originalJson['counts']['unsupported']==3 and originalJson['counts']['mappedVelocityCount']==5 and generatedJson['counts']['unsupported']==8 and 'mappedVelocityCount' not in generatedJson['counts'] and dict((x['kind'],x['count']) for x in generatedJson['counts']['unsupportedKinds'])['joint-velocity-limit']==5
conf=load(doc/'generator-drift-confirm.json');assert conf['secondEqualsFirst']==True and h(secondReport)==conf['secondOutputRawSha256'];assert norm(originalReport)==norm((repo/conf['original']['path']).read_bytes())
(ev/'independent-generator-drift.diff').write_text(''.join(difflib.unified_diff(read(dataset/'expected/import-report.json').splitlines(keepends=True),read(second/'expected/import-report.json').splitlines(keepends=True),fromfile='frozen-original/expected/import-report.json',tofile='second-confirm/expected/import-report.json')),encoding='utf-8')
secondChecks=[]
for f in sorted(dataset.rglob('*.json')):
 rel=f.relative_to(dataset);a=(first/rel).read_bytes();b=(second/rel).read_bytes();secondChecks.append(dict(path=rel.as_posix(),secondRawEqualsFirst=a==b,secondNormalizedEqualsOriginal=norm(b)==norm(f.read_bytes())))
assert all(x['secondRawEqualsFirst'] for x in secondChecks)
save('independent-drift-confirm.json',dict(command=secondCommand,cwd=str(second),exitCode=r2.returncode,secondEqualsFirst=True,secondOutputRawSha256=h(secondReport),originalOutputRawSha256=h(originalReport),jsonChecks=secondChecks,originalCounts=originalJson['counts'],generatedCounts=generatedJson['counts'],productTestsRun=False))
after=snapshot();assert before==after;save('source-after-hashes.json',after)
status=git('status','--porcelain');assert status=='';(ev/'git-status-end.log').write_text(status)
summary=dict(reviewedCommit=sha,comparisonBase=base,driverExit=0,datasetsCopied=29,scriptsExpected=29,scriptsExecuted=29,processResults=dict(passed=29,failed=0,skipped=0,notRun=0),jsonComparisons=dict(total=91,normalizedEqual=sum(x['normalizedEqual'] for x in comparisons),normalizedDifferent=len(different),rawEqual=sum(x['rawEqual'] for x in comparisons),rawDifferent=sum(not x['rawEqual'] for x in comparisons),result='FAIL'),differentPaths=[x['path'] for x in different],all91AreGeneratedFilesClaimed=False,secondConfirmExit=0,secondEqualsFirst=True,sourceFilesHashed=len(before),originalDataUnchanged=True,checkoutClean=True,recordedReviewedCommit=old['reviewedCommit'],recordedMetadataNote='已保存再生证据绑定a78b父代祖先；本独立实跑绑定435db冻结。源码是否相同另按git对象核对。')
save('execution-summary.json',summary);print(json.dumps(summary,ensure_ascii=False,indent=2))
