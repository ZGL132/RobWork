# 工业机械臂设计软件 · trajectory 单元详细设计（阶段 C）

> 2026-10-06 新建（WP-16-T01 承接）。本文是 trajectory 单元的唯一详细设计：依据 REQUIREMENTS TRJ-01～08 与 ARCHITECTURE §3.1/§4/§7.2/§7.5/§7.6 的分配，把输入快照与身份、轨迹数据模型、PTP、笛卡尔直线段与 IK 连续性、路径连接与连续性、RobWork 规划器与 policy 交接、平滑与 TRJ-04 复检协议、时间参数化与节拍、轨迹质量与 evidence 素材、execution/evidence/project/diagnostics/ui 协作、公共接口、插件界面边界、验证与故障注入矩阵、任务拆分与追踪矩阵写到可直接实现的深度。**本文只做详细设计：不实现产品源码、不创建 CMakeLists/插件/worker/测试文件、不修改需求/架构/其他单元卡、不恢复或复制 old/ 历史实现；不自行宣布 Accepted，不把本文自审写成实现测试或正式验收通过。**

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（2026-10-06，首版草案，WP-16-T01 交付物） |
| 日期 | 2026-10-06 |
| 状态 | **`Draft`**（待评审；DETAILED-DESIGN.md 单元状态表中"trajectory｜待产出"以本卡落盘为准，索引行同步由治理侧执行，本卡不代改） |
| 文档代号 | UNIT-TRJ |
| 单元 | trajectory（业务域单元，ARCHITECTURE §3.1：PTP/笛卡尔段/避障规划、简化平滑、时间参数化〔jerk/工艺速度可扩展维度〕、诊断定位；ARCHITECTURE §3.3 二分结构：零 Qt 计算库＋Qt Widgets 插件） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.13（`Draft`，2026-09-27，方案 B.1 宿主融合 SA-18）** |
| 协作输入 | `units/core.md` v0.11、`units/testkit.md` v0.9、`units/project.md` v0.19、`units/evidence.md` v1.3（**§9 评估器接口/注册表已冻结为设计基线**）、`units/runtime.md` v0.17、`units/policy.md` v0.15、`units/execution.md` v0.12、`units/diagnostics.md` v0.13、`units/ui.md` v1.76、`units/io.md` 卡头 v0.9、`units/modeling.md` v0.40、`units/requirements.md` v0.16、`units/kinematics.md` v0.15——除 evidence §9 冻结基线外**均为 Draft/Draft-Structured（未冻结）**；本卡消费的签名以各卡当前文本为基线，冻结后按影响面增量同步（§21.2 P-TRJ-11） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立，trajectory 行"待产出"）→ `units/*.md`。本文即 `units/trajectory.md`，按任务卡深度编写 |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/`——**当前磁盘实况：仅 `include/sdurws/ird/trajectory/README.md` 一个占位文件（2026-10-06 实测）**；主 CMakeLists 中 `sdurws_ird_trajectory` 为 INTERFACE 占位目标（IRD_MODULES 列表成员，无别名用例、无子目录 add_subdirectory）。真实库/插件/测试目标落位归 WP-16-T03（§4/§18） |
| 任务归属 | `development-task-breakdown.md`（v0.53）§2.17 WP-16（T01～T16；T14～T16 为 R2 预留）；本文 §18 只做单元内部任务拆分与排序，不重排 WP 编号 |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释（§2）、框架零源码修改（SA-02）、双模式构建与留痕、提交后推送、分支红线（唯一开发主线 `redesign-main`）；Windows Qt GUI 测试须 VS x64 环境、`QT_QPA_PLATFORM=windows`、绝对路径逐个启动、不用 offscreen |
| 实现口径 | **从头构建**（REQUIREMENTS v1.9/v1.11 确立）：一切实现按需求与本文新建，不继承、不恢复任何历史实现源码；`old/` 仅作功能范围对照（本卡不做旧代码逐文件对照——TRJ 家族在旧代码中无对应插件，需求附录 A.6 明文"轨迹/动力学/选型在旧代码中无插件 → 按本文 TRJ/DYN/SEL 家族需求全新实现"） |

---

## 目录

1. 文档信息、输入版本与缺失项（§1）
2. 需求承接与 R1/R2 边界（§2）
3. 职责边界、依赖与端口（§3，含拥有/消费/不拥有表）
4. 当前代码落位与未来目标布局（§4）
5. 输入快照、需求投影与身份（§5）
6. 轨迹数据模型（§6）
7. 关节空间 PTP（§7）
8. 笛卡尔直线段与 IK 分支连续性（§8）
9. 路径连接与连续性（§9）
10. RobWork 规划器与 policy 交接（§10）
11. 平滑与 TRJ-04 复检协议（§11）
12. 时间参数化与节拍（§12）
13. 轨迹质量与 evidence 素材（§13）
14. execution/evidence/project/diagnostics/ui 协作（§14）
15. 公共接口（§15）
16. 插件界面边界（§16）
17. 验证方案与故障注入矩阵（§17）
18. 阶段 C 任务拆分（§18）
19. 后续阶段与接口交接（§19）
20. 需求—设计—验证追踪矩阵（§20）
21. 设计决策、风险与待裁决项（§21，含变更记录 §21.5 与交付前自审 §21.6 指针）
22. 变更记录（§22）
23. 交付前自审（§23）

---

## 1. 文档信息、输入版本与缺失项

### 1.1 输入文件实测台账（2026-10-06 磁盘实况）

下表逐项记录本卡编写时每个输入文件的磁盘版本、文档状态与对 trajectory 设计的影响。"缺失"指文件或其声明的承载内容在磁盘/上游文档中不存在，本卡如实登记、不虚构。

| 输入文件 | 磁盘版本 | 状态 | 存在性 | 对本设计的影响 |
| --- | --- | --- | --- | --- |
| `RobWork/doc/industrial-robot-design/REQUIREMENTS.md` | v1.16（2026-09-09 签署；C1～C8 已留痕） | `Accepted` | 存在 | 唯一需求权威：TRJ-01～08、§8.1 评估语义（表 1～表 4）、附录 C P-06（未冻结）、附录 D 容差表全部承接（§2） |
| `RobWork/doc/industrial-robot-design/ARCHITECTURE.md` | v0.13（2026-09-27，SA-18 宿主融合） | `Draft`（待评审） | 存在 | 单元定位、六类端口、依赖红线 R-1～R-4、快照管线 §7.6、任务/取消协议 §4、宿主融合 §7.12 全部承接；其 `Draft` 状态构成 P-TRJ-11 基线漂移风险（§21.2） |
| `RobWork/doc/industrial-robot-design/DETAILED-DESIGN.md` | 无版本号（状态表最近登记 2026-09-22） | 索引文档 | 存在 | trajectory 行"待产出"——本卡落盘即为该行的消账依据；主 WP 列记为 WP-F（任务编号体系 WP-16 以 DTB 为准） |
| `RobWork/doc/industrial-robot-design/development-task-breakdown.md` | v0.53 | 治理文档 | 存在 | §2.17 WP-16-T01～T16 任务表、§3 追踪矩阵 TRJ 行、§4.3 O-29（P-06 未冻结）、§4.6 L1 基线表 `sdurw_pathplanners→trajectory` 登记、§5 执行约定（构建/DoD/分支）全部承接 |
| 仓库根 `AGENTS.md` | 无版本号 | 协作约定 | 存在 | 中文注释规范、分支红线、提交与推送、GUI 测试通道约定 |
| `units/core.md` | v0.11 | `Draft`（Draft-Structured） | 存在 | Identity/ContentIdentity/Digest/Provenance/Units/Compare（§4.5）/Evaluation 词表/DiagData/Events 契约的消费基线 |
| `units/testkit.md` | v0.9 | `Draft` | 存在 | 黄金数据集目录约定（`testdata/golden/<datasetId>/manifest.json`）、容差档案、契约测试基座（§17） |
| `units/project.md` | v0.19 | `Draft` | 存在 | ②查询端口 `IProjectQueryPort`、①命令端口 `ProjectCommandService::submit`、`IResultArchivePort` 归档端口（§14.3） |
| `units/evidence.md` | v1.3 | `Draft-Structured`（§9 已冻结为设计基线） | 存在 | AnalysisSnapshot/InputSlice/IEvaluationContext/IEngineeringEvaluator/EvaluatorRegistry/ResultEnvelope/ResultCurrentness/aggregateVerdict 全部契约（§5/§13/§14.2） |
| `units/runtime.md` | v0.17 | `Draft` | 存在 | RuntimeSnapshot/CanonicalModel/WorkCellConstView/IRuntimeModelView/RuntimeNameMap ⑥端口/`makeState` 每线程纪律；§8.1 明文"pathplanners 归 trajectory"（§10.1） |
| `units/policy.md` | v0.15 | `Draft` | 存在 | EngineeringPolicySet/ICollisionEvaluator/CollisionQuery（含 **PathSequence**）/pathParameters 约定/§13 C·trajectory 行（§10.3/§11.3）；P-POL-2/P-POL-11 直接影响复检（§21.2） |
| `units/execution.md` | v0.12 | `Draft` | 存在 | ITaskScheduler/TaskSubmission/九态状态机/检查点/通道 IRDCHN/1/RunRegistry/缓存治理（§14.1）；**无 trajectory 专属 worker 概念**（§4.3 明文承接共享 worker） |
| `units/diagnostics.md` | v0.13 | `Draft` | 存在 | StableCodeRegistry 注册协议（前缀即所有权，业务域前缀清单含 `TRJ`）、IDiagnosticFactory/DiagContext/ConfirmableFinding（§14.4）；**内置码表 87 码中零 TRJ 条目**（§14.4 如实登记） |
| `units/ui.md` | v1.76 | `Draft` | 存在 | IPluginUiRegistrar/PluginUiDescriptor/白名单（**trajectory token 已占位**）/StageId::TrajectoryDynamics/SelectionService/七态投影/CommandRegistry（§16）；**WP-16-T12 与 FormEditCommon 消费者在 ui 卡未登记**（§21.2 P-TRJ-8） |
| `units/io.md` | 卡头 v0.9（§15.5 至 v1.0） | `Draft` | 存在 | ICsvWriter/IAtomicFileWriter/ExportTarget 通用导出通道；**io 卡全文无"轨迹导出"登记**（grep 零命中）→ 标准轨迹导出格式归本卡定义（§15.13、§21.2 P-TRJ-5） |
| `units/modeling.md` | v0.40 | `Draft` | 存在 | 间接上游：工具定义（tool-definition 对象）、场景对象（MDL-15）经快照 objectClosure 消费；本卡不直接消费 modeling 接口 |
| `units/requirements.md` | v0.16 | `Draft` | 存在 | 需求冻结投影（§8.2）、TaskPoint/TaskSegment/WorkRegion/OperatingCondition/OrientationRule 类型、sequenceKey 拓扑、REQ-06 就绪语义、角色键 `req.points/req.conditions`（§5.2） |
| `units/kinematics.md` | v0.15 | `Draft` | 存在 | ③端口供给：评估键 `kin.task-point-ik`（单点/序列 IK 评估服务，TRJ-02/WP-16-T05 消费）、`kin.pose-metrics`；IkOutcome 五类结局枚举、AnalysisConfiguration 身份口径、Quick/Verified 纪律（§8/§10.2） |

### 1.2 代码落位实测与缺失项登记

| # | 检查对象 | 实测结果（2026-10-06） | 处置 |
| --- | --- | --- | --- |
| M-1 | `industrialrobot/trajectory/` 目录 | 存在，仅含 `include/sdurws/ird/trajectory/README.md`（"本目录为 trajectory 单元公共头保留位……源码随对应任务卡落地；本文件不参与编译"） | 如实登记；README 引用与现状一致，无过期引用 |
| M-2 | `trajectory/include/` 下除 README 外的公共头 | 不存在 | 全部公共头随 WP-16-T03 起落位（§4.2） |
| M-3 | `trajectory/src/`、`trajectory/plugin/`、`trajectory/test/`、`trajectory/worker/`、`trajectory/CMakeLists.txt` | 均不存在 | 本卡不创建（§0 范围声明）；worker 目录**永不创建**（§4.3） |
| M-4 | 主 `CMakeLists.txt` 中 trajectory 目标 | `sdurws_ird_trajectory` INTERFACE 占位（IRD_MODULES 循环注册 include 路径），无 `_test`/`_contract_test`/`_plugin`/`_app` 目标 | 转真实库归 WP-16-T03（DTB §6 自检清单 20/20 对应行） |
| M-5 | `units/trajectory.md`（本卡自身） | 编写前不存在 | 本卡即 WP-16-T01 交付物 |
| M-6 | diagnostics 内置码表 TRJ-\* 条目 | 不存在（87 码中零 TRJ 条目；TRJ 前缀已在业务域前缀清单预留，"阶段 B 起业务域注册须域卡在案"） | TRJ-\* 拟注册清单见 §14.4，随 WP-16-T03/T09 注册 |
| M-7 | ui 卡中 WP-16-T12 / trajectory 域消费者登记 | 不存在（WP-10-T08 域消费者清单未列 WP-16；Playback 无接口登记） | 登记 P-TRJ-8（§21.2），随 WP-16-T12/ui 卡增量修订双向补齐 |
| M-8 | io 卡中"标准轨迹导出"格式 | 不存在（io 仅通用 CSV/原子写通道） | 格式定义归本卡 §15.13；是否属需求语义变更登记 P-TRJ-5 |
| M-9 | 已执行的 trajectory 构建/测试/GUI 测试/验收记录 | **不存在**（本卡为纯文档交付，未执行任何构建、测试或验收） | 如实声明；§17 全部验证条目为设计，非执行结果 |
| M-10 | `old/` 历史轨迹实现 | DTB §4.1 O-01 登记 old/ 磁盘不存在；本卡不做旧代码对照 | 从头构建口径（表头"实现口径"行） |

### 1.3 本卡与上游文档的编号口径

- 需求 ID（TRJ/KIN/CON/EVI/TASK/MDL/OPT/NFR/AT/P-xx）一律回指 REQUIREMENTS v1.16，不重定义、不收窄、不扩大（AGENTS §6.1）。
- 任务编号 WP-16-Txx 沿用 DTB v0.53 §2.17；本卡内部不新增 WP 编号。
- 本卡自产编号：设计决策 D-TRJ-x（§21.1）、待裁决项 P-TRJ-x（§21.2）、稳定诊断码建议清单 TRJ-\*（§14.4）。三套编号仅在本卡辖域使用，跨卡引用须经对端卡登记。

---

## 2. 需求承接与 R1/R2 边界

### 2.1 需求条目承接（TRJ 家族全文承接，不改语义）

| 需求 | 语义要点（摘引 REQUIREMENTS v1.16） | 本卡承载章节 |
| --- | --- | --- |
| TRJ-01（P0/C/R1，WP-16） | 关节空间点到点路径＋任务点组成的有序作业序列 | §7（PTP）、§5.4（序列展开）、§18 T04 |
| TRJ-02（P0/C/R1） | 笛卡尔直线接近/撤离段＋沿途 IK 连续性检查 | §8、§9、§18 T05 |
| TRJ-03（P0/C/R1） | 调用 RobWork 规划器完成避障路径搜索，规划器与参数可配置 | §10、§18 T06 |
| TRJ-04（P0/C/R1） | 简化与平滑；平滑后按冻结的碰撞验证协议重新验证碰撞与关节限制（R2 复检协议要素＋R9 度量对象与细分终止；数值登记 P-06，阶段 C 启用前冻结） | §11、§18 T02/T07 |
| TRJ-05（P0/C/R1） | 至少加速度连续的运动律按关节速度/加速度限制时间参数化，输出总节拍与分段时间 | §12、§18 T08 |
| TRJ-06（P0/C/R1） | 对无路径、分支跳变、奇异邻域和限制超标给出具体段落与原因 | §7.6/§8.6/§9.5/§12.6、§14.4（诊断）、§18 T09 |
| TRJ-07（P1/C/R1，支持 WP-10） | 轨迹动画、曲线查看、路点局部重规划和标准轨迹导出 | §14.5/§16、§15.13、§18 T10 |
| TRJ-08（P1/C/R1） | 首版轨迹评估接口保持可扩展（为 jerk、工艺速度、连续工艺段预留评估维度），不创建空字段、空页面或占位模块；S1～S3 分期交付（V13-02：接口约束 R1，用户功能分期并独立验收） | §6.7、§12.7、§19.1、§18 T11 |
| TRJ-08-S1～S3（P1/R2） | jerk 上限约束／工艺速度约束／连续工艺轨迹段 | §19.1、§18 T14～T16（R2 延后，不写成阶段 C 已交付功能） |

### 2.2 跨家族需求承接

| 家族 | 条目 | 对 trajectory 的约束 | 承载章节 |
| --- | --- | --- | --- |
| KIN | KIN-01～05、KIN-13 | ③端口 IK 消费（`kin.task-point-ik`）；KIN-05 缺碰撞检测器→DataInsufficient 口径；AnalysisConfiguration 分层先例 | §8.3/§10.2 |
| CON | CON-01～06 | 快照评估、三态正交、外部资源固化、缓存契约、切片内容身份、策略/名称身份入切片 | §5、§14.2 |
| EVI | EVI-01/02 | 评估模式、RequiredEvidenceProfile（表 4 轨迹行）、必验工况覆盖、五级汇总 | §13、§14.2 |
| TASK | TASK-01～03 | 能力声明、合法组合、任务身份五元组与迟到结果 | §14.1 |
| MDL | MDL-06、MDL-14、MDL-22 | 权威模型编译链消费、RuntimeNameMap、基座—世界变换单一不变量（禁止下游二次旋转） | §5.3/§10.2 |
| OPT | OPT-03/06/07 | 阶段 C 轨迹域为优化（OPT-D）提供节拍指标与联合硬约束的评估供给面；本卡仅保证评估键/Profile/载荷契约可被优化域组合消费（③端口），不实现优化逻辑 | §19.2 |
| NFR | NFR-COR-01/02/03/05、NFR-PERF-01/02/03、NFR-REL-02/03、NFR-MNT-01/03/07 | 解析对照、确定性（同输入+版本+配置+种子→等价结果）、非有限数拒绝、三入口碰撞一致（AT-19）、响应性/取消 2 s/分批流式、worker 崩溃隔离、零 Qt、单一权威、无前缀拼接 | §12/§14/§15 各节 |
| AT | AT-04/05/06/09/10/11/19/27/34/37 | 见 §20 追踪矩阵与 §17 观测点 | §17/§20 |
| 附录 D | 第 1/2/3/10 项＋通用比较公式（C4/C7） | 第 10 项时间参数化限制校验容差＝相对 1×10⁻⁹（固定，AT-06）；运行校验 ε_abs 按量纲（速度 1×10⁻⁹、加速度 1×10⁻⁹、角度 1×10⁻¹²、长度 1×10⁻¹²）；比较一律逐元素经 `core::closeWithin`/`allCloseWithin` | §12.4 |
| 附录 C | P-06（未冻结） | 复检协议数值（关节/笛卡尔最大步长默认、细分上界、细分预算、代表点集规则）**未冻结不得启用复检**；负责人＝需求所有者＋WP-16；冻结动作＝WP-16-T02 | §11.2、§21.2 P-TRJ-1 |

### 2.3 R1 / R2 边界表（阶段 C 承诺面）

| 能力 | 批次 | 本卡处置 |
| --- | --- | --- |
| 关节空间 PTP＋任务点有序作业序列 | R1 | §7 全量设计 |
| 笛卡尔直线接近/撤离＋沿途 IK 连续性 | R1 | §8 全量设计 |
| RobWork 规划器避障搜索（规划器与参数可配置） | R1 | §10 全量设计 |
| 简化/平滑＋TRJ-04 复检协议 | R1（协议框架）；数值启用前置＝P-06 冻结 | §11；P-06 未冻结前 T07 不开工（DTB §2.17 依赖列） |
| 时间参数化（至少加速度连续）＋总节拍/分段时间 | R1 | §12 |
| 失败段诊断定位 | R1 | §7.6/§8.6/§9.5/§12.6/§14.4 |
| 动画/曲线/局部重规划/标准轨迹导出 | R1 | §14.5/§15.13/§16 |
| 评估接口可扩展（TRJ-08 接口约束） | R1 | §6.7/§12.7/§19.1 |
| jerk 限值（TRJ-08-S1） | **R2** | §19.1；不建 jerk 字段 |
| 工艺速度约束（TRJ-08-S2） | **R2** | §19.1；不建工艺速度字段 |
| 连续不停驻工艺段（TRJ-08-S3） | **R2** | §19.1；不建空段类型 |
| 笛卡尔圆弧段及其他几何路径 | **未立项**（需求无条目） | §21.2 P-TRJ-7；不写为已承诺能力 |
| 任务暂停/继续（Paused 态） | R2 承诺（ARCH §4.3）；R1 显式声明不支持并反馈 | §14.1.3 |

---

## 3. 职责边界、依赖与端口

### 3.1 拥有／消费／不拥有表

**trajectory 拥有**（本单元为唯一所有者的语义与实现）：

| # | 拥有项 | 依据 |
| --- | --- | --- |
| O1 | PTP 领域算法与关节空间路径段（多关节同步、构型选择、段间连接） | TRJ-01、runtime.md §8.1"轨迹规划归 trajectory" |
| O2 | 笛卡尔直线路径段（接近/撤离，位姿插值、采样） | TRJ-02、requirements.md §5.1/§8.2 消费约定 |
| O3 | 路径段连接与连续性检查（位置/姿态/速度/加速度连续、关节分支连续、时间边界） | TRJ-02/05、§9 |
| O4 | 关节空间与笛卡尔空间路径表达（数据模型） | §6 |
| O5 | 时间参数化（至少加速度连续运动律）与速度/加速度/节拍指标 | TRJ-05 |
| O6 | 简化和平滑算法 | TRJ-04 |
| O7 | 路径级复检编排（细分协议执行、PathSequence 调用组织、段级结论归集）——**碰撞判定本身不拥有**（policy 唯一） | TRJ-04、policy.md §2.2"细分协议与预算归 trajectory（P-06）；复检执行、段级 DataInsufficient 判定归 trajectory" |
| O8 | 轨迹质量指标（总时长、峰值、路径长度、裕量、采样完整性等） | §13 |
| O9 | 轨迹领域诊断素材与 TRJ-\* 稳定诊断码注册数据（TRJ 失败段定位的素材生产者；码值注册表归 diagnostics） | TRJ-06、diagnostics.md §4.5/§12.1 |
| O10 | trajectory evaluator 实现（评估键 `trj-sequence-plan`）与 trj RequiredEvidenceProfile 注册数据 | EVI-01、evidence.md §6.1"各域按表 4 逐行实例化 Profile"、§13 |
| O11 | trajectory 领域命令能力描述（会话命令与无修订约束的命令清单） | TRJ-07、ui.md §7 |
| O12 | 轨迹插件的领域呈现投影与交互适配（工作流页面、曲线视图入口、动画衔接的域侧数据面） | TRJ-07、ui.md §10.9/§16 |
| O13 | 标准轨迹导出的格式字段字典（导出执行经 io 通用通道） | TRJ-07、io.md C-6"字段字典由格式所有者注册" |
| O14 | RobWork 路径规划算法的消费面（`sdurw_pathplanners` 的规划器选型、参数化与调用编排）——**运行时对象（WorkCell/State）与碰撞仍经 runtime/policy** | TRJ-03、DTB §4.6 基线表、runtime.md §8.1 |

**trajectory 消费**（跨单元端口，全部经 §3.3 登记边）：

| 来源单元 | 消费内容 | 端口/机制 |
| --- | --- | --- |
| core | ObjectId/ContentIdentity/ContentVersion/TaskIdentity 身份类型；ValueProvenance/SourcedValue；Quantity/UnitToken 单位；Tolerance/closeWithin/allCloseWithin/runtimeAbsoluteTolerance；EvaluationMode/TaskOutcome/EngineeringStatus/TaskState 词表；DiagnosticRecord/DiagCode 数据契约；DomainEvent 接口 | core 公共头（L2 接口依赖） |
| runtime | 不可变 RuntimeSnapshot（shared_ptr 只读共享）；CanonicalModel（关节 bounds/maxVelocity/maxAcceleration、工具 tcpOffset、T_world_base）；WorkCellConstView/IRuntimeModelView（workCell()/makeState()/worldToBase()/nameResolver()）；RuntimeNameMap ⑥端口（resolveRuntimeName/resolveObjectId） | runtime 公共头＋运行时注入（P-KIN-2 同款宿主注入形态） |
| requirements | 冻结任务点投影（TaskPoint/PoseConstraint/ToleranceSpec/Must/Should）、接近/作业/撤离段（TaskSegment：ToolZ/ReferenceZ＋距离）、工况（OperatingCondition：负载事件 Grasp/Release/Dwell、processParams.targetCycleTimeS、demands）、sequenceKey 拓扑序、ReadinessSummary | 快照 objectClosure 中 req-\* 对象＋切片条目（requirements.md §8.2）；R-1：不直链 requirements 库 |
| kinematics | 单点/序列 IK 评估服务（`kin.task-point-ik`：多初值 IK、硬过滤、去重、五类结局）；位姿指标（`kin.pose-metrics`：Jacobian/奇异值/条件数） | ③评估器端口语义＋运行时注入端口（§15.4 IKinematicsComputePort；P-KIN-2 先例）；R-1：不直链 kinematics 库 |
| policy | 已解析 EngineeringPolicySet（只读、内容身份入切片）；CollisionEvaluationSession（SingleState/PathSequence/SampleSet 查询、findings 含对象 ID 对与 pathParameter、coverage 记录）；安全间距阈值唯一来源 | ④策略端口 IPolicyProvider＋会话句柄（§10.3）；R-5：不直链 sdurw_proximity |
| evidence | AnalysisSnapshot/InputSlice（切片身份＝缓存键）；IEvaluationContext（取消/进度/对象字节）；IEngineeringEvaluator/IEvaluatorFactory/EvaluatorRegistry；ResultEnvelope 构造（经调用侧 aggregateVerdict）；RequiredEvidenceProfile 注册；judgeCacheHit/judgeCheckpointCompatibility；ResultCurrentness | ③端口＋evidence 公共头（§13/§14.2） |
| execution | 任务提交 ITaskScheduler::submit(TaskSubmission)；取消/暂停/继续 ITaskController；检查点 ICheckpointCoordinator；共享 worker `sdurws_ird_execution_worker` 装配面；RunRegistry 五元组接纳；缓存治理 | execution 公共头（§14.1） |
| project | ②查询端口 IProjectQueryPort（组装输入读对象）；①命令端口（仅当未来出现需要修订的轨迹命令时）；results 归档经 execution→IResultArchivePort（trajectory 不直接写盘） | project 公共头（§14.3） |
| diagnostics | StableCodeRegistry（TRJ-\* 码注册）；IDiagnosticFactory/DiagContext；两级日志与脱敏设施 | diagnostics 公共头（§14.4） |
| ui | 宿主装配（IPluginUiRegistrar/PluginUiDescriptor）；命令注册（ICommandRegistry）；选择模型（SelectionService）；状态投影（DomainReadinessItem）；会话态设施（零修订契约） | ui 公共头＋L5 装配（§16） |
| io | 标准轨迹导出的 CSV 通道（ICsvWriter 方言标识/可逆转义）＋IAtomicFileWriter/ExportTarget | io 公共头（§15.13） |

**trajectory 不拥有**（防止职责越界——每条对应一个"禁止"）：

| # | 不拥有项 | 越界后果 |
| --- | --- | --- |
| N1 | RobotDesign、CanonicalModel 或模型编译（归 runtime） | 自建关节链表示违反 CM-0（runtime.md §4.1） |
| N2 | RuntimeNameMap 的生成和名称拼接/剥离（归 runtime，R-4 唯一例外点在 NameMap 实现） | NFR-MNT-07 红线命中 |
| N3 | FK/IK 实现（归 kinematics） | R-1 互链/重复实现 |
| N4 | 碰撞算法、碰撞阈值、安全间距数值与工程策略副本（归 policy） | ARC-05/NFR-COR-05 违约（AT-19 三入口不一致） |
| N5 | 任务点、区域、工况的数据定义和编辑规则（归 requirements） | 需求语义重定义 |
| N6 | AnalysisSnapshot/InputSlice/ResultEnvelope/ResultCurrentness 机制（归 evidence） | 汇总判定与当前性权威分裂 |
| N7 | 任务级 Feasible/EngineeringInfeasible/DataInsufficient 判定（归 evidence aggregateVerdict） | §5.6 状态语义越权 |
| N8 | 稳定诊断码注册表（归 diagnostics；trajectory 只交 CodeDescriptor 数据） | 码值权威分裂 |
| N9 | execution 的调度器、进程池、worker、取消状态机、检查点与缓存治理 | 本卡不创建 trajectory_worker（§4.3） |
| N10 | 项目文件读写、修订、锁和事务（归 project；trajectory 不直接写盘） | 磁盘写入唯一归属被破坏 |
| N11 | 动力学、传动、选型和优化算法（归 dynamics/drivetrain/selection/optimization） | R-1 互链 |
| N12 | 报告渲染（归 reporting） | 重复实现 |
| N13 | CSV/JSON 通用解析与转义（归 io；trajectory 只登记轨迹导出字段字典） | SA-12 单一权威破坏 |

### 3.2 依赖形态与红线自查（ARCH §3.5 口径）

- trajectory 计算库 `sdurws_ird_trajectory`（L2，零 Qt）：接口依赖 core/evidence/runtime/policy/execution/project/diagnostics/io/testkit(仅测试侧) 公共头；**业务域互链禁止（R-1）**——对 requirements/kinematics/ui 等 L4 同层单元零编译期依赖，跨域计算经"运行时注入端口＋③评估器端口语义"。
- R-2：只 include 其他单元 `include/sdurws/ird/<unit>/` 公共头，禁止任何私有头。
- R-3：计算库零 Qt；Qt Widgets 仅存在于 `sdurws_ird_trajectory_plugin`（界面目标）。
- R-4：全部对象定位经 runtime 名称端口，零前缀拼接/剥离。
- R-5（DTB §4.5 登记）：sdurw_proximity 直链禁止——碰撞唯一经 policy；**`sdurw_pathplanners` 为 DTB §4.6 基线表明确登记给 trajectory 的 L1 基线库**（规划算法面），其与 ARCH §5.1"只经 runtime/policy 适配层接触"措辞的张力登记为 P-TRJ-2（§21.2），本卡采用口径见 §10.1。
- L1 基线链接集（拟随 WP-16-T03 登记）：`sdurw_math`、`sdurw_kinematics`、`sdurw_models`、`sdurw_pathplanners`（均为 PRIVATE 或按 CMake 传播策略最小化），零 Qt。

### 3.3 端口协作总图

```
                         ┌────────────────────────── 主进程/L5 装配 ──────────────────────────┐
                         │                                                                    │
  ui（宿主/命令/选择/     │   trajectory 插件（_plugin，Qt）──零计算逻辑                        │
  七态投影/会话态）◄──────┤        │ 提交/取消/投影只读                                          │
                         │        ▼                                                            │
                         │   execution（ITaskScheduler/ITaskController/检查点/RunRegistry）     │
                         │        │ 派发（snapshot+slice+key 五元组）                           │
                         └────────┼────────────────────────────────────────────────────────────┘
                                  ▼
                  ┌── 共享工作进程 sdurws_ird_execution_worker ──────────────────────────┐
                  │  物化快照（runtime）→ 注册表实例化评估器（evidence）                   │
                  │  TrjSequencePlanEvaluator（本域，零 Qt）：                            │
                  │    读切片 → req 投影解码 → 序列展开                                   │
                  │      │                                                              │
                  │      ├─ 注入端口 IKinematicsComputePort ──(适配)── kinematics        │
                  │      │     （IIkSolver/IFkEvaluator：单点/序列 IK、位姿指标）         │
                  │      ├─ 注入端口 IPolicySessionPort ──────(适配)── policy            │
                  │      │     （CollisionEvaluationSession：SingleState/PathSequence）  │
                  │      ├─ runtime IRuntimeModelView/NameMap（快照只读视图/名称端口）    │
                  │      └─ sdurw_pathplanners（规划算法，碰撞约束经 policy 会话适配）    │
                  │  评估结果 EvaluationOutput → 通道回传 → RunRegistry 接纳 →            │
                  │  aggregateVerdict（evidence）→ ResultEnvelope → results/<run-id>/    │
                  └──────────────────────────────────────────────────────────────────────┘
