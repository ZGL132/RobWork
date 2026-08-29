# WP-06 运行时模型与名称实施计划

> 阶段/发布：阶段 A / R1；运行时模型和名称公共接口所有者：WP-06。实现者、验证者和评审者必须是不同执行上下文。

**目标：** 从同一 `RobotDesign` 生成唯一规范 SE(3) 计算模型、确定性 `RuntimeNameMap`、WorkCell 和 DynamicWorkCell，确保 objectId、物理坐标和运行时名称始终可追溯。

## 1. 目标与非目标

交付 DH/ExplicitJoint/URDF 到规范模型的编译、objectId 双向名称解析、WorkCell/DWC 全成全败编译、任意关节轴适配、重命名回归和名称前缀静态扫描。首版只有一个机械臂，但名称绑定必须含 `ownerScopeId`，为多机械臂命名空间保留扩展点。

不实现 FK/IK 算法、轨迹/动力学评估、碰撞策略、GUI 场景、项目持久化或业务插件私有名称拼接。

## 2. 需求、契约和发布切片

- 需求：ARC-03、ARC-04、CON-06、MDL-06、MDL-09、MDL-10、MDL-14、NFR-COR-05、NFR-MNT-07、AT-01、AT-15、AT-16、AT-18。
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`。
- 模块方案：`module-design/runtime-model.md`。
- 阶段门禁：阶段 A 交付模型/名称/双编译基线；阶段 B 供 WP-13～15、WP-20 使用；不以阶段 C/D 轨迹和动力学能力作为本 WP 退出条件。

## 3. 文件所有权与依赖

拥有目录：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/`，含 `include/sdurws/ird/runtime/`、`src/`、`test/`、`testdata/`、`out/test-evidence/wp-xx/<run-id>/`（AGENTS §3）。允许依赖 WP-03 core、RobWork/RobWorkSim 适配 API 和标准库；禁止 Qt Widgets、WP-13 业务 UI、其他 WP 私有头、直接写项目 revision 或自行定义诊断枚举。

目标：`sdurws_ird_runtime`、`sdurws_ird_runtime_test`、`sdurws_ird_runtime_contract_test`。

## 4. 冻结公共模型

`CanonicalKinematicModel` 包含 `projectId`、`revisionId`、`robotDesignId`、`baseFrame`、`links[]`、`joints[]`、`tools[]`、`environments[]`、`sourceFormat`、`algorithmVersion`；`sourceFormat` 枚举冻结为 `StandardDH | ExplicitJoint | UrdfImport`（URDF 导入后为 `ExplicitJoint`）。每个 Joint 必填 `objectId`、`ownerScopeId`、`jointType`、`origin`、`axis`、`home`、`limits`、`childLinkId`；Joint 冻结字段集不含 `zero` 字段——规范 Zero ⇔ `q=0`、零位偏置全部折叠进 `OriginPose`（canonical-kinematics §3），被折叠偏置的溯源保留在源修订 JointDefinition 内，canonical 模型不重复保存；`home` 由 `RobotDesign` 的 `homePosition` 逐轴映射；轴以单位化有限三维向量表达，原始输入值和补偿变换分开保存。

编译入口：`CanonicalModelCompiler::compile(const RobotDesign& robot, const CompileContext& context) -> expected<CanonicalKinematicModel, CompileError>`；`CompileContext{projectId, revisionId, tools[], environments[]}` 由调用方（WP-13/WP-20）从项目修订装配——`RobotDesign` 不内嵌修订身份与工具/环境清单（persistence-schema §2.4 裁决）；canonical 模型中的 `projectId/revisionId` 取自 `CompileContext`。

`RuntimeNameBinding`：`objectId`、`ownerScopeId`、`objectKind`、`runtimeDeviceName`、`runtimeScopedName`、`localName`。机器人内部名称固定为 `<runtimeDeviceName>.<localName>`；WORLD 和外部环境保持全局命名，不加机器人前缀。映射必须一一对应，未知、歧义、双前缀、旧前缀和去前缀重名均拒绝。

