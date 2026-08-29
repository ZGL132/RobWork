# WP-15 运动学实施计划

**目标：** 基于规范 SE(3) 模型提供 FK、IK、Jacobian、区域覆盖、碰撞证据和稳定候选排序。

**阶段/发布：** 阶段 B，R1；阶段 B 只交付模型—需求—运动学—静态优化链路。

**需求与契约：** KIN-01～08；AT-03～05、AT-18～19 阶段 B 子链路；引用 `architecture/domain-model.md`、`public-interfaces.md`、`testing-contract.md`。

**拥有目录：** `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`、测试和解析算例。碰撞规则只调用 WP-07，名称只调用 WP-06。

**输入/输出：** 输入为已编译 `RobotDesign`、`EngineeringRequirements`、`CollisionPolicy` 和评估配置；输出为 `KinematicResult`、IK 候选、区域覆盖和 `EvidenceBundle`。

## 任务

1. **WP-15-T01 FK 与规范模型**：接入 `RuntimeNameMap` 编译工件，验证零位、Home、边界和世界坐标 FK；引用失败必须阻止求值。
2. **WP-15-T02 IK 候选**：实现位置/姿态约束、残差、关节限制、重复解去重和稳定候选 ID；排序键包含应用状态、残差和稳定 ID。
3. **WP-15-T03 Jacobian 与奇异性**：实现 `J_norm`、任务子空间、可操作度和条件数；`L*` 非有限/非正时按架构契约回退或 DataInsufficient。
4. **WP-15-T04 区域覆盖**：实现边界包含、位置/姿态分母、采样取消和预算耗尽语义；不得将 Partial 结果标为 Verified。
5. **WP-15-T05 碰撞证据**：调用共享 `CollisionEvaluator`，保存对象 ID 对、策略版本、分辨率和“未发现碰撞”限定措辞。
6. **WP-15-T06 批处理执行**：通过 WP-08 调度批次，携带项目/分支/修订/运行/尝试身份；迟到回调不得覆盖当前结果。
7. **WP-15-T07 应用与 GUI**：双击只预览；“用于规划/锁定分支”才创建命令和修订；结果导出引用快照而非当前 Widget。
8. **WP-15-T08 契约回归**：从运动学和阶段 B 优化入口复核 AT-19 对象 ID、判定和原因完全一致。

## 任务卡

详见 `agent-tasks/WP-15-T01-fk.md`～`WP-15-T08-cross-entry.md`。

## 验证

前置：WP-01、WP-02、WP-05～08、WP-13、WP-14。

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_test$'
```

必须提交：FK/IK/Jacobian 解析对照、碰撞证据、区域覆盖报告、排序稳定性报告和 AT-18/19 阶段 B 记录。

## 迁移与删除

旧运动学入口只用于黄金对照；阶段 B 通过后删除旧主链路和重复排序/碰撞适配。

## 独立评审

由独立算法验证者复核 FK/IK/Jacobian、碰撞协议、排序和 AT-19 证据。

## 退出条件

KIN-01～08、AT-03～05、AT-18 阶段 B 子链路和 AT-19 静态入口通过；容差符合第 15.3 节；不可行、数据不足和取消结果不进入正式可行集。
