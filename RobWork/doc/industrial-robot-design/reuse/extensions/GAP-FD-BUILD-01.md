# GAP-FD-BUILD-01：优先补现成ODE后端的构建供给

状态：**供给定向探测完成（REUSE-T31，2026-10-25），可裁决供给提案；阶段 B（安装与隔离树验证）属新依赖启用，未获所有者批准不实施**。DYN-05要求RobWorkSim正动力学场景，不能用自持质量阵/消元/RK4满足。首轮检索与源码/target摘要见 `proposal-search.json`（REUSE-T01 批），本次定向探测的命令与输出见同文件增量批（REUSE-T31 批）及 [REUSE-T31 原始记录](../../traceability/reuse/REUSE-T31/README.md)。当前产品仍pending，未进入T04-B；`tasks/REUSE-T04.json` 保持 planned。

## 1. 证据与真实缺口（2026-10-25 定向实核，行号为当日 HEAD e74886dfc 现状）

- `rwsim/simulator/PhysicsEngine.hpp` 纯虚接口齐备：step(:99)/resetScene(:105)/initPhysics(:110)/exitPhysics(:115)/getTime(:120)；目标 `sdurwsim`。
- 力驱动直连 API 链完整（全部只读实核）：`RigidDevice::setMotorForceTargets` 同时写目标并置 modes=1（Force）（RigidDevice.cpp:234）；`setJointVelocities` 经 State 写速度并 clamp 速度限位（:260）——resetScene 清速后经它恢复初速的语义待阶段 B 实测；`ODEVelocityDevice::update` 每步读回 motor modes（ODEVelocityDevice.cpp:113），Force 分支实际调用 `ODEJoint::setForce`（:166）；`ODESimulator::addDevice` 对 RigidDevice 构造 ODEVelocityDevice（ODESimulator.cpp:1214）。**优先验证现成力控制路径，不申请重写积分器。**
- `rwsimlibs/ode/CMakeLists.txt` 注册真实目标 `sdurwsim_ode`，`:19 set(DEFAULT ${RWSIM_HAVE_ODE})` 按 ODE 供给决定默认开关；`rw_subsys_option` 生成 `BUILD_sdurwsim_ode` 缓存开关。现有 `gtest/ode/ODEControlTest.cpp`（:45 ODEControlDeviceTest——movePTP 速度控制）、ODESimpleTest（落体/托盘稳定）与 `gtest/CMakeLists.txt:63-73`（`if(TARGET sdurwsim_ode)` 门控 sdurwsim_ode-gtest）可复用；示例的movePTP不是DYN-05的力输入测试，不能改名当通过；既有 gtest 资产中无关节力输入用例，探针用例须新设计（:73 起注释掉的 _FC 变体不构成可用测试）。
- 本机既有集成缓存 BUILD_sdurwsim_ode=OFF（build/CMakeCache.txt:240）、ODE_DIR/ODE_INCLUDE_DIR/ODE_LIBRARY_TMP=NOTFOUND（:600,603,606）、VCPKG_MANIFEST_MODE=OFF/x64-windows（:942,953）。只证明本次环境未供给，**不证明原库无FD**。
- Bullet::addDevice 实测抛未实现（BtSimulator.cpp:585-588 `RW_THROW ("BtSimulator (addDevice): not implemented yet!")`）；RWPhysics 对 RigidDevice 打印 "NO SUPPORT FOR RIGID DEVICES!"（RWSimulator.cpp:132-135）；`rwsim::simulator::RK4Integrator` 是单 `RigidBody` 的 `BodyIntegrator`（RK4Integrator.hpp:22-36，构造收 RigidBody*），非关节 ODE 求解器——不把刚体RK4Integrator接口冒充关节ODE。以上失败只排除已测路径，不排除整个RobWorkSim。

## 1a. 供给事实（本次实核补齐 §2 原不确定项）

- **vcpkg 渠道存在适用 port**：`vcpkg/ports/ode/vcpkg.json`＝ode 0.16.6，许可 `BSD-3-Clause OR LGPL-2.1-or-later`，`supports: !uwp`，依赖 vcpkg-cmake/vcpkg-cmake-config（host）。portfile 关闭 DEMOS/TESTS（`-DODE_WITH_DEMOS=OFF -DODE_WITH_TESTS=OFF`），`vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/ode-${VERSION})` 将 config 包落到 `share/ode`（安装布局），未覆盖精度选项＝上游默认（上游自拉 0.16.2 时显式 `-DODE_DOUBLE_PRECISION=ON`，见 RobWorkSimSetup.cmake:214 旁证；实际精度须安装后经 ODE_BUILD_WITH/ode-config 实测确认，不预先填写）。
- **当前未安装**：`vcpkg/installed/x64-windows/lib` 零 ode 库，`share/` 下 bullet3、fcl 在位（对照）；集成缓存 ODE 三变量 NOTFOUND。经典模式（manifest OFF）安装命令草拟：`vcpkg install ode:x64-windows`——属新依赖安装，须所有者批准后执行。
- **部署握手**：`FindODE.cmake:10-16` 先走 `find_package(ODE QUIET PATHS .../lib/cmake/ode-* ... NO_MODULE)` config 包路径，与 vcpkg 安装布局兼容；config 命中走文件尾 `else()` 分支（`ODE_BUILD_WITH=DOUBLE`、`ODE_INCLUDE_DIR=${ODE_INCLUDE_DIRS}`），手工宏路径不启用。`rwsimlibs/ode/CMakeLists.txt:65-86` 对 config 变量检测 `ODE_WITH_THREADING_IMPL`，0.16.6 走上游 threading-impl；`:118-123` 的 dDOUBLE/dSINGLE 手工宏仅 ODE<0.13 无 threading-impl 时启用，0.16.6 不触发。
- **启用机制与规避项**：`RobWorkSimSetup.cmake:175-231`——`find_package(ODE QUIET)` 命中且 `RW_BUILD_WITH_PQP OR RW_BUILD_WITH_FCL` 时 `RWSIM_HAVE_ODE=TRUE`（集成树 RW_USE_PQP=ON，build/CMakeCache.txt:834；FCL 已装，前提成立）；**WIN32 下 ODE 未找到会 ExternalProject 从 bitbucket git 拉取 ODE 0.16.2 网络构建（:206-221）——这正是本提案 §2 禁止的"网络下载另装"路径，阶段 B 必须先经 vcpkg 安装使命中 config 包来规避**；未命中且禁用时静默 `RWSIM_DISABLE_ODE`（:230-232 "ODE disabled."）。

