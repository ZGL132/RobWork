# WP-10-T01 会话状态、草稿与阶段模型

- **Task ID / 需求 ID / ADR / 阶段：**WP-10-T01；UX-01～UX-08、KIN-06、NFR-PERF-03（需求 §5.4/§5.5）；ADR-005（正交状态）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（`industrialrobot/ui/` 尚不存在）；语义源 `module-design/session-ui.md` v0.3
- **前置任务及必需工件：**WP-01-T03（`run-tests.ps1` 统一测试入口）；WP-03-T02（`ObjectId`/`ObjectIdentity` 公共头）；WP-04-T02（`IProjectCommandService`/`DomainCommand` 公共头）；WP-04-T04（drafts 持久化端口）；WP-05-T03（`ResultCurrentness`/`EngineeringStatus` 公共头）；WP-09-T01（`Diagnostic` 公共头）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/SessionState.hpp`、`EditDraft.hpp`、`StageStatusModel.hpp`；`ui/src/SessionState.cpp`、`DraftController.cpp`、`StageStatusModel.cpp`；`ui/test/SessionStateTest.cpp`；`ui/testdata/session/`；`ui/evidence/WP-10/T01/`；`ui/CMakeLists.txt`（仅登记 `sdurws_ird_ui`、`sdurws_ird_ui_model_test`）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`IProjectCommandService`/`IProjectQuery`（public-interfaces §1）、`ResultEnvelope`/`ResultCurrentness`（§5/§7 值对象表）、WP-03/09 公共头；`architecture/`、`module-design/`、`schemas/`、其他模块目录；模型层头不得包含 `<QWidget>` 或 Qt Widgets
- **修改前接口：**无（目录与 CMake 目标不存在；旧插件 Widget 内嵌会话字段）
- **修改后接口：**值类型 `SessionState{sessionId,projectRef,selectedObjectId,cameraPose,visibility,colorMode,filter,jogPose,playbackState,previewRef}`；`EditDraft{draftId,baseRevisionRef,patches[],validationDiagnostics[],dirty,savedAt}`；`StageStatusModel` 八值枚举（输入未完成/可计算/计算中/结果有效/需要重算/证据不足/计算失败/工程不可行）＋`ResultCurrentness` 映射；`DraftController::applyDraft(base,EditDraft,DomainCommand)->expected<CommandResult,ProjectError>`（内部经 WP-04 端口，UI 不直写）
- **实施步骤：**1) 按 session-ui.md §3 冻结三类值对象字段；2) 实现 `StageStatusModel` 八值↔`ResultCurrentness/EngineeringStatus` 映射（需要重算=`Superseded`、历史证据=`Historical`，`DataInsufficient/Partial/NotEvaluated` 显式展示并按 `gaps` 列举）；3) `DraftController` 保存草稿（WP-04 drafts）与应用（比较 base revision，冲突交命令服务）；4) 登记 CMake 目标与测试
- **RED 测试：**Given `EditDraft` 的 base revision 已过期，When `applyDraft`，Then 返回 `IRD-PROJ-STALE-REVISION` 透传诊断，草稿与项目修订均不变（`SessionStateTest` 先行，构建失败/断言失败为 RED）
- **最小实现：**三类值对象＋映射表＋`DraftController` 冲突比较分支，仅够 RED 转绿；不实现投影、组件或后台线程
- **正常/边界/失败测试：**
  - 正常：Given 合法草稿，When 用户点击应用，Then 恰好产生一个新修订（命令服务替身计数＝1），阶段状态刷新为"需要重算"
  - 边界：Given Jog/IK 双击/播放/筛选/着色/候选预览，When 更新 `SessionState`，Then revision/snapshot/cache 零变化；`SessionState` 不出现在 `AnalysisSnapshot` 序列化中
  - 失败：Given base revision 过期（`IRD-PROJ-STALE-REVISION`），When 应用，Then 显示冲突诊断、草稿保留、修订号不变、可恢复动作为"刷新后重新应用"
- **精确验证命令**（仓库根，模型测试仅 `QCoreApplication`）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_model_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_ui_model_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_ui_model_test$"
  ```
- **diff 和禁止项检查：**`git diff --name-only 94fb910e` 仅含允许清单路径；`grep -rn "QWidget\|QtWidgets" ui/include/sdurws/ird/ui/{SessionState,EditDraft,StageStatusModel}.hpp` 零命中；`grep -rn "AnalysisSnapshot" ui/include/sdurws/ird/ui/SessionState.hpp` 零命中（会话态不进快照）
- **证据工件：**`ui/evidence/WP-10/T01/`——状态转移表（GWT 三类断言）、revision 计数日志、草稿 JSON 样例、诊断样本与命令日志
- **提交格式：**`WP-10-T01: implement session and draft state model`
- **停止与升级条件：**会话态与项目态出现隐式互转、需要 Widget 才能测试、或 WP-04/05 公共头未合入时暂停并上报 WP-10 负责人；字段语义与 session-ui.md §3 冲突时升级 ADR，不得现场改契约
