# 工业机械臂设计软件 · modeling 单元详细设计（阶段 B）

> 2026-09-22 新建（WP-13-T01 承接）。本文是 modeling 单元的唯一详细设计：依据 REQUIREMENTS MDL-01～22 与 ARCHITECTURE §3.1/§3.3/§7.1～§7.10 的分配，把 RobotDesign 数据模型、模板与参数化、URDF/Xacro/WorkCell 导入映射、双权威参数化、物性估算、DH↔显式转换、传动耦合输入、模型就绪校验、CanonicalModel 交接、建模命令与撤销/重做写到可直接实现的深度。本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.20（2026-09-26：WP-24-T03 首版装配——P-MDL-8 消账：ModelingUiModule 同形→继承 ui::IPluginUiModule（三方法 override，签名/语义零变化）＋新增装配 API（bindCommandSubmit/bindTextResolver/seedTemplateSession/createPanel）＋面板 setCommandTitleResolver 呈现缝＋装配门面 assembly/sdurws/ird/modeling/ModelingPluginAssembly.hpp/.cpp（plugin 目标 PUBLIC 包含面，宿主装配层唯一消费入口）＋真宿主挂位与 UiText 中文文案实证（ui.md v1.15 同批；分支 wp24-t03；版本号承接 wp13-t15-app v0.19 先推序）；§15 变更记录；v0.18（2026-09-25：WP-13-T16 实现落位登记——契约测试套件四文件（test/ 三件两模式＋DhConvert 黄金件集成门控）＋contract_test 跨单元对端件＋四数据集＋容差档案落位＋§11 T16 行落位注记＋§15 变更记录；v0.17＝2026-09-25：WP-13-T15 实现落位登记——`sdurws_ird_modeling_plugin` 插件目标落位＋plugin/ 五件源码＋构建图契约测试随附同步＋测试目标 23 条具名用例＋冒烟分支 boost 头路径＋§14.6 变更记录；v0.16＝2026-09-25 WP-13-T14 实现落位登记——§3.3 表 ModelDiff.hpp 行 `ModelDiff.hpp/.cpp` 落位＋§14.6 变更记录；v0.15＝2026-09-25 WP-13-T13 实现落位登记——§3.3 表 Package.hpp 行 `Package.hpp/.cpp` 落位＋WC/DWC XML 外供编排面登记＋§9.5 T13 行两码注册＋§14.6 变更记录；v0.14＝2026-09-25 WP-13-T12 实现落位登记——§3.3 表 CanonicalBridge.hpp 行 `CanonicalBridge.hpp/.cpp` 落位＋§9.1/§9.2 实现层细化登记＋§15 变更记录；v0.13＝2026-09-25 WP-13-T11 实现落位登记——§3.3 表 Template.hpp 行 T11 落位＋Parts.hpp 行归属订正＋§15 变更记录；v0.12＝2026-09-25 WP-13-T10 实现落位登记——§3.3 T10 行 `Parts.hpp` T10 行编辑流落位＋§4.10 增补 I-MDL-13 行＋§9.5 增登 `MDL-REF-PROTECTED`/`MDL-READINESS-DEFAULT-TCP-INCOMPLETE` 两行＋§14.6 变更记录；v0.11＝2026-09-25 WP-13-T09 验收返工登记——§14.6 变更记录；v0.10＝2026-09-25 WP-13-T09 实现落位登记——§3.3 T09 行 `DhConvert.hpp/.cpp` 落位＋§14.6 变更记录；v0.9＝2026-09-22 WP-13-T08 实现落位登记——§3.3 T08 行 `Readiness.hpp/.cpp`＋`CommandHandlers.hpp/.cpp` 落位＋§9.5 增登 `MDL-READINESS-PHYSICS-MISSING` 行＋§14.6 变更记录；v0.8＝2026-09-22 WP-13-T07 实现落位；v0.7＝2026-09-22 WP-13-T06 实现落位；v0.6＝2026-09-22 WP-13-T05 实现落位；v0.5＝2026-09-22 WP-13-T04 实现落位；v0.4＝2026-09-22 WP-13-T03 实现落位；v0.3＝2026-09-22 WP-13-T02 构建落位；v0.2＝2026-09-22 评审响应；v0.1＝2026-09-22 首版草案，WP-13-T01 交付物） |
| 日期 | 2026-09-25 |
| 状态 | **`Draft`**（待评审；DETAILED-DESIGN.md 单元状态表中的"modeling｜待产出"以本卡落盘为准，索引行同步由治理侧执行，本卡不代改） |
| 文档代号 | UNIT-MODELING |
| 单元 | modeling（业务域单元，ARCHITECTURE §3.1：模板创建、URDF/Xacro 导入、双权威参数化、物性估算、工具/场景/安装姿态编辑、编译触发、DH↔显式转换；ARCHITECTURE §3.3 二分结构：零 Qt 计算库＋Qt Widgets 插件） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.12（`Draft`，2026-09-10）** |
| 协作输入 | `units/core.md` v0.11、`units/runtime.md` v0.15、`units/project.md` v0.18、`units/policy.md` v0.14、`units/io.md` 卡头 v0.9（§15.5 变更链至 v1.0）、`units/diagnostics.md` v0.12、`units/evidence.md` v1.3、`units/ui.md` v1.5（文末登记 v1.6）、`units/testkit.md` v0.9——均为 **Draft/Draft-Structured（未冻结）**；本卡消费的签名以各卡当前文本为基线，冻结后按影响面增量同步（§14.3 P-MDL-8） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立）→ `units/*.md`（单元任务卡）。本文即 `units/modeling.md`，按任务卡深度编写 |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/`（**WP-13-T02 已落位**：单元级 `CMakeLists.txt`——`sdurws_ird_modeling` STATIC（零 Qt、C++17、PUBLIC 链 §3.2 六边）＋`sdurws_ird_modeling_test`/`sdurws_ird_modeling_contract_test`（ird_add_gtest 宏＋ctest LABELS "ird"）＋配置期红线守卫；公共面 `ObjectTypes/Errors/DiagCodes.hpp`（§3.3 表 T02 行）＋对应 src 翻译单元；门禁白名单 `modeling→core/diagnostics/project/runtime/policy/io` 六边已登记（ird_gates_whitelist.cmake `IRD_ALLOWED_UNIT_EDGES`＋`IRD_EXTRA_EDGE_REFS` 成对登记）；`_plugin` 随 WP-13-T15——见 §3.2、§11） |
| 任务归属 | `development-task-breakdown.md`（v0.16）WP-E／WP-13（T01～T19）；本文 §11 只做单元内部任务拆分与排序，不重排 WP 编号 |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送；Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`、逐个绝对路径启动 |
| 实现口径 | **从头构建**（REQUIREMENTS v1.9/v1.11 确立）：一切实现按需求与本文新建，不继承、不恢复任何历史实现源码。旧代码位置（用户提供 `D:\10_Source_Repos\old\src`；需求登记为仓库根 `./old/`）**本卡编写全程未读取**，仅按需求附录 A 功能级台账做范围对照，不构成任何设计或实现的语义来源 |

---

## 目录

1. 文档信息、上游基线与目标（§1）
2. 需求承接与职责边界（§2）
3. 单元组成、依赖与公共头文件布局（§3）
4. RobotDesign 数据模型（§4）
5. 模板、参数化和物性估算（§5）
6. URDF/Xacro/WorkCell 导入映射（§6）
7. 双权威参数化与 DH 转换（§7）
8. 传动耦合与模型就绪校验（§8）
9. CanonicalModel 交接与公共接口（§9）
10. 验证方案及故障注入矩阵（§10）
11. 阶段 B 实现任务拆分（§11）
12. 后续阶段承接与接口交接清单（§12）
13. 需求—设计—验证追踪矩阵（§13）
14. 设计决策、风险、待裁决项与变更记录（§14）

---

## 1. 文档信息、上游基线与目标

### 1.1 文档定位

modeling 是七阶段工作流的第一阶段（`ui` StageId 序列 `modeling → requirements → …`，ui.md §6.4）的业务域单元：工程师在本单元完成机器人方案的**业务建模**——创建模板、编辑参数、导入 URDF/Xacro/WorkCell、配置物性与传动、校验就绪，最终以领域命令把合法修改提交为项目修订，并触发 runtime 双编译。本文回答"modeling 内部如何组织"：数据模型、对象身份、接口签名、状态机、错误语义与验证设计。

三条不变立场（全文贯穿，任何章节冲突时以此为准）：

1. **RobotDesign 是权威参数化的业务编辑模型；CanonicalModel 是 runtime 消费的规范模型；WorkCell/DynamicWorkCell 是编译派生对象**（ARCH §7.3 三段链）。modeling 只拥有第一段的编辑语义，不拥有后两段。
2. **一切"应用修改"经 project 命令端口**（ARC-01 唯一写路径）；modeling 不直接写项目目录、不实现事务与锁；导入结果永远是草稿，不经确认与命令不产生修订（PM-01、io.md §10.5）。
3. **单一真值**：不维护第二套 RobotDesign 真值；UI 控件不是模型数据；编辑态对象不得被任何评估器消费（CON-01/PA-3）；CanonicalModel 的编译唯一归 runtime（ARC-03）。

### 1.2 上游与磁盘现状登记（2026-09-22 实测）

| 输入 | 磁盘版本 | 状态 | 本卡消费方式 |
| --- | --- | --- | --- |
| `REQUIREMENTS.md` | v1.16（2026-09-09，含 C1～C8） | `Accepted` | 需求语义与验收唯一权威；MDL 全 22 条、CON-01/02、PM-01/04/05/12、UX-02/04/06/07/08、NFR-COR-01/02/03、NFR-MNT-01/03/07、NFR-SEC-01/02、AT-01/04/05/15/16/17/18/27/28/29/30/31/33/37/38、附录 D 容差表 |
| `ARCHITECTURE.md` | v0.12（2026-09-10） | `Draft` | 单元边界、二分结构（§3.3）、依赖表（§3.5）、编译链（§7.3）、名称解析（§7.4）、导入安全（§7.9）、基座—世界单一不变量（§7.3/M-11） |
| `AGENTS.md`（仓库根） | 当前工作区版本 | 生效中 | 编码与协作约定（注释、分支、提交、验证留痕） |
| `units/core.md` | v0.11 | `Draft-Structured` | ObjectId/ContentVersion/SourcedValue/ValueProvenance/Units/Compare/DiagData/Events 契约 |
| `units/testkit.md` | v0.9 | `Draft-Structured` | 黄金数据集清单契约（`ird-golden-manifest/1`）、容差档案（`testdata/tolerance/<profile>/v<ver>.json`）、边角样例强制项——仅 §10 数据集设计消费 |
| `units/project.md` | v0.18 | `Draft` | ProjectCommandService/ICommandHandler/HandlerContext/CommandPlan/DraftService/IProjectQueryPort/StaleRevisionRejected |
| `units/evidence.md` | v1.3 | `Draft-Structured` | AnalysisSnapshot 引用形态、`model.robot-design` 依赖键示例、§13 交接清单 B·modeling 行、Preview 非快照规则 |
| `units/runtime.md` | v0.15 | `Draft-Structured` | RobotDesignDescription/IRobotDesignReader/ICanonicalModelCompiler/RuntimeNameMap/RuntimeSnapshot/P-RT-4/P-RT-5；**`kRobotDesignObjectType="robot-design"`（`Sources.hpp` 已落位常量，字面值以代码为准）** |
| `units/policy.md` | v0.14 | `Draft-Structured` | IPolicyProvider/EngineeringPolicySet/JointThresholds（行程上限 4π）/IJointLimitEvaluator/SceneObjectRole 词表归建模语义注记 |
| `units/execution.md` | 存在（未逐条消费） | — | modeling 不直接依赖 execution（任务/调度/工作进程均经 project 命令与 L5 装配间接协作）；仅 §2.3 边界声明引用 |
| `units/diagnostics.md` | v0.12 | `Draft` | StableCodeRegistry 注册协议、MDL- 前缀所有权、ConfirmableFinding/FindingRecord/callbackToken、IDiagnosticFactory/IDiagnosticSink |
| `units/ui.md` | v1.5（文末登记 v1.6） | `Draft` | IDraftController/attachModule/IModuleDraftSource、IPluginUiRegistrar/IPluginUiModule、DomainReadinessItem、七态状态词、CommandDescriptor |
| `units/io.md` | 卡头 v0.9／§15.5 至 v1.0 | `Draft` | ISafePathResolver/IBudgetGuard/IResourceReader（open/snapshot/identify/dependencyTree）/ResourceSnapshot/IResourceSnapshotter（solidifyToStaging）/外部引用三段边界/Xacro 护栏 |
| `units/reporting.md` | 存在（未消费） | — | modeling 不直接依赖 reporting（报告消费对象库与证据，不经 modeling） |
| `DETAILED-DESIGN.md` | 索引（70 行） | — | modeling 行登记"待产出／WP-E"；本卡落盘后该行更新归治理侧 |
| `development-task-breakdown.md` | v0.16 | `Draft` | WP-13-T01～T19 任务包、§5 构建约定、§4 待裁决（O-16/P-RT-4、O-27/P-03）、§3 追踪矩阵 MDL 行 |
| 代码落位 | `industrialrobot/modeling/`：**WP-13-T02 已落位**——单元级 `CMakeLists.txt`（STATIC＋两测试目标＋配置期守卫）、公共头 `ObjectTypes/Errors/DiagCodes.hpp`＋src 三翻译单元、`test/`＋`contract_test/` 用例；`_plugin` 目标随 WP-13-T15 建 | — | 上级 `cmake/ird_gates_whitelist.cmake` 中 modeling 列于 `IRD_BUSINESS_UNITS`（R-1 禁互链集合）、`IRD_ALLOWED_UNIT_EDGES` 已增 modeling→core/diagnostics/project/runtime/policy/io 六条登记边（WP-13-T02，§3.2 边表；`IRD_EXTRA_EDGE_REFS` 成对登记出处） |

缺失文件登记：用户清单所列输入文件**全部存在**，无缺失；`units/modeling.md` 本身此前不存在，即本卡（新建 v0.1）。

### 1.3 交付目标

本卡落盘后，后续单元（requirements/kinematics/trajectory/dynamics/selection/optimization/workflow 及 ui/project/io/runtime 的阶段 B 增量）应能依据统一契约：

- 创建和编辑 RobotDesign（模板、参数化、物性、工具、场景、安装姿态）；
- 从 URDF/Xacro（R1）与 WorkCell（R2/MDL-18）导入模型，得到可观察的映射报告与草稿；
- 管理几何、质量、质心、惯量、关节、工具和场景，且引用受保护、身份稳定；
- 维护唯一模型真值（编辑态→草稿→已应用修订，无第二真值）；
- 经 `IRobotDesignReader` 向 runtime 提供确定性编译输入，生成 CanonicalModel；
- 把合法修改作为 project 命令提交（断言分域、可确认诊断放行、双编译原子、可撤销/重做）；
- 对输入错误、模型不完整和编译失败提供可追溯诊断（MDL- 前缀稳定码）。

### 1.4 非目标（本卡明确不做）

- 不实现 WorkCell/DynamicWorkCell 编译（runtime）；不实现 FK/IK/轨迹/动力学/传动虚功映射/选型/优化算法；
- 不实现 CSV/URDF/Xacro 的安全解析底层设施（io 拥有 SafePath/BudgetGuard/资源读取；Xacro 护栏归 io，展开语义归 modeling，见 §6.5 与 P-MDL-4）；
- 不实现碰撞策略、碰撞评估（policy 唯一实现；modeling 禁止直链 `sdurw_proximity`，policy.md R-POL-2）；
- 不实现项目目录写入、事务、锁、草稿定时器（project/ui 分工）；不实现报告渲染；
- 不提前接入阶段 C/D 消费（轨迹/动力学/选型/优化）；R2 条目（MDL-18/21、MDL-12-S1）只设计边界不实现；
- 本次不启动任何 GUI 程序（§10 测试仅设计流程，Windows GUI 测试遵循 AGENTS.md，执行归实现任务）。

---

## 2. 需求承接与职责边界

### 2.1 MDL 家族承接总表

| 需求 | 摘要（语义以 REQUIREMENTS 原文为准） | 本卡设计落点 |
| --- | --- | --- |
| MDL-01 | 六/七轴全旋转产品模板创建，逐轴定义关节（类型、轴线、零位、限位、速度/加速度）；4～5 轴或含移动关节仅导入识别与草稿兼容编辑（§2.1 支持矩阵） | §5.1（模板清单与 P-03 七轴登记不启用）、§6.4（链型判定） |
| MDL-02 | StandardDH 与显式 Joint+Origin+Axis 双权威参数化；权威源互斥、派生只读；切换规则；七轴/S-R-S 锁定显式 | §7.1～§7.3（权威模型）、§7.6（切换条件） |
| MDL-03 | URDF 导入：字段映射、默认补全、忽略/不支持项报告；不静默猜测/降级/排除 | §6.1～§6.3 |
| MDL-04 | 视觉/碰撞网格、基座、法兰、默认 TCP、Home/Zero、自碰撞配置 | §4.4～§4.6（数据模型）、§6.3（自碰撞→策略草稿输入） |
| MDL-05 | 几何物性估算层（实心/空心圆截面、矩形截面；唯一公式表）；惯量基准＝质心＋连杆系（M-2）；权威值覆盖；质心修改的平行轴确认 | §5.3 |
| MDL-06 | 应用前物理合法性前置断言（分域 V15-01）；行程上限可配置策略校验（M-10，确认放行 SA-15）；原子双编译，失败不提交 | §8.2（分层校验）、§9.3（命令处理器与断言执行） |
| MDL-07 | 关节树、参数表、三维选择联动；选中只显示相关属性 | §9.4（IRobotDesignEditor 提供数据）；界面落位 §11 WP-13-T15 |
| MDL-08 | 数据层基线/候选差异比较（Model Diff 数据实体）；呈现归 UX-13 | §9.4（IModelDiffService） |
| MDL-09 | Axis 独立可持久化一等字段；显式模式可编辑、DH 模式只读派生；改轴＝设计变更 | §7.2（参数来源表）、§7.3（冲突矩阵） |
| MDL-10 | DH→通用关节无损；通用关节→DH 五状态判定（两阶段、互斥化）；权威切换仅 Exact/ExactNonUnique 经等价验证 | §7.4～§7.6 |
| MDL-11 | URDF 任意有限非零轴；缺 `<axis>` 按 URDF 语义取局部 +X 成待确认草稿；零/非法轴仅报告不提交 | §6.3（映射表 M-8/M-9 行） |
| MDL-12 | 导入识别四类关节；文件拓扑维度一（分支报告＋显式选链）；所选主链能力维度二；continuous 工程工作范围确认；mimic/planar/floating/耦合阻断口径 | §6.4 |
| MDL-13 | 工具定义编辑；RobotDesign 保存法兰与默认 TCP；任务/负载经引用使用 ToolDefinition 不复制几何 | §4.4（ToolDefinition 对象） |
| MDL-14 | 编译生成完整 RuntimeNameMap；引用经对象 ID 解析，不漏/不重复前缀/不引用旧名 | §9.1（交接；实现归 runtime，modeling 保证对象 ID 与 localName 输入质量） |
| MDL-15 | 场景固定帧与环境几何编辑；环境对象显式引用进入项目并参与碰撞 | §4.5（SceneObject 对象） |
| MDL-16 | 动力学参数与限值层（额定/峰值力矩、摩擦 fv/fc/偏置缺失→DataInsufficient 降级闭环 M-8）；合成规则见 SEL-10 | §4.7（DrivetrainDesign 对象）、§5.4 |
| MDL-17 | 用户命名位姿管理（除 Home/Zero）；位姿仅会话与检查参考，不改权威模型 | §4.6（PoseSet 对象，独立对象不进计算切片） |
| MDL-18 | 任意外部 WorkCell 反向导入（有损提取＋提取报告），结果为草稿；R2/阶段 D | §6.6（仅边界设计） |
| MDL-19 | Xacro 受控预处理；展开失败/依赖缺失可定位诊断；展开结果进 URDF 同一安全边界 | §6.5 |
| MDL-20 | 规范模型包导出/导入 roundtrip（自有工件）；外供 WorkCell/DWC XML；导出失败项目状态不变 | §6.8 |
| MDL-21 | 传动耦合矩阵（R2/阶段 D）：常矩阵双向映射；非常/病态诊断阻止；mimic/闭环 R1 阻断 | §8.1 |
| MDL-22 | 基座安装姿态一等参数（预设＋任意欧拉角，原子持久化）；未配置默认地面；基座—世界变换单一编译不变量（M-11） | §4.3（BasePlacement 字段）、§7.7（隔离声明）、§9.1（编译进 R_world_base） |

### 2.2 其他家族承接（与 modeling 相关行）

| 需求 | modeling 侧承接 |
| --- | --- |
| ARC-01/02/04 | 全部修改经①命令端口；不读其他插件控件/内存/私有文件；引用一律 ObjectId，不拼接名称前缀（R-4） |
| ARC-03 | modeling 保证"同修订→同 Description→同 CanonicalModel"的输入确定性；双编译原子性由 runtime 编译器＋project 事务承载（§9.1） |
| ARC-05 | 行程上限阈值、碰撞规则只读自 policy（④端口）；不持有影响计算的私有开关/默认值/重复算法；本地不设第二 4π 常量（NFR-MNT-07） |
| CON-01 | modeling 对象统一 ObjectId＋ContentVersion 包络；值带 SourcedValue/ValueProvenance（五类来源） |
| CON-02 | 旧结果保留为原快照历史证据——modeling 不删除对象库内容，"删除"＝引用移除（§4.8） |
| CON-03 | Verified/正式报告前外部资源按三段边界固化（一次性读取→外部引用记录→solidify），modeling 在草稿与导入报告中承载该状态（§6.7） |
| CON-05 | 切片粒度由对象分解支撑：电机成本（selection 域对象）不在任何 modeling 对象内，天然不失效运动学/轨迹（AT-05 方向②自动成立；方向① TCP 变更经工具对象失效下游） |
| CON-06 | 快照中的策略与 RuntimeNameMap 由 runtime/evidence 组装；modeling 只保证 robot-design 对象可确定性解码（§9.2） |
| PM-01 | 模板内容供新建向导消费（模板→草稿→创建确认）；URDF 项目不可修改基线修订、外部资源复制入资源区或登记外部引用（§6.7） |
| PM-04 | 保存（drafts 落盘，模块 token `modeling`）与应用（恰好一个新修订）分离；StaleRevisionRejected 时草稿以 `apply-retained` 保留（§9.3） |
| PM-12 | URDF 基线修订只读；建模编辑只发生在方案分支——modeling 提交时 `expectedRevision` 语义遵循 project（§9.3） |
| PM-18 | 项目级撤销/重做经逆命令产生新修订；与草稿局部撤销（编辑器内、零修订）分离（§9.3） |
| UX-02/03/06/07 | 界面工程用语（无哈希/Schema/插件名）；比较型诊断三要素齐全；状态词与图例统一（七态归 ui 投影）；表单级应用确认、会话显示操作不弹提示（§9.4 插件消费约定） |
| UX-04/08 | 求解器/种子等高级参数不归 modeling；modeling 面板只显示策略摘要与跳转（消费 ui `PolicySummaryCard` 契约），不设碰撞开关 |
| NFR-COR-01/02 | DH 转换、物性估算等算法有解析对照黄金数据集（§10.3）；同输入＋版本→等价结果、稳定排序（全部建模纯函数满足） |
| NFR-COR-03 | 非有限数、非法单位、引用缺失不得静默转 0 或默认通过——导入映射与就绪校验的第一戒律（§6.3、§8.2） |
| NFR-MNT-01/03/07 | 计算库零 Qt、可被模型测试直调；单位换算唯一走 core；无名称前缀操作、无策略副本、无影响计算的本地布尔开关 |
| NFR-SEC-01/02 | modeling 不自行读文件；路径/预算防护全部经 io（SA-14）；项目包/资源不逃逸项目资源区 |
| AT-01/04/05/15/16/17/18/27/28/29/30/31/33/37/38 | 观测点设计见 §10 故障注入矩阵（逐行标注 AT 依据） |

### 2.3 拥有／消费／不拥有

| 类别 | 内容 |
| --- | --- |
| **拥有** | RobotDesign 业务模型（对象 schema、canonical 编码、对象类型 token 登记）；机器人链/关节/连杆/基座/工具/场景/安装姿态编辑契约；参数化建模与模板创建；几何、质量、质心、惯量数据及其来源标记；DH 与显式变换模型的转换契约（含五状态判定）；URDF/Xacro/WorkCell 导入后的**业务映射**（字段映射、默认补全、忽略/不支持项报告、有损提取语义）；物性估算输入/输出与唯一公式表；模型就绪校验；建模命令处理器族（project `ICommandHandler` 实现）；面向 runtime 的 `IRobotDesignReader` 实现与 Description 构造；建模诊断码（`MDL-` 前缀）与模型变更摘要；**场景对象角色词表**（`SceneObjectRole`，policy.md §4.3 注明"归建模语义，本文消费"）；命名位姿参考数据（PoseSet 对象）；Model Diff 数据实体（MDL-08） |
| **消费** | core：身份、SourcedValue/ValueProvenance、单位换算、比较容差、诊断契约基类型、事件契约；project：①命令端口、HandlerContext、查询端口、DraftService、对象分配；io：SafePath/BudgetGuard、IResourceReader（字节/快照/依赖树）、IResourceSnapshotter（固化执行）、AtomicFile（MDL-20 导出）；runtime：RobotDesignDescription、编译器（经 project `IModelCompilePort` 间接触发）、RuntimeNameMap（只读查询经装配注入）；diagnostics：StableCodeRegistry、IDiagnosticFactory/IDiagnosticSink、ConfirmableFinding 服务；policy：④端口只读策略解析、IJointLimitEvaluator 比较型结果、（经 L5 的）CollisionScene 组装输入；ui：DraftController 接入、IPluginUiRegistrar/IPluginUiModule、就绪投影、命令注册、七态投影数据源；evidence：快照组装请求方角色约定（经 L5/评估器，无直接编译依赖） |
| **不拥有** | 项目目录、HEAD、分支、锁和事务（project）；模型编译与 RobWork 适配（runtime）；碰撞策略与评估实现（policy）；FK/IK、轨迹、动力学、传动虚功映射（kinematics/trajectory/dynamics/drivetrain）；TaskScheduler、RunRegistry、worker、检查点（execution）；结果当前性、证据 Profile 与工程判定（evidence/各评估域）；外部资源安全读取（io）；报告渲染（reporting）；UI 布局与阶段导航门控（ui/workflow）；估算质量**不构成**动力学证据（DYN-06 可信等级归 dynamics/evidence 口径） |

### 2.4 边界红线自查锚点（与 §14.5 自审清单对应）

| 红线（用户约束/架构） | 本卡保障位置 |
| --- | --- |
| 不重复 runtime/project/io/evidence/policy 职责 | §2.3 不拥有表；§6.2 三分表；§9.1 交接 |
| 不存在第二套模型真值 | §4.1 三形态辨析；§4.8 单一 canonical 存储 |
| 不把 UI 控件当模型真值 | §4.9 编辑态三态；会话姿态归 ui（KIN-06） |
| 不绕过 ProjectCommandService | §9.3 命令处理器族；导入仅产草稿 |
| 不重复实现基座—世界变换/名称解析 | §7.7；§9.1（唯一编译进 `R_world_base`；名称解析唯一在 runtime） |
| 模型未就绪 ≠ 工程不可行 | §8.2 状态正交表 |
| 编辑态对象不进入计算 | §9.1（评估只消费 RuntimeSnapshot）；Preview 限制声明 |
| 资源引用保护 | §4.10（I-MDL-9/10）、§6.7 三段边界 |
| 导入文件路径不作对象身份 | §4.8（身份=ObjectId＋内容摘要；路径仅 ExternalResourceRecord 记录字段） |
| 双权威冲突全部定义 | §7.2/§7.3（来源表＋冲突矩阵＋非法组合） |
| 不引入未经上游批准的新状态/阈值/语义 | §14.4（新增语义逐项登记：六轴模板默认值、位姿集对象、场景角色词表等均为 modeling 所有权的登记而非上游扩张）；§14.3 待裁决 |

### 2.5 旧代码（`old/src/rwslibs/robotmodelbuilder`）功能范围对照

口径声明（REQUIREMENTS 附录 A）：从头构建口径下旧代码**仅作功能范围对照**——本表把旧实现（28 个文件、约 1.84 万行：多页签建模 UI〔RobotModelBuilderWidget〕、RobotModelSpec 纯数据模型、URDF 导入器、XML 五件套写出器、WorkCell 反向转换器、发布/指纹/项目路径设施）逐项映射到需求 ID 与本卡设计落点；**不构成实现继承或正确性背书**，承接语义一律以需求条目与本卡章节为准，与旧行为不一致处均为需求驱动的架构演进并逐行注明。承接状态图例：✅＝等价/增强承接；♻＝架构重定位承接（能力保留、所有权或介质按新架构重新落位）；⏸＝分期承接（R2）；❌＝按上游裁决或需求范围不承接。

| 旧功能（来源） | 需求/处置（附录 A） | 本卡落点 | 承接状态 |
| --- | --- | --- | --- |
| 多页签建模 UI（Kinematics/Drawables/Limits/Poses/Dynamics/Scene/Preview） | MDL-01~07/15~17、AT-01｜等价承接 | §9.7（面板与界面逻辑）、§11 T15 | ✅ 功能承接（信息架构重组：页签→对象树＋属性投影＋阶段面板；v0.2 增补设计） |
| 位姿/DH 双模式切换（DH 为投影视图） | MDL-02/09/10、AT-16｜等价承接 | §7.1～§7.6 | ✅ 增强（旧简化投影公式＋lossy 标志→两阶段五状态＋编译链等价验证；旧"DH 反写真值"工具→仅 Exact/ExactNonUnique 经验证可切换权威） |
| 关节表编辑（增删/上移下移/六轴重置） | MDL-01、AT-01｜等价承接 | §5.1/§5.2、§9.4.1 | ✅ |
| 连杆几何 Drawables＋自动连杆圆柱（autoLink/regenerateLinkHelpers/computeLinkPose） | MDL-04、AT-01｜等价承接 | §4.3-B、§5.2 几何生成辅助（v0.2 增补） | ✅（辅助＝确定性占位原语生成，产物为普通几何引用） |
| 外部几何文件导入（stl/obj/dae/wrl/iv 选择） | MDL-04/PM-09、AT-15｜等价承接 | §6.7 资源三段边界＋io 格式族 | ✅（文件读取/识别/预算归 io，modeling 只持引用） |
| 独立碰撞模型表＋从 Drawables 生成碰撞模型 | MDL-04/ARC-05、AT-19｜等价承接 | §4.3-B `shape.collision`、§5.2 引用复制式辅助（v0.2 增补） | ✅（碰撞**几何**引用归 modeling；碰撞**判定**仍唯一归 policy） |
| CollisionSetup 排除对（基座-首关节/相邻/静态三开关＋手动对＋Manual/Auto/Imported 来源标记） | MDL-04/ARC-05、AT-19｜等价承接 | §6.3（SelfCollisionHints→策略草稿候选输入）、P-MDL-3 | ♻（策略权威归 policy：modeling 产建议清单，自动排除规则在策略解析入口应用） |
| ProximitySetup（includeAll/静态排除/通配规则表） | 上游未设对应需求条目 | — | ❌ 不承接（判定规则归 policy 工程策略；如需走需求变更立项） |
| 场景帧/场景几何（层级 refFrame、Fixed/Movable/DAF、RPY/4×4 双位姿模式） | MDL-15、AT-28｜等价承接 | §4.5 | ✅/➖（世界系固连＋role 词表承接；**层级挂载与 DAF 不承接**——首版静态场景需求未要求，需则走需求变更；RPY/4×4 由统一 Transform3D 表示＋显示投影取代） |
| 限位/力限值/质量/材料估算（含 RobWork EstimateInertia 开关） | MDL-05/16、AT-01｜等价承接 | §5.3/§5.4 | ✅ 增强（唯一公式表＋来源标记＋断言分域；估算收归建模公式表，不再调用 RobWork 估算——DTB §4.6 基线库使用原则） |
| URDF 导入（仅单链＋fixed frame；package 根解析；缺失网格策略 Fail） | MDL-03/11、AT-15｜等价承接 | §6.1～§6.3 | ✅ 大幅增强（io 安全边界＋维度一/二链型判定＋分支报告＋待确认草稿取代"仅单链"限制；缺失网格 Fail→Recorded 引用＋告警＋转正式固化门禁） |
| WorkCell 反向同步提取（设备/关节/限位/Q 构型/场景/碰撞提取＋侧车融合＋目标设备选择） | MDL-18、AT-33（R2）｜分期承接 | §6.6（边界设计） | ⏸ R2（目标设备选择对应分支报告＋显式选链；侧车融合对应 MDL-20 自有工件识别） |
| 生成预览/保存 XML 五件套＋保存并加载（失败回滚） | MDL-06/20、AT-28｜等价承接 | §6.8、§9.1、§9.7 预览页 | ♻（XML 由 runtime 编译产生，导出为外供副本；**编辑态即时 XML 预览有意不承接**——草稿不编译（D-MDL-10），对应物＝基于已应用修订的导出预览；"保存并加载"由修订事件→快照刷新取代） |
| DWC 生成开关 | MDL-06、AT-01｜等价承接 | §9.1（`CompileOptions.requestDynamicWorkCell`＋能力门控 SkippedNoPhysics） | ✅ |
| 导出 DHJoint XML（Advanced，无损条件检查） | MDL-02、AT-16｜等价承接 | §6.8（规范包保留 DH 权威语义）；WC/DWC XML 内容形态归 runtime 适配层 | ♻ |
| .rmb.json 侧车＋项目路径可移植化（makePortable/resolveManaged） | 附录 B 裁决排除（侧车格式）；PM/CON | §4.2 对象库、§4.8（路径不作身份） | ❌→✅ 按上游裁决以对象库＋ObjectId＋内容摘要取代 |
| 发布服务（publishAndLoad） | PM/CON、AT-20｜等价承接 | §9.3 命令＋修订事件 | ♻（"发布并加载"→应用命令＋双编译＋修订事件驱动视图刷新） |
| 模型指纹 Current/Stale（canonicalSha256） | CON-02/05、AT-05｜附录 B 裁决排除（哈希链→修订＋当前性） | §4.8（ContentVersion 由 project 计算） | ❌→✅ 按上游裁决 |
| 项目文档集成（加载/保存/脏快照比对/恢复/清理上下文） | PM-04/08、AT-21｜等价承接 | §4.9、§9.3（DraftService＋ui DraftController） | ✅ |
| 三维选择联动（Drawable 高亮/打开场景同步） | MDL-07、AT-01｜等价承接 | §9.7.2 L-1 选中联动数据流 | ✅（拾取契约归 ui View3D，建模侧供 ObjectId 投影与高亮目标） |
| 插件动态加载元数据（Q_PLUGIN_METADATA/动态卸载） | NFR-SEC-04｜附录 B 裁决排除 | §3.1/§3.2（静态白名单＋IPluginUiRegistrar） | ❌→✅ 按上游裁决 |

**对照结论**：需求级功能**全覆盖**——等价/增强承接 14 项、架构重定位 4 项、分期（R2）2 项、按上游裁决或需求范围不承接 4 项，与附录 A 处置三分类一致；两项有意不承接（编辑态即时 XML 预览、场景层级挂载/DAF）均注明理由与替代语义/需求变更通道；实现层面旧代码零拷贝（从头构建口径不变）。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 单元组成（ARCHITECTURE §3.3 二分结构）

```
sdurws_ird_modeling            计算库（L2 等效，零 Qt，STATIC）：
                               RobotDesign 值模型与 canonical 编解码、模板工厂、URDF/Xacro/WorkCell
                               业务映射、物性估算、DH↔显式转换、就绪校验、Model Diff、
                               规范模型包导出/导入、IRobotDesignReader 与命令处理器实现
                               ——可被模型测试（_test/_contract_test）直接调用（NFR-MNT-01）
sdurws_ird_modeling_plugin     插件界面（L4，Qt Widgets）：
                               关节树/参数表/三维选择联动（MDL-07）、编辑器面板、导入向导域侧页、
                               就绪诊断呈现；只消费计算库公共接口＋ui 端口＋只读投影，
                               不持有计算逻辑与业务判定（面板信息架构与界面逻辑见 §9.7）
sdurws_ird_modeling_test       单元测试（googletest，经 find_package(GTest CONFIG REQUIRED)）
sdurws_ird_modeling_contract_test  跨单元契约测试（project/runtime/io/policy/diagnostics 对端面）
```

