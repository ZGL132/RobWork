import pathlib,json,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';sha='435dbdb716b9eb86d0932a478eb086642206ac76';base='a703b88225b1e094947b46723706b5be8af5159f'
def read(p):return p.read_text(encoding='utf-8-sig')
def load(n):return json.loads(read(ev/n))
s=load('execution-summary.json');facts=load('F-959-facts.json');assert s['reviewedCommit']==sha
assert int(read(ev/'replay-driver.exit.txt').strip())==0 and int(read(ev/'second-confirm.exit.txt').strip())==0 and int(read(ev/'finish-execution.exit.txt').strip())==0 and int(read(ev/'check-f959.exit.txt').strip())==0
assert s['processResults']==dict(passed=29,failed=0,skipped=0,notRun=0) and s['jsonComparisons']['normalizedEqual']==90 and s['jsonComparisons']['normalizedDifferent']==1
assert load('source-before-hashes.json')==load('source-after-hashes.json')
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,encoding='utf-8').strip()==sha and subprocess.check_output(['git','status','--porcelain'],cwd=repo,encoding='utf-8')==''
diff=subprocess.check_output(['git','diff',base,sha,'--','RobWork/RobWorkStudio/src','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg'],cwd=repo,encoding='utf-8');assert diff=='';(ev/'product-upstream-diff.log').write_text(diff)
# 补全URDF输出两条写入语句的冻结源码，避免仅由摘要推断文件角色。
sp=repo/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/mdl-urdf-import/1.0.0/generate/make_import_expectations.mjs';lines=read(sp).splitlines();out='\n'.join(f'{i+1}: {lines[i]}' for i in range(max(0,len(lines)-15),len(lines)))+'\n';(ev/'urdf-generator-write-sites.log').write_text(out,encoding='utf-8')
report=dict(schemaVersion='ird-independent-acceptance/1',taskId='REUSE-T01',batch='B16-generator-execution',reviewedCommit=sha,comparisonBase=base,verdict='PASS',scope='仅29生成器隔离执行证据、91 dataset JSON对照与F-959复现及消费者源码核对；不含43行语义验收。',processExecution=dict(result='PASS',expected=29,executed=29,passed=29,failed=0,skipped=0,notRun=0),regenerationConsistency=dict(result='FAIL',jsonComparisons=91,passed=90,failed=1,skipped=0,notRun=0,datasetResults=dict(passed=28,failed=1),rawEqual=90,rawDifferent=1,allComparedFilesAreGenerated=False,categories=facts['jsonPathCategories'],differentPaths=s['differentPaths']),repeatConfirmation=dict(result='PASS',executed=1,passed=1,failed=0,skipped=0,notRun=0,secondOutputRawEqualsFirst=True,outputRawSha256=load('independent-drift-confirm.json')['secondOutputRawSha256'],scope='只重复URDF漂移dataset，未将它混入主29脚本计数。'),findings=[dict(id='F-959',status='open',independentlyReproduced=True,newFinding=False,generatedUnsupported=8,generatedJointVelocityLimit=5,generatedMappedVelocityCountAbsent=True,frozenUnsupported=3,frozenMappedVelocityCount=5,consumerReadsCounts=True,consumerMissingFieldAddsFailure=True,productionVelocityMappingObservedStatically=True,cppConsumerExecuted=False)],executionSummary=s,evidenceFacts=facts,recordedEvidenceIdentity=dict(recordedCommit=facts['recordedEvidenceCommit'],isAncestor=True,relevantSourcesUnchanged=True,all29ScriptRawHashesMatch=True,currentIndependentRunBoundTo=sha),sourceReadOnly=dict(filesBeforeAfterMatched=148,originalDataUnchanged=True,finalCheckoutClean=True,cacheRelocatedTo=str(ev/'first-run-cache')),wrapperIncident=dict(initialExit=1,scope='独立外层驱动末尾工作树检查；冻结replay脚本与29 Node进程均已exit0，第二目录确认已完成。',reason='新建.cache未被Git忽略，驱动原clean断言不成立。',resolution='保留原run-replay.log；只将独立输出.cache移动到Temp根first-run-cache，继续完成收尾核对，未重跑29脚本。',finishExit=0),reproduction=dict(unchangedFrozenScript=True,requiredPreparation='从冻结repo根运行，先创建空.cache父目录且保证.cache/b16-golden-replay尚不存在。',commandRecord='driver-command.json',nodeRuntime=load('runtime.json')),repositoryModified=False,parentWorktreeModified=False,committed=False,pushed=False,merged=False,subagentsCreated=False,wholeT01Pass=False,newCoreApproved=False,notRun=['43行函数语义验收（另行负责）','产品C++构建/消费者执行','GUI/数值/性能验收','task/reuse/docs门禁（非本次分工）','全T01退出/T02准入','远程推送/CI'],reportRoot=str(ev))
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
md=f'''# REUSE-T01 B16 生成器执行证据独立验收

结论：**执行证据验收 PASS；再生一致性 FAIL（1/91），F-959保持open**。独立冻结 `{sha}`，父 `{base}`。本报告仅涵盖生成器执行与F-959，43行语义由另一验收范围负责，不授全T01或新核心通过。

## 真实执行与分列计数

| 项目 | executed/total | passed | failed | skipped | notRun |
| --- | ---: | ---: | ---: | ---: | ---: |
| 主批Node进程 | 29 | 29 | 0 | 0 | 0 |
| dataset JSON规范化对照 | 91 | 90 | 1 | 0 | 0 |
| dataset一致性 | 29 | 28 | 1 | 0 | 0 |
| 第二目录URDF重复执行 | 1 | 1 | 0 | 0 | 0 |

原样运行冻结 `replay-generators.py`，其自身exit0；Node为 `D:/software/Nodev/node.exe`，v24.14.1，执行器SHA见runtime.json。脚本前置要求.cache父目录已存在、输出子目录不存在，已在新独立clone中显式准备，命令与cwd见driver-command.json。29个dataset各复制一份后逐mjs真实执行；完整stdout/stderr/exit在independent-generator-replay.json和generator-01～29日志中。

91是dataset JSON对照数，包含expected=32、inputs=30、manifest=29，**不是生成文件数**。例如URDF脚本只写两个expected JSON，该dataset manifest仅随dataset复制并参与对照。独立补查原始集合及复制后集合均为91，无文件新增/缺失被遗漏。原始字节也是90相同、1不同，不是换行漂移。

## F-959复现及真实消费者

唯一不同为 `mdl-urdf-import/1.0.0/expected/import-report.json`：旧脚本输出unsupported=8、含joint-velocity-limit×5，且缺mappedVelocityCount；冻结入库文件为unsupported=3、mappedVelocityCount=5，并有F01说明。第二个全新目录再次从原dataset复制并运行该mjs，exit0，import-report与第一次逐字节一致，SHA `{load('independent-drift-confirm.json')['secondOutputRawSha256']}`；第二目录其余JSON也与第一次相同。

消费者 `modeling/test/GoldenImportTest.cpp:305-315` 实际读取该expected并调用mapUrdf；406-420核unsupported总数/分布，429-440统计真实mapped中的.maxVelocity行并读mappedVelocityCount；jNum在119-126对缺字段ADD_FAILURE。`modeling/src/Import.cpp:1919-1962` 真实把合法velocity写entry.maxVelocity及report.mapped。消费者旧“unsupported=8”等注释不能替代实际断言。此处是源码核对，**未执行产品C++消费者**，不声称已实跑其失败。

F-959登记仍open，描述与独立实跑一致；本验收不修脚本、不回退正确入库字段，不授权新oracle核心。后续修复应同步过期生成脚本及相关注释/回归，不能用旧错误产物覆盖黄金值洗绿。

## 证据身份、原件与限制

实施者保存的generator-replay.json绑定祖先a78b41ff…；已核该提交是当前冻结祖先，29脚本原始摘要全部一致，相关testdata/消费者/Import源码到435db冻结无diff。drift-confirm中的原expected、脚本、消费者三项规范化摘要均匹配。本独立执行JSON明确绑定435db冻结，无借用旧执行冒充当前运行。

原golden目录148文件执行前后原始摘要全部相同，父树未写。外层独立驱动首次在最后clean断言exit1，因为本次生成的.cache并不被Git忽略；冻结重放脚本和29Node进程已全部成功，第二目录确认也已完成。保留run-replay.log和该exit1，随后仅将本次输出.cache移动至本Temp根first-run-cache，finish-execution exit0，Git最终干净，未重复执行29脚本或掩盖失败。命令历史及移动映射均留档。

关键证据：execution-summary.json、independent-json-comparisons.json、independent-generator-drift.diff、independent-drift-confirm.json、F-959-source-evidence.log、urdf-generator-write-sites.log、F-959-facts.json与原始日志。独立clone/checkout均exit0，相对父产品/上游/vcpkg无diff。未实施、未提交/推送/合并、未创建子代理；产品C++、GUI、性能、task/reuse/docs门禁和全T01退出均NOT RUN（不属于本次分工）。
'''
(ev/'acceptance.md').write_text(md,encoding='utf-8');print('ACCEPTANCE PASS for execution evidence/F-959 reproduction; process 29 PASS; JSON consistency FAIL 90/91; repeat confirmed; original 148 files unchanged; no product C++')
