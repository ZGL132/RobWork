# 工业机械臂设计软件 · core 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案） |
| 日期 | 2026-09-09 |
| 状态 | **`Draft-Structured`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收） |
| 文档代号 | UNIT-CORE |
| 单元 | core（平台内核，L2 计算内核层；ARCHITECTURE §2.3/§3.1） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（20 单元详设）→ `units/*.md`（单元任务卡）。本文即 `units/core.md`，按任务卡深度编写（接口签名、数据类型在本文件冻结） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/`（骨架已建：目标 `sdurws_ird_core`〔INTERFACE 占位〕＋别名 `RWS::ird::core`＋公共头保留位；见该目录上级 `CMakeLists.txt`） |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 仅功能范围对照——且**当前磁盘上 `old/` 不存在**（见 §1.2 登记项 R-5） |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 core 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 core 的职责（§3.1：对象身份/版本包络、值来源（ValueProvenance）、位姿语义、评估语义、单位换算唯一实现；§3.5：跨单元共享纯契约归 core），把其数据类型、公共接口、不变量、异常处理、测试设计与阶段 A 实现任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不收窄、不扩大；本文引用的需求 ID 与章节号均指 v1.16 当前正文。架构归属以 `ARCHITECTURE.md` v0.11 为准；本文对其两处含混表述的登记见 §10.2（P-AR-1、P-AR-2），不私自裁决。

### 1.2 上游与磁盘现状登记（2026-09-09 实测）

| 项 | 状态 | 说明 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源；本文引用其当前正文措辞 |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源；若评审产生 A9+ 处置，本文按影响面同步 |
| `DETAILED-DESIGN.md` | 已建立 | 20 个单元详设总目录，本文是单元详设正文 |
| `development-task-breakdown.md` | 已建立 | WP-A～WP-I 主 WP 计划，本文只维护单元任务 |
| `units/*.md` | 仅本文（新建） | 其余 19 个单元任务卡均未产出；core README（`core/include/sdurws/ird/core/README.md`）引用本文 §9，与本文结构一致 |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 个单元 README＋`patches/`（2 文件），共 23 文件 | 全部 `sdurws_ird_*` 为 INTERFACE 占位；无产品源码。与 REQUIREMENTS 文档头"20 单元骨架，23 文件"一致 |
| `old/` | **不存在于磁盘**（git 亦未跟踪） | REQUIREMENTS v1.10 声明其在仓库根（523 文件可读）；实测缺失，登记为 R-5（§10.1）。不阻塞本文：从头构建口径下 old/ 仅作范围对照，无对照需求时缺失不产生设计输入 |

### 1.3 设计目标

core 是"任何层都可依赖"的 L2 平台内核（ARCHITECTURE §3.5 共享契约位置规则），因此它的设计目标同时是它的约束：

1. **只放跨单元共享的纯契约与纯函数**：身份/版本包络值类型、值来源与缺失值表达、单位换算唯一实现、容差比较工具、评估语义基础词表、诊断数据契约、领域事件接口（⑤端口）。全部为可自由复制的值类型或纯虚接口，无服务实现、无状态机、无 I/O。
2. **零 Qt、零反向依赖**（NFR-MNT-01、ARCHITECTURE §2.3 L2 行）：只依赖 C++ 标准库与 L1 的 RobWork 数学类型（`rw::math`）；不依赖任何其他 industrialrobot 单元。
3. **防误用优先**：不同身份类型互不可隐式转换（ARC-04、附录 D 第 12 项精确等值）；缺失值与非有限数不得静默转为 0（NFR-COR-03）；单位/量纲错误在换算入口暴露。
4. **确定性**（NFR-COR-02 的一部分）：同一字节输入产生同一摘要、同一数值对产生同一比较结论；无隐藏状态、不依赖线程时序。
5. **不成为"所有 DTO 与通用工具的集合"**（NFR-MNT-04）：每个公共类型在 §2.4 表中逐一给出唯一所有者、依据、消费者与"为何不在别处"；没有消费者的能力不预建。

### 1.4 语言、标准库与构建约束（按仓库实测确认）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | 构建缓存 `build/CMakeCache.txt`：Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计，不做平台特定扩展 |
| Qt | Qt 6.11.1（msvc2022_64） | **core 禁止包含任何 Qt 头**（构建红线 R-3，ARCHITECTURE §3.2） |
| Eigen | `EIGEN3_INCLUDE_DIR` 指向仓库根 `vcpkg/installed/x64-windows/include/eigen3`（外部依赖，经 vcpkg） | core 不直接包含 Eigen 头；经 `rw::math` 头传递出现（`Transform3D.hpp` 含 `Eigen/Core`），由 `sdurw_math` 目标的公开依赖传递 |
| RobWork 基线 C++ 标准 | `RobWork/CMakeLists.txt` 设 `CMAKE_CXX_STANDARD 11`；RobWorkStudio 及其 cmake 宏未设全局标准 | industrialrobot 各目标**显式**设 `CXX_STANDARD 17`（决策 D-01，§12）：MSVC 2022 完整支持；与 C++11 编译的 RobWork 库在同一 ABI（同一运行时）下链接为通行做法；首次集成构建验证为 CORE-T01 完成条件。**不使用 C++20 及以上特性**（对基线保持保守） |
| CMake | 骨架 `cmake_minimum_required(3.16)`；集成入口经 patch 0001 守卫（默认 OFF） | 保持骨架目标名 `sdurws_ird_core`／别名 `RWS::ird::core` 与 include 根 `core/include/`（内含 `sdurws/ird/core/`）不变；INTERFACE→STATIC 升级随 CORE-T01 |
| 可用标准库 | C++17：`std::optional`、`std::string_view`、`std::variant`、`std::chrono`、`std::array` 等 | 全部允许；禁止引入 Boost 等第三方运行库 |
| RobWork 数学类型 | `rw::math::{Vector3D, Rotation3D, Transform3D, EAA, Quaternion, Q, VelocityScrew6D, Wrench6D, InertiaMatrix, Jacobian}`（`RobWork/src/rw/math/`，库目标 `sdurw_math`） | core **直接使用、不包装**（NFR-MNT-04；§4.6 论证） |
| 异常 | RobWork 惯例使用异常（`rw::common::Exception`） | core 使用自有异常类型 `CoreError`（§4.10），并在可恢复查询路径提供 `try*` 非抛出变体 |

---

## 2. 需求承接与范围边界

### 2.1 直接承接的需求（core 是实现责任方或数据契约责任方之一）

| 需求 | core 承接的部分 | 不可越界的部分（其他单元所有） |
| --- | --- | --- |
| CON-01（身份/版本包络；分析从完整不可变快照运行） | 身份/版本包络的**值类型**（ObjectId/ContentVersion/修订身份等）及其解析/格式化/相等/哈希 | 快照组装与"分析只消费快照"的执行机制归 evidence；包络的持久化编址归 project |
| ARC-04（稳定对象 ID） | ObjectId 强类型与精确等值（附录 D 第 12 项） | 设备全名生成/反解归 runtime（⑥名称端口） |
| TASK-03（请求/完成事件携带五元组身份） | 五元组**值类型** `TaskIdentity` | 运行登记表（RunRegistry）与接纳规则归 execution（ARCHITECTURE §4.5） |
| TASK-02 / §8.1 表 3（ResultEnvelope 合法组合） | outcome / engineeringStatus 的**枚举词表**与稳定 token | ResultEnvelope 类型本身、构造边界校验器归 evidence（SA-13） |
| ERR-01（诊断字段规范） | 诊断**数据契约**：DiagnosticRecord、比较型字段、不适用显式标记 | 诊断码注册表（StableCodeRegistry）、文案权威、两级日志、脱敏归 diagnostics；显示口径归 ui（UX-03） |
| MDL-06④＋SA-15（可确认诊断放行） | ConfirmableFinding **基类型**（数据形状＋确认状态＋确认凭据） | 注册于 StableCodeRegistry 的实现、确认交互（ui）、放行与修订提交（project） |
| EVI-01 / §8.1 表 1（评估模式） | EvaluationMode 枚举词表（Preview/Quick/Verified）与 token | RequiredEvidenceProfile、汇总判定、正式通过五条件归 evidence |
| KIN-12（显示单位）＋§7.7 | 单位换算**唯一实现**（SA-12：单位换算只有一个权威定义） | 显示单位设置项的持久化与 UI 归 kinematics/ui；扩展单位 inch/grad/turn 的**用户可见启用**归 KIN-12-S1（R2） |
| NFR-COR-02（确定性） | 摘要、比较、换算、token 解析全部为纯函数/纯值 | 等价候选集合与稳定排序的端到端保障归各评估域＋testkit |
| NFR-COR-03（非有限数、非法单位、引用缺失不得静默转 0/通过） | SourcedValue 四态、Quantity 构造拒绝非有限、换算拒绝未知/异量纲 | 引用缺失（对象解析失败）的处置归消费单元（evidence/project），core 只保证不产生伪值 |
| NFR-MNT-01（计算内核零 Qt） | core 目标本体 | 全部 L2/L4 计算库的持续合规由构建红线 R-3 门禁保障 |
| NFR-MNT-03（单一权威定义） | 单位换算权威、本文件全部公共类型的唯一定义点 | 指标 ID（各域单元）、状态词（ui）、诊断文案（diagnostics）**不在 core**（§2.4） |
| 附录 D（P-01 冻结） | 通用比较公式实现（C4：\|a−b\| ≤ ε_rel·\|ref\|＋ε_abs，逐元素）与产品运行校验 ε_abs 按量纲默认值（C7）的**转写** | 各容差项的类别归属（固定/分析配置默认/工程策略默认）与业务消费归各自所有者；P-06/P-04 范围显式排除 |
| MDL-05/16、DYN-06（来源标记） | ValueProvenance 值类型与五类来源 | 可信等级、降级判定归 dynamics/evidence；估算公式表归 modeling |
| PM-03（任务清单 9 态短标签） | 九态**共享词表** `TaskState`（ARCHITECTURE §4.3 状态机） | 状态机转移、能力声明、调度归 execution；短标签文案归 ui |
| UX-03（比较型三要素：实际值/要求值/单位） | ComparativeFields 数据形状 | 渲染与措辞归 ui/diagnostics |

### 2.2 明确不做（core 的非目标）

以下能力在需求或架构中有明确的其他所有者，core **不实现、不预建桩**：

1. CanonicalModel、确定性编译器、RuntimeNameMap、RobWork 运行时适配（runtime，SA-04/SA-05）；
2. EngineeringPolicySet、CollisionEvaluator、任何工程判定阈值（policy，SA-06）——包括行程上限 4π（附录 D 第 11 项，工程策略默认）；
3. AnalysisSnapshot、InputSlice、ResultEnvelope、RequiredEvidenceProfile、ResultCurrentness、证据门禁与汇总判定（evidence，SA-07/SA-13）；
4. 任务状态机、调度、运行登记表（RunRegistry）、检查点、缓存治理（execution）；
5. 事件总线/分发的**实现**、事件持久化、订阅生命周期管理设施（execution·ui 分发，L5 装配注入；§4.9）；
6. 诊断码注册表、文案、两级日志、脱敏（diagnostics）；
7. 确认对话交互（ui）、放行与修订提交（project）；
8. 任何文件/JSON/XML/CSV 编码解码与项目磁盘写入（io/project；§7 序列化边界）；
9. 指标 ID 注册表、UX-10 七态状态词（各域单元/ui）；
10. 黄金数据集、容差档案、稳定集合断言设施（testkit；core 测试可消费但不归属 core）；
11. 通用容器、字符串、数学库等"顺手工具"（NFR-MNT-04；无边界价值不进 core）。

### 2.3 core 拥有／消费／不拥有对照表

| 单元 | core 为其提供（core→该单元） | core 从其消费（该单元→core） | 易误认为 core 的、但明确归该单元的内容 |
| --- | --- | --- | --- |
| project | 全部身份值类型、ContentVersion/ContentIdentity、TaskIdentity、ValueProvenance/SourcedValue、ConfirmableFinding、DiagnosticRecord、IDomainEventBus/DomainEvent、单位换算（无直接调用点，见注） | **无**（project→core 有向边，反向禁止） | ProjectRevision 聚合根、命令服务、事务协议、.rwdesign 编址、身份**分配策略**、ProjectMetadata |
| evidence | 枚举词表（EvaluationMode/TaskOutcome/EngineeringStatus）、ContentIdentity/ContentDigester、Tolerance/比较工具、ObjectId | 无 | AnalysisSnapshot/InputSlice/ResultEnvelope/ResultCurrentness、评估器接口与注册表、证据门禁 |
| runtime | ObjectId、位姿/单位约定（§4.6，文档契约＋约定锁定测试）、ContentIdentity | 无（L1 rw::math 属框架基线） | CanonicalModel、R_world_base、编译链、名称解析器 |
| policy | ContentIdentity/ContentDigester、Tolerance 值类型 | 无 | EngineeringPolicySet、阈值数值、CollisionEvaluator |
| execution | TaskIdentity、TaskState 词表、TaskOutcome、DomainEvent 家族＋IDomainEventBus 接口 | 无 | 九态状态机、RunRegistry、调度、检查点、事件总线实现 |
| diagnostics | DiagnosticRecord/ComparativeFields/ConfirmableFinding **基类型**、DiagCode 承载 | 无 | StableCodeRegistry、码值分配、文案、日志、脱敏、诊断目录 |
| io | tryParse/tryConvert 非抛出接口、UnitToken 校验、SourcedValue（保留原始非法输入） | 无 | SafePath/BudgetGuard（归属待裁决 P-AR-1，本文建议归 io）、CSV 方言/转义 |
| ui | DomainEvent/IDomainEventSink、TaskState 词表（9 态短标签投影的语义源）、单位换算（显示投影） | 无 | 七态状态词、命令注册表、快捷键表、确认对话 |
| testkit | 全部公共值类型（稳定集合断言的对象）、Tolerance 值类型（容差档案） | 无（core 产品目标不链 testkit；仅测试目标可用） | 黄金数据集、容差档案内容、契约测试基座 |
| 业务域单元（modeling…workflow） | §2.1 所列全部公共契约（经各自依赖边） | 无 | 领域对象、评估器、领域算法、工艺模板等全部业务语义 |
| （框架）RobWork rw::math | — | **唯一被消费方**：Vector3D/Rotation3D/Transform3D/Q 等数学类型（L2 允许，ARCHITECTURE §2.3） | — |

注：project 对 core 的消费主要是类型；单位换算的直接调用者是 io（目录单位校验）、各业务域（显示投影）与 testkit。core 对任何 industrialrobot 单元**零依赖**（构建红线 R-1/R-2 的基础）。

### 2.4 公共类型逐项归属论证

| 类型/接口 | 唯一所有者 | 需求/架构依据 | 实际消费者（阶段 A 起） | 放在 core 的理由 | 不放在其他单元的理由 |
| --- | --- | --- | --- | --- | --- |
| ObjectId、ProjectId、BranchId、RevisionId、RunId、AttemptId、TaskIdentity | core | ARC-04、CON-01、TASK-03；ARCH §3.1、§4.5 | project、evidence、execution、runtime、diagnostics、io、reporting、全部业务域 | 任何层都可依赖 core（ARCH §3.5），身份被全部单元消费 | 放 project 则 runtime/evidence（L2）不能依赖 L3；放任一业务单元造成反向依赖 |
| ContentVersion（内容版本） | core | CON-01、ARCH §6.2 | project（编址/写）、全部持对象引用者 | 与 ObjectId 构成"身份/版本包络"的值层 | 计算规则（对什么字节做摘要）属 project/io；值类型须全局唯一 |
| ContentIdentity（内容身份）＋ContentDigester | core | CON-05/CON-06、SA-07；ARCH §3.1 | evidence（切片）、policy（策略内容身份）、runtime（RuntimeNameMap 内容身份）、project（持久化引用）、execution（缓存命中比较） | 跨 L2 三内核＋L3 两服务的共享值；摘要算法必须唯一，否则跨进程身份不一致（NFR-COR-02） | 各自实现即"重复算法"（ARC-05 精神、NFR-MNT-03/07） |
| EventId、DomainEvent 家族、IDomainEventSink/IDomainEventBus | core（接口） | ARCH §7.2 ⑤端口行："core（接口）/execution·ui（分发）"；TASK-03、UX-10/12 | execution（发布计算侧事件）、project（修订/失效事件）、ui/workflow（订阅）、L5（装配总线实例） | 端口接口跨 L3 同层多单元共享；发布方 project/execution 与订阅方 ui/workflow 无公共下层除 core 外 | 总线实现归 execution·ui（若接口也放彼处，project 无法零 Qt 依赖地发布事件） |
| ProvenanceKind、ValueProvenance、SourcedValue\<T\> | core | ARCH §6.2（五类来源）、MDL-05/16、DYN-06、NFR-COR-03 | modeling（物性）、requirements（字段）、selection（目录回填）、dynamics（可信等级依据）、reporting（限定语数据源） | 缺失/不适用/非法的四态区分是全产品数据纪律（NFR-COR-03），须单点定义 | 放任一业务单元则其他域复制语义；diagnostics 只需要其中"不适用标记"的显示口径 |
| QuantityKind、UnitToken、convert/tryConvert、Quantity\<K\> | core | KIN-12、SA-12、DYN-03（类型化广义力）、SEL-02（单位校验）、NFR-COR-03 | kinematics（显示单位）、io/selection（目录单位）、trajectory/dynamics（类型化量）、ui/reporting（显示投影） | "单位换算只有一个权威定义"（NFR-MNT-03）且被 L2/L3/L4 全层消费 | 放 kinematics 则 dynamics/selection 反向依赖业务单元 |
| Tolerance、closeWithin、allCloseWithin、runtimeAbsoluteTolerance | core | 附录 D C4/C7、NFR-COR-01/02、AT-03/06 | evidence/trajectory/dynamics（校验）、testkit（对照断言）、policy（阈值承载的值形状） | 通用比较公式逐域重复实现会破坏 C4 逐元素/零参考语义一致性 | 阈值**数值**与类别归属归各所有者（附录 D 类别说明）；core 只提供机制与 C7 转写 |
| EvaluationMode、TaskOutcome、EngineeringStatus | core | §8.1 表 1/表 3、EVI-01、TASK-02、ERR-01（三轴正交）；ARCH §3.1"评估语义" | evidence（包络）、execution（事件/任务）、reporting（RPT-05 措辞规则）、workflow/ui | 三轴正交词表被 diagnostics（→仅依赖 core）与 evidence 双方消费，词表必须位于两者公共依赖集＝core | 放 evidence 则 diagnostics 无法引用 EngineeringStatus 表达正交性（diagnostics→core 单边依赖） |
| TaskState（九态词表） | core | ARCH §4.3、PM-03、UX-10 | execution（状态机）、ui（9 态短标签）、workflow | 同上：execution 与 ui/workflow 的公共下层是 core | 状态机本身归 execution（词表≠机器） |
| DiagCode 承载、DiagnosticRecord、ComparativeFields | core | ERR-01、UX-03；ARCH §3.5、§7.8（"数据契约定义于 core"） | diagnostics（注册/文案）、全部诊断产生单元（modeling/execution/io/各域）、reporting | 架构明文：契约/实现分离 | 码表/文案/日志归 diagnostics；显示归 ui |
| ConfirmableFinding、ConfirmationCredential | core | SA-15、MDL-06④（M-10） | project（构造/放行/摘要留痕）、diagnostics（注册为比较型子类）、ui（确认对话数据） | 架构明文："ConfirmableFinding 基类型定义于 core"（§7.8） | 确认交互归 ui；放行归 project |
| CoreError | core | 本文（错误表达统一） | core 内部＋直接调用 core 的单元 | 异常类型随接口走 | 非 core 特有错误不得复用此类型 |
| （对照）SafePath/BudgetGuard | **io（建议）** | ARCH §7.9 | io 各导入通道 | — | 见 P-AR-1：§3.5 依赖表"用途"列措辞与 §7.9 归属存在含混，本文建议类型与实现均归 io，core 不预建 |
| （对照）指标 ID 注册表 | 各业务域单元 | NFR-MNT-03 | kinematics/optimization/reporting | — | "同一指标只有一个权威定义"由指标所属域满足；core 建全局注册表即"所有 DTO 的集合"之始（NFR-MNT-04） |
| （对照）UX-10 七态状态词 | ui | UX-10 | ui/workflow | — | 界面投影语义，非内核词表 |

---

## 3. 单元组成、依赖和公开头文件布局

### 3.1 组成

core 由 10 个公共头模块＋1 个编译实现组成（实现文件随任务落地，头为契约权威）：

| 头（`core/include/sdurws/ird/core/`） | 内容 | 详见 |
| --- | --- | --- |
| `Identity.hpp` | ObjectId/ProjectId/BranchId/RevisionId/RunId/AttemptId/TaskIdentity/EventId、Id128 表示、generate/parse/format | §4.1、§5.1 |
| `Digest.hpp` | Digest256、ContentVersion、ContentIdentity、ContentDigester | §4.2、§5.2 |
| `Provenance.hpp` | ProvenanceKind、ValueProvenance、SourcedValue\<T\> | §4.3、§5.3 |
| `Units.hpp` | QuantityKind、UnitToken、单位表、convert/tryConvert、Quantity\<K\> 及别名 | §4.4、§5.4 |
| `Compare.hpp` | Tolerance、closeWithin、allCloseWithin、runtimeAbsoluteTolerance | §4.5、§5.5 |
| `Evaluation.hpp` | EvaluationMode、TaskOutcome、EngineeringStatus、TaskState 及 token 往返 | §4.7、§5.6 |
| `DiagData.hpp` | DiagCode、DiagnosticRecord、ComparativeValue/Fields、ConfirmationState/Credential、ConfirmableFinding | §4.8、§5.7 |
| `Events.hpp` | DomainEventKind、四类事件载荷、DomainEvent、IDomainEventSink/IDomainEventBus/IEventSubscription | §4.9、§5.8 |
| `Errors.hpp` | CoreError | §4.10 |
| `README.md` | 既有保留位说明（不参与编译） | — |
| `src/`（实现，随 CORE-T01 建） | 非模板实现：解析/格式化/摘要/单位表 | §6 |

位姿与坐标系语义（§4.6）**无头文件**：core 不引入新类型，只冻结使用 `rw::math` 的约定，由 core 测试目标中的"约定锁定测试"（§8 UT-CONV）钉住。

### 3.2 依赖

```
core ──► C++17 标准库
     ──► sdurw_math（L1：rw::math；仅 Compare.hpp 的 Q 重载与文档化约定需要）
     ──► （无其他：零 Qt〔R-3〕、零 industrialrobot 单元、零 Eigen 直接包含）
```

### 3.3 命名空间、目标与 CMake 集成

- 命名空间：`sdurws::ird::core`（嵌套 namespace，C++17 写法）。
- 目标：`sdurws_ird_core`（骨架 INTERFACE → CORE-T01 升级 STATIC），别名 `RWS::ird::core`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_core ... sdurw_math)`。
- 测试目标：`sdurws_ird_core_test`（不预建于骨架，随 CORE-T01 登记；gtest 接入方式见 §10.2 P-ENV-2）。
- 头包含形式：`#include <sdurws/ird/core/Identity.hpp>`；私有实现头不得置于 `include/`（R-2 红线）。

---

## 4. 数据类型与不变量

约定（适用于本节全部类型，下文不再重复）：

- **值语义**：全部数据类型可拷贝/移动、无共享可变状态、无析构副作用；并发只读安全，跨线程传递无需同步（线程安全列不再逐条重复）。
- **默认构造**＝"空/未设置"态，`isValid()==false`；全零比特为保留值，任何生成器不得产出（§4.1 U-1）。
- **相等**：除非特别说明，逐字段精确相等（附录 D 第 12 项：名称与对象身份比较无容差）。
- **排序**：提供 `operator<`（字典序/数值序）仅为容器与确定性去重排序服务，**无业务含义**；业务稳定排序使用各域自有编号（如 KIN-02 稳定编号）。
- **可变性**：全部为不可变使用约定（构造后不修改；不提供 setter）；`SourcedValue`/`ConfirmableFinding` 等聚合体同样按值替换整体。

### 4.1 身份与版本基础类型（`Identity.hpp`）

#### U-1 底层表示 Id128

| 项 | 规定 |
| --- | --- |
| 表示 | `std::array<std::uint8_t, 16>`（128 位，不解释内部结构） |
| 保留值 | 全零＝"空/未设置"，`isValid()` 恒 false；`generate()` 检验非零（零则重取） |
| 规范文本 | `<tag>-<32 个小写十六进制>`，如 `obj-0f3a…`；tag 见下表 |
| 解析 | 严格：tag 正确、长度 32、仅 `[0-9a-f]`（大写拒绝）；多余前后缀/空白拒绝 |
| 往返 | `parse(format(x)) == x`（对合法实例） |
| 相等/哈希 | 字节精确相等；`std::hash` 特化（FNV-1a 128 over bytes） |
| 分配 | `generate()`＝`std::random_device` 播种的 `mt19937_64`（thread_local 引擎）取两字；**分配策略（谁在何时生成）归各所有者单元**（对象创建→project；运行派发→execution） |

各身份类型（均为 Id128 的**独立强类型**包装，互无可隐式转换，仅显式 `fromCanonical`/`toCanonical`）：

| 类型 | tag | 含义 | 分配者（值生命周期） | core 之外的语义边界 |
| --- | --- | --- | --- | --- |
| `ObjectId` | `obj-` | 持久化对象稳定身份，跨修订不变（ARC-04、§6.2） | project（对象创建时一次） | 对象编码/编址归 project/io；名称映射归 runtime |
| `ProjectId` | `prj-` | 项目身份（project.json 创建期一次写入，§6.1；另存为换新 id，PM-05） | project | — |
| `BranchId` | `brn-` | 方案分支身份（PM-12：分支记录 baseRevisionId） | project | 分支表/ProjectMetadata 归 project |
| `RevisionId` | `rev-` | 修订身份（一次命令提交＝一个修订，ARC-01） | project | **单调序号不在 id 内**：修订顺序、父修订、命令摘要归 project 的修订记录（D-04） |
| `RunId` | `run-` | 一次评估运行身份 | execution（派发时） | RunRegistry 登记规则归 execution（§4.5 ARCH） |
| `EventId` | `evt-` | 领域事件身份（投递去重/日志关联） | 发布方（project/execution） | — |

| 类型 | 表示 | 含义 | 约束 |
| --- | --- | --- | --- |
| `AttemptId` | `std::uint64_t` | 同一 Run 的尝试序号 | ≥1；0＝空/保留；规范文本 `att-<十进制>`；分配（重试递增、旧 attempt 被取代标记）归 execution |
| `TaskIdentity` | 五字段聚合：`ProjectId project; BranchId branch; RevisionId revision; RunId run; AttemptId attempt;` | 请求与完成事件携带的身份五元组（TASK-03、ARCH §4.5） | `isValid()`＝五字段全合法；相等＝五字段全等；哈希＝字段组合；**接纳判定规则（查表、失配拒绝、幂等）归 execution** |

合法/非法实例示例：

- 合法：`rev-9f2c7b…（32 hex）`；`att-3`；五元组全合法的 `TaskIdentity`。
- 非法（`fromCanonical` 抛 `CoreError`，`tryFromCanonical` 返回空）：`obj-9F2C…`（大写）、`rev-<31 hex>`（长度）、`att-0`（保留值）、`obj-`＋31 位后跟 `g`（非法字符）、把 `rev-…` 文本解析为 `ObjectId`（tag 不符——类型误用在解析边界即失败）。

#### 4.1.1 四个"身份"概念的区别（CON-01 语义澄清）

| 概念 | 类型 | 回答的问题 | 稳定性 | 举例 |
| --- | --- | --- | --- | --- |
| 对象身份 | `ObjectId` | "这是哪一个（逻辑）对象" | 跨内容修改、跨修订稳定 | 某连杆的质量参数对象 |
| 内容版本 | `ContentVersion`（§4.2） | "这是该对象内容的哪一版不可变字节" | 内容变则变（摘要） | 同一连杆参数改密度 → 新 ContentVersion，ObjectId 不变 |
| 修订身份 | `RevisionId` | "这是哪一次命令提交产生的项目状态" | 只增不改（PA-2） | 一次"应用修改"＝一个新修订 |
| 内容身份 | `ContentIdentity`（§4.2） | "这组（跨对象）内容整体的字节摘要是什么" | 由被摘要集合决定 | 一个 InputSlice 的内容身份＝缓存键与失效判据（CON-05） |

前两者构成 CON-01"身份/版本包络"的对象侧；第三者是修订侧；第四者是依赖/缓存侧。四者互不替代：对象身份不承载内容信息（同内容不同对象可共存），内容身份不标识单一对象。

### 4.2 摘要类型（`Digest.hpp`）

| 类型 | 表示 | 含义 | 规范文本 |
| --- | --- | --- | --- |
| `Digest256` | `std::array<std::uint8_t, 32>` | 256 位摘要值 | 64 个小写 hex（无 tag 前缀）；全零保留＝空 |
| `ContentVersion` | `Digest256` | 单个对象内容的摘要版本戳 | `cv-<64hex>`（持久化时带 tag） |
| `ContentIdentity` | `Digest256` | 一组内容的复合摘要（切片/策略/名称映射） | `cid-<64hex>` |

`ContentDigester`：增量式字节流摘要器（算法：SHA-256，决策 D-05）。

| 成员 | 契约 |
| --- | --- |
| `void update(const void* data, std::size_t n)` | 追加字节；本对象非线程安全（每线程各持实例） |
| `Digest256 finalize()` | 结束并取摘要；finalize 后实例不可再用（再调用抛 `CoreError`） |
| 纯函数性 | 相同字节序列 → 相同摘要，与平台/编译器/线程无关（NFR-COR-02） |
| 测试向量 | FIPS 180-2 已知向量（空串、`"abc"`）钉住（§8 UT-ID-D） |

**责任边界**：core 只提供"字节 → 摘要"。对**什么**做摘要（canonical 序列化）归各所有者：切片内容序列化归 evidence；策略序列化归 policy；RuntimeNameMap 序列化归 runtime；对象 canonical 编码归 project/io。跨单元内容身份可比的前提是被比较方使用相同的 canonical 序列化——该序列化约定登记于各所有者任务卡，并作为交接项（§10.3）。

### 4.3 值来源与缺失值（`Provenance.hpp`）

#### ProvenanceKind（枚举，token 冻结）

| 值 | token | 语义（ARCH §6.2 五类） | 典型产生者 |
| --- | --- | --- | --- |
| `UserProvided` | `user-provided` | 用户直接输入 | 各编辑域 |
| `GeometricEstimate` | `geometric-estimate` | 几何解析估算（MDL-05 唯一公式表） | modeling |
| `CatalogBackfill` | `catalog-backfill` | 器件目录回填（SEL-10） | selection |
| `ImportMapped` | `import-mapped` | 导入通道字段映射（URDF/CSV/WorkCell） | modeling/requirements/io |
| `DerivedReadOnly` | `derived-readonly` | 派生只读（权威源互斥下的另一侧表示，MDL-02/09） | modeling/runtime |

#### ValueProvenance

| 字段 | 类型 | 必填 | 默认 | 约束 |
| --- | --- | --- | --- | --- |
| `kind` | `ProvenanceKind` | 是 | 无（必须显式） | — |
| `sourceObject` | `std::optional<ObjectId>` | 否 | `nullopt` | 值来源关联对象（如目录条目对象、导入记录对象）；提供时须 `isValid()` |
| `sourceVersion` | `std::optional<ContentVersion>` | 否 | `nullopt` | `sourceObject` 的内容版本（目录版本锁定 SEL-08 的引用基础）；仅在 `sourceObject` 存在时允许存在 |
| `methodTag` | `std::optional<std::string>` | 否 | `nullopt` | 产生方法稳定短标记（如 `MDL-05/hollow-cylinder`）；字符集 `[a-z0-9./_-]`、非空、≤64 字符；**词表归产生单元**，core 只做语法校验 |

相等：四字段全等。不变量 P-1：`sourceVersion.has_value() ⇒ sourceObject.has_value()`（工厂校验，违反抛 `CoreError`）。

**语义边界（DYN-06）**：来源标记是事实记录，**不直接等同于工程可信等级，也不构成正式通过结论**。可信等级/降级判定由 dynamics/evidence 依据（含但不止于）来源标记作出。

#### SourcedValue\<T\>（四态字段值）

| 状态 | token | 含义 | `value()` | `invalidRawInput()` | `provenance()` |
| --- | --- | --- | --- | --- | --- |
| `Provided` | `provided` | 有值 | ✔ 返回 T | — | ✔ 有效 |
| `NotProvided` | `not-provided` | 未提供（如物性缺失，MDL-06：不触发断言、走 DataInsufficient 降级） | 抛 `CoreError` | — | 无意义（返回默认，不得使用） |
| `NotApplicable` | `not-applicable` | 不适用（ERR-01：显式标记，不伪造数值） | 抛 `CoreError` | — | 同上 |
| `Invalid` | `invalid` | 已提供但非法（保留原始输入；NFR-COR-03：不得静默转 0） | 抛 `CoreError` | ✔ 返回原始输入串 | 同上 |

| 成员 | 签名 | 契约 |
| --- | --- | --- |
| 默认构造 | `SourcedValue()` | `NotProvided`（**无默认值语义**——T 内部占位初始化为 `T{}` 但契约上不可观测，杜绝"缺失＝零"） |
| 工厂 | `provided(T v, ValueProvenance p)`／`notProvided()`／`notApplicable()`／`invalid(std::string raw)` | `raw` 非空（空串拒绝） |
| 读取 | `state()`／`tryValue() -> std::optional<T>`（非抛出）／`value()`（前置 `Provided`，否则抛 `CoreError`） | |
| T 约束 | 需可默认构造＋可拷贝 | 数值场景 T＝`double`/`Quantity<K>`（后者构造已拒非有限） |

合法/非法实例：`SourcedValue<double>::provided(12.5, {GeometricEstimate,…})` 合法；`invalid("1e999")`（解析溢出为非有限，由导入层以原串登记）合法保留原串；对 `NotProvided` 实例调 `value()` 非法（抛错，不是返回 0）。

### 4.4 数量、单位与数值基础契约（`Units.hpp`）

#### QuantityKind（枚举，token 冻结）

`length`、`angle`、`mass`、`time`、`force`、`torque`、`inertia`、`power`、`linear-velocity`、`angular-velocity`、`linear-acceleration`、`angular-acceleration`、`voltage`、`dimensionless`。

覆盖需求实际出现的量纲：位置/长度（m）、角度（rad）、速度与加速度（线/角分列——DYN-03 类型化广义力、TRJ-05 限制校验）、力（N）、力矩（N·m）、质量（kg）、惯量（kg·m²）、时间（s）、功率（W，DYN-03 机械功率）、电压（V，SEL-03 电压筛选）。**不设**温度/电流/货币量纲（R1/R2 无消费者；货币呈现归 selection，非物理单位）。

#### UnitToken 与单位表（R1 冻结；R2 扩展单列）

| QuantityKind | R1 token（SI 因子：value_si = value × factor） | R2 扩展（KIN-12-S1，随 R2 启用登记） |
| --- | --- | --- |
| length | `m`(1)、`cm`(0.01)、`mm`(0.001) | `inch`(0.0254) |
| angle | `rad`(1)、`deg`(π/180) | `grad`(π/200)、`turn`(2π) |
| mass | `kg`(1) | — |
| time | `s`(1) | — |
| force | `N`(1) | — |
| torque | `N*m`(1) | — |
| inertia | `kg*m^2`(1) | — |
| power | `W`(1) | — |
| linear-velocity | `m/s`(1) | — |
| angular-velocity | `rad/s`(1) | — |
| linear-acceleration | `m/s^2`(1) | — |
| angular-acceleration | `rad/s^2`(1) | — |
| voltage | `V`(1) | — |
| dimensionless | `1`(1) | — |

规则：token 全 ASCII、区分大小写、冻结后不改名（持久化契约，§7.2）；新增 token（如阶段 C 可能的 `rpm`、`min`）为**向后兼容追加**，须在对应消费者任务卡确认后登记（不预建无消费者条目）。

#### 换算接口（唯一入口，SA-12）

```cpp
// Units.hpp（节选；完整契约见 §5.4）
std::optional<UnitToken>  findUnit(std::string_view symbol) noexcept;  // 已知表查询
QuantityKind              kindOf(UnitToken u) noexcept;                // 前置：已注册
double                    siFactor(UnitToken u) noexcept;              // 前置：已注册

double                    convert(double v, UnitToken from, UnitToken to);        // 抛 CoreError
std::optional<double>     tryConvert(double v, UnitToken from, UnitToken to) noexcept;
```

错误口径（NFR-COR-03）：

| 情形 | `convert` | `tryConvert` |
| --- | --- | --- |
| `from`/`to` 未注册（"未知单位"——仅来自未走 `findUnit` 的调用方违约） | 抛 `CoreError` | 返回 `nullopt` |
| `kindOf(from) != kindOf(to)`（量纲不匹配，如 N·m→N） | 抛 `CoreError` | 返回 `nullopt` |
| `v` 非有限（NaN/±Inf） | 抛 `CoreError` | 返回 `nullopt` |
| 结果溢出为非有限（如 `DBL_MAX cm→m`） | 抛 `CoreError` | 返回 `nullopt` |

换算数值行为：`to_si = v * siFactor(from)`，`out = to_si / siFactor(to)`——各一次乘除，不承诺位往返精确（0.01 无精确二进制表示）；测试按附录 D 容差断言往返一致（§8 UT-UNIT）。

#### Quantity\<K\>（强类型量，SI 真值）

```cpp
template <QuantityKind K> struct Quantity {
    static Quantity                 fromSi(double v);                       // 非有限抛 CoreError
    static std::optional<Quantity>  tryFromSi(double v) noexcept;
    static std::optional<Quantity>  tryParse(double v, UnitToken u);        // kind 匹配且有限才成功
    double            siValue() const noexcept;                             // SI 真值
    double            displayValueIn(UnitToken u) const;                    // kind 匹配（唯一换算入口）
    bool              operator==(Quantity o) const noexcept;                // 逐值精确相等（比较请用 Compare）
};
using Length = Quantity<QuantityKind::Length>;   // …Angle/Mass/Time/Force/Torque/Inertia/
                                                 // Power/LinearVelocity/AngularVelocity/
                                                 // LinearAcceleration/AngularAcceleration/
                                                 // Voltage/Scalar
```

- 单位不匹配在**类型层**消除（`Torque` 与 `Force` 不可互赋——DYN-03 类型化广义力）；显示单位只是投影：`displayValueIn` 不改变 SI 真值（KIN-12：切换显示单位不触发重算、不产生修订）。
- `rw::math` 聚合类型（`Vector3D`/`Q` 等）**不包装**：其数值一律为 SI 真值（约定 §4.6）；显示层换算由调用方对元素施加上述唯一入口（`Compare.hpp` 的 `allCloseWithin` 消费 `Q`；不另设向量换算函数——无第二个换算实现点）。

### 4.5 容差与比较（`Compare.hpp`）

| 类型/函数 | 定义 | 契约 |
| --- | --- | --- |
| `Tolerance` | `struct { double relative; double absolute; }` | 两分量均须 ≥0 且有限（工厂校验）；**数值来源归各所有者**（附录 D 类别：固定/分析配置默认/工程策略默认） |
| `closeWithin(value, reference, t)` | `|value−reference| ≤ t.relative·|reference| + t.absolute`（附录 D C4 通用比较公式） | 零参考退化为 `absolute`（不要求精确零）；任一输入非有限 → **false**（不抛、不静默通过，NFR-COR-03）；`noexcept` |
| `allCloseWithin(a, b, t)`（`rw::math::Q` 与 `std::vector<double>` 重载） | 逐元素应用上式，全部满足才 true | **逐元素**（C4：不以差值总和替代，防正负抵消）；长度不等抛 `CoreError`（契约违约） |
| `runtimeAbsoluteTolerance(QuantityKind) -> std::optional<double>` | 附录 D C7 产品运行校验 ε_abs 默认值**转写** | angle→1×10⁻¹²；length→1×10⁻¹²；linear-velocity/angular-velocity→1×10⁻⁹；linear/angular-acceleration→1×10⁻⁹；torque→1×10⁻⁹；dimensionless→1×10⁻¹²；**其余量纲返回 `nullopt`**（附录 D 未声明默认——按 C7"无默认且无配置来源的量不得引入产品侧相对校验"执行） |

core **不持有**任何业务阈值：第 1/2/3 项（IK 容差，分析配置默认）、第 4/5 项（编译链/DH，固定）等数值由 kinematics/runtime/modeling 各自落地；`runtimeAbsoluteTolerance` 是附录 D C7 的机械转写（修改只能随需求变更，D-06）。

### 4.6 位姿与坐标系基础语义（无新类型；约定＋测试锁定）

| 约定 | 内容 | 依据 |
| --- | --- | --- |
| 位姿方向 | `rw::math::Transform3D<double>` 记 `T_ab`＝"b 系相对 a 系的位姿"：b 系中点 `p_b` 映射为 `p_a = T_ab * p_b`（R 为 b→a 旋转，d 为 b 原点在 a 系中的位置） | RobWork `Transform3D` 语义；core 冻结此读法，UT-CONV-1 以已知算例锁定 |
| 组合顺序 | `T_ac = T_ab * T_bc`（左乘链式）；逆 `T_ba = inverse(T_ab)` | 同上，UT-CONV-2 锁定 |
| 单位 | 一切计算用长度 m、角度 rad（SI 真值）；deg 等仅存在于显示/导入边界并立即经唯一换算入口转 SI | KIN-12、NFR-COR-03 |
| 点 vs 方向 | `Vector3D<double>` 兼表点与自由向量：作为**方向**使用时约定为单位化或自由向量、不参与平移变换（`T.R * dir`）；作为**点**参与完整变换。区分由使用契约承担，**不建包装类型**（无边界价值，NFR-MNT-04） | 本文 D-02 |
| 旋转表示 | 计算权威为 `Rotation3D`（正交性由产生者保证，core 不复检）；`EAA`/`Quaternion` 用于界面/序列化表示，互转用 `rw::math` 自带函数 | MDL-02/09 语义在 modeling/runtime |
| 参考系标识 | 产品级帧引用一律 `ObjectId`（ARC-04）；RobWork 设备作用域全名仅存在于编译产物并经⑥名称端口（runtime）生成/反解；core 不提供 FrameRef 包装 | ARC-04、NFR-MNT-07 |
| 刚体变换 vs 旋转 vs 螺旋 | 语义按 `rw::math` 既有类型分工（`Transform3D`/`Rotation3D`/`VelocityScrew6D`/`Wrench6D`），core 不重复定义 | NFR-MNT-04 |
| 基座—世界变换 | 语义＝"基座系相对世界系的旋转，编译进 CanonicalModel 的单一 `R_world_base`"（MDL-22、ARCH §7.3 M-11）。**core 不拥有、不计算、不存储**该变换；本文仅在 notation 上承认 `T_world_base` 的读法，四消费方一致性测试归 runtime/policy 的契约测试（AT-37） | 上游语义承接，不重复实现 |

### 4.7 基础评估语义词表（`Evaluation.hpp`）

四个枚举＋token 往返（`toToken`/`xxxFromToken`，token 冻结、持久化契约）。**语义以 REQUIREMENTS §8.1/§4.3（ARCH 转述）为准，本文不复述判定规则，只提供词表与承载**：

| 枚举 | 值（token） | 语义锚点 |
| --- | --- | --- |
| `EvaluationMode` | `Preview`(`preview`)／`Quick`(`quick`)／`Verified`(`verified`) | §8.1 表 1（证据效力分层；Quick 不得单独支撑正式通过、Preview 不产生正式证据与结果对象） |
| `TaskOutcome` | `Completed`(`completed`)／`Canceled`(`canceled`)／`Failed`(`failed`)／`Interrupted`(`interrupted`) | §8.1 表 3（payload 约束与合法组合校验器归 evidence） |
| `EngineeringStatus` | `Feasible`／`EngineeringInfeasible`／`DataInsufficient`／`NotApplicable`（`feasible`/`engineering-infeasible`/`data-insufficient`/`not-applicable`） | §8.1 表 3＋ERR-01（NotApplicable＝取消/失败/中断的显式标记，不伪造判定） |
| `TaskState` | 九态：`Queued`/`Preparing`/`Running`/`Paused`/`Canceling`/`Canceled`/`Completed`/`Failed`/`Interrupted`（token 逐一对应小写连字符形式） | ARCH §4.3 状态机（转移矩阵、能力声明、取消协议归 execution；ui 的 PM-03 九态短标签以本词表为语义源） |

不变量：枚举无序语义（序列化只认 token）；未知 token 解析返回 `nullopt`/抛错；`TaskOutcome` 与 `TaskState` 的终态同名但属两个轴（信封 outcome vs 状态机状态），映射（如 `Running→Failed` 状态与 `outcome=Failed` 的对应）归 execution（§10.3 交接）。

**归属说明（设计解释，需下游交叉核对，见 §10.2 P-D-1）**：四词表置于 core 的依据是 ARCH §3.1 core 行明列"评估语义"＋依赖拓扑：ERR-01 要求"执行状态、工程判定、诊断类别正交"，diagnostics（仅依赖 core）须能引用 EngineeringStatus 表达该正交；execution 的完成事件（core 事件接口）须携带 TaskOutcome/TaskState。若置于 evidence，diagnostics 与 ui（ui→core,diagnostics）均不可达。

### 4.8 公共诊断数据契约（`DiagData.hpp`）

| 类型/字段 | 类型 | 必填 | 约束与语义（ERR-01） |
| --- | --- | --- | --- |
| `DiagCode` | `std::string`（别名） | — | 稳定诊断码 token：`^[A-Z0-9]+(-[A-Z0-9]+)*$`、≤64 字符；**码值分配与合法性权威＝diagnostics 的 StableCodeRegistry**，core 仅承载 |
| `DiagnosticRecord.code` | `DiagCode` | 是 | 非空且句法合法（工厂校验） |
| `.subject` | `std::optional<ObjectId>` | 条件 | 稳定（可持久化/可入报告）诊断项**必须**携带合法 subjectObjectId；瞬时开发诊断可空。完整性强制归 diagnostics/reporting 边界（§10.3 交接） |
| `.localName`、`.runtimeName` | `std::optional<std::string>` | 否 | 局部名/运行时名（经⑥名称端口取得）；缺失＝空 optional（显式，不伪造） |
| `.context` | `std::string` | 是 | 上下文描述（用户可见文案的措辞权威在 diagnostics/ui；本字段为承载） |
| `.cause`、`.recommendedAction` | `std::string` | 是 | 原因、建议动作（同上） |
| `.comparison` | `std::optional<ComparativeFields>` | 条件 | 比较型校验必填（UX-03 三要素）；非比较型为空 |
| `ComparativeValue.quantity` | `SourcedValue<double>` | 是 | 实际/期望侧数值：可 `NotApplicable`（不适用显式标记）或 `Invalid`（保留原输入） |
| `ComparativeValue.unit` | `UnitToken` | 是 | 已注册 token（显示单位；经 core 单位表产生标签） |
| `ComparativeFields.actual/.expected` | `ComparativeValue` | 是 | 实际值/期望值两侧 |
| `ConfirmationState` | 枚举 | — | `pending`/`confirmed`/`rejected` |
| `ConfirmationCredential.principal` | `std::string` | 是（凭据内） | 确认主体（如 Windows 用户名；采集归 ui/project 边界） |
| `ConfirmationCredential.confirmedAtUtc` | `std::chrono::system_clock::time_point` | 是 | UTC；无包装类型（D-02） |
| `ConfirmableFinding` | `{ DiagnosticRecord record; ConfirmationState state; std::optional<ConfirmationCredential> credential; }` | — | SA-15 比较型子类 |

不变量（core 工厂强制）：

- C-1：`ConfirmableFinding::make(record)` 要求 `record.comparison` 存在，否则抛 `CoreError`（可确认诊断必为比较型，SA-15：超限→实际值/阈值/单位）。
- C-2：`state==Confirmed ⇔ credential.has_value()`；`Pending`/`Rejected` 不得携带凭据。
- C-3：`DiagnosticRecord` 工厂校验 code 句法与必填串非空。

数据边界：确认**决策记录**随命令摘要持久化（project）；凭据的真伪/重放校验归 project 命令服务；core 不存任何确认状态机之外的状态（`Rejected` 的后续处置——阻止应用——归 project，§7.1）。

### 4.9 共享事件与端口基础契约（`Events.hpp`）

| 元素 | 定义 | 契约 |
| --- | --- | --- |
| `DomainEventKind` | `RevisionCommitted`/`DependencyInvalidated`/`TaskStatusChanged`/`ResultArchived`（token：`revision-committed` 等） | 覆盖上游所列四类（修订/失效/任务状态/结果写入） |
| `RevisionCommittedPayload` | `{ProjectId project; BranchId branch; RevisionId revision; std::optional<RevisionId> parent;}` | 首修订无 parent |
| `DependencyInvalidatedPayload` | `{ProjectId project; BranchId branch; RevisionId revision;}` | 通知信号；失效范围由 evidence 按 CON-05 切片计算，事件**不携带**对象清单（避免 DTO 膨胀；消费者经②查询端口取数） |
| `TaskStatusChangedPayload` | `{TaskIdentity task; TaskState newState;}` | 进度/心跳走 execution 自有通道（§2.2 ARCH），不入领域事件 |
| `ResultArchivedPayload` | `{TaskIdentity task;}` | 归档位置取自 RunRegistry 登记记录（execution），事件不携带路径 |
| `DomainEvent` | `{EventId id; std::chrono::system_clock::time_point emittedAtUtc; DomainEventKind kind; std::variant<四载荷> payload;}` | 值类型；`variant` 与 kind 一致（工厂保证） |
| `IDomainEventSink` | `virtual void onEvent(const DomainEvent&) = 0` | 订阅方实现；投递线程由总线实现方承诺（见下） |
| `IEventSubscription` | `virtual void unsubscribe() = 0` | RAII 语义由实现方以句柄类包装；重复 unsubscribe 幂等 |
| `IDomainEventBus` | `virtual void publish(const DomainEvent&) = 0; virtual std::unique_ptr<IEventSubscription> subscribe(IDomainEventSink&) = 0;` | **接口归 core，实现归 execution·ui 并由 L5 装配注入**（§3.5 运行时注入行）；R1 无事件过滤（订阅方收全部四类，过滤在 sink 内）——最小化，待有真实过滤需求再评估 |

总线契约（实现方必须满足，core 测试以参考实现校验语义）：`publish` 线程安全；**同一发布者**的事件 FIFO 投递（修订事件先于其失效事件）；`subscribe`/`unsubscribe` 线程安全且不与投递死锁；事件**不持久化**（需要历史者消费修订/结果领域对象，非事件重放）。

**为何必须在 core**：发布方含 project 与 execution（L3），订阅方含 ui/workflow（L3/L4）与各插件；若接口在任何一方，其余方要么反向依赖、要么复制契约（R-1/R-2 红线）。消费责任与生命周期：分发/调度/订阅管理归总线实现（execution·ui 详设冻结），core 仅持有接口与数据形状。

### 4.10 错误类型（`Errors.hpp`）

```cpp
class CoreError : public std::runtime_error {   // core 唯一异常类型
public: using std::runtime_error::runtime_error;
};
```

- 消息前缀稳定（`core/identity/parse:`、`core/units/dimension:`、`core/state/precondition:` 等），面向**开发诊断**；用户可见文案不在 core 生成（NFR-REL-05 口径）。
- 抛出场景穷尽列举于 §5 各接口"错误"行；非列举场景不得抛。
- 可恢复查询一律另有 `try*` 非抛出变体；异常仅在调用方违约或需要快速失败时出现。

---

## 5. 公共接口详细设计

以下签名为实现建议（自洽契约片段，非完整实现）。通用契约：全部函数**线程安全（可重入）**、**确定性**（同输入同输出，无隐藏状态；`generate()` 的随机性除外——身份生成不是计算结果）、**无副作用**（不触 I/O、不改全局状态）。

### 5.1 Identity.hpp

```cpp
namespace sdurws::ird::core {

// 六个 Id128 强类型同形，仅 tag 不同（obj/prj/brn/rev/run/evt）；
// 此处以 ObjectId 为代表给出完整契约，其余五个成员逐一同型：
struct ObjectId {
    static ObjectId                   generate();                       // 非零随机 128 位
    static ObjectId                   fromCanonical(std::string_view);  // "obj-<32hex>"
    static std::optional<ObjectId>    tryFromCanonical(std::string_view) noexcept;
    std::string                       toCanonical() const;
    bool                              isValid() const noexcept;         // 非全零
    bool                              operator==(const ObjectId&) const noexcept;
    bool                              operator< (const ObjectId&) const noexcept;
    std::array<std::uint8_t, 16>      bytes{};                          /* 全零＝空（保留值） */
};
// ProjectId/BranchId/RevisionId/RunId/EventId 为同形【独立 struct】（tag 分别为
// "prj-"/"brn-"/"rev-"/"run-"/"evt-"），成员与 ObjectId 逐一相同；
// 各自独立定义、互无 using 别名或隐式转换——强类型防误用是硬约束，不是示意。

