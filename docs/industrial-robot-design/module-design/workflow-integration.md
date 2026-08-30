# 产品工作流整合模块详细方案（workflow-integration）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；负责 WP：WP-22（阶段 E / R1+R2）；任务卡：`agent-tasks/WP-22-T01～T05`
- 架构契约：`architecture/evaluation-semantics.md`（§4～5）、`architecture/execution-model.md`（§1）、`architecture/public-interfaces.md`（§1、§4～§6）、`architecture/symbol-registry.md`、`architecture/testing-contract.md`；代码前置 WP-10、WP-12～21（总纲 §5.4），不新增领域权威实现

## 1. 模块职责与前置裁决

拥有产品驾驶舱（CockpitDashboard）、七阶段转移表、CommandPalette、候选比较视图（ComparisonView）与"下一步建议"（NextStepAdvisor）。阶段状态唯一权威为 WP-10 `StageStatusModel`（八值，以 `module-design/session-ui.md` §3 定义为准，本模块只消费不复制）；任务面板复用 WP-10 组件（`TaskState` 9 态以 execution-model §1 为准）。一切业务操作经公共端口：命令经 `IProjectCommandService`、运行经 `IEvaluationScheduler`、结果与当前性经 `IResultRepository`、诊断经 `IDiagnosticCatalog`、报告经 WP-12 `ReviewReportBuilder`；正式可行结论只渲染 WP-05 谓词输出的 `FeasibilityVerdict/gaps`，本模块不自行判定。非目标：业务计算、结果接纳、场景投影（WP-10 `ISceneProjection`）、报告渲染。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workflow/
  include/sdurws/ird/ui/workflow/
    CockpitDashboard.hpp StageTransitionTable.hpp NextStepAdvisor.hpp CommandPalette.hpp
  src/CockpitDashboard.cpp StageTransitionTable.cpp NextStepAdvisor.cpp CommandPalette.cpp
  test/WorkflowModelTest.cpp WorkflowGuiTest.cpp  testdata/                      # 证据统一写 out/test-evidence/wp-xx/<run-id>/（AGENTS §3）
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/comparison/
  include/sdurws/ird/ui/comparison/ ComparisonView.hpp MetricDiffModel.hpp
  src/ComparisonView.cpp MetricDiffModel.cpp
  test/ComparisonModelTest.cpp  testdata/                      # 证据统一写 out/test-evidence/wp-xx/<run-id>/（AGENTS §3）
```

CMake target：`sdurws_ird_workflow`（含 workflow/ 与 comparison/）、`sdurws_ird_workflow_model_test`（QCoreApplication）、`sdurws_ird_workflow_test`（GUI 回归）。允许依赖：WP-03/04/05/08/09/10/12 公共头、WP-20/21 结果值对象读取（`DesignCandidate` SYM-OPT-005、`ParetoSet` SYM-OPT-006）、Qt Widgets/Model-View、RobWorkStudio 主窗口框架；禁止：业务插件私有头与 Widget、读取其他插件控件、绕过命令服务写项目、复制 `StageStatusModel`/`TaskState`/评估枚举定义。

## 3. 七阶段转移表（冻结，自需求 §5.1 主流程推导）

导航是建议不是强制：工程师可自由返回上游（需求 §5.1），驾驶舱必须显示受影响下游"需要重算"；比较候选与评审报告是第七阶段的完成出口。

| # | 阶段 | 进入条件 | 完成证据 | 下一步建议 |
| --- | --- | --- | --- | --- |
| 1 | 机械臂与环境建模 | 项目已创建（含从 URDF 新建项目） | `CompiledRobotArtifacts` 全成全败编译成功；三套等价模型 FK/轴线/惯量满足 §15.3（AT-01/15/16/17） | 定义任务需求 |
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
| 打开工程设置／打开、另存项目 | WP-07/WP-10 策略入口；WP-04/WP-11 | 按命令要求检查项目状态 |
| 从 URDF 新建项目 | WP-04/WP-11 新建流程 | 无未处理编辑；仅在主窗口项目入口提供 |

## 6. ComparisonView（候选比较视图）

- 输入以 `ResultRef`/runId 引用基线与候选（文件路径不是结果身份），候选取 `DesignCandidate`（SYM-OPT-005）；指标集＝需求 §9.3 默认八项比较指标（总体尺寸包络、结构质量、节拍、关节侧正机械功、器件质量、器件成本、最小关节裕量、最小驱动裕量），激活目标与 `comparisonTolerance` 读取优化研究定义（WP-20），本模块不重新声明。
- 差异高亮：逐指标给出基线值、候选值、绝对/相对变化（需求 §5.3-5）；小于 `comparisonTolerance` 标"无差别"不构成支配；硬约束违反项单独列出，不被总分掩盖。
- 候选预览经 `ISceneProjection.projectCandidate`（会话态，退出恢复，AT-04 不产生修订）；应用入口走 WP-04 命令——"设为当前方案"创建方案分支＋恰好一个新修订（AT-12：基线不覆盖、复算一致、运行期间修订数不随候选数增长），预览中的候选必须先复算为 Current 结果方可应用。

## 7. 错误码（已登记入 diagnostics.md §3，D10 裁决）

| 码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-WF-EVIDENCE-MISSING` | 阶段完成证据缺失时请求下一步建议 | Engineering | Warning | 显示缺口并回数据入口 |
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

