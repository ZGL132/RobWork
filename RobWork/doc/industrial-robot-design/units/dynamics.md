# 工业机械臂设计软件 · dynamics 单元详细设计（阶段 C）

> 2026-10-06 新建（WP-17-T01 承接）。本文是 dynamics 单元的唯一详细设计：依据 REQUIREMENTS DYN-01～08 与 ARCHITECTURE §3.1/§3.5/§7.3/§7.10/§10 的分配，把动力学输入快照与数据模型、RNEA 逆动力学、正动力学一致性、峰值/RMS/包络/功率/能量、多工况与 drivetrain/evidence 交接、execution/selection/reporting/ui 协作、公共接口、验证与故障注入矩阵、任务拆分与追踪矩阵写到可直接实现的深度。**本文只做详细设计：不实现产品源码、不创建 C++/CMake/测试/UI 文件、不修改需求/架构/其他单元详设；`units/dynamics.md` 此前不存在（2026-10-06 实测，无旧稿可审核），本文为首版；不自行宣布 Accepted，不把本文自审写成实现测试或正式验收通过。**

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（2026-10-06，首版草案，WP-17-T01 交付物） |
| 日期 | 2026-10-06 |
| 状态 | **`Draft`**（待评审；DETAILED-DESIGN.md 单元状态表中"dynamics｜待产出"与 traceability/unit-status.json 中 dynamics `planned/not-written` 以本卡落盘为准，索引行同步由治理侧执行，本卡不代改） |
| 文档代号 | UNIT-DYN |
| 单元 | dynamics（业务域单元，ARCHITECTURE §3.1：逆动力学〔RNE，关节空间精确〕、正动力学一致性、包络统计、可信等级；ARCHITECTURE §3.3 二分结构：零 Qt 计算库＋Qt Widgets 插件） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.13（`Draft`，2026-09-27，方案 B.1 宿主融合 SA-18）** |
| 协作输入 | `units/core.md` v0.11、`units/testkit.md` v0.9、`units/project.md` v0.19、`units/evidence.md` v1.3（**§9 评估器接口/注册表已冻结为设计基线**）、`units/runtime.md` v0.17、`units/policy.md` v0.15、`units/execution.md` v0.12、`units/diagnostics.md` v0.13、`units/ui.md` v1.76、`units/io.md` 卡头 v0.9、`units/modeling.md` v0.40、`units/requirements.md` v0.16、`units/kinematics.md` v0.15、`units/trajectory.md` v0.1（2026-10-06，同批产出）、`units/reporting.md` v0.16——除 evidence §9 冻结基线外**均为 Draft/Draft-Structured（未冻结）**；本卡消费的签名以各卡当前文本为基线，冻结后按影响面增量同步（§15.2 P-DYN-11） |
| **缺失上游（如实登记）** | **`units/drivetrain.md` 不存在**（WP-18-T01 未产出——DriveTrainMappingEvaluator 契约未冻结，本卡以需求 DYN-04/ARCH §7.10 原文为消费契约基线，§8.2/§15.2）；**`units/selection.md` 不存在**（WP-19-T01 未产出——消费协议以需求 SEL-05/10 为基线）；**`units/optimization.md` 不存在**（WP-20/21 未产出——R2 承接仅登记边界）；`units/workflow.md` 不存在 |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立，dynamics 行"待产出"）→ `units/*.md`。本文即 `units/dynamics.md`，按任务卡深度编写 |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/`——**WP-17-T02 已落位**：计算库 STATIC（PUBLIC 链 core/evidence/runtime/execution/diagnostics 五条 §3.2 CMake 行登记边＋集成模式 L1 基线 sdurw_math/kinematics/models＋sdurwsim＋Eigen3 PRIVATE）＋`sdurws_ird_dynamics_plugin` 最小可注册实现＋`sdurws_ird_dynamics_test`/`_contract_test` 随落位登记；**WP-17-T03 已落位**：RNEA 逆动力学评估器（DynTypes/Errors/InverseDynamics 公共头＋src/InverseDynamics.cpp——见 §1.2 T03 实现登记注）；**WP-17-T04 已落位**：输出序列与峰值/RMS 统计面（DynTypes 表尾增列＋SeriesBuilder/Envelope/PowerEnergy 公共头与三实现 TU——见 §1.2 T04 实现登记注）；**WP-17-T05 已落位**：正动力学一致性检查（ForwardDynamics 公共头＋src/ForwardDynamics.cpp＋DYN-FD-NUMERIC-ANOMALY 增码——见 §1.2 T05 实现登记注）；**WP-17-T06 已落位**：数据不足降级与可信来源标记（DynTypes 表尾增列 DynamicsEvidence＋EvidenceBuilder 公共头＋src/EvidenceBuilder.cpp——见 §1.2 T06 实现登记注）；**WP-17-T07 已落位**：多工况与包络合并（DynTypes 表尾增列 DynamicsEnvelope＋Envelope.hpp/cpp 增列 mergeEnvelope＋src/CanonicalDigest.hpp 私有摘要头——见 §1.2 T07 实现登记注）；**WP-17-T08 已落位**：曲线联动与峰值定位/回放数据（Replay.hpp/Commands.hpp 两公共头＋src/Replay.cpp/src/Commands.cpp 两实现 TU——见 §1.2 T08 实现登记注）；评估器注册等其余业务实现随 WP-17-T09～T10（§3.2） |
| 任务归属 | `development-task-breakdown.md`（v0.53）§2.18 WP-17（T01～T10）、§2.19 WP-18（外部依赖，T03 映射实现）、§2.24 WP-23（横切）；本文 §12 只做单元内部任务拆分与排序，不重排 WP 编号 |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释（§2）、框架零源码修改（SA-02）、双模式构建与留痕、提交后推送、分支红线（唯一开发主线 `redesign-main`）；Windows Qt GUI 测试须 VS x64 环境、`QT_QPA_PLATFORM=windows`、绝对路径逐个启动、不用 offscreen——**本次不启动 GUI**（§11.5） |
| 实现口径 | **从头构建**（REQUIREMENTS v1.9/v1.11 确立）：一切实现按需求与本文新建，不继承、不恢复任何历史实现源码。旧代码对照：REQUIREMENTS 附录 A.6 明文"轨迹/动力学/选型在旧代码中无插件 → 按本文 TRJ/DYN/SEL 家族需求全新实现"——本卡不做旧代码逐文件对照（DYN 家族无旧实现可对照） |

---

## 目录

1. 文档信息、输入版本与当前代码状态（§1）
2. 需求承接、R1/R2 边界和职责边界（§2）
3. 单元组成、依赖关系、目标布局和公共头文件（§3）
4. 动力学输入快照与数据模型（§4）
5. 逆动力学（§5）
6. 正动力学一致性（§6）
7. 峰值、RMS、包络、功率与能量（§7）
8. 多工况、drivetrain 交接与 evidence 交接（§8）
9. execution、selection、reporting 和 UI 协作（§9）
10. 公共接口详细设计（§10）
11. 验证方案及故障注入矩阵（§11）
12. 阶段 C 实现任务拆分（§12）
13. 后续阶段承接与接口交接清单（§13）
14. 需求—设计—验证追踪矩阵（§14）
15. 设计决策、风险、待裁决项和变更记录（§15）
16. 交付前自审和未执行事项（§16）

---

## 1. 文档信息、输入版本与当前代码状态

### 1.1 输入文件实测台账（2026-10-06 磁盘实况）

| 输入文件 | 磁盘版本 | 状态 | 存在性 | 对本设计的影响 |
| --- | --- | --- | --- | --- |
| `REQUIREMENTS.md` | v1.16（C1～C8 已留痕） | `Accepted` | 存在 | 唯一需求权威：DYN-01～08、MDL-06/14/16/21/22、TRJ-01～08（轨迹输入面）、CON/EVI/TASK、SEL-05/10、OPT-03/05/06/07、NFR 家族、AT-05/07/10/11/19/27/34/37/38、附录 D、表 4 动力学证据行全部承接（§2） |
| `ARCHITECTURE.md` | v0.13（SA-18） | `Draft`（待评审） | 存在 | §3.1 dynamics 行、§3.3 二分结构、§3.5 依赖表、§7.3 编译链、§7.10 虚功映射契约（M-12）、§4 worker 模型、§10.2 R2 核对表全部承接；其 `Draft` 状态构成 P-DYN-11 基线漂移风险 |
| `DETAILED-DESIGN.md` | 无版本号（状态表 2026-09-22） | 索引文档 | 存在 | dynamics 行"待产出"——本卡落盘即消账依据；主 WP 列记 WP-G（任务编号以 DTB 为准） |
| `development-task-breakdown.md` | v0.53 | 治理文档 | 存在 | §2.18 WP-17-T01～T10、§2.19 WP-18-T01～T05、§2.24 WP-23-T01～T09、§3 追踪矩阵 DYN 行（DYN-04→WP-18-T03 消费 WP-17-T03）、§4.6 L1 基线表 `sdurwsim→runtime(WP-06-T01)、dynamics(WP-17-T02)`、§5 执行约定全部承接 |
| 仓库根 `AGENTS.md` | 无版本号 | 协作约定 | 存在 | 中文注释规范、分支红线、GUI 测试通道约定 |
| `units/core.md` | v0.11 | `Draft` | 存在 | Identity/ContentIdentity/SourcedValue（四态）/Provenance（五来源）/Units（QuantityKind 14 值含 Torque/Force/Power 分立）/Compare（Tolerance/closeWithin/allCloseWithin/runtimeAbsoluteTolerance）/Evaluation 词表/DiagData 契约 |
| `units/testkit.md` | v0.9 | `Draft` | 存在 | 黄金数据集 schema（`ird-golden-manifest/1`）、容差档案、`IRD_EXPECT_*` 断言、GoldenFixture/FaultInterceptor（§11） |
| `units/project.md` | v0.19 | `Draft` | 存在 | ②查询端口/①命令端口/IResultArchivePort（经 execution）；results 归档唯一写入口 |
| `units/evidence.md` | v1.3 | `Draft-Structured`（§9 冻结基线） | 存在 | AnalysisSnapshot/InputSlice（DependencyKind 七值含 **UpstreamResult**）/IEvaluationContext/IEngineeringEvaluator/EvaluatorRegistry/ResultEnvelope/aggregateVerdict 五级/CaseCoverageMatrix/judgeCacheHit/judgeCheckpointCompatibility/ResultCurrentness |
| `units/runtime.md` | v0.17 | `Draft` | 存在 | RuntimeSnapshot/CanonicalModel（摩擦 fv/fc/bias、SourcedValue 物性）/IRuntimeModelView（**tryDynamicWorkCell→DynamicWorkCellConstView、gravityBase()＝R_world_baseᵀ·g_W、makeState 每线程**）/RuntimeNameMap ⑥端口/RT-\* 稳定码；§6.4/§13.2 dynamics 行（"rwsim 库由其链接"——WP-17-T02 链 rwsim 基线） |
| `units/policy.md` | v0.15 | `Draft` | 存在 | **dynamics 不消费 policy 语义**（无碰撞/无判定阈值需求）；CON-06 策略身份经快照 policyRef 承载（§4.1）；AT-19 中 dynamics 无碰撞入口（§11 V-19 行归属说明） |
| `units/execution.md` | v0.12 | `Draft` | 存在 | ITaskScheduler/TaskSubmission/九态/ITaskController/ICheckpointCoordinator/通道 IRDCHN/1/RunRegistry 五元组/缓存治理/`sdurws_ird_execution_worker` 共享 worker（**全文无逐域 worker**）；EX-\* 码 18 项 |
| `units/diagnostics.md` | v0.13 | `Draft` | 存在 | StableCodeRegistry（业务域前缀清单含 `DYN`，**内置码表 87 码零 DYN 条目**）、IDiagnosticFactory/DiagContext（评估路径 snapshot/slice 必填）、两级日志 |
| `units/ui.md` | v1.76 | `Draft` | 存在 | 静态白名单 **dynamics token 已占位**（§11.1）、StageId::TrajectoryDynamics（共享阶段）、IPluginUiRegistrar/SelectionService/七态投影/View3DSessionPoseContract 四常量（零修订机器可断言）；**WP-17-T09 未在 ui 卡登记**（§15.2 P-DYN-8） |
| `units/io.md` | 卡头 v0.9 | `Draft` | 存在 | 无 dynamics 直接消费（导出经 reporting/io 通道；本域零 CSV/JSON 解析） |
| `units/modeling.md` | v0.40 | `Draft` | 存在 | 间接上游：MDL-05 物性估算（GeometricEstimate 来源）、MDL-16 动力学参数层（额定/峰值力矩限值、摩擦参数入口）、MDL-21（R2 耦合矩阵——R1 阻断门禁）经快照对象闭包消费 |
| `units/requirements.md` | v0.16 | `Draft` | 存在 | OperatingCondition（payloads{toolRef,mass,com?,inertia?}、events{Grasp/Release/Dwell}、processParams、demands、appliesTo）、RequiredCaseSet（mandatory≡Must）、角色键 `req.conditions` |
| `units/kinematics.md` | v0.15 | `Draft` | 存在 | 间接参照：config 身份口径（configDigest→sliceId）、Quick 纪律（screening-only）、结果绑定模式；dynamics 不消费 kinematics 接口（无 FK/IK 需求——轨迹已带 q/qd/qdd） |
| `units/trajectory.md` | v0.1（2026-10-06） | `Draft` | 存在 | **上游结果消费面**：评估键 `trj-sequence-plan`、payload kindToken `trj.sequence-plan.v1`、TimedSample{t,q,qd,qdd,tcpPose}、Trajectory 身份块（snapshotId/sliceId/contractVersion/algorithmVersion）、TimeParameterization（knotTimesS/totalDurationS）、驻留段语义；§19.2 交接行"轨迹 TimedSample/分段时长作为 DYN-03 峰值/RMS 的输入面——消费 payload 或归档结果，R-1 经端口" |
| `units/reporting.md` | v0.16 | `Draft` | 存在 | 下游消费面：`IReportSectionProvider`（§9.2 装配期注册）、章节 `dynamics-envelope`（**归属 dynamics**，DYN-01～07 投影）、`drivetrain-operating-points`（归属 drivetrain）、包络合并呈现规则（"包络合并（呈现方式）"标注、不替代逐工况条目）、限定语词表（estimated/data-insufficient/screening-only/historical-superseded/not-applicable）、C 级章节注册前置＝对应域结果可产出 Completed envelope、大数组以 ResultBinding 引用保留 |
| `traceability/unit-status.json` | 随治理批次 | 机读索引 | 存在 | dynamics 条目：`{status: planned, masterWps: [WP-G], designCompletion: not-written, implementationStatus: not-verified}`——与本卡落盘前的磁盘事实一致 |

### 1.2 代码落位实测与差异登记（2026-10-06）

| # | 检查对象 | 实测结果 | 处置 |
| --- | --- | --- | --- |
| C-1 | `industrialrobot/dynamics/` 目录 | 存在，仅含 `include/sdurws/ird/dynamics/README.md`（占位说明："源码随对应任务卡（见 units/dynamics.md §9）落地；本文件不参与编译"） | 如实登记；README 的"§9"指向按本卡实际章节号（§3/§10）在 T02 落位时同步（README 指向核对随落位任务执行——runtime 卡 RT-T13 先例）。**已闭环（WP-17-T02）**：README 已改写为落位说明版（指向卡 §3.2/§12，列当前公共头 DiagCodes.hpp 与后续落位路线） |
| C-2 | `dynamics/core/`、`dynamics/plugin/`、`dynamics/worker/`、`dynamics/src/`、`dynamics/test/`、`dynamics/contract_test/`、`dynamics/CMakeLists.txt` | **均不存在**（当前尚未落位） | 本卡 §3.2 给出目标布局设计（明确标记为目标设计，不写成已存在）；落位归 WP-17-T02 |
| C-3 | 主 CMakeLists 中 dynamics 目标 | `sdurws_ird_dynamics` INTERFACE 占位（IRD_MODULES 循环注册 include 路径），无别名用例、无 `_plugin`/`_test`/`_contract_test` | 转真实库归 WP-17-T02（DTB §6 自检清单 20/20 对应行） |
| C-4 | `sdurws_ird_dynamics_worker` 目标 | **不存在；且本设计明确不创建**（与任务指令建议布局的差异及依据见 §3.3） | §3.3 差异登记 |
| C-5 | 已执行的 dynamics 构建/测试/契约测试/GUI 测试/验收记录 | **不存在**（本卡为纯文档交付，未执行任何构建、测试或验收） | 如实声明；§11 全部验证条目为设计，非执行结果 |
| C-6 | 与任务拆分的差异 | DTB §2.18 WP-17-T02 输出列为"计算库（链 rwsim 零 Qt 基线）＋`_plugin`"——**无 worker 目标**；与 ARCH §4.1/execution.md §3.1 共享 worker 口径一致 | 本卡与 DTB/架构零差异；任务指令建议布局中"必要时的独立 worker"按"不必要"处置（§3.3） |
| C-7 | diagnostics 内置码表 DYN-\* 条目 | 不存在（87 码零 DYN；DYN 前缀已在业务域清单预留，"阶段 B 起业务域注册须域卡在案"） | DYN-\* 拟注册清单见 §9.4，随 WP-17-T02/T03 注册 |
| C-8 | ui 卡中 WP-17-T09 / dynamics 域消费者登记 | 不存在（WP-10-T08 域消费者清单未列 WP-17；Playback 动力学衔接无接口登记） | 登记 P-DYN-8（§15.2），随 WP-17-T09/ui 卡增量修订双向补齐 |

> **WP-17-T02 落位登记（2026-10-06，构建落位任务——上表 C-1～C-8 为 T01 时点实测，本注为落位后状态）**：`dynamics/CMakeLists.txt` 已建成——`sdurws_ird_dynamics` 由上级 INTERFACE 占位升级为真实 STATIC 库（C++17、PUBLIC 链 core/evidence/runtime/execution/diagnostics 五条 §3.2 CMake 行登记边，ird_gates 白名单 "dynamics->…" 五行随任务登记并同步刷新 dependency-graph.json；project 边 T02 零公共值类型引用不落——requirements 卡 T02"runtime 边暂不登记"同款处理，实际引用时按 §3.2 增登；集成模式另链 L1 基线 sdurw_math/kinematics/models＋sdurwsim〔DTB §4.6 登记行〕＋Eigen3 PRIVATE〔参照 kinematics O-40〕；PUBLIC 形态与 §3.2 CMake 行"PRIVATE 链"字样的差异＝DiagCodes.hpp 内嵌 diagnostics/core 承载类型需随链到达＋测试目标表外边禁令，已落位各单元同以 PUBLIC 建边——P-RPT-9 同款，DTB §5.4 偏差登记面）；`sdurws_ird_dynamics_plugin` 最小可注册实现（自持描述符装配门面 `assembly/DynamicsPluginAssembly.hpp`——pluginId/titleKey 均有 ui.md §11.1/§3.5 登记出处，零 ui 编译边；selection WP-19-T02 同款先例）；`sdurws_ird_dynamics_test`/`_contract_test` 随落位登记（ctest LABELS ird——本注取代 §3.2 CMake 行"注册 _test/_contract_test（T10 起）"的时点口径：任务契约 WP-17-T02 acceptance 1 明文三目标同批创建）；`src/DiagCodes.cpp`＋`include/.../DiagCodes.hpp`＝DYN-* 15 码登记表（§9.4 全表物化——本单元依赖白名单含 diagnostics 编译边，走 kinematics WP-15-T02 注册函数形态〔CodeDescriptor＋registerDynamicsCodes 装配期注册，真实 StableCodeRegistry 验证〕，与 selection/drivetrain 纯登记表形态差异来自各自卡 §3.2 有无 diagnostics 边；卡面 §6.1/§10.2 提及但不在 §9.4 登记表的 DYN-FD-NUMERIC-ANOMALY 不进本批——随 WP-17-T05 先走 §9.4 增量修订再表尾追加）；占位 README 文案修正为落位说明版（C-1 闭环）；`dynamics/worker/` 仍不创建（§3.3/D-DYN-10，符合设计）。T03+ 源码集（DynTypes/Errors/DynConfig/InverseDynamics/ForwardDynamics/SeriesBuilder/Envelope/PowerEnergy/Evidence/Evaluators/TrajectorySourcePort/Commands/Handoff）尚未落地——§3.2 布局表任务列为权威。

> **WP-17-T03 实现落位登记（2026-10-07，RNEA 逆动力学评估器——§5/§10.1 落地面与实现口径）**：新增公共头 `DynTypes.hpp`（§4.4 类型面的 T03 必需子集——DynJointType/SampleNumericState/DynamicsSample/DynamicsValidity 四类型；其余类型随消费任务 T04/T07 增列同头，不预建占位〔NFR-MNT-04〕；`DynamicsValidity.externalValidationPending` 字段语义保留但 T03 恒 false——CON-03 Recorded 态事实在本任务输入面无通道，物性 SourcedValue 来源五类无外部验证态）、`Errors.hpp`（§10.0 域错误 DynamicsError：调用方错误 fail-fast 异常轨，token 前缀 dynamics/...、不发稳定码）、`InverseDynamics.hpp`（§10.1 评估器＋§5.3/§5.4 输入 DTO）与 `src/InverseDynamics.cpp`（RNEA 实现）。**实现口径与契约形态的微调（DTB §5.4 登记）**：①§10.1 `evaluate(request, context)` 的 request 为域内 DTO〔InverseDynRequest：CanonicalModel 只读引用＋基座系重力向量＋激励样本数组＋负载池/事件〕——不直接吞 evidence EvaluationRequest（AnalysisSnapshot/InputSlice/objectClosure 完整消费链随 T10 装配冻结；sliceId/上游轨迹 payload 身份字段届时入请求）；②单工况粒度——一次 evaluate 产出一个工况的 InverseDynOutcome（样本行＋DynamicsValidity＋诊断素材），多工况循环与包络合并归 T07 编排（§10.9 精神）；③RNEA 参数提取**单源 CanonicalModel**（§5.1 两种视图输入的读取面收敛：链几何/连杆物性/摩擦/工具全部经 CanonicalModel 只读读取——DWC 视图与 CanonicalModel 同为一次编译的产物〔S6/S7 同源〕，单源避免双读漂移；摩擦四态承载〔MDL-16 缺失降级〕只有 CanonicalJointFriction 具备；DWC 视图消费随 T05 正动力学 RobWorkSim 场景落地）；④SPD 完整复检不在本域重复——建模侧 MDL-06 断言②已强制（build() 拒绝非 SPD），本域只做卡面点名的附录 D 第 6 项对称性（1×10⁻¹²）＋有限性防御复检；⑤非有限输入按**样本级**处置（§4.6"该样本 NonFiniteInput"——任一关节输入非有限经耦合项传播必然污染全样本输出，整样本标记才与"非有限不可静默传播"一致），RNEA 输出非有限→该样本起工况失败（DYN-RNEA-FAILED 素材，§5.6）；⑥重力唯一入口＝请求携带的基座系重力向量（调用方自快照 `gravityBase()` 取）——接口参数面无 R_world_base，下游二次旋转结构上不可表达（D-DYN-3/AT-37 拦截面）；⑦上游轨迹经激励样本数组注入（ITrajectorySourcePort 的数据面）——归档读取适配器落位待裁决（P-DYN-1），本域零 trajectory 类型依赖（R-1）；⑧冒烟 header-only 纪律：rw::math 值类型只出现在提取边界逐元素读取（runtime RT-T03/BaseWorldTransform.cpp 同款先例），RNEA 内核为自持 Vec3/Mat3 逐元素算术（Eigen3 PRIVATE 链接面 T02 已建、本任务未消费——预备面保留）。测试面：`test/RneaTest.cpp`＋`test/TwoLinkFixture.hpp`（自持 CanonicalModel 直构夹具——R-2 不跨单元 include 他单元测试私有头）14 用例；`contract_test/RneaBoundaryContractTest.cpp` 2 用例（DYN-04 产品面零传动映射消费符号词表扫描＋include 白名单）。

> **WP-17-T04 实现落位登记（2026-10-07，输出序列与峰值/RMS 包络——§4.4/§7/§10.3-5 落地面与实现口径）**：`DynTypes.hpp` 表尾增列 §4.4 类型四枚——PeakRecord/DynamicsSeries/PowerEnergySummary/OperatingConditionResult（DynamicsEnvelope 连同 mergeEnvelope 随 T07 包络合并落位〔DTB §2.18 T07 行分界〕、DynamicsEvidence 随 T06/T10、JointSideSeriesPack 随 drivetrain 交接任务——不预建占位〔NFR-MNT-04〕）；新增公共头 `SeriesBuilder.hpp`（§10.3 序列构建器＋SeriesIdentity 身份块 DTO）、`Envelope.hpp`（§10.4 统计面 computePeaks/computeRms）、`PowerEnergy.hpp`（§10.5 功率/能量）与实现 `src/SeriesBuilder.cpp`/`src/Envelope.cpp`/`src/PowerEnergy.cpp`（CMake 源码集增列）。**实现口径与契约形态的微调（DTB §5.4 登记）**：①§10.4 以**具体类**形态落地（同 T03 先例——evidence 适配与注册面随 T10 装配冻结），mergeEnvelope 不在本批（见上分界），本头 token 表注释钉扎每关节恒 6 行峰值行序（τ⁺/τ⁻/速度/加速度/P⁺/P⁻——§7.2 枚举序，§7.6"(jointIndex, 量纲 token, conditionId) 全序"的落地面）；②峰值持续时间窗＝峰值样本在其**关节时间序**上的连续位等值 run（D-DYN-8 无阈值化；series 全局行序为多关节交错行，run 扩展以单关节 Ok 行序列为准；并列峰值取时间轴首个样本——tPeakS 确定性口径；非 Ok 行切断 run 连续性）；③τ_max⁺/P⁺ 取带符号实际值 max（全循环无正向值时为负值形态、信息不丢失），τ_max⁻/P⁻＝max(−值) 幅值形态；PowerEnergySummary.powerPeak 单记录位取合并幅值 max(|P|)（正反向分列由 computePeaks 承载）；④RMS＝τ_total² 梯形时间加权、T_cycle＝有效行全跨度（§7.3 含驻留；禁样本平均）；非 Ok 行不进统计、缺口不插值（缺口时长计入相邻有效对 Δt、贡献 0——保守低估，完整循环统计以 Complete 序列为准）；无有效行/行数<2/零跨度→显式 NaN（不伪造 0——§4.6）；⑤`includesDwell` 统计器口径＝"积分已按序列全程时间轴执行"（上游轨迹含驻留段即自动计入；统计器无段类型信息、不做任何速度阈值判定）；⑥SeriesIdentity 为 §10.3 身份块的字段级落地（§4.4 身份块＋plannedSampleCount〔计划数来自调用方——构建器无法从行集反推上游计划采样数〕＋validitySeed 事实透传〔frictionMissing/估算计数/forwardCheck——构建器无此事实通道，派生字段由行集重算覆写〕）；⑦非法示例处置分解：时间**倒退**＝§10.3 前置行明文违例→addSample 即抛 DynamicsError("series-non-monotonic")（DYN-SERIES-NON-MONOTONIC 语义 fail-fast）；同刻度**重复 (t, jointIndex) 行对**＝非法示例"addSample 不报错但 finalize 标记"→diagRefs 追加 DYN-INPUT-INVALID 语义素材＋完整性压 Partial（§4.3 上游校验才是正式拒绝点，builder 侧防御性二次校验）；⑧contentIdentity canonical 编码唯一实现点＝SeriesBuilder.cpp（域分隔 magic "IRDDYNS1"＋§4.4 字段序＋显式小端标量编码＋u32 长度前缀字符串＋diagRefs 排序后编码〔清单等价集合〕——SHA-256，同输入字节同摘要）；⑨P-DYN-9 遵守：能量/功率量纲零运行校验容差（产品侧无任何自设阈值），数值正确性唯一对照面＝测试黄金算例。测试面：`test/StatisticsTest.cpp` 16 用例（DynSeries 6＋DynEnvelope 4＋DynPowerEnergy 3＋DynStatsIntegration 3——含峰值平顶/孤立窗与段报告黄金、含驻留时间加权 RMS 解析对照与禁样本平均钉扎、E⁺/E⁻ 制动段符号、RNEA→序列→统计端到端、Prismatic+Revolute 混合链类型化不混算黄金、内容身份确定性与灵敏度、fail-fast 边界）；_contract_test 无新增（T04 零新跨单元边界——既有 BuildGraph/include 白名单/DYN-04 词表扫描自动覆盖新头）。测试期间测试侧期望式修正两处（构建器乱序输入跨时刻构成时间倒退被 fail-fast 正确拒收〔产品行为正确〕；E⁺ 手算漏跳变段梯形项½(2+0)×1——期望式修正，实现与 V-22 对照性质〔E_net=样本行 energyIntegralJ 末值〕一致通过）。

> **WP-17-T05 实现落位登记（2026-10-08，正动力学一致性检查——§6/§10.2 落地面与实现口径）**：新增公共头 `ForwardDynamics.hpp`（§10.2 检查器＋§4.2 forwardCheck 六字段域内 DTO ForwardCheckSettings＋ForwardCheckRequest/ForwardCheckOutcome）与 `src/ForwardDynamics.cpp`（CMake 源码集增列）；DiagCodes.hpp/.cpp 表尾增行 16 `DYN-FD-NUMERIC-ANOMALY`（§9.4 增量修订随本批——v1 表 15 码→16 码，DiagCodesTest 登记数组同批同步）。**实现口径与契约形态的微调（DTB §5.4 登记）**：①§10.2 `IForwardDynamicsValidator` 抽象接口以域内**具体类** ForwardDynamicsValidator 落地（T03/T04 先例——evidence 适配与注册面随 T10 装配冻结）；②request 为域内 DTO——参考段以激励样本数组注入（同 T03 数据面），τ_ref 由检查器内部经公共 `InverseDynamicsEvaluator::evaluate` 产出（§6.1 时序图第①步检查器内化——调用方不可注入外部力矩序列，"用另一工况的负载做仿真"的共源违约在本接口面结构上不可表达，§10.2 非法示例）；③outcome 的 `tOfMaxErr` 单字段增列为 `tOfMaxErrQ`/`tOfMaxErrQd` 两位（§6.1 ④"各最大误差发生时间"逐量承载）；payload 扩展块（误差序列）不进本结构——DYN-08 回放数据面归 T08；④DynConfig.hpp 全量配置面与 canonical 编码随 T10 装配落位（forwardCheck 六字段随本头承载，§4.2 canonical 编码与 configDigest→sliceId 身份链归装配层）；⑤**正动力学引擎选型（P-DYN-7 落位锁定，见 §6.1 登记注）＝同源 RNEA 质量阵/偏置力提取＋固定步长四阶龙格—库塔积分**，integratorToken 封闭词表锁定值 `rk4-fixed`（§4.2 回填）；RobWorkSim 物理引擎（rwsim::simulator::PhysicsEngine 工厂）经实测不落位，四条依据：ODE 依赖未启用（集成构建缓存 ODE_DIR-NOTFOUND 实测）、Bullet 引擎为 RW_ADD_PLUGIN 动态插件机制（BtPlugin.cpp 实测——L2 进程内计算库无插件加载入口，直连 BtSimulator＝依赖 rwsimlibs 私有实现头非 rwsim 公共契约）、工况条件负载需向引擎场景注入负载体（运行中 addBody——rwsim 公共 API 无此消费先例，注入即自建第二套关节链违反 CM-0/D-DYN-2）、迭代约束求解器容差内漂移与 §10.2 确定性行"同配置同输入→等价误差序列"冲突＋冒烟零外联符号纪律（rwsim 引擎符号为框架 .cpp 外联，冒烟配置树无框架库可链——本实现全链零外联，双模式同源可构建）；⑥仿真变体一致性：M/h 单样本提取调用一律传**参考样本区间起点时刻**——evaluate 内部事件时间线"样本时刻 t≥tEvent 生效"（§5.4 保守边界）使正动力学侧变体与 τ_ref(t_j) 严格同变体，变体切换只发生在采样保持区间边界（⑤共源强制的结构性实现）；⑦发散检出界 1e10（rad/m、rad/s、m/s）与质量阵奇异判据 1e-14×‖M‖∞ 为实现常量（异常检测判据非工程判定阈值，黄金算例锁定面）；relTolerance 在固定步长实现下为记录性身份/审计分量（无自适应消费点——如实登记，积分器换自适应实现时接手语义）；⑧**测试期间发现并修复 T03 既有缺陷一处（产品缺陷，回归用例全绿）**：RNEA 物性提取对 NotProvided 惯量的分支未显式置零——`RneaBody.inertia` 保留 `Mat3` 默认构造值（单位阵，旋转恒元语义）＝"每个缺失惯量的连杆/负载自带 1 kg·m² 惯量"的静默错误值；T03 降级用例只测标记面未测数值面故未检出，本任务零物性黄金算例（质量阵奇异检测）暴露——修复为连杆缺失分支与点质量路径（extractEndComponent）显式零张量，T03/T04 既有 39 用例回归通过。测试面：`test/ForwardDynamicsTest.cpp` 11 用例（DynForward——解析黄金算例〔伸直构型 q₂≡0 匀加速：τ_ref 恒定→采样保持零误差→RK4 对二次多项式轨迹精确，误差机器精度量级<1e-12，toleranceQ=1e-9 判 Passed＋独立解析界双重对照〕/一致性超阈值 Failed＋比较型素材/发散检出 DYN-FD-DIVERGED/质量阵奇异 DYN-FD-NUMERIC-ANOMALY〔即上述缺陷的检出用例〕/NotRun 空与单样本与初始态非有限/NotApplicable Skip 短路/取消语义/确定性复现/负载事件共源〔中段 Grasp 两路同变体 Passed＋摩擦缺失素材透传〕/fail-fast 十项边界）；`contract_test/ForwardDynamicsBoundaryContractTest.cpp` 2 用例（DynForwardBoundary——P-DYN-7 落位选型静态钉扎〔正动力学面零 rwsim 物理引擎消费符号词表扫描：PhysicsEngine/BtSimulator/DynamicWorkCell/RigidDevice/makeState/namespace rwsim〕＋公共契约形态编译期核对〔词表锁定值/四态枚举同源/默认 Skip〕）。

> **WP-17-T06 实现落位登记（2026-10-08，数据不足降级与可信来源标记——§5.5/§8.4/§9.4/§10.6 落地面与实现口径）**：`DynTypes.hpp` 表尾增列 §4.4 末类型 `DynamicsEvidence`（§8.4 dyn Profile 六项的域内组织形态——四 refs 组＋两建议可用性位；各 refs 组只在对应证据项 Satisfied 时非空，逐项状态/原因/摘要由装配器 evidenceItems() 承载，单一事实源不重复）；新增公共头 `EvidenceBuilder.hpp` 与 `src/EvidenceBuilder.cpp`（CMake 源码集增列）。**实现口径与契约形态的微调（DTB §5.4 登记）**：①§10.6 `IDynamicsEvidenceBuilder` 以域内**具体类** DynamicsEvidenceBuilder 落地（T03～T05 先例——评估器注册面随 T10 装配冻结），方法签名与卡面一致（addSeries/addConditionResult/addForwardCheck/build→DynamicsEvidence）；②证据项清单与缺失清单以显式访问器交付（`evidenceItems()` 恒 6 行〔§8.4 表行序〕/`missingList()` 全量不短路/`trustQualifiersCached()`——T06 契约"evidence EvidenceItem 承载"与 §8.4"缺失项全量列出"的执行面；卡面未给清单访问器签名，本落地面为最直接对应）；③dyn Profile 登记面物化＝`dynProfile()`（§8.4 表逐行实例化：required 四项＋suggested 两项、description 锚定表 4 行文、替代标志 ①②⑤ true/③④ false；profileId="dyn"/version="1" 常量唯一书写点，contentIdentity 保留值——注册时由 evidence 计算，真实 EvidenceProfileRegistry 注册用例验证可注册），装配期注册归 L5（§8.4"先于评估器注册"顺序不变）；④**数据不足的证据状态映射**（卡面无逐态对照表，按 evidence §6.2 五态语义＋V-09/V-10 行"property-friction-provenance=Invalid/降级标记"落定）：摩擦缺失→必需项③ **Invalid**＋invalidReason=DYN-FRICTION-MISSING 稳定码诊断；估算物性→③ **Invalid**＋DYN-PROPERTY-DOWNGRADED（附估算计数）；外部验证未完成→③ **Unverified**（CON-03 Recorded 态的 §6.2 本义映射；Invalid>Unverified 保守优先序）；⑤建议项状态映射：正动力学 Passed→Satisfied/Failed→Invalid（invalidReason＝检查器附带 DYN-FD-* 首条诊断，空清单防御性合成）/NotApplicable（Skip）→NotApplicable＋原因必填（C2 不计缺失）/NotRun→Missing（V-14 不阻断）；power-energy 项 timeParamAvailable=false→Missing（§4.6 NaN 不是产物，不伪造积分）；⑥**限定语纪律单点**＝`trustQualifiers(validity)`（§5.5 三层→§9.6 词表三值 data-insufficient/estimated/external-validation-incomplete，层序＝登记序，全干净＝空清单——不弱化也不添加；token 常量唯一书写点，contract_test 编译期钉扎 reporting §6.4 词表冻结值）；⑦证据项 artifactDigest＝Satisfied 项绑定素材的 canonical 摘要投影（magic "IRDDYEV1"＋codec 版本＋itemId 行序号＋各素材字段定宽小端编码——SHA-256，非往返载体无 parse，同输入字节同摘要）；⑧fail-fast 面：重复注入（单装配器单工况）/未注序列先注结果（缺绑定锚）/conditionId 不一致（错误工况引用——evidence §6.2 EV-COV-2 同源）/身份块不完整（§10.0 缺身份拒绝）——全部 DynamicsError 调用方错误轨；未装配项显式 Missing 不可省略（§10.6 非法示例行），build 不因数据缺失失败（数据不足是合法产出）。测试面：`test/EvidenceBuilderTest.cpp` 8 用例（DynEvidence——dynProfile 表 4 对账＋注册期校验＋真实 EvidenceProfileRegistry 注册＋内容身份确定性/干净链全 Satisfied 无限定语＋validateEvidenceItems 与 checkEvidenceCompleteness 双零问题〔V-48 对照面〕/摩擦缺失→③ Invalid＋DYN-FRICTION-MISSING＋对账 requiredGaps 恰含该项〔V-09〕/估算物性→estimated 限定语＋DYN-PROPERTY-DOWNGRADED 不包装精确〔V-10〕/缺失清单全量不短路〔三面缺失并存〕/正动力学四态映射/fail-fast 契约/Empty 与未 build 访问器）；`contract_test/EvidenceBoundaryContractTest.cpp` 2 用例（DynEvidenceBoundary——dyn Profile 六项 itemId 词表编译期静态钉扎〔§8.4"实现落位冻结"列〕＋限定语 token 词表冻结与 trustQualifiers 三层映射契约〔RPT-05 不弱化的机器判读面〕）。测试期间发现并修正**测试侧缺陷一处**：用例在临时 vector 上取指针（`findItem(b.evidenceItems(),…)` 返回悬垂指针——未定义行为致字符串字段读取随机为空），全部改为具名 vector 后消费；产品实现无缺陷检出。

> **WP-17-T07 实现落位登记（2026-10-08，多工况与包络合并——§7.4/§8.3/§10.4 落地面与实现口径）**：`DynTypes.hpp` 表尾增列 §4.4 类型 `DynamicsEnvelope`（DYN-07 合并产物——逐关节 JointEnvelope〔jointIndex/jointObjectId/力矩正反·速度·加速度·功率幅值峰值/rmsTau/contributingConditions/envelopeComplete〕＋包络级 conditionCount/coversAllMandatory/contentIdentity）；`Envelope.hpp`/`src/Envelope.cpp` 增列 `DynamicsEnvelopeCalculator::mergeEnvelope`（§10.4 第三方法的唯一实现点——T04/T07 分界注兑现）；新增私有实现头 `src/CanonicalDigest.hpp`（DigestWriter 自 SeriesBuilder.cpp 文件局部抽取为 src/ 私有头——序列/包络两摘要消费点共用一套显式小端编码工具，防两套漂移；R-2 私有头不入 include/、不跨单元暴露，SeriesBuilder.cpp 同步改 include）。**实现口径与契约形态的微调（DTB §5.4 登记）**：①§10.4 草案第二形参 `CaseCoverageSnapshot`（evidence 卡未产出该类型）以**快照冻结 `evidence::RequiredCaseSet`** 承载（§8.1"工况集合＝快照 caseSet〔RequiredCaseSet〕"原文口径；覆盖矩阵分母——任务契约 acceptance 2 明文"分母口径按快照冻结 RequiredCaseSet"；已登记 evidence 编译边内公共头消费，零新增编译边）；②包络 canonical 摘要 magic "IRDDYVE1"（公共头常量 `kEnvelopeContentMagic`——契约测试编译期钉扎；编码字段序＝conditionCount→coversAllMandatory→逐关节行〔jointIndex/jointObjectId/五峰值记录槽〔§7.2 token 序，token 4/5 并入 powerPeak 单记录位——§4.4 包络结构仅五个 PeakRecord 槽〕/rmsTau/contributingConditions〔升序长度前缀〕/envelopeComplete〕——SHA-256 显式小端，同输入同摘要，NFR-COR-02）；③**合并规则落地**：工况处理序＝conditionId 字典序升序（输出与入参顺序无关）；逐关节六 token 跨工况严格大于才替换（并列峰值取 conditionId 更小工况的记录——值/发生时间/段/窗随胜者整体归因，确定性）；powerPeak＝跨工况幅值合并 max(|P|)（逐工况取 max(max(P),max(−P))——与 PowerEnergySummary.powerPeak 幅值形态同口径；胜出值恒非负）；rmsTau＝跨工况 computeRms 取 max（RMS 不可跨工况时间加权合并——各工况循环时长/轴不同、无定义的组合公式，max 是无新语义的唯一合并；NaN 不参与、全无效→NaN 显式无效——§7.3 不伪造 0）；contributingConditions＝该关节有峰值贡献的工况集（升序——稳定排序）；envelopeComplete＝该关节全部来源 Complete（任一 Partial/Failed→false＋来源清单仍在——§7.4"不输出看似完整的包络"；Empty 完整度来源无关节行、不毒化任何关节行，其缺项经覆盖呈现〔④〕与 evidence 覆盖矩阵独立可见）；④**coversAllMandatory 呈现参考口径**（EVI-02，acceptance 2）：分母＝coverage 的 enabled∧mandatory 条目（disabled Must＝NotApplicable 不参与——§8.1 覆盖矩阵 C4 行）；分子＝results 中 completeness≠Empty 的 conditionId 集（Empty＝执行未产出样本，保守不计——缺项方向只低报不高报）；空分母→true＝平凡完备（P-EV-7——保守处置归 evidence 汇总判定层）；**包络合并不替代必验工况覆盖规则**（逐工况 Executed 五态判定归 evidence 覆盖矩阵——合并器无从解读执行态词表，本布尔仅按"在场且产出样本"计，仅为呈现参考——§4.4 原文）；⑤**fail-fast 装配契约**（§10.0 调用方错误轨，DynamicsError 不发稳定码）：conditionId 与 series.conditionId 不恒同／validity 与 series.validity 不恒同〔§4.4 透传契约〕／results 内 conditionId 重复／result.peaks 与 computePeaks(series) 逐位不一致〔峰值装配必须产自统计口径唯一实现点——含"序列有 Ok 行而 peaks 为空"的装配缺失形态〕／同关节行 jointObjectId 矛盾／coverage 条目 caseId 空或重复〔evidence §4.1.3 保留值拒绝＋冻结集唯一性防御面〕——全部拒绝合并；⑥Empty 语义（§7.4/§7.6）：无任一入参产出统计行→joints 空（"包络不产出"，绝不 0 值伪装），身份字段照算（conditionCount/coversAllMandatory/contentIdentity——空包络仍可寻址）。测试面：`test/EnvelopeMergeTest.cpp` 7 用例（DynEnvelopeMerge——三工况手工黄金 max 合并与来源归因〔并列 τ⁻ 按 conditionId 序取胜＋逆序重合并内容身份逐位同〕/多负载〔工具 2.0 kg〕＋保持型设计工况统一 RNEA 端到端〔P-DYN-4：保持工况只是给定静态样本的普通工况，无自设构造语义；轻/重载解析黄金 τ₁/τ₂〕/Partial 来源→envelopeComplete=false＋来源清单仍在＋覆盖呈现独立〔V-26〕/覆盖分母口径〔V-25 漏验呈现/disabled 不参与/空分母平凡完备/纯 Should 完整包络不得声称覆盖——EVI-02 钉扎〕/Empty 语义〔空集/全无 Ok 行/Empty 来源覆盖保守不计〕/RMS NaN 不参与 max 与全无效显式 NaN/fail-fast 装配契约七面）；`contract_test/EnvelopeBoundaryContractTest.cpp` 2 用例（DynEnvelopeBoundary——mergeEnvelope 契约形态编译期核对〔签名静态钉扎〔§10.4 设计基线＋①分母承载〕＋magic IRDDYVE1 词面冻结＋token 常量〕＋EVI-02 覆盖分母口径行为契约〔分母翻转→coversAllMandatory 翻转且内容身份随之改变／coversAllMandatory 与 envelopeComplete 互相独立／空结果集 Empty 语义〕）。测试期间发现并修正**测试侧期望式错误一处**：多负载算例工具对关节 1 的力臂漏计 L₁（首跑 FAILED：实得 −25.0155、错期望 −15.2055——按 T03 用例 3 同式"工具对关节 1 力臂＝L₁+tcpOffset"修正后复跑全绿；产品实现无缺陷检出）。

> **WP-17-T08 实现落位登记（2026-10-09，曲线联动与峰值定位/回放数据——§9.5/§10.7 落地面与实现口径）**：新增公共头 `Replay.hpp`（DYN-08 数据面——`JointCurves`/`CurveProjection` 曲线联动投影与 `ReplayJointState`/`ReplayFrame`/`ReplayData`/`ReplaySample` 回放数据四类型＋`DynamicsCurveProjector`/`DynamicsReplayProjector`/`DynamicsPeakLocator` 三投影器类）与 `Commands.hpp`（§10.7 落地面——§9.5 五命令 token 词表常量＋`ReplaySessionContract`/`kReplaySessionContract` 零修订会话契约＋`CommandPayload`/`CommandOutcome` 域内 DTO＋`DynamicsCommandHandler` 具体类）；`src/Replay.cpp`/`src/Commands.cpp` 两实现 TU 落位（CMake 源码集增列）。**实现口径与契约形态的微调（DTB §5.4 登记）**：①§3.2 布局表 Commands.hpp 行兑现＋**布局表外增头 Replay.hpp**（§9.5 三命令数据面承载头——T05 Evidence.hpp→EvidenceBuilder.hpp 先例同款登记；命令面〔词表/契约/受理〕与数据面〔投影/回放/定位〕分头承载，两 TU 零互相依赖）；②§10.7 草案抽象接口以**域内具体类** DynamicsCommandHandler 落地（T03～T06 先例——ui CommandRegistry 注册面随 T09 装配冻结）；③**View3DSessionPoseContract 四常量的 dynamics 侧承载＝ReplaySessionContract 同值结构**（§9.5 replay-at 行"四常量零修订机器可断言"的承接——dynamics 依赖白名单五登记边不含 ui，R-1/R-2 禁止 include ui 公共头，故以自有 constexpr 结构承载同值语义〔sessionStateOnly=true／writesDesignModel=false／producesRevision=false／invalidatesResults=false〕；对账锚＝两侧契约测试以同一文档出处〔ui.md/View3DContract.hpp KIN-06/AT-04 钉住值〕字面冻结四值——ui 侧已由 sdurws_ird_ui_test View3DContractTest 钉住，dynamics 侧随本批 DynCommandsBoundary 钉住，T09 装配层同编译单元消费两侧时自然对账）；④**命令受理/拒绝语义**（§10.7 无逐值定义，按 outcome"受理"语义落定）：词表封闭精确匹配〔大小写敏感〕，表外 token／负载交叉校验错配→accepted=false＋rejectionToken（"unknown-token"/"payload-token-mismatch"）——**拒绝走 outcome 不走异常轨**（消费方为 UI 会话态，误用属用户可见交互而非进程级契约违约；§10.0 fail-fast 轨保留给评估入口与投影防御面）；⑤**峰值定位消费 T04 computePeaks 行集零重算**（§9.5"UI 线程零计算"的结构性执行——locate 不复算统计，只做行集结构对账〔行数＝Ok 关节数×kPeaksPerJoint，违约 fail-fast"peaks-series-mismatch"〕后按 Envelope.hpp token 表行序取行；Ok 关节表以序列为推导锚——关节 0 全非 Ok 时行集自关节 1 起，仅凭行下标无法反推关节）；⑥**投影/回放构建的行序防御面**：t 倒退→"series-non-monotonic"（同 SeriesBuilder 口径）；同刻度 (t, jointIndex) 行重复→"series-row-duplicate"**fail-fast 而非 builder 的 finalize 标记**（投影是曲线显示直接数据源，重复行取值歧义在显示面不可接受，且投影器零计算零诊断、无 diagRefs 产出通道——结构违约只能 fail-fast 暴露）；⑦回放数据＝DYN-08 数据面随 payload、不入 dyn Profile（§9.5 投影行/D-14——ReplayData 由调用侧随 payload 组装归档，本域零写盘零修订；曲线投影 completeness 透传供 UI 标注缺失，非 Ok 行剔除并 nonOkCount 如实计数）；⑧**插值查表口径**（§9.5"UI 线程（插值查表）"）：区间 [tL,tR) 段归属取左帧（§5.4 保守边界左闭精神）、逐关节双指针对齐（共有关节五量线性混合、单侧独有关节单侧取值＋completeAllJoints=false——不跨帧虚构）、越界不外推（§4.3"不插值外推"同口径）、非有限时刻 fail-fast"replay-time-invalid"；"UI 线程零计算"口径＝纯数据重组（值＝样本行原值直拷或相邻两样本线性混合，无 RNEA/积分/统计聚合——§9.5 备注"RNEA/仿真/统计全在 worker"不排除投影重组）。测试面：`test/ReplayCurveTest.cpp` 18 用例（DynCurveProjector 4——两关节五通道黄金投影逐字段直拷对账／非 Ok 行剔除＋nonOkCount 计数＋Partial 透传／行序违约 fail-fast〔构建器 addSample 拦截＋投影器重复行与手工倒退序列双路径 token 核对〕／Empty 显式；DynReplayData 3——帧重组黄金〔帧 t/段定位键/帧内关节升序/五量直拷/严格递增〕／缺行帧 completeAllJoints=false 不截断／Empty 无帧；DynReplaySampler 4——帧点精确命中原值直拷／区间中点 α=0.5 线性插值逐点手算黄金＋段取左帧＋端点亦帧点／越界不外推＋NaN fail-fast／空数据集空态＋缺行帧逐关节独立插值单侧取值；DynPeakLocate 3——两关节全 6 token 定位与 computePeaks 行集逐位对账＋峰值三要素手工黄金抽查〔速度峰 4 rad/s@t=2 段 1／全负循环力矩正峰带符号 −5 N〕／无 Ok 行关节与空序列定位空态／token 越界与行集截断 fail-fast；DynCommands 3——五命令全受理四语义位零修订〔AT-04 机器断言〕＋表外与错配负载拒绝〔大小写敏感〕／零修订契约常量与词表字面冻结；DynDyn08Chain 1——曲线投影→computePeaks→locate→sampleAt(tPeak)→replay-at 受理全链联动〔"曲线游标跳转峰值时刻＋三维姿态同步"的领域侧演练，链尾零修订钉扎〕）；`contract_test/CommandsBoundaryContractTest.cpp` 3 用例（DynCommandsBoundary——§9.5 五 token 词表与 kReplaySessionContract 四值编译期 static_assert 钉扎＋CommandOutcome 拒绝形态默认安全／命令面零动力学计算红线源码级词表扫描〔Commands.hpp/.cpp 剥注释后零评估器/统计器/投影器消费符号——§10.7 硬边界〕／受理结果与契约逐位恒等）。测试期间修正**缺陷两处**（首跑 8 FAILED 复盘后全绿）：①**产品缺陷（本批新增代码）**——投影器 Ok 关节收集与关节行推进按"同关节行连续"假设实现，而行序按 (t, jointIndex) 交错排列：关节表膨胀〔2 关节误收 6 项〕且关节 0 的非首刻度行被跳过（黄金投影用例检出）；修复为逐行查重插入＋升序排序＋逐行二分定位（交错行序安全）；②测试侧错误——倒退用例把构建器 addSample 的抛出放在 EXPECT_THROW 外（构建器对倒退是 addSample 即抛非 finalize，SeriesBuilder 契约）；修正后另补投影器对"绕过构建器的手工倒退序列"的防御路径覆盖。`sdurws_ird_dynamics_test` **83/83 通过**（DynDiagCodes 5＋DynBuildRedLine 4＋DynRnea 14＋DynSeries 6＋DynEnvelope 4＋DynPowerEnergy 3＋DynStatsIntegration 3＋DynForward 11＋DynEvidence 8＋DynEnvelopeMerge 7＋本批 DynCurveProjector 4＋DynReplayData 3＋DynReplaySampler 4＋DynPeakLocate 3＋DynCommands 3＋DynDyn08Chain 1）；`sdurws_ird_dynamics_contract_test` **20/20 通过**（DynBuildGraph 6＋DynPluginAssembly 3＋DynRneaBoundary 2＋DynForwardBoundary 2＋DynEvidenceBoundary 2＋DynEnvelopeBoundary 2＋**DynCommandsBoundary 3**）；gtest XML 留痕 traceability/gtest-reports/wp-17-t08/。双模式构建零错误（集成模式 sdurws_ird_dynamics/_plugin/_test/_contract_test Release 增量构建零错误＋独立冒烟全树构建退出 0）＋ird_gates 零命中（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02）。§11.5 GUI 手动点验通道仍归 T09（本批零 GUI 用例——领域数据面与命令面无 GUI 载体）。

### 1.3 本卡与上游文档的编号口径

- 需求 ID（DYN/TRJ/MDL/CON/EVI/TASK/SEL/OPT/NFR/AT/P-xx）一律回指 REQUIREMENTS v1.16，不重定义、不收窄、不扩大（AGENTS §6.1）。
- 任务编号 WP-17-Txx/WP-18-Txx/WP-23-Txx 沿用 DTB v0.53；本卡内部不新增 WP 编号。
- 本卡自产编号：设计决策 D-DYN-x（§15.1）、待裁决项 P-DYN-x（§15.2）、稳定诊断码建议清单 DYN-\*（§9.4）。三套编号仅在本卡辖域使用，跨卡引用须经对端卡登记。

---

## 2. 需求承接、R1/R2 边界和职责边界

### 2.1 需求条目承接（DYN 家族全文承接，不改语义）

| 需求 | 语义要点（摘引 REQUIREMENTS v1.16） | 本卡承载章节 |
| --- | --- | --- |
| DYN-01（P0/C/R1，WP-17） | 基于 RobWorkSim 刚体模型和递归牛顿—欧拉计算逆动力学 | §5、§12 T03 |
| DYN-02（P0/C/R1） | 重力、连杆惯性、末端负载、外部力以及黏性+库仑关节摩擦模型（摩擦参数 fv/fc/偏置的建模采集入口见 MDL-16，未填写触发 DYN-06 降级——M-8 闭环） | §5.3/§5.5/§9.3、§12 T03/T06 |
| DYN-03（P0/C/R1，支持 WP-18） | 输出每轴转角/位移、转速、加速度、类型化广义力（转动 N·m、移动 N）、机械功率、峰值和 RMS 包络；峰值必须报告持续时间窗和所在轨迹段，RMS 基于完整任务循环（含驻留） | §4.4/§7、§12 T04 |
| DYN-04（P0/C/R1，WP-18） | 输出与候选传动无关的关节侧结果；共享计算层提供唯一 `DriveTrainMappingEvaluator`（映射契约：电机侧工作点、η⁺/η⁻、反射惯量、惯量比、能量分项、四象限统计；M-12 虚功映射 τ_motor＝Cᵀ·τ_joint＋J_rotor·θ̈，θ̈＝C⁺·q̈；不对角化/准静态；C 非常矩阵或病态诊断阻止；转子等效惯量按 SEL-10）——**映射实现归 drivetrain（WP-18-T03），dynamics 只交付关节侧输出与消费契约** | §8.2、§13.1 |
| DYN-05（P0/C/R1） | 使用明确控制输入、初始状态、步长和容差的 RobWorkSim 正动力学场景进行响应一致性检查和异常检测 | §6、§12 T05 |
| DYN-06（P0/C/R1） | 物性或摩擦数据不足时标记可信等级，不把估算结果包装成精确结论 | §4.4/§5.5/§9.3、§12 T06 |
| DYN-07（P1/C/R1） | 支持多负载工况、急停/保持等设计工况和结果包络合并（包络合并不替代必验工况覆盖规则，见 EVI-02） | §8.3、§12 T07 |
| DYN-08（P1/C/R1） | 提供各关节曲线联动、峰值定位和三维轨迹时刻回放 | §9.5、§12 T08 |

### 2.2 跨家族需求承接

| 家族 | 条目 | 对 dynamics 的约束 | 承载章节 |
| --- | --- | --- | --- |
| MDL | MDL-06（编译原子性）/MDL-14（RuntimeNameMap）/MDL-16（动力学参数与限值层：额定/峰值力矩限值、摩擦 fv/fc/偏置入口、合成质量/质心/惯量与估算区分来源）/MDL-21（R2 耦合矩阵——R1 阻断）/MDL-22（基座—世界变换单一不变量） | 物性/摩擦/限值输入链（SourcedValue 来源标记）；重力投影唯一消费 `gravityBase()`，禁止二次旋转（AT-37）；R1 耦合链由 modeling/runtime 门禁阻断，dynamics 零耦合代码且防御性拒绝（§5.6） | §4.1/§5/§8.2 |
| TRJ | TRJ-01～08（间接） | 轨迹为动力学唯一运动学激励源：消费不可变、带时间参数的轨迹（q/qd/qdd/时间轴/段结构/驻留）；轨迹无时间参数→拒绝正式评估 | §4.3 |
| CON | CON-01～06 | 快照评估、三态正交、切片内容身份（轨迹/工况/物性变化→失效）、策略/名称身份经快照承载 | §4.1/§9.4 |
| EVI | EVI-01/02 | 评估模式、表 4 动力学证据行逐项实例化（`dyn` Profile）、必验工况覆盖、五级汇总 | §8.4/§9.2 |
| TASK | TASK-01～03 | 能力声明、合法组合（表 3）、任务五元组与迟到结果 | §9.1 |
| SEL | SEL-05/SEL-10 | selection 只能消费 drivetrain 统一电机侧工作点（dynamics 不直接供电机侧量）；SEL-10 回填→新修订→复算提示，复核前不沿用通过结论（切片失效自动承载） | §8.2/§9.3/§13.2 |
| OPT | OPT-03/05/06/07 | 阶段 C 各域交付使节拍等指标可算（OPT-07 命名依据）；**OPT-D（R2）才将 dynamics evaluator 作为联合内层**——本卡只保证可组合边界，不实现优化 | §13.3 |
| NFR | NFR-COR-01/02/03/05、NFR-PERF-01/02/03、NFR-REL-02/03、NFR-MNT-01/03/07 | 二连杆解析算例对照（附录 D 第 9 项）；同输入+版本+配置+线程→等价结果；非有限数拒绝；碰撞零实现（NFR-COR-05/AT-19 中 dynamics 无入口）；响应性/取消 2 s/分批流式；worker 崩溃隔离；零 Qt；单一权威；无前缀拼接 | §5/§6/§9/§11 |
| AT | AT-05/07/10/11/19/27/34/37/38 | 见 §14 追踪矩阵与 §11 观测点归属 | §11/§14 |
| 附录 D | 第 6/7 项（物性断言）、第 8 项（并行归约 1×10⁻¹²）、第 9 项（解析算例对照 标量相对 1×10⁻⁹＋黄金算例逐例声明）、第 10 项（TRJ-05——上游轨迹已满足） | 物性张量对称性相对 1×10⁻¹²、SPD 特征值严格＞0（建模侧断言，dynamics 只读消费合格模型）；dynamics 侧校验容差只有两类来源：测试对照（黄金算例逐例声明）与分析配置（FD 一致性容差，进身份）——**功率/能量/惯量/质量量纲在 runtimeAbsoluteTolerance 中无默认（nullopt），不得引入产品侧相对校验**（附录 D C7） | §4.5/§6.4/§7.6 |

### 2.3 R1/R2 边界表（阶段 C 承诺面；图示＝任务指令要求的"R1 动力学能力与 R2 耦合矩阵扩展边界表"）

| 能力 | 批次 | 本卡处置 |
| --- | --- | --- |
| RNEA 逆动力学（重力/连杆惯性/末端负载/外部力/黏性+库仑摩擦；关节空间精确含全部耦合项） | R1 | §5 全量设计 |
| 输出序列（转角/位移、转速、加速度、类型化广义力、机械功率） | R1 | §4.4/§7 |
| 峰值（持续时间窗＋所在轨迹段）与完整任务循环 RMS（含驻留） | R1 | §7.2/§7.3 |
| 正动力学响应一致性检查（DYN-05） | R1 | §6 |
| 数据不足降级与可信来源标记（DYN-06） | R1 | §5.5/§9.3 |
| 多负载工况、急停/保持设计工况、包络合并（DYN-07） | R1（统计面）；急停/保持工况模板结构归 requirements（P-DYN-4） | §8.3 |
| 曲线联动、峰值定位、三维轨迹时刻回放数据（DYN-08） | R1 | §9.5 |
| drivetrain 交接（关节侧输出＋消费契约） | R1 交付契约面；映射实现归 WP-18-T03 | §8.2 |
| MDL-21 耦合矩阵 C 全链消费 | **R2/阶段 D**（WP-18-T05）——dynamics 零耦合矩阵代码、零虚功映射、零电机侧换算 | §8.2.4/§13.1 |
| OPT-D 联合优化内层评估器 | **R2**——本卡只保证可批量/可取消/可缓存兼容/可恢复的评估边界 | §13.3 |
| R2 并行、检查点、暂停/继续、内存节流、性能规模化 | **R2**（execution/optimization 承担）——dynamics 只声明能力位 | §9.1.3 |
| 暂停/继续（Paused 态） | R2 承诺；R1 显式声明不支持并反馈 | §9.1.3 |

**R1 强制边界（任务指令"阶段范围"节逐条承接）**：

1. 目标链含 mimic、闭环或 R1 禁止的线性耦合时，**不得静默按独立关节链计算**——阻断归 modeling/runtime 统一门禁（MDL-21/§2.1 矩阵），dynamics 消费的快照必然已过门禁；dynamics 侧另设防御性拒绝（§5.6）。
2. MDL-21 尚未启用时，耦合链由上游门禁阻断并给出诊断——dynamics 不重复实现该判定。
3. 不得通过 dynamics 内部对角化、准静态近似或默认独立关节处理绕过门禁（DYN-04/M-12 红线；违反即 review 打回）。
4. R1 的无耦合旋转传动可由 drivetrain 以等价对角传动比形式处理（WP-18-T03 验收项"常矩阵 C 下与对角传动比等价验证"）——**但该映射在 drivetrain，dynamics 不实现、不复制 τ_motor＝Cᵀ·τ_joint＋J_rotor·θ̈、不自行计算 θ̈＝C⁺·q̈**。

### 2.4 职责边界：拥有／消费／不拥有表

**dynamics 拥有**（本单元为唯一所有者）：

| # | 拥有项 | 依据 |
| --- | --- | --- |
| O1 | 关节空间逆动力学评估（RNEA：重力/惯性/科氏-离心/摩擦/外力分项与总广义力） | DYN-01/02、runtime.md §8.1"逆动力学 RNEA 归 dynamics" |
| O2 | 正动力学响应一致性检查（RobWorkSim 场景组织、积分控制、误差度量） | DYN-05 |
| O3 | 轨迹样本到动力学输入的校验（时间单调性、采样完整性、维度匹配、身份兼容） | §4.3 |
| O4 | 关节位置、速度、加速度和类型化广义力输出 | DYN-03 |
| O5 | 重力、惯性、离心/科氏、摩擦和外力分项 | DYN-01/02 |
| O6 | 机械功率与数据完整时的能量统计（正负功分项） | DYN-03、表 4 建议项 |
| O7 | 峰值（持续时间窗＋所在段）、RMS（完整循环含驻留）、轨迹段级/工况级/关节级统计 | DYN-03 |
| O8 | 多工况动力学结果与包络统计（合并产物、工况来源引用） | DYN-07 |
| O9 | 动力学输入完整性与计算诊断（DYN-\* 素材生产；码值注册表归 diagnostics） | ERR-01、diagnostics.md §4.5 |
| O10 | 物性、摩擦和负载来源标记（DYN-06 可信降级的素材面） | DYN-06、MDL-05/16 |
| O11 | 动力学证据明细（dyn Profile 逐项装配） | 表 4 动力学行、evidence.md §6.1 |
| O12 | dynamics evaluator 注册描述（评估键 `dyn-rnea-analysis`、descriptor、依赖声明） | evidence.md §9 |
| O13 | 面向 ui/execution 的领域命令适配器（**IDynamicsCommandHandler 只是领域命令适配器，不是全局命令注册表或 UI 命令权威**——命令注册权威归 ui CommandRegistry） | ui.md §7、ARCH §7.11 |
| O14 | 关节侧序列交接包契约（JointSideSeriesPack DTO——drivetrain/selection 的消费契约数据） | DYN-04、§8.2 |

**dynamics 消费**（跨单元端口，R-1/R-2 合规）：

| 来源单元 | 消费内容 | 端口/机制 |
| --- | --- | --- |
| core | 单位（Quantity/UnitToken/QuantityKind）、身份（ObjectId/ContentIdentity/TaskIdentity）、SourcedValue/ValueProvenance、Tolerance/closeWithin/allCloseWithin/runtimeAbsoluteTolerance、EvaluationMode/TaskOutcome/EngineeringStatus/TaskState、DiagnosticRecord、DomainEvent | core 公共头（L2 接口依赖） |
| runtime | 不可变 RuntimeSnapshot；CanonicalModel 只读视图（连杆质量/质心/惯量 SourcedValue、摩擦 fv/fc/bias、关节类型/限位/限速、工具 tcpOffset）；`tryDynamicWorkCell()`→DynamicWorkCellConstView（rwsim 刚体参数）；`gravityBase()`（＝R_world_baseᵀ·g_W——**唯一重力投影来源，禁止二次旋转**）；`makeState()`（每线程独立 State）；RuntimeNameMap ⑥端口；能力位（capability）——DYN-06 降级对 capability 的消费映射（runtime.md §13.2 dynamics/drivetrain 行） | runtime 公共头＋快照绑定（rwsim 基线库由 dynamics 链接——DTB §4.6、WP-17-T02） |
| trajectory | **不可变、带时间参数的轨迹**（评估键 `trj-sequence-plan` 的归档结果：TimedSample q/qd/qdd、时间轴、段结构、驻留、身份块） | 上游结果依赖（UpstreamResult 切片条目）＋注入端口 ITrajectorySourcePort（归档结果读取适配；R-1：零 trajectory 编译依赖） |
| requirements | OperatingCondition（负载/事件/驻留/过程参数/demands）、RequiredCaseSet（必验冻结）、`req.conditions` 角色键 | 快照 objectClosure 中 req-condition-set 对象＋切片条目（requirements.md §8.2） |
| drivetrain | DriveTrainMappingEvaluator（**唯一映射实现**）——dynamics 只定义关节侧序列交接契约，映射调用点归 selection/宿主编排（§8.2，D-DYN-5） | ③端口语义＋JointSideSeriesPack DTO（drivetrain 卡未产出，契约以 DYN-04/ARCH §7.10 为基线） |
| evidence | AnalysisSnapshot/InputSlice/IEvaluationContext/IEngineeringEvaluator/EvaluatorRegistry/RequiredEvidenceProfile（profileId="dyn"）/ResultEnvelope（经调用侧构造）/CaseCoverageMatrix/judgeCacheHit/ResultCurrentness | ③端口＋evidence 公共头 |
| execution | 任务提交/取消/暂停/检查点/共享 worker 装配面/RunRegistry/缓存治理 | execution 公共头（§9.1） |
| diagnostics | StableCodeRegistry（DYN-\* 注册）、IDiagnosticFactory/DiagContext、两级日志 | diagnostics 公共头（§9.4） |
| project | 只读修订查询（组装输入）；结果归档经 execution→IResultArchivePort（dynamics 不直接写盘） | project 公共头（§9.1） |
| reporting | 章节投影契约（IReportSectionProvider 注册 `dynamics-envelope` 章节）——dynamics 供数，渲染归 reporting | reporting.md §9.2（§9.6） |
| selection | 消费协议（selection 消费 dynamics 归档结果与 JointSideSeriesPack——selection 卡未产出，协议以 SEL-05/10 为基线登记） | 归档结果＋DTO（§9.3） |

**dynamics 不拥有**（逐项禁止）：

| # | 不拥有项 | 越界后果 |
| --- | --- | --- |
| N1 | RobotDesign 和 CanonicalModel 的创建或编译（归 runtime）——不重新编译模型、不构造第二套规范模型、不重复应用基座—世界变换 | SA-04/CM-0 违约 |
| N2 | FK、IK、Jacobian 和轨迹规划（归 kinematics/trajectory）——只消费不可变轨迹 | R-1/重复实现 |
| N3 | 碰撞检测和工程碰撞策略（归 policy）——dynamics 全程零碰撞（AT-19 中无 dynamics 入口） | ARC-05 违约 |
| N4 | 传动映射、耦合矩阵求逆、虚功映射、电机侧换算（归 drivetrain） | DYN-04/M-12 红线 |
| N5 | 电机、减速器和其他器件选型（归 selection） | SEL 家族越界 |
| N6 | OPT-B 或 OPT-D 优化算法（归 optimization）——不实现候选搜索、Pareto、鲁棒性或误淘汰审计 | OPT 家族越界 |
| N7 | evidence Profile、工程判定和结果当前性（归 evidence）——不自行新增可信等级枚举、不判定任务级可行 | N7 状态越权（§4.6） |
| N8 | TaskScheduler、worker 池和缓存淘汰（归 execution） | §3.3 worker 差异登记 |
| N9 | 项目目录、草稿、修订、事务和结果路径（归 project）——dynamics 不直接写项目目录 | 磁盘写入唯一归属 |
| N10 | 外部文件、CSV、JSON、目录和网格读取（归 io） | SA-14 越界 |
| N11 | 报告渲染（归 reporting）——只供数与注册章节 | RPT 家族越界 |
| N12 | 全局 UI 命令和快捷键（归 ui CommandRegistry/HotkeyBindingTable） | SA-16 越界 |
| N13 | R2 的暂停/继续控制语义与 R2 性能基准达标判定（归 execution/WP-23） | 阶段越权 |

---

## 3. 单元组成、依赖关系、目标布局和公共头文件

### 3.1 依赖形态与红线自查（ARCH §3.5 口径）

- 计算库 `sdurws_ird_dynamics`（L2，零 Qt）：接口依赖 core/evidence/runtime/execution/project/diagnostics 公共头；**业务域互链禁止（R-1）**——trajectory/requirements/drivetrain/selection 零编译期依赖（上游结果经注入端口，需求经快照对象）。
- L1 基线链接集（拟随 WP-17-T02 登记）：`sdurw_math`、`sdurw_kinematics`、`sdurw_models`、**`sdurwsim`**（DynamicWorkCell/刚体动力学——DTB §4.6 明文登记 dynamics 为消费方，WP-17-T02"链 rwsim 零 Qt 基线"）、Eigen3（RNEA 矩阵运算，参照 kinematics O-40 先例 PRIVATE 链）。**零 sdurw_proximity**（N3）。
- R-2：只消费 `include/sdurws/ird/<unit>/` 公共头；R-3：计算库零 Qt；R-4：名称经⑥端口（诊断中对象定位），零前缀拼接。

### 3.2 未来目标布局（全部为设计；落位动作归 WP-17-T02 及后续任务——**当前均不存在**）

```
industrialrobot/dynamics/
├── CMakeLists.txt                    # WP-17-T02：sdurws_ird_dynamics INTERFACE→STATIC（C++17、零 Qt）
│                                     #   PRIVATE 链 core/evidence/runtime/execution/diagnostics
│                                     #   ＋L1 基线（sdurw_math/kinematics/models + sdurwsim + Eigen3）
│                                     #   ＋注册 _test/_contract_test（T10 起）；配置期红线守卫
├── include/sdurws/ird/dynamics/      # 公共头（命名空间 sdurws::ird::dynamics）
│   ├── README.md                     # 已存在（占位）
│   ├── DynTypes.hpp                  # §4.4 数据模型（DynamicsSample/JointTorqueSample/DynamicsSeries/
│   │                                 #   DynamicsEnvelope/PowerEnergySummary/DynamicsValidity/
│   │                                 #   OperatingConditionResult/DynamicsEvidence＋JointSideSeriesPack）
│   ├── Errors.hpp                    # TrajectoryError 同款域错误 token 集（dynamics/... 前缀）
│   ├── DiagCodes.hpp                 # DYN-* CodeDescriptor 拟注册清单数据（§9.4）
│   ├── DynConfig.hpp                 # DynamicsAnalysisConfiguration＋canonical 编码（进 config.dyn 切片条目）
│   ├── InverseDynamics.hpp           # §5 RNEA 评估器（IInverseDynamicsEvaluator）
│   ├── ForwardDynamics.hpp           # §6 正动力学一致性（IForwardDynamicsValidator）
│   ├── SeriesBuilder.hpp             # §7 序列构建（IDynamicsSeriesBuilder）
│   ├── Envelope.hpp                  # §7 包络/峰值/RMS（IDynamicsEnvelopeCalculator）
│   ├── PowerEnergy.hpp               # §7 功率/能量（IPowerEnergyCalculator）
│   ├── Evidence.hpp                  # dyn Profile＋证据项装配（IDynamicsEvidenceBuilder）
│   ├── Evaluators.hpp                # dyn-rnea-analysis 评估器与工厂
│   ├── TrajectorySourcePort.hpp      # ITrajectorySourcePort（注入端口，最小接口）
│   ├── Commands.hpp                  # IDynamicsCommandHandler（领域命令适配器，零计算逻辑）
│   └── Handoff.hpp                   # JointSideSeriesPack 交接契约（drivetrain/selection 消费面）
├── src/                              # 实现 TU（RNEA 核心积分/递归实现、rwsim 适配、统计实现）
├── test/                             # WP-17-T10：单元测试（gtest）
├── contract_test/                    # WP-17-T10：跨单元契约测试（③端口/Profile/Envelope/交接包）
└── plugin/                           # WP-17-T09：dynamics 插件界面（Qt Widgets；零计算逻辑）
```

### 3.3 不创建 `sdurws_ird_dynamics_worker`（与任务指令建议布局的差异登记）

- **事实与依据**：execution.md §3.1/§3.4/D-16——全产品唯一工作进程为共享 `sdurws_ird_execution_worker`（池化 ×N），各域评估器经 `IEvaluatorFactory` 链接进其装配清单（"评估器链接清单归 L5/DTB"）；ARCH §4.1 明文列举动力学类批量计算由共享 worker 承载；DTB §5.1"产品交付路径"行明文"产品交付路径唯一＝`sdurws_ird_studio`＋`sdurws_ird_execution_worker`"；**DTB §2.18 WP-17-T02 输出列只含"计算库＋`_plugin`"，无 worker 目标**。
- **结论**：本卡不设计 `dynamics/worker/` 目录、不创建 `sdurws_ird_dynamics_worker` 目标——任务指令建议布局中"必要时的独立 worker 目标"按**不必要**处置。动力学评估（RNEA 逐样本、正动力学仿真）为纯计算，经共享 worker 的取消/检查点/崩溃隔离协议统一承接（§9.1）。
- **触发重审的条件（登记，不预设）**：若未来出现①RobWorkSim 仿真引擎要求独占进程级资源（如物理引擎全局态不可重入且量大）、②正动力学场景与 RNEA 评估的崩溃域必须隔离的实证依据，须先登记依据、所有者、协议与待裁决项并走 DTB 增量修订，方可引入独立 worker——在裁决前维持共享 worker。

### 3.4 与 runtime/trajectory/drivetrain/evidence/execution/selection 的责任矩阵（图示）

| 职责 | runtime | trajectory | dynamics | drivetrain | evidence | execution | selection |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 模型编译/快照/名称 | **拥有** | 消费 | 消费（只读） | 消费 | 消费身份 | 物化派发 | — |
| 轨迹规划/时间参数化 | — | **拥有** | **只消费不可变结果** | — | — | 派发 | — |
| RNEA/正动力学/统计 | 提供 DWC/gravityBase 视图 | 提供运动激励 | **拥有** | — | — | 派发 | — |
| 传动映射（DYN-04） | 提供 coupling/ratio 视图（R2） | — | **只交关节侧序列** | **拥有唯一实现** | — | — | 消费工作点 |
| 证据 Profile/汇总/当前性 | — | — | 供素材与证据项 | 同左 | **拥有** | 调 aggregateVerdict | — |
| 任务调度/取消/检查点/缓存 | — | — | 声明能力位 | 同左 | 判定兼容 | **拥有** | — |
| 结果归档 | — | — | 经端口 | 同左 | — | 调 IResultArchivePort | — |
| 选型校核（SEL-05） | — | — | 供关节侧输入 | 提供工作点 | — | — | **拥有** |

---

## 4. 动力学输入快照与数据模型

### 4.1 输入清单与冻结链（图示：RuntimeSnapshot → Trajectory → DynamicsSeries → DynamicsEnvelope 流程）

```
当前修订（HEAD 锚定）＋选择的工况集＋DynamicsAnalysisConfiguration
   ▼ evidence SnapshotBuilder 组装（(oid,cv) 经 IRevisionClosureSource 校验防混入；一个任务一份）
