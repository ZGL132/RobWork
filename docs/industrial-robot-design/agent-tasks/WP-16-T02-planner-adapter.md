# WP-16-T02 RobWork 规划器适配

- **Task ID / 需求 ID / ADR / 阶段：** WP-16-T02；TRJ-03（调用 RobWork 规划器完成避障路径搜索，规划器与参数可配置）＋AT-06；无直接关联 ADR；阶段 C / R1。契约：`module-design/trajectory-planning.md` v0.3 §2/§4/§5.4（避障约束只经 WP-07 投影）、`architecture/execution-model.md` §1～3、`architecture/public-interfaces.md` §3。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（trajectory-planning.md v0.3、需求 v0.7）
- **前置任务及必需工件：** WP-16-T01（`sdurws_ird_trajectory` 目标、段 Schema 与上游校验可用）；WP-07-T02（共享 `CollisionEvaluator`）；WP-07-T04（`CollisionPolicy` → RobWork 设置投影）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/`）
  - 创建：`src/PlannerAdapter.cpp`、`test/PlannerAdapterTest.cpp`、`testdata/trajectory/planner/`（版本/参数/种子/失败段夹具，登记 WP-02 manifest）、`evidence/WP-16/`（本卡工件）
  - 修改：`CMakeLists.txt`（新源文件编入 `sdurws_ird_trajectory` 与 `sdurws_ird_trajectory_test`）、`include/sdurws/ird/trajectory/TrajectoryDiagnostics.hpp`（新增 `IRD-TRJ-NO-PATH`、`IRD-TRJ-PLANNER-TIMEOUT`、`IRD-TRJ-PLANNER-FAILED` 常量）；不删除文件。
- **禁止修改的文件和公共接口：** RobWork pathplanning 稳定 API（只调用不改）；WP-07 `CollisionPolicy` 字段与投影实现（只消费投影结果，不复制启用状态、配对、安全距离或分辨率）；WP-08 resourceBudget 语义；WP-16-T01 已交付类型；一切非本拥有目录源码；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（模块内新增；基线旧链路为 RobWork 规划器既有调用链与参数，属 §13.2 迁移项，经本适配器只读对照，路径黄金数据一致后才切换）。
- **修改后接口：** 模块私有 `PlannerAdapter`：以 RobWork rwlibs/pathplanners 稳定 API 执行避障路径搜索；规划器版本、参数、种子与失败段进快照（入 `AnalysisSnapshot` 记录）；约束构造只经 WP-07 从 `CollisionPolicy` 的投影；诊断 `IRD-TRJ-NO-PATH`（Engineering/Error，附段与端点）、`IRD-TRJ-PLANNER-TIMEOUT`（预算来自 resourceBudget）、`IRD-TRJ-PLANNER-FAILED`（System/Error，保留旧结果按诊断重试）。
- **实施步骤：**
  1. 先写 `PlannerAdapterTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现 `PlannerAdapter` 对 RobWork 规划器的封装：版本/参数/种子采集并写入快照记录。
  3. 实现约束构造：只调用 WP-07 投影接口，无本地无碰撞参数副本。
  4. 实现失败处理：无碰路径不存在 → `IRD-TRJ-NO-PATH`（附段与端点）；超时（预算来自 resourceBudget）→ `IRD-TRJ-PLANNER-TIMEOUT`；系统故障 → `IRD-TRJ-PLANNER-FAILED`。
  5. 实现失败段定位（段序号与端点入诊断）。
  6. 生成 planner 夹具并登记 WP-02 manifest，按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `PlannerVersionParamsSeedRecordedInSnapshot`；`ConstraintConstructionOnlyViaWp07Projection`；`NoPathReportsSegmentAndEndpoints`；`TimeoutConsumesResourceBudget`；`PlannerFailureKeepsOldResult`；`FailedSegmentLocatableInDiagnostics`。
- **最小实现：** 仅规划器适配与失败诊断转绿所需；路径简化与时间参数化归 WP-16-T03、平滑后复检归 WP-16-T04；不实现任何碰撞判定（全部经 WP-07）。
- **正常/边界/失败测试：**
  - 正常：Given 可行任务段与合法策略投影，When 规划，Then 返回无碰路径且版本/参数/种子/路径入快照记录。
  - 边界：Given 引导点与超时预算边界，When 规划，Then 超时诊断携带预算来源与已耗时间，路径部分不发布。
  - 失败：Given 起终点不可行或无碰路径不存在，When 规划，Then `IRD-TRJ-NO-PATH` 附段与端点；Given 规划器系统故障，Then `IRD-TRJ-PLANNER-FAILED` 且旧结果保留不被污染。
- **精确验证命令：**（仓库根目录、VS x64 环境；三形式任选其一必须通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory_test$'`；预期退出码 0。
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_test`；预期构建成功。
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_trajectory_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；无本地碰撞开关/采样参数/安全距离副本（静态检查零命中）；无 RobWork API 改动；planner 夹具有 source/generationMethod 并入 WP-02 manifest；`check-boundaries.ps1` 零违规。
- **证据工件：** `evidence/WP-16/planner-adapter-report.md`（版本/参数/种子记录、失败段定位案例、与旧调用链只读对照说明）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核约束投影一致性。
- **提交格式：** `WP-16-T02: RobWork 规划器适配`
- **停止与升级条件：** WP-07 投影接口或 resourceBudget 语义未冻结、或 RobWork 规划器稳定 API 无法满足确定性记录时停止并升级 WP-07/架构负责人；实现者不得担任本卡独立验证者。
