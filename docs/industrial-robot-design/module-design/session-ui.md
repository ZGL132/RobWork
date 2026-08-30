# 会话、场景与公共 UI 模块详细方案

- 方案版本：v0.4；需求基线：v0.8；架构检查点：`IRD-D2-20260829`
- 负责 WP：WP-10；阶段/发布：阶段 A / R1；任务卡：`agent-tasks/WP-10-T01～T05`
- 架构契约：`architecture/execution-model.md`（§1）、`architecture/public-interfaces.md`（§1、§4、§6）、`architecture/evaluation-semantics.md`（§1、§5）、`architecture/symbol-registry.md`、`architecture/testing-contract.md`

## 1. 模块职责与前置裁决

模块拥有无 QWidget 的 `SessionState`、`EditDraft`、`StageStatusModel`（八值，归本模块所有；WP-22 workflow-integration 自 D5 起改为引用本定义，不再自定义同名模型）、`SelectionModel`、只读场景投影 `ISceneProjection`（签名见 public-interfaces §6）和公共 Qt 组件。Widget 只发出用户意图，业务命令、评估和策略一律经公共端口执行；模块不直接写项目 revision、不修改领域对象。

两级前置裁决：① 代码前置 WP-03～05、09（总纲 §5.2），状态模型、草稿、投影与组件的模型层可先行实现与测试；② 端口契约前置 WP-07/08——策略入口消费 `IEngineeringPolicyProvider`（public-interfaces §6，WP-07），任务面板消费 `IEvaluationScheduler`/`TaskSnapshot`（§4，WP-08），签名已由 D2 冻结，集成期代码交付依赖 WP-07/08（总纲将同步标注），UI 侧先以契约测试替身开发。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/
  include/sdurws/ird/ui/
    SessionState.hpp EditDraft.hpp StageStatusModel.hpp SelectionModel.hpp
    ISceneProjection.hpp SceneProjection.hpp
    EngineeringPolicyPanel.hpp DiagnosticPanel.hpp
    EngineeringTableView.hpp VirtualResultModel.hpp StageNavigationView.hpp
  src/SessionState.cpp DraftController.cpp StageStatusModel.cpp SceneProjection.cpp
      SelectionModel.cpp PolicyPanel.cpp DiagnosticPanel.cpp
      EngineeringTableView.cpp VirtualResultModel.cpp
  test/SessionStateTest.cpp SceneProjectionTest.cpp CommonComponentsTest.cpp
      PolicyUiTest.cpp ResponsiveListsTest.cpp
  testdata/                      # 证据统一写 out/test-evidence/wp-xx/<run-id>/（AGENTS §3）
```

CMake target：`sdurws_ird_ui`、`sdurws_ird_ui_model_test`、`sdurws_ird_ui_widget_test`。允许依赖：WP-03～05/09 公共头（代码依赖，见 §1 裁决①）、WP-07/08 端口头（集成期接入，裁决②）、Qt Widgets/Model-View、RobWorkStudio 场景 API；禁止：业务插件私有对象与 Widget 头、跨线程操作 QWidget、直接写项目文件、绕过 `IProjectCommandService` 的持久化和手工 CSV。

## 3. 数据与接口

- `SessionState`：`sessionId`、`projectRef`、`selectedObjectId`、`cameraPose`、`visibility`、`colorMode`、`filter`、`jogPose`、`playbackState`、`previewRef`；会话内可变，更新不产生 revision（需求 §5.5）。
- `EditDraft`：`draftId`、`baseRevisionRef`、`patches[]`、`validationDiagnostics[]`、`dirty`、`savedAt`；保存到 WP-04 drafts，不进入 `EvaluatorInputSlice`；应用前必须重新比较 base revision，冲突交命令服务。
- `StageStatusModel` 八值：输入未完成、可计算、计算中、结果有效、需要重算、证据不足、计算失败、工程不可行（需求 §5.4）。映射：需要重算 = `Superseded`、历史证据 = `Historical`（`ResultCurrentness`，evaluation-semantics §1/§5）；`DataInsufficient/Partial/NotEvaluated` 显式展示并按 `gaps` 列举缺口，不与"不可行"混排（§5 展示义务）。
- 候选显示使用 `DesignCandidate`（SYM-OPT-005 规范名，禁止 `CandidateResult`）；投影 `projectCurrent()/projectCandidate(ResultRef)` 只读源，节点以 objectId 绑定，显示名称经 WP-06 `IRuntimeNameResolver`。
- 任务面板显示 `TaskState` 9 态（SYM-STA-006，execution-model §1）：面板只读 `TaskSnapshot`，18 条转移表以契约为准、本模块不复制；任务生命周期不与阶段状态混用（需求 §5.4）。

## 4. 调用与状态

```text
Widget(用户意图) -> DraftController/EditDraft -> 用户点击"应用"
  -> IProjectCommandService.apply(base, cmd)   // 一次应用恰好一个新 revision，commandId 幂等
  -> 阶段状态刷新为"需要重算"