AnalysisSnapshot（不可变；objectClosure＝{robot-design, tool-definition, req-condition-set,
   drivetrain-design?（若已回填）}；policyRef/nameMapRef 承载 CON-06 身份；snapshotId）
   ▼ 依赖声明 → InputSlice（evaluationKey=dyn-rnea-analysis、entries 字典序、
        含 UpstreamResult 条目{upstreamKey="trj-sequence-plan", upstreamSliceId, upstreamRunId}）
   ▼ execution 派发（共享 worker 物化重建快照并核对身份）
   ▼ 评估器：
       ITrajectorySourcePort 读取上游轨迹运行（archived payload trj.sequence-plan.v1）
         → 兼容性校验（同 model 身份、时间参数完整）→ q/qd/qdd(t) 激励序列
       runtime 视图：DWC 刚体参数＋gravityBase()＋摩擦/物性 SourcedValue＋每线程 makeState()
         → RNEA 逐样本（§5）→ DynamicsSeries（逐工况）
       统计（§7）→ 峰值/RMS/功率/能量 → OperatingConditionResult ×N → 包络合并（§8.3）
   ▼ EvaluationOutput（证据＋payload dyn.rnea-analysis.v1）→ 通道回传 → RunRegistry 接纳
   ▼ aggregateVerdict（evidence）→ ResultEnvelope → results/<run-id>/ 归档
   ▼ ResultCurrentness：切片身份 vs 当前 HEAD 切片身份 → Current / Superseded
