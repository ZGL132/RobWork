# REUSE-T32 / WP-11：其余路径轨迹工具收口与 PTP 极值缺口定案

基线 `c65ffc4fc495da0b01b2060ff3049eb127091d72`，`redesign-main` 本地串行实施。当前为实施自查完成、可交独验；独验 **NOT RUN**，任务保持 ready，未登记 closed。直线（笛卡尔姿态）已由 REUSE-T26 交付；本包不重做 T26/T09 成果，REUSE-T22 登记的 PTP 缺口由本包完成候选2定案。

## 需求、删除与适配边界

TRJ-03/TRJ-06/TRJ-02 与 §10.4/§9.1。生产修改面＝`trajectory/src/Planner.cpp`：三处度量手写核删除——逐相邻对 Linf 步长（→`MetricFactory::makeInfinity`）、L1 路径长度（→`PathAnalyzer::pathLength` 折叠 `MetricFactory::makeManhattan`）、直连采样 L2（→`MetricFactory::makeEuclidean` 单对 distance）；`planPtpWithObstacleAvoidance` 两处内联 Σ|Δq| 循环同日删除并委托 `pathJointLength`（"同语义同一实现点"）。稳定选择序四键全序、词表、身份面、取消/预算/种子纪律不变；无隐藏 fallback、无新缓存/模型/状态。`trajectory/CMakeLists.txt` 仅新增 `test/PlannerReuseTest.cpp` 注册（`if(TARGET sdurw_kinematics)` 集成条件段，冒烟模式不受影响——smoke-build.log）。**Continuity.cpp 本包零改动**：`rotationAngleBetween` 按实测反例保留（见下）。

被否候选（如实登记，H1 第1条）：

- `rotationAngleBetween` 换 `EAA(Ra·Rb⁻¹).angle()` 或 `Rotation3DAngleMetric`（内部同 EAA 路径）：**否决**。同矩阵实测（PlannerReuseTest RotationAngleCounterExample 用例、Quaternion 半角精确构造输入）：EAA acos 分支边界系统性超 C7 角容差——轴对齐 Δ=1e-6 实测偏差 ≈4.4e-11，一般轴 Δ=π−3e-6 ≈1.6e-10（邻域扫描 π−{1.5,2,3,5}e-6 为 8.1e-11～1.6e-10），超容差 1e-12 约 40~160 倍；产品 2·atan2(‖vec‖,|w|) 全网格（0/微角/一般角/近π/恰π×4轴）1e-12 内命中解析期望。注意：EAA 的 0/π 邻域一阶补偿分支在补偿带内精确（π−1e-9 实测偏差 0），反例在其边界带——原矩阵行"候选EAA::angle；须核0、微小角、pi及相对旋转方向"就此结案。`EAA::toRotation3D` 经 `axis()` 的 θ<1e-6 死区一并登记（测试构造改走 Quaternion 路径规避）。
- `PathAnalyzer::pathLength` 单独用于 `pathJointLength`：**已采用**（即本包迁移本体）；catalog boundary 的求和序警示落地为实测——50 节点路径新旧相对差 1.9e-15（末位舍入量级，performance.log），选择序全序结论不变（黄金算例全过）。
- `ird_add_gtest` 换上游测试宏：**否决**（上游 `ADD_RW_GTEST` 非 CONFIG 导出，20 单元同形薄宏为既有形态）——business-specific 保留，TEST-HARNESS capability 保持 candidate（无 adapter 行消费，SUITABILITY 前置不触发）。

PTP 极值缺口（GAP-TRJ-LINEAR-01）：候选2（上游稳定求值入口）**定案为无**——LinearInterpolator.hpp:70-76/86 差分式、Rotation3D 特化为 slerp、RampInterpolator 差分+梯形剖面、SQUAD 仅四元数、Polynomial Horner 同构差分（GAP 文档 §候选2定案，2026-10-25）。候选3（SA-02 最小上游扩展 patch）为唯一剩余路径，**待所有者裁决，维持 blocked**；批准前不改 Ptp.cpp 与矩阵迁移状态；真实 Q 模板路径极值探测 **NOT RUN**。

## 覆盖限制（零模型构造红线，如实登记）

新增测试受 IRD-GATE-AD 业务域零模型构造红线约束（治理自测 `grant-business-construction-rejected` 在案：非 runtime 单元 grant 不得携带 construct-model）——`PlannerReuseTest.cpp` 为**纯数学面**（直连采纳 L1 独立 oracle＋近 π 反例），不构造 WorkCell/Device/会话。由此：

- `pathJointLength` 观测面完整（直连采纳 L1 与独立参考逐位对照，六轴非对称端点）；
- `sampleStraightPath` 的采样计数公式级观测（计数替身夹具）与 `maxAdjacentStep`/`candidatePreferred` 的 RRT 选择序直接 oracle **未建**——其观测面由既有 PlannerAvoidance 黄金算例承载：直连筛查分辨率敏感性（断开红例 l2 证实：L2→0 即漏检中间碰撞，黄金断言真实失败）、同种子重放采纳段逐点相等（黄金算例内置断言）、选择序全序结论回归（黄金算例全过）；
- 多重折叠（多路点路径）求和序的独立参考对照未建（直连采纳为单对）——求和序差异以微基准 1.9e-15 实测与 catalog boundary 登记替代。

该限制属治理红线下的诚实分列，不构成 NOT RUN 冒称；如所有者要求装置级直接 oracle，需先裁决红线适用性。

## 验证与原件

