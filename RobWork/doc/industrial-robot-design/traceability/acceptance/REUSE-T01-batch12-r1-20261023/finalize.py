import pathlib,json,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent; repo=ev/'repo'; sha='d38e4472ec403d9d9f1b7201a0f8e2b4b83f7a0c'; base='a3cb564810b3eae8515ba11bfed9c37756d808f4'
def load(n): return json.loads((ev/n).read_text(encoding='utf-8-sig'))
def git(*a): return subprocess.check_output(['git',*a],cwd=repo,encoding='utf-8')
assert git('rev-parse','HEAD').strip()==sha
status=git('status','--porcelain'); assert status==''; (ev/'git-status-end.log').write_text(status)
diff=git('diff',base,sha,'--','RobWork/RobWorkStudio/src','RobWork/RobWork/src','RobWork/RobWorkSim/src','vcpkg'); assert diff==''; (ev/'product-upstream-diff.log').write_text(diff)
assert len(load('new-rows.json'))==34 and all(x['decision']=='pending' and x['status']=='pending' for x in load('new-rows.json'))
assert len(load('bindings.json'))==172 and all(x['match'] for x in load('bindings.json'))
assert len(load('definition-checks.json'))==34 and all(x['completeBoundary'] for x in load('definition-checks.json'))
assert all(x['exitMatches'] and x['stdoutSetMatches'] and x['stderrMatches'] for x in load('search-replay.json'))
assert all(x['newlineMatches'] for x in load('archive-checks.json'))
gates={n:int((ev/f'{n}.exit.txt').read_text(encoding='utf-8-sig').strip()) for n in ['task','reuse','docs']}; assert all(v==0 for v in gates.values())
assert int((ev/'scoped-diff-check.exit.txt').read_text(encoding='utf-8-sig').strip())==0
finding=dict(id='B12-P2-01',priority=2,topic='监督器getter组混淆当前运行态与历史观测位',files=[dict(path='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/test-tools-lifecycle-audit.md',line=23),dict(path='RobWork/doc/industrial-robot-design/reuse/functions/workflow.json',line=523)],evidence='CloseSupervisor.hpp:161-162明确running为start后、stop/自然退出前；CloseSupervisor.cpp:76置true，92/129/165/176清false。后三个观测位只置true且start不清零。',impact='本批生命周期语义审计将可复位过程态登记成保留历史事实，不能以全部历史位的描述验收该getter组。',requestedCorrection='说明与workflow.CloseSupervisor.running.differences均改为：running只读当前运行态；storeClosedObserved/abandonDiagObserved/gaveUp只读保留历史观测位；四者acquire读取且不构成第二状态权威。同步该说明被引用的auditEvidence摘要。无需产品代码修改。')
observations=[
 ('定义与作用域','34条完整源码区段、构造initializer list和析构均核对；int64重载独立；ProcessRunner三个辅助函数确在全局anonymous namespace，quoteWindowsArg在testkit::detail；游标3定义和getter4定义均为明确组，不将其伪算单一定义。'),
 ('摘要与targets','34处源码区段及inputs/auditEvidence共172项规范化摘要全部相符；testkit三个TU注册于sdurws_ird_testkit，CloseSupervisor注册于sdurws_ird_workflow，公共include根由后者传播。'),
 ('候选API','UTIL-THREAD保持candidate；5项现有/新增上游引用和CMake摘要全匹配，新增ThreadTask/Timer/TimerUtil符号真实存在于sdurw_common。ThreadTask为线程父子任务而非OS进程/Job树；TimerUtil Windows clock、其他gettimeofday，不等于可注入steady_clock deadline。不得用sleep替代成功事件。'),
 ('JsonLite与Check','JsonLite游标、BOM/空/尾随/深度/字面量及强类型deepEquals边界成立；PropertyTree差异沿B8，未批准新解析器。Check委托core::closeWithin，字符串/整数精确比较；sourceTag丢弃、字符串详情缺失及int64失败payload转double风险被如实保留，未宣称比较使用double。'),
 ('进程工具','启动挂起→Job绑定→恢复、零句柄继承；四态缓存、非Exited exitCode=0协议，析构/Impl回收及EventWatch事件优先deadline语义成立。UTF8 flags、环境键大小写和start异常窗口仅静态待核，说明明确无复现，未冒称已复现缺陷。execution私有CLI/管道不能直接跨层引用，未批准新公共进程平台。'),
 ('关闭监督','借持store/scheduler/DevLog；正阈值、start幂等、stop join、闭合优先、一次Dev日志、阈值宽限和无scheduler tick均可定位。生产宿主接线仍pending。getter状态含义错误单列B12-P2-01。'),
 ('搜索重放','6条原参数全部独立执行exit0，排序后命中集合及stderr一致。原process查询fork片段双转义，补查正确转义仍仅2条Doxyfile popen注释；不以此宣告无上游能力，候选排除依据为实际线程/时钟API和业务协议。'),
 ('历史归档','B10-r2 22文件(19字节一致/3仅换行)、B11 47(44/3)、Planning API 28(25/3)，逐文件对原TEMP比较均无缺漏/内容失配。B10-r2仍只关闭B10-P2-01；B11仅33条审计PASS；Planning API冻结a3cb、仅4项API通过(4 passed/0 failed/skipped/notRun)、保留首次编译失败原日志和生产限制，无扩大范围。')]
