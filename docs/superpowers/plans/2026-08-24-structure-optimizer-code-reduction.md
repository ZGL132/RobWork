# Structure Optimizer Code Reduction Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** 在不改变固定种子结果、项目格式和 Qt 插件行为的前提下，移出未接入运行时的代码，消除评估器/优化器双轨实现，并收敛结构优化插件的核心边界。

**Architecture:** 先把只被测试使用的实现从 sdurws_structureoptimizer_core 移到测试目标，再统一候选评估入口和混合优化入口。运行时最终采用 canonical model → design-space/candidate compiler → engineering evaluator pipeline → one hybrid optimizer 的单链路；legacy 类型只保留在 JSON/UI/迁移边界。适配器和 UI 的物理文件合并放在核心行为稳定之后执行。

**Tech Stack:** C++17/RobWork CMake、Qt 5/6、QtConcurrent、PowerShell、CTest、MSVC x64 Developer Environment。

---

## 现状与不变约束

- 代码目录：RobWork/RobWorkStudio/src/rwslibs/structureoptimizer。
- 当前约 239 个文件，其中 111 个 cpp、119 个 hpp；CoreSrcFiles 中有 101 个核心源文件。
- StructureOptimizationTest.cpp 约 12,542 行，约 111 个测试函数；它不是插件运行时代码。
- 生产优化链路：

  ~~~text
  StructureOptimizerWidget
    -> StructureOptimizationController
    -> SystemEngineeringOptimizer
    -> HybridStructureOptimizer
    -> PipelineCandidateEvaluator
    -> EngineeringEvaluatorPipeline
    -> KinematicEngineeringEvaluator
    -> candidate evaluation implementation
  ~~~

- 基线评估另走 CanonicalBaselineEvaluationBridge，不能在没有等价性测试的情况下删除任一评估路径。
- 旧项目 JSON 读取/迁移至少在一个明确的兼容窗口内保留。
- Windows GUI/Qt 测试必须在 Visual Studio x64 Developer Environment 中运行，设置 QT_QPA_PLATFORM=windows，一次只启动一个可执行文件。
- 每个阶段单独提交；不使用 git reset --hard，不覆盖用户已有未提交修改。

## 目标文件边界

### 继续保留在运行库

数据与兼容边界：StructureOptimizationTypes、DesignVariable、ParameterBinding、DesignSpaceCompiler、DesignVector。

canonical 模型：CanonicalKinematicModel、KinematicModelImporter、CanonicalModelShadowService。

候选编译与适配：CandidateCompiler、CandidatePatch 系列、AdapterRegistry、各类 parameter adapter。

评价与优化：EngineeringEvaluatorPipeline、KinematicEngineeringEvaluator、SystemEngineeringOptimizer、HybridStructureOptimizer、StructureObjectiveScorer、StructureCandidateCache。

项目和导出：StructureOptimizationJson、StructureOptimizationProjectAdapter、StructureOptimizationProjectFactory、StructureOptimizationExportService、CSV/报告/候选导出。

UI 与控制：StructureOptimizerWidget、StructureOptimizationController、table models、CandidatePreviewController。

### 第一阶段移出核心库

以下模块当前没有生产调用方，只被 StructureOptimizationTest.cpp 或彼此引用：

- HybridOptimizer.*
- InitialSampler.*
- LocalSearch.*
- FinalValidationPlan.*
- IndependentFinalVerifier.*
- CandidateEvaluationScheduler.*
- OptimizationCheckpoint.*
- Phase8Acceptance.*
- Phase8PerformanceAudit.*
- Phase8ResourceAudit.*
- Phase8ReleaseManifest.*
- EliteSelector.*
- QuickScreeningPolicy.*
- EvaluationCache.*
- CacheKey.*
- KinematicMetricAggregator.*
- EstimatedWorkspaceStage.*
- OrientationCoverageStage.*
- DesignTemplateApplication.*
- DhProjection.*
- CanonicalForwardKinematics.*

OptimizationRunSnapshot.*、OptimizationRunJson.*、OptimizationRunStore.*、StructureOptimizationWorkflowResolver.* 也没有接入控制器或 Widget，作为待接入功能单独处理。

