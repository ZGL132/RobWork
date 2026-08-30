# WP-22 产品工作流整合实施计划

> 阶段/发布：阶段 E / R1＋R2；方案对齐 `module-design/workflow-integration.md` v0.4（本模块唯一权威，本文只做实施深化，不复述其冻结语义）；架构检查点 `IRD-D2-20260829`；需求基线 v0.8。
> 不新增领域权威实现：一切业务操作经公共端口；实现者、独立验证者与独立评审者必须是不同执行上下文（总纲 §4.1）。

**需求与契约：** UX-01～08、AT-04/05/12；架构契约与模块方案清单见 §2。
**拥有目录：** `industrialrobot/ui/workflow/` 与 `industrialrobot/ui/comparison/` 及其测试（文件树见 §3）。
**输入/输出：** 输入＝项目状态、阶段结果（`ResultEnvelope`/`ResultCurrentness`）、诊断与候选（`DesignCandidate`/`ParetoSet`）；输出＝驾驶舱状态投影、下一步建议、比较视图与项目命令请求（见 §4）。

## 1. 目标与非目标

**目标：** 交付无项目入口、新建项目向导、产品驾驶舱、七阶段转移表、命令面板、候选比较视图与下一步建议，把七个业务域整合为单项目单机械臂工作流。阶段状态唯一权威为 WP-10 `StageStatusModel`；任务面板复用 WP-10 组件；正式可行结论只渲染 WP-05 的 `FeasibilityVerdict/gaps`。URDF 只作为新建项目来源，建模插件不提供导入入口。
- 目标交付：`sdurws_ird_workflow`（含 workflow/ 与 comparison/）、`sdurws_ird_workflow_model_test`、`sdurws_ird_workflow_test`（GUI 回归）、状态展示矩阵与固定任务脚本证据。
- 完成定义：UX-01～08、AT-04/05/12 断言通过；R1 可独立完成并报告"建模至基础选型"闭环（第七阶段为 OPT-B 静态子集）。

**非目标：** 业务计算、结果接纳、场景投影（WP-10 `ISceneProjection`，本包只调用）、报告渲染（WP-12 `ReviewReportBuilder` 入口）、复制 `StageStatusModel`/`TaskState`/评估枚举定义、读取其他插件 Widget 私有状态。

## 2. 需求、契约与发布切片

- 需求：UX-01～08；§5.1（主流程与七阶段）、§5.3-5（候选比较须给相对基线变化）、§5.4（用户可见状态）、§9.3（默认八项比较指标与 `comparisonTolerance` 语义归 WP-20）；AT-04（预览不改设计）、AT-05（失效范围显示）、AT-12（应用候选：方案分支＋恰好一个新修订＋基线不覆盖＋复算一致＋修订数不随候选数增长）。
- 架构契约：`architecture/evaluation-semantics.md` §4～5（谓词与展示义务，本文不复制）、`architecture/execution-model.md` §1（任务状态机）、`architecture/public-interfaces.md` §1、§4～§6（命令/调度/结果仓库/策略场景诊断端口）、`architecture/symbol-registry.md`（SYM-OPT-005 `DesignCandidate`、SYM-OPT-006 `ParetoSet` 等，引用不复制）、`architecture/testing-contract.md`。
- 发布切片：R1（阶段 E 前段）完成第 1～6 阶段全链路与第 7 阶段 OPT-B 静态子集；R2 交付第 7 阶段全量联合优化入口。

## 3. 文件所有权与 CMake 目标

拥有目录两处，文件树以模块详设 §2 为权威：

```text
ui/workflow/   include/sdurws/ird/ui/workflow/{CockpitDashboard,StageTransitionTable,NextStepAdvisor,CommandPalette}.hpp
               src/{CockpitDashboard,StageTransitionTable,NextStepAdvisor,CommandPalette}.cpp
               project-entry/  test/{WorkflowModelTest,WorkflowGuiTest,ProjectEntryModelTest}.cpp  testdata/
ui/comparison/ include/sdurws/ird/ui/comparison/{ComparisonView,MetricDiffModel}.hpp
               src/{ComparisonView,MetricDiffModel}.cpp
               test/ComparisonModelTest.cpp  testdata/
```