```

输入要素与身份归属（逐项）：

| 输入 | 来源与类型 | 身份归属 | 单位/参考系 |
| --- | --- | --- | --- |
| RuntimeSnapshot | runtime（物化或主进程构建） | 快照整体（snapshotId） | — |
| AnalysisSnapshot/InputSlice | evidence（组装协议 §4.1.5） | snapshotId ⊇ sliceId ⊇ inputBaselineId | — |
| Trajectory（不可变、带时间参数） | trajectory 归档结果（评估键 trj-sequence-plan、payload kindToken `trj.sequence-plan.v1`） | **UpstreamResult 切片条目**：upstreamSliceId＋upstreamRunId＋payload 内容摘要——轨迹时间参数化身份由此进入本域切片（轨迹重算/失效→dynamics Superseded） | q：rad/m；qd：rad/s、m/s；qdd：rad/s²、m/s²；t：s（轨迹时间轴为全文档时间基准） |
| OperatingCondition | req-condition-set 对象 | `req.conditions` Object 条目 | 负载质量 kg；驻留 s |
| LoadCondition（负载） | OperatingCondition.payloads[{toolRef, mass, com?, inertia?}] | req.conditions 条目内 | mass kg；com m（安装系）；inertia kg·m²（质心系） |
| ToolDefinition | tool 对象（CanonicalTool.tcpOffset） | `tcp` Object 条目 | tcpOffset m（法兰系） |
| TCP 与参考 Frame | TaskPoint/条件引用→CanonicalModel | 模型身份内 | 对象 ID 引用（ARC-04） |
| 质量/质心/惯量 | CanonicalLink SourcedValue 三元组（MDL-05/16 合成，来源标记 Provided/估算） | model.robot-design 条目 | kg；m（连杆系）；kg·m²（质心系、连杆坐标系姿态——MDL-05 惯量基准） |
| 重力向量 | `gravityBase()`＝R_world_baseᵀ·g_W（runtime 编译产物；g_W 默认 (0,0,−9.81) m/s²） | 模型/快照身份（MDL-22 单一变换） | m/s²；**基座系**（重力沿基座 Z 负向的投影由编译链完成，dynamics 零二次旋转） |
| 关节摩擦参数 | CanonicalJoint.friction{fv, fc, bias}（MDL-16 入口；未填写＝NotProvided→DYN-06 降级） | model.robot-design 条目 | fv：N·m·s/rad（转动）或 N·s/m（移动）；fc：N·m 或 N；bias：N·m 或 N |
| 关节限位与速度/加速度限制 | CanonicalJoint.bounds/maxVelocity/maxAcceleration（SourcedValue；未显式设值＝+inf） | model.robot-design 条目 | rad/m；rad/s、m/s；rad/s²、m/s² |
| 外部力/外部力矩 | **R1 预留**：需求未定义结构化来源（REQ-04 工况无外力字段）→ ExternalWrench 类型预留、正式评估中恒 NotApplicable（零来源不伪造）；来源结构待 P-DYN-3 | —（预留类型不入 v1 payload 必需字段） | 力 N；力矩 N·m；参考系对象 ID |
| 求解器配置 | DynamicsAnalysisConfiguration（§4.2） | `config.dyn` Configuration 条目（进 sliceId 不进 inputBaselineId） | — |
| 正动力学积分步长/容差 | config.dyn.forwardCheck | 同上 | 步长 s；容差相对 |
| 采样计划与轨迹时间参数 | 上游轨迹 TimedSample/knotTimesS＋config.dyn 输出采样抽取 | UpstreamResult＋config.dyn | s |
| EngineeringPolicySet 身份 | 快照 policyRef（CON-06）——**dynamics 不消费策略语义**（无碰撞/无阈值），策略变更不失效动力学结果（对齐 AT-05 电机成本类比） | 快照身份承载，不设依赖条目 | — |
| RuntimeNameMap 身份 | 快照 nameMapRef＋`namemap` NameMap 条目（CON-06 名称反解义务） | namemap 条目 | — |
| 算法/契约/实现版本 | evaluatorContractVersion＋algorithmVersion＋rwsim/robwork 基线锚（robworkBaselineVersion 经快照 environment） | payload 身份块 | — |
| 随机种子/线程配置 | R1 动力学为确定性数值计算，**无随机源**（seed 不设）；threadCount 进 config.dyn（进 sliceId 不进 inputBaselineId——KIN 先例同口径） | config.dyn | — |

**冻结与身份纪律**：

- 输入由完整不可变快照冻结（CON-01）；**不得混用不同项目、分支、修订、模型、轨迹和工况**——快照组装以修订闭包锚定，UpstreamResult 条目绑定的轨迹运行必须与 dynamics 任务同 revisionId（装配层校验，跨修订引用→拒绝＋DYN-INPUT-INVALID）。
- **轨迹时间参数化身份**经 UpstreamResult 条目进切片（轨迹无时间参数→拒绝正式评估，§4.3）；**物性内容身份**经 model.robot-design 条目（对象 cv 变化→sliceId 变化）；工况/工具/负载/摩擦变化均经对应对象条目使切片失效（AT-05 矩阵：TCP 变更失效下游——动力学随之 Superseded；电机成本类变更不失效）。
- 旧快照支持历史归档：结果写入登记记录指定的 `results/<run-id>/`（原修订），HEAD 前进仅用于当前性；迟到结果以完整五元组（projectId/branchId/revisionId/runId/attemptId）核对接纳（TASK-03/PM-13）。
- 显示单位切换不改变 SI 计算值与结果身份（KIN-12/§4.5）；缺失物性/估算物性/外部验证物性/精确物性以 SourcedValue 四态＋Provenance 五来源区分（§5.5），**估算物性不得自动升级为精确动力学证据**。
- **输入快照缺失身份、工况或轨迹时间参数时不得开始正式评估**（缺失清单输出→DataInsufficient 素材；Preview/Quick 同样拒绝——无激励源无评估对象）。

### 4.2 DynamicsAnalysisConfiguration（config.dyn；schemaVersion=1；canonical magic `IRDCFGDY1`）

| 字段 | 类型/单位 | 语义与约束 |
| --- | --- | --- |
| `sampleDecimation` | uint32（≥1） | 输出序列对轨迹 TimedSample 的抽取步距（1＝全采样）；进身份 |
| `forwardCheck.mode` | enum{Skip, Standard} | 正动力学一致性开关；Quick 默认 Skip（筛选语义）、Verified 默认 Standard；Skip 时建议证据项 `dyn.forward-dynamics-consistency`＝NotRun（非缺失阻断——建议项语义） |
| `forwardCheck.integratorToken` | string（封闭词表；**WP-17-T05 落位锁定值＝`rk4-fixed`**——同源 RNEA＋固定步长 RK4，见 §6.1 登记注与 P-DYN-7 行；扩展新积分器先本表增量修订词表再追加常量，既有值不重排） | 积分器标识（进身份） |
| `forwardCheck.maxStepS` | double，s，>0 | 最大积分步长（DYN-05"步长明确"） |
| `forwardCheck.relTolerance` | double，>0 | 积分相对容差（DYN-05"容差明确"；进身份） |
| `forwardCheck.toleranceQ` | double，rad/m，>0 | 一致性判定的位置误差阈值（**分析配置来源**——附录 D C7 允许配置来源；无需求侧默认，WP-17-T05 黄金算例测试对照参考值 1e-9 rad——测试对照容差与产品容差分离，黄金文件另声明） |
| `forwardCheck.toleranceQd` | double，rad/s、m/s，>0 | 速度误差阈值（同上） |
| `threadCount` | uint32（0=自动） | 工况级并行度（进 sliceId；不改逐工况数值结果——§5.6 确定性口径） |
| `powerIntegral` | enum{Trapezoidal}（封闭词表，当前唯一值） | 功率/能量积分格式（§7.5）；扩展走本卡修订 |

- 身份路径：canonical 编码→`configDigest`＝SHA-256→切片 `config.dyn` 条目→**进 sliceId、不进 inputBaselineId**（求解配置改变＝重算，但不改变"样本基准"类比较基准——D-04 同口径）。
- **复检/阈值零副本**：本配置不含任何碰撞、间距或工程判定阈值（dynamics 无 policy 消费面）。

### 4.3 轨迹消费契约（R-1 合规的上游结果绑定）

1. **读取**：评估器经注入端口 `ITrajectorySourcePort::loadTrajectoryRun(upstreamRunId)`（§10.8）取得上游轨迹 payload canonical 字节＋身份块；端口实现由装配面适配（读 results/<run-id>/ 归档，经 project 只读侧/execution 通道）——本域零 trajectory 库链接、零私有头。
2. **兼容性校验（评估起点，全部通过才继续）**：
   - 身份链：轨迹 payload 的 snapshotId 所属修订＝本任务修订（装配层已校验，评估器二次防御）；
   - 模型一致：轨迹 payload 绑定的 model.robot-design cv＝本快照闭包内 cv（不一致→DYN-UPSTREAM-TRAJECTORY-INCOMPATIBLE，拒绝）；
   - 时间参数完整：TimeParameterization 存在、totalDurationS>0、TimedSample 非空且时间单调非降（违反→DYN-TIME-PARAM-MISSING / DYN-SERIES-NON-MONOTONIC，拒绝正式评估）；
   - 轨迹上游当前性：归档轨迹结果为 Superseded 时**允许评估但结果标注**（当前性由各自结果独立判定；正式链推荐上游 Current——ui 提交面提示，不阻断，登记为呈现约定）。
3. **激励映射**：q/qd/qdd 逐样本直接采用（dynamics 不重算轨迹运动学、不平滑、不插值外推——缺样本按 §4.6 处理，不伪造）；轨迹段结构（segmentIndex）与驻留段随样本携带（峰值定位/段级统计/DYN-08 回放的定位键）；夹取/释放事件按条件事件时间线映射为模型变体边界（§5.4）。
4. **与 trajectory 卡的衔接**：trajectory.md §19.2 明文"轨迹 TimedSample/分段时长作为 DYN-03 峰值/RMS 的输入面——消费 payload 或归档结果，R-1 经端口"——本节即该行的 dynamics 侧承接。

### 4.4 动力学数据模型（8 个必需类型＋1 个交接 DTO；签名＝设计基线，实现任务可按 DTB §5.4 微调并登记）

```cpp
namespace sdurws::ird::dynamics {

/// 关节类型（随 CanonicalJoint；决定广义力量纲——类型化 DYN-03）。
enum class DynJointType { Revolute, Prismatic, Continuous };

/// 数值状态（样本级；封闭词表——不是全局任务状态，不是证据等级）。
enum class SampleNumericState { Ok, NonFiniteInput, NonFiniteOutput, Overflow };

/// 单样本动力学记录（§5 RNEA 逐样本输出；值语义、构造后不修改）。
struct DynamicsSample {
    double t;                        // 样本时间，单位 s（上游轨迹时间轴；单调非降）
    std::uint32_t segmentIndex;      // 所在轨迹段序号（0 基；来自上游轨迹段结构）
    core::ObjectId conditionId;      // 工况对象 ID（EVI-02 覆盖矩阵关联键）
    std::uint32_t jointIndex;        // 关节序号（0 基，链序）
    core::ObjectId jointObjectId;    // 关节稳定对象 ID（ARC-04）
    DynJointType jointType;          // 关节类型——决定下列力字段的量纲（Revolute/Continuous→N·m；Prismatic→N）
    double q;                        // 关节位置：rad（转动/连续）或 m（移动）——SI 真值
    double qd;                       // 关节速度：rad/s 或 m/s
    double qdd;                      // 关节加速度：rad/s² 或 m/s²
    double tauGravity;               // 重力项广义力（N·m 或 N，按 jointType）
    double tauInertia;               // 惯性项（含角加速度/线加速度项）
    double tauCoriolisCentrifugal;   // 科氏/离心项（含交叉耦合项——逐样本精确计算，不对角化）
    double tauFriction;              // 摩擦项（黏性＋库仑＋偏置；§5.5 符号约定）
    double tauExternal;              // 外力项（R1 恒 0 且标记 NotApplicable——P-DYN-3）
    double tauTotal;                 // 总广义力（=五分项之和；恒等式由黄金算例校验）
    double mechanicalPower;          // 机械功率 P=τ_total·q̇，单位 W（§7.5 符号约定）
    double energyIntegralJ;          // 能量积分状态 E(t)=∫₀ᵗ P dτ，单位 J（净能量；时间加权）
    std::uint32_t payloadVariantIndex; // 负载模型变体索引（§5.4 事件时间线；0=基线工具）
    core::ObjectId toolObjectId;     // 工具对象 ID（变体所属工具/负载引用）
    // 参考系说明：广义力为关节空间标量（沿关节轴），无坐标系分量；重力投影经基座系（§4.1）。
    SampleNumericState numericState; // 数值状态（非 Ok 时诊断必附——NFR-COR-03）
};

/// 可信性/完整性摘要（值对象；**不是**新的全局任务状态、证据等级或工程判定——只作结果侧事实记录）。
struct DynamicsValidity {
    enum class Completeness { Complete, Partial, Empty } completeness;
    std::size_t plannedSampleCount;  // 计划样本数（轨迹采样×抽取）
    std::size_t actualSampleCount;   // 实际样本数（缺样本/间隙不计入 actual）
    std::size_t nonFiniteCount;      // 非有限值样本数
    std::size_t estimatedLinkCount;  // 估算物性连杆数（Provenance=GeometricEstimate）
    std::size_t estimatedPayloadCount; // 估算物性负载数（com/inertia 缺失按 §5.5 保守估算）
    bool frictionMissing;            // 存在摩擦参数缺失（MDL-16/DYN-06：DataInsufficient 降级素材）
    bool externalValidationPending;  // 外部验证未完成（CON-03 Recorded 态物性）
    enum class ForwardCheckState { NotRun, Passed, Failed, NotApplicable } forwardCheck;
    // 约束：本类型只承载"结果完整性＋数值质量＋来源信息"；Feasible/EngineeringInfeasible/
    // DataInsufficient 判定一律归 evidence aggregateVerdict（§4.6）。
};

/// 单工况动力学序列（逐工况产出；身份块完整）。
struct DynamicsSeries {
    // —— 身份块（构造期一次冻结；全部入 series 内容身份编码）——
    core::ContentIdentity snapshotId;          // 绑定快照
    core::ContentIdentity sliceId;             // 绑定切片（当前性判据）
    core::ContentIdentity trajectoryPayloadId; // 上游轨迹 payload 内容身份（Trajectory 内容身份——不等于轨迹 sliceId，
                                               //   轨迹 sliceId 另存于 upstreamRef 供失效比对）
    core::ObjectId conditionId;                // 工况对象 ID
    core::ObjectId toolObjectId;               // 工具对象 ID
    std::uint32_t evaluatorContractVersion;    // 本域契约版本
    std::string   algorithmVersion;            // RNEA/统计实现版本 token
    std::string   dynConfigDigest;             // config.dyn 摘要
    core::TaskIdentity task;                   // 运行身份五元组
    // —— 内容块 ——
    std::vector<DynamicsSample> samples;       // 按 (conditionId, t, jointIndex) 排序（§4.6 排序规则）
    DynamicsValidity validity;                 // 完整性/数值质量/来源摘要
    std::vector<core::ObjectId> diagRefs;      // 诊断引用（对象级；条目本体随 EvaluationOutput.diagnostics）
    core::ContentIdentity contentIdentity;     // series canonical 内容身份（SHA-256）
};

/// 峰值记录（DYN-03：峰值必须报告持续时间窗和所在轨迹段）。
struct PeakRecord {
    double value;                    // 峰值（SI：N·m/N/rad·s⁻¹/W 等，按量纲行说明）
    double tPeakS;                   // 峰值发生时间，s
    std::uint32_t segmentIndex;      // 峰值所在轨迹段
    double windowStartS;             // 持续时间窗起点（峰值样本连续覆盖区间，§7.2——样本级精确，不引入比例阈值）
    double windowEndS;               // 持续时间窗终点，s
    core::ObjectId conditionId;      // 来源工况（包络跨工况时必填）
};

/// 多工况包络（DYN-07 合并产物；统计呈现，不替代逐工况、不自动形成工程判定）。
struct DynamicsEnvelope {
    // 逐关节 × 逐量纲的最大包络（正向/反向分列——方向相关）：
    struct JointEnvelope {
        std::uint32_t jointIndex;
        core::ObjectId jointObjectId;
        PeakRecord tauMaxPositive;   // 正向力矩/力峰值包络（跨工况取 max）
        PeakRecord tauMaxNegative;   // 反向（|min|）
        PeakRecord velocityPeak;     // 速度峰值（rad/s 或 m/s）
        PeakRecord accelerationPeak; // 加速度峰值
        PeakRecord powerPeak;        // 机械功率峰值（W，含正负——正向 max(P)、反向 max(−P)）
        double rmsTau;               // 完整任务循环力矩 RMS（含驻留；时间加权，§7.3）
        std::vector<core::ObjectId> contributingConditions; // 包络来源工况集（稳定排序）
        bool envelopeComplete;       // 任一来源工况不完整→false（§7.6：不输出看似完整的包络）
    };
    std::vector<JointEnvelope> joints;
    std::size_t conditionCount;      // 参与合并的工况数
    bool coversAllMandatory;         // 是否覆盖全部启用必验工况（呈现参考；覆盖判定归 evidence 覆盖矩阵）
    core::ContentIdentity contentIdentity;
};

/// 功率与能量摘要（建议证据项 dyn.power-energy-split 的数据面）。
struct PowerEnergySummary {
    struct JointPowerEnergy {
        std::uint32_t jointIndex;
        double positiveEnergyJ;      // E⁺=∫max(P,0)dt，J（驱动/提升/加速——正功）
        double negativeEnergyJ;      // E⁻=∫min(P,0)dt，J（≤0；制动/下降/发电——负功）
        double netEnergyJ;           // E_net=E⁺+E⁻，J
        double meanPowerW;           // 平均功率 E_net/T_cycle，W
        PeakRecord powerPeak;        // 功率峰值（含窗与段）
    };
    std::vector<JointPowerEnergy> joints;
    double cycleDurationS;           // 完整任务循环时长（含驻留），s；缺失→NotProvided 语义（不伪造）
    bool includesDwell;              // 驻留是否计入（Verified 必须 true——§7.3）
    bool timeParamAvailable;         // 时间参数可用性（false 时能量/平均功率字段无效且显式标记）
};

/// 单工况结果（payload 内聚合形态）。
struct OperatingConditionResult {
    core::ObjectId conditionId;      // 工况 ID
    std::vector<core::ObjectId> caseScope;  // 覆盖的 case 集（通常单工况自身；EVI-02 关联）
    DynamicsSeries series;           // 序列（身份块见上）
    std::vector<PeakRecord> peaks;   // 工况级峰值（逐关节逐量）
    PowerEnergySummary powerEnergy;  // 功率/能量摘要
    DynamicsValidity validity;       // 工况级完整性
    std::vector<core::DiagnosticRecord> diagnostics; // 工况级诊断（失败定位到工况/段/样本）
};

/// 证据装配视图（评估器出口 EvaluationOutput 的域内组织形态）。
struct DynamicsEvidence {
    // dyn Profile 证据项逐项装配（§8.4：itemId/status/artifactDigest/caseScope/subject/
    // notApplicableReason/invalidReason）；本体为 evidence EvidenceItem——此结构仅组织域内素材。
    std::vector<core::ObjectId> seriesRefs;   // 关节侧广义力序列引用（必需项 ①）
    std::vector<core::ObjectId> statsRefs;    // 峰值/RMS 统计引用（必需项 ②）
    std::vector<core::ObjectId> provenanceRefs; // 物性/摩擦来源标记引用（必需项 ③）
    std::vector<core::ObjectId> loadConditionRefs; // 负载工况标识引用（必需项 ④）
    bool powerEnergyAvailable;                // 建议项：功率/能量分项
    bool forwardCheckAvailable;               // 建议项：正动力学一致性检查记录
};

/// 关节侧序列交接包（drivetrain/selection 消费契约；§8.2——DYN-04 交接面）。
struct JointSideSeriesPack {
    core::ContentIdentity sourceSeriesId;      // 来源 DynamicsSeries 内容身份
    core::ContentIdentity trajectoryPayloadId; // 激励轨迹内容身份
    core::ContentIdentity conditionContentId;  // 工况内容身份
    std::string algorithmVersion;              // dynamics 实现版本
    // 逐关节序列（与 DynamicsSample 同源抽取；drivetrain 映射的输入）：
    struct JointSeries {
        std::uint32_t jointIndex;
        core::ObjectId jointObjectId;
        DynJointType jointType;                // 转动→τ 单位 N·m；移动→F 单位 N
        std::vector<double> t;                 // s
        std::vector<double> q, qd, qdd;        // rad·m / rad·s⁻¹·m·s⁻¹ / rad·s⁻²·m·s⁻²
        std::vector<double> generalizedForce;  // N·m 或 N（按 jointType）
        std::vector<double> payloadVariant;    // 负载变体索引（随事件时间线）
    };
    std::vector<JointSeries> joints;
    // 传动输入引用（drivetrain 映射所需、dynamics 不解释）：
    core::ObjectId driveTrainDesignRef;        // 传动配置对象引用（若已回填；R1 可为空——映射 Skip）
    std::optional<core::ContentIdentity> couplingMatrixContentId; // R2（MDL-21）矩阵内容身份引用位——
                                               //   R1 恒 nullopt 且零消费代码（§8.2.4）
};
} // namespace
```

字段义务对照（任务指令"每个数据类型必须说明"清单）：**版本**＝evaluatorContractVersion/algorithmVersion/configDigest；**身份**＝ObjectId（对象级）＋TaskIdentity（运行级）；**内容身份**＝series/envelope 的 canonical SHA-256；**引用关系**＝diagRefs/seriesRefs/upstreamRef/driveTrainDesignRef；**所有权**＝全部值语义、深拷贝安全、构造后不可变；**生命周期**＝评估期内构建→payload canonical 化→归档持有；**线程约束**＝并发只读安全、构建期单线程（每工况一构建器）；**坐标系**＝关节空间标量（沿轴）＋基座系重力投影；**单位**＝逐字段注明（§4.5 表）；**错误语义**＝SampleNumericState＋DYN-\* 码；**取消/失败**＝§4.6/§9.1；**R1/R2**＝payload v1 全 R1，couplingMatrixContentId 为 R2 引用位（零消费代码）。

### 4.5 关节型广义力和单位表（图示；全卡唯一量纲权威）

| 物理量 | 转动/连续关节 | 移动关节 | 参考系/说明 |
| --- | --- | --- | --- |
| 关节角/位移 q | rad | m | 权威角（q_authoritative＝q_zeroOffset＋q_rw，runtime 口径） |
| 关节速度 q̇ | rad/s | m/s | core LinearVelocity/AngularVelocity 对应量纲 |
| 关节加速度 q̈ | rad/s² | m/s² | 同上（acceleration 量纲） |
| 广义力 τ（重力/惯性/科氏/摩擦/外力/总） | N·m | N | 关节轴标量；core Torque/Force 强类型在 API 边界强制（二者不可互赋——core.md DYN-03 承载），存储用 SI double＋jointType 标签（性能：10⁵ 样本级） |
| 质量 | kg（不分关节类型） | kg | 连杆系/负载 |
| 惯量张量 | kg·m² | kg·m² | 质心、连杆坐标系姿态（MDL-05 基准） |
| 重力/线加速度 | m/s² | m/s² | 基座系（gravityBase 投影产物） |
| 外力/力矩 | N·m（力矩）/N（力） | 同左 | 参考系＝ExternalWrench.refFrame（R1 预留） |
| 机械功率 | W | W | P=τ·q̇（关节标量） |
| 能量 | J | J | **core UnitToken R1 表无 J token**（Power 有 W、无 Energy 量纲）——能量字段以 SI 真值 double＋本表注释承载，显示/导出换算归消费方；强类型化若需补齐走 core 卡增量修订（P-DYN-9，低风险） |
| 时间 | s | s | 上游轨迹时间轴（唯一时间基准） |

显示单位是纯投影（KIN-12）：切换不改 SI 真值、不产生修订、不触发重算、不进任何身份。

### 4.6 序列纪律与边界情形（用户清单逐项承接）

| 情形 | 处理 |
| --- | --- |
| 形态区分 | 离散轨迹（上游输入）→ 动力学时间序列（DynamicsSeries）→ 峰值/统计（PeakRecord/OperatingConditionResult）→ 最终包络（DynamicsEnvelope）→ 分批结果（通道 ResultBatch DTO，非 envelope）→ 最终结果（Completed envelope payload）——六形态不可混用 |
| 样本排序 | samples 按 (conditionId, t 升序, jointIndex 升序) 稳定排序；同刻度逐关节行序固定（NFR-COR-02 稳定排序） |
| 时间重复/倒退/零间隔 | 上游校验（§4.3）：时间非单调非降→拒绝正式评估（DYN-SERIES-NON-MONOTONIC）；评估器不排序修复、不插值抹平 |
| 缺样本/采样间隙 | plannedSampleCount−actualSampleCount＝缺口；validity.completeness=Partial＋DYN-SAMPLE-GAP 素材（附首个缺口 t 与段）；**不插值补齐**；Partial 不阻断该工况其余统计（逐段标注覆盖区间） |
| 非有限数 | 输入非有限→该样本 NonFiniteInput（不静默转 0——NFR-COR-03）；输出非有限/溢出→NonFiniteOutput/Overflow＋该样本后本工况统计标注非完备（序列保留至故障点，不截断伪造） |
| 部分结果 | 未完成工况的序列/统计仅入通道批次与诊断账户；**不进入 Completed envelope**；不作为正式缓存命中（CON-04） |
| 取消 | 评估器在工况/批次边界轮询取消→cancelled 语义→Canceled envelope（NotApplicable）；正常取消零错误诊断（UX-03） |
| worker 崩溃 | NFR-REL-02：仅当前任务失败；最近检查点保留（§9.1.4） |
| 超时 | evaluationTimeout（nullopt=不启用，R1 默认）＋取消协议 2 s/10 s（execution 承载） |
| 检查点恢复 | 工况级检查点（§9.1.4）：已完成工况结果不重算；恢复后仍走完整接纳 |
| 历史结果不可变 | 归档后只增不改（PA-2）；重算＝新 RunId |
| 合法/非法 envelope 组合 | Completed×{Feasible, EngineeringInfeasible, DataInsufficient}；Canceled/Failed/Interrupted×NotApplicable（payload 空、无 Satisfied 声明）；任意×Preview 拒绝（evidence §7.1 表 3 原样承接——本卡不新增组合） |
| 取消/失败/数据不足/不完整 | 一律不进正式可行集、不支撑正式通过结论（TASK-02/表 1） |
| 无时间参数 | 不伪造节拍、功率积分或能量：PowerEnergySummary.timeParamAvailable=false 时 energy/meanPower 字段显式无效并列入缺失清单（汇总④级） |

---

## 5. 逆动力学

### 5.1 算法定位与模型来源

- **DYN-01 原文口径**："基于 RobWorkSim 刚体模型和递归牛顿—欧拉计算逆动力学"——RNEA 为本域自实现算法（递归牛顿—欧拉，逐关节递推），其**输入参数取自 RobWorkSim 刚体模型**：经 `DynamicWorkCellConstView`（runtime 视图）读取各刚体质量/质心/惯量/轴几何，经 CanonicalModel 只读视图读取关节类型/限位/摩擦/工具偏置——**不重编译模型、不自建第二套关节链**（CM-0）。
- 关节角权威换算 `q_authoritative = q_zeroOffset + q_rw`（runtime 口径）：本域全链使用权威角；与 rwsim 引擎/状态交互的换算在适配层单点执行。
- 每线程独立 `makeState()`；评估全程不修改共享 RuntimeSnapshot（runtime §5.7 线程纪律）。

### 5.2 逆动力学分项图（图示）

```
                     RuntimeSnapshot（不可变）
        ┌──────────────┬──────────────────┬───────────────────┐
        ▼              ▼                  ▼                   ▼
  DWC 刚体参数    CanonicalModel      gravityBase()        Trajectory（上游）
  （质量/质心/   （关节类型/摩擦      （＝R_world_baseᵀ·g_W   q(t)/q̇(t)/q̈(t)
   惯量/轴几何)    fv/fc/bias/限速）    基座系重力投影）      ＋段结构＋事件时间线
        └──────────────┴──────────────────┴─────────┬─────────┘
                                                     ▼
  逐样本（t, q, q̇, q̈, 负载变体 k）                    │
        │                                            │
        ▼                                            ▼
  ┌─────────────── RNEA（递归牛顿—欧拉，本域实现）───────────────┐
  │ 外向递推（i=1→n）：ω_i, α_i, a_ci, F_i, N_i                  │
  │   重力折叠：基座加速度 = −g_base（gravityBase 投影，应用一次）  │
  │   负载变体 k：末端刚体参数随夹取/释放事件切换（§5.4）            │
  │ 内向递推（i=n→1）：f_i, n_i                                   │
  │   τ_i = { n_i·z_i（转动/连续） | f_i·z_i（移动） }             │
  │ 摩擦叠加：τ_fric = fv·q̇ + fc·sgn₀(q̇) + bias（§5.5 约定）      │
  │ 外力项：τ_ext（R1 恒 0，NotApplicable——P-DYN-3）              │
  └──────────────────────────────┬───────────────────────────────┘
                                 ▼
   τ_total = τ_gravity + τ_inertia + τ_coriolis + τ_friction + τ_external
   （恒等式逐样本成立——黄金算例校验分项可加性）
                                 ▼
        DynamicsSample ×(样本×关节) → DynamicsSeries（逐工况）
