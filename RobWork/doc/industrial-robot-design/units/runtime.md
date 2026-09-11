# 工业机械臂设计软件 · runtime 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.6（RT-T05 实现落位登记；v0.5＝RT-T04 实现落位登记；v0.4＝RT-T03 实现落位登记；v0.3＝全链一致性审计消账；v0.2＝FOUNDATION-CR-01 契约审查修正；v0.1＝首版草案） |
| 日期 | 2026-09-12 |
| 状态 | **`Draft-Structured`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收） |
| 文档代号 | UNIT-RUNTIME |
| 单元 | runtime（平台内核，L2 计算内核层；ARCHITECTURE §2.3/§3.1：CanonicalModel、确定性编译器（含基座—世界变换）、RuntimeNameMap、RobWork 运行时适配） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md` **v0.1（Draft，未冻结）**、`units/testkit.md` **v0.1（Draft，未冻结）**、`units/project.md` **v0.1（Draft，未冻结）**、`units/evidence.md` **v0.1（Draft，未冻结）**。本文消费的 core 公共契约以其 v0.1 签名为基线并逐项登记（§3.2）；协作输入变更时本文按影响面增量同步（P-RT-1） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立）→ `units/*.md`（单元任务卡）。本文即 `units/runtime.md`，按任务卡深度编写（接口签名、数据类型在本文件冻结） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/`（骨架已建：目标 `sdurws_ird_runtime`〔INTERFACE 占位〕＋别名 `RWS::ird::runtime`＋公共头保留位 `include/sdurws/ird/runtime/README.md`；见上级 `industrialrobot/CMakeLists.txt`） |
| 任务归属 | 详见 `development-task-breakdown.md` WP-B～WP-I 映射；本文只维护单元内部任务
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 仅功能范围对照且**当前磁盘缺失**（core.md §1.2 R-5 同源登记）；历史实现不作为语义来源，不复制、不恢复 |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 runtime 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 runtime 的职责（§3.1 单元总表行；§7.3 支柱三"权威模型与确定性编译"、§7.4 支柱四"名称解析"；§7.2 ⑥名称端口；SA-04/SA-05），把 CanonicalModel 数据契约、确定性编译链（含基座—世界变换单一不变量）、RuntimeNameMap 双向解析、RobWork 运行时适配层、RuntimeSnapshot 与并发/缓存纯判定、公共接口、跨单元交接、验证方案与阶段 A 任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不收窄、不扩大；本文引用的需求 ID 与章节号均指 v1.16 当前正文（重点：ARC-03/04、CON-01/02/05、MDL-06/14/18/21/22、KIN/TRJ/DYN 中涉及运行时模型的条款、NFR-COR/MNT/REL、AT-01/18/19/27/37、附录 D 数值容差）。架构归属以 `ARCHITECTURE.md` v0.11 为准（SA-02/SA-04/SA-05/SA-10 直接约束本单元）；本文对上游含混或未登记事项的解释集中登记于 §15.3（P-RT-1～P-RT-9），不私自裁决、不越权修改。

**三段链声明（贯穿全文）**：`RobotDesign`（权威参数化，项目对象，schema 归 modeling）→ `CanonicalModel`（runtime 拥有的不可变 SE(3) 关节链规范模型，编译输入的规范化快照）→ `WorkCell` / `DynamicWorkCell`（runtime 构造并持有的 RobWork 派生运行时视图）。一切 FK/IK/轨迹/动力/碰撞计算只消费这条链（ARC-03）；本单元是这条链的唯一实现者。

### 1.2 上游与磁盘现状登记（2026-09-10 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文承接：ARC-03/04（主）、CON-01/02/05、MDL-06/14/18/20/21/22、KIN-01/12/13/14、TRJ-01～04、DYN-01～04、NFR-COR-01/02/03、NFR-MNT-01/03/04/07、NFR-REL-04、附录 D（第 4/5/12 项）、AT-01/16/18/19/27/37/38 |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。§3.5 依赖表登记 runtime→core（**唯一 industrialrobot 编译依赖**）；§7.3/§7.4/§7.2⑥/SA-04/SA-05 直接约束本文。若评审产生 A9+ 处置按影响面同步（P-RT-2） |
| `units/core.md` | 存在，v0.1，`Draft`（未冻结） | 协作输入。本文消费其身份/摘要/词表/诊断/单位契约（§3.2 逐项登记）；其 §10.3 已向 runtime 交接"R_world_base 在 CanonicalModel 中的字段、RuntimeNameMap canonical 序列化"——本文 §4.3/§7.6 即该交接的承接答复 |
| `units/testkit.md` | 存在，v0.1，`Draft`（未冻结） | 协作输入。其 §10.2 已登记 runtime 侧义务：FK/编译链等价数据集（analytic，附录 D 第 4 项容差）、名称往返测试——本文 §11/§12 承接 |
| `units/project.md` | 存在，v0.1，`Draft`（未冻结），**磁盘实测完整**（§1～§15 全在，1384 行） | 协作输入。其 §5.3.6 已单侧冻结 `IModelCompilePort` 最小契约并点名"runtime 详设起草时以本文契约为起点交叉核对"（P-PR-7）——本文 §10.2 给出承接答复（对齐结论：兼容，差异两处登记）。**注意**：evidence.md §1.2/P-EV-6 曾登记 project.md "磁盘版本不完整（仅 §1～§7，`__PART3__` 占位）"——该登记基于其编写时点，现已过时；本文以磁盘实测为准，并登记该差异（P-RT-9）供 evidence 侧消账 |
| `units/evidence.md` | 存在，v0.1，`Draft`（未冻结） | 协作输入。其 §3.3 注入模式（IObjectBytesSource/IRevisionClosureSource）、§4.1.1 规范模型引用行（"CanonicalModel＝瞬态派生，不入快照存储；其身份＝f(对象闭包, 编译器契约版本)"）、§13"runtime/policy 承接：RuntimeNameMap 的 canonical 序列化与内容身份计算"——本文 §4.5/§7.6/§9.5 承接。git 状态显示其 README 有一处未提交修改（§9→§12 指向修正，其变更记录已留痕），不影响契约 |
| `units/runtime.md` | **不存在**（本文新建） | 其余 15 个单元任务卡（policy/execution/diagnostics/io/modeling 等）均未产出；本文需要其接口处给最小依赖契约并登记交接（§10/§13），不代写对方详设 |
| `DETAILED-DESIGN.md` | 已建立 | 20 个单元详设总目录，本文是单元详设正文 |
| `development-task-breakdown.md` | 已建立 | WP-A～WP-I 主 WP 计划，本文只维护单元任务 |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 单元目录＋`patches/`（0001 补丁＋PATCHES.md）；`runtime/` 下仅 `include/sdurws/ird/runtime/README.md`（公共头保留位，已引用本文 §9——随本文结构修正为 §12，见变更记录） | `sdurws_ird_runtime` 为 INTERFACE 占位，无源码；与 REQUIREMENTS 文档头"20 单元骨架，23 文件"一致 |
| RobWork 基线（实测） | `rw::models::WorkCell`（addFrame/addDevice/findFrame/getFrames/getWorldFrame/getDefaultState）、`rw::models::SerialDevice/JointDevice`、`rw::models::RevoluteJoint/Joint`（setBounds/getMaxVelocity/getMaxAcceleration）、`rw::kinematics::Frame/FixedFrame/MovableFrame/State/StateStructure`、`rwsim::dynamics::DynamicWorkCell`（addBody/addDevice/findBody，`rw::core::Ptr` 共享所有权）——源码树 `RobWork/RobWork/src/rw/`、`RobWork/RobWorkSim/src/rwsim/` | 本文 §8 适配层按实测 API 设计；基线 C++11、无 Qt 依赖的库目标（`sdurw_math`/`sdurw_kinematics`/`sdurw_models`/rwsim dynamics），精确目标名随 RT-T01 集成构建确认 |
| `old/` | **不存在于磁盘**（git 亦未跟踪） | 与 REQUIREMENTS v1.10 声明不符（core.md §1.2 R-5 已登记）；对本文无输入 |

### 1.3 设计目标

1. **规范真值唯一**（ARC-03/SA-04）：CanonicalModel 是全产品唯一的 SE(3) 关节链规范模型；一切下游计算经 runtime 适配层消费编译产物，任何单元不得绕过该链自建运动学表示。
2. **确定性**（ARC-03 验收、NFR-COR-02）：编译是纯函数——同输入对象集（含资源内容身份与编译选项）→ 同 CanonicalModel 内容身份 → 同 WorkCell/DWC 结构与同 RuntimeNameMap；不读隐藏状态、不依赖线程时序、不依赖运行环境（路径、 locale、时间）。
3. **名称唯一解析**（ARC-04/SA-05）：RuntimeNameMap 是 ObjectId↔运行时名称的唯一双向解析器；前缀拼接/剥离唯一合法位置是本单元实现（R-4 红线例外登记，NFR-MNT-07）。
4. **基座—世界变换单一不变量**（MDL-22/M-11、ARCH §7.3）：安装姿态以单一 `T_world_base` 编译进 CanonicalModel，WorkCell 中唯一写入点，四类消费方读同一编译产物，禁止任何下游二次叠加。
5. **失败可恢复、无半成品**（MDL-06、CON-04）：编译是事务——任一阶段失败不发布任何部分产物；产物与输入内容身份绑定；取消/超时/资源失败按稳定诊断清理。
6. **零 Qt、零反向依赖**（NFR-MNT-01、ARCH §2.3/§3.5）：runtime 仅依赖 core＋标准库＋L1 RobWork 基线库；project/evidence/policy/execution/modeling 等能力一律经注入的最小只读接口或值传递获得。
7. **不越权判定**：能力缺失≠输入非法≠编译失败≠工程不可行——四者正交承载（§5.6/§9.6）；runtime 不实现碰撞规则、工程判定阈值或任何业务算法（policy/各域所有）。
8. **不预建空业务**（任务约束）：阶段 A 交付 CanonicalModel 契约、校验器、编译基础设施、WC/DWC 适配、基座—世界规则、名称映射、快照、诊断与能力声明、缓存纯判定、可控测试替身与契约测试；真实 RobotDesign 读取（modeling 实现）、真实动力学消费（dynamics）按阶段 B/C 接入。

### 1.4 语言、标准库与构建约束（按仓库实测确认，与 core.md §1.4 同源）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | 构建缓存 `build/CMakeCache.txt`：Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计，不做平台特定扩展 |
| Qt | Qt 6.11.1（msvc2022_64） | **runtime 目标禁止包含任何 Qt 头**（构建红线 R-3，ARCH §3.2；L2 计算内核零 Qt） |
| C++ 标准 | 基线 RobWork `CMAKE_CXX_STANDARD 11`；industrialrobot 目标显式 C++17（core.md D-01） | 同口径：显式 `cxx_std_17`；`std::optional/variant/string_view/shared_ptr` 允许；不用 C++20；不引入 Boost 等第三方运行库 |
| RobWork 基线库 | `rw::math`（sdurw_math）、`rw::kinematics`（Frame/State）、`rw::models`（WorkCell/Device/Joint）、`rwsim::dynamics`（RobWorkSim） | runtime 直接使用（L1 允许边界；§8.1 论证＋P-RT-3 登记 §2.3 措辞确认）；**零源码修改**（SA-02），确需修改走 `patches/` 集中登记（§8.8） |
| Eigen | 经 `rw::math` 头传递出现 | 不直接包含 Eigen 头 |
| 异常 | RobWork 惯例异常（`rw::common::Exception`） | runtime 用自有异常类型 `RuntimeError`（§3.4）；适配层在边界捕获 RobWork 异常转稳定诊断（§8.4） |
| JSON/序列化 | 无共享 JSON 库（vcpkg installed 实测无，testkit.md §1.4） | CanonicalModel/NameMap canonical 编码为**自有二进制编码**（RT-Codec，§4.5）——与 evidence SliceCodec 同一决策模式，不依赖 JSON |

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有总表（本节先行给出，全文以此为准）

**runtime 拥有（实现责任方）**：

| # | 能力 | 上游依据 | 设计落点 |
| --- | --- | --- | --- |
| O-1 | CanonicalModel 不可变数据契约（字段、单位、身份、能力块、诊断块）及其 canonical 编码（RT-Codec）与内容身份计算 | ARC-03、SA-04、CON-01（消费侧） | §4 |
| O-2 | 规范模型→运行时模型的确定性编译器（十段编译链、编译事务、失败回滚、取消） | ARC-03、MDL-06、ARCH §7.3 | §5 |
| O-3 | WorkCell 与 DynamicWorkCell 的构造与一致性校验（含 WC↔DWC 关联校验、基座—世界一致性检查） | MDL-06、AT-01、ARCH §7.3 | §5、§8 |
| O-4 | 基座—世界变换的唯一编译规则（`T_world_base` 单一存储、WorkCell 唯一写入点、四消费方读取契约） | MDL-22（M-7/M-11）、AT-37、ARCH §7.3 | §6 |
| O-5 | RuntimeNameMap 双向名称解析（生成/反解/冲突处置/内容身份）——⑥名称端口所有者 | ARC-04、MDL-14、CON-06、AT-18、ARCH §7.2⑥/§7.4 | §7 |
| O-6 | 运行时对象身份、层级关系与路径索引（规范对象↔Frame/Joint/Device/Body/Geometry/Sensor 的对象级映射） | MDL-14、ARC-04 | §4.6、§7 |
| O-7 | 编译诊断、模型适配错误与运行时能力声明的承载（RT-\* 建议诊断码、RuntimeCapability） | ERR-01（承载）、AT-01 | §3.4、§5.6、§9.6 |
| O-8 | 编译结果缓存键的纯内容契约与兼容判定（不做存储/淘汰） | CON-04、OPT-06 | §9.4 |
| O-9 | 面向下游的只读运行时视图（IRuntimeModelView、WorkCellConstView、per-thread State 工厂）与 RuntimeSnapshot | ARC-03、KIN-01、TRJ-01、DYN-01 | §8、§9 |
| O-10 | RobWork 适配层（异常/空句柄/无效层级→稳定诊断；基线版本登记；显式设值纪律） | SA-02、NFR-MNT-07、ARCH §5.1 | §8 |
| O-11 | 资源引用在 CanonicalModel 中的记录契约（摘要/来源/固化状态/读取版本）与编译期资源一致性检测 | CON-03（记录侧）、NFR-REL-04（检测消费） | §5.4、§8.6 |

**runtime 消费（经依赖白名单与注入）**：

| 消费方 → 被消费 | 形态 | 消费内容 | 上游依据 | 状态 |
| --- | --- | --- | --- | --- |
| runtime → core | 接口依赖（编译链接，**唯一** industrialrobot 边） | ObjectId/ProjectId/BranchId/RevisionId、ContentVersion/ContentIdentity/ContentDigester、SourcedValue/ValueProvenance、QuantityKind/UnitToken/convert、Tolerance/closeWithin、DiagnosticRecord/ComparativeFields、CoreError | ARCH §3.5 表 | core.md v0.1 **Draft 未冻结**（P-RT-1） |
| runtime → L1 RobWork 基线 | 接口依赖 | `rw::math`/`rw::kinematics`/`rw::models`/`rwsim::dynamics` 类型与库（只读使用＋构造期写入，零源码修改） | ARCH §5.1/§7.3、SA-02 | 基线实测 API（§1.2）；目标名随 RT-T01 确认 |
| runtime →（注入）project 只读能力 | **运行时注入**（零编译依赖） | 修订对象字节与修订闭包查询，经本文最小接口 `IObjectBytesSource`/`IRevisionClosureSource`（§3.3；适配 project ②端口 IProjectQueryPort；适配器归 L5 装配——与 evidence §3.3 同一模式） | ARCH §3.5（表未登记 runtime→project 边） | project.md §5.2 已有 IProjectQueryPort |
| runtime →（注入）modeling 参数化读取 | **运行时注入** | `IRobotDesignReader`（RobotDesign 对象字节→`RobotDesignDescription` 中性值类型；§4.2），modeling 实现、L5 装配注入 | ARCH §7.3（编译输入）、MDL-02/09/13/22 | modeling 详设未产出（P-RT-5） |
| runtime →（注入）io 资源读取 | **运行时注入** | `IRuntimeResourceProvider`（已校验资源字节＋摘要；§8.6），io 实现 | NFR-SEC-01/02、NFR-REL-04（检测归 io） | io 详设已编写；P-RT-6 已由 io §8.6 裁决关闭 |
| runtime →（注入）execution 取消/预算 | **运行时注入** | `ICompileCancelToken`（协作取消查询）、内存预算信号（§5.7） | TASK-01、NFR-PERF-04 | execution 详设未产出（§13 交接） |
| runtime →（值传递）policy 内容身份 | 值传递（core ContentIdentity） | 已解析 EngineeringPolicySet 内容身份——编译缓存键**不含**策略（策略经 evidence 切片进入缓存与失效，CON-06；runtime 编译产物不消费策略内容——碰撞规则/阈值归 policy 评估期消费） | CON-06、ARC-05 | policy 详设未产出 |

**runtime 不拥有（其他单元所有，本文不实现、不预建桩）**：

| # | 不拥有内容 | 所有者 | 上游依据 |
| --- | --- | --- | --- |
| N-1 | 项目目录、HEAD、修订、分支、草稿、锁与事务；`.rwdesign` 一切读写 | project | §17 表注、ARCH §6 |
| N-2 | RobotDesign 的编辑命令、业务参数化算法、DH↔显式转换求解、物性估算公式表 | modeling | MDL-01～10/13/16、ARCH §7.3 |
| N-3 | FK/IK/Jacobian/可操作度、轨迹规划、逆动力学 RNEA、选型与优化算法 | kinematics/trajectory/dynamics/drivetrain/selection/optimization | KIN/TRJ/DYN/SEL/OPT 家族 |
| N-4 | 碰撞算法、碰撞策略、工程判定阈值（含行程上限 4π）、工程结论 | policy | ARC-05、KIN-05、MDL-06④、附录 D 第 11 项 |
| N-5 | AnalysisSnapshot、InputSlice、ResultEnvelope、RequiredEvidenceProfile、结果当前性的业务定义与计算 | evidence | CON-02/04/05/06、EVI-01/02、SA-07/SA-13 |
| N-6 | RunRegistry、任务状态机、调度、工作进程池、检查点、缓存存储与淘汰治理 | execution | TASK-01～03、CON-04、ARCH §4 |
| N-7 | URDF/Xacro/CSV/网格/WorkCell XML 的解析与安全导入 | io | NFR-SEC-01～03、MDL-03/18/19 |
| N-8 | 诊断码值分配/文案、两级日志、脱敏 | diagnostics | ERR-01、NFR-REL-05 |
| N-9 | 报告渲染、界面呈现、阶段状态 | reporting/ui/workflow | RPT/UX 家族 |
| N-10 | 结果当前性与缓存淘汰策略（runtime 只提供纯判定） | evidence/execution | CON-02/04/05 |
| N-11 | MDL-20 规范模型包导出/导入与 WorkCell/DWC XML 导出（消费 runtime 编译产物，序列化归 modeling/io） | modeling/io | MDL-20 |

### 2.2 直接承接的需求（runtime 为实现责任方或数据契约责任方之一）

| 需求 | runtime 承接的部分 | 不可越界的部分 |
| --- | --- | --- |
| ARC-03（运行时运动学真值唯一） | CanonicalModel 定义与确定性编译链；编译前后 FK/世界轴线一致的承载（等价验证入口，§11 RT-EQ-\*） | FK/IK 算法本体（kinematics 消费编译产物执行） |
| ARC-04（稳定对象 ID＋统一名称解析器） | RuntimeNameMap 生成/反解唯一实现；R-4 例外登记（前缀操作唯一合法位置） | ObjectId 类型（core）；业务单元引用规则（静态门禁） |
| CON-01（分析从完整不可变快照运行） | 编译输入只来自只读修订视图（防混入，§5.2 S1）；RuntimeSnapshot 编译完成后零外部依赖 | 快照组装（evidence）；修订视图（project ②端口） |
| CON-02（三态正交） | RuntimeSnapshot 不可变、旧快照存活支持迟到结果归档（§9.3）；当前性投影归 evidence | 当前性计算与结果生命周期 |
| CON-04（缓存按契约判定） | 编译缓存键纯判定（judgeCompileCacheCompatibility，§9.4）；部分/失败产物不得作为完整命中 | 缓存存储、淘汰、命中执行（execution） |
| CON-05（切片内容身份） | CanonicalModel 内容身份＋编译器契约版本进入评估切片的 Environment 条目（evidence 复现块消费，§9.5）；"不把当前 HEAD 前进自动视为运行时模型失效"（缓存键纯内容，§9.4） | 切片构建与失效矩阵（evidence） |
| CON-06（快照保存 RuntimeNameMap 内容身份；名称先反解后接纳） | RuntimeNameMap canonical 序列化与内容身份计算（core R-2 承接）；反解服务按**结果所绑定快照**的映射执行（§7.5/§9.3） | 接纳流程执行（execution）；策略解析（policy） |
| MDL-06（双编译原子性） | 编译事务：WorkCell 与 DynamicWorkCell 任一**硬失败**→整体失败、不发布（§5.3）；经 L5 适配实现 project 的 IModelCompilePort（§10.2） | 物理合法性断言判定（modeling 处理器）；确认放行流（project/diagnostics） |
| MDL-14（编译时生成完整 RuntimeNameMap） | 全量生成与双向校验（关节/Frame/限位/几何/碰撞与动力学引用经 ObjectId 解析，无遗漏/无重复前缀/无旧机械臂名） | 碰撞规则语义（policy）；建模编辑（modeling） |
| MDL-21（传动耦合矩阵，R2/D） | CanonicalModel 承载常矩阵 C＋适用边界；编译期校验（非常矩阵/病态→诊断并阻止，§5.2 S3）；view 供 DYN-04/SEL 消费 | 虚功映射算法（drivetrain）；R1 阻断门控（modeling，MDL-12 口径） |
| MDL-22（基座安装姿态与重力配置） | `T_world_base` 单一存储与编译、默认地面安装（未显式配置→R=I，V15-04）、四消费方同一编译产物、显示/计算变换隔离（§6） | 安装姿态编辑 UI（modeling）；重力投影计算本体（dynamics 消费 view） |
| KIN-01/12/13/14（运行时模型相关） | 编译产物只读视图＋per-thread State；显示单位变化不入模型身份（§4.4）；求解配置不入编译输入（归 evidence Configuration 条目）；设默认 TCP 经命令（modeling/project，runtime 消费新修订） | FK/Jacobian 算法；单位换算（core）；配置 schema（kinematics） |
| TRJ-01～04、DYN-01～04（运行时模型相关） | 为路径/时间参数化提供设备与关节限位/速度/加速度视图；为 RNEA 提供含质量/惯量/摩擦的 DWC 与关节侧参数；DYN-04 关节侧结果与传动无关（runtime 不在编译中做映射） | 轨迹/动力学算法（trajectory/dynamics/drivetrain） |
| NFR-COR-01/02/03 | 确定性编译（同输入同字节产物）；身份不含浮点近似等价；非有限/非法单位/引用缺失在编译边界拒绝（§5.2 S3） | 算法对照测试执行（testkit/各域） |
| NFR-MNT-01/07（内核零 Qt；名称前缀禁令） | runtime 目标本体；R-4 唯一例外实现点＋登记 | 全局门禁（WP-01-T01） |
| NFR-REL-04（外部资源可检测） | 编译期资源摘要复查（资源变化→编译失败/缓存失效，§8.6）——检测能力的 runtime 切面 | 检测实现与用户流程（io/PM-09） |
| 附录 D 第 4 项（FK/编译链等价验证容差 1×10⁻⁹ m/rad） | 契约测试消费：同一 CanonicalModel 重复编译 FK 一致、canonical↔WC FK 等价（§11 RT-EQ-\*） | 容差数值权威＝REQUIREMENTS（testkit 档案转写） |
| 附录 D 第 12 项（名称与身份精确等值） | RuntimeNameMap 全部比较精确等值、无容差（§7.2） | — |

### 2.3 明确不做（非目标，与 §2.1 不拥有表互为补充）

不实现第二套模型真值（CanonicalModel 是唯一规范模型；WC/DWC 只是它的派生视图）；不实现第二套名称映射或基座变换规则；不把字符串名称拼接/剥离当作通用解析机制（唯一解析器在 §7）；不用"确定性/线程安全/语义一致"等口号代替可执行契约（逐条落到字段表、状态机、判定表与测试矩阵）；不实现碰撞规则、工程判定阈值或任何策略内容；不持有 4π 等策略数值；不做结果当前性或缓存淘汰决策；不在编译期间写项目历史（编译是只读输入→瞬态产物的纯变换，事务归 project）；不序列化/持久化 WC/DWC 编译产物（MDL-20 导出由 modeling/io 消费编译产物另行序列化）；不代写 policy/execution/io/modeling 详设（最小注入契约＋§13 交接）。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 组成（公共头模块）

runtime 由 12 个公共头模块＋1 个编译实现组成（实现文件随 §12 任务落地，头为契约权威）：

| 头（`runtime/include/sdurws/ird/runtime/`） | 内容 | 详见 |
| --- | --- | --- |
| `Errors.hpp` | RuntimeErrorCode（稳定 token 建议）、RuntimeError、Expected\<T,E\>、RuntimeResolveError/RuntimeNameError | §3.4、§7.4 |
| `Sources.hpp` | IObjectBytesSource、IRevisionClosureSource、RevisionSummary、ICompileCancelToken（注入最小契约） | §3.3、§5.1 |
| `Description.hpp` | RobotDesignDescription（中性输入值类型：关节/连杆/基座/工具/场景/参数/物性/传动，全 SourcedValue 化）、IRobotDesignReader；另承载 §4.2 引用的辅助值类型（JointType/WorkingRange/GeometryRef/CouplingMatrix/InstallationPresetToken——后者 §3.1 原列 BaseWorldTransform.hpp，因 §4.2 BasePlacementDescription 字段依赖随 RT-T03 在此落位，RT-T06 经 include 复用，见 §15.4 v0.4） | §4.2 |
| `CanonicalModel.hpp` | CanonicalModel 及全部子结构（§4.3 字段表）、CanonicalModelBuilder、RuntimeCapability | §4、§9.6 |
| `Codec.hpp` | RT-Codec（CanonicalModel/NameMap canonical 二进制编码与解码、往返、往返版本） | §4.5、§7.6 |
| `Compiler.hpp` | CompileRequest/CompileOptions/CompileOutcome/CompileStatus、ICanonicalModelCompiler、编译链阶段枚举 | §5 |
| `BaseWorldTransform.hpp` | 安装预设、T_world_base 校验、正反解纯函数（InstallationPresetToken 枚举自 v0.4 起随 Description.hpp 落位——§4.2 字段依赖先行，本头经 include 复用） | §6 |
| `NameMap.hpp` | RuntimeName、RuntimeNameView、ObjectRef、NameScope、RuntimeNameMap、buildRuntimeNameMap、IRuntimeNameResolver | §7 |
| `Adapter.hpp` | WorkCellConstView、DynamicWorkCellConstView、DeviceView、RobWorkBaselineVersion、IRobWorkAdapterFactory | §8.2～§8.5 |
| `Resource.hpp` | ResourceRef/ResourceManifest、ResourceReadError、IRuntimeResourceProvider（值类型 ResourceState/ResourceRef 自 v0.4 起随 RT-T03 落位——§4.2 resourceRefs 字段依赖先行；其余实体随资源消费任务） | §8.6 |
| `Snapshot.hpp` | RuntimeSnapshot、SnapshotIdentity、IRuntimeSnapshotFactory、MaterializedSnapshotCodec（worker 物化） | §9.1～§9.3 |
| `CacheKey.hpp` | CompileCacheKey 及其分量、CompileCacheCompatibility、judgeCompileCacheCompatibility、IRuntimeCompileCacheKey | §9.4 |
| `README.md` | 既有保留位说明（不参与编译；已指向本文 §12） | — |
| `src/`（实现，随 RT-T01 建） | 编译器、编码器、名称映射、适配层、校验器等非模板实现 | §5～§9 |

### 3.2 依赖（含 core 契约消费状态登记）

```
sdurws_ird_runtime ──► RWS::ird::core（PUBLIC：身份/摘要/来源/单位/比较/诊断数据契约）
                  ──► C++17 标准库
                  ──► L1 RobWork 基线：sdurw_math、rw::kinematics、rw::models（WorkCell/Device/Joint）、
                      rwsim::dynamics（DynamicWorkCell；目标名随 RT-T01 集成构建确认）——零 Qt、零源码修改（SA-02）
                  ──✖ 零 Qt（R-3）、零其他 industrialrobot 单元链接（policy/evidence/execution/io/业务单元）
运行时注入（零编译依赖）：IObjectBytesSource/IRevisionClosureSource（适配 project ②端口）、
                      IRobotDesignReader（modeling）、IRuntimeResourceProvider（io）、
                      ICompileCancelToken（execution）、L5 装配负责适配器与注入
```

消费的 core 契约清单（全部来自 core.md v0.1 §4/§5；**状态：Draft 未冻结**，P-RT-1）：

| core 契约 | runtime 用途 | 状态锚点 |
| --- | --- | --- |
| ObjectId/ProjectId/BranchId/RevisionId | CanonicalModel 身份块、名称映射、快照绑定 | v0.1 §4.1/§5.1 |
| ContentVersion/ContentIdentity/ContentDigester | CanonicalModel/NameMap/快照内容身份；资源摘要（SHA-256 唯一实现） | v0.1 §4.2/§5.2 |
| SourcedValue\<T\>/ValueProvenance | Description 与 CanonicalModel 物性/摩擦/限位的四态承载（缺失≠非法，§5.6） | v0.1 §4.3/§5.3 |
| QuantityKind/UnitToken/convert/Quantity\<K\> | 编译边界单位换算（SI 化）的唯一入口；非法单位拒绝 | v0.1 §4.4/§5.4 |
| Tolerance/closeWithin | 基座—世界一致性数值检查（§6.5）、耦合矩阵条件数等数值校验的公式承载 | v0.1 §4.5/§5.5 |
| DiagnosticRecord/ComparativeFields | 编译诊断与能力缺失警告的承载（码值权威＝diagnostics 注册表） | v0.1 §4.8/§5.7 |
| CoreError | core 抛错捕获与转发 | v0.1 §4.10 |
| （文档契约）rw::math 位姿读法 T_ab＝b 相对 a | 全部位姿字段的方向语义（core §4.6 冻结；UT-CONV 锁定） | v0.1 §4.6 |

### 3.3 对 project/modeling/io/execution 能力的注入边界（依赖白名单的实施形态）

ARCH §3.5 依赖表只登记 runtime→core；快照编译客观上需要读修订对象、参数化模型与资源。解决方案＝**值传递＋最小注入接口**（与 evidence §3.3、project §5.3.6 同一模式；适配器归 L5 应用壳装配期统一提供，避免各请求方重复适配——NFR-MNT-04）：

```cpp
// Sources.hpp —— runtime 定义的最小只读接口（适配器归 L5，不归 runtime）
class IObjectBytesSource {            // 适配 project ②端口 tryObject(oid, cv)
public:
    virtual ~IObjectBytesSource() = default;
    virtual std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId, core::ContentVersion) const = 0;
};
struct ObjectRefEntry {               // 修订对象引用（与 evidence §4.1.2 ObjectRefEntry 同形）
    core::ObjectId      objectId;
    core::ContentVersion contentVersion;
    std::string          objectTypeToken;
    core::Digest256      digest;
};
struct RevisionSummary {              // 修订只读视图的最小投影（适配 project RevisionView）
    core::RevisionId id; std::uint64_t seq;
    std::optional<core::RevisionId> parent; core::BranchId branch;
    std::vector<ObjectRefEntry> objectRefs;
};
class IRevisionClosureSource {        // 适配 project ②端口 revision()/head() ＋闭包校验
public:
    virtual ~IRevisionClosureSource() = default;
    virtual std::optional<RevisionSummary> tryRevision(core::RevisionId) const = 0;
    virtual bool objectInRevision(core::RevisionId, core::ObjectId,
                                  core::ContentVersion) const = 0;
};
class ICompileCancelToken {           // 适配 execution 取消信号（协作取消；注入可为空＝不可取消）
public:
    virtual ~ICompileCancelToken() = default;
    virtual bool cancellationRequested() const = 0;
};
```

worker 进程内的实现＝"随请求物化的载荷存储"（execution 序列化修订摘要与对象字节后装配，§9.3），同样零 project 依赖。

### 3.4 命名空间、错误类型与 CMake 集成

- 命名空间 `sdurws::ird::runtime`；目标 `sdurws_ird_runtime`（骨架 INTERFACE → RT-T01 升级 STATIC），别名 `RWS::ird::runtime`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_runtime PUBLIC RWS::ird::core <rw/rwsim 目标>)`。
- 测试目标：`sdurws_ird_runtime_test`（单元内）、`sdurws_ird_runtime_contract_test`（跨单元契约：与 core 值类型/比较、与 project 适配器、与 testkit 谓词与容差档案、与真实 RobWork 基线库的结构断言；gtest 接入按 development-task-breakdown.md §5.5 定稿——vcpkg＋`find_package(GTest CONFIG REQUIRED)`，失败即停），随 RT-T01 登记；产品目标不链 testkit（T-1 红线）。
- 头包含形式 `#include <sdurws/ird/runtime/CanonicalModel.hpp>`；私有实现头不入 `include/`（R-2 纪律）。
- **错误类型**：`RuntimeError`（携 `RuntimeErrorCode` 稳定 token，消息前缀 `runtime/...` 面向开发诊断）；查询路径一律提供 `Expected<T, E>` 非抛出变体：

```cpp
// Errors.hpp
enum class RuntimeErrorCode {           // token 稳定（持久化于诊断/报告）；建议码值归 diagnostics StableCodeRegistry
    InputInvalid,          // runtime/input-invalid          输入非法（结构/数值/引用，就地定位）
    UnitMismatch,          // runtime/unit-mismatch          单位/量纲非法
    StructureInvalid,      // runtime/structure-invalid      链结构非法（环/断链/重复身份）
    ResourceMissing,       // runtime/resource-missing       资源缺失（io 已诊断，此处定位 resourceId）
    ResourceChanged,       // runtime/resource-changed       编译期间资源内容变化（§8.6 复查失败）
    ResourceBudget,        // runtime/resource-budget        资源预算超限
    WorkCellCompileFailed, // runtime/wc-compile-failed      WorkCell 编译硬失败（含 RobWork 异常转译）
    DwcCompileFailed,      // runtime/dwc-compile-failed     DynamicWorkCell 编译硬失败
    NameConflict,          // runtime/name-conflict          名称冲突无法消歧/非法字符无法合法化
    BaseWorldInconsistent, // runtime/base-world-inconsistent 基座—世界一致性检查失败
    CacheIncompatible,     // runtime/cache-incompatible     缓存兼容判定拒绝（供调用方转译）
    Cancelled,             // runtime/cancelled              协作取消（非错误路径，UX-03）
    UnknownObject,         // runtime/unknown-object         解析目标不在本快照
    ContextReleased,       // runtime/context-released       快照/编译上下文已释放
    RobWorkError,          // runtime/robwork-error          RobWork 基线异常/空句柄/无效层级（§8.4）
};
class RuntimeError : public std::runtime_error { /* code() 访问器 */ };
template <class T, class E> struct Expected {    // C++17 值语义结果（无 std::expected）
    std::variant<T, E> value;
    bool ok() const noexcept;  const T& get() const;   // 前置 ok()
    const E& error() const;                          // 前置 !ok()
    static Expected ok(T);  static Expected err(E);
};
```

---

## 4. CanonicalModel 与规范数据契约

### 4.1 四个模型对象的区别与真值/派生关系（先于字段表冻结）

| 对象 | 是什么 | 谁拥有 schema/实现 | 可变性 | 生命周期 | 与其他对象的关系 |
| --- | --- | --- | --- | --- | --- |
| `RobotDesign` | 权威参数化项目对象（StandardDH 或显式 Joint+Origin+Axis 两种权威源之一，MDL-02；含 Axis 一等字段、安装姿态、工程工作范围、物性来源标记、工具/场景引用） | modeling（schema 与编辑）；project（持久化编址）；对象字节经注入的 IRobotDesignReader 读取 | 项目对象不可变（修改＝新内容版本＋新修订） | 持久化（objects/） | **规范真值**——一切运行时模型的唯一来源 |
| `CanonicalModel` | 从某修订的对象闭包确定性编译出的不可变 SE(3) 关节链规范模型（规范化后的值快照：SI 单位、显式关节表示、单一 `T_world_base`、资源内容绑定、能力与诊断块） | runtime（本文单点冻结） | **不可变**（构造后无 setter；值语义拷贝） | 瞬态（不持久化；随 RuntimeSnapshot 存活；可经 RT-Codec 序列化供 worker 物化） | 派生的**运行时规范真值**：同修订重复编译字节一致；其内容身份进入评估切片 Environment 条目 |
| RobWork `WorkCell` | CanonicalModel 的运动学运行时视图（Frame 树/Device/Joint/几何/限位） | RobWork 基线类型；runtime 构造并持有（§8） | 构造期由编译器写入；发布后只读 | 瞬态（随快照存活；不序列化） | 纯派生视图——任何字段都可从 CanonicalModel 重新导出 |
| RobWork `DynamicWorkCell` | CanonicalModel 的动力学运行时视图（在 WC 之上叠加 Body/RigidDevice/质量惯量/摩擦） | RobWorkSim 基线类型；runtime 构造并持有 | 同上 | 瞬态 | 纯派生视图；与 WC 的关联由编译器保证（§8.5） |

**不变量 CM-0（真值唯一）**：不存在第四个运动学真值表示。kinematics/trajectory/dynamics/policy 的计算一律消费上述派生视图（经 §9 只读接口）；任何单元不得缓存"自算的"关节链。同一修订中**不混入不同对象版本**：CanonicalModel 的每个对象引用 (ObjectId, ContentVersion) 必须通过 `IRevisionClosureSource::objectInRevision(revision, oid, cv)` 校验（§5.2 S1/S2，evidence §4.1.5 同一防线），失败＝`StructureInvalid` 诊断并整体失败——这是"防混入"的编译侧执行点。

### 4.2 RobotDesignDescription（注入输入的中性值类型）

**为什么需要它**：RobotDesign 对象 schema 归 modeling（N-2），runtime 不解析其对象内码；但编译需要完整的参数化语义。方案＝runtime 公共头定义**中性描述值类型**，modeling 实现 `IRobotDesignReader`（对象字节→Description；纯函数），L5 装配注入。阶段 A 以契约夹具直接构造 Description 值测试编译链（§12）；真实 reader 随 modeling（阶段 B）交付（P-RT-5 登记：与 modeling 详设交叉核对接口形态；若 modeling 选择"直接解码自有 canonical 编码"，本文以增量修订对齐，编译链其余部分不变）。

```cpp
// Description.hpp（节选；字段语义=MDL 家族，全部 SourcedValue 化承载缺失/非法四态）
struct JointDescription {          // 关节（权威参数化侧；DH 权威时由 modeling 先转为显式表示）
    std::string localName;        // 权威局部名（进入 RuntimeNameMap 与模型身份）
    JointType type;               // Revolute | Continuous | Prismatic | Fixed（类型保留，MDL-12/V12-01）
    rw::math::Vector3D<double> axis;            // 轴向（单位向量；权威一等字段 MDL-09）
    rw::math::Transform3D<double> origin;       // T_parent_joint（父连杆系→关节系，core §4.6 读法）
    SourcedValue<double> lower, upper;          // rad（Prismatic 为 m）；Continuous＝NotProvided（工作范围另载）
    SourcedValue<double> maxVelocity, maxAcceleration;   // rad/s、rad/s²（Prismatic 量纲对应）
    std::optional<WorkingRange> workingRange;   // Continuous 工程工作范围（有限区间，MDL-12；分析消费属性）
};
struct LinkDescription {           // 连杆
    std::string localName;
    std::optional<GeometryRef> visual, collision;   // 资源引用（§8.6；几何级权威值可覆盖估算，MDL-05）
    SourcedValue<double> mass;                     // kg
    SourcedValue<rw::math::Vector3D<double>> centerOfMass;   // 连杆系（惯量基准=质心/连杆参考姿态，M-2）
    SourcedValue<rw::math::InertiaMatrix<double>> inertia;   // 质心系张量（MDL-05 惯量基准）
    std::optional<std::string> material;           // 材料标识（估算来源用）
};
struct ToolDescription { std::string localName; std::optional<GeometryRef> geometry;
    SourcedValue<double> mass; SourcedValue<rw::math::Vector3D<double>> centerOfMass;
    SourcedValue<rw::math::InertiaMatrix<double>> inertia;
    rw::math::Transform3D<double> tcpOffset; };    // 法兰→TCP（KIN-14 默认 TCP 的权威来源）
