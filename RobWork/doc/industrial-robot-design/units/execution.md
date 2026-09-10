# 工业机械臂设计软件 · execution 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.2（全链一致性审计消账；v0.1＝首版草案） |
| 日期 | 2026-09-10 |
| 状态 | **`Draft`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收） |
| 文档代号 | UNIT-EXECUTION |
| 单元 | execution（平台服务，L3；ARCHITECTURE §2.3/§3.1：任务状态机、调度、工作进程池、检查点、缓存治理、资源节流） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md` v0.1、`units/evidence.md` v0.1、`units/project.md` v0.1、`units/runtime.md` v0.1、`units/policy.md` v0.1、`units/testkit.md` v0.1（均 `Draft` 未冻结）；`DETAILED-DESIGN.md`（已建立为 20 单元详设总目录）；`development-task-breakdown.md` **v0.2**（任务分配与构建约定；其 §2.9 WP-08-T01 即本文，§5.1 构建约定本文全部承接）。协作卡如有变更，本文按影响面增量同步（P-EX-1） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md` → `units/*.md`（单元任务卡）。本文即 `units/execution.md`，按任务卡深度编写（接口签名、数据类型在本文件冻结） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/`（骨架已建：目标 `sdurws_ird_execution`〔INTERFACE 占位〕＋别名 `RWS::ird::execution`＋公共头保留位 `include/sdurws/ird/execution/README.md`；见上级 `industrialrobot/CMakeLists.txt`） |
| 任务包 | WP-08（任务执行平台；TASK-01～03 主 WP、CON-04 支持方、NFR-PERF-02/04 主 WP、NFR-REL-02/03 主 WP、PM-13 主 WP；REQUIREMENTS §3 阶段 A；主 WP 归属 WP-D"任务执行与诊断闭环"，见 DTB §0） |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 仅功能范围对照且**当前磁盘不存在**（core.md §1.2 R-5、DTB §4.1 O-01 同源登记）；不复制、不恢复任何历史实现 |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 execution 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 execution 的职责（§3.1 单元总表行；§4 运行时视图——进程模型 SA-08、线程模型、任务生命周期与状态机 §4.3、取消与终止协议 §4.4、迟到结果接纳与 RunRegistry §4.5、资源治理 §4.6；§6.5/§6.8 存储协作；§7.6 管线 B 执行横切），把任务/运行/尝试身份、任务状态机与转换守卫、调度与资源治理、工作进程模型与通道协议、取消/暂停/继续/强制终止、检查点与缓存治理、运行登记表与迟到结果防护、project 归档协作、公共接口、验证方案与故障注入矩阵、阶段 A 任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不收窄、不扩大；本文引用的需求 ID 与章节号均指 v1.16 当前正文（重点：TASK-01～03、CON-01～06、NFR-PERF-01～04、NFR-REL-01～03、PM-03/07/08/13、OPT-06、§8.1 表 1～4、AT-10/11/13/34/35）。架构归属以 `ARCHITECTURE.md` v0.11 为准（SA-08 直接约束本单元；A1/A5/A7/A8 处置已并入其 §4.3～§4.5 与 §6.8）；本文对其含混或未登记事项的解释集中登记于 §15.3（P-EX-1～P-EX-10），不私自修改上游。

**贯穿全文的三条所有权声明**：

1. execution 拥有**执行轴**（任务是否算完、怎么算、在哪算、算到哪），不拥有**判定轴**（结果工程上是否可行——evidence 汇总）与**当前性轴**（结果是否仍适用——evidence 按切片内容身份计算）。"任务完成"永远不等于"工程通过"，也不等于"归档完成"。
2. execution 是**调度与治理层**：不实现任何业务算法（FK/IK/轨迹/动力学/选型/优化），业务计算一律经 evidence ③端口的注册评估器（注入）接入；缓存与检查点的**命中/兼容判定**是 evidence（`judgeCacheHit`/`judgeCheckpointCompatibility`）与 runtime（`judgeCompileCacheCompatibility`，经注入）的纯函数，execution 只负责查找、存储、淘汰与恢复调度的编排。
3. 一切磁盘写入经 project 存储端口（`IResultArchivePort` 等）：execution 不直接写 `.rwdesign` 正式目录，不持有项目写锁，但必须尊重并消费 project 的权限结果（§9）。

### 1.2 上游与磁盘现状登记（2026-09-10 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文承接：TASK-01～03、CON-01～06、ERR-01（消费侧）、EVI-01（模式效力消费侧）、NFR-PERF-01～04、NFR-REL-01～03、PM-03/07/08/13/18（协作侧）、OPT-06（平台能力承载侧）、§8.1 表 1/表 3、AT-10/11/13/34/35 |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。§4 全节（状态机九态、2 s/10 s 取消协议、五元组 RunRegistry、70% 内存治理）与 §3.5 依赖表（execution→core,evidence,diagnostics,project 四条边）直接约束本文；若评审产生 A9+ 处置按影响面同步（P-EX-2） |
| `units/core.md` | 存在，v0.1，`Draft`（未冻结） | 消费其 TaskIdentity 五元组、RunId/AttemptId 类型、TaskState 九态词表、TaskOutcome/EngineeringStatus/EvaluationMode、DomainEvent 家族与 IDomainEventBus 接口、DiagnosticRecord；其 §10.3 向 execution 交接"RunRegistry 规则、状态机转移矩阵、TaskState→TaskOutcome 终态映射、总线实现（投递线程承诺）"——本文 §5/§9/§10 即该交接的承接答复 |
| `units/evidence.md` | 存在，v0.1，`Draft`（未冻结） | 消费其 AnalysisSnapshot/InputSlice（sliceId/inputBaselineId）、EvaluatorRegistry（manifest 摘要比对）、IEvaluationContext（execution 实现）、EvaluationOutput、ResultEnvelope::make/validateCombination（接纳复用）、aggregateVerdict、judgeCacheHit/judgeCheckpointCompatibility；其 §13"execution 行"交接的四项义务本文全部承接（§6/§8/§9/§10） |
| `units/project.md` | 存在，v0.1，`Draft`（**磁盘完整**，§1～§15，2026-09-10 实测；evidence.md P-EV-6 的"不完整"登记已过时，DTB §4.4 已定稿消账） | 消费其 IResultArchivePort（begin/writeBatch/finalize/abandon）、ProjectStore（writable/requestClose/closed/subscribeClose、存储上下文引用协议 §9.7/§10.1）、StoreError 码表；其 §13.2 要求 execution 冻结"RunRegistry 登记项（runDir/runKind）、调度线程调用约定（P-PR-4）、完成事件重复投递上限、强杀后的 abandon 调用时机"——本文 §9.1/§9.5 逐项冻结 |
| `units/runtime.md` | 存在，v0.1，`Draft`（未冻结） | 其 §13.2"execution 行"交接：CompileOutcome、IRuntimeSnapshotFactory（create/materialize）、取消令牌、缓存键判定 §9.4、快照引用持有纪律 §9.3、MaterializedSnapshotCodec 字节——本文经**注入接口**消费（§3.3，理由与 evidence D-07 同源：ARCH §3.5 未登记 execution→runtime 边） |
| `units/policy.md` | 存在，v0.1，`Draft`（未冻结） | 策略经快照 PolicyRef 内容身份**值传递**进入运行绑定（CON-06）；worker 内评估器经④端口消费 policy——execution 零 policy 编译依赖；取消经 IPolicyCallContext 同构（由注入适配，§3.3） |
| `units/testkit.md` | 存在，v0.1，`Draft`（未冻结） | 消费其 FaultInterceptor（接缝：execution 进程启动/通道发送）、TempDir、DeterministicEnv、ManualClock、IRD_TEST_INFO；TestProcessRunner 按 TK-T11 触发式交付（WP-08-T10 按需） |
| `DETAILED-DESIGN.md` | 存在（20 单元总目录；execution 行登记为"待产出"） | 本文产出后该行状态应由"待产出"更新为"Draft / 已有任务卡"（登记于 §15.3 P-EX-10，随文档同步任务处置） |
| `development-task-breakdown.md` | 存在，v0.2，`Draft` | WP-08-T01～T10 已登记（T01＝本文）；本文 §12 局部编号 EX-Txx 经"≙"映射对接（DTB §2 通用约定）；构建约定（C++17、gtest 经 vcpkg `find_package(GTest CONFIG REQUIRED)`、`ird_gates` 门禁、双模式构建）全部承接 |
| `units/execution.md` | **不存在**（本文新建） | 其余 13 个单元卡（diagnostics/ui/io/reporting/modeling/requirements/kinematics/trajectory/dynamics/drivetrain/selection/optimization/workflow）未产出——本文需要其接口处给最小依赖契约并登记交接（§13），不代写对方详设 |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 单元目录＋`patches/`，共 23 文件 | `sdurws_ird_execution` 为 INTERFACE 占位，无产品源码；`execution/include/sdurws/ird/execution/README.md` 引用本文"§9"（任务卡实际在 §12，随 EX-T01 修正——同 evidence.md 变更记录先例） |
| `old/` | **不存在于磁盘**（git 亦未跟踪） | 从头构建口径不受影响；本文不引用其任何机制 |

### 1.3 设计目标

1. **统一任务契约**：后续业务单元（阶段 B/C/D）依据本文提交长时计算任务——获得稳定任务身份与状态、后台执行与进度、取消/暂停/继续/失败恢复、工作进程崩溃隔离、检查点写出与恢复、兼容缓存复用——无需再协商契约（任务约束§二交付目标）。
2. **迟到结果零污染**（TASK-03/PM-13/ARCH §4.5 A8）：一切结果接纳先过 RunRegistry 完整五元组核对——未知 RunId、任一字段失配、陈旧 AttemptId 一律拒绝；合法历史结果归档到**登记记录记载的**原项目/原修订，不因当前 HEAD 变化而拒绝；当前性判断交 evidence；绝不写入新项目（AT-10）。
3. **失败诚实**（TASK-02/NFR-REL-03）：取消、失败、中断、工作进程崩溃永远不包装为完整成功结果；Paused 不是 Completed；检查点可恢复不等于任务已成功；缓存命中不等于当前结果；正常用户取消不产生错误诊断（UX-03）。
4. **不阻塞界面**（NFR-PERF-01/03）：超过 1 s 的工作一律转后台；结果分批流式回传；调度与接纳全部在 execution 自有线程，UI 线程只消费事件与进度投影。
5. **崩溃隔离**（NFR-REL-02/SA-08）：批量/长时计算在池化工作进程执行，worker 崩溃只导致当前任务失败，主界面不退出、项目不损坏；资源接近上限先节流再诊断（NFR-PERF-04）。
6. **不越权**：不拥有项目持久化、证据判定、模型编译、碰撞与业务算法；不自行增加任务状态、证据等级或判定规则；不承诺分布式"恰好一次"语义（ARCH §4.5 幂等仅覆盖进程内单存储上下文）。

### 1.4 语言、标准库与构建约束（按仓库实测确认，与 core.md/project.md §1.4 同源）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | 构建缓存 `build/CMakeCache.txt`：Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计；Windows 专属代码（进程/作业对象/管道/临时目录）隔离在 `src/win32/` 实现文件内，公共头零平台 API 泄漏（同 project.md §1.4） |
| Qt | Qt 6.11.1（msvc2022_64） | **execution 目标零 Qt（含 Core）**（决策 D-01）：L3 允许但不必需；线程/进程/同步用 std＋Win32；UI 线程投递由 ui 侧总线扩展承担（§10.6），execution 不引事件循环依赖 |
| C++ 标准 | 基线 RobWork `CMAKE_CXX_STANDARD 11`；industrialrobot 显式 C++17（core.md D-01） | 同口径：显式 `cxx_std_17`；`std::optional/variant/string_view/filesystem` 允许；不用 C++20；不引入 Boost 等第三方运行库 |
| Windows API | kernel32（CreateProcessW/CreateNamedPipeW/CreateJobObject/TerminateJobObject/WaitForMultipleObjects 等） | MSVC 默认链接；行为保证逐项对照 Microsoft Learn 文档口径登记（§6.6），**不凭记忆宣称断电安全**（同 project.md §7.2 纪律） |
| 异常 | RobWork 惯例异常 | execution 用自有 `ExecutionError`（§3.4）；通道与进程边界一律错误码化（异常不跨进程，core §6.2/evidence D-15 同源） |
| gtest | DTB §5.5 定稿：vcpkg 安装＋`find_package(GTest CONFIG REQUIRED)` 唯一机制 | 承接；`_test`/`_contract_test` 目标按此接入 |

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有总表（本节先行给出，全文以此为准）

**execution 拥有（实现责任方）**：

| # | 能力 | 上游依据 | 设计落点 |
| --- | --- | --- | --- |
| O-1 | TaskRecord/RunRecord/AttemptRecord 三级记录与任务身份分配（TaskId） | TASK-03、ARCH §4.5 | §4 |
| O-2 | 任务状态机（九态）、转换守卫、并发守卫与 TaskState→TaskOutcome 终态映射 | TASK-01、ARCH §4.3、PM-03 | §5 |
| O-3 | 任务队列、调度器、优先级与资源预算（CPU/线程/内存/worker 数） | ARCH §4.2/§4.6、NFR-PERF-04 | §6.1～§6.3 |
| O-4 | 工作进程池（WorkerLauncher/WorkerSupervisor）、进程生命周期、通道协议与心跳 | ARCH §4.1（SA-08）、NFR-REL-02 | §6.4～§6.6 |
| O-5 | 取消、暂停、继续、超时与强制终止（协作窗 2 s/10 s） | NFR-PERF-02、ARCH §4.3/§4.4、TASK-01 | §7 |
| O-6 | 检查点写出、兼容性检查调度与恢复调度（生成/验证/恢复编排） | TASK-01、CON-04、ARCH §4.3/§6.5 | §8.1 |
| O-7 | 运行登记表 RunRegistry 与结果接纳前完整五元组核对、迟到结果拒绝与合法历史结果归档协调 | TASK-03、ARCH §4.5（A8）、PM-13 | §9.1～§9.4 |
| O-8 | 任务进度、诊断与事件发布（TaskStatusChanged/ResultArchived） | TASK-03、ARCH §7.2⑤ | §6.2、§10.4 |
| O-9 | 缓存与检查点的**存储治理**（查找/写入/淘汰/预算）与命中请求的调度边界 | CON-04、OPT-06、ARCH §3.1 | §8.2 |
| O-10 | 进程内事件总线参考实现（core IDomainEventBus 接口的 execution 侧实现，ui 可扩展） | core.md §4.9/§10.3、ARCH §3.5 | §10.4 |

**execution 消费（经依赖白名单与注入）**：

| 消费方 → 被消费 | 形态 | 消费内容 | 上游依据 | 状态 |
| --- | --- | --- | --- | --- |
| execution → core | 接口依赖（编译链接） | TaskIdentity/RunId/AttemptId、TaskState/TaskOutcome/EvaluationMode、DomainEvent 家族＋IDomainEventBus 接口、DiagnosticRecord/ComparativeFields、CoreError | ARCH §3.5 表 | core.md v0.1 Draft 未冻结（P-EX-1） |
| execution → evidence | 接口依赖（编译链接） | AnalysisSnapshot/InputSlice（只读消费）、EvaluatorRegistry（find/manifest 摘要）、EvaluationRequest/Output、IEvaluationContext（execution 实现）、aggregateVerdict＋ResultEnvelope::make/validateCombination（接纳复用）、judgeCacheHit/judgeCheckpointCompatibility（缓存与检查点判定） | ARCH §3.5 表、§7.6 | evidence.md v0.1 Draft（P-EX-1） |
| execution → diagnostics | 接口依赖（边已登记；实现经注入适配） | 稳定诊断码注册（EX-\* 已收编 18 项——diagnostics.md §4.6）、两级日志 | ARCH §3.5 表、§7.8 | diagnostics.md 已产出；消费仍经本文最小 sink 注入（§3.3），目标链接形态按 P-EX-8 裁决（不因卡存在自动消账） |
| execution → project | 接口依赖（编译链接） | IResultArchivePort（begin/writeBatch/finalize/abandon）、ProjectStore（writable/requestClose/closed/subscribeClose、存储上下文引用协议）、IProjectQueryPort（runDir 读取，协作侧） | ARCH §3.5 表、§6.5/§6.8/§10.1 | project.md v0.1 Draft（P-EX-1；P-PR-4 在本文 §9.5 冻结） |
| execution →（注入）runtime 能力 | **运行时注入**（零编译依赖） | 快照编译与物化（create/materialize）、编译缓存键与判定、取消令牌对接、MaterializedSnapshotCodec 字节——经本文最小接口 `IExecutionModelService`/`ICompileCacheJudge`（§3.3），适配器归 L5 | runtime.md §10.9/§13.2；ARCH §3.5 未登记 execution→runtime 边（P-EX-3） | runtime.md v0.1 Draft |
| execution →（值传递）policy | 值传递（零依赖） | 已解析 EngineeringPolicySet 内容身份（快照 PolicyRef，CON-06）——进入运行绑定与登记扩展字段；worker 内评估器自行经④端口消费策略 | CON-06、ARCH §7.5 | policy.md v0.1 Draft |
| execution →（注入）业务计算 | 运行时注册（L5 装配） | IEngineeringEvaluator 各域实现（阶段 B/C/D 接入；阶段 A 用测试替身） | ARC-02、ARCH §7.2③ | 未产出（本文提供调度边界与替身） |
| execution → Windows 系统设施 | 系统调用 | 进程/作业对象/管道/句柄/临时文件/内存查询（§6.6） | NFR-DEP-01 | — |

**execution 不拥有（其他单元所有，本文不实现、不预建桩）**：

| # | 不拥有内容 | 所有者 | 上游依据 |
| --- | --- | --- | --- |
| N-1 | 项目 HEAD、修订、分支、草稿、事务、`.rwdesign` 全部磁盘布局与写入 | project | §17 表注、ARCH §6 |
| N-2 | WorkCell/DynamicWorkCell 编译实现、CanonicalModel、RuntimeNameMap | runtime | ARC-03/04、ARCH §7.3/§7.4 |
| N-3 | FK、IK、轨迹、动力学、选型、优化算法与领域对象 schema | kinematics/trajectory/dynamics/selection/optimization 等 | KIN/TRJ/DYN/SEL/OPT 家族 |
| N-4 | 证据等级、RequiredEvidenceProfile、工程判定汇总、结果当前性计算 | evidence | EVI-01/02、CON-02/05 |
| N-5 | 报告渲染与证据包 | reporting | RPT 家族 |
| N-6 | UI 面板、任务清单呈现、命令面板、确认对话 | ui/workflow | UX 家族、PM-03 |
| N-7 | 外部资源解析与包编解码 | io | NFR-SEC-01～03 |
| N-8 | 项目写锁本身——execution 不持锁、不以 PID/心跳/UI 状态为写权限依据，只消费 project 的权限结果（writable/StoreError） | project | ARCH §6.8（SA-17） |
| N-9 | 碰撞算法与工程策略判定（含行程上限阈值） | policy | ARC-05 |

### 2.2 直接承接的需求（execution 为实现责任方或数据契约责任方之一）

| 需求 | execution 承接的部分 | 不可越界的部分 |
| --- | --- | --- |
| TASK-01（任务状态机＋能力声明） | 九态状态机、逐转移守卫、能力声明（暂停/检查点粒度/强制终止代价）、强制终止矩阵 | 九态词表（core）；状态机验收归 TASK-01 验收要点 |
| TASK-02（执行结果只允许合法组合） | 结果接纳路径：EvaluationOutput→aggregateVerdict→ResultEnvelope::make（复用 evidence 校验器）；取消/失败/中断结果不得进入正式报告或可行集的**执行侧保证**（不构造、不归档为正式结果、不作缓存命中） | 合法组合表本身（§8.1 表 3）与构造校验器（evidence SA-13） |
| TASK-03（请求/完成事件携带五元组；旧任务不得成为其他会话当前结果） | 五元组分配与携带（通道消息/事件/登记）、RunRegistry 核对、归档到原修订、不进入新会话 | 五元组值类型（core TaskIdentity）；当前性上下文判定（evidence） |
| CON-01（分析从完整不可变快照运行） | 派发前快照完整性与修订闭包消费（经 evidence 校验器＋注入源）；物化投递 | 快照类型与组装协议（evidence） |
| CON-02（完整性/当前性/判定正交） | 执行轴承载与正交强制（§5.6 正交表）；Superseded 结果的归档不受影响 | 三轴判定本身（evidence） |
| CON-03（Verified 外部资源固化） | 派发前消费快照 externalResources 状态（经 evidence validateForMode 结果），不自行判定 | 固化状态判定与存储（evidence/project/io） |
| CON-04（缓存/检查点按契约判断；部分/失败结果不作正式命中） | 存储/查找/淘汰/写入执行；判定调用 evidence/runtime 纯函数；仅 finalize 产物入缓存 | 判定规则（evidence §8.2、runtime §9.4） |
| CON-05（切片内容身份驱动） | 派发绑定 sliceId/inputBaselineId；缓存键与失效原因消费 | 切片身份计算（evidence） |
| CON-06（策略与名称映射内容身份入快照；名称先反解后接纳） | 运行绑定扩展字段携带两个内容身份；接纳时名称反解编排（经⑥端口注入的解析器） | 策略解析（policy）、名称映射（runtime） |
| NFR-PERF-01（>1 s 转后台） | 后台任务通道与内联门槛（可预测 <1 s 调度线程内联，ARCH §4.1） | 交互响应时延本身（WP-23/ui） |
| NFR-PERF-02（取消 2 s/10 s 协议） | 取消状态机与协作窗实现 | — |
| NFR-PERF-03（分批/流式/大规模） | 通道分批结果流、进度、不整体装载（消费侧分页归 ui） | 数据规模基准（WP-23） |
| NFR-PERF-04（70% 内存、先节流后诊断） | ResourceController 汇总主进程＋worker 内存、节流（降并行/停派发）、资源不足诊断 | 规模化验收（WP-23-T06） |
| NFR-REL-02（worker 崩溃只失败当前任务） | 进程隔离、Job Object、崩溃检测与任务终结 | — |
| NFR-REL-03（中断任务显示"已中断"） | 中断重建（重启后由归档预留残留＋恢复诊断重建 Interrupted 记录）、不伪装完整结果 | 恢复扫描与呈现（project §7.4/PM-15→ui） |
| PM-03（关闭/切换任务二选） | 等待（排空）与协作取消两路径的执行侧实现；任务清单数据（九态投影） | 对话框 UI（ui/workflow） |
| PM-07（只读模式） | 需写 results/ 的正式评估在只读模式拒绝启动并诊断（消费 store.writable()） | 写权限判定（project） |
| PM-08（崩溃恢复） | "已中断"任务数据与重跑入口数据；abandon 调用时机（§9.5） | 恢复扫描实现（project） |
| PM-13（切换场景归档） | 迟到结果归档到原项目（§9.4/图 9-2） | — |
| OPT-06（Quick/Verified/缓存/种子/并行/检查点） | 平台机制承载（取消 R1、缓存与种子 B 期消费、暂停/继续与并行/检查点规模化 D 期——§13） | 优化域语义（optimization） |
| §8.1 表 1（评估模式效力） | 模式作为提交属性透传；Preview 不产生 envelope/不归档；Quick/Verified 缓存互不升降级（消费 evidence 判定） | 模式效力定义（需求） |

### 2.3 明确不做（非目标，与 §2.1 不拥有表互为补充）

不实现第二套任务状态或证据等级（九态与四值 outcome/EngineeringStatus 词表归 core，本文零新增——提交拒绝在状态机之外表达，见 §5.2 注）；不做任务持久化第二账本（任务态/登记表为会话内存态，磁盘痕迹唯一经 project 归档预留与检查点，§5.4）；不把缓存命中当当前结果、不把检查点恢复当任务成功、不把取消/失败/中断包装为成功；不自动重试失败任务（无上游需求，重跑是用户动作——D-09）；不承诺分布式恰好一次（幂等仅限进程内单存储上下文，project §10.1 同口径）；不修改业务算法、不在调度器内重复实现缓存判定；不让 worker 直接写项目正式目录或与 UI 交互（一切 worker 结果经主进程登记核对）；不绕过 RunRegistry 接纳任何结果；不复制或恢复 `old/` 历史实现；不预建无消费者的命令面板条目（会话态命令归 ui/workflow，ARCH §7.11）。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 组成（公共头模块）

execution 由 12 个公共头模块＋1 个 worker 入口＋编译实现组成（实现文件随 §12 任务落地，头为契约权威）：

| 头（`execution/include/sdurws/ird/execution/`） | 内容 | 详见 |
| --- | --- | --- |
| `Errors.hpp` | ExecutionErrorCode（稳定 token）、ExecutionError | §3.4 |
| `TaskTypes.hpp` | TaskId、TaskSubmission、TaskCapability、TaskPriority、TaskSnapshot、ProgressReport、TerminationCause、TaskRecord 投影 | §4.2、§5 |
| `Scheduler.hpp` | ITaskScheduler、SchedulerConfig、ResourceBudget、DrainPolicy、ISubmissionGuard（提交前验证注入点） | §6.1、§10.1 |
| `Controller.hpp` | ITaskController、CancelAck/StatusAck、ICancelSignal（取消信号最小接口，供 L5 适配 runtime/policy 令牌） | §7、§10.2 |
| `StateMachine.hpp` | TaskStateMachine（转移表＋守卫，公共可测）、TransitionRequest | §5.2、§5.3 |
| `RunRegistry.hpp` | RunRegistration（登记记录全字段）、IRunRegistry、AdmissionDecision、ArrivalEnvelope、IResultAdmission、ArchivePhase | §4.3、§9.1、§9.3、§10.5 |
| `WorkerSupervisor.hpp` | WorkerId、WorkerStatus、WorkerAssignment、IWorkerSupervisor、WorkerLaunchResult、HeartbeatPolicy | §6.4、§10.3 |
| `ChannelProtocol.hpp` | 帧布局、消息类型枚举、协议版本、五元组身份头（主/worker 共用；worker 目标与测试均消费） | §6.5 |
| `Checkpoint.hpp` | CheckpointRecord、CheckpointWriteRequest/Result、CheckpointRestoreRequest/Result、ResumeQuery、ICheckpointCoordinator | §8.1、§10.6 |
| `CacheCoordinator.hpp` | CacheLookupQuery/Result、CacheEntrySummary、EvictionPolicy、IExecutionCacheCoordinator、ICompileCacheJudge（编译缓存判定注入） | §8.2、§10.7 |
| `EventBus.hpp` | DomainEventBusImpl（core IDomainEventBus 的进程内参考实现：线程安全、同发布者 FIFO、RAII 退订） | §10.4 |
| `Ports.hpp` | IExecutionDiagnosticsSink（诊断注入）、IExecutionModelService（模型准备/物化/编译缓存判定注入）、INameResolverAdapter（⑥端口反解注入） | §3.3 |
| `README.md` | 既有保留位说明（不参与编译；任务卡指向随 EX-T01 修正为 §12） | — |
| `worker/main.cpp`（目标 `sdurws_ird_execution_worker`） | worker 进程入口：握手→物化→评估器循环→检查点/结果回传（装配清单归 L5，§6.4） | §6.4、§6.5 |
| `src/`（实现，随 EX-T01 建） | `Scheduler.cpp`、`StateMachine.cpp`、`RunRegistry.cpp`、`WorkerSupervisor.cpp`＋`win32/ProcessLauncher.*`、`win32/ChannelPair.*`（命名管道/匿名管道帧读写）、`win32/JobScope.*`、`win32/MemProbe.*`、`Checkpoint.cpp`、`CacheCoordinator.cpp`、`EventBus.cpp` | §5～§9 |

### 3.2 依赖（含契约消费状态登记）

```
sdurws_ird_execution ──► RWS::ird::core（PUBLIC）
                    ──► RWS::ird::evidence（PUBLIC）
                    ──► RWS::ird::project（PUBLIC；归档端口与存储上下文）
                    ──► RWS::ird::diagnostics（ARCH §3.5 已登记边；消费经 IExecutionDiagnosticsSink
                         注入适配——diagnostics.md 已产出并收编 EX-\* 18 项〔§4.6〕，sink 名称/归属统一仍按
                         P-EX-8 裁决，测试/装配侧暂以空实现占位——project §3.2 同模式）
                    ──► C++17 标准库 ＋ Win32 kernel32（隔离于 src/win32/）
                    ──✖ 零 Qt（D-01）、零 runtime/policy/业务单元编译边（R-1/R-2 门禁口径）、零 testkit（T-1）