- 命名空间：`sdurws::ird::modeling`（沿用平台单元单层命名约定；测试 `sdurws::ird::modeling::test`）。
- 命名风格：类型/函数 `PascalCase`、局部变量 `camelCase`、成员前缀 `m_`，与 core/io 既有代码一致；公共头 include guard `<PROJ>_<Path>_HPP` 风格（如 `IRD_MODELING_ROBOTDESIGN_HPP`）。
- C++17 显式（`CXX_STANDARD 17`，不用 C++20）；MSVC `/utf-8`；第三方一律经 vcpkg 经典模式（本单元新增 XML 解析依赖建议见 P-MDL-5，未经 DTB 登记前不得引入）。
- 无 `_worker` 目标：建模命令在主进程命令执行线程串行执行（ARCH §4.2），modeling 无进程外计算入口；R2 如需批量导入物性估算等工作进程化，另行走需求/设计变更。

### 3.2 目标升级与门禁边登记（WP-13-T02 执行清单）

- `modeling/CMakeLists.txt` 新建：`sdurws_ird_modeling` 由上级 INTERFACE 占位升级为 **STATIC**（目标名与别名不变，DTB §5.1）；同文件注册 `_test`/`_contract_test`（沿用 core/io 自持 `ird_add_gtest` 宏形态：add_test＋`<target>_report` XML）；`_plugin` 目标随 WP-13-T15 落位（Widgets，`AUTOMOC`，仅插件目标链接 Qt）。
- 配置期红线守卫随文件自持（参照 core/CMakeLists.txt 末尾 foreach：链接到任何 `^sdurws_ird_<非本单元>` 业务目标→FATAL_ERROR；任何 Qt→FATAL_ERROR——R-1/R-3）。
- `ird_gates` 白名单（`cmake/ird_gates_whitelist.cmake` 的 `IRD_ALLOWED_UNIT_EDGES`）原无任何 modeling 出边；本卡声明建模所需依赖边如下，**已随 WP-13-T02 构建落位登记**（`IRD_ALLOWED_UNIT_EDGES` 增六行；因 `traceability/dependency-graph.json` 的机器镜像刷新归 traceability 维护任务，六边经 `IRD_EXTRA_EDGE_REFS` 成对登记出处——门禁第 6 步一致性核对通过；属 §3.5"各业务域单元→L2/L3 公共接口"既有许可方向的实例化，非新增架构边）：

| 边（modeling →） | 形态 | 用途 |
| --- | --- | --- |
| core | 接口依赖 | 身份/SourcedValue/单位/比较/诊断契约/事件 |
| diagnostics | 接口依赖 | 稳定码注册、IDiagnosticFactory/IDiagnosticSink |
| project | 接口依赖 | ICommandHandler/CommandPlan/HandlerContext/DraftService/查询端口 |
| runtime | 接口依赖 | RobotDesignDescription/IRobotDesignReader/资源引用类型 |
| policy | 接口依赖 | EngineeringPolicySet 只读解析、IJointLimitEvaluator |
| io | 接口依赖 | SafePath/BudgetGuard/IResourceReader/ResourceSnapshot/AtomicFile 值与服务类型 |

- 插件目标 `sdurws_ird_modeling_plugin` → `sdurws_ird_modeling`＋`sdurws_ird_ui`（＋上述平台库按需）；**不得链接任何其他业务域单元目标（R-1）**；不得 include 其他单元私有头（R-2）。

### 3.3 公共头文件布局（`modeling/include/sdurws/ird/modeling/`，随 §11 任务逐个落位）

| 头 | 内容 | 任务 |
| --- | --- | --- |
| `ObjectTypes.hpp` | 对象类型 token 常量、`SceneObjectRole` 词表、对象 schema 版本常量 | T02/T03 |
| `RobotDesign.hpp` | RobotDesign 聚合值模型（关节/连杆/基座/权威模式/引用表）、合法实例不变量 | T03 |
| `Parts.hpp` | ToolDefinition/SceneObject/PoseSet/DrivetrainDesign 值模型 | T03/T10 |
| `Codec.hpp` | `IRobotDesignCodec`：建模对象 canonical 编码/解码（确定性序列化登记，core §6.3 分工） | T03 |
| `Template.hpp` | `IRobotDesignTemplateFactory`、模板描述符、六轴默认参数表（§5.1）、逐轴编辑流＋基座安装姿态编辑流（§5.2——T11 编辑流落位于本头而非 `Parts.hpp`：basePlacement 是根对象字段，编辑域内核与逐轴编辑流同址；原表 Parts.hpp 行的 T11 归属订正见 §15 v0.13 ④） | T07/T11 |
| `PropertyEstimation.hpp` | `IPropertyEstimator`、唯一公式表（版本 `mdl-property-formula/1`）、材料密度默认表 | T04 |
| `Import.hpp` | `IModelImportMapper`、URDF/Xacro/WorkCell 映射值类型与导入报告 | T05/T06/T17 |
| `XacroExpand.hpp` | `IXacroExpandService`（受控展开语义；护栏经 io） | T06 |
| `DhConvert.hpp` | `IDhExplicitConverter`、DH 参数结构、五状态判定结果 | T09 |
| `Readiness.hpp` | `IModelReadinessChecker`、分层检查与 `ModelReadinessReport` | T08 |
| `CanonicalBridge.hpp` | `ICanonicalModelInputBuilder`＋`RobotDesignReader`（runtime `IRobotDesignReader` 实现） | T12 |
| `CommandHandlers.hpp` | `IModelingCommandHandler` 基类与五命令处理器族 | T08 |
| `ModelDiff.hpp` | `IModelDiffService`、差异增量表数据实体（MDL-08） | T14 |
| `Package.hpp` | `IModelPackagePort`：规范模型包导出/导入（MDL-20；T13 落位——导出/导入结果以 `PackageExportOutcome`/`PackageImportOutcome` 承载〔§9.4.9 行"ExportOutcome/ImportOutcome"的落位命名：Import.hpp `ImportOutcome` 已占用，等价命名登记见 §14.6 v0.15 ②〕；包格式常量与条目布局随头登记；WC/DWC XML 外供编排面 `exportWorkCellXml` 同头承载——§6.8 第二条） | T13 |
| `Errors.hpp` | `ModelingError`/`ModelingErrorCode` 与域错误→诊断码映射数据 | T02 |
| `DiagCodes.hpp` | modeling 稳定码清单（`CodeDescriptor` 工厂数据，§9.5） | T02 |
| `README.md` | 已存在保留位（指向本卡） | — |

私有实现头（`src/` 内部）不跨单元暴露（R-2）。

### 3.4 线程与确定性总约定（各接口详细契约的公共前提）

1. 计算库中**纯函数类服务**（Codec/Template/Estimator/Mapper/XacroExpand/DhConvert/ReadinessChecker/Diff/Package/CanonicalBridge）：无共享可变状态、可重入、多线程并发调用安全；**确定性**：不读环境变量/时钟/locale/文件系统随机性，同输入字节→同输出字节（NFR-COR-02）。
2. `IRobotDesignEditor`（编辑器）**非线程安全**：仅 UI 线程访问（会话对象，TASK-02 语义外的 UI 约束）。
3. 命令处理器：由 ProjectCommandService 在**命令执行线程**串行调用（全局唯一执行槽，project.md §5.3.1），处理器内部无需再加锁；处理器不得回调命令服务写入口（project §5.3.3 同源约束）。
4. 一切物理量在建模对象内以 **SI（m/rad/kg/s/N·m）** 存储；导入/显示边界的单位换算唯一经 core `Units.hpp`（SA-12）；角度制式 rad（注释显式标注，AGENTS §2.5）。

---

## 4. RobotDesign 数据模型

### 4.1 三种模型形态辨析（单一真值的边界）

```
RobotDesign（权威参数化，业务编辑模型，modeling 拥有 schema 与 canonical 编码）
   │  编辑态（内存工作集，无身份承诺）      ←评估器不得消费
   │  草稿态（DraftDocument 落 drafts/，PM-04） ←无内容身份承诺，不是快照（evidence.md §4.1.1）
   │  已应用修订态（objects/ 不可变对象＋修订闭包，CON-01） ←唯一可计算形态
   ▼  runtime 确定性编译（唯一编译者，ARC-03）
CanonicalModel（规范模型，runtime 拥有；基座—世界变换唯一存储 R_world_base，M-11）
   ▼  runtime 运行时适配
WorkCell / DynamicWorkCell 实例（编译派生对象，随 RuntimeSnapshot 值持有，modeling 不持有）
```

- **RobotDesign 不是"内存里的第二真值"**：编辑态工作集是已应用基线＋未应用编辑差值的演算视图，其权威落点仍是（a）已应用修订的对象库字节（基线）与（b）草稿载荷（未应用部分）；同一编辑结果在对象库中只有一份 canonical 字节。
- **CanonicalModel 与 WorkCell 是派生物**：modeling 不缓存、不复算、不持有可变 WorkCell（§9.1）；UI 三维预览消费的是已应用修订的 RuntimeSnapshot（经 ui/runtime 只读视图），**未应用草稿不提供三维编译预览**（R1 非目标——避免私设第二编译路径；草稿预览限于参数表/树/就绪诊断）。
- 命名位姿（MDL-17）与显示名等参考数据**不属于权威运动学语义**：存放于独立对象、不进入 Description（§4.6），"不改变权威模型"由对象分解保证而非字段标记。

### 4.2 对象分解与对象类型登记

modeling 拥有的持久化对象类型（objectTypeToken，登记于 `ObjectTypes.hpp`；根对象 token 与 runtime 已落位常量 `kRobotDesignObjectType="robot-design"` 字面一致，**不得另设第二常量**）：

| objectTypeToken | 对象 | 内容概要 | 独立成对象的理由 |
| --- | --- | --- | --- |
| `robot-design` | RobotDesign 根/聚合 | 权威模式、链与关节/连杆权威参数、基座安装、默认 TCP、工具/场景/位姿集/传动引用表、资源清单引用、schema 版本 | 编译入口对象（runtime S2 以此定位，`builtFrom`＝其字节 SHA-256） |
| `tool-definition` | ToolDefinition（每工具一个） | 安装接口、TCP 列表、几何/物性（MDL-13） | 任务/负载经引用使用、不复制工具几何（MDL-13）；TCP 变更须精确失效运动学及下游（AT-05①） |
| `scene-object` | SceneObject（每对象一个） | 固定帧/环境几何、世界系位姿、角色（MDL-15） | 被 REQ-04 等跨域引用；场景变更不失效纯运动学切片 |
| `named-pose-set` | PoseSet（每模型一份） | 命名位姿/Home 参考（MDL-17） | 纯参考数据：变更不触发重算、不进任何计算切片 |
| `robot-drivetrain` | DrivetrainDesign（每模型一份） | 传动比、耦合矩阵（R2）、摩擦、力矩限值、选型回填字段（MDL-16/21、SEL-10 目标） | selection 回填与 OPT StageB 传动比编辑的独立写对象；力矩/摩擦仅动力学消费（evidence.md §5.3 失效矩阵行） |

连杆物性与几何**内嵌于根对象**（不逐连杆拆对象）——取舍依据：evidence.md §5.3"几何与物性是不同对象（MDL-05 分层）"的失效粒度诉求，与对象爆炸/闭包膨胀的代价权衡后，R1 采用**字段级分层（SourcedValue 逐项来源）＋对象级保守失效**：几何或物性任一变更都会更换根对象 cv，从而保守地把模型相关切片整体标 Superseded（历史 payload 不变，CON-02）。该行措辞与对象级分解的偏差登记为 P-MDL-2（§14.3，建议 evidence 侧确认为字段级语义或 modeling 侧后续按需求变更拆分对象）。

**修订闭包与引用**：根对象仅持部件**引用表 `{objectId}`（不含 cv）**；修订闭包由 project 累积组合（RevisionView.objectRefs 全量引用集），runtime S2/S5 据此复核 CM-0。由此：位姿集/工具/场景/传动的独立修订**不改写根对象字节**，失效范围由各评估器声明的 DependencyKey 决定（evidence.md §4.2.1），实现"位姿集变更不失效任何计算切片"等精细行为。

### 4.3 RobotDesign 根对象字段表

所有取值字段一律 `core::SourcedValue<T>`（四态 Provided/NotProvided/NotApplicable/Invalid＋ValueProvenance，core.md §4.3）；单位 SI；位姿 `rw::math::Transform3D<double>` 记 `T_ab`＝"b 系相对 a 系"（core.md §4.6 约定）。

| 字段 | 类型 | 单位/约束 | 来源（ValueProvenance） | 可变性与引用约束 |
| --- | --- | --- | --- | --- |
| `schemaVersion` | `std::uint32_t` | ≥1；与 `descriptionContractVersion`（runtime Description 契约版本，入 CanonicalModel 身份与缓存键）同源演进 | —（结构字段） | 只增不减；演进规则见 §9.1 与风险 R-MDL-3 |
| `displayName` | `std::string` | 仅呈现（UX-02：不进编译身份，runtime §4.3.6 等价规则） | UserProvided | 可变；变更产生修订但不改变 Description（不触发重算内容，见 P-MDL-2 粗粒度注记） |
| `authority` | `AuthorityMode { Explicit, StandardDH }` | 互斥（MDL-02）；切换受 §7.6 条件约束 | UserProvided | 可变；StandardDH 态下 axis/origin 为派生只读 |
| `basePlacement` | `BasePlacement { preset: InstallationPresetToken(ground|inverted|wall|custom); customEaa: SourcedValue<rw::math::Vector3D<double>>（custom 必填，rad）; basePosition: SourcedValue<rw::math::Vector3D<double>>（m）}` | 预设轴向以 runtime `BaseWorldTransform.hpp::installationPresetRotation()` 为唯一权威产出点（P-RT-4：倒挂=R_x(π)、壁装=R_y(π/2)；modeling 只存参数不存矩阵，见 §7.7） | UserProvided / ImportMapped | 可变；未显式配置＝地面（默认值在**模板/导入映射层**填入并带来源标记，MDL-22 V15-04） |
| `joints[]` | `JointEntry`（表 4.3-A），串联有序，≥1 | — | — | 增删经编辑命令；`objectId` 创建时一次分配、跨修订稳定 |
| `links[]` | `LinkEntry`（表 4.3-B），`links.size()==joints.size()+1`（runtime `StructureInvalid` 同口径） | — | — | 同上 |
| `defaultTcp` | `std::optional<TcpRef{ toolOid: ObjectId; tcpKey: std::string }>` | 有 tools 时须已设置（runtime `defaultTcp` 必填口径，KIN-14） | UserProvided | 可变；引用的 toolOid 必须在 tools 引用表内（I-MDL-9） |
| `toolRefs[]` | `std::vector<ObjectId>`（指向 `tool-definition` 对象） | 无重复 | — | 引用保护见 §4.10 |
| `sceneRefs[]` | `std::vector<ObjectId>`（指向 `scene-object` 对象） | 无重复 | — | 同上 |
| `poseSetRef` | `std::optional<ObjectId>` | 至多一份 | — | 位姿集变更不改根字节（§4.2） |
| `drivetrainRef` | `std::optional<ObjectId>` | — | — | 回填目标（SEL-10）与 StageB 传动比编辑对象 |
| `resourceManifest[]` | `std::vector<ResourceRef{ resourceId: std::string; contentDigest: core::Digest256; state: Recorded|Solidified; solidifiedObject: optional<(ObjectId, ContentVersion)>; externalRecord: optional<ExternalResourceRecord{absPath, recordedDigest}> }>` | 路径不入 CanonicalModel 身份（runtime §4.3.5）；`contentDigest` 是身份要素 | ImportMapped / UserProvided | 固化（Solidified）后不可逆回 Recorded；状态流转归 §6.7 |
| `notes` | `std::string` | 纯备注，不入编译身份 | UserProvided | 可变 |

**表 4.3-A `JointEntry`**：

| 字段 | 类型/单位 | 权威模式下的地位（MDL-02/09） |
| --- | --- | --- |
| `objectId` | `core::ObjectId`（创建时一次分配，经 `HandlerContext.objectId()`；稳定可追溯，ARC-04） | 两模式不变；作为诊断 `subjectObjectId` 与未来名称映射锚 |
| `localName` | `std::string`（运行时名称 localName 源，合法字符集 `[A-Za-z0-9_.-]` 以 runtime 消歧规则兜底） | 改名＝新内容（runtime §4.3.6：localName 是解析字段，重命名＝新身份） |
| `type` | `JointType { Revolute, Continuous, Prismatic, Fixed }` | 两模式均为权威；类型保留不回写（V12-01）；R1 正式链限全旋转（§6.4） |
| `axis` | `SourcedValue<rw::math::Vector3D<double>>`，单位向量（无量纲；模长偏差按附录 D C7 产品校验：以 1×10⁻¹² 无量纲绝对容差归一化） | **Explicit：可编辑权威一等字段（MDL-09）；StandardDH：只读派生** |
| `origin` | `SourcedValue<rw::math::Transform3D<double>>`＝`T_parent_joint`（m/rad） | 同上（Explicit 权威/DH 派生） |
| `zeroOffset` | `double`，rad（转动）或 m（移动）；`q_authoritative = q_zeroOffset + q_rw`（runtime 口径） | 两模式均为权威（零位语义不随权威模式切换） |
| `bounds` | `SourcedValue<std::pair<double,double>>{qmin,qmax}`，rad/m；Revolute/Prismatic 必填且 qmin<qmax（硬断言 MDL-06④）；Continuous＝NotApplicable | 两模式均为权威 |
| `workingRange` | `SourcedValue<std::pair<double,double>>`，rad；仅 Continuous，必须有限区间 qmin'<qmax' | 分析消费属性，**不回写 bounds**（MDL-12/V12-01） |
| `dhDerived` | `std::optional<DhParameters>`（θ 偏置/d/a/α，rad·m） | 仅 StandardDH 态填写＝权威；Explicit 态下如存在则为只读派生展示值（不参与编码身份，见 §7.2） |

**表 4.3-B `LinkEntry`**：

| 字段 | 类型/单位 | 说明 |
| --- | --- | --- |
| `objectId` / `localName` | 同关节 | — |
| `body.mass` | `SourcedValue<double>`，kg；已提供则 m>0（断言①） | 缺失＝NotProvided，不触发断言，走 DataInsufficient 降级（MDL-06/V15-01） |
| `body.centerOfMass` | `SourcedValue<rw::math::Vector3D<double>>`，m，连杆系下 | 修改触发平行轴确认流（§5.3，M-2） |
| `body.inertia` | `SourcedValue<InertiaTensor{ixx,iyy,izz,ixy,ixz,iyz}>`，kg·m²；基准＝**质心、参考姿态＝连杆坐标系**（M-2）；已提供则对称（相对 1×10⁻¹²，附录 D 第 6 项）＋SPD（对称化后特征值严格>0，第 7 项）＋惯性椭球三角不等式（断言②③） | 同上 |
| `body.material` | `std::optional<MaterialRef{ materialId, densitySourced }>`, kg/m³ | 估算输入（§5.3） |
| `shape.visual` / `shape.collision` | `std::optional<GeometryRef{ resourceRefId（→resourceManifest）, localTransform（T_link_geom，m/rad）, kind: Mesh|Primitive }>` | 视觉/碰撞分离字段（evidence 失效矩阵的语义基础）；碰撞几何不等于碰撞判定（policy 唯一评估） |
| `selfCollisionHints` | `std::optional<SelfCollisionSetup{ excludedPairs: vector<pair<linkName,linkName>> }>` | **仅导入映射的中间产物**：不入根对象编码权威语义，经导入报告转策略草稿输入（§6.3/P-MDL-3） |

### 4.4 ToolDefinition（`tool-definition` 对象，MDL-13）

| 字段 | 类型/单位 | 约束 |
| --- | --- | --- |
| `objectId` / `localName` / `displayName` | 同前 | 被任务/负载经 ObjectId 引用，不复制几何（MDL-13） |
| `mountInterface` | `T_flange_tool: Transform3D`（m/rad） | 法兰系 {F} 下工具安装接口 |
| `tcpList[]` | `{ key: std::string; offset: T_tool_tcp（m/rad）；displayName }` | ≥1；`defaultTcp` 经 (toolOid, tcpKey) 引用其一 |
| `geometry` | `GeometryRef`（同连杆 shape） | — |
| `mass / centerOfMass / inertia` | SourcedValue，kg / m / kg·m²；断言①②③同连杆 | — |
| `payloadAttributes` | `std::optional<{ ratedLoad kg, ... }>`（登记保留，语义归 requirements 负载工况） | 不进 Description |

### 4.5 SceneObject（`scene-object` 对象，MDL-15）

| 字段 | 类型/单位 | 约束 |
| --- | --- | --- |
| `objectId` / `localName` | 同前 | 显式引用进入项目（MDL-15） |
| `worldPose` | `Transform3D`（**世界坐标系固连**，m/rad；不得预乘安装旋转——runtime §4.3.4 同口径，M-11） | — |
| `geometry` | `GeometryRef` | — |
| `role` | `SceneObjectRole { RobotLink, Tool, Payload, EnvironmentObject, Workpiece }` | **词表所有者＝modeling**（policy.md §4.3 注记"场景对象角色词表归建模语义，本文消费"）；策略作用域消费该 token |
| `collisionProfileHint` | `std::optional<std::string>`（导入的自碰撞分组提示，仅报告用途） | 不改变策略（策略权威归 policy） |

### 4.6 PoseSet（`named-pose-set` 对象，MDL-17）

- 条目：`{ key: std::string; jointConfiguration: vector<double>（rad/m，与关节序一一对应）; note: std::string }`；`homeConfiguration`/`zeroConfiguration` 两个保留键作为编辑器"复位 Home/Zero"会话命令的目标参考（UX-13/ KIN-06：复位只改 ui 会话姿态，不产生修订）。
- **不进 Description、不进任何评估器依赖键**：位姿仅作会话与检查参考（MDL-17）；其修订不触发重算。消费方为 ui 会话层与 MDL-20 导出（语义保留范围含命名位姿）。

### 4.7 DrivetrainDesign（`robot-drivetrain` 对象，MDL-16/21、SEL-10 目标）

| 字段 | 类型/单位 | 约束与阶段 |
| --- | --- | --- |
| `ratioPerJoint[]` | `vector<SourcedValue<double>>`（无量纲，逐可动关节；>0 且有限） | R1 可编辑（OPT StageB 连续变量，V12-02）；回填来源 `CatalogBackfill`（SEL-10） |
| `coupling` | `std::optional<CouplingDesign{ C: 矩阵（常矩阵，行×列＝适用关节窗口）; jointRange: [i,j]; conditionNumber: double }>` | **R1＝禁止配置**（存在即阻断＋诊断，MDL-12/21 R1 口径）；R2/阶段 D 经 MDL-21 启用线性耦合（§8.1） |
| `frictionPerJoint[]` | `vector<{ viscous fv: N·m·s/rad（移动：N·s/m）；coulomb fc: N·m（N）；bias: N·m（N）}>`，全 SourcedValue | 未填写→NotProvided→DataInsufficient 降级（DYN-06/M-8 闭环）；进入 Description `friction`（runtime §4.2） |
| `torqueLimitsPerJoint[]` | `{ rated, peak }` SourcedValue，N·m（转动）/N（移动） | 不进 CanonicalModel（消费方 SEL/DYN）；进入驱动工作点评估的输入 |
| `catalogBackfill` | `std::optional<{ catalogVersion: string; motorKey/reducerKey: string; mounting: string }>` | SEL-10 回填字段（阶段 C 由 selection 命令写入；schema 本卡登记） |

### 4.8 身份、版本与引用约束

- **身份**：每个对象一个 `core::ObjectId`（`obj-<32hex>`）；关节/连杆的子 ObjectId 在创建时经 `HandlerContext.objectId()` 分配并**内嵌于根对象字节、跨修订稳定**（它们不是独立存储对象——与 runtime v0.12②"四子结构 objectId ∈ objectRefs"的口径差异登记为 P-MDL-1：本卡主张 objectRefs 复核范围限于真实存储对象〔根/工具/场景/位姿集/传动/资源〕，关节/连杆子 ID 属模型内标识，§14.3）。
- **版本**：对象字节由 modeling canonical 编码（`Codec.hpp`，确定性序列化：字段定序、小端、UTF-8、无填充、集合按 ObjectId 规范文本字典序）；`ContentVersion`（`cv-<64hex>`）由 **project 计算**（SHA-256，project.md D-10——处理器不自行申报）。
- **改名语义**：`localName` 改名→根/部件对象字节变化→新 ContentVersion→CanonicalModel 新身份（runtime §4.3.6）→按切片依赖失效（正确行为：名称进入 RuntimeNameMap，CON-06）；`displayName`/`notes` 改名→同样产生新修订，但 Description 不变（编译缓存可复用）。
- **"删除"＝引用移除**：对象库不可变、只增不改（PA-2/CON-02）；"删除工具/场景对象"＝从根对象引用表移除引用（历史修订闭包中的旧版本永久保留）。被本域引用（如 defaultTcp 指向该工具）时移除被拒（I-MDL-9）；跨域引用（requirements→工具 ObjectId）无法由 modeling 直接检查（R-1 禁互链），处置：移除命令摘要附"可能存在外部引用"提示＋⑤事件通知，悬空检测归 requirements 就绪校验（§12 交接 P-MDL-6）。
- **导入文件路径不是身份**：路径仅出现在 `ExternalResourceRecord`（外部引用记录，NFR-SEC-01 口径）与诊断 `context` 文案中；对象身份只有 ObjectId＋内容摘要（§2.4）。

### 4.9 编辑态／草稿态／已应用修订（三态辨析）

| 维度 | 编辑态（内存工作集） | 草稿态（drafts/） | 已应用修订态（objects/＋修订闭包） |
| --- | --- | --- | --- |
| 载体 | `IRobotDesignEditor` 工作集（基线闭包＋编辑差值） | `DraftDocument{ moduleId="modeling", baseRevisionId, payload canonical, externalRefs[], origin }`（project §4.4.5） | 不可变对象＋修订记录 |
| 身份承诺 | 无（不得冒充快照，evidence.md §4.1.1） | 无内容身份承诺；有 baseRevision 基线锚 | ObjectId＋ContentVersion 完整包络（CON-01） |
| 消费者 | 仅本插件面板与就绪校验 | 打开恢复（PM-04）、跨模块草稿汇总（ui） | runtime 编译、各域评估（唯一可计算形态） |
| 撤销 | 编辑器局部撤销（零修订，ui `undoLocalEdit` 族） | 丢弃/保留草稿（`DraftService.discard`/`apply-retained`） | 项目级撤销＝逆命令新修订（PM-18，§9.3） |
| 失效 | 不触发任何失效 | 不触发 | 修订事件→切片当前性重算（§7.6 管线） |

### 4.10 合法/非法实例与不变量

合法实例不变量（`RobotDesign.hpp` 静态断言层，违例即调用方错误 fail-fast 或阻断应用）：

- **I-MDL-1（结构）**：`joints.size()>=1`；`links.size()==joints.size()+1`；链单串联无环（"循环层级"非法——父指针唯一、无回路）；Fixed 关节可存在于链上但 R1 正式链的可动主链限定见 §6.4。
- **I-MDL-2（身份唯一）**：同模型内 ObjectId、localName（同作用域）不得重复；重复＝调用方错误（构造/编辑边界拒绝，不静默重命名——NFR-COR-03）。
- **I-MDL-3（单位合法）**：一切 SourcedValue 在 Provided 态下数值有限（NaN/Inf＝非法，不得静默置 0）；单位换算只经 core（非法单位→`CoreError`，不降级）。
- **I-MDL-4（限位有序）**：Revolute/Prismatic 提供限位时 qmin<qmax（硬断言④前半）；Continuous＝bounds NotApplicable 且 workingRange 有限区间。
- **I-MDL-5（物性合法）**：已提供的 m>0、惯量对称（相对 1×10⁻¹²）＋SPD（严格>0）＋三角不等式；缺失不阻断（降级路径）。
- **I-MDL-6（轴有效）**：可动关节 axis 非零、有限、可归一化；零轴/非有限轴＝非法（导入侧按 MDL-11 仅报告，见 §6.3）。
- **I-MDL-7（基座合法）**：custom 预设必填 customEaa 且旋转矩阵正交（容差 1×10⁻¹²，runtime `InputInvalid` 同口径）；preset≠ground 而 R=I 的组合在映射层拒绝（§7.7）。
- **I-MDL-8（权威互斥）**：StandardDH 态下 joints[].axis/origin 必须为派生值（DerivedReadOnly 来源）；编辑权威派生字段＝调用方错误（§7.3 冲突矩阵 C-1）。
- **I-MDL-9（引用完整）**：defaultTcp.toolOid∈toolRefs；toolRefs/sceneRefs 无重复、指向对象存在于闭包且 token 匹配；移除被引用对象→拒绝＋比较型定位诊断。
- **I-MDL-10（资源状态机）**：Recorded 资源必须带 externalRecord（absPath＋recordedDigest）；Solidified 必须带 solidifiedObject；状态只可 Recorded→Solidified（CON-03 固化单向）。
- **I-MDL-11（传动）**：ratio 有限>0；R2 下 C 须方阵、可逆、条件数 ≤1×10⁸（P-RT-7 设计默认，§8.1；R1 下 coupling 禁止配置见 I-MDL-12）。
- **I-MDL-12（R1 耦合阶段锁）**：coupling 不得配置（R1 存在即阻断——原本条在 I-MDL-11 行以括注定义"R1 下 coupling 不得配置（I-MDL-12）"，随 WP-13-T03 实现落位显式化补行；稳定码 `MDL-21-COUPLING-STAGE-LOCKED` 仍随 T18/R2 注册，§9.5 纪律不变）。
- **I-MDL-13（工具 TCP 完整）**：`tcpList` ≥1 且 TCP 键非空、集合内唯一（§4.4 tcpList 行约束"≥1；defaultTcp 经 (toolOid, tcpKey) 引用其一"的编号化——KIN-14 引用锚的构造侧保证；随 WP-13-T10 实现落位补行，枚举/谓词表尾追加）；经 Codec 解码门校验链④自动强制（命令载荷与存储字节双通道共用同一判定）。

非法实例处置原则：**构造/编辑边界 fail-fast（调用方错误）**；**应用（命令）边界阻断＋精确定位诊断（环境/数据错误走 MDL- 稳定码）**；任何非法值不得被静默修复、截断或默认填充（NFR-COR-03）。

## 5. 模板、参数化和物性估算

### 5.1 模板创建（MDL-01、PM-01、§2.1 支持矩阵）

模板清单（`IRobotDesignTemplateFactory::listTemplates()` 返回；数值冻结状态逐项登记）：

| 模板 | 轴数 | 权威模式 | 安装预设选项 | 状态 |
| --- | --- | --- | --- | --- |
| 六轴通用串联（generic-6r） | 6×Revolute | Explicit | ground/inverted/wall | **R1 可用**；默认参数表 T-MDL-1（下）为设计默认值（非上游冻结需求值），随 WP-13-T16 黄金数据集 `mdl-template-6r` 数值锁定 |
| 七轴串联（generic-7r） | 7×Revolute | **Explicit（MDL-02：七轴默认且 S-R-S/带偏置拓扑锁定显式）** | 同上 | **登记不启用**（P-03 七轴模板工程数值未冻结，附录 C；`enabled=false`，创建入口阻止并提示——AT-20 向导语义） |
| 自定义链（custom-chain） | 用户逐轴添加 | Explicit | 同上 | R1 可用；逐轴定义（类型/轴线/零位/限位/速度/加速度） |

**表 T-MDL-1 六轴模板默认参数（设计默认值；单位 m/rad/rad·s⁻¹/rad·s⁻²；黄金数据集锁定后为 AT-20 示例项目基线）**：

| 轴 | type | axis（连杆系） | zeroOffset | bounds | workingRange | maxVel | maxAcc |
| --- | --- | --- | --- | --- | --- | --- | --- |
| J1 | Revolute | (0,0,1) | 0 | [−π, +π] | — | π | 2π |
| J2 | Revolute | (0,1,0) | 0 | [−π/2, +π/2] | — | π | 2π |
| J3 | Revolute | (0,1,0) | 0 | [−π, +π/2] | — | π | 2π |
| J4 | Revolute | (1,0,0) | 0 | [−π, +π] | — | 2π | 4π |
| J5 | Revolute | (0,1,0) | 0 | [−π/2, +π/2] | — | 2π | 4π |
| J6 | Revolute | (1,0,0) | 0 | [−2π, +2π]（行程 4π＝阈值边界，含于合规侧，附录 D 第 11 项/D-08） | — | 2π | 4π |

连杆几何默认：圆柱段（钢，密度 7850 kg/m³，材料默认表见 §5.3）；基座默认 ground；无工具（defaultTcp 空，tools 空——由用户添加）；关节行程模板值确保行程上限策略校验默认通过（J6 恰在边界，兼作边界用例）。

创建流程（模板**不得绕过 project 命令**，§2.4）：

```
向导（workflow/ui，PM-01）→ IRobotDesignTemplateFactory.createDraft(id, preset, localName)
  → 纯函数产出 ModelingWorkingSet 草稿（含 BasePlacement 来源=UserProvided/Template）
  → 草稿落盘（ui DraftController，PM-04）→ 用户"创建确认"→ ①命令端口 apply-robot-design
  → 新修订（URDF 基线分支语义不适用模板路径；模板创建即产生可编辑基线修订）
取消或失败不留半成品（草稿丢弃即可，无事务残留——PM-01 同口径）
```

### 5.2 参数化编辑（MDL-01/07/09、UX-05/07）

- 编辑操作全部经 `IRobotDesignEditor::applyEdit(ModelingEdit)`（§9.4）：字段级编辑（SetJointField/SetBasePlacement/SetDrivetrainField…）、对象增删（AddJoint/AddTool/RemoveObjectRef…）、改名（RenameEdit）、权威切换（SetAuthorityModeEdit，§7.6 流程）。参数表批量编辑（UX-05）= 编辑器按行批量提交同一类 Edit，一次 `applyEdit` 调用一个批量变体，产出单条变更摘要。
- **稳定对象身份**：新增关节/连杆/工具/场景对象时，编辑器在**命令 prepare 阶段**经 `HandlerContext.objectId()` 分配新 ObjectId（内存编辑态先用临时句柄，提交时回填——保证同一次编辑在草稿多次保存间身份稳定：临时句柄与分配结果的映射保存在编辑差值内，确定性可重放）。
- **改名**：`localName` 改名＝设计变更（新内容版本、失效下游，§4.8）；编辑器就地提示"该修改将使运动学及下游结果需要重算"（MDL-09）。
- **引用保护**：RemoveObjectRefEdit 对被本域引用对象（defaultTcp、被其他连杆/工具引用的几何资源）拒绝并给比较型定位诊断（I-MDL-9）；对可能被跨域引用对象给出迁移提示（§4.8）。
- **草稿隔离**：未应用编辑只存在于编辑器工作集与草稿载荷；任何其他消费者（评估器、报告、其他插件面板）读到的是已应用修订状态——"未应用草稿中的编辑如何隔离"由 PA-3 保证（草稿不是快照、无对象身份）。
- **几何生成辅助**（v0.2 增补；承接旧 autoLink/碰撞生成辅助，见 §2.5）：①`生成连杆占位几何`——确定性纯函数，按相邻关节原点连线生成圆柱占位原语（视觉用，参数可在属性区改写）；②`碰撞引用复制辅助`——把视觉几何引用复制为同资源碰撞引用（不引入网格重画/凸包简化算法，如需简化代理走需求变更）。两者产物均为普通 `GeometryRef`，与手编几何同权、同校验，来源标记 `GeometricEstimate`（methodTag 区分辅助类型）。

### 5.3 物性估算（MDL-05，唯一公式表版本 `mdl-property-formula/1`）

**输入**：连杆段元（segment）列表——每段 `{ primitive: SolidCylinder{r,L} | HollowCylinder{rOut,rIn,L} | Box{a,b,L}；T_link_seg（段坐标系在连杆系下的位姿，m/rad）；material: MaterialRef }`；材料密度默认表（设计默认值，黄金数据集登记）：钢 7850、铝 2700、铸铁 7200、钛合金 4430、工程塑料 1200（kg/m³）。

