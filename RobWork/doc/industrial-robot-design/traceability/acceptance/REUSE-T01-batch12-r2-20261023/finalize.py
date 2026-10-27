import json,pathlib,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo'
def load(n):return json.loads((ev/n).read_text(encoding='utf-8-sig'))
facts=load('facts.json');gates={n:int((ev/f'{n}.exit.txt').read_text(encoding='utf-8-sig').strip()) for n in ['task','reuse','docs']};assert all(v==0 for v in gates.values())
assert int((ev/'check-rework.exit.txt').read_text(encoding='utf-8-sig').strip())==0
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,encoding='utf-8').strip();assert head==facts['reviewedCommit']
status=subprocess.check_output(['git','status','--porcelain'],cwd=repo,encoding='utf-8');assert status=='';(ev/'git-status-end.log').write_text(status)
report=dict(schemaVersion='ird-independent-acceptance/1',taskId='REUSE-T01',batch='B12-r2',reviewedCommit=head,comparisonBase=facts['comparisonBase'],verdict='PASS',scope='仅独立复验B12-P2-01文档/矩阵返工并关闭此项，不重新授予B12全部语义或全T01 PASS。',closedFindings=['B12-P2-01'],newFindings=[],facts=facts,semanticReview=dict(running='当前监督运行态；start置true，stop及自然退出置false，acquire getter',history='storeClosedObserved/abandonDiagObserved/gaveUp只置true，start/stop不复位，acquire getter',source='CloseSupervisor.hpp:161-178；CloseSupervisor.cpp:73-76,92,128-129,143,157,165,176',descriptionAndMatrixAgree=True,onlyExpectedRowChanges=True,pendingRelationsRemain=34),gates=dict(exitCodes=gates,passed=3,failed=0,skipped=0,scope='治理结构/文档/任务，不代表算法合规'),archive=dict(originalFrozenCommit='d38e4472ec403d9d9f1b7201a0f8e2b4b83f7a0c',originalVerdict='FAIL',findingPreserved='B12-P2-01',files=47,rawMatches=44,newlineOnly=3,missingOrContentMismatches=0),checkoutClean=True,repositoryModified=False,committed=False,pushed=False,merged=False,subagentsCreated=False,wholeT01Pass=False,remoteDelivery=dict(status='NOT DELIVERED / NOT VERIFIED',reason='委派说明远程当前不可达；本轮仅使用本地冻结源做独立验收，未推送、未验证远程可达或交付。',taskCompleteClaimed=False),notRun=['C++构建/测试','GUI','数值/性能','原B12其余语义重验','历史API探测重跑','DeepEvidence','全T01退出','远程推送/CI'],reportRoot=str(ev))
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
text=f'''# REUSE-T01 B12-P2-01 返工独立复验

结论：**PASS，仅关闭 B12-P2-01**。冻结 `{head}`，父提交 `{facts['comparisonBase']}`。独立Temp clone及checkout均exit0，已核完整SHA和父关系，初末工作树干净。

## 复验事实

- 说明 `test-tools-lifecycle-audit.md:23` 与 `workflow.CloseSupervisor.running` 矩阵已明确：running为当前监督运行态，start置true、stop或自然退出清false；另外三个getter保留历史观测事实，start/stop不清零。四者均acquire读取，不产生第二状态权威。核对 `CloseSupervisor.hpp:161-178` 和 `CloseSupervisor.cpp:73-76,92,128-129,143,157,165,176`，描述与实际写路径相符。
- 34条B12关系均更新说明摘要至 `{facts['newNoteDigest']}`，34/34匹配。逐条对原B12登记比较，除说明摘要及running组differences外字段一致；sources/inputs/target/权限/decision/status未被顺改，34条仍pending。
- 原B12-r1归档逐文件对原独立TEMP比对：47个文件，44字节一致、3仅CRLF/LF不同，无缺漏或内容失配。acceptance仍是冻结d38e447…的FAIL并保留B12-P2-01，不回写历史结论。
- `batch12-rework.json` 的补查使用正确的 `fork\\s*\\(` 正则参数；独立执行exit0，两条Doxyfile popen注释命中与保存结果一致，stderr为空。原搜索保留，未用未命中宣告无上游能力。
- 相对父提交产品/上游/vcpkg源码diff为空；范围内git diff --check exit0。README如实写原FAIL及待独立复验，未提前宣告全T01通过。

## 实际验证与范围

validate-task、冻结Head的validate-reuse、validate-docs均exit0：passed=3、failed=0、skipped=0。复用门禁仅治理结构通过。返工核查脚本exit0，事实记录见facts.json、hash-checks.json、semantic-change.json、archive-checks.json、search-replay.json和getter-source-evidence.log；原命令输出见对应log/exit文件。

C++构建/测试、GUI、数值/性能、原B12其他语义重验、历史API重跑、DeepEvidence和全T01退出均 **NOT RUN**。本结论只关闭此P2，不授全T01 PASS、整文件关闭、生产权限或T02/T05准入；原B12其他通过项沿原冻结范围引用。

未实施、未改父树或冻结仓库、未提交/推送/合入、未派代理。**远程交付尚未完成/验证**：委派说明当前远程不可达，本轮仅从本地源建立独立冻结现场，未尝试推送，不称整个任务完成。报告与日志只写本Temp根。
'''
(ev/'acceptance.md').write_text(text,encoding='utf-8');print('PASS only B12-P2-01; gates=3 PASS; 34 hashes/47 archive files; remote delivery NOT DELIVERED/NOT VERIFIED')
