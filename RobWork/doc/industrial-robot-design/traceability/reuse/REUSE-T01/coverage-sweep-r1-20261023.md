# REUSE-T01 覆盖清查 R1（独立清查会话，2026-10-23）

**现场与分支**：独立工作树 `C:/Users/zgl18/.codex/worktrees/coverage-sweep/RobWork`，分支 `codex/coverage-sweep`，基于 `reuse-t01` HEAD `c27349a80d6116151e2b9625c3196c77a4398fff`。主重构会话在 `reuse-audit` 现场的在途改动（REUSE-T07 实施）未被复制、未被修改；本会话全程只读产品源码，仅新增本档与 `coverage-sweep-r1/` 证据目录，不修改矩阵/catalog/任务状态/权限，不操作远程。

**分工与整合约定**：本会话是覆盖清查会话（职责：找遗漏功能、隐藏核心、上游复用机会）；主重构会话是唯一矩阵整合方。下列发现按"待整合建议"交付，由主会话按 audit-contract 登记进 `functions/*.json`；两会话不并发修改同一矩阵文件。清查顺序按所有者指示：先未覆盖范围与高风险隐藏核心，不重复已有效审查内容。

**方法**：
1. 解析两次覆盖独立验收 NOT PASS 报告（`REUSE-T01-coverage-20261023`（冻结 bb997f1，COV-01~16）与 `REUSE-T01-full-coverage-20261023`（冻结 b83fa7a3，FCOV-01~20 及 6 项补充）的全部 (文件,符号) 遗漏清单，逐项对当前冻结矩阵（c27349a80 的 `functions/*.json` sources/auditedSections/inputs 与全行文本）机器核对（脚本 `coverage-sweep-r1/check-known-gaps.mjs`，结果 `known-gaps-status.tsv`）。
2. 枚举当前 HEAD 全部产品文件（`industrialrobot/` 下 1,752 文件 + `scripts/industrialrobot/` 20 脚本），对照矩阵引用集合求差集（`file-diff.mjs` → `unit-stats.tsv`、`uncovered-files.tsv`）。
3. 对 src 下未登记 .cpp 做数值特征启发式扫描（`numeric-scan.mjs` → `numeric-core-candidates.tsv`；启发式含词法噪音，仅作分流），对高风险者实读文件头职责注与关键函数签名定性。
4. 需求侧双向核对：提取 REQUIREMENTS.md 表格内需求 ID（249 个）与矩阵 1,105 行 requirements 字段引用集（73 个 ID）求差（双向证据：需求零引用 × 同域源码零登记相互印证）。

---

## 结论摘要

- 已知 36 项遗漏（COV+FCOV+补充）中 **26 项已被 B16–B35 补审关闭，10 项仍开放**（见 §1）。
- 当前仍有 **938 个实现候选文件未被矩阵任何行引用**（src 非测试 .cpp 424 个；按单元分布见 §2）。
- 实读确认 **14 组零登记的实质生产功能簇**（§3，CS-01~CS-14），其中多个与迁移顺序 §12.11 的下一批能力（runtime offset → FK/Jacobian → 轨迹 → IK → RNE/FD）直接相关。
- 需求侧 116 个单元功能需求 ID（MDL/REQ/KIN/TRJ/DYN/SEL/OPT/RPT/PM/UX/EVI/CON 等）从未被任何矩阵行引用，与源码零登记双向印证（§4）。
- **对当前能力 REUSE-T07：无阻断实施的发现；1 项验收证据缺口提示（CS-01）**——CouplingMath 的真实消费者 `Parts.cpp`/`CommandHandlers.cpp`/`ModelingPanelWidget.cpp` 均零登记，T07 acceptance"真实生产链消费""原Parts/Readiness回归有效"的证据面不完整，冻结能力包前建议由主会话补消费者调用点登记或在独验中核对消费行为。

## §1 已知遗漏清单关闭核对（P0）

**仍开放（10 项）**：