```

---

## 4. 当前代码落位与未来目标布局

### 4.1 当前落位（如实登记，2026-10-06 实测）

| 对象 | 状态 |
| --- | --- |
| `trajectory/include/sdurws/ird/trajectory/README.md` | 唯一存在的文件（占位说明，不参与编译） |
| `sdurws_ird_trajectory` CMake 目标 | INTERFACE 占位（IRD_MODULES 循环），仅注册 include 路径 |
| `_plugin`/`_test`/`_contract_test`/`_app` 目标 | 均不存在（不预建空目标——DTB §5.1） |
| trajectory 专属 worker | **不存在，且本设计明确不创建**（§4.3） |
| 已执行构建/测试留痕 | 不存在 |

### 4.2 未来目标布局（全部为设计；落位动作归 WP-16-T03 及后续任务）

```
industrialrobot/trajectory/
├── CMakeLists.txt                    # WP-16-T03 落位：sdurws_ird_trajectory INTERFACE→STATIC（C++17、零 Qt）
│                                     #   PRIVATE 链 core/evidence/runtime/policy/execution/diagnostics/io
│                                     #   ＋L1 基线（sdurw_math/kinematics/models/pathplanners）
│                                     #   ＋注册 _test/_contract_test（T13 起）；配置期红线守卫（R-1/R-2/R-3/R-5）
├── include/sdurws/ird/trajectory/    # 公共头（命名空间 sdurws::ird::trajectory）
│   ├── README.md                     # 已存在
│   ├── TrjTypes.hpp                  # §6 数据模型（Trajectory/Segment/Waypoint/TimedSample/Quality/失败段记录）
│   ├── Errors.hpp                    # TrajectoryError token 集（errors/... 前缀，fail-fast 与域错误语义）
│   ├── DiagCodes.hpp                 # TRJ-* CodeDescriptor 拟注册清单数据（§14.4；注册表设施归 diagnostics）
│   ├── PlanConfig.hpp                # TrajectoryPlanConfiguration＋canonical 编码（进 config.trj 切片条目）
│   ├── Sequence.hpp                  # 任务序列展开（req 投影→段链；sequenceKey 拓扑）
│   ├── Ptp.hpp                       # §7 PTP 规划
│   ├── CartesianLine.hpp             # §8 笛卡尔直线段
│   ├── Continuity.hpp                # §9 连接与连续性检查
│   ├── Planner.hpp                   # §10 规划器适配面（IPathPlannerAdapter；sdurw_pathplanners 消费）
│   ├── Smooth.hpp                    # §11 简化/平滑
│   ├── Recheck.hpp                   # §11 复检协议编排（PathSequence 组织、预算、段级结论）
│   ├── TimeParam.hpp                 # §12 时间参数化与节拍
│   ├── Quality.hpp                   # §13 质量指标
│   ├── Evidence.hpp                  # trj Profile＋证据项装配（EvaluationOutput 组装）
│   ├── Evaluators.hpp                # TrjSequencePlanEvaluator＋工厂（评估键 trj-sequence-plan）
│   ├── KinematicsPort.hpp            # IKinematicsComputePort（注入端口，最小接口）
│   ├── Commands.hpp                  # 命令注册适配（会话命令、无修订）
│   └── Export.hpp                    # 标准轨迹导出格式字段字典（执行经 io 通道）
├── src/                              # 实现TU（同名单；canonical 编码实现、规划适配实现）
├── test/                             # WP-16-T13：单元测试（gtest）
├── contract_test/                    # WP-16-T13：跨单元契约测试（③端口/④端口/Profile/Envelope 组合）
├── plugin/                           # WP-16-T12：trajectory 插件界面（Qt Widgets；零计算逻辑）
└── app/                              # （T12 落位时按 DTB §5.1"插件 GUI 手动验证通道"建 _app harness；
                                      #   本卡不预建，不改变产品交付路径）
```

- **装配关系**（L5/worker 装配清单，按 execution.md §3.4/§13）：
  - 主进程：trajectory 插件经 `IPluginUiRegistrar::registerPluginUi` 注册（白名单 token `trajectory` 已占位于 ui.md §11.1）；域命令经 `WorkbenchContentDeps` 命令设施装配段注册（先例 WP-24-T03 T03b）。
  - worker：`sdurws_ird_execution_worker` 链接评估器装配清单——trajectory 的 `IEvaluatorFactory` 实现加入"主/worker 同清单"注册（execution.md §13 末行）；worker 装配属 L5 装配决策（execution.md D-16），不构成业务互链。
  - 注入端口（§15.4）：`IKinematicsComputePort`/`IPolicySessionPort` 的适配器实现属装配面（L5），本域只定义最小接口——与 kinematics `IKinRuntimeView` 注入先例同构（P-KIN-2 已放行形态）。

### 4.3 不创建 trajectory 专属 worker（明文承诺）

- 长时轨迹规划（避障搜索、逐点 IK、复检细分、时间参数化）默认全部经 execution 的**共享** `sdurws_ird_execution_worker` 运行（ARCH §4.1 明文列举"避障搜索"为 worker 承载任务；execution.md 全文无逐域 worker 划分）。
- worker 崩溃隔离（NFR-REL-02）、2 s/10 s 取消协议（NFR-PERF-02）、检查点保留（§14.1.4）均由 execution 统一供给，trajectory 只声明能力（§14.1.3）。
- 若未来出现需要独立进程的依据（如规划器独占地址空间、隔离第三方数值库崩溃域），必须先登记：依据、所有者、协议与待裁决项，走 DTB 增量修订——**本卡不预设、不创建 `trajectory_worker` 目标**。

---

## 5. 输入快照、需求投影与身份

### 5.1 输入冻结链（全部复用既有机制，不新建）

```
当前修订（HEAD 锚定）＋选择的工况集＋TrajectoryPlanConfiguration＋已解析 EngineeringPolicySet
   ▼ evidence SnapshotBuilder 组装（一次评估一份；(oid,cv) 经 IRevisionClosureSource 校验防混入）
AnalysisSnapshot（完整、不可变；objectClosure 含 req-*/robot-design/tool/scene 对象；
   policyRef{policyContentIdentity}、nameMapRef{nameMapContentIdentity}、snapshotId）
   ▼ 依赖声明 → InputSlice（evaluationKey=trj-sequence-plan、entries 按字典序、sliceId）
   ▼ execution 派发（worker 物化重建快照并核对身份）→ 评估