运行时注入（零编译依赖）：IExecutionModelService/ICompileCacheJudge（适配 runtime）、
                    INameResolverAdapter（适配 runtime ⑥端口）、各域评估器（L5 装配注册）、
                    事件总线实例（本文提供参考实现，L5 决定装配）
```

消费的协作契约清单（全部来自各卡 v0.1 Draft；**状态：未冻结**，P-EX-1）：

| 契约 | 来源 | execution 用途 |
| --- | --- | --- |
| TaskIdentity/RunId/AttemptId/TaskState/TaskOutcome/EvaluationMode | core §4.1/§4.7 | 五元组、状态机词表、envelope 组装 |
| DomainEvent 家族＋IDomainEventBus/IEventSubscription | core §4.9 | TaskStatusChanged/ResultArchived 发布；总线参考实现 |
| DiagnosticRecord/ComparativeFields | core §4.8 | 全部执行诊断承载 |
| AnalysisSnapshot/InputSlice（sliceId/inputBaselineId/snapshotId） | evidence §4 | 派发绑定、身份核对扩展字段 |
| EvaluatorRegistry（find/manifest 摘要）、IEvaluatorFactory/IEngineeringEvaluator、EvaluationRequest/Output、IEvaluationContext | evidence §9 | worker 内评估调用；主/worker manifest 比对；IEvaluationContext 实现 |
| aggregateVerdict、ResultEnvelope::make/validateCombination、FormalPassEligibility | evidence §6.4/§7 | 接纳路径组装与二次校验 |
| judgeCacheHit/judgeCheckpointCompatibility | evidence §8.2 | 缓存/检查点判定（纯函数调用） |
| IResultArchivePort（begin/writeBatch/finalize/abandon/ArchiveSessionRef/RunManifest）、ProjectStore（writable/requestClose/closed/subscribeClose）、StoreError | project §5.1/§5.6 | 归档、存储上下文引用、只读拒绝 |
| IRuntimeSnapshotFactory 等价能力（经注入）、CompileCacheKey/judgeCompileCacheCompatibility（经注入）、MaterializedSnapshotCodec 字节（经注入产出） | runtime §9/§13.2 | Preparing 段模型准备、编译缓存判定、worker 物化投递 |
| RuntimeNameMap 反解（经注入 INameResolverAdapter） | runtime §7、CON-06 | 接纳时名称反解（结果引用 ObjectId 化） |
| EngineeringPolicySet 内容身份（值传递） | policy §5.3、CON-06 | 运行绑定扩展字段（PolicyIdentity） |

### 3.3 对 runtime / policy / diagnostics 能力的注入边界（依赖白名单的实施形态）

ARCH §3.5 依赖表登记 execution 的同层/下层边为 **core、evidence、diagnostics、project 四条**（表外边＝构建失败，SA-10）；而运行时视图（ARCH §4.1）与 runtime.md §10.9 客观上要求 execution 在 Preparing 段驱动模型编译/物化/编译缓存判定。解决方案＝**值传递＋最小注入接口**（与 evidence D-07、project §5.3.6、runtime §3.3 同一模式；适配器归 L5 应用壳装配期统一提供）：

```cpp
// Ports.hpp —— execution 定义的最小注入接口（适配器归 L5，不归 execution）
class ICancelSignal {                    // 取消信号的最小形态；L5 将其适配为 runtime ICompileCancelToken
public:                                  // 与 policy IPolicyCallContext（policy §9.7）
    virtual ~ICancelSignal() = default;
    virtual bool cancellationRequested() const = 0;   // 并发只读；运行终结后恒 true（迟到调用安全）
};

struct MaterializedDispatch {            // 派发物：物化载荷（字节对 execution 不透明）
    std::vector<std::uint8_t> snapshotBytes;   // evidence SnapshotCodec(materialized)（身份＝refs-only 形态）
    std::vector<std::uint8_t> modelBytes;      // runtime MaterializedSnapshotCodec（适配器产出；worker 侧由
                                               // 评估器装配消费——execution 只透传，不解析）
    core::ContentIdentity snapshotIdentity;    // 供登记扩展字段与 worker 握手核对
    core::ContentIdentity modelIdentity;       // 同上（runtime §9.2 物化核对值的主进程侧副本）
};
struct ModelPrepareResult {
    bool ok;                                     // 编译/物化成功
    bool compileCacheFullReuse;                  // 编译缓存完整命中（经 ICompileCacheJudge）
    bool compileCacheWorkCellOnlyReuse;          // 仅 WC 层可复用（runtime §9.4 三态）
    MaterializedDispatch dispatch;
    std::vector<core::DiagnosticRecord> diagnostics;
};
class IExecutionModelService {           // 适配 runtime：IRuntimeSnapshotFactory::create＋缓存键＋物化编码
public:
    virtual ~IExecutionModelService() = default;
    virtual ModelPrepareResult prepare(const evidence::AnalysisSnapshot&,
                                       const PrepareOptions&, ICancelSignal&) = 0;
};
class ICompileCacheJudge {               // 适配 runtime judgeCompileCacheCompatibility（纯函数）
public:
    virtual ~ICompileCacheJudge() = default;
    virtual CompileCacheVerdict judge(const CompileCacheKeyView& requested,
                                      const CompileCacheKeyView& cached) const = 0;
};
class INameResolverAdapter {             // 适配 runtime ⑥端口反解（CON-06：接纳前名称→ObjectId）
public:
    virtual ~INameResolverAdapter() = default;
    virtual std::optional<core::ObjectId> tryResolve(core::ContentIdentity nameMapIdentity,
                                                     std::string_view runtimeName) const = 0;
};
class IExecutionDiagnosticsSink {        // 对齐 project §5.0 IDiagnosticsSink 形态；sink 名称/归属统一按 P-EX-8 裁决（diagnostics.md 已产出，收编见其 §4.6）
public:
    virtual ~IExecutionDiagnosticsSink() = default;
    virtual void report(const core::DiagnosticRecord&) = 0;
    virtual void reportDev(const std::string& channel, const std::string& message) = 0;
};
```

- **适配器位置**：L5 应用壳装配期统一提供（约数十行，非"仅转发无边界价值的包装器"——价值＝跨层依赖方向隔离，ARC-02 端口精神的实施件）。
- **策略**：已解析策略内容身份随快照 `PolicyRef` 值传递（CON-06）；worker 内评估器经④端口消费策略实现（worker 装配目标合法链接，§6.4）；execution 主进程不解析策略。
- **任务指令与依赖表的一处出入登记**（P-EX-3）：任务指令述"execution 可依赖 core、evidence、runtime、policy、project、diagnostics"；ARCH §3.5 实际登记四条边（runtime/policy 不在表内）。本文按 §3.5 白名单＋注入消费执行（R-1/R-2 门禁的硬约束），不私自增边；如架构侧决定补登 execution→runtime 边，本文 §3.3 注入层可原样退役为直连（接口形状不变）。

### 3.4 命名空间、错误类型与 CMake 集成

- 命名空间 `sdurws::ird::execution`；目标 `sdurws_ird_execution`（骨架 INTERFACE → EX-T01 升级 STATIC），别名 `RWS::ird::execution`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_execution PUBLIC RWS::ird::core RWS::ird::evidence RWS::ird::project)`（diagnostics 边：ARCH §3.5 已登记，当前经注入实现、目标暂不链接——链接形态按 P-EX-8 裁决，不因 diagnostics.md 已产出而自动消账；同边在 io/ui/reporting 侧为直接链接，口径差异已登记）。
- worker 目标 `sdurws_ird_execution_worker`（WIN32 可执行）：链接 `sdurws_ird_execution`＋评估器装配清单（阶段 A 仅测试替身；正式评估器链接属 L5 装配决策，登记为**装配目标**——业务目标互链红线 R-1 不因此失效，worker 不链接任何 `_plugin`/Widgets，计算内核零 Qt）；注册随 EX-T06。
- 测试目标：`sdurws_ird_execution_test`（单元内：状态机/登记表/调度逻辑）、`sdurws_ird_execution_contract_test`（跨单元：与 evidence 校验器、与 project 归档端口、真实 worker 进程场景；gtest 按 DTB §5.5 接入），随 EX-T01 登记；产品目标不链 testkit（T-1 红线）。
- 头包含形式 `#include <sdurws/ird/execution/RunRegistry.hpp>`；私有实现头不入 `include/`（R-2 纪律）。

```cpp
// Errors.hpp
enum class ExecutionErrorCode {          // token 稳定（诊断/日志承载）；码值建议归 diagnostics StableCodeRegistry
    SubmissionRejected,     // execution/submission-rejected       提交验证失败（快照/评估器/权限/预算）
    StaleSnapshot,          // execution/stale-snapshot            快照身份失效或载荷校验失败
    StoreReadOnly,          // execution/store-read-only           只读模式拒绝正式评估（PM-07）
    ResourceInsufficient,   // execution/resource-insufficient     资源不足（先节流后诊断，NFR-PERF-04）
    CapabilityUnsupported,  // execution/capability-unsupported    任务能力不支持（如暂停）——显式反馈非静默
    WorkerLaunchFailed,     // execution/worker-launch-failed      worker 启动失败
    WorkerCrashed,          // execution/worker-crashed            worker 异常退出（NFR-REL-02）
    WorkerHung,             // execution/worker-hung               心跳失联（超时判定）
    ForceTerminated,        // execution/force-terminated          强制终止（区别于普通失败的显式标记）
    ChannelProtocolError,   // execution/channel-protocol-error    帧非法/版本失配/序号断裂（开发诊断）
    RegistryUnknownRun,     // execution/registry-unknown-run      未知 RunId（迟到结果拒绝，开发诊断）
    RegistryMismatch,       // execution/registry-mismatch         五元组任一字段失配（开发诊断）
    StaleAttempt,           // execution/stale-attempt             陈旧 AttemptId（被取代尝试）
    CheckpointCorrupt,      // execution/checkpoint-corrupt        检查点完整性校验失败
    CheckpointIncompatible, // execution/checkpoint-incompatible   检查点不兼容（版本/身份/判定拒绝）
    ArchiveFailed,          // execution/archive-failed            归档失败（透传 StoreError 细节）
    ArchiveAuthorityLost,   // execution/archive-authority-lost    上下文已 Closed 的迟到归档（A7 防御）
    ContextClosed,          // execution/context-closed            调度器已排空/关闭
    InvalidState,           // execution/invalid-state             非法转换请求（状态机拒绝）
};
class ExecutionError : public std::runtime_error {
public: ExecutionError(ExecutionErrorCode, std::string detail);
        ExecutionErrorCode code() const noexcept;   // detail 面向开发诊断，前缀 "execution/<域>:"
};
```

EX-\* 稳定诊断码清单（**码值分配权威＝diagnostics StableCodeRegistry，已收编全量 18 项〔diagnostics.md §4.6，2026-09-10〕**，与下列清单一致）：`EX-TASK-REJECTED`、`EX-SNAPSHOT-STALE`、`EX-STORE-READ-ONLY`、`EX-RESOURCE-INSUFFICIENT`、`EX-CAPABILITY-UNSUPPORTED`、`EX-WORKER-LAUNCH-FAILED`、`EX-WORKER-CRASHED`、`EX-WORKER-HUNG`、`EX-FORCE-TERMINATED`、`EX-CHANNEL-PROTOCOL-ERROR`（开发级）、`EX-REGISTRY-UNKNOWN-RUN`（开发级）、`EX-REGISTRY-MISMATCH`（开发级）、`EX-STALE-ATTEMPT`（开发级）、`EX-CHECKPOINT-CORRUPT`、`EX-CHECKPOINT-INCOMPATIBLE`、`EX-ARCHIVE-FAILED`、`EX-ARCHIVE-AUTHORITY-LOST`、`EX-TASK-INTERRUPTED`（恢复呈现数据源，PM-08/PM-15）。**枚举对齐说明（v0.2）**：枚举值 `ContextClosed`/`InvalidState` 属调用方契约违约（ExecutionError 异常 fail-fast，token 仅供日志，不发稳定诊断码）；`EX-TASK-INTERRUPTED` 为恢复期 Interrupted 终态的状态标注诊断（§5.5/§7.5，非 API 错误），故无对应枚举值——清单与枚举"17 个错误码 1:1＋2 个 fail-fast token＋1 个状态标注码"的关系就此冻结。

