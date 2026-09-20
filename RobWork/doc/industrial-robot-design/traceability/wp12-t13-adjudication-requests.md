# 裁决申请：P-RPT-1（reporting→io 消费形态）与 P-RPT-2（reporting→runtime 注入形态）与 P-RPT-3（core 收编 `fnd-`/`rpt-` tag 类型）

| 字段 | 值 |
| --- | --- |
| 文档性质 | **裁决申请（adjudication request）——只提交申请与依据，不构成裁决、不构成合入授权、不修改任何对端契约**。裁决权在文末标注的所有者；裁决结论产出后由 reporting 单元卡（units/reporting.md）按 DTB §5.4 增量修订收编，对端卡（io/runtime/core/diagnostics）各自承接。**申请≠裁决——本申请不私改 ARCHITECTURE.md 与 core 公共头（RPT-T13 契约 forbiddenFiles 红线）；裁决前各任务注入形态维持不变** |
| 提交任务 | RPT-T13（卡内编号——reporting §11 末段治理行；分支 wp12-t13）文档与门禁同步——契约 acceptance 3 规定的流程动作（关联 io P-IO-1） |
| 分支 / 基线 | wp12-t13 / 3012df850b15d61b75e1ce1421d83f0556668a4b |
| 日期 | 2026-09-20 |
| 登记通道 | P-RPT-1＝reporting.md §14.3 P-RPT-1（建议与 P-IO-1/P-PR-5 合并裁决——io.md §15.3 P-IO-1、wp04-t16-adjudication-requests.md 申请一同案）；P-RPT-2＝reporting.md §14.3 P-RPT-2（runtime.md §10.12/§13.2 交接）；P-RPT-3＝reporting.md §14.3 P-RPT-3（diagnostics.md §14.3 P-DIAG-4 同案收编） |
| 需要裁决者 | 申请一：**架构所有者**（P-IO-1 同案）；申请二：**架构所有者＋runtime 详设所有者**（§14.3 P-RPT-2 原文）；申请三：**core 详设所有者**（P-DIAG-4 同） |

---

## 申请一（P-RPT-1）：ARCH §3.5 是否补登 reporting→io 依赖边，或确认注入式消费形态（建议与 P-IO-1/P-PR-5 合并裁决）

### 1. 问题陈述

ARCHITECTURE §3.5 依赖表未登记 reporting→io 边（表外同层边＝构建失败红线），而 RPT-02（HTML＋JSON/CSV 三格式）与 AT-22（多格式逐字段一致）要求 reporting 消费 io 的 canonical 写出设施（ICsvWriter/IJsonWriter/IAtomicFileWriter——io.md §10.10 已把 reporting 列为消费方，§13.1 登记对应义务）。报告渲染/导出/证据包组装的**编译期依赖形态**必须先有裁决：**补登边**（编译链接）或**确认注入式为正式形态**（reporting 定义窄接口、L5 装配注入 io 实现，零编译边）。阶段 A 已按注入式先行交付（接口契约与裁决结果无关——D-14 双形态兼容），本申请不阻塞任何在途任务，裁决影响的是阶段 B/C 的 L5 装配终态与 R-8 登记的"装配重复"收口时机。

### 2. 依据（磁盘实测，2026-09-20）