struct SceneObjectDescription { std::string localName; rw::math::Transform3D<double> worldPose;
    GeometryRef geometry; };                       // 环境对象（世界系固连，MDL-15）
struct BasePlacementDescription {  // 基座布置（MDL-22）
    InstallationPresetToken preset;                // Ground | Inverted | Wall | Custom（§6.2）
    SourcedValue<rw::math::Vector3D<double>> customEaa;  // Custom 时必填（编辑表示；旋转本体存矩阵）
    rw::math::Vector3D<double> basePosition;       // 基座原点在世界系位置（m）
};
struct DrivetrainDescription {     // 传动（阶段 B 消费；MDL-21 矩阵为 R2/D）
    std::vector<SourcedValue<double>> ratioPerJoint;      // 对角传动比（OPT-02 StageB 连续变量）
    std::optional<CouplingMatrix> coupling;               // 线性耦合常矩阵 C＋适用关节范围（R2）
};
struct JointFrictionDescription {  // 摩擦（MDL-16；缺失走 DataInsufficient 降级——判定归 dynamics）
    SourcedValue<double> viscous, coulomb, bias; };       // fv、fc、偏置
struct RobotDesignDescription {
    std::uint32_t descriptionContractVersion;     // 契约版本（进入编译缓存键，§9.4）
    std::string robotLocalName;
    std::vector<JointDescription> joints;         // 串联顺序（基座→法兰）
    std::vector<LinkDescription> links;           // joints.size()+1（含基座连杆）
    std::vector<ToolDescription> tools;           // ≥0；首项为默认 TCP（MDL-13/04）
    std::vector<SceneObjectDescription> scene;
    BasePlacementDescription base;
    DrivetrainDescription drivetrain;
    std::vector<JointFrictionDescription> friction;   // 逐关节；可为空（全部 NotProvided）
    std::vector<ResourceRef> resourceRefs;            // 全部资源引用清单（§8.6）
};
class IRobotDesignReader {         // modeling 实现（阶段 B）；纯函数：同字节→同 Description
public:
    virtual ~IRobotDesignReader() = default;
    virtual Expected<RobotDesignDescription, RuntimeError>
        read(const std::vector<std::uint8_t>& objectBytes,
             std::uint32_t objectTypeFormatVersion) const = 0;
};
```

**单位纪律**：Description 中数值字段标注于注释（rad/m/kg/...）；调用方（reader 实现）负责在生成 Description 前经 core 唯一换算入口完成 SI 化（**CanonicalModel 内只有 SI 真值**，KIN-12/AT-27）；编译器对 Description 入口做抽样单位一致性复核不可行（值已无量纲），故单位错误在 reader 侧拒绝、在编译器侧以数值合法性（有限性、量纲期望）复核（§5.2 S3）。

### 4.3 CanonicalModel 字段表

结构级约定（适用于 §4.3 全部子结构，字段表不再逐列重复）：**版本**＝结构级 `canonical-model/<major>.<minor>`（当前 `canonical-model/1.0`；major 变化＝编码破坏性变更，走设计变更评审）；**可变性**＝全部不可变（无 setter，构造后只读，值语义拷贝/移动）；**所有权**＝CanonicalModel 实例由 RuntimeSnapshot 以值持有并随快照共享（下游只拿到 `const` 引用或快照 shared_ptr）；**身份语义**列标注该字段是否参与内容身份（§4.5）；**引用约束**＝跨对象引用一律 ObjectId（精确等值，附录 D 第 12 项），绝不以名称作引用。

#### 4.3.1 身份与来源块（`CanonicalModelHeader`）

| 字段 | 类型 | 必填 | 默认 | 身份语义 | 引用约束 | 合法与非法实例 |
| --- | --- | --- | --- | --- | --- | --- |
| `project / branch / revision` | core::ProjectId / BranchId / RevisionId | 是 | — | **入**（定位来源，保证可追溯） | 必须来自注入的修订视图 | 合法：闭包内修订 `rev-…`；非法：空 id 或闭包外修订 |
| `revisionSeq` | std::uint64_t | 是 | — | **不入**（仅展示排序；内容身份不依赖修订序号——CON-05 精神） | — | 合法：≥0 |
| `objectRefs` | std::vector\<ObjectRefEntry\> | 是（≥1） | — | **入**（经内容身份间接：参与编码） | 每 (oid,cv) ∈ revision 闭包（CM-0） | 合法：全部通过 objectInRevision；非法：混入其他修订版本（编译失败） |
| `descriptionContractVersion` | std::uint32_t | 是 | — | **入**（编译输入契约版本） | — | 合法：≥1 |
| `compilerContractVersion` | std::uint32_t | 是 | — | **入**（本编译器契约版本；同时进缓存键与 evidence Environment 条目） | — | 合法：≥1 |
| `builtFrom` | RobotDesignDescription 摘要 | 是 | — | **入**（Description canonical 字节摘要——reader 输出的身份） | — | 非空 Digest256 |

#### 4.3.2 世界与基座块（`WorldPlacement`；§6 详述）

| 字段 | 类型 | 必填 | 默认 | 身份语义 | 引用约束 | 合法与非法实例 |
| --- | --- | --- | --- | --- | --- | --- |
| `T_world_base` | rw::math::Transform3D\<double\> | 是 | 平移 0、旋转 I（＝地面安装默认，V15-04） | **入**（基座姿态修改＝模型身份变化＝下游依赖变化，MDL-22/AT-05） | 唯一存储位置（单字段；无第二副本） | 合法：R 正交（RᵀR=I，det=+1，逐元素容差 1×10⁻¹²）、t 有限；非法：非正交/反射矩阵/含非有限分量→`InputInvalid` |
| `installPreset` | InstallationPresetToken＋SourcedValue 来源 | 是 | Ground（未显式配置） | **不入身份**（`T_world_base` 已承载结果；preset 是来源记录） | — | 合法：`inverted` 且 R=R_x(π)；非法：preset=Custom 而 R=I（校验失败） |
| `gravityWorld` | rw::math::Vector3D\<double\> | 是 | (0,0,−9.81) m/s² | **入**（世界系重力恒定——DYN-01/MDL-22 口径；写入以显式值为准，不沿用 RobWork 默认） | 常量；规范值归 dynamics 消费侧复核 | 合法：有限非零；非法：全零/非有限 |

#### 4.3.3 机器人链块（`RobotChain`）

| 字段 | 类型 | 必填 | 默认 | 身份语义 | 引用约束 | 合法与非法实例 |
| --- | --- | --- | --- | --- | --- | --- |
| `robotObjectId` | core::ObjectId | 是 | — | **入**（规范对象身份锚） | ∈ objectRefs | 合法：`obj-…`；非法：不在闭包 |
| `robotLocalName` | std::string | 是 | — | **入**（进入 RuntimeNameMap 设备作用域名，MDL-14） | charset §7.2 | 合法：`IRB6700`；非法：空/含 `/` |
| `deviceName` | std::string | 是 | —（派生） | **入**（RobWork Device 名＝名称端口生成物） | ＝§7 生成规则输出 | 合法：与名称映射一致；非法：与映射不一致（一致性检查失败） |
| `joints` | std::vector\<CanonicalJoint\> | 是（≥1） | — | **入** | 串联序（i 的 parent＝i−1；基座连杆为 0） | 合法：6/7 轴全旋转（产品链口径归 modeling 门控；编译器机械支持 1..N） |
| `links` | std::vector\<CanonicalLink\> | 是 | — | **入** | links.size()==joints.size()+1 | 非法：数量失配→`StructureInvalid` |

`CanonicalJoint`（逐关节）：

| 字段 | 类型 | 必填 | 默认 | 身份 | 引用约束 | 合法与非法实例 |
| --- | --- | --- | --- | --- | --- | --- |
| `objectId` | core::ObjectId | 是 | — | **入** | ∈ objectRefs；全链唯一（重复→`StructureInvalid`） | 合法：关节对象；非法：与连杆/工具重复的 id |
| `localName` | std::string | 是 | — | **入**（进入名称映射与诊断定位） | 链内唯一（§7.2） | 合法：`joint_1`；非法：与兄弟同名且不可消歧 |
| `type` | JointType（Revolute/Continuous/Prismatic/Fixed） | 是 | — | **入**（类型保留不回写，V12-01） | — | 合法：`continuous`＋workingRange 有限；非法：`continuous` 无 workingRange 而进入正式计算（capability 降级而非硬失败，§5.6） |
| `axis` | Vector3D\<double\> | 是 | — | **入**（MDL-09 权威轴线） | 单位化（编译器规格化；零向量→`InputInvalid`） | 合法：任意有限非零轴（MDL-11：不要求为 Z）；非法：零向量/非有限 |
| `origin` | Transform3D\<double\>（T_parent_joint） | 是 | — | **入** | R 正交；t 有限 | 合法：任意刚体变换；非法：奇异旋转（§6.6 错误表） |
| `zeroOffset` | double（rad 或 m） | 是 | 0 | **入**（零位偏置：q_authoritative = q_zeroOffset + q_rw；RobWork 侧 q 从零起算） | — | 合法：有限；非法：非有限 |
| `bounds` | SourcedValue 意义下的 optional 区间 | 条件 | — | **入**（值与存在性都入身份） | Revolute/Prismatic 必填（qmin＜qmax，MDL-06④ 硬断言归 modeling，编译器复核一致性：qmin≥qmax→`InputInvalid`）；Continuous＝无（工作范围替代） | 合法：(−2.97, 2.97)；非法：qmin≥qmax、非有限、单位不符（rad 期望而值域像 deg→量纲抽样校验失败） |
| `workingRange` | optional 区间 | 条件 | — | **入** | 仅 Continuous；有限且 qmin＜qmax（MDL-12：分析消费属性，不回写权威模型） | 合法：continuous＋(−π, π)；非法：无限区间 |
| `maxVelocity / maxAcceleration` | SourcedValue\<double\>（SI） | 否 | NotProvided | **入**（含 NotProvided 状态本身） | 缺失→capability 降级（§5.6），非硬失败 | 合法：provided(2.5)；非法：provided(负数)→`InputInvalid` |
| `friction` | {fv, fc, bias} 三元 SourcedValue | 否 | 全 NotProvided | **入**（MDL-16；缺失→dynamics 侧 DataInsufficient 降级 DYN-06，runtime 只承载） | — | 合法：NotProvided 或有限正值 |

`CanonicalLink`（逐连杆）：

| 字段 | 类型 | 必填 | 默认 | 身份 | 引用约束 | 合法与非法实例 |
| --- | --- | --- | --- | --- | --- | --- |
| `objectId` / `localName` | core::ObjectId / std::string | 是 | — | **入** | 链内唯一 | 同关节口径 |
| `visual / collision` | optional ResourceRef | 否 | nullopt | **入**（资源以**内容摘要**入身份，路径不入——§8.6） | ResourceRef ∈ CanonicalModel.resourceManifest | 合法：已固化资源摘要；非法：引用清单外的资源 |
| `mass / centerOfMass / inertia` | SourcedValue 三元组 | 否 | 全 NotProvided | **入**（含状态） | 惯量基准＝质心、参考姿态＝连杆系（M-2）；物性断言（m＞0/SPD/三角不等式）判定归 modeling 处理器（MDL-06），编译器复核已提供值的合法性（非法→`InputInvalid`，阻止编译——与断言一致的就地拦截） | 合法：NotProvided（capability 降级）；合法：provided 且 SPD；非法：provided 且 m≤0 或张量非对称（对称性容差附录 D 第 6 项）非正定 |

#### 4.3.4 工具、场景与传动块

| 字段 | 类型 | 必填 | 默认 | 身份 | 引用约束 | 合法与非法实例 |
| --- | --- | --- | --- | --- | --- | --- |
| `tools` | std::vector\<CanonicalTool\> | 否 | {} | **入** | 工具几何不复制（MDL-13：引用 ToolDefinition 资源）；TCP 偏置＝法兰→TCP 变换 | 合法：tcpOffset 正交；非法：工具对象 ObjectId 重复 |
| `defaultTcp` | index 或 explicit | 是（有 tools 时） | — | **入** | KIN-14 经命令产生的新修订自然改变此字段 | 合法：指向 tools 内项 |
| `scene` | std::vector\<CanonicalSceneObject\> | 否 | {} | **入** | 环境对象显式引用（MDL-15）；worldPose＝世界系固连位姿（**不得**预乘安装旋转——§6.4 禁止项） | 合法：世界系位姿；非法：位姿非有限 |
| `drivetrain.ratioPerJoint` | 逐关节 SourcedValue\<double\> | 否 | NotProvided | **入** | 对角传动比（OPT-02 StageB） | 合法：正有限值 |
| `drivetrain.coupling` | optional CouplingMatrix{C(rows×cols 常矩阵), jointRange 适用边界, conditionNumber} | 否 | nullopt | **入** | MDL-21：C 须常矩阵（本类型只承载常矩阵——非常矩阵在建模侧就无法进入）；编译校验：方阵维度＝适用关节数、可逆、条件数 ≤1×10⁸（**设计默认，登记 P-RT-7 待策略侧确认归属**）——病态/奇异→`InputInvalid` 阻止编译并给比较型诊断（M-6/M-12） | 合法：良态可逆 C；非法：奇异（det≈0）/条件数超限/非方阵 |

#### 4.3.5 资源、诊断、能力与身份块

| 字段 | 类型 | 必填 | 默认 | 身份 | 引用约束 | 合法与非法实例 |
| --- | --- | --- | --- | --- | --- | --- |
| `resourceManifest` | std::vector\<ResourceRef\> | 是（可空集） | {} | **入**（每条含内容摘要） | §8.6 字段表；与 Description.resourceRefs 一致 | 合法：逐条摘要非零；非法：清单与引用失配 |
| `diagnostics` | std::vector\<core::DiagnosticRecord\> | 是（可空集） | {} | **不入**（诊断是编译过程记录，不影响模型内容；同输入同诊断由确定性保证） | 仅警告级（error 级＝编译失败，不进模型） | 合法：名称冲突消歧警告；非法：error 级记录出现在发布模型（构造拒绝） |
| `capabilities` | RuntimeCapability（§9.6） | 是 | — | **不入**（能力是内容的派生投影：由上述字段推导；写入便于下游直查） | 与字段一致性由构造器校验 | 合法：物性全 NotProvided→hasMassInertia=false；非法：hasMassInertia=true 而存在 NotProvided（构造拒绝） |
| `contentIdentity` | core::ContentIdentity | 是 | —（派生） | —（**即**身份本身） | ＝SHA-256 over RT-Codec(本模型除 diagnostics/capabilities/contentIdentity 外全部字段)，§4.5 | 非零 Digest256；builder 计算非调用方申报 |

#### 4.3.6 内容身份之外的等价关系（三种"等价"冻结）

| 等价类别 | 定义 | runtime 地位 |
| --- | --- | --- |
| 字节相同 | RT-Codec 规范编码逐字节相等（→摘要相等） | **CanonicalModel 内容身份的唯一等价关系**（与 evidence §5.1 同口径；比较用 core 精确等值） |
| 语义等价 | 编码不同但工程语义相同（如 SourcedValue 来源标记不同而数值相同） | **不参与身份**：来源标记入身份（报告呈现需要区分），故语义等价的两实例身份不同——保守方向（宁可重算不错复用）；显示性字段（描述/备注/预设 token）不入身份，其变化**不产生**新身份 |
| 数值容差内等价 | \|a−b\| ≤ ε_rel·\|ref\|＋ε_abs（附录 D C4） | **只用于数值校验断言**（FK 等价验证〔附录 D 第 4 项〕、旋转正交性检查、耦合条件数）；**严禁**作为身份/缓存键等价关系（浮点近似无传递性；RT-ID-2 用例钉住） |

**显示名称变化与模型身份**：RobotDesign 对象的**显示名/描述/备注/3D 呈现属性**不入 Description、不入 CanonicalModel、不入身份——改名不触发任何重算（与 evidence §5.3"项目显示名"行同源）。**关节/连杆/机器人 localName 是解析字段**（进入 RuntimeNameMap 与模型身份）——重命名＝设计变更＝新内容版本＝新 CanonicalModel 身份＝RuntimeNameMap 重建；但下游持久引用一律 ObjectId（ARC-04），重命名不破坏既有对象引用（AT-18 往返继续成立）。

### 4.4 单位与数值纪律（CanonicalModel 内只有 SI 真值）

- 一切字段 SI：m、rad、kg、s、N·m、kg·m²（core §4.6 约定；KIN-12 显示单位是纯投影，不进入本模型）。
- 非有限值（NaN/±Inf）在 Description→CanonicalModel 构造边界拒绝（`InputInvalid`＋定位到对象/字段，NFR-COR-03：不得静默转 0）。
- SourcedValue 四态保留到 CanonicalModel（物性/摩擦/限速缺失是**能力缺失**不是非法，§5.6 正交表）。
- 单位一致性抽样校验（§5.2 S3）：旋转关节限位量级复核（|q|>4π×10 等异常量级给警告——**不阻断**；硬单位错误由 reader 侧换算入口拒绝）。

### 4.5 RT-Codec：canonical 编码与内容身份

（core §4.2 责任边界的 runtime 侧承接：core 提供"字节→摘要"，"对什么字节做摘要"归本文。）

| 规则 | 内容 | 依据 |
| --- | --- | --- |
| 编码形态 | 确定性二进制：magic `IRDCANO`＋结构版本号＋字段按**声明序**（集合类先按稳定键排序：对象按 ObjectId 规范文本字典序、关节/连杆按链序）＋长度前缀；大端；无填充 | 跨进程一致（main/worker 同身份，NFR-COR-02） |
| 可选值 | presence 字节显式编码（nullopt ≠ 零值）；SourcedValue 状态字节＋值 | NFR-COR-03（缺失不伪造） |
| 数值 | IEEE754 双精度 8 字节位模式（round-trip 精确）；NaN/±Inf 编码入口拒绝 | 同 evidence D-06 论证 |
| 旋转矩阵 | 9 分量按行主序位模式编码（表示无关：EAA/欧拉是编辑表示，只在 Description 侧，CanonicalModel 只存 Rotation3D） | 消除表示歧义 |
| 身份域 | `contentIdentity`＝SHA-256 over（身份字段全集，**排除** diagnostics、capabilities、contentIdentity 自身、revisionSeq、installPreset 来源标记） | 身份＝内容；过程记录与派生投影不入 |
| 往返 | `parse(encode(x))==x`（全字段含资源摘要）；编码器版本入编码头并进缓存键 | 编码升版＝全体身份变化（破坏性，走设计变更评审） |
| 线程/确定性 | 纯函数、可重入、无 I/O、无隐藏状态 | ARC-03 |

### 4.6 运行时对象身份与路径索引

CanonicalModel 内维护三类只读索引（builder 构建，O(1)/O(log n) 查询）：

1. **对象索引**：ObjectId → {kind: Robot/Joint/Link/Tool/SceneObject/Resource, 链上位置}——名称映射与诊断定位的基础；
2. **层级索引**：Frame 路径（规范侧树：World→BaseMount→Base→Joint₁→…→Flange→Tool/TCP；Scene 对象挂 World）——供 WC 编译时按同构树写入、一致性检查时逐节点对照；
3. **资源索引**：resourceId → ResourceRef——资源复查（§8.6）的枚举来源。

---

## 5. 确定性编译链

### 5.1 编译链总览与流程图

```
RobotDesign（项目对象，modeling schema，经 IRobotDesignReader）
   │  注入：IObjectBytesSource / IRevisionClosureSource（project ②端口适配）
   ▼
