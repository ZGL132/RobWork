# REUSE-T31／WP-08：GAP-FD-BUILD-01 动力学后端供给定向探测记录

实施日期：2026-10-25；分支 redesign-main；起始 SHA `e74886dfcb972a2bbcc4f1336e22ab146037ffde`（[WP-06-T30] 收口提交）。
任务契约：[REUSE-T31](../../tasks/REUSE-T31.json)。范围＝探测/证据/提案，零产品代码、零 vcpkg/上游改动。状态＝实施完成待独验（不自签）；提案裁决由所有者安排。

本包把 `reuse/extensions/GAP-FD-BUILD-01.md` 从"待供给确认"修订为**可裁决供给提案**。以下分节记录本次全部只读实核命令与输出摘要；增量检索同步登记于 [proposal-search.json](../../../reuse/extensions/proposal-search.json) `increments[taskId=REUSE-T31]`。所有行号为 2026-10-25 HEAD `e74886dfc` 现状。

## §A vcpkg ode port 供给事实

命令：`cat vcpkg/ports/ode/vcpkg.json`、`head -40 vcpkg/ports/ode/portfile.cmake`、`grep -rn 'DOUBLE\|PRECISION' vcpkg/ports/ode/`。

- `vcpkg/ports/ode/vcpkg.json`：`"name": "ode", "version": "0.16.6"`，`"license": "BSD-3-Clause OR LGPL-2.1-or-later"`，`"supports": "!uwp"`，依赖 `vcpkg-cmake`/`vcpkg-cmake-config`（host）。
- `vcpkg/ports/ode/portfile.cmake`：`vcpkg_from_bitbucket(REPO odedevs/ode REF ${VERSION} SHA512 c9160d9b…)`（二进制来源锁定）；`-DODE_WITH_DEMOS=OFF -DODE_WITH_TESTS=OFF`；`vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/ode-${VERSION})`——安装后 config 包落 `share/ode`（lib/cmake 残留被 `file(REMOVE_RECURSE .../lib/cmake)` 清除）。
- port 树零 `DOUBLE/PRECISION` 命中：**port 未覆盖精度选项＝上游默认**。旁证：上游 `RobWorkSimSetup.cmake:214` 自拉 0.16.2 时显式 `-DODE_DOUBLE_PRECISION=ON`。实际精度在阶段 B 安装后经 `ODE_BUILD_WITH`/ode-config 实测确认，不预先填写。

**结论**：GAP §2 原不确定项"若该渠道当前无适用port"收窄为——**适用 port 在位**（0.16.6，许可/构建选项在案）。

## §B 当前安装状态与环境缓存

命令：`ls vcpkg/installed/x64-windows/lib/ | grep -i ode`、`ls vcpkg/installed/x64-windows/share/ | grep -iE '^(bullet3|fcl|ode)$'`、`grep -n 'BUILD_sdurwsim_ode\|ODE_DIR:\|ODE_INCLUDE_DIR\|ODE_LIBRARY_TMP' build/CMakeCache.txt`、`grep -n 'VCPKG_MANIFEST_MODE\|VCPKG_TARGET_TRIPLET:' build/CMakeCache.txt`、`grep -n 'pqp\|PQP' build/CMakeCache.txt | head -6`、`grep -n 'BUILD_sdurwsim_bullet' build/CMakeCache.txt`。

- installed `lib/` 零 ode 命中、`share/` 零 ode 条目——**ODE 未安装**；对照：`share/bullet3`、`share/fcl` 在位（bullet3 系已装库，但对 RigidDevice FD 不可用，见 §D）。
- `build/CMakeCache.txt:240` `BUILD_sdurwsim_ode:BOOL=OFF`；`:600/:603/:606` `ODE_DIR/ODE_INCLUDE_DIR/ODE_LIBRARY_TMP` 均 `-NOTFOUND`；`:2771` `RW_SUBSYS_BUILD_sdurwsim_ode:INTERNAL=FALSE`。
- `:942` `VCPKG_MANIFEST_MODE:BOOL=OFF`、`:953` `VCPKG_TARGET_TRIPLET:STRING=x64-windows`——经典模式安装（`vcpkg install ode:x64-windows` 即可，属新依赖启用，待批准）。
- `:834` `RW_USE_PQP:BOOL=ON`（`RobWorkSimSetup.cmake` 的 PQP 前提成立；FCL 已装）——阶段 B 启用前提满足。

