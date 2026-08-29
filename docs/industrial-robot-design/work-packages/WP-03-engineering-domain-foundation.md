# WP-03 工程领域基础实施计划

> 阶段/发布：阶段 A 交付 / R1。
> 负责范围：无 Qt Widgets 的核心领域库，提供所有后续 WP 共用的单位、姿态、身份、来源、状态和正式可行语义。
> 责任分离：核心实现者、契约测试者和领域评审者必须是不同执行上下文。

## 1. 目标与非目标

**目标：** 建立不可变、可序列化、无运行时依赖的领域类型，使后续模块不再自行定义单位、枚举、身份或工程状态。
- 目标交付：sdurws_ird_core、公共头文件、JSON 适配、领域校验、参数化测试和依赖边界报告。
- 完成定义：所有公共枚举只有一个定义；非法单位/非有限数/缺引用不能默认为 0；核心测试不创建 QApplication。

**非目标：** 不实现 RobWork WorkCell 编译、项目保存、碰撞、调度、算法评估或 Widget；不保存运行时设备全名和 Qt 类型。

## 2. 架构边界与依赖

调用链固定为：外部适配输入 → core 值类型/校验 → 项目/快照/评估模块消费。core 只向下依赖 C++ 标准库和允许的数学基础，不向上依赖 WP-04～12。

| 层 | 允许内容 | 禁止内容 |
| --- | --- | --- |
| values | SI 值类型、Pose、Axis、Limit、Identity | 裸 double 跨领域传递 |
| identity | ObjectId、ownerScopeId、localName、来源 | 从名称推导身份 |
| semantics | outcome、engineeringStatus、evidence、可行谓词 | 各插件私有状态 |
| aggregates | RobotDesign 等引用骨架 | RobWork 指针、Widget、当前选择 |
| json adapter | 规范 JSON 往返 | 把 JSON 字符串当领域对象 |

## 3. 文件与构建目录

RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/
├─ include/sdurws/ird/core/
│  ├─ EngineeringUnits.hpp
│  ├─ EngineeringPose.hpp
│  ├─ ObjectIdentity.hpp
│  ├─ ValueProvenance.hpp
│  ├─ EvaluationSemantics.hpp
│  ├─ DomainObjects.hpp
│  ├─ DomainValidation.hpp
│  └─ DomainJson.hpp
├─ src/DomainJson.cpp、DomainValidation.cpp
├─ test/DomainValuesTest.cpp、IdentityTest.cpp、SemanticsTest.cpp
└─ CMakeLists.txt

目标：sdurws_ird_core、sdurws_ird_core_test。公共头安装到 include/sdurws/ird/core；测试和夹具不安装。

## 4. 类型和字段契约

### 4.1 数值类型

| 类型 | 单位 | 合法性 |
| --- | --- | --- |
| Length | m | finite；按需求范围校验 |
| Angle | rad | finite；显示层才转度 |
| Mass | kg | finite 且非负 |
| Time | s | finite 且非负 |
| Power | W | finite；正负号保留 |
| RotationalTorque | N·m | 只用于转动关节 |
| LinearForce | N | 只用于移动关节 |

构造函数必须拒绝 NaN/Infinity；范围错误返回结构化 ValidationError。禁止隐式从 double 构造跨单位类型。

### 4.2 身份和来源

ObjectIdentity 包含 objectId、ownerScopeId、localName。objectId 为规范化小写 UUID 或内容 ID，不由名称派生；同一 ownerScopeId 内 localName 唯一；删除后 ID 不复用。

ImportOrigin 保存 sourceType、sourceUri、sourceHash、sourceRevision；ValueProvenance 保存 valueKind、confidence、method、authoritative。两者正交，导入来源变化不自动覆盖值可信度。

### 4.3 评估语义

EvaluationMode：Quick、Verified；EvidenceLevel：Screening、PreliminaryDesign、ExternallyValidated；ExecutionOutcome：Completed、Canceled、Failed、Interrupted；EngineeringStatus：Pass、Warning、Infeasible、DataInsufficient、NotEvaluated；PayloadCompleteness：Complete、Partial、None。

正式可行必须同时满足 Completed + Pass + Complete、所有 Must 硬约束通过和 RequiredEvidenceProfile 满足。Completed 不等于正式可行；Quick、Partial、DataInsufficient 不能通过。

## 5. 数据流与逻辑

1. 外部适配器创建输入值类型，构造阶段完成有限数、单位和范围检查。
2. DomainValidation 检查聚合引用、作用域唯一性、目标主链和枚举组合。
3. DomainJson 将值类型按固定字段写入 JSON；读取时先 schema/version，再字段和引用校验。
4. 下游 WP 只接收 const 值对象或不可变 Envelope；不得保存 UI 指针或运行时名称。
5. 诊断包含 code、category、objectId、actual、expected、cause、action；缺 objectId 时显式为空。

