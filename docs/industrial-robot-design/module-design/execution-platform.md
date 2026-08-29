# 计算任务平台模块详细方案

- 方案版本：v0.2；需求基线：v0.7；负责 WP：WP-08；阶段/发布：阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`

## 1. 职责和目录

本模块负责请求排队、能力校验、任务状态机、worker 生命周期、取消/暂停、缓存、检查点、资源节流和确定性调度。它不实现评估算法、不写项目 revision、不接管 WP-05 结果当前性。

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/
  include/sdurws/ird/execution/
    EvaluationRequest.hpp TaskHandle.hpp TaskStateMachine.hpp
    IEvaluationScheduler.hpp IEngineeringEvaluator.hpp
    CancellationToken.hpp ProgressSink.hpp WorkerProtocol.hpp
    EvaluationCache.hpp CacheKey.hpp CheckpointStore.hpp
    ResourceBudget.hpp ExecutionDiagnostics.hpp
  src/Scheduler.cpp TaskStateMachine.cpp WorkerLauncher.cpp
      Cache.cpp CheckpointStore.cpp ResourceController.cpp
  worker/WorkerMain.cpp WorkerProtocol.cpp
  test/StateMachineTest.cpp RequestIdentityTest.cpp CancellationTest.cpp
      CacheCheckpointTest.cpp BoundedParallelismTest.cpp
```

目标：`sdurws_ird_execution`、`sdurws_ird_execution_worker`、`sdurws_ird_execution_test`、`sdurws_ird_execution_contract_test`。

## 2. 请求和所有权

`EvaluationRequest` 为不可变值对象，包含项目/分支/修订、snapshot/slice hash、评估器和算法版本、run/attempt、模式、seed、线程数、资源预算、缓存和检查点策略。调度器复制请求；worker 只读 snapshot，通过 IPC 返回进度、检查点和结果引用。任何 Qt/RobWork 对象由创建线程销毁，不跨 IPC 传裸指针。

## 3. 状态机

状态：`Queued`、`Running`、`Pausing`、`Paused`、`Canceling`、`Completed`、`Canceled`、`Failed`、`Interrupted`。只有表中转移合法，终态不可转移；每次转移带 timestamp、reason、runId/attemptId。取消先进入 Canceling，worker 确认或被终止后 Canceled；非用户崩溃 Failed；启动扫描未完成请求 Interrupted。

## 4. 调度和 IPC

提交流程为 `validate -> capability check -> cache lookup -> bounded queue -> worker spawn -> batch messages -> admission`。请求消息包含 schema、身份、预算和 seed；worker 消息包含 batchId、progress、checkpointRef、diagnostics 和结果摘要。IPC 断开先标记 Failed，保留最后检查点；禁止 worker 打开项目根写句柄。

## 5. 缓存和检查点

缓存 key 使用规范 JSON 覆盖 snapshot/slice、policy、canonical physical identity、evaluator/version、软件 baseline、seed、threadCount、budget、mode 和 checkpoint schema。命中前验证 payload 完整度和兼容性。检查点按 `<runId>/<attemptId>/<checkpointId>` 追加，包含已完成 batch 集合；恢复创建新 attemptId 并去重批次，不覆盖旧检查点。

## 6. 资源控制和确定性

队列最大长度、worker 并发、每任务内存/CPU/磁盘预算在配置中显式记录。内存达到物理内存 70% 触发节流；预算超限返回 `IRD-EXEC-RESOURCE-BUDGET`。固定 seed、线程、输入和版本时按稳定 taskId/batchId 排序派发，结果合并也按稳定键排序，避免线程完成顺序影响输出。

## 7. 测试与证据

单元测试覆盖所有状态转移和非法组合；契约测试覆盖身份、迟到回调、缓存排除、检查点兼容和 worker 写权限；故障注入覆盖取消时限、超时、进程崩溃、IPC 中断、磁盘满和内存阈值；性能记录排队延迟、吞吐、P50/P95、峰值内存。证据含状态日志、request/cache/checkpoint hash、批次集合、资源曲线和 WP-05 接纳回执。

## 8. 迁移与评审

旧异步入口通过 adapter 接入，未证明状态和迟到保护前标 Rewrite/EvidenceOnly。评审确认状态机无隐式转移、worker 无项目写权限、失败缓存不可命中、恢复不重复计数、取消时限可测且固定输入结果确定。