CMake 目标：`sdurws_ird_workflow`（含 workflow/ 与 comparison/）、`sdurws_ird_workflow_model_test`（`QCoreApplication` 模型测试，覆盖 WorkflowModelTest＋ComparisonModelTest）、`sdurws_ird_workflow_test`（GUI 回归）。允许依赖：WP-03/04/05/08/09/10/12 公共头、WP-20/21 结果值对象读取（`DesignCandidate`、`ParetoSet`）、Qt Widgets/Model-View、RobWorkStudio 主窗口框架；禁止：业务插件私有头与 Widget、读取其他插件控件、绕过命令服务写项目、复制 `StageStatusModel`/`TaskState`/评估枚举定义。

## 4. 输入/输出与数据流

- 输入（只读端口）：`IResultRepository.findLatest/currentness`（`ResultEnvelope`、`ResultCurrentness`）、`IEvaluationScheduler.snapshot`、`IDiagnosticCatalog`、`IProjectQuery`、WP-12 报告构建器；候选取 `DesignCandidate`，输入以 `ResultRef`/runId 引用基线与候选（文件路径不是结果身份）；激活目标与 `comparisonTolerance` 读取优化研究定义（WP-20），本包不重新声明。
- 输出：驾驶舱状态投影（阶段×评估入口聚合为 `StageStatusModel` 八值）、下一步建议、命令请求（经 `IProjectCommandService`/`TaskHandle`/WP-12 端口发出，不在本包内执行业务计算）、比较视图与差异高亮；一切写操作只产生命令，不直写 revision。
- 数据流：端口快照 → 阶段状态聚合（WP-10 模型消费）→ 转移表与建议规则求值 → 驾驶舱/命令面板渲染；比较视图按 `ResultRef` 取候选 → 八项指标聚合与差异计算 → 预览经 `ISceneProjection.projectCandidate`（会话态，退出恢复）→ 应用走 WP-04 命令。
- 错误码（提名，经 WP-09 目录登记后使用）：`IRD-WF-EVIDENCE-MISSING`（阶段完成证据缺失时请求下一步建议，Input/Warning）、`IRD-WF-NOT-COMPAREABLE`（比较集缺同名指标或含非正式可行项，Input/Error，拒绝整组比较）、`IRD-WF-APPLY-BLOCKED`（候选未通过正式可行判定仍请求应用，Engineering/Error，列出 gaps 并保持当前修订）。

## 5. 冻结行为裁决（引用模块详设 §3～§6，实施不得偏离）