以上“无生产引用”的判定只针对当前仓库源代码。它们目前仍在 CoreHeaderFiles 中，由 rw_add_includes 导出；移出前必须检查安装树和仓库内全部下游目标，不能仅凭 structureoptimizer 目录内的 grep 删除公开头文件。

---

### Task 1: 建立基线、引用清单和回滚点

**Files:** 只读 CMakeLists.txt、StructureOptimizationTest.cpp、README.md、ARCHITECTURE.md。

- [ ] Step 1: 检查工作树。

~~~powershell
git status --short
git diff -- RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
~~~

Expected: 只记录现有状态；目标目录已有修改时不得覆盖。

- [ ] Step 2: 记录候选模块的生产/测试引用。

~~~powershell
$p = 'RobWork/RobWorkStudio/src/rwslibs/structureoptimizer'
foreach ($name in 'HybridOptimizer','InitialSampler','LocalSearch','FinalValidationPlan','IndependentFinalVerifier','CandidateEvaluationScheduler','OptimizationCheckpoint','Phase8Acceptance','Phase8PerformanceAudit','Phase8ResourceAudit','Phase8ReleaseManifest','EliteSelector','QuickScreeningPolicy','EvaluationCache','CacheKey','KinematicMetricAggregator','EstimatedWorkspaceStage','OrientationCoverageStage','DesignTemplateApplication','DhProjection','CanonicalForwardKinematics','OptimizationRunSnapshot','OptimizationRunStore','OptimizationRunJson','StructureOptimizationWorkflowResolver') {
    Write-Output "--- $name"
    rg -l --glob '*.{cpp,hpp}' "\b$name\b" $p
}
~~~

Expected: 只有测试或模块内部引用的文件进入 Task 2。

- [ ] Step 3: 构建基线测试目标。

从 RobWork 子项目根目录执行：

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test
~~~

Expected: 测试目标构建成功。

- [ ] Step 4: 运行基线测试并记录通过数量、失败名称、总耗时。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer" --output-on-failure
~~~

- [ ] Step 5: 提交计划和基线记录。

~~~powershell
git add docs/superpowers/plans/2026-08-24-structure-optimizer-code-reduction.md
git commit -m "docs: add structure optimizer code reduction plan"
~~~

---

### Task 2: 将全部仓库内仅测试可达模块从核心库移出

**Files:** RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt 的 CoreSrcFiles、CoreHeaderFiles、测试目标列表。

- [ ] Step 1: 从 CoreSrcFiles 删除 21 个仅测试可达 cpp。

删除 HybridOptimizer.cpp、LocalSearch.cpp、FinalValidationPlan.cpp、IndependentFinalVerifier.cpp、CandidateEvaluationScheduler.cpp、OptimizationCheckpoint.cpp、InitialSampler.cpp、Phase8Acceptance.cpp、Phase8PerformanceAudit.cpp、Phase8ResourceAudit.cpp、Phase8ReleaseManifest.cpp、EliteSelector.cpp、QuickScreeningPolicy.cpp、EvaluationCache.cpp、CacheKey.cpp、KinematicMetricAggregator.cpp、EstimatedWorkspaceStage.cpp、OrientationCoverageStage.cpp、DesignTemplateApplication.cpp、DhProjection.cpp、CanonicalForwardKinematics.cpp。

- [ ] Step 2: 将这 21 个 cpp 加入 add_executable(sdurws_structureoptimizer_test ...)。

保留原测试头文件的源码目录 include 路径，使 StructureOptimizationTest.cpp 的 include 不变。

- [ ] Step 3: 从 CoreHeaderFiles 删除对应的 21 个 hpp，避免将测试契约安装成公开 API。

CoreHeaderFiles 会传给 rw_add_includes；因此此步骤同时消除这些头文件的导出/安装路径。执行前后均运行：

~~~powershell
rg -n "EliteSelector|QuickScreeningPolicy|EvaluationCache|CacheKey|KinematicMetricAggregator|EstimatedWorkspaceStage|OrientationCoverageStage|DesignTemplateApplication|DhProjection|CanonicalForwardKinematics|Phase8" RobWork/RobWorkStudio/src RobWork/RobWorkStudio/CMakeLists.txt
~~~

