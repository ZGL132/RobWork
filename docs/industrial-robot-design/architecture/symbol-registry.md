# 公共符号注册表

> D1 检查点：`IRD-D1-20260829`  
> 文档状态：`Proposed`  
> 作用：冻结跨模块类型、接口和核心术语的规范名称，阻止同名异义、异名同义和重复 DTO。

本表只登记跨模块公共符号。模块私有实现类型不进入本表；一旦某类型被两个及以上模块交换、持久化或写入证据，就必须先登记。符号名称区分大小写，英文名称用于代码和持久化；中文显示名称由 WP-09 术语表统一管理。

## 1. 身份、领域与运动学

| 符号 ID | 规范符号 | 种类 | 唯一语义/边界 | 所有者 | 契约 | 禁止的替代定义 | D1 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `SYM-ID-001` | `ObjectId` | 值类型 | 项目内稳定对象身份；重命名、分支和修订不改变 | WP-03 | CTR-DOM-001 | 名称、数组索引、运行时全名作主键 | `Proposed` |
| `SYM-ID-002` | `RobotId` | 值类型 | 机械臂所有者作用域 ID；首版项目内唯一 | WP-03 | CTR-DOM-001 | `RobotName` 作持久身份 | `Proposed` |
| `SYM-ID-003` | `ObjectIdentity` | 值对象 | `objectId + ownerScopeId + localName` | WP-03 | CTR-DOM-001 | 各插件私有身份 DTO | `Proposed` |
| `SYM-ID-004` | `ProjectRevisionRef` | 值对象 | 项目命令目标：`projectId + branchId + revisionId` | WP-04 | CTR-API-001 | 裸 revision 字符串 | `Proposed` |
| `SYM-ID-005` | `SourceRevisionRef` | 值对象 | 结果溯源引用，不参与当前性或缓存判定 | WP-05 | CTR-EXE-002 | 与 `ProjectRevisionRef` 混为同一语义 | `Proposed` |
| `SYM-ID-006` | `RunIdentity` | 值对象 | `projectId + branchId + revisionId + runId + attemptId` 的执行身份 | WP-08 | CTR-EXE-001 | 仅凭 runId 接纳结果 | `Proposed` |
| `SYM-DOM-001` | `ProjectRevision` | 聚合根 | 当前已应用项目状态；领域命令原子产生新实例 | WP-04 | CTR-DOM-002 | 插件内共享对象副本 | `Proposed` |
| `SYM-DOM-002` | `RobotDesign` | 聚合 | 单机械臂权威设计 | WP-03、WP-13 | CTR-DOM-002 | DH/URDF/Widget 模型作第二真值 | `Proposed` |
| `SYM-DOM-003` | `JointDefinition` | 值对象 | 关节类型、Origin Pose、Axis、限制和来源 | WP-03、WP-13 | CTR-KIN-001 | RPY+位置+隐式 Z 轴作正式定义 | `Proposed` |
| `SYM-DOM-004` | `ToolDefinition` | 聚合 | 工具安装、TCP、几何和物性定义 | WP-03、WP-13 | CTR-DOM-002 | 在任务中复制工具几何 | `Proposed` |
| `SYM-DOM-005` | `EnvironmentModel` | 聚合 | 基座、障碍、夹具和地面几何 | WP-03、WP-13 | CTR-DOM-002 | 插件私有障碍集合 | `Proposed` |
| `SYM-DOM-006` | `EngineeringRequirements` | 聚合 | 任务、区域、姿态、节拍、Must/Should 和负载引用 | WP-14 | CTR-DOM-002 | UI 表格作权威数据 | `Proposed` |
| `SYM-DOM-007` | `LoadCase` | 值对象 | 负载、外力和工艺事件工况 | WP-14 | CTR-DOM-002 | 动力学插件私有负载真值 | `Proposed` |
| `SYM-DOM-008` | `DriveTrainDesign` | 聚合 | 已应用的每轴传动设计 | WP-18 | CTR-DOM-002 | 选型结果直接充当设计 | `Proposed` |
| `SYM-KIN-001` | `KinematicAuthorityKind` | 枚举 | `StandardDH` 或 `ExplicitJoint` 的互斥权威类型 | WP-06、WP-13 | CTR-KIN-002 | 布尔 `useDh` | `Proposed` |
| `SYM-KIN-002` | `StandardDH` | 权威表示 | 可编辑标准 DH 参数化；不是运行时真值 | WP-13 | CTR-KIN-002 | 派生 DH 被当作权威 | `Proposed` |
| `SYM-KIN-003` | `ExplicitJoint` | 权威表示 | 完整 Origin Pose 与任意 Axis 关节参数化 | WP-13 | CTR-KIN-002 | 有损反算后覆盖显式关节 | `Proposed` |
| `SYM-KIN-004` | `CanonicalKinematicModel` | 编译值对象 | 从权威参数化确定性生成的唯一 SE(3) 运行时模型 | WP-06 | CTR-KIN-001 | `KinematicChainModel` 或 DH 作为并行真值 | `Proposed` |
| `SYM-KIN-005` | `CompiledRobotArtifacts` | 编译结果 | canonical、names、WorkCell、DWC 和诊断的全成全败工件 | WP-06 | CTR-API-002 | 部分成功指针集合 | `Proposed` |

