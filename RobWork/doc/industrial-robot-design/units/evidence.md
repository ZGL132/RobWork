# 工业机械臂设计软件 · evidence 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案） |
| 日期 | 2026-09-09 |
| 状态 | **`Draft-Structured`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收） |
| 文档代号 | UNIT-EVIDENCE |
| 单元 | evidence（平台内核，L2 计算内核层；ARCHITECTURE §2.3/§3.1：AnalysisSnapshot/InputSlice/ResultEnvelope/ResultCurrentness、评估器接口与注册表、证据契约） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md` **v0.1（`Draft`，未冻结）**、`units/testkit.md` **v0.1（`Draft`，未冻结）**、`units/project.md` **v0.1（`Draft`，未冻结）**。本文消费的 core 公共契约以 core.md v0.1 签名为基线并逐项标注状态（§3.2）；协作输入如有变更，本文按影响面增量同步（待裁决 P-EV-1） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立）→ `units/*.md`（单元任务卡）。本文即 `units/evidence.md`，按任务卡深度编写（接口签名、数据类型在本文件冻结） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/evidence/`（骨架已建：目标 `sdurws_ird_evidence`〔INTERFACE 占位〕＋别名 `RWS::ird::evidence`＋公共头保留位 `include/sdurws/ird/evidence/README.md`；见上级 `industrialrobot/CMakeLists.txt`） |
| 任务归属 | 详见 `development-task-breakdown.md` WP-B～WP-I 映射；本文只维护单元内部任务
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 仅功能范围对照且**当前磁盘缺失**（core.md §1.2 R-5 同源登记）；历史实现不作为语义来源 |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 evidence 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 evidence 的职责（§3.1 单元总表行；§7.6 横切管线 A"快照→切片→结果包络→当前性"；§7.2 ③评估器端口），把快照/切片/内容身份、证据 Profile 与工程判定汇总、ResultEnvelope 与结果生命周期、当前性、缓存与检查点兼容判定、评估器接口与注册表的数据类型、公共接口、不变量、跨单元调用流程、验证方案与阶段 A 任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不收窄、不扩大；本文引用的需求 ID 与章节号均指 v1.16 当前正文（重点：CON 家族、TASK、ERR、EVI、§8.1 全部表格、KIN、OPT、RPT 相关条目、附录 D）。架构归属以 `ARCHITECTURE.md` v0.11 为准（SA-07/SA-13 直接约束本单元）；本文对其含混或未登记事项的解释集中登记于 §15.3（P-EV-1～P-EV-9），不私自修改上游。

**工程判定的所有权声明（贯穿全文）**：evidence **汇总**工程判定（实现 §8.1 表 2 的五级优先级与正式通过判定），但**领域证明由各评估器产生**（IK 收敛记录、碰撞证据、覆盖率计数、传动工作点等）；evidence 的汇总器对"确定性不可行证明"做**字段级校验**（类别/作用对象/前提/覆盖范围/输入身份/产生者，§6.4），**绝不**凭结果里的一个状态字段相信"已经证明不可行"。

### 1.2 上游与磁盘现状登记（2026-09-09 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文承接：CON-01～06、TASK-02/03、ERR-01、EVI-01/02、§8.1 表 1～4（P-05 冻结产物）、REQ-06（就绪口径）、KIN-02/04/05/06/12/13、TRJ-02/04、DYN-06/07、OPT-06/08/12、RPT-03/05、NFR-COR-02/04、附录 D（P-01 冻结）、AT-03/05/06/09/10/19/27/32 |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。§3.5 依赖表登记 evidence→core（**唯一 industrialrobot 依赖**）；§7.6 管线、§7.2 ③端口、§4.5 迟到结果两段式、SA-07/SA-13 直接约束本文。若评审产生 A9+ 处置按影响面同步（P-EV-2） |
| `units/core.md` | 存在，v0.1，`Draft`（**未冻结**） | 协作输入。本文消费其身份/摘要/词表/诊断/事件契约（§3.2 逐项登记）；其 §10.3 已向 evidence 交接"切片 canonical 序列化、ResultEnvelope 构造校验器、RequiredEvidenceProfile 承载、证据等级词表（P-AR-3）"——本文 §4/§5/§6/§7 即该交接的承接答复；其 P-D-1 要求 evidence 侧交叉核对评估词表位置（本文 §3.4 回复） |
| `units/testkit.md` | 存在，v0.1，`Draft`（**未冻结**） | 协作输入。本文验证方案（§11）消费其 ContractCheck 谓词/断言宏/TempDir/DeterministicEnv；其 §10.2 已登记 evidence 侧义务（envelope 访问器特化、切片序列化契约夹具数据集）——本文 §11/§13 承接 |
| `units/project.md` | 存在，v0.1，Draft，§1～§15 已齐备 | 2026-09-10 同步：历史磁盘不完整问题已消除；跨单元接口仍按 CR-03 最小契约复核，不因正文齐备而自动 frozen。 |
| `units/evidence.md` | **不存在**（本文新建） | 其余 16 个单元任务卡均未产出；runtime/policy/execution/diagnostics 详设缺失——本文需要其接口处给最小依赖契约并登记交接（§13），不代写对方详设 |
| `DETAILED-DESIGN.md` | 已建立 | 20 个单元详设总目录，本文是单元详设正文 |
| `development-task-breakdown.md` | 已建立 | WP-A～WP-I 主 WP 计划，本文只维护单元任务 |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 单元目录＋`patches/`，共 23 文件（实测一致） | `sdurws_ird_evidence` 为 INTERFACE 占位，无源码；`evidence/include/sdurws/ird/evidence/README.md` 已引用本文（其"§9"指向随本文结构修正为"§12"，见变更记录） |
| `old/` | **不存在于磁盘**（git 亦未跟踪） | 与 REQUIREMENTS v1.10 声明不符（core.md R-5 已登记）；对本文无输入。**附录 B 裁决排除"哈希/指纹校验链（九层复核、双指纹、12 指纹运行快照）"**——本文内容身份机制以 CON-05 明文授权的"切片内容身份"为唯一目标，不恢复任何多重指纹体系（§5.1） |

### 1.3 设计目标

1. **统一契约**：快照、切片、依赖、证据、结果包络、当前性、兼容判定、评估器端口在本文单点冻结；后续业务单元（B/C/D 阶段）依据本文产生、提交和消费证据，无需再协商契约。
2. **失效精准**（CON-05/SA-07）：缓存、当前性与失效基于**切片内容身份**，不基于整个项目修订——电机成本变更不失效运动学/轨迹，TCP 变更失效运动学及下游（AT-05 矩阵原文）；切片身份由**实际消费依赖**决定（§5.3 失效矩阵）。
3. **判定准确**（§8.1 表 2 五级优先级，C2/C5/C6/C8 修订口径）：通用证据门禁先行；确定性不可行仅限三类且证明对象必须是任务级；搜索未果（含全部解因碰撞被过滤）判 DataInsufficient 不判不可行；缺失项全量列出。
4. **三态正交**（CON-02）：结果完整性（outcome）、工程判定（engineeringStatus）、当前性（Current/Superseded）互不替代；Superseded 不改写历史 payload，旧结果保留为原快照历史证据。
5. **零 Qt、零反向依赖**（NFR-MNT-01、ARCH §2.3 L2 行、§3.5 依赖表）：evidence 仅依赖 core＋标准库；project/execution/runtime/policy/业务单元的能力一律经**注入的最小只读接口**或**运行时注册**获得（§3.3），不存在 evidence→他们的编译期边。
6. **不预建空业务**（任务约束§九）：阶段 A 交付通用契约、快照/切片基础设施、结果构造校验、汇总框架、当前性与兼容判定、评估器注册表及**可控测试替身**；不实现任何业务评估器，不伪造完整证据。

### 1.4 语言、标准库与构建约束（按仓库实测确认，与 core.md/project.md §1.4 同源）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | 构建缓存 `build/CMakeCache.txt`：Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计，不做平台特定扩展 |
| Qt | Qt 6.11.1（msvc2022_64） | **evidence 目标禁止包含任何 Qt 头**（构建红线 R-3，ARCH §3.2；L2 计算内核零 Qt） |
| C++ 标准 | 基线 RobWork `CMAKE_CXX_STANDARD 11`；industrialrobot 目标显式 C++17（core.md D-01） | 同口径：显式 `cxx_std_17`；`std::optional/variant/string_view` 允许；不用 C++20；不引入 Boost 等第三方运行库 |
| RobWork 数学 | `rw::math`（`sdurw_math`，经 core 公共头传递） | evidence **不直接包含** rw::math/Eigen 头（本单元契约不含几何数值类型；数值仅经 core Tolerance/字节承载） |
| 异常 | RobWork 惯例异常 | evidence 用自有异常类型 `EvidenceError`（§3.5）；可恢复查询路径提供 `try*` 非抛出变体 |
| JSON/序列化 | 无共享 JSON 库（vcpkg installed 实测无，testkit.md §1.4） | evidence 的 canonical 编码为**自有二进制编码**（SliceCodec/SnapshotCodec，§5.2）——不依赖 JSON，测试侧 JsonLite 不用于产品契约（testkit D-02 边界同精神） |

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有总表（本节先行给出，全文以此为准）

**evidence 拥有（实现责任方）**：

| # | 能力 | 上游依据 | 设计落点 |
| --- | --- | --- | --- |
| O-1 | AnalysisSnapshot 类型、组装不变量、冻结与完整性校验、canonical 序列化 | CON-01/03/06、ARCH §7.6 | §4.1、§4.2 |
| O-2 | InputSlice 与依赖清单（依赖声明结构、条件依赖闭包校验、冻结） | CON-05/06、KIN-13 | §4.3、§5.3 |
| O-3 | 切片/快照内容身份与 canonical 编码（SliceCodec/SnapshotCodec） | CON-05、SA-07、core.md §4.2 责任边界 | §5.1、§5.2 |
| O-4 | RequiredEvidenceProfile 的**数据结构与消费**、通用必需项、证据清单（EvidenceManifest） | EVI-01、§8.1 表 4（P-05） | §6.1～§6.3 |
| O-5 | 工程判定汇总（五级优先级决策表、不可行证明契约与校验、搜索未果口径、Must/Should、多层级汇总） | §8.1 表 2（C2/C5/C6/C8）、EVI-01/02、REQ-06 | §6.4、§6.5 |
| O-6 | 多工况覆盖矩阵契约与区域采样证据表达（覆盖契约检查；覆盖率**算法**归 kinematics） | EVI-02、KIN-04（R8）、DYN-07 | §6.6 |
| O-7 | ResultEnvelope 数据契约与构造边界校验（SA-13） | TASK-02、§8.1 表 3 | §7 |
| O-8 | ResultCurrentness 计算（纯投影） | CON-02/05、ARCH §7.6 | §8.1 |
| O-9 | 缓存复用与检查点兼容的**纯判定契约** | CON-04、OPT-06 | §8.2 |
| O-10 | IEngineeringEvaluator 接口、评估器注册表（③评估器端口）、Profile 注册与注册期验证 | EVI-01、ARC-02、ARCH §7.2③/§7.10 | §9 |

**evidence 消费（经依赖白名单与注入）**：

| 消费方 → 被消费 | 形态 | 消费内容 | 上游依据 | 状态 |
| --- | --- | --- | --- | --- |
| evidence → core | 接口依赖（编译链接，**唯一** industrialrobot 边） | 身份类型全族、ContentVersion/ContentIdentity/ContentDigester、SourcedValue、Tolerance、EvaluationMode/TaskOutcome/EngineeringStatus 词表、DiagnosticRecord/ComparativeFields、IDomainEventBus（订阅方）、CoreError | ARCH §3.5 表 | core.md v0.1 **Draft 未冻结**（P-EV-1） |
| evidence →（注入）project 只读能力 | **运行时注入**（零编译依赖） | 对象字节与修订闭包查询，经本文最小接口 `IObjectBytesSource`/`IRevisionClosureSource`（适配 project ②端口；适配器归 L5 装配/请求方，§3.3） | ARCH §3.5（表未登记 evidence→project 边；PA-3 经快照） | project.md §5.2 已有 IProjectQueryPort；其磁盘版本不完整（P-EV-6） |
| evidence →（注入）policy / runtime 内容身份 | 值传递（core ContentIdentity） | 已解析 EngineeringPolicySet 内容身份、RuntimeNameMap 内容身份——由组装方随快照传入；evidence 不解析策略、不生成名称映射 | CON-06、ARCH §7.5/§7.4 | policy/runtime 详设未产出（§13 交接） |
| evidence →（注册）业务评估器 | 运行时注册（L5 装配） | IEngineeringEvaluator 各域实现（阶段 B/C/D 接入） | ARC-02、ARCH §7.10 | 未产出（本文提供注册表与测试替身） |
| evidence →（订阅）领域事件 | 运行时注入总线实例 | RevisionCommitted/DependencyInvalidated 驱动当前性重算（事件不携带数据，core D-09；重算经②端口取数） | ARCH §7.1 | 总线实现归 execution·ui（core.md §4.9） |

**evidence 不拥有（其他单元所有，本文不实现、不预建桩）**：

| # | 不拥有内容 | 所有者 | 上游依据 |
| --- | --- | --- | --- |
| N-1 | 项目文件读写、事务、锁与修订提交；results/checkpoints 磁盘布局与写入 | project | §17 表注、ARCH §6 |
| N-2 | RunRegistry、任务派发、进程管理、取消状态机、迟到结果五元组核对 | execution | TASK-01～03、ARCH §4.5 |
| N-3 | 工程策略解析实现与碰撞算法（EngineeringPolicySet 内容、CollisionEvaluator） | policy | ARC-05、ARCH §7.5 |
| N-4 | RuntimeNameMap 生成/反解实现与模型编译（CanonicalModel） | runtime | ARC-03/04、ARCH §7.3/§7.4 |
| N-5 | FK/IK、轨迹、动力学、选型及优化算法与领域对象 schema（RobotDesign/任务点/工况/AnalysisConfiguration 字段） | kinematics/trajectory/dynamics/selection/optimization 及 modeling/requirements | KIN/TRJ/DYN/SEL/OPT 家族 |
| N-6 | 缓存存储、淘汰、检查点写入和恢复调度 | execution（治理）/project（磁盘） | CON-04、ARCH §3.1 execution 行 |
| N-7 | 报告渲染、界面状态呈现和日志设施 | reporting/ui/diagnostics | RPT、UX、ERR-01 |
| N-8 | 区域覆盖率/姿态覆盖率的**数值算法**（存在性/全局口径的计数与计算） | kinematics | KIN-04（R3/R8） |
| N-9 | 需求就绪校验的业务规则（Must/Should 条目合法性判定） | requirements | REQ-06 |

### 2.2 直接承接的需求（evidence 为实现责任方或数据契约责任方之一）

| 需求 | evidence 承接的部分 | 不可越界的部分 |
| --- | --- | --- |
| CON-01（分析从完整不可变快照运行） | AnalysisSnapshot 类型、完整性校验、不可变保证 | 快照的持久化编址（project）；对象字节读取（project ②端口） |
| CON-02（完整性/当前性/工程判定正交；旧结果保留历史证据） | 三轴正交的承载与强制（envelope 组合表＋currentness 独立投影）；历史 payload 不可变约定 | 结果生命周期事件（execution） |
| CON-03（Verified 引用外部资源须项目内不可变副本） | 快照记录外部资源固化状态；Verified 模式前置校验（未固化→阻断正式结论的证据不足口径） | 固化副本的存储发布（project §5.7）；检测（io） |
| CON-04（缓存/检查点按契约判断命中与兼容；部分/失败结果不得作正式缓存命中） | 纯判定接口 `judgeCacheHit`/`judgeCheckpointCompatibility`（§8.2） | 缓存/检查点的存储、淘汰、写入、恢复执行（execution/project） |
| CON-05（切片与依赖清单；基于切片内容身份） | InputSlice、依赖清单、切片内容身份、失效矩阵语义、当前性按内容身份比较 | 各域"哪个评估实际消费哪些对象"的声明内容（评估器 descriptor 提供，域所有） |
| CON-06（快照保存已解析策略与 RuntimeNameMap 内容身份；名称先反解后接纳；跨入口一致） | 快照字段与校验；证据/载荷引用 ObjectId 的契约（runtimeName 仅辅助显示）；同一快照→同一判定输入 | 策略解析（policy）；名称映射（runtime）；反解执行（接纳流程归 execution，经⑥端口） |
| TASK-02/§8.1 表 3（合法组合构造边界拒绝） | ResultEnvelope::make 校验器（SA-13） | 任务状态机（execution） |
| TASK-03（身份五元组；旧任务不得成为其他会话当前结果） | TaskIdentity 承载与有效性校验；当前性上下文绑定（项目/分支/方案） | 五元组核对与登记（execution §4.5） |
| ERR-01（三轴正交；诊断字段） | envelope/证据/判定中的 DiagnosticRecord 构造（经 core 契约）；NotApplicable 显式标记不伪造数值 | 诊断码表与文案权威（diagnostics）；显示（ui） |
| EVI-01/§8.1 全表（评估模式、证据等级、Profile 契约、正式判定全局唯一） | 模式承载与效力强制（Preview 无结果对象、Quick 不支撑正式通过）；Profile 结构与消费；汇总与正式通过判定五条件的**承载与判定** | 各域 Profile **明细内容**（按表 4 由域登记）；领域证据的产生（评估器） |
| EVI-02（必验工况全覆盖；一致基准比较） | 必验工况集冻结、覆盖矩阵契约与校验、比较基准一致性纯检查 | 工况定义 schema（requirements）；各域执行（评估器） |
| KIN-04 相关（样本冻结/分母/降级/零样本） | 采样计划引用与样本集身份、区域覆盖**证据表达**契约（分母完整性、零样本、降级标记）与契约检查 | 覆盖率算法与样本生成（kinematics） |
| KIN-13（AnalysisConfiguration 进入运行身份与缓存身份；按依赖提示重算） | 配置经 Configuration 依赖条目进入切片身份；重算提示的数据来源（失效原因清单） | 配置 schema 与持久化（kinematics/PM-14） |
| OPT-06（Quick 筛选/Verified 复核/缓存/种子） | 模式与兼容判定契约（Quick 结果不得当 Verified 证据——缓存判定层面拒绝） | 优化执行与缓存实例（optimization/execution） |
| RPT-03（证据包导出要素） | 快照/切片/结果引用与复现要素（版本、种子、线程、容差）的数据契约 | 打包与渲染（reporting/io） |
| RPT-05（两类声明口径） | FormalPassEligibility 与 ReviewRecordEligibility 两个独立纯检查（§7.5） | 措辞与渲染（reporting） |
| NFR-COR-02（同输入+版本+配置+种子+线程→等价结果） | 身份要素（软件/契约版本、种子、线程、配置）全部进入切片身份；确定性纯函数 | 端到端确定性（各域+execution） |
| NFR-COR-04（结论定位到快照与证据） | envelope→sliceId→snapshotId→对象引用的追溯链 | 报告呈现（reporting） |

### 2.3 明确不做（非目标，与 §2.1 不拥有表互为补充）

不实现第二套数值容差或比较公式（core 唯一）；不引入被附录 B 裁决排除的多重指纹/九层复核体系（内容摘要只用于 CON-05 授权的切片/快照/证据内容身份）；不新增评估模式或证据等级（§8.1 表 1 之外不发明冻结枚举——实现承载词表的边界见 §6.2 与 P-EV-8）；不调整任何容差、阈值、工程判定优先级或发布范围（附录 D/表 2 为准）；不在评估器注册表内另建任务调度器、取消通道或进度设施（IEvaluationContext 仅是评估器调用约定的一部分，实现归 execution）；不创建任何"仅转发调用"的逐业务域包装器（NFR-MNT-04——域直接实现 IEngineeringEvaluator）；不伪造完整证据或预建空业务算法（阶段 B/C/D 才接入真实评估器）。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 组成（公共头模块）

evidence 由 10 个公共头模块＋1 个编译实现组成（实现文件随 §12 任务落地，头为契约权威）：

| 头（`evidence/include/sdurws/ird/evidence/`） | 内容 | 详见 |
| --- | --- | --- |
| `Errors.hpp` | EvidenceErrorCode（稳定 token）、EvidenceError | §3.5 |
| `Dependency.hpp` | DependencyKey/Kind、DependencyDeclaration、ApplicabilityCondition、DependencyEntry、UpstreamResultRef | §4.3、§5.3 |
| `Snapshot.hpp` | CaseId/RequiredCaseSet、SamplingPlanRef、ExternalResourceState、ReproductionBlock、AnalysisSnapshot、IObjectBytesSource、IRevisionClosureSource、SnapshotBuilder、SnapshotCodec | §4.1、§4.2 |
| `Slice.hpp` | InputSlice、SliceId、InputBaselineId、SliceBuilder、SliceCodec | §4.3、§5.1、§5.2 |
| `Evidence.hpp` | EvidenceItemStatus/Item/Manifest、RequiredEvidenceProfile 族、CommonRequiredItems、DeterministicInfeasibilityProof/ProofCategory、SearchExhaustedRecord、CaseCoverageMatrix、RegionCoverageEvidence | §6 |
| `Verdict.hpp` | ReadinessSummary、DomainVerdictInputs、aggregateVerdict、VerdictTrace、FormalPassEligibility、ReviewRecordEligibility、ComparisonBaselineCheck | §6.4、§6.5、§7.5 |
| `Envelope.hpp` | ResultEnvelope、ResultEnvelopeDraft、DomainPayload、PartialDataRef、validateCombination | §7 |
| `Currentness.hpp` | CurrentnessStatus、InvalidationReason、CurrentnessQuery/Result、computeCurrentness、CurrentnessIndex | §8.1 |
| `Compatibility.hpp` | CacheHitQuery/Result、CheckpointCompatibilityQuery/Result | §8.2 |
| `Evaluator.hpp` | EvaluationKey、EvaluatorDescriptor、IEvaluatorFactory、IEngineeringEvaluator、EvaluationRequest/Output、IEvaluationContext、EvaluatorRegistry、RegistrationManifest、EvidenceProfileRegistry | §9 |
| `README.md` | 既有保留位说明（不参与编译；已指向本文 §12） | — |
| `src/`（实现，随 EV-T01 建） | canonical 编码器、快照/切片构建、汇总、注册表等非模板实现 | §4～§9 |

### 3.2 依赖（含 core 契约消费状态登记）

```
sdurws_ird_evidence ──► RWS::ird::core（PUBLIC：身份/摘要/词表/诊断数据/事件接口）
                    ──► C++17 标准库
                    ──✖ 零 Qt（R-3）、零 Eigen/rw::math 直接包含、零其他 industrialrobot 单元链接
运行时注入（零编译依赖）：IObjectBytesSource/IRevisionClosureSource（适配 project ②端口）、
                    事件总线实例（execution·ui 实现）、各域评估器与 Profile（L5 装配注册）
```

消费的 core 契约清单（全部来自 core.md v0.1 §4/§5；**状态：Draft 未冻结**，P-EV-1）：

| core 契约 | evidence 用途 | 状态锚点 |
| --- | --- | --- |
| ObjectId/ProjectId/BranchId/RevisionId/RunId/AttemptId/TaskIdentity | 快照身份块、结果绑定、当前性上下文 | v0.1 §4.1/§5.1 |
| ContentVersion/ContentIdentity/ContentDigester | 切片/快照/证据/Profile 内容身份（对"什么字节做摘要"归本文 SliceCodec/SnapshotCodec——core §4.2 责任边界的 evidence 侧承接） | v0.1 §4.2/§5.2 |
| EvaluationMode/TaskOutcome/EngineeringStatus | envelope/评估请求/汇总承载（词表归 core，P-D-1 交叉核对结论见 §3.4） | v0.1 §4.7/§5.6 |
| DiagnosticRecord/ComparativeFields/SourcedValue | 缺失/不适用/比较型诊断承载 | v0.1 §4.8/§5.7 |
| Tolerance/closeWithin | 本单元不直接做数值判定，仅为 Profile/复现要素承载（阈值数值归各所有者） | v0.1 §4.5 |
| IDomainEventBus/DomainEvent | 当前性重算的事件触发（订阅方；事件不携带数据） | v0.1 §4.9/§5.8 |
| CoreError | core 抛错捕获与转发 | v0.1 §4.10 |

### 3.3 对 project/policy/runtime 能力的注入边界（依赖白名单的实施形态）

ARCH §3.5 依赖表**未登记** evidence→project/policy/runtime 边（这三者与 evidence 同属 L2/L3 且互为平级/上层），而快照组装客观上需要读取修订对象、策略内容身份与名称映射内容身份。解决方案＝**值传递＋最小注入接口**，与 project.md 对 runtime 的 `IModelCompilePort` 同一模式：

```cpp
// Snapshot.hpp —— evidence 定义的最小只读接口（适配器归 L5 装配或请求方，不归 evidence）
class IObjectBytesSource {          // 适配 project ②端口 tryObject(object, contentVersion)
public:
    virtual ~IObjectBytesSource() = default;
    virtual std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId, core::ContentVersion) const = 0;
};
class IRevisionClosureSource {      // 适配 project ②端口 revision()/head() 的修订对象闭包
public:
    virtual ~IRevisionClosureSource() = default;
    virtual bool objectInRevision(core::RevisionId, core::ObjectId,
                                  core::ContentVersion) const = 0;
};
```

- **适配器位置**：L5 应用壳装配期提供一个共享适配器（推荐，避免每个请求方重复适配——NFR-MNT-04），或由已依赖 project 的请求方（业务域插件，L4→L3 合法）自行构造；适配器约十行，不是"仅转发调用无边界价值的包装器"（其价值＝跨层依赖方向的隔离，ARC-02 端口精神的实施件）。
- **策略与名称映射**：组装方经④/⑥端口取得**已解析**策略与 RuntimeNameMap 的内容身份（core ContentIdentity 值）后随快照条目传入；evidence 永不解析策略内容、永不拼接/剥离名称（R-4）。
- **worker 进程**：工作进程内的 IObjectBytesSource 实现为"随请求物化的快照载荷存储"（execution 序列化快照后装配），同样零 project 依赖。

### 3.4 命名空间、目标与 CMake 集成

- 命名空间 `sdurws::ird::evidence`；目标 `sdurws_ird_evidence`（骨架 INTERFACE → EV-T01 升级 STATIC），别名 `RWS::ird::evidence`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_evidence PUBLIC RWS::ird::core)`。
- 测试目标：`sdurws_ird_evidence_test`（单元内，含可控评估器替身）、`sdurws_ird_evidence_contract_test`（跨单元契约：与 core 值类型、与 project 查询适配器、与 testkit 谓词的协作；gtest 接入按 development-task-breakdown.md §5.5 定稿——vcpkg＋`find_package(GTest CONFIG REQUIRED)`，失败即停），随 EV-T01 登记；产品目标不链 testkit（T-1 红线）。
- 头包含形式 `#include <sdurws/ird/evidence/Slice.hpp>`；私有实现头不入 `include/`（R-2 纪律）。
- **对 core.md P-D-1 的交叉核对回复**：四评估词表（EvaluationMode/TaskOutcome/EngineeringStatus/TaskState）维持归 core——本文消费且不重复定义；diagnostics 仅依赖 core 也能引用 EngineeringStatus 表达三轴正交（ERR-01），core 的拓扑论证成立，evidence 侧无异议。**对 P-AR-3 的回复**：需求全文除 §8.1 表 1"评估模式/证据效力"外无独立"证据等级"枚举定义（OPT-06 的 Quick/Verified 与表 1 一致），本文按"证据等级＝评估模式的效力分层"落地，**不新增等级词表**；Quick 产物在 Verified 判定中的地位以 EvidenceItemStatus::Unverified 表达（§6.2，实现承载非冻结契约，登记 P-EV-8）。

---

## 4. AnalysisSnapshot 与 InputSlice

### 4.1 AnalysisSnapshot

#### 4.1.1 五个"输入/身份"概念的区别（先于字段表冻结）

| 概念 | 类型/载体 | 回答的问题 | 谁产生 | 与其他概念的边界 |
| --- | --- | --- | --- | --- |
| 未应用草稿预览输入 | 域草稿内存态（modeling/requirements 等） | "如果应用这些修改会怎样" | 各业务域编辑器 | **不是** AnalysisSnapshot：Preview 模式不产生正式证据与结果对象（§8.1 表 1）；预览输入不进入修订闭包、无内容身份承诺，不得冒充快照 |
| 正式分析快照 | `AnalysisSnapshot`（本节） | "本次正式评估消费的完整不可变输入闭包是什么" | 请求方组装、evidence 校验冻结 | 含修订身份＋对象引用闭包＋配置引用＋策略/名称映射内容身份＋工况集＋采样计划＋复现块 |
| 规范模型引用 | CanonicalModel（runtime 编译产物） | "计算实际使用的 SE(3) 链" | runtime 自快照对象确定性编译（ARCH §7.3） | **瞬态派生**，不入快照存储；其内容身份＝`RuntimeSnapshot.modelIdentity`（runtime 计算，组成含对象闭包、编译器契约版本、编译器版本、RobWork 基线版本等——runtime.md §4.5/§9.1），由组装方值传递录入切片 Environment 条目 `runtime.model-identity` 参与 sliceId；evidence **不自行重算/重编码** CanonicalModel（CR-05 裁决，traceability/foundation-api-diff.md；ARC-03 确定性由 runtime 侧保证） |
| 运行尝试身份 | `core::TaskIdentity` 五元组 | "哪一次派发/尝试" | execution（RunId/AttemptId 分配） | 快照不含 RunId——派发时绑定；同一切片可多次运行（重试新 attempt） |
| 实际执行环境 | worker 主机/进程事实（run manifest 记录） | "在哪算的" | execution | 声明性要素（种子、线程数——NFR-COR-02 确定性要素）进切片；观测性要素（耗时、机器）不进切片、不进判定 |

#### 4.1.2 字段表（`Snapshot.hpp`；全部字段构造后不可变，无 setter）

| 字段 | 类型 | 必填 | 默认 | 约束与语义 |
| --- | --- | --- | --- | --- |
| `project / branch / revision` | core::ProjectId / BranchId / RevisionId | 是 | — | 身份三元组；revision 是**唯一**对象解析锚（见下"防混入"） |
| `revisionSeq` | std::uint64_t | 是 | — | 项目内修订序号（project 分配；仅展示/排序，不参与内容身份语义判断） |
| `caseSet` | RequiredCaseSet | 是 | — | 冻结的必验工况集合（§4.1.3） |
| `objectClosure` | std::vector\<ObjectRefEntry\> | 是（≥1） | — | 对象引用闭包：{objectId, contentVersion, objectTypeToken, digest}——**引用**而非副本；载荷物化经 IObjectBytesSource（§4.2） |
| `configurationRefs` | std::vector\<ConfigEntry\> | 是（可空集） | {} | 分析配置引用：{configKindToken, canonicalBytes, contentIdentity}——AnalysisConfiguration（KIN-13）等配置的**不透明**canonical 承载（schema 归 kinematics 等域）；内容身份由 evidence 计算 |
| `policyRef` | PolicyRef | 是 | — | {policyContentIdentity}——已解析 EngineeringPolicySet 内容身份（CON-06；解析归 policy，组装方传入） |
| `nameMapRef` | NameMapRef | 是 | — | {nameMapContentIdentity}——执行时 RuntimeNameMap 内容身份（CON-06；生成归 runtime） |
| `externalResources` | std::vector\<ExternalResourceState\> | 否 | {} | {resourceId(ObjectId), state: Recorded\|Solidified, solidifiedContentVersion?}——外部资源固化状态（CON-03/PM-01 三段边界的快照侧记录）；Verified 前置校验消费（§6.2） |
| `samplingPlans` | std::vector\<SamplingPlanRef\> | 否 | {} | 冻结采样计划引用（§4.1.4） |
| `reproduction` | ReproductionBlock | 是 | — | 复现要素：{productVersion, evidenceContractVersion, codecVersions, compilerContractVersion?（消费 CanonicalModel 的评估必填）, collisionBackendVersion?（策略启用碰撞的评估必填）}——"软件、算法或契约版本等必要复现信息"（NFR-COR-02/RPT-03）；这些版本作为 Environment 依赖进入切片身份（§5.3） |
| `snapshotId` | core::ContentIdentity | 是 | — | 快照内容身份＝SHA-256 over SnapshotCodec(refs-only 形态)（§5.2）；由 builder 计算，非调用方申报 |

合法实例：分支 tip 修订 r5 上组装、含 3 个启用必验工况、对象闭包全部 ∈ r5 修订清单、策略与名称映射内容身份非空、复现块完整——`snapshotId` 非零。
非法实例（builder 拒绝，抛 EvidenceError）：对象引用 (oid,cv) 不属于 revision 的修订清单（混入其他修订数据）；policyRef/nameMapRef 内容身份为空（CON-06 违反）；caseSet 含重复工况 id；Verified 前置校验时存在 state==Recorded 的被消费外部资源（CON-03 阻断——以 `validateForMode(Verified)` 表达，见 §6.2）。

#### 4.1.3 RequiredCaseSet（必验工况集合的冻结）

| 字段 | 类型 | 必填 | 约束 |
| --- | --- | --- | --- |
| `entries[]` | {caseId(core::ObjectId), label, enabled, mandatory(必验标记)} | 是 | caseId 唯一（重复→builder 拒绝）；工况对象 schema 归 requirements（REQ-04，P-EV-7）；enabled＋mandatory 由需求对象在快照冻结时解析 |
| `requiredCaseSetId` | core::ContentIdentity | 是 | entries 规范编码摘要（冻结凭据：覆盖矩阵与此核对，§6.6） |

#### 4.1.4 SamplingPlanRef（冻结采样计划与样本集表达）

| 字段 | 类型 | 必填 | 约束 |
| --- | --- | --- | --- |
| `regionObjectId` | core::ObjectId | 是 | 工作区域对象（REQ-03，requirements schema） |
| `planContentIdentity` | core::ContentIdentity | 是 | 采样计划（区域定义+采样参数）canonical 内容身份 |
| `plannedPositionSamples / plannedPoseSamples` | std::uint64_t | 是 | 计划样本数（**分母来源**，KIN-04 R8：分母＝计划样本总数）；0 合法（零样本场景，§6.6） |
| `sampleSetIdentity` | core::ContentIdentity | 是 | 样本集身份＝SHA-256 over (planContentIdentity ‖ 采样预算/种子参数 canonical)。样本集由采样计划确定性生成（KIN-04），**身份对计划参数计算而非对枚举列表**（确定性生成保证同计划同样本集；大样本集不枚举入快照） |

语义：`sampleSetIdentity` 随快照冻结即"复评不得增删更换样本"的凭据——任何改变样本集的输入（区域、采样定义、预算、种子）都会改变该身份；EVI-02/RPT-04 的比较基准一致性检查消费它（§6.5）。**"同一冻结样本集"不等于"所有配置永远不允许改变"**：求解类配置（IK 初值策略、迭代上限）可改——它们改变切片身份（D-04：进入 sliceId、不进入 inputBaselineId），样本基准保持不变（§5.1 双层身份正是为此设计）。

#### 4.1.5 组装、一致读取与冻结协议（谁组装/如何一致/何时冻结/如何验证/如何防混入）

```
组装发起：请求正式评估的域插件（kinematics/…，经②④⑥端口取数）或 execution 派发前准备段。
  Preview 输入不走本协议（§4.1.1），不产生 AnalysisSnapshot。

SnapshotBuilder 协议（单线程、有限时长）：
 ①锚定修订：组装开始时取一次目标 RevisionView（tip 或指定修订）→ 之后一切对象解析只对该修订
    —— 一致读取视图：修订不可变（PA-2），组装期间 HEAD 前进不影响已取视图；新修订数据不可能"混入"
    （所有 (oid,cv) 必须通过 IRevisionClosureSource.objectInRevision(revision, oid, cv) 校验，失败即拒绝）
 ②逐类录入：对象闭包（含类型 token）、工况集、配置引用、策略/名称映射内容身份、外部资源状态、
    采样计划、复现块
 ③冻结：build() 一次性计算 snapshotId（refs-only canonical 编码摘要）→ 返回不可变 AnalysisSnapshot；
    此后无任何修改途径（值语义＋无 setter）。冻结时刻＝build() 返回时。
 ④完整性验证（两个层级）：
    a. builder 即时验证：修订闭包包含性（防混入）、字段完整性（CON-06 非空）、工况唯一性、
       采样计划参数非负、外部资源状态机合法（Recorded→Solidified 单向）；
    b. 载荷级校验（惰性）：物化对象字节时重算 SHA-256 与 contentVersion 比对（core ContentDigester），
       不符→EvidenceError(snapshot-integrity)——检测传输/磁盘损坏（NFR-COR-03 不静默通过）。
 ⑤Worker 投递：SnapshotCodec 支持 refs-only 与 materialized（含被评估切片的对象字节）两种形态；
    两种形态的 snapshotId 相同（身份取 refs-only——载荷字节已由 contentVersion 承诺，物化不改变身份）。
```

### 4.2 InputSlice 与依赖清单

#### 4.2.1 依赖表达模型（对象级为主、字段语义经角色键承载）

**粒度选择（设计决策 D-03）**：依赖以**对象级**（ObjectId+ContentVersion）为基本粒度，配**语义角色键**（DependencyKey，如 `model.robot-design`、`task-points`、`collision-models`、`catalog.motor-capability`）标注消费用途；不做字段级依赖（对象不可变＋内容寻址使对象级即精确——同一对象内改任一字段必然产生新 ContentVersion；字段级清单会把各域 schema 知识搬进 evidence，违反 N-5/N-9 边界）。**实际消费输入与未消费输入的区分由此自然成立**：切片只含被声明的条目，未消费对象（如电机成本字段所在对象对运动学）根本不进入切片——这正是 AT-05 失效矩阵的实现基础。

| 类型（`Dependency.hpp`） | 字段/值 | 语义 |
| --- | --- | --- |
| `DependencyKey` | string，语法 `[a-z][a-z0-9.-]{2,63}` | 语义角色 token；**词表归各域评估器声明**（注册时登记，跨域唯一性由注册表校验） |
| `DependencyKind` | `Object / Configuration / Policy / NameMap / SampleSet / UpstreamResult / Environment` | 七类依赖（见下表） |
| `DependencyDeclaration`（评估器声明） | {key, kind, requiredness: Required\|Conditional, applicability?: ApplicabilityCondition, resolutionNote} | 依赖声明——**由评估器 descriptor 提供**（§9.2），evidence 在注册期验证（§5.3） |
| `ApplicabilityCondition` | {conditionToken, referencedKeys[]} | 条件依赖的适用条件：conditionToken 归域登记；referencedKeys＝**决定该条件的依赖键**——闭包校验对象（§5.3 防漏声明） |
| `DependencyEntry`（切片条目，冻结结果） | {key, kind, …按 kind 的载荷…, applied: bool, notAppliedReason?} | 冻结进切片的实际条目；Conditional 条目记录解析结果（applied 与否）——**未适用的已声明条件依赖保留在切片中**（带 notAppliedReason），其条件输入仍在身份里 |
| `UpstreamResultRef` | {upstreamKey, upstreamSliceId, upstreamRunId?} | 上游结果依赖（如优化消费运动学结果）：跨运行依赖，当前性传播消费（§8.1） |

七类依赖的载荷与进入方式：

| Kind | 载荷 | 策略/算法配置/目录/资源版本如何进入切片 |
| --- | --- | --- |
| Object | (ObjectId, ContentVersion, objectTypeToken) | 模型对象、任务点/区域/工况对象、碰撞几何、负载、摩擦参数对象、目录锁定版本引用对象——各自作为独立角色键 |
| Configuration | (configKindToken, canonicalBytes, contentIdentity) | AnalysisConfiguration 的相关子集（IK 初值策略/数量、迭代上限、容差、去重阈值、采样预算与线程、随机种子、线程数）——**求解随机种子、线程配置与预算的身份归属＝Configuration 依赖条目**（KIN-13：进入运行身份与缓存身份；NFR-COR-02 确定性要素） |
| Policy | policyContentIdentity | 已解析 EngineeringPolicySet 内容身份（CON-06：策略进入依赖切片与缓存键）——碰撞启用/阈值/行程上限等一切策略状态都包含在该身份内（KIN-13：分析配置不得覆盖策略，因此无需第二字段） |
| NameMap | nameMapContentIdentity | RuntimeNameMap 内容身份（CON-06） |
| SampleSet | sampleSetIdentity（+regionObjectId） | 冻结采样计划身份（§4.1.4） |
| UpstreamResult | UpstreamResultRef | 上游结果切片身份（优化组合，ARCH §7.10） |
| Environment | (token, valueToken) | 复现块的版本要素（产品版本/评估契约版本/编译器契约版本/碰撞后端版本/编码器版本）＋runtime 供给身份要素：`runtime.model-identity`（值＝RuntimeSnapshot.modelIdentity 规范文本）、`runtime.robwork-baseline`（值＝robworkBaselineVersion）——消费 CanonicalModel 的评估**必填**后两项（CR-05；经组装方值传递取得，evidence 不重算）；"算法/契约版本兼容"的载体（CON-04） |

#### 4.2.2 InputSlice 字段表

| 字段 | 类型 | 必填 | 约束 |
| --- | --- | --- | --- |
| `evaluationKey / evaluatorContractVersion` | EvaluationKey / uint32 | 是 | 切片绑定的评估器与契约版本（进入身份——同输入不同契约版本＝不同切片，CON-04） |
| `entries` | std::vector\<DependencyEntry\> | 是（≥1） | 冻结的依赖清单（按 (kind,key) 字典序稳定存储）；每条 Object 条目必须能在快照 objectClosure 中找到（子集校验，防漏声明错配） |
| `sliceId` | core::ContentIdentity | 是 | 切片内容身份（§5.1）——缓存键与失效判据（CON-05） |
| `inputBaselineId` | core::ContentIdentity | 是 | **输入基准身份**（§5.1 D-04）：仅模型/需求/工况集/冻结样本集条目参与，排除求解类配置——"同一冻结输入复评"（C5）与比较基准（EVI-02）的凭据 |
| `snapshotId` | core::ContentIdentity | 是 | 来源快照（切片 ⊆ 快照内容） |

#### 4.2.3 声明—解析—冻结协议与条件依赖

```
谁提供：评估器（descriptor.inputs，域所有）声明"本评估消费什么"（直接依赖）；
        派生依赖（如"TCP 改变→IK 目标改变"）由域在对象图上的声明链表达——evidence 不推导对象间
        语义关系（N-5），只承载声明并计算身份。
谁验证：①注册期（EvaluatorRegistry）：声明语法、kind 载荷合法、Conditional 条目的 referencedKeys
          ⊆ 同一 descriptor 已声明键 ∪ 快照事实键（policy/nameMap/caseSet）——闭包规则，防漏声明；
        ②冻结期（SliceBuilder）：解析每条声明的实际条目（请求方提供解析结果或经 IObjectBytesSource
          逐条取引用）；Conditional 条目解析 applied/notAppliedReason；Object 条目做快照子集校验。
何时冻结：切片在**评估派发前**冻结（SliceBuilder.build() 计算 sliceId 后不可变）；
        快照在组装时冻结；运行身份在派发时绑定（五元组）。
三者关系：snapshotId ⊇ sliceId（内容包含）；sliceId ⊇ inputBaselineId（条目子集）；
        TaskIdentity 绑定"某次尝试在某个冻结切片上的运行"——同一切片可多次运行（重试），
        同一 inputBaseline 可有多个切片（改求解配置复评，§4.1.4 末段）。
```

**条件依赖的防错误复用规则（D-10）**：任何可能"同时存在不同消费方式"的场景，以**条件＋条件输入入切片**表达，而不是武断全失效或全不失效。例：

| 场景 | 条件 | 条件输入（必须已入切片） | 效果 |
| --- | --- | --- | --- |
| 碰撞证据（KIN-05） | 策略启用碰撞 | `Policy` 条目（碰撞开关在策略内容身份内） | 启用→碰撞几何/负载几何为已消费依赖；禁用→碰撞证据项不适用（不缺失） |
| 笛卡尔段 IK 连续性（TRJ-02，表 4） | 路径含笛卡尔段 | `task-points`/任务序列对象条目 | 纯关节路径→该项显式不适用（C2 例） |
| 负载几何参与碰撞 | 工况启用负载且策略启用碰撞 | 工况对象＋策略条目 | 未启用负载的工况→负载几何未消费 |
| 摩擦参数（DYN-02） | 动力学评估声明（无条件） | 摩擦对象条目 | 缺失→SourcedValue::NotProvided→DataInsufficient（DYN-06，域判定） |

条件输入变化→其 ContentIdentity/内容身份变化→sliceId 变化→条件重新解析。**条件输入未入切片的声明在注册期即被拒绝**（闭包校验）——这是"漏声明导致错误复用"的系统性防线；残余风险（域声明遗漏本来就该消费的对象）登记 §15.2 R-3。

### 4.2.4 修改求解预算后的复评（不冒充原运行）

扩大初值数量/迭代预算/采样预算后复评（C5/KIN-13）：保持模型/需求/**冻结样本集**基准（inputBaselineId 不变——若改的是采样预算则样本基准本身改变，属新一轮研究基准，比较基准检查会拦截与旧结果的直接比较）；Configuration 条目变化→**新 sliceId**→缓存不命中→重算→**新 RunId/Attempt（新运行）**。旧运行结果保留为原快照历史证据（CON-02），新旧结果并存、不可互改；KIN-13"新旧结果不可直接比较"的提示数据＝两次运行的 inputBaselineId/sliceId 差异清单（§8.1 失效原因输出）。

---

## 5. 内容身份和依赖声明

### 5.1 身份的产生、比较与双层设计

| 身份 | 计算对象 | 算法 | 比较语义 | 用途 |
| --- | --- | --- | --- | --- |
| `snapshotId` | SnapshotCodec(refs-only) 规范字节 | SHA-256（core ContentDigester，D-05 同源） | 字节等值（摘要相等） | 快照完整性凭据；通用证据门禁项（§6.2） |
| `sliceId` | SliceCodec(全部冻结条目含 Environment 版本要素) | 同上 | 字节等值 | **缓存键与失效判据**（CON-05）；运行身份组成部分 |
| `inputBaselineId` | SliceCodec 的**子集**（Object(task/region/case-set/model 类角色)＋SampleSet＋NameMap＋快照身份块；排除求解类 Configuration 与 Environment 中的非基准版本；`runtime.model-identity`/`runtime.robwork-baseline` 属**基准类要素、进入** inputBaselineId——跨基线/跨编译器比较被 EVI-02 拦截，CR-05） | 同上 | 字节等值 | 比较基准（EVI-02/RPT-04）；"同一冻结输入复评"凭据（C5） |
| `evidenceManifest.digest` / `profile.contentIdentity` / `proof` 绑定身份 | 各自 canonical 编码 | 同上 | 字节等值 | 结果追溯与兼容判定（§6、§8.2） |

**三种"等价"的区别（冻结规则）**：

| 等价类别 | 定义 | 在本单元的地位 |
| --- | --- | --- |
| 字节相同 | 规范编码逐字节相等 → 摘要相等 | **内容身份的唯一等价关系**。摘要比较即字节比较的等价表达；比较用 core 精确等值（附录 D 第 12 项精神） |
| 语义等价 | 不同字节但工程语义相同（如同一 TCP 值经不同编码路径） | **不参与身份**：canonical 编码的存在正是为了消除语义等价歧义（§5.2 归一化规则）；语义等价的对象重编码会产生新 ContentVersion→新身份（保守方向，可接受：宁可多算不可错复用） |
| 数值容差内等价 | \|a−b\| ≤ ε_rel·\|ref\|＋ε_abs（附录 D C4） | **只用于数值校验断言**（各域/测试侧，经 core Compare）；**严禁**作为身份/哈希键等价关系——浮点"近似相等"无传递性，作为缓存键等价会导致错误复用（testkit.md §5.4.1 同源论证；EV-ID-2 用例钉住） |

**身份纪律**：ObjectId（对象身份）、ContentVersion（对象内容版本）、RevisionId（修订身份）、ContentIdentity/摘要（集合内容身份）四者互不替代、互不转换（core §4.1.1 分界的本单元重申）：失效判定用**条目级 ContentVersion/内容身份对比**（哪个依赖条目变了），从不用"修订号变了"代替（CON-05：不基于整个项目修订）；重新打开项目不改变任何内容身份（身份只由内容决定）；项目显示名、会话姿态、显示单位不在任何切片条目中——展示名称变化/界面切换不可能造成失效（EV-CUR-1/EV-ID-3 用例）。

**边界重申（附录 B 裁决排除项）**：内容摘要只用于上表既定目标（切片/快照/证据/Profile 内容身份，CON-04/05/06 明文授权）；不建立多重指纹体系、不做九层复核链、不把摘要暴露给用户界面（UX-02：界面不显示哈希——当前性以"过期＋原因"呈现）。

### 5.2 canonical 编码（SliceCodec / SnapshotCodec）

| 规则 | 内容 | 依据 |
| --- | --- | --- |
| 编码形态 | 确定性二进制：magic（`IRDSLCE1`/`IRDSNAP1`）＋条目按 (kind, key) 字典序＋每字段长度前缀；无填充、无端序歧义（大端） | 跨进程一致（worker/main 同身份，NFR-COR-02） |
| 可选值 | presence 字节（0x00/0x01）显式编码——optional 缺失与空值不等价 | NFR-COR-03（缺失不伪造） |
| 数值 | 整数十进制无关（定宽整型承载）；配置中的浮点以 IEEE754 双精度 8 字节大端**位模式**承载（round-trip 精确）；NaN/±Inf 在编码入口拒绝（抛 EvidenceError） | 位模式保证同值同字节；非有限拒绝 |
| 单位归一化 | 切片不承载物理量数值（配置容差等以域 schema 字节不透明承载，其 SI 归一化归域）；evidence 自有字段的数值仅为计数/版本（无量纲） | 单位换算唯一权威在 core（SA-12），本单元不出现第二换算点 |
| 字符串 | UTF-8、禁止 NUL；token 语法校验在构造入口 | 稳定排序与编码安全 |
| 版本化 | codec 版本号写入编码并作为 Environment 条目进入 sliceId——**编码升版＝全体切片身份变化**（破坏性变更，走设计变更评审并同步缓存治理登记） | 身份可演化且留痕 |
| 实现 | 纯函数：同输入同字节；无隐藏状态；线程安全（可重入） | NFR-COR-02 |

### 5.3 输入变化与失效矩阵（AT-05 承接；按实际消费关系，条件显式声明）

矩阵读法：某输入变化→其新 ContentVersion/内容身份与切片条目比对；仅当该条目**在切片中且被消费**时失效（sliceId 变化→当前性 Superseded＋缓存不命中）。✓＝失效（需重算），✗＝不失效，◐＝条件失效（条件在"条件"列，条件输入已入切片）。

| 输入变化（新修订产生） | 运动学（IK/FK/批量） | 区域覆盖 | 轨迹（含复检） | 动力学 | 选型 | 优化（OPT-B 静态） | 条件与依据 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 电机**成本**（成本字段/成本目录数据） | ✗ | ✗ | ✗ | ✗ | ✓（成本指标条目） | ◐（仅器件成本指标激活时） | 成本对象不在运动学/轨迹/动力学切片（AT-05 原文正例）；OPT 指标按激活目标（OPT-07） |
| **TCP 偏置**（KIN-14 设默认经命令） | ✓（IK 目标/解） | ✓ | ✓（笛卡尔段/节拍） | ◐（TCP 影响负载力臂时——负载安装/TCP 对象同版本变化） | ✗ | ✓（Must 可达约束） | AT-05 原文反例："TCP 变更失效运动学及下游"——下游按实际消费条目传播 |
| 连杆**几何**（碰撞模型对象） | ◐（策略启用碰撞→碰撞证据消费碰撞几何；禁用→不消费） | ◐（同左） | ◐（复检碰撞） | ✗（动力学消费惯量/质量对象，几何仅经物性对象影响） | ✗ | ◐（静态碰撞约束） | 几何与物性是不同对象（MDL-05 分层）；仅视觉几何变化不涉碰撞消费时不进碰撞相关切片 |
| **负载**（质量/惯量/负载几何对象） | ◐（负载几何仅在碰撞证据消费链上） | ◐（同左） | ◐（复检碰撞/动力学耦合） | ✓（RNEA 负载输入） | ◐（惯量比条目） | ◐ | 负载质量/惯量与负载几何为不同角色键，分别按消费关系失效 |
| **摩擦参数**（MDL-16） | ✗ | ✗ | ✗ | ✓（DYN-02） | ✗ | ✐（OPT-D 前） | 摩擦仅动力学消费；缺失→DataInsufficient 降级（DYN-06）走域判定，非失效问题 |
| **目录能力曲线**（SEL-01/02 锁定版本） | ✗ | ✗ | ✗ | ✗（关节侧结果与传动无关，DYN-04） | ✓（工作点/淘汰条目） | ◐（OPT-D 器件联合启用后） | 目录版本锁定（SEL-08）经 Object 条目进选型切片；更新不静默改变历史结果（历史结果保持原切片身份） |
| **工程策略**（碰撞规则/阈值/行程上限） | ◐（策略身份变化→凡声明 Policy 条目者失效；碰撞证据按启用条件） | ◐ | ◐（安全间距/复检阈值） | ◐（判定阈值消费） | ◐（判定阈值消费） | ◐ | 策略进入依赖切片（CON-06）；"实际依赖"由各评估器声明是否含 Policy 条目 |
| **求解配置**（AnalysisConfiguration） | ◐（仅该域消费的参数：IK 参数→运动学；采样预算→覆盖样本基准） | ◐（采样预算/种子→**样本集身份变化**＝基准变化） | ◐ | ◐（线程数等） | ◐ | ◐ | KIN-13：进入运行身份与缓存身份；按实际输入依赖提示受影响结果重算（AT-27） |
| **需求定义**（任务点/区域/工况集） | ✓ | ✓ | ✓ | ✓ | ◐（负载工况条目） | ✓ | 需求对象直接被各域消费；工况集变化另触发覆盖矩阵核对（EVI-02） |
| **显示单位**（KIN-12） | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | 纯显示投影（ARCH §7.7）——不在任何切片条目 |
| 会话姿态（KIN-06 双击/动画/Home 复位） | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | 会话态不产生修订、不进缓存身份（AT-04） |
| 项目显示名/界面切换/重开项目 | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | 身份只由内容决定（§5.1） |

（✐＝OPT-D 启用前的阶段语义——阶段 B 优化切片不含动力学/选型条目，摩擦变化不影响 OPT-B 静态子集。）

---

## 6. 证据 Profile、适用条件及证明契约

### 6.1 RequiredEvidenceProfile 数据结构（EVI-01/§8.1 表 4 承载）

**内容权威声明**：各评估域的必需/建议证据项**明细**已由 REQUIREMENTS §8.1 表 4 冻结（P-05 产物，v1.14 C2/v1.15 C5·C6/v1.16 C8 修订），**内容归需求**；evidence 拥有**数据结构、注册与消费**，各域按表 4 逐行实例化 Profile（§13 交接）。evidence 不新增、不删除、不改写任何证据项。

```cpp
// Evidence.hpp（节选；完整契约见本节各表）
enum class EvidenceItemClass { Common,        // 通用必需（表 4"通用必需项"，evidence 内建，域不得重复登记）
                               Required,      // 域必需（表 4 各行"必需证据项"）
                               Suggested };   // 域建议（缺失不阻断，单独标注）
struct EvidenceProfileItem {
    std::string itemId;          // 稳定 id："<域>.<项>"，如 "kin.ik-convergence-per-point"
    EvidenceItemClass itemClass;
    std::string description;     // 人读说明（锚定表 4 行文）
    Applicability applicability; // {conditionToken, referencedKeys[]}；无条件＝Always
    bool substitutableByInfeasibility;  // 成功产物类＝true（C2 替代规则）；通用门禁类＝false（C6 不豁免）
};
struct RequiredEvidenceProfile {
    std::string profileId;                     // "kin"|"trj"|"dyn"|"sel"|"opt"
    std::string version;                       // 域登记版本（语义化）
    core::ContentIdentity contentIdentity;     // 注册时由 evidence 计算（canonical 编码摘要）
    std::vector<EvidenceProfileItem> required;
    std::vector<EvidenceProfileItem> suggested;
};
```

| 结构要素 | 设计 | 依据 |
| --- | --- | --- |
| 通用必需项 | evidence 内建 `commonRequiredItems()`：①完整 AnalysisSnapshot 身份（含已解析策略与 RuntimeNameMap 内容身份——CON-06）；②必验工况覆盖矩阵（EVI-02）；③评估模式与证据等级标识。**任何 Verified 评估隐式附加**，域 Profile 不得重复登记（单一权威） | 表 4"通用必需项"原文 |
| 适用条件 | 每个 item 可声明 Applicability——条件不满足时该 item 显式标记 NotApplicable（ERR-01），**不计缺失**（例：纯关节空间路径不含笛卡尔段，TRJ-02 段内 IK 连续性检查不适用） | C2（v1.14） |
| 替代关系 | substitutableByInfeasibility＝true 的 item（成功产物类：IK 解集、路径、工作点序列等）在存在**有效**不可行证明时按"因不可行而不适用"记录，不计缺失；＝false 的 item（通用身份/可追溯性/工况覆盖）**不豁免** | C2＋C6（v1.15）：替代豁免仅限"因不可行而无法生成的成功产物"；不可行证明自身的可追溯性先经通用证据门禁校验 |
| Profile 版本与内容身份参与追溯/兼容 | envelope.evidence 绑定 profileId+version+contentIdentity；缓存/复用兼容判定比对 profile 身份（§8.2）；profile 内容变化→新 contentIdentity→旧结果对同 profile 请求不可直接复用 | EVI-01 全局唯一；CON-04 |
| 注册 | EvidenceProfileRegistry（§9.5）：重复 (profileId,version) 注册拒绝；contentIdentity 由 evidence 计算（域不可申报）；注册期校验 item 语法/条件闭包/替代标志与 itemClass 一致性（Common 类禁止域登记） | NFR-MNT-03 单一权威 |

### 6.2 证据项状态与证据清单

| EvidenceItemStatus | 含义 | 判定后果 | 依据 |
| --- | --- | --- | --- |
| `Satisfied` | 产物存在且通过绑定校验（摘要、快照/工况/对象范围绑定、产生者） | 满足 | — |
| `Missing` | 无产物 | 不满足，**全量列出**（不因首个缺失短路） | 表 2 ④ |
| `Invalid` | 产物存在但绑定校验失败（摘要不符/引用错误快照或工况/产生者未注册） | 不满足，附 invalidReason 诊断 | ERR-01 口径（不伪造） |
| `Unverified` | 产物存在但未在满足正式要求的条件下验证——典型：Quick 产物用于 Verified 判定、或校验未执行 | 不满足（用于正式判定），诊断区别于 Missing | 实现承载词表（P-EV-8；Quick 不得单独支撑正式通过＝表 1） |
| `NotApplicable` | 适用条件不满足，显式标记 | **不计缺失**；notApplicableReason 必填 | C2＋ERR-01"不适用显式标记" |

EvidenceItem 字段：{itemId, status, artifactDigest?（Satisfied 必填）, caseScope?（工况范围——单工况证据逐工况记录）, subject?（对象范围，core::ObjectId）, notApplicableReason?（N/A 必填）, invalidReason?（Invalid 必填 DiagnosticRecord）}。**证据与快照绑定**：EvidenceManifest 记录 snapshotId/sliceId/profile 身份，逐项证据的 caseScope ⊆ 快照 caseSet、subject ∈ 快照 objectClosure（汇总器校验，防错误工况/对象引用——EV-COV-2）。**证据一律引用 ObjectId**；运行时名称仅作辅助显示字段（CON-06 反解口径）。

**Verified 模式前置校验**（快照有效性的一部分，进入通用证据门禁）：`validateForMode(Verified)` 要求——被消费外部资源全部 state==Solidified（CON-03/PM-01：未固化即阻断正式结论，证据不足口径）；复现块版本要素完整。Quick/Preview 不强制固化（其产物本就不支撑正式结论）。

### 6.3 确定性不可行证明契约（表 2 ③ / C5 / C8 承载）

```cpp
enum class ProofCategory { AnalyticBound, ConstraintContradiction, MandatoryStateCollision };
struct DeterministicInfeasibilityProof {
    ProofCategory category;                 // 仅三类（C5/C8）——数值搜索未果不是证明（§6.3 末）
    std::string claimToken;                 // 域登记的具体论断 id（如 "kin.reach-beyond-link-sum"）
    core::ObjectId subject;                 // 作用对象（任务点/区域/必经状态所属对象）
    // —— 按 category 的条件必填（validateProof 逐字段校验，缺一即 Invalid）——
    std::string boundExpression;            // AnalyticBound 必填：界限表达（域登记，如"目标距离 > Σ连杆长"）
    std::string contradictionExpression;    // ConstraintContradiction 必填：约束矛盾表达
    MandatoryStateDescriptor mandatoryState;// MandatoryStateCollision 必填：{stateKind: TaskPointConfig|
                                            //   StartPoint|EndPoint|…, 为什么不可选择(必经性依据), objectId}
    std::vector<CollisionPair> collisionPairs; // MandatoryStateCollision 必填：对象 ID 对＋判定（构型级证据来源）
    // —— 通用必填 ——
    std::string preconditions;              // 适用前提（证明成立所假设的需求/配置——绑定输入身份）
    std::string coverageClaim;              // 覆盖范围："覆盖全部允许选择"(Analytic/Contradiction) 或
                                            //   "该必经状态"(MandatoryState)——作用域声明
    core::ContentIdentity snapshotId;       // 输入身份（证明针对哪个快照）
    core::ContentIdentity sliceId;
    EvaluationKey producer;                 // 产生者
    std::uint32_t producerContractVersion;
};
// validateProof(proof, registry, snapshot)：类别合法三元之一；category 条件字段齐备；producer 已注册
//   且契约版本相符；snapshotId/sliceId 非空且与被汇总结果一致；coverageClaim 句法合法。
//   —— 字段级校验：汇总器绝不凭单一状态字段（如 bool provenInfeasible）采信证明（§1.1 声明的落地）。
```

**作用域规则（C8）的承载**：构型/路径级碰撞**不进入**本契约——某 IK 解碰撞＝该解被硬过滤（进入 SearchExhaustedRecord.filteredSolutions）；某候选路径碰撞＝该路径淘汰并触发重规划（轨迹域证据）；只有当碰撞对象是**必经状态**（mandatoryState 不可选择性依据成立）或证明覆盖全部允许选择（AnalyticBound/ConstraintContradiction）时，证明才合法携带 MandatoryStateCollision/其他两类。登记为三类之外的 category（如"多初值全发散"）→ validateProof 拒绝（EV-VER-2/3/4 用例）。

**SearchExhaustedRecord（搜索未果口径，C5/C8）**：{searchBudgetUsed, initialGuessesTried, filteredSolutions[]{solutionRef, filterReason: Residual|JointLimit|Collision}}——多初值未收敛或已找到的解全部被硬条件过滤（含全部因碰撞被过滤）时由评估器产出；汇总层判 **DataInsufficient**（附该记录），**不得**输出不可行结论；用户扩大初值/预算后按 §4.2.4 复评。

### 6.4 工程判定汇总（表 2 五级优先级的可执行实现）

#### 6.4.1 汇总决策表（自上而下首个命中生效；C2/C5/C6/C8 修订后口径）

| 级 | 条件（按序判定） | 输出 engineeringStatus | 附加义务 |
| --- | --- | --- | --- |
| ⓪ 前置 | outcome ≠ Completed | 不汇总（envelope 强制 NotApplicable，§7） | 取消/失败/中断无工程判定（TASK-02） |
| ① 输入非法 | ReadinessSummary.valid==false（任一启用 Must 条目非法——REQ-06 口径，由请求方/域提供） | DataInsufficient（"输入未完成"，评估不派发；若结果对象仍产生〔评估中检出〕，以此为由） | missingItems 含 input-invalid 清单；**不运行正式评估**（REQ-06）——ui 的"未完成"状态由此投影（非新增状态） |
| ② 通用证据门禁 | 快照身份（含策略/名称映射内容身份、复现块、Verified 前置固化校验）缺失/无效 ∨ 工况覆盖矩阵缺失/无效（漏验/错误引用/重复）∨ 模式与证据等级标识缺失/无效 | DataInsufficient | 缺失项全量列出；**不可行证明自身的可追溯性同受此门禁**——证明绑定校验失败时不进入③（C6：不被替代规则豁免） |
| ③ 确定性不可行证明 | 存在 DeterministicInfeasibilityProof 且 validateProof 通过（含快照/切片绑定一致、门禁已过） | EngineeringInfeasible | 证明即该域必需产物；成功产物类证据按"因不可行而不适用"记录（substitutableByInfeasibility=true 且 NotApplicable 原因=不可行）；通用门禁类不豁免 |
| ④ 必需证据缺失 | 必需项（适用者）存在 Missing/Invalid/Unverified 且无有效证明 | DataInsufficient | **缺失项全量列出**（不短路；碰撞证据缺失按 KIN-05 同口径——缺检测器即数据不足） |
| ⑤ 工程判定 | 证据齐备 | 有效 Must 未满足→EngineeringInfeasible（判定记录）；Should 未满足→Feasible＋警告诊断（REQ-06）；否则 Feasible | Must/Should 违例清单来自 DomainVerdictInputs（域判定，evidence 汇总） |

```cpp
// Verdict.hpp —— 纯函数，确定性，无副作用，线程安全（可重入）
struct VerdictInput {
    core::TaskOutcome outcome;                    // ⓪
    ReadinessSummary readiness;                   // ① {valid, invalidMustItems[]}
    SnapshotGateResult snapshotGate;              // ② {complete, missingFields[]}（含固化/复现块校验）
    CaseCoverageMatrix coverage;                  // ②（§6.6 校验结果内含）
    std::optional<DeterministicInfeasibilityProof> proof;   // ③
    EvidenceManifest evidence;                    // ③④⑤（含 Profile 绑定与逐项状态）
    DomainVerdictInputs domain;                   // ⑤ {mustViolations[], shouldViolations[]}
};
struct VerdictResult {
    core::EngineeringStatus status;               // ⓪→NotApplicable；①～④→DataInsufficient；
                                                  //  ③→EngineeringInfeasible；⑤→Feasible|EngineeringInfeasible
    std::vector<MissingItem> missingItems;        // ①②④ 全量清单（itemId+原因）
    std::vector<core::DiagnosticRecord> diagnostics;
    VerdictTrace trace;                           // 命中级次与逐级判定记录（追溯，NFR-COR-04）
    std::optional<SearchExhaustedRecord> searchRecord; // 域产出时透传（④场景的数据不足凭据）
};
VerdictResult aggregateVerdict(const VerdictInput&, const EvaluatorRegistry&,
                               const EvidenceProfileRegistry&);
```

#### 6.4.2 必须区分的判定对（正/反例语义冻结）

| 区分对 | 规则 | 依据 |
| --- | --- | --- |
| 执行失败 vs 工程不可行 | outcome=Failed 是**执行轴**（进程/异常），engineeringStatus 强制 NotApplicable；工程不可行只在 Completed 且③命中时成立 | TASK-02/表 3、CON-02 |
| 搜索未找到解 vs 已证明无解 | 前者＝SearchExhaustedRecord→DataInsufficient（附预算/初值数/过滤记录）；后者＝三类证明→EngineeringInfeasible。**多初值未收敛、已找到解全部被过滤（含全部碰撞过滤）不得自动转为不可行** | C5/C8；EV-VER-2/3/4 |
| 构型/候选路径碰撞 vs 任务不可行 | 解碰撞→过滤该解；路径碰撞→淘汰并重规划（重规划成功→任务不受影响）；仅必经状态碰撞或覆盖全部选择的证明上升为任务级 | C8；EV-VER-3/4/5 |
| 必经状态 vs 可选择的构型/路径 | 必经性由 mandatoryState.stateKind＋不可选择性依据声明并校验；可选择对象（多解 IK、多条候选路径）的碰撞永远只过滤/淘汰该对象 | C8 |
| 当前性过期 vs 证据本身无效 | Superseded 是**投影**（相对当前上下文），不改变证据在其快照内的有效性；证据 Invalid 是**绑定校验失败**（摘要/引用/产生者）——两者独立，过期结果的证据仍可作历史证据 | CON-02/05；EV-CUR-3 |
| Must vs Should | 有效 Must 未满足→不可行（⑤）；有效 Should 未满足→Feasible＋警告（不阻断） | REQ-06 |

#### 6.4.3 汇总层级（单个域 → 任务点 → 工作区域 → 工况 → 整体方案）

| 层级 | 汇总规则 | 备注 |
| --- | --- | --- |
| 单个域（单工况内） | §6.4.1 决策表 | 产出该域该工况的 engineeringStatus |
| 任务点级 | 域内逐任务点状态（可行/不可行/数据不足三态，KIN-03）：点级不可行须有点级证明或 Must 违例；点级搜索未果→点级 DataInsufficient | 点级明细进 payload（域所有） |
| 工作区域级 | 区域覆盖证据契约（§6.6）：分母完整性/数据不足降级/零样本；覆盖率**数值**归 kinematics，evidence 检查证据与汇总是否符合契约 | KIN-04 |
| 工况级 | 该工况全部域证据＋覆盖矩阵该行 Executed；工况级状态＝各域状态按 ⓪～⑤ 再入决策表（域作为证据项来源） | EVI-02 |
| 整体方案级 | **EVI-02 前置**：全部启用必验工况覆盖完备后方可给正式判定；任一工况 ②④ 级命中→整体 DataInsufficient（缺失全量列出）；存在任务级有效证明（③）→整体 EngineeringInfeasible（证明为任务级作用域——见 P-EV-3 登记）；全部 Feasible→整体 Feasible；多工况包络合并仅为呈现（DYN-07），不替代覆盖 | EVI-02/表 2 |

### 6.5 比较基准一致性检查（EVI-02/RPT-04/OPT 纯判定）

```cpp
struct ComparisonBaseline {          // 从各结果的 envelope 提取
    core::ProjectId project; core::BranchId branch;
    core::ContentIdentity inputBaselineId;   // 模型/需求/工况集/冻结样本集基准（§5.1）
    core::ContentIdentity requiredCaseSetId; // 必验工况集身份
    std::vector<core::ContentIdentity> sampleSetIds;
};
CheckResult checkComparisonBaselinesConsistent(const std::vector<ComparisonBaseline>&);
// 全部相等→通过；任何不等→失败并列出差异维度（基准不同的方案/候选/报告变体不得直接比较）
```

方案比较（RPT-04）、优化候选比较（OPT 内部 Pareto 比较）与报告变体章节共用本检查；样本集身份不等（如采样预算改变）→ 旧覆盖率/候选指标不可与新的直接比较（KIN-04 冻结样本集 + C5 复评规则的联合落地，EV-COV-4）。

### 6.6 多工况覆盖与区域采样证据契约

**CaseCoverageMatrix**：

| 字段 | 类型 | 约束 |
| --- | --- | --- |
| `requiredCaseSetId` | core::ContentIdentity | 必须等于快照 caseSet 的身份（与计划核对——不同即 ② 级门禁失败） |
| `entries[]` | {caseId, status: Executed\|NotExecuted\|Invalid\|NotApplicable\|Failed, runId?, resultSliceId?} | caseId ⊆ 必验集（**错误工况引用→Invalid**）；caseId 重复→矩阵非法（**重复记录拒绝**，EV-COV-2）；每个 enabled∧mandatory 工况必须有 Executed 条目（**漏验→② 级失败**，EV-COV-1）；NotApplicable 必填原因 |
| 校验规则 | 覆盖完备 ⇔ 全部 enabled∧mandatory 工况 Executed；**包络结果（DYN-07 合并产物）的存在不替代任何工况的 Executed 条目**；空必验集合/全不适用→P-EV-7 保守处置（见下） | EVI-02 |

已执行/未执行/无效/不适用/失败五态即 CaseExecutionStatus——Failed 工况（该工况运行 outcome=Failed）不构成覆盖，须重跑或整体降级。

**RegionCoverageEvidence（区域采样证据表达；覆盖率算法归 kinematics，本契约只约束能进正式判定的证据形状）**：

| 字段 | 约束（KIN-04 R3/R8 承接） |
| --- | --- |
| `sampleSetIdentity / plannedPositionSamples / plannedPoseSamples` | 与快照采样计划一致（分母来源） |
| `reachable / unreachable / dataInsufficient`（位置与姿态两栏） | 计数按类分列；**分母完整性**：reachable＋unreachable＋dataInsufficient == plannedTotal（不符→证据 Invalid，EV-COV-3）；不可达保留分母（禁止按结果剔除样本） |
| `downgraded` | dataInsufficient>0 → true：覆盖率数值仍可作参考值报告，但**结论整体降级 DataInsufficient**、不得输出正式覆盖率通过结论；补全证据后按**同一冻结样本集**复评（inputBaselineId 凭据） |
| 零样本 | plannedTotal==0 → 覆盖率不定义：判 DataInsufficient＋诊断（采样计划为空/区域退化），**不得输出 0%/100%**（EV-COV-3 反例） |

**空集合与全不适用（P-EV-7 登记的保守处置）**：必验工况集为空——覆盖矩阵平凡完备，但依赖工况证据的判定项全部无法满足→按 ④ 级判 DataInsufficient（附"无启用必验工况"诊断），不得输出正式通过；全部工况显式 NotApplicable——同理，凡"不适用"须逐项给出不适用原因并经条件校验，正式通过判定不成立。该处置为保守方向（宁可数据不足不可虚通过），上游未明文定义，登记待裁决。

---

## 7. ResultEnvelope 与结果生命周期

### 7.1 数据结构与合法组合矩阵（表 3/S A-13）

```cpp
// Envelope.hpp
struct DomainPayload {                       // 域结果载荷（evidence 视为不透明）
    std::string kindToken;                   // 域登记（如 "kin.batch-ik.v1"）
    std::vector<std::uint8_t> canonicalBytes;// 域 canonical 编码（编码契约归域）
    core::ContentIdentity digest;            // evidence 计算摘要（追溯/完整性）
};
struct PartialDataRef {                      // 取消/失败/中断时的诊断性部分数据
    std::filesystem::path archiveHint;       // 归档位置提示（实际写入归 execution/project）
    bool reusable = false;                   // 恒 false：诊断价值保留，不得正式复用
};
struct ResultEnvelope {
    // —— 身份与绑定 ——
    core::TaskIdentity task;                 // 五元组（execution 分配；make 校验 isValid）
    EvaluationKey evaluationKey;
    std::uint32_t evaluatorContractVersion;
    core::EvaluationMode mode;               // Preview 被构造边界拒绝（表 1：不产生结果对象）
    core::ContentIdentity snapshotId, sliceId, inputBaselineId;
    CaseScope caseScope;                     // 本结果覆盖的工况 id 集（⊆ 快照 caseSet；make 校验）
    EvidenceProfileRef profile;              // profileId+version+contentIdentity
    // —— 三轴之执行轴与判定轴 ——
    core::TaskOutcome outcome;
    core::EngineeringStatus engineeringStatus;
    // —— 证据（表 3：Completed 必含）——
    EvidenceManifest evidence;               // 逐项状态＋绑定（§6.2）
    std::optional<DeterministicInfeasibilityProof> infeasibilityProof;
    std::optional<SearchExhaustedRecord> searchRecord;
    std::vector<MissingItem> missingItems;   // DataInsufficient 必含全量清单
    // —— 载荷与诊断 ——
    std::optional<DomainPayload> payload;    // 正式结论载荷（仅 Completed 组合）
    std::optional<PartialDataRef> partialData;
    std::vector<core::DiagnosticRecord> diagnostics;
    ProducerInfo producer;                   // {producedIn: MainProcess|Worker, productVersion, …}
    // 构造边界（SA-13）：唯一入口，非法组合抛 EvidenceError；无 setter，构造后不可变
    static ResultEnvelope make(ResultEnvelopeDraft&&);
};
```

**合法组合矩阵（构造边界逐条强制）**：

| outcome | engineeringStatus | payload/evidence 约束 | make() 校验 |
| --- | --- | --- | --- |
| Completed | Feasible / EngineeringInfeasible / DataInsufficient | **必含**证据清单与工况标识；DataInsufficient 须含缺失项**全量**清单（空清单＋DataInsufficient→拒绝）；EngineeringInfeasible 须含有效证明（validateProof 通过）或 Must 违例记录；Feasible 须无未解决缺失项 | ✔ |
| Completed | NotApplicable | — | ✗ 拒绝（NotApplicable 只配非 Completed，ERR-01） |
| Canceled / Failed / Interrupted | NotApplicable（显式标记，不伪造判定） | **不得含正式结论字段**：payload 必须为空、evidence 不得含 Satisfied 判定声明、missingItems 不适用（保留诊断与 partialData{reusable=false}）；不得进入正式报告或可行集（TASK-02）、不得作为正式缓存命中（CON-04） | ✔（违反→拒绝） |
| 任意 | — | mode==Preview | ✗ 拒绝（表 1：Preview 不产生结果对象——EV-ENV-1） |

**分批数据与最终结果对象的区别**：评估运行中的流式/分批数据（进度、部分样本）是 execution 通道 DTO（execution 所有），**不是** ResultEnvelope；只有运行终结时经 make() 构造的唯一 envelope 是正式结果对象。构造边界＝make() 调用（worker 内产出后、回传接纳前各校验一次；接纳侧 execution 复用同一校验器——纯函数）。`Completed + DataInsufficient` 的含义＝**计算执行完成但证据不满足正式判定**（含搜索未果、缺碰撞检测器、物性不足、工况覆盖不全等）——合法且常见，不是执行失败。

### 7.2 结果生命周期与资格（RPT-05 两类声明）

| 阶段 | 归属 | evidence 提供 |
| --- | --- | --- |
| 构造校验 | evidence（make/validateCombination 纯函数） | §7.1 |
| 运行接纳（五元组核对）与归档触发 | execution（RunRegistry） | envelope 承载五元组；TaskIdentity 有效性校验 |
| 磁盘保存（results/\<run-id\>/） | project（IResultArchivePort） | RunManifest 所需的透传字段（project.md §4.4.7 已登记 taskIdentity/runKind/evaluationKey） |
| 当前性投影 | evidence（§8.1，纯计算，不写回） | §8.1 |
| 报告消费 | reporting | 资格检查（下表） |

**两类声明的资格（相互独立，RPT-05 冻结措辞的判定承载）**：

| 资格 | 条件（纯检查） | 消费者 |
| --- | --- | --- |
| FormalPassEligibility（可渲染"正式通过结论"） | mode==Verified ∧ outcome==Completed ∧ 覆盖矩阵完备（EVI-02）∧ 必需证据齐备（无 Missing/Invalid/Unverified）∧ engineeringStatus==Feasible——**五条件同时满足**（§8.1 表 2 原文） | reporting/ui |
| ReviewRecordEligibility（可作"正式评审记录"） | mode==Verified ∧ outcome==Completed ∧ ② 级门禁通过 ∧ engineeringStatus==EngineeringInfeasible ∧ 携带有效证明（或 Must 违例记录） | reporting（RPT-05：经验证的不可行结论及其原因允许作为正式评审记录明确呈现） |

**历史 payload 不可改写**：envelope 构造后不可变（值语义＋无 setter）；当前性/资格都是**派生投影**，其结果保存在 evidence 的会话态索引（§8.1 CurrentnessIndex）或由消费者即时计算，**永不**回写归档结果（CON-02"旧结果保留为原快照历史证据"；EV-CUR-3 用例）。

---

## 8. 当前性、缓存复用和检查点兼容

### 8.1 ResultCurrentness（纯投影）

**输入**：{结果引用（envelope 或其身份摘要：task/evaluationKey/contractVersion/sliceId/inputBaselineId）, 目标上下文 CurrentnessTarget{projectId, branchId, scheme?, evaluationKey, 当前 AnalysisConfiguration 内容身份}, 依赖重建源（IRevisionClosureSource/IObjectBytesSource＋当前策略/名称映射身份＋注册表当前 descriptor）}。

**计算步骤**：

```
computeCurrentness(result, target, sources):
 1 上下文匹配：result.task.project == target.project ∧ 分支/方案匹配？
    否 → NotEvaluable{cross-context}（原项目结果不能成为另一项目/会话的当前结果，TASK-03；
          不产生任何持久状态，仅诊断）
 2 契约匹配：result.evaluatorContractVersion == 注册表当前 descriptor.contractVersion？
    否 → Superseded{evaluator-contract-changed}
 3 目标切片重建：以同一 descriptor 的依赖声明，对 target 上下文当前对象版本/当前配置/当前策略/
    名称映射/当前样本计划重建切片 → targetSliceId
    任一依赖无法解析（对象缺失/资源缺失/工况对象不存在）→ NotEvaluable{unresolved-dependency}
    ＋诊断——**不得默认 Current**（任务约束§五.8；EV-CUR-2）
 4 比较：result.sliceId == targetSliceId → Current（HEAD 前进本身不出现在比较里——
    仅当它造成被消费条目内容变化才在步骤 3 体现；"仅电机成本变更"→运动学切片不变→Current，S7 正例）
    不等 → Superseded{逐条目失效原因清单}：
      {dependencyKey, kind: ObjectContentChanged(旧cv→新cv) | ConfigurationChanged |
       PolicyChanged | NameMapChanged | SampleBaselineChanged | ConditionFlipped |
       UpstreamResultChanged | …}——具体失效原因输出（KIN-13 重算提示的数据源）
 5 输出 CurrentnessResult{status: optional<Current|Superseded>, reasons[], evaluatedAgainst, diagnostics}
```

**规则冻结**：

| 规则 | 设计 | 依据 |
| --- | --- | --- |
| 当前性相对什么计算 | (project, branch, evaluationKey, 当前配置内容身份)——方案/配置是上下文一部分；切换界面/方案视图**不改变**历史 payload，只改变"哪个上下文的当前性被查询" | CON-02/05 |
| 状态词表 | **仅 Current / Superseded 两个持久语义状态**（ARCH §7.6/S7 用的即此两词）；"无法判定"（NotEvaluable）是**计算结果形态**（status=空＋诊断），不是第三个持久状态——上游只定义两态，不私增（任务约束§五.8；呈现口径登记 P-EV-4） | CON-02 |
| 派生投影不可写回 | CurrentnessIndex（会话态缓存，键＝(runId, targetContextId)）缓存计算结果；修订事件（RevisionCommitted/DependencyInvalidated——事件不携带数据，core D-09）触发按需重算；归档结果永不修改 | CON-02、ARCH §7.1 |
| 默认值 | 无默认 Current：未计算/不可判定 → 消费者不得当作 Current 使用 | 任务约束§五.8 |

### 8.2 缓存复用与检查点兼容（纯判定契约；存储/淘汰/恢复归 execution/project）

```cpp
// Compatibility.hpp —— 四类复用严格区分（任务约束§五.9）
struct CachedResultSummary {              // 从缓存条目/envelope 提取（不载入全载荷）
    core::EvaluationMode mode; core::TaskOutcome outcome; bool manifestFinalized;
    core::ContentIdentity sliceId; std::uint32_t evaluatorContractVersion;
    core::ContentIdentity profileContentIdentity; std::optional<core::ContentIdentity> inputBaselineId;
};
struct CacheHitQuery  { core::EvaluationMode requestedMode; core::ContentIdentity requestSliceId;
                        std::uint32_t requestContractVersion;
                        core::ContentIdentity requestProfileIdentity; };
struct CacheHitResult {
    enum Verdict { FullHit, DiagnosticOnly, Incompatible } verdict;
    std::vector<CacheMissReason> reasons;   // mode-mismatch / slice-mismatch / contract-mismatch /
                                            // profile-mismatch / outcome-not-completed / manifest-incomplete
};
CacheHitResult judgeCacheHit(const CacheHitQuery&, const CachedResultSummary&);

// FullHit ⇔ 模式相等 ∧ sliceId 相等 ∧ 契约版本相等 ∧ profile 身份相等
//          ∧ outcome==Completed ∧ manifestFinalized
//   ——策略/种子/线程/配置/样本/环境版本已含于 sliceId（D-01/D-11：身份要素完备性是 FullHit 的前提）
//   ——Quick 缓存对 Verified 请求：mode-mismatch → Incompatible（Quick 结果不得当 Verified 证据，
//      表 1/CON-04/OPT-06；EV-CPA-1）。反方向（Verified 命中供 Quick 用）同为 Incompatible：
//      模式是请求属性，不做隐式升降级——保守方向，防"高级别证据降级冒充"
// DiagnosticOnly ⇔ sliceId/契约/模式/Profile 相等 ∧ (outcome≠Completed ∨ 未 finalize)
//   ——部分/失败/取消结果可读作诊断，不得正式命中（CON-04；EV-CPA-2）
// Incompatible：其余一切

struct CheckpointSummary { EvaluationKey evaluatorKey; core::ContentIdentity sliceId;
    std::uint32_t evaluatorContractVersion; std::uint32_t checkpointFormatVersion;
    bool integrityVerified; };
struct CheckpointCompatibilityResult { enum Verdict { Resumeable, Incompatible } verdict;
    std::vector<std::string> reasons; };
CheckpointCompatibilityResult judgeCheckpointCompatibility(const CacheHitQuery& request,
                                                           const CheckpointSummary&);
// Resumeable ⇔ evaluatorKey 相等 ∧ sliceId 相等 ∧ 契约版本相等 ∧ checkpoint 格式版本兼容
//              ∧ integrityVerified。检查点是否可恢复与该任务上次是否成功是两个独立问题：
// 上次任务 Canceled/Failed 的检查点仍可 Resumeable（CON-04/TASK-01；强制终止保留最近检查点）；
// 恢复执行、续跑调度与检查点写入归 execution（ARCH §4.3/§6.5）。
```

---

## 9. 评估器接口与注册表（③评估器端口）

### 9.1 端口定位与调度分离

evidence 拥有③评估器端口（ARCH §7.2）：**按评估键发现已注册评估器并调用**。跨域评估组合（优化消费运动学/碰撞/动力学校核、选型消费传动映射）经本端口协作，**不反向链接业务实现**（注册制，L5 装配）。评估器调用与任务执行分离：evidence 定义**计算接口**（本节）；execution 负责**调度、取消信号传递、进程控制与派发**（ARCH §4）——注册表内**没有**任务调度器、队列、取消通道或进度设施；`IEvaluationContext` 只是评估器调用约定（取消查询/进度上报/对象读取的**接口**），其实现由 execution 注入。

### 9.2 EvaluatorDescriptor 与依赖/能力声明

```cpp
struct EvaluatorDescriptor {                 // 注册时一次性声明，运行期只读
    EvaluationKey key;                       // 如 "kin.batch-ik"；语法 [a-z][a-z0-9-]{1,63}
    std::uint32_t contractVersion;           // 输入/输出契约版本（进入 sliceId，CON-04）
    std::vector<DependencyDeclaration> inputs;   // 依赖声明（§4.2.1；注册期闭包校验）
    EvidenceProfileRef profile;              // {profileId, version}——注册时必须已可解析
    std::vector<core::EvaluationMode> supportedModes;  // Preview/Quick/Verified 子集，≥1
    bool stateless;                          // true=无跨调用状态（可自由并发）
    ThreadSafety threadSafety;               // SingleThread / ConcurrentReadOnly / FullyThreadSafe
};
```

### 9.3 IEngineeringEvaluator 与调用约定

```cpp
struct EvaluationRequest {
    core::TaskIdentity task;                 // 派发时绑定（execution 分配五元组）
    core::EvaluationMode mode;               // Preview 请求合法（域做草稿预览），但不产生 envelope
    AnalysisSnapshot snapshot;               // 物化或引用形态（worker 场景为物化）
    InputSlice slice;                        // 冻结切片（评估器按 entries 索引导入）
    std::vector<CaseId> caseSubset;          // 分批评估的工况子集（覆盖矩阵跨批次汇总）
};
struct EvaluationOutput {                    // 评估器产出；envelope 由调用侧（execution/域）经
                                            // aggregateVerdict + ResultEnvelope::make 组装
    std::vector<EvidenceItem> evidence;      // 领域证据（含证明/搜索未果记录的域内素材）
    std::optional<DeterministicInfeasibilityProof> proof;
    std::optional<SearchExhaustedRecord> searchRecord;
    DomainVerdictInputs verdictInputs;       // Must/Should 违例（REQ-06 口径）
    std::optional<DomainPayload> payload;    // 域结果载荷（canonical，域契约）
    std::vector<core::DiagnosticRecord> diagnostics;
};
class IEvaluationContext {                   // 调用约定接口；实现由宿主（execution/主进程）注入
public:
    virtual ~IEvaluationContext() = default;
    virtual bool cancellationRequested() const = 0;      // 查询式取消（协作取消；非 evidence 调度）
    virtual void reportProgress(std::uint8_t percent, std::string_view phase) = 0;
    virtual std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId, core::ContentVersion) const = 0;   // 物化快照读取
};
class IEngineeringEvaluator {
public:
    virtual ~IEngineeringEvaluator() = default;
    virtual const EvaluatorDescriptor& descriptor() const = 0;
    // 契约：同 (request 切片, 环境) → 等价输出（NFR-COR-02）；纯计算——不派发任务、不写项目、
    //       不产生修订、不自我注册；抛 EvidenceError(评估域错误码) 或返回诊断（域自选，约定登记于域任务卡）；
    //       可重入性＝descriptor.threadSafety；长评估必须周期性查询 cancellationRequested
    virtual EvaluationOutput evaluate(const EvaluationRequest&, IEvaluationContext&) = 0;
};
class IEvaluatorFactory {                    // 实例创建（worker 各建实例；无状态评估器可共享）
public:
    virtual ~IEvaluatorFactory() = default;
    virtual const EvaluatorDescriptor& descriptor() const = 0;
    virtual std::unique_ptr<IEngineeringEvaluator> create() const = 0;
};
```

调用示例（域注册，阶段 B 起真实存在；阶段 A 用测试替身，§11）：

```cpp
// kinematics 计算库内（示例契约，非实现）：
class BatchIkEvaluator final : public IEngineeringEvaluator { /* 领域算法 */ };
class BatchIkFactory final : public IEvaluatorFactory { /* descriptor: key="kin.batch-ik",
    inputs={"model.robot-design"(Object), "tcp"(Object), "task-points"(Object),
            "collision-models"(Object, Conditional←policy), "policy.resolved"(Policy),
            "namemap"(NameMap), "config.ik"(Configuration)},
    profile={"kin",表4版本} … */ };
