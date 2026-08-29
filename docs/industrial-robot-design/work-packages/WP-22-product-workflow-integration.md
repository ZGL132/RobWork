# WP-22 产品工作流整合实施计划

**目标：** 将七个业务域整合为单项目单机械臂的项目驾驶舱、阶段导航和候选决策流程。

**阶段/发布：** 阶段 E，R1/R2；不增加新的领域权威实现。

**需求与契约：** UX-01～08、AT-04、05、12；引用 `architecture/public-interfaces.md`、`execution-model.md`、`testing-contract.md`。

**拥有目录：** `industrialrobot/ui/workflow/`、`ui/comparison/` 及测试；只消费公共端口，不读取 Widget 私有状态。

**输入/输出：** 输入为项目状态、阶段结果、诊断和候选；输出为驾驶舱状态、下一步建议、比较视图和项目命令请求。

## 任务

1. 实现七阶段导航、输入完整度和单机械臂作用域显示。
2. 展示结果有效/需重算/证据不足/工程不可行与任务生命周期。
3. 实现候选比较、临时预览、基线差异和设为当前方案命令。
4. 统一诊断、策略摘要、下一步建议和报告入口。
5. 用固定任务脚本完成新机型、改型和错误恢复用户流程测试。

## 验证

前置：WP-10、WP-12～WP-21；

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_test$'
```

必须提交 GUI 回归报告、用户任务脚本和状态展示矩阵。

## 迁移与删除

旧插件菜单和跨 Widget 适配只保留迁移期入口；新驾驶舱验收后删除重复导航和状态映射。

## 独立评审

由产品、体验和独立测试人员按固定任务脚本复核工作流。

## 退出条件

UX-01～08、AT-04、AT-05、AT-12、AT-19 通过；R1 可独立完成并报告建模至基础选型闭环。

## 任务卡索引

- [WP-22-T01 阶段导航与作用域](../agent-tasks/WP-22-T01-stage-navigation.md)
- [WP-22-T02 状态与任务投影](../agent-tasks/WP-22-T02-status-projection.md)
- [WP-22-T03 候选比较与应用](../agent-tasks/WP-22-T03-candidate-compare.md)
- [WP-22-T04 诊断与报告入口](../agent-tasks/WP-22-T04-diagnostic-guidance.md)
- [WP-22-T05 端到端用户流程](../agent-tasks/WP-22-T05-workflow-tests.md)
