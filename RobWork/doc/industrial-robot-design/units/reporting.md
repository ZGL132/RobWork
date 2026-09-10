# 工业机械臂设计软件 · reporting 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案） |
| 日期 | 2026-09-10 |
| 状态 | **`Draft`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试通过或正式验收） |
| 文档代号 | UNIT-REPORTING |
| 单元 | reporting（平台服务，L3；ARCHITECTURE §2.3/§3.1：ReviewReport 对象〔B/C 两级〕、HTML/JSON/CSV 渲染、证据包导出、往返复算） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md`、`units/testkit.md`、`units/project.md`、`units/evidence.md`、`units/runtime.md`、`units/policy.md`、`units/execution.md`、`units/diagnostics.md`、`units/io.md`、`units/ui.md`——均 v0.1（`Draft`，未冻结）；本文消费的 core/evidence 契约以各卡 v0.1 签名为基线逐项登记（§3.2），上游冻结版本变更时按影响面增量同步（待裁决 P-RPT-9） |
| 上游下游链位置 | ARCHITECTURE §11.1 / DETAILED-DESIGN.md：`units/reporting.md` 为 20 单元任务卡之一；对应 development-task-breakdown **WP-12-T01**（"编写 reporting 单元任务卡"）的产物；主 WP 归属 WP-H、WP-I（DETAILED-DESIGN 20 单元状态表；实施任务 WP-12-T02～T09 见 dtb §2.13） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/reporting/`（骨架已建：目标 `sdurws_ird_reporting`〔INTERFACE 占位〕＋别名 `RWS::ird::reporting`＋公共头保留位 `include/sdurws/ird/reporting/README.md`；README 引用本文 §9〔公共接口〕作为源码落位依据，与本文结构一致——实测核对） |
| 任务包 | WP-12（报告；RPT-01～06 主 WP；NFR-COR-04 主 WP〔与 WP-05/23 共同承载〕；REQUIREMENTS §3 阶段 B/C〔首用阶段 B〕） |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 历史实现仅功能范围对照、不作语义来源（RV-13）——且**当前磁盘不存在**（仓库根实测 2026-09-10，git 亦未跟踪；与 REQUIREMENTS v1.10 声明不符，core.md R-5/io.md §1.2 同源登记）；本文不引用、不复制、不恢复其任何机制（含旧 `StructureOptimizationReportWriter`/`KinematicAnalysisReportJson`——其功能对应关系仅见需求附录 A 台账） |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 reporting 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 reporting 的职责（§3.1 单元总表 reporting 行；§10.3 架构级 AT 映射"AT-22 多格式逐字段一致＝reporting 渲染单一数据源"；§10.1 阶段启用"B/C 阶段 reporting(RPT-01-B/RPT-01-C)"），把 ReviewReport 数据模型（B/C 两级章节契约）、结果/证据/诊断/当前性表达、报告生成冻结与幂等归档协议、HTML/JSON/CSV 渲染与逐字段一致性校验、证据包导出、评审签署元数据、公共接口与跨单元协作、验证方案与阶段 A/B/C 任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不收窄、不扩大；本文引用的需求 ID 与章节号均指 v1.16 当前正文（重点：RPT-01～06 及分级子级 RPT-01-B/RPT-01-C、NFR-COR-04、EVI-01/02 与 §8.1 全部表格、TASK-02、CON-01～06、PM-05/07/08/12、OPT-12、UX-02/03、NFR-SEC-03/07、NFR-MNT-01/03/04、附录 D、§16 验收要点）。架构归属以 `ARCHITECTURE.md` v0.11 为准（§3.5 依赖表、§7.6 证据管线、§7.9 导入安全、SA-12/SA-13 直接约束本单元）；本文对其含混或未登记事项的解释集中登记于 §14.3（P-RPT-1～P-RPT-9），不私自修改上游、不越权修改需求、架构或其他单元机制。

**权威与约束复述（贯穿全文）**：REQUIREMENTS.md 是需求及验收标准的唯一权威；ARCHITECTURE.md 决定报告数据源、证据引用和渲染边界。reporting 属 L3 平台服务，编译期登记边为 core、evidence、diagnostics、project（ARCH §3.5）；io 与 runtime 的能力经**注入式最小接口**消费（P-RPT-1/P-RPT-2，io.md P-IO-1 同案）。reporting **不拥有**项目事务、结果当前性计算、工程判定、证据生成、业务算法、UI 布局、`.rwpack` 编解码实现或业务结果缓存；evidence 提供证据和结果资格，reporting 只消费并呈现；project 负责报告工件持久化和发布，reporting 不绕过 project 直接写正式项目目录；io 负责 CSV/JSON 基础编码和包处理基础能力，reporting 负责报告语义和格式映射、不重复建立安全解析设施。reporting 不把报告显示状态当作工程真值；不因当前 HEAD 变化改写历史报告内容；不把失败、取消、中断或数据不足结果渲染为正式通过结论；不在报告层自行新增证据等级、判定阈值或当前性状态（RPT-05 措辞冻结的渲染侧承载）。

### 1.2 上游与磁盘现状登记（2026-09-10 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文承接：RPT-01（含 RPT-01-B/RPT-01-C 分级子级）、RPT-02、RPT-03、RPT-04、RPT-05、RPT-06、§16 验收要点（缺正式结果章节默认不选并显示缺项／预览按章节跳转／导出失败保留选择与路径可重试／AT-32 逐章追溯／AT-22 多格式一致）、NFR-COR-04、EVI-01/02＋§8.1 表 1～4、TASK-02、CON-02/03/05、PM-05/07/08、OPT-12、KIN-04（P-EV-5 参考值限定语）、附录 D 第 12 项、AT-10/14/19/22/30/32/34 |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。§3.5 依赖表登记 reporting→core,evidence,diagnostics,project（**未登记** reporting→io 与 reporting→runtime——本文按注入式设计并登记 P-RPT-1/P-RPT-2，io.md P-IO-1 同案）；§7.6 证据管线（envelope→snapshotId 追溯链）、§7.9 CSV 转义 roundtrip、SA-12 单一权威、§10.3"AT-22＝渲染单一数据源"直接约束本文。若评审产生 A9+ 处置按影响面同步（P-RPT-9） |
| `units/core.md` | 存在，v0.1，`Draft`（未冻结） | 本文消费其身份类型全族（无 ReportId——本文自建 `rpt-` tag 类型，P-RPT-3，diagnostics FindingId `fnd-` 同案）、ContentIdentity/ContentDigester、SourcedValue/ValueProvenance（RPT-05 限定语数据源）、EvaluationMode/TaskOutcome/EngineeringStatus 词表、DiagnosticRecord、UnitToken/convert（单位呈现唯一换算入口）、CoreError |
| `units/testkit.md` | 存在，v0.1，`Draft`（未冻结） | §2.3 对照表 reporting 行已冻结口径：**测试报告不经 reporting 生成**（TestRecord/`ird-test-report.json` 与 ReviewReport 数据契约无共享）；本文验证方案（§10）消费其 IRD_TEST_INFO/IRD_EXPECT_\*/TempDir/DeterministicEnv/ContractCheck |
| `units/project.md` | 存在，v0.1，`Draft` | §4.1 `reports/<report-id>/` 行（工件只增＋`report.json` 原子替换＋`<report-id>` 由 reporting 分配＋幂等追加/冲突拒绝归 reporting 契约）、§5.2 IProjectQueryPort（`listRuns` 仅 finalize 运行、`runDir` 供 reporting 读工件）、§10.6/§13.2 reporting 行（project 提供 reports/ 写入端口〔D-13/D-14 模式〕，reporting 须冻结**报告工件引用结构与幂等追加规则**）——本文 §7.3/§9.6 即该交接的承接答复；§6.8 写入口权限检查（只读项目全写入口拒绝） |
| `units/evidence.md` | 存在，v0.1，`Draft-Structured`（v0.2 变更 2026-09-10） | §13 B·reporting 行：evidence 向 reporting 交付 **FormalPass/ReviewRecord 两类资格检查、envelope→snapshot 追溯链、复现要素（RPT-03 证据包数据源）**；§7.2 资格检查、§7.1 ResultEnvelope、§8.1 当前性纯投影（仅 Current/Superseded 两持久态＋不可判定为计算形态，P-EV-4）、§4.1.2 ReproductionBlock、P-EV-5（覆盖率参考值须携带降级限定语）、P-EV-8（EvidenceItemStatus 五值为实现承载词表）——本文逐项消费并锚定 |
| `units/runtime.md` | 存在，v0.1，`Draft-Structured` | §10.12 与 reporting 专节：reporting 调用 runtime **只读摘要**（快照身份块/能力声明/资源清单——RPT-01 输入摘要章节数据源），**不含 RobWork 对象**；reporting 不触发编译、不直接读 WC/DWC；其 §13.2 要求 reporting 冻结**摘要 schema**——本文 §9.7 承接；报告引用的快照已释放时以归档的快照身份元数据呈现 |
| `units/policy.md` | 存在，v0.1，`Draft-Structured` | EngineeringPolicySet.contentIdentity（CON-06，报告输入摘要引用策略内容身份）；无 reporting 专属只读接口——报告经快照 policyRef 值传递消费（§6.3） |
| `units/execution.md` | 存在，v0.1，`Draft` | 九态 TaskState 词表（core）、TaskIdentity 五元组、只读查询 ITaskScheduler::tryTask/tasksByProject、ITaskController::progress、IResultAdmission::archivePhase（任意线程并发只读）；事件 TaskStatusChanged/ResultArchived（不持久化）——报告章节消费任务终态/进度投影（§10.6） |
| `units/diagnostics.md` | 存在，v0.1，`Draft` | §8.10/§9.7：reporting 消费 **exportSafeSummary**（稳定码＋安全参数，输出再过脱敏——双保险）与 DiagProjectionItem 只读投影（值拷贝）；reportable=false 的码不进报告；码表 deprecated 映射（tombstone）；本文 RPT-\* 建议码随注册收编（§3.5） |
| `units/io.md` | 存在，v0.1，`Draft` | §10.10/§13.2：reporting 消费 ICsvWriter/IJsonWriter（canonical 确定性＝AT-22 多格式一致基础）、IAtomicFileWriter（导出失败保留先前输出/可重试——RPT 验收要点）；CSV 方言标识行 `#rwcsv1`＋可逆编码（§5.1/§5.3）、JSON canonical（§5.9.3）为渲染格式基础；编译边未登记（P-RPT-1，io P-IO-1 同案） |
| `units/ui.md` | 存在，v0.1，`Draft` | C-14/N-8：ui→reporting（接口依赖，阶段 B 承接）；`report.export` 命令项已占位（readOnlyAllowed=是——导出到项目外不属项目写）；ui 只提供预览宿主与命令入口，渲染数据源归 reporting；**预览接口形状待本文冻结**（P-UI-9，裁决者=reporting 详设所有者）——本文 §9.8/§12 承接 |
| `units/reporting.md` | **不存在**（本文新建） | 其余 9 个业务/编排单元卡（modeling/requirements/kinematics/trajectory/dynamics/drivetrain/selection/optimization/workflow）未产出——本文需要其章节投影处给最小依赖契约并登记交接（§12），不代写对方详设 |
| `DETAILED-DESIGN.md` | 存在（2026-09-10 磁盘版） | 20 单元状态表登记 reporting 为"待产出"、主 WP-H/WP-I；本文产出后该表由治理流程同步，本文不代改 |
| `development-task-breakdown.md` | 存在，v0.2（2026-09-10 磁盘版） | §2.13 登记 WP-12-T01（本卡）～T09；§3 追踪矩阵 RPT 行（RPT-01-B→WP-12-T03；RPT-01-C/RPT-04→WP-12-T07；RPT-02→T04；RPT-03→T05；RPT-05→T06；RPT-06→T08）；§4 O-25（渲染层是否预留 PDF 接口——本卡裁决并登记，见 P-RPT-6）；§5.1/§5.5 构建与测试框架约定为本文 §3.4/§10 的执行基线 |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 单元目录＋`patches/`（实测 23 文件口径） | `sdurws_ird_reporting` 为 INTERFACE 占位，无源码；`reporting/include/sdurws/ird/reporting/README.md` 引用本文 §9（实测一致）；`_test`/`_contract_test` 目标按任务卡逐个登记、不预建空目标 |
| `old/` | **不存在于磁盘**（仓库根实测；git 亦未跟踪） | 与 REQUIREMENTS v1.10 声明不符（core.md R-5/io.md 同源登记）；从头构建口径不受影响；本文不引用其任何机制 |

### 1.3 设计目标

1. **统一契约，供后续单元消费**：ReviewReport 对象（B/C 两级）、章节与证据绑定模型、报告级版本与内容身份、HTML/JSON/CSV 渲染契约、多格式逐字段一致性校验、幂等导出与冲突拒绝、证据包要素、评审签署元数据——在本文单点冻结；阶段 B/C 各业务单元依据本文注册章节投影并按同一契约呈现，无需再协商（任务约束§二）。
2. **只读、可追溯（Q1/NFR-COR-04）**：报告对象只读、只引用明确 ID 的修订、快照与结果；每项正式结论定位到输入快照与证据（envelope→snapshotId 追溯链的直接消费）；报告内容身份与文件路径、显示名称分离。
3. **不越权呈现（RPT-05/EVI-01）**：只有 evidence 的 FormalPassEligibility 五条件齐备的结果可渲染为"正式通过结论"；经验证的不可行结论经 ReviewRecordEligibility 可作正式评审记录——两声明独立；估算值、数据不足、外部验证未完成等限定语必须保留；失败/取消/中断/数据不足绝不渲染为正式通过。
4. **单一数据源多格式一致（AT-22）**：全部格式从同一 ReviewReport 的一次性字段矩阵投影渲染；不允许每种格式重新计算结果；canonical 确定性（同报告同字节）。
5. **不可变历史（PA-2/CON-02）**：报告对象构造后不可变；历史报告文件内容不因当前性变化或 HEAD 前进被改写；幂等导出以内容摘要判定、同路径不同内容冲突拒绝；不把文件存在误当作正式发布。
6. **零 Qt、零反向依赖、不预建空业务（NFR-MNT-01/04）**：目标仅依赖 core＋evidence＋diagnostics＋project（登记边）＋标准库；io/runtime 能力经注入式最小接口（P-RPT-1/2）；阶段 A 交付报告基础模型、渲染器、一致性检查、导出归档与可控测试替身，不实现任何业务章节内容、不预建空页面（TRJ-08 同精神）、不留 PDF 接口桩（附录 B 裁决，P-RPT-6）。
7. **可直接落地**：阶段 A 交付基础模型与契约测试（替身章节）；阶段 B 接入 B 级真实章节；阶段 C 接入 C 级章节、变体报告与往返复算（§11）。

### 1.4 语言、标准库与构建约束（按仓库实测确认，与 core.md/project.md/io.md §1.4 同源）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | 构建缓存 `build/CMakeCache.txt`：Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计，不做平台特定扩展 |
| C++ 标准 | 基线 RobWork `CMAKE_CXX_STANDARD 11`；industrialrobot 显式 C++17（core.md D-01） | 同口径：显式 `cxx_std_17`；`std::optional/variant/string_view/filesystem` 允许；不用 C++20；不引入 Boost 等第三方运行库 |
| Qt | Qt 6.11.1（msvc2022_64）；L3 允许 Qt Core/Gui、禁 Widgets | **reporting 目标零 Qt（含 Core）**（决策 D-01）：报告渲染为纯字节生成（HTML/JSON/CSV 文本），预览宿主归 ui；最大化模型测试直调能力（NFR-MNT-01 精神，与 project/diagnostics/io D-01 同源） |
| RobWork 数学 | `rw::math`（`sdurw_math`，经 core 公共头传递） | reporting 不直接包含 rw::math/Eigen 头（本单元契约不含几何数值类型；数值经 SourcedValue/Quantity 承载） |
| JSON/CSV/ZIP | io 提供 CSV/JSON 读写器与 ZIP 通道（io.md §5/§7，vcpkg 第三方经 io 引入） | reporting **不引入第二套编码器**（SA-12）：CSV/JSON 写出与原子替换一律经 io 设施（注入形态见 P-RPT-1）；证据包 ZIP 封装经注入的 IArchiveWriter（§9.6，io 适配） |
| 异常 | RobWork 惯例异常 | reporting 用自有 `ReportError`（§3.5）；可恢复查询路径提供 `try*` 非抛出变体 |
| 测试框架 | dtb §5.5 定稿：vcpkg gtest，`find_package(GTest CONFIG REQUIRED)` | `sdurws_ird_reporting_test`/`_contract_test` 按此接入 |

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有总表（本节先行给出，全文以此为准）

**reporting 拥有（实现责任方）**：

| # | 能力 | 上游依据 | 设计落点 |
| --- | --- | --- | --- |
| O-1 | ReviewReport 对象（身份/版本/内容身份/章节列表/元数据/不可变保证） | RPT-01、CON-02 精神 | §4 |
| O-2 | 报告章节与段落模型（章节 ID/版本/来源/证据绑定/排序/适用性） | RPT-01、NFR-COR-04 | §4.3、§5.3 |
| O-3 | 报告级版本、内容身份（dataIdentity/contentIdentity）与模板/章节模型版本 | RPT-01、NFR-COR-02 精神 | §4.2、§4.4 |
| O-4 | 证据、结果和诊断在报告中的引用关系（引用结构冻结——project §13.2 交接的承接） | NFR-COR-04、RPT-01 | §4.3、§6 |
| O-5 | RPT-01-B/RPT-01-C 范围选择与章节契约（B/C 章节清单、缺正式结果默认不选、C 级降级/拒绝规则） | RPT-01-B/C、§16 验收要点 | §5 |
| O-6 | HTML/JSON/CSV 渲染器（报告语义与格式映射；单一数据源） | RPT-02、AT-22 | §8 |
| O-7 | 多格式一致性校验（逐字段） | RPT-02、AT-22 | §8.5 |
| O-8 | 报告导出任务契约（格式选择、外部导出目标、失败保留选择与路径可重试） | RPT-02、§16 验收要点 | §7.4、§9.5 |
| O-9 | 报告工件完整性摘要（ReportArtifactManifest：工件清单＋摘要＋渲染器/模板版本） | RPT-01、project.md §4.1 reports 行 | §7.3 |
| O-10 | 幂等导出与冲突检测契约（内容摘要判定、同路径不同内容拒绝） | RPT-01（"报告目录追加幂等、冲突拒绝"）、project.md D-13/D-14 模式 | §7.4 |
| O-11 | EvidenceBundle 导出的格式与组装（快照引用＋结果引用＋复现要素打包） | RPT-03、NFR-COR-04 | §7.6 |
| O-12 | RPT-05 措辞冻结的渲染规则（限定语保留、两类声明） | RPT-05、EVI-01（消费） | §6.4、§8.2 |
| O-13 | 评审/签署元数据的结构化承载（评审人/时间/依据修订与快照/意见/签署状态/变更记录/改型差异与取舍理由） | RPT-01-C、RPT-04 | §4.5 |
| O-14 | 报告往返复算的比对入口（阶段 C，RPT-06） | RPT-06、AT-22 | §7.7 |
| O-15 | RPT-\* 稳定诊断码族与报告错误码（建议值，码值权威归 diagnostics StableCodeRegistry） | ERR-01（支撑） | §3.5 |

**reporting 消费（经登记边与注入，不拥有）**：

| # | 消费方 → 被消费 | 形态 | 消费内容 | 上游依据 | 状态 |
| --- | --- | --- | --- | --- | --- |
| C-1 | reporting → core | 接口依赖（编译链接；ARCH §3.5 登记边） | 身份类型全族、ContentVersion/ContentIdentity/ContentDigester、SourcedValue/ValueProvenance、EvaluationMode/TaskOutcome/EngineeringStatus 词表、DiagnosticRecord/ComparativeFields、UnitToken/Quantity、CoreError | ARCH §3.5 | core.md v0.1 Draft 未冻结（P-RPT-9） |
| C-2 | reporting → evidence | 接口依赖（编译链接；ARCH §3.5 登记边） | ResultEnvelope（只读值）、FormalPassEligibility/ReviewRecordEligibility 纯检查、EvidenceItemStatus/EvidenceManifest（呈现口径）、CurrentnessResult（当前性投影）、ReproductionBlock、checkComparisonBaselinesConsistent（RPT-04 一致基准纯检查） | ARCH §3.5、EVI-01/02、RPT-03/04/05 | evidence.md v0.1 Draft（P-RPT-9） |
| C-3 | reporting → diagnostics | 接口依赖（编译链接；ARCH §3.5 登记边） | DiagProjectionItem/exportSafeSummary（报告诊断章节数据源——双保险脱敏）、StableCodeRegistry 只读访问（deprecated 映射）、IDiagnosticSink（报告过程诊断上报） | ARCH §3.5、ERR-01、NFR-SEC-07 | diagnostics.md v0.1 Draft |
| C-4 | reporting → project | 接口依赖（编译链接；ARCH §3.5 登记边） | IProjectQueryPort（②端口只读：head/tryRevision/currentMetadata/branchTips/listRuns/runDir）、报告工件写入端口（**IReportArtifactSink——本文定义最小契约，project 实现**，D-13/D-14 模式，§9.6） | ARCH §3.5、RPT-01、project.md §10.6/§13.2 | project.md v0.1 Draft；写入端口为其 §13.2 预留交接（P-RPT-4 关联） |
| C-5 | reporting →（注入）io | **运行时注入**（零编译依赖；P-RPT-1，io.md P-IO-1 同案） | ICsvWriter/IJsonWriter（canonical 写出）、IAtomicFileWriter（原子替换＋失败保留先前输出）、IoCancelToken（协作取消）——CSV 方言标识行与可逆编码唯一实现归 io | RPT-02、AT-22、NFR-SEC-03、io.md §10.10/§13.2 | io.md v0.1 Draft；裁决补边前经 L5 装配注入 |
| C-6 | reporting →（注入）runtime | **运行时注入**（零编译依赖；P-RPT-2） | **IModelSummaryProvider**（本文 §9.7 冻结的只读摘要 schema：快照身份块/能力声明/资源清单——不含 RobWork 对象；runtime §10.12/§13.2 预留交接的承接） | RPT-01-B 输入摘要章节、runtime.md §10.12 | runtime.md v0.1 Draft；L5 装配适配 |
| C-7 | reporting →（注入）execution | 运行时注入（只读投影） | 任务完成/失败/进度投影（ITaskScheduler::tryTask／ITaskController::progress——报告章节呈现任务终态与"已中断"）；TaskStatusChanged/ResultArchived 事件（订阅方，事件不持久化） | TASK-02、NFR-REL-03 | execution.md v0.1 Draft |
| C-8 | reporting ←（注册）业务域单元 | 装配期注册（IReportSectionProvider，§9.2） | 各域只读结果投影（model/requirements/kinematics/optimization/trajectory/dynamics/drivetrain/selection 章节内容） | RPT-01-B/C、ARC-02 | 各域卡未产出（§12 交接；阶段 A 用替身） |
| C-9 | reporting ← ui | 入边调用（ui→reporting，ui.md C-14） | ui 预览宿主消费 ReviewReport 只读值＋HTML 片段渲染（预览接口形状本文冻结，P-UI-9 承接）；`report.export` 命令的目标服务 | UX-13、RPT-02 | ui.md v0.1 Draft（阶段 B 承接） |

