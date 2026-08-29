# WP-16 轨迹规划实施计划

**目标：** 从需求任务和 IK 候选生成可复核的连续路径与时间参数化轨迹。

**阶段/发布：** 阶段 C，R1；轨迹结果必须可被 WP-17 消费。

**需求与契约：** TRJ-01～08；AT-04、AT-06、AT-18～19；引用 `architecture/public-interfaces.md`、`execution-model.md`、`testing-contract.md`。

**拥有目录：** `industrialrobot/plugins/trajectory/` 及测试。规划器参数、随机种子和失败段进入快照；碰撞只调用 WP-07。

**输入/输出：** 输入为 `RobotDesign`、`EngineeringRequirements`、IK 候选和 `CollisionPolicy`；输出为 `TrajectoryPlan`、`ResolvedIkBranchSequence` 和轨迹证据。

## 任务

1. 实现关节空间 PTP、笛卡尔接近/撤离和 `IkBranchPolicy`。
2. 接入 RobWork 规划器并记录版本、参数、种子和失败段。
3. 实现路径简化、平滑和至少加速度连续的时间参数化。
4. 对平滑轨迹调用共享碰撞评估器，验证位置、速度、加速度和节拍限制。
5. 输出 `TrajectoryPlan` 与 `ResolvedIkBranchSequence`；候选双击只预览。
6. 通过 WP-08 支持取消、失败、恢复和迟到回调隔离。

## 验证

前置：WP-07、WP-08、WP-14、WP-15；命令入口由 WP-01 提供。

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory_test$'
```

必须提交路径黄金数据、平滑前后碰撞报告、时间参数报告和 AT-06/19 记录。

## 迁移与删除

旧规划器仅用于对照；新轨迹输出稳定后删除旧播放/导出适配。

## 独立评审

由规划和测试负责人独立复核路径、平滑、碰撞和时间参数证据。

## 退出条件

TRJ-01～08、AT-06、AT-19 通过；有限离散验证只使用协议限定措辞，不宣称连续安全证明；轨迹输出可被 WP-17 复算。

## 任务卡索引

- [WP-16-T01 PTP 与笛卡尔接近撤离](../agent-tasks/WP-16-T01-ptp-cartesian.md)
- [WP-16-T02 RobWork 规划器适配](../agent-tasks/WP-16-T02-planner-adapter.md)
- [WP-16-T03 路径平滑与时间参数化](../agent-tasks/WP-16-T03-smoothing-time.md)
- [WP-16-T04 轨迹碰撞与运动限制](../agent-tasks/WP-16-T04-collision-limits.md)
- [WP-16-T05 轨迹结果与候选预览](../agent-tasks/WP-16-T05-trajectory-result.md)
- [WP-16-T06 轨迹任务生命周期](../agent-tasks/WP-16-T06-lifecycle.md)
