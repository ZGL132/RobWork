# 治理协调与待裁决项流转登记（governance-log）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（随 WP-00-T01 建立，2026-09-10） |
| 文档代号 | GOV-LOG |
| 用途 | 11 张单元卡全部 P-\* 待裁决项与 development-task-breakdown §4 O-01~O-33 的**集中流转登记**：编号/所有者/状态/消账留痕一表可查 |
| 基本纪律 | **只登记流转、不裁决任何 P-\*/O-\* 项**——裁决权归各登记行"所有者"列对应的文档所有者（需求/架构/详设/构建约定）；本文件状态翻转仅跟随权威文档的消账事实（增量修订/验收记录/定稿行），不自行宣布裁决 |
| 权威关系 | 各登记的**权威本体**仍在单元卡待裁决节与 DTB §4；本文件是集中视图与流转台账——两者不一致时以卡/DTB 为准并回改本表（留痕） |
| 需求追溯 | PILOT-01（支持：试点签署与缺陷门禁的待决依赖可集中追踪）、DEL-02（支持：Blocker/Critical 处置有登记可循）；ARCHITECTURE §11.3（变更管理与待同步）、DTB §2.1（WP-00-T01 任务行） |
| 状态词表 | open（登记待裁决）／closed（已裁决并有消账留痕）／partial（部分消账，余项留痕注明）／stale（登记所依据的事实已被后续实测推翻，卡内待翻转——本表登记现状并注明证据）／deferred（明文延期，触发条件注明） |

## 1. 单元卡 P-* 待裁决项集中登记

> 逐卡核对（§3 零丢失矩阵）：core §10.2（6）｜testkit §12.3（7）｜evidence §15.3（9）｜runtime §15.3（9）｜policy §15.3（9）｜project §15.3（8）｜execution §15.3（10）｜diagnostics §14.3（9）｜io §15.3（7）｜reporting §14.3（9）｜ui §16.4（10）——合计 **93** 项。

### 1.1 core（6 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-AR-1 | SafePath/BudgetGuard 归属措辞（ARCH §3.5"用途"列可误读为类型在 core） | 架构所有者 | open | core 已按"归 io"设计（§2.4）；待架构侧在依赖表澄清措辞 |
| P-AR-2 | ARCHITECTURE v0.11 Draft 待评审（同源：O-03） | 架构所有者 | open | 评审后按影响面增量修订 |
| P-AR-3 | "证据等级"是否另有独立词表（等级＝模式效力？） | evidence 详设所有者＋需求侧 | open | evidence §3.4 已按"等级＝模式效力"回复确认，待正式冻结留痕 |
| P-D-1 | 四词表（EvaluationMode/TaskOutcome/EngineeringStatus/TaskState）置 core 的跨卡确认（同源：O-24） | execution/ui 卡 | open | evidence/runtime/policy 已默认承接；待 WP-08-T01/WP-10-T01 侧确认 |
| P-ENV-1 | C++17 与 C++11 基线同树混链 | 构建负责人 | **closed** | 2026-09-10 关闭（findings F-003）：CORE-T01 双模式构建＋验收独立复现；core.md v0.3 翻转 |
| P-ENV-2 | `_test` 目标 gtest 接入机制 | 构建约定所有者 | **closed** | 2026-09-10 关闭（CR-07）：DTB §5.5 定稿（vcpkg＋find_package REQUIRED）；§4.4 定稿行登记 gtest@1.18.0；core.md v0.2 消账 |

### 1.2 testkit（7 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-TK-1 | gtest 接入机制（与 P-ENV-2 同源） | 构建约定所有者 | **closed** | 2026-09-10 关闭（CR-07）：testkit.md v0.2 消账；TK-T01 已按机制落地（验收 pass，2026-09-10） |
| P-TK-2 | core v0.1 Draft 契约基线漂移 | core 详设所有者 | open | core 冻结时出 diff 清单 |
| P-TK-3 | TK-Txx 与 WP 分配对齐（≙映射制） | 任务分解所有者 | **partial** | DTB 已建立"≙"映射登记制并收编（DTB §4.4 定稿行，v0.5）；卡内条目待下次增量修订翻转 |
| P-TK-4 | ARCH §3.5 未列 testkit 边（同源：O-21） | 架构所有者 | **closed** | 2026-09-10 消账（WP-01-T01）：ARCH §3.5 补登 testkit→core 行＋门禁白名单收录；testkit 卡侧待增量修订同步翻转 |
| P-TK-5 | 大体绩黄金数据存储/CI 策略（同源：O-22） | 构建负责人＋WP-02 | open | 首个大数据集出现时裁决；阶段 A 源内 |
| P-TK-6 | JsonLite 与统一测试侧 JSON 机制归并 | 构建约定所有者 | open | breakdown 若统一机制则 JsonLite 退役换源 |
| P-TK-7 | requirements-ids.json lint 字典维护责任（同源：O-23） | 需求所有者＋WP-02 | open | 建议随需求变更流程同步更新 |

