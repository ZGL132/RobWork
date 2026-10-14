# WP-23-T07 ird_gates 命中集增量登记（预期面）

| 字段 | 值 |
| --- | --- |
| 任务 | WP-23-T07 并行吞吐达标（分支 wp23-t07，base＝redesign-main@6e424a32） |
| 比对口径 | ird_gates 引擎直跑为登记权威（WP-10-T64 先例口径）；**本任务实施段未直跑门禁引擎**（集成构建树由合入批执行——冒烟构建树不含全量目标面），本登记册按构建脚本新增边如实登记**预期命中集**，双态直跑核定随合入批/治理面补做 |
| 结论（预期） | base↔head 归一化命中集恰增下表登记条目，无任何表外预期命中；产品目标（sdurws_ird_optimization／sdurws_ird_optimization_plugin）链接面零新增边 |
| 责任方 | 登记册（DTB §4.5）回填与直跑核定归 WP-01-T03 治理面（本任务面如实申报） |

## 预期恰增条目（全部为测试面引用——T-1 允许形态，WP-23-T02/T03/T05 新建 perf 目标同型）

| # | 预期命中 | 承载语义 |
| --- | --- | --- |
| 1 | sdurws_ird_optimization_perf_test → sdurws_ird_optimization | 新增测试目标链接被测产品目标（optimization 单元内部引用——同单元目标引用不构成跨单元边；BuildGraphContractTest.UnitEdgesNineRegisteredAndClosed 白名单随本目标扩登为"本单元五目标"——WP-23-T07 登记） |
| 2 | sdurws_ird_optimization_perf_test → sdurws_ird_testkit | T-1 允许形态：PerfBenchmark 报告拖挂面＋RecordListener 报告设施（BuildGraphContractTest.NoTestkitEdge 允许集随本目标扩登——`_perf_test` 与 `_test`/`_contract_test` 同属测试目标允许集；testkit 边第二检查面预期同 WP-23-T03 登记册条目 2 口径） |

## 附加说明

- 本批为**新建测试目标**批（WP-23-T02 ui_perf_test／T03 execution_perf_test／T05 project_perf_test 同型先例——非 T06 的纯源清单增列形态）：`optimization/CMakeLists.txt` 新增 `add_executable(sdurws_ird_optimization_perf_test perf_test/TestMainPerf.cpp perf_test/ParallelThroughputBenchmarkTest.cpp)`＋编译属性＋单行链接语句＋POST_BUILD DLL 拷贝＋`ird_add_gtest`＋ctest LABELS "ird"。
- 产品库 `sdurws_ird_optimization` 链接块**零变化**（仍仅九条登记边＋ASM-PLUG 第十边 ui——BuildGraphContractTest.UnitEdgesRegisteredOnly（登记存在性）与配置期守卫两道防线未动）；新增引用全部落在新增测试目标 `sdurws_ird_optimization_perf_test`。
- 本目标**零 add_dependencies**（B-06 为进程内编排基准——不启动 worker 子进程，与 T03/T06 的真进程基准唯一结构差异；IRD_*_WORKER_EXE 类路径注入宏不存在），预期无 T03 登记册条目 3 的目标级依赖命中。
- 本目标**零 Qt**（L2 计算内核基准——D-01；不链 sdurws_ird_optimization_plugin，与 ui perf 目标的形态差异同 execution perf 目标）。
- 契约测试面同步两处（同批）：`BuildGraphContractTest.cpp` 的 UnitEdgesNineRegisteredAndClosed allowed 集增列 `sdurws_ird_optimization_perf_test`（四目标→五目标）＋NoTestkitEdgeOnProductTarget 的 onTestTargetLine 允许集增列同名（登记性扩登，非语义放宽——T-1"仅测试目标"口径不变）。
- 本任务零 testkit 变更（PerfBenchmark.hpp 报告拖挂面直接复用——WP-23-T02 产物；`units/testkit.md` 本任务零触碰，在任务契约 forbiddenFiles 列）。
- 新增数据集 manifest `industrialrobot/testdata/performance/b06-parallel-throughput-manifest.json`（数据资产非编译面——命中集预期零变化）。
- 文档增量：performance-baseline.md 仅 B-06 复现命令位回填＋§5.6 挂点登记＋§7 修订记录 v0.8（达标数值零私改——≥4× 中位吞吐照抄 REQUIREMENTS NFR-PERF-05 原文、归约容差 1×10⁻¹² 照抄附录 D 第 8 项 P-01 冻结值、ε_abs=0 为本卡黄金算例声明值随 manifest/报告登记）＋traceability 两份新增文档——非编译面，命中集预期零变化。