// L5 装配：registry.registerEvaluator(std::make_unique<BatchIkFactory>());
```

### 9.4 EvaluatorRegistry（注册表行为冻结）

| 行为 | 规则 |
| --- | --- |
| 注册时机 | L5 装配期（主进程与 worker 进程各执行同一注册清单）；运行期不增删 |
| 重复注册 | 同 evaluationKey 再次注册 → 注册边界拒绝：EvidenceError(duplicate-evaluator)，**不覆盖、不静默** |
| 未知键 | `find(key)` 返回 nullptr（查询非抛）；调用方给"评估器不可用"诊断（EV-REG-1） |
| 版本冲突 | 同键不同 contractVersion 的并发注册＝重复注册拒绝；**换版本＝装配清单变更**（新版本新清单，跨进程一致），不存在运行期热替换 |
| 注册期验证 | descriptor 完整性（key 语法/契约版本>0/模式集非空/Profile 已注册且版本可解析）；依赖声明闭包校验（§4.2.3）；threadSafety 合法——任一失败即拒绝并指明字段 |
| 实例/生命周期 | 注册表持 factory（共享）；实例由调用方经 create() 取得并独占；无状态评估器建议 stateless=true＋共享实例 |
| 线程安全 | 注册期单线程约定（装配）；运行期 find/manifest() 并发只读安全；create() 线程安全 |
| 主/worker 一致性 | `manifest()` 返回按 key 字典序排序的 {key, contractVersion, profileIdentity} 清单及其摘要——execution 在派发/握手时比对两侧摘要，不一致即拒绝派发（同一快照跨入口/跨进程同一评估器同一判定，CON-06/AT-19 的装配侧保障） |
| 反向链接禁止 | 注册表只持 factory 接口；evidence 不链接任何业务单元（R-1/R-2）；**不创建逐域转发包装器**（域直接实现 IEngineeringEvaluator，NFR-MNT-04） |

### 9.5 EvidenceProfileRegistry

注册 RequiredEvidenceProfile（域按表 4 实例化）；重复 (profileId, version) 拒绝；contentIdentity 由 evidence 于注册时计算（canonical 编码摘要，域不可申报）；注册期校验 item 语法、条件闭包（referencedKeys ⊆ 域评估器声明键∪快照事实键——经注册顺序解耦：Profile 条件的 referencedKeys 语法校验在 Profile 注册时做，与评估器声明的交叉校验在评估器注册时做）、替代标志仅限非 Common 类。线程安全同 9.4。

---

## 10. 跨单元调用流程

### 10.1 主流程：快照→切片→评估→结果→当前性

```
 业务域插件          evidence(本单元)                 execution              project(②端口)      L5装配
 (kinematics/…)     Snapshot/Slice/Registry          (调度/接纳)            /适配器注入
    │ ①取修订视图+对象引用 ───────────────────────────────────────────────► (IProjectQueryPort)
    │ ②取策略/名称映射内容身份 ──► (④policy/⑥runtime 端口，域自行调用)
    │ ③SnapshotBuilder 逐类录入 ─► │
    │     ·objectInRevision 校验    │ 修订闭包包含性(防混入)/CON-06 非空/工况唯一
    │  ◄── AnalysisSnapshot(冻结,snapshotId)
    │ ④SliceBuilder(descriptor.inputs+解析) ─► │ 子集校验/条件冻结
    │  ◄── InputSlice(sliceId, inputBaselineId)
    │ ⑤提交运行(快照+切片+模式) ────────────────────────────────────► │ RunRegistry 登记五元组
    │                                                                │ 派发 worker(物化快照)
    │                                   worker 内: 注册表(同一 manifest)find(key)→create()
    │                                   evaluator.evaluate(request, ctx[execution 注入取消/进度])
    │                                   → EvaluationOutput(证据/证明/搜索未果/payload)
    │  ◄──────────── 完成事件(五元组) ─────────────────────────────── │ 登记表核对(A8 两段式)
    │                                   │ aggregateVerdict(五级优先级) ──► │
    │                                   │ ResultEnvelope::make(构造校验,SA-13)
    │                                   │ 归档 begin/writeBatch/finalize ─► results/<run-id>/(project)
    │ ⑥修订事件(DependencyInvalidated,无数据) ────────────────────────────► │
    │     computeCurrentness(结果切片 vs 当前上下文重建切片) ─► Current/Superseded(+原因清单)
    │     CurrentnessIndex(会话态投影) ──► ui 状态呈现/重算提示（不写回归档）