Expected: 这些名字只允许出现在测试目标源文件列表、测试源文件和历史文档；不得仍在 CoreSrcFiles、CoreHeaderFiles 或其他安装清单中。

- [ ] Step 4: 构建并运行原有算法、阶段、缓存、canonical FK、设计模板和 DH 投影测试。

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(hybrid_optimizer|local_search|cache|cache_key|kinematic_metric_aggregator|spatial_evaluation_stages|phase8_.*)" --output-on-failure
~~~

Expected: 无参数主测试覆盖 CanonicalForwardKinematics、DhProjection、DesignTemplateApplication 等没有独立 CTest 名称的测试；所有命令通过，且 core 目标不再编译这些源文件。

- [ ] Step 5: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "build: move test-only optimizer helpers out of core"
~~~

---

### Task 3: 处理未接入运行时的运行快照和 workflow resolver 模块

**Files:** CMakeLists.txt、OptimizationRunSnapshot.*、OptimizationRunJson.*、OptimizationRunStore.*、StructureOptimizationWorkflowResolver.*、README.md。

- [ ] Step 1: 确认没有控制器或 Widget 调用方。

~~~powershell
$p = 'RobWork/RobWorkStudio/src/rwslibs/structureoptimizer'
rg -n --glob '*.{cpp,hpp}' --glob '!StructureOptimizationTest.cpp' "OptimizationRunSnapshot|OptimizationRunStore|OptimizationRunJson|StructureOptimizationWorkflowResolver" $p
~~~

Expected: 只出现实现文件之间的内部引用。

- [ ] Step 2: 默认将这些 cpp/hpp 从 CoreSrcFiles/CoreHeaderFiles 移到测试目标。

若产品确实要求断点续跑或 workflow 持久化，必须先建立控制器调用链、项目文件字段和恢复测试，再保留运行库版本。

- [ ] Step 3: 在 README 的限制项中说明它们是测试覆盖的预备模块，尚未接入插件主流程。

- [ ] Step 4: 运行测试。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(run_snapshot|run_store|workflow_resolver)" --output-on-failure
~~~

- [ ] Step 5: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/README.md
git commit -m "build: isolate unconnected run persistence modules"
~~~

---

### Task 4: 删除无行为源文件并清理明显重复调用

**Files:** 删除 StructureOptimizationDocument.cpp；修改 CMakeLists.txt 和 StructureOptimizerPlugin.cpp。

- [ ] Step 1: 从 CoreSrcFiles 删除 StructureOptimizationDocument.cpp，保留 StructureOptimizationDocument.hpp 的协议常量。

- [ ] Step 2: 在 StructureOptimizerPlugin.cpp 保留一次 dirty 更新和一次宿主通知：

~~~cpp
_projectProvider->setDirty(_widget->isProjectDocumentDirty());
studio->notifyProjectDocumentChanged();
~~~

删除紧邻的第二次 setDirty 调用。

- [ ] Step 3: 构建插件。

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer
~~~

- [ ] Step 4: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp
git rm RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationDocument.cpp
git commit -m "cleanup: remove empty document unit and duplicate dirty update"
~~~

---

### Task 5: 统一候选评估器入口

**Files:** KinematicEngineeringEvaluator.hpp/.cpp、StructureCandidateEvaluator.hpp/.cpp、SystemEngineeringOptimizer.cpp、StructureOptimizationController.cpp、StructureOptimizationTest.cpp、CMakeLists.txt。

**设计决定：** 保留 IStructureCandidateEvaluator 作为依赖注入接口，但只保留一个运动学实现。KinematicEngineeringEvaluator 增加：

~~~cpp
class KinematicEngineeringEvaluator : public IEngineeringEvaluator
{
public:
    void evaluateCandidate(StructureCandidateResult& candidate,
                           StructureEvaluationStage stage,
                           const StructureOptimizationCallbacks& callbacks,
                           StructureCandidateCache* cache = nullptr);
};
~~~

IEngineeringEvaluator::evaluate 只负责把候选结果映射为公共工程结果；模型构建、IK、碰撞和指标汇总由同一个候选实现完成。

