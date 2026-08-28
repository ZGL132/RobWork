# 工业机械臂设计软件重构文档

本目录是工业机械臂设计软件重构的唯一权威文档入口。旧插件的阶段性方案、临时设计稿和历史复盘不再作为实施依据；需要追溯时使用 Git 历史。

## 阅读顺序

1. [产品需求基线](requirements.md)：产品边界、领域规则、功能需求、非功能需求与验收场景。
2. [开发任务拆解总纲](development-task-breakdown.md)：26 个工作包、依赖关系、阶段门禁、责任分离和关键路径。
3. [需求追踪矩阵](requirement-traceability.csv)：124 项需求到实现、测试、评审和验收场景的机器可检查映射。
4. [工作包计划](work-packages/)：WP-00～WP-12 的详细实施计划；WP-13～WP-25 按总纲中的契约冻结规则逐阶段细化。

## 维护规则

- `requirements.md` 是唯一产品需求基线；需求变更必须更新修订记录和追踪矩阵。
- `development-task-breakdown.md` 是唯一总体实施计划；不得在其他目录创建平行总纲。
- 新的工作包计划只进入 `work-packages/`，文件名使用 `WP-XX-<topic>.md`。
- 算法证据、报告和测试结果进入产品定义的项目工件或 CI 产物，不在仓库中散落临时 Markdown。
- 执行 `generate-traceability.ps1` 更新追踪矩阵，执行 `validate-development-docs.ps1` 检查完整性。

## 当前状态

- 需求基线：v0.4。
- 需求数量：124，其中 P0 110 项、P1 14 项。
- 验收场景：AT-01～AT-19。
- 已细化工作包：WP-00～WP-12。
