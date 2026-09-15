# diagnostics

本目录为 diagnostics 单元公共头保留位（命名空间 sdurws/ird/diagnostics）。
本文件不参与编译；产品实现位于 ../src、测试位于 ../test（随任务卡落地）。
任务拆分与逐任务落位见 doc/industrial-robot-design/units/diagnostics.md §11；
接口契约以 units/diagnostics.md §3~§9 为准。

2026-09-10（WP-09-T02≙DIAG-T02 构建落位）：`sdurws_ird_diagnostics` 已由 INTERFACE
占位升级为 STATIC 库（C++17、PUBLIC 链 core、零 Qt——D-01），目标名与别名
`RWS::ird::diagnostics` 不变；gtest 测试目标 `sdurws_ird_diagnostics_test` 与
`sdurws_ird_diagnostics_contract_test` 按 development-task-breakdown §5.5 注册。

2026-09-15（DIAG-T03~T10 逐任务落地＋DIAG-T11 指向核对同步）：公共头现有九个契约头
模块——Errors.hpp（§9.0 错误类型）、DiagCodes.hpp（§4.3/§4.4 词表＋§4.5/§9.1 稳定码
注册表＋§4.6 内置码表）、Catalog.hpp（§4.2 信封＋§6.2 生命周期与容量护栏＋§6.4 去重
排序＋§9.7 目录/投影/sink）、Factory.hpp（§9.2 工厂＋ErrorCodeTranslator）、
Confirmable.hpp（§5/§9.3 可确认诊断服务）、Aggregation.hpp（§6.3/§6.4/§9.4 聚合）、
Logging.hpp（§7.1~§7.5/§9.6 两级日志）、Redaction.hpp（§7.7/§9.5 脱敏）、
CrashReport.hpp（§7.6 崩溃诊断文件）——与 units/diagnostics.md §3.1 模块表逐行对应；
测试体（§10 DT-* 验证矩阵逐组落位表）见 ../test/README.md。接口冻结状态与实现口径
登记见 units/diagnostics.md（§14.4 变更记录）；验证留痕见
traceability/builds/wp09-t02~t11；验收记录见 traceability/acceptance/。

任务编排、接口冻结前置与实现状态见
doc/industrial-robot-design/traceability/phase-one-readiness.md。
本文不表示接口已冻结；实现与测试通过状态以 units/diagnostics.md §14.4 与上述留痕为准。