**唯一公式表**（段局部坐标系原点在段几何中心、z 轴沿段轴向；符号：ρ 密度 kg/m³，m 质量 kg，I 惯量 kg·m²）：

| 段元 | m | Ixx=Iyy | Izz |
| --- | --- | --- | --- |
| 实心圆柱（r,L） | ρπr²L | m(3r²+L²)/12 | mr²/2 |
| 空心圆柱（rOut,rIn,L） | ρπ(rOut²−rIn²)L | m(3(rOut²+rIn²)+L²)/12 | m(rOut²+rIn²)/2 |
| 长方体（a,b,L；z 沿 L） | ρabL | m(b²+L²)/12 | m(a²+b²)/12 |

**合成**（第 i 段，质心 cᵢ、旋转 Rᵢ、质心相对连杆合成质心 C 的位移 dᵢ）：
`m_link = Σmᵢ`；`C = Σmᵢcᵢ / m_link`；`I_C = Σ [ Rᵢ·Iᵢ·Rᵢᵀ + mᵢ( (dᵢ·dᵢ)E − dᵢdᵢᵀ ) ]`（平行轴定理）。结果张量**定义在连杆合成质心、参考姿态为连杆坐标系**（M-2）；全 SI、双精度、**不舍入**（显示舍入归 ui）。

**规则**：

1. 估算结果一律 `ValueProvenance{kind=GeometricEstimate, methodTag="mdl-property-formula/1"}`；用户直接修改物性→UserProvided 覆盖（保留来源标记，MDL-05"允许几何级权威值覆盖"）。
2. **质心修改确认流**：用户修改 com 时，编辑器必须让用户显式二选一——(a) 惯量按平行轴定理迁移（`I' = I + m((d·d)E − ddᵀ)`，d＝质心位移）；(b) 强制覆盖完整张量（用户输入全部六分量）。选择随编辑差值与命令摘要留痕；**不允许质心与惯量基准静默脱钩**（M-2/MDL-05）。既无迁移也无覆盖的 com 修改＝调用方错误拒绝。
3. **合法性与缺失**：估算输出在返回前自检（对称、SPD、三角不等式）；物性**缺失**（NotProvided）不触发断言、不阻断应用，登记为就绪校验 Warning 并在动力学评估侧按 DYN-06 走 DataInsufficient 降级（V15-01）。
4. **估算精度与版本**：公式表版本号随 methodTag 入来源；估算算法确定性（同输入→同字节）；估算精度不做统计声明（"估算值"限定语在报告措辞冻结，RPT-05）。
5. **边界**：modeling 只负责参数与估算契约；runtime 负责编译；dynamics 负责实际动力学计算；**估算质量不当作动力学证据**（DYN-06 可信等级由 dynamics/evidence 口径判定，modeling 仅提供来源标记）。

### 5.4 动力学参数与限值层（MDL-16）

- 关节额定/峰值力矩限值、摩擦参数（fv/fc/偏置）存放于 `robot-drivetrain` 对象（§4.7）；逐项 SourcedValue 来源标记（UserProvided/ImportMapped/CatalogBackfill）。
- 摩擦未填写→NotProvided→就绪校验 Warning→动力学评估 DataInsufficient 降级（M-8 闭环，DYN-02/06）；modeling **不判定**可信等级。
- 器件回填后的合成规则（壳体质量与转子等效惯量区分、禁止重复计入）归 SEL-10/drivetrain；modeling 只提供 schema 字段与回填写入口（阶段 C）。

---

## 6. URDF/Xacro/WorkCell 导入映射

### 6.1 导入总管线与三分边界（io.md §6.1/§10.5 承接）

```
┌─ io（文件层，唯一文件读取者）────────────────────────────────────────┐
│ 编码识别/良构检查/include 与依赖树枚举/循环检测/预算/读取快照          │
│ 产物：ResourceStreamHandle（已验证字节）＋ResourceSnapshot（digest）   │
│      ＋ResourceDependencyTree（有环→IO-FORMAT-XML-CYCLE；             │
│        缺席→IO-RES-MISSING＋缺失清单）                                │
│ Xacro 护栏：IncludeDepth/TotalBytes/绝不执行任意代码/未定义宏定位      │
└──────────────┬───────────────────────────────────────────────┘
               ▼
┌─ modeling（业务语义，本单元）────────────────────────────────────────┐
│ ①字段映射与默认补全（§6.3） ②链型判定（§6.4） ③结构语义校验（§8.2 分层│
│  → RobotDesign 草稿＋导入报告（映射/补全/忽略/不支持/分支/待确认项）    │
└──────────────┬───────────────────────────────────────────────┘
               ▼
   用户确认（向导页呈现报告；显式选链；待确认项逐条决议）
               ▼
   ①命令端口 apply-robot-design（project prepare 断言→确认放行→双编译→修订）
```

时序（URDF 导入，含对象身份产生点）：

```
向导域侧页     io(IResourceReader)      modeling(IModelImportMapper)   project
   │ open/snapshot/dependencyTree    │                              │
   │────────────────────────────────>│                              │
   │  bytes+tree+snapshots           │                              │
   │────────────────────────────────>│ mapUrdf(...) 纯函数          │
   │                                 │ → ImportOutcome{draft, report}│
   │ 呈现报告/分支选择/待确认项（不产生任何修订）                     │
   │ 用户"导入完成"→ 草稿落盘（DraftService，origin=manual）         │
   │ 用户"应用"──────────────────────────────────────────────────> submit(apply-robot-design,
   │                                 │      expectedRevision=draft.baseRevisionId)
   │                                 │   prepare：新对象 ObjectId 分配（此处才产生
   │                                 │   持久对象身份；导入映射期间用临时句柄）
   │                                 │   断言→确认放行→双编译→事务提交→新修订
```

**红线**：导入失败（解析/预算/缺失/取消）→ 不产生任何修订、不留半成品（草稿不落盘或丢弃）；导入成功但模型未就绪 → 草稿照常保存，就绪诊断随草稿呈现（应用被就绪阻断）；modeling 不自行读文件（io 唯一读取者）；io 不建 RobotDesign、不判"不可表达"（io.md §10.5 禁止反向）。

### 6.2 io／modeling／runtime 三分表

| 职责 | 所有者 | 本卡落点 |
| --- | --- | --- |
| 路径/预算/依赖树/读取快照/循环检测/Xacro 护栏原语 | io | 直接消费 `IResourceReader` 四方法与 `IO-*` 诊断码 |
| URDF/Xacro 字段映射、默认补全、忽略/不支持项、链型判定、WorkCell 有损提取语义、Xacro 展开语义（宏/参数解析） | **modeling** | §6.3～§6.6 |
| 安全解析底层设施（SafePath/BudgetGuard 等防护机制本身） | io | 不复制、不绕过（SA-14） |
| CanonicalModel/WorkCell/DWC 编译与适配 | runtime | §9.1 |

### 6.3 URDF 字段映射表（MDL-03/11/12；MDL-04）

映射纯函数、确定性；所有"默认补全"必须出现在导入报告"默认补全"清单中（不得静默——MDL-03/NFR-COR-03）：

| URDF 元素/属性 | RobotDesign 落点 | 映射规则与诊断 |
| --- | --- | --- |
| `<robot name>` | displayName ＋根 localName 候选 | 非法字符按 runtime 消歧规则预处理并报告 |
| `<link>` | LinkEntry | localName 映射；重复名→名称冲突诊断（应用边界前拦截，I-MDL-2） |
| `<inertial><origin><mass><inertia ixx…>` | body.com/body.mass/body.inertia | 缺 `<inertial>`→NotProvided（降级路径）；已提供但 m≤0/非 SPD→导入报告错误项（应用将被断言阻断） |
| `<visual>/<collision><geometry><mesh filename scale>` | shape.visual/collision → resourceManifest 条目＋GeometryRef | `package://` 等 ROS URI 不支持（io R1 口径）→定位诊断引导改相对路径；文件缺失→IO-RES-MISSING＋外部引用记录（草稿可带 Recorded 资源，§6.7） |
| `<material>` | materialRef（估算密度提示） | URDF 材质无密度→密度 NotProvided（估算不可用，报告登记） |
| `<joint type="revolute|continuous|prismatic|fixed">` | JointEntry.type | 四类识别（MDL-12）；mimic/planar/floating→§6.4 阻断口径，**不得转 FixedFrame、不得经选择绕过**（M-6） |
| `<axis>` | axis | 任意有限非零轴可接受（不要求 Z，MDL-11）；**缺失**→按 URDF 语义取局部 +X 并记入"待确认草稿"清单（用户确认后可应用）；**零轴/非有限**→仅报告、该关节标记 Invalid，包含此类关节的草稿**不得提交修订**（MDL-11 后半，prepare 断言兜底） |
| `<limit effort velocity lower upper>` | bounds、maxVelocity；effort→torqueLimit（drivetrain 对象，Peak 候选） | 缺 lower/upper（revolute）→待确认草稿项；continuous→bounds NotApplicable（类型保留） |
| `<mimic>` | 不映射 | 识别＋报告＋阻断（MDL-12；MDL-21 启用后仅线性耦合放开） |
| `<transmission>` | 不映射（线性耦合候选提示，R2） | 报告"不支持项" |
| `<gazebo>` 等外来扩展 | 忽略项清单 | 报告不解释 |
| `<disable_collisions>`（自碰撞配置） | SelfCollisionHints（临时）→导入报告"策略草稿候选输入" | **不写策略对象**（policy 权威）；交接 API 待 policy 冻结（P-MDL-3） |
| （无安装语义） | basePlacement＝ground（来源 ImportMapped＋"默认补全"清单） | MDL-22 V15-04：未显式配置默认地面 |

### 6.4 链型判定（§2.1 两维度；MDL-12）

- **维度一（文件拓扑）**：单可动主链→直接维度二；多可动分支→分支报告（对象与原因可观察）＋用户显式选择一条主链，辅助分支（fixed 连接的子树等）转为场景/环境候选或忽略项（用户选择），**辅助分支本身不构成拒绝**。
- **维度二（所选主链能力）**：

| 所选主链 | 模板创建 | 导入识别 | 草稿编辑 | 正式计算/报告 |
| --- | --- | --- | --- | --- |
| 六/七轴全旋转（含经确认工程工作范围的 continuous，类型保留） | ✅ | ✅ | ✅ | ✅ |
| 4/5 轴，或目标链含 prismatic | ❌（创建入口阻止并提示） | ✅（识别＋诊断） | ✅（兼容编辑） | ❌（诊断"超出首版产品模板范围"；R1；MDL-12-S1 启用后仅六/七轴含 prismatic 放开） |
| 目标链含 mimic/planar/floating/耦合 | — | ✅（识别＋报告） | — | ❌ 阻断＋诊断（不得转 FixedFrame/绕过；MDL-21 启用后仅线性耦合放开） |

- **continuous 工程工作范围**：类型与来源语义保留；未确认→不得正式运行（就绪校验 Blocking 项"工程工作范围未确认"）；确认值必须有限区间（qmin'<qmax'）、不设行程上限校验、**不回写权威 bounds**；确认动作经待确认草稿项决议＋命令摘要留痕。
- 判定实现为导入映射的纯函数部分（`Import.hpp`），同一判定被模板创建入口复用（§2.1 创建列）。

### 6.5 Xacro 受控预处理（MDL-19）

- 入口：`IXacroExpandService::expand(ValidatedXacroSource, options, diags)`（modeling 计算库，语义所有者）；输入为 io 已验证字节＋依赖树；护栏（递归深度 IncludeDepth、TotalBytes、`IO-FORMAT-XML-CYCLE`、`IO-RES-MISSING`、未定义宏/参数的行列定位、**绝不执行任意代码**）由 io 机制承载，modeling 语义层在护栏内解析宏/参数/属性替换。
- 参数列表展示（向导页）＋展开环境（ substitutions 清单）随导入报告留痕（来源记录：原始 .xacro 的 ResourceSnapshot digest 进草稿 externalRefs）。
- 展开产物进入与 URDF **相同**的映射与安全边界（§6.3/§6.4）；展开失败/依赖缺失→可定位诊断（宏名/行列），不产生草稿。
- 所有权口径：展开**语义/引擎**归 modeling（DTB WP-13-T06 `XacroExpand.*`）；io.md §13.3 同名单与 WP-13-T06 的重叠登记为 P-MDL-4（§14.3），建议口径"护栏 io、语义 modeling"，待 io 卡所有者确认。

### 6.6 WorkCell 反向导入（MDL-18，R2／阶段 D——仅边界设计）

- 通道：`IModelImportMapper::mapWorkCellXml(...)`（有损提取）；io 侧 `IResourceReader` 通用 XML 通道（`GenericXml` 文件层形态，具体签名 io.md 未定义——R2 启用前由 io 卡冻结）。
- 提取语义：关节/限位/位姿/几何/碰撞设置→RobotDesign 草稿＋**提取报告**（不可表达/未保留内容逐项列出）；不可表达内容按 MDL-10/12 **同一判定规则**处置；结果永远是草稿，不经确认不产生修订。
- R1 不实现、不留桩代码（DTB WP-13-T17，阶段 D 启用）；"从 WorkCell XML 新建项目来源"维持明确不做（PM 明确不做清单）。

### 6.7 导入资源与对象身份（CON-03 三段边界、PM-01）

```
一次性读取（P-1，io；导入向导显式选择的外部源，不受资源区逃逸检查但受预算）
   → 草稿期外部引用记录 ExternalResourceRecord{absPath＋recordedDigest}（Recorded；io probe 检测缺失/变化）
   → 应用（apply-robot-design）：robot-design 对象引用资源以 Recorded 态入库；runtime 编译可读
   → 转正式前（Verified 评估/正式报告）固化：io solidifyToStaging→project 入对象库＋引用保护
     （Solidified；此后不可变，runtime S10 免复查）；未固化→证据门禁阻断（evidence，非 modeling 判定）
```

- 用户在 PM-01 向导选择"复制入项目资源区"→导入时即固化路径（Recorded→Solidified 提前）；选择"外部引用记录"→维持 Recorded。检测能力（Missing/Changed）归 io（NFR-REL-04），modeling 在就绪校验中呈现资源状态（§8.2 L6）。
- **导入后对象身份**：全部新 ObjectId 在**命令 prepare 阶段**分配；导入映射期间（草稿期）使用确定性临时句柄（`tmp-<序号>`，不入库）；同一草稿重复应用因 StaleRevisionRejected 只会成功一次。
- **导入取消**（用户取消/失败/预算超限）：丢弃内存工作集；已落盘草稿由用户经关闭对话框处置（PM-03 三选）；正常取消不属于错误、不产生错误诊断（UX-03）。

### 6.8 规范模型导出/导入 roundtrip（MDL-20）

- **导出**（会话级文件操作，不产生修订）：规范模型包 = ZIP 封装（io `ZipChannel`）＋`manifest.json`（显式来源标识 `producer=ird-modeling`、schemaVersion、对象清单与 digest）＋根/部件对象 canonical 字节＋Solidified 资源副本＋命名位姿；**原子写出**经 io `AtomicFile`（临时区→校验→替换）——**导出失败保证项目状态不变且旧输出文件不被破坏**（MDL-20"恢复先前输出"的文件层语义）。
- 另可导出 WorkCell/DWC XML 供外部查看：数据源＝RuntimeSnapshot 的只读视图（runtime `WorkCellConstView`），modeling 仅编排写出（io AtomicWriter），不改造内容。
- **导入回读**：仅识别本软件导出的规范工件（manifest 来源标识＋schema 校验）；其他文件→稳定诊断引导至 MDL-18/R2 通道；roundtrip 后逐项验证一致（权威参数化 DH/显式、物性、资源引用、碰撞规则、命名位姿——逐项清单入导入报告，对照 §10 行 V-20）。

---

## 7. 双权威参数化与 DH 转换

### 7.1 双权威关系图（MDL-02/09）

```
                    ┌────────────────────────────────────────────┐
                    │        RobotDesign（权威源二选一，互斥）        │
                    │   authority = Explicit ｜ StandardDH        │
                    └──────────────┬─────────────┬───────────────┘
              权威字段（可编辑）     │             │     权威字段（可编辑）
        JointType/zeroOffset/bounds │             │  DhParameters{θ偏置,d,a,α}
        maxVel/maxAcc/workingRange  │             │  （关节链几何全由 DH 派生）
                                   ▼             ▼
                 ┌─────────────────────────┐  ┌──────────────────────────┐
                 │ 显式权威下的派生只读字段      │  │ DH 权威下的派生只读字段       │
                 │ dhDerived（展示用）        │  │ joints[].axis / origin    │
                 └─────────────────────────┘  └──────────────────────────┘
                        ▲      单向同步（权威→派生，无反向写）      ▲
                        └────────────── 切换规则（§7.6）───────────┘
```

- **不允许两个字段同时成为无条件真值**：`axis/origin` 与 `DhParameters` 的权威地位由 `authority` 单一开关裁定（I-MDL-8）；任何时刻另一侧只能是 `DerivedReadOnly` 来源的派生缓存。
- 派生缓存**不参与** canonical 编码身份（与 installPreset 来源标记同理，runtime §4.5 排除口径）——重算派生缓存不改变对象内容版本；**canonical 编码只含权威字段＋模式开关**，派生值在读取时按需重算（确定性纯函数）。

### 7.2 参数来源表（逐字段权威/派生/优先级）

| 字段 | Explicit 权威态 | StandardDH 权威态 | 来源优先级（高→低） |
| --- | --- | --- | --- |
| joints[].axis / origin | **权威可编辑**（MDL-09） | 派生只读（DerivedReadOnly） | 用户编辑 > 导入映射 > 模板默认 |
| joints[].dhDerived | 派生只读（展示） | **权威可编辑** | 用户编辑 > DH 转换产出 > — |
| type / zeroOffset / bounds / workingRange / maxVel / maxAcc | 两态均权威（零位与限位语义不随模式切换） | 同左 | 用户 > 导入 > 模板 |
| 连杆物性 | 权威（估算=GeometricEstimate 来源的权威值，可被 UserProvided 覆盖——MDL-05） | 同左 | UserProvided > GeometricEstimate（估算仅为来源标记，同为权威值） |
| 基座安装 / 传动 / 工具 / 场景 | 权威，与权威模式无关 | 同左 | 同上 |

### 7.3 冲突矩阵、更新时序与非法组合

| 编号 | 冲突场景 | 处置 |
| --- | --- | --- |
| C-1 | DH 权威态下编辑 axis/origin | 调用方错误拒绝（I-MDL-8）；UI 呈现"该字段为派生只读，切换权威模式需经转换判定" |
| C-2 | Explicit 权威态下编辑 dhDerived | 同上（派生只读） |
| C-3 | SetAuthorityModeEdit 且链结构不满足 DH 结构前提 | 转换判定 NotExpressible → 拒绝切换＋终判诊断（§7.5） |
| C-4 | 显式→DH 判定 Approximate | **不得成为权威 DH**（MDL-10）；拒绝切换＋附误差度量 E 与收敛状态 |
| C-5 | DH→显式（权威展开） | 无条件允许（MDL-10"DH→通用关节必须无损"）；展开后新 authority=Explicit，axis/origin 变为权威 |
| C-6 | 切换权威的同时还有其他未应用编辑 | 编辑器要求先决断既有编辑（提交或撤销）再执行切换（切换＝独立命令，摘要单独留痕） |
| C-7 | 导入草稿与模板路径同时挂起 | 向导域单草稿（module=`modeling`）互斥；第二个导入会话要求先处置既有草稿（PM-04） |

**更新时序**（权威编辑→派生重算）：

```
用户编辑权威字段 → applyEdit 校验（I-MDL-1~12）→ 权威值更新
  → 同步重算派生缓存（纯函数，同帧完成；失败＝权威值回滚＋诊断——派生失败
     说明权威组合退化〔如零长连杆〕，按 I-MDL-6/7 拒绝）
  → 就绪校验增量重估 → UI 投影刷新（不产生修订，不触发下游失效）
用户"应用" → 命令 prepare（断言/确认/双编译）→ 新修订 → 切片失效按依赖传播
```

**非法组合**（构造/编辑边界拒绝）：authority=StandardDH 且 axis 为 UserProvided 来源；Explicit 且 dhDerived 参与编码；continuous 关节带有限 bounds；prismatic 带 workingRange（workingRange 仅 continuous）。

### 7.4 DH 约定与转换公式（标准 DH／Schilling 约定，MDL-02）

- 参数（逐关节 i）：关节角 θᵢ（rad，含零位偏置）、连杆偏距 dᵢ（m）、连杆长度 aᵢ（m）、扭转角 αᵢ（rad）。连杆变换（标准/远置约定）：

```
T_{i-1,i} = Rot_z(θᵢ + qᵢ) · Trans_z(dᵢ) · Trans_x(aᵢ) · Rot_x(αᵢ)      （qᵢ 为关节变量）
```

- 基座与工具变换**不在 DH 参数内**：基座安装走 BasePlacement（§7.7 隔离）；法兰后工具走 ToolDefinition——DH 只参数化关节链（MDL-02 范围）。
- 零位对齐：权威零位语义由 `zeroOffset` 承载（`q_authoritative = q_zeroOffset + q_rw`，runtime 口径）；DH 的 θᵢ 定义为**含偏置前**的几何参数，θᵢ_offset 与 zeroOffset 在转换中显式分离，roundtrip 验证含零位（q=0 时显式位姿==DH 派生位姿）。
- **DH→显式（权威展开，无损）**：逐级累乘得 `T_base_jointi` 与轴线 `z_i`（`T_{0,i}·(0,0,1)`），构造 `origin=T_{parent,i}`、`axis=z_i`；数值误差仅浮点累乘（对照附录 D 第 4 项容差验证，实测应远优于 1×10⁻⁹）。
- **变换方向图**：

```
DH 权威 ──(展开，无损，允许)──> 显式权威        显式权威 ──(求解，五状态，受控)──> DH 判定结果
   │                                                    │ Exact/ExactNonUnique 且等价验证通过
   ▼                                                    ▼
 派生 axis/origin（只读）                          才可 SetAuthorityMode(StandardDH)
```

### 7.5 显式→DH 五状态判定（MDL-10，两阶段互斥化）

```
输入：所选主链的显式权威关节序列（Explicit 态）
【第一阶·结构适用性检查】（解析，先于一切数值求解）
   纯串联 ∧ 每关节单自由度旋转（Revolute；continuous 按 MDL-12 确认范围后视同旋转）
   ∧ 相邻关节轴几何关系可参数化（存在公共法线或平行/相交退化参数化）
   └─ 不满足 → NotExpressible（终判，不进入求解；含 prismatic 关节、分支、多自由度）
【第二阶·数值求解】（仅结构适用链）
   未知量 x＝逐关节 (θ_offset, d, a, α)；目标＝DH 链重建的轴线/原点 vs 显式权威轴线/原点
   求解：确定性非线性最小二乘（固定初值策略：单位参数；固定迭代上限与收敛判据；
         不读时钟/环境，同输入同解——NFR-COR-02）
   判定（附录 D 第 5 项，逐关节逐项上界——C3）：
     全部关节 轴线方向角偏差 ≤1×10⁻⁹ rad 且 原点位置偏差 ≤1×10⁻⁹ m
       ├─ 解唯一（在去重容差内无可辨识第二解） → Exact
       └─ 多解（如相邻平行轴 a 任意、相交轴 d 不定等退化族） → ExactNonUnique（报告解集＋按
            参数向量字典序确定性选定其一——稳定选择规则，禁止随机挑选）
     收敛但任一项超容差 → Approximate（不得成为权威 DH；输出最小误差近似＋报告度量
            E＝Σᵢ(轴线角偏差[rad]＋原点位置偏差[m])——E 仅为呈现指标、不参与判定；
            须报告达到的误差值与收敛状态）
     求解器数值失败（不收敛/发散/资源异常） → AnalysisFailed（另报诊断，不构成语义结论）
```

- 退化与奇异处理：平行相邻轴（αᵢ=0，aᵢ=间距，dᵢ 自由族）→ExactNonUnique；相交轴（aᵢ=0，dᵢ=0）；重合轴（α=0 且 a=0，d 自由族）。全部退化族的自由参数按字典序规则定值，保证结果可复现。
- 判定结果五态互斥，随诊断携带逐关节逐项偏差明细（比较型三要素：实际值/期望值/单位）。

### 7.6 等价验证与权威切换（MDL-02/10、ARC-03）

- **权威切换条件**：仅 Exact/ExactNonUnique **且**编译链 FK 对照等价验证通过后，才可置 `authority=StandardDH`；Approximate/NotExpressible 锁定显式权威。等价验证＝对同一模型分别按（切换前显式权威）与（候选 DH 权威）构造 Description→经 runtime 编译链（S1–S5 buildCanonicalModel）取 FK 对照——位置 ≤1×10⁻⁹ m、姿态 ≤1×10⁻⁹ rad（附录 D 第 4 项；AT-16）。
- **roundtrip 验证**（§10 行 V-11）：DH 权威→展开显式→再求 DH：参数级（≤第 5 项容差）与 FK 级（≤第 4 项容差）双重一致；黄金数据集三套（DH/显式/不可表达样本，DTB WP-13-T16）承载。
- 切换动作是**独立领域命令**（`apply-robot-design` 的权威切换变体）：prepare 内执行转换判定＋等价验证（调用 runtime 编译分段入口，只读、不发布快照），摘要记录判定状态与验证容差结果；验证失败不产生修订。
- **基座—世界变换隔离（MDL-22/M-11）**：转换只作用于关节链；`R_world_base` 由 runtime 从 BasePlacement 编译产生、四消费方（三维视图/环境几何/碰撞/重力投影）读同一编译产物；modeling 在任何转换、导入、优化路径中都**不得**计算、缓存或二次叠加基座—世界旋转（含倒挂/壁装模板的渲染位姿——归 runtime 编译产物）。预设轴向以 runtime `installationPresetRotation()` 为唯一权威产出点（P-RT-4 交叉核对随 WP-13-T11 完成并留痕）。

## 8. 传动耦合与模型就绪校验

### 8.1 传动与耦合模型输入（MDL-21，R2／阶段 D；MDL-16 R1 部分）

| 项 | 设计口径 |
| --- | --- |
| 常矩阵 | 耦合矩阵 C 为**常矩阵**（建模期不可含时变/工况项）；存放于 `robot-drivetrain.coupling`（§4.7）；R1 该字段禁止配置（存在即阻断＋诊断 `MDL-21-COUPLING-STAGE-LOCKED`） |
| 关节↔电机映射 | 位置映射 `Δq_joint = C·Δθ_motor`、力矩对偶 `τ_motor = Cᵀ·τ_joint`（DYN-04 虚功口径）——**映射算法归 drivetrain 的 DriveTrainMappingEvaluator（唯一实现），modeling 只存矩阵与适用关节窗口、不实现虚功映射**（M-12；NFR-MNT-07 无重复算法） |
| 矩阵身份与版本 | C 随 `robot-drivetrain` 对象入内容版本；矩阵字节进入 CanonicalModel 编译内容（runtime `DrivetrainDescription.coupling`），变更产生新修订（PA-1/PA-2） |
| 线性耦合启用 | MDL-21 启用后仅**线性耦合矩阵**放开（mimic 经同一启用口径放开线性部分；planar/floating/闭环维持阻断——M-6） |
| 病态/非常矩阵 | 非方阵、奇异、条件数 >1×10⁸（P-RT-7 设计默认，与 runtime `InputInvalid` 同口径）→编辑边界比较型诊断＋应用阻断；数值阈值以 policy/附录 D 演进为准，modeling 不私设第二常量（阈值来源登记见 P-MDL-7） |
| mimic/闭环阻断 | R1：目标链含 mimic/闭环→就绪校验 Blocking＋导入阻断（§6.4）；建模侧**不提供** mimic 建模入口 |
| Stage B 阶段锁 | 阶段锁消费口径与 OPT StageB 一致（传动比 ratioPerJoint 为 StageB 连续变量可编辑；耦合矩阵归阶段 D）——V12-02"参数可编辑"与"性能可评估"两阶段口径 |
| 消费边界 | runtime：编译进 CanonicalModel；drivetrain：虚功映射与工作点（DYN-04/SEL-05）；dynamics：关节侧 RNEA＋驱动端映射入口；selection：反射惯量/惯量比校核——四消费者经各自端口读同一对象，modeling 不代替任何一方计算 |
| 变更传播 | 耦合/传动比变更→新修订→依赖声明含传动键的切片失效（限位校验经映射的域自行声明） |

### 8.2 模型就绪校验（分层、状态机、结果流向）

**分层检查表**（`IModelReadinessChecker::check`，纯函数；顺序即短路优先级——高层依赖低层通过）：

| 层 | 检查 | 数据依据 | 失败级别 |
| --- | --- | --- | --- |
| L0 结构完整 | 链连通、单串联无环、joints/links 计数关系、Fixed 关节位置合法 | I-MDL-1 | **Blocking** |
| L1 引用完整 | defaultTcp/toolRefs/sceneRefs/poseSetRef/drivetrainRef 指向闭包存在对象、token 匹配 | I-MDL-9 | Blocking |
| L2 单位/数值合法 | 一切 Provided 值有限；非法单位不存在（构造层已保证） | I-MDL-3 | Blocking |
| L3 关节轴有效 | 可动关节 axis 非零有限可归一化 | I-MDL-6 | Blocking |
| L4 限位有序 | Revolute/Prismatic qmin<qmax；continuous 有已确认有限 workingRange（未确认→Blocking"工程工作范围未确认"） | I-MDL-4、MDL-06④/MDL-12 | Blocking（区间错）/Blocking（未确认） |
| L5 惯量合法 | 已提供值 m>0、对称（相对 1×10⁻¹²）、SPD（严格>0）、三角不等式 | I-MDL-5、附录 D 第 6/7 项 | Blocking（已提供非法）／Warning（缺失——DataInsufficient 降级预告） |
| L6 资源存在 | resourceManifest 逐项状态：Recorded 且探测 Missing/Changed→Warning（含固化提示）；Solidified→通过 | I-MDL-10、CON-03 | Warning（不阻断应用；正式评估由证据门禁阻断） |
| L7 工具与 TCP 完整 | 有 tools 则 defaultTcp 已设且 tcpKey 存在；工具物性合法 | I-MDL-9、KIN-14 | Blocking |
| L8 基座姿态合法 | custom 必填 customEaa；preset≠ground 而 R=I 拒绝；正交容差 1×10⁻¹² | I-MDL-7、P-RT-4 | Blocking |
| L9 传动可用 | ratio 有限>0；R1 无 coupling；R2 矩阵方阵/可逆/条件数阈值 | I-MDL-11/12 | Blocking |
| L10 可构造 CanonicalModel | schema 版本与 Description 契约匹配、字段集完整（§9.1 映射表全绿） | descriptionContractVersion | Blocking |
| L11 可请求编译 | writable、策略可解析（④端口 resolvePolicy 有 Valid 结果）、（应用路径）baseRevision==tip | project/policy | Blocking（带定位） |

**级别语义**：`Blocking`＝应用被阻止（就地、精确定位到对象——MDL-06 断言就地阻止口径）；`Warning`＝可应用但登记（缺失物性/未固化资源/传动缺省）；`Confirmable`＝策略校验超限类（行程上限），不就地阻断、经显式确认放行（SA-15，§9.3）。

**就绪状态图**：

```
                 ┌────────────┐  任一 Blocking      ┌──────────────┐
  编辑变更 ────> │ 逐层评估     │ ─────────────────> │ NotReady     │
                 │ L0→L11      │                     │ (blockers 列表)│
                 └─────┬──────┘                      └──────────────┘
                       │ 无 Blocking
                       ▼
                 ┌────────────┐  有 Warning/待确认   ┌──────────────┐
                 │ 层结果聚合   │ ─────────────────> │ ReadyWithNotes│
                 └─────┬──────┘                      └──────────────┘
                       │ 无 Warning 无待确认
                       ▼
                 ┌────────────┐
                 │ Ready      │（可提交命令；提交后再经 prepare 断言/确认/双编译）
                 └────────────┘
```

- **不可行模型 ≠ 未就绪模型**：就绪校验只回答"模型输入是否完备合法、能否构造与编译"；"工程上是否可行"（可达性/碰撞/动力学裕度）归各评估域的工程判定（EngineeringStatus，evidence 词表）。modeling 不输出 Feasible/Infeasible 结论；就绪诊断不使用工程判定状态词（七态投影归 ui 求值，modeling 只供 `inputComplete`/缺项数据）。
- **结果流向**：UI——`DomainReadinessItem{domainKey="modeling", inputComplete, missingItemKeys}` 经 `IPluginUiModule::readonlyProjections()` 上报（ui §6.5 只汇聚不判定）；project——Blocking/Confirmable 在命令 prepare 内重估（就绪校验结果不替代 prepare 现场断言，防 TOCTOU）；evidence——不直接消费建模就绪（快照完整性由组装方与通用证据门禁判定）；诊断目录——Warning/Confirmable 明细经 IDiagnosticSink 留痕。

**四轴状态正交关系表**（编辑状态 × 模型就绪 × 编译状态 × 工程状态——四轴互不替代，CON-02/TASK-02 正交原则）：

| 轴 | 取值（词表来源） | 所有者 | 正交规则 |
| --- | --- | --- | --- |
| 编辑状态 | 编辑中（内存未落盘）／草稿（落盘未应用）／已应用修订 | ui DraftController／project | 只有已应用修订才有后续三轴的非平凡取值；编辑态与草稿态下编译/工程轴恒为"未编译／未评估" |
| 模型就绪 | NotReady／ReadyWithNotes／Ready | modeling（§8.2） | 描述"输入完备合法、可否构造与编译"；对编辑态与草稿态即时求值（预检），对已应用修订在 prepare 重估；不随工程判定改变 |
| 编译状态 | 未编译／已编译（RuntimeSnapshot 存在）／编译失败（未提交，无修订） | runtime／project | 只作用于已应用修订；编译失败＝修订不存在＝编辑状态回退到"草稿保留"；旧快照与新修订并存（历史可追溯） |
| 工程状态 | 未评估／可行／工程不可行／数据不足（core EngineeringStatus） | evidence／各评估域 | 只针对已编译快照的评估结果；**"未就绪"不产生任何工程判定**（不可行≠未就绪）；数据不足与可行/不可行互斥（汇总优先级见 REQUIREMENTS §8.1 表 2） |

非法组合示例（均被构造边界/命令边界拒绝或不可能出现）：草稿态＋已编译（草稿无修订身份，无编译锚）；NotReady＋可行（就绪阻断在评估组装之前——输入未完成不运行正式评估，REQ-06/§8.1 表 2）；编译失败＋可行（无快照即无评估输入）。

- **边界**：就绪校验**不能代替**运动学求解或工程判定（不自建 FK 检查"杆长是否合理"之类语义判断）；除附录 D/policy 明确容差外不引入新数值阈值（§2.4）。



---

## 9. CanonicalModel 交接与公共接口

### 9.1 CanonicalModel 构造与 runtime 交接（ARC-03；MDL-06/14/22）

**端到端流程图**（编辑→CanonicalModel→RuntimeSnapshot）：

```
[编辑态] IRobotDesignEditor 工作集
     │ 用户"应用"
     ▼
①命令端口 submit(apply-robot-design, expectedRevision)
     │ project：S1 形式校验→S2 基线解析→S3 处理器 prepare（本单元：断言/确认产出/CommandPlan）
     │          S4 确认放行（ICommandInteraction 回调 ui）→S5 双编译（requiresDualCompile=true 时）
     │            └─ IModelCompilePort.compileWorkCellAndDwc(CompileRequest)
     │                 └─ runtime 十段链：S2 调 modeling 提供的 IRobotDesignReader
     │                      （robot-design 对象字节→RobotDesignDescription）
     │                 任一失败→Failed(compile-failed)→不提交修订（MDL-06 原子性）
     │          S6 七步事务→S7 修订事件＋失效通知
     ▼
[已应用修订] objects/ 不可变对象（根/工具/场景/位姿集/传动/资源）
     │ 评估组装（evidence/各域，经 L5）：同一修订闭包→IRuntimeSnapshotFactory.create
     ▼
RuntimeSnapshot（不可变；含 CanonicalModel＋RuntimeNameMap＋WorkCell/DWC 只读视图；
                  modelIdentity/contentIdentity 入快照身份；编译后零外部依赖）
     │
     ├─ 各评估域消费（唯一可计算形态；编辑态永不进入）
     ├─ UI 三维视图消费（经 IRuntimeModelView 只读；UI 不直接调用 RobWork）
     └─ 输入变化后旧快照保留（历史结果归档与反解仍可用——CON-02/§4.5 runtime）
```