- [ ] Step 1: 在现有 shared evaluator consistency 测试旁增加固定问题和固定候选值，比较旧包装器与新入口的 status、feasible、requiredReachableCount、collisionFreeRate、totalScore。

- [ ] Step 2: 将 evaluateLegacy 改名为 evaluateCandidate，暂时保留兼容转发：

~~~cpp
void KinematicEngineeringEvaluator::evaluateLegacy(
    StructureCandidateResult& candidate,
    StructureEvaluationStage stage,
    const StructureOptimizationCallbacks& callbacks,
    StructureCandidateCache* cache)
{
    evaluateCandidate(candidate, stage, callbacks, cache);
}
~~~

- [ ] Step 3: 将 StructureCandidateEvaluator.cpp 中的候选评估实现移动到 KinematicEngineeringEvaluator.cpp，使文件名与实现类一致。StructureCandidateEvaluator::evaluate 只保留转发，不再包含业务逻辑。

- [ ] Step 4: 迁移 SystemEngineeringOptimizer.cpp、StructureOptimizationController.cpp 和测试调用方到 evaluateCandidate。

- [ ] Step 5: 删除所有直接调用 evaluateLegacy 的生产代码，只保留一个兼容性测试。

- [ ] Step 6: 删除前检查公开安装边界和全部仓库下游依赖。

~~~powershell
rg -n "StructureCandidateEvaluator" RobWork -g '*.{cpp,hpp,cmake,txt}'
rg -n "StructureCandidateEvaluator" RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
~~~

Expected: 除迁移提交中的删除记录外，没有生产源文件、CoreHeaderFiles、rw_add_includes 或下游目标依赖该头文件。若存在仓库外 ABI 消费者，则先提供独立兼容库或保留弃用头，不得直接删除。

- [ ] Step 7: 确认上述检查通过后，删除 StructureCandidateEvaluator.cpp/.hpp，并从 CMake 清单删除。

- [ ] Step 8: 运行评估器回归。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(evaluator_consistency|evaluator|engineering_pipeline|phase6_integration)" --output-on-failure
~~~

- [ ] Step 9: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "refactor: unify structure candidate evaluation path"
~~~

---

### Task 6: 确认并保留唯一生产优化编排器

**Files:** HybridStructureOptimizer.cpp/.hpp、StructureCandidateGenerator.*、测试专用 HybridOptimizer/InitialSampler/LocalSearch/FinalValidationPlan/IndependentFinalVerifier、StructureOptimizationTest.cpp。

- [ ] Step 1: 增加固定种子生产流程特征测试，断言 baselineCandidateIndex、quickEvaluatedCandidates、verifiedEliteCandidates、finalVerifiedCandidates 和 sensitivityEvaluations 的语义。

- [ ] Step 2: 确认生产只引用 StructureCandidateGenerator。

~~~powershell
rg -n --glob '*.{cpp,hpp}' --glob '!StructureOptimizationTest.cpp' "InitialSampler|StructureCandidateGenerator" RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
~~~

Expected: 生产路径只保留 StructureCandidateGenerator。

- [ ] Step 3: 保留 HybridStructureOptimizer.cpp 内部已经用于生产的局部搜索；LocalSearch 不重新接回核心库。若未来复用，先把接口改成生产候选类型，再做等价性测试。

- [ ] Step 4: 确认最终验证只由 HybridStructureOptimizer::optimize 管理，FinalValidationPlan 和 IndependentFinalVerifier 不进入运行时。

- [ ] Step 5: 运行优化主链测试。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(hybrid_optimizer|optimizer|hybrid_verification|sensitivity)" --output-on-failure
~~~

- [ ] Step 6: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "refactor: keep one production optimization orchestrator"
~~~

---

### Task 7: 收敛校验入口和上下文完整性判断

**Files:** StructureOptimizationValidation.hpp/.cpp、OptimizationPreflight.hpp/.cpp、StructureOptimizationUiLogic.cpp、StructureOptimizationProjectFactory.cpp、StructureOptimizationProjectAdapter.cpp、StructureOptimizationTest.cpp。

**目标接口：**

