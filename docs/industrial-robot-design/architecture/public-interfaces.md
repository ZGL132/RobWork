# 公共接口契约

> 契约 ID：`CTR-API-001`（§1）、`CTR-API-002`（§2）、`CTR-API-003`（§3、§5）、`CTR-API-004`（§5）、`CTR-NAM-001`（§2）、`CTR-DIA-001`（§7）  
> 检查点：`IRD-D2-20260829`  
> 文档状态：`Proposed`（等待各接口所有者与消费者评审签署）  
> 权威边界：本文件是跨模块端口签名、公共值对象字段和错误面的唯一权威。需求 §6.3～6.4 是端口集合决策来源。

## 0. 通用规则

- 命名空间 `sdurws::ird`；公共头位于 `industrialrobot/<module>/include/sdurws/ird/<module>/`；实现包不得暴露 Widget、RobWork 私有指针或当前界面状态。
- **错误与异常策略**：公共接口不抛异常表达可预期失败；一律返回 `expected<T, Error>`，错误携带结构化 `Diagnostic`（CTR-DIA-001）。内部不变量违例可断言。异常不得替代可观察工程失败（需求 §12）。
- 返回值全值语义（拷贝或 `shared_ptr` 到不可变对象）；接口无所有权转移的裸出参。
- 标注 `const` 的方法并发读安全；未标注的方法需要外部串行化或由所有者保证单线程。
- `expected<T, E>` 为 `std::expected`（C++23）或项目统一等价物，由 WP-01 冻结实现选型；语义以本契约为准。

## 1. 项目查询与命令（所有者 WP-04）

```cpp
struct ProjectError { DiagnosticCode code; std::vector<Diagnostic> diagnostics; };

class DomainCommand {
public:
    virtual ~DomainCommand() = default;
    virtual CommandId commandId() const = 0;                  // 稳定唯一，幂等去重键
    virtual std::string commandKind() const = 0;              // 稳定命令类型名
    virtual std::vector<ObjectId> targetObjects() const = 0;
    virtual expected<std::monostate, ProjectError>
        validate(const ProjectRevision& target) const = 0;
    virtual expected<MutationSet, ProjectError>
        buildMutations(const ProjectRevision& target) const = 0;   // 纯函数，不落盘
};

class IProjectQuery {           // 头文件 project/IProjectQuery.hpp；只读、并发安全
public:
    virtual expected<ProjectRevision, ProjectError>
        load(const ProjectRevisionRef&) const = 0;
    virtual expected<const DomainObject*, ProjectError>
        object(const ProjectRevisionRef&, const ObjectId&) const = 0;
};

class IProjectCommandService {  // 头文件 project/IProjectCommandService.hpp；单写者串行
public:
    virtual expected<CommandResult, ProjectError>
        apply(const ProjectRevisionRef& base, const DomainCommand& cmd) = 0;
    virtual expected<CommandResult, ProjectError> undo(const ProjectRevisionRef&) = 0;
    virtual expected<CommandResult, ProjectError> redo(const ProjectRevisionRef&) = 0;
};
```

- `apply` 成功恰好产生一个新修订；`undo/redo` 是新命令，不改写历史 payload（需求 §6.3）。
- **幂等**：同一 `commandId` 对同一 `base` 重复 apply 为 no-op，返回既有修订。
- 错误映射：`IRD-PROJ-BRANCH-MISMATCH`、`IRD-PROJ-STALE-REVISION`、`IRD-PROJ-VALIDATION-FAILED`、`IRD-PROJ-NOTHING-TO-UNDO`、`IRD-PROJ-NOTHING-TO-REDO`。
- 契约测试：`ProjectCommandContractTest`（原子性、幂等、撤销语义）、`ProjectQueryContractTest`（修订不可变）。

## 2. 编译与名称解析（所有者 WP-06）

```cpp
struct NameResolutionError { DiagnosticCode code; std::vector<Diagnostic> diagnostics; };

class IRuntimeNameResolver {    // 头文件 runtime/IRuntimeNameResolver.hpp；只读、并发安全
public:
    virtual expected<std::string, NameResolutionError>
        scopedName(const ObjectId&) const = 0;
    virtual expected<ObjectId, NameResolutionError>
        resolve(std::string_view scopedName) const = 0;
    virtual const RuntimeNameMap& nameMap() const = 0;
};
```

