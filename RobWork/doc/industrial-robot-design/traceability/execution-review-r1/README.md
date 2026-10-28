# execution 第一轮评审整改（EX-T11）

现场：`C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`，分支 `codex/core-review-fixes`，起点 `6a49e128c00f8cca3943083819ea59aea16a7be9`，起始干净。只改 execution、UI 两处生命周期注释及必要设计/凭证/证据；框架、vcpkg、main、治理规则及既有任务状态不变。没有远程操作、子智能体、新分支或工作树。

所有者要求“按照审核建议进行修改”，并批准 C1 和 EXR-10 局部签名修订；原话与具体边界见 [PROPOSALS.md](PROPOSALS.md)。[原评审](ORIGINAL-REVIEW.md)按当时源码保留，其“本轮均未实施”等历史描述不作为本次实施状态。以下为实施自查，**未完成独立验收**，不推进 diagnostics。

## 实施结果和依赖

| 原发现 | 本批改动及直接影响 | 反例和回归 |
| --- | --- | --- |
| EXR-01 | Admission 使用登记 runtime 映射的返回值补齐诊断 subject，自报对象冲突先拒绝，再调用 evidence 判定/工厂；不取当前项目映射 | w1-red/green，最终 unit |
| EXR-03 | CheckpointRecord 要求 checkpointId.run 与 task.run 同源，写入/索引前拒绝 | 同上 |
| EXR-04 | worker seq 分配和完整帧写入共锁；分片逐帧分实际 seq，片间可发心跳，不回写旧 cursor；普通路径不再无锁读 seq | writer-red/green 固定交错，真 worker 契约 |
| EXR-05 | 重组逐片核首片类型/身份；命令按启动五元组和严格 seq 核对，阶段 A 不换绑；Supervisor 核握手、DispatchAccept、请求事实和控制确认后结果 | ipc-red/green 手工非法片，writer 异尝试控制，phase-before-accept-red-r3，最终 contract |
| EXR-07 | reader 提前停止后仍从自有进程句柄收割退出；保留 Dead 墓碑/HandleAdapter，释放 Job、进程、管道、线程及物化/分片/乱序字节 | process-red/green；最终 contract 预热后连续四个异常子进程，句柄数不增长 |
| EXR-08 | CancelIoEx 后用 GetOverlappedResult(TRUE) 等待读完成，再释放 OVERLAPPED、事件和缓冲；析构/退出排空共用 | lifecycle-probe red-r3/green-r3；证明完成次序，不声称已复现系统崩溃 |
| EXR-09 / C1 | 精确订阅位先失效，再等待该位已开始回调；尚未开始者跳过，返回后可销毁 sink；旧句柄不移除同 sink 新位 | bus-red/green，最终 contract；UI 原 drain 保留，只同步注释 |
| EXR-10 | 聚合返回 optional；任一 Job 失采或求和溢出返回 nullopt，空池/合法零为有值0；采样器传播无数据，原 ResourceController 保级 | lifecycle-probe red-r3/green-r3：真实 Job/生产采样链，含失采保级与恢复解除 |
| EXR-11 | STARTUPINFOEX 仅继承本任务两管道句柄；保留挂起创建→Job assign→恢复次序 | 自有可继承事件＋坏帧 worker，process-red/green，最终 contract |
| EXR-13 | 模型同键幂等比较完整字节；完整键优先、同 WC 候选按稳定序交原 judge，FullReuse 优先；judge/缺失诊断在锁外 | model-red/green 三项，最终 unit / CacheJudge 契约 |
| EXR-14 | publish 冻结接收位，新订阅不补收历史排队帧 | bus-red/green，最终 contract |

EXR-02（完整结果持久化/批次身份）、EXR-06（暂停确认/恢复派发）、EXR-12（正式归档字节进入结果缓存）**尚未修复**，依赖待裁决 C5/C2，不私造 evidence codec、公共 batchIndex、检查点确认或新 owner。S1—S6 疑点、C3 混合内存计量、C4 查询线程口径继续保留；65%/70%、取消预算和支持范围均不扩大。

