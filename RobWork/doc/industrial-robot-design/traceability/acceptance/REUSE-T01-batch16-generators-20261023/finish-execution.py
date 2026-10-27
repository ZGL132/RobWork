import pathlib,json,subprocess,shutil,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo'
def load(n):return json.loads((ev/n).read_text(encoding='utf-8-sig'))
def save(n,x):(ev/n).write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
result=load('independent-generator-replay.json');comp=load('independent-json-comparisons.json');second=load('independent-drift-confirm.json');assert load('source-before-hashes.json')==load('source-after-hashes.json')
src=(repo/'.cache').resolve();dest=(ev/'first-run-cache').resolve();assert src.parent==repo.resolve() and src.name=='.cache' and dest.parent==ev.resolve() and not dest.exists()
status=subprocess.check_output(['git','status','--porcelain'],cwd=repo,encoding='utf-8');(ev/'git-status-with-generated-cache.log').write_text(status,encoding='utf-8');assert status.strip()=='?? .cache/'
shutil.move(str(src),str(dest));save('artifact-relocation.json',dict(originalExecutionOutput=str(src),savedAt=str(dest),reason='冻结脚本写入.cache目录不被Git忽略；只移动本独立运行的输出缓存，原testdata未改。',nodeRerun=False))
status=subprocess.check_output(['git','status','--porcelain'],cwd=repo,encoding='utf-8');assert status=='';(ev/'git-status-end.log').write_text(status)
assert result['counts']==dict(executed=29,passed=29,failed=0,skipped=0,notRun=0) and len(comp)==91 and sum(not x['normalizedEqual'] for x in comp)==1 and second['exitCode']==0 and second['secondEqualsFirst']
summary=dict(reviewedCommit=result['reviewedCommit'],comparisonBase='a703b88225b1e094947b46723706b5be8af5159f',driverExit=int((ev/'replay-driver.exit.txt').read_text().strip()),datasetsCopied=29,scriptsExpected=29,scriptsExecuted=29,processResults=dict(passed=29,failed=0,skipped=0,notRun=0),jsonComparisons=dict(total=91,normalizedEqual=sum(x['normalizedEqual'] for x in comp),normalizedDifferent=sum(not x['normalizedEqual'] for x in comp),rawEqual=sum(x['rawEqual'] for x in comp),rawDifferent=sum(not x['rawEqual'] for x in comp),result='FAIL'),differentPaths=[x['path'] for x in comp if not x['normalizedEqual']],all91AreGeneratedFilesClaimed=False,secondConfirmExit=0,secondEqualsFirst=True,sourceFilesHashed=len(load('source-before-hashes.json')),originalDataUnchanged=True,checkoutClean=True,recordedReviewedCommit='a78b41ff29261b17fc8379ee27e4afb1933ab827',independentWrapper=dict(initialExit=1,reason='全部生成/对照/第二目录确认已完成；首次收尾clean断言发现本次输出.cache未被Git忽略。保留run-replay.log，移动输出后继续收尾通过，未重新执行29脚本。',finishExit=0))
save('execution-summary.json',summary);print(json.dumps(summary,ensure_ascii=False,indent=2))
