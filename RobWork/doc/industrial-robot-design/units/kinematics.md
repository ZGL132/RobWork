# 工业机械臂设计软件 · kinematics 单元详细设计（阶段 B）

> 2026-09-22 新建（WP-15-T01 承接）。本文是 kinematics 单元的唯一详细设计：依据 REQUIREMENTS KIN-01～14 与 ARCHITECTURE §3.1/§4/§7.5/§7.6/§7.10 的分配，把输入快照与求解配置、FK/IK 契约、构型与解集、批量任务点验证、区域采样与覆盖率、策略调用、确定性/并行/缓存、evidence 与 execution 协作、公共接口与插件界面写到可直接实现的深度。本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案，2026-09-22；对应 development-task-breakdown.md WP-15-T01 交付物） |
| 日期 | 2026-09-22 |
| 状态 | **`Draft`**（待评审；DETAILED-DESIGN.md 单元状态表中的"kinematics｜待产出"以本卡落盘为准，索引行同步由治理侧执行，本卡不代改） |
| 文档代号 | UNIT-KIN |
| 单元 | kinematics（业务域单元，ARCHITECTURE §3.1：FK/IK/批量验证/区域覆盖、显示单位、求解配置；ARCHITECTURE §3.3 二分结构：零 Qt 计算库＋Qt Widgets 插件） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.12（`Draft`，2026-09-10）** |
| 协作输入 | `units/core.md` v0.11、`units/runtime.md` v0.15、`units/project.md` v0.18、`units/policy.md` v0.14、`units/io.md` 卡头 v0.9（§15.5 至 v1.0）、`units/diagnostics.md` v0.12、`units/evidence.md` v1.3（**§9 评估器接口/注册表已冻结为设计基线**：EvaluatorDescriptor/EvaluationRequest/EvaluationOutput/IEvaluationContext/IEngineeringEvaluator/IEvaluatorFactory/EvaluatorRegistry、§4.1.4 SamplingPlanRef、§6.4 五级汇总）、`units/execution.md`（存在，调度/取消/检查点经其公共契约消费）、`units/requirements.md` v0.2（同批业务卡：req.* 角色键、采样计划定义、必经状态语义）、`units/modeling.md` v0.2、`units/ui.md` v1.5（v1.6）、`units/testkit.md` v0.9——均为 **Draft/Draft-Structured（未冻结）**；本卡消费的签名以各卡当前文本为基线，冻结后按影响面增量同步（§14.3 P-KIN-7） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立）→ `units/*.md`（单元任务卡）。本文即 `units/kinematics.md`，按任务卡深度编写 |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/`（**现状：上级以 INTERFACE 占位注册 `sdurws_ird_kinematics`＋别名；单元级 CMakeLists.txt 不存在；`include/sdurws/ird/kinematics/` 仅 README.md 保留位——由 WP-15-T02 转真实目标**，见 §3.2、§11） |
| 任务归属 | `development-task-breakdown.md`（v0.16）WP-F／WP-15（T01～T17；T14～T17 为 R2/D 预留）；本文 §11 只做单元内部任务拆分与排序，不重排 WP 编号 |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送；Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`、逐个绝对路径启动 |
| 实现口径 | **从头构建**（REQUIREMENTS v1.9/v1.11 确立）：一切实现按需求与本文新建，不继承、不恢复任何历史实现源码。旧代码位置（用户提供 `D:\10_Source_Repos\old\src\rwslibs\kinematicanalysis`，实测 62 个文件、头文件 19 个约 4200 行）**仅作功能范围对照**（§2.5），不构成实现继承或正确性背书 |

---

## 目录

1. 文档信息、上游基线与目标（§1）
2. 需求承接与职责边界（§2，含 §2.5 旧代码功能对照）
3. 单元组成、依赖与公共头文件布局（§3）
4. 输入快照与求解配置（§4）
5. FK 与 IK 契约（§5）
6. 构型、解集与确定性排序（§6）
7. 批量任务点和区域覆盖（§7）
8. 策略调用、证据生成与执行协作（§8）
9. 公共接口与线程生命周期（§9，含 §9.9 插件界面设计与界面逻辑）
10. 验证方案及故障注入矩阵（§10）
11. 阶段 B 实现任务拆分（§11）
12. 后续阶段承接与接口交接清单（§12）
13. 需求—设计—验证追踪矩阵（§13）
14. 设计决策、风险、待裁决项与变更记录（§14）

---

## 1. 文档信息、上游基线与目标

### 1.1 文档定位

kinematics 是七阶段工作流的第三阶段（StageId 序列 `modeling → requirements → kinematics → …`，ui.md §6.4）的业务域单元：把任务需求（任务点/区域/工况）翻译成**运动学可达性事实**——FK/指标、多初值 IK 与解集、批量验证、区域覆盖与覆盖率——并把这些事实作为**证据明细**交给 evidence 汇总判定。本单元是优化（OPT-B 静态硬约束）与轨迹（IK 连续性）的计算供给方。

四条不变立场（全文贯穿，任何章节冲突时以此为准）：

1. **只算不判**：kinematics 产出领域计算结果与证据明细（EvaluationOutput）；工程判定（Feasible/EngineeringInfeasible/DataInsufficient 的最终裁定）、覆盖矩阵核对、当前性与正式通过资格**唯一归 evidence**（§8.1 汇总优先级；evidence §6.4 五级汇总）。
2. **搜索未果 ≠ 不可行**（C5/C8 铁律）：多初值未收敛、搜索未找到有效解（含全部解被限位/残差/碰撞过滤）、单个构型碰撞——三者一律产出**搜索未果记录/构型级过滤记录**，判 DataInsufficient 或仅过滤该解，**不得输出任务级不可行**；任务级确定性不可行仅限三类证明（解析界限/约束矛盾/必经状态碰撞），且由 evidence 校验裁定（§5.4）。
3. **只消费快照**：一切计算消费绑定修订的 AnalysisSnapshot＋RuntimeSnapshot（瞬态派生），不触 HEAD、不持可变 WorkCell、不经名称拼串定位对象（ARC-03/PA-3/R-4）。
4. **确定性优先**：同输入＋版本＋配置＋种子＋线程数→等价结果与稳定排序（NFR-COR-02）；并行分片确定性合并；显示单位/会话姿态不入任何身份（KIN-12/KIN-06/AT-04）。

### 1.2 上游与磁盘现状登记（2026-09-22 实测）

| 输入 | 磁盘版本 | 状态 | 本卡消费方式 |
| --- | --- | --- | --- |
| `REQUIREMENTS.md` | v1.16（2026-09-09，含 C1～C8） | `Accepted` | 需求语义与验收唯一权威；KIN-01～14、CON-01～06、EVI-01/02（§8.1 表 1~4＋搜索未果口径 C5/C8）、MDL-06/14/22、REQ 家族消费面、TASK-01～03、OPT-03/06/07、NFR-COR/PERF/REL、AT-03/04/05/09/10/11/19/27/34/37、附录 D 第 1~3/8/12 项 |
| `ARCHITECTURE.md` | v0.12（2026-09-10） | `Draft` | 二分结构（§3.3）、任务生命周期与取消（§4.3/§4.4）、迟到结果两段式（§4.5）、编译链与基座—世界不变量（§7.3）、策略权威（§7.5）、快照管线（§7.6）、会话态规则（§7.7）、评估器组合（§7.10） |
| `AGENTS.md`（仓库根） | 当前工作区版本 | 生效中 | 编码与协作约定 |
| `units/core.md` | v0.11 | `Draft-Structured` | ObjectId/ContentVersion/Units/Compare（runtimeAbsoluteTolerance）/EvaluationMode·TaskOutcome·EngineeringStatus 词表/DiagData/Events |
| `units/testkit.md` | v0.9 | `Draft-Structured` | 黄金数据集契约（`ird-golden-manifest/1`）、容差档案、边角样例（零值/近零/正负抵消） |
| `units/project.md` | v0.18 | `Draft` | ②查询端口/results 归档形态；KIN-14 设默认命令经①端口（命令处理器归 modeling，§9.7） |
| `units/evidence.md` | v1.3 | `Draft-Structured` | **§9 评估器接口与注册表**（EvaluatorDescriptor/EvaluationRequest/EvaluationOutput/IEvaluationContext/IEngineeringEvaluator/IEvaluatorFactory/manifest 握手）、§4.1 AnalysisSnapshot/InputSlice/DependencyKey、§4.1.4 SamplingPlanRef（sampleSetIdentity 公式）、§6.4 五级汇总与 DomainVerdictInputs、§5.3 失效矩阵、§13 B·kinematics 行 |
| `units/runtime.md` | v0.15 | `Draft-Structured` | RuntimeSnapshot/IRuntimeModelView（model/nameMap/workCell/worldToBase/gravityBase/makeState/nameResolver）、CanonicalModel（关节/限位/基座—世界/重力）、worker 物化（IRuntimeSnapshotFactory.materialize） |
| `units/policy.md` | v0.14 | `Draft-Structured` | IPolicyProvider.resolvePolicy、ICollisionEvaluator.createSession(policy, CollisionScene, IPolicyNameContext)（唯一碰撞实现；R-POL-2 禁直链 proximity）、IJointLimitEvaluator、JointThresholds（近限位比/条件数警告） |
| `units/execution.md` | 存在（经公共契约消费） | — | 任务注册/能力声明、取消（2 s/10 s 协议）、检查点、worker 池、结果归档（§8.5 消费面） |
| `units/diagnostics.md` | v0.12 | `Draft` | `KIN-` 前缀所有权（业务域词表 MDL/REQ/**KIN**/…）、IDiagnosticFactory/IDiagnosticSink |
| `units/requirements.md` | v0.2（同批业务卡） | `Draft` | req.* 角色键（`req.points/req.regions/req.conditions/req.sampling-plans`）、TaskPoint/WorkRegion/SamplingPlan 消费面、必经状态语义（level=Must∧enabled）、要求值与阈值分离 |
| `units/modeling.md` | v0.2（同批业务卡） | `Draft` | `tool-definition`/`apply-robot-design` 命令（KIN-14 设默认 TCP 的命令归宿，§9.7） |
| `units/ui.md` | v1.5（文末登记 v1.6） | `Draft` | IPluginUiRegistrar/IPluginUiModule、DomainReadinessItem、七态、View3D 契约、会话态（KIN-06 承载点） |
| `DETAILED-DESIGN.md` | 索引 | — | kinematics 行登记"待产出／WP-F" |
| `development-task-breakdown.md` | v0.16 | `Draft` | WP-15-T01～T17、§3 追踪矩阵 KIN 行、§5 构建约定、关键路径（WP-15-T03~T07 → optimization） |
| 代码落位 | `industrialrobot/kinematics/`：仅 `include/sdurws/ird/kinematics/README.md` 保留位 | — | 无单元级 CMakeLists.txt、无产品源码、无 `_plugin/_test` 目标（WP-15-T02 建）；上级门禁白名单无 kinematics 出边（§3.4 登记应增边） |

缺失文件登记：用户清单所列输入文件**全部存在**，无缺失；`units/kinematics.md` 本身此前不存在，即本卡（新建 v0.1）。

### 1.3 交付目标

本卡落盘后，后续单元应能依据统一契约：

- 对任务点和区域执行 FK/IK（多构型、批量求解、确定性排序）；
- 执行关节限位、姿态残差和策略（碰撞）约束校验，缺碰撞证据即数据不足；
- 生成冻结样本集与覆盖证据（分母＝计划样本总数；零样本→DataInsufficient）；
- 严格区分"找到有效解／多初值未收敛／搜索未找到有效解／全部候选被过滤／任务级确定性不可行"五类结局，前三类绝不升级为不可行；
- 通过 execution 提交长时批量计算（进度/取消/检查点/worker）；
- 通过 evidence 构造证据明细并交由其判定当前性与工程结论；
- 为 trajectory（③端口 IK）、optimization（OPT-B 静态硬约束）、reporting（结果归档）提供稳定输出。

### 1.4 非目标（本卡明确不做）

- 不实现轨迹规划、动力学、传动映射、选型、优化算法（trajectory/dynamics/drivetrain/selection/optimization）；
- 不实现碰撞算法（policy 唯一实现；本单元禁止直链 `sdurw_proximity`——R-POL-2）；
- 不实现证据汇总/工程判定/当前性/证据 Profile（evidence）；不实现调度/worker 生命周期/缓存淘汰（execution）；
- 不实现项目持久化/修订/草稿（project）；不实现 CSV/JSON 解析（io——结果导出的**数据**由本单元整理为值行，写出经 io 通道）；
- 不自行新增证据等级、判定阈值、当前性状态；不改动基座—世界变换与 RuntimeNameMap 语义；
- 不提前实现 R2 项的实现体（KIN-09/10/11/12-S1 → WP-15-T14~T17，§11 仅预留边界）；
- 本次不启动任何 GUI 程序（§10 仅设计测试流程）。

---

## 2. 需求承接与职责边界

### 2.1 KIN 家族承接总表

| 需求 | 摘要（语义以 REQUIREMENTS 原文为准） | 本卡设计落点 |
| --- | --- | --- |
| KIN-01 | 当前姿态 FK、TCP、Jacobian、奇异值、条件数、可操作度、关节裕量；统一尺度规则 | §5.2（FK/指标契约）、D-KIN-2（尺度规则定义） |
| KIN-02 | 多初值 IK；硬过滤（残差/关节/碰撞）→关节空间逐轴去重（附录 D 第 3 项）→裕量/可操作度/当前距离/稳定编号排序 | §5.4～§5.6、§6 |
| KIN-03 | 批量验证任务点：可行/工程不可行/数据不足三态分别报告 | §7.1 |
| KIN-04 | 区域采样：样本集合冻结（分母＝计划样本总数）；位置覆盖率（存在性口径）＋姿态覆盖率（全局口径）；数据不足样本保留分母整体降级；零样本→DataInsufficient | §7.2 |
| KIN-05 | 缺碰撞检测器必须返回数据不足，不得视为无碰撞 | §8.2 |
| KIN-06 | 双击任务/候选只改会话姿态：不改模型、不触发失效 | §9.9（L-K4）、§4.5（会话姿态不入身份） |
| KIN-07 | 三维显示失败点/薄弱区/碰撞对象/最差关节裕量 | §9.9（可视化数据页——渲染数据归本单元、呈现归 ui） |
| KIN-08 | 结果筛选、最差项排序、JSON/CSV 导出、批量复算 | §7.4、§9.9 |
| KIN-09（R2） | 工作空间采样可视化浏览 | §11 T14 预留 |
| KIN-10（R2） | 近似外包络（默认 180 方向，标注"近似、非精确"） | §11 T15 预留；§7.3（包络≠样本全覆盖） |
| KIN-11（R2） | 独立位姿可达性 | §11 T16 预留 |
| KIN-12 | 显示单位纯投影（m/cm/mm、deg/rad）；扩展单位 R2 子项 | §4.5、§11 T17 |
| KIN-13 | 求解配置（初值策略/数量、迭代上限、求解容差、去重阈值、采样预算、线程数）独立持久化、入运行/缓存身份；碰撞启用状态只读引用策略；判定阈值归 policy | §4.4（AnalysisConfiguration schema＋canonical） |
| KIN-14 | 当前 TCP/设备设为项目默认（经命令产生新修订，不破坏引用） | §9.7（命令门面——载荷写 modeling 对象） |

