# WP-23-T06 ird_gates 命中集增量登记（预期面）

| 字段 | 值 |
| --- | --- |
| 任务 | WP-23-T06 内存与节流规模化达标（分支 wp23-t06，base＝redesign-main@15a0f7ab） |
| 比对口径 | ird_gates 引擎直跑为登记权威（WP-10-T64 先例口径）；**本任务实施段未直跑门禁引擎**（集成构建树由合入批执行——冒烟构建树不含全量目标面），本登记册按构建脚本增量如实登记**预期命中集**，双态直跑核定随合入批/治理面补做 |
| 结论（预期） | base↔head 归一化命中集**恰增＝零**——本批零新增构建目标、零新增链接语句、零新增 add_dependencies；唯一构建面变更＝既有测试目标 `sdurws_ird_execution_perf_test` 的源清单增列一个 TU（WP-23-T03 建目标时的登记命中〔SUB→execution／SUB→testkit／T1→testkit 第二检查面＋条件性 worker 目标级依赖〕为已核定存量，本批不新增） |
| 责任方 | 登记册（DTB §4.5）回填与直跑核定归 WP-01-T03 治理面（本任务面如实申报） |

## 预期恰增条目

无（零条目）。

## 附加说明

- **构建面增量唯一处**：`execution/CMakeLists.txt` 的 `add_executable(sdurws_ird_execution_perf_test ...)` 源清单增列 `perf_test/MemoryThrottleBenchmarkTest.cpp`——目标集合、链接语句块、依赖语句块全部零变化（WP-23-T03 先例"单列目标"形态保持；LinkageContractTest 的 UnitEdgesRegisteredOnly/NoTestkitEdge 白名单面零变更——既有登记已覆盖本目标）。
- **新增 TU 的 include 面**全部落在同目标既有 TU（CancelTimingBenchmarkTest.cpp）已覆盖的 include 族内：core（DiagData/Evaluation/Identity）、自单元公共头（Scheduler/WorkerSupervisor/Checkpoint/ChannelProtocol/TaskTypes/Ports——同单元头不构成跨单元边）、project（ArchivePort/StoreTypes——经由既有链接边 sdurws_ird_execution→project 的公共头消费）、testkit（PerfBenchmark/gtest AssertMacros——既有 T1→testkit 登记面）。如引擎按文件粒度归并 include 命中产生表外条目，处置归 WP-01-T03 治理面（预期为零——同目标同族）。
- 产品库 `sdurws_ird_execution` 与 `sdurws_ird_execution_worker` 链接块**零变化**（仍仅 core/evidence/project 三条登记边＋worker 的 core/evidence——LinkageContractTest 两道防线未动）；本批产品源码零变更（新增文件全部位于测试目标源清单与数据资产目录）。
- 本任务零 testkit 变更（PerfBenchmark.hpp 报告拖挂面直接复用——WP-23-T02 产物；`units/testkit.md` 本任务零触碰，在任务契约 forbiddenFiles 列）。
- 新增数据集 manifest `industrialrobot/testdata/performance/b05-memory-manifest.json`（数据资产非编译面——命中集预期零变化）。
- 文档增量：performance-baseline.md 仅 B-05 复现命令位回填＋§5.5 挂点登记＋§7 修订记录 v0.7（达标数值零私改——70% 照抄 REQUIREMENTS NFR-PERF-04 原文、65% 为 D-05 实现参数照抄单元卡登记）＋traceability 两份新增文档——非编译面，命中集预期零变化。