## 2. 名称、策略、执行与证据

| 符号 ID | 规范符号 | 种类 | 唯一语义/边界 | 所有者 | 契约 | 禁止的替代定义 | D1 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `SYM-NAM-001` | `RuntimeNameMap` | 编译值对象 | 稳定对象 ID 与 RobWork 作用域全名的不可变双向映射 | WP-06 | CTR-NAM-001 | 插件内前缀表 | `Proposed` |
| `SYM-NAM-002` | `IRuntimeNameResolver` | 接口 | 运行时名称唯一生成和反解端口 | WP-06 | CTR-NAM-001 | 字符串拼接/剥离工具 | `Proposed` |
| `SYM-POL-001` | `EngineeringPolicySet` | 聚合 | 影响工程判定的版本化策略唯一集合 | WP-07 | CTR-POL-001 | 插件私有计算开关和默认值 | `Proposed` |
| `SYM-POL-002` | `CollisionPolicy` | 值对象 | 碰撞参与、配对、后端、安全距离和路径验证规则 | WP-07 | CTR-POL-001 | `CollisionSetup`/`ProximitySetup` 作权威 | `Proposed` |
| `SYM-POL-003` | `CollisionEvaluator` | 服务 | 跨运动学、轨迹和优化共享的唯一碰撞判定实现 | WP-07 | CTR-POL-001 | 各插件重复碰撞算法 | `Proposed` |
| `SYM-EVI-001` | `EvaluatorDependencyManifest` | 值对象 | 评估器声明字段、资源、上游工件和可选证据依赖 | WP-05 | CTR-EXE-002 | 隐式依赖列表 | `Proposed` |
| `SYM-EVI-002` | `EvaluatorInputSlice` | 不可变值对象 | 缓存、当前性和失效的规范化输入内容身份 | WP-05 | CTR-EXE-002 | 整个项目修订作缓存键 | `Proposed` |
| `SYM-EVI-003` | `AnalysisSnapshot` | 不可变值对象 | 一次运行的完整输入、版本、策略和名称绑定 | WP-05 | CTR-EXE-002 | 运行时补默认值 | `Proposed` |
| `SYM-EVI-004` | `EvidenceBundle` | 不可变工件 | 配置、资源保真度、诊断、统计、来源和签署证据 | WP-05 | CTR-TST-002 | 日志文件替代结构化证据 | `Proposed` |
| `SYM-EVI-005` | `ResultEnvelope` | 不可变值对象 | 执行、工程、完整度、当前性、证据等级及 payload 身份的正交包络 | WP-05 | CTR-API-003、CTR-EXE-004 | `EvaluationEnvelope`、插件私有 Result DTO | `Proposed` |
| `SYM-EVI-006` | `ResultCurrentness` | 枚举 | `Current / Superseded / Historical` 的索引关联状态，不修改历史 payload | WP-05 | CTR-EXE-002 | 布尔 `isCurrent` | `Proposed` |
| `SYM-EVI-007` | `RequiredEvidenceProfile` | 值对象 | 正式用途所需评估器、证据等级、资源保真度和允许警告 | WP-03 | CTR-DOM-004 | 报告层自行猜测证据要求 | `Proposed` |
| `SYM-STA-001` | `EvaluationMode` | 枚举 | `Quick / Verified` | WP-03 | CTR-DOM-004 | 与碰撞开关混用 | `Proposed` |
| `SYM-STA-002` | `EvidenceLevel` | 枚举 | `Screening / PreliminaryDesign / ExternallyValidated` | WP-03 | CTR-DOM-004 | 模块自定义证据等级 | `Proposed` |
| `SYM-STA-003` | `ExecutionOutcome` | 枚举 | `Completed / Canceled / Failed / Interrupted` | WP-03 | CTR-DOM-004 | 与任务生命周期状态混用 | `Proposed` |
| `SYM-STA-004` | `EngineeringStatus` | 枚举 | `Pass / Warning / Infeasible / DataInsufficient / NotEvaluated` | WP-03 | CTR-DOM-004 | 用异常或空结果表达工程不可行 | `Proposed` |
| `SYM-STA-005` | `PayloadCompleteness` | 枚举 | `Complete / Partial / None` | WP-03 | CTR-DOM-004 | 由 payload 是否为空隐式推断 | `Proposed` |
| `SYM-STA-006` | `TaskState` | 枚举 | `Queued / Running / Pausing / Paused / Canceling / Completed / Canceled / Failed / Interrupted`（9 态，与需求 §6.4 状态机一致） | WP-08 | CTR-EXE-001 | 与 `ExecutionOutcome` 合并；缺少 `Pausing/Paused` 的 7 态版本 | `Proposed` |
| `SYM-STA-007` | `ArtifactIntegrity` | 枚举 | `Valid / Corrupt`；工件可解释性，仅由结果仓库读回时赋予 | WP-05 | CTR-DOM-004、CTR-EXE-004 | `PayloadCompleteness` 使用 `Corrupt` 值 | `Proposed` |
| `SYM-EXE-001` | `EvaluationRequest` | 不可变值对象 | 快照、评估器、run/attempt、模式、资源和恢复策略请求 | WP-08 | CTR-EXE-001 | 可变任务参数包 | `Proposed` |
| `SYM-EXE-002` | `ResultRef` | 值对象 | 结果仓库中不可变记录引用 | WP-05 | CTR-API-004 | 文件路径作结果身份 | `Proposed` |

