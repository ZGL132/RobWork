# WP-06-T04 任意轴运行时适配

- Task ID：WP-06-T04
- 需求/阶段：MDL-06、MDL-09、MDL-10、MDL-14、NFR-COR-05；阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/runtime-model.md`
- 前置：WP-06-T01、WP-06-T03。
- 允许：修改 `runtime/src/RobWorkModelAdapter.cpp`、`src/AxisCompensation.cpp`、`test/AxisAdapterTest.cpp`、`testdata/runtime/axes/`。
- 禁止：改写 canonical Origin/Axis、改变关节物理类型、修改 WP-07 碰撞策略或 GUI。
- 产出：非 Z、非单位、Continuous、Prismatic 轴的局部补偿及几何/惯量同步转换。

## 数据流

`canonical axis -> compute rigid compensation to local Z -> insert internal frame/link -> transform visual/collision/COM/inertia -> compile -> compare world pose/axis`。补偿只存在运行时适配层，resolver 仍返回原 objectId。

## Given/When/Then

- Given 任意有限非零轴，When compile，Then Zero、Home、正负边界和固定 100 状态的末端位姿/世界轴线满足冻结容差。
- Given 零轴、非有限轴或无法构造补偿，When compile，Then返回 Input/Engineering 诊断且不产生部分工件。
- Given Prismatic/Continuous，When adapt，Then位移/连续角度语义和 limits 保持，视觉、碰撞、COM、惯量使用同一变换。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_axis_adapter_test$'
```
证据：补偿矩阵、位姿/轴线误差、几何和惯量对照、诊断及 RobWork 版本。提交：`WP-06-T04: implement arbitrary-axis runtime adapter`。

停止：补偿会改变领域原始物理值或容差未在需求中定义时暂停。