```

- **一个任务不得混用多个修订**：快照组装以修订闭包锚定（evidence.md §4.1.5 协议①），任务身份五元组绑定该修订；本域评估器只消费快照 objectClosure 内对象（切片 Object 条目子集校验强制）。
- **输入冻结时点**：切片在评估派发前冻结（evidence.md §4.2.3）；评估运行期间输入不可变（worker 物化字节重建，身份核对）。
- **历史轨迹绑定旧快照归档**：结果写入登记记录指定的 `results/<run-id>/`（原修订），不写当前 HEAD（execution.md §9.1 归档位置"不重新推导"）；HEAD 前进仅用于当前性判定（§14.2.4）。

### 5.2 需求投影（trajectory 消费面）

| 投影内容 | 类型（requirements.md v0.16） | trajectory 用途 |
| --- | --- | --- |
| 任务点集 | `req-point-set` 对象→TaskPoint[]：refFrame、tcpRef（Tool{oid,tcpKey}/DefaultTcp）、pose（PoseConstraint：受约束 DOF 位姿分量＋OrientationRule）、tolerance（位置 m/姿态 rad）、level（Must/Should）、enabled、sequenceKey、demands{collisionFreeRequired, minimumJointMargin} | 序列展开的目标位姿来源；Must/Should 违例判定输入；姿态规则解析值（REQ-09 解析随快照冻结） |
| 接近/作业/撤离段 | TaskPoint 内 TaskSegment{enabled, axis: ToolZ/ReferenceZ, distanceM}——approach 沿轴负向趋近、retract 沿轴正向离开（requirements.md §5.1 方向语义归 requirements） | 笛卡尔直线段几何（§8）；作业段＝任务点本身 |
| 工况集 | `req-condition-set`→OperatingCondition[]：environmentRefs、toolRefs、payloads（质量/质心/惯量）、events（Grasp/Release/Dwell{stationRef,durationS}）、processParams.targetCycleTimeS、demands、appliesTo | 参与碰撞几何（工具/负载/环境）上下文；驻留段时间；节拍目标（Should 口径）；必验工况覆盖矩阵（EVI-02）的 case 范围 |
| 必验冻结 | RequiredCaseSet（mandatory≡(level==Must)，requirements.md §6.2 P-EV-9） | 快照 caseSet——Verified 判定的覆盖矩阵分母 |
| 就绪语义 | ReadinessSummary{valid, invalidMustItems[]}（REQ-06） | 汇总①级"输入未完成"由 evidence 判定；本域在 Preview/Quick 中同样只消费有效条目 |

- **R-1 合规**：投影解码消费快照中的 req-\* 对象字节与 requirements.md 登记的 canonical schema（IRDREQO family，kCurrentRequirementFormatVersion{1,0}）；解码实现落位口径随 kinematics 既有消费先例（kinematics 已声明 `req.points` 依赖并解码同源对象），若 kinematics 落位采用 requirements 公共编解码面或域内自持解码，trajectory 跟随同一裁决——登记于 §21.2 P-TRJ-12（跨卡契约确认项）。
- **消费键**：本域评估器依赖声明含 `req.points`、`req.conditions`（角色键，requirements.md §8.4 典型声明方明文含 trajectory）。

### 5.3 起始/终止状态、TCP、设备、Frame、限位与速度/加速度约束

| 输入 | 来源 | 身份归属 | 说明 |
| --- | --- | --- | --- |
| 起始状态 | `TrajectoryPlanConfiguration.startStateRef`：{Home, Zero, TaskPoint(oid)} 三选一 | config.trj（进 sliceId） | **不从会话姿态读取**（KIN-06/AT-04：会话姿态不进计算身份）；Home/Zero 为权威模型字段（CanonicalModel 经编译链） |
| 终止状态 | 序列最后一个启用任务点（含其 retract 段收尾） | 由 req.points 决定 | 序列终点即必经状态（§5.6） |
| TCP/工具 | TaskPoint.tcpRef→ToolDefinition（tool 对象在闭包内）；CanonicalTool.tcpOffset（法兰→TCP） | tcp 对象条目（与 kinematics 同键 `tcp`） | 笛卡尔段 TCP 参考点选择随 TaskPoint；默认 DefaultTcp |
| 设备/Frame | CanonicalModel RobotChain（唯一 SE(3) 链）；T_world_base（MDL-22 单一不变量——trajectory 消费 worldToBase()，**禁止自行叠加安装旋转**，AT-37 契约） | model.robot-design 条目 | 坐标语义：路径点/连续变换在 T_world_base 之后的设备链上表达（runtime.md §6.4 trajectory 行原文） |
| 关节限位 | CanonicalJoint.bounds（Revolute/Prismatic 必填 qmin<qmax；显式设值纪律 RT-AD-2） | model 身份 | evaluationIntervals 评价区间语义随 kinematics 先例（快照评价区间） |
| 速度/加速度约束 | CanonicalJoint.maxVelocity/maxAcceleration（SourcedValue<double>，SI：rad/s、rad/s²；未显式设值＝+inf） | model 身份 | §12 限值校验唯一来源；本域不得私设默认限值（缺失＝+inf 语义由 runtime 显式设值决定，非本域补值） |
| 碰撞几何参与集 | policy CollisionScene 的 SceneObjectEntry（RobotLink/Tool/Payload/EnvironmentObject 角色）＋conditions.environmentRefs/payloads | policy.resolved＋collision-models 条目（Conditional←policy 启用） | **复检代表点集**对象＝该参与集（P-06 冻结其生成规则，§11.2） |

### 5.4 任务序列展开算法（TRJ-01"有序作业序列"的领域语义）

1. 输入：req-point-set 中 enabled 任务点（含各自 TaskSegment），按 `sequenceKey` 拓扑排序（requirements R7 无环校验前置；本域对残余环/悬空键给 TRJ-INPUT-INVALID 素材并终止展开——不自行修复顺序）。
2. 逐站展开为段链：`approach（若 enabled）→ 作业（任务点驻留/工艺动作）→ retract（若 enabled）`；站间为关节空间 PTP 连接段（§7）。
3. 驻留段：condition events 的 Dwell{stationRef, durationS} 映射为时间轴上的常值段（§9.4/§12.3）。
4. 展开产物＝**段计划（SegmentPlan）**：有序段列表 {段序号、空间类型、几何要素、来源任务点 ObjectId、约束}——只含几何与约束，不含时间（时间在 §12）。

### 5.5 求解配置：TrajectoryPlanConfiguration（设计基线，进运行身份与缓存身份）

| 字段（schemaVersion=1） | 类型/单位 | 语义与约束 |
| --- | --- | --- |
| `startStateRef` | enum{Home, Zero, TaskPoint}＋optional<ObjectId> | 起始状态选择（§5.3）；不得指向未启用任务点 |
| `plannerSelection` | PlannerSelection{family token, params（键值规范表）} | TRJ-03"规划器与参数可配置"的承载；family 词表当前仅登记实现选型（§10.2），扩展走本卡增量修订 |
| `planningSeed` | uint64（0 非法，随 KIN-13 seed 纪律——I-KIN-4 同款拒绝） | 随机化规划器/采样的确定性种子（NFR-COR-02） |
| `cartesianSampleStep` | double，m，>0 | 笛卡尔段 IK 连续性检查采样步长上限（分析配置类数值，进身份） |
| `ikContinuityThreshold` | double，rad（转动）/m（移动），>0 | 相邻采样点关节分支连续判定阈值（逐轴上界，附录 D 第 3 项同源量纲；取值登记黄金算例） |
| `smoothTolerance` | double（关节 rad／TCP m，双域各一），>0 | 简化/平滑几何保持容差（§11.4；进身份） |
| `timeParamMethod` | enum{quintic-spline-c2}（封闭词表，当前唯一值＝唯一实现） | §12 运动律选择；TRJ-08 扩展点（§6.7） |
| `limitsScaleFactor` | double，(0,1] | 关节速度/加速度限值使用比例（保守缩放；1.0＝全限值） |
| `dwellPolicy` | enum{HonorEvents, IgnoreDwell} | 驻留事件消费开关（预览快速评估可忽略驻留；Verified 必须 HonorEvents） |

- 身份路径（与 kinematics config.ik 同口径）：canonical 编码（实现 magic `IRDCFGTR1`，定长封闭布局、小端、f64 位模式）→`configDigest`＝SHA-256→切片 `config.trj` Configuration 条目→**进 sliceId、不进 inputBaselineId**（D-04 同款："求解配置改变不改变样本基准"；本域无采样基准概念，仅 sliceId）。
- **复检数值不在本配置内**：TRJ-04 步长/预算默认属 P-06（需求附录 C），调整通道＝工程策略（TRJ-04③⑤原文"可经工程策略调整"）；运行承载位＝EngineeringPolicySet 扩展字段（policy 卡增量修订，随 P-06 冻结）——本域经 `policy.resolved` 依赖消费，不私设默认值（§11.2）。

### 5.6 身份与"必经状态"界定（§5.6 语义的输入侧落点）

- 运行身份要素＝sliceId（已含：对象 cv、policy 内容身份、nameMap 身份、config.trj、评估键＋契约版本、模式、环境 token）＋任务五元组＋seed/线程（config 内）。**不得以整个项目修订号代替切片内容身份**（CON-05/AT-05：电机成本变更不失效轨迹；TCP 变更失效——evidence.md §5.3 失效矩阵轨迹行原文）。
- **必经状态（任务强制且不可选择的状态）**在轨迹域的枚举（供 MandatoryStateCollision 证明素材 subject 用）：
  1. 起始状态（startStateRef 解析出的构型）；
  2. 终止状态（序列终点构型）；
  3. 任务点构型（Must 任务点的选定 IK 构型——Must 点位姿不可绕行，其构型是序列的强制经过点）。
  - 中间路点、候选路径、可选构型**不是**必经状态（C8：候选路径碰撞仅淘汰该路径并触发重规划）。
- 采样计划进入身份：本域采样计划（笛卡尔段采样步长、复检细分计划）的确定性生成参数全部在 config.trj/policy 内，随切片冻结；复评不得增删更换采样（KIN-04/R8 冻结口径同源）。

---

## 6. 轨迹数据模型

### 6.1 数据形态六分类（先分类后建模——不可混用）

| 形态 | 定义 | 可变性 | 可否发布 |
| --- | --- | --- | --- |
| 离散路径（DiscretePath） | 有序路点序列（JointWaypoint/CartesianWaypoint），无时间、无参数化 | 域中间态（段构建期可变，构建完成即冻结） | 否 |
| 连续几何路径（GeometricPath） | 参数化几何（s∈[0,1]：关节空间线性段/笛卡尔直线段），无时间 | 域中间态 | 否 |
| 时间参数化轨迹（TimedTrajectory） | 几何路径＋时间律（样条系数或 TimedSample 序列） | 域中间态 | 否 |
| 分批轨迹（批次 DTO） | execution 通道 ResultBatch 片段（execution 视为不透明字节） | 通道语义 | 否（非 ResultEnvelope，evidence.md §7 明文） |
| 最终轨迹（Trajectory） | 完整、不可变、含身份与证据引用的轨迹对象，置于 DomainPayload 内 | **不可变**（构造后无 setter） | 仅当所在 ResultEnvelope outcome=Completed |
| 失败/取消/部分结果 | FailedSegmentRecord/取消标记/未完成段集合 | 随 EvaluationOutput 产出 | 否——Canceled/Failed/Interrupted 不进正式报告/可行集（TASK-02）、不作正式缓存命中（CON-04） |

### 6.2 核心类型设计（签名＝设计基线；实现任务允许按 DTB §5.4 微调并登记偏差）

```cpp
namespace sdurws::ird::trajectory {

/// 轨迹段空间类型（封闭词表；圆弧等扩展走需求变更，本卡不预留空值）。
enum class SegmentSpaceType { JointLinear, CartesianLine };

/// 路点种类（必经状态标记供证明素材引用；Via＝规划器/平滑引入的中间点）。
enum class WaypointKind { Start, TaskPoint, Via, Dwell, End };

/// 单个路点：关节空间或笛卡尔空间二选一（variant 承载；同一路点不得双域同时有效）。
struct Waypoint {
    WaypointKind kind;                       // 路点种类
    std::size_t  segmentIndex;               // 所属段序号（0 基，单调递增）
    std::optional<rw::math::Q> q;            // 关节路点：权威关节角，单位 rad（移动关节 m）；SI 真值
    std::optional<rw::math::Transform3D<double>> target; // 笛卡尔路点：TCP 目标位姿，基座系 {B}（T_world_base 之后），非世界系
    std::optional<core::ObjectId> sourceTaskPoint;  // 来源任务点对象（TaskPoint kind 必填；可追溯性 NFR-COR-04）
    std::optional<double> dwellDurationS;    // 驻留时长，单位 s（Dwell kind 必填，>0）
};

/// 单个轨迹段：几何路径＋段级约束与身份（时间参数化产物另存于 TimedTrajectory）。
struct TrajectorySegment {
    std::uint32_t        segmentIndex;       // 段序号（0 基，全轨迹单调）
    SegmentSpaceType     spaceType;          // 空间类型
    std::vector<Waypoint> waypoints;         // 段内路点（含段端点；至少 2 个）
    core::ObjectId       tcpRef;             // TCP/工具对象引用（ARC-04 对象 ID，禁止运行时名称直存）
    std::optional<core::ObjectId> frameRef;  // 参考系对象（TaskPoint.refFrame 解析产物；World 时 nullopt）
    SegmentConstraint    constraint;         // 段级约束（限值引用与采样计划，见下）
    core::ObjectId       sourceTaskPoint;    // 来源任务点（approach/work/retract 归属站）
    double               pathLengthJoint;    // 关节空间路径长度（各轴 |Δq| 之和），单位 rad·轴 或 m·轴 混合计量逐轴记录
    double               pathLengthTcp;      // TCP 参考点路径长度，单位 m（派生观察——仅 JointLinear 段填写并标记 derived）
};

/// 段级约束：限值只做"引用＋缩放系数"，绝不复制阈值（ARC-05：不持有影响计算的私有副本）。
struct SegmentConstraint {
    double limitsScaleFactor;                // 限值使用比例，(0,1]；真值来源＝CanonicalJoint.maxVelocity/maxAcceleration
    double cartesianSampleStep;              // 笛卡尔段采样步长上限，m（来自 config.trj）
    bool   collisionCheckApplicable;         // 碰撞检查适用性（policy 启用且参与几何非空时 true）
};

/// 时间采样点：时间参数化轨迹的离散表达（样条系数的导出视图，两者并存且同源）。
struct TimedSample {
    double t;                                // 时刻，单位 s，自轨迹起点起算，非递减
    rw::math::Q q;                           // 关节位置，SI（rad/m）
    rw::math::Q qd;                          // 关节速度，SI（rad/s 或 m/s，逐轴按关节类型）
    rw::math::Q qdd;                         // 关节加速度，SI（rad/s² 或 m/s²）
    std::optional<rw::math::Transform3D<double>> tcpPose; // TCP 位姿（基座系）——派生观察（FK 派生），
                                                          // 不冒充笛卡尔规划结果（§7.5）；JointLinear 段必标 derived
};

/// 时间参数化：运动律系数＋采样视图（运动律词表当前仅 quintic-spline-c2，见 §12）。
struct TimeParameterization {
    std::string motionLawToken;              // 封闭词表："quintic-spline-c2"（当前唯一实现值）
    std::vector<double> knotTimesS;          // 结点时刻，单位 s，严格递增，端点对齐段边界
    std::vector<TimedSample> samples;        // 统一采样视图（等步长或事件对齐，采样计划入身份）
    double totalDurationS;                   // 总时长，单位 s（= knotTimesS.back()，含全部驻留）
};

/// 轨迹质量指标（§13 全量口径；缺失项以 SourcedValue 四态显式标记，不伪造数值——NFR-COR-03）。
struct TrajectoryQuality {
    double totalDurationS;                   // 总节拍（含驻留），s
    std::vector<double> segmentDurationsS;   // 分段时长，s（与段序号对齐）
    rw::math::Q peakJointVelocity;           // 逐关节峰值速度，SI
    rw::math::Q peakJointAcceleration;       // 逐关节峰值加速度，SI
    std::vector<double> jointPathLengths;    // 逐关节路径长度
    double tcpPathLengthM;                   // TCP 路径长度（派生观察标记），m
    double orientationChangeRad;             // 姿态总变化量（逐段姿态差之和，rad）
    std::vector<SourcedValue<double>> constraintMargins; // 约束裕量（限值−峰值，逐关节逐量，SI）
    SourcedValue<double> minCollisionClearance;          // 碰撞余量——当前后端无距离能力时 NotApplicable（P-POL-11）
    double sampleCoverageRatio;              // 采样完整性＝已检样本/计划样本，[0,1]；0 分母时 NotApplicable
    RecheckBudgetUsage recheckBudget;        // 复检预算占用（§11.5；未执行复检时 NotProvided）
    bool dataComplete;                       // 数据完整性（false 时必须同时给缺失清单）
};

/// 失败段与局部诊断记录（TRJ-06 定位载体；随 EvaluationOutput.diagnostics 与 payload 出口）。
struct FailedSegmentRecord {
    std::uint32_t segmentIndex;              // 失败段序号；全轨迹级失败用特殊值 0xFFFFFFFF 并在 phase 说明
    std::string   phaseToken;                // 失败阶段：plan-ptp / plan-line / plan-avoid / smooth / recheck / time-param
    std::string   reasonToken;               // TRJ-* 稳定码建议（§14.4）；evidence/aggregate 侧语义按表 4
    std::optional<double> pathParameter;     // 段内定位参数 s∈[0,1]（可定位到点时必填）
    std::optional<core::ObjectId> subject;   // 绑定对象（路点/段来源任务点/碰撞对象对之一）
    std::string   cause;                     // 中文原因（ERR-01 字段）
    std::string   recommendedAction;         // 中文建议动作（ERR-01 字段）
    std::optional<ComparativeFields> comparison; // 比较型字段（超限类必填：实际值/期望值/单位）
};

/// 最终轨迹对象（DomainPayload 内容的域内形态；canonical 编码 magic IRDTRJ1）。
struct Trajectory {
    // —— 身份块（构造期一次冻结；全部入 payload digest）——
    core::ContentIdentity snapshotId;        // 绑定快照（CON-01）
    core::ContentIdentity sliceId;           // 绑定切片（缓存键/当前性判据，CON-05）
    core::ContentIdentity policyContentIdentity; // 已解析策略身份（CON-06）
    std::uint32_t evaluatorContractVersion;  // 本域评估器契约版本（=descriptor.contractVersion）
    std::string   algorithmVersion;          // 轨迹算法版本（本域实现版本 token，入身份）
    std::string   planConfigDigest;          // config.trj 摘要（§5.5）
    std::uint64_t planningSeed;              // 规划种子（确定性复现要素）
    std::string   robworkBaselineVersion;    // RobWork 基线锚（planner/后端版本进入身份，NFR-DEP-05 消费）
    std::string   kinematicsPortContractVersion; // 所消费 IK/FK 服务的契约版本（kin.task-point-ik 契约版本记录）
    // —— 内容块 ——
    std::vector<TrajectorySegment> segments; // 段序列（有序、segmentIndex 连续）
    TimeParameterization timeParam;          // 时间参数化（可能缺失——见 dataComplete 语义）
    TrajectoryQuality  quality;              // 质量指标
    std::vector<FailedSegmentRecord> failedSegments; // 失败段记录（成功轨迹应为空）
    // —— 证据引用块 ——
    std::vector<core::ObjectId> evidenceRefs; // 证据对象引用（证据一律引用 ObjectId——evidence.md §6.2）
};
} // namespace
```

### 6.3 字段级义务清单（用户 §6.4 要求逐项说明——与 6.2 结构对齐）

| 义务 | 落点 |
| --- | --- |
| 内容身份 | payload digest（DomainPayload.core::ContentIdentity，canonical 编码后由 evidence 计算）；Trajectory 身份块逐字段入编码 |
| 快照/切片身份 | snapshotId/sliceId 两字段显式携带；当前性判定消费 sliceId（§14.2.4） |
| 段序号 | segmentIndex 单调连续；诊断/复检/动画共用同一定位键 |
| 空间类型 | SegmentSpaceType 封闭词表（R1 两值；扩展走需求变更） |
| 几何表达 | Waypoint variant（关节/笛卡尔二选一）＋段级 GeometricPath 语义（§6.1）；TCP 位姿一律基座系（T_world_base 之后），**世界系表达只在派生观察层换算** |
| 时间 | TimeParameterization.knotTimesS/samples/totalDurationS，单位 s |
| 位置/速度/加速度 | TimedSample.q/qd/qdd，SI 真值，逐轴按关节类型定纲（rad 或 m） |
| 姿态与参考系 | target 为基座系 TCP 位姿；frameRef 为对象 ID（World 时显式 nullopt，不伪造默认） |
| 工具/TCP/Frame | tcpRef 必填（对象 ID）；工具运动学偏置消费 CanonicalTool.tcpOffset，本域不复算 |
| 来源任务点 | sourceTaskPoint（approach/work/retract 归属站；Via 点无来源） |
| 约束 | SegmentConstraint 只存"引用＋缩放系数＋采样步长"，零阈值副本 |
| 采样间隔 | samples 采样计划由 config.trj/policy 决定并进身份；相邻采样间隔记录于 RecheckBudgetUsage/采样计划摘要 |
| 算法版本 | algorithmVersion token（实现版本变更→新身份→结果失效） |
| policy 身份 | policyContentIdentity（快照 policyRef 透传） |
| kinematics 结果引用 | kinematicsPortContractVersion＋evidenceRefs（对上游 IK 证据的对象级引用） |
| 所有权 | Trajectory 值语义、深拷贝安全；payload canonical 字节归 execution 通道/evidence 归档；评估器不持有跨调用状态（stateless 评估器） |
| 生命周期 | 域中间态生命周期≤单次 evaluate 调用；最终轨迹随 payload 进入归档后由 results 存储持有 |
| 线程安全 | 数据类型并发只读安全；评估器实例 threadSafety=SingleThread（每 worker 每任务一实例——与 kinematics 落位口径一致），descriptor 声明 |
| 可变/不可变边界 | 中间态（DiscretePath/GeometricPath/TimedTrajectory）构建期可变、提交即冻结；Trajectory 构造后不可变（无 setter） |

### 6.4 与 runtime/policy/kinematics 类型的边界

- 关节向量用 `rw::math::Q`、位姿用 `rw::math::Transform3D<double>`（core.md §4.6 冻结读法：T_ab＝b 相对 a；本域位姿全部标注基座系）——不包装、不重定义。
- 关节角权威换算 `q_authoritative = q_zeroOffset + q_rw`（runtime.md §4.3.3）：**本域数据模型的 q 一律权威角**；与 RobWork 规划器/状态交互的换算在适配层单点执行（§10.4），不在数据模型内混存两套角。
- 限值/间距/阈值一律"引用"（SourcedValue/Constraint 引用模型与策略），本域零副本（ARC-05）。

### 6.5 R1 范围守护（TRJ-08 反向约束）

- **不建 jerk 字段**：TimedSample 无 jerk 成员；quality 无 jerk 指标；诊断无 jerk 码。TRJ-08-S1 启用时以 payload kindToken `trj.sequence-plan.v2`＋Profile 版本递进承载（§19.1）。
- **不建工艺速度字段**：SegmentConstraint 无工艺速度槽位；S2 启用时以段约束扩展版本承载。
- **不建连续工艺段类型**：SegmentSpaceType 无 ContinuousProcess 值；S3 启用时经需求变更＋本卡修订扩展词表。
- **不建空页面/占位模块**：插件界面（§16）只注册已实现的投影与命令；TRJ-08 的"可扩展"由证据项/Profile/词表的版本化机制承担（§6.6），不以空 UI 承诺。

### 6.6 扩展机制（TRJ-08 接口约束的设计承载）

1. **载荷版本化**：DomainPayload.kindToken=`trj.sequence-plan.v1`；v2 起（S1/S2/S3 任一启用）新 kindToken＋新 canonical schema，旧 payload 永久可读（不可变历史）。
2. **Profile 版本化**：trj RequiredEvidenceProfile 版本递进＝证据项集合显式变化（evidence.md §9.5 注册期验证），不新增"静默维度"。
3. **运动律词表**：timeParamMethod 封闭词表（当前单值）；新增值＝本卡增量修订＋实现＋黄金算例，四者同批落地。
4. **能力声明**：评估器 descriptor 的依赖声明与 Profile 引用即能力边界；优化域（OPT-D）经 ③端口按 descriptor 判断可组合性——无"空接口"。

### 6.7 失败/取消/部分结果的表达

- 失败段：FailedSegmentRecord（段内可定位时带 pathParameter）；整链失败时 segments 可为空但身份块仍完整（可追溯失败输入）。
- 取消：评估器返回 cancelled 语义（经 IEvaluationContext.cancellationRequested 观测）→ execution 以 Canceled envelope 收尾；**取消不是错误**（UX-03），不产错误诊断。
- 部分结果：不构造 Completed envelope；分批数据仅入通道 DTO 与诊断账户（execution.md §8.2"部分诊断缓存"），**永不作为正式缓存命中**（CON-04/TASK-02）。

---

## 7. 关节空间 PTP

### 7.1 PTP 与笛卡尔直线对比图（TRJ-01/02 两类段的能力分界）

```
            关节空间 PTP 段（SegmentSpaceType::JointLinear）      笛卡尔直线段（CartesianLine）
