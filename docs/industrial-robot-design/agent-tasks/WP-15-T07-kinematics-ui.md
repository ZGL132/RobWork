# WP-15-T07 应用与 GUI

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T07；KIN-06～08、UX-01～08、AT-04；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.4 §8、`module-design/session-ui.md` v0.4 §8、`architecture/public-interfaces.md` §7、`architecture/evaluation-semantics.md` §5。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档语义源 `kinematics.md` v0.4 §8。
- **前置任务及必需工件：** WP-15-T06（`ird.kinematics` 评估器与 `KinematicsSettings` 可用）；WP-10-T03（导航/诊断/表格公共组件）；WP-04-T02（命令端口："用于规划/锁定分支"经命令应用 `IkBranchPolicy`）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`gui/KinematicsPlugin.hpp`、`gui/KinematicsPlugin.cpp`、`gui/panels/`（当前姿态、候选、任务/区域、能力探索和二维图）、`test/KinematicsGuiTest.cpp`
  - 修改：`CMakeLists.txt`（登记 `sdurws_ird_kinematics_plugin` 薄插件目标与 `sdurws_ird_kinematics_gui_test` 目标）
  - 创建：`out/test-evidence/wp-15/<run-id>/`（本卡工件）；不删除文件。
- **禁止修改的文件和公共接口：** 旧插件 `sdurws_kinematicanalysis` 及一切非本拥有目录源码；WP-10 会话状态模型与公共组件（只消费）；WP-04 命令/修订语义（双击不得直接产生修订）；WP-08 调度接口；计算核心 `sdurws_ird_kinematics` 内部类型（薄插件只消费公共头）；不读取其他插件 Widget 状态；不新增 symbol-registry 未登记公共符号。
- **修改前接口：** 无（GUI 层新增；基线旧链路为 `sdurws_kinematicanalysis` 面板直接写姿态，属 Rewrite 对照项）。
- **修改后接口：** 薄插件按 kinematics v0.4 §8 提供三模式：当前姿态、任务/区域评估、能力探索；包含候选表、失败点/薄弱区/碰撞对象筛选和二维图。双击任务或候选只改会话姿态；“用于规划/锁定分支”才经 WP-04 命令应用 `IkBranchPolicy`；导出引用快照而非当前 Widget；预览不创建修订、不触发结果失效。
- **实施步骤：**
  1. CMake 登记两目标并先写 `KinematicsGuiTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现三模式、当前姿态与候选面板：展示 `IkCandidate` 排序结果和不可应用原因，双击仅写会话姿态。
  3. 实现任务/区域与能力探索：失败点、薄弱区、碰撞对象和二维图与三维视图联动。
  4. 实现显式应用和导出：应用经 WP-04 命令端口，导出引用快照与结果 ID。
  5. 按 GUI 约束（`QT_QPA_PLATFORM=windows`、一次只启动一个 GUI 测试可执行文件）执行验证命令，写证据并提交。
- **RED 测试：** `CandidatePreviewDoesNotCreateRevision`；`DoubleClickOnlyChangesSessionPose`；`ExplicitApplyGoesThroughCommandPort`；`ExportReferencesSnapshotNotWidgetState`；`UnavailableCandidatesListedWithReasons`。
- **最小实现：** 三模式、当前姿态、候选、任务/区域、能力探索、二维图、显式应用与导出；跨入口一致性归 WP-15-T08。
- **正常/边界/失败测试：**
  - 正常：Given 评估结果含可应用候选，When 双击候选，Then 三维模型切换到该会话姿态且修订计数不变。
  - 边界：Given 候选全部不可应用或 150% 缩放，When 预览，Then 分组原因可见、应用入口禁用、模式和主按钮不被截断；Given 导出请求，Then 输出引用快照 ID 与 payload 而非 UI 缓存。
  - 失败：Given 用户直接构造分支锁定而无命令端口上下文，When 应用，Then 拒绝并提示经 WP-04 命令执行，不产生部分修订。
- **精确验证命令：**（仓库根目录、VS x64 环境；先 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_gui_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_gui_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_gui_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；GUI 层无工程计算逻辑（薄插件）；无双击直写修订/直写项目路径；无读取其他插件 Widget 状态；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-15/<run-id>/kinematics-gui-report.md`（三模式、候选、任务/区域、能力探索、二维图、应用/导出截图、修订计数和三档缩放记录）＋测试日志；独立评审者复核 AT-04/AT-05 对应行为。
- **提交格式：** `WP-15-T07: 新增运动学插件界面`

  - 新增运动学结果显示与目标点交互界面
  - 新增显示开关不变量测试
  - 新增运行证据记录
- **停止与升级条件：** 应用语义与 WP-04 命令端口或 WP-10 会话规则冲突、或 GUI 测试环境（QT_QPA_PLATFORM=windows）不可用时停止并升级 WP-10/WP-04 负责人；实现者不得担任本卡独立验证者。
