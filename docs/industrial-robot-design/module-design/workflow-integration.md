# 产品工作流整合模块详细方案（workflow-integration）

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；负责 WP：WP-22（阶段 E / R1+R2）；任务卡：`agent-tasks/WP-22-T01～T05`
- 架构契约：`architecture/evaluation-semantics.md`（§4～5）、`architecture/execution-model.md`（§1）、`architecture/public-interfaces.md`（§1、§4～§6）、`architecture/symbol-registry.md`、`architecture/testing-contract.md`；代码前置 WP-10、WP-12～21（总纲 §5.4），不新增领域权威实现

## 1. 模块职责与前置裁决

拥有产品驾驶舱（CockpitDashboard）、七阶段转移表、CommandPalette、候选比较视图（ComparisonView）与"下一步建议"（NextStepAdvisor）。阶段状态唯一权威为 WP-10 `StageStatusModel`（八值，以 `module-design/session-ui.md` §3 定义为准，本模块只消费不复制）；任务面板复用 WP-10 组件（`TaskState` 9 态以 execution-model §1 为准）。一切业务操作经公共端口：命令经 `IProjectCommandService`、运行经 `IEvaluationScheduler`、结果与当前性经 `IResultRepository`、诊断经 `IDiagnosticCatalog`、报告经 WP-12 `ReviewReportBuilder`；正式可行结论只渲染 WP-05 谓词输出的 `FeasibilityVerdict/gaps`，本模块不自行判定。非目标：业务计算、结果接纳、场景投影（WP-10 `ISceneProjection`）、报告渲染。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workflow/
  include/sdurws/ird/ui/workflow/
    CockpitDashboard.hpp StageTransitionTable.hpp NextStepAdvisor.hpp CommandPalette.hpp
  src/CockpitDashboard.cpp StageTransitionTable.cpp NextStepAdvisor.cpp CommandPalette.cpp
  test/WorkflowModelTest.cpp WorkflowGuiTest.cpp  testdata/ evidence/
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/comparison/
  include/sdurws/ird/ui/comparison/ ComparisonView.hpp MetricDiffModel.hpp
  src/ComparisonView.cpp MetricDiffModel.cpp
  test/ComparisonModelTest.cpp  testdata/ evidence/
