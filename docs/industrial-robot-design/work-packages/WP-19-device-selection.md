# WP-19 器件选型实施计划

**目标：** 导入不可变企业目录，按真实轨迹和传动映射筛选电机/减速器组合并提供逐项淘汰证据。

**阶段/发布：** 阶段 C，R1；首版只支持旋转传动，移动关节输出范围外诊断。

**需求与契约：** SEL-01～09；AT-08、AT-19；引用 `architecture/domain-model.md`、`persistence-schema.md`、`public-interfaces.md`。

**拥有目录：** `industrialrobot/plugins/selection/` 及测试。目录解析调用 WP-11，传动计算调用 WP-18。

**输入/输出：** 输入为 `DynamicResult`、`DriveTrainDesign`、`CatalogVersionRef` 和筛选规则；输出为只读 `ComponentSelectionResult`、裕量和淘汰诊断。

## 任务

1. 冻结 motors、reducers、曲线和 compatibility 表字段、单位、空值和版本规则。
2. 实现分段线性插值；曲线外、温度降额或峰值时间缺失返回 DataInsufficient。
3. 实现连续/峰值转矩、转速、功率、过载、工作制、电压、温度和安全系数筛选。
4. 复用 `DriveTrainMappingEvaluator` 校核惯量比、效率和组合兼容。
5. 输出可行组合、裕量、来源和每项实际值/阈值/诊断码；应用通过项目命令产生修订。
6. 对目录版本更新执行历史结果不变和旧版本拒绝测试。

## 验证

前置：WP-08、WP-11、WP-17、WP-18；

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_selection_test$'
```

必须提交目录包、插值报告、可行/不可行黄金表和 AT-08 证据。

## 迁移与删除

旧目录导入器仅用于迁移对照；新 CSV 目录包稳定后删除旧字段映射和重复筛选逻辑。

## 独立评审

由器件目录负责人独立复核字段、插值、淘汰原因和版本锁定证据。

## 退出条件

SEL-01～09、AT-08、AT-19 相关选型断言通过；目录更新不改变历史结果；移动关节不静默套用旋转公式。

## 任务卡索引

- [WP-19-T01 器件目录 Schema](../agent-tasks/WP-19-T01-catalog-schema.md)
- [WP-19-T02 曲线插值与数据不足](../agent-tasks/WP-19-T02-curve-interpolation.md)
- [WP-19-T03 器件约束筛选](../agent-tasks/WP-19-T03-constraint-filter.md)
- [WP-19-T04 传动映射复核](../agent-tasks/WP-19-T04-mapping-check.md)
- [WP-19-T05 选型结果与修订应用](../agent-tasks/WP-19-T05-selection-output.md)
- [WP-19-T06 目录版本兼容](../agent-tasks/WP-19-T06-catalog-version.md)
