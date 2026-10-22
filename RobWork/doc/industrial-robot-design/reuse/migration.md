# 上游复用迁移计划（2026-10-22）

## 1. 审计基点与结论

基点 b12e8503cc1c2a1acc040f506842a02e126b9515；根目录内还有 RobWork/ 子树。Fk.cpp 自持 FK/Jacobian；Ik.cpp 自持 DLS/约束差分；InverseDynamics.cpp 自持 Vec3/Mat3/RNEA；ForwardDynamics.cpp 自持质量阵/消元/RK4。CouplingMath.hpp、DescriptionValidator.cpp、MappingCore.cpp 各有 SVD；CartesianLine.cpp 手写 SLERP。Planner.cpp 与 policy/runtime 的 RRT/碰撞适配保留。全文件入口为 inventory.json，未完成语义核查如实标记 audit-required。

## 2. 功能与复用映射

| 能力 | 当前位置（industrialrobot/ 下） | 上游文件/API | 边界 |
| --- | --- | --- | --- |
| FK/Jacobian | kinematics/src/Fk.cpp | RobWork/src/rw/models/Device.hpp：setQ/baseTframe/baseJframe | q_auth-offset、TCP、参考系、固定段 |
| 普通 IK | kinematics/src/Ik.cpp | RobWork/src/rw/invkin/JacobianIKSolver.hpp：DLS/solve/solveLocal | 双容差、取消、统计、稳定解集 |
| SVD/伪逆 | modeling/src/CouplingMath.hpp；runtime/src/DescriptionValidator.cpp；drivetrain/src/MappingCore.cpp | RobWork/src/rw/math/LinearAlgebra.hpp：svd/pseudoInverse | 精度参数显式，保留业务病态阈值 |
| 逆动力学 | dynamics/src/InverseDynamics.cpp | RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.hpp：solve/setGravity/setPayload/setEnvironment | RigidDevice/DOF/Body 对齐、物性参考系、载荷与摩擦 |
| 正动力学 | dynamics/src/ForwardDynamics.cpp | RobWorkSim/src/rwsim/simulator/PhysicsEngine.hpp：load/initPhysics/step | 真实任务私有场景、力控制、后端、步长与初态 |
| 插值 | trajectory/src/Ptp.cpp；CartesianLine.cpp | rw/trajectory/LinearInterpolator.hpp；rw/math/Quaternion.hpp：slerp | 端点、最短弧、异常和零长段 |
| 旋转/变换 | runtime/src/BaseWorldTransform.cpp；modeling/src/CanonicalBridge.cpp/DhConvert.cpp/RpyMath.hpp；kinematics/src/Sampling.cpp | rw/math/EAA.hpp、RPY.hpp、Transform3D.hpp | 坐标约定、π/零角、DH 类型 |
| 高阶轨迹 | trajectory/src/Smooth.cpp/TimeParam.cpp | rw/trajectory/CubicSplineFactory.hpp、RampInterpolator.hpp、ParabolicBlend.hpp（候选） | 不把三次当五次/七次，不放宽 jerk/C²/C³ 要求 |
| 规划/碰撞 | trajectory/src/Planner.cpp；policy/runtime | RRTPlanner、QSampler、CollisionDetector | 保留 policy 端口与已复用计算 |

上述上游路径以仓库根下 RobWork/ 为前缀；catalog.json 保存完整路径、具体符号和规范化内容摘要，review-checklist.md 定义真实调用证据。

## 3. 优先级与终态

P0：先修规则/构建/冲突约束，替换 FK/Jacobian、普通 IK、RNEA 和不符合 DYN-05 的正动力学路径。P1：消除重复 SVD/旋转/SLERP/线性插值。约束 IK、高阶轨迹、DH 拟合和固定段模型在证据不足时先提案，不扩写内核。保留业务过滤、排序、工况覆盖、包络与可信度，不保留旧生产内核作隐藏兜底。

## 4. 架构与兼容