### 2.2 其他家族承接（与 kinematics 相关行）

| 需求 | kinematics 侧承接 |
| --- | --- |
| CON-01/02/04/05/06 | 只消费绑定修订的快照；结果经 execution 归档 results/<run-id>/；缓存命中兼容判定消费 evidence/execution 结论；切片依赖声明（§4.3）；同快照跨入口一致（CON-06/AT-19） |
| EVI-01/02 | 评估模式（Preview/Quick/Verified）词表唯一归 core/evidence；必验工况覆盖矩阵由 evidence 核对（本单元按 caseSubset 分批产出并逐工况标记完成）；搜索未果记录/不可行证明素材按 §8.1 口径产出 |
| MDL-06/14/22 | 消费双编译产物（RuntimeSnapshot）；对象解析一律经 RuntimeNameMap（⑥端口/nameResolver）；重力/基座—世界只读消费 `worldToBase()/gravityBase()`（AT-37 观测点：本单元重力相关量仅经快照，不自行投影） |
| REQ-01~12 消费面 | TaskPoint（位姿/容差/约束分量/三段）、WorkRegion（Box＋采样定义＋覆盖率目标）、OperatingCondition（要求值）、SamplingPlan（§5.2 定义）——以 `req.*` 角色键依赖声明消费（requirements.md §8.4） |
| TASK-01~03 | 任务能力声明（暂停/检查点/终止代价）；ResultEnvelope 合法组合由调用侧构造边界保证（本单元产出 EvaluationOutput，取消/失败不产正式证据）；身份五元组贯穿分批结果 |
| OPT-03/06/07 | 阶段 B 优化经③端口消费 Must 工位可达/Must 区域覆盖/碰撞静态子集/关节限位（OPT-B 硬约束）；Quick 筛选→Verified 复核；种子/线程入身份 |
| NFR-COR-01/02/03 | 解析算例黄金对照（附录 D 第 4/9 项容差）；并行归约容差（第 8 项 1e-12）＋稳定排序；非有限/非法不静默（NFR-COR-03——非有限关节值/位姿拒绝入算） |
| NFR-PERF-01~03 | >1 s 工作转后台（execution）；取消 2 s 进 Canceling/10 s 收敛；5,000 点/100,000 采样分批流式不分页装爆界面 |
| NFR-REL-02/03 | worker 崩溃仅当前任务失败；恢复后"已中断"如实标注（本单元不伪装完整结果） |
| AT-05/09/10/11/19/27/34/37 | 观测点见 §10 矩阵（V-16~V-25） |

### 2.3 拥有／消费／不拥有

| 类别 | 内容 |
| --- | --- |
| **拥有** | FK 计算契约与指标（Jacobian/奇异值/条件数/可操作度/关节裕量——含统一尺度规则定义）；IK 求解器接口与多初值策略；构型与解集模型（KinematicSolution/ConfigurationSignature/SolutionSet）；批量任务点验证评估器；区域采样**执行**与覆盖率计算（样本生成算法、样本集内容身份实现）；姿态残差/限位/收敛诊断（`KIN-` 码）；运动学评估器族（③端口 `IEngineeringEvaluator` 实现：pose-metrics/task-point-ik/task-points-batch/region-coverage）及其依赖声明；运动学证据明细（含解析界限证明素材、搜索未果记录）；求解配置（AnalysisConfiguration）schema、canonical 编码与确定性排序规则；结果筛选/导出的**数据整理** |
| **消费** | core：单位/位姿约定/身份/比较容差/EvaluationMode 词表/诊断契约；runtime：RuntimeSnapshot/IRuntimeModelView（Device/Frame/名称解析/worldToBase/gravityBase）、worker 物化；requirements：req.* 对象（经切片与闭包，无编译依赖）；policy：④端口 CollisionEvaluator/JointLimitEvaluator/已解析策略（只读）；evidence：AnalysisSnapshot/InputSlice/EvaluationRequest·IEvaluationContext·EvaluationOutput 契约、聚合判定（只交出、不调用判定权）；execution：任务提交/取消/进度/检查点/worker 物化/结果归档通道；diagnostics：稳定码注册/工厂/目录；project：②只读修订（经切片已覆盖）、results 归档（经 execution）；ui：参数编辑/结果投影/会话姿态承载/View3D |
| **不拥有** | RobotDesign/CanonicalModel 编译（modeling/runtime）；工程策略与碰撞算法（policy）；轨迹/动力学/传动/选型/优化；证据 Profile、工程判定最终裁定、结果当前性（evidence）；任务调度/进程管理/缓存淘汰（execution）；项目持久化/修订/草稿（project）；报告渲染（reporting）；CSV/JSON 解析（io）；需求定义与采样计划定义（requirements）；显示单位词表的存储语义（core Units＋ui 投影） |

### 2.4 边界红线自查锚点（与 §14.5 自审清单对应）

| 红线 | 本卡保障位置 |
| --- | --- |
| 不重复 runtime/policy/evidence/execution/requirements 职责 | §2.3 不拥有表；§5.1/§8.1/§8.5 边界 |
| 搜索失败/局部碰撞/全部过滤 ≠ 任务不可行 | §5.4 五类结局判定表（铁律）＋§6.5 边界图 |
| 必验工况/采样计划/覆盖分母不遗漏 | §7.1（caseSubset 完成标记）/§7.2（分母规则）/§4.3（req.sampling-plans 依赖） |
| 未冻结样本集不得进入正式证据 | §7.2（sampleSetIdentity 复核；不一致→DataInsufficient） |
| 显示单位不入计算依赖 | §4.5（身份外元素清单） |
| 无不稳定排序/线程漂移 | §6.3（稳定排序键）/§8.4（确定性并行合并） |
| Quick 不冒充 Verified | §4.4/§8.4（模式入身份；词表唯一） |
| 不绕过 RuntimeNameMap/基座—世界变换 | §5.1/§8.2（nameResolver/worldToBase 唯一来源） |
| 取消/失败后不发布完整结果 | §5.6/§8.5（取消传播；调用侧 envelope 构造边界） |
| 不引入未经上游批准的新状态/阈值/证据等级 | §14.4 登记制（词表均在 kinematics 所有权内；判定阈值零私设） |
| 不越权修改需求/架构/其他单元 | §14.3 待裁决集中登记 |

### 2.5 旧代码（`old/src/rwslibs/kinematicanalysis`）功能范围对照

口径声明（REQUIREMENTS 附录 A）：从头构建口径下旧代码**仅作功能范围对照**——本表把旧实现（62 个文件：六页 UI、ConfigurationEvaluator/TargetEvaluator/RegionCoverageEvaluator/OrientationCoverageEvaluator、KinematicBatchRunner 缓存、Workspace/PoseReachability 采样、Visualization/Envelope/Plot、ReportJson、自建碰撞检测器、项目设置文档）逐项映射到需求 ID 与本卡落点；**不构成实现继承或正确性背书**。图例：✅＝等价/增强承接；♻＝架构重定位承接；⏸＝分期承接；❌＝按上游裁决或需求范围不承接。

| 旧功能（来源） | 需求/处置（附录 A） | 本卡落点 | 承接状态 |
| --- | --- | --- | --- |
| 三工作流页＋报告页（Diagnose/Validate·TaskPoints/Workspace/PoseReachability/Visualization/Report，KinematicAnalysisWidget） | KIN 家族·AT-03｜等价承接 | §9.9（四面板重组：位姿指标/任务点验证/区域覆盖/结果与可视化；Validate 页需求源从冻结工件改为快照闭包） | ✅（信息架构重组） |
| 当前位姿诊断（KinematicAnalyzer.analyzeCurrentPose：FK/Jacobian/SVD/条件数/可操作度/关节裕量） | KIN-01·AT-03｜等价承接 | §5.2（FK/指标契约＋解析对照黄金） | ✅ |
| 单点 IK（TargetEvaluator：seedCount=8 多初值/maxSolutions=64/评分排序/安全应用） | KIN-02·AT-03｜等价承接 | §5.4～§5.6 | ✅ 增强（初值策略/去重/排序按附录 D 第 1~3 项与 KIN-13 口径重定义；旧 1e-4 去重阈值→1e-6 逐轴默认） |
| IK 候选评分（KinematicMetrics.computeSolutionScore 综合评分） | KIN-02｜附录 B"加权总分"相邻语义 | §6.3（排序键＝裕量/可操作度/距离/稳定编号，KIN-02 明文；无综合标量分） | ♻（综合评分→多键稳定排序，防加权总分复活） |
| "Refresh TCP Pose to IK Target"（当前位姿回填 IK 目标） | KIN-01/06·AT-27｜等价承接 | §9.9 L-K5（会话读＋显示投影） | ✅ |
| 设为项目默认 TCP/设备 | KIN-14·AT-27｜等价承接 | §9.7（经①端口提交 modeling `apply-robot-design` 载荷；命令处理器归 modeling） | ♻（从界面直写改为命令端口） |
| 阈值对话框（KinematicThresholds 八阈值：近限位比/条件数警告·失败/奇异值/可操作度警告/容差/去重阈值） | KIN-13（判定阈值归 WP-07 策略）·AT-27｜口径拆分（附录 A.4 行） | §4.4（容差/去重阈值＝分析配置默认，附录 D）；近限位比/条件数警告→policy JointThresholds（④端口只读） | ♻（阈值权威重定位：插件不再自持阈值） |
| 任务点表（TaskPointTableModel：增删/批量分析/局部选择分析） | KIN-03·AT-03｜等价承接 | §7.1、§9.9（任务点验证面板；任务点**定义**归 requirements——表格为消费视图） | ♻（任务点真值归 requirements，本单元只消费） |
| 任务点 CSV 导入/导出 | KIN-08/REQ-05｜等价承接 | §7.4（导出数据整理＋io 通道；导入归 requirements REQ-05 通道，本单元不重复建导入） | ♻ |
| 冻结需求导入适配器（FrozenRequirementKinematicAdapter） | 附录 B 裁决排除（冻结工件双账本） | §4.1（需求经 AnalysisSnapshot 闭包消费） | ❌→✅ 按上游裁决 |
| 工作空间采样（KinematicAnalysisWorkspace：Random/Grid、上限 1e6 样本/100 步、sanitize 诊断） | KIN-09·AT-25｜分期承接（R2） | §11 T14 预留；§7.2（区域覆盖采样为 R1 正式语义——与旧"探索式工作空间"分立） | ⏸ R2（旧探索式采样与 R1 区域覆盖采样语义分立：前者 Quick/估算性质，后者冻结样本集） |
| 位姿可达性（PoseReachability：斐波那契螺旋方向×滚转、计划预览、协作取消、方向覆盖率） | KIN-11·AT-26｜分期承接（R2） | §11 T16 预留（方向集生成器设计已在 §7.2 登记，R2 复用） | ⏸ R2 |
| 区域覆盖评估（RegionCoverageEvaluator：格点＋朝向覆盖率分别统计） | KIN-04·AT-03｜等价承接 | §7.2 | ✅ 增强（样本集冻结/分母＝计划总数/数据不足样本保留分母/零样本→DataInsufficient——R3/R8 口径） |
| 朝向目标采样（OrientationCoverageEvaluator：方向×滚转、可达判定） | KIN-04/11｜等价承接 | §7.2（方向集定义黄金锁定） | ✅ |
| 可视化（VisualizationTypes：点云着色/XY·XZ·YZ 投影/散点·包络/标量模式/PNG 导出） | KIN-07/09·AT-25｜等价承接（KIN-07 R1 部分＝失败点/薄弱区/最差裕量渲染数据） | §9.9（渲染数据归本单元；呈现归 ui View3D/绘图契约） | ♻（自绘 plot→ui 视图契约；KIN-09 完整点云 R2） |
| 近似外包络（KinematicAnalysisEnvelope：180 方向射线 Rmax、异步＋防过期 generation、缓存键含关节界限） | KIN-10·AT-26｜分期承接（R2） | §11 T15 预留；§7.3（包络≠样本全覆盖） | ⏸ R2 |
| 绘图对话框/PNG 导出（KinematicPlotDialog） | KIN-09·AT-25｜分期承接 | §11 T14 预留 | ⏸ R2 |
| 可视化点回写 3D（visualPointClicked→setState） | KIN-06/09·AT-25｜分期承接（回写语义 R1 已由 KIN-06 承载） | §9.9 L-K4（会话姿态，零修订） | ✅（语义 R1 承载）/⏸（点云来源 R2） |
| 报告 JSON/CSV（KinematicAnalysisReportJson：筛选/最差排序/provenance） | KIN-08·AT-03｜等价承接 | §7.4（结果归档进 results/<run-id>/；导出经 io；provenance→快照身份） | ♻（报告工件归 reporting/results，插件导出为数据副本） |
| 批量评估与缓存（KinematicBatchRunner：maxBatchSize=128＋KinematicBatchCacheKey{模型/环境/需求指纹＋stage＋configHash＋seed}） | KIN-03/CON-04·AT-03｜等价承接（缓存键重构——附录 A.4 行） | §7.1/§8.4（缓存键＝切片内容身份＋评估键＋契约版本＋模式；指纹→排除） | ♻ |
| 自建碰撞检测器（KinematicAnalysisCollision.makeKinematicAnalysisCollisionDetector） | KIN-05/ARC-05·AT-19｜等价承接（碰撞统一评估——附录 A.4 行） | §8.2（policy ICollisionEvaluator 唯一实现；禁直链 proximity——R-POL-2） | ❌→✅ 按上游裁决（自建检测器废除） |
| 域内工程判定枚举（Feasibility/Quality＋RequirementValidationSummary Must 聚合） | EVI-01/CON-02｜附录 A"等价承接（以修订＋当前性实现）" | §8.3（本单元只产 EvaluationOutput；聚合判定唯一归 evidence aggregateVerdict；per-item 计算状态保留但非工程结论） | ♻（判定权上收 evidence） |
| AnalysisContext（持 WorkCell*/Device*/State/CollisionDetector 裸指针族） | ARC-03/PA-3｜架构重定位 | §4.2（只持 RuntimeSnapshot 只读视图；state 经 makeState() 每线程拷贝） | ♻ |
| 项目设置文档（KinematicAnalysisProjectDocument：设备/TCP/单位/采样配置/可视化偏好持久化） | KIN-13/PM-14·AT-27｜等价承接（设置分层） | §4.4（AnalysisConfiguration 入运行/缓存身份＋用户级持久化）；可视化偏好属 ui 会话偏好不入身份 | ♻（拆分：计算配置入身份，呈现偏好归 ui） |
| Estimated/Quick/Verified 证据阶段枚举 | EVI-01｜词表统一 | §4.4（core EvaluationMode Preview/Quick/Verified 唯一词表；"估算"语义并入 Preview/Quick 消费约束） | ♻ |
| 单位换算 helper（米↔in/grad/turn 等） | KIN-12·AT-27｜等价承接 | §4.5（core Units 唯一入口；inch/grad/turn 为 R2 扩展 token） | ♻ |
| 失败原因枚举（KinematicFailureReason 15 值：NoDevice/NoTcpFrame/IkNoSolution/Collision/TargetResidual/JointLimit/Near*/Singular*/InvalidTarget/SolverError/CollisionDetectorUnavailable/FrameNotFound） | ERR-01/KIN 诊断·AT-03｜等价承接 | §5.5/§9.6（`KIN-` 稳定码映射表——语义保留、码值重登记） | ✅ |
| 插件动态加载元数据（Q_PLUGIN_METADATA） | NFR-SEC-04｜附录 B 裁决排除 | §3.1/§3.2（静态白名单） | ❌→✅ 按上游裁决 |

