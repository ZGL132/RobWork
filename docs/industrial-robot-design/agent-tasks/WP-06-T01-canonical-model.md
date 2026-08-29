# WP-06-T01 规范 SE(3) 计算模型

- Task ID：WP-06-T01
- 需求/阶段：ARC-03、ARC-04、CON-06、MDL-06、MDL-14、NFR-MNT-07；阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/runtime-model.md`
- 前置：WP-03 core、WP-01 构建脚本。
- 允许：修改 `runtime/include/.../CanonicalKinematicModel.hpp`、`src/CanonicalModel.cpp`、`test/CanonicalModelTest.cpp`、`testdata/runtime/{dh,explicit,urdf}/`。
- 禁止：修改领域单位/枚举、FK/IK 业务算法、RobWork 运行时名称、GUI 和项目持久化。
- 产出：DH/ExplicitJoint/URDF 到规范模型的转换器、SE(3) 链和等价性测试。

## 数据流

`source model -> validate IDs/units/axis/chain -> normalize Origin/quaternion/axis/limits -> canonical joint/link graph -> deterministic serialization`。下游只接收 canonical，不再读取 DH 表或 Widget 数据。

## Given/When/Then

- Given 等价 StandardDH、ExplicitJoint 和 URDF，When compile，Then joint/link 拓扑、FK、世界轴线和 limits 在冻结容差内一致。
- Given 零轴、非有限量、断链、重复 objectId 或固定关节带可动轴，When compile，Then返回 Input 诊断且无模型输出。
- Given Continuous 或 Prismatic 关节，When compile，Then类型、单位和限位语义保持，不被转换为 Revolute。

## 测试、证据与提交

覆盖四入口黄金模型、负/零边界、四元数归一化和固定 100 状态。命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_canonical_model_test$'
```
证据：canonical JSON、FK/轴线差异、诊断和输入哈希。提交：`WP-06-T01: implement canonical SE3 model`。

停止：来源格式语义无法唯一转换或需要修改 WP-03 字段时暂停并报告。
