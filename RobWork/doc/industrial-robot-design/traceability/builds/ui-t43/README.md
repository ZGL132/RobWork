# UI-T43 实施留痕（建模域宿主接线修复批次）

分支 `ui-t43`（基点 redesign-main@1a3c9cd7）；任务契约 `tasks/foundation/UI-T43.json`。

## 改动面

- **W1 可写链路三段贯通**：`UiPlugin.cpp`（openViaSessionController 建模域接线，与需求域同位同源事实）→ `ModelingPluginAssembly::setWritable`（门面转发）→ `ModelingUiModule::setWritable`（面板创建前暂存初值/创建后即时转发）→ `createPanel` 按模块初值创建面板（废除硬编码 `true`）。
- **W2 面板使能统一收口**：`ModelingPanelWidget::refreshCommandEnablement()` 单出口（提交出口∧只读门∧可用性快照三合取）承接 `setCommandSubmit`/`setCommandAvailability`/`setWritable`/`refreshPanel` 四触发点；修复 `setCommandAvailability` 缺只读合取项的门控旁路；`setWritable` 即时重算命令使能＋启用态 tooltip 回复悬停说明。
- **W3 文案解析回归钉**：核实结论——生产绑定在位（`DomainAssembly.cpp:145` `bindTextResolver(standardTextResolver())`），外审报告"宿主未绑定"不成立；补 `ModelingUiModuleHostWiringGuiTest.cpp` 两用例（生产装配序文案解析零泄漏＋setWritable 暂存/即时双形态）＋面板级防复活用例（`ReadOnlySession_AvailabilityRefreshCannotReviveWriteCommands_L7_UI_T43`）。
- **F-472 消账（顺带）**：`HostIntegrationGuiTest` 占位清除段悬挂指针修复（refresh 的 `clear()` 后重取组行指针再断言）——冒烟留痕实证 1/4 轮次翻红的既有 flaky，修复后五轮全绿。

## 构建证据

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| `build-integrated.log` | 集成模式建模目标（plugin/test/gui_test 等） | 零错误 |
| `build-integrated-ui.log` | 集成模式 ui 目标（ui_plugin/studio/ui_test/ui_gui_test） | 零错误 |
| `smoke-configure.log` / `smoke-build.log` | 独立冒烟树 `build-smoke-ui-t43`（vcpkg 工具链＋Qt 前缀；单元本地五目标） | 零错误 |
| `validate-task` / `validate-docs.log` | 治理双校验 | PASS / PASS |

## 测试证据（`tests/`，t＝集成树、s＝冒烟树）

| 套件 | 集成 | 冒烟 |
| --- | --- | --- |
| sdurws_ird_modeling_test | 294/294（t1） | 236/236（s1，gated TU 不编入口径） |
| sdurws_ird_modeling_gui_test | 12/12（t2，含 UI-T43 三新用例） | 10/10（s2，模块两用例 gated） |
| sdurws_ird_ui_test | 239/239（t3） | 239/239（s3） |
| sdurws_ird_ui_gui_test | 67/67（t4，含 F-472 修复面） | 67/67（s4；修复前 1/4 轮次翻红，修复后五轮全绿） |
| sdurws_ird_modeling_contract_test | 17/17（t5） | —（契约面两模式同源，本批零 CMake 链接面变化） |
| sdurws_ird_ui_contract_test | 32/32（t6） | —（同上） |

环境：`QT_QPA_PLATFORM=windows`；GUI 冒烟另带 `QT_QPA_PLATFORM_PLUGIN_PATH`（冒烟树无 ctest ENVIRONMENT 注入）。

## 诚实边界

- 集成构建日志存在两条存量 C4003 警告（`ModelingCommandFlows.cpp:321`，本批未触碰该文件）。
- 冒烟树首建曾因**实施初版**把模块级用例放在无条件编入的 `ModelingPanelGuiTest.cpp` 触发 LNK2019（policy 符号冒烟不编入）——按 `PluginModuleT03BTest` 先例拆出并 `TARGET sdurw_kinematics` gating 后双树干净；告诫已登记任务卡 note。
- verify-task 全量执行未跑（验收段职责）；verify 列表中各命令均已逐条真实执行并留痕于本目录。
