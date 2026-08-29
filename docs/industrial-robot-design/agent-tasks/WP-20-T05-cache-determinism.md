# WP-20-T05 缓存与确定性

- 需求/阶段：OPT-06 静态子集、CON-04；B/R1
- 契约：`architecture/execution-model.md`、`architecture/persistence-schema.md`
- 前置：WP-08、WP-20-T01～T04；允许：静态缓存适配；禁止：重复实现缓存规则
- 产出：完整缓存键、兼容命中判断和固定种子复现
- Given 任一依赖变化，When 查缓存，Then 不命中正式结果
- Given 同快照/种子/线程，When 重算，Then 候选 ID、集合和排序稳定
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_optimization_definition_test$'`
- 证据：命中/拒绝矩阵、复现报告；提交：`WP-20-T05: add deterministic cache`
- 停止：缓存键缺少依赖字段时暂停
