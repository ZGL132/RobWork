# 裁决申请：P-PR-5（project→io 消费形态）与 P-PR-6（IDiagnosticsSink 形态统一）

| 字段 | 值 |
| --- | --- |
| 文档性质 | **裁决申请（adjudication request）——只提交申请与依据，不构成裁决、不构成合入授权、不修改任何对端契约**。裁决权在文末标注的所有者；裁决结论产出后由 project 单元卡（units/project.md）按 DTB §5.4 增量修订收编，对端卡（io/diagnostics/execution）各自承接 |
| 提交任务 | PRJ-T16（≙WP-04-T16）文档与门禁同步——契约 acceptance 3 规定的流程动作 |
| 分支 / 基线 | wp04-t16 / 94a2072ecee79a746f1b00e3a2ab40606caf0617 |
| 日期 | 2026-09-17 |
| 登记通道 | P-PR-5＝DTB §4.2 **O-09**（"登记（阻塞 WP-04-T18 编译形态，不阻塞注入式先行设计）"）；P-PR-6＝project.md §15.3 P-PR-6＋diagnostics.md §14.3 P-DIAG-5（消账路径＝"project/execution 详设修订联动"） |
| 需要裁决者 | 申请一：**架构所有者**；申请二：**diagnostics 详设所有者＋架构所有者**（P-DIAG-5 登记口径） |

---

## 申请一（P-PR-5）：ARCH §3.5 是否补登 project→io 依赖边，或确认注入式消费形态

### 1. 问题陈述

ARCHITECTURE §3.5 依赖表未登记 project→io 边（表外同层边＝构建失败红线），而任务约束与 PM-05（另存为/项目包）、CON-03（外部资源不可变副本）要求 project 消费 io 能力（包 ZIP 编解码、外部源安全读取、SafePath/BudgetGuard、固化协作）。阶段 B 任务 WP-04-T17（包/另存为实体）/WP-04-T18（固化服务实体）落地时，其编译期依赖形态必须先有裁决：**补登边**（编译链接）或**注入式**（project 定义窄接口、L5 装配注入 io 实现，零编译边）。

### 2. 依据（磁盘实测，2026-09-17）

| # | 事实 | 出处 |
| --- | --- | --- |
| 1 | §3.5 依赖表 project 行实测为"`project → core, diagnostics`"，无 project→io 边；门禁口径"上表之外的同层边（含反向边与新边）＝未登记依赖，构建失败" | ARCHITECTURE.md §3.5（v0.12，仍 Draft 待评审——P-PR-2） |
| 2 | project 侧设计预留：固化流程"io 安全读取（SafePath/预算）→前后哈希比对→canonical 字节入对象库"；另存为/包"ZIP 编解码与逐字节还原校验经 io（P-PR-5）"；§10.4 明示"依赖边待裁决（P-PR-5）……否则阶段 B 以 project 定义 IPackageCodec/IResourceReader 接口＋L5 注入 io 实现落地（零编译边）。阶段 A 无 io 依赖" | units/project.md §5.7/§5.8/§10.4 |
| 3 | io 侧承接答复已按注入式设计：登记"io ←（注入）project……**未登记编译边**；P-IO-1（关联 P-PR-5）——本文按注入式设计，**接口不因裁决结果变化**" | units/io.md §3.1（P-IO-1 含对 P-PR-5 的承接答复）、§8 依赖表 |
| 4 | 治理登记："阻塞 WP-04-T18 编译形态，不阻塞注入式先行设计" | development-task-breakdown.md §4.2 O-09 |
| 5 | 阶段 A 实现现状：`sdurws_ird_project` 零 io 编译期消费（卡头构建落位行"零 io 编译期消费——O-09 处置"；LinkageContractTest 钉住单元边），阶段 A 全部 16 项任务已落地并通过验收，不受本裁决影响 | units/project.md §15.4 v0.3～v0.17；traceability/acceptance/PRJ-T* |
| 6 | 层次合法性两侧均成立：project（L3）、io（L3）为同层——同层边须逐条登记方可编译链接；注入式则不产生编译边，天然合规 | ARCHITECTURE.md §2.3 分层规则、§3.5 门禁实施行 |

### 3. 影响面

- **阶段 A**：零影响（已收口，project 目标零 io 依赖为已验收事实）。
- **阶段 B**：WP-04-T17（另存为/包导入导出实体，PM-05）/WP-04-T18（固化服务实体，PM-01/09、CON-03）的骨架与 CMake 编写直接依赖本裁决；不裁决则两任务只能按 kept planned＋blocked 原因登记，或先行注入式显式登记（见 §5 回填条款）。
- **架构文档**：方案一需 ARCH §3.5 增量修订（唯一改动点）；方案二零架构修订。