| 项 | 文件 | 符号 | 现状 |
| --- | --- | --- | --- |
| FCOV-09 | execution/worker/main.cpp | `wWinMain`（:613，命令读/心跳线程/握手/分帧派发） | 文件零登记 |
| FCOV-11 | execution/src/Controller.cpp | `forceKillSequence`（:686，一次终止保护/超时失联区分） | 文件零登记 |
| FCOV-12 | diagnostics/src/Confirmable.cpp | `submitConfirmation`（:498，互斥状态迁移/凭据写入，身份放行边界） | 文件零登记 |
| FCOV-13 | diagnostics/src/Redaction.cpp | `redact`（:580，规则链执行/失败降级文本） | 文件零登记 |
| FCOV-17 | cmake/ird_gates.cmake | `ird_classify_target`（:152）、`ird_read_flat`（:95）、`ird_extract_blocks`（:104） | 零登记 |
| FCOV-18 | testdata/golden/rt-fk-equation/…/make_fk_equation.mjs | `closedFormFk`（:53，平面3R闭式FK oracle） | 零登记 |
| FCOV-20 | testdata/golden/pol-collision-analytic/…/make_pol_collision_analytic.mjs | `handDerived`（:41，手算盒碰撞 oracle） | 零登记 |
| FCOV-S1 | project/src/ObjectStore.cpp | `scanObjects`（:441–591，双向引用/磁盘扫描） | 文件有登记但该区段不相交 |
| FCOV-S2 | execution/src/WorkerSupervisor.cpp | `handleHandshake`（:999–1053，协议拒绝/终结/派发） | 同上 |
| FCOV-S3 | execution/src/Scheduler.cpp | `dispatchOne`（:538–645，并发预算/优先级/单项目额度） | 同上 |

**已关闭（26 项）**：FCOV-01~08、10、14~16、19、S4、S5 与 COV-01~06、08~12b、15、16（含 COV-07 部分：`putU32` 符号仍 MISS，其余 FrameAssembler/FrameSequencer/MessageReassembler/fragmentMessage 已登记）。核对明细见 `coverage-sweep-r1/known-gaps-status.tsv`。

## §2 文件级差集统计（P1）

当前 HEAD `industrialrobot/` 全部文件 × 矩阵引用（sources/auditedSections 计为已引用，inputs-only 单列）。未引用实现候选 938 = src 非测试 .cpp 424 + 含实现特征 .hpp 若干 + mjs/ps1/cmake。完整清单 `coverage-sweep-r1/uncovered-files.tsv`，按单元汇总 `unit-stats.tsv`。零引用最多的单元：ui 147、modeling 122、kinematics 90、requirements 85、trajectory 72、project 72、selection 66、optimization 66、dynamics 50、runtime 50。**该数是候选入口计数，不是缺口数或完成率**；同类启发式限制沿用 full-coverage 报告 §限制 1–3。

## §3 实读确认的零登记功能簇（按迁移顺序优先级）

每项含：文件/符号、规模、职责（依据文件头职责注）、消费者证据、上游候选初判（对照 catalog 35 能力）、优先级与阻断判定。

