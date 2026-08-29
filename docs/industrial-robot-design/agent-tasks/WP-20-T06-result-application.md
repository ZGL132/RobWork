# WP-20-T06 结果与应用

- 需求/阶段：OPT-07、08、CON-02；B/R1
- 契约：`architecture/execution-model.md`、`public-interfaces.md`
- 前置：WP-04、WP-05、WP-20-T04；允许：结果仓库和应用适配；禁止：候选直接写项目
- 产出：运行结果归属、候选应用命令和分支创建
- Given 运行候选，When 评估中修改项目，Then 修订号不随候选增长
- Given 显式设为当前，When 应用，Then 只创建一个分支和新修订
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_optimization_definition_test$'`
- 证据：修订/结果当前性矩阵；提交：`WP-20-T06: integrate optimization results`
- 停止：结果 payload 被回写时暂停
