# WP-23 系统质量与规模化实施计划

**目标：** 建立跨模块质量门禁、故障注入、性能基准、确定性和恢复验证。

**阶段/发布：** 阶段 A 持续建设，R1/R2；阶段 D 完成规模化门禁。

**需求与契约：** AT-01～19、NFR-COR、NFR-PERF、NFR-REL；引用 `architecture/testing-contract.md`、`execution-model.md`。

**拥有目录：** `industrialrobot/testkit/system/`、`benchmark-manifest.json` 和 CI 测试配置；不得修改业务实现以绕过门禁。

**输入/输出：** 输入为各 WP 工件、标准数据和故障注入配置；输出为测试报告、性能基准、缺陷清单和阶段门禁证据。

## 任务

1. 将 AT-01～AT-19 转为独立系统测试，固定黄金数据和随机种子。
2. 注入保存、进程、缓存、检查点、外部资源和报告渲染故障。
3. 按基准清单测量交互 P95、批处理、吞吐、内存和恢复统计。
4. 验证固定线程下候选集合、排序、稳定 ID 和 Pareto 关系一致。
5. 汇总缺陷等级、追踪矩阵和发布门禁证据。

## 验证

前置：各阶段模块和 WP-01 门禁入口；

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_system_test$'
```

必须提交性能报告、故障注入日志、恢复统计和门禁清单。

## 迁移与删除

历史测试结果只读保存；新系统测试门禁稳定后删除失效测试入口和重复基准配置。

## 独立评审

由未参与业务实现的质量负责人复核故障注入、性能、恢复和缺陷门禁证据。

## 退出条件

AT-01～AT-19、NFR-COR、NFR-PERF、NFR-REL 通过；性能报告引用基准清单且所有失败可追溯。

## 任务卡索引

- [WP-23-T01 系统验收测试套件](../agent-tasks/WP-23-T01-system-suite.md)
- [WP-23-T02 故障注入与恢复](../agent-tasks/WP-23-T02-fault-injection.md)
- [WP-23-T03 性能与规模基准](../agent-tasks/WP-23-T03-benchmark.md)
- [WP-23-T04 并行确定性](../agent-tasks/WP-23-T04-determinism.md)
- [WP-23-T05 质量与发布门禁](../agent-tasks/WP-23-T05-release-gate.md)