## 10. 工程工作流界面

本节负责项目入口、阶段导航、跨阶段状态、候选比较和报告入口。主窗口 Dock、公共按钮、任务、诊断和错误展示统一引用 [session-ui.md §8](session-ui.md#8-工程工作台-ui-规范)。

### 10.1 无项目入口

```text
┌────────────────────────────── 三维视图 ──────────────────────────────┐
│                                                                      │
│                         工业机械臂工作台                             │
│                    [ 新建项目 ]  [ 打开项目 ]                        │
│                                                                      │
│ 最近项目                                                             │
│  · 装配机器人方案                                                    │
│  · 上下料机器人方案                                                  │
└──────────────────────────────────────────────────────────────────────┘
```

无项目时，七个阶段入口、运行、应用更改和报告均禁用；保留项目菜单、新建、打开和最近项目。最近项目打不开时保留列表项，显示“项目位置不可用”，提供“重新选择”和“从列表移除”。

项目菜单：

| 命令 | 启用条件 | 结果 |
| --- | --- | --- |
| 新建项目 | 始终可用；有未处理编辑时先确认 | 打开新建项目向导 |
| 打开项目 | 始终可用；有未处理编辑时先确认 | 打开文件选择器 |
| 保存 | 项目已打开且存在更改 | 保存项目和草稿 |
| 另存为 | 项目已打开 | 保存为新位置并切换当前项目 |
| 关闭项目 | 项目已打开 | 返回无项目入口 |
| 最近项目 | 存在最近记录 | 直接打开或显示位置不可用 |

### 10.2 新建项目向导

向导固定为三步，主按钮依次为“下一步”“创建”；返回上一步保留已填写内容。

```text
┌──────────────────────────── 新建项目 ────────────────────────────────┐
│ ①项目信息 ───── ②初始来源 ───── ③创建确认                           │
├──────────────────────────────────────────────────────────────────────┤
│ 项目名称  [ 装配机器人方案                         ]                 │
│ 保存位置  [ D:\RobotProjects\Assembly             ] [选择]          │
│ 说明      [ 六轴装配机器人研发                     ]                 │
│                                                                      │
│                     [取消] [上一步] [下一步/创建]                    │
└──────────────────────────────────────────────────────────────────────┘
```

| 步骤 | 字段 | 校验与状态 |
| --- | --- | --- |
| 项目信息 | 项目名称、保存位置、说明 | 名称与路径合法后“下一步”启用 |
| 初始来源 | 从模板、从 URDF、空白项目 | 必须选择一个来源；来源变化不清空项目信息 |
| 创建确认 | 来源摘要、机器人摘要、资源处理、保存位置 | 所有必要字段合法后“创建”启用 |

从模板时显示模板名称、机器人类型和自由度摘要。空白项目只要求机器人名称。选择“从 URDF”后显示：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| URDF 文件 | 是 | 支持浏览选择；错误显示在字段下方 |
| 机器人名称 | 是 | 默认取文件内名称，可修改 |
| 主链起点 | 是 | 从可用连杆中选择 |
| 主链终点 | 是 | 从可用连杆中选择 |
| 目标运动链 | 是 | 默认使用主链 |
| 连续关节范围 | 是 | 每个连续关节给出工程范围 |
| 资源处理 | 是 | 复制到项目或引用原位置 |

读取问题直接显示在字段下方或确认页问题列表中，不提供独立模型检查页。创建期间“创建”禁用并显示进度；取消或失败不得留下半成品项目。成功后直接打开项目并进入建模阶段。

URDF 只属于主窗口新建项目流程。建模阶段不得再提供 URDF 导入、模型导入、导入报告或独立模型检查入口。从 URDF 创建的项目以通用关节表示生成不可修改的基线修订（需求 §5.3）；创建后的编辑均发生在方案分支上，基线本身不提供编辑入口。

### 10.3 七阶段工具栏

```text
项目 ▾ │ 1 建模 │ 2 需求 │ 3 运动学 │ 4 轨迹 │ 5 动力学 │ 6 选型 │ 7 优化
       │ 当前阶段状态 │ 撤销 │ 重做 │ 应用更改 │ 运行 │ 取消 │ 更多
```

- 点击阶段按钮只抬升对应左侧 Dock 和主要底部结果页，不替换中央三维视图。
- 阶段按钮始终允许返回上游；尚未满足入口的下游阶段可打开查看，但计算按钮禁用并显示首个缺项。
- 上游方案应用更改后，受影响的下游阶段统一显示“需重算”，仍允许查看旧结果，但不得把旧结果用于正式报告。
- 工具栏状态使用 session-ui §8.4 的八个短标签；禁用原因不超过一句话。
- “更多”只放低频操作，例如布局恢复、页面帮助和阶段导出；工程设置使用右侧公共面板。

### 10.4 阶段流转与下一步

| 当前阶段 | 首屏主要输入 | 首屏主要结果 | 下一步 |
| --- | --- | --- | --- |
| 建模 | 结构、几何、物性、工具、环境 | 模型状态与诊断 | 定义需求 |
| 需求 | 工位、区域、工艺、负载 | 就绪状态 | 运动学验证 |
| 运动学 | 当前模型与需求目标 | 可达性、覆盖率、逆解候选 | 轨迹规划 |
| 轨迹 | 只读任务序列与规划设置 | 轨迹段、节拍、碰撞和限位 | 动力学校核 |
| 动力学 | 有效轨迹与物性 | 关节侧负载包络 | 器件选型 |
| 选型 | 各轴需求与产品目录 | 可行组合与整机草案 | 联合优化或报告 |
| 优化 | 变量、目标、约束 | Pareto 候选和比较 | 正式复核、采用方案 |

下一步建议只给一个主要按钮。存在多个缺口时，先定位最上游的阻断项，其余问题保留在诊断表。

### 10.5 候选比较

比较视图支持同时选择 2～4 个方案，默认按共同指标并列：

| 指标 | 基准方案 | 方案 A | 方案 B | 说明 |
| --- | ---: | ---: | ---: | --- |
| 尺寸包络 |  |  |  | 越小越优或目标区间 |
| 结构质量 |  |  |  | 质量差异 |
| 节拍 |  |  |  | 正式计算结果 |
| 机械功 |  |  |  | 关节侧口径 |
| 器件质量 |  |  |  | 目录数据 |
| 器件成本 |  |  |  | 目录版本对应价格 |
| 最小关节裕量 |  |  |  | 越大越优 |
| 最小驱动裕量 |  |  |  | 越大越优 |

硬约束违反项单独置顶，不生成综合总分。选择一列时三维视图预览对应方案；退出比较恢复当前方案。只有正式复核通过且结果仍有效时，“设为当前方案”才启用；确认后形成一个新方案版本并触发相关阶段重算。

### 10.6 报告对话框

| 分组 | 字段 |
| --- | --- |
| 基本信息 | 报告名称、方案、保存位置 |
| 包含内容 | 模型、需求、运动学、轨迹、动力学、选型、方案比较、假设、遗留问题 |
| 输出格式 | HTML；按需附带 JSON 数据或 CSV 表格 |

“生成”在报告名称、方案和保存位置有效时启用。缺少正式结果的章节默认不选；用户选择后显示“该章节依据不足”，报告中必须保留相同提示。生成失败时保留全部选项和路径，提供“重试”和“详情”。报告只读取所选方案和已保存的计算结果，不读取界面控件临时状态或未应用草稿。

### 10.7 页面状态

| 场景 | 提示 | 主要操作 |
| --- | --- | --- |
| 无项目 | 新建或打开项目后开始设计 | 新建项目 |
| 阶段输入不足 | 请先完成标出的必要项 | 定位缺项 |
| 下游结果过期 | 上游方案已变化，需要重新计算 | 重新运行 |
| 比较项不足 | 至少选择两个可比较方案 | 选择方案 |
| 报告依据不足 | 部分章节没有正式结果 | 查看缺项 |

系统错误沿用 session-ui §8.8；工作流层不得用内部任务标识、文件摘要或插件名称替代工程提示。