## 数据流、职责和复用

保留冻结身份/输入→准入与模型准备→RunRegistry→内联或阶段 A worker→值/字节回传→登记接纳→evidence 判定/工厂→原项目归档端口→缓存协调/事件的组件数据流。L5 装配没有扩大：正式业务 evaluator 尚未接入该脚本 worker；UI 现有 Preview 异步路径也不是正式 envelope/归档闭环。脚本替身真进程只证明通道/治理，不证明正式算法已经经 worker 执行。Completed、工程判定、当前性、archivePhase 仍为不同轴，未因归档失败改任务 Failed。

按 H1—H6 的保留/删除/适配与归属：

| 范围 | 保留和适配 | 删除的错误路径 / 权威边界 |
| --- | --- | --- |
| Admission / Checkpoint | core 强类型身份、runtime 登记映射、evidence aggregateVerdict/validateCombination/ResultEnvelope::make | 丢弃反解值、跨 run 索引；execution 仅编排，正式判定归 evidence、写入归 project |
| Protocol / worker | 原固定小端协议、FrameSequencer、MessageReassembler | 混合首末片、跨尝试换绑、不同同步域 seq 读写；不传模型地址/会话借持句柄 |
| Supervisor / Win32 | 现 JobScope、ChannelPair、ProcessLauncher 薄适配和标准同步 | 全宿主继承、未收敛即释放、退出依赖已停止 reader；仅操作自有资源，Job 不是完整安全沙箱 |
| EventBus / UI | REUSE-T41 已装配的唯一 execution 总线、core 事件值、专用线程/FIFO/锁外回调，UI 原 drain | 裸 sink 快照、投递时才选接收者；不恢复旧生产装配或第三套总线 |
| Cache / Scheduler | 原索引、ICompileCacheJudge、ResourceController | 同长度冒充同内容、完整键预过滤部分复用、失采当零；兼容规则归注入 judge |

定向核实上游（框架与起始 SHA 相同）：`rw/common/ThreadPool.hpp`、`ThreadTask.hpp`、`ThreadSafeQueue.hpp` 无五元组、原项目归档、Job 所有权和节流责任；`RobWorkSim/src/rwsim/simulator/ThreadSimulator.*` 面向模拟线程，不能代替业务调度。`BINArchive.hpp::writeValue` 用原生 sizeof 表示，`BINArchive.cpp::doWrite(string)` 用流插入长度，不满足 IRDCHN 固定字节合同，保留原私有协议编码，不新增通用 codec。core `Events.cpp::ReferenceEventBus` 是同步参考实现，不提供专用线程/C1订阅位等待，不替换正式总线。

矩阵仅更新本批产品文件及实际引用输入：原候选按上述差异登记 business-specific/migrated，表示实现方案落位，**不是 closed、独验通过或能力包结项**。未触及文件 pending 不关闭。新增/相邻函数及测试按真实符号登记，注释/声明并入相邻区段，不造“注释功能”。core/testkit/ui 只重钉受影响输入摘要，UI grant 仅 source.sha256 更新，ID/权限/操作/范围不变。sync-bindings.py 重算本批凭证；Check-LocalBindings.py 只核本批摘要/符号/覆盖，不代替正式全量门禁。

四维自查：准确性见身份负例、固定交错、真进程；架构保留 owner/端口；简洁性复用读取消收敛、Job reset、订阅位和候选判定路径，未加平台；运行效果见下列实测。完整能力和语义仍须独立审查。

## 实际验证与原件

