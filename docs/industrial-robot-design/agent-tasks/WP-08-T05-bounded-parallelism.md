# WP-08-T05 有界并行、资源预算与确定性

- **Task ID / 需求 ID / ADR / 阶段：**WP-08-T05；NFR-PERF-04～06、NFR-COR-02、CON-04；无新 ADR（默认值由 module-design/execution-platform.md §3 冻结）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线 `structureoptimizer/StructureOptimizationController.cpp` 等无界排队、按完成顺序合并结果）；契约 `architecture/execution-model.md` §2（CTR-EXE-001）、`architecture/testing-contract.md` §2；方案 `module-design/execution-platform.md` v0.3 §5
- **前置任务及必需工件：**WP-08-T01（状态机 Queued 语义）、WP-08-T03（worker 启动器与进程隔离）、WP-08-T04（缓存键与批次集合）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/` 下 `include/sdurws/ird/execution/ResourceBudget.hpp`、`src/{Scheduler.cpp（有界队列准入与确定性合并）,ResourceController.cpp}`、`test/BoundedParallelismTest.cpp`、`testdata/execution/failpoints/`
- **禁止修改的文件和公共接口：**评估算法、`resourceBudget` 默认值集（module-design §3）、无限队列、按完成顺序合并、手工调整性能数据、`IEvaluationScheduler` 冻结签名
- **修改前接口：**并发无上限（QtConcurrent 全局池）、结果合并顺序取决于线程完成顺序、无内存预算监测
- **修改后接口：**`ResourceBudget` 携带冻结默认值 `maxConcurrentTasksPerProject=4`、`maxWorkers=物理核数−1（上限 8）`、`memoryBudgetPercent=70`、`checkpointInterval=每批次`；超限任务保持 Queued 并产生 `IRD-EXEC-RESOURCE-BUDGET`；批次结果流式摘要、明细按查询读取
- **实施步骤：**实现有界队列准入 → ResourceController 监测内存（达物理内存约 70% 先降并发/停派发）→ 仍不足返回 `IRD-EXEC-RESOURCE-BUDGET` → 按稳定 taskId/batchId 排序派发与合并 → 流式摘要输出
- **RED 测试：**`test/BoundedParallelismTest.cpp`（注册于 `sdurws_ird_execution_test`）：固定输入、seed、线程数与版本重复并行运行，任务顺序、候选集合、稳定 ID 与 Pareto 关系必须完全一致；队列达上限时 submit 返回排队/backpressure 诊断而非无限增长——先确认测试在无实现时失败
- **最小实现：**有界队列＋三默认值执行＋确定性合并；性能数据记录引用 `benchmark-manifest.json`（testing-contract §2），不手工调整
- **正常/边界/失败测试：**正常：大批次结果按 taskId/batchId 排序合并且摘要统计无丢失。边界：并发恰为 4、worker 数恰为核数−1 与上限 8、内存恰过 70% 阈值先节流。失败：内存持续超限 → `IRD-EXEC-RESOURCE-BUDGET` 且任务保持 Queued；队列满 → backpressure 诊断不丢任务
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_execution(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_execution_worker
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_execution(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；无 `unbounded`/无上界队列构造；合并代码无按完成时间排序残留；默认值无第二份硬编码副本；无评估算法文件改动
- **证据工件：**`execution/evidence/WP-08/T05/`：队列/内存曲线、吞吐 P50/P95 与峰值内存、重复运行 diff（逐字节对照）、预算诊断样例、命令日志与评审签名
- **提交格式：**`WP-08-T05: implement bounded deterministic execution`
- **停止与升级条件：**结果依赖线程完成顺序、内存预算在目标平台无法测量或性能数据不可复现时暂停并升级至 WP-08 所有者与独立测试负责人
