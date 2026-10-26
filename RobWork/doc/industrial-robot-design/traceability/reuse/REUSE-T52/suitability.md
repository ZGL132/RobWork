# REUSE-T52 上游适用性报告（DYN-RNE 生产结项复核）

任务：[tasks/REUSE-T52.json](../../../tasks/REUSE-T52.json)（migration §13.3 WP-07 / T04 RNE 子任务）。
基线：起点 `bf682893ff174d759a93e905889458d2f4e80e21`；上游文件未改动（复核版本＝基线，UPSTREAM-HASH 与 git show 版本核对由门禁实算）。

## 1. 需求与正确性标准

DYN-01（基于 RobWorkSim 刚体模型与 RNE 的逆动力学）在逆动力学评估器的生产消费面：CanonicalModel 权威链值 → runtime 工厂 `buildRneConsumptionDevice`（RNE 可消费一次性载体——StateStructure/SerialDevice/RigidDevice，BodyInfo 已按关节帧换算）→ 上游 `rwsim::util::RecursiveNewtonEuler` solve（DOF+1 Wrench）→ 按关节类型投影的关节广义力（转动 τ·z／移动 F·z）＋产品层摩擦叠加。正确性标准＝§5.2 四通道恒等式、§5.4 负载变体保守边界、§5.5 sgn₀(0)=0 降级语义不变；权威轴≠z 时质心世界位置正确（GAP-RNE-FRAME 消费边界薄适配）。

## 2. 上游检索证据（H6）

```
rg -n "solve|setGravity|setPayload" RobWork/RobWork/src RobWork/RobWorkSim/src RobWork/RobWork/gtest RobWork/RobWorkSim/gtest
rg -n "RecursiveNewtonEuler" RobWork/RobWorkSim/src/rwsim/util RobWork/RobWorkStudio/src/rwslibs/industrialrobot
```

`RecursiveNewtonEuler.hpp/.cpp` 逐行实读（REUSE-T30 建立基线、本包生产结项复读）：solve（.cpp:227-235）＝getBodyMotion/getBodyNetForces/getJointForces 三段递推；getGravity/setGravity（.cpp:41-49，ctor 默认零）；getJointForces 工具条目（.cpp:203-210）payload 力矩臂 −cross(rCOM,F) 与手算物理 +cross(rCOM,F) 反号（本包代数复核确认，REUSE-T30 次要发现②同向登记）；solveMotorTorques（.cpp:249-259）对移动关节同投影 torque 且末元素复用陈旧 z（上游缺陷）。上游无 RNE 单元测试（T30 §2 在案）。

## 3. 被否候选与裁决脉络

- **S7 DWC 设备直用**：其 BodyInfo 为权威连杆系逐字承载（§8.2 canonical 断言）且经 const 视图发布不可换算——GAP-RNE-FRAME 薄适配在数学上要求新构 BodyInfo，故否（A2 裁决"不动 S7/§8.2/modeling 导出"的必然结论）。
- **dynamics 侧自构 RNE 载体**（首版实施形态）：IRD-GATE-AD 模型所有权机器门禁拒绝（reuse-contract.ps1:108——construct-model/mutate-model 仅 runtime-owner 可授权；「kinematics 自建 WorkCell 拒绝」同款红线）。**2026-10-26 所有者裁决＝构造面上收 runtime 公共工厂**（门禁零改动；WP-07/WP-09 共同消费，架构收益复利；裁决否决了治理例外方案并明确授权 runtime 最小面扩容）。
- **setPayload 工具通道承载合成体**：上游工具条目力矩臂符号缺陷在案，生产不消费（res[DOF] 按 R1 ExternalWrench 恒零不读）；合成体按 §5.4 既有语义并入法兰 BodyInfo（消费方变体回写，写面＝Body 非 const getInfo 访问器——task-private 授权）。
- **solveMotorTorques 投影复用**：移动关节缺陷（同上）——按关节类型自行投影（migration §13.3 WP-07 行）。

