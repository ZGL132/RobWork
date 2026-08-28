# WP-10 会话、场景与公共界面实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 建立统一会话态、编辑草稿、已应用设计、三维场景投影、阶段导航和公共 Qt 组件，保证 Jog/预览/显示操作不污染设计基线。

**Architecture:** `SessionState` 仅驻留当前会话，`EditDraft` 可保存但不参与计算，设计修改只通过项目命令进入 `ProjectRevision`。`ISceneProjection` 将当前方案或候选投影到 RobWorkStudio，不反写领域对象。

**Tech Stack:** C++、Qt Widgets/Model-View、RobWorkStudio、Qt Test/CTest。

---

## 文件与目标

**创建目标：** `sdurws_ird_ui`、`sdurws_ird_ui_model_test`、`sdurws_ird_ui_widget_test`。

**创建：**

- `industrialrobot/ui/include/.../SessionState.hpp`
- `industrialrobot/ui/include/.../EditDraft.hpp`
- `industrialrobot/ui/include/.../ISceneProjection.hpp`
- `industrialrobot/ui/include/.../StageStatusModel.hpp`
- `industrialrobot/ui/include/.../EngineeringPolicyPanel.hpp`
- `industrialrobot/ui/include/.../DiagnosticPanel.hpp`
- `industrialrobot/ui/include/.../EngineeringTableView.hpp`
- `industrialrobot/ui/src/`
- `industrialrobot/ui/test/`

**覆盖需求：** UX-01～08，KIN-06，NFR-PERF-01、03，AT-04、05、12、19。

## 状态契约

```text
SessionState: 视角、隐藏、着色、筛选、Jog、播放、候选预览
EditDraft:    尚未应用的表单/批量修改，可保存但不参与计算
ProjectRevision: 已应用设计，计算快照的唯一来源
```

用户可见阶段状态固定为：输入未完成、可计算、计算中、结果有效、需要重算、证据不足、计算失败、工程不可行。任务面板单独显示任务生命周期。

## 任务

### Task 1：纯状态模型

- [ ] 先写 Jog、IK 双击、轨迹播放、候选预览、显示几何和筛选不创建修订的测试。
- [ ] 实现 SessionState、EditDraft 和 StageStatusModel，不依赖 QWidget。
- [ ] 设计修改只有点击表单级“应用”才调用 IProjectCommandService，并只产生一个修订。

### Task 2：场景投影

- [ ] 实现当前方案、候选和历史快照的只读场景投影。
- [ ] 预览前保存会话姿态，退出后恢复当前方案，不写项目。
- [ ] 列表选择、三维对象和详情面板使用 objectId 联动，不使用显示名称查找。

### Task 3：公共工程组件

- [ ] 实现阶段导航、输入问题列表、下一步建议、统一状态图例和诊断详情。
- [ ] 工程表格支持批量粘贴、筛选、单位显示、逐行错误和分页/虚拟化。
- [ ] 高级设置折叠显示求解器、种子和开发诊断，不占据默认主流程。

### Task 4：统一策略入口

- [ ] 提供唯一“工程策略”页面；插件只显示当前策略摘要和跳转入口。
- [ ] 碰撞计算模式与“显示碰撞几何/高亮”分组、异名并说明影响。
- [ ] 显示开关不得触发项目命令、快照、缓存失效或重新计算。

### Task 5：响应与大列表

- [ ] 5,000 任务、100,000 摘要和 10,000 候选使用分页/虚拟模型，不装载全部明细。
- [ ] 导航、选择、筛选、编辑和切换 P95 不超过 200 ms；超过 1 秒工作转后台。
- [ ] GUI 测试验证主线程无超过 2 秒无响应窗口。

## GUI 验证命令

在 Visual Studio x64 开发环境中执行，一次只启动一个 GUI 测试：

```powershell
$env:QT_QPA_PLATFORM='windows'
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_model_test$'
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -GuiExecutable 'sdurws_ird_ui_widget_test.exe'
```

## 退出条件

- A-GATE-01、02、07 和 AT-04、05、12、19 的 UI 侧断言通过。
- 会话、草稿和设计状态没有隐式互转。
- 公共 UI 不直接访问业务插件私有对象，业务逻辑不进入 Widget。