┌────────────────────────── runtime 确定性编译（ICanonicalModelCompiler，纯变换）──────────────────────────┐
│ S1 修订只读视图锚定 ─► S2 规范模型解析 ─► S3 结构与单位校验 ─► S4 资源读取与完整性校验                    │
│      │                     │                    │                        │                                │
│      ▼                     ▼                    ▼                        ▼                                │
│ S5 CanonicalModel 构造（RT-Codec 身份计算）                                                              │
│      ▼                                                                                                  │
│ S6 WorkCell 编译 ─► S7 DynamicWorkCell 编译（能力门控：物性齐备才构造）                                    │
│      ▼                                                                                                  │
│ S8 RuntimeNameMap 建立 ＋ WC/DWC 对象名交叉校验                                                          │
│      ▼                                                                                                  │
│ S9 基座—世界一致性检查（WC 实际变换 vs CanonicalModel 字段，附录 D 第 4 项容差）                           │
│      ▼                                                                                                  │
│ S10 运行时快照发布（RuntimeSnapshot，原子交接；此前一切产物对外不可见）                                     │
└──────────────────────────────────────────────────────────────────────────────────────────────────────┘
   产物绑定：snapshot.modelIdentity＝CanonicalModel.contentIdentity（＝输入内容＋编译器契约的纯函数）
```

输入（CompileRequest）：`RevisionId（或预解析的 RevisionSummary）＋IObjectBytesSource＋IRevisionClosureSource＋IRobotDesignReader＋IRuntimeResourceProvider＋CompileOptions＋ICompileCancelToken*`。输出（CompileOutcome）：`status{Published|Failed|Cancelled} ＋ shared_ptr<const RuntimeSnapshot>（仅 Published）＋ diagnostics 全量`。

### 5.2 十段编译逐步表

| 段 | 输入 | 输出 | 失败条件（诊断码） | 可恢复 | 临时对象 | 允许部分结果 | 可入缓存 | 可供下游 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| S1 锚定修订 | RevisionId | RevisionSummary（一次读取，之后一切解析只对该视图） | 修订不存在/闭包外（`ContextReleased`/`UnknownObject`）；上下文已关闭 | 是（换修订重试） | 无 | 否 | — | 否 |
| S2 规范模型解析 | objectRefs 中 robot-design 对象字节＋IRobotDesignReader | RobotDesignDescription | reader 失败（`InputInvalid`，含定位）；(oid,cv) 不在闭包（`StructureInvalid`——防混入 CM-0） | 是（修复输入后重编译） | Description 值 | 否 | 否 | 否 |
| S3 结构与单位校验 | Description | 校验通过或诊断集 | 链断裂/重复 ObjectId/数量失配（`StructureInvalid`）；零轴/非有限/反射旋转/qmin≥qmax/m≤0/非 SPD（`InputInvalid`，逐条定位对象与字段）；耦合矩阵奇异/病态（`InputInvalid`＋比较型诊断，MDL-21/M-6） | 是（建模侧修复） | 无 | 否 | 否 | 否 |
| S4 资源读取与完整性校验 | resourceManifest＋IRuntimeResourceProvider | 已验证资源字节集（摘要一致） | 资源缺失（`ResourceMissing`＋resourceId）；摘要不符（`ResourceChanged`）；预算超限（`ResourceBudget`）——检测与原始诊断归 io，runtime 定位与转译 | 是（重关联/固化后重编译，PM-09/CON-03） | 资源字节缓存（编译期内） | 否 | 否 | 否 |
| S5 CanonicalModel 构造 | Description＋资源引用＋身份块 | CanonicalModel（含 contentIdentity、capabilities、警告级 diagnostics） | 构造不变量违反（`InputInvalid`）——能力缺失在此降级为 capability 而非失败（§5.6） | 是 | 模型值 | 否 | **CanonicalModel 层缓存键成立**（§9.4） | 否（尚未发布） |
| S6 WorkCell 编译 | CanonicalModel | rw::models::WorkCell（Frame 树/SerialDevice/Joint/限位/几何）＋设备名 | RobWork 异常/空句柄/层级无效（`WorkCellCompileFailed`，§8.4 转译）；`T_world_base` 非法旋转（`InputInvalid`） | 是（同输入重试结果一致） | WC＋Frame/Joint/Geometry 对象（瞬态，失败即弃） | 否 | WC 层缓存键成立 | 否 |
| S7 DynamicWorkCell 编译 | CanonicalModel＋WorkCell | rwsim DynamicWorkCell（Body/RigidDevice/质量惯量/摩擦） | **能力门控**：任一被消费 Body 的物性 NotProvided→**跳过构造**（capability.hasDynamicWorkCell=false，警告诊断，不算失败）；物性已提供但构造失败（`DwcCompileFailed`，含 RobWork 异常转译） | 是 | DWC 对象 | 否 | DWC 层缓存键成立（跳过状态也入键——§9.4） | 否 |
| S8 RuntimeNameMap 建立 | CanonicalModel | RuntimeNameMap＋内容身份；对 WC/DWC 实际对象名逐项交叉校验 | 名称非法化失败/不可消歧冲突（`NameConflict`）；WC 对象名与映射不一致（`NameConflict`——如 RobWork 内部改名导致双前缀/旧名，MDL-14 反例） | 是 | 映射值 | 否 | 映射内容身份入键 | 否 |
| S9 基座—世界一致性检查 | CanonicalModel＋WC | 通过或诊断 | WC 内 BaseMount 变换与 `T_world_base` 超出附录 D 第 4 项容差（`BaseWorldInconsistent`）；检测到二次叠加（实际变换≈T·T，AT-37 反例） | 否（实现缺陷类失败——须修复编译器，不得放行） | 无 | 否 | 否 | 否 |
| S10 快照发布 | 全部产物 | RuntimeSnapshot（shared_ptr<const>，原子交接） | 仅因发布后置校验失败（防御性；理论不触发） | — | — | 否 | **完整快照缓存键成立**（§9.4） | **是**（唯一对外发布点） |

**任一阶段失败不得发布半成品**（MDL-06 原子性）：失败时 S6/S7 的 RobWork 对象全部析构（见 §8.7 销毁顺序），诊断全量返回（不短路——与 ERR-01"全量列出"同精神）；**编译期间不修改项目历史**——编译输入全部经只读注入接口，零写路径（§2.3）。命令路径（project 命令服务 S5 双编译）经 L5 适配器调用本编译器：任一失败→`compile-failed`→不提交修订（project.md §5.3.6 语义，由其编排保证；runtime 提供整体 CompileOutcome）。

### 5.3 编译事务状态机（失败回滚）

```
 Idle ──start()──► Anchored(S1) ──► Parsed(S2) ──► Validated(S3) ──► ResourcesReady(S4)
                                                                      │
   ┌──────────────────────── 任一段失败（诊断码留痕）───────────────────┤
   │                                                                  ▼
 RollingBack ◄──失败/取消── CanonicalBuilt(S5) ─► WorkCellCompiled(S6) ─► DynReady(S7: Compiled|SkippedByCapability)
   │                                                                          │
   │                                                                          ▼
   │                                              NameMapBuilt(S8) ─► ConsistencyChecked(S9) ─► Published(S10)
   ▼
 RolledBack（临时对象全部析构、资源缓存清空、无任何对外可见产物）
   ├─失败──► CompileOutcome{Failed, diagnostics}          （不产生快照；不产生修订）
   └─取消──► CompileOutcome{Cancelled, 诊断（正常取消非错误，UX-03）}
```

回滚规则：RAII 逐段清理（编译器内部 `CompileTransaction` 对象持有全部瞬态产物）；**无磁盘副作用**（分析路径零写；命令路径的 `.staging/tmp/` 临时文件归 project 事务清理，project.md §6.6）。重复进入 RollingBack 幂等；发布后（Published）不再有失败路径（防御性后置校验失败→开发诊断＋快照立即作废——理论不触发）。

### 5.4 临时对象与资源复查（编译期间资源被替换的检测）

- 编译内临时对象：Description 值、资源字节缓存、WC/DWC RobWork 对象、NameMap 值——全部由 CompileTransaction 持有，失败/取消即析构。
- **资源替换检测**（NFR-REL-04 runtime 切面）：S4 读取时记录每资源摘要；**S10 发布前对已被消费的资源复查摘要**（IRuntimeResourceProvider 复读）——任一不符→`ResourceChanged`→整体失败（防止"读取时刻与发布时刻之间被替换"的窗口）。已固化（Solidified，项目内不可变副本）资源免复查（存储不可变保证）。资源缓存仅编译期内存活，绝不跨编译复用（防陈旧）。

### 5.5 可重入、重复编译与稳定性

| 性质 | 设计 |
| --- | --- |
| 可重入 | 编译器实例无共享可变状态；同一编译器对象可被多线程并发调用（每次调用独立 CompileTransaction）；全部注入接口约定并发只读安全 |
| 同输入重复编译 | CanonicalModel.contentIdentity 相等；RT-Codec 字节相等；WC/DWC 结构相等（Frame 树同构同名、Device 同名、bounds/限速逐关节相等——附录 D 第 4 项容差断言）；RuntimeNameMap 及其内容身份相等（AT-16/18 契约测试 RT-EQ-\*） |
| 稳定名称 | NameMap 生成只依赖 CanonicalModel（排序与消歧规则确定性，§7.3）——重复编译名称逐字节稳定 |
| 无隐藏状态 | 不读环境变量/时钟/locale/文件系统（资源经注入接口）；日志走诊断收集器非全局流 |
| 取消 | 每段边界与资源逐项循环检查 ICompileCancelToken；取消→RollingBack→Cancelled（2 s 协作窗由 execution 通道保证，runtime 只保证边界检查密度）；**取消不产生部分快照** |
| 超时/内存不足 | 编译本身不设内部超时（由 execution 通道取消令牌承载）；`std::bad_alloc`→`Failed`＋`ResourceBudget` 诊断（对象级清理由 RAII）；资源预算超限由 provider 拒绝（io BudgetGuard），编译器转译 `ResourceBudget` |

### 5.6 能力缺失、输入非法、编译失败的正交关系

| 维度 | 输入非法（InputInvalid） | 能力缺失（Capability） | 编译失败（Compile Failed） |
| --- | --- | --- | --- |
| 判定对象 | 输入数据本身违反硬规则（零轴/非有限/反射旋转/qmin≥qmax/m≤0/非 SPD/耦合奇异/引用不在闭包） | 输入**合法但未提供**某类数据（物性/摩擦/限速 NotProvided、无场景、无工具、无 DWC） | 编译过程无法完成（RobWork 异常、一致性检查失败、资源失败） |
| 后果 | **该次编译失败**、就地定位非法对象（与 modeling 断言同口径 MDL-06） | **快照照常发布**＋capability 声明 false＋**警告级**诊断 | 编译失败、无快照、error 级诊断 |
| 与工程判定关系 | 无工程结论（输入未完成，REQ-06 口径归 requirements/evidence） | **绝不**升级为工程不可行——下游按声明决定：dynamics→DataInsufficient（DYN-06）、kinematics 不受影响、reporting 标注缺项 | 无工程结论（执行失败轴，TASK-02） |
| 诊断级别 | error（阻断） | warning（不阻断） | error（阻断） |
| 典型反例 | m=−1（provided 而非法）≠ m 未提供（NotProvided→能力缺失）——两者严格区分（RT-CAP-2 用例） | — | — |

正交组合合法性表（发布门禁用）：

| 编译状态 | 能力 | 诊断 | 合法组合 |
| --- | --- | --- | --- |
| Published | 全齐备 | 无警告 | ✔（完整快照） |
| Published | 有缺失项 | 对应警告逐项 | ✔（降级快照——下游按能力声明处置） |
| Published | 任意 | 存在 error 级 | ✘（构造拒绝：有 error 即不得 Published） |
| Failed | —（未发布，能力块无意义） | error 全量 | ✔ |
| Cancelled | — | 取消诊断（非 error，UX-03） | ✔ |

### 5.7 后台线程与 RobWork 对象线程归属

- 编译**允许**在命令执行线程或后台线程执行（ARCH §4.2：编译类命令可与界面并行）；编译器对调用线程无亲和要求（无 TLS、无 GUI 假设）。
- RobWork 对象线程归属（基线事实约束，§8.7 详述）：WC/DWC 构造与修改只在编译线程；**发布后只读共享**——WorkCell/Device/Joint 结构查询并发只读安全（无内部可变状态；RobWork 惯例），`rw::kinematics::State` 为每线程独立副本（§9.2 makeState）。
- 跨线程传递边界：跨线程只允许传递 `shared_ptr<const RuntimeSnapshot>`（不可变值＋只读视图）；**禁止**跨线程传递裸 Frame*/Device* 或可变 State（§8.3 视图封装强制）。
- 内存预算：execution 的 ResourceController 信号经 ICompileCancelToken 扩展（预算超限→取消语义）；runtime 不自建资源治理（NFR-PERF-04 归 execution）。

---


## 6. 基座—世界变换单一不变量

### 6.1 坐标系定义与变换关系图

| 坐标系 | 定义 | 固连对象 |
| --- | --- | --- |
| 世界系 W | 与地面固连的固定系；重力恒定 g_W | 项目安装环境（MDL-15 环境几何、三维视图地面） |
| 项目基座安装系 | 基座安装面参考（工作台/吊装梁），平移分量来源 | 项目布置（basePosition） |
| 机器人基座系 B | 机器人安装底面坐标系（Device base frame） | 机器人本体 |
| 连杆/关节系 | 逐关节（T_parent_joint 链） | 连杆 |
| 法兰系 F | 末轴输出端（Device end frame） | 机器人本体 |
| 工具坐标系 T（TCP） | 工具参考点（tcpOffset 定义于 F） | 工具（MDL-13） |

```
                ┌─────────── 唯一存储：CanonicalModel.WorldPlacement.T_world_base ───────────┐
                │                                                                            │
 世界系 W ═════════ T_world_base ══════════► 机器人基座系 B ═ T_B_J1(q1) ═► J1 ═ … ═► 法兰 F
   │  ▲          （旋转 R_world_base＝安装姿态 MDL-22；                                        │
   │  │            平移＝基座布置）                                                            └═ T_F_tcp（工具偏置）═► TCP
   │  │  T_base_world ＝ inverse(T_world_base)
   │  └── 场景对象（世界系固连位姿，绝不预乘安装旋转）
   ▼  g_W＝(0,0,−9.81) m/s²（恒定；动力学消费 g_B＝R_world_baseᵀ·g_W）