几何空间     关节空间：q(s)=(1-s)·q_a + s·q_b（逐轴线性）          任务空间：TCP 沿直线平移＋姿态插值（基座系）
自变量       s∈[0,1]（关节空间弧长参数）                            s∈[0,1]（直线弧长参数）
输入         起点/终点关节构型（选定构型）                          起点/终点位姿（TCP 目标）
IK 需求      仅端点（多初值 IK，选构型）                            沿途逐采样（TRJ-02 连续性检查）
笛卡尔连续性  不提供（TCP 轨迹=派生观察，可拐弯）                     提供（几何即直线＋IK 分支连续）
碰撞检查     端点＋路径采样（policy；R1 直连段可触发避障搜索）        端点＋路径采样（policy PathSequence）
时间参数化   关节空间样条（§12）                                    同一运动律（§12），限值取路径最严关节
失败模式     无解/限位/碰撞/搜索未果                                 IK 分支跳变/奇异邻域/限位/碰撞
典型用途     站间转移、撤离后回位                                    接近/撤离（REQ-02 ToolZ/ReferenceZ）
```

### 7.2 输入与前置

- 起点：上一段终点构型（序列首段为 startStateRef 解析构型）；终点：任务点构型（多初值 IK 端口产出、经构型选择规则选定）。
- 前置：任务点位姿已解析（OrientationRule 解析值随快照冻结）；关节限位/限速视图可用（CanonicalModel）；策略会话可用（碰撞启用时）。
- 空路径/退化：起终点构型相同→零长 PTP 段合法（时间参数化后退化为驻留点，时间=0 或按 dwellPolicy 处理）；序列无启用任务点→TRJ-INPUT-INVALID 素材（非不可行）。

### 7.3 关节空间插值与多关节同步

- 几何路径：逐轴线性插值（joint-linear），保证全部关节同时到达（同步系数 s 对各轴一致）。
- 同步时间：时间参数化阶段（§12）以"最严关节"决定段时长，几何与时间解耦——PTP 段的几何不因限值改变。
- 边界：单关节轴差为零→该轴恒值；全部轴差为零→零长段（§7.2）。

### 7.4 限位、限速与时间计算

- 限位：路径两端点必在评价区间内（IK 硬过滤已保证）；线性插值保持凸组合→段内不越限位（数学性质，不需逐点检查；黄金算例覆盖该性质）。
- 限速/限加速度：不在几何阶段判定；统一在时间参数化（§12）按 §5.3 限值来源校验（唯一判定点，避免双口径）。

### 7.5 构型与候选解选择（确定性规则）

同一任务点存在多个 IK 候选解（KIN-02 关节空间逐轴去重后保留全部构型），PTP 构型选择必须确定且可复现（NFR-COR-02）：

1. **延续性优先**：与上一段终点构型的关节空间距离（逐轴 |Δq| 上界）≤ `ikContinuityThreshold` 的候选优先——避免无谓的构型跳变。
2. **裕量优先**：无延续候选时，按 minimumJointMargin 降序（与 kinematics 稳定排序第一键一致）。
3. **稳定编号兜底**：仍并列取 kinematics 解集 stableIndex 升序（四键排序的最终键，保证全序）。
4. 选择过程与结果记录入 Trajectory 身份块与证据（解引用 ObjectId/解集 stableIndex），review 可回放。

### 7.6 policy 碰撞调用与失败语义（TRJ-06 PTP 侧）

| 情形 | 处置 | 语义归类（§5.6） |
| --- | --- | --- |
| 选定构型（必经状态）碰撞 | 该构型不可用→作为 **MandatoryStateCollision 证明素材**（subject=起点/终点/任务点构型） | 任务级不可行素材（evidence 校验后成立） |
| 候选解碰撞 | 该解被过滤（port 返回的硬过滤语义，KIN-02 同源），换下一候选 | 构型级：仅过滤该解 |
| 端点无碰撞但直连路径采样碰撞 | 触发避障搜索（§10）或淘汰该直连候选 | 路径级：淘汰并重规划 |
| 全部候选路径碰撞/规划器搜索未果 | SearchExhaustedRecord（预算、已试候选数、逐条过滤记录） | 搜索未果→DataInsufficient 素材，**不判不可行**（§8.1 C5/C8） |
| IK 无解 | 段失败定位（TRJ-NO-PATH 素材）；若该点为 Must 任务点且解析界限可证→解析界限素材 | 视证明类别 |
| 起终点不一致/非法输入 | TRJ-INPUT-INVALID 素材，评估终止 | 输入非法（汇总①级） |

- **PTP 不自动提供笛卡尔连续性证据**：JointLinear 段的 TCP 轨迹仅为 FK 派生观察（TimedSample.tcpPose 标记 derived）；**纯关节路径不得伪装成笛卡尔路径、不得产生笛卡尔连续性证据**（TRJ-02 段内 IK 连续性检查对该段标记 NotApplicable——表 4 适用条件原文"纯关节路径不含笛卡尔段"）。
- 段间连续性：见 §9。

---

## 8. 笛卡尔直线段与 IK 分支连续性

### 8.1 R1 范围声明

- R1 仅设计**笛卡尔直线**接近/撤离段（TRJ-02 原文"笛卡尔直线接近/撤离段"）；圆弧、样条等其他几何路径**不在本卡承诺面**（§21.2 P-TRJ-7）。
- 不重新定义 runtime 的基座—世界变换、名称解析或位姿语义（§3.1 N1/N2；MDL-22 单一变换）。

### 8.2 几何构造

- 输入：任务点 TCP 目标位姿 T_end（基座系）、TaskSegment{axis, distanceM}、工具/参考系。
- 方向轴解析：axis=ToolZ→工具系 Z 轴方向经 T_end（或 T_start）旋转部换算至基座系；axis=ReferenceZ→frameRef 姿态的 Z 轴（frameRef 对象经名称端口定位、位姿经编译链）。方向语义（approach 负向/retract 正向）沿用 requirements.md §5.1 登记，本域只做几何解释。
- 起点生成：沿轴从任务点回退/外推 distanceM 得 T_start（approach）或终点外推（retract）；起点位姿同样须 IK 可达（作为段端点检查）。
- 姿态插值：位置线性＋姿态最短弧 slerp（四元数最短路径；黄金算例锁定插值口径与端点还原精度——NFR-COR-01 解析对照）。

### 8.3 直线采样与 IK 端口消费

- 采样计划：按 `cartesianSampleStep`（config.trj）等步长采样（端点必含）；采样计划参数进身份（§5.6）。
- 每采样点经 `IKinematicsComputePort::solveIk`（§15.4，适配 kinematics `kin.task-point-ik` 服务语义）：多初值、硬过滤（残差/限位/碰撞）、去重（关节空间逐轴）、稳定排序——**trajectory 不实现 IK**。
- 每采样点经 `evaluateFk`（`kin.pose-metrics` 语义）取奇异值/条件数→奇异邻域判定（§8.5）。
- **IK 分支连续性检查（TRJ-02 核心）**：对相邻采样点 i, i+1：存在一对解 (q_i, q_{i+1}) 满足逐轴 |Δq|≤`ikContinuityThreshold`→同分支连续；跟踪"当前分支"取链式一致解（首采样点分支取 PTP 构型选择规则延续）。分支断裂→**关节跳变**失败定位（TRJ-BRANCH-JUMP 素材，附采样点 s 与两端解）。
- 证据绑定：连续性检查结果（逐采样解索引、逐轴偏差、判定）入 `trj.cartesian-ik-continuity` 证据项素材（§13.3）；纯关节路径该证据项整体 NotApplicable（表 4 C2 例）。

### 8.4 关节限位与采样失败

- 采样点全部解被硬过滤（残差/限位/碰撞）→该采样失败：单点失败先尝试初值策略扩充记录（搜索未果口径）；仍失败→段失败定位（附 s、已试初值数、过滤原因分布）→SearchExhaustedRecord 素材，不判不可行。
- 限位逼近提示（近限位比阈值属 policy JointThresholds，只读消费）作 warning 级诊断素材，不阻断。

### 8.5 奇异邻域

- 判定输入：条件数/奇异值（FK 端口）；阈值唯一来源＝policy `conditionNumberWarning`（nullopt＝检查显式不适用，P-POL-2 语义承接）。
- 命中→TRJ-SINGULAR-NEIGHBORHOOD warning 素材（附 s、条件数、单位 1）；**不阻断段规划**（奇异邻域是工程警告非硬失败——TRJ-06 要求"给出具体段落与原因"，非禁止通行）；若后续导致 IK 发散/跳变，由 §8.3/§8.4 的失败语义接管。
- policy 未启用条件数警告→该检查 NotApplicable（显式标记，不计缺失）。

### 8.6 policy 路径碰撞与工具/Frame 语义

- 段级碰撞验证统一走 §11 复检协议的 PathSequence 通道（段内采样即复检样本）；规划期的快速筛查亦经同一 policy 会话（SingleState/PathSequence），保证 AT-19 三入口一致。
- 工具/负载几何：conditions.payloads 的工具/负载位姿随 TCP/法兰状态链进入 CollisionScene（policy 会话构建职责，policy.md §6.1）；trajectory 只负责把段状态序列（构型序列）交给 policy——**不自行装配或裁剪碰撞几何**。
- 采样与 evidence 绑定：段采样计划（步长、样本数）与逐样本判定入复检证据项（§13.3）。

---

## 9. 路径连接与连续性

### 9.1 连续性维度与 R1 验收锚点

| 维度 | 定义（逐元素比较，core::closeWithin/allCloseWithin） | R1 地位 |
| --- | --- | --- |
| 位置连续 | 段边界两端 TCP 位置差 ≤ 容差（附录 D 运行校验 ε_abs：长度 1×10⁻¹² m 相对公式） | 必检（段构造保证；检查作为守卫） |
| 姿态连续 | 段边界姿态差 ≤ 容差（角度 1×10⁻¹² rad 相对公式） | 必检（同上） |
| 速度连续 | 时间参数化后结点两侧 qd 差 ≤ 容差（速度 1×10⁻⁹ 相对公式——附录 D 第 10 项同源） | 必检（TRJ-05 运动律性质） |
| 加速度连续 | 结点两侧 qdd 差 ≤ 容差（加速度 1×10⁻⁹） | **必检＝R1 验收重点**（"至少加速度连续的时间参数化"） |
| jerk 连续 | — | **R2（TRJ-08-S1）**；本卡零 jerk 容差承诺，不自扩验收 |
| 关节分支连续 | 相邻采样点逐轴 |q_i−q_{i+1}| ≤ `ikContinuityThreshold`（config，进身份） | 笛卡尔段必检（TRJ-02） |
| 时间边界 | 段边界时刻衔接（knotTimesS 单调、端点对齐）；驻留段两端速度/加速度为零 | 必检（§12.3） |
| 驻留/不停驻边界 | 驻留＝零速零加速度常值段（C² 满足）；**"不停驻连续工艺段"属 R2**，本卡不设计其边界语义 | R1 仅驻留 |

- 容差来源合法性声明：上表容差全部引用附录 D（第 10 项＋C7 运行校验默认）或本域分析配置（`ikContinuityThreshold`/`smoothTolerance`，进身份）或 policy 阈值；**本卡不发明其他数值**。

### 9.2 合法/非法连接与诊断示例

| 场景 | 判定 | 产物 |
| --- | --- | --- |
| PTP→笛卡尔 approach：位置/姿态差在容差内，速度/加速度经样条结点匹配连续 | 合法连接 | 无诊断 |
| approach 末点与任务点位姿差 > 姿态容差 | 非法连接 | TRJ-CONTINUITY-BROKEN 素材（段序号、维度、实际差/容差比较型字段） |
| 相邻段构型跳变（分支切换：两段端点同为一位姿但构型逐轴差超阈值） | 可报告（不必然非法——若为显式构型切换须经避障搜索衔接） | TRJ-BRANCH-JUMP 素材；切换段强制走 §10 规划 |
| 样条结点两侧 qdd 差超加速度容差 | 非法（运动律实现缺陷） | TRJ-CONTINUITY-BROKEN（维度=加速度）＋黄金算例回归拦截（§17） |
| 驻留段后首样本速度非零 | 非法 | 同上（维度=速度） |
| 纯关节序列被请求笛卡尔连续性证据 | 不适用（非非法） | `trj.cartesian-ik-continuity` 项 NotApplicable（原因=无笛卡尔段） |

### 9.3 检查执行点

- 几何连续性（位置/姿态/分支）：段构建与段连接时即时检查（早期失败、精确定位）。
- 时间连续性（速度/加速度）：时间参数化完成后全链检查（§12.5），结果入证据。
- 全部检查结果入 EvaluationOutput（证据素材＋诊断），由 evidence 汇总——本域不自行判定"轨迹可行"。

---

## 10. RobWork 规划器与 policy 交接

### 10.1 归属与装配边界（含上游口径张力登记）

- **上游事实**：TRJ-03 要求"调用 RobWork 规划器"；DTB §4.6 L1 基线表明文登记 `sdurw_pathplanners | 路径规划 | trajectory（WP-16-T06；业务域经各自计算库消费）`；runtime.md §8.1 明文"rw pathplanners（归 trajectory）"不在 runtime 依赖内；而 ARCHITECTURE §5.1 概括表述 RobWork"只经 runtime/policy 适配层接触（§7.3）"。
- **本卡采用口径**（与 DTB/runtime 明文登记一致）：trajectory **计算库** PRIVATE 链接 `sdurw_pathplanners` 消费规划算法（L2 允许消费 L1 基线类型——ARCH §2.3 层规则）；同时：①WorkCell/Device/State 实例**只能**来自 runtime RuntimeSnapshot 只读视图（WorkCellConstView/makeState，零自建）；②碰撞判定**只能**经 policy 会话（R-5，sdurw_proximity 零直链——规划器的碰撞约束经 policy 会话适配，见 §10.3）；③名称经⑥端口。
- 该口径与 ARCH §5.1 概括措辞的差异**登记为 P-TRJ-2**（§21.2），建议架构所有者在 ARCH 评审时把 §5.1 细化为与 §2.3/DTB §4.6 一致的分库口径；在其裁决前，本口径不得扩大到 proximity（R-5 红线不动）。

### 10.2 规划器选型与参数（TRJ-03"规划器与参数可配置"）

- 选型面：`PlannerSelection{family, params}`（config.trj）；family 词表当前登记拟实现值（以 `sdurw_pathplanners` 实际可用算法为准，落位时在黄金算例中锁定），参数表为显式键值（时间预算、步长、扩展参数、目标偏置等），**逐键登记于 PlanConfig.hpp 并入 canonical 身份**。
- 版本身份：`robworkBaselineVersion`（runtime 基线锚，含 pathplanners 行为版本）＋`algorithmVersion`（本域适配实现版本）＋planConfigDigest 三者共同构成"规划器与参数"的完整身份——换规划器/改参数→sliceId 变化→旧轨迹 Superseded。

### 10.3 与 policy 的交接（唯一碰撞权威）

```
trajectory（规划器循环）
  │ 每个候选构型 q
  ▼
QConstraint 适配器（trajectory 持有，纯适配零策略逻辑）
  │ CollisionQuery{kind=SingleState, configurations={q}}
  ▼
policy CollisionEvaluationSession::evaluate(query, IPolicyCallContext)
  │ findings（对象 ID 对、判定、原因码）/ status
  ▼
无碰撞→扩展继续；碰撞→剪枝
候选路径完成（离散构型序列）
  │ CollisionQuery{kind=PathSequence, configurations=…, pathParameters=…}
  ▼
PathSequence 评估→findings（含 pathParameter 段内定位）→该候选路径淘汰并触发重规划（C8）
```

- **policy 返回什么**：CollisionEvaluation{status, findings 稳定排序（sampleIndex×对象对×kind）, PairCoverageRecord coverage, minDistances, appliedFilters, diagnostics, finalized}（policy.md §6.2）。finalized=false（Failed/Canceled）**不得采信**——按 KIN-05 口径转 DataInsufficient 素材。
- **pathParameters 约定**（policy.md §6.2/§13 原文）：PathSequence 查询必须携带与 configurations 等长的 pathParameters；findings.pathParameter 必填（段内定位 TRJ-04）；**样本由调用方（本域）按其细分协议生成，policy 不生成采样**；coverage 只承诺"已检样本"——段间未验证属调用方 DataInsufficient 口径。
- **阈值零参数化**：CollisionQuery 不携带任何阈值/开关（R-POL-5）；安全间距唯一来源＝会话绑定的 EngineeringPolicySet。
- **取消/预算/超时传递**：evaluate 经 IPolicyCallContext 取消观测（execution ICancelSignal 适配）；规划器时间预算＝plannerSelection.params 显式配置；评估级超时＝TaskCapability.evaluationTimeout（§14.1.3）。取消到达→规划循环立即停止→评估器返回 cancelled 语义（非错误）。
- **环境/工具/负载参与几何**：policy 会话构建时由 CollisionScene（对象集＋相邻对＋场景身份）确定；trajectory 经 conditions（payloads/environmentRefs）保证相关对象在快照闭包与 `collision-models` 条目内（Conditional←policy 启用）——会话构建失败（如 POLICY-CLL-SCENE-INVALID）→评估失败/段级 DataInsufficient 素材，不静默降级。

### 10.4 避障重规划流程图（TRJ-03 + C8 语义）

```
直连候选（PTP 构型对）路径采样 ──碰撞？──否──► 采纳为该段路径
        │是
        ▼
进入避障搜索（TRJ-03；规划器=plannerSelection）
  │  碰撞约束 = policy 会话 SingleState 适配（§10.3）
  │  状态/设备 = runtime 快照视图；名称 = ⑥端口
  ▼
规划器产出候选路径 k=1..K（预算内；K 与逐条结果入证据）
  │
  ├─ 候选 k PathSequence 复核 ──碰撞──► 淘汰候选 k（记录：对象对、pathParameter）
  │                                       │ 还有候选/预算？
  │                                       ├─是──► 下一候选（重规划循环）
  │                                       └─否──► SearchExhaustedRecord ──► DataInsufficient 素材（不判不可行）
  │
  └─ 候选 k 通过（全部样本无碰撞且 coverage 完整）──► 采纳；按稳定选择序取首条
        （选择序：延续性→裕量→路径长度→候选序号；全序可复现，NFR-COR-02）
```

- 重规划触发记录（逐候选淘汰原因）入诊断与证据素材——"初始候选路径碰撞、重规划成功"是 AT-06 明文反例（C8），§17 对应行验证。

---

## 11. 平滑与 TRJ-04 复检协议

### 11.1 平滑与复检流程图

```
已验收候选路径（离散路点）
   ▼
①简化（冗余路点剔除：在 smoothTolerance 几何保持容差内删除可省路点）
   ▼
②平滑（关节空间五次样条拟合/圆角过渡；端点强制不变）
   ▼
几何或采样是否变化？──否──► 沿用既有复检结论（零变化零复检）
   │是
   ▼
③TRJ-04 复检（强制，不可跳过）：
   端点复检（各段端点/路点）＋段内采样细分
   细分双上界：关节空间最大步长 ∧ 笛卡尔最大步长（最严对象）同时满足才终止
   笛卡尔步长度量对象 = TCP 参考点 ∧ 全部参与碰撞验证几何代表点集之最大位移（R9；代表点集规则随 P-06）
   采样→ policy PathSequence（pathParameters 携带）＋关节限位复检
   │
   ├─ 全部样本已检且无碰撞/限位 OK ──► 段复检通过（coverage 记录入证据）
   ├─ 发现碰撞/限位违例 ──► 淘汰该平滑结果→回退该段（保留候选路径）→重平滑或重规划（TRJ-06 定位输出）
   ├─ 验证器/碰撞证据缺失（policy Failed、检测器不可用）──► 段级 DataInsufficient 素材（KIN-05 口径；不得视为无碰撞）
   └─ 达到细分预算（P-06：最大层数/子段数）仍不满足步长 ──► 段级 DataInsufficient 素材
        （附实际达到的最大步长与预算占用——R9 原文；不得默认接受未充分验证段）