```

### 10.2 评估器注册与调用时序

```
 L5装配(主进程)                EvaluatorRegistry         L5装配(worker进程)
   │ registerEvaluator(factory) │                          │ registerEvaluator(同一清单)
   │───────────────────────────►│ 注册期验证(闭包/Profile)  │───────────────────────────►│
   │                            │ manifest() 摘要 M        │      manifest() 摘要 M'     │
   │◄───────────────────────────│                          │◄───────────────────────────│
   │ (execution 派发时随请求携带 M；worker 握手比对 M==M'，不等→拒绝派发，CON-06)             │

 execution(worker宿主)         IEngineeringEvaluator        IEvaluationContext(execution实现)
   │ create() ─► registry ──┐   │                            │
   │ evaluate(request,ctx) ─────────────►│ ①按 slice.entries 导入输入(物化快照)          │
   │                        │   │ ②领域计算(周期性 cancellationRequested() ──────────►) │
   │                        │   │ ③产出证据/证明/搜索未果/payload/诊断                    │
   │ ◄── EvaluationOutput ────────┘                            │
   │ aggregateVerdict → ResultEnvelope::make → 归档(project)   │
```

### 10.3 三轴正交关系表（CON-02 承载）

| 轴 | 值域 | 何时确定 | 可变性 | 典型组合示例 |
| --- | --- | --- | --- | --- |
| 执行完整性 outcome | Completed/Canceled/Failed/Interrupted | 运行终结（execution） | 构造后不可变 | Canceled 结果带完整历史但无判定 |
| 工程判定 engineeringStatus | Feasible/EngineeringInfeasible/DataInsufficient/NotApplicable | 汇总时（Completed 才有判定） | 构造后不可变 | Completed+DataInsufficient（计算完成但证据不足）；Failed+NotApplicable（执行失败不伪造判定） |
| 当前性 Currentness | Current/Superseded（+不可判定形态） | 每次相对目标上下文计算 | **派生投影**，随时重算，不写回 | Superseded 的 Feasible 历史结果（原快照证据仍有效，仅对当前上下文过期）；Current 的 DataInsufficient 结果（可补证复评） |

三轴互不推导：Superseded 不改变判定；NotApplicable 不因取消而变 Feasible；判定不因当前性而改写（EV-CUR-3）。

### 10.4 关键场景走查（上游 S2/S3/S7 的 evidence 切面）

- **S2 批量验证**：kinematics 组装快照→③端口注册的批量 IK 评估器→失效矩阵按切片传播（TCP 新修订→运动学切片身份变化→Superseded；电机成本变更→Current，AT-05）。
- **S3 优化组合**：优化候选＝基线对象＋补丁→同一编译链→同一注册表执行静态子集评估；UpstreamResult 依赖记录其消费的运动学结果切片；Pareto 比较前经 checkComparisonBaselinesConsistent（§6.5）。
- **S7 迟到结果**：execution 两段式接纳后，evidence 的当前性按原项目 A 当前 HEAD 切片计算（内容身份，不按修订号）；无论当前性如何，结果不进入新会话的"当前结果"（TASK-03——上下文匹配即 §8.1 步骤 1 的拦截）。

---

## 11. 验证与反例矩阵

测试目标 `sdurws_ird_evidence_test`（单元内）与 `sdurws_ird_evidence_contract_test`（跨单元）。**全部为设计用例，未实现未运行**；实现状态随 §12 任务登记。测试设施：testkit 断言/夹具（ContractCheck 谓词消费本文类型——envelope 访问器特化即 testkit §10.2 交接的承接）；**可控评估器替身**（`ScriptedEvaluator`：按脚本返回预设证据/证明/搜索未果/取消抛出，注册于测试内 registry）——替身只验证 evidence 契约与汇总逻辑，**其输出不构成任何业务算法正确性证明**（任务约束§八；自审项 A-9）。

| 组 | 用例（需求/AT 依据｜输入｜操作｜预期｜观测点） |
| --- | --- |
| EV-ID-1 身份确定性 | CON-05/NFR-COR-02｜同内容切片构建两次｜build→比较 sliceId｜相等｜sliceId 逐字节相等；SnapshotCodec/SliceCodec 往返 parse(encode(x))==x |
| EV-ID-2 无浮点近似等价 | §5.1｜两配置容差相差 1e-15（数值上"近似相等"）｜构建两切片比较｜sliceId **不等**（位模式编码）｜缓存/当前性判定不命中；文档断言：容差不进入身份 |
| EV-ID-3 展示无关性 | CON-05/AT-05｜同对象集，改项目显示名/重开"项目"（新会话同内容）｜重建切片｜sliceId 不变、Currentness 不变｜无失效原因条目产生 |
| EV-INV 失效矩阵 | CON-05/AT-05/AT-27｜§5.3 矩阵逐行（电机成本/TCP/几何/负载/摩擦/目录曲线/策略/求解配置/单位/会话姿态）｜改对象→重建目标切片→computeCurrentness｜按矩阵单元格（✓→Superseded+对应 reason；✗→Current；◐→条件值决定且条件输入入切片）｜InvalidationReason.kind 与 dependencyKey 逐条对应；EV-INV 子例：显示单位切换→无任何 reason；求解配置修改→ConfigurationChanged 且 inputBaselineId 不变 |
| EV-VER-1 门禁优先 | EVI-01 §8.1 表 2 C6｜ScriptedEvaluator 返回不可行证明＋快照身份缺失（无策略内容身份）｜aggregateVerdict｜② 级命中→DataInsufficient（证明不豁免门禁）｜VerdictTrace 命中级次=②；missingItems 含快照身份项；证明未进入③ |
| EV-VER-2 搜索未果≠不可行 | C5/AT-03｜替身返回"多初值全发散"（无证明）＋扩大初值后返回有效解（两次输入）｜两次 aggregateVerdict｜第一次 DataInsufficient（附搜索预算/初值数/过滤记录）；第二次 Feasible｜searchRecord 透传；无 EngineeringInfeasible 输出；两次 inputBaselineId 相等、sliceId 不等（配置变） |
| EV-VER-3 构型碰撞仅过滤 | C8/AT-03｜替身返回两组 IK 解：一组碰撞被过滤、一组有效｜aggregateVerdict｜Feasible（碰撞解仅入 filteredSolutions）｜filteredSolutions 含碰撞原因；无证明产生 |
| EV-VER-4 全部解被过滤 | C8/AT-03｜全部解因碰撞过滤｜aggregateVerdict｜DataInsufficient（搜索未果口径）｜searchRecord.filteredSolutions 全为 Collision；不产生 MandatoryStateCollision 证明 |
| EV-VER-5 路径碰撞重规划 | C8/AT-06｜替身：初始候选路径碰撞（路径级证据）→重规划成功｜aggregateVerdict｜Feasible｜路径碰撞记录为淘汰证据，非任务级证明 |
| EV-VER-6 必经状态证明 | C8/表 2③｜替身返回任务点构型（必经）碰撞证明：mandatoryState+collisionPairs 齐备｜validateProof＋aggregateVerdict｜③ 命中→EngineeringInfeasible；缺 mandatoryState 的同款证明→Invalid→④ DataInsufficient｜证明字段级校验逐项观测 |
| EV-VER-7 不适用不计缺失 | C2/表 4 轨迹行｜纯关节路径（条件输入=任务序列对象，无笛卡尔段）｜aggregateVerdict｜TRJ-02 连续性项 NotApplicable（原因=无笛卡尔段），不计缺失；整体不因该缺项降级｜EvidenceItem.status=NotApplicable+notApplicableReason 非空 |
| EV-VER-8 Must/Should | REQ-06｜替身返回 Must 违例 / Should 违例两组｜aggregateVerdict｜Must→EngineeringInfeasible（⑤ 级）；Should→Feasible＋警告诊断｜verdictInputs 透传；VerdictTrace 级次=⑤ |
| EV-COV-1 漏验拦截 | EVI-02/AT-32｜覆盖矩阵缺一个 enabled∧mandatory 工况｜aggregateVerdict｜② 级 DataInsufficient；即使已有包络合并结果亦拦截（DYN-07）｜missingItems 含漏验工况 id；包络条目不替代 |
| EV-COV-2 重复/错误引用 | EVI-02｜矩阵含重复 caseId 条目；含快照 caseSet 外的 caseId｜矩阵校验｜非法（重复拒绝/错误引用 Invalid）｜校验错误列出具体条目 |
| EV-COV-3 零样本与分母 | KIN-04 R8/AT-03｜plannedTotal=0；另例 reachable60+unreachable40+dataInsufficient0（及变体 dataInsufficient>0）｜RegionCoverageEvidence 校验＋汇总｜零样本→DataInsufficient 不输出 0%/100%；固定计数→分母完整性通过、60% 可作参考值；dataInsufficient>0→downgraded=true 整体 DataInsufficient｜分母和校验；downgraded 标记 |
| EV-COV-4 基准一致性 | EVI-02/RPT-04/C5｜两结果 inputBaselineId 不等（采样预算变）／相等｜checkComparisonBaselinesConsistent｜不等→失败并列差异维度；相等→通过｜差异维度逐项输出 |
| EV-ENV-1 组合矩阵 | TASK-02/表 3｜逐一构造：Completed×三种状态合法载荷；Canceled/Failed/Interrupted×NotApplicable；非法组合（Canceled×Feasible、Completed+DataInsufficient+空缺失清单、Preview 模式、Failed+payload）｜ResultEnvelope::make｜合法→构造成功；非法→抛 EvidenceError｜错误码逐条对应表 3；Draft 校验失败消息含字段 |
| EV-ENV-2 部分结果不可复用 | CON-04/TASK-02｜Canceled 结果（partialData）作缓存查询｜judgeCacheHit｜DiagnosticOnly（可读诊断）非 FullHit｜reasons 含 outcome-not-completed；partialData.reusable==false |
| EV-CPA-1 Quick≠Verified | CON-04/OPT-06/表 1｜Quick 结果缓存 vs Verified 请求（sliceId 相同）｜judgeCacheHit｜Incompatible(mode-mismatch)；Quick 证据项在 Verified 判定中=Unverified｜reasons 精确；EvidenceItemStatus 区分 |
| EV-CPA-2 契约版本 | CON-04｜同 sliceId、评估器 contractVersion 不同（模拟算法升级）｜judgeCacheHit/judgeCheckpointCompatibility｜Incompatible(contract-mismatch)｜sliceId 不变而契约条目变→身份含 Environment 条目的直接观测 |
| EV-CPA-3 检查点独立 | CON-04/TASK-01｜上次任务 Failed 的检查点（integrityVerified=true）｜judgeCheckpointCompatibility｜Resumeable（可恢复性与任务成败独立）｜verdict 与上次 outcome 无关字段 |
| EV-CUR-1 内容身份当前性 | CON-02/05、S7｜结果归档后 HEAD 前进（仅电机成本对象变化）｜computeCurrentness（运动学结果）｜Current｜reasons 为空；HEAD 修订号不参与判定 |
| EV-CUR-2 不可解析不默认 Current | §8.1｜目标上下文某依赖对象缺失｜computeCurrentness｜status 空＋unresolved-dependency 诊断（无持久第三状态）｜CurrentnessResult.status==nullopt；diagnostics 非空 |
| EV-CUR-3 历史不可改写 | CON-02/AT-05｜Superseded 判定后重读归档 envelope｜比对字节/摘要｜payload 与 engineeringStatus 不变；currentness 仅存在于投影索引｜envelope 摘要与归档一致 |
| EV-CUR-4 跨上下文 | TASK-03/AT-10｜项目 A 结果对项目 B 上下文查询｜computeCurrentness｜NotEvaluable(cross-context)｜不产生任何项目 B 侧持久状态 |
| EV-REG-1 注册边界 | §9.4｜重复键注册；未知键 find；descriptor 缺 Profile｜注册操作｜重复→拒绝不覆盖；未知键→nullptr；缺 Profile→注册拒绝并指明字段｜错误码与消息 |
| EV-REG-2 并发调用 | §9.4｜多线程 find/create/manifest 并发｜并发只读＋create｜无数据竞争（TSAN 或等价评审证据）、manifest 摘要稳定｜排序稳定性（key 字典序） |
| EV-REG-3 替身边界声明 | 任务约束§八｜全部替身用例｜评审检查｜测试文档显式声明：替身输出仅验证契约，不构成 IK/动力学等算法正确性证明｜测试注释/文档留痕 |

每条用例经 `IRD_TEST_INFO` 登记需求/AT 追溯；契约夹具数据集（切片序列化样本）按 testkit manifest schema 登记（contract-fixture 类）。

---

## 12. 阶段 A 实现任务拆分

局部编号 `EV-Txx`（evidence＝**WP-05**，REQUIREMENTS §3/ARCH §3.1 既有登记；不重排任何上游 WP 编号；DETAILED-DESIGN/development-task-breakdown 产出后如需对齐，以增量修订处理）。依赖顺序自上而下。**阶段 A 交付**：通用契约、快照/切片基础设施、结果构造校验、汇总框架、当前性与兼容判定、评估器注册表、可控测试替身；**不含**：任何业务评估器、真实证据生成、缓存/检查点存储设施（B/C/D 阶段接入，§13）。

| 任务 | 输入 | 产物 | 依赖 | 涉及文件 | 验证方式 | 完成条件 |
| --- | --- | --- | --- | --- | --- | --- |
| EV-T01 构建落位 | 骨架 CMakeLists、§3.4 | `sdurws_ird_evidence` 升级 STATIC（C++17、链 `RWS::ird::core`）；注册 `_test`/`_contract_test`（gtest 按 development-task-breakdown §5.5 定稿） | 无（与 CORE-T01/TK-T01 平行） | `industrialrobot/CMakeLists.txt`、`evidence/CMakeLists.txt`（新）、`evidence/src/*`（空起步） | 独立冒烟＋集成构建配置成功；UT-BUILD 等价扫描（无 Qt、无私有头出 include/、依赖图仅 core 边） | 两模式构建零错误；红线扫描零命中 |
| EV-T02 错误与依赖类型 | §3.5、§4.2 | `Errors.hpp/.cpp`、`Dependency.hpp` | EV-T01 | 同名文件 | 单元测试（token/语法/闭包校验器） | 声明闭包校验用例通过 |
| EV-T03 快照 | §4.1 | `Snapshot.hpp/.cpp`（builder/校验/SnapshotCodec refs-only＋materialized） | EV-T02（core 依赖随目标） | 同名文件 | EV-ID-1/3；修订闭包拒绝（混入反例）用例 | 全部快照不变量用例通过 |
| EV-T04 切片与身份 | §4.3、§5 | `Slice.hpp/.cpp`（builder/SliceCodec/双层身份） | EV-T03 | 同名文件 | EV-ID-1/2、EV-INV 前置（身份比较） | canonical 往返＋浮点位模式＋NaN 拒绝用例通过 |
| EV-T05 证据与 Profile 承载 | §6.1～§6.3 | `Evidence.hpp/.cpp`（状态/清单/Profile/证明/搜索未果/覆盖证据校验） | EV-T04 | 同名文件 | EV-VER-6/7、EV-COV-2/3 | validateProof 逐字段反例通过 |
| EV-T06 汇总判定 | §6.4～§6.6 | `Verdict.hpp/.cpp`（决策表/VerdictTrace/资格/基准检查） | EV-T05 | 同名文件 | EV-VER-1~8、EV-COV-1/4 | 五级优先级全部正反例通过；缺失全量列出 |
| EV-T07 结果包络 | §7 | `Envelope.hpp/.cpp`（Draft/make/validateCombination） | EV-T06 | 同名文件 | EV-ENV-1/2 | 表 3 全组合矩阵用例通过 |
| EV-T08 当前性 | §8.1 | `Currentness.hpp/.cpp`（computeCurrentness/CurrentnessIndex） | EV-T04（重建切片）、EV-T07（结果引用） | 同名文件 | EV-CUR-1~4 | 内容身份/不可解析/跨上下文/不写回用例通过 |
| EV-T09 兼容判定 | §8.2 | `Compatibility.hpp/.cpp` | EV-T07 | 同名文件 | EV-CPA-1~3 | Quick≠Verified/契约/检查点独立用例通过 |
| EV-T10 评估器接口与注册表 | §9 | `Evaluator.hpp/.cpp`（descriptor/接口/两注册表/manifest） | EV-T02、EV-T05（Profile） | 同名文件 | EV-REG-1/2 | 注册边界/并发/manifest 稳定用例通过 |
| EV-T11 测试替身与契约套件 | §11、testkit.md §10.2 | `ScriptedEvaluator` 及 EV-* 全部用例体（`_test`）；envelope 访问器特化与切片契约夹具数据集（`_contract_test`） | EV-T03~T10、testkit 可用 | `evidence/test/*`、`testdata/golden/ev-slice-fixture/` | §11 矩阵逐条 | 全部用例通过并留痕；替身边界声明在案（EV-REG-3） |
| EV-T12 文档与门禁同步 | 全文 | README 指向核对；§15.3 待裁决项状态更新；UT-BUILD 建议并入 CI（WP-01 侧） | EV-T01~T11 | 本文、`evidence/include/.../README.md` | 评审 | 本文与实现零偏差登记；未决项有最新状态 |

每任务完成条件均含"测试通过并留痕"；任何未执行测试不得标注通过。

---

## 13. 后续阶段承接及接口交接清单

| 阶段/单元 | 从 evidence 接收 | 须自行提供（责任） | 典型 AT 载体 |
| --- | --- | --- | --- |
| B·kinematics | 快照/切片/评估器接口/Profile 结构；KIN-04 覆盖证据契约 | `kin.batch-ik`/`kin.region-coverage` 评估器与依赖声明；kin Profile（表 4 运动学行）；覆盖率算法；AnalysisConfiguration schema 与 canonical 编码 | AT-03/05/27 |
| B·modeling/requirements | 快照组装协议（请求方角色）；工况/任务点对象进快照的角色键约定 | 对象 canonical 编码登记（core §6.3 分工）；REQ-06 就绪校验实现（ReadinessSummary 数据源）；工况 schema（P-EV-7） | AT-01/02 |
| B·optimization | 注册表（跨域组合）；UpstreamResult 依赖；比较基准检查；Quick/Verified 缓存判定 | OPT 评估器；候选补丁与编译身份（表 4 优化行）；审计计数消费 | AT-09/12/34 |
| B·reporting | FormalPass/ReviewRecord 两类资格检查；envelope→snapshot 追溯链；复现要素（RPT-03 证据包数据源） | 渲染与措辞（RPT-05 冻结语）；证据包打包 | AT-14/22/32 |
| C·trajectory/dynamics/drivetrain/selection | 同上通用契约；SearchExhausted/路径淘汰证据形态；DYN-06 降级经 EvidenceItem 表达 | 各域评估器与 Profile（表 4 对应行）；TRJ-04 复检证据；DriveTrainMappingEvaluator 注册 | AT-06/07/08/19/38 |
| A·execution（并行交付，接口交接） | envelope 校验器（接纳复用）；judgeCacheHit/judgeCheckpointCompatibility；Registry manifest 摘要比对；IEvaluationContext 实现约定 | 调度/取消/进程/RunRegistry/归档触发；缓存与检查点存储治理；通道 DTO | TASK-01~03、AT-10/13 |
| A·project（协作） | 快照/切片对②端口的读取需求（IObjectBytesSource/IRevisionClosureSource 适配器）；RunManifest 透传字段 | 适配器（L5 或请求方）；results/checkpoints 磁盘协议（其 §10.1 产出后核对，P-EV-6） | CON-01/04 |
| A·runtime/policy（协作） | 内容身份承载约定（ContentIdentity 值传递）；复现块的编译器/碰撞后端版本字段 | 策略与 RuntimeNameMap 的 canonical 序列化与内容身份计算（各自任务卡登记——跨单元身份可比的前提，core R-2 同源） | CON-06、AT-18/19 |
| diagnostics | EvidenceError 的稳定 token 与建议诊断码（EVI-SNAPSHOT-INCOMPLETE/EVI-CASE-COVERAGE-MISSING/EVI-EVIDENCE-MISSING/EVI-PROOF-INVALID/EVI-ENVELOPE-ILLEGAL-COMBINATION/EVI-CACHE-INCOMPATIBLE/EVI-EVALUATOR-DUPLICATE 等——**建议值，码值权威归 StableCodeRegistry**，P-PR-6 同模式） | 码值分配与文案 | ERR-01 |
| ui/workflow | CurrentnessResult（过期原因清单→UX-10"结果过期附原因"）；VerdictTrace 摘要（"未完成"缺项列表数据源） | 七态投影与九态映射；呈现口径（P-EV-4 关联） | UX-10/12 |
| testkit | envelope 访问器特化（checkEnvelopeCombination 消费）；切片/快照契约夹具数据集 | 断言设施（已就绪，§11 消费） | §8.1 表 3 |

阶段 B/C/D 接入顺序约束：任何业务评估器注册前，其 Profile 必须先注册（注册期验证依赖此序）；真实证据生成不得以替身数据冒充（EV-REG-3 边界）。

---

## 14. 需求—设计—验证追踪矩阵

| 需求/上游条款 | 设计落点 | 验证（§11 组） |
| --- | --- | --- |
| CON-01（完整不可变快照） | §4.1 类型/组装/冻结/完整性 | EV-ID-1、EV-T03 用例 |
| CON-02（三态正交；历史证据） | §7.1 组合矩阵、§8.1 投影、§10.3 正交表 | EV-ENV-1、EV-CUR-3 |
| CON-03（外部资源固化） | §4.1.2 externalResources、§6.2 Verified 前置校验 | EV-VER-1（门禁含固化） |
| CON-04（缓存/检查点契约判定） | §8.2、§5.1 身份含契约版本 | EV-CPA-1~3、EV-ID-2 |
| CON-05（切片内容身份；非修订） | §4.2、§5.1/§5.3 | EV-ID-1~3、EV-INV、EV-CUR-1 |
| CON-06（策略/名称映射身份；跨入口一致） | §4.1.2 policyRef/nameMapRef、§9.4 manifest 比对、§6.2 ObjectId 引用契约 | EV-VER-1、EV-REG-2 |
| TASK-02/§8.1 表 3（合法组合） | §7.1 make 校验（SA-13） | EV-ENV-1/2 |
| TASK-03（五元组；会话隔离） | §7.1 task 承载、§8.1 步骤 1 | EV-CUR-4 |
| ERR-01（三轴正交；不适用标记） | §6.2 NotApplicable、§7.1 NotApplicable 强制 | EV-VER-7、EV-ENV-1 |
| EVI-01/§8.1 表 1（模式效力） | §6.2 Unverified、§7.1 Preview 拒绝、§8.2 模式不升降级 | EV-ENV-1、EV-CPA-1 |
| EVI-01/§8.1 表 2（五级优先级；C2/C5/C6/C8） | §6.3 证明契约、§6.4 决策表 | EV-VER-1~8 |
| EVI-01/§8.1 表 4（Profile） | §6.1 结构与消费（内容归需求） | EV-VER-7（适用条件/替代） |
| EVI-02（工况全覆盖；一致基准） | §4.1.3、§6.5、§6.6 | EV-COV-1/2/4 |
| REQ-06（Must/Should；输入未完成） | §6.4 ①⑤ 级 | EV-VER-8、EV-VER-1 |
| KIN-04（样本冻结/分母/降级/零样本——R8） | §4.1.4、§6.6 | EV-COV-3/4 |
| KIN-05（缺检测器→数据不足） | §5.3 条件依赖＋§6.4 ④ 全量列出 | EV-INV（◐ 行） |
| KIN-06/12（会话姿态/显示单位不失效） | §5.3 矩阵行 | EV-ID-3、EV-INV |
| KIN-13（配置入运行/缓存身份；按依赖重算） | §4.2.1 Configuration、§5.1 双层身份、§8.1 reasons | EV-INV、EV-VER-2 |
| TRJ-02 适用条件（C2 例） | §4.2.3、§6.1 applicability | EV-VER-7 |
| DYN-06/07（降级；包络不替代覆盖） | §6.2/§6.6 | EV-COV-1/3 |
| OPT-06/08（Quick/Verified；缓存种子） | §8.2、§9（组合端口） | EV-CPA-1、EV-COV-4 |
| RPT-03/05（证据包要素；两类声明） | §4.1.2 复现块、§7.2 资格检查 | EV-ENV-1（资格字段） |
| NFR-COR-02（确定性要素） | §5.1/§5.2（版本/种子/线程入身份） | EV-ID-1/2 |
| NFR-COR-04（结论→快照/证据追溯） | §7.1 追溯链、§6.4 VerdictTrace | EV-VER-*（trace 观测点） |
| 附录 D 第 12 项（身份精确等值） | §5.1 字节等值比较 | EV-ID-2 |
| ARCH §7.2③/§7.6/SA-07/SA-13 | §3、§4、§7、§9 | EV-REG-*、EV-ENV-* |
| AT-03/05/06/10/19/27/32（场景支撑） | §5.3、§8.1、§9.4、§11 对应行 | EV-INV/VER/CUR/REG |

---

## 15. 设计决策、风险、待裁决项与变更记录

### 15.1 设计决策登记（本文作出并说明理由的普通实现选择）

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | 切片身份对**依赖条目**（oid+cv+角色键+配置内容身份+策略/名称映射/样本/环境版本）计算，不对物化载荷字节计算 | 载荷字节已由 ContentVersion（对象侧）与各内容身份（集合侧）承诺；条目级身份免读全量对象、worker 物化不改变身份。备选（对载荷摘要）浪费且与对象库编址重复 |
| D-02 | 自有二进制 canonical 编码（SliceCodec/SnapshotCodec），不用 JSON | 无可用共享 JSON 库（实测）；编码版本化＋大端＋长度前缀保证跨进程逐字节一致；身份只依赖自身编码器（NFR-COR-02） |
| D-03 | 依赖粒度＝对象级＋语义角色键；不做字段级 | 对象不可变＋内容寻址使对象级即精确；字段级会把各域 schema 知识搬进 evidence（越界 N-5/N-9） |
| D-04 | 双层身份：sliceId（含求解配置/契约版本——缓存键）与 inputBaselineId（仅模型/需求/工况/样本基准）分离 | 同时满足 KIN-13"配置进入运行与缓存身份"与 C5"扩大初值后按同一冻结输入复评"、EVI-02 比较基准——两者对"输入"的界定不同，单一身份无法同时表达 |
| D-05 | 摘要＝SHA-256（经 core ContentDigester），单点实现 | core D-05 同源；跨进程一致；测试向量公开 |
| D-06 | 浮点配置值以 IEEE754 位模式入编码，NaN/Inf 拒绝 | 位模式保证同值同字节（身份可靠）；近似相等无传递性、严禁入身份（EV-ID-2） |
| D-07 | evidence→core 唯一编译依赖；project/policy/runtime 能力经 IObjectBytesSource/IRevisionClosureSource 注入＋内容身份值传递 | ARCH §3.5 依赖表未登记反向边；与 project.md IModelCompilePort 同一注入模式；适配器十行级、非转发包装器 |
| D-08 | envelope 唯一构造入口 make()，非法组合抛 EvidenceError；Preview 在构造边界即拒绝 | SA-13"构造边界一律拒绝"；表 1"Preview 不产生结果对象"的机械化 |
| D-09 | 汇总为纯函数＋VerdictTrace 留痕；证明走字段级 validateProof | 汇总器不凭状态字段采信证明（§1.1）；trace 支撑 NFR-COR-04 追溯 |
| D-10 | 条件依赖的适用条件必须"条件输入入切片"（闭包校验注册期强制） | 漏声明导致错误复用的系统性防线；条件翻转→条件输入身份变→sliceId 变→重解析 |
| D-11 | Environment 依赖条目（产品/契约/编码器/编译器/碰撞后端版本）进入 sliceId | CON-04"算法/契约版本"兼容判定的载体；升版即全体缓存失效（保守正确方向） |
| D-12 | 评估器注册表持 factory、装配期注册、manifest 摘要供主/worker 比对 | worker 各建实例的隔离性＋跨进程一致性（CON-06/AT-19 装配侧）；不建调度器（与 execution 分界） |
| D-13 | Quick↔Verified 缓存互不隐式升降级（一律 Incompatible） | 模式是请求效力属性；隐式降级会以低效力证据冒充（表 1）；保守方向 |
| D-14 | Profile 内容归需求表 4（域按行实例化），evidence 只拥有结构/注册/消费/校验 | EVI-01 全局唯一＋单一权威（NFR-MNT-03）；evidence 不复述明细防双账本 |
| D-15 | EvidenceError 不跨进程边界（通道层由 execution 错误码化） | 与 core §6.2、project 同一进程模型边界 |

### 15.2 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | core.md v0.1 未冻结，其契约（词表/ContentIdentity/DiagData 签名）可能变化 | EV-T02 起返工 | §3.2 消费清单逐项锚定 core.md 章节；core 冻结时按 diff 增量同步（P-EV-1） |
| R-2 | 策略/RuntimeNameMap/域对象的 canonical 序列化归各所有者，若不一致或不稳定，切片身份跨单元不可比或全局漂移 | 缓存误判/大规模无关失效 | §13 交接强制各任务卡登记其 canonical 序列化；契约夹具数据集钉住编码；编码升版走设计变更（D-02） |
| R-3 | 域评估器依赖声明遗漏（本应消费的对象未声明）→错误缓存复用 | 结果看似有效实为过期 | 注册期闭包校验只能防"条件泄漏"，不能防"根本未声明"（evidence 无对象语义）——各域契约测试＋评审覆盖；残余风险显式登记（本文不宣称已消除） |
| R-4 | 跨域汇总歧义（不可行证明与他域证据缺失并存）与空工况集口径未获需求确认 | 汇总行为可能与验收解释冲突 | 按 §8.1 表 2 字面顺序实现＋P-EV-3/P-EV-7 待裁决；裁决前行为可预测且保守 |
| R-5 | project.md 磁盘版本不完整（§8～§15 缺失） | 归档协议/锁/任务拆分细节不可查证，适配器契约可能调整 | 只依赖其 §1～§7 已冻结接口（IProjectQueryPort/IResultArchivePort）；最小注入接口隔离（D-07）；P-EV-6 登记待其补全后核对 |
| R-6 | 大快照物化与序列化的性能（NFR-PERF-03 规模） | worker 派发延迟 | 身份不依赖物化（D-01：refs-only 计算身份）；物化仅限被评估切片；性能属 execution 通道治理 |
| R-7 | 测试替身被误读为业务算法验证 | 验收误判 | EV-REG-3 显式边界声明＋自审项 A-9 |
| R-8 | ARCHITECTURE v0.11 Draft 评审（A9+）可能调整端口/依赖 | §3/§9 返工 | 严格锚定 §3.5/§7.2/§7.6 明文；P-EV-2 登记同步义务 |

### 15.3 待裁决项（问题—依据—影响—建议—需要裁决者）

| # | 问题 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-EV-1 | 本文消费的 core 契约以其 v0.1（Draft）为基线；冻结版可能调整签名/字段（含 P-D-1 词表位置、P-AR-3 证据等级两项 core 侧待裁决的 evidence 侧确认——本文 §3.4 已按"词表归 core、等级＝模式效力"回复） | core.md 文档头状态行、其 §10.2/§10.3 | EV-T02 起返工 | core 冻结时出 diff 清单，本文按影响面增量修订并留痕 | core 详设所有者＋本文所有者 |
| P-EV-2 | ARCHITECTURE v0.11 Draft 待评审；本文以其为基线 | ARCH 文档头 | 评审结论可能要求同步（尤其 §7.6/§7.2③） | 评审后按影响面增量修订 | 架构所有者 |
| P-EV-3 | 跨域汇总顺序：任务级不可行证明（域 A，③）与他域证据缺失（域 B，④）并存时，本文按表 2 字面顺序（③ 先于 ④）输出整体 EngineeringInfeasible；"替代豁免仅限因不可行无法生成的成功产物"（C6）在跨域场景的边界（B 的缺失若与不可行无关）未明文 | §8.1 表 2 ③④、C6 | 整体判定的验收解释可能分歧 | 维持字面顺序（证明为任务级作用域，先命中先出）；如需"B 类无关缺失阻断整体"语义，走需求变更 | 需求所有者 |
| P-EV-4 | 当前性"不可判定"（依赖无法解析/资源缺失/跨上下文）的呈现：上游仅定义 Current/Superseded 两态，本文以 status=空＋诊断表达、不新增持久第三状态；UX-10"结果过期"与 PM-11"是否过期"的 UI 呈现口径需与之对齐 | CON-02/05、UX-10、PM-11 | ui 投影措辞与状态呈现 | 维持两持久态＋不可判定诊断；ui 详设产出时引用本条定呈现 | ui 详设所有者＋需求侧确认 |
| P-EV-5 | 区域覆盖率参考值呈现：KIN-04"数据不足样本存在时覆盖率数值仍可计算（作参考值报告）但结论整体降级"——参考值与正式结论的分离呈现归 reporting/ui；本文仅承载 downgraded 标记 | KIN-04（R8） | 报告措辞（RPT-05 限定语） | reporting 详设引用本条；参考值必须携带"降级中"限定语 | reporting 详设所有者 |
| P-EV-6 | project.md 磁盘版本不完整（实测仅 §1～§7，`__PART3__` 占位，其引用的 §8～§15 缺失）：归档协作协议 §10.1、锁 §9、任务拆分 §12、待裁决 P-PR-1～8 不可查证 | 本文 §1.2 实测 | 与 project 协作的适配器/归档细节可能随其补全调整 | 其补全后核对 IResultArchivePort/RunManifest/IProjectQueryPort 与本文 §3.3/§7.2/§13 的一致性，按增量修订同步；不阻塞本文其余设计 | project 详设所有者 |
| P-EV-7 | 空必验工况集合与全不适用工况的正式判定口径上游未定义；本文保守处置（覆盖平凡完备但判定项不满足→DataInsufficient，不得输出正式通过） | EVI-02、KIN-04 零样本类比 | 边界场景验收 | 保守处置维持至需求侧明文；如需"空集合法通过"语义，走需求变更 | 需求所有者 |
| P-EV-8 | EvidenceItemStatus 词表（Satisfied/Missing/Invalid/Unverified/NotApplicable）为实现承载：上游明文定义缺失（④"缺失"）、不适用（ERR-01/C2）与不可用口径，Unverified（Quick 产物用于 Verified 判定等）为本文最小必要扩展，**不作为需求级冻结契约** | §8.1 表 1/表 2、ERR-01 | 下游（reporting/ui）呈现词表 | 维持五值实现词表并锚定上游语义；若需求侧未来定义证据状态枚举，以增量修订对齐 | 需求所有者＋reporting/ui 详设 |
| P-EV-9 | EVI-02"必验工况"标记的权威 schema（负载工况对象的 enabled/mandatory 字段定义）归 requirements，其详设未产出 | REQ-04、本文 §4.1.3 | RequiredCaseSet 解析口径 | requirements 详设产出时冻结字段并回接本文 §4.1.3；当前以 {caseId, enabled, mandatory} 最小承载 | requirements 详设所有者 |

### 15.4 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-09 | 首版：基于 REQUIREMENTS v1.16（Accepted）与 ARCHITECTURE v0.11（Draft）、core/testkit/project 三份协作输入（均 Draft；project 当前正文 §1～§15 已齐备，联合接口复核待执行，P-EV-6 登记）完成 15 章详细设计；冻结快照/切片/双层内容身份、证据 Profile 承载与五级汇总决策表（C2/C5/C6/C8 口径）、ResultEnvelope 构造校验（SA-13）、当前性纯投影、缓存/检查点兼容判定、评估器接口与注册表；失效矩阵（AT-05）与验证反例矩阵 EV-* 24 组；实现任务 EV-T01～T12；待裁决 9 项（P-EV-1～9）。同日：`evidence/include/sdurws/ird/evidence/README.md` 的任务卡指向由"§9"修正为"§12"（与本文任务拆分章节号一致） |
| v0.2 | 2026-09-10 | FOUNDATION-CR-01 契约冻结审查修正（CR-05）：§4.1.1 规范模型身份由"f(对象闭包, 编译器契约版本) 推导"更正为"runtime 计算值传递"（modelIdentity 组成含编译器版本/基线版本，归 runtime）；§4.2.1 Environment 新增 `runtime.model-identity`/`runtime.robwork-baseline`（消费 CanonicalModel 的评估必填）；§5.1 该二条目划入基准类、进入 inputBaselineId（拦截跨基线比较）；§3.4/EV-T01 行 gtest 引用改指 development-task-breakdown §5.5 定稿。差异记录见 traceability/foundation-api-diff.md |

### 15.5 交付前自审记录（v0.1；自审≠实现测试≠正式验收）

| 检查项（任务约束§九） | 结论 | 证据位置 |
| --- | --- | --- |
| 是否重复 core/project/execution/runtime/policy 的职责 | ✔ 未重复：身份/摘要/词表/比较全经 core（§3.2）；磁盘/事务/锁归 project、调度/RunRegistry/取消归 execution、策略/碰撞归 policy、编译/名称归 runtime——均经注入或值传递（§2.1、§3.3、D-07） | §2、§3 |
| 是否存在反向依赖或 Qt 渗入 | ✔ 编译依赖仅 core＋std（§3.2）；评估器/适配器/总线全为注入或注册；零 Qt（EV-T01 红线扫描承载） | §3、§12 EV-T01 |
| 是否混淆完整性、当前性、工程判定 | ✔ 三轴正交表＋构造边界强制＋当前性纯投影不写回（§7.1、§8.1、§10.3；EV-ENV-1/EV-CUR-3） | §7、§8、§10.3 |
| 是否把搜索失败或局部碰撞升级为任务不可行 | ✔ 搜索未果→DataInsufficient（附记录）；构型/路径碰撞仅过滤/淘汰；任务级仅三类证明且字段级校验（§6.3/§6.4；EV-VER-2~5） | §6 |
| 是否遗漏通用证据门禁或工况覆盖 | ✔ 门禁为②级且不可行证明不豁免（C6）；覆盖矩阵为门禁组成、包络不替代（EVI-02/DYN-07；EV-VER-1/EV-COV-1） | §6.4、§6.6 |
| 是否用整个修订代替实际输入依赖 | ✔ 失效按切片条目内容身份；HEAD 前进不自动 Superseded（CON-05；EV-ID-3/EV-CUR-1） | §5.1、§5.3、§8.1 |
| 是否对缓存与检查点采用了同一套错误门禁 | ✔ 两套独立判定接口；部分/失败结果≠正式缓存命中；检查点可恢复性与任务成败独立（§8.2；EV-CPA-2/3） | §8.2 |
| 是否引入未经上游批准的状态、阈值或证据等级 | ✔ 无新增评估模式/证据等级/容差/阈值；Unverified 为实现承载并登记 P-EV-8；两态当前性＋不可判定为计算形态（P-EV-4） | §6.2、§8.1、§15.3 |
| 后续业务单元能否据此产生、提交和消费证据 | ✔ 注册→声明依赖→组装快照→冻结切片→评估→证据/证明→汇总→包络→归档→当前性的全链契约与交接清单（§4、§6、§7、§9、§13） | 全文 |
| 替身结果是否被当作业务算法正确性证明 | ✔ EV-REG-3 显式边界声明；§11/§12 完成条件要求留痕 | §11 |

## AI 执行就绪补充

本单元进入 AI 实现前必须满足：

- 本文中的职责、非职责、公共接口、数据模型、错误语义和不变量不得与 ARCHITECTURE.md 冲突。
- 单元任务使用 $(evidence.ToUpper())-Txx 编号，并通过 doc/industrial-robot-design/tasks/*.json 声明前置任务、允许修改文件和验证命令。
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
