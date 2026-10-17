# 设计区 Agent 规则（industrial-robot-design/AGENTS.md）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-10-17，随 Agent-native 治理 Task 2 建立） |
| 用途 | 设计文档链分区的**本地规则入口**：权威关系、最小阅读路径、修订纪律。进入本区工作的 Agent 先读本文件，再按阅读路径取用具体资产 |
| 上游 | 仓库根 `AGENTS.md`（协作/编码/分支红线总入口，冲突时以根规则为准） |
| 下游 | `context/README.md`（资产分层）、`units/<unit>.md`、`tasks/<taskId>.json` |
| 姊妹分区 | 源码区 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md`（代码红线与测试规则）——跨区任务（既改文档又改代码）两个分区规则都读 |

---

## 1. 本区职责与边界

本区承载**需求 → 架构 → 详设 → 任务契约 → 追踪证据**的完整文档链。产品代码规则不在本区（见姊妹分区）；本区 Agent 默认**只读**，修订必须携带任务编号或文档代号前缀提交（见 §4）。

## 2. 权威关系（同一语义多文档出现时，谁说了算）

| 资产 | 权威角色 | 冲突时 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 需求语义**唯一**权威（ID 如 ARC-01、验收标准 AT） | 一切以需求原文为准；拿不准回查条目原文，不凭记忆推断 |
| `ARCHITECTURE.md` | 20 单元组成/分层/端口/决策 SA-xx | 服从 REQUIREMENTS；事实修正走 `[ARCH]` 提交 |
| `units/<unit>.md` | 单元内接口/数据模型/状态机/错误语义 | 服从上层；实现与单元卡偏差按 DTB §5.4 登记（单元卡增量修订），不允许代码先行、文档失实 |
| `development-task-breakdown.md` | 任务拆分（WPnn-Tkk）与构建约定（§5）权威；§4 O-xx 待裁决项本体 | 任务语义以 DTB 任务行为准 |
| `tasks/<taskId>.json` | 任务执行契约（CCP 协议编译，planned→ready） | 契约与 DTB 任务行不一致时停手登记，不自行改写 |
| `traceability/` | 证据库：**不可变历史**，只增不改（PA-2） | 流水线状态唯一权威＝`traceability/pipeline/state.json`（PIPE §0.2），任何治理产物不得复制其状态字段（context/README.md §4 红线） |

发现同一决策在多文档重复表述且不一致：按上表权威序判定，登记 findings 消重——**不自行删除**任何一侧（删除动作须等治理 Task 7 证据索引建成并过链接完整性校验）。

## 3. 最小阅读路径（默认且仅默认）

1. 仓库根 `AGENTS.md` → 2. 本文件 → 3. `context/README.md` + `context/asset-inventory.json`（资产地图）→ 4. 按任务 ID 取 `tasks/<taskId>.json` → 5. 按契约 ownerUnit/需求引用取 `units/<unit>.md` 对应章节与 REQUIREMENTS 对应条目 → 6. 需要流水线状态时读 `traceability/pipeline/state.json` 本体。

**禁止**默认整册加载：REQUIREMENTS.md、ARCHITECTURE.md、development-task-breakdown.md、traceability/ 证据（完整规则与理由见 `context/README.md` §2–§3）。五条任务型阅读路径（核心算法/Qt 功能/持久化/流程门禁/测试故障）见 `context/reading-paths.md`（治理 Task 3 已建）。

## 4. 修订纪律

- **提交前缀**：`[DTB]`/`[units]`/`[ARCH]`/`[governance]`/`[docs]`，语义见根 AGENTS.md §6.2；纯文档修订可在 `redesign-main` 小步提交。
- **根级资产登记**：设计树根下新增文件/目录必须同步登记 `context/asset-inventory.json`，否则 validate-docs.ps1 门禁报错；点前缀工具目录不入清单（context/README.md §6）。
- **PIPE 权威红线**：不在任何新产出（清单、索引、报告）里复制 state.json 的 status/phase/queue 状态字段——需要状态就读本体。
- **不批量移动/删除**：治理 Task 1–3 期间禁止移动或删除旧文档与 traceability 证据（实施计划停止条件）。
