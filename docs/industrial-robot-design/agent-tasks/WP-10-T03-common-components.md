# WP-10-T03 公共工程组件

- Task ID：WP-10-T03
- 需求/阶段：UX-01～UX-08、NFR-PERF-01、NFR-PERF-03；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`；模块方案：`module-design/session-ui.md`
- 前置：WP-10-T01/T02、WP-09 diagnostics、WP-07 policy provider。
- 允许：修改 `ui/include/.../StageNavigator.hpp`、`DiagnosticPanel.hpp`、`EngineeringTableView.hpp`、`src/CommonComponents.cpp`、`test/CommonComponentsTest.cpp`。
- 禁止：业务算法进入 Widget、硬编码诊断文案、绕过公共 provider 或修改 requirements。
- 产出：阶段导航、问题列表、建议、状态图例、诊断详情和工程表格。

## Given/When/Then

- Given输入缺失/过期/证据不足，When渲染阶段导航，Then显示对应状态、阻塞原因和下一步 action。
- Given批量粘贴含非法单位/行，When commit draft，Then逐行诊断且合法行不越过应用边界。
- Given高级设置折叠，When默认打开页面，Then主流程不依赖 seed/开发诊断。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_common_components_test$'`。证据：组件状态截图、粘贴矩阵、诊断映射和评审。提交：`WP-10-T03: implement common engineering components`。

停止：组件需要自行解释业务状态或出现重复文案目录时暂停。
