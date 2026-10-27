# GOV-T27 — WP-05 同 IK 入口分审记录（仅位置/部分姿态/双容差/取消与迭代观测）

状态：**实施完成待独验**（契约 [tasks/GOV-T27.json](../../../tasks/GOV-T27.json)，status=ready）。
起始 SHA＝`4f83917e91bc3987e9ee257b326e58cd80a8b268`（REUSE-T51 收口提交）；分支 redesign-main 主工作树串行。
本包为 migration §13.3 WP-05 行（"WP-05 / GAP-IK-01 相关子任务"）的分审与登记包：**零产品代码变更**；结论＝仅位置与部分姿态两模式属**实质缺口**，[GAP-IK-01](../../reuse/extensions/GAP-IK-01.md) blocked 登记待所有者裁决（提案维持 Draft 未批准，本包不预支批准）。

## 1. 分审范围与任务口径

migration §13.3 WP-05 行原文要点：同 IK 入口分审**位置、部分姿态、双容差、取消/迭代观测**；比较已有上游入口；**EAA 与 Euler 掩码不得混同，后过滤不得冒充约束求解**；实质缺口先提案批准、不另写 DLS/差分；各模式残差、奇异/取消/误报收敛验证；所有消费者迁移后删除余下旧核心。

产品现状基线（本包实测核对，非引用）：`kinematics/src/Ik.cpp`（1318 行，HEAD 4f83917e9 与 REUSE-T51 收口版逐位一致——`git diff 554fa6f7a..HEAD -- .../kinematics/src/Ik.cpp` 为空）三模式：

| 模式 | 判别条件（Ik.cpp:806/651-657） | 迭代核 |
| --- | --- | --- |
| 完整位姿普通 | `taskSpace==FullPose && !taskConstraint` | 上游 `rw::invkin::JacobianIKSolver`（REUSE-T51，已收口 done） |
| 仅位置 | `taskSpace==PositionOnly` | 本单元 3 维位置子块 DLS（`dampedLeastSquaresStep`，Ik.cpp:422-444） |
| 任务约束（部分姿态） | `taskConstraint.has_value()` | 本单元差分 Jacobian＋DLS（`constrainedPoseError`/`constrainedDampedLeastSquaresStep`，Ik.cpp:336-409） |

## 2. 上游入口全目录比较（rw/invkin 13 入口，本包逐一核对）

目录清单（`RobWork/RobWork/src/rw/invkin/`，本包 ls 实测）：AmbiguityResolver、CCDSolver、ClosedFormIK、ClosedFormIKSolverKukaIIWA、ClosedFormIKSolverUR、IKMetaSolver、InvKinSolver、IterativeIK、IterativeMultiIK、JacobianIKSolver、JacobianIKSolverM、ParallelIKSolver、PieperSolver。

| 上游入口 | 任务形态（本包读码证据） | 对产品三模式的意义 |
| --- | --- | --- |
| JacobianIKSolver | **恒全 6 维**：`solveLocal` 误差＝完整速度旋量（位置 3＋EAA 3，JacobianIKSolver.cpp:81-93），`normInf(b_eXed)` 单标量停判；无掩码/无逐行容差参数 | 完整位姿已消费（REUSE-T51）；对仅位置/部分姿态**无入口** |
| JacobianIKSolverM | JacobianIKSolver 多解变体（IterativeMultiIK），同 6 维任务面 | 同上——无掩码 |
| CCDSolver | 全 6 维：位置误差 ep 与姿态误差 eo 同对 `maxError` 判停（CCDSolver.cpp:313/330） | 无掩码、无逐行容差 |
| IKMetaSolver | 包装另一 IterativeIK 做随机重启（IKMetaSolver.hpp:47-70），任务形态随被包装者 | 无自身任务掩码 |
| CCDSolver/Transpose/DLS/Weighted | 均为 JacobianIKSolver 内部步型开关（`setSolverType`） | 不构成独立任务面 |
| ClosedFormIKSolverUR / ClosedFormIKSolverKukaIIWA / PieperSolver | 设备专属闭式解（UR/KukaIIWA/Pieper 运动学类） | 产品任意串链不适用（无设备族前提） |
| InvKinSolver / IterativeIK / IterativeMultiIK / ClosedFormIK / AmbiguityResolver | 接口/基类/歧义消解装饰 | **关键事实**：接口面仅有 `solve(baseTend,state)` 全位姿形态与 `setMaxError`/`setMaxIterations` 单标量配置——无任务行掩码、无逐行容差、无真实迭代统计回传 |
| **ParallelIKSolver** | **唯一掩码入口**：`Target::enabled` 6 行＝"x, y, z and **EAA** x, y, z directions"（ParallelIKSolver.hpp:132-141/166-168）；但构造形参＝`const rw::models::ParallelDevice*`（ParallelIKSolver.hpp:69） | 掩码存在但 (a) 语义为 **EAA 行≠产品 Euler 行**；(b) 要求 **ParallelDevice**——产品编译产物为 SerialDevice，无该视图（见 §3/§4） |