- `scopedName/resolve` 互为逆函数；双前缀、旧前缀、去前缀重名、未知名一律 `IRD-NAME-AMBIGUOUS` / `IRD-NAME-UNRESOLVED` / `IRD-NAME-DUPLICATE-PREFIX`，绝不取第一个匹配（需求 §6.7.1）。
- `RuntimeNameMap` 条目的 `objectKind` 值域冻结：`Device / Joint / Link / Frame / FixedFrame / CompensationFrame / Tool / EnvironmentObject`。
- 除本实现内部，禁止出现前缀拼接/剥离逻辑（静态扫描由 WP-06 执行）。
- 契约测试：`ResolverContractTest`（双向一一对应、重命名后旧绑定消失）。

## 3. 评估端口（所有者 WP-05、WP-08）

```cpp
struct EvaluationError { DiagnosticCode code; std::vector<Diagnostic> diagnostics; };
using ProgressCallback = std::function<void(const ProgressReport&)>;
using CancellationToken = std::function<bool()>;   // 协作式取消：返回 true 时评估器停止

class IEngineeringEvaluator {  // 头文件 evidence/IEngineeringEvaluator.hpp
public:                        // evaluate 在 worker 线程调用；实现不得访问 UI 与可变全局态
    virtual EvaluatorDependencyManifest dependencyManifest() const = 0;
    virtual expected<std::monostate, EvaluationError>
        validate(const EvaluationRequest&) const = 0;
    virtual expected<ResultEnvelope, EvaluationError>
        evaluate(const AnalysisSnapshot& snapshot,
                 const ProgressCallback& progress,
                 const CancellationToken& cancellation) = 0;
    virtual TaskCapabilities capabilities() const = 0;
};
```

- 返回类型是 **`ResultEnvelope`**（SYM-EVI-005）；`EvaluationEnvelope` 为 D1 起禁止名称。
- 类型化 payload 经 `payloadId` 引用不可变存储，由各评估器提供模块私有的类型化视图；公共层只交换 envelope。
- 取消经 `CancellationToken` 协作生效，安全点规则见执行模型 §1～2。
- 契约测试：`EvaluatorContractTest`（合法组合、取消、进度、能力声明）。

## 4. 调度端口（所有者 WP-08）

```cpp
class IEvaluationScheduler {   // 头文件 execution/IEvaluationScheduler.hpp；状态机唯一写者
public:
    virtual expected<TaskHandle, EvaluationError> submit(const EvaluationRequest&) = 0;
    virtual expected<TaskSnapshot, EvaluationError> snapshot(const RunIdentity&) const = 0;
};

struct TaskHandle {            // 值语义；pause/resume/cancel/checkpoint 语义见执行模型 §1
    virtual TaskCapabilities capabilities() const = 0;
    virtual expected<TaskSnapshot, EvaluationError> pause() = 0;
    virtual expected<TaskSnapshot, EvaluationError> resume() = 0;
    virtual expected<TaskSnapshot, EvaluationError> cancel() = 0;
    virtual expected<CheckpointRef, EvaluationError> checkpoint() = 0;
};
```

- 契约测试：`SchedulerContractTest`（转移表全覆盖、有界并行、迟到事件）。

## 5. 结果仓库（所有者 WP-05）

```cpp
struct ResultAdmissionError { DiagnosticCode code; std::vector<Diagnostic> diagnostics; };

class IResultRepository {      // 头文件 evidence/IResultRepository.hpp；追加式、并发安全
public:
    virtual expected<ResultRef, ResultAdmissionError>
        append(const AnalysisSnapshot&, const ResultEnvelope&, const EvidenceBundle&) = 0;
    virtual expected<std::optional<ResultEnvelope>, ResultAdmissionError>
        findLatest(const EvaluatorInputSlice&, const EvaluationRequest&) const = 0;
    virtual expected<std::vector<ResultEnvelope>, ResultAdmissionError>
        history(const SourceRevisionRef&) const = 0;
    virtual expected<ResultCurrentness, ResultAdmissionError>
        currentness(const ResultRef&) const = 0;
};
```

- 只追加；拒绝不兼容快照、错误分支、重复尝试与同键异内容（错误码见执行模型 §5）。
- 契约测试：`ResultRepositoryContractTest`（幂等、冲突、当前性判定、查询）。

## 6. 策略、场景与诊断端口（所有者 WP-07 / WP-10 / WP-09）

