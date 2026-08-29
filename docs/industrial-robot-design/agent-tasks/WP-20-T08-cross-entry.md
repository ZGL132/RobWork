# WP-20-T08 跨入口契约

- 需求/阶段：ARC-05、NFR-COR-05；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`
- 前置：WP-07、WP-15-T08；允许：优化契约测试；禁止：复制 CollisionEvaluator
- 产出：运动学/WP-20 共享策略一致性测试和 WP-21 扩展端口
- Given 同一策略与快照，When 从两个入口求值，Then 对象 ID 对、判定和原因一致
- Given 只改变显示开关，When 重算，Then 输入切片和结果当前性不变
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_optimization_definition_test$'`
- 证据：AT-19 阶段 B 记录；提交：`WP-20-T08: add optimization contract tests`
- 停止：公共接口需要变更时先走 ADR