**结论**：本机集成缓存只证明环境未供给，不证明上游无 FD；阶段 B 前提（PQP）已在位。

## §C 力驱动直连 API 链（行号级核验）

命令：`grep -n 'void RigidDevice::setMotorForceTargets\|void RigidDevice::setJointVelocities' RobWork/RobWorkSim/src/rwsim/dynamics/RigidDevice.cpp`；`grep -n '_odeJoints\[i\]->setForce' RobWork/RobWorkSim/src/rwsimlibs/ode/ODEVelocityDevice.cpp`；`grep -n 'RigidDevice\* rdev = dynamic_cast' RobWork/RobWorkSim/src/rwsimlibs/ode/ODESimulator.cpp`；`grep -n 'virtual void step\|virtual void resetScene\|virtual void initPhysics\|virtual void exitPhysics\|virtual double getTime' RobWork/RobWorkSim/src/rwsim/simulator/PhysicsEngine.hpp`；附 `sed -n '225,265p' RigidDevice.cpp`、`sed -n '110,175p' ODEVelocityDevice.cpp`、`sed -n '1205,1230p' ODESimulator.cpp`、`sed -n '95,120p' PhysicsEngine.hpp` 读源段确认语义。

- `RigidDevice.cpp:234` `setMotorForceTargets`：写 `_target` 并逐关节置 `modes[i] = 1`（`MotorControlMode::Force`，RigidDevice.hpp:106）——**力目标与模式切换单 API 完成**。
- `RigidDevice.cpp:260` `setJointVelocities`：经 State 写速度数组并 clamp 到 `_jdev->getVelocityLimits()`——resetScene 清速后恢复非零初速的公开通道（生效性阶段 B 实测）。
- `ODEVelocityDevice.cpp:113` `update` 每步 `getMotorModes(state)` 读回并同步 `_modes`；`:120-123` 力限变化检测；`:166` Force 分支 `_odeJoints[i]->setForce(targets[qi])`（模式切换时 `setMotorEnabled(false)` 关速度伺服）——**力驱动路径真实存在**。
- `ODESimulator.cpp:1214` `addDevice` 对 `RigidDevice` 构造 `ODEVelocityDevice(base, rdev, state, this)` 入 `_odeDevices`。
- `PhysicsEngine.hpp:99/105/110/115/120`：`step(dt, state)`/`resetScene(state)`/`initPhysics(state)`/`exitPhysics()`/`getTime()` 全纯虚——引擎会话接口面。

**结论**：`setMotorForceTargets → ODEVelocityDevice::update → setForce` 直连链完整，DYN-05 力输入语义有现成承载，无需重写积分器。

## §D 负面证据（Bullet / RWPhysics / 刚体 RK4Integrator）

命令：`sed -n '580,595p' RobWork/RobWorkSim/src/rwsimlibs/bullet/BtSimulator.cpp`、`sed -n '128,140p' RobWork/RobWorkSim/src/rwsim/rwphysics/RWSimulator.cpp`、`sed -n '1,40p' RobWork/RobWorkSim/src/rwsim/rwphysics/RK4Integrator.hpp`。

- `BtSimulator.cpp:585-588`：`void BtSimulator::addDevice (DynamicDevice::Ptr dev, State& nstate) { RW_THROW ("BtSimulator (addDevice): not implemented yet!"); }`——**addDevice 直接抛未实现**；bullet3 虽为已装库（§B），对 RigidDevice FD 不可用。
- `RWSimulator.cpp:132-135`：RigidDevice 分支仅 `std::cout << "NO SUPPORT FOR RIGID DEVICES!"`（KinematicDevice 同样 NO SUPPORT）——RWPhysics 明示不支持。
- `RK4Integrator.hpp:22-36`：`class RK4Integrator : public BodyIntegrator`，构造收 `dynamics::RigidBody*`——**单刚体 BodyIntegrator，非关节 ODE 求解器**；不得把刚体积分器接口冒充关节力控制后端。

