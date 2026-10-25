# REUSE-T30 上游适用性报告（RUNTIME-DWC-MODEL）

任务：[tasks/REUSE-T30.json](../../../tasks/REUSE-T30.json)（migration §13.3 WP-06 / T02 动力学映射子任务）。
基线：起点 `c816a2c482088b9d03d537c103cf9ac314259187`；上游文件未改动（复核版本＝HEAD）。

## 1. 需求与正确性标准

DYN-01（基于 RobWorkSim 刚体模型与 RNE 的逆动力学）、DYN-02（重力/连杆惯性/末端负载/摩擦模型承载）、MDL-06（原子编译与物理合法性断言分域）在 runtime S7 编译器（`DynamicWorkCellCompiler.cpp`）的映射面：CanonicalModel ＋ S6 产物 WorkCell → rwsim DynamicWorkCell。正确性标准＝每 Body 物性（质量/质心/惯量）、DWC 重力、摩擦承载、RigidDevice 关节集与 WC 设备一致（§8.5）、offset 语义（T13：q_auth=zeroOffset+q_rw）在产物中保持；映射产物可被下游动力学消费。

## 2. 上游检索证据（H6）

```
rg -n "class BodyInfo|class Body |class FixedBody|class RigidDevice|class DynamicWorkCell|class MaterialDataMap|class RecursiveNewtonEuler" RobWork/RobWorkSim/src/rwsim/dynamics RobWork/RobWorkSim/src/rwsim/util
rg -ln "RecursiveNewtonEuler|RigidDevice" RobWork/RobWorkSim/gtest RobWork/RobWorkSim/example
rg -n "setPayload|_payload|getEnd|validate" RobWork/RobWorkSim/src/rwsim/util/RecursiveNewtonEuler.cpp
```

结论：`rwsim::dynamics`（Body/BodyInfo/FixedBody/RigidDevice/DynamicWorkCell/MaterialDataMap，target=sdurwsim——`RobWork/RobWorkSim/src/rwsim/CMakeLists.txt` 平铺单一目标）与 `rwsim::util::RecursiveNewtonEuler` 公共 API 完整定位；上游无 RNE 单元测试（gtest 命中为 DWC XML 夹具与 SimTaskPlugin 示例，间接引用），`RecursiveNewtonEuler.hpp/.cpp` 与 `Body.hpp` 逐行实读为语义依据。基线消费先例：`rwsim/control/SerialDeviceController.cpp:586`（RNE 以 RigidDevice 构造）。

## 3. 被否候选

- **RUNTIME-WC-MODEL 原样复用被否**：其 upstream 全部为 `rw/models`＋`rw/kinematics`（targets=sdurw_models/sdurw_kinematics），零 `rwsim/dynamics` 符号——与本包上游 `rwsim::dynamics`（target=sdurwsim）不符，绑定会构成假 capabilityId（H6）。故新登记 `RUNTIME-DWC-MODEL`。
- **自写平行动力学核心**：S7 现状已直接构造上游对象（BodyInfo/FixedBody/RigidDevice/DWC/MaterialDataMap），无重复数值核心可删（policy §1.1.3）；H1 直接 API 满足，无薄适配新增。

## 4. 定向核结论（保留/删除/适配/职责）

- **保留**：S7 全部映射（能力门控/名称唯一源/§8.5 关联校验/摩擦承载/异常转译/销毁顺序）；BodyInfo 按权威连杆系逐字承载（Body.hpp "body frame" 契约自洽）；工具物性为 Tcp 帧 FixedBody（不进设备连杆序列——canonical 未声明耦合语义）。
- **删除**：无（映射无自写核心）。
- **适配（既有）**：名称映射（R-4）、能力门控降级（§5.6）、摩擦 material-pair 承载约定（§8.8 无补丁方案——不接管摩擦策略，消费语义归 dynamics）。
- **职责**：runtime 唯一模型 owner、任务私有 State；不接管摩擦/时间策略；不跨进程传指针。

## 5. oracle 与证据

- 新增 3 个测试（`DynamicWorkCellCompilerTest.cpp`）：①非零 zeroOffset 默认 State 位姿/bounds/Device 同一性 oracle；②固定段复合刚体（多重集回溯＋同段角速度相等＋跨段独立期望）；③RNE 可消费性（单 z 轴非退化全分量手算：静止重力矩/cross(com,F)、ddq 惯量＋切向合成 (I_zz+m(rx²+ry²))·α、validate/solve DOF+1/setPayload 通道）。
- 全量 runtime 309 项（308 passed＋1 既有 skipped＋0 failed）；modeling 消费者 479 项全过（含 MdlPackageWorkCell.WorkCellDwcXmlExportFromSnapshotView_WP13T13_ACC5）。
- mutation 聚合断开（mass 置零＋重力偏移 0.01）：exitCode=1、CompiledPathBodiesAndExplicitValues_RT_T08 与 DwcProductConsumableByUpstreamRne_REUSE_T30 真实失败；已恢复并复验（normal 308 passed）。

## 6. GAP 登记（所有者 2026-10-25 裁决：接受为登记缺口，包内不实施修复）

**GAP-RNE-FRAME**：上游 `RecursiveNewtonEuler`/`RigidLink` 把 `BodyInfo.masscenter` 乘**关节帧**旋转（`getBodyMotion`/`getJointForces` 的 baseTlink 链，`RecursiveNewtonEuler.cpp:136,183,203`；基线 loader 惯例＝body frame 即关节帧），而 S7 承载帧挂连杆帧下恒等（body frame＝权威连杆系）、masscenter 填 canonical 逐字值——权威轴≠z 时两者差 R_align 对齐旋转，质心世界位置错位。探针实证：j2 轴 y、com=(0,0,0.2) 时 RNE 静止输出 τ_x=±5.886（恰为 R_align 混入量级 0.2×3×9.81）；且 com 沿 z 时 Rz(q) 不动它（构型相关项缺失，bent==rest）。权威轴=z（R_align=I）面无此缺口——测试 3 的全分量手算 oracle 即该面证据。

修复落点提案参考（WP-07 准入前按提案流程比较，本包未实施）：
1. S7 映射改填 R_alignᵀ 变换值（masscenter＝R_alignᵀ·com、inertia＝R_alignᵀ·I·R_align）——动 §8.2 canonical 逐字断言、modeling DWC 导出、RigidLink 速度参考面，爆炸半径大；
2. RNE 消费边界薄适配（H3）——可能不动 §8.2 断言与 modeling 导出。
提案须逐项列全消费者（WP-07 RNE、WP-09 FD、modeling DWC 导出、RigidLink 速度参考点）并确认语义（migration §13.2 交接索引已登记阻断）。

**次要发现（归 WP-07 深核）**：①RNE 重力不入映射产物（ctor 默认零——消费方须 setGravity 显式传递）；②`getJointForces` 的 payload 力矩传递含 `cross(dr−com_link, F_p)` 项，与直接物理手算（payload 质心臂）不一致；③`RigidLink::getVelocity` 的 linear 分量经 `inverse(bTf)·(bJf·vel)` AD 变换含体位置相关附加项（参考点语义未文档化）——本包测试按角速度面（参考点无关）钉住速度积语义。

## 7. 适用边界

本能力覆盖 CanonicalModel→DynamicWorkCell 的结构映射与物性承载；不代表全项目动力学已迁移（RNE/FD 生产接线归 WP-07/WP-09）；上游无 RNE 测试，适用性以本包产品 oracle 与探针实证为准；verified 不代表适用于所有需求。