### 3.5 源码目录布局

```
industrialrobot/execution/
  include/sdurws/ird/execution/   # §3.1 公共头（契约权威）
  src/
    Scheduler.cpp  StateMachine.cpp  RunRegistry.cpp  Admission.cpp
    WorkerSupervisor.cpp  Checkpoint.cpp  CacheCoordinator.cpp  EventBus.cpp
    win32/ProcessLauncher.{hpp,cpp}  # CreateProcessW＋Job Object；IProcessOps 接缝（fault 注入）
    win32/ChannelPair.{hpp,cpp}      # 管道帧读写（长度前缀；读写超时）
    win32/JobScope.{hpp,cpp}         # 作业对象 RAII（强杀进程树）
    win32/MemProbe.{hpp,cpp}         # 主进程＋worker 内存汇总（GlobalMemoryStatusEx＋job 内存）
  worker/main.cpp                   # worker 入口（宿主循环；评估器经注册表实例化）
  test/  contract_test/             # EX-* 用例（§11）
```

---

## 4. 任务、运行与尝试身份

### 4.1 身份概念总表与混用防线

execution 视角的全部身份概念及其所有权（**类型未在本单元定义的只消费，不重定义**）：

| 概念 | 类型/载体 | 回答的问题 | 分配者 | 所有权 | 与其他概念的边界 |
| --- | --- | --- | --- | --- | --- |
| **TaskId** | `tsk-<32hex>`（Id128 强类型，同 core.md §4.1 纪律：tag 区分、全零保留、往返严格） | 用户提交的**一次逻辑任务**是哪一个 | execution（提交受理时） | **execution**（core 未定义此类型；本文按 core D-03 同纪律新增，属本单元数据模型而非跨单元契约——不进 core 事件五元组） | 绑定一份冻结输入（快照＋切片＋模式＋评估器）；改求解配置重跑＝新 TaskId（evidence §4.2.4：新 sliceId→新运行）；同 TaskId 内的进程崩溃恢复/暂停继续不换 TaskId |
| **RunId** | core::RunId（`run-<32hex>`） | 一次**执行登记**（RunRegistry 条目）是哪一个 | execution（派发登记时） | 类型 core，**值分配与登记规则归 execution**（core §4.1 分配列） | 一个 TaskId 在正常路径恰有一个 RunId；RunId 是归档目录键（`results/<run-id>/`）与五元组成员 |
| **AttemptId** | core::AttemptId（`att-<十进制>`，≥1） | 一次**进程/恢复尝试** | execution（重试/恢复时递增；旧 attempt 标记被取代） | 同上 | worker 崩溃后重启、暂停后继续、检查点恢复→同 RunId **新 AttemptId**；迟到结果携带被取代 attempt→拒绝（ARCH §4.5） |
| SnapshotId | core::ContentIdentity（evidence AnalysisSnapshot.snapshotId） | 本次评估消费的完整不可变输入闭包的字节身份 | evidence（builder 计算） | evidence | 派发绑定的扩展核对字段；execution 不计算 |
| RevisionId | core::RevisionId | 快照锚定的修订（对象解析唯一锚） | project | core/project | 归档目标修订＝登记记录的 revision，与当前 HEAD 无关 |
| InputSliceId | core::ContentIdentity（evidence InputSlice.sliceId） | 评估输入切片内容身份（缓存键与失效判据） | evidence | evidence | 派发绑定；缓存查找键的组成 |
| PolicyIdentity | core::ContentIdentity（快照 PolicyRef） | 已解析 EngineeringPolicySet 内容身份 | policy（计算）经快照值传递 | policy | 登记扩展字段（CON-06）；execution 不解析策略 |
| EvaluatorKey＋版本 | evidence::EvaluationKey＋contractVersion | 用哪个评估器、哪个输入/输出契约版本 | evidence（注册表） | evidence | 派发前 find；主/worker manifest 摘要比对 |
| WorkerId | `std::uint64_t`（supervisor 内单调；规范文本 `wkr-<十进制>`） | 哪一个 worker 进程实例 | execution（launch 时） | **execution**（内部身份，不入事件/登记表持久字段；诊断可携带） | 与 PID 区分：WorkerId 是池内逻辑槽位身份，PID 是 OS 事实（WorkerStatus 记录两者） |
| CheckpointId | {core::RunId run; std::uint64_t sequence;}（规范文本 `chk-<run>-<seq>`） | 哪一份检查点 | execution（写出时分配；目录编址归 project `checkpoints/<run-id>/<seq>/`，project §4.1） | **execution**（身份）＋project（磁盘编址） | 与最终结果区别：检查点是**可续中间状态**，不构成任何结论（§8.1） |

**混用防线（冻结规则）**：

1. 全部强类型互不可隐式转换（core D-03 同纪律）；TaskId 不进 core 五元组——事件与通道消息携带的是 `core::TaskIdentity`（五元组）；TaskRecord 内部以 TaskId 为主键、以 TaskIdentity 关联当前运行。
2. **迟到结果核对只认完整五元组**（ARCH §4.5 A8：projectId/branchId/revisionId/runId/attemptId 任一字段与登记记录不一致即拒绝）——SnapshotId/sliceId/EvaluatorKey/PolicyIdentity 是登记记录的**扩展绑定字段**，用于结果包络一致性核对（不符→开发诊断＋拒绝，§9.3 第二道核对），不替代五元组判定，也不放宽判定（HEAD 变化绝不参与接纳判定）。
3. 身份一经分配不可变；AttemptId 单调递增且被取代尝试保留在登记记录的 `supersededAttempts`（供迟到判定与审计）。

### 4.2 记录字段表（TaskRecord / RunRecord / AttemptRecord）

通用约定：值语义；构造后不可变字段以 ─ 标注；会话内存态（不持久化——磁盘痕迹唯一经 project 归档预留与检查点，§5.4）；调度线程为唯一写者（并发只读安全；查询经快照拷贝）。

**TaskRecord（任务主记录）**：

| 字段 | 类型 | 必填 | 默认 | 版本 | 身份语义 | 可变性 | 所有权 | 生命周期 | 合法/非法实例 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `taskId` | TaskId | 是 | — | v1 | 主键 | 不可变 | execution | 提交受理→终态后保留窗口（§10.2 releaseResources）→释放 | 非零；重复提交得到不同 TaskId（幂等性不按提交内容承诺——同一输入重复提交＝两个任务，各自完整执行） |
| `submission` | TaskSubmission | 是 | — | v1 | 请求内容（快照/评估器/模式/优先级/resumeFrom） | 不可变 | execution | 同上 | snapshot 已冻结（evidence builder 产物）；mode∈{Preview,Quick,Verified} |
| `capability` | TaskCapability | 是 | 由评估器 descriptor 推导 | v1 | 暂停/检查点粒度/终止代价声明（TASK-01） | 不可变（评估器注册期声明，运行期只读） | 评估器（evidence descriptor）＋execution 承载 | 同上 | supportsPause=false 的任务收到暂停请求→显式反馈（EX-CAPABILITY-UNSUPPORTED），不静默 |
| `state` | core::TaskState | 是 | Queued | v1 | 当前状态机状态 | 仅状态机转移修改 | execution | 同上 | 九态之一；非法值构造拒绝 |
| `run` | std::optional\<core::RunId\> | 否 | nullopt | v1 | 当前关联运行（登记后非空） | 派发时置入；终态后不变 | execution | 同上 | Queued 期可空；Running 起必非空 |
| `attempt` | core::AttemptId | 条件 | 0（空） | v1 | 当前尝试 | 每次重启/继续递增 | execution | 同上 | run 非空时 ≥1 |
| `progress` | std::optional\<ProgressReport\> | 否 | nullopt | v1 | 最近进度 | 流式更新（节流，§6.2） | execution | 同上 | percent≤100；phaseToken 非空 |
| `termination` | std::optional\<TerminationCause\> | 否 | nullopt | v1 | 终结原因 | 终态时一次写入 | execution | 同上 | 仅终态非空；Canceled/Failed/Completed/Interrupted 四类（§5.3） |
| `archivePhase` | ArchivePhase | 是 | NotApplicable | v1 | 归档阶段（**独立于任务状态**，§5.6） | 归档路径推进 | execution | 同上 | Preview 任务恒 NotApplicable；Quick/Verified 终态后推进至终相 |
| `diagnostics` | std::vector\<core::DiagnosticRecord\> | 是 | 可空 | v1 | 任务级诊断累积 | 追加 | execution | 同上 | 诊断码合法（ERR-01 字段完整） |

**RunRecord＝RunRegistry 登记记录（`RunRegistration`，§9.1 字段表）**；**AttemptRecord（execution 内部，随 RunRecord 保存）**：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `attempt` | core::AttemptId | 是 | 尝试号（≥1） |
| `worker` | std::optional\<WorkerId\> | 否 | 承载该尝试的 worker（内联执行为空） |
| `startedAtUtc / endedAtUtc` | time_point / optional | 是 / 否 | 起止 |
| `endKind` | std::optional\<enum\> | 否 | Completed / Canceled / Failed / Interrupted / Superseded（被继续/恢复取代） / ForceTerminated |
| `checkpoint` | std::optional\<CheckpointId\> | 否 | 该尝试保留的最近检查点（取消/失败/强杀均保留——ARCH §4.3/§4.4） |
| `exitCode` | std::optional\<int\> | 否 | worker 进程退出码（崩溃诊断） |

### 4.3 身份分配协议（何时、由谁、如何）

```
提交（调度线程串行）：
  提交验证通过 → TaskId::generate() → TaskRecord{state=Queued} → 入队 → 事件 TaskStatusChanged(Queued)
派发（调度线程，出队后 Preparing）：
  RunId::generate() → RunRegistry.registerRun（登记完整五元组＋扩展绑定字段＋runDir＋runKind）
  → AttemptId=1 → 归档预留 archive.begin（Quick/Verified；§9.5）→ worker 启动
重启/恢复（同 RunId）：
  worker 崩溃（不自动重试，D-09）→ 任务 Failed；用户重跑＝新 TaskId/新 RunId（可 resumeFrom 检查点）
  暂停后继续 / 检查点恢复 → 同 RunId，AttemptId+1（登记追加，旧 attempt 移入 supersededAttempts）
事件与通道消息：一律携带当前 (run, attempt) 的完整五元组；被取代 attempt 的迟到消息按陈旧拒绝
```

---

## 5. 任务状态机与转换守卫

### 5.1 状态定义（九态，词表归 core；本文零新增）

| 状态 | 定义 | 持久/内存 | 可重试性 | 用户可见（PM-03 九态短标签的语义源） | 失败诊断 |
| --- | --- | --- | --- | --- | --- |
| `Queued` | 已受理入队，未派发 | 内存 | 可取消（未派发即取消，不适用 2 s 协作窗——ARCH §4.3 原文） | 排队中 | — |
| `Preparing` | 已出队：模型准备/物化、RunRegistry 登记、归档预留、worker 启动中 | 内存＋归档预留目录（磁盘痕迹） | 可取消（丢弃组装中的快照/派发物）；失败→Failed | 准备中 | EX-WORKER-LAUNCH-FAILED/EX-SNAPSHOT-STALE/EX-STORE-READ-ONLY |
| `Running` | worker 执行中（或内联轻任务执行中） | 内存＋归档预留 | 可取消（协作窗）；崩溃→Failed | 计算中（附进度阶段，UX-10） | EX-WORKER-CRASHED/EX-WORKER-HUNG |
| `Paused` | 暂停确认边界已达成（最近检查点已写出并确认），无在途批次 | 内存＋检查点（磁盘） | 继续＝同 Run 新 Attempt；暂停中取消直达且**保留检查点**（ARCH §4.3 A5） | 已暂停 | — |
| `Canceling` | 取消请求已受理：停止派发新批次，在途批次收敛中 | 内存 | — | 取消中 | — |
| `Canceled` | 终态：协作取消完成（正常用户取消**不属于错误**，不产生错误诊断——UX-03） | 内存（归档 abandon） | 重跑＝新任务 | 已取消 | 仅取消失败/异常中断走错误路径 |
| `Completed` | 终态：运行终结、结果经接纳（五元组核对＋envelope 构造校验通过）。**不代表结果已归档**（归档由 archivePhase 独立表达，§5.6），更不代表工程通过 | 内存（归档进行/完成） | — | 已完成 | 归档失败→EX-ARCHIVE-FAILED（不改任务终态） |
| `Failed` | 终态：执行异常（评估器失败、worker 崩溃/卡死、通道错误、准备失败、强制终止——携 EX-FORCE-TERMINATED 区分标记） | 内存（归档 abandon；**最近检查点保留可续**——ARCH §4.4） | 重跑＝新任务（可从检查点续，新身份链） | 失败（附对象定位与修复建议数据——UX-10/UX-03） | 终结原因诊断必附 |
| `Interrupted` | 终态（恢复期指派）：进程/会话中断——主进程崩溃后，重启恢复扫描发现归档预留未终结的运行，重建为"已中断"（NFR-REL-03，不伪装为完整结果） | 恢复期重建 | 重跑＝新任务 | 已中断（可重跑） | EX-TASK-INTERRUPTED |

> **上游对齐说明（不新增状态）**：任务指令示例列出 Created/CancelRequested/Rejected 三态名，上游（REQUIREMENTS PM-03"9 态短标签"、ARCH §4.3 状态机、core TaskState 词表）实际定义即上述九态。按"不自行增加任务状态"约束（任务约束§三/§八），本文执行九态：提交受理前的拒绝发生在**状态机之外**（提交边界验证失败→SubmitResult.rejected＋诊断，不产生 TaskRecord、不占用任何状态）；`Canceling` 即"取消请求已受理"相（对应指令中 CancelRequested 的语义）；不存在 Created 态（受理即 Queued）。登记 P-EX-4。

### 5.2 状态机图与转换矩阵

```
（提交验证通过）                                     事件：TaskStatusChanged（每次转移发布，core ⑤端口）
      │ submit
      ▼
  ┌────────┐  出队+资源可用   ┌───────────┐  物化/登记/预留/worker 握手成功   ┌─────────┐
  │ Queued │ ──────────────► │ Preparing │ ────────────────────────────────► │ Running │◄────────┐
  └────────┘                 └───────────┘                                  └─────────┘         │
      │ 取消(未派发:直接出队)      │ 取消:丢弃派发物     准备失败(快照/启动/只读)  │      │完成:接纳通过      │继续(新Attempt)
      ▼                          ▼                    ▼              ┌─────┴────┐ │(检查点恢复)
 ┌──────────┐               ┌──────────┐         ┌────────┐          │          │ └──┬───────┘
 │ Canceling│◄──────────────│          │         │ Failed │◄────────┤ 暂停请求   │    │暂停确认边界
 └──────────┘               │  (经     │         │(保留检查点)        │(检查点写出 │    ▼
      │                     │ Canceling│         └────────┘          │  且确认)  │ ┌────────┐
      ▼                     │  过渡)   │               ▲             └────┬─────┘ │ Paused │
  ┌─────────┐               └──────────┘               │ 强制终止          │        └────────┘
  │ Canceled│                     │                    │ (Canceling超时/   │暂停中取消      │取消(无在途批次,
  └─────────┘                     ▼                    │ 用户强杀)         │(保留检查点,A5) │直达,保留检查点)
                                 ▼                    │                  ▼               │
                            ┌──────────┐               │            ┌──────────┐          │
                            │ Canceled │               └───────────►│ Canceling│◄─────────┘
                            └──────────┘    运行终结:    卡死/崩溃/通道错误  └────┬─────┘
                                 ▲          ┌─────────┐                     │批次≤10s收敛 │超时(§4.4)
                                 └──────────│Running  │────────────────────►┘             │终止进程树
                                            └─────────┘                                    ▼
                                     完成事件迟到/项目切换/崩溃后恢复:                        ┌────────┐
                                     ┌────────────┐                                   │ Failed │
                                     │ Interrupted│◄─────────────────────────────────── └────────┘
                                     └────────────┘   (恢复期指派:归档预留未终结的运行)
   内联轻任务（可预测<1s,ARCH §4.1）: Queued→Preparing→Running→终态 于调度线程内联完成（同一转移表）
```

转换矩阵（行＝源态，列＝目标态；✔＝合法转换；·＝禁止。触发/守卫见 §5.3）：

| 源＼目标 | Queued | Preparing | Running | Paused | Canceling | Canceled | Completed | Failed | Interrupted |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Queued | — | ✔ | · | · | ✔ | ✔（经 Canceling 瞬时） | · | · | · |
| Preparing | · | — | ✔ | · | ✔ | ✔（经 Canceling） | · | ✔ | · |
| Running | · | · | — | ✔ | ✔ | ✔（经 Canceling） | ✔ | ✔ | ·（运行期不指派；恢复期指派） |
| Paused | · | · | ✔（继续，新 Attempt） | — | ✔ | ✔（经 Canceling，保留检查点） | · | · | · |
| Canceling | · | · | · | · | — | ✔ | · | ✔（超时强杀） | · |
| Canceled | · | · | · | · | · | —（终态） | · | · | · |
| Completed | · | · | · | · | · | · | —（终态） | · | · |
| Failed | · | · | · | · | · | · | · | —（终态） | · |
| Interrupted | · | · | · | · | · | · | · | · | —（终态） |

**禁止转换示例（守卫拒绝，EX-SM-7 用例）**：Paused→Completed（必须先继续）；Canceled/Failed/Interrupted→任何态（终态无出边；重跑＝新 TaskId）；Queued→Running（必须经 Preparing——登记与预留不可跳过）；Completed→Failed（归档失败不改任务终态——归档是 archivePhase 轴）；Running→Paused（无检查点确认的"即时暂停"——暂停确认边界＝检查点，AT-35）。

### 5.3 逐转移表（触发／守卫／事件／清理）

| # | 转移 | 触发 | 守卫（并发与前置） | 事件 | 资源清理与检查点 |
| --- | --- | --- | --- | --- | --- |
| T1 | →Queued | submit 受理 | 提交验证全通过（§6.3）；调度器未 shutdown | TaskStatusChanged(Queued) | — |
| T2 | Queued→Preparing | 调度器出队 | 资源预算允许（否则留队＋诊断）；缓存查找已执行（命中→不经本转移，直接短路径完成，§8.2） | TaskStatusChanged(Preparing) | — |
| T3 | Queued→Canceling | requestCancel | 状态==Queued（重复取消幂等：已在 Canceling→ack） | TaskStatusChanged(Canceling) | 直接出队，无在途批次（不适用 2 s 协作窗——ARCH §4.3） |
| T4 | Canceling→Canceled | 在途批次收敛（≤10 s）或无在途批次 | — | TaskStatusChanged(Canceled) | worker 退出回收；归档 abandon(Canceled)；**保留最近检查点** |
| T5 | Preparing→Running | 归档预留成功＋worker 握手成功（manifest 摘要一致＋物化身份核对通过） | AttemptRecord 建立 | TaskStatusChanged(Running) | — |
| T6 | Preparing→Canceling | requestCancel | 组装中止：丢弃物化派发物、终结归档预留 | TaskStatusChanged(Canceling) | 无在途批次→速达 Canceled |
| T7 | Preparing→Failed | 准备失败（快照校验/评估器缺失/worker 启动失败/只读拒绝） | 终结原因记录 | TaskStatusChanged(Failed) | 归档预留 abandon(Failed)；诊断必附 |
| T8 | Running→Completed | worker FinalOutput 到达且接纳通过（五元组核对＋名称反解＋envelope 构造校验，§9.3） | envelope 合法（make 成功）；否则→Failed（EX-CHANNEL-PROTOCOL-ERROR 或域错误码） | TaskStatusChanged(Completed)；归档 finalize 后追加 ResultArchived | worker 退出回收；缓存登记（仅 Completed 产物，§8.2） |
| T9 | Running→Failed | 评估器失败／worker 崩溃／卡死超时强杀／通道协议错误 | AttemptRecord.endKind 记录区分（ForceTerminated 携显式标记） | TaskStatusChanged(Failed) | 归档 abandon(Failed)；**最近检查点保留可续**（ARCH §4.4） |
| T10 | Running→Paused | requestPause＋capability.supportsPause＋检查点写出并确认（暂停确认边界＝检查点，AT-35） | 不支持→显式反馈（EX-CAPABILITY-UNSUPPORTED），状态不变 | TaskStatusChanged(Paused) | worker 退出回收（释放预算；检查点在盘） |
| T11 | Paused→Running | requestResume＋检查点兼容判定通过（evidence judgeCheckpointCompatibility） | **新 AttemptId**（登记追加，旧 attempt 标 Superseded）；继续不重复统计（AT-35：续跑统计自检查点累计） | TaskStatusChanged(Running) | 新 worker 加载检查点启动 |
| T12 | Paused→Canceling | requestCancel | 暂停态无在途批次，直达 | TaskStatusChanged(Canceling)→(Canceled) | **保留最近检查点**（可续跑——ARCH §4.3 A5） |
| T13 | Canceling→Failed | 2 s 内未入 Canceling 或批次 10 s 未收敛→强制终止进程树 | 强杀仅针对该任务 worker（Job Scope，§6.4） | TaskStatusChanged(Failed)＋EX-FORCE-TERMINATED | abandon(ForceTerminated)；检查点保留；**不伪装为普通失败**（诊断显式区分） |
| T14 | →Interrupted（恢复期） | 重启恢复扫描发现归档预留未终结（无 manifest 且无存活登记） | 主进程新会话启动期；不与运行期转移并发 | TaskStatusChanged(Interrupted) | 归档预留 abandon 由恢复流程执行（§9.5 强杀时机） |

