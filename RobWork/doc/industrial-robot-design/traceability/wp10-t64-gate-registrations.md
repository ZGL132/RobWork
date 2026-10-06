# WP-10-T64（UI-T64）ird_gates 命中集增量登记

| 字段 | 值 |
| --- | --- |
| 任务 | UI-T64 kinematics 覆盖评估执行通道产品装配（分支 ui-t64，base＝redesign-main@9326060e） |
| 比对口径 | ird_gates 引擎直跑（cmake --build --target ird_gates），base↔head 双态 stash 切换法（同一构建树、同一归一化脚本；留痕 traceability/builds/ui-t64/） |
| 附件 | traceability/builds/ui-t64/（gate 双侧 raw＋hitset） |
| 结论 | base↔head 归一化命中集 **恰增下表登记条目**，无任何表外命中；详数随留痕 gate 双侧文件核定 |
| 责任方 | 登记册（DTB §4.5）回填归 WP-01-T03 治理面 |

## 恰增条目（全部为测试面直链——UI-T14/UI-T20/UI-T23 既有测试面边同型）

| # | 命中 | 承载语义 |
| --- | --- | --- |
| 1 | sdurws_ird_ui_test → sdurws_ird_kinematics_plugin | UI-T64 测试面直链：KinEvaluationChannelTest 的执行器用例消费通道值面（assembly include PUBLIC 面）＋runRegionCoverageComputation/analysisConfigurationDigest（经 plugin 目标 PUBLIC 传递的计算库面）——被测 TU 同源编入（HostCompilePort 先例形态，UI-T46 同型） |
| 2 | sdurws_ird_ui_test → sdurws_ird_requirements | UI-T64 测试面直链：执行器切片消费 SamplingPlanBuilder::digest（计划内容身份域函数——零复制纪律，CON-05 禁伪造身份的测试面实证） |

## 附加说明

- 新增产品面 TU `ui/plugin/KinEvaluationChannel.cpp`（ui 插件私有——执行器）：同源编入 `sdurws_ird_ui_plugin`／`sdurws_ird_studio`／`sdurws_ird_ui_test` 三目标（同源防漂移，HostCompilePort 先例）；消费面＝kinematics 装配门面公共头（KinematicsPluginAssembly.hpp/KinematicsPanelChannels.hpp——R-2 零私有头）＋requirements/runtime 公共头；产品目标链接面零新增边（ui_plugin/studio 对 kinematics_plugin 的装配边为 UI-T23 既有登记）。
- 新增 assembly 公共头 `kinematics/assembly/sdurws/ird/kinematics/KinematicsPanelChannels.hpp`（通道值面——头文件非链接边，命中集零变化）；kinematics plugin 私有 TU 增翻译函数具名外链（KinPanelChannelTranslation.hpp 声明——同单元内部面）。
- 门面方法 `installAssemblyChannels`/`noteAssemblyBackgroundResult`/`bindSessionFacts` 为 kinematics 装配门面（既有边承载）的增量方法——链接边零新增。
