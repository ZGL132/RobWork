# WP-15-T07 运动学应用与 GUI

- 需求/阶段：KIN-06～08、UX-01～08；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`
- 前置：WP-10、WP-15-T02、WP-15-T06；允许：运动学薄插件和 GUI 测试；禁止：双击直接应用
- 产出：候选预览、分支锁定、应用命令和导出
- Given 双击候选，When 预览，Then 不创建修订；显式应用才创建修订
- Given 导出结果，When 生成报告，Then 引用快照而非当前 Widget
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_kinematics_test$'`
- 证据：GUI 回归和应用日志；提交：`WP-15-T07: add kinematics ui`
- 停止：应用语义与 WP-04 冲突时暂停
