# 计算任务平台模块详细方案

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；负责 WP：WP-08；阶段/发布：阶段 A / R1
- 最高权威：`architecture/execution-model.md`（9 态状态机、转移表、并发/取消/接纳规则）；其余契约：`architecture/public-interfaces.md` §3～4/§7、`architecture/evaluation-semantics.md` §1～2、`architecture/testing-contract.md`；需求锚点：§6.4；任务卡：`agent-tasks/WP-08-T01～T05`

## 1. 模块职责

实现 `IEvaluationScheduler` 调度端口与 9 态任务状态机、worker 隔离、取消/暂停、缓存、检查点、资源节流与确定性调度，并在接纳前校验 envelope 合法组合。不实现评估算法、不写项目 revision、不接管 WP-05 的结果接纳与当前性。本模块与 canonical-kinematics 契约无关：不解析、不引用规范运动学内容。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/
  include/sdurws/ird/execution/
    IEvaluationScheduler.hpp TaskHandle.hpp EvaluationRequest.hpp
    TaskSnapshot.hpp TaskCapabilities.hpp TaskStateMachine.hpp
    WorkerProtocol.hpp EvaluationCache.hpp CacheKey.hpp
    CheckpointStore.hpp ResourceBudget.hpp ExecutionDiagnostics.hpp
  src/Scheduler.cpp TaskStateMachine.cpp WorkerLauncher.cpp
      Cache.cpp CheckpointStore.cpp ResourceController.cpp
  worker/WorkerMain.cpp WorkerProtocol.cpp
  test/StateMachineTest.cpp RequestIdentityTest.cpp CancellationTest.cpp
      CacheCheckpointTest.cpp BoundedParallelismTest.cpp
  testdata/execution/{checkpoints,failpoints}/
  evidence/WP-08/
```

CMake 目标：`sdurws_ird_execution`、`sdurws_ird_execution_worker`、`sdurws_ird_execution_test`、`sdurws_ird_execution_contract_test`。代码前置 WP-04、05（总纲 §5.2）：直接链接 WP-03 core 与 WP-05 evidence 接口，WP-04 经 WP-05 传递、本模块不直接包含 project/ 头；允许 Qt Core/Concurrent/Process 与标准库；禁止 Qt Widgets、评估器私有线程池、worker 打开项目根写句柄、手工 CSV。`IEngineeringEvaluator` 头文件位于 `evidence/`（public-interfaces §3），本模块消费不复制。`WorkerProtocol`、`EvaluationCache`、`CheckpointStore`、`ResourceController` 为模块私有类型。

## 3. 数据与接口

- `IEvaluationScheduler`/`TaskHandle` 签名以 public-interfaces §4 为准；`EvaluationRequest`（SYM-EXE-001）、`RunIdentity`（SYM-ID-006）、`TaskState`（SYM-STA-006）、`ResultEnvelope`（SYM-EVI-005）等公共符号以 symbol-registry §2 为准。
- `EvaluationRequest` 为不可变值对象：项目/分支/修订、snapshot/slice 身份、评估器与版本、runId/attemptId、模式、seed、threadCount、resourceBudget、cache/checkpoint 策略；调度器复制请求，worker 只读快照。
- Worker 消息协议（模块私有）：请求消息含 schema、身份、预算与 seed；回报消息三类——`progress`（batchId＋进度）、`checkpoint`（checkpointRef＋已完成批次集合）、`completion`（envelope 摘要）；跨进程不传裸指针，IPC 断开视为 worker 丢失。
- **resourceBudget 默认值（本模块冻结，架构层故意留白处）**：`maxConcurrentTasksPerProject=4`；`maxWorkers=物理核数−1`（上限 8）；`memoryBudgetPercent=70`；`cancelTimeoutMs=30000`；`checkpointInterval=每批次`。默认值随请求进入快照与证据记录；调用方可收紧，放宽需评审记录。

## 4. 调用与状态

```text
submit 不可变请求 → 身份/预算校验 → capability check → cache lookup（全键）
  → 有界队列准入 → worker spawn（只读快照）→ 批次消息（安全点＝批次边界）
  → completion → evaluation-semantics §2 合法组合校验 → WP-05 接纳 → 追加历史