```cpp
class IEngineeringPolicyProvider {   // policy/；只读、并发安全
public:
    virtual expected<EngineeringPolicySet, ProjectError>
        resolvedPolicy(const ProjectRevisionRef&) const = 0;
    virtual expected<EngineeringPolicySet, ProjectError>
        resolvedPolicy(const AnalysisSnapshot&) const = 0;    // 不允许叠加私有默认值
};

class ISceneProjection {             // ui/；会话态投影，不回写设计基线
public:
    virtual expected<SceneSnapshot, ProjectError> projectCurrent() const = 0;
    virtual expected<SceneSnapshot, ProjectError> projectCandidate(const ResultRef&) const = 0;
};

class IDiagnosticCatalog {           // diagnostics/；只读、并发安全
public:
    virtual expected<DiagnosticInfo, ProjectError> lookup(DiagnosticCode) const = 0;
    virtual std::vector<DiagnosticCode> codesByCategory(DiagnosticCategory) const = 0;
};
```

**`Diagnostic` 字段冻结（CTR-DIA-001）**：`code`（形如 `IRD-<AREA>-<NAME>` 的稳定码）、`category`（`Input / Engineering / System`）、`severity`（`Info / Warning / Error`）、`subjectObjectId`、局部/运行时名称、实际值、要求值、原因和建议动作。`severity` 与 `EngineeringStatus` 正交：Warning 级诊断不等于工程判定 Warning，工程判定由评估器给出。

- 安全 CSV/JSON/资源读写端口（WP-11）与报告渲染端口（WP-12）在各自模块详设（D4）登记签名；本表登记其存在与所有权，不得由业务插件私自解析不可信文件。

## 7. 公共值对象字段表（冻结）

| 值对象 | 字段（类型；单位/可空性） |
| --- | --- |
| `ProjectRevisionRef` | `projectId, branchId, revisionId`（string；均非空） |
| `SourceRevisionRef` | `projectId, branchId, revisionId`；仅溯源，不参与缓存/当前性 |
| `RunIdentity` | `projectId, branchId, revisionId, runId, attemptId` |
| `CommandResult` | `applied(bool)`、`revision(ProjectRevisionRef)`、`diagnostics[]` |
| `EvaluatorDependencyManifest` | `requiredFields[](字段路径)`、`requiredResources[]（资源内容 ID）`、`requiredUpstreamArtifacts[]（ResultRef）`、`optionalEvidence[]` |
| `EvaluatorInputSlice` | `sliceId(sha256)`、`evaluatorId+evaluatorVersion`、`manifestDigest`、`fieldValues（规范化内容）`、`resourceIds[]`、`upstreamRefs[]`、`policyContentId`、`nameMapId`、`algorithmVersion`、`randomSeed(uint64)`、`threadCount(uint32)` |
| `AnalysisSnapshot` | `snapshotId`、`sourceRevision(SourceRevisionRef)`、`objectRevisions[]`、`config（analysisConfigurationId + contentDigest 身份引用）`、`evaluator+RobWork/RobWorkSim 版本`、`randomSeed`、`manifest`、`resolvedPolicyContentId`、`nameMapId` |
| `ResultEnvelope` | `runIdentity`、`sliceId`、`evaluatorId+version`、`mode(EvaluationMode)`、`outcome(ExecutionOutcome)`、`engineeringStatus`、`payloadCompleteness`、`evidenceLevel(EvidenceLevel)`、`payloadId(内容 ID)`、`evidenceRef`、`diagnostics[]`、`timing/resourceUsage`；读回时由仓库附加 `artifactIntegrity` 与 `currentness` |
| `EvidenceBundle` | `config 快照`、`resourceFidelity[]`、`diagnostics[]`、`statistics`、`provenance(工具/库版本、种子)`、`reproduction(命令与输入身份)` |
| `ResultRef` | `runId, attemptId, evaluatorId, payloadId`；文件路径不是结果身份 |

- 枚举值域以 [evaluation-semantics.md](evaluation-semantics.md) §1 为准；`ArtifactIntegrity` 由仓库读回时赋予。
- 持久化 JSON 形态见 [persistence-schema.md](persistence-schema.md) 与 `schemas/`。

## 8. 所有权汇总

WP-03 身份/单位/状态；WP-04 项目命令与查询；WP-05 快照/结果/证据；WP-06 名称映射与编译；WP-07 策略与碰撞；WP-08 调度；WP-09 诊断；WP-10 场景投影；WP-11 安全 IO；WP-12 报告；WP-18 传动映射。消费者只能通过本文件端口使用；`IRD-*` 稳定码总目录由 WP-09 拥有（CTR-DIA-001）。

## 9. 接口级契约测试清单

`ProjectCommandContractTest`、`ProjectQueryContractTest`、`ResolverContractTest`、`EvaluatorContractTest`、`SchedulerContractTest`、`ResultRepositoryContractTest`、`PolicyProviderContractTest`、`DiagnosticCatalogContractTest`——每个端口至少失败/正常/边界三例（testing-contract.md §1）。