**并发守卫（总规则）**：状态机唯一写者＝调度线程；一切控制请求（cancel/pause/resume/forceTerminate）投递至调度线程命令队列串化处理（无锁状态读取经不可变快照）；同一任务的并发控制请求按到达顺序处理，后到者对已生效语义幂等（重复取消＝ack；暂停后取消＝T12；取消后暂停→拒绝 InvalidState）。

**Queued 取消**：能取消（T3）；Preparing 取消清理＝丢弃组装中的快照/派发物＋终结归档预留（T6）；Running 取消传递＝通道 CancelRequest→worker IEvaluationContext.cancellationRequested() 轮询→批次边界收敛（T8 前）；Paused 允许继续（T11）；暂停中取消保留检查点（T12）。

### 5.4 持久化/内存边界

| 数据 | 位置 | 生命周期 |
| --- | --- | --- |
| TaskRecord/RunRegistry/AttemptRecord | **会话内存**（调度线程私有＋只读快照发布） | 调度器存活期；终态任务保留可查询窗口（默认 30 min，实现参数 D-07）后 releaseResources 回收 |
| 磁盘任务痕迹 | 唯一经 project：`results/<run-id>/`（派发即归档预留，§9.5）与 `checkpoints/<run-id>/<seq>/` | project 拥有（§17 表注）；崩溃后由恢复扫描发现（NFR-REL-03 的"已中断"数据源） |
| Queued 任务 | 纯内存 | 主进程崩溃即消失（无可观测残留、不需"已中断"呈现——P-EX-6 登记） |

### 5.5 能力声明（TASK-01）

任务能力在评估器注册期由 descriptor 派生（evidence EvaluatorDescriptor 无能力字段时按"最小能力"处理——execution 侧补充读取注册表扩展声明 `EvaluatorRuntimeCapabilities`，登记为 evidence 交接项 P-EX-7）：`supportsPause`（R2 承诺，ARCH §4.3）、`checkpointGranularity`（None/Batch/Segment/Sample）、`forceTerminateCost`（Cheap/Moderate/Expensive——仅声明与呈现，不影响协议）。R1 内不支持暂停的任务收到暂停请求→显式状态反馈（不静默忽略，ARCH §4.3 原文）。

### 5.6 四轴正交关系表（任务状态 × 执行结果 × 工程判定 × 当前性）

| 轴 | 值域 | 何时确定 | 所有权 | 可变性 | 典型组合（防混淆样例） |
| --- | --- | --- | --- | --- | --- |
| 任务执行状态 | core::TaskState 九态 | 调度全程 | **execution** | 状态机推进 | Paused（不是 Completed——暂停即无结果） |
| 执行结果完整性 | core::TaskOutcome 四值 | 运行终结（envelope） | evidence（envelope 承载；映射规则 execution） | 构造后不可变 | Canceled＋完整历史进度但**无**判定（不进正式报告/可行集——TASK-02） |
| 工程判定 | core::EngineeringStatus | 汇总时（仅 Completed） | evidence | 构造后不可变 | Completed＋DataInsufficient（计算完成但证据不足——合法且常见）；Failed＋NotApplicable（执行失败不伪造判定） |
| 结果当前性 | Current/Superseded（＋不可判定形态） | 每次相对目标上下文计算 | evidence（投影） | 派生可重算、不写回 | Superseded＋Feasible（历史证据仍有效，仅对当前输入过期） |
| （辅）归档阶段 | ArchivePhase：NotApplicable/Reserved/Archiving/Archived/ArchiveFailed | Completed 后异步推进 | **execution** | 归档路径推进 | Completed＋ArchiveFailed（**任务完成≠归档完成**；诊断＋有界重试＋abandon，§9.5） |

TaskState→TaskOutcome 终态映射（core.md §10.3 交接项，本文冻结）：`Completed→Completed`；`Canceled→Canceled`；`Failed→Failed`；`Interrupted→Interrupted`；非终态无 outcome。四轴互不推导：任务 Completed 不意味着工程通过（判定轴独立）、不意味着结果 Current（当前性轴独立）、不意味着归档完成（辅轴独立）；当前性 Superseded 不改写历史 payload（CON-02）。

---

## 6. 调度、资源治理与工作进程

### 6.1 队列、优先级与并发治理

| 项 | 设计 | 依据/说明 |
| --- | --- | --- |
| 队列排序 | 双优先级（Interactive/Background）＋同级 FIFO（提交序号单调）；不抢占（已 Running 任务不被更高优先级打断——取消/暂停只能由用户发起） | 实现参数化排序键（D-04）；需求未定义抢占语义 |
| 并发上限 | `maxConcurrentTasks`（默认＝min(4, 逻辑核/2)，可配，实现参数 D-05）；同项目并发正式任务上限（默认 2，可配）——超出排队 | 防单项目独占（多任务排队诊断可见） |
| worker 数量 | 池化 `maxWorkers`（默认＝逻辑核数-1，下限 1，可配）；轻任务（可预测 <1 s，ARCH §4.1）在调度线程内联执行（零派发开销） | 池按内存预算节流伸缩（§6.3） |
| 内存预算 | 主进程＋全部 worker 合计峰值默认 ≤ 物理内存 70%；接近上限（默认阈值 65%，实现参数）**先节流**：暂停派发新任务＋降低并行度（回收空闲 worker）→ 仍超限→"资源不足"诊断（EX-RESOURCE-INSUFFICIENT）＋新任务保持排队（**不失败已排队/运行中任务**） | NFR-PERF-04（上游阈值 70%）；R2 验收、阶段 A 交付基础形态（WP-08-T08） |
| CPU/线程预算 | worker 内并行线程数来自 AnalysisConfiguration（Configuration 条目，KIN-13——**进入切片身份与缓存兼容性**）；execution 不改写业务线程配置，仅约束 worker 总数与全局并行 | 线程配置属求解配置（evidence §4.2.1），非调度预算 |
| 同项目/同快照并发 | 同一 (project, snapshotId) 的多任务共享派发物缓存（物化字节会话级复用，D-08）；同一 runDir 绝无两个活动 worker（登记互斥——EX-RES-1 用例） | 派发物复用不改变任务身份（身份绑定快照内容，非字节副本） |
| 资源不足时的排队与诊断 | 排队（不拒绝、不失败）＋诊断（用户可见"等待资源"） | 提交期预算检查仅拒绝对列容量溢出（队列容量默认 256，实现参数——溢出＝SubmissionRejected＋诊断） |
| 取消等待任务 | Queued 取消直接出队（T3，无协作窗） | ARCH §4.3 |
| UI 线程不阻塞 | 提交/查询/控制全部非阻塞（提交即返回 TaskId；状态经事件与查询）；>1 s 工作一律后台（任务通道），UI 线程零计算 | NFR-PERF-01、ARCH §4.2 |
| 进度报告与节流 | ProgressReport{percent, phaseToken, batchesDone/Total}；调度器侧节流（同任务对外发布 ≤10 Hz，实现参数 D-07）＋状态变化即时发布；进度**不入**领域事件（core.md D-09——进度走 execution 自有通道与查询投影） | ARCH §4.1 通道四类之一 |

**任务开始前验证（提交与 Preparing 两道，§6.3 提交验证清单）**：快照冻结且完整（evidence builder 产物＋载荷摘要抽查）、修订仍在存储闭包内（防过期快照——修订目录被判定闭包外/对象缺失→EX-SNAPSHOT-STALE 拒绝）、评估器已注册且 contractVersion 与快照复现块一致（否则 SubmissionRejected）、project 上下文 Active 且 writable（Quick/Verified；只读→EX-STORE-READ-ONLY 拒绝启动并诊断——PM-07/ARCH §6.8）、策略与名称映射内容身份非空（CON-06，经快照校验器）、资源预算未超提交上限。**验证不改变任务身份**（预算变化只影响排队与派发时机）。

### 6.2 线程模型（execution 侧）

| 线程/通道 | 归属 | 职责与约束 |
| --- | --- | --- |
| 调度线程（每调度器 1 条） | execution | 状态机唯一写者；出队/派发/Preparing 编排；结果接纳与 envelope 组装；**调用归档端口的线程**（P-PR-4 冻结：归档端口由调度线程调用，经 project writer 互斥串行化——project §9.8；execution 不另持项目写锁） |
| 通道读取线程（每 worker 1 条） | execution | 阻塞读管道帧→投递调度线程队列（进度/心跳/结果批次）；不做业务解析外的处理 |
| worker 宿主线程池 | worker 进程内 | 评估器执行（线程数＝AnalysisConfiguration）；检查点写出在 worker 内序列化 |
| 资源监控线程 | execution | 周期内存采样＋节流决策建议（决策仍由调度线程执行） |
| 事件总线投递线程 | execution（EventBusImpl） | 满足 core 总线契约：publish 线程安全、同发布者 FIFO、退订不死锁；UI 线程 marshaling 由 ui 侧扩展承担（§13 交接） |

### 6.3 提交验证与内联门槛

```
submit(submission)（任意线程进入，转调度队列）：
 [V1] 形式校验：五元组身份前缀合法（project/branch/revision 属当前存储上下文）、快照已冻结（snapshotId 非零）、
      evaluatorKey 在注册表 find 命中且 contractVersion 相符、mode 合法
 [V2] 内容校验：快照修订闭包消费（经注入 IRevisionClosureSource 形态的查询——evidence §3.3 同源）＋
      载荷摘要抽查（物化时全量校验）；失败→EX-SNAPSHOT-STALE
 [V3] 权限校验（Quick/Verified）：store 上下文 Active ∧ writable；Preview 跳过（不写 results）
 [V4] 预算校验：队列容量；资源预算状态（超限仅影响排队优先级提示，不拒绝）
 任一失败→SubmitResult{rejected, diagnostics}（不产生 TaskRecord/状态——§5.1 注）
 通过→TaskId 分配→Queued
内联门槛：请求标记 inline-eligible 且可预测 <1 s（ARCH §4.1）→调度线程内联走 Preparing→Running→终态
（同一转移表；无 worker 派发；仍登记 RunRegistry——内联运行 run.worker=空）
```

### 6.4 工作进程模型（主进程／worker 边界）

```
主进程：
  任务登记(TaskId/RunId/Attempt) → 快照绑定(扩展字段) → 调度(出队/预算/缓存查找)
  → 启动 worker(launch+握手) → 接收进度/心跳/结果分片 → 五元组核对+名称反解+envelope 构造
  → 归档(project 端口) → 事件发布
worker（sdurws_ird_execution_worker，池化×N，按内存预算节流）：
  握手(manifest 摘要+协议版本) → 读取不可变输入(物化字节重建快照,身份核对) → 注册表实例化评估器
  → 执行评估器(evaluate; 周期查询 cancellationRequested; 周期 reportProgress)
  → 写检查点/结果片段(经通道回传主进程; worker 自身仅写自身临时目录)
  → 返回状态(FinalOutput/CancelAck/PauseAck/ErrorReport; 退出码)
```

边界规则（冻结）：worker 不直接修改项目正式目录（一切结果经主进程登记核对后由 project 归档——ARCH §6.1 磁盘写入唯一归属）；worker 不与 UI 交互（无 Qt、无窗口、无控制台输出到 UI；诊断一律经通道 ErrorReport 回传）；worker 结果必须经主进程 RunRegistry 核对（worker 自报的身份不可信——以登记为准）；worker 临时目录隔离（§6.6）。

**worker 生命周期**：launch（池取或新建进程）→ 握手（Hello/HelloAck：协议版本＋评估器 manifest 摘要比对，不一致→拒绝派发→Preparing→Failed，CON-06/AT-19 装配侧）→ DispatchRequest → 运行 → 终结（正常退出回收池／异常退出销毁）。池化 worker 可跨任务复用（进程保活，空闲超时回收——默认 120 s，实现参数）；**崩溃的 worker 永不回池**（新任务新进程）。

**worker 退出码约定集**（主进程据此分类终结；约定集之外的任何值〔含 Windows 异常码族 0xC0000005 等〕一律判 WorkerCrashed）：

| 退出码 | 含义 | 主进程处置 |
| --- | --- | --- |
| 0 | 正常终结（FinalOutput/CancelAck/PauseAck 已发） | 按通道最后消息走 T8/T4/T10 |
| 10 | 启动失败（依赖/装配错误） | Preparing→Failed（EX-WORKER-LAUNCH-FAILED） |
| 11 | 宿主内部错误（协议/装载错） | Running→Failed（EX-CHANNEL-PROTOCOL-ERROR 或宿主码） |
| 12 | 评估器失败（ErrorReport 已先行） | Running→Failed（域诊断透传） |
| 20 | 协作取消确认 | T4（Canceled） |
| 21 | 暂停确认（检查点已确认） | T10（Paused） |
| 其他/异常码 | 崩溃（NFR-REL-02） | Running→Failed（EX-WORKER-CRASHED；检查点保留） |

**主进程与 worker 时序图（正常完成路径）**：

```
 调度线程            通道读线程         worker进程            project归档端口        事件总线
    │ submit受理         │                  │                     │                  │
    │ Queued→出队→Preparing                │                     │                  │
    │ registerRun(五元组+runDir+扩展)       │                     │                  │
    │ ── archive.begin(预留) ─────────────────────────────────►│                  │
    │ ── launch+JobScope ──────────────► Hello(manifest摘要)    │                  │
    │ ◄─ HelloAck(摘要一致+协议版本) ────┤                     │                  │
    │ ── DispatchRequest(物化字节分帧) ─►│ 身份核对(snapshot/model)│                │
    │ Preparing→Running   │             │ 评估器.evaluate(IEvaluationContext)      │
    │                     │◄─ Progress ──┤ (周期 cancellationRequested 轮询)       │
    │                     │◄─ Heartbeat ─┤                                          │
    │                     │◄─ CheckpointBatch ─┤ → writeBatch(checkpoints/<run>/<seq>)│
    │ ◄─ 进度/心跳转发 ───┤             │                                          │
    │                     │◄─ FinalOutput(EvaluationOutput+绑定身份) ─┤            │
    │ 九步接纳(五元组→反解→aggregateVerdict→envelope.make)                            │
    │ Running→Completed   │             │ 退出(0)→进程回收                          │
    │ ── writeBatch*(结果批次) ─────────────────────────────────────────►│          │
    │ ── finalize(manifest原子发布) ────────────────────────────────────►│          │
    │ ── ResultArchived 事件 ─────────────────────────────────────────────────────►│
    │ 缓存登记(仅Completed) 归档引用释放                                             │
```

### 6.5 通道协议（IPC）

- **载体**：每 worker 一对单向管道（主→worker 命令流、worker→主数据流；`win32/ChannelPair`，字节模式）。
- **帧布局**（`ChannelProtocol.hpp`，协议版本 `IRDCHN/1`）：

```
Frame := magic:"IRDCHN1" | protoVersion:u16 | msgType:u8 | flags:u8 |
         seq:u64 | payloadLen:u32 | TaskIdentityBlock | payload
TaskIdentityBlock := project/branch/revision/run/attempt 五个规范文本（定长域拼接）
——通道消息全部携带任务身份五元组（ARCH §4.1"通道消息全部携带任务身份五元组"）
MsgType := Hello | HelloAck | DispatchRequest | DispatchAccept |
           Progress | Heartbeat | ResultBatch | CheckpointBatch | FinalOutput |
           CancelRequest | CancelAck | PauseRequest | PauseAck |
           ResumeFromCheckpoint | ErrorReport | Shutdown
```

- **消息版本**：protoVersion 主版本失配→握手拒绝（ChannelProtocolError，开发诊断）；次版本追加可选 flags 兼容。
- **输入传输**：DispatchRequest 携带 MaterializedDispatch 字节（物化快照＋模型派发物，§3.3）——大载荷分帧续传（payloadLen 分片＋重组序号）；worker 侧身份核对（snapshotIdentity/modelIdentity 与 DispatchRequest 声明相等，不等拒绝执行——runtime §9.2 同源纪律）。
- **结果分片**：ResultBatch（域载荷片段，execution 视为不透明字节＋批次号）；FinalOutput＝evidence::EvaluationOutput 的 canonical 编码（evidence 编码契约）＋绑定身份；进度与心跳独立小帧。
- **心跳**：worker 每 5 s 发 Heartbeat（默认间隔，实现参数 D-06）；主进程侧读取线程刷新时间戳；连续 3 个间隔无心跳且管道无数据→WorkerHung 判定（§7.4）。
- **乱序与重复**：seq 单调；乱序→按 seq 重排窗口（受限于有界缓冲）；重复/回退→丢弃＋开发诊断（EX-CHANNEL-PROTOCOL-ERROR，EX-CHN-1 用例）；断裂（缺口超时）→通道错误→尝试 Failed。
- **完整性**：帧级靠长度＋解码校验（evidence canonical 解码失败即协议错误）；载荷完整性在 envelope 层（DomainPayload.digest）与物化身份核对层保证；不对本地管道做加密（离线单机，NFR-DEP-03 场景内无对抗者）。

### 6.6 Windows 平台保证（对照 Microsoft Learn 文档口径；不凭记忆超诺——同 project.md §7.2 纪律）

| 操作 | API 与标志 | 文档语义（摘述） | 本文使用与承诺边界 |
| --- | --- | --- | --- |
| 进程创建 | `CreateProcessW`（子进程继承管道句柄；`CREATE_NO_WINDOW`） | 子进程继承可继承句柄；创建即返回 | worker 启动；句柄在主进程侧于启动后关闭继承副本（防泄漏） |
| 进程树管理 | `CreateJobObjectW`＋`AssignProcessToJobObject`（JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE） | 作业对象可容纳进程树；KILL_ON_JOB_CLOSE："与作业关联的最后一个句柄关闭时终止所有进程"（Microsoft Learn：Process and Thread Objects／Job Objects） | 每 worker（含其可能派生的任何子进程）绑独立 Job Scope：主进程崩溃→OS 关闭句柄→worker 树随之终止（**不存在孤儿 worker**） |
| 强制终止 | `TerminateJobObject`（整树）／`TerminateProcess`（单进程） | TerminateProcess："终止指定进程**及其所有线程**"（异步、不通知 DLL）；文档明示不保证 DLL/资源清理 | 仅用于 §4.4 超时与用户强杀路径；终止后任务记 Failed＋EX-FORCE-TERMINATED（不伪装普通失败）；进程内存由 OS 回收 |
| 管道 | `CreateNamedPipeW`/`CreateFileW`（BYTE 模式，`FILE_FLAG_OVERLAPPED` 可选）＋`ReadFile`/`WriteFile` | 阻塞/重叠两种模型；字节流无消息边界 | 帧协议自定界（长度前缀）；读取超时经 OVERLAPPED＋事件（WaitForMultipleObjects 有界等待）——**不无限阻塞**（EX-WKR-3 卡死检测基础） |
| 退出码与等待 | `GetExitCodeProcess`＋`WaitForMultipleObjects` | 退出码在进程对象被等待前保持；STILL_ACTIVE 表示未终止 | 崩溃判定：非约定退出码（§6.4 约定集）或 Windows 异常码（如 0xC0000005 访问违例族）→WorkerCrashed |
| 内存采样 | `GlobalMemoryStatusEx`（系统）＋作业对象内存信息 `QueryInformationJobObject`（JobMemory） | 系统与作业级内存占用查询 | ResourceController 汇总"主进程＋全部工作进程"（主进程经自身 Job 或工作集查询；NFR-PERF-04 口径） |
| 临时目录 | `%TEMP%\ird-worker-\<pid\>-\<run\>-\<attempt\>`（每尝试独立） | — | worker 副产物（评估器中间文件）隔离；尝试终结 best-effort 清理；清理失败→开发诊断（不影响任务终态与已提交检查点——EX-ARC-3 用例）；主进程崩溃残留由下次启动恢复扫描清理（PM-08 通道） |

