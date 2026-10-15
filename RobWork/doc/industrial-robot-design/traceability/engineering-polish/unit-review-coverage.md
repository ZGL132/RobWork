# 20 单元首批覆盖边界

目标执行状态与内层跳过以 test-target-matrix.md 为准。此表不是 20 单元审核通过证书。

| 单元 | 本轮集成目标覆盖 | 工程审核边界 |
| --- | --- | --- |
| core | core_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| diagnostics | diagnostics_contract_test, diagnostics_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| drivetrain | drivetrain_contract_test, drivetrain_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| dynamics | dynamics_contract_test, dynamics_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| evidence | evidence_contract_test, evidence_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| execution | execution_contract_test, execution_perf_test, execution_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| io | io_contract_test, io_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| kinematics | kinematics_contract_test, kinematics_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| modeling | modeling_contract_test, modeling_gui_test, modeling_test | 六轴导入/草稿重开；正式 apply 未执行 |
| optimization | optimization_contract_test, optimization_perf_test, optimization_test | 宿主 run 服务接线缺口；真实 S6 未完成 |
| policy | policy_contract_test, policy_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| project | project_contract_test, project_perf_test, project_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| reporting | reporting_contract_test, reporting_test | F-815/F-816 未动态复现 |
| requirements | requirements_contract_test, requirements_gui_test, requirements_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| runtime | runtime_test | F-784 已复现，数值后继待实施 |
| selection | selection_contract_test, selection_test | F-814 仅静态核实，未四象限反例 |
| testkit | testkit_test | 黄金 manifest hash/size 全匹配，不代表所有参考独立性已审 |
| trajectory | trajectory_contract_test, trajectory_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
| ui | ui_contract_test, ui_gui_test, ui_largedata_perf_test, ui_perf_test, ui_test | auto FAIL；三维着色真评估未执行 |
| workflow | workflow_contract_test, workflow_test | 只有既有套件本轮基线；贯穿案例逐字段/跨域闭环未审 |