1. **七阶段转移表**（模块详设 §3 冻结表为权威）：建模→需求→运动学预检→轨迹与节拍→动力学校核→电机/减速器匹配→联合优化与候选比较，逐阶段进入条件与完成证据（阶段 1 `CompiledRobotArtifacts` 全成全败＋三套等价模型 FK/轴线/惯量 §15.3；阶段 2 就绪校验 REQ-06；阶段 3 批量工位 Current 且覆盖缺口为 DataInsufficient；阶段 4 Verified 轨迹含碰撞协议结论与限值节拍；阶段 5 逆动力学包络峰值可定位、RMS 含驻留；阶段 6 可行组合或逐项淘汰诊断；阶段 7 可行 Pareto 集不含硬约束失败候选＋`ReviewReport` 生成）。导航是建议不是强制：工程师可自由返回上游，驾驶舱必须显示受影响下游"需要重算"；比较候选与评审报告是第七阶段的完成出口。
2. **R1 切片**：第七阶段 R1 为 OPT-B 静态子集，全量入口显示"需要 R2 能力"不可用；R1 报告为初步设计级并列证据等级。
3. **状态投影与展示义务**：按 evaluation-semantics §5——不可行按 `gaps` 列具体缺口；`DataInsufficient/Partial/NotEvaluated` 显式展示、不与不可行混排；`Superseded` 显示"需要重算"、`Historical` 保留原快照名称；Quick 结果不得显示为正式通过。
4. **下一步建议规则表**（模块详设 §4 为权威）：按 `StageStatusModel` 八值×附加条件给建议动作（输入未完整→跳转第一条 Input 诊断编辑位置；可计算→启动阶段计算并提示 Quick/Verified 用途；计算中→查看进度/暂停/取消；结果有效→按转移表进入下一阶段或（阶段 7）比较并生成报告；需要重算→显示失效切片差异并重算；证据不足→按 gaps 分类补数据/升级证据/调整用途，不得静默降级；计算失败→retryable 重试否则查看开发诊断（默认收起）；工程不可行→查看违反项（对象/实际值/要求值）返回上游）。
5. **CommandPalette 首版命令集（冻结）**：保存项目；撤销/重做；运行阶段计算/快速检查；取消任务；切换阶段 1～7；应用为当前方案；比较方案；生成评审报告；打开工程策略和项目命令。URDF 新建项目归 WP-22-T06 的无项目入口，不作为导入命令。命令只绑定既有端口；前置不满足时显示简短工程原因，不显示哈希、Schema 或内部插件名。
6. **ComparisonView**：指标集＝需求 §9.3 默认八项（总体尺寸包络、结构质量、节拍、关节侧正机械功、器件质量、器件成本、最小关节裕量、最小驱动裕量）；差异高亮逐指标给出基线值、候选值、绝对/相对变化；小于 `comparisonTolerance` 标"无差别"不构成支配；硬约束违反项单独列出，不被总分掩盖。候选预览经 `ISceneProjection.projectCandidate`（会话态，AT-04 不产生修订）；应用入口走 WP-04 命令——"设为当前方案"创建方案分支＋恰好一个新修订（AT-12），预览中的候选必须先复算为 Current 结果方可应用。

## 6. 任务依赖 DAG

```text
WP-22-T01 → WP-22-T02 → WP-22-T04
WP-22-T02 → WP-22-T03
WP-04、WP-11、WP-13-T03、WP-10-T06、WP-22-T01、WP-22-T04 → WP-22-T06
WP-22-T01～T04、WP-22-T06 全部完成 → WP-22-T05
```

## 7. 逐任务深化

### WP-22-T01 阶段导航与作用域
- 代码范围：`ui/workflow/include/sdurws/ird/ui/workflow/StageTransitionTable.hpp`、`CockpitDashboard.hpp`；`ui/workflow/src/StageTransitionTable.cpp`、`CockpitDashboard.cpp`；`test/WorkflowModelTest.cpp`（转移表用例）。
- 前置任务：无（包内首任务；外部前置 WP-10、WP-12～21 由总纲 §5.4 规定）。
- 输出工件：七阶段转移表实现（模块详设 §3 冻结表）与驾驶舱导航；单机械臂作用域显示；R1 第七阶段 OPT-B 静态子集与"需要 R2 能力"不可用入口。
- 验收断言：`WorkflowModelTest`（模块详设 §8）——转移表逐行（证据存在/缺失两分支）；导航不强制（自由返回上游时下游显示"需要重算"）。

### WP-22-T02 状态与任务投影
- 代码范围：`ui/workflow/include/sdurws/ird/ui/workflow/NextStepAdvisor.hpp`；`ui/workflow/src/NextStepAdvisor.cpp`；`CockpitDashboard` 状态聚合消费；`test/WorkflowModelTest.cpp`（建议规则参数化用例）。
- 前置任务：WP-22-T01。
- 输出工件：阶段×评估入口聚合投影（消费 WP-10 `StageStatusModel` 八值，不复制定义）与下一步建议规则表（模块详设 §4）；任务面板复用 WP-10 组件（`TaskState` 9 态）。
- 验收断言：`WorkflowModelTest`——建议规则表参数化；展示义务按 evaluation-semantics §5（`DataInsufficient/Partial/NotEvaluated` 不与不可行混排、`Superseded`＝"需要重算"、`Historical` 保留原快照名称、Quick 不显示为正式通过）；证据缺失请求建议 → `IRD-WF-EVIDENCE-MISSING`。