### 4. 备选方案（并列提交，裁决权在架构所有者，本申请不排序背书）

| 方案 | 内容 | 代价与收益 |
| --- | --- | --- |
| ①补登编译边 | ARCH §3.5 project 行修订为"project → core, diagnostics, io"（接口依赖；无环——io→core,diagnostics，无 io→project 回边），project 目标 PUBLIC/PRIVATE 链接 `RWS::ird::io` | 收益：消费面直连、无适配层；代价：架构文档修订＋门禁白名单同步（dependency-graph.json），且与 io 侧"按注入式设计"的已交付形态产生二次对齐成本 |
| ②确认注入形态 | 维持 ARCH §3.5 不变；project 定义消费窄接口（IPackageCodec/IResourceReader 等，§10.4 已预留命名），L5 装配注入 io 实现；接口先冻结于 project 卡（同 §5.7 固化接口先冻结先例） | 收益：零架构修订、零编译边、与 io 侧 P-IO-1 已交付设计零冲突（"接口不因裁决结果变化"）；代价：L5 适配器数十行＋接口定义一次落位 |

### 5. O-09 处置条款（回填路径，随本申请一并提交确认）

按 DOC-T10 契约（PRJ-T16 编译放行记录，traceability/contract-compile-log.md）编入 acceptance 3 的 O-09 处置条款：阶段 B 包/固化任务 **WP-04-T17/T18 届时按 DOC-T10 契约 O-09 处置条款回填**——

- 裁决为方案①（补登边）：两任务契约修订解除阻塞，正常排期；
- 裁决为方案②（注入式）：两任务契约以"注入式先行显式登记"回填（接口冻结面引用本申请 §4 方案②与 project 卡 §5.7/§5.8/§10.4）；
- 裁决前两任务维持 planned：按 kept planned＋blocked 原因登记（blocked-by＝O-09），不得提前实现编译边消费。

### 6. 需要裁决者

**架构所有者**（O-09 登记口径：补登边或确认注入形态）。io 详设所有者/需求所有者无裁决义务，但本申请已附 io 侧立场（依据 #3）供交叉核对。

---

## 申请二（P-PR-6 余项）：IDiagnosticsSink 名称/归属统一与 project→diagnostics 链接形态

### 1. 问题陈述（范围限定：仅 sink 形态；码值部分已消账不重开）

project §5.0 为消费方单侧定义 `IDiagnosticsSink{report(report), reportDev(channel, message)}`（ARCH §3.5 project→diagnostics 边的实现形态），实现采用注入式先行（适配器注入、**零 diagnostics 编译链接边**，LinkageContractTest 双侧钉住）。diagnostics.md §9.7 已交付标准接口 `IDiagnosticSink`＋兼容实现 `DiagnosticsSinkImpl`（DIAG-T04 落地，§14.4 v0.5），其 report/reportDev 签名与 project §5.0/execution §3.3 **逐字一致**（P-DIAG-5 实测）。未裁决项＝**接口名称/归属统一方式**（diagnostics 定义统一接口、消费方收编别名/适配 vs 各消费方自持＋L5 适配转正）及**project→diagnostics 链接边是否随裁决启用**。码值部分已按 §15.4 v0.2 消账（diagnostics.md §4.6 收编 PRJ-\* 全量 10 项），本申请不涉及任何码值变更。

### 2. 依据（磁盘实测，2026-09-17）