**reporting 不拥有（其他单元所有，本文不实现、不预建桩）**：

| # | 不拥有内容 | 所有者 | 上游依据 |
| --- | --- | --- | --- |
| N-1 | 项目 HEAD、修订、分支、草稿和锁；报告工件的磁盘发布与提交指针 | project | ARC-01、§17 表注、project.md §4.1/§6.4 |
| N-2 | 工程判定与证据汇总（五级优先级、正式通过五条件、不可行证明校验） | evidence | EVI-01、§8.1 表 2 |
| N-3 | 结果当前性计算（Current/Superseded 判定） | evidence | CON-02/05、SA-07 |
| N-4 | 运动学、轨迹、动力学、选型和优化算法与领域结果对象 schema | 各业务域 | KIN/TRJ/DYN/SEL/OPT 家族 |
| N-5 | 任务状态机和 worker 调度、RunRegistry | execution | TASK-01～03 |
| N-6 | 外部资源解析与固化检测 | io/project | NFR-SEC-01、CON-03 |
| N-7 | UI 对话框、报告预览宿主、评审签署交互界面 | ui | RPT-01-C（交互部分）、ui.md N-8 |
| N-8 | `.rwpack` 编解码实现与项目包导出导入编排 | io/project | PM-05、io.md §7 |
| N-9 | 业务结果缓存 | execution（治理） | CON-04 |
| N-10 | CSV/JSON 编解码器与转义实现（唯一实现归 io） | io | NFR-SEC-03、SA-12 |
| N-11 | 测试报告（TestRecord/`ird-test-report.json`——与 ReviewReport 数据契约无共享） | testkit | testkit.md §2.3/§7.3 |

### 2.2 直接承接的需求（reporting 为实现责任方或数据契约责任方之一）

| 需求 | reporting 承接的部分 | 不可越界的部分 |
| --- | --- | --- |
| RPT-01/RPT-01-B/RPT-01-C（ReviewReport 两级；只读明确 ID；目录追加幂等、冲突拒绝） | ReviewReport 模型与 B/C 章节契约、只读构造（明确修订/快照/结果 ID）、幂等/冲突判定契约（内容摘要判定）；`<report-id>` 分配 | reports/ 磁盘布局与发布（project）；各章节业务内容（域投影） |
| RPT-02（HTML 首版，可附 JSON/CSV，多格式逐字段一致） | 三格式渲染器、字段矩阵单次投影、逐字段一致性校验、确定性渲染 | 编解码器实现（io）；PDF（附录 B 裁决排除，P-RPT-6） |
| RPT-03（证据包导出） | EvidenceBundle 格式与组装（快照/结果引用/复现要素） | 复现要素的产生（evidence 复现块）；ZIP 字节封装（io） |
| RPT-04（方案/设计变体报告，一致基准） | 变体章节模型（variant-diff）＋消费 evidence checkComparisonBaselinesConsistent 的纯检查结果；基准不一致拒绝 | 基准判定的计算（evidence §6.5）；Model Diff 数据实体（modeling MDL-08） |
| RPT-05（措辞冻结；两类声明） | 渲染规则：限定语保留映射、FormalPass/ReviewRecord 两类声明呈现（消费 evidence 资格检查结果） | 资格判定本身（evidence 纯检查）；界面显示纪律（ui——其 §6.3 同样消费五条件） |
| RPT-06（报告往返复算，阶段 C） | 复算比对入口与"不一致时报告不可用于正式结论"的标记 | 复算执行（③端口评估器；调用方=workflow/各域）；判定阈值（evidence/附录 D） |
| NFR-COR-04（结论定位到快照与证据） | 报告内追溯链呈现（envelope→snapshotId→对象引用；证据项绑定） | 追溯链的计算（evidence §7.1） |
| EVI-01（消费） | 模式效力与五级汇总结果的忠实呈现（不弱化不省略）；DataInsufficient 缺项全量显示 | 汇总判定（evidence §6.4） |
| EVI-02（消费） | 必验工况覆盖矩阵的报告呈现；变体章节基准一致性拒绝 | 覆盖判定与基准检查（evidence §6.5/§6.6） |
| TASK-02（消费） | 取消/失败/中断结果不进入正式通过章节的呈现侧强制 | envelope 构造校验（evidence SA-13） |
| CON-02（消费） | 三轴正交的报告呈现（结果状态/工程判定/当前性分列，不互相推导） | 三轴计算 |
| CON-03（协作） | 报告引用外部资源的状态呈现（Recorded/Solidified——输入摘要与外部验证边界章节） | 固化判定与存储（evidence/project） |
| CON-05（消费） | 报告绑定切片/快照身份的呈现 | 切片计算（evidence） |
| PM-05（协作） | 报告工件随包导出的可勾选性（reports 树镜像——io/project 已承载）；包内报告引用完整性（project §8.9 勾选扫描） | 包编解码与发布（io/project） |
| PM-07（消费） | 只读项目：项目内 reports/ 发布拒绝（project 写入口检查）＋导出到项目外允许（ui readOnlyAllowed 口径） | 写权限判定（project §6.8） |
| PM-08（消费） | 未完成报告工件不列入清单（manifest 缺失＝不完整——project D-13 模式的报告侧对齐） | 恢复扫描（project） |
| OPT-12（协作） | 报告生成规则共用（CSV/JSON canonical、原子导出、限定语纪律）；EvidenceBundle 组装复用 | 优化专属导出清单与 Markdown 证据报告内容（optimization） |
| UX-02/03（消费） | 报告为工程用语（不显示哈希/Schema/内部插件名——身份经规范文本仅在追溯区块呈现）；比较型三要素与"不适用"显式标记的报告呈现 | 界面显示（ui） |
| NFR-SEC-03（消费） | CSV 导出转义经 io 唯一实现；结构化证据保留原始值 | 转义实现（io） |
| NFR-SEC-07（消费） | 报告诊断章节经 exportSafeSummary 双保险脱敏；导出物不含未脱敏本机路径 | 脱敏设施（diagnostics） |
| NFR-MNT-03/04（约束） | 报告状态词/单位换算/文案消费唯一权威（core/ui/diagnostics）；无转发包装器 | — |

### 2.3 明确不做（非目标，与 §2.1 不拥有表互为补充）

不实现第二套结果数据源或报告自算工程判定（一切结论来自 envelope＋evidence 资格检查）；不重新计算当前性（消费 CurrentnessResult 投影；HEAD 前进仅触发重新查询、不改写已冻结内容）；不实现 PDF 渲染或预留 PDF 接口桩（附录 B v1.3 显式裁决"未立项、不留接口桩"——P-RPT-6 登记）；不创建逐业务域的转发包装器（域直接实现 IReportSectionProvider，NFR-MNT-04）；不预建空章节、空页面或占位模块（TRJ-08 同精神——缺正式结果的章节默认不选并显示缺项，而非空壳）；不绕过 project 写正式项目目录（含 reports/——一切项目内写入经 IReportArtifactSink）；不复制或恢复 `old/` 历史实现；不在报告层新增证据等级、判定阈值或当前性状态（EvidenceItemStatus 消费 P-EV-8 词表；当前性仅 Current/Superseded＋不可判定计算形态）；不把报告显示状态（如"已生成"）当作工程真值；不以文件存在当作报告已提交。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 组成（公共头模块）

reporting 由 12 个公共头模块＋1 个编译实现组成（实现文件随 §11 任务落地，头为契约权威）：

| 头（`reporting/include/sdurws/ird/reporting/`） | 内容 | 详见 |
| --- | --- | --- |
| `Errors.hpp` | ReportErrorCode（稳定 token）、ReportError、RPT-\* 稳定诊断码建议清单 | §3.5 |
| `Identity.hpp` | ReportId（`rpt-<32hex>`，reporting 层类型待 core 收编——P-RPT-3）、PublishedReportRecord、ReportLevel | §4.1 |
| `ReportModel.hpp` | ReportSourceSpec、ResultRefSnapshot、EvidenceRefEntry、DiagRefEntry、CurrentnessSnapshot、FieldValue/QualifierToken、ReviewReportSection、ReviewMetadata、ReviewReport、ReportIdentity（dataIdentity/contentIdentity）、UnitPreference | §4 |
| `Sections.hpp` | SectionId 词表（B/C 章节契约）、SectionSelection、SectionStatus、SectionRegistry | §5 |
| `SectionProvider.hpp` | IReportSectionProvider、SectionRequest/SectionContent/SectionEntry/ResultBinding/EvidenceBinding/JumpTarget | §9.2 |
| `Builder.hpp` | IReviewReportBuilder、ReportBuildRequest/Outcome、ReportBuildSession 状态 | §7.1、§9.1 |
| `Render.hpp` | IReportRenderer、ReportRenderFormat、RenderArtifact、FieldMatrix/FieldCell、RenderOptions | §8、§9.3 |
| `Consistency.hpp` | IReportConsistencyChecker、ConsistencyInput/Result/FieldMismatch | §8.5、§9.4 |
| `Export.hpp` | IReportExportService、ReportExportRequest/Result、ExportDestination/ReplacePolicy | §7.4、§9.5 |
| `Archive.hpp` | IReportArchiveCoordinator、ReportArtifactManifest、IReportArtifactSink（project 实现的注入契约）、ArtifactSessionRef/Status | §7.3、§9.6 |
| `Bundle.hpp` | EvidenceBundleRequest/Manifest、IEvidenceBundleAssembler、IArchiveWriter（io 适配的注入契约） | §7.6、§9.6 |
| `ModelSummary.hpp` | IModelSummaryProvider、ModelSummary（runtime 只读摘要 schema——runtime §13.2 交接的承接冻结） | §9.7 |
| `README.md` | 既有保留位说明（不参与编译；已指向本文 §9，实测一致） | — |
| `src/`（实现，随 RPT-T01 建） | ReportCodec（canonical 编码）、构建器、字段矩阵提取、三渲染器、一致性检查、导出/归档协调、证据包组装等非模板实现 | §4～§9 |

### 3.2 依赖（含 core/evidence 契约消费状态登记）

```
sdurws_ird_reporting ──► RWS::ird::core（PUBLIC：身份/摘要/词表/诊断数据/单位）
                    ──► RWS::ird::evidence（PUBLIC：ResultEnvelope 只读值、资格纯检查、CurrentnessResult、
                    │        ReproductionBlock、基准一致性纯检查、EvidenceItemStatus 呈现口径）
                    ──► RWS::ird::diagnostics（PUBLIC：DiagProjection/exportSafeSummary、StableCodeRegistry 只读、
                    │        IDiagnosticSink）
                    ──► RWS::ird::project（PUBLIC：IProjectQueryPort 只读视图；IReportArtifactSink 注入契约）
                    ──► C++17 标准库
                    ──✖ 零 Qt（D-01）、零 io/runtime/execution/业务单元编译边（ARCH §3.5 未登记——P-RPT-1/2）、
                         零 testkit（产品目标，T-1 红线）、零 Eigen/rw::math 直接包含
运行时注入（零编译依赖）：io 写出设施（CSV/JSON/原子写出/取消令牌——P-RPT-1）、IModelSummaryProvider（runtime——
                    P-RPT-2）、execution 任务投影（ITaskScheduler/ITaskController 只读查询）、
                    IReportSectionProvider 各域实现（L5 装配注册）
```

消费的 core/evidence 契约清单（状态：各卡 v0.1 **Draft 未冻结**，P-RPT-9）：

| 契约 | reporting 用途 | 状态锚点 |
| --- | --- | --- |
| core::ObjectId/ProjectId/BranchId/RevisionId/RunId/AttemptId/TaskIdentity | 报告身份块与结果绑定 | core.md v0.1 §4.1/§5.1 |
| core::ContentVersion/ContentIdentity/ContentDigester | 报告 dataIdentity/contentIdentity 与工件摘要（SHA-256 唯一算法） | core.md v0.1 §4.2/§5.2 |
| core::SourcedValue/ValueProvenance | 字段四态（RPT-05 限定语数据源：估算/未提供/不适用/非法保留原文） | core.md v0.1 §4.3/§5.3 |
| core::EvaluationMode/TaskOutcome/EngineeringStatus | 结果状态呈现（三轴之两轴） | core.md v0.1 §4.7/§5.6 |
| core::DiagnosticRecord/ComparativeFields | 报告诊断引用与三要素呈现 | core.md v0.1 §4.8/§5.7 |
| core::UnitToken/Quantity/convert | 报告数值单位呈现（唯一换算入口；显示单位纯投影） | core.md v0.1 §4.4/§5.4 |
| evidence::ResultEnvelope（只读） | 结果引用快照的字段来源 | evidence.md v0.1 §7.1 |
| evidence::FormalPassEligibility/ReviewRecordEligibility 纯检查 | 两类声明的判定（reporting 消费不重算） | evidence.md v0.1 §7.2 |
| evidence::CurrentnessResult（Current/Superseded＋不可判定形态） | 当前性快照（P-EV-4 口径） | evidence.md v0.1 §8.1 |
| evidence::EvidenceManifest/EvidenceItemStatus（P-EV-8 词表） | 章节证据绑定与状态呈现 | evidence.md v0.1 §6.2 |
| evidence::ReproductionBlock | 证据包复现要素（RPT-03） | evidence.md v0.1 §4.1.2 |
| evidence::checkComparisonBaselinesConsistent | 变体章节基准一致性（RPT-04，纯检查消费） | evidence.md v0.1 §6.5 |

### 3.3 对 io／runtime／execution 能力的注入边界（依赖白名单的实施形态）

ARCH §3.5 依赖表**未登记** reporting→io、reporting→runtime、reporting→execution 边（execution 与 reporting 同层且 execution 依赖面更宽，反向边无必要）。而报告渲染客观上需要 io 的 canonical 写出设施、输入摘要章节需要 runtime 的只读摘要、章节呈现需要 execution 的任务投影。解决方案＝**注入式最小接口＋值传递**，与 evidence.md §3.3（IObjectBytesSource/IRevisionClosureSource）、io.md IO-D02（IoRuntime 注入）同一模式：

- **io 写出设施**：reporting 的渲染器/导出服务经注入的 `IReportIoFactory`（§9.5 定义——**公共头零 io 类型**：工厂接口只出现 reporting/core 类型，创建 CSV/JSON 写出器与原子目标的抽象句柄）获得 io 能力；L5 装配适配（或 P-RPT-1 裁决补边后直接链接，届时签名零改动）。该工厂**不是转发包装器**（NFR-MNT-04）：其边界价值＝隔离未登记编译边＋约束报告只能使用 io 的 canonical/原子写出路径（禁止报告自建第二套编码器——SA-12 的实施件）。CSV 方言行、转义、JSON canonical 全部行为归 io，工厂零行为。
- **runtime 摘要**：`IModelSummaryProvider`（§9.7）返回 reporting 自有的 `ModelSummary` 值（快照身份块/能力/资源清单的**投影 schema**——不含 RobWork 对象，runtime §10.12 口径）。该投影 schema 即 runtime §13.2 要求 reporting 冻结的"RPT-01 输入章节摘要 schema"。
- **execution 投影**：报告章节消费任务终态/进度时经注入的只读查询接口（对齐 execution ITaskScheduler::tryTask/ITaskController::progress 的值形态；reporting 侧定义 `ITaskStatusSource` 最小接口，L5 适配）。事件订阅（TaskStatusChanged/ResultArchived）仅用于预览刷新提示，不用于报告内容（报告内容只绑定明确 ID 的冻结事实）。
- **结果信封读取**：reporting 经注入的 `IReportResultSource`（§9.1 内登记）取得 `evidence::ResultEnvelope` 只读值与当前性投影——适配器由 L5 装配（自 results/<run-id>/ 归档工件解码，解码入口归 evidence/execution，登记 §12 交接）。

### 3.4 命名空间、目标与 CMake 集成

- 命名空间 `sdurws::ird::reporting`；目标 `sdurws_ird_reporting`（骨架 INTERFACE → RPT-T01 升级 STATIC），别名 `RWS::ird::reporting`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_reporting PUBLIC RWS::ird::core RWS::ird::evidence RWS::ird::diagnostics RWS::ird::project)`。
- 测试目标：`sdurws_ird_reporting_test`（单元内，含 ScriptedSectionProvider/ScriptedResultSource 替身）、`sdurws_ird_reporting_contract_test`（跨单元契约：与 core/evidence 值类型往返、与 project 工件汇集座〔fake IReportArtifactSink〕、与 io 写出设施〔P-RPT-1 裁决前经注入 fake〕的协作；gtest 按 dtb §5.5 接入），随 RPT-T01 登记；产品目标不链 testkit（T-1 红线）。
- 头包含形式 `#include <sdurws/ird/reporting/ReportModel.hpp>`；私有实现头不入 `include/`（R-2 纪律）。
- 同层依赖表核对：reporting 登记边仅 core/evidence/diagnostics/project（ARCH §3.5 表内既有边）；io/runtime/execution 为注入（表外零编译边，P-RPT-1/2 登记）；不新增任何表外同层编译边（表外边＝构建失败，SA-10）。

### 3.5 错误类型与稳定诊断码（`Errors.hpp`）

```cpp
enum class ReportErrorCode {              // token 稳定（内部错误语义）
    SourceMissing,        // reporting/source-missing          引用的修订/快照/结果不存在或未 finalize
    SourceAmbiguous,      // reporting/source-ambiguous        来源指定不明确（占位"当前"值）
    ScopeInsufficient,    // reporting/scope-insufficient      C 级请求但全部 C 章节缺正式结果
    LevelConflict,        // reporting/level-conflict          B 级携带 C 章节／章节级别与报告级别冲突
    EvidenceRefInvalid,   // reporting/evidence-ref-invalid    章节证据引用未通过绑定校验
    ConsistencyMismatch,  // reporting/consistency-mismatch    多格式逐字段不一致（AT-22 反例）
    RenderFailed,         // reporting/render-failed           渲染错误（模板/编码层）
    DataInvalid,          // reporting/data-invalid            数据错误（报告字段非法——与 RenderFailed 严格区分）
    ArchiveConflict,      // reporting/archive-conflict        同目标不同内容（幂等判定失败）
    ExportFailed,         // reporting/export-failed           外部导出失败（保留选择与路径可重试）
    DiskFull,             // reporting/disk-full               磁盘不足（工件写出失败）
    ReadOnlyStore,        // reporting/read-only-store         只读项目拒绝项目内发布
    RoundtripMismatch,    // reporting/roundtrip-mismatch      往返复算不一致（RPT-06——报告不可用于正式结论）
    BundleIncomplete,     // reporting/bundle-incomplete       证据包要素缺失
    TemplateVersion,      // reporting/template-version        模板/章节模型版本不兼容
    Usage                 // reporting/usage                   调用方违约
};
class ReportError : public std::runtime_error {
public: ReportError(ReportErrorCode, std::string detail);
        ReportErrorCode code() const noexcept;   // detail 前缀 "reporting/<域>:"，面向开发诊断
};
```

RPT-\* 稳定诊断码建议清单（**码值权威＝diagnostics StableCodeRegistry，未收编前为建议值**，P-RPT-8 关联）：`RPT-SOURCE-MISSING`（章节缺正式结果/缺项——Error）、`RPT-SCOPE-INSUFFICIENT`（C 级降级建议——Warning）、`RPT-CONSISTENCY-MISMATCH`（Error）、`RPT-ARCHIVE-CONFLICT`（Error）、`RPT-EXPORT-FAILED`（Error，附可重试动作）、`RPT-ROUNDTRIP-MISMATCH`（Error）、`RPT-CURRENTNESS-UNEVALUABLE`（Warning——当前性不可判定呈现，P-EV-4 关联）、`RPT-SECTION-NOT-APPLICABLE`（Info——不适用章节显式标记）。分类/严重为建议值，随 diagnostics 收编冻结。

---

## 4. ReviewReport 数据模型

### 4.1 身份、版本与三个"报告身份"概念的区别（先于字段表冻结）

| 概念 | 类型/载体 | 回答的问题 | 谁产生 | 与其他概念的边界 |
| --- | --- | --- | --- | --- |
| ReportId | `ReportId`（Id128 形，tag `rpt-`；reporting 层类型，**待 core 收编**——P-RPT-3，diagnostics FindingId `fnd-` 同案） | "这是哪一份报告对象" | reporting（构建成功时一次分配） | 不承载内容信息；同数据源重建报告＝新 ReportId（幂等判定不用它，用内容身份） |
| reportVersion | `std::uint32_t`（≥1） | "同一数据基准上的第几版评审演化" | reporting（沿 supersedes 链递增） | 评审元数据修改→新报告对象（新 ReportId）＋reportVersion+1；数据源不变（dataIdentity 相等） |
| dataIdentity（数据源身份） | `core::ContentIdentity` | "报告基于哪组冻结输入（修订/快照/结果引用集/级别/工况范围/单位选择）" | reporting 计算（ReportCodec-Data 规范字节摘要） | **不含**评审元数据与渲染信息——证明"同一数据基准"的凭据（评审演化链上不变） |
| contentIdentity（内容身份） | `core::ContentIdentity` | "报告全部语义内容（数据源＋章节骨架＋评审元数据＋章节模型版本）的字节摘要" | reporting 计算（ReportCodec-Full） | 幂等导出与冲突判定的**唯一**依据；与文件路径、显示名称、ReportId 分离 |
| 模板/章节模型版本 | `sectionModelVersion`（`ird-report-section-model/1`）＋各格式模板版本（`ird-report-html/1` 等） | "按哪一版章节契约/模板呈现" | reporting（随版本演进走本文变更记录） | 入 contentIdentity（章节结构属语义内容）；渲染器版本入工件清单（RenderArtifact），不入报告身份 |

**身份纪律**（evidence §5.1 同源）：四者互不替代——ReportId 不用于幂等判定（同 ReportId 不同内容＝冲突，见 §7.4）；dataIdentity 相等而 contentIdentity 不同＝同数据基准的评审演化；内容身份与磁盘路径（`reports/<report-id>/`）和显示名称（报告标题）分离——重命名/移动不改变任何身份。摘要一律 SHA-256 经 core ContentDigester（唯一算法）；比较用字节等值（附录 D 第 12 项精神）；浮点值以 IEEE754 双精度位模式入编码、NaN/±Inf 在编码入口拒绝（evidence D-06 同源）。