## 4. 定向核结论（保留/删除/适配/职责）

- **保留**：请求/结局 DTO 与 evaluate 语义（四通道/变体/摩擦/诊断/能量面零变化，kInverseDynContractVersion=1）；摩擦 sgn₀ 叠加与三层降级（上游无逐关节摩擦一职——T30 §4 边界）；验证提取面（词表/轴单位/偏置/物性 F-645/摩擦 F-632——DynamicsError 语义）留 dynamics。
- **删除**：自写 RNEA 旧核全清单——Vec3/Mat3 逐元素算术、axisRotation、SampleGeometry/computeGeometry、RneaChain/buildRneaChain、rneaTorques（dynamics 净 −284 行；门禁指纹仅止增实核）。
- **适配**：runtime 工厂单点——关节帧 tAligned=origin·R_align＋zeroOffset 烘焙＋连杆帧 R_alignᵀ（S6 同源语义）＋BodyInfo 关节帧换算（R_alignᵀ·com、R_alignᵀ·I·R_align；轴对齐公式与 WorkCellCompiler.cpp 同源——本单元内最小授权面镜像，合并归卫生批）。
- **职责**：runtime 唯一模型 owner（工厂构造＋换算单点，runtime-owner 授权）；dynamics 消费载体（task-private：setQ/变体回写/求解/投影）；§8.2 canonical 逐字断言、modeling DWC 导出、S7 产品映射三处零触碰（A2 原约束）。

## 5. oracle 与证据

- normal：dynamics 全量 119 项（118 既有断言零放松＋REUSE-T52 增测）全过；contract 30 项全过；runtime 消费者回归 312 项（311 passed＋1 既有 skip）与基线同名单；独立冒烟 119 项全过（工厂 TU 双模式编译＝单一实现）。
- oracle（禁自比）：①任意构型平衡叉积（弯曲 q2≠0＋挂载负载，手算 Σr×F，含"弯曲≠伸直"判别裕度断言——GAP「bent==rest」失败形态定向探针）；②准静态能量平衡（τ·q̇ 梯形积分＝手算势能差 ΔU，量级 5.98 J、残差 8×10⁻⁶ J；关节 2 离心矩非零×零速度＝零功率物理钉）；③黄金数据集（拉格朗日独立参考）全绿。
- upstreamComparison：同模型同 State 测试侧内联同构载体直连上游 solve，与产品评估器全量通道 τ EXPECT_EQ 逐位一致（生产路径真实路由证明）。
- disconnected（三行各自文件内变异，均 exit=1＋真实失败后恢复复验 119 passed）：①runtime 工厂质心换算行改连杆系直喂→y 轴静态＋弯曲平衡失败（GAP 复现）；②dynamics 验证提取行零位偏置置 0→权威角偏置用例失败（T13 语义链）；③dynamics 通道行 setGravity 注释→静态重力用例失败（T30 次要发现①复现）。

## 6. 已知边界与登记

- GAP-RNE-FRAME 缺口本体（上游缺陷）不因消费适配关闭：S7/§8.2/modeling 导出零触碰，上游代码零改动，缺口登记保持开放；WP-09 FD 消费者按其自身阶段处理（可复用 runtime 工厂设施）。
- getJointForces payload 差异按 §13.2 归档核（本包代数复核：内向递推项恒等成立、工具条目符号反——生产不消费该通道，登记于契约 note）。
- 上游 `RigidLink::getVelocity` linear 参考点语义未文档化（T30 次要发现③）——本消费面不经该路径（solve 内部速度面由 oracle 间接覆盖）。

## 7. 适用边界

本能力覆盖 CanonicalModel 权威链值的逆动力学求解载体与求解；设备词表＝SerialDevice＋转动/连续/移动（Fixed 沿既有评估契约拒绝）；不覆盖正动力学（WP-09 独立阶段，可复用本工厂）、不放宽 §5.5 降级语义；verified 不代表适用于所有需求。
