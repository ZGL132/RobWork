# WP-10-T05 响应性与大列表虚拟化

- Task ID：WP-10-T05
- 需求/阶段：NFR-PERF-01、NFR-PERF-03、UX-01～UX-08；阶段 A / R1
- 架构契约：`architecture/testing-contract.md`、`architecture/execution-model.md`；模块方案：`module-design/session-ui.md`
- 前置：WP-10-T02/T03、WP-08 scheduler、WP-09 logging。
- 允许：修改 `ui/include/.../VirtualResultModel.hpp`、`src/VirtualResultModel.cpp`、`src/PerformanceTelemetry.cpp`、`test/ResponsiveListsTest.cpp`。
- 禁止：一次性装载全部明细、GUI 主线程执行评估/文件 IO、改变分页契约或伪造性能数据。
- 产出：5,000 任务、100,000 摘要、10,000 候选的虚拟模型、后台转移和性能证据。

## Given/When/Then

- Given上述规模数据，When滚动/筛选/选择，Then只加载可视窗口和按需明细，内存受预算约束。
- Given导航、选择、筛选或编辑，When测量，Then P95 ≤ 200 ms；超过 1 秒的工作转后台。
- Given GUI 操作，When持续测量，Then主线程无超过 2 秒无响应窗口。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_responsive_lists_test$'`。GUI 证据需在 Visual Studio x64、`QT_QPA_PLATFORM=windows` 下单独运行。证据：规模、P50/P95、内存和阻塞日志。提交：`WP-10-T05: implement virtualized responsive lists`。

停止：性能结果不可复现、主线程调用业务计算或列表必须全量加载时暂停。