~~~cpp
static bool hasCompleteModel(const RobotModelSpec& spec,
                             std::string* reason = nullptr);
~~~

OptimizationPreflight::run(problem) 负责组合所有校验，UI 不再重复遍历变量和任务。

- [ ] Step 1: 为 hasCompleteModel 增加空 robot name、空 transform joints、合法完整模型测试，并断言错误原因稳定。

- [ ] Step 2: Factory 和 ProjectAdapter 统一调用 hasCompleteModel，不再手写 robotName.empty 与 transformJoints.empty 判断。

- [ ] Step 3: 删除 UiLogic::hasRunnableInputs 中的重复变量/任务扫描，改为读取 preflight.canStart：

~~~cpp
const OptimizationPreflightResult result = OptimizationPreflight::run(problem);
if (result.canStart) {
    if (reason != nullptr) reason->clear();
    return true;
}
if (reason != nullptr)
    *reason = firstBlockingFindingMessage(result);
return false;
~~~

firstBlockingFindingMessage 只能使用已有 finding 的 code、message、remediation，不能再次实现校验规则。

- [ ] Step 4: 中文提示只保留在 UI 投影层，核心输出稳定错误码。

- [ ] Step 5: 运行校验测试。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(preflight_core|controller_state|project_context|phase1_core)" --output-on-failure
~~~

- [ ] Step 6: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "refactor: centralize optimization preflight validation"
~~~

---

### Task 8: 可选的变量建议命名整理（不属于本轮核心减法）

**Files:** 将 StructureOptimizationUiLogic.hpp/.cpp 重命名为 StructureVariableSuggestion.hpp/.cpp；修改 ProjectFactory、Widget、CMakeLists.txt、测试。

该任务没有运行行为收益，且触及 Widget、Factory、CMake 和测试。默认不在本轮代码压缩发布中执行；只有 Task 12 的运行库清理、评估器归一和兼容验证全部通过后，才可单独创建后续提交执行。

- [ ] Step 1: 增加纯核心变量建议测试，覆盖 transform joint、base height、TCP、drawable，验证数量、单位和默认范围 0.7 到 1.3。

- [ ] Step 2: 将调用改为：

~~~cpp
StructureVariableSuggestion::suggest(context)
~~~

preflight 和 hasRunnableInputs 不再与变量建议混在同一类。

- [ ] Step 3: 确认全仓库不再出现 StructureOptimizationUiLogic 后删除旧类名和旧文件。

- [ ] Step 4: 运行 Widget 和 Factory 测试。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(variable_table|variable_actions|widget|project_factory)" --output-on-failure
~~~

- [ ] Step 5: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "refactor: separate variable suggestions from UI logic"
~~~

---

### Task 9: 明确 legacy 与 canonical 模型边界，不擅自改变 JSON 迁移行为

**Files:** LegacyDesignSpaceAdapter.*、StructureOptimizationJson.cpp、StructureOptimizationMigration.*、StructureOptimizationTypes.hpp、StructureOptimizationProjectFactory.cpp、StructureOptimizationTest.cpp、README.md、ARCHITECTURE.md。

**兼容策略：**

- 旧格式只读并迁移到 current envelope；
- current envelope 是唯一写出格式；
- 核心优化消费 canonical model、compiled design space、typed bindings；
- legacy StructureOptimizationProblem 只作为 UI/项目边界投影。

- [ ] Step 1: 列出 legacy 入口并分为 JSON 兼容、UI 展示、核心计算三类。

~~~powershell
rg -n "legacy|Legacy|evaluateLegacy|StructureOptimizationProblem|StructureDesignVariable|StructureConstraint" RobWork/RobWorkStudio/src/rwslibs/structureoptimizer -g '*.{cpp,hpp}'
~~~

核心计算中的 legacy 使用逐项迁移。

- [ ] Step 2: 在不接线生产迁移路径的前提下，先写 LegacyDesignSpaceAdapter 与现有 JSON binding 投影的等价性测试。

