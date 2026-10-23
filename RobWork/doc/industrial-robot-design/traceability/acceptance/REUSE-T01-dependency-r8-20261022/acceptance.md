新增公开依赖API独立验收：PASS

冻结 SHA：`bb997f1cfb89c8d8eb24b457d2c82d4c0711af44`。独立冻结checkout、独立新建构建目录，在VS2022 x64开发环境配置/Release构建并使用绝对路径实跑 `--gtest_filter=ReuseDependencyAudit.*`；configure/build/run均exit0。

本次仅3项：passed=3、failed=0、skipped=0、notRun=0，envUnavailable/datasetInvalid=0。XML与原生JSON逐项名称、状态与汇总一致；与原probe-r8的3项名称/结果一致。没有实跑原19项或r7三项，不写成25项全跑。

核对结论：公开 `Boost::endian`、`Boost::property_tree` 均为真实CONFIG导入INTERFACE目标，解析至既有vcpkg x64安装，版本Boost1.92.0。2份探测源码/CMake规范化摘要与11份已安装依赖原始摘要均匹配。Endian通过指定u32大小端逐字节及u64负零整数位型通道；PropertyTree确实丢失所测值类型/空形状，且接受所测BOM与重复键输入。两项限制证据不是产品JSON完整profile或其他库缺失证明。

结论只覆盖3项公开API探测，不是生产适配、完整codec/数值/性能验收、全T01退出或迁移批准。使用既有D盘上游导出库和安装依赖，其摘要仅标识本机内容；没有重建整个上游来证明源码可复现。原生报告gitCommit为空，冻结身份由外部精确SHA、独立干净检出、构建和产物摘要绑定，未改写原报告。

全新Temp构建产生MSB8029临时目录增量构建警告，实际构建完成且无编译/链接错误。父树、产品/框架/vcpkg及历史probe报告均未修改，未提交推送合入或派子代理。治理门禁与第八批关系审查不在本次仅API验收范围。

完整结构化结论在 `acceptance.json`；实跑原件为 `gtest.xml`、`ird-test-report.json`、`tests.log`，环境与构建日志均在本目录。
