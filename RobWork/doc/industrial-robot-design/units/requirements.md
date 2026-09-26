# 工业机械臂设计软件 · requirements 单元详细设计（阶段 B）

> 2026-09-22 新建（WP-14-T01 承接）。本文是 requirements 单元的唯一详细设计：依据 REQUIREMENTS REQ-01～12 与 ARCHITECTURE §3.1/§3.3 的分配，把任务点/工作区域/工况数据模型、姿态规则、镜像阵列、CSV/JSON 业务映射、必验范围与采样计划定义、需求就绪校验、evidence/kinematics/trajectory/optimization 输入交接、命令与撤销/重做、插件界面写到可直接实现的深度。本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.9（2026-09-26，WP-14-T09 落位登记——§14.6；v0.8 2026-09-26 WP-14-T08 落位登记；v0.1 首版草案 2026-09-22，WP-14-T01 交付物） |
| 日期 | 2026-09-26（v0.9/v0.8）；2026-09-22（v0.1） |
| 状态 | **`Draft`**（待评审；DETAILED-DESIGN.md 单元状态表中的"requirements｜待产出"以本卡落盘为准，索引行同步由治理侧执行，本卡不代改） |
| 文档代号 | UNIT-REQ |
| 单元 | requirements（业务域单元，ARCHITECTURE §3.1：任务点/区域/工况定义、CSV/JSON 导入、姿态规则、镜像阵列、就绪校验；ARCHITECTURE §3.3 二分结构：零 Qt 计算库＋Qt Widgets 插件） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.12（`Draft`，2026-09-10）** |
| 协作输入 | `units/core.md` v0.11、`units/runtime.md` v0.15、`units/project.md` v0.18、`units/policy.md` v0.14、`units/io.md` 卡头 v0.9（§15.5 变更链至 v1.0；`Csv.hpp`/`Json.hpp` 已落位）、`units/diagnostics.md` v0.12、`units/evidence.md` v1.3、`units/ui.md` v1.5（文末登记 v1.6）、`units/modeling.md` v0.2（同批业务卡，对象分解与 P-MDL-1 裁决同源）、`units/testkit.md` v0.9——均为 **Draft/Draft-Structured（未冻结）**；本卡消费的签名以各卡当前文本为基线，冻结后按影响面增量同步（§14.3 P-REQ-8） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立）→ `units/*.md`（单元任务卡）。本文即 `units/requirements.md`，按任务卡深度编写 |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/requirements/`（**WP-14-T02 起为真实目标**：`sdurws_ird_requirements` STATIC（C++17、零 Qt、PUBLIC 链 core/diagnostics/project/io/evidence 五条 §3.2 登记边）＋`_test`/`_contract_test` 两测试目标＋配置期红线守卫；T02 公共面＝ObjectTypes/Errors/DiagCodes 三头；`_plugin` 随 WP-14-T08，见 §3.2、§11） |
| 任务归属 | `development-task-breakdown.md`（v0.16）WP-E／WP-14（T01～T09）；本文 §11 只做单元内部任务拆分与排序，不重排 WP 编号 |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送；Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`、逐个绝对路径启动 |
| 实现口径 | **从头构建**（REQUIREMENTS v1.9/v1.11 确立）：一切实现按需求与本文新建，不继承、不恢复任何历史实现源码。旧代码位置（用户提供 `D:\10_Source_Repos\old\src\rwslibs\engineeringrequirements`，实测 35 文件约 1.21 万行）**仅作功能范围对照**（§2.5），不构成实现继承或正确性背书 |

---

## 目录

1. 文档信息、上游基线与目标（§1）
2. 需求承接与职责边界（§2，含 §2.5 旧代码功能对照）
3. 单元组成、依赖与公共头文件布局（§3）
4. 需求数据模型（§4）
5. 任务点、区域与姿态规则（§5）
6. 工况、必验范围与采样计划（§6）
7. 镜像阵列、派生需求与导入映射（§7）
8. 需求就绪校验与下游依赖（§8）
9. 公共接口与跨单元协作（§9，含 §9.8 插件界面设计与界面逻辑）
10. 验证方案及故障注入矩阵（§10）
11. 阶段 B 实现任务拆分（§11）
12. 后续阶段承接与接口交接清单（§12）
13. 需求—设计—验证追踪矩阵（§13）
14. 设计决策、风险、待裁决项与变更记录（§14）

---

## 1. 文档信息、上游基线与目标

### 1.1 文档定位

requirements 是七阶段工作流的第二阶段（StageId 序列 `modeling → requirements → kinematics → …`，ui.md §6.4）的业务域单元：工程师在本单元把"机器人要做什么"形式化——关键任务点（位置/姿态/容差/约束分量）、接近-作业-撤离段、Box 工作区域及其采样定义、负载工况与必验范围、姿态规则与三维拾取捕获、工艺模板与镜像阵列；最终以领域命令提交为项目修订，并以**冻结的需求内容**作为 evidence 快照、kinematics 验证、trajectory 规划与 optimization 评估的统一任务输入。

三条不变立场（全文贯穿，任何章节冲突时以此为准）：

1. **requirements 只拥有"需求"（要什么），不拥有"求解"（怎么算）与"判定"（行不行）**：FK/IK/覆盖算法归 kinematics，快照/切片/证据 Profile/当前性归 evidence，工程判定（Feasible/Infeasible/DataInsufficient）按 §8.1 汇总优先级归 evidence 与各评估域。需求未就绪 ≠ 工程不可行（§8.3 正交表）。
2. **一切"应用修改"经 project 命令端口**（ARC-01）；不直接写项目目录；CSV/JSON 导入结果是草稿，不经确认与命令不产生修订；不存在第二冻结账本（附录 B 裁决排除"冻结工件双账本"——`REQ-06 草稿→应用→修订`取代旧 Check-and-Publish）。
3. **单一真值**：UI 控件不是需求数据；派生条目（镜像/阵列/模板产物）是独立对象、不覆盖源真值；不自行新增证据等级、阈值权威、工况覆盖规则或当前性状态（那些分属 evidence/policy/KIN-13）。

### 1.2 上游与磁盘现状登记（2026-09-22 实测）

| 输入 | 磁盘版本 | 状态 | 本卡消费方式 |
| --- | --- | --- | --- |
| `REQUIREMENTS.md` | v1.16（2026-09-09，含 C1～C8） | `Accepted` | 需求语义与验收唯一权威；REQ-01～12、CON-01～06、EVI-01/02（§8.1 表 1~4）、KIN-01~04/12/13、TRJ-01/02、OPT-01~04/07、PM-04/12、UX-02/04/06/12、NFR-COR/MNT、AT-02/04/05/09/10/23/24/27/30、附录 D |
| `ARCHITECTURE.md` | v0.12（2026-09-10） | `Draft` | 单元边界、二分结构（§3.3）、端口（§7.2）、快照管线（§7.6）、会话态规则（§7.7） |
| `AGENTS.md`（仓库根） | 当前工作区版本 | 生效中 | 编码与协作约定 |
| `units/core.md` | v0.11 | `Draft-Structured` | ObjectId/ContentVersion/SourcedValue/ValueProvenance/Units/Compare/DiagData/Events 契约 |
| `units/testkit.md` | v0.9 | `Draft-Structured` | 黄金数据集契约（`ird-golden-manifest/1`）、容差档案、边角样例强制项（§10 消费） |
| `units/project.md` | v0.18 | `Draft` | ProjectCommandService/ICommandHandler/CommandPlan/DraftService/IProjectQueryPort/StaleRevisionRejected/objectTypeToken 元数据 |
| `units/evidence.md` | v1.3 | `Draft-Structured` | **RequiredCaseSet**（§4.1.3：`{caseId, label, enabled, mandatory}`＋requiredCaseSetId；P-EV-9 最小承载已实现）、**SamplingPlanRef**（§4.1.4：`{regionObjectId, planContentIdentity, plannedPositionSamples/plannedPoseSamples, sampleSetIdentity}`）、**ReadinessSummary**（`{valid, invalidMustItems[]}`——①级输入门禁数据源）、DependencyKey 角色键约定、§13 交接清单 B·modeling/requirements 行 |
| `units/runtime.md` | v0.15 | `Draft-Structured` | Frame/ObjectId 解析（RuntimeNameMap 只读消费经装配）、P-MDL-1 同源裁决面（子条目 ObjectId 与 objectRefs 关系） |
| `units/policy.md` | v0.14 | `Draft-Structured` | EngineeringPolicySet 只读消费（本单元仅消费判定阈值类别的存在性声明，不读取具体阈值——需求侧"要求值"与策略"判定阈值"分离，见 §6.4） |
| `units/execution.md` | 存在（未逐条消费） | — | requirements 不直接依赖 execution（评估派发经 evidence/L5） |
| `units/diagnostics.md` | v0.12 | `Draft` | StableCodeRegistry 注册协议、`REQ-` 前缀所有权（业务域前缀词表 MDL/**REQ**/KIN/…）、IDiagnosticFactory/IDiagnosticSink |
| `units/ui.md` | v1.5（文末登记 v1.6） | `Draft` | IDraftController/IPluginUiRegistrar/IPluginUiModule、DomainReadinessItem、七态状态词、View3D 拾取契约 |
| `units/io.md` | 卡头 v0.9／§15.5 至 v1.0 | `Draft` | **`Csv.hpp` 已落位**（CsvDialect/CsvCell/RawTable/CsvParseReport/ICsvReader/ICsvWriter、方言标识行 v1、可逆转义、行粒度部分成功）、**`Json.hpp`**（JSON 解析/写出，JsonProfile 注册制）、SafePath/BudgetGuard/IResourceReader/IResourceSnapshotter |
| `units/modeling.md` | v0.2（本日同批产出） | `Draft` | 对象分解模式（根＋集合对象）、P-MDL-1 裁决口径、工具/场景对象 token（`tool-definition`/`scene-object`——本单元跨聚合引用的目标） |
| `DETAILED-DESIGN.md` | 索引 | — | requirements 行登记"待产出／WP-E" |
| `development-task-breakdown.md` | v0.16 | `Draft` | WP-14-T01～T09、§3 追踪矩阵 REQ 行、§4 O-14（P-EV-9）、§5 构建约定 |
| 代码落位 | `industrialrobot/requirements/`：仅 `include/sdurws/ird/requirements/README.md` 保留位 | — | 无单元级 CMakeLists.txt、无产品源码、无 `_plugin/_test` 目标（WP-14-T02 建）；上级门禁白名单无 requirements 出边（§3.4 登记应增边） |

缺失文件登记：用户清单所列输入文件**全部存在**，无缺失；`units/requirements.md` 本身此前不存在，即本卡（新建 v0.1）。

### 1.3 交付目标

本卡落盘后，后续单元应能依据统一契约：

- 创建和编辑任务点、区域、工况（含接近/作业/撤离段与受约束/自由位姿分量）；
- 管理位置、姿态、方向、容差和约束（五种姿态规则，解析可定位）；
- 导入 CSV/JSON 需求数据（字段字典冻结、逐行错误、部分成功、副本导出）；
- 生成镜像阵列和派生任务（独立身份、来源可溯、模板关联可更新/解除）;
- 冻结必验工况和区域采样计划（P-EV-9 schema 冻结；SamplingPlanRef 内容身份来源）；
- 提供任务需求就绪校验（ReadinessSummary 数据源；REQ-06 预览/正式分离）；
- 为 evidence、kinematics、trajectory、optimization 提供稳定输入切片（角色键约定＋内容身份）；
- 通过 project 命令提交修改并支持草稿、撤销和重做。

### 1.4 非目标（本卡明确不做）

- 不实现 FK/IK、覆盖率计算、样本生成、碰撞、轨迹规划、动力学、选型、优化算法（kinematics/trajectory/dynamics/selection/optimization）；区域**采样定义**归本单元，**样本生成与冻结**归 kinematics/evidence（KIN-04 R8）；
- 不实现工程判定（Feasible/Infeasible/DataInsufficient 判定归 evidence §8.1 汇总）；"有效 Must 未满足判不可行"由评估域执行，本单元只供输入合法性（ReadinessSummary）；
- 不实现快照/切片/证据 Profile/当前性（evidence）；不实现 CSV/JSON 底层解析（io）；不实现项目目录写入/事务/锁（project）；
- 不引入新证据等级、判定阈值权威、工况覆盖规则、当前性状态（分属 evidence/policy/KIN-13）；
- 不实现报告渲染；不提前接入阶段 C/D 真实轨迹/动力/选型/优化流程；
- 本次不启动任何 GUI 程序（§10 仅设计测试流程）。

---

## 2. 需求承接与职责边界

### 2.1 REQ 家族承接总表

| 需求 | 摘要（语义以 REQUIREMENTS 原文为准） | 本卡设计落点 |
| --- | --- | --- |
| REQ-01 | 关键任务点、参考坐标系、TCP、受约束/自由的位姿分量、位置/姿态容差、Must/Should | §4.3（TaskPoint/PoseConstraint/ToleranceSpec）、§5.1 |
| REQ-02 | 接近、作业和撤离段；沿工具轴或参考轴表达方向与距离 | §5.1（TaskSegment 三段） |
| REQ-03 | Box 工作区域、位置采样、姿态采样和最低覆盖率 | §5.2（WorkRegion＋SamplingPlan 定义） |
| REQ-04 | 引用环境障碍（MDL-15 场景对象）、负载工况、夹取/释放、驻留、最小关节裕量、碰撞要求、目标节拍 | §6.1（OperatingCondition） |
| REQ-05 | CSV 坐标表导入：字段映射、单位预览、逐行错误；CSV 按数据解析 | §7.3（导入管线与字段字典） |
| REQ-06 | 草稿随时保存；计算前就绪校验；预览只查有效条目不产生正式证据；启用 Must 非法→"输入未完成"；有效 Should 未满足→警告 | §8（分层就绪＋ReadinessSummary）、§7.5（预览分离） |
| REQ-07 | 搬运/上下料等工艺任务模板与重复任务复用 | §7.1（ITemplateArrayService 工艺模板） |
| REQ-08 | 三维场景创建/拖动/检查目标，写回前必须确认 | §9.8（拾取与捕获确认流） |
| REQ-09 | 五种姿态规则；规则应用时解析并记录来源；解析失败可定位诊断 | §5.3（OrientationRule 五规则） |
| REQ-10 | 几何特征拾取与 TCP 捕获进入草稿；未应用不失效 | §9.8（L-R6/L-R7）、§5.1 来源标记 |
| REQ-11 | 工位镜像（过参考系镜像面）、批量阵列（线性/矩形/圆形/三维折线）、需求集撤销/重做（批量整体回滚） | §7.2（镜像/阵列）、§9.3（两级撤销） |
| REQ-12 | JSON 导入/导出（与 CSV 同一字段字典与安全规则）；导出为副本不影响项目 | §7.4（JSON 通道） |

### 2.2 其他家族承接（与 requirements 相关行）

| 需求 | requirements 侧承接 |
| --- | --- |
| EVI-01/02 | 必验工况 schema 冻结（P-EV-9：`enabled`＋`mandatory≡(level==Must)`，§6.3，冻结后回接 evidence §4.1.3）；空必验集不阻断编辑（evidence ④级保守处置，§6.3）；多方案比较基准一致性由 sampleSetIdentity/requiredCaseSetId 承载（§6.2） |
| CON-01/02/05/06 | ObjectId＋ContentVersion 包络；修订闭包引用；需求变更→切片失效（§8.4 依赖声明）；快照含已解析策略与 RuntimeNameMap 由 evidence/runtime 组装，本单元不重复 |
| KIN-01~04 | 任务点/区域/工况消费契约（§8.2）；采样计划定义→样本生成（KIN-04 R8：分母＝计划样本总数，`plannedPositionSamples/plannedPoseSamples`）；覆盖率目标（REQ-03）供 KIN-04 判定 |
| KIN-12/AT-27 | 显示单位是纯投影（core 唯一换算），需求对象恒 SI——显示单位切换不触发无关失效（V-17） |
| TRJ-01/02 | 任务顺序与接近/作业/撤离段消费契约（§8.2） |
| OPT-01~04/07 | 需求基准（Must 工位/区域/必验工况）作为 OPT-B 硬约束输入；候选评估消费同一冻结需求快照（§8.2） |
| PM-04/12 | 保存（drafts 模块 token `requirements`）与应用（恰好一个新修订）分离；StaleRevisionRejected 草稿 `apply-retained` 保留（§9.3） |
| UX-02/04/06/12 | 工程用语；高级参数不落本单元（求解配置归 KIN-13）；状态词与图例归 ui 投影（本单元只供 `inputComplete`/缺项数据）；阶段门控判定归 workflow |
| NFR-COR-01/02/03 | 黄金数据集（搬运任务/部分位姿约束/区域/负载事件）；确定性（同输入→同字节、稳定排序）；非有限数/非法单位/缺失引用不得静默转 0 或默认通过 |
| NFR-MNT-01/03/07 | 计算库零 Qt 可直调；单位/状态词/诊断文案单一权威（core/ui/diagnostics）；无名称前缀操作、无策略副本、无影响计算的本地开关 |
| AT-02/04/05/09/10/23/24/27/30 | 观测点设计见 §10 矩阵 |

### 2.3 拥有／消费／不拥有

| 类别 | 内容 |
| --- | --- |
| **拥有** | 任务点（TaskPoint）与三段（接近/作业/撤离）；工作区域（WorkRegion，Box 几何＋采样定义）；工况（OperatingCondition）与必验/可选标记（P-EV-9 schema 权威）；姿态与方向规则（OrientationRule 五种）；位置/姿态容差（ToleranceSpec）；镜像阵列与派生条目（含工艺模板词表、生成溯源）；区域采样计划**定义**（SamplingPlan）；CSV/JSON 字段字典与业务语义校验；需求就绪校验（ReadinessSummary 数据源）；需求命令处理器族；面向下游的只读需求视图与 DependencyKey 角色键约定；需求变更摘要；需求对象 canonical 编码（core §6.3 分工登记） |
| **消费** | core：身份/SourcedValue/单位/比较/诊断契约/事件；project：①命令端口、HandlerContext、查询端口、DraftService、对象分配；io：CSV/JSON 解析（RawTable/Json）、SafePath/BudgetGuard、读取快照、AtomicFile（副本导出）；evidence：快照/切片契约（请求方角色、角色键约定回授）；runtime：Frame 引用解析（经装配的名称/模型只读视图）；diagnostics：稳定码注册/工厂/目录；ui：DraftController/IPluginUiRegistrar/SelectionModel/View3D 拾取/七态投影数据源；policy：判定阈值类别的存在性（需求侧"要求值"与策略"阈值"分离，§6.4）；modeling：无编译依赖——仅以 ObjectTypeToken 元数据交叉引用其对象（§8.1 边界） |
| **不拥有** | RobotDesign/CanonicalModel/模型编译（modeling/runtime）；FK/IK/覆盖算法/样本生成（kinematics）；碰撞/轨迹/动力学/选型/优化算法；证据等级、工程判定、结果当前性（evidence）；任务调度/worker/检查点/缓存（execution）；项目目录/锁/HEAD/事务（project）；CSV/JSON 底层解析（io）；分析配置（采样预算/种子/迭代——KIN-13）；判定阈值权威（policy）；报告与界面布局（reporting/ui） |