### CS-01 modeling：CouplingMath 消费链未登记 —— **对 REUSE-T07 的验收证据提示（不阻断实施）**
- `modeling/src/Parts.cpp`（1,104 行）：四部件值模型/耦合阶段 token/部件级不变量（I-MDL-5/13、传动 I-MDL-11/12）。**:216 定义 `checkCouplingMatrix` 包装，:1060 在部件校验④数值校验真实调用（I-MDL-11 重算复核）**。
- `modeling/src/CommandHandlers.cpp`（1,900 行）：AssertionSuite 断言分域、命令载荷编解码、prepare 公共管线与五命令钩子（WP-13-T08）；引用 CouplingMath。
- `modeling/plugin/ModelingPanelWidget.cpp`：GUI 面板，引用 CouplingMath。
- 同簇零登记：`Readiness.cpp`(441)、`Codec.cpp`(1,444)、`ModelDiff.cpp`(858)、`Package.cpp`(1,419)、`RobotDesign.cpp`(581)、`StructureEdit.cpp`、`WorkCellReverseImport.cpp`、`WorkingSetCommand.cpp`、`CanonicalBridge.cpp`、`GeometryLinkEdit.cpp`、`ObjectTypes.cpp` 及 Parts/Readiness/Package/RobotDesign/ModelDiff/Template/DhConvert/Import/Codec 等公共头。
- 消费者：CouplingMath 的消费链即 T07 能力包的消费面。`drivetrain/src/MappingCore.cpp` 与 `ui/plugin/UiPlugin.cpp` 已登记，无缺口。
- 上游候选初判：Parts/Readiness 为产品业务不变量（NONE 初判）；Codec/ModelDiff 需对 project canonical codec（已审）与上游 DOM/PropertyTree 做同族比较（待检索）。
- 阻断判定：**不阻断 T07 实施**；但 T07 acceptance[0] 要求核对"真实生产链消费"，acceptance[2] 要求"原Parts/Readiness回归有效"——建议主会话冻结 T07 能力包前补 Parts.cpp:1053–1060 调用点及返回值消费（CouplingMatrixCheck 各字段）的功能登记，或在独验计划中显式列入消费链核对项。按"仅当前能力正确性/权限/真实依赖才阻断"规则，本项属证据面缺口，不是替换正确性疑点。

### CS-02 runtime：编译链与运行时设施未登记 —— 迁移顺序第 2 位（runtime offset 与映射）的直接前置
- `runtime/src/CompilerImpl.cpp/.hpp`（845 行）：**十段编译链产品实现**——S1~S5 共享实现、compile() 整体事务（S1~S10＋事务状态机＋取消轮询＋§5.4 资源摘要复查）、buildCanonicalModel() 分段入口。
- `runtime/src/DynamicWorkCellCompiler.cpp`（546 行）、`CollisionSession.cpp`（230 行，MA-02 真实碰撞设施）、`CacheKey.cpp`（326 行）、`HostPresentationView.cpp`（147 行）、`Errors.cpp`；公共头 `Adapter.hpp`/`CanonicalModel.hpp`/`Description.hpp`/`NameMap.hpp`/`Errors.hpp`。（`Runtime.cpp` 仅 24 行构建占位，可按语义规则成组。）
- 消费者：kinematics/trajectory/dynamics 各端口经 IKinRuntimeView 消费；B9 反例（移动 offset 误编码）落点在 runtime owner 侧。
- 上游候选初判：COLLISION（CollisionSession 对上游 inCollision 的会话封装，catalog 已有）、HOST-SCENE/F-HOST worldTframe（第三批已核部分）；编译链事务与 CanonicalModel 为产品业务（NONE 初判），其中 DW 结构装载可对照上游 WorkCell 加载器（待检索）。
- 优先级：**高**——迁移顺序下一能力包的 owner 单元；CompilerImpl 状态机是 FK/Jacobian/IK 端口的真实供给方。