**结论**：三条已测替代路径明确排除；失败只排除已测路径，不排除整个 RobWorkSim。

## §E 部署握手（FindODE / sdurwsim_ode 构建注册 / RWSIM_HAVE_ODE 探测）

命令：`head -16`/`tail -50 RobWork/RobWorkSim/cmake/Modules/FindODE.cmake`、`grep -n 'ODE_WITH_THREADING_IMPL' RobWork/RobWorkSim/src/rwsimlibs/ode/CMakeLists.txt`（首尾段 `sed -n '1,30p'`/`sed -n '110,130p'`）、`sed -n '170,230p' RobWork/RobWorkSim/cmake/RobWorkSimSetup.cmake`。

- `FindODE.cmake:10-16`：先 `find_package(ODE QUIET PATHS "$ENV(ODE_ROOT)/lib/cmake/ode-*" "${ODE_ROOT}/lib/cmake/ode-*" ${ODE_DIR} ... NO_MODULE)`——**config 包优先**，与 vcpkg 安装布局（config 落 share/ode，经 vcpkg toolchain 前缀注入）兼容。
- config 命中走 `FindODE.cmake` 尾段 `else()` 分支：`set(ODE_BUILD_WITH "DOUBLE")`＋`ODE_INCLUDE_DIR=${ODE_INCLUDE_DIRS}`——走上游 config 变量，不进手工 find_library 链。
- `rwsimlibs/ode/CMakeLists.txt:19` `set(DEFAULT ${RWSIM_HAVE_ODE})`；`:65-86` 检测 config 变量 `ODE_WITH_THREADING_IMPL`（0.16.x 携带）；`:118-123` `dDOUBLE`/`dSINGLE` 手工宏仅在 `NOT ODE_WITH_THREADING_IMPL`（<0.13）时编译定义——**0.16.6 不触发手工宏，精度以 ode-config 实际值为准**。
- `RobWorkSimSetup.cmake:175-231`：`find_package(ODE QUIET)` 命中且 `RW_BUILD_WITH_PQP OR RW_BUILD_WITH_FCL` → `RWSIM_HAVE_ODE=TRUE`；**WIN32 未命中时 `:206-221` ExternalProject 从 bitbucket git 网络拉取 ODE 0.16.2 构建**——属 GAP §2 明令禁止的"网络下载另装"路径，阶段 B 必须先经 vcpkg 安装使命中 config 包来规避；未命中且禁用走 `:230-232` 静默 "ODE disabled."（当前集成树即此状态）。

## §F 产品侧承接面（零改动确认）

命令：`head -30 RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/DynamicWorkCellCompiler.cpp`、`grep -n 'class ForwardDynamicsValidator' .../dynamics/include/sdurws/ird/dynamics/ForwardDynamics.hpp`、`grep -n 'evalAcceleration' .../dynamics/src/ForwardDynamics.cpp | head -3`、`sed -n '340,352p' units/dynamics.md`。

- `DynamicWorkCellCompiler.cpp` 文件头：S7 编译器（CanonicalModel＋S6 产物 → rwsim DynamicWorkCell，Body/RigidDevice/质量惯量/摩擦/重力），**仅集成模式编译**，单位纪律 SI 真值——产品侧 rwsim DWC 物化面已在位，阶段 B/WP-09 直接消费。
- `ForwardDynamics.hpp:303` `class ForwardDynamicsValidator`——公契约不变面。
- `ForwardDynamics.cpp:510` `evalAcceleration` lambda、`:612-692` RK4 子步循环——与 functions/dynamics.json DYN-FD 行登记区段一致（本包未触碰，属 WP-09 替换目标）。
- `units/dynamics.md:345`：`forwardCheck.integratorToken` 封闭词表锁定 `rk4-fixed`（"扩展新积分器先本表增量修订词表再追加常量，既有值不重排"）——后端切换属 WP-09 前瞻，本提案不改词表。

