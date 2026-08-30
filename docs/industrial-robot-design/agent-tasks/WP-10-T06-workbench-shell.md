# WP-10-T06 工程工作台外壳与 Dock 布局

- **Task ID / 需求 ID / ADR / 阶段：** WP-10-T06；UX-01～08、NFR-PERF-03；ADR-001；阶段 A / R1。契约：`architecture/testing-contract.md` §3～§5、`architecture/public-interfaces.md` §4；模块详设 `module-design/session-ui.md` v0.4 §8.1～§8.9。
- **基线 commit：** 代码基线 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档语义源 `session-ui.md` v0.4。
- **前置任务及必需工件：** WP-10-T01（会话状态）、WP-10-T02（场景投影）、WP-10-T03（公共组件）、WP-10-T04（策略 UI）、WP-10-T05（响应式列表）、WP-01-T02（构建骨架）、WP-01-T03（测试入口）；工件为上述任务独立验证通过的实现提交和目标测试。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workbench/`）创建 `include/sdurws/ird/ui/workbench/WorkbenchShell.hpp`、`src/WorkbenchShell.cpp`、`test/WorkbenchShellGuiTest.cpp`；修改相邻 `ui/CMakeLists.txt` 仅登记源文件与 `sdurws_ird_workbench_gui_test`；创建 `out/test-evidence/wp-10/<run-id>/`。禁止删除文件。
- **禁止修改的文件和公共接口：** `RobWork/RobWorkStudio/src/RobWorkStudio.cpp`、`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/`、`docs/industrial-robot-design/architecture/`、`docs/industrial-robot-design/module-design/`、`docs/industrial-robot-design/schemas/`、`docs/industrial-robot-design/testdata/` 和 WP-10-T01～T05 公共接口；禁止替换中央视图、独占菜单栏或重置其他插件状态。
- **修改前接口：** 公共控件与会话投影存在，但没有与 RobWorkStudio 主窗口兼容的工业机械臂工作台外壳、Dock 注册表和缩放恢复测试。
- **修改后接口：** `WorkbenchShell` 只负责 session-ui v0.4 §8 的左/右/底 Dock 容器、阶段入口、布局恢复与公共空态；中央三维视图继续由 RobWorkStudio 所有；业务插件只向已登记 Dock 注入内容，不获得主窗口所有权。
- **实施步骤：**
  1. 先写三档缩放、Dock 注册、布局恢复和中央视图保护的 GUI RED 用例。
  2. 实现工作台外壳和左侧导航、右侧公共面板、底部公共面板的稳定对象名与默认尺寸。
  3. 接入会话空态、未选中对象状态和布局恢复；无项目时不创建业务结果控件。
  4. 登记 `sdurws_ird_workbench_gui_test`，在 VS x64 环境逐个运行并归档证据。
- **RED 测试：** `ThreeScaleProfilesKeepPrimaryActionsVisible`、`DockRegistryHasLeftRightBottomZones`、`RestoreClampsOffscreenGeometry`、`CentralStudioViewRemainsOwnedByRobWorkStudio`、`OtherPluginDockStateIsPreserved`；实现前目标不存在或断言失败。
- **最小实现：** 工作台外壳、三侧 Dock、布局恢复和公共空态；不实现任何领域计算、领域面板字段或项目新建向导。
- **正常/边界/失败测试：**
  - 正常：Given 1920×1080、100% 缩放，When 启用工作台，Then 左/右/底 Dock 和中央三维视图同时可见，主要操作无需滚动。
  - 边界：Given 125% 与 150% 缩放，When 切换阶段和折叠 Dock，Then 中央三维视图逻辑尺寸不小于 640×480，主按钮、状态与表头不被截断。
  - 失败：Given 保存布局来自已移除显示器或含其他插件 Dock，When 恢复，Then 工作台几何钳制到当前屏幕且不重置其他插件布局。
- **精确验证命令：**（仓库根、Visual Studio x64 环境；第一形式必执行；GUI 平台固定为 windows，单次只运行本目标）
  - `$env:QT_QPA_PLATFORM='windows'; powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workbench_gui_test$'`
  - 回退构建：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workbench_gui_test`
  - 回退执行：`$env:QT_QPA_PLATFORM='windows'; $testExe=(Resolve-Path '.\out\build\industrial-robot\bin\Debug\sdurws_ird_workbench_gui_test.exe').Path; & $testExe`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；`rg -n "setCentralWidget|restoreState.*other|reset.*Dock" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workbench; if ($LASTEXITCODE -eq 0) { throw '检测到主窗口越权实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-10/<run-id>/workbench-shell.md`，包含 100%/125%/150% 截图、Dock 尺寸表、屏外恢复记录、中央视图尺寸和其他插件状态对照、完整命令与退出码。
- **提交格式：** `WP-10-T06: 新增工程工作台外壳`

  - 新增 工作台三侧 Dock 与布局恢复
  - 新增 三档缩放和兼容性 GUI 测试
  - 证据 记录中央视图与其他插件状态保护结果
- **停止与升级条件：** 若必须修改 RobWorkStudio 中央视图所有权、其他插件 Dock 恢复协议或 WP-10 公共接口，停止并升级对应所有者；实现者不得担任独立验证者。