### 1.3 evidence（9 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-EV-1 | core v0.1 Draft 契约基线漂移 | core 详设所有者＋本卡 | open | core 冻结时出 diff 清单 |
| P-EV-2 | ARCH Draft 待评审（同源：O-03） | 架构所有者 | open | 评审后增量修订 |
| P-EV-3 | 跨域汇总顺序（③先于④字面顺序）与 C6 边界（同源：O-13） | 需求所有者 | open | 卡按保守字面顺序实现；如需他语义走需求变更 |
| P-EV-4 | 当前性"不可判定"呈现口径（两持久态＋诊断） | ui 详设所有者＋需求侧 | open | ui 详设产出时引用本条定呈现（ui P-UI-2 对应） |
| P-EV-5 | 覆盖率参考值降级限定语呈现归 reporting/ui | reporting 详设所有者 | open | reporting 详设引用（RPT-05 限定语） |
| P-EV-6 | project.md 磁盘不完整（§8~§15 缺失） | project 详设所有者 | **stale** | 2026-09-10 实测 project.md §1~§15 完整（DTB §4.4 定稿行；runtime P-RT-9 同源指出）——本登记已过时，卡内待翻转；接口联合复核仍需执行（phase-one §2） |
| P-EV-7 | 空必验工况集/全不适用工况的判定口径（同源：O-13） | 需求所有者 | open | 卡按保守处置（DataInsufficient） |
| P-EV-8 | EvidenceItemStatus 五值实现词表（Unverified 为扩展，同源：O-13） | 需求所有者＋reporting/ui | open | 不作需求级冻结契约；需求侧未来定义枚举则增量对齐 |
| P-EV-9 | 必验工况标记权威 schema 归 requirements（同源：O-14） | requirements 卡 | open | WP-14-T01 冻结后回接 evidence §4.1.3 |

### 1.4 runtime（9 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-RT-1 | core v0.1 Draft 契约基线漂移 | core 详设所有者＋本卡 | open | core 冻结时出 diff 清单 |
| P-RT-2 | ARCH Draft 待评审（同源：O-03） | 架构所有者 | open | 评审后增量修订 |
| P-RT-3 | L2 合法 L1 依赖集（§2.3 vs §7.3 措辞，同源：O-15） | 架构所有者 | open | WP-06-T01 已按 §7.3 明文执行并留痕 |
| P-RT-4 | 安装预设轴向单侧选择（同源：O-16） | modeling 详设所有者＋需求侧 | open | WP-13-T11 交叉核对后冻结 |
| P-RT-5 | IRobotDesignReader 注入 vs modeling 直解 | modeling 详设所有者＋本卡 | open | modeling 详设起草时交叉核对 |
| P-RT-6 | ResourceBytes 生命周期（=P-IO-2，同源：O-17） | io 详设所有者 | **closed** | 设计级关闭（io §8.6 五条裁决，2026-09-10 回接）；实现测试待 IO-T05/06 执行 |
| P-RT-7 | 耦合矩阵"病态"阈值 1×10⁸ 归属（同源：O-18） | 需求所有者＋policy 卡 | open | 建议并入 EngineeringPolicySet；归 policy 时走其登记 |
| P-RT-8 | IModelCompilePort 适配差异的 project 侧确认 | project 详设所有者 | open | runtime 已按兼容处理（§10.2）；待 project 确认无增量需求 |
| P-RT-9 | evidence P-EV-6 登记已过时的提示 | evidence 详设所有者 | **closed** | 事实登记型：project.md 完整已实测（与 P-EV-6 stale 联动消账） |