### 4.2 ReviewReport 字段表（`ReportModel.hpp`；全部字段构造后不可变，无 setter）

| 字段 | 类型 | 必填 | 默认 | 约束与语义 |
| --- | --- | --- | --- | --- |
| `reportId` | ReportId | 是 | — | 构建成功时分配；规范文本 `rpt-<32hex>` |
| `level` | ReportLevel（`B`/`C`，token `level-b`/`level-c`） | 是 | — | 报告级别（RPT-01 分级子级）；合法组合见 §4.6 |
| `project / branch / revision` | core::ProjectId/BranchId/RevisionId | 是 | — | 身份三元组——**明确 ID**，禁止"当前/tip"占位值（SourceAmbiguous 拒绝）；revision 是报告对象解析锚 |
| `revisionSeq` | std::uint64_t | 是 | — | 展示排序用（不参与身份语义） |
| `snapshotId / inputSliceId` | std::optional\<core::ContentIdentity\> | 否 | nullopt | 报告级统一快照/切片（多结果共享同一快照时填写）；逐结果各自的快照/切片在 ResultRefSnapshot 内——两者一致或逐结果（构建校验） |
| `resultRefs` | std::vector\<ResultRefSnapshot\>（§4.3.1） | 是（≥1） | — | 报告绑定的**明确结果集**（RunId 列表解析而来）；全部为已 finalize 运行（listRuns 口径） |
| `evidenceRefs` | std::vector\<EvidenceRefEntry\>（§4.3.2） | 是（可空集） | {} | 证据引用汇总（逐章节证据绑定的去重并集，含来源章节） |
| `diagRefs` | std::vector\<DiagRefEntry\>（§4.3.3） | 是（可空集） | {} | 诊断引用汇总（诊断章节与各章节诊断的并集） |
| `currentnessSummary` | ReportCurrentnessSummary（§6.3） | 是 | — | 报告生成时刻的当前性快照汇总（生成时冻结，之后不改写） |
| `coverageSummary` | CaseCoverageSummary | 是 | — | 必验工况覆盖摘要（消费 evidence 覆盖矩阵的呈现投影：Executed/NotExecuted/Invalid/NotApplicable/Failed 计数＋必验总数） |
| `externalResourceSummary` | std::vector\<ExternalResourceStateEntry\> | 是（可空集） | {} | 外部资源状态（CON-03 呈现：resourceId＋Recorded/Solidified＋来源）——外部验证边界章节数据 |
| `reproduction` | evidence::ReproductionBlock | 是 | — | 复现要素（evidence §4.1.2 值拷贝；RPT-03 证据包与 NFR-COR-04 追溯的数据源） |
| `sections` | std::vector\<ReviewReportSection\>（§4.3.4） | 是（≥1） | — | 章节列表，按 §5 冻结顺序稳定排列 |
| `review` | ReviewMetadata（§4.5） | 是 | — | 评审/签署元数据（C 级必含签署块；B 级可为空元数据） |
| `unitPreference` | UnitPreference | 是 | SI | 报告数值单位选择（冻结入身份——同报告重渲染字节一致的确定性前提；KIN-12 显示单位纯投影的报告侧冻结点） |
| `generatedAtUtc / generatedBy / generatorVersion` | time_point / std::string（principal 或 `batch`）/ std::string | 是 | — | 生成时间与生成者（principal 采集归 ui；报告内为受控字段——同 diagnostics ConfirmationCredential 口径，不写日志） |
| `supersedes / reportVersion` | std::optional\<ReportId\> / std::uint32_t | 否/是 | nullopt/1 | 评审演化链（§4.5）；`supersedes` 指向被本版取代的报告 |
| `dataIdentity / contentIdentity` | core::ContentIdentity | 是 | — | 由构建器计算（§4.4），调用方不可申报 |
| `sectionModelVersion` | std::string（`ird-report-section-model/1`） | 是 | — | 章节契约版本（入 contentIdentity） |

**生成状态与归档状态的表达方式**（任务约束§五.1 的明确项）：ReviewReport 是**纯值对象**，不含可变状态字段——①"生成状态"属于**构建会话**（ReportBuildSession 状态机，§7.1）：生成失败/取消**不产生报告对象**（不存在"半份报告"）；构建成功即冻结（`frozen` 是类型系统事实：无 setter＋值语义）。②"归档状态"属于**工件清单**（project 侧 `report.json`，D-13 模式）：Staged（部分工件，未 finalize）→ Finalized（manifest 原子发布＝完整）。内存侧由 `PublishedReportRecord`（§7.3）承载发布事实——报告值对象永不因发布回写。

**合法实例**：分支 tip 修订 r7 上、绑定 3 个已 finalize 的 Verified 结果、B 级、8 个 B 章节全部 Populated/显式缺项、复现块完整——`contentIdentity` 非零。**非法实例**（构建器拒绝，抛 ReportError）：revision 为空/占位；resultRefs 为空或含未 finalize 运行；B 级携带 C 专属章节（LevelConflict）；C 级且全部 C 章节未选中（ScopeInsufficient，§5.4）；章节证据引用的 snapshotId 与所属结果 envelope 不一致（EvidenceRefInvalid）；字段值 NaN/非有限（DataInvalid）。

### 4.3 引用类型字段表

#### 4.3.1 ResultRefSnapshot（结果引用快照）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `runId / task` | core::RunId / core::TaskIdentity | 是 | 明确运行与五元组（TASK-03 呈现） |
| `evaluationKey / evaluatorContractVersion` | evidence::EvaluationKey / uint32 | 是 | 评估域与契约版本（追溯） |
| `mode` | core::EvaluationMode | 是 | Preview 结果**拒绝进入报告**（表 1：不产生正式证据与结果对象——构建边界拒绝）；Quick 结果可进入但**永远不满足** FormalPass（呈现"筛选级"限定语） |
| `outcome / engineeringStatus` | core::TaskOutcome / core::EngineeringStatus | 是 | 三轴之两轴（当前性在 currentness 字段，不混排） |
| `snapshotId / sliceId / inputBaselineId` | core::ContentIdentity ×3 | 是 | envelope→snapshot 追溯链（NFR-COR-04 报告侧锚点） |
| `caseScope` | std::vector\<core::ObjectId\> | 是 | 该结果覆盖的工况集（⊆ 快照 caseSet——构建校验） |
| `eligibility` | EligibilitySnapshot | 是 | 两类资格的**检查结果快照**：{formalPass: bool, reviewRecord: bool}——由 evidence 纯检查计算，reporting 只记录与消费（§6.2） |
| `currentness` | CurrentnessSnapshot（§6.3） | 是 | 生成时刻当前性（冻结） |
| `producedIn / productVersion` | evidence::ProducerInfo 摘要 | 是 | 产生侧信息（主/工作进程＋版本） |

#### 4.3.2 EvidenceRefEntry（证据引用）

| 字段 | 类型 | 必填 | 约束 |
| --- | --- | --- | --- |
| `itemId / itemClass` | std::string / evidence::EvidenceItemClass | 是 | 证据项 ID（如 `kin.ik-convergence-per-point`）与类别（Common/Required/Suggested——表 4 语义） |
| `status` | evidence::EvidenceItemStatus | 是 | Satisfied/Missing/Invalid/Unverified/NotApplicable（P-EV-8 实现承载词表——reporting 消费不新增） |
| `artifactDigest` | std::optional\<core::Digest256\> | Satisfied 必填 | 产物摘要（绑定校验通过方为 Satisfied——evidence §6.2 口径的呈现） |
| `caseScope / subject` | std::vector\<core::ObjectId\>（可选）/ std::optional\<core::ObjectId\> | 否 | 工况范围与对象范围（多工况覆盖显示，§6.5） |
| `notApplicableReason / invalidReasonCode` | std::optional\<std::string\> | N/A 必填 / Invalid 必填 | 不适用原因（C2 口径——"因不可行而不适用"等）；Invalid 附诊断码引用 |
| `sourceSection` | std::string（SectionId） | 是 | 来源章节（引用关系可反向导航） |

#### 4.3.3 DiagRefEntry（诊断引用）

| 字段 | 类型 | 必填 | 约束 |
| --- | --- | --- | --- |
| `code / severity / category` | DiagCode / diagnostics::DiagnosticSeverity / DiagnosticCategory | 是 | 稳定码与分类（码表元数据；reportable=false 的码**不进报告**——diagnostics §4.5） |
| `subject / localName / runtimeName` | std::optional\<core::ObjectId\> / std::optional\<std::string\>×2 | 否 | 对象定位（ERR-01） |
| `comparison` | std::optional\<core::ComparativeFields\> | 比较型必填 | 三要素（UX-03） |
| `occurrences` | std::size_t | 是 | 去重计数（diagnostics §6.4——聚合不吞缺失项） |
| `sourceSection / sourceRun` | std::string / std::optional\<core::RunId\> | 是/否 | 来源章节与来源运行（envelope 内诊断携带） |

**诊断脱敏双保险**：报告诊断引用的字段来自 diagnostics 的 DiagProjection/exportSafeSummary（值拷贝、不含原始上下文文本）；渲染时再次过 IRedactionService 口径的脱敏（diagnostics §8.10"输出前强制再过一遍脱敏——双保险"）。用户级报告**不含**调用栈、内存地址、未脱敏路径（NFR-SEC-07/REL-05）。

#### 4.3.4 ReviewReportSection（章节）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `sectionId / sectionVersion` | std::string（§5 词表）/ std::uint32_t | 是 | 章节 ID（稳定 token）与章节契约版本（提供方注册时声明；入 contentIdentity） |
| `selected` | bool | 是 | 是否选入本报告（缺正式结果默认 false——§16 验收要点） |
| `status` | SectionStatus | 是 | Populated／NoFormalResult（缺项，附 missing 清单）／DataInsufficient（附缺失项全量清单）／NotApplicable（附原因）——§5.3 |
| `sourceObjects` | std::vector\<core::ObjectId\> | 是（可空集） | 来源对象（章节引用的项目对象——追溯与跳转） |
| `sourceResults` | std::vector\<core::RunId\> | 是（可空集） | 来源结果（框架章节可为空——如 project-scheme） |
| `entries` | std::vector\<SectionEntryView\> | Populated 必填 | 章节条目（§9.2 SectionEntry 的冻结视图：字段/结果绑定/证据绑定/工况范围/跳转目标） |
| `missingItems` | std::vector\<MissingItemView\> | NoFormalResult/DataInsufficient 必填 | 缺项全量清单（itemId＋原因——不因首个缺失短路，表 2④ 呈现口径） |
| `currentness` | std::optional\<CurrentnessSnapshot\> | 条件 | 章节引用历史结果的当前性（引用 Superseded 结果时必填——§6.3） |
| `diagnostics` | std::vector\<DiagRefEntry\> | 是（可空集） | 章节诊断 |
| `eligibilityNote` | std::optional\<EligibilityNote\> | 否 | 章节级资格说明（该章节结论可否渲染"正式通过"/"评审记录"——由所引结果资格聚合） |
| `renderHint` | RenderHint | 是 | 渲染提示（表格/曲线引用/差异表/元数据块——§8.1） |
| `order` | std::uint16_t | 是 | 排序（§5 冻结顺序——渲染与 CSV/JSON 输出序唯一依据） |

### 4.4 内容身份计算（ReportCodec）

| 身份 | 计算对象（canonical 编码） | 进入编码的字段 | 排除的字段 |
| --- | --- | --- | --- |
| `dataIdentity` | ReportCodec-Data | 身份三元组＋revisionSeq＋level＋snapshotId/sliceId＋resultRefs 的 (runId, snapshotId, sliceId, inputBaselineId, caseScope) 集＋unitPreference＋选中章节的 (sectionId, selected) 集 | 评审元数据、entries 内容、诊断、生成时间/者 |
| `contentIdentity` | ReportCodec-Full | dataIdentity 全部字段＋全部章节完整内容（entries/missingItems/status/diagnostics）＋ReviewMetadata＋sectionModelVersion | —（生成时间/生成者不入：同内容重建身份一致——幂等判定的基础；生成信息记录于工件清单） |

编码规则（evidence §5.2 同源纪律）：确定性二进制（magic `IRDRPT1`/`IRDRPTD1`＋字段按名序/章节按 order＋长度前缀＋大端）；presence 字节显式编码 optional；字符串 UTF-8 禁 NUL；浮点位模式；**编码器版本号入编码**（升版＝全体报告身份变化，走设计变更评审）；纯函数（同输入同字节，NFR-COR-02）。往返契约：`parse(encode(x))==x`（RPT-T02 单测锁定）。

### 4.5 ReviewMetadata（评审/签署元数据，RPT-01-C/RPT-04）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `basisRevision / basisSnapshot` | core::RevisionId / std::optional\<core::ContentIdentity\> | 是 | 依据修订与依据快照（=报告数据源身份的复核字段——渲染时校验与 dataIdentity 一致） |
| `reviewer / reviewedAtUtc` | std::optional\<std::string\> / std::optional\<time_point\> | C 级条件 | 评审人（principal——ui 采集，报告内受控字段不写日志）与评审时间 |
| `comments` | std::vector\<ReviewComment\> | 是（可空集） | 评审意见（{author, atUtc, text≤4KiB, sectionId?}——逐条不可变，追加产生新报告版本） |
| `signOff` | SignOffState | 是 | `Unsigned`（默认）／`Signed{signer, signedAtUtc, statement digest}`——签署**不改变历史结果**（仅元数据事实） |
| `variantDiff` | std::optional\<VariantDiffBlock\> | C 级条件 | 改型差异引用（RPT-04：{baselineRevision, candidateRevision, modelDiffRef(ObjectId), baselineCheckResult（evidence 基准一致性纯检查结果）, tradeOffs[]（取舍理由——逐条 {topic, rationale, sectionId?}）}） |
| `changeLog` | std::vector\<ReportVersionEntry\> | 是 | 变更记录（supersedes 链摘要：{reportId, reportVersion, atUtc, actor, reason}） |

**可修改性与新版本**：ReviewReport 不可变——评审元数据的任何修改（追加意见/签署/改型差异）＝构建**新报告对象**：新 ReportId、`supersedes=旧 ReportId`、`reportVersion=旧+1`、dataIdentity **不变**（数据基准未变——构建器校验 resultRefs/level/units 与旧版一致，不一致则拒绝并要求按新报告处理）、contentIdentity 变化（元数据入身份）。**未签署报告可以导出**（签署状态是呈现事实，非导出门禁——上游无"未签署禁导出"条款；正式结论门禁是 eligibility 而非签署）；签署仅记录事实，不赋予也不撤销任何结果的工程资格。

### 4.6 报告级别与章节合法组合矩阵

| 组合 | 判定 | 说明 |
| --- | --- | --- |
| B 级 ∧ 仅 B 章节词表 | ✔ 合法 | §5.1 词表内任意选择 |
| B 级 ∧ 含 C 专属章节（trajectory-cycle 等 6 个） | ✗ LevelConflict 拒绝 | **B 级报告不能伪造 C 级章节**（任务约束§五.2） |
| C 级 ∧ B＋C 章节任意合法选择 | ✔ 合法 | C 章节可因缺正式结果不选（显示缺项） |
| C 级 ∧ 零 C 章节选中 | ✗ ScopeInsufficient 拒绝（附降级建议 B 的诊断 RPT-SCOPE-INSUFFICIENT） | 全部 C 章节缺正式结果＝C 级无意义（§5.4） |
| 任意 ∧ Preview 结果进入 resultRefs | ✗ SourceMissing 拒绝 | 表 1：Preview 不产生结果对象 |
| 任意 ∧ outcome∈{Canceled,Failed,Interrupted} 结果进入**正式结论条目** | ✗ 构建边界拒绝（该类结果仅可出现在诊断/状态呈现，不构成章节结论——§6.2） | TASK-02 呈现侧强制 |
| 任意 ∧ 章节证据引用与所属结果 envelope 绑定不符 | ✗ EvidenceRefInvalid | 防引用错位 |

**报告级别与业务阶段的关系**（任务约束§五.2）：B 级首用于阶段 B（ARCH §10.1：B 启用 reporting(RPT-01-B)），C 级自阶段 C 起验收（RPT-01-C）——阶段 B 交付的软件**不允许**生成 C 级报告（C 章节提供方未注册，构建即 ScopeInsufficient/LevelConflict——结构性防伪造，而非运行期开关）；**报告级别≠证据等级**（证据等级＝评估模式效力分层，§8.1 表 1，归 evidence——B 级报告同样可以呈现 Verified 结果，C 级报告也可能包含 Quick 筛选结果〔永远带"筛选级"限定语〕）。

**空章节、缺失结果与不适用章节的表达**（任务约束§五.1）：空章节＝`selected=false`＋`status=NoFormalResult`＋missingItems（**不是**省略章节——章节骨架仍在词表内呈现"未选择（缺正式结果）"）；缺失结果＝结果引用根本不进入 resultRefs（构建前用户选择即受 listRuns 事实约束——不存在引用未 finalize 运行的合法路径）；不适用章节＝`status=NotApplicable`＋原因（如纯关节路径对段内 IK 连续性——C2 例；"—"呈现，ERR-01 口径）。渲染层"—"仅用于 NotApplicable/无可选值，**不得**用于掩盖缺失（缺失一律显示缺项清单）。

### 4.7 ReviewReport 对象关系图

```text
                              ┌────────────────────────────────────────────────┐
                              │ ReviewReport（不可变值对象）                     │
                              │  reportId · level(B/C) · reportVersion          │
                              │  dataIdentity ──┐   contentIdentity ──┐         │
                              │  (数据源身份)    │   (内容身份)        │         │
                              │                 │                    │         │
                              │  project/branch/revision(明确 ID) ←── │锚定     │
                              │  snapshotId?/inputSliceId?            │         │
                              │  unitPreference · sectionModelVersion │         │
                              └───────┬─────────────────┬─────────────┬────────┘
                                      │                 │             │
        ┌─────────────────────────────┘                 │             └────────────────────┐
        ▼                                               ▼                                   ▼
┌──────────────────┐   ┌───────────────────────────┐ ┌──────────────────────┐ ┌────────────────────┐
│ ResultRefSnapshot│   │ ReviewReportSection[]     │ │ ReviewMetadata       │ │ ReportCurrentness- │
│  runId·task(五元组)│  │  sectionId·sectionVersion │ │  basisRevision/      │ │ Summary（汇总）     │
│  mode·outcome·   │   │  selected·status          │ │  basisSnapshot       │ │  per-result 快照    │
│  engineeringStatus│  │  sourceObjects[]──────────┼─┤  reviewer·comments[] │ │  (Current/         │
│  snapshotId/     │◄──┤  sourceResults[] ─────────┼─┤  signOff(Unsigned/   │ │   Superseded/      │
│  sliceId/        │   │  entries[]                │ │   Signed)            │ │   不可判定形态)     │
│  inputBaselineId │   │   ├ FieldValue(SourcedValue+Unit)                 │ │  evaluatedAgainst  │
│  caseScope[]     │   │   ├ ResultBinding(runId+fieldPath)                │ │  computedAtUtc     │
│  eligibility{    │   │   ├ EvidenceBinding → EvidenceRefEntry             │ └────────────────────┘
│   formalPass,    │   │   ├ CaseScope·JumpTarget                        │
│   reviewRecord}  │   │  missingItems[]（全量）                          │
│  currentness ────┼──►│  diagnostics[] → DiagRefEntry                   │
└──────────────────┘   │  renderHint·order（冻结顺序）                    │
                       └───────────────────────────────────────────────┘
   追溯链（NFR-COR-04 呈现）：报告条目 → ResultBinding.runId → ResultRefSnapshot.snapshotId/sliceId
        →（evidence 侧）快照对象闭包/依赖清单 → ObjectId（对象定位）；证据项 → itemId+artifactDigest+caseScope
```

关系约束：每章节 `sourceResults ⊆ report.resultRefs`（构建校验）；每 EvidenceRefEntry 的 caseScope ⊆ 所属结果 caseScope；sections 按 order 严格递增且 sectionId 唯一；supersedes 链全局无环（构建器沿链校验）。

---

## 5. RPT-01-B／RPT-01-C 分级与章节契约

### 5.1 章节词表（SectionId 冻结；`ird-report-section-model/1`）

| # | SectionId（token） | 章节 | 级别 | 内容所有者（IReportSectionProvider 注册方） | 主要数据源 |
| --- | --- | --- | --- | --- | --- |
| 1 | `project-scheme` | 项目与方案 | B | **框架**（reporting 内建） | query 端口：currentMetadata（分支表/方案清单/显示名）＋修订摘要 |
| 2 | `input-summary` | 输入摘要 | B | **框架** | ModelSummary（runtime 注入）＋快照身份块＋策略内容身份＋AnalysisConfiguration 引用＋外部资源状态 |
| 3 | `model` | 模型 | B | modeling（阶段 B 注册） | RobotDesign 对象投影（权威模式/轴数/限位/安装姿态/物性来源标记） |
| 4 | `requirements` | 需求 | B | requirements | 任务点/区域/工况定义与就绪摘要（REQ-06 口径） |
| 5 | `kinematics-collision` | 运动学与碰撞 | B | kinematics | KIN-01~05 结果投影（三态/覆盖率/碰撞证据） |
| 6 | `optimization-candidates` | 静态优化候选 | B | optimization | OPT-B 候选与三项静态指标（其余显示"—"，OPT-07） |
| 7 | `diagnostics` | 诊断 | B | **框架** | DiagRefEntry 汇总＋exportSafeSummary |
| 8 | `external-validation-boundary` | 外部验证边界 | B | **框架** | 外部资源状态（Recorded/Solidified）＋复现块＋估算来源统计（RPT-05"外部验证未完成"限定语的载体） |
| 9 | `trajectory-cycle` | 轨迹与节拍 | C | trajectory | TRJ-01~06 结果投影（分段/节拍/失败段） |
| 10 | `dynamics-envelope` | 动力学曲线、峰值和 RMS | C | dynamics | DYN-01~07 投影（类型化广义力/峰值窗/RMS/包络） |
| 11 | `drivetrain-operating-points` | 传动工作点 | C | drivetrain | DYN-04 映射产物（τ/ω/P 序列摘要/效率/反射惯量/惯量比） |
| 12 | `selection-bom` | 选型/BOM 与淘汰依据 | C | selection | SEL-06 投影（可行组合/裕量/成本/逐项淘汰原因——实际值/阈值） |
| 13 | `review-signoff` | 评审/签署元数据 | C | **框架** | ReviewMetadata（§4.5） |
| 14 | `variant-diff` | 改型差异和取舍理由 | C | **框架**＋modeling（MDL-08 数据实体） | VariantDiffBlock（RPT-04：一致基准校验＋差异表＋取舍理由） |

