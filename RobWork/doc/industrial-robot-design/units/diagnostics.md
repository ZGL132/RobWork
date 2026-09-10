# 工业机械臂设计软件 · diagnostics 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案） |
| 日期 | 2026-09-10 |
| 状态 | **`Draft`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收） |
| 文档代号 | UNIT-DIAGNOSTICS |
| 单元 | diagnostics（平台服务，L3；ARCHITECTURE §2.3/§3.1：诊断对象与稳定码注册表、可确认诊断〔ConfirmableFinding〕、诊断目录、两级日志与脱敏） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md` **v0.1**、`units/testkit.md` **v0.1**、`units/project.md` **v0.1**（均 `Draft` 未冻结）；`units/evidence.md` **v0.1**、`units/runtime.md` **v0.1**、`units/policy.md` **v0.1**、`units/execution.md` **v0.1**（均 `Draft` 未冻结）。本文消费的 core 公共契约以 core.md v0.1 签名为基线并逐项标注状态（§3.2）；各协作输入如有变更，本文按影响面增量同步（待裁决 P-DIAG-1） |
| 上游下游链位置 | ARCHITECTURE §11.1 / DETAILED-DESIGN.md：`units/*.md` 单元任务卡。本文即 `units/diagnostics.md`，按任务卡深度编写（接口签名、数据类型在本文件冻结）；对应 development-task-breakdown **WP-09-T01**（"编写 diagnostics 单元任务卡"）的产物 |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/`（骨架已建：目标 `sdurws_ird_diagnostics`〔INTERFACE 占位〕＋别名 `RWS::ird::diagnostics`＋公共头保留位 `include/sdurws/ird/diagnostics/README.md`；README 已指向本文 §11，实测一致） |
| 任务包 | WP-09（诊断与日志；ERR-01 主WP、NFR-SEC-07 主WP、NFR-REL-05 主WP；REQUIREMENTS §3 阶段 A〔WP-00～12〕；DETAILED-DESIGN.md 主 WP-D＝WP-08/09/04-T10~15 能力组） |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 历史实现仅功能范围对照且**当前磁盘缺失**（core.md §1.2 R-5 同源登记）；不复制或恢复 `old/` 历史实现（任务约束§三） |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 diagnostics 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 diagnostics 的职责（§3.1 单元总表行；§7.8 诊断单一权威 SA-12；§7.1 可确认诊断放行 SA-15 的 ConfirmableFinding 设施部分；§3.5 依赖表 diagnostics→core 边），把诊断数据模型（DiagnosticEntry 目录信封、稳定诊断码注册表、分类与严重级别映射）、ConfirmableFinding 的创建/绑定/确认生命周期、诊断目录、两级日志、脱敏与崩溃诊断文件、跨单元错误转换、公共接口、线程模型、验证方案与阶段 A 任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不收窄、不扩大；本文引用的需求 ID 与章节号均指 v1.16 当前正文（重点：ERR-01、MDL-06④〔M-10〕、TASK-01～03、CON-02/04、EVI-01/02、PM-08/15/17、UX-02/03/10、NFR-SEC-07、NFR-REL-05、NFR-MNT-03、附录 D）。架构归属以 `ARCHITECTURE.md` v0.11 为准（SA-12/SA-15 直接约束本单元）；本文对其含混或未登记事项的解释集中登记于 §14.3（P-DIAG-1～P-DIAG-9），不私自修改上游。

**权威与约束复述（贯穿全文）**：`REQUIREMENTS.md` 是需求及验收标准的唯一权威；`ARCHITECTURE.md` 决定诊断层次、确认放行流和跨单元契约；diagnostics 属 L3 平台服务，仅依赖 core（§3.5 表）。diagnostics **不拥有**工程判定、业务算法、项目持久化或 UI 对话框；`ConfirmableFinding` 的数据契约与 core 共享契约一致（core.md §4.8 基类型），**实现设施归 diagnostics**；不自行新增错误等级、证据等级、工程状态或确认语义；不以日志文本代替稳定错误码；不记录未经允许的敏感路径、用户数据或完整外部资源内容；不让工作进程直接请求 UI 确认（SA-15：确认只发生在命令边界）；不把用户确认视为命令成功，也不在确认后跳过重新校验。

### 1.2 上游与磁盘现状登记（2026-09-10 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文承接：ERR-01（诊断字段规范主 WP-09）、MDL-06④＋M-10（行程上限比较型诊断→显式确认放行）、UX-03（失败提示三要素；正常取消非错误）、PM-08/15/17（恢复诊断/统一诊断目录/异常诊断文件）、NFR-SEC-07（脱敏）、NFR-REL-05（两级日志）、NFR-MNT-03（文案单一权威）、CON-02/TASK-02（三轴正交）、TASK-03（五元组关联）、EVI-01/02（证据缺失与数据不足口径）、AT-01/10/11/13/19/30/34 |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。§3.1 diagnostics 行、§3.5 依赖表（diagnostics→core；project/execution/io/ui/reporting→diagnostics 四条入边）、§7.8 SA-12（契约/实现分离：数据契约在 core、注册表与两级日志设施在 diagnostics）、§7.1 SA-15（可确认诊断放行流）直接约束本文。若评审产生 A9+ 处置按影响面同步（P-DIAG-2） |
| `units/core.md` | 存在，v0.1，`Draft`（**未冻结**） | 协作输入。本文消费其 DiagnosticRecord/ComparativeFields/ConfirmableFinding/ConfirmationCredential 契约（§4.8/§5.7）；其 §10.3 已向 diagnostics 交接"StableCodeRegistry、码值清单、文案权威、两级日志、脱敏；诊断对象完整性强制（subject 必填边界）"——本文 §4/§5/§7 即该交接的承接答复；其 P-D-1 要求 diagnostics 仅依赖 core 为硬约束（本文 §3.2 遵守） |
| `units/testkit.md` | 存在，v0.1，`Draft`（未冻结） | 协作输入。其 §5.5 checkDiagnosticRecord/checkComparativeFields 即本文 §10 验证的消费断言；其 §10.2 登记 diagnostics 侧义务"StableCodeRegistry 契约夹具"——本文 §10/§11 承接 |
| `units/project.md` | 存在，v0.1，`Draft` | 协作输入。其 §5.0 定义 `IDiagnosticsSink{report/reportDev}` 并列 PRJ-\* 建议码清单（P-PR-6 待本文收编）；其 §5.3.3 ICommandInteraction（ui 确认回调）、§6.7 确认绑定四元组 `{findingDigest, policyContentId, commandDigest, baseRevisionId}`、§6.10 命令时序——本文 §5 与之对接 |
| `units/evidence.md` | 存在，v0.1，`Draft` | 协作输入。其 §13 列 EVI-\* 建议码清单（P-PR-6 同模式）；其 EvidenceItemStatus 五值与 NotApplicable 口径为本文分类映射的判定轴锚点 |
| `units/runtime.md` | 存在，v0.1，`Draft` | 协作输入。其 §10.11 列 RT-\* 建议码清单（12 项）；其转译规则"适配层不吞异常、用户诊断不含调用栈"为本文 §8.3 错误转换的对接口径 |
| `units/policy.md` | 存在，v0.1，`Draft` | 协作输入。其 §9.6 IPolicyDiagnostics 列 POLICY-\* 建议码清单（约 24 项）；其 §9.4 IJointLimitEvaluator 的 TravelLimitExceeded 比较型结果是 ConfirmableFinding 的领域来源之一 |
| `units/execution.md` | 存在，v0.1，`Draft` | 协作输入。其 §3.3 IExecutionDiagnosticsSink（对齐 project §5.0 形态，P-EX-8 待统一）、§3.4 EX-\* 建议码清单（18 项）；其边界规则"worker 不与 UI 交互、诊断一律经通道 ErrorReport 回传"为本文 §7.5 worker 日志回传的对接口径 |
| `units/diagnostics.md` | **不存在**（本文新建） | 目标文件由本文创建；无既有内容可审核整合 |
| `DETAILED-DESIGN.md` | 存在（45 行，实测） | 20 单元状态索引：diagnostics 标记"待产出"，主 WP-D；规定单元卡最低结构（职责与非职责/输入输出契约/公共接口/数据模型/状态机/错误与诊断/依赖/单元测试/与主 WP 的任务映射/开放问题）——本文 §2/§3/§9/§4~§7/§5.4/§8/§10/§11/§14.3 覆盖全部十项 |
| `development-task-breakdown.md` | 存在（729 行，实测） | 已登记 WP-09-T01～T06；WP-09-T01 即本文产物，要求"卡含：StableCodeRegistry 码值分配权威（收编 PRJ-\*/RT-\*/POLICY-\*/EVI-\* 建议清单）、两级日志接口（P-PR-6 消账）、脱敏、诊断目录、公共头与任务拆分；不裁决码值外的需求语义"——本文逐项承接；其 §5.5 gtest 接入已定稿（vcpkg `find_package(GTest CONFIG REQUIRED)`）；其 §1.1 指出"diagnostics 虽为 L3，但被 project/execution/io/ui 依赖，其任务卡与 StableCodeRegistry 必须先于四者实现"；M2 里程碑含"WP-09 diagnostics 全绿" |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 单元目录＋`patches/`（23 文件）；diagnostics 目录实测仅含 `include/sdurws/ird/diagnostics/README.md`（指向本文 §9 的引用随本文结构修正为 §11，见变更记录） | `sdurws_ird_diagnostics` 为 INTERFACE 占位，无源码；`_test`/`_contract_test` 目标按任务卡逐个登记、不预建空目标 |
| `old/` | **不存在于磁盘**（git 亦未跟踪） | 与 REQUIREMENTS v1.10 声明不符（core.md R-5 已登记）；从头构建口径不受影响，本文不引用其任何机制 |

### 1.3 设计目标

1. **统一契约，供六方消费**：UI（诊断表/日志面板/恢复横幅，PM-15）、execution（任务/worker/检查点诊断）、project（命令拒绝/事务失败/恢复诊断）、evidence（证据缺失/数据不足/证明无效）、runtime/policy/io（编译/策略/导入诊断）与业务域单元（领域诊断）依据本文单点契约创建、关联和消费诊断，无需再协商（任务约束§二）。
2. **稳定可追溯可定位**（ERR-01）：稳定诊断码与本地化文案分离；诊断身份与消息文本分离；每条稳定诊断以 `subjectObjectId` 绑定对象并携带局部/运行时名称、原因和建议动作；比较型校验含实际值/期望值/单位；不适用字段显式标记，不伪造数值。
3. **类别正交、不升级语义**：输入错误、策略拒绝、执行失败、取消、中断和工程不可行在分类与呈现上严格区分（CON-02 三轴正交）；碰撞等局部发现不自动聚合为任务级不可行（C8 口径）；正常用户取消不产生错误诊断（UX-03）。
4. **确认放行可控**（SA-15）：ConfirmableFinding 的创建、绑定（输入版本/策略版本/命令摘要）与确认生命周期设施归本文；放行编排归 project（§7.1）、交互归 ui——确认不是自动成功，确认后仍须重新校验绑定。
5. **安全输出**（NFR-SEC-07/NFR-REL-05）：两级日志（用户级无调用栈/内部哈希；开发级详细）；凭据类敏感信息一律不记录；本机路径按配置脱敏；脱敏失败降级为整字段替换而非放行原文。
6. **零 Qt、零反向依赖、不预建空业务**：目标仅依赖 core＋标准库（D-01）；被 project/execution/io/ui/reporting 依赖而**不依赖它们**（§3.5 有向边方向）；阶段 A 交付设施与测试替身，业务领域码值与文案随各域阶段注册（§12），不提前实现业务判定、碰撞算法或完整 UI 对话框。

### 1.4 语言、标准库与构建约束（按仓库实测确认，与 core.md/project.md §1.4 同源）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | 构建缓存 `build/CMakeCache.txt`：Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计；不做平台特定扩展 |
| C++ 标准 | 基线 RobWork `CMAKE_CXX_STANDARD 11`；industrialrobot 目标显式 C++17（core.md D-01） | 同口径：显式 `cxx_std_17`；`std::optional/variant/string_view/chrono/filesystem` 允许；不用 C++20；不引入 Boost 等第三方运行库 |
| Qt | Qt 6.11.1（msvc2022_64）；L3 允许 Qt Core/Gui、禁 Widgets | **diagnostics 目标零 Qt（含 Core）**（决策 D-01）：L3 虽允许但非必需（ARCH §2.3 L3 行）；development-task-breakdown WP-09-T02 明示"零 Qt"；日志/文件/时间用 std；最大化模型测试直调能力（NFR-MNT-01 精神，与 project D-01 同源） |
| RobWork 数学 | `rw::math`（`sdurw_math`，经 core 公共头传递） | diagnostics 不直接包含 rw::math/Eigen 头（本单元契约不含几何数值类型） |
| 异常 | RobWork 惯例异常 | diagnostics 用自有 `DiagnosticsError`（§9.0）；可恢复查询路径提供 `try*` 非抛出变体 |
| JSON/序列化 | 无共享 JSON 库（vcpkg installed 实测无；testkit.md §1.4 同源） | 崩溃诊断文件与日志格式为**自有文本/键值格式**（§7.6）；测试侧 JsonLite 不用于产品路径（testkit D-02 边界同精神） |
| 测试框架 | dtb §5.5 定稿：vcpkg gtest，`find_package(GTest CONFIG REQUIRED)` | `sdurws_ird_diagnostics_test` 按此接入（testkit.md P-TK-1 / core.md P-ENV-2 已由 dtb 消账） |

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有总表（本节先行给出，全文以此为准）

**diagnostics 拥有（实现责任方）**：

| # | 能力 | 上游依据 | 设计落点 |
| --- | --- | --- | --- |
| O-1 | `DiagnosticRecord`（core 契约）的**构造与校验设施**：工厂强制码已注册、subject 边界、比较型三要素、不适用显式标记 | ERR-01、UX-03；core.md §4.8/§10.3 交接 | §4.2、§9.2 |
| O-2 | 稳定诊断码注册表（StableCodeRegistry）：码命名空间、所有者、参数模式、重复注册/未知码/版本冲突/废弃与迁移 | ERR-01、NFR-MNT-03、ARCH §7.8 | §4.5、§4.6、§9.1 |
| O-3 | 诊断严重级别与分类映射（实现承载词表；判定轴词表归 core） | ERR-01、UX-10、§8.1 | §4.3、§4.4 |
| O-4 | `ConfirmableFinding` 的创建、绑定与确认生命周期设施（FindingRecord 服务、绑定四元组、失效条件、FindingId） | SA-15、MDL-06④（M-10）、ERR-01 | §5、§9.3 |
| O-5 | 诊断目录（DiagCatalog：会话态诊断集合、只读投影、稳定排序、清理）与可定位字段 | PM-15、UX-03 | §6.1/§6.2、§9.7 |
| O-6 | 两级日志设施（用户级/开发级、关联 ID、采样节流、多进程合并、崩溃前保留） | NFR-REL-05、ARCH §7.8 | §7.1～§7.6、§9.6 |
| O-7 | 脱敏与安全输出规则（IRedactionService：路径/用户名/环境变量/资源内容/命令载荷；凭据一律不记录；降级行为） | NFR-SEC-07、PM-17 | §7.7、§9.5 |
| O-8 | 崩溃诊断文件写出（脱敏后，PM-17/AT-11） | PM-17、NFR-SEC-07、AT-11 | §7.6 |
| O-9 | 诊断聚合、关联和去重契约（causedBy/relatedTo/supersedes、去重键、稳定排序、原始证据保留） | ERR-01、NFR-COR-02 精神 | §6.3～§6.5、§9.4 |
| O-10 | 跨单元错误转换设施（ErrorCodeTranslator：各单元错误码→稳定诊断；禁字符串匹配） | ERR-01、ARCH §7.8 | §8、§9.2 |
| O-11 | 面向测试的可控诊断源（ScriptedDiagSource）与契约测试基座 | testkit.md §10.2 diagnostics 行 | §10、§11 |
| O-12 | 各消费单元 sink 接口的**实现**（project §5.0 IDiagnosticsSink / execution §3.3 IExecutionDiagnosticsSink 的落地实现与统一，P-PR-6/P-EX-8 消账） | ARCH §3.5 四条入边 | §8.10、§9.7、§12 |

**diagnostics 消费（经依赖白名单与值传递；"消费"指诊断条目**携带**这些单元产生的上下文数据（core 值类型承载），或由这些单元**调用** diagnostics 设施——不存在 diagnostics→它们的编译期或运行时依赖边）**：

| 上下文来源 | 形态 | diagnostics 承接的内容 | 上游依据 | 状态 |
| --- | --- | --- | --- | --- |
| core | 接口依赖（编译链接，**唯一** industrialrobot 边） | 全部身份类型、ContentVersion/ContentIdentity/ContentDigester、DiagCode/DiagnosticRecord/ComparativeFields/ConfirmableFinding/ConfirmationCredential、SourcedValue、TaskIdentity/TaskState/TaskOutcome/EngineeringStatus/EvaluationMode 词表、UnitToken、CoreError | ARCH §3.5 表 | core.md v0.1 **Draft 未冻结**（P-DIAG-1） |
| project | 值传递＋入边调用 | 命令摘要身份、修订/分支/草稿上下文（RevisionId/BranchId 等 core 类型）；project 调用 sink/工厂产出 PRJ-\* 诊断；ConfirmableFinding 由 project 处理器经本工厂构造（SA-15） | ARCH §3.5 project→diagnostics | project.md v0.1 Draft |
| execution | 值传递＋入边调用 | TaskIdentity 五元组、WorkerId、终态与终结原因；execution 调用 sink 产出 EX-\* 诊断；worker 日志/诊断经通道回传（§7.5） | ARCH §3.5 execution→diagnostics | execution.md v0.1 Draft |
| evidence | 值传递＋入边调用 | snapshotId/sliceId、EvidenceItemStatus、缺失清单、DataInsufficient 口径；EV-\*（EVI-\*）诊断码 | ARCH §3.5 reporting→diagnostics 含 evidence 契约消费；EVI-01 | evidence.md v0.1 Draft |
| runtime | 值传递（经调用方） | 编译诊断、RuntimeNameMap 内容身份、RT-\* 码 | MDL-06、ARC-03 | runtime.md v0.1 Draft |
| policy | 值传递（经调用方） | 策略内容身份、POLICY-\* 码、行程上限比较型结果（ConfirmableFinding 领域来源） | ARC-05、MDL-06④ | policy.md v0.1 Draft |
| io | 值传递＋入边调用 | SafePath/BudgetGuard 违规、导入错误行/列、IO-\* 码（码表随 io 卡注册） | NFR-SEC-01～03 | io 卡未产出（§12 交接） |
| 业务域单元 | 值传递＋入边调用 | 领域诊断（MDL-\*/KIN-\*/…），经注册表登记领域码与参数模式 | ERR-01、NFR-MNT-03 | 各域卡未产出（§12 交接） |
| ui | 入边调用（只读投影＋确认回调数据） | 诊断表/日志面板消费只读投影；确认对话消费 FindingRecord 投影；文案呈现归 ui（UX-02 工程用语） | PM-15、UX-03 | ui 卡未产出 |
| reporting | 入边调用 | 报告诊断章节消费稳定码＋安全参数（§8.10）；报告导出不泄露敏感路径 | RPT-01/05 | reporting 卡未产出 |

**diagnostics 不拥有（其他单元所有，本文不实现、不预建桩）**：

| # | 不拥有内容 | 所有者 | 上游依据 |
| --- | --- | --- | --- |
| N-1 | 业务算法及工程判定（碰撞判定、覆盖率、可行性、Must/Should 判定、证据汇总优先级） | policy/kinematics/evidence 等 | ARC-05、§8.1 表 2 |
| N-2 | 命令执行、确认放行**编排**、事务提交与确认留痕持久化 | project | ARC-01、SA-15、project.md §6.7 |
| N-3 | 用户界面布局、确认对话框、向导、诊断表与日志面板的呈现 | ui/workflow | UX-03/09/13、PM-15 |
| N-4 | 项目目录与日志文件的业务归档（.rwdesign 内无诊断/日志存储；修订/结果内诊断的持久化编址） | project | §17 表注、project.md §4.1 |
| N-5 | 任务状态机、worker 调度、取消协议与迟到结果接纳 | execution | TASK-01～03、ARCH §4 |
| N-6 | 结果当前性（Current/Superseded 计算）与三态正交的判定承载 | evidence | CON-02/05、SA-07 |
| N-7 | 碰撞规则、模型编译和资源解析 | policy/runtime | ARC-03/05 |
| N-8 | 报告渲染与措辞冻结（RPT-05 限定语） | reporting | RPT-01～06 |
| N-9 | 任务/进程崩溃的检测与恢复决策（崩溃**事实**的转译归本文，检测与处置归 execution） | execution | NFR-REL-02/03 |
| N-10 | ERR-01 语义字段的数据契约定义（DiagnosticRecord 基类型归 core；本文只拥有扩展信封与设施） | core | ARCH §7.8 契约/实现分离 |