### 1.5 policy（9 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-POL-1 | runtime 供给接口交叉核对 | runtime 详设所有者＋本卡 | **closed** | 2026-09-10 关闭（CR-04）：runtime.md 承接全部供给，映射见 foundation-api-diff.md |
| P-POL-2 | 安全间距/近限位比/条件数阈值无冻结默认（同源：O-10） | 需求所有者＋WP-07 | open | 裁决前 nullopt＝显式不适用 |
| P-POL-3 | SEL-05 惯量比阈值归属（同源：O-11） | 需求所有者＋selection 卡 | open | 建议归 EngineeringPolicySet；WP-19-T01 起草时核对 |
| P-POL-4 | 三份协作输入＋ARCH 均 Draft | 各详设所有者＋本卡 | open | 各自冻结时出 diff 清单 |
| P-POL-5 | 碰撞后端版本取值依赖 NFR-DEP-05 基线（同源：O-20） | 版本基线所有者（WP-24） | open | WP-24-T01 冻结时锁定 |
| P-POL-6 | project.md 磁盘不完整（与 P-EV-6 同源） | project 详设所有者 | **stale** | 2026-09-10 实测完整（DTB §4.4）——登记过时，卡内待翻转；策略编辑适配器细节随 project 联合复核 |
| P-POL-7 | 按工况差异化策略阈值口径 | 需求所有者 | open | 卡按"全项目统一、场景对象集表达"设计（D-14） |
| P-POL-8 | R-4 例外登记措辞补充（policy 只读消费整名，同源：O-12） | 架构所有者 | **partial** | DTB §4.5 已预登记该例外行（状态"待确认"）；架构侧措辞确认后转生效 |
| P-POL-9 | 策略编辑命令处理器适配器宿主 | 构建约定所有者＋ui 详设所有者 | **closed** | 2026-09-10 定稿（DTB §4.4 定稿行）：ui 单元承载（L5 装配期注入），WP-10-T07 落地；卡内待翻转 |

### 1.6 project（8 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-PR-1 | core v0.1 Draft 契约基线漂移 | core 详设所有者 | open | core 冻结时出 diff 清单 |
| P-PR-2 | ARCH Draft 待评审（同源：O-03） | 架构所有者 | open | 评审后增量修订 |
| P-PR-3 | §7.1 图示"前置断言"位置读法（编排 vs 判定，同源：O-08） | 架构所有者 | open | project 已按"编排归 project、判定归注入处理器＋④端口"设计 |
| P-PR-4 | 归档调用线程与 RunRegistry 对接 | execution 详设所有者 | **partial** | execution §9.5 已冻结方案（调度线程＋writer 互斥＋幂等）；待 project 侧确认消账（与 P-EX-5 成对） |
| P-PR-5 | project→io 依赖边未登记（同源：O-09/P-IO-1） | 架构所有者 | open | 注入式先行；阶段 B 落地前裁决补边或确认注入 |
| P-PR-6 | PRJ-\* 稳定码与 sink 形态待 diagnostics 冻结 | diagnostics 详设所有者 | open | 与 P-EX-8/P-DIAG-5 同案（sink 统一未裁决——phase-one §2.1 确认） |
| P-PR-7 | ICommandInteraction/IModelCompilePort 对端确认 | ui/runtime 详设所有者 | open | runtime 已承接（P-RT-8）；ui 侧待 WP-10 起草核对 |
| P-PR-8 | 分支 label 不可改名是否满足 PM-11（同源：O-19） | 需求所有者 | open | 维持创建时写入；需求侧确认口径 |

### 1.7 execution（10 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-EX-1 | 六份协作输入均 Draft | 各卡所有者＋本卡 | open | 各卡冻结时出 diff 清单 |
| P-EX-2 | ARCH Draft 待评审（同源：O-03） | 架构所有者 | open | 评审后增量修订 |
| P-EX-3 | 任务指令"可依赖六单元"vs ARCH §3.5 四条边 | 架构所有者 | open | 卡按注入消费（D-17）；如需直连走架构修订 |
| P-EX-4 | 11 态 vs 九态词表 | 需求所有者 | open | 按上游九态执行；三态显式化走需求变更 |
| P-EX-5 | 归档对接（=P-PR-4 对端） | project 详设所有者 | **partial** | execution §9.5 已冻结；project 确认后成对消账 |
| P-EX-6 | 排队任务崩溃不呈现"已中断"的判据 | 需求所有者＋project 卡 | open | 维持 D-12/D-13（磁盘痕迹唯一经 project） |
| P-EX-7 | 评估器运行期能力声明承载位置 | evidence 详设所有者 | open | execution 侧注册表扩展（§5.5）；登记为 evidence 交接 |
| P-EX-8 | EX-\* 稳定码与 sink 形态待 diagnostics 冻结 | diagnostics 详设所有者 | open | 与 P-PR-6/P-DIAG-5 同案 |
| P-EX-9 | 旧版本检查点不迁移（Incompatible 拒绝） | 需求所有者 | open | 维持保守（宁可重算不可错续） |
| P-EX-10 | DETAILED-DESIGN execution 行状态更新＋README 指向修正 | 详设目录维护者 | open | 随 EX-T01/EX-T10 消账（文档同步任务） |

