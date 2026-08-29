# WP-22-T01 阶段导航与作用域

- **Task ID / 需求 ID / ADR / 阶段：** WP-22-T01；UX-01（首次进入阶段显示目标/必需输入/当前问题/下一步）、UX-06（统一状态词与图例）、§5.1（七阶段主流程）、AT-04（预览不改设计）；ADR-001（单机械臂单项目作用域）、ADR-003（R1/R2 切片：第七阶段 R1 为 OPT-B 静态子集）。阶段 E / R1＋R2。契约：`architecture/public-interfaces.md` §1、`architecture/evaluation-semantics.md` §5、`architecture/testing-contract.md` §5；模块详设 `module-design/workflow-integration.md` v0.3 §3（七阶段转移表为冻结权威）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（workflow-integration.md v0.3）
- **前置任务及必需工件：** 无包内前置；外部前置：WP-10-T01/T03（`StageStatusModel` 八值与公共组件）、WP-12-T01（报告对象）、WP-13～21 各域入口（总纲 §5.4）；工件：`sdurws_ird_session_ui_test` 与 `sdurws_ird_reporting_test` 通过、WP-02 各域数据集可引用。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/`）创建 `workflow/include/sdurws/ird/ui/workflow/StageTransitionTable.hpp`、`workflow/src/StageTransitionTable.cpp`、`workflow/include/sdurws/ird/ui/workflow/CockpitDashboard.hpp`、`workflow/src/CockpitDashboard.cpp`、`workflow/test/WorkflowModelTest.cpp`（转移表用例）、`workflow/testdata/`；创建/修改 `ui/` CMakeLists（`sdurws_ird_workflow`、`sdurws_ird_workflow_model_test` 目标）；写 `workflow/evidence/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-10 session-ui 源文件（`StageStatusModel` 只消费不复制定义）；WP-03～09/12 公共头；业务插件源文件与私有 Widget；`requirements.md`、CSV；不读取其他插件控件状态。
- **修改前接口：** `ui/workflow/` 目录与 `sdurws_ird_workflow(_model_test)` 目标不存在；无七阶段转移表实现。
- **修改后接口：** `StageTransitionTable` 按模块详设 §3 冻结表逐行实现七阶段（建模→需求→运动学预检→轨迹与节拍→动力学校核→电机/减速器匹配→联合优化与候选比较）进入条件与完成证据查询；`CockpitDashboard` 提供导航（建议不强制，可自由返回上游）与单机械臂作用域显示；R1 第七阶段为 OPT-B 静态子集、全量入口显示"需要 R2 能力"不可用。CMake 新增 `sdurws_ird_workflow`（含 workflow/ 与 comparison/ 目录骨架）与 `sdurws_ird_workflow_model_test`（`QCoreApplication`）。
- **实施步骤：**
  1. 写 RED 测试（转移表逐行证据存在/缺失两分支、自由返回上游、R1 切片不可用入口）。
  2. 定义 `StageTransitionTable` 数据结构：逐行进入条件与完成证据谓词（只调用 `IResultRepository.findLatest/currentness` 与 `IEvaluationScheduler.snapshot`）。
  3. 实现 `CockpitDashboard` 导航与作用域显示（单项目单机械臂，ADR-001）。
  4. 实现返回上游时受影响下游"需要重算"显示（消费 `ResultCurrentness`，不自行计算失效）。
  5. 实现 R1 第七阶段 OPT-B 静态子集与"需要 R2 能力"入口。
  6. CMake 登记目标，执行验证命令，写证据。
- **RED 测试：** 实现前 `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test` 失败（目标不存在）；落地后转移表用例全部通过。
- **最小实现：** 转移表＋驾驶舱导航＋作用域显示＋R1 切片；不做状态投影/建议规则（T02）、比较视图（T03）、命令面板（T04）、GUI 回归（T05）。
- **正常/边界/失败测试：**
  - 正常：Given 阶段 1 完成证据存在（`CompiledRobotArtifacts` 全成全败＋三套等价模型 §15.3 一致），When 查询转移表，Then 允许进入阶段 2 且下一步建议为"定义任务需求"。
  - 边界：Given 阶段 3 批量工位覆盖缺口为 `DataInsufficient`，When 查询完成证据，Then 该阶段不判完成（缺口非通过）；Given 用户从阶段 5 返回阶段 2 修改输入，When 查看下游，Then 阶段 3～5 显示"需要重算"（导航不强制）。
  - 失败：Given 阶段完成证据缺失（无 `ResultEnvelope`），When 请求进入下游建议，Then 拒绝给出完成结论并指向证据缺口（`IRD-WF-EVIDENCE-MISSING` 提名语义，最终经 WP-09 登记后使用）。
- **精确验证命令：**（仓库根、VS x64 环境；`_model_test` 用 `QCoreApplication`，不需要 GUI 平台插件）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_model_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；`ui/workflow/` 不出现 `StageStatusModel` 枚举定义复制（只 include WP-10 头）；无业务计算代码；无直写 revision。
- **证据工件：** `ui/workflow/evidence/t01-stage-navigation.log`：转移表逐行结果矩阵（证据存在/缺失两分支）、R1 切片入口截图或状态记录、命令原文与 commit。
- **提交格式：** `WP-22-T01: 阶段导航与作用域`
- **停止与升级条件：** 模块详设 §3 冻结表与各域完成证据端口（WP-05/08）无法对接、或 `StageStatusModel` 公共头缺失时，停止并升级工作包所有者；实现者不得担任本卡独立验证者。
