# WP-16-T06 轨迹任务生命周期

- **Task ID / 需求 ID / ADR / 阶段：** WP-16-T06；TRJ-01～06 的执行侧（取消/恢复/迟到回调隔离与确定性重复运行，按 `architecture/execution-model.md` §1～3）；无直接关联 ADR；阶段 C / R1。契约：`module-design/trajectory-planning.md` v0.3 §4（安全点＝段边界）、`architecture/execution-model.md` §1～§5、`architecture/evaluation-semantics.md` §2、`architecture/public-interfaces.md` §3。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（trajectory-planning.md v0.3、需求 v0.8）
- **前置任务及必需工件：** WP-16-T05（`TrajectoryEvaluator` 完整装配与 `TrajectoryPlan` 输出可用）；WP-08-T01/T02（`IEvaluationScheduler`/`EvaluationRequest`/`RunIdentity` 五元组）；WP-08-T03（`CancellationToken` 协作取消）；WP-08-T04（缓存与检查点契约）；WP-05-T04（结果接纳）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/`）
  - 创建：`test/LifecycleTest.cpp`、`testdata/trajectory/failpoints/`（取消/恢复/迟到回调注入点，与 WP-16-T04 共用目录，登记 WP-02 manifest）、`out/test-evidence/wp-16/<run-id>/`（本卡工件）
  - 修改：`src/TrajectoryEvaluator.cpp`（仅取消/恢复/迟到回调路径）、`CMakeLists.txt`（测试源编入 `sdurws_ird_trajectory_test`）；不删除文件。
- **禁止修改的文件和公共接口：** WP-08 状态机/身份/取消/缓存/检查点内部实现与 `IEvaluationScheduler` 冻结签名（只接入）；WP-05 接纳语义；WP-16-T01～T05 已交付接口与 `TrajectoryPlan` Schema；一切非本拥有目录源码；自建线程池/调度；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（生命周期路径新增；T05 后 `TrajectoryEvaluator.cpp` 已有评估主流程，本卡只增补协作取消与恢复分支，不改其对外签名）。
- **修改后接口：** WP-08 调度接入完成：请求携带完整 `RunIdentity`＝{projectId, branchId, revisionId, runId, attemptId}；`CancellationToken` 协作取消，安全点＝段边界；取消/恢复按 WP-08 契约（检查点恢复不重复已接纳结果）；迟到回调只追加原分支历史、不改写终态（execution-model §1）；`TaskCapabilities` 声明（能力与预算进调度）；确定性重复运行：固定输入切片、规划器版本、种子与线程数时路点集合、段序、节点时刻与诊断顺序逐字节一致。
- **实施步骤：**
  1. 先写 `LifecycleTest.cpp` 全部 RED 断言，构建确认失败。
  2. 在 `TrajectoryEvaluator.cpp` 增补取消分支：消费 `CancellationToken`，在段边界响应并落检查点。
  3. 实现恢复路径：从段边界检查点续算，不重复已接纳结果。
  4. 实现迟到回调隔离：完成事件校验 `RunIdentity`，不匹配/旧会话只追加原分支历史。
  5. 声明 `TaskCapabilities`；构造固定切片/版本/种子/线程数的确定性重复运行对照。
  6. 生成 failpoints 夹具并登记 WP-02 manifest，按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `CancelRespondsAtSegmentBoundary`；`ResumeFromCheckpointContinuesWithoutDuplication`；`LateCallbackAppendsOriginalBranchOnly`；`MismatchedIdentityNeverBecomesCurrentResult`；`DeterministicRerunByteStable`（固定切片/版本/种子/线程数）；`TaskCapabilitiesDeclaredToScheduler`。
- **最小实现：** 仅生命周期路径与确定性证明转绿所需；WP-08 平台机制（状态机/缓存/检查点实现）零重复；播放/jog 会话态归 WP-10（只经 T05 只读求值接口）。
- **正常/边界/失败测试：**
  - 正常：Given 长轨迹评估与取消请求，When 到达段边界，Then 协作取消、检查点落盘、终态 `Canceled + NotEvaluated + Partial/None` 不进入正式用途。
  - 边界：Given 恢复时切片未变与切片已变两种情形，When 续算，Then 前者从段边界续算且结果与一次运行逐字节一致，后者按 currentness 显示"需要重算"。
  - 失败：Given 身份不匹配或旧会话的迟到回调，When 结果到达，Then 只追加原分支历史、不改写当前结果与终态。
- **精确验证命令：**（仓库根目录、VS x64 环境；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_trajectory_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；`src/TrajectoryEvaluator.cpp` 改动仅限取消/恢复/迟到回调路径；无自建调度/线程池/检查点实现；无直写项目 revision；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-16/<run-id>/lifecycle-report.md`（取消/恢复时序记录、迟到回调隔离日志、确定性重复运行逐字节对照、`RunIdentity` 校验矩阵）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者以 failpoints 注入复核取消与迟到事件。
- **提交格式：** `WP-16-T06: 轨迹任务生命周期`
- **停止与升级条件：** 取消/恢复/迟到回调语义无法映射到 WP-08 现有契约（状态转移未被覆盖）时停止并升级 WP-08 负责人；实现者不得担任本卡独立验证者。