```

**乘法顺序与方向**（core §4.6 冻结读法：`T_ab`＝"b 系相对 a 系的位姿"，`p_a = T_ab · p_b`，组合 `T_ac = T_ab · T_bc`）：

| 公式 | 方向与含义 |
| --- | --- |
| `T_world_base`（正向） | 基座系位姿在世界系中的表达；`p_world = T_world_base · p_base` |
| `T_base_world = inverse(T_world_base)`（反向） | 世界系位姿在基座系中的表达 |
| `T_world_tcp(q) = T_world_base · T_base_tcp(q)` | 世界系→TCP 正向运动链；`T_base_tcp(q)`＝Device 基座到 TCP 的 FK（RobWork 设备坐标系内） |
| `g_base = R_world_baseᵀ · g_world` | 重力向基座系投影（DYN-01 消费；**不在基座系参数化 g**——MDL-22 实现口径） |
| 单位 | 平移 m、角度 rad（SI 真值；安装姿态编辑用欧拉角只是 modeling 的编辑表示，CanonicalModel 只存 Rotation3D） |

### 6.2 安装预设的精确定义（可执行）

| 预设 token | 旋转矩阵 R_world_base | 语义（基座 Z 轴指向） | 来源 |
| --- | --- | --- | --- |
| `ground` | I（单位阵） | +Z_W（竖直向上） | MDL-22 默认（未显式配置即此值——V15-04：含 URDF/Xacro 导入与空白模板路径） |
| `inverted`（倒挂 180°） | R_x(π)＝diag(1,−1,−1) | −Z_W（竖直向下，吊装） | 本文定义（**轴向选择登记 P-RT-4**：MDL-22 只定义"180°"未指明绕轴；本文按"基座 Z 反向"取 R_x(π)，与 modeling 编辑器对齐后冻结） |
| `wall`（壁/侧装 90°） | R_y(π/2)（第 1 行 [0,0,1]，第 3 行 [−1,0,0]） | 水平 | 同上（P-RT-4） |
| `custom` | 任意欧拉角编辑结果（R 由 modeling 换算，CanonicalModel 直存矩阵） | 任意 | MDL-22 |

预设矩阵元素全为 {0,±1}——编码无舍入。preset 与 R 的一致性在 S5 构造校验（preset≠ground 而 R=I→`InputInvalid`）；**preset token 不入身份、R 入身份**（§4.3.2）。

### 6.3 唯一存储位置与唯一写入点

| 环节 | 规则 |
| --- | --- |
| 规范模型唯一存储 | `CanonicalModel.WorldPlacement.T_world_base` 单字段（§4.3.2）——全产品不存在第二处安装变换定义（CON/ARC 层红线：不新增第二套基座变换规则） |
| 编译到 WorkCell 的唯一写入点 | S6 在世界帧与 Device 基座之间构造**单个** FixedFrame（运行时名 `<RobotScope>.BaseMount`，经名称映射登记），其 transform 一次性置为 `T_world_base`；此后编译器任何阶段不再改写；**其他一切 Frame 变换均来自关节链/工具偏置/场景位姿，不含安装分量** |
| 消费读取 | 四类消费方（下表）一律经 `IRuntimeModelView::worldToBase()/baseToWorld()/gravityWorld()/gravityBase()` 读取同一编译产物；**禁止各自从 RobotDesign 或别处另取安装姿态** |

### 6.4 四类消费者的读取契约与禁止清单

| 消费方 | 读取方式 | 典型用途 | 依据 |
| --- | --- | --- | --- |
| kinematics | `worldToBase()`＋Device FK（自备 per-thread State） | 世界系 FK/TCP/雅可比、IK 目标表达 | ARC-03、KIN-01 |
| trajectory | 同 kinematics（路径点/连续变换在 `T_world_base` 之后的设备链上表达） | 笛卡尔段、避障、时间参数化 | TRJ-01～04 |
| dynamics | `gravityBase()`（＝R_world_baseᵀ·g_W）＋DWC 设备 | RNEA 重力投影、正动力学 | DYN-01/02、AT-37 |
| policy（碰撞） | 环境几何＝世界系固连（CanonicalModel.scene 直接编译，**不旋转**）＋设备几何经同一 WC | 共享碰撞评估（经 ARC-05 唯一实现） | MDL-22/M-11、NFR-COR-05 |

**禁止清单**（契约测试锁定，RT-BW-\*）：

1. 禁止下游对 `worldToBase()` 结果再左乘/右乘任何安装旋转（二次叠加）；
2. 禁止在基座系参数化重力向量（"倒挂就令 g_base 取反"之类的就地翻转——必须经 R_world_baseᵀ 投影）；
3. 禁止环境/场景几何单独按安装姿态旋转（环境固连世界系；只有机器人链带着 T_world_base）；
4. 禁止显示层把"视图旋转"写回任何模型字段（显示坐标变换与计算坐标变换隔离：会话相机/视图变换只存在于 ui 会话态，ARCH §7.7 口径）；
5. 禁止第二处安装姿态存储（任何单元持有自己的 `R_world_base` 副本用于计算）。

### 6.5 最小数值验证例（契约测试 RT-BW-1 数据）

```
倒挂预设：R_world_base = R_x(π) = diag(1, -1, -1)；t_world_base = (0, 0, 2.0) m（吊装高度）

① 点变换：p_base = (0.3, 0.2, 0.5) m
   p_world = R·p_base + t = (0.3, -0.2, 2.0-0.5) m = (0.3, -0.2, 1.5) m      ✔ Z 分量反号
② 反向：T_base_world = inverse(T_world_base)（Rᵀ=R；t' = -Rᵀt = (0,0,-2.0)）
   p_base' = T_base_world · p_world = (0.3, 0.2, 0.5) m                      ✔ 与输入逐位一致
③ 重力投影：g_world = (0, 0, -9.81) m/s²
   g_base = Rᵀ·g_world = (0, 0, +9.81) m/s²
   —— 基座系中重力沿 +Z_base（基座 Z 指向世界 −Z）：倒挂静态重力矩符号由 RNEA 据此正确产生（AT-37 联动 DYN-01 解析算例）
④ FK 组合：T_world_tcp(q) = T_world_base · T_base_tcp(q)（结合律断言：三因子两种结合顺序结果
   逐元素一致，容差＝附录 D 第 4 项 1×10⁻⁹ m / 1×10⁻⁹ rad）
```

### 6.6 未显式设置与错误表达

| 情形 | 行为 | 诊断码 |
| --- | --- | --- |
| 世界坐标系/安装姿态未显式配置（含 URDF/Xacro 导入、空白模板） | 默认地面安装：R=I、t=basePosition（未布置则 0）——**确定性默认，非错误**；来源标记 NotProvided 按 Ground 解析（V15-04） | 无（正常） |
| 非法旋转（非正交/反射/行列式 −1） | S3 拒绝（正交性检查：max|RᵀR−I| ≤ 1×10⁻¹²，附录 D 第 6 项对称性容差同尺度） | `InputInvalid`（定位对象＋实测偏差值的比较型诊断） |
| 奇异姿态（欧拉角→矩阵转换产生非有限分量等） | reader 侧拒绝；S3 复核矩阵有限性 | `InputInvalid` |
| 单位错误（basePosition 量纲异常） | reader 换算入口拒绝（core 唯一换算）；S3 量级抽样警告 | `UnitMismatch` |
| 循环 Frame 引用 | 层级索引构建时 DFS 检测（规范侧树深度有限：链长＋场景数，§4.6） | `StructureInvalid` |
| 重复应用基座变换（下游二次叠加） | S9 检测：WC 实际 BaseMount 变换与 T 不符（含 ≈T·T 形态） | `BaseWorldInconsistent`（实现缺陷类失败，不得放行） |

**AT-37 反例观测点**：构造倒挂机型，若某消费方（替身实现模拟）自行再乘 R_x(π)：R²＝I——渲染回正、重力矩符号翻转。契约测试断言：四类消费方（kinematics/trajectory/dynamics/policy 替身）从 `IRuntimeModelView` 取得的 `worldToBase()` 与快照 `T_world_base` 逐元素一致（附录 D 第 4 项容差），且消费端 FK 结果等于 `T_world_base · FK_device`——任何二次叠加实现使等式失败（RT-BW-4/5）。

### 6.7 基座姿态修改的依赖传播

基座姿态变化→RobotDesign 新内容版本→CanonicalModel.contentIdentity 变化（T_world_base 入身份）→（evidence 切片"model.robot-design"条目变化）→运动学/轨迹/动力学及实际依赖下游 Superseded（AT-05 矩阵由 evidence 按切片传播；runtime 只保证身份敏感）。显示层"倒挂渲染预览"（未应用的草稿）不进入本链——草稿不产生 CanonicalModel（§4.1 与 evidence"预览输入不是快照"同口径）。

---

## 7. RuntimeNameMap 与名称解析

### 7.1 命名模型与范围

运行时名称＝**RobWork 设备作用域全名** `RobotScope.LocalName`（MDL-14 原文口径），其中 `RobotScope`＝机器人设备名（由映射从 `robotLocalName` 生成）。映射是 **ObjectId ↔ RuntimeName** 的双向单射（同快照内；附录 D 第 12 项精确等值，无容差）。

| NameScope（名称范围） | 覆盖对象（规范侧 kind） | 运行时名称形态示例 | RobWork 对端 |
| --- | --- | --- | --- |
| Device | robot | `IRB6700`（设备名本身） | SerialDevice 名 |
| Joint | joint | `IRB6700.joint_3` | Joint Frame 名 |
| LinkFrame | link | `IRB6700.link_2` | 连杆 FixedFrame 名 |
| BaseMount | robot（派生节点） | `IRB6700.BaseMount` | 安装 FixedFrame（§6.3 唯一写入点） |
| BaseFrame | robot/link[0] | `IRB6700.Base` | Device base frame 名 |
| Flange | link[N] | `IRB6700.Flange` | Device end frame 名 |
| Tcp | tool | `IRB6700.tcp` / `IRB6700.gripper_tcp` | TCP FixedFrame 名 |
| Geometry | link/tool/scene 的几何资源 | `IRB6700.joint_3.collision` / `.visual`；场景 `Scene.<name>.collision` | Geometry/Drawable 挂接标识 |
| SceneObject | scene | `Scene.<localName>`（世界系固连对象） | FixedFrame 名 |
| Body（动力学） | link | `IRB6700.link_2.body`（DWC 存在时） | rwsim Body 名 |
| Sensor | （阶段 C+ 若有） | 范围 token 预留 `sensor`；R1 无实例不预建映射规则 | rwsim Sensor 名 |

版本与命名空间：映射整体携带 `nameMapRuleVersion`（生成规则版本）与 `nameMapContentIdentity`（内容身份——CON-06 消费）；名称只在**单快照命名空间**内保证单射——跨项目/跨快照不存在名称等值含义（不同项目相同显示名各自生成映射，互不冲突——RT-NM-4 正例"不同项目相同显示名称不发生名称冲突"）。

### 7.2 生成规则（确定性）

| 规则 | 内容 |
| --- | --- |
| 合法字符集 | `[A-Za-z0-9_.-]`（RobWork Frame 名安全集合；**不含** `/`、空格、Unicode） |
| 合法化 | 非法字符逐个替换为 `_`；前导数字加前缀 `n_`；空名→`unnamed`＋警告 |
| 唯一化（消歧） | 同范围内合法化后同名：按 **ObjectId 规范文本字典序**排序，首个保留原名、后续追加 `_2`、`_3`…（序号按排序稳定分配）——同输入同结果；每例产警告级诊断（列出原名/消歧名/对象） |
| 不可消歧 | 消歧后仍冲突（防御性——ObjectId 唯一保证理论不可达）→`NameConflict` 编译失败 |
| 大小写 | 保留原大小写；比较**大小写敏感**（精确等值；与 RobWork findFrame 行为一致） |
| 稳定排序 | 映射内部条目按 (scope, localName, ObjectId) 字典序存储——序列化与遍历确定 |
| 保留字 | 最小保留集 `WORLD`（世界帧名）——localName 为 `WORLD` 时直接消歧加后缀 |

**生成时点**：S8 从 CanonicalModel 生成（不读 WC——保证映射规则可独立测试）；随后对 WC/DWC 实际对象名**逐项交叉校验**（编译器写入 WC 的名字必须与映射输出逐一相等——检测"双前缀/旧机械臂名/遗漏"，MDL-14 验收口径）。

### 7.3 接口（含任务指定的四个入口）

```cpp
// NameMap.hpp
enum class NameScope { Device, Joint, LinkFrame, BaseMount, BaseFrame, Flange,
                       Tcp, Geometry, SceneObject, Body, Sensor };
struct RuntimeName {                      // 值类型：全名及其分解
    std::string fullName;                 // "RobotScope.LocalName"（Geometry 叠加 ".collision" 等后缀段）
    std::string scopeToken;               // 机器人设备名或 "Scene"
    std::string localName;                // 消歧后的局部名
    NameScope   scope;
    bool operator==(const RuntimeName&) const noexcept;   // 精确等值（附录 D 第 12 项）
};
using RuntimeNameView = std::string_view; // 解析入口：完整名字符串
struct ObjectRef {                        // 解析产物：对象身份＋范围语义
    core::ObjectId objectId;
    NameScope      scope;
    std::string    localName;             // 规范侧权威 localName（消歧前）
};
struct RuntimeResolveError { RuntimeErrorCode code; std::string detail;   // unknown-object 等
                             std::string requestedName; };
struct RuntimeNameError   { RuntimeErrorCode code; std::string detail;
                            core::ObjectId requestedId; };

class RuntimeNameMap {
public:
    // 查询（全部 const、并发只读安全、确定性、无副作用、不抛——Expected 返回）
    Expected<ObjectRef, RuntimeResolveError> resolveRuntimeName(RuntimeNameView name) const noexcept;
    Expected<RuntimeName, RuntimeNameError>  resolveObjectId(core::ObjectId id) const noexcept;
    std::vector<ObjectRef>                   objectsInScope(NameScope) const noexcept;  // 稳定排序
    core::ContentIdentity                   contentIdentity() const noexcept;           // CON-06 消费
    std::uint32_t                           ruleVersion() const noexcept;
};
RuntimeNameMap buildRuntimeNameMap(const CanonicalModel& model);   // §7.2 规则；纯函数
```

接口属性表：

| 接口 | 前置 | 后置 | 错误 | 线程 | 确定性 | 写权限 | 副作用 | 生命周期/所有权 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `buildRuntimeNameMap(model)` | model 已通过 S5 构造（身份有效） | 完整映射；同 model 重复调用产出逐字节相等映射与相等内容身份 | 消歧失败→抛 `RuntimeError(NameConflict)`（构造期） | 可重入 | 是（只依赖 model 内容） | 无（纯函数） | 无 | 返回值；随快照持有 |
| `resolveRuntimeName(name)` | name 非空（空串返回错误，不抛） | 命中→ObjectRef（单射唯一）；未命中→错误 | `UnknownObject`（含原名回显）；空串/非法句法同码 | 并发只读 | 是 | 只读 | 无 | 映射随快照存活 |
| `resolveObjectId(id)` | id 合法 | 命中→RuntimeName；未命中→错误 | `UnknownObject`（含 id 规范文本） | 同上 | 是 | 只读 | 无 | 同上 |
| `objectsInScope(scope)` | — | 该范围全部对象（稳定排序） | — | 同上 | 是 | 只读 | 无 | 同上 |

调用示例与合法/非法调用：

```cpp
auto r = nameMap.resolveRuntimeName("IRB6700.joint_3");
if (r.ok()) { core::ObjectId joint = r.get().objectId; /* 持久引用一律 ObjectId */ }
// 合法：对快照内名称查询；结果 ObjectId 进入引用。
// 非法：业务单元自行拼接 "IRB6700." + name（R-4 红线——前缀拼接唯一合法位置是
//       buildRuntimeNameMap 实现内部，登记例外清单）；对空串断言命中；跨快照混用映射与名称。
```

**IRuntimeNameResolver（⑥名称端口形态，供不持快照的调用方）**：

```cpp
class IRuntimeNameResolver {          // 实现＝绑定某快照的映射；execution 接纳/诊断定位消费
public:
    virtual ~IRuntimeNameResolver() = default;
    virtual Expected<ObjectRef, RuntimeResolveError> resolveRuntimeName(RuntimeNameView) const = 0;
    virtual Expected<RuntimeName, RuntimeNameError>  resolveObjectId(core::ObjectId) const = 0;
    virtual core::ContentIdentity nameMapIdentity() const = 0;   // 接纳前核对结果绑定的映射身份
};
```

### 7.4 AT-18 双向往返验证（强制）

```
                 buildRuntimeNameMap(CanonicalModel)
                              │（双射：同快照内一一对应）
   ObjectId ──────── resolveObjectId ────────► RuntimeName
      ▲                                         │
      └──────── resolveRuntimeName ◄────────────┘
   正向链：ObjectId → RuntimeName →（再反解）→ ObjectId'   断言 ObjectId' == ObjectId（全量，无一遗漏）
   反向链：RuntimeName → ObjectId →（再解析）→ RuntimeName' 断言 RuntimeName' == RuntimeName（全量）
