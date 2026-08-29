# 运行时模型与名称模块详细方案

- 方案版本：v0.2；需求基线：v0.7；负责 WP：WP-06；阶段/发布：阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`

## 1. 模块职责

模块将领域 `RobotDesign` 编译为规范 SE(3) 模型、RuntimeNameMap、WorkCell 和 DynamicWorkCell，并验证四者的身份、几何、运动学和动力学绑定一致。RobWork 指针只存在适配层并由创建线程释放；下游通过值语义和 `IRuntimeNameResolver` 使用结果。

## 2. 目录与目标

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/
  include/sdurws/ird/runtime/
    CanonicalKinematicModel.hpp RuntimeNameMap.hpp
    IRuntimeNameResolver.hpp CanonicalModelCompiler.hpp
    CompiledRobotArtifacts.hpp RobWorkModelAdapter.hpp
    RuntimeDiagnostics.hpp
  src/CanonicalModel.cpp RuntimeNameMap.cpp CanonicalModelCompiler.cpp
      RobWorkModelAdapter.cpp DynamicWorkCellAdapter.cpp RuntimeJson.cpp
  test/CanonicalModelTest.cpp NameMapTest.cpp DualCompileTest.cpp
      AxisAdapterTest.cpp RenameScanTest.cpp
  testdata/runtime/{dh,explicit,urdf,names,axes,failpoints}/
```

CMake 目标：`sdurws_ird_runtime`、`sdurws_ird_runtime_test`、`sdurws_ird_runtime_contract_test`。允许 WP-03 core、RobWork/RobWorkSim 和标准库；禁止 Qt Widgets、插件 UI、直接项目写入。

## 3. 规范模型

所有输入先转换成 SI 单位和规范 quaternion/SE(3)。链计算固定为 `OriginPose · Motion(â, q)`（规范公式、坐标系与 q-zero 裁决以 `architecture/canonical-kinematics.md` §2～3 为准；任何零位偏置在编译边界吸收进 `OriginPose`，运行时不叠加第二个偏置），父子顺序由 `parentLinkId/childLinkId` 唯一确定；World/base frame 是显式根。StandardDH 的 `a/alpha/d/theta` 仅存在导入适配器，转换后不再被下游接口接受；ExplicitJoint 和 URDF 必须产生相同 canonical 字段。

Joint 的轴必须 finite、非零并归一化；`Revolute` 使用 rad/N·m，`Prismatic` 使用 m/N，`Continuous` 无位置上限但仍需 finite velocity/effort，固定关节不暴露可动轴。零位和 home 分开保存，限位边界包含 inclusive/exclusive 标记。

## 4. RuntimeNameMap

映射键为 `(ownerScopeId, objectId)`，值为 `runtimeDeviceName`, `localName`, `runtimeScopedName`, `objectKind`。机器人内部名称格式固定 `<device>.<local>`，只允许 `[A-Za-z0-9_.-]`；WORLD/环境对象使用其规范全局名。`resolve(objectId)` 和 `reverse(scopedName)` 必须互为逆函数；重复、旧前缀、双前缀或去前缀后冲突返回稳定诊断，绝不取第一个。

## 5. 双编译和所有权

编译器在隔离 builder 中分别生成 WorkCell、DWC 和名称表，完成后按 objectId 交叉校验 device、joint、frame、geometry、collision/proximity、mass、COM、inertia 和 limits。任一 builder 抛错、返回空或校验失败即释放全部临时指针并返回空 `CompiledRobotArtifacts`；调用方旧工件不变。编译结果不自动写项目，仅由上层显式提交。

## 6. 任意轴补偿

当 RobWork 关节只支持局部 Z 轴，适配器计算从 canonical axis 到 Z 的旋转补偿，在 joint 与 child link 间插入内部 frame。所有视觉/碰撞几何、COM 和 inertia 使用同一刚体变换；反向查询仍以原始 objectId/axis 为准。测试必须比较 canonical 与运行时在 Zero、Home、正负边界和固定 100 个姿态下的末端位姿及世界轴线。

## 7. 重命名与确定性

重命名只改变 `runtimeDeviceName/localName` 和名称表；objectId、canonical 物理内容、sliceHash 和历史快照不变。历史快照保留旧 map，新编译生成新 map；禁止旧名称和新名称同时绑定同一 objectId。固定输入、算法版本、seed 和线程数时，canonical JSON、名称表顺序、artifact 清单和诊断顺序一致。

## 8. 测试与证据

测试夹具覆盖 Arm、ArmA、RobotB、重复 Joint1/TCP、双前缀、旧前缀、非 Z/非单位轴、continuous、prismatic、缺几何、DWC 失败和 WorkCell 失败。契约测试检查 resolver 唯一所有权和下游不得拼接名称；静态扫描只允许 resolver/adapter 目录出现前缀拼接或剥离。证据需含输入身份、名称表、artifact hash、RobWork 版本、容差报告和 failpoint 日志。

## 9. 迁移与评审

旧 `stripDeviceScope` 等链路先以只读适配器隔离，扫描和回归通过后删除；无法证明历史名称映射的结果标 EvidenceOnly。评审逐项核对 canonical/运行时一致性、全成全败、线程所有权、WORLD 例外、未来 ownerScopeId 扩展和禁止跨模块名称猜测。
