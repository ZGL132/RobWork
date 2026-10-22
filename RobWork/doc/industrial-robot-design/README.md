# 工业机械臂设计软件 · 设计文档树入口（README）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.1（2026-10-22，同步精简入口及最终复用方案；v1.0 于 2026-10-17 建立） |
| 定位 | 设计文档树的人类读者入口：项目定位、阶段快照、入口表、变更速查、最短阅读路径 |
| 读者 | **人类**工程师/新协作者；Agent 以根 AGENTS.md §1 的最小上下文规则进入，按任务定位本树规则和相关章节，不强制通读 context |
| 维护 | 每个治理/合入批次随批小步更新 §2 阶段快照；结构性变更（新入口）须同步 `context/asset-inventory.json` |

## 1. 项目定位

本树承载**工业机械臂设计软件**（industrialrobot）的需求 → 架构 → 详设 → 任务契约 → 追踪证据全链文档。产品代码位于 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`（20 单元），构建在 RobWork 上游框架（SDU 基线，**零源码修改**，SA-02）之上。

## 2. 当前阶段（快照 2026-10-17，非实时）

- 单元详设 **20/20 已编写**：平台内核五单元（core/evidence/policy/runtime/testkit）contract-review；其余 15 单元 Draft（后批六业务域 2026-10-06 收官）。
- 阶段一执行推进中：356 份任务契约（ird-task/1，planned/ready/blocked/done 混合态），自动化流水线（PIPE tick）与三段式"实施 → 对抗式验收 → 所有者合入"循环运转中。
- CI 常驻门禁（GitHub Actions / GitLab 双模板）与本地 `gate-all.ps1` 一键门禁常态化。
- 任务执行态以 `traceability/pipeline/state.json` 为唯一权威；单元详设态以 `traceability/unit-status.json` 为准（本快照仅供参考，冲突时以上述载体为准）。

## 3. 入口表（要做什么 → 从哪进）

| 要做什么 | 入口 |
| --- | --- |
| 查某条需求语义 | `REQUIREMENTS.md` 按 ID 检索条目（勿通读全册） |
| 查某单元接口/状态机 | `units/<unit>.md`（`DETAILED-DESIGN.md` 为索引） |
| 领取/查某任务 | `tasks/<taskId>.json` 契约 ＋ `development-task-breakdown.md` §2 对应任务行 |
| 查流水线当前状态 | `traceability/pipeline/state.json`（唯一权威，schema 见 PIPE §0.2） |
| 查问题/缺陷登记 | `traceability/findings.json`（登记簿）；待裁决项另见 `governance-log.md` |
| 查验收/测试证据 | `traceability/acceptance/`（验收记录）＋ `traceability/gtest-reports/`（统一证据索引随治理 Task 7 建） |
| Agent 协作规则 | 根 AGENTS.md §1/§6 与本树 AGENTS.md；context 工具按需使用 |
| 查规则执法方式 | `context/rule-enforcement.md`（gate/review/behavioral 分级审计表） |
| 查上游复用/权限/迁移 | `ARCHITECTURE.md` §3.6 / SA-19 → `reuse/policy.md`；catalog/逐功能格式与待实现门禁见 `reuse/audit-contract.md`，阶段见 `reuse/migration.md` §11 |

## 4. 变更规则速查

- 提交前缀：`[DTB]` / `[units]` / `[ARCH]` / `[governance]` / `[docs]`（语义见根 AGENTS.md §6.2）；纯文档修订可在 `redesign-main` 小步提交。
- 本树根级新增文件/目录**必须**同步登记 `context/asset-inventory.json`，否则 `validate-docs.ps1` 门禁失败。
- 权威关系与执行规则：根 AGENTS.md 与本树 AGENTS.md；资产分层工具说明按需查 `context/README.md`。
- 上游复用覆盖全部 20 单元及函数/类/接口/工具/测试/构建，不限于 16 个已知重复文件。“从头构建”只排除恢复 old/，不允许重写上游核心；本轮文档治理不表示新增权限/功能证据门禁已实现或验收，不放行适配层/算法替换。

## 5. 最短阅读路径

- **人类新人**：本文件 → `ARCHITECTURE.md` §1–§3（20 单元组成/分层/端口）→ `DETAILED-DESIGN.md` 文档分工 → 所在领域的 `units/<unit>.md` → `development-task-breakdown.md` §0（主 WP 结构）。
- **Agent**：根 AGENTS.md §1 → 定位本任务相关单元/契约与引用章节 → 按影响验证；context 仅作可选工具，不扩大默认阅读集。