词表规则：sectionId 为持久化契约（token 只增不改名；新增章节＝sectionModelVersion 升版并走本文变更记录）；框架章节（1/2/7/8/13/14）由 reporting 内建提供方实现（不注册、不可覆盖）；域章节（3/4/5/6/9/10/11/12）由对应单元在装配期注册（阶段 A 前不存在——替身仅供测试）；**不预建无提供方的空章节**（未注册的域章节在报告中呈现为"章节不可用（提供方未注册）"缺项，而非空壳——TRJ-08 同精神）。

### 5.2 B/C 级报告范围图

```text
                      RPT-01 报告范围（ReviewReport 章节词表）
 ┌────────────────────────────────────────────────────────────────────────────┐
 │ RPT-01-B（阶段 B 验收）——8 个 B 章节                                       │
 │  ┌──────────┬──────────┬────────┬──────────┬──────────────────┬──────┬─────────────────┐ │
 │  │项目与方案 │输入摘要  │模型     │需求       │运动学与碰撞       │静态优 │诊断 │外部验证边界 │ │
 │  │(框架)    │(框架)    │(modeling)│(requirements)│(kinematics)  │化候选 │(框架)│(框架)     │ │
 │  └──────────┴──────────┴────────┴──────────┴──────────────────┴──────┴──────┴─────────────────┘ │
 └────────────────────────────────────────────────────────────────────────────┘
                                        ▼ RPT-01-C（阶段 C 起验收）＝B 全部 ＋ 追加 6 个 C 章节
 ┌────────────────────────────────────────────────────────────────────────────┐
 │  ┌──────────┬─────────────────────┬──────────────┬──────────┬──────────┬──────────────┐ │
 │  │轨迹与节拍 │动力学曲线、峰值和 RMS│传动工作点     │选型/BOM 与 │评审/签署  │改型差异和     │ │
 │  │(trajectory)│(dynamics)          │(drivetrain)  │淘汰依据    │元数据(框架)│取舍理由(RPT-04)│ │
 │  │          │                     │              │(selection)│          │(框架+modeling)│ │
 │  └──────────┴─────────────────────┴──────────────┴──────────┴──────────┴──────────────┘ │
 └────────────────────────────────────────────────────────────────────────────┘
 规则：B 级词表 ∩ C 专属词表 ＝ ∅（B 报告携带 C 章节＝LevelConflict 拒绝——不能伪造 C 级章节）
      C 级零 C 章节选中＝ScopeInsufficient 拒绝（附降级建议）
      报告级别 ≠ 证据等级（证据等级＝评估模式效力，归 evidence §8.1 表 1）
```

### 5.3 章节状态与缺项表达（SectionStatus）

| 状态 | token | 语义 | 进入条件 | 呈现 |
| --- | --- | --- | --- | --- |
| Populated | `populated` | 章节有正式内容（≥1 条目） | 提供方返回条目且绑定校验通过 | 完整章节 |
| NoFormalResult | `no-formal-result` | **缺正式结果**（该域无任何可引用的已完成结果） | 提供方声明或构建器判定 | 默认**不选**（selected=false）＋缺项清单——"缺正式结果的章节默认不选并显示缺项"（§16 验收要点） |
| DataInsufficient | `data-insufficient` | 有结果但证据不足（envelope engineeringStatus=DataInsufficient／必需证据 Missing/Invalid/Unverified） | 消费 envelope/证据清单 | 选中＋**缺失项全量清单**＋"数据不足"限定语（RPT-05） |
| NotApplicable | `not-applicable` | 章节对其数据源显式不适用（C2 口径：条件不满足，不计缺失） | 提供方声明＋原因 | 选中＋"—"＋原因（ERR-01：不伪造） |

**区分规则**（任务约束§五.3）：证据缺失（EvidenceItemStatus=Missing——无产物）／无效（Invalid——有产物但绑定校验失败）／未验证（Unverified——如 Quick 产物用于正式判定）／不适用（NotApplicable——条件不满足，不计缺失）／数据不足（engineeringStatus 轴的 DataInsufficient——汇总结论）五个词各有其位，reporting 逐一呈现、不合并、不互相推导（P-EV-8 词表消费）。**不得只显示"通过/失败"而丢失依据**：每个结论条目必带 ResultBinding（runId＋字段路径）＋EvidenceBinding 列表——渲染层把绑定呈现为可追溯引用（HTML 锚点/CSV 列/JSON 字段），缺绑定的结论条目在构建边界拒绝（EvidenceRefInvalid）。

### 5.4 C 级缺必需证据时的降级与拒绝（保守处置，P-RPT-5 登记）

| 情形 | 处置 | 依据 |
| --- | --- | --- |
| C 级请求，部分 C 章节缺正式结果 | 生成 C 级报告；缺失章节 NoFormalResult＋默认不选＋缺项显示（**不伪造**） | §16 验收要点 |
| C 级请求，**全部** C 章节缺正式结果 | **拒绝生成**（ScopeInsufficient）＋诊断 RPT-SCOPE-INSUFFICIENT＋建议生成 B 级（用户显式确认后按 B 级重建——不是静默降级） | 本文保守处置（上游未明文"全缺时如何"——宁可拒绝不可虚级，P-RPT-5） |
| C 章节有结果但必需证据缺失 | 章节 DataInsufficient＋缺失项全量清单＋限定语；报告可生成，该章节结论不得渲染"正式通过" | §8.1 表 2④＋RPT-05 |
| B/C 章节的"不适用"（如纯关节路径之于段内 IK 连续性） | NotApplicable＋原因，显示"—" | C2/ERR-01 |

**发布资格**（任务约束§五.2 的"阶段 B/C 的发布资格"读法）：报告工件发布（reports/ 归档）与"正式通过结论"是两个独立门禁——发布门禁＝构建成功＋一致性通过＋project 写权限；正式结论门禁＝evidence eligibility（§6.2）。B/C 阶段各自的**验收**资格归需求（RPT-01-B 阶段 B 验收、RPT-01-C 阶段 C 起验收），软件侧以章节提供方注册状态结构性表达（§4.6）。

---

## 6. 结果、证据和当前性表达

### 6.1 状态词的来源纪律

报告内一切状态词**只消费既有权威词表，不新增**：结果完整性（core::TaskOutcome 四值）、工程判定（core::EngineeringStatus 四值）、当前性（Current/Superseded＋不可判定计算形态——evidence P-EV-4）、证据状态（evidence::EvidenceItemStatus 五值，P-EV-8 实现承载）、任务九态（core::TaskState，execution 状态机语义源；报告呈现终态）。UI 七态呈现（UX-10）归 ui——报告导出物（HTML/JSON/CSV）使用本节工程词表，不经七态投影（七态是界面聚合视图；报告是文档化事实——两者数据同源，不构成第二权威）。

### 6.2 结果状态与资格的呈现（RPT-05 两类声明的承载）

| 结果状态组合 | 报告呈现规则 | 依据 |
| --- | --- | --- |
| Completed ∧ Feasible ∧ eligibility.formalPass==true（五条件齐备） | **唯一**可渲染"正式通过结论"的组合 | §8.1 正式通过判定＋RPT-05 |
| Completed ∧ EngineeringInfeasible ∧ eligibility.reviewRecord==true | 渲染为"**经验证的不可行结论——正式评审记录**"（附证明类别/作用对象/前提/覆盖范围——DeterministicInfeasibilityProof 的呈现投影）；与"正式通过"为**两种独立声明**，绝不互换措辞 | RPT-05（RV-07） |
| Completed ∧ DataInsufficient | "数据不足"章节/条目＋缺失项全量清单＋限定语；覆盖率等参考值携带"降级中"限定语（P-EV-5） | §8.1 表 2④＋KIN-04 |
| Completed ∧ Feasible ∧ formalPass==false（如 Quick 模式/覆盖不全/证据未验证） | 呈现结果事实＋未达正式通过的原因清单（五条件逐项核对表）；**不得**渲染"正式通过"字样 | RPT-05/EVI-01 |
| Canceled / Failed / Interrupted（engineeringStatus=NotApplicable） | 仅出现在诊断/状态呈现（"已取消/失败/已中断——不构成结论"）；**不得进入正式通过章节**（构建边界强制，§4.6）；其诊断照常呈现（失败可定位） | TASK-02/NFR-REL-03 |
| 当前性=Superseded 的历史结果 | 章节可用（历史证据有效），逐结果/逐章节携带当前性标记＋"输入已变化，复核完成前不沿用原通过结论"提示（SEL-10 复算提示的报告侧呈现——AT-30 观测点） | CON-02/05 |

资格的取得：构建器对每个 Completed 结果调用 evidence 的 FormalPassEligibility／ReviewRecordEligibility **纯检查**并冻结结果到 ResultRefSnapshot.eligibility——reporting 不自算资格、不缓存跨报告复用（每份报告构建时重查）。

### 6.3 当前性表达（CurrentnessSnapshot）

| 字段 | 类型 | 约束 |
| --- | --- | --- |
| `status` | std::optional\<evidence::CurrentnessStatus\>（Current/Superseded） | **仅两持久态**；上游只允许 Current/Superseded——reporting 不增加第三种当前性状态（任务约束§五.4） |
| `evaluatedAgainst` | {core::RevisionId headRevision, contextSummary} | 相对哪个上下文计算（报告生成时刻的当时 HEAD——非报告修订本身；两者一致的常见情形如实记录） |
| `computedAtUtc` | time_point | 计算时刻（快照属性） |
| `reasons` | std::vector\<InvalidationReason 呈现\> | Superseded 时的逐条目失效原因（evidence reasons 的呈现投影——"过期附原因"，UX-10 数据同源） |
| `unevaluableNote` | std::optional\<DiagRefEntry\> | **status==nullopt 时必填**：当前性缺失或无法解析（依赖无法解析/跨上下文）时以诊断表达"当前性无法判定"——**不得默认 Current**（任务约束§五.4；evidence EV-CUR-2 口径） |

规则冻结：

| 规则 | 设计 | 依据 |
| --- | --- | --- |
| 当前性是 evidence 的派生投影 | 构建器经注入适配调用 computeCurrentness（或读 evidence 会话态 CurrentnessIndex 的值快照），结果**冻结**进报告 | CON-02/05 |
| HEAD 前进不自动使报告过期 | 报告内容只含生成时刻快照；HEAD 前进后历史报告字节不变，查看时可另行实时查询当前性作**伴随提示**（不改报告、不入报告内容） | CON-05/任务约束§五.4 |
| 依赖切片变化后保留历史报告 | Superseded 结果照常可被后续报告引用（CON-02"保留为原快照历史证据"），携带当前性标记 | CON-02 |
| 历史报告文件内容不可改写 | 报告工件只增（project §4.1 reports 行）；改写＝冲突拒绝；重新生成＝新 ReportId 新目录 | PA-2/RPT-01 |
| 当前性变化产生诊断但不修改历史内容 | 实时查询发现 Superseded/不可判定→伴随诊断（RPT-CURRENTNESS-UNEVALUABLE 等）；已冻结字段永不更新 | CON-02（EV-CUR-3 同源） |

### 6.4 限定语（QualifierToken）与 RPT-05 措辞冻结

限定语是**数据驱动**的呈现标记，不是自由文本（措辞冻结＝限定语不得弱化或省略——RPT-05）：

| 限定语 token | 触发数据源 | 呈现义务 |
| --- | --- | --- |
| `estimated`（估算值） | ValueProvenance.kind==GeometricEstimate | 数值旁标注"估算"＋methodTag |
| `data-insufficient` | engineeringStatus==DataInsufficient／EvidenceItemStatus∈{Missing,Invalid,Unverified} | 结论旁标注＋缺失清单链接 |
| `external-validation-incomplete` | 外部资源 state==Recorded（未固化）/复现要素缺项 | 外部验证边界章节＋相关条目标注 |
| `downgraded-reference-value` | RegionCoverageEvidence.downgraded==true（KIN-04） | 参考值标注"降级中——不可作为正式覆盖率结论"（P-EV-5 承接） |
| `screening-only` | mode==Quick | "筛选级——不得单独支撑正式通过结论"（表 1） |
| `historical-superseded` | currentness==Superseded | "输入已变化"标记＋原因清单 |
| `not-applicable` | SourcedValue NotApplicable／章节 NotApplicable | "—"＋原因（不伪造数值） |
| `interrupted`/`canceled`/`failed` | outcome 对应值 | 状态事实呈现（非结论） |

渲染器对限定语的处理是**结构性**的（HTML 附加标记/CSV qualifier 列/JSON qualifier 数组字段）——一致性校验把限定语作为字段的一部分逐格式比对（§8.5），任何格式丢失限定语＝ConsistencyMismatch。

### 6.5 多工况结果的覆盖显示

章节条目携带 caseScope（该结论覆盖的工况集）；报告级 coverageSummary 呈现：必验工况总数、逐工况执行状态（Executed/NotExecuted/Invalid/NotApplicable/Failed——evidence CaseCoverageMatrix 五态的呈现投影）、覆盖完备性结论（"全部启用必验工况已覆盖"或"漏验清单"——EVI-02）。**多工况包络合并结果（DYN-07）不替代逐工况条目**——包络条目单独呈现并标注"包络合并（呈现方式）"；替代不可行：漏验任一启用必验工况时，报告呈现整体 DataInsufficient 缺项（§8.1 表 2② 呈现口径），不得输出正式通过。替代不可行证据的表达（任务约束§五.3）：EngineeringInfeasible 结果中 substitutableByInfeasibility=true 的证据项按"因不可行而不适用（NotApplicable，原因=不可行）"呈现，其余证据照常全量列出（C2/C6 口径的呈现侧）。

### 6.6 四轴正交关系表（报告状态 × 结果状态 × 证据状态 × 当前性）

| 轴 | 值域 | 何时确定 | 可变性 | 报告内呈现 | 典型组合例 |
| --- | --- | --- | --- | --- | --- |
| 报告生成状态（会话） | Requested→Resolving→Building→Rendering→Verifying→Publishing→Completed；Failed/Canceled | 构建会话期间 | 会话态（终态后不可变） | 进度/结果提示（不入报告内容） | Canceled 会话不产生报告对象 |
| 报告归档状态（工件） | Staged→Finalized；未完成（abandon 残留） | 发布协议期间 | 工件清单侧（report.json） | 清单/目录呈现 | Staged 工件不列入已发布清单 |
| 结果完整性 outcome | Completed/Canceled/Failed/Interrupted | 运行终结（execution） | 构造后不可变 | 结果引用状态列 | Failed＋NotApplicable（不伪造判定） |
| 工程判定 engineeringStatus | Feasible/EngineeringInfeasible/DataInsufficient/NotApplicable | 汇总时（evidence） | 构造后不可变 | 结论列＋资格两声明 | Completed＋DataInsufficient（计算完成但证据不足） |
| 证据状态 EvidenceItemStatus | Satisfied/Missing/Invalid/Unverified/NotApplicable | 评估/汇总时 | 构造后不可变 | 证据表逐项 | Unverified（Quick 产物）不满足正式通过 |
| 当前性 | Current/Superseded（＋不可判定形态 status=nullopt） | 每次相对上下文计算 | **派生投影**（随时重算、不写回） | 当前性标记列 | Superseded 的 Feasible 历史结果（证据在其快照内仍有效） |

四轴互不推导：Superseded 不改变判定；NotApplicable 不因取消变 Feasible；判定不因当前性改写；证据状态不上升为判定（缺失→DataInsufficient 是 evidence 汇总的结论，reporting 只呈现）。

---

## 7. 报告生成、冻结与归档

### 7.1 生成主流程与会话状态机

```text
 选择修订/快照（明确 ID） → 校验结果与证据引用 → 选择报告级别
   → 组装只读 ReviewReport → 计算报告内容身份 → 生成格式输出
   → 多格式一致性校验 → 提交 project 发布（IReportArtifactSink）
```

构建会话状态机（ReportBuildSession；一次会话一份报告）：

```text
 Requested ──► Resolving ──► Building ──► Rendering ──► Verifying ──► Publishing ──► Completed
 （用户/命令  （锚定修订视图、  （调用章节提供  （三格式渲染＋  （逐字段一致  （工件汇集座       （终态：
   发起）      解析结果/证据/    方组装章节、    字段矩阵提取）   性校验＋确定    begin/write/      报告对象与
              当前性/模型摘要）  资格检查、冻结）                  性复渲染比对）  finalize）        工件已发布）
   │              │                │                │                │               │
   └──────────────┴────────────────┴────────────────┴────────────────┴───────────────┴──► Failed（任一步失败：
                                                                                   诊断＋原项目零修改）
   └──────────────┴────────────────┴────────────────┴────────────────┴───────────────┴──► Canceled（协作取消：
                                                                                   清理临时文件/缓冲）
```

逐项明确（任务约束§五.5）：

| 事项 | 设计 |
| --- | --- |
| 生成期间项目后台写入 | 构建开始时取一次 RevisionView（`query().tryRevision(revision)`——修订不可变，PA-2）；此后一切对象/结果解析只对该修订与构建前枚举的 finalize 运行集——组装期间 HEAD 前进/新修订产生/同修订新运行到达（迟到结果，AT-10）**不影响**已锚定视图，也不进入本报告（报告冻结前结果集已固定）；不重读 HEAD |
| 一致读取视图 | ＝锚定 RevisionView＋构建时刻的 listRuns 快照＋逐结果 envelope 只读值（自归档工件解码，IReportResultSource）——三者在会话内不变（底层数据不可变保证） |
| 生成失败是否修改原项目 | **否**——Resolving~Verifying 全程零项目写；唯一写点在 Publishing（工件汇集座），失败/取消时 abandon（project 清理未 finalize 残留为"未完成工件"，恢复诊断列出——PM-08 口径） |
| 报告对象何时冻结 | `Building` 步末：构建器 `build()` 返回不可变 ReviewReport（值语义＋无 setter）；冻结时刻＝返回时；Rendering/Verifying 只读消费，任何渲染期发现的数据问题＝DataInvalid 失败（**不得**回头改报告——改＝重新构建新报告） |
| 生成中的临时文件 | 项目内发布走工件汇集座（project 管理暂存）；项目外导出的字节先写目标同目录临时文件（io IAtomicFileWriter 协议——`<target>.<8hex>.tmp`）；证据包组装经注入 IArchiveWriter（目标同卷临时区，io TempArea 口径）；reporting 自身不创建其他临时路径 |
| 取消与清理 | 全流程接受协作取消令牌（`ReportCancelToken`——§9 公共约定块定义，与 io IoCancelToken 同形；注入形态下经 L5 适配桥接到 io 令牌，检查点：逐章节/逐格式/逐工件）；取消→中止→清理本会话临时物（RAII）→Canceled（正常取消不产生错误诊断，UX-03）；清理失败→残留诊断（不自动删除非本会话文件） |
| 同一输入重复生成是否幂等 | **是（内容幂等）**：同 (数据源集＋级别＋章节选择＋元数据＋模型版本) 重建→contentIdentity 相等（生成时间/者不入身份，§4.4）；ReportId 不同（新对象）——幂等导出按内容身份判定（§7.4），不按 ReportId |
| 同一目标路径已有不同内容 | 冲突拒绝（RPT-ARCHIVE-CONFLICT/IO-PACK-TARGET-EXISTS 族），不覆盖（§7.4） |
| 清理失败不能破坏已发布报告 | 已 finalize 的 report.json 与工件永不触碰（只增）；清理失败仅影响本会话临时残留＋开发诊断 |

### 7.2 生成与冻结流程图（快照→结果→证据→章节→报告）

```text
 ┌─ Resolving ────────────────────────────────────────────────────────────────────────┐
 │ ①锚定：view = query.tryRevision(revision)（缺失→SourceMissing）                     │
 │ ②结果集：runs = 构建请求给定（明确 RunId 列表）→ 逐个校验 ∈ listRuns(revision)      │
 │    （仅 finalize 运行——project D-13 口径；未 finalize→SourceMissing）                │
 │ ③逐结果：envelope = resultSource.tryEnvelope(runId)（解码失败→SourceMissing＋诊断）  │
 │    eligibility = evidence 纯检查（FormalPass/ReviewRecord）                          │
 │    currentness = 当前性投影快照（§6.3；不可判定→nullopt＋诊断，不默认 Current）       │
 │ ④模型摘要：modelSummary = modelSummaryProvider.trySummary(revision)（§9.7）         │
 │ ⑤快照一致性：报告级 snapshotId 与逐结果 snapshotId 校验（不一致→SourceAmbiguous）     │
 └──────────────────────────────┬──────────────────────────────────────────────────────┘
                                ▼
 ┌─ Building ─────────────────────────────────────────────────────────────────────────┐
 │ ⑥章节组装：框架章节（内建）＋已注册域章节（IReportSectionProvider.project(request)） │
 │    逐章节：sourceResults⊆resultRefs、证据绑定校验、缺项全量收集、SectionStatus 判定   │
 │    级别校验：B 不含 C 章节／C 至少一个 C 章节选中（§5.4）                            │
 │ ⑦冻结：ReportCodec-Data → dataIdentity；ReportCodec-Full → contentIdentity；        │
 │    返回不可变 ReviewReport（无任何修改途径——冻结时刻）                               │
 └──────────────────────────────┬──────────────────────────────────────────────────────┘
                                ▼
 ┌─ Rendering ──► Verifying ──────────────────────────────────────────────────────────┐
 │ ⑧字段矩阵单次提取（FieldMatrix，§8.4）                                              │
 │ ⑨三格式渲染（均消费同一 ReviewReport＋FieldMatrix——不允许任何格式重新计算结果）        │
 │ ⑩一致性校验（§8.5：逐字段回读比对＋二次渲染字节一致）→ 不一致→ConsistencyMismatch 失败 │
 └──────────────────────────────┬──────────────────────────────────────────────────────┘
                                ▼
 ┌─ Publishing（项目内归档时）────────────────────────────────────────────────────────┐
 │ ⑪ReportArtifactManifest 组装（工件清单＋摘要＋版本）                                 │
 │ ⑫sink.begin → 逐工件 writeArtifact → finalize（report.json 原子发布＝完整，D-13）    │
 │ ⑬PublishedReportRecord 返回（§7.3）                                                 │
 └────────────────────────────────────────────────────────────────────────────────────┘
```

### 7.3 报告工件完整性摘要与归档状态图（project D-13/D-14 模式的报告侧）

**ReportArtifactManifest**（持久化为 `reports/<report-id>/report.json`，project §4.1 行为契约的承载）：