投影：ProjectRevision/AnalysisSnapshot/DesignCandidate -> SceneProjection(只读)
  -> RobWorkStudio 场景节点(objectId 绑定) -> selection/Jog/preview 只写 SessionState
  -> 预览退出/失败 -> 恢复已保存姿态与当前 revision，不反写领域数据
```

| 码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| IRD-UI-PASTE-INVALID | 批量粘贴值非法（单位/范围/引用不存在） | Input | Error | 逐行定位并拒绝该行，不调用命令服务 |
| IRD-PROJ-STALE-REVISION | 应用时 base revision 已过期（WP-04 码，UI 透传） | Input | Error | 刷新后回到草稿重新应用 |
| IRD-UI-PROJECTION-FAILED | 场景投影或 Qt 模型/后台任务故障 | System | Error | 保持当前场景与 revision 不变，可重试 |

## 5. 关键实现约定

- 线程：所有 QWidget 与 model mutation 在 GUI 主线程；评估、文件读取和大列表分页在后台，经 queued signal 返回不可变值。
- 响应式预算（NFR-PERF-01/03）：5,000 任务、100,000 摘要、10,000 候选分页/虚拟加载，不一次性创建明细对象；导航/选择/筛选/编辑/切换 P95 ≤ 200 ms；超过 1 s 的工作转后台；主线程连续阻塞不超过 2 s。
- 策略与显示隔离（UX-08）："计算模式"属 `EngineeringPolicySet`（WP-07），修改走 EditDraft + 用户应用；"显示碰撞几何/高亮"属 `SessionState`，立即生效且不得触发命令、快照、缓存失效或重算；插件不得提供同名私有开关。
- 高级设置折叠显示 seed、求解器和开发诊断，默认流程不依赖（UX-04）；阶段导航显示每阶段状态、阻塞诊断和下一步建议（UX-01）。

## 6. 测试与证据

| 测试 | 覆盖 | 目标 |
| --- | --- | --- |
| SessionStateTest | 状态隔离、草稿应用、revision 冲突、预览恢复、objectId 联动、八值状态 | `sdurws_ird_ui_model_test` |
| SceneProjectionTest | 只读投影、候选/历史切换与恢复、不反写 | `sdurws_ird_ui_model_test` |
| CommonComponentsTest | 导航、诊断面板、表格批量粘贴、单位显示、分页虚拟化 | `sdurws_ird_ui_widget_test` |
| PolicyUiTest | 唯一策略入口、策略摘要显示、计算/显示开关隔离 | `sdurws_ird_ui_widget_test` |
| ResponsiveListsTest | 规模数据、P95、主线程阻塞窗口、后台转移 | `sdurws_ird_ui_widget_test` |

验证命令（脚本与原生双形式，均在仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_model_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_widget_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_ui_model_test sdurws_ird_ui_widget_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_ui_(model|widget)_test$"
```

GUI 约束：Windows Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动 `sdurws_ird_ui_widget_test.exe` 一个 GUI 可执行文件；模型测试仅用 QCoreApplication，不需要 GUI 平台插件。证据含截图/录屏、操作脚本、日志、输入 revision 和评审者。

## 7. 迁移与删除表

| 旧资产 | 处置 | 说明 |
| --- | --- | --- |
| 旧插件 Widget 内业务校验与文件读写 | Rewrite | 收敛到公共端口与 EditDraft（需求 §13.3） |
| 会话/显示状态写回设计数据的路径 | Delete | 迁移期 adapter 只读，证明隔离后删除 |
| 显示开关与计算开关耦合的私有配置 | Delete | 拆分为 SessionState 与 EngineeringPolicySet 两类 |
| 旧私有阶段/任务状态枚举 | Delete | 并入 StageStatusModel 与 TaskState 面板显示 |

## 8. 工程工作台 UI 规范

本节是工业机械臂研发工作台公共外壳的界面权威。各业务模块只定义本阶段的字段、表格和操作，不得复制本节的 Dock、状态、选择联动与错误展示规则。

### 8.1 RobWorkStudio 兼容边界