| # | 事实 | 出处 |
| --- | --- | --- |
| 1 | §3.5 依赖表 reporting 行实测为"`reporting → core, evidence, diagnostics, project`"，无 reporting→io 边；门禁实施行"上表之外的同层边（含反向边与新边）＝未登记依赖，构建失败" | ARCHITECTURE.md §3.5（v0.12，仍 `Draft` 待评审） |
| 2 | io 侧已把 reporting 登记为消费方：reporting（RPT-02/AT-22）消费 ICsvWriter/IJsonWriter（canonical 确定性＝多格式逐字段一致基础）、IAtomicFileWriter（导出失败保留先前输出/可重试）；io §13.1 交接行同登记 | units/io.md §10.10、§13.1（v0.9，`Draft`） |
| 3 | io 侧裁决建议已登记："**io 侧已免依赖**：注入式（IO-D02）先行可用、接口契约与裁决无关；建议架构统一处置：a) 补登 project→io（接口依赖，无环）＋确认 runtime 注入为正式形态（本文推荐）；或 b) 全注入式"；io↔project/io↔runtime 注入行已逐条落卡（§3.1/§8） | units/io.md §15.3 P-IO-1（架构所有者，P-PR-5 同案合并裁决） |
| 4 | project 侧同案申请已提交：P-PR-5（project→io 消费形态）裁决申请见 wp04-t16-adjudication-requests.md 申请一（O-09 处置条款随附——WP-04-T17/T18 回填路径） | traceability/wp04-t16-adjudication-requests.md（2026-09-17） |
| 5 | reporting 侧注入式先行已全量落地：IReportIoFactory/IReportCsvWriter/IReportJsonWriter/IReportOutputTarget/ReplacePolicy/ReportCsvCell/ReportJsonDom（暂载 Render.hpp——RPT-T06，偏差登记 §14.4 v0.9）＋IReportCsvReader/IReportCsvReaderFactory CSV 回读缝（RPT-T07，v0.10）＋IReportOutputTarget::write 字节通道（v0.9）＋IArchiveWriter 证据包 ZIP 注入契约（RPT-T10，v0.13——P-RPT-7 关联）；工厂零行为（NFR-MNT-04 非转发包装器）、公共头零 io 类型由三个契约测试源码扫描常驻自证（Render/Consistency/Archive CrossUnitContractTest） | units/reporting.md §3.3、§9.5、§14.4 v0.9/v0.10/v0.12/v0.13 |
| 6 | 阶段 A 实现任务 RPT-T01~T12 全部 done（RPT-T13 为本治理收尾任务）：`sdurws_ird_reporting` 零 io/runtime/execution 编译边（配置期红线守卫＋LinkageContractTest 钉住），两测试目标两模式实跑全绿（wp12-t12 留痕：集成模式 208＋61 例） | units/reporting.md §14.4 v0.4~v0.15；traceability/builds/wp12-t01~t12/ |
| 7 | 层次合法性两侧均成立：reporting（L3）、io（L3）为同层——同层边须逐条登记方可编译链接；无环成立：io→core,diagnostics，无 io→reporting 回边；注入式不产生编译边，天然合规 | ARCHITECTURE.md §2.3 分层规则、§3.5 门禁实施行 |

### 3. 影响面

- **阶段 A**：零影响（已收口——reporting 目标零 io 编译依赖为已验收事实；公共头零 io 类型被契约测试源码扫描钉住）。
- **阶段 B/C**：L5 装配终态（IReportIoFactory 适配器约十行/方法的装配重复——R-8）与 WP-12-T14~T16（B/C 级章节、变体、往返复算）的 CMake/装配编写引用本裁决结论；不裁决则维持注入式显式登记形态（可运行、可验收，无阻塞面）。
- **架构文档**：方案一需 ARCH §3.5 增量修订（reporting 行加"io"一词，唯一改动点）＋dependency-graph.json 门禁白名单同步；方案二零架构修订。
- **对端**：方案一落地后 reporting 侧直连为**签名零改动**切换（D-14 预留），io 侧零改动；io.md P-IO-1 的 a)/b) 统一处置建议可一并覆盖 reporting 行（三案同批裁决避免三次漂移——依据 #3/#4/#5）。

### 4. 备选方案（并列提交，裁决权在架构所有者，本申请不排序背书）

| 方案 | 内容 | 代价与收益 |
| --- | --- | --- |
| ①补登编译边 | ARCH §3.5 reporting 行修订为"`reporting → core, evidence, diagnostics, project, io`"（接口依赖；无环——依据 #7）；reporting 目标对 `RWS::ird::io` 增加链接，L5 装配裁撤 IReportIoFactory 适配层（D-14 双形态兼容——签名零改动直连） | 收益：消费面直连、装配重复归零（R-8 消账）；代价：架构文档修订＋dependency-graph.json 白名单同步，且与 io 侧"注入式先行"已交付形态产生一次对齐成本 |
| ②确认注入形态 | 维持 ARCH §3.5 不变；IReportIoFactory 族（§9.5）转正为终态：L5 装配注入 io 实现，接口契约已冻结于 reporting 卡（§3.3 边界价值三点——隔离未登记边/约束 canonical 唯一路径/SA-12 实施件） | 收益：零架构修订、零编译边、与阶段 A 已交付实现零冲突；代价：L5 适配器数十行长期存在（R-8 维持登记） |

### 5. 合并裁决建议（关联 io P-IO-1，O-09 型回填条款随附）

本申请建议与 **P-IO-1（io↔project/io↔runtime）及 P-PR-5（project→io，wp04-t16 申请一）三案同批裁决**（io.md §15.3 P-IO-1 原文"建议架构统一处置"——依据 #3）：L3 平台服务间 io 协作形态一次定案（补登哪几条边、确认哪几处注入为正式形态），避免 reporting/project/io 三卡分别裁决产生形态漂移。随附回填条款：