| 字段 | 类型 | 约束 |
| --- | --- | --- |
| `formatId / schemaVersion` | `"ird-report"` / uint32 | 格式标识与版本（升级走版本拒绝＋指引，PM-06 执行侧口径） |
| `reportId / reportVersion / supersedes` | ReportId / uint32 / optional | 报告身份链 |
| `level` | ReportLevel | 级别 |
| `project / branch / revision` | core 三身份 | 数据源锚 |
| `dataIdentity / contentIdentity` | core::ContentIdentity ×2 | 幂等/冲突判定依据（唯一） |
| `generatedAtUtc / generatedBy / generatorVersion` | 同报告字段 | 生成信息（报告身份排除项在此持久化） |
| `artifacts[]` | {relPath, format, sha256, sizeBytes, rendererVersion, templateVersion} | 工件清单（逐工件摘要＋版本） |
| `sourceRuns[]` | core::RunId 列表 | 引用的结果集（另存为 `sources.json` 供复制完整性扫描——project §8.9 勾选规则消费） |
| `finalizedAtUtc / manifestDigest` | time_point / 自身规范化摘要 | finalize 时间与幂等重投递判据（D-14 同构） |

**报告工件状态图**（工件目录生命周期；project 实现，reporting 契约）：

```text
                    begin（冲突预检：同 reportId 已 Finalized？）
  （无）──────────────────────► Staged（目录已建，工件逐个写入）
                                  │ writeArtifact ×N（批次文件只增）
                                  ▼
                               Partial（部分工件——目录可见但【不完整】）
                                  │ finalize：report.json 原子发布 ＝ 唯一"完整"标志（D-13）
                                  ▼
                               Finalized（完整：列入报告清单、可被引用/随包导出）
  失败/取消（任一步）──► abandon ──► 未完成工件（manifest 缺失）：
                              · 不列入已发布清单（"文件存在≠已发布"——任务约束§五.6）
                              · 恢复诊断列出（PM-08 口径）；重试＝新会话续写或冲突拒绝（§7.4）
```

**PublishedReportRecord**（内存侧发布事实，reporting 返回给调用方）：{reportId, contentIdentity, archiveState(Finalized), manifestDigest, artifactRelPaths, publishedAtUtc}。

### 7.4 幂等导出与冲突拒绝（RPT-01"报告目录追加幂等、冲突拒绝"的承接）

**幂等判定依据**（任务约束§五.6 的明确项）：**以内容身份判定，不以 ReportId、不以目标路径**——判定键＝`(contentIdentity, format 集, rendererVersion, templateVersion)` 的 manifest 内容摘要（manifestDigest 同构）。

| 场景 | 判定 | 行为 |
| --- | --- | --- |
| 项目内发布：同 reportId 已 Finalized 且 manifest 摘要一致 | **幂等成功**（不重写、不报错——D-14 同构） | 返回既有 PublishedReportRecord |
| 项目内发布：同 reportId 已 Finalized 但摘要不一致（内容/格式/版本变化） | **冲突拒绝** RPT-ARCHIVE-CONFLICT | 诊断（差异定位：身份/版本维度）；不覆盖 |
| 项目内发布：不同 reportId 落同一目录 | begin 冲突预检拒绝（project 侧目录占用） | `<report-id>` 由 reporting 分配（稳定 token）——正常不发生；发生即拒绝 |
| 外部导出：目标不存在 | 写出（原子协议） | 成功 |
| 外部导出：目标存在且字节一致（同内容重复导出） | **幂等成功**（无操作） | 返回成功＋既有摘要 |
| 外部导出：目标存在且内容不同 | 默认 **NeverOverwrite 拒绝**（IO-PACK-TARGET-EXISTS 族）；用户显式确认后 OverwriteAtomic（原子替换——先前输出在替换前完整保留） | 冲突诊断含路径与摘要差异 |
| 已发布目录存在未提交文件（manifest 缺失的残留） | 未完成工件：不列入清单、恢复诊断列出；**不得将文件存在误当作正式发布**（任务约束§五.6） | 续写（同 reportId 重试）或按冲突处理 |
| 磁盘不足 | writeArtifact/写出失败 → abandon＋清理＋RPT-DISK-FULL（比较型：所需/可用） | 可重试（失败保留选择与路径——§16 验收要点） |
| 只读项目 | 项目内发布拒绝（project 写入口检查——RPT-READONLY-STORE）；**导出到项目外用户选择路径允许**（不属项目写，ui `report.export` readOnlyAllowed=是） | 诊断说明两条路径 |
| 归档失败 | abandon＝归档责任终结（不永久等待）；已 Finalized 的其他报告不受影响；清理失败仅残留诊断（不破坏已发布报告） | PM-03/PM-08 口径 |

**幂等导出与冲突拒绝流程图**：

```text
 exportReport(request)
   │
   ├─ 目标＝项目内（reports/）
   │    │ sink.begin：同 reportId 已 Finalized？
   │    │   ├─ 否 ──► Staged → writeArtifact×N → finalize（原子）──► Completed
   │    │   └─ 是 ──► manifestDigest 比对
   │    │              ├─ 一致 ──► 幂等成功（零重写）──► Completed
   │    │              └─ 不一致 ──► RPT-ARCHIVE-CONFLICT 拒绝（诊断：差异维度）──► Failed
   │    └─ 失败/取消（任一步）──► abandon ──► 清理临时 ──► Failed/Canceled（已发布内容零影响）
   │
   └─ 目标＝项目外（ExportTarget 路径）
        │ 目标存在？
        │   ├─ 否 ──► 原子写出（tmp→flush→替换）──► Completed
        │   ├─ 是且字节一致 ──► 幂等成功 ──► Completed
        │   └─ 是且不同 ──► ReplacePolicy？
        │         ├─ NeverOverwrite（默认）──► 冲突拒绝＋可重试（保留选择与路径）
        │         └─ OverwriteAtomic（用户确认）──► 原子替换（替换前先前输出完整）
```

**project 负责提交指针和持久化一致性**（任务约束§五.6）：reporting 只经 IReportArtifactSink 请求写入；manifest 原子发布、目录编址、恢复扫描、随包导出的树镜像与勾选引用完整性（project §8.9——`sources.json` 供其扫描"报告引用的 run 不在勾选集→报告不复制"）全部归 project。

### 7.5 当前性变化与历史报告保留图

```text
 时刻 T1：修订 r7 为 HEAD；报告 A 基于r7 生成并发布（reports/rpt-a…/）
          报告 A 内容：结果 run-1（snapshotId=S1）currentness=Current（对 r7）
 ─────────────────────────────────────────────────────────────────────────────
 时刻 T2：TCP 对象修改 → 新修订 r8 成为 HEAD
          · 报告 A 的磁盘字节：不变（reports/ 只增；不改写——PA-2/RPT-01）
          · 报告 A 对象（已冻结值）：不变；其 currentness 字段仍是 T1 快照（历史事实）
          · evidence 重算：run-1 切片身份变化 → CurrentnessIndex 更新为 Superseded
 ─────────────────────────────────────────────────────────────────────────────
 时刻 T3：用户查看报告 A
          · 呈现报告 A 原内容（含 T1 当前性快照——历史记录）
          · 另行实时查询当前性（evidence 投影）：Superseded＋原因清单 → 伴随提示
            "输入已变化（TCP 变更），复核完成前不沿用原通过结论"（SEL-10/AT-30 观测点）
            ——提示是查看时投影，不写入报告 A、不修改其文件（CON-02：不产生改写历史的新条目）
          · 若生成新报告 B（基于 r8）：新 ReportId/新目录；报告 A 保留为原快照历史证据
 ─────────────────────────────────────────────────────────────────────────────
 时刻 T4：仅电机成本变更 → r9 成为 HEAD
          · 运动学切片身份不变 → 实时当前性查询仍 Current（HEAD 前进本身不使结果过期——CON-05）
          · 报告 A 字节与对象依旧不变
```

### 7.6 证据包导出（EvidenceBundle，RPT-03）

```text
 EvidenceBundleRequest{reportId, runs[]（默认=报告 resultRefs）, target(外部路径)}
   → 逐结果：resultSource.tryEnvelope(runId) ＋ 归档工件清单（project runDir 逐字节读取授权）
   → 组装（IEvidenceBundleAssembler，reporting 所有）：
       bundle.json        # schema "ird-evidence-bundle/1"：{schemaVersion, reportId,
                         #   contentIdentity, sourceRuns[], generatedAtUtc, totalDigest}
       snapshot/<run>/    # 输入快照引用元数据（snapshotId/sliceId/inputBaselineId/caseSet
                         #   摘要——refs-only 形态；载荷物化可选勾选）
       results/<run>/     # 结果引用：envelope 摘要＋证据清单（itemId/status/digest）
                         #   ＋复现要素（ReproductionBlock：产品/评估契约/编译器/碰撞后端版本）
       reproduction.json  # 版本、种子、线程数、容差（NFR-COR-02/RPT-03 复现要素——
                         #   数值来自 evidence 复现块与配置 canonical，不自算）
   → 封装：IArchiveWriter（注入——io ZIP 适配，P-RPT-7）逐条目写入 → finish
   → 原子替换到目标（io IAtomicFileWriter 协议）；取消/失败→清理临时、目标不变
```

要素边界：**快照/结果/复现要素的数据契约归 evidence**（其 §13 B·reporting 行交付），reporting 拥有打包格式与组装；ZIP 字节封装归 io（注入）；bundle 不是第二套项目格式（不含 HEAD/锁/.staging——与 .rwpack 的"项目镜像"语义严格区分，P-RPT-7）。要素缺失（如复现块不完整）→ BundleIncomplete 拒绝并列出（不产出半包）。

### 7.7 报告往返复算（RPT-06，阶段 C）

roundtrip 语义（任务约束§五 承接需求）：从报告数据源（报告绑定的快照/切片与评估键）经③端口重新派发评估→比对复算结果与报告引用的结果摘要（envelope digest／关键结论字段按附录 D 容差——测试对照类）：一致→报告维持可用；**不一致→报告不可用于正式结论**（RoundtripMismatch：报告与当前可复算事实脱节——标记于伴随提示与后续构建的资格核对，不修改历史报告）。比对入口归 reporting（`roundtripCheck(reportId)`——阶段 C 随 WP-12-T08 落地）；复算执行归 execution/各域（reporting 不自建评估调用——经注入的请求接口发起，登记 §12 交接）。

---

## 8. HTML/JSON/CSV 渲染与一致性

### 8.1 渲染总则与 HTML 结构

**总则**（三格式共同）：多格式共享同一 ReviewReport＋同一 FieldMatrix（单次提取，§8.4）——不允许每种格式重新计算结果；稳定排序（章节按 §5 order、条目按 entryKey、证据按 itemId、诊断按 orderKey——同报告同字节）；数值经 `std::to_chars` 最短往返表示（io canonical 同口径）、单位经 UnitPreference 冻结集（core 唯一换算）；无法在表格格式表示的复杂结构（动力学曲线序列等）以**引用**保留（ResultBinding 的 runId＋fieldPath＋JSON 路径——不复制大数组、不丢弃指向）；UTF-8、无 BOM（io 口径）、CSV 行尾 CRLF（方言声明）、HTML/JSON 行尾 LF；渲染错误（RenderFailed——模板/编码/IO 层）与数据错误（DataInvalid——报告字段非法）严格区分（§3.5 错误码分立）。

**HTML（首版交付格式）**：HTML5 静态文档、零外部依赖（无外链 CSS/JS/字体——离线打开一致；样式内联）、零脚本（呈现层不需要执行逻辑）。结构：

```text
<!DOCTYPE html>… <html lang> <head>（内联样式＋<meta charset=utf-8>）
 <body>
  § 报告头块：标题（显示名）·级别（阶段B报告/完整工程报告）·ReportId 规范文本（追溯区块）
     ·数据源（项目/分支/修订 r-…〔seq〕）·生成时间/生成者·报告版本/取代链
  § 目录（章节锚点导航——§5 order）
  § 逐章节 <section id="sec-<sectionId>">：
     章节标题＋状态徽标（Populated/未选择〔缺正式结果〕/数据不足/不适用"—"）
     ·条目表（<table data-section>：每数据单元格 data-field="<fieldKey>"——机器可提取锚，
       §8.5 一致性校验的 HTML 提取依据）
     ·证据表（itemId/状态/artifactDigest/工况范围——Satisfied 行含摘要规范文本）
     ·结果引用（runId＋评估键＋快照身份——追溯区块，折叠呈现）
     ·当前性标记（Current/Superseded＋原因/不可判定诊断）
     ·限定语标记（§6.4 词表——<span data-qualifier>，不可弱化省略）
     ·缺项清单（missingItems 全量——NoFormalResult/DataInsufficient 时）
     ·诊断引用（稳定码/对象定位/三要素/occurrences）
  § 诊断汇总章（diagnostics）·外部验证边界章 ·评审签署块（C：signoff/comments/changeLog/variantDiff）
  § 追溯附录：resultRefs 表（五元组/模式/outcome/engineeringStatus/资格两声明/快照身份）＋复现要素
 </body>
```

预览与跳转（§16 验收要点"预览支持按章节跳转到对象/任务/结果"）：每章节锚点 `sec-<sectionId>`；条目 JumpTarget 携带 {objectId?, caseId?, runId?}——ui 预览宿主据此导航（对象树/任务列表/结果面板）；HTML 内同一锚点体系可供浏览器内跳转。**身份规范文本仅出现在追溯区块/附录**（UX-02：正文工程用语，不显示哈希/Schema/内部插件名）。

### 8.2 RPT-05 措辞冻结的渲染规则

渲染器对两类声明与限定语的处理是**硬编码纪律**（不是模板可选风格）：①"正式通过结论"字样仅当条目所属结果的 eligibility.formalPass==true 才可输出（渲染器对字段做前置断言——违反即 DataInvalid 构造失败，不存在"渲染了再说"路径）；②不可行结论输出"经验证的不可行结论（正式评审记录）"措辞＋证明呈现，**永不**输出"通过"；③§6.4 限定语表逐 token 结构性输出（表格列/标记元素），任何格式省略＝ConsistencyMismatch；④"估算/数据不足/外部验证未完成"限定语跟随数值与结论出现，模板不得移除（模板版本升级若删限定语＝版本不兼容拒绝——TemplateVersion 错误）。

### 8.3 JSON 与 CSV 格式契约

**JSON（`ird-report-json/1`）**：机器读取镜像——ReviewReport 全字段的结构化序列（身份块/章节/条目/证据/诊断/元数据/覆盖/当前性；SourcedValue 四态显式 token；限定语数组字段）；顶层 `schemaVersion`；canonical 写出（io IJsonWriter：键序固定、to_chars 数值、2 空格缩进、LF——同 DOM 二次写出字节相同）；profile 由 reporting 向 io 注册（格式所有者义务，io §5.9.2）。未知字段拒绝（NFR-DEP-04 执行侧）；报告 JSON 工件**不是**项目权威数据（权威归 reports/ 的 report.json＋对象库——JSON 工件是导出镜像）。

**CSV（io 方言 `#rwcsv1`；可附格式）**：主表 `report.csv`——列序冻结：

```text
#rwcsv1 delimiter=, quote=" eol=CRLF encoding=utf-8
field_key,section_id,item_label,value,unit,status,qualifier,result_ref,evidence_ref,case_scope,currentness
```

规则：值列=字段值的文本形态（SourcedValue NotApplicable→"不适用"、NotProvided→"未提供"、Invalid→保留原文——NFR-COR-03）；qualifier=限定语 token 集（分号分隔）；result_ref/evidence_ref=引用规范文本（`run-…`/`itemId@digest 前 12 hex`——引用而非复制，§8.1 复杂结构规则）；空值=空字段（与"不适用"区分）；转义/引号/换行全经 io ICsvWriter（`'=、+、-、@、'` 前缀转义唯一实现——roundtrip 经 io reader 逐字符还原，NFR-SEC-03）；诊断附表 `diagnostics.csv`（code/subject/localName/severity/category/comparison 三要素/occurrences/sourceRun）；多工况明细附表 `coverage.csv`（caseId/status/runId）。同报告同格式二次导出字节一致（确定性）。

### 8.4 字段矩阵（FieldMatrix——单次投影）

`FieldMatrix`＝渲染前对 ReviewReport 的**一次性**遍历产物：`std::vector<FieldCell>{fieldKey(层级稳定键), sectionId, valueRepr(文本规范形——数值 to_chars/四态 token/引用文本), unit?, status?, qualifier[], resultRef?, evidenceRef?, caseScope?, currentness?}`。三渲染器均以 FieldMatrix 为**值来源**（结构来自 ReviewReport；值来自矩阵）——"多格式共享同一 ReviewReport、不允许每种格式重新计算结果"的机制化：任何格式对同一 fieldKey 的输出只能来自同一 FieldCell。字段矩阵本身确定性（遍历序＝章节 order＋条目序；同报告同矩阵——单测锁定）。

### 8.5 多格式渲染时序图与逐字段一致性校验（AT-22）

```text
 调用方        IReportExportService        IReportRenderer(×3)      FieldMatrix 提取器     IReportConsistencyChecker
   │ exportReport(req) │                        │                        │                     │
   │──────────────────►│ ①取报告（只读值）        │                        │                     │
   │                   │──extract(report)──────────────────────────────►│                     │
   │                   │◄─ FieldMatrix（单次）───────────────────────────│                     │
   │                   │ ②render(Html, report+matrix) ──► artifact_H    │                     │
   │                   │   render(Json, report+matrix) ──► artifact_J   │                     │
   │                   │   render(Csv,  report+matrix) ──► artifact_C   │                     │
   │                   │ ③二次渲染（确定性）：render×3 再来一遍 → 字节比对  │                     │
   │                   │   （不一致→RenderFailed——非数据问题）             │                     │
   │                   │──check(report, matrix, artifacts) ──────────────────────────────────►│
   │                   │                          │                        │   ④逐格式回读提取：
   │                   │                          │                        │    · JSON：parse→字段遍历
   │                   │                          │                        │    · CSV：io reader→行/列映射
   │                   │                          │                        │    · HTML：data-field 提取器
   │                   │                          │                        │      （模板自有的机器可解析结构）
   │                   │◄─ ConsistencyResult{consistent, mismatches[]} ───────────────────────│
   │                   │ ⑤不一致→ConsistencyMismatch 失败（不出工件）；一致→发布/导出            │
   │◄──────────────────│                                                                 │
```

**逐字段一致性判定**（AT-22 承接）：对 FieldMatrix 的**每个** FieldCell，三格式回读值必须与源值逐字符一致（fieldKey/value/unit/status/qualifier/引用七元组全部比对）；缺失字段（某格式未输出该 fieldKey）＝mismatch；限定语丢失＝mismatch（§6.4）；数值以文本规范形比对（非浮点再解析——避免二次舍入引入假差异；roundtrip 数值一致性由 to_chars 最短表示保证）。检查器为纯函数（同输入同结论）；mismatch 定位到 (format, fieldKey, 期望/实际)。

**CSV 转义 roundtrip**（AT-22 子项）：导出→io reader 回读→FieldMatrix 重建→与源矩阵比对（`=、+、-、@、'` 前缀与 Unicode/引号/换行/空串样例集——io IO-V01/V02 的报告侧消费）；转义形式不得进入报告结构化层（NFR-SEC-03）。

---

## 9. 公共接口与跨单元协作

约定（适用本章）：签名为实现建议（自洽契约片段，未编译验证——与兄弟单元卡同口径）；除注明外服务级接口并发只读安全、会话级接口单线程；错误一律 `ReportError`（§3.5）；确定性＝同输入同输出（时间戳仅记录性字段且不入身份）；取消＝协作检查点（章节/格式/工件粒度）；reporting 不创建线程（长操作由调用方线程驱动——io §9.13 同则）。

**公共头零 io 类型纪律**（P-RPT-1 注入形态的必然要求——表外编译边下 reporting 头不得包含 io 头）：取消令牌与进度回调为 reporting 自有同形类型，L5 装配负责与 io 的 `IoCancelToken/IoProgressCallback` 桥接（裁决补边后可原位替换、签名零改动）：

```cpp
// Errors.hpp（与 io IoCancelToken 同形；适配器归 L5）
class ReportCancelToken {
public:
    virtual ~ReportCancelToken() = default;
    virtual bool isCancelled() const = 0;    // 幂等；一经真值不再复位
};
struct ReportProgress { std::uint64_t done, total; const char* stage; };
using ReportProgressCallback = std::function<void(const ReportProgress&)>;
```

### 9.1 IReviewReportBuilder（构建器）

