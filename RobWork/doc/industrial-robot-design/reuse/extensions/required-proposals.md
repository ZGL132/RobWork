# 待检索/验证的最小扩展提案入口

本文件只登记未决能力，不构成扩展批准。每项须另建完整提案并独立取得所有者明确授权。

GAP-IK/RNE/FD/TRJ 当前均为待检索/验证，不表示已证实上游无能力。按 [audit-contract §2](../audit-contract.md#2-需求驱动的双向审计) 合并上游 review 与项目功能审计：优先直接 API 和最薄适配，实际不能满足原需求时才提最小上游扩展。SA-19、宿主例外、business-specific 分类和本轮文档批准均不能批准新核心；不得用“业务算法＋架构例外”绕过 H1/H2/H6。

| ID | 检索起点与已知差异 | 优先适配/候选扩展点 | 阻止事项 |
| --- | --- | --- | --- |
| GAP-IK | JacobianIKSolver/IterativeIK、ParallelIKSolver::Target、IKQPSolver/QPController。公共 API 实测：同一帧链的单 ParallelLeg 支持 XYZ 掩码；但 Target 的 EAA 分量与产品参考系下 ZYX Euler 分量不同，存在可复现反例；JacobianIKSolver::solveLocal 在不可达半周姿态可误报收敛。迭代数、取消和失败 State 可观测性仍待核 | 先分完整位姿、仅位置、Euler 部分约束、自由工具滚转分别适配；原返回值须独立残差复核。必要扩展应在原迭代抽象增加误差/观测接口，另提具体方案 | 不把有掩码说成无掩码；不把 EAA 行选择冒充 Euler 约束；不新写 DLS/约束差分核心，不以后过滤完整位姿求解冒充仅位置求解 |
| GAP-RNE | RecursiveNewtonEuler::validate/solve/solveMotorTorques；单移动关节实测 solve 输出 DOF+1 个 Wrench，质量2 kg、加速度3 m/s²、重力−9.81 m/s²时 force.z=25.62 N，而 solveMotorTorques 返回0。混合轴、固定段、Body 坐标仍需覆盖 | 优先 solve 的 Wrench＋按关节类型投影；物性转换经 rw 数学 API。该候选可在薄适配内处理，不能预先认定必须 patch | 不重写 RNE，不截掉末项就宣称正确，不重复工具物性 |
| GAP-FD | PhysicsEngine Factory 不只有 Bullet/ODE，也有 RWPhysics；RWSimulator 对 RigidDevice 明示不支持。Bullet initPhysics 的 device 分支被 #if 0 关闭，addDevice 实测抛未实现；ODEVelocityDevice::update 的 Force 分支实际调用 setForce，是有效候选；本机构建 ODE_DIR/库均未找到 | 优先验证现成 ODE 后端的 load/initPhysics/force→step、初态和精度，先补构建供给事实；不足再提最小原引擎扩展 | 不以 Bullet 已链接冒充机器人力控制已接通；不以未安装 ODE 宣称上游无 FD；不以自持 RK4 代替 DYN-05 场景 |
| GAP-TRJ | 除 CubicSplineFactory/RampInterpolator/ParabolicBlend，还有 LloydHaywardBlend 与通用 Polynomial/PolynomialND。实测 Polynomial 七次及三阶导数可用，LloydHaywardBlend 的静止端点五次形式可用；任意端导数、时间尺度和生产完整契约尚待核 | 优先复用 Polynomial::evaluateDerivatives 及 Interpolator 薄封装，沿用原连续性/jerk 要求；不因没有命名七次轨迹类就提新核心 | 不把三次替换五次/七次，不放宽连续性、jerk 和数值容差 |

每份实际提案必须记录：检索命令及输出证据、上游版本/哈希、具体能力缺口与可复现实例、候选扩展文件/符号、替代方案、API/ABI/数值/性能/构建/测试/许可证影响、最小 patch 范围与默认关闭/回滚策略、所有者逐项批准。搜索范围至少覆盖公共头、实现、测试、示例、构建注册；不能只凭命名未命中断言“库不好用/库没有”。

本次公共 API 探测与复现命令见 [REUSE-T01 实验说明](../../traceability/reuse/REUSE-T01/README.md)。通过项包括对限制的预期断言，仅是审计证据，不是生产复用正向证据或扩展批准；逐功能决策仍以 `functions/` 为准。
