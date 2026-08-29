# WP-21-T03 调度缓存检查点

- **Task ID / 需求 ID / ADR / 阶段：** WP-21-T03；OPT-06（缓存、并行、检查点与确定性种子）、TASK-01（状态机与能力声明）、CON-04（预算耗尽锚点）、AT-10～11、NFR-REL-02、NFR-PERF-04～06；ADR-004（不自建第二套调度/缓存）。阶段 D / R2。契约：`architecture/execution-model.md` §3（缓存键冻结）、§4（取消/检查点/恢复冻结）；模块详设 `module-design/optimization.md` v0.3 §4（`IRD-OPT-BUDGET-EXHAUSTED`）、§5.6。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-21-T01（编排批次结构可用）；WP-08-T03（取消/检查点端口）、WP-08-T04（缓存与检查点契约，`IRD-EXEC-CHECKPOINT-INCOMPATIBLE` 生效）、WP-08-T05（有界并行）；工件：`sdurws_ird_execution_test`（WP-08 侧）通过、T01 用例通过、WP-02 optimization 检查点样本。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/`）修改 `joint/src/JointSearchOrchestrator.cpp` 与 `joint/include/sdurws/ird/opt/JointSearchOrchestrator.hpp`（批次边界检查点、恢复去重、缓存键组装、预算计数挂点）；创建 `test/SchedulerCheckpointTest.cpp`、`testdata/optimization/` 本任务夹具；写 `out/test-evidence/wp-21/<run-id>/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-08 execution 源文件与公共头（只调用其调度/缓存/检查点端口）；WP-20 definition/candidate；WP-16～19 评估器；`requirements.md`、CSV、`schemas/`；不新增 CMake 目标；不得在 `joint/` 内实现第二套调度器、缓存或检查点存储。
- **修改前接口：** 编排无批次边界检查点、无恢复路径、缓存键不含联合搜索维度；`SchedulerCheckpointTest.cpp` 不存在。
- **修改后接口：** 编排按批次边界写检查点（经 WP-08 端口）；恢复＝沿用原 `runId`＋新 `attemptId`＋已完成批次集合去重（execution-model §4），统计不重复；缓存键覆盖 `studyDefinitionVersion`、`algorithmVersion`、`seed`、`threadCount`（execution-model §3）；预算耗尽（`maxCandidates/maxWallClockS/maxVerifiedEvaluations`）→ `IRD-OPT-BUDGET-EXHAUSTED`（Engineering/Warning），终态落 `Completed + DataInsufficient + Complete` 锚点，可加预算后以新 attempt 续跑。
- **实施步骤：**
  1. 写 RED 测试（检查点写入与兼容校验、恢复去重、缓存键四维覆盖、预算耗尽终态组合、加预算续跑）。
  2. 在编排批次循环接入 WP-08 检查点端口（批次边界、保留最近兼容版本）。
  3. 实现恢复路径：校验检查点兼容性（不兼容→`IRD-EXEC-CHECKPOINT-INCOMPATIBLE` 不用于恢复），去重已完成批次，沿用 `runId` 换新 `attemptId`。
  4. 组装缓存键四维（`Partial/Failed/Canceled` 与不兼容版本不得命中正式缓存，复用 WP-08 规则）。
  5. 实现三个预算计数与耗尽处置（终止＋DataInsufficient 锚点＋Warning）。
  6. 执行验证命令，写证据（恢复统计表）。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"` 无 SchedulerCheckpoint 用例；落地后含检查点/恢复/缓存键/预算四组用例全部通过。
- **最小实现：** 批次检查点＋恢复去重＋缓存键四维＋预算耗尽处置；不做误淘汰审计（T04）、不做应用（T05）、不新建存储。
- **正常/边界/失败测试：**
  - 正常：Given 固定种子运行中断于批次 k，When 从兼容检查点恢复（新 `attemptId`），Then 已完成批次不重复执行、恢复后统计与未中断口径一致、`runId` 不变。
  - 边界：Given `maxVerifiedEvaluations` 恰在批次边界耗尽，When 继续提交，Then 终止并落 `Completed + DataInsufficient + Complete`；加预算后新 attempt 从下一批次续跑且缓存命中此前结果。
  - 失败：Given 检查点版本不兼容（`algorithmVersion`/schema 变更），When 请求恢复，Then `IRD-EXEC-CHECKPOINT-INCOMPATIBLE`、不用于恢复且不破坏既有项目状态；Given `threadCount` 或 `seed` 变化，When 查询缓存，Then 键不匹配不命中。
- **精确验证命令：**（仓库根、VS x64 环境）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_joint_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；`joint/` 内无新建调度线程池/缓存容器/检查点文件格式（一律 WP-08 端口）；无统计重复计数路径。
- **证据工件：** `out/test-evidence/wp-21/<run-id>/t03-scheduler-checkpoint.log`：中断-恢复统计对照表（批次去重前后）、缓存键矩阵、预算耗尽终态记录、命令原文与 commit。
- **提交格式：** `WP-21-T03: 调度缓存检查点`
- **停止与升级条件：** WP-08 检查点/缓存端口无法承载联合批次语义（如端口缺批次集合记录）时，停止并升级 WP-08 所有者与工作包所有者；实现者不得担任本卡独立验证者。