### 2.2 直接承接的需求（diagnostics 为实现责任方或数据契约责任方之一）

| 需求 | diagnostics 承接的部分 | 不可越界的部分 |
| --- | --- | --- |
| ERR-01（三轴正交；稳定诊断绑定对象；局部/运行时名称、原因、建议动作；比较型三要素；不适用显式标记） | 工厂校验设施（码已注册、subject 边界、三要素、NotApplicable）、分类映射、目录可定位字段 | 判定轴/执行轴词表（core）；显示口径（ui/UX-03）；工程判定（各域） |
| MDL-06④＋SA-15（行程上限策略校验→比较型诊断→显式确认放行，确认随命令摘要留痕） | ConfirmableFinding 创建/绑定/生命周期设施；确认记录的数据形状（进入命令摘要） | 确认放行编排与提交（project §6.7）；阈值数值（policy）；确认对话（ui）；逆命令（project/modeling） |
| TASK-02＋§8.1 表 3（取消/失败/中断不得进正式报告；NotApplicable 不伪造判定） | 分类映射区分取消/失败/中断/超时；诊断随 envelope 的形状约束（diagnostics 字段无正式结论声明） | envelope 构造校验（evidence SA-13）；状态机（execution） |
| TASK-03（请求/完成事件携带五元组） | 诊断目录条目关联 TaskIdentity（可追溯到运行/尝试）；迟到结果丢弃时的开发诊断构造 | 五元组核对与 RunRegistry（execution） |
| CON-02（完整性/当前性/工程判定正交） | 三轴在分类/严重级别映射中的正交呈现（诊断不携带或改写任何一轴） | 三轴承载（evidence envelope） |
| CON-04（部分/失败结果不得作正式缓存命中） | 相关诊断码（EX-CHECKPOINT-\*/缓存不兼容）的构造；不因诊断存在而改写命中判定 | 命中判定（evidence judgeCacheHit） |
| EVI-01/02（证据缺失全量列出；数据不足不显示通过） | EV-\*（EVI-\*）码注册；"缺失项全量列出（不短路）"的诊断目录组织（聚合不吞缺失项） | 汇总判定（evidence §6.4） |
| PM-08（崩溃恢复诊断）/PM-15（统一诊断目录） | 恢复诊断的构造设施；诊断目录会话态集合与只读投影（恢复横幅数据源之一） | 恢复扫描（project）；横幅呈现（ui） |
| PM-17（异常退出写出诊断文件，不含敏感信息） | 崩溃诊断文件写出设施＋脱敏（与 WP-24-T02 协作，dtb WP-09-T05） | 应用壳装配与启动异常捕获（L5/WP-24） |
| UX-02（不显示哈希/Schema/内部插件名） | 用户级日志与诊断投影的用户文案键体系（文案值归 ui/文案资源，键归码表） | 呈现（ui） |
| UX-03（失败提示≥对象/上下文/原因/建议动作；比较型三要素；正常取消非错误） | 工厂必填校验；取消类分类的严重级别=Info 且正常取消不产生用户诊断（§4.4） | 显示措辞（ui） |
| UX-10（失败附对象定位与修复建议） | 目录条目的 subject/localName/runtimeName 定位字段 | 七态投影（ui） |
| NFR-SEC-07（崩溃转储与日志不记录凭据类敏感信息；本机路径按配置脱敏） | IRedactionService 全部规则与降级（§7.7） | 配置界面（ui） |
| NFR-REL-05（日志分用户诊断和开发诊断；用户诊断不含无意义调用栈或内部哈希） | 两级日志模型、用户级输出过滤（§7.1） | 日志面板呈现（ui） |
| NFR-MNT-03（诊断文案只有一个权威定义） | 码表登记文案键的唯一性；重复定义注册边界拒绝 | 各域文案内容（域＋文案资源） |
| AT-01/10/11/13/19/30/34（观测点） | §10 验证矩阵逐项映射 | 验收归各 AT 主责单元；testkit 自测≠AT 通过 |

### 2.3 明确不做（非目标，与 §2.1 不拥有表互为补充）

不实现工程判定或证据汇总（evidence）；不实现确认放行编排与命令摘要持久化（project）；不实现 UI 对话框、诊断表与日志面板（ui）；不在 `.rwdesign` 内写任何文件（磁盘写入唯一归属 project——§17 表注；日志与崩溃诊断文件写用户目录，D-10）；不让 worker 直接请求 UI 确认或向 UI 输出（SA-15、execution 边界规则）；不复制或恢复 `old/` 历史实现；不以日志文本代替稳定错误码（码值权威＝注册表）；不自行新增需求未定义的错误等级、证据等级、工程状态或确认语义（分类/严重级别为实现承载并登记 P-DIAG-3）；不预建无消费者的码表条目与空目标（dtb 原则）；不记录密钥、令牌或不必要的完整文件内容（NFR-SEC-07）。

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 组成（公共头模块）

diagnostics 由 10 个公共头模块＋1 个编译实现组成（实现文件随 §11 任务落地，头为契约权威）：

| 头（`diagnostics/include/sdurws/ird/diagnostics/`） | 内容 | 详见 |
| --- | --- | --- |
| `Errors.hpp` | DiagnosticsErrorCode（稳定 token）、DiagnosticsError | §9.0 |
| `DiagCodes.hpp` | DiagnosticCategory/Severity、CodeDescriptor、StableCodeRegistry、IDiagnosticRegistry、内置码表（收编清单 §4.6） | §4.3～§4.6、§9.1 |
| `Catalog.hpp` | DiagContext（关联身份块）、DiagnosticEntry、DedupKey、DiagCatalog、IDiagnosticSink | §4.2、§6、§9.7 |
| `Factory.hpp` | IDiagnosticFactory、ErrorCodeTranslator（跨单元错误转换注册表） | §8、§9.2 |
| `Confirmable.hpp` | FindingId、FindingBinding、FindingRecord、IConfirmableFindingService、IFindingConfirmationSink（ui 回调数据契约） | §5、§9.3 |
| `Aggregation.hpp` | CauseLink（causedBy/relatedTo/supersedes）、AggregationRule、AggregatedEntry、IDiagnosticAggregator | §6.3～§6.5、§9.4 |
| `Logging.hpp` | LogLevel/LogTier、LogRecord、LogChannel、ILogger、ILogSink、WorkerLogBatch | §7.1～§7.6、§9.6 |
| `Redaction.hpp` | RedactionPolicy、RedactionResult、IRedactionService | §7.7、§9.5 |
| `CrashReport.hpp` | CrashReportWriter（崩溃诊断文件，§7.6） | §7.6 |
| `README.md` | 既有保留位说明（不参与编译；指向本文 §11） | — |
| `src/`（实现，随 DIAG-T02 建） | 注册表、目录、工厂与转换表、finding 服务、聚合器、日志器、脱敏器、崩溃文件写出 | §4～§9 |

### 3.2 依赖（含 core 契约消费状态登记）

```
sdurws_ird_diagnostics ──► RWS::ird::core（PUBLIC：身份/摘要/词表/诊断数据契约；sdurw_math 经其传递）
                       ──► C++17 标准库（含 <filesystem>，日志/崩溃文件路径处理）
                       ──✖ 零 Qt（D-01）、零其他 industrialrobot 单元链接（ARCH §3.5：diagnostics→core 唯一边）、
                            零 Eigen/rw::math 直接包含、零 testkit（产品目标，T-1 红线）
入边（他方依赖本文，方向不可逆）：project→diagnostics、execution→diagnostics、io→diagnostics、
                            ui→diagnostics、reporting→diagnostics（ARCH §3.5 表；R-1/R-2 门禁数据源）
运行时注入（零编译依赖）：日志文件路径与轮转配置（L5 装配传入）、脱敏配置（用户级设置，经 ui/L5 传入）、
                            ui 确认回调（project ICommandInteraction 间接持有，本文仅产出数据契约）
```

消费的 core 契约清单（全部来自 core.md v0.1 §4/§5；**状态：Draft 未冻结**，P-DIAG-1）：

| core 契约 | diagnostics 用途 | 状态锚点 |
| --- | --- | --- |
| DiagCode（string，句法 `^[A-Z0-9]+(-[A-Z0-9]+)*$` ≤64） | 码承载（句法校验在 core 工厂；**码值分配与合法性权威＝本文 StableCodeRegistry**——core.md §4.8 原文） | v0.1 §4.8 |
| DiagnosticRecord/ComparativeValue/ComparativeFields | ERR-01 语义字段承载（本文信封内嵌） | v0.1 §4.8/§5.7 |
| ConfirmationState/ConfirmationCredential/ConfirmableFinding | SA-15 基类型（本文服务产出 Confirmed/Rejected 态实例） | v0.1 §4.8/§5.7 |
| SourcedValue\<double\>（四态） | 比较型实际/期望值的 NotApplicable/Invalid 语义 | v0.1 §4.3 |
| ObjectId/ProjectId/BranchId/RevisionId/RunId/AttemptId/TaskIdentity | 诊断关联身份（subject、五元组、修订上下文） | v0.1 §4.1/§5.1 |
| ContentVersion/ContentIdentity/ContentDigester | findingDigest/commandDigest 计算、日志文件完整性（可选） | v0.1 §4.2/§5.2 |
| EvaluationMode/TaskOutcome/EngineeringStatus/TaskState | 三轴正交与九态关联（分类映射的判定/执行轴锚点，P-D-1 结论） | v0.1 §4.7/§5.6 |
| UnitToken | 比较型单位字段 | v0.1 §4.4 |
| CoreError | core 抛错捕获与转发 | v0.1 §4.10 |

### 3.3 命名空间、目标与 CMake 集成

- 命名空间 `sdurws::ird::diagnostics`；目标 `sdurws_ird_diagnostics`（骨架 INTERFACE → DIAG-T02 升级 STATIC），别名 `RWS::ird::diagnostics`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_diagnostics PUBLIC RWS::ird::core)`。
- 测试目标：`sdurws_ird_diagnostics_test`（单元内，含 ScriptedDiagSource）、`sdurws_ird_diagnostics_contract_test`（跨单元契约：与 core 值类型、与 testkit checkDiagnosticRecord、与 project/execution sink 形态对齐；gtest 按 dtb §5.5 接入），随 DIAG-T02 登记；产品目标不链 testkit（T-1 红线）。
- 头包含形式 `#include <sdurws/ird/diagnostics/StableCodeRegistry.hpp>`（经 `DiagCodes.hpp` 聚合或单头均可）；私有实现头不入 `include/`（R-2 纪律）。
- 同层依赖表核对：diagnostics 仅 `→core`（ARCH §3.5 表内既有边）；不新增任何同层边（表外边＝构建失败，SA-10）。

---

## 4. DiagnosticRecord 与诊断码

### 4.1 双层诊断模型（core 承载＋diagnostics 信封）

ARCH §7.8（SA-12）冻结：诊断对象的**数据契约**（DiagnosticRecord、ComparativeFields、ConfirmableFinding 基类型）定义于 core；**注册表与设施实现**归 diagnostics。ERR-01 要求的字段集（码/对象/名称/上下文/原因/建议动作/比较型三要素）即 core 契约的全部语义字段——core 契约**不扩充**（不越权重定义需求字段）。诊断在运行与消费中客观还需要：分类、严重级别、关联身份（运行/快照/修订）、时间、线程、原因链、去重键、稳定排序——这些属"诊断目录和可定位字段"（ARCH §3.1 diagnostics 行明文），由 diagnostics 的**目录信封** `DiagnosticEntry` 承载：

```
core::DiagnosticRecord（ERR-01 语义字段，core.md §4.8 冻结，本节不重复定义）
   ▲ 内嵌（组合，非继承——core D-11 同精神）
diagnostics::DiagnosticEntry（目录信封，本文冻结）
   { record + 分类/严重（来自码表元数据）+ DiagContext 关联身份块
     + 时间/线程/worker + 原因链链接 + DedupKey + 稳定排序键 + 脱敏后上下文快照 }
   ▲ 内嵌（Confirmable 类条目的扩展呈现）
diagnostics::FindingRecord（§5，SA-15 服务端记录；其 record 必为比较型）
```

两层的关系与纪律：

| 维度 | core::DiagnosticRecord | diagnostics::DiagnosticEntry |
| --- | --- | --- |
| 所有权 | core（数据契约） | diagnostics（信封与设施） |
| 持久化形态 | 随宿主对象（envelope.diagnostics / CommandRecord / RecoveryReport）原样存储 | **会话态**（诊断目录），不持久化为独立文件；其 record 部分随宿主对象持久化 |
| 可变性 | 不可变（值语义，core 约定） | 不可变追加（目录内条目构造后不修改；"修改"＝新条目＋supersedes 链接，§6.3） |
| 文案 | context/cause/recommendedAction 为**用户文案键**（§4.5：码表登记标题键/详情键），非最终显示文本 | 同左（透传）；显示文本由 ui/文案资源按键解析（UX-02） |

**稳定码与本地化文案分离／诊断身份与消息文本分离**：`code`（稳定 token，注册表权威）≠ `titleKey/detailKey`（文案键，随码注册）≠ 本地化文本（ui/文案资源产出）。诊断条目身份＝`entryId`（目录内单调序号＋进程标识，§4.2），与任何消息文本无关；比较与去重一律用 code/subject/上下文身份，**绝不用文本比对**。

### 4.2 DiagnosticEntry 字段表（`Catalog.hpp`；全部字段构造后不可变，无 setter）

| 字段 | 类型 | 必填 | 默认 | 约束与语义 |
| --- | --- | --- | --- | --- |
| `entryId` | `DiagEntryId`（std::uint64_t，目录内单调 ≥1；0＝空） | 是 | — | 目录内唯一身份；**仅会话态排序与引用用，不持久化为跨会话身份**（持久化诊断的身份＝宿主对象＋序内偏移） |
| `record` | `core::DiagnosticRecord` | 是 | — | ERR-01 语义字段（工厂已校验：code 句法＋已注册、必填串非空、比较型约束 C-1～C-3——core 工厂校验句法，本文工厂追加"码已注册"与 subject 边界，§9.2） |
| `category` | `DiagnosticCategory`（§4.3） | 是 | — | 来自码表元数据（创建时从注册表解析，不允许调用方覆盖——单一权威，NFR-MNT-03） |
| `severity` | `DiagnosticSeverity`（§4.3） | 是 | — | 同上；仅码表所有者可随码版本演进调整 |
| `context` | `DiagContext`（下表） | 是 | 空块 | 关联身份块（全部 core 值类型；可空但**正式诊断不得为空**——§4.2.1 边界） |
| `emittedAtUtc` | `std::chrono::system_clock::time_point` | 是 | — | 产生时间（UTC；时钟来源＝进程内注入的 IClock——测试可替换，testkit ManualClock 兼容形态） |
| `threadTag` | `std::string`（≤32 字符，ASCII） | 否 | 空 | 产生线程标记（`main`/`cmd`/`sched`/`wk-<n>` 等；词表开放、约定登记于各单元） |
| `workerId` | `std::optional<std::uint64_t>` | 否 | `nullopt` | execution WorkerId（worker 侧产生时必填；主进程产生时空） |
| `causedBy` | `std::optional<DiagEntryId>` | 否 | `nullopt` | 根因链（§6.3；指向目录内另一条目） |
| `relatedTo` | `std::vector<DiagEntryId>` | 否 | {} | 关联诊断（无方向语义，仅导航） |
| `supersedes` | `std::optional<DiagEntryId>` | 否 | `nullopt` | 取代关系（§6.3：重复发现的更新表达，不改写旧条目） |
| `dedupKey` | `DedupKey`（§6.4：{code, subject, scopeKind, scopeId}） | 是 | — | 去重键（构造时计算；**不同 subject 绝不同键**——DT-DUP-2 反例钉住） |
| `orderKey` | `std::tuple<时间戳计数, entryId, code>` | 是 | — | 稳定排序键（§6.4；同输入同序，NFR-COR-02 精神） |
| `redactedContextSnapshot` | `std::optional<std::string>` | 否 | `nullopt` | **脱敏后**的开发级上下文快览（如脱敏后路径/摘要；构造时经 IRedactionService 产出，原文不保留在目录——§7.7） |

`DiagContext`（关联身份块）字段：

| 字段 | 类型 | 必填 | 语义 | 不可混淆边界 |
| --- | --- | --- | --- | --- |
| `project / branch / revision` | `std::optional<core::ProjectId/BranchId/RevisionId>` | 条件 | 项目/方案分支/修订上下文（命令路径诊断必填；worker 运行诊断随任务携带） | 与 TaskIdentity 内同名字段一致（同一来源，不二次推导） |
| `task` | `std::optional<core::TaskIdentity>` | 条件 | 运行五元组（execution 路径必填；TASK-03） | 五元组核对归 execution（本文只承载已核验方传入的值——迟到达 entries 携带未核验身份时由 execution 侧先行核对） |
| `snapshotId / sliceId` | `std::optional<core::ContentIdentity>` | 否 | 快照/切片上下文（评估路径诊断；evidence 值传递） | 身份计算归 evidence（本文不计算） |
| `policyContentId` | `std::optional<core::ContentIdentity>` | 否 | 已解析策略内容身份（CON-06；策略相关诊断与 FindingBinding 复用同一来源） | 策略解析归 policy |
| `commandType / commandDigest` | `std::optional<std::string> / std::optional<core::ContentIdentity>` | 条件 | 命令上下文（命令路径诊断；digest＝SHA-256 over 载荷 canonical 字节，经 core ContentDigester） | 命令服务归 project |
| `sourceUnit / sourceInterface` | `std::string / std::string`（token，≤32/≤64） | 是 | 来源单元（`project`/`runtime`/`policy`/`execution`/`io`/`evidence`/`ui`/业务域 token）与来源接口/通道（如 `commands.submit`、`channel.error-report`） | 词表开放、随码注册（§4.5：码的 ownerUnit 必须与 sourceUnit 一致或为其被授权注册方） |
| `contractVersions` | `std::vector<std::pair<std::string,std::uint32_t>>` | 否 | 策略/算法/契约版本标注（如 `{("evaluator-contract",3),("policy-schema",2)}`） | 版本权威归各所有者，此处仅标注 |

**合法/非法实例**：

- 合法：`{record:{code:"PRJ-LOCK-HELD", subject:joint…, context:"打开项目", …}, category:PermissionOrLock, severity:Warning, context:{project:…, sourceUnit:"project", sourceInterface:"store.open"}, emittedAtUtc:…, dedupKey:{…}}`——码已注册、分类来自码表、上下文含项目身份。
- 合法（开发级瞬时）：`{code:"EX-CHANNEL-PROTOCOL-ERROR", severity:Dev, subject:空, …}`——开发级码允许 subject 空（不进用户目录，§6.2）。
- 非法（工厂拒绝，抛 `DiagnosticsError`）：code 未注册（`code-unknown`）；用户级码 subject 为空（`subject-missing`——core.md §4.8"稳定诊断项必须携带合法 subjectObjectId"的边界强制，即 core §10.3 交接的"完整性强制归 diagnostics/reporting 边界"本文侧落地）；category/severity 与码表不符（`category-mismatch`——直接传入字段不存在于工厂签名，仅可经未登记路径构造，由构造唯一入口阻断）；比较型码（码表 `requiresComparison=true`）缺 comparison（`comparison-missing`，core C-1 的注册表侧强化）。

**序列化**：DiagnosticEntry 本体不序列化（会话态）；其 `record` 部分随宿主对象（envelope/CommandRecord/RecoveryReport）持久化，编码归宿主所有者（project/evidence canonical 编码）；诊断目录的**导出**仅两种安全形态（§7.7）：用户级日志行、`exportSafeSummary`（§9.7，稳定码＋脱敏参数）。

### 4.3 诊断分类与严重级别（实现承载词表）

