# WP-10-T23（UI-T23）ird_gates 命中集增量登记

| 字段 | 值 |
| --- | --- |
| 任务 | UI-T23 多领域集成收口（分支 ui-t23，base＝redesign-main@7fddd9461519fe697142d03b5de61b9921c79cb1） |
| 比对口径 | ird_gates 引擎直跑（cmake --build --target ird_gates），base↔head 双态 stash 切换法（同一构建树、同一归一化脚本 build/gate_normalize.ps1：行内多命中拆分＋扫描根剥离＋排序去重） |
| 附件 | traceability/builds/wp10-t23/gate-raw-head.txt／gate-raw-base.txt／gate-hitset-head.txt／gate-hitset-base.txt |
| 结论 | base↔head 归一化命中集 **恰增 7 条 SUB**（下表），LIB/R1/R3/R4/R5/T1/T2 六码零变化，无任何表外命中；构建期门禁目标红为 F-019 登记的存量形态（TK-T03 起），非本任务引入 |
| 责任方 | 登记册（DTB §4.5）回填归 WP-01-T03 治理面 |

## 恰增 7 条 SUB（装配层特权边——O-31 既有登记模式，全部为宿主装配/测试面边）

| # | 命中 | 承载语义 |
| --- | --- | --- |
| 1 | sdurws_ird_ui_plugin → sdurws_ird_requirements_plugin（ui->requirements） | UI-T23 三域装配边：宿主装配层消费 O-45 补建的 requirements 装配门面（RequirementsPluginAssembly.hpp——域私有头零触碰，R-2 门面纪律延伸；B1-SPEC §5.1 三接入面消费） |
| 2 | sdurws_ird_ui_plugin → sdurws_ird_kinematics_plugin（ui->kinematics） | 同上：WP-15-T18 出线的 kinematics 装配门面（KinematicsPluginAssembly.hpp）——D8 Jog 会话姿态桥与三接入面消费 |
| 3 | sdurws_ird_studio → sdurws_ird_requirements_plugin（studio->requirements） | 正式产品装配基座同源（WP-24-T08 既有 studio->modeling 同款登记模式——三域推广） |
| 4 | sdurws_ird_studio → sdurws_ird_kinematics_plugin（studio->kinematics） | 同上 |
| 5 | sdurws_ird_ui_contract_test → sdurws_ird_modeling_plugin | 测试面直链（O-31 测试面既定形态——UI-T14 直链 project/execution、UI-T20 直链 runtime 同型）：HostIntegrationContractTest 的多域组合用例消费建模装配门面 |
| 6 | sdurws_ird_ui_contract_test → sdurws_ird_requirements_plugin | 同上（需求域门面——无会话二态与三接入面注册用例） |
| 7 | sdurws_ird_ui_contract_test → sdurws_ird_kinematics_plugin | 同上（运动学域门面——恒空集诚实边界用例） |

## 附加说明

- 新增产品面 TU `ui/plugin/DomainModuleRunner.cpp`（零 Qt 零域 include——遍历编排只见 ui 公共头与 project 命令面）：无 R-3 命中新增、无表外边；该 TU 同源编入 `sdurws_ird_ui_plugin`／`sdurws_ird_studio`／`sdurws_ird_ui_contract_test` 三目标（同源防漂移，WP-24-T08"T03b 装配基座同 TU 复用"先例）。
- `ui/src/IndustrialProjectTree.cpp` 既有 R-3 命中行无变化（`<QTreeWidget>` 既有命中——本任务增量＝`setGroupPlaceholder` 纯逻辑，零新 Qt 头）。
- `ui/include/sdurws/ird/ui/IPluginUiModule.hpp` 前向声明实体修正（裸 `namespace project` → `sdurws::ird::project`，UI-T23 随实现落位修正——原声明与 project 单元实义命名空间脱钩，属既有缺陷；命中集零变化——纯声明实体归位）。
