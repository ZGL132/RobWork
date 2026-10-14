# WP-23-T08 ird_gates 命中集增量登记（预期面）

| 字段 | 值 |
| --- | --- |
| 任务 | WP-23-T08 优化运行基准达标（分支 wp23-t08，base＝redesign-main@71c44923） |
| 比对口径 | ird_gates 引擎直跑为登记权威（WP-10-T64 先例口径）；**本任务实施段未直跑门禁引擎**（集成构建树由合入批执行——冒烟构建树不含全量目标面），本登记册按构建脚本增量如实登记**预期命中集**，双态直跑核定随合入批/治理面补做（T03/T06/T07 先例口径） |
| 结论（预期） | base↔head 归一化命中集恰增下表登记条目（预期恰增 1 条物理命中——T07 批直跑核定基线 21 项 → 预期 22 项），无任何表外预期命中；产品目标（sdurws_ird_optimization／sdurws_ird_optimization_plugin）链接面零新增边 |
| 责任方 | 登记册（DTB §4.5）回填与直跑核定归 WP-01-T03 治理面（本任务面如实申报） |

## 预期恰增条目（测试面引用——T-1 允许形态）

| # | 预期命中 | 承载语义 |
| --- | --- | --- |
| 1 | sdurws_ird_optimization_perf_test → sdurws_ird_ui | 既有测试目标链接面增列（WP-23-T08 B-07 运行期 NFR-PERF-01 抽查的五类交互无头模型层驱动——WP-23-T02 B-01 同款驱动形态）：ASM-PLUG 第十登记边 optimization→ui（ird_gates_whitelist.cmake 在册行／BuildGraphContractTest.kEdgeUnits 在册项——P-OPT-10 消账批登记）的**测试面消费**，单元级边零新增；产品库 sdurws_ird_optimization 链接块零变化（产品目标不在本批触碰面） |

## 附加说明

- 本批为**既有测试目标增列**批（T06 纯源清单增列形态的变体——目标 sdurws_ird_optimization_perf_test 为 WP-23-T07 新建在册目标，本批增列第二 TU `perf_test/OptRunTimeBenchmarkTest.cpp`＋链接面增列 sdurws_ird_ui）：**零新增 add_executable**，目标级检查面（SUB→optimization／SUB→testkit／T1→testkit——T07 登记册两条目三检查面）保持 T07 直跑核定的既有命中，不重复登记。
- 产品库 `sdurws_ird_optimization` 链接块**零变化**（仍仅九条登记边＋ASM-PLUG 第十边 ui——BuildGraphContractTest.UnitEdgesRegisteredOnly（登记存在性）与配置期守卫两道防线未动）；新增引用全部落在既有测试目标 `sdurws_ird_optimization_perf_test` 的单行链接语句。
- BuildGraphContractTest **零改动**：UnitEdgesNineRegisteredAndClosed 的 allowed 集已含 `sdurws_ird_optimization_perf_test`（T07 批登记）与 `sdurws_ird_ui`（kEdgeUnits——ASM-PLUG 第十边）；NoTestkitEdgeOnProductTarget 的测试目标允许集已含本目标（T07 批登记）——本批零契约测试扩登。
- **Qt 传递依赖**：sdurws_ird_ui 经其 PUBLIC 接口传递 Qt6::Core/Gui/Widgets——门禁对 target_link_libraries 的文本级静态解析只见显式引用 `sdurws_ird_ui`（Qt6 无显式引用行进入本目标链接语句），预期无 Qt 检查面新增命中；QCoreApplication 由新 TU 用例体内自持构造/析构，目标 main 入口非 Qt 形态不变（B-06 用例零影响）。
- 本目标**零 add_dependencies**（B-07 为进程内编排基准——不启动 worker 子进程；IRD_*_WORKER_EXE 类路径注入宏不存在），预期无 T03 登记册条目 3 的目标级依赖命中。
- 本任务零 testkit 变更（PerfBenchmark.hpp 报告拖挂面直接复用——WP-23-T02 产物；`units/testkit.md` 本任务零变更，在任务契约 forbiddenFiles 列）。
- 新增数据集 manifest `industrialrobot/testdata/performance/b07-opt-run-time-manifest.json`（数据资产非编译面——命中集预期零变化）。
- 文档增量：performance-baseline.md 仅 B-07 复现命令位回填＋§5.7 挂点登记＋§7 修订记录 v0.9（达标数值零私改——8 小时/10,000 Quick＋100 Verified 照抄 REQUIREMENTS NFR-PERF-06 原文、200 ms/2 s 照抄 NFR-PERF-01 原文、1×10⁻¹² 照抄附录 D 第 8 项 P-01 冻结值、ε_abs=0 为本卡黄金算例声明值随 manifest/报告登记）＋traceability 两份新增文档——非编译面，命中集预期零变化。
- 契约文件本身（tasks/foundation/WP-23-T08.json planned→ready 批）为非编译面——命中集预期零变化（T06/T07 契约编译放行先例同口径）。