**判定/执行轴词表不在本文**：TaskOutcome/EngineeringStatus/TaskState/EvaluationMode 归 core（P-D-1 已确认）。本文的 `DiagnosticCategory`/`DiagnosticSeverity` 是**分类映射的实现承载**（上游无需求级枚举定义，登记 P-DIAG-3；P-EV-8 同模式），其语义锚点逐项列于 §4.4 矩阵——**不构成新工程状态**：分类只驱动呈现分组、处理动作建议与日志分流，绝不改写 outcome/engineeringStatus/当前性任何一轴。

`DiagnosticCategory`（15 值，token 小写连字符）：

| 值 | token | 语义（上游锚点） | 典型来源码（§4.6） |
| --- | --- | --- | --- |
| 输入非法 | `input-invalid` | 启用 Must 条目非法/"输入未完成"（REQ-06）；硬断言失败（MDL-06①~④硬） | RT-INPUT-INVALID、POLICY-THRESHOLD-NON-FINITE、域码 |
| 格式/版本错误 | `format-or-version` | 旧格式/未来版本拒绝（PM-06）；schema/契约不兼容（CON-04） | PRJ-FORMAT-LEGACY、PRJ-SCHEMA-FUTURE、POLICY-VERSION-FUTURE、EVI-CACHE-INCOMPATIBLE |
| 权限或锁错误 | `permission-or-lock` | 只读/锁持有/写拒绝（PM-07、SA-17） | PRJ-LOCK-HELD、PRJ-WRITE-AUTHORITY-LOST、EX-STORE-READ-ONLY |
| 资源缺失 | `resource-missing` | 外部源 Missing/Changed（NFR-REL-04）；资源越界 | RT-RESOURCE-MISSING/CHANGED/BUDGET、POLICY-GEOMETRY-MISSING |
| 策略拒绝 | `policy-denied` | 策略评估否决（碰撞过滤、行程上限硬口径等——域内事实，非用户错误） | POLICY-CLL-\* 系列、POLICY-JNT-TABLE-INVALID |
| 可确认诊断 | `confirmable` | 策略校验超限待用户显式确认（SA-15/MDL-06④） | MDL-06-TRAVEL-LIMIT（域码，policy 供比较型结果） |
| 执行失败 | `execution-failed` | outcome=Failed 轴（TASK-02；NotApplicable 判定） | EX-WORKER-CRASHED、EX-TASK-REJECTED、RT-WC-COMPILE-FAILED |
| 取消 | `canceled` | outcome=Canceled 轴（正常取消**非错误**，UX-03） | （正常取消无用户诊断；取消失败归执行失败类） |
| 中断 | `interrupted` | outcome=Interrupted 轴（NFR-REL-03"已中断"） | EX-TASK-INTERRUPTED |
| 超时 | `timeout` | 心跳失联/取消协议超时/等待超时 | EX-WORKER-HUNG、EX-FORCE-TERMINATED |
| 数据不足 | `data-insufficient` | engineeringStatus=DataInsufficient 轴（§8.1 表 2②④；搜索未果 C5） | EVI-EVIDENCE-MISSING、EVI-SNAPSHOT-INCOMPLETE、KIN-05 口径（POLICY-CLL-DETECTOR-UNAVAILABLE） |
| 证据缺失 | `evidence-missing` | 必需证据 Missing/Invalid（表 2④ 全量列出） | EVI-CASE-COVERAGE-MISSING、EVI-PROOF-INVALID |
| 工程不可行证明 | `infeasibility-proof` | engineeringStatus=EngineeringInfeasible 轴——**有效工程结论而非错误**（RPT-05 可作正式评审记录；C8 作用域） | 域码（kin/trj 证明类） |
| 内部错误 | `internal` | 防御性检查失败/通道协议错误/不变量违反（开发级为主） | EX-CHANNEL-PROTOCOL-ERROR、EX-REGISTRY-MISMATCH、PRJ-STORE-CORRUPT |
| 安全/脱敏错误 | `security-or-redaction` | SafePath 违规、预算超限、脱敏失败 | IO-\*（io 卡注册）、DIAG-REDACTION-FAILED |

`DiagnosticSeverity`（4 值，token）：

| 值 | token | 含义 | 进入用户目录 | 进入用户级日志 | 进入开发级日志 | 进入报告 |
| --- | --- | --- | --- | --- | --- | --- |
| `Error` | `error` | 阻断当前操作/任务失败/数据损坏风险 | ✔ | ✔ | ✔ | ✔（随宿主） |
| `Warning` | `warning` | 不阻断但需用户知晓（含可确认类、策略拒绝类） | ✔ | ✔ | ✔ | ✔（随宿主） |
| `Info` | `info` | 有效结论/告知（不可行证明、默认值已填入、中断呈现） | ✔ | ✔ | ✔ | ✔（随宿主） |
| `Dev` | `dev` | 开发诊断（无用户语义；NFR-REL-05 开发级） | ✗ | ✗ | ✔ | ✗ |

**传播规则**：severity 归属码表（注册时声明），**不随实例变化**；目录/日志按 severity 分流（上表）；聚合（§6.4）取成员最高 severity（Dev 不参与用户级聚合）；诊断**不向上传播为异常**（diagnostics 设施不抛诊断、不吞调用方异常——§9.0）。

### 4.4 分类—严重级别—处理动作矩阵

每行冻结：该分类的**默认严重级别**（码表注册时可收紧至更具体但不得跨"用户级/开发级"分界——登记于码元数据）、**典型处理动作**（recommendedAction 的动作族，供 ui 呈现与 `actionKind` 元数据）、**UI 映射**（UX-10 七态/日志面板的呈现去向）。分类与三轴的对应仅供呈现分组，**不改写任何轴**。

| 分类 | 默认严重 | 处理动作族（actionKind） | UI 映射 | 三轴锚点（只读呈现） |
| --- | --- | --- | --- | --- |
| 输入非法 | Error | `fix-input`（定位对象/字段→编辑） | 失败态＋对象定位（UX-10） | —（评估未派发，REQ-06） |
| 格式/版本错误 | Error | `open-upgrade-guide` / `choose-compatible` | 失败态（PM-06 升级指引） | — |
| 权限或锁错误 | Warning | `retry-readonly` / `contact-holder`（显示 PID） | 只读横幅（PM-07/11） | — |
| 资源缺失 | Error | `relink` / `reimport`（PM-09 流程入口） | 失败态＋定位 | — |
| 策略拒绝 | Warning | `adjust-policy`（统一工程策略入口，UX-08） | 诊断表 | 域内事实（不上升任务级——C8） |
| 可确认诊断 | Warning | `confirm-or-fix`（确认对话/收窄参数） | 确认对话（SA-15） | — |
| 执行失败 | Error | `retry-task` / `inspect-log` | 失败态（UX-10） | outcome=Failed ∧ status=NotApplicable |
| 取消 | Info（且正常取消**不产生用户诊断**） | `none`（无动作） | 无错误呈现（UX-03） | outcome=Canceled ∧ status=NotApplicable |
| 中断 | Info | `rerun-interrupted` | "已中断"可重跑（NFR-REL-03/PM-08） | outcome=Interrupted |
| 超时 | Error | `retry-task` / `inspect-worker` | 失败态＋EX-FORCE-TERMINATED 区分标记 | outcome=Failed（强杀） |
| 数据不足 | Warning | `supply-evidence`（补全输入后同基准复评，C5） | 数据不足态＋缺项列表（UX-10） | status=DataInsufficient |
| 证据缺失 | Warning | `supply-evidence`（全量列出，不短路） | 数据不足态 | status=DataInsufficient |
| 工程不可行证明 | Info | `review-proof`（呈现证明与前提） | 结论呈现（RPT-05 评审记录） | status=EngineeringInfeasible |
| 内部错误 | Dev（个别如 store-corrupt 为 Error） | `report-bug`（附安全摘要） | 开发日志/恢复横幅 | — |
| 安全/脱敏错误 | Error | `inspect-resource` / `report-bug` | 失败态 | — |

**不得自行增加需求未定义的工程状态**：本表右列全部为**已存在轴值的呈现映射**；分类/严重/动作族是实现承载（P-DIAG-3 登记），任何新分类＝需求变更评估＋本文修订。

### 4.5 稳定诊断码注册表（StableCodeRegistry）

**码命名空间与句法**：`DiagCode` 句法沿用 core（`^[A-Z0-9]+(-[A-Z0-9]+)*$`，≤64）。命名空间约定：**首段＝单元短前缀**（平台：`PRJ`/`RT`/`POL`〔policy 卡用 POLICY-\*，注册表按原文收编，前缀归一为 POLICY〕/`EVI`/`EX`/`IO`/`UI`/`DIAG`；业务域：`MDL`/`REQ`/`KIN`/`TRJ`/`DYN`/`SEL`/`OPT`/`WF`）——前缀即所有权声明，跨前缀注册拒绝。**码值一经注册并进入任何持久化产物即不再改义、不改拼**（持久化契约；改名＝新码＋旧码废弃迁移）。

`CodeDescriptor`（注册项，构造后不可变）：

| 字段 | 类型 | 必填 | 约束 |
| --- | --- | --- | --- |
| `code` | `DiagCode` | 是 | 唯一键；前缀＝ownerUnit 声明域 |
| `ownerUnit` | `std::string` | 是 | 所有者单元 token（如 `project`）；阶段 B 起业务域注册须域卡在案 |
| `category` / `severity` | §4.3 枚举 | 是 | 分类与默认严重 |
| `titleKey` / `detailKey` | `std::string` | 是 | 用户文案键（`diag.<code-lower>.title/detail` 命名约定；键唯一性注册期校验——NFR-MNT-03 文案单一权威） |
| `paramSchema` | `std::string`（受限 JSON 形 schema token 列表） | 是 | 参数模式：命名参数清单（如 `["pid","host"]`）；实例的 context/cause/comparison 须按模式填充（工厂校验占位一致性，§9.2） |
| `confirmable` | `bool` | 是 | 是否为可确认类（true ⇒ requiresComparison=true，SA-15：可确认必为比较型——core C-1 注册表侧强化） |
| `requiresComparison` | `bool` | 是 | 比较型三要素强制（ERR-01/UX-03） |
| `retryable` | `enum{Never,UserRetry,AutoRetry}` | 是 | 可重试性（处理动作族的机器锚点） |
| `userVisible` | `bool` | 是 | 是否允许对外显示（Dev 码=false） |
| `reportable` | `bool` | 是 | 是否允许进入报告（reporting 消费；Dev 码=false） |
| `historical` | `bool` | 是 | 是否允许进入项目历史（随修订/结果持久化；Dev 码=false——瞬时开发诊断不污染历史） |
| `registryVersion` | `std::uint32_t` | 是 | 注册版本（每次该码元数据变更＋1；进入诊断实例的 `contractVersions` 标注，供报告侧检测文案/语义演进） |
| `deprecated` / `supersededBy` | `bool` / `std::optional<DiagCode>` | 否 | 废弃标记与迁移目标（§4.5.1） |

**注册表行为（冻结）**：

| 行为 | 规则 |
| --- | --- |
| 注册时机 | L5 装配期（主进程与 worker 进程执行同一注册清单——跨进程码表一致；与 evidence EvaluatorRegistry manifest 摘要同模式，注册表提供 `manifest()` 摘要供 worker 握手比对）；运行期不增删 |
| 重复注册 | 同 code 再次注册 → 注册边界拒绝 `DiagnosticsError(duplicate-code)`，**不覆盖、不静默**（NFR-MNT-03） |
| 未知码 | `find(code)` 返回 nullptr（查询非抛）；工厂以未注册码构造 → `code-unknown` 拒绝；**反序列化侧**遇未知码 → 宿主对象按各自契约处置（历史修订浏览不解析未知码 payload，project §6.3 同口径；报告侧遇未知码显示占位并开发诊断，不崩溃） |
| 版本冲突 | 同 code 不同 registryVersion 的并发注册＝重复注册拒绝；**码元数据演进＝装配清单变更**（新版本新清单，跨进程一致），不存在运行期热替换 |
| 注册期验证 | 句法、前缀-所有权一致、titleKey/detailKey 唯一、confirmable⇒requiresComparison、Dev 码 userVisible/reportable/historical=false、paramSchema 非空且参数名合法——任一失败即拒绝并指明字段 |
| 业务单元注册领域码 | 域在装配期以本注册表 API 登记（携带域卡在案证明的 ownerUnit=域 token）；**不复制 diagnostics 设施**（域仅提供 CodeDescriptor 数据，设施单点在本文）；诊断码**不能通过异常文本临时生成**——工厂只接受已注册码，异常文本仅可经 ErrorCodeTranslator 的**已登记错误码映射**转译（§8.1），字符串直接作码被 `code-unknown` 拒绝（DT-REG-4 反例） |
| 线程安全 | 注册期单线程约定（装配）；运行期 find/manifest 并发只读安全 |

### 4.5.1 废弃码与迁移

- **废弃**：码元数据置 `deprecated=true`（装配清单修订），旧持久化产物中的该码仍可**只读解析**（码表保留 tombstone：`{code, deprecated, supersededBy}`，不参与新实例构造——工厂拒绝 deprecated 码 `code-deprecated`）。
- **迁移**：`supersededBy` 指向新码；历史产物**不重写**（PA-2 不可变历史）；报告/呈现侧遇 deprecated 码按 tombstone 映射到新码文案键并保留原码原文（追溯不破坏）。
- **删除禁止**：tombstone 一经发布不删除（持久化兼容）。

### 4.6 内置码表（阶段 A 收编清单；码值权威＝本注册表，不重排各卡已登记建议值——dtb WP-09-T03 约束）

收编四份建议清单（project §5.0、runtime §10.11、policy §9.6、evidence §13）＋本文自用码＋execution §3.4 清单。分类/严重为本文登记值（P-DIAG-3）：

| 前缀 | 码（收编，全量） | 分类 | 严重 | 备注 |
| --- | --- | --- | --- | --- |
| PRJ | PRJ-LOCK-HELD、PRJ-STORE-CORRUPT、PRJ-RECOVERY-IGNORED-UNCOMMITTED、PRJ-RECOVERY-ORPHAN-DRAFT、PRJ-RECOVERY-DANGLING-OBJECTS、PRJ-SCHEMA-FUTURE、PRJ-FORMAT-LEGACY、PRJ-STALE-REVISION-REJECTED、PRJ-ARCHIVE-CONFLICT、PRJ-WRITE-AUTHORITY-LOST | 权限/内部/权限/权限/内部/版本/版本/输入/执行/权限 | Warning/Error/Info/Info/Dev/Error/Error/Error/Error/Error | STORE-CORRUPT 定位文件；RECOVERY-\* 随 RecoveryReport（PM-08/15） |
| RT | RT-INPUT-INVALID、RT-STRUCTURE-INVALID、RT-UNIT-MISMATCH、RT-RESOURCE-MISSING、RT-RESOURCE-CHANGED、RT-RESOURCE-BUDGET、RT-WC-COMPILE-FAILED、RT-DWC-COMPILE-FAILED、RT-NAME-CONFLICT、RT-BASE-WORLD-INCONSISTENT、RT-CAPABILITY-MISSING、RT-ROBWORK-ERROR、RT-CACHE-INCOMPATIBLE、RT-CANCELLED | 输入/输入/输入/资源/资源/安全/执行/执行/内部/内部/资源(Warning)/执行/版本/取消(Info) | Error… | runtime §10.11 全量 14 项；RT-CANCELLED 定 Info（正常取消非错误——UX-03） |
| POLICY | POLICY-SCHEMA-UNKNOWN-FIELD、POLICY-SCHEMA-VERSION-FUTURE、POLICY-SCHEMA-VERSION-UNKNOWN、POLICY-THRESHOLD-NON-FINITE、POLICY-THRESHOLD-NON-POSITIVE、POLICY-THRESHOLD-OUT-OF-RANGE、POLICY-UNIT-MISMATCH、POLICY-RULE-DUPLICATE、POLICY-RULE-CONFLICT、POLICY-RULE-CYCLE、POLICY-SCOPE-OBJECT-MISSING、POLICY-APPLICABILITY-INVALID、POLICY-VERSION-INCOMPATIBLE、POLICY-CONTENT-IDENTITY-MISMATCH、POLICY-CLL-DETECTOR-UNAVAILABLE、POLICY-CLL-SCENE-INVALID、POLICY-CLL-NAME-UNRESOLVED、POLICY-CLL-CONTEXT-EXPIRED、POLICY-CLL-EVALUATION-FAILED、POLICY-CLL-GEOMETRY-MISSING、POLICY-JNT-TABLE-INVALID、POLICY-ENGINEERING-RANGE-INVALID、POLICY-INFO-DEFAULT-APPLIED | 版本×3/输入×8/版本×2/数据不足/策略拒绝×4（CLL-EVALUATION-FAILED 归执行失败）/资源/输入/策略拒绝/信息 | Error…Info | policy §9.6 全量；CLL-DETECTOR-UNAVAILABLE=KIN-05 数据不足口径 |
| EVI | EVI-SNAPSHOT-INCOMPLETE、EVI-CASE-COVERAGE-MISSING、EVI-EVIDENCE-MISSING、EVI-PROOF-INVALID、EVI-ENVELOPE-ILLEGAL-COMBINATION、EVI-CACHE-INCOMPATIBLE、EVI-EVALUATOR-DUPLICATE | 数据不足/证据缺失/证据缺失/证据缺失/内部/版本/内部(Dev) | Warning×3/Warning/Dev/Dev | evidence §13 全量；EVALUATOR-DUPLICATE 为装配期错误 |
| EX | EX-TASK-REJECTED、EX-SNAPSHOT-STALE、EX-STORE-READ-ONLY、EX-RESOURCE-INSUFFICIENT、EX-CAPABILITY-UNSUPPORTED、EX-WORKER-LAUNCH-FAILED、EX-WORKER-CRASHED、EX-WORKER-HUNG、EX-FORCE-TERMINATED、EX-CHANNEL-PROTOCOL-ERROR、EX-REGISTRY-UNKNOWN-RUN、EX-REGISTRY-MISMATCH、EX-STALE-ATTEMPT、EX-CHECKPOINT-CORRUPT、EX-CHECKPOINT-INCOMPATIBLE、EX-ARCHIVE-FAILED、EX-ARCHIVE-AUTHORITY-LOST、EX-TASK-INTERRUPTED | 执行×5（含超时×2）/Dev×4（协议/登记表）/版本×2/执行×2/中断 | … | execution §3.4 全量 18 项；四个"开发级"标 Dev |
| DIAG（本文自用） | DIAG-REDACTION-FAILED、DIAG-REGISTRY-DUPLICATE、DIAG-REGISTRY-UNKNOWN-CODE、DIAG-CATALOG-OVERFLOW、DIAG-LOG-WRITE-FAILED、DIAG-FINDING-BINDING-INVALID、DIAG-FINDING-EXPIRED | 安全/内部×3/内部/可确认类辅助×2 | Error/Dev×3/Dev/Error/Info | 诊断设施自省码（红线的红线：诊断设施自身的失败也走稳定码） |
| 示例域码（**阶段 B 起注册**，本文仅登记命名约定不预建条目） | 如 MDL-06-TRAVEL-LIMIT（policy §7.4 TravelLimitExceeded 的确认流呈现，confirmable=true） | 可确认 | Warning | 不预建：dtb"不预建无消费者条目"；域卡产出时注册 |

IO-\*/UI-\* 码表随 io/ui 卡注册（dtb WP-11-T01/WP-10-T01 前置含 WP-09-T01——本文先冻结设施）；业务域码随各域卡。

---

## 5. ConfirmableFinding 与确认放行流（SA-15 承接）

### 5.1 数据来源、创建者/持有者/消费者（先冻结分工，再给字段）

SA-15 原文分工（本文逐字承接）：**ConfirmableFinding 是 diagnostics 的比较型诊断扩展，携带确认状态与确认凭据**；**命令服务**（project）在断言阶段收集待确认项集合，经**命令上下文回调**（ICommandInteraction，ui 实现）呈现确认对话；命令服务凭确认凭据继续或终止；**确认决策记录归命令摘要**，ui 不持有判定权；**工作进程内的批量计算不产生可确认诊断**（确认只发生在命令边界）。