```

### 5.3 输入覆盖清单（任务指令逐项）

| 输入 | 来源/处理 | 边界 |
| --- | --- | --- |
| 关节位置/速度/加速度 | 上游轨迹 TimedSample（§4.3，不重算） | 时间单调校验前置 |
| 连杆质量/质心/惯量张量 | DWC/CanonicalModel SourcedValue | 缺失→§5.5 降级；非法（非有限/SPD 失败）→输入非法（建模侧断言前置，dynamics 防御性复检——张量对称性相对 1×10⁻¹² 复检可执行，附录 D 第 6 项） |
| 世界系/基座系重力 | `gravityBase()`（基座系投影，编译链唯一） | **禁止再次旋转**（AT-37/RT-BW-4 拦截同类缺陷）；倒挂/壁装由安装姿态编译生效 |
| runtime 编译的基座—世界变换 | 经 gravityBase 间接消费（本域不直接读 R_world_base 做额外变换） | 同上 |
| 末端工具和负载 | CanonicalTool.tcpOffset＋条件 payloads（变体切换 §5.4） | 负载物性缺失→§5.5 |
| 外部力和外部力矩 | R1 预留 ExternalWrench（恒 NotApplicable，P-DYN-3） | 不伪造零以外的值 |
| 黏性摩擦 | fv·q̇ | fv 缺失（NotProvided）→该项按 0 计并标记估算降级？**否**——fv 缺失→DYN-06 降级＋缺失清单（MDL-16"未填写标记 DataInsufficient"），分项值仍按 0 计入但样本/序列标记 frictionMissing（数值继续、证据降级——两个层面分开：计算继续、证据不包装精确） |
| 库仑摩擦/偏置 | fc·sgn₀(q̇)＋bias | 同上 |
| 关节类型 | CanonicalJoint.type | Continuous（工程工作范围内）按转动处理 |
| 限位与速度/加速度限制的输入关联 | bounds/maxVelocity/maxAcceleration 只作**参考输出**（限值校验非动力学义务——限速校验归 TRJ-05 上游） | dynamics 不做限值判定（零 policy/零阈值副本）；序列携带 q/q̇/q̈ 供下游对照 |

### 5.4 负载事件时间线与模型变体

- 条件 events（Grasp/Release/Dwell{stationRef, durationS}）的时间定位：事件绑定 stationRef（任务点）→经上游轨迹的段结构/任务点定位映射到轨迹时刻 t_event（任务点样本时刻）；Dwell 即轨迹驻留段（零速区间，直接采用上游驻留语义，不重算）。
- 变体规则：初始变体＝条件 payloads 基线（工具＋初始负载）；Grasp 事件后该负载并入末端（质量/质心/惯量合成到法兰/TCP 刚体——合成遵循 MDL-05/16 来源标记）；Release 后移除。变体索引随样本记录（payloadVariantIndex）。
- **事件落在采样间隙**：变体边界取事件时刻所在区间的下一样本起生效（保守：载荷提前挂载不晚于其后首样本）；边界行为入黄金算例（V-08 行）。
- 负载物性缺失（com/inertia optional）的保守估算规则（D-DYN-6）：com 缺失→取安装点（TCP）；惯量缺失→点质量模型（零转动惯量）——**必须**标记 estimated（Provenance=GeometricEstimate 语义）＋DYN-PROPERTY-DOWNGRADED 素材＋"低估惯性"提示；**不包装为精确结论**（DYN-06）。

### 5.5 摩擦符号约定与数据不足降级（DYN-02/06、MDL-16 M-8 闭环）

- 摩擦模型：`τ_fric = fv·q̇ + fc·sgn₀(q̇) + bias`；**sgn₀(0)＝0**——零速（含驻留）时库仑项取 0：静摩擦/起动摩擦不在 R1 模型内（如实登记为模型边界，黄金算例覆盖 q̇=0 行为），不引入 sgn 平滑阈值（避免自设数值）。
- 来源与降级三层（互斥呈现、不混用）：
  1. **缺失**（NotProvided：摩擦三参数任一未填写，MDL-16）→ `frictionMissing=true`＋缺失清单→汇总④级 DataInsufficient 素材（MDL-16 原文"未填写标记 DataInsufficient 并触发 DYN-06 降级"）；
  2. **估算**（GeometricEstimate：MDL-05 几何估算物性、§5.4 负载保守估算）→ estimated 计数＋限定语 estimated（不包装精确）；
  3. **外部验证未完成**（CON-03 Recorded 态引用资源）→ externalValidationPending＋报告限定语 external-validation-incomplete。
- **可信性不替代工程判定**：DynamicsValidity 只是结果侧事实；Feasible/DataInsufficient 判定归 evidence 汇总（表 4：来源标记缺失按 DYN-06 降级并列入缺失清单）。

### 5.6 数值稳定性、失败语义与耦合链防御（图示之外的规则行）

| 情形 | 处理 | 语义归类 |
| --- | --- | --- |
| 非有限输入（q/q̇/q̈/物性） | 样本 NonFiniteInput，本工况统计标注非完备 | 输入数据问题（④级素材），不升级任务级不可行 |
| 输入维度不匹配（轨迹关节数≠模型关节数） | 评估终止＋DYN-DIMENSION-MISMATCH | 输入非法（①级素材） |
| 轨迹时间不单调 | 评估终止＋DYN-SERIES-NON-MONOTONIC | 输入非法（§4.3） |
| RNEA 计算失败（数值异常） | 该样本起本工况失败记录（DYN-RNEA-FAILED，附 t/段/关节）；**单工况失败不自动推出其他工况失败**——其余工况继续 | 工况级失败 |
| 单个样本超限/局部数值异常 | 样本级标记＋warning 素材；不阻断序列（统计标注） | 局部异常 |
| 计算诊断定位 | 全部诊断绑定：快照身份＋工况 ID＋轨迹段序号＋样本 t＋关节对象 ID＋算法版本（ERR-01 字段齐备） | — |
| **耦合链防御（R1）** | dynamics 零耦合矩阵代码；R1 由 modeling/runtime 门禁阻断 mimic/闭环/线性耦合链（MDL-21/§2.1 矩阵），dynamics 消费的快照必然已过门禁。防御性二次校验：若快照模型携带 R2 才会出现的耦合结构标识而 MDL-21 未启用（跨版本快照），dynamics 拒绝评估＋DYN-COUPLING-GATE-REJECTED——**绝不静默按独立关节链计算、绝不对角化/准静态替代** | 门禁防御 |

- **关节侧结果与候选电机/减速器无关**（DYN-04）：RNEA 输出只依赖模型＋轨迹＋工况＋配置；传动配置变化（drivetrain-design 对象回填）**不失效**关节侧序列（该对象不在本域依赖声明内——映射失效面归 selection/drivetrain 链）。
- 结果绑定：每个 DynamicsSeries 携带轨迹/工况/模型快照/切片四重身份（§4.4 身份块）。

---

## 6. 正动力学一致性（DYN-05）

### 6.1 正动力学与逆动力学一致性时序图（图示）

```
参考轨迹段 [t₀,t₁]（上游 Trajectory）
   │ ①取参考段初始状态 (q₀=q(t₀), q̇₀=q̇(t₀))
   ▼