- 裁决为方案①（补登边）：reporting 卡 §3.3/§14.3 增量修订收编结论，`sdurws_ird_reporting` 链接面与 dependency-graph.json 随后继治理任务同步（本申请不代行——cmake/ 与 ARCHITECTURE.md 均不在 RPT-T13 allowedFiles）；
- 裁决为方案②（注入式）：reporting 卡 §14.3 P-RPT-1 就地消账（注入转正），R-8 转长期登记；
- 裁决前：reporting 维持注入式显式登记形态（阶段 A 已交付面），不预建任何编译边消费。

### 6. 需要裁决者

**架构所有者**（reporting.md §14.3 P-RPT-1 原文"架构所有者（P-IO-1 同案）"）。io 详设所有者/project 详设所有者无裁决义务，但本申请已附两侧立场（依据 #3/#4/#5）供交叉核对。

---

## 申请二（P-RPT-2）：IModelSummaryProvider 注入是否确认为正式形态，或补登 reporting→runtime 边

### 1. 问题陈述

ARCHITECTURE §3.5 未登记 reporting→runtime 边，而 runtime §10.12 已按"reporting 调用 runtime 只读摘要"设计专节（调用方/失败处理/禁反向三行齐备）且其 §13.2 要求 reporting 冻结"RPT-01 输入章节摘要 schema"。reporting 侧已按注入式交付：IModelSummaryProvider（§9.7）＋ModelSummary 投影 schema（RPT-T12 冻结落地）。未裁决项＝**该注入是否确认为正式形态**（架构登记口径），或改为**补登 reporting→runtime 编译边**（届时 runtime 需提供公共摘要入口接口）。裁决前 trySummary 签名冻结不变（契约测试 static_assert 常驻自证——§14.4 v0.15）。

### 2. 依据（磁盘实测，2026-09-20）

| # | 事实 | 出处 |
| --- | --- | --- |
| 1 | §3.5 依赖表无 reporting→runtime 边（reporting 行仅 core/evidence/diagnostics/project——同申请一依据 #1） | ARCHITECTURE.md §3.5（v0.12，`Draft` 待评审） |
| 2 | runtime 侧专节已按只读摘要设计："reporting **调用** runtime 只读摘要（快照身份块、能力声明、资源清单——RPT-01 输入摘要章节数据源）……不含 RobWork 对象""报告引用的快照已释放→reporting 侧以归档的快照身份元数据呈现""reporting 不触发编译、不直接读 WC/DWC" | units/runtime.md §10.12（v0.15，`Draft-Structured`） |
| 3 | runtime §13.2 交接行："reporting｜快照身份块/能力/资源清单的只读摘要入口｜摘要 schema（RPT-01 输入章节）"——schema 冻结义务的对端凭据要求 | units/runtime.md §13.2 |
| 4 | reporting 侧 schema 已冻结并落地：ModelSummary.hpp（§9.7 逐字段——四块 ContentIdentity＋链型/能力/资源/配置投影＋IModelSummaryProvider::trySummary 签名 `std::optional<ModelSummary>(core::RevisionId) const` 冻结）；**schema-freeze-note.md 即 runtime §13.2 交接凭据**（后置三支/在案全字段/闭包外 nullopt 三语义逐条留痕） | units/reporting.md §9.7、§14.4 v0.15；traceability/builds/wp12-t12/schema-freeze-note.md |
| 5 | 注入面零 runtime/execution 编译边：公共头零 runtime/execution/io include 与命名空间限定名扫描由契约测试常驻自证（ModelSummaryTaskStatusCrossUnitContractTest——SA-10 表外边红线） | units/reporting.md §14.4 v0.15；reporting/contract_test/ |
| 6 | 同构先例：evidence §3.3（IObjectBytesSource/IRevisionClosureSource）、io IO-D02（IoRuntime 注入）均为"未登记边以注入式承载、接口契约与补边结果无关"的同模式交付 | units/evidence.md §3.3、units/io.md IO-D02 |

### 3. 影响面

- **阶段 A**：零影响（已收口——schema 冻结＋签名冻结为已验收事实，runtime 消费凭据已留痕）。
- **阶段 B/C**：L5 装配适配器（IRuntimeModelView→IModelSummaryProvider，适配要点已登记于 ModelSummary.hpp 头内建议节）是否裁撤取决于裁决；WP-12-T14~T16 的输入摘要章节装配引用本结论。
- **runtime 侧**：方案二（补边）要求 runtime 把 §10.12 摘要入口公共化（新增公共接口——runtime 卡增量修订）；方案一零 runtime 改动。

