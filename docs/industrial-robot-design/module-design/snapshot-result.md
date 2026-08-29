# 快照、结果与证据模块详细方案（snapshot-result）

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；治理状态：Proposed
- 负责 WP：WP-05；阶段/发布：阶段 A / R1；任务卡：agent-tasks/WP-05-T01～T05
- 架构契约：`architecture/public-interfaces.md` §3/§5/§7、`architecture/evaluation-semantics.md`、`architecture/execution-model.md` §3/§5、`architecture/persistence-schema.md` §4、`architecture/symbol-registry.md`
- 代码前置：WP-03、WP-04（构建/门禁入口由 WP-01 交付）

## 1. 模块职责与依赖裁决

拥有评估输入切片、不可变分析快照、结果包络、证据 bundle、追加式结果仓库与当前性计算；`ArtifactIntegrity`/`ResultCurrentness` 的 C++ 定义落在本模块（SYM-STA-007、SYM-EVI-006，值域以 evaluation-semantics §1 为准）。非目标：评估算法、调度、项目修订写入、报告渲染、重定义 WP-03 正式可行谓词。

**依赖裁决（覆盖 WP-05 计划 §3 中“允许依赖 WP-06 名称解析接口”的旧表述）**：代码依赖仅 WP-03（`sdurws_ird_core`，含组合谓词与 `isFormallyFeasible`）与 WP-04（查询端口＋追加协议原语）。`policyContentId`/`nameMapId` 是**不透明内容 ID**（64 位小写 hex），由 WP-07/WP-06 生成，本模块只做内容 ID 相等比较，不调用 `IRuntimeNameResolver`——WP-05 不依赖 WP-06/07 代码，仅契约引用（public-interfaces §2/§6）；WP-05 计划与 WP-05-T04 任务卡的相应表述需按本裁决回改。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/evidence/
  include/sdurws/ird/evidence/
    EvaluatorDependencyManifest.hpp   EvaluatorInputSlice.hpp
    AnalysisSnapshot.hpp   EvidenceBundle.hpp   ResultEnvelope.hpp
    IEngineeringEvaluator.hpp   IResultRepository.hpp   ResultQuery.hpp
    ResultAdmission.hpp   ResultCurrentnessService.hpp   EvidenceDiagnostics.hpp
  src/DependencyResolver.cpp   InputSlice.cpp   AnalysisSnapshot.cpp
      ResultEnvelope.cpp   ResultAdmission.cpp   ResultRepository.cpp
      ResultCurrentnessService.cpp   EvidenceJson.cpp
  test/InputSliceTest.cpp   SnapshotTest.cpp   ResultStatusTest.cpp
      ResultAdmissionTest.cpp   ReportReadinessTest.cpp
  testdata/evidence/{slice,results,late,invalid}/
  evidence/WP-05/
```

CMake target：`sdurws_ird_evidence`、`sdurws_ird_evidence_test`、`sdurws_ird_evidence_contract_test`（含 `ResultRepositoryContractTest` 与 `EvaluatorContractTest` 骨架，public-interfaces §9）。允许依赖：WP-03、WP-04、C++ 标准库、Qt Core（仅 `QJson*`）。禁止：Qt Widgets、调度器/策略/名称模块私有实现、写项目 revision、直改 `results/` 中已完成工件。

## 3. 数据与接口

`EvaluatorDependencyManifest/EvaluatorInputSlice/AnalysisSnapshot/ResultEnvelope/EvidenceBundle/ResultRef` 字段以 `public-interfaces.md` §7 为准（引用，不复制）；`IResultRepository` 四方法与 `IEngineeringEvaluator` 签名以 §5/§3 为准；持久化 JSON 形态见 `schemas/analysis-snapshot.schema.json`、`result-envelope.schema.json`、`evidence-bundle.schema.json`（示例 `schemas/examples/` 同名 `.example.json`）。模块私有类型：

| 类型（模块私有） | 字段 | 规则 |
| --- | --- | --- |
| 切片依赖条目 | `fieldPath, contentIdentity, semanticRole` | 只按声明字段取值；列表按 `fieldPath` UTF-8 字节序排序 |
| `InvalidationReason` | `fieldPath, semanticRole, reason` | 新旧切片按 semanticRole 比对；原因序列稳定 |
| `EvidenceGap` | `requirementId, missingEvaluator, missingResource, minimumLevel, actualLevel` | 供报告层列举缺口，不猜测状态 |
| `CurrentnessIndex` | `evaluatorId → (sliceId, ResultRef)` | 内存索引，打开时由 `results/` 重建；不作为持久化权威 |

## 4. 调用与状态

时序：`IProjectQuery` 读修订＋已解析策略内容 ID → `DependencyResolver` 按 manifest 选字段/资源/上游 → 规范化排序计算 `sliceHash` → 冻结 `AnalysisSnapshot`（资源经 WP-04 对象库落位）→ 评估器返回 `ResultEnvelope`（构造边界先过 §2 组合校验）→ `ResultAdmission` 校验身份/切片/分支/attempt/幂等 → 经追加协议落盘（不改历史）→ `currentness` 由切片内容比较计算 → 报告消费 `isFormallyFeasible` 与 `gaps`。迟到结果只追加到其携带的原分支历史。错误矩阵：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-RESULT-SLICE-MISMATCH` | append 时快照/切片身份与请求不符 | Input | Error | 拒绝写入；以当前切片重算 |
| `IRD-RESULT-BRANCH-MISMATCH` | 结果归属分支与仓库当前分支不符 | Input | Error | 追加为原分支历史，不提升为当前 |
| `IRD-RESULT-DUPLICATE-ATTEMPT` | 同 `runId` 下 attempt 身份冲突 | Input | Error | 幂等规则先行：同内容 no-op 返回既有 `ResultRef` |
| `IRD-RESULT-CONFLICT` | 同 `runId+attemptId` 追加异内容 | Input | Error | 保留原记录；新内容需新 attempt |
| `IRD-EVIDENCE-SNAPSHOT-INCOMPLETE` | 快照缺软件 baseline、seed 等必填 | Input | Error | 补全输入后重建快照 |
| `IRD-EVIDENCE-NAME-MISMATCH` | `nameMapId` 与快照/修订不一致 | Input | Error | 重编译名称映射后重算 |
| `IRD-RESULT-CORRUPT`（建议码，待 WP-09 登记） | 读回哈希/Schema 校验失败 | System | Error | 赋 `ArtifactIntegrity=Corrupt`，拒绝正式用途，保留诊断待重算 |