**对照结论**：需求级功能**全覆盖**——等价/增强承接 10 项、架构重定位 10 项、分期（R2）5 项、按上游裁决不承接 3 项（均注明理由与替代语义）；R1 部分不实现旧"探索式工作空间/位姿可达性/外包络"（KIN-09/10/11 为 R2 需求，§11 预留）；判定权、碰撞实现、阈值权威、指纹缓存四处按新架构上收/重定位；实现层面旧代码零拷贝。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 单元组成（ARCHITECTURE §3.3 二分结构）

```
sdurws_ird_kinematics            计算库（零 Qt，STATIC）：FK/指标、IK 求解器、解集模型、
                                 四个评估器（pose-metrics/task-point-ik/task-points-batch/
                                 region-coverage）、采样执行与样本生成、解析界限素材、
                                 AnalysisConfiguration 编解码——可被模型测试直调（NFR-MNT-01）
sdurws_ird_kinematics_plugin     插件界面（Qt Widgets）：位姿指标/任务点验证/区域覆盖/结果与
                                 可视化面板（§9.9）；只消费计算库＋ui 端口＋results 投影
sdurws_ird_kinematics_test       单元测试（googletest；解析算例黄金对照）
sdurws_ird_kinematics_contract_test  跨单元契约测试（runtime/policy/evidence/execution 对端面）
```

- 命名空间：`sdurws::ird::kinematics`（测试 `sdurws::ird::kinematics::test`）；命名/include guard 风格同 modeling/requirements 卡；C++17 显式、`/utf-8`；线性代数用 RobWork 基线类型＋自有 SVD 小实现或基线（不引入新第三方——如需 Eigen 级性能走 DTB 依赖登记，P-KIN-6）。
- `_worker` 目标：**不新建**——worker 进程复用本计算库（execution WorkerLauncher 派发＋评估器工厂 manifest 握手，ARCH §4.1；evidence §9.4 主/worker 一致性）。

### 3.2 目标升级与门禁边登记（WP-15-T02 执行清单）

- `kinematics/CMakeLists.txt` 新建：`sdurws_ird_kinematics` INTERFACE→STATIC；`_test`/`_contract_test` 注册（自持 `ird_add_gtest`）；`_plugin` 随 WP-15-T12；配置期红线守卫自持（R-1/R-3 FATAL_ERROR）。
- `ird_gates` 白名单应增边（WP-15-T02 登记；均属 §3.5"业务域→L2/L3"既有许可方向实例化）：

| 边（kinematics →） | 用途 |
| --- | --- |
| core | 身份/单位/比较/EvaluationMode 词表/诊断契约/事件 |
| diagnostics | 稳定码注册、IDiagnosticFactory/IDiagnosticSink |
| runtime | IRuntimeModelView/RuntimeSnapshot 只读消费（worker 物化入口） |
| policy | ④端口 ICollisionEvaluator/IPolicyProvider/IJointLimitEvaluator 只读调用 |
| evidence | IEngineeringEvaluator/EvaluationRequest/IEvaluationContext/EvaluationOutput 契约实现、DependencyDeclaration 值 |
| execution | 任务提交/取消令牌/检查点/结果归档通道（接口消费） |

- 插件目标 → 本计算库＋`sdurws_ird_ui`；**禁止链接任何其他业务域单元（R-1）**——对 requirements/modeling 对象的消费只经切片字节＋各自卡的 canonical 语义约定（不 include 其头）。

### 3.3 公共头文件布局（`kinematics/include/sdurws/ird/kinematics/`，随 §11 落位）

| 头 | 内容 | 任务 |
| --- | --- | --- |
| `AnalysisConfig.hpp` | AnalysisConfiguration schema＋`IAnalysisConfigurationCodec`（canonical 编码，入 configurationRefs） | T10 |
| `KinTypes.hpp` | KinematicSolution/ConfigurationSignature/SolutionSet/SampleRecord/CoverageResult 值模型、结局枚举（§5.4 五类结局） | T03/T04 |
| `Fk.hpp` | `IFkEvaluator`＋`PoseMetrics`（KIN-01） | T03 |
| `Ik.hpp` | `IIkSolver`＋`IkRequest/IkOutcome`（KIN-02） | T04 |
| `SolutionSet.hpp` | `IKinematicSolutionSet`（去重/排序/统计视图） | T04 |
| `Sampling.hpp` | `IWorkspaceSampler`＋样本生成（计划→确定性样本集；sampleSetIdentity 实现） | T06 |
| `Coverage.hpp` | 覆盖率计算值类型（分母/分子/降级标记） | T06 |
| `Evaluators.hpp` | 四个 `IEngineeringEvaluator` 实现与 descriptor（依赖声明 §4.3） | T03~T07 |
| `Evidence.hpp` | `IKinematicEvidenceBuilder`（EvidenceItem/搜索未果记录/解析界限素材组装） | T05/T07 |
| `Bounds.hpp` | 解析工作半径界限（不可行证明素材） | T04 |
| `Commands.hpp` | `IKinematicsCommandHandler`（KIN-14 设默认门面） | T08 |
| `Errors.hpp`/`DiagCodes.hpp` | `KinematicsError` 与 `KIN-` 稳定码清单（§9.6） | T02 |
| `README.md` | 已存在保留位 | — |

### 3.4 线程与确定性总约定

1. 纯函数服务（Fk/Ik/SolutionSet/Sampling/Coverage/Bounds/Codec）：无共享可变状态、可重入；评估器实例线程安全性按 descriptor.threadSafety 声明（建议 FullyThreadSafe——无跨调用状态）。
2. 评估器在**工作线程**运行（execution 派发）；一切取消经 `IEvaluationContext::cancellationRequested()` 周期查询（协作取消，长评估每批/每 N 次迭代查询一次）。
3. 一切数值恒 SI（m/rad/N·m）；比较一律附录 D 通用比较公式（逐元素）；排序比较不引入容差（浮点全序＋稳定键兜底，§6.3）。
4. 随机性唯一来源＝AnalysisConfiguration.seed 派生的确定性序列（同 seed 同序列）；禁止读时钟/环境熵。

---

## 4. 输入快照与求解配置

### 4.1 计算输入总览（AnalysisSnapshot→FK/IK→Evidence→ResultEnvelope 流程图）

```
当前修订（req-* / robot-design / tool-definition / scene-object 同闭包）
   │ 快照组装（evidence/请求方，L5）—— 冻结：全部 (oid,cv) 对锚定修订校验（防混入）
   ▼
AnalysisSnapshot{ objectClosure, caseSet(RequiredCaseSet), samplingPlans(SamplingPlanRef),
                  configurationRefs{config.ik…}, policyRef, nameMapRef,
                  environment{runtime.model-identity, runtime.robwork-baseline},
                  snapshotId }                                        ← 冻结输入（唯一可算形态）
   │ 依赖分析（DependencyDeclaration → InputSlice，sliceId=缓存键）
   ▼
EvaluationRequest{ task 五元组, mode(Preview/Quick/Verified), snapshot, slice, caseSubset }
   │  evaluation host（execution/L5）：RuntimeSnapshot 物化（worker: IRuntimeSnapshotFactory
   │    .materialize；主进程: 编译缓存）＋评估器实例（registry.create()）
   ▼
kinematics 评估器（本单元）：IK/FK/采样 ＋ ④端口碰撞 ＋ ⑥端口名称解析
   │  EvaluationOutput{ evidence[], proof?, searchRecord?, verdictInputs, payload, diagnostics }
   ▼
调用侧（execution/域）：aggregateVerdict（evidence 五级汇总）→ ResultEnvelope::make（构造边界校验）
   → 写 results/<run-id>/（绑定修订）→ ResultCurrentness（evidence）
```

- **输入如何冻结**：快照组装即冻结（evidence §4.1.5 builder 单一校验路径）；kinematics 不接受任何"闭包外"对象字节——`tryObjectBytes(oid,cv)` 未命中即证据缺失（不回退 HEAD）。
- **不得混用多个修订**：一个 EvaluationRequest 一个 snapshot；评估器内禁止二次解析其他修订（review 红线＋契约测试 V-15）。
- **同一模型不同项目隔离**：身份含 project/branch/revision（TaskIdentity＋snapshotId）——同内容不同项目 ObjectId 互异（requirements V-18 同源口径）。
- **旧快照支持历史结果**：RuntimeSnapshot 编译后零外部依赖（runtime §9.2）——迟到的批量结果按其任务五元组归档原修订，当前性由 evidence 独立判定（AT-10 联合）。

### 4.2 RuntimeSnapshot 消费面（本单元视角）

| 消费 | 用途 | 约束 |
| --- | --- | --- |
| `model()`（CanonicalModel） | 关节（类型/轴/origin/zeroOffset/限位/工作范围/速度加速度限）、连杆几何引用、工具/TCP、`T_world_base`（唯一来源，M-11）、`gravityWorld` | 只读；不缓存跨请求（每请求重建视图） |
| `nameMap()`/`nameResolver()` | ObjectId↔RuntimeName（诊断定位、结果标注、碰撞对象对） | 前缀操作唯一合法位（R-4） |
| `makeState()` | 每线程独立 State 拷贝（rw 计算入口） | 不修改快照内 state |
| `workCell()`/`tryDynamicWorkCell()` | 组装 policy CollisionScene 的几何来源 | 只读视图（WorkCellConstView） |
| `worldToBase()/gravityBase()` | 世界系位姿解释、重力相关量（AT-37：仅经快照） | 禁止二次旋转 |

### 4.3 依赖声明（四个评估器的 DependencyDeclaration；键词表与 evidence §9.3 示例同源）

| 评估键 | 依赖声明 | 模式 |
| --- | --- | --- |
| `kin.pose-metrics`（KIN-01） | `model.robot-design`(Object)、`tcp`(Object→tool-definition)、`policy.resolved`(Policy)、`namemap`(NameMap)、`config.ik`(Configuration) | Preview/Quick/Verified |
| `kin.task-point-ik`（KIN-02） | 上述＋`req.points`(Object) | Preview/Quick/Verified |
| `kin.task-points-batch`（KIN-03） | 上述＋`req.conditions`(Object)（caseSubset 分批） | Quick/Verified |
| `kin.region-coverage`（KIN-04） | `model.robot-design`、`tcp`、`req.regions`、`req.sampling-plans`、`req.conditions`、`policy.resolved`、`namemap`、`config.ik`、`collision-models`(Object, Conditional←policy 启用) | Quick/Verified |

- 依赖即失效面：任一声明条目 cv 变化→sliceId 变化（CON-05）；**TCP 变更失效运动学及下游**经 `tcp`/`model.robot-design` 键成立（AT-05①）；电机成本（selection 域）未声明→不失效（AT-05②方向）。
- 条件依赖（Conditional）语义：碰撞启用状态只读自 policy（V13-01）——策略未启用碰撞时 `collision-models` 键不进切片；本单元**不设**碰撞布尔开关。

### 4.4 AnalysisConfiguration schema（KIN-13；canonical 编码登记）

```cpp
struct AnalysisConfiguration {                     // schemaVersion = 1（演进即新版本）
    InitialValueStrategy initialStrategy;          // ReferenceQ | SeededRandom | JointGrid
    std::uint32_t initialValuesCount;              // 多初值数量（≥1）
    std::uint32_t iterationLimit;                  // 单初值迭代上限
    double positionResidualTolerance;              // m；默认 1e-6（附录 D 第 1 项）
    double orientationResidualTolerance;           // rad；默认 1e-6（附录 D 第 2 项）
    double ikDedupThresholdPerAxis;                // rad/m；默认 1e-6 逐轴（附录 D 第 3 项）
    RegionSamplingBudget regionBudget;             // 采样预算/线程数（求解域参数）
    std::uint64_t seed;                            // 确定性随机种子（0 非法——I-KIN-4 拒绝，
                                                   //   不做 0→1 静默替换，NFR-COR-03）
    // 无碰撞开关（V13-01：碰撞启用只读引用策略）；无判定阈值（归 policy，§6.3 分离）
};
```

- **canonical 编码**：定宽小端＋字段定序＋集合字典序（同 requirements/modeling 编码纪律）；`configDigest`＝SHA-256——进入快照 `configurationRefs{configKindToken="config.ik", digest}` 与 sliceId（D-04：入 sliceId、不入 inputBaselineId——求解配置改变不改变样本基准，evidence §4.1.4）。
- **持久化**：用户级设置通道（PM-14"求解配置等分析设置独立持久化"；不入 .rwdesign 修订）；修改后按实际依赖提示受影响结果需重算（AT-27）。
- **模式**：EvaluationMode（Preview/Quick/Verified）随请求进入身份；**Quick 结果不得作为 Verified 正式证据**（EVI-01 表 1——Quick 仅筛选/排序依据）。