### CS-03 kinematics：评估器与解集链未登记 —— 迁移顺序第 3/5 位（FK/Jacobian、IK）的消费面
- `kinematics/src/Evaluators.cpp`（2,905 行）：PoseMetrics 评估器（:55 viewDof、:66 validateQuery、:128 makePoseMetricsDescriptor）、任务点 IK 批量评估（:335 validateTaskPointIkQuery、:1161 mergeDemands、:1196 buildDemandChecks、**:1285 solveItem 调 IIkSolver**、:1375 assembleRecord、:811 canonical payload 编码）。
- `Workspace.cpp`（1,326 行）：关节空间笛卡尔网格采样（:216 generateJointSpaceConfigurations、:483 workspaceSamplingPlan、:637 runWorkspaceSampling）。
- `PoseReach.cpp`（1,104 行）：位姿可达覆盖（:309 generatePoseReachTargets、:364 computePoseReachCoverage、:420 DirectionAggregate）。
- `SolutionSet.cpp`（235 行）：**解集四键稳定排序唯一实现**与构造/筛选/统计/最差项。
- `Collision.cpp`（360 行，④端口碰撞接入/policy 会话→域端口适配）、`Bounds.cpp`、`Recompute.cpp`（59 行，KIN-08）、`AnalysisConfig.cpp`（406 行，配置校验/摘要/变化提示，内含小端 u32/u64/f64 读取——与 selection u32At 同族问题）、`Export.cpp`、`Render.cpp`、`Evidence.cpp`、`Commands.cpp`；公共头 Fk/Collision/Coverage/Evaluators/Sampling/Workspace/PoseReach/Recompute/Export/Render/Evidence/AnalysisConfig.hpp。
- 需求侧印证：KIN-03/08/09/11/14 零引用（KIN-08=Recompute 头注明示）。
- 上游候选初判：solveItem 为 KIN-IK 适配消费（catalog 已有）；Workspace 网格枚举对照确定性 grid 候选（README 已核 generateGridPositions 为确定性体心枚举）；PoseReach 方向聚合对照 GEOM-SPHERE-SAMPLING（差异：业务方向聚合/取消预算）；SolutionSet 排序为业务规则（std::stable_sort 标准库，NONE 初判）。
- 优先级：**高**。

### CS-04 trajectory：序列展开与工艺约束核未登记 —— 迁移顺序第 4 位（高阶轨迹）
- `trajectory/src/Sequence.cpp`（469 行）：任务序列展开——I-REQ-7 顺序校验（悬空/重复/环）、**Kahn 拓扑排序**、站级段链展开（WP-16-T04）。
- `ContinuousProcess.cpp`（507 行，段间速度连续断言，TRJ-08-S3）、`ProcessSpeed.cpp`（598 行，TCP 速度受限/恒定测量与超限诊断，TRJ-08-S2）、`Playback.cpp`（640 行）、`Recheck.cpp`（635 行）、`FailureLocate.cpp`（271 行）、`PlanConfig.cpp`、`Commands.cpp`、`Export.cpp`、`TrjTypes.cpp`；公共头 ContinuousProcess/EvaluationDims/FailureLocate/PlanConfig/ProcessSpeed/Recheck/Sequence/TimeParam/TrjTypes.hpp。
- 需求侧印证：TRJ-07/08 零引用；I-REQ-7（requirements 域）同样零引用——跨单元双向印证。
- 上游候选初判：拓扑排序在上游 rw 无同名能力（待检索 rw/core 与 boost graph——boost 已是既有依赖渠道，新增用法仍需供给确认）；速度测量对照 MATH-STATS/MATH-METRIC（差异：业务段级约束）；Planner.cpp 链已登记（COV-01 已关闭），本簇是其相邻未审面。
- 优先级：**高**。

### CS-05 requirements：值模型/导入/编解码全链未登记
- `RequirementTypes.cpp`（919 行，词表 token 转发、引用/位姿/容差/区域/覆盖率/姿态规则校验、I-REQ-2 集合不变量、逐字段等值）、`Import.cpp`（1,443 行，字段字典冻结表、表头自动识别、**单位预览与 SI 归一**、CSV/JSON 逐行映射、REQ-05/REQ-12/NFR-SEC-03）、`Codec.cpp`（1,826 行，五对象 canonical 字节布局 encode/decode——requirements 的字节布局声明点）、`CommandHandlers.cpp`（1,034 行）。
- 需求侧印证：REQ-01/02/04~09/12 共 9 个 ID 零引用。
- 上游候选初判：SI 归一对照 UTIL-UNITS（B31 已审单位薄适配——本文件是其潜在生产消费者，可作 direct/adapter 正向证据机会）；CSV 对照 B8 已登记 CSV 流式解码候选；canonical 布局为业务特定（与 project Codec 同族比较，不自动复用）。
- 优先级：中高（单元体量大、含字节布局声明点）。

