# 契约联合评审记录（`IRD-D10-20260829`）

> 评审日期：2026-08-29；检查点：`IRD-D10-20260829`
> 评审对象：`architecture/contract-registry.md` 全部 25 项契约、ADR-001～005、`architecture/symbol-registry.md` 全部公共符号
> 所审版本：本记录所在提交的 `docs/industrial-robot-design/` 全树（git log 可查）；需求基线 v0.8（128 项）；契约 25、符号 76、ADR 5
> 评审结论：**通过；未通过项：无。** §5 签署栏已于 2026-08-30 完成签署，25 项契约、76 个公共符号与 ADR-001～005 整体升 `Accepted`（一次提交完成，不改契约正文，仅状态与签署引用）。
> 状态变更自本记录起生效；此后任何契约语义变更必须走契约变更流程（所有者提出 → ADR/变更记录 → 消费者影响清单 → 版本提升 → 契约测试）。

## 1. 评审依据与签署说明

- **语义闭合前置修复（D10）**：评审前完成并验证了以下修复——任务级循环依赖（WP-15-T08↔WP-20-T08）解除；诊断码注册表逐项裁决（117 码全部登记，未登记 0，category/severity 冲突 0，机器比对验证）；7 个跨模块公共类型补登记并冻结字段；WP-04 与持久化契约的目录/HEAD/manifest 口径统一；WP-12 的 PDF 交付与需求基线对齐（PDF 无需求条目，移出 R1）；任务卡验证命令可执行化。
- **独立验证**：文档门禁（PS 5.1 与 PS 7 双跑）、Schema 门禁（合法示例全过、非法示例全拒）、追踪表重生成一致性、任务级依赖 DAG 无环检查、诊断注册表一致性扫描（0 未登记 / 0 冲突）。
- **签署说明（口径声明）**："按建议修复"的指令授权了本轮评审的**执行**，不等于**签署**。评审结论为"通过（待签署）"：全部契约维持 `Proposed`，待 §5 签署栏由产品/需求所有者、契约所有者、消费模块代表与独立验证者签署后升 `Accepted`。签署前的实施范围按用户 2026-08-29 审计指示执行：仅 `WP-00-T01` 开放（治理任务）。评审中代行的全部裁决在 §3 列出，供签署时追认或否决；否决任何一项即退回对应契约并阻塞其消费者任务。

## 2. 契约评审清单（25 项）

