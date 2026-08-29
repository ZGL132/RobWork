# WP-08-T03 取消、暂停与工作进程隔离

- Task ID：WP-08-T03
- 需求/阶段：TASK-01～03、NFR-PERF-02、NFR-REL-02～03；阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/testing-contract.md`；模块方案：`module-design/execution-platform.md`
- 前置：WP-08-T01/T02、WP-01 worker 启动脚本。
- 允许：修改 `execution/include/.../CancellationToken.hpp`、`ProgressSink.hpp`、`src/WorkerLauncher.cpp`、`src/CancellationController.cpp`、`worker/WorkerMain.cpp`、`test/CancellationTest.cpp`。
- 禁止：worker 写项目 revision、修改结果接纳、改变状态机合法转移或伪造暂停能力。
- 产出：取消 2 秒进入 Canceling、批次停止、暂停/恢复和 worker 崩溃隔离。

## Given/When/Then

- Given运行任务，When cancel，Then 2 秒内进入 Canceling，停止新批次，确认后 Canceled。
- Given普通批次超过 10 秒，When timeout，Then终止单 worker，保留最近 checkpoint 并返回 System 诊断。
- Given worker 异常退出或 IPC 断开，When scheduler observes，Then主进程和项目不受损，任务 Failed/Interrupted。
- Given evaluator 无 pause/checkpoint capability，When pause，Then返回明确不支持诊断，不伪装 Paused。

## 测试与证据

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_cancellation_test$'
```
证据：时延、批次计数、进程树、项目目录写权限和恢复 checkpoint。提交：`WP-08-T03: implement cancellation and worker isolation`。

停止：取消超时、worker 可写项目或进程所有权无法证明时暂停。
