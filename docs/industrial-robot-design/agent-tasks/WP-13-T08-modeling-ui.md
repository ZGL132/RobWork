# WP-13-T08 建模插件与 GUI

- 需求/阶段：UX-01～08、MDL-13；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`
- 前置：WP-10、WP-13-T02；允许：建模薄插件和 GUI 测试；禁止：Widget 写领域对象
- 产出：导入、编辑、错误定位、应用确认入口
- Given 未应用草稿，When 切换视图，Then 不创建修订或失效结果
- Given 点击应用，When 命令成功，Then 只创建一个修订并刷新状态
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_modeling_test$'`
- 证据：GUI 回归视频/报告、诊断截图；提交：`WP-13-T08: add modeling ui`
- 停止：GUI 测试无法按 Windows 规则运行时暂停