## 3. 模式一：仅位置（PositionOnly）——实质缺口，GAP blocked 登记

**裁决：无产品可消费的上游对应入口＝实质缺口。** 证据链（全部本包读码实测）：

1. 序列设备求解器恒全 6 维任务（§2 表 JacobianIKSolver/CCD 行）——把仅位置表达为"全位姿求解＋后过滤"被 GAP-IK-01 §4 明确否决（"施加了用户未启用的约束，会错误丢弃合法构型"），本包边界同禁（"后过滤不得冒充约束求解"）。
2. 唯一掩码入口 ParallelIKSolver 的消费前提不成立：
   - 构造要求 `const rw::models::ParallelDevice*`（ParallelIKSolver.hpp:69）；
   - ParallelDevice 由 `std::vector<ParallelLeg*>` legs/junctions 构造（ParallelDevice.cpp:108/117），与序列链是两种设备模型；
   - 产品编译产物设备＝`rw::models::SerialDevice`（runtime/src/WorkCellCompiler.cpp:799-800 `new rw::models::SerialDevice(...)`——本包读码实测）；
   - 业务单元自行构造 ParallelDevice 视图＝模型构造越权（ARCH §3.6/SA-19：runtime 唯一模型 owner）——与 GAP-IK-01 §1 仅位置行"ParallelDevice 视图的构造权限须在 runtime 侧审查，不能在业务自建链"一致，本包读码复核成立且加严（该入口对本产品**当前编译产物形态**根本不可达，消费须 runtime 新增 ParallelDevice 视图能力＋所有者权限裁决）。
3. 处置：**GAP-IK-01 blocked 登记待所有者裁决**（提案含两条路径：runtime 侧 ParallelDevice 视图能力审查，或 GAP-IK-01 §2 最小扩展 patch——均未批准）；不另写 DLS/差分；既有 3 维位置链**保留**（存量唯一迭代核，非隐藏 fallback——Ik.cpp:412-421 注与 functions/kinematics.json dampedLeastSquaresStep 行登记一致）。

## 4. 模式二：部分姿态/任务约束（Euler 掩码）——实质缺口，GAP blocked 登记

**裁决：无上游对应入口＝实质缺口。** 证据链：

1. 产品约束契约＝参考系 ZYX **Euler 分量**掩码（6 分量 x/y/z＋roll/pitch/yaw）＋`referenceInBase` 参考系旋转＋`allowToolRoll`/`rollRange` 自由工具滚转目标族（Ik.cpp constrainedPoseError:336-363、nearestToolRollTarget:296-321；契约面 KinTypes.hpp TaskPoseConstraint；消费者组装 ui/plugin/KinEvaluationChannel.cpp:1257-1332）。
2. 上游唯一掩码语义为 **EAA 坐标行**（ParallelIKSolver.hpp:132-141："The 6 values specify x, y, z and **EAA** x, y, z directions"）——**EAA 行不是 Euler 行**：GAP-IK-01 §1 已登记可复现反例（probe-r2：固定 Euler yaw/pitch 改变 roll 仍改变 EAA 已选分量；traceability/reuse/REUSE-T01/probe-r3/ 同族探测在案），参考系转换也不能一般等价。本包核对该反例引用与登记原文一致，未发现上游新增 Euler 任务面（§2 全目录核对）。
3. 且该入口同样要求 ParallelDevice（§3 第 2 条同链不可达）。
4. 自由工具滚转目标族（最近合法扭转选择）上游无对应物（GAP-IK-01 §1 部分姿态行"自由滚转集合的最近目标规则也要明确验证"——functions/kinematics.json nearestToolRollTarget 行 business-specific 登记在案）。
5. 处置：同 §3——GAP-IK-01 blocked 登记待所有者裁决；约束差分链保留（存量非新写）；后过滤/全位姿替代不实施。

