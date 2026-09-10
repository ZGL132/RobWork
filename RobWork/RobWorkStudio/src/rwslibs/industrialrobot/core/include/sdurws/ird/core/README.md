# core

本目录为 core 单元公共头保留位（命名空间 sdurws/ird/core）。
本文件不参与编译；产品实现位于 ../src、测试位于 ../test（随任务卡落地）。

2026-09-10（WP-03-T01≙CORE-T01 构建落位）：`sdurws_ird_core` 已由 INTERFACE
占位升级为 STATIC 库（C++17，集成模式链 `sdurw_math`，目标名与别名
`RWS::ird::core` 不变）；gtest 测试目标 `sdurws_ird_core_test` 与 UT-BUILD
红线用例（core/test/BuildRedLineTest.cpp）已注册。具体接口（Identity/
Digest/Units 等十个公共头模块）随 units/core.md §9 的 CORE-T02～T08 逐任务
落地，本目录当前不含公共头。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md；实现验收
记录见 doc/industrial-robot-design/traceability/acceptance/。
