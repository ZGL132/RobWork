# REUSE-T51——WP-04 普通位姿 IK → JacobianIKSolver 落地与全文件解决批（原始证据）

起始 SHA＝`a89776f06c80a0408c4fa28e9e58e32215b74738`（2026-10-26，治理批之后）。
契约＝[tasks/REUSE-T51.json](../../../tasks/REUSE-T51.json)；凭证＝[changes/REUSE-T51.json](../../../reuse/changes/REUSE-T51.json)。

## 1. 实施摘要

- **迁移面**：`kinematics/src/Ik.cpp` 完整位姿普通模式（`taskSpace==FullPose` 且无 `taskConstraint`）迭代核迁移上游 `rw::invkin::JacobianIKSolver`（H1 直接 API；migration §13.3 WP-04 行批准范围）。装配经 T17 锚（`runtimeModelView()`→已编译 WorkCell）取 `SerialDevice`＋每初值任务私有 `State`；foi＝默认 TCP 编译产物帧（名称端口权威路径）/命名 TCP 法兰＋目标一次逆偏置复合；`solveLocal(bTed, min(posTol,oriTol), state, 1)` 单步循环逐步驱动——迭代数逐步实测（GAP-IK-01 §1"不能用配置上限伪造实际迭代数"），取消探针保持 §8.3 周期；收敛解经 `JointMapping::qAuthoritativeFromRuntime`（本包新增互逆方向）回权威角后由 `FkEvaluator` 独立 FK 复算**双容差逐项**判定（GAP-IK-01 §1"优先原API，结束后以任务实际位置/姿态容差独立复核"）。
- **删除面**：完整位姿模式自写 6 维 DLS/正规方程（LLT）链——`dampedLeastSquaresStep` 收缩为仅位置模式 3 维专用（`taskDim` 参数删除）；PositionOnly/约束模式仍走既有链（上游 ParallelIKSolver Target 掩码消费被 runtime 视图权限审查阻断、Euler≠EAA 掩码反例在案——GAP-IK-01 §1，归 WP-05）。
- **值面边界**：锚缺失/设备未命中/自由度不符/参考帧未解析/上游异常→既有 `IkFacilityFailure` 值面（阶段 InitialFkEvaluation/IterationFkEvaluation），不抛承诺不破；**无隐藏 fallback**（KinTypes T17 注纪律）。预算耗尽而残差已入双容差按产品验收放行（对齐既有引擎"第 budget 次更新后的复评收敛"预算语义）。
- **契约版本**：`kIkSolverContractVersion` 4→5（CON-04——换核改变同请求数值结果）；请求词表/五类结局/统计口径/取消语义零变化。
- **CMake**：`sdurw_invkin` 增链（集成第四真实目标＋冒烟 `RW::sdurw_invkin` 同名 ALIAS——T07/T12 先例形态；该名非集成模式判定符，全库 rg 零命中）；门禁词表零改动。

## 2. 验证（真实执行，PASS/FAIL/SKIP/NOT RUN 分列）

集成树（VS x64 Release，`build/`）；执行器＝[run-tests.ps1](run-tests.ps1)/[run-contract.ps1](run-contract.ps1)（T36 同款机制）。

| 运行 | 标签 | 结果 | 退出码 |
| --- | --- | --- | --- |
| KinIk 全组（39 用例） | normal-kinik | 39 passed / 0 failed / 0 skipped | 0 |
| 闭式 oracle＋黄金去重 | oracle-closedform | 4 passed / 0 failed | 0 |
| 上游直接对照＋轨迹同一性 | upstream-comparison | 2 passed / 0 failed | 0 |
| 断开实验（生产变异红例） | disconnected-upstream | **2 failed**（预期红） | **1** |
| 全量复测（恢复后） | final-recheck | 297 ran：296 passed＋1 skipped（`PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1`——既有环境 skip，REUSE-T36 同款记录） | 0 |
| 契约测试 | final-contract | 57 passed / 0 failed | 0 |

黄金数据集零改动通过（kin-ik-dedup 三用例、kin-search-outcomes 七场景判定表、kin-fk-analytic 等）。

### 断开实验（ird-reuse-production/1 disconnected 面）

