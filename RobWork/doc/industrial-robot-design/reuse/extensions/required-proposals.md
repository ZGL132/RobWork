# 待检索/验证的最小扩展提案入口

本文件只登记未决能力，不构成扩展批准。每项须另建完整提案并独立取得所有者明确授权。

| ID | 检索起点与已知差异 | 优先适配/候选扩展点 | 阻止事项 |
| --- | --- | --- | --- |
| GAP-IK | rw/invkin/JacobianIKSolver.hpp、IterativeIK.hpp、rwlibs/algorithms/qpcontroller/IKQPSolver.hpp；现有接口未证实支持任务掩码、自由滚转、双容差与真实迭代观测 | 复用原求解器，必要时在原抽象增加任务误差/行选择/取消观测 | 不新写 DLS/约束差分核心，不用后过滤冒充仅位置求解 |
| GAP-RNE | rwsim/util/RecursiveNewtonEuler.cpp::validate/solve/solveMotorTorques；SerialDevice、DOF+1、移动关节广义力投影、Body/关节坐标与固定段 | 优先 solve 的 Wrench＋适配投影；物性转换经 rw 数学 API；上游缺陷最小 patch | 不重写 RNE，不截掉末项就宣称正确，不重复工具物性 |
| GAP-FD | rwsim/simulator/PhysicsEngine.hpp、dynamics/RigidDevice.hpp、rwphysics/RK4Integrator.hpp；刚体 RK4 并非通用关节 ODE | 实测现有 PhysicsEngine 后端 load/initPhysics/step/力控制；不足时扩展原引擎 | 不以自持 RK4 代替 DYN-05 的 RobWorkSim 场景 |
| GAP-TRJ | rw/trajectory/CubicSplineFactory.hpp、RampInterpolator.hpp、ParabolicBlend.hpp | 满足契约时适配；高阶能力不足时在 Interpolator/Trajectory 原抽象最小扩展 | 不把三次替换五次/七次，不放宽连续性、jerk 和数值容差 |

每份实际提案必须记录：检索命令及输出证据、上游版本/哈希、具体能力缺口与可复现实例、候选扩展文件/符号、替代方案、API/ABI/数值/性能/构建/测试/许可证影响、最小 patch 范围与默认关闭/回滚策略、所有者逐项批准。搜索范围至少覆盖公共头、实现、测试、示例、构建注册；不能只凭命名未命中断言“库不好用/库没有”。