### 1.8 diagnostics（9 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-DIAG-1 | core v0.1 Draft 契约基线漂移 | core 详设所有者 | open | core 冻结时出 diff 清单 |
| P-DIAG-2 | ARCH Draft 待评审（同源：O-03） | 架构所有者 | open | 评审后增量修订 |
| P-DIAG-3 | 分类/严重度/actionKind 实现承载词表 | 需求所有者＋ui/reporting | open | 逐行上游锚点；需求侧如需冻结走需求变更 |
| P-DIAG-4 | FindingId `fnd-` tag 收编 core | core 详设所有者 | open | 收编前 diagnostics 自持解析（句法一致）；与 P-RPT-3（`rpt-`）同案 |
| P-DIAG-5 | sink 接口统一（project/execution 单侧定义） | project/execution 详设所有者＋架构所有者 | open | 建议方向已给（diagnostics 冻结标准接口）；phase-one §2.1 确认未裁决 |
| P-DIAG-6 | ConfirmableFinding 超时语义（默认无限期） | 需求所有者 | open | 状态机已预留；启用＝需求变更 |
| P-DIAG-7 | 日志轮转/容量工程默认 | 构建负责人（WP-23） | open | WP-23 性能验收时校准 |
| P-DIAG-8 | diagnostics 入边与 execution 链接启用时机 | execution 详设所有者＋构建负责人 | open | 契约测试以桩验证；EX-T01 后补双库集成冒烟 |
| P-DIAG-9 | 文案本地化策略（键/值分离） | 需求所有者＋ui 详设 | open | 键体系已冻结（持久化契约）；R1 语言范围待需求侧确认 |

### 1.9 io（7 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-IO-1 | io↔project/io↔runtime 边未登记（同源：O-09/P-PR-5） | 架构所有者 | open | io 侧已免依赖（IO-D02 注入先行）；与 P-PR-5/P-RPT-1 同案合并裁决 |
| P-IO-2 | ResourceBytes 生命周期（=P-RT-6，同源：O-17） | io 详设所有者 | **closed** | io §8.6 五条裁决（2026-09-10）；实现测试待 IO-T05/06 |
| P-IO-3 | ZIP/XML 库选型（libzip/miniz；expat/pugixml） | WP-11 评审（实现期） | open | IO-T04 保持阻塞条件（phase-one §2.1 确认）；vcpkg 可用性验证后冻结 |
| P-IO-4 | §4.5.1 预算默认数值表冻结 | 架构评审 | open | 数值＝本卡建议默认（Draft） |
| P-IO-5 | CSV 方言标识行 v1 语法（`#rwcsv1`）冻结 | 需求/架构评审 | open | 语法已给全；确认后视为 v1 冻结 |
| P-IO-6 | IO-\* 诊断码建议值收编 | diagnostics 所有者 | open | 随 IO-T02 注册时与 diagnostics 收编确认 |
| P-IO-7 | 目录包文件清单/文件名结构契约 | selection 详设所有者 | open | io 定义校验框架；schema 由 selection 卡注册 |

