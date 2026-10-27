# 待检索/验证的最小扩展提案入口

本文件只登记未决能力，不构成扩展批准。每项须另建完整提案并独立取得所有者明确授权。

基础平台后续有界提案（REUSE-T13首包之后，**三项有界范围已于2026-10-23分别获所有者批准，详见各提案授权节；独验仍待完成**）：[GAP-CORE-C4-01](GAP-CORE-C4-01.md) 保持非对称C4契约的溢出修正；[GAP-DIAG-RETENTION-01](GAP-DIAG-RETENTION-01.md) 原日志轮转的保留总数边界修正；[GAP-CORE-FNV-01](GAP-CORE-FNV-01.md) 先验证已安装成熟128位整数运算的最薄适配，core必要私有头依赖包含在本次候选A批准范围。既有红例/检索直接引用，不重新清查全库。三项独立决策，未扩大到其他提案，也不修改上游或接管业务算法。当前结果：C4 缩放候选被独立精确 oracle 否决，已撤回，F-966 仍 OPEN（见 traceability/reuse/REUSE-T14）；FNV 与日志已实施自查、待独验，批准不等于验收。

C4当前修订为[GOV-T26／R3成熟原语候选](../../traceability/reuse/REUSE-T14/revision-r3/README.md)：原A失败、R2未批且哨兵分支文实不符；R3有界生产实施已获所有者明确批准；[REUSE-T21](../../traceability/reuse/REUSE-T21/README.md)已完成Compare单点适配、精确oracle与受影响消费者自查。最终性能已获本包明确接受、独验NOT RUN；第三方表达债务保留，F-966仍OPEN。

已形成的具体草案：[GAP-IK-01](GAP-IK-01.md)（原求解器任务行/可观测性最小扩展候选）与 [GAP-FD-BUILD-01](GAP-FD-BUILD-01.md)（先供给现成ODE后端，尚不申请FD新核心；2026-10-25 REUSE-T31 定向探测后修订为可裁决供给提案，阶段 B 待批准）；版本、源摘要、首轮7条与增量检索在 [proposal-search.json](proposal-search.json)。两项均未批准，不能因自动继续指令自行实施。其他差异继续检索，不能将本文件所有入口批量升级为已证实缺口。

