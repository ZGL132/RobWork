# evidence

本目录为 evidence 单元公共头保留位（命名空间 sdurws/ird/evidence）。
源码随对应任务卡（见 doc/industrial-robot-design/units/evidence.md §12）落地；本文件不参与编译。

2026-09-11（WP-05-T01≙EV-T01 构建落位）：`sdurws_ird_evidence` 已由
INTERFACE 占位升级为 STATIC 库（C++17，仅链 core——目标名与别名
`RWS::ird::evidence` 不变）；gtest 测试目标 `sdurws_ird_evidence_test` 与
红线扫描用例（evidence/test/BuildRedLineTest.cpp）已注册。本目录当前
不含公共头——证据契约接口随 units/evidence.md §12 的 EV-T02 起逐任务
落地；验证留痕见 traceability/builds/wp05-t01/。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md。
