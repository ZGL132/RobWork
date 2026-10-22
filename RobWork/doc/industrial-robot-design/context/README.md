# 资产分层与默认上下文规则（context/）

> **GOV-T21**：全部新任务执行根 AGENTS.md §0.1 H1—H6；先读设计根 `reuse/policy.md`、`reuse/migration.md`、`reuse/catalog.json`、`reuse/inventory.json`，核对治理独立验收pass后再执行适配/算法任务。历史done不代表复用合规，缺口先提案并经所有者明确批准。


| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-10-17，随 Agent-native 治理 Task 1 建立） |
| 用途 | 定义本设计树（`doc/industrial-robot-design/`）全部资产的**七类分层**与**默认是否进入 Agent 上下文**的规则；配套机器清单见同目录 `asset-inventory.json`（由 validate-docs.ps1 门禁校验） |
| 上游 | 仓库根 `AGENTS.md`（Agent 协作总入口）；实施计划 `docs/superpowers/plans/2026-10-16-agent-native-context-governance-plan.md` |
| 阅读阶梯 | 新会话：根 AGENTS.md → 本树 `AGENTS.md`（分区规则）→ 本文件 → `asset-inventory.json` → 按任务 ID 取 `tasks/` 契约 → 按 ownerUnit 取 `units/<unit>.md` 对应章节 |

---

## 1. 为什么要分层

本设计树已积累 20 个单元卡、356 个任务契约 JSON 与约 6600 个 traceability 证据文件，`development-task-breakdown.md` 超过 400 KB。**问题不是缺规范，而是规范、设计、任务、历史日志与验证证据混在一棵树里，Agent 无法判断当前权威信息与最小必要上下文。** 本目录用两层手段收敛：

1. **本文件**（人读）：七类资产的语义与默认上下文规则；
2. **`asset-inventory.json`**（机读）：每个根级资产与 load-bearing 嵌套资产的登记条目，`validate-docs.ps1` 校验其完整性——设计树根级新增未登记资产即门禁报错。

## 2. 七类资产定义