| 角色 | 单元 | 职责 |
| --- | --- | --- |
| 产生者 | 域处理器（modeling 等，经④端口读策略）在 project 命令 prepare 阶段产出 | 判定超限（如行程上限，policy IJointLimitEvaluator 供比较型结果）→ 调 diagnostics 工厂创建 finding |
| 创建/绑定/生命周期设施 | **diagnostics（本文）** | FindingId 分配、绑定四元组计算、状态机（§5.4）、失效条件复核、确认记录形状 |
| 放行编排与提交 | project（§5.3/§6.7 project.md） | 收集待确认集→回调→凭据复核→放行/终止→确认留痕入 CommandRecord.confirmations[] |
| 交互呈现 | ui（ICommandInteraction 实现） | 确认对话（实际值/阈值/单位，UX-03 三要素）、principal 采集、Marshal UI 线程 |
| 禁止 | worker | 工作进程**不产生**可确认诊断（SA-15 明文；批量计算中的策略拒绝走诊断回传，不走确认） |

**ConfirmableFinding 数据来自哪里**：`record`（core 契约）来自域处理器的判定结果（比较型：实际值/期望值/单位——policy 供 JointLimitFinding 类结果）；绑定身份来自命令上下文（project 传入 expectedRevision 解析的 baseRevisionId、命令载荷摘要）与④端口的已解析策略内容身份（CON-06）。

### 5.2 FindingRecord 字段表（`Confirmable.hpp`；服务端记录，构造后仅状态字段按状态机转移）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `findingId` | `FindingId`（128 位随机；规范文本 `fnd-<32hex>`，句法/解析同 core Id128 约定——**diagnostics 层类型，待 core 收编登记 P-DIAG-4**） | 是 | 一次待确认事项的稳定身份（日志/目录关联；不持久化入修订——修订内留痕用 findingDigest） |
| `finding` | `core::ConfirmableFinding` | 是 | 数据契约＝core 基类型（record 必为比较型，core C-1；初始 state=Pending） |
| `sourceCommandType` | `std::string` | 是 | 来源命令类型 token（project 处理器注册 token） |
| `commandPayloadDigest` | `core::ContentIdentity` | 是 | 命令载荷摘要（SHA-256 over payloadCanonical，经 core ContentDigester；project 计算 or 本文按传入字节计算——单一计算点＝本文工厂） |
| `project / branch / baseRevisionId` | core 三身份 | 是 | 绑定的输入版本（确认所针对的修订——project §6.7 四元组之 baseRevisionId） |
| `snapshotId / inputSliceId` | `std::optional<core::ContentIdentity>` | 否 | 输入快照/切片身份（命令路径通常无快照——快照属评估路径；存在时参与绑定） |
| `policyContentId` | `core::ContentIdentity` | 条件 | 产生该 finding 的已解析策略内容身份（策略来源类 finding 必填——MDL-06④ 类；CON-06） |
| `subjectScope` | `std::vector<core::ObjectId>` | 是 | 作用对象集（record.subject 及关联对象；**确认只覆盖登记的作用对象**——不自动放行其他对象，见 §5.3） |
| `evidenceRefs` | `std::vector<DiagEntryId>` | 否 | 诊断证据（目录内关联条目：支撑判定的比较型来源） |
| `confirmTextKey / optionKeys` | `std::string / std::vector<std::string>` | 是 | 用户确认文案键与确认选项键（值归文案资源/ui；键随码表登记） |
| `createdAtUtc / expiresAtUtc` | time_point / optional | 是/否 | 创建时间；过期时间——**默认 nullopt（无限期）**：上游无超时自动语义（project §5.3.4"无超时自动确认"），失效由失效条件（§5.3）与命令终结驱动；字段保留供未来需求变更（P-DIAG-6） |
| `state` | `FindingState`（§5.4） | 是 | 服务端生命周期状态（初始 Pending） |
| `callbackToken` | `std::uint64_t` | 是 | 回调令牌：命令服务调用 ICommandInteraction 时携带，回调返回时回验——防止跨命令串用回调（同一令牌仅一次提交有效） |
| `rejectionReason` | `std::optional<std::string>`（token） | 条件 | 拒绝原因（用户拒绝/输入变化失效/策略变化失效/权限失效/回调失效/会话关闭；token 词表 §5.4） |
| `confirmation` | `std::optional<core::ConfirmationCredential>` | 条件 | 确认记录（principal＋confirmedAtUtc；core 契约——进入命令摘要的全部内容即此凭据＋绑定四元组，project §6.7） |
| `binding` | `FindingBinding` | 是 | 绑定四元组 `{findingDigest, policyContentId, commandDigest, baseRevisionId}`（§5.3；findingDigest＝SHA-256 over finding.record canonical 编码，本文计算） |

### 5.3 确认绑定与失效条件（确认不是自动成功；确认后必须重新校验）

**绑定四元组**（承接 project.md §6.7，本文为计算与复核设施）：`{findingDigest（finding 内容摘要）, policyContentId（产生 finding 的已解析策略内容身份）, commandDigest（本次命令载荷摘要）, baseRevisionId（确认所针对的输入版本）}`。创建时计算并随 FindingRecord 冻结；**确认提交时（ui 回调返回后、放行前）与编译前**各复核一次——**四者任一与当前实际值不符 ⇒ 确认凭据失效**，命令按未确认处置（`confirmations-unresolved`/重新确认）。

**失效条件**（任一触发 ⇒ state→Invalidated，登记 rejectionReason）：

| 条件 | 检测点 | 语义 |
| --- | --- | --- |
| 输入修订变化 | baseRevisionId ≠ 当前分支 tip（命令槽串行保证检测无竞争——project §6.1；实际工程中确认等待占命令槽，槽内 tip 不可能变化，本条件防御**跨命令重用旧确认**：新命令以旧凭据提交时复核） | 旧确认不跨修订有效 |
| 策略版本变化 | policyContentId ≠ 当前已解析策略内容身份（④端口复核） | 策略变更→阈值可能变化→须重新判定（CON-06） |
| 权限变化 | 存储上下文失权/只读（project §9.6 三道防线信号经 sink 通知） | 无写权限则无放行 |
| 回调失效 | interaction.isAlive()==false 或回调抛出（project §5.3.3） | Aborted(interaction-lost)；finding 标 Invalidated(interaction-lost) |
| 会话关闭/命令中止 | requestClose/用户取消（project §5.3.4：确认等待可被会话关闭取消） | Invalidated(canceled)；**无永久等待** |
| 用户拒绝 | 回调返回 rejected | Invalidated(user-rejected)——**拒绝确认不产生修订**（阻止应用，MDL-06④；project Rejected(confirmations-rejected)） |
| 过期（仅当 expiresAtUtc 显式设置） | 时间到达 | Expired；当前默认不启用（P-DIAG-6） |

**确认后的义务**（放行 ≠ 成功）：确认仅放行**本 finding 登记的 subjectScope** 内、绑定四元组一致条件下的该次命令继续执行——随后的**硬断言与双编译照常执行**（MDL-06：编译失败仍不提交；SA-15：凭据"继续或终止"）；确认**不是**自动放行所有后续错误（后续其他 finding 各自独立确认；执行失败诊断照常产生）。**确认记录进入命令摘要**：Committed 时 `{binding 四元组, credential}` 写入 CommandRecord.confirmations[]（project 持久化；本文提供形状与复核）。

**等待确认期间的资源占用**（承接 project §5.3.4，本文侧义务）：finding 等待期间**不占用任何 diagnostics 持久资源**（无文件、无句柄；目录中为一条会话态记录）；命令执行槽占用与可取消性归 project（本文仅响应失效通知）。

### 5.4 ConfirmableFinding 服务状态转换图

core 数据契约三态（Pending/Confirmed/Rejected）不变；**服务端**生命周期五态为实现承载（Invalidated/Expired 为设施状态，不进 core 枚举——P-DIAG-6 登记）：

```
                        ┌────────────────────────── 会话关闭/用户取消/回调失效 ──────────────────────────┐
                        │                     （rejectionReason: canceled / interaction-lost）          │
  create ──► Pending ───┼── 用户确认（凭据）──► Confirmed（terminal：进命令摘要；编译前复核绑定）          │
             │          │
             │          ├── 用户拒绝 ──────► Invalidated(user-rejected) ─┐   【core 投影：Rejected】
             │          ├── 输入修订变化 ──► Invalidated(revision-changed)│   （Confirmed/Rejected 落回
             │          ├── 策略版本变化 ──► Invalidated(policy-changed)  │    core::ConfirmableFinding
             │          ├── 权限变化 ──────► Invalidated(write-lost)      │    三态；Invalidated/Expired
             │          └── expiresAtUtc 到期（默认不启用）──► Expired    ┘    仅存在于服务端记录）
             └── 命令以旧凭据重提交 ──► 复核绑定四元组不符 ──► 该凭据作废，finding 回到 Pending 可重新确认
                                             （不静默沿用旧确认——"不得在输入、策略或权限变化后继续使用旧确认"）
  终态清理：命令终结（Committed/Rejected/Aborted/Failed）后，其关联 finding 记录在目录清理周期回收（§6.2）；
           Confirmed 的持久痕迹＝命令摘要（project）；服务端记录本身不持久化。
```

转移约束：Confirmed/Invalidated/Expired 为终态（不可逆转；重新确认＝新 finding）；每次状态转移写入开发级日志（findingId/转移/原因 token——可追溯）；转移不产生用户诊断（除非用户拒绝路径需要呈现"未确认则阻止"——由 project 结果呈现）。

### 5.5 确认时序图（命令—诊断—确认—编译—提交）

与 project.md §6.10 对齐，标注 diagnostics 的参与点（⑥⑦⑧⑨）：

```
 调用方(ui/域)   ProjectCommandService        域处理器              diagnostics(本文)             ICommandInteraction(ui)     runtime
      │ submit   │   prepare                    │                       │                            │                    │
      │─────────►│ S1/S2 形式校验/基线解析        │                       │                            │                    │
      │          │──────prepare(ctx)───────────►│ ①行程超限(policy④端口) │                            │                    │
      │          │                              │──createFinding──────►│ ⑥FindingId分配/绑定四元组计算 │                    │
      │          │                              │◄─FindingRecord────────│   （code=MDL-06-TRAVEL-LIMIT │                    │
      │          │                              │   （CommandPlan       │    confirmable，比较型）      │                    │
      │          │◄─Planned{findings,…}─────────┘    confirmableFindings）│                            │                    │
      │          │ S4 确认放行（findings 非空）：   │                       │                            │                    │
      │          │──requestConfirmations(findings, callbackToken)──────────────────────────────────►│                    │
      │          │                              │                       │        （ui Marshal 至 UI 线程； │                    │
      │          │                              │                       │         确认对话：实际值/阈值/单位）│                    │
      │          │◄─credentials / 拒绝 / 失效 ───│                       │                            │                    │
      │          │──submitConfirmations(findings, credentials, binding复核)─►│ ⑦复核四元组+写Confirmation │                    │
      │          │◄─确认有效 / 失效（→Rejected/重新确认）──────────────────│                            │                    │
      │          │ S5 双编译：│──compileWorkCellAndDwc(planned closure)──────────────────────────────────────────────────────►│
      │          │           │◄─ok / fail+diagnostics（⑧编译失败诊断经工厂入目录；确认不豁免编译——MDL-06）──────────────────┤│
      │          │ S6 七步事务（确认留痕 confirmations[] 入 command.json——project 持久化；⑨Committed 后 finding 清理标记） │
      │          │ S7 事件发布（RevisionCommitted→DependencyInvalidated）      │                            │                    │
      │◄─CommandResult─│                       │                       │                            │                    │
```

要点：⑦绑定复核在**确认提交时**与**S5 编译前**各一次（project §6.7"编译前复核绑定"）；非交互提交（interaction==nullptr 且有待确认集）→ `Rejected(confirmations-unresolved)`（"未确认不能提交命令"，project §6.7——DT-CFM-7 验证）。

### 5.6 UI 关闭和回调失效流程图

```
 确认等待中（命令槽持有；无事务资源）
   │
   ├─ 用户关闭主窗口 / 关闭项目（PM-03 对话框） ──► ui 事件循环开始拆除
   │        │
   │        ▼
   │   ICommandInteraction.isAlive() → false（ui 侧生命周期终止）
   │        │
   │        ▼
   │   命令服务侧（project）：回调调用前置检查/调用中检测 → Aborted(interaction-lost)，不产生修订
   │        │
   │        ▼
   │   diagnostics（本文）：finding → Invalidated(interaction-lost)
   │        │   · 状态转移入开发级日志（findingId/reason）
   │        │   · 不产生用户诊断（会话已在关闭，无呈现对象）
   │        │   · 回调令牌作废（callbackToken 一次性）
   │        ▼
   │   若 ui 在请求发出后、窗口拆除前已完成用户输入：凭据仍按 ⑦ 复核四元组——
   │       有效则照常放行（用户意图已表达且输入未变），无效则 Invalidated（不猜测用户意图）
   │
   ├─ 回调抛出异常 / 超时机制（ui 侧） ──► 同 interaction-lost 路径（project §5.3.3）
   │
   └─ worker 内出现"类确认"需求（假设性反例） ──► 禁止：worker 不产生可确认诊断（SA-15）；
            worker 的策略拒绝/超限一律经通道 ErrorReport 回传为普通诊断，由用户在命令边界另行处置
            （DT-CFM-8 反例钉住：worker 进程内调用 createFinding 即为非法调用——接口在 worker 装配中不暴露）
```

**等待确认不阻塞关闭流程**：project §5.3.4——命令服务主动取消等待（Aborted）；本文 finding 记录立即失效；**不存在因诊断/确认导致的永久等待**（自审项 A-5）。

---

## 6. 诊断生命周期、原因链与聚合

### 6.1 诊断对象生命周期图

```
 发现（各单元：域处理器/编译/评估/调度/io/恢复扫描/设施自省）
   │  经 IDiagnosticFactory::create（码已注册/subject 边界/三要素校验）
   ▼
 规范化（分类/严重←码表；actionKind；paramSchema 一致性；时间/线程/worker 标注）
   │
   ▼
 关联上下文（DiagContext：修订/分支/五元组/快照切片/策略身份/命令摘要/版本标注）
   │
   ├── Dev 级 ──────────────► 仅开发日志（不入用户目录、不入历史）
   └── 用户级 ──► 本地记录（DiagCatalog 追加：entryId 分配/dedupKey/orderKey/causedBy 链接）
                     │
                     ▼
               可选聚合（§6.4：同根因/同码多对象 → AggregatedEntry；成员全保留）
                     │
                     ▼
               消费（UI 只读投影〔订阅变更〕/任务终态随 envelope/报告 exportSafeSummary/命令拒绝随 CommandResult）
                     │
                     ├── 随宿主持久化：envelope.diagnostics（evidence canonical）/CommandRecord/RecoveryReport
                     │   （归 project/evidence；historical=true 的码才允许——Dev 码不进历史）
                     ▼
               项目归档或清理（§6.2：目录保留策略/会话结束回收；持久化部分随宿主不可变——
                     当前性变化〔Current→Superseded〕不改写任何已持久化诊断〔CON-02；DT-LIFE-6〕）
```

### 6.2 生命周期规则（临时/正式、分批、失败保留、清理、归档、绑定）

| 规则 | 设计 | 依据 |
| --- | --- | --- |
| 临时诊断 vs 正式诊断 | **Dev 级＝临时**（开发日志；不入目录/历史/报告）；**用户级＝正式**（目录＋按宿主持久化资格）。二者同工厂创建、分流设施内部 | NFR-REL-05 |
| 任务运行中的分批诊断 | worker/评估器分批产生的诊断：随批次经 execution 通道 ErrorReport 回传（§7.5），主进程逐批入目录（entryId 全局有序）；**目录增量可见**（UI 实时刷新）而 envelope 只在终结时收口（分批 DTO 与最终 envelope 的区别——evidence §7.1 同口径） | NFR-PERF-03、ARCH §4 |
| 最终结果中的诊断 | ResultEnvelope.diagnostics＝终结时收口的正式诊断全量（含终结原因）；目录条目与 envelope 内记录**同源**（同一 record 值；entryId 不入 envelope——会话态身份不持久化） | TASK-02 |
| 失败/取消后的诊断保留 | Failed/Canceled/Interrupted 任务的诊断**照常保留**于目录（会话内可见）与开发日志；终结原因诊断必附（execution T7/T13）；取消任务：正常取消仅 Info 级任务记录（UX-03——正常取消不产生**错误**诊断，但任务事实可追溯） | NFR-REL-03、UX-03 |
| 诊断清理策略 | 目录容量上限（默认 10,000 条，可配；超限淘汰**已消费且非 Warning/Error 的最旧 Info 条目**；Warning/Error 与活动任务关联条目不淘汰——`DIAG-CATALOG-OVERFLOW` 开发诊断登记溢出事实）；会话结束全量回收；**清理只作用于会话态目录，永不触碰已持久化诊断** | 本文（性能护栏） |
| 诊断与历史结果的绑定 | 持久化诊断的绑定＝宿主对象身份（envelope 的 task/snapshotId；CommandRecord 的 revision）；目录条目的绑定＝DiagContext（同源值）；二者不经 entryId 关联（entryId 不持久化） | CON-02 |
| 诊断对象不能因当前性变化被改写 | 已持久化诊断**不可变**（宿主不可变——PA-2）；Superseded 是 evidence 对结果的投影，**不传播为对诊断记录的修改**；目录中条目永不改写（更新＝新条目＋supersedes） | CON-02/05、DT-LIFE-6 |
| 诊断归档失败如何报告 | 诊断随 envelope 归档（execution→project 归档端口）：归档失败＝EX-ARCHIVE-FAILED（Error，随任务终态呈现，用户可见"结果未归档"）；**目录与会话内诊断不受影响**（日志仍在、目录仍在）；失败路径即结束归档责任（project §10.1 abandon——不因非关键诊断永久等待，自审项 A-5） | project §10.1 |
| 关闭流程不能因非关键诊断永久等待 | 诊断设施**无任何关闭阻塞点**：目录回收＝内存释放；日志 flush 有界（§7.6 有界等待；超时放弃并 Dev 日志 DIAG-LOG-WRITE-FAILED）；finding 失效即回收（§5.6） | PM-03 |

### 6.3 原因链：根因与派生诊断（causedBy / relatedTo / supersedes）

| 关系 | 语义 | 规则 |
| --- | --- | --- |
| `causedBy` | 本条目是**另一条目的后果**（指向根因方向） | 单父（DAG，无环——工厂校验指向已存在条目且不成环）；典型：worker 崩溃（EX-WORKER-CRASHED，根因）→ 任务失败（EX-TASK-REJECTED 类派生 causedBy 崩溃条目）→ 结果缺失告知（派生）；**根因不被聚合吞没**：聚合条目（§6.4）保留全部成员的 causedBy 链 |
| `relatedTo` | 无因果的关联导航（同一任务/同一对象的不同侧面） | 多值、无方向；不参与聚合判定 |
| `supersedes` | 本条目**取代**旧条目（同一问题的更新表达） | 单值；旧条目保留不删（不可变）；典型：重试后同一资源缺失的重新发现（新条目 supersedes 旧条目，时间线可追溯） |

**错误转换不得丢失根因**（§8.1 总则的机制承载）：转换器产出派生条目时**必须**链接 causedBy 根因条目（或保留根因码于上下文标注 `contractVersions`/`sourceInterface`）；禁止"翻译后只剩最后一层"。

### 6.4 聚合与去重规则

**去重键** `DedupKey = {code, subject, scopeKind, scopeId}`（scopeKind∈{None, Task, Case, Object, Batch}）：

- **同一错误重复出现**（同 code＋同 subject＋同 scope）→ 目录保留首条，后续命中**计数**（`occurrences` 于 AggregatedEntry；不重复占位）；occurrences 变化产生用户级日志节流（§7.4）。
- **不同作用对象绝不合并**（code 相同、subject 不同 → 不同 DedupKey，各自成条）——"不同作用对象不能错误去重"（DT-DUP-2 反例钉住）。
- 去重**只作用于目录呈现**，绝不改写发送给 envelope/命令结果的诊断集合（证据全量——§8.1 表 2④"全量列出"口径）。

**聚合**（`AggregatedEntry`＝呈现层汇总条目，成员引用全保留）：

