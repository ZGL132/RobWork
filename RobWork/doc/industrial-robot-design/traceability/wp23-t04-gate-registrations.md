# WP-23-T04 ird_gates 命中集增量登记（预期面）

| 字段 | 值 |
| --- | --- |
| 任务 | WP-23-T04 大规模数据加载与分页达标（分支 wp23-t04，base＝origin/redesign-main@24ee6aad） |
| 比对口径 | ird_gates 引擎直跑为登记权威（WP-10-T64 先例口径）；**本任务实施段未直跑门禁引擎**（集成构建树由合入批执有——冒烟构建树不含全量目标面），本登记册按构建脚本新增边如实登记**预期命中集**，双态直跑核定随合入批/治理面补做（WP-23-T02/T03 同款边界申报） |
| 结论（预期） | base↔head 归一化命中集恰增下表登记条目，无任何表外预期命中；产品目标链接面零新增边 |
| 责任方 | 登记册（DTB §4.5）回填与直跑核定归 WP-01-T03 治理面（本任务面如实申报） |

## 预期恰增条目（全部为测试面直链——UI-T14/WP-23-T02 既有测试面边同型）

| # | 预期命中 | 承载语义 |
| --- | --- | --- |
| 1 | sdurws_ird_ui_largedata_perf_test → sdurws_ird_requirements | WP-23-T04 测试面直链：腿 A 任务点 5,000 经 RequirementTypes 值模型＋RequirementCodec canonical 编解码（公共头——R-2 零私有头；O-31 测试面直链先例＝sdurws_ird_ui_contract_test 的 project/execution/diagnostics 直链〔UI-T14〕与 sdurws_ird_ui_perf_test 的 execution 直链〔WP-23-T02〕；LinkageContractTest.LargeDataPerfTestTargetAndDomainDirectLinksRegistered_WP23T04_B03 具名钉） |
| 2 | sdurws_ird_ui_largedata_perf_test → sdurws_ird_kinematics | 测试面直链：腿 B 采样结果 100,000 经 Sampling/Coverage 公共头（产品确定性生成器 generateSampleSet/computeCoverage 真跑＋canonical 载荷编码）——本批同时含该库 src/Sampling.cpp 编码对齐 O(N²)→O(N) 优化（库级链接边零变化） |
| 3 | sdurws_ird_ui_largedata_perf_test → sdurws_ird_optimization | 测试面直链：腿 C 候选摘要 10,000 经 Run/CandidatePatch/Export 公共头（assembleRunResult 工厂＋六工件数据面组装） |
| 4 | sdurws_ird_ui_largedata_perf_test → sdurws_ird_project | 测试面直链：腿 B 归档保存/加载经 ProjectStore/ArchivePort/QueryPort 公共头（begin→writeBatch→finalize＋listRuns/runDir——O-12 唯一存储入口） |
| 5 | sdurws_ird_ui_largedata_perf_test → sdurws_ird_io | 测试面直链：io CSV/原子文件设施（ICsvWriter/ICsvReader/IAtomicFileWriter——三类数据保存/加载通道） |
| 6 | sdurws_ird_ui_largedata_perf_test → sdurws_ird_testkit | T-1 允许形态：PerfBenchmark 夹具＋TempDir＋RecordListener 报告设施（全仓测试目标同形态——F-221 同型既有登记） |

## 附加说明

- 产品库 `sdurws_ird_ui` 链接块**零变化**（LinkageContractTest 产品块扫描与既有 kForbidden 越界扫描两道防线未动）；新增链接边全部落在新增测试目标 `sdurws_ird_ui_largedata_perf_test`（零 Qt 链接——本目标无 Qt 消费面）。
- 五域库公共头消费面（全部 `include/sdurws/ird/<unit>/` 公共头——R-2 零私有头）：requirements（RequirementTypes/Codec）、kinematics（Sampling/Coverage）、optimization（Run/CandidatePatch/Export/Objective/Constraint/EvaluatorPorts/Types）、project（ProjectStore/ArchivePort/QueryPort/PersistenceFormat）、io（Csv/AtomicFile）；core/evidence/rw 面经其 PUBLIC 传染到达（被链目标既有 PUBLIC 边），非新增直链。
- 新增数据集 manifest `industrialrobot/testdata/performance/b03-largedata-manifest.json`（数据资产非编译面——命中集预期零变化）。
- 本批 testkit 零变更（PerfBenchmark/TempDir/RecordListener 全为既有设施复用——T03 先例同款零新增 testkit 头）。
- kinematics 库本体 src/Sampling.cpp 内部实现优化（对齐回查 O(N²)→索引 O(N)，输出字节逐位不变）——链接边/依赖图零变化，命中集预期零变化。