| 契约 | 所有者 | 主要消费者 | 评审关注点与结论 | 结论 |
| --- | --- | --- | --- | --- |
| CTR-DOM-001 | WP-03 | WP-04～25 | 稳定身份与作用域；与 ADR-001 一致 | 通过（待签署） |
| CTR-DOM-002 | WP-03、04 | 全部业务 WP | 聚合所有权；对象头不嵌入修订关联（persistence-schema §2.4） | 通过（待签署） |
| CTR-DOM-003 | WP-03 | 全部计算 WP | SI 单位与有限数；传动映射量纲（SYM-DTM-001）入表 | 通过（待签署） |
| CTR-DOM-004 | WP-03 | WP-05、08、12～23 | 正交状态 2/60 合法组合；`isFormallyFeasible` 消费 `allowedWarningCategories`（ADR-005） | 通过（待签署） |
| CTR-KIN-001 | WP-06 | WP-07、13、15～21 | 规范运动学 12 点冻结；四元数规范符号；q-zero 单偏置 | 通过（待签署） |
| CTR-KIN-002 | WP-06、13 | WP-15～21 | StandardDH/ExplicitJoint 互斥与有损拒绝 | 通过（待签署） |
| CTR-NAM-001 | WP-06 | WP-07、12～23 | `RuntimeNameMap` 双向解析；诊断码 `IRD-NAME-*` 类别已裁决 | 通过（待签署） |
| CTR-POL-001 | WP-07 | WP-05、12、15、16、20、21 | 共享 `CollisionEvaluator` 唯一性；AT-19 跨入口一致性由 WP-20-T08 唯一所有 | 通过（待签署） |
| CTR-PER-001 | WP-04 | WP-11、12、24、25 | `.rwdesign` 目录布局；`objects/<sha256>/` 目录形态与两种成员组合已与 WP-04 对齐 | 通过（待签署） |
| CTR-PER-002 | WP-04 | WP-05、08、12、20、21 | 内容寻址、只追加、`.staging/<transaction-id>/` 原子提交边界已对齐 | 通过（待签署） |
| CTR-PER-003 | WP-04 | 全部持久化消费者 | JSON 规则与版本升级；HEAD JSON 形态已与 WP-04 对齐 | 通过（待签署） |
| CTR-API-001 | WP-04 | 全部业务插件 | 项目命令端口；`IRD-PROJ-*` 类别已裁决 | 通过（待签署） |
| CTR-API-002 | WP-06 | WP-07、13、15～21 | 编译入口与名称解析 | 通过（待签署） |
| CTR-API-003 | WP-05、08 | WP-07、15～21 | 输入切片与 `ResultEnvelope`；`ResolvedIkBranchSequence`/`IkBranchPolicy`/`MotorSideOperatingPoint` 字段已冻结（public-interfaces §7） | 通过（待签署） |
| CTR-API-004 | WP-05 | WP-08、12、15～23 | 结果仓库接纳；`IRD-RESULT-*`/`IRD-EVIDENCE-*` 类别已裁决（System/Engineering 判定） | 通过（待签署） |
| CTR-DIA-001 | WP-09 | 全部模块 | 诊断 13 字段冻结；注册表 117 码 D10 裁决规则入档（diagnostics.md §3） | 通过（待签署） |
| CTR-EXE-001 | WP-08 | 全部评估器和 UI | 9 态 18 转移；`IRD-EXEC-*` 类别已裁决 | 通过（待签署） |
| CTR-EXE-002 | WP-05、08 | WP-12、15～23 | 依赖清单、切片、当前性与缓存键 | 通过（待签署） |
| CTR-EXE-003 | WP-08 | 长任务消费者 | 取消 30s 预算、检查点兼容性（System/Error 已裁决） | 通过（待签署） |
| CTR-EXE-004 | WP-05 | WP-08、12、15～23 | 迟到结果与接纳；BRANCH-MISMATCH=Warning、DUPLICATE-ATTEMPT=Info 裁决 | 通过（待签署） |
| CTR-TST-001 | WP-02、23 | 全部 WP | 测试分层与容差；黄金数据 ird-golden-0.7.1 | 通过（待签署） |
| CTR-TST-002 | WP-02、23 | 全部 WP | GWT、证据字段、独立评审 | 通过（待签署） |
| CTR-REL-001 | WP-00、24 | WP-01～25 | R1/R2 切片；PDF 移出 R1 已同步（需求无 PDF 条目） | 通过（待签署） |
| CTR-OPT-001 | WP-20、21 | WP-05～08、12、13、15～19、22、23 | OPT-B 权威范围；跨入口任务单向依赖已裁决 | 通过（待签署） |
| CTR-OPT-002 | WP-20 | WP-21、05、08 | 设计变量/绑定/向量/补丁与候选稳定 ID | 通过（待签署） |

## 3. 评审中确认的关键裁决（追认清单）

