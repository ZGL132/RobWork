# 运行时模型与名称模块详细方案

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；负责 WP：WP-06；阶段/发布：阶段 A / R1
- 最高权威：`architecture/canonical-kinematics.md`（变换链、q-zero、关节轴、四元数、R_c 补偿）；其余契约：`architecture/public-interfaces.md` §2/§7、`architecture/symbol-registry.md`、`architecture/testing-contract.md`；需求锚点：§6.7.1、§7.1～7.3、§15.3；任务卡：`agent-tasks/WP-06-T01～T05`

## 1. 模块职责

把权威参数化（`StandardDH`/`ExplicitJoint`；URDF 导入后为 `ExplicitJoint`）确定性编译为 `CanonicalKinematicModel`（SYM-KIN-004）、`RuntimeNameMap`（SYM-NAM-001）、WorkCell 与 DynamicWorkCell，并以 `CompiledRobotArtifacts`（SYM-KIN-005）全成全败发布。一切公式、坐标系、q-zero 与 R_c 裁决以 canonical-kinematics.md §1～§8 为准；本模块只规定实现次序、夹具、内部结构与校验清单。不实现 FK/IK 算法、碰撞策略、轨迹/动力学评估、GUI 与项目持久化。

## 2. 目录与构建

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
  evidence/WP-06/
```

CMake 目标：`sdurws_ird_runtime`、`sdurws_ird_runtime_test`、`sdurws_ird_runtime_contract_test`。允许依赖：WP-03 core、RobWork/RobWorkSim 稳定 API、标准库（代码前置仅 WP-03，与总纲 §5.2 一致）；禁止：Qt Widgets、WP-13+ 业务头、其他 WP 私有头、直接写项目 revision、定义公共端口的平行版本。`CanonicalModelCompiler`、`RobWorkModelAdapter`、`DynamicWorkCellAdapter` 为模块私有类型。

## 3. 数据与接口

- 编译入口裁决：`expected<CanonicalKinematicModel, CompileError> compile(const RobotDesign& robot, const CompileContext& context)`；`CompileContext{projectId, revisionId, tools[], environments[]}` 由调用方（WP-13/WP-20）从项目修订装配——`RobotDesign` 不内嵌修订身份与工具/环境清单（persistence-schema §2.4 对象头裁决的同一分工）。canonical 模型冻结字段集中的 `projectId/revisionId` 取自 `CompileContext`，`tools[]/environments[]` 以 `CompileContext` 传入内容编译。
- `IRuntimeNameResolver` 端口签名与 `IRD-NAME-*` 错误码以 public-interfaces §2 为准，本模块是其唯一实现；`RuntimeNameMap` 持久化以 `schemas/runtime-name-map.schema.json` 与 `schemas/examples/runtime-name-map.example.json` 为准。
- 绑定主键 `(ownerScopeId, objectId)`，值为 `runtimeDeviceName/localName/runtimeScopedName/objectKind`；`objectKind` 值域冻结（public-interfaces §2）：`Device/Joint/Link/Frame/FixedFrame/CompensationFrame/Tool/EnvironmentObject`。`<runtimeDeviceName>.<localName>` 拼接只允许出现在 resolver 实现内部；WORLD 与外部环境对象使用全局名。
- `CompiledRobotArtifacts` 同载 canonical、names、WorkCell::Ptr、DWC::Ptr、compileDiagnostics 与 source identity；任一工件失败整体为空，不返回部分指针，编译结果不自动写项目。

## 4. 调用与状态

```text
RobotDesign 权威参数化
  → 校验单位/轴/ID/链拓扑（任一失败即中止）
  → canonical 化（零位偏置只在此吸收进 OriginPose）
  → 分配 qIndex 与 RuntimeNameMap
  → 隔离 builder 分别构建 WorkCell、DWC
  → 按 objectId 交叉校验（见下清单）
  → 原子发布；或全败：释放全部临时指针并返回空工件＋诊断（调用方旧工件不变）