**字段映射表**（RobotDesign/部件对象 → `runtime::RobotDesignDescription`；全 SI）：

| Description 字段 | 来源 | 备注 |
| --- | --- | --- |
| `descriptionContractVersion` | 建模 schemaVersion（初值 1） | 入 CanonicalModel 身份与缓存键；演进即新版本（R-MDL-3） |
| `robotLocalName` | 根对象模型 localName | RuntimeNameMap 设备作用域名来源 |
| `joints[]`（JointDescription） | 根 joints[]（Explicit 直接映射；StandardDH 先经 §7.4 展开为显式表示） | objectId/localName/type/axis/origin/lower/upper/maxVelocity/maxAcceleration/workingRange 全 SourcedValue 化 |
| `links[]`（LinkDescription） | 根 links[]（含 body/shape 解引用 resourceManifest） | mass/centerOfMass/inertia/material/visual/collision |
| `tools[]`（ToolDescription） | tool-definition 对象（经 toolRefs 解引用） | objectId∈objectRefs（CM-0 复核）；有 tools 则 defaultTcp 必填 |
| `scene[]`（SceneObjectDescription） | scene-object 对象（经 sceneRefs 解引用） | worldPose 世界系固连、不预乘安装旋转 |
| `base`（BasePlacementDescription） | 根 basePlacement | preset/customEaa/basePosition——**不在此计算 R_world_base**（runtime 唯一计算） |
| `drivetrain`（DrivetrainDescription） | robot-drivetrain 对象 | ratioPerJoint＋coupling（R2） |
| `friction[]` | robot-drivetrain.frictionPerJoint | 逐关节可空（NotProvided→DWC 能力降级路径） |
| `resourceRefs[]` | 根 resourceManifest（Solidified→项目对象字节；Recorded→重读＋重算 digest 不缓存） | 路径不入身份 |

**身份/版本/快照绑定**：

- 对象 ContentVersion 由 project 计算（字节 SHA-256）；`CanonicalModel.builtFrom`＝robot-design 对象字节摘要；`contentIdentity`＝SHA-256 over RT-Codec 编码（排除 diagnostics/capabilities/revisionSeq 等，runtime §4.5）——modeling 侧**保证同输入字节→同 Description**（reader 纯函数），不重复计算任何内容身份。
- **资源快照**：Recorded 资源每次编译重读＋digest 复核（不符→`ResourceChanged` 整体失败）；Solidified 免复查——固化激励与 CON-03 一致。
- **能力声明**：`RuntimeCapability`（如 DWC 可用性）由 runtime 从 Description 物性缺失推导（`SkippedNoPhysics`），modeling 不自行声明能力。
- **不完整模型拒绝**：三层防线——编辑边界（I-MDL 系 fail-fast）、prepare 断言（就地阻止＋定位）、runtime S3/S5 复核（`InputInvalid/StructureInvalid` 兜底）。modeling 不假设 runtime 会修复任何输入。
- **编译请求路径**：命令路径经 project `IModelCompilePort`（L5 适配 runtime 编译器）；modeling **不直接调用** `ICanonicalModelCompiler` 产生修订路径之外的可发布快照；权威切换等价验证（§7.6）使用 `buildCanonicalModel` S1–S5 分段入口做只读对照，不产生 RuntimeSnapshot。
- **编译失败回传**：`CompileResult{ok=false, diagnostics}`→命令 `Failed(compile-failed)`→修订不产生；诊断（runtime RT-* 码）＋prepare 断言诊断（MDL-* 码）一并入诊断目录，定位到对象。
- **项目修订与快照绑定**：RuntimeSnapshot 身份含 project/branch/revision——修订与快照天然绑定；项目切换后旧快照存活（零外部依赖），迟到结果按旧快照映射反解（runtime §9.2/9.3）；旧快照**永不被 modeling 撤销或失效**（历史证据，CON-02）。

### 9.2 `IRobotDesignReader` 决议（P-RT-5 交叉核对结论）

runtime 契约要求 modeling 提供 `IRobotDesignReader::read(objectBytes, formatVersion) → Expected<RobotDesignDescription, RuntimeError>`（runtime.md §4.2；注入形态由 L5 装配）。本卡**采纳 reader 注入形态**，并做如下细化（P-RT-5 的 modeling 侧答复）：

- `read(bytes)` 输入是**根对象**字节；部件对象（tools/scene/drivetrain/资源）由 reader 内部经**闭包域字节源**解析——该源在 L5 组装 CompileRequest 时与 `CompileRequest.objects` 同源绑定（同一修订闭包），保证 reader 不读越界/越修订数据，确定性等价于纯函数。
- 单位 SI 化在 reader 生成 Description 前经 core 唯一换算入口完成（runtime §4.2 单位纪律的 modeling 侧履行点）。
- 直解方案（modeling 对象编码＝Description 直通编码）被否决：理由 a) Description 是 runtime 契约面，直通会把 runtime 契约版本变化倒灌进建模存储 schema；b) reader 注入保持"建模 schema 演进→descriptionContractVersion 单点适配"（R-MDL-3 的隔离层）。
- 该决议随 WP-13-T01/T12 与 runtime 卡所有者交叉核对留痕；如 runtime 侧主张变更，按 P-RT-5 裁决回改本节。

### 9.3 建模命令处理器族（project `ICommandHandler` 实现；ARC-01/SA-15）

**命令清单**（commandType token 满足 `^[a-z0-9-]{3,64}`；**不含点**——P-PR-9 未决期间采用无点形态，若裁决为点分命名则按裁决迁移，迁移属破坏性变更须同步 schemaVersion）：

| commandType | 写入对象 | requiresDualCompile | 断言/确认 | inverse |
| --- | --- | --- | --- | --- |
| `apply-robot-design` | robot-design（＋新增部件对象） | true | 物性①②③＋限位④硬断言；行程上限 Confirmable；链型判定 | 快照式逆命令 |
| `apply-tool-definition` | tool-definition（＋根引用表增量） | true（工具几何/物性入 WC/DWC） | 工具物性断言；defaultTcp 引用校验 | 同上 |
| `apply-scene-objects` | scene-object 批量（增/改/移除引用） | true（碰撞几何变更） | 场景引用校验 | 同上 |
| `apply-named-poses` | named-pose-set | **false**（不进 Description） | 无物性断言（纯参考数据） | 同上 |
| `apply-drivetrain-design` | robot-drivetrain | true（ratio/摩擦/耦合入 Description） | 传动合法性（I-MDL-11/12）；R1 耦合阻断 | 同上 |

**prepare 管线**（处理器内，S3 阶段；全部同步、命令线程）：

```
decode(payload) ──失败──> RejectedInvalidInput（invalid-payload）
   │
重建基线工作集（expectedRevision 闭包 → reader 同源解码；基线 ≠ ctx 基线 → stale-revision 由
project S2 已拦截；处理器内防御性复核）
   │
应用编辑差值 → 前置断言（MDL-06 分域）：
   硬断言失败 → RejectedHardAssert ＋逐项 DiagnosticRecord（code=MDL-ASSERT-*，
                subject=对象 ObjectId，localName，比较型三要素）——就地阻止＋精确定位
   物性缺失   → 不阻断，转 Warning 诊断随计划留痕
   策略校验   → ④端口 resolvePolicy → IJointLimitEvaluator.evaluate(jointTable, policy)
                → TravelLimitExceeded → 构造 ConfirmableFinding（code=MDL-06-TRAVEL-LIMIT，
                比较型：实际行程/阈值/单位 rad；policyContentId=策略内容身份）
                → CommandPlan.confirmableFindings（project S4 经 ICommandInteraction 呈现；
                确认→凭据＋绑定四元组入命令摘要；未确认→Rejected，不产生修订）
   │
CommandPlan 组装：objectWrites[]（新对象 ObjectId 经 ctx.objectId() 分配；ContentVersion 由
project 计算）＋ requiresDualCompile ＋ inverse{commandType, payload=受影响对象前一 (oid,cv)
canonical 字节集} ＋ summary（人读中文摘要：对象/字段/确认留痕/资源状态变化）
   │
（S5 双编译由 project 触发；失败→Failed(compile-failed)，修订不产生）
```

**撤销/重做流程**（与草稿局部撤销正交，PM-18/REQ-11 分离）：

```
项目级：undo（UndoRedoService）→ 取当前 tip 修订 inverse 记录 → 以 inverse 载荷提交
        restore 型命令（同一 commandType，payload=restore 变体）→ 新修订（历史不改写）
        redo → 正向命令重放载荷（保存于修订 inverse 的对偶记录）→ 新修订
草稿级：编辑器 undoLocal/redoLocal（工作集版本栈，零修订、零落盘）——UI 快捷入口，语义归 ui
空历史：UndoRedoStatus.status 给稳定提示数据（PM-18；词表归 ui 呈现）
StaleRevisionRejected：undo/redo/应用过期草稿→Rejected(stale-revision)→草稿以 apply-retained
        保留，用户基于当前 tip 重新编辑后重提（PM-04；定位差异由 CommandResult 附带）
```

**命令上下文与交互约束**：处理器经 `HandlerContext` 获得 objectId 分配/只读查询/编译与策略端口句柄；`interaction()` 回调仅 project 调用（处理器只产出 ConfirmableFinding 集合，不直接回调交互）；回调内处理器不得参与（确认等待占命令执行槽，project.md D-07）。

### 9.4 公共接口详细设计

公共契约前提：§3.4（线程/确定性）、§4（值模型）；一切接口方法**非异常出口**（返回结果对象＋诊断列表），调用方错误以错误码 fail-fast 语义返回（不抛越过单元边界的异常；R-2 只暴露公共头）。

#### 9.4.1 `IRobotDesignEditor`（编辑器；MDL-01/07/09，插件界面唯一写入口）

```cpp
/// 建模工作集：已应用基线闭包的只读视图 ＋ 未应用编辑差值的演算结果。
/// 非线程安全：仅 UI 线程访问。生命周期由插件面板持有。
class IRobotDesignEditor {
public:
    virtual ~IRobotDesignEditor() = default;

    /// @brief 读取当前工作集（含派生只读字段的最新重算结果）。
    /// @return 只读引用；有效期至下一次 applyEdit/undo/redo。
    virtual const ModelingWorkingSet& workingSet() const noexcept = 0;

    /// @brief 应用一次编辑（字段级/对象级/批量变体，§5.2）。
    /// @param edit 编辑变体（值语义，含来源与可选确认选项——如平行轴迁移选择）。
    /// @post 接受时：权威值更新＋派生缓存同步重算＋工作集本地版本号 +1（undo 栈入栈）。
    ///       拒绝时：工作集字节不变＋逐项诊断（含比较型三要素，UX-03）。
    /// @错误 EditOutcome.errorCode：DuplicateObjectId|RefProtected|AuthorityViolation|
    ///        UnitIllegal|NotFinite|CentroidEditUnresolved|BatchPartial(附逐行定位)
    virtual EditOutcome applyEdit(const ModelingEdit& edit) = 0;

    /// @brief 草稿局部撤销/重做（工作集版本栈；零修订、零落盘）。无可撤销返回 false。
    virtual bool undoLocal() noexcept = 0;
    virtual bool redoLocal() noexcept = 0;

    /// @brief 生成自基线以来的变更摘要（人读中文；命令提交时并入命令摘要）。
    virtual std::string buildChangeSummary() const = 0;

    /// @brief 脏标记与基线信息（供 ui DraftController/IModuleDraftSource 消费）。
    virtual ModelingDraftStatus draftStatus() const = 0;   // {dirty, baseRevisionId, editsSinceBase}
};
```

- **前置条件**：`loadBaseline(closureView)` 已成功（基线闭包来自②查询端口，含部件解引用）；**后置条件**：任何接受编辑后 `workingSet()` 与逐项诊断一致（不变量 I-MDL-1～12 全部维持）。
- **副作用**：仅内存；**写权限**：无（不触达 project）；**所有权**： ModelingWorkingSet 由编辑器持有，调用方不接管。
- **合法调用示例**：`editor.applyEdit(SetJointFieldEdit{j2, Field::qmax, SourcedValue::provided(1.5707963, user)})`；**非法**：DH 权威态下 SetJointField(Field::axis)（C-1 拒绝）。

#### 9.4.2 `IRobotDesignTemplateFactory`（模板；MDL-01、P-03）

```cpp
/// 模板工厂：纯函数服务（无状态、可重入、确定性）。
class IRobotDesignTemplateFactory {
public:
    /// @brief 模板清单（七轴模板 P-03 冻结前 enabled=false，创建入口阻止）。
    virtual std::vector<TemplateDescriptor> listTemplates() const = 0;

    /// @brief 由模板创建初始工作集（含 BasePlacement、默认参数表 T-MDL-1、来源标记）。
    /// @pre  templateId 存在且 enabled；localName 非空且合法字符集。
    /// @post 产出满足 I-MDL-1~12 的工作集；不触达 project、不产生修订。
    /// @错误 TemplateDisabled|IllegalName（附定位诊断）
    virtual ModelingWorkingSet createDraft(TemplateId id, InstallationPresetToken preset,
                                           std::string_view localName,
                                           std::vector<core::DiagnosticRecord>& diags) const = 0;
};
```

调用方＝新建向导（workflow/ui，PM-01）。**非法调用**：七轴模板在 P-03 冻结前 createDraft→`TemplateDisabled`（不静默替换为六轴）。

#### 9.4.3 `IModelImportMapper`（导入映射；MDL-03/11/12/18/19 后段）

```cpp
/// 导入映射器：纯函数服务。输入全部来自 io 已验证产物；绝不自行读文件（io.md §10.5）。
class IModelImportMapper {
public:
    /// @brief URDF 业务映射（§6.3/§6.4）。
    /// @pre  source 为 io open/snapshot/dependencyTree 产物（字节＋快照＋依赖树，无环）。
    /// @post 产出草稿工作集＋导入报告（映射/默认补全/忽略/不支持/分支报告/待确认清单/
    ///       资源状态表）；不产生修订、不落盘。同输入字节+同 options → 同输出字节。
    /// @错误 ImportErrorCode（UnsupportedJointType|MimicBlocked|ZeroAxisReported|
    ///        MultiBranchNeedsSelection|ResourceMissing(转 Recorded 记录)|NameConflict）
    virtual ImportOutcome mapUrdf(const ValidatedSource& source, const ImportOptions& options,
                                  std::vector<core::DiagnosticRecord>& diags) const = 0;

    /// @brief Xacro 展开后映射（展开产物为 URDF 形态，走 mapUrdf 同一边界；来源记录随草稿）。
    virtual ImportOutcome mapXacroExpanded(const ValidatedSource& expanded,
                                           const XacroProvenance& provenance,
                                           const ImportOptions& options,
                                           std::vector<core::DiagnosticRecord>& diags) const = 0;

    /// @brief WorkCell 反向导入（R2/阶段 D；有损提取＋提取报告，MDL-18）。R1 返回 NotImplemented 稳定诊断。
    virtual ImportOutcome mapWorkCellXml(const ValidatedSource& source,
                                         const ImportOptions& options,
                                         std::vector<core::DiagnosticRecord>& diags) const = 0;
};
```

**线程**：可重入多线程；**确定性**：同字节同选项同结果（含诊断顺序——按源文件行序稳定排序）；**所有权**：ImportOutcome 内草稿与报告值语义归调用方。

#### 9.4.4 `IModelReadinessChecker`（就绪校验；MDL-06、§8.2）

```cpp
/// 就绪校验器：纯函数。逐层 L0→L11（§8.2 分层表），结果稳定排序（按层号→对象 id 字典序）。
class IModelReadinessChecker {
public:
    struct CheckContext {
        const policy::EngineeringPolicySet* resolvedPolicy;   // ④端口解析结果（L11/行程比较）
        bool writable;                                        // 只读模式提示（L11 呈现级）
        std::optional<core::RevisionId> baseRevision;         // 应用预检用（L11）
    };
    /// @brief 全量分层校验。不抛异常；结果含 blockers/warnings/confirmables 三组与逐层明细。
    /// @post 不修改输入工作集；不产生修订；不写诊断目录（呈现归调用方）。
    virtual ModelReadinessReport check(const ModelingWorkingSet& ws,
                                       const CheckContext& ctx) const = 0;
};
```

- **与 prepare 断言的关系**：checker 是"预检视图"；命令 prepare 在事务上下文内**重新执行**同等断言（防基线漂移）——两处共用同一断言实现（`AssertionSuite`，`CommandHandlers.hpp` 内部组件），不得出现两套判定（NFR-MNT-04）。
- **非法调用**：ctx.resolvedPolicy 为空且工作集含有限限位旋转关节→L11 报 Blocking"策略不可解析"（不静默跳过行程校验）。

#### 9.4.5 `ICanonicalModelInputBuilder`（Description 构造；§9.1/§9.2）

```cpp
/// 规范输入构造器：从修订闭包构造 runtime Description（reader 的内核，纯函数）。
class ICanonicalModelInputBuilder {
public:
    /// @brief 解引用根对象引用表并生成 Description（全 SI；DH 权威先展开）。
    /// @pre  closure 含根对象（token=robot-design，runtime kRobotDesignObjectType 路由）；
    ///       部件/资源经同一闭包源解析。
    /// @post 同闭包→同 Description 字节（确定性）；单位 SI 化唯一经 core Units。
    /// @错误 ModelingErrorCode（RefMissing→runtime InputInvalid 预演|UnitIllegal|
    ///        DhExpandFailed|SchemaVersionUnsupported）
    virtual runtime::Expected<runtime::RobotDesignDescription, ModelingError>
        build(const ObjectClosureView& closure,
              std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/// runtime 侧 reader 实现（modeling 提供、L5 注入；闭包域字节源装配期绑定，§9.2）。
class RobotDesignReader final : public runtime::IRobotDesignReader {
public:
    runtime::Expected<runtime::RobotDesignDescription, runtime::RuntimeError>
        read(const std::vector<std::uint8_t>& objectBytes,
             std::uint32_t objectTypeFormatVersion) const override;   // 内部委托 builder
};
```

#### 9.4.6 `IPropertyEstimator`（物性估算；MDL-05）

```cpp
/// 物性估算器：纯函数；唯一公式表版本 mdl-property-formula/1（§5.3）。
class IPropertyEstimator {
public:
    /// @brief 逐段估算并合成为连杆物性（质心系、连杆系姿态——M-2）。
    /// @pre  segments 非空；尺寸>0 且有限；密度>0（材料表查询失败→密度 NotProvided 报告）。
    /// @post 输出自检：对称＋SPD＋三角不等式（自检失败=内部错误码，不输出非法张量）；
    ///       来源标记 GeometricEstimate＋methodTag。
    /// @错误 EstimateErrorCode（IllegalDimension|MaterialDensityMissing|SynthesisFailed）
    virtual EstimateOutcome estimateLink(const std::vector<SegmentSpec>& segments,
                                         std::vector<core::DiagnosticRecord>& diags) const = 0;
    /// @brief 平行轴迁移（质心修改确认流选项 a；§5.3 规则 2 的实现）。
    virtual InertiaTensor migrateByParallelAxis(const InertiaTensor& atCom,
                                                double massKg,
                                                const rw::math::Vector3D<double>& deltaM) const noexcept = 0;
    virtual std::string_view formulaVersion() const noexcept = 0;
};
```

#### 9.4.7 `IDhExplicitConverter`（DH↔显式；MDL-02/09/10）

```cpp
/// DH 转换器：纯函数；五状态判定（§7.5）；容差唯一来自附录 D 第 4/5 项（不私设）。
class IDhExplicitConverter {
public:
    /// @brief DH 权威→显式表示（无损展开；§7.4）。
    /// @post 输出 axis 单位向量（归一化）、origin=T_parent_joint；roundtrip 基础。
    /// @错误 DhErrorCode（ChainEmpty|DegenerateBase——基座变换混入即拒绝）
    virtual ExpandOutcome dhToExplicit(const DhChain& chain,
                                       std::vector<core::DiagnosticRecord>& diags) const = 0;

    /// @brief 显式→DH 五状态判定（两阶段；§7.5）。确定性求解（固定初值/迭代上限/字典序选解）。
    /// @post Approximate/NotExpressible/AnalysisFailed 均不产生可置权威的参数；
    ///       Exact/ExactNonUnique 须再经等价验证（§7.6）方可切换权威——本方法不执行切换。
    virtual DhConversionResult explicitToDh(const std::vector<JointEntry>& explicitJoints,
                                            std::vector<core::DiagnosticRecord>& diags) const = 0;

    /// @brief 等价验证：两套权威参数化（同链）经 Description 构造＋FK 对照（附录 D 第 4 项）。
    /// @note  编译分段入口由调用方注入（只读、不发布快照，§7.6）。
    virtual EquivalenceReport verifyEquivalent(const ModelingWorkingSet& a,
                                               const ModelingWorkingSet& b,
                                               const CompileProbe& probe) const = 0;
};
```

#### 9.4.8 `IModelingCommandHandler`（命令处理器基类；project `ICommandHandler` 实现）

```cpp
/// 建模命令处理器基类：封装 §9.3 prepare 管线公共段（解码/基线重建/断言套件/计划组装），
/// 子类只声明 commandType/payloadVersion/对象写入差异。生命周期：L5 装配期构造并注册进
/// HandlerRegistry（一次性），运行期只读（project.md §5.3.5/§6.5）。
class IModelingCommandHandler : public project::ICommandHandler {
public:
    /// @brief payload 版本演进点：schema 变更→+1；旧版本 payload 拒绝并给升级指引
    ///        （NFR-DEP-04 同口径；命令 payload 无自动升级器，须重新编辑）。
    std::uint32_t currentPayloadVersion() const final;

    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& envelope,
                                    const project::RevisionView& baseSnapshot,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>& diags) final;
protected:
    /// 子类钩子：差值解码与写入集表达（断言/确认/inverse/summary 由基类统一执行）。
    virtual DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                        const CommandPayload& payload,
                                        const ModelingWorkingSet& baseline,
                                        project::CommandPlan& out) = 0;
};
```

- **线程约束**：仅命令执行线程（project 串行槽）；**副作用**：仅产出 CommandPlan（真正写入/提交由 project 事务完成）；**写权限**：经①端口，无其他写路径。
- **合法调用**：仅 project 命令服务调用 prepare；**非法**：业务/UI 直接调用处理器（装配注册后由 registry 分发）。

#### 9.4.9 支撑接口（概要契约，同 §9.4.1 约束框架）

| 接口 | 职责 | 关键签名（要点） |
| --- | --- | --- |
| `IRobotDesignCodec` | 建模对象 canonical 编码/解码（版本化；确定性序列化登记，core §6.3 分工） | `Expected<Bytes> encode(const ObjectVariant&, FormatVersion) const`；`Expected<ObjectVariant> decode(Bytes, FormatVersion) const`；未知主版本→`SchemaVersionUnsupported`（NFR-DEP-04） |
| `IXacroExpandService` | Xacro 受控展开语义（宏/参数/属性替换；护栏经 io） | `ExpandOutcome expand(const ValidatedXacroSource&, const SubstitutionMap&, Diags&) const`；未定义宏→行列定位诊断；绝不执行任意代码 |
| `IModelDiffService` | Model Diff 数据实体（MDL-08；两工作集输入，稳定排序增量表） | `ModelDiffReport diff(const ModelingWorkingSet& baseline, const ModelingWorkingSet& candidate) const`；分组：结构/参数（DH、轴线、限位）/物性；条目含对象定位（供 UX-13 点击） |
| `IModelPackagePort` | MDL-20 规范模型包导出/导入（io ZipChannel/AtomicFile 承载） | `ExportOutcome exportPackage(const ObjectClosureView&, const ExportTarget&, Diags&) const`；`ImportOutcome importPackage(const ValidatedSource&, Diags&) const`；非本软件工件→引导 MDL-18/R2 稳定诊断 |

### 9.5 modeling 稳定诊断码登记表（`MDL-` 前缀；装配期经 `IDiagnosticRegistry::registerCode` 注册，ownerUnit=`modeling`）

注册纪律（diagnostics.md §4.5）：码值 `^[A-Z0-9]+(-[A-Z0-9]+)*$` ≤64；一经注册并进入持久化产物不改义不改拼；**不预建无消费者条目**——下表每码随对应任务（§11）注册并附带 CodeDescriptor（category/severity/titleKey/detailKey/paramSchema/confirmable/retryable/…）。

| 码 | 类别/级别 | confirmable | 语义（触发→建议动作） | 任务 |
| --- | --- | --- | --- | --- |
| `MDL-06-TRAVEL-LIMIT` | 比较型/Warning | **true** | 有限限位旋转关节行程超阈值（实际行程/阈值/rad；阈值归 policy）→确认放行或改行程 | T08 |
| `MDL-ASSERT-MASS-NONPOSITIVE` | 断言/error | false | 已提供质量 m≤0 → 修正质量或清空为缺失 | T08 |
| `MDL-ASSERT-INERTIA-NOT-SPD` | 断言/error | false | 惯量非对称正定（对称化后特征值≤0）→ 修正张量 | T08 |
| `MDL-ASSERT-INERTIA-TRIANGLE` | 断言/error | false | 惯性椭球三角不等式不满足 → 修正张量 | T08 |
| `MDL-ASSERT-LIMIT-INTERVAL` | 断言/error | false | qmin≥qmax（有限限位可动关节）→ 修正限位 | T08 |
| `MDL-ASSERT-RANGE-NOT-FINITE` | 断言/error | false | continuous 工程工作范围未确认或非有限区间 → 确认范围 | T08 |
| `MDL-READINESS-REF-MISSING` | 校验/error | false | 引用对象不在闭包/token 不匹配 → 修复引用 | T08 |
| `MDL-READINESS-RESOURCE-STATE` | 校验/Warning | false | Recorded 资源缺失/变化或未固化 → 重关联/固化（CON-03） | T08 |
| `MDL-READINESS-SCHEMA-UNSUPPORTED` | 校验/error | false | 对象 schema 主版本超出本程序支持 → 升级程序/重新编辑 | T02/T03 |
| `MDL-IMPORT-UNSUPPORTED-JOINT` | 导入/error | false | mimic/planar/floating/闭环在所选主链 → 移除或改拓扑（不得绕过） | T05 |
| `MDL-IMPORT-BRANCH-SELECTION` | 导入/info | false | 多可动分支文件 → 显式选择主链 | T05 |
| `MDL-IMPORT-ZERO-AXIS` | 导入/error | false | 零轴/非有限轴关节 → 修正源文件（该草稿不得提交） | T05 |
| `MDL-IMPORT-PENDING-CONFIRM` | 导入/Warning | false | 待确认项未决（缺 axis 默认 +X/缺限位/工作范围）→ 逐条确认 | T05/T06 |
| `MDL-IMPORT-TEMPLATE-RANGE` | 导入/info | false | 所选主链 4/5 轴或含 prismatic，超出首版产品模板范围 → 草稿兼容编辑可用，模板创建与正式计算/报告阻断（类型保留不降级——V12-01；R1；MDL-12-S1 启用后仅六/七轴含 prismatic 放开） | T05（v0.6 实现期增登——§6.4"诊断'超出首版产品模板范围'"语义的码面落位，原表缺行） |
| `MDL-IMPORT-XACRO-UNRESOLVED` | 导入/error | false | Xacro 受控展开语义失败（未定义宏/未定义参数/缺参/签名外属性/不支持构造/宏重定义，宏名或参数名＋源行列）→ 修写源文件为受控子集（include/property/macro/调用；表达式与 $(...) 一律拒绝——绝不执行任意代码，io.md §6.2） | T06（v0.7 实现期增登——§6.5"展开失败→可定位诊断（宏名/行列）"的语义面码位；io 护栏码透传面已覆盖循环/缺失/预算三族，P-MDL-4"护栏 io／语义 modeling"口径下语义面归 modeling；原表缺行） |
| `MDL-IMPORT-PACKAGE-UNKNOWN` | 导入/error | false | 非本软件规范工件 → 使用 MDL-18/R2 通道（R1 引导提示） | T13 |
| `MDL-21-COUPLING-STAGE-LOCKED` | 校验/error | false | R1 配置耦合矩阵 → 阶段 D 启用前移除 | T18（R2 注册） |
| `MDL-DH-NOT-EXPRESSIBLE` | 转换/error | false | 链不满足 DH 结构前提（终判）→ 维持显式权威 | T09 |
| `MDL-DH-APPROXIMATE` | 转换/Warning | false | 近似解（附 E 度量与收敛状态）→ 不得置权威 | T09 |
| `MDL-DH-ANALYSIS-FAILED` | 转换/error | false | 求解器数值失败 → 调整后重试（不构成语义结论） | T09 |
| `MDL-EXPORT-FAILED` | 导出/error | false | 规范包导出失败 → 项目状态不变，检查目标路径/预算后重试 | T13 |
| `MDL-TEMPLATE-DISABLED` | 模板/info | false | 模板登记未启用（P-03 七轴模板工程数值未冻结，O-27 处置"仅登记不启用"）→ 选择已启用模板（如 generic-6r）；启用前须冻结数值并登记。§9.4.2"@错误 TemplateDisabled（附定位诊断）"的码面落位，paramSchema [template-id, freeze-gate] | T07（v0.8 实现期增登——表行序追加于表尾，登记簿纪律不重排既有行） |
| `MDL-READINESS-PHYSICS-MISSING` | 校验/Warning | false | 物性缺失（质量/惯量 NotProvided）→ DataInsufficient 降级预告（V15-01——缺失不触发硬断言、不伪造数值）；补全物性或接受降级。§9.3"物性缺失→不阻断，转 Warning 诊断随计划留痕"与 §8.2 L5"Warning（缺失——DataInsufficient 降级预告）"的码面落位（分类 DataInsufficient；paramSchema []），原表缺行 | T08（v0.9 实现期增登——表行序追加于表尾，登记簿纪律不重排既有行） |
| `MDL-REF-PROTECTED` | 校验/error | false | 移除被 defaultTcp 引用的工具被拒（I-MDL-9 引用保护/V-04：subject=被移除对象 oid；比较型＝引用计数 actual=1/expected=0、单位 1 无量纲）→ 先解除 defaultTcp 引用再移除；对象字节与旧修订闭包不受影响（PA-2/CON-02）。§4.8"被本域引用（如 defaultTcp 指向该工具）时移除被拒（I-MDL-9）"的码面落位（分类 InputInvalid；paramSchema [object-id, reference-holder]），原表缺行 | T10（v0.12 实现期增登——表行序追加于表尾，登记簿纪律不重排既有行） |
| `MDL-READINESS-DEFAULT-TCP-INCOMPLETE` | 校验/error | false | defaultTcp 引用完整性（I-MDL-9/KIN-14/§8.2 L7"工具与 TCP 完整"）：有工具引用而 defaultTcp 未设置／toolOid 不在 toolRefs／tcpKey 不在被引工具 tcpList → 修复 defaultTcp 或工具 TCP 表（就绪校验 L7 与命令 prepare 断言共用同一实现——§9.4.4/NFR-MNT-04；分类 ResourceMissing 与 REF-MISSING 同族；paramSchema [tcp-key, tool-id]），原表缺行 | T10（v0.12 实现期增登——表行序追加于表尾，登记簿纪律不重排既有行） |

域错误→码映射（diagnostics §8.8 分工）：`Errors.hpp` 中 `ModelingErrorCode` 枚举每值登记映射码（如 `AuthorityViolation→MDL-…` 无独立码时复用校验码族）；产码唯一经 `IDiagnosticFactory::create`（码已注册校验），禁字符串拼码。

### 9.6 资源引用与对象身份关系图

```
修订闭包（RevisionView.objectRefs，project 累积）
 ├─ (oid₁,cv₁) robot-design ──── builtFrom=SHA256(bytes₁) ─┐
 ├─ (oid₂,cv₂) tool-definition ◄── toolRefs[0]（引用，不嵌内容）  │
 ├─ (oid₃,cv₃) scene-object ◄──── sceneRefs[0]                 │ runtime 编译闭包读取
 ├─ (oid₄,cv₄) named-pose-set ◄── poseSetRef（不进 Description） │（IObjectBytesSource）
 ├─ (oid₅,cv₅) robot-drivetrain ◄ drivetrainRef                │
 ├─ (oid₆,cv₆) resource-object(Solidified) ◄─ resourceManifest ┘
 └─ ExternalResourceRecord{absPath+digest}(Recorded──只在外部引用记录层，非对象)

身份规则：ObjectId=稳定身份（ARC-04）；ContentVersion=project 对字节 SHA-256；
         路径≠身份；"删除"=移除引用（对象字节永久保留，PA-2）。
```

### 9.7 插件界面设计与界面逻辑（`sdurws_ird_modeling_plugin`；MDL-07、UX-05/06/07）

**定位与分工**：插件界面是建模计算库的唯一交互前端——只消费 §9 公共接口与 ui 平台契约，不持有计算逻辑与业务判定（ARCH §3.3）；工作台壳、五区布局、命令注册表/快捷键权威、确认对话桥、三维视图契约归 ui 单元（ui.md），本节只设计 modeling 域面板的信息架构、控件接线与交互流。旧多页签 UI 的功能映射见 §2.5（v0.2 增补，回应"本卡缺 UI 设计"的评审意见——此前 v0.1 仅在 §3.1/§11 T15 登记插件目标与任务行，未展开界面设计）。

#### 9.7.1 域面板组成与信息架构（挂接 UX-09 五区布局）

| 面板区 | 内容 | 消费契约 |
| --- | --- | --- |
| 建模结构树（左栏对象树的建模节点） | 模型根→基座安装→关节链（串联序）→连杆→工具/场景/位姿集/传动分组；节点锚＝ObjectId，显示 localName 与工程用语标签（UX-02：不显示哈希/内部 token） | ②查询端口基线闭包＋ui SelectionModel（选中只写会话态） |
| 属性编辑区（右栏） | **选中对象只显示相关属性**（MDL-07）：关节→类型/轴/零位/限位/速度/加速度（DH 权威下轴/原点灰显只读，§7.2）；连杆→物性（来源徽标：用户/估算/导入——ValueProvenance 投影）＋几何引用；工具→安装接口/TCP 列表；场景→世界位姿/角色；传动→比率/摩擦/力矩 | §9.4.1 `workingSet()`＋ui FormEditCommon（数值＋单位同显、非法就地原因并保留原值、批量粘贴——UX-05） |
| 域工具区（阶段面板，StageId=`modeling`） | 模板新建、URDF/Xacro 导入向导域侧页、权威模式切换、物性估算、几何生成辅助、Model Diff、规范包导出/导入、（R2）WorkCell 导入入口 | ui ICommandRegistry（命令经 `PluginUiDescriptor.commands` 装配登记，§9.7.3） |
| 就绪与诊断条 | L0～L11 分层结果（Blocking/Warning/Confirmable 计数＋逐项定位跳转到树节点）；建模域七态投影数据源 | §8.2 `ModelReadinessReport`＋`IPluginUiModule::readonlyProjections()`＋IDiagnosticSink |
| 预览页 | **仅基于已应用修订**的只读预览：WC/DWC XML 导出预览（MDL-20 外供内容）、模型几何查看（消费 RuntimeSnapshot 只读视图）。编辑态即时 XML 预览不提供（§2.5/D-MDL-10——草稿不编译） | runtime `IRuntimeModelView`（经 ui/View3D 契约） |

#### 9.7.2 界面逻辑（控件↔计算库接线，十条数据流）

