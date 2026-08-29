# 工业机械臂设计软件重构文档

本目录是工业机械臂设计软件重构的唯一权威文档入口。旧插件的阶段性方案、临时设计稿和历史复盘不再作为实施依据；需要追溯时使用 Git 历史。

> 文档治理基线：`IRD-D0-20260829`  
> 当前架构检查点：`IRD-D10-20260829`（D0～D12 全链完成；契约联合评审通过，全部契约与 ADR 为 `Accepted`）  
> 产品需求基线：`requirements.md` v0.8  
> 总体执行基线：`development-task-breakdown.md` v1.3  
> 代码基线：`94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`  
> 基线说明：[DOCUMENT-BASELINE.md](DOCUMENT-BASELINE.md)

## 阅读顺序

1. [产品需求基线](requirements.md)：产品边界、领域规则、功能需求、非功能需求与验收场景。
2. [架构契约](architecture/)：先读契约注册表、符号注册表和 ADR，再读领域、规范运动学、评估语义、执行模型、候选编译、持久化、公共接口和测试契约。
3. [模块详细方案](module-design/)：模块目录、数据流、算法、适配、错误、测试、迁移和扩展细节。
4. [开发任务拆解总纲](development-task-breakdown.md)：26 个工作包、依赖关系、阶段门禁、责任分离和关键路径。
5. [工作包计划](work-packages/)：WP-00～WP-25 的详细实施计划。
6. [智能体任务卡](agent-tasks/)：WP-00～WP-25 的一任务一卡执行上下文。
7. [需求追踪矩阵](requirement-traceability.csv)：128 项需求到实现、测试、评审、验收场景、阶段和发布切片的机器可检查映射；[治理追踪](governance-traceability.csv)登记治理豁免任务（WP-00 等）。
8. [性能基准清单](benchmark-manifest.json)：固定硬件、数据集、线程、种子和统计口径。
9. [持久化 Schema](schemas/)：机器可验证 JSON Schema 与示例（D3 交付物）。
10. `generate-traceability.ps1` 与 `validate-development-docs.ps1`：派生追踪和文档门禁。

## 权威层次与冲突处理

| 层 | 唯一职责 | 不得承担 |
| --- | --- | --- |
| 需求 | 产品边界、行为、质量属性和验收场景 | 类、字段、目录和实现算法 |
| 架构 | 跨模块字段、状态、接口、所有权和决策 | 改写产品需求 |
| 模块设计 | 在架构约束下给出模块内部可实现设计 | 创建第二套公共契约 |
| 总体计划 | 工作包边界、依赖、阶段与角色分离 | 替代模块设计 |
| 工作包 | 组织交付范围、任务 DAG、迁移和退出证据 | 私自解释上游语义 |
| 任务卡 | 单次执行的文件边界、步骤、测试、命令与停止条件 | 在实现中补做设计决策 |
| 实现与证据 | 执行已接受设计并证明结果 | 反向冻结需求或契约 |

下游与上游冲突时，智能体必须停止，报告冲突位置、受影响消费者和建议的权威文件所有者；不得选择一个“看起来合理”的解释继续编码。

## 文档状态

| 状态 | 含义 |
| --- | --- |
| `Draft` | 内容不完整或仍存在实现者需自行决定的语义 |
| `Proposed` | 内容完整，等待所有者、消费者和验证方评审 |
| `Accepted` | 已通过评审，可作为下游和实现依据 |
| `Superseded` | 已被新版本或 ADR 替代，不再作为实施依据 |

需求 v0.8 当前为 `Accepted`。全部 25 项架构契约、76 个公共符号与 ADR-001～005 已于 `IRD-D10-20260829` 契约联合评审通过（评审记录见 [architecture/review/2026-08-29-contract-review.md](architecture/review/2026-08-29-contract-review.md)），整体为 `Accepted`；此后语义变更必须走契约变更流程（所有者提出、ADR、消费者影响、版本提升）。模块方案、工作包与任务卡状态见 [DOCUMENT-BASELINE.md](DOCUMENT-BASELINE.md) 与 [agent-tasks/task-status.md](agent-tasks/task-status.md)。

## 维护规则

- `requirements.md` 是唯一产品需求基线；需求变更必须更新修订记录和追踪矩阵。
- `architecture/` 是跨模块公共语义的唯一权威；公共契约变化必须包含所有者、ADR、消费者影响和契约测试。
- `module-design/` 只细化模块内部设计；与需求或架构冲突时停止，不得就地覆盖。
- `development-task-breakdown.md` 是唯一总体实施计划；不得在其他目录创建平行总纲。
- 新的工作包计划只进入 `work-packages/`，文件名使用 `WP-XX-<topic>.md`。
- 新的任务卡只进入 `agent-tasks/`，稳定 Task ID 不得复用；状态从 `Planned` 转为 `Ready` 必须逐项通过入口门禁。
- 权威内容变化后，按“需求/ADR → 架构 → 模块设计 → 工作包 → 任务卡 → 追踪 → 校验 → 独立评审”的顺序同步。
- 算法证据、报告和测试结果进入产品定义的项目工件或 CI 产物，不在仓库中散落临时 Markdown。
- 执行 `generate-traceability.ps1` 更新追踪矩阵，执行 `validate-development-docs.ps1` 检查完整性；两个脚本兼容 Windows PowerShell 5.1 与 PowerShell 7。

## 当前状态

- 需求基线：v0.8；需求 128 项（P0 114、P1 14），含 D12 新增试点/交付锚点 PILOT-01/02、DEL-01/02。
- 验收场景：AT-01～AT-19（AT-18/AT-19 按阶段分链路覆盖）。
- D0～D9 完成：文档权威层次与基线冻结、契约/符号登记、五大核心契约正文、机器可验证 Schema（14 个 Schema、45 个非法示例负例）、23 篇模块详设 v0.3、26 个工作包计划、144 张 16 字段任务卡、真实追踪矩阵与门禁增强。
- D10（语义闭合）完成：任务级循环依赖解除、诊断注册表 117 码逐项裁决一致、7 个跨模块类型补登记、WP-04/WP-12 口径统一、契约联合评审通过（`Accepted`）。
- D11（执行闭合）完成：证据根目录统一为 `out/test-evidence/wp-xx/<run-id>/`、任务卡验证命令可执行化（rg、必执行＋回退、无占位路径）、提交格式中文化（144/144）。
- D12（治理闭合）完成：反向追踪闭合（治理豁免 CSV＋PILOT/DEL 需求锚点）、文档状态刷新、任务状态账本建立、验证器新增八项检查。
- 实施入口：按 `development-task-breakdown.md` §8/§9 阶段顺序与 [agent-tasks/task-status.md](agent-tasks/task-status.md) 账本从 WP-00/WP-01 开始；当前产品边界为单项目单机械臂，多机械臂扩展保留命名空间。
