# WP-21-T07 联合分层优化工作台界面

- **Task ID / 需求 ID / ADR / 阶段：** WP-21-T07；OPT-04、OPT-08～10、UX-01～08、AT-09～12；ADR-003、ADR-004；阶段 D / R2。契约：`architecture/testing-contract.md` §3～§5、`architecture/evaluation-semantics.md` §5；模块详设 `module-design/optimization.md` v0.4 §8.1～§8.8、`module-design/session-ui.md` v0.4 §8。
- **基线 commit：** 代码基线 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档语义源 `optimization.md` v0.4。
- **前置任务及必需工件：** WP-21-T02（联合评估）、WP-21-T04（Pareto/稳健性/审计）、WP-21-T05（采用候选）、WP-20-T07（R1 静态优化界面）、WP-10-T06（工作台外壳）、WP-01-T03（测试入口）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/`）创建/修改 `gui/` 下 R2 分层漏斗、八指标、Pareto、候选详情、比较、审计、稳健性、正式复核和采用面板；创建 `test/JointOptimizationGuiTest.cpp`；修改本插件 `CMakeLists.txt` 仅追加 R2 源文件和复用 `sdurws_ird_optimization_gui_test`；创建 `out/test-evidence/wp-21/<run-id>/`。禁止删除或改写 WP-20-T07 静态用例文件。
- **禁止修改的文件和公共接口：** WP-20-T07 的 R1 静态实现与测试、WP-21-T01～T05 计算/采用接口、WP-08 调度、其他插件、WP-10 外壳、架构、Schema 和黄金数据；禁止绕过正式复核直接采用候选。
- **修改前接口：** R1 静态优化界面可用，R2 分层漏斗、八指标、Pareto、审计、稳健性与正式复核界面不存在。
- **修改后接口：** 同一优化插件按能力切换 R1/R2；R2 显示变量、目标/约束、运行控制、分层漏斗、八指标、Pareto、候选详情/比较、审计/稳健性；采用按钮仅对正式复核通过且当前的候选启用。
- **实施步骤：** 1) 写 R1/R2 模式边界、八指标和采用守卫 RED；2) 扩展 R2 变量与目标/约束；3) 实现漏斗、Pareto、候选详情与比较；4) 接入审计、稳健性和正式复核；5) 复用 GUI 目标并运行全部 R1/R2 用例。
- **RED 测试：** `R1StaticCasesRemainUnchanged`、`R2FunnelShowsLayerCountsAndReasons`、`CandidateTableHasEightMetrics`、`ParetoSelectionSynchronizesDetails`、`AdoptRequiresCurrentFormalReview`、`FailedRunKeepsAcceptedCandidates`。
- **最小实现：** §8 的 R2 展示与控制，并保持 R1 行为不变；不实现新优化算法、调度器、稳健性计算或采用命令。
- **正常/边界/失败测试：**
  - 正常：Given R2 结果，When 选择 Pareto 点和候选，Then 漏斗、八指标、详情、审计与稳健性使用同一候选身份。
  - 边界：Given 2～4 候选和 150% 缩放，When 比较，Then 差异列清晰，正式复核状态与采用按钮始终可见。
  - 失败：Given 候选过期、未正式复核或运行失败，When 请求采用，Then 阻断并说明处理动作，当前修订和已接纳候选不变。
- **精确验证命令：**（仓库根、Visual Studio x64 环境；GUI 平台固定为 windows，单次只运行本目标）
  - `$env:QT_QPA_PLATFORM='windows'; powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_gui_test$'`
  - 回退构建：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_gui_test`
  - 回退执行：`$env:QT_QPA_PLATFORM='windows'; $testExe=(Resolve-Path '.\out\build\industrial-robot\bin\Debug\sdurws_ird_optimization_gui_test.exe').Path; & $testExe`
- **diff 和禁止项检查：** diff 仅含 R2 `gui/` 扩展、新测试、CMake 和证据；`git diff --name-only -- RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/test` 不得包含 WP-20-T07 静态用例；`rg -n "applyCandidateDirect|runOptimizer\(" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/gui; if ($LASTEXITCODE -eq 0) { throw '检测到绕过正式复核或 GUI 越权计算' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中。
- **证据工件：** `out/test-evidence/wp-21/<run-id>/joint-optimization-ui.md`，包含 R1 回归、R2 漏斗和八指标、Pareto 联动、采用守卫、三档缩放及命令退出码。
- **提交格式：** `WP-21-T07: 新增联合分层优化工作台界面`

  - 新增 R2 分层漏斗、八指标与 Pareto 交互
  - 新增 正式复核和采用守卫 GUI 测试
  - 测试 保持 R1 静态模式行为不变
- **停止与升级条件：** R1/R2 契约冲突、八指标无法由冻结结果唯一映射或采用必须绕过正式复核时停止并升级所有者；实现者不得担任独立验证者。