断电/崩溃语义声明：worker 崩溃只失败当前任务（NFR-REL-02）；主进程崩溃经 Job 限杀联动 worker；**本文不对管道缓冲与作业终止的断电中间态作超出文档的承诺**（残留唯一落点＝归档预留目录无 manifest＝"未完成"，project §4.1 已定义其语义）。

---

## 7. 取消、暂停、继续与强制终止

### 7.1 协作式取消（NFR-PERF-02 逐步协议）

```
requestCancel(task)（任意线程→调度队列）
 1. 取消请求 2 s 内进入 Canceling 并停止派发新批次（调度线程优先处理取消命令——取消命令插队于派发之前）
 2. Running：通道 CancelRequest→worker 评估器在批次/检查点边界轮询 cancellationRequested()
    （IEvaluationContext 查询式取消——evidence §9.1；策略/编译侧经同一信号适配，§3.3）
    普通批次 10 s 内自然结束→CancelAck→Canceled
 3. 超时（批次 10 s 未收敛）→ 强制终止独立工作进程（TerminateJobObject）→ 任务 Failed，
    最近检查点保留可续（T13；EX-FORCE-TERMINATED 显式标记）
 4. 正常用户取消不产生错误诊断（UX-03）；取消失败/异常中断走错误路径
```

信号传递汇总：控制请求→调度线程→通道 CancelRequest→worker 宿主置取消标志→评估器/策略/编译（经 ICancelSignal 适配的各令牌）在协作点观测→CancelAck。Queued/Preparing/Paused 的取消无在途批次（T3/T6/T12），不适用协作窗（ARCH §4.3 取消路径细则）。

### 7.2 暂停（能力门控；R2 承诺、机制 A 期交付）

前置：capability.supportsPause==true（TASK-01 声明；R1 不支持→显式反馈 EX-CAPABILITY-UNSUPPORTED，状态不变）。流程：requestPause→调度线程置暂停意图→worker 在下一检查点边界（或批次边界，按 granularity）写出检查点（§8.1）→PauseAck（携 CheckpointId）→主进程校验检查点清单→TaskRecord 置 Paused（暂停确认边界＝检查点写出并确认——AT-35"暂停确认边界明确"）→worker 退出回收（释放内存预算；检查点在盘承载续跑状态）。

### 7.3 继续（新尝试）

requestResume→检查点兼容判定（evidence judgeCheckpointCompatibility：evaluatorKey/sliceId/契约版本/检查点格式/完整性——判定归 evidence，调度归 execution）→通过→**AttemptId+1**（旧 attempt 标 Superseded）→新 worker 以 ResumeFromCheckpoint 派发→续跑统计自检查点累计（**继续不重复统计**——AT-35）→Running。**不改变输入快照**（AT-35：输入冻结不变——恢复运行仍绑定原 snapshotId/sliceId）。不兼容→拒绝继续＋诊断（EX-CHECKPOINT-INCOMPATIBLE），任务保持 Paused（用户可取消或等待配置回退）。

### 7.4 四类行为对照表

| 维度 | 协作取消 | 暂停 | 继续 | 强制终止 |
| --- | --- | --- | --- | --- |
| 信号传递 | §7.1（查询式轮询） | PauseRequest→检查点边界 | ResumeFromCheckpoint 派发 | TerminateJobObject（不经协作点） |
| 检查点时机 | 取消前最近检查点保留（不再新写——除非在途批次恰经检查点边界） | 边界写出＋确认 | 加载既有检查点 | 保留最近已提交检查点（强杀不打断已 finalize 的检查点清单；未完成检查点批次作废） |
| 状态转换 | →Canceling→Canceled（超时→Failed） | →Paused | Paused→Running（新 Attempt） | →Failed（EX-FORCE-TERMINATED） |
| 资源释放 | worker 回收＋归档 abandon(Canceled) | worker 回收（检查点在盘） | 新 worker＋预算复检 | 进程树终止＋句柄关闭＋临时目录 best-effort 清理＋归档 abandon(ForceTerminated) |
| 结果资格 | 无正式结果（outcome=Canceled＋NotApplicable；partialData 可有 reusable=false） | 无结果（Paused≠Completed） | 完成后按正常接纳 | 无正式结果（Failed；不伪装普通失败——诊断显式区分） |
| 诊断 | 正常取消无错误诊断（UX-03） | 不支持时显式反馈 | 不兼容时拒绝＋建议 | EX-FORCE-TERMINATED（用户可见"已强制终止，检查点保留"） |
| 用户可继续条件 | 重跑＝新任务（可从保留检查点续，新身份链） | 任意时刻（兼容判定通过） | — | 同取消 |
| worker 无响应 | 心跳失联（3×间隔）→按强杀路径（T13） | 同左（暂停请求超时→诊断，状态保持 Running） | — | 立即 |
| UI 关闭 | PM-03"协作取消"分支：取消命令照常；"等待"分支＝排空（§7.5） | 保持 Paused 跨会话？（**否**——主进程退出即会话终结，任务转 Interrupted 语义，检查点在盘可续） | — | 关闭确认对话框不提供强杀选项（强杀归任务操作，不归关闭流程） |
| 应用崩溃 | Job 联动终止→重启后 Interrupted（归档预留未终结） | 同左（检查点保留可续） | — | 同左 |

### 7.5 UI 关闭与应用崩溃

- **UI 关闭（主进程存活）**：PM-03 统一确认对话框二选——"等待"＝workflow 调 scheduler.shutdown(DrainPolicy::CancelQueuedAndWait)：停止派发新任务（取消排队任务；若需"保留至会话终结"语义，由 workflow 侧选用 KeepQueuedTerminate）、在途运行执行至归档完成（存储上下文引用协议——project §9.7；execution 的终态路径全部释放归档引用）；"协作取消"＝对任务清单逐/批量 requestCancel 后走取消协议。**UI 关闭不等于归档完成**；执行侧保证关闭流程**无永久等待**：排空有界（取消协议 10 s＋归档有界重试＋abandon 兜底；超阈值由 L5 关闭控制器强制 abandonAll(ForceTerminated)——project §9.7 同口径，残留记开发诊断）。〔v0.2 对齐：原文引用的 `DrainPolicy::WaitForInFlight` 不在 §10.1 枚举中，按枚举实际值更正，同时销账 ui.md CF-2/P-UI-3〕
- **应用崩溃**：Job 限杀联动 worker 终止；重启后恢复扫描（project ②端口/RecoveryReport）发现无 manifest 的归档预留→execution 重建 Interrupted 任务条目（EX-TASK-INTERRUPTED，"已中断可重跑"——NFR-REL-03/PM-08）；检查点目录兼容判定后可续跑（新任务）。主进程崩溃期间不可能有迟到结果（worker 已被联动终止，通道已亡）。

### 7.6 故障边界与恢复动作矩阵

| 故障边界 | 检测点 | 波及范围（隔离边界） | 恢复动作 | 任务终态 | 磁盘残留处置 |
| --- | --- | --- | --- | --- | --- |
| worker 进程崩溃（访问违例/异常退出码） | 进程等待返回＋退出码分类 | 仅当前任务（NFR-REL-02）；同池其他 worker 不受影响 | AttemptRecord 记崩溃；归档 abandon(Failed)；EX-WORKER-CRASHED | Failed（**不自动重试**，D-09） | 最近检查点保留；worker 临时目录 best-effort 清理 |
| worker 卡死（心跳失联） | 心跳时间戳超阈（3×间隔） | 仅当前任务 | 强杀路径（T13）＋EX-WORKER-HUNG | Failed（EX-FORCE-TERMINATED） | 同上 |
| 通道帧损坏/序号断裂 | 帧解码失败/seq 缺口超时 | 仅当前任务 | 尝试 Failed＋EX-CHANNEL-PROTOCOL-ERROR；worker 强杀回收 | Failed | 同上 |
| 评估器失败 | ErrorReport 消息（退出码 12） | 仅当前任务 | 域诊断透传＋abandon(Failed) | Failed | 同上 |
| 主进程崩溃 | （OS 视角；Job 限杀联动） | 全部运行中任务＋worker 树 | 重启恢复扫描：无 manifest 预留→Interrupted；检查点兼容可续跑 | Interrupted（恢复期指派） | 预留目录由 project 恢复诊断列出；abandon 由恢复流程执行 |
| 归档批次写失败/磁盘不足 | ArchiveStatus.error | 任务结果落盘（**不波及任务终态与主进程**） | 有界重试（2 次）→abandon＋EX-ARCHIVE-FAILED | 保持 Completed（archivePhase=ArchiveFailed） | 批次残留＝"未完成归档"（project §4.1 语义） |
| 存储上下文失权（迟到写） | begin 的 StoreError(context-closed/lock-held-by-other) | 该迟到归档 | 重新取权→成功照常／失败拒绝＋EX-ARCHIVE-AUTHORITY-LOST | 不变 | 无新写入 |
| 检查点损坏/不兼容 | 恢复期摘要/兼容判定 | 该检查点（不动任务历史） | 废弃标记＋诊断；可退更早检查点或全量重跑 | 不变（恢复请求被拒） | 原文件保留（只读废弃） |
| 通道迟到结果（项目切换/重复投递） | 九步接纳 | 无（被拒于接纳边界） | 幂等重投处理或拒绝＋开发诊断 | 不变 | 不写入任何新位置 |
| 资源预算超限 | 内存采样 | 新任务派发（不波及运行中） | 节流→停派发→资源不足诊断 | 排队任务保持 Queued | 无 |

---

## 8. 检查点、缓存与恢复

### 8.1 检查点契约

**CheckpointRecord（清单/身份数据；磁盘编址 `checkpoints/<run-id>/<seq>/`，project §4.1——批次只增＋manifest 发布＝"完整"；无 manifest＝不作为缓存命中，CON-04）**：

| 字段 | 类型 | 必填 | 默认 | 版本 | 身份语义 | 可变性 | 所有权 | 生命周期 | 合法/非法 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `checkpointId` | CheckpointId{run, seq} | 是 | — | v1 | 主键 | 不可变 | execution（身份）/project（编址） | 写出→淘汰/废弃 | seq≥1；run 为登记内运行 |
| `task`（五元组） | core::TaskIdentity | 是 | — | v1 | 产出该检查点的运行与尝试 | 不可变 | execution | 同上 | 有效性同 core TaskIdentity |
| `snapshotId/sliceId` | core::ContentIdentity | 是 | — | v1 | 输入绑定（含 evaluator contractVersion 与 Environment 版本要素——经 sliceId） | 不可变 | 承载 execution／计算 evidence | 同上 | 非零；与登记扩展字段一致 |
| `evaluatorKey`＋`evaluatorContractVersion` | EvaluationKey＋u32 | 是 | — | v1 | 算法与契约版本绑定 | 不可变 | 承载 execution／权威 evidence | 同上 | 已注册键 |
| `policyIdentity/nameMapIdentity` | core::ContentIdentity | 是 | — | v1 | 策略与名称映射绑定（CON-06） | 不可变 | 值传递 | 同上 | 非零 |
| `checkpointFormatVersion` | u32 | 是 | 1 | v1 | 检查点载荷格式版本（域中间状态编码归评估器域；execution 侧信封版本） | 不可变 | execution（信封）＋域（载荷） | 同上 | ≥1；失配→不兼容 |
| `randomSeed` | u64 | 是 | 0（无随机性） | v1 | 确定性复现要素（NFR-COR-02） | 不可变 | 承载（来源＝AnalysisConfiguration） | 同上 | — |
| `threadCount` | u32 | 是 | 1 | v1 | 并行配置（复现要素） | 不可变 | 同上 | 同上 | ≥1 |
| `completedUnits/totalUnits` | u64 | 是 | — | v1 | 已完成样本/分段（续跑统计基点——继续不重复统计） | 不可变 | 域申报（execution 承载） | 同上 | completed≤total |
| `intermediateStateRef` | 内容寻址引用（批次文件清单＋摘要） | 是 | — | v1 | 中间状态载荷（对 execution 不透明） | 批次只增 | execution 写出/project 编址 | 同上 | 摘要齐全 |
| `payloadDigest` | core::Digest256 | 是 | — | v1 | 校验摘要（全部批次文件 SHA-256 复合） | 不可变 | execution（经 core ContentDigester） | 同上 | 非零；不符→Corrupt |
| `createdAtUtc` | time_point | 是 | — | v1 | 创建时间 | 不可变 | execution | 同上 | — |
| `resumable` | bool | 是 | true | v1 | 可恢复能力（域声明中间状态完整性） | 不可变 | 域申报 | 同上 | false 的检查点仅供诊断 |
| `recordVersion` | u32 | 是 | 1 | v1 | 本记录 schema 版本 | 不可变 | execution | 同上 | 主版本失配→不兼容 |

**职责与规则**：

- **写出位置**：正式发布（manifest finalize）由 project 负责（`checkpoints/` 写入口，ARCH §6.8 写入口清单）；execution 负责生成（域评估器经通道 CheckpointBatch 回传中间状态）、验证（摘要与清单校验）、恢复调度（兼容判定＋新 Attempt 派发）。**临时检查点与正式检查点的区别**＝批次文件已写入但 manifest 未发布（临时/在途，不作缓存命中、可被同 seq 重写）vs manifest 已原子发布（正式、完整、幂等可续）——project D-13 同构。
- **损坏检查点**：装载/恢复期摘要校验失败→EX-CHECKPOINT-CORRUPT＋诊断（定位文件）；该检查点废弃（不删除原文件——只读登记废弃标记），任务可从更早检查点或全量重跑。
- **不兼容检查点**：`judgeCheckpointCompatibility`（evidence）判 Incompatible（evaluatorKey/sliceId/契约版本/格式版本任一失配）→拒绝恢复＋EX-CHECKPOINT-INCOMPATIBLE＋reasons；**旧版本检查点不迁移**（不承诺向后兼容升级——保守方向：宁可重算不可错续；登记 P-EX-9）。
- **恢复后新 AttemptId**：是（T11；恢复失败〔加载错/域报告不可续〕保留原检查点（不删除、不降级），任务回 Paused/Failed＋诊断，可再试或全量重跑）。
- **检查点与最终结果的区别**（冻结）：检查点可恢复**不等于**任务已成功——恢复成功后仍须走完整接纳（五元组＋envelope＋归档）；检查点不进正式报告、不作正式缓存命中（CON-04——仅 `judgeCheckpointCompatibility` 的 Resumeable 语义，与上次任务成败无关——evidence §8.2）。
- **清理失败不破坏已提交检查点**：批次/临时清理失败→开发诊断；已 finalize 的 manifest 与批次不动（project 只增语义）。

**检查点写出与恢复流程图**：

```
【写出】  worker 评估器到达检查点边界（granularity 声明）
   → CheckpointBatch{seq, 中间状态片段} ──通道(五元组头)──► 主进程调度线程
   → 校验（五元组核对＋片段摘要）→ 归档端口 writeBatch（checkpoints/<run>/<seq>/，批次只增）
   → 全部片段齐 → 组装 CheckpointRecord → finalize（manifest 原子发布＝正式检查点）
   → PauseAck（若暂停请求在途）/ 登记最近检查点（AttemptRecord.checkpoint）
【恢复】  requestResume / resumeFrom
   → 查找候选（run 或任务链最近）→ judgeCheckpointCompatibility(evidence 纯判定)
        ├─ Resumeable → AttemptId+1 → 新 worker ResumeFromCheckpoint（携带检查点引用+已完成统计基点）
        │    → 续跑（统计累计，不重复）→ 正常终结路径（取消/完成/失败同前）
        └─ Incompatible/Corrupt → 拒绝+诊断（原检查点保留）
```

### 8.2 缓存治理（四类区分与调度边界）

**四类复用对象严格区分（任务约束§五.9；evidence §8.2 对端）**：

| 类别 | 内容 | 判定权威 | execution 职责 |
| --- | --- | --- | --- |
| 完整结果缓存 | finalize 的 ResultEnvelope 产物（按 sliceId＋模式＋契约版本＋Profile 身份编址） | evidence `judgeCacheHit`（FullHit/DiagnosticOnly/Incompatible） | 查找（派发前）、写入（仅 Completed＋finalize 后）、淘汰、预算 |
| 部分诊断缓存 | 未 finalize/取消/失败批次（DiagnosticOnly 可读） | 同上（DiagnosticOnly≠正式命中） | 保留策略（短周期，实现参数）；不得作为命中上报 |
| 检查点 | §8.1 | evidence `judgeCheckpointCompatibility` | 写出/查找/恢复调度/清理 |
| 运行时模型缓存 | 编译产物（WC/DWC 层） | runtime `judgeCompileCacheCompatibility`（经 ICompileCacheJudge 注入；FullReuse/WorkCellOnlyReuse/Incompatible） | 存储与淘汰；Preparing 段消费判定决定重编/复用 |
| 任务状态记录 | TaskRecord/RunRegistry | —（会话内存态） | §5.4；不入缓存语义 |

**治理规则**：

| 规则 | 设计 | 依据 |
| --- | --- | --- |
| 缓存查找 | 派发前（T2 前）：以 (requestedMode, requestSliceId, contractVersion, profileIdentity) 组查询→evidence 判定；FullHit→**短路径**：不派发 worker，直接以缓存产物构造结果引用＋事件（TaskRecord 走 Queued→Preparing→Completed 快路径，attempt=1、worker=空、runDir 仍登记新 RunId——缓存命中也是一个可追溯运行）；DiagnosticOnly/Incompatible→正常派发 | CON-04/OPT-06 |
| 命中前检查 | 命中产物装载时完整性校验（摘要）；Quick 产物对 Verified 请求→Incompatible（mode-mismatch，**模式不隐式升降级**——evidence D-13）；**缓存命中≠当前结果**：命中只声明"对该键可复用"，当前性由 evidence 另行计算（Superseded 的历史缓存照样可命中同键请求——两者语义不同，runtime §9.4 同口径） | CON-04/05 |
| 失效 | 无主动失效：键含全部身份要素（sliceId 已含策略/种子/线程/样本/版本——evidence D-01/D-11），内容变化→新键→自然不命中；显式清理（用户/预算淘汰）除外 | SA-07 |
| 写入 | 仅 outcome==Completed 且归档 finalize 成功后登记缓存条目（**失败/取消/中断结果禁止进入正式成功缓存**——CON-04 的执行侧落点：写入门槛＋evidence 判定双保险）；部分批次仅入部分诊断缓存 | CON-04/TASK-02 |
| 并发命中 | 同键并发请求：首个执行、后续登记为等待该运行（共享 RunId 的完成事件；不重复派发——EX-RES-1 的缓存面） | 实现决策 D-10 |
| 内容冲突 | 写入时发现同键不同摘要→保留既有＋开发诊断（不覆盖） | project D-14 同精神 |
| 淘汰 | LRU＋总字节预算（默认 2 GiB，可配，实现参数）；模型缓存与结果缓存分账；淘汰只删缓存条目（不动已归档 run） | NFR-PERF-04 |
| 磁盘不足 | 写入失败→诊断（EX-ARCHIVE-FAILED 细节含 disk-full）＋跳过缓存写入（**不使任务失败**——缓存是优化非正确性要件）＋触发淘汰 | 保守方向 |
| 版本升级 | 契约/编码/后端版本入键（Environment 条目）→旧条目自然 Incompatible→按淘汰清理；无迁移 | CON-04 |