### 1.10 reporting（9 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-RPT-1 | reporting→io 边未登记（同源：P-IO-1/P-PR-5） | 架构所有者 | open | 本文已免依赖（IReportIoFactory 注入）；同案合并裁决 |
| P-RPT-2 | reporting→runtime 边（注入 IModelSummaryProvider） | 架构所有者＋runtime 卡 | open | schema 已冻结（§9.7）；待架构确认形态 |
| P-RPT-3 | ReportId `rpt-` tag 归 core | core 详设所有者 | open | 与 P-DIAG-4（`fnd-`）同案：core 收编时一并处理 |
| P-RPT-4 | IReportArtifactSink 单侧契约待 project 对齐 | project 详设所有者 | open | project 详设修订时交叉核对 |
| P-RPT-5 | C 级全缺章节的保守处置 | 需求所有者 | open | 维持"宁可拒绝不可虚级" |
| P-RPT-6 | PDF 接口桩预留（=O-25） | reporting 详设所有者 | **closed** | 已裁决不留 PDF 接口桩（附录 B 裁决；IReportRenderer 格式无关）；O-25 同步关闭 |
| P-RPT-7 | 证据包 ZIP 写出通道 | io 详设所有者 | open | IArchiveWriter 注入；建议随 P-IO-1 裁决一并考虑 |
| P-RPT-8 | RPT-\* 稳定码收编 | diagnostics 详设所有者 | **partial** | 码值部分已消账（diagnostics v0.2 §4.5/§4.6 收编 8 项）；注册动作随 RPT-T01 |
| P-RPT-9 | core/evidence Draft 基线漂移（envelope 解码入口缺位） | core/evidence 详设所有者＋本卡 | open | 冻结 diff 后增量修订；解码入口列入交接催办 |

### 1.11 ui（10 项）

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| P-UI-1 | UX-10 七态词表/优先级冻结确认（CF-3，关联 O-24） | ui 详设所有者＋需求所有者 | open | 本文 §6.3 为建议默认 |
| P-UI-2 | 当前性 NotEvaluable 呈现（承接 P-EV-4） | ui＋evidence＋需求侧 | open | 过期＋"无法判定"原因（D-12） |
| P-UI-3 | DrainPolicy 枚举澄清（CF-2） | execution 所有者 | open | 按语义对接 |
| P-UI-4 | principal 采集口径 | project 所有者＋需求侧 | open | 会话启动采集缓存 |
| P-UI-5 | 架构 §3.5 ui→core 行消歧（CF-1） | 架构所有者 | open | 见 CF-1 建议 |
| P-UI-6 | workflow 三方契约（同源：O-26） | workflow 详设所有者 | open | WP-22-T01 启动时谈判 |
| P-UI-7 | 后台排空任务控制开放性 | 需求侧 | open | 保持只读（D-11） |
| P-UI-8 | T_force/T_force2 关闭超时阈值 | L5/execution＋需求侧 | open | 120 s/300 s 可配 |
| P-UI-9 | reporting 预览接口宿主 | reporting 详设所有者 | open | 预览宿主阶段 B |
| P-UI-10 | UI-\* 诊断码表最终登记 | diagnostics 所有者 | open | §3.5 建议值与 diagnostics 码表合并 |

## 2. DTB §4 O-01~O-33 集中登记

> 与 §1 的同源关系在"流转备注"注明（同源＝同一待裁决事实的两处登记，消账联动）。