### 4.5 身份外元素（明确不入计算身份）

| 元素 | 语义 | 依据 |
| --- | --- | --- |
| 显示单位（m/cm/mm、deg/rad） | 纯投影；切换不改 SI 真值、不产生修订、不触发重算 | KIN-12/AT-27 |
| 会话姿态（双击任务/候选/动画/可视化点回写/复位 Home） | ui 会话态；不产生修订、不进缓存身份 | KIN-06/AT-04/ARCH §7.7 |
| 排序偏好、面板筛选、可视化着色模式 | ui 呈现偏好 | §2.5（旧项目文档拆分结论） |
| 进度/临时缓存/上一 runs 的会话残留 | execution 会话治理 | §2.5（旧 projectDocumentSnapshot"分析结果不入快照"同则） |

---

## 5. FK 与 IK 契约

### 5.1 模型获取与坐标解释（唯一来源声明）

- 模型**只能经 runtime**：评估器从请求绑定的 RuntimeSnapshot 取 `IRuntimeModelView`；关节角权威换算遵循 `q_authoritative = q_zeroOffset + q_rw`（CanonicalJoint 口径）——本单元对内计算一律使用权威 q。
- **不重复定义坐标变换**：目标/结果的参考系解释（refFrame→world→base）只经快照模型（frame 静态链＋`worldToBase()`）；任务点位姿的 refFrame 解析使用模型默认 state（固定 frame 与 state 无关；DAF/动系不在 R1 模型语义内——modeling §2.5 已排除）；**禁止在本单元实现第二套变换代数或基座旋转**（M-11/AT-37 观测点：重力/世界量仅经 `gravityBase()/worldToBase()`）。
- **不使用名称拼接定位对象**：碰撞对、几何、Frame 定位一律 ObjectId→nameMap（R-4）。

### 5.2 FK 与位姿指标（KIN-01；`IFkEvaluator`）

| 项 | 契约 |
| --- | --- |
| 输入 | 权威关节向量 q（逐关节 SI：转动 rad／移动 m）；TCP 引用（tool-definition oid＋tcpKey→快照解析） |
| 输出 | `PoseMetrics{ tcpInBase: Transform3D（m/rad）, tcpInWorld: Transform3D（经 R_world_base，只读消费）, jacobian 6×n（行优先）, singularValues 降序, conditionNumber, manipulability, jointMargins 归一化逐关节 + minimumJointMargin }` |
| 统一尺度规则（D-KIN-2） | Jacobian＝基础雅可比（平移行 m、转动行无量纲 rad），不做关节加权归一；可操作度 w＝√det(J·Jᵀ)；条件数＝σmax/σmin（奇异→+∞，以 `isFinite` 标记承载，不静默截断）；规则随黄金数据集锁定，修改走设计变更 |
| 非法输入 | q 维度不符/非有限→`KinematicsError(IllegalQ)` fail-fast（NFR-COR-03，不钳制不置零） |
| 超限位 q | FK **可计算**（几何上有效）；限位评价交由调用方（对比 bounds→违例素材）——FK 失败与限位违例是两件事 |
| 不可用设备/TCP | 无 device/无 TCP/引用悬空→结构化错误（`KIN-NO-DEVICE`/`KIN-NO-TCP` 素材）——**FK 失败 ≠ 工程不可行**，只说明模型/引用不可用 |
| 确定性 | 纯函数：同 (snapshot, q, tcp) → 同字节输出（含浮点布局） |
| 正反向验证 | FK(q)→（黄金 IK 抽样）IK(FK(q)) 含 q（去重容差内）→FK 一致——黄金数据集必测（V-01）；基座—世界：tcpInWorld 与快照 `worldToBase` 复核一致（V-02，AT-37 观测点） |

### 5.3 IK 求解契约（KIN-02；`IIkSolver`）

**IkRequest**：目标位姿（经 §5.1 解析的 base 系 TCP 目标）、容差（AnalysisConfiguration 两残差阈值）、初值集（按策略生成：ReferenceQ［显式参考构型］/SeededRandom［seed 派生确定性序列］/JointGrid［限位空间均分，DOF 截断规则黄金锁定］）、迭代上限、关节 bounds（来自快照，含 continuous 工作范围）、**去重阈值**（逐轴）、碰撞会话句柄（policy 启用时）、取消令牌、`referenceQ`（排序用参考构型）。

**求解流程（时序图）**：

```
初值集生成（确定性；seed→序列） ──► 逐初值数值迭代（含阻尼最小二乘；周期查询取消令牌）
   │ 每个收敛候选 q̂（残差 ≤ 两容差，逐项）
   ▼
硬过滤（顺序固定）：①残差复验（FK 复算）②关节限位（含 continuous 工作范围）
   ③碰撞（policy 会话；未启用→跳过且标记 collisionNotEvaluated）
   │ 每个被过滤解：记录过滤原因（构型级，不下结论）
   ▼
去重：关节空间逐轴比较（|Δq_i| ≤ dedupThreshold；continuous 按工作范围直接比较、
   无跨周取模——附录 D 第 3 项/C1；去重对象是构型而非位姿——同位姿异构型均保留）
   ▼
稳定排序（§6.3 排序键） ──► IkOutcome{ solutions[], filtered[], stats, outcomeKind(§5.4) }
```

### 5.4 五类结局判定（铁律；对应 evidence §8.1 搜索未果口径 C5/C8）

| # | 结局（`IkOutcomeKind`） | 判定条件 | 产出 | 下游语义 |
| --- | --- | --- | --- | --- |
| 1 | `SolutionsFound` | 去重后存在 ≥1 个通过全部硬过滤的解 | SolutionSet＋evidence 明细 | 可行性素材（判定仍归 evidence） |
| 2 | `MultiInitNoConvergence` | 全部初值迭代至上限未收敛（零候选） | **SearchExhaustedRecord**{预算、已试初值数、迭代统计}→DataInsufficient | **不得输出不可行**（可扩大初值/预算后按同一冻结输入复评——§8.1 搜索未果口径） |
| 3 | `AllCandidatesFiltered` | 有收敛候选但全部被限位/残差/碰撞过滤 | SearchExhaustedRecord＋**逐解过滤记录**（原因/对象对）→DataInsufficient | **不得输出不可行**（含"全部因碰撞被过滤"——构型级碰撞仅过滤该解，C8） |
| 4 | `PartialCollision`（中途形态，非终局） | 部分解碰撞、其余有效 | SolutionSet（碰撞解带标记保留诊断价值） | 该解被过滤；任务结论不受单解影响 |
| 5 | `AnalyticBoundExceeded`（证明**素材**，非裁定） | 目标位置超出解析工作半径上界（连杆长度和，静态可验证、覆盖全部可能解） | `DeterministicInfeasibilityProof` 候选（kind=解析界限，附推导输入） | evidence `validateProof` 校验后才能成立任务级不可行——**kinematics 只产素材，不裁定** |

- 铁律表述：**1↔2/3 互斥；4 是 1 的子形态；2/3/4 任何组合都不得升级为"任务级确定性不可行"**；5 是唯一允许产出证明素材的路径，且证明成立与否由 evidence 校验（expectedSliceId 绑定、D-09"存在性≠有效性"）。
- 取消/超时：不是结局——取消令牌触发后立即返回 `cancelled=true` 载荷（无终局字段）；单初值超预算（迭代上限）计入迭代统计，属 2 的素材。

### 5.5 失败分类与诊断（承接旧 KinematicFailureReason 语义 → `KIN-` 码）

`KIN-NO-DEVICE`／`KIN-NO-TCP`／`KIN-TARGET-ILLEGAL`（目标非法：非有限/容差非法）／`KIN-SOLVER-INTERNAL`（求解器内部错误——fail-fast 轨道）为**错误**；`KIN-RESIDUAL-EXCEEDED`／`KIN-JOINT-LIMIT-VIOLATED`／`KIN-NEAR-LIMIT`／`KIN-NEAR-SINGULAR`／`KIN-COLLISION-FILTERED`／`KIN-SEARCH-EXHAUSTED`／`KIN-COLLISION-UNAVAILABLE`（缺检测器→DataInsufficient 素材，KIN-05）为**评价/证据**级（映射表登记 §9.6）。比较型诊断带实际值/期望值/单位（残差 m·rad、裕量比无量纲）。

### 5.6 结果绑定

一切结果（PoseMetrics/IkOutcome/SampleRecord/CoverageResult）在 payload 中携带 `{snapshotId, sliceId, evaluationKey, contractVersion, mode, task 五元组}`——与对象（pointOid/regionOid/conditionId）和快照双重绑定；归档后可由 reporting/evidence 溯源（NFR-COR-04）。

---

## 6. 构型、解集与确定性排序

### 6.1 值模型（`KinTypes.hpp`）

| 类型 | 字段（要点） | 身份/生命周期 |
| --- | --- | --- |
| `KinematicSolution` | `q`（权威 SI）、`residual{pos m, ori rad}`、`jointMargins[]`＋min、`manipulability`、`conditionNumber`、`collisionStatus{evaluated, inCollision, objectIdPairs[]}`、`sourceInitIndex`、`iterations`、`solverContractVersion` | 值语义；随 payload 归档 |
| `ConfigurationSignature` | q 的 canonical 编码（全精度定宽） | **构型身份**：区分"同一构型"用于记录/统计；**不用于去重**（去重是容差比较，见 I-KIN-3） |
| `SolutionSet` | `targetRef{pointOid, conditionId?}`、`requestIdentity{snapshotId, sliceId, configDigest, mode, seed}`、`solutions[]`（稳定排序）、`filteredRecords[]`、`searchRecord?`、`statistics{rawCount, convergedCount, dedupedCount, filteredCount}` | 结果身份＝requestIdentity＋payload canonical 摘要——**构型身份 ≠ 结果身份**（同构型在不同请求/模式下是不同结果条目） |
| `SampleRecord`（§7.2） | `sampleIndex`（冻结序）、`kind{Position, Orientation}`、坐标/姿态、`status{Reached, Unreachable, DataInsufficient, NotApplicable}`、bestSolutionRef? | 按 sampleIndex 对齐计划（分母完整性核查键） |

- **关节角周期归一化**：R1 **不做**跨周归一化——有限限位关节 q 天然在 [qmin,qmax]；continuous 关节按已确认工程工作范围（MDL-12）直接比较、无跨周取模（附录 D 第 3 项/C1 明文）；超出工作范围的候选按过滤记录处理。归一化语义若未来需要（如无工作范围 continuous）走需求变更。
- **重复解判定**：去重＝求解期**成对容差比较**（逐轴阈值），不是哈希等价——`ConfigurationSignature`（精确编码）仅作记录键，**数值容差不能直接作为哈希等价**（I-KIN-3：两个相差 1e-9 rad 的解哈希不同但去重等价，反之亦然；契约测试锁定）。
- **部分解集的诊断价值**：碰撞解/被过滤解保留在 `filteredRecords`（原因、对象对、指标）——供 UI 检查器与报告解释"为什么少了解"，但**不计入解集排序序列、不进入可行素材**。

### 6.2 `IKinematicSolutionSet` 视图契约（排序/筛选/统计的唯一定义点）

```cpp
/// 解集只读视图：稳定排序/筛选/统计的唯一实现（NFR-MNT-04——UI/报告不各自排序）。
class IKinematicSolutionSet {
public:
    /// @brief KIN-02 稳定排序（已排序集合上幂等）。排序键见 §6.3；不可变操作返回新视图。
    virtual const SolutionSetView& sorted() const = 0;
    /// @brief 筛选（可用解/含碰撞诊断解）；筛选谓词由调用方给出但比较实现在此（一致性）。
    virtual SolutionSetView filtered(const SolutionPredicate& p) const = 0;
    virtual SolutionSetStatistics statistics() const = 0;
    /// @brief 最差项查询（KIN-08 最差排序的规范来源：裕量最小/条件数最大/残差最大）。
    virtual std::optional<SolutionRef> worstBy(WorstMetric m) const = 0;
};
```

### 6.3 稳定排序键（KIN-02 明文四键；D-KIN-3 精确化）

```
排序键（自左向右字典序；同键稳定按 sourceInitIndex 升序兜底——总序、与线程/分片无关）：
  1) minimumJointMargin      降序（裕量大的优先）
  2) manipulability          降序（可操作度高优先）
  3) ‖q − referenceQ‖₂       升序（"当前距离"——referenceQ 是显式评估输入，§D-KIN-4）
  4) stableIndex             升序（初值序：去重保留组内代表＝该组 sourceInitIndex 最小者）
浮点比较为全序比较（无容差）；记录键 canonical；同输入重复排序结果逐位一致（V-04）。
```

- `referenceQ` **显式化**（D-KIN-4）：进入 EvaluationRequest（默认＝模型 Home 构型），身份含之；**禁止隐式读取 UI 会话姿态**参与排序——会话姿态只可作为 UI 填充默认值的呈现来源（KIN-06/AT-04 边界）。

---

## 7. 批量任务点和区域覆盖

### 7.1 批量任务点验证（KIN-03；`kin.task-points-batch`）

**批量执行图（任务点×工况×分批）**：

```
切片内 req.point-set（启用条目）× caseSubset（本批次工况子集，evidence 跨批次汇总）
   │ 展开为 (point, condition) 工作项（appliesTo 过滤；disabled/None 条目→NotApplicable 显式标记）
   ▼
分批（maxBatch 批次大小，黄金锁定默认 256）→ execution 派发 worker
   │ 每批：逐项 IIkSolver 求解（§5.3）＋要求值对比（容差/裕量/碰撞要求值 vs 结果）
   │ 产出 per-item 证据明细：{pointOid, conditionId, outcomeKind(§5.4), bestSolution?,
   │   demandsChecked{collisionFree, minJointMargin 实际值/要求值}, diagnostics[]}
   │ 检查点：每批完成→watermark(批号)（execution 检查点通道）
   ▼
批次合并（确定性合并 §8.4）→ EvaluationOutput{evidence[], searchRecord?, verdictInputs, payload}
```

