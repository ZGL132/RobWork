# REUSE-T26 / WP-11：CartesianLine 上游四元数插值

基线 `507db46bba838bb170e1fbfaf5784a389ad3790d`，`redesign-main` 本地串行实施。当前为实施自查完成、可交独验，独验 **NOT RUN**，任务保持 ready，未登记 closed，不关闭其他包债务。

## 需求、删除与适配边界

TRJ-02/TRJ-06 要求直线位置、姿态最短弧、端点还原和原 IK 连续性/取消/失败协议。删除生产手写 slerp、safeAcos、QuatComponents、normalizedQuat，以及无调用方 quatDot/rotationAngleBetween；删除正交性检查的局部 n2/dot 和参考轴手写点积。无隐藏 fallback。旧核心仅在隔离 perf-old.cpp 中作为历史性能对照，不进入产品目标。

直接复用 `RobWork/RobWork/src/rw/math/Quaternion.hpp` 的构造、normalize、slerp、toRotation3D，及 `Vector3D.hpp::dot`；沿用 `sdurw_math`、原 trajectory 目标、gtest/testkit/报告脚本。矩阵构造本身不归一化，故保留两次上游 normalize。Quaternion 的值语义不取得模型/State 所有权，无新增依赖、公共接口、CMake、权限、facade 或缓存。

数据流：原请求/planCartesianLine → interpolateCartesianLine → 两个局部 Quaternion → 上游 slerp → Rotation3D → 原位姿返回。trajectory 保留 s∈[0,1]、s=0/1 直接返回、位置凸组合、正交性/参考轴 1e-9 产品阈值、采样/分支策略和素材。runtime 仍是模型真值 owner；FK/IK 仅经原端口，不实施 IK，不依赖未独验 T13。

保留原因：LinearInterpolator 采用位置差分式，极值位置语义与原凸组合不同（既有 GAP-TRJ-LINEAR-01），不为姿态复用降低长度契约；正交性是原三列点积阈值，不强接额外行列式/默认容差。qSummary、singularComparison、makeFailure 仅组织 FailedSegmentRecord/警告素材，正式诊断仍由 FailureLocate.cpp 经 IDiagnosticFactory::create 生成；不另造 envelope、码表或模型缓存。采样计划/分支选择属于产品离散与稳定排序策略，不冒充上游算法。测试 Rodrigues、单轴 atan2 和字面期望保留独立 oracle 价值，端口替身只在六类边界。

## 验证与原件

执行于 VS2022 x64、Release/C++17、既有 vcpkg/Qt，测试绝对路径且逐个进程；不全库重配/重测。命令、依赖摘要、XML 和结果绑定见 [verification.json](verification.json)、[production.json](production.json)。

| 项目 | 实际结果 | 原件 |
| --- | --- | --- |
| 旧生产＋新增正确性用例 | PASS：2 passed / 0 failed / 0 skipped；纯重构不伪造红例 | baseline.xml / baseline.log / baseline-build.log |
| 最终集成受影响 test/contract 目标构建 | PASS：exit 0 | final-build.log |
| 最终单元/连续性 | PASS：23 passed / 0 failed / 0 skipped | final.xml / final-report.json / final.log |
| 独立旋转闭式 | PASS：3初态×5轴×7角×7采样=735个位姿，原矩阵1e-12容差；位置与位级重复确定性 | final.xml 中 SlerpIndependentAxisOracle_REUSE_T26 |
| 上游与边界对照 | PASS：微角、精确π/跨半周、极值位置、非法s；不修改EAA或推广其局部容差 | final.xml 中 SlerpUpstreamAndBoundary_REUSE_T26 |
| 真实生产扰动 | 预期 FAIL：0 passed / 3 failed / 0 skipped，exit 1；将 qa.slerp(qb,s) 改为 qa.slerp(qb,0)，之后恢复、重建并得到最终PASS | disconnected.xml / disconnected.log / CartesianLine.mutated.cpp |
| 直接消费者契约 | PASS：16 passed / 0 failed / 0 skipped；IK端口/AT06既有装配，不代表真实IK算法独验 | contract.xml / contract-report.json / contract.log |
| 既有独立冒烟目标 | PASS：原注册/include/依赖；CartesianLine 为集成条件源，不声称冒烟覆盖其算法 | smoke-build.log |
| 架构 | PASS：11类零命中 | architecture-gates.log |
| 上游原测试目标 | NOT RUN：仅引用其构造/转换及插值夹具，未改上游、不重跑无关目标 | catalog 的 tests 引用 |
| 内存分配计数、线程压力/整机端到端性能 | NOT RUN：未引入共享状态/缓存/模型，不以微基准冒充这些结论 | 无 |
| 独立验收 | NOT RUN：实施者不得自签；交所有者另开会话/人工 | 尚无独立记录 |

## 性能、适用性及回滚

[performance.log](performance.log)：MSVC /O2，同轮200000次×四场景×三轮，交替先后；old/new 摘自基线与最终生产函数。复现脚本 [run-performance.cmd](run-performance.cmd)，首次缺Eigen include的编译失败留在 perf-build-initial.log，补既有 include 后成功，无依赖渠道变更。

同姿态、微小角更快；一般角旧0.0544–0.0592µs/次、新0.0596–0.0633µs，同轮慢约6%–10%；近π旧0.0615–0.0638µs、新0.0632–0.0643µs，慢约0.3%–2.7%。所有者本轮明确答复：**“接受本次具体开销，交独验”**。该接受仅覆盖此微基准开销，不外推其他能力，不等于独验PASS或整机加速。API无共享可变状态；线程压力测试未执行。

范围限制：其余 trajectory 能力、T09既有多项式成果、LinearInterpolator极值缺口和历史采样极端容量问题不在本次生产修改内；逐功能 migrated 表示复用判断/实施记录，不等于整文件全域正确性证明或独验关闭。T13未验仍不放行普通IK/RNE。没有新增独立产品缺陷包。

回滚以本包完整提交为边界，代码、两类凭证和catalog条目一并回退，不改写历史、不触碰框架/他人改动；回滚即撤销本次迁移，不留旧算法生产备用路径。后续生产更改须重判相关证据输入。

本轮进度分列：直接复用迁移 **1包已实施、待独验**（T26）；必要配套修复 **0包新增生产交付**（T25仍只是受阻候选）；独立缺陷 **0包新增**。不以工作包编号或审计条目数冒充算法迁移进度。
