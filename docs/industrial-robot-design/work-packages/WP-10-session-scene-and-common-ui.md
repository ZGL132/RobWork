# WP-10 会话、场景与公共界面实施计划

> 阶段/发布：阶段 A / R1；会话与公共 UI 接口所有者：WP-10。实现者、验证者和评审者必须是不同执行上下文。

**目标：** 建立会话态、编辑草稿、已应用设计、三维场景投影、阶段导航和公共 Qt 组件，保证 Jog、预览、显示、筛选等交互不污染项目修订、输入快照或缓存。

## 1. 目标与非目标

交付无 QWidget 的状态模型、只读场景投影、公共工程组件、唯一策略入口和大列表虚拟化。业务逻辑通过 WP-04/05/07/08 公共接口调用；Widget 只绑定模型和发出用户意图。不实现业务计算、项目持久化、碰撞算法、评估调度、报告生成或各插件私有 UI 状态。

## 2. 需求、契约和发布切片

- 需求：UX-01～UX-08、KIN-06、NFR-PERF-01、NFR-PERF-03、AT-04、AT-05、AT-12、AT-19。
- 架构契约：`architecture/execution-model.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`。
- 模块方案：`module-design/session-ui.md`。
- 阶段/发布：阶段 A / R1；阶段 B 仅呈现建模、需求、运动学和静态优化链路状态。

## 3. 文件所有权与依赖

拥有目录：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/`，含 `include/sdurws/ird/ui/`、`src/`、`test/`、`testdata/`、`out/test-evidence/wp-xx/<run-id>/`（AGENTS §3）。允许 WP-03～08 公共接口、Qt Widgets/Model-View 和 RobWorkStudio 场景 API；禁止 Widget 直接读取业务插件私有对象、跨线程操作 QWidget、直接写项目文件或手工 CSV。

目标：`sdurws_ird_ui`、`sdurws_ird_ui_model_test`、`sdurws_ird_ui_widget_test`。

## 4. 三层状态模型

`SessionState` 仅保存视角、隐藏/着色、筛选、Jog、播放、选择和候选预览；`EditDraft` 保存尚未应用的表单/批量 patch，可持久化但不参与计算；`ProjectRevision` 是已应用设计和快照唯一来源。只有用户点击表单级“应用”才调用 `IProjectCommandService`，一次应用最多生成一个 revision；会话显示变化和草稿保存不创建 revision。

阶段状态固定为：输入未完成、可计算、计算中、结果有效、需要重算、证据不足、计算失败、工程不可行；任务生命周期在独立任务面板显示，不混用阶段状态。

## 5. 场景投影与交互数据流

```text
ProjectRevision/AnalysisSnapshot/DesignCandidate
  -> ISceneProjection (read-only objectId bindings)
  -> RobWorkStudio scene nodes
  -> user selection/Jog/preview (SessionState only)
  -> restore saved pose on exit
```

场景节点以 objectId 绑定，显示名称只来自 `IRuntimeNameResolver`；列表、三维对象和详情面板共享 objectId 选择模型。候选/历史快照投影前保存当前姿态，退出或取消预览恢复当前方案，不反写领域对象。

## 6. 公共组件与响应约束

组件包括阶段导航、输入问题列表、下一步建议、统一状态图例、诊断详情、工程表格和策略摘要。表格支持批量粘贴、单位显示、逐行诊断、分页和虚拟化；高级设置折叠显示 seed、求解器和开发诊断。策略页面区分“计算模式”和“显示碰撞几何/高亮”，后者只改变 SessionState。

5,000 任务、100,000 摘要、10,000 候选必须分页/虚拟加载；导航、选择、筛选、编辑和切换 P95 ≤ 200 ms，超过 1 秒的工作后台执行。主线程不得连续阻塞超过 2 秒。

## 任务

| 任务 | 独立产出 | 任务卡 |
| --- | --- | --- |
| WP-10-T01 | 状态模型与应用单修订规则 | [T01](../agent-tasks/WP-10-T01-session-state.md) |
| WP-10-T02 | 当前/候选/历史场景投影 | [T02](../agent-tasks/WP-10-T02-scene-projection.md) |
| WP-10-T03 | 导航、诊断、表格等公共组件 | [T03](../agent-tasks/WP-10-T03-common-components.md) |
| WP-10-T04 | 唯一策略入口和显示隔离 | [T04](../agent-tasks/WP-10-T04-policy-ui.md) |
| WP-10-T05 | 虚拟列表、响应和 GUI 证据 | [T05](../agent-tasks/WP-10-T05-responsive-lists.md) |

依赖：T01 → T02/T03；T04 依赖 WP-07；T05 依赖 T02/T03。每张卡一个 worktree、分支和提交。

## 7. 失败分类与证据

- 输入错误：表单缺失、单位非法、对象不存在；显示逐字段诊断，不调用命令服务。
- 工程不可行：阶段输入不足、结果过期或证据缺口；导航显示状态和下一步建议，不伪造可计算。
- 系统错误：场景投影、Qt 模型、后台任务或资源故障；保持当前场景和 revision 不变，记录可重试动作。

## 验证

模型测试命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\\RobWork\\scripts\\industrial-robot\\build.ps1 -Configuration Debug -Target sdurws_ird_ui_model_test`，随后 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\\RobWork\\scripts\\industrial-robot\\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_model_test$'`。GUI 测试使用同一入口执行 `-Regex '^sdurws_ird_ui_widget_test$'`，必须在 Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，并一次只启动 `sdurws_ird_ui_widget_test.exe`。

## 8. 迁移与评审

旧插件 UI 先通过 adapter 消费公共模型，未证明会话隔离和线程安全时标 Rewrite/EvidenceOnly。评审确认业务逻辑不进入 Widget、objectId 联动、策略入口唯一、显示开关不改 revision/slice/cache、列表规模和主线程时延证据齐全。

## 退出条件

A-GATE-01/02/07 与 AT-04/05/12/19 UI 断言通过；SessionState、EditDraft、ProjectRevision 没有隐式互转；候选/历史预览可恢复；公共 UI 不访问业务私有对象；P95 和 2 秒无响应门禁通过；5 张任务卡证据和独立评审齐全。