| 规则 | 内容 |
| --- | --- |
| 触发 | 显式调用（消费方请求聚合视图：UI 诊断表折叠/报告摘要）或目录内同根因成员数达到阈值（默认 ≥3，可配）；**不在创建时自动聚合**（保留原始事实流） |
| 聚合键 | 同 `causedBy` 根因 或 同 code＋同 scopeKind（多对象场景：如同一策略对 N 个关节各产出一条 → 聚合为一条"N 个对象受影响"的呈现条目） |
| **保留原始证据** | AggregatedEntry 持全部成员 entryId 引用＋各成员 subject/localName（**聚合摘要不得丢失关键作用对象**——展开即得逐对象明细；DT-AGG-3） |
| 计数语义 | occurrences＝成员实际次数总和（不是"≥N"截断） |
| **局部碰撞不自动升级** | 聚合**只是呈现分组**：多个构型/路径级碰撞发现聚合后仍是"策略拒绝"类诊断（Warning），**不得自动生成任务级不可行结论**（C8：任务级仅必经状态碰撞/解析界限/约束矛盾三类证明，且证明由域产生、evidence 校验——本文无此判定权；DT-AGG-4 反例钉住） |
| 稳定排序 | 目录与聚合视图的输出序＝orderKey（时间戳计数, entryId, code 字典序）；同输入同序（NFR-COR-02 精神）；多工况/多对象批量诊断的顺序确定性由创建顺序（分批复现）＋orderKey 双重保证（DT-AGG-5） |
| 多工况/多对象/多任务 | scopeKind 区分聚合域：同任务多工况（scope=Task 下的 Case 子条目）、同评估多对象（Batch）；跨任务**不聚合**（不同 TaskIdentity＝不同 scope） |

### 6.5 诊断关系图与任务/运行/诊断关联图

```
 诊断关系（目录内）                                任务/运行/诊断关联（跨对象）
 ┌────────────────────────────┐                  RunId run-abc（execution 登记，五元组）
 │ [E1] EX-WORKER-CRASHED  Error│◄─causedBy────┐   │ AttemptId 2（att-2；att-1 被 Superseded）
 │  subject:∅ ctx:{task五元组} │                │   ├─► ResultEnvelope{task,diagnostics[E7,E8…]}
 │ [E2] EX-TASK-REJECTED  Error ├───────────────┘   │     （终结收口；evidence canonical 持久化）
 │  causedBy:E1              │                    ├─► 目录条目 E1,E2,E7,E8（DiagContext.task=同一五元组）
 │ [E3] MDL-06-TRAVEL-LIMIT │                    │ └─► 开发日志行（关联 id=run-abc/att-2）
 │  Warn confirmable ◄─聚合呈现─ [A1]（成员E3,E4,E5：│
 │  subject:{joint5}          │   同 code 不同 subject →  华关联：诊断→宿主
 │ [E4] 同码 subject:{joint6} │   各自独立 DedupKey；A1 修订 r9（命令摘要含 confirmations[]）
 │ [E5] 同码 subject:{joint7} │   仅呈现折叠，展开见明细）  ├─► CommandRecord.confirmations[{binding,credential}]
 │ [E6] RT-WC-COMPILE-FAILED │                        ├─► 目录条目 E3..E5（DiagContext.revision=r9）
 │  relatedTo:E3（同命令域）   │                        └─► 结果诊断 → envelope.diagnostics → results/<run-id>/（project）
 └────────────────────────────┘
```

---

## 7. 两级日志、脱敏与安全边界

### 7.1 两级日志模型（NFR-REL-05 承接）

| 层 | 名称 | 内容 | 禁止 | 输出 |
| --- | --- | --- | --- | --- |
| Tier-U | 用户级（用户诊断） | 用户可理解的诊断事件：稳定码＋标题键＋脱敏参数＋对象定位；无调用栈、无内部哈希、无 Schema/内部插件名（UX-02） | 调用栈、内存地址、内容哈希、内部 token、未脱敏路径 | 用户级日志文件（§7.6）＋目录投影（PM-15 日志面板） |
| Tier-D | 开发级（开发诊断） | 全量技术细节：通道、线程、关联 ID、脱敏后上下文快览、异常类型与消息摘要（截断）、finding 状态转移 | 凭据/令牌/未脱敏路径（与 Tier-U 同一脱敏管线，NFR-SEC-07） | 开发级日志文件（独立文件） |

两 Tier 由**同一设施**产出（一个 LogRecord，分流到两个 sink 链）——杜绝"两套日志两种语义"；Tier-U 行可由 Tier-D 行重建（Tier-U ⊆ Tier-D 信息，脱敏后）。

### 7.2 日志字段与关联 ID（LogRecord）

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `timestampUtc` | time_point | 是 | 写入时间（注入 IClock） |
| `tier` | LogTier（U/D） | 是 | 层级 |
| `level` | LogLevel（Error/Warning/Info/Debug/Trace） | 是 | 日志级别（≠诊断 severity；映射：诊断 severity→日志 level 一一对应＋Dev→Debug） |
| `channel` | LogChannel（token，≤48） | 是 | 通道＝来源子系统（`project/store`、`execution/sched`、`diag/catalog`…；词表随单元登记） |
| `correlationIds` | 结构化（entryId?/findingId?/taskId?/runId?/attemptId?/revisionId?） | 条件 | **关联 ID**（§6.5 关联图的日志侧锚点；至少一个非空才可跨行关联——无关联 ID 的行为纯开发输出） |
| `code` | DiagCode? | 条件 | 关联诊断条目时必填（**日志不以文本代替稳定码**——凡对应诊断的事件必带 code） |
| `message` | string（≤4 KiB，超出截断并标注） | 是 | 脱敏后文本 |
| `threadTag / workerId` | string? / uint64? | 否 | 同 DiagnosticEntry |

### 7.3 两级日志与脱敏流程图

```
 产生方（任意线程/worker 通道回传）
   │ ILogger::log(LogRecord draft)
   ▼
 ┌─────────────────── LoggingPipeline（单一管线，串行化于日志线程）───────────────────┐
 │ ①格式化（message 定长截断；关联 ID 规范化）                                         │
 │ ②脱敏（IRedactionService：每条消息强制经过；Tier-U 额外过滤栈/哈希/内部 token 模式）    │
 │      ├─ 命中已知敏感模式 → 替换 [REDACTED:<kind>:n]（保留计数，不保留原文）            │
 │      ├─ 白名单外字段（结构化 kv 中的未知键） → 整字段替换 [REDACTED:unknown-key]      │
 │      └─ 脱敏器自身失败 → 降级：整条 message 替换为 "[REDACTED:redaction-failed]" ＋    │
 │         DIAG-REDACTION-FAILED 开发诊断（绝不放行原文——宁可丢信息不泄敏感）            │
 │ ③级别过滤（Tier-U sink 只收 Error/Warning/Info；Tier-D 按 level 配置）               │
 │ ④节流与采样（§7.4）                                                                 │
 │ ⑤写入：异步队列→日志线程→文件 sink（Tier-U/Tier-D 两文件）                           │
 │      每 N=256 行或 T=2 s 或 level≥Error 即 flush（崩溃前保留窗口≤2 s——§7.6）        │
 └──────────────────────────────────────────────────────────────────────────────┘
   ├─► 用户级日志文件（user-diagnostics.log，轮转 §7.6）
   └─► 开发级日志文件（dev-diagnostics.log，轮转）
 观测：DT-LOG-1/2、DT-SEC-1~4
```

### 7.4 采样、节流与日志顺序

| 机制 | 规则 |
| --- | --- |
| 采样 | 仅 Debug/Trace 级可采样（首条＋每 N 条＋末条；N 随通道配置，默认 100）；Error/Warning/Info **不采样**（用户语义事件不丢） |
| 节流 | 同 (code+channel) 高频重复（目录去重命中、循环重试）：首条全量＋每 10 s 汇总一条（含 occurrences）；节流只影响日志行数，不影响目录计数与 envelope 全量 |
| 日志顺序 | 单线程内严格 FIFO；跨线程按日志线程入队序（时间戳＋单调序号双键稳定排序）；worker 行经通道回传后按批次序合并（§7.5）——**合并后的全局序是稳定序而非物理时序**，每行保留原始时间戳可重排 |

### 7.5 多线程/多进程合并与 worker 日志回传

- **worker 侧**：worker 进程装配同一 LoggingPipeline（零 Qt），两级目的地为：①本地临时文件（`%TEMP%\ird-worker-<pid>-…/dev.log`，execution §6.6 临时目录——崩溃现场）；②**批量回传**：按 flush 窗口组装 `WorkerLogBatch{workerId, task 五元组, 序号, LogRecord[]}`，经 execution 通道（ErrorReport 通道复用/专帧——execution §3.4 帧序号防断裂）回主进程，由主进程日志器**按序号重放合并**入主进程两 Tier 文件（correlationIds 保留 workerId）。
- **通道断裂**：批次丢失→主进程补一条 Dev 诊断（通道序号断裂，EX-CHANNEL-PROTOCOL-ERROR 联动）；**日志丢失不影响正式诊断可用性**（envelope.diagnostics 独立于日志——DT-LOG-4）。
- **崩溃 worker 的本地日志**：强制终止路径（execution T13）回传终止前，本地 dev.log 随临时目录保留（execution §6.6 best-effort 清理之外的情形）供事后诊断；主进程崩溃诊断文件（§7.6）引用其路径（脱敏后）。

### 7.6 崩溃前日志保留与崩溃诊断文件（PM-17/NFR-SEC-07/AT-11）

- **崩溃前保留**：管线 flush 窗口（N=256 行/T=2 s/Error 即时）保证崩溃时丢失窗口 ≤2 s 的 Dev 行、0 行 Error 级（Error 即时 flush）。
- **崩溃诊断文件**（CrashReportWriter）：由 L5 应用壳在异常捕获点调用（PM-17；WP-24-T02 协作——dtb WP-09-T05）；内容＝脱敏后的：进程/版本信息、异常类型与消息摘要（截断 512 B）、最近 512 行开发日志快照（重放内存环形缓冲）、活动任务清单（五元组）、活动 finding 清单（findingId/状态，不含载荷）、打开的项目身份（projectId，不含路径原文——路径按配置脱敏）。写出位置＝用户目录（ARCH §5.4 用户目录\崩溃诊断文件；**不入 .rwdesign**——D-10）；写出失败→stderr 摘要＋放弃（不递归诊断）。**AT-11 观测点：诊断文件写出且脱敏（不含凭据类；路径按配置）**。

### 7.7 脱敏规则（IRedactionService；NFR-SEC-07 承接）

| 规则 | 内容 |
| --- | --- |
| 路径 | 本机路径按配置脱敏：配置项 `pathPolicy ∈ {Keep(默认开发机), Hash, Root-only, Strip}`；`Root-only`＝保留盘符＋一级目录（如 `D:\…\file.stl`）；`Strip`＝仅文件名。默认值＝`Root-only`（企业内网用户可读性与安全的折中，P-DIAG-7 关联登记配置归属 ui） |
| 用户名 | 环境变量中的用户名/主目录（`%USERPROFILE%` 等）替换 `[USER]`；ConfirmationCredential.principal **不写入日志**（仅入命令摘要——持久化对象的受控字段，非日志） |
| 环境变量 | 整体不记录（键名白名单 `IRD_*` 产品自用变量除外） |
| 资源内容 | **不记录完整外部资源内容**；最多记录 {资源身份摘要, 大小, 前 32 B 十六进制预览（经敏感模式过滤）} |
| 命令载荷 | 日志只携带 commandDigest（身份摘要），不携带 payloadCanonical 原文（载荷随修订持久化于 project，非日志职责） |
| 凭据/令牌 | **一律不记录**（无配置例外）：已知模式（`password/token/secret/key/apikey` 键名与常见 token 形态）命中即替换；结构化字段键名白名单外的整字段替换 |
| 脱敏失败降级 | §7.3 ②：整字段/整条替换＋DIAG-REDACTION-FAILED；**绝不放行原文**（宁可信息缺失不泄露——保守方向） |
| 幂等/纯函数 | 同输入同输出（确定性，可测试——DT-SEC-1） |
| 与 reporting | `exportSafeSummary`（§9.7）输出前强制再过一遍脱敏（双保险——报告外发，DT-SEC-4） |

**不记录未经允许的敏感路径、用户数据或完整外部资源内容**（任务约束§三）——上表即全部实现承载。

### 7.8 日志与正式诊断的区别（日志不是项目历史的第二权威来源）

| 维度 | 日志（两 Tier 文件） | 正式诊断（DiagnosticEntry.record 随宿主） |
| --- | --- | --- |
| 权威性 | **非权威**：运维/排障快照；不可用于重建任何项目事实 | 权威：envelope/CommandRecord/RecoveryReport 的组成部分（随宿主不可变持久化） |
| 生命周期 | 轮转覆盖（§7.6；默认 5×2 MiB/文件） | 随宿主对象永久（修订/结果历史） |
| 完整性承诺 | 尽力（采样/节流/崩溃窗口） | 全量（表 2④"缺失项全量列出"在 envelope 侧） |
| 关系 | 日志行可**引用**诊断（code/entryId 关联），不可替代之 | — |

---

## 8. 跨单元错误转换与协作

### 8.1 转换总原则（ErrorCodeTranslator，§9.2 的组成）

1. **不丢根因**：转换产出必须链接根因（causedBy 或根因码标注；§6.3）；异常链/错误码链全程保留至开发级日志。
2. **禁止字符串匹配恢复诊断类型**：转换表按**错误码枚举/异常类型**登记映射（编译期类型安全）；`catch(...)` 只能映射到 `DIAG-REGISTRY-UNKNOWN-CODE` 类"未知错误"条目并**保留原始来源**（来源单元/sourceInterface/异常类型名）与**安全摘要**（消息截断＋脱敏，§7.7）。
3. **同一错误只有一个权威分类**：每条转换规则声明目标码（唯一）；上层转换只在下层条目上**补充上下文**（DiagContext 扩展/scope 变化），**不改变下层事实**（code/category/severity 不被上层覆盖——下层条目原样保留，上层产出新条目并 causedBy 之）。
4. **转译后不吞异常语义**：错误路径继续按其所有者的协议走（如 runtime 编译转译后仍 Failed 终止——runtime §8 转译规则同源；本文只产出诊断，不改变控制流）。

各小节的映射表即注册进 ErrorCodeTranslator 的数据（装配期登记；运行期不可变）。

### 8.2 project 异常/错误 → 诊断（StoreError → DiagnosticRecord）

| StoreError code | 目标码 | 分类/严重 | subject/比较型 | 附加上下文 |
| --- | --- | --- | --- | --- |
| lock-held-by-other | PRJ-LOCK-HELD | 权限/Warning | subject=∅（项目级）；非比较型 | paramSchema{pid,host}；只读打开提示（PM-07） |
| media-read-only / access-denied | PRJ-LOCK-HELD 族（paramSchema 区分 kind） | 权限/Warning | 同上 | — |
| store-corrupt / branch-metadata-regression | PRJ-STORE-CORRUPT | 内部/Error | subject=∅；非比较型 | 定位文件名（脱敏后）+摘要 |
| format-legacy / schema-future | PRJ-FORMAT-LEGACY / PRJ-SCHEMA-FUTURE | 版本/Error | 非比较型 | 版本参数（schema-future 为比较型：实际/期望版本串——version token 无单位，comparison 单位=dimensionless） |
| stale-revision-rejected | PRJ-STALE-REVISION-REJECTED | 输入/Error | subject=冲突对象（若可定位）；非比较型 | 当前 tip/差异定位数据（project §6.2） |
| confirmations-unresolved / confirmations-rejected / interaction-lost / command-aborted | （分别映射；confirmations-unresolved→EX-TASK-REJECTED 族外单独 DIAG-FINDING-\* 关联） | 可确认辅助/Info 或执行 | — | findingId 关联 |
| disk-full / write-rejected / context-closed | PRJ-WRITE-AUTHORITY-LOST（失权）或归档路径 EX-ARCHIVE-FAILED | 权限/执行、Error | — | — |
| draft-corrupt | PRJ-RECOVERY-ORPHAN-DRAFT | 权限/Info（恢复场景） | — | 模块/分支 |
| archive-conflict / archive-target-missing | PRJ-ARCHIVE-CONFLICT | 执行/Error | — | runId |

保留：StoreError.detail（前缀 `project/<域>:`）进开发级日志原文（脱敏后）；用户级仅码＋参数。

### 8.3 runtime 编译错误 → 诊断（RuntimeErrorCode → 码）

runtime §10.11 建议码全量收编（§4.6 RT 行）。转换要点：`CompileOutcome.diagnostics` 由 runtime 直接产出 core::DiagnosticRecord（其已有构造），本文设施仅做**入目录/日志/关联**（execution 派发路径由 Preparing 段转发）；`translateRobWorkError`（rw::common::Exception→RT-ROBWORK-ERROR）保持 runtime 侧实现——本文转换表登记 `std::exception 体系→RT-ROBWORK-ERROR` 的兜底规则（§8.1 规则 2 的合法应用：类型匹配而非文本匹配）。**用户诊断不含 RobWork 调用栈/内部哈希**（NFR-REL-05；runtime §10.11 同源）。

### 8.4 policy 评估错误 → 诊断

policy §9.6 码表全量收编（POLICY 行）。`PolicyParseResult.diagnostics`/评估诊断由 policy 经其 IPolicyDiagnostics 构造（core 契约）；本文承接：分类映射（§4.6 表）；行程上限超限（policy §7.4 TravelLimitExceeded 比较型）→ **不直接生成 finding**——由 project 域处理器调本文 §5 工厂创建（SA-15 分工：policy 不执行放行）。

### 8.5 execution worker 崩溃 → 诊断

| 事件（execution 侧事实） | 目标码 | 说明 |
| --- | --- | --- |
| worker 非零退出/无完成事件 | EX-WORKER-CRASHED（Error，subject=∅，ctx.task=五元组） | 根因条目；任务 Failed 派生条目 causedBy 此条（DT-CHAIN-1） |
| 心跳失联 3×间隔 | EX-WORKER-HUNG（Error）→ 强杀后 EX-FORCE-TERMINATED（Error，显式区分标记——execution T13"不伪装普通失败"） | 超时类 |
| 启动失败 | EX-WORKER-LAUNCH-FAILED | — |
| 通道帧错误/登记表失配/陈旧 attempt | EX-CHANNEL-PROTOCOL-ERROR / EX-REGISTRY-MISMATCH / EX-STALE-ATTEMPT（Dev） | 迟到结果丢弃的开发诊断（AT-10 观测：丢弃＋开发诊断、绝不写新项目） |
| 恢复扫描（主进程崩溃后） | EX-TASK-INTERRUPTED（Info） | PM-08/PM-15"已中断"数据源 |

崩溃**检测与处置**归 execution（N-9）；本文仅转译与记录。

### 8.6 io 路径与预算错误 → 诊断

io 卡未产出：本文登记**接口义务**（io 卡产出时按此注册 IO-\* 码并经 sink 上报）：SafePath 违规（路径逃逸尝试→安全/Error，NFR-SEC-01）、BudgetGuard 超限（→安全/Error，比较型：实际字节数/上限/字节单位，NFR-SEC-02）、CSV 逐行错误（→输入/Error，定位行/列/原文片段——原文片段**经脱敏**后保留于开发级，用户级仅定位，AT-02 口径）。映射表条目随 io 卡冻结（§12 交接）。

### 8.7 evidence 证据缺失与当前性诊断 → 诊断

EVI-\* 码收编（§4.6 EVI 行）。要点：证据缺失条目**逐项产生、全量列出**（表 2④——本文聚合不吞缺失项，§6.4）；DataInsufficient 轴的呈现分类（§4.4）；当前性 Superseded 是 evidence 投影——**不产生新诊断条目改写历史**（重算提示类信息由 ui 消费 CurrentnessResult.reasons，非诊断；DT-LIFE-6）。

### 8.8 业务单元领域错误 → 稳定领域诊断

域在装配期注册领域码（CodeDescriptor，§4.5）；运行期域错误→领域码映射表由**域**登记进 ErrorCodeTranslator（转换设施归本文、映射数据归域）；域诊断经 sink 入目录。**不允许**：绕过注册表以异常文本作码（工厂拒绝）；域内复制两级日志/脱敏/目录设施（NFR-MNT-04）。

### 8.9 Qt/UI 错误 → 平台诊断

ui 卡未产出：登记接口义务——Qt 层异常（事件循环异常、平台插件失败等）经 L5 捕获映射到 UI-\* 平台码（ui 卡注册；分类内部/Dev 为主）；**零 Qt 红线**保证本文设施自身不产生 Qt 异常路径。L5 装配壳的启动失败（PM-17）走崩溃诊断文件路径（§7.6）。

### 8.10 与 project/execution/evidence/UI 的协作细节