struct AttemptId {
    std::uint64_t value = 0;                       // 0＝空（保留）；分配规则归 execution
    static AttemptId fromCanonical(std::string_view);                    // "att-<十进制>"
    static std::optional<AttemptId> tryFromCanonical(std::string_view) noexcept;
    std::string toCanonical() const;
    bool isValid() const noexcept;                 // value >= 1
    bool operator==(AttemptId o) const noexcept { return value == o.value; }
    bool operator< (AttemptId o) const noexcept { return value <  o.value; }
};

struct TaskIdentity {                              // 五元组（TASK-03 / ARCH §4.5）
    ProjectId   project;   BranchId  branch;   RevisionId revision;
    RunId       run;       AttemptId attempt;
    bool isValid() const noexcept;                 // 五字段全 isValid
    bool operator==(const TaskIdentity&) const noexcept;
    bool operator< (const TaskIdentity&) const noexcept;   // 仅容器用
};
// 各类型附 std::hash 特化
}
```

| 接口 | 前置条件 | 后置条件 | 错误 | 调用示例 |
| --- | --- | --- | --- | --- |
| `generate()` | 无 | 返回非零新值；碰撞概率可忽略（128 位随机）；**线程安全**（thread_local 引擎） | 无（重取零直至非零） | `auto oid = ObjectId::generate();`（project 创建对象时） |
| `fromCanonical(s)` | 无 | `parse(format(x))==x` | tag 不符/长度≠32/非 `[0-9a-f]` → `CoreError("core/identity/parse: …")` | `auto rev = RevisionId::fromCanonical(j["revision"].get<std::string>());`（io 反序列化） |
| `tryFromCanonical(s)` | 同上 | 失败返回 `nullopt`，**不抛** | — | CSV 行错误收集（io）：失败行保留原文并定位列（REQ-05/AT-02） |
| `TaskIdentity` 比较 | — | 五字段精确等值（附录 D 第 12 项：无容差） | — | execution 登记表核对（ARCH §4.5 第一段） |

调用方注意：**不同身份类型不可互比/互赋**（独立类型，无隐式转换）；把 `rev-…` 串喂给 `ObjectId::fromCanonical` 在解析边界失败——这是特性不是缺陷（防误用，§8 UT-ID-T）。

### 5.2 Digest.hpp

```cpp
using Digest256 = std::array<std::uint8_t, 32>;      // 全零＝空（保留）

