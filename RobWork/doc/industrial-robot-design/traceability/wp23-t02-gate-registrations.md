# WP-23-T02 ird_gates 命中集增量登记（预期面）

| 字段 | 值 |
| --- | --- |
| 任务 | WP-23-T02 交互响应基准达标（分支 wp23-t02，base＝redesign-main@1499b71d） |
| 比对口径 | ird_gates 引擎直跑为登记权威（WP-10-T64 先例口径）；**本任务实施段未直跑门禁引擎**（集成构建树由合入批执有——冒烟构建树不含全量目标面），本登记册按构建脚本新增边如实登记**预期命中集**，双态直跑核定随合入批/治理面补做 |
| 结论（预期） | base↔head 归一化命中集恰增下表登记条目，无任何表外预期命中；产品目标链接面零新增边 |
| 责任方 | 登记册（DTB §4.5）回填与直跑核定归 WP-01-T03 治理面（本任务面如实申报） |

## 预期恰增条目（全部为测试面直链——UI-T14/UI-T20/UI-T23/UI-T64 既有测试面边同型）

| # | 预期命中 | 承载语义 |
| --- | --- | --- |
| 1 | sdurws_ird_ui_perf_test → sdurws_ird_execution | WP-23-T02 测试面直链：后台态负载注入经 WorkerSupervisor/ChannelProtocol 公共头驱动真实独立 worker 进程流式评估批次（O-31 测试面直链先例＝sdurws_ird_ui_contract_test 的 project/execution/diagnostics 直链——UI-T14 落地登记同型；LinkageContractTest.PerfTestTargetAndExecutionDirectLinkRegistered_WP23T02_B01 具名钉） |
| 2 | sdurws_ird_ui_perf_test → sdurws_ird_testkit | T-1 允许形态：PerfBenchmark 夹具＋RecordListener 报告设施（全仓测试目标同形态——F-221 同型既有登记） |
| 3 | sdurws_ird_ui_perf_test → sdurws_ird_execution_worker（目标级依赖，非链接） | add_dependencies 构建依赖（worker 可执行＝真进程启动目标，IRD_UI_PERF_WORKER_EXE 路径注入——execution 契约测试 IRD_EXECUTION_WORKER_EXE 同款机制）；是否产生门禁命中随引擎的目标图口径，若命中按本条登记 |

## 附加说明

- 产品库 `sdurws_ird_ui` 链接块**零变化**（仍仅 core/diagnostics＋Qt 三件套——LinkageContractTest 产品块扫描与 BuildRedLineTest include 面扫描两道防线未动）；新增链接边全部落在新增测试目标 `sdurws_ird_ui_perf_test`。
- `sdurws_ird_execution` 公共头消费面：`WorkerSupervisor.hpp`/`ChannelProtocol.hpp`/`Ports.hpp`/`TaskTypes.hpp`（全部为 `include/sdurws/ird/execution/` 公共头——R-2 零私有头）；`evidence`/`project`/`core` 面经其 PUBLIC 传染到达（execution 目标既有 PUBLIC 边），非新增直链。
- 新增数据集 manifest `industrialrobot/testdata/performance/b01-interaction-manifest.json`（数据资产非编译面——命中集预期零变化）。
- testkit 新增公共头 `PerfBenchmark.hpp`（header-only——头文件非链接边，命中集预期零变化；§3.1 模块表 13 公共头登记见 testkit.md v1.6）。