| 协作点 | 设计 |
| --- | --- |
| project 命令拒绝/事务失败关联诊断 | CommandResult.diagnostics（project）由处理器经本文工厂构造；目录条目 DiagContext 带 commandType/commandDigest/revision——命令边界诊断可追溯到修订（DT-COLLAB-1） |
| execution 关联 TaskId/RunId/AttemptId/worker | execution 全路径（提交/调度/终结/恢复）产码时必须携带五元组＋WorkerId（工厂校验 execution 来源码的 task 非空——`context-missing` 拒绝）；目录按 task 聚合导航（§6.5） |
| evidence 证据缺失/数据不足/当前性 | §8.7；evidence 调用工厂时携带 snapshotId/sliceId（评估路径码必填校验） |
| UI 只读投影 | IDiagnosticSink::snapshot/订阅（§9.7）：ui 收 DiagProjection（值拷贝、含文案键/actionKind/比较型三要素数据）；**ui 不得反写目录**；呈现归 ui |
| UI 关闭后确认请求终止 | §5.6 |
| reporting 消费稳定码与安全参数 | exportSafeSummary（§9.7）：{code, titleKey, 脱敏参数, subject, severity, category}——**不含原始文本字段**（报告侧据码表与文案资源渲染，RPT-05 限定语不受脱敏影响） |
| 诊断归档与项目写权限边界 | 本文**不写 .rwdesign**（D-10）；诊断持久化经宿主（project 归档/命令摘要）；只读模式下诊断设施照常工作（目录/日志是会话态，非项目写） |
| diagnostics 不调用 Widgets/不持界面对象 | 零 Qt（D-01）＋sink 推送为纯数据（ui 侧自行 Marshal）——静态可验（DT-BUILD） |

---

## 9. 公共接口、线程与生命周期

约定（适用本章）：签名为实现建议（自洽契约片段，未编译验证）；除注明外方法线程安全；全部设施由 L5 装配期创建并注入各消费方（生命周期＝进程级或会话级，逐接口注明）；错误一律 `DiagnosticsError`。

### 9.0 错误类型（`Errors.hpp`）

```cpp
enum class DiagnosticsErrorCode {          // token 稳定（内部错误语义，不对外呈现为诊断码之外的文本）
    DuplicateCode,        // diagnostics/duplicate-code          重复注册/重复转换规则
    CodeUnknown,          // diagnostics/code-unknown            未注册码（工厂/查找）
    CodeDeprecated,       // diagnostics/code-deprecated         废弃码构造
    SubjectMissing,       // diagnostics/subject-missing         用户级码缺 subject
    ComparisonMissing,    // diagnostics/comparison-missing      比较型码缺三要素
    CategoryMismatch,     // diagnostics/category-mismatch       非法分类/严重注入路径
    ParamSchemaMismatch,  // diagnostics/param-schema-mismatch   参数模式不符
    ContextMissing,       // diagnostics/context-missing         来源码必填上下文缺失
    InvalidState,         // diagnostics/invalid-state           finding 状态机非法转移
    BindingMismatch,      // diagnostics/binding-mismatch        绑定四元组复核失败
    Usage                 // diagnostics/usage                   调用方违约（空参数等）
};
class DiagnosticsError : public std::runtime_error {
public: DiagnosticsError(DiagnosticsErrorCode, std::string detail);
        DiagnosticsErrorCode code() const noexcept;  // detail 前缀 "diagnostics/<域>:"，面向开发诊断
};
```

**不抛诊断、不吞异常**：本单元设施不向调用方抛出"作为诊断语义"的异常（诊断经返回值/sink 传递）；调用方异常穿越本单元设施时不捕获不包装（透传——错误转换是**显式调用**的行为，不是隐式 catch）。

### 9.1 IDiagnosticRegistry（稳定码注册表；实现 `StableCodeRegistry`）

```cpp
struct CodeDescriptor;                     // §4.5 字段表（值语义，构造后不可变）

class IDiagnosticRegistry {
public:
    virtual ~IDiagnosticRegistry() = default;
    // 注册（装配期；运行期调用抛 DiagnosticsError(Usage)）
    // 前置：descriptor 全字段合法（§4.5 注册期验证表）
    // 后置：码可被工厂使用；manifest 摘要更新
    // 错误：DuplicateCode / CodeUnknown 前缀冲突 / ParamSchemaMismatch 等（指明字段）
    virtual void registerCode(const CodeDescriptor&) = 0;
    // 查询（运行期；并发只读安全）
    virtual const CodeDescriptor* find(std::string_view code) const noexcept = 0;   // 未注册→nullptr
    virtual std::vector<std::string> registeredCodes(std::string_view ownerUnit) const = 0;
    // 跨进程一致性（worker 握手；与 evidence Registry manifest 同模式）
    // 后置：返回按 code 字典序排序的清单及其 SHA-256 摘要（core ContentDigester）
    virtual CodeTableManifest manifest() const = 0;
    // 废弃（装配清单修订；tombstone 永存）
    virtual void deprecate(std::string_view code, std::optional<std::string> supersededBy) = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置/错误 | 见签名注释；重复注册拒绝不覆盖（NFR-MNT-03） |
| 线程 | 注册期单线程（装配约定）；运行期 find/manifest 并发只读 |
| 生命周期/所有权 | 进程级单例（L5 创建；主进程与 worker 各一，同一注册清单）；消费方持引用 |
| 副作用 | 无 I/O；manifest 摘要计算纯函数 |
| 稳定性 | 码值/句法为持久化契约（§4.5）；接口演进走设计变更 |
| 合法调用 | L5 装配注册 PRJ-\*/RT-\*/…；worker 启动比对 manifest |
| 非法调用 | 运行期 registerCode（Usage）；以未注册字符串直接构造诊断（工厂层再拦） |

### 9.2 IDiagnosticFactory（创建/校验/关联 + ErrorCodeTranslator）

```cpp
struct DiagContext;                        // §4.2 关联身份块
struct DiagnosticEntry;                    // §4.2 信封