变异＝生产 `Ik.cpp` 的上游 `solveLocal` 停判据实参 `upstreamMaxError`→`1e9`（上游对任何初值立即假报收敛，迭代数恒 0、解停在初值处——**上游停判据失效面**）；产品独立双容差复核（FkEvaluator 正解回代）随即拦截：`KinIk.ConvergesOnReachableTarget_WP15T04_ACC2`＋`KinIk.FullPoseUpstreamClosedFormOracle_REUSE_T51` 真实失败（2/2，exit 1）。变异原件＝[production/Ik.mutated.cpp](production/Ik.mutated.cpp)（originalSha256=`6685055f…`→mutated=`b8f402c8…`）；恢复最终源码重建后 final-recheck 绿。该实验同时实证：独立复核非装饰——假成功不得放行为可行解（GAP-IK-01 §1 结果正确性行的产品侧落地）。

### 上游直接对照（upstreamComparison 面）

`KinIk.FullPoseUpstreamDirectComparison_REUSE_T51`：同一编译产物、同初值 (0.35,−0.45)、同目标、同 foi（名称端口解析的默认 TCP 帧），测试内**直接**驱动 `JacobianIKSolver`（单次 `solveLocal(maxIter=预算)`），收敛解经同一权威映射与产品解**逐位一致**——薄适配（q 映射/foi 选择/maxError/单步循环）零数值偏差。
`KinIk.SolveLocalSingleStepLoopTrajectoryIdentity_REUSE_T51`：单步循环 vs 单次多步调用轨迹逐位同一——生产适配"单步循环迭代数＝上游真实更新数"的实证基础。

## 3. 全文件解决批（Ik.cpp migration-debt）

- **Ik.cpp**（1318 行，19 区段连续铺面）：`kinematics.IkSolver.solve` 行 adapter＋四套生产证据（[production-ik-solver-solve.json](production-ik-solver-solve.json)）；其余 9 既有行全部 business-specific/migrated（逐行 nonOverlapEvidence＋差异/保留理由——poseError 双容差验收残差、约束模式数学〔GAP-IK-01 阻断面〕、dampedLeastSquaresStep 仅位置唯一迭代核、makeInitialValues 业务编排等）＋8 新审计铺面行（头/failCaller 错误面/resolveTcpOrThrow/convergedForTask/makeFilteredRecord/evaluationIntervals/axisRange/命名空间收尾）。
- **JointMapping.hpp**（75 行三区段）：新行 `qAuthoritativeFromRuntime`＋头铺面＋既有行刷新。
- **Ik.hpp**（569 行七区段）：契约面铺面（常量/策略/端口/请求/结果/接口/实现类）——此前零覆盖，随变更文件逐行覆盖义务补齐。
- **CMakeLists.txt**：UpstreamLinks 行 1-775＋ird_add_gtest 行 583-595 刷新。
- **IkTest.cpp**：KinIkSuite 1-1905＋MidSolveDegradedView 244-276。
- **permissions.json**：`REUSE-T51-Ik.cpp`（model/state/solver＋write-state，task-private）＋`REUSE-T51-IkTest.cpp`（测试面同型）两 grant；三处 kinematics CMake build 摘要级联。
- **catalog.json**：KIN-IK `candidate→verified`（[ik-capability-review.json](ik-capability-review.json)——T36 COLLISION 先例同型；范围＝JacobianIKSolver 完整位姿迭代核消费，ParallelIK/ParallelDevice 引用仍为检索绑定面不声称可迁）。

## 4. 边界与遗留（诚实分列）

- 冒烟树：`sdurw_invkin` 冒烟 ALIAS 已接线（配置面），但 kinematics 冒烟 test/contract exe 维持 **NOT RUN**——T19 遗留缺口（Fk.cpp 两模式编译而 runtime WorkCellConstView 实现集成专属，LNK2019 原件在案），归 T19 语义/后续包处置；本包不冒称修复。完整位姿上游路径现依赖编译锚（纯值视图→设施失败值面），冒烟树求解面用例预期随该缺口处置归后续包。
- `kIkDampingLambda` 常量保留（仅位置模式链继续消费）；黄金数据集零改动（闭式参考＋容差配对面本就引擎无关，初值即解的用例在两引擎下都零迭代收敛）。
- 独立验收未由实施会话执行（契约 status=ready 待独验）；KIN-IK verified 升级由本包独立验收覆盖（T36 先例同型）。
- 真实迭代统计口径＝solveLocal 单步循环实测（轨迹同一性钉背书）；GAP-IK-01 §2 扩展提案（真内部统计/掩码/取消回调）维持 Draft 未批准，归 WP-05。
