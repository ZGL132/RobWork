# WP-10-T04 统一策略入口与显示隔离

- Task ID：WP-10-T04
- 需求/阶段：UX-01～UX-08、AT-19；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/execution-model.md`；模块方案：`module-design/session-ui.md`
- 前置：WP-07 policy provider、WP-10-T01、WP-09 diagnostics。
- 允许：修改 `ui/include/.../EngineeringPolicyPanel.hpp`、`src/PolicyPanel.cpp`、`test/PolicyUiTest.cpp`。
- 禁止：插件私有策略开关、显示高亮触发命令/计算、修改 EngineeringPolicySet 字段或碰撞算法。
- 产出：唯一工程策略页面、计算模式与显示开关分组及隔离测试。

## Given/When/Then

- Given修改计算模式/安全距离，When用户编辑，Then进入 EditDraft，应用后才调用 WP-07 provider/命令。
- Given切换显示碰撞几何/高亮，When toggle，Then仅更新 SessionState，不改变 revision、sliceHash、缓存或结果。
- Given插件尝试提供同名私有开关，When scan/test，Then拒绝并指出来源。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy_ui_test$'`。证据：策略摘要、显示隔离计数和界面截图。提交：`WP-10-T04: implement unified policy UI`。

停止：计算设置和显示设置无法区分或 UI 需要复制策略默认值时暂停。
