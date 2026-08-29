# WP-21 联合优化实施计划

**目标：** 在 R2 中编排轨迹、动力学、传动和器件评估，提供可恢复、可复现、可审计的 Pareto 候选。

**阶段/发布：** 阶段 D，R2；完整实现 OPT-01～10，包含 OPT-05、OPT-09、OPT-10。

**需求与契约：** OPT-01～10 全量；AT-09～14；NFR-PERF-04～06；引用 `architecture/execution-model.md`、`public-interfaces.md`、`testing-contract.md`。

**拥有目录：** `industrialrobot/plugins/optimization/joint/` 及测试。WP-20 静态链路作为输入，不复制其约束、缓存或 Pareto 语义。

**输入/输出：** 输入为基线修订、研究定义、轨迹/动力/选型评估器和预算；输出为可恢复 `OptimizationRunResult`、候选集合、Pareto 集和报告证据。

## 任务

1. 冻结外层结构/传动探索、内层 Quick/Verified/器件匹配策略和预算。
2. 实现 HardConstraint、SoftConstraint、Metric、Objective 四层判定；证据不足和 Partial 不进入可行集。
3. 接入调度器、缓存和检查点，保存种子、预算、已完成批次和候选稳定 ID。
4. 实现 Pareto、基线差异、多样性、Quick 误淘汰审计和三类鲁棒性协议。
5. 支持候选预览与“设为当前方案”；应用只创建一个新修订并触发复算。
6. 实现 AT-10～14 的分支切换、恢复、崩溃和性能证据收集。

## 验证

前置：WP-16～20、WP-08、WP-23；

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_joint_test$'
```

必须提交 R2 基准报告、误淘汰审计、恢复统计、Pareto 黄金集和 AT-09～14 记录。

## 迁移与删除

WP-20 静态链路作为输入保留；旧联合优化入口验收后删除，不得形成第二套调度或 Pareto 实现。

## 独立评审

由独立优化验证者复核恢复、误淘汰率、鲁棒性和性能报告。

## 退出条件

OPT-01～10 全量、AT-09～14、NFR-PERF-04～06 通过；误淘汰率、恢复统计和 Pareto 关系符合第 15.3 节。

## 任务卡索引

- [WP-21-T01 联合搜索策略](../agent-tasks/WP-21-T01-search-strategy.md)
- [WP-21-T02 约束与指标判定](../agent-tasks/WP-21-T02-feasibility-layers.md)
- [WP-21-T03 调度缓存检查点](../agent-tasks/WP-21-T03-scheduler-checkpoint.md)
- [WP-21-T04 Pareto 与鲁棒性](../agent-tasks/WP-21-T04-pareto-robustness.md)
- [WP-21-T05 候选预览与应用](../agent-tasks/WP-21-T05-apply-candidate.md)
- [WP-21-T06 联合优化验收证据](../agent-tasks/WP-21-T06-acceptance-evidence.md)