### 2.4 边界红线自查锚点（与 §14.5 自审清单对应）

| 红线 | 本卡保障位置 |
| --- | --- |
| 不重复 project/io/evidence/runtime/kinematics 职责 | §2.3 不拥有表；§7.3/§8.1 边界 |
| 区域定义与采样算法不混淆 | §5.2/§6.2（定义归本单元；生成/冻结归 kinematics/evidence——KIN-04 R8） |
| 需求未就绪 ≠ 工程不可行 | §8.3 四轴正交表 |
| 必验工况/引用完整性/样本冻结不遗漏 | §6.3（P-EV-9 冻结）、§8 就绪 R1/R5/R6、§6.2（SamplingPlanRef 来源） |
| 无第二套坐标变换/姿态语义 | §5.3（core 位姿约定＋runtime 统一解释；规则解析不重定义变换） |
| UI 控件不作需求真值 | §4.6 三态、§9.8（面板不缓存权威数据） |
| 不绕过 ProjectCommandService | §9.3 命令族；导入仅产草稿 |
| 派生对象不覆盖源需求 | §7.2（独立 ObjectId＋生成溯源） |
| 不引入未经上游批准的新状态/阈值/证据等级 | §14.4 登记制（词表均为 requirements 所有权内登记；Info 等级不承接——§2.5） |
| 不越权修改需求/架构/其他单元 | §14.3 待裁决集中登记（含 P-EV-9 回接义务） |

### 2.5 旧代码（`old/src/rwslibs/engineeringrequirements`）功能范围对照

口径声明（REQUIREMENTS 附录 A）：从头构建口径下旧代码**仅作功能范围对照**——本表把旧实现（35 个文件、约 1.21 万行：三页 UI、RequirementSet/KeyStation/BoxRegion 数据模型、RequirementCompiler 语义编译、RequirementFreezer 冻结工件双账本、StationTemplateService 工艺模板、StationImportService CSV/JSON 导入、几何拾取/姿态规则解析器、区域三维预览、撤销栈、JSON 序列化与迁移）逐项映射到需求 ID 与本卡落点；**不构成实现继承或正确性背书**。图例：✅＝等价/增强承接；♻＝架构重定位承接；⏸＝分期承接；❌＝按上游裁决或需求范围不承接。

| 旧功能（来源） | 需求/处置（附录 A） | 本卡落点 | 承接状态 |
| --- | --- | --- | --- |
| 三页 UI（工位/区域/校验发布，EngineeringRequirementsWidget） | REQ 家族·AT-02/23/24｜等价承接 | §9.8（四面板：工位/区域/工况/校验；工况页为 REQ-04 新增承接——旧无工况对象） | ✅（信息架构重组：页签→对象树＋属性投影＋阶段面板） |
| 工位增/复制/删、检查器编辑（KeyStation 表＋Inspector） | REQ-01·AT-02｜等价承接 | §5.1、§9.8 L-R2 | ✅ |
| TCP 捕获（captureCurrentTcp，消费主程序发布的 State 快照） | REQ-10·AT-23｜等价承接 | §9.8 L-R7（经 ui View3D/runtime 只读视图捕获入草稿；插件不持 `WorkCell*`/`State` 副本） | ♻（状态来源从插件自持改为 ui 会话契约） |
| 几何特征拾取（Ctrl+双击，GeometryFeatureResolver 对 WorkCell 解析） | REQ-10·AT-23｜等价承接 | §9.8 L-R6（拾取契约归 ui View3D；引用以 ObjectId 承载） | ♻ |
| 姿态规则（OrientationMode 四模式＋allowToolRollFree 修饰＋滚转范围） | REQ-09·AT-23｜等价承接 | §5.3（五规则枚举与 REQ-09 一一对应；解析记录来源/失败可定位） | ✅（五规则显式化） |
| 工艺模板六类（BinPicking/MachineTending/Palletizing/Inspection/ToolChange/Handover＋实例关联/参数快照/一键更新/解除关联） | REQ-07·AT-24｜等价承接 | §7.1（模板词表＋生成溯源 `{generatorId, instanceId, linked, parameters}`） | ✅（词表为 requirements 所有权的登记，§14.4） |
| 批量阵列四类（线性/矩形/圆形/三维折线；行列层/间距/角度/半径可配） | REQ-11·AT-24｜等价承接 | §7.2 | ✅ |
| 工位镜像（过参考系镜像面） | REQ-11·AT-24｜等价承接 | §7.2（镜像位姿变换＋不可镜像规则降级诊断） | ✅ |
| 需求集撤销/重做（RequirementSetUndoStack 快照栈 32 层） | REQ-11·AT-24｜等价承接 | §9.3（ui 局部撤销栈 `pushLocalCheckpoint` 族——项目级撤销归命令服务） | ♻（承载点从域内自建栈移至 ui DraftController） |
| Must/Should/容差/关节余量/可操作度裕度（ValidationPolicy） | REQ-01/04·AT-02｜等价承接 | §4.3（ToleranceSpec）＋§6.1（条件级 demands） | ✅／♻（区域级判定参数迁移至工况统一承载；判定阈值权威仍归 policy——本单元只存"要求值"） |
| 盒形区域编辑（间距采样＋上限常量 64/轴·1000 方向·360 滚转钳制） | REQ-03·AT-02｜等价承接 | §5.2（网格计数/间距两种定义，构建时规范化为计数；**上限常量不承接为需求侧规则**——预算归 KIN-13，计划仅校验非退化） | ♻（预算权威重定位） |
| 区域 3D 预览着色（WorkspaceRegionSceneVisualizer：区域框＋Good/Weak/Failed 格元） | KIN-07/UX-11·AT-03｜等价承接 | §9.8（区域轮廓/采样格预览经 ui View3D 契约；结果着色归 kinematics KIN-07） | ♻（预览介质从插件自绘 Drawable 改为 ui 视图契约） |
| 语义编译校验（RequirementCompiler validateDetailed/compile，REQ_* 诊断码） | REQ-06·AT-02｜等价承接 | §8（分层就绪＋`REQ-` 稳定码登记表）；`fingerprint()`→项目 ContentVersion 取代 | ✅／♻（指纹按附录 B 裁决排除） |
| 冻结发布 Check-and-Publish（RequirementFreezer＋FrozenRequirementArtifact＋fingerprint＋发布信号） | 附录 B 裁决排除（冻结工件双账本）| §1.1 立场 2、§9.3（草稿→应用→修订取代） | ❌→✅ 按上游裁决 |
| 冻结状态展示（Draft/Published 标签） | PM-11/UX-10·AT-20｜等价承接 | §9.8（ui 七态/草稿脏标记投影取代自绘标签） | ♻ |
| 关键工位 CSV 导入（逐行诊断 recordNumber+message、部分成功） | REQ-05·AT-02｜等价承接 | §7.3（io RawTable 行粒度部分成功＋字段字典＋单位预览） | ✅ 增强（解析下沉 io，方言标识/转义 roundtrip 获得统一防护） |
| 需求 JSON 导入/导出（RequirementSetJson；导出副本不影响项目） | REQ-12·AT-24｜等价承接 | §7.4（同一字段字典；io AtomicFile 副本导出） | ✅ |
| 机器人模型绑定（RobotModelBinding：路径＋SHA256 指纹＋名称） | ARC-04·AT-20｜等价承接（修订绑定取代指纹绑定——附录 A.3 行） | §4.6（同一修订闭包内 robot-design 与 req-set 天然绑定；不存模型路径/指纹） | ❌→✅ 按上游裁决 |
| 项目文档集成（load/save/dirty 字节快照/markClean/迁移 v3→v4） | PM-04·AT-21｜等价承接 | §4.6/§9.3（DraftService＋ui DraftController）；schema 演进走 NFR-DEP-04 版本拒绝＋project 升级器口径 | ♻ |
| 插件动态加载元数据（Q_PLUGIN_METADATA） | NFR-SEC-04｜附录 B 裁决排除 | §3.1/§3.2（静态白名单＋IPluginUiRegistrar） | ❌→✅ 按上游裁决 |
| `extensions` QJsonObject 未知字段往返保留 | （旧实现策略） | §4.7（schema 版本化＋未知主版本拒绝；不设自由扩展袋——NFR-DEP-04 口径） | ❌ 有意差异（注明理由） |
| RequirementLevel::Info 信息级 | （旧词表） | §4.3（EVI/REQ-06 词表仅 Must/Should；信息归条目 `note` 字段） | ❌ 有意差异（上游词表无 Info） |
| 工位可信度权重 confidence [0,1] | （旧实现字段） | — | ❌ 不承接（无需求依据；权重类语义属 OPT 变量/策略域，需则走需求变更） |
| ProcessType 11 类工艺语义 | （旧词表，REQ-07 模板语义载体） | §4.3（`processTag` 词表登记，模板/报告消费，§14.4） | ✅ 词表登记制 |

**对照结论**：需求级功能**全覆盖**——等价/增强承接 13 项、架构重定位 6 项、按上游裁决或无需求依据不承接 5 项（均注明理由与替代语义）；工况对象（REQ-04/EVI-02）与采样计划冻结表达（KIN-04 R8）为上游新增需求，旧代码无对应物（旧"场景快照/冻结工件"部分语义由 evidence AnalysisSnapshot 取代）；实现层面旧代码零拷贝。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 单元组成（ARCHITECTURE §3.3 二分结构）

```
sdurws_ird_requirements            计算库（零 Qt，STATIC）：需求值模型与 canonical 编解码、
                                   任务点/区域/工况服务、姿态规则、镜像阵列/模板、CSV/JSON 业务映射、
                                   采样计划定义、就绪校验、命令处理器——可被模型测试直调（NFR-MNT-01）
sdurws_ird_requirements_plugin     插件界面（Qt Widgets）：工位/区域/工况/校验面板、导入向导域侧页、
                                   拾取与捕获确认流（§9.8）；只消费计算库公共接口＋ui 端口
sdurws_ird_requirements_test       单元测试（googletest）
sdurws_ird_requirements_contract_test  跨单元契约测试（project/io/evidence/diagnostics 对端面）
```

- 命名空间：`sdurws::ird::requirements`（测试 `sdurws::ird::requirements::test`）；命名与 include guard 风格同 modeling.md §3.1；C++17 显式、`/utf-8`、第三方仅经 vcpkg。
- 无 `_worker` 目标：命令在主进程命令执行线程串行；CSV/JSON 导入映射为可预测 <1 s 的同步计算（NFR-PERF-01 内联门槛），万行级导入如实测超时再评估工作进程化（走设计变更）。

### 3.2 目标升级与门禁边登记（WP-14-T02 执行清单）

- `requirements/CMakeLists.txt` 新建：`sdurws_ird_requirements` INTERFACE→STATIC（目标名/别名不变）；`_test`/`_contract_test` 随文件注册（自持 `ird_add_gtest` 宏）；`_plugin` 随 WP-14-T08 落位；配置期红线守卫随文件自持（R-1/R-3 FATAL_ERROR，照抄 core/io 形态）。
- `ird_gates` 白名单应增边（WP-14-T02 登记；均属 §3.5"业务域→L2/L3 公共接口"既有许可方向的实例化）：

| 边（requirements →） | 用途 |
| --- | --- |
| core | 身份/SourcedValue/单位/比较/诊断契约/事件 |
| diagnostics | 稳定码注册、IDiagnosticFactory/IDiagnosticSink |
| project | ICommandHandler/CommandPlan/HandlerContext/DraftService/查询端口 |
| io | Csv/Json 解析值与服务、SafePath/BudgetGuard、AtomicFile |
| evidence | ReadinessSummary/Snapshot 角色键值类型（请求方角色；无评估器注册） |
| runtime | 无编译依赖——仅经 L5 装配的只读解析视图（如需引用其值类型则登记此边） |

- 插件目标 → 本单元计算库＋`sdurws_ird_ui`；**禁止链接任何其他业务域单元（R-1）**；跨业务交叉引用（任务点→工具/场景对象）只以 ObjectTypeToken 元数据＋ObjectId 承载（§8.1）。

### 3.3 公共头文件布局（`requirements/include/sdurws/ird/requirements/`，随 §11 任务落位）

| 头 | 内容 | 任务 |
| --- | --- | --- |
| `ObjectTypes.hpp` | 对象类型 token、`processTag`/模板/阵列词表、schema 版本常量 | T02/T03 |
| `RequirementTypes.hpp` | TaskPoint/WorkRegion/OperatingCondition/SamplingPlan/PoseConstraint/OrientationRule/ToleranceSpec/RequirementReference/RequirementProfile 值模型 | T03 |
| `Codec.hpp` | `IRequirementCodec`：需求对象 canonical 编码/解码（确定性序列化登记，core §6.3 分工） | T03 |
| `Editor.hpp` | `IRequirementEditor`（编辑器与局部撤销） | T03 |
| `Services.hpp` | `ITaskPointService`/`IWorkRegionService`/`IOperatingConditionService` | T03 |
| `TemplateArray.hpp` | `ITemplateArrayService`（工艺模板/镜像/阵列/重生成） | T07 |
| `Import.hpp` | `IRequirementImporter`（CSV/JSON 字段映射、单位预览、副本导出） | T04 |
| `Sampling.hpp` | `ISamplingPlanBuilder`（区域采样**定义**构建与规范化；不含样本生成） | T03 |
| `Readiness.hpp` | `IRequirementReadinessChecker`＋`ModelReadinessReport` 同构的 `RequirementReadinessReport`＋ReadinessSummary 投影 | T05 |
| `OrientationResolution.hpp` | 姿态规则五规则浅解析（确定性参考姿态/方向＋resolution 留痕＋可定位诊断——§5.3 隔离声明的落位） | T06 |
| `Capture.hpp` | 三维拾取/TCP 捕获域侧消费面（`IRequirementCaptureService`——确认门写回/REQ-CAPTURE-STATE-STALE 产码/未应用不失效，§9.8 L-R6/L-R7） | T06 |
| `CommandHandlers.hpp` | `IRequirementCommandHandler` 基类与命令族 | T05 |
| `Errors.hpp`/`DiagCodes.hpp` | `RequirementError(code)` 与 `REQ-` 稳定码清单（§9.6） | T02 |
| `README.md` | 已存在保留位 | — |

### 3.4 线程与确定性总约定

1. 纯函数服务（Codec/Services/TemplateArray/Importer/Sampling/ReadinessChecker）：无共享可变状态、可重入、并发安全；确定性：同输入→同输出字节，集合按 ObjectId 规范文本字典序稳定排序（NFR-COR-02）。
2. `IRequirementEditor` 非线程安全（仅 UI 线程）；命令处理器仅命令执行线程调用（project 串行槽）。
3. 一切物理量恒 SI（m/rad/kg/s），导入边界的单位换算唯一经 core `Units.hpp`；CSV/JSON 中的单位声明是**导入选项**，落库前归一（REQ-05 单位预览）。
4. 需求条目名称（`name`）是语义字段：进报告/诊断定位/覆盖清单，重命名＝内容变更＝新 ContentVersion（与 modeling 的 displayName 分层不同——见 §4.8 决策 D-REQ-6）。

---

## 4. 需求数据模型

### 4.1 对象分解与对象类型登记（objectTypeToken，登记于 `ObjectTypes.hpp`）

| objectTypeToken | 对象 | 内容概要 | 独立成对象的理由 |
| --- | --- | --- | --- |
| `req-set` | TaskRequirementSet 根/聚合 | 名称、条目集合引用表、需求档派生源 | 编译入口对象；修订闭包锚 |
| `req-point-set` | 任务点集合（单对象内条目数组） | 全部 TaskPoint 条目 | 任务点可达数千条（NFR-PERF-03 规模口径），逐点建对象会爆炸闭包；集合对象一条依赖键即可表达"任务点变更"失效面 |
| `req-region-set` | 区域集合 | 全部 WorkRegion 条目 | 区域少量；与点集对称 |
| `req-condition-set` | 工况集合 | 全部 OperatingCondition 条目 | 工况变更独立失效面（不影响点位几何切片的字节） |
| `req-plan-set` | 采样计划集合 | 逐区域 SamplingPlan 条目 | 计划变更独立失效面（对应 SamplingPlanRef.planContentIdentity 来源） |

**闭包形态**：根对象仅持四个集合对象的 ObjectId 引用（不含 cv）；修订闭包由 project 累积组合。姿态规则/容差/三段/生成溯源**内嵌**于条目（不独立成对象）。五个 token 均为本单元所有权登记（§14.4）；子条目 ObjectId 经 `HandlerContext.objectId()` 分配、跨修订稳定、内嵌于集合对象字节——与 P-MDL-1 同源口径（子条目 ID 是模型内标识，objectRefs 复核范围限于真实存储对象；裁决引用 P-REQ-2，§14.3）。

### 4.2 TaskRequirementSet 根对象字段表

| 字段 | 类型 | 约束/说明 |
| --- | --- | --- |
| `schemaVersion` | `std::uint32_t` | ≥1；需求对象 schema 演进单点（NFR-DEP-04：主版本不识别→稳定拒绝＋升级指引，升级器归 project 口径） |
| `name` | `std::string` | 需求集语义名（进报告） |
| `pointSetRef/regionSetRef/conditionSetRef/planSetRef` | `std::optional<ObjectId>` | 指向四个集合对象；无重复 |
| `note` | `std::string` | 备注（不入内容身份语义，但随对象字节） |

四个集合对象结构对称：`{ entries: vector<Entry> }`；条目按 `objectId` 规范文本字典序存放（canonical 编码确定性，I-REQ-1）。

### 4.3 TaskPoint 条目字段表（REQ-01/02/09/10）

所有数值字段 `core::SourcedValue<T>` 四态；单位 SI；位姿在 `refFrame` 参考系下表达，**与机器人基座的关系由 runtime 编译/解析统一解释（本单元不做坐标变换）**。

