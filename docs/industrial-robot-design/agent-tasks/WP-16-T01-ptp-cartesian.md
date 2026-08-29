# WP-16-T01 PTP 与笛卡尔接近撤离

- **Task ID / 需求 ID / ADR / 阶段：** WP-16-T01；TRJ-01（关节空间点到点与有序作业序列）、TRJ-02（笛卡尔直线接近/撤离段＋沿途 IK 连续性检查）＋AT-06；无直接关联 ADR；阶段 C / R1。契约：`module-design/trajectory-planning.md` v0.3 §2/§3/§5.3（IK 连续性冻结）、`architecture/canonical-kinematics.md`、`architecture/evaluation-semantics.md` §1～2、`architecture/public-interfaces.md` §3/§7。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（trajectory-planning.md v0.3、需求 v0.7）
- **前置任务及必需工件：** 无包内前置；外部前置（总纲 §5.3）：WP-15-T06（上游 `KinematicResult` 可产出并经 WP-05 接纳）、WP-14-T01（`EngineeringRequirements`/`LoadCase`）、WP-05-T03（`IEngineeringEvaluator` 端口头与 `ResultEnvelope`）、WP-07-T02（`CollisionEvaluator` 契约引用）、WP-08-T01/T02（调度契约引用）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/`）
  - 创建：`CMakeLists.txt`（登记 `sdurws_ird_trajectory`、`sdurws_ird_trajectory_test`、`sdurws_ird_trajectory_contract_test`，随 `IRD_BUILD_BUSINESS_PLUGINS`）；修改 `../CMakeLists.txt`（plugins 聚合入口仅新增 trajectory 子目录一行）
  - 创建：`include/sdurws/ird/trajectory/IkBranchPolicy.hpp`、`TrajectoryDiagnostics.hpp`、`TrajectoryEvaluator.hpp`（端口骨架）、`src/TrajectoryEvaluator.cpp`（上游校验）、`src/PtpCartesianPlanner.cpp`、`src/IkContinuityChecker.cpp`、`test/PtpCartesianTest.cpp`、`testdata/trajectory/ptp/`、`testdata/trajectory/cartesian/`、`evidence/WP-16/`
- **禁止修改的文件和公共接口：** 一切非本拥有目录源码；WP-15 `KinematicResult`/`IkCandidate` 语义（契约引用只含公共类型头，不链接实现）、WP-14 `EngineeringRequirements`/`LoadCase`、WP-07 碰撞策略、WP-05/08 公共接口与签名；`TrajectoryPlan` 不得出现第二套 DTO；不建 GUI 测试目标（TRJ-07 归 WP-10/WP-22）；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（`plugins/trajectory/` 不存在，无任何 `sdurws_ird_trajectory*` 目标）。
- **修改后接口：** CMake 目标 `sdurws_ird_trajectory`（计算核心）、`sdurws_ird_trajectory_test`、`sdurws_ird_trajectory_contract_test`（骨架登记）；公共领域类型 `IkBranchPolicy`（自动分支策略、用户显式锁定、连续性阈值，经 `AnalysisConfiguration` 进输入切片）与段类型 Schema v1 生成器（`kind ∈ {JointPtp, CartesianLine, Dwell}`，`CartesianArc` 为已登记枚举值、产生该类段属 TRJ-08 后续交付；每段含路点、来源任务点引用，驻留段携带持续时间与 `LoadCase` 工况切换引用）；IK 连续性检查器：笛卡尔段按关节步长采样（转动 0.05 rad、移动 0.01 m）逐点以上一解为初值求 IK，相邻解逐轴差超 `IkBranchPolicy` 连续性阈值（模块私有默认 0.05 rad/0.01 m，可评审）判分支跳变；诊断 `IRD-TRJ-UPSTREAM-MISSING`（Input/Error）、`IRD-TRJ-BRANCH-JUMP`（附段、点对、差值）、`IRD-TRJ-SINGULARITY`（`J_norm` 条件数阈值 100，Warning 不阻断）。
- **实施步骤：**
  1. 创建 CMake 接入并先写 `PtpCartesianTest.cpp` 全部 RED 断言，构建确认失败。
  2. 定义 `IkBranchPolicy`、`TrajectoryDiagnostics` 与错误码常量。
  3. 实现 `TrajectoryEvaluator` 端口骨架与上游校验（`KinematicResult` 缺失/非 Current/版本不兼容 → `IRD-TRJ-UPSTREAM-MISSING`）。
  4. 实现 `PtpCartesianPlanner`：任务序列分段为 JointPtp/CartesianLine/Dwell，路点/时间戳/单位按 Schema v1。
  5. 实现 `IkContinuityChecker`（冻结采样步长与连续性阈值、分支跳变诊断、奇异邻域按 `J_norm` 条件数）。
  6. 生成 ptp/cartesian 夹具并登记 WP-02 manifest，按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `PtpSegmentFromOrderedTaskSequence`；`CartesianLineApproachRetreatSegments`；`DwellSegmentCarriesDurationAndLoadCaseRef`；`IkContinuityUsesFrozenSamplingStep`（0.05 rad/0.01 m）；`BranchJumpReportsSegmentPairAndDelta`；`MissingUpstreamYieldsUpstreamMissing`；`SingularityWarnsWithoutBlocking`。
- **最小实现：** 仅段生成、上游校验与 IK 连续性转绿所需；RobWork 避障搜索归 WP-16-T02、平滑与时间参数化归 WP-16-T03（段的 `tStart/tDuration` 由 T03 时间参数化填充）。
- **正常/边界/失败测试：**
  - 正常：Given 任务序列与上游 `KinematicResult` 候选，When 生成段，Then JointPtp/CartesianLine/Dwell 路点、参考系 objectId 与来源引用符合 Schema v1。
  - 边界：Given 笛卡尔段相邻解差恰在连续性阈值边界与奇异邻域候选，When 检查，Then 边界内通过、超阈值判 `IRD-TRJ-BRANCH-JUMP`、奇异仅 Warning。
  - 失败：Given 上游结果缺失/非 Current，When 评估，Then `IRD-TRJ-UPSTREAM-MISSING` 且不产生部分提交或正式结果。
- **精确验证命令：**（仓库根目录、VS x64 环境；三形式任选其一必须通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory_test$'`；预期退出码 0。
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_test`；预期构建成功。
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_trajectory_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `plugins/trajectory/` 新文件与 plugins 聚合 CMake 一行接入；无 Qt Widgets、其他插件私有头、本地碰撞参数副本、直接文件 IO；无第二套 `TrajectoryPlan` DTO；`check-boundaries.ps1` 零违规。
- **证据工件：** `evidence/WP-16/ptp-cartesian-report.md`（段 Schema 案例、IK 连续性采样与跳变诊断记录、夹具清单）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核冻结步长与阈值。
- **提交格式：** `WP-16-T01: PTP 与笛卡尔接近撤离`
- **停止与升级条件：** 段 Schema/连续性阈值与 trajectory-planning.md §3/§5.3 冲突、或上游 `KinematicResult` 接口未冻结时停止并升级架构负责人；实现者不得担任本卡独立验证者。