```

状态机实现要点（9 态与 18 条合法转移以 execution-model.md §1 转移表为准，引用不复制）：

- **单写者线程模型**：`TaskState` 仅由主进程调度线程转移并原子发布；UI 与其他线程只读快照；worker 不持有状态机，只回报消息。
- **取消语义**：`Canceling` 中重复 cancel 为 no-op；`cancelTimeoutMs` 超时强杀 worker → `Canceled`＋"强制终止"诊断＋保留最近兼容检查点；取消期间非用户异常 → `Failed`，与超时强杀严格区分。
- **迟到事件**：终态后到达的 progress/checkpoint/completion 只追加原分支历史，不改写终态、不写入当前结果。
- **attempt 串行**：同一 runId 的新 attempt 仅在旧 attempt 终态后启动；`Interrupted` 的恢复是携带新 attemptId 的新尝试，不是旧任务转移。

错误矩阵：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-EXEC-RESOURCE-BUDGET` | 并发任务/worker 数/内存超 resourceBudget 上限 | Engineering | Warning | 任务保持 Queued；释放资源后重派 |
| `IRD-EXEC-CAPABILITY-UNSUPPORTED` | 对未声明暂停/检查点能力的任务请求该能力 | Input | Error | 状态不变；改用支持能力的评估器 |
| `IRD-EXEC-ALREADY-TERMINAL` | 终态后调用 cancel/pause | Input | Info | no-op 并返回当前状态与诊断 |
| `IRD-EXEC-CHECKPOINT-INCOMPATIBLE` | 检查点 schema/版本不兼容 | Engineering | Error | 不用于恢复；保留并标记原因 |
| `IRD-EXEC-REQUEST-INVALID` | 身份缺失、预算非法、重复请求 | Input | Error | 不入队、不创建结果 |
| `IRD-EXEC-WORKER-LOST` | worker 崩溃或 IPC 断开 | System | Error | 转 `Failed` 附退出原因；保留检查点 |
| `IRD-EXEC-ILLEGAL-TRANSITION` | 未列入转移表的转移请求 | System | Error | 构造边界拒绝，状态不变 |

## 5. 关键实现约定

1. 安全点＝批次边界（或评估器声明的等价边界）；暂停/取消/检查点仅在安全点生效，`Pausing` 有界等待且一经接受不可撤销。
2. 缓存键覆盖 snapshot/sliceHash、policy 内容身份、canonical 物理身份、评估器/算法/库版本、seed、threadCount、budget、mode 与 checkpoint schema（execution-model §3）；仅 `Completed + Complete + 兼容` 可正式命中。
3. 检查点按 `<runId>/<attemptId>/<checkpointId>` 追加，含 `completedBatchIds`；恢复沿用原 runId、创建新 attemptId，批次去重、统计不重复，不覆盖旧检查点。
4. 确定性：固定 seed、线程数、输入切片与版本时，按稳定 taskId/batchId 排序派发与合并，输出与线程完成顺序无关。
5. capability 声明：pause/checkpoint/强制终止能力由 `capabilities()` 声明（评估器与调度器合并）；轻量任务可不支持（execution-model §1 冻结规则）。
6. envelope 验收：completion 到达先按 evaluation-semantics §2 校验合法组合，非法组合不转移 `Running → Completed`，转 `Failed` 并保留诊断（锚点：用户取消＝`Canceled + NotEvaluated + Partial/None`；预算耗尽＝`Completed + DataInsufficient + Complete`）。

## 6. 测试与证据

| 测试 | 断言要点 |
| --- | --- |
| StateMachineTest | 18 条合法转移逐条通过；全部非法转移在构造边界拒绝 |
| RequestIdentityTest | 身份校验、重复请求拒绝、迟到事件只追加历史 |
| CancellationTest | 排队/运行/暂停中取消、重复取消幂等、超时强杀 Canceled 与取消期异常 Failed 可区分 |
| CacheCheckpointTest | 全键缓存、失败/Partial/Quick 不命中、检查点兼容两分支、恢复不重复计数 |
| BoundedParallelismTest | 超限任务保持 Queued、内存阈值节流、固定输入并行结果一致 |
| SchedulerContractTest | public-interfaces §4 契约与 execution-model §6 契约测试全覆盖 |

验证命令（脚本形式与原生回退；worker 可执行文件须先构建）：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_execution(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_execution_worker
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_execution_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_execution_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_execution(_contract)?_test$"
```

故障注入覆盖取消时限、worker 崩溃、IPC 中断、磁盘满与内存阈值；性能记录排队延迟、吞吐、P50/P95 与峰值内存。证据包含 request/snapshot/run/attempt 身份、状态转移日志、cache key、checkpoint hash、批次集合、资源曲线、取消耗时、WP-05 接纳回执与独立评审签名。

## 7. 迁移与删除表

| 旧链路 | 处置 | 条件 |
| --- | --- | --- |
| 旧线程池与异步评估入口 | 适配器接入 → Rewrite | 状态/身份/缓存契约通过 |
| 无迟到保护的旧回调链路 | Rewrite/EvidenceOnly | 迟到保护可证明 |
