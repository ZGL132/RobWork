# D3 机器可验证 Schema 层

> 位置：`docs/industrial-robot-design/schemas/`（相对仓库根 `RobWork/docs/industrial-robot-design/schemas/`）
> 状态：`Proposed`（与各契约文档同处 IRD-D2 检查点）
> 权威边界：本目录是 `.rwdesign` 持久化文件与公共值对象的**字段级机器可验证形态**。语义唯一权威仍是各架构契约文档；Schema 与契约冲突时以契约文档为准并立即修正本目录（见 `architecture/persistence-schema.md` 头注）。

## 1. 运行校验

在 `schemas/` 的父目录（即 `docs/industrial-robot-design/`）下运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\schemas\validate-schemas.ps1
```

脚本用 `$PSScriptRoot` 自行解析位置，也可在任意工作目录用绝对路径调用。要求 Windows PowerShell 5.1 兼容（未用三元运算符、`??`、链式运算符）。全部通过时输出统计行并 `exit 0`；任何意外（含非法 JSON、BOM、CRLF、非有限数、未知 Schema 关键字、示例缺失、非法示例未被拒绝）`exit 1`。

脚本自身实现说明（工程细节，不影响 Schema 契约）：

- `validate-schemas.ps1` 为 **UTF-8 带 BOM**（PS 5.1 对无 BOM 脚本按 ANSI 读取，中文注释会破坏解析）；JSON 文件全部为无 BOM UTF-8 + LF，由脚本逐文件检查。
- PS 5.1 `ConvertFrom-Json` 会把**单元素数组解包为标量、空数组读为 null**；校验器在 schema 声明数组语义处自动归一化还原，不改变 Schema 契约本身。
- 校验器对 `result-envelope` 额外执行 evaluation-semantics §2 合法组合表镜像检查（`Completed + {Pass|Warning|Infeasible|DataInsufficient} + Complete` 或 `{Canceled|Failed|Interrupted} + NotEvaluated + {Partial|None}`），作为子集表达力缺口的演示性合同测试。

## 2. 关键字子集纪律（冻结）

校验器只支持以下 JSON Schema 关键字：

```
$schema  $id  title  description  type  properties  required  additionalProperties
enum  items  minimum  maximum  minLength  maxLength  pattern  minItems
```

禁止使用：`$ref`（跨文件引用）、`oneOf/anyOf/allOf/not`、`format`、`default`、`const`、`nullable`、`definitions/$defs`、数组形式 `type`、`exclusiveMinimum/exclusiveMaximum`、`additionalItems` 等。每个 schema 文件自包含；小的公共定义（如 ObjectId 的 pattern）在各文件内重复。

补充约定：

- `type` 单值。可空字段（如 `parentRevisionId`）不声明 `type`，只用 `pattern` 约束非 null 值——`pattern` 按标准语义仅作用于字符串，null 通过。
- 所有 object 一律 `additionalProperties: false`（契约：未知字段保留是加载器行为，Schema 校验的是已知必填集合的完整性；未知字段的保留/拒绝策略由实现按 `persistence-schema.md §3` 执行）。
- 无法用子集表达的语义（见 §4）在 `description` 中指向契约章节，由代码契约测试保证；`validate-schemas.ps1` 对 `result-envelope` 额外实现 evaluation-semantics §2 合法组合表的镜像检查。
- `schemaVersion` 用 `enum: [1]` 表达"当前唯一版本"。
- 所有 JSON 文件 UTF-8（无 BOM）、LF、禁止 NaN/Infinity。

## 3. 权威来源映射表

| Schema 文件 | 校验对象 | 权威来源（章节） |
| --- | --- | --- |
| `project.schema.json` | `.rwdesign/project.json` | `architecture/persistence-schema.md` §2.2 |
| `project-revision.schema.json` | `revisions/<rid>/manifest.json` | `architecture/persistence-schema.md` §2.3、§2.4 |
| `robot-design.schema.json` | `RobotDesign`（含 `JointDefinition`、连杆物性、原生 DH） | `architecture/domain-model.md` §1～2；`architecture/canonical-kinematics.md` §1～§6、§8；`requirements.md` §7.1～7.2 |
| `engineering-requirements.schema.json` | `EngineeringRequirements`（任务/区域/约束/负载） | `architecture/domain-model.md` §2；`requirements.md` §7.2、§8.2 |
| `engineering-policy.schema.json` | `EngineeringPolicySet`（含 `CollisionPolicy`、`RequiredEvidenceProfile`） | `requirements.md` §6.6、§6.7.2、§15.3；`architecture/evaluation-semantics.md` §3 |
| `runtime-name-map.schema.json` | `RuntimeNameMap`（绑定数组） | `requirements.md` §6.7.1、§7.2；`architecture/public-interfaces.md` §2 |
| `analysis-snapshot.schema.json` | `AnalysisSnapshot`（含 `EvaluatorDependencyManifest`） | `architecture/public-interfaces.md` §7；`requirements.md` §7.1 |
| `result-envelope.schema.json` | `ResultEnvelope`（`results/<run-id>/result-<attemptId>.json`） | `architecture/public-interfaces.md` §7；`architecture/evaluation-semantics.md` §1～§2；`architecture/execution-model.md` §5 |
| `evidence-bundle.schema.json` | `EvidenceBundle`（`results/<run-id>/evidence-<attemptId>/`） | `architecture/public-interfaces.md` §7；`requirements.md` §7.2 |
| `optimization-study.schema.json` | `OptimizationStudyDefinition`（变量/约束/指标/目标/预算/算法） | `architecture/candidate-compilation.md` §1、§3、§6；`requirements.md` §7.2、§9.1～9.3、§8.7.1 |
| `candidate-patch.schema.json` | `CandidatePatch`（含 `DesignVector` 条目数组） | `architecture/candidate-compilation.md` §2、§4～§5 |
| `checkpoint.schema.json` | `Checkpoint`（`checkpoints/<run-id>/<attempt-id>/`） | `architecture/execution-model.md` §1、§4；`architecture/persistence-schema.md` §4 |
| `catalog/catalog-manifest.schema.json` | 目录包清单 | `requirements.md` §8.6（SEL-01/02）；`module-design/secure-io.md` §4 |
| `catalog/column-dictionary.schema.json` | 目录 CSV 列字典 | `requirements.md` §8.6（SEL-01/02）；`module-design/secure-io.md` §3 |

枚举值域唯一来源：`architecture/evaluation-semantics.md` §1（`ExecutionOutcome`/`EngineeringStatus`/`PayloadCompleteness`/`ArtifactIntegrity`/`ResultCurrentness`）与 `requirements.md` §6.6（`EvaluationMode`/`EvidenceLevel`）；`ValueProvenance`/`ImportOrigin` 来源 `requirements.md` §7.1。规范名称（如 `AnalysisConfiguration` 而非 `AnalysisConfig`、`OptimizationStudyDefinition` 而非 `OptimizationStudy`）来源 `architecture/symbol-registry.md` §4。

## 4. 子集无法表达、由代码契约测试保证的语义

| 语义 | 契约位置 | 处理方式 |
| --- | --- | --- |
| outcome × engineeringStatus × payloadCompleteness 合法组合（60 选 8） | `evaluation-semantics.md` §2 | Schema 只声明枚举值域；`validate-schemas.ps1` 内置组合表镜像检查（演示用），正式由 C++ 构造边界 60 组合测试保证 |
| 可动关节数 4～7、恰好一个目标主链、qIndex 连续 | `canonical-kinematics.md` §4、`domain-model.md` §2 | `joints` 只约束 minItems 4；计数与连续性由代码校验 |
| 可动关节必须带 `axis`/`limits`，固定关节不得带 | `requirements.md` §7.1 | 条件必填无法用子集表达；`description` 声明，代码校验 |
| `lower < upper`、`domain.min ≤ domain.max`、正质量、正惯量 | 各契约 | 子集只有含边界 `minimum/maximum`，严格不等式由代码校验 |
| 同一对象对不得同时属于 excludedPairs 与 allowedContactPairs | `requirements.md` §6.7.2 | 跨字段互斥由代码校验 |
| writeSet 两两交集为空、dependencies 无环、绑定已注册 | `candidate-compilation.md` §3 | 图/集合语义由代码校验 |
| runtimeScopedName 与 device+localName 一致、绑定双向一一 | `requirements.md` §6.7.1 | 由 ResolverContractTest 保证 |
| 四元数单位性（\|‖q‖−1\| ≤ 1e-12）与符号规范化 | `canonical-kinematics.md` §6 | 数值语义由代码校验；Schema 只冻结 `{x,y,z,w}` 对象形态 |
| designVector 条目 numericValue/stringValue 恰居其一（按 valueType） | `candidate-compilation.md` §2 | 条件必填由代码校验（Schema 已保证出现的字段类型正确） |
| LoadCase 事件 anchorTaskId 与 duration 恰居其一（每项至少一个时间字段） | `requirements.md` §7.2、§8.2；`module-design/requirements-definition.md` §3 | 条件必填由代码校验 |
| section.dimensions 按 sectionKind 的必填键集合（Solid→diameter；Hollow→outer/inner；Rect→width/height），且数值 > 0、壁厚为正 | `requirements.md` §9.1；`module-design/robot-modeling.md` §5 | 条件必填与严格不等式由代码校验 |

## 5. 示例

- `examples/<name>.example.json`：每个 schema 一个合法示例（六轴机器人、SI 单位、物理合理数值）。命名必须与 `<name>.schema.json` 的 `<name>` 一致。
- `examples/invalid/<schema-name>.<原因>.json`：故意非法示例，**必须**被校验器拒绝。当前 3 个：
  - `result-envelope.illegal-combination.example.json`：`Completed + NotEvaluated + Complete`（evaluation-semantics §2 非法组合，由脚本组合检查拒绝）；
  - `candidate-patch.missing-field.example.json`：mutation 缺 `writeSet` 且顶层缺 `writeSetFingerprint`（writeSet 冲突检测所需字段缺失，`required` 拒绝）；
  - `project.missing-required.example.json`：缺必填 `robotId`（`required` 拒绝）。

## 6. 版本与升级

`schemaVersion` 当前为 1（`enum: [1]`）。升级遵循 `architecture/persistence-schema.md` §5：逐版本显式升级器、未来版本只读拒绝（`IRD-PERSIST-FUTURE-SCHEMA`）。修改任何 schema 的字段名/语义前必须先修改对应契约文档。

## 7. 变更记录

- **2026-08-29（D5 修正，小版本）**：
  - `engineering-requirements.schema.json`：`tasks[]` 新增必填 `tcpRef`（ToolDefinition objectId 或 RobotDesign 默认 TCP 的 objectId，module-design/requirements-definition.md §3 提名）；`loads[]` 新增可选 `events[]`（Grip/Release/Dwell 事件序列，`anchorTaskId`/`duration` 二选一时间字段 + 可空 `payloadRef`）。
  - `robot-design.schema.json`：`links[]` 新增可选 `section`（`sectionKind` + `dimensions` 按类型的特征尺寸（直径制，m）+ `materialRef`；robot-modeling.md §5 解析估算以半径代入）。
  - `candidate-patch.schema.json`：`derivedRecomputation.unit` 枚举补 `kg·m2`（惯量张量分量量纲）。
  - 示例同步更新；`optimization-study` / `candidate-patch` 示例中的截面变量路径由 `links[2].sectionSize` 对齐为 `links[2].section.dimensions.outerDiameter`。
  - 版本处置：D3 层尚无任何持久化数据（实现未启动），字段变更并入 `schemaVersion: 1`，不产生升级器；首个实现落地后再引入 v2+ 升级机制。
