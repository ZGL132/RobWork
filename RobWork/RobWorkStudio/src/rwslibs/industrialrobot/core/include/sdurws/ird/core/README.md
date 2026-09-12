# core

本目录为 core 单元公共头保留位（命名空间 sdurws/ird/core）。
本文件不参与编译；产品实现位于 ../src、测试位于 ../test（随任务卡落地）。

2026-09-10（WP-03-T01≙CORE-T01 构建落位）：`sdurws_ird_core` 已由 INTERFACE
占位升级为 STATIC 库（C++17，集成模式链 `sdurw_math`，目标名与别名
`RWS::ird::core` 不变）；gtest 测试目标 `sdurws_ird_core_test` 与 UT-BUILD
红线用例（core/test/BuildRedLineTest.cpp）已注册。具体接口（Identity/
Digest/Units 等十个公共头模块）随 units/core.md §9 的 CORE-T02～T08 逐任务
落地。
2026-09-11（CORE-T02~T08 逐任务落地＋CORE-T10 同步）：公共头现有九个模块——
Errors.hpp（CoreError 唯一异常）、Identity.hpp（六强类型＋AttemptId＋TaskIdentity）、
Digest.hpp（SHA-256＋ContentVersion/ContentIdentity）、Provenance.hpp（五类来源＋
SourcedValue 四态）、Units.hpp（R1 单位表＋Quantity<K>）、Compare.hpp（C4 公式＋C7 转写）、
Evaluation.hpp（四词表）、DiagData.hpp（诊断记录＋可确认发现）、Events.hpp（四类领域事件＋总线接口＋测试内参考总线）。
CORE-T09 产出测试体（core/test/）。接口冻结状态见 units/core.md（v0.11）；验收记录见
traceability/acceptance/CORE-T02~T08。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md；实现验收
记录见 doc/industrial-robot-design/traceability/acceptance/。