```

### 11.2 P-06 依赖声明（数值零自设）

- 复检协议数值参数＝**关节/笛卡尔最大步长默认值与细分上界、细分预算（最大层数/子段数）、碰撞几何代表点集生成规则**——全部登记于 REQUIREMENTS 附录 C P-06，负责人＝需求所有者＋WP-16，冻结前置＝阶段 C（TRJ）启用（WP-16-T02 为硬前置，DTB §2.17/§4.3 O-29）。
- **P-06 未冻结前：本域复检协议不启用、T07 不开工**（DTB 依赖列明文）；实现落位后运行时数值读取顺序：EngineeringPolicySet 复检字段（若经 policy 卡增量修订引入）→ 否则按 P-06 冻结默认——**本域代码不携带任何默认值字面量**（TRJ-04③"复检不得使用私有线宽/阈值"；违反即 review 打回）。
- 安全间距阈值消费 policy（§10.3）；当前后端无距离能力时 MarginViolation 不可产出（P-POL-11）→间距检查显式 NotApplicable＋`minCollisionClearance` NotApplicable（不伪造数值）。

### 11.3 复检执行要点

- 复检范围：**只要几何或采样发生变化就重新复检**（简化、平滑、重规划产物一视同仁）；端点强制复检＋段内细分采样。
- 细分实现：对每段按双上界递归二分/加密至满足或预算耗尽；实际达到的最大步长（关节维/笛卡尔维）与预算占用逐段记录（入 TrajectoryQuality.recheckBudget 与证据）。
- 关节限位复检：平滑后逐采样限位检查（限值来源 §5.3；平滑可能越出原路径包络）。
- **复检数据不足 ≠ 无碰撞**；**复检不足不能默认为通过**（TRJ-04④/R9 原文；evidence 汇总按表 4 判 DataInsufficient）。
- 平滑后重新碰撞的处置：淘汰该平滑结果，回退到上一已复检状态；连续 m 次平滑失败（m 记录入诊断）→该段保持未平滑候选路径并给 TRJ-RECHECK-COLLISION 素材——**不静默放行未复检的平滑路径**。

### 11.4 简化/平滑模型与几何保持

- 简化：贪心逐点剔除（保留端点与必经点），剔除后几何偏差（关节维逐轴＋TCP 派生维）≤ smoothTolerance。
- 平滑：关节空间五次样条重拟合（结点＝保留路点），端点位置/姿态/导数边界不变；平滑前后路径偏差 ≤ smoothTolerance（双域容差独立配置）。
- 几何保持验证入证据（偏差实际值/容差比较型字段）；偏差超容差→该次平滑作废。

### 11.5 预算占用记录结构

```cpp
struct RecheckBudgetUsage {
    SourcedValue<double> actualMaxJointStep;    // 实际达到的最大关节步长（rad/m 逐轴最严）；未复检=NotProvided
    SourcedValue<double> actualMaxCartesianStep;// 实际达到的最大笛卡尔步长（m，代表点集口径）；后端无距离能力时按 R9 规则仍可由几何计算
    std::uint32_t subdivisionDepthUsed;         // 实际细分层数
    std::uint32_t subdivisionBudget;            // 预算上界（来源= policy/P-06；引用值非副本）
    std::uint32_t subsegmentsExamined;          // 实际检查子段数
    bool budgetExhausted;                       // 预算耗尽标记（true→该段 DataInsufficient 素材）
};
```

---

## 12. 时间参数化与节拍

### 12.1 运动律（TRJ-05"至少加速度连续"）

- R1 唯一实现运动律：**关节空间五次样条**（分段五次多项式，结点 C² 匹配；端点边界条件：起点/终点零速度零加速度；驻留结点两侧导数为零）——满足"至少加速度连续"，且与 AT-06 验收要点"五次样条 C² 结点匹配"直接对应。
- 结点集＝段边界＋驻留点＋（可选）段内关键路点（平滑后保留路点）；时间自由度经迭代缩放求解（§12.2）。
- jerk 连续属 R2（TRJ-08-S1）；本卡不承诺、不预留字段。

### 12.2 限值校验与时间缩放（唯一判定点）

1. 初值时间分配：按段路径长度/最严关节估计。
2. 构造样条→采样求峰值（逐关节 qd/qdd）。
3. 峰值 vs 限值：限值＝CanonicalJoint.maxVelocity/maxAcceleration × limitsScaleFactor（§5.3 来源；+inf 语义＝runtime 显式设值，非本域补值）。
4. 超限→等比放大时间轴（缩放不改变几何路径）→重算，至满足或达到迭代上限（配置项，进身份；达上限→TRJ-TIME-PARAM-FAILED 素材）。
5. 校验容差：附录 D 第 10 项——相对 1×10⁻⁹（固定），经 `core::closeWithin`/`allCloseWithin` 逐元素判定（速度/加速度 ε_abs 按量纲 1×10⁻⁹ 补充——附录 D C7 运行校验默认）。**超限定位**：命中关节/时刻/段序号入 FailedSegmentRecord（比较型字段：实际峰值/限值/单位）。

### 12.3 多段同步与驻留

- 段端边界条件衔接（结点 C²）保证多段同步；驻留段＝常值段（§9.1），时间计入总节拍与分段时长。
- dwellPolicy=IgnoreDwell 仅限 Preview/Quick（筛选语义）；Verified 必须 HonorEvents（必验工况覆盖）——模式与配置的组合校验在评估器入口执行（组合非法→TRJ-INPUT-INVALID）。

### 12.4 节拍输出（TRJ-05"总节拍和分段时间"）

- 总时长 `totalDurationS`＝全部段＋全部驻留；分段时间 `segmentDurationsS` 逐段对齐；节拍分阶段分解（接近/作业/撤离/转移/驻留）为建议证据项素材（表 4 轨迹行建议项"节拍分阶段分解"）。
- **无时间参数化结果时不得伪造节拍**：时间参数化失败/缺失→quality.totalDurationS 以 SourcedValue 标记 NotProvided，`trj.path-and-time-record` 证据项状态=Missing/Invalid（缺失全量清单——汇总④级 DataInsufficient），**不得输出 0 或估计值**（NFR-COR-03）。
- 目标节拍对照：processParams.targetCycleTimeS 存在时，超标→Should 违例素材（REQ-06：有效 Should 未满足给警告；进入 DomainVerdictInputs.shouldViolations）——**时间参数化失败不自动等于任务级不可行**（缺时间参数属证据缺失④级，与不可行③级不同层）。

### 12.5 时间化后的契约义务

- 时间参数化结果仍受证据与当前性契约约束：入 payload（kindToken v1）、证据项（path-and-time-record）、sliceId 绑定——输入/策略/配置任一变化→Superseded（§14.2.4）。
- 速度/加速度连续性检查（§9.1）在时间化完成后全链执行并入证据。

### 12.6 超限与失败定位（TRJ-06 时间侧）

| 情形 | 产物 |
| --- | --- |
| 迭代缩放达上限仍超速 | TRJ-TIME-PARAM-FAILED＋TRJ-LIMIT-EXCEEDED（比较型：逐关节峰值/限值/单位；定位段序号+时刻） |
| 驻留/边界导数不连续（实现缺陷） | TRJ-CONTINUITY-BROKEN＋黄金算例回归（§17） |
| 时间参数化输入缺限值（模型未设限速） | +inf 语义→时间无界→TRJ-TIME-PARAM-FAILED 素材（原因=限值未定义；建议动作=补模型限值），不伪造节拍 |

### 12.7 可扩展维度接口（TRJ-08 本体约束的落位）

- TimeParameterization.motionLawToken 词表＋TimeParam.hpp 内部"运动律构造器"分派点（当前一个分支）——S1 jerk 限值启用时：新增运动律 token（或样条阶次扩展）＋限值消费扩展＋payload v2；接口分派点已存在但**当前无空分支、无空字段**。
- 评估维度扩展：新增证据项经 Profile 版本递进（§6.6）；jerk/工艺速度/连续工艺段对应的新证据项（trj.jerk-limit-record 等）**不在 v1 Profile 注册**（零占位）。

---

## 13. 轨迹质量与 evidence 素材

### 13.1 质量指标全量口径（TrajectoryQuality 的统计语义）

| 指标 | 统计口径 | 单位 | 来源 |
| --- | --- | --- | --- |
| 总时长 totalDurationS | 全轨迹含驻留 | s | 时间参数化 |
| 分段时长 segmentDurationsS | 逐段（含驻留段） | s | 同上 |
| 峰值速度/加速度 | 全轨迹逐关节最大绝对值（含驻留零段的邻域） | SI | 采样+样条解析峰值取严 |
| 路径长度 | 逐关节 |Δq| 积分；TCP 长度为派生观察 | rad·轴 / m | 几何 |
| 姿态变化 | 逐段 TCP 姿态差之和 | rad | 几何（派生观察） |
| 约束裕量 | 限值−峰值（逐关节逐量） | SI | 时间参数化+模型限值 |
| 碰撞余量 | 最近距离 | m | policy minDistances；后端无距离能力→NotApplicable（P-POL-11） |
| 采样完整性 | 已检样本/计划样本 | [0,1] | 复检 coverage；0 分母→NotApplicable |
| 复检预算占用 | §11.5 结构 | — | 复检记录 |
| 数据完整性 | dataComplete＋缺失清单 | — | 全流程 |

- 单位一律 SI 真值（显示投影是 ui/导出层职责，KIN-12 语义）；来源标记（ValueProvenance 语义：用户提供/派生只读等）随 SourcedValue 携带。

### 13.2 质量指标的地位边界

- 质量指标**不能替代** evidence 的工程判定；**轨迹质量不是证据等级**（不新增等级词表——evidence.md §3.4"不新增等级词表"承接）。
- 缺失碰撞证据、缺失时间参数、预算耗尽必须**可观察**：分别经证据项状态（Missing/Invalid）、SourcedValue NotProvided、RecheckBudgetUsage.budgetExhausted 显式暴露——汇总层（evidence aggregateVerdict）据表 4 判定，本域不代判。

### 13.3 trj RequiredEvidenceProfile（按表 4 轨迹行逐行实例化；evidence.md §6.1"内容归需求，各域逐行实例化，不新增/删除/改写"）

| itemId（提议，实现落位冻结） | itemClass | 对应表 4 内容 | 适用条件 |
| --- | --- | --- | --- |
| `trj.path-and-time-record` | Required | 分段路径与时间参数化记录（含速度/加速度限制校验） | 总是（不可行替代时按替代规则豁免"成功产物"） |
| `trj.smooth-recheck-evidence` | Required | 平滑复检证据（细分步长、段内碰撞判定、预算占用） | 总是（纯关节路径同样复检——TRJ-04 不限笛卡尔段） |
| `trj.cartesian-ik-continuity` | Required | 段内 IK 连续性检查（TRJ-02） | **适用条件＝路径含笛卡尔段**；纯关节路径→NotApplicable（不计缺失，C2 例） |
| `trj.cycle-phase-breakdown` | Suggested | 节拍分阶段分解 | 时间参数化成功时产出 |
| `trj.animation-data` | Suggested | 轨迹动画数据 | 成功轨迹（入 payload 供 ui 消费） |
| 通用必需项 | Required | 快照身份/覆盖矩阵/模式标识（common.snapshot-identity 等） | evidence 内建，隐式附加 |

- 不可行替代（表 4/C6）：必经状态碰撞证明（MandatoryStateCollision）或解析界限/约束矛盾证明成立时，成功产物类证据项按"因不可行而不适用"记录，**不计缺失**；证明自身的可追溯性（快照/工况/诊断身份）不豁免通用门禁（C6）。
- **evidence 域一致性**：本 Profile 经 `EvidenceProfileRegistry` 注册（profileId="trj"），且**先于评估器注册**（evidence.md §13 接入顺序）；评估器 descriptor.profile 引用该 Profile。**不创建 trajectory 私有评估器注册表**（③端口唯一；NFR-MNT-04 禁止逐域转发包装器）。

### 13.4 EvaluationOutput 组装（trajectory 评估器的出口契约，evidence.md §9.3 冻结基线）

```cpp
EvaluationOutput out;
out.evidence      = { /* trj Profile 各证据项：EvidenceItem{itemId, status, artifactDigest, caseScope,
                          subject, notApplicableReason, invalidReason}；证据一律引用 ObjectId */ };
out.proof         = /* 仅当必经状态碰撞/解析界限/约束矛盾素材经本域构造为
                       DeterministicInfeasibilityProof（kind=MandatoryStateCollision 等）；
                       校验归 evidence validateProof——本域只产素材 */;
out.searchRecord  = /* SearchExhaustedRecord（全部候选/初值/分支被过滤时；含预算、已试数、逐条过滤记录） */;
out.verdictInputs = /* DomainVerdictInputs：mustViolations（demands.collisionFreeRequired 于必经状态被证伪以外的
                       Must 需求违例、minimumJointMargin 违例）、shouldViolations（目标节拍超标、Should 任务点未达） */;
out.payload       = /* DomainPayload{kindToken="trj.sequence-plan.v1", canonicalBytes=Trajectory 编码, digest} */;
out.diagnostics   = /* core::DiagnosticRecord 列表（TRJ-* 码建议 + 既有码复用） */;
```

- 纯计算纪律（evidence §9.3 冻结契约）：不派发任务、不写项目、不产生修订、不自我注册；同 (切片,环境)→等价输出（NFR-COR-02）；长评估周期查询 cancellationRequested。

---

## 14. execution/evidence/project/diagnostics/ui 协作

### 14.1 execution 协作（任务提交、能力、取消、检查点、分批）

#### 14.1.1 任务提交链

```
trajectory 插件（会话命令）
  → 组装 AnalysisSnapshot（evidence SnapshotBuilder；req/model/policy/nameMap/config.trj 闭包）
  → TaskSubmission{ snapshot, evaluatorKey="trj-sequence-plan", mode, priority, resumeFrom? }
  → ITaskScheduler::submit → TaskRecord（Queued→Preparing→Running…）
  → worker 派发（共享 sdurws_ird_execution_worker；manifest 握手含 trj 评估器）
  → 评估（§5~§12 管线；批次/检查点经通道 IRDCHN/1 回传）
  → FinalOutput（EvaluationOutput canonical）→ RunRegistry 九步接纳
  → aggregateVerdict（evidence）→ ResultEnvelope::make → results/<run-id>/ 归档
```

- 提交入口归 ui/域宿主会话层；**评估器本体不提交任务**（evidence §9.3 纯计算纪律）。
- 任务类型/评估键登记：`trj-sequence-plan`＋contractVersion 随 descriptor 注册（主/worker 同清单）。

#### 14.1.2 共享 worker 的使用（不建专属 worker）

- 评估器经 `IEvaluatorFactory` 进入 worker 装配清单（execution.md §3.4"正式评估器链接属 L5 装配决策"）；worker 循环（握手→物化→实例化评估器→evaluate→检查点/结果回传）由 execution 持有，trajectory 零改动。
- 长时性承接：避障搜索与逐采样 IK 属"批量/长时计算"，天然落在 worker（ARCH §4.1 明文列举"避障搜索"）；轨迹规划无 UI 线程执行路径（§16 红线）。

#### 14.1.3 能力声明（注册期，execution.md §5.5 EvaluatorRuntimeCapabilities）

| 能力位 | 声明值（R1 设计） | 依据 |
| --- | --- | --- |
| supportsPause | **false**（R1 不承诺 Paused；收到暂停请求→EX-CAPABILITY-UNSUPPORTED 显式反馈，不静默） | ARCH §4.3"Paused 为 R2 承诺"；execution.md §5.5 |
| checkpointGranularity | Segment（段级：每段几何+复检+时间化完成可写检查点） | §14.1.4 |
| forceTerminateCost | Moderate（段级中间态可弃，重算代价中等） | 声明与呈现，不影响协议 |
| evaluationTimeout | 来自 TaskCapability（nullopt=不启用监视）；轨迹任务默认不启用（预算在 config/规划器参数内显式表达） | execution.md EX-WKR-5 承载位 |

#### 14.1.4 检查点（写什么、何时写、如何恢复）

- 写出时机：段级完成边界（该段路径验收＋复检结论＋时间化完成）；评估器经通道 CheckpointBatch 回传，manifest 发布归 project（execution→IResultArchivePort，checkpoints/ 同 results 模式）。
- 检查点内容（域中间态，对 execution 不透明）：已完成段集（段计划/选定构型/复检记录/时间化系数）、当前段游标、随机数状态（规划器流）、config/policy 身份回执。`checkpointFormatVersion=1`（信封版本）；`resumable=true`。
- 恢复：requestResume→evidence `judgeCheckpointCompatibility`（evaluatorKey∧sliceId∧契约版本∧格式版本∧integrity）→通过则新 AttemptId 续跑，**输入快照不变**（AT-35）；失败→EX-CHECKPOINT-INCOMPATIBLE＋重跑指引（旧版本不迁移，P-EX-9——轨迹长任务跨版本升级即全量重跑，如实承接）。
- 边界语义：检查点可恢复≠任务成功；恢复后仍走完整接纳（execution.md §8.1 原文承接）。强杀/崩溃→最近检查点保留（NFR-PERF-02/ARCH §4.4）。

#### 14.1.5 取消/暂停/继续的传递

- 取消：ui/域会话→ITaskController::requestCancel→Running 通道 CancelRequest→评估器在采样/批次/检查点边界轮询 `IEvaluationContext::cancellationRequested`→2 s 内进入 Canceling、停止派发新批次；批次 10 s 收敛→Canceled；超时→强杀（EX-FORCE-TERMINATED）＋最近检查点。
- **取消后不得发布完整轨迹**：terminationCause=Canceled/Failed/ForceTerminated→接纳层拒绝 FinalEnvelope（execution.md §9.2 第⑦步）；评估器侧即使已完成部分段，也以 cancelled 语义收尾（无 FinalOutput）。
- 暂停/继续：R1 supportsPause=false→EX-CAPABILITY-UNSUPPORTED（显式反馈，不静默）；R2 启用时按 Paused 态语义（暂停确认边界＝检查点）扩展本卡。

#### 14.1.6 分批路径输出（NFR-PERF-03/04）

- 大序列任务分批：批次粒度＝段级（ResultBatch：逐段几何+复检+时间化增量）；批次为通道 DTO（execution 视为不透明字节＋批次号），**不是** ResultEnvelope；最终统一在 FinalOutput 汇成完整 EvaluationOutput。
- 进度：reportProgress(percent, phase)（phase token：plan-ptp/plan-line/plan-avoid/smooth/recheck/time-param）；进度走 execution 自有通道与查询投影，**不入领域事件**（core.md D-09）。

### 14.2 evidence 协作（评估产出、证据绑定、汇总、当前性）

#### 14.2.1 评估器注册（装配期，主/worker 同清单）

```
EvidenceProfileRegistry.register({profileId="trj", version=1, required=[…§13.3], suggested=[…]})   // 先注册
EvaluatorRegistry.register(TrjSequencePlanEvaluatorFactory{
    key="trj-sequence-plan", contractVersion=1,
    inputs=[ model.robot-design(Object,Required), tcp(Object,Required),
             req.points(Object,Required), req.conditions(Object,Required),
             policy.resolved(Policy,Required), namemap(NameMap,Required),
             config.trj(Configuration,Required), collision-models(Object,Conditional←policy 启用) ],
    profile=trj/1, supportedModes=[Quick, Verified], stateless=true,
    threadSafety=SingleThread })
