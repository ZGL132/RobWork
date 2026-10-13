# WP-23-T03 ird_gates 命中集增量登记（预期面）

| 字段 | 值 |
| --- | --- |
| 任务 | WP-23-T03 取消时序达标（分支 wp23-t03，base＝redesign-main@a76fd7b8） |
| 比对口径 | ird_gates 引擎直跑为登记权威（WP-10-T64 先例口径）；**本任务实施段未直跑门禁引擎**（集成构建树由合入批执行——冒烟构建树不含全量目标面），本登记册按构建脚本新增边如实登记**预期命中集**，双态直跑核定随合入批/治理面补做 |
| 结论（预期） | base↔head 归一化命中集恰增下表登记条目，无任何表外预期命中；产品目标（sdurws_ird_execution／sdurws_ird_execution_worker）链接面零新增边 |
| 责任方 | 登记册（DTB §4.5）回填与直跑核定归 WP-01-T03 治理面（本任务面如实申报） |

## 预期恰增条目（全部为测试面引用——T-1 允许形态，execution 单元既有测试目标同型）

| # | 预期命中 | 承载语义 |
| --- | --- | --- |
| 1 | sdurws_ird_execution_perf_test → sdurws_ird_execution | 新增测试目标链接被测产品目标（execution 单元内部引用——同单元目标引用不构成跨单元边；LinkageContractTest.UnitEdgesRegisteredOnly 白名单随本目标扩登为"本单元五目标"——WP-23-T03 登记） |
| 2 | sdurws_ird_execution_perf_test → sdurws_ird_testkit | T-1 允许形态：PerfBenchmark 报告面＋RecordListener 报告设施（LinkageContractTest.NoTestkitEdge 允许集随本目标扩登——`_perf_test` 与 `_test`/`_contract_test` 同属测试目标允许集；testkit 边第二检查面预期同 WP-23-T02 登记册条目 2 口径） |
| 3 | sdurws_ird_execution_perf_test → sdurws_ird_execution_worker（目标级依赖，非链接） | add_dependencies 构建依赖（worker 可执行＝真进程启动目标，IRD_EXECUTION_PERF_WORKER_EXE 路径注入——契约测试 IRD_EXECUTION_WORKER_EXE 同款机制）；是否产生门禁命中随引擎的目标图口径，若命中按本条登记（WP-23-T02 登记册条目 3 同型） |

## 附加说明

- 产品库 `sdurws_ird_execution` 与 `sdurws_ird_execution_worker` 链接块**零变化**（仍仅 core/evidence/project 三条登记边＋worker 的 core/evidence——LinkageContractTest.UnitEdgesRegisteredOnly/RegisteredEdgesPresent 与配置期守卫两道防线未动）；新增引用全部落在新增测试目标 `sdurws_ird_execution_perf_test`。
- 本任务零 testkit 变更（PerfBenchmark.hpp 报告拖挂面直接复用——WP-23-T02 产物；`units/testkit.md` 本任务零触碰，在任务契约 forbiddenFiles 列）。
- 新增数据集 manifest `industrialrobot/testdata/performance/b02-cancel-manifest.json`（数据资产非编译面——命中集预期零变化）。
- 文档增量：performance-baseline.md 仅 B-02 复现命令位回填＋§5.2 挂点登记＋§7 修订记录 v0.4（达标数值零私改——2 s/10 s 照抄 REQUIREMENTS NFR-PERF-02 原文）＋traceability 两份新增文档——非编译面，命中集预期零变化。
