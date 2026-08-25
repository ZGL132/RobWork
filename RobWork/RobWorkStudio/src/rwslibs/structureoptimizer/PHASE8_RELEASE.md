# StructureOptimizer Phase 8 发布说明与验收证据

本文档是 Phase 8 发布包的操作清单。它描述可复现输入、验收命令、性能预算、历史
报告读取规则和已知限制；不把本机绝对路径或构建产物写入仓库。

## 固定输入与复现

1. 固定 WorkCell、机器人模型快照、冻结需求工件和外部环境指纹。
2. 固定 `StructureOptimizationRunConfig::randomSeed`；验收样例使用 `20260727`，
   工程项目必须在报告中记录实际种子。
3. 固定评价器 ID/版本、Envelope schema 版本、设计变量顺序和运行策略。
4. 使用同一输入重复运行两次，`phase8_acceptance` 必须确认候选顺序、稳定索引、值、
   得分、状态及最佳候选索引完全一致。

## Windows 验收命令

在 Visual Studio x64 开发环境中执行，并保持 Qt 平台变量为 `windows`：

```powershell
$env:QT_QPA_PLATFORM='windows'
$env:PATH='C:\path\to\RobWork\build\phase8\RobWorkStudio\bin\Debug;C:\path\to\RobWork\build\phase8\RobWork\bin\Debug;D:\software\QT\6.11.1\msvc2022_64\bin;'+$env:PATH
$test = 'C:\path\to\RobWork\build\phase8\RobWorkStudio\bin\Debug\sdurws_structureoptimizer_test.exe'
& $test phase8_acceptance
& $test phase8_performance
& $test phase8_resource
& $test phase8_manifest
```

每条命令都必须单独启动一个绝对路径 executable。GUI/Widget 测试不得设置
`QT_QPA_PLATFORM=offscreen`；模型-only 的 QCoreApplication 测试可以不依赖 GUI 插件。

## 性能预算解释

`Phase8PerformanceBudget` 的默认值是生成候选不超过 100000 个、总耗时不超过 3600 秒、
模型构建不超过 1800 秒、评估不超过 3600 秒。缓存命中率和灵敏度评估次数按运行诊断
记录审计。预算超限不会伪装成失败，而是写入结构化 Warning；数据损坏、负数计数、
计数矛盾或非有限数值是 Error，发布门必须阻断。

## 历史报告读取规则

- 读取报告前先检查 Envelope/schema、评价器版本和输入指纹。
- 版本兼容且指纹一致时可展示为当前项目的历史证据；版本不兼容时只允许只读展示。
- 任一模型、需求、环境或评价器指纹失效，都不得复用旧缓存、恢复旧运行或标记为当前
  `Verified`；必须重新冻结并重新运行。

## 发布前清理与已知限制

- 清理所有 `QTemporaryDir`、`structure-optimizer-preview-*` 和未登记 staging 目录，
  再运行 `Phase8ResourceAudit`。
- 发布清单只能引用项目相对 ProjectResource ID；绝对路径、`..` 路径、临时目录和
  NaN/Inf 数值会被 `Phase8ReleaseManifestAudit` 拒绝。
- 当前阶段仍只覆盖运动学结构优化；轨迹、动力学、电机/减速器选型不是启用的评价器。
- 候选评估保持单 worker；大规模候选或高密度工作空间采样可能触发性能 Warning。

## 证据归档

归档发布清单 JSON、项目 JSON、结果 JSON、候选/任务/审计 CSV、Markdown 报告、候选模型
包及四个 Phase 8 套件的完整 stdout。不要归档构建目录、Qt 部署 DLL 或临时 WorkCell。

## 唯一构建树规则

StructureOptimizer 的修复、验收和发布证据必须来自同一构建树：

`D:/10_Source_Repos/21_robot/RobWork/RobWork/build/codex-structure-remediation-debug`

执行前必须核对该目录 `CMakeCache.txt` 中的 `CMAKE_HOME_DIRECTORY` 与当前源码
目录一致，并记录源码 revision、可执行文件绝对路径、文件时间和 SHA256。QtCreator
链接目录以及其他 `codex-vs-debug*` 目录中的同名可执行文件属于历史输出，不能作为
修复完成的证据。

## Code reduction release record (2026-08-25)

Implemented per docs/superpowers/plans/2026-08-24-structure-optimizer-code-reduction.md
(Tasks 1-7, 9-12; Task 8 variable-suggestion rename deferred as optional).

- Directory after reduction: 228 files (101 cpp / 118 hpp / docs+resources);
  core library source list reduced from 101 to 65 entries.
- Moved from the core library into the test target only (not installed):
  HybridOptimizer, LocalSearch, EliteSelector, QuickScreeningPolicy,
  InitialSampler, DeterministicSeed, EvaluationCache, CacheKey,
  KinematicMetricAggregator, EstimatedWorkspaceStage,
  OrientationCoverageStage, DesignTemplateApplication, DhProjection,
  CanonicalForwardKinematics, FinalValidationPlan, IndependentFinalVerifier,
  CandidateEvaluationScheduler, OptimizationCheckpoint, Phase8 audits x4,
  OptimizationRunSnapshot/RunJson/RunStore,
  StructureOptimizationWorkflowResolver.
- Removed: StructureOptimizationDocument.cpp (stateless unit) and the
  duplicate setDirty call in StructureOptimizerPlugin.
- Unified: KinematicEngineeringEvaluator::evaluateCandidate is the single
  candidate evaluation implementation (evaluateLegacy remains only as a
  forwarding member exercised by one compatibility test); the
  StructureCandidateEvaluator wrapper class was deleted.
- Centralized: StructureOptimizationValidation::hasCompleteModel is the only
  model-completeness check (ProjectFactory / ProjectAdapter / Template /
  Validation); hasRunnableInputs projects OptimizationPreflight::run.
- Adapter implementations merged 11 -> 3 files (Joint / Pose / Geometry)
  with classes, registry IDs, ABI and validation order unchanged.
- Verification: core, plugin and test targets build clean (MSVC x64 Debug);
  all 43 sdurws_structureoptimizer CTest tests pass; the fixed-seed
  UR-6-85-5-A acceptance run keeps best candidate index, scores,
  reachability, collision-free rate, coverage and fingerprints unchanged
  (only timing fields differ); the Windows GUI widget suite passes with
  QT_QPA_PLATFORM=windows.
- Known runtime gaps and display-consistency notes are recorded in
  ARCHITECTURE.md with source anchors.

## Dead-code deletion record (2026-08-25, Tier A)

Deleted the superseded alternative implementations and their test suites
(git history retains everything): HybridOptimizer, LocalSearch, EliteSelector,
QuickScreeningPolicy, InitialSampler, DeterministicSeed, EvaluationCache,
CacheKey, IndependentFinalVerifier, FinalValidationPlan,
CandidateEvaluationScheduler, KinematicMetricAggregator, EstimatedWorkspaceStage,
OrientationCoverageStage, DesignTemplateApplication - 30 files / ~1900 source
lines plus ~915 lines of tests and 10 CTest entries.  Kept (test-target only):
OptimizationCheckpoint, Phase8 audits, OptimizationRunSnapshot/RunJson/RunStore,
StructureOptimizationWorkflowResolver, LegacyDesignSpaceAdapter,
CanonicalForwardKinematics, StructureOptimizationMigration.  Verification:
core/plugin/test build clean; all 33 remaining CTest tests pass; widget GUI
suite passes.
