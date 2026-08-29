# WP-08 计算任务平台实施计划

> 阶段/发布：阶段 A / R1；调度与执行公共接口所有者：WP-08。实现者、验证者和评审者必须是不同执行上下文。

**目标：** 为所有评估器提供统一的请求身份、任务状态机、调度器、工作进程、取消/暂停、缓存、检查点、资源预算和迟到结果保护，确保失败或过期结果不会污染当前项目。

## 1. 目标与非目标

交付 `IEvaluationScheduler`、任务状态机、有界队列、独立 worker、取消与暂停协议、版本化缓存/检查点、内存节流和确定性并行。长任务必须在独立进程运行；结果只经 WP-05 接纳。轻量任务若不支持暂停/检查点必须明确声明能力。

不实现具体 FK/IK/动力学/优化算法、项目事务、结果报告、GUI 调度状态展示或替代 WP-05 的结果接纳语义。

## 2. 需求与契约

- 需求：TASK-01～TASK-03、CON-04、NFR-COR-02、NFR-PERF-02、NFR-PERF-04～06、NFR-REL-02～03、AT-10、AT-11、AT-13、AT-14。
- 架构契约：`architecture/execution-model.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`。
- 模块方案：`module-design/execution-platform.md`。
- 阶段/发布：阶段 A / R1；阶段 B 只使用已冻结调度接口，阶段 D 才启用全量并行优化。

## 3. 文件所有权与依赖

拥有目录：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/`，含 `include/sdurws/ird/execution/`、`src/`、`worker/`、`test/`、`testdata/`、`evidence/`。允许 WP-03 core、WP-05 快照/结果接口、Qt Core/Concurrent/Process 和标准库；禁止 Qt Widgets、评估器私有线程池、工作进程写项目 revision、手工 CSV。

目标：`sdurws_ird_execution`、`sdurws_ird_execution_worker`、`sdurws_ird_execution_test`、`sdurws_ird_execution_contract_test`。

## 4. 请求、状态和公共接口

`EvaluationRequest` 必填：`projectId`、`branchId`、`revisionId`、`snapshotId`、`evaluatorId/version`、`runId`、`attemptId`、`mode`、`randomSeed`、`threadCount`、`resourceBudget`、`cachePolicy`、`checkpointPolicy`。提交时复制为不可变值对象。

`IEvaluationScheduler`/`TaskHandle`/`TaskSnapshot` 的签名与语义（`pause/resume/cancel/checkpoint` 经 `TaskHandle`，全部返回 `expected<TaskSnapshot, EvaluationError>`）以 `architecture/public-interfaces.md` §4 为唯一权威，本计划不复制定义。

任务状态机为 9 态（`Queued/Running/Pausing/Paused/Canceling/Completed/Canceled/Failed/Interrupted`），18 条合法转移以 `architecture/execution-model.md` §1 转移表为唯一权威（含 `Queued→Canceling`、`Paused→Canceling`、`Canceling→Failed` 等旧文本遗漏的转移）。终态不可再转移且幂等；非法转移返回 `IRD-EXEC-ILLEGAL-TRANSITION` 且不改变状态。

## 5. 调度、进程和结果数据流

```text
submit immutable request
  -> validate identity/capability/budget
  -> cache lookup (full key only)
  -> bounded queue admission
  -> spawn worker with read-only snapshot + result channel
  -> progress/checkpoint/cancel messages
  -> worker result tagged run/attempt/snapshot
  -> WP-05 ResultAdmission
  -> append history; currentness decided outside scheduler
```

取消请求接受后停止派发新批次；取消超时强杀阈值 `resourceBudget.cancelTimeoutMs` 由 module-design/execution-platform.md 冻结默认值 30000 ms（`Canceling` 起算；强杀→`Canceled`＋"强制终止"诊断，取消期间非用户异常→`Failed`，两者严格区分）。主进程崩溃或 worker 异常不能写项目文件。迟到回调只追加原 branch/revision 历史，不成为当前结果。

## 6. 缓存与检查点

缓存键必须覆盖 `snapshot/sliceHash`、policy hash、canonical model physical identity、evaluator/version、algorithm/library baseline、seed、threadCount、resource budget、mode 和 checkpoint schema。只有 `Completed + Complete + compatible` 可作为正式命中；Failed/Canceled/Interrupted/Partial/Quick 不得命中正式缓存。

检查点包含 `checkpointSchema`、project/branch/revision、snapshot/slice hash、runId、attemptId、evaluator/version、algorithm state、completedBatchIds、seed、threadCount、createdAt。恢复前逐字段兼容检查；新 attemptId 继承原 runId，已完成批次集合去重，统计不得重复。不兼容检查点保留并标记原因。

## 7. 资源预算与确定性

队列有界，批次结果默认流式摘要，明细按查询请求读取。内存达到物理内存约 70% 时先降低并发/暂停派发，仍不足返回 `IRD-EXEC-RESOURCE-BUDGET`；CPU、worker 数和磁盘预算均记录在请求和证据中。固定线程数、seed、输入切片和版本时，任务顺序、候选集合、稳定 ID、可行集合和 Pareto 关系必须一致。

## 任务

| 任务 | 独立产出 | 任务卡 |
| --- | --- | --- |
| WP-08-T01 | 状态机与终态合法性 | [T01](../agent-tasks/WP-08-T01-state-machine.md) |
| WP-08-T02 | 请求身份和迟到保护 | [T02](../agent-tasks/WP-08-T02-request-identity.md) |
| WP-08-T03 | 取消/暂停和 worker 隔离 | [T03](../agent-tasks/WP-08-T03-cancellation.md) |
| WP-08-T04 | 缓存、检查点和恢复 | [T04](../agent-tasks/WP-08-T04-cache-checkpoint.md) |
| WP-08-T05 | 有界并行、资源节流和确定性 | [T05](../agent-tasks/WP-08-T05-bounded-parallelism.md) |

依赖：T01 → T02 → T03；T04 依赖 T02/T03；T05 依赖 T01/T03。每张卡一个 worktree、分支和提交。

## 8. 失败分类与证据

- 输入错误：身份缺失、能力不支持、预算非法、重复 request；不入队、不创建结果。
- 工程不可行：数据不足、检查点不兼容、资源预算无法满足；保留诊断和历史，不伪装成功。
- 系统错误：worker 崩溃、超时、进程/磁盘/IPC 故障；主进程和项目不受损，结果标 Failed/Interrupted。

证据必须含 request/snapshot/run/attempt 身份、状态转移日志、cache key、checkpoint hash、批次集合、资源曲线、取消/超时耗时、结果接纳回执和独立评审签名。

## 验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_execution_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_execution(_contract)?_test$'
```

## 9. 迁移

旧线程池和评估入口先以适配器接入，只有状态/身份/缓存契约通过才标 Migratable；无法证明迟到保护的标 Rewrite/EvidenceOnly。

## 退出条件

A-GATE-03/05 与 AT-10/11/13/14 通过；取消、崩溃和重启不产生正式结果或损坏项目；检查点恢复不重复计数；非法缓存命中为 0；固定输入并行运行结果完全一致。
