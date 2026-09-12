# policy

本目录为 policy 单元公共头保留位（命名空间 sdurws/ird/policy）。
源码随对应任务卡（见 doc/industrial-robot-design/units/policy.md §12）落地；本文件不参与编译。

2026-09-11（WP-07-T01≙POL-T01 构建落位）：`sdurws_ird_policy` 已由
INTERFACE 占位升级为 STATIC 库（C++17，链 core；集成模式另链 L1 基线库
sdurw_math/sdurw_kinematics/sdurw_models/sdurw_proximity——R-5 proximity
直链唯一许可方；目标名与别名 `RWS::ird::policy` 不变）；gtest 测试目标
`sdurws_ird_policy_test` 与红线扫描用例（policy/test/BuildRedLineTest.cpp）
已注册。本目录当前不含公共头——共享策略接口随 units/policy.md §12 的
POL-T02 起逐任务落地；验证留痕见 traceability/builds/wp07-t01/。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md。