```

- 覆盖范围＝快照内**全部**对象（Device/Joint/Link/BaseMount/Base/Flange/Tcp/Geometry/Scene/Body），非抽样；
- 断言双射：`names.size() == objects.size()`（无重复名称、无重复对象）；
- 断言**无漏/旧/双前缀**：交叉校验 WC 实际对象名集合 ⊆ 映射名集合（无遗漏）；重编译对照用例锁定不残留旧机械臂名（RT-NM-5）；不存在 `Robot.Robot.joint_1` 形态双前缀（句法检查＋WC 交叉，RT-NM-6）。

### 7.5 别名、旧名称、持久化与迁移策略

| 事项 | 决定 |
| --- | --- |
| 名称是否持久化 | **不持久化**（RuntimeNameMap 每次编译确定性重建；确定性＋稳定排序使名称事实上稳定）；唯一入持久化链路的是其**内容身份**（随快照进入 evidence 切片与结果绑定，CON-06）——持久化位置责任：evidence（切片条目）与 execution（结果接纳记录），runtime 不写盘 |
| 运行时名称变化是否影响模型内容身份 | 是（localName 入 CanonicalModel 身份，§4.3.3）；但**对象引用不受影响**（持久引用一律 ObjectId——ARC-04） |
| 别名 | **不设别名机制**（R1/R2 无需求；别名破坏双射并引入第二真值——拒绝） |
| 旧名称迁移 | 无历史兼容负担（从头构建，不恢复 `old/` 命名体系）；跨修订重命名＝新映射＋新身份；旧快照的旧名称**在其快照内继续有效**（迟到结果反解用其绑定快照的映射，§9.3——绝不用当前映射反解旧结果） |
| 允许"一名多对象" | **不存在**（单射是构造不变量；消歧规则在生成期保证） |
| 必须拒绝 | 未知名/空名/跨快照名/大小写不符（精确等值）/业务单元自带前缀操作（静态门禁 R-4，NFR-MNT-07） |

### 7.6 canonical 序列化（内容身份计算）

NameMap 的 canonical 编码（RT-Codec 子形态 `IRDNAME`）：条目按 (scope, localName, ObjectId 规范文本) 字典序、长度前缀、UTF-8 无 NUL；`nameMapContentIdentity`＝SHA-256 over 该编码（core ContentDigester）；`nameMapRuleVersion` 入编码头。**跨进程一致**：worker 反序列化的映射与主进程身份相等（execution 通道比对凭据）。往返 `parse(encode(x))==x` 契约测试钉住（RT-NM-1）。

---

## 8. RobWork 适配层与资源边界

### 8.1 依赖边界（P-RT-3 登记）

ARCH §2.3 L2 行允许"L1 的 RobWork 数学/运动学类型"，而 §5.1/§7.3 明文要求 runtime 构造 WorkCell/DynamicWorkCell（rw::models／rwsim::dynamics）。本文按后者明文将以下基线库列为 runtime 的 L1 依赖：`sdurw_math`、rw kinematics（Frame/State/StateStructure）、rw models（WorkCell/SerialDevice/Joint）、rwsim dynamics（DynamicWorkCell/RigidDevice 构造）——全部为**零 Qt 的 RobWork 库**（R-3 不受影响）；**不使用**：rw loaders（XML 解析归 io）、rw pathplanners（归 trajectory）、rw proximity（碰撞归 policy）、rws（Studio 层，L3+）。该措辞差异请求架构评审确认（P-RT-3）；确认前按 §7.3 明文实施，不引入任何表外依赖。

### 8.2 所有权与只读包装

| 事项 | 设计 |
| --- | --- |
| RobWork 指针所有权 | WC/DWC 以 `rw::core::Ptr`（引用计数共享）持有于 RuntimeSnapshot 私有成员；快照销毁且无其他持有者时自动释放——runtime 是唯一构造者，快照是唯一规范持有者 |
| 只读包装 | 公共接口**不暴露** `rw::core::Ptr<WorkCell>`；暴露 `WorkCellConstView`（§8.3）——只转发 const 查询；可变句柄（addFrame/setBounds 等）只在编译器 `src/` 内部使用，公共头不可达 |
| 不向上层暴露裸指针 | 视图返回的 Frame/Device 访问一律经 view 方法（返回 const 引用），生命周期绑定快照（文档化：不得保存跨快照的 Frame 指针——测试以快照销毁后访问不可达〔ASAN〕锁定） |
| RobWork 对象名不作业务身份 | Frame/Device 名只是编译产物标签；业务身份永远是 ObjectId；名称↔对象互查只经 RuntimeNameMap（§7）；任何下游以 `findFrame(自行拼接名)` 取对象即为 R-4 违例 |
| RobWork 默认值不作需求默认值 | 编译器**显式设置全部被消费字段**（每关节 bounds/maxVelocity/maxAcceleration、DWC 重力、每 Body 质量惯量、Device 名）——不依赖 RobWork 构造默认；契约测试 RT-AD-2 逐字段对照 CanonicalModel（杜绝"RobWork 默认 bounds 悄悄生效"） |

### 8.3 WorkCellConstView 与 IRuntimeModelView

```cpp
// Adapter.hpp（节选）——只读视图：零可变句柄；发布后 WC 不再被修改，并发只读安全
class WorkCellConstView {
public:
    const rw::models::WorkCell& workCell() const noexcept;      // 只读引用；生命周期随快照
    const rw::kinematics::Frame* findFrame(std::string_view name) const noexcept;  // 未命中 nullptr
    rw::core::Ptr<const rw::models::SerialDevice> findDevice(std::string_view name) const noexcept;
    std::size_t frameCount() const noexcept;
    rw::kinematics::State defaultState() const;                 // 值拷贝——调用方线程私有
};
class DynamicWorkCellConstView {                                 // DWC 不存在时由 capability 表达，不构造本视图
public:
    const rwsim::dynamics::DynamicWorkCell& dynamicWorkCell() const noexcept;
    bool hasBody(std::string_view name) const noexcept;
};
```

```cpp
// Snapshot.hpp 内的统一只读入口（kinematics/trajectory/dynamics/policy 的共同消费点）
class IRuntimeModelView {           // 由 RuntimeSnapshot 实现；全部 const、并发只读安全
public:
    virtual ~IRuntimeModelView() = default;
    virtual const CanonicalModel& model() const = 0;
    virtual const RuntimeNameMap& nameMap() const = 0;
    virtual const RuntimeCapability& capabilities() const = 0;
    virtual const WorkCellConstView& workCell() const = 0;          // hasWorkCell 恒 true（否则无快照）
    virtual Expected<DynamicWorkCellConstView, RuntimeError> tryDynamicWorkCell() const = 0; // 无→错误＋定位能力项
    // —— 基座—世界唯一读取点（§6）——
    virtual rw::math::Transform3D<double> worldToBase() const = 0;
    virtual rw::math::Transform3D<double> baseToWorld() const = 0;
    virtual rw::math::Vector3D<double>    gravityWorld() const = 0;
    virtual rw::math::Vector3D<double>    gravityBase()  const = 0; // = R_world_base^T · g_world
    // —— 每线程 State 工厂（跨线程绝不共享 State）——
    virtual rw::kinematics::State makeState() const = 0;            // = WC defaultState 值拷贝
    virtual const IRuntimeNameResolver& nameResolver() const = 0;
};
```

IRuntimeModelView 接口属性：

| 属性 | 内容 |
| --- | --- |
| 前置/后置 | 前置：快照已发布（Published）；后置：返回只读引用，生命周期随快照 shared_ptr |
| 错误 | `tryDynamicWorkCell` 无 DWC→`RuntimeError`（code 指向能力缺失，非崩溃路径） |
| 线程 | 并发只读安全（发布后不可变）；`makeState` 每次返回线程私有值 |
| 确定性 | 全部查询为字段/结构直读（gravityBase 为纯矩阵乘） |
| 写权限/副作用 | 无/无（不存在任何修改入口） |
| 调用示例 | `auto st = view.makeState(); auto T = view.worldToBase() * device->baseTframe(st);`（kinematics 侧组合——runtime 自身不做 FK） |
| 非法调用 | 保存返回的 Frame 指针跨快照使用；对 view 的 const 结果 const_cast 修改（评审＋测试锁定） |

### 8.4 RobWork 异常→稳定诊断的转译

```cpp
// Adapter.hpp —— 转译纯函数（编译器在 S6/S7/S8 边界统一使用）
core::DiagnosticRecord translateRobWorkError(const std::exception& ex, std::string_view stage,
                                             std::optional<core::ObjectId> subject);
```

| RobWork 侧情形 | 转译结果（诊断码建议） | 语义 |
| --- | --- | --- |
| `rw::common::Exception`（含 message） | `wc-compile-failed` / `dwc-compile-failed`＋stage＋原始消息摘要（开发诊断保留原文；用户诊断不含调用栈，NFR-REL-05） | 编译硬失败 |
| 空 Ptr / nullptr Frame（基线 API 返回空） | `robwork-error`（`runtime/robwork-null-handle`）＋操作名 | 空句柄 |
| 层级无效（父子 Frame 不在树/循环 attach） | `robwork-error`＋`structure-invalid` 联合定位 | 无效层级 |
| 资源加载失败（Geometry 构造抛） | `resource-missing`/`robwork-error`（依 io 原始诊断） | 资源错误 |
| `std::bad_alloc` | `resource-budget` | 内存不足 |

转译规则：**适配层不吞异常**——转译后编译以 Failed 终止；全部记录进 CompileOutcome.diagnostics（subjectObjectId 尽量定位到规范对象）。基线版本差异：`RobWorkBaselineVersion`（commit/tag＋构建选项摘要，NFR-DEP-05 冻结基线数据源）随快照与缓存键记录；API 使用清单（本节列出的类型与方法）登记于冻结基线——基线升级时逐项复核（§13）。

### 8.5 WC↔DWC 关联校验与销毁顺序

| 校验 | 规则 |
| --- | --- |
| 同源校验 | DWC 由 S6 产出的同一 WC 构造（编译器内部保证；防御性断言 DWC.getWorkCell() 指向本 WC） |
| Body 覆盖 | 每个有物性的 CanonicalLink 恰好对应一个 Body；Body 数＝有物性连杆数＋有物性工具数；缺项与超项均为 `dwc-compile-failed` |
| 设备一致 | DWC 内 RigidDevice 引用的关节 Frame 集与 WC Device 关节集一致（逐关节名交叉校验，经名称映射） |
| 销毁顺序 | 快照成员声明序：nameMap（值）→ canonicalModel（值）→ dynamicWorkCell（Ptr）→ workCell（Ptr）——析构逆序＝DWC 先于 WC 释放（rwsim DWC 自持 WC 引用，顺序安全；值成员先析构无害） |

> P-RT-6 同步（2026-09-10）：资源 provider 的 io 实现采用 io.md §8.6 五条生命周期裁决；此补充不扩大调用方的指针持有权限。

### 8.6 资源与外部引用（IRuntimeResourceProvider）

runtime 对外部资源的职责＝**消费已验证内容＋记录引用＋编译期一致性检测**；解析、安全读取、预算、缺失/变化检测归 io（NFR-SEC-01/02、NFR-REL-04）。

```cpp
// Resource.hpp
enum class ResourceState { Recorded, Solidified };       // CON-03/PM-01 三段边界
struct ResourceRef {                    // CanonicalModel 内的资源引用（§4.3.5）
    core::ObjectId        resourceId;   // 资源对象身份（外部源记录经 CON-03 固化后成为项目对象）
    core::Digest256       contentDigest;// 内容摘要（SHA-256；io 计算后传入或编译器复算）
    std::optional<std::string> sourcePathHint; // 源路径（仅追溯提示——不作身份、不作引用键）
    ResourceState         state;        // Recorded | Solidified
    std::uint32_t         accessVersion;// 读取契约版本（io 格式版本）
};
struct ResourceBytes { const std::uint8_t* data; std::size_t size; core::Digest256 digest; };
struct ResourceReadError { RuntimeErrorCode code; core::ObjectId resourceId; std::string detail; };
class IRuntimeResourceProvider {        // io 实现（阶段 B 接入）；并发只读安全
public:
    virtual ~IRuntimeResourceProvider() = default;
    virtual Expected<ResourceBytes, ResourceReadError>
        tryResourceBytes(core::ObjectId resourceId) const = 0;   // 已验证内容（SafePath/Budget 在 io 侧已过）
};
```

资源规则总表：

| 规则 | 内容 |
| --- | --- |
| 路径不作身份 | ResourceRef 以 resourceId＋contentDigest 为键；sourcePathHint 仅提示；路径变化而内容不变→身份不变（不触发失效） |
| 资源变化→模型与缓存 | digest 入 CanonicalModel 身份（§4.3.5）→内容变化＝新身份＝缓存不命中（§9.4） |
| 缺失/变化/读取失败/预算超限 | io 负责检测与原始诊断；runtime 在 S4 定位 resourceId 并以 `ResourceMissing`/`ResourceChanged`/`ResourceBudget` 转译——**四类诊断严格区分**（RT-RES-\* 用例） |
| 编译期间被替换 | S10 前摘要复查（§5.4）；Solidified 资源免复查（项目内不可变存储保证） |
| 临时资源目录 | 编译期如需落盘临时物（基线 Geometry 加载的路径需求等），写入进程级临时目录的会话子目录（RAII 清理；清理失败→开发诊断不阻断）；**命令路径**的临时文件只允许 `.staging/tmp/`（project.md §5.3.6 口径，由命令侧适配器保证） |
| 不以外部路径为唯一引用 | CanonicalModel 绝不存裸外部路径作正式引用——未固化资源进入正式评估前由 evidence 的 Verified 门禁拦截（CON-03）；runtime 对 Recorded 状态资源产出**警告级**诊断"正式评估前须固化"，不代为阻断（阻断判定归 evidence validateForMode） |

### 8.7 线程安全与序列化边界

| 边界 | 规则 |
| --- | --- |
| WC/DWC 并发只读 | 发布后不修改（编译器不再触碰）；结构查询（findFrame/getFrames/bounds 读）多线程只读安全；**State 是唯一可变工作区且线程私有**（makeState 拷贝） |
| 跨线程共享 | 允许：`shared_ptr<const RuntimeSnapshot>`、其中的 view 与 nameMap；禁止：裸 Frame\*/Device\*/State\* 跨线程传递（view 封装强制＋评审规则） |
| 序列化 | runtime **不序列化 WC/DWC**（编译产物不持久化——MDL-06/S1：修订仅含权威参数化对象）；MDL-20 的 WorkCell/DWC XML 导出＝modeling 消费快照产物经 io/rw loaders 序列化（N-11 边界）；CanonicalModel/NameMap 的序列化仅 RT-Codec（worker 物化用，§9.5） |

### 8.8 框架补丁登记（SA-02 承接）

本文**不假定任何补丁存在**。已知风险触发点（若集成期确需补丁，按下表登记于 `industrialrobot/patches/PATCHES.md`，默认可关闭、关闭时与基线字节等价——SA-02）：

| 触发需求 | 可能补丁位置 | 影响范围 | 回滚方式 |
| --- | --- | --- | --- |
| Geometry 构造的确定性（浮点格式化路径差异）——附录 D 第 4 项契约测试若发现基线非确定性 | rw::geometry 相关构造/序列化 | 仅编译产物字节层；CanonicalModel 身份不含 WC 字节，身份不受影响 | 补丁默认 OFF；关闭后功能不受影响 |
| DWC Body 摩擦参数 API 缺项（若 rwsim 现有接口无法表达偏置摩擦模型） | rwsim::dynamics 相关头 | S7 构造路径 | OFF 时按能力缺失降级（DWC 跳过构造＋能力声明，§5.6） |

登记前以"无补丁方案"设计为准（上表第二行已给降级路径）；新增补丁一律走 SA-02 流程，不在本单元私自实现框架修改。

---

## 9. RuntimeSnapshot、并发与缓存契约

### 9.1 RuntimeSnapshot 字段表

结构级约定同 §4.3（版本 `runtime-snapshot/1.0`；不可变；由 IRuntimeSnapshotFactory 构造后经 shared_ptr\<const\> 共享；"身份"列标注是否进入 `snapshotIdentity`）。**快照构造完成后只读**——无任何修改途径；所有字段构造期一次性赋值。

| 字段 | 类型 | 必填 | 默认 | 身份 | 合法与非法实例 |
| --- | --- | --- | --- | --- | --- |
| `project / branch / revision / revisionSeq` | core 身份四元组 | 是 | — | **入**（来源定位） | 闭包内修订 |
| `modelIdentity` | core::ContentIdentity | 是 | — | **入**（＝CanonicalModel.contentIdentity） | 非零 |
| `nameMapIdentity / nameMapRuleVersion` | ContentIdentity / uint32 | 是 | — | **入** | 非零 |
| `workCellCompileIdentity` | core::ContentIdentity | 是 | — | **入**（＝SHA-256 over (modelIdentity, compilerContractVersion, compilerVersion, robworkBaselineVersion, nameMapRuleVersion, baseWorldRuleVersion, codecVersions, compileOptions〔DWC 无关子集〕)——WC 层缓存键，§9.4） | 非零 |
| `dynamicWorkCellState` | enum{Compiled, SkippedNoPhysics}＋Skipped 时的缺失对象清单 | 是 | — | **入**（Skipped 是能力事实，非失败） | 与 capability 一致（不一致构造拒绝） |
| `compilerContractVersion / compilerVersion` | uint32 / string | 是 | — | **入** | ≥1／非空 |
| `robworkBaselineVersion` | string（commit/tag＋选项摘要） | 是 | — | **入**（基线变化＝产物不可比，NFR-DEP-05） | 非空 |
| `codecVersions` | {canonical-model, name-map, snapshot} 三元 | 是 | — | **入** | 与编码头一致 |
| `compileOptions` | CompileOptions（布尔/枚举集：如碰撞几何包含、几何细节级别） | 是 | 默认集 | **入** | — |
| `resourceManifest` | std::vector\<ResourceRef\> | 是 | 可空 | **入**（经 modelIdentity 已含；冗余记录供下游直查） | 与 model.resourceManifest 一致 |
| `capabilities` | RuntimeCapability | 是 | — | **入**（与内容绑定，§9.6） | 与字段一致 |
| `diagnostics` | 警告级诊断集 | 是 | 可空 | **不入**（过程记录） | 无 error 级 |
| `createdAtUtc / createdFrom` | time_point / 枚举{Command, EvaluationPrepare, Worker} | 是 | — | **不入**（观测性要素不进身份——与 evidence §4.1.1 同口径） | — |
| `snapshotIdentity` | core::ContentIdentity | 是 | 派生 | —（即身份） | ＝SHA-256 over 全部"入"字段编码 |
| （私有）`model_ / nameMap_ / workCell_ / dynamicWorkCell_` | 值/Ptr | 是 | — | — | 仅经 IRuntimeModelView 暴露 |

### 9.2 并发与共享规则（含主进程/worker 隔离流程图）

| 问题 | 规则 |
| --- | --- |
| 构造完成后是否只读 | 是（shared_ptr\<const\>；无 setter；view 全 const） |
| 下游是否共享同一快照 | 是——同一切片的多次评估/多入口（kinematics/trajectory/policy）共享同一快照实例（省内存＋跨入口一致，AT-19 的模型侧基础） |
| 快照是否允许跨线程 | 是（不可变值＋只读视图；发布＝shared_ptr 原子交接，happens-before 由 shared_ptr 保证） |
| RobWork 对象是否跨线程共享 | WC/DWC 结构**是**（发布后只读）；State **否**（每线程 makeState 拷贝）；快照中不存在可变 RobWork 对象 |
| worker 隔离副本 | 工作进程**不共享主进程内存**：execution 序列化（修订摘要＋对象字节＋资源字节＋选项）→ worker 内 `IRuntimeSnapshotFactory::materialize` 重建独立快照（RT-Codec 保证重建后 modelIdentity/nameMapIdentity 相等——worker 按身份核对，不等即拒绝执行）；进程内 worker 线程池则共享主进程快照（只读）＋各自 State |
| 编译是否后台 | 允许（§5.7）；UI 线程禁止执行编译（ARCH §4.2） |

```
主进程                                            工作进程（N 个）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 请求方（域插件/命令服务）
   │ CompileRequest（注入源集合）
   ▼
 IRuntimeSnapshotFactory::create ──S1..S10──► shared_ptr<const RuntimeSnapshot>
   │                                         │ 主进程内：各域插件/评估器只读共享（同一实例）
   │                                         │ ＋ per-thread State（makeState）
   └──execution 派发──► [序列化：RevisionSummary＋对象字节＋资源字节＋CompileOptions]
                          │                     │
                          ▼                     ▼
                   worker：IRuntimeSnapshotFactory::materialize(bytes)
                          │ 重建独立快照；核对 modelIdentity/nameMapIdentity 与请求一致
                          ▼ 不一致→拒绝执行（开发诊断）；一致→评估器只读消费＋私有 State
```

### 9.3 项目切换后的旧快照与迟到结果

| 场景 | 规则 |
| --- | --- |
| 项目切换后旧快照存活 | RuntimeSnapshot **零外部依赖**（不持 project 存储上下文/查询端口句柄——编译完成后注入接口不再被引用）：项目 A 切到 B 后，A 的在途运行持有的旧快照继续支持迟到结果的**名称反解**（用结果绑定快照的 RuntimeNameMap——§7.5，绝不用当前映射）与证据补录 |
| 已释放上下文的迟到请求 | 归档写入归 project（其上下文关闭即拒绝——A7 防御规则）；runtime 侧：迟到请求若需**重新解析对象**（如补资源）而注入源已释放→`ContextReleased` 拒绝＋诊断；纯快照内操作（反解、只读查询）不依赖上下文，照常服务 |
| 取消与关闭期间的引用释放 | 快照 shared_ptr 引用计数自然释放（最后一个消费者释放即析构——DWC→WC 顺序 §8.5）；取消路径不强制持有；execution 保证在途运行结束后才允许快照引用清零（与 project 存储上下文 Draining 对齐，project §10.2） |

```
项目 A 运行中（run r1 绑定快照 S_A[r5]）
  → 用户切换项目（PM-03）→ 界面会话切 B；A 存储上下文保持至在途归档完成（A7）
  → r1 完成事件迟到 → execution 五元组核对（ARCH §4.5）→ 名称反解用 S_A 的 RuntimeNameMap（⑥端口）
  → 归档至项目 A 原修订 results/<r1>（project）→ 当前性独立判定（evidence，按内容身份）
  → r1 引用释放 → S_A 最后引用析构 → A 上下文 Closed
（全程 S_A 不访问项目存储；S_A 身份进入 r1 结果绑定——历史证据可追溯，CON-02）
```

### 9.4 编译缓存纯判定（IRuntimeCompileCacheKey）

runtime 只定义缓存身份与兼容判定（纯函数）；**存储、命中执行、淘汰归 execution**（CON-04/N-6）。

**缓存键分量**（§五.8 要求的全部分量及其来源）：

| 分量 | 来源 | 说明 |
| --- | --- | --- |
| CanonicalModel 内容身份 | modelIdentity | 已含：对象内容（oid+cv）、资源内容摘要、Description 契约版本、数值位模式与单位规范编码（§4.5 身份域） |
| 资源内容身份 | resourceManifest（经 modelIdentity） | 资源变化→digest 变→modelIdentity 变（不单列第二键） |
| 编译器版本 | compilerContractVersion＋compilerVersion | 契约或实现变化＝新键 |
| RobWork 基线版本 | robworkBaselineVersion | 基线升级＝新键 |
| RuntimeNameMap 规则版本 | nameMapRuleVersion | 规则变化＝名称可能变＝新键 |
| 基座—世界变换规则版本 | baseWorldRuleVersion | §6 规则版本常量（随规则修改递增） |
| 单位与规范编码版本 | codecVersions | 编码升版＝全体身份变化（§4.5） |
| 编译选项 | compileOptions | 选项集不同＝不同键 |
| 能力级别 | capabilityLevel（是否要求 DWC） | **不入 WC 层键、入 DWC 层键**（见下） |

```cpp
// CacheKey.hpp
struct CompileCacheKey {                     // 分层键：单模型派生两层
    core::ContentIdentity workCellKey;       // = f(modelIdentity, compiler*, baseline, nameMapRule,
                                             //    baseWorldRule, codecVersions, compileOptions〔DWC 无关子集〕)
    std::optional<core::ContentIdentity> dynamicWorkCellKey;  // = f(workCellKey 全部分量, compileOptions 全集,
                                             //    capabilityLevel)——SkippedNoPhysics 时为 nullopt
};
class IRuntimeCompileCacheKey {
public:
    virtual ~IRuntimeCompileCacheKey() = default;
    virtual CompileCacheKey buildKey(const CanonicalModel&, const CompileOptions&) const = 0;  // 纯函数
};
struct CompileCacheCompatibility {
    enum class Verdict { FullReuse, WorkCellOnlyReuse, Incompatible } verdict;
    std::vector<std::string> reasons;        // 分量级差异清单（model-changed / compiler-changed / …）
};
CompileCacheCompatibility judgeCompileCacheCompatibility(const CompileCacheKey& requested,
                                                         const CompileCacheKey& cached);  // 纯函数
