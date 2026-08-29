# WP-08-T01 任务状态机与合法结果

- Task ID：WP-08-T01
- 需求/阶段：TASK-01～03、NFR-COR-02、NFR-REL-02～03；阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/execution-platform.md`
- 前置：WP-03 状态枚举、WP-01 构建脚本。
- 允许：修改 `execution/include/.../TaskStateMachine.hpp`、`TaskHandle.hpp`、`src/TaskStateMachine.cpp`、`test/StateMachineTest.cpp`。
- 禁止：修改 WP-03 枚举、WP-05 结果状态、worker IPC 字段或 GUI 状态显示。
- 产出：合法状态转移、终态封闭和转移诊断。

## 数据流

`TaskHandle + current state + command -> validate transition -> append timestamp/reason/run/attempt -> new state`。状态记录不可变追加，终态禁止再转移。

## Given/When/Then

- Given Queued/Running/Pausing/Paused/Canceling，When执行表内转移，Then状态和审计事件正确。
- Given终态或未列出的转移，When transition，Then返回 `IRD-EXEC-INVALID-TRANSITION`，旧状态不变。
- Given worker 崩溃/重启未完成/用户取消，When mark，Then分别为 Failed/Interrupted/Canceled，原因可定位。

## 测试与证据

参数化覆盖全部合法/非法边，命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_state_machine_test$'
```
证据：状态转移矩阵、事件日志、诊断 JSON。提交：`WP-08-T01: implement execution state machine`。

停止：需求状态表与架构契约不一致时暂停。