---

## 9. RunRegistry、迟到结果与归档

### 9.1 登记记录全字段（RunRegistration）

| 字段 | 类型 | 必填 | 说明（承接 project §13.2 冻结义务之一：登记项 runDir/runKind） |
| --- | --- | --- | --- |
| `identity` | core::TaskIdentity | 是 | 完整五元组（当前 attempt）——**接纳判定的唯一核对基准** |
| `task` | TaskId | 是 | 关联任务 |
| `evaluatorKey`＋`contractVersion` | EvaluationKey＋u32 | 是 | 评估键与契约版本（透传 RunManifest，project §4.4.7） |
| `snapshotId/sliceId/inputBaselineId` | core::ContentIdentity | 是 | 输入绑定（扩展核对字段） |
| `policyIdentity/nameMapIdentity` | core::ContentIdentity | 是 | CON-06 绑定（名称反解依据） |
| `runDir` | std::filesystem::path | 是 | **归档目标**：任务快照绑定修订的 `results/<run-id>/`——接纳后归档位置一律取自本记录（**不重新推导**——ARCH §4.5 A8 原文） |
| `runKind` | string | 是 | 任务类型 token（透传 RunManifest） |
| `allowedResultKinds` | enum 集 | 是 | {FinalEnvelope, ResultBatch, CheckpointBatch, Progress, PartialDataOnTerminate}——按 mode 与阶段限定（Preview 运行不登记本表） |
| `registeredAtUtc` | time_point | 是 | 登记时间 |
| `currentState` | core::TaskState | 是 | 登记时刻任务状态镜像（随任务状态机同步） |
| `terminationCause` | std::optional\<TerminationCause\> | 否 | 终结原因（接纳语义裁量的输入——已 Canceled/Failed 的登记拒绝 FinalEnvelope） |
| `supersededAttempts` | std::vector\<core::AttemptId\> | 是（可空） | 被取代尝试（迟到判定：携带其中之一→StaleAttempt 拒绝） |
| `resourceContext` | {worker 快照引用, 归档会话引用, 存储上下文句柄} | 是 | 资源上下文（排空协议的引用持有） |
| `registryVersion` | u32 | 是 | 登记表 schema 版本（当前 1） |

### 9.2 接纳程序（九步，A8 两段式；一次性判定）

```
admitResult(Arrival)（调度线程串行）：
 1. 以 Arrival.identity.run 查 RunRegistry → 未命中 → 拒绝（RegistryUnknownRun；丢弃＋开发诊断——
    含跨项目迟到事件，绝不写入当前/新项目 results/，AT-10）
 2. 核对完整身份：Arrival 五元组 vs 登记记录 identity
    任一字段（projectId/branchId/revisionId/runId/attemptId）不符 → 拒绝（RegistryMismatch＋开发诊断）
 3. （防未知）上两步共同覆盖：不存在"未登记而接纳"路径
 4. attempt ∈ supersededAttempts → 拒绝（StaleAttempt＋开发诊断）
 5. 第二道绑定核对（开发级）：Arrival 携带的 snapshotId/sliceId/evaluatorKey/policyIdentity
    vs 登记扩展字段——不符→拒绝＋开发诊断（EX-REGISTRY-MISMATCH；防通道串扰）
 6. 结果类型 ∈ allowedResultKinds → 否则拒绝（协议错误）
 7. 终结语义检查：terminationCause 已为 Canceled/Failed/ForceTerminated → 拒绝 FinalEnvelope
    （取消/失败后不得产生完整成功结果——TASK-02 执行侧落点）
 8. 名称反解（CON-06）：载荷内运行时名称 → ObjectId（经 INameResolverAdapter，以登记的
    nameMapIdentity 绑定映射——绝不用当前映射，runtime §7.5/§9.3）
 9. 组装：aggregateVerdict（evidence 五级汇总）→ ResultEnvelope::make（构造边界校验，SA-13）
    → validateCombination 二次校验（纯函数复用）
    → 通过 → T8（Running→Completed）→ 归档触发（§9.5）；make 失败 → Running→Failed＋诊断
```

**九步之后**（当前性交接）：归档完成后发布 ResultArchived 事件；evidence 的 ResultCurrentness 另行计算（结果切片内容身份 vs 当前 HEAD 切片内容身份，CON-05）——**HEAD 变化仅用于当前性判定，不参与接纳判定**（即使运行期间 HEAD 已前进，如仅电机成本变更不影响运动学切片，结果仍被接纳并可 Current——ARCH §4.5 原文正例）。

### 9.3 迟到结果处置规则

| 问题 | 规则 |
| --- | --- |
| 项目切换后旧运行继续 | 存储上下文与界面会话分离（A7）：切到项目 B 后，A 的在途运行持有的归档引用使 A 上下文保持存活至归档完成（project §9.7）；完成事件迟到到达主进程→九步接纳（一次性判定，无"先按当前会话判失配丢弃、再按原项目接纳"的两段歧义——ARCH S7）→归档到登记的 A 原 runDir |
| UI 关闭与存储上下文释放分离 | 同上：UI 会话结束≠上下文结束；execution 的终结路径（成功 finalize／取消/失败/强杀 abandon）是引用清零的唯一驱动 |
| 已释放写上下文时的迟到写请求 | 防御路径（正常不应发生——execution 持引用期间上下文不 Closed）：归档 begin 遇 StoreError(context-closed)→**先尝试重新取得写权限**（重新 open writable）；成功→照常归档（manifest 记原五元组）；失败（锁被他持/介质只读）→拒绝归档＋EX-ARCHIVE-AUTHORITY-LOST 诊断（结果不落盘，用户可重跑；开发诊断记录丢失细节）——ARCH §6.8 A7 原文 |
| 迟到结果拒绝是否留诊断 | 是：开发级诊断（EX-REGISTRY-UNKNOWN-RUN/MISMATCH/STALE-ATTEMPT）＋丢弃计数（观测通道健康） |
| 重投递 | 同 (runId, attemptId) 的 FinalOutput 重投递：九步重新执行→若已 Completed 且归档 manifest 摘要一致→**幂等成功**（不重写、不报错）；摘要不一致→archive-conflict 拒绝（project D-14）；**完成事件重复投递无次数上限**（每次幂等处理；project §13.2 冻结义务之二）——批次级重投递：目标已存在且摘要一致→跳过；不一致→冲突拒绝 |
| 内容相同 vs 内容冲突 | 如上：摘要一致＝幂等/跳过；不一致＝冲突拒绝＋诊断（不覆盖、不静默） |
| 归档失败/磁盘不足/进程异常如何结束责任 | writeBatch/finalize 失败→ArchiveStatus.error→有界自动重试（默认 2 次、退避 1 s/4 s，实现参数 D-11）→仍失败→abandon(ArchiveEndReason 对应 Completed 外的终结因)＋EX-ARCHIVE-FAILED 诊断（透传 StoreError 细节）＋**任务终态不变**（Completed 保持；archivePhase=ArchiveFailed）——责任终结＝finalize 成功或 abandon（project §10.1"运行完成≠归档完成"）；进程异常（主进程崩溃）→恢复扫描路径（Interrupted） |

### 9.4 项目切换后的迟到结果归档流程图（S7 execution 侧）

```
项目A: 任务提交(t1) → 登记(r1: 五元组+runDir=A/results/<r1>+扩展字段) → 归档预留 → worker 运行
   │ 用户切换项目（PM-03：草稿三选＋任务二选[等待/协作取消]）
   ▼
UI 会话 ──► 项目B；项目A 存储上下文保持（r1 归档引用未释放；Draining 由关闭流程触发或保持 Active）
 r1 完成事件迟到（worker→主进程通道）
   ▼
九步接纳（一次性判定）：
   ├─ 无匹配登记（跨项目迟到事件等）→ 丢弃＋开发诊断 ──► 绝不写入项目B 的 results/（AT-10）
   └─ 匹配（属项目A 的 r1，字段逐项一致）
        ▼
 归档：A 上下文 archive.begin(task=r1五元组, runDir=登记记录值)  ── 上下文已 Closed？
        │ Active/Draining(预留已建立) → writeBatch*（分批）→ finalize（manifest 原子发布）
        │ Closed → 先重新取得写权限（A7 防御）：成功→照常；失败→拒绝归档＋诊断
        ▼
 ResultArchived 事件 → 当前性独立判定（evidence：r1 切片内容身份 vs A 当前 HEAD 切片内容身份）
        ├─ 输入未变（仅电机成本变更）→ Current（可供后续消费）
        └─ 输入已变 → Superseded（保留为原快照历史证据，CON-02）
        ▼
 无论何种当前性：不进入当前界面会话、不成为项目B 的"当前结果"（TASK-03）
 r1 引用释放 → 项目A pending==0 → 存储上下文 Closed、写锁释放（project §9.7）
```

### 9.5 与 project 的归档协作（project §10.1 对端；P-PR-4 冻结）

| 要素 | execution 侧设计 |
| --- | --- |
| 调用线程 | **调度线程**调用归档端口（P-PR-4 冻结：project 内部经 writer 互斥与命令服务/草稿落盘串行化——project §9.8；execution 不并发调用、不另持项目锁、不以 PID/心跳/UI 状态为权限依据） |
| 派发期预留 | Preparing 段即 `archive.begin`（空会话）：建立 `results/<run-id>/` 归档预留——作用：①权限/白名单前置校验（writable=false→Preparing→Failed，PM-07）；②崩溃可观测性（无 manifest 的预留＝"已中断"判据，§5.1/§7.5） |
| 再验证 | project 接收的是**已核验**存储请求（九步通过）但仍验证项目上下文与写权限（project §5.6 begin 校验：task.project==projectId、runDir 白名单、幂等比对）——双层防线 |
| 分批与最终发布 | writeBatch（批次只增）≠ finalize（manifest 原子发布＝"完整"）——**唯一完整标志**；消费者只认 manifest 在的 run（project D-13） |
| 事务边界 | 检查点批次/结果批次/最终 envelope 的**批次写入各自独立**（无跨批次回滚——批次只增）；"最终完整发布"＝finalize 一次原子动作；无多目录联合事务（不越权造第二事务协议） |
| 写入失败 | §9.3 末行（有界重试→abandon＋诊断；任务终态不变） |
| UI 关闭≠归档完成 | §7.5（排空协议） |
| 无永久等待 | abandon 全路径覆盖＋L5 超阈值强制 abandonAll 兜底（project §9.7 同口径） |
| 只读/锁竞争/介质只读区分 | 消费 StoreError 码：`lock-held-by-other`/`media-read-only`/`access-denited`→不同稳定诊断（PM-07 呈现口径归 ui/diagnostics）；只读在提交/预留期即拦截（不进入运行） |
| 迟到写请求在上下文释放后 | §9.3 防御路径（重新取得权限→失败拒绝） |
| 强杀后的 abandon 调用时机（project §13.2 冻结义务之三） | ①运行期强杀（T13）：进程树终止确认（WaitForMultipleObjects 返回）后立即 abandon(ForceTerminated)；②恢复期（T14）：恢复扫描发现预留→重建 Interrupted 后由恢复流程 abandon；③关闭排空超阈值：L5 强制 abandonAll。**不存在无 abandon 的终结路径**（project §9.7 引用清零保证） |

---

## 10. 公共接口详细设计

通用约定：签名为实现建议（自洽契约片段，未编译验证）；除注明外方法线程安全（并发只读）；控制/提交通道任意线程进入、内部转调度线程串行；错误一律 `ExecutionError`（稳定 code＋开发诊断 detail）或结构化 Ack（用户可见诊断经 core::DiagnosticRecord 承载）。

### 10.1 ITaskScheduler（提交/查询/预算/排空）

```cpp
struct ResourceBudget {
    double maxMemoryRatio = 0.70;        // 物理内存占比上限（NFR-PERF-04 上游值）
    double throttleRatio  = 0.65;        // 节流阈值（实现参数 D-05）
    std::uint32_t maxWorkers = 0;        // 0=自动（逻辑核-1，下限 1）
    std::uint32_t maxConcurrentTasks = 0;// 0=自动（min(4, 核/2)）
    std::uint32_t maxTasksPerProject = 2;
    std::size_t   queueCapacity = 256;
};
struct SubmitResult {
    bool accepted = false;
    std::optional<TaskId> task;                 // 拒绝时空
    std::vector<core::DiagnosticRecord> diagnostics;   // 拒绝原因（ERR-01 字段完整）
};
enum class DrainPolicy { CancelQueuedAndWait, KeepQueuedTerminate };

class ITaskScheduler {
public:
    virtual ~ITaskScheduler() = default;
    // 前置：快照已冻结（evidence builder 产物）；评估器已注册；调度器未 shutdown
    // 后置：受理→TaskId 分配→Queued；拒绝→无 TaskRecord（§5.1 注）
    // 错误：ExecutionError(SubmissionRejected/StaleSnapshot/StoreReadOnly/ResourceInsufficient)
    //       ——结构化拒绝优先经 SubmitResult.diagnostics（不抛），抛出仅限调用方违约
    // 副作用：事件 TaskStatusChanged(Queued)（受理时）
    virtual SubmitResult submit(TaskSubmission&&) = 0;
    virtual std::optional<TaskSnapshot> tryTask(TaskId) const noexcept = 0;      // 并发只读快照
    virtual std::vector<TaskSnapshot> tasksByProject(core::ProjectId) const = 0; // PM-03 任务清单数据
    virtual void setResourceBudget(const ResourceBudget&) = 0;   // 运行期可调；不影响任务身份（§6.1）
    virtual void shutdown(DrainPolicy) = 0;                      // §7.5 排空；幂等
    virtual bool drained() const noexcept = 0;                   // shutdown 完成查询（无永久等待）
};
```

| 合法调用 | 非法调用 |
| --- | --- |
| 任意线程 submit/tryTask/tasksByProject；shutdown 后 submit→ExecutionError(ContextClosed)；同快照重复 submit（得到两个独立任务） | 提交未冻结快照（evidence builder 中间态）→SubmissionRejected；Preview 任务的 submit 要求 capability.archiveless 显式声明 |

调用示例：`auto r = scheduler.submit({.snapshot=snap, .evaluatorKey="kin.batch-ik", .mode=core::EvaluationMode::Verified, .priority=TaskPriority::Interactive});`

### 10.2 ITaskController（取消/暂停/继续/强杀/进度/释放）

```cpp
struct CancelAck  { bool accepted; std::optional<core::DiagnosticRecord> feedback; };  // 正常取消无错误诊断
struct StatusAck  { bool accepted; core::TaskState currentState;
                    std::optional<core::DiagnosticRecord> feedback; };  // 不支持暂停等显式反馈

class ITaskController {
public:
    virtual ~ITaskController() = default;
    // 前置：任务存在且非终态（终态→accepted=false＋InvalidState 反馈；重复取消幂等 ack）
    // 后置：状态机 T3/T6/T8/T12/T13 路径启动；2 s/10 s 协议由调度线程执行
    virtual CancelAck requestCancel(TaskId) = 0;
    // 前置：state==Running ∧ supportsPause；否则显式反馈（EX-CAPABILITY-UNSUPPORTED/InvalidState）
    // 后置：检查点边界确认后 Paused（异步——返回时可能仍 Running；状态经事件观察）
    virtual StatusAck requestPause(TaskId) = 0;
    // 前置：state==Paused；后置：兼容判定通过→新 AttemptId→Running；不通过→保持 Paused＋诊断
    virtual StatusAck requestResume(TaskId) = 0;
    // 前置：任务存在；后置：进程树终止→Failed(EX-FORCE-TERMINATED)；终态任务→幂等 no-op
    virtual StatusAck requestForceTerminate(TaskId) = 0;
    virtual std::optional<ProgressReport> progress(TaskId) const noexcept = 0;
    // 前置：任务处于终态且过保留窗口策略（强制释放终态任务资源——内存态回收；磁盘归档不动）
    virtual void releaseResources(TaskId) = 0;
};
```

### 10.3 IWorkerSupervisor

```cpp
struct WorkerStatus { WorkerId id; std::uint32_t pid; core::RunId currentRun;
                      enum class Phase { Idle, Booting, Running, Draining, Dead } phase;
                      std::chrono::system_clock::time_point lastHeartbeatUtc;
                      std::uint64_t tasksServed = 0; };
struct WorkerAssignment { core::TaskIdentity identity; evidence::EvaluationKey evaluatorKey;
                          MaterializedDispatch dispatch; HeartbeatPolicy heartbeat;
                          std::optional<CheckpointId> resumeFrom; ResourceBudget workerShare; };
struct WorkerLaunchResult { bool ok; WorkerId worker; std::vector<core::DiagnosticRecord> diagnostics; };

class IWorkerSupervisor {
public:
    virtual ~IWorkerSupervisor() = default;
    // 前置：进程可执行可达（与主进程同版本基线——部署同目录）；预算允许
    // 后置：进程启动＋Job Scope 绑定＋握手完成（manifest 摘要一致）；失败→ok=false（Preparing→Failed 由调度器处置）
    // 线程：调度线程调用；status/list 并发只读
    virtual WorkerLaunchResult launch(const WorkerAssignment&) = 0;
    virtual void requestCooperativeCancel(WorkerId) = 0;   // 通道 CancelRequest
    virtual void terminateForce(WorkerId, TerminationCause) = 0;  // TerminateJobObject；句柄关闭＋ reap
    virtual WorkerStatus status(WorkerId) const = 0;
    virtual std::vector<WorkerStatus> list() const = 0;    // ResourceController 汇总消费
};
```

### 10.4 EventBus（core IDomainEventBus 参考实现）与事件发布

`EventBus.hpp` 提供 `DomainEventBusImpl`：`publish` 线程安全；**同一发布者 FIFO**（调度线程发布的 TaskStatusChanged 序＝状态机转移序；ResultArchived 在归档 finalize 后）；`subscribe` 返回 RAII 句柄、退订幂等不与投递死锁；事件不持久化（core D-09）。execution 发布的事件：`TaskStatusChanged{task:五元组, newState}`（每次转移；进度**不**入事件——core Events 契约）与 `ResultArchived{task}`（finalize 成功后）。UI 线程 marshaling 投递由 ui 侧扩展/包装（§13 交接；execution 不引 Qt）。

### 10.5 IRunRegistry / IResultAdmission

```cpp
struct ArrivalEnvelope {                      // 到达的结果（通道 FinalOutput 或重投递）
    core::TaskIdentity identity;              // 五元组（自报——以登记核对为准）
    core::ContentIdentity snapshotId, sliceId;// 绑定自报（第二道核对）
    evidence::EvaluationKey evaluatorKey; std::uint32_t contractVersion;
    core::ContentIdentity policyIdentity, nameMapIdentity;
    ResultKind kind;                          // FinalEnvelope | ResultBatch | CheckpointBatch | ...
    std::vector<std::uint8_t> payloadCanon;   // EvaluationOutput canonical（FinalEnvelope 时）
};
struct AdmissionDecision {
    enum class Verdict { Admitted, DuplicateIgnored, Rejected } verdict;
    enum class RejectReason { UnknownRun, IdentityMismatch, StaleAttempt, BindingMismatch,
                              KindNotAllowed, AlreadyTerminated } reason;   // Rejected 时有效
    std::vector<core::DiagnosticRecord> diagnostics;   // 拒绝→开发级诊断
};
class IRunRegistry {
public:
    virtual ~IRunRegistry() = default;
    // 派发时登记（§4.3）；重试/继续→appendAttempt（旧 attempt 移入 superseded）
    virtual RunRegistration& registerRun(TaskId, const RegistrationInput&) = 0;
    virtual void appendAttempt(core::RunId, core::AttemptId) = 0;   // 旧 attempt 标 Superseded
    // 一次性判定（九步之 1~7；纯查表＋比对，无副作用）
    virtual AdmissionDecision classifyArrival(const ArrivalEnvelope&) const = 0;
    virtual std::optional<RunRegistration> tryRun(core::RunId) const noexcept = 0;
    virtual void markTerminated(core::RunId, TerminationCause) = 0;
};
class IResultAdmission {
public:
    virtual ~IResultAdmission() = default;
    // 九步之 8~9＋归档触发＋缓存登记＋事件；调度线程串行调用
    // 后置：Admitted→envelope 构造校验通过→T8/归档；构造失败→T9＋诊断
    virtual AdmissionOutcome admitResult(ArrivalEnvelope&&) = 0;
    virtual ArchivePhase archivePhase(core::RunId) const noexcept = 0;
};
```

