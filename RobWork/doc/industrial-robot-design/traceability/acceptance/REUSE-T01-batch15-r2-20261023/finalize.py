import pathlib,json,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo'
def read(p):return p.read_text(encoding='utf-8-sig')
def load(n):return json.loads(read(ev/n))
facts=load('facts.json');gates={n:int(read(ev/f'{n}.exit.txt').strip()) for n in ['task','reuse','docs']};assert all(v==0 for v in gates.values())
assert int(read(ev/'check-rework.exit.txt').strip())==0 and int(read(ev/'clone.exit.txt').strip())==0 and int(read(ev/'checkout.exit.txt').strip())==0
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,encoding='utf-8').strip()==facts['reviewedCommit'];assert subprocess.check_output(['git','status','--porcelain'],cwd=repo,encoding='utf-8')=='';(ev/'git-status-end.log').write_text('')
report=dict(schemaVersion='ird-independent-acceptance/1',taskId='REUSE-T01',batch='B15-r2',reviewedCommit=facts['reviewedCommit'],comparisonBase=facts['comparisonBase'],verdict='PASS',scope='仅独立复验B15-P2-01并关闭此项；不重授原B15其余语义、全UI/T01或T02通过。',closedFindings=['B15-P2-01'],newFindings=[],facts=facts,semanticReview=dict(actualCMake='ui/CMakeLists.txt:416-427，if(TARGET sdurws)下target_sources(sdurws_ird_ui_test PRIVATE ...)在425行直接编入QtTextBillboard.cpp',fiveTargetRowsCorrect=True,allNoteReferencesFresh=True,unrelatedRowsUnchangedExceptNoteDigest=True,sourceCMakeUnchangedSinceOriginalB15=True,authorityAndGrantsUnchanged=True,restrictedPermissionsRemainPending=True),archive=dict(originalVerdict='FAIL',originalCommit='85e561539a4038e37231dcfcb506109a5a188151',findingPreserved='B15-P2-01',files=50,rawMatches=47,newlineOnly=3,missingOrContentMismatches=0),gates=dict(exitCodes=gates,passed=3,failed=0,skipped=0,scope='治理结构/文档/任务，不代表语义或算法合规'),checkoutClean=True,repositoryModified=False,committed=False,pushed=False,merged=False,subagentsCreated=False,wholeT01Pass=False,t02Approved=False,remoteDelivery=dict(userReportedPushedCommit=facts['reviewedCommit'],independentlyRechecked=False,detail='委派已明确该冻结提交成功推远程。本轮不执行推送或重复远程核验；原FAIL中的历史网络状态保留。'),notRun=['C++构建/产品测试','GUI/GL/真实后端','数值/性能','原B15其余语义重验','DeepEvidence','全T01退出','T02准入验收','远程推送/CI重验'],reportRoot=str(ev))
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
text=f'''# REUSE-T01 B15-P2-01 返工独立复验

结论：**PASS，仅关闭 B15-P2-01**。独立冻结 `{facts['reviewedCommit']}`，父 `{facts['comparisonBase']}`。新建独立Temp clone并启用longpaths检出，clone/checkout均exit0，初末工作树干净。

## 复验事实

- 五条QtText关系均已增加 `sdurws_ird_ui_test`，没有移除原plugin/studio目标或新增重复项。实际 `ui/CMakeLists.txt:416-427` 在 `if(TARGET sdurws)` 条件下以 `target_sources(sdurws_ird_ui_test PRIVATE ...)` 直接编入 `QtTextBillboard.cpp`（425行）；该CMake自原B15冻结85e561…未变。本批24条目标集合现全部与原已核实际编译集合匹配。
- `host-preview-audit.md:23` 已明确QtText的ui_test消费者及条件，仍要求精确文件/目标/操作核权、不凭同源传播grant。24条说明引用全部更新为 `{facts['noteDigest']}`，24/24摘要匹配；扫描全部当前functions矩阵，引用该说明的也恰为这24条，无其他陈旧引用。逐行对原登记比较，仅五条targets和24条说明摘要改变，其他字段一致，全部仍pending。
- `batch15-rework.json` 的findingId、五个functionId、addedTarget、condition、CMake路径/摘要与真实源码相符；grantsChanged/productChanged/fullStagePass均false。README如实保留首轮FAIL及待冻结复验，未自授全T01通过。
- 原FAIL归档 `REUSE-T01-batch15-r1-20261023` 逐文件对原独立TEMP：50文件，47字节一致、3仅CRLF/LF不同，无缺漏/额外文件/内容失配；原acceptance仍为冻结85e561…的FAIL并保留B15-P2-01及历史网络事实。
- 相对父提交产品/上游/vcpkg源码、ARCH和permissions均无diff，`grants=[]`不变；未将真实测试编译补登扩大成HOST例外或新授权。范围内git diff --check exit0。

## 实际验证与范围

validate-task、冻结Head的validate-reuse、validate-docs均exit0，passed=3、failed=0、skipped=0；复用门禁仅治理结构通过。返工核查脚本exit0。事实和逐项证据为facts.json、target-checks.json、hash-checks.json、all-note-references.json、archive-checks.json、cmake-evidence.log与对应原始log/exit文件。

C++构建/产品测试、真实GUI/GL/后端、数值/性能、原B15其余语义重验、DeepEvidence、全T01退出及T02准入均 **NOT RUN**。本PASS只关闭此P2，不授全UI/T01通过或T02准入；原B15其他核对结果按原冻结范围引用。

未实施、未改主工作树/冻结仓库、未提交/推送/合入、未创建子代理。委派已明确本冻结提交成功推远程；本轮未重复远程核验，也不改写原FAIL中的历史网络状态。报告与日志只写本Temp根，结构化结论见acceptance.json。
'''
(ev/'acceptance.md').write_text(text,encoding='utf-8');print('PASS only B15-P2-01; 5 target corrections/24 note hashes/50 archived originals; gates=3 PASS; no authority/grant changes')