struct ContentVersion {                              // 对象内容版本（CON-01）
    Digest256 bytes{};
    static ContentVersion fromCanonical(std::string_view);   // "cv-<64hex>"
    static std::optional<ContentVersion> tryFromCanonical(std::string_view) noexcept;
    std::string toCanonical() const;  bool isValid() const noexcept;
    bool operator==(const ContentVersion&) const noexcept;  /* + <, hash */
};
struct ContentIdentity {                             // 内容集合身份（CON-05/06）
    Digest256 bytes{};                               // 同上 API，tag "cid-"
};

class ContentDigester {                              // SHA-256（D-05）
public:
    ContentDigester() = default;
    void       update(const void* data, std::size_t nBytes);   // 追加
    Digest256  finalize();                                     // 终结；之后再用抛 CoreError
};
```

前置/后置：`update` 接受任意字节（含空）；`finalize` 幂等禁止（二次调用抛错）。示例（evidence 侧用法示意）：

```cpp
ContentDigester d;
d.update(canonicalSliceBytes.data(), canonicalSliceBytes.size());
ContentIdentity sliceId;  sliceId.bytes = d.finalize();   // 进入缓存键与失效判据（CON-05）
```

### 5.3 Provenance.hpp

```cpp
enum class ProvenanceKind { UserProvided, GeometricEstimate, CatalogBackfill,
                            ImportMapped, DerivedReadOnly };
