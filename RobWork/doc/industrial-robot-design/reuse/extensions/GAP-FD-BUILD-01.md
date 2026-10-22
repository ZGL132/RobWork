# GAP-FD-BUILD-01：优先补现成ODE后端的构建供给

状态：**待供给确认 / 未批准修改依赖环境，未申请新FD核心**。DYN-05要求RobWorkSim正动力学场景，不能用自持质量阵/消元/RK4满足。检索记录与源码/target摘要见 `proposal-search.json`；当前产品仍pending，未进入T04-B。

## 1. 证据与真实缺口

- `rwsim/simulator/PhysicsEngine.hpp` 已有load/initPhysics/step/resetScene/exitPhysics；目标 `sdurwsim`。
- `rwsimlibs/ode/ODEVelocityDevice.cpp::update` 的Force分支确实调用ODEJoint::setForce，`rwsim/dynamics/RigidDevice.hpp` 已有setMotorForceTargets。**优先验证现成力控制路径，不申请重写积分器。**
- `rwsimlibs/ode/CMakeLists.txt` 注册真实目标 `sdurwsim_ode`，按RWSIM_HAVE_ODE供给。现有 `gtest/ode/ODEControlTest.cpp`、ODESimpleTest、`example/cpp/ex-ode-simloop.cpp` 和gtest/CMake接线可复用；示例的movePTP不是DYN-05的力输入测试，不能改名当通过。
- 本机既有集成缓存BUILD_sdurwsim_ode=OFF、ODE_DIR/ODE_INCLUDE_DIR/ODE_LIBRARY_TMP=NOTFOUND。只证明本次环境未供给，**不证明原库无FD**。
- Bullet::addDevice实测抛未实现；RWPhysics对RigidDevice明示不支持，不把刚体RK4Integrator接口冒充关节ODE。以上失败只排除已测路径，不排除整个RobWorkSim。

## 2. 最小供给方案与权限

先由所有者确认同一vcpkg渠道的ODE版本/精度/运行库供给，以及Windows x64/Release目标可用性；不得vendor ODE、网络下载另装或修改只读vcpkg树规避。当前仓库的安装目录未携带vcpkg状态库，不能据此编造锁定版本；基线、port、triplet与二进制来源须在正式供给时记录。若该渠道当前无适用port，返回提交独立依赖提案，不能自行换渠道。

获准后在隔离构建树配置现有项目，显式启用 `BUILD_sdurwsim_ode=ON`，复用原FindODE与导出目标，不手拼lib或改上游CMake。先运行原 `sdurwsim_ode-gtest` 受影响用例，再在既有审计探测目标增加单自由度力输入→step验证；如原gtest资产/依赖不完整如实标记，不以编译通过代替实际运行。是否需要上游patch须以该轮失败证据另提，本提案不预授权修框架。

runtime负责真实DWC/RigidDevice、初始State和引擎会话物化；业务只通过受控任务会话提交SI广义力/状态与步长，不能另建WorkCell或跨worker传借持句柄。引擎虽然load接口可变，操作只允许在私有物化副本/明确允许的会话，不获得权威模型写权；T02须验证其保活及版本映射。diagnostics/evidence职责不下放给ODE适配层。

## 3. 需要验证的适配差异

控制输入为N·m（转动）/N（移动），位置rad/m、速度rad/s或m/s、时间s；axis、zeroOffset、固定段、惯量参考系、重力、工具/载荷和摩擦逐项对齐，同一物性不得重复计入。resetScene会清速度，非零初速度必须经已公开设备/刚体状态API验证恢复，不能在reset前赋值后宣称有效。禁用隐藏位置控制器影响；每个step前设置Force targets并核实际motor mode。

DYN-05参考样本的控制保持/事件边界/步长/精度属于产品契约；ODE子步/迭代参数不保证与旧RK4数值轨迹一致。先以解析单摆/移动质量及RNE反算进行收敛和能量检查，再验证复杂链、接触是否可关闭、确定性、取消/引擎资源回收、两任务隔离。正常结果及异常须映射原有稳定码，不吞错；若算法误差不能满足原容差，不放宽需求，返回缺口提案。

## 4. 影响与替代/回滚

影响仅先为构建供给、审计实验和依赖证据；通过后T04-B才替换 `dynamics/src/ForwardDynamics.cpp` 中evalAcceleration、质量阵/消元及RK4，保留请求/结果/工况/误差统计。产品契约/缓存身份变化须显式版本迁移。ODE许可证及同渠道包版权、双精度选项、MSVC运行库匹配在供给时核实，不预先填写未知版本。

首选现成ODE；Bullet/RWPhysics保留已查失败证据，原RNE-Wrench只用于逆动力学与独立一致性验证，不能充当FD引擎；拒绝产品自写积分器、新建通用动力学框架或生产fallback。新后端在任务分支启用，失败撤销该阶段接线与构建选项，已归档证据保留。未获供给确认、未实跑通过时，T04-B继续阻塞，其他不依赖它的审计可继续。
