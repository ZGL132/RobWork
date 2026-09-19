# 状态更新申请：DETAILED-DESIGN.md execution 行（P-EX-10 文档索引消账）

| 字段 | 值 |
| --- | --- |
| 文档性质 | **状态更新申请（status update request）——只提交核对事实与申请，不构成裁决、不构成索引语义变更授权、不私改共享索引**。索引行处置权在文末标注的详设目录维护者；裁决后由其（或经其确认的增量修订）收编 |
| 提交任务 | EX-T10 文档与门禁同步（execution 单元卡内治理任务，契约 `tasks/foundation/EX-T10.json` acceptance 3 规定的申请动作） |
| 分支 / 基线 | wp08-t10 / ddf2031f85fac89e9c0042a0bba2f4c8f1cf882a |
| 日期 | 2026-09-19 |
| 上游登记 | units/execution.md §15.3 **P-EX-10**（〔EX-T10 状态同步 2026-09-19〕已消账登记——消账依据即本申请 §1/§2；裁决者＝详设目录维护者）；units/execution.md §1.2（DETAILED-DESIGN 行实测与目标状态预登记） |
| 需要裁决者 | **详设目录维护者**（P-EX-10 登记口径："详设目录维护者（本文所有者执行）"——execution 侧执行动作已由本任务完成，索引侧收编待维护者裁决） |

---

## §1 核对事实（2026-09-19 磁盘实测）

| # | 事实 | 出处 |
| --- | --- | --- |
| 1 | `DETAILED-DESIGN.md`"20 单元状态"表 execution 行（第 30 行）实测为"**Draft / 已有任务卡**"——与 units/execution.md §1.2 预登记的目标状态（"本文产出后该行状态应由『待产出』更新为『Draft / 已有任务卡』"）**一致**：原 P-EX-10 登记的"待产出"半区已于 2026-09-10 首批详设索引同步落位（DTB v0.3／DOC-T02 索引同步期） | DETAILED-DESIGN.md 第 30 行；units/execution.md §1.2；§15.4 v0.1 同日登记的 README 指向修正 |
| 2 | execution 行三项字段与实际状态逐项一致：类别"平台服务"＝卡头"平台服务，L3"；详设状态"Draft"＝卡头状态行 `Draft`（不自行宣布 Accepted）；主 WP"WP-D、WP-F"＝REQUIREMENTS §3/DTB §0.1 归属 | DETAILED-DESIGN.md 第 30 行；units/execution.md 卡头；DTB §0.1 |
| 3 | `traceability/unit-status.json` execution 条目实测：status=draft、designCompletion=written、masterWps=[WP-D, WP-F]——与第 1~2 条一致；`implementationStatus: "not-verified"` 为 **2026-09-10 快照字段**（全 20 单元均 not-verified，含实现已落地并验收的 project/diagnostics/evidence/runtime/policy 等；该文件自 2026-09-10 DTB v0.3 后零提交） | traceability/unit-status.json（git log 实测仅 8984f4bc 一次写入） |
| 4 | 实现实际进度：EX-T01～T09（≙WP-08-T02～T10）全部落地并验收——`sdurws_ird_execution` STATIC＋12 契约头＋13 翻译单元＋worker 目标；两测试套件 149/149＋38/38（2026-09-19 复跑复证，traceability/builds/wp08-t10/）；单元卡 v0.11（EX-T09 落位登记），EX-T10 后 v0.12 | units/execution.md §15.4 v0.3～v0.12；traceability/builds/wp08-t01～t10/；traceability/acceptance/ |
| 5 | execution README（公共头保留位）任务卡指向 §12 复核零偏差（2026-09-10 修正、EX-T01 复核、EX-T10 再复核） | include/sdurws/ird/execution/README.md（2026-09-19 复核行） |

## §2 申请事项（并列提交，裁决权在详设目录维护者，本申请不代行）

| # | 申请 | 说明 |
| --- | --- | --- |
| ① | **确认 execution 行现状值"Draft / 已有任务卡"与实际状态一致**（P-EX-10 原"待产出"半区消账的事实面） | 行值自 2026-09-10 起即为 §1.2 预登记目标值，无需改动；本申请请求维护者对该一致性予以确认（确认后 P-EX-10 的消账登记在对端无异议地成立） |
| ② | **裁决 unit-status.json 的 implementationStatus 字段是否/何时刷新**（"not-verified"→实际状态，如 implemented/verified 或维持现状口径） | 该字段为 2026-09-10 跨单元快照、20 单元一体，其刷新口径（由谁、以何证据、是否一次性全量刷新）属详设目录索引治理语义，本任务不单方改写 execution 条目以免制造与其他 19 单元的口径分裂；如维护者裁决刷新，建议按单元验收记录（traceability/acceptance/）为证据源统一处置 |
| ③ | **裁决是否需要在"20 单元状态"表增设实现进度标注**（如"实现已落地"列或脚注） | 现表仅承载详设编写状态；实现进度目前以各卡 §15.4 与 builds/ 留痕为准（可检索但不在索引面）。增设与否属索引结构变更，归维护者；不增设则现状（本申请 §1 事实链）即为索引与留痕的衔接口径 |

## §3 提交方声明

- 本申请内容为**事实核对登记与申请**，不改变任何需求/架构语义，不裁决任何未决项；DETAILED-DESIGN.md 与 unit-status.json 本次零改动（git diff 可复核——两文件不在本提交变更集内）。
- units/execution.md §15.3 P-EX-10 已按其登记的处置路径（"随 EX-T01/EX-T10 消账"——EX-T01 完成 README 半区、EX-T10 完成 DETAILED-DESIGN 核对与本申请）登记消账依据与日期；如维护者裁决与本核对事实不符，以维护者裁决为准并回改 execution 卡登记。
- 本申请随 EX-T10 验收请求送验；维护者处置后请在本文件追加处置记录（或于 DETAILED-DESIGN.md 变更记录中回链本文件）。
