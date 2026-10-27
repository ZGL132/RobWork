import pathlib,json,hashlib,subprocess
ev=pathlib.Path(__file__).parent; repo=ev/'repo'
r=json.loads((ev/'independent-review.json').read_text(encoding='utf-8'))
def exit_code(name):return int((ev/(name+'.exit.txt')).read_text(encoding='utf-8-sig').strip())
checks={n:{'exitCode':exit_code(n),'log':n+'.log'} for n in ['ps7','ps51','task','reuse','docs','scope-probe','diff-check','active-diff-check','inspect','baseline-red-replay','real-final-debt']}
assert all(c['exitCode']==0 for n,c in checks.items() if n not in ['diff-check','baseline-red-replay','real-final-debt'])
assert checks['diff-check']['exitCode']==2
assert checks['baseline-red-replay']['exitCode']!=0 and 'REUSE-UPSTREAM-TARGET : TEST-HARNESS: target evidence required' in (ev/'baseline-red-replay.log').read_text(encoding='utf-8-sig')
assert checks['real-final-debt']['exitCode']!=0 and 'REUSE-MIGRATION : unresolved function' in (ev/'real-final-debt.log').read_text(encoding='utf-8-sig')
diff=(ev/'diff-check.log').read_text(encoding='utf-8-sig')
diagnostics=[line for line in diff.splitlines() if ': trailing whitespace.' in line or ': new blank line at EOF.' in line]
assert diagnostics and all('/traceability/acceptance/' in line or '/gates/gtest-target/' in line for line in diagnostics)
assert (ev/'protected-scope-diff.log').read_text(encoding='utf-8-sig').strip()==''
assert not subprocess.check_output(['git','-C',str(repo),'status','--short'],text=True).strip()
report={'schemaVersion':'ird-independent-gtest-target-gate-acceptance/1','taskId':'REUSE-T01','verdict':'pass','reviewedCommit':r['frozenSha'],'baseCommit':r['baseSha'],'scope':'仅gtest目标路径门禁与隔离142项驱动补正，不含第七批其他登记、生产迁移或全T01退出','independentClone':str(repo),'trackedStatus':'','checks':checks,'selftests':r['independentRuns'],'archivedReports':r['archiveHistory'],'rawFrozenScriptSha256':r['rawFrozenScriptSha256'],'selectedNegativeAndPositiveCases':r['selectedActualTests'],'additionalBoundaryProbes':r['additionalScopeProbes'],'isolationReview':r['isolationReview'],'protectedScopeUnchanged':r['protectedScopeUnchanged'],'baselineRedReplay':'基线版本校验器针对冻结真实catalog重现REUSE-UPSTREAM-TARGET TEST-HARNESS；当前版本真实task/reuse/docs通过。','finalDebtStillRejected':'真实TaskFile + RequireMigrationComplete仍因未决函数拒绝；两套完整驱动22-final-debt-rejected也真实exit1。','relationCounts':r['relationCounts'],'findings':[],'observations':['全差异diff-check=2，仅历史归档日志原文尾空白/EOF空行，未洗历史。活动门禁/驱动/契约/README的独立diff-check=0。','精确gtest根允许仅是来源登记；不授权产品链接带main或全局初始化的测试入口。','两个关闭负例在隔离副本排除现有未决矩阵后各自构造相关行，PutJson记录原字节并由Case finally恢复；不会清空真实库存/债务。'],'notRun':['无关C++/Qt GUI构建与产品测试（本轮产品/上游/vcpkg/库存未改）','第七批其他逐功能登记验收','全T01语义审计退出、生产迁移或T02准入','DeepEvidence全库证据深查、远程CI','提交/推送/合入'],'evidenceFiles':'evidence-files.json','details':'independent-review.json'}
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# REUSE-T01 gtest目标门禁补正独立验收：PASS

冻结SHA `{r['frozenSha']}`；基点 `{r['baseSha']}`。独立随机Temp clone、detached HEAD，使用冻结版本的validator/helper/driver；最终git status为空。未改父树、产品、上游、vcpkg或库存，未提交/推送/合并。

PowerShell 7.6.5与Windows PowerShell 5.1分别完整真实运行142项：各passed=142、failed=0、skipped=0、notRun=0、complete=true；两个进程exit0。每项日志与JSON输出逐项一致，JUnit计数及全部142个唯一用例匹配。真实REUSE-T01 task/docs/reuse均exit0，仅是治理检查。两套独立报告中的validator/helper/driver原始SHA256均等于冻结源码；归档r4的两套报告摘要也全部匹配。

允许范围核对：校验器只从原src子目录路径增加`RobWork/RobWork/gtest/CMakeLists.txt`和`RobWork/RobWorkSim/gtest/CMakeLists.txt`两精确根。原有文件/符号/摘要/目标检查保留，SafePath未改；reuse-contract.ps1未变。驱动sparse checkout补齐两gtest根。独立补充8项实跑：两真实根（sdurw-gtest-main/sdurwsim-gtest-main）通过；两嵌套fake目录、gtest2、RobWorkStudio/gtest、CMakeLists.txt.fake与真实根伪造目标均exit1并报REUSE-UPSTREAM-TARGET。

两套完整驱动中的新正例、任意测试目录拒绝例、两个新文件最终关闭负例均按预期真实执行。关闭负例输出明确为“new/current file lacks independent closure”，没有被无关pending抢先截断。IsolateFinalClosureMatrices只在这两个Case调用，变动仅在隔离副本；PutJson记录原字节、Case finally恢复。01-clean仍看到200关系/closedFiles=0及DEBT=16/UNRESOLVED=1399；22-final-debt-rejected保留真实矩阵并拒绝。另对真实REUSE-T01加RequireMigrationComplete独立重跑，仍以未决函数拒绝，债务未被放行。

独立重放基线版本validator对冻结真实catalog，得到原REUSE-UPSTREAM-TARGET TEST-HARNESS错误；当前门禁通过，修正对应实际触发点。归档r1/r2/r3/r4均保留：r1各executed30/passed7/failed23/notRun112，r2各142/139/3/0，r3各142/141/1/0，r4各142/142/0/0；JSON/JUnit真实失败及driver未完成状态一致，没有把complete=true当作零失败。

全量diff-check=2，只来自历史归档日志尾空白/EOF空行；原文保留，未洗历史。活动validator/driver/audit-contract/gtest-target README单列diff-check=0。源码/上游/vcpkg/库存差异为空，因此无须无关C++或GUI构建。累计200关系=197 pending/3 proposal，closedFiles=0。

PASS只限本门禁补正；不代表第七批其他登记通过、全T01退出、生产迁移或T02准入。未跑无关产品/上游测试、DeepEvidence、远程CI；未提交推送合并。原始日志/报告在ps7、ps51及本目录；归档文件清单见evidence-files.json，勿递归复制repo或scope-fixture。
'''
(ev/'acceptance.md').write_text(md,encoding='utf-8')
paths=[p for p in ev.iterdir() if p.is_file() and p.name!='evidence-files.json']
for folder in ['ps7','ps51']:
    paths += [p for p in (ev/folder).iterdir() if p.is_file()]
manifest=[{'path':p.relative_to(ev).as_posix(),'bytes':p.stat().st_size,'rawSha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(paths)]
(ev/'evidence-files.json').write_text(json.dumps({'scope':'Only listed evidence files; exclude repo/scope-fixture/baseline-validator directories','files':manifest},ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS: '+str(ev/'acceptance.md'))
print('Evidence files: '+str(len(manifest)))