## 5. 关键实现约定

- 构造边界：`ResultEnvelope` 工厂调用 WP-03 组合谓词，非法组合拒绝并透传 `IRD-CORE-COMBINATION-ILLEGAL`，不产生半包络；合法两类的落位以 evaluation-semantics §2 为准。
- 完整性：`ArtifactIntegrity=Corrupt` 只在仓库装载/读回时按内容哈希或 Schema 失败赋予，构造边界不得产生；`Corrupt` 结果不入任何正式用途，payload 视同不可解释。
- 当前性：`currentness()` 以 `ResultRef` 定位 envelope 的 `sliceId`，与当前修订重算切片**按内容比较**（不比修订号）：一致＝`Current`，切片已变＝`Superseded`，旧快照/旧分支归属＝`Historical`；只改索引，不改历史 payload/JSON。
- `sliceHash` 规范化：规范 JSON——对象键按 UTF-8 字节序排序、无多余空白、LF、UTF-8 无 BOM——取 SHA-256 小写 hex；依赖字段先按 `fieldPath` 排序；同构内容（如四元数 `q` 与 `−q`）先经 WP-03 符号规范化再入切片，保证相同输入必得相同 `sliceHash`。
- 失效矩阵（WP-05 §4.1 冻结）：TCP/工具物理内容→运动学/轨迹/动力学/优化失效；负载→动力学/传动/选型；电机成本→仅选型/优化；显示开关、当前选择、名称拼写→`NonPhysical` 不失效物理结果，但 `nameMapId` 仍进快照并在接纳时比较。
- 缓存（execution-model §3）：`findLatest` 仅完整 `sliceHash` 命中；`Partial/Failed/Canceled/Interrupted` 与不兼容版本不得命中正式缓存。
- 正式可行：调用 WP-03 `isFormallyFeasible()`（evaluation-semantics §4），本模块只产 `EvidenceGap`；`Quick`、`Partial`、`DataInsufficient`、`NotEvaluated` 与非 `Current` 结果一律不得进入正式报告。
- 临时/Quick 快照允许只保存外部路径＋哈希；进入 Verified 或正式报告的资源必须已复制为 WP-04 不可变对象。

## 6. 测试与证据

| 测试文件 | 覆盖 |
| --- | --- |
| InputSliceTest | 哈希确定性、键序无关性、无关字段不失效、四规则失效矩阵、`NonPhysical` 隔离 |
| SnapshotTest | 不可变（getter 无可变视图）、缺 baseline/seed→`IRD-EVIDENCE-SNAPSHOT-INCOMPLETE`、资源冻结引用 |
| ResultStatusTest | 60 组合构造（非法拒绝并透传 core 码）、未知枚举、缺 payload、读回哈希失败→`Corrupt`、payload 哈希前后不变 |
| ResultAdmissionTest | 幂等/冲突/重复 attempt、迟到结果、跨分支、`nameMapId` 不一致、查询过滤与顺序稳定 |
| ReportReadinessTest | `isFormallyFeasible` 正反例复用、`EvidenceGap` 列举、证据等级边界、profile 唯一 |

往返夹具先过 `schemas/validate-schemas.ps1`（`testdata/evidence/` 样本与 `schemas/examples/analysis-snapshot.example.json`、`result-envelope.example.json`、`evidence-bundle.example.json` 同构；`invalid/` 含 §2 非法组合样本，对照 `result-envelope.illegal-combination.example.json`）。证据写入 `evidence/WP-05/`：输入/快照/资源哈希、sliceHash、诊断 JSON、append 序号、评审签名。验证命令（双形式）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_evidence_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_evidence_contract_test$'
```

原生回退：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_evidence_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_evidence_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_evidence_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_evidence_contract_test$"
```

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| 现有固定种子测试、候选编译缓存与检查点资产 | 迁移（§13.2，黄金数据固定行为） | 字段/状态/哈希契约全过才标 Migratable |
| 旧结果文件与无法验证来源的历史结果 | 只读适配；无法证明来源→EvidenceOnly | 不回写、不改判状态 |
| `EvaluationEnvelope` 等禁止名称与插件私有 Result DTO | 删除（symbol-registry §4.1） | 静态扫描零命中 |
| 手工修改追踪 CSV、结果覆盖式保存路径 | 删除 | 行为映射闭合后退出构建 |
