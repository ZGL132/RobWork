# WP-25 试点与交付实施计划

**目标：** 用真实机械臂和搬运任务验证 R1/R2 工程价值，完成用户测试、培训和交付材料。

**阶段/发布：** 阶段 E，R1/R2；阶段 C 先完成数据签署，阶段 E 完成真实试点。

**需求与契约：** 第 3.3、14、17 章；引用 `architecture/testing-contract.md`、`architecture/persistence-schema.md`。

**拥有目录：** `docs/industrial-robot-design/pilot/`、样例项目、报告模板、用户手册和培训材料。完整试点依赖 WP-22～WP-24；阶段 C 末数据签署是进入真实试点的前置门禁。

**输入/输出：** 输入为已发布 R1/R2、签署的真实数据和用户任务脚本；输出为对照报告、用户研究报告、缺陷清单、签署记录和发布检查表。

## 阶段门禁

试点数据负责人在阶段 C 末签署机器人模型、工位/障碍、工具/TCP、负载、公差、器件目录和逐指标对照容差。未签署的数据只能用于标准样例或敏感度参考，不得进入真实试点验收。

## 任务

1. 固化一台真实机械臂、一类搬运/上下料任务和对应环境、工具、负载与目录版本。
2. 执行解析算例、独立工具/实测对照和完整项目报告复核，记录差异与限制。
3. 组织至少 5 名目标用户完成固定任务脚本，记录完成率、介入次数、耗时和问题分类。
4. 修复 Blocker，给每个遗留 Critical 指定负责人、影响范围和计划日期。
5. 发布样例项目、报告模板、安装说明、用户手册和培训材料。

## 验证

验证证据：

- `pilot/data-signoff.md`：机器人模型、工位/障碍、工具/TCP、负载、公差、器件目录和逐指标对照容差的负责人签署记录；
- `pilot/comparison-report.*`：解析算例、独立工具/实测对照、差异与限制；
- `pilot/user-study-report.*`：至少 5 名目标用户的完成率、介入次数、耗时和问题分类；
- `pilot/defect-register.*`：Blocker/Critical 缺陷及负责人、影响范围和计划日期；
- `pilot/release-checklist.*`：R1/R2 发布检查表。

系统冒烟测试可以作为辅助证据，但不替代上述人工验收和签署记录：

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Release -Regex '^sdurws_ird_system_test$'
```

## 迁移与删除

试点期间保留原始对照数据和报告；发布完成后清理临时数据，但签署记录和缺陷证据永久归档。

## 独立评审

由业务数据负责人、产品负责人和独立试点观察员共同复核签署记录与用户报告。

## 退出条件

达到第 3.3 节业务指标，关键结论满足签署容差，开放 Blocker 为 0，R1/R2 发布检查表完整。

## 任务卡索引

- [WP-25-T01 试点范围与固定目录](../agent-tasks/WP-25-T01-pilot-scope.md)
- [WP-25-T02 算例与实测对照](../agent-tasks/WP-25-T02-validation-report.md)
- [WP-25-T03 目标用户任务脚本](../agent-tasks/WP-25-T03-user-study.md)
- [WP-25-T04 缺陷关闭与遗留项](../agent-tasks/WP-25-T04-defect-closure.md)
- [WP-25-T05 交付材料](../agent-tasks/WP-25-T05-delivery-kit.md)