### WP-22-T03 候选比较与应用
- 代码范围：`ui/comparison/include/sdurws/ird/ui/comparison/ComparisonView.hpp`、`MetricDiffModel.hpp`；`ui/comparison/src/ComparisonView.cpp`、`MetricDiffModel.cpp`；`ui/comparison/test/ComparisonModelTest.cpp`。
- 前置任务：WP-22-T02；外部消费 WP-20/21 结果对象（`DesignCandidate`/`ParetoSet`）。
- 输出工件：比较视图与差异模型（八项指标、绝对/相对变化、无差别容差、硬约束违反单列）；预览经 `ISceneProjection.projectCandidate`；应用走 WP-04 命令并守卫 `isFormallyFeasible`。
- 验收断言：`ComparisonModelTest`（模块详设 §8）——八项指标聚合、差异高亮、无差别容差、应用守卫（WP-02 optimization 样本）；缺同名指标或含非正式可行项 → `IRD-WF-NOT-COMPAREABLE` 拒绝整组比较；未通过正式可行仍请求应用 → `IRD-WF-APPLY-BLOCKED` 列 gaps；应用创建方案分支＋恰好一个新修订（AT-12），候选先复算为 Current 方可应用。

### WP-22-T04 诊断与报告入口
- 代码范围：`ui/workflow/include/sdurws/ird/ui/workflow/CommandPalette.hpp`；`ui/workflow/src/CommandPalette.cpp`；`test/WorkflowModelTest.cpp`（命令守卫用例）。
- 前置任务：WP-22-T01、WP-22-T02。
- 输出工件：CommandPalette 首版命令集（模块详设 §5 九组，冻结）与守卫；诊断入口经 `IDiagnosticCatalog`（UX-03 失败提示含对象/实际值/要求值/原因/建议动作）；报告入口经 WP-12 `ReviewReportBuilder`。
- 验收断言：`WorkflowModelTest`——命令守卫（前置不满足显示不可用原因；UX-02 不显示哈希/Schema/内部插件名）；撤销/重做空态显示 `IRD-PROJ-NOTHING-TO-*` 文案、跨分支/失效命令给诊断；取消任务守卫＝存在非终态任务。

### WP-22-T05 端到端用户流程
- 代码范围：`ui/workflow/test/WorkflowGuiTest.cpp`；`ui/workflow/testdata/`（固定任务脚本）；`out/test-evidence/wp-22/<run-id>/`（录屏与日志）。
- 前置任务：WP-22-T01～T04、WP-22-T06。
- 输出工件：无项目、URDF 新建、新机型、改型、错误恢复五条固定脚本；七阶段×八值矩阵；100%/125%/150% 录屏、日志和输入修订记录。
- 验收断言：`WorkflowGuiTest`——AT-04/05/12、五条脚本和三档缩放通过；中央三维视图与主操作可见；GUI 按 Windows 单实例规则执行。

### WP-22-T06 项目入口与新建项目向导
- 代码范围：`ui/workflow/project-entry/`、`test/ProjectEntryModelTest.cpp`、本模块 CMake 与 `out/test-evidence/wp-22/<run-id>/`。
- 前置任务：WP-04、WP-11、WP-13-T03、WP-10-T06、WP-22-T01、WP-22-T04。
- 输出工件：无项目入口，空白模板、内置样例、URDF 三种新建来源，摘要确认、取消与错误恢复；模型用例编入 `sdurws_ird_workflow_model_test`。
- 验收断言：所有来源都经 WP-04 项目命令原子创建；URDF 经 WP-11/WP-13 端口；失败无半项目；建模 GUI 无 URDF/模型导入或独立检查动作。

## 8. 测试矩阵（模块详设 §8 为断言权威）