GAP-IK/RNE/FD/TRJ 当前均为待检索/验证，不表示已证实上游无能力。按 [audit-contract §2](../audit-contract.md#2-需求驱动的双向审计) 合并上游 review 与项目功能审计：优先直接 API 和最薄适配，实际不能满足原需求时才提最小上游扩展。SA-19、宿主例外、business-specific 分类和本轮文档批准均不能批准新核心；不得用“业务算法＋架构例外”绕过 H1/H2/H6。

| ID | 检索起点与已知差异 | 优先适配/候选扩展点 | 阻止事项 |
| --- | --- | --- | --- |
| GAP-IK | JacobianIKSolver/IterativeIK、ParallelIKSolver::Target、IKQPSolver/QPController。公共 API 实测：同一帧链的单 ParallelLeg 支持 XYZ 掩码；但 Target 的 EAA 分量与产品参考系下 ZYX Euler 分量不同，存在可复现反例；JacobianIKSolver::solveLocal 在不可达半周姿态可误报收敛。迭代数、取消和失败 State 可观测性仍待核 | 先分完整位姿、仅位置、Euler 部分约束、自由工具滚转分别适配；原返回值须独立残差复核。必要扩展应在原迭代抽象增加误差/观测接口，另提具体方案 | 不把有掩码说成无掩码；不把 EAA 行选择冒充 Euler 约束；不新写 DLS/约束差分核心，不以后过滤完整位姿求解冒充仅位置求解 |
| GAP-RNE | RecursiveNewtonEuler::validate/solve/solveMotorTorques；单移动关节实测 solve 输出 DOF+1 个 Wrench，质量2 kg、加速度3 m/s²、重力−9.81 m/s²时 force.z=25.62 N，而 solveMotorTorques 返回0。混合轴、固定段、Body 坐标仍需覆盖 | 优先 solve 的 Wrench＋按关节类型投影；物性转换经 rw 数学 API。该候选可在薄适配内处理，不能预先认定必须 patch | 不重写 RNE，不截掉末项就宣称正确，不重复工具物性 |
| GAP-FD | PhysicsEngine Factory 不只有 Bullet/ODE，也有 RWPhysics；RWSimulator 对 RigidDevice 明示不支持。Bullet initPhysics 的 device 分支被 #if 0 关闭，addDevice 实测抛未实现；ODEVelocityDevice::update 的 Force 分支实际调用 setForce，是有效候选；本机构建 ODE_DIR/库均未找到。**2026-10-25 REUSE-T31 定向实核收窄**：vcpkg 渠道存在 ode 0.16.6 port（BSD-3-Clause OR LGPL-2.1-or-later）而未安装；力驱动直连 API 链行号级核验完成（RigidDevice::setMotorForceTargets→ODEVelocityDevice::update Force 分支 setForce→ODESimulator::addDevice 建 ODEVelocityDevice）；Bullet/RWPhysics/RK4Integrator 负面证据落位；部署握手＝FindODE config 包路径与 vcpkg 布局兼容，须规避 RobWorkSimSetup WIN32 的 git 网络拉取回退 | 优先验证现成 ODE 后端的 load/initPhysics/force→step、初态和精度；供给提案已可裁决（[GAP-FD-BUILD-01](GAP-FD-BUILD-01.md) §1a/§2），阶段 B（安装与隔离树验证）属新依赖启用待所有者批准，批准前不实施、不解除 T04-B 阻断 | 不以 Bullet 已链接冒充机器人力控制已接通；不以未安装 ODE 宣称上游无 FD；不以自持 RK4 代替 DYN-05 场景；不把 RobWorkSimSetup 的 WIN32 git 拉取当合法供给渠道 |
| GAP-TRJ | 除 CubicSplineFactory/RampInterpolator/ParabolicBlend，还有 LloydHaywardBlend 与通用 Polynomial/PolynomialND。实测 Polynomial 七次及三阶导数可用，LloydHaywardBlend 的静止端点五次形式可用；任意端导数、时间尺度和生产完整契约尚待核 | 优先复用 Polynomial::evaluateDerivatives 及 Interpolator 薄封装，沿用原连续性/jerk 要求；不因没有命名七次轨迹类就提新核心 | 不把三次替换五次/七次，不放宽连续性、jerk 和数值容差 |

每份实际提案必须记录：检索命令及输出证据、上游版本/哈希、具体能力缺口与可复现实例、候选扩展文件/符号、替代方案、API/ABI/数值/性能/构建/测试/许可证影响、最小 patch 范围与默认关闭/回滚策略、所有者逐项批准。搜索范围至少覆盖公共头、实现、测试、示例、构建注册；不能只凭命名未命中断言“库不好用/库没有”。

第二批未决入口还有：`modeling/src/DhConvert.cpp` 的LM/数值差分/消元与秩判定；`kinematics/src/SeedMix.hpp` 和 `optimization/src/EvaluatorPorts.cpp` 的64位任务私有随机流/LHS；`core/src/Digest.cpp` 的SHA-256；`testkit/src/JsonLite.cpp` 与 `io/src/Json.cpp` 的JSON及 `io/src/Csv.cpp` 同副本UTF-8校验。它们是后续检索/提案入口，不是已证实缺口或保留许可。上游sandbox优化目标默认关闭，当前API/安装适用性待实测；数学Random和HyperSphere不能直接承诺旧样本身份与顺序；JSON类型/规范化和跨平台摘要须继续查同渠道既有依赖。来源及逐项差异见功能矩阵，真实检索输出见实验目录 `batch2-search.json`。

本次公共 API 探测与复现命令见 [REUSE-T01 实验说明](../../traceability/reuse/REUSE-T01/README.md)。通过项包括对限制的预期断言，仅是审计证据，不是生产复用正向证据或扩展批准；逐功能决策仍以 `functions/` 为准。

`probe-r3`进一步确认：ParallelIK预算耗尽也可能返回in-bounds候选，所有候选必须独立验证任务残差；不能把非空vector作为收敛。标准DH可直接调用但Craig约定不同；EAA反向对齐默认轴需适配产品滚转约定；Polynomial支持非零端导数与时间尺度，不需要另写五/七次数值求值核心。这些事实收窄后续扩展范围，不批准扩展或关闭生产路径。

B30形成待批准 [GAP-DIGEST-01](GAP-DIGEST-01.md)：先核同渠道成熟摘要依赖，CNG仅作Windows独立参照。原SHA测试通过不消除复用债务；F-965两公开摘要类型hash反例真实失败，均不授权T01修算法。

REUSE-T08新增待确认 [GAP-EAA-01](GAP-EAA-01.md)：原微小角矩阵容差下EAA候选不适用，正确ABI下Quaternion::ln在零/极小角非有限；RPY正向适用不表示完整旋转能力已迁移。保留原容差、产品和上游，不实施未批扩展。

2026-10-23所有者已逐项批准GAP-EAA-01方案A，实施跟踪REUSE-T10；仅A获批，尚未独验/接受性能/关闭生产债务，其他缺口批准状态不变。

最新有效决议（2026-10-23）：所有者已撤销GAP-EAA-01方案A，恢复上游EAA及测试原状，改为根据原库实测结果局部调整旋转转换误差阈值。REUSE-T10不再执行，原FAIL/批准/测量留史。不能沿用上句历史授权或接受状态。其他缺口不受影响。