- **结果状态**：per-item 计算状态（`CandidateFound/NoConvergence/AllFiltered/InputInvalid/NotApplicable`）——**不是工程判定**；三态报告（可行/工程不可行/数据不足）由 evidence 汇总产出（EVI/KIN-03 的"分别报告"落在汇总层，本单元保证素材完备）。
- **必验工况漏验不得输出正式通过**：本单元义务＝对 caseSubset **逐工况完成并标记**（完成矩阵素材）；漏验的最终拦截在 evidence 覆盖矩阵（②级门禁）——分批取消/失败时未完成工况如实标记（无伪完成）。
- **一个点失败不判全任务不可行**：per-item 状态独立；聚合（Must 违例→Infeasible 等）只在 evidence DomainVerdictInputs 通道（verdictInputs 由本单元按 REQ-06 口径填报"启用 Must 条目的计算结局"，判定归 evidence ⑤级）。
- **缺失与重复**：切片内点集与 caseSet 的对齐核查（引用了不存在点的事件/适用范围→InputInvalid 素材）；重复 (point, condition) 工作项按构造去重（appliesTo 展开集合语义）。
- **进度/取消/失败/分批**：进度经 `reportProgress(percent, phase)`（批粒度）；取消→未完成批如实标记 `NotRun`（partial record）；worker 崩溃→任务 Failed（execution），已归档批次保留可续（检查点续跑＝新 attempt，NFR-REL-03"已中断"语义）。
- **完整性校验**：EvaluationOutput 组装前自检——工作项总数＝Σ批项数＋NotRun 数；caseSubset 每项有终态标记；不完整即返回带 `incomplete` 标记的输出（调用侧据此不产 Completed envelope）。

### 7.2 区域采样与覆盖率（KIN-04 R3/R8；`kin.region-coverage`＋`IWorkspaceSampler`）

**覆盖率图（定义→样本→分母→覆盖率→降级）**：

```
req.plan-set（SamplingPlan 定义，requirements 拥有）
   ＋ AnalysisConfiguration.regionBudget（预算/种子——KIN-13）
   ▼ 样本生成（kinematics 拥有"算法"）——确定性：
   位置样本：Grid counts → 均分坐标（体心规则黄金锁定）；Random→seed 派生序列
   姿态样本：方向集（斐波那契螺旋，黄金锁定）× roll 均分
   sampleSetIdentity = SHA-256(planContentIdentity ‖ 预算/种子 canonical)   ← evidence §4.1.4 公式，本单元实现
   ▼ 冻结（快照组装方把 SamplingPlanRef 写入快照；复评不得增删更换样本）
逐样本评估：IK（位置；姿态样本＝完整位姿）＋ 碰撞（策略启用时；缺检测器→该样本 DataInsufficient，KIN-05）
   ▼
样本状态：Reached / Unreachable / DataInsufficient / NotApplicable / NotRun（取消）
   ▼
位置覆盖率 = |Reached 位置样本| ÷ 计划位置样本总数（存在性口径：存在 ≥1 有效解即达）
姿态覆盖率 = |Reached (位置×姿态) 样本| ÷ 计划姿态样本总数（全局口径）
数据不足样本：保留在分母、不计入分子、单独计数并列入报告 → 结论整体降级 DataInsufficient
零样本（计划计数乘积=0）→ 覆盖率不定义 → DataInsufficient ＋诊断（绝不输出 0% 或 100%）
取消/崩溃 → partial → 不产正式覆盖率（NotRun 保留，重跑同一样本集）
```

- **职责边界**：requirements 定义区域与计划（§5.2 requirements 卡）；kinematics 执行采样并计算覆盖率（含 sampleSetIdentity 实现）；evidence 校验冻结凭据与证据资格（requiredCaseSetId/SamplingPlanRef 对账）。
- **样本集身份复核**：评估器重算 sampleSetIdentity 与快照 `SamplingPlanRef.sampleSetIdentity` 比对——不一致→证据缺失（DataInsufficient＋诊断），**绝不沿用未冻结/不一致样本集**。
- **覆盖率数值与容差**：分子/分母为整数计数（无浮点容差问题）；per-sample 可达判定使用 AnalysisConfiguration 容差（附录 D 第 1/2 项）；**覆盖证据**＝逐样本状态表（canonical）＋汇总计数——包络/插值类结果不能替代样本全覆盖（KIN-10 R2 边界：包络标注"近似、非精确"，不作 Must 区域判定依据）。
- **镜像/分层**：区域采样定义（requirements）支持的结构；样本生成按定义展开（镜像样本独立计数）——生成器对"定义→样本"一对一确定，无隐藏分层。

### 7.3 与 KIN-09/10/11（R2）的边界

探索式工作空间采样/位姿可达性/外包络是 R2 需求（Quick/估算性质，AT-25/26）；其结果**不得**进入 Must 区域判定或必验覆盖（§7.2 冻结样本集是唯一正式口径）；接口在 §11 T14~T16 预留（复用 §7.2 方向集生成器与解集设施），R1 不留实现桩。

### 7.4 结果筛选与导出（KIN-08）

- 筛选/最差排序的唯一实现点＝`IKinematicSolutionSet`（§6.2）——UI/报告消费视图，不各自实现比较。
- 导出：本单元把结果整理为**值行**（JSON/CSV 行数据，含 snapshotId/对象引用），写出经 io 通道（插件侧触发）；**导出不产生修订**（AT-04）；导出自 results/<run-id>/ 归档或会话内存结果（来源在导出文件头声明）。
- 批量复算＝按原 slice 重新提交任务（execution 新 run）——不复用会话残留，结果当前性重新判定。

---

## 8. 策略调用、证据生成与执行协作

### 8.1 与 policy 的交接（KIN-05/NFR-COR-05/AT-19；构型碰撞与任务级不可行边界图）

```
                    ┌──────────────────────────────┐
   评估器（本单元）──▶ │ ④端口：policy CollisionEvaluator│ ◀── 唯一碰撞实现（R-POL-2 禁直链 proximity）
                    │ createSession(policy,         │
                    │   CollisionScene, names)      │
                    └──────────┬───────────────────┘
                               │ evaluate(query) → inCollision + objectIdPairs
        ┌──────────────────────┴──────────────────────┐
        ▼                                             ▼
  构型级碰撞（本单元消费）                      任务级不可行（evidence 裁定）
  "该解碰撞" → 该解被过滤/标记               仅限三类证明：解析界限（本单元可产素材
  （C8：一组解碰撞另一组有效→任务仍可行；        AnalyticBoundExceeded）/约束矛盾/
    全部解碰撞被过滤→DataInsufficient）        必经状态碰撞（素材=必经点构型的碰撞
                                              明细，由 evidence 汇总裁定）
  缺检测器/策略未启用碰撞 → 证据缺失标记 → DataInsufficient（KIN-05：绝不视为无碰撞）
```

- **输入运行时对象**：CollisionScene 由本单元按快照组装（workCell 视图＋scene objects＋device＋相邻连杆对——policy.md §6.1 装配者语义"请求方"）；对象一律 ObjectId。
- **评估作用域**：构型级（Self/Tool/Environment 按 policy 作用域矩阵；本单元不传阈值/模式参数——R-POL-5）；路径级碰撞属 trajectory（TRJ-03/04），本单元不产。
- **策略版本进入输入身份**：`policy.resolved` 依赖键＋快照 policyRef 内容身份（CON-06）——策略变更→切片失效→重算。
- **取消/失败**：碰撞会话调用周期检查取消；碰撞设施异常→该样本/解标记 DataInsufficient 素材＋诊断（不视为无碰撞、不中断整批）。
- **证据明细**：碰撞证据项 `{subjectPair(ObjectId×ObjectId), configurationRef, verdict}` 随 EvaluationOutput.evidence 交付——三入口一致（AT-19：同一 resolved policy＋同一评估器实例语义）由④端口保证，本单元不做二次缓存判定。

### 8.2 证据生成（`IKinematicEvidenceBuilder`；EvidenceItem/搜索未果/证明素材）

| 产出 | 内容 | 对应 evidence 消费 |
| --- | --- | --- |
| EvidenceItem（逐项证据） | 每任务点×工况：结局、最佳解指标（残差/裕量/可操作度）、硬过滤记录、碰撞证据引用、要求值对比（实际/要求/单位） | 表 4 运动学行必需项（IK 收敛状态与残差、解集硬过滤记录、限位裕量、碰撞证据、搜索未果记录） |
| SearchExhaustedRecord | 结局 2/3：预算、初值数、迭代统计、逐解过滤记录 | §8.1 搜索未果口径→DataInsufficient（C5/C8） |
| DeterministicInfeasibilityProof 素材 | 解析界限（工作半径推导输入＋目标距离）；必经状态碰撞明细素材 | §8.1 表 2 ③——validateProof 校验后才成立（D-09） |
| DomainVerdictInputs | 启用 Must 条目的计算结局（违例/满足/不适用）——REQ-06 口径填报 | evidence ⑤级 Must/Should 违例裁定 |
| payload | SolutionSet/SampleRecord 表（canonical，域契约版本化） | 归档/报告/下游消费 |

**只产不判**：以上全部是**素材**；Feasible/EngineeringInfeasible/DataInsufficient 的最终标记由 evidence aggregateVerdict 产出（五级汇总，本单元不调用）。

### 8.3 与 execution 的协作（提交/取消/检查点/分批/worker）

```
提交：域插件/编排方 → execution 提交任务（类型 kin.task-points-batch / kin.region-coverage；
      能力声明：支持取消 ✓、检查点=批 watermark（batch 覆盖任务）/样本 watermark（coverage）、
      暂停=不支持（R1 如实声明，收到暂停请求给明确状态反馈）、强制终止代价=低）
派发：worker 池（NFR-PERF 批量/长时）；manifest 握手（评估器注册清单摘要比对，不一致拒派发）
取消：ui/编排 → execution 取消 → IEvaluationContext.cancellationRequested=true
      → 评估器批间查询退出（2 s 内停止派发新批、在途批 10 s 内收敛——ARCH §4.4；
      本单元义务：每批/每 N 样本查询一次，响应粒度黄金锁定）
检查点：批/样本 watermark 写 checkpoints/（CON-04 兼容契约：sliceId+contractVersion 匹配才可续）
      → 恢复=新 attempt 续跑，已完成批不重算（幂等键=批号+sliceId）
分批结果：流式回传（NFR-PERF-03）→ 身份五元组核对（§4.5 ARCH）→ 归档 results/<run-id>/
崩溃：worker 崩溃仅当前任务失败（NFR-REL-02）；恢复后"已中断"如实呈现（NFR-REL-03）
```

### 8.4 确定性、并行与缓存

| 项 | 契约 |
| --- | --- |
| 稳定初值顺序 | 初值集按策略确定性生成（seed 序列/网格序）；批内工作项按 (pointOid, conditionId) 字典序——分片前全序确定 |
| 并行分片与稳定合并 | 分片＝全序工作项的连续区间；合并＝按全序归并（非到达序）；浮点聚合（如有）满足附录 D 第 8 项归约容差 1×10⁻¹²；**同输入同线程数→逐位一致；不同线程数→等价集合与稳定排序一致**（数值等价、不承诺逐位——NFR-COR-02） |
| 线程配置与身份 | `regionBudget` 内线程数**进入** AnalysisConfiguration canonical（KIN-13 明文"线程数"入身份）→ 改线程数=新身份=重算（不静默复用） |
| 求解器版本 | `solverContractVersion`（算法/契约版本）入评估器 descriptor.contractVersion 与 payload——升级=新契约版本=切片失效 |
| 重复运行 | 同 (sliceId, key, contractVersion, mode, caseSubset)→等价输出（缓存可命中）；缓存键＝execution/evidence 共同判定（CON-04：版本与切片身份匹配才命中；部分/失败/取消结果**不得**作为正式缓存命中） |
| Quick/Verified | 模式入请求身份；Quick 仅筛选排序；正式判定必须 Verified＋覆盖全部启用必验工况（EVI-01/02）——本单元在 Quick 输出上标记 `screening-only` |

### 8.5 evidence 侧接线（汇总/当前性/归档——消费视角）

- ResultEnvelope **不由本单元构造**：调用侧（execution/域宿主）以 EvaluationOutput→aggregateVerdict→`ResultEnvelope::make`（构造边界校验合法组合，TASK-02）；取消/失败任务产出 Canceled/Failed envelope（NotApplicable 判定，不进正式报告/可行集）。
- 当前性：归档后 evidence 对比切片内容身份 vs 当前 HEAD（Superseded 保留为历史证据——本单元旧结果 payload 不改写）。
- 数据不足报告：④级缺失项全量列出由 evidence 汇总；本单元保证缺失素材（KIN-05 碰撞缺失/搜索未果记录/NotRun 清单）齐全可列。

---

## 9. 公共接口与线程生命周期

### 9.1 接口契约总则

同 modeling §9.4/requirements §9.2 前提：纯函数服务非异常出口（`Expected<T, KinematicsError>` 或结果对象＋诊断列表）；调用方错误 fail-fast；评估器族实现 evidence `IEngineeringEvaluator`（§9.3 evidence 契约为准）；取消一律协作式（`IEvaluationContext::cancellationRequested()`）；一切数值 SI；确定性按 §3.4。

### 9.2 七个必需接口

