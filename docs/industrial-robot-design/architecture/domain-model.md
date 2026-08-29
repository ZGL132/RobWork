# 领域模型契约

## 1. 作用域与身份

首版一个项目只能有一个 `RobotDesign`。每个持久化对象都有稳定 `objectId` 和 `ownerScopeId`；首版 `ownerScopeId` 必须等于项目唯一 `robotId`。名称不是身份，重命名不得改变 `objectId`。

```cpp
using ObjectId = std::string; // canonical lowercase UUID or content id
using RobotId = ObjectId;

struct ObjectIdentity {
    ObjectId objectId;
    ObjectId ownerScopeId;
    std::string localName;
};
```

校验：ID 非空且规范化；同一作用域内 `localName` 唯一；跨作用域引用必须显式携带目标作用域；删除的 ID 永不复用。

**ID 与标识格式（冻结）**：`objectId`/`RobotId` 为小写 UUIDv4；内容身份（`sliceId`、`sha256`、`contentId`、`writeSetFingerprint` 等）为 64 位小写十六进制（SHA-256）。`ImportOrigin` 值域冻结为 `TemplateCreated / ManuallyCreated / UrdfImport / WorkCellImport / OtherInput`；`ValueProvenance` 值域为 `Estimated / Imported / UserSpecified / Measured`。

## 2. 核心对象

| 对象 | 必填字段 | 不变量 |
| --- | --- | --- |
| `RobotDesign` | identity、joints、targetChainId、authority | 4～7 个可动关节；恰好一个目标主链 |
| `JointDefinition` | type、origin、axis/固定标记、limits、provenance | 可动轴有限且非零；固定关节不保存轴语义 |
| `ToolDefinition` | identity、flangeRef、tcpPose、loadRef | TCP 不复制几何；引用必须可解析 |
| `EnvironmentModel` | identity、meshes、collisionRefs | 路径只能指向项目资源区 |
| `EngineeringRequirements` | tasks、regions、constraints、loads | Must/Should 状态可计算且来源可追溯 |
| `ProjectRevision` | revisionId、parent、robotRef、objects、policyRef | 首版恰好一个 `robotRef`；应用命令原子产生新修订 |

## 3. 枚举与状态

评估正交枚举（`EvaluationMode`、`EvidenceLevel`、`ExecutionOutcome`、`EngineeringStatus`、`PayloadCompleteness`、`ArtifactIntegrity`、`ResultCurrentness`）、合法组合表和正式可行谓词的**唯一权威是 [evaluation-semantics.md](evaluation-semantics.md)**；本文件不再复述枚举定义，避免第二套口径。任何消费者需要枚举值域时引用该契约。

## 4. 数值规则

内部使用 SI 单位。所有浮点输入必须有限；角度 rad、长度 m、质量 kg、时间 s、功率 W。转动广义力为 N·m，移动广义力为 N，不允许裸 `double` 跨类型传递。`L*` 必须为有限正值，无法得到有效回退时返回 `DataInsufficient`。补充冻结：体积类指标单位 m³；成本类指标单位记录数值 + ISO 4217 货币代码（如 `{"value": 1250.0, "currency": "CNY"}`），不以裸数传递；`kg·m²` 仅用于惯量张量分量。