runtime 复用已编译 WorkCell/SerialDevice/DynamicWorkCell/RigidDevice，统一关节与 TCP 映射；不从 CanonicalModel 在业务域再建链。单元私有适配器连接现有 Evaluator 门面与上游 API；通过工厂/版本化能力接口补齐 IKinRuntimeView，保持 DTO/命令/错误语义，ABI 变化显式版本化。State/求解器/引擎任务私有，快照只读。

## 5. 文档覆盖

根与分区 AGENTS、REQUIREMENTS、ARCHITECTURE、DETAILED-DESIGN、20 单元卡、DTB、契约编译/验收/执法文档、README、六阶段/Qt/Python Skills、PR 模板均链接政策。旧 O-40“rw 无 SVD”与 P-DYN-7“自持 RK4/禁引擎”登记为已被本治理决定取代的历史，不修改历史验收证据或将旧 done 解释为复用合规。

## 6. 门禁与约束

validate-reuse 与负夹具接入本地/CI/ird_gates；新增/改动产品文件须精确摘要与复用方式；扩展须提案＋范围明确的批准；库存不能删项移基点；最终 RequireMigrationComplete 必须零债务零未审计。启发式扫描无法证明所有语义重复，独立审查是必需检查。

## 7. 顺序、范围与回滚

| 任务 | 范围/动作 | 前置 | 回滚 |
| --- | --- | --- | --- |
| GOV-T21 | 文档、约束、门禁、CI、审查规则及库存；零算法修改 | 本次所有者指令 | 完整任务 revert |
| REUSE-T01 | 全 20 单元逐功能复用审计、最小扩展提案、API/构建/运行探测 | GOV-T21 独立 pass 记录 | 独立审计任务 revert |
| REUSE-T02 | runtime 能力/线程私有 State/上游链接及数学薄封装设计、实现与真接线 | T01 完整审计＋相关提案单独批准 | 适配任务 revert |
| REUSE-T03 | FK/Jacobian/普通 IK 替换；约束 IK 按批准提案 | T02 | 分能力 revert |
| REUSE-T04 | RNE、真实 RobWorkSim 正动力学场景 | T02＋动力学差异验证/批准 | 分能力 revert |
| REUSE-T05 | trajectory/modeling/drivetrain 及其余单元重复实现清理 | T03/T04＋高阶能力批准 | 分能力 revert |
| REUSE-T06 | 移除旧核心、完整回归/性能/包验证、最终零债务验收 | T05 | 发布制品/完整提交 revert |

后续 planned 契约必须按审计结果细化精确 allowedFiles、verify、acceptance，不能把此表当作任意修改许可。各次代码、测试、CMake、文档同提交并立即推送；不自行合入。

## 8. 测试与验收

复用上游 SerialDeviceTest/KinematicsTest/JacobianIKSolverTest/LinearAlgebraTest/TrajectoryFactoryTest；新增适配边界与真实端口集成。包含非 Z 轴、offset、移动/固定/连续关节、多 TCP、倒挂/侧装、载荷事件、零/缺失物性、摩擦唯一计入、奇异/病态、取消/错误、轨迹连续性与 jerk。解析 oracle 独立于新旧内核；不通过重产旧黄金值掩盖差异。双模式日志＋gtest XML＋ird-test-report；GUI 按 VS x64/windows/逐 exe 规则。治理 PowerShell 自测用 JUnit XML＋JSON，单独计数。

## 9. 风险、缺口与扩展停止条件

上游 RNE validate 限定 SerialDevice/转动或移动关节；solveMotorTorques 返回 DOF+1 且按 torque 投影，移动关节不能机械调用。Body 参考系与固定段需验证，载荷不可重复。RK4Integrator 是刚体接口，不是关节 ODE 直接替代；PhysicsEngine 力控制/后端/确定性须实测。约束 IK 双容差、取消/统计和高阶时间律能力尚未验证。以上逐项写入 extensions/required-proposals.md，不能因此另写核心或降低需求。

## 10. 分阶段完成定义

A 文档/约束/门禁经独立冻结 SHA 验收后，B 审计与适配能力探测，C 分能力替换，D 清理与最终验收。治理 pass 只证明规则可执行，不证明现有算法已合规；算法扩展与合入决策仍归所有者。远程分支保护必须实际读取验证，未配置不得声称已强制。