```

CMake target：`sdurws_ird_workflow`（含 workflow/ 与 comparison/）、`sdurws_ird_workflow_model_test`（QCoreApplication）、`sdurws_ird_workflow_test`（GUI 回归）。允许依赖：WP-03/04/05/08/09/10/12 公共头、WP-20/21 结果值对象读取（`DesignCandidate` SYM-OPT-005、`ParetoSet` SYM-OPT-006）、Qt Widgets/Model-View、RobWorkStudio 主窗口框架；禁止：业务插件私有头与 Widget、读取其他插件控件、绕过命令服务写项目、复制 `StageStatusModel`/`TaskState`/评估枚举定义。

## 3. 七阶段转移表（冻结，自需求 §5.1 主流程推导）

导航是建议不是强制：工程师可自由返回上游（需求 §5.1），驾驶舱必须显示受影响下游"需要重算"；比较候选与评审报告是第七阶段的完成出口。

| # | 阶段 | 进入条件 | 完成证据 | 下一步建议 |
| --- | --- | --- | --- | --- |
| 1 | 机械臂与环境建模 | 项目已创建或导入基线修订（URDF 生成不可修改基线，§5.3） | `CompiledRobotArtifacts` 全成全败编译成功；三套等价模型 FK/轴线/惯量满足 §15.3（AT-01/15/16/17） | 定义任务需求 |
| 2 | 任务需求定义 | 阶段 1 结果有效 | 就绪校验通过：无非法 Must/Should、任务/区域/负载引用可解析（REQ-06） | 运动学预检 |
| 3 | 运动学预检 | 阶段 2 结果有效 | 批量工位验证结果 Current 且判定可解释；覆盖缺口为 DataInsufficient 而非通过（AT-03） | 规划路径并计算节拍 |
| 4 | 轨迹与节拍 | 阶段 3 结果有效 | Verified 轨迹完成：碰撞协议结论、限值与节拍满足或违反项可定位（AT-06） | 动力学校核 |
| 5 | 动力学校核 | 阶段 4 结果有效 | 逆动力学包络完成：峰值可定位轨迹时刻、RMS 含驻留（AT-07） | 电机/减速器匹配 |
| 6 | 电机/减速器匹配 | 阶段 5 结果有效 | 可行组合，或每个淘汰项含实际值/阈值诊断（AT-08） | R2：分层联合优化；R1：直接比较候选并报告 |
| 7 | 联合优化与候选比较 | 阶段 6 结果有效（R2 全量；R1 为 OPT-B 静态子集，全量入口显示"需要 R2 能力"不可用） | 可行 Pareto 集不含硬约束失败候选（AT-09～12）；`ReviewReport` 生成（R1 为初步设计级并列证据等级） | 应用候选设为当前方案 → 复算 → 评审 |

## 4. 状态投影与下一步建议规则表

投影数据源：`IResultRepository.findLatest/currentness`（`ResultEnvelope`、`ResultCurrentness`）与 `IEvaluationScheduler.snapshot`，按阶段×评估入口聚合为 `StageStatusModel` 八值；展示义务按 evaluation-semantics §5——不可行按 `gaps` 列具体缺口，`DataInsufficient/Partial/NotEvaluated` 显式展示不与不可行混排，`Superseded` 显示"需要重算"、`Historical` 保留原快照名称，Quick 结果不得显示为正式通过。

| StageStatusModel | 附加条件 | 建议动作 |
| --- | --- | --- |
| 输入未完成 | — | 跳转第一条 Input 诊断的编辑位置（UX-01/03） |
| 可计算 | — | 启动当前阶段计算（Quick/Verified 按用途提示） |
| 计算中 | capabilities 声明暂停 | 查看进度、暂停或取消 |
| 结果有效 | 阶段 1～6 | 按转移表进入下一阶段 |
| 结果有效 | 阶段 7 | 比较候选并生成评审报告 |
| 需要重算 | — | 显示变化原因（失效切片差异）并重算 |
| 证据不足 | 缺评估器／证据等级低／警告类别未允许 | 按 `gaps` 分类给补充数据、升级证据或调整用途入口；不得静默降级 |
| 计算失败 | retryable | 重试；否则查看开发诊断（默认收起） |
| 工程不可行 | — | 查看违反项（对象/实际值/要求值），返回对应上游阶段 |

## 5. CommandPalette 首版命令集（冻结）

命令是 UI 意图到既有端口的绑定，不新增接口；前置不满足时显示不可用原因（UX-02 工程用语，不显示哈希/Schema/内部插件名）：

| 命令 | 绑定 | 守卫 |
| --- | --- | --- |
| 保存项目 | WP-04 保存（草稿随存） | 项目已打开 |
| 撤销 / 重做 | `IProjectCommandService.undo/redo` | 无可撤销/重做时显示 `IRD-PROJ-NOTHING-TO-*` 文案；跨分支/已失效命令给诊断（需求 §10.1） |
| 运行阶段计算 / 快速检查 | `IEvaluationScheduler.submit`（Verified/Quick） | 阶段=可计算；Quick 提示不作正式证据 |
| 取消任务 | `TaskHandle.cancel` | 存在非终态任务 |
| 切换阶段 1～7 | 驾驶舱导航 | 无守卫；显示下游需重算 |
| 应用为当前方案 | WP-04 命令（方案分支） | 候选满足 `isFormallyFeasible` |
| 比较方案 | ComparisonView | ≥2 个可比较结果 |
| 生成评审报告 | WP-12 `ReviewReportBuilder` | 输入完整，否则列 `IRD-RPT-INPUT-INCOMPLETE` 缺口 |
| 打开工程策略／打开、另存项目／导入 URDF | WP-07/WP-10 策略入口；WP-04/WP-11 | 项目已打开 |

## 6. ComparisonView（候选比较视图）

- 输入以 `ResultRef`/runId 引用基线与候选（文件路径不是结果身份），候选取 `DesignCandidate`（SYM-OPT-005）；指标集＝需求 §9.3 默认八项比较指标（总体尺寸包络、结构质量、节拍、关节侧正机械功、器件质量、器件成本、最小关节裕量、最小驱动裕量），激活目标与 `comparisonTolerance` 读取优化研究定义（WP-20），本模块不重新声明。
- 差异高亮：逐指标给出基线值、候选值、绝对/相对变化（需求 §5.3-5）；小于 `comparisonTolerance` 标"无差别"不构成支配；硬约束违反项单独列出，不被总分掩盖。
- 候选预览经 `ISceneProjection.projectCandidate`（会话态，退出恢复，AT-04 不产生修订）；应用入口走 WP-04 命令——"设为当前方案"创建方案分支＋恰好一个新修订（AT-12：基线不覆盖、复算一致、运行期间修订数不随候选数增长），预览中的候选必须先复算为 Current 结果方可应用。

## 7. 错误码（提名，经 WP-09 目录登记后使用）

| 码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-WF-EVIDENCE-MISSING` | 阶段完成证据缺失时请求下一步建议 | Input | Warning | 显示缺口并回数据入口 |
| `IRD-WF-NOT-COMPAREABLE` | 比较集缺同名指标或含非正式可行项 | Input | Error | 列出不可比项，拒绝整组比较 |
| `IRD-WF-APPLY-BLOCKED` | 候选未通过正式可行判定仍请求应用 | Engineering | Error | 列出 gaps；保持当前修订 |

## 8. 测试与证据

| 测试 | 覆盖 | 目标 |
| --- | --- | --- |
| WorkflowModelTest | 转移表逐行（证据存在/缺失两分支）、建议规则表参数化、命令守卫 | `sdurws_ird_workflow_model_test` |
| ComparisonModelTest | 八项指标聚合、差异高亮、无差别容差、应用守卫（WP-02 optimization 样本） | `sdurws_ird_workflow_model_test` |
| WorkflowGuiTest | AT-04/05/12；新机型、改型、错误恢复固定任务脚本（T05） | `sdurws_ird_workflow_test` |

验证命令（脚本与原生双形式，均在仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_(model_)?test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test sdurws_ird_workflow_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_(model_)?test$"
```

GUI 约束同 testing-contract §5（`QT_QPA_PLATFORM=windows`，一次一个 GUI 可执行文件）。证据：状态展示矩阵（七阶段×八值）、任务脚本录屏、测试日志、输入修订身份与评审签名。

## 9. 迁移与删除表

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| 旧插件菜单与跨 Widget 适配入口 | Rewrite → Delete | 新驾驶舱通过 UX-01～08 后删除重复导航 |
| 各插件私有阶段/状态映射 | Delete | `StageStatusModel` 投影覆盖全部入口 |
| 旧候选比较表格与临时报告导出 | Delete | ComparisonView＋WP-12 报告入口通过 AT-12 |