`CompiledRobotArtifacts` 同时包含 canonical、names、WorkCell::Ptr、DynamicWorkCell::Ptr、compileDiagnostics 和 source identity；任一工件失败则整个结果为空，不得返回部分指针。

## 5. 编译和适配数据流

```text
RobotDesign/URDF/DH/ExplicitJoint
  -> validate units, axes, IDs and chain
  -> normalize to CanonicalKinematicModel (SE(3), SI)
  -> allocate deterministic RuntimeNameMap
  -> compile WorkCell and DynamicWorkCell in isolated builders
  -> bind Collision/Proximity/geometry/limits/dynamics by objectId
  -> cross-check both artifacts and names
  -> publish CompiledRobotArtifacts atomically
```

规范变换链以 `architecture/canonical-kinematics.md` §2～§3 冻结口径为准：`T_parent_child(q) = OriginPose · Motion(â, q)`，零位偏置只允许在编译边界折叠进 `OriginPose`，禁止 `Origin * AxisRotation(q-zero)` 等任何双偏置变体；固定关节不产生可动轴。RobWork 只接受局部 Z 时，适配器增加不可见补偿 frame/link，并将视觉/碰撞几何、质心和惯量按同一补偿变换转换；Canonical 原始 Origin/Axis 永不改写。Zero、Home、有限边界和固定 100 个状态必须使用第 15.3 节冻结容差。

## 任务

| 任务 | 独立产出 | 任务卡 |
| --- | --- | --- |
| WP-06-T01 | 规范 SE(3) 模型和 DH/ExplicitJoint 等价编译 | [T01](../agent-tasks/WP-06-T01-canonical-model.md) |
| WP-06-T02 | objectId 与运行时作用域名双向映射 | [T02](../agent-tasks/WP-06-T02-name-map.md) |
| WP-06-T03 | WorkCell/DWC/名称全成全败双编译 | [T03](../agent-tasks/WP-06-T03-dual-compile.md) |
| WP-06-T04 | 任意轴、连续轴和棱柱轴适配 | [T04](../agent-tasks/WP-06-T04-axis-adapter.md) |
| WP-06-T05 | 重命名、历史名称和前缀静态扫描 | [T05](../agent-tasks/WP-06-T05-rename-scan.md) |

依赖：T01 → T02 → T03；T04 依赖 T01/T03；T05 依赖 T02/T03。每张任务卡一个 worktree、分支和提交。

## 7. 失败分类与统一行为

- 输入错误：空链、非法轴、重复 ID/名称、非有限值、非法关节类型；返回 Input 诊断，不生成运行时工件。
- 工程不可行：RobWork 不支持的关节/惯量、缺少几何或动力 Link；返回 Engineering/DataInsufficient，不返回部分编译结果。
- 系统错误：第三方构造、线程或资源失败；返回 System 诊断，释放已创建对象并保持调用方旧工件不变。

## 验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_runtime_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_runtime(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
```

模型测试使用 QCoreApplication 或无 Qt 入口；若包含 GUI 消费者，按 Windows Qt 规则单独启动并设置 `QT_QPA_PLATFORM=windows`。

## 8. 测试、迁移和证据

测试覆盖三入口等价、名称黄金集、任意轴、连续/棱柱轴、零/家位置、边界状态、双编译 failpoint、重命名、旧前缀扫描和确定性重复编译。固定输入、线程和版本时 canonical JSON、名称表、artifact 清单和诊断顺序必须一致。

旧运行时名称链路先保留只读适配器，契约测试通过后标 Migratable；无法证明前缀语义的标 Rewrite/EvidenceOnly。证据包含输入 revision/snapshot、source format、artifact hash、名称表、状态矩阵、RobWork 版本、命令日志和独立评审。

## 退出条件

A-GATE-06 阶段 A 链路和 AT-01、AT-15、AT-16 的模型/名称断言通过；AT-18 仅按阶段 B 规定子集验证。RuntimeNameMap 双向一一对应；WorkCell/DWC 任一失败均无可提交工件；规范与运行时 FK、轴、几何、质心和惯量满足冻结容差；静态扫描确认前缀逻辑只有 resolver/adapter 所有目录。