```

- 重复键注册拒绝；**不自我注册**（装配清单登记）；descriptor 校验（键语法 `[a-z][a-z0-9-]{1,63}`、Profile 已注册、依赖闭包）在注册期完成（evidence §9.4）。
- Preview 模式：本域支持 Preview 语义的部分预览（只检查有效条目、不产生正式证据与结果对象）——**Preview 请求不产生 envelope**（ResultEnvelope 构造边界拒绝 Preview，evidence §7）；Quick 输出标 `screening-only`（kinematics 先例），不得支撑正式结论（表 1）。

#### 14.2.2 输入绑定（快照/切片/工况/策略/算法版本）

- AnalysisSnapshot 绑定：评估器只消费 request.snapshot/slice；对象解码经 IEvaluationContext::tryObjectBytes(oid, cv)。
- 工况绑定：caseSubset 分批（跨批次覆盖矩阵汇总）；每证据项携带 caseScope。
- 策略/名称身份：切片 policy.resolved/namemap 条目＋快照 policyRef/nameMapRef 双重绑定（CON-06）；结果 payload 携带 snapshotId/sliceId/evaluationKey/contractVersion/mode/五元组（kinematics §5.6 结果绑定同款）。
- 算法版本：Trajectory.algorithmVersion＋contractVersion＋robworkBaselineVersion 入 payload 与身份块。

#### 14.2.3 证据明细生成与 ResultEnvelope

- 证据明细：§13.3 各证据项逐项装配（status/artifactDigest/caseScope/subject/notApplicableReason）。
- envelope 组装归属：**评估器产出 EvaluationOutput；ResultEnvelope 由调用侧（execution 接纳层）经 aggregateVerdict＋ResultEnvelope::make 构造**（kinematics §8.5 同款、evidence §9.3 注）——本域不构造 envelope。
- DataInsufficient 缺失清单：必需证据 Missing/Invalid 时**全量列出**（不因首个缺失短路——汇总④级规则；本域在 EvaluationOutput.diagnostics/payload 中同步携带缺失明细以便呈现）。

#### 14.2.4 当前性判定（消费侧语义，本域配合项）

- ResultCurrentness.computeCurrentness 五步（evidence §8.1）：契约版本匹配→以同一 descriptor 依赖声明对当前上下文重建 targetSliceId→sliceId 相等=Current，不等=Superseded（逐条目失效原因：ObjectContentChanged/PolicyChanged/NameMapChanged/ConfigurationChanged/…）。
- 失效矩阵承接（evidence §5.3 轨迹行）：电机成本✗不失效；**TCP 偏置✓失效**（笛卡尔段/节拍）；连杆几何◐（复检碰撞面）；负载◐；工程策略◐（安全间距/复检阈值）；求解配置◐；需求定义✓；显示单位/会话姿态/显示名✗。
- 历史轨迹：Superseded 不改变 payload、保留为原快照历史证据（CON-02）；本域零动作（当前性归 evidence/ui 投影）。

#### 14.2.5 失败/取消/数据不足交给 evidence

- 失败段/搜索未果/预算耗尽/验证器缺失→分别以 FailedSegmentRecord、SearchExhaustedRecord、RecheckBudgetUsage.budgetExhausted、证据项 Invalid/Missing 交给 evidence——**本域不输出任务级工程判定**（§3.1 N7）。

### 14.3 project 协作（读输入、归档、未来修订命令）

- 输入读取：评估组装前经 IProjectQueryPort 读当前修订对象（objectClosure 组装的源头）；评估运行内只消费快照（PA-3）。
- 归档：trajectory **不直接写盘**；results/<run-id>/ 与 checkpoints/ 写入经 execution→IResultArchivePort（写入口唯一归属 project）；只读模式/失权→拒绝并诊断（PRJ-WRITE-AUTHORITY-LOST 等，project §9.6）——本域如实传播拒绝。
- 修订命令：R1 轨迹域**无产生修订的命令**（规划/动画/导出均为会话或评估行为，AT-04）；若未来出现（如"轨迹设为任务默认路径"类），必须实现 ICommandHandler 经①端口注册——本卡仅预留该机制认知，不定义具体命令（零占位）。

### 14.4 diagnostics 协作（TRJ-* 码注册、诊断构造、上下文）

- 注册协议（diagnostics §4.5）：装配期以 ownerUnit="trajectory" 注册 TRJ-* CodeDescriptor（前缀即所有权；**码值权威归 StableCodeRegistry**，本表为拟注册清单——WP-16-T03/T09 注册时按协议提交，注册期校验/重复拒绝/manifest 握手均走 diagnostics 设施）。
- 拟注册清单（v1 设计基线；注册时可按 paramSchema 规范微调并登记）：

| 码（建议） | 严重度 | 比较型 | 用途 |
| --- | --- | --- | --- |
| TRJ-INPUT-INVALID | error | 否 | 输入非法（序列环/悬空引用/配置非法/模式组合非法）——对应汇总①级素材 |
| TRJ-NO-PATH | error | 否 | 无路径（IK 无解/规划失败段定位） |
| TRJ-BRANCH-JUMP | warning | 否 | 关节分支跳变（附采样点 s 与两端解摘要） |
| TRJ-SINGULAR-NEIGHBORHOOD | warning | 是（条件数/阈值/1） | 奇异邻域（阈值缺失→notApplicable 显式标记） |
| TRJ-LIMIT-EXCEEDED | error | 是（实际/期望/单位） | 速度/加速度/限位超标（TRJ-06 限制超标） |
| TRJ-CONTINUITY-BROKEN | error | 是（实际差/容差/单位） | 位置/姿态/速度/加速度连续性破坏（§9.2） |
| TRJ-RECHECK-COLLISION | error | 否 | 平滑后重新碰撞（段定位＋对象对） |
| TRJ-RECHECK-BUDGET-EXHAUSTED | error | 是（实际步长/预算） | 细分预算耗尽（R9：附实际最大步长与预算占用） |
| TRJ-RECHECK-DATA-INSUFFICIENT | error | 否 | 验证器/碰撞证据缺失（KIN-05 口径） |
| TRJ-TIME-PARAM-FAILED | error | 否 | 时间参数化失败（含限值未定义情形） |
| TRJ-CYCLE-TIME-EXCEEDED | warning | 是（实际节拍/目标/ s） | 目标节拍超标（Should 口径） |
| TRJ-EXPORT-FAILED | error | 否 | 标准轨迹导出失败（io 通道错误转译；先前输出完整保留语义随 io） |

- 诊断构造：经 IDiagnosticFactory.create（码已注册校验、subject/比较型完整性、DiagContext 必填——评估路径 snapshotId/sliceId 必填、任务路径 task 五元组必填）；产生者仅登记，判定不越权（infeasibility-proof 类素材归"有效工程结论"呈现分类，diagnostics §4.3）。
- 两级日志：用户诊断无调用栈/哈希（NFR-REL-05）；开发诊断经 ILogger.logDev；worker 日志经 WorkerLogBatch 通道（帧表归 execution）。
- 可确认诊断：R1 轨迹域**无** ConfirmableFinding 产生点（确认只发生在命令边界；worker 不产生可确认诊断——SA-15）。

### 14.5 ui 协作（动画/曲线/局部重规划/导出/投影）

#### 14.5.1 领域命令（TRJ-07 会话能力；全部零修订，AT-04）

| 命令（提议 token） | 作用 | 修订 | 计算线程 |
| --- | --- | --- | --- |
| `trajectory.plan-sequence` | 发起轨迹规划任务（组装快照→submit） | 无（评估行为） | 提交即返；计算在 worker |
| `trajectory.preview-segment` | 预览单段（Preview 模式，无正式证据） | 无 | worker（Preview 任务不归档正式结果） |
| `trajectory.local-replan` | 路点局部重规划：对选中路段以冻结输入重新求值（新 RunId，新归档结果；不改既有结果、不产生修订） | 无 | worker |
| `trajectory.play` / `trajectory.playback-seek` | 动画播放/进度——驱动会话姿态（KIN-06 语义：sessionStateOnly、producesRevision=false、invalidatesResults=false 四常量契约，ui §16.7 v0.7 同款机器可断言） | 无 | UI 线程（纯插值查表，零规划/碰撞/时间化计算） |
| `trajectory.export` | 标准轨迹导出（§15.13；ExportTarget＋IAtomicFileWriter；失败保留先前输出） | 无 | worker 或 IO 线程（数据已在归档结果中，无需重算） |
| `trajectory.show-curve` | 曲线视图（q/qd/qdd/时刻曲线；只读投影） | 无 | UI 线程（读归档 payload 投影） |

- 命令经 ICommandRegistry 装配期注册（CommandDescriptor：id 点分小写、ownerUnit="trajectory"、scope=Session/View、readOnlyAllowed 语义按 ui §7）；重复 id 注册边界拒绝。

#### 14.5.2 投影与会话态

- DomainReadinessItem（domainKey="trajectory"）经 IUiDomainReadinessSource 端口供给七态投影（verdict/输入完成/缺失清单/活动任务）——域只供数，呈现映射归 ui（ui §6.5）。
- 动画数据：Trajectory payload 的 TimedSample/样条系数投影（建议证据项 trj.animation-data）；播放=按时刻插值查表驱动会话姿态；Playback 呈现衔接按 SA-18 D8/D9（kinematics 迁移先例：Playback 面板驱动会话姿态零修订；**完整轨迹动画归 trajectory 域**——kinematics.md WP-15-T18 行明文）。
- 局部重规划的产品语义：新增一份归档结果并可在会话中与原轨迹比较（两份结果都是历史运行，不改历史）；会话选中态经 SelectionService。

---

## 15. 公共接口

> 本节签名＝**设计基线（Draft）**：不脱离已有单元卡冻结名称——凡消费对端类型的接口（注入端口、评估器、Profile、payload、码表）为对端契约，变更须对端会签；本域内部接口实现任务允许按 DTB §5.4 微调并登记偏差。每接口统一给出：输入/输出与引用关系、前置/后置、错误类型与稳定码归属、取消、确定性、线程、生命周期/所有权、后台执行、副作用/修订、R1/R2/待裁决归属、合法/非法调用示例。

### 15.0 通用约定（适用于 15.1～15.13，各接口不再重复）

| 维度 | 约定 |
| --- | --- |
| 错误类型 | 域错误 `TrajectoryError : std::runtime_error`（token 前缀 `trajectory/...`，如 `trajectory/input-invalid`）；调用方契约违约 fail-fast（断言/异常，不转诊断码）；环境类失败返回素材/诊断（ERR-01），稳定码归属＝diagnostics StableCodeRegistry（TRJ-*，§14.4） |
| 取消 | 一切长计算接口经注入取消观测（IEvaluationContext/ICancelSignal 适配）；取消＝非错误（UX-03），返回 cancelled 语义 |
| 确定性 | 同 (输入字节, 配置, 种子, 线程数) → 等价输出（NFR-COR-02；并行归约容差经 core::closeWithin） |
| 线程安全 | 数据类型并发只读；算法对象单线程使用（每任务一实例）或无共享可变态 |
| 副作用/修订 | 计算库接口零副作用、零修订、零写盘（评估器写盘仅经 execution 通道回传后由接纳层归档） |
| 后台执行 | 长计算（规划/复检/时间化/批量 IK）一律 worker；<1 s 内联门槛仅适用于纯函数小工具（如 Waypoint 几何工具） |
| R1/R2 标注 | 本节全部接口 R1，除显式标注"待裁决/R2"者 |

### 15.1 PTP 规划（域内算法，PlanPtpSegment）

| 项 | 内容 |
| --- | --- |
| 签名（提议） | `SegmentPlanResult planPtpSegment(const PtpRequest&)`；PtpRequest{起点构型 Q、终点候选解集（KinematicSolution 引用）、段约束 SegmentConstraint、policy 会话句柄、取消观测} |
| 输出 | SegmentPlanResult{status{Ok, NoPath, SearchExhausted, MandatoryStateCollisionMaterial, Canceled}, TrajectorySegment, optional<SearchExhaustedRecord>, optional<ProofMaterial>, diagnostics} |
| 前置 | 起终点构型在评价区间内；限位视图可用；碰撞适用性已解析 |
| 后置 | Ok 时段几何确定（waypoints 含端点）；失败时 FailedSegmentRecord 可定位 |
| 取消 | 规划循环边界轮询 |
| 确定性 | 候选选择序 §7.5 全序 |
| 线程/生命周期 | 单线程；请求/结果值语义，无跨调用状态 |
| 后台执行 | 是（worker） |
| 副作用/修订 | 无/无 |
| 归属 | R1（WP-16-T04） |
| 合法示例 | 起点构型→任务点解集{解1,解2}，延续性规则选解2，直连采样无碰撞→Ok |
| 非法示例 | 终点解集为空却调用（前置违约→fail-fast）；传入会话姿态作为起点（违反 §5.3——调用方错误） |

### 15.2 笛卡尔直线规划（PlanCartesianLine）

| 项 | 内容 |
| --- | --- |
| 签名（提议） | `SegmentPlanResult planCartesianLine(const CartesianLineRequest&)`；Request{T_start/T_end（基座系 TCP）、axis/distanceM 语义解析产物、tcpRef/frameRef、采样计划（cartesianSampleStep）、IK/FK 端口句柄、policy 会话、取消} |
| 输出 | SegmentPlanResult{Ok, BranchJump(s), SampleUnreachable(s), SingularNeighborhood(s), DataInsufficientMaterial, Canceled}＋TrajectorySegment＋连续性记录 |
| 前置 | T_start/T_end 经 FK/IK 端点校验可达；工具/参考系对象已解析 |
| 后置 | Ok 时逐采样分支连续记录完整；证据素材（trj.cartesian-ik-continuity）可装配 |
| 错误/稳定码 | TRJ-BRANCH-JUMP/TRJ-SINGULAR-NEIGHBORHOOD/TRJ-NO-PATH（素材归属） |
| 确定性 | 采样计划＋IK 端口确定性＋稳定选择序 |
| 归属 | R1（WP-16-T05） |
| 合法示例 | approach 段 32 个采样点全分支连续→Ok |
| 非法示例 | distanceM≤0（输入非法）；axis 对象未在闭包（输入非法） |

### 15.3 路径连接与连续性检查（CheckContinuity）

| 项 | 内容 |
| --- | --- |
| 签名（提议） | `ContinuityReport checkContinuity(const std::vector<TrajectorySegment>&, const TimeParameterization*)`——几何维随时可用；时间维需时间化结果 |
| 输出 | ContinuityReport{逐边界逐维度判定、违规清单（TRJ-CONTINUITY-BROKEN 素材）、纯关节路径的笛卡尔项 NotApplicable 标记} |
| 前置 | 段序号连续；容差来源已解析（附录 D/config/policy） |
| 确定性/线程 | 纯函数、可重入 |
| 归属 | R1（§9；随 T04/T05/T08 消费） |
| 合法/非法示例 | 见 §9.2 表 |

### 15.4 kinematics IK/FK 注入端口（IKinematicsComputePort）

| 项 | 内容 |
| --- | --- |
| 形态 | trajectory 拥有的最小接口（KinematicsPort.hpp）；实现由装配面适配 kinematics `IIkSolver`/`IFkEvaluator`（P-KIN-2 宿主注入先例同构）；**R-1 合规：本域零 kinematics 编译依赖** |
| 签名（提议） | `IkPortResult solveIk(const IkPortRequest&)`（目标位姿、初值策略/数量、容差、去重阈值、限位区间、碰撞会话引用、种子、取消）；`PoseMetricsPort evaluateFk(const std::vector<double>& q)` |
| 语义 | 完整沿用 `kin.task-point-ik`/`kin.pose-metrics` 契约（五类结局、硬过滤记录、稳定排序、统一尺度）——本端口为透传适配，不重解释 |
| 前置 | 快照已绑定；端口已在装配期注入（缺失→装配失败，不运行） |
| 错误 | 端口层错误 token `trajectory/kin-port-*`（适配失败/契约不匹配）；IK 业务结局按 kinematics 契约 |
| 确定性 | 透传（种子/配置入请求身份） |
| 归属 | R1（WP-16-T05 消费）；**适配器实现落位＝P-TRJ-3 待裁决**（§21.2） |
| 合法示例 | 笛卡尔段逐采样调用 solveIk（同种子同配置→等价解序） |
| 非法示例 | 绕过端口直链 kinematics 头（R-1/R-2 违规，构建门禁拦截） |

### 15.5 RobWork 规划器适配（IPathPlannerAdapter）

| 项 | 内容 |
| --- | --- |
| 形态 | Planner.hpp：适配面（trajectory 拥有）；实现 PRIVATE 消费 `sdurw_pathplanners`（P-TRJ-2 口径）；WorkCell/State 一律来自注入的 WorkCellConstView/makeState |
| 签名（提议） | `PlanSearchResult search(const PlannerSearchRequest&)`；Request{起终点构型、PlannerSelection、QConstraint 适配器（内部转 policy 会话 SingleState）、时间预算、种子、取消} |
| 输出 | PlanSearchResult{status{Found, BudgetExhausted, Canceled, PlannerError}, 候选路径列表（各自 policy 复核状态）, diagnostics} |
| 错误 | PlannerError→TRJ 域素材＋RT-ROBWORK-ERROR 转译登记（runtime translateRobWorkError 同款不吞异常）；稳定码归属 diagnostics |
| 取消 | 预算与取消观测双通道 |
| 确定性 | 种子化规划器＋稳定候选选择序；同种子等价候选集合（RobWork 规划器随机性由种子约束；若实测某 planner 内部不可种子化→该 family 不入选，登记实现任务验证项） |
| 归属 | R1（WP-16-T06）；sdurw_pathplanners 消费资格＝P-TRJ-2（待架构确认登记） |
| 合法示例 | 直连碰撞→search 返回 3 候选，候选 2 通过 PathSequence→采纳 |
| 非法示例 | 直接构造 rw::proximity 检测器注入规划器（R-5 违规） |

### 15.6 路径复检与 policy 碰撞调用（RecheckPipeline）

| 项 | 内容 |
| --- | --- |
| 签名（提议） | `RecheckOutcome recheckSegment(const RecheckRequest&)`；Request{段几何（细分前）、细分参数（policy/P-06 来源引用）、代表点集描述（P-06 规则产物）、policy 会话、取消} |
| 输出 | RecheckOutcome{perSegmentConclusion{Passed, Collision(pathParameter, 对象对), DataInsufficient(reason, budgetUsage)}, coverage 记录, 实际步长记录} |
| 前置 | P-06 数值可用（未冻结→调用本身被域入口拒绝：`trajectory/recheck-unavailable`，WP-16-T02 前置守卫）；policy 会话健康 |
| 后置 | 段级结论唯一（通过/碰撞/数据不足三态，无第四态）；结论入证据 |
| 取消 | 细分循环边界轮询 |
| 确定性 | 细分计划确定性（输入+P-06 数值决定样本集） |
| 归属 | R1（WP-16-T07）；数值启用前置＝P-TRJ-1 |
| 合法示例 | 细分 4 层满足双上界、全样本无碰撞→Passed |
| 非法示例 | 调用方私传步长阈值覆盖 policy（R-POL-5 违约→fail-fast）；finalized=false 的评估被采信（调用方契约违约） |

### 15.7 平滑（SmoothPipeline）

| 项 | 内容 |
| --- | --- |
| 签名（提议） | `SmoothOutcome smooth(const SmoothRequest&)`；Request{候选路径、smoothTolerance（双域）、迭代上限（配置，进身份）、取消} |
| 输出 | SmoothOutcome{status{Smoothed(新路径+偏差记录), NoChange, ToleranceViolated, GiveUpAfterRetries}, 平滑前后偏差实际值} |
| 后置 | Smoothed 必触发复检（§11.1 强制边）；端点不变为硬条件 |
| 归属 | R1（WP-16-T07） |
| 合法/非法示例 | 平滑后偏差 0.4×tolerance→Smoothed→复检；平滑后端点漂移→实现缺陷（黄金算例拦截） |

### 15.8 时间参数化（TimeParameterize）

| 项 | 内容 |
| --- | --- |
| 签名（提议） | `TimeParamResult timeParameterize(const TimeParamRequest&)`；Request{段序列（冻结几何）、限值视图、limitsScaleFactor、motionLawToken、边界条件、驻留表、迭代上限、取消} |
| 输出 | TimeParamResult{status{Ok, LimitUnreachable, LawFailed}, TimeParameterization, 峰值统计, 超限定位} |
| 校验 | 附录 D 第 10 项容差逐元素校验；速度/加速度连续性（§9.1） |
| 归属 | R1（WP-16-T08） |
| 合法示例 | 五次样条 C² 结点匹配、峰值≤限值×(1+1e-9 相对容差内)→Ok |
| 非法示例 | 限值全 +inf→LimitUnreachable（素材，不伪造节拍） |

### 15.9 轨迹质量评估（ComputeQuality）

- 签名（提议）：`TrajectoryQuality computeQuality(const Trajectory&, const QualitySources&)`；QualitySources{policy minDistances/coverage 引用、复检预算记录}。纯函数、零策略副本、缺失项 SourcedValue 四态显式。R1（T08）。

### 15.10 trajectory 评估器（TrjSequencePlanEvaluator）

| 项 | 内容 |
| --- | --- |
| 形态 | 实现 evidence `IEngineeringEvaluator`（冻结基线签名：descriptor()/evaluate(EvaluationRequest, IEvaluationContext)）；工厂实现 IEvaluatorFactory |
| 输入 | EvaluationRequest{task 五元组, mode, snapshot, slice, caseSubset}；注入上下文（快照视图/kinematics 端口/policy 会话端口/名称端口——工厂闭包绑定） |
| 输出 | EvaluationOutput（§13.4） |
| 前置 | 装配期：Profile 先注册、依赖闭包校验通过；运行期：切片 entries 可解析、config.trj 解码成功 |
| 后置 | 纯计算零副作用；同输入等价输出；Preview 不产生 envelope（构造边界拒绝） |
| 错误 | EvidenceError（评估域错误码，evidence §13）或域内返回诊断——不抛诊断、不吞异常 |
| 取消 | 周期查询 cancellationRequested（采样/批次/检查点边界） |
| 确定性 | NFR-COR-02；缓存命中语义经 judgeCacheHit（sliceId+mode+契约版本+Profile 身份） |
| 线程 | threadSafety=SingleThread；stateless=true |
| 生命周期 | 每 worker 每任务一实例；不跨任务复用 |
| 后台执行 | 必然（仅 worker 装配） |
| 副作用/修订 | 无/无（归档由接纳层） |
| 归属 | R1（T03 注册面/T04~T08 能力渐进） |
| 合法示例 | Verified 请求、含笛卡尔段序列→全证据项 Satisfied/NotApplicable 明确、payload v1 |
| 非法示例 | 未注册 Profile 即注册评估器（装配失败）；评估器内部调用 ITaskScheduler（纯计算纪律违约） |

### 15.11 trajectory 领域命令接入（Commands.hpp）

- 形态：命令描述符数据＋处理器薄适配（§14.5.1 清单）；命令处理器组装快照并 submit，**零计算逻辑**（计算在评估器）；无修订命令集（AT-04 语义）；注册经 ui CommandRegistry（装配期，重复拒绝）。
- 归属：R1（T10/T12）；命令 token 为 ui CommandId 词形（点分小写）——**注意 project 命令 token 语法争议（P-PR-9）不适用于本域命令**（本域命令走 ui CommandRegistry，非 project ICommandHandler；若未来出现 project 级轨迹命令须待 P-PR-9 裁决 token 语法）。

### 15.12 execution 任务提交适配（submitTrajectoryTask）

- 签名（提议）：`SubmitResult submitTrajectoryTask(ITaskScheduler&, const TrajectoryTaskSpec&)`；TrajectoryTaskSpec{snapshot、mode、priority、caseSubset、resumeFrom?}——薄组装（TaskSubmission 填充评估键/契约版本/能力已注册前提校验）。前置：评估器与 Profile 已注册（manifest 握手通过）。R1（T04 起可用）。

### 15.13 标准轨迹导出（Export.hpp——格式字段字典归本域）

| 项 | 内容 |
| --- | --- |
| 定位 | **格式所有者＝trajectory**（io.md C-6"字段字典由格式所有者注册"；io 卡无轨迹格式登记→本卡定义，P-TRJ-5 登记是否需需求侧背书）；执行通道＝io ICsvWriter（方言标识/可逆转义由 io 唯一实现，NFR-SEC-03）＋IAtomicFileWriter/ExportTarget（可写性预检、原子替换、不覆盖未经确认文件） |
| R1 字段表（提议） | 头部：方言标识行（io 方言）＋`#ird-trajectory-export v1`＋身份块（snapshotId/sliceId/policyContentIdentity/algorithmVersion/planningSeed/robworkBaselineVersion——复现要素 NFR-COR-04）；数据列：`t_s, joint_index, q, qd, qdd, joint_type`（SI 单位；逐时刻逐关节长表）＋可选段映射列 `segment_index, source_task_point`；驻留行显式（qd=qdd=0） |
| 取舍 | TCP 位姿列不进 R1 导出（派生观察、体积大；建议证据项 animation-data 承载呈现）——登记为格式决策留痕 |
| 前置 | 数据源＝已归档 Completed 结果的 payload（不从会话态导出）；目标可写 |
| 后置 | 文件原子就位；失败/取消→先前输出完整保留（io 语义） |
| 副作用 | 文件系统写（导出副本）；**不产生修订**（AT-04） |
| 归属 | R1（T10） |
| 合法/非法示例 | 导出含 2 段+1 驻留的轨迹→列数恒定、身份块完整；试图从 Superseded 结果冒充当前导出→允许（导出副本带身份块自明），但 UI 默认仅提供 Current 结果入口（呈现约定） |