| # | 交互 | 数据流（→＝调用方向） | 契约锚 |
| --- | --- | --- | --- |
| L-1 | 选中联动（三维拾取↔树↔属性区） | 树点击/View3D ray-cast 拾取→ui SelectionModel（写会话态，零修订）→属性区只读投影刷新；反向"定位"：属性区/诊断条跳转→树滚动＋三维高亮目标（ObjectId→名称端口取显示名） | ui §4.2/View3DContract；MDL-07；KIN-06 |
| L-2 | 字段编辑流 | 控件提交→`applyEdit(ModelingEdit)`→接受：树/属性区/就绪条增量刷新＋`notifySessionDirty`（脏标记→标题 `*`，PM-04/PM-11）；拒绝：就地错误（比较型三要素）＋保留原值，不弹模态 | §9.4.1；UX-03/05/07 |
| L-3 | 应用（提交）流 | `draft.apply`（ui 既有命令）→`IPluginUiModule.buildDraftCommand("modeling")`→①端口 submit；有 ConfirmableFinding 时 project 经 `ICommandInteraction` 回调→ui CommandInteractionBridge marshal 到 UI 线程呈现确认对话（行程上限：实际行程/阈值/单位 rad——UX-03 三要素）→凭据回传→修订产生或拒绝 | §9.3；SA-15；ui CommandInteractionBridge |
| L-4 | 修订事件刷新 | 订阅⑤事件（修订产生/失效通知）→重载基线闭包→工作集按"基线＋未应用编辑"保序重演（重演失败提示手工处置，不静默丢弃编辑）→全面板刷新 | §9.4.1；⑤事件端口 |
| L-5 | 撤销/重做（两级并存） | 项目级：命令面板/快捷键→UndoRedoService（逆命令新修订）；草稿级：编辑器工具条 undoLocal/redoLocal（零修订零落盘）；两级入口同屏且文案明确区分（PM-18/REQ-11） | §9.3；ui IDraftController |
| L-6 | Stale 冲突对话 | Rejected(stale-revision)→ui"草稿基线冲突"对话（[基于当前版本重新编辑]/[查看差异对象]/[取消]）；建模侧经 ModelDiffService 供给 baseRevision vs tip 差异定位数据 | §9.4.9；ui §8.5；PM-04 |
| L-7 | 只读模式 | writable=false→编辑控件禁用、域命令 `readOnlyAllowed=false`；浏览/选中/单位切换/预览可用 | ui §7.6；PM-07 |
| L-8 | 平行轴确认 | 质心编辑→`CentroidEditUnresolved`→面板**内联**二选一（平行轴迁移/覆盖完整张量，附迁移预览数值）→带选择重提 applyEdit（内联非模态优先，表单级确认 UX-07） | §5.3 规则 2 |
| L-9 | 权威模式切换流 | 面板发起→显式→DH 先行五状态判定→结果页（逐关节逐项偏差表＋E 度量＋收敛状态）→Exact/ExactNonUnique 才允许发起切换命令（等价验证在 prepare 内执行）→Approximate/NotExpressible 阻断呈现并说明 | §7.5/§7.6；AT-16 |
| L-10 | 导入向导域侧页 | 选择文件（io 预检）→映射报告页（分组：字段映射/默认补全/忽略项/不支持项/分支报告＋显式选链/待确认项逐条决议/资源状态表）→确认→草稿落盘→draft.apply | §6.1/§6.4；PM-01 |

#### 9.7.3 域命令登记清单（装配期经 `IPluginUiRegistrar`；CommandId 点分小写＝ui CommandId 词表，与 project commandType 无点词表是两套命名空间，不混用）

| CommandId | 类别/作用域 | 语义 | readOnlyAllowed |
| --- | --- | --- | --- |
| `modeling.new-from-template` | Stage/Project | 模板新建（TemplateFactory→草稿） | false |
| `modeling.import-urdf` / `modeling.import-xacro` | Stage/Project | 导入向导（io 预检→§6.1 管线） | false |
| `modeling.switch-authority` | Stage/Project | DH↔显式权威切换流（L-9） | false |
| `modeling.estimate-properties` | Stage/Project | 选中连杆批量物性估算 | false |
| `modeling.generate-placeholder-geometry` | Stage/Project | 连杆占位几何生成辅助（§5.2） | false |
| `modeling.diff-baseline` | Stage/Project | 与基线 Model Diff 查看 | true |
| `modeling.export-package` / `modeling.import-package` | Stage/Project | MDL-20 规范包导出/导入 | true / false |
| `modeling.reset-home-zero` | Session | 复位 Home/Zero（会话姿态，KIN-06 语义，零修订） | true |

（`draft.apply`/`draft.save` 等项目级命令由 ui 既有注册承载，modeling 只供给 `buildDraftCommand`；快捷键绑定一律经 ui HotkeyBindingTable，插件不私占全局快捷键——SA-16。）

#### 9.7.4 线程与刷新约束

- 编辑器/工作集仅 UI 线程访问（§3.4）；命令提交后 UI 不阻塞（命令执行槽全局串行），完成经修订事件回sync；确认回调由桥接器 marshal 至 UI 线程（project P-PR-7）。
- 刷新一律事件驱动（修订/失效/任务事件），禁止轮询；三维视图消费 RuntimeSnapshot 只读视图，**插件不直接调用 RobWork**（ARC-03）。
- 面板不得缓存模型权威数据——每次投影从 `workingSet()`/查询端口取值，防止 UI 副本成为第二真值（§2.4）。

## 10. 验证方案及故障注入矩阵

### 10.1 总则

- 验证分四组：**V-T 模板与编辑**、**V-I 导入**、**V-C 转换与一致性**、**V-S 状态与失效**；每行给出需求/AT 依据、前置、操作、预期、观测点。**未执行的测试不得标注通过**（AGENTS §4.2）；本卡为设计，全部用例随 WP-13-T16 及各实现任务执行留痕（gtest XML＋`ird-test-report.json`＋构建日志）。
- 计算库行为用 gtest 单元/契约测试覆盖（进程内、确定性）；插件界面交互按 AGENTS.md 的 Windows GUI 测试规程设计（VS x64＋`QT_QPA_PLATFORM=windows`、逐个绝对路径启动），**本次不启动 GUI 程序**，GUI 用例仅登记流程。
- 黄金数据集（WP-13-T16，testkit 契约 `ird-golden-manifest/1`）：`testdata/golden/mdl-template-6r/`（六轴模板参数锁定）、`mdl-dh-equivalence/`（DH/显式等价——AT-16）、`mdl-not-expressible/`（不可表达样本——AT-16）、`mdl-urdf-import/`（导入映射——AT-15/17）；容差档案 `testdata/tolerance/mdl-dh/`（锚定附录 D 第 4/5/6/7 项，固定类不放宽）；边角样例强制含零值/近零值/正负抵消（C4）。

### 10.2 故障注入矩阵

| # | 组 | 场景 | 依据 | 前置 | 操作 | 预期结果 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| V-01 | T | 六轴模板创建→应用 | MDL-01、AT-01/20 | 空项目可写 | 向导选 generic-6r×ground→创建确认→命令提交 | 恰好 1 个新修订；T-MDL-1 参数入库；7 连杆 6 关节闭合 | 修订计数；对象 token=`robot-design`；摘要含模板标识 |
| V-02 | T | 七轴模板登记不启用 | P-03、MDL-01/02 | 同上 | listTemplates→尝试创建七轴 | `enabled=false`；创建入口阻止＋提示；无半成品 | TemplateDisabled 诊断；不产生修订 |
| V-03 | T | 参数编辑与整体撤销 | MDL-07/09、PM-18、AT-29 | V-01 已应用 | 改 J2 qmax→应用（新修订）→项目撤销→重做 | 撤销/重做各产生**新修订**；历史不改写；axis 改动触发"下游需重算"提示（MDL-09） | 修订链父指针；HEAD 字节不变性；重算提示事件 |
| V-04 | T | 删除被引用对象 | I-MDL-9、CON-02 | 工具被 defaultTcp 引用 | RemoveObjectRef(该工具) | 拒绝＋比较型定位诊断；对象字节与旧修订闭包完整 | RefProtected 错误码；诊断 subject=oid |
| V-05 | T | 删除未引用对象＝引用移除 | §4.8、PA-2 | 场景对象无引用 | 移除引用并应用 | 新修订；对象库旧字节仍在；闭包不再含新修订对该对象的新引用 | 修订 objectRefs；对象库文件仍在 |
| V-06 | I | URDF 黄金导入 | MDL-03/04/11、AT-15 | mdl-urdf-import 数据集 | mapUrdf→呈现报告→确认→应用 | 映射/默认补全/忽略/不支持项全部可观察；草稿→恰好一个修订 | 报告逐项计数与数据集期望一致；AT-15 断言 |
| V-07 | I | 多分支文件选链 | MDL-12、§2.1、AT-17 | 双分支 URDF | 导入→分支报告→显式选主链→维度二判定 | 辅助分支不构成拒绝；按所选主链处置 | 分支报告对象清单；判定结论 |
| V-08 | I | 缺失网格资源 | CON-03、NFR-REL-04、V-06 变体 | URDF 引用不存在网格 | 导入→就绪校验→转正式评估 | IO-RES-MISSING＋外部引用记录（Recorded）；应用可过（Warning）；正式评估被证据门禁阻断 | resourceManifest 状态；L6 Warning；evidence 门禁行为（跨单元观测） |
| V-09 | I | include/宏循环 | NFR-SEC-02、io §6.2 | 循环 include 的 xacro | expand | IO-FORMAT-XML-CYCLE 定位诊断；无草稿、无修订 | io 码透传；无对象写入 |
| V-10 | I | 不支持拓扑阻断 | MDL-12/M-6、AT-17 | 含 mimic/planar/floating 主链 | 导入→应用尝试 | 阻断＋MDL-IMPORT-UNSUPPORTED-JOINT；不得转 FixedFrame；无修订 | 断言失败清单；修订计数不变 |
| V-11 | C | DH↔显式 roundtrip | MDL-02/10、AT-16、附录 D 第 4/5 项 | mdl-dh-equivalence 三套 | 显式→DH（Exact）→等价验证→切权威→展开→再求 DH | 判定 Exact/ExactNonUnique；FK 对照 ≤1×10⁻⁹ m/rad；roundtrip 参数级一致 | 逐关节逐项偏差表；等价报告；五状态各含样例（含 NotExpressible 终判不进入求解、Approximate 附 E 与收敛态、AnalysisFailed 反例） |
| V-12 | C | 基座—世界变换不重复应用 | MDL-22/M-11、AT-37 | 倒挂模板模型 | 应用→编译→（联动 runtime 契约测试）三维渲染/环境几何/碰撞/重力投影读同一 R_world_base | modeling 侧断言：Description.base 只含 preset/参数，无任何预乘旋转矩阵；场景 worldPose 未被旋转 | Description.base 字节；AT-37 联合观测（runtime/policy 侧执行，本卡观测点=输入面） |
| V-13 | C | 三预设＋任意欧拉角安装编辑 | MDL-22、AT-37 | 任意模型 | 编辑 basePlacement（ground/inverted/wall/custom→customEaa）→应用 | 原子持久化（单命令单修订）；未配置路径默认地面（导入/空白模板来源标记核对） | 修订摘要；ImportMapped/UserProvided 来源 |
| V-14 | S | 双权威冲突拒绝 | MDL-02/09、§7.3 C-1/C-2 | DH 权威模型 | 编辑 axis | 调用方错误拒绝＋"派生只读"提示 | AuthorityViolation；工作集字节不变 |
| V-15 | S | 断言分域反例 | MDL-06/V15-01、AT-01 | 构造四个模型 | ①m=0 ②惯量非 SPD ③qmin≥qmax ④物性全缺失＋含已确认范围 continuous | ①②③就地阻断＋定位；④可应用（缺失走 DataInsufficient 预告；continuous 豁免限位断言） | 逐项诊断码（MDL-ASSERT-*）；④应用成功 |
| V-16 | S | 行程上限确认放行 | MDL-06④/M-10、AT-01 | ±1080° 多圈关节模型 | 应用（默认阈值 4π）→确认对话→确认/拒绝两分支 | 超限比较型诊断（实际行程/阈值/rad）；确认→放行＋摘要留痕；拒绝→无修订；阈值经工程策略配置后正常放行 | ConfirmableFinding 流转；命令摘要确认记录；policy 配置生效 |
| V-17 | S | 质量/质心/惯量平行轴确认 | MDL-05/M-2 | 已估算连杆 | 改质心（选迁移/选覆盖/两者都不选） | 前两者接受并留痕；第三者拒绝（CentroidEditUnresolved） | 编辑差值记录选择；摘要留痕 |
| V-18 | S | 传动耦合矩阵非法/病态与阶段锁 | MDL-21、§8.1、AT-38 | R1／R2 两态 | R1 配置 C→应用；R2 配置奇异阵/条件数 1×10⁹→应用 | R1：MDL-21-COUPLING-STAGE-LOCKED 阻断；R2：比较型诊断＋阻断；合法 C→应用成功且四消费者同源（联合观测） | 阻断诊断；修订拒绝计数；AT-38 联合（R2） |
| V-19 | S | 模型未就绪不能编译 | MDL-06、§8.2 | 含 Blocking 项模型 | 尝试应用 | 阻止＋定位；不触发编译端口；无修订 | prepare 返回 RejectedHardAssert；IModelCompilePort 未被调用（mock 观测） |
| V-20 | S | runtime 编译失败不产生修订 | MDL-06、ARC-03 | 故障注入 DWC 构造失败（契约测试桩） | 应用合法模型 | Failed(compile-failed)；HEAD 不变；RT-*＋上下文诊断入目录 | 修订计数；诊断码 RT-DWC-*；旧修订字节完整 |
| V-21 | S | 导入取消与失败无修订 | PM-01、UX-03 | 导入进行中 | 用户取消／预算超限 | 无草稿残留（或草稿按关闭对话处置）；正常取消无错误诊断；无修订 | drafts 目录；诊断目录无 error |
| V-22 | S | 草稿切换与 StaleRevisionRejected | PM-04、AT-29 | 草稿 baseRevision≠tip | 应用旧草稿 | Rejected(stale-revision)；草稿 apply-retained 保留；冲突定位数据呈现；重编辑后成功 | StoreError 码；草稿仍在 drafts/；ui 冲突对话数据 |
| V-23 | S | 项目切换后旧快照可追溯 | CON-02、§9.1 | 已有快照与历史结果 | 切换项目再切回 | 旧 RuntimeSnapshot 不被 modeling 撤销/失效；历史结果保留原 payload | 快照绑定修订身份；结果当前性归 evidence 观测 |
| V-24 | S | 显示单位变化不修改规范模型 | KIN-12、AT-27 | 任意模型 | 切换显示单位 m↔mm、deg↔rad | 不产生修订、不触发重算；模型对象字节不变（显示换算归 core/ui 投影） | 修订计数；对象 cv 不变 |
| V-25 | S | AT-05 失效矩阵输入面 | CON-05、AT-05 | 模型＋工具＋选型回填 | 分别改 TCP（工具对象）／电机成本（selection 域） | TCP 变更→工具对象新修订→运动学及下游失效（下游域观测）；成本变更→无 modeling 对象变更→运动学切片不失效 | 对象级 cv 变化范围；依赖键声明核对 |
| V-26 | S | AT-18 名称解析输入面 | ARC-04、MDL-14、AT-18 | 含特殊字符名模型 | 应用→编译（runtime 生成 NameMap） | modeling 输入面断言：localName 合法字符集、无重复；往返解析由 runtime 契约测试执行（本卡观测点=输入质量） | localName 清单；消歧报告 |
| V-27 | S | AT-04 会话预览无修订 | KIN-06、AT-04 | 打开模型面板 | 三维点选/双击/复位 Home/Zero（会话命令） | 零修订、零失效；位姿集参考数据读取不产生写 | 修订计数；会话态归 ui 观测 |
| V-28 | S | AT-30 选型回填输入面 | SEL-10、AT-30 | （阶段 C 联合） | selection 回填命令写 robot-drivetrain | schema 字段接受回填（目录版本/安装关系/壳体-转子分离）；modeling 侧只校验 I-MDL-11 | 回填后对象 cv；复算提示事件（联合观测） |
| V-29 | I/G | MDL-20 包 roundtrip | MDL-20、AT-28 | 已应用模型（含命名位姿/资源） | 导出包→重导入→逐项比对 | 权威参数化/物性/资源引用/碰撞规则/命名位姿逐项一致；导出失败时旧输出文件完好、项目状态不变 | 逐项清单 diff=空；AtomicFile 回滚观测 |
| V-30 | G | GUI 主流程（设计登记） | MDL-07、UX-05/07、§9.7 | Windows GUI 环境 | 关节树选择→属性表联动→批量粘贴→表单应用确认；两条界面数据流抽测（L-2/L-3） | 只显示选中对象相关属性；非法输入就地显示原因并保留原值；会话操作无保存提示；确认对话三要素齐全 | 面板截图＋诊断呈现；按 AGENTS GUI 规程执行（本次不启动） |

**AT 承接声明**：AT-01（V-01/03/15/16）、AT-04（V-27）、AT-05（V-25）、AT-15/17（V-06/07/10）、AT-16（V-11）、AT-18（V-26 联合）、AT-27（V-24）、AT-28（V-29 及场景/位姿行）、AT-29（V-03/22）、AT-30（V-28 联合）、AT-31（V-09 及 Xacro 黄金样例）、AT-33（R2，mapWorkCellXml 启用时补行）、AT-37（V-12/13 联合）、AT-38（V-18，R2）。凡"联合观测"行，本卡测试只负责 modeling 侧输入/输出面，跨域断言归对应域契约测试——不把未执行测试写为通过。

---

## 11. 阶段 B 实现任务拆分

DTB WP-13 任务包（v0.16 §2.14）为权威拆分，本卡不重排编号；下表给出本卡视角的**内部依赖顺序与产出落点**（"前置"为 DTB 登记的跨单元前置）：

| 任务 | 产出（本卡落点） | 前置（DTB） | 内部顺序 |
| --- | --- | --- | --- |
| WP-13-T01 | 本卡（units/modeling.md v0.1；含 P-RT-4/P-RT-5/P-03 交叉核对登记） | WP-06/07/04/11/09 各卡 | ①（已完成——本提交） |
| WP-13-T02 | `modeling/CMakeLists.txt`（STATIC 零 Qt＋`_test`/`_contract_test` 注册＋红线守卫＋门禁白名单六边登记，§3.2）；`ObjectTypes/Errors/DiagCodes.hpp` | T01、WP-03-T01 | ② |
| WP-13-T03 | `RobotDesign/Parts/Codec.hpp/.cpp`＋不变量 UT（I-MDL-1~12） | T02 | ③（核心，阻塞其后全部） |
| WP-13-T04 | `PropertyEstimation.hpp/.cpp`（公式表＋平行轴）＋UT（MDL-05/16） | T03 | ④a |
| WP-13-T05 | `Import.hpp/.cpp`（URDF 映射＋链型判定）＋UT（AT-15/17）——两提交 | T03、WP-11-T03 | ④b |
| WP-13-T06 | `XacroExpand.hpp/.cpp`（受控展开语义）＋UT（AT-31） | T05、WP-11-T03 | ④c |
| WP-13-T07 | `Template.hpp/.cpp`（六轴模板；七轴登记不启用）＋UT（AT-20 建模侧） | T03 | ④d |
| WP-13-T08 | `Readiness/CommandHandlers.hpp/.cpp`（断言分域＋五命令＋确认流）＋UT（AT-01） | T04、WP-04-T10/T11、WP-06-T11、WP-07-T08 | ⑤（依赖 ③④a） |
| WP-13-T09 | `DhConvert.hpp/.cpp`（五状态＋等价验证）＋UT（AT-16） | T03、WP-06-T04/T07 | ⑤ |
| WP-13-T10 | 工具/场景/命名位姿编辑（Parts 编辑流＋apply-tool-definition/apply-scene-objects/apply-named-poses）＋UT（AT-28） | T03 | ⑤ |
| WP-13-T11 | 基座安装姿态编辑（BasePlacement 编辑＋P-RT-4 交叉核对留痕）＋UT（AT-37 输入面） | T03、WP-06-T06 | ⑤ |
| WP-13-T12 | `CanonicalBridge.hpp/.cpp`（reader＋builder；P-RT-5 决议落地）＋契约测试（与 runtime RT-T12 协作） | T03、WP-06-T05/T07/T11 | ⑥ |
| WP-13-T13 | `Package.hpp/.cpp`（MDL-20 roundtrip）＋UT（AT-28） | T03、WP-11 | ⑥ |
| WP-13-T14 | `ModelDiff.hpp/.cpp`（MDL-08）＋UT（AT-12 数据实体面） | T03 | ⑥ |
| WP-13-T15 | `sdurws_ird_modeling_plugin`（面板信息架构/接线/命令清单按 §9.7 实现；ui 接入） | T03~T11、WP-10-T08 | ⑦ |
| WP-13-T16 | 契约测试套件＋三套黄金模型数据集（§10.1 清单；容差档案；边角样例）——**已落位**（v0.18）：test/ 三件两模式＋DhConvert 黄金件（集成门控）＋contract_test 对端件；数据集实为四套（本行 §10.1 清单为准——DTB"三套"计数表述偏差随下次 DTB 增量修订消账） | T05~T14、WP-02 | ⑧（收口，v0.18） |
| WP-13-T17 | WorkCell 反向导入（R2/阶段 D 启用时领取） | T05、WP-11-T06、阶段 D | R2 |
| WP-13-T18 | 传动耦合矩阵建模（R2/阶段 D；含 R1 阻断反例——阻断码已在 T08 注册） | T03、WP-18-T05、阶段 D | R2 |
| WP-13-T19 | MDL-12-S1 混合链启用（R2；前置 SEL-09-S1） | WP-19-T12＋跨域扩展 | R2 |

DoD 沿 DTB §5.2：双模式构建零错误、`ird_gates` 零命中、验收用例通过并留痕、文档同步零偏差（含本卡增量修订——接口签名以实现复核后的 v0.x 修订为准）。

## 12. 后续阶段承接与接口交接清单

### 12.1 阶段 C/D 消费接入（本卡只预留，不实现）

| 消费方（阶段） | 消费内容 | 本卡提供的契约面 | 接入约束 |
| --- | --- | --- | --- |
| requirements（B 并行） | 引用 `scene-object`/`tool-definition` ObjectId（REQ-04 负载/障碍） | §4.4/§4.5 对象 schema 与引用稳定性承诺 | requirements 侧就绪校验负责其引用完整性（跨域悬空检测，P-MDL-6） |
| kinematics（C 前置，B 尾） | 已应用修订的 RuntimeSnapshot（经 runtime） | robot-design 输入确定性（同修订→同 Description→同 CanonicalModel） | 评估器消费快照而非编辑态（PA-3）；`model.robot-design` 等依赖键声明归评估器 |
| trajectory/dynamics（C） | 同上＋`robot-drivetrain`（摩擦/力矩限值输入） | §4.7 字段与来源标记；摩擦缺失→NotProvided（DataInsufficient 降级输入） | dynamics 自行判定可信等级（DYN-06），modeling 不代判 |
| drivetrain（C） | coupling（R2）＋ratio | §8.1 常矩阵口径；虚功映射唯一实现归 drivetrain | modeling 不实现映射（M-12） |
| selection（C） | `robot-drivetrain.catalogBackfill` 回填写入口 | schema 字段＋`apply-drivetrain-design` 命令（回填变体） | 回填命令处理器归属阶段 C 登记（selection 或共用本卡处理器扩展，随 WP-19 契约冻结） |
| optimization（B/D） | StageB 传动比变量、候选补丁→临时候选模型→同一编译链 | ratioPerJoint 为普通权威字段（候选不经修订，采用守卫归 OPT-08） | 优化候选编译消费同一 reader（P-MDL-6 同源） |
| workflow/ui | 建模阶段就绪投影、草稿、命令注册 | DomainReadinessItem 数据、IPluginUiModule 实现、命令清单（§9.3） | 门控判定归 workflow；七态求值归 ui（modeling 只供数） |
| reporting（C） | 模型章节输入（权威参数化/物性/来源标记） | 对象库只读（经②端口）＋导入/确认摘要留痕 | 报告措辞冻结（RPT-05 限定语）在 reporting 侧 |

### 12.2 R2 条目承接（PA-7：既有机制扩展，无新架构机制）

| R2 条目 | 承载 | 本卡预留 |
| --- | --- | --- |
| MDL-18 WorkCell 反向导入 | modeling 导入通道＋io 通用 XML 通道＋runtime 编译链（不经——runtime.md §13.1） | §6.6 边界；`mapWorkCellXml` 返回 NotImplemented 稳定诊断（R1） |
| MDL-21 传动耦合矩阵 | robot-drivetrain 扩展＋编译链＋drivetrain 虚功映射 | §4.7 字段＋§8.1 口径＋R1 阻断码（T08 注册） |
| MDL-12-S1 六/七轴含 prismatic | 链型判定扩展（§6.4 维度二开关）＋SEL-09-S1 前置 | 判定函数参数化启用位（enablement token），R1 恒 false |
| PM-04-S1 草稿周期可调 | ui DraftController | 无 modeling 侧改动 |

### 12.3 交接清单（双向义务，供各对端卡登记）

| 对端 | modeling 收到 | modeling 交出 |
| --- | --- | --- |
| project（§13.2 承接） | ①命令提交协议、HandlerContext、draft 模块 token（`modeling`）、inverse 记录义务 | 五命令 payload canonical 序列化登记、prepare 断言实现、逆命令表达、命令清单与 requiresDualCompile 声明 |
| runtime（§13.2 承接） | RobotDesignDescription 契约、kRobotDesignObjectType 路由、编译诊断定位 | `IRobotDesignReader` 实现（含 P-RT-5 决议）、descriptionContractVersion 演进承诺、MDL-10 等价验证协作（RT-EQ 数据集）、P-RT-4 预设轴向确认 |
| io（§10.5 承接） | 文件层读取/依赖树/预算/快照、护栏、AtomicFile、固化执行 | URDF 字段映射与报告、WorkCell 有损提取（R2）、Xacro 展开语义、MDL-20 包 profile 注册 |
| policy（§13 承接） | 行程上限比较型结果（IJointLimitEvaluator）、策略只读解析 | 导入自碰撞配置→策略草稿候选输入（RawPolicyInput 来源之一——交接 API 待 P-MDL-3）、SceneObjectRole 词表（modeling 所有） |
| diagnostics（§12.2 承接） | 注册协议＋工厂＋sink＋日志设施 | `MDL-*` CodeDescriptor 清单＋paramSchema＋文案键（随各任务注册）、域错误→码映射 |
| ui（§11 承接） | DraftController/注册器/就绪投影/七态词表 | `IPluginUiModule` 实现、模块草稿源、命令项、缺项文案键 |
| evidence（§13 承接） | 快照组装协议（请求方角色）、角色键约定 | 对象 canonical 编码登记（core §6.3 分工——`IRobotDesignCodec` 确定性规则）；"评估器不得声明 named-pose-set 依赖键"的提示性约定 |
| core | 全部公共契约（身份/来源/单位/比较/诊断/事件） | 无（纯消费） |

---

## 13. 需求—设计—验证追踪矩阵

| 需求/AT | 设计落点（本卡章节） | 验证（本卡行） | 主任务（DTB） |
| --- | --- | --- | --- |
| MDL-01 | §5.1、§6.4 | V-01/02 | T03/T07 |
| MDL-02 | §7.1～§7.3、§7.6 | V-11/14 | T03/T09 |
| MDL-03 | §6.1～§6.3 | V-06 | T05 |
| MDL-04 | §4.4～§4.6、§6.3 | V-06/29 | T07/T10 |
| MDL-05 | §5.3 | V-17 | T04 |
| MDL-06 | §8.2、§9.3 | V-15/16/19/20 | T08 |
| MDL-07 | §9.4.1、§9.7、§11 T15 | V-30 | T15 |
| MDL-08 | §9.4.9 | V-29 联动（AT-12 数据面） | T14 |
| MDL-09 | §7.2/§7.3 | V-03/14 | T03 |
| MDL-10 | §7.4～§7.6 | V-11 | T09 |
| MDL-11 | §6.3 | V-06/10 | T05 |
| MDL-12 | §6.4 | V-07/10 | T05/T08 |
| MDL-13 | §4.4 | V-04/25 | T10 |
| MDL-14 | §9.1 | V-26（联合 AT-18） | T12＋WP-06-T05 |
| MDL-15 | §4.5 | V-05/29 | T10 |
| MDL-16 | §4.7、§5.4 | V-28（联合） | T04 |
| MDL-17 | §4.6 | V-27/29 | T10 |
| MDL-18（R2） | §6.6 | R2 启用时补 | T17 |
| MDL-19 | §6.5 | V-09 | T06 |
| MDL-20 | §6.8 | V-29 | T13 |
| MDL-21（R2） | §8.1 | V-18 | T18 |
| MDL-22 | §4.3、§7.7、§9.1 | V-12/13 | T11＋WP-06-T06 |
| ARC-01/02 | §2.3、§9.3 | V-19/20 | T08 |
| ARC-03 | §9.1/§9.2 | V-11/20 | T12 |
| ARC-04 | §4.8、§9.1 | V-26 | T03/T12 |
| ARC-05 | §8.2、§9.3（④端口只读） | V-16 | T08 |
| CON-01/02/03/05/06 | §4.8/§4.9/§6.7/§4.2/§9.1 | V-05/08/23/25 | T03/T05/T08 |
| PM-01/04/12/18 | §5.1/§9.3/§9.3/§9.3 | V-01/21/22 | T07/T08 |
| UX-02/04/06/07/08 | §2.2、§9.4.9（插件消费约定） | V-30 | T15 |
| NFR-COR-01/02/03 | §10.1/§3.4/§4.10 | V-11 及全部确定性断言 | T16 |
| NFR-MNT-01/03/07 | §3.1/§3.4/§2.4 | 门禁（R-1/R-3）＋UT | T02 |
| NFR-SEC-01/02 | §6.1/§6.7 | V-08/09 | T05/T06 |
| AT-01 | — | V-01/15/16 | T08/T16 |
| AT-04 | — | V-27 | T15 |
| AT-05 | — | V-25 | T03（输入面） |
| AT-15/17 | — | V-06/07/10 | T05 |
| AT-16 | — | V-11 | T09/T16 |
| AT-18 | — | V-26（联合） | T12 |
| AT-27 | — | V-24 | T15（投影） |
| AT-28 | — | V-29 | T10/T13 |
| AT-29 | — | V-03/22 | T08 |
| AT-30 | — | V-28（联合，阶段 C） | T08（schema 面） |
| AT-31 | — | V-09 | T06 |
| AT-37 | — | V-12/13（联合） | T11 |
| AT-38（R2） | — | V-18 | T18 |
| 附录 D 第 4/5/6/7/11/12 项 | §7.5/§5.3/§8.1/§4.10 | V-11/15/16（容差档案） | T09/T16 |

---

## 14. 设计决策、风险、待裁决项与变更记录

### 14.1 设计决策登记（D-MDL-x；仅登记 modeling 所有权内的取舍，不改变上游语义）

| ID | 决策 | 依据/理由 | 影响面 |
| --- | --- | --- | --- |
| D-MDL-1 | RobotDesign 采用"根对象＋有限部件对象"分解（根/工具/场景/位姿集/传动），根只持部件 ObjectId 引用（不含 cv） | AT-05 失效方向、MDL-13 引用语义、MDL-17 不失效要求、CON-05 切片粒度；避免逐连杆对象爆炸 | §4.2；对端：runtime 闭包读取、evidence 依赖键 |
| D-MDL-2 | 连杆物性与几何内嵌根对象（不逐连杆拆对象）；接受"视觉几何微调→模型切片保守整体 Superseded"的粗粒度 | 对象数/闭包膨胀与 AT 级要求的权衡；CON-02 保证历史不丢 | §4.2 注记；P-MDL-2 与 evidence 措辞对齐 |
| D-MDL-3 | 命名位姿/Home/Zero 参考独立成 `named-pose-set` 对象，不进 Description/依赖键 | MDL-17"不改变权威模型"的可执行化；KIN-06 会话语义的数据源 | §4.6；对端：ui 会话、MDL-20 导出 |
| D-MDL-4 | P-RT-5 采纳 reader 注入形态；reader 经闭包域字节源解析部件对象 | 保持建模 schema 与 runtime Description 契约解耦（R-MDL-3 隔离层）；拒绝直解 | §9.2 |
| D-MDL-5 | 派生字段（DH 态的 axis/origin；显式态的 dhDerived）不入 canonical 编码身份，读取时确定性重算 | 消除双真值存储；身份只随权威字段变化 | §7.1 |
| D-MDL-6 | 命令 token 采用无点形态（`apply-robot-design` 等） | project 冻结语法 `^[a-z0-9-]{3,64}`；P-PR-9 未决期间取保守侧 | §9.3；若裁决点分则迁移（破坏性，随 schemaVersion） |
| D-MDL-7 | 六轴模板默认参数表 T-MDL-1 为设计默认值，黄金数据集数值锁定 | 向导示例项目（AT-20）需要确定基线；上游未冻结该组数值（P-03 仅涉七轴） | §5.1；WP-13-T16 |
| D-MDL-8 | "删除"＝引用移除（对象字节永不物理删除） | PA-2/CON-02；project CommandPlan 无移除原语——不需要扩上游契约 | §4.8 |
| D-MDL-9 | 撤销采用快照式逆命令（inverse 载荷＝受影响对象前一版本 canonical 字节集） | 确定性、与对象不可变模型同构；避免逆运算正确性风险（如 DH 切换不可逆运算） | §9.3 |
| D-MDL-10 | 未应用草稿不提供三维编译预览（R1 非目标） | 避免私设第二编译路径与"编辑态进计算"风险（§2.4） | §4.1 |
| D-MDL-11 | 就绪校验与命令 prepare 断言共用同一 `AssertionSuite` 实现 | NFR-MNT-04（无重复判定）；防 TOCTOU | §9.4.4 |

### 14.2 风险登记（R-MDL-x）

| ID | 风险 | 缓解 |
| --- | --- | --- |
| R-MDL-1 | 对端契约（core/project/runtime/policy/io/diagnostics/ui）均未冻结，签名可能漂移 | 本卡基线版本已在 §1.2 登记；冻结后按影响面增量同步（P-MDL-8）；实现任务按当周对端卡现状复核 |
| R-MDL-2 | 根对象权威内容随模型增长膨胀（全链＋物性＋几何引用单对象） | canonical 编码定长字段＋引用化资源；超限（如万级关节）非 R1 场景；需要时按需求变更拆分（与 D-MDL-2 同通道） |
| R-MDL-3 | 建模 schema 演进导致缓存/快照身份漂移 | `schemaVersion`↔`descriptionContractVersion` 单点演进（入缓存键，runtime 风险 R-3 同源）；主版本升级须前向升级器评估（NFR-DEP-04） |
| R-MDL-4 | DH 数值求解在退化族上的收敛鲁棒性 | 固定初值/迭代上限/字典序选解＋五状态互斥判定；AnalysisFailed 可观测不伪装；黄金数据集覆盖退化族（V-11） |
| R-MDL-5 | 跨域引用（requirements→工具）悬空在建模侧不可见（R-1） | 引用移除附提示＋事件；requirements 就绪校验兜底（P-MDL-6 协调） |
| R-MDL-6 | 五命令 payload 版本演进与旧草稿不兼容 | payloadVersion 拒绝＋重新编辑指引（无静默升级）；草稿属会话短命数据，损失可接受并如实提示 |

### 14.3 待裁决项（P-MDL-x；上游冲突集中登记——依据/影响/建议/裁决者）