### 4. 备选方案（并列提交，裁决权在登记的所有者，本申请不排序背书）

| 方案 | 内容 | 代价与收益 |
| --- | --- | --- |
| ①确认注入为正式形态 | 架构所有者确认登记（可在 ARCH §3.5"运行时注入"注记行补一笔，或仅在两卡 §14.3 消账）；runtime 卡 §13.2 行注记"schema 已冻结（reporting.md §9.7＋wp12-t12/schema-freeze-note.md），L5 适配" | 收益：零编译边、与阶段 A 交付零冲突、与 evidence/io 注入先例同构（依据 #6）；代价：L5 适配器长期存在 |
| ②补登编译边 | ARCH §3.5 reporting 行增"runtime"（接口依赖；无环——runtime→core，无 reporting 回边）；runtime 提供公共摘要入口接口，reporting 直连（IModelSummaryProvider 签名即为该入口形状，切换签名零改动） | 收益：直连无适配层；代价：ARCH 修订＋runtime 公共接口新增与卡增量修订＋装配层二次对齐 |

### 5. 需要裁决者

**架构所有者＋runtime 详设所有者**（reporting.md §14.3 P-RPT-2 原文）。裁决前 trySummary 签名与 ModelSummary schema 维持冻结不变（P-RPT-2 处置注记——§14.4 v0.15）。

---

## 申请三（P-RPT-3）：core 收编 `fnd-`/`rpt-` tag 身份类型（P-DIAG-4 同案）

### 1. 问题陈述

ReportId（规范文本 `rpt-<32hex>`）为 reporting 层自建身份类型（core.md §4.1 六类型无 ReportId；core Id128 体系冻结 7 tag：obj/prj/brn/rev/run/evt/att——无 `rpt-`）。diagnostics 侧 FindingId（`fnd-<32hex>`）同案在册（P-DIAG-4，2026-09-15 DIAG-T11 状态同步"仍 open（core 未收编）……与 P-RPT-3（`rpt-`）同案收编路径不变"）。未裁决项＝core 是否在下次修订**一并收编 `fnd-`/`rpt-` 两 tag**（新增独立类型）。收编前两单元自持解析维持（句法与 core Id128 严格一致，契约测试钉住）。

### 2. 依据（磁盘实测，2026-09-20）

| # | 事实 | 出处 |
| --- | --- | --- |
| 1 | core Id128 冻结 7 tag 无 `fnd-`/`rpt-`；core.md 实测 v0.11（`Draft-Structured`，未冻结）全文零 `fnd-`/`rpt-` | units/core.md §4.1（v0.11）；diagnostics.md §14.3 P-DIAG-4 状态同步 |
| 2 | reporting 侧自建已落地：Identity.hpp ReportId（`rpt-<32hex>` Id128 形自建——不调用 core 头内 detail 非公共契约层、零 core 修改；tryFromCanonical 非抛出双轨；句法契约用例钉住） | units/reporting.md §4.1、§14.4 v0.5；reporting/include/sdurws/ird/reporting/Identity.hpp |
| 3 | diagnostics 侧同案在册且已交付自持解析："FindingId……diagnostics 层类型，待 core 收编登记 P-DIAG-4"；D-05"与 core Id128 同构但 tag 未在 core 冻结集内……不私自改 core"；P-DIAG-4 建议列"建议 core 下次修订收编 `fnd-` tag（影响面小：新增独立类型）；收编前本文自持解析（句法与 core 严格一致）" | units/diagnostics.md §5.3、§14.1 D-05、§14.3 P-DIAG-4 |
| 4 | 影响（两案共同）：跨单元规范文本解析无统一入口——core/reporting/diagnostics 三处各自持解析实现（句法一致由各自契约测试保证）；core 若收编则两类型签名变更（P-DIAG-4 影响列原文） | units/diagnostics.md §14.3 P-DIAG-4；units/reporting.md §14.3 P-RPT-3 |
| 5 | 上游基线事实：reporting/diagnostics 消费的 core 契约均以 core.md v0.1 Draft 签名为基线（P-RPT-9/P-DIAG-1 登记）——core 未冻结，收编属其"下次修订"自然窗口，无预先变更压力 | units/reporting.md §14.3 P-RPT-9；units/diagnostics.md §14.3 P-DIAG-1 |

### 3. 影响面