1. **Completed＋Warning 可行性组合**（ADR-005）：Warning 级诊断与工程判定正交；`isFormallyFeasible()` 仅消费 `RequiredEvidenceProfile.allowedWarningCategories`。
2. **诊断 category/severity 裁决规则**（diagnostics.md §3 D10 段）：类别=责任边界（Input/Engineering/System），severity=影响（阻断=Error、降级但结论完整=Warning、幂等 no-op=Info）；117 码逐项按此复核，跨码锚点（逐行拒绝=Error、确证不可行=Warning、no-op=Info）已入档。
3. **AT-19 跨入口唯一所有权**：WP-15-T08 交付运动学侧探针与三元组/夹具约定，WP-20-T08 为集成用例唯一所有者（单向依赖），消除任务级循环。
4. **WP-04 持久化口径**：`objects/<sha256>/` 为目录（`object.json` 或 `payload.bin+meta.json`），HEAD 为 JSON 文档，manifest 字段集以 persistence-schema §2.3 为唯一权威。
5. **PDF 移出 R1**：需求基线无 PDF 条目；不交付 PDF 且不保留 `PdfReportRenderer` 接口桩；未来引入须先过 WP-01 依赖门禁＋ADR。
6. **跨模块类型补登记**：`SessionState`、`EditDraft`、`StageStatusModel`、`SelectionModel`、`ResolvedIkBranchSequence`、`IkBranchPolicy`、`MotorSideOperatingPoint`（SYM-UI-001～004、SYM-TRJ-002/003、SYM-DTM-001）。
7. **D3～D9 期间代行裁决**（约 20 项，详见 DOCUMENT-BASELINE 各检查点行与 ADR-005）：编译输入字段集、sourceFormat/零点语义、`IRD-PERSIST-COMMIT-FAILED` 增补、Continuous 关节排序归一、错误码注册表唯一权威等。

## 4. 遗留义务

- 无阻塞项。签署前全部契约维持 `Proposed`（实施范围按用户 2026-08-29 审计指示：仅 `WP-00-T01` 开放）；契约后续变更一律按 §1 变更流程执行；消费者在实现中发现契约缺口时停止并上报，不得本地解释。
- WP 计划文件中的治理状态行已同步为 `Proposed`（通过，待签署）口径；实现启动顺序按总纲依赖顺序与 `agent-tasks/task-status.md` 任务状态账本执行。

## 5. 签署栏（签署后整体升 `Accepted`）

| 角色 | 姓名/标识 | 签署日期 | 所审版本/提交 | 结论 |
| --- | --- | --- | --- | --- |
| 产品/需求所有者 | 用户 ZGL132（2026-08-30 指令"签署契约与 ADR"） | 2026-08-30 | IRD-D10-20260829 评审内容；签署决定于治理提交 `2dfa6cb` 之后作出 | 通过，追认 §3 全部裁决 |
| 契约所有者（WP-03/04/05/06/07/08/09/20 代表） | 用户 ZGL132（同一签署指令覆盖各契约所有者 WP） | 2026-08-30 | 同上 | 通过，追认 §3 全部裁决 |
| 消费模块代表（WP-10～25 代表） | 用户 ZGL132（同一签署指令覆盖消费模块 WP-10～25） | 2026-08-30 | 同上 | 通过，追认 §3 全部裁决 |
| 独立验证者 | 独立验证者/治理协调（ZCode 治理会话） | 2026-08-30 | 同上；验证依据：WP-00-T01～T04 独立复跑证据（双 shell 门禁、8 夹具、128 项需求六环节链路评审、Schema 门禁） | 通过 |

> 签署留痕（2026-08-30）：本项目治理结构为单一人类权威（用户 ZGL132）＋独立 ZCode 执行上下文。产品/需求所有者、契约所有者与消费模块代表三行的签署由用户 2026-08-30 明确指令"签署契约与 ADR：在注册表和 ADR 文档中将其升为 Accepted，随后治理上下文复判九项门禁并将 WP-01-T01 转 Ready"一次性给出；独立验证者行由治理会话基于 WP-00-T01～T04 独立复跑证据签署。§3 追认清单 7 项裁决随签署全部追认，无否决项；§4 遗留义务无阻塞项。按本节程序，contract-registry、symbol-registry、ADR-001～005 与各契约文件头部状态在同一治理提交中由 `Proposed` 升为 `Accepted`。
