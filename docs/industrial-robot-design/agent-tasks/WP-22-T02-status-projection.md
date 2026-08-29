# WP-22-T02 状态与任务投影

- **Task ID / 需求 ID / ADR / 阶段：** WP-22-T02；UX-01、UX-03（失败提示含对象/实际值/要求值/原因/建议动作）、UX-06（统一状态词）、§5.4（用户可见状态）、AT-05（失效范围显示）；ADR-005（正交结果状态与命名，展示不得混排）、ADR-004（`StageStatusModel`/`TaskState` 只消费权威定义）。阶段 E / R1＋R2。契约：`architecture/evaluation-semantics.md` §4～§5（谓词与展示义务唯一权威）、`architecture/execution-model.md` §1（`TaskState` 9 态）；模块详设 `module-design/workflow-integration.md` v0.3 §4（建议规则表冻结）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-22-T01（转移表与驾驶舱骨架可用）；WP-10-T01/T03（`StageStatusModel` 八值〔session-ui §3 权威〕与任务面板组件）；WP-05-T03（`FeasibilityVerdict/gaps` 谓词输出）；工件：T01 用例通过、WP-02 各域状态样本（Current/Superseded/Historical、DataInsufficient/Partial/NotEvaluated、Quick/Verified）。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workflow/`）创建 `include/sdurws/ird/ui/workflow/NextStepAdvisor.hpp`、`src/NextStepAdvisor.cpp`；修改 `src/CockpitDashboard.cpp` 与 `include/sdurws/ird/ui/workflow/CockpitDashboard.hpp`（状态聚合消费挂点）、`test/WorkflowModelTest.cpp`（追加建议规则参数化与展示义务用例）、`testdata/`（状态样本）；写 `evidence/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-10 源文件（`StageStatusModel` 八值、任务面板组件只引用不复制）；WP-05 谓词实现；执行模型枚举定义；业务插件；`requirements.md`、CSV；不新增 CMake 目标。
- **修改前接口：** 无 `NextStepAdvisor`；驾驶舱未按阶段×评估入口聚合状态；`WorkflowModelTest` 无建议规则用例。
- **修改后接口：** `NextStepAdvisor` 按模块详设 §4 冻结规则表求值：`StageStatusModel` 八值（输入未完整/可计算/计算中/结果有效/需要重算/证据不足/计算失败/工程不可行）×附加条件（阶段 1～6 或 7、capabilities 暂停、retryable 等）输出建议动作；驾驶舱把 `IResultRepository`/`IEvaluationScheduler.snapshot` 按阶段×评估入口聚合为 `StageStatusModel` 八值（WP-10 模型消费，零复制）；任务面板复用 WP-10 组件（`TaskState` 9 态）。
- **实施步骤：**
  1. 写 RED 测试（建议规则表逐行参数化、展示义务矩阵、证据缺失诊断）。
  2. 实现阶段×评估入口聚合器（输入只读端口快照，输出 `StageStatusModel` 值）。
  3. 实现 `NextStepAdvisor` 规则表求值（规则条目逐行对齐模块详设 §4，含"证据不足按 `gaps` 分类给补数据/升级证据/调整用途入口，不得静默降级"）。
  4. 实现展示义务渲染：不可行按 `gaps` 列具体缺口；`DataInsufficient/Partial/NotEvaluated` 显式展示不与不可行混排；`Superseded`＝"需要重算"；`Historical` 保留原快照名称；Quick 不显示为正式通过（evaluation-semantics §5）。
  5. 挂接驾驶舱与任务面板，执行验证命令，写证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"` 无建议规则/展示义务用例；落地后全部通过。
- **最小实现：** 聚合器＋规则表＋展示义务渲染；不做候选比较（T03）、命令面板（T04）。
- **正常/边界/失败测试：**
  - 正常：Given 阶段 k 结果 `Current + Pass`，When 求建议，Then"按转移表进入下一阶段"（阶段 7 为"比较候选并生成评审报告"）。
  - 边界：Given `Superseded` 与 `Historical` 结果并存，When 渲染，Then 前者显示"需要重算"（附失效切片差异原因）、后者保留原快照名称；Given 计算失败且 retryable，When 求建议，Then"重试"否则"查看开发诊断（默认收起）"。
  - 失败：Given 阶段完成证据缺失时请求下一步建议，When 求值，Then `IRD-WF-EVIDENCE-MISSING`（Input/Warning）：显示缺口并回数据入口；Given Quick 结果，When 渲染，Then 不显示为正式通过。
- **精确验证命令：**（仓库根、VS x64 环境；`QCoreApplication` 模型测试）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_model_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；不出现 `StageStatusModel`/`TaskState`/评估枚举的本包定义（只 include WP-10 与执行模型公共头）；展示层不自行计算可行性（只渲染 `FeasibilityVerdict/gaps`）。
- **证据工件：** `ui/workflow/evidence/t02-status-projection.log`：状态展示矩阵（七阶段×八值逐格）、建议规则参数化结果表、命令原文与 commit。
- **提交格式：** `WP-22-T02: 状态与任务投影`
- **停止与升级条件：** evaluation-semantics §5 展示义务与模块详设 §4 规则表冲突、或 WP-10 `StageStatusModel` 公共头未交付时，停止并升级工作包所有者；实现者不得担任本卡独立验证者。