const char* toToken(ProvenanceKind) noexcept;
std::optional<ProvenanceKind> provenanceKindFromToken(std::string_view) noexcept;

struct ValueProvenance {
    ProvenanceKind                    kind;             // 必填
    std::optional<ObjectId>           sourceObject;
    std::optional<ContentVersion>     sourceVersion;    // 仅 sourceObject 存在时允许
    std::optional<std::string>        methodTag;        // [a-z0-9./_-]{1,64}
    static ValueProvenance make(ProvenanceKind, std::optional<ObjectId> = {},
                                std::optional<ContentVersion> = {},
                                std::optional<std::string> = {});      // 校验 P-1/语法，违约抛 CoreError
    bool operator==(const ValueProvenance&) const noexcept;
};

enum class FieldState { Provided, NotProvided, NotApplicable, Invalid };

template <class T>
class SourcedValue {
public:
    SourcedValue();                                            // NotProvided
    static SourcedValue provided(T value, ValueProvenance);
    static SourcedValue notProvided();
    static SourcedValue notApplicable();
    static SourcedValue invalid(std::string rawInput);         // rawInput 非空
    FieldState           state() const noexcept;
    const T&             value() const;                        // 前置 Provided，否则抛 CoreError
    std::optional<T>     tryValue() const noexcept;
    const std::string&   invalidRawInput() const;              // 前置 Invalid
    const ValueProvenance& provenance() const;                 // 仅 Provided 时有意义
    bool operator==(const SourcedValue&) const noexcept;
};
```

示例（modeling 物性，MDL-05/06 语义）：

```cpp
SourcedValue<double> mass = SourcedValue<double>::provided(
    12.3, ValueProvenance::make(ProvenanceKind::GeometricEstimate,
                                /*sourceObject=*/{}, /*sourceVersion=*/{},
                                /*methodTag=*/"MDL-05/hollow-cylinder"));
