# reporting

本目录为 reporting 单元公共头保留位（命名空间 sdurws/ird/reporting）。
源码随对应任务卡（见 doc/industrial-robot-design/units/reporting.md §11）落地；本文件不参与编译。

2026-09-10：第一批详设已编写；任务编排、接口冻结前置与实现状态见 doc/industrial-robot-design/traceability/phase-one-readiness.md。本文不表示库已实现或测试已通过。

2026-09-19：RPT-T01（≙WP-12-T02）落位复核——任务卡指向已核对：本文件指向 units/reporting.md §11（阶段 A/B/C 实现任务拆分），与 project 家族修正后口径同型（project README→§12）；单元卡 v0.1~v0.3 所载"已指向本文 §9，实测一致"与磁盘不符（PRJ-T01 家族同型偏差），卡文两处（构建落位行＋§3.1 行）已随 reporting.md v0.4 更正，零悬空指向（本核对结论为 RPT-T13 文档同步的前置事实）。同日库目标 `sdurws_ird_reporting` 升级 STATIC（src/ 空起步锚点翻译单元 Reporting.cpp；测试目标 `sdurws_ird_reporting_test`/`_contract_test` 按 DTB §5.5 注册，落位期最小构建冒烟用例），本文件仍不参与编译；§3.1 契约头随 RPT-T02+ 落地，本目录与库目标不表示公共契约已实现或测试已通过（证据：traceability/builds/wp12-t01/）。

2026-09-20：RPT-T13（文档与门禁同步）README 复核——**指向核对零偏差**：本文件指向 units/reporting.md §11（阶段 A/B/C 实现任务拆分），与卡文 §3.1 README 行现文（"指向本文 §11〔任务拆分〕——v0.4 RPT-T01 复核更正"）及 §11 现文（该节真实存在、含 RPT-T13 治理行）三方一致；RPT-T01 acceptance 4 核对结论（前置事实）复审通过，无回退、无第二指向。**实现状态段同步**（取代上行"§3.1 契约头随 RPT-T02+ 落地"前瞻句）：本目录现状＝13 个契约头（Errors/Identity/ReportModel/Sections/SectionProvider/Builder/Render/Consistency/Export/Archive/Bundle/ModelSummary/TaskStatusSource——与卡文 §3.1 组成表逐行对应，RPT-T02~T12 依次落地，登记版本链 v0.4~v0.15，验证留痕 traceability/builds/wp12-t01~t12/）＋本 README；src/ 实现 12 翻译单元＋2 私有头（ReportCodec.hpp/RenderText.hpp——R-2 不出 include/）。**声明保留**：本文件不参与编译；本目录与库目标不表示公共契约已实现或测试已通过——实现与测试通过状态以 units/reporting.md §14.4 变更记录与 builds 留痕为准。核对结论留痕：traceability/builds/wp12-t13/README-audit.md（RPT-T13 证据）。