| 字段 | 类型/单位 | 说明与约束 |
| --- | --- | --- |
| `objectId` | `core::ObjectId` | 创建时一次分配，跨修订稳定；诊断 `subjectObjectId` 锚 |
| `name` | `std::string` | 语义名，集合内唯一（I-REQ-3）；改名＝内容变更 |
| `processTag` | `ProcessTag` 词表 | `Generic/Pick/Place/MachineLoad/MachineUnload/Inspect/WeldStart/WeldEnd/ToolChange/SafeStandby/Handover`（模板/报告消费；§14.4 登记制） |
| `level` | `Must \| Should` | 上游词表仅两级（`Info` 不承接，信息归 `note`——§2.5） |
| `enabled` | `bool` | 启用状态；未启用条目不进入正式就绪判定（REQ-06"任一**启用的** Must 条目非法"口径） |
| `source` | `core::ValueProvenance` | `UserProvided`（手工/捕获，methodTag=`captured-tcp` 等）/`ImportMapped`（CSV/JSON）；镜像/模板产物亦为 `UserProvided`＋methodTag=`gen/<generatorId>`（独立可编辑条目，非 DerivedReadOnly——D-REQ-4） |
| `generation` | `std::optional<GenerationProvenance{generatorId, instanceId, linked, parameters[]}>` | 模板/阵列/镜像溯源（REQ-07/11）；`linked=false`＝已解除关联 |
| `importProvenance` | `std::optional<{sourceDigest: core::Digest256, recordNumber}>` | 导入溯源（源文件**内容摘要**＋行号；路径不入身份——I-REQ-8） |
| `refFrame` | `RequirementReference{World \| ModelFrame{oid} \| SceneObject{oid}}` | 参考坐标系（默认 World） |
| `tcpRef` | `std::optional<RequirementReference{Tool{oid, tcpKey} \| DefaultTcp}>` | 工具/TCP 引用（REQ-01；引用 modeling `tool-definition` 对象，浅校验见 §8.1） |
| `pose` | `PoseConstraint` | 见下 |
| `tolerance` | `ToleranceSpec` | 见下 |
| `approach/work/retract` | `TaskSegment{enabled, axis: ToolZ\|ReferenceZ, distanceM: double}` | 三段（REQ-02）；`work` 段即任务点本身（enabled 恒 true，占位表达段序） |
| `demands` | `{collisionFreeRequired: bool, minimumJointMargin: std::optional<double>（rad/m 依关节类型）}` | 任务级"要求值"（REQ-04）；判定阈值权威仍归 policy（§6.4 分离声明） |
| `sequenceKey` | `std::optional<std::string>` | 任务顺序键（顺序集＝按 key 拓扑消费；无环校验 R7） |
| `conditionScope` | 继承工况侧 `appliesTo`（点侧不反向登记） | 绑定方向唯一（D-REQ-5） |
| `note` | `std::string` | 备注（吸收旧 Info 级信息） |

**PoseConstraint**（受约束/自由位姿分量，REQ-01）：

| 字段 | 类型 | 约束 |
| --- | --- | --- |
| `position` | `SourcedValue<rw::math::Vector3D<double>>`（m，refFrame 系） | Provided 时三分量有限 |
| `constrainedDof` | `struct {x,y,z,roll,pitch,yaw: bool}` | 六分量受约束掩码（false＝该分量自由，供下游解空间解释；全 false 非法——任务点至少约束一个分量，I-REQ-5） |
| `orientation` | `OrientationRule` | §5.3 五规则 |

**ToleranceSpec**：`{positionTolerance: double（m，>0；默认 1×10⁻³）, orientationTolerance: double（rad，>0；默认 π/180）}`——默认值登记为设计默认（黄金数据集锁定）；**比较口径遵循附录 D 通用比较公式**（逐元素、零参考退化 ε_abs）；容差是"要求值"，不是判定阈值（评估侧按此校验残差）。

### 4.4 WorkRegion 条目字段表（REQ-03；详见 §5.2）

`{objectId, name, level, enabled, source, generation/importProvenance, refFrame, tcpRef, box: {center: Vector3（m）, size: Vector3（m，三分量>0，I-REQ-6）}, positionSampling, orientationSampling, coverageTargets: {minPositionCoverage: double [0,1], minOrientationCoverage: std::optional<double>}, demands（同任务点）, sequenceKey?, note}`。覆盖率目标 REQ-03 默认 0.8 登记为设计默认（黄金锁定）。

### 4.5 OperatingCondition 条目字段表（REQ-04；详见 §6.1）

`{objectId, name, level（Must|Should）, enabled, environmentRefs: vector<SceneObject oid>, toolRefs: vector<{oid, tcpKey}>, payloads: [{toolRef, mass（kg）, com?（m）, inertia?（kg·m²）} 全 SourcedValue], events: [{type: Grasp|Release|Dwell, stationRef: Point oid, durationS?}], processParams: {targetCycleTimeS?: double}, demands, verificationOrderHint?: uint32, appliesTo: {scope: AllStations | Stations[oid…] | None}, note}`。

### 4.6 草稿态与已应用修订（三态，与 modeling.md §4.9 同构）

| 维度 | 编辑态（编辑器工作集） | 草稿态（drafts/，module=`requirements`） | 已应用修订态（objects/＋闭包） |
| --- | --- | --- | --- |
| 身份承诺 | 无（不冒充快照） | baseRevisionId 锚＋canonical 载荷 | ObjectId＋ContentVersion 完整包络 |
| 消费者 | 本插件面板＋就绪校验 | 打开恢复/关闭对话 | evidence 快照组装、各域评估（唯一可计算形态） |
| 撤销 | 编辑器局部撤销（零修订） | 丢弃/`apply-retained` 保留 | 项目级逆命令（新修订） |
| 失效 | 不触发 | 不触发 | 修订事件→切片当前性重算 |

### 4.7 合法/非法实例与不变量（`RequirementTypes.hpp` 校验层）

- **I-REQ-1（确定性编码）**：集合条目按 ObjectId 字典序存放；同输入字节→同 ContentVersion（project 计算 SHA-256）。
- **I-REQ-2（身份不混用）**：任务点/区域/工况/计划条目的 ObjectId 全局唯一且 token 与所属集合一致；跨集合重复＝调用方错误（构造边界拒绝）。
- **I-REQ-3（名称唯一）**：同集合内 `name` 唯一（报告/覆盖清单定位依据）；重复＝构造边界拒绝（不静默加后缀——NFR-COR-03）。
- **I-REQ-4（引用合法）**：`RequirementReference` 的 kind 与目标 token 匹配（Tool→`tool-definition`、SceneObject→`scene-object`…）；悬空/错 token＝就绪 Blocking（§8 R1）＋导入期错误行。
- **I-REQ-5（约束非空）**：`constrainedDof` 至少一真；容差两分量 >0 且有限。
- **I-REQ-6（区域非退化）**：Box size 三分量 >0 且有限；覆盖率 ∈ [0,1]；采样计数 ≥0（0 合法——零样本场景由评估判 DataInsufficient，KIN-04 R8/§6.2）。
- **I-REQ-7（顺序无环）**：`sequenceKey` 构成的顺序关系无环、无重复键（R7 Blocking）。
- **I-REQ-8（路径不作身份）**：导入溯源只存内容摘要＋行号；绝对路径仅入草稿 externalRefs 与诊断文案。
- **I-REQ-9（必验派生唯一）**：`mandatory ≡ (level==Must)`（P-EV-9 冻结规则，§6.3）——不存在独立的第三"必验"开关。
- **I-REQ-10（派生不回写）**：镜像/模板/阵列产物为普通独立条目；对源条目零引用、零回写（§7.2）。

**空集合**：任一集合可为空（空需求集合法可保存——阶段门控由 workflow 依就绪投影判定）；**循环派生**：生成溯源是一次性参数快照、非活性链接，按构造无环（§7.2）；**非法组合**示例：`constrainedDof` 全 false＋Fixed 规则、`appliesTo.scope=Stations` 且列表空、`level=Should` 且被下游声明为必验（不存在——必验仅由 level 派生）。

### 4.8 身份、版本与显示名

- ObjectId 一次分配跨修订稳定；ContentVersion 由 project 对 canonical 字节计算（SHA-256）。
- **`name` 是语义字段**（D-REQ-6）：与 modeling 的"显示名不触发重算"不同——需求名称进报告/覆盖清单/诊断定位/必验清单 `label`，改名＝内容变更＝新版本（下游按切片依赖正确失效）。这是业务语义差异，不是不一致。
- 需求档（RequirementProfile）：集合级**只读派生视图** `{必验工况清单(enabled∧Must), Must/Should 计数, 覆盖目标汇总, contentIdentity}`——DerivedReadOnly 来源，不入 canonical 编码（重算即得），供 evidence 组装与 UI 摘要。

## 5. 任务点、区域与姿态规则

### 5.1 任务点（TaskPoint；REQ-01/02；§4.3 字段表）

**关系图（任务点/区域/工况/计划）**：

```
                 ┌────────────────────────── req-set（根）──────────────────────────┐
                 │ pointSetRef      regionSetRef      conditionSetRef    planSetRef │
                 └──────┬─────────────────┬─────────────────┬──────────────┬────────┘
                        ▼                 ▼                 ▼              ▼
              req-point-set        req-region-set    req-condition-set  req-plan-set
              TaskPoint[]          WorkRegion[]      OperatingCondition[] SamplingPlan[]
              ├ pose(约束分量)      ├ Box 几何          ├ level/enabled     └ 逐区域计划
              ├ orientationRule    ├ 位置/姿态采样定义   ├ 环境障碍引用         （regionRef＋
              ├ tolerance          ├ 覆盖率目标         ├ 负载/事件/节拍        采样参数）
              ├ approach/work/     └ tcpRef            ├ demands              ▲
              │ retract 三段                            └ appliesTo─┐          │ planContentIdentity
              └ tcpRef/refFrame                                     │          │ （evidence §4.1.4 来源）
                    ▲                                               │          │
                    └──── tcpRef 指向 modeling tool-definition（跨聚合 ObjectId 浅引用）──┐
                                                                          ▼
                                              同一修订闭包（robot-design/tool-definition/scene-object/req-*）
```

- **进入/离开语义**：`approach`/`retract` 段沿 `ToolZ`（工具系 Z）或 `ReferenceZ`（参考系 Z）表达距离（m，>0）；作业段即任务点本身。方向的正负语义：approach 沿轴负向趋近、retract 沿轴正向离开——**语义约定归本单元登记**，几何解释（该轴在世界系的方向）由 runtime/下游统一计算（§5.3 隔离声明）。TRJ-01/02 消费三段构造接近/撤离路径（§8.2）。
- **必经状态与可选择构型**：必经性由 `level=Must`＋`enabled` 表达（必经任务点＝启用 Must 点；evidence §8.1 表 2 的"必经状态碰撞证明"消费该集合）；**可选择构型**不做硬性登记——构型硬过滤（残差/限位/碰撞）归 KIN-02，需求侧仅允许以 `note`/`processTag` 作注释性提示（D-REQ-7：不私设构型约束语义，避免与 KIN 的解空间/排序规则双头）。
- **删除引用保护**：删除被 `appliesTo`/`events.stationRef`/顺序键引用的任务点→命令边界拒绝＋定位诊断；被外部域（报告历史/优化候选）引用的旧版本对象字节永在（PA-2），悬空由下游就绪校验兜底（§8.1）。
- **任务点修改对 evidence 切片的影响**：点集对象 cv 变化→声明了点集依赖键（§8.4 角色键）的切片整体换身份→相应结果标 Superseded（保守）；名称改名同样如此（D-REQ-6）。
- **边界**：任务点数据本身不能证明工程可行/不可行——"点不可达"是 kinematics 评估结论（Feasible/Infeasible），本单元最多在就绪层报告"数据不完整/引用悬空"。

### 5.2 工作区域与采样计划定义（WorkRegion＋SamplingPlan；REQ-03；样本生成为 KIN-04）

- **区域几何**：R1 仅 Box（center＋size，refFrame 系）；边界即盒面。其他几何（圆柱/球）走需求变更（§14.3 P-REQ-7 不预留桩）。
- **位置采样定义**：`PositionSampling{method: Grid{counts[3]} | GridBySpacing{spacing[3]（m）} | Random{count}}`；`GridBySpacing` 在**计划构建时**规范化为 counts（`counts[i] = floor(size[i]/spacing[i])+1`，确定性、I-REQ-1）——计划内容身份以规范化后的计数计算，保证同义计划同身份（D-REQ-2）。
- **姿态采样定义**：`OrientationSampling{directionSamples: uint32（≥1）, rollSamples: uint32（≥1）, method token}`（供 KIN-04 姿态覆盖率的全局口径采样）。
- **覆盖率目标**：`minPositionCoverage`（REQ-03 必填）、`minOrientationCoverage`（可选）；KIN-04 按"分母＝计划样本总数"判定（evidence SamplingPlanRef.plannedXxxSamples 为分母来源）。
- **冻结语义（图）**：

```
需求侧（本单元，拥有"定义"）                    下游（拥有"生成与冻结"）
WorkRegion（几何＋采样定义）                     KIN-13 采样预算/种子（分析配置）
  └ SamplingPlan 条目                            │
     planContentIdentity ←── canonical(区域定义+采样参数)     ▲
                                        │                   │
                              evidence 快照组装（§4.1.4）─────┘
                              SamplingPlanRef{regionObjectId,
                                planContentIdentity,
                                plannedPositionSamples/plannedPoseSamples（分母）,
                                sampleSetIdentity = SHA-256(planContentIdentity ‖ 预算/种子)}
                              ──"样本集随快照冻结；复评不得增删更换样本"（KIN-04 R8）
```

- **计划变更与当前性**：区域几何或采样定义任一变更→plan 对象 cv 与 planContentIdentity 变化→`sampleSetIdentity` 变化→旧覆盖率结果 Superseded（历史保留）；**求解类配置（初值/迭代）变化不改样本基准**（evidence §4.1.4 D-04 双层身份，本单元无动作）。
- **零样本/不适用表达**：counts 乘积=0 的计划**合法存储**（不阻断编辑）；"零样本→DataInsufficient、不输出 0%/100%"的判定在评估/汇总层（KIN-04 R8/evidence I-5 分流）；纯关节任务无区域→区域条目不存在＝"不适用"（ERR-01 显式标记），本单元不伪造区域。
- **不以包络结果替代工况全覆盖**：区域覆盖率（KIN-04）与必验工况覆盖（EVI-02）是两条独立检查线；本单元只保证两者输入完备（§8 R5/R6），合并解释归 evidence。
- **样本生成算法归 kinematics**：本单元任何接口不得枚举/生成样本点（`ISamplingPlanBuilder` 只产出规范化计划定义与计划摘要）。

### 5.3 姿态与方向规则（OrientationRule 五规则；REQ-09）

| kind | 参数 | 语义（应用时解析，来源留痕） |
| --- | --- | --- |
| `Fixed` | `fixedRpy: Vector3（rad，refFrame 系）` | 固定姿态（欧拉序 Z-Y-X 显式登记；不设四元数编辑态——显示/输入层换算归 core/ui，D-REQ-3） |
| `AlignFrame` | `targetFrame: RequirementReference` | 对齐目标参考系姿态 |
| `AlignGeometryNormal` | `targetSceneObject: oid`＋`feature: FrameOrigin \| FramePlaneNormal`＋`invertNormal: bool` | 对齐几何法向（引用 MDL-15 场景对象） |
| `PointAtTarget` | `targetPoint: Vector3（m，refFrame 系）` | 工具轴指向目标点 |
| `ToolRollFree` | `rollRange: {min,max}（rad，默认 [−π,π]）` | 允许工具滚转（第五规则；主方向由伴随规则/固定主轴给出时组合表达——AT-23 五规则样例覆盖） |

- **隔离声明**：本单元不重定义坐标变换（T_ab 语义遵循 core §4.6，矩阵计算归 runtime/下游）；不重定义姿态求解（规则"解析"＝把规则参数解析为确定性参考姿态/方向并记录来源 `resolution`（kind＋目标 ObjectId＋解析值），**求解失败的"失败"指引用/参数错误**，不是 IK 失败——后者归 kinematics）。
- **数值容差来源**：容差比较一律附录 D 通用比较公式；本单元不新增姿态等价的专有容差。
- **等价姿态与内容身份**：同一物理姿态的不同参数表达（如 RPY 等效角）**不**做规范化归一（避免隐性改写用户输入，NFR-COR-03）；内容身份按参数字面计算——参数编辑即内容变更（保守失效，历史保留）。等价性判定（如"两规则解析到同一姿态"）是下游解析层职责。
- **非法旋转与零向量诊断**：非有限角、`PointAtTarget` 零向量目标、`AlignGeometryNormal` 缺 feature、`ToolRollFree` rollRange 逆序——构造/编辑边界拒绝＋`REQ-*` 定位诊断（§9.6）；解析失败（目标悬空等）→可定位诊断（REQ-09"解析失败给出可定位诊断"）。
- **显示姿态与计算姿态隔离**：deg 显示、四元数呈现均为 ui/core 投影（KIN-12 同源）；存储恒 SI rad＋Z-Y-X 欧拉参数字面。

---

## 6. 工况、必验范围与采样计划

### 6.1 工况（OperatingCondition；REQ-04；§4.5 字段表）

- **身份与内容身份**：objectId（稳定）＋条目字节随 `req-condition-set` 对象版本化；必验集合的冻结凭据 requiredCaseSetId 由 evidence 对 entries 规范编码计算（evidence §4.1.3 I-4）——本单元保证 entries 解析确定性（§6.3）。
- **引用**：环境障碍引用 `scene-object`（REQ-04"引用 MDL-15 场景对象"）；工具引用 `tool-definition`；均为跨聚合 ObjectId 浅引用（§8.1）。
- **负载与事件**：payloads（质量/质心/惯量，SourcedValue）＋events（夹取/释放/驻留，绑定任务点）；物性缺失→NotProvided（DataInsufficient 降级预告，判定归 dynamics DYN-06）。
- **适用任务**：`appliesTo{AllStations|Stations[oid…]|None}`——绑定方向唯一（工况侧声明，D-REQ-5）；`None`＋`enabled` 合法（声明"当前不适用"的工况，ERR-01 不适用标记的存储形态）。
- **必验顺序**：`verificationOrderHint` 为**建议性**元数据（供多工况验证的呈现排序）；EVI-02 覆盖检查是集合语义、与顺序无关（D-REQ-8，不私设"顺序覆盖规则"）。
- **工况重复/漏验/错误引用**：name 集合内唯一（I-REQ-3）；"漏验"的拦截在 evidence 覆盖矩阵（②级门禁）——本单元保证必验集合解析确定（R5）；错误引用（悬空场景/工具 oid）→就绪 R1 Blocking。
- **工况变更的下游失效**：条件集对象 cv 变化→requiredCaseSetId/依赖工况的切片失效；仅 `note` 变更也随对象字节保守失效（已知粗粒度，与 modeling P-MDL-2 同源取舍，R-REQ-2）。
- **多方案比较基准一致性**：EVI-02/RPT-04 由 `requiredCaseSetId`＋`sampleSetIdentity` 承载（evidence §6.5 基准检查）——本单元保证同一方案分支内集合稳定、跨方案差异如实反映（不自动同步）。
- **空工况集合**：合法存储；"无 enabled∧mandatory 工况"→正式通过被 evidence ④级保守拦截（P-EV-7——绝不输出正式通过；本单元就绪层给 Warning"无启用必验工况"，**不阻断编辑**）。
- **边界**：本单元不判断工况验证结果是否通过（判定归 evidence/评估域）。