| 测试目标/文件 | 断言要点 | 覆盖需求 |
| --- | --- | --- |
| `sdurws_ird_workflow_model_test` / WorkflowModelTest.cpp | 转移表逐行（证据存在/缺失两分支）、建议规则表参数化、命令守卫 | UX-01～04、§5.1 |
| 同上 / ComparisonModelTest.cpp | 八项指标聚合、差异高亮、无差别容差、应用守卫 | §5.3-5、§9.3、AT-12 |
| 同上 / ProjectEntryModelTest.cpp | 无项目、三种新建来源、取消、错误恢复、原子创建和建模禁导入 | UX-01～05、MDL-01/11 |
| `sdurws_ird_workflow_test` / WorkflowGuiTest.cpp | AT-04/05/12；无项目、URDF 新建、新机型、改型、错误恢复；三档缩放 | UX-01～08、AT-04/05/12 |

模型测试使用 `QCoreApplication`；GUI 回归在 Visual Studio x64 环境设置 `QT_QPA_PLATFORM=windows` 且一次只启动一个 GUI 测试可执行文件（testing-contract §5）。

## 验证命令（双形式，仓库根执行；GUI 目标单独运行）

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_model_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test sdurws_ird_workflow_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_test$"
```

GUI 可执行文件运行前在同一 PowerShell 会话设置 `$env:QT_QPA_PLATFORM='windows'`，一次只运行一个；`sdurws_ird_workflow_model_test` 不需要 GUI 平台插件。

## 10. 独立验证与独立评审

- 独立验证（黑盒）：状态展示矩阵（七阶段×八值）逐格验证、失效传播注入（AT-05：改 TCP/负载与单独改电机成本的失效范围差异）、应用候选后基线不被覆盖与修订数不随候选数增长（AT-12）。
- 独立评审：由产品、体验与独立测试人员按固定任务脚本复核工作流；评审 UX-01～08 逐条签署。
- 证据：状态展示矩阵（七阶段×八值）、任务脚本录屏、测试日志、输入修订身份与评审签名（分别写入 `out/test-evidence/wp-22/<run-id>/`、`out/test-evidence/wp-22/<run-id>/`）。

## 11. 迁移与删除（模块详设 §9）

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| 旧插件菜单与跨 Widget 适配入口 | Rewrite → Delete | 新驾驶舱通过 UX-01～08 后删除重复导航 |
| 各插件私有阶段/状态映射 | Delete | `StageStatusModel` 投影覆盖全部入口 |
| 旧候选比较表格与临时报告导出 | Delete | ComparisonView＋WP-12 报告入口通过 AT-12 |

## 退出条件

- UX-01～08、AT-04、AT-05、AT-12 断言通过；R1 可独立完成并报告建模至基础选型闭环（第七阶段 OPT-B 静态子集，全量入口显示"需要 R2 能力"）。
- 阶段状态、任务状态、评估枚举均消费权威定义（WP-10/执行模型/评估语义），本包零复制；正式可行结论只来自 WP-05 谓词。
- 一切写操作经公共端口命令；预览不改设计、不产生修订。
- §11 删除清单执行完毕，重复导航与状态映射退出构建。

## 13. 人周（总纲 §5.4：6～9 人周，含实现/测试/评审/修正）

| 任务 | 人周 |
| --- | ---: |
| WP-22-T01 | 1～1.5 |
| WP-22-T02 | 1～1.5 |
| WP-22-T03 | 1.5～2 |
| WP-22-T04 | 1～1.5 |
| WP-22-T05 | 1.5～2.5 |
| WP-22-T06 | 1～1.5 |

## 任务卡索引

- [WP-22-T01 阶段导航与作用域](../agent-tasks/WP-22-T01-stage-navigation.md)
- [WP-22-T02 状态与任务投影](../agent-tasks/WP-22-T02-status-projection.md)
- [WP-22-T03 候选比较与应用](../agent-tasks/WP-22-T03-candidate-compare.md)
- [WP-22-T04 诊断与报告入口](../agent-tasks/WP-22-T04-diagnostic-guidance.md)
- [WP-22-T05 端到端用户流程](../agent-tasks/WP-22-T05-workflow-tests.md)
- [WP-22-T06 项目入口与新建项目向导](../agent-tasks/WP-22-T06-project-entry.md)
