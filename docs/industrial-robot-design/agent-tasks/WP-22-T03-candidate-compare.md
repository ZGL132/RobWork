# WP-22-T03 候选比较与应用

- **Task ID / 需求 ID / ADR / 阶段：** WP-22-T03；UX-06、AT-04、AT-12；ADR-004；阶段 E / R1＋R2。契约：`architecture/evaluation-semantics.md` §4、`architecture/symbol-registry.md` SYM-OPT-005/006；模块详设 `module-design/workflow-integration.md` v0.4 §6～§7、§10.5，`module-design/optimization.md` v0.4 §8.5～§8.7。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-22-T02（状态投影可用）；外部消费 WP-20/21 结果对象：WP-20-T03/T04（静态指标与 Pareto）、WP-21-T04/T05（联合 `DesignCandidate`/`ParetoSet` 与应用路径）；WP-10-T02（`ISceneProjection.projectCandidate`）、WP-04-T02（命令端口）；工件：T01～T02 用例通过、WP-02 optimization 样本（候选与基线）。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/`）创建 `comparison/include/sdurws/ird/ui/comparison/ComparisonView.hpp`、`comparison/src/ComparisonView.cpp`、`comparison/include/sdurws/ird/ui/comparison/MetricDiffModel.hpp`、`comparison/src/MetricDiffModel.cpp`、`comparison/test/ComparisonModelTest.cpp`、`comparison/testdata/`；修改 `ui/` CMakeLists（把 ComparisonModelTest 编入 `sdurws_ird_workflow_model_test`）；写 `comparison/evidence/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-20/21 优化源文件（只读消费 `DesignCandidate`/`ParetoSet` 值对象）；WP-05 谓词；WP-10 `ISceneProjection`；WP-04 project 源文件；`requirements.md`、CSV；不新增 CMake 目标。
- **修改前接口：** `ui/comparison/` 目录不存在；`sdurws_ird_workflow_model_test` 不含 ComparisonModelTest；无比较视图与差异模型。
- **修改后接口：** `ComparisonView`＋`MetricDiffModel`：一次比较 2～4 个候选，指标固定为总体尺寸包络、结构质量、节拍、关节侧正机械功、器件质量、器件成本、最小关节裕量、最小驱动裕量；逐指标显示基线值、候选值、绝对/相对变化与差异高亮，小于 `comparisonTolerance` 标“无差别”；硬约束违反项单列。预览经 `ISceneProjection.projectCandidate`，应用走 WP-04 命令并守卫 `isFormallyFeasible`。
- **实施步骤：**
  1. 写 RED 测试（2～4 个候选、八项指标、差异高亮、无差别容差、应用守卫、`IRD-WF-NOT-COMPAREABLE`/`IRD-WF-APPLY-BLOCKED`）。
  2. 实现 `MetricDiffModel`：按 `ResultRef` 取候选与基线结果，八项指标聚合与差异计算（容差与激活目标读研究定义）。
  3. 实现 `ComparisonView` 差异高亮与硬约束违反单列。
  4. 实现可比性守卫：缺同名指标或含非正式可行项 → `IRD-WF-NOT-COMPAREABLE`（Input/Error）列出不可比项、拒绝整组比较。
  5. 实现预览（`projectCandidate` 会话态，AT-04 不产生修订）与应用入口（WP-04 命令；守卫失败 → `IRD-WF-APPLY-BLOCKED` 列 `gaps` 并保持当前修订；预览候选先复算为 Current 方可应用）。
  6. CMake 编入模型测试目标，执行验证命令，写证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"` 无 Comparison 用例；落地后全部通过。
- **最小实现：** 差异模型＋比较视图＋三守卫；不做 GUI 回归（T05）、不做报告生成（WP-12 入口在 T04 绑定）。
- **正常/边界/失败测试：**
  - 正常：Given 基线与两个正式可行候选（同名八项指标齐全），When 比较，Then 逐指标基线值/候选值/绝对/相对变化输出，差值小于 `comparisonTolerance` 的指标标"无差别"。
  - 边界：Given 候选仅与基线差值全部落在容差内，When 判定，Then 互不支配并列显示，不给出支配结论；Given 预览中的候选，When 请求应用，Then 要求先复算为 Current 结果。
  - 失败：Given 比较集缺同名指标或含非正式可行项，When 请求比较，Then `IRD-WF-NOT-COMPAREABLE` 拒绝整组比较并列出不可比项；Given 未通过正式可行判定的候选，When 请求应用，Then `IRD-WF-APPLY-BLOCKED` 列 `gaps`、保持当前修订。
- **精确验证命令：**（仓库根、VS x64 环境；`QCoreApplication` 模型测试）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_model_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；不重新声明八项指标集合或 `comparisonTolerance` 默认值（一律读研究定义）；无加权总分；预览路径无 revision 写入。
- **证据工件：** `ui/comparison/evidence/t03-candidate-compare.log`：八项指标聚合与差异对照表（WP-02 optimization 样本）、无差别容差边界样例、守卫触发诊断样例、命令原文与 commit。
- **提交格式：** `WP-22-T03: 候选比较与应用`
- **停止与升级条件：** WP-20 研究定义公共头无法提供激活目标与 `comparisonTolerance`、或 WP-21-T05 应用路径未交付时，停止并升级工作包所有者；实现者不得担任本卡独立验证者。
