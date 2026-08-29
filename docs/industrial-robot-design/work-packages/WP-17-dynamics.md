# WP-17 动力学实施计划

**目标：** 用刚体模型和递归牛顿—欧拉计算关节侧动力需求，并用正动力学校核响应。

**阶段/发布：** 阶段 C，R1；关节侧结果与候选传动无关。

**需求与契约：** DYN-01～08；AT-07；引用 `architecture/domain-model.md`、`execution-model.md`、`testing-contract.md`。

**拥有目录：** `industrialrobot/plugins/dynamics/` 及测试。外力坐标、摩擦、积分器和功率语义必须在任务开始前冻结。

**输入/输出：** 输入为 `RobotDesign`、`TrajectoryPlan`、`LoadCase` 和摩擦假设；输出为关节侧 `DynamicResult`、功率/能量包络和证据等级。

## 任务

1. 冻结外力参考坐标系、摩擦符号、零速处理、控制输入插值和积分器配置。
2. 实现重力、惯性、末端负载、外力、黏性和库仑摩擦逆动力学。
3. 输出类型化广义力、速度、功率、峰值窗、完整循环 RMS 和 `W+ = ∫ max(P_joint(t), 0)dt`。
4. 接入 RobWorkSim 正动力学，记录步长、初始状态、控制输入和收敛诊断。
5. 缺少物性或摩擦数据时降级证据，不生成精确结论。
6. 将结果交给 WP-18 映射并验证候选传动不改变关节侧结果。

## 验证

前置：WP-06、WP-08、WP-16；

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_dynamics_test$'
```

必须提交二连杆、静态重力矩、正动力学收敛和完整循环积分报告。

## 迁移与删除

旧动力学实现只保留解析对照所需的最小夹具；新链路验收后删除重复功率和摩擦计算。

## 独立评审

由动力学/驱动工程师独立复核公式、坐标系、积分、峰值窗和证据等级。

## 退出条件

DYN-01～08、AT-07 通过；类型、单位、积分、峰值窗和证据等级符合需求第 15.3 节。

## 任务卡索引

- [WP-17-T01 动力学语义冻结](../agent-tasks/WP-17-T01-semantic-freeze.md)
- [WP-17-T02 逆动力学计算](../agent-tasks/WP-17-T02-inverse-dynamics.md)
- [WP-17-T03 广义力功率与能量](../agent-tasks/WP-17-T03-power-energy.md)
- [WP-17-T04 RobWorkSim 正动力学](../agent-tasks/WP-17-T04-forward-dynamics.md)
- [WP-17-T05 物性与摩擦数据不足](../agent-tasks/WP-17-T05-insufficient-data.md)
- [WP-17-T06 动力学到传动映射](../agent-tasks/WP-17-T06-drivetrain-contract.md)