## 6. JSON 示例和兼容

{
  "objectId": "9f7b...lowercase",
  "ownerScopeId": "robot-001",
  "localName": "Joint1",
  "jointType": "Revolute",
  "origin": {"translationM": [0, 0, 0.35], "quaternion": [1, 0, 0, 0]},
  "axis": [0, 0, 1],
  "provenance": {"valueKind": "Imported", "confidence": "Verified"}
}

JSON number 必须有限；姿态真值使用四元数/刚体变换，RPY 只在输入或显示适配层存在。未知未来 schema 拒绝，已知版本由显式升级器处理。

## 7. 实施步骤

### WP-03-T01 工程数学和类型安全
先写单位混用、非有限值、零轴、无效旋转、转动/移动广义力混用失败测试；实现值类型、姿态、旋转测地角和有向轴误差；确认 RPY 不进入持久化结构。

### WP-03-T02 稳定身份与来源
先写重命名保持 objectId、重复 localName、跨作用域引用和删除不复用测试；实现 Identity、ImportOrigin、ValueProvenance 和 JSON 往返。

### WP-03-T03 全局评估语义
参数化覆盖全部 outcome/status/payload 合法与非法组合（以 architecture/evaluation-semantics.md §2/§4 为准）；实现 RequiredEvidenceProfile 和唯一正式可行谓词；验证 Canceled/Failed/Interrupted、Partial、DataInsufficient、NotEvaluated 和 Quick 不被误判为正式通过，Completed+Warning 仅在允许警告类别内可正式可行。

### WP-03-T04 领域聚合 Schema
定义 RobotDesign、ToolDefinition、EnvironmentModel、EngineeringRequirements、LoadCase、DriveTrainDesign、AnalysisConfiguration、CatalogRef 和 OptimizationStudyDefinition 的身份/引用骨架；不实现业务算法字段。

### WP-03-T05 依赖边界扫描
扫描公共头和 CMake，拒绝 QWidget、QApplication、旧插件头、运行时名称拼接、可变全局状态和未登记依赖；只链接 core 完成模型测试。

## 8. 测试矩阵

| 场景 | 输入 | 预期 |
| --- | --- | --- |
| 单位错误 | Angle 传入 Length | 编译或校验失败，不隐式转换 |
| 非有限 | NaN/Infinity | ValidationError，payload=None |
| 身份 | 改 localName | objectId 和引用不变 |
| 作用域 | 重复 localName/跨域无 scope | 稳定诊断，不取首个 |
| 状态 | Canceled + Pass + Complete | 非法组合被拒绝 |
| 可行性 | Completed + Warning（警告类别全部在 allowedWarningCategories 内 / 任一未允许） | 前者 isFormallyFeasible=true；后者 false 且 gaps 列出未允许警告类别 |
| JSON | 有效聚合 | 字段、枚举、引用往返一致 |
| 边界 | QWidget/旧头 | check-boundaries 非零 |

## 验证

WP-03 验证必须在 Visual Studio x64 环境构建 sdurws_ird_core_test，并运行模型、JSON 契约和依赖边界测试；测试过程不得创建 QApplication 或 Widget。在仓库根目录执行：

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_core_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1

模型测试只使用 QCoreApplication 或无 Qt 入口；GUI 平台规则不适用于 core。

## 10. 迁移、评审和证据

旧类型先通过只读适配器迁移；满足黄金数据和契约测试才标 Migratable，否则 Rewrite/EvidenceOnly。删除旧类型前保留输入和差异证据。

必须提交公共头清单、CMake 依赖图、JSON 样例、参数化测试日志、边界扫描报告、迁移 verdict 和独立评审记录。

## 退出条件

- 公共单位、身份、来源、状态和正式可行谓词各只有一个定义。
- 非有限值、非法单位、缺引用和非法状态组合明确失败。
- core 无 Qt Widgets、旧插件头、RobWork 运行时对象和可变全局状态。
- JSON 往返、黄金数据和边界扫描全部通过。
- 后续 WP 可直接引用 core 公共类型，无需重新决定字段、枚举和单位。

## 任务卡索引

- [WP-03-T01 工程数学和类型安全](../agent-tasks/WP-03-T01-math-types.md)
- [WP-03-T02 稳定身份与来源](../agent-tasks/WP-03-T02-identity-provenance.md)
- [WP-03-T03 全局评估语义](../agent-tasks/WP-03-T03-evaluation-semantics.md)
- [WP-03-T04 领域聚合 Schema](../agent-tasks/WP-03-T04-aggregate-schema.md)
- [WP-03-T05 依赖边界扫描](../agent-tasks/WP-03-T05-dependency-boundary.md)
