# WP-15-T07 应用与 GUI

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T07；KIN-06（双击任务或候选只改会话姿态，不修改设计模型、不触发结果失效，P0）、KIN-07～08（三维视图失败点/薄弱区/碰撞对象显示与筛选/导出，P1）＋AT-04；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §5.6、`architecture/public-interfaces.md` §7、`architecture/evaluation-semantics.md` §5；GUI 约束见 WP-15 计划"验证命令"节。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.8）
- **前置任务及必需工件：** WP-15-T06（`ird.kinematics` 评估器与 `KinematicsSettings` 可用）；WP-10-T03（导航/诊断/表格公共组件）；WP-04-T02（命令端口："用于规划/锁定分支"经命令应用 `IkBranchPolicy`）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`gui/KinematicsPlugin.hpp`、`gui/KinematicsPlugin.cpp`、`gui/panels/`（候选预览、显式应用、导出面版）、`test/KinematicsGuiTest.cpp`
  - 修改：`CMakeLists.txt`（登记 `sdurws_ird_kinematics_plugin` 薄插件目标与 `sdurws_ird_kinematics_gui_test` 目标）
  - 创建：`out/test-evidence/wp-15/<run-id>/`（本卡工件）；不删除文件。
- **禁止修改的文件和公共接口：** 旧插件 `sdurws_kinematicanalysis` 及一切非本拥有目录源码；WP-10 会话状态模型与公共组件（只消费）；WP-04 命令/修订语义（双击不得直接产生修订）；WP-08 调度接口；计算核心 `sdurws_ird_kinematics` 内部类型（薄插件只消费公共头）；不读取其他插件 Widget 状态；不新增 symbol-registry 未登记公共符号。
- **修改前接口：** 无（GUI 层新增；基线旧链路为 `sdurws_kinematicanalysis` 面板直接写姿态，属 Rewrite 对照项）。
- **修改后接口：** 薄插件 `sdurws_ird_kinematics_plugin`（仅消费评估器公共头与 WP-10 组件）：候选预览、显式应用入口、导出。会话规则（kinematics.md §5.6，KIN-06）：双击任务/候选只改会话姿态（KIN-06），"用于规划/锁定分支"才经 WP-04 命令应用 `IkBranchPolicy` 并产生设计修改修订；导出引用快照而非当前 Widget；候选预览不创建修订、不触发结果失效。
- **实施步骤：**
  1. CMake 登记两目标并先写 `KinematicsGuiTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现候选预览面板：展示 `IkCandidate` 排序结果、逐项不可应用原因，双击仅写会话姿态（经 WP-10 会话状态）。
  3. 实现显式应用入口："用于规划/锁定分支"经 WP-04 命令端口产生修订。
  4. 实现导出：JSON/CSV 引用快照与结果 ID，不读取当前 Widget 状态。
  5. 按 GUI 约束（`QT_QPA_PLATFORM=windows`、一次只启动一个 GUI 测试可执行文件）执行验证命令，写证据并提交。
- **RED 测试：** `CandidatePreviewDoesNotCreateRevision`；`DoubleClickOnlyChangesSessionPose`；`ExplicitApplyGoesThroughCommandPort`；`ExportReferencesSnapshotNotWidgetState`；`UnavailableCandidatesListedWithReasons`。
- **最小实现：** 仅候选预览、显式应用与导出转绿所需；三维视图失败点/薄弱区渲染（KIN-07 P1）只预留面板挂点；跨入口一致性归 WP-15-T08。
- **正常/边界/失败测试：**
  - 正常：Given 评估结果含可应用候选，When 双击候选，Then 三维模型切换到该会话姿态且修订计数不变。
  - 边界：Given 候选全部不可应用，When 预览，Then 主列表为空、单列分组原因可见、应用入口禁用；Given 导出请求，Then 输出引用快照 ID 与 payload 而非 UI 缓存。
  - 失败：Given 用户直接构造分支锁定而无命令端口上下文，When 应用，Then 拒绝并提示经 WP-04 命令执行，不产生部分修订。
- **精确验证命令：**（仓库根目录、VS x64 环境；先 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_gui_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_gui_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_gui_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；GUI 层无工程计算逻辑（薄插件）；无双击直写修订/直写项目路径；无读取其他插件 Widget 状态；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-15/<run-id>/kinematics-gui-report.md`（预览/应用/导出操作记录与截图、修订计数对照、GUI 约束执行记录）＋测试日志（命令、commit、配置）；独立评审者复核 AT-04/AT-05 对应行为。
- **提交格式：** `WP-15-T07: 新增运动学插件界面`

  - 新增运动学结果显示与目标点交互界面
  - 新增显示开关不变量测试
  - 新增运行证据记录
- **停止与升级条件：** 应用语义与 WP-04 命令端口或 WP-10 会话规则冲突、或 GUI 测试环境（QT_QPA_PLATFORM=windows）不可用时停止并升级 WP-10/WP-04 负责人；实现者不得担任本卡独立验证者。