### 10.6 ICheckpointCoordinator

```cpp
class ICheckpointCoordinator {
public:
    virtual ~ICheckpointCoordinator() = default;
    // 写出：批次校验→归档端口 writeBatch（checkpoints/<run>/<seq>/）→finalize（manifest 发布）
    // 前置：五元组核对通过；后置：正式检查点（或批次残留＋诊断）；失败不破坏已提交检查点
    virtual CheckpointWriteResult writeCheckpoint(const CheckpointWriteRequest&) = 0;
    // 恢复：候选查找→evidence 兼容判定→通过则返回恢复规格（供新 Attempt 派发）
    // 前置：任务 Paused/终态（续跑）；失败→原检查点保留＋诊断
    virtual CheckpointRestoreResult restore(const CheckpointRestoreRequest&) = 0;
    virtual std::vector<CheckpointSummary> listCompatible(const ResumeQuery&) const = 0;
    virtual void discard(CheckpointId) = 0;   // 显式废弃（标记；磁盘删除归 project 域，阶段 A 不做 GC）
};
```

### 10.7 IExecutionCacheCoordinator

```cpp
class IExecutionCacheCoordinator {
public:
    virtual ~IExecutionCacheCoordinator() = default;
    // 派发前查找：结果缓存（evidence judgeCacheHit）＋模型缓存（ICompileCacheJudge 注入）联合查询
    // 后置：FullHit→返回命中规格（调度器走短路径）；否则 Incompatible/DiagnosticOnly＋reasons
    virtual CacheLookup lookup(const CacheLookupQuery&) = 0;
    // 归档 finalize 成功后登记（仅 Completed；失败/取消不登记——CON-04 执行侧门槛）
    virtual void storeResult(core::RunId) = 0;
    virtual void setEvictionPolicy(const EvictionPolicy&) = 0;   // 预算/容量（实现参数）
    virtual CacheStats stats() const noexcept = 0;
};
```

### 10.8 接口共性约束表（所有 §10 接口）

| 维度 | 规则 |
| --- | --- |
| 线程约束 | 提交/控制/查询/预算任意线程进入（内部转调度串行）；classifyArrival/tryTask/status/progress/stats 并发只读；admitResult/writeCheckpoint/launch/terminateForce 仅调度线程 |
| 并发规则 | 同一 TaskId 的并发控制请求按到达序串化生效；重复取消幂等；查询永不被状态写阻塞（不可变快照） |
| 生命周期 | 调度器实例随 L5 装配创建；shutdown 后一切变更接口→ContextClosed；RunRegistry 随调度器 |
| 所有权 | 接口实例归 L5/宿主；TaskRecord/登记表归调度器；返回值深拷贝 |
| 副作用 | 状态转移→事件；接纳→归档端口＋ResultArchived；写检查点→project 端口；无任何直接磁盘写 |
| 错误 | 调用方违约抛 ExecutionError；可预期失败经 Ack/diagnostics（结构化） |

---

## 11. 验证方案及故障注入矩阵

测试目标 `sdurws_ird_execution_test`（单元内：状态机/登记表/调度/缓存治理——纯逻辑，用替身）与 `sdurws_ird_execution_contract_test`（跨单元：与 evidence 校验器/判定器、project 归档端口 fake、**真实 worker 进程**场景——TestProcessRunner 按 TK-T11 触发，WP-08-T10 消费）。**全部为设计用例，未实现未运行；实现状态随 §12 登记；任何未执行测试不得标注通过。**

设施与替身：`ScriptedEvaluator`（evidence 测试替身复用/另建）＋`FakeWorkerMain`（可控 worker 测试替身：按脚本发进度/心跳/结果/崩溃/卡死）；`FaultInterceptor` 接缝（`execution/process-launcher/create`、`execution/channel/send-frame`、`execution/channel/recv-frame`、`execution/archive/*`——经 project 端口 fake）；`ManualClock`（2 s/10 s/心跳超时窗虚拟推进——testkit §6.6）；`TempDir`＋`DeterministicEnv`。**替身输出不构成任何业务算法正确性证明**（同 evidence EV-REG-3 边界声明，自审项 A-9）。

