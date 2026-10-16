# KIN-P4 ird_gates 命中集增量登记

| 字段 | 值 |
| --- | --- |
| 工作包 | KIN-P4 正式产品闭环（宿主服务装配）——分支 kin-p4，base＝kin-p3@e53a0c3f |
| 比对口径 | ird_gates 引擎直跑（head 态）：`cmake -DIRD_ROOT=<industrialrobot> -P cmake/ird_gates.cmake`——head 态零命中（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02）；留痕 traceability/builds/kin-p4/ird-gates.log |
| 结论 | 本包新增两条**测试面直链**（下表），head 态引擎判定零命中（登记边由本表与 `ird_gates_whitelist.cmake` 的 `IRD_TEST_TARGET_EDGES` 表尾追加承载——UI-T64 既有登记形态同型）；产品目标链接面零新增边 |
| 责任方 | 登记册（DTB §4.5）回填归 WP-01-T03 治理面 |

## 登记条目（全部为测试面直链——UI-T64 既有测试面边同型）

| # | 边 | 承载语义 |
| --- | --- | --- |
| 1 | sdurws_ird_ui_test → sdurws_ird_project | KIN-P4 测试面直链：KinHostServicesAssemblyTest 的网关用例消费 project::IProjectQueryPort/CommandResult/CommandStatus 值面（fetchBaseline 基线取数与 submit 信封转译语义自证——被测 TU 同源直编 HostCompilePort 先例形态）；产品库 sdurws_ird_ui 本体零触碰（O-31 常驻红线不变） |
| 2 | sdurws_ird_ui_test → sdurws_ird_io | KIN-P4 测试面直链：导出写出缝用例消费 io::AtomicFile/makeAtomicFileWriter（L-K10 写出半区的产品级真实写出自证——临时目录原子写＋回读一致＋目录代建） |

## 附加说明

- 新增产品面 TU `ui/plugin/KinHostServicesAssembly.cpp`（ui 插件私有——宿主服务装配半区）：同源编入 `sdurws_ird_ui_plugin`／`sdurws_ird_studio`／`sdurws_ird_ui_test`（守卫块——集成树专属，project/io 符号面）三目标（同源防漂移先例）；消费面＝kinematics 公共头（Commands.hpp/KinTypes.hpp）＋project/io 公共头＋modeling 公共头（RobotDesignCodec/PoseSet）——R-1/R-2 零私有头；链接边零新增（ui_plugin/studio 对各域 plugin 的装配边为 UI-T23/ASM-STUDIO 既有登记）。
- assembly 公共头 `KinematicsPanelChannels.hpp` 增列七缝（ikSolver/fkEvaluator/sessionPose/commandHandler/exportWriter/configPersist/homePose）与结构化结果投影值（KinChannelSolveResult/KinChannelTaskPointOutcome）——头文件非链接边，命中集零变化；翻译单点在 `KinematicsPluginAssembly.cpp`（同单元消费私有头合法）。
- 门面增量方法 `applySessionSolveOutcome`/`clearCommandHandler` 为 kinematics 装配门面（既有边承载）的表尾追加——链接边零新增。
- 诊断透传 code 语义（`KinProjectCommandGateway::submit` 非提交态回显的 `core::DiagCode` 承载对端 status 词表 token——project 权威词表直投非私造码）随 units/kinematics.md §14.6 增量修订登记。