class IDiagnosticFactory {
public:
    virtual ~IDiagnosticFactory() = default;
    // 唯一创建入口（诊断不能经异常文本临时生成的机制承载）
    // 前置：code 已注册且未废弃；用户级码 subject 合法；比较型码 comparison 完整（C-1 强化）；
    //       来源码的必填上下文齐备（execution→task；评估路径→snapshot/slice；命令路径→command/revision）
    // 后置：返回不可变 DiagnosticEntry（分类/严重取自码表；entryId/dedupKey/orderKey 已赋；
    //       redactedContextSnapshot 已过脱敏）；同输入同输出（时间戳来自注入 IClock）
    // 错误：CodeUnknown / CodeDeprecated / SubjectMissing / ComparisonMissing /
    //       ParamSchemaMismatch / ContextMissing / Usage
    virtual DiagnosticEntry create(const core::DiagnosticRecord& record,
                                   const DiagContext& context) = 0;
    // 错误转换（§8：按登记的类型映射，非字符串匹配）
    // 前置：转换规则已在装配期登记（registerTranslation）
    // 后置：产出目标码条目并 causedBy 根因条目（root 参数为空则标注原始来源，不丢根因）
    // 错误：未登记类型→产出 DIAG-REGISTRY-UNKNOWN-CODE 条目（保留来源类型名+安全摘要），不抛
    template <class E> DiagnosticEntry translate(const E& err, const DiagContext&,
                                                 const DiagnosticEntry* root = nullptr);
    // 转换规则登记（装配期；同错误类型重复登记→DuplicateCode）
    virtual void registerTranslation(std::type_index errType, std::string_view targetCode) = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置/错误 | 见注释；translate 的兜底条目是合法产物（"未知错误"保留原始来源与安全摘要——§8.1 规则 2） |
| 线程 | 并发安全（内部无共享可变状态；entryId 单调序用原子） |
| 生命周期/所有权 | 进程级；产出值语义条目（调用方持有） |
| 副作用 | 无（不入目录——入目录是 sink 的显式行为，创建与记录分离便于测试替身） |
| 稳定性 | 参数模式（paramSchema）演进＝码 registryVersion＋1 |
| 合法调用 | 各单元产码路径；ScriptedDiagSource（测试） |
| 非法调用 | 运行期 registerTranslation；对同条目二次修改（无 setter，类型层面杜绝） |

调用示例：

```cpp
auto& factory = ...;                                   // L5 注入
auto entry = factory.create(
    core::DiagnosticRecord::make(/*code=*/"PRJ-LOCK-HELD", /*subject=*/{},
        /*localName=*/{}, /*runtimeName=*/{},
        /*context=*/"打开项目时写权限被占用",
        /*cause=*/"另一进程持有项目写锁",
        /*recommendedAction=*/"以只读方式打开或联系持有进程"),
    DiagContext{ .project = pid, .sourceUnit = "project", .sourceInterface = "store.open",
                 .commandType = {}, .commandDigest = {},
                 .params = {{"pid", std::to_string(holderPid)}, {"host", holderHost}} });
catalog.append(std::move(entry));                      // §9.7 显式记录
```

### 9.3 IConfirmableFindingService（SA-15 设施）

```cpp
struct FindingId;  struct FindingBinding;  struct FindingRecord;   // §5.2

class IConfirmableFindingService {
public:
    virtual ~IConfirmableFindingService() = default;
    // 创建（project 域处理器在 prepare 阶段调用；worker 装配中不暴露本接口——SA-15）
    // 前置：finding.record 为比较型（core C-1）；code 的 confirmable=true；
    //       policy 来源类须 policyContentId；commandDigest/baseRevisionId 必填
    // 后置：FindingRecord（Pending；findingId 分配；binding 四元组冻结；callbackToken 分配）
    // 错误：ComparisonMissing / ContextMissing / Usage
    virtual FindingRecord create(const core::ConfirmableFinding& finding,
                                 std::string_view commandType, core::ContentIdentity commandDigest,
                                 core::ProjectId, core::BranchId, core::RevisionId baseRevisionId,
                                 std::optional<core::ContentIdentity> policyContentId,
                                 std::vector<core::ObjectId> subjectScope) = 0;
    // 提交确认（project 收到 ui 凭据后调用）
    // 前置：finding 处于 Pending；token 匹配
    // 后置：复核 binding 四元组（当前策略/命令/修订）→ 一致：Confirmed＋confirmation 写入；
    //       不一致：Invalidated(binding-mismatch)，返回失败——不静默沿用旧确认
    // 错误：InvalidState / BindingMismatch（返回值与异常双轨：结果枚举优先）
    virtual ConfirmOutcome submitConfirmation(FindingId, std::uint64_t callbackToken,
                                              core::ConfirmationCredential) = 0;
    // 提交拒绝（ui 返回 rejected）
    virtual void submitRejection(FindingId, std::uint64_t callbackToken,
                                 std::string_view reasonToken) = 0;
    // 使失效（project 关闭/中止/失权信号；§5.3 失效条件的服务端入口）
    virtual void invalidate(FindingId, std::string_view reasonToken) = 0;
    // 查询（ui 确认对话数据源/命令服务复核）
    virtual std::optional<FindingRecord> tryFind(FindingId) const noexcept = 0;
    virtual std::vector<FindingRecord> pendingFor(core::RevisionId) const = 0;
};
// ConfirmOutcome = Confirmed | BindingMismatch | InvalidState | UnknownFinding
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置/错误 | 见注释；确认后**编译前复核**由 project 调 tryFind+自行比对实现（§5.3——本文提供 binding 数据，复核时机归 project 编排） |
| 线程 | 并发安全；状态转移内部互斥（短临界区，无 I/O） |
| 生命周期/所有权 | 进程级服务＋会话级记录（命令终结后回收，§5.4）；FindingRecord 值返回 |
| 副作用 | 状态转移写开发级日志（§5.4）；不产生用户诊断；不写文件 |
| 稳定性 | 状态机（§5.4）与绑定四元组为契约；core 数据契约不因本文演进改变 |
| 合法调用 | project 处理器 create；命令服务 submit/invalidate；ui 经 project 间接读（投影） |
| 非法调用 | worker 内 create（装配不暴露）；Confirmed 后 submitConfirmation（InvalidState）；跨命令重用 callbackToken（token 一次性校验） |

### 9.4 IDiagnosticAggregator

```cpp
struct AggregatedEntry;                   // §6.4（成员引用全保留）

class IDiagnosticAggregator {
public:
    virtual ~IDiagnosticAggregator() = default;
    // 聚合视图（纯函数式查询；不改目录）
    // 前置：entries 为目录子集（按消费方过滤：任务/修订/全部）
    // 后置：AggregatedEntry 列表（稳定排序；成员 subject 明细保留；局部碰撞不升级——仅呈现分组）
    virtual std::vector<AggregatedEntry> aggregate(const std::vector<DiagnosticEntry>& entries,
                                                   AggregationScope scope) const = 0;
    // 去重计数（目录侧增量维护；此处为纯查询）
    virtual std::size_t occurrenceCount(const std::vector<DiagnosticEntry>&, const DedupKey&) const = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 线程/副作用 | 纯函数、并发安全、无副作用、确定性（同输入同输出——DT-AGG-5） |
| 生命周期 | 无状态；进程级共享 |
| 合法调用 | UI 折叠视图、报告摘要、目录容量统计 |
| 非法调用 | 期望聚合改写原条目（接口无写路径）；期望聚合产出工程判定（无此能力——C8 边界） |

### 9.5 IRedactionService

```cpp
enum class PathPolicy { Keep, Hash, RootOnly, Strip };      // §7.7
struct RedactionPolicy { PathPolicy pathPolicy = PathPolicy::RootOnly;
                         std::size_t maxPreviewBytes = 32; };

class IRedactionService {
public:
    virtual ~IRedactionService() = default;
    // 纯函数：同输入同输出；绝不抛出（失败降级为 [REDACTED:...]——§7.3②）
    virtual std::string redact(std::string_view raw, LogTier tier) const noexcept = 0;
    virtual std::string redactPath(std::string_view rawPath) const noexcept = 0;
    // 安全摘要导出（reporting/崩溃文件消费；双保险入口）
    virtual std::string safeSummary(std::string_view raw, std::size_t maxBytes) const noexcept = 0;
    // 配置（L5/ui 会话变更；变更后新调用生效，已写行不回溯）
    virtual void setPolicy(const RedactionPolicy&) = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 前置/后置 | 无前置；输出必为脱敏后文本（不变式：输出不含凭据模式/未授权原文——DT-SEC-1~3 断言） |
| 线程 | redact\* 并发安全（策略快照读）；setPolicy 写时拷贝切换 |
| 生命周期 | 进程级；策略会话可变（用户级设置经 ui/L5，PM-14 归属） |
| 稳定性 | 凭据模式清单为安全契约（只增不减）；路径策略枚举冻结 |
| 合法调用 | 日志管线（强制）、工厂（快照字段）、exportSafeSummary、崩溃文件 |
| 非法调用 | 以 redact 后文本反推原文（单向；无逆接口） |

### 9.6 ILogger（两级日志）

```cpp
enum class LogTier { User, Dev };
enum class LogLevel { Error, Warning, Info, Debug, Trace };
struct LogRecord;                          // §7.2

class ILogger {
public:
    virtual ~ILogger() = default;
    // 记录（任意线程；异步管线——§7.3；返回即入队确认，不等待落盘）
    // 前置：record.message 为原文（脱敏在管线内强制执行——调用方无需也无法预脱敏）
    // 后置：按 tier/level 分流；Error 级即时 flush 请求
    // 错误：队列满→丢弃最旧 Debug/Trace＋Dev 计数诊断（不阻塞调用方、不抛）
    virtual void log(LogRecord record) = 0;
    virtual void logDev(std::string_view channel, LogLevel, std::string message,
                        CorrelationIds ids = {}) = 0;      // Dev 快捷入口（对齐 project reportDev 语义）
    // 排空（关闭/崩溃前）：有界等待（默认 3 s；超时放弃＋DIAG-LOG-WRITE-FAILED——不永久等待）
    virtual bool flush(std::chrono::milliseconds deadline) = 0;
    // 配置（L5：文件路径/轮转/采样参数）
    virtual void configure(LogSinkConfig) = 0;
};
```

| 维度 | 契约 |
| --- | --- |
| 线程 | 任意线程调用；内部日志线程串行化（§7.4 顺序规则） |
| 生命周期 | 进程级；关闭时 flush 有界（§6.2"不永久等待"） |
| 副作用 | 文件写入（用户目录——D-10，不入 .rwdesign） |
| 合法调用 | 一切单元的日志路径；worker 侧经 WorkerLogBatch 回传后由主进程 logger 重放 |
| 非法调用 | 在 message 中放置凭据（管线兜底替换——防线不依赖调用方自觉）；UI 线程长阻塞等待 flush（flush 仅关闭路径） |

### 9.7 IDiagnosticSink（诊断目录＋消费投影）

```cpp
struct DiagProjectionItem {                 // ui/reporting 消费的只读投影（值拷贝）
    std::uint64_t entryId; std::string code, titleKey, detailKey;
    DiagnosticCategory category; DiagnosticSeverity severity;
    std::optional<core::ObjectId> subject; std::optional<std::string> localName, runtimeName;
    std::string actionKind;                                     // §4.4 动作族
    std::optional<core::ComparativeFields> comparison;          // 三要素（UX-03）
    DiagContext context; std::size_t occurrences = 1;            // 去重计数
    std::optional<std::uint64_t> aggregatedUnder;                // 聚合导航
    // 注意：投影不含 redactedContextSnapshot（开发级专属）、不含原始 context 文本
};

class IDiagnosticSink {
public:
    virtual ~IDiagnosticSink() = default;
    // 记录入目录（工厂产出后的显式记录动作；Dev 级条目拒绝——Dev 走日志）
    // 前置：entry.severity != Dev（若 Dev 码误入→DiagnosticsError(Usage)）
    // 后置：目录追加＋去重计数＋变更通知（订阅方）；容量策略执行（§6.2）
    virtual void append(DiagnosticEntry entry) = 0;
    // 只读投影（ui 拉取；值拷贝——ui 不持目录引用）
    virtual std::vector<DiagProjectionItem> snapshot(DiagQuery) const = 0;   // 过滤：任务/修订/类别/严重
    // 订阅变更（ui 实时刷新；回调在目录线程执行——订阅方自行 Marshal）
    virtual std::unique_ptr<ISubscription> subscribe(IDiagObserver&) = 0;
    // 安全摘要导出（reporting：稳定码＋安全参数；输出再过脱敏——双保险）
    virtual std::string exportSafeSummary(DiagQuery, std::size_t maxEntries) const = 0;
};
// 与既有 sink 形态的统一（P-PR-6/P-EX-8 消账）：project §5.0 IDiagnosticsSink{report(record),
// reportDev(channel,message)} 与 execution §3.3 IExecutionDiagnosticsSink 同形——本文提供
// DiagnosticsSinkImpl 同时实现三者语义：report＝factory.create＋catalog.append（无上下文时
// sourceUnit 取注入的宿主标识）；reportDev＝logger.logDev。L5 装配将各消费方的 sink 指针
// 绑到本实现；两消费方详设修订时对齐接口名（§12 交接）。
```

| 维度 | 契约 |
| --- | --- |
| 线程 | append 并发安全（内部互斥）；snapshot 并发只读；订阅回调线程＝目录通知线程（文档化，ui 负责 Marshal） |
| 生命周期/所有权 | 会话级目录（项目打开→关闭回收）；进程级服务壳 |
| 副作用 | 内存目录＋变更通知；无文件 I/O（日志另走 logger） |
| 稳定性 | 投影字段集为 ui/reporting 契约（新增字段＝次版本） |
| 合法调用 | ui 诊断表/日志面板/恢复横幅（PM-15）；reporting 章节数据 |
| 非法调用 | ui 反写/删除条目（无接口）；以投影文本反查原文（投影不含原文） |

### 9.8 线程模型总表

| 线程/通道 | 归属 | 职责与约束 |
| --- | --- | --- |
| 日志线程（每进程 1） | diagnostics | 管线串行化（§7.3）；flush 有界；崩溃前窗口 ≤2 s |
| 目录通知线程（复用日志线程或独立） | diagnostics | 订阅回调派发（文档化线程——ui Marshal） |
| 调用方线程（任意） | 各单元 | factory/registry/sink/logger 入口并发安全 |
| finding 服务临界区 | diagnostics | 状态转移短互斥（无 I/O，无死锁面） |
| worker 进程 | diagnostics（同库零 Qt 装配） | 本地日志＋WorkerLogBatch 回传（经 execution 通道——本文无跨进程设施） |

---

## 10. 验证方案及故障注入矩阵（设计；未实现未运行，实现状态随 §11 登记）

测试目标 `sdurws_ird_diagnostics_test`（单元）与 `sdurws_ird_diagnostics_contract_test`（跨单元：与 core 值类型往返、与 testkit checkDiagnosticRecord/checkComparativeFields、与 project/execution sink 形态、ScriptedDiagSource 基座）。消费 testkit：IRD_TEST_INFO/IRD_EXPECT_\*、DeterministicEnv（种子）、TempDir（日志文件）、ManualClock（时间注入）、FaultInterceptor（日志写失败注入——经 ILogFileOps 接缝，testkit D-10 形态）。产品目标不链 testkit（T-1）。每条含需求/AT 依据、前置、操作、预期、观测点：

| 组 | 用例 | 依据 | 前置 | 操作 | 预期 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- |
| DT-REG-1 | 稳定码注册与查询 | ERR-01、NFR-MNT-03 | 空注册表 | 注册 PRJ-\*/RT-\*/POLICY-\*/EVI-\*/EX-\*/DIAG-\* 全量（§4.6） | 全部注册成功；find 命中；manifest 摘要稳定（两次计算相等） | registeredCodes 计数＝清单；摘要逐字节相等 |
| DT-REG-2 | 重复注册拒绝 | NFR-MNT-03 | 已注册 PRJ-LOCK-HELD | 同码再注册（含元数据全同） | DuplicateCode 拒绝，不覆盖 | 抛错码；find 返回首版描述 |
| DT-REG-3 | 未知码构造拒绝 | ERR-01 | — | 以未注册码调工厂 | CodeUnknown | 错误码；无条目产生 |
| DT-REG-4 | 异常文本不作码 | 任务约束§五.3 | 注册表就绪 | 以异常 message 字符串直接作 code 构造；经未登记类型 translate | 直接构造→CodeUnknown；translate→DIAG-REGISTRY-UNKNOWN-CODE 条目（保留来源类型＋安全摘要，不抛） | 条目 code/上下文标注；根因不丢 |
| DT-REG-5 | 参数模式/版本冲突/废弃迁移 | §4.5 | 已注册码 | paramSchema 缺参构造；运行期重复注册；deprecate 后构造；tombstone 解析 | ParamSchemaMismatch/DuplicateCode/CodeDeprecated；旧持久化文本解析保留映射 | 错误码集合；supersededBy 导航 |
| DT-DIAG-1 | 字段完整性（用户级） | ERR-01、UX-03 | 注册码 | 创建各类型条目（比较型/非比较型/不适用值） | 工厂校验全过；不适用为显式标记非数值 | testkit checkDiagnosticRecord 通过 |
| DT-DIAG-2 | subject 边界与分类注入阻断 | core §4.8 交接 | — | 用户级码缺 subject；尝试构造后改 category（无路径） | SubjectMissing 拒绝；category 仅码表来源（类型无 setter） | 错误码；条目只读 |
| DT-DUP-1 | 同根因稳定聚合 | §6.4 | 目录含同 (code,subject,scope) ×N | append×N＋aggregate | 首条目＋occurrences=N；聚合稳定（两次相等） | occurrenceCount；聚合序稳定 |
| DT-DUP-2 | 不同作用对象不去重 | 任务约束§五.6 | 同码两 subject | append 两条 | 两条独立条目（DedupKey 不同） | entryId 数＝2；subject 各自保留 |
| DT-CAT-1 | 输入非法 vs 执行失败区分 | CON-02、TASK-02 | — | 构造 RT-INPUT-INVALID 与 EX-WORKER-CRASHED | 分类/严重/动作族不同；三轴字段独立 | 投影 category/actionKind |
| DT-CAT-2 | 取消/失败/中断/超时区分 | UX-03、NFR-REL-03 | — | 构造四类 | 取消=Info 且无用户错误诊断；失败=Error；中断=Info＋rerun；超时=Error＋FORCE-TERMINATED 标记 | severity/code 集合 |
| DT-AGG-1 | 聚合保留成员明细 | §6.4 | 同码多对象条目 | aggregate | AggregatedEntry 含全部成员 subject/localName | 成员引用数；展开明细 |
| DT-AGG-2 | 多工况稳定排序 | NFR-COR-02 精神 | 批量条目乱序插入 | 两次构建＋snapshot | 同序输出 | orderKey 序列相等 |
| DT-AGG-3 | 聚合不吞缺失项 | §8.1 表 2④ | 证据缺失×N＋其他 | aggregate | 全部缺失条目保留（不因聚合短路） | 成员计数＝N |
| DT-AGG-4 | 局部碰撞不升级任务不可行 | C8 | N 条构型级碰撞（策略拒绝类） | aggregate＋查任何输出 | 无 EngineeringInfeasible 语义产生（分类不变；无证明字段——本文无此能力） | 分类枚举集合；无 infeasibility-proof 条目 |
| DT-CFM-1 | Finding 创建/确认 | SA-15、MDL-06④ | 命令上下文就绪 | create→submitConfirmation（绑定一致） | Confirmed；confirmation 字段完整（principal/时间） | FindingRecord.state；core 投影 Confirmed |
| DT-CFM-2 | 拒绝路径 | MDL-06④ | Pending finding | submitRejection(user-rejected) | Invalidated；**无修订产生**（命令侧结果——契约测试与 project 桩联动验证 Rejected） | state/reasonToken |
| DT-CFM-3 | 超时（仅显式设置时） | §5.3 | expiresAtUtc 设置 | 推进 ManualClock 至过期 | Expired；默认无 expiresAtUtc 时不触发 | state；默认实例恒 Pending |
| DT-CFM-4 | 输入修订变化后确认失效 | 任务约束§五.4 | 已确认 finding | 以旧凭据对前进后的修订复核 binding | BindingMismatch→Invalidated(revision-changed)；不静默沿用 | ConfirmOutcome |
| DT-CFM-5 | 策略版本变化后确认失效 | CON-06 | 同上 | policyContentId 变化后复核 | 同上（policy-changed） | 同上 |
| DT-CFM-6 | UI 关闭回调拒绝 | project §5.3.3 | 确认等待中 | interaction isAlive=false→project abort→invalidate | Invalidated(interaction-lost)；无修订；callbackToken 作废（再用→InvalidState） | state；token 复用拒绝 |
| DT-CFM-7 | 未确认不能提交命令 | MDL-06④ | 待确认集非空＋无 interaction | project 桩以 confirmations-unresolved 提交 | 无修订；finding 仍 Pending→会话路径失效 | 命令桩结果＋finding state |
| DT-CFM-8 | worker 不产生 finding | SA-15 | worker 装配清单 | 检查 worker 注册面（静态）＋运行期 worker 侧仅 ErrorReport 通道 | create 接口不在 worker 装配暴露（链接面/装配清单断言）；通道回传的均为普通诊断 | 装配清单断言；通道帧类型 |
| DT-CFM-9 | 确认后重新校验（编译失败仍不提交） | MDL-06、SA-15 | 已确认 finding | 编译桩失败 | 命令 Failed/无修订（确认≠成功）——与 project 桩契约测试 | 命令桩结果 |
| DT-CFM-10 | 确认记录进入命令摘要 | project §6.7 | 确认＋提交成功 | 读 CommandRecord.confirmations | 四元组＋credential 完整 | 字段比对（project 桩回读） |
| DT-CHAIN-1 | 原因链不丢根因 | §8.1 | worker 崩溃场景 | translate 链（崩溃→任务失败→告知） | 派生条目 causedBy 链完整到根因 | 链遍历至 EX-WORKER-CRASHED |
| DT-COLLAB-1 | 命令/任务/运行关联 | TASK-03 | 命令与任务路径各产诊断 | snapshot 按 revision/task 过滤 | 各自命中且互不混 | DiagQuery 结果集 |
| DT-LOG-1 | 两级分流 | NFR-REL-05 | 各级记录 | 写 Error/Dev/Info | Tier-U 文件无 Dev 行/无栈/无哈希；Tier-D 全量 | 文件内容断言（模式扫描） |
| DT-LOG-2 | 关联 ID 与顺序 | §7.2/§7.4 | 多线程交错写 | 并发写＋回读 | 行完整；同线程 FIFO；全局稳定序 | 序号单调；关联字段 |
| DT-LOG-3 | 日志写失败 | §7.6 | ILogSinkOps 注入失败 | 磁盘错误注入 | DIAG-LOG-WRITE-FAILED（Dev）；**不阻塞调用方**（log 立即返回）；flush 有界失败 | 注入命中记录；无异常逃逸 |
| DT-LOG-4 | 日志丢失正式诊断仍可用 | §7.8 | 禁用日志 sink | 走完整诊断路径 | 目录/envelope 诊断不受影响 | 目录条目在；宿主持久化正常（桩验证） |
| DT-LOG-5 | worker 日志回传合并 | §7.5 | 模拟 WorkerLogBatch 乱序/断裂 | 重放合并 | 按序号恢复全局稳定序；断裂补 Dev 诊断 | 合并后序号连续性；补条存在 |
| DT-SEC-1 | 凭据不记录 | NFR-SEC-07 | 消息含 token/password 形态 | log＋redact | 输出无原文（替换计数） | 模式扫描零命中 |
| DT-SEC-2 | 路径按配置脱敏 | NFR-SEC-07 | 四种 PathPolicy | redactPath | Keep/Hash/RootOnly/Strip 各自正确 | 输出比对 |
| DT-SEC-3 | 脱敏失败降级 | §7.7 | 构造超限/非法输入 | redact | 整段 [REDACTED:redaction-failed]＋DIAG-REDACTION-FAILED；不抛、不放行原文 | 输出字面量；Dev 诊断存在 |
| DT-SEC-4 | 导出不泄敏感路径 | RPT/AT-11 精神 | 目录含路径类内容 | exportSafeSummary | 输出过脱敏（无原始路径/用户名） | 输出扫描 |
| DT-LIFE-1 | 目录容量与清理 | §6.2 | 上限调小 | 超量 append | Info 最旧淘汰；Warning/Error/活动关联保留；溢出 Dev 诊断 | 目录计数；DIAG-CATALOG-OVERFLOW |
| DT-LIFE-2 | 失败/取消后诊断保留 | NFR-REL-03 | 任务 Failed/Canceled | 终结后 snapshot | 诊断仍在（会话内）；终结原因必附 | 结果集含原因码 |
| DT-LIFE-3 | 诊断归档失败报告 | project §10.1 | 归档桩失败 | 任务终结＋归档失败路径 | EX-ARCHIVE-FAILED（用户可见）；目录/日志不受影响；**关闭不等待**（flush 有界） | 错误条目；关闭时限 |
| DT-LIFE-4 | 磁盘不足（日志/崩溃文件） | NFR-REL-01 精神 | 注入 disk-full | 写日志＋崩溃文件 | 降级（Dev 诊断/stderr 摘要）；不崩溃、不阻塞 | 注入命中；进程存活 |
| DT-LIFE-5 | 历史诊断不可因当前性改写 | CON-02 | 持久化诊断（桩）＋Superseded 投影 | 重读 | 记录字节不变 | 摘要比对 |
| DT-BUILD | 零 Qt/零反向依赖/无私有头出 include | NFR-MNT-01/02、ARCH §3.5 | — | 脚本扫描＋CMake 依赖图 | `#include <Q` 零命中；依赖仅 core 边；无 io/project/execution 链接 | 扫描报告 |
| DT-AT 映射 | AT 观测点契约（与各 AT 主责单元联动，**本文只提供观测点，不代验 AT**） | AT-01（行程上限比较型字段＋确认留痕——DT-CFM-1/10 数据形状）；AT-10（迟到丢弃开发诊断——EX-REGISTRY-\* 形状，DT-CHAIN-1）；AT-11（崩溃诊断文件写出且脱敏——DT-SEC-1/2/4＋§7.6）；AT-13（恢复诊断字段——PRJ-RECOVERY-\*，DT-DIAG-1）；AT-19（原因码一致——码表单点＋AT-19 载体在 policy/kinematics 契约测试，本文保证码值唯一来源）；AT-30（回填复算提示数据源——诊断上下文携带失效原因清单形状）；AT-34（取消/进度——正常取消非错误：DT-CAT-2） | — | — | 观测点数据形状就绪 | 各 AT 载体用例引用本文断言 |

每条用例经 `IRD_TEST_INFO` 登记需求/AT 追溯；**任何未执行测试不得标注通过**。

---

## 11. 阶段 A 实现任务拆分

局部编号 `DIAG-Txx`（diagnostics＝**WP-09**，REQUIREMENTS §3/ARCH §3.1 既有登记；与 development-task-breakdown §2.10 WP-09-T01～T06 已登记任务一一对应〔≙〕；不重排任何上游编号）。依赖顺序自上而下。阶段 A 聚焦：DiagnosticRecord 构造与校验设施、稳定码注册表、ConfirmableFinding、原因链与聚合、两级日志与脱敏、跨单元错误转换、UI 确认回调数据契约、可控诊断源与测试替身、契约测试与故障注入（任务约束§八）；**不含**：业务判定/碰撞算法/完整 UI 对话框、业务领域码值与文案内容（B/C 随域注册）、崩溃转储完整机制（WP-24-T02 协作面之外的 OS 级 dump 归 WP-24）。

| 任务 | ≙dtb | 输入 | 产物 | 依赖 | 涉及文件 | 验证方式 | 完成条件 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| DIAG-T01 任务卡定稿 | WP-09-T01 | 全部上游输入 | 本文（≥v0.1） | 无 | 本文 | 评审 | 十四节齐备；建议码全量收编登记；P-PR-6/P-EX-8 消账路径明确 |
| DIAG-T02 构建落位 | WP-09-T02 | §3.3、骨架 CMakeLists | `sdurws_ird_diagnostics` STATIC（C++17、链 core、零 Qt）；`_test`/`_contract_test` 注册（dtb §5.5 gtest） | DIAG-T01、CORE-T01 | `diagnostics/CMakeLists.txt`（新）、`src/` 空起步 | 双模式构建；DT-BUILD | 零错误；红线扫描零命中 |
| DIAG-T03 错误类型与注册表 | WP-09-T03 | §4.5/§4.6、§9.0/§9.1 | `Errors.*`、`DiagCodes.*`（CodeDescriptor/StableCodeRegistry/内置码表全量收编） | DIAG-T02 | 同名文件 | DT-REG-1~5 | 码表清单与 §4.6 一致；重复/未知/废弃用例过 |
| DIAG-T04 目录与工厂 | WP-09-T04（部分） | §4.2、§9.2/§9.7 | `Catalog.*`（DiagContext/Entry/DedupKey/DiagCatalog）、`Factory.*`（工厂＋ErrorCodeTranslator 骨架与 PRJ/RT/POLICY/EVI/EX 映射） | DIAG-T03 | 同名文件 | DT-DIAG-1/2、DT-DUP-1/2、DT-CAT-1/2 | 字段完整性/边界/去重用例过 |
| DIAG-T05 ConfirmableFinding 服务 | WP-09-T03/T04（设施面） | §5、§9.3 | `Confirmable.*`（FindingId/Binding/Record/服务/状态机/失效条件） | DIAG-T04 | 同名文件 | DT-CFM-1~10 | 状态机全转移/绑定复核/失效路径用例过（含 project 桩联动） |
| DIAG-T06 聚合器 | —（WP-09-T03 范围） | §6.3~§6.5、§9.4 | `Aggregation.*`（CauseLink/规则/稳定排序） | DIAG-T04 | 同名文件 | DT-AGG-1~4、DT-CHAIN-1 | 聚合/排序/不升级反例过 |
| DIAG-T07 两级日志 | WP-09-T04 | §7.1~§7.5、§9.6 | `Logging.*`（管线/节流采样/轮转/WorkerLogBatch 重放；ILogFileOps 接缝） | DIAG-T02、DIAG-T03 | 同名文件 | DT-LOG-1~5 | 分流/顺序/失败注入/合并用例过 |
| DIAG-T08 脱敏与崩溃文件 | WP-09-T05 | §7.6/§7.7、§9.5 | `Redaction.*`、`CrashReport.*` | DIAG-T07 | 同名文件 | DT-SEC-1~4、DT-LIFE-4 | 凭据零命中/路径四策略/降级/崩溃文件用例过 |
| DIAG-T09 生命周期与容量 | —（WP-09-T04 范围） | §6.1/§6.2 | 目录清理策略、宿主留痕形状（envelope/CommandRecord 对接数据） | DIAG-T04/T07 | `Catalog.*` 扩展 | DT-LIFE-1~3/5 | 保留/清理/不可改写用例过 |
| DIAG-T10 测试替身与契约套件 | WP-09-T06 | §10、testkit | `ScriptedDiagSource`（脚本化可控诊断源：按序列产出预设条目/finding/日志行）＋DT-\* 全部用例体 | DIAG-T03~T09、testkit 可用 | `diagnostics/test/*`、`contract_test/*` | §10 矩阵逐条 | 全部用例通过并留痕；替身边界声明（替身输出仅验证契约——testkit EV-REG-3 同模式） |
| DIAG-T11 文档与门禁同步 | — | 全文 | README 指向核对（§9→§11 已随本文修正）；§14.3 状态更新；P-PR-6/P-EX-8 消账确认提交（project/execution 详设修订联动） | T01~T10 | 本文、`include/.../README.md` | 评审 | 本文与实现零偏差登记；待裁决最新状态 |

每任务完成条件均含"测试通过并留痕"；任何未执行测试不得标注通过。

---

## 12. 后续阶段承接与接口交接清单

### 12.1 diagnostics 在后续阶段的交付（本文已设计、随阶段落地）

| 能力 | 阶段/触发 | 锚点 |
| --- | --- | --- |
| 业务领域码注册（MDL-\*/REQ-\*/KIN-\*/TRJ-\*/DYN-\*/SEL-\*/OPT-\*/WF-\*）与领域文案键 | B/C/D 随各域卡 | §4.5 注册协议；§8.8 |
| io/ui 码表（IO-\*/UI-\*）与映射规则 | B（io/ui 卡产出） | §8.6/§8.9、§4.6 |
| 报告诊断章节消费（RPT-01-B/C 的诊断章节）与 exportSafeSummary 正式接入 | B/C | §9.7、§8.10 |
| PM-15 恢复横幅/诊断表/日志面板的 ui 正式接入 | B（WP-10） | §9.7 投影契约 |
| 崩溃诊断文件与 OS 级崩溃捕获的完整链路（WP-24-T02 协作） | A 末/E | §7.6 |
| 会话日志持久化策略扩展（如项目绑定日志归集） | 按需（需求变更评估） | §7.8 边界复述 |

### 12.2 接口交接清单（各单元详设/实现的直接输入）

| 单元 | 从 diagnostics 接收 | 须在其侧冻结/提供的相邻契约 |
| --- | --- | --- |
| core | FindingId/信封字段扩展建议（P-DIAG-4：Id128 新 tag `fnd-` 收编）；确认服务状态机实现承载说明（P-DIAG-6） | core 契约冻结 diff（P-DIAG-1）；DiagData 不变量稳定性 |
| project | IDiagnosticsSink 实现与统一（P-PR-6 消账）；PRJ-\* 码收编确认；ConfirmableFinding 工厂/复核设施（§5）；CommandRecord.confirmations 形状（binding 四元组＋credential——其 §4.4.4 的 diagnostics 侧数据源） | ICommandInteraction 桥接（§5.5 时序⑥⑦⑧的 project 侧已冻结——project §5.3.3/§6.10，无增量）；编译前复核时机（其 §6.7——已对齐） |
| execution | IExecutionDiagnosticsSink 实现与统一（P-EX-8 消账）；EX-\* 码收编确认；WorkerLogBatch 帧契约（回传格式/序号——其通道 §6 帧表登记时对齐本文 §7.5） | ErrorReport 通道帧类型（诊断批与日志批的区分）；崩溃事件→EX-WORKER-CRASHED 的调用点（其 §6.4） |
| evidence | EVI-\* 码收编确认；诊断随 envelope 的形状约束（Dev 码不入 envelope——historical=false） | envelope canonical 编码中 DiagnosticRecord 的嵌套编码（其 §7.1 DomainPayload 同域登记） |
| runtime / policy | RT-\*/POLICY-\* 码收编确认；translate 兜底规则（§8.3） | 各自 ErrorCode→码映射的最终表（其 §10.11/§9.6 已给建议值——本文按原文收编，无重排） |
| io | 码表注册协议；SafePath/BudgetGuard 诊断形状（§8.6：比较型预算三要素） | IO-\* 码值与 paramSchema（io 卡） |
| ui | DiagProjectionItem/FindingRecord 投影（§9.7/§9.3）；actionKind 动作族→呈现映射；确认对话数据（比较型三要素＋选项键）；日志面板数据（Tier-U） | 文案资源（titleKey/detailKey 解析——UX-02）；七态投影（P-EV-4 关联）；ICommandInteraction 实现（P-PR-7——project 已给起点） |
| reporting | exportSafeSummary 契约；码表只读访问（deprecated 映射） | 报告章节字段选择与限定语（RPT-05——rendering 侧冻结） |
| 各业务域 | 注册协议＋工厂＋sink＋日志/脱敏设施（全部复用，不复制） | 领域 CodeDescriptor 清单＋paramSchema＋文案键；域错误→码映射数据（§8.8） |
| testkit | ScriptedDiagSource 基座；checkDiagnosticRecord 消费的契约稳定性 | 契约夹具数据集登记（diagnostics 行——其 §10.2） |
| WP-24 | 崩溃诊断文件格式与位置（用户目录，脱敏） | 应用壳异常捕获接入点（PM-17） |

---

## 13. 需求—设计—验证追踪矩阵

| 需求/上游条款 | 设计落点 | 验证（§10 组） |
| --- | --- | --- |
| ERR-01（诊断字段/绑定对象/三要素/不适用标记） | §4.1/§4.2 信封与工厂、§9.2 | DT-DIAG-1/2、DT-REG-3 |
| MDL-06④＋SA-15（可确认诊断放行） | §5 全节、§9.3 | DT-CFM-1~10 |
| TASK-02＋§8.1 表 3（取消/失败/中断不进正式报告；NotApplicable） | §4.3 分类、§6.2 生命周期 | DT-CAT-1/2、DT-LIFE-2 |
| TASK-03（五元组关联） | §4.2 DiagContext.task、§8.10 | DT-COLLAB-1 |
| CON-02（三轴正交；历史不可改写） | §4.3/§4.4 呈现映射不改轴、§6.2 | DT-CAT-1、DT-LIFE-5 |
| CON-04（部分/失败不作缓存命中） | §4.6 EX-CHECKPOINT-\*/EVI-CACHE-INCOMPATIBLE 收编 | DT-REG-1（码表） |
| CON-06（策略内容身份进确认绑定） | §5.2/§5.3 binding.policyContentId | DT-CFM-5 |
| EVI-01/02（证据缺失全量列出） | §6.4 聚合不吞缺失、§4.6 EVI 行 | DT-AGG-3 |
| §8.1 C5/C8（搜索未果/碰撞作用域——不升级） | §6.4 聚合不升级、§4.4 策略拒绝类 | DT-AGG-4 |
| PM-08/PM-15（恢复诊断/统一诊断目录） | §4.6 PRJ-RECOVERY-\*、§9.7 目录投影 | DT-DIAG-1、DT-COLLAB-1 |
| PM-17（异常诊断文件） | §7.6 | DT-SEC-1/2/4、DT-LIFE-4 |
| UX-02（不显示哈希/内部名） | §4.1 文案键体系、§7.1 Tier-U 禁项 | DT-LOG-1 |
| UX-03（三要素；正常取消非错误） | §4.4 矩阵取消行、工厂校验 | DT-CAT-2、DT-DIAG-1 |
| UX-10（失败附定位与修复建议） | §4.2 定位字段、§4.4 actionKind | DT-DIAG-1 |
| NFR-SEC-07（脱敏） | §7.7、§9.5 | DT-SEC-1~4 |
| NFR-REL-05（两级日志） | §7.1~§7.5、§9.6 | DT-LOG-1~5 |
| NFR-MNT-01/02（零 Qt/静态扫描） | §1.4 D-01、§3.2 | DT-BUILD |
| NFR-MNT-03（文案/码单一权威） | §4.5 注册表、titleKey 唯一性 | DT-REG-1/2 |
| NFR-MNT-04（禁重复设施） | §2.3、§8.8（域不复制设施） | 设计评审＋DT-BUILD |
| NFR-COR-02 精神（稳定排序/确定性） | §6.4 orderKey、§7.4 日志序 | DT-AGG-2、DT-LOG-2 |
| ARCH §3.5（diagnostics→core 唯一边） | §3.2 | DT-BUILD |
| ARCH §7.8 SA-12（契约/实现分离） | §4.1 双层模型 | DT-BUILD＋评审 |
| ARCH §7.1 SA-15（确认流分工/worker 禁令） | §5.1/§5.6 | DT-CFM-6~8 |
| AT-01/10/11/13/19/30/34（观测点） | §10 DT-AT 行 | 各 AT 主责单元载体（本文不代验） |
| dtb WP-09-T01~T06 | §11 任务映射 | 实现留痕 |

---

## 14. 设计决策、风险、待裁决项与变更记录

### 14.1 设计决策登记（本文作出并说明理由的普通实现选择）

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | diagnostics 目标零 Qt（含 Core） | L3 允许但非必需；dtb WP-09-T02 明示零 Qt；std 文件/时间/线程足够（日志/崩溃文件用 std::filesystem）；最大化模型测试直调（NFR-MNT-01 精神；project D-01 同源）。备选（QFile/QTimer）被否 |
| D-02 | 双层诊断模型（core record 内嵌＋DiagnosticEntry 信封） | 契约/实现分离（SA-12）的忠实落地：core 契约不扩充；目录/关联/分类/去重等设施字段归信封；持久化只涉及 record（随宿主） |
| D-03 | 分类 15 值＋严重 4 值为实现承载词表，锚定上游语义（§4.4 矩阵逐行给锚点） | 上游无需求级诊断分类/严重枚举（ERR-01 只定字段）；P-EV-8 同模式登记；备选（不设分类，纯码表自由标注）会丧失 UX-10/PM-15 的分组呈现与 §7 分流依据 |
| D-04 | 码前缀＝所有权（PRJ/RT/POLICY/EVI/EX/DIAG…）；收编四卡建议值原文不重排 | dtb WP-09-T03 明文"码值不重排已登记建议"；前缀即注册期所有权校验依据 |
| D-05 | FindingId＝128 位随机（`fnd-<32hex>`），diagnostics 层类型 | 与 core Id128 同构但 tag 未在 core 冻结集内（obj/prj/brn/rev/run/evt/att）；不私自改 core——P-DIAG-4 提交接 |
| D-06 | finding 服务端五态（+Invalidated/Expired）；core 三态投影 | core ConfirmationState 是数据契约（持久化面）；失效/过期是设施生命周期事实（会话态），混入 core 会扩大其契约面 |
| D-07 | 绑定四元组＝project §6.7 原文（findingDigest/policyContentId/commandDigest/baseRevisionId）；本文为计算与复核设施 | SA-15 单侧冻结的对接；摘要算法统一经 core ContentDigester（跨进程一致） |
| D-08 | 两级日志＝同一管线双 Tier（Tier-U ⊆ Tier-D） | NFR-REL-05"日志分两级"而非"两套日志"；单管线保证脱敏/顺序/关联一致性 |
| D-09 | 脱敏默认拒绝（deny-by-default）：结构化键白名单＋敏感模式替换＋失败整段降级 | NFR-SEC-07 保守方向；"宁可丢信息不泄敏感"（任务约束§三） |
| D-10 | 日志/目录/崩溃文件不入 .rwdesign（用户目录/会话内存） | 磁盘写入唯一归属 project（§17 表注）；日志非项目历史第二权威（§7.8） |
| D-11 | 聚合仅呈现分组，成员引用全保留，永不升级工程语义 | C8 作用域＋表 2④ 全量列出；聚合无判定权（N-1） |
| D-12 | 去重键含 subject（不同对象绝不合并）；去重只影响目录呈现不影响 envelope 全量 | 任务约束§五.6/表 2④ 双约束的并集 |
| D-13 | worker 诊断/日志经 execution 通道回传（ErrorReport/WorkerLogBatch），本文无跨进程设施 | ARCH §4 通道模型；execution 边界规则（worker 无 UI/无直写）；SA-10 依赖红线 |
| D-14 | 错误转换＝装配期类型映射表（ErrorCodeTranslator），禁字符串匹配 | §8.1 规则 2 的机制承载；类型安全＋可静态审计 |
| D-15 | 诊断顺序＝orderKey（时间戳计数/entryId/code）；日志同线程 FIFO＋全局稳定序 | NFR-COR-02 精神（可复现呈现）；DT-AGG-2/DT-LOG-2 钉住 |
| D-16 | 正常取消不产生用户诊断（任务事实为 Info 记录且仅开发/任务视图） | UX-03 明文；取消失败才走错误路径（EX-FORCE-TERMINATED） |
| D-17 | 目录容量护栏（默认 10,000，可配）＋分级淘汰 | 长会话/大规模批量诊断（NFR-PERF-03 规模精神）防内存无界；Warning/Error 不淘汰保追溯 |
| D-18 | 创建与记录分离（工厂不入目录；append 显式） | 便于测试替身（ScriptedDiagSource）与"评估路径先造后随宿主收口"的时序（envelope 收口前目录可见——§6.2 分批） |

### 14.2 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | core.md v0.1 未冻结（DiagData/身份签名可能变） | DIAG-T03 起返工 | §3.2 消费清单逐项锚定；core 冻结 diff 后增量同步（P-DIAG-1） |
| R-2 | ARCH v0.11 Draft 评审（A9+）调整 §7.8/SA-15 | §4/§5 返工 | 严格锚定明文；P-DIAG-2 登记同步义务 |
| R-3 | 分类/严重词表与各域/ ui 呈现期望不一致（实现承载未获确认） | 呈现分组返工 | P-DIAG-3 集中登记；§4.4 锚点全部可追溯到上游条款，语义面窄 |
| R-4 | 四卡建议码与其最终实现漂移（收编后上游改码） | 码表双账本 | 收编后以本文注册表为唯一权威（NFR-MNT-03）；上游修订须经本文（dtb WP-09-T03 完成条件"各卡建议码全部收编登记"） |
| R-5 | worker 日志回传与 execution 通道帧契约未对齐（其 §6 帧表冻结晚于本文） | DT-LOG-5/合并逻辑返工 | §7.5 只定载荷语义（WorkerLogBatch），帧类型归 execution §6（§12.2 交接）；P-EX-3/P-EX-8 联动 |
| R-6 | 大量诊断的目录/日志性能（NFR-PERF-03 规模） | UI 卡顿/内存 | 异步管线＋容量护栏（D-17）＋节流（§7.4）；性能验收归 WP-23 |
| R-7 | 脱敏误伤（过度脱敏致开发诊断失去价值） | 排障效率 | 敏感模式清单受控（只增）；RootOnly 默认保留可用信息；模式命中计数可观测（DT-SEC-1 观测点） |
| R-8 | 确认流三单元（本文/project/ui）契约交叉依赖均未冻结 | 集成返工 | §5 全部对接点已按 project v0.1 冻结侧对齐（无增量要求）；P-DIAG-5/P-PR-7 联动跟踪 |

### 14.3 待裁决项（问题—依据—影响—建议—需要裁决者）

| # | 问题 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-DIAG-1 | 本文消费的 core 契约以其 v0.1（Draft）为基线；冻结版可能调整签名（DiagData/Id128/词表） | core.md 文档头状态行 | DIAG-T03 起返工 | core 冻结时出 diff 清单，本文按影响面增量修订并留痕 | core 详设所有者 |
| P-DIAG-2 | ARCHITECTURE v0.11 Draft 待评审；本文以其为基线 | 架构文档头 | 评审结论可能要求同步（§7.8/SA-15/依赖表） | 评审后按影响面增量修订 | 架构所有者 |
| P-DIAG-3 | DiagnosticCategory（15）/DiagnosticSeverity（4）/actionKind 动作族为本文实现承载词表：上游无需求级枚举（ERR-01 只定字段；NFR-REL-05 只分两级日志），但 UX-10/PM-15 分组呈现与 §7 分流需要机器可读分类 | ERR-01、NFR-REL-05、UX-10、PM-15 | 下游（ui/reporting）呈现分组依赖该词表；若需求侧未来定义正式枚举需对齐 | 维持实现承载＋§4.4 逐行上游锚点（不新增工程状态——呈现映射只引用既有轴值）；需求侧如需冻结走需求变更 | 需求所有者＋ui/reporting 详设 |
| P-DIAG-4 | FindingId 的类型归属：core Id128 体系现冻结 7 tag（obj/prj/brn/rev/run/evt/att），无 `fnd-`；本文按同构自定义（D-05） | core.md §4.1/§5.1 | 跨单元规范文本解析无统一入口；core 若收编则签名变更 | 建议 core 下次修订收编 `fnd-` tag（影响面小：新增独立类型）；收编前本文自持解析（句法与 core 严格一致） | core 详设所有者 |
| P-DIAG-5 | project §5.0 IDiagnosticsSink 与 execution §3.3 IExecutionDiagnosticsSink 的接口统一：两者为消费方单侧定义（diagnostics 产出前占位），本文 §9.7 DiagnosticsSinkImpl 承诺实现二者语义（report/reportDev），但接口名/归属（diagnostics 定义统一接口 vs 各消费方自持＋L5 适配）未裁决 | project P-PR-6、execution P-EX-8 | 三处接口漂移风险；装配层适配代码量 | 建议方向：diagnostics 冻结 `IDiagnosticSink`（§9.7）为标准接口，project/execution 详设修订时将其本地 sink 别名/继承到标准接口（一次性小改）；过渡期 L5 适配器桥接（本文已承诺实现兼容） | project/execution 详设所有者＋架构所有者 |
| P-DIAG-6 | ConfirmableFinding 服务端扩展态（Invalidated/Expired）与 `expiresAtUtc` 字段：SA-15/MDL-06④ 未定义超时语义；project §5.3.4 明言"无超时自动确认" | SA-15、project §5.3.4 | 若需求侧未来引入确认超时（如安全审计要求），本文状态机已预留 | 维持默认无限期（仅失效条件驱动）；expiresAtUtc 字段保留但默认 nullopt；启用＝需求变更＋码表/文案更新 | 需求所有者 |
| P-DIAG-7 | 日志文件轮转参数（默认 5×2 MiB/文件、flush 窗口 N=256/T=2 s）与目录容量（10,000）为本文工程默认；上游未定义 | NFR-REL-05 无参数；性能归 WP-23 | 长会话磁盘/内存占用 | 维持可配默认；WP-23 性能验收时校准；参数不属需求语义（实现配置） | 构建负责人（WP-23 消费时确认） |
| P-DIAG-8 | dtb §1.1 指出"diagnostics 虽为 L3，但被 project/execution/io/ui 依赖，其任务卡与 StableCodeRegistry 必须先于四者实现"——execution.md 已产出但其目标暂未链接 diagnostics（其 §3.4：边经注入实现）；本文按"入边存在、链接随消费方任务启用"理解 | ARCH §3.5、execution §3.4、dtb §1.1 | 契约测试（DT-COLLAB/DT-CFM 桩联动）需 mock 还是链接执行 | 本文测试以桩+契约夹具验证；execution 链接启用（EX-T01 后按 P-EX-8）时补双库集成冒烟 | execution 详设所有者＋构建负责人 |
| P-DIAG-9 | 用户级文案本地化策略：本文冻结"文案键"体系（titleKey/detailKey），文案值与语言解析归 ui/文案资源；多语言（中英）是否 R1 范围上游未定义 | UX-02、NFR-MNT-03 | 键体系是持久化契约（进码表），值策略影响 ui/交付 | 维持键/值分离（键本文冻结、值归 ui）；R1 语言范围走需求侧确认（不影响本文设施） | 需求所有者＋ui 详设 |

### 14.4 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-10 | 首版：基于 REQUIREMENTS v1.16（Accepted）、ARCHITECTURE v0.11（Draft）与 core/testkit/project/evidence/runtime/policy/execution 七份协作输入（均 Draft；DETAILED-DESIGN/development-task-breakdown 已存在并实测登记）完成 14 节详细设计；冻结双层诊断模型（core 契约＋DiagnosticEntry 信封）、分类/严重词表与映射矩阵（实现承载登记）、StableCodeRegistry（收编 PRJ-\*10/RT-\*14/POLICY-\*23/EVI-\*7/EX-\*18/DIAG-\*7 建议码全量）、ConfirmableFinding 生命周期（绑定四元组/失效条件/五态服务状态机/worker 禁令）、原因链与聚合规则（不升级工程语义）、两级日志（单管线双 Tier/worker 回传/崩溃前保留）、脱敏（deny-by-default/降级）、跨单元错误转换（类型映射禁字符串匹配）、七个公共接口；验证矩阵 DT-\* 29 组；实现任务 DIAG-T01～T11（≙WP-09-T01～T06）；待裁决 9 项（P-DIAG-1～9）。同日：`diagnostics/include/sdurws/ird/diagnostics/README.md` 的任务卡指向由"§9"修正为"§11"（与本文任务拆分章节号一致——core/evidence 同先例） |

### 14.5 交付前自审记录（v0.1；自审≠实现测试≠正式验收）

| 检查项（任务约束§八） | 结论 | 证据位置 |
| --- | --- | --- |
| 是否重复 core/evidence/project/execution 的职责 | ✔ 未重复：ERR-01 字段契约归 core（内嵌不扩充，N-10）；工程判定/证据汇总归 evidence（N-1）；放行编排/持久化归 project（N-2）；调度/崩溃检测归 execution（N-5/N-9）；本文仅信封/设施/转换 | §2.1、§4.1、§5.1 |
| 是否以日志文本代替稳定错误码 | ✔ 码值权威＝注册表；工厂只接受注册码；异常文本作码被拒（DT-REG-4）；日志行凡对应诊断必带 code（§7.2） | §4.5、§7.2、§9.2 |
| 是否把确认视为自动成功 | ✔ 确认后编译照常、失败仍无修订（DT-CFM-9）；绑定四元组双复核（提交时＋编译前）；旧凭据不复用（DT-CFM-4/5） | §5.3 |
| 是否让 worker 直接触发 UI | ✔ worker 装配不暴露 create 接口；诊断经通道回传（DT-CFM-8）；SA-15"确认只在命令边界" | §5.1/§5.6、§7.5 |
| 是否在输入、策略或权限变化后继续使用旧确认 | ✔ 三类失效条件＋binding 复核（Invalidated；不静默沿用） | §5.3 |
| 是否丢失根因、作用对象或版本身份 | ✔ causedBy 链强制（DT-CHAIN-1）；DedupKey 含 subject（DT-DUP-2）；DiagContext 携带修订/五元组/策略身份/契约版本 | §6.3、§6.4、§4.2 |
| 是否把局部失败升级为工程不可行 | ✔ 聚合仅呈现分组、无判定权（DT-AGG-4）；C8 作用域锚定 | §6.4 |
| 是否泄露路径、用户信息或资源内容 | ✔ deny-by-default 脱敏＋失败降级＋导出双保险（DT-SEC-1~4）；凭据一律不记录；principal 不入日志 | §7.7 |
| 是否存在诊断归档导致的永久等待 | ✔ 归档失败即报告即释放（DT-LIFE-3）；日志 flush 有界；finding 失效即回收；目录清理不阻塞 | §6.2、§5.6、§9.6 |
| 是否引入未经上游批准的新状态、阈值或证据等级 | ✔ 无新增轴值/证据等级（分类仅呈现映射且逐行锚定上游——P-DIAG-3 登记）；无业务阈值（4π 等归 policy）；数值默认（容量/轮转）标注为实现配置（P-DIAG-7） | §4.3/§4.4、§14.3 |
| 是否越权修改需求、架构或其他单元机制 | ✔ 未修改任何上游文件；接口统一（sink）与 FindingId 收编均以待裁决登记（P-DIAG-5/4）；README 指向修正属本文产物自身的结构对齐 | §14.3、§14.4 |
| 未参与开发者可直接实现 | ✔ 每数据类型字段表（§4.2/§5.2）、每接口签名/前置/错误/线程/生命周期/合法非法（§9）、验证矩阵含观测点（§10）、任务卡（§11） | 全文 |
| 上游输入版本如实登记 | ✔ §1.2 磁盘实测表（含 DETAILED-DESIGN/breakdown 已存在的更新状态，未虚构） | §1.2 |
