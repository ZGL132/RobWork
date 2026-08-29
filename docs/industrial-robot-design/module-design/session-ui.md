# 会话、场景与公共 UI 模块详细方案

- 方案版本：v0.2；需求基线：v0.7；负责 WP：WP-10；阶段/发布：阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`

## 1. 模块职责与目录

模块拥有无 QWidget 的会话状态模型、编辑草稿、阶段状态、只读场景投影和 Qt 公共组件。Widget 只能发出用户意图；业务命令、评估和策略通过公共接口执行。模块不直接写项目 revision 或修改领域对象。

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/
  include/sdurws/ird/ui/
    SessionState.hpp EditDraft.hpp StageStatusModel.hpp
    ISceneProjection.hpp SceneProjection.hpp SelectionModel.hpp
    EngineeringPolicyPanel.hpp DiagnosticPanel.hpp
    EngineeringTableView.hpp VirtualResultModel.hpp
  src/SessionState.cpp DraftController.cpp StageStatusModel.cpp
      SceneProjection.cpp SelectionModel.cpp PolicyPanel.cpp
      DiagnosticPanel.cpp EngineeringTableView.cpp VirtualResultModel.cpp
  test/SessionStateTest.cpp SceneProjectionTest.cpp CommonComponentsTest.cpp
      PolicyUiTest.cpp ResponsiveListsTest.cpp
```

目标：`sdurws_ird_ui`、`sdurws_ird_ui_model_test`、`sdurws_ird_ui_widget_test`。

## 2. 状态字段和转移

`SessionState` 字段：`sessionId`、`projectRef`、`selectedObjectId`、`cameraPose`、`visibility`、`colorMode`、`filter`、`jogPose`、`playbackState`、`previewRef`。它可变但只存在会话内，更新不产生 revision。

`EditDraft` 字段：`draftId`、`baseRevisionRef`、`patches[]`、`validationDiagnostics[]`、`dirty`、`savedAt`。保存到 WP-04 drafts，不进入 EvaluatorInputSlice；应用前必须重新比较 base revision，冲突交给命令服务。

`StageStatusModel` 取值：输入未完成、可计算、计算中、结果有效、需要重算、证据不足、计算失败、工程不可行。任务生命周期单独显示，不能用阶段状态替代。

## 3. 数据流和场景投影

```text
ProjectRevision/AnalysisSnapshot/DesignCandidate
  -> SceneProjection(objectId bindings, immutable source)
  -> RobWorkStudio scene nodes
  -> selection/Jog/visibility/preview -> SessionState
  -> preview exit/cancel -> restore saved pose and current revision
```

投影器只读 source，节点通过 objectId 绑定，名称来自 WP-06 resolver；列表、三维场景、详情面板共享 `SelectionModel`。候选/历史预览保存当前姿态和 selection，失败/退出时恢复，不反写领域数据。

## 4. 公共组件

阶段导航显示每阶段状态、阻塞诊断和下一步建议；诊断面板按 code、对象、实际/期望、action 展示；工程表格支持粘贴批量值、SI 单位显示、逐行错误、分页和虚拟模型；策略面板只展示 WP-07 provider 的快照摘要。高级设置折叠显示 seed、求解器和开发诊断，默认流程不依赖它。

## 5. 线程和性能

所有 QWidget 和 model mutation 在 GUI 主线程；评估、文件读取和大列表分页在后台，通过 queued signal 返回不可变值。5,000 任务、100,000 摘要、10,000 候选不得一次性创建明细对象。导航/选择/筛选/编辑 P95 ≤ 200 ms；后台工作超过 1 秒；主线程连续阻塞不得超过 2 秒。

## 6. 策略与显示隔离

“计算模式”属于 WP-07 `EngineeringPolicySet`，修改需创建 EditDraft 并由用户应用；“显示碰撞几何/高亮”属于 SessionState，立即生效但不得触发命令、快照、缓存失效或重算。插件不得提供同名私有开关。

## 7. 测试与证据

模型测试覆盖状态隔离、草稿应用、revision 冲突、预览恢复、objectId 联动和阶段状态；Widget 测试覆盖导航、表格粘贴、诊断、策略显示和高亮隔离。性能测试记录规模、P95、主线程阻塞窗口和后台转移；GUI 在 Windows Visual Studio x64、`QT_QPA_PLATFORM=windows` 下单进程执行。证据含截图/录屏、操作脚本、日志、输入 revision 和评审者。

## 8. 迁移与评审

旧插件 UI 通过 adapter 接入；无法证明会话不写项目或线程安全时标 Rewrite/EvidenceOnly。评审确认业务逻辑不进入 Widget、selection 使用 objectId、预览可恢复、策略入口唯一、显示状态不污染快照。