```

**兼容性判定表**：

| 情形 | requested vs cached | 判定 | 说明 |
| --- | --- | --- | --- |
| 字节相同（全分量相等） | workCellKey 等 ∧ DWC 键等（或同为 nullopt 且请求不要求 DWC） | **FullReuse** | 完整命中；策略/种子/线程**不在本键**（它们经 evidence 切片身份进入**评估**缓存，CON-06——runtime 编译产物不消费这些要素） |
| WC 键相等、DWC 不满足 | workCellKey 等 ∧（cached DWC 键 nullopt 而 requested 要求 DWC ∨ DWC 键不等） | **WorkCellOnlyReuse** | "WorkCell 可复用而 DynamicWorkCell 不可"的显式表达：调用方可复用 WC 层产物、须重编 DWC；**不得**作为完整命中上报（CON-04） |
| 任一基础分量不等 | — | **Incompatible**（附分量级 reasons） | 旧编译器版本/旧基线/旧编码器一律在此拒绝（RT-CACHE-3）——不存在"版本接近可凑用"的语义等价复用 |
| 语义等价（编码不同但语义同） | — | **不参与复用判定**（§4.3.6：字节等值是唯一等价关系） | 保守方向（宁可重算不错复用） |
| 部分模型/失败产物 | — | **永不构成命中** | Failed/Cancelled 产物不进入缓存（存储侧由 execution 执行；本判定接口对"未 finalize/身份为空"输入直接 Incompatible——与 evidence §8.2 的分工：evidence 管评估结果缓存，本表管编译产物缓存） |
| 缓存命中 vs 结果当前性 | — | **两回事** | 命中＝产物对该键可复用；当前性＝该键相对当前输入是否仍适用（evidence computeCurrentness 按切片内容身份判定，CON-05）。**当前 HEAD 前进本身不使编译缓存失效**——只有被消费对象内容变化→modelIdentity 变→新键（AT-05"电机成本变更不失效运动学"在 runtime 侧的体现＝RobotDesign 对象未变→modelIdentity 不变→FullReuse） |

**与 evidence 输入切片的衔接**：`compilerContractVersion` 与 `robworkBaselineVersion` 同时进入 evidence 复现块（其 §4.1.2 ReproductionBlock 预留 compilerContractVersion/collisionBackendVersion 字段——本文回接：runtime 在快照中暴露这两个版本值，组装方随快照录入）；切片 Environment 条目引用 modelIdentity——同切片→同 modelIdentity→同 WC/DWC 键（评估缓存与编译缓存的键链一致性，SA-07）。

### 9.5 worker 物化编码（MaterializedSnapshotCodec）

用途＝execution 派发通道序列化（§9.2）；内容＝RevisionSummary＋对象字节（被消费闭包）＋资源字节＋CompileOptions＋编译身份预期值；**不是**项目持久化格式（不入 .rwdesign；与 io/project 格式无关）。约束：确定性编码（RT-Codec 家族，magic `IRDMAT1`）；worker 重建后身份核对（modelIdentity/nameMapIdentity 相等断言，不等拒绝执行）；大小受 execution 通道预算治理（NFR-PERF-03/04 归 execution）。

### 9.6 RuntimeCapability（能力声明）

```cpp
// CanonicalModel.hpp
struct RuntimeCapability {
    bool hasWorkCell            = true;   // 恒 true（无 WC 即无快照）
    bool hasDynamicWorkCell     = false;  // 全部被消费 Body 物性 provided 才 true（§5.2 S7）
    bool hasFullMassInertia     = false;  // 全连杆＋工具物性齐备
    bool hasJointVelocityLimits = false;  // 全关节 maxVelocity provided
    bool hasCollisionGeometry   = false;  // >=1 连杆/工具/场景 collision 几何
    bool hasTools               = false;  // tools 非空
    bool hasScene               = false;  // scene 非空
    bool hasFrictionModel       = false;  // 全关节摩擦 provided（dynamics DYN-06 消费）
    bool hasCouplingMatrix      = false;  // drivetrain.coupling 存在且良态（R2，MDL-21）
    std::vector<JointType> jointTypesPresent;   // 按链序（含 Continuous 类型保留事实，V12-01）
    bool hasBidirectionalNameMap= true;   // 恒 true（映射随快照必建且双射）
    // 下游所需模型视图的支持性由上述布尔组合表达；不预建"支持某分析"的域级布尔（那是各域的能力推导）
};
```

| 规则 | 内容 |
| --- | --- |
| 能力缺失 vs 输入非法 | NotProvided（缺失）→能力 false＋警告；provided 而非法→编译失败（§5.6 正交表） |
| 能力缺失 vs 编译失败 | 缺失不妨碍发布；失败不发布——正交，无相互推导 |
| 进入 diagnostics | 每个缺失能力一条**警告级** DiagnosticRecord（subject 定位首个缺失对象；建议动作指向建模补全路径；文案权威归 diagnostics/ui） |
| 下游处置 | 各域按声明决定跳过/阻塞/降级（dynamics 无 DWC→其评估按 DYN-06 判 DataInsufficient——域判定；kinematics 不受物性影响；reporting 标注缺项）；**runtime 绝不把能力缺失升级为工程不可行**（§5.6） |
| 能力与键 | capabilityLevel 入 DWC 层缓存键（§9.4） |

**能力 × 编译状态 × 诊断状态正交关系表**（发布门禁用）：

| 编译状态 | 能力 | 诊断 | 合法组合 | 快照发布 |
| --- | --- | --- | --- | --- |
| Published | 全齐备 | 无警告 | ✔ 完整快照 | 是 |
| Published | 有缺失项 | 对应警告逐项 | ✔ 降级快照（下游按能力声明处置） | 是 |
| Published | 任意 | 存在 error 级 | ✘ 构造拒绝 | 否 |
| Failed | —（未发布） | error 全量 | ✔ | 否 |
| Cancelled | — | 取消诊断（非 error，UX-03） | ✔ | 否 |

---

## 10. 跨单元协作与接口交接

### 10.0 公共接口总表（ICanonicalModelCompiler 与 IRuntimeSnapshotFactory 的完整属性）

```cpp
// Compiler.hpp / Snapshot.hpp
struct CompileOptions {
    bool includeCollisionGeometry = true;   // 编译碰撞几何入 WC（Visual 恒编译）
    GeometryDetail geometryDetail  = GeometryDetail::Full;  // 几何细节级别（缓存键成分）
    bool requestDynamicWorkCell    = true;  // 请求能力级别（§9.4 capabilityLevel）
};
struct CompileRequest {
    core::RevisionId               revision;        // 或预解析 RevisionSummary（worker 物化路径）
    const IObjectBytesSource*      objects;         // 注入（可空＝已带 RevisionSummary 的物化路径）
    const IRevisionClosureSource*  closure;
    const IRobotDesignReader*      designReader;
    const IRuntimeResourceProvider* resources;
    CompileOptions                 options;
    const ICompileCancelToken*     cancel;          // 可空＝不可取消
};
enum class CompileStatus { Published, Failed, Cancelled };
struct CompileOutcome {
    CompileStatus                          status;
    std::shared_ptr<const RuntimeSnapshot> snapshot;     // 仅 Published 非空
    std::vector<core::DiagnosticRecord>    diagnostics;  // 全量（不短路）
};

class ICanonicalModelCompiler {          // 十段链的实现入口（§5）；无状态、可重入
public:
    virtual ~ICanonicalModelCompiler() = default;
    virtual CompileOutcome compile(const CompileRequest&) = 0;
    // 分段入口（编译链内部使用/测试替身注入点；独立于 compile 的整体事务）：
    virtual Expected<CanonicalModel, RuntimeError> buildCanonicalModel(const CompileRequest&) = 0;  // S1–S5
    virtual std::uint32_t contractVersion() const noexcept = 0;
    virtual std::string   implementationVersion() const noexcept = 0;
};

class IRuntimeSnapshotFactory {          // S1–S10 编排 + worker 物化
public:
    virtual ~IRuntimeSnapshotFactory() = default;
    virtual CompileOutcome create(const CompileRequest&, ICanonicalModelCompiler&) = 0;
    // worker 侧：从物化字节重建独立快照；身份核对失败返回 Failed（诊断含期望/实得身份）
    virtual CompileOutcome materialize(const std::vector<std::uint8_t>& materializedBytes) = 0;
};
```

接口属性表（任务 §五.10 十项覆盖点逐项）：

| 能力（入口） | 前置 | 后置 | 错误 | 线程 | 确定性 | 写权限 | 副作用 | 生命周期/所有权 | 调用示例 | 非法调用 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 构造 CanonicalModel（buildCanonicalModel，S1–S5） | 注入源非空且一致（同一修订） | 返回不可变模型或诊断；不发布快照 | `InputInvalid`/`StructureInvalid`/`Resource*`（Expected） | 可重入（多线程各自请求） | 是 | 只读输入 | 无（零磁盘写） | 返回值归调用方 | 契约测试构造夹具模型 | 注入空源后断言成功；对物化路径同时给 objects 源 |
| 编译 WorkCell / DynamicWorkCell / 取得 RuntimeSnapshot（create，S1–S10） | 同上＋编译器实例有效 | Published→快照唯一出口；Failed/Cancelled→无快照＋诊断全量 | 见 §5.2 各段诊断码 | 允许后台线程；同一请求不可并发重入同一 CompileRequest | 是（同输入同身份同名称） | 只读输入；瞬态产物内部持有 | 无磁盘写（命令路径临时文件归 project 事务） | 快照 shared_ptr 归调用方与全部下游消费者 | `auto out = factory.create(req, compiler);` | 发布后继续向编译器写入同请求；忽略 status 直接取 snapshot |
| 解析规范对象→运行时对象 / 运行时名称→规范对象（resolveObjectId/resolveRuntimeName） | 快照已发布 | 双射保证；未命中错误含回显 | `UnknownObject` | 并发只读 | 是 | 只读 | 无 | 随快照 | §7.3 示例 | 跨快照混用；空串断言命中 |
| 校验基座—世界变换（S9＋view 读取） | WC 已编译 | WC BaseMount 变换与 T_world_base 在附录 D 第 4 项容差内一致 | `BaseWorldInconsistent` | 编译线程 | 是 | — | 无 | 编译期执行 | RT-BW-\* 用例 | 下游自行叠加旋转后仍期望通过 |
| 查询能力（capabilities/tryDynamicWorkCell） | 快照已发布 | 声明与字段一致 | 无 DWC→Expected 错误（非崩溃） | 并发只读 | 是（能力是内容派生） | 只读 | 无 | 随快照 | dynamics 消费前检查 hasDynamicWorkCell | 能力缺失时当作编译失败处置（§5.6 正交） |
| 生成编译缓存键（buildKey） | 模型有效 | 分层键（WC/DWC）；纯函数 | 无（不抛） | 可重入 | 是 | 只读 | 无 | 返回值 | execution 缓存登记前调用 | 把键当"当前性"判据（§9.4） |
| 释放/取消编译（cancel 令牌＋快照引用释放） | 编译进行中/快照持有中 | 取消→RolledBack→Cancelled；释放→引用计数归零析构 | 取消不是错误（UX-03） | 取消查询任意线程；析构随最后引用 | — | — | RAII 清理 | execution 持取消令牌；快照引用归持有者 | 取消后继续使用快照（已作废） |

### 10.1 与 core

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | runtime → core（接口依赖，编译链接，唯一 industrialrobot 边） |
| 输入/输出 | runtime 消费：身份类型、ContentDigester（SHA-256）、SourcedValue、单位换算、closeWithin、DiagnosticRecord；core 无回调 |
| 线程/生命周期 | 全部值类型/纯函数，并发安全；无共享实例 |
| 失败处理 | core 抛 `CoreError`→编译边界转译为对应 `RuntimeErrorCode`＋诊断（不透传异常出编译器） |
| 不允许的反向依赖 | core 不得包含 runtime 头/链接 runtime（core.md §2.3 已对齐） |

### 10.2 与 project（含 IModelCompilePort 承接答复——P-PR-7 的 runtime 侧回复）

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | project 命令服务（S5 双编译）**调用** runtime（经 L5 装配注入的适配器）；runtime **调用** project 只读能力（经 L5 适配的 IObjectBytesSource/IRevisionClosureSource）——双向均零编译依赖 |
| 接口承接 | project.md §5.3.6 `IModelCompilePort{compileWorkCellAndDwc(CompileRequest{query, plannedWrites, baseRevision})}` 与本文 `ICanonicalModelCompiler::compile` **兼容**：L5 胶水适配器构造本文 CompileRequest（objects/closure 适配 query，baseRevision＋plannedWrites 构成计划闭包视图——**适配器责任**，约数十行，归 L5/命令侧；其"plannedWrites 未提交对象"用内存态 ObjectSource 包装），CompileResult.ok 映射 status==Published。**差异两处**：①project 版 CompileResult 只含 ok＋diagnostics，无快照句柄——命令路径不需要快照（仅验证可编译性，产物瞬态）；适配器丢弃快照即可；②project 版无取消令牌参数——命令路径同步执行（ARCH §4.2 命令执行线程），取消走命令中止。登记 P-RT-8 请 project 侧确认无增量需求 |
| 输入/输出 | runtime 从 project：RevisionSummary、对象字节（(oid,cv) 寻址）、闭包校验；runtime 向 project：CompileOutcome（经适配器） |
| 线程 | 命令路径在命令执行线程同步调用（全局串行槽内；ARCH §4.2"编译类命令可与界面并行但不能与下一命令并发"）；分析路径在请求方线程 |
| 生命周期 | 注入接口引用仅在编译期内有效（编译器不保存）；快照编译完成后零 project 依赖（§9.3） |
| 失败处理 | 任一硬失败→适配器返回 ok=false→project 不提交修订（MDL-06 原子性，由 project 编排）；`compile-failed` 码沿用其 StoreErrorCode 表 |
| 不允许的反向依赖 | runtime 不得链接 project/包含 project 头（ARCH §3.5；适配器归 L5） |

### 10.3 与 evidence

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | 证据组装方（请求域/evidence 消费者）**调用** runtime 取快照身份与名称映射身份（值传递）；evidence 不调用 runtime 编译（快照组装是请求方职责，ARCH §7.6） |
| 输入/输出 | runtime 提供：modelIdentity、nameMapIdentity、compilerContractVersion、robworkBaselineVersion（落点——nameMapIdentity 入快照 nameMapRef；compilerContractVersion 入复现块；modelIdentity/robworkBaselineVersion 入切片 Environment 条目 `runtime.model-identity`/`runtime.robwork-baseline`，且属基准类要素进入 inputBaselineId：evidence.md §4.1.1/§4.2.1/§5.1，CR-05 关闭记录见 traceability/foundation-api-diff.md）；evidence 向 runtime 无输入 |
| 线程/生命周期 | 值传递，无耦合；结果接纳时的名称反解经⑥端口（IRuntimeNameResolver 绑定**结果所绑定快照**，§9.3） |
| 失败处理 | 反解失败（未知名）→接纳侧拒绝结果（CON-06"运行时名称必须先反解为对象 ID 才能接纳"的 runtime 侧支撑） |
| 不允许的反向依赖 | evidence↔runtime 互不链接（L2 内核横向互链禁止，ARCH §3.2）；evidence 的 IObjectBytesSource 与本文 §3.3 同形但**各自定义**（无共享头），L5 适配器可复用同一底层实现 |

### 10.4 与 modeling

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | modeling 实现 IRobotDesignReader（注入）；modeling 编辑器经①命令端口产生新修订后由评估准备路径触发重编译 |
| 输入/输出 | modeling→runtime：RobotDesign 对象字节→RobotDesignDescription（中性值）；runtime→modeling：编译诊断（经命令结果，供编辑器定位非法对象）与能力缺失警告（提示补全建模数据） |
| 线程/生命周期 | reader 为纯函数（并发只读）；每次编译调用 |
| 失败处理 | reader 失败→S2 `InputInvalid`（定位对象）；DH↔显式转换（MDL-10）在 modeling 完成，转换后模型经同一编译链等价验证（AT-16——等价验证入口＝RT-EQ-\* 契约测试的 FK 对照，附录 D 第 4 项容差） |
| 不允许的反向依赖 | runtime 不得链接 modeling/包含其 schema 头（P-RT-5 交叉核对） |

### 10.5 与 policy

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | policy 碰撞评估器**调用** runtime 只读视图（IRuntimeModelView：WC＋名称映射＋世界系环境几何）；runtime 不调用 policy（编译产物不消费策略——策略经 evidence 切片进入失效/缓存，CON-06） |
| 输入/输出 | runtime 提供：WorkCellConstView（几何/Frame 访问）、nameMap（碰撞对象 ID 对的名称互查）、世界系固连场景几何（§6.4） |
| 线程/生命周期 | 只读共享快照＋每线程 State；policy 自建其 RobWork 碰撞适配（rw proximity 归 policy 依赖，§8.1） |
| 失败处理 | 视图查询失败＝快照内不可能（双射保证）；策略解析失败归 policy |
| 不允许的反向依赖 | runtime 不实现碰撞规则/阈值（ARC-05/N-4）；policy 不重新编译模型 |

### 10.6 与 kinematics / 10.7 与 trajectory

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | kinematics/trajectory **调用** runtime 只读视图与 makeState；runtime 不调用它们 |
| 输入/输出 | 设备视图（SerialDevice、关节 bounds/限速）、worldToBase（§6.4 契约）、名称解析（诊断/结果中对象定位）；TRJ 的时间参数化限值消费 CanonicalJoint 的 bounds/maxVelocity/maxAcceleration（显式设值产物，§8.2） |
| 线程/生命周期 | 并发只读＋每线程 State；评估器实例生命周期归 execution |
| 失败处理 | FK/IK 失败归域算法；模型侧失败（不该发生）→视作实现缺陷诊断 |
| 不允许的反向依赖 | 两域不得自建关节链表示、不得绕过⑥端口拼名称（ARC-03/04） |

### 10.8 与 dynamics（含 drivetrain 边界）

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | dynamics **调用** runtime：tryDynamicWorkCell（能力检查）＋DynamicWorkCellConstView＋gravityBase；drivetrain 消费 view 中的 coupling 矩阵与传动比（DYN-04 虚功映射在其内实现） |
| 输入/输出 | DWC（Body/质量/惯量/摩擦——显式设值）、g_base、关节侧参数；DWC 缺失→Expected 错误→域按 DYN-06 判 DataInsufficient（**不是** runtime 判定） |
| 线程/生命周期 | 并发只读＋每线程 State；RNEA/正动力学在域内执行（rwsim 算法库由 dynamics 链接，非 runtime 传递） |
| 失败处理 | 病态耦合矩阵已在编译期阻止（§4.3.4）；运行期数值问题归域 |
| 不允许的反向依赖 | runtime 不实现 RNEA/映射（N-3）；dynamics 不自行装配 WC/DWC |

### 10.9 与 execution

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | execution **调用** runtime：快照工厂（评估准备段 Preparing）、materialize（worker）、缓存键判定（派发前）；runtime **消费** execution 注入的取消令牌与资源预算信号 |
| 输入/输出 | CompileOutcome→execution 派发物（含 snapshotIdentity 进运行记录）；缓存判定结果（FullReuse/WorkCellOnlyReuse/Incompatible）→execution 决定复用或重编（存储/淘汰归它） |
| 线程/生命周期 | Preparing 段在调度线程或其委派线程；worker 内物化在 worker 进程；快照引用由运行记录持有至归档完成（§9.3） |
| 失败处理 | 编译 Failed/Cancelled→任务按其状态机处置（Preparing→Failed/Canceled）；取消令牌触发→Cancelled |
| 不允许的反向依赖 | runtime 不实现调度/登记表/缓存存储（N-6）；execution 不得绕过缓存键判定私自复用产物（CON-04） |

### 10.10 与 io

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | io 实现 IRuntimeResourceProvider（注入）；runtime 消费已验证资源字节 |
| 输入/输出 | io→runtime：ResourceBytes（摘要已核）；runtime→io（经诊断转译）：resourceId 定位信息（供其原始诊断关联） |
| 线程/生命周期 | provider 并发只读；编译期内调用 |
| 失败处理 | 缺失/变化/预算/读取失败四类由 io 先诊断，runtime 转译定位（§8.6 表）；MDL-18/MDL-03/19 的 URDF/Xacro/WC 解析全程在 io/modeling，runtime 不参与 |
| 不允许的反向依赖 | runtime 不解析任何外部格式（N-7）；io 不编译模型 |

### 10.11 与 diagnostics

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | runtime 产出 DiagnosticRecord（core 契约）经调用方送 diagnostics 设施；码值与文案注册归 diagnostics——RT-\* 14 项**已收编**（diagnostics.md §4.6，2026-09-10；P-PR-6 同模式） |
| 输入/输出 | 稳定码（已收编 14 项，diagnostics.md §4.6）：RT-INPUT-INVALID、RT-STRUCTURE-INVALID、RT-UNIT-MISMATCH、RT-RESOURCE-MISSING/CHANGED/BUDGET、RT-WC-COMPILE-FAILED、RT-DWC-COMPILE-FAILED、RT-NAME-CONFLICT、RT-BASE-WORLD-INCONSISTENT、RT-CAPABILITY-MISSING、RT-ROBWORK-ERROR、RT-CACHE-INCOMPATIBLE、RT-CANCELLED（**码值权威＝StableCodeRegistry**）。**枚举对齐说明（v0.3）**：枚举值 `UnknownObject`/`ContextReleased` 属调用方契约违约（RuntimeError fail-fast，token 仅供日志，不发稳定码）；`RT-CAPABILITY-MISSING`/`RT-ROBWORK-ERROR` 为诊断事件码（编译诊断/RobWork 异常转译路径，非 RuntimeError 异常路径），故无对应枚举值——清单与枚举"12 个错误码 1:1＋2 个 fail-fast token＋2 个事件码"的关系就此冻结 |
| 失败处理 | 用户诊断不含 RobWork 调用栈/内部哈希（NFR-REL-05——转译只留消息摘要） |
| 不允许的反向依赖 | runtime 不链接 diagnostics（经 core 契约承载；注册由调用方/L5） |

### 10.12 与 reporting

| 项 | 内容 |
| --- | --- |
| 调用方/被调用方 | reporting **调用** runtime 只读摘要（快照身份块、能力声明、资源清单——运行时模型摘要与追溯信息，RPT-01 输入摘要章节数据源） |
| 输入/输出 | 只读投影（不含 RobWork 对象——报告不需要也不得依赖 WC 实例） |
| 失败处理 | 报告引用的快照已释放→reporting 侧以归档的快照身份元数据呈现（其详设冻结） |
| 不允许的反向依赖 | reporting 不触发编译、不直接读 WC/DWC（经 io 序列化产物或只读摘要） |

---

## 11. 验证方案及反例矩阵

测试目标 `sdurws_ird_runtime_test`（单元内）与 `sdurws_ird_runtime_contract_test`（跨单元：core 值类型/比较公式、project 适配器形态、真实 RobWork 基线结构断言）。**全部为设计用例，未实现未运行**；实现状态随 §12 任务登记。设施：testkit 断言（IRD_TEST_INFO/IRD_EXPECT_CLOSE/IRD_EXPECT_IDENTICAL/IRD_EXPECT_SET_EQ）、容差档案（`rt` profile，依据附录 D 第 4/5/12 项）、TempDir、DeterministicEnv；**测试替身**＝ScriptedObjectSource/ScriptedClosureSource（内存对象库）、FakeResourceProvider（可编程缺失/变化/预算）、ScriptedReader（Description 直构）、CancelToggle（取消注入）；**RobWork 本体不做替身**——结构断言直接针对真实基线库构造的 WorkCell/DWC（替身只覆盖注入接口；对 RobWork 行为的断言使用真实库，不以伪造输出充当框架行为——替身边界声明随测试文档留痕，RT-STUB-0）。

| 组 | 用例（依据｜前置｜操作｜预期｜观测点） |
| --- | --- |
| RT-ID-1 重复编译稳定 | ARC-03/AT-16｜同夹具输入｜compile 两次＋跨进程（子进程）一次｜modelIdentity/snapshotIdentity/NameMap 逐字节相等；WC 结构同名同构｜身份对比；Frame/Device 名集合 IRD_EXPECT_SET_EQ |
| RT-ID-2 无浮点近似等价 | §4.3.6/附录 D C4｜两 Description 某浮点字段相差 1e-15｜各编译｜身份**不等**（位模式编码）；缓存判定 Incompatible｜identity 对比；缓存 reasons 含 model-changed |
| RT-ID-3 展示无关性 | CON-05/AT-27｜同内容改项目显示名/重开会话｜重编译｜身份不变｜无失效类 reasons |
| RT-BW-1 变换数值例 | MDL-22/AT-37｜§6.5 数据（倒挂）｜编译＋view 读取｜点变换/反变换/重力投影逐元素符合手算（附录 D 第 4 项容差）｜worldToBase/baseToWorld/gravityBase 数值断言 |
| RT-BW-2 正反一致 | MDL-22｜地面/倒挂/壁装/自定义四预设｜T·inverse(T)==I 容差断言；四预设矩阵元素精确 {0,±1}（IRD_EXPECT_IDENTICAL）｜矩阵断言 |
| RT-BW-3 默认地面 | MDL-22/V15-04｜Description 未配置 base｜编译｜T_world_base==I 且来源 NotProvided→Ground；无诊断 error｜字段断言 |
| RT-BW-4 二次叠加反例（AT-37） | MDL-22/M-11｜替身消费方在 worldToBase() 后再乘 R_x(π)｜S9/消费端一致性检查｜等式 worldToBase()==快照字段 失败→`BaseWorldInconsistent`；反例被拦截｜诊断码＋偏差比较字段（实际值/期望值/单位） |
| RT-BW-5 四消费方一致 | AT-37｜倒挂机型，kinematics/trajectory/dynamics/policy 四替身各取 view｜各自计算 FK/重力｜全部等于 T_world_base·FK_device／Rᵀ·g（附录 D 第 4 项）｜四路结果互等断言 |
| RT-BW-6 非法变换 | NFR-COR-03｜反射矩阵/含 NaN 的 T／场景位姿非有限/循环 Frame（构造环）｜编译｜`InputInvalid`/`StructureInvalid` 就地定位｜诊断 subject/字段 |
| RT-NM-1 双向往返 | ARC-04/MDL-14/AT-18｜有效快照｜全量 ObjectId→Name→Id 与 Name→Id→Name｜双向全等、双射计数相等、编码往返 parse(encode(x))==x｜往返断言（IRD_EXPECT_IDENTICAL——附录 D 第 12 项无容差） |
| RT-NM-2 名称冲突消歧 | MDL-14｜两关节同 localName（含非法字符变体）｜编译｜确定性消歧（字典序后缀）、警告诊断列出消歧对｜映射内容＋诊断 |
| RT-NM-3 未知/空名 | ARC-04｜resolve("")/未知名/大小写不符名｜查询｜Expected 错误含回显；不抛、不默认命中｜错误码与 detail |
| RT-NM-4 跨项目同名 | ARC-04/MDL-14｜两项目相同 robotLocalName 各自编译｜两映射独立成立、无冲突｜两快照映射各自往返通过 |
| RT-NM-5 重命名后无旧名残留 | MDL-14/AT-18｜重命名 joint 后重编译｜新映射无旧名；旧快照旧名仍可反解（用旧快照映射）｜新旧映射对照 |
| RT-NM-6 双前缀反例 | AT-18｜注入替身 WC 内出现 `Robot.Robot.joint_1` 名（模拟错误写入）｜S8 交叉校验｜`NameConflict` 失败｜校验诊断 |
| RT-NM-7 身份不混用与前缀禁令 | ARC-04/CON-01｜把 RuntimeName 当 ObjectId 用（编译期类型层阻止）＋对象版本与身份摘要交叉对比；另以替身消费单元内拼接前缀后查询（模拟 R-4 违例——证明绕过唯一解析器不可靠）｜类型断言＋行为断言｜类型层不通过；拼名查询不得命中（映射键全为 ObjectId，全名只经 resolveRuntimeName 整串匹配）｜编译期＋运行期双检查；静态扫描（R-4 例外外零命中）归 WP-01-T01 门禁 |
| RT-CPX-1 编译事务 | MDL-06/AT-01｜WC 编译成功、DWC 构造异常（注入坏物性数据路径）｜compile｜整体 Failed、无快照、诊断含两段、临时对象析构（ASAN 无泄漏）｜status/诊断/内存 |
| RT-CPX-2 物性缺失降级 | DYN-06/MDL-06 断言分域｜全连杆 mass NotProvided｜compile｜Published＋hasDynamicWorkCell=false＋警告清单（非失败、非输入非法）｜capability＋诊断级别 |
| RT-CPX-3 provided 非法 | MDL-06｜mass=−1（provided）｜compile｜Failed＋`InputInvalid` 定位连杆（与能力缺失严格区分）｜诊断码对照 RT-CPX-2 |
| RT-CPX-4 取消/内存不足/清理失败 | TASK-01/UX-03、§5.5/§8.6｜CancelToggle 在 S6 置位；另注入 `std::bad_alloc`（替身 reader 抛）；再令临时目录清理失败（文件占用）｜compile｜取消→Cancelled（非错误诊断）＋无半成品＋可重入重试成功；bad_alloc→`ResourceBudget`＋对象清理（ASAN）；清理失败→开发诊断不阻断 | status/诊断级别/内存/临时目录残留 |
| RT-RES-1 缺失/变化/预算区分 | NFR-REL-04/§8.6｜FakeResourceProvider 三种编程｜编译｜`ResourceMissing`/`ResourceChanged`/`ResourceBudget` 三码分立、resourceId 定位｜诊断码逐例 |
| RT-RES-2 编译中替换 | §5.4｜S4 后、S10 前 provider 换内容｜编译｜`ResourceChanged` 整体失败（Recorded 资源）；Solidified 资源不受影响｜status/诊断 |
| RT-RES-3 路径不作身份 | §8.6｜同内容资源换 sourcePathHint｜重编译｜modelIdentity 不变｜身份对比 |
| RT-CACHE-1 键完备 | CON-04｜逐分量扰动（选项/编译器版本/基线版本/规则版本/编码版本/能力级别）；另按实际依赖扰动内容：几何资源字节变、质量值变、工具 TCP 变→各自经 modelIdentity 变键；**策略内容身份变化→键不变**（策略归 evidence 切片，§9.4）｜buildKey 对比｜任一分量变化→DWC 或 WC 键变化；reasons 精确；内容类变化全部落在 model-changed｜键与 reasons 逐例 |
| RT-CACHE-2 WC 复用 DWC 不可 | CON-04/§9.4｜cached 物性缺失（DWC 键 nullopt）、requested 要求 DWC｜judge｜WorkCellOnlyReuse（非 FullReuse）｜verdict＋调用方处置路径 |
| RT-CACHE-3 旧版本拒绝 | CON-04｜compilerContractVersion 不等的 cached 键｜judge｜Incompatible＋contract-changed reason｜verdict |
| RT-CACHE-4 命中≠当前性 | CON-05/AT-05｜"电机成本对象变化"场景（RobotDesign 未变）｜重建请求键｜FullReuse（运行时缓存不失效）；当前性变化归 evidence（本测试只断言 runtime 侧行为）｜verdict＋文档断言 |
| RT-SNAP-1 只读并发 | NFR-COR-02｜多线程并发 view/nameMap 查询｜TSAN/等价评审｜零数据竞争；makeState 各线程独立｜线程断言 |
| RT-SNAP-2 worker 隔离 | ARCH §4.1｜主进程快照＋worker 物化重建｜materialize｜身份相等可执行；worker 侧修改替身不可达主进程对象（进程隔离天然＋接口无写入口）｜身份核对＋只读断言 |
| RT-SNAP-3 项目切换旧快照 | TASK-03/AT-10｜项目 A 快照持有下切换 B｜迟到反解（用 A 快照映射）＋A 上下文关闭后的迟到补资源请求｜反解成功；补资源请求 `ContextReleased` 拒绝｜两路结果 |
| RT-SNAP-4 释放后迟到 | §9.3｜快照引用清零后再访问（持悬垂 view）｜ASAN 用例｜访问不可达（生命周期由 shared_ptr 保证）｜ASAN 报告 |
| RT-AD-1 RobWork 异常转译 | §8.4｜构造触发 rw 异常的输入（重复 Frame 名等）｜编译｜`wc-compile-failed` 稳定诊断（含 stage、无调用栈泄漏到用户诊断）｜诊断字段 |
| RT-AD-2 显式设值 | §8.2｜正常编译产物｜逐关节对照 CanonicalModel vs WC 实际 bounds/maxVelocity/maxAcceleration；DWC 重力 vs gravityWorld｜全部显式相等，无 RobWork 默认残留（如未设关节的默认 ±inf bounds）｜逐字段断言 |
| RT-AD-3 销毁顺序 | §8.5｜快照析构（含 DWC）｜ASAN｜DWC 先于 WC 释放、无泄漏、无 use-after-free｜ASAN |
| RT-CPL-1 耦合矩阵病态阻止 | MDL-21/M-6（R2 预置用例）｜奇异 C 与条件数 1e10 的 C｜编译｜`InputInvalid`＋比较型诊断（实际条件数/阈值）；良态 C 正常编译入模型｜诊断对照 |
| RT-CAP-1 能力声明一致性 | §9.6｜混合缺失输入（物性/摩擦/几何/工具各缺一）｜编译｜各能力位正确、每缺失一条警告、subject 定位首个缺失对象｜capability＋诊断清单 |
| RT-CAP-2 缺失不升级 | §5.6｜能力缺失快照交给替身消费方｜消费方按声明处置（dynamics 替身→DataInsufficient 语义由域声明；runtime 不产出工程判定）｜runtime 输出中无任何 EngineeringStatus 字段（类型层不存在）｜接口签名审查＋测试断言 |
| RT-CONT-1 防混入 | CON-01/CM-0｜对象源返回不属于目标修订的 (oid,cv)｜编译｜`StructureInvalid` 拒绝｜诊断 |
| RT-CONT-2 修订只读 | §2.3/MDL-06｜编译全程对内存对象库做写尝试探针（只读接口无写方法——类型层）＋编译前后对象字节哈希对比｜字节不变｜哈希对比 |
| RT-EQ-1 canonical↔WC FK 等价 | ARC-03/AT-16/附录 D 第 4 项｜夹具模型若干构型｜canonical 侧手写 FK（测试内独立小实现，仅测试用）vs RobWork Device FK 对照｜逐关节轴线/原点偏差 ≤1e-9 m/rad（第 4 项）——注意：替身 FK 是**测试对照工具**，不构成产品 FK 算法证明（真值以 RobWork 为消费基线；等价断言只证明编译映射正确）｜逐元素 CLOSE |
| RT-STUB-0 替身边界声明 | 任务约束§八｜全部替身用例｜评审检查｜测试文档显式声明：替身输出仅验证 runtime 契约，不构成 RobWork 算法/业务算法正确性证明；任何未执行测试不得标注通过｜文档留痕 |

**Windows GUI 测试规则承接**：runtime 全部测试为无界面模型测试（零 Qt、零平台插件需求）——用户级 AGENTS.md 的 GUI 分支（QT_QPA_PLATFORM=windows、单实例绝对路径等）不适用；未来若 runtime 相关集成测试出现 GUI 形态（不应有），按该规则执行。本阶段只设计流程，不启动 GUI 程序。

---

## 12. 阶段 A 实现任务拆分

局部编号 `RT-Txx`（runtime＝**WP-06**，REQUIREMENTS §3/ARCH §3.1 既有登记；不重排任何上游 WP 编号；DETAILED-DESIGN/development-task-breakdown 产出后如需对齐，以映射表增量登记——同 testkit P-TK-3 口径）。依赖自上而下；**阶段 A 交付**：CanonicalModel 契约与校验、确定性编译基础设施、WC/DWC 适配、基座—世界单一规则、RuntimeNameMap、RuntimeSnapshot、诊断与能力声明、缓存纯判定、可控测试替身与契约测试；**不含**：真实 RobotDesignReader（modeling，阶段 B）、真实资源提供者（io，阶段 B）、RNEA/碰撞等业务算法消费（各域，阶段 B/C）、执行/存储侧缓存设施（execution）。不提前实现业务算法，不构造伪造的完整工程模型。

| 任务 | 输入 | 产物 | 依赖 | 涉及文件 | 验证方式 | 完成条件 |
| --- | --- | --- | --- | --- | --- | --- |
| RT-T01 构建落位 | 骨架 CMakeLists、§3.4、P-RT-3 | `sdurws_ird_runtime` 升级 STATIC（C++17、链 core＋rw/rwsim 基线目标——目标名本任务确认）；注册 `_test`/`_contract_test`（gtest 按 development-task-breakdown §5.5 定稿） | 无（CORE-T01 后更佳，可平行） | `industrialrobot/CMakeLists.txt`、`runtime/CMakeLists.txt`（新）、`runtime/src/*`（空起步） | 独立冒烟＋集成构建配置成功；零 Qt/零单元边扫描（PRJ-TX-14 等价） | 两模式构建零错误；基线库链接清单留痕（P-RT-3 消账输入） |
| RT-T02 错误与注入契约 | §3.3/§3.4 | `Errors.hpp/.cpp`、`Sources.hpp` | RT-T01 | 同名文件 | 单元测试（token/Expected 语义） | 错误码全表用例通过 |
| RT-T03 Description 与校验器 | §4.2、§5.2 S2/S3 | `Description.hpp`＋结构与单位校验器（含耦合矩阵校验） | RT-T02 | 同名文件 | RT-CPX-3、RT-CPL-1、RT-BW-6（校验器部分） | 全部硬校验反例通过 |
| RT-T04 CanonicalModel 与编码器 | §4.3/§4.5 | `CanonicalModel.hpp/.cpp`（含 RuntimeCapability、builder 不变量）、`Codec.hpp/.cpp`（RT-Codec 三形态） | RT-T03 | 同名文件 | RT-ID-1/2/3（模型层）；往返 parse(encode(x))==x | 字段表逐项实现；身份域排除项用例通过 |
| RT-T05 名称映射 | §7 | `NameMap.hpp/.cpp`（生成/消歧/双射/内容身份） | RT-T04 | 同名文件 | RT-NM-1～7 | AT-18 双向往返全量用例通过 |
| RT-T06 基座—世界规则 | §6 | `BaseWorldTransform.hpp/.cpp`（预设/校验/正反解纯函数） | RT-T04 | 同名文件 | RT-BW-1/2/3/6（规则部分） | 四预设＋反例用例通过；P-RT-4 冻结留痕 |
| RT-T07 WC 编译器 | §5.2 S6、§8.2/§8.4 | `Adapter.hpp`（视图/转译）＋WC 编译实现（Frame 树/Device/Joint/BaseMount 唯一写入） | RT-T04/T06 | `src/WorkCellCompiler.*` 等 | RT-AD-1/2、RT-EQ-1、RT-BW-4（S9 部分） | 显式设值与等价用例通过 |
| RT-T08 DWC 编译器 | §5.2 S7、§8.5 | DWC 构造＋能力门控＋关联校验 | RT-T07 | `src/DynamicWorkCellCompiler.*` | RT-CPX-1/2、RT-AD-3 | 门控/原子性/销毁顺序用例通过 |
| RT-T09 快照与工厂 | §9.1～§9.3 | `Snapshot.hpp/.cpp`（RuntimeSnapshot/IRuntimeModelView 实现/工厂/materialize） | RT-T05/T07/T08 | 同名文件 | RT-SNAP-1～4、RT-CPX-4 | 并发/隔离/迟到用例通过 |
| RT-T10 缓存纯判定 | §9.4 | `CacheKey.hpp/.cpp` | RT-T04 | 同名文件 | RT-CACHE-1～4 | 分层键与判定表全用例通过 |
| RT-T11 编译器集成与取消 | §5.1～§5.7 | `Compiler.hpp/.cpp`（十段链/事务/取消/资源复查） | RT-T03～T09 | `src/CompilerImpl.*` 等 | RT-CPX 全组、RT-RES-1/2、RT-CONT-1/2 | 事务状态机全转移用例通过 |
| RT-T12 测试替身与契约套件 | §11、testkit §10.2 | Scripted\*/Fake\* 替身＋RT-\* 全部用例体；`testdata/golden/rt-fk-equation/`（analytic，附录 D 第 4 项）、`rt-namemap-roundtrip/`（contract-fixture）数据集与容差档案 `rt` | RT-T01～T11、testkit 可用 | `runtime/test/*`、`testdata/…` | §11 矩阵逐条 | 全部用例通过并留痕；RT-STUB-0 边界声明在案；**未执行不得标注通过** |
| RT-T13 文档与门禁同步 | 全文 | README 指向核对（§9→§12）；§15.3 待裁决项状态更新；R-4 例外登记（名称解析器实现文件清单）提交 WP-01-T01；UT-BUILD 建议并入 CI | RT-T01～T12 | 本文、`runtime/include/.../README.md` | 评审 | 本文与实现零偏差登记；未决项最新状态 |