## 3. 公共端口、诊断与优化

| 符号 ID | 规范符号 | 种类 | 唯一语义/边界 | 所有者 | 契约 | 禁止的替代定义 | D1 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `SYM-API-001` | `DomainCommand` | 命令基类 | 对已知修订执行的一次领域修改意图 | WP-04 | CTR-API-001 | 插件直接写共享文件 | `Proposed` |
| `SYM-API-002` | `CommandResult` | 值对象 | applied、新修订引用和结构化诊断 | WP-04 | CTR-API-001 | bool+字符串错误 | `Proposed` |
| `SYM-API-003` | `IProjectQuery` | 接口 | 按修订引用读取不可变项目状态 | WP-04 | CTR-API-001 | 读取其他插件内存 | `Proposed` |
| `SYM-API-004` | `IProjectCommandService` | 接口 | apply/undo/redo 的唯一共享写入口 | WP-04 | CTR-API-001 | 插件私有保存服务 | `Proposed` |
| `SYM-API-005` | `IEngineeringEvaluator` | 接口 | 消费 `EvaluatorInputSlice` 并返回 `ResultEnvelope` | WP-05、WP-08 | CTR-API-003 | 模块专用不兼容评估入口 | `Proposed` |
| `SYM-API-006` | `IResultRepository` | 接口 | 接纳和查询不可变结果的唯一端口 | WP-05 | CTR-API-004 | 结果文件被调用方覆盖 | `Proposed` |
| `SYM-DIA-001` | `Diagnostic` | 值对象 | code、category、对象、实际/要求、原因和动作 | WP-09 | CTR-DIA-001 | 裸字符串或异常作工程结果 | `Proposed` |
| `SYM-DIA-002` | `DiagnosticCategory` | 枚举 | `Input / Engineering / System` | WP-09 | CTR-DIA-001 | 各插件自定义错误分类 | `Proposed` |
| `SYM-OPT-001` | `OptimizationStudyDefinition` | 聚合 | 基线、变量、约束、目标、预算和算法策略 | WP-20 | CTR-OPT-001 | 优化器私有第二份模型 | `Proposed` |
| `SYM-OPT-002` | `OptimizationRunResult` | 只追加工件 | 运行身份、检查点、候选集合、统计和完成状态 | WP-21 | CTR-OPT-001 | 每候选一个项目修订 | `Proposed` |
| `SYM-OPT-003` | `CandidateInputSnapshot` | 不可变值对象 | 基线修订、研究版本和设计向量构成的候选输入 | WP-20 | CTR-OPT-001 | 可变候选字段副本 | `Proposed` |
| `SYM-OPT-004` | `CompiledCandidateArtifact` | 编译工件 | 候选规范模型、物性、目录引用和 `RuntimeNameMap` | WP-20 | CTR-OPT-001 | 候选直接修改 `RobotDesign` | `Proposed` |
| `SYM-OPT-005` | `DesignCandidate` | 结果值对象 | 参数、器件、各域结果、违反项和证据 | WP-21 | CTR-OPT-001 | 候选等同正式设计 | `Proposed` |
| `SYM-OPT-006` | `ParetoSet` | 结果值对象 | 非支配候选、目标值和支配关系 | WP-21 | CTR-OPT-001 | 单一加权总分替代 | `Proposed` |
| `SYM-OPT-007` | `Metric` | 定义类型 | 始终计算和展示的比较指标 | WP-20 | CTR-OPT-001 | 与 Objective 同义 | `Proposed` |
| `SYM-OPT-008` | `HardConstraint` | 定义类型 | 决定工程可行性的强制约束 | WP-20 | CTR-OPT-001 | 被加权分数抵消 | `Proposed` |
| `SYM-OPT-009` | `SoftConstraint` | 定义类型 | 只产生警告/次级排序，默认不参与 Pareto | WP-20 | CTR-OPT-001 | 隐式改变可行性 | `Proposed` |
| `SYM-OPT-010` | `Objective` | 定义类型 | 用户显式选择并参与 Pareto 支配的指标 | WP-20 | CTR-OPT-001 | 所有 Metric 自动成为目标 | `Proposed` |
| `SYM-OPT-011` | `DesignVariableDefinition` | 值对象 | 变量 ID、绑定、目标对象、参数路径、值类型、域、量化和依赖 DAG | WP-20 | CTR-OPT-002 | 优化器私有变量副本 | `Proposed` |
| `SYM-OPT-012` | `DesignVector` | 不可变值对象 | `variableId → 类型化值`；零值是合法设置值，缺省表示未设置 | WP-20 | CTR-OPT-002 | 可变候选字段包 | `Proposed` |
| `SYM-OPT-013` | `CandidatePatch` | 值对象 | 有序原子修改、派生重算、诊断和 writeSet 指纹；全成全败 | WP-20 | CTR-OPT-002 | 逐字段部分应用 | `Proposed` |
| `SYM-EVL-001` | `DriveTrainMappingEvaluator` | 服务 | 关节侧与电机侧传动映射、效率和反射惯量唯一实现 | WP-18 | CTR-DOM-003；ADR-004 | 动力学、选型和优化各自映射 | `Proposed` |
| `SYM-RPT-001` | `ReviewReport` | 不可变报告对象 | 修订、结果、候选取舍、限制和签署信息 | WP-12 | CTR-TST-002 | 从 UI 状态临时拼报告 | `Proposed` |