- RobWorkStudio 原生三维视图始终作为主窗口中央部件，不被业务页面、欢迎页或结果页替换。
- 工业机械臂功能通过工具栏和 `QDockWidget` 接入；Dock 继续支持移动、隐藏、标签化和浮动。
- 工业工作流只管理自身 Dock。“恢复工业默认布局”不得关闭、重排或重置其他 RobWorkStudio 插件。
- 原生三维选择、相机控制、渲染和场景树能力继续可用；业务模块只向公共场景投影提交显示意图。

### 8.2 主窗口与 Dock 分区

```text
┌────────────────────────────────────────────────────────────────────────────┐
│ 菜单栏：文件  编辑  视图  工具  帮助                                    │
├────────────────────────────────────────────────────────────────────────────┤
│ 项目 ▾ │ 建模 │ 需求 │ 运动学 │ 轨迹 │ 动力学 │ 选型 │ 优化 │状态│操作│
├──────────────┬──────────────────────────────────────┬──────────────────────┤
│ 当前阶段     │                                      │ 对象属性 / 点动       │
│ 业务 Dock    │         RobWorkStudio 三维视图       │ 工程设置 / 场景显示   │
│              │                                      │                      │
├──────────────┴──────────────────────────────────────┴──────────────────────┤
│ 阶段结果 / 曲线 / 任务 / 诊断 / 计算依据 / 日志                          │
├────────────────────────────────────────────────────────────────────────────┤
│ 项目 │ 方案版本 │ 结果状态 │ 当前任务 │ 单位 │ 简短消息                  │
└────────────────────────────────────────────────────────────────────────────┘
```

| 区域 | 默认 Dock | 默认行为 |
| --- | --- | --- |
| 左侧 | 建模、需求、运动学、轨迹、动力学、选型、优化 | 七个阶段 Dock 标签化；阶段按钮抬升对应 Dock |
| 右侧 | 对象属性、点动、工程设置、场景显示 | 对象属性跟随选择；其余面板按需标签化或浮动 |
| 底部 | 阶段结果、曲线、任务、诊断、计算依据、日志 | 阶段切换时抬升主要结果，但不隐藏运行任务和错误 |

布局基准：

| Windows 缩放 | 左侧默认宽度 | 右侧默认宽度 | 底部默认高度 | 布局调整 |
| --- | ---: | ---: | ---: | --- |
| 100% | 440 px | 320 px | 280 px | 左右 Dock 同时显示 |
| 125% | 380 px | 300 px | 280 px | 可折叠次要字段标签 |
| 150% | 380 px | 与左侧标签化 | 240 px | 优先保证中央视图和当前阶段 Dock |

中央三维视图逻辑尺寸不得小于 640×480，Dock 最小宽度为 320 px。界面使用 Qt 高 DPI 机制，不对字体、图标或坐标另做倍率换算。浮动 Dock 若位于当前屏幕之外，启动时移回主屏幕可见区域。工具栏阶段按钮由阶段编号、短名称、状态短标签（§8.4）、需重算标记和问题数量组成，需重算标记和问题数量随对应阶段结果实时更新。

### 8.3 公共编辑与选择交互

统一编辑流程：

```text
编辑草稿 → 行内校验 → 应用更改 → 形成一个新方案版本 → 相关结果需重算
```

- 一次“应用更改”只形成一个方案版本；非法字段保持编辑态并定位首个问题。
- 预览、点动、候选查看和轨迹播放只改变当前场景，不修改方案。
- 表格选中对象时，三维视图高亮同一对象，对象属性显示其详情。
- 三维拾取对象时，当前阶段表格滚动并选中对应行；对象不属于当前表格时，只更新对象属性。
- 诊断“定位”同时抬升对应阶段 Dock、选中表格行并高亮三维对象。
- 同一选择变化不得自动切换工程阶段，避免研发过程被意外打断。

### 8.4 公共结果状态

界面短标签与模型状态的映射如下；业务模块不得另造近义状态：

| 界面状态 | 含义 | 主要操作 |
| --- | --- | --- |
| 输入未完成 | 必要输入缺失或引用未完成 | 定位缺项 |
| 可计算 | 输入完整且校验通过 | 运行 |
| 计算中 | 当前阶段任务正在执行 | 查看任务、取消 |
| 结果有效 | 结果与当前方案及工程设置一致 | 查看、比较、报告 |
| 需重算 | 方案或工程设置已变化 | 重新运行 |
| 依据不足 | 快速检查、覆盖不足或输入可信度不足 | 正式计算、补充输入 |
| 计算失败 | 任务未完成或系统错误 | 重试、查看详情 |
| 不可行 | 计算完成但存在明确违反项 | 查看约束、返回修改 |