---

## 13. 后续阶段承接与接口交接清单

### 13.1 runtime 在后续阶段的交付（本文已设计、随阶段落地）

| 能力 | 阶段 | 说明 |
| --- | --- | --- |
| 真实 IRobotDesignReader 接入（URDF/Xacro 导入→RobotDesign→Description） | B（modeling 侧实现） | 本文契约（§4.2）不变；P-RT-5 交叉核对后冻结 |
| 真实 IRuntimeResourceProvider 接入（网格/目录读取） | B（io 侧实现） | §8.6 契约不变 |
| 命令路径双编译适配器（IModelCompilePort over ICanonicalModelCompiler） | B（L5 装配/命令侧） | §10.2 方案；P-RT-8 确认后落地 |
| WC 反向导入（MDL-18，R2） | D | 不经本编译链（草稿通道，modeling/io 主导）；runtime 仅在其"转正式"后按正常链编译 |
| 传动耦合矩阵全链（MDL-21/DYN-04/SEL，R2/D） | D | CanonicalModel 已承载（§4.3.4）；drivetrain 消费 view；AT-38 |
| KIN-09～11 大规模采样对快照只读并发的压力使用 | D | 契约不变；性能归 execution/各域 |

### 13.2 接口交接清单（各对端单元详设/实现的直接输入）

| 对端单元 | 从 runtime 接收 | 须在其详设冻结的相邻契约 |
| --- | --- | --- |
| modeling | RobotDesignDescription 契约与 IRobotDesignReader 接口（§4.2）；编译诊断定位（对象/字段）；能力缺失警告清单（补全建模数据的提示数据源） | reader 实现（其对象 canonical 编码→Description 的映射与 SI 化点）；MDL-10 等价验证与 RT-EQ 数据集协作；安装预设轴向确认（P-RT-4） |
| policy | IRuntimeModelView（WC 只读＋名称互查＋世界系场景几何）；快照只读共享约定（AT-19 模型侧基础） | 其 RobWork 碰撞适配的依赖声明（rw proximity 归 policy）；碰撞对象 ID 对的产出规则 |
| kinematics/trajectory | DeviceView、bounds/限速视图、makeState、worldToBase | FK/IK/规划的 State 使用纪律（每线程 State）；诊断中名称经⑥端口 |
| dynamics/drivetrain | tryDynamicWorkCell、gravityBase、摩擦/物性视图、coupling/ratio 视图 | RNEA 消费方式（rwsim 库由其链接）；DYN-06 降级对 capability 的消费映射 |
| execution | CompileOutcome、IRuntimeSnapshotFactory（create/materialize）、取消令牌接口、缓存键与判定（§9.4）、快照引用持有纪律（§9.3） | 序列化通道格式消费（MaterializedSnapshotCodec 字节）；缓存存储/淘汰；Preparing 段线程模型 |
| evidence | modelIdentity/nameMapIdentity/compilerContractVersion/robworkBaselineVersion（值传递入口）；IRuntimeNameResolver（接纳反解） | 复现块字段对齐（其 §4.1.2）；切片 Environment 条目引用 modelIdentity 的编码 |
| io | IRuntimeResourceProvider 接口、ResourceRef 记录契约 | 资源读取/检测/预算实现；accessVersion 语义 |
| diagnostics | RT-\* 建议诊断码清单（§10.11）与错误 token 表（§3.4） | 码值收编与文案；用户/开发两级呈现 |
| reporting | 快照身份块/能力/资源清单的只读摘要入口 | 摘要 schema（RPT-01 输入章节） |
| project | §10.2 适配方案（P-PR-7 回复） | CompileRequest 适配器归属与实现（L5/命令侧）；`.staging/tmp` 使用口径 |
| testkit | RT-\* 用例对断言/容差档案的消费；数据集清单登记（rt-fk-equation analytic／rt-namemap-roundtrip contract-fixture） | 容差档案 `rt` 条目与附录 D 第 4/5/12 项的 basis 声明 |

---

## 14. 需求—设计—验证追踪矩阵

| 需求/上游条款 | 设计落点 | 验证（§11 组） |
| --- | --- | --- |
| ARC-03（运行时真值唯一；编译确定性） | §4（CanonicalModel 唯一规范模型）、§5（十段链）、§4.1 四对象区分 | RT-ID-1/2、RT-EQ-1、RT-CONT-1/2 |
| ARC-04（稳定对象 ID＋统一名称解析器） | §7（RuntimeNameMap 双射＋唯一前缀操作点）、§3.4 R-4 例外 | RT-NM-1～7 |
| CON-01（不可变快照；防混入） | §4.1 CM-0、§5.2 S1/S2 闭包校验 | RT-CONT-1 |
| CON-02（三态正交；旧快照历史证据） | §9.1/§9.3（快照零外部依赖、迟到反解用旧映射） | RT-SNAP-3/4 |
| CON-03（外部资源固化；记录侧） | §8.6（ResourceState/警告不代阻断） | RT-RES-\*、RT-CPX-2 关联 |
| CON-04（缓存契约判定；部分/失败不作命中） | §9.4（分层键/判定表/WorkCellOnlyReuse） | RT-CACHE-1～4、RT-CPX-1 |
| CON-05（切片内容身份；HEAD 前进不自动失效） | §4.3.6/§4.5（身份域）、§9.4（键纯内容） | RT-ID-3、RT-CACHE-4 |
| CON-06（NameMap 内容身份入快照；反解后接纳） | §7.6（内容身份）、§9.3（绑定快照反解）、§10.3 | RT-NM-1/5、RT-SNAP-3 |
| MDL-06（双编译原子性；断言分域） | §5.2/§5.3（事务；物性 provided 非法→InputInvalid／NotProvided→降级） | RT-CPX-1/2/3 |
| MDL-14（完整 RuntimeNameMap；无漏/旧/双前缀） | §7.2/§7.4（交叉校验＋往返） | RT-NM-1/5/6 |
| MDL-21（耦合矩阵承载与阻止，R2） | §4.3.4（CouplingMatrix 校验） | RT-CPL-1 |
| MDL-22/AT-37（安装姿态；单一变换；默认地面） | §6 全节 | RT-BW-1～6 |
| KIN-12/AT-27（显示单位不影响真值/身份） | §4.4（只有 SI）、§4.3.6（显示字段不入身份） | RT-ID-3 |
| KIN-13（求解配置不入编译输入；不入策略覆盖） | §5.1（CompileRequest 无求解配置）、§9.4（键不含） | RT-CACHE-1（分量表） |
| TRJ-05/DYN-03（限值显式来源） | §8.2（显式设值纪律） | RT-AD-2 |
| DYN-01/02（重力投影；摩擦承载） | §6.1（g_base 公式）、§4.3.3（friction） | RT-BW-1/5、RT-CAP-1 |
| DYN-04（关节侧与传动无关；C 供映射） | §4.3.4（C 承载）、§10.8（drivetrain 消费） | RT-CPL-1 |
| DYN-06/MDL-16（缺失→降级非阻断） | §5.6/§9.6（能力正交） | RT-CPX-2、RT-CAP-1/2 |
| NFR-COR-01/附录 D 第 4 项（等价验证容差） | §6.5/§11 RT-EQ-1（1e-9 m/rad） | RT-EQ-1、RT-BW-\* |
| NFR-COR-02（确定性要素） | §4.5/§5.5（可重入/稳定产物） | RT-ID-1、RT-SNAP-1 |
| NFR-COR-03（非有限/非法单位/引用缺失不静默） | §4.4/§5.2 S3 | RT-BW-6、RT-CPX-3 |
| NFR-MNT-01（内核零 Qt） | §3.2/§3.4 | RT-T01 扫描（PRJ-TX-14 等价） |
| NFR-MNT-04（禁无价值包装） | §4.2（Description 为注入边界件非转发器——论证：隔离 modeling schema 耦合）、§8.3（view 承载线程/生命周期契约） | 设计评审 |
| NFR-MNT-07/R-4（前缀操作唯一例外） | §7（例外登记于 RT-T13） | RT-NM-\*＋静态门禁 |
| NFR-REL-04（资源可检测的 runtime 切面） | §5.4/§8.6 | RT-RES-1/2/3 |
| SA-02（框架零修改＋补丁登记） | §8.8 | 评审＋patches 门禁 |
| SA-04/SA-05/SA-10/SA-15（架构决策承接） | §1.1/§2.1/§5/§7 | 全文＋RT-\* |
| TASK-01/03（取消；快照与运行绑定） | §5.5/§9.3 | RT-CPX-4、RT-SNAP-3 |
| AT-01（断言分域＋双编译原子性） | §5.2/§5.6 | RT-CPX-1/2/3 |
| AT-05（失效矩阵的 runtime 侧） | §4.3.6/§9.4 | RT-ID-3、RT-CACHE-4 |
| AT-10（迟到结果；旧快照支持） | §9.3 | RT-SNAP-3 |
| AT-16（DH/显式等价入口） | §10.4/§11 RT-EQ-1 | RT-EQ-1 |
| AT-18（名称往返） | §7.4 | RT-NM-1/5/6 |
| AT-19（跨入口一致的模型侧基础） | §9.2（共享快照）、§10.5 | RT-SNAP-1 |
| AT-27（单位/配置分层） | §4.4、§9.4 | RT-ID-3、RT-CACHE-1 |
| AT-37（基座—世界一致消费） | §6 全节 | RT-BW-1/2/4/5 |
| AT-38（耦合矩阵映射一致，R2） | §4.3.4/§13.1 | RT-CPL-1 |