SourcedValue<double> friction = SourcedValue<double>::notProvided();
// MDL-06：friction 缺失不触发断言；DYN-06 依此走 DataInsufficient 降级——由 dynamics 判定
```

### 5.4 Units.hpp

```cpp
enum class QuantityKind { Length, Angle, Mass, Time, Force, Torque, Inertia, Power,
                          LinearVelocity, AngularVelocity, LinearAcceleration,
                          AngularAcceleration, Voltage, Dimensionless };

class UnitToken {                                   // 已注册单位的值句柄
public:
    static std::optional<UnitToken> find(std::string_view symbol) noexcept;
    std::string_view symbol() const noexcept;       // 冻结 token
    QuantityKind     kind() const noexcept;
    double           siFactor() const noexcept;      // value_si = value * factor
    bool             operator==(UnitToken) const noexcept;
private: std::uint16_t tableIndex_ = 0xFFFF;        // 未注册＝无效；仅经 find() 获得
};

double                convert(double v, UnitToken from, UnitToken to);
std::optional<double> tryConvert(double v, UnitToken from, UnitToken to) noexcept;

template <QuantityKind K> struct Quantity { /* §4.4 已列成员 */ };
```

| 接口 | 前置 | 后置 | 错误 | 示例 |
| --- | --- | --- | --- | --- |
| `find(sym)` | 无 | 未知符号返回 `nullopt` | — | `UnitToken::find("mm")`（io 校验目录单位标签，SEL-02） |
| `convert/tryConvert` | `from`/`to` 经 `find` 取得 | `out = v*f(from)/f(to)`，一次乘除 | 量纲不匹配/非有限/溢出 → 抛/空（§4.4 表） | `convert(1234.0, *find("mm"), *find("m")) == 1.234`（含舍入） |
| `Quantity<K>::tryParse(v,u)` | `u` 已注册 | `kind(u)==K` 且 v 有限才成功 | — | 目录导入：`Torque::tryParse(4.5, *UnitToken::find("N*m"))` |
| `displayValueIn(u)` | kind 匹配 | 返回显示数值；**不改 SI 真值** | kind 不匹配抛 `CoreError` | kinematics 显示投影（KIN-12） |

### 5.5 Compare.hpp

```cpp
struct Tolerance {
    double relative = 0.0, absolute = 0.0;
    static Tolerance make(double rel, double abs);   // rel,abs>=0 且有限，否则抛 CoreError
};
bool closeWithin(double value, double reference, Tolerance t) noexcept;   // |v-r| <= rel*|r|+abs
bool allCloseWithin(const rw::math::Q& a, const rw::math::Q& b, Tolerance t);
bool allCloseWithin(const std::vector<double>& a, const std::vector<double>& b, Tolerance t);
std::optional<double> runtimeAbsoluteTolerance(QuantityKind k) noexcept; // 附录 D C7 转写
```

| 接口 | 前置 | 后置 | 错误 | 示例 |
| --- | --- | --- | --- | --- |
| `closeWithin` | 无（noexcept） | 非有限输入→false；零参考退化为 absolute | — | 黄金对照（testkit 消费同一公式，NFR-COR-01） |
| `allCloseWithin` | 长度相等 | 逐元素全部满足（防正负抵消，C4） | 长度不等抛 `CoreError`（违约显性化） | 轨迹速度限制校验（TRJ-05，其阈值归 trajectory） |
| `runtimeAbsoluteTolerance` | 无 | 附录 D 声明量纲→默认值；未声明→`nullopt` | — | 运行时相对校验的 ε_abs 来源（C7） |

### 5.6 Evaluation.hpp

```cpp
enum class EvaluationMode { Preview, Quick, Verified };
enum class TaskOutcome   { Completed, Canceled, Failed, Interrupted };
enum class EngineeringStatus { Feasible, EngineeringInfeasible, DataInsufficient, NotApplicable };
enum class TaskState { Queued, Preparing, Running, Paused, Canceling,
                       Canceled, Completed, Failed, Interrupted };