### 6.2 必验范围冻结（P-EV-9 承接；本卡权威冻结条款）

**冻结 schema（requirements → evidence §4.1.3 回接登记义务，见 P-REQ-1）**：

```
OperatingCondition 条目：
  enabled   : bool                      —— 用户启用开关
  level     : Must | Should             —— 需求等级（上游词表，无第三值）
冻结解析规则（唯一、确定性）：
  mandatory := (level == Must)
RequiredCaseSet.entries[i] := { caseId: objectId, label: name,
                                enabled: enabled, mandatory: (level==Must) }
必验集合 := { c ∈ conditions | c.enabled ∧ (c.level==Must) }
```

- 该 schema 即 evidence `RequiredCaseSet` 解析的**权威来源**（evidence 现 minimal 承载 `{caseId, enabled, mandatory}` 字面消费，P-EV-9/O-14 登记"requirements 卡冻结后回接"）；WP-14-T01（本卡）落盘后由 evidence 侧增量同步确认（§12 交接）。
- 必验工况集合随快照冻结（requiredCaseSetId）；集合变更＝新修订＋新快照身份——"冻结"是快照属性，不是对象上的布尔锁（与旧 frozen 标志的本质差异，§2.5）。

### 6.3 需求侧"要求值"与策略"判定阈值"分离（REQ-04 × ARC-05/KIN-13）

| 概念 | 所有者 | 例子 | 进入 |
| --- | --- | --- | --- |
| 要求值（demand） | **requirements**（REQ-04 授权） | 最小关节裕量、无碰撞要求、目标节拍、覆盖率目标、位置/姿态容差 | 需求对象→切片→评估输入 |
| 判定阈值（policy） | policy（ARC-05） | 近限位比、条件数警告、行程上限 4π、安全间距 | EngineeringPolicySet→快照已解析策略 |
| 求解配置 | KIN-13/kinematics | 采样预算/种子、迭代上限、IK 容差默认 | AnalysisConfiguration→运行/缓存身份 |

本单元只存储/传递要求值；任何"多少算合格"的判定阈值不得在本单元出现第二份（NFR-MNT-07）；评估把两者在同一快照内对齐（CON-06）。

---

## 7. 镜像阵列、派生需求与导入映射

### 7.1 工艺模板（REQ-07；承接旧六类模板，§2.5）

- 模板词表（`TemplateKind`）：`BinPicking/MachineTending/Palletizing/Inspection/ToolChange/Handover`——requirements 所有权登记（§14.4），模板参数数值为设计默认（黄金数据集锁定，同 modeling T-MDL-1 模式）。
- 生成：`ITemplateArrayService::applyTemplate(kind, params)` 纯函数产出**编辑批次**（新条目数组＋生成溯源 `{generatorId, instanceId, linked=true, parameters}`）→编辑器应用→草稿→命令提交。模板产物为独立普通条目（可继续手改）；`linked` 条目支持"一键按参数重生成/解除关联"。
- 模板**不得绕过命令**：模板只是草稿生成辅助；应用仍走 `apply-requirement-set`。

### 7.2 镜像阵列与派生需求（REQ-11）

**派生关系图**：

```
源条目（TaskPoint，独立 ObjectId）                     派生条目（独立 ObjectId）
  pose/orientation/tolerance/segments    ──镜像变换──▶  同构字段（镜像后值）
                                                        generation={generatorId="mirror",
                                                          instanceId, linked=true,
                                                          parameters={平面系, 轴}}
  ◀────────────── 零引用/零回写（I-REQ-10）──────────────▶
  阵列：Linear/Rectangular/Circular/Polyline 参数（方向/数量/间距/角度/半径/折线）
        → 批量派生 N 条；重生成＝替换 linked∧未手改 条目；手改过的 linked 条目→冲突诊断
```

- **镜像几何**：过参考系镜像面（`refFrame`＋镜像轴法向）变换位置与姿态（反射变换为确定性纯函数，登记于本单元——这是**需求数据变换**，不是机器人坐标变换语义，§5.3 隔离声明不受影响）。
- **不可镜像规则处置**：`AlignGeometryNormal`/`AlignFrame` 引用的目标在镜像侧不存在→派生条目 `orientation` 保留规则但标记 `PendingManualResolution`＋Warning 诊断（**不静默猜、不静默降级为 Fixed**——用户显式处置后消除）；`Fixed`/`PointAtTarget` 正常镜像。
- **循环派生**：生成溯源是一次性参数快照，非活性链接——对已派生条目再次执行阵列/镜像＝以其为源生成**新**条目（溯源链参数留痕），按构造无环；不存在"源变→派生自动变"的活性传播（变更传播＝用户显式"重生成"）。
- **删除保护**：删除被 `linked` 重生成批次引用的源条目→允许（源删除不破坏派生条目独立性），但诊断提示"该源仍有 N 条 linked 派生"；删除 linked 派生条目无特殊限制。

### 7.3 CSV 坐标表导入（REQ-05；io/requirements 边界）

**导入时序图**：

```
向导域侧页        io（ICsvReader，唯一解析者）         requirements（IRequirementImporter，纯函数）      project
   │ open(P-1 一次性读取)──> RawTable＋CsvParseReport（行粒度部分成功/逐行错误定位）            │
   │<──────────────────────────│                        │                                     │
   │ 表头映射（字段字典自动识别＋手动改映射）／单位预览（每列单位声明，换算预览）                    │
   │────────────────────────────────────────────────> mapCsv(table, mapping, options)         │
   │                                                 → ImportOutcome{entries[], rowErrors[],   │
   │                                                   sourceDigest}（纯函数/确定性）            │
   │ 呈现：正确行预览＋错误行清单（行号/列名/原文/原因——AT-02）                                  │
   │ 用户确认 → 编辑器载入（草稿态）→ 落盘（DraftService）──应用──────────────────────────────> ①命令
   │ （取消/失败：无草稿、无修订——不产生半成品）                                                 │
```

- **字段字典（冻结表，CSV/JSON 同源——WP-14-T01 交付物）**：`id, name, process_tag, level(Must|Should), enabled(true|false), ref_frame, tcp, x,y,z（长度列）, roll,pitch,yaw（角度列）, pos_tol（长度）, ori_tol（角度）, approach_axis, approach_dist, retract_dist, min_joint_margin, note`——canonical 名＋别名表（中英头自动识别）；未映射列→忽略项报告（不静默丢弃提示）。
- **缺失/多余字段**：必填列缺失（id/name/位置三分量）→结构级错误（该文件不可导入）；可选列缺失→默认值＋报告；多余列→忽略项清单。
- **行级错误**：数值非法/单位无法换算/Frame 引用悬空/重复 id/重复 name/level 非法→逐行错误（行号＋列名＋原文），**正确行保留**（部分成功，AT-02）；零修改地保存在 RawTable 的原文用于展示（io 职责）。
- **单位**：长度列默认 m、角度列默认 rad，可按列声明 mm/deg——预览展示换算结果，落库前经 core 唯一换算归一 SI。
- **部分导入/失败回滚/取消**：导入只产出草稿——任何时刻取消/失败＝丢弃工作集，无修订、无半成品（PM-01 同口径）；已应用的导入修订不可"部分回滚"，只能再经命令修正（修订不可变）。
- **导入后对象身份与来源追溯**：新 ObjectId 在命令 prepare 分配；条目 `importProvenance={sourceDigest, recordNumber}`（源文件内容摘要，路径不入身份——I-REQ-8）。

### 7.4 JSON 导入/导出（REQ-12）

- 同一字段字典与安全规则（REQ-12）；JSON 结构＝需求对象的文档投影（含 schemaVersion）；解析经 io JSON 通道（JsonProfile 注册制，io.md §5），语义校验/映射同 §7.3。
- **导出为副本**：`IRequirementImporter::exportCopy(...)`——io AtomicFile 原子写出，不产生修订、不影响项目（失败时旧文件完好）；导出内容来自已应用修订或草稿（调用方声明，草稿导出带 `"draft": true` 标记防止误当正式数据）。

### 7.5 预览与正式分离（REQ-06/EVI-01）

- 导入预览/编辑器即时校验为 **Preview 语义**：只检查条目、不产生正式证据与结果对象（EVI-01 表 1）；预览输入不进修订闭包、无内容身份承诺（evidence §4.1.1"未应用草稿预览输入"行）。
- "任一**启用的 Must 条目**非法 → 正式就绪状态＝输入未完成（ReadinessSummary.valid=false，evidence ①级拦截，不运行正式评估）；有效 Should 未满足→警告"的**执行点**在评估组装/汇总层——本单元供数（§8）。

---

## 8. 需求就绪校验与下游依赖

### 8.1 就绪校验分层（`IRequirementReadinessChecker`，纯函数；R0→R9 短路优先）

| 层 | 检查 | 失败级别 |
| --- | --- | --- |
| R0 结构完整 | 根引用表解析、集合 token 一致、条目 id/name 非空唯一（I-REQ-2/3） | Blocking |
| R1 引用完整 | RequirementReference 存在性＋token 匹配（**浅校验**——仅查闭包 objectRefs 元数据，不解码 modeling 对象内容，见下） | Blocking |
| R2 坐标系有效 | refFrame 引用浅有效；World 缺省合法 | Blocking |
| R3 位姿合法 | 数值有限、容差>0、constrainedDof 非空、姿态规则参数合法/零向量/rollRange 有序（I-REQ-5） | Blocking |
| R4 工况绑定完整 | appliesTo 引用存在；events.stationRef 存在 | Blocking |
| R5 必验范围明确 | 必验集合解析确定（可空——空集 Warning"无启用必验工况"，正式拦截归 evidence P-EV-7） | Warning |
| R6 采样计划有效 | 计划-区域一一对应、Box 非退化（I-REQ-6）、计数/方向数非负（0 合法→零样本由评估判定） | Blocking（退化）/Warning（零计划） |
| R7 任务顺序无环 | sequenceKey 拓扑无环、无重复（I-REQ-7） | Blocking |
| R8 工具/模型引用存在 | tcpRef/环境引用浅有效 | Blocking（悬空）/Warning（PendingManualResolution 姿态规则） |
| R9 可生成 evidence 输入切片 | schemaVersion 受支持、五集合 canonical 可编码、必验解析确定（§6.2） | Blocking |

**级别**：Blocking（阻止应用＋定位）/Warning（可应用，登记——含"数据不足预告"如 payload 缺失）/NotApplicable（显式标记，如无区域任务）/可确认（本单元暂无可确认诊断——行程上限类策略校验属 modeling；若未来出现走 SA-15 同款流，不私设）。

**跨聚合浅引用边界（§8.1 专项声明）**：任务点 tcpRef/环境引用指向 modeling 对象。R-1 禁止业务互链→requirements **不得**解码 modeling 对象字节；就绪层仅校验"ObjectId 存在于闭包且 objectTypeToken 匹配"（RevisionView.objectRefs 元数据，project 提供）；语义级有效性（tcpKey 存在、frame 在模型中可解析、姿态规则目标可解析）由**评估时解析**（runtime/kinematics 经快照权威解析——REQ-09"规则在应用时解析"中的深度解析即此），解析失败→可定位诊断回指需求条目（objectId＋name）。

### 8.2 下游消费契约（evidence/kinematics/trajectory/optimization）

```
需求快照 → evidence 输入切片流程图：

已应用修订（req-* 五对象＋robot-design/tool/scene 同闭包）
   │ 快照组装（evidence/请求方，L5）
   ▼
AnalysisSnapshot{ objectClosure（含 req-* (oid,cv)）,
                  caseSet = RequiredCaseSet（§6.2 冻结解析）,
                  samplingPlans = [SamplingPlanRef（§5.2 来源）],
                  policyRef/nameMapRef/…（evidence/runtime 组装）,
                  snapshotId }
   │ 依赖分析（DependencyKey 角色键——本单元回授词表，§8.4）
   ▼
InputSlice{ …, 需求条目（点集/区域集/条件集/计划集）＋digest }
   ▼
kinematics（点/区域/姿态/工况消费，KIN-01~04）｜trajectory（顺序/三段，TRJ-01/02）
｜optimization（Must 工位/区域/必验工况=OPT-B 硬约束，OPT-03）
```

- **evidence 如何取得冻结需求快照**：快照组装方（L5/evidence）按修订闭包读 req-* 对象字节→经本单元 `IRequirementCodec` 解码→按 §6.2 派生 RequiredCaseSet、按 §5.2 提供 planContentIdentity——**冻结动作与凭据归 evidence**（SnapshotBuilder），本单元只保证解析确定。
- **kinematics 消费**：点（位姿/容差/约束分量/三段）、区域（几何/采样定义/覆盖率目标→KIN-04 分母/判定）、工况（碰撞/裕量要求值→硬条件解释）、Must/Should→硬过滤与警告分级。kinematics 不得绕过需求对象自建任务清单。
- **trajectory 消费**：sequenceKey 顺序＋approach/work/retract 段（TRJ-01 点到点/作业序列、TRJ-02 笛卡尔段方向/距离）。
- **optimization 消费**：需求基准＝冻结快照内的 Must 集（OPT-03 阶段 B 硬约束）；候选评估与基线同基准（EVI-02/RPT-04 一致性由快照身份保证）。
- **下游如何验证需求版本/内容身份/工况集合**：切片内 req-* 条目携带 (oid, cv, digest)；requiredCaseSetId/sampleSetIdentity 随快照——复评/报告可核对（CON-05/06）。
- **需求变更→切片失效**：§8.4 依赖声明；**不允许 requirements 直接调用下游业务算法**（R-1；下游发现评估器，需求只被消费）。

### 8.3 状态正交关系表（需求状态 × 就绪 × 执行 × 工程）

| 轴 | 取值 | 所有者 | 正交规则 |
| --- | --- | --- | --- |
| 需求编辑状态 | 编辑中／草稿／已应用修订 | ui/project | 后三轴非平凡取值仅存在于已应用修订 |
| 需求就绪 | NotReady／ReadyWithNotes／Ready（ReadinessSummary{valid, invalidMustItems[]}） | requirements | 只判输入完备合法；对编辑/草稿态即时预检，对修订在评估组装前复核 |
| 执行状态 | 未运行／Queued…Completed／Canceled/Failed/Interrupted（TASK 九态） | execution | 与工程判定轴正交（TASK-02⓪：outcome≠Completed→NotApplicable） |
| 工程状态 | 未评估／可行／工程不可行／数据不足 | evidence/评估域 | "输入未完成（①级）"时**不运行正式评估**；需求未就绪不产生任何工程判定 |

非法组合示例：草稿态＋已评估（草稿无快照身份）；NotReady＋可行（①级输入非法不运行评估——REQ-06/§8.1 汇总①）。

### 8.4 DependencyKey 角色键约定（本单元回授 evidence 的登记义务，evidence §13 B 行）

| 角色键（token） | 绑定对象 | 典型声明方 |
| --- | --- | --- |
| `req.points` | req-point-set | kinematics/trajectory/optimization |
| `req.regions` | req-region-set | kinematics（覆盖率） |
| `req.conditions` | req-condition-set | 全部评估域（必验覆盖） |
| `req.sampling-plans` | req-plan-set | kinematics（样本基准） |

细粒度说明：四键独立→改工况不失效纯几何覆盖率切片的字节依据（但 requiredCaseSetId 变化会拦截正式通过——覆盖矩阵分母变了，正确行为）；模型侧依赖（`model.robot-design` 等）由 kinematics 并行声明——TCP 变更失效运动学及下游（AT-05①）经 `tool-definition` 对象由建模/运动学两侧声明完成，本单元不代声明。

---

## 9. 公共接口与跨单元协作

### 9.1 命令处理器族（project `ICommandHandler`；token 无点语法，P-PR-9 同源保守侧）

| commandType | 写入对象 | 断言/确认 | inverse |
| --- | --- | --- | --- |
| `apply-requirement-set` | req-set＋四个集合对象（全量或增量） | 就绪 R0~R9 现场重估（防基线漂移） | 快照式逆命令（受影响对象前版字节） |
| `apply-requirement-import` | 同上（导入批次专用摘要） | 同上＋导入溯源完整性 | 同上 |

- prepare 管线同 modeling §9.3 模式：decode→基线重建→就绪断言（Blocking 拒绝＋逐项定位诊断）→CommandPlan{objectWrites, inverse, summary}。本单元**无 ConfirmableFinding 产出**（无策略校验类放行场景）——`confirmableFindings` 恒空，如未来出现走 SA-15 通用流。
- 撤销/重做/草稿/Stale 语义全部同 modeling §9.3（project 机制，不重复设计）；草稿模块 token＝`requirements`。

### 9.2 接口契约总则

同 modeling §9.4 前提（§3.4）：纯函数服务非异常出口（结果对象＋诊断列表）、调用方错误 fail-fast 语义返回；编辑器仅 UI 线程；处理器仅命令线程；一切数值 SI。

### 9.3 `IRequirementEditor`（编辑器；REQ-01/06/11）

```cpp
/// 需求编辑器：基线闭包＋未应用编辑差值的演算视图。非线程安全（仅 UI 线程）。
class IRequirementEditor {
public:
    virtual ~IRequirementEditor() = default;
    /// @brief 载入基线（修订闭包解码）并返回可变工作集句柄。
    /// @pre  closure 含 req-set（token 路由）；解码失败→RequirementError(SchemaVersionUnsupported)。
    virtual LoadOutcome loadBaseline(const ObjectClosureView& closure) = 0;
    /// @brief 只读工作集（含 RequirementProfile 派生档）。
    virtual const RequirementWorkingSet& workingSet() const noexcept = 0;
    /// @brief 应用一次编辑（字段级/条目增删/批量变体/模板镜像阵列批次）。
    /// @post 接受：工作集本地版本 +1（局部撤销入栈）＋就绪增量重估；拒绝：字节不变＋逐项诊断。
    virtual EditOutcome applyEdit(const RequirementEdit& edit) = 0;
    virtual bool undoLocal() noexcept = 0;
    virtual bool redoLocal() noexcept = 0;
    /// @brief 自基线以来的人读中文变更摘要（命令提交时并入命令摘要）。
    virtual std::string buildChangeSummary() const = 0;
    virtual RequirementDraftStatus draftStatus() const = 0;  // {dirty, baseRevisionId, edits}
};
```

### 9.4 领域服务接口（ITaskPointService / IWorkRegionService / IOperatingConditionService）