逆动力学（§5）对参考段逐样本求 τ_ref(t)          ──同一冻结模型/重力/工具/负载/工况──┐
   │                                                                              │
   ▼                                                                              │
②正动力学场景（RobWorkSim）：                                                     │
   控制输入＝τ_ref(t)（采样保持）；初始状态＝(q₀,q̇₀)；                              │
   积分器/最大步长/相对容差＝config.dyn.forwardCheck；                              │
   仿真时长＝t₁−t₀；输出采样计划＝sampleDecimation；                               │
   独立 State（makeState）——不触碰共享快照                                         │
   │                                                                              │
   ▼                                                                              │
③积分回放：q_sim(t), q̇_sim(t)                                                    │
   │                                                                              │
   ▼                                                                              │
④误差度量（与参考轨迹状态逐样本比较，逐元素）：                                    │
   e_q(t)=q_sim−q_ref；e_qd(t)=q̇_sim−q̇_ref                                        │
   max|e_q|、RMS(e_q)、max|e_qd|、RMS(e_qd)、各最大误差发生时间                      │
   │                                                                              │
   ├─ max|e_q|≤toleranceQ ∧ max|e_qd|≤toleranceQd ──► forwardCheck=Passed        │
   ├─ 超阈值 ──► Failed＋DYN-FD-CONSISTENCY-FAILED（比较型：实际/期望/单位）        │
   ├─ 积分发散/非有限 ──► Failed＋DYN-FD-DIVERGED（异常检测——DYN-05 本义）          │
   └─ 数值异常（刚性/溢出）──► Failed＋DYN-FD-NUMERIC-ANOMALY                      │
                                                                                  │
   ⑤同一模型/重力/负载/工况（两路共用同一冻结快照与事件时间线）◄────────────────────┘
```

> **正动力学引擎选型登记（P-DYN-7 落位锁定，2026-10-08，WP-17-T05）**：上图②的"正动力学场景（RobWorkSim）"落位形态＝**同源 RNEA 质量阵/偏置力提取＋固定步长 RK4 积分**（integratorToken 词表锁定值 `rk4-fixed`——§4.2 回填）。时序图语义逐项不变：①τ_ref 由检查器经公共逆动力学评估器对参考段产出（同源）；②控制输入采样保持、初始状态 (q₀,q̇₀)＝参考段起点、步长/容差经 config.dyn.forwardCheck 显式入配置与身份、输出采样计划＝参考样本时刻轴；③积分回放 q_sim/q̇_sim（独立局部仿真状态——CanonicalModel 为 const 只读引用、零共享 State 句柄，§6.3.6"不触碰共享 RuntimeSnapshot"在本接口面结构成立）；④误差度量与四态判定逐项实现；⑤共源强制经"τ_ref 与 M/h 提取共用同一 evaluate 入口＋区间起点时刻调用"结构性成立（变体切换只发生在采样保持区间边界）。RobWorkSim 物理引擎（rwsim::simulator::PhysicsEngine 工厂形态）不落位的四条实测依据（§1.2 T05 登记注⑤）：ODE 依赖未启用（构建缓存 ODE_DIR-NOTFOUND）；Bullet 引擎为动态插件机制、L2 计算库无合法接入点；负载注入引擎场景＝自建第二套关节链（CM-0）；迭代求解器漂移与 §10.2 确定性行及冒烟零外联纪律冲突。§10.2 R1/R2 行"rwsim 引擎类选型为 T05 落位验证项"自此消账（P-DYN-7 状态更新见 §15.2）；后续批次若引入 rwsim 引擎，须先本卡增量修订＋契约测试词表（ForwardDynamicsBoundaryContractTest）同步放行。

### 6.2 输入与前置（任务指令清单）

| 要素 | 设计 |
| --- | --- |
| 给定力矩/控制输入 | τ_ref(t)＝参考段逆动力学结果（§5 产出；采样保持到积分步内） |
| 初始位置/速度/初始状态 | 取参考段起点 (q(t₀), q̇(t₀))；缺失（轨迹无该状态）→检查 NotRun＋DYN-FD-INITIAL-STATE-MISSING 素材（**建议证据项缺失不阻断**——表 4 建议项语义） |
| 重力/负载 | 与逆动力学同源（同一冻结快照、同一事件时间线变体——⑤共源强制） |
| 积分器/步长/容差/时长/输出采样 | config.dyn.forwardCheck（§4.2；进身份） |
| 轨迹回放方式 | 状态比较对象＝上游轨迹参考段逐采样 (q_ref, q̇_ref)；不重放 TCP/笛卡尔量 |
| 比较对象与误差计算 | §6.1 ④；逐元素（core::allCloseWithin 同款口径） |
| 最大/RMS 误差与发生时间 | 随检查记录输出（payload 扩展块＋建议证据项） |

### 6.3 必须明确（任务指令逐条）

1. **正动力学一致性是验证证据，不等同于工程通过**——Passed 只进建议证据项 `dyn.forward-dynamics-consistency`（Satisfied），不改变汇总判定路径。
2. 正/逆动力学**共用同一冻结模型、重力、工具、负载和工况**（⑤）；基座—世界变换只消费 runtime 编译结果，dynamics 不再次旋转。
3. 比较容差＝config.dyn.forwardCheck.toleranceQ/toleranceQd（**分析配置来源**——附录 D C7"必须有默认值或配置来源"；产品运行校验不依赖黄金数据集；黄金算例另声明测试对照容差——两类容差分开，C7 口径）。
4. 初始状态/时间参数缺失→数据不足/不适用既有语义（NotRun＋素材；不伪造 Passed）；不适用条件＝forwardCheck.mode=Skip（显式 NotRun，非缺失）。
5. **正动力学失败不能直接判定模型无效**——Failed 只产生建议证据项 Invalid＋warning 诊断（含积分发散/异常检测记录）；模型有效性归建模/编译链。
6. 仿真场景**不得修改共享 RuntimeSnapshot**；每次评估使用独立、线程安全的仿真状态（makeState 每线程；rwsim 场景对象评估器内私有构造）。

---

## 7. 峰值、RMS、包络、功率与能量

### 7.1 统计范围与层级

| 层级 | 范围 | 产物 |
| --- | --- | --- |
| 样本级 | 逐样本分项/总力矩/功率 | DynamicsSample |
| 轨迹段级 | 段内峰值/极值 | 段定位（segmentIndex 附于 PeakRecord） |
| 工况级 | 单工况全循环（含驻留） | OperatingConditionResult.peaks＋PowerEnergySummary |
| 关节级 | 逐关节跨量统计 | 各结构 JointXxx 行 |
| 完整任务循环级 | 多工况完整循环 | RMS（逐工况/逐关节）、EVI-02 覆盖下的包络 |

### 7.2 峰值与持续时间窗（DYN-03 硬性口径）

- 单关节峰值：正向/反向力矩（力）分列（τ_max⁺/τ_max⁻）、速度峰值、加速度峰值、机械功率峰值（正向 max(P)、反向 max(−P)）。
- 峰值发生时间 tPeakS＋所在轨迹段 segmentIndex 必填。
- **持续时间窗**＝|值| 达到峰值的连续样本集合的最小覆盖时间区间 [windowStartS, windowEndS]——**样本级精确定义，不引入比例阈值**（避免自设数值；窗的数值等值容差仅在黄金算例逐例声明——测试对照口径，附录 D C7）；平顶峰值窗口自然覆盖整段平顶。
- 峰值绑定工况来源（包络合并时 conditionId 必填）。

### 7.3 RMS 与完整任务循环（DYN-03"含驻留"硬性口径）

- 定义（全卡固定）：`RMS_i = sqrt( (1/T_cycle) · ∫₀^{T_cycle} v_i(t)² dt )`——**时间加权**（非样本算术平均）；T_cycle＝完整任务循环时长**含驻留**（驻留段速度≈0/力矩=重力保持矩——重力矩计入 RMS，速度项贡献时间权重）。
- 非均匀采样：梯形时间加权数值积分（对采样网格），公式固定、黄金算例对照解析积分（V-30 行）；**禁止** Σ/√N 的样本平均（会因采样不均失真——口径钉死）。
- 时间缺失（上游无时间参数）→不伪造 RMS/能量/节拍（§4.6；字段显式无效＋缺失清单）。

### 7.4 多工况包络（DYN-07）

- 合并规则：逐关节逐量纲，跨工况取 max⁺/max⁻（方向分列）；PeakRecord.conditionId 记录来源工况；contributingConditions 稳定排序。
- **包络只属于统计结果，不自动形成工程判定**；**多工况包络不能替代全部必验工况**（EVI-02：覆盖矩阵逐工况 Executed 仍必需；coversAllMandatory 字段仅为呈现参考——判定归 evidence 覆盖矩阵）。
- **数据不足时不输出看似完整的包络**：任一来源工况 Partial/Failed→该关节 envelopeComplete=false＋来源清单；**空集合（无已完成工况）→包络不产出**（Empty 语义，绝不返回 0 或默认通过——NFR-COR-03）。
- 不得把单一工况外推为全部工况（包络必须携带来源工况集与完整性标记）。

### 7.5 机械功率、能量与符号约定（全卡固定）

- 功率：`P_i(t) = τ_total,i(t) · q̇_i(t)`，单位 W；正号＝驱动器输出正功（沿运动方向加速/提升），负号＝制动/下降/发电。
- 能量：`E⁺ = ∫ max(P,0) dt`（正功）、`E⁻ = ∫ min(P,0) dt`（负功，≤0）、`E_net = E⁺ + E⁻`；单位 J（§4.5 量纲说明）。
- 积分边界＝[t₀, t_N]（完整任务循环，含驻留）；时间基准＝上游轨迹时间轴；积分格式＝梯形时间加权（config.dyn.powerIntegral 唯一词表值）。
- **能量分项边界**：关节侧正负功分项归 dynamics（表 4 建议项）；电机侧能量分项与四象限工作制统计归 drivetrain（DYN-04 映射契约）——不混算。

### 7.6 统计完整性规则（汇总）

- 样本完整性：planned/actual 计数＋缺口定位（§4.6）；空样本→Empty（不产出统计，绝不做 0 值伪装）。
- 稳定排序：统计条目按 (jointIndex, 量纲 token, conditionId) 全序（NFR-COR-02）。
- **百分位统计不是 R1/DYN-01～08 必需功能**（需求无条目）——不实现、不进入阶段 C 验收范围（如未来需要走需求变更）。
- 单位/参考系：§4.5 表唯一权威。
- 结果进 evidence：绑定相应工况（caseScope）与 dyn Profile 证据项（§8.4）。

---

## 8. 多工况、drivetrain 交接与 evidence 交接

### 8.1 工况消费与执行状态（图示见 §8.3）

- 工况集合＝快照 caseSet（RequiredCaseSet，mandatory≡(level==Must)——requirements §6.2 冻结解析）；工况 ID＝caseId（ObjectId）；工具/负载/安装 Frame 随条件对象；夹取/释放/驻留→§5.4 事件时间线；重力方向/基座安装姿态→编译链（gravityBase，消费面只读）；目标节拍→processParams.targetCycleTimeS（Should 口径的呈现参考——节拍判定归 trajectory 域 TRJ-05/§12.4，dynamics 不重复判定）。
- 任务循环＝上游轨迹全程（多站序列含驻留）；急停/保持等设计工况＝condition 对象中的设计工况条目——**dynamics 对任意给定工况执行统一 RNEA 评估，不自行定义急停/保持的构造语义**（工况模板结构归 requirements，P-DYN-4）；多负载工况＝payloads 集合＋事件时间线。
- 必验/可选：必验由 requirements/evidence 冻结（mandatory≡Must）；dynamics 负责逐工况执行与统计，**不重新定义必验范围**。
- 执行状态词表（覆盖矩阵五态，evidence §6.6 承接）：Executed/NotExecuted/Invalid/NotApplicable/Failed——逐工况条目由本域按 caseSubset 产出，**漏验不得输出完整正式结论**（汇总呈现口径：漏验→整体 DataInsufficient 缺项）。
- 工况覆盖矩阵（图示）：

```
caseSet（冻结）＝ {C1(Must), C2(Must), C3(Should), C4(Must·disabled)}
     │ caseSubset 分批
     ▼
 ┌────────────────────────────────────────────────────────┐
 │ 工况   │ C1        │ C2        │ C3        │ C4(disabled) │
 │ 执行   │ Executed  │ Executed  │ NotExecuted│ NotApplicable│
 │ 批次   │ run/att1  │ run/att1  │   —       │   —          │
 └────────────────────────────────────────────────────────┘
 覆盖完备 ⇔ 全部 enabled∧mandatory 工况 Executed（evidence §6.6 原文）
 包络合并（DYN-07）＝Executed 工况的统计合并——覆盖完备性独立核算，包络不替代矩阵
```

- 空工况集合/工况引用缺失→可定位诊断（DYN-CONDITION-REF-MISSING / 汇总②级通用门禁——快照身份/覆盖矩阵缺失先行）；工况变化进输入切片并使结果失效（req.conditions 条目）；方案比较必须使用一致的需求、模型和工况基准（ComparisonBaseline inputBaselineId——evidence §6.5）；负载/工具/重力方向变化不得复用不兼容历史结果（对象 cv 变化→Superseded）。

### 8.2 drivetrain 交接（DYN-04/MDL-21/AT-38/ARCH §7.10 承接）

#### 8.2.1 关节侧→驱动侧映射图（图示；调用点边界标注）

```
dynamics（本域）                                  drivetrain（WP-18-T03，唯一映射实现）
┌────────────────────────────┐                  ┌──────────────────────────────────┐
│ RNEA 关节侧结果（与候选     │  JointSideSeries │ DriveTrainMappingEvaluator       │
│ 传动无关）：                │  Pack（§4.4 DTO）│  ③端口注册；输入＝关节侧序列      │
│  q/q̇/q̈/τ 序列（类型化）    ├─────────────────►│  ＋传动配置＋（R2）耦合矩阵 C      │
│  工具/负载惯量引用          │                  │                                  │
│  工况与任务循环引用         │                  │  τ_motor=Cᵀ·τ_joint+J_rotor·θ̈   │
│  传动映射版本引用位         │                  │  θ̈=C⁺·q̈（R2）；R1 对角等价链      │
└────────────────────────────┘                  │  → 电机侧 τ/ω/P 序列、η⁺/η⁻、     │
      调用点＝selection/宿主编排（D-DYN-5）       │    反射惯量 J·i²、惯量比、        │
      （dynamics 评估器内不嵌套调用映射）          │    能量分项、四象限工作制统计      │
                                                └───────────────┬──────────────────┘
                                                                ▼
                                                   selection（SEL-05 校核，唯一消费方）
```

#### 8.2.2 必须明确（任务指令逐条）

1. **drivetrain 是唯一映射实现**（DYN-04/ARCH §7.10）；dynamics 只提交关节侧序列和传动输入（JointSideSeriesPack），**不复制映射公式、不实现 τ_motor=Cᵀ·τ_joint＋J_rotor·θ̈、不自行计算 θ̈=C⁺·q̈、不对角化或准静态替代**。
2. 传动映射包含全部已批准交叉耦合项；常矩阵 C 的位置/力矩映射由 drivetrain 处理；电机侧转子惯量由 drivetrain 按统一契约处理；**转子等效惯量不得与壳体质量重复计入**（SEL-10——dynamics 侧保证：交出的负载/工具惯量为刚体物性，无转子项混入）。
3. C 非方、奇异或病态时由 modeling/runtime/drivetrain 既有契约返回诊断并阻止——dynamics 不把映射失败转写为整机工程不可行（映射链失败素材随对端诊断）。
4. **R1 对耦合链保持阻断**（上游门禁＋§5.6 防御）；**R2 MDL-21 启用后**，dynamics、drivetrain、selection 必须消费同一矩阵身份和版本（JointSideSeriesPack.couplingMatrixContentId 引用位——R1 恒 nullopt、零消费代码）。
5. 映射结果必须携带输入身份、矩阵内容身份、算法版本和工况身份（drivetrain 卡义务，本卡交接包已含全部输入身份）。
6. **selection 只能消费 drivetrain 提供的统一电机侧工作点**（dynamics 不直接供电机侧量——§9.3）。
7. 高速多轴工况不能用对角近似替代交叉耦合（M-12；dynamics 侧义务＝逐样本精确 RNEA 含全部耦合项——§5.2）。

#### 8.2.3 dynamics 不内嵌映射调用的理由（D-DYN-5）

- 表 4 dyn Profile 无电机侧证据项（evidence D-14：Profile 内容归需求，不新增）——评估器内调用映射将产生无 Profile 依托的结果块；
- 映射结果须绑定传动配置身份（独立切片语义，CON-05 逐评估切片）——混入 dynamics envelope 会破坏身份分离；
- ③端口嵌套调用缺注入点（P-KIN-2 同源问题）；
- 调用点归 selection（SEL-05 校核链）/宿主编排——上游结果机制（归档→消费）已足。**本决策不影响任务指令"通过 drivetrain 的唯一映射生成电机侧工作点"的交付**——该交付由 selection 链承接，dynamics 提供稳定输入契约。

#### 8.2.4 R1/R2 耦合边界（图示＝R1 能力与 R2 扩展边界表的耦合行）

| 项 | R1 | R2（MDL-21/WP-18-T05） |
| --- | --- | --- |
| 耦合矩阵 C | 零代码（dynamics 不接触 C） | 仍零接触——C 消费全链在 drivetrain（dynamics 只透传引用位） |
| mimic/闭环/线性耦合链 | 上游门禁阻断＋本域防御拒绝（§5.6） | MDL-21 启用后按其口径放开（线性耦合） |
| 对角传动比等价链 | drivetrain 以对角等价处理 R1 无耦合旋转传动（dynamics 无感） | 同左 |
| 交叉耦合项 | RNEA 关节侧逐样本精确（含机械耦合项） | 映射侧电机/驱动端交叉耦合由 drivetrain 计入 |

### 8.3 多工况覆盖和包络统计图

```
              ┌─ C1（Must）─ Executed ─ OperatingConditionResult₁（series+peaks+power+validity）
caseSet ──┼───┤─ C2（Must）─ Executed ─ OperatingConditionResult₂
（冻结）   │   └─ C3（Should）─ Executed ─ OperatingConditionResult₃
           └──（disabled/NotApplicable → 不参与）
                        │ ①覆盖矩阵逐工况条目（EVI-02）
                        │ ②包络合并（仅对 Executed 且 Complete 的工况）
                        ▼
        DynamicsEnvelope（逐关节 τ⁺/τ⁻/ω/q̈/P 峰值包络＋RMS＋来源工况集）
                        │
        ├─ 全部来源 Complete → envelopeComplete=true
        └─ 任一 Partial/Failed → envelopeComplete=false＋来源清单（不输出看似完整包络）
```

### 8.4 evidence 交接（dyn Profile——表 4 动力学行逐行实例化，不增删改写）

| itemId（提议，实现落位冻结） | itemClass | 对应表 4 内容 | 适用条件 |
| --- | --- | --- | --- |
| `dyn.joint-generalized-force-series` | Required | 关节侧广义力序列（RNEA、类型化单位） | 总是（不可行替代时按 C6 替代规则） |
| `dyn.peak-rms-statistics` | Required | 峰值（含持续时间窗与所在段）与完整循环 RMS（含驻留） | 总是 |
| `dyn.property-friction-provenance` | Required | 物性/摩擦参数来源标记（缺失按 DYN-06 降级并列入缺失清单） | 总是 |
| `dyn.load-condition-identity` | Required | 负载工况标识 | 总是 |
| `dyn.power-energy-split` | Suggested | 机械功率/能量分项 | 数据完整时产出 |
| `dyn.forward-dynamics-consistency` | Suggested | 正动力学一致性检查记录 | mode=Standard 且前置齐备；Skip→NotRun（非缺失） |
| 通用必需项 | Required | 快照身份/覆盖矩阵/模式标识 | evidence 内建隐式附加 |

- Profile：profileId="dyn"、version=1，经 EvidenceProfileRegistry 注册（**先于评估器注册**——evidence §13 顺序）；**不创建 dynamics 私有评估器注册表**（③端口唯一；NFR-MNT-04 零逐域转发包装器）。
- 评估键 `dyn-rnea-analysis`（kebab 词形 `[a-z][a-z0-9-]{1,63}`）；payload kindToken `dyn.rnea-analysis.v1`（canonical magic `IRDDYNA1`）。
- 依赖声明（descriptor.inputs）：`model.robot-design`(Object, Required)、`tcp`(Object, Required)、`req.conditions`(Object, Required)、`namemap`(NameMap, Required)、`config.dyn`(Configuration, Required)、`upstream.trajectory`(UpstreamResult, Required——upstreamKey="trj-sequence-plan")。**无 policy 条目**（不消费策略语义；策略身份经快照承载）。
- 结果绑定：payload/证据携带 {snapshotId, sliceId, evaluationKey, contractVersion, mode, task 五元组, caseScope}（evidence §5.6 模式）；DataInsufficient 缺失项全量列出（不短路）。

---

## 9. execution、selection、reporting 和 UI 协作

### 9.1 execution 协作

#### 9.1.1 提交链与身份生成

```
ui/workflow（dynamics 页命令）
  → 组装 AnalysisSnapshot（含 upstream.trajectory 引用——ui 选择 Current 轨迹运行）
  → TaskSubmission{snapshot, evaluatorKey="dyn-rnea-analysis", mode, priority, caseSubset, resumeFrom?}
  → ITaskScheduler::submit（五元组由 execution 分配：projectId/branchId/revisionId/runId/attemptId）
  → 共享 worker（manifest 握手含 dyn 评估器）→ 物化快照 → 注册表实例化评估器
  → 逐工况评估（ResultBatch 逐工况回传；CheckpointBatch 工况级检查点）
  → FinalOutput（EvaluationOutput canonical）→ RunRegistry 九步接纳（五元组核对→名称反解→envelope 校验）
  → aggregateVerdict → ResultEnvelope::make → results/<run-id>/ 归档
