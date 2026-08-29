# 核心领域基础模块详细方案（core-domain）

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；治理状态：Proposed
- 负责 WP：WP-03；阶段/发布：阶段 A / R1；任务卡：agent-tasks/WP-03-T01～T05
- 架构契约：`architecture/domain-model.md`、`architecture/evaluation-semantics.md`、`architecture/canonical-kinematics.md`（§6）、`architecture/public-interfaces.md`（§0/§7）、`architecture/symbol-registry.md`
- 代码前置：WP-00（构建/门禁入口由 WP-01 交付）；无其他代码依赖

## 1. 模块职责

提供无 Qt、无 RobWork 运行时对象的不可变 SI 值类型、身份与来源记录、评估正交枚举的唯一 C++ 定义、合法组合谓词、`isFormallyFeasible()`、领域聚合引用骨架和规范 JSON 适配。枚举值域、合法组合表与谓词语义的唯一权威是 `evaluation-semantics.md` §1～§4（`EvaluationMode`/`EvidenceLevel` 值域另见 requirements §6.6 与 evaluation-semantics §3），本模块只落代码，不复述契约表。非目标：WorkCell 编译、项目仓库、碰撞、调度、业务算法、Widget；不把 RPY、运行时名称或界面选择持久化为真值。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/
  include/sdurws/ird/core/
    EngineeringUnits.hpp   EngineeringPose.hpp   ObjectIdentity.hpp
    ValueProvenance.hpp    EvaluationSemantics.hpp   DomainObjects.hpp
    DomainValidation.hpp   DomainJson.hpp
  src/DomainValidation.cpp   src/DomainJson.cpp
  test/DomainValuesTest.cpp  test/IdentityTest.cpp  test/SemanticsTest.cpp
  testdata/domain/{valid-aggregate,invalid-axis,invalid-quaternion,unknown-version}.json
  evidence/WP-03/
```

CMake target：`sdurws_ird_core`、`sdurws_ird_core_test`（与 WP-03 §3 一致；无独立 contract_test，契约断言并入三个测试文件）。允许依赖：C++ 标准库＋WP-01 批准的数学基础；禁止：Qt（库本体零 Qt 依赖，测试入口不创建 QApplication）、RobWork/RobWorkSim、旧插件头与 `sdurws_robotmodelbuilder*` 等旧目标。

## 3. 数据与接口

公共符号只使用 symbol-registry 登记名：`ObjectId/RobotId/ObjectIdentity`（SYM-ID-001～003）、`RobotDesign/JointDefinition/ToolDefinition/EnvironmentModel` 等（SYM-DOM-002～005）、`EvaluationMode/EvidenceLevel/ExecutionOutcome/EngineeringStatus/PayloadCompleteness`（SYM-STA-001～005）、`RequiredEvidenceProfile`（SYM-EVI-007）；字段与签名以 `public-interfaces.md` §7、`domain-model.md` §1～§4 为准，本文不复述。`ArtifactIntegrity`/`ResultCurrentness` 的 C++ 定义归 WP-05（SYM-STA-007、SYM-EVI-006），本模块不定义。

模块私有类型（core 公共头内、未登记注册表）：

| 类型（模块私有） | 字段 | 规则 |
| --- | --- | --- |
| 单位包装 `Length/Angle/Mass/Time/Power` | `double value` | 显式构造；finite；Mass/Time 非负；禁止隐式 double 转换 |
| `RotationalTorque`/`LinearForce` | `double value` | N·m 仅转动、N 仅移动，互不转换（domain-model §4） |
| `InertiaComponent`/`Volume`/`Money` | `double`；Money 另含 `currency` | kg·m² 仅惯量分量；m³；Money 必带 ISO 4217，不以裸数传递 |
| `ImportOriginRecord` | `sourceType, sourceUri, sourceHash, sourceRevision` | sourceType 值域冻结五值（domain-model §1） |
| `ValueProvenanceRecord` | `valueKind, confidence, method, authoritative` | valueKind 值域冻结四值（domain-model §1）；与 ImportOrigin 正交持久化 |
| `FeasibilityGap` | `evaluatorId, failedCondition, detail` | failedCondition 为稳定字符串枚举（对应 evaluation-semantics §4 各 return gap 分支） |
| `IdFormat` 校验函数 | `isValidObjectId / isValidContentId` | 小写 UUIDv4 / 64 位小写 hex（domain-model §1 冻结） |

## 4. 调用与状态

时序：外部适配器构造值类型 → `DomainValidation` 聚合校验 → `DomainJson` 序列化/读回 → 下游 WP 消费 const 值对象。校验顺序（模块私有冻结）：值＝有限性→单位/范围→组合；聚合＝ID 格式→ownerScopeId→localName 唯一→引用存在→目标主链→枚举组合；JSON＝schemaVersion→必填→枚举→数值→引用→未知字段保留再序列化（persistence-schema §3）。错误一律 `expected<T, Diagnostic>`（CTR-DIA-001）；模块私有错误码（建议登记入 WP-09 总目录）：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-CORE-VALUE-INVALID` | 非有限数、越物理范围、单位/类型混用 | Input | Error | 修正输入后重新构造 |
| `IRD-CORE-IDENTITY-INVALID` | ID 格式非法、作用域内 localName 重复 | Input | Error | 修正 ID 或改名（不改 objectId） |
| `IRD-CORE-REFERENCE-UNRESOLVED` | 聚合引用缺失或跨作用域未携带目标作用域 | Input | Error | 补齐被引对象或显式作用域 |
| `IRD-CORE-COMBINATION-ILLEGAL` | outcome×status×payload 落在 evaluation-semantics §2 两类合法组合之外 | Input | Error | 按锚点用例重新落位后构造 |
| `IRD-CORE-SCHEMA-FUTURE` | JSON 携带未知未来 schemaVersion | Input | Error | 用支持版本打开或走升级链 |

