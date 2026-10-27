# REUSE-T01 B12 独立语义验收

结论：**FAIL，仅B12审计记录**。冻结 `d38e4472ec403d9d9f1b7201a0f8e2b4b83f7a0c`，比较 `a3cb564810b3eae8515ba11bfed9c37756d808f4`。独立Temp clone，未改父树，未实施/提交/推送/合入，未派代理。

## 需返工：B12-P2-01

`test-tools-lifecycle-audit.md:23` 与 `reuse/functions/workflow.json:523` 将四个getter一并描述为只读历史位。这与冻结源码不符：`running()` 是本次监督的当前运行态，start置true，stop/自然退出清false；只有另外三个观测位保留历史事实，start不清零。证据为 `CloseSupervisor.hpp:161-178` 与 `CloseSupervisor.cpp:73-76,92,128-129,143,157,165,176`。

请在说明和 `workflow.CloseSupervisor.running` 的differences中明确区分“running读取当前运行态”和“另外三个getter读取保留历史观测位”，保留acquire读取与非第二状态权威的边界，并同步引用说明的auditEvidence摘要。只需修正文档/登记，不要求修改产品实现。这是生命周期语义事实错误，非排版问题；其余已核项目不因此失效。

## 已核事实

- **定义与作用域：** 34条完整源码区段、构造initializer list和析构均核对；int64重载独立；ProcessRunner三个辅助函数确在全局anonymous namespace，quoteWindowsArg在testkit::detail；游标3定义和getter4定义均为明确组，不将其伪算单一定义。

- **摘要与targets：** 34处源码区段及inputs/auditEvidence共172项规范化摘要全部相符；testkit三个TU注册于sdurws_ird_testkit，CloseSupervisor注册于sdurws_ird_workflow，公共include根由后者传播。

- **候选API：** UTIL-THREAD保持candidate；5项现有/新增上游引用和CMake摘要全匹配，新增ThreadTask/Timer/TimerUtil符号真实存在于sdurw_common。ThreadTask为线程父子任务而非OS进程/Job树；TimerUtil Windows clock、其他gettimeofday，不等于可注入steady_clock deadline。不得用sleep替代成功事件。

- **JsonLite与Check：** JsonLite游标、BOM/空/尾随/深度/字面量及强类型deepEquals边界成立；PropertyTree差异沿B8，未批准新解析器。Check委托core::closeWithin，字符串/整数精确比较；sourceTag丢弃、字符串详情缺失及int64失败payload转double风险被如实保留，未宣称比较使用double。

- **进程工具：** 启动挂起→Job绑定→恢复、零句柄继承；四态缓存、非Exited exitCode=0协议，析构/Impl回收及EventWatch事件优先deadline语义成立。UTF8 flags、环境键大小写和start异常窗口仅静态待核，说明明确无复现，未冒称已复现缺陷。execution私有CLI/管道不能直接跨层引用，未批准新公共进程平台。

- **关闭监督：** 借持store/scheduler/DevLog；正阈值、start幂等、stop join、闭合优先、一次Dev日志、阈值宽限和无scheduler tick均可定位。生产宿主接线仍pending。getter状态含义错误单列B12-P2-01。

- **搜索重放：** 6条原参数全部独立执行exit0，排序后命中集合及stderr一致。原process查询fork片段双转义，补查正确转义仍仅2条Doxyfile popen注释；不以此宣告无上游能力，候选排除依据为实际线程/时钟API和业务协议。

- **历史归档：** B10-r2 22文件(19字节一致/3仅换行)、B11 47(44/3)、Planning API 28(25/3)，逐文件对原TEMP比较均无缺漏/内容失配。B10-r2仍只关闭B10-P2-01；B11仅33条审计PASS；Planning API冻结a3cb、仅4项API通过(4 passed/0 failed/skipped/notRun)、保留首次编译失败原日志和生产限制，无扩大范围。

## 实际验证与限制

validate-task、冻结Head的validate-reuse、validate-docs均exit0；门禁计数passed=3、failed=0、skipped=0。复用门禁输出functions=362、closedFiles=0，仅治理结构通过。范围内git diff --check exit0。34关系仍pending，UTIL-THREAD仍candidate。

定义区段34项、规范化绑定172项、catalog引用5项、原参数搜索重放6条、归档文件97项均按上述范围核对。辅助脚本、原输出和逐项结果见 definition-checks.json/source-excerpts.log、bindings.json、catalog-bindings.json、search-replay.json/search日志、archive-checks.json。逐文件摘要按仓库规范归一化文本，不以Windows CRLF误判失配。

C++构建/产品测试、GUI、数值/性能、API探测重跑、DeepEvidence和全T01退出均 **NOT RUN**。本轮为只读文档审计，归档API结果是保真核对，不冒称本轮重新运行。无新产品权限、生产迁移或T02/T05准入。

最终HEAD正确且checkout干净，相对base产品/上游/vcpkg源码diff为空。完整结构化结论见acceptance.json。本FAIL不推翻B10-r2/B11/API各自冻结范围的历史结论，也不授予全T01 PASS。
