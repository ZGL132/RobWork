# WP-10-T02 只读场景投影与预览恢复

- **Task ID / 需求 ID / ADR / 阶段：**WP-10-T02；UX-01～UX-08、KIN-06、AT-04、AT-05；ADR-001（单机械臂作用域）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/session-ui.md` v0.3、`architecture/public-interfaces.md` §6
- **前置任务及必需工件：**WP-10-T01（`SessionState`/`StageStatusModel` 工件合入）；WP-06-T02（`IRuntimeNameResolver`/`RuntimeNameMap` 公共头——代码前置）；WP-04-T02（`IProjectQuery` 公共头）；WP-05-T02（`AnalysisSnapshot` 公共头）；WP-01-T03（测试入口）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/ISceneProjection.hpp`、`SceneProjection.hpp`、`SelectionModel.hpp`；`ui/src/SceneProjection.cpp`、`SelectionModel.cpp`；`ui/test/SceneProjectionTest.cpp`；`ui/testdata/scene/`；`ui/out/test-evidence/wp-10/<run-id>/`；`ui/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`ISceneProjection` 签名以 public-interfaces §6 冻结为准不得偏离；WP-06 名称解析实现、WP-04/05 公共接口、RobWork 编译工件（WorkCell/DWC 由 WP-06 产出）；`architecture/`、`module-design/`、其他模块目录；禁止按显示名称查找对象、跨线程操作 QWidget
- **修改前接口：**无（投影接口与选择模型不存在；旧插件直接改场景节点）
- **修改后接口：**`ISceneProjection::projectCurrent() const -> expected<SceneSnapshot, ProjectError>`、`projectCandidate(const ResultRef&) const -> expected<SceneSnapshot, ProjectError>`（会话态投影，不回写设计基线）；`SelectionModel` 以 `objectId` 为唯一键，`subscribe` 通知列表/三维/详情三视图
- **实施步骤：**1) 按 §6 冻结签名落地 `ISceneProjection`；2) 投影源限 `ProjectRevision`/`AnalysisSnapshot`/`DesignCandidate`，节点以 `objectId` 绑定、显示名称只取自 `IRuntimeNameResolver`；3) 进入候选/历史预览前保存当前姿态到 `SessionState.previewRef`，退出/取消/失败恢复；4) `SelectionModel` 三视图联动；5) 补测试与证据
- **RED 测试：**Given 投影源包含未知 `objectId` 或名称解析失败（`IRD-NAME-UNRESOLVED`），When `projectCurrent()`，Then 返回 `IRD-UI-PROJECTION-FAILED`（System/Error），当前场景与 revision 不变（`SceneProjectionTest` 先行）
- **最小实现：**只读快照构建＋objectId→节点绑定＋失败透传；预览恢复仅保存/恢复姿态两分支；不实现渲染优化与后台加载
- **正常/边界/失败测试：**
  - 正常：Given current revision、candidate 或历史 snapshot，When project，Then 节点只读、objectId 绑定、显示名经 resolver
  - 边界：Given 进入候选预览后投影失败，When 退出/取消，Then 恢复原姿态、选择与当前方案，`SessionState` 之外零写入；投影路径不修改任何领域对象字段
  - 失败：Given 解析器返回 `IRD-NAME-AMBIGUOUS`，When 投影，Then 整体失败（不取第一个匹配）、旧场景保持、可重试动作记录
- **精确验证命令**（仓库根，模型测试仅 `QCoreApplication`）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_model_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_ui_model_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_ui_model_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "scopedName\s*\+|\+\s*\"\.\"" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/SceneProjection.cpp; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中（禁止名称拼接）；`rg -n "set.*Pose|write" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/SceneProjection.cpp` 命中处仅限 `SessionState.previewRef` 保存
- **证据工件：**`ui/out/test-evidence/wp-10/<run-id>/`——投影节点表（objectId↔显示名来源）、恢复前后姿态日志、三视图联动事件序列、失败诊断样本
- **提交格式：**`WP-10-T02: 新增只读场景投影与预览恢复`

  - 新增 ISceneProjection 只读投影与 SelectionModel 三视图联动实现
  - 新增 投影失败/预览恢复测试及目标登记
  - 新增 投影节点表与恢复姿态日志证据记录
- **停止与升级条件：**场景 API 要求回写领域对象、名称无法经 `IRuntimeNameResolver` 反解、或 WP-06 公共头未合入时暂停；接口签名需偏离 public-interfaces §6 时升级架构评审，不得私改