| ID | 事项 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-MDL-1 | runtime v0.12②"Description 四子结构 objectId ∈ objectRefs"与 reader 单根字节输入的矛盾：关节/连杆子 ObjectId 并非真实存储对象，无法进入 objectRefs | runtime.md §4.2/§4.12②、§9.2 本卡决议 | S5/CM-0 复核口径；若强制成立则建模被迫逐关节/连杆建对象（对象数×3） | 确认 objectRefs 复核范围＝真实存储对象（根/工具/场景/位姿集/传动/资源），关节/连杆子 ID 为模型内标识（ERR-01 subject 与未来名称映射锚） | runtime 详设所有者（WP-13-T12 交叉核对留痕） |
| P-MDL-2 | evidence.md §5.3"几何与物性是不同对象（MDL-05 分层）"的对象级措辞 vs 本卡字段级分层＋保守失效 | evidence.md 行 405、本卡 D-MDL-2 | 视觉几何微调会把碰撞/运动学切片一并标 Superseded（保守、不违约但欠精细） | evidence 侧确认该行为字段级语义说明；若需对象级粒度，modeling 按需求变更拆分连杆对象 | evidence 详设所有者＋需求所有者（若拆分） |
| P-MDL-3 | 导入自碰撞配置（CollisionSetup）→policy 草稿解析入口（RawPolicyInput 来源之一）的交接 API 双侧均未定义 | policy.md §2.2 MDL-04 行、本卡 §6.3 | V-06 报告项落不了地；策略草稿预填断链 | policy 卡增补 RawPolicyInput 的"导入候选来源"装配形态（值数据交接，modeling 不写策略对象） | policy 详设所有者 |
| P-MDL-4 | Xacro 展开引擎所有权：io.md §13.3 自列"Xacro 展开机制实现（护栏已定）"为 io 阶段 B 增量，DTB WP-13-T06 将 `XacroExpand.*` 派给 modeling | io.md §12/§13.3、DTB §2.14 | 重复实现或接口真空 | 按"护栏 io／语义 modeling"切分：io 交付护栏原语确认单，modeling 交付展开语义引擎 | io 详设所有者＋DTB 维护者 |
| P-MDL-5 | URDF/Xacro XML 解析器依赖：io 不产出 DOM（expat 为其 PRIVATE 依赖），modeling 需 XML 解析能力 | io.md §6.1/§6.5、DTB §4.6（sdurw_loaders 不使用） | WP-13-T05/T06 无法开工或被迫手写解析器 | 建议经 vcpkg 引入 `pugixml`（modeling PRIVATE，BudgetGuard 前置使 DOM 有界），在 DTB 登记后启用；备选：io 增加通用 XML 树公共读接口（改 io 公共面） | DTB 维护者（依赖登记）＋io 所有者（备选方案） |
| P-MDL-6 | 跨域引用悬空检测与优化候选编译的数据源声明（requirements 悬空提示、optimization 候选走同一 reader） | 本卡 §4.8/§12.1 | 引用保护跨域闭环 | requirements 就绪校验兜底＋evidence/optimization 侧确认候选编译复用闭包 reader | requirements/evidence/optimization 各所有者（会签） |
| P-MDL-7 | 耦合矩阵条件数阈值 1×10⁸ 目前为 runtime 设计默认（P-RT-7），非附录 D 冻结值 | runtime.md P-RT-7、本卡 §8.1 | 阈值漂移将改变阻断行为 | modeling 引用 runtime/常量单源（不私设），P-RT-7 冻结时同步；如需用户可调走工程策略变更 | runtime＋policy 所有者 |
| P-MDL-8 | 本卡消费的全部对端契约均未冻结（§1.2） | 各卡状态列 | 实现期签名漂移返工 | 冻结后出 diff 增量同步（同 P-TK-2/P-DIAG-1 惯例）；实现任务以当周对端现状复核 | 各对端所有者（冻结触发） |
| （引用） | P-RT-4 预设轴向、P-RT-5 reader 形态、P-PR-9 命令 token 点分、P-UI-6 就绪源三方契约、P-DIAG-4 FindingId、P-03 七轴数值 | DTB §4/各卡 | 见对应章节交叉核对动作 | 本卡立场已登记（§5.1/§7.7/§9.2/§9.3/§9.4/D-MDL-6）；服从各原裁决结论 | 原登记裁决者 |

### 14.4 新增语义登记（全部位于 modeling 所有权内；无上游扩张）

1. `named-pose-set`/`tool-definition`/`scene-object`/`robot-drivetrain` 对象类型 token 与 schema（modeling 对象所有权，ARC-04/CON-01 框架内）。
2. `SceneObjectRole` 词表的建模侧正式登记（policy.md 已注明词表归建模语义）。
3. 六轴模板默认参数表、材料密度默认表（设计默认值，黄金数据集锁定，D-MDL-7）。
4. `MDL-*` 稳定码清单（§9.5，diagnostics 注册纪律内，随任务注册不预建）。
5. 物性估算唯一公式表 `mdl-property-formula/1`（MDL-05 授权范围内）。
6. 几何生成辅助两条（连杆占位圆柱生成、碰撞引用复制式辅助——§5.2，v0.2）：编辑辅助确定性纯函数，产物为普通 GeometryRef；**不引入网格重画/凸包简化算法**（上游未授权，如需走需求变更）。

### 14.5 交付前自审结论（2026-09-22，文档级自审——不等同实现测试或正式验收）

| 自审项 | 结论 |
| --- | --- |
| 是否重复 runtime/project/io/evidence/policy 职责 | 否——§2.3 不拥有表逐项核对；编译/事务/解析/评估/策略均只消费 |
| 是否存在第二套模型真值 | 否——canonical 字节单一存储；编辑态/派生字段不入身份（D-MDL-5/10） |
| 是否把 UI 状态当模型数据 | 否——会话姿态归 ui；位姿集为参考对象且不进依赖键（D-MDL-3） |
| 是否绕过 ProjectCommandService | 否——五命令全部经①端口；导入仅产草稿（§6.1/§9.3） |
| 是否重复实现基座—世界变换或名称解析 | 否——§7.7 隔离声明＋V-12 观测点；名称解析唯一在 runtime |
| 是否把模型未就绪误判为工程不可行 | 否——§8.2 状态正交；就绪不输出 Feasible/Infeasible |
| 是否让编辑态对象直接进入计算 | 否——§4.1/§9.1；草稿预览限于参数/诊断（D-MDL-10） |
| 是否忽略资源引用保护 | 否——I-MDL-9/10＋三段边界（§6.7）＋V-04/08 |
| 是否把导入文件路径当对象身份 | 否——§4.8；路径仅存于 ExternalResourceRecord/诊断文案 |
| 双权威参数冲突是否全部定义 | 是——§7.2/§7.3 来源表＋冲突矩阵 C-1～C-7＋非法组合 |
| 是否引入未经上游批准的新状态/阈值/语义 | 已审计——§14.4 全部登记于 modeling 所有权内；阈值（行程/条件数/容差）均引上游来源；模板默认值登记为设计默认＋黄金锁定 |
| 是否越权修改需求、架构或其他单元机制 | 否——本卡仅新建 units/modeling.md；全部上游张力进 §14.3 待裁决；未改任何其他文件 |
| 旧代码功能覆盖与 UI 设计完整性（v0.2 增审） | 已闭环——§2.5 对 old/src/rwslibs/robotmodelbuilder 23 项功能逐项对照（需求级全覆盖；4 项架构重定位、2 项有意不承接均注明理由与替代语义）；UI 界面设计与界面逻辑由 §9.7 承接（面板信息架构/十条数据流/域命令清单/线程约束），消除 v0.1 仅登记插件目标与任务行、未展开界面设计的缺口 |
| 遗留 | DETAILED-DESIGN.md 索引行（modeling 待产出→Draft）与 `traceability/unit-status.json`、`requirements-to-units.json` 的机器索引同步未由本卡代改——按"不修改其他文档"约束留给治理侧（建议随 DOC 系列任务消账） |