const char* toToken(EvaluationMode) noexcept;   /* 每个枚举一对 toToken/FromToken */
std::optional<EvaluationMode> evaluationModeFromToken(std::string_view) noexcept;
// …TaskOutcome/EngineeringStatus/TaskState 同型 API
```

token 往返：`fromToken(toToken(x))==x`；未知 token → `nullopt`。语义锚点见 §4.7（判定规则不在此）。示例：`evaluationModeFromToken("verified")`（io 从快照元数据反序列化）。

### 5.7 DiagData.hpp

```cpp
using DiagCode = std::string;

struct ComparativeValue { SourcedValue<double> quantity; UnitToken unit; };
struct ComparativeFields { ComparativeValue actual, expected; };

struct DiagnosticRecord {
    DiagCode   code;                                 // 句法校验
    std::optional<ObjectId> subject;
    std::optional<std::string> localName, runtimeName;
    std::string context, cause, recommendedAction;
    std::optional<ComparativeFields> comparison;
    static DiagnosticRecord make(/*上列字段*/);      // C-3 校验；违约抛 CoreError
};

enum class ConfirmationState { Pending, Confirmed, Rejected };
struct ConfirmationCredential { std::string principal;
                                std::chrono::system_clock::time_point confirmedAtUtc; };

struct ConfirmableFinding {
    DiagnosticRecord  record;
    ConfirmationState state = ConfirmationState::Pending;
    std::optional<ConfirmationCredential> credential;    // C-2
    static ConfirmableFinding make(DiagnosticRecord r);  // C-1：必为比较型
    ConfirmableFinding confirmed(ConfirmationCredential) &&;  // Pending→Confirmed
    ConfirmableFinding rejected() &&;                        // Pending→Rejected
};
```

示例（MDL-06④ 行程上限，阈值数值与比较由 project/modeling 依策略执行，core 只承载结果）：

```cpp
auto finding = ConfirmableFinding::make(DiagnosticRecord::make(
    /*code=*/"MDL-06-TRAVEL-LIMIT",                       // 码值归 diagnostics 注册表
    /*subject=*/jointOid, /*localName=*/"joint_5", /*runtimeName=*/"Robot.joint_5",
    /*context=*/"应用模型修改：行程上限策略校验",
    /*cause=*/"有限限位旋转关节行程超出工程策略默认阈值（4π rad）",
    /*recommendedAction=*/"确认多圈工艺必要性后放行，或收窄限位",
    /*comparison=*/ComparativeFields{
        {SourcedValue<double>::provided(6.0*rw::math::Pi, ValueProvenance::make(ProvenanceKind::UserProvided)),
         UnitToken::find("rad").value()},
        {SourcedValue<double>::provided(4.0*rw::math::Pi, ValueProvenance::make(ProvenanceKind::UserProvided)),
         UnitToken::find("rad").value()}}));
// → project 命令服务收集待确认集，经命令上下文回调交 ui 呈现（SA-15）
```

### 5.8 Events.hpp

```cpp
enum class DomainEventKind { RevisionCommitted, DependencyInvalidated,
                             TaskStatusChanged, ResultArchived };
struct RevisionCommittedPayload    { ProjectId project; BranchId branch;
                                     RevisionId revision; std::optional<RevisionId> parent; };
struct DependencyInvalidatedPayload{ ProjectId project; BranchId branch; RevisionId revision; };
struct TaskStatusChangedPayload    { TaskIdentity task; TaskState newState; };
struct ResultArchivedPayload       { TaskIdentity task; };

struct DomainEvent {
    EventId   id;                       // 发布方 generate()
    std::chrono::system_clock::time_point emittedAtUtc;
    DomainEventKind kind;
    std::variant<RevisionCommittedPayload, DependencyInvalidatedPayload,
                 TaskStatusChangedPayload, ResultArchivedPayload> payload;
    static DomainEvent make(DomainEventKind, /*对应载荷*/);   // kind↔variant 一致性校验
};

class IDomainEventSink { public: virtual ~IDomainEventSink() = default;
                         virtual void onEvent(const DomainEvent&) = 0; };
class IEventSubscription { public: virtual ~IEventSubscription() = default;
                           virtual void unsubscribe() = 0; };
class IDomainEventBus { public: virtual ~IDomainEventBus() = default;
    virtual void publish(const DomainEvent&) = 0;
    virtual std::unique_ptr<IEventSubscription> subscribe(IDomainEventSink&) = 0; };
```

| 接口 | 前置 | 后置/契约 | 错误 | 示例 |
| --- | --- | --- | --- | --- |
| `DomainEvent::make` | 载荷字段合法 | kind 与 variant 备选一致 | 不一致抛 `CoreError` | project 提交修订后构造 `RevisionCommitted` |
| `bus.publish(e)` | 总线实例已注入 | 同发布者 FIFO；投递线程由实现承诺；**至少一次/进程内恰一次** | 实现 定义 | project 发布修订→失效两事件（顺序敏感） |
| `bus.subscribe(s)` | sink 存活期覆盖订阅期 | 返回 RAII 句柄；退订后不再投递 | 实现 定义 | workflow 订阅全部事件驱动门控（UX-12） |

---

## 6. 核心行为、算法与异常处理

### 6.1 行为与算法要点

1. **解析严格性**：一切 `fromCanonical` 拒绝宽松输入（大小写、空白、前缀零填充变体）；这保证持久化文本与运行值一一对应，io 层无需做"归一化"二次判断。
2. **摘要确定性**：SHA-256 标准实现；不引入盐、不依赖端序（按字节流处理）；跨进程（worker/main）一致（CON-06 内容身份可比的前提）。
3. **换算数值口径**：单次乘＋单次除；`deg→rad` 因子 `π/180` 以 `rw::math::Pi`（与框架一致的 π 常量）计算；往返误差按附录 D 容差在测试中约束（§8）。
4. **比较零参考退化**：`closeWithin` 在 `reference==0`（含 −0）时仅用 `absolute`（附录 D C4"零参考退化为 ε_abs，不要求精确零"）。
5. **随机性范围**：仅 `generate()`（身份分配）；一切计算函数不含随机因素（NFR-COR-02）。
6. **保留值纪律**：全零 Id128/Digest256、`AttemptId==0` 为"空"；所有工厂不产出、`isValid()` 拒绝；杜绝"全零 id 被当作真实对象"。

### 6.2 异常处理策略

| 场景类 | 处理 | 理由 |
| --- | --- | --- |
| 调用方违约（前置条件破坏：状态读取越界、长度不等、kind 不匹配） | 抛 `CoreError`（无 UB；debug 下前置断言同时触发） | 边界显性化（TASK-02"构造边界拒绝"同精神的值层版本） |
| 数据错误但调用方需收集（导入解析、目录校验） | `try*` 返回 `nullopt`，调用方（io/各域）组织逐行/逐列诊断 | REQ-05/AT-02 逐行错误模式 |
| 数值非法（非有限/溢出） | 拒绝产生值（抛/空），绝不归零 | NFR-COR-03 |
| core 不捕获、不吞异常；异常不跨越进程边界（通道层由 execution 负责错误码化） | — | 进程模型边界（ARCH §4） |

### 6.3 序列化：core 类型稳定表示契约（三层分工）

| 层 | 责任 | 位置 |
| --- | --- | --- |
| **稳定表示契约**（本文冻结） | 各类型的规范文本（§4.1/§4.2 tag＋hex、§4.3 状态/来源 token、§4.4 单位 token、§4.7 枚举 token）；规则：全 ASCII、`parse(format(x))==x`、token 只增不改名（改名＝破坏性变更，走设计变更评审并同步升级器责任评估） | core |
| 编码/解码实现（JSON/CSV/XML 中嵌这些 token、转义、方言标识） | io（NFR-SEC-03 CSV 方言即 io 读写器职责） | io |
| 磁盘写入与格式升级（.rwdesign 布局、schemaVersion、升级器） | project（NFR-DEP-04） | project |

**core 不读写任何项目文件**；`fromCanonical/toToken` 只做字符串⇄值，不触 I/O。

---

## 7. 跨单元协作与典型调用流程

### F1 建模应用与可确认诊断放行（S1/SA-15 的 core 类型走位）

```
modeling 产出 ConfirmableFinding（core DiagData；码值来自 diagnostics 注册表）
→ project ProjectCommandService 收集待确认集 → 命令上下文回调（project 接口）→ ui 确认对话
→ 用户确认：ui 组装 ConfirmationCredential → project 调 finding.confirmed(cred)
→ 确认记录写入命令摘要（project 持久化）→ 修订提交 → project 经 IDomainEventBus 发布
   RevisionCommitted → DependencyInvalidated（同一发布者，FIFO 保证顺序）
