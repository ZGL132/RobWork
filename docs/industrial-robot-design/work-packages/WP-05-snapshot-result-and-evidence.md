# WP-05 快照、结果与证据实施计划

> 阶段/发布：阶段 A / R1；结果与证据公共接口所有者：WP-05。实现者、验证者和评审者必须是不同执行上下文。

**目标：** 将一次评估冻结为可复现的输入切片和不可变分析快照，追加保存结果及证据，精确标记当前性，并复用 WP-03 的唯一正式可行谓词。

## 1. 目标与非目标

交付字段级依赖清单、`EvaluatorInputSlice`、`AnalysisSnapshot`、`ResultEnvelope`、`EvidenceBundle`、追加式结果仓库和当前性服务。结果的执行完整性、工程结论、payload 完整度、证据等级和当前性必须正交保存；历史结果永不原地修改。

不实现评估算法、任务调度、项目文件事务、报告渲染、GUI 或重新定义 WP-03 的正式可行语义。

## 2. 需求与架构契约

- 需求：CON-01～CON-06、EVI-01、NFR-COR-04、NFR-COR-02 结果集合部分、AT-04、AT-05、AT-10、AT-12。
- 阶段/发布：阶段 A / R1；阶段 B 仅使用已冻结静态优化输入切片。
- 架构契约：`architecture/execution-model.md`、`architecture/persistence-schema.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`。
- 模块方案：`module-design/snapshot-result.md`。

## 3. 文件所有权与依赖

拥有目录：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/evidence/`，含 `include/sdurws/ird/evidence/`、`src/`、`test/`、`testdata/` 和 `evidence/`。允许依赖 WP-03 core（含组合谓词与 `isFormallyFeasible`）、WP-04 查询端口＋追加协议原语和 Qt Core JSON；WP-05 不依赖 WP-06 代码，快照只保存不透明 `nameMapId`（内容 ID 相等比较，不调用 `IRuntimeNameResolver`，module-design/snapshot-result.md 依赖裁决）；禁止写项目 revision、直接依赖 Widget、调度器私有实现或手工修改追踪 CSV。

目标：`sdurws_ird_evidence`、`sdurws_ird_evidence_test`、`sdurws_ird_evidence_contract_test`。

## 4. 冻结数据模型

### 4.1 EvaluatorInputSlice

必填：`projectId`、`branchId`、`revisionId`、`robotDesignContentId`、`requirementsContentId`、`policyContentId`、`evaluatorId`、`evaluatorVersion`、`algorithmVersion`、`catalogVersion`、`randomSeed`、`threadCount`、`configHash`、`dependencyFields[]`、`sliceHash`。所有 ID 为规范小写字符串；线程数为正整数；种子显式保存；列表按字段路径排序后计算 SHA-256。

依赖规则固定为：TCP/工具物理内容使运动学、轨迹、动力学和优化失效；负载使动力学、传动和选型失效；电机成本只使选型/优化失效，不使 FK/IK/轨迹失效；显示开关、当前选择和运行时名称拼写不使物理结果失效。名称映射仍进入快照并在接纳时校验。

### 4.2 AnalysisSnapshot 与 ResultEnvelope

`AnalysisSnapshot` 字段以 `architecture/public-interfaces.md` §7 为准（`snapshotId`、`sourceRevision`、`objectRevisions[]`、`config`、软件基线、`randomSeed`、`manifest`、`resolvedPolicyContentId`、`nameMapId`）；`resolvedPolicyContentId` 与 `nameMapId` 是不透明内容 ID（64 位小写 hex），WP-05 对 WP-06/07 无代码依赖。创建后只读，序列化不得依赖 UI 状态。

`ResultEnvelope` 包含 `resultId`、`project/branch/revision`、`snapshotId`、`runId`、`attemptId`、`evaluatorId/version`、`executionOutcome`、`engineeringStatus`、`payloadCompleteness`、`currentness`、`evidenceLevel`、`payloadRef`、`diagnostics[]`、`createdAt`。`currentness` 取 `Current`、`Superseded`、`Historical`；变更只更新索引，不改 payload。

## 5. 端到端数据流

```text
ProjectRevision + policy + evaluator declaration
  -> dependency resolver selects fields
  -> normalize/sort/hash EvaluatorInputSlice
  -> freeze AnalysisSnapshot (resources/name map/version/seed)
  -> evaluator emits ResultEnvelope + EvidenceBundle
  -> ResultAdmission validates identity, slice, status and evidence
  -> append repository (never overwrite)
  -> currentness service compares latest compatible slice
  -> report consumer queries accepted history and explicit gaps
