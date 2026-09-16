# project

本目录为 project 单元公共头保留位（命名空间 sdurws/ird/project）。
源码随对应任务卡（见 doc/industrial-robot-design/units/project.md §12）落地；本文件不参与编译。

2026-09-10：第一批详设已编写；任务编排、接口冻结前置与实现状态见 doc/industrial-robot-design/traceability/phase-one-readiness.md。本文不表示库已实现或测试已通过。

2026-09-15：PRJ-T01（≙WP-04-T01）落位复核——任务卡指向已核对：本文件指向 units/project.md §12（阶段 A 实现任务拆分），原 §9 指向已于 2026-09-10 版修正，确认无误、零偏差（DTB §2.5 WP-04-T01 验收列"project README 任务卡指向 §9→§12 修正"消账）。同日库目标 `sdurws_ird_project` 升级 STATIC（src/ 空起步锚点翻译单元 Project.cpp），本文件仍不参与编译；§3.1 契约头随 PRJ-T04+ 落地，本目录与库目标不表示公共契约已实现或测试已通过。

2026-09-17：PRJ-T16（≙WP-04-T16）文档与门禁同步复核——任务卡指向再核对：本文件指向 units/project.md §12 保持一致、零偏差（§12 PRJ-T16 行产物列即"本文、project/include/.../README.md"）。目录现状与单元卡 §3.1 组成表对齐：阶段 A 公共头 8 个已全部落地（StoreTypes/ProjectStore/QueryPort/CommandService/UndoRedo/DraftService/ArchivePort/PersistenceFormat——各头落位任务见 §15.4 v0.4～v0.16 登记）；ResourceRefs.hpp/PackageService.hpp/Upgrade.hpp 为 §3.1 表内标注的阶段 B 落位项（接口先冻结于单元卡 §5.7/§5.8，零消费者不预建）；StoreTypes.hpp 的 StorePath 类型零消费者暂不落位（§15.4 v0.10 登记）——均为设计内节奏而非偏差。阶段 A 测试结论（单元 254/255＋1 显式跳过、契约 28/28，PRJ-T15 留痕 traceability/builds/wp04-t15/）以该留痕为准，本文件仍不参与编译、不自行宣布实现通过。