## §G 既有 gtest 资产与 T04 状态

命令：`sed -n '40,75p' RobWork/RobWorkSim/gtest/ode/ODEControlTest.cpp`、`sed -n '63,73p' RobWork/RobWorkSim/gtest/CMakeLists.txt`、`tasks/REUSE-T04.json` 状态读取。

- `gtest/CMakeLists.txt:63-73`：`if(TARGET sdurwsim_ode)` 门控 `sdurwsim_ode-gtest`（链接 sdurwsim-gtest-main/sdurwsim/sdurwsim_ode；源 ODEControlTest.cpp＋ODESimpleTest）。
- `ODEControlTest.cpp:45` `ODEControlDeviceTest`：`DynamicWorkCellLoader::load(...cup_pg70_table.dwc.xml)`＋`SerialDeviceController::movePTP`＋`odesim->step(0.01, state)` 循环——**movePTP 速度控制，非 DYN-05 力输入测试**；`:73` 起注释掉的 `ODEControlDeviceTest_FC` 变体不构成可用测试。
- `ODESimpleTest.cpp`：落体/托盘稳定（:36/:77/:134），亦非力输入。
- **既有 gtest 资产中无关节力输入用例**——阶段 B 探针用例须新设计（解析单摆/移动质量 oracle＋RNE 反算交叉验证＋能量守恒＋确定性重复，独立于产品 RK4）。
- `tasks/REUSE-T04.json`：`status=planned`（"替换RNE和真实正动力学场景"）——T04 未动，阻断不变。

## 阶段 B 设计（待所有者批准，本包不实施）

1. **供给**：`vcpkg install ode:x64-windows`（经典模式，port 0.16.6；许可 BSD-3-Clause OR LGPL-2.1-or-later 在案，实际精度/运行库匹配安装后核实）。
2. **配置/构建**：隔离构建树 `cmake -S RobWork/RobWorkSim ...`（或产品集成树副本）带 vcpkg toolchain，`find_package(ODE)` 命中 config → `RWSIM_HAVE_ODE=TRUE` → `BUILD_sdurwsim_ode` 默认 ON（或显式 `-DBUILD_sdurwsim_ode=ON`）；复用 FindODE/导出目标，不改上游 CMake、不触发 RobWorkSimSetup 的 git 网络拉取回退。
3. **回归底噪**：先真实运行既有 `sdurwsim_ode-gtest`（ODEControlTest/ODESimpleTest），PASS/FAIL/SKIP/NOT RUN 分列；结果只作回归底噪，不冒充力输入验证。
4. **力输入探针**（新用例）：单自由度力输入→step——解析单摆/移动质量 oracle、RNE 反算交叉验证（τ=M(q)q̈+C+G 单关节退化）、能量守恒、确定性重复（同输入双跑逐位一致）；`resetScene` 清速后经 `setJointVelocities` 恢复初速实测验证；独立于产品 RK4，不与 ForwardDynamics 自持核互证。保留 gtest XML 与 per-exe `ird-test-report.json`。
5. **回滚**：仅关 `BUILD_sdurwsim_ode`/`RWSIM_DISABLE_ODE` 构建选项＋移除 vcpkg 安装物，零产品接线；已归档证据保留。

## 验证与移交

本包阶段 A 零 C++ 构建（无产品代码改动），验证＝文档与复用治理门禁（validate-docs -TaskFile 含全量复用检查、commit-lint、verify-task-scope 显式 Base/Head），命令与结论登记于契约 `verify` 与提交正文。提案可裁决性（§1a/§2 供给事实＋API 链＋负面证据＋阶段 B 设计＋回滚）供所有者另开会话或所有者明确指令的单次验收子智能体裁决；阶段 B 未获批不实施，T04/WP-09 阻断不变。实施完成待独验，不自签、不置 done。