```

Quick 模式允许临时外部引用，但结果只能是 Screening/Partial 或 Historical，不能进入正式报告。缓存命中必须使用完整 `sliceHash`；Partial、Failed、Canceled、Interrupted 和不兼容版本不得命中正式缓存。

## 6. 状态与失败分类

执行状态沿用 WP-03：`Completed`、`Canceled`、`Failed`、`Interrupted`；工程状态：`Pass`、`Warning`、`Infeasible`、`DataInsufficient`、`NotEvaluated`；payload：`Complete`、`Partial`、`None`。非法组合在构造边界拒绝，例如 Canceled/Failed/Interrupted 不得带 Pass 或 Complete payload。输入缺失为 Input，数据不足/工程不可行属于 Engineering，仓库/进程/版本故障属于 System。

## 任务

| 任务 | 独立产出 | 任务卡 |
| --- | --- | --- |
| WP-05-T01 | 字段依赖、规范化、sliceHash 与失效矩阵 | [T01](../agent-tasks/WP-05-T01-input-slice.md) |
| WP-05-T02 | 不可变快照及资源/版本冻结 | [T02](../agent-tasks/WP-05-T02-immutable-snapshot.md) |
| WP-05-T03 | 结果正交状态与合法组合校验 | [T03](../agent-tasks/WP-05-T03-orthogonal-status.md) |
| WP-05-T04 | 接纳、迟到结果和历史查询 | [T04](../agent-tasks/WP-05-T04-result-acceptance.md) |
| WP-05-T05 | 正式可行复用与报告就绪缺口 | [T05](../agent-tasks/WP-05-T05-report-readiness.md) |

依赖：T01 → T02 → T03 → T04 → T05；T03 可并行编写状态测试但必须消费 T02 的字段。公共接口只由 WP-05 修改。

## 8. 测试、性能与证据

模块测试覆盖依赖失效矩阵、哈希确定性、快照不可变性、状态组合、接纳拒绝、迟到结果和查询过滤。契约测试验证所有身份字段、版本、`nameMapId` 内容一致性（module-design/snapshot-result.md 裁决：名称校验收窄为内容 ID 比较，不做运行时反解）、证据缺口和缓存排除规则。性能基准使用 `benchmark-manifest.json`，记录固定数据集、线程、种子、预热、P50/P95；至少测 10k 结果追加和 100k 历史查询。

每项证据含 Task ID、需求 ID、提交 SHA、环境、命令、输入/快照/资源哈希、期望与实际状态、诊断 JSON、仓库目录清单和独立评审者。

## 验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_evidence_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_evidence(_contract)?_test$'
```

脚本由 WP-01 提供；不存在时停止，不复制临时脚本。

## 9. 迁移、兼容与退出条件

旧结果先以只读适配器读取，满足字段/状态/哈希契约才标记 Migratable；无法证明来源的结果标记 EvidenceOnly。新字段必须先更新架构契约并提交 ADR，不使用隐式默认值。

## 退出条件

A-GATE-01～03 与 AT-04、AT-05、AT-10、AT-12 快照/当前性断言通过；所有结果可追到不可变快照；失败、取消、部分和 Quick 结果不能成为正式证据；相同切片产生相同 `sliceHash`，无关显示字段变化不使物理结果失效；5 张任务卡证据和独立评审齐全。