执行于 VS2022 x64、Release/C++17、既有 vcpkg/Qt；测试绝对路径逐进程；不全库重配/重测。命令、XML 与结果绑定见 verification.json、production-*.json（ird-reuse-production/1 ×5：maxAdjacentStep、pathJointLength、sampleStraightPath、search、doInCollision）。

| 项目 | 实际结果 | 原件 |
| --- | --- | --- |
| 最终集成受影响目标构建 | PASS：exit 0 | final-build.log（cmake --build build --target sdurws_ird_trajectory_test sdurws_ird_trajectory_contract_test） |
| 最终全量单元 | PASS：193 passed / 0 failed / 0 skipped（含 PlannerAvoidance 黄金算例回归与新增 2 用例） | final.xml / final-report.json / final.log |
| 独立度量 oracle | PASS：直连采纳 L1 与独立参考逐位对照；L1/L2 算式区分防串线 | final.xml 中 DirectAdoptPathLengthMatchesIndependentOracle_REUSE_T32 |
| 近 π/分支边界反例（保留 2·atan2 的依据） | PASS：EAA 候选两反例实测超 C7（4.4e-11 / 1.6e-10），产品全网格 1e-12 内 | final.xml 中 RotationAngleCounterExampleKeepsAtan2_REUSE_T32 |
| 真实生产扰动 ×3 | 预期 FAIL：L1 折叠断开→1 failed、采样 L2 恒 0→2 failed（黄金直连筛查漏检）、规划期剪枝恒真→1 failed（搜索未果），均 exit 1；各自恢复、重建并复测绿 | disconnected-l1/l2/cll.xml+log＋Planner.mutated-*.cpp＋*-recheck.* |
| 契约测试 | PASS：69 passed / 0 failed / 0 skipped | contract.xml / contract-report.json / contract.log |
| 独立冒烟目标 | PASS：原注册/include/依赖；Planner.cpp 为集成条件源，不声称冒烟覆盖其算法 | smoke-build.log |
| 架构 | PASS：R-1~SA02 全检零命中（rw/pathplanning/PathAnalyzer.hpp 在原 include 面内） | architecture-gates.log |
| 上游原测试目标 | NOT RUN：仅引用其构造/折叠/规划器夹具，未改上游、不重跑无关目标 | catalog 的 tests 引用 |
| 采样计数公式级观测／RRT 选择序直接 oracle | NOT RUN（红线限制）：零模型构造红线下不可建夹具，观测面由黄金算例与断开红例承载（见覆盖限制节） | 无 |
| 1e6 分段上限护栏 | NOT RUN（登记不测）：触发即 ≥10⁶ 样本，生产规模不可执行；属病态输入防溢出面 | 无 |
| 真实 Q 模板路径 PTP 极值 | NOT RUN：GAP 候选2定案仅覆盖检索事实；候选3未获批不实施 | GAP-TRJ-LINEAR-01.md |
| 性能 | 微基准在案：pathL1(50 节点) 相对 +24~41%（绝对 +40ns/次）、maxStep/L2 噪声区；生产面每次 search 仅 K 次键计算，被 RRT 碰撞查询主导。**接受与否交所有者/独立验收裁决，本包不声称性能 PASS** | performance.log / run-performance.cmd / perf.cpp |
| 独立验收 | NOT RUN：实施者不得自签；交所有者另开会话/人工 | 尚无独立记录 |

## 范围限制、外部指纹与回滚

catalog 升级：MATH-METRIC/PATH-PLANNING candidate→verified（review.version＝本包起点 SHA，树内上游文件与现状逐字节一致——verify-review-version.ps1 全 MATCH；suitability 原件 math-metric-suitability.md / path-planning-suitability.md）；TRAJ-LINEAR 保持 candidate、search.conclusion 记录候选2定案；TEST-HARNESS 未动。permissions.json 新增 REUSE-T32-Planner.cpp grant（model+solver/query+borrow，runtime-borrow——本包改动使 Planner.cpp 进入 functionalChanges 全量 AD 检查的必要精确登记）。

范围限制：Ptp.cpp（GAP 候选3待裁决）、Smooth/TimeParam Polynomial 成果（T09，migration §13.3"WP-11 不重写"）、minNormalizedMargin/candidatePreferred 等产品契约 helper（business-specific 审计行）不在生产修改内；逐功能 migrated 表示复用判断/实施记录，不等于整文件全域正确性证明或独验关闭。T13 未独验仍不放行普通 IK/RNE。

外部证据指纹刷新（机械性登记）：本包注册新测试文件改变 `trajectory/CMakeLists.txt` 摘要，T26/T09 三份 production.json（REUSE-T26/production.json、REUSE-T09/production-smooth.json、production-timeparam.json）内该文件的输入绑定与对应矩阵行 evidence 摘要同步刷新——仅指纹，不涉其运行内容（xml/log/结论原样，FRESHNESS 门禁要求绑定当前输入）。

回滚以本包完整提交为边界：代码、矩阵、catalog 条目、grant、凭证与证据（含上述三处指纹刷新）一并回退，不改写历史、不触碰框架/他人改动；回滚即撤销本次度量换核与 catalog 升级，不留旧算法生产备用路径。

本轮进度分列：直接复用迁移 **1 包已实施、待独验**（T32，其余路径轨迹工具）；PTP 缺口候选3 **1 项待所有者裁决**（GAP-TRJ-LINEAR-01 blocked 维持）；独立缺陷 **0 包新增**。不以审计条目数冒充算法迁移进度。