## 5. 关键实现约定

- `isFormallyFeasible()` 逐条按 evaluation-semantics §4 伪代码实现，返回 `FeasibilityVerdict`（每个失败条件一条 `FeasibilityGap`）；`Completed + Warning` 仅当全部警告类别在 `allowedWarningCategories` 内才可行。谓词不重复实现硬约束逻辑（由评估器以 `Infeasible` 表达）。
- 四元数：`EngineeringPose` 持久化与计算接口只用单位四元数 `{"x","y","z","w"}` 对象形态；载入校验阈值、重归一化区间与符号规范化规则以 canonical-kinematics §6 为准；构造时统一规范符号，序列化不再二次处理；姿态误差用测地角并忽略正负号。RPY 仅存在于输入/显示边界的换算 helper，不进持久化结构。
- 确定性：`DomainJson` 输出规范 JSON（UTF-8、LF、无 BOM、键序固定、有限 number），相同内存对象产生逐字节相同输出；浮点往返误差 ≤ `1e-12`（persistence-schema §3）。
- 身份：`objectId` 不由名称派生；重命名保持 ID；删除不复用；跨项目复制/重导入必须换新 ID 并在来源记录原 ID（requirements §7.1）。
- `L*` 规则（domain-model §4）：无法得到有限正值回退时消费方必须返回 `DataInsufficient`；本模块只冻结该规则断言，不实现 `L*` 计算。

## 6. 测试与证据

| 测试文件 | 覆盖 |
| --- | --- |
| DomainValuesTest | 单位混用、NaN/Infinity、负质量、零轴（<1e-12）、非单位/反平行四元数、转动/移动力混用、Money 缺货币、JSON 往返 `1e-12` |
| IdentityTest | 重命名保 ID、复制/删除不复用、重复 localName、跨作用域引用、来源正交保存与覆盖估算保留原来源 |
| SemanticsTest | 60 组合构造（仅 §2 两类合法）、谓词正反例（含 Warning 类别边界、证据等级差一档）、profile 同 usageId 唯一 |

夹具以 `schemas/examples/robot-design.example.json`、`engineering-requirements.example.json` 为合法基线（先过 `schemas/validate-schemas.ps1` 再进往返断言）；`testdata/domain/` 只放故意非法样本。证据写入 `evidence/WP-03/`：任务 ID、需求 ID、提交 SHA、测试日志、边界扫描报告、独立评审记录。验证命令（双形式）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'
```

原生回退（WP-01 入口未交付时）：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"
```

边界扫描（WP-03-T05，脚本由 WP-01 交付）：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1`。

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| RobotModelSpec 中已验证纯计算部分 | 迁移（经 WP-02 黄金数据固定行为） | 黄金对照通过才标 Migratable |
| `JointTransformSpec` RPY 承载任意轴、隐式轴语义 | 重写为 `JointDefinition`＋显式 Axis | §13.3 消除项，禁止直接迁入 |
| 各插件私有单位转换/枚举/状态定义 | 删除，统一引用 core | 边界扫描零命中 |
| 旧目标 `sdurws_*` 头文件引用 | 不成为 core 依赖；阶段 B 后退出构建 | 依赖图审计 |
