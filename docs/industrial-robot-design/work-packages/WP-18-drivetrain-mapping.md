# WP-18 传动映射实施计划

**目标：** 提供唯一 `DriveTrainMappingEvaluator`，将关节侧动力结果映射到电机侧，避免效率、惯量和摩擦重复计算。

**阶段/发布：** 阶段 C，R1；共享计算包，不提供独立业务插件。

**需求与契约：** DYN-04、SEL-05；引用 `architecture/domain-model.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`。

**拥有目录：** `industrialrobot/evaluation/drivetrain/` 及其 `test/`。依赖 WP-03、WP-17；这是共享计算包，不提供独立业务插件或业务 Widget。

**输入/输出：** 输入为关节侧 `DynamicResult`、传动设计和目录证据；输出为电机侧映射结果、能量分项和诊断。关节侧结果不可被候选传动改变。

## 任务

1. 冻结速比方向、单位、正/反向效率、反射惯量和传动摩擦公式版本。
2. 实现旋转传动映射；移动传动返回明确范围外诊断，不套用旋转公式。
3. 分离关节侧机械功、电机侧机械功、驱动器输入电能和可回馈能量。
4. 实现峰值持续时间窗、完整循环 RMS、制动/保持/四象限假设。
5. 为动力学、选型和优化提供同一个评估器入口。

## 验证

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_drivetrain_test$'
```

## 迁移与删除

旧插件传动计算只保留黄金对照；共享 evaluator 验收后删除重复效率、惯量和摩擦实现。

## 独立评审

由驱动工程师和独立测试人员复核双向效率、能量分项和候选无关性。

## 退出条件

DYN-04、SEL-05、AT-07、AT-08 的传动映射断言通过；不同器件候选不改变关节侧结果。

## 任务卡索引

- [WP-18-T01 传动映射语义冻结](../agent-tasks/WP-18-T01-mapping-semantics.md)
- [WP-18-T02 旋转传动映射](../agent-tasks/WP-18-T02-rotary-mapping.md)
- [WP-18-T03 能量边界分离](../agent-tasks/WP-18-T03-energy-boundaries.md)
- [WP-18-T04 峰值与工作制评估](../agent-tasks/WP-18-T04-duty-cycle.md)
- [WP-18-T05 共享传动评估器](../agent-tasks/WP-18-T05-shared-evaluator.md)
