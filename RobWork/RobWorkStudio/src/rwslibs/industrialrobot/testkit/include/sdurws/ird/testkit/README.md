# testkit

testkit 单元公共头根（命名空间 `sdurws::ird::testkit`）。接口冻结与任务拆分见 `doc/industrial-robot-design/units/testkit.md`（§3.1 组成、§9 任务表）。

2026-09-10 随 WP-02-T01（≙TK-T01）落地首个公共头 `TestPaths.hpp`（黄金数据根两级解析：环境变量 `SDURWS_IRD_TESTDATA_DIR` 优先，回落编译期默认 `industrialrobot/testdata`，两皆无则 `env-unavailable`）与统一错误类型 `TestKitError`（原设计位于 Dataset.hpp，因 TestPaths 先行落地而前置定义于此，见单元卡变更记录 v0.3）。库目标 `sdurws_ird_testkit` 已升级为 STATIC（C++17、链 core），测试目标 `sdurws_ird_testkit_test` 注册（gtest 经 vcpkg `find_package(GTest CONFIG REQUIRED)` 接入——DTB §5.5 定稿）。其余公共头（JsonLite/Dataset/ToleranceProfile/Check/SetCheck/ContractCheck/Fixture/Fault/ProcessRunner/Report 及 gtest 适配层）随 §9 后续任务逐个落地；本 README 不表示未落地接口已实现或其测试已通过。