```cpp
// Builder.hpp
struct ReportBuildRequest {                 // 任务约束§五.5"选择修订/快照"的承载
    core::ProjectId project; core::BranchId branch;
    core::RevisionId revision;              // 明确 ID（"当前"占位在构造边界拒绝）
    std::optional<core::ContentIdentity> snapshotId;
    std::vector<core::RunId> resultRuns;    // 明确结果集（≥1，全部须为 finalize 运行）
    ReportLevel level;                      // B / C
    std::vector<SectionSelection> sectionOverrides;   // 缺省＝§5 默认规则
    std::optional<UnitPreference> units;    // 缺省 SI
    std::optional<ReviewMetadataSeed> reviewSeed;     // 评审演化：基于旧报告的元数据种子
};
struct ReportBuildOutcome {
    std::unique_ptr<ReviewReport> report;   // 成功时唯一非空（不可变值）
    std::vector<core::DiagnosticRecord> diagnostics;  // 缺项/降级建议等构建诊断
    std::optional<ReportError> error;
};

class IReviewReportBuilder {                // 服务级（无会话状态）；实现持注入源（§3.3）
public:
    virtual ~IReviewReportBuilder() = default;
    // 前置：请求字段合法（身份合法、RunId 非空）；sectionOverrides 词表内
    // 后置：成功＝返回冻结报告（contentIdentity 非零）；失败＝无报告对象、零项目写
    // 错误：SourceMissing/SourceAmbiguous/ScopeInsufficient/LevelConflict/
    //       EvidenceRefInvalid/DataInvalid/Usage
    // 取消：章节/结果解析检查点；取消→清理（无临时物或仅内存态）→Canceled
    virtual ReportBuildOutcome build(const ReportBuildRequest&,
                                     const ReportCancelToken* cancel = nullptr,
                                     IReportProgressSink* progress = nullptr) = 0;
};

// 注入源（L5 装配绑定；reporting 定义最小契约，§3.3）：
class IReportResultSource {                 // 归档结果→envelope 只读值＋当前性投影
public:
    virtual ~IReportResultSource() = default;
    virtual std::optional<evidence::ResultEnvelope>
        tryEnvelope(core::RunId) const = 0;             // 解码归 entry：results/<run-id>/（evidence/execution 适配）
    virtual evidence::CurrentnessResult
        currentnessOf(const evidence::ResultEnvelope&, core::RevisionId headContext) const = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置 | 见签名注释；**评审演化**（reviewSeed 携带旧 ReportId）：构建器校验旧报告 dataIdentity 与本次数据源一致（不一致→Usage：数据已变，须按新报告处理）并继承 reportVersion+1/supersedes |
| 线程 | build 并发安全（无共享可变状态；每次调用独立会话）；同一请求并发调用＝两份独立报告（内容幂等——contentIdentity 相等） |
| 确定性 | 同 (数据源集, 级别, 章节选择, 元数据, 模型版本) → contentIdentity 相等（生成时间/者排除）；替身数据下同（测试锁定） |
| 取消 | Resolving~Building 检查点；取消不产生报告对象（UX-03：正常取消非错误） |
| 生命周期/所有权 | 服务进程级；返回的 ReviewReport 独占归调用方（值语义可拷贝） |
| 副作用 | 零 I/O、零项目写（只读消费注入源）；诊断经 sink 上报 |
| 调用示例 | `auto out = builder.build({proj, brn, rev7, nullopt, {run1,run2}, ReportLevel::B, {}, nullopt, nullptr, nullptr);` |
| 合法调用 | 阶段 B：B 级＋已注册 B 域章节；阶段 C：C 级 |
| 非法调用 | revision 为 tip 占位（构造拒绝）；resultRuns 含未 finalize 运行（SourceMissing——listRuns 口径）；B 级＋C 章节覆盖（LevelConflict）；Preview 结果（SourceMissing） |

### 9.2 IReportSectionProvider（章节投影提供方，域注册）

```cpp
// SectionProvider.hpp
struct SectionRequest {                     // 只读、值拷贝——提供方不得持有
    ReportLevel level;
    core::RevisionId revision;
    std::optional<core::ContentIdentity> snapshotId;
    std::vector<evidence::ResultEnvelope> results;   // 本章节相关的结果子集（构建器按注册声明过滤）
    ReportCurrentnessSummary currentness;            // 逐结果当前性快照
    UnitPreference units;
};
struct SectionEntry {                       // 章节条目（提供方产出→构建器校验冻结）
    std::string entryKey;                   // 章节内稳定（语法 [a-z0-9.-]{2,63}）
    std::vector<FieldValue> fields;         // {key, SourcedValue<double>|文本, UnitToken?}
    ResultBinding result;                   // {runId, fieldPath}——结论条目必填
    std::vector<EvidenceBinding> evidence;  // {itemId, status, digest?, caseScope}
    std::vector<core::ObjectId> caseScope;  // 工况范围（⊆ 所属结果 caseScope）
    JumpTarget jump;                        // {objectId?, caseId?, runId?}——预览跳转
};
struct SectionContent {
    std::uint32_t providerContractVersion;  // 提供方契约版本（入 sectionVersion）
    SectionStatus status;                   // §5.3 四态
    std::vector<SectionEntry> entries;
    std::vector<MissingItemView> missingItems;   // NoFormalResult/DataInsufficient 必填（全量）
    std::optional<std::string> notApplicableReason;
    std::vector<core::DiagnosticRecord> diagnostics;
    RenderHint renderHint;                  // Table/CurveRef/DiffTable/MetadataBlock
};
class IReportSectionProvider {              // 域实现（L5 装配注册）；纯投影
public:
    virtual ~IReportSectionProvider() = default;
    virtual std::string sectionId() const = 0;          // §5.1 词表
    virtual ReportLevel minimumLevel() const = 0;       // B 章节=B；C 章节=C
    virtual std::vector<std::string> requiredEvaluationKeys() const = 0;  // 声明消费的结果域
    // 前置：request.results 非空（构建器已按 requiredEvaluationKeys 过滤）
    // 后置：纯函数——同请求同输出；不写项目、不派发任务、不产生修订
    // 错误：提供方以 SectionContent.status＋diagnostics 表达缺项（不抛业务结论）；
    //       结构违约（entryKey 重复/绑定越界）由构建器拒绝（EvidenceRefInvalid/DataInvalid）
    virtual SectionContent project(const SectionRequest&) = 0;
};
class SectionRegistry {                     // 装配期注册（与 evidence EvaluatorRegistry 同模式）
public:
    void registerProvider(std::unique_ptr<IReportSectionProvider>);   // 重复 sectionId 拒绝
    IReportSectionProvider* find(std::string_view sectionId) const noexcept;
    std::vector<std::string> registeredSections() const noexcept;     // 稳定排序（字典序）
};
```

| 维度 | 契约 |
| --- | --- |
| 前置 | 注册时 sectionId ∈ §5.1 词表且级别匹配；minimumLevel 与词表一致 |
| 后置 | 注册后只读；project 为纯投影（替身与真实提供方同一契约——阶段 A 替身输出不构成业务正确性证明，边界声明见 §10） |
| 线程 | 注册期单线程（装配）；运行期 find 并发只读；project 并发安全（无状态建议） |
| 确定性 | 同请求同输出（NFR-COR-02 精神；条目排序稳定——entryKey 序） |
| 取消 | 无内部取消点（纯内存投影；长计算不该在此——域结果已在 envelope） |
| 生命周期/所有权 | registry 持 provider 独占；SectionContent 值归构建器 |
| 副作用 | 零（禁止读项目外文件/发任务——构建器传入的结果即全部输入） |
| 调用示例 | kinematics 注册 sectionId=`kinematics-collision`、minimumLevel=B、requiredEvaluationKeys={"kin.batch-ik","kin.region-coverage"} |
| 非法调用 | project 内读取请求外数据（评审约束）；伪造 Satisfied 证据（构建器绑定校验拦截——digest/范围核对）；C 章节注册为 B（注册拒绝） |

### 9.3 IReportRenderer（渲染器）

```cpp
// Render.hpp
enum class ReportRenderFormat { Html, Json, Csv };
struct RenderOptions { ReportRenderFormat format; };    // 模板版本随渲染器（不可调用方选）
struct RenderArtifact {
    ReportRenderFormat format;
    std::vector<std::uint8_t> bytes;
    core::Digest256 digest;                  // SHA-256(bytes)
    std::uint32_t rendererVersion, templateVersion;   // 入工件清单（§7.3）
    core::ContentIdentity sourceReportIdentity;
};
class IReportRenderer {                      // 服务级（无状态）；实现经注入使用 io 写出设施
public:
    virtual ~IReportRenderer() = default;
    // 前置：report 已冻结（contentIdentity 非零）；matrix 与 report 同源（调用方保证——ExportService 内部生成）
    // 后置：成功＝完整字节（canonical：同输入同字节）；失败＝无部分产物外泄（内存缓冲，落盘归导出服务）
    // 错误：RenderFailed（模板/编码层）/DataInvalid（报告字段非法——与渲染错误严格区分）/
    //       TemplateVersion（模板版本与报告 sectionModelVersion 不兼容）
    // 取消：无内部检查点（纯内存生成、毫秒级——落盘阶段在导出服务取消）
    virtual RenderOutcome render(const ReviewReport&, const FieldMatrix&,
                                 ReportRenderFormat) = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置 | 见注释；**HTML/JSON/CSV 三实现消费同一矩阵**（§8.4——AT-22 的结构性前提） |
| 线程 | 并发安全（无状态；缓冲局部） |
| 确定性 | 同 (report, matrix, format, rendererVersion, templateVersion) → 字节相同（二次渲染比对为一致性校验组成部分） |
| 取消 | 渲染本身不可取消（上界＝报告规模；大报告的耗时治理在导出落盘/证据包阶段） |
| 生命周期/所有权 | 服务进程级（L5 装配创建；三格式实现可独立替换/新增——新格式＝新实现，非接口变更） |
| 副作用 | 零 I/O（字节返回内存；写文件归导出服务经 io 原子协议） |
| 调用示例 | `auto a = renderer->render(report, matrix, ReportRenderFormat::Html);` |
| 合法调用 | 三格式任意子集（RPT-02：HTML 首版，JSON/CSV 可附——至少渲染 HTML 方可发布） |
| 非法调用 | 从报告以外数据源取值（评审约束——字段矩阵唯一值源）；渲染时改报告（不可变——编译期即无途径）；输出未经资格断言的"正式通过"字样（§8.2 前置断言——违反即 DataInvalid） |

### 9.4 IReportConsistencyChecker（一致性检查器）

```cpp
// Consistency.hpp
struct ConsistencyInput {
    const ReviewReport* report;
    const FieldMatrix* matrix;
    std::vector<const RenderArtifact*> artifacts;    // ≥2 才有意义（单格式跳过——返回平凡通过＋注记）
};
struct FieldMismatch { ReportRenderFormat format; std::string fieldKey;
                       std::string expected, actual; std::string dimension; };  // value/unit/status/qualifier/ref/missing
struct ConsistencyResult { bool consistent; std::vector<FieldMismatch> mismatches; std::size_t fieldCount; };
class IReportConsistencyChecker {           // 纯函数
public:
    virtual ~IReportConsistencyChecker() = default;
    // 前置：artifacts 与 report/matrix 同源（导出服务内部组装）
    // 后置：逐字段回读比对（§8.5：JSON parse/CSV io reader 回读/HTML data-field 提取）；
    //       mismatches 全量列出（不短路）
    // 错误：回读本身失败（格式损坏）→ ConsistencyMismatch（dimension=parse-failed）
    virtual ConsistencyResult check(const ConsistencyInput&) = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置 | 见注释；确定性纯函数（同输入同结论）；**不修改任何输入** |
| 线程 | 并发安全 |
| 取消 | 无（上界＝字段数×格式数；性能护栏：字段矩阵规模上限登记 §14.2 R-4） |
| 生命周期 | 服务进程级 |
| 副作用 | 零 |
| 调用示例 | 导出服务步骤④（§8.5 时序图）；契约测试逐格式注入坏样本验证 mismatch 定位 |
| 合法调用 | 2~3 格式工件集 |
| 非法调用 | 以"格式存在"当"内容一致"（必须逐字段回读——文件存在≠一致）；跳过限定语维度（§6.4 硬约束） |

### 9.5 IReportExportService（导出服务）

```cpp
// Export.hpp
struct ExportDestination {
    enum Kind { ProjectArchive, ExternalPath };
    Kind kind;
    std::filesystem::path externalPath;     // ExternalPath 必填（P-7 角色——io 口径）
    ReplacePolicy replace = ReplacePolicy::NeverOverwrite;   // 外部导出默认不覆盖
};
struct ReportExportRequest {
    core::ProjectId project;
    ReportId reportId;                      // 已构建报告（或直接携带报告值——见合法调用）
    std::vector<ReportRenderFormat> formats;    // ≥1，必含 Html（RPT-02）
    ExportDestination destination;
    bool withEvidenceBundle = false;        // 同时导出证据包（RPT-03）
};
struct ReportExportResult {
    std::optional<PublishedReportRecord> published;    // ProjectArchive 成功时
    std::vector<ExportedFile> files;                   // {path, format, sha256}（外部导出）
    std::optional<core::Digest256> bundleDigest;       // 证据包
    std::vector<core::DiagnosticRecord> diagnostics;
    std::optional<ReportError> error;
};
class IReportExportService {                // 服务级；内部编排渲染/一致性/归档（§7.1/§8.5）
public:
    virtual ~IReportExportService() = default;
    // 前置：reportId 可解析（本会话构建或自已发布清单加载）；formats 合法；destination 合法
    // 后置：成功＝按 §7.4 幂等/冲突规则发布或写出；失败/取消＝目标不变（外部）或未完成工件（项目内），
    //       选择与路径保留可重试（§16 验收要点）
    // 错误：ConsistencyMismatch/ArchiveConflict/ExportFailed/DiskFull/ReadOnlyStore/Usage
    // 取消：逐格式/逐工件检查点（io 令牌贯通）
    virtual ReportExportResult exportReport(const ReportExportRequest&,
                                            const ReportCancelToken* cancel = nullptr,
                                            ReportProgressCallback = {}) = 0;
};
```

```cpp
// Export.hpp（续）——IReportIoFactory：reporting 定义、L5 装配适配 io（P-RPT-1 注入形态）
// 接口只出现 reporting/core 类型（公共头零 io 类型——§3.3 纪律）；实现内部绑定 io 设施
class IReportOutputTarget {                // 原子输出目标抽象（io IAtomicFileWriter 的投影）
public:
    virtual ~IReportOutputTarget() = default;
    virtual bool commit() = 0;             // 原子替换；失败/放弃→目标不变
    virtual void abort() = 0;              // 清理临时
};
class IReportCsvWriter {                   // io ICsvWriter 的投影（方言行/转义/RFC4180 行为全在 io）
public:
    virtual ~IReportCsvWriter() = default;
    virtual bool open(IReportOutputTarget&&, bool emitDialectMarker = true) = 0;
    virtual bool writeHeader(const std::vector<std::string>&) = 0;
    virtual bool writeRow(const std::vector<ReportCsvCell>&) = 0;   // string|int64|double|空
    virtual bool finish() = 0;
};
class IReportJsonWriter {                  // io IJsonWriter 的投影（canonical 行为在 io）
public:
    virtual ~IReportJsonWriter() = default;
    virtual bool write(IReportOutputTarget&&, const ReportJsonDom&) = 0;
};
class IReportIoFactory {                   // 注入工厂（适配器归 L5；补边后可直连 io——签名零改动）
public:
    virtual ~IReportIoFactory() = default;
    virtual std::unique_ptr<IReportCsvWriter> makeCsvWriter() = 0;
    virtual std::unique_ptr<IReportJsonWriter> makeJsonWriter() = 0;
    virtual std::unique_ptr<IReportOutputTarget>
        makeAtomicTarget(const std::filesystem::path&, ReplacePolicy) = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置 | 见注释；**一致性不通过不出工件**（任何目标——项目内不 finalize、项目外零写出） |
| 线程 | 单次调用单线程；不同导出会话可并行（不同目标/不同 reportId——同 reportId 同目标并发由工件汇集座互斥拒绝） |
| 确定性 | 同 (报告, 格式集, 版本) → 字节相同；幂等判定仅依赖 manifest 摘要（§7.4） |
| 取消 | 检查点密集（逐格式渲染后、逐工件写出前后）；取消→清理临时→Canceled（正常取消非错误） |
| 生命周期/所有权 | 服务进程级（持渲染器/检查器/汇集座/注入 io 工厂的引用）；结果值归调用方 |
| 副作用 | 项目内：经 IReportArtifactSink（唯一项目写路径）；项目外：io 原子写出；诊断经 sink |
| 调用示例 | ui `report.export` 命令 → `exportReport({proj, rptId, {Html,Json,Csv}, {ExternalPath, userPath}})`（只读项目亦可） |
| 合法调用 | 只读项目＋ExternalPath（允许）；可写项目＋ProjectArchive |
| 非法调用 | 只读项目＋ProjectArchive（ReadOnlyStore）；绕过汇集座直写 reports/（无此路径）；formats 不含 Html 即发布（Usage——RPT-02 HTML 为首版主格式）；已 Finalized 同 reportId 不同内容再发布（ArchiveConflict） |

### 9.6 IReportArchiveCoordinator 与注入汇集座（project/io 交接契约）

```cpp
// Archive.hpp —— reporting 定义、project 实现（D-13/D-14 模式；project.md §13.2 交接的承接答复）
struct ReportArtifactBeginRequest {
    ReportId reportId; core::ProjectId project;
    core::ContentIdentity contentIdentity;      // 冲突预检键（幂等判定用 manifest 摘要，finalize 时复核）
    bool idempotencyCheckRequired = true;
};
struct ArtifactWrite { std::string relPath; std::vector<std::uint8_t> bytes; };
enum class ArtifactStatus { Ok, IdempotentHit, Conflict, DiskFull, WriteRejected, ContextClosed };
class IReportArtifactSink {                 // project 实现；writable 检查归 project（§6.8）
public:
    virtual ~IReportArtifactSink() = default;
    // 前置：存储上下文 Active ∧ writable；reportId 目录未被其他会话占用
    // 后置：begin 成功＝目录已建（Staged）；同 reportId 已 Finalized→记录待比对（finalize 判幂等）
    virtual ArtifactSessionRef begin(const ReportArtifactBeginRequest&) = 0;
    virtual ArtifactStatus writeArtifact(ArtifactSessionRef, const ArtifactWrite&) = 0;   // 只增
    // finalize：report.json 原子发布＝完整（D-13）；幂等：manifest 摘要一致→IdempotentHit（零重写）；
    //          不一致→Conflict（RPT-ARCHIVE-CONFLICT）；不覆盖已 Finalized 内容
    virtual ArtifactStatus finalize(ArtifactSessionRef, const ReportArtifactManifest&) = 0;
    virtual void abandon(ArtifactSessionRef, ReportEndReason) = 0;   // Completed/Canceled/Failed——责任终结
};
class IReportArchiveCoordinator {            // reporting 实现（编排；会话级）
public:
    virtual ~IReportArchiveCoordinator() = default;
    // 前置：报告已冻结、一致性已通过
    // 后置：成功＝PublishedReportRecord（Finalized/幂等命中）；失败/取消＝abandon＋清理
    virtual ArchiveOutcome publish(const ReviewReport&, const std::vector<RenderArtifact>&,
                                   const ReportArtifactManifest&) = 0;
    virtual std::vector<ReportListingEntry> listPublished(core::ProjectId) const = 0;  // 仅 Finalized
};

// Bundle.hpp —— IArchiveWriter：reporting 定义、io 实现（ZIP 通道适配——P-RPT-7）
class IArchiveWriter {
public:
    virtual ~IArchiveWriter() = default;
    virtual void open(const std::filesystem::path& target) = 0;             // 同卷临时区
    virtual void addEntry(const std::string& entryName,                     // 纯相对正斜杠（SP-2 口径）
                          const std::vector<std::uint8_t>&) = 0;
    virtual core::Digest256 finish() = 0;   // 原子替换到目标；异常/失败→清理临时、目标不变
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置 | 汇集座：见注释（幂等/冲突/只增全部承载于 project 实现——reporting 契约定义语义）；协调器：编排顺序＝begin→write×N→finalize，任一非 Ok→abandon |
| 线程 | 汇集座由 project 实现承诺（对齐其 writer 互斥——project §9.8）；协调器会话单线程 |
| 确定性 | manifest 摘要＝canonical 编码摘要（同工件集同摘要——幂等判定稳定） |
| 取消 | 逐工件检查点；取消→abandon（未完成工件不列入清单——"文件存在≠已发布"） |
| 生命周期/所有权 | 汇集座随存储上下文（project）；协调器随导出会话 |
| 副作用 | 项目内 reports/ 写入（唯一路径）；IArchiveWriter 临时区＋目标写出 |
| 调用示例 | 导出服务步骤⑤（§8.5）；证据包组装逐条目 addEntry |
| 合法调用 | 重试续写（同 reportId 未 Finalized 会话——write 只增幂等：目标存在且摘要一致跳过） |
| 非法调用 | 覆盖已 Finalized 工件（无此路径——Conflict）；reporting 绕过汇集座写项目目录（无此路径）；IArchiveWriter 条目名绝对/上溯（io SP-2 拒绝） |

### 9.7 IModelSummaryProvider（runtime 只读摘要——runtime §13.2 交接的承接冻结）

```cpp
// ModelSummary.hpp —— reporting 自有投影 schema（不含 RobWork 对象；runtime §10.12 口径）
struct ModelSummary {
    core::RevisionId revision;
    core::ContentIdentity snapshotIdentity;      // 快照身份块
    core::ContentIdentity modelIdentity;         // runtime 计算（含编译器契约版本——evidence v0.2 口径）
    core::ContentIdentity nameMapIdentity;       // RuntimeNameMap 内容身份（CON-06 呈现）
    core::ContentIdentity policyContentIdentity; // 已解析策略内容身份（快照 policyRef 值传递）
    std::uint32_t jointCount; bool allRevolute;  // 轴数与全旋转（链型摘要）
    std::string installPresetToken;              // Ground/Inverted/Wall/Custom（呈现用，不入身份）
    struct Capabilities {                       // 能力声明摘要（runtime RuntimeCapability 的值投影）
        bool hasDynamicWorkCell, hasFullMassInertia, hasJointVelocityLimits,
             hasCollisionGeometry, hasTools, hasScene, hasFrictionModel, hasCouplingMatrix;
    } capabilities;
    std::vector<ResourceSummary> resources;      // {resourceId, state(Solidified/Recorded), digest}
    std::vector<ConfigSummary> configurations;   // {configKindToken, contentIdentity}（AnalysisConfiguration 引用）
};
class IModelSummaryProvider {               // L5 装配适配 runtime（IRuntimeModelView 只读投影）
public:
    virtual ~IModelSummaryProvider() = default;
    // 前置：revision 存在于权威闭包
    // 后置：值拷贝投影（零 RobWork 对象）；快照已释放→按 runtime §10.12 以归档快照身份元数据呈现
    //       （返回值 snapshotIdentity 非空、其余字段可空＋注记——不伪造）
    virtual std::optional<ModelSummary> trySummary(core::RevisionId) const = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置 | 见注释；**DH/显式权威模式不在 CanonicalModel**（runtime §4.1——显式规范化）——权威模式呈现经 model 章节的 modeling 提供方，不经本接口 |
| 线程 | 并发只读安全（runtime 承诺） |
| 确定性 | 同修订同投影（值语义） |
| 取消/生命周期/所有权 | 无取消点；适配器随存储上下文（或 L5）；返回值归调用方 |
| 副作用 | 零（只读；**不触发编译**——runtime §10.12 禁项） |
| 调用示例 | 构建器 Resolving 步④：`modelSummary = summaryProvider->trySummary(rev7)` → input-summary 章节数据 |
| 非法调用 | 经本接口要求编译/读取 WC/DWC 实例（禁——runtime §10.12）；以 installPreset 参与任何判定（呈现字段，不入身份） |

### 9.8 与 ui 的预览契约（P-UI-9 承接）

预览数据源＝**ReviewReport 只读值**（ui 消费 `std::shared_ptr<const ReviewReport>`，不复制语义、不反写）；预览呈现＝ui 预览宿主（widget 容器）内嵌 HTML 片段——reporting 提供 `renderPreview(report)`（Html 格式、与导出同模板同矩阵路径，保证预览＝导出内容）；跳转＝章节锚点＋JumpTarget（§8.1）。ui 的 `report.export` 命令目标服务＝IReportExportService（§9.5）。**reporting 不持有界面对象、不实现签署交互**（N-7——签署表单/确认归 ui，元数据以 ReviewMetadataSeed 回传构建器产生新报告版本，§4.5）。

### 9.9 跨单元协作总表（调用方/输入/输出/一致视图/失败/取消/归档/线程/禁止反向/引用边界）

| 对端 | 调用方 | 输入→输出 | 一致视图 | 失败/取消 | 归档 | 线程 | 禁止反向 | 引用边界 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| project | 双向（reporting 查询/请求写入） | 查询：revision/listRuns/runDir→视图与运行事实；写入：manifest＋工件字节→PublishedRecord | 锚定 RevisionView（不可变） | StoreError 透传→ReportError；取消→abandon | IReportArtifactSink（唯一项目写路径） | 查询并发只读；汇集座按 project writer 互斥 | project 不渲染报告内容（N-8 对向） | 报告引用修订/RunId/快照身份；不引用 HEAD/"当前" |
| evidence | reporting 消费 | envelope 只读值/资格纯检查/当前性投影/复现块/基准检查→结果引用快照 | 构建时刻快照（冻结） | 解码失败→SourceMissing；不可判定→诊断不默认 Current | —（evidence 不涉报告工件） | 纯函数并发 | evidence 不消费 reporting（§13 其 B 行为单向交付） | 证据引用=itemId+digest+caseScope；不重算判定 |
| execution | reporting 消费（注入） | 任务投影（tryTask/progress）→章节终态呈现 | 会话内只读 | 任务失败与报告生成独立（互不传播） | — | 只读查询任意线程 | execution 不调 reporting（事件仅作预览刷新提示） | 任务状态引用=TaskIdentity（呈现，非结论） |
| diagnostics | reporting 消费/上报 | exportSafeSummary/DiagProjection→诊断章节；reporting 过程诊断→sink | 值拷贝（脱敏双保险） | 码未注册→占位＋开发诊断（不崩溃——diagnostics §4.5） | — | sink 并发 | diagnostics 不渲染（N-8） | 诊断引用=稳定码+subject+occurrences |
| io | reporting 消费（注入） | CSV/JSON 写出/原子替换/取消令牌→canonical 字节 | 内存缓冲→落盘单点 | IO 失败→ExportFailed/DiskFull；取消→清理 | 临时区协议（io TempArea） | 会话级单线程（io 口径） | io 不组装报告章节（io N-6/§10.10） | 转义唯一实现归 io；reporting 不触文件层转义 |
| runtime | reporting 消费（注入） | ModelSummary→输入摘要章节 | 值投影 | 快照释放→归档元数据呈现（不伪造） | — | 并发只读 | runtime 不依赖 reporting（§10.12 单向） | 摘要引用=身份值（modelIdentity 等）；不触 WC/DWC |
| ui | ui 消费 reporting | 预览值/渲染片段/导出服务 | 只读值 | 导出失败保留选择与路径可重试 | — | ui 线程 Marshal 自理 | reporting 不依赖 ui（注入回调亦无——元数据经 Seed 值回传） | — |
| workflow | workflow 编排 | 生命周期入口（报告入口整合归 WP-22） | — | — | — | — | reporting 不依赖 workflow | — |
| 业务单元 | 域注册提供方 | SectionRequest→SectionContent | 构建会话内请求值 | 结构违约→构建失败（定位章节） | — | project 并发安全 | 域不依赖 reporting（注册制——L5 装配） | 提供方只见请求内结果（不得自取项目外数据） |

---

## 10. 验证方案及故障注入矩阵

测试目标 `sdurws_ird_reporting_test`（单元内）与 `sdurws_ird_reporting_contract_test`（跨单元）。**全部为设计用例，未实现、未运行；实现状态随 §11 任务登记；任何未执行测试不得标注"通过"**。测试设施：testkit 的 IRD_TEST_INFO/IRD_EXPECT_\*/TempDir/DeterministicEnv/ContractCheck；**可控替身**：`ScriptedResultSource`（按脚本返回 envelope/当前性——含替身 envelope 构造经 evidence ResultEnvelope::make，合法组合-only）、`ScriptedSectionProvider`（按脚本返回章节内容/缺项/不适用）、`FakeArtifactSink`（D-13/D-14 语义 fake：可注入冲突/磁盘满/清理失败）、`FakeArchiveWriter`。**替身边界声明**（evidence EV-REG-3 同源）：替身输出仅验证 reporting 契约（绑定校验/状态呈现/一致性/幂等），**不构成**任何运动学/轨迹/动力学/选型结果的业务正确性证明；替身 envelope 一律经 evidence 构造校验器产生（非法组合在构造边界即被 evidence 拒绝——reporting 不自造非法样本）。

### 10.1 用例矩阵

| 组 | 用例 | 需求/AT 依据 | 前置 | 操作 | 预期结果 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- |
| RP-MDL-1 | 报告身份确定性 | RPT-01、NFR-COR-02 | 同数据源两次构建（替身同脚本） | build×2 | contentIdentity 逐字节相等；ReportCodec 往返 parse(encode(x))==x | 身份比较；ReportId 不同（新对象）如实登记 |
| RP-MDL-2 | 引用不可解析拒绝 | RPT-01、NFR-COR-04 | 脚本注入：结果未 finalize／修订不存在／Preview 结果 | build | SourceMissing（逐项定位）；无报告对象；零项目写 | 错误码；FakeSink 零调用 |
| RP-MDL-3 | 身份与路径/名称分离 | RPT-01 | 构建后重命名显示名再导出 | 导出 | contentIdentity 不变；幂等命中 | manifest 摘要比对 |
| RP-SCOPE-1 | B 级章节完整性 | RPT-01-B、AT-32(B) | 8 个 B 章节提供方就绪（替身） | build(B) | 章节集=§5.1 前 8 项且按 order；每章节结构完整（状态/条目/绑定） | sections 枚举与 order 断言 |
| RP-SCOPE-2 | C 级章节完整性 | RPT-01-C、AT-32 | B＋C 提供方就绪 | build(C) | 14 章节全选场景＋评审签署块存在 | 章节枚举；ReviewMetadata 字段 |
| RP-SCOPE-3 | C 缺必需证据降级/拒绝 | RPT-01-C、§16 要点 | ①部分 C 章节缺正式结果 ②全部 C 缺 | build(C)×2 | ①C 报告生成，缺失章节 NoFormalResult＋默认不选＋缺项显示；②ScopeInsufficient 拒绝＋RPT-SCOPE-INSUFFICIENT＋建议 B | section.selected/status/missingItems；错误码与诊断 |
| RP-SCOPE-4 | B 不伪造 C 章节 | RPT-01-B | 请求 B＋C 章节覆盖 | build | LevelConflict 拒绝 | 错误码；无报告对象 |
| RP-STATE-1 | 失败/取消/中断不入正式结论 | TASK-02、RPT-05 | 替身返回 Canceled/Failed/Interrupted envelope（合法组合） | build＋渲染 | 该类结果仅现于诊断/状态；正文无"正式通过"字样；渲染器前置断言生效 | HTML 文本扫描"正式通过"计数=0；资格字段 |
| RP-STATE-2 | DataInsufficient 正确表达 | §8.1 表 2④、RPT-05 | 替身 envelope=Completed+DataInsufficient＋缺失清单 | build＋渲染 | 章节 DataInsufficient＋缺失项**全量**清单＋限定语；无结论字样 | missingItems 计数与内容；qualifier 输出 |
| RP-STATE-3 | 不可行评审记录 | RPT-05（RV-07） | 替身 envelope=EngineeringInfeasible＋有效证明（经 evidence） | build＋渲染 | "经验证的不可行结论（正式评审记录）"措辞＋证明呈现；**无**"通过"措辞；与正式通过两声明并存不混淆 | 文本断言；eligibility.reviewRecord |
| RP-STATE-4 | 替身边界声明 | 任务约束§八 | 全部替身用例 | 评审检查 | 测试文档显式声明替身输出不构成业务算法证明 | 测试注释留痕（RP-REG 类似 EV-REG-3） |
| RP-CUR-1 | HEAD 前进不改历史 | CON-02/05、RPT-01 | 报告 A 发布（基于 r7）→模拟 TCP 变更产生 r8 | 重读报告 A 工件与对象 | 字节与字段不变；T1 当前性快照保留 | 文件哈希比对；字段断言 |
| RP-CUR-2 | 当前性变化产生诊断不修改内容 | CON-02、AT-30 | 同上场景后实时查询 | 查询投影 | 伴随提示 Superseded＋原因清单；报告 A 零修改（诊断与内容分离） | 提示数据；报告哈希再比对 |
| RP-CUR-3 | 不可判定不默认 Current | 任务约束§五.4、P-EV-4 | 脚本：当前性依赖无法解析 | build | currentness.status=nullopt＋RPT-CURRENTNESS-UNEVALUABLE 诊断；呈现"无法判定" | status 字段；诊断码 |
| RP-COV-1 | 多工况覆盖显示 | EVI-02、AT-32 | 覆盖矩阵：漏一个启用必验工况／全覆盖两脚本 | build＋渲染 | 漏验→整体缺项呈现（不得正式通过）；全覆→覆盖摘要"全部已覆盖"；包络条目标注"包络合并" | coverageSummary 计数；限定语 |
| RP-TRACE-1 | 证据引用可追溯 | NFR-COR-04、AT-14 | 合法报告 | 逐结论条目校验 | ResultBinding(runId+fieldPath)→snapshotId/sliceId 可解析；EvidenceBinding→itemId+digest 在证据清单 | 追溯链遍历断言（ContractCheck 扩展谓词） |
| RP-CONS-1 | 多格式逐字段一致 | RPT-02、AT-22 | 合法报告＋三格式渲染 | 一致性检查＋**注入坏样本**（HTML 删一个 data-field/CSV 改一值/JSON 删限定语） | 正常→consistent；坏样本→mismatch 定位到 (format, fieldKey, dimension) | mismatches 全量；fieldCount |
| RP-CONS-2 | CSV 转义 roundtrip | NFR-SEC-03、AT-22 | 字段含 `=+−@'` 前缀/中文/引号/换行/空串样例 | 导出→io reader 回读→矩阵重建比对 | 逐字符一致；转义形式不入结构层 | 逐字段断言（io IO-V01/V02 报告侧消费） |
| RP-CONS-3 | 稳定排序 | NFR-COR-02 精神 | 同报告两次导出 | 字节比对 | 三格式各自二次导出字节相同 | 文件 SHA-256 |
| RP-CONS-4 | Unicode/单位/复杂结构引用 | RPT-02、KIN-12 | 含中文对象名/单位集 mm·deg/曲线类字段 | 渲染 | Unicode 正确（UTF-8 无 BOM）；单位列与 SI 真值换算正确（core 唯一入口）；曲线以引用呈现不复制 | 编码检查；单位换算断言；CSV 引用列格式 |
| RP-CONC-1 | 生成期间后台写入 | CON-01/05、AT-10（观测） | 构建中注入：新修订提交＋同修订迟到结果事件 | 构建会话继续 | 报告内容不受影响（锚定视图）；迟到结果不进入已冻结结果集；不写新项目 | 报告字段；FakeSink 调用序列 |
| RP-CANC-1 | 取消与临时清理 | UX-03、RPT-02 要点 | 各阶段注入取消令牌 | 导出/构建 | Canceled（非错误诊断）；临时文件清理；目标不变/未完成工件不入清单 | 会话终态；临时目录枚举；FakeSink abandon 调用 |
| RP-IDEM-1 | 幂等导出（项目内） | RPT-01 | 已 Finalized 同内容再发布 | exportReport×2 | 第二次 IdempotentHit（零重写）；manifest 零变化 | sink 调用记录；摘要比对 |
| RP-IDEM-2 | 幂等导出（外部） | RPT-01、§7.4 | 外部目标已存在同字节文件 | export | 幂等成功（无操作） | 文件 mtime/哈希不变 |
| RP-CONF-1 | 同路径不同内容冲突 | RPT-01 | ①项目内同 reportId 不同内容 ②外部目标不同内容 | export×2 | ①ArchiveConflict（差异维度定位）②NeverOverwrite 拒绝（显式确认后 OverwriteAtomic 替换成功且先前输出在替换前完整） | 错误码；差异定位字段；文件状态 |
| RP-DISK-1 | 磁盘不足 | §7.4 | FakeSink/IO 注入写失败 | export | DiskFull＋abandon＋清理；可重试（选择与路径保留） | 错误码；重试成功 |
| RP-RO-1 | 只读项目 | PM-07、ui readOnlyAllowed | 只读上下文 | ①ProjectArchive ②ExternalPath | ①ReadOnlyStore 拒绝＋诊断说明 ②成功 | 错误码；导出文件存在 |
| RP-SAN-1 | 诊断脱敏 | NFR-SEC-07、AT-11 精神 | 诊断含本机路径样例 | 渲染诊断章节 | 输出经脱敏（无完整路径/凭据；exportSafeSummary 双保险） | 输出扫描（io IO-V30 同法） |
| RP-REVW-1 | 评审元数据新版本 | RPT-01-C | C 报告生成→签署/追加意见 | rebuild(reviewSeed) | 新 ReportId＋reportVersion+1＋supersedes；dataIdentity 不变；旧报告文件不变 | 身份链断言；旧文件哈希 |
| RP-REVW-2 | 未签署可导出 | §4.5 | signOff=Unsigned | export | 成功；签署状态如实呈现 | 导出结果；呈现字段 |
| RP-BUN-1 | 证据包与归档协作 | RPT-03、AT-14、PM-05（观测） | 合法报告＋FakeArchiveWriter | assemble | bundle 清单完整（快照引用/结果引用/复现要素）；要素缺失→BundleIncomplete 全量列出；取消→清理目标不变 | bundle.json 内容；条目枚举；终态 |
| RP-ROUND-1 | 往返复算一致/不一致（阶段 C） | RPT-06、AT-22 | ①复算结果与报告一致 ②不一致（脚本改输入） | roundtripCheck | ①一致（可用）②RoundtripMismatch——报告不可用于正式结论（标记；历史报告零修改） | 比对结果；标记字段 |
| RP-GATE-1 | 构建红线 | NFR-MNT-01/02/04 | — | 脚本扫描 | 零 Qt 头；无私有头出 include/；依赖边=登记四条＋零表外同层边；零 testkit 产品链接 | 扫描报告 |

### 10.2 AT 观测点登记（本单元承载部分）

| AT | 本单元观测点 | 用例 |
| --- | --- | --- |
| AT-10（并发与迟到回调） | 报告构建的锚定视图隔离＋迟到结果不入已冻结报告（execution 侧主责——本单元为消费观测） | RP-CONC-1 |
| AT-19（碰撞策略一致） | 报告不重算碰撞证据——碰撞条目逐引用呈现（policy/kinematics 主责） | RP-TRACE-1（证据绑定含碰撞证据项） |
| AT-22（报告往返与格式） | 方案变体基准一致＋roundtrip＋限定语保留＋多格式逐字段一致 | RP-CONS-1~4、RP-ROUND-1（变体基准检查见 §6.5/证据 checkComparisonBaselinesConsistent 消费——RP-SCOPE-2 的 variantDiff 字段） |
| AT-30（选型应用与回填） | Superseded 结果的报告呈现"复核完成前不沿用原通过结论" | RP-CUR-2 |
| AT-32（完整工程报告） | 逐章追溯到结果/工况/快照＋必验工况覆盖核对入章节完整性 | RP-SCOPE-1~3、RP-COV-1、RP-TRACE-1 |
| AT-34（优化一站式导出） | 与 optimization 共用报告规则（canonical/原子导出/限定语）；契约过期（数据源过期）阻断正式导出——OPT-12 主责、本单元提供共用基座 | RP-CONS-3（字节确定性）、RP-CUR-2（过期不沿用） |

### 10.3 Windows GUI 测试规则承接

依据用户级 `C:\Users\zgl18\.codex\AGENTS.md`（实测已读）：reporting 全部测试为无界面模型测试（零 Qt，无平台插件需求）；阶段 B 起报告预览宿主（ui）的 GUI 验证按该规则执行（VS x64 环境、`QT_QPA_PLATFORM=windows`、单实例绝对路径启动、禁 offscreen、`ird_gui` 标签串行）——**本阶段仅登记流程，不启动 GUI 程序**。任何未执行测试不得标注"通过"。

---

## 11. 阶段 A/B/C 实现任务拆分

局部编号 `RPT-Txx`（reporting＝**WP-12**，REQUIREMENTS §3/ARCH §3.1 既有登记；与 dtb §2.13 的 WP-12-Tkk 经"≙"映射对接，不重排、不双轨）。依赖自上而下。**阶段 A（单元基础，可控替身）**：报告基础模型、引用、B/C 范围契约、只读构造、三格式基础渲染、一致性检查、幂等导出与冲突检测、project 归档交接、测试替身与契约测试——不实现任何业务章节内容、证据判定或评审 UI。**阶段 B**：接入 B 级真实章节（model/requirements/kinematics/optimization 注册）。**阶段 C**：C 级章节、变体报告、往返复算。

| 任务 | ≙ | 输入 | 产物 | 依赖 | 涉及文件 | 验证方式 | 完成条件 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RPT-T01 构建落位 | WP-12-T02 | 骨架 CMakeLists、§3.4 | `sdurws_ird_reporting` 升级 STATIC（C++17、链 core＋evidence＋diagnostics＋project）；注册 `_test`/`_contract_test`（gtest 按 dtb §5.5） | 无（CORE-T01 后更佳） | `industrialrobot/CMakeLists.txt`、`reporting/CMakeLists.txt`（新）、`src/`（空起步） | 独立冒烟＋集成双模式构建；RP-GATE-1 | 两模式零错误；红线扫描零命中 |
| RPT-T02 错误与身份类型 | — | §3.5、§4.1 | `Errors.hpp/.cpp`、`Identity.hpp`（ReportId/PublishedRecord/Level）＋RPT-\* 码建议清单 | RPT-T01 | 同名文件 | 单测（token/句法/保留值） | 身份往返用例过；码清单登记 |
| RPT-T03 报告模型与编码 | WP-12-T03（模型部分） | §4 全节 | `ReportModel.hpp/.cpp`（全部引用类型/字段校验）＋`ReportCodec`（Data/Full 双编码） | RPT-T02 | 同名文件 | RP-MDL-1~3 | 不可变/身份/合法组合矩阵用例过 |
| RPT-T04 章节契约与注册表 | — | §5 | `Sections.hpp/.cpp`＋`SectionProvider.hpp`（接口＋注册表） | RPT-T03 | 同名文件 | RP-SCOPE-1~4（替身） | 词表/级别校验/注册边界用例过 |
| RPT-T05 构建器 | WP-12-T03（构造部分） | §7.1/§7.2、§9.1 | `Builder.hpp/.cpp`（会话状态机/锚定/资格快照/当前性快照/冻结） | RPT-T04 | 同名文件 | RP-MDL-2、RP-SCOPE-1~4、RP-CUR-3、RP-STATE-1~3 | 构建全反例矩阵过；零项目写验证 |
| RPT-T06 渲染器（三格式） | WP-12-T04 | §8.1~§8.4、§9.3 | `Render.hpp/.cpp`（FieldMatrix 提取＋HTML/JSON/CSV 渲染；注入 io 工厂） | RPT-T05 | 同名文件 | RP-CONS-3/4、RP-STATE-1~3（渲染断言）、RP-SAN-1 | 措辞断言/确定性/限定语用例过 |
| RPT-T07 一致性检查器 | WP-12-T04 | §8.5、§9.4 | `Consistency.hpp/.cpp` | RPT-T06 | 同名文件 | RP-CONS-1/2 | 逐字段/坏样本定位用例过 |
| RPT-T08 措辞冻结规则 | WP-12-T06 | §6.4、§8.2 | 渲染规则（限定语映射/两类声明前置断言） | RPT-T06 | `Render.cpp` 内 | RP-STATE-1~3、RP-COV-1 | 限定语保留/两声明独立用例过 |
| RPT-T09 导出与归档协调 | WP-12-T03（幂等部分）/T04 | §7.3/§7.4、§9.5/§9.6 | `Export.hpp/.cpp`、`Archive.hpp/.cpp`（＋IReportArtifactSink 契约与 Fake） | RPT-T07 | 同名文件 | RP-IDEM-1/2、RP-CONF-1、RP-DISK-1、RP-RO-1、RP-CANC-1 | 幂等/冲突/磁盘/只读/取消用例过 |
| RPT-T10 证据包组装 | WP-12-T05 | §7.6 | `Bundle.hpp/.cpp`（＋IArchiveWriter 契约与 Fake） | RPT-T05 | 同名文件 | RP-BUN-1 | 要素完整/缺失拒绝/取消清理用例过 |
| RPT-T11 替身与契约套件 | WP-12-T09 | §10、testkit | `ScriptedResultSource/ScriptedSectionProvider/FakeArtifactSink/FakeArchiveWriter`＋RP-\* 用例体 | RPT-T02~T10、WP-02 可用 | `reporting/test/*`、`contract_test/*` | §10.1 矩阵逐条 | 全部用例执行通过并留痕；替身边界声明在案（RP-STATE-4） |
| RPT-T12 模型摘要与任务投影接入 | — | §9.7、§3.3 | `ModelSummary.hpp`＋ITaskStatusSource 最小接口＋L5 适配建议 | RPT-T05 | 同名文件＋适配说明 | 单测（投影 schema/空快照注记） | schema 冻结留痕（runtime 交接） |
| RPT-T13 文档与门禁同步 | — | 全文 | README 核对；§14.3 状态更新；P-RPT-1/2/3 裁决申请提交（关联 io P-IO-1） | RPT-T01~T12 | 本文、README | 评审 | 零偏差登记；待裁决最新 |
| RPT-T14 B 级真实章节接入（阶段 B） | WP-12-T03~T06 验收 | §5.1、各域卡 | 与 modeling/requirements/kinematics/optimization 的注册联调（各域提供方就绪后） | RPT-T11、各域卡 | 集成测试 | AT-32(B 部分)＋RP-SCOPE-1（真实数据） | B 级端到端用例过 |
| RPT-T15 C 级扩展与变体（阶段 C） | WP-12-T07 | §5.1 C 行、§4.5、§6.5 | C 章节接入＋ReviewMetadata/variantDiff＋基准一致性拒绝 | RPT-T14、阶段 C 域结果 | 同名文件 | AT-32、RP-REVW-1/2、RP-COV-1 | 完整工程案例逐章追溯过 |
| RPT-T16 往返复算（阶段 C） | WP-12-T08 | §7.7 | roundtripCheck＋不一致标记 | RPT-T15、③端口联调 | 同名文件 | RP-ROUND-1 | 复算一致/不一致双路用例过 |

---

## 12. 后续阶段承接与接口交接清单

### 12.1 对上游交接义务的承接答复

| 来源 | 义务 | 本文落点 | 状态 |
| --- | --- | --- | --- |
| project.md §13.2 reporting 行 | 冻结**报告工件引用结构与幂等追加规则** | §7.3（ReportArtifactManifest/sources.json）＋§7.4＋§9.6（IReportArtifactSink） | 已冻结（project 侧实现随其阶段 B 任务对齐，P-RPT-4 关联） |
| runtime.md §13.2 | 冻结 **RPT-01 输入章节摘要 schema** | §9.7 ModelSummary/IModelSummaryProvider | 已冻结（L5 适配） |
| ui.md P-UI-9 | 冻结**预览接口形状**（裁决者=reporting 详设所有者） | §9.8（ReviewReport 只读值＋renderPreview＋JumpTarget） | 已裁决（ui 侧消费即可消账） |
| evidence.md §13 B·reporting 行 | 消费 FormalPass/ReviewRecord、追溯链、复现要素 | §6.2/§4.3.1/§7.6 | 已消费（envelope 归档工件的解码入口待 evidence/execution 提供——下方交接） |
| io.md §10.10/§13.2 | 消费 CsvWriter/JsonWriter/AtomicWriter | §3.3/§8.3/§9.5（注入形态，P-RPT-1） | 契约已定（编译边待裁决） |
| diagnostics.md §12 reporting 行 | 消费 exportSafeSummary/码表只读 | §4.3.3/§6 | 已消费；RPT-\* 码建议清单待收编（P-RPT-8） |
| dtb §4 O-25 | 裁决**渲染层是否预留 PDF 接口** | §14.3 P-RPT-6（裁决：不留桩） | 已裁决 |

### 12.2 向下游/对端的交接（各单元详设或实现的直接输入）

| 单元 | 从 reporting 接收 | 须在其侧冻结/提供 |
| --- | --- | --- |
| project | IReportArtifactSink 契约（D-13/D-14 报告语义：只增/manifest 原子发布/幂等/冲突）；ReportArtifactManifest＋sources.json 结构 | reports/ 写入端口实现（§4.1 reports 行既定行为）；随包导出勾选的引用完整性扫描消费 sources.json |
| evidence | 消费口径：报告读取 envelope 的**归档解码入口**（results/<run-id>/ envelope 工件→ResultEnvelope 只读值——建议其提供 decode 入口或经 execution 通道 DTO）；CurrentnessResult 的报告侧取值形态 | EnvelopeCodec/解码入口；CurrentnessIndex 值快照查询形态（P-RPT-9 关联） |
| io | IArchiveWriter 契约（证据包 ZIP 适配——其 ZipChannel 之上的薄适配）；reporting 的 JsonProfile 注册（`ird-report-json/1`） | ZIP 通道适配或通用 ZIP 写出接口（P-RPT-1/7 合并考虑）；profile 注册接纳 |
| runtime | ModelSummary schema（§9.7——冻结凭据） | L5 适配器（IRuntimeModelView→IModelSummaryProvider）；快照释放时的归档身份元数据供给 |
| ui | 预览契约（§9.8）；`report.export` 目标服务签名（§9.5） | 预览宿主实现；签署表单（ReviewMetadataSeed 采集）；命令注册联调 |
| execution | （无直接边）报告生成作为后台任务时的任务类型注册建议 | 若报告导出纳入任务体系（>1 s 转后台）：任务类型＋取消令牌桥接（io 令牌） |
| 业务域（modeling…selection） | IReportSectionProvider 契约＋§5.1 词表＋§9.2 请求/内容结构 | 各域章节提供方实现（阶段 B/C）；entryKey/字段字典登记 |
| optimization | 共用导出规则（canonical/原子/限定语——OPT-12"共用 RPT-02/03 规则"的基座）；EvidenceBundle 复用 | 其专属导出清单（Markdown 证据报告等内容归 optimization） |
| testkit | （无义务——测试报告不经 reporting） | — |

### 12.3 阶段 B/C 接入顺序约束

B 级真实章节注册前，其域评估器与 Profile 必须已注册（evidence §13 顺序约束的报告侧对应）；C 级章节提供方注册前，对应域结果对象（轨迹/动力学/传动/选型）须已可产出 Completed envelope；真实章节内容不得以替身数据冒充验收（RP-STATE-4 边界——AT-32 验收必须真实链路）。

---

## 13. 需求—设计—验证追踪矩阵

| 需求/上游条款 | 设计落点 | 验证（§10.1） |
| --- | --- | --- |
| RPT-01/RPT-01-B（两级对象；只读明确 ID；幂等/冲突） | §4（模型）、§5.1/§5.2（B 范围）、§7.4 | RP-MDL-1~3、RP-SCOPE-1/4、RP-IDEM-1/2、RP-CONF-1 |
| RPT-01-C（追加章节＋签署元数据＋改型差异） | §5.1 C 行、§4.5 | RP-SCOPE-2/3、RP-REVW-1/2 |
| RPT-02（HTML 首版＋JSON/CSV；多格式逐字段一致） | §8 全节 | RP-CONS-1~4、RP-CANC-1 |
| RPT-03（证据包导出） | §7.6、§9.6（IArchiveWriter） | RP-BUN-1 |
| RPT-04（方案变体报告；一致基准） | §4.5 variantDiff、§6.5（基准检查消费） | RP-SCOPE-2（variantDiff）、RP-COV-1 |
| RPT-05（措辞冻结；两类声明） | §6.4、§8.2 | RP-STATE-1~3、RP-COV-1、RP-SAN-1 |
| RPT-06（往返复算） | §7.7 | RP-ROUND-1（阶段 C） |
| NFR-COR-04（结论定位快照与证据） | §4.3 引用类型、§6.2 | RP-TRACE-1 |
| EVI-01/§8.1（模式效力/汇总呈现） | §6.2 呈现规则 | RP-STATE-1~3 |
| EVI-02（必验覆盖/一致基准） | §6.5、§4.2 coverageSummary | RP-COV-1 |
| TASK-02（取消/失败/中断不入正式报告） | §4.6 组合矩阵、§6.2 | RP-STATE-1 |
| CON-02（三轴正交呈现） | §6.6 | RP-CUR-1/2、RP-STATE-1 |
| CON-03（外部资源状态呈现） | §4.2 externalResourceSummary、§6.4 限定语 | RP-CONS-4（单元覆盖）、RP-SAN-1 |
| CON-05（切片身份呈现） | §4.3.1 | RP-TRACE-1 |
| PM-05（包导出协作/失败不留目标） | §7.4/§7.6（原子协议/取消清理） | RP-CANC-1、RP-BUN-1 |
| PM-07（只读项目） | §7.4 表、§9.5 | RP-RO-1 |
| PM-08（未完成工件不入清单） | §7.3 状态图 | RP-CANC-1、RP-CONF-1 |
| OPT-12（导出规则共用） | §9.9、§12.2 | RP-CONS-3、RP-CUR-2 |
| KIN-04（P-EV-5 参考值限定语） | §6.4 downgraded-reference-value | RP-COV-1 |
| UX-02/03（工程用语/三要素/不适用） | §8.1、§4.3.3 | RP-SAN-1、RP-CONS-4 |
| NFR-SEC-03（CSV 转义 roundtrip） | §8.3/§8.5 | RP-CONS-2 |
| NFR-SEC-07（脱敏） | §4.3.3 | RP-SAN-1 |
| NFR-MNT-01/02/04（零 Qt/红线/无包装器） | §1.4、§3.2/§3.4 | RP-GATE-1 |
| 附录 D 第 12 项（身份精确等值） | §4.1 身份纪律 | RP-MDL-1 |
| ARCH §3.5/§7.6/§7.9/SA-12/SA-13 | §3.2/§3.3、§6、§8.3 | RP-GATE-1、RP-STATE-\*、RP-CONS-2 |
| AT-10/19/22/30/32/34（观测点） | §10.2 登记表 | 对应用例 |

---

## 14. 设计决策、风险、待裁决项与变更记录

### 14.1 设计决策登记（D-RPT-xx）

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | reporting 目标零 Qt（含 Core） | 报告为纯字节生成；预览宿主归 ui；模型测试直调（NFR-MNT-01 精神；project/diagnostics/io 同源）。备选（Qt XML/网络视图）引入依赖面被否 |
| D-02 | 报告幂等以**内容身份**（manifest 摘要）判定，不以 ReportId/路径 | RPT-01"追加幂等"语义＝同内容不重复写入；ReportId 演化链上同 dataIdentity 不同 contentIdentity 属不同内容（评审演化）——按 Id 判幂等会误放行内容变化 |
| D-03 | dataIdentity/contentIdentity 双层身份 | 评审元数据修改须产生新版本（新内容身份）同时证明数据基准未变（SEL-10 式复算提示与 RPT-06 需要该凭据）；单层身份无法同时表达 |
| D-04 | 生成时间/生成者不入内容身份 | 同输入重建身份一致是内容幂等的前提；生成信息记录于工件清单（发布事实） |
| D-05 | 值对象＋会话状态机＋工件清单三层状态承载 | 报告不可变（PA-2）与"生成/归档状态"需求的调和：失败不产生对象；归档状态属工件（project D-13 模式）——不存在可变报告 |
| D-06 | C 级全缺正式结果＝拒绝＋显式降级建议（非静默降级） | 保守方向（宁可拒绝不可虚级）；静默降级会掩盖"用户要 C 得 B"的意图偏差（P-RPT-5 登记） |
| D-07 | 三格式渲染消费同一 FieldMatrix（单次提取） | AT-22"渲染单一数据源"（ARCH §10.3）的机制化；备选（逐格式遍历报告）留格式间重算漂移缝隙，被否 |
| D-08 | HTML 机器可提取锚（data-field/data-qualifier） | 一致性校验需要 HTML 回读；模板为 reporting 自有，可保证结构可提取——不引入第三方 HTML 解析依赖 |
| D-09 | 数值以 to_chars 最短文本为一致性比对基准（非浮点再解析） | 避免二次舍入假差异；roundtrip 数值一致性由最短表示保证（io canonical 同口径） |
| D-10 | UnitPreference 冻结入报告身份 | RPT-06/确定性要求同报告重渲染字节一致；显示单位是用户级会话态（KIN-12），不冻结会破坏可复现性 |
| D-11 | 评审元数据修改＝新报告对象（supersedes 链），不改写旧报告 | 不可变历史（PA-2）＋评审演化可追溯；旧报告文件保留 |
| D-12 | 未签署报告可导出 | 上游无"未签署禁导出"条款；正式结论门禁是 eligibility（工程资格）而非签署（行政事实）——两门禁独立 |
| D-13 | 框架章节（6 个）内建、域章节注册制 | project-scheme/input-summary 等数据源全部在平台侧（query/evidence/diagnostics/runtime）——建域提供方即转发包装器（NFR-MNT-04）；域章节内容归域 |
| D-14 | io/runtime/execution 经注入式最小接口（IReportIoFactory/IModelSummaryProvider/ITaskStatusSource/IReportResultSource） | ARCH §3.5 未登记边（表外边＝构建失败）；evidence §3.3/io IO-D02 同模式；裁决补边后可零改动直连 |
| D-15 | 证据包为独立格式 `ird-evidence-bundle/1`（ZIP 经注入 IArchiveWriter），不复用 .rwpack | 证据包语义（快照/结果/复现要素引用）≠项目镜像（PM-05）；复用会引入"第二权威格式"风险（io N 原则） |
| D-16 | Preview 模式结果在构建边界拒绝（非渲染时过滤） | 表 1"Preview 不产生结果对象"——存在即异常，边界显性化（TASK-02 同精神） |
| D-17 | 当前性快照冻结进报告＋查看时伴随实时投影（两通道） | 历史事实（报告内）与现时提示（查看时）分离；CON-02 不改写历史与 UX-10"过期附原因"同时满足 |
| D-18 | 签署/意见的 principal 为报告内受控字段、不写日志 | 评审记录本义需要署名；NFR-SEC-07 精神（凭据类不入日志——diagnostics ConfirmationCredential 同口径） |

### 14.2 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | core/evidence 契约未冻结（签名/字段可能变） | RPT-T02 起返工 | §3.2 消费清单逐项锚定；冻结 diff 后增量同步（P-RPT-9） |
| R-2 | ARCH v0.11 Draft 评审（A9+）可能调整依赖表/端口 | §3.2/§3.3 返工 | 注入式接口与补边结果无关（双形态兼容）；P-RPT-1/2 登记 |
| R-3 | envelope 归档解码入口未定（evidence/execution 均未显式提供 EnvelopeCodec 公共入口） | RPT-T05 构建器阻塞 | IReportResultSource 注入隔离；§12.2 交接催办；必要时以 execution 通道 DTO 适配 |
| R-4 | 大报告的一致性校验成本（字段数×格式数回读） | 导出耗时 | 字段矩阵为线性结构、检查为单遍；规模护栏（章节条目上限登记）＋>1 s 转后台（NFR-PERF-01 口径） |
| R-5 | 域章节提供方结构违约（绑定越界/伪造 Satisfied） | 报告可信度 | 构建边界逐项校验（EvidenceRefInvalid/DataInvalid）＋契约测试坏样本 |
| R-6 | HTML 模板演化与历史工件一致性（旧工件无法用新模板复验） | 回溯审计 | 工件清单携带 rendererVersion/templateVersion（§7.3）；复验用清单内版本——渲染器版本保留策略登记 |
| R-7 | C 级降级口径未获需求确认（P-RPT-5） | 边界场景验收分歧 | 保守处置可预测；登记待裁决 |
| R-8 | io 注入形态在补边裁决前的 L5 装配重复 | 装配代码量 | IReportIoFactory 单点收口（一适配器约十行/方法） |

### 14.3 待裁决项（问题—依据—影响—建议—需要裁决者）

| # | 问题 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-RPT-1 | ARCH §3.5 未登记 reporting→io 边，而 RPT-02/AT-22 需要 reporting 消费 io canonical 写出设施 | ARCH §3.5（表外边＝构建失败）；io.md P-IO-1/IO-D02（注入先行、双形态兼容）；io.md §10.10 已把 reporting 列为消费方 | 编译形态（注入 vs 直连） | **本文已免依赖**（IReportIoFactory 注入先行，接口契约与裁决无关）；建议与 P-IO-1/P-PR-5 合并裁决：补登 reporting→io（接口依赖，无环——io 不依赖 reporting） | 架构所有者（P-IO-1 同案） |
| P-RPT-2 | ARCH §3.5 未登记 reporting→runtime 边；runtime §10.12 已按"reporting 调用只读摘要"设计且其 §13.2 要求本文冻结摘要 schema | ARCH §3.5；runtime.md §10.12/§13.2 | 编译形态 | 维持注入式（IModelSummaryProvider，§9.7 已冻结 schema）；建议架构确认注入为正式形态或补边 | 架构所有者＋runtime 详设所有者 |
| P-RPT-3 | ReportId（`rpt-` tag）为 reporting 层类型，core 未定义 | core.md §4.1 六类型无 ReportId；diagnostics P-DIAG-4（FindingId `fnd-` 同案） | 跨单元身份类型分散 | reporting 自建并遵循 core Id128 约定；建议 core 收编时一并处理 `fnd-`/`rpt-` | core 详设所有者 |
| P-RPT-4 | project 侧 reports/ 写入端口（IReportArtifactSink）为本文单侧冻结，project.md 未定义对应公共接口 | project.md §4.1 reports 行（行为契约）＋§13.2（交接预留） | project 实现时对齐 | 以本文 §9.6 契约为起点；project 详设修订时交叉核对（D-13/D-14 语义一致性） | project 详设所有者 |
| P-RPT-5 | C 级全部追加章节缺正式结果时的处置（拒绝＋显式降级建议）为本文保守设计，上游未明文 | RPT-01-C/§16 验收要点未覆盖该边界 | 边界场景验收解释 | 维持保守（宁可拒绝不可虚级）；如需"全缺仍生成 C 骨架"语义，走需求变更 | 需求所有者 |
| P-RPT-6 | **O-25 裁决：渲染层是否预留 PDF 接口** | REQUIREMENTS 附录 B PDF 行（未立项、不设需求条目、**不留接口桩**——待同步消费者＝WP-12 卡）；dtb §4 O-25 | 渲染器架构 | **裁决：不留 PDF 接口桩**——IReportRenderer 是格式无关抽象（新格式＝新实现，天然可扩展，非 PDF 专用预留）；不建 PDF 专属字段/配置/空页面（附录 B 裁决＋TRJ-08"不建占位"同精神）；未来 PDF 走需求变更 | 本文裁决（O-25 消账；需求侧知悉） |
| P-RPT-7 | 证据包 ZIP 封装的实现通道：io 未暴露通用 ZIP 写出公共接口（ZipChannel 为其 src 内部） | io.md §3.1/§7；RPT-03 | RPT-T10 实现 | IArchiveWriter 注入（io 薄适配）；建议 io 在 P-IO-1 裁决时一并考虑暴露通用 ZIP 写出接口或提供适配器 | io 详设所有者 |
| P-RPT-8 | RPT-\* 稳定诊断码建议清单收编 | diagnostics.md §4.5（码值权威）；§3.5 建议 | 码表冲突 | 随 RPT-T01 后向 StableCodeRegistry 注册时与 diagnostics 收编确认 | diagnostics 详设所有者 |
| P-RPT-9 | 本文消费的 core/evidence 契约以其 v0.1（Draft）为基线，冻结版可能调整（含 envelope 解码入口缺位——R-3） | 各卡文档头状态行 | RPT-T02 起返工 | 冻结 diff 清单后按影响面增量修订；envelope 解码入口列入 evidence/execution 交接催办 | core/evidence 详设所有者＋本文所有者 |

### 14.4 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-10 | 首版草案：基于 REQUIREMENTS v1.16（Accepted）与 ARCHITECTURE v0.11（Draft）、十份兄弟单元卡（v0.1 Draft）完成 14 章详细设计；冻结 ReviewReport 数据模型（双层内容身份/三层状态承载）、B/C 分级章节契约（14 章节词表＋降级拒绝规则）、结果/证据/当前性表达（四轴正交＋限定语词表）、生成冻结与幂等归档协议（D-13/D-14 模式）、HTML/JSON/CSV 渲染与逐字段一致性（FieldMatrix 单次投影）、公共接口六件＋注入契约四件；裁决 O-25（不留 PDF 桩）；验证矩阵 RP-\* 29 组；实现任务 RPT-T01～T16（≙ WP-12-T02~T09）；待裁决 9 项（P-RPT-1~9）。状态 `Draft`。 |

### 14.5 交付前自审记录（任务约束§八逐项；自审≠实现测试≠正式验收）

| 检查项 | 结论 | 证据位置 |
| --- | --- | --- |
| 是否重复 evidence/project/io/diagnostics 的职责 | ✔ 未重复：资格/判定/当前性全经 evidence 纯检查与投影（§6.2/§6.3）；磁盘发布/编址/恢复归 project（§7.3/§7.4）；转义/JSON/ZIP 字节层归 io（§8.3/§7.6）；码表/脱敏归 diagnostics（§4.3.3） | §2.1、§3.3 |
| 是否重新计算当前性或工程判定 | ✔ 未重算：eligibility/currentness 均为构建时快照（evidence 计算）；reporting 只冻结与呈现 | §6.2/§6.3 |
| 是否把失败或数据不足结果渲染成正式通过 | ✔ 前置断言＋构建边界（outcome 组合矩阵＋渲染器断言）；DataInsufficient 全量缺项＋限定语 | §4.6/§6.2/§8.2 |
| 是否改写历史报告 | ✔ 报告不可变；工件只增；当前性两通道分离；评审演化＝新对象 | §4.2/§7.5/§11(D-11) |
| 是否存在第二套报告数据源 | ✔ 单一 ReviewReport＋单一 FieldMatrix（三格式同源）；JSON 工件为导出镜像非权威；测试报告与 ReviewReport 无共享（testkit §2.3） | §8.4/§2.3 |
| 是否把文件存在误当作报告已提交 | ✔ manifest 原子发布＝唯一完整标志；未完成不入清单；幂等按内容摘要 | §7.3/§7.4 |
| 是否丢失快照、修订、证据或工况引用 | ✔ 追溯链逐字段（ResultBinding/EvidenceBinding/caseScope）；RP-TRACE-1 | §4.3 |
| 是否让多种格式产生不同语义 | ✔ 单次字段矩阵＋逐字段回读校验＋限定语纳入比对 | §8.4/§8.5 |
| 是否允许同路径覆盖不同内容 | ✔ 项目内冲突拒绝；外部默认 NeverOverwrite（确认后原子替换） | §7.4 |
| 是否泄露敏感路径或内部信息 | ✔ exportSafeSummary 双保险；正文工程用语（身份文本限追溯区块）；principal 不入日志 | §4.3.3/§8.1 |
| 是否引入未经上游批准的新状态、阈值或证据等级 | ✔ 状态词全消费既有权威词表；SectionStatus/限定语为实现承载呈现标记（不改任何轴）；无阈值；RPT-\* 码为建议值待收编 | §6.1/§6.4/§3.5 |
| 是否越权修改需求、架构或其他单元机制 | ✔ 本文仅新增 units/reporting.md；上游文件零修改；冲突集中 §14.3（P-RPT-1~9） | §14.3 |

**自审结论**：12 项全部通过（其中"新状态"项含实现承载词表的显式登记——P-EV-8/P-DIAG-3 同模式）。本自审不等同实现测试通过或正式验收（§10.1 全部用例状态＝已设计未执行）。

## AI 执行就绪补充

本单元进入 AI 实现前必须满足：

- 本文中的职责、非职责、公共接口、数据模型、错误语义和不变量不得与 ARCHITECTURE.md 冲突。
- 单元任务使用 RPT-Txx 编号（≙ WP-12-Tkk），并通过 doc/industrial-robot-design/tasks/*.json 声明前置任务、允许修改文件和验证命令。
- 跨单元协作只能使用本文列出的公共接口或架构端口；发现接口缺失、需求冲突或依赖未登记时（P-RPT-1/2/3/4/7），任务状态必须标记为 locked。
- 实现完成必须通过单元测试、依赖门禁和追踪矩阵校验，不能只以代码编译成功作为完成条件。

### 单元任务退出条件

| 条件 | 要求 |
|---|---|
| 设计 | 本文接口、状态、不变量和错误语义已冻结或明确登记开放问题 |
| 实现 | 只修改任务契约允许的文件 |
| 测试 | 单元测试覆盖本文列出的正例、反例和不变量 |
| 追踪 | 每个任务至少关联一个需求和一个验证目标 |
| 门禁 | validate-docs.ps1、validate-task.ps1 和单元验证命令通过 |

**本文档结束**（reporting 单元详细设计 v0.1，状态 `Draft`；对应任务 WP-12-T01。）


---


---
