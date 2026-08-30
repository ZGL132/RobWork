# WP-16-T07 轨迹规划工作台界面

- **Task ID / 需求 ID / ADR / 阶段：** WP-16-T07；TRJ-01～07、UX-01～08、NFR-PERF-03；阶段 C / R1。契约：`architecture/testing-contract.md` §3～§5、`architecture/evaluation-semantics.md` §2～§5；模块详设 `module-design/trajectory-planning.md` v0.4 §8.1～§8.6、`module-design/session-ui.md` v0.4 §8。
- **基线 commit：** 代码基线 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档语义源 `trajectory-planning.md` v0.4。
- **前置任务及必需工件：** WP-16-T05（完整 `TrajectoryPlan`/结果）、WP-16-T06（任务生命周期）、WP-10-T06（工作台外壳）、WP-01-T03（测试入口）；工件为只读结果端口、任务状态端口和已登记 Dock 容器。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/`）创建/修改 `gui/` 下轨迹段、设置、曲线、播放、碰撞和状态面板；创建 `test/TrajectoryGuiTest.cpp`；修改本插件 `CMakeLists.txt` 仅登记 GUI 文件与 `sdurws_ird_trajectory_gui_test`；创建 `out/test-evidence/wp-16/<run-id>/`。禁止删除计算核心文件。
- **禁止修改的文件和公共接口：** `src/` 与公共轨迹计算头、WP-08 调度、WP-10 外壳、其他插件、RobWorkStudio 中央视图、架构和 Schema；禁止在 GUI 线程执行规划、碰撞检测或文件 IO。
- **修改前接口：** 轨迹计算和生命周期可用，但没有符合 §8 字段、按钮状态、表列、曲线联动和错误态的工作台面板。
- **修改后接口：** 轨迹 GUI 经冻结端口提交规划、取消与重试，显示轨迹段表、规划设置、曲线与播放、碰撞/限位/分支信息；选择和播放只投影到现有三维视图，不改写设计。
- **实施步骤：** 1) 写页面结构、按钮状态和表列 RED；2) 实现轨迹段与设置模型；3) 接入曲线、播放和三维选择联动；4) 接入空态、运行态、失败态与部分结果；5) 登记并运行 GUI 目标。
- **RED 测试：** `SegmentTableColumnsMatchSpecification`、`PlanButtonRequiresValidInput`、`RunningStateDisablesMutatingActions`、`CurveSelectionSyncsSceneWithoutCommit`、`FailureKeepsLastAcceptedResult`。
- **最小实现：** §8 要求的轨迹工作台面板和状态投影；不新增规划算法、曲线计算或三维渲染器。
- **正常/边界/失败测试：**
  - 正常：Given 有效任务点和机器人模型，When 规划完成并选择一段，Then 表格、曲线、播放位置与三维视图指向同一段。
  - 边界：Given 150% 缩放和长关节名，When 调整 Dock，Then 主按钮与状态可见，表格横向滚动而不挤压中央视图。
  - 失败：Given 碰撞、限位或规划失败，When 结果返回，Then 显示对象、位置、原因与建议，保留上次已接纳结果并提供重试。
- **精确验证命令：**（仓库根、Visual Studio x64 环境；GUI 平台固定为 windows，单次只运行本目标）
  - `$env:QT_QPA_PLATFORM='windows'; powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory_gui_test$'`
  - 回退构建：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_gui_test`
  - 回退执行：`$env:QT_QPA_PLATFORM='windows'; $testExe=(Resolve-Path '.\out\build\industrial-robot\bin\Debug\sdurws_ird_trajectory_gui_test.exe').Path; & $testExe`
- **diff 和禁止项检查：** diff 仅含本插件 `gui/`、测试、CMake 和证据；`rg -n "plan\(|collisionCheck\(|QFile" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/gui; if ($LASTEXITCODE -eq 0) { throw '检测到 GUI 越权计算或 IO' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中；边界脚本零违规。
- **证据工件：** `out/test-evidence/wp-16/<run-id>/trajectory-ui.md`，包含字段/列/按钮矩阵、三档缩放截图、选择联动、正常/边界/失败日志和命令退出码。
- **提交格式：** `WP-16-T07: 新增轨迹规划工作台界面`

  - 新增 轨迹段、曲线与播放面板
  - 新增 状态和失败行为 GUI 测试
  - 证据 记录缩放与三维联动结果
- **停止与升级条件：** 冻结结果端口不能提供 §8 字段、GUI 需要直接调用计算核心或修改 WP-10 外壳时停止并升级所有者；实现者不得担任独立验证者。