测试构造一个同时包含 BaseHeight、JointPositionX、DhA 和非法单位变量的 legacy problem；调用 currentEnvelopeToJson 解析出 designSpace.bindings，再调用 LegacyDesignSpaceAdapter::preview。断言每个变量的 ID、单位、enabled、binding ID、adapter ID、targetName 和诊断 disposition 完全一致。测试骨架：

~~~cpp
const QJsonObject envelope = QJsonDocument::fromJson(
    QByteArray::fromStdString(StructureOptimizationJson::currentEnvelopeToJson(problem))).object();
const QJsonArray jsonBindings = envelope.value("designSpace").toObject().value("bindings").toArray();
const LegacyDesignSpaceMigrationPreview preview =
    LegacyDesignSpaceAdapter::preview(problem, hints);
REQUIRE(normalizeJsonBindings(jsonBindings) == normalizePreviewBindings(preview));
~~~

normalizeJsonBindings 和 normalizePreviewBindings 必须定义在 StructureOptimizationTest.cpp 的匿名命名空间中，并输出排序后的 value object；不得修改生产 JSON 代码来让测试通过。

- [ ] Step 3: 运行等价性测试并做迁移决策。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(legacy_json_migration|current_json_envelope)" --output-on-failure
~~~

Expected: 若等价性测试失败，LegacyDesignSpaceAdapter 继续留在测试目标，不得接线 JSON 迁移；记录字段差异并在后续独立设计中解决。只有所有字段和 fingerprint 等价时，才能进行下一步。

- [ ] Step 4: 仅在 Step 3 等价通过时，将 LegacyDesignSpaceAdapter 接为唯一变量迁移入口，并保留 current envelope 的 binding ID、adapter ID、单位和未知扩展字段输出不变。

- [ ] Step 5: 增加旧 JSON 读取、迁移、current 写出、current 读取、未知扩展字段保留测试。

- [ ] Step 6: 对 UR-6-85-5-A 示例做 round-trip，比较模型、变量、任务、目标、约束和扩展字段 fingerprint。

- [ ] Step 7: 更新 README 和 ARCHITECTURE，明确 canonical 是运行时真相，legacy 只存在于兼容边界；若 Step 3 未等价，明确 adapter 尚未接入生产迁移。

- [ ] Step 8: 运行 JSON/迁移/接受示例测试。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(current_json_envelope|legacy_json_migration|robot_file_acceptance)" --output-on-failure
~~~

- [ ] Step 9: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "refactor: isolate legacy project compatibility at the boundary"
~~~

---

### Task 10: 合并适配器实现文件，减少物理文件数量

**Files:**
- Create: JointParameterAdapters.cpp、PoseParameterAdapters.cpp、GeometryParameterAdapters.cpp
- Delete after migration: BasePlacementAdapter.cpp、FlangePoseAdapter.cpp、TcpPoseAdapter.cpp、JointAxisAdapter.cpp、JointLimitAdapter.cpp、JointOriginAdapter.cpp、JointZeroAdapter.cpp、ParameterizedLinkAdapter.cpp、ParameterizedGeometryAdapter.cpp、ParameterizedCollisionAdapter.cpp、MeshTransformAdapter.cpp
- Modify: CMakeLists.txt
- Test: StructureOptimizationTest.cpp 的 adapter 测试段

只合并 cpp 实现，保留现有 hpp 公共类接口，不改变 ABI 和注册 ID。

- [ ] Step 1: 增加每个 adapter 的行为快照测试，覆盖支持的 SemanticKind、成功 binding、缺失目标、非法属性、写集冲突和 patch 应用结果。

- [ ] Step 2: 按职责移动实现：
  - JointParameterAdapters.cpp：JointAxis、JointLimit、JointOrigin、JointZero；
  - PoseParameterAdapters.cpp：BasePlacement、FlangePose、TcpPose；
  - GeometryParameterAdapters.cpp：ParameterizedLink、ParameterizedGeometry、ParameterizedCollision、MeshTransform。

每个合并文件保留原命名空间、类方法定义和注册行为，不在此阶段改算法。

- [ ] Step 3: 保留 ParameterBindingValidator::validate(binding) 作为唯一通用绑定校验；适配器只检查自己的目标对象和属性语义。

- [ ] Step 4: 从 CoreSrcFiles 删除 11 个旧 cpp，加入 3 个合并 cpp；hpp 安装清单保持原接口。