## 5. 模式三：双容差——薄适配组合（WP-04 已验收模式），无新增迁移面

- 上游事实：`solveLocal(bTed, maxError, state, maxIter)` 单一 `maxError` 标量（无逐行/分单位容差参数；JacobianIKSolver.cpp:81）；`IterativeIK` 配置面仅 `setMaxError`/`setMaxIterations` 单标量（接口面读码）。
- 产品口径（REUSE-T51 已落地并独验 PASS）：上游停判 `maxError=min(positionTolerance, orientationTolerance)`（不承担验收语义），解的最终验收＝阶段①独立 FK 复算按双容差逐项复核（Ik.cpp:808-815/1014-1018；GAP-IK-01 §1 完整位姿行"结束后以任务实际位置/姿态容差独立复核"）。
- 仅位置任务＝单容差（positionTolerance 唯一判定；orientationTolerance 仅合法域校验——Ik.hpp IkRequest 注/A02 契约）。
- 约束任务双容差属 §4 缺口模式范围（随 GAP 裁决）。
- **结论：双容差在上游无独立入口是结构性事实，薄适配组合（停判 min＋独立逐项复核）是已验收的正确消费形态；本包无新增迁移面。**

## 6. 模式四：取消/迭代观测——单步循环模式（WP-04 已验收先例），无新增迁移面

- 上游事实：`solveLocal` 返回 bool＋最终 State，无真实迭代数/停止原因回传；`IterativeIK` 仅有上限配置（GAP-IK-01 §1 取消/统计行；本包接口面复核一致）。
- 产品口径：FullPose 路径经 `solveLocal(maxIter=1)` 单步循环驱动——每调用＝上游循环体恰一次，迭代数逐步实测，取消探针保持 §8.3 周期；单步/多步调用轨迹同一性有测试钉（`SolveLocalSingleStepLoopTrajectoryIdentity_REUSE_T51`，IkTest.cpp:1846）。该形态经 REUSE-T51 独立验收 PASS。
- 余下两模式走产品自有链，迭代数天然逐步实测、取消探针逐迭代周期（Ik.cpp:963-979/1082-1089）。
- 真实内部统计（真实求值次数/停止原因分类回传）属 GAP-IK-01 §2 `SolveReport` 扩展提案范围——未批准，不实施。
- **结论：取消/迭代观测无上游入口，单步循环＝已验收的诚实替代模式；本包无新增迁移面。**

## 7. 各模式残差/奇异/取消/误报收敛验证（本包真实执行）

本包零产品代码变更——验证面＝存量行为如实复跑（增量口径：修改面为治理文档，验证规模按本包契约 verify 命令执行）。执行器与本目录脚本：`run-tests.ps1`（kinematics 全量）、`run-contract.ps1`（contract 套件）；ui 消费通道定向见 §7.3。证据文件（gtest XML＋ird-test-report.json＋日志）落本目录。

### 7.1 逐模式验证矩阵（映射到既有测试面——全部真实运行）