→ ui/workflow/evidence 订阅方刷新（ARCH §7.1）
```

core 承担：ConfirmableFinding/凭据形状、事件形状；不做：断言判定（project/modeling）、交互（ui）、摘要落盘（project）。

### F2 批量评估的身份与结果词表（S2 的 core 切面）

```
kinematics 组装快照（evidence）→ execution 派发：登记 RunRegistry（TaskIdentity 五元组＋归档位置）
→ 工作进程完成 → 完成事件携带五元组（core TaskIdentity）
→ execution 登记表核对（规则归 execution；相等比较用 core 精确等值）
→ 结果接纳 → ResultEnvelope（evidence；其 outcome/engineeringStatus 用 core 枚举词表）
→ execution 发布 TaskStatusChanged / ResultArchived（core 事件）
→ ResultCurrentness 用 ContentIdentity 比较（evidence 计算，core 承载值与摘要器）
```

### F3 显示单位切换（AT-27 的 core 切面）

```
用户切 mm → kinematics 更新显示设置（PM-14 用户级，不入项目）
→ 显示层逐值 displayValueIn(UnitToken::find("mm")) → 界面呈现
→ 不改 SI 真值、不产生修订、不触发重算、不进入缓存身份（KIN-12/AT-27）
（换算唯一入口＝core；不存在第二处 ×1000 实现）
```

### F4 导入行错误与缺失值（io/requirements 切面）

```
CSV 逐行解析（io）→ 字段类型 tryFromCanonical/tryParse 失败 → 该值入 SourcedValue::invalid(原文)
→ 正确行保留、错误定位到列与原文（AT-02）→ 未填字段＝ notProvided()
→ 非法 Must 条目 →"输入未完成"（REQ-06，requirements 判定；core 只保证四态不混淆）
```

### F5 项目打开的身份解析（project/io 切面）

```
project 读 revisions/<rev-id>/ 清单 → RevisionId::fromCanonical（目录名即规范文本）
→ 任何失败＝文件损坏诊断（PM-02 读校验），core 不猜测、不修复
```

---

## 8. 验证与测试设计

测试目标 `sdurws_ird_core_test`（gtest 接入方式见 P-ENV-2）。**本文所有测试均为设计，未实现、未运行；实现状态随 §9 任务登记。**测试可使用 testkit 设施（稳定集合断言、容差档案），但 core 产品目标不依赖 testkit。测试以边界风险驱动，分组如下：

| 组 | 用例（节选，含正/反例） | 针对的风险 | 追踪 |
| --- | --- | --- | --- |
| UT-ID-T 身份误用 | `rev-…` 解析为 `ObjectId` 失败；tag 大写/长度 31/非法字符失败；合法往返 `parse(format(x))==x`；`generate()` 非零且同进程内大量生成无碰撞（1e6 次，去重计数）；五元组缺一即 `isValid()==false` | 类型误用、解析宽松 | ARC-04、附录 D 第 12 项 |
| UT-ID-D 摘要 | SHA-256（FIPS 180-2）已知向量（空串 `e3b0c442…`、`"abc"` `ba7816bf…`）；同字节同摘要、异字节摘要异；finalize 后再用抛错 | 算法实现错误、跨平台不一致 | NFR-COR-02、CON-05/06 |
| UT-MISS 缺失值 | 四态互不相等；`NotProvided`/`NotApplicable`/`Invalid` 调 `value()` 抛 `CoreError`；`invalid` 保留原文；`tryValue` 仅 Provided 有值；默认构造≠数值零（比较 `SourcedValue<double>{} == provided(0.0,…)` 为 false） | 缺失静默转零（NFR-COR-03） | NFR-COR-03、MDL-06 |
| UT-UNIT 单位 | `mm→m`、`deg→rad` 往返（附录 D 容差内）；量纲不匹配（`N*m`→`N`）抛错/返回空；`tryConvert(NaN,…)→nullopt`；`DBL_MAX mm→m` 溢出拒绝；未知符号 `find` 空；`Quantity<Torque>::tryParse(4.5, "N")` 失败（kind 不符）；token 表完整性（R1 清单逐项注册） | 非法单位、非有限、溢出 | NFR-COR-03、KIN-12、SEL-02 |
| UT-TOL 容差比较 | 零参考退化（`closeWithin(0,0,{0,1e-12})==true`；`closeWithin(1e-13,0,…)` 视 ε）；近零值；**正负抵消反例**：`a={+1,−1}, b={−1,+1}` 逐元素判不等（总和比较会误判相等——C4）；`Q` 长度不等抛错；`runtimeAbsoluteTolerance` 各量纲返回值与附录 D C7 逐一对照、未声明量纲返回空 | 比较公式走样、阈值私设 | 附录 D C4/C7 |
| UT-CONV 位姿约定（约定锁定测试） | 已知算例：绕 Z 转 90° 后点 (1,0,0)→(0,1,0)；`T_ac==T_ab*T_bc` 数值断言（平移+旋转复合）；`inverse(T_ab)*T_ab==Identity`（容差内） | 框架约定误读传染全产品 | ARCH §7.3、MDL-22 notation |
| UT-EVAL 词表 | 四枚举 token 往返；未知 token 空；`NotApplicable` 存在于 EngineeringStatus（表 3 显式标记） | 词表漂移 | §8.1 表 1/表 3、ARCH §4.3 |
| UT-DIAG 诊断契约 | 比较型字段存在性（非比较型 record 构造成功、Confirmable 必须比较型否则抛）；C-2 状态/凭据约束；`invalid` 实际值保留原文（UX-03"保留原值"）；`NotApplicable` 期望值显式标记 | 字段缺失/伪造数值 | ERR-01、UX-03、SA-15 |
| UT-EVT 事件 | 四类载荷构造与 kind 一致性；`make` 错配抛错；事件值拷贝/移动/相等；参考总线（测试内最小实现）验证 FIFO 与退订语义 | 契约漂移 | TASK-03、ARCH §7.2⑤ |
| UT-BUILD 构建红线 | core 目标无 Qt（编译期以脚本扫描 `#include <Q` 零命中）；无私有头出 `include/`；CMake 依赖图无 industrialrobot 其他单元边（依赖表门禁 R-1/R-2/R-3 的 core 侧用例，正式门禁归 WP-01-T01） | Qt 渗入、反向依赖 | NFR-MNT-01/02、ARCH §3.2 |
| UT-VAL 值语义 | 公共数据类型拷贝/移动/相等/哈希一致性（相等实例哈希相等；容器可用性编译断言） | 所有权/生命周期误用 | §4 约定 |

---

## 9. 阶段 A 实现任务拆分

局部编号 `CORE-Txx`（上游 WP 归属：core＝**WP-03**，REQUIREMENTS §3/ARCH §3.1 既有登记；本表不重排任何 WP 编号；DETAILED-DESIGN/development-task-breakdown 产出后如需对齐，以增量修订处理）。

| 任务 | 输入 | 产出 | 依赖 | 涉及文件 | 验证方式 | 完成条件 |
| --- | --- | --- | --- | --- | --- | --- |
| CORE-T01 构建落位 | 骨架 CMakeLists、§3.3 | `sdurws_ird_core` 升级 STATIC（C++17、链 `sdurw_math`）；注册 `sdurws_ird_core_test`（gtest 按 development-task-breakdown §5.5 定稿） | 无 | `industrialrobot/CMakeLists.txt`、`core/CMakeLists.txt`（新）、`core/src/*`（空起步） | 独立冒烟＋集成构建各配置成功；UT-BUILD 脚本通过 | 两模式下构建零错误；C++17 与基线共存验证记录（P-ENV-1 消账） |
| CORE-T02 身份与摘要 | §4.1/§4.2、§5.1/§5.2 | `Identity.hpp/.cpp`、`Digest.hpp/.cpp`（SHA-256） | CORE-T01 | 同名文件 | UT-ID-T、UT-ID-D | 全部用例通过；token/保留值行为与本文一致 |
| CORE-T03 来源与缺失值 | §4.3、§5.3 | `Provenance.hpp/.cpp` | CORE-T02（引用 ObjectId/ContentVersion） | 同名文件 | UT-MISS | 四态语义与 P-1 不变量用例通过 |
| CORE-T04 单位与量 | §4.4、§5.4 | `Units.hpp/.cpp`（R1 单位表） | CORE-T01 | 同名文件 | UT-UNIT | R1 token 表完整；错误口径三态（抛/空/正常）用例通过 |
| CORE-T05 容差比较 | §4.5、§5.5 | `Compare.hpp/.cpp` | CORE-T04（QuantityKind）、rw::math | 同名文件 | UT-TOL | C4 公式与 C7 转写用例通过（含正负抵消反例） |
| CORE-T06 评估词表 | §4.7、§5.6 | `Evaluation.hpp/.cpp` | CORE-T01 | 同名文件 | UT-EVAL | token 往返用例通过 |
| CORE-T07 诊断契约 | §4.8、§5.7 | `DiagData.hpp/.cpp` | CORE-T03、CORE-T04 | 同名文件 | UT-DIAG | C-1～C-3 不变量用例通过 |
| CORE-T08 事件契约 | §4.9、§5.8 | `Events.hpp/.cpp` | CORE-T02、CORE-T06 | 同名文件 | UT-EVT | 载荷/一致性/FIFO（测试内参考总线）用例通过 |
| CORE-T09 约定锁定与值语义 | §4.6、§8 | UT-CONV、UT-VAL 测试体 | CORE-T01 | `core/test/*` | 直接运行 | 已知算例与值语义断言通过 |
| CORE-T10 文档与门禁同步 | 全文 | README 指向核对；UT-BUILD 扫描并入 CI 建议；§10.2 待裁决项状态更新 | CORE-T01…T09 | 本文、`core/include/.../README.md` | 评审 | 本文与实现零偏差登记；未决项有最新状态 |

每任务完成条件均含"测试通过并留痕"；任何未执行测试不得标注通过。

---

## 10. 风险、待裁决项与下游交接清单

### 10.1 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | 上游 ARCHITECTURE 尚为 `Draft`（v0.11 待评审），评审可能调整单元职责/依赖表 | core 契约位置或形状需返工 | 本文严格锚定 §3.1/§3.5/§7.2/§7.8 明文；变更影响面集中在 §2.4 表，可增量同步 |
| R-2 | canonical 序列化分散在各所有者（evidence/policy/runtime/project），若不一致则 ContentIdentity 不可跨单元比较 | 缓存/当前性误判 | §4.2 责任边界＋§10.3 交接项要求各任务卡登记其 canonical 序列化；跨单元契约测试（后续单元） |
| R-3 | 词表双轴（TaskState vs TaskOutcome；七态 vs 九态）易被下游混用 | 状态语义漂移 | §4.7 不变量＋交接项；ui 详设须区分 UX-10 七态（ui 投影）与九态词表 |
| R-4 | core 头被无节制追加"顺手工具"，退化为杂物箱（NFR-MNT-04 红线） | 依赖图劣化 | §2.4 论证表为准入门槛：新公共类型必须补行登记 |
| R-5 | 磁盘事实与 REQUIREMENTS v1.10 不符（`old/` 缺失） | 附录 A 台账不可现场查证 | 本文仅登记不处置；从头构建口径不受影响；建议需求侧下次修订留痕 |
| R-6 | C++17 与 RobWork C++11 基线混链的 ABI/ODR 边缘（MSVC 下通常安全） | 集成构建失败或罕见运行时问题 | CORE-T01 双模式构建验证（P-ENV-1）；不使用 C++20 |

### 10.2 待裁决项（问题—影响—建议—裁决者）

| # | 问题 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- |
| P-AR-1 | ARCHITECTURE §3.5"io→core，diagnostics｜接口依赖｜SafePath/BudgetGuard 类型、导入诊断"与 §7.9/§3.1（SafePath/BudgetGuard 为 io 设施）表述含混：依赖表"用途"列可读作"SafePath/BudgetGuard 类型在 core" | 若按依赖表读法，core 需增两个安全设施类型，与"core 不拥有 io 防护"的分层叙述冲突 | 按本文 §2.4 执行：SafePath/BudgetGuard 类型与实现均归 io（仅 io 消费，无共享必要）；core 只向 io 提供 ObjectId 等身份类型；待架构侧在依赖表澄清措辞 | 架构所有者（本文已按建议设计，**不新增反向依赖**，裁决不改 core 现设计） |
| P-AR-2 | ARCHITECTURE v0.11 状态 `Draft` 待评审；本文以其为基线 | 评审结论可能要求同步 | 评审后按影响面增量修订本文并留痕 | 架构所有者 |
| P-D-1 | EvaluationMode/TaskOutcome/EngineeringStatus/TaskState 四词表置 core，是对 ARCH §3.1"评估语义"＋依赖拓扑的解释性落地（§4.7 论证） | evidence/execution/diagnostics/ui 详设若另作安排（如全数移入 evidence），core 需迁移词表 | 维持本文方案；evidence.md 与 execution.md 起草时交叉核对（diagnostics 仅依赖 core 是硬约束） | 详设所有者（evidence/execution 任务卡）；不涉需求语义，无需需求变更 |
| P-ENV-1 | **已关闭（2026-09-10，findings F-003）**：sdurws_ird_core（C++17 STATIC）与 sdurw_math/common（C++11 dll）同树混链构建零错误，C++17/基线共存成立 | （已消除）原"混链失败即阻塞 CORE-T01 起全部任务"风险 | 消账证据：CORE-T01 实施段留痕（traceability/builds/wp03-t01/、phase-one-validation.log 消账段）＋CORE-T01 验收 4.2 验收者独立复现（traceability/acceptance/CORE-T01-20260910.md）；回落 C++14 预案未启用，留档备查 | 构建负责人（验收独立复现即确认） |
| P-ENV-2 | **已关闭（2026-09-10，CR-07）**：`_test` 目标 gtest 接入机制定稿——development-task-breakdown v0.2 §5.5：vcpkg 安装（`gtest:x64-windows`）＋`find_package(GTest CONFIG REQUIRED)`，失败即停；不消费从未生成的 `RW::gtest`（实测 USE_gtest=OFF、`RobWork/cmake/gtestTargets.cmake` 不存在） | （已消除）原测试目标注册方式悬空 | 按 §5.5 执行；安装与版本登记随 TK-T01（≙WP-02-T01），CORE-T01 仅按机制注册目标 | 构建约定所有者（已定稿） |
| P-AR-3 | "证据等级"（EVI-01 措辞）除 EvaluationMode 外是否另有独立等级词表 | 若有且被 diagnostics/ui 消费，则需在 core 增词表；若仅为模式效力（§8.1 表 1），则现状足够 | 本文按"等级＝模式效力"理解（不新增）；evidence.md 起草时确认，必要时走本文增量修订 | evidence 详设所有者＋需求侧澄清 |

### 10.3 下游交接清单（后续单元详设的直接输入）