---

## 16. 插件界面边界

### 16.1 目标与形态

- trajectory 领域界面＝`sdurws_ird_trajectory_plugin`（Qt Widgets，L4；WP-16-T12 落位）——**零计算逻辑**：规划、碰撞、时间参数化、复检全部在 worker/计算库；UI 线程只做投影、插值查表（动画）、命令提交。
- SA-18 宿主融合边界（ARCH §7.12）：RobWorkStudio 唯一产品主窗口；trajectory 经 `IPluginUiRegistrar::registerPluginUi(PluginUiDescriptor)` 注册（白名单 token `trajectory` 已占位，ui.md §11.1；装配序＝白名单序）；域面板经 Provider 三接入面（IUiTreeNodesProvider/IUiPropertyPagesProvider）渐进迁移（先例：requirements WP-14-T11 域装配门面 `RequirementsPluginAssembly.hpp` 出线模式）；**宿主融合不等于把 trajectory 领域模型/算法搬入 ui**——算法留在计算库，ui 只见投影与命令。
- 阶段归属：StageId::TrajectoryDynamics（ui §10.2 冻结值）页面承载轨迹工作流＋曲线视图入口（WP-16-T12 验收口径）。
- 当前落位：**无已落位插件目标、无 GUI 测试可执行文件**（§1.2 M-4）——本卡不虚构其存在；T12 落位时按 DTB §5.1"插件 GUI 手动验证通道"建 `_app` harness 做人工点验（不入产品交付路径）。

### 16.2 零修订与零计算红线（机器可断言）

| 红线 | 机制 |
| --- | --- |
| 界面预览/动画/导出不产生项目修订 | 会话命令零修订（§14.5.1）；View3DSessionPoseContract 四常量同款（sessionStateOnly=true/writesDesignModel=false/producesRevision=false/invalidatesResults=false）适用于轨迹播放会话态——T12 落位时按 ui 契约测试模式登记断言 |
| UI 线程不得执行规划/碰撞/时间参数化 | 全部长计算经 ITaskScheduler；曲线/动画只读归档 payload；命令处理器零计算逻辑（T12 review 检查项＋§17 GUI 流程用例） |
| 插件不读其他单元控件/内存/私有文件（ARC-02） | 只经端口与只读投影 |
| 显示单位/会话姿态不进计算身份 | 投影层换算经 core Units；评估输入组装不含会话态（§5.3） |

### 16.3 与 ui 既有设施的衔接缺口（如实登记）

- WP-16-T12 在 ui.md 无登记（P-TRJ-8）：FormEditCommon（WP-10-T08）域消费者清单未列 WP-16；Playback 会话接口无冻结签名——T12 开工前须与 ui 卡双向增量登记（消费 WP-10-T08 公共件、Playback 轨迹衔接缝），避免 UI-T56 式装配断链（DTB §5.1 F-518 教训）。

---

## 17. 验证方案与故障注入矩阵

### 17.1 验证总体口径（只设计，不宣称已执行）

- 测试框架：googletest 经 vcpkg（DTB §5.5 定稿）；目标 `sdurws_ird_trajectory_test`/`_contract_test`（T13 注册）；黄金数据集 `testdata/golden/trj-*`（testkit §3.4 schema：`ird-golden-manifest/1`）＋容差档案（导出量 ε_abs 逐例声明——附录 D C7 测试对照口径）。
- 双模式构建＋`ird_gates` 零命中＋留痕（gtest XML＋ird-test-report.json＋构建日志）＝DoD（DTB §5.2）；**本卡交付时全部未执行**（§1.2 M-9）。
- 测试替身纪律：替身只能证明**端口、状态与错误传播**，不能证明真实 RobWork 规划器、碰撞后端、runtime 快照或轨迹算法正确性——凡标注"需真实 planner/真实后端"的用例不得以替身替代送验（§17.3 列标注）；替身注入经 testkit FaultInterceptor（进程内）与域端口替身（KinematicsPort/policy 会话/runtime 视图替身）。

### 17.2 故障注入矩阵（核心 30 行；全部为设计）

| # | 用例 | 需求/AT 依据 | 责任单元 | 前置 | 操作/故障注入 | 预期结果 | 观测点 | 真实 planner? | 仅替身可行? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| V-01 | PTP 起终点基准 | TRJ-01/AT-06 | trajectory | 黄金模型+两点序列 | 正常规划 | 段几何=逐轴线性；端点还原≤附录 D 容差 | payload/黄金对照 | 否（解析算例） | 部分（几何可替身；限值校验真实） |
| V-02 | 多关节同步 | TRJ-01 | trajectory | 三关节差量 PTP | 时间化后检查同时到达 | 各轴同时刻到达；峰值≤限值 | TimedSample | 否 | 部分 |
| V-03 | PTP 限位 | TRJ-01/06 | trajectory | 端点近限位模型 | 规划+限位校验 | 端点在区间内；越限输入被拒 | TRJ-LIMIT-EXCEEDED 素材 | 否 | 是 |
| V-04 | 笛卡尔直线基准 | TRJ-02/AT-06 | trajectory | approach 段黄金算例 | 直线插值+端点还原 | 位置/姿态还原≤容差；slerp 最短弧 | 黄金对照 | 否 | 部分 |
| V-05 | 姿态插值 | TRJ-02 | trajectory | 绕行姿态样例 | slerp vs 显式四元数对照 | 解析对照通过（NFR-COR-01） | 黄金数据集 | 否 | 是 |
| V-06 | IK 分支连续 | TRJ-02 | trajectory | 双构型可达直线 | 正常采样 | 逐采样分支连续记录完整 | trj.cartesian-ik-continuity=Satisfied | 否（IK 替身可；语义需真实 IK 复核） | 部分 |
| V-07 | 奇异邻域 | TRJ-06 | trajectory | 腕奇异附近直线 | policy conditionNumberWarning 启用 | warning 素材含 s/条件数；不阻断 | TRJ-SINGULAR-NEIGHBORHOOD | 否 | 是（阈值消费替身） |
| V-08 | 笛卡尔连续性 | TRJ-02/05 | trajectory | 黄金直线段 | 时间化后全链检查 | 位置/姿态/速度/加速度四维连续 | ContinuityReport | 否 | 是 |
| V-09 | 纯关节路径不产生笛卡尔证据 | 表 4 C2 例/AT-06 | trajectory | 纯 PTP 序列 | 评估 | trj.cartesian-ik-continuity=NotApplicable（原因=无笛卡尔段）；不计缺失 | EvidenceItem 状态 | 否 | 是 |
| V-10 | 单候选碰撞→淘汰→替代成功 | TRJ-03/C8/AT-06 | trajectory(+policy) | 单障碍场景 | 直连碰撞→避障 | 候选1 淘汰记录+候选2 采纳；**不判任务不可行** | 重规划记录/envelope Feasible | 是（真实碰撞后端） | 否 |
| V-11 | 所有候选碰撞 | C5/C8/AT-06 | trajectory(+policy) | 密闭障碍场景 | 避障预算内全候选碰撞 | SearchExhaustedRecord→DataInsufficient 素材；**不输出不可行** | searchRecord/envelope 判定 | 是 | 否 |
| V-12 | 搜索未果（预算耗尽） | TRJ-03 | trajectory | 大迷宫场景 | 规划时间预算耗尽 | BudgetExhausted→DataInsufficient 素材（附预算） | PlanSearchResult | 是 | 部分 |
| V-13 | 必经状态碰撞 | §8.1③/C8/AT-06 | trajectory(+policy) | 起点构型碰撞场景 | 起点碰撞检测 | MandatoryStateCollision 证明素材（非路径淘汰语义） | proof 字段/validateProof | 是 | 否 |
| V-14 | 平滑后重新碰撞 | TRJ-04/AT-06 | trajectory(+policy) | 平滑诱发碰撞场景 | 平滑→复检 | 复检检出→回退/重平滑；不静默放行 | TRJ-RECHECK-COLLISION | 是 | 否 |
| V-15 | 平滑后限位复检 | TRJ-04 | trajectory | 平滑越限模型 | 平滑→限位复检 | 越限检出→淘汰该平滑结果 | 复检记录 | 否 | 是 |
| V-16 | 速度超限 | TRJ-05/06 | trajectory | 低限值模型 | 时间化 | 缩放收敛或 TRJ-LIMIT-EXCEEDED（比较型） | 峰值/限值/单位 | 否 | 是 |
| V-17 | 加速度超限 | TRJ-05/06 | trajectory | 低加速度模型 | 同上 | 同上 | 同上 | 否 | 是 |
| V-18 | 时间参数化失败 | TRJ-05/06 | trajectory | 限值未定义模型 | 时间化 | TRJ-TIME-PARAM-FAILED；节拍 NotProvided（不伪造） | quality/证据 Missing | 否 | 是 |
| V-19 | P-06 预算耗尽 | TRJ-04/R9/AT-06 | trajectory(+policy) | 强制细分场景 | 细分达预算仍超步长 | 段级 DataInsufficient 素材（附实际步长+预算占用）；不默认接受 | RecheckBudgetUsage | 是 | 部分 |
| V-20 | 验证器缺失 | TRJ-04④/KIN-05/AT-06 | trajectory(+policy) | 碰撞检测器不可用注入 | 复检执行 | 段级 DataInsufficient；**不得视为无碰撞** | TRJ-RECHECK-DATA-INSUFFICIENT | 否（故障注入） | 是 |
| V-21 | 采样不足 | TRJ-04 | trajectory | 采样计划被截断注入 | 复检 coverage 核对 | sampleCoverage<1→证据 Invalid/素材 | coverage 记录 | 否 | 是 |
| V-22 | 缺少时间参数 | TRJ-05/表 4 | trajectory | payload 去时间化注入 | 汇总前校验 | trj.path-and-time-record=Missing→DataInsufficient | EvidenceItem/aggregate | 否 | 是 |
| V-23 | 取消 | TASK-01/NFR-PERF-02/AT-34 观测 | trajectory+execution | 长序列任务 | 运行中 requestCancel | 2 s 入 Canceling、停止派发；Canceled envelope；无完整轨迹发布；零错误诊断 | 状态机/通道/UX-03 | 否 | 是（协议替身+真实通道） |
| V-24 | 超时强杀 | NFR-PERF-02 | execution(+trajectory) | 无响应 worker 注入 | Canceling 超时 | 强杀→Failed＋EX-FORCE-TERMINATED；检查点保留 | 状态/检查点 | 否 | 是 |
| V-25 | worker 崩溃 | NFR-REL-02/AT-11 | execution(+trajectory) | 评估中崩溃注入 | 进程终止 | 仅当前任务失败；主界面不退；Interrupted/恢复语义 | 退出码/恢复扫描 | 否 | 是 |
| V-26 | 检查点恢复 | TASK-01/AT-35 | trajectory+execution | 段级检查点 | 强杀→resume | 兼容判定通过→新 attempt 续跑；统计不重复；输入不变 | checkpoint/judge | 否 | 是 |
| V-27 | Quick/Verified 分层 | EVI-01/表 1 | trajectory | 同输入两模式 | 双模式评估 | Quick 标 screening-only；Quick→Verified 请求 Incompatible（不隐式升降级） | judgeCacheHit | 否 | 是 |
| V-28 | 策略/输入版本变化 | CON-05/AT-05 | trajectory+evidence | 结果已归档 | 变更 TCP/策略/配置后重评 | 旧结果 Superseded（逐条目原因）；电机成本变更仍 Current | ResultCurrentness | 否 | 是 |
| V-29 | 历史轨迹当前性 | CON-02/04 | trajectory+evidence+project | 归档后 HEAD 前进 | 查询历史结果 | payload 不变、保留为历史证据；不作正式缓存命中 | 归档/缓存查询 | 否 | 是 |
| V-30 | 多工况漏验 | EVI-02/AT-06/32 | trajectory+evidence | 双必验工况、仅一批执行 | 覆盖矩阵核对 | 未全覆盖→不得正式通过（矩阵非完备） | CaseCoverageMatrix | 否 | 是 |

- 追加观测性行：AT-04（动画/导出零修订断言）、AT-19（三入口碰撞一致——与 kinematics/optimization 联合契约测试）、AT-27（求解配置/单位分层——配置进 sliceId 断言）、AT-34（取消/进度）、AT-37（基座—世界变换：倒挂模型笛卡尔段几何/碰撞一致——真实后端）、AT-09/10/11/06 相应行——均含于上表对应行或 §17.4。

### 17.3 "需真实 planner/后端"清单（不得替身替代送验）

V-10、V-11、V-12（部分）、V-13、V-14、V-19（部分）、AT-19 联合行、AT-37 行——这些用例验证 sdurw_pathplanners 行为、policy 真实碰撞后端与 runtime 快照的真实交互；替身版本仅作为端口/状态回归的补充留痕。

### 17.4 GUI/插件流程（只设计流程；当前无 GUI 目标，不虚构测试）

- 适用约束（AGENTS.md/DTB §5.1）：VS x64 开发环境；`QT_QPA_PLATFORM=windows`（不用 offscreen）；绝对路径逐个启动（一次只启动一个 GUI 可执行文件）；集成模式构建零错误后启动；留痕入 `traceability/builds/<task>/`。
- T12 落位时的手动点验流程（设计）：1) 构建集成模式→2) 启动 `sdurws_ird_trajectory_app`（或装配后宿主）→3) 打开黄金项目→4) 轨迹页可见、命令面板可达 trajectory 命令→5) 发起规划任务→进度/取消→6) 动画播放（会话姿态零修订断言）→7) 曲线查看→8) 导出（文件产生、零修订断言）→9) 截图留痕。
- **当前 trajectory 无 GUI 目标：本卡不声明任何 GUI 测试已存在或已通过。**

---

## 18. 阶段 C 任务拆分（对齐 DTB §2.17 WP-16-T01～T16）

| 任务 | 内容 | 本卡依据 | 前置 | 关键验收（DTB 原文口径） |
| --- | --- | --- | --- | --- |
| WP-16-T01 | 编写 trajectory 单元任务卡 | **本文即交付物** | WP-06/07/05/08 各卡＋WP-15 卡 | 卡含 PTP/笛卡尔/避障、平滑复检协议（P-06 数值消费）、时间参数化可扩展维度、诊断定位、任务拆分；不建空字段/空页面/占位模块 |
| WP-16-T02 | 冻结 P-06 复检协议数值（横切：需求侧＋WP-16） | §11.2/P-TRJ-1 | T01 | 数值表冻结留痕并入 REQUIREMENTS 附录 C 修订；未冻结不得启用复检；本文不私设默认值 |
| WP-16-T03 | 构建落位：占位转真实库＋插件目标 | §4.2 | T01、WP-03-T01 | `trajectory/CMakeLists.txt` 新建；双模式构建零错误；二分结构扫描通过；TRJ-\* 码注册随批 |
| WP-16-T04 | PTP 与作业序列路径 | §5.4/§7 | T03、WP-06-T09 | PTP 基准用例通过 |
| WP-16-T05 | 笛卡尔段与段内 IK 连续性 | §8/§9 | T04、WP-15-T04（③端口） | 连续性检查适用/不适用双路用例；**不直链 kinematics（R-1）** |
| WP-16-T06 | 避障路径搜索接入 | §10 | T04、WP-07-T07 | 避障基准用例；候选碰撞→淘汰并重规划（C8） |
| WP-16-T07 | 简化平滑与碰撞复检协议 | §11 | T05/T06、**T02**、WP-07-T07 | 段内碰撞反例检出；预算耗尽段判 DataInsufficient 附实际步长；间距阈值消费工程策略；无私有线宽/阈值 |
| WP-16-T08 | 时间参数化 | §12 | T07 | 五次样条 C² 结点匹配；限速校验容差（附录 D 第 10 项）用例 |
| WP-16-T09 | 失败段诊断定位 | §14.4/各失败语义 | T04~T08 | 失败段定位到段落与原因码用例 |
| WP-16-T10 | 动画/曲线/局部重规划/导出 | §14.5/§15.13 | T08、WP-10 | 动画/导出不产生修订用例 |
| WP-16-T11 | 评估接口可扩展（接口约束） | §6.6/§12.7/§19.1 | T08 | 可扩展性评审通过；零空字段/空页面；**不实现 S1~S3 用户功能** |
| WP-16-T12 | trajectory 插件界面 | §16 | T04~T10、WP-10-T08（登记缺口 P-TRJ-8） | 界面链路用例；插件零计算逻辑；GUI 手动验证通道留痕 |
| WP-16-T13 | 契约测试与轨迹黄金数据集 | §17 | T04~T12、WP-02 | `trajectory/test/*`＋`testdata/golden/trj-*`；AT-06 全部反例；全部用例通过并留痕 |
| WP-16-T14 | jerk 上限约束 | **R2**（TRJ-08-S1） | T08/T11 | 限值满足断言＋超限段定位诊断 |
| WP-16-T15 | 工艺速度约束 | **R2**（TRJ-08-S2） | T08/T11 | 工艺速度约束满足断言＋超限诊断 |
| WP-16-T16 | 连续工艺轨迹段 | **R2**（TRJ-08-S3） | T08/T11 | 段间速度连续、驻留/不停驻切换明确断言 |

- T14～T16 **明确为 R2 延后任务**：本卡不为其设计字段/页面/模块（§6.5）；其启用随需求侧 S1~S3 独立验收（V13-02）。
- 任务排序约束：T02 是 T07 的硬前置（P-06）；T03 先于一切实现任务；评估器注册面（T03 批内）先于 T04 首个能力接入。

---

## 19. 后续阶段与接口交接

### 19.1 R2（TRJ-08-S1～S3）与未来扩展的接口交接

| 扩展 | 启用时的交接物（现不预建） | 现有承接点 |
| --- | --- | --- |
| TRJ-08-S1 jerk 限值 | 新运动律 token 或样条阶次扩展＋限值消费扩展＋payload `trj.sequence-plan.v2`＋Profile 递进＋黄金算例 | §12.7 运动律分派点；§6.6 版本化机制 |
| TRJ-08-S2 工艺速度 | SegmentConstraint 扩展版本（指定段 TCP 速度受限/恒定）＋节拍分解细化 | §6.6；WP-16-T15 |
| TRJ-08-S3 连续工艺段 | SegmentSpaceType 词表扩展＋段间边界行为定义（R2 验收：段间速度连续、驻留/不停驻切换明确） | §9.1 边界表（R2 行留白为显式登记非占位） |
| 笛卡尔圆弧等几何 | 需求变更立项→本卡修订 SegmentSpaceType＋§8 扩展章 | P-TRJ-7 |
| 优化域（OPT-D）消费 | 节拍指标与轨迹联合硬约束经 ③端口组合（优化域在 OPT-D 启用联合评估时按本域 descriptor/Profile 组合消费；阶段 B/C 无优化切片——§15.0 权威范围） | 评估键/Profile/payload 契约稳定；P-TRJ-2/§19.2 |

### 19.2 对下游/协作单元的接口交接清单（供各卡登记消费）

| 对端 | 交接内容 | 对端承接章节建议 |
| --- | --- | --- |
| dynamics（WP-17） | 轨迹 TimedSample/分段时长作为 DYN-03 峰值/RMS 的输入面（完整任务循环含驻留）——消费 payload 或归档结果，R-1 经端口 | dynamics 卡（待产出） |
| drivetrain/selection | 无直接消费（经 dynamics/优化域间接） | — |
| optimization（WP-20/21） | `trj-sequence-plan` 评估器经 ③端口可组合（OPT-D）；Quick/Verified 分层与缓存语义沿用 | optimization 卡 |
| reporting（WP-12） | RPT-01-C"轨迹与节拍"章节数据源＝归档 payload＋trj 证据项；零渲染职责在本域 | reporting.md §13 |
| evidence | Profile trj/1＋评估器注册（先 Profile 后评估器）；TRJ-02 条件依赖行已由 evidence D-10 登记 | evidence.md §4.2.3/§13 |
| execution | 评估器进 worker 装配清单；能力声明（§14.1.3）；检查点域格式 v1 | execution.md §13 |
| diagnostics | TRJ-* CodeDescriptor 清单注册（T03/T09） | diagnostics.md §12.1 |
| ui | WP-16-T12 消费登记补齐（P-TRJ-8）；Provider 三接入面迁移批次排期 | ui.md §13/§14.1 |
| requirements | TRJ-01/02 消费面（sequenceKey＋TaskSegment）已登记（V-19 联合验证） | requirements.md §13 |
| kinematics | `kin.task-point-ik` 服务消费（单点/序列）；注入端口适配（P-TRJ-3） | kinematics.md §12 |

---

## 20. 需求—设计—验证追踪矩阵

> 覆盖判定沿用 DTB §3 口径：每个需求 ID 映射到本卡设计章节＋验证矩阵行＋任务行。AT 观测点以 REQUIREMENTS §21 定义为准；本卡不重定义验收标准。