| 模式 × 面向 | 测试（IkTest.cpp 除注明外） | 断言意图 |
| --- | --- | --- |
| 完整位姿·残差 oracle | `FullPoseUpstreamClosedFormOracle_REUSE_T51`、`FullPoseUpstreamDirectComparison_REUSE_T51` | 闭式解析参考＋上游直接驱动对照（REUSE-T51 落地面） |
| 完整位姿·误报收敛 | `FullPoseUpstreamUnreachableNoFalseSuccess_REUSE_T51` | 不可达目标上游假成功被独立复核拦截（REUSE-T51 disconnected 变异红例同源机制实证） |
| 完整位姿·限位过滤 | `FullPoseUpstreamLimitViolationFiltered_REUSE_T51` | 上游不查限位默认下产品硬过滤②契约区间直比 |
| 完整位姿·迭代观测 | `SolveLocalSingleStepLoopTrajectoryIdentity_REUSE_T51` | 单步/多步调用轨迹同一性（迭代数真实） |
| 仅位置·残差 oracle | `PositionOnlyTaskIndependentDimension_KIN_P2` | 已知解解析 sanity＋独立 FK 复验（位置入容差、姿态如实记录且不参与判定）＋FullPose 对照保守语义（退化链无解不冒称可达） |
| 仅位置·请求校验 | `TaskSpaceOutOfRangeFailsFast_KIN_P2` | 词表外值 fail-fast（NFR-COR-03） |
| 约束·残差 oracle（真实 FK） | `ToolRollDirectionCorrectionRealFk_REQ09_WP14T12`、`ToolRollAxisCenteredSeedCorrects_REQ09_WP14T12`、`RotatedReferenceMaskConverges_REQ01_WP14T12`、`PitchRowNonzeroYawConverges_WP14T12` | 收敛解经独立 FK 复算满足 Euler 掩码/滚转族约束（求得的解与验收语义同源） |
| 约束·奇异（万向锁） | `YawOnlyMaskSatisfiedAtGimbalTarget_WP14T12`、`GimbalPlus90PitchConverges_WP14T12` | ±90° 俯仰奇异分支同号语义下不误拒、可收敛（批34 F3 修复面） |
| 约束·误报收敛（族外拒绝） | `ToolRollRangeScreensTwist_WP14T12`、`ToolRollOppositeAxisRejected_REQ09_WP14T12`、`TiltedToolTwistFreeDirection_WP14T12`、`ToolRollPeriodicIntervals_REQ09_WP14T12` | 滚转族外扭转筛除/反向轴拒绝（族内不误收、族外不漏拒） |
| 取消 | `CancellationYieldsNoTerminalFields_WP15T04_ACC2` | 首探针即取消＝cancelled=true 且无终局字段（取消不是结局） |
| 确定性 | `RepeatedSolveBitwiseIdentical_WP15T04_ACC3` | 同请求两次求解逐位一致 |

### 7.2 覆盖缺口如实登记（不补造、不冒称）

- **奇异面**：仅位置模式的"奇异位形收敛行为"由 KIN-P2 用例的六轴同轴退化链承载（FullPose 对照＝无解保守语义、PositionOnly＝收敛）；约束模式奇异面＝万向锁两用例；序列雅可比奇异值解析 oracle 在 FkTest（`RevPrismSingularValuesAnalytic_WP15T03_ACC1` 等，指标链非 IK 迭代链）。IK 迭代核在一般意义奇异位形（J 亏秩非退化链）下的专用红例**无既有用例**——本包不新造（分审包不扩测试面；该面向随两模式迁移实施包按 GAP 裁决后补），登记为验证面现状。
- **①残差复验拦截的专用变异红例**：REUSE-T51 的 disconnected 变异实证了"独立复核拦截上游假成功"机制（完整位姿路径）；旧链两模式的 3a 复验为同一硬过滤机制（Ik.cpp:1164-1205 对全部模式统一执行），无逐模式专用变异用例——登记现状，迁移实施包按提案验收条款补造（GAP-IK-01 §5）。

### 7.3 消费者契约

- kinematics 单元内消费者（Envelope/Workspace/Sampling/Evaluators——仅位置；Evaluators——约束）随 kinematics 全量套件真实运行（EnvelopeTest/WorkspaceTest/TaskPointsBatchTest/KinP4FlowsTest 等）。
- contract 套件（`sdurws_ird_kinematics_contract_test`）＝单元契约面（REUSE-T51 同口径）。
- ui 消费通道（约束模式组装消费：`ui/plugin/KinEvaluationChannel.cpp:1257-1332`）：`sdurws_ird_ui_test --gtest_filter=KinEvaluationChannel.*` 定向子集（本包零 ui 变更——定向消费面证据而非全量 ui 回归）。

### 7.4 实测结果（2026-10-26 本包执行，PASS/FAIL/SKIP 如实分列）