所有源码/测试产物来自本 C: 树。集成 CMAKE_HOME_DIRECTORY 指本树 RobWork，RWS_BUILD_INDUSTRIALROBOT=ON；新 build/execution-smoke 的 home 为本树 industrialrobot，RobWork_DIR 为本树 RobWork/RobWork/cmake，未复用指向 D: 源码的旧 smoke。D: 只供现有 VS/CMake/Qt/vcpkg。绝对路径、一次一个 exe；execution 零 Qt，所选 UI 契约用 QCoreApplication，不需 GUI 插件。

[logs/](logs/) 保留各 tag stdout/stderr、gtest XML、per-exe ird-test-report.json。父 PID 见 test-processes.log，本批异常 worker PID 见 delivery-contract.stderr.log；没有用 _report 再跑同组。所有数字按 passed/skipped/failed 分列。

| 验证 / tag | 结果 | passed | skipped | failed |
| --- | --- | ---: | ---: | ---: |
| 集成 delivery-unit | PASS | 169 | 0 | 0 |
| 集成 delivery-contract | PASS | 44 | 0 | 0 |
| 独立 smoke-delivery-unit | PASS | 169 | 0 | 0 |
| 独立 smoke-delivery-contract | PASS | 44 | 0 | 0 |
| UI ui-consumer：实际 UiLinkage / SessionContract | PASS | 12 | 0 | 0 |
| UI ui-port-consumer：PeerBridge / InteractionTaskContractTest | PASS | 9 | 0 | 0 |
| cancel-perf：仅正常协作取消基准 | PASS | 1 | 0 | 0 |
| lifecycle-probe-green-r3 隔离探针 | PASS | 2 | 0 | 0 |
| lifecycle-probe-red-r3 旧实现对照 | FAIL（预期反例） | 0 | 0 | 2 |

取消15有效样本、0无效，另剔除1次预热；请求→Canceling 最大25.3256ms（预算2s），Canceling→协作收敛最大16.1701ms（预算10s），原始样本见 b02-cancel-normalsettle.json。其余 perf、真实 GUI E2E、正式业务 worker E2E 均 **NOT RUN**，不将21项 UI 契约写成全UI/GUI通过。

正式反例红→绿：w1为0/0/3→3/0/0；writer为0/0/2→2/0/0；ipc为0/0/1→1/0/0；process为0/0/1→1/0/0；bus为0/0/3→3/0/0；model为0/0/3→3/0/0。受理前 final 的 phase-before-accept-red-r3 为0/0/1，明确观测 FinalOutput=1；修后并入最终44项。手工非法片和独立预期序号保留，不只同实现往返。

失败历史原样保留：中间过早删除 Dead 墓碑导致41项中39通过/2失败，现已恢复原 HandleAdapter 查询契约；一次测试误将 Status 当 bool 编译失败，后改独立枚举预期；探针首次缺 DLL PATH 未成功执行，修正后红/绿。受理前 final 最初两次探针漏分片头，被畸形帧防线拒绝，不能证明阶段门；phase-before-accept-red[-r2] 只留构造失误记录，有效证据为 r3 和最终契约，不计前两次为修复通过。

## 门禁和限制

- delivery-gates.log：R1/R2/R3/R4/R5/T1/T2/SUB/GRAPH/LIB/SA02 架构扫描子阶段零命中；**ird_gates 复合目标 FAIL**，停在全量复用既有 runtime/CMakeLists.txt 生产证据绑定失配。早先 final-build-gates.log 的 UI 注释源摘要过期已仅重钉 source.sha256，后续不再停在那里。
- validate-docs.ps1 -TaskFile .../EX-T11.json 的 docs-reuse-r2.log 同样 **FAIL** 于该 runtime 阻断，不能声称文档/复用整体 PASS；最终矩阵的完整机器检查尚未走完。没有放宽、跳过或修改门禁，不重复执行已确认同一无关失败。
- final-baseline-binding-proof.log 与 Check-BaselineBinding.py 只读比较起始 Git 对象：runtime 源码和历史报告均与起点完全一致；实际源摘要 23b072f9ced2d31c056d0af78272bcbfd8623bee32591e2edb8c6bd93da4a377，历史绑定 5ef2e38b8e00e67c8889e50f5a09f0146908d2f791d2b52f5d669089313577cd。例为 REUSE-T13/production/runtime.compileWorkCell.json，本批不改 runtime/历史生产报告。
- 一次 -Head 基线全量读取因逐 Git 对象耗时主动中断，无基线全量 PASS/FAIL 结论，不能替代上述精确差异证明。日志历史编码原样保留，没有改写为“干净通过”。本地提交、范围/提交格式检查不构成独验。