- **收编前**：零阻塞——两单元自持解析已交付且行为可预测（同构句法、契约测试钉住）；无跨单元运行时互解析场景（报告诊断引用经 DiagRefEntry 承载 core::DiagnosticRecord，不经 ReportId/FindingId 互转）。
- **收编时**：core 新增两独立类型（影响面小——P-DIAG-4 建议列口径）；reporting/diagnostics 各出一次卡内增量修订收编（切换至 core 类型或别名），实现侧切换为签名级小改。
- **不收编**：三处自持解析长期并存（同 P-PR-6 三接口并存漂移风险同型），建议至少登记为长期决策。

### 4. 备选方案（并列提交，裁决权在 core 详设所有者，本申请不排序背书）

| 方案 | 内容 | 代价与收益 |
| --- | --- | --- |
| ①一并收编 | core 下次修订在 §4.1 Id128 tag 冻结集增补 `fnd-`（FindingId）与 `rpt-`（ReportId）两 tag——新增独立类型，句法/解析沿用 core Id128 约定单点 | 收益：规范文本解析统一入口、三处自持实现可裁撤；代价：core 修订＋两消费卡增量修订＋实现侧签名级切换 |
| ②维持层自建 | core 不收编；reporting/diagnostics 各自持类型（现状转正为终态），两卡 §14.3 就地消账登记为长期决策 | 收益：零 core 修订、消费面零改动；代价：句法一致无机器单点保证（靠三处契约测试各自钉住）长期并存 |

### 5. 需要裁决者

**core 详设所有者**（reporting.md §14.3 P-RPT-3 原文"core 详设所有者"；diagnostics.md §14.3 P-DIAG-4 同）。建议两案同批裁决（P-DIAG-4 状态同步原文"同案收编路径"）。

---

## 附：提交时 P-RPT-1～9 状态快照（与 units/reporting.md §14.3 RPT-T13 状态同步一致，2026-09-20）

| 项 | 状态 | 说明 |
| --- | --- | --- |
| P-RPT-1 | **裁决申请已提交** | 即本文件申请一（建议与 P-IO-1/P-PR-5 三案同批裁决）；实现侧注入形态已全量落地且零阻塞，裁决前注入形态维持不变 |
| P-RPT-2 | **裁决申请已提交** | 即本文件申请二；schema 已冻结落地（wp12-t12/schema-freeze-note.md＝runtime §13.2 交接凭据），trySummary 签名冻结不变 |
| P-RPT-3 | **裁决申请已提交** | 即本文件申请三（与 P-DIAG-4 同案收编 `fnd-`/`rpt-`）；reporting 自建 `rpt-` 已落地、句法与 core Id128 严格一致 |
| P-RPT-4 | 仍未决（对端未定义） | reporting 侧 IReportArtifactSink 契约已单侧冻结并落地（Archive.hpp——RPT-T09）；project.md 实测 v0.18 §13.2 交接行在册、reports/ 写入端口随其阶段 B 任务——project 详设修订时以 reporting.md §9.6 为起点交叉核对 |
| P-RPT-5 | 仍未决（需求所有者未裁决） | 保守处置已逐字落位（RPT-SCOPE-INSUFFICIENT 诊断＋ScopeInsufficient 拒绝路径——RPT-T04/T05），裁决前行为可预测；如需"全缺仍生成 C 骨架"语义走需求变更 |
| P-RPT-6 | **已裁决**（O-25 消账，维持） | 不留 PDF 接口桩——实现侧零 PDF 词位/字段自证（RPT-T06，v0.9）；DTB §4 O-25 行实测"已关闭（设计级）" |
| P-RPT-7 | 仍未决（io 详设所有者） | reporting 侧 IArchiveWriter 注入契约已冻结落地（Bundle.hpp——RPT-T10，零第二套 ZIP 实现源码扫描常驻自证）；io.md 实测 v0.9 通用 ZIP 写出公共接口仍未暴露——建议随 P-IO-1 裁决一并考虑 |
| P-RPT-8 | **已消账**（码值部分，维持） | 2026-09-10 diagnostics.md §4.5/§4.6 收编全量 8 项；RPT-T02 注册按收编表登记（v0.5——StableCodeRegistry 逐码一致性契约测试常驻自证） |
| P-RPT-9 | 仍未决（冻结未发生） | core.md 实测 v0.11（Draft-Structured）、evidence.md 实测 v1.3（Draft-Structured）、ARCHITECTURE 实测 v0.12（Draft 待评审）——v0.1 签名基线维持；消费面由契约测试 static_assert/类型恒等断言常驻钉住；envelope 解码入口缺位（R-3）仍由 evidence/execution 交接催办（IReportResultSource 注入隔离已落地——v0.8） |