| # | 用例 | 需求/AT 依据 | 前置 | 操作 | 预期结果 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- |
| EX-SUB-1 | 并发提交与稳定任务身份 | TASK-03 | 可写项目上下文 | 多线程并发提交 N 个任务 | 各获唯一 TaskId；无重复；事件序（同任务 FIFO）；重复同输入提交＝两个独立任务 | TaskId 唯一性（checkStableIdsUnique 同型）；事件序断言 |
| EX-SUB-2 | 过期快照拒绝 | CON-01 | 快照锚定修订被移出闭包/对象缺失 | submit | SubmissionRejected＋EX-SNAPSHOT-STALE；无 TaskRecord | SubmitResult.diagnostics；状态查询为空 |
| EX-SM-1 | Queued 取消 | ARCH §4.3 A5 | 任务排队未派发 | requestCancel | 直接出队→Canceled；不经协作窗；无错误诊断 | 状态序 Queued→Canceling→Canceled；诊断空 |
| EX-SM-2 | Preparing 取消 | ARCH §4.3 | 派发组装中 | requestCancel（V1 启动注入延迟） | 丢弃派发物＋终结预留→Canceled | 预留目录 abandon 调用；物化字节释放 |
| EX-SM-3 | Running 取消（2 s/10 s） | NFR-PERF-02/AT-34 | worker 运行中 | requestCancel＋ManualClock 推进 | 2 s 内 Canceling＋停止派发批次；批次 10 s 收敛→Canceled；正常取消无错误诊断 | 状态时序（虚拟时钟断言）；诊断空；worker CancelAck |
| EX-SM-4 | Paused 取消保留检查点 | ARCH §4.3 A5/AT-35 | 任务暂停中（检查点已确认） | requestCancel | 直达 Canceled；**检查点文件/manifest 仍在**（可续跑） | checkpoints 目录内容；磁盘断言 |
| EX-SM-5 | 继续创建新 AttemptId | AT-35 | Paused＋兼容检查点 | requestResume | 同 RunId、AttemptId+1；旧 attempt 入 superseded；统计自检查点累计 | 登记记录；进度计数不回退 |
| EX-SM-6 | 非法转换拒绝 | TASK-01 | 各状态构造 | 构造 Paused→Completed、Canceled→Running 等矩阵外请求 | InvalidState 拒绝；状态不变 | 状态断言；ExecutionError.code |
| EX-SM-7 | 不支持暂停的显式反馈 | TASK-01/ARCH §4.3 | supportsPause=false 任务 Running | requestPause | 状态不变＋EX-CAPABILITY-UNSUPPORTED 反馈（不静默） | StatusAck.feedback |
| EX-WKR-1 | worker 正常完成 | TASK-01 | FakeWorker 完整脚本 | 运行至 FinalOutput | 九步接纳→Completed；归档 finalize；ResultArchived 事件；缓存登记 | runDir manifest 存在；事件序 |
| EX-WKR-2 | worker 崩溃 | NFR-REL-02 | FakeWorker 注入访问违例/异常退出码 | 运行中崩溃 | 仅当前任务→Failed（EX-WORKER-CRASHED）；主进程存活；**最近检查点保留**；其他任务不受影响；崩溃 worker 不回池 | 任务状态；池状态；checkpoints |
| EX-WKR-3 | worker 卡死 | NFR-PERF-02（超时路径） | FakeWorker 停发心跳/停响应 | ManualClock 推进心跳失联阈值 | WorkerHung 判定→强杀路径→Failed＋EX-FORCE-TERMINATED | 心跳时间戳；退出码；诊断码区分 |
| EX-WKR-4 | 强制终止 | NFR-PERF-02 | Running＋批次不收敛 | 取消超时/直接 requestForceTerminate | 进程树终止（Job）→Failed＋EX-FORCE-TERMINATED（**区别于普通失败**）；句柄关闭 | 进程存活探测；诊断标记 |
| EX-WKR-5 | 运行超时 | （超时治理） | 评估器超过声明时限（能力声明字段） | 超时 | 同卡死路径（诊断区分 timeout） | 终结原因 |
| EX-CHN-1 | 进度乱序/重复 | ARCH §4.1 | FakeWorker 乱序/重复 seq | 注入 | 按 seq 重排/去重；断裂→协议错误→Failed | ProgressReport 序；EX-CHANNEL-PROTOCOL-ERROR |
| EX-CKP-1 | 检查点损坏 | CON-04 | 篡改批次文件一字节 | restore | EX-CHECKPOINT-CORRUPT；原文件保留；拒绝恢复 | 诊断；目录内容不变 |
| EX-CKP-2 | 检查点版本不兼容 | CON-04 | checkpointFormatVersion/contractVersion 失配构造 | restore | EX-CHECKPOINT-INCOMPATIBLE＋reasons；不迁移 | judgeCheckpointCompatibility 调用记录（判定归 evidence 验证） |
| EX-CKP-3 | 恢复失败保留原检查点 | §8.1 | 域申报不可续（resumable 翻转） | restore | 拒绝＋原检查点完整保留 | 磁盘断言 |
| EX-CCH-1 | Quick 缓存不替代 Verified | CON-04/OPT-06/§8.1 表 1 | Quick 命中条目＋Verified 请求 | lookup | Incompatible(mode-mismatch)（判定来自 evidence——本用例验证调度侧消费与不派发伪装） | CacheLookupResult.reasons |
| EX-CCH-2 | 失败/取消结果不入正式缓存 | CON-04/TASK-02 | Canceled/Failed 运行终结 | storeResult 路径执行 | 不登记缓存条目；部分批次仅 DiagnosticOnly | 缓存清单断言 |
| EX-REG-1 | 未知 RunId | TASK-03/AT-10 | 无登记构造 Arrival | admitResult | Rejected(UnknownRun)＋开发诊断；**不写任何项目 results/** | 两项目 runDir 目录内容 |
| EX-REG-2 | 任一五元组字段失配 | TASK-03/ARCH §4.5 | 逐字段构造 5 组失配 Arrival | admitResult | 全部 Rejected(IdentityMismatch) | 逐字段用例独立断言 |
| EX-REG-3 | 陈旧 AttemptId | ARCH §4.5 | 携带 superseded attempt 的 Arrival | admitResult | Rejected(StaleAttempt) | supersededAttempts 命中 |
| EX-REG-4 | 项目切换后合法迟到结果 | TASK-03/PM-13/AT-10 | 项目 A 运行中切换至 B | 完成事件迟到 | 接纳→归档至 A 原 runDir；**不进 B 会话/不写 B**；A 上下文存活至 finalize | 归档路径；closed() 时序；B 目录 |
| EX-REG-5 | 已释放上下文后的迟到写请求 | ARCH §6.8 A7 | A 上下文已 Closed（异常构造） | 迟到 admitResult→归档 | 重新取权成功路径归档；失败路径→EX-ARCHIVE-AUTHORITY-LOST 拒绝＋诊断 | StoreError 消费；诊断码 |
| EX-REG-6 | HEAD 变化但历史归档仍接纳 | CON-02/05/S7 | 运行期间 A 产生新修订（仅电机成本变更） | 完成事件到达 | 接纳不受影响（HEAD 不参与判定）；当前性由 evidence 另判（Current 断言在其侧） | 归档成功；无 PolicyChanged 类拒绝出现 |
| EX-REG-7 | 重投递幂等 | ARCH §4.5 | 同 (run,attempt) FinalOutput 重发两次＋摘要一致 | admitResult×2 | 第二次幂等成功（不重写/不报错）；摘要不一致→冲突拒绝 | manifest 内容不变；计数 |
| EX-REG-8 | 内容冲突拒绝 | project D-14 | 同 runDir 不同摘要批次 | writeBatch 重投 | 冲突拒绝＋诊断（不覆盖） | 文件内容 |
| EX-ARC-1 | 磁盘不足 | NFR-REL-01 协作 | 归档端口 fake 注入 disk-full | 完成归档 | 有界重试→abandon＋EX-ARCHIVE-FAILED；任务保持 Completed；缓存跳过登记 | archivePhase=ArchiveFailed；诊断 |
| EX-ARC-2 | 写入拒绝 | 同上 | 注入 write-rejected | 同上 | 同上路径 | StoreError 透传 |
| EX-ARC-3 | 临时文件清理失败 | §6.6 | worker 临时目录删除失败注入 | 尝试终结 | 任务终态不受影响；开发诊断；已提交检查点不动 | 目录残留记录 |
| EX-ARC-4 | UI 已关闭但归档仍在途 | PM-03/A7 | shutdown(Wait)+在途归档 | 完成事件迟到（UI 已关） | 上下文存活至 finalize；drained() 后 Closed；**无永久等待**（超阈值强制 abandon 兜底路径亦验证） | closed() 时序；drained 断言 |
| EX-ARC-5 | 任务完成但归档失败 | project §10.1 | EX-ARC-1 场景 | 同上 | 任务 Completed＋archivePhase=ArchiveFailed（**完成≠归档完成**的正交观测） | TaskSnapshot.archivePhase |
| EX-RCV-1 | 崩溃后中断任务显示"已中断" | NFR-REL-03/PM-08/AT-11 | 运行中 kill 主进程（TestProcessRunner） | 重启＋恢复扫描 | 归档预留无 manifest 的运行重建为 Interrupted；不伪装完整结果；检查点可续 | 恢复条目；EX-TASK-INTERRUPTED |
| EX-RES-1 | 双 worker 竞争同一任务 | 调度互斥 | 并发派发注入竞争 | 同任务双 launch 尝试 | 登记互斥：仅一个活动 worker；另一个拒绝（开发诊断） | WorkerSupervisor 状态 |
| EX-RES-2 | 并发资源预算超限 | NFR-PERF-04 | 内存采样 fake 推至 >70% | 持续提交 | 先节流（停派发/降并行）→仍超限→资源不足诊断；已排队/运行任务**不失败** | 派发暂停观测；诊断 |
| EX-ORT-1 | 任务状态与判定/当前性不混淆 | CON-02/TASK-02 | 构造 Completed+DataInsufficient、Failed、Canceled、Superseded-Current 等组合 | 状态/envelope/当前性查询 | 四轴互不推导（§5.6 表逐行）；取消/失败不产生正式结论字段 | envelope 字段断言（复用 evidence 校验器） |
| EX-BLD-1 | 构建红线 | NFR-MNT-01/02/T-1 | — | 扫描 | 零 Qt（含 worker 目标）；无私有头出 include/；依赖图仅四条登记边；产品目标零 testkit | ird_gates＋脚本扫描 |

每条用例经 `IRD_TEST_INFO` 登记需求/AT 追溯；时钟敏感用例一律 ManualClock（不 sleep 断言——testkit §6.5 同步纪律）。

---

## 12. 阶段 A 实现任务拆分

局部编号 `EX-Txx`（execution＝**WP-08**，REQUIREMENTS §3/ARCH §3.1 既有登记；经 DTB §2.9"≙"映射对接 WP-08-T02～T10，不重排、不双轨；WP-08-T01＝本文本身，无对应实现任务）。依赖自上而下。**阶段 A 交付**：任务身份与状态机、调度器、Windows worker 监督、取消/暂停/继续/强杀、检查点契约、缓存兼容调度、RunRegistry、迟到结果防护、project 归档交接、可控 worker 测试替身与契约/故障注入套件；**不含**：任何业务评估器接入、真实运动学/轨迹/动力学/选型/优化任务（B/C/D 经③端口接入）、暂停/继续与并行/检查点的规模化验收（D 期，AT-35）。

| 任务 | 输入 | 产物 | 依赖 | 涉及文件 | 验证方式 | 完成条件 | ≙ |
| --- | --- | --- | --- | --- | --- | --- | --- |
| EX-T01 构建落位 | 骨架 CMakeLists、§3.4、DTB §5.1/§5.5 | `sdurws_ird_execution` STATIC（C++17、链 core/evidence/project）；注册 `_test`/`_contract_test`（gtest vcpkg 接入）；README 任务卡指向 §9→§12 修正 | 无（WP-03/05/04-T01 后更佳，可平行） | `industrialrobot/CMakeLists.txt`、`execution/CMakeLists.txt`（新）、`src/`（空起步） | 双模式构建；EX-BLD-1 前两项 | 两模式零错误；红线扫描零命中 | WP-08-T02 |
| EX-T02 任务身份与状态机 | §4、§5 | `TaskTypes.*`、`StateMachine.*`（转移表＋守卫＋事件发布接缝）、能力声明承载 | EX-T01 | 同名文件 | EX-SUB-1/SM-1~7 | 逐转移矩阵＋强制终止矩阵用例通过；四态取消出口覆盖（ARCH §11.2-6） | WP-08-T03 |
| EX-T03 取消与终止协议 | §7、ARCH §4.4 | 取消命令通道、协作窗（ManualClock 可注入时钟）、强杀编排、DrainPolicy | EX-T02 | `Controller.*`、调度器取消路径 | EX-SM-3/4、EX-WKR-4/5 | 2 s/10 s 协议时序用例通过；正常取消零错误诊断 | WP-08-T04 |
| EX-T04 RunRegistry 与接纳 | §9、ARCH §4.5 | `RunRegistry.*`（登记/追加/一次性判定）、`Admission.*`（九步＋名称反解编排＋envelope 组装复用 evidence 校验器） | EX-T02、WP-05-T07、WP-04-T14 | 同名文件 | EX-REG-1~8、EX-ORT-1 | 未知/失配/陈旧拒绝＋登记位置取用＋幂等全过（ARCH §11.2-7） | WP-08-T05 |
| EX-T05 调度线程与事件/进度分发 | §6.1~§6.3、§10.4 | `Scheduler.*`（队列/优先级/预算/内联门槛/提交验证）、`EventBus.*`（core 接口参考实现）、进度节流 | EX-T02、WP-03-T08 | 同名文件 | EX-SUB-1/2、EX-RES-2（调度面） | 事件 FIFO 与投递线程契约用例通过；UI 线程零计算（结构断言） | WP-08-T06 |
| EX-T06 WorkerLauncher 与通道 | §6.4~§6.6 | `win32/ProcessLauncher/ChannelPair/JobScope.*`＋`worker/main.cpp`＋`ChannelProtocol.*`＋心跳/崩溃检测＋临时目录隔离 | EX-T04、WP-06-T09（物化注入可用） | 同名文件＋`worker/` | EX-WKR-1~3、EX-CHN-1 | worker 崩溃只失败当前任务；通道消息全携五元组；流式分批回传用例通过 | WP-08-T07 |
| EX-T07 ResourceController 基础节流 | §6.1/§6.6 | `win32/MemProbe.*`＋汇总＋节流决策＋资源不足诊断 | EX-T06 | 同名文件 | EX-RES-2 | 接近 70% 先节流后诊断用例通过（基础形态；规模化归 WP-23-T06） | WP-08-T08 |
| EX-T08 检查点与缓存存储治理 | §8 | `Checkpoint.*`、`CacheCoordinator.*`（含 ICompileCacheJudge 消费） | EX-T04、WP-05-T09、WP-06-T10（注入可用） | 同名文件 | EX-CKP-1~3、EX-CCH-1/2 | 部分失败结果不作正式命中；检查点兼容契约用例通过；判定逻辑零复制（经 evidence/runtime 注入） | WP-08-T09 |
| EX-T09 契约测试套件落地 | §11 全表、testkit §6.4/§6.5 | `execution/test/*`、`contract_test/*`（FakeWorkerMain＋FakeArchivePort＋FaultInterceptor 接缝；TestProcessRunner 按 TK-T11 触发接入 EX-RCV-1/EX-WKR-2 真进程场景） | EX-T01~T08、WP-02-T09（T11 按需） | `execution/test/*`、`contract_test/*` | §11 矩阵逐项 | 全部用例执行通过并留痕（未执行不得标注通过）；替身边界声明在案 | WP-08-T10 |
| EX-T10 文档与门禁同步 | 全文 | README 核对；§15.3 待裁决项状态更新（P-PR-4/P-EX-*）；DETAILED-DESIGN execution 行状态更新申请；`ird_gates`/CI 建议提交 | EX-T01~T09 | 本文、README、DETAILED-DESIGN.md | 评审 | 本文与实现零偏差登记；未决项最新 | （卡内治理任务，DTB §2.9 未列——按 DTB §5.4 以增量修订登记） |

---

## 13. 后续阶段承接及接口交接清单

| 阶段/单元 | 从 execution 接收 | 须自行提供（责任） | 典型 AT 载体 |
| --- | --- | --- | --- |
| B·kinematics | 任务通道（提交/进度/取消/状态）、RunRegistry 归档、缓存调度（Quick/Verified 判定经 evidence） | `kin.batch-ik`/`kin.region-coverage` 评估器与能力声明（暂停/检查点粒度）；批量任务的业务诊断 | AT-03/34（取消/进度） |
| B·optimization（OPT-B） | 取消/进度/缓存与确定性种子的平台承载（§15.0 B 子集） | 候选批量评估的任务编排（优化域内部）；审计计数消费 | AT-34 |
| D·optimization（OPT-D） | Paused 态消费（暂停确认边界/继续不重复统计/暂停中取消保检查点）、并行与检查点规模化、资源预算 70% 治理 | 联合优化的大规模任务声明；误淘汰审计口径 | AT-35 |
| C·trajectory/dynamics/selection | 同通用通道 | 各域评估器 | AT-06/07/08 |
| diagnostics | EX-\* 稳定码建议清单（§3.4）、IExecutionDiagnosticsSink 形态 | StableCodeRegistry 码值收编、两级日志、脱敏（P-EX-8） | ERR-01 |
| ui | TaskState 九态事件流、ProgressReport 投影、TaskSnapshot（PM-03 任务清单数据）、EventBusImpl（UI 线程 marshaling 扩展点） | 九态短标签、任务清单呈现、关闭对话框（shutdown/DrainPolicy 对接） | PM-03/UX-10 |
| workflow | shutdown(DrainPolicy)/drained、恢复期 Interrupted 条目（重跑入口数据） | 关闭/切换编排、恢复横幅（PM-15） | PM-03/08、AT-21 |
| project | §9.5 调用线程与预留/abandon 时机（P-PR-4 冻结答复）、完成事件幂等语义 | 归档端口实现核对、RecoveryReport 对预留目录的恢复诊断呈现 | AT-10/11/13 |
| evidence | envelope 组装复用清单（aggregateVerdict→make→validateCombination）、IEvaluationContext 实现约定（取消/进度/物化读取） | 判定器与校验器的 execution 侧消费支持（已就绪） | TASK-01~03 |
| runtime | ICancelSignal（取消令牌适配源）、快照引用持有纪律执行（§7.5/运行记录持引用至归档） | L5 适配器（IExecutionModelService/ICompileCacheJudge/INameResolverAdapter 三件） | CON-06/AT-18 |
| testkit | TestProcessRunner/EventWatch 消费场景（EX-RCV-1/EX-WKR-2/3）；faultPointId 命名（`execution/process-launcher/create`、`execution/channel/send-frame`、`execution/channel/recv-frame`） | TK-T11 触发式实现（WP-08-T09 前就绪） | AT-11/13 |
| L5 应用壳 | 装配清单：事件总线实例、三注入适配器、评估器注册（主/worker 同清单）、DrainPolicy 关闭控制器 | 装配与关闭编排、worker exe 部署（bin 同目录，NFR-DEP-02） | — |

---

## 14. 需求—设计—验证追踪矩阵

| 需求/上游条款 | 设计落点 | 验证（§11） |
| --- | --- | --- |
| TASK-01（状态机＋能力声明） | §5 全节、§5.5 | EX-SM-1~7、EX-WKR-4 |
| TASK-02（合法组合；取消/失败不入正式） | §9.2 步 7/9、§8.2 写入门槛 | EX-ORT-1、EX-CCH-2 |
| TASK-03（五元组；会话隔离） | §4.1/§4.3、§9.1~§9.4 | EX-SUB-1、EX-REG-1~7 |
| CON-01（快照运行） | §6.3 V2、§9.2 步 5 | EX-SUB-2 |
| CON-02（三态正交） | §5.6、§9.2（HEAD 不参与接纳） | EX-ORT-1、EX-REG-6 |
| CON-03（固化门禁消费） | §6.3 V1（evidence validateForMode 消费侧） | 阶段 B 契约（登记） |
| CON-04（缓存/检查点契约；部分失败不命中） | §8.1/§8.2 | EX-CKP-1~3、EX-CCH-1/2 |
| CON-05（切片内容身份） | §6.1 缓存键、§9.1 扩展字段 | EX-CCH-1、EX-REG-6 |
| CON-06（策略/名称身份；先反解后接纳） | §9.2 步 8、§6.4 握手 manifest 比对 | EX-WKR-1（握手）、EX-REG-*（反解编排） |
| NFR-PERF-01（>1 s 转后台） | §6.1/§6.3 | 结构断言（EX-T05 完成条件） |
| NFR-PERF-02（取消 2 s/10 s） | §7.1、T13 | EX-SM-3、EX-WKR-3/4/5 |
| NFR-PERF-03（分批/流式） | §6.5 ResultBatch | EX-WKR-1、EX-CHN-1 |
| NFR-PERF-04（70% 先节流） | §6.1/§6.6 | EX-RES-2 |
| NFR-REL-02（worker 崩溃隔离） | §6.4/§6.6 | EX-WKR-2 |
| NFR-REL-03（已中断） | §5.1 Interrupted、§7.5 | EX-RCV-1 |
| PM-03（关闭二选） | §7.5、DrainPolicy | EX-ARC-4 |
| PM-07（只读拒绝） | §6.3 V3 | EX-SUB-2 变体（预留失败路径） |
| PM-08（崩溃恢复协作） | §7.5、§9.5 abandon 时机 | EX-RCV-1 |
| PM-13（切换场景） | §9.4 | EX-REG-4 |
| OPT-06（平台承载） | §8.2、§13 | EX-CCH-1 |
| §8.1 表 1/表 3（模式效力/合法组合） | §6.3（Preview 不登记/不归档）、§9.2 步 9 | EX-ORT-1 |
| AT-10/11/13/34/35（场景载体） | §9.4/§7.5/§7.1/§7.2~7.3 | EX-REG-4/EX-RCV-1/EX-ARC-1~5/EX-SM-3/EX-SM-4~5 |
| ARCH §4（SA-08）/§4.5（A8）/§6.8（A7）/§11.2-6/-7 | §3、§6、§9 | EX-WKR-2、EX-REG-1~8、EX-SM-1~7 |
| NFR-MNT-01/02（零 Qt/红线） | §1.4、§3.4 | EX-BLD-1 |

---

## 15. 设计决策、风险、待裁决项与变更记录

### 15.1 设计决策登记（本文作出并说明理由的普通实现选择）

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | execution 目标零 Qt（含 Core；worker 亦零 Qt） | L3 允许但不必需；std＋Win32 足够；UI 线程 marshaling 归 ui 侧总线扩展（§10.4/§13）；最大化模型/进程测试直调。备选（QThread/QProcess）引入事件循环依赖被否 |
| D-02 | worker 池＋每 worker 独立 Job Scope（KILL_ON_JOB_CLOSE） | 池化省启动成本；Job 保证主进程崩溃联动终止（无孤儿 worker，NFR-REL-02 的进程侧闭环）；崩溃 worker 永不回池（状态污染防线） |
| D-03 | envelope 组装单点＝主进程调度线程接纳路径（worker 回传 EvaluationOutput，不在 worker 构造 envelope） | 接纳是唯一权威点（九步＋名称反解依赖主进程登记与⑥端口注入）；避免 worker 构造后传输失真与双构造点漂移；evidence §7.1"接纳侧复用同一校验器"的自然落位 |
| D-04 | 双优先级＋FIFO，不抢占 | 需求未定义抢占；用户发起的取消/暂停已覆盖干预需求 |
| D-05 | 资源治理三段：节流阈值 65%→停派发/降并行→70% 诊断 | NFR-PERF-04 上游值 70%＋"先节流"原文；65% 为实现参数（登记非需求阈值） |
| D-06 | 心跳 5 s×3 失联判定；进度对外 ≤10 Hz | 实现参数（非需求阈值）；2 s/10 s 为需求值不改动 |
| D-07 | 终态任务保留窗口 30 min 后可释放 | 查询体验与内存平衡；实现参数 |
| D-08 | 同 (project,snapshotId) 派发物会话级复用 | 物化是 Preparing 主要成本；复用不改变身份（身份绑内容不绑副本） |
| D-09 | 失败任务不自动重试；重跑＝新 TaskId（可从检查点续，新身份链） | 上游无自动重试需求（避免与"不承诺恰好一次"冲突）；用户主导重跑语义清晰（NFR-REL-03"可重跑"） |
| D-10 | 同键并发缓存请求合并为等待同一运行 | 防重复计算（EX-RES-1 缓存面）；备选各自运行被否（浪费且归档冲突） |
| D-11 | 归档失败有界自动重试 2 次（1 s/4 s 退避）后 abandon | 责任有界终结（project §10.1"不永久等待"）；实现参数 |
| D-12 | 派发期即归档预留（空 begin） | 三合一：权限前置（PM-07）、白名单校验、崩溃可观测（无 manifest＝中断判据——避免独立任务 journal 第二账本） |
| D-13 | 任务状态/登记表会话内存态，磁盘痕迹唯一经 project | §17 表注磁盘写入唯一归属；不造第二持久化（Queued 任务崩溃即消失——P-EX-6 登记） |
| D-14 | 暂停＝检查点确认边界后释放 worker | 释放内存预算（检查点在盘承载状态）；恢复成本＝新进程启动（池化后可接受） |
| D-15 | 通道为字节管道＋自定义帧（IRDCHN/1），不用命名对象/COM/文件共享 | 最小依赖面；帧自定界＋seq 序控满足全部需求；Windows 离线单机无跨机需求 |
| D-16 | `sdurws_ird_execution_worker` 定性为装配目标（评估器链接清单归 L5/DTB） | R-1 业务互链红线不因 worker 失效（worker 不链接 `_plugin`；阶段 A 仅替身）；同 SA-01 静态装配精神 |
| D-17 | runtime/policy 经三注入接口消费（IExecutionModelService/ICompileCacheJudge/INameResolverAdapter＋ICancelSignal） | ARCH §3.5 白名单硬约束（表外边＝构建失败）；同 evidence D-07/project §5.3.6 注入模式；架构若补登边可平滑直连 |
| D-18 | RunRegistry 扩展绑定字段（snapshotId/sliceId/evaluator/policy）作第二道开发级核对，不参与五元组接纳判定 | A8 判定基准唯一（防双权威）；扩展核对防通道串扰（开发诊断级） |

### 15.2 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | core/evidence/project 卡 v0.1 未冻结（词表/校验器/归档端口签名可能变） | EX-T02 起返工 | §3.2 消费清单逐项锚定各卡章节；冻结 diff 后增量同步（P-EX-1） |
| R-2 | ARCHITECTURE v0.11 Draft（A9+ 评审可能调整 §4/依赖表） | 状态机/通道/注入层返工 | 严格锚定 §4/§3.5 明文；影响面集中 §3/§5/§9（P-EX-2） |
| R-3 | Windows 进程/作业/管道行为与文档口径偏差（句柄继承、强杀时序） | 崩溃隔离/强杀可靠性 | §6.6 逐项对照 Microsoft Learn；EX-WKR-2/3/4 真进程用例实证；不超诺 |
| R-4 | 心跳/超时/节流实现参数在慢机/大快照下误判（假卡死/误节流） | 误失败/误降速 | 参数可配；EX-WKR-3 用 ManualClock 独立于真实时延；大数据集（P-06/大规模）阶段 C/D 复测 |
| R-5 | 物化大快照的派发延迟与内存峰值（NFR-PERF-03 规模） | 派发慢/预算压力 | D-08 复用；分帧续传；性能归 WP-23 基准（本文不预设达标） |
| R-6 | diagnostics 未产出（码表/日志形态） | 诊断码与 sink 适配返工 | 最小 sink 注入＋建议码清单（P-EX-8，P-PR-6 同模式） |
| R-7 | 替身 worker 被误读为业务算法验证 | 验收误判 | §11 边界声明＋自审项 A-9 |
| R-8 | 恢复期"已中断"判据依赖归档预留目录语义（project D-13/D-12） | 与 project 实现漂移 | §9.5 冻结交互；EX-RCV-1 契约用例双向锁定（P-EX-6 登记） |

### 15.3 待裁决项（问题—依据—影响—建议—需要裁决者）

| # | 问题 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-EX-1 | 消费的 core/evidence/project/runtime/policy/testkit 契约均 v0.1 Draft 未冻结 | 各卡文档头 | EX-T02 起返工 | 各卡冻结时出 diff，本文按影响面增量修订留痕 | 各卡所有者＋本文所有者 |
| P-EX-2 | ARCHITECTURE v0.11 Draft 待评审 | 架构文档头 | §3/§5/§9 可能同步 | 评审后增量修订（A9+ 处置） | 架构所有者 |
| P-EX-3 | 任务指令述"execution 可依赖 core、evidence、runtime、policy、project、diagnostics"，ARCH §3.5 实际登记四条边（runtime/policy 不在表内） | 任务约束§三 vs ARCH §3.5（SA-10 表外边＝构建失败） | 编译期依赖形态 | 维持本文 §3.3 注入消费（D-17）；如需直连请架构修订 §3.5 补登 execution→runtime 边 | 架构所有者 |
| P-EX-4 | 任务指令示例列出 11 态（含 Created/CancelRequested/Rejected），上游（PM-03/ARCH §4.3/core 词表）定义九态 | REQUIREMENTS §17 PM-03、ARCH §4.3、core.md §4.7 | 状态机词表 | 按上游九态执行（§5.1 注：提交拒绝在状态机之外、Canceling 承载取消请求相）；如需三态显式化走需求变更 | 需求所有者（登记确认） |
| P-EX-5 | P-PR-4（project §15.3）：归档调用线程与 RunRegistry 对接 | project.md §15.3 | 契约测试线程模型 | 本文 §9.5 已冻结（调度线程调用＋writer 互斥串行化＋完成事件幂等无上限＋abandon 三时机）；请 project 侧确认消账 | project 详设所有者 |
| P-EX-6 | 任务中断持久化判据＝归档预留目录（无独立 journal）：Queued 任务崩溃即消失（不呈现"已中断"）；Preparing 起可观测 | PM-08/NFR-REL-03、project §4.1 results 行 | 崩溃恢复呈现范围 | 维持 D-12/D-13（磁盘痕迹唯一经 project；无第二账本）；若需求"排队任务也须恢复呈现"走需求变更 | 需求所有者＋project 详设所有者 |
| P-EX-7 | 评估器运行期能力声明（暂停/检查点粒度）的承载位置：evidence EvaluatorDescriptor 未含执行能力字段 | TASK-01、evidence §9.2 | 能力声明数据源 | execution 侧注册表扩展声明（§5.5），登记为 evidence 交接（其卡增量修订时决定是否上收 descriptor） | evidence 详设所有者 |
| P-EX-8 | EX-\* 稳定码与 IExecutionDiagnosticsSink 形态待 diagnostics 冻结 | diagnostics 未产出、ARCH §3.5 边已登记 | 码表/适配器返工 | diagnostics.md 起草时收编 §3.4 建议清单；sink 与 project §5.0 形态统一 | diagnostics 详设所有者 |
| P-EX-9 | 旧版本检查点不迁移（Incompatible 拒绝） | CON-04 保守方向 | 升级后长任务须重跑 | 维持保守（宁可重算不可错续）；如需迁移工具走需求/设计变更 | 需求所有者（登记确认） |
| P-EX-10 | DETAILED-DESIGN.md execution 行状态"待产出"需随本文更新；execution README 任务卡指向 §9 需修正 §12 | §1.2 实测 | 索引一致性 | 随 EX-T01/EX-T10 消账（文档同步任务） | 详设目录维护者（本文所有者执行） |

### 15.4 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-10 | 首版：基于 REQUIREMENTS v1.16（Accepted）、ARCHITECTURE v0.11（Draft）、六张协作卡（core/evidence/project/runtime/policy/testkit v0.1 Draft）与 development-task-breakdown v0.2 完成 15 章详细设计；冻结任务/运行/尝试三级身份与分配协议、九态状态机逐转移矩阵与四轴正交表、调度与资源治理（70% 先节流）、worker 模型与 IRDCHN/1 通道协议、取消/暂停/继续/强杀协议（2 s/10 s）、检查点契约与缓存四分治理、RunRegistry 九步接纳与迟到结果防护、project 归档协作（P-PR-4 冻结答复）；验证矩阵 EX-\* 33 组；实现任务 EX-T01～T10（≙ WP-08-T02～T10）；待裁决 10 项（P-EX-1～10）。同日：execution README 任务卡指向 §9→§12 修正（与本文 §12 一致，同 evidence.md 先例） |
| v0.2 | 2026-09-10 | 全链一致性审计消账：①§7.5 `DrainPolicy::WaitForInFlight`（枚举中不存在）更正为 `CancelQueuedAndWait`——销账 ui.md CF-2/P-UI-3；②§3.4 稳定码清单由"未产出建议值"更新为"已收编 18 项（diagnostics.md §4.6）"，并冻结枚举对齐说明（ContextClosed/InvalidState＝fail-fast token 不发码；EX-TASK-INTERRUPTED＝状态标注码无枚举值）；③§2.1.2/§3.1/§3.3/§3.4 四处"diagnostics 详设未产出/产出前"过期表述更正——P-EX-8（sink 统一与链接形态）保持登记不消账 |

### 15.5 交付前自审记录（v0.1；自审≠实现测试≠正式验收）

| 检查项（任务约束§八） | 结论 | 证据位置 |
| --- | --- | --- |
| 是否重复 project/evidence/runtime/policy 的职责 | ✔ 未重复：磁盘/锁/事务归 project（§2.1 N 表）；判定/当前性/缓存判定归 evidence（§1.1 声明 2）；编译/名称归 runtime（经注入 §3.3）；策略经值传递（§3.3） | §2、§3、§8 |
| 是否把任务完成误当作归档完成 | ✔ 四轴正交表＋ArchivePhase 独立轴（§5.6）；EX-ARC-5 专项反例 | §5.6、§9.3 |
| 是否把取消、失败或中断伪装成成功 | ✔ 九步之 7（已终结登记拒绝 FinalEnvelope）；envelope 构造复用 evidence 校验；EX-FORCE-TERMINATED 显式区分强杀 | §7.4、§9.2 |
| 是否允许未知或失配结果进入项目 | ✔ 九步 1~6（未知/失配/陈旧/绑定/类型五重拒绝）；worker 结果必经主进程核对 | §9.2、§6.4 |
| 是否把当前 HEAD 变化错误地当作历史归档非法 | ✔ HEAD 不参与接纳判定（§9.2 末段）；EX-REG-6 反例锁定 | §9.2、§9.4 |
| 是否在锁或存储上下文释放后继续写入 | ✔ 消费 project 权限结果（§9.5）；Closed 后迟到写→重取权→失败拒绝（§9.3） | §9.3、§9.5 |
| 是否存在永久等待路径 | ✔ 排空有界（§7.5）；归档有界重试＋abandon（§9.3）；取消 10 s 收敛＋强杀兜底（§7.1）；确认类等待不属本单元 | §7、§9 |
| 是否将缓存命中误当作当前结果 | ✔ §8.2"命中≠当前"显式规则；EX-CCH-1 | §8.2 |
| 是否把检查点恢复误当作任务成功 | ✔ §8.1"检查点与最终结果的区别"冻结；恢复后仍走完整接纳 | §8.1 |
| 是否引入未经上游批准的新状态、阈值或语义 | ✔ 九态零新增（P-EX-4 登记）；70%/2 s/10 s 为上游值；65%/心跳/保留窗口等标注实现参数（D-05~D-07/D-11） | §5.1、§6、§15.1 |
| 是否存在 UI 线程阻塞或 worker 直接访问正式项目目录 | ✔ 提交/控制非阻塞＋UI 线程零计算（§6.1）；worker 边界规则（§6.4）；EX-T05/BLD-1 结构断言 | §6 |
| 是否越权修改需求、架构或其他单元机制 | ✔ 全部排除（§2.3）；跨卡冲突 10 项集中登记待裁决（§15.3） | §2、§15.3 |