- [ ] Step 5: 运行适配器测试。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_(adapter_registry|candidate_compiler|projection|parameterized_geometry)" --output-on-failure
~~~

- [ ] Step 6: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "refactor: group parameter adapter implementations"
~~~

---

### Task 11: 整理 JSON、Widget 和测试代码，但不扩大运行库文件数

**Files:** StructureOptimizationJson.cpp、StructureOptimizerWidget.cpp、StructureOptimizationTest.cpp、README.md、ARCHITECTURE.md。

- [ ] Step 1: JSON 中只保留一个有限浮点数策略、一个枚举字符串转换策略、一个扩展字段策略；不在 legacy/current 两条路径中复制相同规则。

- [ ] Step 2: 第一轮不拆分 StructureOptimizationJson.cpp，避免为了可读性增加文件数；先通过删除 legacy 重复写入和统一 helper 降低行数。

- [ ] Step 3: 检查 setEditingEnabled、updateRunState、handleRunningChanged、handlePausedChanged，保证按钮状态只有一个来源。

- [ ] Step 4: 测试夹具、临时模型构造器和 Phase8 审计数据留在测试目标，不回加 core。

- [ ] Step 5: 在 ARCHITECTURE.md 增加“已知运行时缺口与显示一致性”小节，并写入以下已核验事实：

- StructureOptimizerWidget.cpp 的 Required Tasks Reachable 与 StructureConstraintTableModel.cpp 的 Required Task Reachable 是重复标签函数的不同文案；本轮不修 UI 文案，后续统一为一个共享标签入口。
- StructureOptimizationCsv.cpp 使用 std::to_string(double)，CSV 通常为 6 位小数；ReportWriter 使用 fixed/precision(3)，JSON 是数值编码。三种输出精度不一致，本轮不把 CSV 用作 fingerprint 或精度权威来源。
- KinematicEngineeringEvaluator 将 typed raw metrics 投影为以 metricId 字符串索引的 EngineeringMetric，SystemEngineeringOptimizer 再映射回 StructureRawMetrics；这是字段 ID 耦合，不是当前的文本数值往返。后续修改任一 metric ID 必须同时更新生产者、消费者和缺失 metric 测试。
- CanonicalBaselineEvaluationBridge 的默认 AdapterRegistry 为空。adapter 类已实现，但默认生产入口未注册 adapter；adapter binding 链路尚未经过真实默认运行路径的端到端验证。

- [ ] Step 6: 为上述文档断言添加定位检查，防止后续文档与代码脱节。

~~~powershell
rg -n "Required Tasks Reachable|Required Task Reachable" RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureConstraintTableModel.cpp
rg -n "std::to_string\(value\)|setprecision\(3\)" RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationCsv.cpp RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationReportWriter.cpp
rg -n "const AdapterRegistry defaultAdapters" RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/CanonicalBaselineEvaluationBridge.cpp
~~~

Expected: 每条文档记录都能对应到一个当前源码位置；若某项已在独立提交中修复，则删除相应技术债条目并加入该提交的回归测试。

- [ ] Step 7: 运行 UI/报告回归测试。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer_phase7|sdurws_structureoptimizer_(report|export|preview|widget)" --output-on-failure
~~~

- [ ] Step 8: 提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "docs: align structure optimizer architecture after cleanup"
~~~

---

### Task 12: 全量验证、性能比较和发布门

**Files:** 全部 structureoptimizer 源文件；必要时 README.md、PHASE8_RELEASE.md、ARCHITECTURE.md。

- [ ] Step 1: 重新统计目录和 CMake 运行库清单。

~~~powershell
$p = 'RobWork/RobWorkStudio/src/rwslibs/structureoptimizer'
Get-ChildItem $p -File | Group-Object Extension | Select-Object Name,Count
~~~

Expected: 测试专用模块不再出现在核心库 CMake 清单，运行库文件数量较基线下降。

- [ ] Step 2: 在 VS x64 环境中分别构建核心、插件、测试目标。

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_core
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test
~~~

一次只构建一个目标。

- [ ] Step 3: 运行全部 structureoptimizer CTest。

