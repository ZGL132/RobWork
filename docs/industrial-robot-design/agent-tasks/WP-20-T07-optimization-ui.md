# WP-20-T07 阶段 B 优化 UI

- 需求/阶段：UX-01～08、OPT-08；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`
- 前置：WP-10、WP-20-T04、T06；允许：优化薄插件和 GUI 测试；禁止：实现 WP-21 功能
- 产出：变量编辑、候选比较、证据查看和应用确认
- Given 不可行候选，When 展示，Then 显示硬约束原因且不可应用
- Given 合法候选，When 应用，Then 使用项目命令并显示新修订
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_optimization_definition_test$'`
- 证据：GUI 回归报告；提交：`WP-20-T07: add static optimization ui`
- 停止：阶段 D 功能混入阶段 B 时暂停
