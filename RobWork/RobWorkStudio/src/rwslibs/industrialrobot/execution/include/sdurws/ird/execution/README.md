# execution

本目录为 execution 单元公共头保留位（命名空间 sdurws/ird/execution）。
源码随对应任务卡（见 doc/industrial-robot-design/units/execution.md §12）落地；本文件不参与编译。

2026-09-10：第一批详设已编写；任务编排、接口冻结前置与实现状态见 doc/industrial-robot-design/traceability/phase-one-readiness.md。本文不表示库已实现或测试已通过。

2026-09-18（EX-T01 构建落位复核）：任务卡指向 §12 确认零偏差（§9→§12 修正已于 2026-09-10 版完成）；库本体已升级 STATIC 并注册 `_test`/`_contract_test` 目标（见 execution/CMakeLists.txt）。本目录契约头（§3.1 组成表 12 头）随 EX-T02+ 逐任务落地，此前仅本 README——"不参与编译/不表示已实现"声明保留。