计算方式在界面显示为“快速检查”和“正式计算”。快速检查不得显示为正式通过；历史结果可查看，但必须与当前有效结果清楚区分。

### 8.5 公共按钮状态

| 按钮 | 启用条件 | 禁用或执行中状态 |
| --- | --- | --- |
| 撤销 | 存在可撤销编辑 | 无历史时禁用；计算中撤销会把相关结果标为需重算 |
| 重做 | 存在可重做编辑 | 无历史时禁用 |
| 应用更改 | 存在合法且尚未应用的草稿 | 无修改、校验失败或互斥任务提交中时禁用 |
| 运行 | 阶段为可计算且无互斥任务 | 执行中禁用并启用“取消” |
| 取消 | 存在可取消任务 | 请求发出后显示“正在取消”，防止重复点击 |
| 重试 | 上次任务失败且输入仍有效 | 输入已变化时改为“重新运行” |
| 更多 | 当前上下文存在低频操作 | 运行中只保留安全的只读操作 |

按钮使用短标签，主要操作保持在首屏；低频项放入“高级设置”或“更多”。不得用长段说明替代禁用原因，悬停提示只给出原因和下一步。

### 8.6 右侧公共面板

#### 对象属性

固定摘要字段：名称、类型、所属阶段、状态。其余字段随对象类型切换；方案字段可编辑，计算结果只读。无选择时显示“在表格或三维视图中选择对象”。

#### 点动

| 分组 | 字段或操作 |
| --- | --- |
| 模式 | 关节、笛卡尔 |
| 参考 | 基座、工具、当前参考系 |
| 步长 | 位置步长、角度步长、速度比例 |
| 操作 | 正/负方向、停止、回到命名姿态 |

点动只更新场景预览。需要保存姿态时，必须在对应业务页执行“记录姿态”。项目无有效模型、任务计算占用运动控制或选中对象不可点动时，方向按钮禁用并显示简短原因。

#### 工程设置

标签页：通用、碰撞、运动学、轨迹、动力学、选型、优化。每项显示名称、当前值、单位、适用范围和“恢复默认”；高级项默认折叠。应用设置后，只把受影响阶段标为需重算。

#### 场景显示

显示项：机器人、工具、环境、坐标系、轨迹、碰撞体、包络、标注；另有透明度和选中高亮。显示变化立即生效，不产生方案版本，也不触发重算。

### 8.7 底部公共面板

任务表：

| 任务 | 阶段 | 方式 | 状态 | 进度 | 已用时间 | 开始时间 | 操作 |
| --- | --- | --- | --- | ---: | ---: | --- | --- |

操作按任务状态显示“查看”“取消”或“重试”；没有任务时显示“当前没有运行任务”。

诊断表：

| 级别 | 阶段 | 对象 | 问题 | 实际值 | 要求值 | 建议 | 操作 |
| --- | --- | --- | --- | --- | --- | --- | --- |

“定位”是唯一行操作。技术详情默认折叠；首屏只显示工程问题和处理建议。

计算依据显示：方案版本、计算方式、模型摘要、需求摘要、主要设置、开始时间、完成时间、结论。不得把内部标识、文件摘要或运行编号作为用户字段。

状态栏字段：项目、方案版本、结果状态、当前任务、单位和简短消息。正常状态不滚动输出日志；完整日志保留在 RobWorkStudio 日志 Dock。

### 8.8 公共空态与错误态

空态只说明当前缺少什么、下一步是什么，并只提供一个主要按钮：

| 场景 | 提示 | 主要按钮 |
| --- | --- | --- |
| 当前表格无数据 | 尚未添加内容 | 新增 |
| 当前筛选无结果 | 没有符合条件的结果 | 清除筛选 |
| 当前阶段输入不足 | 请先完成标出的必要项 | 定位缺项 |
| 当前阶段未计算 | 输入已就绪，可开始计算 | 运行 |
| 当前阶段结果过期 | 方案已变化，需要重新计算 | 重新运行 |

字段问题显示在字段下方；跨对象问题进入诊断表；系统错误使用短横幅“操作未完成”，提供“重试”和“详情”。错误发生后保留草稿、筛选和当前选择。计算失败时可保留已完成的中间结果，但必须标为不可用于正式结论。

### 8.9 文案约束

- 一级标签优先使用二至四字名称；按钮通常不超过六个汉字。
- 面向研发工程师使用“方案版本、计算依据、结果状态、快速检查、正式计算”等工程名称。
- 默认界面不显示哈希、Schema、缓存、内部对象标识、运行编号、内部插件名或冻结类操作。
- 不常用解释放入帮助或详情，不在面板中长期占用宽度。