| # | 事实 | 出处 |
| --- | --- | --- |
| 1 | project 单侧定义与实现形态：`IDiagnosticsSink` 定义于 StoreTypes.hpp（§5.0 契约位），全部 PRJ-\* 诊断经注入 sink 产出；构建图零 diagnostics 链接边（PRJ-T03 首个消费点起维持） | units/project.md §5.0/§3.2；§15.4 v0.5/v0.8/v0.12 |
| 2 | diagnostics 侧标准接口已交付且签名逐字一致："本文侧标准接口 IDiagnosticSink＋DiagnosticsSinkImpl 兼容形态已交付（DIAG-T04……report/reportDev 与 project §5.0/execution §3.3 同形签名逐字一致）" | units/diagnostics.md §14.3 P-DIAG-5（DIAG-T11 状态同步）、§14.4 v0.5、§9.7 |
| 3 | 消账路径已由对端登记："消账路径＝project/execution 详设修订联动（各自卡内增量修订时收编统一接口与链接形态），本文不私改对端契约"——本申请即 project 侧联动动作 | units/diagnostics.md §14.3 P-DIAG-5 |
| 4 | 同案登记：execution P-EX-8（IExecutionDiagnosticsSink 统一）与 P-PR-6 同模式，两案裁决宜同批处理避免三次漂移 | units/execution.md §15.2 R-6（"P-EX-8，P-PR-6 同模式"） |
| 5 | ARCH §3.5 project→diagnostics 边已登记（接口依赖），但 project 构建落位行明示"diagnostics 边按注入形态不落链接——P-PR-6/P-EX-8" | ARCHITECTURE.md §3.5；units/project.md 卡头构建落位行 |

### 3. 影响面

- 现行实现（注入式先行）已全绿且被契约测试钉住，裁决前无阻塞面；影响限于：①三处消费方接口（project/execution/io——diagnostics §4.2 O-12）长期并存的漂移风险与装配层适配代码量；②裁决后 project 卡一次性小改（§5.0 收编别名/适配说明＋§3.2 依赖图注记），实现侧增量任务承接代码面。
- 本申请不改 diagnostics.md/execution.md/io.md（对端联动由其卡各自承接——同 DIAG-T11"不私改对端契约"先例）。

### 4. 备选方案（并列提交，裁决权在登记的所有者）

| 方案 | 内容 | 代价与收益 |
| --- | --- | --- |
| ①收编 diagnostics 标准接口 | project 卡 §5.0 修订：`IDiagnosticsSink` 更名/别名至 diagnostics §9.7 `IDiagnosticSink`（或显式继承适配），§3.2 注记统一接口来源；链接边是否随裁决启用（项目目标 PUBLIC 链 diagnostics vs 维持 L5 注入）由架构所有者一并明示 | 收益：消除接口名漂移、装配直连；代价：project 卡增量修订＋实现侧适配增量任务；若启用链接边，LinkageContractTest 白名单与卡头构建落位行同步修订 |
| ②维持消费方单侧定义＋L5 适配转正 | 现状转正为终态：各消费方自持窄接口（D-10 生产窄接口形态），diagnostics DiagnosticsSinkImpl 经 L5 适配接入 | 收益：零修订、消费面不增依赖；代价：三处接口并存的事实长期登记（P-PR-6/P-EX-8 保持 open 或转"已裁决——维持现状"） |

### 5. 需要裁决者

**diagnostics 详设所有者＋架构所有者**（P-DIAG-5 登记口径）；execution 卡 P-EX-8 同案建议同批裁决（依据 #4）。

---

## 附：提交时 P-PR-1～9 状态快照（与 units/project.md §15.3 PRJ-T16 状态同步一致，2026-09-17）

| 项 | 状态 | 说明 |
| --- | --- | --- |
| P-PR-1 | 仍未决 | core.md 实测 v0.11（Draft-Structured）未冻结；零 core 修改基线维持，冻结 diff 后增量修订 |
| P-PR-2 | 仍未决 | ARCHITECTURE 实测 v0.12（Draft）待评审；增量未触及本文锚点 |
| P-PR-3 | 裁决中 | O-08 维持；本文解释已随 PRJ-T10 兑现，等架构评审确认 |
| P-PR-4 | **已消账** | execution.md §9.5 冻结答复与本文逐项一致，本文确认零增量需求（对端 P-EX-5 消账由其卡承接） |
| P-PR-5 | **裁决申请已提交** | 即本文件申请一（O-09 处置联动，不私裁） |
| P-PR-6 | 码值已消账；sink 形态**裁决申请已提交** | 即本文件申请二（P-DIAG-5 联动，不私改对端） |
| P-PR-7 | **已消账** | ui.md §9.2/D-4 交叉核对通过；runtime.md §10.2 承接答复"兼容"，两处差异确认无增量需求（对端 P-RT-8 消账由其卡承接） |
| P-PR-8 | 裁决中 | O-19 维持；PRJ-T06 双重防线已落地，等需求所有者确认口径 |
| P-PR-9 | 仍未决（不在本次 acceptance 点名范围，如实附记） | commandType 语法矛盾，裁决权在 project 详设所有者；PRJ-T10 已按 §4.4.4 字段级契约执行并如实登记 blocked 半区（§15.4 v0.6/v0.12） |
