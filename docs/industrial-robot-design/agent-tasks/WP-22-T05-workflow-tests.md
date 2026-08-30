# WP-22-T05 端到端用户流程

- **Task ID / 需求 ID / ADR / 阶段：** WP-22-T05；UX-01～08、AT-04、AT-05、AT-12；ADR-001、ADR-003；阶段 E / R1＋R2。契约：`architecture/testing-contract.md` §3、§5、`architecture/evaluation-semantics.md` §5；模块详设 `module-design/workflow-integration.md` v0.4 §8、§10，`module-design/session-ui.md` v0.4 §8。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档语义源 `workflow-integration.md` v0.4。
- **前置任务及必需工件：** WP-22-T01～T04、WP-22-T06 全部完成；工件：`sdurws_ird_workflow_model_test` 全部用例通过；WP-02-T02/T03 黄金数据；WP-01-T03 测试入口可用。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workflow/`）创建 `test/WorkflowGuiTest.cpp`、`testdata/`（新机型、改型、错误恢复三条固定任务脚本）；修改 `ui/` CMakeLists（登记 `sdurws_ird_workflow_test` GUI 回归目标）；写 `evidence/`（录屏与日志）。不删除文件。
- **禁止修改的文件和公共接口：** T01～T04 已交付实现（`StageTransitionTable`/`NextStepAdvisor`/`ComparisonView`/`CommandPalette` 只驱动不改源）；WP-10/WP-12 源文件；业务插件私有头与 Widget；`requirements.md`、CSV；不改 testing-contract §5 GUI 规则。
- **修改前接口：** `WorkflowGuiTest.cpp` 与 `sdurws_ird_workflow_test` 目标不存在；无固定任务脚本回归。
- **修改后接口：** `WorkflowGuiTest`＋目标 `sdurws_ird_workflow_test` 覆盖五条固定脚本：无项目空态、从 URDF 新建项目、新机型全链路、改型失效与重算、错误恢复；每条在 100%/125%/150% 中指定档位运行，中央三维视图与主要操作保持可见。含 AT-04 预览恢复、AT-05 失效范围和 AT-12 应用守卫；模型级断言仍由 `sdurws_ird_workflow_model_test` 承载。
- **实施步骤：**
  1. 编写五条固定任务脚本（`testdata/`：无项目、URDF 新建、新机型、改型、错误恢复），逐条标注缩放档位与 UX/AT 断言。
  2. 写 RED 用例（脚本入口缺失/断言不满足时非零）。
  3. 实现 `WorkflowGuiTest`：经公共端口驱动驾驶舱（不读其他插件控件状态）。
  4. CMake 登记 `sdurws_ird_workflow_test`，在 VS x64＋`QT_QPA_PLATFORM=windows` 环境逐脚本执行并录屏。
  5. 写证据（录屏、日志、输入修订身份记录）。
- **RED 测试：** 实现前 `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_test` 失败（目标不存在）；落地后五条固定任务脚本全部通过。
- **最小实现：** 五条固定脚本、三档缩放 GUI 回归、录屏与日志证据；不做新功能实现或性能测量。
- **正常/边界/失败测试：**
  - 正常：Given 无项目或新机型脚本，When 从 URDF 新建并执行全链路，Then 项目入口与七阶段证据连续可用，建模界面无导入动作。
  - 边界：Given 100%/125%/150% 缩放和改型脚本仅修改 TCP，When 查看下游，Then 主操作与中央视图可见且仅实际依赖下游显示“需要重算”；预览退出后场景恢复且修订号不变。
  - 失败：Given 注入计算失败，When 渲染，Then 失败提示含对象/实际值/要求值/原因/建议动作（UX-03）；Given 未通过正式可行的候选请求应用，Then 阻断、列 gaps 并保持当前修订（AT-12）。
- **精确验证命令：**（仓库根、VS x64 环境；GUI 一律 `QT_QPA_PLATFORM=windows` 且一次只运行一个 GUI 测试）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test sdurws_ird_workflow_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_test$"`
  - GUI 可执行文件运行前在同一 PowerShell 会话设置 `$env:QT_QPA_PLATFORM='windows'`；`sdurws_ird_workflow_model_test` 用 `QCoreApplication`、不需要 GUI 平台插件（testing-contract §5）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；GUI 用例不设置 offscreen、不并行启动多个 GUI 可执行文件；不改被测实现源文件；固定脚本输入不含开发机绝对路径。
- **证据工件：** `ui/workflow/evidence/t05-workflow-tests.log`：五条任务脚本与三档缩放录屏、测试日志（含 `QT_QPA_PLATFORM=windows` 与单实例顺序）、输入修订记录、状态矩阵、命令原文与 commit。
- **提交格式：** `WP-22-T05: 端到端用户流程`
- **停止与升级条件：** 三条固定脚本无法在 `QT_QPA_PLATFORM=windows`＋单实例规则下稳定执行、或 T01～T04 任一交付物缺失时，停止并升级工作包所有者；实现者不得担任本卡独立验证者（独立评审按 WP-22 计划 §10 由产品、体验与独立测试人员按固定任务脚本复核 UX-01～08）。