### CS-06 selection：电机/减速器硬筛选生产实现未登记（黄金对照存在）
- `selection/src/Screening.cpp`（1,652 行）：电机九维＋减速器九维逐维独立判定、全量原因保留、稳定排序与取消截断（WP-19-T04，units/selection.md §7/§8）。
- 注意：黄金数据集 `sel-screening-golden`/`sel-combo-golden` 的生成器已在 B5 审读，**但生产侧实现零登记**——oracle 与被测实现的登记不对称。
- 需求侧印证：SEL-01/06/07/08/09 零引用。
- 上游候选初判：逐维业务筛选 NONE 初判；interp/screen 聚合可对照 MATH-STATS（待检索）。
- 优先级：中高。

### CS-07 reporting：渲染实现未登记
- `reporting/src/Render.cpp`（2,757 行）：FieldMatrix 提取（单次投影）＋HTML/JSON/CSV 三格式渲染器，"渲染器对条目字段值零自行计算"纪律（RPT-T06）。
- 需求侧印证：RPT-02~06 零引用（仅 RPT-01 已引用）。
- 上游候选初判：HTML/CSV 串接为业务特定；JSON 写出可对照既有 JSON 写候选（B8）。
- 优先级：中。

### CS-08 project：撤销/重做与其余 41 文件
- `UndoRedoServiceImpl.cpp`（452 行，tip 逆命令磁盘推导/信封组装/命令端口提交编排，PRJ-T13，需求 PM-18——**PM-18 零引用**）；另有 project 单元 41 个未登记 .cpp（清单见 TSV）。
- 上游候选初判：命令模式业务特定 NONE；磁盘推导可对照 UTIL-FILE（待检索）。
- 优先级：中。

### CS-09 diagnostics：确认与脱敏服务（FCOV-12/13，已知开放项的功能定性）
- `Confirmable.cpp`（:498 submitConfirmation——互斥状态迁移/惰性过期/一次性 token/凭据写入，**身份/放行边界**）、`Redaction.cpp`（:580 redact——规则链/失败降级，**安全边界**）；diagnostics 25 个 cpp 中 17 个零登记。
- 优先级：中高（权限/身份域；与 T07 无依赖，不阻断）。

### CS-10 execution：worker 入口与控制器终止链（FCOV-09/11 + 同文件区段缺口）
- `worker/main.cpp` `wWinMain`（:613）、`Controller.cpp` `forceKillSequence`（:686）；已登记文件内 `scanObjects`/`handleHandshake`/`dispatchOne` 区段缺口同列（§1）。
- 优先级：中高（进程/权限边界）。

### CS-11 构建与门禁（FCOV-17）
- `cmake/ird_gates.cmake`：`ird_read_flat`(:95)/`ird_extract_blocks`(:104)/`ird_classify_target`(:152) 及顶层 IRD_EDGES 扫描（自边/测试/工具/白名单/Qt 规则）零登记；foundation-governance 33 库存文件仍近乎全零（仅黄金生成器部分登记）。
- 治理脚本 20 个中 2 个零登记（清单 TSV）；19 脚本与 B19–B27 的登记/关联方式仍未明确（沿用 full-coverage 报告 §130–134 的范围缺口记录）。
- 优先级：中（门禁可信度基础，但不阻断产品能力）。

### CS-12 黄金 oracle 未登记（FCOV-18/20）
- `make_fk_equation.mjs::closedFormFk`(:53)、`make_pol_collision_analytic.mjs::handDerived`(:41)（FK 与碰撞的独立 oracle）；B16 只补了 dyn-two-link（FCOV-19 已关闭）。
- 原则：独立 oracle 保留独立参考路线，登记目的是明确适用域/独立性/消费，不因复用改期望值同源。
- 优先级：中。