```

双编译交叉校验清单：device/joint/frame 集合与 objectId 一一对应；几何、collision/proximity 绑定、limits、mass、COM、inertia 按 objectId 双侧一致。错误矩阵：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-RUNTIME-AXIS-INVALID` | 轴范数 <1e-12、非有限、固定关节带轴语义 | Input | Error | 修正权威参数化后重编译 |
| `IRD-RUNTIME-DUAL-OFFSET` | 变换链出现第二偏置项（canonical-kinematics §3.2） | System | Error | 偏置折叠进 OriginPose 后重新提交 |
| `IRD-RUNTIME-NAME-COLLISION` | 名称表生成时重复绑定/去前缀重名/双前缀 | Input | Error | 修正名称后重新编译 |
| `IRD-RUNTIME-COMPILE-FAILED` | builder 抛错、返回空或 RobWork 构造失败 | System | Error | 保留调用方旧工件，按诊断修复后重试 |
| `IRD-RUNTIME-ARTIFACTS-MISMATCH` | 交叉校验任一项不一致 | Engineering | Error | 全部工件作废并返回诊断 |

解析期（非编译期）错误使用 public-interfaces §2 冻结的 `IRD-NAME-AMBIGUOUS`/`IRD-NAME-UNRESOLVED`/`IRD-NAME-DUPLICATE-PREFIX`，绝不取第一个匹配。

## 5. 关键实现约定

1. 链实现次序按 canonical-kinematics §2：先 `OriginPose`（吸收全部零位偏置，§3 裁决），再 `Motion(â, q)`；非单位 `T_Jm_C` 编译为 `FixedFrame` 序列；禁止任何双偏置变体（§3.2）。
2. R_c 适配按 §7 冻结阈值实现平行/反平行/一般三夹具；模块只实现夹具选择与帧插入次序（前置补偿帧 → RobWork Z 关节 → 后置补偿帧），公式不复制。补偿帧 `objectKind=CompensationFrame`、拥有稳定 objectId、不入 `q`；几何/COM/惯量绑规范坐标系；反向查询以规范链为准。
3. qIndex 按基座到法兰拓扑序仅分配给可动关节、从 0 连续编号（§4）；固定关节与 FixedFrame/CompensationFrame 不占位，`dim(q)` 等于可动关节数。
4. 四元数符号规范化按 §6 实现：`(w,x,y,z)` 顺序首个绝对值 >1e-12 的分量为正；序列化、缓存键与 `q ≡ −q` 逐字节一致测试共用同一实现；姿态误差用测地角并忽略正负号。
5. 重命名只更新名称表与 `runtimeDeviceName/localName`；objectId、canonical 物理内容、sliceHash 与历史快照不变；同一 objectId 禁止同时绑定新旧名，新模型不得残留旧前缀或双前缀。
6. 确定性与所有权：固定输入、算法版本、seed、线程数时 canonical JSON、名称表顺序、artifact 清单与诊断顺序逐字节一致；RobWork 指针只在隔离 builder 内由创建线程释放。

## 6. 测试与证据

| 测试 | 断言要点 |
| --- | --- |
| CanonicalModelTest | DH/Explicit/URDF 三入口 canonical 字段一致；双偏置夹具拒绝；qIndex 连续性 |
| NameMapTest | 双向一一对应；Arm/ArmA、重复 Joint1/TCP、双前缀、旧前缀、重命名 RobotB 夹具 |
| DualCompileTest | WorkCell/DWC failpoint 全败；交叉校验清单逐项；确定性重复编译 |
| AxisAdapterTest | 非 Z/非单位/反平行轴：规范链与适配链 FK 等价（§7 容差、§9 姿态集：Zero/Home/边界/固定 100 姿态） |
| RenameScanTest | 重命名后 sliceHash 与物理结果不变；前缀拼接/剥离静态扫描仅命中 resolver/adapter 目录 |
| ResolverContractTest | public-interfaces §2 契约：互逆、重命名后旧绑定消失 |

验证命令（脚本形式与原生回退）：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_runtime(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_runtime_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_runtime_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_runtime(_contract)?_test$"
```

证据包含输入 revision/snapshot 身份、sourceFormat、canonical JSON、名称表、artifact hash、RobWork 版本、容差报告与 failpoint 日志、独立评审签名。

## 7. 迁移与删除表

| 旧链路 | 处置 | 条件 |
| --- | --- | --- |
| `stripDeviceScope` 等前缀拼接/剥离链路 | 只读适配器隔离 → 删除 | 静态扫描与 AT-18 阶段子集通过 |
| DH 参数直接进入运行时的旧编译链路 | Rewrite | 三入口等价黄金数据通过 |
| 无法证明来源的历史名称映射 | EvidenceOnly | 评审记录在案 |