| 编号 | 主题 | 所有者 | 状态 | 消账留痕/流转备注 |
| --- | --- | --- | --- | --- |
| O-01 | old/ 不存在于仓库根（与 REQUIREMENTS 头表声明不符） | 需求所有者（勘误留痕） | open | 勘误请求登记；从头构建口径不受影响 |
| O-02 | 详设补齐（11/20 已编写，余 9 卡待编写） | 各单元所有者 | open | 持续项 |
| O-03 | ARCHITECTURE v0.11 Draft 待评审 | 架构所有者 | open | 同源：P-AR-2/P-PR-2/P-EV-2/P-RT-2/P-EX-2/P-DIAG-2/P-POL-4 |
| O-04 | PA-1~PA-7 与任务指令口径差（少一项） | —（事实登记） | **closed** | 事实登记无裁决项；按 ARCH 现行 PA-1~7 执行 |
| O-05 | gtest 现状与接入机制 | 构建约定所有者 | **closed** | 已定稿（DTB §5.5＋§4.4 版本登记 gtest@1.18.0）；两卡（P-ENV-2/P-TK-1）已关闭 |
| O-06 | 单元 README 任务卡指向同步 | —（执行项） | **closed** | 2026-09-10 文档级完成 |
| O-07 | drivetrain/dynamics→runtime 边未登记 | 架构所有者＋两卡 | open | WP-17-T01/WP-18-T01 起草时处置 |
| O-08 | P-PR-3 同源登记 | 架构所有者 | open | 见 P-PR-3 |
| O-09 | P-PR-5 同源登记（project→io 边） | 架构所有者 | open | 见 P-PR-5/P-IO-1；阻塞 WP-04-T18 编译形态，不阻塞注入式设计 |
| O-10 | P-POL-2 同源登记（阈值冻结默认） | 需求所有者＋WP-07 | open | 见 P-POL-2 |
| O-11 | P-POL-3 同源登记（惯量比阈值归属） | 需求所有者＋selection 卡 | open | 见 P-POL-3 |
| O-12 | P-POL-8 同源登记（R-4 例外措辞） | 架构所有者 | open | DTB §4.5 预登记行"待确认"；WP-01-T01 验收含 O-12 处置核对 |
| O-13 | P-EV-3/P-EV-7/P-EV-8 同源登记 | 需求所有者 | open | 见对应 P 项 |
| O-14 | P-EV-9 同源登记（必验工况 schema） | requirements 卡 | open | WP-14-T01 冻结后回接 |
| O-15 | P-RT-3 同源登记（L2 合法 L1 依赖集） | 架构所有者 | open | 见 P-RT-3 |
| O-16 | P-RT-4 同源登记（安装预设轴向） | modeling 卡 | open | 见 P-RT-4 |
| O-17 | ResourceBytes 生命周期（=P-RT-6/P-IO-2） | io 详设所有者 | **closed** | 设计级关闭（io §8.6）；实现测试待 IO-T05/06 |
| O-18 | P-RT-7 同源登记（病态阈值归属） | 需求所有者＋policy 卡 | open | 见 P-RT-7 |
| O-19 | P-PR-8 同源登记（分支 label） | 需求所有者 | open | 见 P-PR-8 |
| O-20 | P-POL-5 同源登记（碰撞后端版本基线） | 版本基线所有者（WP-24） | open | 见 P-POL-5 |
| O-21 | P-TK-4 同源登记（testkit 边） | 架构所有者 | **closed** | 2026-09-10 消账（WP-01-T01 验收含 O-21 消账项：ARCH §3.5 补登＋门禁白名单收录） |
| O-22 | P-TK-5 同源登记（大数据集策略） | 构建负责人＋WP-02 | open | 见 P-TK-5 |
| O-23 | P-TK-7 同源登记（lint 字典维护） | 需求所有者＋WP-02 | open | 见 P-TK-7 |
| O-24 | P-D-1 同源登记（四词表跨卡确认） | execution/ui 卡 | open | 见 P-D-1 |
| O-25 | reporting PDF 接口桩（=P-RPT-6） | reporting 详设所有者 | **closed** | 设计级关闭（不留接口桩） |
| O-26 | UX-13 碰撞检查命令三方契约（关联 P-UI-6） | workflow 卡＋ui 卡 | open | WP-22-T12 落地 |
| O-27 | P-03 七轴模板工程数值 | WP-13-T07 | open | 附录 C 未冻结；模板启用前冻结 |
| O-28 | P-04 扰动/鲁棒性协议延期 | WP-21-T01 | **deferred** | 正式延期至 OPT-D 启用前（v1.13 决议）；硬前置 |
| O-29 | P-06 轨迹碰撞复检协议数值 | WP-16-T02 | open | 阶段 C 启用前硬前置；附录 C 未冻结 |
| O-30 | P-02 需求追踪表格式契约 | WP-00-T02 | open | 本流水线队列在管（下一任务 WP-00-T02） |
| O-31 | ui.md 依赖声明三方矛盾（§2.1.2 C 表 vs §3.1 vs ARCH §3.5） | 架构所有者＋ui 卡 | open | WP-10 落位前必须裁决 |
| O-32 | PILOT/DEL 四条 P0/R1 需求无架构 ID 级落点 | 架构所有者 | open | WP-22/WP-24 任务细化前补登 |
| O-33 | WP-02-T11 ≙TK-T11 映射悬空 | testkit 卡 | open | 触发条件满足时补登 TK-T11 并恢复映射 |

## 3. 逐卡核对零丢失矩阵（acceptance 第 1 条）