| 套件 | 结果 | 计数分列 | 证据 |
| --- | --- | --- | --- |
| sdurws_ird_kinematics_test 全量 | PASS（exit 0） | **297 ran＝296 passed＋1 skipped**（既有环境 skip `PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1`——REUSE-T51 基线同名单）、0 failed | `full-suite.{xml,log,-report.json}` |
| sdurws_ird_kinematics_contract_test | PASS（exit 0） | **57 ran＝57 passed**、0 failed/0 skipped | `contract.{xml,log,-report.json}` |
| sdurws_ird_ui_test 定向（KinEvaluationChannel.*） | **FAIL（exit 1）——12/28 failed，全部为已登记既有失败，零新增** | 28 ran＝16 passed＋**12 failed**（逐名 ⊆ REUSE-T41 ui_test 14 项已登记失败名单〔F-973 数值链×13＋F-560×1——traceability/findings.json F-973 open、所有者归类处置中〕；本包逐名 diff 实证：`notInOld=[]`）；失败形态与 F-973 登记一致（fk.ok() 为假/批量 outcomeState=not-run——ui 通道物化面，非产品算法面：同请求的 IkSolver 约束/仅位置用例在 kinematics 全量 296 passed 全绿） | `ui-consumer.{xml,log,-report.json}` |

零因果证明（三条独立）：①本包产品树零 diff（`git diff 554fa6f7a..HEAD -- kinematics/{src,include}` 为空＋工作区产品文件零改动）；②12 项失败逐名 ⊆ REUSE-T41 候选态（2026-10-25，HEAD 8c402228b 时代）已登记名单；③同一 IkSolver 约束/仅位置路径在 kinematics 套件（同 HEAD 构建）全绿。ui 通道 12 项失败归 F-973（open，所有者归类），本包不修复不越权。

## 8. 消费者面与旧核心保留清单

消费者（本包 grep 实测）：

- 仅位置：`kinematics/src/Workspace.cpp:802`、`Envelope.cpp:164`、`Evaluators.cpp:2584`（位置存在性/覆盖/包络）。
- 约束：`kinematics/src/Evaluators.cpp:631/1315/1591`（评估通道）＋`ui/plugin/KinEvaluationChannel.cpp:1257-1332`（UI 规则组装 TaskPoseConstraint——allowToolRoll/rollRange）。

旧核心保留清单（删除前置"所有消费者迁移后"不满足——两模式迁移 blocked）：

| 函数（Ik.cpp） | 归属模式 | 保留理由 |
| --- | --- | --- |
| `dampedLeastSquaresStep`（:422-444） | 仅位置 | 该模式唯一迭代核；上游无序列设备仅位置入口（§3） |
| `constrainedPoseError`（:336-363） | 约束 | A-16 唯一契约残差构造（求解与验收同源）；上游无 Euler 掩码入口（§4） |
| `constrainedDampedLeastSquaresStep`（:377-409） | 约束 | 约束模式唯一迭代核（差分整个残差含参考系投影/欧拉提取/掩码导数——上游无对应） |
| `nearestToolRollTarget`（:296-321） | 约束 | 自由滚转目标族最近合法扭转（上游无 roll 族/区间投影对应物） |
| `zyxEulerAngles`（:235-253） | 约束 | 参考系 ZYX 欧拉提取（万向锁两分支同号——批34 F3）；上游无 Euler 分量接口 |

## 9. 结论与移交

1. 四模式分审定案：完整位姿已收口；双容差与取消/迭代观测＝已验收薄适配模式，无新增迁移面；**仅位置与部分姿态＝实质缺口，GAP-IK-01 blocked 登记待所有者裁决**（本包零产品变更，不另写 DLS/差分）。
2. 待所有者裁决的三选一（GAP-IK-01 §5 批准项口径）：①批准原库最小扩展（ParallelIKSolver 非虚重载等——patch 渠道，需登记 PATCHES.md）；②批准 runtime 侧 ParallelDevice 视图能力（模型能力扩展审查）；③否决——两模式按裁决保留既有链并相应归档债务。
3. 迁移完成后（任一路径落地且消费者全迁）才删除 §8 旧核心——前置现不满足。
4. 验证证据：本目录 `full-suite.{xml,log,-report.json}`、`contract.{xml,log,-report.json}`、`ui-consumer.{xml,log,-report.json}`、ird_gates/commit-lint/validate-docs/verify-task-scope 输出随提交批记录于契约 note。