---

## 15. 设计决策、风险、待裁决项与变更记录

### 15.1 设计决策登记（本文作出并说明理由的普通实现选择）

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | CanonicalModel 为**瞬态不可变值模型**（不持久化），身份＝RT-Codec 摘要 | ARCH §7.3 三段链语义＋MDL-06（修订仅含权威参数化）；持久化会引入第二真值风险。备选（持久化规范模型）被 PA-1 否决 |
| D-02 | RobotDesign→Description 经**注入 reader**（IRobotDesignReader）而非 runtime 直接解码 modeling 对象编码 | 解耦 schema 演化（modeling 拥有对象格式）；与 IModelCompilePort/IObjectBytesSource 同一注入模式；阶段 A 可用夹具直构。备选（直接解码）耦合更强，登记 P-RT-5 供对端选择 |
| D-03 | DWC 缺失以**能力门控**表达（物性 NotProvided→SkippedNoPhysics），DWC 硬失败则整体失败 | 同时满足 MDL-06 原子性（命令路径任一硬失败不提交）与 DYN-06 降级语义（缺失≠失败）；"半成品快照"不存在——Skipped 是声明完整的发布态 |
| D-04 | 编译缓存键**分层**（WC 键/DWC 键），capabilityLevel 只入 DWC 键 | 支持"WC 可复用而 DWC 不可"（任务 §五.8 明确场景）且避免无 DWC 请求被 DWC 变化误伤 |
| D-05 | 名称映射**不持久化、无别名**，确定性重建 | ARC-04 双射＋内容身份已满足 CON-06 追溯；别名/持久化引入第二真值与迁移负担（从头构建无历史包袱） |
| D-06 | 基座—世界以 `Transform3D`（R＋t）整量存储（非仅旋转） | MDL-22 聚焦姿态但场景布置需要平移；整量单一字段避免"旋转一处/平移另一处"的分裂存储 |
| D-07 | 安装预设轴向前定义后冻结：倒挂＝R_x(π)、壁装＝R_y(π/2)（P-RT-4） | MDL-22 未指明轴向；设计需要可执行矩阵；与 modeling 编辑器对齐后冻结，分歧走该待裁决 |
| D-08 | RT-Codec 自有二进制编码（同 evidence D-02 模式），浮点位模式、大端、presence 字节 | 无共享 JSON 库（实测）；跨进程身份一致（NFR-COR-02）；身份不依赖第三方 |
| D-09 | 视图封装（WorkCellConstView/IRuntimeModelView）不暴露 `rw::core::Ptr` | 生命周期绑定快照＋禁跨快照裸指针（§8.2）；只读包装承载线程契约——非无价值转发（NFR-MNT-04 论证见 §8.3） |
| D-10 | 资源以 digest 入身份、路径仅 hint；Solidified 免发布前复查 | 路径变化内容不变不失效（正确方向）；固化资源由存储不可变保证，复查只对 Recorded 有意义 |
| D-11 | 编译器无内部超时，取消一律经注入令牌；取消＝非错误 | 取消语义/时窗归 execution（NFR-PERF-02）；UX-03 正常取消不产错误诊断 |
| D-12 | 诊断（diagnostics）与能力（capabilities）不入 CanonicalModel 身份 | 身份＝内容；同输入同诊断由确定性间接保证；能力是内容派生投影（冗余存储仅为 O(1) 查询，一致性构造期校验） |
| D-13 | worker 物化身份核对（modelIdentity/nameMapIdentity 相等才执行） | 防通道错配/半传输；与 evidence 注册表 manifest 比对同精神（CON-06 装配侧） |
| D-14 | 快照成员声明序保证 DWC 先析构 | rwsim DWC 持 WC 引用，顺序安全显式化（§8.5） |

### 15.2 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | core.md v0.1 未冻结，契约签名可能变化 | RT-T02 起返工 | §3.2 消费清单逐项锚定 core.md 章节；core 冻结出 diff 后增量同步（P-RT-1） |
| R-2 | rwsim DWC/摩擦 API 与 MDL-16 摩擦模型（fv/fc/偏置）的表达力差异未逐一实测 | S7 构造路径返工或需补丁 | RT-T08 前置 API 实测（read 阶段仅确认类形）；表达力缺项走 §8.8 补丁流程或能力降级（已备路径） |
| R-3 | modeling 侧 RobotDesign schema 演化频繁（阶段 B 起迭代） | Description 契约反复升版 | descriptionContractVersion 入缓存键（升版＝新键，保守正确）；P-RT-5 对端冻结 |
| R-4 | RobWork 基线并发只读假设（无内部惰性可变缓存）若有例外 | 并发只读破坏 | RT-SNAP-1 TSAN 全覆盖＋只读路径 API 清单审查（§8.4 API 使用清单）；发现例外则该查询退化为受互斥保护的 view 方法 |
| R-5 | 大模型（7 轴＋多工具＋密集场景）编译时延影响命令串行槽 | 命令吞吐下降 | 编译选项几何细节分级（compileOptions）；实测后由 execution 决定缓存策略（§9.4 已给纯判定） |
| R-6 | 名称消歧序号（`_2` 后缀）与用户既有命名冲突概率非零 | 名称不稳定观感 | 确定性保证同输入同结果；冲突本身再触发消歧规则闭环（字典序覆盖全部冲突集） |
| R-7 | 测试对照 FK（RT-EQ-1 独立小实现）自身错误 | 等价断言误导 | 数据集采用 analytic-case（闭式解推导登记出处，testkit §4.2.1 首选）；RT-STUB-0 边界声明 |
| R-8 | ARCHITECTURE v0.11 Draft 评审（A9+）调整端口/依赖 | §3/§10 返工 | 锚定 §3.5/§7.2/§7.3/§7.4 明文；P-RT-2 登记同步义务 |

### 15.3 待裁决项（问题—依据—影响—建议—需要裁决者）

| # | 问题 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-RT-1 | 本文消费的 core 契约以其 v0.1（Draft）为基线；冻结版可能调整签名 | core.md 文档头状态行 | RT-T02 起返工 | core 冻结时出 diff 清单，本文按影响面增量修订并留痕 | core 详设所有者＋本文所有者 |
| P-RT-2 | ARCHITECTURE v0.11 Draft 待评审 | 架构文档头 | 评审结论可能要求同步（尤其 §7.2⑥/§7.3/§3.5） | 评审后按影响面增量修订 | 架构所有者 |
| P-RT-3 | ARCH §2.3 L2 行"L1 的 RobWork 数学/运动学类型"与 §5.1/§7.3 要求 runtime 构造 WC/DWC（rw::models/rwsim）的措辞差异 | ARCH §2.3 vs §5.1/§7.3 | runtime 的合法 L1 依赖集需权威确认 | 按 §7.3 明文将 rw kinematics/models＋rwsim dynamics 列为 runtime 依赖（零 Qt 基线库）；请架构评审确认 §2.3 措辞覆盖 | 架构所有者 |
| P-RT-4 | 安装预设轴向（倒挂绕 X、壁装绕 Y）为本文选择，MDL-22 未指明 | MDL-22（"预设 180°/90°"无轴向） | 预设矩阵与 modeling 编辑器/用户预期的对齐 | 维持本文定义（§6.2）；modeling 详设起草时交叉核对后冻结，分歧时以建模侧用户口径为准并回改本文 | modeling 详设所有者＋需求侧确认 |
| P-RT-5 | IRobotDesignReader 注入形态 vs modeling 直接解码其 canonical 编码 | 本文 §4.2（D-02） | 阶段 B 集成形态 | 维持注入 reader（解耦 schema）；modeling 详设起草时交叉核对，若其主张直解再评估 | modeling 详设所有者＋本文所有者 |
| P-RT-6 | ResourceBytes 生命周期已由 io §8.6 / P-IO-2 裁决 | 本文 §8.6；io §8.6 | io 实现与跨边界使用 | 已关闭（设计级）：provider 持有稳定缓冲至析构；调用方同步消费、不跨调用长期持有；Recorded 每次重读重算；实现测试待执行 | io 详设所有者（2026-09-10 回接） |
| P-RT-7 | 耦合矩阵"病态"阈值（条件数 ≤1×10⁸）为本文设计默认，MDL-21/DYN-04 只说"病态给诊断并阻止" | MDL-21（M-6/M-12） | 阻止边界 | 维持 1×10⁸ 设计默认并随诊断输出实际条件数；建议后续并入 EngineeringPolicySet（工程策略默认类，附录 D 类别说明）——归 policy 时走其登记 | 需求所有者＋policy 详设所有者 |
| P-RT-8 | project.md §5.3.6 IModelCompilePort 与本文编译入口的适配差异（CompileResult 无快照句柄、无取消参数）已按兼容处理（§10.2） | project.md P-PR-7 点名 runtime 交叉核对 | 阶段 B 命令路径落地 | 请 project 侧确认无增量需求（如命令路径也要快照句柄——本文可提供但需其 CommandResult 扩字段） | project 详设所有者 |
| P-RT-9 | evidence.md §1.2/P-EV-6 登记"project.md 磁盘版本不完整"已过时（2026-09-10 实测完整 §1～§15） | 本文 §1.2 实测 | evidence 侧 P-EV-6 状态应消账 | 请 evidence 详设所有者按磁盘现状更新其登记；本文不受影响（已按完整版承接其 §5.3.6/§10.7） | evidence 详设所有者 |

### 15.4 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-10 | 首版：基于 REQUIREMENTS v1.16（Accepted）与 ARCHITECTURE v0.11（Draft）及 core/testkit/project/evidence 四份协作输入（均 Draft；project.md 磁盘实测完整——与 evidence 侧登记差异见 P-RT-9）完成 15 章详细设计：冻结 CanonicalModel 数据契约与 RT-Codec 身份、十段确定性编译链与事务状态机、基座—世界变换单一不变量（含预设矩阵与数值例）、RuntimeNameMap 双射解析、RobWork 适配层（只读视图/异常转译/显式设值）、RuntimeSnapshot 并发与 worker 隔离、分层编译缓存纯判定、能力正交声明、公共接口（含 IModelCompilePort 承接答复）、验证反例矩阵 RT-\* 41 组、实现任务 RT-T01～T13；待裁决 9 项（P-RT-1～9）。同日：`runtime/include/sdurws/ird/runtime/README.md` 的任务卡指向由"§9"修正为"§12"（与本文任务拆分章节号一致，同 evidence README 修正先例） |
| v0.2 | 2026-09-10 | FOUNDATION-CR-01 契约冻结审查修正（CR-05/CR-07）：§10.3 对 evidence 的供给落点表述更正（modelIdentity/robworkBaselineVersion 入切片 Environment 条目 `runtime.model-identity`/`runtime.robwork-baseline` 并进入 inputBaselineId；复现块仅 compilerContractVersion——原文"§4.1.2 预留字段"不实）；§3.4/RT-T01 行 gtest 引用改指 development-task-breakdown §5.5 定稿。差异记录见 traceability/foundation-api-diff.md |
| v0.3 | 2026-09-10 | 全链一致性审计消账：①§10.11 稳定码由"建议值"更新为"已收编 14 项（diagnostics.md §4.6）"，并冻结枚举对齐说明（UnknownObject/ContextReleased＝fail-fast token 不发码；RT-CAPABILITY-MISSING/RT-ROBWORK-ERROR＝诊断事件码无枚举对应）；②卡头版本 v0.1→v0.3 更正（原卡头停留 v0.1 而变更记录已达 v0.2，与 policy/reporting 卡的 v0.2 引用不符） |
| v0.4 | 2026-09-12 | RT-T03 实现落位登记（DTB §5.4，两处分阶段偏差＋一处机制说明）：①InstallationPresetToken 枚举由 §3.1 原列 BaseWorldTransform.hpp（RT-T06）调整随 §4.2 Description.hpp 落位（BasePlacementDescription.preset 字段类型依赖、RT-T03 先于 RT-T06）——RT-T06 的预设→矩阵纯函数经 include 复用，单一权威不重复定义（§3.1 两行已同步）；②Resource.hpp 的值类型 ResourceState/ResourceRef 随 RT-T03 先行落位（RobotDesignDescription.resourceRefs 字段依赖；§12 无单列任务行，首个消费者原则）——ResourceBytes/ResourceReadError/IRuntimeResourceProvider 随资源消费任务落位（§3.1 行已同步）；③耦合矩阵校验（RT-CPL-1）的奇异值计算采用单侧 Jacobi（Hestenes）SVD——小奇异值相对精度不受条件数放大（两侧法经 CᵀC 会平方化动态范围、奇异/病态无法分档），属实现层选型、契约阈值（1×10⁸／1×10⁻¹²）与语义不变；④构建机制：冒烟模式自 RT-T03 起引入 rw 模板头 header-only include（Description 字段类型＝rw::math，两模式单一类型形态；仍零框架链接）。实现证据见 traceability/builds/wp06-t03/ |
| v0.5 | 2026-09-12 | RT-T04 实现落位登记（DTB §5.4，CanonicalModel.hpp/.cpp＋Codec.hpp/.cpp 随 §12 RT-T04 交付，含一处分阶段说明＋三处实现层选型登记）：①§4.6 三类只读索引中的对象索引与资源索引随本任务落地（builder 构建，O(log n)）；**层级索引**（规范侧 Frame 树路径）依赖 §7.2 名称生成与 §6 层级命名的消费语义，随其消费任务 RT-T05/RT-T07 落位——§3.1/§12 无独立任务行，首个消费者原则；②S5 构造不变量的诊断拒绝面＝§10.11 冻结的十段链硬失败码（经 Errors.hpp registryCode 单一来源取 token：INPUT-INVALID/STRUCTURE-INVALID/UNIT-MISMATCH/RESOURCE-\*/WC-DWC-COMPILE-FAILED/NAME-CONFLICT/BASE-WORLD-INCONSISTENT 十码）——出现即违 MDL-06 原子性（编译失败却试图发布）；Cancelled／RT-CAPABILITY-MISSING／RT-ROBWORK-ERROR／未来警告类码不在拒绝集（码值权威归 diagnostics，不私裁收窄）；③惯量正定复核（S5 防御层）采用 Sylvester 顺序主子式（对称矩阵正定 ⟺ 各阶主子式＞0）——与 S3 的对称 Jacobi 最小特征值＞0 数学等价（物理惯量远离奇异边界，两判据合法域内结论一致），O(1) 确定性无迭代，属实现层选型；④parse（RT-Codec 解码）在 §4.5 往返语义之上叠加三重防御：builder 全量不变量复核＋重算身份与携带值比对＋能力块与派生比对（防篡改/半传输——§9.5 worker 身份核对 D-13 的模型层前置）；⑤RuntimeCapability.hasDynamicWorkCell 的模型层派生口径＝"全部连杆物性 Provided"（DWC Body 集合＝链连杆，工具以工具系/RigidDevice 层消费）——S7 实际构造门控的最终落位在 RT-T08，如有出入按 DTB §5.4 增量对齐；⑥Errors.hpp 的 Expected 工厂实现由"默认构造＋emplace"改为标签构造直接初始化 variant（原实现要求 T 默认可构造，与其 §3.4 注释"工厂支持 move-only 的 T"承诺不符——CanonicalModel 私有默认构造无法入轨；公共签名与两态语义不变，RT-T02 既有用例回归通过，属契约注释既明承诺的实现补齐）。实现证据见 traceability/builds/wp06-t04/（含失败能力探针：身份域排除断言注入即红、精确还原复绿） |
| v0.6 | 2026-09-12 | RT-T05 实现落位登记（DTB §5.4，NameMap.hpp/.cpp 随 §12 RT-T05 交付＋IRDNAME 子形态随 §7.6 在 Codec 增量落位；含一处实现层澄清＋四处设计默认/选型登记）：①**§7.4 反向链的落位口径（实现层澄清，PA-1 下两处规范原文的同时成立方式）**——§7.1 范围表与 §6.3（BaseMount 名"经名称映射登记"）要求派生节点名（BaseMount/BaseFrame/Flange/Tcp/Geometry/Body）作为映射条目存在，而派生条目共享所属对象的 ObjectId；故映射条目分两层：**身份作用域条目**（每对象恰一条：robot→Device、joint→Joint、link→LinkFrame、tool→Tcp、scene→SceneObject、资源→Geometry——resolveObjectId 的返回值来源，AT-18 双向往返在其上逐名回等全量成立）＋**派生条目**（共享 ObjectId，resolveRuntimeName 单向命中并经身份名链闭合）；资源被多处引用时身份条目取映射序首条（确定性）。②消歧后缀格式（`_2`/`_3`…）与数字预算（uint64 十进制递增、无人工上限、溢出即防御性 NameConflict——§7.2"不可消歧"行的理论不可达通道）＝§7.2 设计默认，**待产品确认后冻结**（与 DTB O-12 同源登记，实现不私裁）；保留字最小集 WORLD、合法化规则（非法字符→`_`、前导数字→`n_`、空名→`unnamed`＋警告）按 §7.2 字面实现。③nameMapRuleVersion 初值＝1（规则表初始版本；规则变化＝名称可能变＝新缓存键 §9.4——升版走设计变更评审）。④Geometry 条目的 facet 后缀：连杆 visual/collision 按引用字段取 `visual`/`collision`，工具与场景几何统一 `collision`（几何在碰撞评估消费——实现层选型）。⑤IRDNAME 编码（§7.6）落位 Codec.hpp/.cpp：内容身份＝SHA-256 over 编码自身（含规则版本头），编码内**不嵌摘要字段**（自引用不可行）——防篡改核对以重算身份比对实现（D-13 worker 核对的映射层形态）；parseNameMap 叠加条目数剩余字节预算防线（防伪造 count 触发超大内存预留）。⑥名称消歧/空名警告的**数据承载**＝RuntimeNameNotice（原名/消歧名/对象三元组，§7.2 原文三项）随映射携带——稳定诊断码的码值权威归 diagnostics StableCodeRegistry（PA-1），§10.11 v0.3 冻结清单尚无该警告码，CompileOutcome.diagnostics 的转译落位随编译器任务 RT-T11 与 diagnostics 侧登记。⑦S8 交叉校验以 `crossCheckRuntimeNames(map, actualNames)` 纯函数交付（实际名⊆映射名判定＋未命中逐条回显，违者 RuntimeError(NameConflict)——§5.2 S8 行失败通道）；真实 WC 对象名收集随 RT-T07/T11。⑧R-4 例外实现文件＝NameMap 模块（NameMap.hpp 声明＋NameMap.cpp 的 joinScopeLocal 单点）——清单提交 WP-01-T01 门禁随 RT-T13。实现证据见 traceability/builds/wp06-t05/（双模式 130/130；失败能力探针：禁用前导数字规则注入即红、精确还原复绿） |

### 15.5 交付前自审记录（v0.1；自审≠实现测试≠正式验收）

| 检查项（任务约束§八） | 结论 | 证据位置 |
| --- | --- | --- |
| 是否重复 core/project/evidence/policy 的职责 | ✔ 未重复：身份/摘要/换算/比较全经 core（§3.2）；磁盘/修订/事务/锁归 project（§2.1 N-1，仅只读注入）；快照/切片/结果包络/当前性归 evidence（N-5，仅值传递）；策略/碰撞/阈值归 policy（N-4，编译产物不消费策略） | §2、§3.3、§10 |
| 是否存在反向依赖或 Qt 渗入 | ✔ 编译依赖仅 core＋std＋L1 RobWork 基线（§3.2）；全部对端能力经注入/值传递；零 Qt（RT-T01 扫描承载）；L1 依赖集以 P-RT-3 登记请架构确认，未引入表外 industrialrobot 边 | §3.2、§3.3 |
| 是否出现第二套模型真值、第二套名称解析或第二套基座变换规则 | ✔ CanonicalModel 唯一规范模型（CM-0）；名称解析唯一实现＋R-4 例外登记（§7）；T_world_base 单一存储/单一写入点（§6.3） | §4.1、§6.3、§7 |
| 是否把 RobWork 名称误当作业务身份 | ✔ 名称只是产物标签，业务身份＝ObjectId，互查唯经⑥端口（§8.2）；RT-NM-7 锁定不混用 | §8.2、§11 |
| 是否重复应用基座—世界变换 | ✔ 四消费方读同一 view 读取点；禁止清单五条＋S9 一致性检查＋AT-37 反例（RT-BW-4/5） | §6.3/§6.4/§6.6 |
| 是否把编译失败误判为工程不可行 / 能力缺失误判为输入非法 | ✔ 四维正交表与发布门禁（§5.6/§9.6）；runtime 输出不含任何工程判定字段（RT-CAP-2 类型层断言） | §5.6、§9.6 |
| 是否允许半成品进入缓存或下游 | ✔ 快照唯一发布点 S10；Failed/Cancelled 无快照；缓存判定对未 finalize 输入恒 Incompatible；WC-only 复用不得上报完整命中 | §5.2/§5.3、§9.4 |
| 是否在线程之间错误共享可变 RobWork 对象 | ✔ 发布后只读＋每线程 State＋裸指针禁令（§8.7/§9.2）；TSAN 用例（RT-SNAP-1） | §8.7、§9.2 |
| 是否在项目切换后错误丢弃历史快照 | ✔ 快照零外部依赖、引用计数存活至迟到归档完成（§9.3） | §9.3、RT-SNAP-3 |
| 是否在锁或上下文释放后继续访问项目数据 | ✔ 注入源仅在编译期有效；释放后迟到请求 `ContextReleased` 拒绝 | §9.3、RT-SNAP-3 |
| 是否把显示变化当作计算依赖 | ✔ 显示字段不入 Description/身份（§4.3.6）；RT-ID-3 钉住 | §4.3.6 |
| 是否将缓存命中误当作结果当前 | ✔ 判定表显式分行＋HEAD 前进不失效（§9.4） | §9.4、RT-CACHE-4 |
| 是否越权重定义需求、架构或其他单元机制 | ✔ 无新增状态/阈值（耦合条件数为设计默认并登记 P-RT-7 建议归 policy）；对上游含混处集中登记 P-RT-1～9 不私裁 | §15.3 |
| 后续单元能否据交接清单继续设计 | ✔ 11 类对端均有接收物与待冻结相邻契约（§10/§13.2）；任务指定六个接口全部给出签名与属性表（§7.3/§8.3/§8.6/§9.4/§10.0） | §10、§13 |
| 替身结果是否被当作 RobWork/业务算法正确性证明 | ✔ RT-STUB-0 边界声明；RT-EQ-1 明确对照实现的测试工具属性 | §11 |

## AI 执行就绪补充

本单元进入 AI 实现前必须满足：

- 本文中的职责、非职责、公共接口、数据模型、错误语义和不变量不得与 ARCHITECTURE.md 冲突。
- 单元任务使用 $(runtime.ToUpper())-Txx 编号，并通过 doc/industrial-robot-design/tasks/*.json 声明前置任务、允许修改文件和验证命令。
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