```cpp
/// FK 与位姿指标（KIN-01）。纯函数、可重入、并发安全；确定性：同输入同字节。
class IFkEvaluator {
public:
    /// @brief 计算权威关节向量下的 TCP 位姿与全套指标。
    /// @pre  view 为请求绑定 RuntimeSnapshot 的只读模型视图；q 维度=device DOF 且全部有限。
    /// @post 输出经黄金解析算例容差（附录 D 第 4/9 项）；不修改 view。
    /// @错误 KinematicsError{IllegalQ, NoDevice, NoTcp, FrameUnresolved}
    /// @取消 不可取消（可预测 <1 s 内联计算；超界场景转批量评估器）。
    /// @所有权 值语义产出；view 由调用方持有。
    /// @示例 fk.evaluate(view, tcpRef, q) → PoseMetrics
    /// @非法 q 含 NaN（拒绝）；view 属另一 snapshot（调用方契约违约）
    virtual Expected<PoseMetrics, KinematicsError>
        evaluate(const IKinRuntimeView& view, const TcpRef& tcp,
                 const std::vector<double>& q) const = 0;
};

/// 多初值 IK 求解器（KIN-02）。纯函数；长运算——必须周期查询取消。
class IIkSolver {
public:
    /// @brief 五类结局求解（§5.4 铁律；过滤/去重/排序内部完成）。
    /// @pre  request.target 已解析到 base 系；collisionSession 由④端口创建（策略启用时）。
    /// @post SolutionsFound 时 solutions 非空且已按 §6.3 稳定排序；结局 2/3 必附
    ///       SearchExhaustedRecord；结局 5 仅产证明素材（不裁定）。
    /// @取消 返回 IkOutcome{cancelled=true}（无终局字段）——取消不是结局。
    /// @确定性 同 request（含 seed/referenceQ）→同输出（同线程数逐位、异线程数等价）。
    virtual IkOutcome solve(const IkRequest& request) const = 0;
};

/// 批量任务点验证评估器（KIN-03；= IEngineeringEvaluator 实现，key="kin.task-points-batch"）。
/// descriptor：依赖声明 §4.3；supportedModes={Quick, Verified}；stateless=true；
/// threadSafety=FullyThreadSafe；contractVersion 随 §11 T05 演进。
class IKinematicBatchEvaluator : public evidence::IEngineeringEvaluator {
public:
    /// @post 每工作项终态标记齐备（含 NotRun/NotApplicable）；不完整→incomplete 标记输出。
    ///       caseSubset 逐工况完成矩阵素材齐备（漏验拦截素材交 evidence）。
    /// @副作用 无（纯计算；归档/检查点经宿主通道）。
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override;
};

/// 区域采样与覆盖率（KIN-04；key="kin.region-coverage"）。
class IWorkspaceSampler : public evidence::IEngineeringEvaluator {
public:
    /// @brief 样本集生成（独立入口，供契约测试与检查点续跑核对 sampleSetIdentity）。
    /// @post sampleSetIdentity 与快照 SamplingPlanRef 一致（不一致→调用方证据缺失路径）。
    /// @确定性 同 (plan, budget, seed) → 同样本集（同序）。
    virtual Expected<SampleSet, KinematicsError>
        generateSamples(const SamplingPlan& plan, const RegionSamplingBudget& budget) const = 0;
    /// @brief 覆盖率计算（§7.2 图；分母=计划样本总数；零样本→DataInsufficient 素材）。
    virtual CoverageResult computeCoverage(const SampleSet& set,
                                           const SampleResultSet& results) const = 0;
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override;
};

/// 解集视图（§6.2——排序/筛选/统计/最差项唯一实现点）。不可变；并发只读安全。
class IKinematicSolutionSet {
public:
    /// @pre 以已求解 SolutionSet 构造（值持有）；构造时完成一次稳定排序。
    virtual const SolutionSetView& sorted() const = 0;
    virtual SolutionSetView filtered(const SolutionPredicate& p) const = 0;
    virtual SolutionSetStatistics statistics() const = 0;
    virtual std::optional<SolutionRef> worstBy(WorstMetric m) const = 0;
};

/// 证据组装器（§8.2——EvidenceItem/搜索未果记录/解析界限素材/verdictInputs 的唯一组装点）。
class IKinematicEvidenceBuilder {
public:
    /// @post 产出符合 evidence §9.3 EvaluationOutput 形状；证明素材自带
    ///       expectedSliceId 绑定（D-09 可校验）；诊断全部经 IDiagnosticFactory（已注册码）。
    /// @确定性 同输入→同输出（证据序=工作项全序）。
    virtual evidence::EvaluationOutput build(const BatchComputation& computation,
                                             const EvidenceContext& ctx) const = 0;
};

/// 命令门面（KIN-14 设默认 TCP/设备）。注意：kinematics 无自有项目对象 schema——
/// 设默认写的是 modeling 的 robot-design.defaultTcp 字段，命令处理器归 modeling（D-KIN-5）；
/// 本接口只负责"组装载荷＋经①端口提交＋结果回显"。
class IKinematicsCommandHandler {
public:
    /// @brief 把 (toolRef|device) 设为项目默认——组装增量载荷提交 modeling `apply-robot-design`。
    /// @pre  writable（只读模式拒绝）；快照基线=当前 tip。
    /// @post 成功=新修订（不破坏既有引用——modeling 命令断言保证）；失败=零修订＋诊断。
    /// @线程 UI 线程调用（异步等待 CommandResult）。
    virtual CommandSubmission setProjectDefaultTcp(const TcpRef& tcp) = 0;
    virtual CommandSubmission setProjectDefaultDevice(const core::ObjectId& robotOid) = 0;
};
```

每接口的前置/后置/错误/线程/确定性/取消/生命周期/所有权/副作用/示例已内联；合法与非法调用对照（抽样）：`IFkEvaluator` 合法＝快照内 q＋快照内 tcpRef，非法＝跨 snapshot 视图、非有限 q；`IIkSolver` 合法＝④端口会话句柄，非法＝自建碰撞实现（编译不可达）；`IKinematicsCommandHandler` 合法＝writable 会话，非法＝只读会话提交（拒绝＋诊断）。

### 9.3 评估器注册（L5 装配；evidence §9.4 注册表行为为准）

四工厂（`kin.pose-metrics`/`kin.task-point-ik`/`kin.task-points-batch`/`kin.region-coverage`）随装配清单注册主进程与 worker（manifest 握手一致）；重复键注册拒绝；本单元**不自我注册、不持有注册表**（evidence 拥有）。

### 9.4 生命周期与线程总表

| 对象 | 生命周期 | 线程 |
| --- | --- | --- |
| 评估器实例 | create()→任务结束丢弃（无状态建议共享） | 工作线程 |
| IKinRuntimeView | 每请求构建（绑定 RuntimeSnapshot）；请求结束失效 | 构建于宿主；使用于工作线程（只读） |
| CollisionSession | 每评估构建；评估结束销毁 | 工作线程 |
| IKinematicSolutionSet | 插件会话/归档读取期持有 | UI 线程只读消费 |
| AnalysisConfiguration | 用户级持久化；会话装载 | UI 线程编辑；入快照为值 |

### 9.5 求解配置、策略版本与输入切片依赖矩阵（失效面速查）

| 输入变化 | model.robot-design | tcp(tool) | req.points | req.conditions | req.regions/plans | config.ik | policy.resolved |
| --- | --- | --- | --- | --- | --- | --- | --- |
| pose-metrics 切片 | ✓ | ✓ | — | — | — | ✓ | ✓ |
| task-point-ik 切片 | ✓ | ✓ | ✓ | — | — | ✓ | ✓ |
| task-points-batch 切片 | ✓ | ✓ | ✓ | ✓ | — | ✓ | ✓ |
| region-coverage 切片 | ✓ | ✓ | ✓ | ✓ | ✓ | ✓（样本基准除外※） | ✓ |

※ 求解配置改变 sliceId 但**不改变 sampleSetIdentity**（样本基准仅随 plan＋budget/seed 变——evidence §4.1.4 D-04 双层身份）；策略变更经 policy.resolved 全列失效（CON-06）；电机成本等 selection 对象不在任何行→不失效（AT-05②）。

### 9.6 kinematics 稳定诊断码登记表（`KIN-` 前缀；ownerUnit=`kinematics`，装配期注册，不预建无消费者条目）

| 码 | 级别 | 语义（旧 KinematicFailureReason 映射） | 任务 |
| --- | --- | --- | --- |
| `KIN-NO-DEVICE` | error | 无可用设备（NoDevice） | T03 |
| `KIN-NO-TCP` | error | TCP 未配置/悬空（NoTcpFrame） | T03 |
| `KIN-TARGET-ILLEGAL` | error | 目标位姿/容差非法（InvalidTarget） | T04 |
| `KIN-RESIDUAL-EXCEEDED` | warning | FK 验算残差超容差（TargetResidual；比较型：实际/期望/单位） | T04 |
| `KIN-JOINT-LIMIT-VIOLATED` | warning | 解超限位（JointLimit） | T04 |
| `KIN-NEAR-LIMIT` | warning | 接近限位（NearJointLimit；阈值读 policy） | T04 |
| `KIN-NEAR-SINGULAR` | warning | 条件数恶化/可操作度低（NearSingular/Singular；阈值读 policy） | T03 |
| `KIN-COLLISION-FILTERED` | warning | 该解因碰撞被过滤（Collision——构型级） | T07 |
| `KIN-COLLISION-UNAVAILABLE` | warning | 碰撞检测器缺失/策略未启用→证据缺失（CollisionDetectorUnavailable；KIN-05） | T07 |
| `KIN-SEARCH-EXHAUSTED` | warning | 搜索未果（多初值未收敛/全部候选被过滤→DataInsufficient 素材，附记录） | T04 |
| `KIN-SOLVER-INTERNAL` | error | 求解器内部错误（SolverError；fail-fast 轨道） | T04 |
| `KIN-CONFIG-ILLEGAL` | error | 求解配置非法（seed=0/容差≤0/计数负——I-KIN-4） | T10 |
| `KIN-SAMPLE-IDENTITY-MISMATCH` | error | 样本集身份与快照不一致（未冻结/漂移——§7.2） | T06 |
| `KIN-COVERAGE-ZERO-SAMPLES` | warning | 零样本→覆盖率不定义（DataInsufficient 素材） | T06 |
| `KIN-RESULT-INCOMPLETE` | warning | 批次不完整（取消/失败；NotRun 清单） | T05 |

### 9.7 跨域命令归宿声明（KIN-14）

kinematics **无自有项目对象 schema**（分析配置为用户级持久化，非修订对象）→ 无自有领域命令处理器；KIN-14"设默认 TCP/设备"由 `IKinematicsCommandHandler` 组装 **modeling** `apply-robot-design` 增量载荷经①端口提交（处理器/断言/inverse 归 modeling 卡 §9.3）——登记为 D-KIN-5 并知会 modeling 所有者（§14.3 P-KIN-5）。

### 9.8 插件界面设计与界面逻辑（`sdurws_ird_kinematics_plugin`；KIN-01/03/06/07/08/12/13、UX-04/05）

**定位与分工**：同前两卡——工作台壳/命令注册/快捷键/三维视图契约归 ui；本节只设计 kinematics 域面板。旧六页 UI 的功能映射见 §2.5。

**面板组成（挂接 UX-09 五区布局；求解器/种子等高级参数收拢于"高级面板"——UX-04）**：

| 面板区 | 内容 | 消费契约 |
| --- | --- | --- |
| 位姿指标面板（承接旧 Diagnose 页） | 设备/TCP 选择（快照内解析）、关节表（显示单位投影 KIN-12）、FK 位姿/Jacobian 汇总/奇异值/条件数/可操作度/关节裕量；单点 IK：目标编辑（单位投影）＋求解＋候选检查器（解表→检查器联动：健康摘要/关节/雅可比） | §9.2 IFkEvaluator/IIkSolver/IKinematicSolutionSet＋ui FormEditCommon |
| 任务点验证面板（承接旧 Task Points/Validate 页） | 任务点消费视图（真值归 requirements——只读投影＋结果列）、批量分析（经 execution 后台：进度/取消）、逐点结果（结局五类标识）与解检查器跳转；必验覆盖完成标记列 | §7.1＋results 投影＋ui ITaskPresentationModel |
| 区域覆盖面板 | 区域/计划消费视图、采样预算编辑（config.ik 高级面板）、覆盖率结果（位置/姿态分别呈现＋数据不足降级标识＋零样本提示）、覆盖证据查看（逐样本状态） | §7.2＋CoverageResult |
| 结果与可视化面板（承接旧 Visualization/Report 页） | 结果筛选/最差项排序（IKinematicSolutionSet 视图）、失败点/薄弱区/碰撞对象/最差裕量**渲染数据**（KIN-07——数据本单元产、呈现经 ui View3D/绘图契约）、JSON/CSV 副本导出（io 通道，零修订） | §6.2/§7.4＋ui View3D 契约 |
| 求解配置（高级面板内） | AnalysisConfiguration 编辑（初值策略/数量/迭代/容差/去重阈值/预算/线程/seed）＋"修改将使受影响结果需重算"提示；无碰撞开关、无判定阈值（只读策略摘要跳转——UX-08） | §4.4＋KIN-13/AT-27 |

**界面逻辑（十二条数据流）**：

| # | 交互 | 数据流 | 契约锚 |
| --- | --- | --- | --- |
| L-K1 | 选中联动 | 任务/候选/样本选中→SelectionModel→检查器/三维高亮（渲染数据） | ui §4.2；KIN-07 |
| L-K2 | 单点求解 | 面板 Solve→IIkSolver（内联 <1 s 或转后台）→解表（稳定排序）→检查器 | §5.3/§6.3 |
| L-K3 | 批量验证 | 提交 execution 任务→进度/取消（协作）→分批结果投影→完成矩阵列 | §7.1/§8.3；AT-34 |
| L-K4 | 会话姿态（双击候选/可视化点回写/复位 Home） | 只写 ui 会话态：零修订、零失效、不入缓存身份 | KIN-06/AT-04；ARCH §7.7 |
| L-K5 | TCP 回填目标 | "以当前 TCP 为目标"→会话态读→显示单位回填（仅呈现默认值；求解身份不含会话） | §6.3 D-KIN-4；KIN-12 |
| L-K6 | 区域覆盖运行 | 提交 coverage 任务→进度→覆盖率呈现（降级/零样本标识） | §7.2 |
| L-K7 | 配置修改提示 | config.ik 编辑→保存（用户级）→按依赖提示受影响结果需重算（不自动重算） | §4.4；AT-27 |
| L-K8 | 单位切换 | 显示单位切换→全面板重投影：零修订/零重算/结果不动 | KIN-12/AT-27；V-17 |
| L-K9 | 设默认 TCP/设备 | 面板发起→§9.7 门面→①端口（modeling 命令）→新修订→依赖失效提示 | KIN-14；§9.7 |
| L-K10 | 结果导出 | 筛选/排序→JSON/CSV 副本（io AtomicFile）→零修订；失败旧文件完好 | §7.4；KIN-08 |
| L-K11 | 只读模式 | writable=false→提交正式评估（写 results）拒绝＋诊断；会话级 FK 预览/单位切换可用 | ui §7.6；PM-07 |
| L-K12 | 任务状态呈现 | 任务九态/中断（"已中断"如实）/迟到结果不进当前会话——经 ui ITaskPresentationModel 投影 | TASK/NFR-REL-03；AT-10/11 |

**域命令登记清单**（`kinematics.*`，ui CommandId 词表）：

| CommandId | 语义 | readOnlyAllowed |
| --- | --- | --- |
| `kinematics.analyze-pose` | 当前位姿指标（会话级） | true |
| `kinematics.solve-ik` | 单点 IK（会话级；正式验证走批量任务） | true |
| `kinematics.validate-task-points` | 批量验证提交（写 results——正式） | false |
| `kinematics.evaluate-coverage` | 区域覆盖提交（写 results——正式） | false |
| `kinematics.set-default-tcp` / `kinematics.set-default-device` | KIN-14（经①端口，新修订） | false |
| `kinematics.export-results` | JSON/CSV 副本导出 | true |
| `kinematics.reset-session-pose` | 复位 Home（会话） | true |