| 需求/AT | 设计章节 | 验证行（§17） | 任务（WP-16） |
| --- | --- | --- | --- |
| TRJ-01 | §5.4/§7 | V-01/02/03 | T04 |
| TRJ-02 | §8/§9 | V-04~09 | T05 |
| TRJ-03 | §10 | V-10~12 | T06 |
| TRJ-04 | §11 | V-14/15/19/20/21 | T02/T07 |
| TRJ-05 | §12 | V-08/16/17/18/22 | T08 |
| TRJ-06 | §7.6/§8.5-6/§9.2/§12.6/§14.4 | V-03/07/16/17/18 | T09 |
| TRJ-07 | §14.5/§15.13/§16 | GUI 流程（§17.4）＋AT-04 断言 | T10/T12 |
| TRJ-08 | §6.5-6.7/§12.7/§19.1 | 评审项（T11 验收）＋零占位断言 | T11 |
| TRJ-08-S1~S3 | §19.1（R2 留痕） | R2 独立验收（§17.1 子项） | T14~T16 |
| KIN-01~05/13 | §8.3/§10.2（③端口消费纪律） | V-06/07＋AT-19 联合 | T05 |
| CON-01~06 | §5/§14.2 | V-28/29 | T04~T08 |
| EVI-01/02 | §13/§14.2 | V-09/22/27/30 | T13 |
| TASK-01~03 | §14.1 | V-23~26 | T04 起 |
| MDL-06/14/22 | §5.3/§10.2 | AT-37 行（真实后端） | T05/T06 |
| OPT-03/06/07 | §19.1/§19.2 | （OPT-D 启用时）V-27 语义复用 | T11 |
| NFR-COR-01/02/03/05 | §12/§15.0/§10.3 | V-01/04/05（解析对照）；V-10~14（AT-19） | T13 |
| NFR-PERF-01/02/03 | §14.1/§16 | V-23/24/25 | T12/T13 |
| NFR-REL-02/03 | §4.3/§14.1.4 | V-25/26 | T13 |
| NFR-MNT-01/03/07 | §3.2/§4.2/§16 | ird_gates＋红线扫描（每落位任务） | T03 |
| AT-04 | §14.5/§16 | 零修订断言＋GUI 流程 | T10/T12 |
| AT-05 | §14.2.4 | V-28 | T13 |
| AT-06 | 全链 | V-01~22/30（反例全集） | T13 |
| AT-09 | §19.1（供给面） | 经 optimization 域验收（本域观测点：评估器可组合） | T11 |
| AT-10 | §14.3（PM-13/TASK-03 承接） | execution 域验收（本域观测点：归档位置取自登记记录） | T04 起 |
| AT-11 | §14.1.4 | V-25/26 | T13 |
| AT-19 | §10.3 | 三入口联合契约测试（kinematics/trajectory/optimization） | T13 |
| AT-27 | §5.5/§16.2 | 配置进 sliceId 断言＋显示分层用例 | T13 |
| AT-34 | §14.1/§14.5 | V-23（取消/进度观测点） | T12/T13 |
| AT-37 | §5.3/§8.2 | 倒挂一致性（真实后端，与 runtime/policy 联合） | T06 |
| 附录 D 第 10 项 | §12.2/§9.1 | V-16/17（容差逐元素校验） | T08 |
| P-06 | §11.2 | T02 冻结留痕（前置，非测试） | T02 |

---

## 21. 设计决策、风险与待裁决项

### 21.1 设计决策登记（D-TRJ-x）

| # | 决策 | 依据与理由 | 影响 |
| --- | --- | --- | --- |
| D-TRJ-1 | 单评估键 `trj-sequence-plan` 承载全链（PTP/直线/避障/平滑复检/时间化），段级拆分仅作为域内部结构与分批粒度 | AT-06 把轨迹链作为一个验收整体；单键使缓存/当前性/覆盖矩阵判定口径唯一；多键会引入键间依赖声明与部分完成态的组合爆炸（表 3 合法组合约束） | 评估键稳定后变更＝契约版本递进 |
| D-TRJ-2 | kinematics 计算能力经**注入端口**消费（零编译依赖），不經 upstream result 依赖 | TRJ-02 需要逐采样 IK 服务（非一次性结果）；R-1 禁止直链；与 P-KIN-2 宿主注入放行形态同构 | 适配器落位待裁决（P-TRJ-3） |
| D-TRJ-3 | 规划算法经 `sdurw_pathplanners`（DTB §4.6 登记），碰撞约束经 policy 会话适配——规划器内部零 proximity 触碰 | TRJ-03 明文调用 RobWork 规划器；R-5 唯一碰撞权威（AT-19）；runtime §8.1 明文 pathplanners 归 trajectory | P-TRJ-2 登记与 ARCH 措辞对齐 |
| D-TRJ-4 | 时间参数化 R1 唯一运动律＝关节空间五次样条（C²），端点零速零加速度；限值校验单点化（§12.2 唯一判定点） | "至少加速度连续"硬约束；AT-06"五次样条 C² 结点匹配"验收原文；单点校验避免 PTP 几何层与时间层双口径 | 运动律词表封闭单值，扩展走修订 |
| D-TRJ-5 | 复检数值零字面量：运行时读取顺序 policy（若经 P-06 冻结扩展）→P-06 冻结默认；未冻结则复检入口整体拒绝 | TRJ-04③⑤原文；DTB"本文不私设默认值"禁止项；review 红线 | T02 为 T07 硬前置 |
| D-TRJ-6 | 必经状态枚举＝{起始状态, 终止状态, Must 任务点构型}，作为 MandatoryStateCollision 证明素材的 subject 词表 | §8.1 表 2③"必经状态"定义（任务强制且不可选择）；候选路径/可选构型明确排除（C8） | stateKind 词表与 evidence §6.4 对齐（P-TRJ-9） |
| D-TRJ-7 | 构型选择三键全序：延续性（逐轴阈值）→minimumJointMargin 降序→stableIndex 升序 | NFR-COR-02 确定性；与 kinematics 稳定排序键兼容；避免跨段构型跳变 | 记录入证据可回放 |
| D-TRJ-8 | 标准轨迹导出格式（字段字典）归本域，执行经 io 通用通道；R1 字段表见 §15.13 | io.md C-6 格式所有者注册机制；io 无轨迹格式登记的事实 | P-TRJ-5（需求侧背书） |
| D-TRJ-9 | R1 不创建 trajectory worker、不注册 supportsPause=true、不定义产生修订的命令 | ARCH §4.1/§4.3；AT-04；execution §13 通用通道 | R2 需求出现时按增量修订 |
| D-TRJ-10 | 依赖键词表复用 kinematics 已登记语义（model.robot-design/tcp/req.points/req.conditions/policy.resolved/namemap/collision-models），新增仅 `config.trj` | evidence 依赖键跨域唯一性校验；同语义同键防止切片漂移口径分裂 | 注册期闭包校验强制 |

### 21.2 待裁决项（P-TRJ-x；登记格式按 DTB §4：编号｜问题｜影响｜来源｜建议裁决者｜状态）

| # | 问题 | 影响 | 来源 | 建议裁决者 | 状态 |
| --- | --- | --- | --- | --- | --- |
| P-TRJ-1 | **P-06 未冻结**：关节/笛卡尔最大步长默认、细分上界、细分预算（层数/子段数）、碰撞几何代表点集生成规则均无数值；运行时承载位（EngineeringPolicySet 扩展字段）未落 policy 卡 | TRJ-04 复检无法启用；T07 硬前置；细分协议无实现参数 | REQUIREMENTS 附录 C P-06；DTB §4.3 O-29；本卡 §11.2 | 需求所有者＋WP-16（T02）；policy 卡所有者随批 | 未冻结（登记） |
| P-TRJ-2 | **规划器适配归属口径**：DTB §4.6 登记 `sdurw_pathplanners→trajectory`、runtime §8.1"pathplanners 归 trajectory"，与 ARCH §5.1"只经 runtime/policy 适配层接触"概括措辞存在张力 | T06 实现合法性依据；CMake 依赖白名单/例外登记需架构背书 | 本卡 §10.1；DTB §4.6；runtime.md §8.1；ARCH §5.1 | 架构所有者（ARCH 评审时将 §5.1 细化为分库口径并同步依赖表） | 登记 |
| P-TRJ-3 | **kinematics 计算能力注入适配器的实现落位**：IKinematicsComputePort/IPolicySessionPort 适配器实现归 L5 装配还是由 kinematics/policy 提供导出适配面 | T05/T06 开工前置；P-KIN-2 同源（IEvaluationContext 无模型访问器） | 本卡 §15.4；kinematics.md P-KIN-2 | evidence＋execution 所有者会签（kinematics 参与） | 登记 |
| P-TRJ-4 | **碰撞后端距离能力**：policy 内置后端为二值碰撞（ProximityStrategyRW 无距离查询），safetyClearance>0 的 MarginViolation 与 requestMinDistance 当前不产出；TRJ-04③ 安全间距复检语义受影响 | 复检证据只有二值 Collision；碰撞余量指标 NotApplicable；黄金算例的间距断言受限 | policy.md P-POL-11；本卡 §11.2/§13.1 | 需求所有者＋架构（后端引入距离能力 vs 间距检查口径确认） | 登记 |
| P-TRJ-5 | **标准轨迹导出格式无需求语义背书**：TRJ-07 仅言"标准轨迹导出"，未定义格式；本卡 §15.13 给出 R1 字段表提议 | 格式属用户可见契约，冻结后变更影响外部使用者 | 本卡 §15.13；io.md（零登记） | 需求所有者（确认"格式由详设定义"或走需求变更） | 登记 |
| P-TRJ-6 | **evaluationTimeout 与暂停的 R1 口径**：R1 supportsPause=false 已按 ARCH §4.3；evaluationTimeout 默认不启用是否满足 NFR-PERF-02 的监视预期 | 能力声明与反馈行为 | 本卡 §14.1.3；execution.md §5.5 | execution 所有者确认 | 登记（低风险） |
| P-TRJ-7 | **圆弧等几何路径未立项**：TRJ-02 仅直线；圆弧无需求条目 | 本卡不承诺；扩展走需求变更 | REQUIREMENTS §12；本卡 §8.1 | 需求所有者（如需立项） | 登记（范围外） |
| P-TRJ-8 | **ui 卡登记缺口**：WP-16-T12 消费 WP-10-T08 公共件未在 ui §14.1 登记；Playback 轨迹衔接无冻结接口；Provider 迁移批次未排 trajectory | T12 装配断链风险（F-518 教训同款） | 本卡 §16.3；ui.md §13/§14.1 | ui 详设所有者＋WP-16 双向增量登记 | 登记 |
| P-TRJ-9 | **必经状态 stateKind 词表未冻结**：evidence §6.4 DeterministicInfeasibilityProof.mandatoryState 字段存在但 stateKind 词表未定 | 证明素材结构对齐；validateProof 校验实现 | 本卡 §5.6/D-TRJ-6；evidence.md §6.4 | evidence 所有者（需求侧表 4 ③已定义语义，词表属设计承载） | 登记 |
| P-TRJ-10 | **驻留/多段边界与 S3 的 R2 边界确认**：R1 仅驻留（零速常值段）；连续工艺段边界语义 R2 定义 | §9.1 边界表 R2 行 | 本卡 §9.1；TRJ-08-S3 | 需求侧（S3 启用时） | 登记（R2） |
| P-TRJ-11 | **上游基线漂移**：ARCHITECTURE v0.13 仍 Draft 待评审；core/evidence/runtime/policy/execution/project/diagnostics/ui/requirements/kinematics 各卡未冻结 | 本卡消费的全部签名存在 diff 风险；冻结后按影响面增量同步（evidence §9 与各卡 §13 已冻结基线除外） | 各卡 P-x-7 同源登记 | 各对端所有者（冻结时通知下游） | 持续 |
| P-TRJ-12 | **需求投影解码机制确认**：req-* 对象解码口径（requirements 公共编解码面 vs 域内自持解码）随 kinematics 先例落地，trajectory 跟随同一裁决 | T04 输入组装实现 | 本卡 §5.2；requirements.md §9.5；kinematics.md §4.3 | requirements＋kinematics 所有者（trajectory 跟随） | 登记 |

### 21.3 风险登记

| # | 风险 | 缓解 |
| --- | --- | --- |
| R-TRJ-1 | RobWork 规划器随机性不可完全种子化→确定性验收（NFR-COR-02）失败 | T06 落位时逐 family 验证种子化能力；不可种子化 family 不入选（§15.5）；黄金算例锁定候选集合等价性口径 |
| R-TRJ-2 | 逐采样 IK 服务性能不满足响应性（长直线段×多初值） | 段级分批＋检查点（§14.1.6）；采样计划进身份保证可续算；进度节流上报 |
| R-TRJ-3 | 平滑与复检的迭代不收敛（平滑→碰撞→回退循环） | 回退上限（§11.3 连续失败计数）＋保留未平滑候选路径＋诊断可观察；不静默放行 |
| R-TRJ-4 | 跨卡契约漂移导致返工（P-TRJ-11） | 本卡消费面集中登记（§3.1/§15）；冻结 diff 走增量修订；evidence §9 冻结基线降低最大暴露面 |
| R-TRJ-5 | 二值碰撞后端下间距语义缺失被误读为"无间距问题" | minCollisionClearance/间距检查显式 NotApplicable＋诊断文案不弱化（RPT-05 限定语精神） |

### 21.4 开放问题（非裁决、仅登记）

- 五次样条时间缩放的迭代上限默认值：属分析配置默认（进身份），数值随 T08 黄金算例声明（测试对照容差逐例声明口径，附录 D C7），不进本卡正文——避免与 P-01/P-06 的范围边界冲突（附录 D 范围边界行：显示与舍入位数及本类实现参数不属容差表）。
- 动画播放的会话态接口（Playback 衔接缝）签名：待 P-TRJ-8 与 ui 双侧冻结后回登本卡 §16。

### 21.5 变更记录

见 §22。

### 21.6 交付前自审

见 §23。

---

## 22. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-10-06 | 首版草案（WP-16-T01 交付物）：依据 REQUIREMENTS v1.16 与 ARCHITECTURE v0.13 完成 23 节详细设计——输入版本与缺失项实测台账、R1/R2 边界、拥有/消费/不拥有表、未来目标布局（不建专属 worker）、输入快照与身份（config.trj 进切片）、轨迹数据模型（零 jerk/工艺速度占位）、PTP、笛卡尔直线与 IK 分支连续性、路径连续性（R1 验收锚点=至少加速度连续）、RobWork 规划器与 policy 交接（pathplanners 消费口径＋P-TRJ-2 登记）、平滑与 TRJ-04 复检协议（P-06 零自设）、时间参数化（五次样条 C²＋附录 D 第 10 项容差）、trj Profile 与证据素材、execution/evidence/project/diagnostics/ui 协作（共享 worker、无专属 worker、取消/检查点/当前性）、公共接口 13 族、插件界面边界（零计算逻辑、零修订）、30 行故障注入矩阵、T01~T16 任务拆分（T14~T16 R2）、追踪矩阵、10 项设计决策、12 项待裁决、5 项风险、交付前自审。纯文档交付：未执行任何构建/测试/GUI 测试/验收 |

---

## 23. 交付前自审

逐项核对（对照任务指令 18 项检查清单；结论为**文档设计层面**自查，不构成实现测试或正式验收）：

| # | 检查项 | 结论 | 依据 |
| --- | --- | --- | --- |
| 1 | 是否假定了不存在的 trajectory 源码、插件或 worker | **否**——§1.2 实测台账：仅 README 占位＋INTERFACE 目标；§4.1 如实登记；§15 全部接口标注"设计基线/提议"，§18 全部任务未执行 | §1.2/§4.1/§23 前言 |
| 2 | 是否错误创建 trajectory 专属 worker | **否**——§4.3 明文承诺不创建；长时计算走共享 `sdurws_ird_execution_worker` | §4.3/§14.1.2 |
| 3 | 是否违反 R-1/R-2/R-3/R-4 | **否**——§3.2 逐红线自查：requirements/kinematics 零编译依赖（注入端口＋③端口语义）；只消费公共头；计算库零 Qt（插件目标除外，ARCH §3.3 二分）；名称全部经⑥端口；sdurw_pathplanners 消费口径已登记 P-TRJ-2（不扩大到 proximity，R-5 不动） | §3.2/§10.1 |
| 4 | 是否重复 runtime/kinematics/policy/evidence/execution/project/diagnostics/ui 职责 | **否**——§3.1 不拥有表 N1~N13 逐项列禁；IK/FK、碰撞、判定、调度、归档、码表、渲染、解析均归原所有者 | §3.1 |
| 5 | 是否把纯关节路径误当作笛卡尔连续性 | **否**——§7.6"PTP 不自动提供笛卡尔连续性证据"、§8/§9：`trj.cartesian-ik-continuity` 适用条件=含笛卡尔段，纯关节路径 NotApplicable（V-09） | §7.6/§9.2/§13.3 |
| 6 | 是否把单个构型碰撞、单条路径碰撞或搜索未果误判为任务不可行 | **否**——§7.6 表：构型碰撞=过滤该解；路径碰撞=淘汰并重规划（§10.4）；搜索未果=SearchExhaustedRecord→DataInsufficient 素材；必经状态碰撞才产 MandatoryStateCollision 素材；最终判定归 evidence | §5.6/§7.6/§10.4/§13.4 |
| 7 | 是否遗漏路径、工况、策略、规划器和算法版本身份 | **否**——Trajectory 身份块七要素（snapshotId/sliceId/policy/契约版本/algorithmVersion/configDigest/seed/robworkBaseline/kinematics 契约版本）；工况经 caseSet+caseSubset；规划器身份=planConfigDigest+robworkBaselineVersion+algorithmVersion | §6.2/§6.3/§10.2 |
| 8 | 是否把 KIN-04 区域覆盖采样和 TRJ-04 路径复检采样混为一谈 | **否**——KIN-04 样本集归 kinematics 区域覆盖（req.sampling-plans/kin-region-coverage）；TRJ-04 采样=本域复检细分计划（config/policy 决定、PathSequence 样本由调用方生成）；两套采样分属两域两证据项 | §5.2/§8.6/§11 |
| 9 | 是否在平滑后跳过碰撞和关节限制复检 | **否**——§11.1 强制边："只要几何或采样发生变化就重新复检"；端点+段内细分+限位复检；V-14/V-15 验证 | §11.1/§11.3 |
| 10 | 是否绕过 P-06 自行设置复检参数 | **否**——§11.2 零字面量承诺；运行读取顺序 policy→P-06 默认；未冻结则入口拒绝；D-TRJ-5 | §11.2 |
| 11 | 是否把 jerk、工艺速度或连续工艺段写成 R1 功能 | **否**——§6.5 零字段/零类型/零页面承诺；§19.1 R2 交接；T14~T16 标注 R2 | §6.5/§19.1/§18 |
| 12 | 是否把缺少时间参数的数据伪装成节拍 | **否**——§12.4：无时间参数化结果→totalDurationS NotProvided、证据 Missing、缺失全量清单；不输出 0/估计值 | §12.4 |
| 13 | 是否让显示单位或会话姿态改变计算身份 | **否**——§5.3 起始状态不从会话姿态读取；§5.5 配置仅计算参数；§16.2 红线表；§14.2.4 失效矩阵显示项✗ | §5.3/§16.2 |
| 14 | 是否在取消或失败后发布完整轨迹 | **否**——§14.1.5：取消→cancelled 语义、无 FinalOutput；接纳层第⑦步拒绝；部分结果仅诊断账户、不作缓存命中 | §14.1.5/§6.7 |
| 15 | 是否创建了新的状态、证据等级、阈值或工程判定 | **否**——§5.6 六状态正交表全部复用既有词表（TaskState 九态/EvidenceItemStatus 五态/EngineeringStatus 四态/Currentness 二态/ArchivePhase）；§13.2"轨迹质量不是证据等级"；诊断码仅"拟注册清单"（码值权威归 StableCodeRegistry）；阈值零副本 | §5.6/§13.2/§14.4 |
| 16 | 是否把跨单元验收误写成 trajectory 单元独立通过 | **否**——§17 责任单元列含联合行（execution/evidence/policy/真实后端）；AT-19/AT-06/AT-37 标注联合验收；本卡零"已通过"声明 | §17.2/§17.3 |
| 17 | 是否将文档自审写成实现测试或正式验收 | **否**——§1.2 M-9 明示未执行任何构建/测试/验收；§17.1"只设计，不宣称已执行"；本节结论限于设计层面自查 | §1.2/§17.1 |
| 18 | 附带检查：图示完备性（任务指令要求的 7 项图示） | **齐备**——需求→段→轨迹→时间化流程（§5.1/§14.1.1 链图＋§6.1 分类）；PTP 与笛卡尔对比（§7.1）；IK 分支连续性时序（§8.3 文字时序＋V-06）；避障重规划流程（§10.4）；平滑→复检流程（§11.1）；轨迹结果→证据→当前性关系（§13.3→§14.2.3/§14.2.4 链）；四状态正交表（§5.6 表＋§14.1/§14.2 词表对照） | 各节 |

**自审总结论**：本卡在文档设计层面未发现上述 18 类越界或失实；全部"未执行"事项已如实登记（§1.2 M-9、§17.1、§22）。本卡为 Draft，待评审；评审发现的偏差按 DTB §5.4 处理。