```

- **dynamics 不直接写项目目录**（归档经 execution→IResultArchivePort）；**不直接决定正式工程判定**（aggregateVerdict 归 evidence）；**不直接提交报告**（reporting 经章节注册消费归档结果）；**不直接管理缓存淘汰**（execution IExecutionCacheCoordinator）——四条协作全部经既有端口。

#### 9.1.2 快照传递与轨迹批次

- worker 物化（MaterializedSnapshotCodec，身份核对）后经工厂闭包绑定快照视图＋ITrajectorySourcePort；上游轨迹批次＝单次 payload 载入（轨迹非动力学产出批次——动力学分批粒度是**工况**）。

#### 9.1.3 能力声明（EvaluatorRuntimeCapabilities，注册期）

| 能力位 | 声明值（R1） | 依据 |
| --- | --- | --- |
| supportsPause | **false**（R1 不承诺；暂停请求→EX-CAPABILITY-UNSUPPORTED 显式反馈） | ARCH §4.3 Paused=R2；R2 承接见 §13.3 |
| checkpointGranularity | **Batch**（批＝工况；已完成工况结果即检查点单元） | enum{None,Batch,Segment,Sample} 最近语义 |
| forceTerminateCost | Moderate（工况级重算） | 声明与呈现 |
| evaluationTimeout | nullopt（R1 默认不启用；2 s/10 s 取消协议由 execution 统一承载） | execution §5.5 |

#### 9.1.4 检查点（写什么/何时写/如何恢复）

- 时机：每工况完成（series+stats+powerEnergy 冻结后）经通道 CheckpointBatch 回传；checkpointFormatVersion=1；resumable=true；completedUnits/totalUnits=工况计数（续跑统计不重复——AT-35 口径）。
- 内容（域中间态，对 execution 不透明）：已完成工况的 OperatingConditionResult 集合、事件时间线解析缓存、config 回执。
- 恢复：judgeCheckpointCompatibility（key∧sliceId∧契约版本∧格式版本∧integrity）→新 attempt 续跑，输入快照不变；旧版本检查点不迁移（P-EX-9——跨版本全量重跑，如实承接）。

#### 9.1.5 取消/超时/崩溃/迟到结果

- 取消：工况边界轮询 cancellationRequested→2 s 入 Canceling、停止派发新工况批次→Canceled envelope（**取消后不得发布完整动力学序列**——接纳层第⑦步拒绝 FinalEnvelope；正常取消零错误诊断）。
- 超时/worker 崩溃：execution 协议（EX-FORCE-TERMINATED/EX-WORKER-CRASHED）；崩溃只致当前任务失败（NFR-REL-02），最近检查点保留。
- 迟到结果：五元组 RunRegistry 核对（跨项目迟到绝不写入新项目 results/——AT-10）；归档位置取自登记记录；当前性独立判定。
- 重新计算不覆盖旧结果：新 RunId＋results 只增（PA-2）；同入口身份一致性：同一（快照/轨迹/策略/工况身份）经 sliceId 强制——不同入口（ui 页/reporting 复算/未来 OPT-D）组装同切片即等价（NFR-COR-02/CON-06）。

### 9.2 evidence 协作（注册/证据/覆盖/当前性）

- 注册（装配期，主/worker 同清单）：先 `dyn` Profile（EvidenceProfileRegistry）→ `dyn-rnea-analysis` 评估器（EvaluatorRegistry；重复键拒绝；descriptor 校验）。Quick 输出标 `screening-only`；Preview 不产生 envelope（构造边界拒绝）。
- 证据明细/Profile 绑定/覆盖矩阵：§8.4/§8.1。
- 当前性：ResultCurrentness 五步（evidence §8.1）——本域相关失效原因映射：ObjectContentChanged（模型/工具/工况对象）、ConfigurationChanged（config.dyn）、**UpstreamResultChanged（上游轨迹重算/失效）**、NameMapChanged、EvaluatorContractChanged、EnvironmentChanged；**PolicyChanged 不出现**（无 policy 依赖条目——策略变更不失效动力学，§4.1）。

### 9.3 selection 协作（交接协议；selection 卡未产出——P-DYN-2）

- dynamics 交付面：归档 Completed 结果（payload dyn.rnea-analysis.v1）＋JointSideSeriesPack 契约（Handoff.hpp 公共头数据）。
- selection 消费链（SEL-05）：其校核评估器经 ③端口调用 drivetrain 映射，输入=JointSideSeriesPack＋传动配置；dynamics 不直接对接 selection 内部。
- SEL-10 回填闭环：选型应用→新修订→drivetrain-design 对象 cv 变化→**dynamics 关节侧结果不失效**（不依赖该对象）→selection 链复算；复核完成前不沿用原通过结论（其链义务）。
- **关节侧与电机侧不混为同一结果对象**：电机侧工作点/效率/反射惯量/四象限均为 drivetrain 评估产物（其 payload/证据），dynamics 归档结果内零电机侧字段。

### 9.4 diagnostics 协作（DYN-\* 码）

- 注册协议：装配期 ownerUnit="dynamics" 注册（diagnostics §4.5；**码值权威归 StableCodeRegistry**，本表为拟注册清单——随 WP-17-T02/T03 提交）。
- 拟注册清单（v1 设计基线）：

| 码（建议） | 严重度 | 比较型 | 用途 |
| --- | --- | --- | --- |
| DYN-INPUT-INVALID | error | 否 | 输入非法（跨修订绑定/维度不匹配/配置非法/模式组合非法）——①级素材 |
| DYN-UPSTREAM-TRAJECTORY-INCOMPATIBLE | error | 否 | 上游轨迹身份/模型不兼容 |
| DYN-TIME-PARAM-MISSING | error | 否 | 轨迹缺时间参数（拒绝正式评估） |
| DYN-SERIES-NON-MONOTONIC | error | 否 | 轨迹时间重复/倒退/零间隔 |
| DYN-SAMPLE-GAP | warning | 是（缺口数/计划数/1） | 缺样本/采样间隙（附首个缺口 t/段） |
| DYN-NON-FINITE | error | 否 | 非有限输入/输出（附样本定位） |
| DYN-DIMENSION-MISMATCH | error | 是（实际/期望/1） | 维度不匹配 |
| DYN-RNEA-FAILED | error | 否 | RNEA 计算失败（工况/样本定位） |
| DYN-FD-INITIAL-STATE-MISSING | warning | 否 | 正动力学初始状态缺失（建议项 NotRun） |
| DYN-FD-DIVERGED | warning | 否 | 积分发散（异常检测——DYN-05 本义） |
| DYN-FD-CONSISTENCY-FAILED | warning | 是（实际误差/阈值/单位） | 一致性超阈值 |
| DYN-PROPERTY-DOWNGRADED | warning | 否 | 物性/负载估算降级（DYN-06；附来源计数） |
| DYN-FRICTION-MISSING | warning | 否 | 摩擦参数缺失（MDL-16→DataInsufficient 素材） |
| DYN-CONDITION-REF-MISSING | error | 否 | 工况引用缺失/空工况集合 |
| DYN-COUPLING-GATE-REJECTED | error | 否 | 耦合链防御拒绝（§5.6） |
| DYN-FD-NUMERIC-ANOMALY | warning | 否 | 正动力学数值异常（WP-17-T05 增行——表尾追加，v1 表 15 码→16 码；质量阵奇异/病态致线性求解失败，§6.1 ④"数值异常（刚性/溢出）"分支的产码面；Failed 只进建议证据项 Invalid＋warning 诊断，不判定模型无效） |

- 诊断构造：IDiagnosticFactory（码已注册校验、subject/比较型完整、DiagContext——评估路径 snapshotId/sliceId 必填）；每诊断绑定快照/工况/段/对象 ID/算法版本（§5.6）；比较型含实际值/要求值/单位；不适用字段显式标记。
- worker 不产生可确认诊断（SA-15）；R1 无 ConfirmableFinding 产生点。

### 9.5 UI 协作（DYN-08；插件零计算逻辑）

| 命令/能力（提议 token） | 作用 | 修订 | 计算线程 |
| --- | --- | --- | --- |
| `dynamics.analyze` | 发起动力学评估任务（选择 Current 轨迹运行＋工况集→组装快照→submit） | 无 | 提交即返；计算在 worker |
| `dynamics.show-curves` | 各关节曲线联动（q/q̇/q̈/τ/P 逐关节多曲线＋游标联动）——读归档 payload 投影 | 无 | UI 线程（零计算） |
| `dynamics.locate-peak` | 峰值定位（曲线游标跳转峰值时刻＋三维姿态同步） | 无 | UI 线程 |
| `dynamics.replay-at` | 三维轨迹时刻回放：按 t 驱动会话姿态（View3DSessionPoseContract 四常量：sessionStateOnly=true/writesDesignModel=false/producesRevision=false/invalidatesResults=false——零修订机器可断言） | 无 | UI 线程（插值查表） |
| `dynamics.export-curve-data` | 曲线数据导出（经 reporting/io 通道或域 DTO 副本——R1 经域 CSV 通道按 §15.13 trajectory 先例，格式登记 P-DYN-10） | 无 | worker/IO 线程 |

- 投影：DomainReadinessItem（domainKey="dynamics"）供七态投影；回放数据（逐时刻关节状态）随 payload（DYN-08 数据面——不入 dyn Profile，D-14 约束）。
- **UI 线程不得执行动力学计算**（RNEA/仿真/统计全在 worker）；IDynamicsCommandHandler 只是领域命令适配器（O13 边界）。
- ui 卡登记缺口：WP-17-T09 未登记（P-DYN-8，同 P-TRJ-8 模式）。

### 9.6 reporting 协作

- 章节注册：装配期经 `IReportSectionProvider`（reporting §9.2）注册 `dynamics-envelope` 章节（reporting §5 章节表第 10 行——**归属 dynamics**，DYN-01～07 投影：类型化广义力/峰值窗/RMS/包络）。
- C 级前置：对应域结果可产出 Completed envelope（reporting §13 行 1386 口径）——真实章节内容不得以替身数据冒充验收（RP-STATE-4）。
- 供数义务：曲线/峰值/RMS/包络/功率能量/数据不足/估算值/正动力学一致性结果/轨迹段定位/限定语（estimated、data-insufficient、screening-only、historical-superseded、not-applicable——reporting §6.4 词表，dynamics 供数据源不供文案）；**RPT-05 限定语不得弱化**。
- 大数组（动力学曲线序列）以 ResultBinding 引用保留（runId＋fieldPath——reporting §8 渲染总则），不复制。
- 报告模型衔接：hasDynamicWorkCell/hasFullMassInertia/hasJointVelocityLimits 能力位（reporting §10.2 模型字段）由本域 readiness 投影支撑。

---

## 10. 公共接口详细设计

> 本节签名＝**设计基线（Draft）**：消费对端类型的接口（注入端口、评估器、Profile、payload、码表）为对端契约，变更须对端会签；本域内部接口实现任务允许按 DTB §5.4 微调并登记偏差。任务指令要求的 7 个接口全部覆盖（§10.1～§10.7）；**不新增域接口**的理由见 §10.9；注入端口见 §10.8。目标名称遵守项目约定：`sdurws_ird_dynamics`／`RWS::ird::dynamics`／`sdurws_ird_dynamics_plugin`／`sdurws_ird_dynamics_test`／`sdurws_ird_dynamics_contract_test`——**均为目标设计，当前不存在**；`sdurws_ird_dynamics_worker` 不设立（§3.3）。

### 10.0 通用约定（适用于 10.1～10.8，各接口不再重复）

| 维度 | 约定 |
| --- | --- |
| 错误类型 | 域错误 `DynamicsError : std::runtime_error`（token 前缀 `dynamics/...`）；**调用方错误**（前置违约/非法参数/身份缺失）fail-fast 异常、不发稳定码；**环境错误**（数值失败/上游不兼容/资源问题）返回素材/诊断（ERR-01），稳定码归属＝diagnostics StableCodeRegistry（DYN-\*，§9.4） |
| 数据不足行为 | 返回结构化素材（Missing 清单/DynamicsValidity）＋证据项 Missing/Invalid/NotApplicable——绝不伪造数值、绝不输出 0 冒充 |
| 取消行为 | 工况/批次边界轮询取消观测；取消＝非错误（UX-03），cancelled 语义返回 |
| 超时/worker 崩溃 | execution 协议承接（EX-WORKER-HUNG/EX-WORKER-CRASHED/EX-FORCE-TERMINATED）；本域接口不内置超时（evaluationTimeout 归 TaskCapability） |
| 线程安全 | 数据类型并发只读；构建器/评估器实例单线程使用（每任务一实例）；rwsim 状态每线程独立（makeState） |
| 确定性 | 同（输入字节＋config.dyn＋契约版本＋线程数）→等价输出（NFR-COR-02）；逐工况数值结果与线程数无关（工况间并行无跨工况归约；包络 max/min 精确）；线程数进 config 身份 |
| 随机种子 | R1 动力学无随机源（确定性数值计算；正动力学积分确定）——无 seed 字段（§4.1） |
| 所有权/生命周期 | 全部值语义；评估器 stateless（每 worker 每任务一实例）；中间态生命周期≤单次 evaluate |
| 副作用 | 零写盘、零修订、零项目目录写入（归档经接纳层）；仿真不触碰共享快照 |
| 缓存/检查点边界 | 缓存判定归 evidence judgeCacheHit（sliceId+mode+契约版本+Profile）；检查点＝工况级（§9.1.4），域中间态对 execution 不透明 |
| 身份要求 | 一切正式接口要求输入含完整身份块（snapshotId/sliceId/上游轨迹身份/工况身份）——缺身份拒绝正式评估（§4.1） |
| R1/R2 | 本节接口全部 R1；R2 变化仅限能力位/耦合引用位（§13） |

### 10.1 IInverseDynamicsEvaluator（RNEA 逆动力学评估）

```cpp
/// 关节空间逆动力学评估（DYN-01/02；逐样本 RNEA，含全部分项）。
class IInverseDynamicsEvaluator {
public:
    virtual ~IInverseDynamicsEvaluator() = default;
    /// @param[in]  request  评估输入（快照视图、上游轨迹激励、工况、config.dyn——身份块完整）
    /// @param[in]  context  取消/进度/对象字节上下文（evidence IEvaluationContext）
    /// @param[out] —        返回值承载
    /// @return 逐工况 OperatingConditionResult（单位见 §4.5；关节空间标量；重力基座系投影）
    virtual InverseDynOutcome evaluate(const InverseDynRequest& request,
                                       IEvaluationContext& context) = 0;
};
```

| 项 | 内容 |
| --- | --- |
| 前置 | 快照已物化且身份核对通过；上游轨迹过 §4.3 兼容性校验；工况集非空且引用可解析 |
| 后置 | 逐工况 series 冻结（身份块完整）；恒等式 τ_total=Σ分项逐样本成立；诊断绑定四重定位 |
| 输入身份要求/结果身份 | request 携带 snapshotId/sliceId/UpstreamResult/工况集；结果 series.contentIdentity＋payload 汇总身份 |
| 取消 | 工况边界（§9.1.5） |
| 数据不足 | 物性/摩擦缺失→序列继续＋DYN-06 降级标记＋缺失清单；轨迹缺时间参数→整体拒绝 |
| 线程/确定性 | 每任务一实例；工况间可并行（threadCount）；逐工况结果线程数无关 |
| 合法示例 | 两工况×黄金轨迹→两 series＋恒等式校验通过＋倒挂工况重力矩符号正确（AT-37 联动） |
| 非法示例 | 轨迹关节数≠模型关节数（fail-fast）；混用另一修订的轨迹运行（DYN-INPUT-INVALID） |
| R1/R2 | R1（WP-17-T03） |

### 10.2 IForwardDynamicsValidator（正动力学一致性，DYN-05）

```cpp
/// 正动力学响应一致性检查（§6；建议证据项 dyn.forward-dynamics-consistency 的产出器）。
class IForwardDynamicsValidator {
public:
    virtual ~IForwardDynamicsValidator() = default;
    /// @param[in] request 参考段（来自逆动力学同源序列）＋forwardCheck 配置
    /// @param[in] context 取消观测
    /// @return ForwardCheckOutcome{state: Passed|Failed|NotRun|NotApplicable,
    ///         maxErrQ, rmsErrQ, maxErrQd, rmsErrQd, tOfMaxErr, numericAnomaly?}
    virtual ForwardCheckOutcome validate(const ForwardCheckRequest& request,
                                         IEvaluationContext& context) = 0;
};
```

| 项 | 内容 |
| --- | --- |
| 前置 | 参考段逆动力学序列可用；初始状态可解析；mode=Standard |
| 后置 | 独立仿真状态（共享快照零修改）；误差四元组＋发生时间；异常检测记录 |
| 容差 | toleranceQ/toleranceQd（分析配置来源，进身份）；测试对照容差由黄金算例另声明（C7 分离） |
| 数据不足/不适用 | 初始状态缺失→NotRun＋素材；mode=Skip→NotApplicable |
| 失败语义 | Failed≠模型无效（§6.3.5）；发散/数值异常→DYN-FD-DIVERGED/DYN-FD-NUMERIC-ANOMALY |
| 确定性 | 同配置同输入→等价误差序列（积分确定）；仿真引擎非确定因素（如迭代求解器容差内漂移）→黄金算例按附录 D 第 9 项声明对照容差 |
| 合法示例 | 黄金二连杆：τ_ref 回放→max|e_q|≤toleranceQ→Passed |
| 非法示例 | 用另一工况的负载做仿真（共源违约——fail-fast） |
| R1/R2 | R1（WP-17-T05）；rwsim 引擎类选型为 T05 落位验证项（P-DYN-7） |

### 10.3 IDynamicsSeriesBuilder（序列构建）

```cpp
/// 从 RNEA 逐样本输出构建 DynamicsSeries（排序/完整性/身份冻结；§4.4/§4.6 纪律执行点）。
class IDynamicsSeriesBuilder {
public:
    virtual ~IDynamicsSeriesBuilder() = default;
    virtual void addSample(const DynamicsSample& sample) = 0;      // [in]
    virtual DynamicsSeries finalize(SeriesIdentity identity) = 0;  // [in] 身份块；[out] 冻结序列
};
```

- 前置：样本时间单调非降（违例→DYN-SERIES-NON-MONOTONIC，不排序修复）；finalize 后不可变。
- 职责：排序校验、planned/actual 计数、缺口/非有限标记、身份块冻结、contentIdentity 计算。
- 合法示例：全采样 finalize→Complete；非法示例：重复时间戳 addSample 不报错但 finalize 标记（校验在评估入口——见 §4.3；builder 侧为防御性二次校验）。
- R1（WP-17-T04）。

### 10.4 IDynamicsEnvelopeCalculator（峰值/RMS/包络）

```cpp
/// 峰值（含窗与段）/完整循环 RMS（含驻留）/多工况包络计算（§7；统计口径唯一实现点）。
class IDynamicsEnvelopeCalculator {
public:
    virtual ~IDynamicsEnvelopeCalculator() = default;
    virtual std::vector<PeakRecord> computePeaks(const DynamicsSeries& series) = 0;          // [in]
    virtual double computeRms(const DynamicsSeries& series, std::uint32_t jointIndex) = 0;   // [in]；时间加权
    virtual DynamicsEnvelope mergeEnvelope(const std::vector<OperatingConditionResult>& results,
                                           const CaseCoverageSnapshot& coverage) = 0;        // [in]
};
```

- 前置：series 完整性已知（Partial 允许但包络标记传播）；merge 前各结果身份块齐备。
- 后置：RMS 时间加权含驻留（T_cycle）；包络来源工况集＋envelopeComplete 传播；空集→Empty（不产出）。
- 合法示例：三工况合并→τ⁺ 包络来源=C1；非法示例：对空结果列表调用 merge（Empty 语义返回而非 0 值包络）。
- R1（WP-17-T04/T07）。

### 10.5 IPowerEnergyCalculator（功率/能量）

```cpp
/// 机械功率与正负功分项（§7.5 符号约定唯一实现点；数据完整时产出）。
class IPowerEnergyCalculator {
public:
    virtual ~IPowerEnergyCalculator() = default;
    virtual PowerEnergySummary compute(const DynamicsSeries& series) = 0;  // [in]
};
```

- 前置：时间参数可用（否则 timeParamAvailable=false＋字段显式无效——不伪造积分）。
- 后置：E⁺/E⁻/E_net/meanPower/峰值（窗＋段）；积分边界 [t₀,t_N] 含驻留；单位 J/W（§4.5）。
- 合法示例：含驻留循环→E⁻<0< E⁺（存在制动段）；非法示例：对缺时间参数序列求能量（返回无效标记而非 0）。
- R1（WP-17-T04）。

### 10.6 IDynamicsEvidenceBuilder（证据装配）

```cpp
/// dyn Profile 证据项装配（§8.4；EvaluationOutput.evidence 的域内组织——envelope 构造仍归调用侧）。
class IDynamicsEvidenceBuilder {
public:
    virtual ~IDynamicsEvidenceBuilder() = default;
    virtual void addSeries(const DynamicsSeries& series) = 0;                 // [in]
    virtual void addConditionResult(const OperatingConditionResult& result) = 0; // [in]
    virtual void addForwardCheck(const ForwardCheckOutcome& outcome) = 0;     // [in]
    virtual DynamicsEvidence build() = 0;                                     // [out]
};
```

- 前置：Profile 已注册（装配期校验）；输入结果身份完整。
- 后置：必需四项/建议两项状态齐备（Satisfied/Missing/Invalid/Unverified/NotApplicable 显式）；缺失全量列出。
- 合法示例：纯关节完整链→四必需 Satisfied＋FD 项 Satisfied；非法示例：跳过 provenance 项装配（缺项即 Missing，不可省略）。
- R1（WP-17-T06/T10）。

### 10.7 IDynamicsCommandHandler（领域命令适配器）

```cpp
/// 领域命令适配器（O13 边界：只是适配器——不是全局命令注册表、不是 UI 命令权威；
/// 命令注册权威＝ui CommandRegistry，命令 token 见 §9.5 表）。
class IDynamicsCommandHandler {
public:
    virtual ~IDynamicsCommandHandler() = default;
    /// @param[in]  commandToken  命令 token（dynamics.* 词表，§9.5）
    /// @param[in]  payload       会话上下文（选中轨迹运行/工况集/时刻等；零计算语义）
    /// @param[out] outcome       提交结果（TaskSubmission 受理/投影查询句柄）
    virtual CommandOutcome handle(std::string_view commandToken,
                                  const CommandPayload& payload) = 0;
};
```

- 硬边界：处理器内**零动力学计算**（组装快照/提交任务/投影查询）；零修订（全部会话命令，AT-04）；不注册全局快捷键。
- 合法示例：dynamics.analyze→组装快照→submit；非法示例：处理器内直接调 IInverseDynamicsEvaluator（计算必须在 worker）。
- R1（WP-17-T08/T09）。

### 10.8 注入端口（装配面适配；最小接口）

```cpp
/// 上游轨迹归档结果读取端口（装配面适配 results/<run-id>/ 读取；R-1：零 trajectory 库依赖）。
class ITrajectorySourcePort {
public:
    virtual ~ITrajectorySourcePort() = default;
    /// @param[in]  upstreamRunId  上游轨迹运行 ID（RunId）
    /// @return 轨迹 payload canonical 字节＋身份块（snapshotId/sliceId/contractVersion/algorithmVersion/
    ///         payload 内容身份）；不可用/身份不符→端口错误（dynamics/trajectory-source-*）
    virtual TrajectorySourceResult loadTrajectoryRun(core::RunId upstreamRunId) const = 0;
};
```

- 归属：接口归本域（TrajectorySourcePort.hpp），**适配器实现落位待裁决**（P-DYN-1——与 P-KIN-2/P-TRJ-3 同源：L5 装配 vs 上游单元导出适配面）。
- R1（WP-17-T03 消费）。

### 10.9 接口清单完备性说明（为何不新增域接口）

- 任务指令 7 接口已覆盖：评估（10.1）、校验（10.2）、序列（10.3）、包络统计（10.4）、功率能量（10.5）、证据装配（10.6）、命令适配（10.7）。
- 多工况编排不设独立接口：逐工况循环与包络合并由评估器内部编排（10.1/10.4 承载）——新增 runner 会与 10.1 职责重叠并引入第二状态面（NFR-MNT-04 禁止无边界价值包装器）。
- drivetrain 交接不设调用接口：dynamics 不内嵌映射调用（§8.2.3/D-DYN-5）——交接是数据契约（JointSideSeriesPack，DynTypes.hpp），非接口。
- 注入端口仅 ITrajectorySourcePort 一个（10.8）——不引入新的跨单元依赖面；policy/kinematics 端口无需求（零碰撞、零 FK/IK）。
- 无需登记架构待裁决的新端口（P-DYN-1 为既有同源问题的归属确认，非新边）。

---

## 11. 验证方案及故障注入矩阵

### 11.1 验证总体口径（只设计，不宣称已执行）

- 框架：googletest 经 vcpkg（DTB §5.5）；目标 `sdurws_ird_dynamics_test`/`_contract_test`（T10 注册）；黄金数据集 `testdata/golden/dyn-*`（testkit schema）＋容差档案（**测试对照容差逐例声明**——附录 D 第 9 项/C7）。
- 双模式构建＋`ird_gates` 零命中＋留痕＝DoD（DTB §5.2）；**本卡交付时全部未执行**（§1.2 C-5）。
- 替身纪律：替身（TrajectorySourcePort 替身、runtime 视图替身、FaultInterceptor）只验证端口/状态/错误传播——**不能证明 RNEA、RobWorkSim 仿真、真实快照的正确性**；解析算例/黄金对照承担算法正确性（NFR-COR-01）。
- **本次不启动 GUI**（§11.5 只设计流程）。

### 11.2 故障注入矩阵（设计；列含任务指令要求的失败分类/诊断定位/evidence/execution 影响）

| # | 用例 | 需求/AT | 前置 | 操作/故障注入 | 预期结果 | 失败分类/诊断定位 | evidence/execution 影响 | 观测点 | R1/R2 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| V-01 | RNEA 二连杆解析算例 | DYN-01/NFR-COR-01/AT-07 | 黄金模型+解析解数据集 | 标准轨迹激励 RNEA | 逐样本 τ 与解析解一致（附录 D 第 9 项对照） | 算法缺陷→黄金失败（留痕） | 序列项 Satisfied | series/黄金对照 | R1 |
| V-02 | 多关节惯性耦合 | DYN-01/M-12 | 三轴耦合黄金模型 | 惯性耦合工况 RNEA | 科氏/离心分项含交叉耦合（不对角化） | 同上 | 同上 | 分项可加性恒等式 | R1 |
| V-03 | 质量/质心/惯量变化 | DYN-01/02 | 参数扫描黄金集 | 逐一变更重算 | τ 随参数单调合理变化、与解析一致 | 同上 | 同上 | 黄金对照 | R1 |
| V-04 | 重力方向变化 | DYN-02/AT-37 | 地面/壁装/倒挂模型 | 三种安装姿态编译→RNEA | 静态重力矩符号/量值随姿态正确（AT-37 动力侧） | 模型/投影缺陷→对照失败 | 同上 | gravityBase 消费路径 | R1 |
| V-05 | 基座—世界变换只应用一次 | MDL-22/AT-37 | 倒挂模型 | 检测二次旋转注入 | 二次叠加被拦截（runtime RT-BW-4 同款自检）＋dynamics 消费路径唯一 | 越界实现→门禁/测试失败 | — | gravityBase 单点消费 | R1 |
| V-06 | 外部负载变化 | DYN-02 | 负载变体黄金集 | Grasp/Release 事件时间线 | 变体切换处 τ 阶跃正确、事件间隙行为符合 §5.4 | — | 负载工况标识项 | payloadVariantIndex | R1 |
| V-07 | 工具 TCP/质量变化 | DYN-02/CON-05/AT-05 | 工具变更前后 | 重评 | TCP 对象 cv 变化→动力学 Superseded | — | UpstreamResult/Object 失效原因 | ResultCurrentness | R1 |
| V-08 | 黏性/库仑摩擦变化＋偏置 | DYN-02/MDL-16 | 摩擦扫描黄金集 | fv/fc/bias 变更重算 | τ_fric 分项按 §5.5 约定变化；q̇=0 时 fc 项=0 | — | provenance 项 | 分项序列 | R1 |
| V-09 | 摩擦参数缺失 | DYN-06/MDL-16 | fv 未填写注入 | 评估 | frictionMissing＋缺失清单→④级 DataInsufficient 素材；不包装精确 | 数据不足（非错误崩溃） | property-friction-provenance=Invalid/降级标记 | validity/缺失清单 | R1 |
| V-10 | 物性估算来源 | DYN-06/MDL-05 | GeometricEstimate 模型 | 评估 | estimated 计数＋限定语 estimated；不升级精确 | 同上 | 同上 | estimatedLinkCount | R1 |
| V-11 | 物性非有限值 | NFR-COR-03 | NaN 惯量注入 | 评估 | 输入非法样本（NonFiniteInput）——不静默转 0 | 输入数据问题 | 证据 Invalid＋DYN-NON-FINITE | 样本定位 | R1 |
| V-12 | 物性张量非法 | MDL-06/附录 D 6/7 项 | 非对称张量注入 | 防御性复检 | 拒绝（对称性相对 1×10⁻¹² 复检）——建模断言前置的纵深防御 | 输入非法 | — | 复检诊断 | R1 |
| V-13 | 正逆动力学一致性 | DYN-05/表 4 | 黄金模型+参考段 | τ_ref 回放仿真→比较 | max/RMS 误差≤配置阈值→Passed | 超限→Failed＋比较型诊断 | FD 建议项状态 | FD 记录 | R1 |
| V-14 | 正动力学初始状态缺失 | DYN-05 | 剔除初始状态注入 | 校验 | NotRun＋DYN-FD-INITIAL-STATE-MISSING（建议项缺失不阻断） | 数据不足（建议项） | FD=NotRun/缺失清单 | — | R1 |
| V-15 | 正动力学时间参数缺失 | DYN-05 | 上游无时间参数 | 评估 | 整体拒绝（DYN-TIME-PARAM-MISSING）——不伪造节拍/功率/能量 | 输入非法 | 评估不启动 | 拒绝诊断 | R1 |
| V-16 | 正动力学积分不收敛 | DYN-05 | 刚性工况注入 | 仿真 | 发散检出→Failed＋DYN-FD-DIVERGED；**不判模型无效** | 数值异常（建议项 Failed） | FD=Invalid＋诊断 | 异常记录 | R1 |
| V-17 | 非有限输入（轨迹侧） | NFR-COR-03 | NaN q̇ 注入 | 评估 | 样本 NonFiniteInput＋序列非完备标注 | 输入数据问题 | 证据降级 | 样本定位 | R1 |
| V-18 | 轨迹样本缺失 | §4.6 | 抽取计划缺口注入 | 评估 | Partial＋DYN-SAMPLE-GAP（附缺口 t/段）；不插值补齐 | 数据不足 | 证据降级 | planned/actual | R1 |
| V-19 | 时间倒退/重复时间戳 | §4.3/§4.6 | 时间轴倒退注入 | 评估入口校验 | 拒绝＋DYN-SERIES-NON-MONOTONIC | 输入非法 | 评估不启动 | 入口校验 | R1 |
| V-20 | 非均匀采样 RMS | DYN-03 | 变步长黄金集 | RMS 计算 | 时间加权积分与解析一致（≠样本平均） | 统计缺陷→黄金失败 | peak-rms 项 | 黄金对照 | R1 |
| V-21 | 空样本 | §7.6/§4.6 | 空轨迹注入 | 评估 | 拒绝（无激励）——绝不做 0 值统计 | 输入非法 | — | — | R1 |
| V-22 | 峰值定位/持续时间窗 | DYN-03/AT-07 | 平顶峰值黄金集 | 统计 | tPeak/segment/window 端点正确（样本级覆盖） | 统计缺陷→黄金失败 | peak-rms 项 | PeakRecord | R1 |
| V-23 | RMS 分母/含驻留 | DYN-03 | 含驻留黄金循环 | RMS/能量 | T_cycle 含驻留；重力保持矩计入 | 同上 | 同上 | cycleDurationS | R1 |
| V-24 | 多工况覆盖 | EVI-02/AT-07 | 三必验工况 | 全执行 | 覆盖矩阵全 Executed→正式判定可行 | — | 覆盖矩阵 | CaseCoverageMatrix | R1 |
| V-25 | 多工况漏验 | EVI-02/AT-07 | 漏一必验工况注入 | 部分执行 | 不得输出正式通过（矩阵不完备→缺项呈现） | 流程失败 | 覆盖不完备→整体 DataInsufficient | 覆盖矩阵/报告缺项 | R1 |
| V-26 | 包络合并不替代必验 | DYN-07/EVI-02 | 包络已合并但一工况 Failed | 汇总 | 包络标注不完整＋覆盖矩阵仍缺项 | — | 两者独立呈现 | envelopeComplete/矩阵 | R1 |
| V-27 | 缺少必验工况（空集） | EVI-02/P-EV-7 | 空必验集注入 | 评估 | 可定位诊断＋保守处置（DataInsufficient——evidence 口径） | 输入/门禁问题 | ②级门禁 | — | R1 |
| V-28 | 关节侧/电机侧分离 | DYN-04/SEL-05 | 全链检查 | 结果审计 | dynamics 归档结果零电机侧字段；selection 只经 drivetrain 取工作点 | 越界→review 打回 | payload 结构断言 | payload 键集 | R1 |
| V-29 | drivetrain 无耦合映射（对角等价） | DYN-04/WP-18-T03 | 无耦合链+传动配置 | 交接包→映射 | R1 对角等价链由 drivetrain 验证（dynamics 侧仅验证交接包身份完整） | — | （drivetrain 域验收） | JointSideSeriesPack | R1 |
| V-30 | drivetrain 虚功映射/效率/反射惯量/转子不重复 | DYN-04/WP-18-T03 | 传动黄金数据 | 映射验收 | 归 drivetrain 卡验收（本卡 V-29/V-31 为消费侧衔接） | — | — | — | R1（实现归 WP-18） |
| V-31 | MDL-21 耦合矩阵/交叉耦合/非方/奇异/病态 | DYN-04/AT-38 | R2 数据集 | 映射边界 | 归 drivetrain/WP-18-T05 验收；dynamics 断言零 C 消费代码（静态扫描） | — | — | R-扫描 | **R2**（AT-38） |
| V-32 | R1 耦合链阻断 | MDL-21/§5.6 | mimic 链注入 | 评估 | 上游门禁阻断在先；防御性 DYN-COUPLING-GATE-REJECTED 兜底；**无静默对角化** | 门禁拒绝 | 评估不启动 | 拒绝诊断 | R1 |
| V-33 | R2 耦合链启用边界 | MDL-21/WP-18-T05 | R2 启用后 | 全链 | dynamics 仍零耦合代码、只透传矩阵身份引用位 | — | — | couplingMatrixContentId | **R2** |
| V-34 | drivetrain 映射版本不兼容 | DYN-04 | 版本失配注入 | 交接 | 交接包身份校验拒绝（对端契约版本不匹配→DYN-INPUT-INVALID 族素材） | 环境错误 | — | 身份块 | R1 |
| V-35 | 取消 | TASK-01/NFR-PERF-02/AT-34 | 长任务 | 运行中取消 | 2 s 入 Canceling；工况批次停止；Canceled envelope；无完整序列发布；零错误诊断 | — | Canceled×NotApplicable | 状态机/envelope | R1 |
| V-36 | 超时 | NFR-PERF-02 | 无响应注入 | 协议超时 | 强杀→Failed＋检查点保留 | 环境错误 | Failed | EX-FORCE-TERMINATED | R1 |
| V-37 | worker 崩溃 | NFR-REL-02/AT-11 | 评估中崩溃注入 | 进程终止 | 仅当前任务失败；主界面不退；恢复语义 | 环境错误 | Failed/Interrupted | 退出码/恢复扫描 | R1 |
| V-38 | 检查点写入/恢复 | TASK-01/AT-35 | 工况级检查点 | 强杀→resume | 已完成工况不重算；统计不重复；输入不变 | — | 恢复后完整接纳 | checkpoint/续跑统计 | R1 |
| V-39 | 取消后不得成为完整结果 | TASK-02/CON-04 | 取消后重投递 | 接纳 | FinalEnvelope 拒绝（terminationCause）；不作缓存命中 | — | Canceled×NotApplicable | 接纳第⑦步 | R1 |
| V-40 | Quick/Verified 模式 | EVI-01 | 双模式 | 同输入双评 | Quick 标 screening-only；跨模式缓存 Incompatible | — | mode 失效判定 | judgeCacheHit | R1 |
| V-41 | 算法/模型/轨迹/工况版本不兼容 | CON-05/AT-05 | 四类版本变更注入 | 重评/查询 | 各自 sliceId 失效原因（EvaluatorContractChanged/Object/UpstreamResult/…）→Superseded | — | 当前性原因清单 | ResultCurrentness | R1 |
| V-42 | 历史结果当前性 | CON-02 | HEAD 前进 | 查询历史 | payload 不变、保留历史证据；不作正式命中 | — | Superseded 语义 | 归档/当前性 | R1 |
| V-43 | 项目切换后迟到结果 | TASK-03/AT-10 | 跨项目迟到事件 | 接纳 | 五元组核对拒绝；绝不写入新项目 results/ | — | — | RunRegistry | R1 |
| V-44 | 显示单位切换不改 SI 真值 | KIN-12/AT-27 | 单位切换 | 重评对照 | 结果身份不变、SI 值不变；仅投影变化 | — | — | 身份对比 | R1 |
| V-45 | 求解配置变化致失效 | CON-05/AT-27 | config.dyn 变更 | 重评 | sliceId 变化→Superseded（ConfigurationChanged）；不进 inputBaselineId（比较基准不变语义） | — | 失效原因 | sliceId 对比 | R1 |
| V-46 | 共享策略变化不由 dynamics 私自覆盖 | ARC-05/§4.1 | 策略变更 | 重评对照 | dynamics 结果**不失效**（无 policy 依赖）；策略身份经快照承载；零本地阈值副本 | 越界→review 打回 | — | 静态扫描＋身份对比 | R1 |
| V-47 | selection 消费工作点与 drivetrain 结果一致 | SEL-05/AT-19 关联 | 全链 | 三方对照 | selection 所见=drivetrain 映射产物（dynamics 只供关节侧） | — | — | 三方身份链 | R1 |
| V-48 | reporting 保留限定语 | RPT-05/DYN-06 | 估算/数据不足结果 | 报告渲染 | estimated/data-insufficient 限定语保留不弱化；包络合并标注"（呈现方式）" | — | 章节投影 | 报告限定语 | R1 |

### 11.3 AT 归属说明（任务指令九项）

AT-05→V-07/41/45；AT-07→V-01/13/22/23/24/25（动力学链）；AT-10→V-43；AT-11→V-37；AT-19→**dynamics 无碰撞入口，本域仅以"零碰撞代码＋零 policy 消费"的静态断言参与**（V-46；三入口一致性由 kinematics/trajectory/optimization 契约测试承载）；AT-27→V-44/45；AT-34→V-35（取消/进度；优化消费链归 optimization 域验收——dynamics 不拥有优化控制）；AT-37→V-04/05；AT-38→V-31/33（R2）。

### 11.4 替身边界声明

- TrajectorySourcePort 替身/runtime 视图替身/FaultInterceptor：只验证端口、身份传播、错误路径——不证明 RNEA 数值正确性（V-01 解析算例承担）、不证明 RobWorkSim 仿真正确性（V-13 黄金对照承担）、不证明真实快照兼容性（V-07/41 归档链路承担）。
- 正动力学仿真的引擎替身：仅用于协议/取消/异常路径测试；一致性结论（Passed）必须**真实引擎**产出（RP-STATE-4 同款边界）——引擎选型经 WP-17-T05 落位锁定（P-DYN-7，§6.1 登记注）＝同源 RNEA＋固定步长 RK4（`rk4-fixed`），Passed 结论由该真实引擎产出并经解析黄金算例对照；RobWorkSim 物理引擎（rwsim::simulator::PhysicsEngine 工厂）经实测不落位（§1.2 T05 登记注⑤四条依据），后续批次若引入须先本卡增量修订并同步契约测试词表。

### 11.5 Windows GUI 测试流程（只设计；本次不启动 GUI）

- 遵循 AGENTS.md：VS x64 Developer Environment；`$env:QT_QPA_PLATFORM='windows'`；一次只启动一个 GUI 可执行文件；不用 offscreen；绝对路径。
- WP-17-T09 落位时的手动点验流程（设计）：①集成模式构建零错误→②启动 `sdurws_ird_dynamics_app`（或装配后宿主）→③黄金项目→动力学页可见、命令面板可达 dynamics 命令→④发起评估任务→进度/取消→⑤曲线联动/峰值定位→⑥时刻回放（零修订断言）→⑦截图留痕入 `traceability/builds/wp17-t09/`。
- **当前 dynamics 无 GUI 目标：本卡不声明任何 GUI 测试已存在或已通过。**

---

## 12. 阶段 C 实现任务拆分（对齐 DTB §2.18/§2.19/§2.24）

| 任务 | 内容 | 本卡依据 | 前置 | 关键验收/禁止（DTB 原文口径） |
| --- | --- | --- | --- | --- |
| WP-17-T01 | 编写 dynamics 单元任务卡 | **本文即交付物** | WP-06/WP-05 各卡 | 卡含 RNEA 评估器、输出序列与包络统计、正动力学一致性、可信等级、多工况、任务拆分；**禁止：DYN-04 映射不自行实现（归 drivetrain）** |
| WP-17-T02 | 构建落位：占位转真实库＋插件目标 | §3.2 | T01、WP-03-T01 | `ird/dynamics/CMakeLists.txt` 新建；计算库（链 rwsim 零 Qt 基线）＋`_plugin`；双模式构建零错误；二分结构扫描通过；DYN-\* 码注册随批 |
| WP-17-T03 | 实现 RNEA 逆动力学评估器 | §5/§10.1 | T02、WP-06-T08/T09（DWC/重力投影） | 二连杆解析算例＋静态重力矩（含倒挂 AT-37 动力侧）用例通过；**禁止：关节侧结果与候选传动无关（DYN-04）** |
| WP-17-T04 | 实现输出序列与峰值/RMS 包络 | §4.4/§7/§10.3-5 | T03 | 峰值窗与所在段报告、RMS 含驻留用例通过；**禁止：力矩/力类型化混用（转动 N·m/移动 N）** |
| WP-17-T05 | 实现正动力学一致性检查 | §6/§10.2 | T03 | 响应一致性与异常检测用例通过 |
| WP-17-T06 | 实现数据不足降级与可信来源标记 | §5.5/§9.4/§10.6 | T03；evidence EvidenceItem 承载 | 缺失走 DataInsufficient 降级并列入缺失清单；不包装成精确结论用例通过 |
| WP-17-T07 | 实现多工况与包络合并 | §8.3/§10.4 | T04 | 包络合并不替代必验工况覆盖用例通过 |
| WP-17-T08 | 实现曲线联动与峰值定位/回放数据 | §9.5 | T04、WP-10 | 联动与回放不产生修订用例通过 |
| WP-17-T09 | 实现 dynamics 插件界面 | §9.5/§10.7 | T03~T08、WP-10-T08（登记缺口 P-DYN-8） | 界面链路用例通过；**禁止：插件零计算逻辑**；GUI 手动验证通道留痕（§11.5） |
| WP-17-T10 | 契约测试与动力学黄金数据集 | §11 | T03~T09、WP-02 | `dynamics/test/*`＋`testdata/golden/dyn-*`；AT-07（完整循环包络/降级/多工况不漏验）；全部用例通过并留痕 |
| WP-18-T03（外部依赖） | DriveTrainMappingEvaluator 唯一实现 | §8.2 | WP-18-T02、WP-05-T10（③端口） | **归 drivetrain**：映射黄金数据（双向效率/反射惯量/摩擦不重复计入）；常矩阵 C 下与对角传动比等价验证；**禁止与壳体质量重复计入转子惯量**——dynamics 侧仅交付 JointSideSeriesPack 消费契约 |
| WP-23（横切关联） | T02 交互响应/T03 取消时序/T04 大规模分页/T09 确定性复现 | §9.1/§10.0 | WP-23-T01 | 各域配合基准执行（P95≤200 ms 交互、2 s/10 s 取消、确定性复现抽样）——dynamics 提供可基准化评估边界，不拥有基准判定 |

**任务边界声明**：DYN-04 的映射实现归 drivetrain（WP-18-T03），不是 dynamics；MDL-21 耦合矩阵的建模和统一映射属于 R2/阶段 D 扩展（WP-18-T05/WP-13-T18）；dynamics 在 R1 只准备稳定的关节侧输出和 drivetrain 消费端口；不提前实现 OPT-D；不把阶段 C 的动力学评估器实现写成 R2 联合优化已经完成。

---

## 13. 后续阶段承接与接口交接清单

### 13.1 交接给 drivetrain（WP-18-T03/T05）

- 关节侧广义力序列（类型化：转动 N·m/移动 N）；位置/速度/加速度序列；传动配置引用（driveTrainDesignRef）；工具和负载惯量（刚体物性、无转子项混入）；工况和任务循环引用；传动映射版本引用位；R2 耦合矩阵内容身份引用位（R1 恒空、零消费代码）。
- 交接形态：JointSideSeriesPack（§4.4 DTO；Handoff.hpp 公共头）＋归档结果引用；**调用点归 selection/宿主编排**（D-DYN-5）。

### 13.2 交接给 selection（WP-19；卡未产出）

- 电机侧工作点：**经 drivetrain 提供**（dynamics 不直接供）——selection 消费链＝JointSideSeriesPack＋传动配置→DriveTrainMappingEvaluator→工作点/效率/反射惯量/惯量比/能量分项/四象限。
- dynamics 直接供给：峰值、RMS、功率和能量（关节侧统计）；负载惯量（物性引用）；工况包络（DynamicsEnvelope）；传动映射诊断（随对端）；数据不足和估算来源标记（DYN-06 素材）。
- SEL-10 回填闭环：drivetrain-design 对象 cv 变化→selection 链复算；dynamics 关节侧结果不失效（§9.3）。

### 13.3 交接给 optimization（R2/OPT-D；卡未产出）

- R1 OPT-B 不消费完整动力学作为退出硬约束（§15.0 权威范围：阶段 B 仅静态硬约束）。
- R2 OPT-D 将 dynamics evaluator 作为轨迹—动力—器件联合内层（经 ③端口组合）——dynamics 不实现候选搜索、Pareto、鲁棒性或误淘汰审计。
- dynamics 评估边界义务（本卡已保证）：可批量（工况级 caseSubset 分批＋ResultBatch）、可取消（§9.1.5）、可缓存兼容（sliceId+mode+契约版本+Profile——judgeCacheHit FullHit 语义）、可恢复（工况级检查点）。
- R2 并行/检查点/暂停继续/内存节流/规模化由 execution/optimization 承担——dynamics 只需按 §9.1.3 能力位配合（supportsPause R2 届时重声明）。

### 13.4 交接给 reporting（v0.16 已产出）

- 动力学曲线、峰值和 RMS、工况包络、功率和能量、数据不足、估算值、正动力学一致性结果、轨迹段定位、传动交接结果（引用位）、正式报告限定语（RPT-05 不弱化）。
- 注册面：IReportSectionProvider→`dynamics-envelope` 章节（§9.6）；C 级前置＝Completed envelope 可产出。

---

## 14. 需求—设计—验证追踪矩阵

| 需求/AT | 设计章节 | 验证行（§11） | 任务（WP-17/WP-18） |
| --- | --- | --- | --- |
| DYN-01 | §5 | V-01/02/03 | T03 |
| DYN-02 | §5.3-5.5 | V-03/04/06/08/09/10/11 | T03/T06 |
| DYN-03 | §4.4/§7 | V-20/21/22/23 | T04 |
| DYN-04 | §8.2/§13.1 | V-28/29/30/31/34（实现归 WP-18-T03/T05） | 交接契约 T03/T10；映射 WP-18-T03 |
| DYN-05 | §6 | V-13/14/15/16 | T05 |
| DYN-06 | §5.5/§9.4 | V-09/10/48 | T06 |
| DYN-07 | §8.1/§8.3 | V-24/25/26/27 | T07 |
| DYN-08 | §9.5 | GUI 流程（§11.5）＋零修订断言 | T08/T09 |
| MDL-06/14/16 | §4.1/§5.3/§5.5 | V-09/12 | T03/T06 |
| MDL-21/AT-38 | §8.2.4 | V-31/32/33 | R2（WP-18-T05） |
| MDL-22/AT-37 | §4.1/§5.3 | V-04/05 | T03 |
| TRJ-01～08（输入面） | §4.3 | V-15/18/19/41 | T03 |
| CON-01～06 | §4.1/§9.2 | V-07/41/42/43/44/45 | T03～T10 |
| EVI-01/02 | §8.4/§8.1 | V-24/25/26/40 | T07/T10 |
| TASK-01～03 | §9.1 | V-35/36/37/38/39/43 | T03 起 |
| SEL-05/10 | §8.2/§9.3 | V-29/47 | 交接契约（实现归 selection） |
| OPT-03/05/06/07 | §13.3 | （OPT-D 启用时复用 V-40 语义） | 边界登记 |
| NFR-COR-01/02/03 | §5/§6/§10.0 | V-01/02/03/11/17 | T03/T10 |
| NFR-PERF-01/02/03 | §9.1 | V-35/36＋WP-23 基准 | T02 起 |
| NFR-REL-02/03 | §9.1.5 | V-37/38 | T10 |
| AT-05 | §9.2 | V-07/41/45 | T10 |
| AT-07 | 全链 | V-01/13/22/23/24/25 | T10 |
| AT-10/AT-11 | §9.1.5 | V-43/V-37 | T10 |
| AT-19 | §4.1/§11.3 | V-46（零碰撞静态断言） | T02（扫描） |
| AT-27 | §4.5/§4.2 | V-44/45 | T10 |
| AT-34 | §9.1 | V-35（dynamics 观测点；优化控制归 optimization） | T08/T10 |
| AT-37 | §5.3 | V-04/05 | T03 |
| AT-38 | §8.2.4 | V-31/33 | R2 |
| 附录 D 6/7/8/9 项 | §4.5/§5.3/§10.0 | V-03/11/12＋并行归约声明 | T03/T10 |

---

## 15. 设计决策、风险、待裁决项和变更记录

### 15.1 设计决策登记（D-DYN-x）

| # | 决策 | 依据与理由 |
| --- | --- | --- |
| D-DYN-1 | 单评估键 `dyn-rnea-analysis`（RNEA＋统计＋可选 FD 一致性），FD 一致性作为建议证据项由 config.dyn.forwardCheck.mode 控制 | 表 4 dyn 行为一个证据域；FD 为建议项（缺失不阻断）；单键使缓存/覆盖/当前性口径唯一 |
| D-DYN-2 | RNEA 本域自实现（递归牛顿—欧拉），输入取自 RobWorkSim 刚体模型（DWC 视图）；正动力学用 RobWorkSim 场景 | DYN-01 原文"基于 RobWorkSim 刚体模型和递归牛顿—欧拉"；RobWork 不提供成品 RNEA 评估器——自实现＋解析算例对照（NFR-COR-01） |
| D-DYN-3 | 重力投影唯一消费 `gravityBase()`（基座系），全卡零二次旋转 | MDL-22/AT-37 单一变换；runtime RT-BW-4 消费端拦截先例 |
| D-DYN-4 | 类型化广义力＝存储 SI double＋jointType 标签，API 边界出 core::Torque/Force 强类型 | core"Torque 与 Force 不可互赋"（DYN-03 类型化）＋10⁵ 样本级性能；强类型不进热路径存储 |
| D-DYN-5 | dynamics 评估器**不内嵌调用** drivetrain 映射；交接＝JointSideSeriesPack 数据契约＋调用点归 selection/宿主编排 | 表 4 dyn Profile 无电机侧证据项（D-14）；映射结果须绑定传动配置独立切片身份（CON-05）；③端口嵌套调用缺注入点；不影响"经唯一映射生成电机侧工作点"交付（selection 链承接） |
| D-DYN-6 | 负载 com/inertia 缺失的保守估算规则（com→安装点；惯量→点质量）＋强制 estimated 标记 | DYN-06 允许估算但禁止包装精确；mass 为必填、com/inertia 可选（requirements 结构）；数值行为入黄金算例 |
| D-DYN-7 | 摩擦 sgn₀(0)=0（零速库仑项为零；静摩擦不在 R1 模型） | 避免自设平滑阈值（附录 D 无此容差）；确定性、可黄金对照；模型边界如实登记 |
| D-DYN-8 | 峰值持续时间窗＝峰值样本连续覆盖区间（样本级精确），不引入比例阈值 | DYN-03 要求窗但无数值依据；比例阈值属自设数值；窗容差仅在黄金算例声明（测试对照口径） |
| D-DYN-9 | RMS 时间加权（梯形积分）含驻留；禁样本平均 | DYN-03"完整任务循环（含驻留）"；非均匀采样下样本平均失真——口径钉死＋黄金对照 |
| D-DYN-10 | 不设 `sdurws_ird_dynamics_worker`；共享 worker＋工况级检查点（Batch 粒度） | execution.md D-16/ARCH §4.1/DTB §5.1 产品交付路径；WP-17-T02 输出列无 worker（§3.3 差异登记） |

### 15.2 待裁决项（P-DYN-x；格式：编号｜问题｜影响｜来源｜建议裁决者｜状态）

| # | 问题 | 影响 | 来源 | 建议裁决者 | 状态 |
| --- | --- | --- | --- | --- | --- |
| P-DYN-1 | **ITrajectorySourcePort 适配器实现落位**：归档结果读取端口的适配器归 L5 装配还是 execution/project 提供读取面（与 P-KIN-2/P-TRJ-3 同源） | T03 开工前置（上游轨迹读取） | 本卡 §10.8 | evidence＋execution 所有者会签（trajectory 参与） | 登记 |
| P-DYN-2 | **selection 卡未产出**：JointSideSeriesPack 消费协议无对端确认 | 交接契约冻结悬空（T10 契约测试的对端面） | 本卡 §9.3；DTB §2.19/§2.20 | selection 详设所有者（WP-19-T01） | 登记（上游缺失） |
| P-DYN-3 | **外部力/力矩输入通道未定义**：DYN-02 含外力，但 REQ-04 工况结构无外力字段 | ExternalWrench R1 恒 NotApplicable——外力动力学场景无法正式配置 | REQUIREMENTS DYN-02 vs REQ-04 | 需求所有者（需求变更补充结构或确认 R1 无外力输入通道） | 登记 |
| P-DYN-4 | **急停/保持设计工况模板结构未定义**：DYN-07 提及，requirements 卡无对应工况模板字段 | 设计工况构造归 requirements；dynamics 只按通用工况评估 | REQUIREMENTS DYN-07；requirements.md §4.5 | 需求所有者＋requirements 详设所有者 | 登记 |
| P-DYN-5 | **drivetrain 卡未产出**：DriveTrainMappingEvaluator 契约未冻结（含 O-07 runtime 视图消费形态） | JointSideSeriesPack 对端契约基线暂以 DYN-04/ARCH §7.10 原文为准；T03 交接面验收依赖 WP-18-T03 | DTB §2.19（O-07 在 WP-18-T01 裁决）；本卡 §8.2 | drivetrain 详设所有者（WP-18-T01） | 登记（上游缺失） |
| P-DYN-6 | **能量量纲无 core UnitToken**：QuantityKind 无 Energy、R1 单位表无 J | 能量以 SI double＋字段注释承载；强类型化/显示换算需 core 增量修订 | 本卡 §4.5；core.md §4.4 | core 详设所有者（低风险增量） | 登记 |
| P-DYN-7 | **正动力学 RobWorkSim 引擎类选型**：rwsim 仿真引擎具体类/API 与确定性表现需 T05 落位时实测锁定（本卡不虚构类名） | forwardCheck.integratorToken 词表值；黄金对照容差声明 | 本卡 §4.2/§10.2 | dynamics 实现任务（T05）验证后回登本卡 | **已落位锁定（2026-10-08，WP-17-T05）**：本域确定性引擎＝同源 RNEA＋固定步长 RK4，integratorToken 词表锁定值 `rk4-fixed`（§4.2 回填）；RobWorkSim 物理引擎不落位（四条实测依据——ODE_DIR-NOTFOUND/Bullet 动态插件机制/负载注入＝CM-0 违约/迭代求解器漂移与确定性行冲突，详见 §6.1 登记注与 §1.2 T05 登记注⑤）；契约测试词表静态钉扎（ForwardDynamicsBoundaryContractTest） |
| P-DYN-8 | **ui 卡登记缺口**：WP-17-T09 消费 WP-10-T08 公共件未登记；动力学回放与 Playback 衔接无冻结接口 | T09 装配断链风险（F-518 教训同款） | 本卡 §9.5；ui.md §13/§14.1 | ui 详设所有者＋WP-17 双向增量登记 | 登记 |
| P-DYN-9 | **runtimeAbsoluteTolerance 无功率/能量/惯量/质量默认**（nullopt） | 这些量纲不得引入产品侧相对校验（附录 D C7）；统计校验只能测试对照 | core.md §5.5；附录 D C7 | 需求所有者（如需运行校验走需求变更） | 登记（口径约束） |
| P-DYN-10 | **曲线数据导出格式**：DYN-08 曲线导出格式未在需求/io 登记（trajectory 先例 P-TRJ-5 同款） | 导出为用户可见契约 | 本卡 §9.5；io.md 零登记 | 需求所有者（确认详设定义或需求变更） | 登记 |
| P-DYN-11 | **上游基线漂移**：ARCHITECTURE v0.13 Draft；core/evidence/runtime/execution/project/diagnostics/ui/requirements/kinematics/trajectory 各卡未冻结 | 本卡消费签名存在 diff 风险；冻结后按影响面增量同步（evidence §9 冻结基线除外） | 各卡 P-x-7 同源 | 各对端所有者 | 持续 |
| P-DYN-12 | **需求投影解码机制确认**（req-condition-set 解码口径）：同 P-TRJ-12，随 kinematics 先例落地 | T03 输入组装实现 | 本卡 §4.1；requirements.md §9.5 | requirements＋kinematics 所有者（dynamics 跟随） | 登记 |

### 15.3 风险登记

| # | 风险 | 缓解 |
| --- | --- | --- |
| R-DYN-1 | RNEA 逐样本性能（10⁵ 样本×7 关节×多工况）不满足响应性 | 工况级并行（threadCount 进身份）＋工况级分批/检查点＋sampleDecimation 抽取；基准归 WP-23 |
| R-DYN-2 | RobWorkSim 正动力学引擎的确定性与步长敏感性 | 引擎选型与容差在 T05 实测锁定（P-DYN-7）；黄金算例按附录 D 第 9 项声明对照容差；发散/异常检测兜底 |
| R-DYN-3 | 负载事件时间线与轨迹段对齐误差（事件落在采样间隙） | §5.4 保守边界规则＋黄金算例锁定；payloadVariantIndex 随样本可审计 |
| R-DYN-4 | 上游轨迹/卡片契约漂移（P-DYN-1/5/11） | 消费面集中登记（§2.4/§10）；UpstreamResult 身份机制使漂移表现为 Superseded 而非错算 |
| R-DYN-5 | 估算物性被下游误读为精确 | 三层来源标记（缺失/估算/外部验证）＋限定语强制（reporting 词表）＋黄金用例 V-09/10/48 |

### 15.4 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-10-06 | 首版草案（WP-17-T01 交付物）：依据 REQUIREMENTS v1.16 与 ARCHITECTURE v0.13 完成 16 节详细设计——输入版本与代码落位实测（仅 README 占位；drivetrain/selection/optimization 卡缺失如实登记）、R1/R2 边界与拥有/消费/不拥有表、目标布局（不设专属 worker 的差异登记）、输入快照与数据模型（8 类型＋JointSideSeriesPack；单位表；能量量纲缺口登记）、RNEA 逆动力学（分项图；摩擦 sgn₀ 约定；负载事件变体；DYN-06 三层降级；耦合链防御）、正动力学一致性（时序图；分析配置容差来源）、峰值/RMS/包络/功率能量（时间加权含驻留；峰值窗样本级定义；百分位非 R1 明示）、多工况与 drivetrain/evidence 交接（dyn Profile 六项；不内嵌映射调用决策）、execution/selection/reporting/ui 协作（共享 worker、工况级检查点、15 条 DYN-\* 拟注册码、零修订命令集）、公共接口 7＋1 端口（含不新增接口论证）、52 行故障注入矩阵（AT-05/07/10/11/19/27/34/37/38 归属）、T01～T10＋WP-18-T03＋WP-23 任务拆分、四单元交接清单、追踪矩阵、10 项设计决策、12 项待裁决、5 项风险、交付前自审。纯文档交付：未执行任何构建/测试/GUI 测试/验收 |
| v0.2 | 2026-10-06 | WP-17-T02 构建落位登记：头表"构建落位"行刷新＋§1.2 追加落位登记注（CMake/STATIC 五登记边＋L1 基线/插件最小可注册/两测试目标同批〔任务契约 acceptance 1 口径取代 §3.2 CMake 行"T10 起"时点注〕/DYN-* 15 码登记表 kinematics 注册函数形态/DYN-FD-NUMERIC-ANOMALY 不进本批的登记面/README 修正 C-1 闭环——业务实现随 T03+）；文档语义零变化（设计面不含 T02 实现新增决策——DYN-* 登记表为卡面既有登记值的物化；PUBLIC 链接形态差异为 DTB §5.4 偏差登记面，§1.2 注已载） |
| v0.3 | 2026-10-07 | WP-17-T03 实现落位登记：§1.2 追加 T03 实现登记注（DynTypes/Errors/InverseDynamics 三头＋src/InverseDynamics.cpp 落位；八项实现口径微调登记——request 域内 DTO 与单工况粒度〔T10/T07 承接面〕、提取单源 CanonicalModel、SPD 复检归建模侧、非有限样本级处置、重力唯一入口 g_base 向量、轨迹数据面注入〔P-DYN-1 规避〕、冒烟逐元素纪律）；§16.3 测试行刷新（_test 23/23＋_contract_test 11/11——DynRnea 14＋DynRneaBoundary 2 用例真实执行，含三处缺陷检出与返工记录：测试期望式修正、composeEndEffector 重复合成缺陷修复〔flangeBase 基线〕、非有限样本级处置修正）；文档其余章节语义零变化（§5/§10.1 设计面与本实现一致——差异均为实现口径细化登记非语义变更） |
| v0.4 | 2026-10-07 | WP-17-T04 实现落位登记：§1.2 追加 T04 实现登记注（DynTypes 表尾增列 PeakRecord/DynamicsSeries/PowerEnergySummary/OperatingConditionResult 四类型＋SeriesBuilder/Envelope/PowerEnergy 三公共头＋三实现 TU；九项实现口径微调登记——具体类形态与 T04/T07 分界〔mergeEnvelope 归 T07〕、峰值窗关节时间序位等值 run、正/反向峰值符号语义与 powerPeak 幅值合并位、RMS 梯形时间加权含驻留与非 Ok 行跳行口径、includesDwell 统计器口径、SeriesIdentity 字段级落地〔planned/validitySeed〕、倒退 fail-fast 与重复行 finalize 标记分解、canonical 编码唯一实现点、P-DYN-9 零运行容差）；§16.3 测试行刷新（_test 39/39——DynSeries 6＋DynEnvelope 4＋DynPowerEnergy 3＋DynStatsIntegration 3 计 16 新用例真实执行＋_contract_test 11/11 无新增；两处测试侧期望式修正如实登记、产品实现零缺陷检出）；表头"构建落位"行同步；文档其余章节（§4.4/§7/§10.3-5 设计面）与本实现一致——差异均为实现口径细化登记非语义变更 |
| v0.5 | 2026-10-08 | WP-17-T05 实现落位登记：§1.2 追加 T05 实现登记注（ForwardDynamics 公共头＋src/ForwardDynamics.cpp 落位＋DiagCodes 表尾增行 16 DYN-FD-NUMERIC-ANOMALY；八项实现口径微调登记——具体类形态、τ_ref 检查器内化〔共源结构性强制〕、tOfMaxErr 增列双时刻、DynConfig.hpp 随 T10、**P-DYN-7 引擎选型落位锁定 rk4-fixed**〔RobWorkSim 物理引擎不落位四条实测依据〕、变体一致性区间起点时刻口径、发散界 1e10 与奇异判据 1e-14×‖M‖∞ 实现常量、**T03 既有缺陷修复**〔物性缺失惯量分支未置零——Mat3 默认单位阵静默充当 1 kg·m² 惯量，连杆缺失分支与点质量路径显式零张量，T03/T04 回归 39 用例全绿〕）；§4.2 integratorToken/toleranceQ 行回填；§6.1 追加引擎选型登记注；§9.4 表尾增行 16；§11.4 替身边界措辞同步（"真实引擎"＝落位选型引擎）；§15.2 P-DYN-7 状态更新为已落位锁定；§16.3 测试行刷新（_test 50/50——DynForward 11 新用例〔解析黄金/四态/发散/奇异/取消/确定性/负载事件共源/fail-fast 十项〕＋_contract_test 13/13——DynForwardBoundary 2 新用例〔P-DYN-7 词表静态钉扎＋契约形态编译期核对〕；双模式构建零错误＋gtest XML 留痕 traceability/gtest-reports/wp-17-t05/）；表头"构建落位"行同步；文档其余章节（§6.2/§6.3/§10.2 设计面）与本实现一致——差异均为实现口径细化登记非语义变更 |
| v0.6 | 2026-10-08 | WP-17-T06 实现落位登记：§1.2 追加 T06 实现登记注（DynTypes 表尾增列 DynamicsEvidence＋EvidenceBuilder 公共头＋src/EvidenceBuilder.cpp 落位；八项实现口径微调登记——具体类形态、证据项/缺失/限定语三访问器交付面、dynProfile 登记面物化〔真实注册用例〕、数据不足证据状态映射〔摩擦缺失/估算→③ Invalid＋稳定码诊断、外部验证→Unverified、Invalid>Unverified 保守优先序〕、建议项四态映射、trustQualifiers 限定语单点〔词表编译期钉扎〕、artifactDigest canonical 摘要投影〔IRDDYEV1〕、fail-fast 四面＋未装配项显式 Missing）；§16.3 测试行刷新（_test 58/58——DynEvidence 8 新用例〔Profile 对账＋真实注册/干净链全绿双校验/V-09 摩擦缺失降级/V-10 估算限定语/缺失清单全量不短路/FD 四态映射/fail-fast/Empty〕＋_contract_test 15/15——DynEvidenceBoundary 2 新用例〔itemId 词表静态钉扎＋限定语 token 冻结与三层映射〕；测试侧悬垂指针缺陷修正一处、产品实现零缺陷检出；双模式构建零错误＋ird_gates 零命中＋gtest XML 留痕 traceability/gtest-reports/wp-17-t06/）；表头"构建落位"行同步；文档其余章节（§5.5/§8.4/§9.6/§10.6 设计面）与本实现一致——差异均为实现口径细化登记非语义变更 |
| v0.7 | 2026-10-08 | WP-17-T07 实现落位登记：§1.2 追加 T07 实现登记注（DynTypes 表尾增列 DynamicsEnvelope＋Envelope.hpp/cpp 增列 mergeEnvelope〔§10.4 第三方法——T04/T07 分界兑现〕＋src/CanonicalDigest.hpp 私有摘要头抽取〔DigestWriter 单点共用〕；六项实现口径微调登记——§10.4 草案形参 CaseCoverageSnapshot 以冻结 RequiredCaseSet 承载〔acceptance 2 分母口径〕、包络摘要 magic IRDDYVE1 与编码字段序〔五峰值记录槽 token 序〕、合并规则〔conditionId 序处理/逐 token 严格大于/powerPeak 幅值合并/rmsTau 跨工况 max 且 NaN 不参与/envelopeComplete 传播〕、coversAllMandatory 呈现参考口径〔分母 enabled∧mandatory／分子 completeness≠Empty／空分母平凡完备——包络不替代覆盖〕、fail-fast 装配契约七面〔peaks 须产自统计口径唯一实现点〕、Empty 语义）；§16.3 测试行刷新（_test 65/65——DynEnvelopeMerge 7 新用例〔黄金合并归因/统一 RNEA 多负载＋保持端到端/Partial 完整性传播〔V-26〕/覆盖分母口径〔V-25/EVI-02〕/Empty 语义/RMS NaN/fail-fast〕＋_contract_test 17/17——DynEnvelopeBoundary 2 新用例〔签名与 magic 编译期钉扎＋EVI-02 分母口径行为契约〕；测试侧期望式错误一处修正〔工具对关节 1 力臂漏计 L₁——首跑 FAILED 修正后全绿〕、产品实现零缺陷检出；双模式构建零错误＋ird_gates 零命中＋gtest XML 留痕 traceability/gtest-reports/wp-17-t07/）；表头"构建落位"行同步；文档其余章节（§7.4/§8.3/§10.4 设计面）与本实现一致——差异均为实现口径细化登记非语义变更 |
| v0.8 | 2026-10-09 | WP-17-T08 实现落位登记：§1.2 追加 T08 实现登记注（新增公共头 Replay.hpp〔DYN-08 数据面：曲线联动投影 JointCurves/CurveProjection＋回放数据 ReplayJointState/ReplayFrame/ReplayData/ReplaySample＋三投影器 DynamicsCurveProjector/DynamicsReplayProjector/DynamicsPeakLocator〕与 Commands.hpp〔§10.7 落地面：五命令 token 词表＋ReplaySessionContract 零修订会话契约＋CommandPayload/CommandOutcome＋DynamicsCommandHandler 具体类〕＋src/Replay.cpp/src/Commands.cpp 两实现 TU；八项实现口径微调登记——布局表外增头 Replay.hpp、具体类形态〔T03～T06 先例〕、View3DSessionPoseContract 四常量的同值结构承载〔R-1 禁 include ui 头——两侧契约测试以同一文档出处字面冻结对账〕、命令拒绝走 outcome 非异常轨、峰值定位消费 computePeaks 行集零重算〔结构对账防错位〕、投影行序防御 fail-fast 与 builder 标记通道的分解、回放数据随 payload 不入 dyn Profile〔D-14〕、插值查表口径〔段取左帧/逐关节独立插值/越界不外推〕）；§16.3 测试行刷新（_test 83/83——DynCurveProjector 4＋DynReplayData 3＋DynReplaySampler 4＋DynPeakLocate 3＋DynCommands 3＋DynDyn08Chain 1 计 18 新用例真实执行＋_contract_test 20/20——DynCommandsBoundary 3 新用例〔词表与四值 static_assert 钉扎/命令面零计算词表扫描/受理逐位恒等〕；首跑 8 FAILED 复盘——**产品缺陷一处**〔投影器同关节行连续假设与 (t, jointIndex) 交错行序矛盾，修复为逐行查重＋二分定位〕＋**测试侧错误一处**〔倒退用例抛出在 EXPECT_THROW 外〕，修正后全绿）；表头"构建落位"行同步；文档其余章节（§9.5/§10.7 设计面）与本实现一致——差异均为实现口径细化登记非语义变更 |

---

## 16. 交付前自审和未执行事项

### 16.1 逐项自审（任务指令 23 项检查清单；结论限于**文档设计层面**自查，不构成实现测试或正式验收）

| # | 检查项 | 结论 | 依据 |
| --- | --- | --- | --- |
| 1 | 是否把 R1 阶段 C 与 R2 阶段 D 混淆 | **否**——§2.3 R1/R2 边界表逐行标注；MDL-21/OPT-D/暂停/规模化均标 R2 | §2.3/§12/§13 |
| 2 | 是否将 dynamics 错写成传动映射实现者 | **否**——N4/O14：零映射代码；§8.2.3 不内嵌调用；D-DYN-5；V-28 静态断言 | §2.4/§8.2 |
| 3 | 是否重复 runtime 的模型编译或基座—世界变换 | **否**——N1；gravityBase 唯一消费（D-DYN-3）；V-05 | §2.4/§5.1 |
| 4 | 是否重复 trajectory 的轨迹规划或时间参数化 | **否**——N2；只消费不可变轨迹（§4.3），q/q̇/q̈ 不重算 | §4.3 |
| 5 | 是否重复 policy 的碰撞算法或阈值 | **否**——N3；零碰撞、零 policy 依赖条目（V-46） | §2.4/§4.1 |
| 6 | 是否重复 evidence 的工程判定、当前性和证据等级 | **否**——N7；DynamicsValidity 仅值对象（§4.4 明文约束）；判定归 aggregateVerdict | §4.4/§8.4 |
| 7 | 是否重复 execution 的任务调度、worker、检查点或缓存淘汰 | **否**——N8；不设专属 worker（§3.3）；检查点经 ICheckpointCoordinator | §3.3/§9.1 |
| 8 | 是否把估算质量、质心、惯量当作精确动力学证据 | **否**——三层来源标记（§5.5）＋estimated 强制（D-DYN-6）＋限定语（V-48） | §5.5/§9.6 |
| 9 | 是否把单个工况失败或局部超限直接判为整机工程不可行 | **否**——§5.6"单工况失败不推出全部工况失败"；样本级/工况级/任务级分层；判定归 evidence | §5.6/§4.6 |
| 10 | 是否遗漏工况覆盖矩阵 | **否**——§8.1 覆盖矩阵（EVI-02 五态）＋V-24/25/26 | §8.1 |
| 11 | 是否遗漏完整任务循环和驻留对 RMS 的影响 | **否**——§7.3 时间加权含驻留（D-DYN-9）＋V-23 | §7.3 |
| 12 | 是否在缺少时间参数时伪造功率、能量或节拍 | **否**——§4.6/§7.6：字段显式无效＋缺失清单；V-15 | §4.6/§7.6 |
| 13 | 是否混淆关节侧力矩和电机侧力矩 | **否**——V-28 payload 零电机侧字段；selection 只经 drivetrain 取工作点（§9.3） | §8.2/§9.3 |
| 14 | 是否复制 drivetrain 的虚功映射 | **否**——§8.2.2 第 1 条逐字禁止；V-31 静态扫描 | §8.2 |
| 15 | 是否对 R1 耦合链进行静默对角化 | **否**——§5.6 防御拒绝＋上游门禁；V-32 | §5.6/§8.2.4 |
| 16 | 是否在 R2 设计中错误地让 dynamics 拥有耦合矩阵 | **否**——R2 仍零接触（C 消费全链在 drivetrain；dynamics 只透传引用位） | §8.2.4 |
| 17 | 是否允许显示单位改变 SI 真值或输入身份 | **否**——§4.5 纯投影；V-44 | §4.5 |
| 18 | 是否在取消、失败、崩溃或数据不足后发布完整动力学结果 | **否**——§9.1.5（接纳第⑦步拒绝）＋§4.6 部分结果纪律；V-39 | §9.1/§4.6 |
| 19 | 是否新增未经上游批准的状态、阈值、证据等级或诊断码 | **否**——六状态正交全复用（TaskState/EvidenceItemStatus/EngineeringStatus/Currentness/ArchivePhase/DynamicsValidity 仅结果侧值对象）；FD 容差为分析配置来源（C7 允许）；DYN-\* 为拟注册清单（码值权威归 StableCodeRegistry）；峰值窗/sgn₀ 等定义为"无阈值化"设计 | §4.4/§9.4/§15.1 |
| 20 | 是否把 OPT-D、R2 性能规模化或鲁棒性分析提前写成阶段 C 已实现 | **否**——§13.3 明示 R1 不消费、R2 承接边界；§12 任务边界声明 | §13.3/§12 |
| 21 | 是否将 UI 计算逻辑写入 dynamics 插件 | **否**——§9.5 全命令零计算（提交/投影/查表）；§10.7 处理器硬边界 | §9.5/§10.7 |
| 22 | 是否把缺失的实际代码、CMake 或测试目标写成已完成 | **否**——§1.2 逐项实测（仅 README＋INTERFACE 占位）；§3.2 全部标记"目标设计"；§10 目标名标注"当前不存在" | §1.2/§3.2/§10 |
| 23 | 是否越权修改需求、架构或其他单元机制 | **否**——本卡零上游文件改动（§0 范围声明）；上游冲突全部登记待裁决（§15.2），未私裁 | §0/§15.2 |

### 16.2 上游冲突集中列出（任务指令要求格式）

| 冲突 | 依据/位置 | 对 dynamics 的影响 | 当前采用的保守设计 | 需要裁决者 | 不受影响部分 |
| --- | --- | --- | --- | --- | --- |
| 外力输入通道缺失 | DYN-02（含外力）vs REQ-04（工况无外力字段） | 外力动力学场景无法正式配置 | ExternalWrench 预留、恒 NotApplicable、零伪造 | 需求所有者（P-DYN-3） | RNEA 其余分项、摩擦、负载链 |
| drivetrain 契约缺失 | DYN-04/ARCH §7.10 有语义、units/drivetrain.md 不存在 | 交接包对端契约未冻结 | 以需求原文为契约基线＋引用位留白 | drivetrain 所有者（P-DYN-5） | 关节侧输出、统计、证据链 |
| 动力学 vs 策略变更失效口径 | CON-06 策略身份入快照 vs dynamics 零策略消费 | 策略变更是否失效动力学的口径 | 无 policy 依赖条目→不失效（AT-05 电机成本类比）；已登记口径依据 | evidence 所有者复核（P-DYN-11 关联） | 全部评估链 |
| 任务指令建议 worker 布局 | 指令建议 `sdurws_ird_dynamics_worker` vs execution.md D-16/DTB §5.1 唯一交付 worker | 目标布局差异 | 不设专属 worker（§3.3 差异登记＋触发重审条件） | 架构所有者（如需重审） | 共享 worker 全部协作 |

### 16.3 执行状态（WP-17-T02 落位后按真实结果刷新；v0.1 行为 T01 时点登记，历史如实保留于 §15.4）

- **构建**：**双模式已执行零错误**（WP-17-T02：集成模式四目标 sdurws_ird_dynamics/_plugin/_test/_contract_test Release 构建零错误零警告；独立冒烟全树配置＋构建退出 0——留痕 traceability/builds/wp17-t02/ 两份构建日志）。
- **测试**：`sdurws_ird_dynamics_test` **9/9 通过**（DynDiagCodes 5＋DynBuildRedLine 4）、`sdurws_ird_dynamics_contract_test` **9/9 通过**（DynBuildGraph 6＋DynPluginAssembly 3）——gtest XML＋ird-test-report.json 留痕同目录；期间契约测试曾真实检出插件面注释含词表符号字样的违例（PluginFaceHasZeroComputationSymbols 首跑 FAILED），修正注释措辞后复跑通过——失败能力与检出纪律有效。**WP-17-T03 起业务用例入列（2026-10-07）**：`sdurws_ird_dynamics_test` **23/23 通过**（DynDiagCodes 5＋DynBuildRedLine 4＋**DynRnea 14**——二连杆解析算例〔静态重力矩/倒挂 AT-37 符号翻转/工具负载合成/分项恒等式＋M·q̈ 与科氏解析对照/摩擦 sgn₀ 三分支/MDL-16 缺失降级/负载事件变体/外力恒零/维度与单调性等 fail-fast/非有限样本级标记/DYN-04 传动无关/取消语义〕）；`sdurws_ird_dynamics_contract_test` **11/11 通过**（DynBuildGraph 6＋DynPluginAssembly 3＋**DynRneaBoundary 2**——DYN-04 零传动映射消费符号＋include 白名单）；gtest XML 留痕 traceability/gtest-reports/wp-17-t03/。期间测试真实检出三处缺陷并返工：①测试期望式漏工具 L1 力臂/行索引错位（测试侧修正）；②composeEndEffector 以已合成值作基项导致变体重复合成（产品缺陷，改 flangeBase 只读基线修复）；③单关节非有限输入经耦合项污染他关节输出（产品缺陷，按 §4.6 改样本级 NonFiniteInput 处置）。**WP-17-T04 统计用例入列（2026-10-07）**：`sdurws_ird_dynamics_test` **39/39 通过**（DynDiagCodes 5＋DynBuildRedLine 4＋DynRnea 14＋**DynSeries 6**〔身份冻结与行序稳定排序/内容身份确定性与灵敏度/时间倒退 fail-fast/重复行 finalize 标记/身份缺失七项 fail-fast/Empty 语义与缺口计数〕＋**DynEnvelope 4**〔峰值平顶窗·孤立窗·段报告·token 序黄金/含驻留时间加权 RMS 解析对照与禁样本平均钉扎/非 Ok 行剔除/Empty 与退化显式 NaN〕＋**DynPowerEnergy 3**〔含驻留 E⁺/E⁻/E_net 黄金与 V-22 对照/制动段符号与多段等值窗/退化时间显式无效〕＋**DynStatsIntegration 3**〔RNEA→序列→统计端到端静态循环/Prismatic+Revolute 混合链类型化不混算黄金/OperatingConditionResult 聚合形态〕）；`sdurws_ird_dynamics_contract_test` **11/11 通过**（无新增——T04 零新跨单元边界，既有 BuildGraph/插件装配/DYN-04 边界契约继续覆盖）；gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-17-t04/ 与 traceability/builds/wp17-t04/。期间测试检出两处**测试侧期望式错误**并修正（产品实现无缺陷）：①构建器乱序用例的喂入顺序跨时刻构成时间倒退——被 addSample fail-fast 正确拒收（§10.3 前置行行为正确，乱序改限同刻度内）；②E⁺ 手算漏跳变段梯形项 ½(2+0)×1——修正后 E_net=3 J 与样本行 energyIntegralJ 末值对照一致（V-22 性质成立）。**WP-17-T05 正动力学一致性用例入列（2026-10-08）**：`sdurws_ird_dynamics_test` **50/50 通过**（DynDiagCodes 5＋DynBuildRedLine 4＋DynRnea 14＋DynSeries 6＋DynEnvelope 4＋DynPowerEnergy 3＋DynStatsIntegration 3＋**DynForward 11**〔解析黄金算例——伸直构型 q₂≡0 匀加速 τ_ref 恒定，RK4 二次多项式精确，误差<1e-12 双重对照/一致性超阈 Failed＋比较型素材/发散检出 DYN-FD-DIVERGED/质量阵奇异 DYN-FD-NUMERIC-ANOMALY/NotRun 三路径〔空/单样本/初始态非有限〕/NotApplicable Skip 短路/取消语义/确定性复现逐位/负载事件共源 Passed＋素材透传/fail-fast 十项〕）；`sdurws_ird_dynamics_contract_test` **13/13 通过**（DynBuildGraph 6＋DynPluginAssembly 3＋DynRneaBoundary 2＋**DynForwardBoundary 2**〔P-DYN-7 落位选型静态钉扎——正动力学面零 rwsim 物理引擎消费符号词表扫描＋公共契约形态编译期核对〕）；gtest XML 留痕 traceability/gtest-reports/wp-17-t05/。期间测试真实检出**产品缺陷一处并修复（T03 遗留）**：RNEA 物性提取对 NotProvided 惯量分支未显式置零——`Mat3` 默认单位阵静默充当 1 kg·m² 惯量（T03 降级用例只测标记面未测数值面故漏过），本任务零物性黄金算例暴露——连杆缺失分支与点质量路径修复为显式零张量后，T03/T04 既有 39 用例回归全绿＋本批 13 新用例全绿。**WP-17-T06 证据装配用例入列（2026-10-08）**：`sdurws_ird_dynamics_test` **58/58 通过**（DynDiagCodes 5＋DynBuildRedLine 4＋DynRnea 14＋DynSeries 6＋DynEnvelope 4＋DynPowerEnergy 3＋DynStatsIntegration 3＋DynForward 11＋**DynEvidence 8**〔dynProfile §8.4 表 4 对账＋注册期校验＋真实 EvidenceProfileRegistry 注册＋内容身份确定性/干净链四必需＋两建议全 Satisfied 且 validateEvidenceItems＋checkEvidenceCompleteness 双零问题、无限定语/摩擦缺失→provenance 项 Invalid＋DYN-FRICTION-MISSING＋对账 requiredGaps 恰含该项〔V-09〕/估算物性→estimated 限定语＋DYN-PROPERTY-DOWNGRADED 不包装精确〔V-10〕/缺失清单全量不短路〔Invalid＋两 Missing 三面并存〕/正动力学四态→证据五态映射〔Passed→Satisfied/Failed→Invalid＋DYN-FD 素材/Skip→NotApplicable 不计缺失/NotRun→Missing〕/fail-fast 契约〔重复注入/缺锚/工况不一致/身份缺失〕/Empty 序列与未 build 访问器〕）；`sdurws_ird_dynamics_contract_test` **15/15 通过**（DynBuildGraph 6＋DynPluginAssembly 3＋DynRneaBoundary 2＋DynForwardBoundary 2＋**DynEvidenceBoundary 2**〔dyn Profile 六项 itemId 词表编译期静态钉扎＋DYN-06 限定语 token 词表冻结与 trustQualifiers 三层映射契约〕）；gtest XML 留痕 traceability/gtest-reports/wp-17-t06/。期间测试检出**测试侧缺陷一处并修正**（用例在临时 vector 上取证据行指针——悬垂指针未定义行为），产品实现无缺陷检出。ird_gates 复跑零命中。**WP-17-T07 多工况与包络合并用例入列（2026-10-08）**：`sdurws_ird_dynamics_test` **65/65 通过**（DynDiagCodes 5＋DynBuildRedLine 4＋DynRnea 14＋DynSeries 6＋DynEnvelope 4＋DynPowerEnergy 3＋DynStatsIntegration 3＋DynForward 11＋DynEvidence 8＋**DynEnvelopeMerge 7**〔三工况手工黄金 max 合并与来源归因——并列 τ⁻ 按 conditionId 字典序取胜、逆序重合并内容身份逐位同／多负载＋保持型设计工况统一 RNEA 端到端〔P-DYN-4 无自设构造语义〕解析黄金／Partial 来源→envelopeComplete=false＋来源清单仍在＋覆盖呈现独立〔V-26〕／覆盖分母＝冻结 RequiredCaseSet〔V-25 漏验呈现/disabled Must 不参与/空分母平凡完备/纯 Should 完整包络不得声称覆盖——EVI-02〕／Empty 语义〔空集/全无 Ok 行/Empty 来源覆盖保守不计〕／RMS NaN 不参与 max 与全无效显式 NaN／fail-fast 装配契约七面〕）；`sdurws_ird_dynamics_contract_test` **17/17 通过**（DynBuildGraph 6＋DynPluginAssembly 3＋DynRneaBoundary 2＋DynForwardBoundary 2＋DynEvidenceBoundary 2＋**DynEnvelopeBoundary 2**〔mergeEnvelope 契约形态编译期核对——签名静态钉扎＋包络摘要 magic IRDDYVE1 词面冻结＋token 常量；EVI-02 覆盖分母口径行为契约——分母翻转→coversAllMandatory 翻转且内容身份随之改变／与 envelopeComplete 互相独立／空结果集 Empty 语义〕）；gtest XML 留痕 traceability/gtest-reports/wp-17-t07/。期间测试检出**测试侧期望式错误一处并修正**（多负载算例工具对关节 1 力臂漏计 L₁——首跑 FAILED，按 T03 用例 3 同式修正后复跑全绿），产品实现无缺陷检出。双模式构建零错误（集成模式 sdurws_ird_dynamics/_plugin/_test/_contract_test 四目标 Release 构建零错误＋独立冒烟全树配置与构建退出 0）＋ird_gates 零命中（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02）。§11 V-24/V-25/V-26 的 dynamics 统计面与 V-27 的空集保守呈现已随本批真实执行（覆盖矩阵本体与汇总保守判定归 evidence，随 T10 装配对账）；V-15～V-19、V-35～V-48（黄金数据集/取消超时崩溃链/GUI 等）仍为设计，**未执行**（随 T08～T10）。**WP-17-T08 DYN-08 数据面与命令面用例入列（2026-10-09）**：`sdurws_ird_dynamics_test` **83/83 通过**（DynDiagCodes 5＋DynBuildRedLine 4＋DynRnea 14＋DynSeries 6＋DynEnvelope 4＋DynPowerEnergy 3＋DynStatsIntegration 3＋DynForward 11＋DynEvidence 8＋DynEnvelopeMerge 7＋**DynCurveProjector 4**〔两关节五通道黄金投影逐字段直拷对账／非 Ok 行剔除＋nonOkCount 计数＋Partial 透传／行序违约 fail-fast〔构建器 addSample 拦截＋投影器重复行与手工倒退序列双路径 token 核对〕／Empty 显式〕＋**DynReplayData 3**〔帧重组黄金——帧 t/段定位键/帧内关节升序/五量直拷/严格递增／缺行帧 completeAllJoints=false 不截断／Empty 无帧〕＋**DynReplaySampler 4**〔帧点精确命中原值直拷／区间中点 α=0.5 线性插值逐点手算黄金＋段取左帧＋端点亦帧点／越界不外推＋NaN fail-fast／空数据集空态＋缺行帧逐关节独立插值单侧取值〕＋**DynPeakLocate 3**〔两关节全 6 token 定位与 computePeaks 行集逐位对账＋峰值三要素手工黄金抽查〔速度峰 4 rad/s@t=2 段 1／全负循环力矩正峰带符号 −5 N〕／无 Ok 行关节与空序列定位空态／token 越界与行集截断 fail-fast〕＋**DynCommands 3**〔五命令全受理四语义位零修订〔AT-04 机器断言〕＋表外与错配负载拒绝〔大小写敏感〕／零修订契约常量与词表字面冻结〔KIN-06/AT-04 同值对账〕〕＋**DynDyn08Chain 1**〔曲线投影→computePeaks→locate→sampleAt(tPeak)→replay-at 受理全链联动——"曲线游标跳转峰值时刻＋三维姿态同步"的领域侧演练，链尾零修订钉扎〕）；`sdurws_ird_dynamics_contract_test` **20/20 通过**（DynBuildGraph 6＋DynPluginAssembly 3＋DynRneaBoundary 2＋DynForwardBoundary 2＋DynEvidenceBoundary 2＋DynEnvelopeBoundary 2＋**DynCommandsBoundary 3**〔§9.5 五 token 词表与 kReplaySessionContract 四值编译期 static_assert 钉扎＋CommandOutcome 拒绝形态默认安全／命令面零动力学计算红线源码级词表扫描——Commands.hpp/.cpp 剥注释后零评估器/统计器/投影器消费符号〔§10.7 硬边界〕／受理结果与契约逐位恒等〕）；gtest XML 留痕 traceability/gtest-reports/wp-17-t08/。期间首跑 8 FAILED 复盘修正**缺陷两处**：①产品缺陷（本批新增代码）——投影器 Ok 关节收集与关节行推进按"同关节行连续"假设实现，与 (t, jointIndex) 交错行序矛盾（关节表膨胀且非首刻度行被跳过，黄金投影用例检出），修复为逐行查重插入＋升序排序＋逐行二分定位；②测试侧错误——倒退用例把构建器 addSample 抛出放在 EXPECT_THROW 外（构建器对倒退是 addSample 即抛，SeriesBuilder 契约），修正后补投影器对手工倒退序列的防御路径覆盖。双模式构建零错误（集成模式四目标 Release 增量构建＋独立冒烟全树构建退出 0）＋ird_gates 零命中（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02）。§11.5 GUI 手动点验通道仍归 T09（本批零 GUI 用例——领域数据面与命令面无 GUI 载体）。
- **GUI 测试**：未执行（T02 无 GUI 用例；插件界面与 GUI 手动验证通道随 WP-17-T09 按 §11.5 约定执行，一次一个可执行文件、不用 offscreen）。
- **正式验收**：未发起、未通过；本卡 Draft 待评审，WP-17-T02 实施验收按 acceptance-protocol.md 由独立上下文执行。
- **门禁**：**ird_gates 已执行零命中**（WP-17-T02：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中＋引擎自测 9 例按预期检出——留痕 integrate-build 日志）。
- **文档验证脚本**：validate-docs.ps1 已在 WP-17-T02 交付会话执行（PASS：20 units/12 trace entries/307 task files）。
- 后续落位路径：WP-17-T03（RNEA 逆动力学评估器）起按 §12 任务表推进，全部验收以真实执行留痕为准。