```cpp
/// 任务点服务：纯函数；对工作集中 TaskPoint 集合做查询/校验/批量构造。
class ITaskPointService {
public:
    /// @brief 由参数构造一条合法任务点（含位姿/容差/三段/来源标记；分配临时句柄）。
    /// @错误 DuplicateName|IllegalTolerance|ZeroVectorTarget|AllDofFree
    virtual CreateOutcome createPoint(const TaskPointSpec& spec,
                                      std::vector<core::DiagnosticRecord>& diags) const = 0;
    /// @brief 顺序键拓扑校验（无环/重复——R7 的独立入口，供编辑器增量调用）。
    virtual SequenceCheckResult checkSequence(const std::vector<TaskPoint>& points) const = 0;
    /// @brief 镜像/阵列/模板前的源条目可派生性预检（不可镜像规则清单）。
    virtual DerivePrecheck precheckDerivation(const std::vector<ObjectId>& sources) const = 0;
};
/// IWorkRegionService：区域构造/采样定义规范化（GridBySpacing→counts，D-REQ-2）/覆盖率目标校验。
class IWorkRegionService {
public:
    virtual CreateOutcome createRegion(const WorkRegionSpec& spec,
                                       std::vector<core::DiagnosticRecord>& diags) const = 0;
    /// @brief 规范化采样定义（确定性；规范化后计数即 planContentIdentity 的输入）。
    virtual NormalizedSampling normalizeSampling(const PositionSampling& raw) const = 0;
};
/// IOperatingConditionService：工况构造/必验派生解析（I-REQ-9）/适用范围校验。
class IOperatingConditionService {
public:
    virtual CreateOutcome createCondition(const OperatingConditionSpec& spec,
                                          std::vector<core::DiagnosticRecord>& diags) const = 0;
    /// @brief 必验集合解析（enabled∧Must；P-EV-9 冻结规则的唯一实现点）。
    virtual RequiredCaseResolution resolveRequiredCases(
        const std::vector<OperatingCondition>& conditions) const = 0;
};
```

### 9.5 其余四个必需接口＋支撑接口