**线程与刷新约束**：UI 线程零计算（>1 s 全部转 execution——NFR-PERF-01）；批量/覆盖结果经 results 投影刷新（事件驱动）；会话级单点 FK/IK 内联门槛 <1 s，超界自动转后台并给状态反馈；面板不缓存权威结果（消费 results 归档与会话对象）。

## 10. 验证方案及故障注入矩阵

### 10.1 总则

四组：**V-F FK/基座**、**V-I IK 与结局**、**V-B 批量/覆盖**、**V-S 确定性/执行/契约**。**未执行的测试不得标注通过**（AGENTS §4.2）；用例随 WP-15-T03~T13 执行留痕。黄金数据集（WP-15-T13，DTB：按 AT 反例分组）：`kin-fk-analytic/`（二连杆/六轴解析 FK＋正反变换）、`kin-ik-dedup/`（去重/排序/同位姿异构型）、`kin-search-outcomes/`（五类结局各含样例＋换初值反例）、`kin-coverage/`（固定计数 100/60＋数据不足保留分母＋零样本）——`ird-golden-manifest/1`＋容差档案 `tolerance/kin-*`（锚附录 D 第 1/2/3/8 项；解析对照第 9 项黄金算例自带更严值声明）＋零值/近零/正负抵消边角样例。可控求解器与碰撞**测试替身**：契约测试用确定性替身求解器（预设收敛序列）与替身碰撞评估器（脚本化判定），验证评估器管线/证据组装/取消路径（WP-15-T03~T07 依赖）。

### 10.2 故障注入矩阵

| # | 组 | 场景 | 依据 | 前置 | 操作 | 预期结果 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| V-01 | F | FK 解析对照与正反变换 | KIN-01、NFR-COR-01、附录 D 第 4/9 项 | kin-fk-analytic 黄金 | FK(q)→抽样 IK(FK(q))→FK 复算 | 指标与解析值在容差内；正反变换闭环含于去重容差 | 黄金断言；残差值 |
| V-02 | F | 基座—世界变换消费 | MDL-22/M-11、AT-37 | 倒挂安装快照 | FK 求 tcpInWorld＋重力相关量 | 仅经 worldToBase/gravityBase；与 runtime 契约一致（无二次旋转——联合观测） | tcpInWorld 断言；AT-37 联合 |
| V-03 | F | 非法输入拒绝 | NFR-COR-03 | 任意 | NaN q/负容差/seed=0 | fail-fast（IllegalQ/CONFIG-ILLEGAL），不钳制不置零 | 错误码 |
| V-04 | I | 多构型稳定排序与去重 | KIN-02、附录 D 第 3 项、AT-03 | kin-ik-dedup 黄金 | 同位姿多构型求解；重复运行两次 | 同末端位姿异构型均保留；去重逐轴 1e-6；排序逐位一致（四键稳定） | 解表；两次输出逐位比对 |
| V-05 | I | IK 有效解 | KIN-02 | 可达目标 | 求解 | SolutionsFound；残差≤容差；排序正确 | outcomeKind；首解指标 |
| V-06 | I | 多初值未收敛 | §8.1 C5、AT-03 | 不可达姿态（数值难收敛）+小预算 | 求解 | MultiInitNoConvergence＋SearchExhaustedRecord（预算/初值数）；**无不可行结论** | 记录字段；无 proof |
| V-07 | I | 搜索未找到有效解 | §8.1 C5 | 换初值反例对 | 原初值全发散→扩大初值/预算复评 | 前次 DataInsufficient（搜索未果）；扩大后找到有效解——同冻结输入复评语义 | 两次 SearchExhausted/Found |
| V-08 | I | 一构型碰撞另一构型有效 | §8.1 C8、AT-19 | 双构型目标（替身碰撞：一组碰撞） | 求解（策略启用） | 碰撞解被过滤（KIN-COLLISION-FILTERED）＋另一构型在解集；任务可行素材不受影响 | filteredRecords；解集 |
| V-09 | I | 全部候选被过滤 | §8.1 C8 | 全部解碰撞（替身） | 求解 | AllCandidatesFiltered＋逐解过滤记录→DataInsufficient；**不得输出不可行** | SearchExhaustedRecord；无 proof |
| V-10 | I | 必经状态碰撞证明素材 | §8.1 ③、C8 任务级 | 必经点（Must∧enabled）碰撞明细 | 批量验证＋素材组装 | 仅产证明素材（绑定 expectedSliceId）；成立与否由 evidence validateProof（联合观测） | proof 素材字段；evidence 裁定 |
| V-11 | B | 任务点漏验拦截 | EVI-02、KIN-03 | 批量验证中取消一批 | 中断→汇总 | 未完成工况如实 NotRun→不产 Completed envelope→正式通过被拦（evidence ②级矩阵联合） | 完成矩阵素材；envelope 状态（联合） |
| V-12 | B | 工况重复/错误引用消费面 | REQ-04、EVI-02 | appliesTo 引用悬空点 | 批量展开 | 工作项展开核查→InputInvalid 素材＋诊断（重复项按集合语义去重） | 展开清单；诊断 |
| V-13 | B | 区域零样本 | KIN-04 R8、AT-03 | counts 乘积=0 计划 | 覆盖评估 | DataInsufficient＋KIN-COVERAGE-ZERO-SAMPLES；**不输出 0%/100%** | CoverageResult；诊断 |
| V-14 | B | 覆盖率分母与降级 | KIN-04 R3/R8、AT-03 | kin-coverage 黄金（计划 100 可达 60＋数据不足变体） | 覆盖评估 | 60%（固定计数）；数据不足样本保留分母不计分子＋整体降级；位置/姿态分别统计 | 分子分母计数；降级标记 |
| V-15 | B | 未冻结样本集拒绝 | §7.2、CON-04 | 篡改 planContentIdentity | 覆盖评估 | KIN-SAMPLE-IDENTITY-MISMATCH→证据缺失（不沿用） | 错误码；无覆盖率输出 |
| V-16 | S | Quick 与 Verified 分层 | EVI-01、OPT-06 | 同目标两种模式 | 分别评估 | Quick 输出 screening-only 标记；正式判定仅 Verified＋全覆盖（联合） | mode 字段；envelope（联合） |
| V-17 | S | 显示单位变化无失效 | KIN-12、AT-27、§4.5 | 任意结果 | 切换单位 | 零重算/零修订；sliceId/configDigest 不变 | 身份摘要比对 |
| V-18 | S | 会话姿态不入身份 | KIN-06、AT-04 | 双击候选/回写点/复位 Home | 会话操作 | 零修订零失效；后续求解身份不变（referenceQ 未变） | 修订计数；requestIdentity |
| V-19 | S | 求解器版本不兼容 | CON-04、§8.4 | 升级 contractVersion | 复用旧缓存/检查点 | 缓存不命中（版本不符）→重算；检查点拒绝续跑 | 缓存判定；checkpoint 拒绝 |
| V-20 | S | 随机种子不同 | KIN-13、NFR-COR-02 | 同目标 seed A/B | 求解/采样 | 不同身份（configDigest 不同）→各自结果；不静默复用 | configDigest 比对 |
| V-21 | S | 线程配置变化 | NFR-COR-02 | 1/8 线程同输入 | 批量求解 | 等价集合＋稳定排序一致（数值不承诺逐位）；线程数入身份→分别缓存 | 合并序比对；双缓存条目 |
| V-22 | S | 取消/超时/worker 崩溃 | NFR-PERF-02、TASK-02、AT-34 | 长批量运行 | 2 s 取消窗口/注入崩溃 | Canceling 停止派发；未完成批 NotRun；**无完整结果发布**；崩溃仅当前任务失败 | partial 记录；envelope 状态（联合） |
| V-23 | S | 检查点恢复 | CON-04、NFR-REL-03、AT-11 | 覆盖任务中途崩溃 | 重启续跑 | 新 attempt 自 watermark 续跑；已完成样本不重算；恢复后"已中断"如实呈现 | watermark；attempt 五元组 |
| V-24 | S | 结果当前性与历史归档 | CON-02/05、AT-05/10 | 已归档结果＋TCP 变更/项目切换 | 当前性判定/迟到归档 | TCP 变更→运动学切片 Superseded（AT-05①）；迟到结果按原修订归档不进新项目（五元组核对） | 当前性标记；归档路径（联合） |
| V-25 | S | 三入口碰撞一致 | ARC-05、NFR-COR-05、AT-19 | 同快照＋同策略 | kinematics/（trajectory·optimization 联合）调共享评估器 | 对象 ID 对/判定/原因码完全一致（本单元侧：无本地碰撞副本——静态扫描＋运行断言） | 会话调用记录（联合） |
| V-26 | G | GUI 主流程（设计登记） | UX-04/05、§9.8 | Windows GUI 环境 | 位姿指标→单点求解→批量提交→取消→配置修改提示→导出 | 高级面板收拢；就地错误；进度/取消；配置重算提示；导出零修订 | 面板截图（本次不启动） |

**AT 承接声明**：AT-05（V-24 联合）、AT-09（需求基准面——OPT 消费本单元评估器，联合）、AT-10（V-23/24 联合）、AT-11（V-23 联合）、AT-19（V-08/25 联合）、AT-27（V-17/18/20）、AT-34（V-22 联合）、AT-37（V-02 联合）；AT-03 反例全部映射 V-04/06/07/08/13/14。"联合观测"行本卡只负责 kinematics 侧输入/输出面——不把未执行测试写为通过。

---

## 11. 阶段 B 实现任务拆分

DTB WP-15 任务包（v0.16 §2.16）为权威拆分；本卡视角内部顺序：

| 任务 | 产出（本卡落点） | 前置（DTB） | 内部顺序 |
| --- | --- | --- | --- |
| WP-15-T01 | 本卡（含五类结局判定表 §5.4、去重口径 §6.1、覆盖率算法 §7.2、AnalysisConfiguration schema §4.4、插件界面 §9.8、任务拆分 §11） | WP-06/07/05/08 各卡 | ①（已完成——本提交） |
| WP-15-T02 | `kinematics/CMakeLists.txt`（STATIC 零 Qt＋测试目标＋守卫＋白名单六边，§3.2）；KinTypes/Errors/DiagCodes | T01、WP-03-T01 | ② |
| WP-15-T03 | FK 与指标评估器（`kin.pose-metrics`＋IFkEvaluator＋Bounds 解析素材）＋解析黄金 UT（AT-03/NFR-COR-01） | T02、WP-06-T09（DeviceView） | ③（核心阻塞） |
| WP-15-T04 | 多初值 IK＋去重排序（`kin.task-point-ik`＋IIkSolver＋SolutionSet；搜索未果记录；两提交） | T03、WP-07-T07 | ④ |
| WP-15-T05 | 批量任务点验证（`kin.task-points-batch`＋EvidenceBuilder＋检查点 watermark） | T04、WP-08 | ⑤ |
| WP-15-T06 | 区域覆盖率评估（`kin.region-coverage`＋IWorkspaceSampler＋sampleSetIdentity 实现＋零样本/降级） | T04 | ⑤ |
| WP-15-T07 | 碰撞证据接入（④端口会话组装＋KIN-05 语义＋三入口一致断言） | T04、WP-07-T07 | ⑤ |
| WP-15-T08 | 会话姿态与设默认（L-K4/L-K9＋Commands 门面） | T03、WP-04-T10、WP-10 | ⑥ |
| WP-15-T09 | 结果筛选/导出/批量复算（SolutionSet 视图消费面＋导出值行） | T05 | ⑥ |
| WP-15-T10 | 显示单位与求解配置（config.ik canonical＋用户级持久化接线＋AT-27 提示） | T03、WP-05-T04 | ⑥ |
| WP-15-T11 | 失败点/薄弱区三维可视化**渲染数据** | T05、WP-10-T05 | ⑥ |
| WP-15-T12 | `sdurws_ird_kinematics_plugin`（四面板按 §9.8） | T03~T11、WP-10-T08 | ⑦ |
| WP-15-T13 | 契约测试＋四套黄金数据集＋容差档案（含测试替身求解器/碰撞） | T03~T12、WP-02 | ⑧（收口） |
| WP-15-T14 | 工作空间采样与可视化（R2/D；KIN-09） | T06、阶段 D 启用 | R2 |
| WP-15-T15 | 近似外包络（R2/D；KIN-10） | T14 | R2 |
| WP-15-T16 | 独立位姿可达性（R2/D；KIN-11） | T14 | R2 |
| WP-15-T17 | 扩展显示单位（R2；KIN-12-S1） | T10 | R2 |

DoD 沿 DTB §5.2：双模式构建零错误、`ird_gates` 零命中、验收用例通过并留痕、文档同步零偏差。

---

## 12. 后续阶段承接与接口交接清单

| 对端 | kinematics 收到 | kinematics 交出 |
| --- | --- | --- |
| trajectory（C） | —（经③端口调用本单元） | IK 连续性检查所需的单点/序列 IK 评估服务（`kin.task-point-ik`；TRJ-02/WP-16-T05 消费）——不直链（R-1） |
| optimization（B/D） | 静态硬约束编排协议（OPT-03） | `kin.task-points-batch`/`kin.region-coverage` 评估器（Must 工位可达/Must 区域覆盖/碰撞静态子集/关节限位）；Quick/Verified 双模式；种子/线程入身份 |
| reporting | results 归档读取 | canonical 结果 payload（SolutionSet/CoverageResult）与快照绑定溯源 |
| evidence | EvaluationRequest/IEvaluationContext/aggregateVerdict 判定 | EvaluationOutput（证据明细/搜索未果/证明素材/verdictInputs）；`kin.*` 评估器注册清单；req.* 角色键消费声明 |
| runtime | RuntimeSnapshot/IRuntimeModelView/worker 物化 | 消费面反馈（DeviceView 需求——WP-06-T09 对端） |
| policy | ④端口评估器/已解析策略 | CollisionScene 组装（请求方职责）；构型级碰撞证据明细 |
| execution | 任务注册/取消/检查点/归档协议 | 任务类型能力声明（取消/检查点 watermark/不支持暂停如实声明）；manifest 清单 |
| requirements | req.* 对象消费（切片闭包） | 采样执行与 sampleSetIdentity 实现（§7.2——与 requirements §5.2 定义对偶）；覆盖率结果回授其覆盖目标核对 |
| diagnostics | 注册协议/工厂/目录 | `KIN-*` CodeDescriptor 清单（§9.6）＋域错误→码映射 |
| ui | DraftController（本单元无草稿——分析配置用户级）/注册器/View3D/任务投影 | 域命令清单（§9.8）、渲染数据（KIN-07）、DomainReadinessItem 数据 |