### CS-13 optimization 需求域大面积零引用
- OPT-01/02/03/05/08~12 共 9 个 ID 零引用；B10/B11 补了 Pareto/Generator/包络/功率（已登记），但该单元 54 个未登记 .cpp 中仍有命令/应用/装配面未审（清单 TSV）。
- 优先级：中。

### CS-14 ui 插件域
- 98 个未登记 .cpp（plugin/ 下多数装配、面板、流程文件）＋UX-01/03~11/13/14 零引用；B15 已补宿主预览 21 条，其余未动。
- 优先级：低（GUI 交互面大、数值密度低；按"最后补简单工具功能"顺序排后；其中 UiPlugin.cpp 已登记，无新缺口）。

## §4 需求侧零引用清单（双向核对）

REQUIREMENTS.md 表格内 249 个 ID 中，以下单元功能需求从未出现在任何矩阵行 requirements 字段（剔除 NFR-*/AT-*/WP-*/P-*/PILOT/DEL 等非逐功能登记对象后；完整双向清单由重算命令复现）：

- modeling：MDL-03/07/08/09/11/13/14/15/16/17/18/20（12 个；MDL-11/12 与 CS-01 Parts.cpp 头注直接对应）
- requirements：REQ-01/02/04/05/06/07/08/09/12（9 个）
- optimization：OPT-01/02/03/05/08/09/10/11/12（9 个）
- ui：UX-01/03/04/05/06/08/09/10/11/13/14（11 个）
- kinematics：KIN-03/08/09/11/14（5 个）；trajectory：TRJ-07/08（2 个）；dynamics：DYN-02/06/07（3 个）
- selection：SEL-01/06/07/08/09（5 个）；reporting：RPT-02/03/04/05/06（5 个）
- project：PM-01/03/04/05/06/07/09~18（16 个）；另有 EVI-01/02、CON-03、ARC-01/02 零引用

说明：矩阵行 requirements 字段经多轮独立验收纠正（如 B3 返工），字段本身可信；零引用是"该需求对应功能未被登记"的强信号而非产品缺陷证据。ARC/CON 类横切需求可能按 audit-contract 属横切登记，处置建议由整合方决定。

## §5 阻断判定汇总与后续顺序建议

- **阻断当前能力（REUSE-T07）实施的发现：无。**
- **T07 冻结前建议处理：CS-01**（消费链登记或独验列入消费核对；不扩大 T07 allowedFiles，登记动作属 T01 矩阵维护）。
- 与迁移顺序对齐的补审顺序建议（供整合方与所有者参考，不自我批准）：CS-02 runtime 编译链 → CS-03 kinematics 评估链 → CS-04 trajectory 核 → CS-05 requirements → CS-06 selection → CS-09/CS-10（权限/进程边界）→ CS-07/CS-08 → CS-11/CS-12（构建与 oracle）→ CS-13/CS-14。
- 每簇进入矩阵时按 audit-contract §4 提供实际区段/调用链/结果消费；发现簇内"文件已登记但区段不相交"的（如 §1 后三行）按同文件补区段处理，避免重复整文件登记。
- 已有效审查内容（26 项已关闭项、Planner/Pareto/Generator/Envelope/XacroExpand/ChannelProtocol/Codec/ReportCodec/JsonLite/Check/ProcessRunner 等）本轮未重复审读。

## §6 限制

1. 启发式文件分流含误报/漏报；938 与各簇行数为候选入口计数，不是缺口结论或完成率。
2. 本轮全部为只读静态审读 + 机器差集；未构建/执行任何产品、测试、门禁；无 C++/GUI 探测。
3. 未逐条验证 1,105 条既有关系的语义正确性；不撤销任何既有局部 PASS/FAIL 结论。
4. 未修改产品/上游/vcpkg/矩阵/catalog/任务契约；不操作远程；主会话在途改动未消费。
5. 上游候选均为初判（基于 catalog 既有 35 能力与既有批次结论），适用性确认需按 policy §1.1 走能力包流程，不构成本会话的批准。