report=dict(schemaVersion='ird-independent-acceptance/1',taskId='REUSE-T01',batch='B12',reviewedCommit=sha,comparisonBase=base,verdict='FAIL',scope='仅B12新增34关系、UTIL-THREAD扩展、说明、检索与本提交归档保真；不授全T01 PASS。',findings=[finding],observations=[dict(topic=t,result='FAIL' if t=='关闭监督' else 'PASS',detail=v) for t,v in observations],checks=dict(newRelations=34,pendingRelations=34,sourceDefinitions=34,normalizedBindings=172,failedBindings=0,catalogReferences=5,newCatalogReferences=3,replayedSearches=6,searchMismatches=0,archiveFiles=97,archiveRawMatches=88,archiveNewlineOnly=9,archiveContentMismatches=0),gates=dict(exitCodes=gates,passed=3,failed=0,skipped=0,scope='治理结构/文档/任务；不是算法合规'),scopedDiffCheckExit=0,checkoutClean=True,productUpstreamDiffEmpty=True,notRun=['C++构建/产品测试','GUI','数值/性能验收','API探测重新执行','DeepEvidence','全T01退出'],repositoryModified=False,committed=False,pushed=False,merged=False,subagentsCreated=False,wholeT01Pass=False,reportRoot=str(ev))
(ev/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
text=f'''# REUSE-T01 B12 独立语义验收

结论：**FAIL，仅B12审计记录**。冻结 `{sha}`，比较 `{base}`。独立Temp clone，未改父树，未实施/提交/推送/合入，未派代理。

## 需返工：B12-P2-01

`test-tools-lifecycle-audit.md:23` 与 `reuse/functions/workflow.json:523` 将四个getter一并描述为只读历史位。这与冻结源码不符：`running()` 是本次监督的当前运行态，start置true，stop/自然退出清false；只有另外三个观测位保留历史事实，start不清零。证据为 `CloseSupervisor.hpp:161-178` 与 `CloseSupervisor.cpp:73-76,92,128-129,143,157,165,176`。

请在说明和 `workflow.CloseSupervisor.running` 的differences中明确区分“running读取当前运行态”和“另外三个getter读取保留历史观测位”，保留acquire读取与非第二状态权威的边界，并同步引用说明的auditEvidence摘要。只需修正文档/登记，不要求修改产品实现。这是生命周期语义事实错误，非排版问题；其余已核项目不因此失效。

## 已核事实

'''+ '\n\n'.join('- **'+t+'：** '+v for t,v in observations)+f'''

## 实际验证与限制

validate-task、冻结Head的validate-reuse、validate-docs均exit0；门禁计数passed=3、failed=0、skipped=0。复用门禁输出functions=362、closedFiles=0，仅治理结构通过。范围内git diff --check exit0。34关系仍pending，UTIL-THREAD仍candidate。

定义区段34项、规范化绑定172项、catalog引用5项、原参数搜索重放6条、归档文件97项均按上述范围核对。辅助脚本、原输出和逐项结果见 definition-checks.json/source-excerpts.log、bindings.json、catalog-bindings.json、search-replay.json/search日志、archive-checks.json。逐文件摘要按仓库规范归一化文本，不以Windows CRLF误判失配。

C++构建/产品测试、GUI、数值/性能、API探测重跑、DeepEvidence和全T01退出均 **NOT RUN**。本轮为只读文档审计，归档API结果是保真核对，不冒称本轮重新运行。无新产品权限、生产迁移或T02/T05准入。

最终HEAD正确且checkout干净，相对base产品/上游/vcpkg源码diff为空。完整结构化结论见acceptance.json。本FAIL不推翻B10-r2/B11/API各自冻结范围的历史结论，也不授予全T01 PASS。
'''
(ev/'acceptance.md').write_text(text,encoding='utf-8'); print('REPORT FAIL B12-P2-01; gates=3 PASS; 34 definitions/172 bindings/97 archive files checked; clean frozen checkout')
