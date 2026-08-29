# 会话、场景与公共 UI 模块详细方案

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`
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
