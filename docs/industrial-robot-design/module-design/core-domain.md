# 核心领域模型模块详细方案

- 方案版本：v0.2；对应需求基线：v0.7
- 负责 WP：WP-03；阶段/发布：阶段 A / R1
- 架构契约：architecture/domain-model.md、architecture/public-interfaces.md
- 任务卡：agent-tasks/WP-03-T01～T05

## 1. 目标与非目标

- 目标：提供无 Qt Widgets、无 RobWork 运行时对象的不可变值类型、身份来源、评估状态和聚合引用骨架。
- 非目标：不实现 WorkCell 编译、项目仓库、碰撞、调度、业务算法或 Widget；不把 RPY、运行时名称或当前选择持久化为真值。

## 2. 代码与构建

- 拥有目录：RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/。
- 公共头：include/sdurws/ird/core/EngineeringUnits.hpp、EngineeringPose.hpp、ObjectIdentity.hpp、ValueProvenance.hpp、EvaluationSemantics.hpp、DomainObjects.hpp、DomainValidation.hpp、DomainJson.hpp。
- 私有实现：src/DomainValidation.cpp、DomainJson.cpp；测试：test/DomainValuesTest.cpp、IdentityTest.cpp、SemanticsTest.cpp、SchemaTest.cpp。
- CMake target：sdurws_ird_core 和 sdurws_ird_core_test；只允许 C++ 标准库和已批准数学基础；不得链接 Qt Widgets 或旧插件。

## 3. 字段、单位和所有权

| 类型/对象 | 字段 | 单位/规则 |
| --- | --- | --- |
| Length | value | m，finite |
| Angle | value | rad，finite |
| Mass | value | kg，finite 且非负 |
| Time | value | s，finite 且非负 |
| RotationalTorque | value | N·m，只用于 Revolute |
| LinearForce | value | N，只用于 Prismatic |
| ObjectIdentity | objectId、ownerScopeId、localName | ID 小写规范化；作用域内名称唯一 |
| RobotDesign | identity、joints、targetChainId、authority | 4～7 可动关节；恰好一个目标主链 |

公共对象按值语义传递；跨线程只传 const/不可变对象；core 不拥有 Qt/RobWork 指针。构造失败返回 ValidationError，不能静默归零。

## 4. 调用和验证逻辑

外部适配器 → 值类型构造 → DomainValidation → 聚合引用校验 → DomainJson 序列化/反序列化 → 下游 WP 消费。

- 值类型校验顺序：有限性 → 单位类型 → 物理范围 → 组合约束。
- 聚合校验顺序：ID 格式 → ownerScopeId → localName 唯一 → 引用存在 → 目标主链 → 枚举组合。
- JSON 读取顺序：schemaVersion → 必填字段 → 枚举 → 数值 → 引用 → unknown/未来版本策略。
- 诊断必须包含 category、code、objectId、actual、expected、cause、action；对象不存在时 objectId 为空但不能用名称代替。

## 5. 状态和正式可行谓词

EvaluationMode：Quick/Verified；EvidenceLevel：Screening/PreliminaryDesign/ExternallyValidated；ExecutionOutcome：Completed/Canceled/Failed/Interrupted；EngineeringStatus：Pass/Warning/Infeasible/DataInsufficient/NotEvaluated；PayloadCompleteness：Complete/Partial/None。

合法正式可行条件为 Completed + Pass + Complete + 全部 Must 硬约束通过 + RequiredEvidenceProfile 满足。以下均为 false：Completed + Warning、Completed + Partial、Quick + Pass、DataInsufficient、缺少必需评估器或资源。

## 6. JSON 规则

{
  "objectId": "9f7b...lowercase",
  "ownerScopeId": "robot-001",
  "localName": "Joint1",
  "jointType": "Revolute",
  "origin": {"translationM": [0, 0, 0.35], "quaternion": [1, 0, 0, 0]},
  "axis": [0, 0, 1],
  "provenance": {"valueKind": "Imported", "confidence": "Verified"}
}

浮点采用 JSON number；NaN/Infinity 禁止写入；姿态真值用 quaternion/SE(3)，RPY 只能在输入/显示边界。已知版本通过显式升级器，未知未来版本拒绝。

## 7. 测试与证据

- 单元测试：单位混用、NaN/Infinity、负质量、零轴、无效旋转、转动/移动力混用。
- 契约测试：身份重命名、复制/删除不复用 ID、跨作用域引用、来源正交保存、所有状态组合。
- Schema 测试：合法聚合往返、未知枚举、缺字段、重复 ID、缺引用和浮点 1e-12 往返。
- 边界测试：公共头 QWidget/QApplication/旧插件头扫描；core 测试只使用 QCoreApplication 或无 Qt。
- 证据包含 Task ID、需求 ID、提交 SHA、编译 target、输入 JSON、实际诊断和评审者。

## 8. 迁移与扩展

旧领域类型通过只读适配器迁移；满足黄金数据和契约测试才标 Migratable，否则 Rewrite/EvidenceOnly。新单位、枚举或聚合字段必须先更新架构契约并提交 ADR；不得由业务 WP 自行增加默认值。

## 9. 验证命令

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_core_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
