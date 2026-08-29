# WP-14-T07 需求 GUI

- 需求/阶段：UX-01～08；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`
- 前置：WP-10、WP-14-T05；允许：需求薄插件和 GUI 测试；禁止：Widget 直接计算
- 产出：批量编辑、筛选、单位显示、错误定位和就绪摘要
- Given 错误字段，When 提交，Then 定位对象/实际值/要求值并阻止应用
- Given 合法数据，When 应用，Then 使用项目命令并更新状态
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_requirements_test$'`
- 证据：GUI 回归报告；提交：`WP-14-T07: add requirements ui`
- 停止：GUI 平台环境不符合 Windows 规则时暂停