| 下游单元 | 从 core 接收 | 须在其详设冻结的相邻契约 |
| --- | --- | --- |
| project | 全身份类型、ContentVersion/ContentIdentity、SourcedValue/ValueProvenance、DiagnosticRecord/ConfirmableFinding、IDomainEventBus（发布方） | 身份分配时机、修订记录结构（含单调序号——不进 RevisionId）、命令摘要中确认记录的编码、对象 canonical 编码（供 ContentVersion 计算）、事件发布时序 |
| evidence | 枚举四词表、ContentIdentity/ContentDigester、Tolerance/closeWithin、TaskIdentity | 切片 canonical 序列化、ResultEnvelope 构造校验器、RequiredEvidenceProfile 承载、证据等级词表（P-AR-3） |
| runtime | ObjectId、位姿/单位约定（§4.6＋UT-CONV）、ContentIdentity | R_world_base 在 CanonicalModel 中的字段、RuntimeNameMap canonical 序列化 |
| policy | ContentIdentity/ContentDigester、Tolerance 值形状 | EngineeringPolicySet 序列化与阈值数值（附录 D 第 11 项等） |
| execution | TaskIdentity、TaskState/TaskOutcome、IDomainEventBus（发布方/实现方之一） | RunRegistry 规则、状态机转移矩阵、TaskState→TaskOutcome 终态映射、总线实现（投递线程承诺） |
| diagnostics | DiagnosticRecord/ConfirmableFinding 基类型、DiagCode 句法 | StableCodeRegistry、码值清单、文案权威、两级日志、脱敏；诊断对象完整性强制（subject 必填边界） |
| io | tryParse/tryConvert、UnitToken、SourcedValue::invalid（原文保留） | CSV 方言/转义（NFR-SEC-03）、SafePath/BudgetGuard（按 P-AR-1 结论）、token 在文件格式中的嵌套编码 |
| ui | IDomainEventSink/IDomainEventBus、TaskState（9 态短标签）、单位换算（显示） | 确认对话凭据采集、UX-10 七态投影与九态的映射、总线 UI 线程投递实现 |
| testkit | 全部公共值类型、Tolerance、比较公式 | 黄金数据集容差档案与 core Tolerance 的一致引用方式 |
| 各业务域 | §2.1 所列全部公共契约 | 各自领域对象中的 SourcedValue 字段化、显示单位消费、枚举使用 |

---

## 11. 需求—设计—验证追踪矩阵

| 需求条目 | core 设计落点 | 验证（§8 组） |
| --- | --- | --- |
| CON-01（身份/版本包络） | §4.1/§4.2 类型与四概念区分 | UT-ID-T、UT-ID-D |
| CON-05/06（内容身份；策略/名称映射内容身份） | §4.2 ContentIdentity/ContentDigester、§5.2 | UT-ID-D |
| ARC-04（稳定对象 ID；精确等值） | §4.1 强类型＋附录 D 第 12 项 | UT-ID-T |
| TASK-02＋§8.1 表 3（合法组合词表） | §4.7 TaskOutcome/EngineeringStatus（校验器归 evidence） | UT-EVAL |
| TASK-03（五元组身份） | §4.1 TaskIdentity | UT-ID-T |
| ERR-01（诊断字段；比较型；不适用标记） | §4.8 全部字段与不变量 C-1～C-3 | UT-DIAG |
| MDL-06④＋SA-15（可确认诊断） | §4.8 ConfirmableFinding/ConfirmationCredential、§5.7 示例 | UT-DIAG |
| EVI-01 表 1（评估模式） | §4.7 EvaluationMode | UT-EVAL |
| KIN-12（显示单位不改真值） | §4.4 唯一换算入口＋Quantity、§7 F3 | UT-UNIT |
| DYN-03（类型化广义力） | §4.4 Force/Torque 分量级 | UT-UNIT |
| SEL-02（单位校验） | §4.4 find/tryConvert | UT-UNIT |
| NFR-COR-02（确定性） | §6.1 要点 2/3/5 | UT-ID-D、UT-TOL |
| NFR-COR-03（非有限/非法单位/缺失不得静默转 0） | §4.3 四态、§4.4 错误口径、§4.5 非有限→false | UT-MISS、UT-UNIT |
| NFR-MNT-01（内核零 Qt） | §3.2/§3.3、§2.2 第 11 条 | UT-BUILD |
| NFR-MNT-03（单位换算单一权威） | §2.1、§4.4 | UT-UNIT＋构建门禁 |
| NFR-MNT-04（禁无价值包装） | §4.6 点/方向不包装、§2.4 准入表 | 设计评审＋UT-BUILD |
| 附录 D C4（通用比较公式/逐元素） | §4.5 closeWithin/allCloseWithin | UT-TOL |
| 附录 D C7（运行时 ε_abs 按量纲） | §4.5 runtimeAbsoluteTolerance（转写） | UT-TOL |
| 附录 D 第 12 项（身份精确等值） | §4.1 相等语义 | UT-ID-T |
| MDL-05/16、DYN-06（来源标记；非可信等级） | §4.3 ValueProvenance＋语义边界声明 | UT-MISS（结构） |
| PM-03（9 态短标签语义源） | §4.7 TaskState | UT-EVAL |
| UX-03（比较型三要素数据基础） | §4.8 ComparativeFields | UT-DIAG |
| ARCH §7.2⑤（事件端口接口归 core） | §4.9 | UT-EVT |
| ARCH §7.3/MDL-22（基座—世界变换单一不变量） | §4.6 承接声明（core 不实现） | 归 runtime 契约测试（AT-37）；core 侧仅 UT-CONV 约定锁定 |

---

## 12. 设计决策与变更记录

### 12.1 设计决策登记（本文作出并说明理由的普通实现选择）

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | industrialrobot 目标显式 `CXX_STANDARD 17`，不用 C++20 | MSVC2022/Qt6.11 工具链完整支持；C++17 的 optional/variant/string_view 是本文契约的自然载体；基线 RobWork 为 C++11，混链安全性由 CORE-T01 验证（P-ENV-1）。备选 C++14 需自造 optional/variant，成本高 |
| D-02 | 不包装 `rw::math` 类型、时间点、点/方向；帧引用直接用 ObjectId | NFR-MNT-04：无边界价值的包装器禁止；RobWork 类型即框架基线边界；`std::chrono` 即标准边界 |
| D-03 | 身份＝128 位随机值＋类型 tag 前缀规范文本；全零保留为"空" | tag 使跨类型误用在解析边界失败（防误用）；128 位随机满足非对抗唯一性；保留值纪律杜绝"全零对象"。备选（顺序号）会迫使 core 卷入分配策略（project 语义） |
| D-04 | RevisionId 不内嵌单调序号；顺序信息归 project 修订记录 | 修订分配/排序是 ARC-01/PM-12 语义，core 只提供不透明稳定身份；避免双权威 |
| D-05 | 摘要算法＝SHA-256（256 位） | 非对抗场景下 FNV-128 亦足，但 SHA-256 有公开测试向量可钉、碰撞余量充分、实现约百行；单一算法单点实现（跨进程一致性） |
| D-06 | `runtimeAbsoluteTolerance` 在 core 转写附录 D C7 默认值 | 按量纲键控、无单一业务所有者、被多域消费——单点转写防逐域复制；修改只能随需求变更（本文不构成对附录 D 的修订） |
| D-07 | 错误表达＝`CoreError` 异常＋`try*` 非抛出双轨 | RobWork 异常惯例＋导入路径需错误收集（REQ-05 逐行）；查询路径 noexcept 化降低热路径负担 |
| D-08 | 事件为值类型＋variant 载荷，不建 OOD 继承体系 | 值可跨线程队列、可拷贝留痕；四类载荷封闭（对齐上游四类），扩展走设计变更 |
| D-09 | 事件不携带数据快照（仅身份），失效明细由消费者查询②端口 | 避免事件 DTO 膨胀与双权威（数据在修订/结果对象里）；通知与数据分离 |
| D-10 | 电压入量纲、货币/温度不入；`rpm`/`min` 不预建 | 严格按 R1/R2 已登记消费者（SEL-03 电压）；无消费者不预建（总体详设 §1.5 原则同精神）；后续追加为兼容性扩展 |
| D-11 | `ConfirmableFinding` 为组合（内含 DiagnosticRecord）而非 C++ 继承 | 值语义优先、避免切片；"比较型子类"为语义关系（SA-15），非实现继承 |
| D-12 | 词表 token 全小写连字符、单位 token 全 ASCII | 持久化与跨平台安全（避免 Unicode `·`/`²` 的编码风险） |

### 12.2 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-09 | 首版：基于 REQUIREMENTS v1.16（Accepted）与 ARCHITECTURE v0.11（Draft）完成 12 章详细设计；登记磁盘现状（DETAILED-DESIGN/development-task-breakdown/其余 units 卡缺失、old/ 缺失）；登记待裁决 6 项（P-AR-1～3、P-ENV-1～2、P-D-1 归入待核对）；实现任务 CORE-T01～T10 |
| v0.2 | 2026-09-10 | FOUNDATION-CR-01 契约冻结审查（CR-01/02/06/07/08 均通过，词表/摘要/比较/错误契约被四个消费单元按原文对齐——差异记录见 traceability/foundation-api-diff.md）：P-ENV-2 关闭（gtest 定稿＝vcpkg＋find_package(GTest CONFIG REQUIRED)，不消费从未生成的 RW::gtest）；CORE-T01 行 gtest 引用改指 development-task-breakdown §5.5；本文接口无修改 |
| v0.3 | 2026-09-10 | P-ENV-1 关闭（findings F-003 消账）：C++17/基线混链经 CORE-T01 双模式构建＋验收独立复现证实成立，回落 C++14 预案留档不用；仅 §10.2 状态行翻转，接口与任务行零修改 |
| v0.4 | 2026-09-10 | CORE-T02（≙WP-03-T02）落位登记：①§4.1/§5.1 身份六强类型＋AttemptId＋TaskIdentity 与 §4.2/§5.2 摘要类型＋ContentDigester（SHA-256，D-05）按原文契约实现，接口零偏差；②`Errors.hpp`（§4.10 CoreError）无独立任务行，随本任务以"首个消费者落位最小契约"惯例建立——签名与 §4.10 原文逐字一致（仅 using 构造透传），不新增成员（DTB §5.4 偏差登记）；③实现取舍留痕：保留值"全零"在解析层可回（句法合法），拒绝由 isValid() 在业务边界执行；FNV-1a 128 哈希为两 64 位半字模拟实现（uint64 定宽保证平台无关，NFR-COR-02） |

### 12.3 自审记录（v0.1 交付前逐项检查；自审≠实现测试≠正式验收）

| 检查项 | 结论 |
| --- | --- |
| 需求/架构引用存在且指向当前正文 | ✔ 全部引用 v1.16/v0.11 现行条款；未引用已删除章节（旧 §29 等未出现） |
| 重复权威/反向依赖/Qt 渗入 | ✔ §2.3/§2.4 逐一论证；core 仅依赖 std＋sdurw_math；无业务阈值私有化 |
| 业务算法/平台服务误入 core | ✔ 快照/策略/状态机/总线实现/码表/文件 I/O 均显式排除（§2.2） |
| 未经需求批准的阈值/状态/功能 | ✔ 唯一数值转写来自附录 D C7（声明修改通道）；无新增状态（TaskState 九态＝ARCH §4.3 原文） |
| 公共接口未定义类型/循环依赖/所有权不清 | ✔ 全部类型定义于本文四个模块或指明归 rw::math/std；无前向引用缺失；所有权见 §2.4 |
| 阶段 A 可独立实现与验证 | ✔ CORE-T01～T10 依赖闭包完整（仅外部项 P-ENV-1/2 已登记并给出回落方案） |
| 下游可据交接清单继续设计 | ✔ §10.3 十类接收方均有输入与待冻结相邻契约清单 |
| 待上游裁决事项集中登记 | ✔ §10.2 六项（问题—影响—建议—裁决者） |

## AI 执行就绪补充

本单元进入 AI 实现前必须满足：

- 本文中的职责、非职责、公共接口、数据模型、错误语义和不变量不得与 ARCHITECTURE.md 冲突。
- 单元任务使用 $(core.ToUpper())-Txx 编号，并通过 doc/industrial-robot-design/tasks/*.json 声明前置任务、允许修改文件和验证命令。
- 跨单元协作只能使用本文列出的公共接口或架构端口；发现接口缺失、需求冲突或依赖未登记时，任务状态必须标记为 locked。
- 实现完成必须通过单元测试、依赖门禁和追踪矩阵校验，不能只以代码编译成功作为完成条件。

### 单元任务退出条件

| 条件 | 要求 |
|---|---|
| 设计 | 本文接口、状态、不变量和错误语义已冻结或明确登记开放问题 |
| 实现 | 只修改任务契约允许的文件 |
| 测试 | 单元测试覆盖本文列出的正例、反例和不变量 |
| 追踪 | 每个任务至少关联一个需求和一个验证目标 |
| 门禁 | alidate-docs.ps1、alidate-task.ps1 和单元验证命令通过 |
