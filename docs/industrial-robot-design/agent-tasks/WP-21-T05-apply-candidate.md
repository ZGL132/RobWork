# WP-21-T05 候选预览与应用

- **Task ID / 需求 ID / ADR / 阶段：** WP-21-T05；OPT-08（候选归 `OptimizationRunResult` 不产生修订；"设为当前方案"＝方案分支＋恰好一个新修订＋完整复算）、MDL-08、AT-12、NFR-PERF-04～06；ADR-004（应用经 WP-04 命令，不直写 revision）。阶段 D / R2。契约：`architecture/candidate-compilation.md` §4（候选不得创建项目修订）、`architecture/public-interfaces.md`（WP-04 `DomainCommand` 端口）；模块详设 `module-design/optimization.md` v0.3 §3（方案分支应用包）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-21-T02、WP-21-T04（可行集与 Pareto 判定可用）；WP-04-T02（`DomainCommand`/方案分支命令端口）；WP-20-T06（`ResultApplicationTest` 共享夹具与静态应用实现）；WP-20-T07（`OptimizationPlugin` 与 `gui/panels/` 面板骨架，`sdurws_ird_optimization_gui_test` 可用）；工件：T01～T04 用例通过。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/`）创建 `joint/include/sdurws/ird/opt/CandidateApplication.hpp`、`joint/src/CandidateApplication.cpp`；修改 `gui/panels/` 候选预览/应用面板扩展、`test/ResultApplicationTest.cpp`（追加联合应用子句，与 WP-20-T06 共享夹具）、`test/OptimizationGuiTest.cpp`（追加联合预览/应用 GUI 用例）与模块 CMakeLists；写 `out/test-evidence/wp-21/<run-id>/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-04 project 源文件（只发 `DomainCommand`）；WP-10 `ISceneProjection`（只调用）；WP-20 definition/candidate 源文件；WP-05 谓词；`requirements.md`、CSV、`schemas/`；不新增 CMake 目标（面板行为由既有 `sdurws_ird_optimization_gui_test` 扩展用例覆盖）。
- **修改前接口：** 无联合候选应用包类型；面板仅有 WP-20 静态候选预览/应用入口；`ResultApplicationTest` 无联合应用子句。
- **修改后接口：** `CandidateApplication.hpp` 提供方案分支应用包（`DesignVector`＋`writeSetFingerprint`＋目标分支名，optimization.md §3）经 WP-04 `DomainCommand` 发出："设为当前方案"创建方案分支＋恰好一个新修订并触发完整复算（OPT-08/AT-12），运行期间修订数不随候选数量增长；候选预览不建修订；不可行候选（未通过 `isFormallyFeasible`）不可应用。
- **实施步骤：**
  1. 写 RED 测试：`ResultApplicationTest` 联合应用子句（应用计数、修订数不随候选增长、不可行拒用）与 `OptimizationGuiTest` 联合用例（预览不改设计、应用确认）。
  2. 定义应用包类型与 WP-04 命令组装（含 `writeSetFingerprint` 校验）。
  3. 实现预览路径（只读展示，不建修订、不触发复算）。
  4. 实现"设为当前方案"路径：守卫＝候选满足 `isFormallyFeasible`，否则列 `gaps` 并保持当前修订；通过后仅创建一个新修订并完整复算。
  5. 扩展 `gui/panels/` 面板入口与确认文案。
  6. 执行验证命令（模型与 GUI 双目标），写证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition_test$"` 无联合应用子句、`-R "^sdurws_ird_optimization_gui_test$"` 无联合 GUI 用例；落地后全部通过。
- **最小实现：** 应用包＋预览＋守卫＋单修订复算触发；不做 AT-10～14 证据装配（T06）；不改动 WP-20 静态应用行为。
- **正常/边界/失败测试：**
  - 正常：Given 正式可行的联合候选，When"设为当前方案"，Then 创建方案分支＋恰好一个新修订并触发完整复算，基线不被覆盖；运行期间产生 100 个候选后修订数不变（AT-12）。
  - 边界：Given 预览中的候选（未复算为 Current），When 请求应用，Then 要求先复算为 Current 结果方可应用（会话态预览不构成应用依据）。
  - 失败：Given 硬约束失败或证据不足候选，When 请求应用，Then 拒绝并列出 `gaps`，当前修订保持不变；Given `writeSetFingerprint` 不匹配，When 组装应用包，Then 拒绝且不产生部分命令。
- **精确验证命令：**（仓库根、VS x64 环境；GUI 目标设 `$env:QT_QPA_PLATFORM='windows'` 且一次只启动一个 GUI 可执行文件）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_joint_test$'`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition_test$'`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_gui_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test sdurws_ird_optimization_definition_test sdurws_ird_optimization_gui_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition_test$"`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_gui_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；`joint/` 无直写 revision 代码（只经 WP-04 命令）；WP-20 静态应用用例零回归；GUI 用例不在无平台插件环境运行。
- **证据工件：** `out/test-evidence/wp-21/<run-id>/t05-apply-candidate.log`：应用前后修订计数对照（候选数 0→100 修订数不变）、不可行拒用诊断样例、GUI 用例结果、命令原文与 commit。
- **提交格式：** `WP-21-T05: 候选预览与应用`
- **停止与升级条件：** WP-04 命令端口无法表达方案分支＋单修订＋复算事务、或 WP-20-T06 共享夹具不可复用时，停止并升级 WP-04 所有者与工作包所有者；实现者不得担任本卡独立验证者。
