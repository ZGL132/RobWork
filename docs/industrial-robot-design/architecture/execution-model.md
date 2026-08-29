# 执行模型契约

> 契约 ID：`CTR-EXE-001`（§1～2）、`CTR-EXE-002`（§3）、`CTR-EXE-003`（§4）、`CTR-EXE-004`（§5）  
> 检查点：`IRD-D2-20260829`  
> 文档状态：`Proposed`（IRD-D10-20260829 联合评审通过，待签署）  
> 权威边界：本文件是后台任务状态机、请求身份、并发与所有权、取消/检查点/恢复和结果接纳的唯一权威；需求 §6.4 是产品语义决策来源。

## 1. 任务状态机（冻结）

`TaskState`（SYM-STA-006，9 态）：

```cpp
enum class TaskState {
    Queued, Running, Pausing, Paused, Canceling,
    Completed, Canceled, Failed, Interrupted
};
```

完整转移表（未列出的转移一律非法，必须在调度器构造边界拒绝）：

| 起始 | 目标 | 触发 | 守卫 / 附加要求 |
| --- | --- | --- | --- |
| Queued | Running | 调度派发 | 不可变快照已附加；资源预算允许 |
| Queued | Canceling | 取消请求 | — |
| Running | Pausing | 暂停请求 | `capabilities().pause == true`，否则拒绝且状态不变 |
| Running | Canceling | 取消请求 | 停止派发新批次 |
| Running | Completed | 评估器正常结束 | envelope 通过评估语义合法组合校验 |
| Running | Failed | 非用户触发的 worker 异常退出 | 附退出原因诊断 |
| Running | Interrupted | 应用退出 / 系统中断 | 重启后发现未完成任务 |
| Pausing | Paused | 到达安全点（批次/评估器声明的边界） | 有界等待；检查点可选保留 |
| Pausing | Canceling | 取消请求 | — |
| Pausing | Failed | 非用户异常退出 | 附退出原因诊断 |
| Pausing | Interrupted | 应用退出 / 系统中断 | — |
| Paused | Running | 恢复请求 | 新 `attemptId`，沿用原 `runId` |
| Paused | Canceling | 取消请求 | — |
| Paused | Failed | 非用户异常退出 | — |
| Paused | Interrupted | 应用退出 / 系统中断 | — |
| Canceling | Canceled | 干净取消，或取消超时强制终止 worker | 强杀必须附“强制终止”诊断并保留最近兼容检查点（需求 §6.4） |
| Canceling | Failed | 取消期间非用户原因异常退出 | 与超时强杀的 `Canceled` 严格区分 |
| Canceling | Interrupted | 应用退出 / 系统中断 | — |

冻结规则：

- **终态幂等**：`Completed / Canceled / Failed / Interrupted` 没有出边。`Interrupted` 任务的恢复是携带新 `attemptId` 的新尝试，不是旧任务的状态转移。
- **重复取消幂等**：`cancel()` 在 `Canceling` 中为 no-op；在终态为 no-op 并返回当前状态与诊断 `IRD-EXEC-ALREADY-TERMINAL`，不得产生第二个终态记录。
- **能力声明**：暂停、检查点、强制终止能力由 `capabilities()` 声明；对不支持能力的请求返回 `IRD-EXEC-CAPABILITY-UNSUPPORTED`，状态不变。并非所有轻量任务都必须支持暂停或检查点（需求 §6.4）。
- **暂停不可撤销**：`Pausing` 只能进入 `Paused / Canceling / Failed / Interrupted`；暂停请求一经接受不得回退为未请求。
- **迟到响应**：终态之后到达的 worker 事件（完成、进度、检查点）只能追加到原分支历史，不得改写终态、不得写入当前结果（需求 §6.4）。

## 2. 请求身份与并发所有权（冻结）

- 每个请求携带 `RunIdentity = {projectId, branchId, revisionId, runId, attemptId}`（SYM-ID-006）；完成事件必须校验身份，不匹配或旧会话的任务不得成为当前结果（TASK-03）。
- **状态机唯一写者**：`TaskState` 只能由主进程调度线程转移，转移原子发布；UI 与其他线程只读快照。worker 进程不持有状态机，通过消息回报进度、检查点和完成。
- **worker 隔离**：轻量交互计算可进程内运行；长时间优化、批量 IK 和批量动力学进入独立工作进程。worker 只接收不可变快照，不直接写项目目录（结果一律经 `IResultRepository` 由主进程接纳）。
- **有界并行**：同一项目并发运行任务数、全局 worker 数和内存预算受 `resourceBudget` 上限约束；超限任务保持 `Queued` 并产生 `IRD-EXEC-RESOURCE-BUDGET` 诊断。默认数值由 WP-08 模块方案给出并进入快照记录。
- **attempt 串行**：同一 `runId` 的新 attempt 只能在旧 attempt 终态后启动。
- **协作式取消/暂停**：只在批次或评估器声明的安全点生效；强制终止仅调度器可执行，且仅用于取消超时（阈值 `resourceBudget.cancelTimeoutMs`，由 WP-08 模块方案给默认值）。

## 3. 输入切片与缓存（冻结）

- 评估输入由模型、需求、策略、算法版本、目录版本、随机种子、线程数和配置组成；缓存键必须覆盖全部依赖（`EvaluatorDependencyManifest` 声明，CTR-EXE-002）。
- `Partial`、`Failed`、`Canceled` 结果与不兼容版本不得命中正式缓存。
- 缓存键基于切片内容身份（`sliceHash`），不基于项目修订号；重命名不改变数值缓存键（需求 §7.1、§6.7.1）。

## 4. 取消、检查点与恢复（冻结）

- 取消后停止派发新批次；超时可终止独立工作进程（→ `Canceled` + “强制终止”诊断）。
- 检查点保留最近兼容版本；不兼容检查点不得用于恢复（`IRD-EXEC-CHECKPOINT-INCOMPATIBLE`）。
- 恢复必须记录原 `runId`、新 `attemptId` 和已完成批次集合；统计不得重复（需求 §6.4）。
- 应用退出时所有非终态任务在重启后发现为 `Interrupted`，不得伪装为完整结果。

## 5. 结果接纳（冻结）

- 接纳前校验：对象 ID 集合、输入切片身份、策略内容身份、分支与尝试身份（需求 §6.4）。
- 只有满足评估语义合法组合且通过 `isFormallyFeasible()` 判定的结果进入正式报告与可行 Pareto 集；`DataInsufficient`、`Partial`、`NotEvaluated` 必须显式展示。
- 拒绝码：`IRD-RESULT-SLICE-MISMATCH`、`IRD-RESULT-BRANCH-MISMATCH`、`IRD-RESULT-DUPLICATE-ATTEMPT`、`IRD-RESULT-CONFLICT`（同键不同内容）。

## 6. 契约测试

1. 转移表全覆盖：18 条合法转移逐条通过；全部非法转移在构造边界拒绝。
2. 取消语义矩阵：排队取消、运行中取消、暂停中取消、重复取消、取消超时强杀（`Canceled`）与取消期异常（`Failed`）可区分。
3. 迟到事件：终态后到达的完成事件只追加历史。
4. 并发：有界并行超限任务保持 `Queued`；同 `runId` attempt 串行。
5. 恢复：检查点兼容/不兼容两分支；统计不重复。