| 类别 | 语义 | 典型资产 | 默认进上下文 | 变更纪律 |
| --- | --- | --- | --- | --- |
| authority | 语义或状态的**唯一权威**载体（含机器维护的流水线状态） | REQUIREMENTS.md、ARCHITECTURE.md、DETAILED-DESIGN.md、units/*.md、traceability/pipeline/state.json | 否——按任务与阅读路径**章节级取用**，禁止整册加载 | 所有者裁决或按任务卡增量修订；不重定义、不收窄、不扩大 |
| workflow | 执行协议与流转台账 | automation-pipeline.md（PIPE）、acceptance-protocol.md（ACC）、contract-compilation.md（CCP）、governance-log.md、performance-baseline.md、context/（本目录） | 否——参与对应流程时读对应章节 | 协议演进走文档代号前缀提交；台账只登记流转不裁决 |
| task-source | 任务契约来源 | development-task-breakdown.md、tasks/*.json、traceability/phase-one-task-index.json | 否——**按任务 ID 取对应契约**，不通读 | 契约编译按 CCP 协议；修订与任务行同批 |
| generated | 脚本生成物，**禁止手改** | context/packs/（Task 6 已建，试点 CORE-T01）、traceability/evidence-index.json（Task 7 建）；state.json 具机器生成属性但按 authority 语义登记（权威优先，`generated=true` 标记生成方式） | 否——经校验脚本消费 | 只能由对应脚本重建；过期即重跑脚本，不手工修补 |
| evidence | 运行与验收证据 | traceability/ 下验收记录、构建日志、gtest XML、findings 附件等 | 否——**默认不加载**，经 `traceability/evidence-index.json` 摘要索引按任务/类型/生命周期检索（Task 7 已建） | 只增不改（PA-2 不可变历史精神）；删除/移动须过 traceability/README.md §3 三前置 |
| decision | 长期决策记录（Agent Notes） | `RobWork/.agents/notes/`（Task 4 已建：implemented 五条） | 否——按决策主题检索 | Problem/Decision/Alternatives/Consequences/Verification/Related sources 六段式；implemented 不得用计划语气（validate-docs 启发式黑名单＋验收 review 把关） |
| archive | 已被取代的历史资产 | 当前无 | 否 | 任何资产移入 archive 前必须先过 Task 7 证据索引的链接完整性校验 |

## 3. 新会话默认上下文规则（起步集）

新会话接手任务时的**默认且仅默认**加载以下内容，其余资产一律按需章节级取用：

1. 仓库根 `AGENTS.md`（协作与编码总规则）；
2. 本树 `AGENTS.md`（设计区分层规则：权威关系与阅读路径，治理 Task 2 建）；
3. 本文件 + `asset-inventory.json`（资产地图）；
4. 按任务 ID 取 `tasks/<taskId>.json` 契约；
5. 按契约的 ownerUnit / 需求引用取 `units/<unit>.md` 对应章节与 REQUIREMENTS 对应条目；
6. 流水线当前状态读 `traceability/pipeline/state.json` 本体（见 §4）。

**禁止**默认整册加载：REQUIREMENTS.md、ARCHITECTURE.md、development-task-breakdown.md、traceability/ 证据。阅读路径细化（核心算法/Qt 功能/持久化/流程门禁/测试故障五条）见 `reading-paths.md`（治理 Task 3 已建）。规则执法分级审计见 `rule-enforcement.md`（治理原则 1 落地）。

## 4. 与 PIPE 的权威关系（评审修正 3，红线）

- **流水线状态唯一权威 = `traceability/pipeline/state.json`**（schema 见 automation-pipeline.md §0.2，由 `validate-state.ps1` 机器校验）。本清单与后续 context pack **只登记其路径与 schema 出处，不复制 status/phase/queue 等状态字段**——需要状态时读 state.json 本体。
- **findings 唯一登记簿 = `traceability/findings.json`**；待裁决项流转台账 = `governance-log.md`（权威本体在各单元卡与 DTB §4，台账只登记不裁决）。
- 违反方式举例（均禁止）：在 asset-inventory、context pack 或任何新治理产物里写"当前任务状态=implementing"之类的状态快照字段。治理层出现与 state.json 不一致的状态复制品即触发计划停止条件。

## 5. 基线快照（2026-10-17，Task 1 统计）

> 本节是**带日期的快照而非实时数据**，仅供回溯治理起点；实时口径以 validate-docs.ps1 输出为准。

| 指标 | 值 | 统计口径 |
| --- | --- | --- |
| 单元卡 | 20 | `units/*.md`，validate-docs 校验 unit-status 20 个唯一单元 |
| 任务契约 JSON | 357 | `tasks/` 树下全部 JSON（validate-docs 口径 356 个任务卡＋foundation-tasks.json） |
| traceability 文件 | 6625 | `traceability/` 树下全部文件（验收/构建/测试证据＋结构条目） |
| 文档门禁基线 | `validate-docs: PASS (20 units, 12 trace entries, 356 task files)` | 2026-10-17 扩展前版本 |
| 既有盘点底稿 | `audit/file-inventory.txt`（868 行源码树清单）、`audit/full-audit-report.md` | 2026-10 审计产物，本次盘点复用未重建 |

## 6. 登记纪律（新增/变更资产时）

1. **根级全覆盖**：设计树根下新增任何文件或目录，必须同步登记进 `asset-inventory.json`，否则 validate-docs.ps1 报 `unregistered root-level asset` 错误。
2. **嵌套资产默认不登记**，但满足以下条件之一的 load-bearing 条目须显式登记：被门禁脚本机器校验引用（如 requirements-to-units.json）、被跨文档作为权威出处引用（如 state.json、findings.json、phase-one-task-index.json）。
3. **字段口径**：`kind` 固定七类（§2）；`owner` ∈ `owner`（所有者裁决）/`pipeline`（流水线机器维护）/`sessions`（执行会话追加）/`script`（脚本生成）；`defaultContext`/`generated` 为布尔值；`canonicalLinks` 为相对本设计树根的可解析路径（可带 `#锚点`，校验时忽略锚段）。
4. **generated=true 的资产禁止手改**，只能重跑生成脚本。
5. **点前缀条目不入清单**：根级全覆盖检查跳过 `.` 开头的条目——那是工具本地目录（如 `.zcode` 工作流运行产物，自含 .gitignore 自忽略），不是设计资产。若未来某点前缀目录升级为正式资产，先改名再登记。
