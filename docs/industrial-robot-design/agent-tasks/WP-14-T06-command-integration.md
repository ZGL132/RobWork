# WP-14-T06 项目命令集成

- 需求/阶段：ARC-01、CON-05；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/execution-model.md`
- 前置：WP-04、WP-05、WP-14-T05；允许：需求命令适配；禁止：直接写项目文件
- 产出：应用、撤销、重做和失效切片集成
- Given 未应用草稿，When 修改会话，Then 修订和下游结果不变
- Given 应用需求命令，When 成功，Then 一个新修订并按字段失效
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_requirements_test$'`
- 证据：修订/失效矩阵；提交：`WP-14-T06: integrate requirement commands`
- 停止：命令所有权不明确时暂停