## 2. 最小供给方案与权限

供给事实已由 §1a 实核收窄：同一 vcpkg 渠道的 ode 0.16.6 port 存在且未安装，基线/port/triplet（x64-windows）与二进制来源（vcpkg_from_bitbucket SHA512 锁定）在 portfile 在案。**阶段 B（待所有者批准）**：`vcpkg install ode:x64-windows` 后在隔离构建树配置现有项目，`find_package(ODE)` 经 vcpkg toolchain 命中 config 包使 `RWSIM_HAVE_ODE=TRUE`→`BUILD_sdurwsim_ode` 默认 ON（或显式 `-DBUILD_sdurwsim_ode=ON`），复用原FindODE与导出目标，不手拼lib或改上游CMake；不得vendor ODE、网络下载另装（含 RobWorkSimSetup 的 WIN32 git 拉取回退）或修改只读vcpkg树规避。许可 `BSD-3-Clause OR LGPL-2.1-or-later`、同渠道包版权、实际精度（双精度期望）与MSVC运行库匹配在阶段 B 安装后核实登记。

阶段 B 验证设计（批准后执行）：先运行原 `sdurwsim_ode-gtest` 受影响用例（ODEControlTest 速度控制/ODESimpleTest 稳定性——真实运行作回归底噪，不冒充力输入验证）；再新增单自由度力输入→step 探针用例：解析单摆/移动质量 oracle、RNE 反算交叉验证（τ=M(q)q̈+C+G 单关节退化）、能量守恒与确定性重复（同输入双跑逐位一致）；独立于产品 RK4（不与 ForwardDynamics 自持核互证）。resetScene 清速后经 `setJointVelocities` 恢复非零初速须实测验证生效。如原gtest资产/依赖不完整如实标记，不以编译通过代替实际运行。是否需要上游patch须以该轮失败证据另提，本提案不预授权修框架。

runtime负责真实DWC/RigidDevice、初始State和引擎会话物化；业务只通过受控任务会话提交SI广义力/状态与步长，不能另建WorkCell或跨worker传借持句柄。产品侧承接面已在位：`runtime/src/DynamicWorkCellCompiler.cpp`（S7）已产 rwsim DynamicWorkCell（集成模式编译，文件头登记），公契约 `ForwardDynamics.hpp:303 ForwardDynamicsValidator` 不变。引擎虽然load接口可变，操作只允许在私有物化副本/明确允许的会话，不获得权威模型写权；T02须验证其保活及版本映射。diagnostics/evidence职责不下放给ODE适配层。`units/dynamics.md:345` `forwardCheck.integratorToken` 封闭词表当前锁定 `rk4-fixed`——切换后端属 WP-09 前瞻，须先修订词表再追加常量，本提案不改词表。

## 3. 需要验证的适配差异（阶段 B 清单）

控制输入为N·m（转动）/N（移动），位置rad/m、速度rad/s或m/s、时间s；axis、zeroOffset、固定段、惯量参考系、重力、工具/载荷和摩擦逐项对齐，同一物性不得重复计入。resetScene会清速度，非零初速度必须经已公开设备/刚体状态API（`RigidDevice::setJointVelocities`，RigidDevice.cpp:260）验证恢复，不能在reset前赋值后宣称有效。禁用隐藏位置控制器影响；每个step前设置Force targets并核实际motor mode（`getMotorModes` 读回，ODEVelocityDevice.cpp:113 每步同步）。

DYN-05参考样本的控制保持/事件边界/步长/精度属于产品契约；ODE子步/迭代参数不保证与旧RK4数值轨迹一致。先以解析单摆/移动质量及RNE反算进行收敛和能量检查，再验证复杂链、接触是否可关闭、确定性、取消/引擎资源回收、两任务隔离。正常结果及异常须映射原有稳定码，不吞错；若算法误差不能满足原容差，不放宽需求，返回缺口提案。

## 4. 影响与替代/回滚

影响仅先为构建供给、审计实验和依赖证据；通过后T04-B才替换 `dynamics/src/ForwardDynamics.cpp` 中evalAcceleration（:510）、质量阵/消元及RK4子步（:612-692），保留请求/结果/工况/误差统计。产品契约/缓存身份变化须显式版本迁移（integratorToken 词表先行）。

首选现成ODE；Bullet/RWPhysics保留已查失败证据（§1 行号在案），原RNE-Wrench只用于逆动力学与独立一致性验证，不能充当FD引擎；拒绝产品自写积分器、新建通用动力学框架或生产fallback。新后端在任务分支启用，失败撤销该阶段接线与构建选项（仅关 `BUILD_sdurwsim_ode`/`RWSIM_DISABLE_ODE`，零产品接线，vcpkg 安装物可整体移除），已归档证据保留。未获供给确认、未实跑通过时，T04-B继续阻塞，其他不依赖它的审计可继续。
