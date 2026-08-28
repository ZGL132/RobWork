# WP-08 计算任务平台实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 为全部评估器提供统一调度、工作进程、任务状态、取消/暂停、缓存、检查点、资源预算和迟到结果保护。

**Architecture:** 调度器只发送不可变快照；评估器声明依赖和能力；长任务在独立工作进程运行，结果必须经 WP-05 接纳。缓存与检查点按版本化契约判断兼容，不复用失败、取消或部分结果。

**Tech Stack:** C++、Qt Core/Concurrent/Process、CTest、PowerShell 故障注入。

---

## 文件与目标

**创建目标：** `sdurws_ird_execution`、`sdurws_ird_execution_worker`、`sdurws_ird_execution_test`。

**创建：**

- `industrialrobot/execution/include/.../IEngineeringEvaluator.hpp`
- `industrialrobot/execution/include/.../IEvaluationScheduler.hpp`
- `industrialrobot/execution/include/.../EvaluationRequest.hpp`
- `industrialrobot/execution/include/.../TaskHandle.hpp`
- `industrialrobot/execution/include/.../TaskStateMachine.hpp`
- `industrialrobot/execution/include/.../EvaluationCache.hpp`
- `industrialrobot/execution/include/.../CheckpointStore.hpp`
- `industrialrobot/execution/include/.../ResourceBudget.hpp`
- `industrialrobot/execution/src/`
- `industrialrobot/execution/worker/`
- `industrialrobot/execution/test/`

**覆盖需求：** TASK-01～03，CON-04，NFR-COR-02，NFR-PERF-02、04～06，NFR-REL-02、03，AT-10、11、13、14。

## 冻结接口

```cpp
template<class Request, class Result>
class IEngineeringEvaluator {
public:
    virtual EvaluatorDependencyManifest dependencyManifest() const = 0;
    virtual ValidationResult validate(const Request&) const = 0;
    virtual Result evaluate(const Request&, ProgressSink&, CancellationToken&) = 0;
};

class IEvaluationScheduler {
public:
    virtual TaskHandle submit(const EvaluationRequest&) = 0;
};
```

状态机严格采用需求第 6.4 节。能力由 `capabilities()` 声明；不支持暂停/检查点的轻量任务不得伪装支持。

## 任务

### Task 1：状态机和合法结果

- [ ] 对每条合法转移和所有非法转移编写参数化测试。
- [ ] 实现 Queued、Running、Pausing、Paused、Canceling 及四种终态。
- [ ] 用户取消后强杀为 Canceled；非用户崩溃为 Failed；重启发现未完成为 Interrupted。

### Task 2：请求身份与迟到保护

- [ ] 请求携带项目、分支、修订、切片、run、attempt、评估器版本、模式、种子和预算。
- [ ] 切换项目/分支后迟到结果只追加原历史，不能成为当前结果。
- [ ] 输入校验失败不创建运行结果。

### Task 3：取消、暂停和工作进程

- [ ] 取消 2 秒内进入 Canceling 并停止派发新批次。
- [ ] 普通批次 10 秒内结束；超时允许终止单一工作进程并保留最近检查点。
- [ ] 注入工作进程异常退出，验证主界面进程和项目不受损。

### Task 4：缓存和检查点

- [ ] 缓存键包含输入切片、策略、规范模型物理身份、评估器版本、种子和预算。
- [ ] 失败、取消、Interrupted 和 Partial 不作为正式缓存命中。
- [ ] 检查点保存版本、run/attempt、算法状态、完成批次和模式；恢复前显式检查兼容性。
- [ ] 恢复后已完成批次不重复计数，不兼容检查点保留供诊断。

### Task 5：资源与并行确定性

- [ ] 批量任务使用有界队列、流式摘要和按需明细。
- [ ] 总内存接近物理内存 70% 时先节流，再返回资源不足诊断。
- [ ] 固定线程数和种子时候选集合、稳定 ID、可行集合和 Pareto 关系一致。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_execution_test$'
```

## 退出条件

- A-GATE-03、05 与 AT-10、11、13 的平台断言通过。
- 崩溃、取消和中断不会产生正式结果或损坏项目。
- 检查点恢复统计不重复，非法缓存命中为 0。