## 复现、回滚及后续

VS x64 环境构建：cmake --build build --config Release --target sdurws_ird_execution_test sdurws_ird_execution_contract_test；独立将 build 换为 build/execution-smoke。UI plugin/contract 和 execution_perf_test 增量构建见 consumer-perf-build.log。Run-Test.ps1 指定 C: 绝对 exe，过滤器原件见各日志首行；smoke exe 在 build/execution-smoke/execution/Release。脚本超时180s，只按本轮PID清理进程树，worker由自有Job清理。

将 [probes/](probes/) 源码/脚本/compile-inputs.rsp 复制到本树 build/fix-execution-20261028，运行 Build-Probes.py，在 VS x64 环境执行 cl @probes-red.rsp / cl @probes-green.rsp。沿已有 C: includes/libs 与 D: vcpkg；旧源码取起始 SHA，仅适配旧聚合函数返回类型为获批 optional，使“忽略失败”旧函数体可与新头编译，不改变反例逻辑。运行过滤器 *EXR10Probe:*EXR08Probe；DLL PATH 含本树三组 Release bin 和 D: vcpkg bin。外部180s及内部事件期限保证有界，临时目录/Job只属本轮。

回滚按依赖成组：身份接纳/检查点校验及测试；IPC writer/命令阶段/重组/监督器及真进程测试；Win32句柄/读取消/退出清理；C1总线/公共注释/单元设计/UI两处注释/契约；optional签名/采样器/文档/探针；模型候选/字节比较及judge测试。相应矩阵/逐文件凭证同步回滚。本批无新磁盘格式/协议字段、无旧归档重写，不为回滚恢复部分结果或扩大句柄继承。

前序 REUSE-T23/F-964 的拒绝转换原子性和 REUSE-T41/F-967 的生产总线选择继续保留，未重报历史已修问题；只修 execution 相邻生命周期缺口。diagnostics 后续仍应消费 evidence 正式判定、登记映射及稳定码，不能把脚本worker、Cancelled/部分输出、归档失败包装成完整正式成功。C5/C2和全量复用阻断解决后，仍须所有者另开独立验收。

## 独立验收（2026-10-28）

所有者另开独立会话对实施与回归面验收 **PASS**，登记于 [acceptance/EX-T11-20261028-r1.json](../acceptance/EX-T11-20261028-r1.json)（证据同目录），范围显式 `-Base 6a49e128c -Head 4284e8e05a42`（193 路径允许面）。独立会话真实重跑：集成 169＋44／独立冒烟 169＋44／UI 消费者 12＋9（ui_contract_test 目标同过滤）／取消协作基准 1（8.2s）全部 decisive；隔离探针独立重编（VsDevCmd＋Build-Probes.py＋cl @red/@green）＝红 0/2 预期反例＋绿 2/2。C1 订阅位与 EXR-01 准入 diff 审查成立，其余九项逐条对应。树级门禁失败与本批前逐字一致（runtime/CMakeLists.txt 冻结生产报告绑定，本批零 runtime 改动）；隐藏 COVERAGE 缺口复算＝恰为 POL-T13 批 16 文件 7,420 行，EX-T11 自身文件区段覆盖完整、零新增债。**任务保持 ready**：树级门禁转绿（两项既有结案债，见 POL-T13-20261028-r1.json admissionGate）后收口 done。