### 14.6 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-22 | 首版草案（WP-13-T01 承接）：14 章全量——上游基线登记（REQUIREMENTS v1.16／ARCHITECTURE v0.12／九单元卡 Draft 系）、拥有/消费/不拥有边界、五对象类型数据模型、模板/参数化/物性估算、URDF/Xacro/WorkCell 导入映射、双权威与 DH 五状态转换、传动耦合与分层就绪校验、CanonicalModel 交接（含 P-RT-5 reader 决议）与 8+9 公共接口契约、19 个稳定诊断码登记表、30 行故障注入矩阵、WP-13-T01～T19 任务排序、阶段 C/D 承接与双向交接清单、追踪矩阵、11 项设计决策/6 项风险/8 项待裁决＋6 项引用裁决、12 项交付前自审。状态 `Draft`，待评审 |
| v0.2 | 2026-09-22 | 响应评审两问：①新增 §2.5 旧代码（`old/src/rwslibs/robotmodelbuilder`，28 文件约 1.84 万行）功能范围对照表（23 行逐项映射，按附录 A 口径仅作范围对照不继承实现）——确认需求级功能全覆盖；4 项架构重定位（策略草稿输入、导出预览/发布、DHJoint 导出、指纹/侧车取代）与 2 项有意不承接（编辑态即时 XML 预览、场景层级挂载/DAF）注明理由与替代语义；②新增 §9.7 插件界面设计与界面逻辑（五区面板信息架构、十条控件↔计算库数据流、域命令登记清单、线程/刷新约束）——消除 v0.1 仅登记插件目标与任务行、未展开界面设计的缺口；③§5.2 增补几何生成辅助（占位圆柱/碰撞引用复制），§14.4 登记项 +1；同步更新 §3.1/§11/§13/V-30/§14.5；文档头版本升 v0.2 |
| v0.3 | 2026-09-22 | WP-13-T02 构建落位登记（文档同步）：①§1.2 构建落位行同步——`modeling/CMakeLists.txt` 新建（`sdurws_ird_modeling` INTERFACE 占位→STATIC、目标名/别名不变、C++17、零 Qt、配置期红线守卫随文件自持）＋`_test`/`_contract_test` 两测试目标（自持 ird_add_gtest 宏：add_test＋`<target>_report` XML、ctest LABELS "ird"——core/io 同款）；`_plugin` 不随本任务（本卡 §3.2 原文"随 WP-13-T15 落位"——DTB §2.14 T02 产出列含"_plugin 注册"与本卡的偏差按本卡执行，DTB §5.4 增量修订消账归治理侧）；②门禁白名单六边登记完成（`IRD_ALLOWED_UNIT_EDGES` 增 modeling→core/diagnostics/project/runtime/policy/io 六行＋`IRD_EXTRA_EDGE_REFS` 成对登记出处——dependency-graph.json 机器镜像刷新归 traceability 维护任务）；③公共头三件落位（§3.3 表 T02 行）：`ObjectTypes.hpp`（五对象 objectTypeToken——robot-design 以 using 引入 runtime::kRobotDesignObjectType 同一实体、不另设第二常量；SceneObjectRole 词表五值建模侧正式登记，token 串与 policy 消费侧镜像逐值同串）＋`Errors.hpp`（ModelingErrorCode 全表 12 值＝§9.4 已登记域错误名收编、ModelingError 值类型、域错误→稳定码映射数据——当前仅 SchemaVersionUnsupported→MDL-READINESS-SCHEMA-UNSUPPORTED 已登记行，其余随生产者任务登记不私定）＋`DiagCodes.hpp`（MDL-* 码描述符工厂——按 §9.5 分批纪律只登记 T02 行 MDL-READINESS-SCHEMA-UNSUPPORTED，其余 18 行随各自任务表尾追加不预建；真实 StableCodeRegistry 注册验证通过）；④对象 schema 版本常量不在本任务（§3.3 该行 T02/T03 双承载，消费者随 T03 codec 落位——无消费者不预建）；⑤测试目标门禁命中登记：`sdurws_ird_modeling_test`/`sdurws_ird_modeling_contract_test`→`sdurws_ird_testkit` 两处 IRD-GATE-SUB 与 F-051/F-072 同根因（引擎 SUB 形态一对测试目标→testkit 边未豁免），已按 EV-T11 先例自登记 findings；产品目标零 testkit 边、modeling 产品面零命中（GATE 命中集 base..branch diff 仅此 2 条新增，留痕 traceability/builds/wp13-t02/）。文档头版本升 v0.3 |
| v0.4 | 2026-09-22 | WP-13-T03 实现落位登记（文档同步）：①§3.3 表 T03 行公共头三件落位——`RobotDesign.hpp`（§4.3 根对象字段表全套值模型＋AuthorityMode/JointType/资源状态机/不变量层 I-MDL-1～12〔InvariantId/InvariantViolation/checkInvariants〕＋双权威编辑守卫 authorityEditGuard〔§7.3 C-1/C-2，返回 AuthorityViolation 值面〕）＋`Parts.hpp`（§4.4～§4.7 四部件值模型＋工具物性核查〔断言①②③同连杆〕＋传动核查 checkInvariants(DrivetrainDesign, CouplingStage)〔I-MDL-11/12〕）＋`Codec.hpp`（IRobotDesignCodec＋RobotDesignCodec 无状态实现：五对象 variant、FormatVersion{kCurrentFormatVersion=1.0}、查询轨复用 runtime::Expected 模板〔E=ModelingError〕）；单元私有头 `src/InertiaMath.hpp`（R-2：不入 include/——对称 3×3 解析式特征值＋SPD/三角不等式单一实现，连杆/工具共用）。②对象 schema 版本常量随本任务增补 `ObjectTypes.hpp`（§3.3 该行 T02/T03 双承载的 T03 半段——消费者 codec 主版本检查同批落地；五常量 kXxxSchemaVersion=1，单一权威禁写字面量）。③Errors.hpp 表尾追加 `MalformedPayload`（第 13 值——§9.4.9 codec 行"其余解码失败"承载：magic/截断/越界/非有限/UTF-8/集合未规范化/解码产物不变量违例；token 表同步；映射行暂缺随 §9.5 纪律）。④偏差与分工登记：a) `DhParameters` 定义于 RobotDesign.hpp（§3.3 DhConvert.hpp 行"DH 参数结构"的落位选择——该类型先被 §4.3-A JointEntry.dhDerived 需要，T09 直接复用不另立第二 DH 结构）；b) `JointEntry.origin` 的 SourcedValue 载荷类型为 modeling::JointPose（与 rw::math::Transform3D\<double\> 同构可互转、逐元素恒等默认构造）——rw Transform3D 默认构造引用框架库外联符号 Rotation3D::identity()，SourcedValue 四态工厂必然默认构造载荷，两者在冒烟 header-only 纪律（runtime RT-T03 同款）下不可共存；语义/编码字节不变，T12 reader 经隐式转换零感知；c) §7.1/§7.2 提及的 maxVel/maxAcc 未入 §4.3-A 关节字段表——本任务不建模表未登记字段（不发明 schema），其归属随消费方任务按卡面修订澄清；d) §4.4 payloadAttributes 仅承载卡面具名 ratedLoad（"..."其余字段待 requirements 语义冻结后表尾追加）；e) I-MDL-9"指向对象存在于闭包"半段需要修订闭包视图（②查询端口），归 T08 就绪校验 L1 层（§8.2），值模型只核查根对象内可判面；f) I-MDL-7"preset≠ground 而 R=I 在映射层拒绝"归 T05/T11 映射层，不属于值模型校验；g) I-MDL-11 的 C 可逆性/条件数**重算复核**随 T18/R2 经 runtime 单源（单侧 Jacobi SVD），值模型只核查已登记字段自洽（方阵/条件数≤1×10⁸/窗口有序）；1×10⁸ 暂以同值字面承载，P-RT-7 冻结后单源常量替换（P-MDL-7）。⑤D-MDL-5 编码面落地：canonical 编码（"IRDMDLO"家族、小端、UTF-8 无 NUL、无填充、集合按 ObjectId 规范文本/resourceId/tcpKey/位姿键字典序规范化）只含权威字段——Explicit 态不编码 dhDerived、StandardDH 态不编码 axis/origin（解码后为 NotProvided 待重算，重算入口随 T09）、selfCollisionHints 不编码（§4.3-B）；往返相等＝规范化形态相等。⑥验证留痕：双模式构建零错误（集成 Release＋独立冒烟——冒烟配置需 Qt 前缀：testkit_qt/ui 单元此后置任务引入无条件 find_package(Qt6)，非 modeling 单元行为；modeling 产品面零 Qt 由 BuildRedLineTest 复核）；`sdurws_ird_modeling_test` 51/51（不变量 I-MDL-1～12 逐条用例名绑定＋V-14 权威守卫＋MDL-09 权限矩阵＋Codec 确定性/往返/版本闸/破损面）＋契约测试通过，gtest XML＋ird-test-report.json 双模式留痕 traceability/builds/wp13-t03/；ird_gates base..branch 命中集 diff 为空（本任务零新增命中；存量 58 条含 §v0.3 ⑤已登记两项）。文档头版本升 v0.4 |
| v0.5 | 2026-09-22 | WP-13-T04 实现落位登记（文档同步）：①§3.3 表 T04 行公共头落位——`PropertyEstimation.hpp`＋`src/PropertyEstimation.cpp`（物性估算唯一公式表 `mdl-property-formula/1`，§5.3/§9.4.6 全量落地）：IPropertyEstimator 三方法（estimateLink 四步算法〔段元尺寸/密度解析→段级闭式公式→平行轴合成 I_C=Σ[Rᵢ·Iᵢ·Rᵢᵀ+mᵢ((dᵢ·dᵢ)E−dᵢdᵢᵀ)]→输出自检〕＋diags 输出参数按卡面签名保留——§9.5 无 T04 行码（分批注册、不预建），估算失败经 EstimateOutcome 值面定位，物性缺失 Warning 登记面归 T08 就绪校验；migrateByParallelAxis 平行轴迁移（确认流选项 a 的实现）；formulaVersion()）＋EstimateOutcome＝runtime::Expected\<EstimatedLinkProperties, EstimateError\>（E=EstimateErrorCode{IllegalDimension|MaterialDensityMissing|SynthesisFailed}——§9.4.6 @错误 行的局部错误枚举，Errors.hpp 头注"两族专用错误枚举"承载，不并入域级 ModelingErrorCode 表）＋材料密度默认表查询 defaultMaterialDensity（键词表稳定 token："steel"7850/"aluminum"2700/"cast-iron"7200/"titanium-alloy"4430/"engineering-plastic"1200 kg/m³——§14.4 第 3 项设计默认值的键值落位，黄金数据集锁定随 WP-13-T16）＋质心修改确认流域级判定 applyCentroidEdit（§5.3 规则 2 三分支：MigrateInertia 接受〔惯量平行轴迁移且保留原 provenance〕/OverwriteInertia 接受〔com/inertia 均 UserProvided〕/nullopt→ModelingError{CentroidEditUnresolved} 拒绝且 body 零副作用——MDL-05/M-2 不静默脱钩；前置 mass/com/inertia 均 Provided＝V-17"已估算连杆"，违约 fail-fast 走异常轨）。②私有头 `src/InertiaMath.hpp` 增补 synthesisSelfCheck（§9.4.6 @post 输出自检单一实现：有限性→对称相对 1×10⁻¹²→SPD 严格>0→惯性椭球三角不等式；失败返回稳定短语进 SynthesisFailed detail——不输出非法张量；物理合法输入合成恒 SPD，触发即内部错误）。③偏差与澄清登记（DTB §5.4 口径）：§5.3 公式表第 3 行（长方体 a,b,L；z 沿 L）列头"Ixx=Iyy"为轴对称两行（圆柱）的合并列——长方体 a≠b 时 Ixx≠Iyy 是解析事实，实现按同式取 Iyy=m(a²+L²)/12（Ixx=m(b²+L²)/12 与 Izz=m(a²+b²)/12 为表值原样）；表行数值本身不变，UT 以 a≠b 用例钉住两轴差异。④空心圆柱尺寸约束 rOut>rIn>0（壁厚为正是质量公式的数学前提）随 @pre 显式化；空段元列表走值面 IllegalDimension（§9.4.6 @错误 行把 @pre 尺寸违约归同码）。⑤验证留痕：双模式构建零错误（集成 Release＋独立冒烟——Qt 前缀同 v0.4 ⑥口径）；`sdurws_ird_modeling_test` 68/68（新增 17 条：ACC1 三段元闭式公式手算对照〔公式表三行逐项双精度＋密度解析序＋空表拒绝〕/ACC2 两段平行轴合成＋M-2 参考姿态旋转钉子＋自检分量级反例拒收〔对称超差/非正定/三角不等式＋容差闭边界〕＋公共面溢出 SynthesisFailed 反例〔非法张量不可观测〕＋确定性同输入逐位相等/ACC3 确认流三分支＋前置 fail-fast/ACC4 密度表五键＋NotProvided 不断言〔V-15 第④例建模面〕/ACC5 传动 schema 字段约束与来源标记＋摩擦力矩缺失接受）＋契约测试 5/5 通过；gtest XML＋ird-test-report.json 双模式留痕 traceability/builds/wp13-t04/；validate-task/validate-docs 双 PASS；ird_gates 命中 51 项＝base 存量（门禁码计数多重集与 wp13-t03 base/branch 留痕一致，本任务零新增命中；modeling 面仅存量两条测试目标→testkit SUB＝F-292，无法由本任务消账）。文档头版本升 v0.5 |
| v0.6 | 2026-09-22 | WP-13-T05 实现落位登记（文档同步）：①§3.3 表 T05 行公共头落位——`Import.hpp`＋`src/Import.cpp`（URDF 导入字段映射与链型判定，按 DTB §2.14 T05 规模列拆「解析映射/链型判定」两提交）：IModelImportMapper 三方法（mapUrdf/mapXacroExpanded/mapWorkCellXml——§9.4.3 签名；mapWorkCellXml R1 返回 NotImplemented 稳定错误＋通道引导条目，不留桩实现〔§6.6/MDL-18 R2〕）＋ModelImportMapper 无状态实现＋ValidatedSource（§9.4.3"字节＋快照＋依赖树"复合输入——io 未产出该复合类型〔P-MDL-8/R-MDL-1 处置〕，以 modeling 自有值类型组合 io 公共值类型 ResourceSnapshot/ResourceDependencyTree，装配面归调用方；依赖树允许缺失叶——io §6.5 缺失清单载体）＋ImportOutcome{draft,report,error}（三元关系：报告恒产出——"识别＋报告"语义；error＝§9.4.3 @错误 行具名阻断条件中首个命中者，不等于"无草稿"）＋ImportErrorCode 八值（@错误 行六值收编＋SourceInconsistent〔字节/树不匹配与 URDF 结构违例——io 只保 XML 良构〕＋NotImplemented〔R1 边界〕）＋ImportReport 全清单（映射/默认补全/忽略/不支持四清单＋待确认＋关节状态＋资源状态表＋自碰撞候选〔P-MDL-3：仅报告候选不写策略对象、LinkEntry.selfCollisionHints 不填〕＋传动回填候选〔effort→torqueLimit Peak〕＋错误项＋分支报告＋主链能力结论）。②§6.3 映射表逐行落位与偏差澄清（DTB §5.4 口径）：a) `<limit velocity>` 无 schema 落点——§4.3-A 未登记 maxVelocity（v0.4 ③c 同口径不发明字段），以不支持项报告承载不静默丢弃，归属随消费方任务按卡面修订澄清；b) 图元几何（box/cylinder/sphere）§6.3 未登记映射语义——不支持项报告不映射（不静默降级）；mesh 非单位 scale 无 GeometryRef 落点（刚体变换之外）——同归不支持项报告；c) 多 visual/collision——schema 单引用，仅首个映射其余逐条报告；d) robot 级 `<material>` 定义无落点无密度语义——忽略清单（visual 内联材质名为 body.material 映射源，密度 NotProvided 估算不可用报告登记）；e) URDF 惯量在 inertial origin rpy 姿态系给定而 BodyData.inertia 参考姿态是连杆系（M-2）——rpy≠0 时实现 R·I·Rᵀ 旋系（复用 InertiaMath 单一实现的断言面）；f) prismatic 缺 lower/upper 同 revolute 入待确认项（§6.3 行只列 revolute——MDL-06④ 同管辖 prismatic，同语义扩展）；g) 名称净化＝[A-Za-z0-9_.-] 之外逐字节替换 '_'（§4.3 字符集预处理；冲突判据＝净化后 localName，NameConflict 不静默二次改名——I-MDL-2）；h) 1～3 轴链 §6.4 能力矩阵未列——无模板语义同归"超出首版产品模板范围"（类型保留、草稿可编辑），§6.4 行语义澄清不扩需求；i) 确定性临时句柄（§6.7"tmp-\<序号\> 不入库"）以（类别,序号,localName）的确定性散列派生 128 位 ObjectId 字节承载（ObjectId 为二进制强类型无文本位；同输入同句柄、不经随机源，真实 ObjectId 仍由 prepare 分配——PA-1）；j) 缺失网格（§6.7/V-08"应用可过（Warning）"）以 Recorded 记录草稿可携带：digest 空保留值＋相对键入 ExternalResourceRecord.absPath（无绝对路径事实——io probe 届时报 Missing；io dependencyTree 缺失叶时整体失败不回树的等价调整需求登记于实施提交，io 侧按 R-MDL-1 冻结后增量同步）。③§9.5 增登 `MDL-IMPORT-TEMPLATE-RANGE` 行（导入/info，confirmable=false，任务列 T05）：卡 §6.4 能力矩阵"诊断'超出首版产品模板范围'"语义的原表缺行——实现期增登（表行序插于 PENDING-CONFIRM 与 PACKAGE-UNKNOWN 之间；diagnostics §4.3 分类 InfeasibilityProof＝有效工程结论族）；T05 行五码（UNSUPPORTED-JOINT/BRANCH-SELECTION/ZERO-AXIS/PENDING-CONFIRM/TEMPLATE-RANGE）全量以真实 StableCodeRegistry 注册验证通过（DiagCodesTest 工厂清单封闭性同步 T02+T05 六码）。④pugixml 依赖（O-40，DTB §4 2026-09-22 所有者批次授权）：经 vcpkg 经典模式安装 **pugixml 1.16 x64-windows**（2026-09-22），modeling PRIVATE（`find_package(pugixml CONFIG REQUIRED)`＋独立 PRIVATE 链接语句，六边 PUBLIC 登记形态不变）；公共头零 DOM 类型暴露（消费者零感知）；DOM 有界性由 io BudgetGuard 前置保证（输入字节已经 io 预算入账）；数值解析 locale 无关（std::from_chars/to_chars——NFR-COR-01/02）。⑤验证留痕：双模式构建零错误（集成 Release 三目标＋独立冒烟——冒烟配置带 vcpkg toolchain＋Qt CMAKE_PREFIX_PATH 前缀，v0.4 ⑥同口径）；`sdurws_ird_modeling_test` 91/91（新增 23 条 MdlImport：ACC1 接口落位/R1 边界/可重入/Xacro 留痕；ACC2 §6.3 逐行四清单＋包 URI 定位引导＋缺失资源 IO-RES-MISSING/Recorded 草稿可携带＋物理错误项 m≤0/三角不等式＋名称冲突与非法值原串保留＋惯量 rpy 旋系；ACC3 轴语义三分支〔非零非 Z/缺 axis +X 待确认/零与 NaN 仅报告不提交〕；ACC4 链型两维度〔多分支未选链拒绝＋分支报告可观察/显式选链放行＋辅助分支候选不构成拒绝/残留分裂再选链/mimic 类型保留阻断/planar 无草稿/六·七轴全能力/4·5 轴与含 prismatic 范围外＋TEMPLATE-RANGE 诊断＋类型保留 V12-01/continuous 工作范围待确认〕；ACC5 确定性同字节同输出＋诊断按源文件行序稳定排序）＋契约测试 5/5 通过（六边封闭形态不受 pugixml PRIVATE 边影响）；gtest XML＋ird-test-report.json 双模式留痕 traceability/builds/wp13-t05/；validate-task/validate-docs 双 PASS；ird_gates base..branch 命中集多重集比对（base 侧经临时 worktree @ dd97c920 直跑同脚本）＝恰 1 条新增 IRD-GATE-LIB「pugixml::pugixml 词汇表外」（O-40 已登记依赖的门禁词表暴露——治理侧扩词表，自登记 findings F-302；modeling 面其余零新增）。文档头版本升 v0.6 |
| v0.7 | 2026-09-22 | WP-13-T06 实现落位登记（文档同步）：①§3.3 表 T06 行公共头落位——`XacroExpand.hpp`＋`src/XacroExpand.cpp`（Xacro 受控展开语义引擎，§6.5/§9.4.9）：IXacroExpandService 单方法 expand（§9.4.9 概要签名落位——`ExpandOutcome expand(const ValidatedXacroSource&, const XacroSubstitutionMap&, Diags&) const`）＋XacroExpandService 无状态实现＋ValidatedXacroSource（io 已验证字节＋入口快照＋依赖树＋includeBytes 字节表——io 产物装配面归调用方，本单元零文件访问〔io.md §10.5/SA-14〕；入口快照 digest/路径为来源记录消费契约）＋XacroSubstitutionMap（构造序保序 vector-of-pair，与 XacroProvenance.substitutions 同形）＋ExpandOutcome{expandedBytes, parameters, provenance, error}（四元关系：失败无产物——"展开失败不产生草稿"〔§6.5〕；parameters＝参数列表展示数据〔MDL-19 向导页〕；provenance 恒产出＝mapXacroExpanded 第二输入）＋XacroExpandErrorCode 五值＋xacroExpandErrorCodeToken。②受控子集与护栏口径（§6.5/io.md §6.2 落位）：支持 include（filename｜file｜href，与 io dependencyTree include 边同词表）/property/macro（params 空白分隔、name:=default 字面默认值）/宏调用（签名序绑定、缺参与签名外属性失败）/普通元素透传＋${标识符} 替换（单遍扫描替换文本不二次展开——防注入递归；substitutions→文档属性→宏绑定的作用域链）；${} 内非标识符（表达式）与 $(...)（ROS 替换参数）显式拒绝——绝不执行任意代码（§9.4.9/NFR-COR-03）；注释/PI 不进产物；展开产物为 URDF 形态流式序列化（XML 转义自持，mapUrdf 直接可解析）。护栏执行面（P-MDL-4"护栏 io／语义 modeling"）：io 公共面零改动（git diff base..branch -- …/io/ 为空）；递归深度 ≤ IncludeDepth(16)（kXacroMaxExpansionDepth——io.md §6.2 边界值，include 拼接与宏调用共用深度计）、产物总字节 ≤ TotalBytes 防御值（kXacroExpandedTotalBytesGuard＝2 GiB——io Budget.hpp TotalBytes 产品默认；单元测试不触发该值，如实声明于头注）——超限分别透传 IO-SEC-BUDGET-INCLUDE/IO-SEC-BUDGET-TOTAL；输入带环树防御性 DFS 复核→IO-FORMAT-XML-CYCLE＋环路径（V-09 透传）；include 边缺失预扫描（缺失叶/字节缺项，回边命中入口文档自身以 entryBytes 豁免）→IO-RES-MISSING＋缺失清单；已验证字节解析失败（调用方装配违约）→IO-FORMAT-XML-SYNTAX 透传＋SourceInconsistent。③§9.5 增登 `MDL-IMPORT-XACRO-UNRESOLVED` 行（导入/error，confirmable=false，任务列 T06；表行序插于 TEMPLATE-RANGE 与 PACKAGE-UNKNOWN 之间）：未定义宏/未定义参数/缺参/签名外属性/不支持构造/宏重定义的语义定位面（item-kind/symbol 两键 paramSchema，行列由诊断 context 定位段承载）——io 护栏码透传面之外的原表缺行（T05 增登行同款实现期登记，§14.6 v0.6 先例）；DiagCodes.hpp 常量＋DiagCodes.cpp 描述符表尾追加＋真实 StableCodeRegistry 注册验证通过（DiagCodesTest 工厂封闭性同步 T02+T05+T06 七码）。④pugixml 复用（O-40）：零新依赖渠道——`XacroExpand.cpp` 与 `Import.cpp` 同批消费 WP-13-T05 已登记的 pugixml 1.16/x64-windows（modeling PRIVATE），CMakeLists 登记语句零改动。⑤偏差与澄清登记（DTB §5.4 口径）：a) 展开产物快照由调用方装配（mapXacroExpanded 输入的 ValidatedSource.entrySnapshot——对展开字节以 core ContentDigester 计算真实摘要；原始 .xacro 的 digest 经 provenance 进草稿 externalRefs〔Recorded〕，T05 已落位消费面）；b) 深度护栏的码面取 IO-SEC-BUDGET-INCLUDE（io 已注册族中最贴近"展开递归深度"的码——io.md §6.2 把展开深度绑定 IncludeDepth 维；actual/limit 比较三要素随诊断）；c) 行列按 pugixml offset_debug＝元素名起点定位（列＝名字节列，稳定性优先——Import.cpp LineIndex 同款口径）；d) 宏默认值与 property name 为字面文本不做 ${} 解析（受控子集边界，头注显式声明）。⑥验证留痕：双模式构建零错误（集成 Release sdurws_ird_modeling/_test/_contract_test＋独立冒烟——冒烟配置带 vcpkg toolchain＋Qt CMAKE_PREFIX_PATH 前缀，v0.6 ⑤同口径）；`sdurws_ird_modeling_test` 104/104（新增 13 条 MdlXacroExpand：ACC1 接口落位（接口多态调用）＋include 拼接宏登记＋绝不执行（表达式/$(...) 拒绝）＋输入契约违约值面；ACC2 未定义宏/未定义参数行列定位（宏名/参数名＋源行列断言）＋substitutions 可解同一引用对照面＋IO-FORMAT-XML-CYCLE 环透传（V-09，环路径清单）＋IO-RES-MISSING 缺失清单透传＋深度护栏 IO-SEC-BUDGET-INCLUDE（actual/limit）＋参数列表展示数据（substitution 前置＋property 行列）＋substitutions 优先级面；ACC3 展开产物 mapXacroExpanded/mapUrdf 与等价手写 URDF 草稿逐字段相等（剥除 xacro-source 来源条目后恒等——同一映射最直接证据）＋原始 .xacro digest 进草稿 externalRefs＋失败无产物；ACC5 确定性同输入同字节同诊断序＋交错调用可重入）＋契约测试 5/5 通过；gtest XML＋ird-test-report.json 双模式四件套留痕 traceability/builds/wp13-t06/；validate-task/validate-docs 双 PASS；ird_gates base..branch 命中集多重集比对（base 侧经临时 worktree @ 6707f447 直跑同脚本，111=111 行归一后逐条一致）＝零新增命中（modeling 面仅存量 pugixml LIB＝F-302 与两测试目标 testkit SUB 存量，无本任务因果）。文档头版本升 v0.7 |
| v0.8 | 2026-09-22 | WP-13-T07 实现落位登记（文档同步）：①§3.3 表 T07 行公共头落位——`Template.hpp`＋`src/Template.cpp`（模板创建与参数化编辑，§5.1/§5.2/§9.4.2）：IRobotDesignTemplateFactory 两方法（listTemplates＝§5.1 清单三行〔generic-6r 6×Revolute R1 可用／generic-7r enabled=false P-03 冻结前仅登记不启用〔O-27 处置〕／custom-chain 逐轴定义〕＋createDraft 建链〔generic-6r 按表 T-MDL-1 六行建 6×Revolute＋7 连杆；BasePlacement 预设＋零位地面默认值带模板来源标记〔MDL-22 V15-04〕；连杆材料种子钢取 §5.3 默认表单源；物性数值 NotProvided 不猜测〕；拒绝面 TemplateDisabled〔附 MDL-TEMPLATE-DISABLED 提示诊断、不静默替换六轴、无半成品——V-02〕｜IllegalName 值面；清单外 id/Custom 预设＝@pre 违约 fail-fast〕）＋RobotDesignTemplateFactory 无状态实现＋TemplateId（静态目录 token——std::string 别名，不用 Id128 强类型：非持久身份）＋TemplateDescriptor＋sixAxisTemplateDefaults（表 T-MDL-1 载体：type/axis/zeroOffset/bounds/maxVel/maxAcc 六列；J6 行程恰 4π 阈值边界兼作边界用例——阈值常量归 policy 不私设〔ARC-05〕）＋ModelingWorkingSet v1＋ModelingChangeRecord＋buildChangeSummary＋creationEntryGuard（创建入口维度二守卫：FullTemplateRange 放行／Beyond 阻止＋TEMPLATE-RANGE 提示——T05 行码复用同语义）＋JointEditField/JointEditErrorCode 局部枚举＋applyJointFieldEdit/applyJointFieldEditBatch（§5.2 字段级/批量变体：拒绝工作集字节不变、批量产出单条变更摘要、C-1 权威守卫复用 authorityEditGuard）＋GeneratedGeometry＋makeLinkPlaceholderCylinder/copyVisualToCollision（§5.2 v0.2 两条几何辅助：普通 GeometryRef＋GeometricEstimate 来源 methodTag 区分；不重画/不凸包简化）。②Import.hpp 增补 judgeChainCapability 纯函数（§6.4 尾段"同一判定被模板创建入口复用"的单一实现——自 mapUrdf 第九步内联逻辑行为保持提取，reason 串逐字不变、ImportTest 既有断言钉住；导入报告与创建入口共用）。③§9.5 增登 `MDL-TEMPLATE-DISABLED` 行（模板/info，confirmable=false，任务列 T07；表行序追加于表尾——登记簿纪律）：TemplateDisabled"附定位诊断"的码面落位（paramSchema [template-id, freeze-gate]，分类 InfeasibilityProof＝冻结门未过的有效工程结论族）；DiagCodes.hpp 常量＋DiagCodes.cpp 描述符表尾追加＋Errors.cpp modelingDiagCode 映射行同步（TemplateDisabled→MDL-TEMPLATE-DISABLED，映射数据与工厂同源对账）；真实 StableCodeRegistry 注册验证通过（DiagCodesTest 工厂封闭性同步 T02+T05+T06+T07 八码）。④偏差与澄清登记（DTB §5.4 口径）：a) §9.4.2 createDraft 卡面签名直返 ModelingWorkingSet，落位为 TemplateOutcome＝runtime::Expected\<ModelingWorkingSet, ModelingError\>（§9.4 前言"一切接口方法非异常出口"——错误轨值面返回；Codec/PropertyEstimation 两态先例；卡 §9.4.2 代码块以 v0.8 修订为准）；b) ModelingWorkingSet 定义于 Template.hpp（§3.3 头表未指派——T07 为首个消费者；v1 仅承载 design＋changes，T08 编辑器扩展基线/差值/undo 内部态）；c) 草稿关节/连杆 ObjectId 为确定性派生临时句柄（SHA-256 前十六字节，v0.6 ②i 同款纪律；真实 ObjectId 由命令 prepare 回填——PA-1）；d) custom-chain createDraft 产 1 轴种子（T-MDL-1 J1 行为种子默认，逐轴编辑入口；增轴经 T08 AddJoint——I-MDL-1 joints≥1 排除零轴种子）；e) 编辑面（类型/轴线/零位/限位）仅覆盖 §4.3-A 已登记字段，速度/加速度无 schema 落点（v0.4 ③c 不发明字段纪律）——数值由 sixAxisTemplateDefaults 承载供向导预填/黄金数据集核对，字段级编辑随 schema 澄清表尾追加；f) 连杆几何种子不预填：§5.1"连杆几何默认"句的圆柱半句受 R1 schema 图元承载边界约束（v0.6 ②b 同源——GeometryRef 经 resourceManifest 而 I-MDL-10 状态机无图元合法态），预填即违 I-MDL（与 acceptance 1"产出满足 I-MDL-1~12"冲突）；几何辅助作为纯函数交付、其持久化承载随 schema 澄清落位（§12 承接），材料半句（钢 7850）已落；g) 关节/连杆名种子 j\<序\>/base/l\<序\>、origin 种子恒位姿（表 T-MDL-1 未登记 origin 列）——均为 D-MDL-7 设计默认值增量登记。⑤验证留痕：双模式构建零错误（集成 Release 三目标＋独立冒烟——冒烟配置带 vcpkg toolchain＋Qt CMAKE_PREFIX_PATH 前缀，v0.4 ⑥同口径）；`sdurws_ird_modeling_test` 119/119（新增 13 条 MdlTemplate：ACC1 三模板清单逐行＋generic-6r 草稿 I-MDL-1~12 全量核查＋非法输入面〔清单外 id/Custom 预设 fail-fast＋IllegalName 值面＋错误态无草稿〕＋确定性临时句柄；ACC2 七轴 TemplateDisabled＋提示诊断恰一条＋无半成品＋不替换六轴；ACC3 T-MDL-1 表值独立抄写逐格位级比对＋J6 行程 4π 边界＋草稿关节字段来自表值＋材料单源对账；ACC4 字段级编辑接受/拒绝边界〔非有限/零轴/坏区间/Continuous 冲突/C-1 权威锁——拒绝字节不变〕＋批量变体单条摘要＋拒绝行保留原值＋buildChangeSummary 确定性＋创建入口守卫 5 轴/含 prismatic 阻止＋TEMPLATE-RANGE 提示＋6/7 轴放行＋与 judgeChainCapability 同源对照；ACC5 占位圆柱轴向/中点/±z 特例/正交 1×10⁻¹²/确定性/输入面 fail-fast＋碰撞引用复制同资源同位姿同类别＋methodTag 区分）＋契约测试 5/5 通过；gtest XML＋ird-test-report.json 双模式留痕 traceability/builds/wp13-t07/；validate-task/validate-docs 双 PASS；ird_gates base..branch 命中集多重集比对（base 侧经临时 worktree @ 5e9719aa 直跑同脚本，52＝52 条归一后逐条一致）＝零新增命中（modeling 面仅存量 pugixml LIB＝F-302 与两测试目标 testkit SUB 存量，无本任务因果）。文档头版本升 v0.8 |
| v0.9 | 2026-09-22 | WP-13-T08 实现落位登记（文档同步）：①§3.3 表 T08 行公共头落位——`Readiness.hpp`＋`src/Readiness.cpp`（IModelReadinessChecker：§8.2 分层检查表 L0→L11 编排——check 纯函数（同输入逐字段相等）、严格顺序执行＋短路优先级（层阻断后继层"未执行"明细）、结果 blockers/warnings/confirmables/notes 四组（前三组＝§9.4.4 冻结三组；notes＝呈现级层结论承载：无登记码的构造层保证域〔L2/L3 等〕与缺项预告面〔L5 DataInsufficient→走 UI missingItemKeys 通道，不入诊断目录〕与 L11 呈现级子项〔writable——§9.4.4 字段注释原文"只读模式提示（L11 呈现级）"；baseRevision==tip 强制点归 project S2/prepare 复核〕）＋稳定排序（层号→对象 id 字典序→码字典序）＋ModelReadinessReport/ReadinessLayer/ReadinessStatus/ReadinessNote/LayerDetail 值模型＋ModelReadinessChecker/CheckerFactory）＋`CommandHandlers.hpp`＋`src/CommandHandlers.cpp`（AssertionSuite——§9.4.4 指定的就绪校验/prepare 共用唯一断言实现〔D-MDL-11/NFR-MNT-04：物性①②③经 src/InertiaMath.hpp 同源特征值、限位④、行程上限经 policy IJointLimitEvaluator 唯一实现、闭包引用、资源状态面、schema 版本〕；命令载荷模型 encodeCommandPayload/tryDecodeCommandPayload（magic IRDMCP1＋小端长度前缀＋版本受理＝{1}；槽语义 allocateNew〔全零身份→ctx.objectId() 取号〕/显式身份替换）；IModelingCommandHandler 基类 prepare 公共管线（decode→基线重建＋防御性复核〔expectedRevision≠baseSnapshot.id＝契约违约 fail-fast；基线字节不可解码＝数据损坏 fail-fast〕→子类 decodeAndPlan 钩子→断言分域→计划最终化〔requiresDualCompile/confirmableFindings 稳定排序/inverse 快照逆载荷/中文摘要〕）＋五命令处理器（ApplyRobotDesign/ApplyToolDefinition/ApplySceneObjects/ApplyNamedPoses/ApplyDrivetrainDesign——§9.3 表逐行；requiresDualCompile 仅 apply-named-poses=false；行程校验域门控＝dec.travelRelevant（根命令行断言列含"行程上限"，其余命令不改关节行程事实不消费④端口））＋registerModelingCommandHandlers L5 装配注册）。②§9.5 增登 `MDL-READINESS-PHYSICS-MISSING` 行（校验/Warning→DataInsufficient/Warning，任务列 T08；表尾追加）：§9.3"物性缺失→不阻断，转 Warning 诊断随计划留痕"与 §8.2 L5 Warning 行的码面落位（原表缺行——沿 TEMPLATE-RANGE/XACRO-UNRESOLVED/TEMPLATE-DISABLED 实现期增登先例）；DiagCodes.hpp 常量＋DiagCodes.cpp 描述符表尾追加（T08 行九码＝八卡面行＋本增登行；paramSchema 统一 "[]"——定位走 subject＋localName、三要素走 comparison、文案走 context/cause/action，不经 DiagContext params 通道）；DiagCodesTest 分期封闭性同步 T02+T05+T06+T07+T08 十七码（requiresComparison/confirmable 改逐码断言——TRAVEL-LIMIT confirmable＋比较型强制例外面）。③ModelingWorkingSet v0.9 增量（Template.hpp——表尾追加向后兼容）：rootObjectId＋toolObjects/sceneObjects/poseSetObject/drivetrainObject 闭包部件对象值视图（§8.2 L1/L5/L7/L9 与 prepare 断言的部件事实承载；模板路径缺省值不变、T07 既有用例不受影响）；编辑器内部态（undo 栈/差值）仍不入本值。④MDL-ASSERT-* 诊断记录形态落位：subject=对象 ObjectId＋localName＋比较型三要素（①actual=m/expected=0/kg；②actual=λmin/expected=0/kg·m²；③actual=λmax/expected=λmid＋λmin/kg·m²；④actual=qmin、range.first/expected=qmax、range.second/rad|m）——就地阻止＋精确定位（DTB 禁止项对位）；行程 ConfirmableFinding 比较型三要素 actual=T=|qmax−qmin|/expected=阈值/单位 rad，cause 内嵌 policyContentId 规范文本（§6.7 四元组的定位素材——四元组组装归 project S6）。⑤偏差与范围登记（DTB §5.4 口径）：a) 契约 acceptance 5"V-20……HEAD 不变、修订计数不变、RT-*＋MDL-* 诊断一并入目录"的全管线面归 project S5 已验契约（PRJ-TX-3 双编译桩三路）——project HandlerRegistry 无公共注入面（R-2 禁跨单元私有头），modeling 契约测试以 HandlerContext 接缝验证：V-19 prepare RejectedHardAssert＋编译端口零调用（mock 计数）＋计划清空；V-20 计划面 requiresDualCompile=true＋§6.6 CompileRequest 装配数据流＋故障注入 ok=false＋RT-*/MDL-* 诊断共存于收集面；V-22 stale 面＝处理器防御性复核 fail-fast（std::invalid_argument）＋拒绝后可复用（S2 拦截与草稿 apply-retained 归 project 已验契约）；b) L10"字段集完整"半段（Explicit 权威 axis/origin 缺失）＝呈现级阻断（无登记码面——不私定码值），prepare 层由 runtime S3 编译兜底（§9.1 三层防线口径）；c) L6"缺失/变化探测"为 I/O——就绪层只承载状态机事实（Recorded 未固化→RESOURCE-STATE Warning），探测归 io 护栏与 runtime 编译复核（ResourceChanged）；d) MDL-ASSERT 硬断言拦截的输入面＝解码门可过、跨对象/策略域事实（如 continuous 范围未确认）——物性①②③/区间错在解码门（I-MDL 全量）先行拦截，V-15 反例经 AssertionSuite/就绪检查器直接验证（V-19 契约路径用解码门可过的 Range-Not-Finite 反例）；e) 行程校验命令域门控（dec.travelRelevant）＝§9.3 断言列的域归属细化（其余四命令不改关节行程事实）——登记为 §9.3 的实现细化而非语义变更；f) HandlerServices（policyProvider/policyObject/policyVersion 断言端口集）＝§5.3.2"处理器自持④端口句柄"的落位形态（HandlerContext PRJ-T10 冻结面无④端口访问器）。⑥验证留痕：双模式构建零错误（集成 Release 全量 0 错误＋独立冒烟 Release——冒烟配置带 vcpkg toolchain＋Qt6_DIR/CMAKE_PREFIX_PATH 前缀〔testkit_qt 无条件 find_package(Qt6)，v0.4 ⑤同口径〕）；T08 测试 TU（ReadinessTest/CommandHandlersTest/CommandPipelineContractTest）消费 makeJointLimitEvaluator——集成模式专属 gating（POL-T06/T07/T08 同款 TARGET sdurw_kinematics 判定，冒烟不编入）；`sdurws_ird_modeling_test` 139/139（新增 20 条：MdlReadiness 5＋MdlReadinessAssertionDomains 4＋MdlReadinessTravelLimit 2＋MdlCommandHandlers 4＋MdlCommandPrepare 5）＋`sdurws_ird_modeling_contract_test` 9/9（新增 4 条：V-19/V-20/V-22/T08 行九码注册）双模式通过（集成 139＋9、冒烟 119＋5〔gated 子集〕）；gtest XML＋ird-test-report.json 双模式留痕 traceability/builds/wp13-t08/；validate-task/validate-docs 双 PASS；ird_gates base（fddf851b）..branch 命中集归一化多重集比对（base 侧经临时 worktree 直跑同脚本）＝EMPTY——39 条唯一存量命中零新增。文档头版本升 v0.9 |
| v0.10 | 2026-09-25 | WP-13-T09 实现落位登记（文档同步）：①§3.3 表 T09 行公共头落位——`DhConvert.hpp`＋`src/DhConvert.cpp`：DhErrorCode（ChainEmpty|DegenerateBase——基座变换混入拒绝面＝DhChain.mixedInBaseTransform 检测位，M-11 隔离声明的可执行形态）＋DhChain/DhChainJoint（θ_offset 与 zeroOffset 显式分离＋两态均权威字段〔type/bounds/workingRange〕透传）＋ExpandOutcome＋五状态 DhDetermination/DhConvergenceState/DhJointDeviation/DhConversionResult＋CompileProbe（§9.4.7 原文"编译分段入口由调用方注入"的抽象——只读、不发布快照、确定性三契约）＋EquivalenceReport＋IDhExplicitConverter/DhExplicitConverter（dhToExplicit/explicitToDh/verifyEquivalent）＋prepareAuthoritySwitch 权威切换域门（四道门：①前置 Explicit 态②C-6 先决断既有编辑〔draft.changes 非空即拒〕③转换判定 C-3/C-4④等价验证——验证失败不产生修订）。②explicitToDh 两阶段确定性求解（§7.5 实现细化登记）：相位 1＝单位参数固定初值 LM（§7.5 字面）；相位 2＝解析种子＋LM 精化——仅相位 1 未达第 5 项上界时启用，种子自目标**全帧**闭式读出 θ/d/a/α（θ'＝atan2(L10,L00)、α＝atan2(−L12,L22)、a/d 自局部平移），对 DH 一致链逐位精确，满足 V-11 roundtrip 全局收敛要求；择优规则确定（达容差者优→E 小者优→平手取相位 2）。唯一性判定＝解处数值雅可比列秩（列字典序贪心消元，秩亏⟺退化族）；ExactNonUnique＝自由坐标字典序钉中性值 0 约束重解定值＋解集证人（首解＋定值解＋逐自由坐标固定偏移投影证人）；去重容差＝第 5 项同尺度 1×10⁻⁹。输出面规范分支：θ/α wrap (−π,π]＋"偏距非负"支（恒等式 T(θ,d,a,α)=T(θ+π,−d,−a,α+π)；浮点噪声经 1×10⁻¹² m 容差吸收）——黄金数据集（WP-13-T16）同规。自由坐标语义注：卡 §7.5 行文"平行轴 d 自由族"基于经典轴线语义；按 §7.4 展开定义的目标集（逐帧原点+z 轴）实现后平行样例的自由坐标为末级 θ（重合轴为 θ1/θ2）——退化族判定、字典序定值与 ExactNonUnique 结论纪律不变，属实现细化非语义变更。求解器过程常数（微分步长/迭代上限/阻尼界限/主元容差）为数值程序参数，与附录 D 第 4/5 项工程阈值分属两类（DhConvert 文件头注）。③等价验证零位折叠（§7.6 实现细化）：runtime §4.2 Description 无零位承载（CompilerImpl S5 组装恒置 zeroOffset=0——runtime.md §15.4 v0.12 对端登记），modeling→Description 映射执行折叠——origin_desc=origin·R(axis,zeroOffset)（权威零位旋转入几何，与 §7.4 展开式 Rot_z(θ+q0) 零位对齐一致）＋bounds/workingRange 平移 −zeroOffset（权威 q→RobWork q）；两侧折叠同规，零位错位即 FK 超差。链级 Description 构造为等价验证专用内部面（关节/连杆身份名称/基座预设，物性 NotProvided——FK 对照是运动学结论）；全量构造器归 T12 ICanonicalModelInputBuilder，非重复实现。④命令协作面（§7.6/C-6）：`HandlerServices` 表尾追加 dhConverter/dhCompileProbe 两注入位（nullopt＝未装配，切换变体到达即装配缺陷 fail-fast）；IModelingCommandHandler::prepare 公共段增 ③.5 权威切换门——触发条件＝正向载荷∧基线 Explicit∧候选 StandardDH（对非切换载荷恒假，五命令既有行为零变化）；C-6 命令面投影＝载荷恰一根槽＋候选除权威侧字段（authority/dhDerived/axis/origin）外与基线逐字段一致（混合编辑→RejectedInvalidInput）；转换判定与等价验证在 prepare 内执行，拒绝→RejectedHardAssert＋计划清空＋MDL-DH-* 诊断全量传导（验证失败不产生修订）；通过→候选以 prepare 计算解重编码（不信任载荷自带的权威参数声明）；子类钩子契约（Planned|RejectedInvalidInput 两态）不变。⑤§9.5 T09 行三码注册：MDL-DH-NOT-EXPRESSIBLE（转换/error→InfeasibilityProof/Error——结构前提终判为有效工程结论，TEMPLATE-RANGE 同判例）、MDL-DH-APPROXIMATE（转换/Warning→InfeasibilityProof/Warning——权威资格否定不可确认放行，比较型强制）、MDL-DH-ANALYSIS-FAILED（转换/error→ExecutionFailed/Error——执行失败轴不构成语义结论，非比较型）；DiagCodes.hpp 常量＋DiagCodes.cpp 描述符表尾追加（paramSchema "[]"）；DiagCodesTest 分期封闭性同步 T02+T05+T06+T07+T08+T09 二十码。⑥测试：test/DhConvertTest.cpp（两模式均编译——ACC1 无损展开逐级累乘/零位对齐/ChainEmpty·DegenerateBase 拒绝＋ACC2 五状态样例/prismatic 终判不进求解/Exact 逐关节回收＋ACC3 重合与平行轴 ExactNonUnique 字典序定值/Approximate 附 E 与收敛态/AnalysisFailed 溢出反例/确定性）＋test/DhConvertEquivalenceTest.cpp（集成模式 gating——ACC4 verifyEquivalent 正/负路径与 roundtrip 双重一致＋ACC5 域门四道/命令 prepare 正路径·C-3 终判拒绝·C-6 混合编辑拒绝·非切换载荷零影响·装配缺失 fail-fast；CompileProbe 测试替身以 runtime 公共 CanonicalModelBuilder 装配 S5——产品编译器类在 runtime/src/CompilerImpl.hpp 为单元私有头，R-2 禁跨单元 include，生产探针归 L5 装配）。⑦P-MDL-8 处置：runtime Compiler/CanonicalModel/Description 按当周现状消费——buildCanonicalModel 分段入口签名未变（blocked 触发未发生）；Description 无零位承载的对端语义（v0.12）已按消费侧落实（见③）。⑧验证留痕：双模式构建零错误（集成 Release 全量＋独立冒烟 build_smoke_t09——vcpkg toolchain＋Qt prefix 前缀，v0.4 ⑤同口径）；矩阵合成逐元素算术（rotMul/transformMul/rotVec/axisAngleRotation）——rw operator*（R·R/R·v）内经 multiply() 外联符号，冒烟不可链接（BaseWorldTransform 同款纪律）；`sdurws_ird_modeling_test` 159/159（新增 20 条：MdlDhConvert 9＋MdlDhEquivalence 6＋MdlDhCommand 5）＋`sdurws_ird_modeling_contract_test` 9/9 双模式通过（集成 159＋9、冒烟 128＋5〔gated 子集：DhConvertEquivalenceTest 消费 runtime 公共构造器与 policy 评估器，随 TARGET sdurw_kinematics gating 不编入〕）；gtest XML＋ird-test-report.json 双模式留痕 traceability/builds/wp13-t09/；validate-task/validate-docs 双 PASS；ird_gates base（0bb40848）..branch 命中集归一化多重集比对（base 侧经临时 worktree 直跑同脚本）＝EMPTY——存量命中零新增。文档头版本升 v0.10 |
| v0.11 | 2026-09-25 | WP-13-T09 验收返工登记（attempt 1 fail→fix；验收记录 traceability/acceptance/WP-13-T09-20260925.md）：①B-1/F-337 消账——contract_test/CommandPipelineContractTest.cpp 的 T08 时代"T09 行不预建"钉住断言（registry.find("MDL-DH-NOT-EXPRESSIBLE")==nullptr）随本任务 §9.5 T09 行三码合法登记而过期，更新为在册断言：T09 三码经常量同源对账查得＋ownerUnit=modeling＋NOT-EXPRESSIBLE confirmable=false（C-3 拒绝面依赖语义）＋APPROXIMATE requiresComparison=true（比较型强制）逐项断言；缺席断言仅余 T13/T18 行（MDL-EXPORT-FAILED/MDL-21-COUPLING-STAGE-LOCKED）——分期封闭口径与 DiagCodesTest FactoryScopeIsStagedRows_WP13T09 对齐；测试更名 T08StableCodesRegisteredWithoutPrebuilding→StableCodesRegisteredWithoutPrebuilding_WP13T09（T08 分期测试更名先例 FactoryScopeIsStagedRows 推进口径；后续任务行登记按同款"缺席→在册"推进）。②F-339/F-316 同族消账——DiagCodes.hpp modelingCodeDescriptors @brief 计数"当前 19 项"→20 项（工厂实际 20 项；DiagCodes.cpp 文件头注与 DiagCodesTest 二十码封闭断言本已正确，纯注释滞后零行为影响）。③F-341 消账——DhConvert.cpp lmSolve 残留 IRD_DH_DEBUG 调试块清除（原块 std::fprintf 未含 \<cstdio\>、宏一旦定义即编译失败、if 缩进错位）；初值非有限分支保留原语义（Diverged 不产参数），就地补注分支业务含义。④F-340 消账——集成侧 ird-test-report.json 重收为全量 168 条（sdurws_ird_modeling_test 159＋sdurws_ird_modeling_contract_test 9，两 exe 全量运行后经确定性序合并；summary 与两份 gtest XML 逐位可对账），替代原 9 条契约子集。⑤F-338 维持 open 不消账（契约 verify 数组缺 sdurws_ird_modeling_contract_test 构建行——F-300 家族）：契约文件为 CCP 冻结产物、登记所有者为契约编译/治理侧，实施侧不私改契约本体；本验收按 interim 口径补建该目标后全量执行（ctest 2/2）。⑥验证留痕（返工后源码全新捕获，traceability/builds/wp13-t09/ 五件全部重录）：集成模式 modeling 单元对象树清零后全量重编译（三目标零错误）亲跑——`sdurws_ird_modeling_test` 159/159＋`sdurws_ird_modeling_contract_test` 9/9（含更名后分期注册用例），gtest XML 双件（integration-modeling-test.xml 159/0、integration-modeling-contract-test.xml 9/0）与合并报告 ird-test-report.json（168/168）一致、可由当前源码复现（B-1 留痕矛盾消解）；冒烟模式全新目录 build_smoke_t09-r2 配置构建零错误亲跑——128/128＋5/5（gtest XML smoke-modeling-test.xml 128/0＋smoke-ird-test-report.json 128 条全量）；ctest -L "^ird$" 2/2；validate-task/validate-docs 双 PASS；ird_gates 引擎直跑口径（F-336 建议）base（0bb40848，临时 worktree）..branch 归一化多重集比对 411=411 diff 为空＝零新增命中。文档头版本升 v0.11 |
| v0.12 | 2026-09-25 | WP-13-T10 实现落位登记（文档同步；任务契约 tasks/foundation/WP-13-T10.json）：①**Parts 编辑流**（§3.3 表 Parts.hpp T10 行）：a) `checkInvariants(ToolDefinition)` 增核 **I-MDL-13**（§4.10 新增行——tcpList≥1 且键非空唯一〔§4.4 tcpList 行约束编号化，枚举表尾追加；经 Codec 解码门校验链④自动强制——命令载荷与存储字节双通道共用〕）；b) `mergeNamedPoseEntries` 命名位姿合并编辑流（§4.6/D-MDL-3/MDL-17：保留键 `homeConfiguration`/`zeroConfiguration` 拒绝写入〔MDL-17"除 Home/Zero 外"字面〕＋基线保留键原样保留〔V-27 建模侧——KIN-06 复位走会话命令零修订〕＋条目 jointConfiguration 与根关节表长度一一对应〔§4.6 字面——对照 §4.7"逐可动关节"的有意区分〕＋键唯一非空＋键字典序规范化产物；局部错误轨道 PoseEditErrorCode——EstimateErrorCode/DhErrorCode 同款先例）。②**命令载荷 v2**（CommandHandlers.hpp/.cpp——kCommandPayloadVersion 1→2、magic IRDMCP2；v1 拒收＝NFR-DEP-04＋R-MDL-5 草稿短命数据口径）：新增 removals 引用移除段（PayloadRemovalSlot；§9.3 表行 apply-scene-objects"移除引用"与 §4.8"删除＝引用移除"的命令载体——移除＝仅根写入，被移除对象零写入〔PA-2/CON-02 对象字节与历史修订闭包保留〕；Restore 模式携带 removals＝无效载荷）。③**AssertionSuite 增两断言**（§9.4.4 单一判定面）：assertDefaultTcp（defaultTcp 闭包级引用完整性——I-MDL-9/KIN-14/§8.2 L7：有工具引用未设 defaultTcp／toolOid∉toolRefs／tcpKey 不在被引工具 tcpList 三面阻断；Readiness.cpp runL7 呈现级 addNote 面退役改调同一实现）＋assertReferenceProtection（V-04——被 defaultTcp 引用工具的移除请求逐对象拒绝，比较型定位 subject=oid、actual=1/expected=0；prepare ③.6 保护门先于其余断言＝拒绝面精确定位）；DecodeOutcome 表尾追加 removedRefs（钩子只陈述移除事实，保护判定统一归基类——钩子契约两态不变）。④**三命令差异面**：apply-tool-definition 移除面（写入/移除二选一——混载无效；移除对象须在闭包且被引用）＋写入面候选播种修复（isNew 路径候选未从基线播种＝潜伏缺陷，基线部件在候选闭包缺失——随 T10 defaultTcp/闭包断言显形，本任务修复并登记）；apply-scene-objects 批量增/改/移除混载（根写入单次）；apply-named-poses 合并流接入（合并失败＝RejectedInvalidInput；requiresDualCompile=false 不变）。⑤**移除命令摘要**附"可能存在外部引用"提示（§4.8/P-MDL-6——跨域引用直检禁止 R-1；悬空检测归 requirements 就绪校验；⑤事件随修订提交由 project TxEngine 发布〔RevisionCommitted→DependencyInvalidated，§7.1 第 6 步已验契约〕，本卡观测点＝摘要提示面——V-20 同款"本单元可执行范围"口径）。⑥**§9.5 增登 T10 行两码**（v0.12 实现期增登，表尾追加）：`MDL-REF-PROTECTED`（校验/error→InputInvalid/Error、比较型强制、paramSchema [object-id, reference-holder]）＋`MDL-READINESS-DEFAULT-TCP-INCOMPLETE`（校验/error→ResourceMissing/Error、非比较型、paramSchema [tcp-key, tool-id]）；DiagCodes.hpp 常量＋DiagCodes.cpp 描述符表尾追加（清单 20→22）；DiagCodesTest 分期封闭性推进 T02+…+T09+T10（22 码）＋逐字段锚定增两分支。⑦测试：新增 `test/PartObjectCommandTest.cpp`（MdlPartObjectCommands 12 条——ACC1 tcpList/defaultTcp 三面/工具引用不复制；ACC2 V-04/V-05/批量混载/移除约束；ACC3 worldPose 不预乘安装旋转〔V-12 建模侧〕/角色五值词表与 policy 侧逐值同串/collisionProfileHint 往返保真且零④端口消费；ACC4 requiresDualCompile=false＋保留键保留＋保留键与关节序拒绝面＋位姿内容不入根字节〔D-MDL-3 建模侧〕；ACC5 载荷 v2 removals 往返与破损/写入面）；PartsTest 增 2 条（I-MDL-13 四反例＋解码门强制；合并流保留键/关节序/违例面）；CommandHandlersTest 载荷破损用例同步 v2 魔数（IRDMCP2）＋v1 拒收断言——钉住断言随合法登记同步（T09 attempt 1 B-1 先例）。⑧验证留痕：集成 Release 全量构建 0 错误；`sdurws_ird_modeling_test` 175/175＋`sdurws_ird_modeling_contract_test` 9/9（ird-test-report.json 合并 184/184）；独立冒烟 Release（vcpkg toolchain＋CMAKE_PREFIX_PATH Qt6 前缀——v0.9 ⑤同口径）130/130＋契约 5/5（smoke-modeling-test.xml＋smoke-ird-test-report.json 130 条全量）；ctest -L "^ird$" 2/2；validate-task/validate-docs 双 PASS；ird_gates 引擎直跑口径 base（a2d1ae6c，临时 worktree）..branch 归一化多重集比对 56=56 diff 为空＝零新增命中（traceability/builds/wp13-t10/ird-gates-comparison.txt）。⑨范围注记：Debug 配置冒烟构建在 T09 域用例 MdlDhConvert.DhToExplicitZeroAlignment_WP13T09_ACC1 出现 SEH 崩溃（本任务 diff 不触达 DhConvert 路径；T09 留痕与集成模式均为 Release 口径——Debug 配置缺陷登记归 T09 域，不冒充本任务验证面）。文档头版本升 v0.12 |
| v0.13 | 2026-09-25 | WP-13-T11 实现落位登记（文档同步；任务契约 tasks/foundation/WP-13-T11.json）：①**基座安装姿态编辑流**（§5.2 SetBasePlacement 域内核，§3.3 表 Template.hpp 行 T11 落位）：`BasePlacementEditValue`（整体替换语义——preset 直用 runtime::InstallationPresetToken 词表〔单一权威，不另设词表〕＋customEaa 仅 Custom 允许携带〔携带即调用方契约违约 fail-fast〕＋basePosition m）＋`BasePlacementEditErrorCode` 四值（ValueNotFinite/CustomEaaMissing/PresetIdentityRotation/RotationNotOrthogonal）＋token 表＋`applyBasePlacementEdit`（检查序：有限性 I-MDL-3→custom 缺 customEaa＝I-MDL-7 前半→**preset≠ground 而 R=I 映射层拒绝**＝I-MDL-7 后半〔T03 §15 v0.4 增量 f) 登记的 T11 落位点；恒等判定逐元素 1×10⁻¹²，与 runtime checkPresetConsistency 的 InputInvalid 同口径〕→正交性复用 checkInvariants IMdl7 单一判定〔NFR-MNT-04〕→候选构造〔切离 Custom 时 customEaa 复位 NotProvided——显式语义非静默清除，NFR-COR-03〕→提交＋恰一条变更摘要；拒绝工作集字节不变——applyJointFieldEdit 同款强保证）；EAA→R 唯一经 runtime rotationFromCustomEaa 权威换算（P-RT-4）——modeling 只存参数不存矩阵（M-11/§7.6 隔离）。②**P-RT-4 交叉核对留痕**（O-16 兑现行）：traceability/wp13-t11-prt4-crosscheck.md——唯一权威产出点＝runtime BaseWorldTransform.hpp::installationPresetRotation()（倒挂 R_x(π)／壁装 R_y(π/2)，runtime.md §6.2 冻结值）双方一致无分歧；V-12 建模侧输入面断言＝Description.base 只含 preset/customEaa/basePosition（§9.1 base 行），无任何预乘旋转矩阵（场景 worldPose 不预乘已由 T10 ACC3 落位）；文件含 §7.7 引用失真卡缺陷注记（契约 note ⑤）。③测试：新增 `test/BasePlacementEditTest.cpp`（MdlBasePlacementEdit 9 条，两模式均编译——命令路径用例选移动关节设计，hasTravelRelevantJoints 不触发④策略端口：ACC1 三预设＋custom＋位置编辑面／预设词表类型级钉住〔static_assert 复用 runtime 枚举〕／apply-robot-design 原子持久化〔恰一根写入＋requiresDualCompile＋inverse 前版字节 D-MDL-9／PA-2＋写入字节解码核对＋二次编辑仍单写入〕；ACC2 模板路径 UserProvided/Template 来源零值〔V15-04〕＋导入路径 ImportMapped 默认补全清单＋I-MDL-7 非法组合〔缺 EAA／零 EAA R=I／容差边界 1×10⁻⁶ rad 接受·1×10⁻¹³ rad 拒绝／EAA·位置非有限／非 Custom 携带 EAA fail-fast／全部拒绝路径工作集字节不变〕；ACC3 P-RT-4 三预设冻结矩阵逐元素交叉核对＋与 installationPresetRotation() 输出一致＋位置透传；ACC4 custom EAA 手算 R_z(π/2) 编译单一字段＋checkWorldBaseTransform/checkPresetConsistency 通过＋字节面单字段证据；RotationNotOrthogonal 防御闸在 Rodrigues 构造下数学不可达——无公开输入面负向用例，正交容差判定本体由 RobotDesignTest I-MDL-7 用例钉住）。④**§3.3 表归属订正**（DTB §5.4 偏差登记）：Parts.hpp 行的 T11 归属移至 Template.hpp 行（T07/T11）——basePlacement 为根对象字段，其编辑域内核与逐轴编辑流（applyJointFieldEdit）同址 Template.hpp，Parts.hpp 载部件对象值模型、无根字段编辑面。⑤CMakeLists.txt：测试目标增列 BasePlacementEditTest.cpp（T11 增列注释）。⑥验证留痕（最终源码全新捕获）：集成 Release 三目标构建零错误；`sdurws_ird_modeling_test` 184/184（新增 9 条 MdlBasePlacementEdit）＋`sdurws_ird_modeling_contract_test` 9/9（ird-test-report.json 合并 193/193）；独立冒烟 Release（build_smoke_t11——vcpkg toolchain＋CMAKE_PREFIX_PATH Qt6 前缀，v0.8 ⑤同口径）139/139＋契约 5/5（smoke-modeling-test.xml＋smoke-ird-test-report.json 144 条全量）；ctest -L "^ird$" 2/2；validate-task/validate-docs 双 PASS；ird_gates 引擎直跑口径 base（bf9c995d，临时 worktree）..branch 归一化多重集比对（57 SUB/41 R3/10 LIB/5 T1/3 R4/1 T2/1 R5/1 R1 双侧一致）diff 为空＝零新增命中（traceability/builds/wp13-t11/ird-gates-comparison.txt）。文档头版本升 v0.13 |
| v0.14 | 2026-09-25 | WP-13-T12 实现落位登记（文档同步；任务契约 tasks/foundation/WP-13-T12.json）：①**编译协作桥落位**（§3.3 表 CanonicalBridge.hpp 行 T12——`CanonicalBridge.hpp`＋`src/CanonicalBridge.cpp`）：`ObjectClosureView`（§9.2 闭包域字节源抽象——`tryObjectByToken` 根路由面〔kRobotDesignObjectType 单点，§4.2 不另设第二常量〕＋`tryObject` 部件解引用面；`ClosureObject{objectTypeToken, bytes}`；实现方约束＝并发只读安全/确定性/闭包域纪律；非 owning——L5 装配期与 CompileRequest.objects 同源绑定）＋`ICanonicalModelInputBuilder`/`CanonicalModelInputBuilder`（§9.4.5 build——闭包→`runtime::RobotDesignDescription` 全量构造器，两态不产诊断记录〔值面错误＋定位参数；无 §9.5 登记码不产诊断——DhConvert 先例〕）＋`RobotDesignReader`（runtime `IRobotDesignReader` 产品实现——`read(objectBytes, formatVersion)` 单根字节输入＋构造期绑定闭包〔空指针＝装配违约 fail-fast〕；失败恒 RuntimeError InputInvalid＋modeling 错误 token＋定位参数入 detail——runtime.md §5.2 S2 归属表；格式版本门＝kRobotDesignSchemaVersion 单点，NFR-DEP-04）。②**§9.1 字段映射表全量落位**（十类来源映射逐行 UT——CanonicalBridgeTest ACC1）：descriptionContractVersion←schemaVersion 同源（R-MDL-3；初值 1）；robotLocalName←displayName＋输入质量面；joints（Explicit 直映／StandardDH 先经 §7.4 展开为显式表示——`expandStandardDhGeometry` 内部执行体，转换器单一实现语义单一源 NFR-MNT-04；零位折叠 T09 v0.10③ 同规——origin_desc=origin·R(axis,zeroOffset)＋bounds/workingRange 平移 −zeroOffset；maxVelocity/maxAcceleration＝NotProvided——§4.3-A 无落点 v0.6②a 不发明字段；Fixed 无可动轴且 axis 缺失→+Z 单位向量中性填充〔DerivedReadOnly＋methodTag "fixed-axis-neutral"——结构承载面无运动语义，实现细化登记〕；可动关节 axis/origin 缺失＝必备输入缺失→RefMissing 拒绝不伪造）；links（body 物性三元共用映射＋材料 materialId＋visual/collision 经 resourceManifest 解引用）；tools（经 toolRefs 闭包解引用＋token 复核；defaultTcp 引用工具居首——runtime"首项为默认 TCP"；tcpOffset＝mountInterface∘所选 TCP 条目〔默认工具按 defaultTcp.tcpKey、其余取 tcpList 首条——确定性选取细化〕；有 tools 而 defaultTcp 缺失/toolOid∉toolRefs/tcpKey∉tcpList→RefMissing）；scene（经 sceneRefs 解引用；worldPose 世界系固连原样透传不预乘安装旋转——M-11；geometry 缺失→RefMissing——runtime CanonicalSceneObject.geometry 必有，不可伪造）；base（preset/customEaa/basePosition 编辑表示透传，**不在此计算 R_world_base**）；drivetrain（drivetrainRef 解引用；ratioPerJoint 透传＋无量纲 SI 门；coupling 映射面 R2 预留——闭区间→半开区间换算）；friction（frictionPerJoint 逐关节透传＋NotProvided 保真——DYN-06）；resourceRefs（根 resourceManifest 全量——Solidified→solidifiedObject.objectId＋闭包存在性复核；Recorded→确定性声明键〔非对象不复核闭包——O-36〕；contentDigest 透传不自算——**不重复计算任何内容身份**（ContentVersion/builtFrom/contentIdentity 归 project/runtime）；sourcePathHint 恒空＝路径不入 Description 即不入身份；accessVersion＝io::kAccessVersion 单点〔P-RT-6〕）。③**实现层细化登记**（DTB §5.4）：a) robotLocalName 源＝根对象 displayName（模型名承载——T05 §6.3"displayName＋根 localName 候选"/T07 createDraft 基名/T09 链级构造三条在案实践同源）；输入质量面＝空串或含 '/' 拒绝（IllegalName——runtime §4.3.3 硬规则的建模侧预演，不伪造默认名、不假设 runtime 修复）；其余字符原样透传（消歧/合法化归 runtime，R-4）。§4.3 displayName 行"不进编译身份"随之窄化：displayName 作为 robotLocalName 源进入 Description（MDL-14 设备作用域名源），呈现语义不变。b) 单位 SI 化唯一经 core Units 换算入口（SA-12 履行点）：有量纲标量（rad/m/kg/kg·m²/1）经 tryConvert 同量纲恒等门（非有限拒绝）；摩擦系数族（N·m·s/rad）无 R1 量纲行——有限性门履行 SI 语义，不私设换算。c) Invalid 态 SourcedValue→UnitIllegal（已提供但非法无 SI 真值可映射——I-MDL-3 履行点/NFR-COR-03）。d) Recorded 资源声明键＝（域串"recorded-resource", 清单 resourceId）的 FNV-1a 双实例 128 位派生（Import deriveTempObjectId 同族算法——同输入同键、不经随机源 NFR-COR-02；io.md §8.5 Recorded 分支按此键回查外部引用记录；非 project 对象身份、不参与对象库编址）。e) 工具/场景跨对象作用域 localName 重复→IllegalName（V-26 同作用域无重复——解码门 I-MDL-2 链作用域不覆盖跨对象作用域；重复拒绝不改名，消歧唯一归 runtime R-4）。④**O-36 裁决落地**（P-MDL-1 关闭面，2026-09-22 裁决）：builder 闭包复核范围＝真实存储对象（根〔token 路由〕/工具/场景/传动/固化资源），关节/连杆子 ObjectId 为模型内标识不查闭包（内嵌根字节、随 Description objectId 字段透传供 S5 唯一性/名称映射锚——UT ClosureScopeRealStoredObjectsOnly 钉住）；资源对象 token 归 project/io 词表——只核存在性不核 token。⑤P-MDL-8 处置：runtime Description/Compiler/CanonicalModel/Errors/Sources 按当周现状消费——§4.2 字段集未变（RT-T11 objectId 增量已于对端 v0.12 同步），blocked 触发未发生；S5 输入面对齐（CanonicalTool.geometry 可空/CanonicalSceneObject.geometry 必有/zeroOffset=0 显式表示已折叠）在 CompileBridgeContractTest 端到端钉住。⑥测试：新增 `test/CanonicalBridgeTest.cpp`（MdlCanonicalBridge 24 条，两模式均编译——纯函数服务零命令/策略依赖，自持夹具 `test/BridgeFixtures.hpp`：ACC1 十类映射逐行〔契约版本同源/robotLocalName/joints Explicit＋DH 展开＋零位折叠手算对照/links 物性六分量→3×3/tools defaultTcp 居首与 tcpOffset 合成/scene 世界系固连不预乘/base 编辑表示＋无矩阵承载/drivetrain＋friction/resourceRefs 两态＋Fixed 中性轴〕；ACC2 同闭包→同 Description 深比较＋reader 无缓存＋资源摘要透传不自算；ACC3 单根字节输入/闭包域解析计数观测/格式版本门/O-36 复核范围/空闭包 fail-fast；ACC4 RefMissing·UnitIllegal·DhExpandFailed·SchemaVersionUnsupported 定位到对象 ObjectId/字段＋三层防线边界〔必备输入缺失值面拒绝——不假设 runtime 修复；两态零诊断记录〕；ACC5 R-4 名称透传不消歧＋V-26/AT-18 输入质量面〔字符集/robotLocalName 硬规则/工具作用域重复〕＋资源状态声明口径）；契约测试增列 `contract_test/DescriptionBridgeContractTest.cpp`（3 条，两模式——§4.2 字段集十类对齐＋RT-T11 objectId 透传＋预设词表类型级钉住/schemaVersion↔descriptionContractVersion 同源初值 1/reader 失败轨值面契约）＋`contract_test/CompileBridgeContractTest.cpp`（2 条，集成模式 gating——reader 产出→runtime 公共 CanonicalModelBuilder 装配 S5 端到端对齐〔R-2 装配取舍＝T09 RuntimeCompileProbe 同款纪律，产品编译器私有头不跨单元 include，生产装配归 L5〕＋reader 失败面不达 S5；objectRefs 装配为收窄前兼容形态——runtime S5"模型内对象∈objectRefs"实现尚未按 O-36 裁决收窄〔runtime 卡增量义务〕，装配不裁决口径，建模侧口径由 CanonicalBridgeTest 钉住）。⑦CMakeLists.txt：库增列 src/CanonicalBridge.cpp；`sdurws_ird_modeling_test` 增列 CanonicalBridgeTest.cpp（两模式）；`sdurws_ird_modeling_contract_test` 增列 DescriptionBridgeContractTest.cpp（两模式）＋CompileBridgeContractTest.cpp（gated TARGET sdurw_kinematics）。⑧验证留痕（最终源码全新捕获，traceability/builds/wp13-t12/）：集成 Release 三目标构建零错误；`sdurws_ird_modeling_test` 208/208（新增 24 条 MdlCanonicalBridge）＋`sdurws_ird_modeling_contract_test` 14/14（新增 5 条——3＋2 gated；ird-test-report.json 合并 222/222）；独立冒烟 Release（build_smoke_t12——vcpkg toolchain＋CMAKE_PREFIX_PATH Qt6 前缀，v0.8 ⑤同口径）163/163＋契约 8/8（smoke-ird-test-report.json 171 条全量；gated 2 条不编入）；ctest -L "^ird$" 2/2；validate-task/validate-docs 双 PASS；ird_gates 引擎直跑口径（F-336/F-347——IRD_SELFTEST_DIR 缺省 TEMP）base（1752bb85，临时 worktree .wt-base-t12 直跑同脚本，用毕即删）..branch 归一化命中直方图比对（57 SUB/41 R3/10 LIB/5 T1/3 R4/1 T2/1 R5/1 R1＝发射 119 次）完全一致 diff 为空＝零新增命中（traceability/builds/wp13-t12/ird-gates-comparison.txt）。文档头版本升 v0.14 |

