# WP-08-T01 任务状态机与合法结果

- **Task ID / 需求 ID / ADR / 阶段：**WP-08-T01；TASK-01～03、NFR-COR-02、NFR-REL-02～03；ADR-005（TaskState 冻结 9 态）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线无任务状态机：`kinematicanalysis`/`structureoptimizer` 用 QThreadPool/QtConcurrent 内联异步，无终态概念）；契约 `architecture/execution-model.md` §1（CTR-EXE-001）；方案 `module-design/execution-platform.md` v0.3 §4
- **前置任务及必需工件：**WP-03-T03（TaskState 枚举 SYM-STA-006）、WP-03-T02（RunIdentity SYM-ID-006）、WP-01-T02（`sdurws_ird_execution` 目标骨架）、WP-01-T03（测试入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/` 下 `include/sdurws/ird/execution/{TaskStateMachine.hpp,TaskHandle.hpp,TaskSnapshot.hpp}`、`src/TaskStateMachine.cpp`、`test/StateMachineTest.cpp`
- **禁止修改的文件和公共接口：**execution-model §1 转移表、WP-03 枚举、WP-05 结果状态与接纳语义、worker IPC 字段、GUI 状态显示、`IEvaluationScheduler`/`TaskHandle` 冻结签名（public-interfaces §4）
- **修改前接口：**无状态机类型；异步任务以 QFuture 回调表示，取消/暂停/崩溃无统一终态与审计事件
- **修改后接口：**`TaskStateMachine`（模块私有）按 §1 表驱动转移并原子发布；终态（Completed/Canceled/Failed/Interrupted）无出边且幂等；非法转移在构造边界拒绝并返回 `IRD-EXEC-ILLEGAL-TRANSITION`（System），旧状态不变；`SchedulerContractTest` 注册于 `sdurws_ird_execution_contract_test`
- **实施步骤：**实现 9 态枚举与 §1 转移表为唯一数据源 → 单写者线程模型（仅主进程调度线程转移，UI/其他线程只读快照）→ 转移记录追加时间戳/原因/runId/attemptId → 终态封闭与重复取消幂等 → 非法转移拒绝路径
- **RED 测试：**`test/StateMachineTest.cpp`（注册于 `sdurws_ird_execution_test`）：逐行断言 execution-model.md §1 转移表（正文计数 18 条，含 Queued→Canceling、Running→Pausing→Paused、Paused→Canceling、Canceling→Canceled、Canceling→Failed）全部通过；未列出转移（如 Paused→Completed、终态出边）一律拒绝——先确认测试在无实现时失败
- **最小实现：**转移表驱动核心＋审计事件追加＋`IRD-EXEC-ILLEGAL-TRANSITION`/`IRD-EXEC-ALREADY-TERMINAL` 两个拒绝路径；不实现 worker 与调度队列
- **正常/边界/失败测试：**正常：表内转移状态与审计事件正确。边界：`Interrupted` 恢复为携带新 attemptId 的新尝试而非转移、`cancel()` 在 Canceling 与终态为 no-op、`Pausing` 只能进 Paused/Canceling/Failed/Interrupted。失败：非法转移返回 `IRD-EXEC-ILLEGAL-TRANSITION` 且旧状态不变；终态后 cancel/pause 返回 `IRD-EXEC-ALREADY-TERMINAL`（Input/Info）
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_execution(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_execution_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_execution(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；转移表无表外分支（代码中无硬编码状态字符串 if 链外的路径）；WP-03 枚举与 public-interfaces §4 头未被改动；无 Qt Widgets include
- **证据工件：**`execution/out/test-evidence/wp-08/<run-id>/`：状态转移矩阵参数化结果、事件日志样例、拒绝诊断 JSON、命令日志与评审签名
- **提交格式：**`WP-08-T01: 新增执行任务状态机`

  - 新增 9 态转移表驱动状态机与审计事件实现
  - 新增 转移表逐行断言测试与目标登记
  - 新增 状态转移矩阵与拒绝诊断证据记录
- **停止与升级条件：**requirements §6.4 状态表与 execution-model.md §1 不一致时暂停并升级至 ADR-005 所有者裁决，不得自行增删转移