~~~powershell
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_structureoptimizer" --output-on-failure
~~~

所有既有测试必须通过，不得用跳过测试代替修复。

- [ ] Step 4: 运行固定种子 UR-6-85-5-A 接受示例，比较 best candidate index、total score、required reachable count、collision-free rate、workspace coverage、candidate/result fingerprint、审计阶段计数。允许变化的只有明确记录的耗时字段。

- [ ] Step 5: 运行 Windows GUI 测试，一次只启动一个绝对路径可执行文件。

~~~powershell
$env:QT_QPA_PLATFORM = 'windows'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\sdurws_structureoptimizer_test.exe' widget
~~~

若 Qt 平台插件初始化失败，先终止进程，检查继承的 QT_*/QML_* 环境变量，再重新启动。

- [ ] Step 6: 检查移出模块是否仍出现在安装清单或生产引用中。

~~~powershell
rg -n "StructureCandidateEvaluator|HybridOptimizer|InitialSampler|LocalSearch|Phase8|OptimizationRunStore|StructureOptimizationWorkflowResolver|EliteSelector|QuickScreeningPolicy|EvaluationCache|CacheKey|KinematicMetricAggregator|EstimatedWorkspaceStage|OrientationCoverageStage|DesignTemplateApplication|DhProjection|CanonicalForwardKinematics" RobWork/RobWorkStudio/src RobWork/RobWorkStudio/CMakeLists.txt
~~~

若外部组件仍依赖某符号，恢复为明确的兼容库，不要偷偷重新加入核心库。对 CoreHeaderFiles 中移出的每个头，检查安装树和下游链接目标；不以“仓库内没有调用”替代 ABI 检查。

- [ ] Step 7: 将最终文件数量、CTest 通过数量、固定种子比较结果写入 PHASE8_RELEASE.md，并注明移到测试目标的模块。

- [ ] Step 8: 最终提交。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/structureoptimizer RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "refactor: complete structure optimizer runtime reduction"
~~~

---

## 建议提交顺序

1. docs: add structure optimizer code reduction plan
2. build: move test-only optimizer helpers out of core
3. build: isolate unconnected run persistence modules
4. cleanup: remove empty document unit and duplicate dirty update
5. refactor: unify structure candidate evaluation path
6. refactor: keep one production optimization orchestrator
7. refactor: centralize optimization preflight validation
8. refactor: isolate legacy project compatibility at the boundary
9. refactor: group parameter adapter implementations
10. docs: align structure optimizer architecture after cleanup
11. refactor: complete structure optimizer runtime reduction
12. refactor: separate variable suggestions from UI logic (optional follow-up; not in this release)

每个提交都必须能够单独构建；Task 5、Task 9、Task 10 只在前一阶段测试通过后继续。

## 完成标准

- 运行库中不再编入只被测试使用的算法、Phase8 审计、未接入运行快照模块，以及 EliteSelector、QuickScreeningPolicy、EvaluationCache、CacheKey、KinematicMetricAggregator、EstimatedWorkspaceStage、OrientationCoverageStage、DesignTemplateApplication、DhProjection、CanonicalForwardKinematics。
- 生产优化流程只保留一套混合优化编排器。
- 生产候选运动学评估只保留一个实现，不再通过 evaluateLegacy 跨文件承载主逻辑。
- UI、Factory、ProjectAdapter 使用同一套 preflight/context validation 结果。
- legacy JSON 仍可读取，current envelope 仍可写出，固定 fingerprint 不变。
- LegacyDesignSpaceAdapter 只有在与现有 JSON binding 投影完成字段级和 fingerprint 等价验证后才能接入生产；不等价时必须保持测试专用状态。
- 已知用户可见的约束标签不一致、CSV/报告/JSON 精度差异、metricId 映射耦合和默认空 AdapterRegistry 状态已记录在 ARCHITECTURE.md，并各自保留源码定位。
- 现有 structureoptimizer CTest 全部通过；Windows GUI 测试按项目规则通过。
- 固定 seed 的候选结果、分数、fingerprint 和审计计数保持一致，性能变化有记录且没有隐藏的准确性回退。