> 自审声明：本文档自审仅覆盖设计一致性、边界与上游对齐，不等同于实现测试通过或正式验收（acceptance-protocol.md 流程另行执行）。

| v0.15 | 2026-09-25 | WP-13-T13 实现落位登记（文档同步；任务契约 tasks/foundation/WP-13-T13.json）：①**规范模型包导出/导入落位**（§3.3 表 Package.hpp 行 T13——`Package.hpp`＋`src/Package.cpp`）：`IModelPackagePort`/`ModelPackagePort`（§9.4.9 行签名——`exportPackage(closure, target, diags)`/`importPackage(source, diags)`；会话级文件操作零修订）＋`PackageExportTarget`（目标/替换策略/createdAtUtc 调用方传入——服务不取时钟，NFR-COR-02）＋`roundtripChecklist`（五组逐项核对清单唯一实现——authority/physics/resource/collision/pose，V-20 核对形态；selfCollisionHints 不入清单——§4.3-B 编码权威边界）＋`SolidifiedResourceCopy`（固化资源字节副本——CON-03 回读面）。②**等价命名登记**（DTB §5.4）：§9.4.9 行"ExportOutcome/ImportOutcome"落位为 `PackageExportOutcome`/`PackageImportOutcome`——Import.hpp `ImportOutcome`（URDF 映射报告）已占用同命名空间名，Package 前缀消歧，签名形状与语义不变。③**包格式与 ZIP 容器实现登记**：包＝modeling 自有工件（契约 note ⑤——与 io .rwpack 传输封装不同物，不混用格式契约），布局＝`manifest.json`（formatId "ird-model-package"/producer "ird-modeling"/schemaVersion 1/entries 按 path 字典序〔逐条 path+size+sha256〕/contentDigest＝清单规范序列 SHA-256）＋`objects/<token>/<oid>.bin`（根/部件 canonical 字节**原样入包**——CON-01/PA-2 不重编码）＋`resources/<oid>.bin`（Solidified 副本）。ZIP 写侧＝本单元自实现 STORED 最小 APPNOTE 容器（确定性：固定 DOS 时间戳/条目字典序/CRC-32 容器字段；<65535 条目防御门）——io 公共面无 ZIP 写出器（ZipChannel 只读、libzip 写侧为 io 私有实现），而 modeling 新增 libzip 链接属未登记第三方依赖（DTB §4 通道，所有者裁决，本任务不引入）；读侧统一消费 io `IZipChannel`（IO-T04 落位面——导入三道校验与导出预提交自检的读权威），形成"写已知、读权威"闭环。CRC-32 为容器规范自带传输校验，非内容身份——内容身份唯一 SHA-256（SA-12 不变）。④**导出原子性实现登记**（MDL-20"恢复先前输出"/V-29）：内存装配→io `ITempAreaManager` PackExport 会话暂存→`IZipChannel` 重开暂存文件自检（逐条目哈希＋manifest 摘要比对——不绿即拒绝替换）→io `IAtomicFileWriter` prepare/write/commit 原子替换目标；任一步失败清理暂存、目标零接触、旧输出完好。暂存自检不用 AtomicTarget 暂存位（commit 前独占句柄，libzip 打不开——实现注），按 §7.2 导出协议"临时区→校验→替换"时序承载。⑤**WC/DWC XML 外供编排面**（§6.8 第二条/ACC5）：`exportWorkCellXml(snapshot, target, diags)`——数据源＝runtime 快照只读视图（WorkCellConstView/DynamicWorkCellConstView），零修订/零失效/不私设第二编译路径；XML 为 modeling 自有外供查看表示（元素命名镜像 RobWork WorkCell/DWC 概念），非 RobWork wc.xml 格式——框架写侧 sdurw_loaders 属"原则不使用、启用须登记"表行（DTB §4.6，本任务不引入，与 io/modeling 导入通道自行实现 XML 处理同款设计哲学）；数值 std::to_chars 最短往返、逐项只读不改内容；实现于 `src/PackageWorkCell.cpp`（gated TARGET sdurw_kinematics——runtime 编译产物非模板类，冒烟模式声明可用无 TU 消费）。⑥**诊断码登记**（§9.5 T13 行两码注册；Errors.hpp 表尾追加两值）：`MDL-IMPORT-PACKAGE-UNKNOWN`（导入/error→InputInvalid；非本软件工件 fail-closed——manifest 缺失/formatId/producer 不符/容器不可读，引导 MDL-18/R2 通道，不代替其解析——mapWorkCellXml R1 NotImplemented 边界不受影响）＋`MDL-EXPORT-FAILED`（导出/error→ExecutionFailed；原子写出链路任一环失败，项目状态不变）；域错误面 `ModelingErrorCode` 表尾追加 `PackageUnknown`/`ExportFailed`（映射经 modelingDiagCode 登记）；包格式版本超前→SchemaVersionUnsupported 值面拒绝不产诊断（MDL-READINESS-SCHEMA-UNSUPPORTED 语义为对象 schema 主版本，不混用）。⑦测试：新增 `test/PackageTest.cpp`（MdlPackage 6 条，两模式均编译——ACC1 包形态〔manifest 来源标识/schema/清单 digest/根部件字节/Solidified 副本/命名位姿/STORED/非加密〕＋确定性同包字节＋闭包零突变；ACC2 V-29 失败恢复〔NeverOverwrite 拒绝＋父目录缺失〕＋MDL-EXPORT-FAILED 诊断＋无半成品＋RefMissing 值面不产诊断；ACC3 非本软件工件拒绝〔PackageUnknown＋引导诊断〕＋mapWorkCellXml R1 边界不受影响；ACC4 roundtrip 五组清单 diff=空＋对象值相等＋Solidified 字节逐字节回读＋manifest 摘要回环；篡改拒绝 fail-closed）；新增 `test/PackageWorkCellTest.cpp`（MdlPackageWorkCell 1 条，gated 集成——S5 替身＋S6~S10 真实工厂编译快照→两面 XML 内容逐项只读核对〔设备/关节限位/重力/体物性透传〕＋确定性＋V-29＋DWC 能力缺失拒绝面）；`DiagCodesTest` 钉住断言随 T13 合法登记同步（分批封闭性扩 T13 行＋字段锚定两分支——T10 先例）；`CommandPipelineContractTest` "T13 行不预建"钉住断言过期化改为在册断言（T09 行同款推进先例）。⑧CMakeLists.txt：库增列 src/Package.cpp（两模式）＋src/PackageWorkCell.cpp（gated）；`sdurws_ird_modeling_test` 增列 PackageTest.cpp（两模式）＋PackageWorkCellTest.cpp（gated）。⑨验证留痕（最终源码全新捕获，traceability/builds/wp13-t13/）：集成 Release 三目标构建零错误；`sdurws_ird_modeling_test` 215/215（新增 6 条 MdlPackage＋1 条 MdlPackageWorkCell gated）＋`sdurws_ird_modeling_contract_test` 14/14（ird-test-report.json 合并 229/229）；独立冒烟 Release（build_smoke_t13——vcpkg toolchain＋CMAKE_PREFIX_PATH Qt6 前缀）169/169＋契约 8/8（smoke-ird-test-report.json 177 条全量；gated 1 条不编入）；ctest -L "^ird$" 2/2＋ctest -R sdurws_ird_io_test 1/1（对端回归）；validate-task/validate-docs 双 PASS；ird_gates 引擎直跑口径（F-336/F-347）base（2979e5cd，临时 worktree .wt-base-t13 直跑同脚本，用毕即删）..branch 归一化命中直方图比对（29 SUB/21 R3/5 LIB/3 T1/2 R4/1 T2/1 R5/1 R1＝发射 119 次）完全一致 diff 为空＝零新增命中（ird-gates-comparison.txt）。文档头版本升 v0.15 |
| v0.16 | 2026-09-25 | WP-13-T14 实现落位登记（文档同步；任务契约 tasks/foundation/WP-13-T14.json）：①**Model Diff 差异增量表数据实体落位**（§3.3 表 ModelDiff.hpp 行 T14——`ModelDiff.hpp`＋`src/ModelDiff.cpp`）：`IModelDiffService`/`ModelDiffService`（§9.4.9 行签名——`diff(baseline, candidate)→ModelDiffReport`，两 ModelingWorkingSet 输入、直返报告无错误轨）＋`ModelDiffReport`（三组 structure/parameters/properties＋方向字段 baselineObjectId/candidateObjectId——自工作集 rootObjectId 原样带入，nullopt＝模板草稿；空差集→三组空表合法报告）＋`ModelDiffEntry`（group＋kind 三态〔Added/Removed/Modified——"candidate 相对 baseline"观察方向，交换输入呈镜像〕＋objectId〔UX-13 点击定位锚——ARC-04；引用表条目锚＝被引用对象 id、defaultTcp 锚＝工具 id；根对象字段＝全零占位〕＋subjectPath〔观察方向下标口径：Added/Modified 取 candidate 下标、Removed 取 baseline 下标〕＋field 叶字段名＋valueChanged/provenanceChanged 两面标记〔来源标记仅变化时 provenanceChanged 单独为真〕＋两侧确定性文本摘要）＋`modelDiffGroupToken`/`modelDiffChangeKindToken` 词表 token。②**分组语义登记**（§9.4.9 行"分组：结构/参数（DH、轴线、限位）/物性"的逐字段映射——ModelDiffGroup 类注钉住，UT 机械钉住防漂移）：Structure＝根元字段（schemaVersion/displayName/notes）＋authority＋basePlacement（preset/customEaa/basePosition——安装构成）＋关节/连杆集合成员与链序位（chainIndex）＋localName/type＋几何引用（visual/collision）＋引用表（defaultTcp/toolRefs/sceneRefs/poseSetRef/drivetrainRef）＋resourceManifest；Parameters＝axis/origin（仅两侧 Explicit）＋zeroOffset＋bounds＋workingRange＋dhDerived（仅两侧 StandardDH）——MDL-08 括注"DH、轴线、限位"为本组代表项，origin 系 MDL-09 与 axis 并列的权威一等字段、zeroOffset/workingRange 系 §4.3-A 登记的关节参数字段，同组承载（映射决策登记，非卡面收窄）；Properties＝body.mass/centerOfMass/inertia/material（MDL-05 物性分层）。③**D-MDL-5 差异面＝编码身份面**（本头核心语义决策）：权威受管三字段（axis/origin/dhDerived——§7.3 C-1/C-2 字段轴）只在两侧同为权威时比较；两侧权威不同→受管字段全部跳过（每字段在至少一侧为派生值），authority 差异条目为唯一语义事实；selfCollisionHints 不入表（不入根对象编码权威语义——§4.3-B 行原文，同一口径）；ModelingWorkingSet.changes 不参与比较（编辑派生记录非模型内容——§4.9 编辑态行）。④**比较面与范围决策登记**（DTB §5.4）：MDL-08"两个 RobotDesign 之间的差异增量表"字面落位——diff 逐字段比较两工作集的 design；部件对象内容（工具物性/场景位姿等）不入本表，根对象引用表（toolRefs/sceneRefs/poseSetRef/drivetrainRef/defaultTcp）变化经根字段入表（锚＝被引用对象 id）；部件对象内容比较随消费方任务（WP-22-T11）按需扩展，本任务不预建。⑤**集合与链序语义**（§4.8 canonical 规范化对齐）：toolRefs/sceneRefs 按 ObjectId 规范文本键集比较（纯重排≠差异——编码层字典序规范化同构，UT 钉住）；resourceManifest 按 resourceId 键集；关节/连杆链序位变化＝chainIndex Modified 条目（数组下标是权威语义——串联有序 §4.3）；派生缓存不一致的 axis/origin/dhDerived 差异零条目（③规则）。⑥**确定性**（NFR-COR-02）：组内排序键＝（ObjectId 规范文本→字段序〔§4.3/§4.3-A/§4.3-B 字段表行序，实现内固定序数承载〕→subjectPath→kind）stable_sort；值摘要文本＝classic locale＋17 位有效数字（同值必同串，不读 locale/环境/时钟——§3.4）；同输入重复 diff 逐字段相等。⑦测试：新增 `test/ModelDiffTest.cpp`（MdlModelDiff 8 条，两模式均编译——纯函数比较服务零命令/策略依赖、自持夹具固定规范文本 ObjectId：ACC1 接口落位＋空差集空报告非 null；ACC2 逐字段三态九处差异双对账＋仅来源变化条目〔provenanceChanged 单独为真〕＋派生只读字段豁免三分支〔双侧 DH/双侧 Explicit/混合权威〕＋同输入同报告＋字段序序列钉住＋引用集合纯重排零条目/增删锚核对/链序位/资源清单语义；ACC3 纯函数输入只读双证据〔operator== 深相等＋RobotDesignCodec canonical 字节前后对照〕＋交换镜像全量核对〔方向字段互换/Added↔Removed/文本互换/两面标记不变〕；ACC4 AT-12 数据侧逐项可观察〔恰 3 处差异↔恰 3 条条目，无聚合无遗漏〕＋条目定位四元组完整；零 Qt 由 BuildRedLineTest 产品面扫描＋ird_gates 复核双承载）。⑧CMakeLists.txt：库增列 src/ModelDiff.cpp（两模式）；`sdurws_ird_modeling_test` 增列 ModelDiffTest.cpp（两模式）。⑨验证留痕（最终源码全新捕获，traceability/builds/wp13-t14/）：集成 Release 三目标构建零错误；`sdurws_ird_modeling_test` 223/223（新增 8 条 MdlModelDiff）＋`sdurws_ird_modeling_contract_test` 14/14（ird-test-report.json 合并 237/237）；独立冒烟 Release（build_smoke_t14——vcpkg toolchain＋CMAKE_PREFIX_PATH Qt6 前缀）177/177＋契约 8/8（smoke-ird-test-report.json 185 条全量）；ctest -L "^ird$" 2/2；validate-task/validate-docs 双 PASS；ird_gates 引擎直跑口径（F-336/F-347——IRD_SELFTEST_DIR 缺省 TEMP）base（694449b8，临时 worktree .wt-base-t14 直跑同脚本，用毕即删）..branch 归一化命中直方图比对（57 SUB/41 R3/10 LIB/5 T1/3 R4/1 T2/1 R5/1 R1＝发射 119 次）完全一致 diff 为空＝零新增命中（ird-gates-comparison.txt）。文档头版本升 v0.16 |
| v0.17 | 2026-09-25 | WP-13-T15 实现落位登记（文档同步；任务契约 tasks/foundation/WP-13-T15.json）：①**插件目标落位**（acceptance 1；卡 §3.2"T02 预留的插件行兑现"）：`sdurws_ird_modeling_plugin` STATIC（Qt Widgets、AUTOMOC 仅该目标显式开启——DTB §5.1 v0.17 行口径；链接面＝sdurws_ird_modeling＋sdurws_ird_ui＋Qt6 Core/Gui/Widgets，单行链接语句——BuildGraphContractTest 行级钉住；源码布局 `modeling/plugin/`——门禁 R-3 产品面扫描域（include/＋src/）之外，与计算库零 Qt 文件域隔离）；②**插件源码五件**（plugin/ 私有面，不入 include/——R-2）：`PanelModel.hpp/.cpp`（零 Qt 五区信息架构纯函数投影：结构树〔节点锚＝ObjectId＋UX-02 守卫经 ui::ensureNoInternalIdentity 唯一出口〕／属性区按类别投影〔关节六字段面、DH 权威轴/原点 ReadOnlyGrey——§7.2；连杆物性三行＋ValueProvenance 徽标直投；工具/场景/传动 §4.4/§4.5/§4.7 投影〕／就绪条〔L0～L11 层结果行＋三组计数＋逐项 subject→树锚跳转〕／预览页〔AppliedRevisionView 类型化强制——D-MDL-10 编辑态即时 XML 预览不提供〕）；`PanelSelection.hpp`（L-1 会话选中态零修订＋反向定位 LocateTarget——ui SelectionModel 未落位面的 P-MDL-8 接缝，落位后退化为薄适配）；`PanelEditFlow.hpp/.cpp`（L-2 编辑流编排——域裁决唯一经 applyJointFieldEdit，接受→增量刷新＋notifySessionDirty／拒绝→就地错误＋原值保留零模态；L-8 数据面——centroidChoicePreview 域 dry-run 预览数值＋submitCentroidEdit 三分支分流）；`PanelCommandCatalog.hpp/.cpp`（ACC4 十条 CommandDescriptor＝卡 §9.7.3 表逐行值化〔id/readOnlyAllowed/ownerUnit="modeling"/scope 映射 Stage/Project→Project·Session→Session/默认零绑定 bindable=true——SA-16〕＋modelingProjectCommandTokens 两套命名空间分离守卫＋PanelRegistrationRecord §10.9 形状承载〔P-MDL-8 暂持——AboutDialog.hpp PluginAssemblyReport 同案先例〕＋modelingReadinessProjection §6.5 汇聚直投＋applyReadOnlyGate L-7）；`ModelingPanelWidget.hpp/.cpp`（五区薄 widget——Q_OBJECT/Q_SIGNALS〔QT_NO_KEYWORDS 集成树口径〕、零计算逻辑、事件驱动刷新、AUTFaces 由呈现模型承载）；`ModelingUiModule.hpp/.cpp`（ui.md §11.2 三方法同形实现——onShellReady/readonlyProjections/buildDraftCommand： moduleId≠"modeling"→nullopt、changes 空→nullopt、否则 project::CommandEnvelope{apply-robot-design＋kCommandPayloadVersion(2)＋RobotDesignCodec 确定性编码根对象槽〔allocateNew＝根身份未回填〕}——§8.5/L-3 域侧组装，P-MDL-8：ui 侧头落位后改继承 ui::IPluginUiModule）；③**门禁登记面**（ird_gates 引擎直跑 base f3b87dfb 归一化比对——traceability/builds/wp13-t15/ird_gates-comparison.md）：恰增 2 处——"modeling->modeling"（插件自边，R-1 码面＝引擎按两端业务域粗分类的同单元自边误射；R-1 语义＝业务域**之间**互链）＋"modeling->ui"（acceptance 1 明文链接面）——与 UI-T15/T16 ui->ui/ui->project 两行同型（DTB §4.5 既有登记模式）；DTB §4.5/whitelist 回填归 WP-01-T03 治理面（本任务 allowedFiles 不含两文件，未私改）；④**构建图契约测试随附同步**（合法登记——T02 落位期断言被 T15 交付兑现）：BuildGraphContractTest 的 _plugin"落位期不得出现"断言翻转为"应已落位"、allowed 集增 sdurws_ird_modeling_plugin/sdurws_ird_ui、ui 边从"全禁"收窄为"行级钉住"（仅插件链接语句行——NoTestkitEdge 同款行扫描口径）；⑤**测试目标增量**（acceptance 2~5 具名自证）：`test/PluginPanelTest.cpp` 23 条（ACC2 六条：树锚/UX-02 哈希拒/关节字段面＋DH 灰显/来源徽标两态/部件投影/就绪条/预览 D-MDL-10；ACC3 五条：L-1 两向零修订＋幂等＋无效锚不伪造、L-2 接受＋拒绝两分支、L-3 三要素经 ui CommandInteractionBridge marshal〔替身呈现器＋主线程泵送——ui CommandInteractionBridgeTest 同型〕、L-8 数据面三分支；ACC4 六条：十条点分 id/readOnlyAllowed 卡表/SA-16 零默认键/命名空间分离/L-7 门控/装配描述符＋就绪投影；ACC5 四条：L-4 保序重演＋失败手工处置不静默丢弃、零缓存纯函数投影、§3.4 UI 线程守卫 fail-fast）；TestMainReport.cpp 增 QCoreApplication（L-3 桥 Marshal 面——ui test main 同款先例，模型测试豁免行 testkit §6.7）；⑥**属性字段面划界登记**（§9.7.1"速度/加速度"与 §4.3-A schema 的边界）：速度/加速度限值不属关节 schema（§15 v0.4 ③c），投影面按对象类别划界——关节六字段，速度/加速度归传动（MDL-16——§4.7），与 Template.hpp JointEditField 注同口径；⑦**冒烟分支增量**：`sdurws_ird_modeling_plugin` 两模式注册（无框架依赖——与 ui 宿主插件 integrated-only 形态区分：本目标是正式装配面非框架动态通道）；Readiness 链经 policy/CollisionQuery.hpp 传播 rw/math/Q.hpp→boost 头——冒烟模式经 vcpkg Boost::headers include 目录注入（header-only 语义，runtime 冒烟 Eigen 同款先例，不入链接清单）；⑧验证留痕（traceability/builds/wp13-t15/）：集成 Release 四目标构建零错误（sdurws_ird_modeling_plugin/test/contract_test/modeling）、`sdurws_ird_modeling_test` 246/246＋`sdurws_ird_modeling_contract_test` 14/14（ird-test-report.json 合并 260/260）、ctest -L "^ird$" 2/2；独立冒烟 Release 全量零错误（build_smoke_t15——vcpkg toolchain×CMAKE_PREFIX_PATH Qt6 前缀，QCoreApplication 级 246/246＋契约 8→14/14）＋ctest 2/2；validate-task/validate-docs 双 PASS；ird_gates 归一化比对恰增 2 处（见③）。文档头版本升 v0.17 |
| v0.18 | 2026-09-25 | WP-13-T16 实现落位登记（文档同步；任务契约 tasks/foundation/WP-13-T16.json）：①**契约测试套件落位**（acceptance 1，§10.2 矩阵行组织、用例名绑定需求/AT）：`test/GoldenDatasetTest.cpp`（MdlGoldenDatasets 7 例——ACC2 四数据集装载全链＋容差档案固定类不放宽＋fieldPath 前置可解析＋coveredRequirements 字典 lint，两模式）；`test/GoldenTemplateTest.cpp`（MdlGoldenTemplate 3 例——ACC3 T-MDL-1 表值逐格锁定＋V-01 草稿面＋V-02 七轴登记不启用，AT-01/AT-20，两模式）；`test/GoldenImportTest.cpp`（MdlGoldenImport 5 例——ACC3 V-06 报告八清单计数＋草稿值面＋ACC4 惯量档案门＋AT-31 黄金宏展开＋V-09 循环透传，AT-15/17/31，两模式）；`test/GoldenDhEquivalenceTest.cpp`（MdlGoldenDhEquivalence 4 例——ACC3 V-11 闭式展开逐关节逐项/roundtrip 参数级/FK 级双一致〔exact 族〕/五状态各含样例，AT-16；`TARGET sdurw_kinematics` gating 同 DhConvertEquivalenceTest——消费 runtime 公共构造器探针）；`contract_test/GoldenPeerContractTest.cpp`（MdlGoldenPeerContract 3 例——ACC5 io 缺失叶契约＋project 对端 V-01 收口＋AT-37 V-12/V-13 输入面；集成模式 gating，同 CommandPipelineContractTest）。计数：集成 `sdurws_ird_modeling_test` 265/265（246＋19）、`sdurws_ird_modeling_contract_test` 17/17（14＋3）；独立冒烟 Release 215/215＋8/8（T08/T09/T12/T13 集成专属 gating 的 50 条冒烟不编入——F-353 同族教训自查：两口径计数均以 gtest stdout/XML/report 三方一致留痕）。②**四数据集＋容差档案落位**（acceptance 2）：`testdata/golden/{mdl-template-6r,mdl-dh-equivalence,mdl-not-expressible,mdl-urdf-import}/1.0.0/`（各含 manifest.json＋inputs/expected/generate 同置）＋`testdata/tolerance/mdl-dh/v1.0.0.json`（附录 D 第 4/5/6/7 项锚定；全部条目 source=appendixD-fixed 且 tolerance==allowedMax——固定类不放宽）；integrity SHA-256＋sizeBytes 全量申报并经 `GoldenDataset::load`/`ToleranceProfile::load` 装载校验通过；**manifest 位于版本目录内**（`<id>/1.0.0/manifest.json`——testkit.md §3.4 根布局与 TK-T03 装载器实现偏差，按实现现状执行，ev-slice-fixture 等 6 个既有数据集同布局，登记本行为 §4.2 的实现侧事实）；`mdl-urdf-import` 二进制资产 `meshes/l1.stl`（最小二面片二进制 STL，134 字节）经数据集根 `.gitattributes` `*.stl -text` 豁免（testdata/.gitattributes 目录注记规则的执行点——text 归一化对二进制流是破坏性的）。③**数据集与实现语义对账**（V-06 期望面以 T05 已验收实现为准修正三处，generate 脚本同步修正并独立重跑 byte-identical）：待确认条目 subject＝定位文本 `joint '<净化后名>'`；忽略清单含 `<disable_collisions>`（顶层扫描外来扩展面＋全树自碰撞候选收集的双登记——ImportTest 包含式断言同口径），计数 2→3；对应 integrity 随重生成刷新。④**V-11 黄金样本行为事实登记**（AT-16；详见 findings F-356）：（a）exact 族求解存在保几何离散等价支 `(θ±π, d, −a, ∓α)`（位置/轴线逐项相等、帧相位差 π），general-3r 原 J3 参数命中该支致 roundtrip/FK 期望失配——经数据集侧调整该样本 J3 输入参数（α 符号位）避开，闭式展开/roundtrip/FK 三面全绿；`canonicalForm` 登记的翻转恒等式 `(θ+π,−d,−a,α+π)` 经独立数值验证不保位置（d≠0 时原点 z 翻转）且未纳入保几何支——产品语义归 T09 所有者裁决（F-356），本任务零产品代码修改；（b）exact-non-unique 族字典序钉定代表保轴线/原点几何（位置面 ≤第 5 项上界），自由 theta 相位差进入 FK 姿态面——黄金 FK 断言 exact 族全称等价、非唯一族位置面（帧相位语义登记于用例注；切权威场景语义归 T08/T09 命令面）。⑤**存量头面缺陷登记 findings F-355**：DhConvert.hpp 与 XacroExpand.hpp 的 `ExpandOutcome` 同名类型冲突（同命名空间两 struct，任何同时包含两头的 TU 编译失败）——GoldenPeerContractTest 首次撞线，处置为该 TU 不包含 XacroExpand.hpp（V-09/AT-31 黄金面由 GoldenImportTest 承载）；修复＝接口级改名，归 T06/T09 所有者。⑥**验证留痕**：集成 Release 三目标＋独立冒烟（vcpkg toolchain＋Qt CMAKE_PREFIX_PATH，Release 配置——Debug 配置与 vcpkg/Qt release DLL 混载产生跨 CRT 堆噪声，按 T15 冒烟树口径取 Release）双模式零错误；ird_gates 引擎直跑 base..head 归一化比对＝58 处存量命中集完全一致（零新增；附件 `traceability/builds/wp13-t16/ird_gates-comparison.md`，base 侧经临时 worktree @a4c8c416 比对后删除）；validate-task/validate-docs 双 PASS。文档头版本升 v0.18。 |
| v0.20 | 2026-09-26 | **WP-24-T03 首版装配——P-MDL-8 消账＋装配门面落位**（分支 wp24-t03 待验收；owner 指示 2026-09-26"每完成一个插件进行装配，及时验证效果"提前启动——原排期阶段 E；ui.md v1.15 同批；版本号承接 wp13-t15-app v0.19 先推序）：①**继承切换**——ModelingUiModule 自"逐方法同形实现"切换为继承 ui::IPluginUiModule（ui 侧两头 IPluginUiRegistrar/IPluginUiModule 已随首版装配落位；三方法 override，签名/语义零变化——R-MDL-1 增量同步预告的兑现；§14.6 P-MDL-8 处置注同步翻转）。②**装配 API**——模块新增 bindCommandSubmit（提交出口绑定，面板创建前后皆可）／bindTextResolver（titleKey→工程用语，宿主接 ui::resolveText）／seedTemplateSession（generic-6r 真实草稿种子；readiness 不预置＝DataInsufficient 缺省行，不伪造）／createPanel（§10.9 工厂半区——会话提供器/出口/文案一次接线＋attachPanel）；面板新增 setCommandTitleResolver（解析失败回退键名原文，不空洞不虚构）。③**装配门面**——assembly/sdurws/ird/modeling/ModelingPluginAssembly.hpp/.cpp（plugin 目标新增 PUBLIC 包含面 assembly/——宿主装配层（ui 插件 DomainAssembly）唯一消费入口，零建模私有头依赖（R-2）； ModelingPluginAssembly{module+descriptor+bind*+seed} 单点结构，friend 装配点回填 m_impl）。④**真宿主实证**——ui 插件直载后建模 Dock 入宿主 Left 区＋十条命令按钮 UiText 中文工程用语（cmd.modeling.*.title 键族随 ui.md v1.15 登记）＋L-1/L-2 编辑流可交互＋无错误弹窗；截图 traceability/builds/wp24-t03-first/modeling-in-host-smoke-02.png。⑤**收口余量（登记于 ui.md v1.15④）**——域命令入 content 注册表/StageStatusModel 汇聚/中央区 CentralAreaHost 挂位/draft.apply 域侧挂钩/AssertionSuite 真判定注入/会话事件同步——随装配收口任务兑现，插件面零改动预期。 |