```cpp
/// 需求导入器：CSV/JSON 字段映射/单位预览/逐行错误/副本导出（纯函数；io 已解析产物为输入）。
class IRequirementImporter {
public:
    /// @pre  table 为 io ICsvReader 产物（RawTable＋CsvParseReport）；不自行读文件。
    /// @post 正确行→条目草稿；错误行→逐行诊断（行号/列名/原文）；同输入同输出。
    virtual ImportOutcome mapCsv(const io::RawTable& table, const FieldMapping& mapping,
                                 const ImportUnitOptions& units,
                                 std::vector<core::DiagnosticRecord>& diags) const = 0;
    virtual ImportOutcome mapJson(const std::vector<std::uint8_t>& jsonBytes,
                                  std::vector<core::DiagnosticRecord>& diags) const = 0;
    /// @brief 副本导出（REQ-12；io AtomicFile 原子写出；不产生修订、失败旧文件完好）。
    virtual ExportOutcome exportCopy(const RequirementWorkingSet& ws, ExportFormat format,
                                    const io::ExportTarget& target) const = 0;
    /// @brief 字段字典（冻结表 §7.3；供向导自动识别表头与单位预览）。
    virtual const FieldDictionary& fieldDictionary() const noexcept = 0;
};

/// 采样计划构建器：只构建/规范化/校验"计划定义"，绝不生成样本（KIN-04 生成归 kinematics）。
class ISamplingPlanBuilder {
public:
    /// @post 产出 SamplingPlan 条目（规范化计数）；planContentIdentity 的 canonical 输入即此。
    /// @错误 DegenerateRegion|NegativeCount|RegionNotBox
    virtual PlanOutcome buildPlan(const WorkRegion& region, const SamplingPlanSpec& spec,
                                  std::vector<core::DiagnosticRecord>& diags) const = 0;
    /// @brief 计划摘要（供快照组装方填充 SamplingPlanRef.plannedXxxSamples 的来源核对）。
    virtual PlanDigest digest(const SamplingPlan& plan) const = 0;
};

/// 就绪校验器：R0~R9（§8.1）；产出报告＋ReadinessSummary 投影（evidence ①级数据源）。
class IRequirementReadinessChecker {
public:
    virtual RequirementReadinessReport check(const RequirementWorkingSet& ws,
                                             const CheckContext& ctx) const = 0;
    /// @brief 就绪→evidence ①级输入门禁数据（任一启用 Must 条目非法→valid=false＋全量清单）。
    virtual evidence::ReadinessSummary readinessSummary(
        const RequirementReadinessReport& report) const = 0;
};

/// 命令处理器基类：封装 §9.1 prepare 管线公共段（L5 装配注册；仅命令线程调用）。
class IRequirementCommandHandler : public project::ICommandHandler {
public:
    std::uint32_t currentPayloadVersion() const final;   // schema 演进点；旧版本拒绝＋升级指引
    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& envelope,
                                    const project::RevisionView& baseSnapshot,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>& diags) final;
protected:
    virtual DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                        const CommandPayload& payload,
                                        const RequirementWorkingSet& baseline,
                                        project::CommandPlan& out) = 0;
};

/// 支撑：模板/镜像/阵列服务（§7.1/§7.2）与 canonical 编解码（core §6.3 分工登记）。
/// 【WP-14-T07 落位修订（§14.6 v0.7 等价调整登记）】：①applyMirror 源入参
///   由 ObjectId 列表修订为源条目值列表 `std::vector<TaskPoint>`——纯函数
///   服务无状态，仅凭 ObjectId 无法解析出源条目值，源值必须随入参给出；
/// ②ArrayParams 内嵌 `sources`（源条目值数组）同理；③重生成"手改"判定
///   ＝参数快照确定性重算＋逐名称配对（除 ObjectId/溯源外任一字段差异
///   即手改）；④"解除关联"落位为同头自由函数 `unlinkGenerator(entries,
///   instanceId)`（linked→false，其余字节保持）；⑤生成器标识词表：
///   `template:<小写连字符 kind token>`/`mirror`/`array:<…>`。
class ITemplateArrayService {
public:
    virtual EditBatch applyTemplate(TemplateKind kind, const TemplateParams& params) const = 0;
    virtual EditBatch applyMirror(const std::vector<TaskPoint>& sources,
                                  const MirrorPlaneSpec& plane) const = 0;
    virtual EditBatch applyArray(ArrayKind kind, const ArrayParams& params) const = 0;
    /// @brief 重生成：替换 linked∧未手改 条目；手改条目→冲突诊断清单（不静默覆盖）。
    ///        （替换面＝newPoints 仅含未手改候选＋replaceNames 同名清单——
    ///        手改保留条目同名候选不进批次，避免编辑器集合级名称核对整批拒绝。）
    virtual RegenerateOutcome regenerate(const RequirementWorkingSet& ws,
                                         const std::string& generatorInstanceId) const = 0;
};
std::vector<TaskPoint> unlinkGenerator(const std::vector<TaskPoint>& entries,
                                       const std::string& generatorInstanceId);
class IRequirementCodec {
public:
    virtual Expected<std::vector<std::uint8_t>, RequirementError> encode(
        const RequirementObjectVariant& obj, std::uint32_t formatVersion) const = 0;
    virtual Expected<RequirementObjectVariant, RequirementError> decode(
        const std::vector<std::uint8_t>& bytes, std::uint32_t formatVersion) const = 0;
};
```

每接口的线程/确定性/所有权/副作用均按 §9.2 总则与 modeling §9.4 逐条同构执行（纯函数服务：可重入、无副作用、值语义产出；编辑器：UI 线程、内存副作用；处理器：命令线程、仅产出 CommandPlan）。

### 9.6 requirements 稳定诊断码登记表（`REQ-` 前缀；ownerUnit=`requirements`，装配期注册，不预建无消费者条目）

| 码 | 级别 | 语义 | 任务 |
| --- | --- | --- | --- |
| `REQ-READY-REF-MISSING` | error | 引用悬空/token 不匹配（R1/R8；R0 根引用表/R2 槽-种类/R4 绑定悬空按"引用悬空"语义就近承载同码，层标注区分——Readiness.hpp 层-码映射） | T05〔已落位〕 |
| `REQ-READY-SEQ-CYCLE` | error | 任务顺序成环/重复键（R7；悬空前驱按层-码对齐以本码承载、cause 区分） | T05〔已落位〕 |
| `REQ-READY-POSE-ILLEGAL` | error | 位姿/容差/约束分量非法（R3） | T05〔已落位〕 |
| `REQ-READY-NO-REQUIRED-CASE` | warning | 无启用必验工况（R5；正式拦截归 evidence） | T05〔已落位〕 |
| `REQ-READY-PLAN-DEGENERATE` | error | 区域退化/计划-区域失配（R6） | T05〔已落位〕 |
| `REQ-READY-INPUT-INCOMPLETE` | error | 启用 Must 条目非法汇总（ReadinessSummary.valid=false 投影面；命令 prepare 拒绝时随逐项诊断附一条汇总） | T05〔已落位〕 |
| `REQ-READY-PLAN-MISSING` | warning | 区域已定义而计划集为空（R6"Warning（零计划）"分支——原六码无 warning 级承载面，WP-14-T05 实现期表尾增登，modeling WP-13-T10 实现期增登先例；正式判定归评估/KIN-04 零样本） | T05〔已落位〕 |
| `REQ-IMPORT-ROW-ERROR` | error | CSV/JSON 行级错误（行号/列名/原文） | T04 |
| `REQ-IMPORT-DUPLICATE-ID` | error | 导入重复 id/name | T04 |
| `REQ-IMPORT-UNIT-ILLEGAL` | error | 单位声明无法换算/列缺失 | T04 |
| `REQ-IMPORT-FRAME-UNKNOWN` | warning | Frame 引用浅悬空（可保留为待解析） | T04 |
| `REQ-DERIVE-MIRROR-PENDING` | warning | 姿态规则不可镜像→PendingManualResolution | T07〔已落位——WP-14-T07；分类 ResourceMissing，paramSchema `["entry","rule","target"]`〕 |
| `REQ-DERIVE-REGENERATE-CONFLICT` | warning | linked 条目已手改，重生成冲突 | T07〔已落位——WP-14-T07；分类 ResourceMissing，paramSchema `["entry","instance"]`〕 |
| `REQ-DERIVE-SOURCE-REMOVED` | warning | 被 linked 批次引用的源条目已删除（允许删除＋提示"仍有 N 条 linked 派生"——§7.2 删除保护行的承载面） | T07 表尾增登〔WP-14-T07 实现期增登先例——PLAN-MISSING 同款；分类 ResourceMissing，paramSchema `["entry","linked-derived"]`〕 |
| `REQ-SCHEMA-UNSUPPORTED` | error | 对象 schema 主版本超出支持 | T02/T03 |
| `REQ-CAPTURE-STATE-STALE` | warning | TCP 捕获时会话状态与当前快照不一致（REQ-10 确认流） | T06〔已落位〕 |

### 9.7 需求条目与对象引用关系图

```
修订闭包 objectRefs
 ├─ (oid,cv) req-set ── refs: pointSet/regionSet/conditionSet/planSet（仅 oid）
 ├─ (oid,cv) req-point-set     entries[TaskPoint]{子oid 稳定}
 ├─ (oid,cv) req-region-set    entries[WorkRegion]
 ├─ (oid,cv) req-condition-set entries[OperatingCondition] ──appliesTo──▶ TaskPoint 子oid
 ├─ (oid,cv) req-plan-set      entries[SamplingPlan] ──regionRef──▶ WorkRegion 子oid
 ├─ (oid,cv) tool-definition（modeling）◀──tcpRef 浅引用（token 校验；语义解析归评估）
 ├─ (oid,cv) scene-object（modeling） ◀──environmentRefs/AlignGeometryNormal 浅引用
 └─ (oid,cv) robot-design（modeling）  ◀──（同闭包隐式绑定，取代旧"模型绑定指纹"，§2.5）
 身份规则同 modeling §9.6：路径≠身份；"删除"=引用/条目移除；字节永久保留（PA-2）
```

### 9.8 插件界面设计与界面逻辑（`sdurws_ird_requirements_plugin`；REQ-06/08/10/11、UX-05/06）

**定位与分工**：同 modeling §9.8 前提——工作台壳/命令注册/快捷键/确认对话桥/三维视图契约归 ui；本节只设计 requirements 域面板。旧三页 UI 的功能映射见 §2.5。

**面板组成（挂接 UX-09 五区布局）**：

| 面板区 | 内容 | 消费契约 |
| --- | --- | --- |
| 需求对象树（左栏，需求节点） | 需求集→工位/区域/工况/计划四分组→条目；节点锚＝ObjectId，显示 name（工程用语） | ②查询端口＋ui SelectionModel |
| 工位面板（右栏投影） | 列表＋检查器：位姿分量约束/容差/三段/姿态规则（五规则联动表单：按 kind 显隐参数——承接旧 templateParameterVisibilityMask 思路）/等级与启用/来源徽标（手工/捕获/镜像/模板/导入） | §9.4 服务＋ui FormEditCommon（批量粘贴/单位同显/就地错误——UX-05） |
| 区域面板 | 区域表＋采样定义编辑（计数/间距双模式）＋覆盖率目标＋**区域轮廓与采样格三维预览**（仅几何预览；结果着色归 KIN-07） | §9.4/ISamplingPlanBuilder＋ui View3D 契约 |
| 工况面板 | 工况表＋负载/事件/节拍/适用范围编辑＋必验清单预览（RequirementProfile 投影） | §6.1/§9.4 |
| 校验面板 | R0~R9 分层结果（Blocking/Warning 计数＋逐项定位跳转）；预览/正式语义说明（REQ-06） | §8＋`IPluginUiModule::readonlyProjections()`＋IDiagnosticSink |

**界面逻辑（十二条数据流）**：

| # | 交互 | 数据流 | 契约锚 |
| --- | --- | --- | --- |
| L-R1 | 选中联动 | 树/表选中→SelectionModel→投影刷新；诊断定位反向跳转 | ui §4.2；UX-06 |
| L-R2 | 字段编辑 | 控件提交→`applyEdit`→接受刷新＋脏标记；拒绝就地原因保留原值 | §9.3；UX-03/05/07 |
| L-R3 | 应用提交 | `draft.apply`→`buildDraftCommand("requirements")`→①端口；就绪 Blocking 在 prepare 现场重估 | §9.1；PM-04 |
| L-R4 | 撤销/重做两级 | 项目级 UndoRedoService／草稿级编辑器 undoLocal（REQ-11 需求集撤销＝批量整体回滚——单条 applyEdit 批次一次入栈） | §9.3；REQ-11 |
| L-R5 | Stale 对话 | Rejected(stale-revision)→ui 冲突对话（建模同款）；草稿 apply-retained | PM-04 |
| L-R6 | 几何特征拾取 | 面板"拾取"→ui View3D 进入拾取态→拾取结果（Frame/几何 ObjectId）→**写回前确认**（REQ-08）→入草稿；未应用不失效 | REQ-08/10；AT-23 |
| L-R7 | TCP 捕获 | 面板"捕获当前 TCP"→ui 会话态/runtime 只读视图取当前 TCP 位姿→确认→构造 Fixed 任务点入草稿（来源 methodTag=`captured-tcp`）；会话过期→`REQ-CAPTURE-STATE-STALE` | REQ-10；KIN-06（会话语义） |
| L-R8 | 姿态规则编辑 | kind 切换→参数表联动显隐→应用时解析（目标引用存在性即时浅校验；深度解析留评估）→解析失败可定位 | §5.3；REQ-09 |
| L-R9 | 镜像/阵列/模板 | 面板发起→precheckDerivation→参数表单→EditBatch→applyEdit（一次批次一次入栈）→派生条目带溯源徽标 | §7.1/§7.2；REQ-11 |
| L-R10 | 导入向导域侧页 | 文件选择（io 预检）→表头映射＋单位预览→逐行错误清单（正确行可折叠预览）→确认→草稿→draft.apply | §7.3；AT-02 |
| L-R11 | JSON 副本导出 | 面板"导出副本"→exportCopy（AtomicFile）→成功提示路径；失败旧文件完好 | §7.4；REQ-12 |
| L-R12 | 只读模式 | writable=false→编辑禁用、域命令 readOnlyAllowed=false；浏览/预览/副本导出可用 | ui §7.6；PM-07 |

**域命令登记清单**（`requirements.*`，ui CommandId 词表；快捷键经 HotkeyBindingTable）：

| CommandId | 语义 | readOnlyAllowed |
| --- | --- | --- |
| `requirements.import-csv` / `requirements.import-json` | 导入向导 | false |
| `requirements.export-copy` | JSON/CSV 副本导出 | true |
| `requirements.capture-tcp` | TCP 捕获（会话读＋确认写草稿） | true |
| `requirements.pick-feature` | 几何特征拾取态 | true |
| `requirements.mirror-stations` / `requirements.create-array` / `requirements.apply-template` | 镜像/阵列/模板 | false |
| `requirements.regenerate-linked` | 按模板重生成 | false |

**线程与刷新约束**：同 modeling §9.7.4（编辑器仅 UI 线程；命令异步；确认/拾取经 ui 桥 marshal；事件驱动刷新禁轮询；面板不缓存权威数据——防第二真值）。

## 10. 验证方案及故障注入矩阵

### 10.1 总则

四组：**V-P 任务点/区域/姿态**、**V-C 工况/必验/计划**、**V-I 导入/派生**、**V-S 状态/失效/契约**。**未执行的测试不得标注通过**（AGENTS §4.2）；用例随 WP-14-T04~T09 执行留痕（gtest XML＋`ird-test-report.json`＋构建日志）。GUI 用例仅登记流程（AGENTS Windows 规程），本次不启动。黄金数据集（WP-14-T09，DTB 指定四套）：`req-transport-task/`（搬运任务）、`req-partial-pose/`（部分位姿约束）、`req-region-sampling/`（区域与采样定义）、`req-payload-events/`（负载事件）——`ird-golden-manifest/1` 契约＋容差档案 `tolerance/req-*`（锚附录 D 通用比较公式）＋零值/近零/正负抵消边角样例。

### 10.2 故障注入矩阵

| # | 组 | 场景 | 依据 | 前置 | 操作 | 预期结果 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| V-01 | P | 任务点创建/修改/删除与引用保护 | REQ-01、AT-02、I-REQ-3/4 | 已应用需求集 | 增点→改名→删除被 appliesTo/events 引用点 | 增改产生草稿→应用恰一修订；删除被引用点拒绝＋定位诊断 | 修订计数；REQ-READY-REF-MISSING subject=oid |
| V-02 | P | 区域边界与零样本 | REQ-03、KIN-04 R8 | 区域集 | size 含 0/负→拒绝；counts=0 计划→存储合法 | 退化区域 Blocking（I-REQ-6）；零计划 Warning；零样本判定归评估（本单元不判） | 就绪报告分级；planContentIdentity=0 计划可编码 |
| V-03 | P | 姿态规则与非法旋转 | REQ-09、AT-23 | 工位面板 | 五规则各建样例；零向量目标/rollRange 逆序/缺 feature | 解析成功记录来源；失败可定位（构造边界拒绝＋REQ-READY-POSE-ILLEGAL） | resolution 留痕；诊断三要素 |
| V-04 | C | 工况重复/漏验/错误引用 | REQ-04、EVI-02 | 工况集 | 重复 name→拒；悬空场景引用→Blocking；漏一个必验工况评估（联合观测） | 本单元：重复/悬空拦截；漏验拦截在 evidence 覆盖矩阵（EV-COV 联合） | 对象级诊断；requiredCaseSetId 变化 |
| V-05 | C | 必验工况冻结 | P-EV-9、EVI-02、§6.2 | 工况集 | level=Must＋enabled 组合变更→应用→快照组装 | mandatory≡(level==Must) 唯一解析；RequiredCaseSet 与冻结规则逐条一致；空必验集→Warning＋evidence ④级拦截（联合） | 冻结解析表；evidence EV-CASET 对端断言 |
| V-06 | I | 镜像阵列 | REQ-11、AT-24 | 源工位 | 过参考系镜像面镜像；四类阵列生成 | 镜像坐标/姿态正确（反射变换黄金断言）；四类阵列数量/间距/角度正确；派生条目独立 ObjectId＋溯源完整 | EditBatch 黄金比对；generation.parameters |
| V-07 | I | 派生循环与不可镜像规则 | REQ-11、§7.2 | 派生条目 | 对派生条目再派生；AlignGeometryNormal 镜像 | 无环（溯源快照语义）；不可镜像→PendingManualResolution＋REQ-DERIVE-MIRROR-PENDING（不静默降级） | 溯源链；Warning 清单 |
| V-08 | I | CSV 导入黄金与部分成功 | REQ-05、AT-02 | req-transport 黄金 CSV | 导入含错行文件 | 正确行保留、错误行定位到列与原文；字典自动识别表头；单位预览正确 | 行级诊断逐条；条目数=正确行数 |
| V-09 | I | 单位与 Frame 错误 | REQ-05、NFR-COR-03 | mm/deg 声明列＋悬空 Frame | 导入 | 换算归一 SI 正确；Frame 悬空→REQ-IMPORT-FRAME-UNKNOWN（可保留待解析） | SI 值断言；Warning 清单 |
| V-10 | I | JSON 导入/导出副本 | REQ-12、AT-24 | req-partial-pose 黄金 | JSON 导入→导出副本→重导入 | roundtrip 逐字段一致；导出失败旧文件完好、项目状态不变 | 字节级 roundtrip；AtomicFile 回滚 |
| V-11 | S | 空需求集合 | §4.7、§8 R5 | 空集项目 | 保存→应用→就绪 | 空集合法保存/应用；R5 Warning"无启用必验工况"；正式评估被 evidence 拦截（联合） | 修订可产生；ReadinessSummary 投影 |
| V-12 | S | 需求未就绪 ≠ 工程不可行 | REQ-06、§8.3 | 含 Blocking 项需求 | 尝试应用/评估 | 应用阻止＋定位；不产生任何工程判定结论；①级输入未完成不运行正式评估（联合） | prepare 拒绝；无 EngineeringStatus 输出面 |
| V-13 | S | 草稿保存/恢复/应用 | PM-04、AT-21 | 编辑中 | 关闭重开（恢复）→应用 | 草稿 60s 落盘/打开恢复；应用恰一修订 | drafts/requirement.draft.json；修订计数 |
| V-14 | S | StaleRevisionRejected | PM-04、AT-29 | 草稿 base≠tip | 应用旧草稿 | Rejected(stale-revision)；草稿 apply-retained；重编辑后成功 | StoreError 码；草稿仍在 |
| V-15 | S | 下游快照绑定 | CON-05/06、AT-10 | 已应用需求集 | 评估组装（联合）→项目切换→迟到结果 | 快照 caseSet/samplingPlans/req 切片与冻结凭据一致；迟到结果不进新项目（TASK-03） | requiredCaseSetId；五元组登记表（联合观测） |
| V-16 | S | 需求变化导致正确切片失效 | CON-05、AT-05 | 点集/条件集/计划集 | 分别改任务点、改工况 note、改采样计数 | 三者分别换各自对象 cv→声明对应角色键的切片失效；未声明域不失效（如改工况不失效纯几何切片字节依据，但 requiredCaseSetId 变化拦截正式通过） | 角色键声明核对；对象级 cv 变化范围 |
| V-17 | S | 显示单位变化不触发无关失效 | KIN-12、AT-27 | 任意需求 | 切换显示单位 m↔mm/deg↔rad | 零修订、零失效；需求对象字节不变 | 修订计数；对象 cv 不变 |
| V-18 | S | 同一需求内容不同项目修订的身份隔离 | CON-01、ARC-04 | 两个项目各含同内容需求集 | 分别应用 | ObjectId 各自独立（不跨项目复用）；ContentVersion 可相同（同内容）；引用互不串扰 | 双项目闭包比对 |
| V-19 | C | 与 kinematics/evidence 契约 | §8.2/§8.4、AT-03 联合 | 黄金需求集 | 评估组装→点/区域/工况消费 | 采样计划→plannedXxxSamples 分母一致；角色键声明齐备；必验集与冻结解析一致 | SamplingPlanRef/RequiredCaseSet 对端断言（联合） |
| V-20 | S | AT-04 预览无修订 | EVI-01、AT-04 | 编辑/导入预览 | 预览/即时校验/区域三维预览 | 零修订、零正式证据；预览输入无内容身份承诺 | 修订计数；evidence 无对象（联合） |
| V-21 | S | AT-30 联合观测（回填影响面） | SEL-10、AT-30 | 需求集＋工具引用 | selection 回填工具对象（阶段 C 联合） | 需求侧 tcpRef 浅引用不变；下游复算提示由工具对象 cv 变化经建模/运动学依赖完成（本单元零动作） | req 对象 cv 不变；联合观测 |
| V-22 | G | GUI 主流程（设计登记） | UX-05/06、§9.8 | Windows GUI 环境 | 工位表批量粘贴→姿态规则切换→拾取确认→导入向导 | 就地错误保留原值；写回前确认；五规则表单联动；确认对话三要素 | 面板截图＋诊断呈现（本次不启动） |

**AT 承接声明**：AT-02（V-08/09）、AT-04（V-20）、AT-05（V-16 联合）、AT-09（V-19/需求基准面联合）、AT-10（V-15 联合）、AT-23（V-03/拾取捕获 L-R6/L-R7）、AT-24（V-06/07/10/需求集撤销 L-R4）、AT-27（V-17）、AT-30（V-21 联合）。"联合观测"行本卡只负责 requirements 侧输入/输出面——不把未执行测试写为通过。

---

## 11. 阶段 B 实现任务拆分

DTB WP-14 任务包（v0.16 §2.15）为权威拆分；本卡视角的内部顺序：

| 任务 | 产出（本卡落点） | 前置（DTB） | 内部顺序 |
| --- | --- | --- | --- |
| WP-14-T01 | 本卡（含 P-EV-9 schema 冻结 §6.2、CSV/JSON 字段字典 §7.3、五姿态规则 §5.3、镜像阵列 §7.2、就绪校验 §8、任务拆分 §11） | WP-11/WP-05/WP-04/WP-10 各卡 | ①（已完成——本提交） |
| WP-14-T02 | `requirements/CMakeLists.txt`（STATIC 零 Qt＋测试目标＋守卫＋白名单六边，§3.2）；ObjectTypes/Errors/DiagCodes | T01、WP-03-T01 | ② |
| WP-14-T03 | RequirementTypes/Codec/Editor/Services/Sampling＋不变量 UT（I-REQ-1~10；canonical 编码登记供 evidence 切片） | T02 | ③（核心阻塞） |
| WP-14-T04 | Import.hpp/.cpp（CSV/JSON 映射＋字段字典＋单位预览＋副本导出）＋UT（AT-02/24） | T03、WP-11-T04/T05 | ④a |
| WP-14-T05 | Readiness/CommandHandlers＋UT（REQ-06 预览/正式分离；ReadinessSummary 数据源；Must/Should 分级） | T03、WP-05-T06 | ④b |
| WP-14-T06 | 姿态规则解析＋三维拾取/TCP 捕获域侧＋UT（AT-23） | T03、WP-10-T05 | ④c |
| WP-14-T07 | TemplateArray.hpp/.cpp（六模板/镜像/四阵列/重生成/需求集撤销）＋UT（AT-24） | T03 | ④d |
| WP-14-T08 | `sdurws_ird_requirements_plugin`（四面板按 §9.8；ui 接入） | T03~T07、WP-10-T08 | ⑤ |
| WP-14-T09 | 契约测试＋四套黄金数据集（§10.1）＋容差档案〔已落位——WP-14-T09：四套 req-* 数据集＋req-golden 档案＋test/GoldenDatasetsContractTest.cpp＋contract_test/PeerFaceContractTest.cpp（TK-T09 FaultInterceptor 对端面＋T-1/T-2 红线）〕 | T03~T08、WP-02 | ⑥（收口） |

DoD 沿 DTB §5.2：双模式构建零错误、`ird_gates` 零命中、验收用例通过并留痕、文档同步零偏差（含 P-EV-9 回接 evidence §4.1.3 的跨卡同步项——WP-14-T01 落盘后移交 evidence 所有者，§14.3 P-REQ-1）。

---

## 12. 后续阶段承接与接口交接清单

| 对端 | requirements 收到 | requirements 交出 |
| --- | --- | --- |
| evidence | 快照/切片契约（SnapshotBuilder/RequiredCaseSet/SamplingPlanRef/ReadinessSummary 类型）、角色键登记协议 | 必验 schema 冻结规则（§6.2，回接 §4.1.3）；`req.*` 角色键四键词表（§8.4：req.points/req.regions/req.conditions/req.sampling-plans）；canonical 编码登记〔**已落位——WP-14-T03**：`IRequirementCodec`（Codec.hpp）五对象变体＋`kCurrentRequirementFormatVersion{1,0}`，family magic "IRDREQO"，集合条目按 ObjectId 字典序（I-REQ-1），RequirementProfile 派生档不入编码（§4.8）〕；ReadinessSummary 数据源〔**已落位——WP-14-T05**：`IRequirementReadinessChecker`（Readiness.hpp）R0~R9 分层校验＋`readinessSummary` 两字段投影（evidence §6.4① 值类型直投；O-39 现场重算纯函数面——修订内不持久化就绪结论）〕；planContentIdentity canonical 输入〔**已落位——WP-14-T03**：`ISamplingPlanBuilder`（Sampling.hpp）产出规范化 Grid 计数（D-REQ-2：counts[i]=floor(size[i]/spacing[i])+1），同义计划同身份〕 |
| kinematics | 消费契约确认（点/区域/姿态/工况/Must-Should→硬过滤与覆盖） | 冻结需求只读视图；采样计划定义（非样本）；要求值语义（对齐 policy 阈值的职责边界声明 §6.3） |
| trajectory | 顺序/三段语义 | sequenceKey 拓扑保证（无环）；TaskSegment 方向/距离约定（§5.1） |
| optimization | 需求基准一致性机制（快照身份） | Must 集/必验集冻结视图；候选评估与基线同基准的输入承诺 |
| project | ①命令协议/HandlerContext/draft 模块 token（`requirements`）/inverse 义务 | 两命令 payload canonical 登记、prepare 断言、逆命令、命令清单与摘要规范 |
| io | CSV/JSON 解析（RawTable/Json）、SafePath/BudgetGuard、AtomicFile | 需求字段字典（字典消费方）；导出 profile 注册（JsonProfile）〔**已落位——WP-14-T04**：`IRequirementImporter`（Import.hpp）消费 io RawTable/IStructuredDataReader（自持 profile `ird-requirements-import/1` 注册制）/ICsvWriter（canonical CSV 唯一转义点）/canonicalizeJson＋IAtomicFileWriter（原子就位）；卡面 §9.5 `io::ExportTarget` 以 requirements 侧 `ExportTarget{filePath,draft}` 等价承载——io 基线无该类型，io.md §9.0 等价调整纪律（决策登记 §14.6 v0.4）〕 |
| diagnostics | 注册协议/工厂/目录/日志 | `REQ-*` CodeDescriptor 清单（§9.6）＋文案键＋域错误→码映射 |
| ui | DraftController/注册器/SelectionModel/View3D 拾取/七态投影 | IPluginUiModule 实现、域命令清单（§9.8）、DomainReadinessItem 数据、缺项文案键；三维拾取/TCP 捕获**域侧消费面**〔**已落位——WP-14-T06**：`IRequirementCaptureService`（Capture.hpp）两写入口——`captureTcpAsFixedPoint`（L-R7：确认门→Fixed 任务点入草稿，source=UserProvided＋methodTag=captured-tcp，会话基线对账 REQ-CAPTURE-STATE-STALE）/`applyPickToOrientation`（L-R6：确认门→拾取结果装配 AlignFrame/AlignGeometryNormal 写回目标任务点）；ui 只消费确认**结果**（CaptureConfirmation——C-2 同构），对话装配/marshal 归 ui §9.2 |
| modeling | 无编译依赖（仅 token 元数据交叉引用） | 跨聚合浅引用规范（ObjectId＋token）；tcpRef/环境引用消费承诺（语义解析归评估侧） |
| workflow/ui 门控 | 就绪投影 | `requirements` 域 inputComplete/缺项数据（门控判定归 workflow）〔**DomainReadinessItem 已落位——WP-14-T05**：恰三字段（层/级别/诊断记录）呈现数据——P-REQ-6 边界：报告不含门控动作语义〕 |

**R2/后续承接**：KIN-12-S1 扩展单位（io 方言版本升级通道，本单元字典单位表随扩展）；MDL-12-S1 prismatic 链启用（点容差角度/长度维度随关节类型——字典已按列类型预留）；OPT-D 全量（需求基准不变）；采样定义新增几何（圆柱/球区域）走需求变更（P-REQ-7）。

---

## 13. 需求—设计—验证追踪矩阵

| 需求/AT | 设计落点 | 验证 | 主任务（DTB） |
| --- | --- | --- | --- |
| REQ-01 | §4.3、§5.1 | V-01 | T03 |
| REQ-02 | §5.1 三段 | V-19（联合 TRJ） | T03 |
| REQ-03 | §5.2、§6.2 | V-02/19 | T03 |
| REQ-04 | §4.5、§6.1 | V-04/05 | T03 |
| REQ-05 | §7.3 | V-08/09 | T04 |
| REQ-06 | §7.5、§8 | V-11/12/20 | T05 |
| REQ-07 | §7.1 | V-06/07 | T07 |
| REQ-08 | §9.8 L-R6 | V-22（GUI 登记） | T06 |
| REQ-09 | §5.3 | V-03 | T06 |
| REQ-10 | §9.8 L-R6/L-R7 | V-22＋L-R7 单元面 | T06 |
| REQ-11 | §7.2、§9.3 | V-06/07 | T07 |
| REQ-12 | §7.4 | V-10 | T04 |
| EVI-01/02 | §6.2/§6.3/§7.5 | V-05/11/20 | T05 |
| CON-01/02/05/06 | §4.6/§8.4 | V-15/16/18 | T03/T05 |
| KIN-01~04 消费面 | §8.2 | V-19（联合） | T03 |
| TRJ-01/02 消费面 | §5.1/§8.2 | V-19（联合） | T03 |
| OPT-01~04/07 消费面 | §8.2 | V-19（联合 AT-09） | T03 |
| PM-04/12 | §9.1/§9.3 | V-13/14 | T05 |
| UX-02/04/06/12 | §9.8/§8 | V-22 | T08 |
| NFR-COR-01/02/03 | §10.1/§3.4/§4.7 | V-08/10 及确定性断言〔已落位——WP-14-T09：四套黄金数据集（req-transport-task/req-partial-pose/req-region-sampling/req-payload-events）经 GoldenFixture 随 CI 跑通＋确定性断言（同输入→同编码字节/I-REQ-1 字典序稳定排序复验/非有限与非法与缺失引用不静默负样例逐条），见 test/GoldenDatasetsContractTest.cpp DeterminismFaces 族〕 | T09 |
| NFR-MNT-01/03/07 | §3.1/§3.4/§6.3 | 门禁＋UT | T02 |
| AT-02/04/05/09/10/23/24/27/30 | — | V-08/09/16/19/20/03/06/07/17/21 | T04~T09 |
| P-EV-9（回接义务） | §6.2 | V-05 | T01→evidence 侧同步 |
| 附录 D 通用比较公式 | §4.3/§5.3 容差口径 | V-08/黄金档案〔已落位——WP-14-T09：容差档案 tolerance/req-golden/v1.0.0.json（basis 锚 appendixD#12＋#4；全条目零宽容差且 tolerance==allowedMax——无放宽通道；消费 fieldPath 前置可解析断言）〕 | T09 |

---

## 14. 设计决策、风险、待裁决项与变更记录

### 14.1 设计决策登记（D-REQ-x；仅 requirements 所有权内取舍）

| ID | 决策 | 依据/理由 |
| --- | --- | --- |
| D-REQ-1 | 五对象分解（根＋点/区域/条件/计划四集合对象），条目内嵌、子 ObjectId 稳定 | 任务点数千条规模（NFR-PERF-03）＋四条独立失效面＋闭包膨胀权衡；与 modeling D-MDL-1 同构 |
| D-REQ-2 | 间距式采样定义在计划构建时规范化为计数；planContentIdentity 以规范化计数计算 | 同义计划同身份（确定性）；evidence §4.1.4 身份对参数计算的前提 |
| D-REQ-3 | 姿态存储恒 SI rad＋Z-Y-X 欧拉参数字面；deg/四元数仅显示投影 | core 单一权威（SA-12）；不做等效姿态归一（NFR-COR-03 不改写输入） |
| D-REQ-4 | 镜像/模板/阵列产物＝普通独立条目＋UserProvided＋methodTag 溯源；非 DerivedReadOnly、非活性链接 | 派生条目可继续编辑（REQ-07 复用语义）；活性派生图会引入跨条目真值耦合（违背单一真值） |
| D-REQ-5 | 工况→任务绑定方向唯一（appliesTo 在工况侧） | 单向登记防双头维护；点侧只读消费 |
| D-REQ-6 | 需求条目 `name` 是语义字段（改名＝内容变更＝失效），不设非语义 displayName | 名称进报告/必验清单 label/覆盖定位——语义负载高于 modeling 显示名 |
| D-REQ-7 | 不登记"允许构型"硬语义；构型约束注释化（note/processTag） | 构型硬过滤/排序规则归 KIN-02（避免双头解空间语义） |
| D-REQ-8 | `verificationOrderHint` 仅为呈现建议；EVI-02 覆盖检查与顺序无关 | 不私设"顺序覆盖规则"（§1.4 立场 3） |
| D-REQ-9 | 命令族仅两条（apply-requirement-set/-import），无逐字段命令 | 需求编辑批次频繁、整体一致性强；细粒度由编辑器局部撤销承载（REQ-11） |
| D-REQ-10 | 跨聚合浅引用（token 校验）＋语义解析归评估时 | R-1 禁互链；深度校验依赖 modeling 对象解码权，交给持有快照的下游 |

### 14.2 风险登记（R-REQ-x）

| ID | 风险 | 缓解 |
| --- | --- | --- |
| R-REQ-1 | 对端契约未冻结（evidence RequiredCaseSet/SamplingPlanRef/ReadinessSummary、io Csv/Json、ui 拾取）签名漂移 | §1.2 基线登记＋冻结后增量同步（P-REQ-8） |
| R-REQ-2 | 条目级粗粒度失效（如工况 note 改动使条件集整体换身份） | 已知取舍（与 P-MDL-2 同源）；历史保留（CON-02）；需要更细粒度走需求变更拆条目对象 |
| R-REQ-3 | 大点集对象字节膨胀（数千点×全字段） | canonical 定宽编码＋字段 SourcedValue presence 字节；性能实测超标再分片（走设计变更，不预设） |
| R-REQ-4 | CSV 方言/编码边界（外部文件无方言标识） | io 侧已有探测/拒绝码面（IO-FORMAT-CSV-ENCODING 等）；本单元只消费 RawTable，不复制规则 |
| R-REQ-5 | 拾取/捕获的会话状态与草稿基线错位 | L-R7 确认流＋REQ-CAPTURE-STATE-STALE；捕获值恒带来源与方法标记（可追溯重捕） |
| R-REQ-6 | P-EV-9 回接不同步导致双账本解读 | 冻结规则单点（resolveRequiredCases 唯一实现）；回接义务登记 P-REQ-1 并入 WP-14-T01 交付 |

### 14.3 待裁决项（P-REQ-x；上游冲突集中登记）

| ID | 事项 | 依据 | 影响 | 建议 | 裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-REQ-1 | **P-EV-9 消账**：本卡 §6.2 已冻结 enabled/mandatory schema（mandatory≡(level==Must)），evidence §4.1.3 需回接登记（"字段级权威＝requirements §6.2"） | evidence.md §15.3 P-EV-9/O-14、本卡 §6.2 | 双账本解读风险（低——evidence 现为字面消费） | evidence 所有者增量同步确认；实现面零改动（最小承载已一致） | evidence 详设所有者 |
| P-REQ-2 | 子条目 ObjectId 与 objectRefs 复核范围（与 P-MDL-1 同源）：req 集合条目子 ID 为模型内标识，非独立存储对象 | runtime v0.12②、modeling §14.3 P-MDL-1 | S5/CM-0 复核口径一致性 | 与 modeling 同一裁决：objectRefs 复核限于真实存储对象（五 req 对象＋跨域对象） | runtime 详设所有者（两卡一并裁决） |
| P-REQ-3 | ReadinessSummary 消费时序：①级输入门禁的 ReadinessSummary 由"请求方/域提供"（evidence §6.4 ①），requirements 提供数据源，但**组装点**（评估组装方现场重算 vs 消费修订内就绪记录）未明确 | evidence.md §6.4 ①/§13 | 就绪 TOCTOU（基线漂移窗口） | 组装时现场重算（本卡 prepare 同款断言套件复用，NFR-MNT-04），修订内不持久化就绪结论 | evidence 详设所有者（会签） |
| P-REQ-4 | IModuleDraftSource 方法级签名未定义（ui.md §10.5），requirements 与 modeling 共用同一接入面 | ui.md（文未定义） | T08 插件接入 | 两业务卡统一提案（buildDraftDocument/dirty 通知/模块句柄最小面），ui 卡冻结 | ui 详设所有者 |
| P-REQ-5 | 命令 token 点分 vs 无点（P-PR-9 同源）：本卡采用无点 `apply-requirement-set` | project.md §4.4.4/§6.5 矛盾 | token 迁移成本 | 服从 P-PR-9 裁决；点分则两业务卡同步迁移（随 schemaVersion） | project 详设所有者 |
| P-REQ-6 | 就绪 Blocking 的 UI 拦截点：阶段门控（workflow）消费 `inputComplete`，但"Blocking 时禁止进入 kinematics 阶段"的门控规则未在 workflow 卡冻结 | UX-12、ui P-UI-6 | 阶段解锁行为 | 门控规则归 workflow/ui（本单元只供数）；P-UI-6 裁决时一并明确 | workflow/ui 所有者 |
| P-REQ-7 | 区域几何扩展（圆柱/球）与姿态采样方向集方法（老代码有 1000 方向/360 滚转先例）上游无冻结规格 | REQ-03 仅 Box | 未来扩展无规格 | R1 维持 Box＋计数制；扩展走需求变更（不预留桩） | 需求所有者 |
| P-REQ-8 | 全部对端契约未冻结（同 P-MDL-8） | §1.2 | 实现期漂移 | 冻结后 diff 增量同步 | 各对端所有者 |

### 14.4 新增语义登记（全部位于 requirements 所有权内；无上游扩张）

1. 五个对象类型 token 与需求 schema（`req-set/req-point-set/req-region-set/req-condition-set/req-plan-set`）。
2. `ProcessTag` 11 值、`TemplateKind` 6 值、`ArrayKind` 4 值词表（REQ-07/11 语义载体；模板参数数值为设计默认，黄金锁定）。
3. CSV/JSON 字段字典（§7.3——WP-14-T01 指定交付物）与单位声明列选项（m/mm、rad/deg）。
4. `REQ-*` 稳定码 14 项（§9.6，diagnostics 注册纪律内随任务注册）。
5. `req.*` DependencyKey 角色键 4 键（§8.4——evidence §13 指定的 requirements 交出义务）。
6. 采样计划规范化规则（GridBySpacing→counts，D-REQ-2）与设计默认值（容差/覆盖率默认，黄金锁定）。

### 14.5 交付前自审结论（2026-09-22，文档级自审——不等同实现测试或正式验收）

| 自审项 | 结论 |
| --- | --- |
| 是否重复 project/io/evidence/runtime/kinematics 职责 | 否——§2.3 不拥有表；解析（io）/快照（evidence）/求解（kinematics）/门控（workflow）均只消费 |
| 是否将区域定义与采样算法混淆 | 否——§5.2/§6.2 明确"定义归本单元、生成与冻结归 kinematics/evidence"；ISamplingPlanBuilder 不生成样本 |
| 是否将需求未就绪误判为工程不可行 | 否——§8.3 四轴正交；ReadinessSummary 仅作①级输入门禁数据 |
| 是否遗漏必验工况/引用完整性/样本冻结 | 否——§6.2（P-EV-9 冻结＋回接义务）、§8 R1/R5/R6、§5.2 SamplingPlanRef 来源 |
| 是否存在第二套坐标变换/姿态语义 | 否——§5.3 隔离声明（core 位姿约定＋runtime 统一解释；镜像反射为需求数据变换并已声明边界） |
| 是否把 UI 控件当需求真值 | 否——§4.6 三态、§9.8（面板不缓存权威数据） |
| 是否绕过 ProjectCommandService | 否——§9.1 两命令；导入仅产草稿 |
| 是否把派生对象覆盖源需求 | 否——§7.2 独立 ObjectId＋零回写（I-REQ-10）＋重生成冲突诊断 |
| 是否引入未经上游批准的新状态/阈值/证据等级 | 已审计——§14.4 全部为 requirements 所有权内登记；Must/Should 词表未扩展（Info 不承接）；判定阈值零私设（§6.3 分离） |
| 是否越权修改需求/架构/其他单元机制 | 否——仅新建本卡；上游张力（P-EV-9 回接等）集中 §14.3；未改任何其他文件 |
| 旧代码覆盖与 UI 完整性 | §2.5 逐项对照（23 行，等价/增强 13、重定位 6、不承接 5 均注明理由）；UI 面板/十二条数据流/命令清单见 §9.8（v0.1 即内置，无 modeling v0.1 缺口重演） |
| 遗留 | DETAILED-DESIGN.md 索引行与 traceability 机器索引同步归治理侧（同 modeling §14.5 口径） |

### 14.6 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.9 | 2026-09-26 | WP-14-T09 落位登记（收口）：①四套黄金数据集＋容差档案交付——`testdata/golden/req-transport-task|req-partial-pose|req-region-sampling|req-payload-events/1.0.0/`（ird-golden-manifest/1 契约全量登记：datasetId/版本目录一致、integrity SHA-256＋size 逐文件申报、generator committed、history 首版留痕）＋`testdata/tolerance/req-golden/v1.0.0.json`（basis 锚 appendixD#12＋#4；全条目零宽容差且 tolerance==allowedMax——无放宽通道）。数据集为 contract-fixture 类（hand-built）：expected/ 由 generate/ 下 Node 脚本按设计规则**独立重算**产出（§6.2 必验冻结解析 mandatory≡(level==Must)、D-REQ-2 floor 规范化〔IEEE754 位级：0.6/0.2→counts=3，整数直觉 4 恰为夹具钉死面〕、I-REQ-7 Kahn 拓扑、π/180 独立计算）——与产品实现双实现互证；边角样例（零值 mass=0.0 provided/近零 1e-18/正负抵消 x+y=0/invalid 保留原串）全置位。lint 工具（TK-T03）全量扫描 13 数据集版本 0 违规。②契约测试套件落位——`test/GoldenDatasetsContractTest.cpp`（GoldenFixture〔TK-T08〕六步生命周期消费四数据集；卡 §10.2 V 行 requirements 侧断言：V-01 编辑保护与恰一命令载荷/V-02 退化拒绝＋零样本计划合法＋R6 Warning/V-03 五规则解析与非法定位/V-04 重复与悬空 R4/V-05 必验冻结黄金表/V-06 镜像黄金闭式/V-08~V-10 CSV 部分成功＋单位 SI＋JSON roundtrip/V-15~V-19 切片面〔联合行仅登记〕/V-11 空集＋R5 Warning 投影/V-17~V-18/V-20~V-21〔联合登记〕；确定性断言 DeterminismFaces 族：同输入→同编码字节＋I-REQ-1 规范化写出复验〔任意输入序同字节——编码器按规范化副本排序写出〕＋非有限 NaN 位模式注入 decode 拒绝＋非法单位 REQ-IMPORT-UNIT-ILLEGAL＋缺失引用 REF-MISSING 负样例逐条；V-22 GUI 以 GTEST_SKIP＋setOutcome(EnvUnavailable) 如实登记不执行）。③对端面契约套件落位——`contract_test/PeerFaceContractTest.cpp`（TK-T09 FaultInterceptor 伪造四对端：命令 payload 对 project〔真实 prepare 管线 Planned＋写槽 token 词表＋object() 缝故障注入 fail-fast〕、mapCsv 对 io RawTable/CsvParseReport〔rows 空而 dataRows>0 形状违约 invalid_argument＋IAtomicFileWriter commit 故障→旧文件字节完好〕、readinessSummary 对 evidence〔两字段值类型逐字段直投＋sink 故障注入〕、REQ- 16 码对 diagnostics StableCodeRegistry 契约夹具〔逐码 find 命中＋前缀-所有权一致〕；T-1/T-2 测试侧红线：产品 include/＋src/＋plugin/ 面 zero testkit 头、测试链接语句恰 {被测目标, testkit, gtest} 允许形态、testkit CMake 零 requirements 反向边〔无环〕、requirements 零 install 语句〔testdata 不入安装树——全仓安装树扫描登记归 WP-24〕）。④登记义务兑现：§13 NFR-COR-01/02/03 行与附录 D 行、§11 T09 行落位标记由本行承载。⑤增量登记（实现核对）：§10.2 V-08 行 req-transport 黄金 CSV 未置独立文件——CSV 用例按 P-REQ-8 处置经 io::RawTable 直构（CSV 方言归 io，本单元零第二方言消费面），黄金锚定由 expected 换算值承载。lint 工具缺省字典路径 `<root>/../requirements-ids.json` 与数据根布局不符（实际 `<root>/requirements-ids.json`），本任务以 --dict 显式传参执行——TK-T03 侧修正归其所有者。无语义偏差 |
| v0.8 | 2026-09-26 | WP-14-T08 落位登记：`sdurws_ird_requirements_plugin` 插件界面目标落位（§3.2"T08 随 WP-14-T08 落位"兑现；§3.1 二分结构的插件半区）——`plugin/` 目录 11 文件：PanelTreeModel（对象树投影——根→四分组→条目、节点锚＝ObjectId、UX-02 守卫复用 ensureNoInternalIdentity）、PanelStationModel（工位检查器行/五规则按 kind 显隐参数表〔承接旧 templateParameterVisibilityMask 思路，表即数据〕/来源徽标六态映射〔手工/捕获/镜像/模板/阵列/导入，判序首中即返〕/stationQuantitySpecs 消费 ui FormEditCommon〔WP-10-T08 公共件——UX-05 批量粘贴/单位同显/就地错误的宿主面〕/applyStationEditSet 编辑回填〔词表外键 fail-fast〕）、PanelRegionModel（区域表/计数-间距双模式采样视图〔规范化经 IWorkRegionService 现算——D-REQ-2 零复制〕/覆盖率目标行/区域轮廓＋采样格预览几何〔呈现几何——8 角点＋每轴 counts+1 条格线，零计数轴不产线；结果着色归 KIN-07 不实现〕）、PanelConditionModel（工况表/负载四态"未提供"占位/必验清单预览经 deriveRequirementProfile——P-EV-9 单点）、PanelValidationModel（R0~R9 恒 10 层行计数/逐项行保报告稳定序＋subject 跳转锚/预览-正式语义说明固定文案〔REQ-06〕）、PanelCommandCatalog（九条域命令逐卡表值化＋PanelRegistrationRecord §10.9 形状承载＋requirementsReadinessProjection〔P-REQ-6 边界：inputComplete/missingItemKeys 为数据事实，门控动作归 workflow/ui〕＋applyReadOnlyGate）、PanelEditFlow（submitEntryEdit/submitBatchEdit 域裁决分流〔接受→刷新＋脏标记；拒绝→就地原因保留原值〕＋LocalUndoTracker 撤销记账〔draftStatus 差值计数推不出栈态——canUndo/canRedo 精确事实源〕＋TwoLevelUndoView 两级分离呈现）、PanelRefresh/PanelSelection（§9.8 线程与刷新约束行落实——UI 线程守卫/事件驱动重演不静默丢弃/零修订选中态）、ImportWizardFlow（L-R10 五步域侧流〔io 预检半区归装配层，映射/单位预览/逐行错误/确认入草稿经域函数零复制〕＋exportCopyPrompt〔L-R11 成功提示路径/失败旧文件完好〕）、RequirementsUiModule（§11.2 三方法同形实现——P-REQ-8 暂持先例〔modeling ModelingUiModule 对齐〕＋buildDraftCommand 五对象槽信封〔根槽 allocateNew 按会话根身份、集合槽按根引用表挂载态；就绪 Blocking 不本地复判——P-REQ-6〕＋RequirementsDraftSource 真实继承 ui::IModuleDraftSource〔UI-T12 冻结面——P-REQ-4 处置兑现：buildDraftDocument 五对象载荷与命令载荷同构同版本/adoptRestoredDocument 解码经内存闭包视图重建基线＋restoredDraftPending 应用资格/rebuildOnRevision 基线锚更新〕）、RequirementsPanelWidget（Q_OBJECT 薄装配——左树＋右四页 Tab＋九命令按钮＋两级撤销三独立控件＋状态行就地呈现；AUTOMOC 仅插件目标开启）。测试增列 test/PluginPanelTest.cpp（14 用例——acceptance 1~5 具名自证；QCoreApplication 级，V-22 GUI 流程仅登记不启动）。测试入口 TestMainReport.cpp 增 QCoreApplication 构造（modeling T15 先例）。①登记义务兑现：§3.1 单元组成插件行/§3.2 执行清单"_plugin 随 WP-14-T08"/§11 T08 行落位标记由本行承载。②增量同步义务登记（R-REQ-1）：ui 侧 IPluginUiRegistrar/IPluginUiModule/SelectionModel 落位后，PanelRegistrationRecord/RequirementsUiModule 同形面/PanelSelectionState 按建模 P-MDL-8 同款机制迁移至 ui 类型（构造注入替换），本次以已落位 ui 公共类型（CommandDescriptor/UiTypes/IDraftController/IWorkbenchShell/FormEditCommon）承载。③构建图登记说明：插件链接面＝本单元计算库＋sdurws_ird_ui＋Qt6 Core/Gui/Widgets（卡 §3.2 原文——R-1 禁其他业务域单元），ird_gates 引擎解析恰增 "requirements->requirements"（自边）与 "requirements->ui"（表外 SUB）两处既有模式命中——与 modeling 插件两行同型（modeling WP-13-T15 同款先例），登记册回填归 WP-01-T03 治理面；base..head 归一化比对附件随留痕提交（traceability/builds/wp14-t08/）。无语义偏差 |
| v0.7 | 2026-09-26 | WP-14-T07 落位登记：`TemplateArray.hpp/.cpp` 公共头＋实现 TU 落位（§3.3 表 T07 行）——`ITemplateArrayService`/`TemplateArrayService`（applyTemplate/applyMirror/applyArray/regenerate 四出口＋自由函数 unlinkGenerator）。①六类工艺模板（§7.1）：TemplateKind 词表 6 值逐类黄金默认参数（defaultTemplateParams——网格规模 1×2/1×3/2×2/1×1/1×1/1×2、间距 0.5/0.4/0.3 m、ReferenceZ 进退 0.1 m、Fixed (0,0,0) rad、默认容差、ProcessTag 就近映射 Pick/MachineLoad/Place/Inspect/ToolChange/Handover——黄金锁定，修改即语义变更）；产物 source=UserProvided＋methodTag=template-…（D-REQ-4 非 DerivedReadOnly，可继续手改）。②工位镜像（§7.2）：过参考系镜像面（MirrorPlaneSpec＝refFrame＋法向）反射——位置反射 p−2(n·p)n（等距）、姿态反射共轭 R'=M·R·M 后 Z-Y-X 提取（万向锁 roll'=0 确定约定）——黄金闭式：法向 X→(+roll,−pitch,−yaw)、法向 Y→(−roll,+pitch,−yaw)、法向 Z→(−roll,−pitch,+yaw)（绕法向轴旋转分量与反射对易不变）；Fixed/PointAtTarget/ToolRollFree 正常镜像（滚转区间反手性 [min,max]→[−max,−min]），AlignFrame/AlignGeometryNormal 保留规则原样＋参数快照 orientation-pending 标记＋REQ-DERIVE-MIRROR-PENDING（不静默降级为 Fixed）；派生条目 sequenceKey 不继承（前驱名引用——原样继承即 R7 重复键）、importProvenance 不继承（来源事实不随派生失真）。③四类阵列（§7.2）：Linear（方向/间距/数量）、Rectangular（两向/行×列）、Circular（圆心/半径/起角/步距/数量——refFrame XY 平面）、Polyline（折线顶点/弧长间距——条数 floor(总弧长/间距) 派生）逐一黄金断言。④重生成/解除关联（§7.1/§7.2）：手改判定＝参数快照确定性重算＋逐名称配对（内容等值比较忽略 ObjectId/generation）；REQ-DERIVE-REGENERATE-CONFLICT 逐条 warning、手改条目原样保留不静默覆盖；unlinkGenerator（linked→false，参数快照留痕不失真）。⑤编辑器批次入口（§9.3）：applyEdit(EditBatch) 重载——批量原子性（任一条目/替换名校验失败整批拒绝；接受＝恰一次撤销入栈一次整体回滚）＋替换名核验（appliesTo/events 引用保护 §5.1 同源）＋批次警告诊断经 EditOutcome.diagnostics 透传；删除被 linked 批次引用的源条目＝允许＋REQ-DERIVE-SOURCE-REMOVED 提示"仍有 N 条 linked 派生"（§7.2 删除保护行）。⑥派生无环（§7.2）：溯源为一次性参数快照（源 ObjectId 仅以 "source-id" 字符串键入参数快照——非活性链接），再派生＝以其为源生成新批次（溯源链留痕），零活性传播。⑦§9.6 T07 行 2 码随消费者注册＋表尾增登 1 码 REQ-DERIVE-SOURCE-REMOVED（§7.2 删除保护提示分支原 14 码无承载面——T05 批 PLAN-MISSING 实现期增登同款先例），DiagCodes 工厂清单 13→16 表尾追加、DiagCodesTest 分批封闭断言随附同步（§9.6 表至此全量登记完毕）。⑧增量登记（DTB §5.4 等价调整，§9.5 代码块已随本版修订）：applyMirror 源入参由 ObjectId 列表修订为源条目值列表、ArrayParams 内嵌 sources——纯函数服务无状态无法以 ObjectId 解析源值（基类 T05 钩子"只传工作集"同源取舍）；regenerate 的 instanceId 载体以 std::string 承载（GenerationProvenance.instanceId 即 string——卡面 ObjectId 字面以 string 等价承载）；重生成替换面 newPoints 仅含未手改候选＋replaceNames（保同名候选不进批次，避免编辑器集合级名称核对整批拒绝）。⑨实现决策：批次内名称消歧＝确定性试缀（"-2"、"-3"…）——只保批次内部自洽，与工作集既有名冲突由编辑器 I-REQ-3 集合级核对整批拒绝（服务无工作集视图，越界拒绝归编辑边界）；参数快照数值文本化＝"%.17g"（IEEE754 无损往返）＋strtod 全消费解析。无语义偏差（⑧为签名等价承载登记） |
| v0.6 | 2026-09-26 | WP-14-T06 落位登记：`OrientationResolution.hpp/.cpp`＋`Capture.hpp/.cpp` 四公共头/实现 TU 落位（§3.3 表 T06 两行）。①姿态规则浅解析 `resolveOrientationRule`（§5.3 隔离声明落位——"规则'解析'＝把规则参数解析为确定性参考姿态/方向并记录来源 resolution"）：五规则各建样例产出确定性解析值（Fixed＝参数字面 rad/Z-Y-X 不归一等效角、PointAtTarget＝refFrame 系单位方向、ToolRollFree＝滚转区间、AlignFrame/AlignGeometryNormal＝闭包浅核对通过的目标 ObjectId＋特征语义——数值姿态/法向归评估时深度解析，本层零坐标变换零姿态求解）；resolution 留痕＝`OrientationResolution{kind＋targetObjectId＋解析值}`（AT-23/V-03 观测点）。②失败可定位（acceptance 2）：参数面复用 `validateOrientationRule` 单点（零向量/rollRange 逆序/缺 feature/非有限角）＋稳定码 REQ-READY-POSE-ILLEGAL；引用面复用闭包浅核对（悬空/token 失配）＋REQ-READY-REF-MISSING——诊断 subject/localName 回指需求条目（objectId＋name）；**§8.1 浅校验边界**：仅读 objectRefs 的 oid/token 元数据、不解码 modeling 对象字节（用例全程无 modeling 对象的结构性自证）。③三维拾取/TCP 捕获域侧 `IRequirementCaptureService`（§9.8 L-R6/L-R7）：两写入口均以 `CaptureConfirmation` 必填参数承接 ui §9.2 确认对话桥**结果**（C-2 同构不变量：Confirmed⇔principal 非空；Pending/Rejected 同走零变更）——确认门先于一切写路径，接口面无绕过确认的入口（acceptance 4：编译期成员形状静态断言＋运行期取消/未决/凭据违约三形态零变更负向用例）；L-R7 产物条目 source=UserProvided＋methodTag=captured-tcp（§5.1 来源标记行，R-REQ-5 缓解第二半）；L-R6 拾取结果装配 AlignFrame/AlignGeometryNormal 更新目标任务点（其余字段原样保留）；未应用不失效＝服务接口面无任何 project 写端口（草稿唯一写目标——结构保证）＋重载基线后工作集/基线对象字节零变化用例。④§9.6 T06 行 `REQ-CAPTURE-STATE-STALE` 随消费者注册（DiagCodes 工厂清单 12→13 表尾追加，DiagCodesTest 分批封闭断言随附同步；分类 ResourceMissing——会话基线相对草稿基线 Changed）；**空基线口径实现决策**：编辑器闭包抽象不携带修订身份（Editor.hpp 载入注），draftStatus().baseRevisionId 为空串——严格相等对账下带会话基线的捕获恒登记 STALE（保守知情：基线未标注即无法证明一致；命令提交面补齐基线 id 后对账自然精确化），warning 不阻断、重捕可消除（UserRetry）。⑤实现决策登记：Readiness.cpp 文件内私有辅助 `closureRefViolation` 提升为公共自由函数（§8.1 浅核对语义单源——就绪层与解析层共用，行为零变化）；STALE 对账仅 L-R7 承载（§9.6 行语义钉在"TCP 捕获时"——L-R6 拾取无会话修订键，不扩面）。§12 ui 交接行落位标记同步。无语义偏差 |
| v0.5 | 2026-09-26 | WP-14-T05 落位登记：`Readiness.hpp/.cpp`＋`CommandHandlers.hpp/.cpp` 四公共头/实现 TU 落位（§3.3 表 T05 两行）。①`IRequirementReadinessChecker`（§9.5 原文签名）——R0~R9 分层就绪校验（§8.1 表逐行，短路优先；Blocking/Warning/NotApplicable 三级），CheckContext 闭包元数据浅校验（仅读 objectRefs 的 oid/token——§8.1 浅引用边界），`readinessSummary` 两字段投影（evidence §6.4① 值类型直投）；**层-码映射**（Readiness.hpp 文件头登记）：R0 根引用表→REF-MISSING、R1/R2→REF-MISSING、R3→POSE-ILLEGAL、R4 悬空绑定→REF-MISSING（"引用悬空"语义就近承载）、R5→NO-REQUIRED-CASE（Warning）、R6→PLAN-DEGENERATE＋PLAN-MISSING（零计划 Warning）、R7→SEQ-CYCLE、R8→REF-MISSING、R9→SCHEMA-UNSUPPORTED；条目 id/name 不变量违约＝调用方前置 fail-fast（错误二分——不产诊断）；条目级各层仅判启用条目（§4.3 enabled 行——ACC3「任一启用的 Must 条目」口径）。②R7 复用 `TaskPointService::checkSequence`（区域以同名/同键探针复用——零算法复制）、R5/R9 复用 `resolveRequiredCases`（P-EV-9 单点）——NFR-MNT-04。③O-39 处置（acceptance 5，消解 P-REQ-3）：`readinessSummary(check(ws,ctx))` 可重入纯函数投影面，修订内不持久化就绪结论；valid 面＝报告无 Blocking（Should/集合级非法同样使输入不可消费——保守门禁），invalidMustItems＝enabled∧Must 条目全量清单（check 内计算、summary 零重算）。④`IRequirementCommandHandler` 基类＋`apply-requirement-set`/`apply-requirement-import`（O-35 无点 token——P-REQ-5 随裁决消解）：prepare 管线 decode→基线同源重建（防御性复核 fail-fast）→通用槽校验→钩子装配（PA-1 取号）→**就绪 R0~R9 现场重估**（候选闭包后像；Blocking→RejectedHardAssert＋逐项定位＋REQ-READY-INPUT-INCOMPLETE 汇总一条；Warning→随 diags 留痕）→计划最终化（requiresDualCompile=false——需求对象不进 WorkCell 描述；confirmableFindings 恒空——SA-15 不私设）；快照式逆命令＝受影响对象前版字节（首次应用无前版＝nullopt）；import 附加导入溯源完整性断言（I-REQ-8：任务点/区域条目溯源在场＋摘要非全零＋行号≥1——工况/计划条目无溯源字段不适用）。⑤实现决策登记：§9.5 钩子签名只传工作集（值模型无五对象存储 oid），职责切分＝基类持基线条目面做通用校验/inverse、钩子做候选装配；Apply 恒含根槽（修订闭包锚），根字节恒由候选根重编码（挂载增量随根持久化），载荷根字节引用与取号挂载的一致性由就绪 R0 闭包核对兜底。⑥§9.6 T05 行六码随消费者注册＋表尾增登 `REQ-READY-PLAN-MISSING`（§8.1 R6"Warning（零计划）"分支原六码无 warning 级承载——modeling WP-13-T10 实现期增登先例），DiagCodes 工厂清单 5→12、DiagCodesTest 分批封闭断言随附同步。⑦R8"Warning（PendingManualResolution 姿态规则）"分支依赖 T07 派生流状态标记（现值模型无承载字段），随 T07 落位增补——本层不私建状态语义（NFR-COR-03）。P-REQ-6 边界声明：DomainReadinessItem 恰三字段呈现数据，报告不含门控动作语义（用例 ReqReadiness.PReq6ReportCarriesNoGateAction_ACC7 断言）。§12 evidence/workflow 行落位标记同步。无语义偏差 |
| v0.4 | 2026-09-26 | WP-14-T04 落位登记：`Import.hpp/.cpp` 公共头＋实现 TU 落位（§3.3 表 T04 行）——`IRequirementImporter`/`RequirementImporter`（mapCsv/mapJson/exportCopy/fieldDictionary 四出口）、字段字典冻结表（§7.3 20 字段＋中英别名表＋必备集 {id,name,x,y,z}＋长度/角度/无单位/文本列种类，`FieldDictionary`/`autoDetectMapping`）、单位声明与预览（`ImportUnitOptions`/`previewUnitConversion`——与 mapCsv 同一声明校验/换算入口，落库经 core 唯一换算归一 SI，NFR-COR-03）、行级部分成功（`ImportOutcome{status,entries,rowErrors,ignoredColumns,defaultedFields,sourceDigest,sourceMarkedDraft}`——REQ-IMPORT-ROW-ERROR/DUPLICATE-ID 行级、UNIT-ILLEGAL 列级/结构级、FRAME-UNKNOWN 警告级）、副本导出（CSV 经 io ICsvWriter canonical 写出＋内部原子替换；JSON 经 canonicalizeJson＋IAtomicFileWriter OverwriteAtomic——失败旧文件完好）。增量登记：①§9.6 T04 行 4 码随消费者注册（DiagCodes 工厂清单 1→5 表尾追加，DiagCodesTest 分批封闭断言 1→5 随附同步——T03 期 8→9 ErrorsTest 先例同源）；②卡面 §9.5 `io::ExportTarget` 以 requirements 侧 `ExportTarget{filePath,draft}` 等价承载（io 磁盘基线无该类型——P-REQ-8 处置：io.md §9.0"等价调整、语义不变"；CSV draft 导出无标记通道→值面拒绝 fail-visible）；③CSV 源摘要＝RawTable 规范投影 SHA-256（io 流式契约不回传文件字节——通道内内容寻址自洽）；JSON 源摘要＝输入字节 SHA-256；④行号口径＝RawTable 数据行序/JSON 记录序（1 起——RawTable 不携带物理行号）；⑤导入条目 ObjectId 恒全零待命令 prepare 分配（§7.3 原文＋O-36——纯函数确定性与随机分配解耦）；⑥导出 id 列＝ObjectId 规范文本，草稿（全零）合成占位 "draft-<条目序>"（去重键非空语义保持——确定性合成）；⑦引用文本语法：ref_frame ∈ {World|model:<obj->|scene:<obj->|裸 <obj->→ModelFrame＋FRAME-UNKNOWN}、tcp ∈ {DefaultTcp|tool:<obj->|<tcpKey>}；⑧JSON 通道未知记录键＝行级错误（NFR-DEP-04 不静默吞字段——与 CSV 多余列"忽略清单"分域，顶层未知键经 io profile Reject）；⑨词表可选列空单元格＝字典缺省（不判词表外）；⑩坐标表通道导出仅 Fixed 姿态规则参数字面（非 Fixed 规则条目完整参数不经本通道——正式工件通道承载）。§12 io 交接行同步落位标记（acceptance 2 登记义务）。无语义偏差 |
| v0.3 | 2026-09-25 | WP-14-T03 落位登记：`RequirementTypes/Codec/Services/Sampling/Editor` 五公共头＋实现 TU 落位——TaskPoint/WorkRegion/OperatingCondition/SamplingPlan/五对象值模型与 I-REQ-1~10 校验层（§4.7 实现面）；`IRequirementCodec` canonical 编码（family "IRDREQO"、小端定宽、SourcedValue 四态、集合条目规范化副本排序写出——同集合任意输入序必得同字节；decode 四步校验链：版本→结构→规范序→不变量复核；RequirementProfile 派生档不入编码）；三领域服务（createPoint/Region/Condition 构造边界拒绝＋checkSequence 前驱名拓扑〔顺序键＝前驱条目名引用——§5.1"删除被顺序键引用的任务点→拒绝"的语义落实，Kahn 消去无环＋重复/悬空检出〕＋normalizeSampling〔D-REQ-2〕＋resolveRequiredCases〔§6.2 冻结 schema 唯一实现点〕）与 deriveRequirementProfile（§4.8，复用冻结解析单点）；`ISamplingPlanBuilder`（buildPlan 规范化计数/digest 计划分母＋canonical 摘要；接口面零样本出口——§5.2）；`IRequirementEditor`（闭包注入载入/编辑差值四段校验链/删除引用保护 §5.1/局部撤销重做/中文变更摘要——UI 线程）。增量登记：①`RequirementErrorCode` 表尾增列第 9 值 `MalformedPayload`（canonical decode 结构校验失败码——本头 §9.3~§9.5 接口 @错误 行收编口径的自然延伸；无 REQ-* 映射码行，字节面错误经值面返回）；②`ErrorsTest` 全表封闭断言随 8→9 同步（合法登记随附同步）；③canonical 编码"查询轨两态载体"自持 `RequirementExpected`（§9.5 Expected 签名落位形态）——不复用 runtime::Expected：卡 §3.2 边表 runtime 行"无编译依赖"＋T02 红线测试五边封闭＋白名单登记属治理面，本单元内自持同构最小模板（值语义与 runtime 同构，登记决策见 Codec.hpp 头注）；④`ObjectClosureView` 以 requirements 侧 `RequirementObjectClosureView` 注入抽象落位（modeling CanonicalBridge 同构——P-RT-5 注入形态）；⑤rw::math::Vector3D 头依赖随集成/冒烟两模式注入（runtime RT-T03 冒烟 header-only 机制同款，CMakeLists 条件分支——零新增链接边）。§1.2 代码落位行相应事实由本行承载；§12 evidence 交接行同步 canonical 编码登记＋planContentIdentity 输入落位标记（acceptance 2 登记义务）。无语义偏差 |
| v0.2 | 2026-09-25 | WP-14-T02 落位登记：`requirements/CMakeLists.txt` 新建（`sdurws_ird_requirements` INTERFACE 占位→STATIC，目标名/别名不变；C++17 显式、零 Qt、PUBLIC 链 core/diagnostics/project/io/evidence 五条 §3.2 登记边——runtime 边未登记〔T02 零 runtime 公共值类型引用，实际引用时按 §3.2 增登〕；`_test`/`_contract_test` 随文件注册〔ird_add_gtest 自持宏，LABELS ird〕；配置期红线守卫自持 R-1/R-3）；T02 公共面三头落位——`ObjectTypes.hpp`（五对象 token＋ProcessTag 11/TemplateKind 6/ArrayKind 4 词表＋五 schema 版本常量）、`Errors.hpp`（RequirementErrorCode 8 值＝§9.3~§9.5 @错误 行收编＋RequirementError 值类型＋域错误→稳定码映射）、`DiagCodes.hpp`（REQ-SCHEMA-UNSUPPORTED 先注册——§9.6 T02/T03 行，其余 13 码随消费者任务分批注册、不预建）；project/io/evidence 三边落位期"仅链接不消费"（P-RPT-9 先例，消费随 T04/T05 回填）。§1.2 代码落位行、§3.2 执行清单相应事实由本行承载；无语义偏差 |
| v0.1 | 2026-09-22 | 首版草案（WP-14-T01 承接）：14 章全量——上游基线登记（REQUIREMENTS v1.16/ARCHITECTURE v0.12/十单元卡 Draft 系/旧代码 35 文件实测）；REQ-01~12 承接总表＋拥有/消费/不拥有边界＋§2.5 旧代码功能对照（23 行）；五对象数据模型（I-REQ-1~10）；任务点三段/区域采样定义/五姿态规则；工况与 P-EV-9 必验 schema 冻结（§6.2，含 evidence §4.1.3 回接义务）；镜像阵列与 CSV/JSON 导入管线（字段字典冻结）；R0~R9 就绪分层＋四轴正交表＋角色键约定；8 必需＋2 支撑接口契约；14 个 REQ- 稳定码；22 行故障注入矩阵；WP-14-T01~T09 排序；双向交接清单；追踪矩阵；10 决策/6 风险/8 待裁决；12 项自审。状态 `Draft`，待评审 |

> 自审声明：本文档自审仅覆盖设计一致性、边界与上游对齐，不等同于实现测试通过或正式验收（acceptance-protocol.md 流程另行执行）。

