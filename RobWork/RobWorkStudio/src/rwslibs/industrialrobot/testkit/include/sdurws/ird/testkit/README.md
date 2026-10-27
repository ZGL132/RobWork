# testkit

testkit 单元公共头根（命名空间 `sdurws::ird::testkit`）。接口冻结与任务拆分见 `doc/industrial-robot-design/units/testkit.md`（§3.1 组成、§9 任务表）。

2026-09-10 随 WP-02-T01（≙TK-T01）落地首个公共头 `TestPaths.hpp`（黄金数据根两级解析：环境变量 `SDURWS_IRD_TESTDATA_DIR` 优先，回落编译期默认 `industrialrobot/testdata`，两皆无则 `env-unavailable`）与统一错误类型 `TestKitError`（原设计位于 Dataset.hpp，因 TestPaths 先行落地而前置定义于此，见单元卡变更记录 v0.3）。库目标 `sdurws_ird_testkit` 已升级为 STATIC（C++17、链 core），测试目标 `sdurws_ird_testkit_test` 注册（gtest 经 vcpkg `find_package(GTest CONFIG REQUIRED)` 接入——DTB §5.5 定稿）。

## 当前头清单（2026-10-27 核对）

| 头 | 内容 | 层 |
| --- | --- | --- |
| `TestPaths.hpp` | 数据根两级解析＋`TestKitError` 统一错误类型（TK-T01） | 库 |
| `JsonLite.hpp` | 受限 JSON（TK-T02） | 库 |
| `Dataset.hpp` | 黄金数据集清单装载＋完整性（TK-T03） | 库 |
| `ToleranceProfile.hpp` | 容差档案装载/解析（TK-T04） | 库 |
| `Check.hpp` | 数值断言＋CompareDetail（TK-T05） | 库 |
| `SetCheck.hpp` | 集合/顺序断言（TK-T06） | 库 |
| `ContractCheck.hpp` | 契约断言五族＋谓词组合（TK-T07） | 库 |
| `Fixture.hpp` | TempDir/ReproRecord/DeterministicEnv（TK-T08） | 库 |
| `Fault.hpp`/`ProcessRunner.hpp` | 故障注入原语；Windows Job 子进程树、超时与事件观察（TK-T09/T11） | 库 |
| `PerfBenchmark.hpp`/`ReproBenchmark.hpp` | 单调时钟采样/P95；多轮集合及顺序复现验证，不代替产品算法 | 库 |
| `Report.hpp` | TestRecord/六类 outcome/聚合 Report/登记表（TK-T10） | 库 |
| `gtest/AssertMacros.hpp` | IRD_EXPECT_*／IRD_TEST_INFO 宏（TK-T05/T10） | gtest 适配 |
| `gtest/GoldenFixture.hpp` | 夹具生命周期六步 gtest 形态（TK-T08） | gtest 适配 |
| `gtest/RecordListener.hpp` | TestRecordListener＋`installTestRecordListener(argc, argv)`——聚合产出 `ird-test-report.json`（TK-T10） | gtest 适配 |

分层红线：`gtest/` 适配层含 gtest 头，仅供消费方 `_test`/`_contract_test` 目标包含；**库本体零 gtest**（testkit.md §2.4 D-06）。

## 消费方接入要点

- **测试 main 自持**（DTB §5.5）：`installTestRecordListener(argc, argv)`（RecordListener.hpp，TK-T10）或链接 `GTest::gtest_main` 二选一；前者额外在进程工作目录聚合产出 `ird-test-report.json`（与 gtest XML 并存，§7.2；`--ird_report=<path>` 可指定落盘位置）。CI 按四类分列统计，`envUnavailable`/`datasetInvalid`/`notRun` 非零＝不可判定（§7.5）。
- **追溯登记**：测试体首行 `IRD_TEST_INFO("KIN-12", {"AT-03"}, DatasetRef{...})`（§7.3）。
- **core 接入示例**（附录 A.1 样板）：见 `test/CoreIntegrationExampleTest.cpp`（UnitsTolerance.MmToDegRoundtrip_AppendixD_Item9）——core 侧 `sdurws_ird_core_test` 接入时按该样板复制（core 类型＋testkit 断言＋IRD_TEST_INFO 追溯）。
- **分发红线**（§3.6/NFR-SEC-05）：testkit 库、测试程序、`testdata/`、`tools/` 一律不进产品安装包；安装树扫描建议稿见 `tools/install-scan/ird-install-scan.ps1`（门禁实施归 WP-01/WP-24）。

集合断言先精确匹配已有身份，只有两侧无身份的元素进入 Boost 数值最大匹配；数值对照唯一委托 core。`parameters` 缺省为 `{}`；完整性检查包含生成入口和 `generate/` 下全部来源文件。lint 真正装载被引用的容差档案并检查需求/AT字典，不能以 lint 成功证明 expected 的数学正确性。

进程支撑已有真实实现与回归；按现有接口，非零退出统一为 `Crashed`，该形态的 `exitCode` 固定为零，消费者必须先看 `kind`。具体 CLI 退出码验证另用原生子进程驱动。安装扫描仍为建议工具，尚不代表正式打包门禁已接入。

种子按所有者批准保留完整 uint64 JSON 整数；连续量 `.number` 写法保留，精确整数用 `setUnsigned/tryUnsigned`，相关测试消费者必须重编。失败报告非有限值用 `"NaN"`/`"+Inf"`/`"-Inf"`，长度缺失用 `null`；一般 JSON writer 拒绝非有限 number。第一轮整改的实施与兼容边界见设计区 `traceability/testkit-review-r1/README.md`；实施自查不代替独立验收。早期任务原始留痕仍见 `traceability/builds/wp02-t01~t10/`。