**R2 承接**（PA-7 无新机制）：KIN-09/10/11（T14~T16，复用采样/解集/方向集设施＋execution 检查点）；KIN-12-S1 扩展单位（core Units token 扩展＋本单元投影表）；MDL-12-S1 prismatic 链（IK 去重阈值维度随关节类型——附录 D 第 3 项已预留）。

---

## 13. 需求—设计—验证追踪矩阵

| 需求/AT | 设计落点 | 验证 | 主任务（DTB） |
| --- | --- | --- | --- |
| KIN-01 | §5.2、D-KIN-2 | V-01 | T03 |
| KIN-02 | §5.3~§5.6、§6 | V-03~V-07 | T04 |
| KIN-03 | §7.1 | V-11/12 | T05 |
| KIN-04 | §7.2 | V-13~V-15 | T06 |
| KIN-05 | §8.1、§9.6 | V-08/09＋缺检测器路径 | T07 |
| KIN-06 | §4.5、§9.8 L-K4 | V-18 | T08 |
| KIN-07 | §9.8（渲染数据） | V-26（GUI 登记） | T11 |
| KIN-08 | §6.2/§7.4 | V-26＋导出单元面 | T09 |
| KIN-09/10/11（R2） | §7.3、§11 T14~T16 | R2 启用时补 | T14~T16 |
| KIN-12 | §4.5 | V-17 | T10 |
| KIN-13 | §4.4、§6.3 | V-07/20/21 | T10 |
| KIN-14 | §9.7 | V-26＋命令面 UT | T08 |
| CON-01/02/04/05/06 | §4.1/§8.4/§8.5 | V-15/19/24 | T05/T06 |
| EVI-01/02 | §5.4/§7.1/§8.2 | V-05~V-11/16 | T04/T05 |
| MDL-06/14/22 消费面 | §5.1/§8.1 | V-02 | T03 |
| TASK-01~03 消费面 | §8.3/§8.5 | V-22/23/24 | T05/T06 |
| OPT-03/06/07 消费面 | §8.2 交接/§8.4 | V-16（联合 AT-09） | T05/T06 |
| NFR-COR-01/02/03 | §10.1/§8.4/§5.2 | V-01/04/21 | T13 |
| NFR-PERF-01~03 | §8.3 | V-22 | T05/T06 |
| NFR-REL-02/03 | §8.3 | V-22/23 | T05/T06 |
| AT-05/09/10/11/19/27/34/37 | — | V-24/16（联合）/V-23/24/25/V-17/18/20/V-22/V-02 | T05~T07/T10 |
| 附录 D 第 1/2/3/8/12 项 | §4.4/§6.1/§8.4 | V-04/21＋黄金档案 | T13 |

---

## 14. 设计决策、风险、待裁决项与变更记录

### 14.1 设计决策登记（D-KIN-x；仅 kinematics 所有权内取舍）

| ID | 决策 | 依据/理由 |
| --- | --- | --- |
| D-KIN-1 | 四评估器切分（pose-metrics/task-point-ik/task-points-batch/region-coverage），键与依赖声明 §4.3 | 依赖面与失效面最小化；evidence §9.3 示例键同源；单点内联与批量后台分流 |
| D-KIN-2 | 统一尺度规则＝基础雅可比（平移行 m、转动行无量纲）、w＝√det(JJᵀ)、条件数＝σmax/σmin；黄金锁定 | KIN-01"统一尺度"上游未给具体形式——本卡定义＋黄金数据集锁定，修改走设计变更（P-KIN-1） |
| D-KIN-3 | 去重＝成对逐轴容差比较；ConfigurationSignature（精确编码）仅作记录键不参与去重 | "数值容差不能直接作为哈希等价"（用户约束/附录 D 第 3 项精神）；契约测试锁定 |
| D-KIN-4 | 排序参考构型 referenceQ 显式入请求身份；禁止隐式读会话姿态 | KIN-02"当前距离"若无显式化会引入会话依赖→破坏确定性/身份（KIN-06/AT-04 边界） |
| D-KIN-5 | kinematics 无自有领域命令；KIN-14 设默认＝组装 modeling 命令经①端口 | 对象 schema 所有权在 modeling；避免第二命令处理器写同一对象 |
| D-KIN-6 | 批大小/方向集/网格体心规则/取消响应粒度等数值默认随黄金数据集锁定 | 与 modeling T-MDL-1/requirements 默认值同模式：设计默认＋锁定，非上游冻结 |
| D-KIN-7 | 五类结局枚举（`IkOutcomeKind`）作为域内唯一结局词表 | §5.4 铁律的类型化承载；C5/C8 合规的结构性保证（枚举无"任务不可行"值） |
| D-KIN-8 | 样本生成器与 sampleSetIdentity 实现归 kinematics，公式字面采 evidence §4.1.4 | requirements 定义/kinematics 生成/evidence 冻结三方分工的落点；公式单源防漂移 |
| D-KIN-9 | 取消一律协作式查询；取消结果＝`cancelled` 载荷（非结局、非错误、零正式证据） | TASK-02/UX-03（正常取消非错误）；NFR-PERF-02 2 s/10 s 窗口 |
| D-KIN-10 | 解析界限（工作半径）是本单元唯一可产的不可行证明**素材**；约束矛盾/必经状态碰撞素材由批量证据明细承载 | §8.1 三条件分工：可静态验证的解析界限天然属运动学；裁定权全部留 evidence |

### 14.2 风险登记（R-KIN-x）

| ID | 风险 | 缓解 |
| --- | --- | --- |
| R-KIN-1 | 对端契约未冻结（evidence §9/§4.1.4、runtime view、policy 会话、execution 通道） | §1.2 基线登记＋冻结后增量同步（P-KIN-7）；T03 前以测试替身先行 |
| R-KIN-2 | 数值 IK 在特殊拓扑（S-R-S 偏置/近奇异）收敛质量 | 多初值策略＋预算可调＋搜索未果如实记录（不伪装）；解析算例黄金仅覆盖 FK/解析可验证面——IK 质量属"搜索未果"口径如实降级 |
| R-KIN-3 | 大区域样本量（10 万级）性能/内存 | 分批流式（NFR-PERF-03/04）＋检查点 watermark；样本集不枚举入快照（身份对参数计算——evidence §4.1.4） |
| R-KIN-4 | 跨线程浮点合并漂移 | 合并按全序归并（非到达序）；聚合容差 1e-12（附录 D 第 8 项）；V-21 双线程数等价断言 |
| R-KIN-5 | IK 结果被下游误用为工程结论 | payload 全程携带 outcomeKind＋screening-only/mode 标记；判定词表不在本单元输出面（结构性隔离） |

### 14.3 待裁决项（P-KIN-x；上游冲突集中登记）

| ID | 事项 | 依据 | 影响 | 建议 | 裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-KIN-1 | 统一尺度规则（KIN-01）具体形式上游未冻结 | REQUIREMENTS KIN-01、本卡 D-KIN-2 | 指标跨版本可比性 | 本卡定义＋黄金锁定；如需求侧另有意图走需求变更 | 需求所有者（确认或变更） |
| P-KIN-2 | RuntimeSnapshot 进入评估器的传递机制：evidence `IEvaluationContext` 现无运行时模型访问器，EvaluationRequest 亦无该字段——评估器获取 `IKinRuntimeView` 的注入点未定义 | evidence.md §9.3、runtime.md §9 | T03~T07 实现接线 | 建议 evidence §9 增补 `IEvaluationContext` 只读运行时模型访问（或约定宿主经工厂闭包注入＋manifest 扩展）；本卡按"宿主注入"抽象（`IKinRuntimeView` 自有最小接口）先行 | evidence＋execution 所有者（会签） |
| P-KIN-3 | sampleSetIdentity 公式实现归属与 canonical 格式：公式在 evidence §4.1.4，生成算法归 kinematics（requirements §5.2）——预算/种子 canonical 编码字节须三方一致 | 三卡交叉 | 身份不一致→误 DataInsufficient | evidence 侧冻结 canonical 格式（同 requiredCaseSetId 的 I-4 先例），kinematics 实现并对账用例 | evidence 所有者（格式）＋本卡（实现） |
| P-KIN-4 | AnalysisConfiguration 持久化通道细节（PM-14 用户级存储的载体/键）未在 ui/project 卡冻结 | PM-14/KIN-13 | T10 接线 | kinematics 只定 schema＋canonical；存储经 PM-14 用户设置通道（ui 侧），接口待 ui 卡冻结 | ui 所有者（会签） |
| P-KIN-5 | KIN-14 命令归宿：主 WP-15 但写入对象 schema 归 modeling | DTB §2.16/§3、modeling.md | 命令处理器归属 | kinematics 组装＋提交 modeling 命令（D-KIN-5）；modeling 卡知会登记 | modeling 所有者（确认） |
| P-KIN-6 | 线性代数/SVD 实现选型：基线 rw::math 无 SVD；引 Eigen 级依赖需 DTB 登记 | DTB §5.3（第三方经 vcpkg） | T03 实现路径 | 建议 vcpkg Eigen（PRIVATE，仅 kinematics 计算库），DTB 登记后启用；备选自带小规模 SVD（对称矩阵特征分解） | DTB 维护者（依赖登记） |
| P-KIN-7 | 全部对端契约未冻结（同 P-MDL-8/P-REQ-8） | §1.2 | 实现期漂移 | 冻结后 diff 增量同步 | 各对端所有者 |
| （引用） | P-PR-9 命令 token、P-UI-6 门控三方契约、P-REQ-3 就绪组装时点 | 前两卡 §14.3 | 间接影响 | 服从原裁决 | 原登记裁决者 |

### 14.4 新增语义登记（全部位于 kinematics 所有权内；无上游扩张）

1. `IkOutcomeKind` 五类结局词表（§5.4——C5/C8 铁律的类型化；无"任务不可行"值）。
2. `kin.*` 评估键 4 个＋依赖声明实例（evidence §9.3 示例同源具体化）。
3. AnalysisConfiguration schema（§4.4——KIN-13 授权的求解域参数集；默认值＝附录 D 第 1/2/3 项，seed=0 非法不静默替换）。
4. `KIN-*` 稳定码 15 项（§9.6，diagnostics 注册纪律内随任务注册；旧 15 值失败原因语义映射保留）。
5. 数值默认（批大小/方向集/网格体心/取消响应粒度/排序键方向）＝设计默认，黄金锁定（D-KIN-6）。
6. `IKinRuntimeView` 最小消费接口（§4.2——runtime 只读视图的本单元侧封装，不新增 runtime 语义）。

### 14.5 交付前自审结论（2026-09-22，文档级自审——不等同实现测试或正式验收）

| 自审项 | 结论 |
| --- | --- |
| 是否重复 runtime/policy/evidence/execution/requirements 职责 | 否——§2.3 不拥有表；碰撞/判定/调度/解析/定义均只消费 |
| 搜索失败/局部碰撞/全部过滤是否误判为任务不可行 | 否——§5.4 五类结局（枚举无不可行值）＋§6.5 边界图＋V-06~V-10；证明素材仅解析界限且不裁定 |
| 必验工况/采样计划/覆盖分母是否遗漏 | 否——§7.1 完成矩阵素材/§4.3 req.sampling-plans 依赖/§7.2 分母规则（V-11/13/14） |
| 未冻结样本集是否可进入正式证据 | 否——sampleSetIdentity 复核不一致即证据缺失（V-15） |
| 显示单位是否纳入无关计算依赖 | 否——§4.5 身份外清单；V-17 |
| 是否存在不稳定排序/线程漂移 | 否——§6.3 四键稳定排序＋§8.4 全序归并＋V-04/21 |
| Quick 是否可能冒充 Verified | 否——模式入身份＋screening-only 标记＋正式判定在 evidence（V-16） |
| 是否绕过 RuntimeNameMap/基座—世界变换 | 否——§5.1 唯一来源声明＋V-02；无名称拼串（R-4） |
| 取消/失败后是否可能发布完整结果 | 否——取消非结局＋incomplete 标记＋调用侧 envelope 构造边界（V-11/22） |
| 是否引入未经上游批准的新状态/阈值/证据等级 | 已审计——§14.4 全部为 kinematics 所有权内登记；判定阈值零私设（读 policy）；EvaluationMode 词表未扩展 |
| 是否越权修改需求/架构/其他单元机制 | 否——仅新建本卡；上游张力（P-KIN-2 注入点等）集中 §14.3；未改任何其他文件 |
| 旧代码覆盖与 UI 完整性 | §2.5 逐项对照（26 行：等价/增强 10、重定位 10、R2 分期 5、上游裁决 3 均注明理由）；UI 面板/十二条数据流/命令清单见 §9.8（v0.1 内置） |
| 遗留 | DETAILED-DESIGN.md 索引行与 traceability 机器索引同步归治理侧（同前两卡口径） |

### 14.6 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-22 | 首版草案（WP-15-T01 承接）：14 章全量——上游基线登记（REQUIREMENTS v1.16/ARCHITECTURE v0.12/十二单元卡 Draft 系/旧代码 62 文件实测）；KIN-01~14 承接总表＋拥有/消费/不拥有边界＋§2.5 旧代码功能对照（26 行）；输入快照与 AnalysisConfiguration schema（含身份外元素与依赖矩阵）；FK/指标契约（统一尺度规则定义）与 IK 契约（**五类结局判定表**——搜索未果铁律）；构型/解集/稳定排序；批量验证与区域覆盖（分母规则/零样本/样本集身份）；策略调用（构型级 vs 任务级边界图）与证据生成（只产不判）；确定性/并行/缓存契约与 execution 协作；7 必需接口契约＋15 个 KIN- 稳定码＋§9.8 插件界面（四面板＋十二条数据流＋命令清单，v0.1 内置）；26 行故障注入矩阵＋四套黄金数据集＋测试替身；WP-15-T01~T17 排序；双向交接清单；追踪矩阵；10 决策/5 风险/7 待裁决（含 P-KIN-2 运行时视图注入点）；13 项自审。状态 `Draft`，待评审 |

> 自审声明：本文档自审仅覆盖设计一致性、边界与上游对齐，不等同于实现测试通过或正式验收（acceptance-protocol.md 流程另行执行）。