| 卡 | 待裁决节 | 提取项 | 数量 | 核对结论 |
| --- | --- | --- | ---: | --- |
| core | §10.2 | P-AR-1~3、P-D-1、P-ENV-1~2 | 6 | ✔ 全量登记（§1.1） |
| testkit | §12.3 | P-TK-1~7 | 7 | ✔ 全量登记（§1.2） |
| evidence | §15.3 | P-EV-1~9 | 9 | ✔ 全量登记（§1.3） |
| runtime | §15.3 | P-RT-1~9 | 9 | ✔ 全量登记（§1.4） |
| policy | §15.3 | P-POL-1~9 | 9 | ✔ 全量登记（§1.5） |
| project | §15.3 | P-PR-1~8 | 8 | ✔ 全量登记（§1.6） |
| execution | §15.3 | P-EX-1~10 | 10 | ✔ 全量登记（§1.7） |
| diagnostics | §14.3 | P-DIAG-1~9 | 9 | ✔ 全量登记（§1.8） |
| io | §15.3 | P-IO-1~7 | 7 | ✔ 全量登记（§1.9） |
| reporting | §14.3 | P-RPT-1~9 | 9 | ✔ 全量登记（§1.10） |
| ui | §16.4 | P-UI-1~10 | 10 | ✔ 全量登记（§1.11） |
| DTB §4 | §4.1~§4.3 | O-01~O-33 | 33 | ✔ 全量登记（§2） |

核对方法：逐卡定位"待裁决"登记节、提取表格**登记行**（非正文提及；如 reporting 的 RP-COV/RP-CONS 为验证矩阵 ID，不在待裁决体系）；同源项（同一事实两处登记）双侧注明互链。

## 4. 状态汇总与维护规则

- 汇总（2026-09-10）：P-\* 93 项——closed 9（P-ENV-1/P-ENV-2/P-TK-1/P-POL-1/P-RT-6/P-IO-2/P-RPT-6/P-RT-9/P-POL-9；其中 P-RT-6 与 P-IO-2 为同案双登记，去重后 8 案）、partial 5（P-TK-3/P-PR-4/P-EX-5/P-POL-8/P-RPT-8）、stale 2（P-EV-6/P-POL-6）、open 77；O-\* 33 项——closed 6（O-04/O-05/O-06/O-17/O-21/O-25）、deferred 1（O-28）、open 26。
- 维护规则：①各卡/DTB 增量修订消账后，其任务提交或紧随治理提交同步翻转本表对应行（留痕＝消账文档版本/验收记录/findings 编号）；②本表不新增待裁决项——新待裁决一律先登记到所属卡/DTB §4，再在本表补行（保持权威方向）；③发现本表与卡/DTB 不一致：以卡/DTB 为准回改本表并在提交信息注明。

## 5. 与 phase-one-readiness.md 状态口径的一致性核对（acceptance 第 3 条）

| phase-one §2 断言 | 本表对应 | 结论 |
| --- | --- | --- |
| P-RT-6/O-17 设计级关闭（io §8.6 裁决） | §1.4/§1.9/§2 状态 closed | ✔ 一致 |
| O-25/P-RPT-6 已裁决不留 PDF 桩 | §1.10/§2 状态 closed | ✔ 一致 |
| P-PR-6/P-EX-8/P-DIAG-5 sink 统一未裁决（"不因卡片存在而自动消账"） | §1.6/§1.7/§1.8 三项 open | ✔ 一致 |
| P-PR-5/P-IO-1/P-RPT-1~3 注入先行、交接项保留 | §1.6/§1.9/§1.10 open＋备注 | ✔ 一致 |
| P-IO-3 选型待 vcpkg 验证（IO-T04 阻塞） | §1.9 open | ✔ 一致 |
| O-03 ARCH Draft 未 Accepted | §2 open | ✔ 一致 |
| project 文档完整性（"只有 §1~§7 已过时"） | P-EV-6/P-POL-6 状态 stale（登记过时待翻转） | ✔ 一致（本表进一步区分"事实已消、卡内未翻转"的 stale 态） |

## 6. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v1.0 | 2026-09-10 | 随 WP-00-T01 建立：11 卡 93 项 P-\*（core 6/testkit 7/evidence 9/runtime 9/policy 9/project 8/execution 10/diagnostics 9/io 7/reporting 9/ui 10）＋O-01~O-33 集中登记；逐卡零丢失矩阵；与 phase-one-readiness §2 口径核对一致；只登记不裁决 |