## 4. 命名裁决与使用规则

1. 规范结果包络统一使用 `ResultEnvelope`；`EvaluationEnvelope` 从 D1 起为禁止名称（`public-interfaces.md` 已于 D2 修正，复审时如再出现即为缺陷）。
2. `ProjectRevisionRef` 是命令目标，`SourceRevisionRef` 是结果溯源，两者不得 typedef 为同一语义类型。
3. `TaskState` 描述调度生命周期（9 态，含 `Pausing/Paused`），`ExecutionOutcome` 描述结果终态，两者不得合并。
4. `CanonicalKinematicModel` 是唯一规范模型名称；文档中的“规范模型”“SE(3) 关节链”均指该符号，但代码不得另建同义公共类型。
5. `CompiledRobotArtifacts` 是基线模型的运行时编译工件（WP-06）；`CompiledCandidateArtifact` 是优化候选工件（WP-20）。两者是**不同符号**，后者可组合前者的编译管线但不能混用身份；任何文档把二者当同义替换使用即为缺陷。
6. 动力学结果规范名为 `DynamicResult`（需求 §7.2）；`DynamicsResult` 为禁止名称。
7. 候选结果值对象规范名为 `DesignCandidate`；`CandidateResult` 为禁止名称。
8. 分析配置规范名为 `AnalysisConfiguration`；`AnalysisConfig` 为禁止名称。
9. 优化研究定义与运行结果的规范名为 `OptimizationStudyDefinition` 与 `OptimizationRunResult`；单独的 `OptimizationStudy` 为禁止名称（无法区分定义与运行）。
10. `DomainCommand` 的公共接口定义唯一位于 [public-interfaces.md](public-interfaces.md) §1；模块不得定义平行的命令基类。
11. D2/D3 新增公共符号前必须先更新本表、契约注册表、所有者和消费者；未登记符号不得进入公共头或持久化 Schema。
