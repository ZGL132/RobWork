# WP-08-T05 有界并行、资源预算与确定性

- Task ID：WP-08-T05
- 需求/阶段：NFR-PERF-04～06、NFR-COR-02、CON-04；阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/testing-contract.md`；模块方案：`module-design/execution-platform.md`
- 前置：WP-08-T01、WP-08-T03、WP-08-T04。
- 允许：修改 `execution/include/.../ResourceBudget.hpp`、`src/ResourceController.cpp`、`src/BoundedQueue.cpp`、`src/DeterministicMerge.cpp`、`test/BoundedParallelismTest.cpp`。
- 禁止：无限队列、按完成顺序合并结果、修改评估算法、绕过预算或手工调整性能数据。
- 产出：有界队列、流式摘要、70% 内存节流和固定输入确定性合并。

## Given/When/Then

- Given队列达到上限，When submit，Then返回 backpressure/排队诊断，不无限增长。
- Given内存接近物理内存 70%，When monitor，Then先降低并发/停止派发；仍不足返回 `IRD-EXEC-RESOURCE-BUDGET`。
- Given固定输入、seed、线程和版本，When重复并行运行，Then任务顺序、候选集合、稳定 ID 和 Pareto 关系一致。
- Given大批次结果，When merge，Then按稳定 taskId/batchId 排序，摘要流式输出且不丢统计。

## 测试与证据

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_bounded_parallelism_test$'
```
证据：队列/内存曲线、吞吐 P50/P95、重复运行 diff、预算诊断和评审签名。提交：`WP-08-T05: implement bounded deterministic execution`。

停止：结果依赖线程完成顺序、内存预算无法测量或性能数据不可复现时暂停。
