# WP-22-T05 端到端用户流程

- **Task ID / 需求 ID / ADR / 阶段：** WP-22-T05；UX-01～08、AT-04（预览不改设计）、AT-05（失效范围显示）、AT-12（应用＝方案分支＋恰好一个新修订＋基线不覆盖）；ADR-001（单机械臂单项目作用域）、ADR-003（R1/R2 切片：第七阶段 R1 为 OPT-B 静态子集）。阶段 E / R1＋R2。契约：`architecture/testing-contract.md` §3（Given/When/Then）、§5（Windows GUI 规则：`QT_QPA_PLATFORM=windows`、一次一个 GUI 可执行文件）、`architecture/evaluation-semantics.md` §5；模块详设 `module-design/workflow-integration.md` v0.3 §8（测试矩阵：WorkflowGuiTest 为断言权威）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（workflow-integration.md v0.3）
- **前置任务及必需工件：** WP-22-T01～T04 全部完成（D6 DAG：转移表、状态投影与建议、比较视图、命令面板）；工件：`sdurws_ird_workflow_model_test` 全部用例通过；WP-02-T02/T03 黄金数据（models＋requirements 标准工位、optimization 样本）；WP-01-T03 `run-tests.ps1` 入口可用。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workflow/`）创建 `test/WorkflowGuiTest.cpp`、`testdata/`（新机型、改型、错误恢复三条固定任务脚本）；修改 `ui/` CMakeLists（登记 `sdurws_ird_workflow_test` GUI 回归目标）；写 `evidence/`（录屏与日志）。不删除文件。
- **禁止修改的文件和公共接口：** T01～T04 已交付实现（`StageTransitionTable`/`NextStepAdvisor`/`ComparisonView`/`CommandPalette` 只驱动不改源）；WP-10/WP-12 源文件；业务插件私有头与 Widget；`requirements.md`、CSV；不改 testing-contract §5 GUI 规则。
- **修改前接口：** `WorkflowGuiTest.cpp` 与 `sdurws_ird_workflow_test` 目标不存在；无固定任务脚本回归。
- **修改后接口：** `WorkflowGuiTest`＋CMake 目标 `sdurws_ird_workflow_test`，覆盖三条固定任务脚本（数据取自 `testdata/` 固定输入，逐条 Given/When/Then）：①新机型——建模→需求→运动学预检→轨迹与节拍→动力学校核→电机/减速器匹配→联合优化与候选比较全链路（ADR-001 单机械臂）；②改型——修改 TCP/负载后失效范围显示与重算（AT-05：仅实际依赖下游"需要重算"）；③错误恢复——注入计算失败→诊断（对象/实际值/要求值/原因/建议动作，UX-03）→重试或取消→恢复；含 AT-04（预览退出后设计恢复且无新增修订）与 AT-12（应用创建方案分支＋恰好一个新修订、基线不覆盖、修订数不随候选数增长）。模型级断言仍由 `sdurws_ird_workflow_model_test` 承载，GUI 回归不重复实现领域计算。
- **实施步骤：**
  1. 编写三条固定任务脚本（`testdata/`：新机型、改型、错误恢复），逐条标注覆盖的 UX/AT 断言。
  2. 写 RED 用例（脚本入口缺失/断言不满足时非零）。
  3. 实现 `WorkflowGuiTest`：经公共端口驱动驾驶舱（不读其他插件控件状态）。
  4. CMake 登记 `sdurws_ird_workflow_test`，在 VS x64＋`QT_QPA_PLATFORM=windows` 环境逐脚本执行并录屏。
  5. 写证据（录屏、日志、输入修订身份记录）。
- **RED 测试：** 实现前 `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_test` 失败（目标不存在）；落地后三条固定任务脚本全部通过。
- **最小实现：** 三条固定脚本 GUI 回归＋录屏与日志证据；不做新功能实现、不做性能测量（WP-23-T03）与系统级验收（WP-23-T01）。
- **正常/边界/失败测试：**
  - 正常：Given 新机型固定脚本，When 全链路执行，Then 七阶段完成证据逐段产出、评审报告生成（R1 为初步设计级并列证据等级）。
  - 边界：Given 改型脚本仅修改 TCP，When 查看下游，Then 仅实际依赖下游显示"需要重算"（AT-05）；Given 预览候选后退出，Then 场景恢复且修订号不变（AT-04）。
  - 失败：Given 注入计算失败，When 渲染，Then 失败提示含对象/实际值/要求值/原因/建议动作（UX-03）；Given 未通过正式可行的候选请求应用，Then 阻断、列 gaps 并保持当前修订（AT-12）。
- **精确验证命令：**（仓库根、VS x64 环境；GUI 一律 `QT_QPA_PLATFORM=windows` 且一次只运行一个 GUI 测试）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_test$'`
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test sdurws_ird_workflow_test`
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_test$"`
  - GUI 可执行文件运行前在同一 PowerShell 会话设置 `$env:QT_QPA_PLATFORM='windows'`；`sdurws_ird_workflow_model_test` 用 `QCoreApplication`、不需要 GUI 平台插件（testing-contract §5）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；GUI 用例不设置 offscreen、不并行启动多个 GUI 可执行文件；不改被测实现源文件；固定脚本输入不含开发机绝对路径。
- **证据工件：** `ui/workflow/evidence/t05-workflow-tests.log`：三条任务脚本录屏、测试日志（含 `QT_QPA_PLATFORM=windows` 设置记录与单实例执行顺序）、输入修订身份记录、状态展示矩阵（七阶段×八值，与 T02 证据合并归档）、命令原文与 commit。
- **提交格式：** `WP-22-T05: 端到端用户流程`
- **停止与升级条件：** 三条固定脚本无法在 `QT_QPA_PLATFORM=windows`＋单实例规则下稳定执行、或 T01～T04 任一交付物缺失时，停止并升级工作包所有者；实现者不得担任本卡独立验证者（独立评审按 WP-22 计划 §10 由产品、体验与独立测试人员按固定任务脚本复核 UX-01～08）。
