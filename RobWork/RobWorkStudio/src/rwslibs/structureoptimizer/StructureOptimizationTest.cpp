#include "StructureOptimizationTypes.hpp"
#include "EvaluationPlan.hpp"
#include "EvaluationStage.hpp"
#include "MetricRegistry.hpp"
#include "ConstraintObjective.hpp"
#include "TaskEvaluationStage.hpp"
#include "VerifiedRegionStage.hpp"
#include "CandidateResult.hpp"
#include "OptimizationRunStateMachine.hpp"
#include "OptimizationCheckpoint.hpp"
#include "CanonicalBaselineEvaluationBridge.hpp"
#include "StructureOptimizationContracts.hpp"
#include "KinematicConventions.hpp"
#include "CanonicalKinematicModel.hpp"
#include "CanonicalForwardKinematics.hpp"
#include "KinematicModelImporter.hpp"
#include "DhProjection.hpp"
#include "KinematicFingerprint.hpp"
#include "KinematicBaselineSnapshot.hpp"
#include "CanonicalModelShadowService.hpp"
#include "DesignVariable.hpp"
#include "ParameterBinding.hpp"
#include "DesignSpaceRegistry.hpp"
#include "ParameterizationMode.hpp"
#include "WriteSetValidator.hpp"
#include "DerivedExpression.hpp"
#include "DependencyGraph.hpp"
#include "DesignSpaceCompiler.hpp"
#include "DesignVector.hpp"
#include "LegacyDesignSpaceAdapter.hpp"
#include "CandidatePatch.hpp"
#include "CandidatePatchMerge.hpp"
#include "CandidatePatchApply.hpp"
#include "CandidateCompiler.hpp"
#include "RobotModelSpecProjectionAdapter.hpp"
#include "EvaluationDeviceBuilder.hpp"
#include "ModelParameterAdapter.hpp"
#include "AdapterRegistry.hpp"
#include "JointOriginAdapter.hpp"
#include "ParameterizedLinkAdapter.hpp"
#include "JointAxisAdapter.hpp"
#include "JointZeroAdapter.hpp"
#include "JointLimitAdapter.hpp"
#include "BasePlacementAdapter.hpp"
#include "FlangePoseAdapter.hpp"
#include "TcpPoseAdapter.hpp"
#include "ParameterizedGeometryAdapter.hpp"
#include "ParameterizedCollisionAdapter.hpp"
#include "MeshTransformAdapter.hpp"
#include "PoseDelta.hpp"
#include "StructureOptimizationValidation.hpp"
#include "StructureDesignMutator.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureCandidateGenerator.hpp"
#include "StructureCandidateCache.hpp"
#include "CandidateModelFactory.hpp"
#include "KinematicEngineeringEvaluator.hpp"
#include "EngineeringEvaluatorPipeline.hpp"
#include "SystemEngineeringOptimizer.hpp"
#include "HybridStructureOptimizer.hpp"
#include "StructureOptimizationStrategy.hpp"
#include "StructureSensitivityAnalyzer.hpp"
#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationDocument.hpp"
#include "StructureOptimizationMigration.hpp"
#include "OptimizationRunSnapshot.hpp"
#include "OptimizationRunJson.hpp"
#include "OptimizationRunStore.hpp"
#include "StructureOptimizationWorkflowResolver.hpp"
#include "OptimizationPreflight.hpp"
#include "Phase8Acceptance.hpp"
#include "Phase8PerformanceAudit.hpp"
#include "Phase8ResourceAudit.hpp"
#include "Phase8ReleaseManifest.hpp"
#include "StructureOptimizationCsv.hpp"
#include "StructureVariableTableModel.hpp"
#include "StructureVariableFilterProxyModel.hpp"
#include "OptimizationTaskTableModel.hpp"
#include "StructureCandidateTableModel.hpp"
#include "StructureOptimizationUiLogic.hpp"
#include "StructureOptimizationTemplate.hpp"
#include "StructureCandidateComparison.hpp"
#include "StructureOptimizerPlugin.hpp"
#include "StructureOptimizerWidget.hpp"
#include "StructureOptimizationController.hpp"
#include "StructureOptimizationObjectiveProfile.hpp"
#include "StructureConstraintTableModel.hpp"
#include "StructureOptimizationProjectAdapter.hpp"
#include "StructureOptimizationProjectFactory.hpp"
#include "RobotModelStalenessChecker.hpp"
#include "StructureWorkspaceCoverage.hpp"
#include "StructureOptimizationExportService.hpp"
#include "StructureOptimizationReportWriter.hpp"
#include "StructureCandidateExporter.hpp"
#include "CandidatePreviewController.hpp"
#include "EngineeringRequirementArtifactAdapter.hpp"
#include "FrozenRequirementProjectImportService.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>
#include <rwslibs/engineeringrequirements/RequirementSetJson.hpp>
// 用于构造与校验 v4 工件的执行契约(execution)与执行指纹。
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>

#include <rwslibs/kinematicanalysis/FrozenRequirementKinematicAdapter.hpp>
#include <rwslibs/kinematicanalysis/ConfigurationEvaluator.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisContext.hpp>
#include <rwslibs/kinematicanalysis/RegionCoverageEvaluator.hpp>
#include <rwslibs/kinematicanalysis/TargetEvaluator.hpp>
#include <rwslibs/kinematicanalysis/OrientationCoverageEvaluator.hpp>

#include <rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelPublishService.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelProjectPaths.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>
#include <rwslibs/robotmodelbuilder/WorkCellConverter.hpp>

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/ProjectManager.hpp>
#include <rws/RobWorkStudio.hpp>

#include <rw/loaders/WorkCellLoader.hpp>

#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/kinematics/MovableFrame.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/PrismaticJoint.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/UniversalJoint.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Q.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QMetaObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QSet>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTableView>
#include <QTabWidget>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <stdexcept>
#include <limits>
#include <cstdlib>
#include <memory>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <vector>

// =============================================================================
//  最小化测试框架 (无依赖)
// =============================================================================

static int g_testFailures = 0;

static void require(bool condition, const char* file, int line, const char* expr)
{
    if (!condition)
    {
        std::fprintf(stderr, "  FAIL at %s:%d: %s\n", file, line, expr);
        ++g_testFailures;
    }
}

#define REQUIRE(cond) require((cond), __FILE__, __LINE__, #cond)

class PassingEvaluationStage final : public rws::EvaluationStage
{
  public:
    std::string id() const override { return "passing"; }
    std::vector<std::string> requiredCapabilities() const override { return {"target"}; }
    rws::EvaluationStageResult run(const rws::EvaluationStageContext&, std::atomic_bool*) const override
    {
        rws::EvaluationStageResult result;
        result.status = rws::EvaluationStageStatus::Passed;
        result.completedCount = 1;
        result.requestedCount = 1;
        return result;
    }
};

static void testEvaluationPipelineAndMetricRegistry()
{
    std::printf("testEvaluationPipelineAndMetricRegistry ... ");
    rws::RequirementExecutionSet requirements;
    requirements.schemaVersion = 4;
    requirements.provenance.requirementFingerprint = "req";
    rws::EvaluationPlanCompilerOptions options;
    options.modelFingerprint = "model";
    options.capabilities.insert("target");
    const rws::EvaluationPlan plan = rws::EvaluationPlanCompiler::compile(requirements, options);
    rws::EvaluationPipeline pipeline;
    pipeline.addStage(std::make_shared<PassingEvaluationStage>());
    const auto results = pipeline.run(plan, "candidate");
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].stageId == "passing");
    REQUIRE(results[0].status == rws::EvaluationStageStatus::Passed);

    rws::EvaluationPlan withoutCapability = plan;
    withoutCapability.capabilities.clear();
    const auto insufficient = pipeline.run(withoutCapability);
    REQUIRE(insufficient.size() == 1);
    REQUIRE(insufficient[0].status == rws::EvaluationStageStatus::DataInsufficient);

    rws::MetricRegistry registry;
    std::string error;
    REQUIRE(registry.registerMetric({"custom.metric", "m", "test", true, true, false, true}, &error));
    REQUIRE(!registry.registerMetric({"custom.metric", "m", "test", true, true, false, true}, &error));
    REQUIRE(registry.find("custom.metric") != nullptr);
    const rws::MetricResult unavailable{"custom.metric", 0.0, "m",
                                        rws::MetricAvailability::InsufficientData};
    REQUIRE(!unavailable.usable());
    REQUIRE(rws::MetricRegistry::standard().find("jacobian.manipulability_normalized") != nullptr);
    std::printf("PASSED\n");
}

static void testConstraintObjectiveAggregation()
{
    std::printf("testConstraintObjectiveAggregation ... ");
    const rws::MetricResult metric{"m", 0.4, "ratio", rws::MetricAvailability::Available};
    rws::ConstraintSpec hard;
    hard.id = "hard";
    hard.metricId = "m";
    hard.threshold = 0.8;
    const rws::ConstraintResult hardResult = rws::ConstraintEvaluator::evaluate(hard, metric);
    REQUIRE(!hardResult.satisfied);
    REQUIRE(hardResult.evidenceAvailable);

    rws::ObjectiveSpec objective;
    objective.id = "maximize";
    objective.metricId = "m";
    objective.weight = 1.0;
    const auto objectiveResult = rws::ObjectiveAggregator::evaluate(objective, metric);
    REQUIRE(objectiveResult.usable);
    const auto aggregate = rws::ObjectiveAggregator::aggregate({hardResult}, {objectiveResult});
    REQUIRE(!aggregate.feasible);
    REQUIRE(aggregate.score > 0.0);

    const rws::MetricResult missing{"m", 0.0, "ratio", rws::MetricAvailability::InsufficientData};
    const auto missingResult = rws::ConstraintEvaluator::evaluate(hard, missing);
    REQUIRE(!missingResult.evidenceAvailable);
    REQUIRE(!rws::ObjectiveAggregator::evaluate(objective, missing).usable);

    rws::MetricResult verified = metric;
    verified.evidenceStage = "Quick";
    rws::ConstraintSpec evidenceConstraint = hard;
    evidenceConstraint.requiredEvidenceStage = "Verified";
    const auto insufficientStage =
        rws::ConstraintEvaluator::evaluate(evidenceConstraint, verified);
    REQUIRE(!insufficientStage.evidenceAvailable);

    rws::ConstraintSpec less;
    less.id = "less";
    less.comparison = rws::ConstraintComparison::LessThanOrEqual;
    less.threshold = 0.5;
    less.tolerance = 0.1;
    REQUIRE(rws::ConstraintEvaluator::evaluate(less, metric).satisfied);
    rws::ConstraintSpec equal;
    equal.id = "equal";
    equal.comparison = rws::ConstraintComparison::Equal;
    equal.threshold = 0.4;
    equal.tolerance = 0.01;
    REQUIRE(rws::ConstraintEvaluator::evaluate(equal, metric).satisfied);
    rws::ConstraintSpec range;
    range.id = "range";
    range.comparison = rws::ConstraintComparison::InRange;
    range.threshold = 0.3;
    range.upperThreshold = 0.5;
    REQUIRE(rws::ConstraintEvaluator::evaluate(range, metric).satisfied);

    rws::ConstraintSpec highPriority = hard;
    highPriority.id = "high";
    highPriority.priority = 10;
    rws::ConstraintSpec lowPriority = hard;
    lowPriority.id = "low";
    lowPriority.priority = 1;
    const auto ordered = rws::ObjectiveAggregator::aggregate(
        {rws::ConstraintEvaluator::evaluate(lowPriority, metric),
         rws::ConstraintEvaluator::evaluate(highPriority, metric)}, {});
    REQUIRE(ordered.constraints.front().id == "high");
    std::printf("PASSED\n");
}

static bool cancellationRequested(void* data)
{
    return data != nullptr && *static_cast<bool*>(data);
}

static void testTaskEvaluationStage()
{
    std::printf("testTaskEvaluationStage ... ");
    rws::EvaluationPlan plan;
    plan.status = rws::EvaluationPlanStatus::Valid;
    rws::EvaluationPlanTask must;
    must.source.id = "must-target";
    must.source.compileState = rws::RequirementExecutionCompileState::Included;
    must.source.level = rws::RequirementExecutionLevel::Must;
    must.hardConstraint = true;
    plan.tasks.push_back(must);

    rws::AnalysisContext context;
    const rws::TaskEvaluationResult result =
        rws::TaskEvaluationStage().evaluate(context, plan);
    REQUIRE(result.tasks.size() == 1);
    REQUIRE(result.tasks.front().taskId == "must-target");
    REQUIRE(result.stage.status == rws::EvaluationStageStatus::DataInsufficient);
    REQUIRE(result.stage.completedCount == 1);
    REQUIRE(result.mustFeasibility == rws::Feasibility::DataInsufficient);
    REQUIRE(!result.tasks.front().failureCodes.empty());

    bool cancel = true;
    rws::CancellationToken token;
    token.isCancellationRequested = &cancellationRequested;
    token.userData = &cancel;
    const rws::TaskEvaluationResult canceled =
        rws::TaskEvaluationStage().evaluate(context, plan, token);
    REQUIRE(canceled.stage.status == rws::EvaluationStageStatus::Canceled);
    REQUIRE(canceled.stage.completedCount == 0);
    REQUIRE(canceled.mustFeasibility == rws::Feasibility::NotEvaluated);
    std::printf("PASSED\n");
}

static void testCandidateResultAssembly()
{
    std::printf("testCandidateResultAssembly ... ");
    rws::CandidateAssemblyInput input;
    input.candidateId = "candidate-1";
    input.feasibility = rws::Feasibility::Feasible;
    input.evidenceStage = rws::AnalysisEvidenceStage::Verified;
    input.quality = rws::Quality::Good;
    input.completion.requestedCount = 2;
    input.completion.completedCount = 2;
    input.representativeQ = {1.0, 2.0};
    input.metrics.push_back({"m", 0.8, "ratio", rws::MetricAvailability::Available});
    rws::ConstraintResult constraint;
    constraint.id = "hard";
    constraint.hard = true;
    constraint.satisfied = true;
    constraint.evidenceAvailable = true;
    input.constraints.push_back(constraint);
    rws::ObjectiveResult objective;
    objective.id = "score";
    objective.usable = true;
    objective.contribution = 0.8;
    input.objectives.push_back(objective);
    rws::EvaluationStageResult stage;
    stage.stageId = "task_evaluation";
    stage.status = rws::EvaluationStageStatus::Passed;
    stage.requestedCount = 2;
    stage.completedCount = 2;
    input.stages.push_back(stage);

    const auto result = rws::CandidateResultAssembler::assemble(input);
    REQUIRE(result.lifecycle == rws::CandidateLifecycle::Completed);
    REQUIRE(result.feasibility == rws::Feasibility::Feasible);
    REQUIRE(result.stages.size() == 1);
    REQUIRE(result.metrics.size() == 1);
    REQUIRE(result.representativeQ.size() == 2);
    REQUIRE(rws::CandidateResultAssembler::toLegacy(result, 3).status ==
            rws::StructureCandidateStatus::Feasible);

    input.compileSucceeded = false;
    input.compileDiagnostic = "compile failed";
    const auto compileFailure = rws::CandidateResultAssembler::assemble(input);
    REQUIRE(compileFailure.lifecycle == rws::CandidateLifecycle::Failed);
    REQUIRE(compileFailure.feasibility == rws::Feasibility::NotEvaluated);

    input.compileSucceeded = true;
    input.evaluationSucceeded = false;
    input.evaluationDiagnostic = "evaluation failed";
    const auto evaluationFailure = rws::CandidateResultAssembler::assemble(input);
    REQUIRE(evaluationFailure.lifecycle == rws::CandidateLifecycle::Failed);
    REQUIRE(evaluationFailure.feasibility == rws::Feasibility::DataInsufficient);

    input.evaluationSucceeded = true;
    input.completion.canceled = true;
    const auto canceled = rws::CandidateResultAssembler::assemble(input);
    REQUIRE(canceled.lifecycle == rws::CandidateLifecycle::Canceled);
    REQUIRE(canceled.feasibility == rws::Feasibility::DataInsufficient);

    rws::CandidateAssemblyInput insufficient = input;
    insufficient.completion.canceled = false;
    insufficient.feasibility = rws::Feasibility::DataInsufficient;
    const auto insufficientResult = rws::CandidateResultAssembler::assemble(insufficient);
    REQUIRE(rws::CandidateResultAssembler::betterForRanking(result, insufficientResult));
    std::printf("PASSED\n");
}

static rws::CandidateResult makeEliteCandidate(const char* id,
                                               rws::Feasibility feasibility,
                                               rws::AnalysisEvidenceStage stage,
                                               double score,
                                               std::initializer_list<double> design)
{
    rws::CandidateResult result;
    result.candidateId = id;
    result.lifecycle = rws::CandidateLifecycle::Completed;
    result.feasibility = feasibility;
    result.evidenceStage = stage;
    rws::ObjectiveResult objective;
    objective.usable = true;
    objective.contribution = score;
    result.objectives.push_back(objective);
    result.representativeQ = design;
    return result;
}

static rws::CandidateResult makeLocalSearchResult(const std::vector<double>& design,
                                                  double score,
                                                  rws::Feasibility feasibility =
                                                      rws::Feasibility::Feasible,
                                                  rws::AnalysisEvidenceStage stage =
                                                      rws::AnalysisEvidenceStage::Verified)
{
    rws::CandidateResult result = makeEliteCandidate("local", feasibility, stage, score, {});
    result.representativeQ = design;
    return result;
}

static rws::CandidateResult makeFinalVerificationCandidate(
    const char* id,
    rws::Feasibility feasibility = rws::Feasibility::Feasible,
    rws::AnalysisEvidenceStage stage = rws::AnalysisEvidenceStage::Verified)
{
    rws::CandidateResult result = makeEliteCandidate(id, feasibility, stage, 2.0, {});
    result.lifecycle = rws::CandidateLifecycle::Completed;
    return result;
}

static void testOptimizationCheckpoint()
{
    std::printf("testOptimizationCheckpoint ... ");

    // 状态机只允许有意义的生命周期转移，非法转移返回稳定诊断码。
    rws::OptimizationRunStateMachine state;
    REQUIRE(state.state() == rws::OptimizationRunState::Idle);
    REQUIRE(state.start().ok);
    REQUIRE(state.pause().ok);
    REQUIRE(state.resume().ok);
    REQUIRE(state.requestCancel().ok);
    REQUIRE(state.requestCancel().ok); // cancel 必须幂等。
    REQUIRE(state.complete().ok);
    REQUIRE(state.state() == rws::OptimizationRunState::Completed);
    REQUIRE(!state.pause().ok);
    REQUIRE(state.pause().diagnostic == "RUN_PAUSE_NOT_ALLOWED");
    state.reset();
    REQUIRE(!state.resume().ok);
    REQUIRE(state.resume().diagnostic == "RUN_RESUME_NOT_ALLOWED");

    rws::OptimizationCheckpointFingerprints fingerprints;
    fingerprints.model = "model-fingerprint";
    fingerprints.environment = "environment-fingerprint";
    fingerprints.requirements = "requirements-fingerprint";
    fingerprints.designSpace = "design-space-fingerprint";

    const rws::CandidateResult completed = makeFinalVerificationCandidate("completed");
    rws::CandidateResult active = makeFinalVerificationCandidate("active");
    active.completion.requestedCount = 10;
    active.completion.completedCount = 3;
    active.lifecycle = rws::CandidateLifecycle::Evaluating;
    const auto checkpoint = rws::OptimizationCheckpoint::create(
        fingerprints, 42, 7, {7, 8, 9}, {completed}, {active});
    REQUIRE(checkpoint.valid());
    REQUIRE(checkpoint.randomSeed == 42);
    REQUIRE(checkpoint.nextCandidateIndex == 7);
    REQUIRE(checkpoint.pendingStableIndices.size() == 3);
    REQUIRE(checkpoint.completedResults.size() == 1);
    REQUIRE(checkpoint.partialResults.size() == 1);
    REQUIRE(checkpoint.partialResults.front().lifecycle == rws::CandidateLifecycle::Canceled);
    REQUIRE(checkpoint.partialResults.front().feasibility == rws::Feasibility::DataInsufficient);
    REQUIRE(checkpoint.partialResults.front().completion.canceled);
    REQUIRE(checkpoint.partialResults.front().completion.completedCount == 3);

    const auto restored = rws::restoreCheckpoint(checkpoint, fingerprints);
    REQUIRE(restored.ok);
    REQUIRE(restored.checkpoint.randomSeed == 42);
    REQUIRE(restored.checkpoint.partialResults.size() == 1);

    auto mismatched = fingerprints;
    mismatched.environment = "changed-environment";
    const auto rejected = rws::restoreCheckpoint(checkpoint, mismatched);
    REQUIRE(!rejected.ok);
    REQUIRE(rejected.diagnostic == "CHECKPOINT_FINGERPRINT_MISMATCH");

    auto invalid = fingerprints;
    invalid.designSpace.clear();
    const auto invalidRestore = rws::restoreCheckpoint(checkpoint, invalid);
    REQUIRE(!invalidRestore.ok);
    REQUIRE(invalidRestore.diagnostic == "CHECKPOINT_CURRENT_FINGERPRINTS_INVALID");

    std::printf("PASSED\n");
}

static rws::CompiledDesignSpace samplerFixture(bool reversed = false)
{
    rws::DesignVariableDefinition length;
    length.id = "length";
    length.semanticKind = rws::SemanticKind::LinkLength;
    length.unit = rws::DesignVariableUnit::Metres;
    length.minimum = 0.1;
    length.maximum = 0.5;
    length.step = 0.1;
    length.nominalValue = 0.3;
    length.currentValue = 0.3;

    rws::DesignVariableDefinition count;
    count.id = "count";
    count.semanticKind = rws::SemanticKind::LinkWidth;
    count.domain = rws::VariableDomain::Integer;
    count.unit = rws::DesignVariableUnit::Unitless;
    count.minimum = 1.0;
    count.maximum = 3.0;
    count.step = 1.0;
    count.nominalValue = 2.0;
    count.currentValue = 2.0;

    rws::DesignVariableDefinition material;
    material.id = "material";
    material.semanticKind = rws::SemanticKind::ParameterizedMaterial;
    material.domain = rws::VariableDomain::Discrete;
    material.discreteOptions = {{"steel", "Steel", "steel.json"},
                                {"aluminum", "Aluminum", "aluminum.json"}};

    rws::CompiledDesignSpace space;
    space.schemaVersion = 1;
    space.fingerprint = "sampler-space";
    space.independentVariables = reversed
        ? std::vector<rws::DesignVariableDefinition>{material, length, count}
        : std::vector<rws::DesignVariableDefinition>{length, count, material};
    std::sort(space.independentVariables.begin(), space.independentVariables.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    for (std::size_t index = 0; index < space.independentVariables.size(); ++index)
        space.canonicalVectorSchema.push_back(
            {space.independentVariables[index].id, index, space.independentVariables[index].unit});
    return space;
}

static void testEvaluationPlanCompiler()
{
    std::printf("testEvaluationPlanCompiler ... ");
    rws::RequirementExecutionSet requirements;
    requirements.schemaVersion = 4;
    requirements.provenance.requirementFingerprint = "req-1";
    requirements.provenance.robotModelFingerprint = "model-1";
    requirements.provenance.environmentFingerprint = "scene-1";

    rws::RequirementExecutionTask must;
    must.id = "must-target";
    must.level = rws::RequirementExecutionLevel::Must;
    must.collisionFreeRequired = false;
    requirements.tasks.push_back(must);
    rws::RequirementExecutionTask should;
    should.id = "should-target";
    should.level = rws::RequirementExecutionLevel::Should;
    should.collisionFreeRequired = false;
    requirements.tasks.push_back(should);

    rws::RequirementExecutionRegion region;
    region.id = "region";
    region.level = rws::RequirementExecutionLevel::Must;
    region.collisionFreeRequired = false;
    requirements.workspaceRegions.push_back(region);

    rws::EvaluationPlanCompilerOptions options;
    options.modelFingerprint = "model-1";
    options.environmentFingerprint = "scene-1";
    options.toolFingerprint = "tool-1";
    options.capabilities.insert("target");
    const rws::EvaluationPlan first =
        rws::EvaluationPlanCompiler::compile(requirements, options);
    const rws::EvaluationPlan second =
        rws::EvaluationPlanCompiler::compile(requirements, options);
    REQUIRE(first.valid());
    REQUIRE(first.tasks.size() == 2);
    REQUIRE(first.tasks[0].hardConstraint);
    REQUIRE(!first.tasks[1].hardConstraint);
    REQUIRE(first.regions.size() == 1);
    REQUIRE(first.fingerprint == second.fingerprint);

    // The frozen-requirement producer and the JSON contract both define the
    // execution payload as schema v1.  A valid v1 contract must therefore
    // enter the optimizer plan instead of being rejected before baseline
    // evaluation starts.
    requirements.schemaVersion = 1;
    const rws::EvaluationPlan v1 =
        rws::EvaluationPlanCompiler::compile(requirements, options);
    REQUIRE(v1.valid());
    REQUIRE(v1.diagnostics.empty());
    requirements.schemaVersion = 4;

    options.capabilities.clear();
    must.collisionFreeRequired = true;
    requirements.tasks[0] = must;
    const rws::EvaluationPlan missingCapability =
        rws::EvaluationPlanCompiler::compile(requirements, options);
    REQUIRE(!missingCapability.valid());
    REQUIRE(missingCapability.hasBlockingDiagnostics());

    requirements.schemaVersion = 3;
    const rws::EvaluationPlan v3 =
        rws::EvaluationPlanCompiler::compile(requirements, options);
    REQUIRE(!v3.valid());
    bool sawVerifiedRejection = false;
    for (const auto& item : v3.diagnostics)
        sawVerifiedRejection = sawVerifiedRejection ||
                               item.code == "REQUIREMENT_SCHEMA_VERIFIED_UNSUPPORTED";
    REQUIRE(sawVerifiedRejection);
    std::printf("PASSED\n");
}

// 子套件 ABI:验证结构优化域的几个历史公开入口仍按既有函数指针类型可链接——
// resolveExternalAssetPaths / loadProject / createProblem / check。
// 防止重构(改签名或改名)后旧插件或既有调用方二进制链接失败。
static void testHistoricalStructureOptimizerAbiRemainsLinkable()
{
    using ResolveExternalAssetPaths = void (*)(rws::RobotModelSpec&);
    using LoadProject = bool (*)(const QString&, rws::StructureOptimizationProblem&, int*,
                                 QString*);
    using CreateProblem = bool (*)(const QString&, const rw::models::WorkCell&,
                                   const rw::kinematics::State&,
                                   rws::StructureOptimizationProblem&,
                                   rws::FrozenRequirementValidationResult*, std::string*);
    using CheckStaleness = rws::RobotModelStalenessResult (*)(
        const rws::RobotDesignContext&, const QString&);

    const ResolveExternalAssetPaths resolveExternalAssetPaths =
        static_cast<ResolveExternalAssetPaths>(
            &rws::CandidateModelFactory::resolveExternalAssetPaths);
    const LoadProject loadProject =
        static_cast<LoadProject>(&rws::StructureOptimizationProjectAdapter::loadProject);
    const CreateProblem createProblem = static_cast<CreateProblem>(
        &rws::FrozenRequirementProjectImportService::createProblem);
    const CheckStaleness checkStaleness = &rws::RobotModelStalenessChecker::check;

    REQUIRE(resolveExternalAssetPaths != nullptr);
    REQUIRE(loadProject != nullptr);
    REQUIRE(createProblem != nullptr);
    REQUIRE(checkStaleness != nullptr);
}

// Phase 0/S03: the new result contract keeps lifecycle, feasibility, evidence,
// quality, and completion orthogonal.  This is deliberately model-only so it
// can protect core semantics without a Qt Widgets platform dependency.
static void testOptimizationResultContract()
{
    rws::CandidateLifecycle lifecycle = rws::CandidateLifecycle::Canceled;
    rws::EvaluationCompletion completion;
    completion.requestedCount = 10;
    completion.completedCount = 4;
    completion.canceled = true;
    completion.partialReason = "CanceledByUser";

    REQUIRE(std::string(rws::toString(lifecycle)) == "Canceled");
    REQUIRE(std::string(rws::toString(rws::Feasibility::DataInsufficient)) ==
            "DataInsufficient");
    REQUIRE(std::string(rws::toString(rws::AnalysisEvidenceStage::Quick)) == "Quick");
    REQUIRE(std::string(rws::toString(rws::Quality::Unknown)) == "Unknown");
    REQUIRE(!completion.complete());
    REQUIRE(completion.canceled);
    REQUIRE(completion.partial());

    rws::CandidateLifecycle parsedLifecycle = rws::CandidateLifecycle::Pending;
    std::string error;
    REQUIRE(rws::candidateLifecycleFromString("Canceled", parsedLifecycle, &error));
    REQUIRE(parsedLifecycle == rws::CandidateLifecycle::Canceled);
    REQUIRE(error.empty());
    REQUIRE(!rws::candidateLifecycleFromString("NotAState", parsedLifecycle, &error));
    REQUIRE(!error.empty());

    rws::StructureCandidateStatus legacy = rws::StructureCandidateStatus::Canceled;
    const rws::CandidateStateProjection projected =
        rws::projectLegacyCandidateStatus(legacy);
    REQUIRE(projected.lifecycle == rws::CandidateLifecycle::Canceled);
    REQUIRE(projected.feasibility == rws::Feasibility::DataInsufficient);
    REQUIRE(projected.evidenceStage == rws::AnalysisEvidenceStage::Quick);
    REQUIRE(projected.quality == rws::Quality::Unknown);
}

// Phase 0/S04: freeze the SE(3) convention before it is used by any importer
// or candidate compiler.  These tests intentionally use no WorkCell objects.
static void testKinematicConventions()
{
    using namespace rw::math;

    REQUIRE(std::fabs(rws::KinematicConventions::modelCoordinate(0.3, -0.1) - 0.2) <
            1e-12);

    const Transform3D<> revolute = rws::KinematicConventions::jointMotion(
        rws::CanonicalJointMotion::Revolute, Vector3D<>::z(), rw::math::Pi / 2.0);
    REQUIRE(std::fabs(revolute.R()(0, 0)) < 1e-12);
    REQUIRE(std::fabs(revolute.R()(1, 0) - 1.0) < 1e-12);
    REQUIRE(rws::KinematicConventions::isProperRotation(revolute.R()));

    const Transform3D<> prismatic = rws::KinematicConventions::jointMotion(
        rws::CanonicalJointMotion::Prismatic, Vector3D<>::x(), 0.25);
    REQUIRE(std::fabs(prismatic.P()(0) - 0.25) < 1e-12);
    REQUIRE(std::fabs(prismatic.P()(1)) < 1e-12);
    REQUIRE(std::fabs(prismatic.P()(2)) < 1e-12);

    const Transform3D<> fixed = rws::KinematicConventions::jointMotion(
        rws::CanonicalJointMotion::Fixed, Vector3D<>::z(), 42.0);
    REQUIRE(std::fabs(fixed.P().norm2()) < 1e-12);
    REQUIRE(rws::KinematicConventions::isProperRotation(fixed.R()));

    const Transform3D<> parentToJoint(Vector3D<>(1.0, 0.0, 0.0));
    const Transform3D<> motionToChild(Vector3D<>(0.0, 2.0, 0.0));
    const Transform3D<> composed = rws::KinematicConventions::composeJointTransform(
        parentToJoint, rws::CanonicalJointMotion::Prismatic, Vector3D<>::z(), 0.5, 0.25,
        motionToChild);
    REQUIRE(std::fabs(composed.P()(0) - 1.0) < 1e-12);
    REQUIRE(std::fabs(composed.P()(1) - 2.0) < 1e-12);
    REQUIRE(std::fabs(composed.P()(2) - 0.75) < 1e-12);

    const rws::TangentBasis basis =
        rws::KinematicConventions::stableTangentBasis(Vector3D<>(0.0, 0.0, -1.0));
    REQUIRE(basis.valid);
    REQUIRE(std::fabs(dot(basis.first, Vector3D<>(0.0, 0.0, -1.0))) < 1e-12);
    REQUIRE(std::fabs(dot(basis.second, Vector3D<>(0.0, 0.0, -1.0))) < 1e-12);
    REQUIRE(std::fabs(dot(basis.first, basis.second)) < 1e-12);

    const Vector3D<> reference = Vector3D<>::z();
    const Vector3D<> tilted = rws::KinematicConventions::tiltedAxis(reference, 0.3, 0.4);
    REQUIRE(std::fabs(tilted.norm2() - 1.0) < 1e-12);
    REQUIRE(std::fabs(rws::KinematicConventions::angleBetween(reference, tilted) - 0.5) <
            1e-12);
    const Vector3D<> unchanged = rws::KinematicConventions::tiltedAxis(reference, 0.0, 0.0);
    REQUIRE(std::fabs((unchanged - reference).norm2()) < 1e-12);
}

// Phase 1/S10: the canonical model owns explicit Frame/Joint/DOF topology.
// This fixture intentionally contains a Tool frame and a Fixed joint so the
// validator cannot accidentally equate frame count with the Q dimension.
static rws::CanonicalKinematicModel validCanonicalModelFixture()
{
    rws::CanonicalKinematicModel model;
    model.schemaVersion = 1;
    model.modelId = "canonical-fixture";
    model.rootFrameId = "base";
    model.baseFrameId = "base";
    model.activeDeviceChainId = "arm";

    model.frames = {{"base", "Base", rws::CanonicalFrameType::Base},
                    {"link", "Link", rws::CanonicalFrameType::Link},
                    {"guide", "Fixed guide", rws::CanonicalFrameType::Fixed},
                    {"flange", "Flange", rws::CanonicalFrameType::Flange},
                    {"tcp", "Tool", rws::CanonicalFrameType::Tool}};

    rws::JointEdge joint;
    joint.id = "joint-1";
    joint.name = "Joint 1";
    joint.type = rws::CanonicalJointType::Revolute;
    joint.parentFrameId = "base";
    joint.childFrameId = "link";
    joint.motionAxisInJoint = rw::math::Vector3D<>::z();
    joint.dofId = "dof-0";
    model.joints.push_back(joint);

    rws::JointEdge fixed;
    fixed.id = "fixed-flange";
    fixed.name = "Flange mount";
    fixed.type = rws::CanonicalJointType::Fixed;
    fixed.parentFrameId = "link";
    fixed.childFrameId = "guide";
    fixed.motionAxisInJoint = rw::math::Vector3D<>::z();
    model.joints.push_back(fixed);

    rws::JointEdge prismatic;
    prismatic.id = "joint-2";
    prismatic.name = "Joint 2";
    prismatic.type = rws::CanonicalJointType::Prismatic;
    prismatic.parentFrameId = "guide";
    prismatic.childFrameId = "flange";
    prismatic.motionAxisInJoint = rw::math::Vector3D<>::x();
    prismatic.dofId = "dof-1";
    model.joints.push_back(prismatic);

    model.dofs = {{"dof-0", "joint-1", 0, rws::CanonicalJointType::Revolute,
                   rws::CanonicalCoordinateUnit::Radians},
                  {"dof-1", "joint-2", 1, rws::CanonicalJointType::Prismatic,
                   rws::CanonicalCoordinateUnit::Metres}};
    model.deviceChains = {{"arm", "base", "flange",
                           {"joint-1", "fixed-flange", "joint-2"},
                           {"dof-0", "dof-1"}}};
    model.toolBindings = {{"tool", "flange", "tcp"}};
    return model;
}

static bool hasCanonicalDiagnostic(const rws::CanonicalKinematicModelValidationResult& result,
                                   const std::string& code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [&code](const rws::StructureOptimizationDiagnostic& diagnostic) {
                           return diagnostic.code == code;
                       });
}

static void testCanonicalKinematicModelValidation()
{
    const rws::CanonicalKinematicModel valid = validCanonicalModelFixture();
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(valid).valid);

    rws::CanonicalKinematicModel duplicateFrame = valid;
    duplicateFrame.frames.push_back(duplicateFrame.frames.front());
    const auto duplicateFrameResult =
        rws::CanonicalKinematicModelValidator::validate(duplicateFrame);
    REQUIRE(!duplicateFrameResult.valid);
    REQUIRE(hasCanonicalDiagnostic(duplicateFrameResult, "KINEMATIC_FRAME_ID_DUPLICATE"));

    rws::CanonicalKinematicModel fixedWithDof = valid;
    fixedWithDof.joints[1].dofId = "dof-0";
    const auto fixedWithDofResult =
        rws::CanonicalKinematicModelValidator::validate(fixedWithDof);
    REQUIRE(!fixedWithDofResult.valid);
    REQUIRE(hasCanonicalDiagnostic(fixedWithDofResult, "KINEMATIC_FIXED_JOINT_HAS_DOF"));

    rws::CanonicalKinematicModel movableWithoutDof = valid;
    movableWithoutDof.joints[0].dofId.clear();
    const auto movableWithoutDofResult =
        rws::CanonicalKinematicModelValidator::validate(movableWithoutDof);
    REQUIRE(!movableWithoutDofResult.valid);
    REQUIRE(hasCanonicalDiagnostic(movableWithoutDofResult,
                                   "KINEMATIC_MOVABLE_JOINT_MISSING_DOF"));

    rws::CanonicalKinematicModel duplicateMovableDof = valid;
    duplicateMovableDof.joints[2].dofId = "dof-0";
    const auto duplicateMovableDofResult =
        rws::CanonicalKinematicModelValidator::validate(duplicateMovableDof);
    REQUIRE(!duplicateMovableDofResult.valid);
    REQUIRE(hasCanonicalDiagnostic(duplicateMovableDofResult,
                                   "KINEMATIC_MOVABLE_JOINT_DOF_DUPLICATE"));

    rws::CanonicalKinematicModel nonContiguousQ = valid;
    nonContiguousQ.dofs[0].qIndex = 1;
    const auto nonContiguousQResult = rws::CanonicalKinematicModelValidator::validate(nonContiguousQ);
    REQUIRE(!nonContiguousQResult.valid);
    REQUIRE(hasCanonicalDiagnostic(nonContiguousQResult,
                                   "KINEMATIC_DOF_QINDEX_NOT_CONTIGUOUS"));

    rws::CanonicalKinematicModel wrongUnit = valid;
    wrongUnit.dofs[0].unit = rws::CanonicalCoordinateUnit::Metres;
    const auto wrongUnitResult = rws::CanonicalKinematicModelValidator::validate(wrongUnit);
    REQUIRE(!wrongUnitResult.valid);
    REQUIRE(hasCanonicalDiagnostic(wrongUnitResult, "KINEMATIC_DOF_UNIT_MISMATCH"));

    rws::CanonicalKinematicModel disconnectedChain = valid;
    disconnectedChain.deviceChains[0].tipFrameId = "tcp";
    const auto disconnectedChainResult =
        rws::CanonicalKinematicModelValidator::validate(disconnectedChain);
    REQUIRE(!disconnectedChainResult.valid);
    REQUIRE(hasCanonicalDiagnostic(disconnectedChainResult, "KINEMATIC_CHAIN_DISCONNECTED"));

    rws::CanonicalKinematicModel invalidTool = valid;
    invalidTool.toolBindings[0].flangeFrameId = "link";
    const auto invalidToolResult = rws::CanonicalKinematicModelValidator::validate(invalidTool);
    REQUIRE(!invalidToolResult.valid);
    REQUIRE(hasCanonicalDiagnostic(invalidToolResult, "KINEMATIC_TOOL_BINDING_INVALID"));

    rws::CanonicalKinematicModel duplicateToolBinding = valid;
    duplicateToolBinding.toolBindings.push_back(duplicateToolBinding.toolBindings.front());
    const auto duplicateToolBindingResult =
        rws::CanonicalKinematicModelValidator::validate(duplicateToolBinding);
    REQUIRE(!duplicateToolBindingResult.valid);
    REQUIRE(hasCanonicalDiagnostic(duplicateToolBindingResult,
                                   "KINEMATIC_TOOL_BINDING_ID_DUPLICATE"));

    rws::CanonicalKinematicModel invalidPhysicalLimits = valid;
    invalidPhysicalLimits.joints[0].physicalLimits = {
        true, std::numeric_limits< double >::quiet_NaN(), 1.0,
        rws::CanonicalCoordinateUnit::Radians, rws::JointCoordinateConvention::QInput};
    const auto invalidPhysicalLimitsResult =
        rws::CanonicalKinematicModelValidator::validate(invalidPhysicalLimits);
    REQUIRE(!invalidPhysicalLimitsResult.valid);
    REQUIRE(hasCanonicalDiagnostic(invalidPhysicalLimitsResult,
                                   "KINEMATIC_PHYSICAL_LIMITS_NONFINITE"));

    rws::CanonicalKinematicModel wrongOperationalUnit = valid;
    wrongOperationalUnit.joints[2].operationalLimits = {
        true, -0.2, 0.2, rws::CanonicalCoordinateUnit::Radians,
        rws::JointCoordinateConvention::QInput};
    const auto wrongOperationalUnitResult =
        rws::CanonicalKinematicModelValidator::validate(wrongOperationalUnit);
    REQUIRE(!wrongOperationalUnitResult.valid);
    REQUIRE(hasCanonicalDiagnostic(wrongOperationalUnitResult,
                                   "KINEMATIC_OPERATIONAL_LIMITS_UNIT_MISMATCH"));

    rws::CanonicalKinematicModel fixedLimitModel = valid;
    fixedLimitModel.joints[1].physicalLimits = {
        true, -1.0, 1.0, rws::CanonicalCoordinateUnit::Radians,
        rws::JointCoordinateConvention::QInput};
    const auto fixedLimitModelResult =
        rws::CanonicalKinematicModelValidator::validate(fixedLimitModel);
    REQUIRE(!fixedLimitModelResult.valid);
    REQUIRE(hasCanonicalDiagnostic(fixedLimitModelResult,
                                   "KINEMATIC_FIXED_JOINT_LIMITS_FORBIDDEN"));
}

static bool sameTransform(const rw::math::Transform3D<>& first,
                          const rw::math::Transform3D<>& second,
                          double tolerance = 1e-12)
{
    if ((first.P() - second.P()).norm2() >= tolerance)
        return false;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            if (std::fabs(first.R()(row, column) - second.R()(row, column)) >= tolerance)
                return false;
        }
    }
    return true;
}

static bool hasCanonicalDiagnostic(const rws::CanonicalForwardKinematicsResult& result,
                                   const std::string& code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [&code](const rws::StructureOptimizationDiagnostic& diagnostic) {
                           return diagnostic.code == code;
                       });
}

// Phase 1/S11: canonical FK evaluates the only frozen joint equation and
// never mutates the source model or supplied Q vector.
static void testCanonicalForwardKinematics()
{
    using rw::math::Transform3D;
    using rw::math::Vector3D;

    rws::CanonicalKinematicModel model = validCanonicalModelFixture();
    model.joints[0].parentToJointZero = Transform3D<>(Vector3D<>(1.0, 0.0, 0.0));
    model.joints[0].jointMotionToChild = Transform3D<>(Vector3D<>(0.0, 1.0, 0.0));
    model.joints[0].zeroPositionOffset = 0.1;
    model.joints[1].parentToJointZero = Transform3D<>(Vector3D<>(0.0, 0.0, 0.5));
    model.joints[1].jointMotionToChild = Transform3D<>(Vector3D<>(0.0, 0.0, 0.2));
    model.joints[2].parentToJointZero = Transform3D<>(Vector3D<>(0.0, 2.0, 0.0));
    model.joints[2].jointMotionToChild = Transform3D<>(Vector3D<>(0.0, 0.0, 0.3));
    model.joints[2].zeroPositionOffset = 0.05;
    model.toolBindings[0].flangeToTcp = Transform3D<>(Vector3D<>(0.0, 0.0, 0.4));

    const std::vector< double > zeroQ = {0.0, 0.0};
    const rws::CanonicalForwardKinematicsResult zeroResult =
        rws::CanonicalForwardKinematics::evaluate(model, zeroQ);
    REQUIRE(zeroResult.valid);

    const std::vector< double > q = {0.2, 0.4};
    const rws::CanonicalForwardKinematicsResult result =
        rws::CanonicalForwardKinematics::evaluate(model, q);
    REQUIRE(result.valid);
    REQUIRE(q[0] == 0.2);
    REQUIRE(q[1] == 0.4);

    const Transform3D<> expectedLink =
        model.joints[0].parentToJointZero *
        rws::KinematicConventions::jointMotion(rws::CanonicalJointMotion::Revolute,
                                                Vector3D<>::z(), 0.3) *
        model.joints[0].jointMotionToChild;
    const Transform3D<> expectedGuide =
        expectedLink * model.joints[1].parentToJointZero * model.joints[1].jointMotionToChild;
    const Transform3D<> expectedFlange =
        expectedGuide * model.joints[2].parentToJointZero *
        rws::KinematicConventions::jointMotion(rws::CanonicalJointMotion::Prismatic,
                                                Vector3D<>::x(), 0.45) *
        model.joints[2].jointMotionToChild;
    const Transform3D<> expectedTcp = expectedFlange * model.toolBindings[0].flangeToTcp;

    Transform3D<> actual;
    REQUIRE(rws::CanonicalForwardKinematics::frameTransform(result, "link", actual));
    REQUIRE(sameTransform(actual, expectedLink));
    REQUIRE(rws::CanonicalForwardKinematics::frameTransform(result, "guide", actual));
    REQUIRE(sameTransform(actual, expectedGuide));
    REQUIRE(rws::CanonicalForwardKinematics::frameTransform(result, "flange", actual));
    REQUIRE(sameTransform(actual, expectedFlange));
    REQUIRE(rws::CanonicalForwardKinematics::frameTransform(result, "tcp", actual));
    REQUIRE(sameTransform(actual, expectedTcp));

    rws::StructureOptimizationDiagnostic missingFrame;
    REQUIRE(!rws::CanonicalForwardKinematics::frameTransform(result, "not-a-frame", actual,
                                                               &missingFrame));
    REQUIRE(missingFrame.code == "KINEMATIC_FK_FRAME_NOT_FOUND");

    const rws::CanonicalForwardKinematicsResult invalidQ =
        rws::CanonicalForwardKinematics::evaluate(model, {0.2});
    REQUIRE(!invalidQ.valid);
    REQUIRE(hasCanonicalDiagnostic(invalidQ, "KINEMATIC_FK_Q_DIMENSION_MISMATCH"));
}

// Phase 1/S12: the formal WorkCell/Device/TCP boundary is the source of
// canonical kinematics.  Fixed frames stay out of Q, while a prismatic joint
// retains its metre coordinate and every imported node remains traceable to
// its RobWork object.
static void testKinematicModelImporter()
{
    const rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ImporterBase",
                                       rw::math::Transform3D<>(rw::math::Vector3D<>(0.3, 0.0, 0.0),
                                                                rw::math::RPY<>(0.1, -0.2, 0.3).toRotation3D())));
    const rw::models::RevoluteJoint::Ptr revolute = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("ImporterRevolute",
                                      rw::math::Transform3D<>(rw::math::Vector3D<>(0.0, 0.1, 0.0),
                                                               rw::math::RPY<>(0.0, 0.2, 0.1).toRotation3D())));
    const rw::kinematics::FixedFrame::Ptr fixed = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ImporterFixed",
                                       rw::math::Transform3D<>(rw::math::Vector3D<>(0.1, 0.0, 0.0),
                                                                rw::math::RPY<>(0.0, 0.4, 0.0).toRotation3D())));
    const rw::models::PrismaticJoint::Ptr prismatic = rw::core::ownedPtr(
        new rw::models::PrismaticJoint("ImporterPrismatic",
                                       rw::math::Transform3D<>(rw::math::Vector3D<>(0.0, 0.2, 0.0),
                                                                rw::math::RPY<>(0.2, 0.0, 0.1).toRotation3D())));
    const rw::kinematics::FixedFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ImporterTcp",
                                       rw::math::Transform3D<>(rw::math::Vector3D<>(0.0, 0.0, 0.2),
                                                                rw::math::RPY<>(0.1, 0.0, -0.2).toRotation3D())));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(revolute, base);
    structure->addFrame(fixed, revolute);
    structure->addFrame(prismatic, fixed);
    structure->addFrame(tcp, prismatic);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "ImporterWorkCell"));
    const rw::models::SerialDevice::Ptr device = rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tcp, "ImporterDevice", structure->getDefaultState()));
    workcell->addDevice(device);

    rws::KinematicImportRequest request;
    request.workcell = workcell.get();
    request.device = device.get();
    request.tcpFrame = tcp.get();
    rws::RobotModelSpec sourceSnapshot;
    sourceSnapshot.robotName = "CapturedRobotModelSnapshot";
    request.sourceSnapshot = &sourceSnapshot;
    request.sourceFingerprint = "source-fingerprint";
    request.environmentFingerprint = "environment-fingerprint";
    const rws::KinematicImportResult result = rws::KinematicModelImporter::import(request);

    REQUIRE(result.ok);
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.model.dofs.size() == 2);
    REQUIRE(result.model.dofs.at(0).unit == rws::CanonicalCoordinateUnit::Radians);
    REQUIRE(result.model.dofs.at(1).unit == rws::CanonicalCoordinateUnit::Metres);
    REQUIRE(std::all_of(result.model.joints.begin(), result.model.joints.end(),
                        [](const rws::JointEdge& joint) {
                            return joint.type == rws::CanonicalJointType::Fixed ||
                                   (joint.physicalLimits.coordinateConvention ==
                                        rws::JointCoordinateConvention::QInput &&
                                    joint.operationalLimits.coordinateConvention ==
                                        rws::JointCoordinateConvention::QInput);
                        }));
    REQUIRE(result.model.frames.size() == 6);
    REQUIRE(result.model.deviceChains.size() == 1);
    REQUIRE(result.model.deviceChains.at(0).orderedJointIds.size() == 5);
    REQUIRE(result.model.deviceChains.at(0).orderedDofIds.size() == 2);
    REQUIRE(result.model.joints.at(2).type == rws::CanonicalJointType::Fixed);
    REQUIRE(result.model.joints.at(2).dofId.empty());
    REQUIRE(result.model.sourceFingerprint == "source-fingerprint");
    REQUIRE(result.model.environmentFingerprint == "environment-fingerprint");
    REQUIRE(result.provenance.workcellName == "ImporterWorkCell");
    REQUIRE(result.provenance.deviceId == "ImporterDevice");
    REQUIRE(result.provenance.tcpFrameId == "ImporterTcp");
    REQUIRE(result.hasSourceSnapshot);
    REQUIRE(result.sourceSnapshot.robotName == "CapturedRobotModelSnapshot");
    REQUIRE(result.model.modelId == "ImporterDevice");
    rws::StructureOptimizationProblem shadowedProblem;
    shadowedProblem.context.robotName = "legacy-evaluator-input";
    std::string shadowError;
    REQUIRE(rws::CanonicalModelShadowService::attach(request, shadowedProblem, &shadowError));
    REQUIRE(shadowedProblem.canonicalModelShadow.status ==
            rws::CanonicalModelShadowStatus::Current);
    REQUIRE(shadowedProblem.canonicalModelShadow.hasSnapshot());
    REQUIRE(shadowedProblem.canonicalModelShadow.snapshot->modelFingerprint ==
            rws::KinematicFingerprint::forModel(result.model).value);
    REQUIRE(shadowedProblem.context.robotName == "legacy-evaluator-input");
    rws::StructureOptimizationProblem createdProject;
    const rws::RobotModelSpec projectSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        projectSpec, request, createdProject, &shadowError));
    REQUIRE(createdProject.canonicalModelShadow.status ==
            rws::CanonicalModelShadowStatus::Current);
    REQUIRE(createdProject.canonicalModelShadow.hasSnapshot());
    REQUIRE(createdProject.canonicalModelShadow.snapshot->modelFingerprint ==
            rws::KinematicFingerprint::forModel(result.model).value);
    REQUIRE(createdProject.context.modelSpec.robotName == projectSpec.robotName);
    QTemporaryDir shadowProjectDirectory;
    REQUIRE(shadowProjectDirectory.isValid());
    const QString shadowProjectPath = shadowProjectDirectory.filePath("canonical-shadow.sop.json");
    QString shadowProjectError;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        shadowProjectPath, createdProject, -1, &shadowProjectError));
    rws::StructureOptimizationProblem loadedCurrentProject;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        shadowProjectPath, request, loadedCurrentProject, nullptr, &shadowProjectError));
    REQUIRE(loadedCurrentProject.canonicalModelShadow.status ==
            rws::CanonicalModelShadowStatus::Current);
    rws::KinematicImportRequest changedSourceRequest = request;
    changedSourceRequest.sourceFingerprint = "changed-source-fingerprint";
    rws::StructureOptimizationProblem loadedStaleProject;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        shadowProjectPath, changedSourceRequest, loadedStaleProject, nullptr,
        &shadowProjectError));
    REQUIRE(loadedStaleProject.canonicalModelShadow.status ==
            rws::CanonicalModelShadowStatus::Stale);
    rws::StructureOptimizationProblem legacyProject;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        projectSpec, legacyProject, &shadowError));
    const QString legacyProjectPath = shadowProjectDirectory.filePath("legacy.sop.json");
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        legacyProjectPath, legacyProject, -1, &shadowProjectError));
    rws::StructureOptimizationProblem loadedLegacyProject;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        legacyProjectPath, request, loadedLegacyProject, nullptr, &shadowProjectError));
    REQUIRE(loadedLegacyProject.canonicalModelShadow.status ==
            rws::CanonicalModelShadowStatus::CanonicalModelMissing);
    REQUIRE(!loadedLegacyProject.canonicalModelShadow.hasSnapshot());

    rws::StructureOptimizationProblem legacyScoreProblem;
    rws::StructureCandidateResult legacyCandidate;
    legacyCandidate.index = 7;
    legacyCandidate.raw.modelValid = true;
    legacyCandidate.raw.requiredTaskCount = 5;
    legacyCandidate.raw.requiredReachableCount = 4;
    legacyCandidate.raw.weightedReachability = 0.8;
    legacyCandidate.raw.manipulabilityP10 = 0.5;
    legacyCandidate.raw.jointMarginP10 = 0.4;
    legacyCandidate.raw.totalKinematicLength = 1.2;
    legacyCandidate.raw.collisionFreeRate = 0.9;
    legacyCandidate.raw.engineeringPreference = 0.7;
    rws::StructureCandidateResult shadowCandidate = legacyCandidate;
    rws::StructureOptimizationProblem shadowScoreProblem = legacyScoreProblem;
    shadowScoreProblem.canonicalModelShadow = createdProject.canonicalModelShadow;
    rws::StructureObjectiveScorer scorer;
    scorer.score(legacyScoreProblem, legacyCandidate);
    scorer.score(shadowScoreProblem, shadowCandidate);
    REQUIRE(shadowCandidate.totalScore == legacyCandidate.totalScore);
    REQUIRE(shadowCandidate.feasible == legacyCandidate.feasible);
    REQUIRE(shadowCandidate.status == legacyCandidate.status);
    REQUIRE(shadowCandidate.violatedConstraints == legacyCandidate.violatedConstraints);
    const rws::CanonicalForwardKinematicsResult importedFk =
        rws::CanonicalForwardKinematics::evaluate(result.model, {0.0, 0.0});
    REQUIRE(importedFk.valid);
    rw::math::Transform3D<> importedTcp;
    REQUIRE(rws::CanonicalForwardKinematics::frameTransform(importedFk, "ImporterTcp", importedTcp));
    const rw::math::Transform3D<> workcellTcp = rw::kinematics::Kinematics::frameTframe(
        workcell->getWorldFrame(), tcp.get(), workcell->getDefaultState());
    REQUIRE(sameTransform(importedTcp, workcellTcp));

    rw::kinematics::State nonzeroState = workcell->getDefaultState();
    device->setQ(rw::math::Q(2, 0.25, 0.15), nonzeroState);
    const rws::CanonicalForwardKinematicsResult nonzeroImportedFk =
        rws::CanonicalForwardKinematics::evaluate(result.model, {0.25, 0.15});
    REQUIRE(nonzeroImportedFk.valid);
    const std::vector< const rw::kinematics::Frame* > keyFrames = {
        revolute.get(), fixed.get(), prismatic.get(), tcp.get()};
    for (const rw::kinematics::Frame* keyFrame : keyFrames) {
        rw::math::Transform3D<> importedTransform;
        REQUIRE(rws::CanonicalForwardKinematics::frameTransform(
            nonzeroImportedFk, keyFrame->getName(), importedTransform));
        const rw::math::Transform3D<> workcellTransform = rw::kinematics::Kinematics::frameTframe(
            workcell->getWorldFrame(), keyFrame, nonzeroState);
        REQUIRE(sameTransform(importedTransform, workcellTransform));
    }

    const rw::kinematics::StateStructure::Ptr sixAxisStructure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr sixAxisBase = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("SixAxisBase", rw::math::Transform3D<>()));
    sixAxisStructure->addFrame(sixAxisBase, sixAxisStructure->getRoot());
    rw::core::Ptr< rw::kinematics::Frame > sixAxisParent = sixAxisBase;
    std::vector< rw::models::RevoluteJoint::Ptr > sixAxisJoints;
    for (int index = 0; index < 6; ++index) {
        const rw::models::RevoluteJoint::Ptr joint = rw::core::ownedPtr(
            new rw::models::RevoluteJoint("SixAxisJoint" + std::to_string(index + 1),
                                          rw::math::Transform3D<>(
                                              rw::math::Vector3D<>(0.05 * (index + 1),
                                                                   0.01 * index, 0.1))));
        sixAxisStructure->addFrame(joint, sixAxisParent);
        sixAxisParent = joint;
        sixAxisJoints.push_back(joint);
    }
    const rw::kinematics::FixedFrame::Ptr sixAxisTcp = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("SixAxisTcp",
                                       rw::math::Transform3D<>(rw::math::Vector3D<>(0.0, 0.0, 0.15))));
    sixAxisStructure->addFrame(sixAxisTcp, sixAxisParent);
    const rw::models::WorkCell::Ptr sixAxisWorkcell = rw::core::ownedPtr(
        new rw::models::WorkCell(sixAxisStructure, "SixAxisWorkCell"));
    const rw::models::SerialDevice::Ptr sixAxisDevice = rw::core::ownedPtr(
        new rw::models::SerialDevice(sixAxisBase, sixAxisTcp, "SixAxisDevice",
                                     sixAxisStructure->getDefaultState()));
    sixAxisWorkcell->addDevice(sixAxisDevice);
    rws::KinematicImportRequest sixAxisRequest;
    sixAxisRequest.workcell = sixAxisWorkcell.get();
    sixAxisRequest.device = sixAxisDevice.get();
    sixAxisRequest.tcpFrame = sixAxisTcp.get();
    const rws::KinematicImportResult sixAxisResult =
        rws::KinematicModelImporter::import(sixAxisRequest);
    REQUIRE(sixAxisResult.ok);
    REQUIRE(sixAxisResult.model.dofs.size() == 6);
    const std::vector< std::vector< double > > sixAxisSamples = {
        {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
        {0.1, -0.2, 0.3, -0.4, 0.5, -0.6},
        {-0.35, 0.25, -0.15, 0.45, -0.55, 0.65}};
    for (const std::vector< double >& sample : sixAxisSamples) {
        rw::kinematics::State sixAxisState = sixAxisWorkcell->getDefaultState();
        rw::math::Q sixAxisQ(sample.size());
        for (std::size_t index = 0; index < sample.size(); ++index)
            sixAxisQ(index) = sample[index];
        sixAxisDevice->setQ(sixAxisQ, sixAxisState);
        const rws::CanonicalForwardKinematicsResult sixAxisFk =
            rws::CanonicalForwardKinematics::evaluate(sixAxisResult.model, sample);
        REQUIRE(sixAxisFk.valid);
        for (const rw::models::RevoluteJoint::Ptr& joint : sixAxisJoints) {
            rw::math::Transform3D<> importedTransform;
            REQUIRE(rws::CanonicalForwardKinematics::frameTransform(
                sixAxisFk, joint->getName(), importedTransform));
            const rw::math::Transform3D<> workcellTransform = rw::kinematics::Kinematics::frameTframe(
                sixAxisWorkcell->getWorldFrame(), joint.get(), sixAxisState);
            REQUIRE(sameTransform(importedTransform, workcellTransform));
        }
        rw::math::Transform3D<> sixAxisImportedTcp;
        REQUIRE(rws::CanonicalForwardKinematics::frameTransform(
            sixAxisFk, "SixAxisTcp", sixAxisImportedTcp));
        const rw::math::Transform3D<> sixAxisWorkcellTcp = rw::kinematics::Kinematics::frameTframe(
            sixAxisWorkcell->getWorldFrame(), sixAxisTcp.get(), sixAxisState);
        REQUIRE(sameTransform(sixAxisImportedTcp, sixAxisWorkcellTcp));
    }

    REQUIRE(result.sourceMappings.size() == 11);
    REQUIRE(result.sourceMappings.at(1).sourceObjectId == "ImporterBase");
    REQUIRE(result.sourceMappings.at(1).fieldPath ==
            "workcell.devices['ImporterDevice'].frames[0]");

    rws::KinematicImportRequest missingTcp = request;
    missingTcp.tcpFrame = nullptr;
    const rws::KinematicImportResult missingTcpResult =
        rws::KinematicModelImporter::import(missingTcp);
    REQUIRE(!missingTcpResult.ok);
    REQUIRE(!missingTcpResult.diagnostics.empty());
    if (!missingTcpResult.diagnostics.empty()) {
        REQUIRE(missingTcpResult.diagnostics.at(0).code == "KINEMATIC_IMPORT_TCP_MISSING");
        REQUIRE(missingTcpResult.diagnostics.at(0).fieldPath == "tcpFrame");
        REQUIRE(missingTcpResult.diagnostics.at(0).objectId == "ImporterDevice");
    }

    rws::KinematicImportRequest missingWorkcell = request;
    missingWorkcell.workcell = nullptr;
    const rws::KinematicImportResult missingWorkcellResult =
        rws::KinematicModelImporter::import(missingWorkcell);
    REQUIRE(!missingWorkcellResult.ok);
    REQUIRE(!missingWorkcellResult.diagnostics.empty());
    if (!missingWorkcellResult.diagnostics.empty()) {
        REQUIRE(missingWorkcellResult.diagnostics.at(0).code ==
                "KINEMATIC_IMPORT_WORKCELL_MISSING");
        REQUIRE(missingWorkcellResult.diagnostics.at(0).objectId == "ImporterDevice");
        REQUIRE(missingWorkcellResult.diagnostics.at(0).fieldPath == "workcell");
    }

    const rw::models::SerialDevice::Ptr secondDevice = rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tcp, "SecondImporterDevice", structure->getDefaultState()));
    workcell->addDevice(secondDevice);
    rws::KinematicImportRequest missingDevice = request;
    missingDevice.device = nullptr;
    const rws::KinematicImportResult missingDeviceResult =
        rws::KinematicModelImporter::import(missingDevice);
    REQUIRE(!missingDeviceResult.ok);
    REQUIRE(!missingDeviceResult.diagnostics.empty());
    if (!missingDeviceResult.diagnostics.empty()) {
        REQUIRE(missingDeviceResult.diagnostics.at(0).code == "KINEMATIC_IMPORT_DEVICE_MISSING");
        REQUIRE(missingDeviceResult.diagnostics.at(0).fieldPath == "device");
        REQUIRE(missingDeviceResult.diagnostics.at(0).objectId == "ImporterWorkCell");
    }

    const rw::kinematics::FixedFrame outsideTcp("OutsideImporterTcp", rw::math::Transform3D<>());
    rws::KinematicImportRequest outsideTcpRequest = request;
    outsideTcpRequest.tcpFrame = &outsideTcp;
    const rws::KinematicImportResult outsideTcpResult =
        rws::KinematicModelImporter::import(outsideTcpRequest);
    REQUIRE(!outsideTcpResult.ok);
    REQUIRE(!outsideTcpResult.diagnostics.empty());
    if (!outsideTcpResult.diagnostics.empty()) {
        REQUIRE(outsideTcpResult.diagnostics.at(0).code == "KINEMATIC_IMPORT_TCP_NOT_IN_CHAIN");
        REQUIRE(outsideTcpResult.diagnostics.at(0).objectId == "OutsideImporterTcp");
        REQUIRE(outsideTcpResult.diagnostics.at(0).fieldPath == "tcpFrame");
    }

    prismatic->setBounds(rw::math::Q(1, 1.0), rw::math::Q(1, -1.0));
    const rws::KinematicImportResult invalidLimitResult =
        rws::KinematicModelImporter::import(request);
    REQUIRE(!invalidLimitResult.ok);
    REQUIRE(!invalidLimitResult.diagnostics.empty());
    if (!invalidLimitResult.diagnostics.empty()) {
        REQUIRE(invalidLimitResult.diagnostics.at(0).code == "KINEMATIC_IMPORT_LIMIT_INVALID");
        REQUIRE(invalidLimitResult.diagnostics.at(0).objectId == "ImporterPrismatic");
        REQUIRE(invalidLimitResult.diagnostics.at(0).fieldPath ==
                "workcell.devices['ImporterDevice'].frames[3].bounds");
    }

    prismatic->setBounds(rw::math::Q(1, -0.5), rw::math::Q(1, 0.5));
    const rw::models::UniversalJoint::Ptr unsupportedJoint = rw::core::ownedPtr(
        new rw::models::UniversalJoint("UnsupportedImporterJoint", rw::math::Transform3D<>()));
    structure->addFrame(unsupportedJoint, tcp);
    const rw::models::SerialDevice::Ptr unsupportedDevice = rw::core::ownedPtr(
        new rw::models::SerialDevice(base, unsupportedJoint, "UnsupportedImporterDevice",
                                     structure->getDefaultState()));
    workcell->addDevice(unsupportedDevice);
    rws::KinematicImportRequest unsupportedJointRequest = request;
    unsupportedJointRequest.device = unsupportedDevice.get();
    unsupportedJointRequest.tcpFrame = unsupportedJoint.get();
    const rws::KinematicImportResult unsupportedJointResult =
        rws::KinematicModelImporter::import(unsupportedJointRequest);
    REQUIRE(!unsupportedJointResult.ok);
    REQUIRE(!unsupportedJointResult.diagnostics.empty());
    if (!unsupportedJointResult.diagnostics.empty()) {
        REQUIRE(unsupportedJointResult.diagnostics.at(0).code ==
                "KINEMATIC_IMPORT_JOINT_TYPE_UNSUPPORTED");
        REQUIRE(unsupportedJointResult.diagnostics.at(0).objectId == "UnsupportedImporterJoint");
        REQUIRE(unsupportedJointResult.diagnostics.at(0).fieldPath ==
                "workcell.devices['UnsupportedImporterDevice'].frames[5].jointType");
    }

}

// Phase 1/S14: DH is a read-only compatibility projection. It can report an
// exact restricted standard-DH view, but pitch, lateral offsets, and tilted
// axes are explicit lossy/unsupported conditions rather than hidden write-back.
static void testDhProjection()
{
    rws::CanonicalKinematicModel exactModel = validCanonicalModelFixture();
    exactModel.joints[2].motionAxisInJoint = rw::math::Vector3D<>::z();
    const rws::DhProjectionResult exact = rws::DhProjection::project(exactModel);
    REQUIRE(exact.status == rws::DhProjectionStatus::Exact);
    REQUIRE(exact.rows.size() == 2);
    REQUIRE(exact.lostComponents.empty());
    REQUIRE(exact.diagnostics.empty());

    rws::CanonicalKinematicModel pitchModel = exactModel;
    const rw::math::Transform3D<> originalPitchTransform(
        rw::math::Vector3D<>(), rw::math::RPY<>(0.0, 0.25, 0.0).toRotation3D());
    pitchModel.joints[0].parentToJointZero = originalPitchTransform;
    const rws::DhProjectionResult pitch = rws::DhProjection::project(pitchModel);
    REQUIRE(pitch.status == rws::DhProjectionStatus::Lossy);
    REQUIRE(!pitch.lostComponents.empty());
    REQUIRE(!pitch.diagnostics.empty());
    REQUIRE(pitch.diagnostics.at(0).code == "DH_PROJECTION_PARENT_ROTATION_LOSSY");
    REQUIRE(sameTransform(pitchModel.joints[0].parentToJointZero, originalPitchTransform));

    rws::CanonicalKinematicModel lateralModel = exactModel;
    lateralModel.joints[0].jointMotionToChild =
        rw::math::Transform3D<>(rw::math::Vector3D<>(0.0, 0.1, 0.0));
    const rws::DhProjectionResult lateral = rws::DhProjection::project(lateralModel);
    REQUIRE(lateral.status == rws::DhProjectionStatus::Lossy);
    REQUIRE(!lateral.lostComponents.empty());
    REQUIRE(!lateral.diagnostics.empty());
    REQUIRE(lateral.diagnostics.at(0).code == "DH_PROJECTION_CHILD_TRANSLATION_LOSSY");

    rws::CanonicalKinematicModel tiltedAxisModel = exactModel;
    tiltedAxisModel.joints[0].motionAxisInJoint = rw::math::Vector3D<>::x();
    const rws::DhProjectionResult tiltedAxis = rws::DhProjection::project(tiltedAxisModel);
    REQUIRE(tiltedAxis.status == rws::DhProjectionStatus::Unsupported);
    REQUIRE(!tiltedAxis.diagnostics.empty());
    REQUIRE(tiltedAxis.diagnostics.at(0).code == "DH_PROJECTION_AXIS_UNSUPPORTED");
}

// Phase 1/S15: fingerprints are content hashes of canonical data, not memory
// layouts or insertion order. Any semantic kinematic change must invalidate a
// baseline snapshot and non-finite data must never enter a hash.
static void testKinematicFingerprint()
{
    const rws::CanonicalKinematicModel model = validCanonicalModelFixture();
    const rws::KinematicFingerprintResult baseline =
        rws::KinematicFingerprint::forModel(model);
    REQUIRE(baseline.ok);
    REQUIRE(baseline.algorithmId == "fnv1a-64");
    REQUIRE(baseline.serializationVersion == "canonical-kinematic-model-v1");
    REQUIRE(!baseline.value.empty());
    REQUIRE(!rws::KinematicFingerprint::visualColorAffectsFingerprint());
    REQUIRE(rws::KinematicFingerprint::forModel(model).value == baseline.value);

    const rws::KinematicFingerprintResult baselineEnvironment =
        rws::KinematicFingerprint::forEnvironment(model);
    const rws::KinematicFingerprintResult baselineTool =
        rws::KinematicFingerprint::forTool(model);
    REQUIRE(baselineEnvironment.ok);
    REQUIRE(baselineTool.ok);

    rws::CanonicalKinematicModel reordered = model;
    std::reverse(reordered.frames.begin(), reordered.frames.end());
    std::reverse(reordered.joints.begin(), reordered.joints.end());
    std::reverse(reordered.dofs.begin(), reordered.dofs.end());
    const rws::KinematicFingerprintResult reorderedFingerprint =
        rws::KinematicFingerprint::forModel(reordered);
    REQUIRE(reorderedFingerprint.ok);
    REQUIRE(reorderedFingerprint.value == baseline.value);

    rws::CanonicalKinematicModel reorderedBindings = model;
    reorderedBindings.toolBindings[0].geometryBindingIds = {"geometry-a", "geometry-b"};
    reorderedBindings.toolBindings[0].collisionBindingIds = {"collision-a", "collision-b"};
    const rws::KinematicFingerprintResult orderedTool =
        rws::KinematicFingerprint::forTool(reorderedBindings);
    const rws::KinematicFingerprintResult orderedEnvironment =
        rws::KinematicFingerprint::forEnvironment(reorderedBindings);
    std::reverse(reorderedBindings.toolBindings[0].geometryBindingIds.begin(),
                 reorderedBindings.toolBindings[0].geometryBindingIds.end());
    std::reverse(reorderedBindings.toolBindings[0].collisionBindingIds.begin(),
                 reorderedBindings.toolBindings[0].collisionBindingIds.end());
    REQUIRE(rws::KinematicFingerprint::forTool(reorderedBindings).value == orderedTool.value);
    REQUIRE(rws::KinematicFingerprint::forEnvironment(reorderedBindings).value ==
            orderedEnvironment.value);

    rws::CanonicalKinematicModel transformChanged = model;
    transformChanged.joints[0].parentToJointZero.P()(0) = 0.01;
    REQUIRE(rws::KinematicFingerprint::forModel(transformChanged).value != baseline.value);

    rws::CanonicalKinematicModel rotationChanged = model;
    rotationChanged.joints[0].parentToJointZero.R() =
        rw::math::RPY<>(0.0, 0.0, 0.1).toRotation3D();
    REQUIRE(rws::KinematicFingerprint::forModel(rotationChanged).value != baseline.value);

    rws::CanonicalKinematicModel childTransformChanged = model;
    childTransformChanged.joints[0].jointMotionToChild.P()(1) = 0.01;
    REQUIRE(rws::KinematicFingerprint::forModel(childTransformChanged).value != baseline.value);

    rws::CanonicalKinematicModel axisChanged = model;
    axisChanged.joints[0].motionAxisInJoint = rw::math::Vector3D<>::x();
    REQUIRE(rws::KinematicFingerprint::forModel(axisChanged).value != baseline.value);

    rws::CanonicalKinematicModel limitChanged = model;
    limitChanged.joints[0].physicalLimits.enabled = true;
    limitChanged.joints[0].physicalLimits.lower = -1.0;
    limitChanged.joints[0].physicalLimits.upper = 1.0;
    limitChanged.joints[0].physicalLimits.coordinateConvention =
        rws::JointCoordinateConvention::QInput;
    REQUIRE(rws::KinematicFingerprint::forModel(limitChanged).value != baseline.value);

    rws::CanonicalKinematicModel operationalLimitChanged = model;
    operationalLimitChanged.joints[0].operationalLimits.enabled = true;
    operationalLimitChanged.joints[0].operationalLimits.lower = -0.5;
    operationalLimitChanged.joints[0].operationalLimits.upper = 0.5;
    operationalLimitChanged.joints[0].operationalLimits.coordinateConvention =
        rws::JointCoordinateConvention::QInput;
    REQUIRE(rws::KinematicFingerprint::forModel(operationalLimitChanged).value != baseline.value);

    rws::CanonicalKinematicModel limitConventionChanged = limitChanged;
    limitConventionChanged.joints[0].physicalLimits.coordinateConvention =
        rws::JointCoordinateConvention::QModel;
    REQUIRE(rws::KinematicFingerprint::forModel(limitConventionChanged).value !=
            rws::KinematicFingerprint::forModel(limitChanged).value);

    rws::CanonicalKinematicModel dofChanged = model;
    std::swap(dofChanged.dofs[0].qIndex, dofChanged.dofs[1].qIndex);
    REQUIRE(rws::KinematicFingerprint::forModel(dofChanged).value != baseline.value);

    rws::CanonicalKinematicModel toolChanged = model;
    toolChanged.toolBindings[0].flangeToTcp.P()(2) = 0.02;
    REQUIRE(rws::KinematicFingerprint::forModel(toolChanged).value == baseline.value);
    REQUIRE(rws::KinematicFingerprint::forTool(toolChanged).value != baselineTool.value);

    rws::CanonicalKinematicModel geometryChanged = model;
    geometryChanged.toolBindings[0].geometryBindingIds.push_back("geometry-change");
    REQUIRE(rws::KinematicFingerprint::forModel(geometryChanged).value == baseline.value);
    REQUIRE(rws::KinematicFingerprint::forEnvironment(geometryChanged).value ==
            baselineEnvironment.value);
    REQUIRE(rws::KinematicFingerprint::forTool(geometryChanged).value != baselineTool.value);

    rws::CanonicalKinematicModel collisionChanged = model;
    collisionChanged.toolBindings[0].collisionBindingIds.push_back("collision-change");
    REQUIRE(rws::KinematicFingerprint::forModel(collisionChanged).value == baseline.value);
    REQUIRE(rws::KinematicFingerprint::forTool(collisionChanged).value == baselineTool.value);
    REQUIRE(rws::KinematicFingerprint::forEnvironment(collisionChanged).value !=
            baselineEnvironment.value);

    rws::CanonicalKinematicModel nonFinite = model;
    nonFinite.joints[0].parentToJointZero.P()(0) = std::numeric_limits< double >::infinity();
    const rws::KinematicFingerprintResult nonFiniteResult =
        rws::KinematicFingerprint::forModel(nonFinite);
    REQUIRE(!nonFiniteResult.ok);
    REQUIRE(!nonFiniteResult.diagnostics.empty());
    REQUIRE(nonFiniteResult.diagnostics.at(0).code == "KINEMATIC_FINGERPRINT_NONFINITE");

    rws::CanonicalKinematicModel nanModel = model;
    nanModel.joints[0].jointMotionToChild.P()(1) =
        std::numeric_limits< double >::quiet_NaN();
    const rws::KinematicFingerprintResult nanResult =
        rws::KinematicFingerprint::forModel(nanModel);
    REQUIRE(!nanResult.ok);
    REQUIRE(!nanResult.diagnostics.empty());
    REQUIRE(nanResult.diagnostics.at(0).code == "KINEMATIC_FINGERPRINT_NONFINITE");

    rws::CanonicalKinematicModel toolNanModel = model;
    toolNanModel.toolBindings[0].flangeToTcp.P()(0) =
        std::numeric_limits< double >::quiet_NaN();
    REQUIRE(!rws::KinematicFingerprint::forModel(toolNanModel).ok);
    REQUIRE(!rws::KinematicFingerprint::forEnvironment(toolNanModel).ok);
    REQUIRE(!rws::KinematicFingerprint::forTool(toolNanModel).ok);

    const rws::KinematicBaselineSnapshotResult snapshot =
        rws::KinematicBaselineSnapshot::create(model);
    REQUIRE(snapshot.ok);
    REQUIRE(snapshot.snapshot.serializationVersion == baseline.serializationVersion);
    REQUIRE(snapshot.snapshot.modelFingerprint == baseline.value);
    REQUIRE(snapshot.snapshot.environmentFingerprint == baselineEnvironment.value);
    REQUIRE(snapshot.snapshot.toolFingerprint == baselineTool.value);
    REQUIRE(snapshot.snapshot.model.modelId == model.modelId);
    REQUIRE(rws::KinematicFingerprint::forModel(snapshot.snapshot.model).value ==
            snapshot.snapshot.modelFingerprint);
    REQUIRE(rws::KinematicFingerprint::forEnvironment(snapshot.snapshot.model).value ==
            snapshot.snapshot.environmentFingerprint);
    REQUIRE(rws::KinematicFingerprint::forTool(snapshot.snapshot.model).value ==
            snapshot.snapshot.toolFingerprint);
}

// Phase 1/S16: canonical kinematics is a persisted shadow only.  Legacy
// projects retain their original evaluator input and explicitly report that
// the canonical shadow is absent.
static void testCanonicalModelShadow()
{
    rws::StructureOptimizationProblem legacy;
    REQUIRE(legacy.canonicalModelShadow.status ==
            rws::CanonicalModelShadowStatus::CanonicalModelMissing);
    REQUIRE(!legacy.canonicalModelShadow.hasSnapshot());

    const rws::KinematicBaselineSnapshotResult snapshot =
        rws::KinematicBaselineSnapshot::create(validCanonicalModelFixture());
    REQUIRE(snapshot.ok);
    rws::StructureOptimizationProblem problem;
    problem.canonicalModelShadow.snapshot =
        std::make_shared< rws::KinematicBaselineSnapshot >(snapshot.snapshot);
    problem.canonicalModelShadow.status = rws::CanonicalModelShadowStatus::Current;
    REQUIRE(problem.canonicalModelShadow.hasSnapshot());

    const std::string serialized = rws::StructureOptimizationJson::problemToJson(problem);
    REQUIRE(serialized.find("canonicalModelShadow") != std::string::npos);
    rws::StructureOptimizationProblem restored;
    std::string error;
    REQUIRE(rws::StructureOptimizationJson::problemFromJson(serialized, restored, &error));
    REQUIRE(restored.canonicalModelShadow.status == rws::CanonicalModelShadowStatus::Current);
    REQUIRE(restored.canonicalModelShadow.hasSnapshot());
    REQUIRE(restored.canonicalModelShadow.snapshot->modelFingerprint ==
            snapshot.snapshot.modelFingerprint);
    REQUIRE(restored.canonicalModelShadow.snapshot->environmentFingerprint ==
            snapshot.snapshot.environmentFingerprint);
    REQUIRE(restored.canonicalModelShadow.snapshot->toolFingerprint ==
            snapshot.snapshot.toolFingerprint);
    const rws::CanonicalKinematicModelValidationResult restoredValidation =
        rws::CanonicalKinematicModelValidator::validate(
            restored.canonicalModelShadow.snapshot->model);
    REQUIRE(restoredValidation.valid);
    REQUIRE(restored.canonicalModelShadow.snapshot->model.frames.size() ==
            snapshot.snapshot.model.frames.size());
    REQUIRE(restored.canonicalModelShadow.snapshot->model.joints.size() ==
            snapshot.snapshot.model.joints.size());
    REQUIRE(restored.canonicalModelShadow.snapshot->model.toolBindings.size() ==
            snapshot.snapshot.model.toolBindings.size());
    REQUIRE(sameTransform(
        restored.canonicalModelShadow.snapshot->model.joints.at(0).parentToJointZero,
        snapshot.snapshot.model.joints.at(0).parentToJointZero));
    REQUIRE(rws::KinematicFingerprint::forModel(
                restored.canonicalModelShadow.snapshot->model).value ==
            snapshot.snapshot.modelFingerprint);
    REQUIRE(rws::KinematicFingerprint::forEnvironment(
                restored.canonicalModelShadow.snapshot->model).value ==
            snapshot.snapshot.environmentFingerprint);
    REQUIRE(rws::KinematicFingerprint::forTool(
                restored.canonicalModelShadow.snapshot->model).value ==
            snapshot.snapshot.toolFingerprint);
    REQUIRE(rws::CanonicalModelShadowService::assess(
                restored.canonicalModelShadow, snapshot.snapshot.model) ==
            rws::CanonicalModelShadowStatus::Current);
    rws::CanonicalKinematicModel changedSource = snapshot.snapshot.model;
    changedSource.joints.at(0).parentToJointZero.P()(0) += 0.001;
    REQUIRE(rws::CanonicalModelShadowService::assess(
                restored.canonicalModelShadow, changedSource) ==
            rws::CanonicalModelShadowStatus::Stale);
}

// Phase 2/S20: typed design-space PODs are independent from the legacy Qt
// table model.  Stable semantic/property enums, not display paths, define the
// compiler-facing identity of a variable and its binding.
static void testTypedDesignVariableAndBinding()
{
    rws::DesignVariableDefinition valid;
    valid.id = "joint-1-origin-x";
    valid.displayName = "Joint 1 origin X";
    valid.semanticKind = rws::SemanticKind::JointOriginOffsetX;
    valid.role = rws::VariableRole::Independent;
    valid.domain = rws::VariableDomain::Continuous;
    valid.nominalValue = 0.0;
    valid.currentValue = 0.1;
    valid.minimum = -0.5;
    valid.maximum = 0.5;
    valid.step = 0.01;
    valid.unit = rws::DesignVariableUnit::Metres;
    valid.frameId = "base";
    valid.bindingId = "binding:joint-1-origin-x";
    const rws::DesignVariableValidationResult validResult =
        rws::DesignVariableValidator::validate({valid});
    REQUIRE(validResult.valid);

    rws::DesignVariableDefinition duplicateId = valid;
    const rws::DesignVariableValidationResult duplicateResult =
        rws::DesignVariableValidator::validate({valid, duplicateId});
    REQUIRE(!duplicateResult.valid);
    REQUIRE(std::any_of(duplicateResult.diagnostics.begin(), duplicateResult.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "DESIGN_VARIABLE_ID_DUPLICATE";
                        }));

    rws::DesignVariableDefinition nonFinite = valid;
    nonFinite.currentValue = std::numeric_limits< double >::quiet_NaN();
    REQUIRE(!rws::DesignVariableValidator::validate({nonFinite}).valid);
    rws::DesignVariableDefinition invalidRange = valid;
    invalidRange.currentValue = 0.6;
    REQUIRE(!rws::DesignVariableValidator::validate({invalidRange}).valid);
    rws::DesignVariableDefinition invalidStep = valid;
    invalidStep.step = 0.0;
    REQUIRE(!rws::DesignVariableValidator::validate({invalidStep}).valid);

    rws::DesignVariableDefinition discrete = valid;
    discrete.id = "discrete-material";
    discrete.semanticKind = rws::SemanticKind::ParameterizedMaterial;
    discrete.domain = rws::VariableDomain::Discrete;
    discrete.discreteOptions = {{"steel", "Steel", "material:steel"}};
    REQUIRE(rws::DesignVariableValidator::validate({discrete}).valid);
    discrete.discreteOptions.front().id.clear();
    REQUIRE(!rws::DesignVariableValidator::validate({discrete}).valid);

    rws::DesignVariableDefinition positionWithoutFrame = valid;
    positionWithoutFrame.frameId.clear();
    REQUIRE(!rws::DesignVariableValidator::validate({positionWithoutFrame}).valid);
    rws::DesignVariableDefinition derived = valid;
    derived.id = "derived-clearance";
    derived.role = rws::VariableRole::Derived;
    derived.minimum = 10.0;
    derived.maximum = -10.0;
    derived.step = 0.0;
    derived.derivedExpressionId = "expression:clearance";
    REQUIRE(rws::DesignVariableValidator::validate({derived}).valid);
    derived.derivedExpressionId.clear();
    REQUIRE(!rws::DesignVariableValidator::validate({derived}).valid);

    rws::ParameterBinding binding;
    binding.id = "binding:joint-1-origin-x";
    binding.semanticKind = rws::SemanticKind::JointOriginOffsetX;
    binding.targetObjectType = rws::TargetObjectType::Joint;
    binding.targetObjectId = "joint-1";
    binding.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationX;
    binding.coordinateFrameId = "base";
    binding.ownerAdapterId = "JointOriginAdapter";
    binding.ownerAdapterVersion = 1;
    binding.displayPath = "Robot / Joint 1 / Origin X";
    binding.writeSet = {{rws::TargetObjectType::Joint, "joint-1",
                         rws::TargetPropertyId::ParentToJointTranslationX, "base"}};
    REQUIRE(rws::ParameterBindingValidator::validate(binding).valid);
    rws::ParameterBinding missingObject = binding;
    missingObject.targetObjectId.clear();
    REQUIRE(!rws::ParameterBindingValidator::validate(missingObject).valid);
    rws::ParameterBinding missingProperty = binding;
    missingProperty.targetPropertyId = rws::TargetPropertyId::Unknown;
    REQUIRE(!rws::ParameterBindingValidator::validate(missingProperty).valid);
    rws::ParameterBinding alternateDisplay = binding;
    alternateDisplay.displayPath = "Renamed display path";
    REQUIRE(binding.runtimeEquals(alternateDisplay));

    rws::SemanticKind parsedSemantic = rws::SemanticKind::LinkLength;
    REQUIRE(rws::semanticKindToString(rws::SemanticKind::JointOriginOffsetX) ==
            "JointOriginOffsetX");
    REQUIRE(rws::semanticKindFromString("JointOriginOffsetX", parsedSemantic));
    REQUIRE(parsedSemantic == rws::SemanticKind::JointOriginOffsetX);
    REQUIRE(!rws::semanticKindFromString("FutureSemantic", parsedSemantic));
}

// Phase 2/S21: suggestions originate only from the first-phase semantic
// whitelist plus adapter-declared capabilities; nominal zero values and mesh
// names never stand in for a declared parameterization capability.
static void testDesignSpaceRegistryCapabilities()
{
    rws::DesignSpaceRegistry registry;
    rws::SemanticMetadata metadata;
    metadata.semanticKind = rws::SemanticKind::JointZeroOffset;
    metadata.domain = rws::VariableDomain::Continuous;
    metadata.unit = rws::DesignVariableUnit::Radians;
    metadata.applicability = rws::SemanticApplicability::MovableJoint;
    REQUIRE(registry.registerSemantic(metadata));
    REQUIRE(!registry.registerSemantic(metadata));

    const rws::DesignSpaceRegistry firstPhase = rws::DesignSpaceRegistry::firstPhase();
    const rws::SemanticMetadata* linkLength =
        firstPhase.find(rws::SemanticKind::LinkLength);
    REQUIRE(linkLength != nullptr);
    REQUIRE(linkLength->unit == rws::DesignVariableUnit::Metres);
    REQUIRE(linkLength->domain == rws::VariableDomain::Continuous);
    REQUIRE(linkLength->applicability == rws::SemanticApplicability::ParameterizedLink);
    const rws::SemanticMetadata* flangePose = firstPhase.find(rws::SemanticKind::FlangeTx);
    REQUIRE(flangePose != nullptr);
    REQUIRE(flangePose->applicability == rws::SemanticApplicability::FlangeFrame);
    const rws::SemanticMetadata* flangeRotation =
        firstPhase.find(rws::SemanticKind::FlangeRotationVectorZ);
    REQUIRE(flangeRotation != nullptr);
    REQUIRE(flangeRotation->applicability == rws::SemanticApplicability::FlangeFrame);
    const rws::SemanticMetadata* tcpPose = firstPhase.find(rws::SemanticKind::TcpTx);
    REQUIRE(tcpPose != nullptr);
    REQUIRE(tcpPose->applicability == rws::SemanticApplicability::ToolBinding);

    // The registry is the first-phase semantic whitelist, not merely the
    // subset whose suggestion factory happens to be implemented today.
    const rws::SemanticKind firstPhaseSemantics[] = {
        rws::SemanticKind::JointOffsetAlongAxis,
        rws::SemanticKind::JointLimitLower,
        rws::SemanticKind::JointLimitUpper,
        rws::SemanticKind::BaseTx,
        rws::SemanticKind::BaseRotationVectorX,
        rws::SemanticKind::TcpTx,
        rws::SemanticKind::TcpRotationVectorX,
        rws::SemanticKind::FlangeTx,
        rws::SemanticKind::FlangeRotationVectorX,
        rws::SemanticKind::LinkCrossSectionX,
        rws::SemanticKind::LinkWallThickness,
        rws::SemanticKind::LinkScale,
        rws::SemanticKind::ParameterizedMaterial};
    for (const rws::SemanticKind semantic : firstPhaseSemantics)
        REQUIRE(firstPhase.find(semantic) != nullptr);

    const rws::CanonicalKinematicModel model = validCanonicalModelFixture();
    rws::AdapterCapabilityQuery capabilities;
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                       rws::AdapterCapability::JointZeroOffset);
    capabilities.grant(rws::TargetObjectType::Joint, "joint-2",
                       rws::AdapterCapability::JointZeroOffset);
    capabilities.grant(rws::TargetObjectType::Joint, "fixed-flange",
                       rws::AdapterCapability::JointZeroOffset);
    const std::vector< rws::DesignVariableSuggestion > suggestions =
        firstPhase.suggest(model, capabilities);
    REQUIRE(suggestions.empty());
    REQUIRE(std::none_of(suggestions.begin(), suggestions.end(),
                         [](const rws::DesignVariableSuggestion& suggestion) {
                             return suggestion.variable.semanticKind ==
                                    rws::SemanticKind::LinkRadius;
                         }));
    REQUIRE(std::all_of(suggestions.begin(), suggestions.end(),
                        [](const rws::DesignVariableSuggestion& suggestion) {
                            return suggestion.binding.targetObjectId != "fixed-flange";
                        }));
    // Joint-zero coordinates remain available only through an explicit project
    // binding.  Default capability discovery must not silently open this home
    // / calibration-sensitive parameter.
    rws::DesignVariableDefinition explicitZero;
    explicitZero.id = "explicit-zero:joint-1";
    explicitZero.semanticKind = rws::SemanticKind::JointZeroOffset;
    explicitZero.role = rws::VariableRole::Independent;
    explicitZero.domain = rws::VariableDomain::Continuous;
    explicitZero.minimum = -0.5;
    explicitZero.maximum = 0.5;
    explicitZero.step = 0.01;
    explicitZero.unit = rws::DesignVariableUnit::Radians;
    explicitZero.frameId = "base";
    explicitZero.bindingId = "binding:explicit-zero:joint-1";
    rws::ParameterBinding explicitZeroBinding;
    explicitZeroBinding.id = explicitZero.bindingId;
    explicitZeroBinding.semanticKind = explicitZero.semanticKind;
    explicitZeroBinding.targetObjectType = rws::TargetObjectType::Joint;
    explicitZeroBinding.targetObjectId = "joint-1";
    explicitZeroBinding.targetPropertyId = rws::TargetPropertyId::ZeroPositionOffset;
    explicitZeroBinding.coordinateFrameId = "base";
    explicitZeroBinding.ownerAdapterId = "JointZeroAdapter";
    explicitZeroBinding.ownerAdapterVersion = 1;
    explicitZeroBinding.readSet = {{rws::TargetObjectType::Joint, "joint-1",
                                    rws::TargetPropertyId::ZeroPositionOffset, "base"}};
    explicitZeroBinding.writeSet = explicitZeroBinding.readSet;
    rws::AdapterRegistry adapterRegistry;
    REQUIRE(adapterRegistry.registerAdapter(std::make_shared< rws::JointZeroAdapter >()).ok);
    rws::DesignSpaceCompileRequest compileRequest;
    compileRequest.model = &model;
    compileRequest.registry = &firstPhase;
    compileRequest.capabilities = &capabilities;
    compileRequest.adapterRegistry = &adapterRegistry;
    compileRequest.variables = {explicitZero};
    compileRequest.bindings = {explicitZeroBinding};
    REQUIRE(rws::DesignSpaceCompiler::compile(compileRequest).ok);
}

// Phase 2/S23: selected parameterization modes disable alternative physical
// representations before write-set validation.  Read sharing is harmless, but
// two active owners must never write the same physical target.
static void testParameterizationAndWriteSetValidation()
{
    rws::DesignVariableDefinition cartesian;
    cartesian.id = "cartesian-origin";
    cartesian.semanticKind = rws::SemanticKind::JointOriginOffsetX;
    cartesian.role = rws::VariableRole::Independent;
    cartesian.domain = rws::VariableDomain::Continuous;
    cartesian.minimum = -1.0;
    cartesian.maximum = 1.0;
    cartesian.step = 0.1;
    cartesian.unit = rws::DesignVariableUnit::Metres;
    cartesian.frameId = "base";
    cartesian.bindingId = "binding:cartesian-origin";
    cartesian.parameterizationModeId = "JointOriginMode=Cartesian";

    rws::DesignVariableDefinition alongAxis = cartesian;
    alongAxis.id = "along-axis";
    alongAxis.semanticKind = rws::SemanticKind::JointOffsetAlongAxis;
    alongAxis.bindingId = "binding:along-axis";
    alongAxis.parameterizationModeId = "JointOriginMode=AlongAxis";

    const rws::ParameterizationResolution resolution =
        rws::ParameterizationModeResolver::resolve(
            {cartesian, alongAxis}, rws::ParameterizationModeRegistry::firstPhase(),
            {{"JointOriginMode", "JointOriginMode=AlongAxis"}});
    REQUIRE(resolution.valid);
    REQUIRE(resolution.variables.at(0).status ==
            rws::DesignVariableStatus::DisabledByParameterization);
    REQUIRE(resolution.disabledReasons.at("cartesian-origin") ==
            "DisabledByParameterization: JointOriginMode=AlongAxis");
    REQUIRE(resolution.variables.at(1).status == rws::DesignVariableStatus::Available);

    rws::ParameterBinding first;
    first.id = "binding:cartesian-origin";
    first.semanticKind = rws::SemanticKind::JointOriginOffsetX;
    first.targetObjectType = rws::TargetObjectType::Joint;
    first.targetObjectId = "joint-1";
    first.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationX;
    first.ownerAdapterId = "JointOriginAdapter";
    first.ownerAdapterVersion = 1;
    first.writeSet = {{rws::TargetObjectType::Joint, "joint-1",
                       rws::TargetPropertyId::ParentToJointTranslationX, "base"}};
    first.readSet = {{rws::TargetObjectType::Joint, "joint-1",
                      rws::TargetPropertyId::ParentToJointTranslationX, "base"}};
    rws::ParameterBinding second = first;
    second.id = "binding:along-axis";
    second.ownerAdapterId = "ParameterizedLinkAdapter";

    const rws::WriteSetValidationResult disabledAlternative =
        rws::WriteSetValidator::validate(resolution.variables, {first, second});
    REQUIRE(disabledAlternative.valid);

    rws::DesignVariableDefinition conflicting = cartesian;
    conflicting.parameterizationModeId.clear();
    const rws::WriteSetValidationResult conflict = rws::WriteSetValidator::validate(
        {alongAxis, conflicting}, {first, second});
    REQUIRE(!conflict.valid);
    REQUIRE(std::any_of(conflict.diagnostics.begin(), conflict.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "PARAMETER_WRITE_CONFLICT";
                        }));
}

// Phase 2/S24: derived values form a typed, deterministic DAG.  Evaluation
// reads resolved variables only, returns no partial values on failure, and
// never turns a limit relationship into a derived overwrite.
static void testDerivedExpressionsAndDependencyGraph()
{
    rws::DerivedExpression width;
    width.id = "width-from-scale";
    width.kind = rws::DerivedExpressionKind::Multiply;
    width.operands = {rws::DerivedExpressionOperand::variable("link-scale"),
                      rws::DerivedExpressionOperand::constant(0.10,
                                                               rws::DesignVariableUnit::Metres)};
    rws::DerivedExpression height = width;
    height.id = "height-from-scale";
    height.operands[1] = rws::DerivedExpressionOperand::constant(
        0.20, rws::DesignVariableUnit::Metres);

    const rws::DerivedExpressionEvaluationResult evaluated =
        rws::DependencyGraph::evaluate(
            {width, height}, {{"link-scale", {2.0, rws::DesignVariableUnit::Unitless}}});
    REQUIRE(evaluated.ok);
    REQUIRE(evaluated.evaluationOrder ==
            std::vector< std::string >({"height-from-scale", "width-from-scale"}));
    REQUIRE(evaluated.values.at("width-from-scale").value == 0.20);
    REQUIRE(evaluated.values.at("height-from-scale").value == 0.40);
    REQUIRE(evaluated.values.at("width-from-scale").unit == rws::DesignVariableUnit::Metres);

    rws::DerivedExpression unknown = rws::DerivedExpression::variableReference("unknown-ref", "missing");
    const rws::DerivedExpressionEvaluationResult missing =
        rws::DependencyGraph::evaluate({unknown}, {});
    REQUIRE(!missing.ok);
    REQUIRE(missing.values.empty());
    REQUIRE(missing.diagnostics.front().code == "DERIVED_EXPRESSION_REFERENCE_UNKNOWN");

    rws::DerivedExpression cycleA = rws::DerivedExpression::variableReference("cycle-a", "cycle-b");
    rws::DerivedExpression cycleB = rws::DerivedExpression::variableReference("cycle-b", "cycle-a");
    const rws::DerivedExpressionEvaluationResult cycle =
        rws::DependencyGraph::evaluate({cycleA, cycleB}, {});
    REQUIRE(!cycle.ok);
    REQUIRE(cycle.values.empty());
    REQUIRE(cycle.diagnostics.front().code == "DERIVED_EXPRESSION_CYCLE");

    rws::DerivedExpression clamp;
    clamp.id = "clamp";
    clamp.kind = rws::DerivedExpressionKind::Clamp;
    clamp.operands = {rws::DerivedExpressionOperand::constant(2.0, rws::DesignVariableUnit::Metres),
                      rws::DerivedExpressionOperand::constant(0.0, rws::DesignVariableUnit::Metres),
                      rws::DerivedExpressionOperand::constant(1.0, rws::DesignVariableUnit::Metres)};
    rws::DerivedExpression norm;
    norm.id = "norm";
    norm.kind = rws::DerivedExpressionKind::Norm;
    norm.operands = {rws::DerivedExpressionOperand::constant(3.0, rws::DesignVariableUnit::Metres),
                     rws::DerivedExpressionOperand::constant(4.0, rws::DesignVariableUnit::Metres)};
    const rws::DerivedExpressionEvaluationResult extended =
        rws::DependencyGraph::evaluate({clamp, norm}, {});
    REQUIRE(extended.ok);
    REQUIRE(extended.values.at("clamp").value == 1.0);
    REQUIRE(extended.values.at("norm").value == 5.0);

    rws::DerivedExpression unitMismatch;
    unitMismatch.id = "unit-mismatch";
    unitMismatch.kind = rws::DerivedExpressionKind::Add;
    unitMismatch.operands = {rws::DerivedExpressionOperand::constant(1.0, rws::DesignVariableUnit::Metres),
                             rws::DerivedExpressionOperand::constant(1.0, rws::DesignVariableUnit::Radians)};
    const rws::DerivedExpressionEvaluationResult invalidUnits =
        rws::DependencyGraph::evaluate({unitMismatch}, {});
    REQUIRE(!invalidUnits.ok);
    REQUIRE(invalidUnits.values.empty());
    REQUIRE(invalidUnits.diagnostics.front().code == "DERIVED_EXPRESSION_UNIT_MISMATCH");

    rws::DerivedExpression divideByZero = width;
    divideByZero.id = "divide-by-zero";
    divideByZero.kind = rws::DerivedExpressionKind::Divide;
    divideByZero.operands[1] = rws::DerivedExpressionOperand::constant(
        0.0, rws::DesignVariableUnit::Unitless);
    const rws::DerivedExpressionEvaluationResult invalidDivision =
        rws::DependencyGraph::evaluate({divideByZero}, {{"link-scale", {2.0, rws::DesignVariableUnit::Unitless}}});
    REQUIRE(!invalidDivision.ok);
    REQUIRE(invalidDivision.values.empty());
    REQUIRE(invalidDivision.diagnostics.front().code == "DERIVED_EXPRESSION_DIVIDE_BY_ZERO");

    rws::DerivedExpression absolute;
    absolute.id = "absolute";
    absolute.kind = rws::DerivedExpressionKind::RegisteredFunction;
    absolute.registeredFunctionId = "abs";
    absolute.operands = {rws::DerivedExpressionOperand::constant(
        -2.0, rws::DesignVariableUnit::Metres)};
    const rws::DerivedExpressionEvaluationResult registered =
        rws::DependencyGraph::evaluate({absolute}, {});
    REQUIRE(registered.ok);
    REQUIRE(registered.values.at("absolute").value == 2.0);
    REQUIRE(registered.values.at("absolute").unit == rws::DesignVariableUnit::Metres);

    rws::DerivedExpression indirectA = rws::DerivedExpression::variableReference("indirect-a", "indirect-b");
    rws::DerivedExpression indirectB = rws::DerivedExpression::variableReference("indirect-b", "indirect-c");
    rws::DerivedExpression cycleC = rws::DerivedExpression::variableReference("indirect-c", "indirect-a");
    const rws::DerivedExpressionEvaluationResult indirectCycle =
        rws::DependencyGraph::evaluate({indirectA, indirectB, cycleC}, {});
    REQUIRE(!indirectCycle.ok);
    REQUIRE(indirectCycle.values.empty());

    rws::DerivedExpression divide;
    divide.id = "metres-per-unitless";
    divide.kind = rws::DerivedExpressionKind::Divide;
    divide.operands = {rws::DerivedExpressionOperand::constant(
                           0.10, rws::DesignVariableUnit::Metres),
                       rws::DerivedExpressionOperand::constant(
                           2.0, rws::DesignVariableUnit::Unitless)};
    const rws::DerivedExpressionEvaluationResult division =
        rws::DependencyGraph::evaluate({divide}, {});
    REQUIRE(division.ok);
    REQUIRE(division.values.at("metres-per-unitless").unit == rws::DesignVariableUnit::Metres);

    rws::DerivedExpression nonFinite = rws::DerivedExpression::variableReference("non-finite", "bad");
    const rws::DerivedExpressionEvaluationResult invalidNumeric =
        rws::DependencyGraph::evaluate({nonFinite}, {{"bad", {std::numeric_limits<double>::infinity(),
                                                               rws::DesignVariableUnit::Metres}}});
    REQUIRE(!invalidNumeric.ok);
    REQUIRE(invalidNumeric.values.empty());
    REQUIRE(invalidNumeric.diagnostics.front().code == "DERIVED_EXPRESSION_NONFINITE");

    rws::DesignVariableDefinition limit;
    limit.id = "joint-limit-lower";
    limit.role = rws::VariableRole::Derived;
    limit.semanticKind = rws::SemanticKind::JointLimitLower;
    limit.derivedExpressionId = "width-from-scale";
    const rws::DerivedExpressionTargetValidationResult limitTarget =
        rws::DerivedExpressionTargetValidator::validate({limit});
    REQUIRE(!limitTarget.valid);
    REQUIRE(limitTarget.diagnostics.front().code ==
            "DERIVED_EXPRESSION_TARGET_CONSTRAINT_ONLY");
}

// Phase 2/S25: compiler normalization is the sole runtime design-space entry.
// Equivalent input order must yield one independent-only vector schema and
// one fingerprint, with disabled variables retained only as diagnostics.
static void testDesignSpaceCompiler()
{
    rws::DesignVariableDefinition first;
    first.id = "first";
    first.semanticKind = rws::SemanticKind::JointOriginOffsetX;
    first.role = rws::VariableRole::Independent;
    first.domain = rws::VariableDomain::Continuous;
    first.minimum = -1.0;
    first.maximum = 1.0;
    first.step = 0.1;
    first.unit = rws::DesignVariableUnit::Metres;
    first.frameId = "base";
    first.bindingId = "binding:first";
    rws::DesignVariableDefinition second = first;
    second.id = "second";
    second.bindingId = "binding:second";
    second.semanticKind = rws::SemanticKind::JointOffsetAlongAxis;

    rws::ParameterBinding firstBinding;
    firstBinding.id = first.bindingId;
    firstBinding.semanticKind = first.semanticKind;
    firstBinding.targetObjectType = rws::TargetObjectType::Joint;
    firstBinding.targetObjectId = "joint-1";
    firstBinding.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationX;
    firstBinding.ownerAdapterId = "JointOriginAdapter";
    firstBinding.ownerAdapterVersion = 1;
    firstBinding.writeSet = {{rws::TargetObjectType::Joint, "joint-1",
                              rws::TargetPropertyId::ParentToJointTranslationX, "base"}};
    rws::ParameterBinding secondBinding = firstBinding;
    secondBinding.id = second.bindingId;
    secondBinding.semanticKind = second.semanticKind;
    secondBinding.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationY;
    secondBinding.writeSet.front().propertyId = secondBinding.targetPropertyId;

    const rws::CanonicalKinematicModel model = validCanonicalModelFixture();
    const rws::DesignSpaceRegistry registry = rws::DesignSpaceRegistry::firstPhase();
    rws::AdapterRegistry adapterRegistry;
    REQUIRE(adapterRegistry.registerAdapter(std::make_shared< rws::JointOriginAdapter >()).ok);
    rws::AdapterCapabilityQuery capabilities;
    rws::DesignSpaceCompileRequest request;
    request.model = &model;
    request.registry = &registry;
    request.capabilities = &capabilities;
    request.adapterRegistry = &adapterRegistry;
    request.variables = {second, first};
    request.bindings = {secondBinding, firstBinding};
    const rws::DesignSpaceCompileResult compiled = rws::DesignSpaceCompiler::compile(request);
    REQUIRE(compiled.ok);
    REQUIRE(compiled.designSpace.independentVariables.size() == 2);
    REQUIRE(compiled.designSpace.canonicalVectorSchema.at(0).variableId == "first");
    REQUIRE(compiled.designSpace.canonicalVectorSchema.at(1).variableId == "second");
    REQUIRE(!compiled.designSpace.fingerprint.empty());

    rws::DesignSpaceCompileRequest missingAdapterRegistryMaterial = request;
    missingAdapterRegistryMaterial.adapterRegistry = nullptr;
    const rws::DesignSpaceCompileResult missingAdapterRegistry =
        rws::DesignSpaceCompiler::compile(missingAdapterRegistryMaterial);
    REQUIRE(!missingAdapterRegistry.ok);
    REQUIRE(std::any_of(missingAdapterRegistry.diagnostics.begin(),
                        missingAdapterRegistry.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code ==
                                "DESIGN_SPACE_ADAPTER_REGISTRY_REQUIRED";
                        }));

    rws::DesignSpaceCompileRequest allDisabledWithoutRegistry = request;
    for (rws::DesignVariableDefinition& variable : allDisabledWithoutRegistry.variables)
        variable.parameterizationModeId = "LinkPlacementMode=AlongReferenceDirection";
    allDisabledWithoutRegistry.parameterizationSelections = {
        {"LinkPlacementMode", "LinkPlacementMode=CartesianJointOrigin"}};
    const rws::ParameterizationResolution allDisabledResolution =
        rws::ParameterizationModeResolver::resolve(
            allDisabledWithoutRegistry.variables, rws::ParameterizationModeRegistry::firstPhase(),
            allDisabledWithoutRegistry.parameterizationSelections);
    REQUIRE(allDisabledResolution.valid);
    REQUIRE(std::all_of(allDisabledResolution.variables.begin(), allDisabledResolution.variables.end(),
                        [](const rws::DesignVariableDefinition& variable) {
                            return !variable.enabled &&
                                variable.status == rws::DesignVariableStatus::DisabledByParameterization;
                        }));
    allDisabledWithoutRegistry.adapterRegistry = nullptr;
    const rws::DesignSpaceCompileResult allDisabledMissingRegistry =
        rws::DesignSpaceCompiler::compile(allDisabledWithoutRegistry);
    REQUIRE(!allDisabledMissingRegistry.ok);
    REQUIRE(std::any_of(allDisabledMissingRegistry.diagnostics.begin(),
                        allDisabledMissingRegistry.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code ==
                                "DESIGN_SPACE_ADAPTER_REGISTRY_REQUIRED";
                        }));

    rws::DesignSpaceCompileRequest reordered = request;
    reordered.variables = {first, second};
    reordered.bindings = {firstBinding, secondBinding};
    const rws::DesignSpaceCompileResult reorderedCompiled =
        rws::DesignSpaceCompiler::compile(reordered);
    REQUIRE(reorderedCompiled.ok);
    REQUIRE(reorderedCompiled.designSpace.fingerprint == compiled.designSpace.fingerprint);
    REQUIRE(reorderedCompiled.designSpace.canonicalVectorSchema ==
            compiled.designSpace.canonicalVectorSchema);

    rws::AdapterCapabilityQuery changedCapabilities;
    changedCapabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                              rws::AdapterCapability::JointOrigin);
    rws::DesignSpaceCompileRequest capabilityChanged = request;
    capabilityChanged.capabilities = &changedCapabilities;
    const rws::DesignSpaceCompileResult capabilityChangedCompiled =
        rws::DesignSpaceCompiler::compile(capabilityChanged);
    REQUIRE(capabilityChangedCompiled.ok);
    REQUIRE(capabilityChangedCompiled.designSpace.fingerprint != compiled.designSpace.fingerprint);

    rws::DesignVariableDefinition derived = first;
    derived.id = "derived";
    derived.role = rws::VariableRole::Derived;
    derived.semanticKind = rws::SemanticKind::JointOriginOffsetZ;
    derived.bindingId = "binding:derived";
    derived.derivedExpressionId = "expression:derived";
    rws::ParameterBinding derivedBinding = firstBinding;
    derivedBinding.id = derived.bindingId;
    derivedBinding.semanticKind = derived.semanticKind;
    derivedBinding.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationZ;
    derivedBinding.writeSet.front().propertyId = derivedBinding.targetPropertyId;
    const rws::DerivedExpression derivedExpression =
        rws::DerivedExpression::variableReference(derived.derivedExpressionId, first.id);
    rws::DesignSpaceCompileRequest withDerived = request;
    withDerived.variables = {derived, second, first};
    withDerived.bindings = {derivedBinding, secondBinding, firstBinding};
    withDerived.derivedExpressions = {derivedExpression};
    const rws::DesignSpaceCompileResult derivedCompiled =
        rws::DesignSpaceCompiler::compile(withDerived);
    REQUIRE(derivedCompiled.ok);
    REQUIRE(derivedCompiled.designSpace.independentVariables.size() == 2);
    REQUIRE(derivedCompiled.designSpace.derivedVariables.size() == 1);
    REQUIRE(derivedCompiled.designSpace.dependencyOrder.size() == 1);
    REQUIRE(derivedCompiled.designSpace.dependencyOrder.front() == derived.id);

    rws::DesignVariableDefinition disabled = first;
    disabled.id = "disabled";
    disabled.bindingId = "binding:disabled";
    disabled.semanticKind = rws::SemanticKind::LinkLength;
    disabled.parameterizationModeId = "LinkPlacementMode=AlongReferenceDirection";
    rws::ParameterBinding disabledBinding = firstBinding;
    disabledBinding.id = disabled.bindingId;
    disabledBinding.semanticKind = disabled.semanticKind;
    disabledBinding.referenceDirectionFrameId = "base";
    disabledBinding.referenceDirection = rw::math::Vector3D<>::x();
    rws::DesignSpaceCompileRequest withDisabled = request;
    withDisabled.variables = {first, disabled};
    withDisabled.bindings = {firstBinding, disabledBinding};
    withDisabled.parameterizationSelections = {
        {"LinkPlacementMode", "LinkPlacementMode=CartesianJointOrigin"}};
    const rws::DesignSpaceCompileResult disabledCompiled =
        rws::DesignSpaceCompiler::compile(withDisabled);
    REQUIRE(disabledCompiled.ok);
    REQUIRE(disabledCompiled.designSpace.independentVariables.size() == 1);
    REQUIRE(disabledCompiled.designSpace.disabledReasons.count(disabled.id) == 1);
    REQUIRE(std::any_of(disabledCompiled.diagnostics.begin(), disabledCompiled.diagnostics.end(),
                        [&disabled](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "DESIGN_SPACE_VARIABLE_DISABLED" &&
                                   diagnostic.fieldPath == disabled.id;
                        }));

    rws::DesignVariableDefinition conflicting = second;
    conflicting.id = "conflicting";
    conflicting.bindingId = "binding:conflicting";
    rws::ParameterBinding conflictingBinding = firstBinding;
    conflictingBinding.id = conflicting.bindingId;
    conflictingBinding.semanticKind = conflicting.semanticKind;
    rws::DesignSpaceCompileRequest withConflict = request;
    withConflict.variables = {first, conflicting};
    withConflict.bindings = {firstBinding, conflictingBinding};
    REQUIRE(!rws::DesignSpaceCompiler::compile(withConflict).ok);

    rws::DesignSpaceCompileRequest unbound = request;
    unbound.bindings.clear();
    REQUIRE(!rws::DesignSpaceCompiler::compile(unbound).ok);

    rws::DerivedExpression badUnitExpression = derivedExpression;
    badUnitExpression.operands = {
        rws::DerivedExpressionOperand::constant(1.0, rws::DesignVariableUnit::Radians)};
    rws::DesignSpaceCompileRequest badUnit = withDerived;
    badUnit.derivedExpressions = {badUnitExpression};
    REQUIRE(!rws::DesignSpaceCompiler::compile(badUnit).ok);

    rws::DesignVariableDefinition cycle = derived;
    cycle.id = "cycle";
    cycle.bindingId = "binding:cycle";
    cycle.derivedExpressionId = "expression:cycle";
    rws::ParameterBinding cycleBinding = derivedBinding;
    cycleBinding.id = cycle.bindingId;
    cycleBinding.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationY;
    cycleBinding.writeSet.front().propertyId = cycleBinding.targetPropertyId;
    rws::DerivedExpression cycleExpression = rws::DerivedExpression::variableReference(
        cycle.derivedExpressionId, derived.derivedExpressionId);
    rws::DerivedExpression derivedCycleExpression = rws::DerivedExpression::variableReference(
        derived.derivedExpressionId, cycle.derivedExpressionId);
    rws::DesignSpaceCompileRequest cyclic = request;
    cyclic.variables = {first, derived, cycle};
    cyclic.bindings = {firstBinding, derivedBinding, cycleBinding};
    cyclic.derivedExpressions = {cycleExpression, derivedCycleExpression};
    REQUIRE(!rws::DesignSpaceCompiler::compile(cyclic).ok);
}

static rws::CompiledDesignSpace designVectorSpaceFixture()
{
    rws::CompiledDesignSpace space;
    space.fingerprint = "design-space-fixture";

    rws::DesignVariableDefinition length;
    length.id = "length";
    length.domain = rws::VariableDomain::Continuous;
    length.minimum = -1.0;
    length.maximum = 1.0;
    length.step = 0.1;
    length.unit = rws::DesignVariableUnit::Metres;
    rws::DesignVariableDefinition count = length;
    count.id = "count";
    count.domain = rws::VariableDomain::Integer;
    count.minimum = 0.0;
    count.maximum = 10.0;
    count.step = 2.0;
    count.unit = rws::DesignVariableUnit::Unitless;
    rws::DesignVariableDefinition material = count;
    material.id = "material";
    material.domain = rws::VariableDomain::Discrete;
    material.minimum = 0.0;
    material.maximum = 0.0;
    material.step = 0.0;
    material.discreteOptions = {{"aluminum", "Aluminum", "Al"}, {"steel", "Steel", "Fe"}};
    space.independentVariables = {length, count, material};
    space.canonicalVectorSchema = {{length.id, 0, length.unit}, {count.id, 1, count.unit},
                                   {material.id, 2, material.unit}};
    return space;
}

static void testDesignVector()
{
    const rws::CompiledDesignSpace space = designVectorSpaceFixture();
    const std::vector< rws::NormalizedDesignValue > normalized = {
        {0.75, ""}, {0.4, ""}, {0.0, "steel"}};
    const rws::DesignVectorResult decoded =
        rws::DesignVectorCodec::fromNormalized(space, normalized);
    REQUIRE(decoded.ok);
    REQUIRE(decoded.vector.values.size() == 3);
    REQUIRE(std::abs(decoded.vector.values[0].engineeringValue - 0.5) < 1e-12);
    REQUIRE(std::abs(decoded.vector.values[1].engineeringValue - 4.0) < 1e-12);
    REQUIRE(decoded.vector.values[2].discreteOptionId == "steel");

    const rws::NormalizedDesignVectorResult encoded =
        rws::DesignVectorCodec::toNormalized(space, decoded.vector);
    REQUIRE(encoded.ok);
    REQUIRE(std::abs(encoded.values[0].normalizedValue - normalized[0].normalizedValue) < 1e-12);
    REQUIRE(std::abs(encoded.values[1].normalizedValue - normalized[1].normalizedValue) < 1e-12);
    REQUIRE(encoded.values[2].discreteOptionId == "steel");

    const std::vector< rws::EngineeringDesignValue > engineering = {
        {"length", rws::DesignVariableUnit::Metres, -0.0, ""},
        {"count", rws::DesignVariableUnit::Unitless, 4.0, ""},
        {"material", rws::DesignVariableUnit::Unitless, 0.0, "steel"}};
    const rws::DesignVectorResult fromEngineering =
        rws::DesignVectorCodec::fromEngineering(space, engineering);
    REQUIRE(fromEngineering.ok);
    const rws::NormalizedDesignVectorResult engineeringNormalized =
        rws::DesignVectorCodec::toNormalized(space, fromEngineering.vector);
    REQUIRE(engineeringNormalized.ok);
    const rws::DesignVectorResult engineeringRoundTrip =
        rws::DesignVectorCodec::fromNormalized(space, engineeringNormalized.values);
    REQUIRE(engineeringRoundTrip.ok);
    REQUIRE(engineeringRoundTrip.vector.canonicalBytes == fromEngineering.vector.canonicalBytes);
    const std::vector< rws::EngineeringDesignValue > positiveZeroEngineering = {
        {"length", rws::DesignVariableUnit::Metres, 0.0, ""},
        {"count", rws::DesignVariableUnit::Unitless, 4.0, ""},
        {"material", rws::DesignVariableUnit::Unitless, 0.0, "steel"}};
    const rws::DesignVectorResult positiveZero =
        rws::DesignVectorCodec::fromEngineering(space, positiveZeroEngineering);
    REQUIRE(positiveZero.ok);
    REQUIRE(fromEngineering.vector.canonicalBytes == positiveZero.vector.canonicalBytes);
    REQUIRE(fromEngineering.vector.fingerprint == positiveZero.vector.fingerprint);
    REQUIRE(!fromEngineering.vector.canonicalBytes.empty());
    REQUIRE(!fromEngineering.vector.fingerprint.empty());

    std::vector< rws::NormalizedDesignValue > badContinuous = normalized;
    badContinuous[0].normalizedValue = 1.1;
    REQUIRE(!rws::DesignVectorCodec::fromNormalized(space, badContinuous).ok);
    std::vector< rws::EngineeringDesignValue > badRange = engineering;
    badRange[0].engineeringValue = 1.1;
    REQUIRE(!rws::DesignVectorCodec::fromEngineering(space, badRange).ok);

    std::vector< rws::NormalizedDesignValue > badInteger = normalized;
    badInteger[1].normalizedValue = 0.35;
    REQUIRE(!rws::DesignVectorCodec::fromNormalized(space, badInteger).ok);
    std::vector< rws::EngineeringDesignValue > badIntegerEngineering = engineering;
    badIntegerEngineering[1].engineeringValue = 5.0;
    REQUIRE(!rws::DesignVectorCodec::fromEngineering(space, badIntegerEngineering).ok);

    std::vector< rws::NormalizedDesignValue > badDiscrete = normalized;
    badDiscrete[2].discreteOptionId = "display-name-is-not-an-id";
    REQUIRE(!rws::DesignVectorCodec::fromNormalized(space, badDiscrete).ok);
    badDiscrete = normalized;
    badDiscrete[2].normalizedValue = 0.5;
    REQUIRE(!rws::DesignVectorCodec::fromNormalized(space, badDiscrete).ok);
    std::vector< rws::EngineeringDesignValue > badDiscreteEngineering = engineering;
    badDiscreteEngineering[2].engineeringValue = 1.0;
    REQUIRE(!rws::DesignVectorCodec::fromEngineering(space, badDiscreteEngineering).ok);
    rws::DesignVector malformedDiscreteVector = decoded.vector;
    malformedDiscreteVector.values[2].engineeringValue = 1.0;
    REQUIRE(!rws::DesignVectorCodec::toNormalized(space, malformedDiscreteVector).ok);
    REQUIRE(!rws::DesignVectorCodec::fromNormalized(
        space, std::vector< rws::NormalizedDesignValue >{normalized[0]}).ok);
    rws::CompiledDesignSpace malformedSchema = space;
    malformedSchema.canonicalVectorSchema[0].index = 1;
    REQUIRE(!rws::DesignVectorCodec::fromNormalized(malformedSchema, normalized).ok);
    rws::CompiledDesignSpace duplicateDiscreteOption = space;
    duplicateDiscreteOption.independentVariables[2].discreteOptions.push_back(
        {"steel", "Duplicate steel", "Duplicate"});
    REQUIRE(!rws::DesignVectorCodec::fromNormalized(duplicateDiscreteOption, normalized).ok);

    std::vector< rws::NormalizedDesignValue > nonFinite = normalized;
    nonFinite[0].normalizedValue = std::numeric_limits< double >::infinity();
    REQUIRE(!rws::DesignVectorCodec::fromNormalized(space, nonFinite).ok);

    rws::CompiledDesignSpace withDerivedAndDisabled = space;
    rws::DesignVariableDefinition derived = withDerivedAndDisabled.independentVariables.front();
    derived.id = "derived";
    derived.role = rws::VariableRole::Derived;
    withDerivedAndDisabled.derivedVariables.push_back(derived);
    withDerivedAndDisabled.disabledReasons["disabled"] = "DisabledByParameterization";
    const rws::DesignVectorResult independentOnly =
        rws::DesignVectorCodec::fromNormalized(withDerivedAndDisabled, normalized);
    REQUIRE(independentOnly.ok);
    REQUIRE(independentOnly.vector.values.size() == 3);
    REQUIRE(std::none_of(independentOnly.vector.values.begin(), independentOnly.vector.values.end(),
                         [](const rws::DesignVectorValue& value) {
                             return value.variableId == "derived" || value.variableId == "disabled";
                         }));
}

static void testLegacyDesignSpaceMigrationPreview()
{
    rws::StructureOptimizationProblem problem;
    rws::StructureDesignVariable linkLength;
    linkLength.id = "legacy-link-length";
    linkLength.label = "Legacy link length";
    linkLength.targetName = "Link1";
    linkLength.unit = "mm";
    linkLength.kind = rws::StructureVariableKind::JointPositionX;
    linkLength.currentValue = 500.0;
    linkLength.minimum = 400.0;
    linkLength.maximum = 700.0;
    linkLength.step = 10.0;
    rws::StructureDesignVariable baseHeight = linkLength;
    baseHeight.id = "legacy-base-height";
    baseHeight.label = "Legacy base height";
    baseHeight.targetName = "Base";
    baseHeight.unit = "m";
    baseHeight.kind = rws::StructureVariableKind::BaseHeight;
    baseHeight.currentValue = 0.5;
    baseHeight.minimum = 0.3;
    baseHeight.maximum = 0.7;
    baseHeight.step = 0.01;
    rws::StructureDesignVariable dh = linkLength;
    dh.id = "legacy-dh-a";
    dh.kind = rws::StructureVariableKind::DhA;
    rws::StructureDesignVariable unbound = linkLength;
    unbound.id = "legacy-unbound";
    unbound.targetName.clear();
    unbound.kind = rws::StructureVariableKind::JointPositionY;
    problem.variables = {linkLength, baseHeight, dh, unbound};

    rws::LegacyDesignSpaceBindingHint linkLengthHint;
    linkLengthHint.legacyVariableId = linkLength.id;
    linkLengthHint.semanticKind = rws::SemanticKind::LinkLength;
    linkLengthHint.binding.id = "binding:legacy-link-length";
    linkLengthHint.binding.semanticKind = rws::SemanticKind::LinkLength;
    linkLengthHint.binding.targetObjectType = rws::TargetObjectType::Joint;
    linkLengthHint.binding.targetObjectId = "joint-2";
    linkLengthHint.binding.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationX;
    linkLengthHint.binding.coordinateFrameId = "base";
    linkLengthHint.binding.ownerAdapterId = "ParameterizedLinkAdapter";
    linkLengthHint.binding.ownerAdapterVersion = 1;
    linkLengthHint.binding.referenceDirectionFrameId = "base";
    linkLengthHint.binding.referenceDirection = rw::math::Vector3D<>::x();
    linkLengthHint.binding.writeSet = {{rws::TargetObjectType::Joint, "joint-2",
                                       rws::TargetPropertyId::ParentToJointTranslationX, "base"}};

    const rws::LegacyDesignSpaceMigrationPreview preview =
        rws::LegacyDesignSpaceAdapter::preview(problem, {linkLengthHint});
    REQUIRE(preview.mappedVariables.size() == 2);
    REQUIRE(preview.bindings.size() == 2);
    REQUIRE(preview.entries.size() == problem.variables.size());
    REQUIRE(preview.entries[0].mapped);
    REQUIRE(preview.entries[0].variable.semanticKind == rws::SemanticKind::LinkLength);
    REQUIRE(preview.entries[0].variable.source == rws::DesignVariableSource::Legacy);
    REQUIRE(preview.entries[0].source.unit == "mm");
    REQUIRE(std::abs(preview.entries[0].source.minimum - linkLength.minimum) < 1e-12);
    REQUIRE(std::abs(preview.entries[0].variable.currentValue - 0.5) < 1e-12);
    REQUIRE(std::abs(preview.entries[0].variable.minimum - 0.4) < 1e-12);
    REQUIRE(std::abs(preview.entries[0].variable.maximum - 0.7) < 1e-12);
    REQUIRE(std::abs(preview.entries[0].variable.step - 0.01) < 1e-12);
    REQUIRE(preview.entries[0].variable.unit == rws::DesignVariableUnit::Metres);
    REQUIRE(preview.entries[1].mapped);
    REQUIRE(preview.entries[1].variable.semanticKind == rws::SemanticKind::BaseTz);
    REQUIRE(preview.entries[1].binding.targetPropertyId == rws::TargetPropertyId::BaseTranslationZ);
    REQUIRE(preview.entries[2].disposition == "legacy/projection-only");
    REQUIRE(!preview.entries[2].variable.enabled);
    REQUIRE(preview.entries[3].disposition == "legacy/unbound");
    REQUIRE(!preview.entries[3].variable.enabled);
    REQUIRE(std::any_of(preview.diagnostics.begin(), preview.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "LEGACY_DH_PROJECTION_ONLY";
                        }));
    REQUIRE(std::any_of(preview.diagnostics.begin(), preview.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "LEGACY_VARIABLE_UNBOUND";
                        }));
    REQUIRE(problem.variables.size() == 4);
    REQUIRE(problem.variables[0].unit == "mm");
    REQUIRE(std::abs(problem.variables[0].currentValue - 500.0) < 1e-12);

    const rws::LegacyDesignSpaceMigrationPreview repeated =
        rws::LegacyDesignSpaceAdapter::preview(problem, {linkLengthHint});
    REQUIRE(repeated.entries.size() == preview.entries.size());
    REQUIRE(repeated.mappedVariables.size() == preview.mappedVariables.size());
    REQUIRE(repeated.entries[0].variable.id == preview.entries[0].variable.id);
    REQUIRE(repeated.entries[0].variable.bindingId == preview.entries[0].variable.bindingId);
    REQUIRE(repeated.entries[2].disposition == preview.entries[2].disposition);
}

// Phase 3/S30: registry and adapters are pure compiler contracts.  The mock
// does not retain a WorkCell and only returns a typed patch; later slices add
// the real adapters without changing this contract.
class AdapterRegistryTestAdapter : public rws::IModelParameterAdapter
{
  public:
    explicit AdapterRegistryTestAdapter(const std::string& id = "test-joint-origin",
                                        int version = 1,
                                        bool declareWrites = true,
                                        bool bypassGenericBindingValidation = false,
                                        bool emitUncontextualizedDiagnostic = false,
                                        bool emitUncontextualizedValidationDiagnostic = false,
                                        bool emitOkResultError = false,
                                        bool emitOkPatchError = false)
        : _id(id), _version(version), _declareWrites(declareWrites),
          _bypassGenericBindingValidation(bypassGenericBindingValidation),
          _emitUncontextualizedDiagnostic(emitUncontextualizedDiagnostic),
          _emitUncontextualizedValidationDiagnostic(emitUncontextualizedValidationDiagnostic),
          _emitOkResultError(emitOkResultError), _emitOkPatchError(emitOkPatchError)
    {}

    std::string adapterId() const override { return _id; }
    int adapterVersion() const override { return _version; }
    std::vector< rws::SemanticKind > supportedSemanticKinds() const override
    {
        return {rws::SemanticKind::JointOriginOffsetX};
    }
    std::vector< rws::AdapterCapability > requiredCapabilities() const override
    {
        return {rws::AdapterCapability::JointOrigin};
    }
    rws::AdapterBindingValidationResult validateBinding(
        const rws::ParameterBinding& binding,
        const rws::CanonicalKinematicModel&) const override
    {
        rws::AdapterBindingValidationResult result;
        if (_emitUncontextualizedValidationDiagnostic) {
            rws::StructureOptimizationDiagnostic diagnostic;
            diagnostic.code = "ADAPTER_TEST_VALIDATE_UNCONTEXTUALIZED";
            diagnostic.message = "Test adapter emitted a raw validation diagnostic.";
            result.diagnostics.push_back(diagnostic);
        }
        if (_bypassGenericBindingValidation)
            return result;
        const rws::ParameterBindingValidationResult bindingValidation =
            rws::ParameterBindingValidator::validate(binding);
        result.valid = bindingValidation.valid;
        result.diagnostics.insert(result.diagnostics.end(), bindingValidation.diagnostics.begin(),
                                  bindingValidation.diagnostics.end());
        return result;
    }
    std::vector< rws::ReadWriteTarget > declaredReadSet(
        const rws::ParameterBinding& binding) const override
    {
        return binding.readSet;
    }
    std::vector< rws::ReadWriteTarget > declaredWriteSet(
        const rws::ParameterBinding& binding) const override
    {
        return _declareWrites ? binding.writeSet : std::vector< rws::ReadWriteTarget >();
    }
    rws::AdapterPatchCompileResult compilePatch(
        const rws::AdapterPatchCompileRequest& request) const override
    {
        rws::AdapterPatchCompileResult result;
        if (_emitUncontextualizedDiagnostic) {
            rws::StructureOptimizationDiagnostic diagnostic;
            diagnostic.code = "ADAPTER_TEST_UNCONTEXTUALIZED";
            diagnostic.message = "Test adapter emitted a raw diagnostic.";
            result.diagnostics.push_back(diagnostic);
            return result;
        }
        if (request.binding == nullptr) {
            result.diagnostics.push_back(rws::makeAdapterDiagnostic(
                _id, "", "", "binding", "ADAPTER_TEST_BINDING_REQUIRED",
                "Test adapter requires a binding."));
            return result;
        }
        result.ok = true;
        result.patch.adapterId = _id;
        result.patch.adapterVersion = _version;
        result.patch.bindingId = request.binding->id;
        const rws::ReadWriteTarget target = request.binding->writeSet.empty() ?
            rws::ReadWriteTarget() : request.binding->writeSet.front();
        result.patch.writes.push_back({target, rws::CandidatePatchValue::scalar(0.25)});
        if (_emitOkResultError) {
            rws::StructureOptimizationDiagnostic diagnostic;
            diagnostic.code = "ADAPTER_TEST_RESULT_ERROR";
            diagnostic.severity = "Error";
            diagnostic.message = "Test adapter returned an Error with ok=true.";
            result.diagnostics.push_back(diagnostic);
        }
        if (_emitOkPatchError) {
            rws::StructureOptimizationDiagnostic diagnostic;
            diagnostic.code = "ADAPTER_TEST_PATCH_ERROR";
            diagnostic.severity = "Error";
            diagnostic.message = "Test adapter patch contains an Error with ok=true.";
            result.patch.diagnostics.push_back(diagnostic);
        }
        return result;
    }
    std::string describeEffect(const rws::ParameterBinding&) const override
    {
        return "test-only typed joint-origin patch";
    }

  private:
    std::string _id;
    int _version;
    bool _declareWrites;
    bool _bypassGenericBindingValidation;
    bool _emitUncontextualizedDiagnostic;
    bool _emitUncontextualizedValidationDiagnostic;
    bool _emitOkResultError;
    bool _emitOkPatchError;
};

static bool hasAdapterDiagnostic(const std::vector< rws::StructureOptimizationDiagnostic >& diagnostics,
                                 const std::string& code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [&code](const rws::StructureOptimizationDiagnostic& diagnostic) {
                           return diagnostic.code == code;
                       });
}

static rws::ParameterBinding adapterRegistryBinding()
{
    rws::ParameterBinding binding;
    binding.id = "binding:test-joint-origin";
    binding.semanticKind = rws::SemanticKind::JointOriginOffsetX;
    binding.targetObjectType = rws::TargetObjectType::Joint;
    binding.targetObjectId = "joint-1";
    binding.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationX;
    binding.coordinateFrameId = "base";
    binding.ownerAdapterId = "test-joint-origin";
    binding.ownerAdapterVersion = 1;
    binding.readSet = {{rws::TargetObjectType::Joint, "joint-1",
                        rws::TargetPropertyId::ParentToJointTranslationX, "base"}};
    binding.writeSet = binding.readSet;
    return binding;
}

static void testAdapterRegistryAndCandidatePatch()
{
    rws::AdapterRegistry registry;
    const std::shared_ptr< AdapterRegistryTestAdapter > adapter =
        std::make_shared< AdapterRegistryTestAdapter >();
    const rws::AdapterRegistryRegistrationResult registered = registry.registerAdapter(adapter);
    REQUIRE(registered.ok);
    REQUIRE(registry.supports(rws::SemanticKind::JointOriginOffsetX));
    REQUIRE(!registry.supports(rws::SemanticKind::TcpTx));
    REQUIRE(registry.fingerprintMaterial().find("test-joint-origin|1") != std::string::npos);

    const rws::AdapterRegistryRegistrationResult duplicate = registry.registerAdapter(adapter);
    REQUIRE(!duplicate.ok);
    REQUIRE(hasAdapterDiagnostic(duplicate.diagnostics, "ADAPTER_REGISTRY_DUPLICATE_ID_VERSION"));

    const rws::ParameterBinding binding = adapterRegistryBinding();
    const rws::CanonicalKinematicModel baseline = validCanonicalModelFixture();
    rws::AdapterCapabilityQuery capabilities;
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                       rws::AdapterCapability::JointOrigin);
    rws::AdapterPatchCompileRequest request;
    request.baseline = &baseline;
    request.binding = &binding;
    request.values = {{"offset-x", rws::DesignVariableUnit::Metres, 0.25, ""}};

    const rws::AdapterPatchCompileResult compiled = registry.compilePatch(request, capabilities);
    REQUIRE(compiled.ok);
    REQUIRE(compiled.patch.writes.size() == 1);
    REQUIRE(compiled.patch.writes.front().target.objectType == rws::TargetObjectType::Joint);
    REQUIRE(compiled.patch.writes.front().target.propertyId ==
            rws::TargetPropertyId::ParentToJointTranslationX);

    rws::ParameterBinding zeroOwnerVersion = binding;
    zeroOwnerVersion.ownerAdapterVersion = 0;
    const rws::AdapterPatchCompileResult zeroOwnerVersionResult = registry.compilePatch(
        {&baseline, &zeroOwnerVersion, request.values}, capabilities);
    REQUIRE(!zeroOwnerVersionResult.ok);
    REQUIRE(hasAdapterDiagnostic(zeroOwnerVersionResult.diagnostics,
                                 "PARAMETER_BINDING_OWNER_VERSION_INVALID"));

    rws::ParameterBinding mismatchedOwnerVersion = binding;
    mismatchedOwnerVersion.ownerAdapterVersion = 2;
    const rws::AdapterPatchCompileResult mismatchedOwnerVersionResult = registry.compilePatch(
        {&baseline, &mismatchedOwnerVersion, request.values}, capabilities);
    REQUIRE(!mismatchedOwnerVersionResult.ok);
    REQUIRE(hasAdapterDiagnostic(mismatchedOwnerVersionResult.diagnostics,
                                 "ADAPTER_REGISTRY_BINDING_VERSION_MISMATCH"));

    rws::ParameterBinding invalidBinding = binding;
    invalidBinding.targetObjectType = rws::TargetObjectType::Unknown;
    const rws::AdapterPatchCompileResult invalid = registry.compilePatch(
        {&baseline, &invalidBinding, request.values}, capabilities);
    REQUIRE(!invalid.ok);
    REQUIRE(hasAdapterDiagnostic(invalid.diagnostics, "PARAMETER_BINDING_TARGET_OBJECT_REQUIRED"));

    rws::AdapterRegistry bypassRegistry;
    REQUIRE(bypassRegistry.registerAdapter(std::make_shared< AdapterRegistryTestAdapter >(
        "bypasses-binding-validation", 1, true, true)).ok);
    rws::ParameterBinding bypassedInvalidBinding = binding;
    bypassedInvalidBinding.ownerAdapterId = "bypasses-binding-validation";
    bypassedInvalidBinding.targetObjectType = rws::TargetObjectType::Unknown;
    const rws::AdapterPatchCompileResult genericValidationCannotBeBypassed =
        bypassRegistry.compilePatch({&baseline, &bypassedInvalidBinding, request.values}, capabilities);
    REQUIRE(!genericValidationCannotBeBypassed.ok);
    REQUIRE(hasAdapterDiagnostic(genericValidationCannotBeBypassed.diagnostics,
                                 "PARAMETER_BINDING_TARGET_OBJECT_REQUIRED"));

    rws::ParameterBinding missingOwnerBinding = binding;
    missingOwnerBinding.ownerAdapterId.clear();
    const rws::AdapterPatchCompileResult missingOwner = registry.compilePatch(
        {&baseline, &missingOwnerBinding, request.values}, capabilities);
    REQUIRE(!missingOwner.ok);
    REQUIRE(hasAdapterDiagnostic(missingOwner.diagnostics,
                                 "PARAMETER_BINDING_OWNER_REQUIRED"));

    const rws::AdapterPatchCompileResult missingCapability = registry.compilePatch(
        request, rws::AdapterCapabilityQuery());
    REQUIRE(!missingCapability.ok);
    REQUIRE(hasAdapterDiagnostic(missingCapability.diagnostics,
                                 "ADAPTER_REGISTRY_CAPABILITY_REQUIRED"));

    rws::ParameterBinding missingReadBinding = binding;
    missingReadBinding.readSet.clear();
    const rws::AdapterPatchCompileResult missingRead = registry.compilePatch(
        {&baseline, &missingReadBinding, request.values}, capabilities);
    REQUIRE(!missingRead.ok);
    REQUIRE(hasAdapterDiagnostic(missingRead.diagnostics,
                                 "ADAPTER_DECLARED_READ_SET_REQUIRED"));

    rws::AdapterRegistry undeclaredRegistry;
    REQUIRE(undeclaredRegistry.registerAdapter(
        std::make_shared< AdapterRegistryTestAdapter >("undeclared", 1, false)).ok);
    rws::ParameterBinding undeclaredBinding = binding;
    undeclaredBinding.ownerAdapterId = "undeclared";
    const rws::AdapterPatchCompileResult undeclared = undeclaredRegistry.compilePatch(
        {&baseline, &undeclaredBinding, request.values}, capabilities);
    REQUIRE(!undeclared.ok);
    REQUIRE(hasAdapterDiagnostic(undeclared.diagnostics,
                                 "ADAPTER_DECLARED_WRITE_SET_REQUIRED"));

    rws::CandidatePatch malformedPatch = compiled.patch;
    malformedPatch.writes.front().target = {rws::TargetObjectType::Unknown, "",
                                             rws::TargetPropertyId::Unknown, ""};
    const rws::CandidatePatchValidationResult malformed =
        rws::CandidatePatchValidator::validate(malformedPatch, binding.writeSet);
    REQUIRE(!malformed.valid);
    REQUIRE(hasAdapterDiagnostic(malformed.diagnostics, "CANDIDATE_PATCH_WRITE_TARGET_INVALID"));

    rws::CandidatePatch undeclaredPatch = compiled.patch;
    undeclaredPatch.writes.front().target.propertyId = rws::TargetPropertyId::ZeroPositionOffset;
    const rws::CandidatePatchValidationResult undeclaredWrite =
        rws::CandidatePatchValidator::validate(undeclaredPatch, binding.writeSet);
    REQUIRE(!undeclaredWrite.valid);
    REQUIRE(hasAdapterDiagnostic(undeclaredWrite.diagnostics,
                                 "CANDIDATE_PATCH_WRITE_UNDECLARED"));

    const rws::StructureOptimizationDiagnostic adapterError = rws::makeAdapterDiagnostic(
        "test-joint-origin", binding.id, binding.targetObjectId, "writeSet[0]",
        "ADAPTER_TEST_ERROR", "The test adapter rejected a declared field.");
    REQUIRE(adapterError.objectId == binding.targetObjectId);
    REQUIRE(adapterError.fieldPath == "writeSet[0]");
    REQUIRE(std::find(adapterError.evidenceIds.begin(), adapterError.evidenceIds.end(), binding.id) !=
            adapterError.evidenceIds.end());

    rws::AdapterRegistry diagnosticsRegistry;
    REQUIRE(diagnosticsRegistry.registerAdapter(std::make_shared< AdapterRegistryTestAdapter >(
        "uncontextualized-diagnostic", 1, true, false, true, true)).ok);
    rws::ParameterBinding diagnosticBinding = binding;
    diagnosticBinding.ownerAdapterId = "uncontextualized-diagnostic";
    diagnosticBinding.displayPath = "variables[JointOriginOffsetX]";
    const rws::AdapterPatchCompileResult contextualized = diagnosticsRegistry.compilePatch(
        {&baseline, &diagnosticBinding, request.values}, capabilities);
    REQUIRE(!contextualized.ok);
    const auto contextualizedCompileDiagnostic = std::find_if(
        contextualized.diagnostics.begin(), contextualized.diagnostics.end(),
        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
            return diagnostic.code == "ADAPTER_TEST_UNCONTEXTUALIZED";
        });
    const auto contextualizedValidationDiagnostic = std::find_if(
        contextualized.diagnostics.begin(), contextualized.diagnostics.end(),
        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
            return diagnostic.code == "ADAPTER_TEST_VALIDATE_UNCONTEXTUALIZED";
        });
    REQUIRE(contextualizedCompileDiagnostic != contextualized.diagnostics.end());
    REQUIRE(contextualizedValidationDiagnostic != contextualized.diagnostics.end());
    for (const auto diagnostic : {contextualizedCompileDiagnostic,
                                  contextualizedValidationDiagnostic}) {
        if (diagnostic == contextualized.diagnostics.end())
            continue;
        REQUIRE(diagnostic->objectId == diagnosticBinding.targetObjectId);
        REQUIRE(diagnostic->fieldPath == diagnosticBinding.displayPath);
        REQUIRE(std::find(diagnostic->evidenceIds.begin(), diagnostic->evidenceIds.end(),
                diagnosticBinding.id) != diagnostic->evidenceIds.end());
    }

    const auto requireContextualizedError = [&baseline, &binding, &capabilities, &request](
                                               const std::string& adapterId,
                                               bool resultError, bool patchError,
                                               const std::string& code) {
        rws::AdapterRegistry errorRegistry;
        REQUIRE(errorRegistry.registerAdapter(std::make_shared< AdapterRegistryTestAdapter >(
            adapterId, 1, true, false, false, false, resultError, patchError)).ok);
        rws::ParameterBinding errorBinding = binding;
        errorBinding.ownerAdapterId = adapterId;
        errorBinding.displayPath = "variables[error-channel]";
        const rws::AdapterPatchCompileResult errorResult = errorRegistry.compilePatch(
            {&baseline, &errorBinding, request.values}, capabilities);
        REQUIRE(!errorResult.ok);
        const auto error = std::find_if(
            errorResult.diagnostics.begin(), errorResult.diagnostics.end(),
            [&code](const rws::StructureOptimizationDiagnostic& diagnostic) {
                return diagnostic.code == code;
            });
        REQUIRE(error != errorResult.diagnostics.end());
        if (error != errorResult.diagnostics.end()) {
            REQUIRE(error->severity == "Error");
            REQUIRE(error->objectId == errorBinding.targetObjectId);
            REQUIRE(error->fieldPath == errorBinding.displayPath);
            REQUIRE(std::find(error->evidenceIds.begin(), error->evidenceIds.end(),
                              errorBinding.id) != error->evidenceIds.end());
        }
    };
    requireContextualizedError("ok-result-error", true, false,
                              "ADAPTER_TEST_RESULT_ERROR");
    requireContextualizedError("ok-patch-error", false, true,
                              "ADAPTER_TEST_PATCH_ERROR");

    rws::AdapterRegistry versionOneRegistry;
    rws::AdapterRegistry versionTwoRegistry;
    REQUIRE(versionOneRegistry.registerAdapter(std::make_shared< AdapterRegistryTestAdapter >(
        "design-space-fingerprint-adapter", 1)).ok);
    REQUIRE(versionTwoRegistry.registerAdapter(std::make_shared< AdapterRegistryTestAdapter >(
        "design-space-fingerprint-adapter", 2)).ok);
    rws::DesignVariableDefinition designVariable;
    designVariable.id = "design-space-fingerprint-variable";
    designVariable.semanticKind = rws::SemanticKind::JointOriginOffsetX;
    designVariable.role = rws::VariableRole::Independent;
    designVariable.domain = rws::VariableDomain::Continuous;
    designVariable.minimum = -1.0;
    designVariable.maximum = 1.0;
    designVariable.step = 0.1;
    designVariable.unit = rws::DesignVariableUnit::Metres;
    designVariable.frameId = "base";
    designVariable.bindingId = "binding:design-space-fingerprint";
    rws::ParameterBinding designBinding = adapterRegistryBinding();
    designBinding.id = designVariable.bindingId;
    designBinding.ownerAdapterId = "design-space-fingerprint-adapter";
    designBinding.ownerAdapterVersion = 1;
    const rws::DesignSpaceRegistry designRegistry = rws::DesignSpaceRegistry::firstPhase();
    rws::DesignSpaceCompileRequest designRequest;
    designRequest.model = &baseline;
    designRequest.registry = &designRegistry;
    designRequest.capabilities = &capabilities;
    designRequest.variables = {designVariable};
    designRequest.bindings = {designBinding};
    designRequest.adapterRegistry = &versionOneRegistry;
    const rws::DesignSpaceCompileResult versionOneSpace =
        rws::DesignSpaceCompiler::compile(designRequest);
    REQUIRE(versionOneSpace.ok);
    designRequest.adapterRegistry = &versionTwoRegistry;
    const rws::DesignSpaceCompileResult versionMismatch =
        rws::DesignSpaceCompiler::compile(designRequest);
    REQUIRE(!versionMismatch.ok);
    REQUIRE(std::any_of(versionMismatch.diagnostics.begin(), versionMismatch.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "DESIGN_SPACE_ADAPTER_VERSION_MISMATCH";
                        }));
    designRequest.bindings[0].ownerAdapterVersion = 2;
    const rws::DesignSpaceCompileResult versionTwoSpace =
        rws::DesignSpaceCompiler::compile(designRequest);
    REQUIRE(versionTwoSpace.ok);
    REQUIRE(versionOneSpace.designSpace.fingerprint != versionTwoSpace.designSpace.fingerprint);

    rws::DesignSpaceCompileRequest unknownOwner = designRequest;
    unknownOwner.bindings[0].ownerAdapterId = "not-registered";
    const rws::DesignSpaceCompileResult unknownOwnerResult =
        rws::DesignSpaceCompiler::compile(unknownOwner);
    REQUIRE(!unknownOwnerResult.ok);
    REQUIRE(std::any_of(unknownOwnerResult.diagnostics.begin(), unknownOwnerResult.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "DESIGN_SPACE_ADAPTER_OWNER_UNREGISTERED";
                        }));

    rws::DesignSpaceCompileRequest unsupportedSemantic = designRequest;
    unsupportedSemantic.variables[0].semanticKind = rws::SemanticKind::TcpTx;
    unsupportedSemantic.bindings[0].semanticKind = rws::SemanticKind::TcpTx;
    const rws::DesignSpaceCompileResult unsupportedSemanticResult =
        rws::DesignSpaceCompiler::compile(unsupportedSemantic);
    REQUIRE(!unsupportedSemanticResult.ok);
    REQUIRE(std::any_of(unsupportedSemanticResult.diagnostics.begin(),
                        unsupportedSemanticResult.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "DESIGN_SPACE_ADAPTER_SEMANTIC_MISMATCH";
                        }));
}

// Phase 3/S31: real origin and link adapters compile baseline-relative
// translation patches only.  Applying those writes here is deliberately
// test-local; the generic production patch applier belongs to S36.
static rws::ReadWriteTarget jointTranslationTarget(const std::string& jointId,
                                                    rws::TargetPropertyId property,
                                                    const std::string& parentFrame)
{
    return {rws::TargetObjectType::Joint, jointId, property, parentFrame};
}

static rws::ParameterBinding jointTranslationBinding(const std::string& id,
                                                     rws::SemanticKind semantic,
                                                     const std::string& adapterId,
                                                     const std::string& jointId,
                                                     const std::string& parentFrame)
{
    rws::ParameterBinding binding;
    binding.id = id;
    binding.semanticKind = semantic;
    binding.targetObjectType = rws::TargetObjectType::Joint;
    binding.targetObjectId = jointId;
    binding.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationX;
    binding.coordinateFrameId = parentFrame;
    binding.ownerAdapterId = adapterId;
    binding.ownerAdapterVersion = 1;
    binding.readSet = {
        jointTranslationTarget(jointId, rws::TargetPropertyId::ParentToJointTranslationX, parentFrame),
        jointTranslationTarget(jointId, rws::TargetPropertyId::ParentToJointTranslationY, parentFrame),
        jointTranslationTarget(jointId, rws::TargetPropertyId::ParentToJointTranslationZ, parentFrame)};
    binding.writeSet = binding.readSet;
    return binding;
}

static void applyTranslationPatchForTest(rws::CanonicalKinematicModel& model,
                                         const rws::CandidatePatch& patch)
{
    for (const rws::CandidatePatchWrite& write : patch.writes) {
        for (rws::JointEdge& joint : model.joints) {
            if (joint.id != write.target.objectId)
                continue;
            rw::math::Vector3D<> translation = joint.parentToJointZero.P();
            if (write.target.propertyId == rws::TargetPropertyId::ParentToJointTranslationX)
                translation(0) = write.value.scalarValue;
            else if (write.target.propertyId == rws::TargetPropertyId::ParentToJointTranslationY)
                translation(1) = write.value.scalarValue;
            else if (write.target.propertyId == rws::TargetPropertyId::ParentToJointTranslationZ)
                translation(2) = write.value.scalarValue;
            joint.parentToJointZero = rw::math::Transform3D<>(translation,
                                                               joint.parentToJointZero.R());
        }
    }
}

static void testJointOriginAndParameterizedLinkAdapters()
{
    rws::CanonicalKinematicModel baseline = validCanonicalModelFixture();
    baseline.joints[0].parentToJointZero = rw::math::Transform3D<>(
        rw::math::Vector3D<>(1.0, 2.0, 3.0), rw::math::RPY<>(0.0, 0.0, rw::math::Pi / 2.0).toRotation3D());
    baseline.joints[0].motionAxisInJoint = rw::math::Vector3D<>(2.0, 0.0, 0.0);
    const rws::CanonicalKinematicModel sourceCopy = baseline;

    rws::AdapterRegistry registry;
    REQUIRE(registry.registerAdapter(std::make_shared< rws::JointOriginAdapter >()).ok);
    REQUIRE(registry.registerAdapter(std::make_shared< rws::ParameterizedLinkAdapter >()).ok);
    rws::AdapterCapabilityQuery capabilities;
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1", rws::AdapterCapability::JointOrigin);
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1", rws::AdapterCapability::ParameterizedLink);

    rws::ParameterBinding cartesian = jointTranslationBinding(
        "origin-x", rws::SemanticKind::JointOriginOffsetX, "JointOriginAdapter", "joint-1", "base");
    const rws::AdapterPatchCompileResult cartesianPatch = registry.compilePatch(
        {&baseline, &cartesian, {{"origin-x", rws::DesignVariableUnit::Metres, 0.25, ""}}}, capabilities);
    REQUIRE(cartesianPatch.ok);
    REQUIRE(cartesianPatch.patch.writes.size() == 3);
    REQUIRE(std::fabs(cartesianPatch.patch.writes[0].value.scalarValue - 1.25) < 1e-12);
    REQUIRE(std::fabs(cartesianPatch.patch.writes[1].value.scalarValue - 2.0) < 1e-12);
    REQUIRE(std::fabs(cartesianPatch.patch.writes[2].value.scalarValue - 3.0) < 1e-12);
    REQUIRE(cartesianPatch.patch.writes[0].target == cartesian.writeSet[0]);
    REQUIRE(cartesianPatch.patch.writes[1].target == cartesian.writeSet[1]);
    REQUIRE(cartesianPatch.patch.writes[2].target == cartesian.writeSet[2]);

    rws::ParameterBinding cartesianY = jointTranslationBinding(
        "origin-y", rws::SemanticKind::JointOriginOffsetY, "JointOriginAdapter", "joint-1", "base");
    cartesianY.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationY;
    const rws::AdapterPatchCompileResult cartesianYPatch = registry.compilePatch(
        {&baseline, &cartesianY, {{"origin-y", rws::DesignVariableUnit::Metres, 0.25, ""}}}, capabilities);
    REQUIRE(cartesianYPatch.ok);
    REQUIRE(std::fabs(cartesianYPatch.patch.writes[0].value.scalarValue - 1.0) < 1e-12);
    REQUIRE(std::fabs(cartesianYPatch.patch.writes[1].value.scalarValue - 2.25) < 1e-12);
    REQUIRE(std::fabs(cartesianYPatch.patch.writes[2].value.scalarValue - 3.0) < 1e-12);

    rws::ParameterBinding cartesianZ = jointTranslationBinding(
        "origin-z", rws::SemanticKind::JointOriginOffsetZ, "JointOriginAdapter", "joint-1", "base");
    cartesianZ.targetPropertyId = rws::TargetPropertyId::ParentToJointTranslationZ;
    const rws::AdapterPatchCompileResult cartesianZPatch = registry.compilePatch(
        {&baseline, &cartesianZ, {{"origin-z", rws::DesignVariableUnit::Metres, 0.25, ""}}}, capabilities);
    REQUIRE(cartesianZPatch.ok);
    REQUIRE(std::fabs(cartesianZPatch.patch.writes[0].value.scalarValue - 1.0) < 1e-12);
    REQUIRE(std::fabs(cartesianZPatch.patch.writes[1].value.scalarValue - 2.0) < 1e-12);
    REQUIRE(std::fabs(cartesianZPatch.patch.writes[2].value.scalarValue - 3.25) < 1e-12);

    rws::ParameterBinding alongAxis = jointTranslationBinding(
        "origin-axis", rws::SemanticKind::JointOffsetAlongAxis, "JointOriginAdapter", "joint-1", "base");
    const rws::AdapterPatchCompileResult alongAxisPatch = registry.compilePatch(
        {&baseline, &alongAxis, {{"origin-axis", rws::DesignVariableUnit::Metres, 0.5, ""}}}, capabilities);
    REQUIRE(alongAxisPatch.ok);
    // The offset follows the immutable joint-local axis transformed into its
    // parent frame, so no hidden world-axis default can satisfy this check.
    rw::math::Vector3D<> parentAxis = baseline.joints[0].parentToJointZero.R() *
        baseline.joints[0].motionAxisInJoint;
    parentAxis /= parentAxis.norm2();
    REQUIRE(std::fabs(alongAxisPatch.patch.writes[0].value.scalarValue -
                      (1.0 + 0.5 * parentAxis(0))) < 1e-12);
    REQUIRE(std::fabs(alongAxisPatch.patch.writes[1].value.scalarValue -
                      (2.0 + 0.5 * parentAxis(1))) < 1e-12);
    REQUIRE(std::fabs(alongAxisPatch.patch.writes[2].value.scalarValue -
                      (3.0 + 0.5 * parentAxis(2))) < 1e-12);

    rws::CanonicalKinematicModel nonUnitZAxisBaseline = baseline;
    nonUnitZAxisBaseline.joints[0].motionAxisInJoint = rw::math::Vector3D<>(0.0, 0.0, 2.0);
    const rws::AdapterPatchCompileResult nonUnitZAxis = registry.compilePatch(
        {&nonUnitZAxisBaseline, &alongAxis,
         {{"origin-axis", rws::DesignVariableUnit::Metres, 0.5, ""}}}, capabilities);
    REQUIRE(nonUnitZAxis.ok);
    const rw::math::Vector3D<> zAxisDisplacement(
        nonUnitZAxis.patch.writes[0].value.scalarValue - 1.0,
        nonUnitZAxis.patch.writes[1].value.scalarValue - 2.0,
        nonUnitZAxis.patch.writes[2].value.scalarValue - 3.0);
    REQUIRE(std::fabs(zAxisDisplacement.norm2() - 0.5) < 1e-12);

    rws::CanonicalKinematicModel zeroAxisBaseline = baseline;
    zeroAxisBaseline.joints[0].motionAxisInJoint = rw::math::Vector3D<>();
    const rws::AdapterPatchCompileResult zeroAxis = registry.compilePatch(
        {&zeroAxisBaseline, &alongAxis,
         {{"origin-axis", rws::DesignVariableUnit::Metres, 0.5, ""}}}, capabilities);
    REQUIRE(!zeroAxis.ok);
    REQUIRE(zeroAxis.patch.writes.empty());
    REQUIRE(hasAdapterDiagnostic(zeroAxis.diagnostics, "JOINT_ORIGIN_AXIS_INVALID"));

    rws::CanonicalKinematicModel nonFiniteAxisBaseline = baseline;
    nonFiniteAxisBaseline.joints[0].motionAxisInJoint = rw::math::Vector3D<>(
        std::numeric_limits< double >::infinity(), 0.0, 0.0);
    const rws::AdapterPatchCompileResult nonFiniteAxis = registry.compilePatch(
        {&nonFiniteAxisBaseline, &alongAxis,
         {{"origin-axis", rws::DesignVariableUnit::Metres, 0.5, ""}}}, capabilities);
    REQUIRE(!nonFiniteAxis.ok);
    REQUIRE(nonFiniteAxis.patch.writes.empty());
    REQUIRE(hasAdapterDiagnostic(nonFiniteAxis.diagnostics, "JOINT_ORIGIN_AXIS_INVALID"));

    rws::ParameterBinding permutedSets = cartesian;
    std::reverse(permutedSets.readSet.begin(), permutedSets.readSet.end());
    std::rotate(permutedSets.writeSet.begin(), permutedSets.writeSet.begin() + 1,
                permutedSets.writeSet.end());
    const rws::AdapterPatchCompileResult permuted = registry.compilePatch(
        {&baseline, &permutedSets,
         {{"origin-x", rws::DesignVariableUnit::Metres, 0.25, ""}}}, capabilities);
    REQUIRE(permuted.ok);
    rws::ParameterBinding duplicateSet = cartesian;
    duplicateSet.writeSet.push_back(duplicateSet.writeSet.front());
    const rws::AdapterPatchCompileResult duplicate = registry.compilePatch(
        {&baseline, &duplicateSet,
         {{"origin-x", rws::DesignVariableUnit::Metres, 0.25, ""}}}, capabilities);
    REQUIRE(!duplicate.ok);
    REQUIRE(hasAdapterDiagnostic(duplicate.diagnostics, "JOINT_ORIGIN_TRANSLATION_SET_INVALID"));

    rws::ParameterBinding wrongPrimaryProperty = cartesian;
    wrongPrimaryProperty.targetPropertyId = rws::TargetPropertyId::MotionAxisTiltU;
    const rws::AdapterPatchCompileResult wrongPrimary = registry.compilePatch(
        {&baseline, &wrongPrimaryProperty,
         {{"origin-x", rws::DesignVariableUnit::Metres, 0.25, ""}}}, capabilities);
    REQUIRE(!wrongPrimary.ok);
    REQUIRE(hasAdapterDiagnostic(wrongPrimary.diagnostics,
                                 "JOINT_ORIGIN_PRIMARY_PROPERTY_INVALID"));

    rws::ParameterBinding length = jointTranslationBinding(
        "link-length", rws::SemanticKind::LinkLength, "ParameterizedLinkAdapter", "joint-1", "base");
    length.referenceDirectionFrameId = "base";
    length.referenceDirection = rw::math::Vector3D<>::y();
    const rws::AdapterPatchCompileResult lengthPatch = registry.compilePatch(
        {&baseline, &length, {{"link-length", rws::DesignVariableUnit::Metres, 3.0, ""}}}, capabilities);
    REQUIRE(lengthPatch.ok);
    REQUIRE(std::fabs(lengthPatch.patch.writes[0].value.scalarValue - 1.0) < 1e-12);
    REQUIRE(std::fabs(lengthPatch.patch.writes[1].value.scalarValue - 3.0) < 1e-12);
    REQUIRE(std::fabs(lengthPatch.patch.writes[2].value.scalarValue - 3.0) < 1e-12);

    rws::CanonicalKinematicModel zeroNominalBaseline = baseline;
    zeroNominalBaseline.joints[0].parentToJointZero = rw::math::Transform3D<>(
        rw::math::Vector3D<>(1.0, 0.0, 3.0), baseline.joints[0].parentToJointZero.R());
    const rws::AdapterPatchCompileResult zeroNominalPatch = registry.compilePatch(
        {&zeroNominalBaseline, &length,
         {{"link-length", rws::DesignVariableUnit::Metres, 0.4, ""}}}, capabilities);
    REQUIRE(zeroNominalPatch.ok);
    REQUIRE(std::fabs(zeroNominalPatch.patch.writes[0].value.scalarValue - 1.0) < 1e-12);
    REQUIRE(std::fabs(zeroNominalPatch.patch.writes[1].value.scalarValue - 0.4) < 1e-12);
    REQUIRE(std::fabs(zeroNominalPatch.patch.writes[2].value.scalarValue - 3.0) < 1e-12);

    rws::ParameterBinding missingDirection = length;
    missingDirection.referenceDirectionFrameId.clear();
    const rws::AdapterPatchCompileResult missingDirectionResult = registry.compilePatch(
        {&baseline, &missingDirection,
         {{"link-length", rws::DesignVariableUnit::Metres, 3.0, ""}}}, capabilities);
    REQUIRE(!missingDirectionResult.ok);
    REQUIRE(hasAdapterDiagnostic(missingDirectionResult.diagnostics,
                                 "PARAMETER_BINDING_REFERENCE_DIRECTION_FRAME_REQUIRED"));

    rws::ParameterBinding linkWrongPrimaryProperty = length;
    linkWrongPrimaryProperty.targetPropertyId = rws::TargetPropertyId::MotionAxisTiltU;
    const rws::AdapterPatchCompileResult linkWrongPrimary = registry.compilePatch(
        {&baseline, &linkWrongPrimaryProperty,
         {{"link-length", rws::DesignVariableUnit::Metres, 3.0, ""}}}, capabilities);
    REQUIRE(!linkWrongPrimary.ok);
    REQUIRE(hasAdapterDiagnostic(linkWrongPrimary.diagnostics,
                                 "PARAMETERIZED_LINK_PRIMARY_PROPERTY_INVALID"));

    const rws::AdapterPatchCompileResult tooShort = registry.compilePatch(
        {&baseline, &length, {{"link-length", rws::DesignVariableUnit::Metres, 1e-7, ""}}}, capabilities);
    REQUIRE(!tooShort.ok);
    REQUIRE(hasAdapterDiagnostic(tooShort.diagnostics, "PARAMETERIZED_LINK_LENGTH_TOO_SMALL"));

    rws::CanonicalKinematicModel patched = baseline;
    applyTranslationPatchForTest(patched, cartesianPatch.patch);
    const rws::CanonicalForwardKinematicsResult nominalFk =
        rws::CanonicalForwardKinematics::evaluate(baseline, {0.0, 0.0});
    const rws::CanonicalForwardKinematicsResult patchedFk =
        rws::CanonicalForwardKinematics::evaluate(patched, {0.0, 0.0});
    REQUIRE(nominalFk.valid);
    REQUIRE(patchedFk.valid);
    REQUIRE(std::fabs(patchedFk.frameTransforms.at("link").P()(0) -
                      nominalFk.frameTransforms.at("link").P()(0) - 0.25) < 1e-12);
    REQUIRE(sameTransform(baseline.joints[0].parentToJointZero,
                          sourceCopy.joints[0].parentToJointZero));
    const rws::AdapterPatchCompileResult repeated = registry.compilePatch(
        {&baseline, &length, {{"link-length", rws::DesignVariableUnit::Metres, 3.0, ""}}}, capabilities);
    REQUIRE(repeated.ok);
    REQUIRE(repeated.patch.writes.size() == lengthPatch.patch.writes.size());
    for (std::size_t index = 0; index < repeated.patch.writes.size(); ++index) {
        REQUIRE(repeated.patch.writes[index].target == lengthPatch.patch.writes[index].target);
        REQUIRE(std::fabs(repeated.patch.writes[index].value.scalarValue -
                          lengthPatch.patch.writes[index].value.scalarValue) < 1e-12);
    }
}

// Phase 3/S32: axis tilt remains a pair of typed scalar writes.  The only
// combination of U/V below is intentionally test-local; S36 owns generic
// Patch application to a candidate model.
static rws::ParameterBinding jointAxisBinding(const std::string& id,
                                              rws::SemanticKind semantic,
                                              const std::string& jointId,
                                              const std::string& parentFrame,
                                              double maxTiltAngle)
{
    rws::ParameterBinding binding;
    binding.id = id;
    binding.semanticKind = semantic;
    binding.targetObjectType = rws::TargetObjectType::Joint;
    binding.targetObjectId = jointId;
    binding.targetPropertyId = semantic == rws::SemanticKind::JointAxisTiltU ?
        rws::TargetPropertyId::MotionAxisTiltU : rws::TargetPropertyId::MotionAxisTiltV;
    binding.coordinateFrameId = parentFrame;
    binding.ownerAdapterId = "JointAxisAdapter";
    binding.ownerAdapterVersion = 1;
    binding.maxAxisTiltAngle = maxTiltAngle;
    binding.axisTiltGroupId = "axis-tilt:" + jointId;
    const rws::ReadWriteTarget target = {rws::TargetObjectType::Joint, jointId,
                                         binding.targetPropertyId, parentFrame};
    binding.readSet = {target};
    binding.writeSet = {target};
    return binding;
}

static double axisTiltValueForTest(const rws::CandidatePatch& patch,
                                   rws::TargetPropertyId property)
{
    for (const rws::CandidatePatchWrite& write : patch.writes)
        if (write.target.propertyId == property)
            return write.value.scalarValue;
    return std::numeric_limits< double >::quiet_NaN();
}

static void testJointAxisAdapter()
{
    const double maxTilt = rw::math::Pi / 6.0;
    rws::CanonicalKinematicModel baseline = validCanonicalModelFixture();
    baseline.joints[0].motionAxisInJoint = rw::math::Vector3D<>(0.0, 0.0, 2.0);
    const rws::CanonicalKinematicModel sourceCopy = baseline;

    rws::AdapterRegistry registry;
    REQUIRE(registry.registerAdapter(std::make_shared< rws::JointAxisAdapter >()).ok);
    rws::AdapterCapabilityQuery capabilities;
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1", rws::AdapterCapability::JointAxisTilt);
    capabilities.grant(rws::TargetObjectType::Joint, "joint-2", rws::AdapterCapability::JointAxisTilt);
    capabilities.grant(rws::TargetObjectType::Joint, "fixed-flange", rws::AdapterCapability::JointAxisTilt);

    const rws::ParameterBinding u = jointAxisBinding("axis-u", rws::SemanticKind::JointAxisTiltU,
                                                      "joint-1", "base", maxTilt);
    const rws::ParameterBinding v = jointAxisBinding("axis-v", rws::SemanticKind::JointAxisTiltV,
                                                      "joint-1", "base", maxTilt);
    const std::vector< rws::ResolvedAdapterValue > zeroValues = {
        {"axis-u", rws::DesignVariableUnit::Radians, 0.0, "", rws::SemanticKind::JointAxisTiltU,
         "axis-tilt:joint-1"},
        {"axis-v", rws::DesignVariableUnit::Radians, 0.0, "", rws::SemanticKind::JointAxisTiltV,
         "axis-tilt:joint-1"}};
    const rws::AdapterPatchCompileResult zero = registry.compilePatch(
        {&baseline, &u, zeroValues}, capabilities);
    REQUIRE(zero.ok);
    REQUIRE(zero.patch.writes.size() == 1);
    REQUIRE(zero.patch.writes.front().target == u.writeSet.front());
    REQUIRE(std::fabs(axisTiltValueForTest(zero.patch, rws::TargetPropertyId::MotionAxisTiltU)) < 1e-12);
    REQUIRE(std::fabs((rws::KinematicConventions::tiltedAxis(
        baseline.joints[0].motionAxisInJoint,
        axisTiltValueForTest(zero.patch, rws::TargetPropertyId::MotionAxisTiltU), 0.0) -
        rw::math::normalize(baseline.joints[0].motionAxisInJoint)).norm2()) < 1e-12);
    const rws::AdapterPatchCompileResult tiny = registry.compilePatch(
        {&baseline, &u,
         {{"axis-u", rws::DesignVariableUnit::Radians, 1e-13, "",
           rws::SemanticKind::JointAxisTiltU, "axis-tilt:joint-1"},
          {"axis-v", rws::DesignVariableUnit::Radians, 0.0, "",
           rws::SemanticKind::JointAxisTiltV, "axis-tilt:joint-1"}}}, capabilities);
    REQUIRE(tiny.ok);
    REQUIRE(axisTiltValueForTest(tiny.patch, rws::TargetPropertyId::MotionAxisTiltU) == 1e-13);
    REQUIRE((rws::KinematicConventions::tiltedAxis(
        baseline.joints[0].motionAxisInJoint,
        axisTiltValueForTest(tiny.patch, rws::TargetPropertyId::MotionAxisTiltU), 0.0) -
        rw::math::normalize(baseline.joints[0].motionAxisInJoint)).norm2() > 0.0);

    const double alpha = 0.3;
    const double beta = -0.4;
    const std::vector< rws::ResolvedAdapterValue > signedValues = {
        {"axis-u", rws::DesignVariableUnit::Radians, alpha, "", rws::SemanticKind::JointAxisTiltU,
         "axis-tilt:joint-1"},
        {"axis-v", rws::DesignVariableUnit::Radians, beta, "", rws::SemanticKind::JointAxisTiltV,
         "axis-tilt:joint-1"}};
    const rws::AdapterPatchCompileResult signedU = registry.compilePatch(
        {&baseline, &u, signedValues}, capabilities);
    const rws::AdapterPatchCompileResult signedV = registry.compilePatch(
        {&baseline, &v, signedValues}, capabilities);
    REQUIRE(signedU.ok);
    REQUIRE(signedV.ok);
    REQUIRE(std::fabs(axisTiltValueForTest(signedU.patch, rws::TargetPropertyId::MotionAxisTiltU) - alpha) < 1e-12);
    REQUIRE(std::fabs(axisTiltValueForTest(signedV.patch, rws::TargetPropertyId::MotionAxisTiltV) - beta) < 1e-12);
    const rw::math::Vector3D<> tilted = rws::KinematicConventions::tiltedAxis(
        baseline.joints[0].motionAxisInJoint,
        axisTiltValueForTest(signedU.patch, rws::TargetPropertyId::MotionAxisTiltU),
        axisTiltValueForTest(signedV.patch, rws::TargetPropertyId::MotionAxisTiltV));
    REQUIRE(std::fabs(tilted.norm2() - 1.0) < 1e-12);
    REQUIRE(std::fabs(rws::KinematicConventions::angleBetween(
        baseline.joints[0].motionAxisInJoint, tilted) - 0.5) < 1e-12);
    const rws::TangentBasis firstBasis = rws::KinematicConventions::stableTangentBasis(
        baseline.joints[0].motionAxisInJoint);
    const rws::TangentBasis secondBasis = rws::KinematicConventions::stableTangentBasis(
        baseline.joints[0].motionAxisInJoint);
    REQUIRE(firstBasis.valid && secondBasis.valid);
    REQUIRE((firstBasis.first - secondBasis.first).norm2() < 1e-12);
    REQUIRE((firstBasis.second - secondBasis.second).norm2() < 1e-12);

    const std::vector< rws::ResolvedAdapterValue > boundaryValues = {
        {"axis-u", rws::DesignVariableUnit::Radians, maxTilt, "", rws::SemanticKind::JointAxisTiltU,
         "axis-tilt:joint-1"},
        {"axis-v", rws::DesignVariableUnit::Radians, 0.0, "", rws::SemanticKind::JointAxisTiltV,
         "axis-tilt:joint-1"}};
    const rws::AdapterPatchCompileResult boundary = registry.compilePatch(
        {&baseline, &u, boundaryValues}, capabilities);
    REQUIRE(boundary.ok);
    const std::vector< rws::ResolvedAdapterValue > beyondValues = {
        {"axis-u", rws::DesignVariableUnit::Radians, maxTilt + 1e-6, "", rws::SemanticKind::JointAxisTiltU,
         "axis-tilt:joint-1"},
        {"axis-v", rws::DesignVariableUnit::Radians, 0.0, "", rws::SemanticKind::JointAxisTiltV,
         "axis-tilt:joint-1"}};
    const rws::AdapterPatchCompileResult beyond = registry.compilePatch(
        {&baseline, &u, beyondValues}, capabilities);
    REQUIRE(!beyond.ok);
    REQUIRE(hasAdapterDiagnostic(beyond.diagnostics, "JOINT_AXIS_TILT_CONE_EXCEEDED"));
    rws::ParameterBinding widerThanPi = u;
    widerThanPi.id = "axis-u-wide-cone";
    widerThanPi.maxAxisTiltAngle = rw::math::Pi + 1e-6;
    const rws::AdapterPatchCompileResult invalidWideCone = registry.compilePatch(
        {&baseline, &widerThanPi, signedValues}, capabilities);
    REQUIRE(!invalidWideCone.ok);
    REQUIRE(hasAdapterDiagnostic(invalidWideCone.diagnostics,
                                 "PARAMETER_BINDING_AXIS_TILT_CONE_INVALID"));
    const rws::ParameterBinding piCone = jointAxisBinding("axis-u-pi-cone",
        rws::SemanticKind::JointAxisTiltU, "joint-1", "base", rw::math::Pi);
    const rws::AdapterPatchCompileResult piBoundary = registry.compilePatch(
        {&baseline, &piCone,
         {{"axis-u-pi-cone", rws::DesignVariableUnit::Radians, rw::math::Pi, "",
           rws::SemanticKind::JointAxisTiltU, "axis-tilt:joint-1"},
          {"axis-v-pi-cone", rws::DesignVariableUnit::Radians, 0.0, "",
           rws::SemanticKind::JointAxisTiltV, "axis-tilt:joint-1"}}}, capabilities);
    REQUIRE(piBoundary.ok);
    const rws::ParameterBinding zeroCone = jointAxisBinding("axis-u-zero-cone",
        rws::SemanticKind::JointAxisTiltU, "joint-1", "base", 0.0);
    const rws::AdapterPatchCompileResult folded = registry.compilePatch(
        {&baseline, &zeroCone,
         {{"axis-u-zero-cone", rws::DesignVariableUnit::Radians, 2.0 * rw::math::Pi, "",
           rws::SemanticKind::JointAxisTiltU, "axis-tilt:joint-1"},
          {"axis-v-zero-cone", rws::DesignVariableUnit::Radians, 0.0, "",
           rws::SemanticKind::JointAxisTiltV, "axis-tilt:joint-1"}}}, capabilities);
    REQUIRE(!folded.ok);
    REQUIRE(hasAdapterDiagnostic(folded.diagnostics, "JOINT_AXIS_TILT_CONE_EXCEEDED"));
    const rws::AdapterPatchCompileResult crossGroup = registry.compilePatch(
        {&baseline, &u,
         {{"axis-u", rws::DesignVariableUnit::Radians, 0.1, "",
           rws::SemanticKind::JointAxisTiltU, "axis-tilt:joint-1"},
          {"axis-v-other-joint", rws::DesignVariableUnit::Radians, 0.2, "",
           rws::SemanticKind::JointAxisTiltV, "axis-tilt:joint-2"}}}, capabilities);
    REQUIRE(!crossGroup.ok);
    REQUIRE(hasAdapterDiagnostic(crossGroup.diagnostics, "JOINT_AXIS_TILT_GROUP_MISMATCH"));
    rws::ParameterBinding forgedGroup = u;
    forgedGroup.id = "axis-u-forged-group";
    forgedGroup.axisTiltGroupId = "shared-axis-group";
    const rws::AdapterPatchCompileResult forged = registry.compilePatch(
        {&baseline, &forgedGroup,
         {{"axis-u-forged-group", rws::DesignVariableUnit::Radians, 0.1, "",
           rws::SemanticKind::JointAxisTiltU, "shared-axis-group"},
          {"axis-v-joint-2-forged", rws::DesignVariableUnit::Radians, 0.2, "",
           rws::SemanticKind::JointAxisTiltV, "shared-axis-group"}}}, capabilities);
    REQUIRE(!forged.ok);
    REQUIRE(hasAdapterDiagnostic(forged.diagnostics, "PARAMETER_BINDING_AXIS_TILT_GROUP_INVALID"));

    rws::ParameterBinding prismatic = jointAxisBinding("axis-u-prismatic",
        rws::SemanticKind::JointAxisTiltU, "joint-2", "guide", maxTilt);
    const rws::AdapterPatchCompileResult prismaticResult = registry.compilePatch(
        {&baseline, &prismatic,
         {{"axis-u-prismatic", rws::DesignVariableUnit::Radians, 0.1, "",
           rws::SemanticKind::JointAxisTiltU, "axis-tilt:joint-2"},
          {"axis-v-prismatic", rws::DesignVariableUnit::Radians, 0.2, "",
           rws::SemanticKind::JointAxisTiltV, "axis-tilt:joint-2"}}}, capabilities);
    REQUIRE(prismaticResult.ok);
    rws::ParameterBinding fixed = jointAxisBinding("axis-u-fixed", rws::SemanticKind::JointAxisTiltU,
        "fixed-flange", "link", maxTilt);
    const rws::AdapterPatchCompileResult fixedResult = registry.compilePatch(
        {&baseline, &fixed,
         {{"axis-u-fixed", rws::DesignVariableUnit::Radians, 0.1, "",
           rws::SemanticKind::JointAxisTiltU, "axis-tilt:fixed-flange"},
          {"axis-v-fixed", rws::DesignVariableUnit::Radians, 0.0, "",
           rws::SemanticKind::JointAxisTiltV, "axis-tilt:fixed-flange"}}}, capabilities);
    REQUIRE(!fixedResult.ok);
    REQUIRE(hasAdapterDiagnostic(fixedResult.diagnostics, "JOINT_AXIS_MOVABLE_JOINT_REQUIRED"));
    REQUIRE(std::fabs(baseline.joints[0].zeroPositionOffset - sourceCopy.joints[0].zeroPositionOffset) < 1e-12);
    REQUIRE(sameTransform(baseline.joints[0].parentToJointZero,
                          sourceCopy.joints[0].parentToJointZero));
    REQUIRE((baseline.joints[0].motionAxisInJoint - sourceCopy.joints[0].motionAxisInJoint).norm2() < 1e-12);
    const rws::AdapterPatchCompileResult repeat = registry.compilePatch(
        {&baseline, &u, signedValues}, capabilities);
    REQUIRE(repeat.ok);
    REQUIRE(repeat.patch.writes.size() == signedU.patch.writes.size());
    REQUIRE(repeat.patch.writes.front().target == signedU.patch.writes.front().target);
    REQUIRE(std::fabs(repeat.patch.writes.front().value.scalarValue -
                      signedU.patch.writes.front().value.scalarValue) < 1e-12);
}

// Phase 3/S33: zero offsets and joint limits have independent, explicit
// contracts.  The test-local patch application below deliberately is not a
// production applier (that is S36); it only proves the frozen coordinate
// semantics without creating a second runtime mutation path.
static rws::ParameterBinding jointZeroBinding(const std::string& id,
                                              const std::string& jointId,
                                              const std::string& parentFrame)
{
    rws::ParameterBinding binding;
    binding.id = id;
    binding.semanticKind = rws::SemanticKind::JointZeroOffset;
    binding.targetObjectType = rws::TargetObjectType::Joint;
    binding.targetObjectId = jointId;
    binding.targetPropertyId = rws::TargetPropertyId::ZeroPositionOffset;
    binding.coordinateFrameId = parentFrame;
    binding.ownerAdapterId = "JointZeroAdapter";
    binding.ownerAdapterVersion = 1;
    binding.readSet = {{rws::TargetObjectType::Joint, jointId,
                        rws::TargetPropertyId::ZeroPositionOffset, parentFrame}};
    binding.writeSet = binding.readSet;
    return binding;
}

static rws::ParameterBinding jointLimitBinding(const std::string& id,
                                               rws::SemanticKind semantic,
                                               const std::string& jointId,
                                               const std::string& parentFrame,
                                               rws::JointLimitScope scope)
{
    rws::ParameterBinding binding;
    binding.id = id;
    binding.semanticKind = semantic;
    binding.targetObjectType = rws::TargetObjectType::Joint;
    binding.targetObjectId = jointId;
    binding.targetPropertyId = semantic == rws::SemanticKind::JointLimitLower ?
        (scope == rws::JointLimitScope::Physical ? rws::TargetPropertyId::PhysicalLimitLower :
                                                  rws::TargetPropertyId::OperationalLimitLower) :
        (scope == rws::JointLimitScope::Physical ? rws::TargetPropertyId::PhysicalLimitUpper :
                                                  rws::TargetPropertyId::OperationalLimitUpper);
    binding.coordinateFrameId = parentFrame;
    binding.ownerAdapterId = "JointLimitAdapter";
    binding.ownerAdapterVersion = 1;
    binding.jointLimitScope = scope;
    binding.jointLimitCoordinateConvention = rws::JointCoordinateConvention::QInput;
    binding.jointLimitGroupId = "joint-limits:" + jointId;
    binding.minimumJointLimitRange = 0.1;
    binding.absoluteJointLimitLower = -3.0;
    binding.absoluteJointLimitUpper = 3.0;
    binding.readSet = {{rws::TargetObjectType::Joint, jointId, binding.targetPropertyId,
                        parentFrame}};
    binding.writeSet = binding.readSet;
    return binding;
}

static double scalarPatchValueForTest(const rws::CandidatePatch& patch,
                                      rws::TargetPropertyId property)
{
    for (const rws::CandidatePatchWrite& write : patch.writes)
        if (write.target.propertyId == property)
            return write.value.scalarValue;
    return std::numeric_limits< double >::quiet_NaN();
}

static void applyS33PatchForTest(rws::CanonicalKinematicModel& model,
                                 const rws::CandidatePatch& patch)
{
    for (const rws::CandidatePatchWrite& write : patch.writes) {
        for (rws::JointEdge& joint : model.joints) {
            if (joint.id != write.target.objectId)
                continue;
            if (write.target.propertyId == rws::TargetPropertyId::ZeroPositionOffset)
                joint.zeroPositionOffset = write.value.scalarValue;
            else if (write.target.propertyId == rws::TargetPropertyId::PhysicalLimitLower) {
                joint.physicalLimits.enabled = true;
                joint.physicalLimits.lower = write.value.scalarValue;
            } else if (write.target.propertyId == rws::TargetPropertyId::PhysicalLimitUpper) {
                joint.physicalLimits.enabled = true;
                joint.physicalLimits.upper = write.value.scalarValue;
            } else if (write.target.propertyId == rws::TargetPropertyId::OperationalLimitLower) {
                joint.operationalLimits.enabled = true;
                joint.operationalLimits.lower = write.value.scalarValue;
            } else if (write.target.propertyId == rws::TargetPropertyId::OperationalLimitUpper) {
                joint.operationalLimits.enabled = true;
                joint.operationalLimits.upper = write.value.scalarValue;
            }
        }
    }
}

static void testJointZeroAndLimitAdapters()
{
    rws::CanonicalKinematicModel baseline = validCanonicalModelFixture();
    baseline.joints[0].physicalLimits = {true, -2.0, 2.0,
                                         rws::CanonicalCoordinateUnit::Radians,
                                         rws::JointCoordinateConvention::QInput};
    baseline.joints[2].physicalLimits = {true, -2.0, 2.0,
                                         rws::CanonicalCoordinateUnit::Metres,
                                         rws::JointCoordinateConvention::QInput};
    baseline.joints[0].operationalLimits.coordinateConvention =
        rws::JointCoordinateConvention::QInput;
    baseline.joints[2].operationalLimits.coordinateConvention =
        rws::JointCoordinateConvention::QInput;
    const rws::CanonicalKinematicModel sourceCopy = baseline;

    rws::AdapterRegistry registry;
    REQUIRE(registry.registerAdapter(std::make_shared< rws::JointZeroAdapter >()).ok);
    REQUIRE(registry.registerAdapter(std::make_shared< rws::JointLimitAdapter >()).ok);
    rws::AdapterCapabilityQuery capabilities;
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                       rws::AdapterCapability::JointZeroOffset);
    capabilities.grant(rws::TargetObjectType::Joint, "joint-2",
                       rws::AdapterCapability::JointZeroOffset);
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                       rws::AdapterCapability::JointLimits);
    capabilities.grant(rws::TargetObjectType::Joint, "joint-2",
                       rws::AdapterCapability::JointLimits);

    const rws::ParameterBinding revoluteZero =
        jointZeroBinding("zero-revolute", "joint-1", "base");
    const rws::AdapterPatchCompileResult zero = registry.compilePatch(
        {&baseline, &revoluteZero,
         {{"zero-revolute", rws::DesignVariableUnit::Radians, 0.25, "",
           rws::SemanticKind::JointZeroOffset, ""}}}, capabilities);
    REQUIRE(zero.ok);
    REQUIRE(zero.patch.writes.size() == 1);
    REQUIRE(zero.patch.writes.front().target.propertyId ==
            rws::TargetPropertyId::ZeroPositionOffset);
    REQUIRE(std::fabs(zero.patch.writes.front().value.scalarValue - 0.25) < 1e-12);
    rws::CanonicalKinematicModel zeroApplied = baseline;
    applyS33PatchForTest(zeroApplied, zero.patch);
    REQUIRE(std::fabs(rws::KinematicConventions::modelCoordinate(0.0,
                 zeroApplied.joints[0].zeroPositionOffset) - 0.25) < 1e-12);
    REQUIRE((zeroApplied.joints[0].motionAxisInJoint -
             baseline.joints[0].motionAxisInJoint).norm2() < 1e-12);
    REQUIRE(sameTransform(zeroApplied.joints[0].parentToJointZero,
                          baseline.joints[0].parentToJointZero));
    const rws::CanonicalForwardKinematicsResult zeroFk =
        rws::CanonicalForwardKinematics::evaluate(zeroApplied, {0.0, 0.0});
    REQUIRE(zeroFk.valid);
    REQUIRE(std::fabs(zeroFk.frameTransforms.at("link").R()(0, 0) - std::cos(0.25)) < 1e-12);

    const rws::ParameterBinding prismaticZero =
        jointZeroBinding("zero-prismatic", "joint-2", "guide");
    const rws::AdapterPatchCompileResult prismaticZeroResult = registry.compilePatch(
        {&baseline, &prismaticZero,
         {{"zero-prismatic", rws::DesignVariableUnit::Metres, 0.05, "",
           rws::SemanticKind::JointZeroOffset, ""}}}, capabilities);
    REQUIRE(prismaticZeroResult.ok);
    const rws::AdapterPatchCompileResult wrongZeroUnit = registry.compilePatch(
        {&baseline, &prismaticZero,
         {{"zero-prismatic", rws::DesignVariableUnit::Radians, 0.05, "",
           rws::SemanticKind::JointZeroOffset, ""}}}, capabilities);
    REQUIRE(!wrongZeroUnit.ok);
    REQUIRE(hasAdapterDiagnostic(wrongZeroUnit.diagnostics, "JOINT_ZERO_VALUE_UNIT_INVALID"));
    const rws::ParameterBinding fixedZero =
        jointZeroBinding("zero-fixed", "fixed-flange", "link");
    const rws::AdapterPatchCompileResult fixedZeroResult = registry.compilePatch(
        {&baseline, &fixedZero,
         {{"zero-fixed", rws::DesignVariableUnit::Radians, 0.1, "",
           rws::SemanticKind::JointZeroOffset, ""}}}, capabilities);
    REQUIRE(!fixedZeroResult.ok);
    REQUIRE(hasAdapterDiagnostic(fixedZeroResult.diagnostics, "JOINT_ZERO_MOVABLE_JOINT_REQUIRED"));

    rws::ParameterBinding physicalLower = jointLimitBinding(
        "physical-lower", rws::SemanticKind::JointLimitLower, "joint-1", "base",
        rws::JointLimitScope::Physical);
    rws::ParameterBinding physicalUpper = jointLimitBinding(
        "physical-upper", rws::SemanticKind::JointLimitUpper, "joint-1", "base",
        rws::JointLimitScope::Physical);
    physicalLower.allowPhysicalLimitModification = true;
    physicalUpper.allowPhysicalLimitModification = true;
    const std::vector< rws::ResolvedAdapterValue > physicalValues = {
        {"physical-lower", rws::DesignVariableUnit::Radians, -1.5, "",
         rws::SemanticKind::JointLimitLower, "joint-limits:joint-1", rws::JointLimitScope::Physical},
        {"physical-upper", rws::DesignVariableUnit::Radians, 1.5, "",
         rws::SemanticKind::JointLimitUpper, "joint-limits:joint-1", rws::JointLimitScope::Physical}};
    const rws::AdapterPatchCompileResult physical = registry.compilePatch(
        {&baseline, &physicalLower, physicalValues}, capabilities);
    REQUIRE(physical.ok);
    REQUIRE(physical.patch.affectsStructuralCapability);
    REQUIRE(physical.patch.writes.size() == 1);
    REQUIRE(std::fabs(scalarPatchValueForTest(physical.patch,
                                               rws::TargetPropertyId::PhysicalLimitLower) + 1.5) < 1e-12);
    // Limits declare whether their values constrain q_input or q_model.  A
    // non-zero calibration offset is therefore never silently combined with
    // a bound expressed in the other coordinate convention.
    rws::CanonicalKinematicModel qModelBaseline = baseline;
    qModelBaseline.joints[0].zeroPositionOffset = 0.25;
    qModelBaseline.joints[0].physicalLimits.coordinateConvention =
        rws::JointCoordinateConvention::QModel;
    const rws::AdapterPatchCompileResult qInputMismatch = registry.compilePatch(
        {&qModelBaseline, &physicalLower, physicalValues}, capabilities);
    REQUIRE(!qInputMismatch.ok);
    REQUIRE(hasAdapterDiagnostic(qInputMismatch.diagnostics,
                                 "JOINT_LIMIT_COORDINATE_CONVENTION_MISMATCH"));
    rws::ParameterBinding qModelPhysicalLower = physicalLower;
    qModelPhysicalLower.jointLimitCoordinateConvention =
        rws::JointCoordinateConvention::QModel;
    const rws::AdapterPatchCompileResult qModelLimit = registry.compilePatch(
        {&qModelBaseline, &qModelPhysicalLower, physicalValues}, capabilities);
    REQUIRE(qModelLimit.ok);
    REQUIRE(std::fabs(rws::KinematicConventions::modelCoordinate(
                 0.0, qModelBaseline.joints[0].zeroPositionOffset) - 0.25) < 1e-12);
    rws::CanonicalKinematicModel nanLimitBaseline = baseline;
    nanLimitBaseline.joints[0].physicalLimits.lower =
        std::numeric_limits< double >::infinity();
    rws::JointLimitAdapter directBaselineValidation;
    const rws::AdapterPatchCompileResult nanLimitDirect = directBaselineValidation.compilePatch(
        {&nanLimitBaseline, &physicalLower, physicalValues});
    REQUIRE(!nanLimitDirect.ok);
    REQUIRE(hasAdapterDiagnostic(nanLimitDirect.diagnostics,
                                 "KINEMATIC_PHYSICAL_LIMITS_NONFINITE"));
    rws::CanonicalKinematicModel unitMismatchBaseline = baseline;
    unitMismatchBaseline.joints[0].operationalLimits = {
        true, -1.0, 1.0, rws::CanonicalCoordinateUnit::Metres,
        rws::JointCoordinateConvention::QInput};
    const rws::AdapterPatchCompileResult unitMismatchDirect = directBaselineValidation.compilePatch(
        {&unitMismatchBaseline, &physicalLower, physicalValues});
    REQUIRE(!unitMismatchDirect.ok);
    REQUIRE(hasAdapterDiagnostic(unitMismatchDirect.diagnostics,
                                 "KINEMATIC_OPERATIONAL_LIMITS_UNIT_MISMATCH"));
    rws::ParameterBinding lockedBinding = jointLimitBinding(
        "physical-locked", rws::SemanticKind::JointLimitLower, "joint-1", "base",
        rws::JointLimitScope::Physical);
    const rws::AdapterPatchCompileResult locked = registry.compilePatch(
        {&baseline, &lockedBinding,
         physicalValues}, capabilities);
    REQUIRE(!locked.ok);
    // Generic binding validation runs before adapter compilation, making the
    // physical-lock policy impossible to bypass through a direct registry call.
    REQUIRE(hasAdapterDiagnostic(locked.diagnostics,
                                 "PARAMETER_BINDING_JOINT_LIMIT_PHYSICAL_LOCKED"));

    rws::ParameterBinding operationalLower = jointLimitBinding(
        "operational-lower", rws::SemanticKind::JointLimitLower, "joint-1", "base",
        rws::JointLimitScope::Operational);
    const std::vector< rws::ResolvedAdapterValue > operationalValues = {
        {"operational-lower", rws::DesignVariableUnit::Radians, -1.0, "",
         rws::SemanticKind::JointLimitLower, "joint-limits:joint-1", rws::JointLimitScope::Operational},
        {"operational-upper", rws::DesignVariableUnit::Radians, 1.0, "",
         rws::SemanticKind::JointLimitUpper, "joint-limits:joint-1", rws::JointLimitScope::Operational}};
    const rws::AdapterPatchCompileResult operational = registry.compilePatch(
        {&baseline, &operationalLower, operationalValues}, capabilities);
    REQUIRE(operational.ok);
    REQUIRE(!operational.patch.affectsStructuralCapability);

    // Disabled canonical limits may retain Unknown, but a binding never may:
    // an unrecognized enum value must not pass merely because both sides hold
    // the same cast value.  Keep physical limits disabled too so this reaches
    // binding validation rather than the physical-envelope comparison.
    const rws::JointCoordinateConvention invalidConvention =
        static_cast< rws::JointCoordinateConvention >(77);
    rws::CanonicalKinematicModel disabledScopedBaseline = baseline;
    disabledScopedBaseline.joints[0].physicalLimits.enabled = false;
    disabledScopedBaseline.joints[0].operationalLimits = {
        false, -1.0, 1.0, rws::CanonicalCoordinateUnit::Radians, invalidConvention};
    rws::ParameterBinding invalidConventionBinding = operationalLower;
    invalidConventionBinding.jointLimitCoordinateConvention = invalidConvention;
    const rws::AdapterPatchCompileResult invalidConventionRegistry = registry.compilePatch(
        {&disabledScopedBaseline, &invalidConventionBinding, operationalValues}, capabilities);
    REQUIRE(!invalidConventionRegistry.ok);
    REQUIRE(hasAdapterDiagnostic(invalidConventionRegistry.diagnostics,
                                 "PARAMETER_BINDING_JOINT_LIMIT_COORDINATE_INVALID"));
    rws::JointLimitAdapter directInvalidConventionAdapter;
    const rws::AdapterPatchCompileResult invalidConventionDirect =
        directInvalidConventionAdapter.compilePatch(
            {&disabledScopedBaseline, &invalidConventionBinding, operationalValues});
    REQUIRE(!invalidConventionDirect.ok);
    REQUIRE(hasAdapterDiagnostic(invalidConventionDirect.diagnostics,
                                 "JOINT_LIMIT_COORDINATE_CONVENTION_INVALID"));

    // An operational q_model range must be compared to physical q_input
    // bounds after applying the frozen q_model = q_input + zeroOffset rule.
    // [-1, 1] in q_model with offset +.25 is really [-1.25, .75] in
    // q_input, and therefore exceeds the physical lower stop.
    rws::CanonicalKinematicModel crossConventionBaseline = baseline;
    crossConventionBaseline.joints[0].zeroPositionOffset = 0.25;
    crossConventionBaseline.joints[0].physicalLimits = {
        true, -1.0, 1.0, rws::CanonicalCoordinateUnit::Radians,
        rws::JointCoordinateConvention::QInput};
    crossConventionBaseline.joints[0].operationalLimits = {
        true, -1.0, 1.0, rws::CanonicalCoordinateUnit::Radians,
        rws::JointCoordinateConvention::QModel};
    rws::ParameterBinding qModelOperationalLower = operationalLower;
    qModelOperationalLower.jointLimitCoordinateConvention =
        rws::JointCoordinateConvention::QModel;
    const rws::AdapterPatchCompileResult crossConventionOutside = registry.compilePatch(
        {&crossConventionBaseline, &qModelOperationalLower, operationalValues}, capabilities);
    REQUIRE(!crossConventionOutside.ok);
    REQUIRE(hasAdapterDiagnostic(crossConventionOutside.diagnostics,
                                 "JOINT_LIMIT_OPERATIONAL_OUTSIDE_PHYSICAL"));
    rws::JointLimitAdapter directCrossConventionAdapter;
    const rws::AdapterPatchCompileResult directCrossConventionOutside =
        directCrossConventionAdapter.compilePatch(
            {&crossConventionBaseline, &qModelOperationalLower, operationalValues});
    REQUIRE(!directCrossConventionOutside.ok);
    REQUIRE(hasAdapterDiagnostic(directCrossConventionOutside.diagnostics,
                                 "JOINT_LIMIT_OPERATIONAL_OUTSIDE_PHYSICAL"));
    const std::vector< rws::ResolvedAdapterValue > convertedOperationalValues = {
        {"operational-lower", rws::DesignVariableUnit::Radians, -0.75, "",
         rws::SemanticKind::JointLimitLower, "joint-limits:joint-1", rws::JointLimitScope::Operational},
        {"operational-upper", rws::DesignVariableUnit::Radians, 1.25, "",
         rws::SemanticKind::JointLimitUpper, "joint-limits:joint-1", rws::JointLimitScope::Operational}};
    const rws::AdapterPatchCompileResult convertedOperational =
        directCrossConventionAdapter.compilePatch(
            {&crossConventionBaseline, &qModelOperationalLower, convertedOperationalValues});
    REQUIRE(convertedOperational.ok);
    REQUIRE(!convertedOperational.patch.affectsStructuralCapability);
    std::vector< rws::ResolvedAdapterValue > mixedScopeValues = operationalValues;
    mixedScopeValues.front().jointLimitScope = rws::JointLimitScope::Operational;
    mixedScopeValues.back().jointLimitScope = rws::JointLimitScope::Physical;
    const rws::AdapterPatchCompileResult mixedScope = registry.compilePatch(
        {&baseline, &operationalLower, mixedScopeValues}, capabilities);
    REQUIRE(!mixedScope.ok);
    REQUIRE(hasAdapterDiagnostic(mixedScope.diagnostics, "JOINT_LIMIT_GROUP_SCOPE_MISMATCH"));
    std::vector< rws::ResolvedAdapterValue > crossJointValues = operationalValues;
    crossJointValues.back().groupId = "joint-limits:joint-2";
    crossJointValues.front().jointLimitScope = rws::JointLimitScope::Operational;
    crossJointValues.back().jointLimitScope = rws::JointLimitScope::Operational;
    const rws::AdapterPatchCompileResult crossJoint = registry.compilePatch(
        {&baseline, &operationalLower, crossJointValues}, capabilities);
    REQUIRE(!crossJoint.ok);
    REQUIRE(hasAdapterDiagnostic(crossJoint.diagnostics, "JOINT_LIMIT_GROUP_MISMATCH"));
    const std::vector< rws::ResolvedAdapterValue > enlargedOperationalValues = {
        {"operational-lower", rws::DesignVariableUnit::Radians, -2.5, "",
         rws::SemanticKind::JointLimitLower, "joint-limits:joint-1", rws::JointLimitScope::Operational},
        {"operational-upper", rws::DesignVariableUnit::Radians, 1.0, "",
         rws::SemanticKind::JointLimitUpper, "joint-limits:joint-1", rws::JointLimitScope::Operational}};
    const rws::AdapterPatchCompileResult enlargedOperational = registry.compilePatch(
        {&baseline, &operationalLower, enlargedOperationalValues}, capabilities);
    REQUIRE(!enlargedOperational.ok);
    REQUIRE(hasAdapterDiagnostic(enlargedOperational.diagnostics,
                                 "JOINT_LIMIT_OPERATIONAL_OUTSIDE_PHYSICAL"));
    const rws::AdapterPatchCompileResult missingOperational = registry.compilePatch(
        {&baseline, &operationalLower, {operationalValues.front()}}, capabilities);
    REQUIRE(!missingOperational.ok);
    REQUIRE(hasAdapterDiagnostic(missingOperational.diagnostics,
                                 "JOINT_LIMIT_GROUP_VALUE_REQUIRED"));
    std::vector< rws::ResolvedAdapterValue > duplicateOperationalValues = operationalValues;
    duplicateOperationalValues.push_back(operationalValues.front());
    const rws::AdapterPatchCompileResult duplicateOperational = registry.compilePatch(
        {&baseline, &operationalLower, duplicateOperationalValues}, capabilities);
    REQUIRE(!duplicateOperational.ok);
    REQUIRE(hasAdapterDiagnostic(duplicateOperational.diagnostics,
                                 "JOINT_LIMIT_GROUP_VALUE_DUPLICATE"));
    rws::ParameterBinding incompleteBoundaryBinding = operationalLower;
    incompleteBoundaryBinding.absoluteJointLimitUpper = std::numeric_limits< double >::quiet_NaN();
    const rws::AdapterPatchCompileResult incompleteBoundary = registry.compilePatch(
        {&baseline, &incompleteBoundaryBinding, operationalValues}, capabilities);
    REQUIRE(!incompleteBoundary.ok);
    REQUIRE(hasAdapterDiagnostic(incompleteBoundary.diagnostics,
                                 "PARAMETER_BINDING_JOINT_LIMIT_ABSOLUTE_BOUNDS_INVALID"));
    // Even before S36, adapter callers cannot bypass the binding contract by
    // calling a concrete adapter directly instead of through the registry.
    rws::ParameterBinding unscopedBinding = operationalLower;
    unscopedBinding.jointLimitScope = rws::JointLimitScope::Unknown;
    rws::JointLimitAdapter directLimitAdapter;
    const rws::AdapterPatchCompileResult unscopedDirect = directLimitAdapter.compilePatch(
        {&baseline, &unscopedBinding, operationalValues});
    REQUIRE(!unscopedDirect.ok);
    REQUIRE(hasAdapterDiagnostic(unscopedDirect.diagnostics, "JOINT_LIMIT_SCOPE_REQUIRED"));

    const std::vector< rws::ResolvedAdapterValue > crossedValues = {
        {"physical-lower", rws::DesignVariableUnit::Radians, 1.0, "",
         rws::SemanticKind::JointLimitLower, "joint-limits:joint-1", rws::JointLimitScope::Physical},
        {"physical-upper", rws::DesignVariableUnit::Radians, 1.0, "",
         rws::SemanticKind::JointLimitUpper, "joint-limits:joint-1", rws::JointLimitScope::Physical}};
    const rws::AdapterPatchCompileResult crossed = registry.compilePatch(
        {&baseline, &physicalLower, crossedValues}, capabilities);
    REQUIRE(!crossed.ok);
    REQUIRE(hasAdapterDiagnostic(crossed.diagnostics, "JOINT_LIMIT_RANGE_ORDER_INVALID"));
    const std::vector< rws::ResolvedAdapterValue > narrowValues = {
        {"physical-lower", rws::DesignVariableUnit::Radians, -0.01, "",
         rws::SemanticKind::JointLimitLower, "joint-limits:joint-1", rws::JointLimitScope::Physical},
        {"physical-upper", rws::DesignVariableUnit::Radians, 0.01, "",
         rws::SemanticKind::JointLimitUpper, "joint-limits:joint-1", rws::JointLimitScope::Physical}};
    const rws::AdapterPatchCompileResult narrow = registry.compilePatch(
        {&baseline, &physicalLower, narrowValues}, capabilities);
    REQUIRE(!narrow.ok);
    REQUIRE(hasAdapterDiagnostic(narrow.diagnostics, "JOINT_LIMIT_MINIMUM_RANGE_INVALID"));

    const rws::ParameterBinding fixedLimit = jointLimitBinding(
        "fixed-limit", rws::SemanticKind::JointLimitLower, "fixed-flange", "link",
        rws::JointLimitScope::Operational);
    const rws::AdapterPatchCompileResult fixedLimitResult = registry.compilePatch(
        {&baseline, &fixedLimit,
         {{"fixed-limit", rws::DesignVariableUnit::Radians, -1.0, "",
           rws::SemanticKind::JointLimitLower, "joint-limits:fixed-flange", rws::JointLimitScope::Operational},
          {"fixed-limit-upper", rws::DesignVariableUnit::Radians, 1.0, "",
           rws::SemanticKind::JointLimitUpper, "joint-limits:fixed-flange", rws::JointLimitScope::Operational}}}, capabilities);
    REQUIRE(!fixedLimitResult.ok);
    REQUIRE(hasAdapterDiagnostic(fixedLimitResult.diagnostics,
                                 "JOINT_LIMIT_MOVABLE_JOINT_REQUIRED"));

    // Templates never auto-open physical range variables; projects must opt
    // in with both scoped boundary bindings.
    const rws::DesignIntentTemplateInfo* full = rws::StructureOptimizationTemplate::designIntent(
        rws::DesignIntentTemplateKind::FullKinematicDesign);
    REQUIRE(full != nullptr);
    REQUIRE(std::find(full->semanticKinds.begin(), full->semanticKinds.end(),
                      rws::SemanticKind::JointZeroOffset) == full->semanticKinds.end());
    REQUIRE(std::find(full->semanticKinds.begin(), full->semanticKinds.end(),
                      rws::SemanticKind::JointLimitLower) == full->semanticKinds.end());
    REQUIRE(std::find(full->semanticKinds.begin(), full->semanticKinds.end(),
                      rws::SemanticKind::JointLimitUpper) == full->semanticKinds.end());

    // Target-joint coordinate semantics override the registry's generic radian
    // metadata: zero and limit variables for prismatic joints compile in metres.
    rws::DesignVariableDefinition prismaticZeroVariable;
    prismaticZeroVariable.id = "zero-prismatic-variable";
    prismaticZeroVariable.semanticKind = rws::SemanticKind::JointZeroOffset;
    prismaticZeroVariable.role = rws::VariableRole::Independent;
    prismaticZeroVariable.domain = rws::VariableDomain::Continuous;
    prismaticZeroVariable.minimum = -0.2;
    prismaticZeroVariable.maximum = 0.2;
    prismaticZeroVariable.step = 0.01;
    prismaticZeroVariable.unit = rws::DesignVariableUnit::Metres;
    prismaticZeroVariable.frameId = "guide";
    prismaticZeroVariable.bindingId = prismaticZero.id;
    rws::DesignVariableDefinition prismaticLowerVariable = prismaticZeroVariable;
    prismaticLowerVariable.id = "limit-prismatic-lower";
    prismaticLowerVariable.semanticKind = rws::SemanticKind::JointLimitLower;
    prismaticLowerVariable.bindingId = "limit-prismatic-lower-binding";
    prismaticLowerVariable.groupId = "joint-limits:joint-2";
    prismaticLowerVariable.minimum = -1.5;
    prismaticLowerVariable.maximum = -0.1;
    prismaticLowerVariable.nominalValue = -1.0;
    prismaticLowerVariable.currentValue = -1.0;
    rws::DesignVariableDefinition prismaticUpperVariable = prismaticLowerVariable;
    prismaticUpperVariable.id = "limit-prismatic-upper";
    prismaticUpperVariable.semanticKind = rws::SemanticKind::JointLimitUpper;
    prismaticUpperVariable.bindingId = "limit-prismatic-upper-binding";
    prismaticUpperVariable.minimum = 0.1;
    prismaticUpperVariable.maximum = 1.5;
    prismaticUpperVariable.nominalValue = 1.0;
    prismaticUpperVariable.currentValue = 1.0;
    rws::ParameterBinding prismaticLowerBinding = jointLimitBinding(
        prismaticLowerVariable.bindingId, rws::SemanticKind::JointLimitLower, "joint-2", "guide",
        rws::JointLimitScope::Physical);
    rws::ParameterBinding prismaticUpperBinding = jointLimitBinding(
        prismaticUpperVariable.bindingId, rws::SemanticKind::JointLimitUpper, "joint-2", "guide",
        rws::JointLimitScope::Physical);
    prismaticLowerBinding.allowPhysicalLimitModification = true;
    prismaticUpperBinding.allowPhysicalLimitModification = true;
    rws::AdapterRegistry compilerRegistry;
    REQUIRE(compilerRegistry.registerAdapter(std::make_shared< rws::JointZeroAdapter >()).ok);
    REQUIRE(compilerRegistry.registerAdapter(std::make_shared< rws::JointLimitAdapter >()).ok);
    const rws::DesignSpaceRegistry semanticRegistry = rws::DesignSpaceRegistry::firstPhase();
    rws::DesignSpaceCompileRequest compilerRequest;
    compilerRequest.model = &baseline;
    compilerRequest.registry = &semanticRegistry;
    compilerRequest.capabilities = &capabilities;
    compilerRequest.adapterRegistry = &compilerRegistry;
    compilerRequest.variables = {prismaticZeroVariable, prismaticLowerVariable,
                                 prismaticUpperVariable};
    compilerRequest.bindings = {prismaticZero, prismaticLowerBinding, prismaticUpperBinding};
    const rws::DesignSpaceCompileResult prismaticCompiled =
        rws::DesignSpaceCompiler::compile(compilerRequest);
    REQUIRE(prismaticCompiled.ok);
    rws::DesignSpaceCompileRequest wrongPrismaticUnit = compilerRequest;
    wrongPrismaticUnit.variables.front().unit = rws::DesignVariableUnit::Radians;
    const rws::DesignSpaceCompileResult rejectedPrismaticUnit =
        rws::DesignSpaceCompiler::compile(wrongPrismaticUnit);
    REQUIRE(!rejectedPrismaticUnit.ok);
    REQUIRE(hasAdapterDiagnostic(rejectedPrismaticUnit.diagnostics,
                                 "DESIGN_SPACE_VARIABLE_UNIT_MISMATCH"));
    REQUIRE(std::fabs(baseline.joints[0].zeroPositionOffset - sourceCopy.joints[0].zeroPositionOffset) < 1e-12);
    REQUIRE((baseline.joints[0].motionAxisInJoint - sourceCopy.joints[0].motionAxisInJoint).norm2() < 1e-12);
}

static QString sourcePath(const QString& relativePath)
{
    return QDir(QStringLiteral(STRUCTUREOPTIMIZER_TEST_SOURCE_DIR)).filePath(relativePath);
}

// Phase 3/S34: placements remain typed data-only Patches.  S36 owns generic
// Patch application, while this suite freezes the declared coordinate frame
// and right-multiplied SO(3) delta convention used by all pose adapters.
static rws::CanonicalKinematicModel independentFlangeFixture()
{
    rws::CanonicalKinematicModel model = validCanonicalModelFixture();
    model.frames.push_back({"arm-tip", "Arm tip", rws::CanonicalFrameType::Link});
    model.joints[2].childFrameId = "arm-tip";
    rws::JointEdge flangeMount;
    flangeMount.id = "flange-mount";
    flangeMount.name = "Independent flange mount";
    flangeMount.type = rws::CanonicalJointType::Fixed;
    flangeMount.parentFrameId = "arm-tip";
    flangeMount.childFrameId = "flange";
    flangeMount.parentToJointZero = rw::math::Transform3D<>(
        rw::math::Vector3D<>(0.3, -0.2, 0.1),
        rw::math::RPY<>(0.0, 0.0, rw::math::Pi / 2.0).toRotation3D());
    model.joints.push_back(flangeMount);
    model.deviceChains[0].orderedJointIds.push_back(flangeMount.id);
    model.toolBindings[0].flangeToTcp = rw::math::Transform3D<>(
        rw::math::Vector3D<>(0.2, 0.0, 0.0), rw::math::Rotation3D<>());
    return model;
}

static rws::ParameterBinding poseBinding(const std::string& id, rws::SemanticKind semantic,
                                         const std::string& adapterId,
                                         rws::TargetObjectType targetType,
                                         const std::string& targetId,
                                         rws::TargetPropertyId property,
                                         const std::string& coordinateFrame,
                                         const std::string& poseGroup)
{
    rws::ParameterBinding binding;
    binding.id = id;
    binding.semanticKind = semantic;
    binding.targetObjectType = targetType;
    binding.targetObjectId = targetId;
    binding.targetPropertyId = property;
    binding.coordinateFrameId = coordinateFrame;
    binding.ownerAdapterId = adapterId;
    binding.ownerAdapterVersion = 1;
    binding.poseDeltaGroupId = poseGroup;
    binding.poseDeltaComposition = rws::PoseDeltaComposition::Right;
    const rws::ReadWriteTarget target = {targetType, targetId, property, coordinateFrame};
    binding.readSet = {target};
    binding.writeSet = {target};
    return binding;
}

static void testParameterizedGeometryAndCollisionAdapters()
{
    rws::CanonicalKinematicModel baseline = validCanonicalModelFixture();
    rws::GeometryBinding visual;
    visual.id = "visual-cylinder";
    visual.referenceFrameId = "base";
    visual.kind = rws::CanonicalGeometryKind::Cylinder;
    visual.optimizationOwned = true;
    visual.radius = 0.10;
    visual.length = 0.60;
    baseline.geometryBindings.push_back(visual);
    rws::CollisionBinding collision;
    collision.id = "collision-box";
    collision.referenceFrameId = "base";
    collision.kind = rws::CanonicalGeometryKind::Box;
    collision.optimizationOwned = true;
    collision.width = 0.20;
    collision.height = 0.30;
    collision.depth = 0.40;
    baseline.collisionBindings.push_back(collision);
    rws::GeometryBinding tube;
    tube.id = "visual-tube";
    tube.referenceFrameId = "base";
    tube.kind = rws::CanonicalGeometryKind::Tube;
    tube.optimizationOwned = true;
    tube.radius = 0.20;
    tube.length = 0.50;
    tube.wallThickness = 0.05;
    baseline.geometryBindings.push_back(tube);
    rws::GeometryBinding mesh;
    mesh.id = "owned-mesh";
    mesh.referenceFrameId = "base";
    mesh.kind = rws::CanonicalGeometryKind::Mesh;
    mesh.optimizationOwned = true;
    mesh.allowRigidTransform = true;
    baseline.geometryBindings.push_back(mesh);
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(baseline).valid);

    const auto geometryBinding = [] (const std::string& id, const rws::SemanticKind semantic,
                                     const rws::TargetObjectType type, const std::string& target,
                                     const rws::TargetPropertyId property, const std::string& owner,
                                     const std::string& group) {
        rws::ParameterBinding binding;
        binding.id = id; binding.semanticKind = semantic; binding.targetObjectType = type;
        binding.targetObjectId = target; binding.targetPropertyId = property;
        binding.coordinateFrameId = "base"; binding.ownerAdapterId = owner;
        binding.ownerAdapterVersion = 1; binding.geometryGroupId = group;
        const rws::ReadWriteTarget rw = {type, target, property, "base"};
        binding.readSet = {rw}; binding.writeSet = {rw};
        return binding;
    };

    rws::AdapterRegistry registry;
    REQUIRE(registry.registerAdapter(std::make_shared< rws::ParameterizedGeometryAdapter >()).ok);
    REQUIRE(registry.registerAdapter(std::make_shared< rws::ParameterizedCollisionAdapter >()).ok);
    REQUIRE(registry.registerAdapter(std::make_shared< rws::MeshTransformAdapter >()).ok);
    rws::AdapterCapabilityQuery capabilities;
    capabilities.grant(rws::TargetObjectType::Geometry, "visual-cylinder",
                       rws::AdapterCapability::ParameterizedGeometry);
    capabilities.grant(rws::TargetObjectType::Geometry, "visual-tube",
                       rws::AdapterCapability::ParameterizedGeometry);
    capabilities.grant(rws::TargetObjectType::CollisionGeometry, "collision-box",
                       rws::AdapterCapability::ParameterizedCollision);
    const rws::ParameterBinding radius = geometryBinding(
        "visual-radius", rws::SemanticKind::GeometryRadius, rws::TargetObjectType::Geometry,
        "visual-cylinder", rws::TargetPropertyId::GeometryRadius, "ParameterizedGeometryAdapter",
        "geometry:visual:visual-cylinder");
    const rws::AdapterPatchCompileResult visualPatch = registry.compilePatch(
        {&baseline, &radius, {{"visual-radius", rws::DesignVariableUnit::Metres, 0.15, "",
                               rws::SemanticKind::GeometryRadius, "geometry:visual:visual-cylinder"}}},
        capabilities);
    REQUIRE(visualPatch.ok);
    REQUIRE(visualPatch.patch.generatedArtifacts.size() == 1);
    REQUIRE(visualPatch.patch.generatedArtifacts.front().find("geometry-artifact-v1:") == 0);
    REQUIRE(std::fabs(visualPatch.patch.writes.front().value.scalarValue - 0.15) < 1e-12);
    rws::DesignVariableDefinition radiusVariable;
    radiusVariable.id = "visual-radius-variable";
    radiusVariable.semanticKind = rws::SemanticKind::GeometryRadius;
    radiusVariable.role = rws::VariableRole::Independent;
    radiusVariable.groupId = radius.geometryGroupId;
    radiusVariable.nominalValue = 0.10;
    radiusVariable.currentValue = 0.15;
    radiusVariable.domain = rws::VariableDomain::Continuous;
    radiusVariable.minimum = 0.01;
    radiusVariable.maximum = 0.50;
    radiusVariable.step = 0.01;
    radiusVariable.unit = rws::DesignVariableUnit::Metres;
    radiusVariable.frameId = "base";
    radiusVariable.bindingId = radius.id;
    const rws::DesignSpaceRegistry semanticRegistry = rws::DesignSpaceRegistry::firstPhase();
    rws::DesignSpaceCompileRequest visualDesignRequest;
    visualDesignRequest.model = &baseline;
    visualDesignRequest.registry = &semanticRegistry;
    visualDesignRequest.capabilities = &capabilities;
    visualDesignRequest.adapterRegistry = &registry;
    visualDesignRequest.variables = {radiusVariable};
    visualDesignRequest.bindings = {radius};
    const rws::DesignSpaceCompileResult visualDesign =
        rws::DesignSpaceCompiler::compile(visualDesignRequest);
    REQUIRE(visualDesign.ok);
    REQUIRE(!visualDesign.designSpace.fingerprint.empty());
    const rws::ParameterBinding cylinderLength = geometryBinding(
        "visual-length", rws::SemanticKind::GeometryLength, rws::TargetObjectType::Geometry,
        "visual-cylinder", rws::TargetPropertyId::GeometryLength, "ParameterizedGeometryAdapter",
        "geometry:visual:visual-cylinder");
    const rws::AdapterPatchCompileResult lengthPatch = registry.compilePatch(
        {&baseline, &cylinderLength, {{"visual-length", rws::DesignVariableUnit::Metres, 0.75, "",
                                       rws::SemanticKind::GeometryLength, "geometry:visual:visual-cylinder"}}},
        capabilities);
    REQUIRE(lengthPatch.ok);
    REQUIRE(lengthPatch.patch.generatedArtifacts != visualPatch.patch.generatedArtifacts);
    rws::CanonicalKinematicModel movedVisual = baseline;
    movedVisual.geometryBindings[0].referenceToGeometry =
        rw::math::Transform3D<>(rw::math::Vector3D<>(0.01, 0.0, 0.0));
    const rws::AdapterPatchCompileResult movedVisualPatch = registry.compilePatch(
        {&movedVisual, &radius, {{"visual-radius", rws::DesignVariableUnit::Metres, 0.15, "",
                                  rws::SemanticKind::GeometryRadius,
                                  "geometry:visual:visual-cylinder"}}}, capabilities);
    REQUIRE(movedVisualPatch.ok);
    REQUIRE(movedVisualPatch.patch.generatedArtifacts != visualPatch.patch.generatedArtifacts);

    const rws::ParameterBinding boxWidth = geometryBinding(
        "collision-width", rws::SemanticKind::GeometryWidth, rws::TargetObjectType::CollisionGeometry,
        "collision-box", rws::TargetPropertyId::GeometryWidth, "ParameterizedCollisionAdapter",
        "geometry:collision:collision-box");
    const rws::AdapterPatchCompileResult collisionPatch = registry.compilePatch(
        {&baseline, &boxWidth, {{"collision-width", rws::DesignVariableUnit::Metres, 0.25, "",
                                 rws::SemanticKind::GeometryWidth, "geometry:collision:collision-box"}}},
        capabilities);
    REQUIRE(collisionPatch.ok);
    REQUIRE(collisionPatch.patch.generatedArtifacts.size() == 1);
    REQUIRE(collisionPatch.patch.generatedArtifacts.front().find("collision-artifact-v1:") == 0);
    REQUIRE(collisionPatch.patch.generatedArtifacts != visualPatch.patch.generatedArtifacts);
    for (const std::pair< rws::SemanticKind, rws::TargetPropertyId >& dimension :
         std::vector< std::pair< rws::SemanticKind, rws::TargetPropertyId > >{
             {rws::SemanticKind::GeometryHeight, rws::TargetPropertyId::GeometryHeight},
             {rws::SemanticKind::GeometryDepth, rws::TargetPropertyId::GeometryDepth}}) {
        const rws::ParameterBinding boxDimension = geometryBinding(
            "collision-box-dimension", dimension.first, rws::TargetObjectType::CollisionGeometry,
            "collision-box", dimension.second, "ParameterizedCollisionAdapter",
            "geometry:collision:collision-box");
        const rws::AdapterPatchCompileResult boxPatch = registry.compilePatch(
            {&baseline, &boxDimension, {{"collision-box-dimension", rws::DesignVariableUnit::Metres,
                                          0.35, "", dimension.first,
                                          "geometry:collision:collision-box"}}}, capabilities);
        REQUIRE(boxPatch.ok);
    }

    const rws::ParameterBinding tubeWall = geometryBinding(
        "tube-wall", rws::SemanticKind::GeometryWallThickness, rws::TargetObjectType::Geometry,
        "visual-tube", rws::TargetPropertyId::GeometryWallThickness, "ParameterizedGeometryAdapter",
        "geometry:visual:visual-tube");
    const rws::AdapterPatchCompileResult validWall = registry.compilePatch(
        {&baseline, &tubeWall, {{"tube-wall", rws::DesignVariableUnit::Metres, 0.10, "",
                                 rws::SemanticKind::GeometryWallThickness, "geometry:visual:visual-tube"}}},
        capabilities);
    REQUIRE(validWall.ok);
    const rws::AdapterPatchCompileResult invalidWall = registry.compilePatch(
        {&baseline, &tubeWall, {{"tube-wall", rws::DesignVariableUnit::Metres, 0.20, "",
                                 rws::SemanticKind::GeometryWallThickness, "geometry:visual:visual-tube"}}},
        capabilities);
    REQUIRE(!invalidWall.ok);

    const rw::math::Transform3D<> up = rws::ParameterizedGeometryAdapter::segmentTransform(
        rw::math::Vector3D<>(0, 0, 0), rw::math::Vector3D<>(0, 0, 1));
    const rw::math::Transform3D<> down = rws::ParameterizedGeometryAdapter::segmentTransform(
        rw::math::Vector3D<>(0, 0, 0), rw::math::Vector3D<>(0, 0, -1));
    const rw::math::Transform3D<> slanted = rws::ParameterizedGeometryAdapter::segmentTransform(
        rw::math::Vector3D<>(1, 2, 3), rw::math::Vector3D<>(2, 4, 6));
    REQUIRE((up.R() * rw::math::Vector3D<>::z() - rw::math::Vector3D<>::z()).norm2() < 1e-12);
    REQUIRE((down.R() * rw::math::Vector3D<>::z() + rw::math::Vector3D<>::z()).norm2() < 1e-12);
    REQUIRE(std::fabs(down.R()(0, 0) - 1.0) < 1e-12); // not a spurious Z-axis rotation
    const rw::math::Vector3D<> actualSlanted = slanted.R() * rw::math::Vector3D<>::z();
    const rw::math::Vector3D<> expectedSlanted = rw::math::Vector3D<>(1, 2, 3) /
                                                 rw::math::Vector3D<>(1, 2, 3).norm2();
    REQUIRE((actualSlanted - expectedSlanted).norm2() < 1e-12);

    const rws::ParameterBinding meshTransform = geometryBinding(
        "mesh-transform", rws::SemanticKind::GeometryRigidTransform, rws::TargetObjectType::Geometry,
        "owned-mesh", rws::TargetPropertyId::GeometryRigidTransform, "MeshTransformAdapter",
        "geometry:mesh:owned-mesh");
    const rws::AdapterPatchCompileResult noMeshCapability = registry.compilePatch(
        {&baseline, &meshTransform, {{"mesh-transform", rws::DesignVariableUnit::Unitless, 1.0, "",
                                      rws::SemanticKind::GeometryRigidTransform, "geometry:mesh:owned-mesh"}}},
        capabilities);
    REQUIRE(!noMeshCapability.ok);
    capabilities.grant(rws::TargetObjectType::Geometry, "owned-mesh",
                       rws::AdapterCapability::ParameterizedGeometry);
    const rws::AdapterPatchCompileResult allowedMeshTransform = registry.compilePatch(
        {&baseline, &meshTransform, {{"mesh-transform", rws::DesignVariableUnit::Unitless, 1.0, "",
                                      rws::SemanticKind::GeometryRigidTransform, "geometry:mesh:owned-mesh"}}},
        capabilities);
    REQUIRE(allowedMeshTransform.ok);

    rws::CanonicalKinematicModel manualGeometry = baseline;
    manualGeometry.geometryBindings[0].optimizationOwned = false;
    const rws::AdapterPatchCompileResult manualRejected = registry.compilePatch(
        {&manualGeometry, &radius, {{"visual-radius", rws::DesignVariableUnit::Metres, 0.15, "",
                                     rws::SemanticKind::GeometryRadius, "geometry:visual:visual-cylinder"}}},
        capabilities);
    REQUIRE(!manualRejected.ok);
    REQUIRE(hasAdapterDiagnostic(manualRejected.diagnostics, "PARAMETERIZED_GEOMETRY_OWNER_REQUIRED"));

    rws::CanonicalKinematicModel invalidCollisionReference = baseline;
    invalidCollisionReference.collisionBindings[0].referenceFrameId = "missing-frame";
    const rws::AdapterPatchCompileResult invalidCollision = registry.compilePatch(
        {&invalidCollisionReference, &boxWidth, {{"collision-width", rws::DesignVariableUnit::Metres,
                                                  0.25, "", rws::SemanticKind::GeometryWidth,
                                                  "geometry:collision:collision-box"}}}, capabilities);
    REQUIRE(!invalidCollision.ok);
    REQUIRE(hasAdapterDiagnostic(invalidCollision.diagnostics,
                                 "PARAMETERIZED_COLLISION_REF_FRAME_INVALID"));
}

// Phase 3/S36: patch merging and application are pure canonical-model
// operations.  The baseline must remain unchanged and conflicts must fail
// before any candidate model is returned.
static void testCandidatePatchMergeAndApply()
{
    const rws::CandidatePatchMergeResult emptyMerge =
        rws::CandidatePatchMerger::merge({});
    REQUIRE(emptyMerge.ok);
    const rws::CanonicalKinematicModel emptyBaseline = validCanonicalModelFixture();
    const rws::CandidatePatchApplyResult emptyApply =
        rws::CandidatePatchApplier::apply(emptyBaseline, emptyMerge.patch);
    REQUIRE(emptyApply.ok);
    REQUIRE(emptyApply.model.frames.size() == emptyBaseline.frames.size());

    const rws::ReadWriteTarget target = {
        rws::TargetObjectType::Joint, "joint-1",
        rws::TargetPropertyId::ParentToJointTranslationX, "base"};

    rws::CandidatePatch first;
    first.adapterId = "JointOriginAdapter";
    first.adapterVersion = 1;
    first.bindingId = "origin-x";
    first.writes = {{target, rws::CandidatePatchValue::scalar(1.25)}};
    first.generatedArtifacts = {"artifact-a", "artifact-a"};

    rws::CandidatePatch identical = first;
    identical.bindingId = "origin-x-copy";
    identical.generatedArtifacts.push_back("artifact-b");

    const rws::CandidatePatchMergeResult merged =
        rws::CandidatePatchMerger::merge({first, identical});
    REQUIRE(merged.ok);
    REQUIRE(merged.patch.writes.size() == 1);
    REQUIRE(merged.patch.writes.front().target == target);
    REQUIRE(std::fabs(merged.patch.writes.front().value.scalarValue - 1.25) < 1e-12);
    REQUIRE(merged.patch.generatedArtifacts.size() == 2);
    REQUIRE(merged.patch.generatedArtifacts[0] == "artifact-a");
    REQUIRE(merged.patch.generatedArtifacts[1] == "artifact-b");
    REQUIRE(merged.patch.diagnostics.empty());

    rws::CandidatePatch reverseArtifacts = first;
    reverseArtifacts.bindingId = "origin-x-reverse";
    reverseArtifacts.generatedArtifacts = {"artifact-c", "artifact-a"};
    reverseArtifacts.derivedValueIds = {"derived-c", "derived-a"};
    rws::CandidatePatch derivedArtifacts = first;
    derivedArtifacts.bindingId = "origin-x-derived";
    derivedArtifacts.generatedArtifacts = {"artifact-b"};
    derivedArtifacts.derivedValueIds = {"derived-b", "derived-a"};
    const rws::CandidatePatchMergeResult sortedArtifacts =
        rws::CandidatePatchMerger::merge({reverseArtifacts, derivedArtifacts});
    REQUIRE(sortedArtifacts.ok);
    REQUIRE(sortedArtifacts.patch.generatedArtifacts ==
            std::vector< std::string >({"artifact-a", "artifact-b", "artifact-c"}));
    REQUIRE(sortedArtifacts.patch.derivedValueIds ==
            std::vector< std::string >({"derived-a", "derived-b", "derived-c"}));

    rws::CandidatePatch poseA = first;
    poseA.bindingId = "pose-a";
    poseA.poseDeltaComposition = rws::PoseDeltaComposition::Right;
    poseA.poseDeltaGroupId = "pose:a";
    rws::CandidatePatch poseB = poseA;
    poseB.bindingId = "pose-b";
    poseB.poseDeltaGroupId = "pose:b";
    const rws::CandidatePatchMergeResult poseConflict =
        rws::CandidatePatchMerger::merge({poseA, poseB});
    REQUIRE(!poseConflict.ok);
    REQUIRE(hasAdapterDiagnostic(poseConflict.diagnostics,
                                 "CANDIDATE_PATCH_POSE_GROUP_CONFLICT"));
    REQUIRE(poseConflict.patch.diagnostics.size() == poseConflict.diagnostics.size());

    rws::CandidatePatch conflicting = first;
    conflicting.bindingId = "origin-x-conflict";
    conflicting.writes.front().value = rws::CandidatePatchValue::scalar(1.5);
    const rws::CandidatePatchMergeResult conflict =
        rws::CandidatePatchMerger::merge({first, conflicting});
    REQUIRE(!conflict.ok);
    REQUIRE(hasAdapterDiagnostic(conflict.diagnostics,
                                 "CANDIDATE_PATCH_WRITE_CONFLICT"));

    rws::CanonicalKinematicModel baseline = validCanonicalModelFixture();
    baseline.joints[0].parentToJointZero =
        rw::math::Transform3D<>(rw::math::Vector3D<>(1.0, 2.0, 3.0));
    const rws::CanonicalKinematicModel baselineCopy = baseline;
    const rws::CandidatePatchApplyResult applied =
        rws::CandidatePatchApplier::apply(baseline, merged.patch);
    REQUIRE(applied.ok);
    REQUIRE(std::fabs(applied.model.joints[0].parentToJointZero.P()(0) - 1.25) < 1e-12);
    REQUIRE(std::fabs(applied.model.joints[0].parentToJointZero.P()(1) - 2.0) < 1e-12);
    REQUIRE(sameTransform(baseline.joints[0].parentToJointZero,
                          baselineCopy.joints[0].parentToJointZero));

    rws::CandidatePatch invalid = merged.patch;
    invalid.writes.front().target = {
        rws::TargetObjectType::Joint, "missing-joint",
        rws::TargetPropertyId::ParentToJointTranslationX, "base"};
    const rws::CandidatePatchApplyResult rejected =
        rws::CandidatePatchApplier::apply(baseline, invalid);
    REQUIRE(!rejected.ok);
    REQUIRE(rejected.model.frames.empty());
    REQUIRE(hasAdapterDiagnostic(rejected.diagnostics,
                                 "CANDIDATE_PATCH_TARGET_NOT_FOUND"));

    rws::CanonicalKinematicModel axisBaseline = validCanonicalModelFixture();
    const rws::ReadWriteTarget axisUTarget = {
        rws::TargetObjectType::Joint, "joint-1",
        rws::TargetPropertyId::MotionAxisTiltU, "base"};
    const rws::ReadWriteTarget axisVTarget = {
        rws::TargetObjectType::Joint, "joint-1",
        rws::TargetPropertyId::MotionAxisTiltV, "base"};
    rws::CandidatePatch axisU;
    axisU.adapterId = "JointAxisAdapter";
    axisU.adapterVersion = 1;
    axisU.bindingId = "axis-u";
    axisU.writes = {{axisUTarget, rws::CandidatePatchValue::scalar(0.1)}};
    rws::CandidatePatch axisV = axisU;
    axisV.bindingId = "axis-v";
    axisV.writes = {{axisVTarget, rws::CandidatePatchValue::scalar(-0.2)}};
    const rws::CandidatePatchMergeResult mergedAxis =
        rws::CandidatePatchMerger::merge({axisU, axisV});
    REQUIRE(mergedAxis.ok);
    const rws::CandidatePatchApplyResult axisApply =
        rws::CandidatePatchApplier::apply(axisBaseline, mergedAxis.patch);
    REQUIRE(axisApply.ok);
    REQUIRE(std::fabs(axisApply.model.joints[0].motionAxisInJoint.norm2() - 1.0) < 1e-12);
    REQUIRE(std::fabs((axisApply.model.joints[0].motionAxisInJoint -
                       axisBaseline.joints[0].motionAxisInJoint).norm2()) > 1e-6);

    const rws::CandidatePatchApplyResult axisOnlyUApply =
        rws::CandidatePatchApplier::apply(axisBaseline, axisU);
    REQUIRE(!axisOnlyUApply.ok);
    REQUIRE(axisOnlyUApply.model.frames.empty());
    REQUIRE(hasAdapterDiagnostic(axisOnlyUApply.diagnostics,
                                 "CANDIDATE_PATCH_AXIS_SIBLING_REQUIRED"));

    rws::GeometryBinding geometry;
    geometry.id = "visual-cylinder";
    geometry.referenceFrameId = "base";
    geometry.kind = rws::CanonicalGeometryKind::Cylinder;
    geometry.optimizationOwned = true;
    geometry.radius = 0.10;
    geometry.length = 0.50;
    axisBaseline.geometryBindings.push_back(geometry);
    const rws::ReadWriteTarget geometryTarget = {
        rws::TargetObjectType::Geometry, "visual-cylinder",
        rws::TargetPropertyId::GeometryRadius, "base"};
    rws::CandidatePatch geometryPatch;
    geometryPatch.adapterId = "ParameterizedGeometryAdapter";
    geometryPatch.adapterVersion = 1;
    geometryPatch.bindingId = "visual-radius";
    geometryPatch.writes = {{geometryTarget, rws::CandidatePatchValue::scalar(0.25)}};
    const rws::CandidatePatchApplyResult geometryApply =
        rws::CandidatePatchApplier::apply(axisBaseline, geometryPatch);
    REQUIRE(geometryApply.ok);
    REQUIRE(std::fabs(geometryApply.model.geometryBindings.front().radius - 0.25) < 1e-12);

    rws::CanonicalKinematicModel poseBaseline = independentFlangeFixture();
    const rw::math::Vector3D<> mountTranslation =
        poseBaseline.joints.back().parentToJointZero.P();
    rws::CandidatePatch flangePatch;
    flangePatch.adapterId = "FlangePoseAdapter";
    flangePatch.adapterVersion = 1;
    flangePatch.bindingId = "flange-x";
    flangePatch.poseDeltaComposition = rws::PoseDeltaComposition::Right;
    flangePatch.poseDeltaGroupId = "flange-pose:flange";
    flangePatch.writes = {rws::CandidatePatchWrite{
        {rws::TargetObjectType::Frame, "flange",
         rws::TargetPropertyId::ParentToFlangeTranslationX, "arm-tip"},
        rws::CandidatePatchValue::scalar(mountTranslation(0) + 0.1)}};
    const rws::CandidatePatchApplyResult flangeApply =
        rws::CandidatePatchApplier::apply(poseBaseline, flangePatch);
    REQUIRE(flangeApply.ok);
    REQUIRE(std::fabs(flangeApply.model.joints.back().parentToJointZero.P()(0) -
                      mountTranslation(0) - 0.1) < 1e-12);

    rws::CandidatePatch tcpPatch;
    tcpPatch.adapterId = "TcpPoseAdapter";
    tcpPatch.adapterVersion = 1;
    tcpPatch.bindingId = "tcp-x";
    tcpPatch.poseDeltaComposition = rws::PoseDeltaComposition::Right;
    tcpPatch.poseDeltaGroupId = "tcp-pose:tool";
    tcpPatch.writes = {rws::CandidatePatchWrite{
        {rws::TargetObjectType::ToolBinding, "tool",
         rws::TargetPropertyId::FlangeToTcpTranslationX, "flange"},
        rws::CandidatePatchValue::scalar(0.4)}};
    const rws::CandidatePatchApplyResult tcpApply =
        rws::CandidatePatchApplier::apply(poseBaseline, tcpPatch);
    REQUIRE(tcpApply.ok);
    REQUIRE(std::fabs(tcpApply.model.toolBindings.front().flangeToTcp.P()(0) - 0.4) < 1e-12);

    rws::CanonicalKinematicModel limitBaseline = validCanonicalModelFixture();
    limitBaseline.joints[0].physicalLimits = {
        true, -1.0, 1.0, rws::CanonicalCoordinateUnit::Radians,
        rws::JointCoordinateConvention::QInput};
    limitBaseline.joints[0].operationalLimits = {
        true, -0.8, 0.8, rws::CanonicalCoordinateUnit::Radians,
        rws::JointCoordinateConvention::QInput};
    rws::CandidatePatch limitPatch;
    limitPatch.adapterId = "JointLimitAdapter";
    limitPatch.adapterVersion = 1;
    limitPatch.bindingId = "limit-upper";
    limitPatch.writes = {rws::CandidatePatchWrite{
        {rws::TargetObjectType::Joint, "joint-1",
         rws::TargetPropertyId::OperationalLimitUpper, "base"},
        rws::CandidatePatchValue::scalar(0.6)}};
    const rws::CandidatePatchApplyResult limitApply =
        rws::CandidatePatchApplier::apply(limitBaseline, limitPatch);
    REQUIRE(limitApply.ok);
    REQUIRE(std::fabs(limitApply.model.joints[0].operationalLimits.upper - 0.6) < 1e-12);
    REQUIRE(std::fabs(limitApply.model.joints[0].physicalLimits.upper - 1.0) < 1e-12);

    rws::CollisionBinding collision;
    collision.id = "collision-box";
    collision.referenceFrameId = "base";
    collision.kind = rws::CanonicalGeometryKind::Box;
    collision.optimizationOwned = true;
    collision.width = 0.2;
    collision.height = 0.3;
    collision.depth = 0.4;
    limitBaseline.collisionBindings.push_back(collision);
    rws::CandidatePatch collisionPatch;
    collisionPatch.adapterId = "ParameterizedCollisionAdapter";
    collisionPatch.adapterVersion = 1;
    collisionPatch.bindingId = "collision-width";
    collisionPatch.writes = {rws::CandidatePatchWrite{
        {rws::TargetObjectType::CollisionGeometry, "collision-box",
         rws::TargetPropertyId::GeometryWidth, "base"},
        rws::CandidatePatchValue::scalar(0.5)}};
    const rws::CandidatePatchApplyResult collisionApply =
        rws::CandidatePatchApplier::apply(limitBaseline, collisionPatch);
    REQUIRE(collisionApply.ok);
    REQUIRE(std::fabs(collisionApply.model.collisionBindings.front().width - 0.5) < 1e-12);
    REQUIRE(collisionApply.model.geometryBindings.size() ==
            limitBaseline.geometryBindings.size());
}

// Phase 3/S37: candidate compilation resolves one immutable design vector,
// evaluates compiled expressions, compiles grouped bindings, and publishes
// only an atomically validated canonical candidate.
static void testCandidateCompiler()
{
    const rws::CanonicalKinematicModel baseline = validCanonicalModelFixture();
    rws::CompiledDesignSpace space;
    space.schemaVersion = 1;
    space.fingerprint = "candidate-space-fixture";

    rws::DesignVariableDefinition input;
    input.id = "input-length";
    input.semanticKind = rws::SemanticKind::JointOriginOffsetX;
    input.role = rws::VariableRole::Independent;
    input.domain = rws::VariableDomain::Continuous;
    input.minimum = 0.0;
    input.maximum = 1.0;
    input.step = 0.1;
    input.unit = rws::DesignVariableUnit::Metres;
    rws::DesignVariableDefinition derived = input;
    derived.id = "derived-length";
    derived.role = rws::VariableRole::Derived;
    derived.bindingId = "binding:candidate";
    derived.derivedExpressionId = "expression:candidate";
    space.independentVariables = {input};
    space.derivedVariables = {derived};
    space.canonicalVectorSchema = {{input.id, 0, input.unit}};
    space.dependencyOrder = {derived.id};
    space.derivedExpressions = {
        rws::DerivedExpression::variableReference(derived.derivedExpressionId, input.id)};

    rws::ParameterBinding binding = adapterRegistryBinding();
    binding.id = derived.bindingId;
    space.resolvedBindings = {binding};

    const rws::DesignVectorResult vectorResult = rws::DesignVectorCodec::fromEngineering(
        space, {{input.id, input.unit, 0.25, ""}});
    REQUIRE(vectorResult.ok);

    rws::AdapterRegistry registry;
    REQUIRE(registry.registerAdapter(std::make_shared< AdapterRegistryTestAdapter >()).ok);
    rws::AdapterCapabilityQuery capabilities;
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                       rws::AdapterCapability::JointOrigin);

    rws::CandidateCompileRequest request;
    request.baseline = &baseline;
    request.designSpace = &space;
    request.designVector = &vectorResult.vector;
    request.adapterRegistry = &registry;
    request.capabilities = &capabilities;
    const rws::CandidateCompileResult compiled = rws::CandidateCompiler::compile(request);
    REQUIRE(compiled.ok);
    REQUIRE(compiled.candidate.status == rws::CandidateCompileStatus::Compiled);
    REQUIRE(compiled.candidate.derivedValues.at(derived.id).value == 0.25);
    REQUIRE(compiled.candidate.derivedValues.at(derived.id).unit == input.unit);
    REQUIRE(std::fabs(compiled.candidate.kinematicModel.joints[0].parentToJointZero.P()(0) -
                      0.25) < 1e-12);
    REQUIRE(!compiled.candidate.fingerprint.empty());
    REQUIRE(compiled.candidate.candidateId == compiled.candidate.fingerprint);
    REQUIRE(compiled.candidate.diagnostics.size() == compiled.diagnostics.size());
    REQUIRE(std::equal(compiled.candidate.diagnostics.begin(), compiled.candidate.diagnostics.end(),
                       compiled.diagnostics.begin(),
                       [](const rws::StructureOptimizationDiagnostic& first,
                          const rws::StructureOptimizationDiagnostic& second) {
                           return first.code == second.code && first.fieldPath == second.fieldPath;
                       }));
    REQUIRE(std::fabs(baseline.joints[0].parentToJointZero.P()(0)) < 1e-12);

    rws::DesignVector tamperedVector = vectorResult.vector;
    tamperedVector.canonicalBytes += "tampered";
    const rws::CandidateCompileResult tampered = rws::CandidateCompiler::compile(
        {&baseline, &space, &tamperedVector, &registry, &capabilities});
    REQUIRE(!tampered.ok);
    REQUIRE(hasAdapterDiagnostic(tampered.diagnostics,
                                 "CANDIDATE_COMPILE_DESIGN_VECTOR_FINGERPRINT_MISSING"));

    const rws::CandidateCompileResult repeated = rws::CandidateCompiler::compile(request);
    REQUIRE(repeated.ok);
    REQUIRE(repeated.candidate.fingerprint == compiled.candidate.fingerprint);
    REQUIRE(repeated.candidate.kinematicModel.joints[0].parentToJointZero.P()(0) ==
            compiled.candidate.kinematicModel.joints[0].parentToJointZero.P()(0));

    rws::DesignVector wrongSchema = vectorResult.vector;
    wrongSchema.designSpaceFingerprint = "different-space";
    const rws::CandidateCompileResult schemaMismatch = rws::CandidateCompiler::compile(
        {&baseline, &space, &wrongSchema, &registry, &capabilities});
    REQUIRE(!schemaMismatch.ok);
    REQUIRE(schemaMismatch.candidate.status == rws::CandidateCompileStatus::CompileFailed);
    REQUIRE(schemaMismatch.candidate.kinematicModel.frames.empty());
    REQUIRE(hasAdapterDiagnostic(schemaMismatch.diagnostics,
                                 "CANDIDATE_COMPILE_DESIGN_VECTOR_SCHEMA_MISMATCH"));

    rws::CompiledDesignSpace missingExpression = space;
    missingExpression.derivedExpressions.clear();
    const rws::CandidateCompileResult missingExpressionResult = rws::CandidateCompiler::compile(
        {&baseline, &missingExpression, &vectorResult.vector, &registry, &capabilities});
    REQUIRE(!missingExpressionResult.ok);
    REQUIRE(hasAdapterDiagnostic(missingExpressionResult.diagnostics,
                                 "CANDIDATE_COMPILE_DERIVED_EXPRESSION_MISSING"));

    rws::CanonicalKinematicModel invalidBaseline = baseline;
    invalidBaseline.rootFrameId = "missing-root";
    const rws::CandidateCompileResult invalidModel = rws::CandidateCompiler::compile(
        {&invalidBaseline, &space, &vectorResult.vector, &registry, &capabilities});
    REQUIRE(!invalidModel.ok);
    REQUIRE(hasAdapterDiagnostic(invalidModel.diagnostics,
                                 "CANDIDATE_COMPILE_BASELINE_INVALID"));

    rws::CanonicalKinematicModel combinedBaseline = validCanonicalModelFixture();
    combinedBaseline.frames.insert(combinedBaseline.frames.begin(),
                                  {"world", "System root", rws::CanonicalFrameType::Fixed});
    rws::JointEdge baseInstallation;
    baseInstallation.id = "world-to-base";
    baseInstallation.type = rws::CanonicalJointType::Fixed;
    baseInstallation.parentFrameId = "world";
    baseInstallation.childFrameId = "base";
    combinedBaseline.joints.insert(combinedBaseline.joints.begin(), baseInstallation);
    combinedBaseline.rootFrameId = "world";
    combinedBaseline.deviceChains[0].rootFrameId = "world";
    combinedBaseline.deviceChains[0].orderedJointIds.insert(
        combinedBaseline.deviceChains[0].orderedJointIds.begin(), baseInstallation.id);
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(combinedBaseline).valid);

    rws::ParameterBinding link = jointTranslationBinding(
        "binding:link", rws::SemanticKind::LinkLength, "ParameterizedLinkAdapter", "joint-1", "base");
    link.referenceDirectionFrameId = "base";
    link.referenceDirection = rw::math::Vector3D<>::x();
    const rws::ParameterBinding axisU = jointAxisBinding(
        "binding:axis-u", rws::SemanticKind::JointAxisTiltU, "joint-1", "base", rw::math::Pi / 4.0);
    const rws::ParameterBinding axisV = jointAxisBinding(
        "binding:axis-v", rws::SemanticKind::JointAxisTiltV, "joint-1", "base", rw::math::Pi / 4.0);
    const rws::ParameterBinding baseX = poseBinding(
        "binding:base-x", rws::SemanticKind::BaseTx, "BasePlacementAdapter",
        rws::TargetObjectType::Frame, "base", rws::TargetPropertyId::BaseTranslationX,
        "world", "base-pose:base");
    const rws::ParameterBinding tcpX = poseBinding(
        "binding:tcp-x", rws::SemanticKind::TcpTx, "TcpPoseAdapter",
        rws::TargetObjectType::ToolBinding, "tool",
        rws::TargetPropertyId::FlangeToTcpTranslationX, "flange", "tcp-pose:tool");

    const auto independent = [](const std::string& id, const rws::SemanticKind semantic,
                                const rws::DesignVariableUnit unit, const std::string& group,
                                const std::string& bindingId) {
        rws::DesignVariableDefinition variable;
        variable.id = id;
        variable.semanticKind = semantic;
        variable.role = rws::VariableRole::Independent;
        variable.domain = rws::VariableDomain::Continuous;
        variable.minimum = -1.0;
        variable.maximum = 1.0;
        variable.step = 0.01;
        variable.unit = unit;
        variable.groupId = group;
        variable.bindingId = bindingId;
        return variable;
    };
    const rws::DesignVariableDefinition linkVariable = independent(
        "link-length", rws::SemanticKind::LinkLength, rws::DesignVariableUnit::Metres,
        "link:joint-1", link.id);
    const rws::DesignVariableDefinition axisUVariable = independent(
        "axis-u", rws::SemanticKind::JointAxisTiltU, rws::DesignVariableUnit::Radians,
        "axis-tilt:joint-1", axisU.id);
    const rws::DesignVariableDefinition axisVVariable = independent(
        "axis-v", rws::SemanticKind::JointAxisTiltV, rws::DesignVariableUnit::Radians,
        "axis-tilt:joint-1", axisV.id);
    const rws::DesignVariableDefinition baseVariable = independent(
        "base-x", rws::SemanticKind::BaseTx, rws::DesignVariableUnit::Metres,
        "base-pose:base", baseX.id);
    const rws::DesignVariableDefinition tcpVariable = independent(
        "tcp-x", rws::SemanticKind::TcpTx, rws::DesignVariableUnit::Metres,
        "tcp-pose:tool", tcpX.id);
    rws::CompiledDesignSpace combinedSpace;
    combinedSpace.fingerprint = "candidate-combination-space";
    combinedSpace.independentVariables = {linkVariable, axisUVariable, axisVVariable,
                                          baseVariable, tcpVariable};
    for (std::size_t index = 0; index < combinedSpace.independentVariables.size(); ++index) {
        const rws::DesignVariableDefinition& variable = combinedSpace.independentVariables[index];
        combinedSpace.canonicalVectorSchema.push_back({variable.id, index, variable.unit});
    }
    combinedSpace.resolvedBindings = {link, axisU, axisV, baseX, tcpX};
    const rws::DesignVectorResult combinedVector = rws::DesignVectorCodec::fromEngineering(
        combinedSpace, {{linkVariable.id, linkVariable.unit, 0.4, ""},
                        {axisUVariable.id, axisUVariable.unit, 0.1, ""},
                        {axisVVariable.id, axisVVariable.unit, -0.2, ""},
                        {baseVariable.id, baseVariable.unit, 0.3, ""},
                        {tcpVariable.id, tcpVariable.unit, 0.2, ""}});
    REQUIRE(combinedVector.ok);

    rws::AdapterRegistry combinedRegistry;
    REQUIRE(combinedRegistry.registerAdapter(std::make_shared< rws::ParameterizedLinkAdapter >()).ok);
    REQUIRE(combinedRegistry.registerAdapter(std::make_shared< rws::JointAxisAdapter >()).ok);
    REQUIRE(combinedRegistry.registerAdapter(std::make_shared< rws::BasePlacementAdapter >()).ok);
    REQUIRE(combinedRegistry.registerAdapter(std::make_shared< rws::TcpPoseAdapter >()).ok);
    rws::AdapterCapabilityQuery combinedCapabilities;
    combinedCapabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                               rws::AdapterCapability::ParameterizedLink);
    combinedCapabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                               rws::AdapterCapability::JointAxisTilt);
    combinedCapabilities.grant(rws::TargetObjectType::Frame, "base",
                               rws::AdapterCapability::BasePlacement);
    combinedCapabilities.grant(rws::TargetObjectType::ToolBinding, "tool",
                               rws::AdapterCapability::TcpPose);
    combinedCapabilities.grant(rws::TargetObjectType::ToolBinding, "tool",
                               rws::AdapterCapability::ParameterizedGeometry);
    combinedCapabilities.grant(rws::TargetObjectType::ToolBinding, "tool",
                               rws::AdapterCapability::ParameterizedCollision);
    const rws::CandidateCompileResult combined = rws::CandidateCompiler::compile(
        {&combinedBaseline, &combinedSpace, &combinedVector.vector, &combinedRegistry,
         &combinedCapabilities});
    REQUIRE(combined.ok);
    REQUIRE(std::fabs(combined.candidate.kinematicModel.joints[1].parentToJointZero.P()(0) -
                      0.4) < 1e-12);
    REQUIRE(std::fabs((combined.candidate.kinematicModel.joints[1].motionAxisInJoint -
                       combinedBaseline.joints[1].motionAxisInJoint).norm2()) > 1e-6);
    REQUIRE(std::fabs(combined.candidate.kinematicModel.joints[0].parentToJointZero.P()(0) -
                      0.3) < 1e-12);
    REQUIRE(std::fabs(combined.candidate.kinematicModel.toolBindings[0].flangeToTcp.P()(0) -
                      0.2) < 1e-12);
    REQUIRE(std::fabs(combinedBaseline.joints[0].parentToJointZero.P()(0)) < 1e-12);
    REQUIRE(std::fabs(combinedBaseline.toolBindings[0].flangeToTcp.P()(0)) < 1e-12);
}

// Phase 3/S38: projection is an output boundary.  It must preserve the
// canonical chain semantics without producing a DH view, and the worker
// builder must compile an isolated WorkCell whose FK agrees with canonical FK.
static rws::CanonicalKinematicModel s38ProjectionFixture()
{
    rws::CanonicalKinematicModel model;
    model.modelId = "S38ProjectionRobot";
    model.rootFrameId = "Base";
    model.baseFrameId = "Base";
    model.activeDeviceChainId = "S38ProjectionRobot:chain";
    model.frames = {{"Base", "Base", rws::CanonicalFrameType::Base},
                    {"Joint1", "Joint1", rws::CanonicalFrameType::Link},
                    {"FixedGuide", "FixedGuide", rws::CanonicalFrameType::Fixed},
                    {"TCP", "TCP", rws::CanonicalFrameType::Tool}};

    rws::JointEdge revolute;
    revolute.id = "edge:Joint1";
    revolute.name = "Joint1";
    revolute.type = rws::CanonicalJointType::Revolute;
    revolute.parentFrameId = "Base";
    revolute.childFrameId = "Joint1";
    revolute.parentToJointZero = rw::math::Transform3D<>(rw::math::Vector3D<>(0.0, 0.0, 0.2));
    revolute.motionAxisInJoint = rw::math::Vector3D<>::z();
    revolute.dofId = "dof:Joint1";
    revolute.physicalLimits = {true, -rw::math::Pi, rw::math::Pi,
                               rws::CanonicalCoordinateUnit::Radians,
                               rws::JointCoordinateConvention::QInput};

    rws::JointEdge fixed;
    fixed.id = "edge:TCP";
    fixed.name = "TCP";
    fixed.type = rws::CanonicalJointType::Fixed;
    fixed.parentFrameId = "Joint1";
    fixed.childFrameId = "TCP";
    fixed.parentToJointZero = rw::math::Transform3D<>(rw::math::Vector3D<>(0.0, 0.0, 0.15));
    model.joints = {revolute, fixed};
    model.dofs = {{"dof:Joint1", revolute.id, 0, rws::CanonicalJointType::Revolute,
                   rws::CanonicalCoordinateUnit::Radians}};
    model.deviceChains = {{model.activeDeviceChainId, "Base", "TCP",
                           {revolute.id, fixed.id}, {"dof:Joint1"}}};

    rws::GeometryBinding visual;
    visual.id = "visual:Joint1";
    visual.referenceFrameId = "Joint1";
    visual.kind = rws::CanonicalGeometryKind::Cylinder;
    visual.optimizationOwned = true;
    visual.radius = 0.03;
    visual.length = 0.2;
    model.geometryBindings.push_back(visual);

    rws::CollisionBinding collision;
    collision.id = "collision:Joint1";
    collision.referenceFrameId = "Joint1";
    collision.kind = rws::CanonicalGeometryKind::Box;
    collision.optimizationOwned = true;
    collision.width = 0.1;
    collision.height = 0.1;
    collision.depth = 0.2;
    model.collisionBindings.push_back(collision);
    return model;
}

static void testS38ProjectionAndEvaluationDevice()
{
    const rws::CanonicalKinematicModel model = s38ProjectionFixture();
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(model).valid);

    const rws::RobotModelSpecProjectionResult projection =
        rws::RobotModelSpecProjectionAdapter::project({&model, "S38ProjectionRobot", "", nullptr});
    REQUIRE(projection.ok);
    if (!projection.ok)
        return;
    REQUIRE(projection.spec.mode == rws::KinematicsViewMode::JointRPYPos);
    REQUIRE(!projection.spec.exportDhJointsAdvanced);
    REQUIRE(projection.spec.dhJoints.empty());
    REQUIRE(projection.spec.transformJoints.size() == 2);
    REQUIRE(projection.spec.transformJoints[0].type == "Revolute");
    REQUIRE(projection.spec.transformJoints[1].type == "ToolFrame");
    REQUIRE(projection.spec.transformJoints[0].name == "Joint1");
    REQUIRE(projection.spec.transformJoints[1].name == "TCP");
    REQUIRE(projection.spec.drawables.size() == 1);
    REQUIRE(projection.spec.collisionModels.size() == 1);

    rws::EvaluationDeviceBuildRequest request;
    request.model = &model;
    request.deviceName = "S38ProjectionRobot";
    request.tcpFrame = "TCP";
    request.checkCollision = false;
    const rws::EvaluationDeviceBuildResult built =
        rws::EvaluationDeviceBuilder::build(request);
    REQUIRE(built.ok);
    REQUIRE(!built.artifact.workcell.isNull());
    REQUIRE(!built.artifact.device.isNull());
    REQUIRE(built.artifact.tcpFrame->getName() == "TCP" ||
            built.artifact.tcpFrame->getName().find(".TCP") != std::string::npos);

    const std::vector< double > q = {0.35};
    const rws::CanonicalForwardKinematicsResult canonicalFk =
        rws::CanonicalForwardKinematics::evaluate(model, q);
    REQUIRE(canonicalFk.valid);
    rw::kinematics::State state = built.artifact.workcell->getDefaultState();
    built.artifact.device->setQ(rw::math::Q(1, q[0]), state);
    const rw::math::Transform3D<> generatedTcp = rw::kinematics::Kinematics::frameTframe(
        built.artifact.workcell->getWorldFrame(), built.artifact.tcpFrame.get(), state);
    rw::math::Transform3D<> canonicalTcp;
    REQUIRE(rws::CanonicalForwardKinematics::frameTransform(canonicalFk, "TCP", canonicalTcp));
    REQUIRE(sameTransform(generatedTcp, canonicalTcp));

    // Imported WorkCells can expose the installation frame as the device
    // base ("RobotBase") while the legacy XML writer emits its serial-device
    // base as "Base".  Projection must bridge those identifiers instead of
    // rejecting an otherwise valid canonical chain.
    rws::CanonicalKinematicModel robotBaseModel = model;
    robotBaseModel.rootFrameId = "WorldMount";
    robotBaseModel.baseFrameId = "RobotBase";
    robotBaseModel.frames.front() = {"RobotBase", "RobotBase", rws::CanonicalFrameType::Base};
    robotBaseModel.frames.insert(robotBaseModel.frames.begin(),
                                 {"WorldMount", "WorldMount", rws::CanonicalFrameType::Fixed});
    robotBaseModel.joints.front().parentFrameId = "RobotBase";
    rws::JointEdge robotBaseInstallation;
    robotBaseInstallation.id = "edge:RobotBaseInstallation";
    robotBaseInstallation.name = "RobotBaseInstallation";
    robotBaseInstallation.type = rws::CanonicalJointType::Fixed;
    robotBaseInstallation.parentFrameId = "WorldMount";
    robotBaseInstallation.childFrameId = "RobotBase";
    robotBaseInstallation.parentToJointZero = rw::math::Transform3D<>(
        rw::math::Vector3D<>(0.1, 0.2, 0.3));
    robotBaseModel.joints.insert(robotBaseModel.joints.begin(), robotBaseInstallation);
    robotBaseModel.deviceChains.front().rootFrameId = "WorldMount";
    robotBaseModel.deviceChains.front().orderedJointIds.insert(
        robotBaseModel.deviceChains.front().orderedJointIds.begin(), robotBaseInstallation.id);
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(robotBaseModel).valid);
    const auto robotBaseProjection = rws::RobotModelSpecProjectionAdapter::project(
        {&robotBaseModel, "S38RobotBase", "", nullptr});
    REQUIRE(robotBaseProjection.ok);
    if (robotBaseProjection.ok) {
        rws::EvaluationDeviceBuildRequest robotBaseRequest;
        robotBaseRequest.model = &robotBaseModel;
        robotBaseRequest.deviceName = "S38RobotBase";
        robotBaseRequest.tcpFrame = "TCP";
        robotBaseRequest.checkCollision = false;
        const auto robotBaseBuilt = rws::EvaluationDeviceBuilder::build(robotBaseRequest);
        REQUIRE(robotBaseBuilt.ok);
        if (robotBaseBuilt.ok) {
            const rws::CanonicalForwardKinematicsResult robotBaseCanonicalFk =
                rws::CanonicalForwardKinematics::evaluate(robotBaseModel, q);
            REQUIRE(robotBaseCanonicalFk.valid);
            rw::kinematics::State robotBaseState = robotBaseBuilt.artifact.workcell->getDefaultState();
            robotBaseBuilt.artifact.device->setQ(rw::math::Q(1, q[0]), robotBaseState);
            const rw::math::Transform3D<> robotBaseTcp = rw::kinematics::Kinematics::frameTframe(
                robotBaseBuilt.artifact.workcell->getWorldFrame(),
                robotBaseBuilt.artifact.tcpFrame.get(), robotBaseState);
            rw::math::Transform3D<> robotBaseCanonicalTcp;
            REQUIRE(rws::CanonicalForwardKinematics::frameTransform(
                robotBaseCanonicalFk, "TCP", robotBaseCanonicalTcp));
            REQUIRE(sameTransform(robotBaseTcp, robotBaseCanonicalTcp));
        }
    }

    // WorkCell import qualifies device-frame IDs (for example,
    // "ImportedRobot.Joint1"), while an imported CollisionSetup keeps the
    // device-local names.  The output XML must use device-local names again.
    rws::CanonicalKinematicModel qualifiedModel = model;
    qualifiedModel.modelId = "QualifiedProjectionRobot";
    const std::map< std::string, std::string > qualifiedNames = {
        {"Base", "QualifiedProjectionRobot.Base"},
        {"Joint1", "QualifiedProjectionRobot.Joint1"},
        {"FixedGuide", "QualifiedProjectionRobot.FixedGuide"},
        {"TCP", "QualifiedProjectionRobot.TCP"}};
    for (rws::FrameNode& frame : qualifiedModel.frames) {
        frame.id = qualifiedNames.at(frame.id);
        frame.name = qualifiedNames.at(frame.name);
    }
    qualifiedModel.rootFrameId = qualifiedNames.at(qualifiedModel.rootFrameId);
    qualifiedModel.baseFrameId = qualifiedNames.at(qualifiedModel.baseFrameId);
    qualifiedModel.activeDeviceChainId = "QualifiedProjectionRobot:chain";
    for (rws::JointEdge& edge : qualifiedModel.joints) {
        edge.parentFrameId = qualifiedNames.at(edge.parentFrameId);
        edge.childFrameId = qualifiedNames.at(edge.childFrameId);
    }
    qualifiedModel.deviceChains.front().id = qualifiedModel.activeDeviceChainId;
    qualifiedModel.deviceChains.front().rootFrameId = qualifiedNames.at("Base");
    qualifiedModel.deviceChains.front().tipFrameId = qualifiedNames.at("TCP");
    qualifiedModel.geometryBindings.front().referenceFrameId = qualifiedNames.at("Joint1");
    qualifiedModel.collisionBindings.front().referenceFrameId = qualifiedNames.at("Joint1");
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(qualifiedModel).valid);

    rws::RobotModelSpec qualifiedSnapshot;
    qualifiedSnapshot.collisionSetup.enabled = true;
    qualifiedSnapshot.collisionSetup.excludePairs.push_back(
        {"TCP", "Joint1", true, "Imported", ""});
    const auto qualifiedProjection = rws::RobotModelSpecProjectionAdapter::project(
        {&qualifiedModel, qualifiedModel.modelId, "", &qualifiedSnapshot});
    REQUIRE(qualifiedProjection.ok);
    if (qualifiedProjection.ok) {
        REQUIRE(qualifiedProjection.spec.transformJoints.front().name == "Joint1");
        REQUIRE(qualifiedProjection.spec.transformJoints.back().name == "TCP");
        REQUIRE(qualifiedProjection.spec.drawables.front().refFrame == "Joint1");
        REQUIRE(qualifiedProjection.spec.collisionModels.front().refFrame == "Joint1");
        rws::EvaluationDeviceBuildRequest qualifiedRequest;
        qualifiedRequest.model = &qualifiedModel;
        qualifiedRequest.deviceName = qualifiedModel.modelId;
        qualifiedRequest.tcpFrame = "TCP";
        qualifiedRequest.sourceSnapshot = &qualifiedSnapshot;
        qualifiedRequest.checkCollision = false;
        const auto qualifiedBuilt = rws::EvaluationDeviceBuilder::build(qualifiedRequest);
        REQUIRE(qualifiedBuilt.ok);
    }

    rws::CanonicalKinematicModel flangeTool = model;
    flangeTool.frames[1].type = rws::CanonicalFrameType::Flange;
    flangeTool.joints.erase(flangeTool.joints.begin() + 1);
    flangeTool.deviceChains.front().tipFrameId = "Joint1";
    flangeTool.deviceChains.front().orderedJointIds.pop_back();
    flangeTool.toolBindings = {{"tool", "Joint1", "TCP",
                                rw::math::Transform3D<>(rw::math::Vector3D<>(0.0, 0.0, 0.15))}};
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(flangeTool).valid);
    const auto flangeProjection = rws::RobotModelSpecProjectionAdapter::project(
        {&flangeTool, "S38FlangeTool", "", nullptr});
    REQUIRE(flangeProjection.ok);
    REQUIRE(flangeProjection.spec.transformJoints.size() == 2);
    REQUIRE(flangeProjection.spec.transformJoints.back().type == "ToolFrame");
    rws::EvaluationDeviceBuildRequest flangeRequest;
    flangeRequest.model = &flangeTool;
    flangeRequest.deviceName = "S38FlangeTool";
    flangeRequest.tcpFrame = "TCP";
    flangeRequest.checkCollision = false;
    const auto flangeBuilt = rws::EvaluationDeviceBuilder::build(flangeRequest);
    REQUIRE(flangeBuilt.ok);

    rws::CanonicalKinematicModel installed = model;
    installed.frames.insert(installed.frames.begin(),
                            {"WorldMount", "WorldMount", rws::CanonicalFrameType::Fixed});
    rws::JointEdge installation;
    installation.id = "edge:WorldMount";
    installation.name = "WorldMount";
    installation.type = rws::CanonicalJointType::Fixed;
    installation.parentFrameId = "WorldMount";
    installation.childFrameId = "Base";
    installation.parentToJointZero = rw::math::Transform3D<>(
        rw::math::Vector3D<>(0.1, 0.2, 0.3));
    installed.joints.insert(installed.joints.begin(), installation);
    installed.rootFrameId = "WorldMount";
    installed.deviceChains.front().rootFrameId = "WorldMount";
    installed.deviceChains.front().orderedJointIds.insert(
        installed.deviceChains.front().orderedJointIds.begin(), installation.id);
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(installed).valid);
    const auto installedProjection = rws::RobotModelSpecProjectionAdapter::project(
        {&installed, "S38Installed", "", nullptr});
    REQUIRE(installedProjection.ok);
    REQUIRE(std::fabs(installedProjection.spec.robotBaseFrame.pos[0] - 0.1) < 1e-12);

    rws::EvaluationDeviceBuildRequest invalidRequest;
    invalidRequest.deviceName = "S38ProjectionRobot";
    invalidRequest.tcpFrame = "TCP";
    invalidRequest.checkCollision = false;
    const rws::EvaluationDeviceBuildResult failed =
        rws::EvaluationDeviceBuilder::build(invalidRequest);
    REQUIRE(!failed.ok);
    REQUIRE(failed.artifact.workcell.isNull());
}

// S52: the baseline is a canonical candidate at stable index zero.  It must
// traverse the same compiler and Verified task path as later candidates.
static void testCanonicalBaselineEvaluationBridge()
{
    std::printf("testCanonicalBaselineEvaluationBridge ... ");
    const rws::CanonicalKinematicModel model = s38ProjectionFixture();
    const rws::KinematicBaselineSnapshotResult snapshot =
        rws::KinematicBaselineSnapshot::create(model);
    REQUIRE(snapshot.ok);
    if (!snapshot.ok)
        return;

    rws::StructureOptimizationProblem problem;
    problem.context.deviceName = model.modelId;
    problem.canonicalModelShadow.status = rws::CanonicalModelShadowStatus::Current;
    problem.canonicalModelShadow.snapshot =
        std::make_shared< rws::KinematicBaselineSnapshot >(snapshot.snapshot);
    problem.requirementExecution.schemaVersion = 4;
    problem.requirementExecution.provenance.requirementFingerprint = "s52-baseline";
    // Frozen requirements identify their RobotModelSpec and frozen WorkCell
    // using SHA-256.  The compiled canonical candidate has a separate FNV
    // fingerprint for its own audit trail; it must not be compared to these
    // source-contract fingerprints by the evaluation plan.
    const std::string frozenSourceFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(problem.context.modelSpec);
    REQUIRE(!frozenSourceFingerprint.empty());
    problem.requirementExecution.provenance.robotModelFingerprint = frozenSourceFingerprint;
    problem.requirementExecution.provenance.environmentFingerprint = "frozen-environment";
    rws::RequirementExecutionTask target;
    target.id = "nominal-tcp";
    target.refFrame = "Base";
    target.tcpFrame = "TCP";
    target.position = {{0.0, 0.0, 0.35}};
    target.rpyDeg = {{0.0, 0.0, 0.0}};
    target.positionToleranceMeters = 1e-6;
    target.orientationToleranceDeg = 1e-4;
    target.collisionFreeRequired = false;
    problem.requirementExecution.tasks.push_back(target);

    rws::CanonicalBaselineEvaluationRequest request;
    request.problem = &problem;
    request.deviceName = model.modelId;
    request.tcpFrame = "TCP";
    request.checkCollision = false;
    request.planOptions.capabilities.insert("target");
    const rws::BaselineEvaluationResult result =
        rws::CanonicalBaselineEvaluationBridge::evaluate(request);

    REQUIRE(result.ok);
    REQUIRE(result.baselineIndex == 0);
    REQUIRE(result.designVector.values.empty());
    REQUIRE(result.candidateResult.lifecycle == rws::CandidateLifecycle::Completed);
    REQUIRE(result.candidateResult.evidenceStage == rws::AnalysisEvidenceStage::Verified);
    // This one-DOF fixture has evidence of an unreachable 6D target.  The
    // baseline remains completed, is explicitly Infeasible, and carries the
    // bridge warning instead of being silently discarded.
    REQUIRE(result.candidateResult.feasibility == rws::Feasibility::Infeasible);
    REQUIRE(!result.candidateResult.warnings.empty());
    REQUIRE(!result.candidateFingerprint.empty());
    REQUIRE(!result.modelFingerprint.empty());
    REQUIRE(!result.planFingerprint.empty());
    REQUIRE(result.plan.modelFingerprint == frozenSourceFingerprint);
    REQUIRE(result.plan.environmentFingerprint == "frozen-environment");
    REQUIRE(result.plan.fingerprint == result.planFingerprint);
    const rws::BaselineEvaluationResult repeated =
        rws::CanonicalBaselineEvaluationBridge::evaluate(request);
    REQUIRE(repeated.ok);
    REQUIRE(repeated.baselineIndex == result.baselineIndex);
    REQUIRE(repeated.candidateFingerprint == result.candidateFingerprint);
    REQUIRE(repeated.planFingerprint == result.planFingerprint);
    std::printf("PASSED\n");
}

// S52: the asynchronous controller keeps baseline provenance in its legacy
// result instead of letting a later candidate/result projection overwrite it.
static void testStructureOptimizationControllerBaselineBridge()
{
    std::printf("testStructureOptimizationControllerBaselineBridge ... ");
    const rws::CanonicalKinematicModel model = s38ProjectionFixture();
    const rws::KinematicBaselineSnapshotResult snapshot =
        rws::KinematicBaselineSnapshot::create(model);
    REQUIRE(snapshot.ok);
    if (!snapshot.ok)
        return;

    rws::StructureOptimizationProblem problem;
    const rws::RobotModelSpecProjectionResult projection =
        rws::RobotModelSpecProjectionAdapter::project({&model, model.modelId, "", nullptr});
    REQUIRE(projection.ok);
    if (!projection.ok)
        return;
    // The controller's canonical bridge is authoritative for feasibility, but
    // the legacy result consumed by the widget must still contain the same
    // metrics that candidates use for score/reachability/length columns.
    problem.context.modelSpec = projection.spec;
    problem.context.deviceName = model.modelId;
    problem.context.tcpFrame = "TCP";
    // S52's canonical candidate may have no first-phase parameter bindings,
    // while the legacy optimizer still owns variables that its metric evaluator
    // must receive at their current baseline values.
    rws::StructureDesignVariable jointOffset;
    jointOffset.id = "joint-1-z";
    jointOffset.label = "Joint 1 Z";
    jointOffset.targetName = "Joint1";
    jointOffset.unit = "m";
    jointOffset.kind = rws::StructureVariableKind::JointPositionZ;
    jointOffset.currentValue = 0.2;
    jointOffset.minimum = 0.1;
    jointOffset.maximum = 0.3;
    jointOffset.step = 0.01;
    jointOffset.enabled = true;
    problem.variables = {jointOffset};
    problem.canonicalModelShadow.status = rws::CanonicalModelShadowStatus::Current;
    problem.canonicalModelShadow.snapshot =
        std::make_shared< rws::KinematicBaselineSnapshot >(snapshot.snapshot);
    problem.requirementExecution.schemaVersion = 4;
    problem.requirementExecution.provenance.requirementFingerprint = "s52-controller";

    rws::StructureOptimizationController controller;
    rws::StructureOptimizationResult completed;
    bool received = false;
    QEventLoop loop;
    QObject::connect(&controller, &rws::StructureOptimizationController::baselineCompleted,
                     [&completed, &received, &loop](const rws::StructureOptimizationResult& result) {
                         completed = result;
                         received = true;
                         loop.quit();
                     });
    REQUIRE(controller.startBaselineEvaluation(problem));
    QTimer::singleShot(5000, &loop, SLOT(quit()));
    loop.exec();

    REQUIRE(received);
    REQUIRE(completed.baselineCandidateIndex == 0);
    REQUIRE(!completed.baselineAudit.candidateFingerprint.empty());
    REQUIRE(!completed.baselineAudit.modelFingerprint.empty());
    REQUIRE(!completed.baselineAudit.planFingerprint.empty());
    REQUIRE(completed.candidates.size() == 1);
    REQUIRE(completed.candidates.front().index == completed.baselineCandidateIndex);
    REQUIRE(completed.candidates.front().raw.modelValid);
    REQUIRE(completed.candidates.front().raw.weightedReachability > 0.0);
    REQUIRE(completed.candidates.front().raw.totalKinematicLength > 0.0);
    REQUIRE(completed.candidates.front().totalScore > 0.0);
    std::printf("PASSED\n");
}

// S52 回归:存量项目保存时还没有 canonicalModelShadow。基线评估入口必须按项目
// 自身 modelSpec 现场重建影子(与候选评估同一构建路径),而不是用 S52 永久拒绝。
// 这里同步验证重建三步曲(建模→规范导入→attach),与控制器内实现一一对应。
static void testBaselineRebuildsMissingCanonicalShadow()
{
    std::printf("testBaselineRebuildsMissingCanonicalShadow ... ");
    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.modelSpec.robotName = "ShadowRebuildRobot";
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;
    problem.context.tcpFrame = "TCP";   // 裸名:同时覆盖 TCP 设备前缀回退路径
    // 故意不设置 canonicalModelShadow —— 模拟旧项目加载后的状态
    REQUIRE(!problem.canonicalModelShadow.hasSnapshot());

    rws::CandidateModelBuildRequest buildRequest;
    buildRequest.spec = problem.context.modelSpec;
    buildRequest.deviceName = problem.context.deviceName;
    buildRequest.tcpFrame = problem.context.tcpFrame;
    buildRequest.checkCollision = false;
    rws::CandidateModelFactory factory;
    const rws::CandidateModelBuildResult built = factory.build(buildRequest);
    REQUIRE(built.ok);
    REQUIRE(built.artifact.workcell != nullptr);
    REQUIRE(built.artifact.device != nullptr);
    REQUIRE(built.artifact.tcpFrame != nullptr);

    rws::KinematicImportRequest importRequest;
    importRequest.workcell = built.artifact.workcell.get();
    importRequest.device = built.artifact.device.get();
    importRequest.tcpFrame = built.artifact.tcpFrame.get();
    importRequest.sourceSnapshot = &problem.context.modelSpec;
    importRequest.sourceFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(problem.context.modelSpec);
    std::string shadowError;
    REQUIRE(rws::CanonicalModelShadowService::attach(importRequest, problem,
                                                     &shadowError));
    REQUIRE(problem.canonicalModelShadow.status ==
            rws::CanonicalModelShadowStatus::Current);
    REQUIRE(problem.canonicalModelShadow.hasSnapshot());
    REQUIRE(!problem.canonicalModelShadow.snapshot->modelFingerprint.empty());
    std::printf("PASSED\n");
}

static void testBaseFlangeAndTcpAdapters()
{
    const rws::CanonicalKinematicModel baseline = independentFlangeFixture();
    const rws::CanonicalKinematicModel sourceCopy = baseline;
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(baseline).valid);

    rws::AdapterRegistry registry;
    REQUIRE(registry.registerAdapter(std::make_shared< rws::BasePlacementAdapter >()).ok);
    REQUIRE(registry.registerAdapter(std::make_shared< rws::FlangePoseAdapter >()).ok);
    REQUIRE(registry.registerAdapter(std::make_shared< rws::TcpPoseAdapter >()).ok);
    rws::AdapterCapabilityQuery capabilities;
    capabilities.grant(rws::TargetObjectType::Frame, "base", rws::AdapterCapability::BasePlacement);
    capabilities.grant(rws::TargetObjectType::Frame, "flange", rws::AdapterCapability::FlangePose);
    capabilities.grant(rws::TargetObjectType::ToolBinding, "tool", rws::AdapterCapability::TcpPose);

    const rws::ParameterBinding baseX = poseBinding(
        "base-x", rws::SemanticKind::BaseTx, "BasePlacementAdapter", rws::TargetObjectType::Frame,
        "base", rws::TargetPropertyId::BaseTranslationX, "base", "base-pose:base");
    const rws::AdapterPatchCompileResult basePatch = registry.compilePatch(
        {&baseline, &baseX, {{"base-x", rws::DesignVariableUnit::Metres, 0.25, "",
                              rws::SemanticKind::BaseTx, "base-pose:base"}}}, capabilities);
    REQUIRE(basePatch.ok);
    REQUIRE(basePatch.patch.writes.size() == 1);
    REQUIRE(basePatch.patch.writes.front().target.objectType == rws::TargetObjectType::Frame);
    REQUIRE(basePatch.patch.writes.front().target.objectId == "base");
    REQUIRE(basePatch.patch.writes.front().target.propertyId ==
            rws::TargetPropertyId::BaseTranslationX);
    REQUIRE(basePatch.patch.writes.front().target.coordinateFrameId == "base");
    REQUIRE(std::fabs(basePatch.patch.writes.front().value.scalarValue - 0.25) < 1e-12);
    REQUIRE(basePatch.patch.poseDeltaComposition == rws::PoseDeltaComposition::Right);
    REQUIRE(basePatch.patch.poseDeltaGroupId == "base-pose:base");
    rws::ParameterBinding baseMissingId = baseX;
    baseMissingId.id.clear();
    rws::BasePlacementAdapter directBase;
    const rws::AdapterPatchCompileResult directBaseMalformed = directBase.compilePatch(
        {&baseline, &baseMissingId, {{"base-x", rws::DesignVariableUnit::Metres, 0.25, "",
                                      rws::SemanticKind::BaseTx, "base-pose:base"}}});
    REQUIRE(!directBaseMalformed.ok);
    REQUIRE(hasAdapterDiagnostic(directBaseMalformed.diagnostics,
                                 "PARAMETER_BINDING_ID_REQUIRED"));
    // The Patch names only the base placement; it has no task/environment write
    // and compilation leaves the imported canonical baseline immutable.
    REQUIRE(basePatch.patch.writes.front().target.objectId != baseline.rootFrameId ||
            baseline.rootFrameId == baseline.baseFrameId);
    REQUIRE(sameTransform(baseline.joints[0].parentToJointZero,
                          sourceCopy.joints[0].parentToJointZero));

    // Compiler identity must preserve the frame in which a spatial variable
    // is expressed; a UI frame label may never silently disagree with its
    // adapter binding coordinate frame.
    rws::DesignVariableDefinition baseVariable;
    baseVariable.id = "base-x-frame-check";
    baseVariable.semanticKind = rws::SemanticKind::BaseTx;
    baseVariable.role = rws::VariableRole::Independent;
    baseVariable.domain = rws::VariableDomain::Continuous;
    baseVariable.minimum = -1.0;
    baseVariable.maximum = 1.0;
    baseVariable.step = 0.01;
    baseVariable.unit = rws::DesignVariableUnit::Metres;
    baseVariable.frameId = "flange"; // deliberately not binding.coordinateFrameId == base
    baseVariable.bindingId = baseX.id;
    const rws::DesignSpaceRegistry semanticRegistry = rws::DesignSpaceRegistry::firstPhase();
    rws::DesignSpaceCompileRequest mismatchedFrameRequest;
    mismatchedFrameRequest.model = &baseline;
    mismatchedFrameRequest.registry = &semanticRegistry;
    mismatchedFrameRequest.capabilities = &capabilities;
    mismatchedFrameRequest.adapterRegistry = &registry;
    mismatchedFrameRequest.variables = {baseVariable};
    mismatchedFrameRequest.bindings = {baseX};
    const rws::DesignSpaceCompileResult mismatchedFrame =
        rws::DesignSpaceCompiler::compile(mismatchedFrameRequest);
    REQUIRE(!mismatchedFrame.ok);
    REQUIRE(hasAdapterDiagnostic(mismatchedFrame.diagnostics,
                                 "DESIGN_SPACE_VARIABLE_BINDING_FRAME_MISMATCH"));

    // The rule is deliberately narrow: joint-coordinate values are not pose
    // vectors and retain their established coordinate-frame contract.
    REQUIRE(registry.registerAdapter(std::make_shared< rws::JointZeroAdapter >()).ok);
    capabilities.grant(rws::TargetObjectType::Joint, "joint-1",
                       rws::AdapterCapability::JointZeroOffset);
    rws::DesignVariableDefinition jointZeroVariable = baseVariable;
    jointZeroVariable.id = "joint-zero-nonspatial-frame";
    jointZeroVariable.semanticKind = rws::SemanticKind::JointZeroOffset;
    jointZeroVariable.unit = rws::DesignVariableUnit::Radians;
    jointZeroVariable.frameId = "flange";
    jointZeroVariable.bindingId = "joint-zero-nonspatial-frame";
    const rws::ParameterBinding jointZero = jointZeroBinding(
        jointZeroVariable.bindingId, "joint-1", "base");
    rws::DesignSpaceCompileRequest nonSpatialFrameRequest = mismatchedFrameRequest;
    nonSpatialFrameRequest.variables = {jointZeroVariable};
    nonSpatialFrameRequest.bindings = {jointZero};
    const rws::DesignSpaceCompileResult nonSpatialFrame =
        rws::DesignSpaceCompiler::compile(nonSpatialFrameRequest);
    REQUIRE(nonSpatialFrame.ok);
    REQUIRE(!hasAdapterDiagnostic(nonSpatialFrame.diagnostics,
                                  "DESIGN_SPACE_VARIABLE_BINDING_FRAME_MISMATCH"));

    rws::ParameterBinding wrongBaseFrame = baseX;
    wrongBaseFrame.coordinateFrameId = "flange";
    wrongBaseFrame.readSet.front().coordinateFrameId = "flange";
    wrongBaseFrame.writeSet.front().coordinateFrameId = "flange";
    const rws::AdapterPatchCompileResult badBase = registry.compilePatch(
        {&baseline, &wrongBaseFrame, {{"base-x", rws::DesignVariableUnit::Metres, 0.25, "",
                                      rws::SemanticKind::BaseTx, "base-pose:base"}}}, capabilities);
    REQUIRE(!badBase.ok);
    REQUIRE(hasAdapterDiagnostic(badBase.diagnostics, "BASE_PLACEMENT_ROOT_FRAME_REQUIRED"));

    // All S34 rotation-vector updates are right increments: R' = R * Exp(r).
    // Choosing noncommuting X/Z rotations makes an accidental left update fail.
    const rw::math::Transform3D<> initialRotation(
        rw::math::Vector3D<>(), rw::math::RPY<>(0.0, 0.0, rw::math::Pi / 2.0).toRotation3D());
    const rw::math::Vector3D<> deltaRotation(0.0, 0.0, rw::math::Pi / 2.0);
    const rw::math::Transform3D<> rightRotated =
        rws::PoseDelta::applyRotationVectorDelta(initialRotation, deltaRotation,
                                                   rws::PoseDeltaComposition::Right);
    const rw::math::Transform3D<> expectedRight = initialRotation * rw::math::Transform3D<>(
        rw::math::Vector3D<>(), rw::math::RPY<>(rw::math::Pi / 2.0, 0.0, 0.0).toRotation3D());
    const rw::math::Transform3D<> leftRotated = rw::math::Transform3D<>(
        rw::math::Vector3D<>(), rw::math::RPY<>(rw::math::Pi / 2.0, 0.0, 0.0).toRotation3D()) *
        initialRotation;
    REQUIRE(sameTransform(rightRotated, expectedRight));
    REQUIRE(!sameTransform(rightRotated, leftRotated));

    const rws::ParameterBinding flangeY = poseBinding(
        "flange-y", rws::SemanticKind::FlangeTy, "FlangePoseAdapter",
        rws::TargetObjectType::Frame, "flange", rws::TargetPropertyId::ParentToFlangeTranslationY,
        "arm-tip", "flange-pose:flange");
    const rws::AdapterPatchCompileResult flangePatch = registry.compilePatch(
        {&baseline, &flangeY, {{"flange-y", rws::DesignVariableUnit::Metres, 0.4, "",
                                rws::SemanticKind::FlangeTy, "flange-pose:flange"}}}, capabilities);
    REQUIRE(flangePatch.ok);
    REQUIRE(flangePatch.patch.writes.size() == 1);
    REQUIRE(flangePatch.patch.writes.front().target.coordinateFrameId == "arm-tip");
    // parent-to-flange coordinates are the declared parent-frame coordinates;
    // the baseline Y=-.2 plus .4 delta yields +.2 without Euler state.
    REQUIRE(std::fabs(flangePatch.patch.writes.front().value.scalarValue - 0.2) < 1e-12);
    rws::ParameterBinding flangeMissingOwnerVersion = flangeY;
    flangeMissingOwnerVersion.ownerAdapterVersion = 0;
    rws::FlangePoseAdapter directFlange;
    const rws::AdapterPatchCompileResult directFlangeMalformed = directFlange.compilePatch(
        {&baseline, &flangeMissingOwnerVersion,
         {{"flange-y", rws::DesignVariableUnit::Metres, 0.4, "",
           rws::SemanticKind::FlangeTy, "flange-pose:flange"}}});
    REQUIRE(!directFlangeMalformed.ok);
    REQUIRE(hasAdapterDiagnostic(directFlangeMalformed.diagnostics,
                                 "PARAMETER_BINDING_OWNER_VERSION_INVALID"));

    // A flange frame may exist on a valid side branch, but it is not a design
    // target unless its unique fixed mount belongs to the active device chain.
    rws::CanonicalKinematicModel inactiveFlangeMount = baseline;
    inactiveFlangeMount.deviceChains[0].tipFrameId = "arm-tip";
    inactiveFlangeMount.deviceChains[0].orderedJointIds.pop_back();
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(inactiveFlangeMount).valid);
    const rws::AdapterPatchCompileResult inactiveFlange = registry.compilePatch(
        {&inactiveFlangeMount, &flangeY,
         {{"flange-y", rws::DesignVariableUnit::Metres, 0.4, "",
           rws::SemanticKind::FlangeTy, "flange-pose:flange"}}}, capabilities);
    REQUIRE(!inactiveFlange.ok);
    REQUIRE(hasAdapterDiagnostic(inactiveFlange.diagnostics,
                                 "FLANGE_POSE_INDEPENDENT_FLANGE_REQUIRED"));

    // Even if the active path reaches the flange through a movable joint, an
    // additional side fixed edge makes the flange installation ambiguous.
    rws::CanonicalKinematicModel multipleFlangeInputs = baseline;
    multipleFlangeInputs.joints[2].childFrameId = "flange";
    multipleFlangeInputs.deviceChains[0].tipFrameId = "flange";
    multipleFlangeInputs.deviceChains[0].orderedJointIds.pop_back();
    REQUIRE(rws::CanonicalKinematicModelValidator::validate(multipleFlangeInputs).valid);
    const rws::AdapterPatchCompileResult multipleInputs = registry.compilePatch(
        {&multipleFlangeInputs, &flangeY,
         {{"flange-y", rws::DesignVariableUnit::Metres, 0.4, "",
           rws::SemanticKind::FlangeTy, "flange-pose:flange"}}}, capabilities);
    REQUIRE(!multipleInputs.ok);
    REQUIRE(hasAdapterDiagnostic(multipleInputs.diagnostics,
                                 "FLANGE_POSE_INDEPENDENT_FLANGE_REQUIRED"));

    const rws::ParameterBinding nonIndependentFlange = poseBinding(
        "bad-flange", rws::SemanticKind::FlangeTx, "FlangePoseAdapter",
        rws::TargetObjectType::Frame, "flange", rws::TargetPropertyId::ParentToFlangeTranslationX,
        "guide", "flange-pose:flange");
    const rws::CanonicalKinematicModel noIndependentFlange = validCanonicalModelFixture();
    const rws::AdapterPatchCompileResult blockedFlange = registry.compilePatch(
        {&noIndependentFlange, &nonIndependentFlange,
         {{"bad-flange", rws::DesignVariableUnit::Metres, 0.1, "",
           rws::SemanticKind::FlangeTx, "flange-pose:flange"}}}, capabilities);
    REQUIRE(!blockedFlange.ok);
    REQUIRE(hasAdapterDiagnostic(blockedFlange.diagnostics,
                                 "FLANGE_POSE_INDEPENDENT_FLANGE_REQUIRED"));

    const rws::ParameterBinding tcpX = poseBinding(
        "tcp-x", rws::SemanticKind::TcpTx, "TcpPoseAdapter", rws::TargetObjectType::ToolBinding,
        "tool", rws::TargetPropertyId::FlangeToTcpTranslationX, "flange", "tcp-pose:tool");
    const rws::AdapterPatchCompileResult tcpWithoutToolArtifacts = registry.compilePatch(
        {&baseline, &tcpX, {{"tcp-x", rws::DesignVariableUnit::Metres, 0.15, "",
                             rws::SemanticKind::TcpTx, "tcp-pose:tool"}}}, capabilities);
    REQUIRE(!tcpWithoutToolArtifacts.ok);
    REQUIRE(hasAdapterDiagnostic(tcpWithoutToolArtifacts.diagnostics,
                                 "ADAPTER_REGISTRY_CAPABILITY_REQUIRED"));
    capabilities.grant(rws::TargetObjectType::ToolBinding, "tool",
                       rws::AdapterCapability::ParameterizedGeometry);
    capabilities.grant(rws::TargetObjectType::ToolBinding, "tool",
                       rws::AdapterCapability::ParameterizedCollision);
    const rws::AdapterPatchCompileResult tcpPatch = registry.compilePatch(
        {&baseline, &tcpX, {{"tcp-x", rws::DesignVariableUnit::Metres, 0.15, "",
                             rws::SemanticKind::TcpTx, "tcp-pose:tool"}}}, capabilities);
    REQUIRE(tcpPatch.ok);
    REQUIRE(tcpPatch.patch.writes.size() == 1);
    REQUIRE(tcpPatch.patch.writes.front().target.objectType == rws::TargetObjectType::ToolBinding);
    REQUIRE(tcpPatch.patch.writes.front().target.objectId == "tool");
    REQUIRE(tcpPatch.patch.writes.front().target.coordinateFrameId == "flange");
    REQUIRE(std::fabs(tcpPatch.patch.writes.front().value.scalarValue - 0.35) < 1e-12);
    rws::ParameterBinding tcpMissingBindingVersion = tcpX;
    tcpMissingBindingVersion.bindingVersion = 0;
    rws::TcpPoseAdapter directTcp;
    const rws::AdapterPatchCompileResult directTcpMalformed = directTcp.compilePatch(
        {&baseline, &tcpMissingBindingVersion,
         {{"tcp-x", rws::DesignVariableUnit::Metres, 0.15, "",
           rws::SemanticKind::TcpTx, "tcp-pose:tool"}}});
    REQUIRE(!directTcpMalformed.ok);
    REQUIRE(hasAdapterDiagnostic(directTcpMalformed.diagnostics,
                                 "PARAMETER_BINDING_VERSION_INVALID"));

    // The typed writer key makes a bad shared TCP/Flange target fail before a
    // candidate is ever created, rather than silently choosing one adapter.
    rws::ParameterBinding flangeClaimsTcp = flangeY;
    flangeClaimsTcp.targetObjectType = rws::TargetObjectType::ToolBinding;
    flangeClaimsTcp.targetObjectId = "tool";
    flangeClaimsTcp.targetPropertyId = rws::TargetPropertyId::FlangeToTcpTranslationX;
    flangeClaimsTcp.coordinateFrameId = "flange";
    flangeClaimsTcp.writeSet = {{rws::TargetObjectType::ToolBinding, "tool",
                                 rws::TargetPropertyId::FlangeToTcpTranslationX, "flange"}};
    rws::DesignVariableDefinition flangeVariable;
    flangeVariable.id = "flange-conflict";
    flangeVariable.bindingId = flangeClaimsTcp.id;
    rws::DesignVariableDefinition tcpVariable;
    tcpVariable.id = "tcp-conflict";
    tcpVariable.bindingId = tcpX.id;
    const rws::WriteSetValidationResult conflict = rws::WriteSetValidator::validate(
        {flangeVariable, tcpVariable}, {flangeClaimsTcp, tcpX});
    REQUIRE(!conflict.valid);
    REQUIRE(hasAdapterDiagnostic(conflict.diagnostics, "PARAMETER_WRITE_CONFLICT"));
}

// =============================================================================
//  测试用例: 验证默认值和验证逻辑
// =============================================================================

static void testProblemDefaultsAndValidation()
{
    std::printf("testProblemDefaultsAndValidation ... ");

    // 用默认构造的问题
    rws::StructureOptimizationProblem problem;

    // ── 验证默认值 ──────────────────────────────────────────────────────
    REQUIRE(problem.weights.reachability == 0.35);
    REQUIRE(problem.weights.manipulability == 0.20);
    REQUIRE(problem.weights.jointMargin == 0.15);
    REQUIRE(problem.weights.collision == 0.15);
    REQUIRE(problem.weights.compactness == 0.10);
    REQUIRE(problem.weights.preference == 0.05);

    REQUIRE(problem.run.candidateCount == 300);
    REQUIRE(problem.run.eliteCount == 20);
    REQUIRE(problem.run.localEliteCount == 5);

    const std::vector< rws::ObjectiveTerm > legacyObjectives =
        rws::StructureOptimizationObjectiveProfile::legacyObjectives(problem.weights);
    REQUIRE(legacyObjectives.size() == 6);
    REQUIRE(legacyObjectives[0].metricId == "kinematics.reachability.weighted");
    REQUIRE(std::abs(legacyObjectives[0].weight - 0.35) < 1e-12);
    REQUIRE(problem.run.finalVerificationCount == 3);
    REQUIRE(problem.run.maxLocalSweeps == 20);
    REQUIRE(problem.run.gridSteps == 3);
    REQUIRE(problem.run.randomSeed == 1u);

    REQUIRE(problem.evaluation.checkCollision == true);

    // ── 运行验证 (空问题应该产生至少一个警告) ──────────────────────────
    std::vector< rws::AnalysisWarning > warnings =
        rws::StructureOptimizationValidation::validateProblem(problem);

    REQUIRE(!warnings.empty());

    // 确认至少有一个 Context.Invalid 警告
    bool foundContextInvalid = false;
    for (const auto& w : warnings)
    {
        if (w.code == "StructureOptimization.Context.Invalid")
        {
            foundContextInvalid = true;
            break;
        }
    }
    REQUIRE(foundContextInvalid);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

static void testPhaseOneTemplatesAndPreflight()
{
    std::printf("testPhaseOneTemplatesAndPreflight ... ");

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec.robotName = "TemplateRobot";
    problem.context.modelSpec.transformJoints.resize(1);
    problem.context.modelProvenance.sourceFingerprint = "source";
    problem.context.modelProvenance.snapshotFingerprint = "snapshot";
    rws::StructureDesignVariable variable;
    variable.id = "link_length";
    variable.label = "Link Length";
    variable.targetName = "link1";
    variable.unit = "m";
    variable.currentValue = 0.5;
    variable.minimum = 0.3;
    variable.maximum = 0.8;
    variable.step = 0.01;
    problem.variables.push_back(variable);
    rws::OptimizationTaskPoint task;
    task.point.id = "task1";
    task.point.enabled = true;
    problem.tasks.push_back(task);

    REQUIRE(rws::StructureOptimizationTemplate::apply(
        rws::StructureOptimizationTemplateKind::ReachabilityFirst, problem));
    REQUIRE(problem.context.modelProvenance.snapshotFingerprint == "snapshot");
    REQUIRE(!problem.objectives.empty());
    REQUIRE(problem.objectives.front().metricId ==
            "kinematics.reachability.weighted");
    REQUIRE(problem.objectives.front().weight > problem.objectives.back().weight);
    REQUIRE(problem.run.candidateCount >= 300);

    const std::vector<rws::StructurePreflightFinding> validFindings =
        rws::StructureOptimizationUiLogic::preflight(problem);
    REQUIRE(std::none_of(validFindings.begin(), validFindings.end(),
                         [](const rws::StructurePreflightFinding& finding) {
                             return finding.severity == rws::AnalysisStatus::Fail;
                         }));

    problem.variables.front().step = 0.0;
    const std::vector<rws::StructurePreflightFinding> invalidFindings =
        rws::StructureOptimizationUiLogic::preflight(problem);
    REQUIRE(std::any_of(invalidFindings.begin(), invalidFindings.end(),
                        [](const rws::StructurePreflightFinding& finding) {
                            return finding.code == "StructureOptimization.Variable.InvalidBounds" &&
                                   finding.severity == rws::AnalysisStatus::Fail;
                        }));

    problem.variables.front().step = 0.0000001;
    problem.run.candidateCount = 1;
    const std::vector<rws::StructurePreflightFinding> largeSearchFindings =
        rws::StructureOptimizationUiLogic::preflight(problem);
    REQUIRE(std::any_of(largeSearchFindings.begin(), largeSearchFindings.end(),
                        [](const rws::StructurePreflightFinding& finding) {
                            return finding.code == "StructureOptimization.Run.SearchSpaceLarge" &&
                                   finding.severity == rws::AnalysisStatus::Warning;
                        }));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

static void testPhaseOneCandidateComparison()
{
    std::printf("testPhaseOneCandidateComparison ... ");
    rws::StructureOptimizationResult result;
    result.baselineCandidateIndex = 0;
    rws::StructureCandidateResult baseline;
    baseline.index = 0;
    baseline.totalScore = 40.0;
    baseline.feasible = false;
    baseline.scores.reachability = 0.4;
    baseline.scores.manipulability = 0.2;
    baseline.raw.totalKinematicLength = 1.2;
    rws::StructureCandidateResult candidate = baseline;
    candidate.index = 7;
    candidate.totalScore = 75.0;
    candidate.feasible = true;
    candidate.scores.reachability = 0.9;
    candidate.scores.manipulability = 0.7;
    candidate.raw.totalKinematicLength = 1.0;
    result.candidates = {baseline, candidate};

    const rws::StructureCandidateComparison comparison =
        rws::StructureCandidateComparison::compare(result, {7});
    REQUIRE(comparison.valid);
    REQUIRE(comparison.rows.size() == 1);
    REQUIRE(comparison.rows.front().candidateIndex == 7);
    REQUIRE(std::abs(comparison.rows.front().scoreDelta - 35.0) < 1e-12);
    REQUIRE(std::abs(comparison.rows.front().reachabilityDelta - 0.5) < 1e-12);
    REQUIRE(std::abs(comparison.rows.front().lengthDelta + 0.2) < 1e-12);

    const rws::StructureCandidateComparison duplicate =
        rws::StructureCandidateComparison::compare(result, {7, 7});
    REQUIRE(!duplicate.valid);
    REQUIRE(duplicate.error == "Candidate selections must be unique.");

    const rws::StructureCandidateComparison missing =
        rws::StructureCandidateComparison::compare(result, {99});
    REQUIRE(!missing.valid);
    REQUIRE(missing.error == "Selected candidate was not found.");

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 设计变量突变器
// =============================================================================

static void testMutator()
{
    std::printf("testMutator ... ");

    // ── Create a model with enough structure for kinematics sync ─────────
    rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    spec.robotName = "TestRobot";
    // 默认模型的第一个旋转关节是 "Joint1", pos[2] = 0.3

    // ── Create a JointPositionZ variable targeting Joint1 ────────────────
    const double j1z = spec.transformJoints[0].pos[2];  // save for baseline check
    rws::StructureDesignVariable var;
    var.id          = "z1";
    var.targetName  = spec.transformJoints[0].name;  // first joint (typically "Base")
    var.kind        = rws::StructureVariableKind::JointPositionZ;
    var.minimum     = -1.0;
    var.maximum     =  1.0;
    var.enabled     = true;

    std::vector< rws::StructureDesignVariable > vars   = {var};
    std::vector< double >                       values = {0.5};

    // ── Apply ────────────────────────────────────────────────────────────
    rws::StructureMutationResult result =
        rws::StructureDesignMutator::apply(spec, vars, values);

    if (!result.ok) {
        std::printf("\n  Mutator apply failed, warnings:");
        for (const auto& w : result.warnings)
            std::printf(" [%s] %s", w.code.c_str(), w.message.c_str());
        std::printf("\n");
    }
    REQUIRE(result.ok);
    REQUIRE(result.spec.transformJoints[0].pos[2] == 0.5);

    // ── Verify baseline was NOT modified ─────────────────────────────────
    REQUIRE(spec.transformJoints[0].pos[2] == j1z);

    // ── Test with missing target ─────────────────────────────────────────
    rws::StructureDesignVariable badVar;
    badVar.id          = "bad";
    badVar.targetName  = "NonExistent";
    badVar.kind        = rws::StructureVariableKind::JointPositionZ;
    badVar.minimum     = -1.0;
    badVar.maximum     =  1.0;
    badVar.enabled     = true;

    auto badResult =
        rws::StructureDesignMutator::apply(spec, {badVar}, {0.5});

    // Missing target warns but doesn't set result.ok = false
    bool foundMissing = false;
    for (const auto& w : badResult.warnings) {
        if (w.code == "StructureOptimization.Variable.MissingTarget") {
            foundMissing = true;
            break;
        }
    }
    REQUIRE(foundMissing);

    // ── Test value out of bounds ─────────────────────────────────────────
    auto outResult =
        rws::StructureDesignMutator::apply(spec, vars, {5.0});
    REQUIRE(!outResult.ok);

    // ── Test mismatched count ────────────────────────────────────────────
    auto cntResult =
        rws::StructureDesignMutator::apply(spec, vars, {});
    REQUIRE(!cntResult.ok);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 目标评分器
// =============================================================================

static void testScorer()
{
    std::printf("testScorer ... ");

    rws::StructureOptimizationProblem problem;
    rws::StructureObjectiveScorer scorer;

    // ── Infeasible candidate (required reachable < required) ─────────────
    rws::StructureCandidateResult infeasible;
    infeasible.index = 0;
    infeasible.raw.modelValid             = true;
    infeasible.raw.requiredReachableCount = 3;
    infeasible.raw.requiredTaskCount      = 5;
    infeasible.raw.collisionFreeRate      = 1.0;

    // ── Feasible candidate ───────────────────────────────────────────────
    rws::StructureCandidateResult feasible;
    feasible.index = 1;
    feasible.raw.modelValid             = true;
    feasible.raw.requiredReachableCount = 5;
    feasible.raw.requiredTaskCount      = 5;
    feasible.raw.collisionFreeRate      = 1.0;
    feasible.raw.jointMarginP10         = 0.15;
    feasible.raw.manipulabilityP10      = 0.05;
    feasible.raw.totalKinematicLength   = 0.5;
    feasible.raw.engineeringPreference  = 0.8;

    scorer.score(problem, infeasible);
    scorer.score(problem, feasible);

    // With no constraints defined, no hard constraints are checked.
    // The scorer always sets feasible = true initially, then loops over
    // constraints. With an empty list, both remain feasible.
    REQUIRE(infeasible.feasible);
    REQUIRE(feasible.feasible);
    REQUIRE(feasible.totalScore >= 0.0);
    REQUIRE(feasible.totalScore <= 100.0);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 带约束的评分:在问题里加一条硬约束 RequiredTaskReachable 后,
// 可达数不足的候选被判 infeasible,可达数足够的候选保持 feasible,
// 且 totalScore 保持在 [0,100] 区间;排序 sortForDecision 保证可行候选排前。
static void testScorerWithConstraints()
{
    std::printf("testScorerWithConstraints ... ");

    rws::StructureOptimizationProblem problem;
    rws::StructureObjectiveScorer scorer;

    // Add RequiredTaskReachable constraint
    rws::StructureConstraint reachCon;
    reachCon.id       = "ReachableReq";
    reachCon.kind     = rws::StructureConstraintKind::RequiredTaskReachable;
    reachCon.hard     = true;
    reachCon.enabled  = true;
    problem.constraints.push_back(reachCon);

    // ── Infeasible candidate ─────────────────────────────────────────────
    rws::StructureCandidateResult infeasible;
    infeasible.index = 0;
    infeasible.raw.modelValid             = true;
    infeasible.raw.requiredReachableCount = 3;
    infeasible.raw.requiredTaskCount      = 5;
    infeasible.raw.collisionFreeRate      = 1.0;

    // ── Feasible candidate ───────────────────────────────────────────────
    rws::StructureCandidateResult feasible;
    feasible.index = 1;
    feasible.raw.modelValid             = true;
    feasible.raw.requiredReachableCount = 5;
    feasible.raw.requiredTaskCount      = 5;
    feasible.raw.collisionFreeRate      = 1.0;
    feasible.raw.jointMarginP10         = 0.15;
    feasible.raw.manipulabilityP10      = 0.05;
    feasible.raw.totalKinematicLength   = 0.5;
    feasible.raw.engineeringPreference  = 0.8;

    scorer.score(problem, infeasible);
    scorer.score(problem, feasible);

    REQUIRE(!infeasible.feasible);
    REQUIRE(feasible.feasible);
    REQUIRE(feasible.totalScore >= 0.0);
    REQUIRE(feasible.totalScore <= 100.0);

    // ── Sort and verify feasible ranks first ─────────────────────────────
    std::vector< rws::StructureCandidateResult > candidates = {infeasible, feasible};
    rws::StructureObjectiveScorer::sortForDecision(candidates);

    REQUIRE(candidates[0].feasible == true);
    REQUIRE(candidates[0].index == 1); // feasible had index 1

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 硬约束检查
// =============================================================================

static void testGenericObjectivesAndConstraints()
{
    std::printf("testGenericObjectivesAndConstraints ... ");

    rws::StructureOptimizationProblem problem;
    problem.objectives = {
        {"structure.preference", rws::OptimizationDirection::Maximize,
         {1.0, 0.0, true}, 1.0, true}
    };
    problem.metricConstraints = {
        {"collision.free_rate", rws::ComparisonOperator::GreaterThanOrEqual,
         0.9, true, true}
    };

    rws::StructureCandidateResult candidate;
    candidate.raw.engineeringPreference = 0.8;
    candidate.raw.collisionFreeRate = 0.8;

    rws::StructureObjectiveScorer scorer;
    scorer.score(problem, candidate);

    REQUIRE(std::abs(candidate.totalScore - 80.0) < 1e-12);
    REQUIRE(!candidate.feasible);
    REQUIRE(candidate.violatedConstraints.size() == 1);
    REQUIRE(candidate.violatedConstraints[0] == "collision.free_rate");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 硬约束:注册八类硬约束(ModelValid / RequiredTaskReachable /
// MinimumJointMargin / MaximumTotalLength / MaximumBaseHeight /
// MinimumWorkspaceCoverage / MaximumCrossSection / MaximumLinkSlenderness),
// 用一个逐项违反的候选验证:被判 infeasible,且 violatedConstraints 完整记录
// 全部八个约束 id,不吞掉任何一项(覆盖的完备性检查)。
static void testHardConstraints()
{
    std::printf("testHardConstraints ... ");

    rws::StructureOptimizationProblem problem;
    rws::StructureObjectiveScorer scorer;

    // Add one constraint of each kind that we can violate
    {
        rws::StructureConstraint c;
        c.id = "ModelValid"; c.kind = rws::StructureConstraintKind::ModelValid;
        c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "RequiredReachable"; c.kind = rws::StructureConstraintKind::RequiredTaskReachable;
        c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MinMargin"; c.kind = rws::StructureConstraintKind::MinimumJointMargin;
        c.threshold = 0.05; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MaxLength"; c.kind = rws::StructureConstraintKind::MaximumTotalLength;
        c.threshold = 2.0; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MaxHeight"; c.kind = rws::StructureConstraintKind::MaximumBaseHeight;
        c.threshold = 1.0; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MinCoverage"; c.kind = rws::StructureConstraintKind::MinimumWorkspaceCoverage;
        c.threshold = 0.5; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MaxCross"; c.kind = rws::StructureConstraintKind::MaximumCrossSection;
        c.threshold = 0.1; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MaxSlender"; c.kind = rws::StructureConstraintKind::MaximumLinkSlenderness;
        c.threshold = 20.0; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }

    // Candidate that violates all constraints
    rws::StructureCandidateResult candidate;
    candidate.raw.modelValid             = false;  // violates ModelValid
    candidate.raw.requiredReachableCount = 0;      // violates RequiredReachable
    candidate.raw.requiredTaskCount      = 5;
    candidate.raw.minimumJointMargin     = 0.01;   // violates MinMargin (< 0.05)
    candidate.raw.totalKinematicLength   = 3.0;    // violates MaxLength (> 2.0)
    candidate.raw.baseHeight             = 2.0;    // violates MaxHeight (> 1.0)
    candidate.raw.workspaceCoverage      = 0.1;    // violates MinCoverage (< 0.5)
    candidate.raw.maxCrossSection        = 0.5;    // violates MaxCross (> 0.1)
    candidate.raw.maxLinkSlenderness     = 50.0;   // violates MaxSlender (> 20.0)
    candidate.raw.collisionFreeRate      = 0.0;

    scorer.score(problem, candidate);

    REQUIRE(!candidate.feasible);
    REQUIRE(candidate.violatedConstraints.size() == 8);

    // Verify specific constraint IDs are present
    bool foundModelValid = false;
    bool foundReachable  = false;
    for (const auto& id : candidate.violatedConstraints) {
        if (id == "ModelValid")        foundModelValid = true;
        if (id == "RequiredReachable") foundReachable  = true;
    }
    REQUIRE(foundModelValid);
    REQUIRE(foundReachable);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 候选解生成器
// =============================================================================

static void testGenerator()
{
    std::printf("testGenerator ... ");

    // ── 两个设计变量 ─────────────────────────────────────────────────
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"var1", "Var 1", "joint1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.0, -10.0, 10.0, 0.5},
        {"var2", "Var 2", "joint2", "mm",
         rws::StructureVariableKind::JointPositionY,
         0.0,  -5.0,  5.0, 0.25}
    };

    // ── randomUniform: 10 个候选, seed=42 ───────────────────────────
    auto candidates1 = rws::StructureCandidateGenerator::randomUniform(
        problem.variables, 10, 42);
    REQUIRE(candidates1.size() == 10);
    REQUIRE(candidates1[0].size() == 2);

    // ── 相同 seed 应得到相同结果 ─────────────────────────────────────
    auto candidates2 = rws::StructureCandidateGenerator::randomUniform(
        problem.variables, 10, 42);
    REQUIRE(candidates2.size() == 10);
    bool same = true;
    for (int i = 0; i < 10 && same; ++i)
    {
        for (std::size_t j = 0; j < 2 && same; ++j)
        {
            if (std::abs(candidates1[i][j] - candidates2[i][j]) > 1e-12)
                same = false;
        }
    }
    REQUIRE(same);

    // ── latinHypercube: 10 个候选 ───────────────────────────────────
    auto lhs = rws::StructureCandidateGenerator::latinHypercube(
        problem.variables, 10, 42);
    REQUIRE(lhs.size() == 10);
    REQUIRE(lhs[0].size() == 2);

    // ── quantize ─────────────────────────────────────────────────────
    {
        rws::StructureDesignVariable v;
        v.minimum = 0.0;
        v.maximum = 10.0;
        v.step    = 3.0;

        // round(3.2/3.0)*3 = 1*3 = 3.0
        double q = rws::StructureCandidateGenerator::quantize(3.2, v);
        REQUIRE(std::abs(q - 3.0) < 1e-12);

        // round(5.5/3.0)*3 = 2*3 = 6.0
        q = rws::StructureCandidateGenerator::quantize(5.5, v);
        REQUIRE(std::abs(q - 6.0) < 1e-12);

        // round(11.0/3.0)*3 = 12, clamped to 10.0
        q = rws::StructureCandidateGenerator::quantize(11.0, v);
        REQUIRE(std::abs(q - 10.0) < 1e-12);
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 候选解缓存
// =============================================================================

static void testCache()
{
    std::printf("testCache ... ");

    rws::StructureCandidateCache cache;

    // ── 一个变量的问题 ───────────────────────────────────────────────
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"var1", "Var 1", "joint1", "mm",
         rws::StructureVariableKind::JointPositionX,
         5.0, 0.0, 10.0, 1.0}
    };

    // ── 存入一个结果 ──────────────────────────────────────────────────
    rws::StructureCandidateResult result;
    result.index      = 0;
    result.status     = rws::StructureCandidateStatus::Feasible;
    result.feasible   = true;
    result.totalScore = 0.85;
    problem.evaluation.checkCollision = true;

    std::vector<double> values = {5.0};
    cache.put(problem, values, rws::StructureEvaluationStage::Quick, result);
    REQUIRE(cache.size() == 1);

    // ── 查找应命中 ───────────────────────────────────────────────────
    rws::StructureCandidateResult found;
    bool foundResult = cache.find(problem, values,
                                  rws::StructureEvaluationStage::Quick, found);
    REQUIRE(foundResult);
    REQUIRE(found.feasible);
    REQUIRE(std::abs(found.totalScore - 0.85) < 1e-12);
    REQUIRE(cache.hitCount() == 1);

    // ── 改变 checkCollision → 应未命中 ──────────────────────────────
    problem.evaluation.checkCollision = false;
    foundResult = cache.find(problem, values,
                             rws::StructureEvaluationStage::Quick, found);
    REQUIRE(!foundResult);
    REQUIRE(cache.hitCount() == 1);

    problem.evaluation.checkCollision = true;
    problem.context.modelSpec.robotName = "ChangedModel";
    foundResult = cache.find(problem, values,
                             rws::StructureEvaluationStage::Quick, found);
    REQUIRE(!foundResult);
    REQUIRE(cache.hitCount() == 1);

    // ── clear ────────────────────────────────────────────────────────
    // Workspace sampling and the coverage box change evaluation results.
    cache.clear();
    problem.context.modelSpec.robotName.clear();
    problem.evaluation.coverageBox.enabled = true;
    problem.evaluation.coverageBox.cells = {{2, 2, 2}};
    problem.evaluation.quickWorkspace.sampleCount = 8;
    cache.put(problem, values, rws::StructureEvaluationStage::Quick, result);
    REQUIRE(cache.find(problem, values, rws::StructureEvaluationStage::Quick, found));
    problem.evaluation.coverageBox.cells[0] = 3;
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Quick, found));
    problem.evaluation.coverageBox.cells[0] = 2;
    problem.evaluation.quickWorkspace.randomSeed = 2u;
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Quick, found));

    // 多区域覆盖率与冻结场景都会改变候选的实际评价环境，缓存绝不能把它们当作同一个
    // 问题复用。该断言保护 problemToJson() 参与缓存键的契约，防止后续精简序列化时
    // 意外遗漏新增区域或工装快照。
    cache.clear();
    problem.evaluation.quickWorkspace.randomSeed = 1u;
    problem.evaluation.coverageBoxes.clear();
    rws::WorkspaceCoverageBox regionalBox;
    regionalBox.id = "fixture_area";
    regionalBox.referenceFrame = "Fixture_A";
    regionalBox.enabled = true;
    regionalBox.cells = {{2, 2, 2}};
    problem.evaluation.coverageBoxes.push_back(regionalBox);
    problem.scenarioSnapshot.schemaVersion = 1;
    problem.scenarioSnapshot.snapshotFingerprint = "scenario-fingerprint-a";
    cache.put(problem, values, rws::StructureEvaluationStage::Quick, result);
    REQUIRE(cache.find(problem, values, rws::StructureEvaluationStage::Quick, found));
    problem.evaluation.coverageBoxes[0].cells[2] = 3;
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Quick, found));
    problem.evaluation.coverageBoxes[0].cells[2] = 2;
    problem.scenarioSnapshot.snapshotFingerprint = "scenario-fingerprint-b";
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Quick, found));

    cache.clear();
    problem.scenarioSnapshot.snapshotFingerprint = "scenario-fingerprint-a";
    rws::RequirementExecutionRegion cachedRegion;
    cachedRegion.id = "cached-verified-region";
    cachedRegion.refFrame = "WORLD";
    cachedRegion.minimumVerificationStage = rws::RequirementExecutionStage::Verified;
    problem.requirementExecution.provenance.requirementFingerprint =
        "cache-requirement-fingerprint";
    problem.requirementExecution.workspaceRegions.push_back(cachedRegion);
    cache.put(problem, values, rws::StructureEvaluationStage::Verified, result);
    REQUIRE(cache.find(problem, values, rws::StructureEvaluationStage::Verified, found));
    problem.requirementExecution.workspaceRegions.front().directionSamples = 2;
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Verified, found));

    cache.clear();
    REQUIRE(cache.size() == 0);
    REQUIRE(cache.hitCount() == 0);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 候选模型工厂
// =============================================================================

static void testModelFactory()
{
    std::printf("testModelFactory ... ");

    // ── 使用默认六轴模型(确保 saveFiles 能正常工作) ─────────────────────
    rws::RobotModelSpec spec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    spec.robotName = "TestRobot";

    // ── 构建请求 ─────────────────────────────────────────────────────────
    rws::CandidateModelBuildRequest req;
    req.spec         = spec;
    req.deviceName   = "TestRobot";   // 与 robotName 一致
    req.checkCollision = false;       // 简化测试,不加载碰撞检测

    // ── 执行工厂 ─────────────────────────────────────────────────────────
    rws::CandidateModelFactory factory;
    rws::CandidateModelBuildResult result = factory.build(req);

    REQUIRE(result.ok);
    REQUIRE(!result.artifact.workcell.isNull());
    REQUIRE(!result.artifact.device.isNull());

    // TCP 帧应回退到 device->getEnd()
    REQUIRE(!result.artifact.tcpFrame.isNull());

    // 无碰撞检测时应为 null
    REQUIRE(result.artifact.collisionDetector.isNull());

    // 临时目录应有效
    REQUIRE(result.artifact.temporaryDirectory.get() != nullptr);
    REQUIRE(result.artifact.temporaryDirectory->isValid());

    // 冻结场景必须与变异后的机器人共同出现在候选 WorkCell。这里不通过提前换算
    // WORLD 位姿来规避工装，而是断言 Fixture_A 被真实写入候选场景，供后续 IK/
    // 碰撞评价按同一个 Frame 名称解析。
    rws::StructureOptimizationScenarioSnapshot scenario;
    scenario.schemaVersion = 1;
    scenario.snapshotFingerprint = "scenario-test-fingerprint";
    scenario.sceneSpec.robotName = "FrozenCell";
    rws::FrameSpec fixture;
    fixture.name = "Fixture_A";
    fixture.refFrame = "WORLD";
    fixture.pos = {{0.4, 0.0, 0.2}};
    scenario.sceneSpec.sceneFrames.push_back(fixture);
    QTemporaryDir clonedScenarioRoot;
    REQUIRE(clonedScenarioRoot.isValid());
    REQUIRE(QDir().mkpath(clonedScenarioRoot.filePath("assets")));
    const QString sourceMesh = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/geometry/base.stl");
    const QString clonedMesh = clonedScenarioRoot.filePath("assets/base.stl");
    REQUIRE(QFile::copy(sourceMesh, clonedMesh));
    scenario.sceneSpec.saveDirectory = "scenes";
    rws::SceneGeometrySpec fixtureMesh;
    fixtureMesh.name = "FixtureMesh";
    fixtureMesh.refFrame = "Fixture_A";
    fixtureMesh.kind = rws::GeometryKind::Polytope;
    fixtureMesh.file = "assets/base.stl";
    fixtureMesh.collisionModel = false;
    scenario.sceneSpec.sceneGeometries.push_back(fixtureMesh);
    req.scenarioSnapshot = &scenario;
    req.scenarioBaseDirectory = clonedScenarioRoot.path().toStdString();
    result = factory.build(req);
    REQUIRE(result.ok);
    REQUIRE(result.artifact.workcell->findFrame("Fixture_A") != nullptr);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  Fake evaluator for optimizer testing
// =============================================================================

// 二次型假评价器:得分 = 100 - 10 * Σ(变量值-偏好值)²,全部可行。
// 用于优化器/灵敏度测试——"越接近偏好值越好"的解析行为让断言可精确计算。
class QuadraticFakeEvaluator : public rws::IStructureCandidateEvaluator {
  public:
    void evaluate(
        const rws::StructureOptimizationProblem& problem,
        rws::StructureCandidateResult& candidate,
        rws::StructureEvaluationStage stage,
        const rws::StructureOptimizationCallbacks& callbacks,
        rws::StructureCandidateCache* cache) override
    {
        candidate.stage = stage;

        // Quadratic penalty: closer to preferred values is better
        double error = 0.0;
        for (std::size_t i = 0; i < candidate.values.size() && i < problem.variables.size(); ++i)
        {
            double diff = candidate.values[i] - problem.variables[i].preferredValue;
            error += diff * diff;
        }

        candidate.totalScore = std::max(0.0, 100.0 - error * 10.0);
        candidate.feasible   = true;
        candidate.status     = rws::StructureCandidateStatus::Feasible;

        // Populate minimal raw metrics
        candidate.raw.modelValid             = true;
        candidate.raw.requiredReachableCount = 5;
        candidate.raw.requiredTaskCount      = 5;
        candidate.raw.weightedReachability   = 1.0;
        candidate.raw.manipulabilityP10      = 0.01;
        candidate.raw.jointMarginP10         = 0.1;
        candidate.raw.collisionFreeRate      = 1.0;
        candidate.raw.totalKinematicLength   = 1.0;

        rws::StructureObjectiveScorer scorer;
        scorer.score(problem, candidate);
    }
};

// 全不可行假评价器:任何候选都判 Infeasible、得分 0。
// 专用于验证"无可行解"场景下优化器与灵敏度的兜底行为。
class AlwaysInfeasibleEvaluator : public rws::IStructureCandidateEvaluator {
  public:
    void evaluate(
        const rws::StructureOptimizationProblem&,
        rws::StructureCandidateResult& candidate,
        rws::StructureEvaluationStage stage,
        const rws::StructureOptimizationCallbacks&,
        rws::StructureCandidateCache*) override
    {
        candidate.stage = stage;
        candidate.feasible = false;
        candidate.totalScore = 0.0;
        candidate.status = rws::StructureCandidateStatus::Infeasible;
    }
};

// 流水线工件生产者:记录执行顺序,产出 ik.solutions 工件与可达性指标。
// 用于验证 EngineeringEvaluatorPipeline 能按工件依赖自动拓扑排序。
class PipelineArtifactProducer : public rws::IEngineeringEvaluator {
  public:
    explicit PipelineArtifactProducer(std::vector<std::string>* executionOrder)
        : _executionOrder(executionOrder)
    {}

    std::string id() const override { return "test.producer"; }
    std::string version() const override { return "1"; }
    std::vector<std::string> providedArtifactIds() const override
    {
        return {"ik.solutions"};
    }

    rws::EngineeringEvaluationResult evaluate(
        const rws::CandidateEvaluationContext& candidate,
        const rws::EvaluationRequest&,
        const rws::EvaluationCallbacks&) override
    {
        _executionOrder->push_back(id());
        rws::EngineeringEvaluationResult result;
        result.providerId = id();
        result.providerVersion = version();
        result.status = rws::EngineeringEvaluationStatus::Success;
        result.inputSnapshot = candidate.inputSnapshot;
        result.artifacts.push_back({"ik.solutions", "application/json", "[]"});
        result.metrics.push_back({"kinematics.reachability.weighted", 1.0, "ratio",
                                  rws::EngineeringMetricStatus::Valid, id()});
        return result;
    }

  private:
    std::vector<std::string>* _executionOrder;
};

// 流水线工件消费者:记录执行顺序,要求 ik.solutions 工件——请求中缺失该工件时
// 返回 DataInsufficient;具备时产出轨迹可行性约束。
class PipelineArtifactConsumer : public rws::IEngineeringEvaluator {
  public:
    explicit PipelineArtifactConsumer(std::vector<std::string>* executionOrder)
        : _executionOrder(executionOrder)
    {}

    std::string id() const override { return "test.consumer"; }
    std::string version() const override { return "1"; }
    std::vector<std::string> requiredArtifactIds() const override
    {
        return {"ik.solutions"};
    }

    rws::EngineeringEvaluationResult evaluate(
        const rws::CandidateEvaluationContext& candidate,
        const rws::EvaluationRequest& request,
        const rws::EvaluationCallbacks&) override
    {
        _executionOrder->push_back(id());
        rws::EngineeringEvaluationResult result;
        result.providerId = id();
        result.providerVersion = version();
        result.inputSnapshot = candidate.inputSnapshot;
        result.status = rws::EngineeringEvaluationStatus::Success;
        const bool hasIkArtifact = std::any_of(
            request.inputArtifacts.begin(), request.inputArtifacts.end(),
            [](const rws::EngineeringArtifact& artifact) {
                return artifact.artifactId == "ik.solutions";
            });
        if (!hasIkArtifact) {
            result.status = rws::EngineeringEvaluationStatus::DataInsufficient;
            return result;
        }
        result.constraints.push_back({"trajectory.feasible", "==", 1.0, 1.0,
                                      true, true, ""});
        return result;
    }

  private:
    std::vector<std::string>* _executionOrder;
};

// 子套件 工程评价流水线:验证 EngineeringEvaluatorPipeline 按工件依赖自动
// 拓扑排序——即使先 addEvaluator(consumer) 再 addEvaluator(producer),执行时
// producer 必须先运行产出 ik.solutions,consumer 才能消费(顺序为 producer→consumer);
// 只挂 consumer 而缺少提供者时返回 DataInsufficient 且不执行任何环节。
static void testEngineeringEvaluatorPipeline()
{
    std::printf("testEngineeringEvaluatorPipeline ... ");

    std::vector<std::string> order;
    PipelineArtifactProducer producer(&order);
    PipelineArtifactConsumer consumer(&order);
    rws::EngineeringEvaluatorPipeline pipeline;
    pipeline.addEvaluator(consumer);
    pipeline.addEvaluator(producer);

    rws::CandidateEvaluationContext candidate;
    candidate.inputSnapshot.modelHash = "model";
    rws::EngineeringEvaluationResult result = pipeline.evaluate(
        candidate, rws::EvaluationRequest(), rws::EvaluationCallbacks());

    REQUIRE(result.status == rws::EngineeringEvaluationStatus::Success);
    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == "test.producer");
    REQUIRE(order[1] == "test.consumer");
    REQUIRE(result.artifacts.size() == 1);
    REQUIRE(result.constraints.size() == 1);

    rws::EngineeringEvaluatorPipeline incompletePipeline;
    incompletePipeline.addEvaluator(consumer);
    order.clear();
    result = incompletePipeline.evaluate(
        candidate, rws::EvaluationRequest(), rws::EvaluationCallbacks());
    REQUIRE(result.status == rws::EngineeringEvaluationStatus::DataInsufficient);
    REQUIRE(order.empty());

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 系统指标假评价器:完整输出运动学/碰撞/几何/偏好十二项工程指标,
// 并记录收到的 modelHash 以便断言优化器喂入的模型快照正确。
class SystemMetricsEvaluator : public rws::IEngineeringEvaluator {
  public:
    std::string lastModelHash;

    std::string id() const override { return "test.system-metrics"; }
    std::string version() const override { return "1"; }

    rws::EngineeringEvaluationResult evaluate(
        const rws::CandidateEvaluationContext& candidate,
        const rws::EvaluationRequest&,
        const rws::EvaluationCallbacks&) override
    {
        lastModelHash = candidate.inputSnapshot.modelHash;
        rws::EngineeringEvaluationResult result;
        result.providerId = id();
        result.providerVersion = version();
        result.status = rws::EngineeringEvaluationStatus::Success;
        result.inputSnapshot = candidate.inputSnapshot;
        const std::vector<rws::EngineeringMetric> metrics = {
            {"kinematics.reachability.weighted", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"kinematics.manipulability.p10", 0.01, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"kinematics.joint_margin.p10", 0.2, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"kinematics.joint_margin.minimum", 0.2, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"kinematics.workspace.coverage", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"collision.free_rate", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.compactness", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.kinematic_length", 0.8, "m", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.base_height", 0.2, "m", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.cross_section.maximum", 0.01, "m2", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.link_slenderness.maximum", 5.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"structure.preference", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()}};
        result.metrics = metrics;
        return result;
    }
};

// 子套件 系统级优化器:用真实指标评价器(SystemMetricsEvaluator)驱动
// SystemEngineeringOptimizer,验证候选数(基线 + 3)、未被取消、存在最佳候选,
// 且评价器拿到的 modelHash 与 RobotModelFingerprint 计算的模型指纹一致——
// 证明优化器把正确的模型快照喂给了工程评价环节。
static void testSystemEngineeringOptimizer()
{
    std::printf("testSystemEngineeringOptimizer ... ");

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.modelSpec.robotName = "SystemHashRobot";
    problem.variables = {{"x", "X", "joint1", "m",
                          rws::StructureVariableKind::JointPositionX,
                          0.0, -1.0, 1.0, 0.1, 0.0, 0.0}};
    problem.run.strategy = rws::StructureStrategyKind::Random;
    problem.run.candidateCount = 3;
    problem.run.randomSeed = 8;

    SystemMetricsEvaluator evaluator;
    rws::EngineeringEvaluatorPipeline pipeline;
    pipeline.addEvaluator(evaluator);
    rws::SystemEngineeringOptimizer optimizer;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };
    const rws::StructureOptimizationResult result =
        optimizer.optimize(problem, pipeline, callbacks);

    REQUIRE(!result.canceled);
    REQUIRE(result.candidates.size() == 4);
    REQUIRE(result.bestCandidateIndex >= 0);
    REQUIRE(evaluator.lastModelHash ==
            rws::RobotModelFingerprint::canonicalSha256(problem.context.modelSpec));
    for (const rws::StructureCandidateResult& candidate : result.candidates) {
        REQUIRE(candidate.feasible);
        REQUIRE(candidate.status == rws::StructureCandidateStatus::Feasible);
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 候选解评估器 (接口验证)
// =============================================================================

static void testEvaluator()
{
    std::printf("testEvaluator ... ");

    // Minimal problem — no valid context, so the real evaluator's mutator
    // should fail quickly and return a Failed candidate.
    rws::StructureOptimizationProblem problem;
    rws::KinematicEngineeringEvaluator evaluator(problem);
    rws::StructureCandidateResult     candidate;

    candidate.index  = 0;
    candidate.values = {};

    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };

    evaluator.evaluateCandidate(candidate, rws::StructureEvaluationStage::Quick,
                                callbacks, nullptr);

    // Without a valid model spec, the mutator returns ok=false → Failed
    REQUIRE(candidate.status == rws::StructureCandidateStatus::Failed);

    rws::KinematicEngineeringEvaluator engineeringEvaluator(problem);
    rws::CandidateEvaluationContext context;
    context.variableValues = candidate.values;
    const rws::EngineeringEvaluationResult engineeringResult =
        engineeringEvaluator.evaluate(context, rws::EvaluationRequest(),
                                      rws::EvaluationCallbacks());
    REQUIRE(engineeringResult.status == rws::EngineeringEvaluationStatus::Failed);
    REQUIRE(engineeringResult.providerId == "structure.kinematics");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 混合优化器 (使用 Fake 评估器)
// =============================================================================

static void testOptimizer()
{
    std::printf("testOptimizer ... ");

    // ── Problem: 2 design variables, Hybrid strategy ───────────────────
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "joint1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.1,
         0.3, 0.5},   // preferred = 0.3, weight = 0.5
        {"y", "Y", "joint2", "mm",
         rws::StructureVariableKind::JointPositionY,
         0.0, -1.0, 1.0, 0.1,
         -0.2, 0.5}   // preferred = -0.2, weight = 0.5
    };

    problem.run.strategy         = rws::StructureStrategyKind::Hybrid;
    problem.run.candidateCount   = 30;
    problem.run.eliteCount       = 5;
    problem.run.localEliteCount  = 3;
    problem.run.maxLocalSweeps   = 6;
    problem.run.randomSeed       = 42;

    // Add a RequiredTaskReachable constraint for feasibility testing
    rws::StructureConstraint reachCon;
    reachCon.id      = "Reachable";
    reachCon.kind    = rws::StructureConstraintKind::RequiredTaskReachable;
    reachCon.hard    = true;
    reachCon.enabled = true;
    problem.constraints.push_back(reachCon);

    // ── Run optimization ──────────────────────────────────────────────
    QuadraticFakeEvaluator                    fakeEval;
    rws::HybridStructureOptimizer             optimizer;
    rws::StructureOptimizationCallbacks  callbacks;
    callbacks.isCancellationRequested = []() { return false; };

    rws::StructureOptimizationResult result = optimizer.optimize(
        problem, fakeEval, callbacks);
    rws::StructureOptimizationResult repeated = optimizer.optimize(
        problem, fakeEval, callbacks);

    // ── Assertions ────────────────────────────────────────────────────
    REQUIRE(!result.canceled);
    REQUIRE(result.baselineCandidateIndex == 0);
    REQUIRE(!result.candidates.empty());
    REQUIRE(result.diagnostics.generatedCandidates > 0);
    REQUIRE(result.diagnostics.evaluatedCandidates > 0);
    REQUIRE(result.diagnostics.totalSeconds >= 0.0);

    // At least one feasible candidate should exist
    bool foundFeasible = false;
    for (const auto& c : result.candidates)
        if (c.feasible) { foundFeasible = true; break; }
    REQUIRE(foundFeasible);
    REQUIRE(result.candidates.size() == repeated.candidates.size());
    for (std::size_t i = 0; i < result.candidates.size() &&
                            i < repeated.candidates.size(); ++i) {
        REQUIRE(result.candidates[i].values == repeated.candidates[i].values);
        REQUIRE(result.candidates[i].feasible == repeated.candidates[i].feasible);
        REQUIRE(std::abs(result.candidates[i].totalScore -
                         repeated.candidates[i].totalScore) < 1e-12);
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 灵敏度分析器
// =============================================================================

//! 简化的模拟评估器: 越偏离 0 则得分越低, >0.8 则不可行。
// 子套件 混合优化工作流:驱动 HybridStructureOptimizer 完成 Quick→Local→
// FinalVerified 的完整过程,验证进度回调的 Local/FinalVerified 计划数、诊断计数
// (quickEvaluated=6, verifiedElite=4, finalVerified=1)以及灵敏度条目数,并保证
// 两次相同输入的优化结果(最佳候选、灵敏度条目与降分)完全确定、可复现。
static void testHybridVerificationAndSensitivityWorkflow()
{
    std::printf("testHybridVerificationAndSensitivityWorkflow ... ");

    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "joint1", "m", rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.1, 0.2, 0.5},
        {"y", "Y", "joint2", "m", rws::StructureVariableKind::JointPositionY,
         0.0, -1.0, 1.0, 0.1, -0.2, 0.5}
    };
    problem.run.strategy = rws::StructureStrategyKind::Hybrid;
    problem.run.candidateCount = 4;
    problem.run.eliteCount = 4;
    problem.run.localEliteCount = 2;
    problem.run.finalVerificationCount = 1;
    problem.run.maxLocalSweeps = 2;
    problem.run.randomSeed = 123u;

    std::vector<rws::StructureProgress> progress;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };
    callbacks.onProgress = [&progress](const rws::StructureProgress& value) {
        progress.push_back(value);
    };

    QuadraticFakeEvaluator evaluator;
    rws::HybridStructureOptimizer optimizer;
    const rws::StructureOptimizationResult result =
        optimizer.optimize(problem, evaluator, callbacks);
    QuadraticFakeEvaluator repeatedEvaluator;
    const rws::StructureOptimizationResult repeated =
        optimizer.optimize(problem, repeatedEvaluator, callbacks);

    int localPlanned = -1;
    int finalVerifiedCompleted = 0;
    int finalVerifiedPlanned = -1;
    for (const rws::StructureProgress& value : progress) {
        if (value.stage == "Local")
            localPlanned = value.planned;
        if (value.stage == "FinalVerified") {
            finalVerifiedCompleted = value.completed;
            finalVerifiedPlanned = value.planned;
        }
    }

    REQUIRE(!result.canceled);
    REQUIRE(localPlanned == 2);
    REQUIRE(finalVerifiedPlanned == 1);
    REQUIRE(finalVerifiedCompleted == 1);
    REQUIRE(result.diagnostics.quickEvaluatedCandidates == 6);
    REQUIRE(result.diagnostics.verifiedEliteCandidates == 4);
    REQUIRE(result.diagnostics.finalVerifiedCandidates == 1);
    REQUIRE(result.diagnostics.sensitivityEvaluations ==
            static_cast<int>(result.sensitivity.entries.size()));
    REQUIRE(result.bestCandidateIndex >= 0);
    REQUIRE(!result.sensitivity.entries.empty());
    REQUIRE(result.bestCandidateIndex == repeated.bestCandidateIndex);
    REQUIRE(result.sensitivity.entries.size() == repeated.sensitivity.entries.size());
    for (std::size_t i = 0; i < result.sensitivity.entries.size() &&
                            i < repeated.sensitivity.entries.size(); ++i) {
        REQUIRE(result.sensitivity.entries[i].variableId ==
                repeated.sensitivity.entries[i].variableId);
        REQUIRE(std::abs(result.sensitivity.entries[i].scoreDrop -
                         repeated.sensitivity.entries[i].scoreDrop) < 1e-12);
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 无可行解的灵敏度兜底:当评价器使所有候选都不可行时,优化结果不得
// 谎报最佳候选(bestCandidateIndex == -1),灵敏度等级必须为 Unknown,
// 并给出 StructureOptimization.NoFeasibleCandidate 告警,供 UI 展示原因。
static void testNoFeasibleCandidateLeavesSensitivityUnknown()
{
    std::printf("testNoFeasibleCandidateLeavesSensitivityUnknown ... ");

    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "joint1", "m", rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.1}
    };
    problem.run.strategy = rws::StructureStrategyKind::Hybrid;
    problem.run.candidateCount = 2;
    problem.run.eliteCount = 1;
    problem.run.localEliteCount = 1;
    problem.run.finalVerificationCount = 1;

    AlwaysInfeasibleEvaluator evaluator;
    rws::HybridStructureOptimizer optimizer;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };
    const rws::StructureOptimizationResult result =
        optimizer.optimize(problem, evaluator, callbacks);

    const bool hasNoFeasibleWarning = std::any_of(
        result.warnings.begin(), result.warnings.end(),
        [](const rws::AnalysisWarning& warning) {
            return warning.code == "StructureOptimization.NoFeasibleCandidate";
        });
    REQUIRE(result.bestCandidateIndex == -1);
    REQUIRE(result.sensitivity.robustnessGrade == "Unknown");
    REQUIRE(hasNoFeasibleWarning);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 灵敏度假评价器:值越偏离 0 得分越低,任一变量 |v|>0.8 即不可行。
// 与 QuadraticFakeEvaluator 类似但解析更简单,便于手工推算扰动后的降分。
struct SensitivityMockEvaluator : public rws::IStructureCandidateEvaluator {
    void evaluate(
        const rws::StructureOptimizationProblem& problem,
        rws::StructureCandidateResult& candidate,
        rws::StructureEvaluationStage stage,
        const rws::StructureOptimizationCallbacks& callbacks,
        rws::StructureCandidateCache* cache) override
    {
        (void)problem; (void)callbacks; (void)cache;
        candidate.stage   = stage;
        candidate.feasible = true;
        candidate.totalScore = 100.0;
        for (double v : candidate.values) {
            if (v > 0.8 || v < -0.8) {
                candidate.feasible = false;
                candidate.violatedConstraints.push_back("OutOfRange");
                candidate.totalScore = 0.0;
                candidate.status = rws::StructureCandidateStatus::Infeasible;
                return;
            }
            candidate.totalScore -= std::abs(v) * 10.0;
        }
        candidate.status = rws::StructureCandidateStatus::Feasible;
    }
};

// 子套件 灵敏度分析:用 SensitivityMockEvaluator 在最佳候选 x=0,y=0 上做扰动,
// 验证每个变量生成 +step/-step 两个扰动条目(共 4 个),最大降分 = 2 → 等级 "A",
// 且没有关键变量——关键变量列表只在降分超过阈值时才填充。
static void testSensitivity()
{
    std::printf("testSensitivity ... ");

    // 问题: 两个设计变量
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "j1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.2, 0.0, 0.0, true, false},
        {"y", "Y", "j2", "mm",
         rws::StructureVariableKind::JointPositionY,
         0.0, -1.0, 1.0, 0.2, 0.0, 0.0, true, false}
    };

    // 最佳候选: x=0.0, y=0.0 (全零, score=100)
    rws::StructureCandidateResult best;
    best.index  = 0;
    best.values = {0.0, 0.0};
    best.feasible  = true;
    best.totalScore = 100.0;

    // 执行分析
    SensitivityMockEvaluator mockEval;
    rws::StructureSensitivityAnalyzer analyzer;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };

    rws::StructureSensitivityResult result =
        analyzer.analyze(problem, best, mockEval, callbacks, nullptr);

    // 断言:
    // 每个变量有 2 个扰动方向, 共 4 个 entry
    REQUIRE(result.entries.size() == 4);

    // x = 0, perturb +0.2 -> score = 100 - 0.2*10 = 98, drop = 2
    // y = 0, perturb +0.2 -> score = 100 - 0.2*10 = 98, drop = 2
    // 所以 maxDrop = 2, grade = "A"
    REQUIRE(result.maximumScoreDrop > 0.0);
    REQUIRE(result.maximumScoreDrop <= 2.0 + 1e-12);
    REQUIRE(result.robustnessGrade == "A");

    // 无关键变量 (所有 drop <= 2)
    REQUIRE(result.criticalVariableIds.empty());

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: JSON 序列化往返
// =============================================================================

// 子套件 灵敏度取消:验证取消回调在第三次检查时触发后,灵敏度分析立即停止
// (只剩 1 个扰动条目),整体等级保持 "Unknown"——取消必须贯穿扰动循环,
// 而不是等到分析完成后再检查一次。
static void testSensitivityStopsAfterCancellation()
{
    std::printf("testSensitivityStopsAfterCancellation ... ");

    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "j1", "m", rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.2},
        {"y", "Y", "j2", "m", rws::StructureVariableKind::JointPositionY,
         0.0, -1.0, 1.0, 0.2}
    };
    rws::StructureCandidateResult best;
    best.values = {0.0, 0.0};
    best.feasible = true;
    best.totalScore = 100.0;

    int cancellationChecks = 0;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = [&cancellationChecks]() {
        ++cancellationChecks;
        return cancellationChecks >= 3;
    };

    SensitivityMockEvaluator evaluator;
    rws::StructureSensitivityAnalyzer analyzer;
    const rws::StructureSensitivityResult result = analyzer.analyze(
        problem, best, evaluator, callbacks, nullptr);

    REQUIRE(result.entries.size() == 1);
    REQUIRE(cancellationChecks >= 2);
    REQUIRE(result.robustnessGrade == "Unknown");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 问题 JSON 往返:构造带变量(离散域)、约束、运行参数、快速/验证工作区
// 采样、覆盖盒、目标、指标约束与需求溯源(requirementProvenance)的完整问题,
// 序列化后反序列化并逐字段比对,保证 v2 schema 不丢字段;报告保留冻结姿态的
// 解析来源;旧 v1 权重 JSON 能迁移到 v2 objectives 且不启用覆盖盒、快速采样回退
// 默认 1000。
static void testJsonRoundTrip()
{
    std::printf("testJsonRoundTrip ... ");

    // 创建填充的问题
    rws::StructureOptimizationProblem problem;
    problem.context.projectName = "TestProj";
    problem.context.robotName   = "TestRobot";

    problem.variables = {
        {"a", "Var A", "j1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.5, -1.0, 1.0, 0.1, 0.3, 0.5, true, false}
    };
    problem.variables[0].domainDefinition.domain = rws::DesignVariableDomain::Discrete;
    problem.variables[0].domainDefinition.discreteOptions = {"0.4", "0.5", "0.6"};

    problem.constraints = {
        {"c1", "Constraint 1", "", rws::StructureConstraintKind::ModelValid,
         0.0, 0.0, true, true}
    };

    problem.run.candidateCount = 100;
    problem.run.randomSeed     = 42;
    problem.evaluation.evaluatorId = "test.kinematics";
    problem.evaluation.evaluatorVersion = "2.0";
    problem.evaluation.quickWorkspace.mode = rws::WorkspaceSamplingMode::Grid;
    problem.evaluation.quickWorkspace.sampleCount = 123;
    problem.evaluation.quickWorkspace.gridStepsPerJoint = 4;
    problem.evaluation.quickWorkspace.checkCollision = false;
    problem.evaluation.quickWorkspace.randomSeed = 9u;
    problem.evaluation.verifiedWorkspace.sampleCount = 456;
    problem.evaluation.verifiedWorkspace.randomSeed = 11u;
    problem.evaluation.coverageBox.enabled = true;
    problem.evaluation.coverageBox.minimum = {{-0.5, -0.4, 0.1}};
    problem.evaluation.coverageBox.maximum = {{0.5, 0.6, 1.1}};
    problem.evaluation.coverageBox.cells = {{2, 3, 4}};
    problem.objectives = {
        {"kinematics.reachability.weighted", rws::OptimizationDirection::Maximize,
         {1.0, 0.0, true}, 0.7, true}
    };
    problem.metricConstraints = {
        {"collision.free_rate", rws::ComparisonOperator::GreaterThanOrEqual,
         0.95, true, true}
    };
    // 冻结需求的审计身份必须随优化项目往返，确保导出的项目不会丢失其输入依据。
    // 显式填写每个字段，避免审计结构新增成员后，聚合初始化的成员顺序在测试中被悄然误用。
    problem.requirementProvenance.requirementFingerprint = "requirement-sha256";
    problem.requirementProvenance.workcellFingerprint = "workcell-state-sha256";
    problem.requirementProvenance.compilerVersion = "EngineeringRequirements.Freezer.1";
    problem.requirementProvenance.frozenAt = "2026-07-30T09:15:00.123Z";

    // 序列化
    const std::string json = rws::StructureOptimizationJson::problemToJson(problem);
    REQUIRE(!json.empty());
    REQUIRE(json.find("\"schemaVersion\": 2") != std::string::npos);

    // 反序列化
    rws::StructureOptimizationProblem parsed;
    std::string error;
    bool ok = rws::StructureOptimizationJson::problemFromJson(json, parsed, &error);
    if (!ok)
        std::printf("\n  fromJson error: %s\n", error.c_str());
    REQUIRE(ok);

    // 验证字段
    REQUIRE(parsed.context.projectName == "TestProj");
    REQUIRE(parsed.context.robotName   == "TestRobot");
    REQUIRE(parsed.variables.size() == 1);
    REQUIRE(parsed.variables[0].id == "a");
    REQUIRE(std::abs(parsed.variables[0].currentValue - 0.5) < 1e-12);
    REQUIRE(parsed.variables[0].domainDefinition.domain == rws::DesignVariableDomain::Discrete);
    REQUIRE(parsed.variables[0].domainDefinition.discreteOptions.size() == 3);
    REQUIRE(parsed.constraints.size() == 1);
    REQUIRE(parsed.constraints[0].id == "c1");
    REQUIRE(parsed.run.candidateCount == 100);
    REQUIRE(parsed.run.randomSeed == 42u);
    REQUIRE(parsed.evaluation.evaluatorId == "test.kinematics");
    REQUIRE(parsed.evaluation.evaluatorVersion == "2.0");
    REQUIRE(parsed.evaluation.quickWorkspace.mode == rws::WorkspaceSamplingMode::Grid);
    REQUIRE(parsed.evaluation.quickWorkspace.sampleCount == 123);
    REQUIRE(parsed.evaluation.quickWorkspace.gridStepsPerJoint == 4);
    REQUIRE(!parsed.evaluation.quickWorkspace.checkCollision);
    REQUIRE(parsed.evaluation.quickWorkspace.randomSeed == 9u);
    REQUIRE(parsed.evaluation.verifiedWorkspace.sampleCount == 456);
    REQUIRE(parsed.evaluation.coverageBox.enabled);
    REQUIRE(parsed.evaluation.coverageBox.cells[0] == 2);
    REQUIRE(parsed.evaluation.coverageBox.cells[1] == 3);
    REQUIRE(parsed.evaluation.coverageBox.cells[2] == 4);
    REQUIRE(parsed.objectives.size() == 1);
    REQUIRE(parsed.objectives[0].metricId == "kinematics.reachability.weighted");
    REQUIRE(std::abs(parsed.objectives[0].weight - 0.7) < 1e-12);
    REQUIRE(parsed.metricConstraints.size() == 1);
    REQUIRE(parsed.metricConstraints[0].metricId == "collision.free_rate");
    REQUIRE(parsed.requirementProvenance.requirementFingerprint == "requirement-sha256");
    REQUIRE(parsed.requirementProvenance.workcellFingerprint == "workcell-state-sha256");
    REQUIRE(parsed.requirementProvenance.compilerVersion == "EngineeringRequirements.Freezer.1");
    REQUIRE(parsed.requirementProvenance.frozenAt == "2026-07-30T09:15:00.123Z");

    // 报告必须保留冻结姿态的解析来源，而不是仅展示已经失去业务语义的 RPY 数值。
    rws::OptimizationTaskPoint auditedStation;
    auditedStation.required = true;
    auditedStation.point.id = "fixture_pick";
    auditedStation.point.name = "Fixture pick";
    auditedStation.point.refFrame = "Fixture_A";
    auditedStation.point.tcpFrame = "TCP";
    auditedStation.point.position = {{0.12, 0.03, 0.08}};
    auditedStation.point.rpyDeg = {{0.0, 90.0, 0.0}};
    auditedStation.point.note = "Orientation resolution: resolver=OrientationRuleResolver.1;mode=AlignFrame;target=Fixture_A";
    parsed.tasks.push_back(auditedStation);

    const std::string report = rws::StructureOptimizationReportWriter::write(
        parsed, rws::StructureOptimizationResult{});
    REQUIRE(report.find("test.kinematics@2.0") != std::string::npos);
    REQUIRE(report.find("system evaluators not enabled") != std::string::npos);
    REQUIRE(report.find("Engineering Requirement Provenance") != std::string::npos);
    REQUIRE(report.find("2026-07-30T09:15:00.123Z") != std::string::npos);
    REQUIRE(report.find("Frozen Key Stations") != std::string::npos);
    REQUIRE(report.find("fixture_pick") != std::string::npos);
    REQUIRE(report.find("OrientationRuleResolver.1") != std::string::npos);

    const std::string legacyJson = R"json({
        "schemaVersion": 1,
        "type": "StructureOptimizationProblem",
        "weights": {
            "reachability": 0.6,
            "manipulability": 0.0,
            "jointMargin": 0.0,
            "collision": 0.0,
            "compactness": 0.0,
            "preference": 0.4
        }
    })json";
    rws::StructureOptimizationProblem migrated;
    ok = rws::StructureOptimizationJson::problemFromJson(legacyJson, migrated, &error);
    REQUIRE(ok);
    REQUIRE(migrated.objectives.size() == 6);
    REQUIRE(std::abs(migrated.objectives[0].weight - 0.6) < 1e-12);
    REQUIRE(!migrated.evaluation.coverageBox.enabled);
    REQUIRE(migrated.evaluation.quickWorkspace.sampleCount == 1000);
    const std::string upgradedJson = rws::StructureOptimizationJson::problemToJson(migrated);
    REQUIRE(upgradedJson.find("\"schemaVersion\": 2") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// Phase 0/S07: Persisted structure-optimization JSON must remain valid when
// numerical evaluation data is unavailable.  Unknown forward-compatible fields
// are carried through a legacy read/save cycle, while unknown enum strings are
// rejected instead of being silently treated as a valid default.
static void testJsonSafetyContract()
{
    std::printf("testJsonSafetyContract ... ");

    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"distance", "Distance", "joint1", "mm",
         rws::StructureVariableKind::JointPositionX,
         std::numeric_limits<double>::quiet_NaN(),
         -std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::infinity(), 0.1}
    };

    const QJsonDocument problemDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(rws::StructureOptimizationJson::problemToJson(problem)));
    REQUIRE(!problemDocument.isNull());
    const QJsonObject serializedVariable =
        problemDocument.object().value("variables").toArray().at(0).toObject();
    REQUIRE(serializedVariable.value("unit").toString() == "mm");
    REQUIRE(serializedVariable.value("currentValue").isNull());
    REQUIRE(serializedVariable.value("minimum").isNull());
    REQUIRE(serializedVariable.value("maximum").isNull());

    rws::StructureOptimizationResult result;
    rws::StructureCandidateResult candidate;
    candidate.index = 7;
    candidate.totalScore = std::numeric_limits<double>::infinity();
    candidate.values = {std::numeric_limits<double>::quiet_NaN(),
                        -std::numeric_limits<double>::infinity()};
    result.candidates.push_back(candidate);
    const QJsonDocument resultDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(rws::StructureOptimizationJson::resultToJson(problem, result)));
    REQUIRE(!resultDocument.isNull());
    const QJsonObject serializedCandidate =
        resultDocument.object().value("candidates").toArray().at(0).toObject();
    REQUIRE(serializedCandidate.value("totalScore").isNull());
    REQUIRE(serializedCandidate.value("totalScoreAvailability").toString() == "Unavailable");
    const QJsonArray serializedValues = serializedCandidate.value("values").toArray();
    REQUIRE(serializedValues.at(0).isNull());
    REQUIRE(serializedValues.at(1).isNull());

    const std::string legacyWithExtension = R"json({
        "schemaVersion": 1,
        "type": "StructureOptimizationProblem",
        "weights": {},
        "futureVendorField": {"revision": 3},
        "extensions": {"futureExtension": ["keep", 5]}
    })json";
    rws::StructureOptimizationProblem parsed;
    std::string error;
    REQUIRE(rws::StructureOptimizationJson::problemFromJson(
        legacyWithExtension, parsed, &error));
    const QJsonDocument roundTripped = QJsonDocument::fromJson(
        QByteArray::fromStdString(rws::StructureOptimizationJson::problemToJson(parsed)));
    REQUIRE(roundTripped.object().value("extensions").toObject().value("futureVendorField").
            toObject().value("revision").toInt() == 3);
    REQUIRE(roundTripped.object().value("extensions").toObject().value("futureExtension").
            toArray().at(0).toString() == "keep");

    const std::string unknownVariableKind = R"json({
        "schemaVersion": 2,
        "type": "StructureOptimizationProblem",
        "variables": [{"kind": "FutureVariableKind"}]
    })json";
    REQUIRE(!rws::StructureOptimizationJson::problemFromJson(
        unknownVariableKind, parsed, &error));
    REQUIRE(error.find("FutureVariableKind") != std::string::npos);

    const std::string unknownEnumValues = R"json({
        "schemaVersion": 2,
        "type": "StructureOptimizationProblem",
        "constraints": [{"kind": "FutureConstraintKind"}]
    })json";
    REQUIRE(!rws::StructureOptimizationJson::problemFromJson(
        unknownEnumValues, parsed, &error));
    REQUIRE(error.find("FutureConstraintKind") != std::string::npos);

    const std::string unknownWorkspaceMode = R"json({
        "schemaVersion": 2,
        "type": "StructureOptimizationProblem",
        "evaluationConfig": {"quickWorkspace": {"mode": 99}}
    })json";
    REQUIRE(!rws::StructureOptimizationJson::problemFromJson(
        unknownWorkspaceMode, parsed, &error));
    REQUIRE(error.find("WorkspaceSamplingMode") != std::string::npos);

    const std::string unknownStrategy = R"json({
        "schemaVersion": 2,
        "type": "StructureOptimizationProblem",
        "runConfig": {"strategy": 99}
    })json";
    REQUIRE(!rws::StructureOptimizationJson::problemFromJson(
        unknownStrategy, parsed, &error));
    REQUIRE(error.find("StructureStrategyKind") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// Phase 6/S60：当前 JSON Envelope 契约测试。
//
// 这些断言刻意只关注持久化边界，而不依赖 UI 或运行时 WorkCell：当前文档必须有
// 唯一根类型、各 canonical 分区的独立版本号、稳定的绑定元数据和可重现指纹；
// 运行时指针、候选结果等不属于主配置，不能因为“顺手保存”而进入 Envelope。
static void testCurrentJsonEnvelope()
{
    std::printf("testCurrentJsonEnvelope ... ");

    rws::StructureOptimizationProblem problem;
    problem.context.projectName = "S60Project";
    problem.context.robotName = "S60Robot";
    problem.variables = {
        {"joint-offset", "Joint offset", "Joint1", "mm",
         rws::StructureVariableKind::JointPositionX,
         12.0, -25.0, 25.0, 1.0, 0.0, 0.5, true, false}
    };
    problem.variables[0].domainDefinition.domain = rws::DesignVariableDomain::Continuous;
    problem.tasks.push_back({{}, true});
    problem.tasks.back().point.id = "task-1";
    problem.objectives.push_back({"reachability", rws::OptimizationDirection::Maximize,
                                  {1.0, 0.0, true}, 1.0, true});
    problem.metricConstraints.push_back({"collision.free_rate",
                                         rws::ComparisonOperator::GreaterThanOrEqual,
                                         0.9, true, true});
    problem.extensions["vendorExtension"] = QJsonObject{{"revision", 3}};

    const std::string json = rws::StructureOptimizationJson::currentEnvelopeToJson(problem);
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    REQUIRE(!document.isNull());
    REQUIRE(document.isObject());
    const QJsonObject root = document.object();
    REQUIRE(root.value("type").toString() == "StructureOptimizationDocument");
    REQUIRE(root.value("schemaVersion").toInt() ==
            rws::StructureOptimizationDocument::SchemaVersion);

    const QJsonObject designSpace = root.value("designSpace").toObject();
    const QJsonObject binding = designSpace.value("bindings").toArray().at(0).toObject();
    REQUIRE(designSpace.value("schemaVersion").toInt() ==
            rws::StructureOptimizationDocument::DesignSpaceSchemaVersion);
    REQUIRE(designSpace.value("variables").toArray().at(0).toObject().value("unit").toString() ==
            "m");
    REQUIRE(designSpace.value("variables").toArray().at(0).toObject().value("kind").toString() ==
            "JointPositionX");
    REQUIRE(binding.value("id").toString() == "joint-offset");
    REQUIRE(binding.value("adapterId").toString() == "structure.legacy-variable");
    REQUIRE(binding.value("version").toInt() == 1);
    REQUIRE(!binding.contains("runtimePointer"));

    REQUIRE(root.value("plan").toObject().value("schemaVersion").toInt() ==
            rws::StructureOptimizationDocument::PlanSchemaVersion);
    REQUIRE(root.value("objectives").toObject().value("schemaVersion").toInt() ==
            rws::StructureOptimizationDocument::ObjectivesSchemaVersion);
    REQUIRE(root.value("objectives").toObject().value("items").toArray().at(0)
                .toObject().value("direction").toString() == "Maximize");
    REQUIRE(root.value("constraints").toObject().value("schemaVersion").toInt() ==
            rws::StructureOptimizationDocument::ConstraintsSchemaVersion);
    REQUIRE(root.value("constraints").toObject().value("metric").toArray().at(0)
                .toObject().value("comparison").toString() == "GreaterThanOrEqual");
    REQUIRE(root.value("config").toObject().value("schemaVersion").toInt() ==
            rws::StructureOptimizationDocument::ConfigSchemaVersion);
    REQUIRE(root.value("results").isUndefined());
    REQUIRE(json.find("runtimePointer") == std::string::npos);
    REQUIRE(json.find("StructureOptimizationResult") == std::string::npos);
    REQUIRE(root.value("extensions").toObject().value("vendorExtension")
                .toObject().value("revision").toInt() == 3);

    rws::StructureOptimizationProblem parsed;
    std::string error;

    // 当前 Envelope 的未知根字段必须归档到 extensions，不能在严格读取时静默丢失。
    QJsonObject withFutureField = root;
    withFutureField["futureRootField"] = QJsonObject{{"revision", 4}};
    rws::StructureOptimizationProblem futureParsed;
    REQUIRE(rws::StructureOptimizationJson::currentEnvelopeFromJson(
        QJsonDocument(withFutureField).toJson(QJsonDocument::Compact).toStdString(),
        futureParsed, &error));
    REQUIRE(futureParsed.extensions.value("futureRootField").toObject()
                .value("revision").toInt() == 4);

    // Binding 是跨运行时适配器的持久化契约；缺少稳定 ID、适配器 ID 或版本号时，
    // reader 必须拒绝，而不是生成一个无法重新绑定的半有效文档。
    QJsonObject invalidBindingRoot = root;
    QJsonArray invalidBindings = designSpace.value("bindings").toArray();
    invalidBindings[0] = QJsonObject{{"id", "joint-offset"}};
    QJsonObject invalidDesignSpace = designSpace;
    invalidDesignSpace["bindings"] = invalidBindings;
    invalidBindingRoot["designSpace"] = invalidDesignSpace;
    rws::StructureOptimizationProblem invalidBindingProblem;
    error.clear();
    REQUIRE(!rws::StructureOptimizationJson::currentEnvelopeFromJson(
        QJsonDocument(invalidBindingRoot).toJson(QJsonDocument::Compact).toStdString(),
        invalidBindingProblem, &error));
    REQUIRE(error.find("binding") != std::string::npos);

    REQUIRE(rws::StructureOptimizationJson::currentEnvelopeFromJson(json, parsed, &error));
    REQUIRE(parsed.variables.size() == 1);
    REQUIRE(parsed.variables[0].unit == "m");
    REQUIRE(std::abs(parsed.variables[0].currentValue - 0.012) < 1e-12);
    REQUIRE(parsed.extensions.value("vendorExtension").toObject().value("revision").toInt() == 3);

    const std::string firstFingerprint =
        rws::StructureOptimizationJson::currentEnvelopeFingerprint(problem);
    const std::string secondFingerprint =
        rws::StructureOptimizationJson::currentEnvelopeFingerprint(
            rws::StructureOptimizationJson::currentEnvelopeToJson(parsed));
    REQUIRE(!firstFingerprint.empty());
    REQUIRE(firstFingerprint == secondFingerprint);

    // 对所有对象递归重排字段插入顺序，规范指纹必须保持不变。
    std::function<QJsonValue(const QJsonValue&)> reverseObjectOrder =
        [&reverseObjectOrder](const QJsonValue& value) -> QJsonValue {
        if (value.isArray()) {
            QJsonArray result;
            for (const QJsonValue& item : value.toArray())
                result.append(reverseObjectOrder(item));
            return result;
        }
        if (!value.isObject()) return value;
        const QJsonObject source = value.toObject();
        const QStringList keys = source.keys();
        QJsonObject result;
        for (auto it = keys.crbegin(); it != keys.crend(); ++it)
            result[*it] = reverseObjectOrder(source.value(*it));
        return result;
    };
    const std::string reorderedJson = QJsonDocument(
        reverseObjectOrder(document.object()).toObject()).toJson(QJsonDocument::Compact).toStdString();
    REQUIRE(firstFingerprint == rws::StructureOptimizationJson::currentEnvelopeFingerprint(
        reorderedJson));

    // 非有限数值必须变成 null，不能产生非法 JSON 或隐式的伪零值。
    problem.variables[0].currentValue = std::numeric_limits<double>::quiet_NaN();
    const QJsonObject unsafe = QJsonDocument::fromJson(
        QByteArray::fromStdString(rws::StructureOptimizationJson::currentEnvelopeToJson(problem)))
                                   .object();
    REQUIRE(unsafe.value("designSpace").toObject().value("variables").toArray().at(0)
                .toObject().value("currentValue").isNull());
    problem.variables[0].maximum = std::numeric_limits<double>::infinity();
    const QJsonObject unsafeInf = QJsonDocument::fromJson(
        QByteArray::fromStdString(rws::StructureOptimizationJson::currentEnvelopeToJson(problem)))
                                         .object();
    REQUIRE(unsafeInf.value("designSpace").toObject().value("variables").toArray().at(0)
                .toObject().value("maximum").isNull());

    // 写入门不能把无法解释的单位静默改标成 SI：未知单位或与变量种类不匹配的
    // 单位必须让写出门显式失败，否则 canonical 文档会携带被伪造的米/弧度语义。
    problem.variables[0].currentValue = 12.0;
    problem.variables[0].minimum = -25.0;
    problem.variables[0].maximum = 25.0;
    problem.variables[0].step = 1.0;
    problem.variables[0].preferredValue = 0.5;
    auto writesRejected = [](const rws::StructureOptimizationProblem& candidate) {
        try {
            rws::StructureOptimizationJson::currentEnvelopeToJson(candidate);
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    rws::StructureOptimizationProblem unknownUnit = problem;
    unknownUnit.variables[0].unit = "in";
    REQUIRE(writesRejected(unknownUnit));
    rws::StructureOptimizationProblem missingUnit = problem;
    missingUnit.variables[0].unit.clear();
    REQUIRE(writesRejected(missingUnit));
    rws::StructureOptimizationProblem wrongFamily = problem;
    wrongFamily.variables[0].kind = rws::StructureVariableKind::JointRotationRoll;
    wrongFamily.variables[0].unit = "mm";
    REQUIRE(writesRejected(wrongFamily));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 审计证据输出:构造带模型溯源(源路径/源指纹/快照指纹)与区域级覆盖率
// 指标的候选结果,验证报告、结果 JSON、审计 CSV 三种导出形式都保留证据阶段统计、
// 灵敏度来源与关键变量、各区域覆盖率及本地参考系,供工程师复核是哪个工装区域
// 限制了候选结构。
static void testAuditableEvidenceOutput()
{
    std::printf("testAuditableEvidenceOutput ... ");

    rws::StructureOptimizationProblem problem;
    problem.context.robotName = "AuditRobot";
    problem.context.sourceModelPath = "models/audit.rmb.json";
    problem.context.modelProvenance = {"models/audit.rmb.json", "source-sha",
                                       "snapshot-sha"};
    problem.evaluation.evaluatorId = "test.kinematics";
    problem.evaluation.evaluatorVersion = "2.0";
    problem.evaluation.coverageBox.enabled = true;
    problem.evaluation.coverageBox.minimum = {{-0.5, -0.4, 0.1}};
    problem.evaluation.coverageBox.maximum = {{0.5, 0.6, 1.1}};
    problem.evaluation.coverageBox.cells = {{2, 3, 4}};
    problem.evaluation.quickWorkspace.sampleCount = 120;
    problem.evaluation.quickWorkspace.randomSeed = 17u;
    problem.evaluation.verifiedWorkspace.sampleCount = 480;
    problem.evaluation.verifiedWorkspace.randomSeed = 23u;
    problem.variables = {
        {"link2", "Link 2", "Joint2", "m",
         rws::StructureVariableKind::JointPositionX,
         0.4, 0.2, 0.8, 0.01, 0.4, 0.0, true, false}
    };

    rws::StructureOptimizationResult result;
    result.baselineCandidateIndex = 0;
    result.bestCandidateIndex = 7;
    result.diagnostics.generatedCandidates = 31;
    result.diagnostics.evaluatedCandidates = 29;
    result.diagnostics.cacheHits = 6;
    result.diagnostics.quickEvaluatedCandidates = 20;
    result.diagnostics.verifiedEliteCandidates = 4;
    result.diagnostics.finalVerifiedCandidates = 2;
    result.diagnostics.sensitivityEvaluations = 2;
    result.sensitivity.robustnessGrade = "B";
    result.sensitivity.criticalVariableIds = {"link2"};

    rws::StructureCandidateResult best;
    best.index = 7;
    best.feasible = true;
    best.status = rws::StructureCandidateStatus::Feasible;
    best.stage = rws::StructureEvaluationStage::Verified;
    best.totalScore = 91.0;
    best.values = {0.45};
    best.raw.workspaceCoverage = 0.75;
    best.raw.workspaceOccupiedCellCount = 18;
    best.raw.workspaceTotalCellCount = 24;
    // 多区域冻结需求不能只在评价器内部可见；报告必须保留每个区域的局部参考系、
    // 覆盖率和网格统计，才能让工程师复核哪个工装区域限制了候选结构。
    best.raw.workspaceRegionMetrics = {
        {"area_a", "Fixture_A", 0.90, 9, 10},
        {"area_b", "Fixture_B", 0.50, 5, 10}
    };
    result.candidates.push_back(best);

    const std::string report = rws::StructureOptimizationReportWriter::write(problem, result);
    REQUIRE(report.find("Final verified candidates: 2") != std::string::npos);
    REQUIRE(report.find("Best candidate: 7") != std::string::npos);
    REQUIRE(report.find("Workspace coverage: 0.750") != std::string::npos);
    REQUIRE(report.find("Grid cells: 2 x 3 x 4") != std::string::npos);
    REQUIRE(report.find("Quick sampling: mode=RandomUniform, samples=120") !=
            std::string::npos);
    REQUIRE(report.find("Sensitivity source: verified evaluator") != std::string::npos);
    REQUIRE(report.find("Critical variables: link2") != std::string::npos);
    REQUIRE(report.find("Trajectory evaluator: not enabled") != std::string::npos);
    REQUIRE(report.find("Dynamics evaluator: not enabled") != std::string::npos);
    REQUIRE(report.find("Drive selection evaluator: not enabled") != std::string::npos);
    REQUIRE(report.find("Source status: Tracked") != std::string::npos);
    REQUIRE(report.find("Source model path: models/audit.rmb.json") != std::string::npos);
    REQUIRE(report.find("Source fingerprint: source-sha") != std::string::npos);
    REQUIRE(report.find("Snapshot fingerprint: snapshot-sha") != std::string::npos);
    REQUIRE(report.find("Workspace Coverage Results") != std::string::npos);
    REQUIRE(report.find("area_a") != std::string::npos);
    REQUIRE(report.find("Fixture_B") != std::string::npos);
    REQUIRE(report.find("0.500") != std::string::npos);

    const std::string json = rws::StructureOptimizationJson::resultToJson(problem, result);
    REQUIRE(json.find("\"quickEvaluatedCandidates\": 20") != std::string::npos);
    REQUIRE(json.find("\"finalVerifiedCandidates\": 2") != std::string::npos);
    REQUIRE(json.find("\"sensitivityEvaluations\": 2") != std::string::npos);

    const std::string audit = rws::StructureOptimizationCsv::auditCsv(problem, result);
    REQUIRE(audit.find("QuickEvaluatedCandidates") != std::string::npos);
    REQUIRE(audit.find("FinalVerifiedCandidates") != std::string::npos);
    REQUIRE(audit.find("SensitivityEvaluations") != std::string::npos);
    REQUIRE(audit.find("test.kinematics@2.0") != std::string::npos);
    REQUIRE(audit.find("ModelProvenanceStatus,Tracked") != std::string::npos);
    REQUIRE(audit.find("SourceModelPath,models/audit.rmb.json") != std::string::npos);
    REQUIRE(audit.find("SourceFingerprint,source-sha") != std::string::npos);
    REQUIRE(audit.find("SnapshotFingerprint,snapshot-sha") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: CSV 导出
// =============================================================================

static void testCsvExport()
{
    std::printf("testCsvExport ... ");

    // 准备结果
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"v1", "Var 1", "j1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.1, 0.0, 0.0, true, false}
    };

    rws::StructureOptimizationResult result;

    rws::StructureCandidateResult c0;
    c0.index      = 0;
    c0.feasible   = true;
    c0.totalScore = 85.5;
    c0.status     = rws::StructureCandidateStatus::Feasible;
    c0.values     = {0.1};
    c0.raw.requiredReachableCount = 5;
    c0.raw.requiredTaskCount      = 5;
    c0.raw.manipulabilityP10      = 0.05;
    c0.raw.jointMarginP10         = 0.12;
    c0.raw.collisionFreeRate      = 1.0;
    c0.raw.totalKinematicLength   = 0.8;

    rws::StructureTaskMetric tm;
    tm.taskId = "t1";
    tm.taskName = "Task 1";
    tm.required   = true;
    tm.reachable  = true;
    tm.inCollision = false;
    tm.manipulability = 0.3;
    tm.jointMargin    = 0.15;
    tm.usableSolutionCount = 5;
    c0.raw.taskMetrics.push_back(tm);

    result.candidates.push_back(c0);

    // candidatesCsv
    const std::string csv = rws::StructureOptimizationCsv::candidatesCsv(problem, result);
    REQUIRE(!csv.empty());

    // 检查表头包含关键列
    REQUIRE(csv.find("Index,Status,Feasible,TotalScore") != std::string::npos);
    REQUIRE(csv.find("v1") != std::string::npos);
    // 检查数据行
    REQUIRE(csv.find("\n0,Feasible,") != std::string::npos);
    REQUIRE(csv.find(",true,") != std::string::npos);

    // taskDetailCsv
    const std::string detail = rws::StructureOptimizationCsv::taskDetailCsv(problem, result);
    REQUIRE(!detail.empty());

    REQUIRE(detail.find("CandidateIndex,TaskId,TaskName") != std::string::npos);
    REQUIRE(detail.find("t1") != std::string::npos);
    REQUIRE(detail.find("Task 1") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 工作区覆盖率统计:2x2x2 覆盖盒内放入 Pass/Warning/碰撞三个样本与一个
// 盒外样本,验证只有"落在盒内且未碰撞"的样本才占据单元(2/8=0.25)——
// 碰撞样本与盒外样本都不计入,Warning 仍算可达,覆盖率不可超过 1。
static void testWorkspaceCoverage()
{
    std::printf("testWorkspaceCoverage ... ");

    rws::WorkspaceCoverageBox box;
    box.enabled = true;
    box.minimum = {{0.0, 0.0, 0.0}};
    box.maximum = {{2.0, 2.0, 2.0}};
    box.cells = {{2, 2, 2}};

    rws::WorkspaceSample first;
    first.tcpPosition = {{0.1, 0.1, 0.1}};
    first.status = rws::AnalysisStatus::Pass;

    rws::WorkspaceSample second;
    second.tcpPosition = {{1.1, 1.1, 1.1}};
    second.status = rws::AnalysisStatus::Warning;

    rws::WorkspaceSample collided;
    collided.tcpPosition = {{1.1, 0.1, 0.1}};
    collided.status = rws::AnalysisStatus::Pass;
    collided.inCollision = true;

    rws::WorkspaceSample outside;
    outside.tcpPosition = {{2.1, 0.1, 0.1}};
    outside.status = rws::AnalysisStatus::Pass;

    const std::vector<rws::WorkspaceSample> samples = {
        first, second, collided, outside};
    const rws::StructureWorkspaceCoverageResult result =
        rws::StructureWorkspaceCoverage::analyze(samples, box);
    REQUIRE(result.totalCellCount == 8u);
    REQUIRE(result.occupiedCellCount == 2u);
    REQUIRE(std::abs(result.coverage - 0.25) < 1e-12);
    REQUIRE(std::abs(rws::StructureWorkspaceCoverage::calculate(samples, box) - 0.25) <
            1e-12);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 结构优化器接入真实工作空间覆盖率
// =============================================================================

// 构造一个专用于工作区覆盖率测试的问题:关闭碰撞、启用 1x1x1 大覆盖盒、
// 快速采样 16 个点固定种子。供多个覆盖率用例复用,保证求值环境一致。
static rws::StructureOptimizationProblem makeWorkspaceCoverageProblem()
{
    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.modelSpec.robotName = "WorkspaceCoverageRobot";
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;
    problem.context.tcpFrame.clear();
    problem.evaluation.checkCollision = false;
    problem.evaluation.coverageBox.enabled = true;
    problem.evaluation.coverageBox.minimum = {{-10.0, -10.0, -10.0}};
    problem.evaluation.coverageBox.maximum = {{10.0, 10.0, 10.0}};
    problem.evaluation.coverageBox.cells = {{1, 1, 1}};
    problem.evaluation.quickWorkspace.sampleCount = 16;
    problem.evaluation.quickWorkspace.checkCollision = false;
    problem.evaluation.quickWorkspace.randomSeed = 7u;
    return problem;
}

// 在工程评价结果里按 metricId 查找指标,找不到返回 nullptr。测试辅助函数。
static const rws::EngineeringMetric* findMetric(
    const rws::EngineeringEvaluationResult& result, const std::string& metricId)
{
    for (const rws::EngineeringMetric& metric : result.metrics) {
        if (metric.metricId == metricId)
            return &metric;
    }
    return nullptr;
}

// 判断工程评价结果是否产出了指定工件。测试辅助函数。
static bool hasArtifact(const rws::EngineeringEvaluationResult& result,
                        const std::string& artifactId)
{
    for (const rws::EngineeringArtifact& artifact : result.artifacts) {
        if (artifact.artifactId == artifactId)
            return true;
    }
    return false;
}

// 子套件 工作区覆盖率评价器:验证 KinematicEngineeringEvaluator 在真实六轴模型上
// 产出正值 coverage 指标与 coverage-summary 工件;再挂一条超过可达上限的
// MinimumWorkspaceCoverage 硬约束(阈值 1.1),评价结果必须为 Infeasible。
static void testWorkspaceCoverageEvaluator()
{
    std::printf("testWorkspaceCoverageEvaluator ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    rws::KinematicEngineeringEvaluator evaluator(problem);
    rws::CandidateEvaluationContext context;
    rws::EvaluationRequest request;
    const rws::EngineeringEvaluationResult result = evaluator.evaluate(
        context, request, rws::EvaluationCallbacks());
    const rws::EngineeringMetric* coverage = findMetric(
        result, "kinematics.workspace.coverage");

    REQUIRE(result.status == rws::EngineeringEvaluationStatus::Success);
    REQUIRE(coverage != nullptr);
    REQUIRE(coverage != nullptr && coverage->value > 0.0);
    REQUIRE(hasArtifact(result, "kinematics.workspace.coverage-summary"));

    rws::StructureConstraint minimumCoverage;
    minimumCoverage.id = "coverage-too-high";
    minimumCoverage.kind = rws::StructureConstraintKind::MinimumWorkspaceCoverage;
    minimumCoverage.threshold = 1.1;
    minimumCoverage.hard = true;
    minimumCoverage.enabled = true;
    problem.constraints.push_back(minimumCoverage);
    rws::KinematicEngineeringEvaluator constrainedEvaluator(problem);
    const rws::EngineeringEvaluationResult constrained =
        constrainedEvaluator.evaluate(context, request, rws::EvaluationCallbacks());
    REQUIRE(constrained.status == rws::EngineeringEvaluationStatus::Infeasible);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 评估器入口统一:同一固定问题与固定候选值分别经旧入口 evaluateLegacy
// (兼容转发)与新入口 evaluateCandidate 评估,断言 status/feasible/
// requiredReachableCount/collisionFreeRate/totalScore 完全一致,防止统一
// 评估入口时行为漂移。这是全仓库唯一允许使用 evaluateLegacy 的测试。
static void testEvaluateCandidateMatchesLegacyWrapper()
{
    std::printf("testEvaluateCandidateMatchesLegacyWrapper ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();

    rws::StructureCandidateResult viaLegacyEntry;
    viaLegacyEntry.index = 3;
    rws::StructureCandidateResult viaCandidate = viaLegacyEntry;

    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };

    rws::KinematicEngineeringEvaluator evaluator(problem);
    evaluator.evaluateLegacy(viaLegacyEntry, rws::StructureEvaluationStage::Quick,
                             callbacks, nullptr);
    evaluator.evaluateCandidate(viaCandidate, rws::StructureEvaluationStage::Quick,
                                callbacks, nullptr);

    REQUIRE(viaLegacyEntry.status == viaCandidate.status);
    REQUIRE(viaLegacyEntry.feasible == viaCandidate.feasible);
    REQUIRE(viaLegacyEntry.raw.requiredReachableCount ==
            viaCandidate.raw.requiredReachableCount);
    REQUIRE(viaLegacyEntry.raw.collisionFreeRate == viaCandidate.raw.collisionFreeRate);
    REQUIRE(viaLegacyEntry.totalScore == viaCandidate.totalScore);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 TCP 裸名回退:存量项目在 context.tcpFrame 里存的是模型构建器的
// 裸名("TCP"),而 CandidateModelFactory 生成的 WorkCell 把设备内部帧注册为
// "<device>.TCP"。修复前 findFrame("TCP") 落空导致所有候选在模型构建阶段
// Failed(得分恒 0);回归断言裸名经设备前缀回退后评估正常出分。
static void testTcpBareNameFallback()
{
    std::printf("testTcpBareNameFallback ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.context.tcpFrame = "TCP";   // 故意使用裸名,模拟存量项目数据

    rws::KinematicEngineeringEvaluator evaluator(problem);
    rws::CandidateEvaluationContext context;
    rws::EvaluationRequest request;
    const rws::EngineeringEvaluationResult result = evaluator.evaluate(
        context, request, rws::EvaluationCallbacks());

    REQUIRE(result.status != rws::EngineeringEvaluationStatus::Failed);
    REQUIRE(findMetric(result, "kinematics.reachability.weighted") != nullptr);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 评价器一致性:在候选场景里放置一个覆盖整个可达空间的大碰撞盒子,验证
// KinematicEngineeringEvaluator 对"当前位姿"任务(Must 碰撞必需 / Should 无碰撞)
// 的结果与直接调用 TargetEvaluator + ConfigurationEvaluator 完全一致——Must 任务
// 因碰撞不可达、Should 任务可达;任务指标的 jointMargin/manipulability 与配置
// 求值逐位吻合,防止两条评价路径漂移。
static void testSharedTargetEvaluatorConsistency()
{
    std::printf("testSharedTargetEvaluatorConsistency ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.evaluation.checkCollision = true;
    problem.evaluation.coverageBox.enabled = false;
    problem.evaluation.coverageBoxes.clear();

    rws::SceneGeometrySpec collisionFixture;
    collisionFixture.name = "EvaluatorConsistencyCollisionFixture";
    collisionFixture.refFrame = "WORLD";
    collisionFixture.kind = rws::GeometryKind::Box;
    collisionFixture.size = {{10.0, 10.0, 10.0}};
    collisionFixture.collisionModel = true;
    problem.context.modelSpec.sceneGeometries.push_back(collisionFixture);

    rws::CandidateModelBuildRequest request;
    request.spec = problem.context.modelSpec;
    request.deviceName = problem.context.deviceName;
    request.tcpFrame = problem.context.tcpFrame;
    request.checkCollision = true;
    const rws::CandidateModelBuildResult built = rws::CandidateModelFactory().build(request);
    REQUIRE(built.ok);
    REQUIRE(!built.artifact.collisionDetector.isNull());
    if (!built.ok) return;
    rw::kinematics::State referenceState = built.artifact.state;
    built.artifact.device->setQ(rw::math::Q(6, 0.2, -0.4, 0.6, -0.3, 0.5, -0.7),
                                referenceState);

    const rw::math::Transform3D<> worldTtcp = rw::kinematics::Kinematics::frameTframe(
        built.artifact.workcell->getWorldFrame(), built.artifact.tcpFrame.get(),
        referenceState);
    const rw::math::RPY<> targetRpy(worldTtcp.R());

    rws::RequirementExecutionTask requirementTask;
    requirementTask.id = "shared-evaluator-current-pose";
    requirementTask.name = "Shared evaluator current pose";
    requirementTask.level = rws::RequirementExecutionLevel::Must;
    requirementTask.compileState = rws::RequirementExecutionCompileState::Included;
    requirementTask.refFrame = "WORLD";
    requirementTask.tcpFrame = built.artifact.tcpFrame->getName();
    requirementTask.collisionFreeRequired = true;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        requirementTask.position[axis] = worldTtcp.P()[axis];
        requirementTask.rpyDeg[axis] = targetRpy(axis) * rw::math::Rad2Deg;
    }
    requirementTask.positionToleranceMeters = 1e-6;
    requirementTask.orientationToleranceDeg = 1e-4;
    problem.requirementExecution.tasks.push_back(requirementTask);

    rws::RequirementExecutionTask shouldTask = requirementTask;
    shouldTask.id = "shared-evaluator-should-task";
    shouldTask.name = "Shared evaluator Should task";
    shouldTask.level = rws::RequirementExecutionLevel::Should;
    shouldTask.collisionFreeRequired = false;
    problem.requirementExecution.tasks.push_back(shouldTask);

    rws::StructureCandidateResult structureResult;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = [] () { return false; };
    rws::KinematicEngineeringEvaluator(problem).evaluateLegacy(
        structureResult, rws::StructureEvaluationStage::Verified, callbacks, nullptr);
    REQUIRE(structureResult.raw.taskMetrics.size() == 2);
    if (structureResult.raw.taskMetrics.size() != 2) return;
    REQUIRE(structureResult.raw.taskMetrics[0].required);
    REQUIRE(!structureResult.raw.taskMetrics[1].required);
    REQUIRE(!structureResult.raw.taskMetrics[0].reachable);
    REQUIRE(structureResult.raw.taskMetrics[0].inCollision);
    REQUIRE(structureResult.raw.taskMetrics[1].reachable);
    REQUIRE(!structureResult.raw.taskMetrics[1].inCollision);

    rws::AnalysisContextInput contextInput;
    contextInput.workcell = built.artifact.workcell;
    contextInput.device = built.artifact.device;
    contextInput.tcpFrame = built.artifact.tcpFrame;
    contextInput.baseState = built.artifact.state;
    contextInput.modelFingerprint = "structure-evaluator-consistency-model";
    contextInput.environmentFingerprint = "structure-evaluator-consistency-environment";
    contextInput.thresholds = problem.evaluation.thresholds;
    contextInput.collisionDetector = built.artifact.collisionDetector;
    contextInput.collisionRequired = true;
    rws::AnalysisContext context;
    std::string contextError;
    REQUIRE(rws::makeAnalysisContext(contextInput, context, &contextError));

    rws::TargetEvaluationOptions options;
    rws::TaskPoint target;
    target.id = requirementTask.id;
    target.name = requirementTask.name;
    target.refFrame = requirementTask.refFrame;
    target.tcpFrame = requirementTask.tcpFrame;
    target.position = requirementTask.position;
    target.rpyDeg = requirementTask.rpyDeg;
    target.tolerance.positionMeters = requirementTask.positionToleranceMeters;
    target.tolerance.orientationDeg = requirementTask.orientationToleranceDeg;

    options.evidenceStage = rws::AnalysisEvidenceStage::Verified;
    options.checkCollision = true;
    options.requireCollisionFree = true;
    options.positionToleranceMeters = target.tolerance.positionMeters;
    options.orientationToleranceDeg = target.tolerance.orientationDeg;
    const rws::TargetEvaluation direct =
        rws::TargetEvaluator().evaluate(context, target, options);
    REQUIRE(!direct.candidates.empty());
    if (direct.candidates.empty()) return;

    const rws::TargetCandidate& selected = direct.candidates.front();
    rws::ConfigurationEvaluationOptions configurationOptions;
    configurationOptions.evidenceStage = rws::AnalysisEvidenceStage::Verified;
    configurationOptions.checkCollision = true;
    configurationOptions.requireCollisionFree = true;
    const rws::ConfigurationEvaluation sameQ =
        rws::ConfigurationEvaluator().evaluate(
            context, selected.configuration.q, configurationOptions);
    REQUIRE(sameQ.collisionChecked);
    REQUIRE(sameQ.inCollision);
    REQUIRE(selected.configuration.feasibility == sameQ.feasibility);
    REQUIRE(selected.configuration.inCollision == sameQ.inCollision);
    REQUIRE(std::abs(selected.configuration.minimumJointMargin -
                     sameQ.minimumJointMargin) < 1e-12);
    REQUIRE(std::abs(selected.configuration.manipulability -
                     sameQ.manipulability) < 1e-12);

    const rws::StructureTaskMetric& metric = structureResult.raw.taskMetrics.front();
    REQUIRE(metric.reachable == (direct.feasibility == rws::Feasibility::Feasible));
    REQUIRE(metric.inCollision == sameQ.inCollision);
    REQUIRE(std::abs(metric.jointMargin - sameQ.minimumJointMargin) < 1e-12);
    REQUIRE(std::abs(metric.manipulability - sameQ.manipulability) < 1e-12);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 Verified 区域走共享评价器:在当前位姿处放置一个极小(1e-6)的 Verified
// 区域,验证 kinematic 求值器在 Quick 与 Verified 两个阶段都成功,且 Verified 阶段
// 产出 coverage == 1.0 的指标——证明区域覆盖率确实经由同一套 RegionCoverage
// 机制计算,而非只对任务点生效。
static void testVerifiedRegionUsesSharedEvaluator()
{
    std::printf("testVerifiedRegionUsesSharedEvaluator ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.evaluation.checkCollision = true;
    problem.evaluation.verifiedWorkspace.sampleCount = 8;
    problem.evaluation.verifiedWorkspace.checkCollision = false;

    rws::CandidateModelBuildRequest buildRequest;
    buildRequest.spec = problem.context.modelSpec;
    buildRequest.deviceName = problem.context.deviceName;
    buildRequest.tcpFrame = problem.context.tcpFrame;
    buildRequest.checkCollision = true;
    const rws::CandidateModelBuildResult built =
        rws::CandidateModelFactory().build(buildRequest);
    REQUIRE(built.ok);
    if (!built.ok) return;
    rw::kinematics::State referenceState = built.artifact.state;
    built.artifact.device->setQ(rw::math::Q(6, 0.2, -0.4, 0.6, -0.3, 0.5, -0.7),
                                referenceState);
    const rw::math::Transform3D<> worldTtcp = rw::kinematics::Kinematics::frameTframe(
        built.artifact.workcell->getWorldFrame(), built.artifact.tcpFrame.get(),
        referenceState);
    const rw::math::RPY<> currentRpy(worldTtcp.R());

    rws::RequirementExecutionRegion region;
    region.id = "verified-far-region";
    region.name = "Verified far region";
    region.level = rws::RequirementExecutionLevel::Must;
    region.compileState = rws::RequirementExecutionCompileState::Included;
    region.refFrame = "WORLD";
    region.center = {{worldTtcp.P()[0], worldTtcp.P()[1], worldTtcp.P()[2]}};
    region.size = {{1e-6, 1e-6, 1e-6}};
    region.fixedRpyDeg = {{currentRpy(0) * rw::math::Rad2Deg,
                           currentRpy(1) * rw::math::Rad2Deg,
                           currentRpy(2) * rw::math::Rad2Deg}};
    region.minimumCoverage = 1.0;
    region.sampleCounts = {{2, 2, 2}};
    region.orientationMode = rws::RequirementExecutionOrientationMode::Fixed;
    region.directionSamples = 1;
    region.rollSamples = 1;
    region.minimumOrientationCoverage = 0.5;
    region.minimumVerificationStage = rws::RequirementExecutionStage::Quick;
    region.collisionFreeRequired = false;
    problem.requirementExecution.provenance.requirementFingerprint =
        "verified-region-requirements";
    problem.requirementExecution.workspaceRegions.push_back(region);

    rws::CandidateEvaluationContext context;
    rws::EvaluationRequest quickRequest;
    quickRequest.stage = rws::EngineeringEvaluationStage::Quick;
    const rws::EngineeringEvaluationResult quick =
        rws::KinematicEngineeringEvaluator(problem).evaluate(
            context, quickRequest, rws::EvaluationCallbacks());
    REQUIRE(quick.status == rws::EngineeringEvaluationStatus::Success);

    rws::EvaluationRequest verifiedRequest;
    verifiedRequest.stage = rws::EngineeringEvaluationStage::Verified;
    const rws::EngineeringEvaluationResult verified =
        rws::KinematicEngineeringEvaluator(problem).evaluate(
            context, verifiedRequest, rws::EvaluationCallbacks());
    REQUIRE(verified.status == rws::EngineeringEvaluationStatus::Success);
    const rws::EngineeringMetric* coverage =
        findMetric(verified, "kinematics.workspace.coverage");
    REQUIRE(coverage != nullptr);
    REQUIRE(coverage != nullptr && coverage->value == 1.0);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 区域位置覆盖率保真:验证 PointAtTarget 区域下 orientationCoverage 小于
// positionCoverage(位置可达但方向受限),并把最小方向覆盖率收紧到恰好通过;
// 结构评价器中的区域指标取的是 positionCoverage 并与直接求值一致——防止方向
// 覆盖率把位置可达的区域误判为不可达(只把位置覆盖作为硬约束)。
static void testVerifiedRegionPreservesPositionCoverage()
{
    std::printf("testVerifiedRegionPreservesPositionCoverage ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.evaluation.coverageBox.enabled = false;
    problem.evaluation.coverageBoxes.clear();
    REQUIRE(!problem.context.modelSpec.limits.empty());
    if (problem.context.modelSpec.limits.empty()) return;
    problem.context.modelSpec.limits.back().posMin = -20.0;
    problem.context.modelSpec.limits.back().posMax = 20.0;

    rws::CandidateModelBuildRequest buildRequest;
    buildRequest.spec = problem.context.modelSpec;
    buildRequest.deviceName = problem.context.deviceName;
    buildRequest.tcpFrame = problem.context.tcpFrame;
    buildRequest.checkCollision = false;
    const rws::CandidateModelBuildResult built =
        rws::CandidateModelFactory().build(buildRequest);
    REQUIRE(built.ok);
    if (!built.ok) return;
    rw::kinematics::State referenceState = built.artifact.state;
    built.artifact.device->setQ(rw::math::Q(6, 0.2, -0.4, 0.6, -0.3, 0.5, -0.7),
                                referenceState);

    const rw::math::Transform3D<> worldTtcp = rw::kinematics::Kinematics::frameTframe(
        built.artifact.workcell->getWorldFrame(), built.artifact.tcpFrame.get(),
        referenceState);
    const rw::math::Vector3D<> orientationTarget =
        worldTtcp.P() + worldTtcp.R().getCol(2);

    rws::RequirementExecutionRegion region;
    region.id = "position-versus-orientation-region";
    region.name = "Position versus orientation region";
    region.level = rws::RequirementExecutionLevel::Must;
    region.compileState = rws::RequirementExecutionCompileState::Included;
    region.refFrame = "WORLD";
    region.tcpFrame = built.artifact.tcpFrame->getName();
    region.center = {{worldTtcp.P()[0], worldTtcp.P()[1], worldTtcp.P()[2]}};
    region.size = {{1e-6, 1e-6, 1e-6}};
    region.sampleCounts = {{2, 2, 2}};
    region.orientationMode = rws::RequirementExecutionOrientationMode::PointAtTarget;
    region.orientationTargetPoint = QString("%1,%2,%3")
        .arg(orientationTarget[0], 0, 'g', 17)
        .arg(orientationTarget[1], 0, 'g', 17)
        .arg(orientationTarget[2], 0, 'g', 17).toStdString();
    region.rollSamples = 24;
    region.minimumCoverage = 1.0;
    region.minimumOrientationCoverage = 0.0;
    region.collisionFreeRequired = false;

    rws::AnalysisContextInput contextInput;
    contextInput.workcell = built.artifact.workcell;
    contextInput.device = built.artifact.device;
    contextInput.tcpFrame = built.artifact.tcpFrame;
    contextInput.baseState = referenceState;
    contextInput.modelFingerprint = "position-versus-orientation-model";
    contextInput.environmentFingerprint = "position-versus-orientation-environment";
    contextInput.thresholds = problem.evaluation.thresholds;
    rws::AnalysisContext analysisContext;
    std::string contextError;
    REQUIRE(rws::makeAnalysisContext(contextInput, analysisContext, &contextError));
    const rws::RegionCoverageResult direct =
        rws::RegionCoverageEvaluator().evaluate(analysisContext, region);
    REQUIRE(direct.feasibility == rws::Feasibility::Feasible);
    REQUIRE(direct.positionCoverage == 1.0);
    REQUIRE(direct.orientationCoverage > 0.0);
    REQUIRE(direct.orientationCoverage < direct.positionCoverage);

    region.minimumOrientationCoverage = direct.orientationCoverage;
    problem.requirementExecution.workspaceRegions.push_back(region);
    rws::StructureConstraint positionConstraint;
    positionConstraint.id = "position-coverage-only";
    positionConstraint.targetName = region.id;
    positionConstraint.kind = rws::StructureConstraintKind::MinimumWorkspaceCoverage;
    positionConstraint.threshold = region.minimumCoverage;
    positionConstraint.enabled = true;
    positionConstraint.hard = true;
    problem.constraints.push_back(positionConstraint);

    rws::StructureCandidateResult structureResult;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = [] () { return false; };
    rws::KinematicEngineeringEvaluator(problem).evaluateCandidate(
        structureResult, rws::StructureEvaluationStage::Verified, callbacks, nullptr);
    REQUIRE(structureResult.status == rws::StructureCandidateStatus::Feasible);
    REQUIRE(structureResult.raw.workspaceRegionMetrics.size() == 1);
    REQUIRE(structureResult.raw.workspaceRegionMetrics.size() == 1 &&
            std::abs(structureResult.raw.workspaceRegionMetrics.front().coverage -
                     direct.positionCoverage) < 1e-12);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 覆盖率数据不足:当快速工作区采样数被设为 0 时,工程评价结果必须是
// DataInsufficient 且不产出 coverage 指标——采样缺失不能被伪装成低覆盖率。
static void testWorkspaceCoverageDataInsufficient()
{
    std::printf("testWorkspaceCoverageDataInsufficient ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.evaluation.quickWorkspace.sampleCount = 0;
    rws::KinematicEngineeringEvaluator evaluator(problem);
    rws::CandidateEvaluationContext context;
    const rws::EngineeringEvaluationResult result = evaluator.evaluate(
        context, rws::EvaluationRequest(), rws::EvaluationCallbacks());

    REQUIRE(result.status == rws::EngineeringEvaluationStatus::DataInsufficient);
    REQUIRE(findMetric(result, "kinematics.workspace.coverage") == nullptr);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 覆盖率取消:取消回调在第二次检查时触发后,评价结果必须是 Cancelled,
// 而不是被误判为 Success 或 Infeasible。
static void testWorkspaceCoverageCancellation()
{
    std::printf("testWorkspaceCoverageCancellation ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    rws::KinematicEngineeringEvaluator evaluator(problem);
    rws::CandidateEvaluationContext context;
    int cancellationChecks = 0;
    rws::EvaluationCallbacks callbacks;
    callbacks.isCancellationRequested = [&cancellationChecks]() {
        ++cancellationChecks;
        return cancellationChecks >= 2;
    };
    const rws::EngineeringEvaluationResult result = evaluator.evaluate(
        context, rws::EvaluationRequest(), callbacks);

    REQUIRE(result.status == rws::EngineeringEvaluationStatus::Cancelled);
    REQUIRE(cancellationChecks >= 2);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  UI table models and default variable suggestions
// =============================================================================

static void testUiTableModelsAndSuggestions()
{
    std::printf("testUiTableModelsAndSuggestions ... ");

    rws::StructureDesignVariable variable;
    variable.id = "joint1_z";
    variable.label = "Joint1 Z";
    variable.targetName = "Joint1";
    variable.kind = rws::StructureVariableKind::JointPositionZ;
    variable.currentValue = 0.3;
    variable.minimum = 0.21;
    variable.maximum = 0.39;
    variable.step = 0.001;
    variable.enabled = true;

    rws::StructureVariableTableModel variableModel;
    variableModel.setVariables({variable});
    REQUIRE(variableModel.rowCount() == 1);
    REQUIRE(variableModel.columnCount() >= 8);
    REQUIRE(variableModel.variables().size() == 1);
    REQUIRE(variableModel.variables()[0].id == "joint1_z");
    REQUIRE(variableModel.data(variableModel.index(0, 0)).toString().toStdString() == "joint1_z");

    rws::OptimizationTaskPoint task;
    task.point.id = "pick";
    task.point.name = "Pick";
    task.required = false;

    rws::OptimizationTaskTableModel taskModel;
    taskModel.setTasks({task});
    REQUIRE(taskModel.rowCount() == 1);
    REQUIRE(taskModel.columnCount() >= 7);
    REQUIRE(taskModel.tasks().size() == 1);
    REQUIRE(taskModel.tasks()[0].point.id == "pick");

    rws::StructureCandidateResult candidate;
    candidate.index = 7;
    candidate.feasible = true;
    candidate.totalScore = 88.5;
    candidate.status = rws::StructureCandidateStatus::Feasible;

    rws::StructureCandidateTableModel candidateModel;
    candidateModel.setCandidates({candidate});
    REQUIRE(candidateModel.rowCount() == 1);
    REQUIRE(candidateModel.columnCount() >= 9);
    REQUIRE(candidateModel.candidates().size() == 1);
    REQUIRE(candidateModel.data(candidateModel.index(0, 0)).toInt() == 7);

    rws::StructureOptimizationResult candidateResult;
    rws::StructureCandidateResult baseline = candidate;
    baseline.index = 2;
    baseline.totalScore = 80.0;
    candidateResult.baselineCandidateIndex = 2;
    candidateResult.candidates = {candidate, baseline};
    candidateModel.setResult(candidateResult);
    REQUIRE(candidateModel.candidateByIndex(2) != nullptr);
    REQUIRE(candidateModel.candidateByIndex(2)->totalScore == 80.0);
    REQUIRE(candidateModel.data(candidateModel.index(0,
        rws::StructureCandidateTableModel::ImprovementColumn)).toDouble() == 8.5);

    rws::RobotDesignContext context;
    context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    context.modelSpec.robotName = "SuggestionRobot";
    context.modelSpec.transformJoints[0].pos[2] = 0.3;
    context.modelSpec.robotBaseFrame.pos[2] = 0.2;
    bool setTcpOffset = false;
    for (auto& joint : context.modelSpec.transformJoints) {
        if (rws::typeToKind(joint.type) == rws::JointKind::ToolFrame) {
            joint.pos[0] = 0.05;
            setTcpOffset = true;
            break;
        }
    }
    if (!setTcpOffset) {
        rws::JointTransformSpec tcp;
        tcp.name = "TCP";
        tcp.type = "ToolFrame";
        tcp.pos[0] = 0.05;
        context.modelSpec.transformJoints.push_back(tcp);
    }

    const std::vector< rws::StructureDesignVariable > suggested =
        rws::StructureOptimizationUiLogic::suggestVariables(context);

    bool foundJointZ = false;
    bool foundTcp = false;
    bool foundBaseHeight = false;
    bool foundLinkGeometry = false;
    for (const auto& suggestedVariable : suggested) {
        if (suggestedVariable.kind == rws::StructureVariableKind::JointPositionZ)
            foundJointZ = true;
        if (suggestedVariable.kind == rws::StructureVariableKind::TcpOffsetX ||
            suggestedVariable.kind == rws::StructureVariableKind::TcpOffsetY ||
            suggestedVariable.kind == rws::StructureVariableKind::TcpOffsetZ)
            foundTcp = true;
        if (suggestedVariable.kind == rws::StructureVariableKind::BaseHeight)
            foundBaseHeight = true;
        if (suggestedVariable.kind == rws::StructureVariableKind::LinkRadius ||
            suggestedVariable.kind == rws::StructureVariableKind::LinkWidth ||
            suggestedVariable.kind == rws::StructureVariableKind::LinkHeight)
            foundLinkGeometry = true;
    }
    REQUIRE(foundJointZ);
    REQUIRE(foundTcp);
    REQUIRE(foundBaseHeight);
    REQUIRE(foundLinkGeometry);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureVariableTableDisplayRoles()
{
    std::printf("testStructureVariableTableDisplayRoles ... ");

    rws::StructureDesignVariable length;
    length.id = "link_length";
    length.label = "Link Length";
    length.targetName = "Link1";
    length.kind = rws::StructureVariableKind::LinkHeight;
    length.unit = "m";
    length.currentValue = 0.4;
    length.minimum = 0.2;
    length.maximum = 0.8;
    length.step = 0.01;

    rws::StructureDesignVariable angle = length;
    angle.id = "joint_angle";
    angle.kind = rws::StructureVariableKind::JointRotationYaw;
    angle.unit = "deg";
    angle.currentValue = 12.345;

    rws::StructureVariableTableModel model;
    model.setVariables({length, angle});

    const QModelIndex lengthCurrent = model.index(
        0, rws::StructureVariableTableModel::CurrentColumn);
    const QModelIndex angleCurrent = model.index(
        1, rws::StructureVariableTableModel::CurrentColumn);
    REQUIRE(model.data(lengthCurrent, Qt::DisplayRole).toString() == "0.400 m");
    REQUIRE(model.data(angleCurrent, Qt::DisplayRole).toString() == "12.35 deg");
    REQUIRE(std::abs(model.data(lengthCurrent, Qt::EditRole).toDouble() - 0.4) < 1e-12);
    REQUIRE(model.data(lengthCurrent, Qt::TextAlignmentRole).toInt() ==
            static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
    REQUIRE(model.data(model.index(0, rws::StructureVariableTableModel::EnabledColumn),
                       Qt::TextAlignmentRole).toInt() ==
            static_cast<int>(Qt::AlignCenter));
    REQUIRE(!(model.flags(model.index(0, rws::StructureVariableTableModel::KindColumn)) &
              Qt::ItemIsEditable));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureVariableTableActions()
{
    std::printf("testStructureVariableTableActions ... ");

    rws::StructureDesignVariable first;
    first.id = "first";
    first.label = "First";
    first.targetName = "Joint1";
    first.kind = rws::StructureVariableKind::JointPositionX;

    rws::StructureDesignVariable second = first;
    second.id = "second";
    second.label = "Second";

    rws::StructureDesignVariable third = first;
    third.id = "third";
    third.label = "Third";

    rws::StructureVariableTableModel model;
    model.setVariables({first, third});
    REQUIRE(model.appendVariable(second));
    REQUIRE(model.rowCount() == 3);
    REQUIRE(model.variables().at(2).id == "second");
    REQUIRE(!model.appendVariable(second));
    REQUIRE(model.rowCount() == 3);

    const QModelIndexList selectedIndexes = {
        model.index(2, rws::StructureVariableTableModel::LabelColumn),
        model.index(0, rws::StructureVariableTableModel::IdColumn),
        model.index(2, rws::StructureVariableTableModel::CurrentColumn),
        model.index(0, rws::StructureVariableTableModel::EnabledColumn)};
    REQUIRE(model.removeRows(selectedIndexes) == 2);
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.variables().at(0).id == "third");

    const QModelIndexList lastRow = {
        model.index(0, rws::StructureVariableTableModel::IdColumn)};
    REQUIRE(model.removeRows(lastRow) == 1);
    REQUIRE(model.rowCount() == 0);
    REQUIRE(model.removeRows(lastRow) == 0);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureVariableTableBoundaries()
{
    std::printf("testStructureVariableTableBoundaries ... ");

    rws::StructureDesignVariable baseline;
    baseline.id = "length";
    baseline.label = "Link Length";
    baseline.targetName = "Link1";
    baseline.kind = rws::StructureVariableKind::LinkHeight;
    baseline.unit = "m";
    baseline.currentValue = 0.5;
    baseline.minimum = 0.0;
    baseline.maximum = 1.0;
    baseline.step = 0.1;
    baseline.preferredValue = 0.5;
    baseline.preferenceWeight = 0.0;

    rws::StructureVariableTableModel model;
    model.setVariables({baseline});
    const auto requireReadOnly = [&model](rws::StructureVariableTableModel::Column column) {
        REQUIRE(!(model.flags(model.index(0, column)) & Qt::ItemIsEditable));
    };
    requireReadOnly(rws::StructureVariableTableModel::IdColumn);
    requireReadOnly(rws::StructureVariableTableModel::LabelColumn);
    requireReadOnly(rws::StructureVariableTableModel::TargetColumn);
    requireReadOnly(rws::StructureVariableTableModel::KindColumn);
    REQUIRE(model.flags(model.index(0, rws::StructureVariableTableModel::EnabledColumn)) &
            Qt::ItemIsUserCheckable);
    REQUIRE(!model.setData(
        model.index(0, rws::StructureVariableTableModel::IdColumn), "changed"));

    REQUIRE(model.setData(
        model.index(0, rws::StructureVariableTableModel::CurrentColumn), 0.6));
    REQUIRE(model.setData(
        model.index(0, rws::StructureVariableTableModel::MinimumColumn), 0.5));
    REQUIRE(model.setData(
        model.index(0, rws::StructureVariableTableModel::MaximumColumn), 0.8));
    REQUIRE(model.setData(
        model.index(0, rws::StructureVariableTableModel::StepColumn), 0.05));
    REQUIRE(!model.setData(
        model.index(0, rws::StructureVariableTableModel::CurrentColumn), 0.9));
    REQUIRE(!model.setData(
        model.index(0, rws::StructureVariableTableModel::MinimumColumn), 0.7));
    REQUIRE(!model.setData(
        model.index(0, rws::StructureVariableTableModel::MaximumColumn), 0.55));
    REQUIRE(!model.setData(
        model.index(0, rws::StructureVariableTableModel::StepColumn), 0.0));
    REQUIRE(!model.setData(
        model.index(0, rws::StructureVariableTableModel::StepColumn),
        std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(std::abs(model.variables().at(0).currentValue - 0.6) < 1e-12);
    REQUIRE(std::abs(model.variables().at(0).minimum - 0.5) < 1e-12);
    REQUIRE(std::abs(model.variables().at(0).maximum - 0.8) < 1e-12);
    REQUIRE(std::abs(model.variables().at(0).step - 0.05) < 1e-12);

    REQUIRE(model.setPreferences(0, 0.65, 0.4));
    REQUIRE(!model.setPreferences(0, 0.65, 1.1));
    REQUIRE(!model.setPreferences(0, std::numeric_limits<double>::quiet_NaN(), 0.4));
    REQUIRE(std::abs(model.variables().at(0).preferredValue - 0.65) < 1e-12);
    REQUIRE(std::abs(model.variables().at(0).preferenceWeight - 0.4) < 1e-12);

    REQUIRE(model.duplicateVariable(0) == 1);
    REQUIRE(model.rowCount() == 2);
    REQUIRE(model.variables().at(1).id == "length_copy_1");
    REQUIRE(model.variables().at(1).label == "Link Length (Copy)");
    REQUIRE(model.removeVariable(0));
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.variables().at(0).id == "length_copy_1");
    REQUIRE(!model.removeVariable(4));
    REQUIRE(model.duplicateVariable(4) == -1);

    model.resetVariables({baseline});
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.variables().at(0).id == "length");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureVariableFilterProxy()
{
    std::printf("testStructureVariableFilterProxy ... " );
    rws::StructureDesignVariable first;
    first.id = "joint1_x"; first.label = "Joint One X"; first.targetName = "Joint1";
    first.kind = rws::StructureVariableKind::JointPositionX;
    rws::StructureDesignVariable second = first;
    second.id = "link_radius"; second.label = "Link Radius"; second.targetName = "Link1";
    second.kind = rws::StructureVariableKind::LinkRadius;
    rws::StructureVariableTableModel source;
    source.setVariables({first, second});
    rws::StructureVariableFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setKeyword(QStringLiteral("joint1"));
    REQUIRE(proxy.rowCount() == 1);
    REQUIRE(proxy.index(0, rws::StructureVariableTableModel::IdColumn).data().toString() ==
            QStringLiteral("joint1_x"));
    proxy.setKeyword(QStringLiteral("link"));
    proxy.setKindFilter(rws::StructureVariableKind::JointPositionX);
    REQUIRE(proxy.rowCount() == 0);
    proxy.setKindFilter(std::nullopt);
    REQUIRE(proxy.rowCount() == 1);
    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureVariableAdvancedColumns()
{
    std::printf("testStructureVariableAdvancedColumns ... " );
    rws::StructureDesignVariable variable;
    variable.id = "joint1_x"; variable.label = "Joint One X";
    variable.targetName = "Joint1"; variable.unit = "m";
    variable.kind = rws::StructureVariableKind::JointPositionX;
    variable.currentValue = 0.2; variable.minimum = 0.0; variable.maximum = 1.0;
    variable.step = 0.1; variable.preferredValue = 0.3; variable.preferenceWeight = 0.4;
    rws::StructureVariableTableModel model;
    model.setVariables({variable});
    REQUIRE(model.headerData(rws::StructureVariableTableModel::PreferredColumn, Qt::Horizontal)
                .toString() == QStringLiteral("Preferred"));
    REQUIRE(model.headerData(rws::StructureVariableTableModel::PreferenceWeightColumn,
                             Qt::Horizontal).toString() == QStringLiteral("Preference Weight"));
    REQUIRE(model.data(model.index(0, rws::StructureVariableTableModel::PreferredColumn),
                       Qt::EditRole).toDouble() == 0.3);
    REQUIRE(model.data(model.index(0, rws::StructureVariableTableModel::PreferenceWeightColumn),
                       Qt::TextAlignmentRole).toInt() ==
            static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
    REQUIRE(model.setData(model.index(0, rws::StructureVariableTableModel::PreferredColumn),
                          0.5));
    REQUIRE(model.setData(model.index(0,
                                      rws::StructureVariableTableModel::PreferenceWeightColumn),
                          0.8));
    REQUIRE(!model.setData(model.index(0,
                                       rws::StructureVariableTableModel::PreferenceWeightColumn),
                           1.1));
    REQUIRE(std::abs(model.variables().at(0).preferredValue - 0.5) < 1e-12);
    REQUIRE(std::abs(model.variables().at(0).preferenceWeight - 0.8) < 1e-12);
    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureOptimizerWidgetUsesEnglishCopy()
{
    rws::StructureOptimizerWidget widget;
    QTabWidget* tabs = widget.findChild<QTabWidget*>("structureOptimizerTabs");
    REQUIRE(tabs != nullptr);
    REQUIRE(tabs->tabText(0) == "Design Variables");
    REQUIRE(tabs->tabText(1) == "Tasks & Constraints");
    REQUIRE(tabs->tabText(2) == "Optimization Settings");
    REQUIRE(tabs->tabText(3) == "Candidates");
    REQUIRE(tabs->tabText(4) == "Export Report");

    const auto requireButtonText = [&widget](const char* objectName, const QString& expected) {
        QPushButton* button = widget.findChild<QPushButton*>(objectName);
        REQUIRE(button != nullptr);
        REQUIRE(button->text() == expected);
    };
    requireButtonText("addOptimizationTaskButton", "Add Task");
    requireButtonText("previewStructureCandidateButton", "Preview Candidate");
    requireButtonText("exportStructureOptimizationResultButton", "Export Report & Models");
    requireButtonText("addStructureVariableButton", "Add Variable");
    requireButtonText("duplicateStructureVariableButton", "Duplicate Selected");
    requireButtonText("removeStructureVariablesButton", "Remove Selected");
    requireButtonText("restoreStructureVariableBaselineButton", "Restore Model Baseline");

    QPushButton* removeVariablesButton =
        widget.findChild<QPushButton*>("removeStructureVariablesButton");
    QPushButton* duplicateVariableButton =
        widget.findChild<QPushButton*>("duplicateStructureVariableButton");
    REQUIRE(removeVariablesButton != nullptr);
    REQUIRE(duplicateVariableButton != nullptr);
    if (removeVariablesButton != nullptr)
        REQUIRE(!removeVariablesButton->isEnabled());
    if (duplicateVariableButton != nullptr)
        REQUIRE(!duplicateVariableButton->isEnabled());

    QTableView* variableTable = widget.findChild<QTableView*>("structureVariableTable");
    REQUIRE(variableTable != nullptr);
    if (variableTable != nullptr) {
        const QStringList expectedHeaders = {
            "ID", "Name", "Target", "Type", "Current", "Min", "Max", "Step",
            "Preferred", "Preference Weight", "Enabled"};
        for (int column = 0; column < expectedHeaders.size(); ++column) {
            REQUIRE(variableTable->model()->headerData(
                        column, Qt::Horizontal, Qt::DisplayRole).toString() ==
                    expectedHeaders[column]);
        }
        REQUIRE(variableTable->alternatingRowColors());
        REQUIRE(variableTable->selectionBehavior() == QAbstractItemView::SelectRows);
        REQUIRE(variableTable->selectionMode() == QAbstractItemView::ExtendedSelection);
        REQUIRE(variableTable->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded);
        REQUIRE(variableTable->horizontalHeader()->sectionResizeMode(
                    rws::StructureVariableTableModel::EnabledColumn) == QHeaderView::Fixed);
        REQUIRE(variableTable->columnWidth(
                    rws::StructureVariableTableModel::EnabledColumn) == 56);
        for (const int column : {rws::StructureVariableTableModel::CurrentColumn,
                                 rws::StructureVariableTableModel::MinimumColumn,
                                 rws::StructureVariableTableModel::MaximumColumn,
                                 rws::StructureVariableTableModel::StepColumn})
            REQUIRE(variableTable->horizontalHeader()->sectionResizeMode(column) ==
                    QHeaderView::Stretch);
    }

    const auto requireTableHeaders = [&widget](const char* objectName,
                                                const QStringList& expectedHeaders) {
        QTableView* table = widget.findChild<QTableView*>(objectName);
        REQUIRE(table != nullptr);
        if (table != nullptr) {
            for (int column = 0; column < expectedHeaders.size(); ++column) {
                REQUIRE(table->model()->headerData(
                            column, Qt::Horizontal, Qt::DisplayRole).toString() ==
                        expectedHeaders[column]);
            }
        }
    };
    requireTableHeaders("optimizationTaskTable",
                        {"ID", "Name", "Required", "Enabled", "X", "Y", "Z", "Roll",
                         "Pitch", "Yaw", "Frame", "TCP", "Weight"});
    requireTableHeaders("structureConstraintTable",
                        {"ID", "Name", "Target", "Type", "Limit", "Aux. Limit", "Enabled",
                         "Hard"});
    requireTableHeaders("structureCandidateTable",
                        {"#", "Feasible", "Score", "Reachability", "Manipulability",
                         "Joint Margin", "Collision-Free", "Length", "Improvement"});

    rws::StructureCandidateResult feasibleCandidate;
    feasibleCandidate.feasible = true;
    rws::StructureCandidateTableModel candidateModel;
    candidateModel.setCandidates({feasibleCandidate});
    REQUIRE(candidateModel.data(candidateModel.index(
                0, rws::StructureCandidateTableModel::FeasibleColumn)).toString() == "Yes");
}

static void testStructureOptimizerWidgetPhaseOneControls()
{
    std::printf("testStructureOptimizerWidgetPhaseOneControls ... ");
    rws::StructureOptimizerWidget widget;
    QComboBox* templates = widget.findChild<QComboBox*>(
        "structureOptimizationTemplateCombo");
    QPushButton* applyTemplate = widget.findChild<QPushButton*>(
        "applyStructureOptimizationTemplateButton");
    QPushButton* preflight = widget.findChild<QPushButton*>(
        "preflightStructureOptimizationButton");
    QPushButton* baseline = widget.findChild<QPushButton*>(
        "evaluateStructureBaselineButton");
    QPushButton* compare = widget.findChild<QPushButton*>(
        "compareStructureCandidatesButton");
    QLabel* preflightSummary = widget.findChild<QLabel*>(
        "structureOptimizationPreflightLabel");
    QLabel* baselineSummary = widget.findChild<QLabel*>(
        "structureOptimizationBaselineLabel");
    QLabel* comparisonSummary = widget.findChild<QLabel*>(
        "structureCandidateComparisonLabel");
    REQUIRE(templates != nullptr);
    REQUIRE(applyTemplate != nullptr);
    REQUIRE(preflight != nullptr);
    REQUIRE(baseline != nullptr);
    REQUIRE(compare != nullptr);
    REQUIRE(preflightSummary != nullptr);
    REQUIRE(baselineSummary != nullptr);
    REQUIRE(comparisonSummary != nullptr);
    REQUIRE(templates != nullptr && templates->count() == 4);
    REQUIRE(preflight != nullptr && preflight->isEnabled());
    REQUIRE(baseline != nullptr && !baseline->isEnabled());
    REQUIRE(compare != nullptr && !compare->isEnabled());

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;
    problem.variables = rws::StructureOptimizationUiLogic::suggestVariables(problem.context);
    rws::OptimizationTaskPoint task;
    task.point.id = "phase1-task";
    task.point.enabled = true;
    task.point.position = {0.3, 0.0, 0.3};
    problem.tasks.push_back(task);
    widget.setProblem(problem);
    REQUIRE(applyTemplate->isEnabled());
    templates->setCurrentIndex(1);
    applyTemplate->click();
    REQUIRE(widget.collectProblem().objectives.front().metricId ==
            "kinematics.reachability.weighted");
    REQUIRE(preflightSummary->text().contains(QStringLiteral("Preflight"),
                                              Qt::CaseInsensitive));
    REQUIRE(baseline->isEnabled());

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureOptimizerWidgetVariableEfficiencyControls()
{
    rws::StructureOptimizerWidget widget;
    QTableView* table = widget.findChild<QTableView*>("structureVariableTable");
    QLineEdit* search = widget.findChild<QLineEdit*>("structureVariableSearch");
    QComboBox* typeFilter = widget.findChild<QComboBox*>("structureVariableTypeFilter");
    QCheckBox* showAdvanced = widget.findChild<QCheckBox*>("showStructureVariableAdvanced");
    QPushButton* addMissing =
        widget.findChild<QPushButton*>("addMissingStructureVariablesButton");
    QPushButton* duplicate =
        widget.findChild<QPushButton*>("duplicateStructureVariableButton");
    QToolButton* more = widget.findChild<QToolButton*>("structureVariableMoreButton");
    REQUIRE(table != nullptr);
    REQUIRE(search != nullptr);
    REQUIRE(typeFilter != nullptr);
    REQUIRE(showAdvanced != nullptr);
    REQUIRE(addMissing != nullptr);
    REQUIRE(duplicate != nullptr);
    REQUIRE(more != nullptr);
    if (table == nullptr || search == nullptr || typeFilter == nullptr ||
        showAdvanced == nullptr || addMissing == nullptr || duplicate == nullptr || more == nullptr)
        return;

    REQUIRE(addMissing->isHidden());
    REQUIRE(more->menu() != nullptr);

    REQUIRE(qobject_cast<rws::StructureVariableFilterProxyModel*>(table->model()) != nullptr);
    REQUIRE(table->isColumnHidden(rws::StructureVariableTableModel::PreferredColumn));
    REQUIRE(table->isColumnHidden(rws::StructureVariableTableModel::PreferenceWeightColumn));
    showAdvanced->setChecked(true);
    REQUIRE(!table->isColumnHidden(rws::StructureVariableTableModel::PreferredColumn));
    REQUIRE(!table->isColumnHidden(rws::StructureVariableTableModel::PreferenceWeightColumn));

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;
    const std::vector<rws::StructureDesignVariable> suggested =
        rws::StructureOptimizationUiLogic::suggestVariables(problem.context);
    REQUIRE(!suggested.empty());
    if (suggested.empty())
        return;
    rws::StructureDesignVariable edited = suggested.front();
    edited.currentValue = edited.minimum + (edited.maximum - edited.minimum) * 0.25;
    problem.variables = {edited};
    widget.setProblem(problem);
    REQUIRE(table->model()->rowCount() == 1);
    REQUIRE(addMissing->isEnabled());
    addMissing->click();
    const rws::StructureOptimizationProblem completed = widget.collectProblem();
    REQUIRE(completed.variables.size() == suggested.size());
    const auto editedIt = std::find_if(completed.variables.begin(), completed.variables.end(),
                                       [&edited](const rws::StructureDesignVariable& variable) {
                                           return variable.id == edited.id;
                                       });
    REQUIRE(editedIt != completed.variables.end());
    if (editedIt != completed.variables.end())
        REQUIRE(std::abs(editedIt->currentValue - edited.currentValue) < 1e-12);

    search->setText(QStringLiteral("not-a-variable"));
    REQUIRE(table->model()->rowCount() == 0);
    search->clear();
    typeFilter->setCurrentIndex(0);
    REQUIRE(table->model()->rowCount() == static_cast<int>(suggested.size()));
    table->selectRow(0);
    const QString selectedId = table->model()->index(
        0, rws::StructureVariableTableModel::IdColumn).data().toString();
    duplicate->click();
    const rws::StructureOptimizationProblem duplicated = widget.collectProblem();
    REQUIRE(duplicated.variables.size() == suggested.size() + 1);
    REQUIRE(std::any_of(duplicated.variables.begin(), duplicated.variables.end(),
                        [&selectedId](const rws::StructureDesignVariable& variable) {
                            return variable.id == (selectedId + QStringLiteral("_copy_1")).toStdString();
                        }));
}

// 子套件 约束表模型 + 项目适配器:验证约束表模型能编辑阈值;项目 saveProject/
// loadProject 往返保留约束、策略、局部精英数、权重等设置,且保存的模型输出目录是
// 相对路径(项目可整体搬迁),加载后仍被解析回可直接使用的绝对目录。
static void testConstraintModelAndProjectAdapter()
{
    std::printf("testConstraintModelAndProjectAdapter ... ");

    rws::StructureConstraint constraint;
    constraint.id = "workspace";
    constraint.label = "Workspace coverage";
    constraint.targetName = "workcell";
    constraint.kind = rws::StructureConstraintKind::MinimumWorkspaceCoverage;
    constraint.threshold = 0.85;
    constraint.secondaryThreshold = 0.05;
    constraint.enabled = true;
    constraint.hard = true;

    rws::StructureConstraintTableModel constraintModel;
    constraintModel.setConstraints({constraint});
    REQUIRE(constraintModel.rowCount() == 1);
    REQUIRE(constraintModel.constraints().at(0).id == "workspace");
    REQUIRE(constraintModel.setData(
        constraintModel.index(0, rws::StructureConstraintTableModel::ThresholdColumn),
        0.9));
    REQUIRE(std::abs(constraintModel.constraints().at(0).threshold - 0.9) < 1e-12);

    rws::StructureOptimizationProblem original;
    original.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    original.context.robotName = original.context.modelSpec.robotName;
    original.context.deviceName = original.context.modelSpec.robotName;
    original.constraints.push_back(constraint);
    original.run.strategy = rws::StructureStrategyKind::Grid;
    original.run.gridSteps = 7;
    original.run.localEliteCount = 4;
    original.weights.preference = 0.12;

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString modelOutputDirectory = dir.filePath("model-output");
    REQUIRE(QDir().mkpath(modelOutputDirectory));
    original.context.modelSpec.saveDirectory = modelOutputDirectory.toStdString();
    const QString path = dir.filePath("example.structure-optimization.json");
    QString error;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        path, original, 12, &error));

    // 项目资源可随目录整体搬迁。验证写入的模型输出目录是相对路径，而加载后仍被
    // Adapter 解析回可直接供编辑器和导出服务使用的绝对目录。
    QFile serializedFile(path);
    REQUIRE(serializedFile.open(QIODevice::ReadOnly));
    QJsonParseError jsonError;
    const QJsonDocument serializedDocument =
        QJsonDocument::fromJson(serializedFile.readAll(), &jsonError);
    serializedFile.close();
    REQUIRE(jsonError.error == QJsonParseError::NoError && serializedDocument.isObject());
    const QString savedDirectory = serializedDocument.object().value("problem").toObject()
        .value("context").toObject().value("data").toObject().value("modelSpec").toObject()
        .value("saveDirectory").toString();
    REQUIRE(!savedDirectory.isEmpty() && QFileInfo(savedDirectory).isRelative());

    rws::StructureOptimizationProblem loaded;
    int selectedCandidateIndex = -1;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        path, loaded, &selectedCandidateIndex, &error));
    REQUIRE(selectedCandidateIndex == 12);
    REQUIRE(loaded.context.modelSpec.robotName == original.context.modelSpec.robotName);
    REQUIRE(loaded.constraints.size() == 1);
    REQUIRE(loaded.constraints[0].id == "workspace");
    REQUIRE(loaded.run.strategy == rws::StructureStrategyKind::Grid);
    REQUIRE(loaded.run.gridSteps == 7);
    REQUIRE(loaded.run.localEliteCount == 4);
    REQUIRE(std::abs(loaded.weights.preference - 0.12) < 1e-12);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 冻结工程需求工件适配器:手工构造与 compiled 快照一致的 v4 工件
// (含执行契约与执行指纹),验证适配层只接受已冻结且模型指纹一致的需求——
// Must 工位/覆盖盒转为硬约束与独立评价区域,Should 区域不阻断;v3 工件被拒绝
// 并要求重冻结;多个 Must 区域各自成为独立覆盖盒,不能合并成单个 WORLD 盒。
static void testFrozenEngineeringRequirementArtifactAdapter()
{
    std::printf("testFrozenEngineeringRequirementArtifactAdapter ... ");

    // 使用与结构优化项目相同的 RobotModelSpec 构造冻结工件，验证适配层只
    // 接收已冻结且模型身份一致的工程需求，而不是直接读取编辑态表单数据。
    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;

    rws::FrozenRequirementArtifact artifact;
    artifact.schemaVersion = 4;
    artifact.requirementFingerprint = "requirement-fingerprint";
    artifact.environmentFingerprint = "environment-fingerprint";
    artifact.workcellFingerprint = "workcell-fingerprint";
    artifact.frozenAt = "2026-07-30T09:15:00.123Z";
    artifact.modelBinding.robotName = problem.context.modelSpec.robotName;
    artifact.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(problem.context.modelSpec);
    artifact.compiled.frozen = true;
    artifact.compiled.modelBinding = artifact.modelBinding;
    artifact.compiled.requirementFingerprint = artifact.requirementFingerprint;
    artifact.scenario.environmentFingerprint = artifact.environmentFingerprint;
    artifact.frozenRobotState.deviceName = artifact.modelBinding.robotName;
    artifact.frozenRobotState.tcpFrameName = "TCP";
    artifact.frozenRobotState.kinematicFingerprint = "robot-kinematic-fingerprint";
    artifact.frozenRobotState.tcpWorldPose[15] = 1.0;
    artifact.frozenRobotState.capturedAt = artifact.frozenAt;

    rws::CompiledPoseTask mustStation;
    mustStation.id = "pick";
    mustStation.name = "Pick station";
    mustStation.level = rws::RequirementLevel::Must;
    mustStation.refFrame = "WORLD";
    mustStation.tcpFrame = "TCP";
    mustStation.position = {{0.4, 0.1, 0.3}};
    mustStation.tolerance.positionMeters = 0.002;
    mustStation.validation.collisionFreeRequired = true;
    artifact.compiled.poseTasks.push_back(mustStation);

    rws::CompiledPoseTask shouldStation = mustStation;
    shouldStation.id = "inspect";
    shouldStation.name = "Inspection station";
    shouldStation.level = rws::RequirementLevel::Should;
    shouldStation.validation.collisionFreeRequired = false;
    artifact.compiled.poseTasks.push_back(shouldStation);

    rws::WorkspaceDemandRegion region;
    region.id = "work_area";
    region.name = "Work area";
    region.level = rws::RequirementLevel::Must;
    region.refFrame = "WORLD";
    region.tcpFrame = "TCP";
    region.center = {{0.5, 0.0, 0.4}};
    region.size = {{0.4, 0.2, 0.3}};
    region.minimumCoverage = 0.85;
    region.sampleSpacingMeters = {{0.08, 0.04, 0.06}};
    region.orientationMode = rws::OrientationMode::PointAtTarget;
    region.orientationTargetPoint = "0.8,0.0,0.4";
    region.directionSamples = 2;
    region.rollSamples = 3;
    region.minimumOrientationCoverage = 0.5;
    region.minimumVerificationStage = rws::RequirementVerificationStage::Verified;
    region.collisionFreeRequired = false;
    artifact.compiled.workspaceRegions.push_back(region);

    // Excluded entries do not become executable tasks, but their reason must
    // remain in the frozen execution diagnostics for downstream audit.
    rws::CompiledPoseTask excludedShouldStation = shouldStation;
    excludedShouldStation.id = "optional_legacy_station";
    excludedShouldStation.compileState = rws::RequirementCompileState::Excluded;
    excludedShouldStation.excludedReason = "REQ_FRAME_NOT_RESOLVED";
    artifact.compiled.poseTasks.push_back(excludedShouldStation);

    // 结构优化器只消费冻结 execution 契约：provenance 与顶层指纹逐项对齐，
    // 工位与覆盖盒作为完整已冻结执行输入，最后计算其不可变执行指纹。
    artifact.execution.schemaVersion = 1;
    artifact.execution.provenance.requirementFingerprint = artifact.requirementFingerprint;
    artifact.execution.provenance.robotModelFingerprint = artifact.modelBinding.robotModelFingerprint;
    artifact.execution.provenance.workcellFingerprint = artifact.workcellFingerprint;
    artifact.execution.provenance.environmentFingerprint = artifact.environmentFingerprint;
    artifact.execution.provenance.compilerVersion = artifact.compilerVersion;
    artifact.execution.provenance.frozenAt = artifact.frozenAt;
    artifact.execution.provenance.sourcePath = artifact.modelBinding.sourcePath;
    // 工位任务点从 compiled 投影到执行契约类型(枚举用 static_cast 同序映射)。
    for (const rws::CompiledPoseTask& source : artifact.compiled.poseTasks) {
        if (source.compileState != rws::RequirementCompileState::Included) continue;
        rws::RequirementExecutionTask task;
        task.id = source.id;
        task.name = source.name;
        task.level = static_cast<rws::RequirementExecutionLevel>(source.level);
        task.compileState = static_cast<rws::RequirementExecutionCompileState>(source.compileState);
        task.processType = static_cast<rws::RequirementExecutionProcessType>(source.processType);
        task.excludedReason = source.excludedReason;
        task.refFrame = source.refFrame;
        task.tcpFrame = source.tcpFrame;
        task.position = source.position;
        task.rpyDeg = source.rpyDeg;
        task.positionToleranceMeters = source.tolerance.positionMeters;
        task.orientationToleranceDeg = source.tolerance.orientationDeg;
        task.allowToolRollFree = source.tolerance.allowToolRollFree;
        task.collisionFreeRequired = source.validation.collisionFreeRequired;
        artifact.execution.tasks.push_back(task);
    }
    rws::RequirementExecutionDiagnostic excludedDiagnostic;
    excludedDiagnostic.code = "REQ_FRAME_NOT_RESOLVED";
    excludedDiagnostic.severity = rws::RequirementExecutionDiagnosticSeverity::Warning;
    excludedDiagnostic.requirementId = excludedShouldStation.id;
    excludedDiagnostic.field = "refFrame";
    excludedDiagnostic.message = excludedShouldStation.excludedReason;
    excludedDiagnostic.source = "EngineeringRequirements";
    artifact.execution.diagnostics.push_back(excludedDiagnostic);
    // 覆盖盒同样投影到执行契约类型。
    for (const rws::WorkspaceDemandRegion& source : artifact.compiled.workspaceRegions) {
        rws::RequirementExecutionRegion executionRegion;
        executionRegion.id = source.id;
        executionRegion.name = source.name;
        executionRegion.level = static_cast<rws::RequirementExecutionLevel>(source.level);
        executionRegion.compileState = static_cast<rws::RequirementExecutionCompileState>(source.compileState);
        executionRegion.excludedReason = source.excludedReason;
        executionRegion.refFrame = source.refFrame;
        executionRegion.tcpFrame = source.tcpFrame;
        executionRegion.center = source.center;
        executionRegion.size = source.size;
        executionRegion.minimumCoverage = source.minimumCoverage;
        executionRegion.sampleSpacingMeters = source.sampleSpacingMeters;
        executionRegion.sampleCounts = {{6, 6, 6}};
        executionRegion.minimumVerificationStage =
            static_cast<rws::RequirementExecutionStage>(source.minimumVerificationStage);
        executionRegion.orientationMode =
            static_cast<rws::RequirementExecutionOrientationMode>(source.orientationMode);
        executionRegion.orientationTargetPoint = source.orientationTargetPoint;
        executionRegion.directionSamples = source.directionSamples;
        executionRegion.rollSamples = source.rollSamples;
        executionRegion.minimumOrientationCoverage = source.minimumOrientationCoverage;
        executionRegion.collisionFreeRequired = source.collisionFreeRequired;
        artifact.execution.workspaceRegions.push_back(executionRegion);
    }
    // 依据执行契约计算执行指纹，供适配器的执行契约一致性校验使用。
    artifact.executionFingerprint = rws::RequirementExecutionJson::fingerprint(artifact.execution);

    std::string error;
    REQUIRE(rws::EngineeringRequirementArtifactAdapter::apply(artifact, problem, &error));
    REQUIRE(problem.tasks.size() == 2);
    REQUIRE(problem.tasks[0].required);
    REQUIRE(!problem.tasks[1].required);
    REQUIRE(problem.context.taskPoints.size() == 2);
    REQUIRE(problem.evaluation.coverageBox.enabled);
    REQUIRE(problem.evaluation.coverageBox.cells[0] == 5);
    REQUIRE(problem.requirementProvenance.requirementFingerprint == "requirement-fingerprint");
    REQUIRE(problem.requirementProvenance.workcellFingerprint == "workcell-fingerprint");
    REQUIRE(problem.requirementProvenance.environmentFingerprint == "environment-fingerprint");
    REQUIRE(problem.requirementProvenance.frozenAt == "2026-07-30T09:15:00.123Z");
    REQUIRE(problem.requirementExecution.provenance.requirementFingerprint ==
            artifact.execution.provenance.requirementFingerprint);
    REQUIRE(problem.requirementExecution.tasks.size() == artifact.execution.tasks.size());
    REQUIRE(problem.requirementExecution.tasks[0].collisionFreeRequired);
    REQUIRE(!problem.requirementExecution.tasks[1].collisionFreeRequired);
    REQUIRE(problem.requirementExecution.workspaceRegions.size() ==
            artifact.execution.workspaceRegions.size());
    REQUIRE(problem.requirementExecution.workspaceRegions.front().minimumVerificationStage ==
            rws::RequirementExecutionStage::Verified);
    REQUIRE(problem.requirementExecution.workspaceRegions.front().orientationMode ==
            artifact.execution.workspaceRegions.front().orientationMode);
    REQUIRE(problem.requirementExecution.workspaceRegions.front().orientationTargetPoint ==
            artifact.execution.workspaceRegions.front().orientationTargetPoint);
    REQUIRE(problem.requirementExecution.workspaceRegions.front().directionSamples == 2);
    REQUIRE(problem.requirementExecution.workspaceRegions.front().rollSamples == 3);
    REQUIRE(std::abs(problem.requirementExecution.workspaceRegions.front().minimumOrientationCoverage - 0.5) < 1e-12);
    REQUIRE(!problem.requirementExecution.workspaceRegions.front().collisionFreeRequired);
    REQUIRE(problem.requirementExecution.diagnostics.size() == 1);
    REQUIRE(problem.requirementExecution.diagnostics.front().requirementId ==
            "optional_legacy_station");
    REQUIRE(problem.requirementExecution.diagnostics.front().message ==
            "REQ_FRAME_NOT_RESOLVED");

    const auto requireFailedApplyUnchanged = [] (
        const rws::StructureOptimizationProblem& before,
        const rws::StructureOptimizationProblem& after) {
        REQUIRE(rws::StructureOptimizationJson::problemToJson(after) ==
                rws::StructureOptimizationJson::problemToJson(before));
        REQUIRE(after.tasks.size() == before.tasks.size());
        REQUIRE(after.context.taskPoints.size() == before.context.taskPoints.size());
        REQUIRE(after.evaluation.coverageBoxes.size() == before.evaluation.coverageBoxes.size());
        REQUIRE(after.requirementExecution.tasks.size() == before.requirementExecution.tasks.size());
        REQUIRE(after.requirementExecution.workspaceRegions.size() ==
                before.requirementExecution.workspaceRegions.size());
        REQUIRE(after.requirementExecution.diagnostics.size() ==
                before.requirementExecution.diagnostics.size());
        REQUIRE(after.requirementProvenance.requirementFingerprint ==
                before.requirementProvenance.requirementFingerprint);
        REQUIRE(after.requirementProvenance.executionFingerprint ==
                before.requirementProvenance.executionFingerprint);
    };

    // A Must execution item may never be silently skipped merely because its
    // frozen compile state is Excluded or Invalid.
    rws::FrozenRequirementArtifact excludedMustTaskArtifact = artifact;
    excludedMustTaskArtifact.execution.tasks.front().compileState =
        rws::RequirementExecutionCompileState::Excluded;
    excludedMustTaskArtifact.executionFingerprint =
        rws::RequirementExecutionJson::fingerprint(excludedMustTaskArtifact.execution);
    const rws::StructureOptimizationProblem excludedMustTaskBaseline = problem;
    rws::StructureOptimizationProblem excludedMustTaskTarget = excludedMustTaskBaseline;
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(
        excludedMustTaskArtifact, excludedMustTaskTarget, &error));
    REQUIRE(error.find("pick") != std::string::npos);
    REQUIRE(error.find("Excluded") != std::string::npos);
    requireFailedApplyUnchanged(excludedMustTaskBaseline, excludedMustTaskTarget);

    rws::FrozenRequirementArtifact invalidMustRegionArtifact = artifact;
    invalidMustRegionArtifact.execution.workspaceRegions.front().compileState =
        rws::RequirementExecutionCompileState::Invalid;
    invalidMustRegionArtifact.executionFingerprint =
        rws::RequirementExecutionJson::fingerprint(invalidMustRegionArtifact.execution);
    const rws::StructureOptimizationProblem invalidMustRegionBaseline = problem;
    rws::StructureOptimizationProblem invalidMustRegionTarget = invalidMustRegionBaseline;
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(
        invalidMustRegionArtifact, invalidMustRegionTarget, &error));
    REQUIRE(error.find("work_area") != std::string::npos);
    REQUIRE(error.find("Invalid") != std::string::npos);
    requireFailedApplyUnchanged(invalidMustRegionBaseline, invalidMustRegionTarget);

    rws::FrozenRequirementArtifact sourcePathMismatchArtifact = artifact;
    sourcePathMismatchArtifact.execution.provenance.sourcePath = "tampered-requirements.json";
    sourcePathMismatchArtifact.executionFingerprint =
        rws::RequirementExecutionJson::fingerprint(sourcePathMismatchArtifact.execution);
    REQUIRE(sourcePathMismatchArtifact.execution.provenance.sourcePath !=
            sourcePathMismatchArtifact.modelBinding.sourcePath);
    const rws::StructureOptimizationProblem sourcePathMismatchBaseline = problem;
    rws::StructureOptimizationProblem sourcePathMismatchTarget = sourcePathMismatchBaseline;
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(
        sourcePathMismatchArtifact, sourcePathMismatchTarget, &error));
    REQUIRE(error.find("provenance") != std::string::npos);
    requireFailedApplyUnchanged(sourcePathMismatchBaseline, sourcePathMismatchTarget);

    // v4 execution is authoritative.  A stale compiled audit snapshot must
    // neither reject the frozen execution contract nor leak its tasks/regions.
    rws::FrozenRequirementArtifact compiledDriftArtifact = artifact;
    compiledDriftArtifact.compiled.frozen = false;
    compiledDriftArtifact.compiled.requirementFingerprint = "compiled-drift";
    compiledDriftArtifact.compiled.modelBinding.robotModelFingerprint = "compiled-drift";
    compiledDriftArtifact.compiled.poseTasks.clear();
    compiledDriftArtifact.compiled.workspaceRegions.clear();
    compiledDriftArtifact.compiled.diagnostics.clear();
    rws::StructureOptimizationProblem executionAuthorityProblem = problem;
    REQUIRE(rws::EngineeringRequirementArtifactAdapter::apply(
        compiledDriftArtifact, executionAuthorityProblem, &error));
    REQUIRE(executionAuthorityProblem.tasks.size() == artifact.execution.tasks.size());
    REQUIRE(executionAuthorityProblem.tasks[0].point.id == "pick");
    REQUIRE(executionAuthorityProblem.evaluation.coverageBoxes.size() == 1);
    REQUIRE(executionAuthorityProblem.evaluation.coverageBoxes.front().id == "work_area");
    REQUIRE(executionAuthorityProblem.requirementExecution.diagnostics.size() == 1);
    REQUIRE(executionAuthorityProblem.requirementExecution.diagnostics.front().requirementId ==
            "optional_legacy_station");

    const auto requireProblemUnchanged = [] (const rws::StructureOptimizationProblem& before,
                                             const rws::StructureOptimizationProblem& after) {
        REQUIRE(rws::StructureOptimizationJson::problemToJson(after) ==
                rws::StructureOptimizationJson::problemToJson(before));
        REQUIRE(after.tasks.size() == before.tasks.size());
        REQUIRE(after.context.taskPoints.size() == before.context.taskPoints.size());
        REQUIRE(after.evaluation.coverageBoxes.size() == before.evaluation.coverageBoxes.size());
        REQUIRE(after.requirementExecution.tasks.size() == before.requirementExecution.tasks.size());
        REQUIRE(after.requirementExecution.workspaceRegions.size() ==
                before.requirementExecution.workspaceRegions.size());
        REQUIRE(after.requirementExecution.diagnostics.size() ==
                before.requirementExecution.diagnostics.size());
        REQUIRE(after.requirementProvenance.requirementFingerprint ==
                before.requirementProvenance.requirementFingerprint);
        REQUIRE(after.requirementProvenance.executionFingerprint ==
                before.requirementProvenance.executionFingerprint);
        REQUIRE(after.requirementExecution.provenance.requirementFingerprint ==
                before.requirementExecution.provenance.requirementFingerprint);
        if (after.tasks.empty() || after.context.taskPoints.empty() ||
            after.evaluation.coverageBoxes.empty() || after.requirementExecution.tasks.empty() ||
            after.requirementExecution.workspaceRegions.empty()) return;
        REQUIRE(after.tasks.front().point.id == before.tasks.front().point.id);
        REQUIRE(after.context.taskPoints.front().id == before.context.taskPoints.front().id);
        REQUIRE(after.evaluation.coverageBoxes.front().id ==
                before.evaluation.coverageBoxes.front().id);
        REQUIRE(after.requirementExecution.tasks.front().id ==
                before.requirementExecution.tasks.front().id);
        REQUIRE(after.requirementExecution.workspaceRegions.front().id ==
                before.requirementExecution.workspaceRegions.front().id);
    };

    // A malformed v4 execution contract must not be mistaken for a valid empty
    // contract or atomically replace the problem with empty legacy projections.
    const rws::StructureOptimizationProblem problemBeforeMalformedExecution = problem;
    rws::FrozenRequirementArtifact fingerprintTamperedArtifact = artifact;
    fingerprintTamperedArtifact.executionFingerprint = "tampered-execution-fingerprint";
    rws::StructureOptimizationProblem fingerprintTarget = problemBeforeMalformedExecution;
    error = "stale adapter error";
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(
        fingerprintTamperedArtifact, fingerprintTarget, &error));
    REQUIRE(error.find("REQ_EXECUTION_FINGERPRINT_MISMATCH") != std::string::npos);
    REQUIRE(error.find("stale adapter error") == std::string::npos);
    requireProblemUnchanged(problemBeforeMalformedExecution, fingerprintTarget);

    rws::FrozenRequirementArtifact invalidEnumArtifact = artifact;
    invalidEnumArtifact.execution.tasks.front().level =
        static_cast<rws::RequirementExecutionLevel>(999);
    invalidEnumArtifact.executionFingerprint =
        rws::RequirementExecutionJson::fingerprint(invalidEnumArtifact.execution);
    rws::StructureOptimizationProblem invalidEnumTarget = problemBeforeMalformedExecution;
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(
        invalidEnumArtifact, invalidEnumTarget, &error));
    REQUIRE(error.find("REQ_EXECUTION_INVALID") != std::string::npos);
    requireProblemUnchanged(problemBeforeMalformedExecution, invalidEnumTarget);

    rws::FrozenRequirementArtifact provenanceTamperedArtifact = artifact;
    provenanceTamperedArtifact.execution.provenance.environmentFingerprint =
        "tampered-environment-fingerprint";
    provenanceTamperedArtifact.executionFingerprint =
        rws::RequirementExecutionJson::fingerprint(provenanceTamperedArtifact.execution);
    rws::StructureOptimizationProblem provenanceTarget = problemBeforeMalformedExecution;
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(
        provenanceTamperedArtifact, provenanceTarget, &error));
    requireProblemUnchanged(problemBeforeMalformedExecution, provenanceTarget);

    rws::StructureOptimizationProblem restoredProblem;
    const std::string problemJson = rws::StructureOptimizationJson::problemToJson(problem);
    REQUIRE(rws::StructureOptimizationJson::problemFromJson(
        problemJson, restoredProblem, &error));
    REQUIRE(restoredProblem.requirementExecution.provenance.requirementFingerprint ==
            artifact.execution.provenance.requirementFingerprint);
    REQUIRE(restoredProblem.requirementExecution.tasks[0].collisionFreeRequired);
    REQUIRE(!restoredProblem.requirementExecution.tasks[1].collisionFreeRequired);
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.size() == 1);
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().id == "work_area");
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().orientationMode ==
            rws::RequirementExecutionOrientationMode::PointAtTarget);
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().orientationTargetPoint ==
            "0.8,0.0,0.4");
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().directionSamples == 2);
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().rollSamples == 3);
    REQUIRE(std::abs(restoredProblem.requirementExecution.workspaceRegions.front().minimumOrientationCoverage - 0.5) < 1e-12);
    REQUIRE(!restoredProblem.requirementExecution.workspaceRegions.front().collisionFreeRequired);

    // v3 Quick-only 工件不能满足 Verified 请求，必须给出可机读的重冻结诊断。
    rws::FrozenRequirementArtifact legacyArtifact = artifact;
    legacyArtifact.schemaVersion = 3;
    legacyArtifact.execution.workspaceRegions.front().minimumVerificationStage =
        rws::RequirementExecutionStage::Quick;
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(legacyArtifact, problem, &error));
    REQUIRE(error.find("REQ_V3_REQUIRES_REFREEZE") != std::string::npos);

    rws::FrozenRequirementArtifact mismatchedArtifact = artifact;
    mismatchedArtifact.modelBinding.robotModelFingerprint = "wrong-model-fingerprint";
    mismatchedArtifact.compiled.modelBinding.robotModelFingerprint =
        mismatchedArtifact.modelBinding.robotModelFingerprint;
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(mismatchedArtifact, problem, &error));
    REQUIRE(error.find("RobotModelSpec") != std::string::npos);

    // 追加第二个 Must 覆盖盒：compiled 与 execution 都必须同步加入，并重算执行指纹，
    // 保持两契约一致，否则适配器的一致性校验会失败。
    rws::WorkspaceDemandRegion secondRegion = region;
    secondRegion.id = "second_area";
    artifact.compiled.workspaceRegions.push_back(secondRegion);
    rws::RequirementExecutionRegion secondExecutionRegion = artifact.execution.workspaceRegions.front();
    secondExecutionRegion.id = secondRegion.id;
    artifact.execution.workspaceRegions.push_back(secondExecutionRegion);
    artifact.executionFingerprint = rws::RequirementExecutionJson::fingerprint(artifact.execution);
    // 多个必须覆盖区域必须分别转换为独立的评价区域和硬约束，不能再以“只支持一个”
    // 的方式拒绝工程需求，更不能把两者静默并成一个更大的 WORLD 覆盖盒。
    REQUIRE(rws::EngineeringRequirementArtifactAdapter::apply(artifact, problem, &error));
    REQUIRE(problem.evaluation.coverageBoxes.size() == 2);
    REQUIRE(problem.evaluation.coverageBoxes[0].id == "work_area");
    REQUIRE(problem.evaluation.coverageBoxes[1].id == "second_area");
    REQUIRE(problem.constraints.size() >= 2);

    // A supported artifact may contain an advisory pose-policy workspace.
    // Structure optimization currently evaluates position coverage only, so a
    // Should region must not become a hard blocker solely for this limitation.
    rws::WorkspaceDemandRegion advisoryPoseRegion = region;
    advisoryPoseRegion.id = "advisory_pose_area";
    advisoryPoseRegion.level = rws::RequirementLevel::Should;
    advisoryPoseRegion.directionSamples = 2;
    artifact.compiled.workspaceRegions.push_back(advisoryPoseRegion);
    rws::RequirementExecutionRegion advisoryExecutionRegion =
        artifact.execution.workspaceRegions.front();
    advisoryExecutionRegion.id = advisoryPoseRegion.id;
    advisoryExecutionRegion.level = rws::RequirementExecutionLevel::Should;
    advisoryExecutionRegion.directionSamples = advisoryPoseRegion.directionSamples;
    artifact.execution.workspaceRegions.push_back(advisoryExecutionRegion);
    artifact.executionFingerprint = rws::RequirementExecutionJson::fingerprint(artifact.execution);
    REQUIRE(rws::EngineeringRequirementArtifactAdapter::apply(artifact, problem, &error));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 项目适配器恢复托管场景根:把含托管冻结场景(带 Fixture 与几何)的项目
// 整体复制到新目录、删除原资产后,loadProject 必须解析出显式传入的 cloneRoot
// 作为场景根,使 CandidateModelFactory 仍能构建出带 ManagedFixture 的 WorkCell。
static void testProjectAdapterRestoresManagedScenarioRoot()
{
    std::printf("testProjectAdapterRestoresManagedScenarioRoot ... ");

    QTemporaryDir workspace;
    REQUIRE(workspace.isValid());
    const QString sourceRoot = workspace.filePath("source-project");
    const QString cloneRoot = workspace.filePath("clone-project");
    REQUIRE(QDir().mkpath(QDir(sourceRoot).filePath("optimizations")));
    REQUIRE(QDir().mkpath(QDir(sourceRoot).filePath("assets")));
    REQUIRE(QDir().mkpath(QDir(cloneRoot).filePath("optimizations")));
    REQUIRE(QDir().mkpath(QDir(cloneRoot).filePath("assets")));

    const QString sourceMesh = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/geometry/base.stl");
    const QString sourceProjectMesh = QDir(sourceRoot).filePath("assets/base.stl");
    const QString cloneProjectMesh = QDir(cloneRoot).filePath("assets/base.stl");
    REQUIRE(QFile::copy(sourceMesh, sourceProjectMesh));
    REQUIRE(QFile::copy(sourceMesh, cloneProjectMesh));

    rws::StructureOptimizationProblem original;
    original.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(sourceRoot);
    original.context.robotName = original.context.modelSpec.robotName;
    original.context.deviceName = original.context.modelSpec.robotName;
    original.scenarioSnapshot.schemaVersion = 1;
    original.scenarioSnapshot.snapshotFingerprint = "managed-scenario";
    rws::FrameSpec fixture;
    fixture.name = "ManagedFixture";
    fixture.refFrame = "WORLD";
    original.scenarioSnapshot.sceneSpec.sceneFrames.push_back(fixture);
    rws::SceneGeometrySpec geometry;
    geometry.name = "ManagedFixtureMesh";
    geometry.refFrame = fixture.name;
    geometry.kind = rws::GeometryKind::Polytope;
    geometry.file = "assets/base.stl";
    original.scenarioSnapshot.sceneSpec.sceneGeometries.push_back(geometry);

    const QString sourceDocument =
        QDir(sourceRoot).filePath("optimizations/main.structure-optimization.json");
    const QString cloneDocument =
        QDir(cloneRoot).filePath("optimizations/main.structure-optimization.json");
    QString error;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        sourceDocument, original, -1, &error));
    REQUIRE(QFile::copy(sourceDocument, cloneDocument));
    REQUIRE(QFile::remove(sourceProjectMesh));

    const QString previousCwd = QDir::currentPath();
    REQUIRE(QDir::setCurrent(workspace.filePath("unrelated-cwd")) ||
            (QDir().mkpath(workspace.filePath("unrelated-cwd")) &&
             QDir::setCurrent(workspace.filePath("unrelated-cwd"))));
    rws::StructureOptimizationProblem loaded;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        cloneDocument, loaded, nullptr, &error, cloneRoot));
    REQUIRE(loaded.scenarioSnapshot.baseDirectory == cloneRoot.toStdString());

    rws::CandidateModelBuildRequest request;
    request.spec = loaded.context.modelSpec;
    request.deviceName = loaded.context.deviceName;
    request.checkCollision = false;
    request.scenarioSnapshot = &loaded.scenarioSnapshot;
    request.scenarioBaseDirectory = loaded.scenarioSnapshot.baseDirectory;
    const rws::CandidateModelBuildResult result = rws::CandidateModelFactory().build(request);
    REQUIRE(result.ok);
    REQUIRE(!result.artifact.workcell.isNull());
    if (!result.artifact.workcell.isNull())
        REQUIRE(result.artifact.workcell->findFrame("ManagedFixture") != nullptr);
    REQUIRE(QDir::setCurrent(previousCwd));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 冻结需求项目导入:验证导入服务从真实文件边界读取冻结工件——需求文件中
// 必须有 frozenArtifact;模型用相对路径(随目录搬迁);机器人 jog 只报告
// robotStateChanged 而不拒绝,工装移动则明确拒绝(场景指纹变化);缺少
// frozenArtifact 的文件绝不能成为优化输入。
static void testFrozenRequirementProjectImportCreatesAuditableProblem()
{
    std::printf("testFrozenRequirementProjectImportCreatesAuditableProblem ... ");

    // 导入服务必须从真实文件边界读取冻结工件：需求 JSON 中只有编辑态 RequirementSet 或者
    // 工件缺失时都不能绕过冻结门禁。模型使用相对路径，覆盖工程项目随目录整体移动的场景。
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString modelPath = directory.filePath("robot.rmb.json");
    const QString requirementPath = directory.filePath("cell.requirements.json");
    const QString workcellPath = directory.filePath("source.wc.xml");
    QFile workcellFile(workcellPath);
    REQUIRE(workcellFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(workcellFile.write("<WorkCell name=\"ImportWorkCell\" />\n") > 0);
    workcellFile.close();
    const rws::RobotModelSpec model =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(directory.path());

    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(modelFile.write(QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(model))) > 0);
    modelFile.close();

    rws::RequirementSet requirements;
    requirements.name = "Frozen cell requirement";
    requirements.modelBinding.sourcePath = "robot.rmb.json";
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);
    requirements.frozen = true;

    rws::PoseTask requestedStation;
    requestedStation.id = "load";
    requestedStation.name = "Machine load";
    requestedStation.level = rws::RequirementLevel::Must;
    requestedStation.refFrame = "WORLD";
    requestedStation.tcpFrame = "ImportTcp";
    requestedStation.position = {{0.35, 0.0, 0.45}};
    requirements.poseTasks.push_back(requestedStation);

    const rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ImportBase", rw::math::Transform3D<>()));
    const rw::models::RevoluteJoint::Ptr joint = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("ImportJoint", rw::math::Transform3D<>()));
    const rw::kinematics::FixedFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ImportTcp", rw::math::Transform3D<>()));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(joint, base);
    structure->addFrame(tcp, joint);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "ImportWorkCell", workcellPath.toStdString()));
    const rw::models::SerialDevice::Ptr device = rw::core::ownedPtr(
        new rw::models::SerialDevice(base.get(), tcp.get(), model.robotName,
                                     structure->getDefaultState()));
    workcell->addDevice(device);
    const rw::kinematics::MovableFrame::Ptr fixture = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("ImportFixture"));
    workcell->addFrame(fixture, workcell->getWorldFrame());
    const rw::kinematics::State frozenState = workcell->getDefaultState();

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(rws::RequirementFreezer::freeze(requirements, *workcell, frozenState,
                                             model, artifact, &error,
                                             directory.path().toStdString()));

    QJsonObject requirementProject = rws::RequirementSetJson::toObject(requirements);
    requirementProject["frozenArtifact"] = rws::FrozenRequirementArtifactJson::toObject(artifact);
    QFile requirementFile(requirementPath);
    REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(requirementFile.write(QJsonDocument(requirementProject).toJson()) > 0);
    requirementFile.close();

    rws::StructureOptimizationProblem imported;
    rws::FrozenRequirementValidationResult validation;
    REQUIRE(rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, frozenState, imported, &validation, &error));
    REQUIRE(imported.tasks.size() == 1);
    REQUIRE(imported.tasks.front().point.id == "load");
    REQUIRE(imported.requirementProvenance.requirementFingerprint ==
            artifact.requirementFingerprint);
    REQUIRE(imported.requirementProvenance.frozenAt == artifact.frozenAt);
    REQUIRE(imported.context.sourceModelPath == modelPath.toStdString());
    REQUIRE(validation.warnings.empty());
    REQUIRE(imported.scenarioSnapshot.baseDirectory == directory.path().toStdString());

    rw::kinematics::State joggedState = frozenState;
    device->setQ(rw::math::Q(1, 0.25), joggedState);
    REQUIRE(rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, joggedState, imported, &validation, &error));
    REQUIRE(validation.robotStateChanged);

    rw::kinematics::State fixtureMovedState = frozenState;
    fixture->setTransform(
        rw::math::Transform3D<>(rw::math::Vector3D<>(0.1, 0.0, 0.0)), fixtureMovedState);
    REQUIRE(!rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, fixtureMovedState, imported, &validation, &error));
    REQUIRE(error.find("Fixture or external environment") != std::string::npos);

    // 没有冻结工件的需求文件只能继续编辑，绝不能被误用为下游优化输入。
    requirementProject.remove("frozenArtifact");
    REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    REQUIRE(requirementFile.write(QJsonDocument(requirementProject).toJson()) > 0);
    requirementFile.close();
    REQUIRE(!rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, frozenState, imported, &validation, &error));
    REQUIRE(error.find("frozenArtifact") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 托管冻结需求导入使用显式项目根:构造嵌套目录(项目根/data/frozen)下的
// 冻结需求与从 UR 设备转换的托管模型,验证不传 projectRoot 的启发式导入构造出的
// 场景无法构建候选;传入显式 projectRoot 后场景根正确、模型可移植,项目整体移动
// 后模型溯源仍为 Current。
static void testManagedFrozenRequirementImportUsesExplicitProjectRoot()
{
    std::printf("testManagedFrozenRequirementImportUsesExplicitProjectRoot ... ");

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectRoot = directory.filePath("ManagedProject");
    const QString nestedRoot = QDir(projectRoot).filePath("data/frozen");
    const QString deviceRoot = QDir(projectRoot).filePath("devices");
    const QString assetRoot = QDir(projectRoot).filePath("assets");
    REQUIRE(QDir().mkpath(nestedRoot));
    REQUIRE(QDir().mkpath(QDir(deviceRoot).filePath("geometry")));
    REQUIRE(QDir().mkpath(assetRoot));

    const QString urSourceRoot = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A");
    REQUIRE(QFile::copy(QDir(urSourceRoot).filePath("UR.wc.xml"),
                        QDir(deviceRoot).filePath("UR.wc.xml")));
    const QDir sourceGeometry(QDir(urSourceRoot).filePath("geometry"));
    for (const QString& fileName : sourceGeometry.entryList(QDir::Files)) {
        REQUIRE(QFile::copy(sourceGeometry.filePath(fileName),
                            QDir(deviceRoot).filePath("geometry/" + fileName)));
    }
    REQUIRE(QFile::copy(sourceGeometry.filePath("base.stl"),
                        QDir(assetRoot).filePath("fixture.stl")));

    const QString workcellPath = QDir(projectRoot).filePath("scenes/main.wc.xml");
    REQUIRE(QDir().mkpath(QFileInfo(workcellPath).absolutePath()));
    QFile workcellFile(workcellPath);
    REQUIRE(workcellFile.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray workcellXml =
        "<WorkCell name=\"ManagedImportScene\">\n"
        "  <Frame name=\"Fixture\" refframe=\"WORLD\" />\n"
        "  <Drawable name=\"FixtureMesh\" refframe=\"Fixture\">\n"
        "    <Polytope file=\"../assets/fixture.stl\" />\n"
        "  </Drawable>\n"
        "  <Include file=\"../devices/UR.wc.xml\" />\n"
        "</WorkCell>\n";
    REQUIRE(workcellFile.write(workcellXml) == workcellXml.size());
    workcellFile.close();

    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load(workcellPath.toStdString());
    REQUIRE(!workcell.isNull());
    if (workcell.isNull()) {
        std::printf("FAILED (%d)\n", g_testFailures);
        return;
    }
    QStringList conversionWarnings;
    rws::RobotModelSpec model = rws::WorkCellConverter::convert(
        *workcell, workcell->getDefaultState(), nestedRoot.toStdString(), conversionWarnings);
    REQUIRE(rws::WorkCellConverter::hasConvertibleRobotModel(model));
    REQUIRE(!workcell->getDevices().empty());
    if (!workcell->getDevices().empty()) {
        model.robotName = workcell->getDevices().front()->getName();
        model.includes.clear();
        model.imported = rws::ImportedDocumentSpec();
    }

    const QString modelPath = QDir(nestedRoot).filePath("robot.rmb.json");
    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray modelJson = QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(model));
    REQUIRE(modelFile.write(modelJson) == modelJson.size());
    modelFile.close();

    rws::RequirementSet requirements;
    requirements.name = "Managed nested requirement";
    requirements.frozen = true;
    requirements.modelBinding.sourcePath = "robot.rmb.json";
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);
    rws::PoseTask station;
    station.id = "managed-load";
    station.name = "Managed load";
    station.level = rws::RequirementLevel::Must;
    station.refFrame = "WORLD";
    station.tcpFrame = workcell->getDevices().front()->getEnd()->getName();
    requirements.poseTasks.push_back(station);

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    const bool frozen = rws::RequirementFreezer::freeze(
        requirements, *workcell, workcell->getDefaultState(), model, artifact, &error,
        projectRoot.toStdString());
    if (!frozen)
        std::fprintf(stderr, "Managed import freeze error: %s\n", error.c_str());
    REQUIRE(frozen);
    const QString requirementPath = QDir(nestedRoot).filePath("cell.requirements.json");
    QJsonObject requirementProject = rws::RequirementSetJson::toObject(requirements);
    requirementProject["frozenArtifact"] = rws::FrozenRequirementArtifactJson::toObject(artifact);
    QFile requirementFile(requirementPath);
    REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(requirementFile.write(QJsonDocument(requirementProject).toJson()) > 0);
    requirementFile.close();

    rws::StructureOptimizationProblem heuristicProblem;
    rws::FrozenRequirementValidationResult validation;
    const bool heuristicImported = rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, workcell->getDefaultState(), heuristicProblem,
        &validation, &error);
    if (!heuristicImported)
        std::fprintf(stderr, "Heuristic import error: %s\n", error.c_str());
    REQUIRE(heuristicImported);
    rws::CandidateModelBuildRequest heuristicRequest;
    heuristicRequest.spec = heuristicProblem.context.modelSpec;
    heuristicRequest.deviceName = heuristicProblem.context.deviceName;
    heuristicRequest.checkCollision = false;
    heuristicRequest.scenarioSnapshot = &heuristicProblem.scenarioSnapshot;
    heuristicRequest.scenarioBaseDirectory = heuristicProblem.scenarioSnapshot.baseDirectory;
    REQUIRE(!rws::CandidateModelFactory().build(heuristicRequest).ok);

    rws::StructureOptimizationProblem managedProblem;
    const bool managedImported = rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, workcell->getDefaultState(), managedProblem,
        &validation, &error, projectRoot);
    if (!managedImported)
        std::fprintf(stderr, "Managed import error: %s\n", error.c_str());
    REQUIRE(managedImported);
    REQUIRE(QDir::cleanPath(QString::fromStdString(managedProblem.scenarioSnapshot.baseDirectory)) ==
            QDir::cleanPath(projectRoot));
    rws::CandidateModelBuildRequest managedRequest;
    managedRequest.spec = managedProblem.context.modelSpec;
    managedRequest.deviceName = managedProblem.context.deviceName;
    managedRequest.checkCollision = false;
    managedRequest.scenarioSnapshot = &managedProblem.scenarioSnapshot;
    managedRequest.scenarioBaseDirectory = managedProblem.scenarioSnapshot.baseDirectory;
    const rws::CandidateModelBuildResult managedBuild =
        rws::CandidateModelFactory().build(managedRequest);
    if (!managedBuild.ok) {
        for (const rws::AnalysisWarning& warning : managedBuild.warnings)
            std::fprintf(stderr, "Managed candidate build error: %s\n", warning.message.c_str());
    }
    REQUIRE(managedBuild.ok);

    rws::RobotModelSpec portableModel;
    QString portableError;
    REQUIRE(rws::RobotModelProjectPaths::makePortable(
        model, projectRoot, portableModel, &portableError));
    portableModel.saveDirectory.clear();
    rws::RobotModelSpec portableRuntime;
    REQUIRE(rws::RobotModelProjectPaths::resolveManaged(
        portableModel, projectRoot, portableRuntime, &portableError));
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray portableJson =
        QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(portableModel));
    REQUIRE(modelFile.write(portableJson) == portableJson.size());
    modelFile.close();

    rws::RequirementSet portableRequirements = requirements;
    portableRequirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(portableRuntime);
    rws::FrozenRequirementArtifact portableArtifact;
    REQUIRE(rws::RequirementFreezer::freeze(
        portableRequirements, *workcell, workcell->getDefaultState(), portableRuntime,
        portableArtifact, &error, projectRoot.toStdString()));
    QJsonObject portableRequirementProject =
        rws::RequirementSetJson::toObject(portableRequirements);
    portableRequirementProject["frozenArtifact"] =
        rws::FrozenRequirementArtifactJson::toObject(portableArtifact);
    REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(requirementFile.write(QJsonDocument(portableRequirementProject).toJson()) > 0);
    requirementFile.close();

    rws::StructureOptimizationProblem portableProblem;
    const bool portableImported = rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, workcell->getDefaultState(), portableProblem,
        &validation, &error, projectRoot);
    if (!portableImported)
        std::fprintf(stderr, "Portable managed import error: %s\n", error.c_str());
    REQUIRE(portableImported);
    REQUIRE(rws::RobotModelFingerprint::canonicalSha256(portableProblem.context.modelSpec) ==
            portableRequirements.modelBinding.robotModelFingerprint);

    const QString optimizationPath =
        QDir(projectRoot).filePath("optimizations/main.structure-optimization.json");
    const rws::RobotModelStalenessResult currentStatus =
        rws::RobotModelStalenessChecker::checkManaged(
            portableProblem.context, optimizationPath, projectRoot);
    REQUIRE(currentStatus.status == rws::RobotModelSourceStatus::Current);
    REQUIRE(QDir::cleanPath(currentStatus.resolvedSourcePath) == QDir::cleanPath(modelPath));

    REQUIRE(QDir().mkpath(QFileInfo(optimizationPath).absolutePath()));
    rws::StructureOptimizationProblem persistedProblem = portableProblem;
    const QString relativeModelPath = QDir(QFileInfo(optimizationPath).absolutePath())
                                          .relativeFilePath(modelPath);
    persistedProblem.context.sourceModelPath = relativeModelPath.toStdString();
    persistedProblem.context.modelProvenance.sourceModelPath =
        relativeModelPath.toStdString();
    persistedProblem.context.modelSpec.saveDirectory =
        QStringLiteral("../generated/robot-models").toStdString();
    QString projectError;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        optimizationPath, persistedProblem, -1, &projectError));

    const QString movedRoot = directory.filePath("ManagedProjectMoved");
    REQUIRE(QDir(directory.path()).rename("ManagedProject", "ManagedProjectMoved"));
    QTemporaryDir hostileDirectory;
    REQUIRE(hostileDirectory.isValid());
    const QString previousCwd = QDir::currentPath();
    REQUIRE(QDir::setCurrent(hostileDirectory.path()));

    const QString movedModelPath = QDir(movedRoot).filePath("data/frozen/robot.rmb.json");
    const QString movedOptimizationPath =
        QDir(movedRoot).filePath("optimizations/main.structure-optimization.json");
    rws::StructureOptimizationProblem movedProblem;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        movedOptimizationPath, movedProblem, nullptr, &projectError, movedRoot));
    const rws::RobotModelStalenessResult movedStatus =
        rws::RobotModelStalenessChecker::checkManaged(
            movedProblem.context, movedOptimizationPath, movedRoot);
    REQUIRE(movedStatus.status == rws::RobotModelSourceStatus::Current);
    REQUIRE(QDir::cleanPath(movedStatus.resolvedSourcePath) ==
            QDir::cleanPath(movedModelPath));
    REQUIRE(QDir::setCurrent(previousCwd));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 项目工厂:验证从 RobotModelSpec 创建结构优化问题(设备名/变量/空任务),
// 保存/加载往返后 JSON 完全一致;空模型规格必须被拒绝并给出稳定的
// StructureOptimization.Context.Invalid 错误。
static void testProjectFactory()
{
    std::printf("testProjectFactory ... ");

    rws::RobotModelSpec spec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    spec.robotName = "FactoryRobot";

    rws::StructureOptimizationProblem problem;
    std::string error;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(spec, problem, &error));
    REQUIRE(problem.context.modelSpec.robotName == "FactoryRobot");
    REQUIRE(problem.context.robotName == "FactoryRobot");
    REQUIRE(problem.context.deviceName == "FactoryRobot");
    REQUIRE(!problem.variables.empty());
    REQUIRE(problem.tasks.empty());

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectPath = directory.filePath("factory.structure-optimization.json");
    QString projectError;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        projectPath, problem, -1, &projectError));

    rws::StructureOptimizationProblem loaded;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        projectPath, loaded, nullptr, &projectError));
    REQUIRE(loaded.context.modelSpec.robotName == problem.context.modelSpec.robotName);
    REQUIRE(loaded.context.modelSpec.transformJoints.size() ==
            problem.context.modelSpec.transformJoints.size());
    REQUIRE(loaded.variables.size() == problem.variables.size());
    REQUIRE(rws::StructureOptimizationJson::problemToJson(loaded) ==
            rws::StructureOptimizationJson::problemToJson(problem));

    rws::StructureOptimizationProblem invalidProblem;
    error.clear();
    REQUIRE(!rws::StructureOptimizationProjectFactory::create(
        rws::RobotModelSpec(), invalidProblem, &error));
    REQUIRE(error.find("StructureOptimization.Context.Invalid") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 项目工厂溯源:验证 factory 记录的模型溯源与 RobotModelStalenessChecker
// 状态机联动——源文件未变→Current,修改→Stale,删除→SourceMissing,写坏→
// SourceInvalid;并覆盖相对几何后缀碰撞与平台根目录边界下的托管解析。
static void testProjectFactoryProvenance()
{
    std::printf("testProjectFactoryProvenance ... ");

    QTemporaryDir directory;
    REQUIRE(directory.isValid());

    rws::RobotModelSpec spec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(directory.path());
    spec.robotName = "ProvenanceRobot";
    QStringList saveErrors;
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(spec, saveErrors));
    const QString sourceFilePath = rws::RobotModelXmlWriter::specSidecarFilePath(spec);
    const QString relativeSourcePath = QFileInfo(sourceFilePath).fileName();
    const QString projectPath = directory.filePath("provenance.structure-optimization.json");

    rws::StructureOptimizationProblem problem;
    std::string error;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        spec, relativeSourcePath, problem, &error));
    REQUIRE(problem.context.modelProvenance.sourceModelPath ==
            relativeSourcePath.toStdString());
    REQUIRE(!problem.context.modelProvenance.sourceFingerprint.empty());
    REQUIRE(problem.context.modelProvenance.sourceFingerprint ==
            problem.context.modelProvenance.snapshotFingerprint);
    REQUIRE(rws::RobotModelStalenessChecker::check(problem.context, projectPath).status ==
            rws::RobotModelSourceStatus::Current);

    rws::RobotModelSpec changedSpec = spec;
    changedSpec.transformJoints.at(1).pos[0] += 0.001;
    saveErrors.clear();
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(changedSpec, saveErrors));
    REQUIRE(rws::RobotModelStalenessChecker::check(problem.context, projectPath).status ==
            rws::RobotModelSourceStatus::Stale);

    REQUIRE(QFile::remove(sourceFilePath));
    REQUIRE(rws::RobotModelStalenessChecker::check(problem.context, projectPath).status ==
            rws::RobotModelSourceStatus::SourceMissing);

    QFile invalidSource(sourceFilePath);
    REQUIRE(invalidSource.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(invalidSource.write("{not-json") > 0);
    invalidSource.close();
    REQUIRE(rws::RobotModelStalenessChecker::check(problem.context, projectPath).status ==
            rws::RobotModelSourceStatus::SourceInvalid);

    QTemporaryDir suffixCollisionDirectory;
    REQUIRE(suffixCollisionDirectory.isValid());
    rws::RobotModelSpec suffixCollisionSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(
            suffixCollisionDirectory.path());
    suffixCollisionSpec.robotName = "StandaloneSuffixCollision";
    suffixCollisionSpec.saveDirectory =
        suffixCollisionDirectory.filePath("generated/robot-models").toStdString();
    suffixCollisionSpec.drawables.clear();
    suffixCollisionSpec.collisionModels.clear();
    suffixCollisionSpec.sceneGeometries.clear();
    rws::DrawableSpec relativeDrawable;
    relativeDrawable.name = "standalone-relative-geometry";
    relativeDrawable.refFrame = "WORLD";
    relativeDrawable.shape = "Mesh";
    relativeDrawable.filePath = "assets/base.stl";
    suffixCollisionSpec.drawables.push_back(relativeDrawable);
    const QString suffixCollisionSource =
        suffixCollisionDirectory.filePath("standalone.rmb.json");
    QFile suffixCollisionFile(suffixCollisionSource);
    REQUIRE(suffixCollisionFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray suffixCollisionJson = QByteArray::fromStdString(
        rws::RobotModelSpecJson::toJson(suffixCollisionSpec));
    REQUIRE(suffixCollisionFile.write(suffixCollisionJson) == suffixCollisionJson.size());
    suffixCollisionFile.close();
    rws::StructureOptimizationProblem suffixCollisionProblem;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        suffixCollisionSpec, suffixCollisionSource, suffixCollisionProblem, &error));
    REQUIRE(rws::RobotModelStalenessChecker::check(
                suffixCollisionProblem.context,
                suffixCollisionDirectory.filePath(
                    "standalone.structure-optimization.json")).status ==
            rws::RobotModelSourceStatus::Current);

    QTemporaryDir rootBoundaryDirectory;
    REQUIRE(rootBoundaryDirectory.isValid());
    const QString rootBoundaryAsset =
        rootBoundaryDirectory.filePath("assets/root-boundary.stl");
    REQUIRE(QDir().mkpath(QFileInfo(rootBoundaryAsset).absolutePath()));
    QFile rootBoundaryAssetFile(rootBoundaryAsset);
    REQUIRE(rootBoundaryAssetFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(rootBoundaryAssetFile.write("solid root-boundary\nendsolid root-boundary\n") > 0);
    rootBoundaryAssetFile.close();
    const QString normalizedBoundaryAsset = QDir::fromNativeSeparators(
        QFileInfo(rootBoundaryAsset).absoluteFilePath());
#ifdef Q_OS_WIN
    const QString rootBoundary = normalizedBoundaryAsset.left(3);
#else
    const QString rootBoundary = QStringLiteral("/");
#endif
    REQUIRE(QFileInfo(rootBoundary).isAbsolute());
    const QString rootRelativeAsset =
        QDir(rootBoundary).relativeFilePath(normalizedBoundaryAsset);
    REQUIRE(QFileInfo(rootRelativeAsset).isRelative());
    rws::RobotModelSpec rootPortable =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(rootBoundaryDirectory.path());
    rootPortable.robotName = "ManagedRootBoundary";
    rootPortable.saveDirectory.clear();
    rootPortable.drawables.clear();
    rootPortable.collisionModels.clear();
    rootPortable.sceneGeometries.clear();
    rws::DrawableSpec rootDrawable;
    rootDrawable.name = "managed-root-boundary";
    rootDrawable.refFrame = "WORLD";
    rootDrawable.shape = "Mesh";
    rootDrawable.filePath = rootRelativeAsset.toStdString();
    rootPortable.drawables.push_back(rootDrawable);
    const QString rootSource = rootBoundaryDirectory.filePath("root-boundary.rmb.json");
    QFile rootSourceFile(rootSource);
    REQUIRE(rootSourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray rootSourceJson =
        QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(rootPortable));
    REQUIRE(rootSourceFile.write(rootSourceJson) == rootSourceJson.size());
    rootSourceFile.close();
    rws::RobotModelSpec rootRuntime;
    QString rootPathError;
    REQUIRE(rws::RobotModelProjectPaths::resolveManaged(
        rootPortable, rootBoundary, rootRuntime, &rootPathError));
    rws::StructureOptimizationProblem rootProblem;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        rootRuntime, rootSource, rootProblem, &error));
    REQUIRE(rws::RobotModelStalenessChecker::checkManaged(
                rootProblem.context,
                rootBoundaryDirectory.filePath("root-boundary.structure-optimization.json"),
                rootBoundary).status == rws::RobotModelSourceStatus::Current);

    rws::RobotDesignContext legacyContext;
    REQUIRE(rws::RobotModelStalenessChecker::check(legacyContext, projectPath).status ==
            rws::RobotModelSourceStatus::ModelSpecIncomplete);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 导出服务:验证 exportAll 一次写出报告/审计 CSV/项目 JSON 等 6 个文件,
// 目标已存在时重复导出失败;候选模型导出必须把冻结工装 Fixture_A 写入同一份场景
// XML,保证预览/评价与交付给工程师的模型对工装 Frame 的解释一致。
static void testExportService()
{
    std::printf("testExportService ... ");

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;

    rws::StructureOptimizationResult result;
    rws::StructureCandidateResult candidate;
    candidate.index = 5;
    candidate.feasible = true;
    candidate.status = rws::StructureCandidateStatus::Feasible;
    candidate.totalScore = 91.5;
    candidate.raw.weightedReachability = 1.0;
    result.baselineCandidateIndex = 5;
    result.bestCandidateIndex = 5;
    result.candidates.push_back(candidate);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    rws::StructureOptimizationExportRequest request;
    request.directory = dir.filePath("export");
    request.selectedCandidateIndex = 5;
    request.exportCandidateModel = false;
    const rws::StructureOptimizationExportResult exported =
        rws::StructureOptimizationExportService::exportAll(problem, result, request);
    REQUIRE(exported.ok);
    REQUIRE(exported.writtenFiles.size() == 6);
    REQUIRE(QFileInfo::exists(dir.filePath("export/report.md")));
    REQUIRE(QFileInfo::exists(dir.filePath("export/audit.csv")));
    REQUIRE(QFileInfo::exists(dir.filePath("export/project.structure-optimization.json")));

    const rws::StructureOptimizationExportResult conflict =
        rws::StructureOptimizationExportService::exportAll(problem, result, request);
    REQUIRE(!conflict.ok);
    REQUIRE(!conflict.errors.isEmpty());

    // 候选评价已将冻结工装合入临时 WorkCell，因此最终导出的模型也必须写入同一份
    // 场景。否则预览/评价与交付给研发工程师的 XML 对工装 Frame 的解释会不一致。
    rws::StructureOptimizationScenarioSnapshot scenario;
    scenario.schemaVersion = 1;
    scenario.snapshotFingerprint = "fixture-export-snapshot";
    rws::FrameSpec fixture;
    fixture.name = "Fixture_A";
    fixture.refFrame = "WORLD";
    fixture.pos = {{0.4, 0.0, 0.2}};
    scenario.sceneSpec.sceneFrames.push_back(fixture);
    problem.scenarioSnapshot = scenario;

    const QString modelDirectory = dir.filePath("candidate-with-fixture");
    QStringList modelErrors;
    REQUIRE(rws::StructureCandidateExporter::exportModel(
        problem, candidate, modelDirectory, modelErrors));
    REQUIRE(modelErrors.isEmpty());

    QDir exportedModel(modelDirectory);
    const QStringList sceneFiles = exportedModel.entryList(
        QStringList() << "*Scene.wc.xml", QDir::Files);
    REQUIRE(sceneFiles.size() == 1);
    if (sceneFiles.size() == 1) {
        QFile sceneFile(exportedModel.filePath(sceneFiles.front()));
        REQUIRE(sceneFile.open(QIODevice::ReadOnly));
        if (sceneFile.isOpen()) {
            const QByteArray sceneXml = sceneFile.readAll();
            REQUIRE(sceneXml.contains("Fixture_A"));
        }
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 端到端验收(UR 六轴):从 UR.wc.xml 创建托管项目、转换模型、构造含 3 个
// 任务与 3 个设计变量的问题并保存;重新加载后校验模型溯源为 Current,并实际跑
// 两次系统级优化(验证确定性)与最佳候选模型导出,检查候选 WC 落盘。
static void testAcceptedUr6585AProject()
{
    std::printf("testAcceptedUr6585AProject ... ");

    QTemporaryDir inputDirectory;
    REQUIRE(inputDirectory.isValid());
    const QString sourceWorkCell = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml");
    const QString manifestPath =
        inputDirectory.filePath("UrProject/UrProject.rwproj");
    const QString projectDirectory = QFileInfo(manifestPath).absolutePath();

    QString projectError;
    rws::ProjectManager manager;
    REQUIRE(manager.createProjectFromWorkCell(
        manifestPath, sourceWorkCell, &projectError));
    QString managedWorkCellPath;
    REQUIRE(manager.resolveResource(
        "scene.main", managedWorkCellPath, &projectError));
    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load(
            managedWorkCellPath.toStdString());
    REQUIRE(!workcell.isNull());

    const QString modelDirectory =
        QDir(projectDirectory).filePath("generated/robot-models");
    REQUIRE(QDir().mkpath(modelDirectory));
    QStringList conversionWarnings;
    rws::RobotModelSpec modelSpec;
    if (!workcell.isNull()) {
        modelSpec = rws::WorkCellConverter::convert(
            *workcell, workcell->getDefaultState(),
            modelDirectory.toStdString(), conversionWarnings);
    }
    REQUIRE(rws::WorkCellConverter::hasConvertibleRobotModel(modelSpec));

    const QString modelPath =
        QDir(modelDirectory).filePath("UR-6-85-5-A.rmb.json");
    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray modelJson = QByteArray::fromStdString(
        rws::RobotModelSpecJson::toJson(modelSpec));
    if (modelFile.isOpen()) {
        REQUIRE(modelFile.write(modelJson) == modelJson.size());
        modelFile.close();
    }

    rws::StructureOptimizationProblem generatedProject;
    std::string error;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        modelSpec, modelPath, generatedProject, &error));
    generatedProject.context.sourceScenePath = managedWorkCellPath.toStdString();
    generatedProject.context.tcpFrame.clear();

    const auto addTask = [&generatedProject](
                             const char* id, const char* name,
                             const std::array<double, 3>& position) {
        rws::OptimizationTaskPoint task;
        task.point.id = id;
        task.point.name = name;
        task.point.refFrame = "WORLD";
        task.point.tcpFrame.clear();
        task.point.position = position;
        task.point.rpyDeg = {{180.0, 0.0, 0.0}};
        task.point.enabled = true;
        task.required = true;
        generatedProject.tasks.push_back(task);
        generatedProject.context.taskPoints.push_back(task.point);
    };
    addTask("acceptance-task-1", "UR acceptance task 1", {{0.3, -0.2, 0.4}});
    addTask("acceptance-task-2", "UR acceptance task 2", {{0.35, 0.0, 0.45}});
    addTask("acceptance-task-3", "UR acceptance task 3", {{0.3, 0.2, 0.4}});

    const auto makeVariable = [](
                                  const char* id, const char* label,
                                  const char* target,
                                  rws::StructureVariableKind kind,
                                  double currentValue, double minimum,
                                  double maximum, double step) {
        rws::StructureDesignVariable variable;
        variable.id = id;
        variable.label = label;
        variable.targetName = target;
        variable.unit = "m";
        variable.kind = kind;
        variable.currentValue = currentValue;
        variable.minimum = minimum;
        variable.maximum = maximum;
        variable.step = step;
        variable.enabled = true;
        return variable;
    };
    generatedProject.variables = {
        makeVariable("Joint2_pos_x", "Joint2 X", "Joint2",
                     rws::StructureVariableKind::JointPositionX,
                     -0.425, -0.48, -0.37, 0.005),
        makeVariable("Joint3_pos_x", "Joint3 X", "Joint3",
                     rws::StructureVariableKind::JointPositionX,
                     -0.39243, -0.44, -0.34, 0.005),
        makeVariable("Joint1_pos_z", "Joint1 Z", "Joint1",
                     rws::StructureVariableKind::JointPositionZ,
                     0.0892, 0.07, 0.11, 0.002)};
    generatedProject.evaluation.coverageBox.enabled = true;
    generatedProject.evaluation.coverageBox.minimum = {{-2.0, -2.0, -2.0}};
    generatedProject.evaluation.coverageBox.maximum = {{2.0, 2.0, 2.0}};
    generatedProject.evaluation.coverageBox.cells = {{2, 2, 2}};
    generatedProject.evaluation.quickWorkspace.sampleCount = 8;
    generatedProject.evaluation.verifiedWorkspace.sampleCount = 8;
    generatedProject.run.strategy = rws::StructureStrategyKind::Hybrid;
    generatedProject.run.candidateCount = 8;
    generatedProject.run.eliteCount = 3;
    generatedProject.run.localEliteCount = 2;
    generatedProject.run.finalVerificationCount = 2;
    generatedProject.run.maxLocalSweeps = 2;
    generatedProject.run.gridSteps = 3;
    generatedProject.run.randomSeed = 20260727u;
    generatedProject.objectives =
        rws::StructureOptimizationObjectiveProfile::legacyObjectives(
            generatedProject.weights);

    const QString projectPath = QDir(projectDirectory).filePath(
        "optimizations/UR-6-85-5-A.structure-optimization.json");
    REQUIRE(QDir().mkpath(QFileInfo(projectPath).absolutePath()));
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        projectPath, generatedProject, -1, &projectError));

    rws::RobotModelSpec persistedModelSpec;
    QFile persistedModelFile(modelPath);
    REQUIRE(persistedModelFile.open(QIODevice::ReadOnly));
    if (persistedModelFile.isOpen()) {
        REQUIRE(rws::RobotModelSpecJson::fromJson(
            persistedModelFile.readAll().toStdString(),
            persistedModelSpec, &error));
        persistedModelFile.close();
    }

    rws::StructureOptimizationProblem project;
    int selectedCandidateIndex = -1;
    const bool loaded = rws::StructureOptimizationProjectAdapter::loadProject(
        projectPath, project, &selectedCandidateIndex, &projectError);
    REQUIRE(loaded);
    if (loaded) {
        const std::string sourceFingerprint =
            rws::RobotModelFingerprint::canonicalSha256(persistedModelSpec);
        REQUIRE(project.context.robotName == "UR-6-85-5-A");
        REQUIRE(project.context.modelSpec.robotName == persistedModelSpec.robotName);
        REQUIRE(QDir::cleanPath(QString::fromStdString(project.context.modelSpec.saveDirectory)) ==
                QDir::cleanPath(modelDirectory));
        REQUIRE(project.context.modelSpec.transformJoints.size() ==
                persistedModelSpec.transformJoints.size());
        REQUIRE(!project.context.modelProvenance.sourceFingerprint.empty());
        REQUIRE(project.context.modelProvenance.sourceFingerprint == sourceFingerprint);
        REQUIRE(project.context.modelProvenance.snapshotFingerprint == sourceFingerprint);
        REQUIRE(rws::RobotModelStalenessChecker::check(project.context, projectPath).status ==
                rws::RobotModelSourceStatus::Current);
        REQUIRE(project.tasks.size() == 3);
        REQUIRE(project.tasks[0].point.tcpFrame.empty());
        REQUIRE(project.evaluation.coverageBox.enabled);
        REQUIRE(project.run.randomSeed == 20260727u);
        REQUIRE(project.variables.size() == 3);
        if (project.variables.size() == 3) {
            REQUIRE(project.variables[0].id == "Joint2_pos_x");
            REQUIRE(project.variables[1].id == "Joint3_pos_x");
            REQUIRE(project.variables[2].id == "Joint1_pos_z");
        }

        rws::KinematicEngineeringEvaluator evaluator(project);
        rws::EngineeringEvaluatorPipeline pipeline;
        pipeline.addEvaluator(evaluator);
        rws::StructureOptimizationCallbacks callbacks;
        callbacks.isCancellationRequested = []() { return false; };
        rws::SystemEngineeringOptimizer optimizer;
        const rws::StructureOptimizationResult first = optimizer.optimize(
            project, pipeline, callbacks);
        const rws::StructureOptimizationResult second = optimizer.optimize(
            project, pipeline, callbacks);

        REQUIRE(!first.canceled);
        REQUIRE(first.bestCandidateIndex >= 0);
        REQUIRE(first.bestCandidateIndex == second.bestCandidateIndex);
        REQUIRE(first.sensitivity.robustnessGrade != "Unknown");

        const rws::StructureCandidateResult* best = nullptr;
        for (const rws::StructureCandidateResult& candidate : first.candidates) {
            if (candidate.index == first.bestCandidateIndex) {
                best = &candidate;
                break;
            }
        }
        REQUIRE(best != nullptr);
        if (best != nullptr) {
            REQUIRE(best->feasible);
            REQUIRE(best->raw.requiredReachableCount == best->raw.requiredTaskCount);
            REQUIRE(best->raw.collisionFreeRate == 1.0);
            REQUIRE(best->raw.workspaceCoverage >= 0.01);
        }

        QTemporaryDir exportDirectory;
        REQUIRE(exportDirectory.isValid());
        rws::StructureOptimizationExportRequest request;
        request.directory = exportDirectory.filePath("ur-example-export");
        request.selectedCandidateIndex = first.bestCandidateIndex;
        request.exportCandidateModel = true;
        const rws::StructureOptimizationExportResult exported =
            rws::StructureOptimizationExportService::exportAll(project, first, request);
        REQUIRE(exported.ok);
        const QDir candidateDirectory(request.directory + "/candidate-" +
                                      QString::number(first.bestCandidateIndex));
        REQUIRE(!candidateDirectory.entryList(QStringList() << "*.wc.xml",
                                              QDir::Files).isEmpty());
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 端到端验收(300kg 机器人文件):用 RobotModelBuilderPlugin 从 URDF 创建
// 项目、发布托管 WorkCell、冻结需求,验证项目整体移动后资源仍可解析、冻结工件
// 可重新导入为运动学任务与优化问题;全程校验源目录未被写回(只读源)。
static void testPortable300kgRobotFileProjectAcceptance()
{
    std::printf("testPortable300kgRobotFileProjectAcceptance ... ");

    const QString sourceRoot = QDir(QStringLiteral(STRUCTUREOPTIMIZER_TEST_SOURCE_DIR))
                                   .filePath(QStringLiteral(
                                       "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/"
                                       "300kg_urdf"));
    const QString sourceUrdf = QDir(sourceRoot).filePath(QStringLiteral("output/300kg.urdf"));
    const auto treeSnapshot = [](const QString& root) {
        QMap<QString, QByteArray> result;
        QDirIterator iterator(root, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                result.insert(QStringLiteral("!unreadable!") + path, QByteArray());
                continue;
            }
            result.insert(QDir(root).relativeFilePath(path),
                          QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
        }
        return result;
    };
    const QMap<QString, QByteArray> sourceBefore = treeSnapshot(sourceRoot);
    REQUIRE(QFileInfo(sourceUrdf).isFile());
    REQUIRE(!sourceBefore.isEmpty());

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString originalRoot = directory.filePath(QStringLiteral("RobotProject"));
    const QString originalProject = QDir(originalRoot).filePath(QStringLiteral("300kg.rwproj"));
    REQUIRE(QDir().mkpath(originalRoot));
    QString modelPath;
    QString requirementPath;
    QString publishedWorkCellPath;
    QString error;

    {
        QString requirementDocument;
        rws::CallbackProjectDocumentProvider requirementProvider(
            QStringLiteral("acceptance.requirements"),
            QStringLiteral("rws.engineering-requirements"),
            [](const QString&, const rws::ProjectDocumentContext&, QString*) { return true; },
            [&requirementDocument](const QString& targetPath,
                                   const rws::ProjectDocumentContext&, QString* saveError) {
                QFile source(requirementDocument);
                QFile target(targetPath);
                if (!source.open(QIODevice::ReadOnly) ||
                    !target.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                    target.write(source.readAll()) < 0) {
                    if (saveError != nullptr)
                        *saveError = QStringLiteral("Could not stage the frozen requirement.");
                    return false;
                }
                return true;
            });
        rws::RobotModelBuilderPlugin builderPlugin;
        rw::core::PropertyMap properties;
        rws::RobWorkStudio studio(properties);
        REQUIRE(studio.registerProjectDocumentProvider(&requirementProvider, &error));
        builderPlugin.setRobWorkStudio(&studio);
        builderPlugin.initialize();
        rws::RobotModelBuilderWidget* builder =
            qobject_cast<rws::RobotModelBuilderWidget*>(builderPlugin.widget());
        REQUIRE(builder != nullptr);
        if (builder == nullptr)
            return;

        rws::RobotProjectImportCallbacks callbacks;
        callbacks.preflight = [&builderPlugin](const QString& path, const QString& root,
                                                QString* callbackError) {
            const QString result = builderPlugin.preflightRobotProjectSource(path, root);
            if (callbackError != nullptr)
                *callbackError = result;
            return result.isEmpty();
        };
        callbacks.commit = [&builderPlugin](const QString& path, const QString& root,
                                             QString* callbackError) {
            const QString result = builderPlugin.commitRobotProjectSource(path, root);
            if (callbackError != nullptr)
                *callbackError = result;
            return result.isEmpty();
        };
        const bool created = studio.createProjectFromRobotFilePaths(
            sourceUrdf, originalProject, callbacks, &error);
        if (!created)
            std::fprintf(stderr, "300kg project creation error: %s\n",
                         error.toStdString().c_str());
        REQUIRE(created);
        if (!created)
            return;

        rws::RobotModelSpec runtimeSpec = builder->currentModelSpec();
        REQUIRE(runtimeSpec.robotName == "300kg");
        const std::size_t movableJointCount = static_cast<std::size_t>(std::count_if(
            runtimeSpec.transformJoints.begin(), runtimeSpec.transformJoints.end(),
            [](const rws::JointTransformSpec& joint) {
                return rws::isMovable(rws::typeToKind(joint.type));
            }));
        REQUIRE(movableJointCount == 6);
        REQUIRE(runtimeSpec.drawables.size() == 7);
        REQUIRE(runtimeSpec.collisionModels.size() == 7);

        const auto pathInsideProject = [&originalRoot](const std::string& value) {
            const QString path = QFileInfo(QString::fromStdString(value)).absoluteFilePath();
            const QString relative = QDir::fromNativeSeparators(
                QDir(originalRoot).relativeFilePath(path));
            return QFileInfo(path).isFile() && relative != QStringLiteral("..") &&
                   !relative.startsWith(QStringLiteral("../")) &&
                   !QDir::isAbsolutePath(relative);
        };
        bool geometryContained = true;
        for (const rws::DrawableSpec& drawable : runtimeSpec.drawables)
            geometryContained = geometryContained && pathInsideProject(drawable.filePath);
        for (const rws::CollisionModelSpec& collision : runtimeSpec.collisionModels)
            geometryContained = geometryContained && pathInsideProject(collision.filePath);
        REQUIRE(geometryContained);

        REQUIRE(studio.saveCurrentProject(&error));
        REQUIRE(studio.resolveProjectResource(QStringLiteral("robot-model.main"), modelPath,
                                              &error));
        REQUIRE(QFileInfo(modelPath).isFile());

        rws::RobotModelPublishRequest publishRequest;
        publishRequest.spec = runtimeSpec;
        publishRequest.projectRoot = originalRoot;
        publishRequest.promote = [&studio](const QString& scene,
                                           const QStringList& dependencies,
                                           QString* promoteError) {
            return studio.promoteGeneratedWorkCell(scene, dependencies, promoteError);
        };
        const bool published = rws::RobotModelPublishService::publishAndLoad(
            publishRequest, &error);
        if (!published)
            std::fprintf(stderr, "300kg publication error: %s\n", error.toStdString().c_str());
        REQUIRE(published);
        if (!published) {
            REQUIRE(treeSnapshot(sourceRoot) == sourceBefore);
            std::printf("FAILED (%d)\n", g_testFailures);
            return;
        }

        REQUIRE(!studio.mainWorkCellResourceId().isEmpty());
        REQUIRE(studio.resolveProjectResource(studio.mainWorkCellResourceId(),
                                              publishedWorkCellPath, &error));
        REQUIRE(QFileInfo(publishedWorkCellPath).isFile());
        REQUIRE(studio.getWorkcell() != nullptr);
        REQUIRE(studio.getWorkcell()->getDevices().size() == 1);

        rws::RobotModelBuilderWidget persistedBuilder;
        persistedBuilder.setProjectOutputDirectory(originalRoot);
        REQUIRE(persistedBuilder.loadProjectDocument(modelPath, &error));
        runtimeSpec = persistedBuilder.currentModelSpec();

        const rw::models::WorkCell::Ptr workcell = studio.getWorkcell();
        const rw::kinematics::State state = workcell->getDefaultState();
        const rw::models::Device::Ptr device = workcell->getDevices().front();
        rws::RequirementSet requirements;
        requirements.name = "300kg acceptance requirement";
        requirements.frozen = true;
        requirementPath = QDir(originalRoot).filePath(
            QStringLiteral("requirements/main.requirements.json"));
        REQUIRE(QDir().mkpath(QFileInfo(requirementPath).absolutePath()));
        requirements.modelBinding.sourcePath =
            QDir(QFileInfo(requirementPath).absolutePath())
                .relativeFilePath(modelPath).toStdString();
        requirements.modelBinding.robotName = runtimeSpec.robotName;
        requirements.modelBinding.robotModelFingerprint =
            rws::RobotModelFingerprint::canonicalSha256(runtimeSpec);
        rws::PoseTask task;
        task.id = "home";
        task.name = "300kg home pose";
        task.level = rws::RequirementLevel::Must;
        task.refFrame = "WORLD";
        task.tcpFrame = device->getEnd()->getName();
        const rw::math::Transform3D<> worldTtcp =
            rw::kinematics::Kinematics::worldTframe(device->getEnd(), state);
        task.position = {{worldTtcp.P()[0], worldTtcp.P()[1], worldTtcp.P()[2]}};
        requirements.poseTasks.push_back(task);

        rws::FrozenRequirementArtifact artifact;
        std::string domainError;
        const bool frozen = rws::RequirementFreezer::freeze(
            requirements, *workcell, state, runtimeSpec, artifact, &domainError,
            originalRoot.toStdString());
        if (!frozen)
            std::fprintf(stderr, "300kg freeze error: %s\n", domainError.c_str());
        REQUIRE(frozen);

        QJsonObject requirementObject = rws::RequirementSetJson::toObject(requirements);
        requirementObject.insert(QStringLiteral("frozenArtifact"),
                                 rws::FrozenRequirementArtifactJson::toObject(artifact));
        QFile requirementFile(requirementPath);
        REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray requirementJson = QJsonDocument(requirementObject).toJson();
        REQUIRE(requirementFile.write(requirementJson) == requirementJson.size());
        requirementFile.close();
        requirementDocument = requirementPath;

        std::vector<rws::TaskPoint> kinematicTasks;
        REQUIRE(rws::FrozenRequirementKinematicAdapter::apply(
            artifact, *workcell, state, kinematicTasks, &domainError,
            originalRoot.toStdString()));
        REQUIRE(kinematicTasks.size() == 1);

        rws::StructureOptimizationProblem optimizationProblem;
        rws::FrozenRequirementValidationResult validation;
        const bool optimizationImported =
            rws::FrozenRequirementProjectImportService::createProblem(
            requirementPath, *workcell, state, optimizationProblem, &validation,
            &domainError, originalRoot);
        if (!optimizationImported)
            std::fprintf(stderr, "300kg optimization import error: %s\n", domainError.c_str());
        REQUIRE(optimizationImported);
        REQUIRE(optimizationProblem.tasks.size() == 1);

        rws::ProjectResource requirementResource;
        requirementResource.id = QStringLiteral("engineering-requirements.main");
        requirementResource.kind = QStringLiteral("rws.engineering-requirements");
        requirementResource.path = QDir(originalRoot).relativeFilePath(requirementPath);
        requirementResource.ownership = QStringLiteral("generated");
        requirementResource.required = true;
        requirementResource.dependencies = {QStringLiteral("robot-model.main"),
                                            studio.mainWorkCellResourceId()};
        REQUIRE(studio.ensureGeneratedProjectResource(requirementResource, nullptr, &error));
        REQUIRE(studio.saveCurrentProject(&error));
    }

    const QString movedRoot = directory.filePath(QStringLiteral("RobotProjectMoved"));
    REQUIRE(QDir().rename(originalRoot, movedRoot));
    const QString movedProject = QDir(movedRoot).filePath(QStringLiteral("300kg.rwproj"));
    QTemporaryDir hostileDirectory;
    REQUIRE(hostileDirectory.isValid());
    const QString previousCwd = QDir::currentPath();
    REQUIRE(QDir::setCurrent(hostileDirectory.path()));

    rws::ProjectManager reopened;
    REQUIRE(reopened.openProject(movedProject, &error));
    QString movedSource;
    QString movedModel;
    QString movedWorkCell;
    QString movedRequirements;
    REQUIRE(reopened.resolveResource(QStringLiteral("robot-source.main"), movedSource, &error));
    REQUIRE(reopened.resolveResource(QStringLiteral("robot-model.main"), movedModel, &error));
    REQUIRE(reopened.resolveResource(
        reopened.manifest().entryPoints.value(QStringLiteral("mainWorkCell")),
        movedWorkCell, &error));
    REQUIRE(reopened.resolveResource(QStringLiteral("engineering-requirements.main"),
                                     movedRequirements, &error));
    REQUIRE(movedSource.startsWith(QDir::cleanPath(movedRoot)));
    REQUIRE(movedModel.startsWith(QDir::cleanPath(movedRoot)));
    REQUIRE(movedWorkCell.startsWith(QDir::cleanPath(movedRoot)));
    REQUIRE(movedRequirements.startsWith(QDir::cleanPath(movedRoot)));

    rws::RobotModelBuilderWidget movedBuilder;
    movedBuilder.setProjectOutputDirectory(movedRoot);
    REQUIRE(movedBuilder.loadProjectDocument(movedModel, &error));
    const rws::RobotModelSpec movedSpec = movedBuilder.currentModelSpec();
    REQUIRE(movedSpec.drawables.size() == 7);
    const rw::models::WorkCell::Ptr movedCell =
        rw::loaders::WorkCellLoader::Factory::load(movedWorkCell.toStdString());
    REQUIRE(!movedCell.isNull());
    if (!movedCell.isNull()) {
        QFile requirementFile(movedRequirements);
        REQUIRE(requirementFile.open(QIODevice::ReadOnly));
        const QJsonDocument requirementJson = QJsonDocument::fromJson(requirementFile.readAll());
        requirementFile.close();
        rws::FrozenRequirementArtifact movedArtifact;
        std::string domainError;
        REQUIRE(rws::FrozenRequirementKinematicAdapter::parseArtifactJson(
            requirementJson.object(), movedArtifact, &domainError));
        std::vector<rws::TaskPoint> movedTasks;
        REQUIRE(rws::FrozenRequirementKinematicAdapter::apply(
            movedArtifact, *movedCell, movedCell->getDefaultState(), movedTasks, &domainError,
            movedRoot.toStdString()));
        REQUIRE(movedTasks.size() == 1);
        rws::StructureOptimizationProblem movedProblem;
        rws::FrozenRequirementValidationResult validation;
        const bool movedOptimizationImported =
            rws::FrozenRequirementProjectImportService::createProblem(
            movedRequirements, *movedCell, movedCell->getDefaultState(), movedProblem,
            &validation, &domainError, movedRoot);
        if (!movedOptimizationImported)
            std::fprintf(stderr, "Moved 300kg optimization import error: %s\n",
                         domainError.c_str());
        REQUIRE(movedOptimizationImported);
        REQUIRE(movedProblem.tasks.size() == 1);
    }
    REQUIRE(QDir::setCurrent(previousCwd));
    REQUIRE(treeSnapshot(sourceRoot) == sourceBefore);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 优化器项目资源依赖:首次创建时插件生成 structure-optimization.main 资源
// 并挂接当前可解析的上游(scene.main/robot-model.main/engineering-requirements.main);
// 对已存在的旧资源,保存时去重并保留非空依赖,不能重复添加自身或残留脏依赖。
static void testStructureOptimizerResourceDependencies()
{
    std::printf("testStructureOptimizerResourceDependencies ... ");

    // First creation remains an independent path: no legacy resource exists, so the plugin must
    // create its default resource and attach exactly the currently resolvable upstream IDs.
    {
        QTemporaryDir creationDirectory;
        REQUIRE(creationDirectory.isValid());
        const QString creationProjectPath = creationDirectory.filePath(
            "CreationProject/CreationProject.rwproj");
        const QString creationProjectDirectory =
            QFileInfo(creationProjectPath).absolutePath();
        const QString creationModelPath = QDir(creationProjectDirectory).filePath(
            "generated/robot-models/main.rmb.json");
        const QString creationRequirementsPath = QDir(creationProjectDirectory).filePath(
            "requirements/main.json");
        REQUIRE(QDir().mkpath(QFileInfo(creationModelPath).absolutePath()));
        REQUIRE(QDir().mkpath(QFileInfo(creationRequirementsPath).absolutePath()));
        const auto writeCreationFile = [](const QString& path, const QByteArray& data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                   file.write(data) == data.size();
        };

        QString creationError;
        rws::ProjectManager creationManager;
        REQUIRE(creationManager.createProjectFromWorkCell(
            creationProjectPath,
            sourcePath("RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml"),
            &creationError));
        REQUIRE(writeCreationFile(creationModelPath, "{}\n"));
        REQUIRE(writeCreationFile(creationRequirementsPath, "{}\n"));
        const auto addCreationResource = [&creationManager, &creationError](
                                             const QString& id, const QString& kind,
                                             const QString& path) {
            rws::ProjectResource resource;
            resource.id = id;
            resource.kind = kind;
            resource.path = path;
            resource.ownership = "generated";
            resource.required = false;
            return creationManager.addGeneratedResource(resource, &creationError);
        };
        REQUIRE(addCreationResource("robot-model.main", "rws.robot-model",
                                    "generated/robot-models/main.rmb.json"));
        REQUIRE(addCreationResource("engineering-requirements.main",
                                    "rws.engineering-requirements",
                                    "requirements/main.json"));
        REQUIRE(creationManager.saveProject(&creationError));
        creationManager.closeProject();

        const auto loadDocument = [](const QString&, const rws::ProjectDocumentContext&,
                                     QString*) { return true; };
        const auto saveDocument = [](const QString&, const rws::ProjectDocumentContext&,
                                     QString*) { return true; };
        rws::CallbackProjectDocumentProvider robotModelProvider(
            "creation.robot-model-provider", "rws.robot-model", loadDocument, saveDocument);
        rws::CallbackProjectDocumentProvider requirementProvider(
            "creation.requirement-provider", "rws.engineering-requirements",
            loadDocument, saveDocument);
        rw::core::PropertyMap creationProperties;
        rws::RobWorkStudio creationStudio(creationProperties);
        REQUIRE(creationStudio.registerProjectDocumentProvider(
            &robotModelProvider, &creationError));
        REQUIRE(creationStudio.registerProjectDocumentProvider(
            &requirementProvider, &creationError));
        rws::StructureOptimizerPlugin creationPlugin;
        creationPlugin.setRobWorkStudio(&creationStudio);
        creationPlugin.initialize();
        creationStudio.openFile(creationProjectPath.toStdString());

        rws::StructureOptimizationProblem creationProblem;
        std::string creationFactoryError;
        REQUIRE(rws::StructureOptimizationProjectFactory::create(
            rws::RobotModelXmlWriter::makeDefaultSixAxisModel(creationDirectory.path()),
            creationProblem, &creationFactoryError));
        rws::StructureOptimizerWidget* creationWidget =
            qobject_cast<rws::StructureOptimizerWidget*>(creationPlugin.widget());
        REQUIRE(creationWidget != nullptr);
        if (creationWidget != nullptr) {
            creationWidget->setProblem(creationProblem);
            REQUIRE(QMetaObject::invokeMethod(
                creationWidget, "projectDocumentChanged", Qt::DirectConnection));
        }
        REQUIRE(creationStudio.saveCurrentProject(&creationError));

        rws::ProjectManager creationVerification;
        REQUIRE(creationVerification.openProject(creationProjectPath, &creationError));
        rws::ProjectResource createdOptimization;
        REQUIRE(creationVerification.manifest().findResource(
            "structure-optimization.main", createdOptimization));
        REQUIRE(createdOptimization.kind == "rws.structure-optimization");
        REQUIRE(createdOptimization.path ==
                "optimizations/main.structure-optimization.json");
        REQUIRE(createdOptimization.ownership == "generated");
        REQUIRE(createdOptimization.required);
        REQUIRE(createdOptimization.dependencies == QStringList({
            "scene.main", "robot-model.main", "engineering-requirements.main"}));
        creationStudio.close();
        creationPlugin.setRobWorkStudio(nullptr);
    }

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectPath =
        directory.filePath("DependencyProject/DependencyProject.rwproj");
    const QString sourceWorkCell = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml");
    const QString projectDirectory = QFileInfo(projectPath).absolutePath();
    const QString modelPath =
        QDir(projectDirectory).filePath("generated/robot-models/main.rmb.json");
    const QString requirementsPath =
        QDir(projectDirectory).filePath("requirements/main.json");
    REQUIRE(QDir().mkpath(QFileInfo(modelPath).absolutePath()));
    REQUIRE(QDir().mkpath(QFileInfo(requirementsPath).absolutePath()));

    const auto writeFile = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               file.write(data) == data.size();
    };
    QString error;
    rws::ProjectManager manager;
    REQUIRE(manager.createProjectFromWorkCell(projectPath, sourceWorkCell, &error));
    REQUIRE(writeFile(modelPath, "{}\n"));
    REQUIRE(writeFile(requirementsPath, "{}\n"));

    const auto addResource = [&manager, &error](const QString& id, const QString& kind,
                                                const QString& path) {
        rws::ProjectResource resource;
        resource.id = id;
        resource.kind = kind;
        resource.path = path;
        resource.ownership = "generated";
        resource.required = false;
        return manager.addGeneratedResource(resource, &error);
    };
    REQUIRE(addResource("robot-model.main", "rws.robot-model",
                        "generated/robot-models/main.rmb.json"));
    REQUIRE(addResource("engineering-requirements.main", "rws.engineering-requirements",
                        "requirements/main.json"));
    REQUIRE(addResource("legacy.upstream", "legacy.upstream",
                        "requirements/legacy-upstream.json"));
    REQUIRE(writeFile(QDir(projectDirectory).filePath("requirements/legacy-upstream.json"),
                      "{}\n"));

    rws::StructureOptimizationProblem legacyProblem;
    std::string legacyFactoryError;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(projectDirectory),
        legacyProblem, &legacyFactoryError));
    const QString optimizationPath = QDir(projectDirectory).filePath(
        "legacy/legacy.structure-optimization.json");
    REQUIRE(QDir().mkpath(QFileInfo(optimizationPath).absolutePath()));
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        optimizationPath, legacyProblem, -1, &error));
    rws::ProjectResource legacyOptimization;
    legacyOptimization.id = "structure-optimization.main";
    legacyOptimization.kind = "rws.structure-optimization";
    legacyOptimization.path = "legacy/legacy.structure-optimization.json";
    legacyOptimization.ownership = "project";
    legacyOptimization.required = false;
    legacyOptimization.dependencies = {"scene.main", "legacy.upstream", "scene.main"};
    REQUIRE(manager.addGeneratedResource(legacyOptimization, &error));
    REQUIRE(manager.saveProject(&error));
    manager.closeProject();

    rws::StructureOptimizerPlugin plugin;
    const auto loadDocument = [](const QString&, const rws::ProjectDocumentContext&,
                                 QString*) { return true; };
    const auto saveDocument = [](const QString&, const rws::ProjectDocumentContext&,
                                 QString*) { return true; };
    rws::CallbackProjectDocumentProvider robotModelProvider(
        "test.robot-model-provider", "rws.robot-model", loadDocument, saveDocument);
    rws::CallbackProjectDocumentProvider requirementProvider(
        "test.requirement-provider", "rws.engineering-requirements",
        loadDocument, saveDocument);
    rws::CallbackProjectDocumentProvider legacyProvider(
        "test.legacy-provider", "legacy.upstream", loadDocument, saveDocument);
    rw::core::PropertyMap properties;
    rws::RobWorkStudio studio(properties);
    REQUIRE(studio.registerProjectDocumentProvider(&robotModelProvider, &error));
    REQUIRE(studio.registerProjectDocumentProvider(&requirementProvider, &error));
    REQUIRE(studio.registerProjectDocumentProvider(&legacyProvider, &error));
    plugin.setRobWorkStudio(&studio);
    plugin.initialize();
    studio.openFile(projectPath.toStdString());
    REQUIRE(!studio.projectDirectory().isEmpty());

    REQUIRE(qobject_cast<rws::StructureOptimizerWidget*>(plugin.widget()) != nullptr);
    REQUIRE(studio.saveCurrentProject(&error));

    rws::ProjectManager verificationManager;
    REQUIRE(verificationManager.openProject(projectPath, &error));
    rws::ProjectResource optimizationResource;
    REQUIRE(verificationManager.manifest().findResource(
        "structure-optimization.main", optimizationResource));
    const QSet<QString> uniqueDependencies(optimizationResource.dependencies.begin(),
                                           optimizationResource.dependencies.end());
    REQUIRE(optimizationResource.dependencies.size() == 3);
    REQUIRE(uniqueDependencies.size() == optimizationResource.dependencies.size());
    REQUIRE(uniqueDependencies.contains("scene.main"));
    REQUIRE(uniqueDependencies.contains("robot-model.main"));
    REQUIRE(uniqueDependencies.contains("engineering-requirements.main"));
    REQUIRE(optimizationResource.kind == legacyOptimization.kind);
    REQUIRE(optimizationResource.path == legacyOptimization.path);
    REQUIRE(optimizationResource.ownership == legacyOptimization.ownership);
    REQUIRE(optimizationResource.required == legacyOptimization.required);

    studio.close();
    plugin.setRobWorkStudio(nullptr);
    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 候选预览控制器:验证 preview 把候选模型导出并交给宿主打开
// (host.current 变为新 WC),clearPreview 恢复原 WC,且记录被预览的候选索引。
static void testCandidatePreviewController()
{
    std::printf("testCandidatePreviewController ... ");

    struct FakeHost : rws::IWorkCellPreviewHost {
        QString current = "original.wc.xml";
        QString lastOpened;
        bool openWorkCell(const QString& path, QString*) override {
            lastOpened = path;
            current = path;
            return true;
        }
        QString currentWorkCellPath() override { return current; }
    } host;

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;
    rws::StructureCandidateResult candidate;
    candidate.index = 3;
    candidate.feasible = true;
    candidate.status = rws::StructureCandidateStatus::Feasible;

    rws::CandidatePreviewController preview(&host);
    QString error;
    const bool previewed = preview.preview(problem, candidate, &error);
    if (!previewed)
        std::fprintf(stderr, "  Preview error: %s\n", error.toStdString().c_str());
    REQUIRE(previewed);
    REQUIRE(preview.previewedCandidateIndex() == 3);
    REQUIRE(host.current != "original.wc.xml");
    preview.clearPreview();
    REQUIRE(host.current == "original.wc.xml");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 优化器 Widget 状态:验证五个页签与关键按钮存在、任务/约束表的增删复制、
// collectProblem 的字段保真;项目文档脏状态与 markClean 事务;模型快照过期/托管
// 场景的状态文案;非法模型规格禁用启动按钮。
static void testStructureOptimizerWidgetState()
{
    std::printf("testStructureOptimizerWidgetState ... ");

    rws::StructureOptimizerWidget widget;
    QWidget* modelStatusBanner =
        widget.findChild<QWidget*>("structureModelStatusBanner");
    QLabel* modelStatusBannerText =
        widget.findChild<QLabel*>("structureModelStatusBannerText");
    REQUIRE(modelStatusBanner != nullptr);
    REQUIRE(modelStatusBannerText != nullptr);
    if (modelStatusBanner == nullptr || modelStatusBannerText == nullptr)
        return;

    QTabWidget* tabs = widget.findChild<QTabWidget*>("structureOptimizerTabs");
    REQUIRE(tabs != nullptr);
    if (tabs != nullptr) {
        REQUIRE(tabs->count() == 5);
        REQUIRE(tabs->tabText(0).toStdString() == "Design Variables");
        REQUIRE(tabs->tabText(1).toStdString() == "Tasks & Constraints");
        REQUIRE(tabs->tabText(2).toStdString() == "Optimization Settings");
        REQUIRE(tabs->tabText(3).toStdString() == "Candidates");
        REQUIRE(tabs->tabText(4).toStdString() == "Export Report");
    }

    QPushButton* startButton =
        widget.findChild<QPushButton*>("startOptimizationButton");
    REQUIRE(startButton != nullptr);
    if (startButton != nullptr)
        REQUIRE(!startButton->isEnabled());
    REQUIRE(widget.findChild<QPushButton*>(
                "newStructureOptimizationProjectFromModelBannerButton") != nullptr);
    // 需求定义插件冻结后的工件必须有明确入口，避免工程师重新手工录入已经校验的任务点。
    REQUIRE(widget.findChild<QPushButton*>(
                "newStructureOptimizationProjectFromFrozenRequirementBannerButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>(
                "newStructureOptimizationProjectFromModelButton") == nullptr);
    REQUIRE(widget.findChild<QPushButton*>(
                "newStructureOptimizationProjectFromFrozenRequirementButton") == nullptr);

    rws::StructureOptimizationProblem emptyTaskProject;
    std::string factoryError;
    const rws::RobotModelSpec factorySpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        factorySpec, emptyTaskProject, &factoryError));
    REQUIRE(emptyTaskProject.tasks.empty());
    widget.setProblem(emptyTaskProject);
    if (startButton != nullptr)
        REQUIRE(!startButton->isEnabled());

    QTableView* taskView = widget.findChild<QTableView*>("optimizationTaskTable");
    QPushButton* addTaskButton =
        widget.findChild<QPushButton*>("addOptimizationTaskButton");
    REQUIRE(taskView != nullptr);
    REQUIRE(addTaskButton != nullptr);
    if (taskView != nullptr && addTaskButton != nullptr) {
        REQUIRE(taskView->model()->rowCount() == 0);
        addTaskButton->click();
        REQUIRE(taskView->model()->rowCount() == 1);
        REQUIRE(taskView->model()->columnCount() == 13);
        REQUIRE(taskView->model()->data(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::IdColumn))
                    .toString().startsWith("task_"));
        REQUIRE(taskView->model()->data(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::RefFrameColumn))
                    .toString() == "WORLD");
        REQUIRE(taskView->model()->data(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::TcpFrameColumn))
                    .toString().isEmpty());
        REQUIRE(taskView->model()->setData(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::RollColumn),
                    15.0));
        REQUIRE(taskView->model()->setData(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::TcpFrameColumn),
                    "Joint5"));
        const rws::StructureOptimizationProblem edited = widget.collectProblem();
        REQUIRE(edited.tasks[0].point.rpyDeg[0] == 15.0);
        REQUIRE(edited.tasks[0].point.tcpFrame == "Joint5");

        QPushButton* duplicateTaskButton =
            widget.findChild<QPushButton*>("duplicateOptimizationTaskButton");
        QPushButton* removeTaskButton =
            widget.findChild<QPushButton*>("removeOptimizationTaskButton");
        REQUIRE(duplicateTaskButton != nullptr);
        REQUIRE(removeTaskButton != nullptr);
        if (duplicateTaskButton != nullptr && removeTaskButton != nullptr) {
            taskView->selectRow(0);
            duplicateTaskButton->click();
            REQUIRE(taskView->model()->rowCount() == 2);
            REQUIRE(taskView->model()->data(
                        taskView->model()->index(1, rws::OptimizationTaskTableModel::IdColumn))
                        .toString() != taskView->model()->data(
                        taskView->model()->index(0, rws::OptimizationTaskTableModel::IdColumn))
                        .toString());
            removeTaskButton->click();
            REQUIRE(taskView->model()->rowCount() == 1);
        }
    }

    QTableView* constraintView =
        widget.findChild<QTableView*>("structureConstraintTable");
    QPushButton* addConstraintButton =
        widget.findChild<QPushButton*>("addStructureConstraintButton");
    REQUIRE(constraintView != nullptr);
    REQUIRE(addConstraintButton != nullptr);
    if (constraintView != nullptr && addConstraintButton != nullptr) {
        REQUIRE(constraintView->model()->rowCount() == 0);
        addConstraintButton->click();
        REQUIRE(constraintView->model()->rowCount() == 1);
        QComboBox* constraintKindCombo =
            widget.findChild<QComboBox*>("newStructureConstraintKindCombo");
        REQUIRE(constraintKindCombo != nullptr);
        if (constraintKindCombo != nullptr) {
            constraintKindCombo->setCurrentIndex(constraintKindCombo->findData(
                static_cast<int>(rws::StructureConstraintKind::MinimumJointMargin)));
            addConstraintButton->click();
            REQUIRE(constraintView->model()->rowCount() == 2);
            REQUIRE(constraintView->model()->data(
                        constraintView->model()->index(
                            1, rws::StructureConstraintTableModel::KindColumn))
                        .toString() == "Minimum Joint Margin");
            REQUIRE(constraintView->model()->data(
                        constraintView->model()->index(
                            1, rws::StructureConstraintTableModel::ThresholdColumn))
                        .toDouble() == 0.01);
        }

        QPushButton* duplicateConstraintButton =
            widget.findChild<QPushButton*>("duplicateStructureConstraintButton");
        QPushButton* removeConstraintButton =
            widget.findChild<QPushButton*>("removeStructureConstraintButton");
        REQUIRE(duplicateConstraintButton != nullptr);
        REQUIRE(removeConstraintButton != nullptr);
        if (duplicateConstraintButton != nullptr && removeConstraintButton != nullptr) {
            constraintView->selectRow(0);
            duplicateConstraintButton->click();
            REQUIRE(constraintView->model()->rowCount() == 3);
            removeConstraintButton->click();
            REQUIRE(constraintView->model()->rowCount() == 2);
        }
    }

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;

    rws::StructureDesignVariable variable;
    variable.id = "joint1_z";
    variable.label = "Joint1 Z";
    variable.targetName = problem.context.modelSpec.transformJoints[0].name;
    variable.kind = rws::StructureVariableKind::JointPositionZ;
    variable.currentValue = problem.context.modelSpec.transformJoints[0].pos[2];
    variable.minimum = variable.currentValue - 0.1;
    variable.maximum = variable.currentValue + 0.1;
    variable.step = 0.001;
    variable.enabled = true;
    problem.variables.push_back(variable);

    rws::OptimizationTaskPoint task;
    task.point.id = "target";
    task.point.name = "Target";
    task.point.enabled = true;
    task.required = true;
    problem.tasks.push_back(task);
    problem.constraints.push_back({"coverage", "Coverage", "workcell",
                                   rws::StructureConstraintKind::MinimumWorkspaceCoverage,
                                   0.8, 0.0, true, true});
    problem.run.strategy = rws::StructureStrategyKind::Grid;
    problem.run.localEliteCount = 4;
    problem.run.finalVerificationCount = 2;
    problem.run.maxLocalSweeps = 17;
    problem.run.gridSteps = 6;
    problem.weights.reachability = 0.30;
    problem.weights.preference = 0.10;

    widget.setProblem(problem);
    if (startButton != nullptr)
        REQUIRE(startButton->isEnabled());

    // Provider 使用 Widget 的规范 JSON 快照判断脏状态。加载后必须干净，修改一个
    // 已持久化任务字段后变脏；只有模拟保存事务完整提交后的 markClean 才恢复干净。
    QTemporaryDir projectDocumentDirectory;
    REQUIRE(projectDocumentDirectory.isValid());
    const QString projectDocument = projectDocumentDirectory.filePath("optimization.json");
    QString projectDocumentError;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        projectDocument, problem, -1, &projectDocumentError));
    REQUIRE(widget.loadProjectDocument(projectDocument, &projectDocumentError));
    REQUIRE(!widget.isProjectDocumentDirty());
    if (taskView != nullptr) {
        REQUIRE(taskView->model()->setData(
            taskView->model()->index(0, rws::OptimizationTaskTableModel::RollColumn), 22.0));
        REQUIRE(widget.isProjectDocumentDirty());
        REQUIRE(widget.saveProjectDocument(projectDocument, &projectDocumentError));
        widget.markProjectDocumentClean();
        REQUIRE(!widget.isProjectDocumentDirty());
    }
    QTableView* variableTable = widget.findChild<QTableView*>("structureVariableTable");
    QPushButton* duplicateVariableButton =
        widget.findChild<QPushButton*>("duplicateStructureVariableButton");
    REQUIRE(variableTable != nullptr);
    REQUIRE(duplicateVariableButton != nullptr);
    if (variableTable != nullptr && duplicateVariableButton != nullptr) {
        REQUIRE(variableTable->model()->setData(
            variableTable->model()->index(
                0, rws::StructureVariableTableModel::CurrentColumn), 0.1));
        REQUIRE(widget.isProjectDocumentDirty());
        REQUIRE(widget.saveProjectDocument(projectDocument, &projectDocumentError));
        widget.markProjectDocumentClean();
        variableTable->selectRow(0);
        REQUIRE(duplicateVariableButton->isEnabled());
        duplicateVariableButton->click();
        REQUIRE(variableTable->model()->rowCount() == 2);
        REQUIRE(widget.isProjectDocumentDirty());
    }
    REQUIRE(widget.canCloseProjectDocument(&projectDocumentError));

    QTemporaryDir provenanceDirectory;
    REQUIRE(provenanceDirectory.isValid());
    rws::RobotModelSpec sourceSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(provenanceDirectory.path());
    sourceSpec.robotName = "StaleWidgetRobot";
    QStringList sourceSaveErrors;
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(sourceSpec, sourceSaveErrors));
    const QString sourceModelPath =
        rws::RobotModelXmlWriter::specSidecarFilePath(sourceSpec);
    rws::StructureOptimizationProblem staleProblem;
    std::string staleFactoryError;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        sourceSpec, sourceModelPath, staleProblem, &staleFactoryError));
    staleProblem.variables = problem.variables;
    staleProblem.tasks = problem.tasks;
    staleProblem.constraints = problem.constraints;
    staleProblem.run = problem.run;
    staleProblem.weights = problem.weights;
    staleProblem.objectives = problem.objectives;

    rws::RobotModelSpec changedSourceSpec = sourceSpec;
    changedSourceSpec.transformJoints.at(1).pos[0] += 0.001;
    sourceSaveErrors.clear();
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(changedSourceSpec, sourceSaveErrors));
    widget.setProblem(staleProblem);
    if (startButton != nullptr)
        REQUIRE(startButton->isEnabled());
    REQUIRE(!modelStatusBanner->isHidden());
    REQUIRE(modelStatusBannerText->text().contains(
        QStringLiteral("stale"), Qt::CaseInsensitive));

    QTemporaryDir managedStatusDirectory;
    REQUIRE(managedStatusDirectory.isValid());
    const QString managedStatusRoot = managedStatusDirectory.filePath("ManagedProject");
    const QString managedStatusAsset =
        QDir(managedStatusRoot).filePath("assets/base.stl");
    REQUIRE(QDir().mkpath(QFileInfo(managedStatusAsset).absolutePath()));
    QFile managedStatusAssetFile(managedStatusAsset);
    REQUIRE(managedStatusAssetFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(managedStatusAssetFile.write("solid managed\nendsolid managed\n") > 0);
    managedStatusAssetFile.close();
    rws::RobotModelSpec managedPortable =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(managedStatusRoot);
    managedPortable.robotName = "ManagedWidgetStatus";
    managedPortable.saveDirectory.clear();
    managedPortable.drawables.clear();
    managedPortable.collisionModels.clear();
    managedPortable.sceneGeometries.clear();
    rws::DrawableSpec managedDrawable;
    managedDrawable.name = "managed-widget-geometry";
    managedDrawable.refFrame = "WORLD";
    managedDrawable.shape = "Mesh";
    managedDrawable.filePath = "assets/base.stl";
    managedPortable.drawables.push_back(managedDrawable);
    rws::RobotModelSpec managedRuntime;
    QString managedPathError;
    REQUIRE(rws::RobotModelProjectPaths::resolveManaged(
        managedPortable, managedStatusRoot, managedRuntime, &managedPathError));
    const QString managedStatusSource =
        QDir(managedStatusRoot).filePath("models/main.rmb.json");
    REQUIRE(QDir().mkpath(QFileInfo(managedStatusSource).absolutePath()));
    QFile managedStatusSourceFile(managedStatusSource);
    REQUIRE(managedStatusSourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray managedStatusJson =
        QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(managedPortable));
    REQUIRE(managedStatusSourceFile.write(managedStatusJson) == managedStatusJson.size());
    managedStatusSourceFile.close();
    rws::StructureOptimizationProblem managedStatusProblem;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        managedRuntime, managedStatusSource, managedStatusProblem, &staleFactoryError));
    managedStatusProblem.variables = problem.variables;
    managedStatusProblem.tasks = problem.tasks;
    managedStatusProblem.constraints = problem.constraints;
    managedStatusProblem.run = problem.run;
    managedStatusProblem.weights = problem.weights;
    managedStatusProblem.objectives = problem.objectives;
    const QString managedStatusDocument = QDir(managedStatusRoot).filePath(
        "optimizations/main.structure-optimization.json");
    REQUIRE(QDir().mkpath(QFileInfo(managedStatusDocument).absolutePath()));
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        managedStatusDocument, managedStatusProblem, -1, &projectDocumentError));
    REQUIRE(widget.loadProjectDocument(
        managedStatusDocument, &projectDocumentError, managedStatusRoot));
    REQUIRE(modelStatusBanner->isHidden());
    rws::StructureOptimizationProblem standaloneStatusProblem;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        managedPortable, managedStatusSource, standaloneStatusProblem, &staleFactoryError));
    standaloneStatusProblem.variables = problem.variables;
    standaloneStatusProblem.tasks = problem.tasks;
    standaloneStatusProblem.constraints = problem.constraints;
    standaloneStatusProblem.run = problem.run;
    standaloneStatusProblem.weights = problem.weights;
    standaloneStatusProblem.objectives = problem.objectives;
    widget.setProblem(standaloneStatusProblem);
    REQUIRE(modelStatusBanner->isHidden());
    REQUIRE(widget.loadProjectDocument(
        managedStatusDocument, &projectDocumentError, managedStatusRoot));
    REQUIRE(modelStatusBanner->isHidden());
    REQUIRE(widget.loadProjectDocument(managedStatusDocument, &projectDocumentError));
    REQUIRE(!modelStatusBanner->isHidden());
    REQUIRE(modelStatusBannerText->text().contains(
        QStringLiteral("stale"), Qt::CaseInsensitive));

    const rws::StructureOptimizationProblem collected = widget.collectProblem();
    REQUIRE(collected.variables.size() == 1);
    REQUIRE(collected.tasks.size() == 1);
    REQUIRE(collected.variables[0].id == "joint1_z");
    REQUIRE(collected.tasks[0].point.id == "target");
    REQUIRE(collected.constraints.size() == 1);
    REQUIRE(collected.run.strategy == rws::StructureStrategyKind::Grid);
    REQUIRE(collected.run.localEliteCount == 4);
    REQUIRE(collected.run.finalVerificationCount == 2);
    REQUIRE(collected.run.maxLocalSweeps == 17);
    REQUIRE(collected.run.gridSteps == 6);
    REQUIRE(std::abs(collected.weights.reachability - 0.30) < 1e-12);
    REQUIRE(std::abs(collected.weights.preference - 0.10) < 1e-12);
    REQUIRE(rws::StructureOptimizationObjectiveProfile::isLegacyProfile(
        collected.objectives));
    REQUIRE(std::abs(collected.objectives[0].weight - 0.30) < 1e-12);

    REQUIRE(widget.findChild<QComboBox*>("structureOptimizationStrategyCombo") != nullptr);
    REQUIRE(widget.findChild<QSpinBox*>("structureOptimizationLocalEliteCount") != nullptr);
    REQUIRE(widget.findChild<QSpinBox*>("structureOptimizationFinalVerificationCount") != nullptr);
    REQUIRE(widget.findChild<QSpinBox*>("structureOptimizationMaxLocalSweeps") != nullptr);
    REQUIRE(widget.findChild<QSpinBox*>("structureOptimizationGridSteps") != nullptr);
    REQUIRE(widget.findChild<QDoubleSpinBox*>("structureOptimizationWeightReachability") != nullptr);
    REQUIRE(widget.findChild<QDoubleSpinBox*>("structureOptimizationWeightPreference") != nullptr);

    rws::StructureOptimizationProblem invalidProblem = problem;
    invalidProblem.context.modelSpec = rws::RobotModelSpec();
    widget.setProblem(invalidProblem);
    if (startButton != nullptr)
        REQUIRE(!startButton->isEnabled());
    REQUIRE(widget.statusText().toStdString().find(
                "StructureOptimization.Context.Invalid") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureOptimizerModelStatusGuidance()
{
    std::printf("testStructureOptimizerModelStatusGuidance ... ");
    rws::StructureOptimizerWidget widget;
    QWidget* banner = widget.findChild<QWidget*>("structureModelStatusBanner");
    QLabel* bannerText = widget.findChild<QLabel*>("structureModelStatusBannerText");
    QLabel* bannerSource = widget.findChild<QLabel*>("structureModelStatusBannerSource");
    QPushButton* newFromModel = widget.findChild<QPushButton*>(
        "newStructureOptimizationProjectFromModelBannerButton");
    QPushButton* newFromRequirements = widget.findChild<QPushButton*>(
        "newStructureOptimizationProjectFromFrozenRequirementBannerButton");
    REQUIRE(banner != nullptr);
    REQUIRE(bannerText != nullptr);
    REQUIRE(bannerSource != nullptr);
    REQUIRE(newFromModel != nullptr);
    REQUIRE(newFromRequirements != nullptr);
    if (banner == nullptr || bannerText == nullptr || bannerSource == nullptr ||
        newFromModel == nullptr || newFromRequirements == nullptr)
        return;

    rws::StructureOptimizationProblem untracked;
    untracked.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    untracked.context.robotName = untracked.context.modelSpec.robotName;
    untracked.context.deviceName = untracked.context.modelSpec.robotName;
    widget.setProblem(untracked);
    REQUIRE(!banner->isHidden());
    REQUIRE(bannerText->text().contains(QStringLiteral("untracked"), Qt::CaseInsensitive));
    REQUIRE(!newFromModel->isHidden());
    REQUIRE(!newFromRequirements->isHidden());

    QTemporaryDir sourceDirectory;
    REQUIRE(sourceDirectory.isValid());
    rws::RobotModelSpec sourceSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(sourceDirectory.path());
    sourceSpec.robotName = "StatusGuidanceRobot";
    QStringList saveErrors;
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(sourceSpec, saveErrors));
    const QString sourcePath = rws::RobotModelXmlWriter::specSidecarFilePath(sourceSpec);
    rws::StructureOptimizationProblem tracked;
    std::string factoryError;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        sourceSpec, sourcePath, tracked, &factoryError));
    widget.setProblem(tracked);
    REQUIRE(banner->isHidden());

    rws::RobotModelSpec changed = sourceSpec;
    const double frozenPosition = tracked.context.modelSpec.transformJoints.at(1).pos[0];
    changed.transformJoints.at(1).pos[0] += 0.005;
    saveErrors.clear();
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(changed, saveErrors));
    widget.setProblem(tracked);
    REQUIRE(!banner->isHidden());
    REQUIRE(bannerText->text().contains(QStringLiteral("stale"), Qt::CaseInsensitive));
    REQUIRE(bannerSource->text().contains(sourcePath));
    REQUIRE(std::abs(widget.collectProblem().context.modelSpec.transformJoints.at(1).pos[0] -
                     frozenPosition) < 1e-12);

    REQUIRE(QFile::remove(sourcePath));
    widget.setProblem(tracked);
    REQUIRE(bannerText->text().contains(QStringLiteral("missing"), Qt::CaseInsensitive));

    rws::StructureOptimizationProblem incomplete;
    widget.setProblem(incomplete);
    REQUIRE(!banner->isHidden());
    REQUIRE(bannerText->text().contains(QStringLiteral("incomplete"), Qt::CaseInsensitive));
    REQUIRE(!widget.findChild<QPushButton*>("startOptimizationButton")->isEnabled());

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 托管项目门禁:构造只有源文件与模型文件、尚未发布托管 WorkCell 的机器人
// 项目,点击"从冻结需求新建"时必须显示明确的 WorkCell 就绪门禁文案,禁止在模型
// 发布前创建优化项目。需独立运行(依赖完整 RobWorkStudio 栈)。
static void testManagedRobotProjectRequiresPublishedWorkCell()
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectPath = directory.filePath("RobotDraft/RobotDraft.rwproj");
    rws::ProjectManifest manifest;
    manifest.project.id = "robot-draft";
    manifest.project.name = "RobotDraft";
    rws::ProjectResource source;
    source.id = "robot-source.main";
    source.kind = "robwork.passive-asset";
    source.path = "sources/robot.urdf";
    source.ownership = "project";
    source.required = false;
    rws::ProjectResource model;
    model.id = "robot-model.main";
    model.kind = "robwork.robot-model";
    model.path = "generated/robot-models/robot.rmb.json";
    model.ownership = "generated";
    model.required = false;
    model.dependencies = {source.id};
    manifest.resources = {source, model};
    manifest.entryPoints.insert("robotSource", source.id);
    QString error;
    rws::ProjectManager manager;
    REQUIRE(manager.createProject(projectPath, manifest, &error));
    manager.closeProject();

    const QString projectRoot = QFileInfo(projectPath).absolutePath();
    REQUIRE(QDir().mkpath(QFileInfo(QDir(projectRoot).filePath(source.path)).absolutePath()));
    QFile sourceFile(QDir(projectRoot).filePath(source.path));
    REQUIRE(sourceFile.open(QIODevice::WriteOnly));
    REQUIRE(sourceFile.write("<robot name=\"RobotDraft\"/>") > 0);
    sourceFile.close();
    REQUIRE(QDir().mkpath(QFileInfo(QDir(projectRoot).filePath(model.path)).absolutePath()));
    QFile modelFile(QDir(projectRoot).filePath(model.path));
    REQUIRE(modelFile.open(QIODevice::WriteOnly));
    REQUIRE(modelFile.write("{}") == 2);
    modelFile.close();

    rw::core::PropertyMap properties;
    rws::RobWorkStudio studio(properties);
    rws::CallbackProjectDocumentProvider modelProvider(
        "test.robot-model", "robwork.robot-model",
        [](const QString&, const rws::ProjectDocumentContext&, QString*) { return true; },
        [](const QString&, const rws::ProjectDocumentContext&, QString*) { return true; });
    REQUIRE(studio.registerProjectDocumentProvider(&modelProvider, &error));
    studio.openFile(projectPath.toStdString());
    REQUIRE(!studio.projectDirectory().isEmpty());
    REQUIRE(studio.mainWorkCellResourceId().isEmpty());
    rw::models::WorkCell::Ptr placeholder =
        rw::core::ownedPtr(new rw::models::WorkCell("RobotDraft"));
    rws::StructureOptimizerWidget widget;
    widget.setRobWorkStudio(&studio);
    widget.setScenarioContext(placeholder.get(), placeholder->getDefaultState());
    QPushButton* create = widget.findChild<QPushButton*>(
        "newStructureOptimizationProjectFromFrozenRequirementBannerButton");
    REQUIRE(create != nullptr);
    if (create != nullptr)
        create->click();
    REQUIRE(widget.statusText() == QStringLiteral(
        "The robot project has not generated its managed WorkCell. Review the model in "
        "RobotModelBuilder and run Save and Load first."));
    studio.close();
}

// S62：运行快照必须冻结输入，候选结果必须是独立的项目相对资源。
static void testOptimizationRunSnapshot()
{
    std::printf("testOptimizationRunSnapshot ... ");
    rws::StructureOptimizationProblem problem;
    problem.context.projectName = "snapshot";
    problem.context.robotName = "robot";
    problem.run.randomSeed = 42;
    problem.run.candidateCount = 7;

    rws::OptimizationRunSnapshot snapshot = rws::makeOptimizationRunSnapshot(
        "run-42", problem, "{}", "plan", "{}", "final", "model", "environment",
        "requirements", "tool", "adapters");
    REQUIRE(rws::optimizationRunSnapshotValid(snapshot));
    REQUIRE(snapshot.randomSeed == 42);
    REQUIRE(snapshot.requestedCandidateCount == 7);
    REQUIRE(snapshot.currentEnvelopeJson.find("baseDirectory") == std::string::npos);

    const std::string json = rws::optimizationRunSnapshotToJson(snapshot);
    rws::OptimizationRunSnapshot parsed;
    std::string error;
    REQUIRE(rws::optimizationRunSnapshotFromJson(json, parsed, &error));
    REQUIRE(parsed.randomSeed == 42);
    REQUIRE(parsed.input.modelFingerprint == "model");
    REQUIRE(!rws::optimizationRunSnapshotStatusFromString("Unknown", parsed.status, &error));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

// Phase 8/S81：验证重复运行的稳定候选身份、最佳候选门禁以及 DataInsufficient 保护。
static void testPhase8Acceptance()
{
    std::printf("testPhase8Acceptance ... ");
    rws::StructureOptimizationResult first;
    first.baselineCandidateIndex = 0;
    first.bestCandidateIndex = 1;
    first.diagnostics.generatedCandidates = 2;
    first.diagnostics.evaluatedCandidates = 2;
    rws::StructureCandidateResult baseline;
    baseline.index = 0;
    baseline.status = rws::StructureCandidateStatus::Infeasible;
    rws::StructureCandidateResult best;
    best.index = 1;
    best.status = rws::StructureCandidateStatus::Feasible;
    best.feasible = true;
    best.values = {0.5, 1.0};
    best.totalScore = 82.0;
    first.candidates = {baseline, best};
    const rws::Phase8AcceptanceResult valid = rws::Phase8Acceptance::validateResult(first);
    REQUIRE(valid.passed);

    const rws::Phase8AcceptanceResult repeated =
        rws::Phase8Acceptance::compareDeterministic(first, first);
    REQUIRE(repeated.passed);

    rws::StructureOptimizationResult changed = first;
    changed.candidates[1].values[0] = 0.51;
    const rws::Phase8AcceptanceResult mismatch =
        rws::Phase8Acceptance::compareDeterministic(first, changed);
    REQUIRE(!mismatch.passed);
    REQUIRE(mismatch.hasCode("Phase8.Determinism.CandidateMismatch"));

    rws::StructureOptimizationResult insufficient = first;
    insufficient.candidates[1].status = rws::StructureCandidateStatus::Pending;
    insufficient.candidates[1].feasible = true;
    const rws::Phase8AcceptanceResult rejected =
        rws::Phase8Acceptance::validateResult(insufficient);
    REQUIRE(!rejected.passed);
    REQUIRE(rejected.hasCode("Phase8.Candidate.FalseFeasible"));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

// Phase 8/S82：验证性能预算区分数据错误与预算超限警告，并计算缓存命中率门。
static void testPhase8PerformanceAudit()
{
    std::printf("testPhase8PerformanceAudit ... ");
    rws::StructureOptimizationResult result;
    result.diagnostics.generatedCandidates = 120;
    result.diagnostics.evaluatedCandidates = 100;
    result.diagnostics.cacheHits = 20;
    result.diagnostics.totalSeconds = 12.0;
    result.diagnostics.modelBuildSeconds = 2.0;
    result.diagnostics.kinematicEvaluationSeconds = 7.0;
    result.diagnostics.workspaceEvaluationSeconds = 1.0;
    rws::Phase8PerformanceBudget budget;
    budget.maxGeneratedCandidates = 100;
    budget.maxTotalSeconds = 10.0;
    budget.minimumCacheHitRate = 0.25;
    const rws::Phase8PerformanceAuditResult audited =
        rws::Phase8PerformanceAudit::audit(result, budget);
    REQUIRE(audited.valid);
    REQUIRE(!audited.withinBudget);
    REQUIRE(audited.hasCode("Phase8.Performance.CandidateBudgetExceeded"));
    REQUIRE(audited.hasCode("Phase8.Performance.TotalBudgetExceeded"));
    REQUIRE(audited.hasCode("Phase8.Performance.CacheHitRateLow"));

    result.diagnostics.cacheHits = 101;
    const rws::Phase8PerformanceAuditResult invalid =
        rws::Phase8PerformanceAudit::audit(result);
    REQUIRE(!invalid.valid);
    REQUIRE(invalid.hasCode("Phase8.Performance.DiagnosticsInvalid"));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

// Phase 8/S83：验证控制器状态互斥和结果临时路径审计。
static void testPhase8ResourceAudit()
{
    std::printf("testPhase8ResourceAudit ... ");
    rws::Phase8ControllerSnapshot valid;
    const rws::Phase8ResourceAuditResult validState =
        rws::Phase8ResourceAudit::auditController(valid);
    REQUIRE(validState.passed);

    rws::Phase8ControllerSnapshot invalid = valid;
    invalid.running = true;
    invalid.state = rws::OptimizationRunState::Idle;
    invalid.baselineRunning = true;
    const rws::Phase8ResourceAuditResult invalidState =
        rws::Phase8ResourceAudit::auditController(invalid);
    REQUIRE(!invalidState.passed);
    REQUIRE(invalidState.hasCode("Phase8.Controller.RunningStateMismatch"));
    REQUIRE(invalidState.hasCode("Phase8.Controller.ConcurrentRuns"));

    rws::StructureOptimizationResult result;
    rws::AnalysisWarning warning;
    warning.code = "StructureOptimization.Preview.OpenFailed";
    warning.message = "structure-optimizer-preview-abc";
    result.warnings.push_back(warning);
    const rws::Phase8ResourceAuditResult resource =
        rws::Phase8ResourceAudit::auditResult(result);
    REQUIRE(!resource.passed);
    REQUIRE(resource.hasCode("Phase8.Result.TemporaryPath"));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

// Phase 8/S84：验证发布清单字段完整、资源 ID 可移植且 JSON 序列化稳定。
static void testPhase8ReleaseManifest()
{
    std::printf("testPhase8ReleaseManifest ... ");
    rws::Phase8ReleaseManifest manifest;
    manifest.productVersion = "8.0.0";
    manifest.evaluatorId = "structure.kinematics";
    manifest.evaluatorVersion = "1";
    manifest.buildIdentifier = "robwork-ci-20260822";
    manifest.totalRunSeconds = 12.5;
    rws::Phase8ReleaseArtifact report;
    report.id = "report";
    report.projectResourceId = "structure-optimization/report.json";
    report.fingerprint = "report-fingerprint";
    report.sizeMegabytes = 0.25;
    rws::Phase8ReleaseArtifact model = report;
    model.id = "model";
    model.projectResourceId = "structure-optimization/candidate.main";
    model.fingerprint = "model-fingerprint";
    manifest.artifacts = {report, model};

    const rws::Phase8ReleaseAuditResult audited =
        rws::Phase8ReleaseManifestAudit::audit(manifest);
    REQUIRE(audited.passed);
    REQUIRE(audited.serializable);
    REQUIRE(audited.stableJson.find("{\"artifacts\"") == 0);

    rws::Phase8ReleaseManifest reordered = manifest;
    std::reverse(reordered.artifacts.begin(), reordered.artifacts.end());
    const rws::Phase8ReleaseAuditResult reorderedAudit =
        rws::Phase8ReleaseManifestAudit::audit(reordered);
    REQUIRE(reorderedAudit.passed);
    REQUIRE(reorderedAudit.stableJson == audited.stableJson);

    rws::Phase8ReleaseManifest invalid = manifest;
    invalid.artifacts.front().projectResourceId = "C:/temp/report.json";
    const rws::Phase8ReleaseAuditResult invalidPath =
        rws::Phase8ReleaseManifestAudit::audit(invalid);
    REQUIRE(!invalidPath.passed);
    REQUIRE(invalidPath.hasCode("Phase8.Release.ResourceIdNotRelative"));

    invalid = manifest;
    invalid.totalRunSeconds = std::numeric_limits<double>::quiet_NaN();
    const rws::Phase8ReleaseAuditResult invalidNumber =
        rws::Phase8ReleaseManifestAudit::audit(invalid);
    REQUIRE(!invalidNumber.passed);
    REQUIRE(invalidNumber.hasCode("Phase8.Release.NonFiniteNumber"));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

static void testOptimizationRunStore()
{
    std::printf("testOptimizationRunStore ... ");
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    rws::OptimizationRunStore store(temporary.path().toStdString());
    rws::StructureOptimizationProblem problem;
    problem.context.projectName = "store";
    problem.context.robotName = "robot";
    rws::OptimizationRunSnapshot snapshot = rws::makeOptimizationRunSnapshot(
        "store-run", problem, "{}", "plan", "{}", "final", "model", "environment",
        "requirements", "tool", "adapters");
    rws::CandidateResult candidate;
    candidate.candidateId = "candidate-a";
    rws::OptimizationRunResourceRef ref;
    std::string error;
    REQUIRE(store.publishCandidateResult(snapshot.runId, candidate, ref, &error));
    snapshot.candidateResults.push_back(ref);
    const bool saved = store.saveSnapshot(snapshot, &error);
    REQUIRE(saved);
    if (!saved) std::printf("(%s) ", error.c_str());
    rws::OptimizationRunSnapshot loaded;
    REQUIRE(store.loadSnapshot(snapshot.runId, loaded, &error));
    const rws::OptimizationResourceLoadResult resource = store.loadCandidateResult(ref);
    REQUIRE(resource.availability == rws::OptimizationResourceAvailability::Available);
    REQUIRE(resource.candidate.candidateId == "candidate-a");
    snapshot.status = rws::OptimizationRunSnapshotStatus::Completed;
    snapshot.completedAt = "2026-08-21T00:00:00Z";
    snapshot.generatedCandidateCount = snapshot.requestedCandidateCount;
    snapshot.completedCandidateCount = snapshot.requestedCandidateCount;
    REQUIRE(store.saveSnapshot(snapshot, &error));
    snapshot.randomSeed = 43;
    REQUIRE(!store.saveSnapshot(snapshot, &error));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

static void testStructureOptimizationWorkflowResolver()
{
    std::printf("testStructureOptimizationWorkflowResolver ... ");
    rws::StructureOptimizationWorkflowInputs inputs;
    inputs.projectOpen = true;
    inputs.binding.projectId = "project";
    inputs.binding.targetDevice = "robot";
    inputs.binding.tcpFrame = "TCP";
    inputs.binding.sceneResourceId = "scene";
    inputs.binding.modelResourceId = "model";
    inputs.binding.sourceKind = "managed";
    inputs.binding.sourceFingerprint = "binding-fp";
    inputs.currentProjectId = "project";
    inputs.currentModelFingerprint = "model-fp";
    inputs.currentSceneFingerprint = "scene-fp";
    inputs.currentEnvironmentFingerprint = "env-fp";
    inputs.currentRequirementFingerprint = "req-fp";
    inputs.currentKinematicValidationFingerprint = "kin-fp";
    inputs.persistedModelFingerprint = "model-fp";
    inputs.persistedSceneFingerprint = "scene-fp";
    inputs.persistedEnvironmentFingerprint = "env-fp";
    inputs.persistedRequirementFingerprint = "req-fp";
    inputs.persistedKinematicValidationFingerprint = "kin-fp";
    inputs.currentTcpFrame = "TCP";
    inputs.persistedTcpFrame = "TCP";
    inputs.currentEvaluatorVersion = "2";
    inputs.persistedEvaluatorVersion = "1";
    const rws::OptimizationRunPreconditions ready =
        rws::StructureOptimizationWorkflowResolver::resolve(inputs);
    REQUIRE(ready.canStart);
    REQUIRE(!ready.cacheReusable);
    REQUIRE(ready.historicalRunsReadable);
    REQUIRE(ready.staleCodes.contains("EvaluatorVersionChanged"));

    inputs.currentSceneFingerprint = "changed";
    const rws::OptimizationRunPreconditions stale =
        rws::StructureOptimizationWorkflowResolver::resolve(inputs);
    REQUIRE(!stale.canStart);
    REQUIRE(stale.blockingCodes.contains("SceneFingerprintMismatch"));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 模型完整性唯一入口:hasCompleteModel 是 Factory/ProjectAdapter/
// Template/Validation 共用的模型完整性判断。断言空 robotName、空
// transformJoints、合法完整模型三种路径,以及失败原因以稳定错误码为前缀。
static void testValidationHasCompleteModel()
{
    std::printf("testValidationHasCompleteModel ... ");

    rws::RobotModelSpec spec;

    std::string reason;
    REQUIRE(!rws::StructureOptimizationValidation::hasCompleteModel(spec, &reason));
    REQUIRE(reason ==
            "StructureOptimization.Context.Invalid: robotName must be non-empty.");

    spec.robotName = "HasNameRobot";
    REQUIRE(!rws::StructureOptimizationValidation::hasCompleteModel(spec, &reason));
    REQUIRE(reason ==
            "StructureOptimization.Context.Invalid: transformJoints must contain "
            "at least one joint.");

    rws::JointTransformSpec joint;
    joint.name = "base_slider";
    spec.transformJoints.push_back(joint);
    REQUIRE(rws::StructureOptimizationValidation::hasCompleteModel(spec, &reason));
    REQUIRE(reason.empty());
    REQUIRE(rws::StructureOptimizationValidation::hasCompleteModel(spec));

    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

static void testOptimizationPreflightCore()
{
    std::printf("testOptimizationPreflightCore ... ");
    rws::OptimizationPreflightInput input;
    input.hasModel = false;
    input.hasRequirements = false;
    input.hasKinematicValidation = false;
    const rws::OptimizationPreflightResult result = rws::OptimizationPreflight::run(input);
    REQUIRE(!result.canStart);
    REQUIRE(result.hasCode("MODEL_MISSING"));
    REQUIRE(result.hasCode("REQUIREMENT_MISSING"));
    REQUIRE(result.hasCode("KINEMATIC_VALIDATION_MISSING"));
    input.hasModel = input.hasRequirements = input.hasKinematicValidation = true;
    input.independentVariableCount = 1;
    const rws::OptimizationPreflightResult ready = rws::OptimizationPreflight::run(input);
    REQUIRE(ready.canStart);
    if (g_testFailures == 0) std::printf("PASSED\n");
    else std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 legacy/canonical 边界门禁:比较 currentEnvelopeToJson 的
// designSpace.bindings 投影与 LegacyDesignSpaceAdapter::preview 的语义绑定。
// 两者当前不是同一套绑定模型——JSON 迁移为每个旧变量写通用
// "structure.legacy-variable" 透传绑定;adapter 只为可语义化的变量
// (默认仅 BaseHeight)生成 typed binding,DH A/D 保持只读投影,其余判
// legacy/unbound。本测试把这一分歧固化为门禁:任一侧改变都会使断言失败,
// 强制重新审视"是否把 adapter 接线为唯一迁移入口"的决策。在两条路径
// 字段级等价之前,adapter 不得接入生产 JSON 迁移。
static void testLegacyAdapterJsonBindingDivergenceGate()
{
    std::printf("testLegacyAdapterJsonBindingDivergenceGate ... ");

    rws::StructureOptimizationProblem problem;
    rws::StructureDesignVariable baseHeight;
    baseHeight.id = "legacy-base-height";
    baseHeight.label = "Legacy base height";
    baseHeight.targetName = "Base";
    baseHeight.unit = "m";
    baseHeight.kind = rws::StructureVariableKind::BaseHeight;
    baseHeight.currentValue = 0.5;
    baseHeight.minimum = 0.3;
    baseHeight.maximum = 0.7;
    baseHeight.step = 0.01;
    rws::StructureDesignVariable jointX = baseHeight;
    jointX.id = "legacy-joint-x";
    jointX.targetName = "Link1";
    jointX.unit = "mm";
    jointX.kind = rws::StructureVariableKind::JointPositionX;
    jointX.currentValue = 500.0;
    jointX.minimum = 400.0;
    jointX.maximum = 700.0;
    jointX.step = 10.0;
    rws::StructureDesignVariable dhA = jointX;
    dhA.id = "legacy-dh-a";
    dhA.unit = "mm";
    dhA.kind = rws::StructureVariableKind::DhA;
    problem.variables = {baseHeight, jointX, dhA};

    // ---- JSON 迁移路径的 bindings 投影 ----
    const QJsonObject envelope = QJsonDocument::fromJson(
        QByteArray::fromStdString(
            rws::StructureOptimizationJson::currentEnvelopeToJson(problem)))
        .object();
    const QJsonArray jsonBindings =
        envelope.value("designSpace").toObject().value("bindings").toArray();

    // ---- adapter 语义迁移路径的 bindings 投影 ----
    const rws::LegacyDesignSpaceMigrationPreview preview =
        rws::LegacyDesignSpaceAdapter::preview(problem);

    // JSON 侧特征:全部变量都有透传绑定,adapterId 固定,id 等于变量 id,
    // 数值已转 SI(JointPositionX 500 mm -> 0.5 m)。
    REQUIRE(jsonBindings.size() == 3);
    for (const QJsonValue& value : jsonBindings) {
        REQUIRE(value.toObject().value("adapterId").toString() ==
                QStringLiteral("structure.legacy-variable"));
    }
    const QJsonObject jsonJointX = [&envelope]() {
        for (const QJsonValue& value :
             envelope.value("designSpace").toObject().value("variables").toArray())
            if (value.toObject().value("id").toString() == QStringLiteral("legacy-joint-x"))
                return value.toObject();
        return QJsonObject();
    }();
    REQUIRE(jsonJointX.value("unit").toString() == QStringLiteral("m"));
    REQUIRE(std::abs(jsonJointX.value("currentValue").toDouble() - 0.5) < 1e-12);

    // adapter 侧特征:仅 BaseHeight 被语义映射;DH 只读投影;其余 unbound。
    REQUIRE(preview.entries.size() == 3);
    REQUIRE(preview.bindings.size() == 1);
    REQUIRE(preview.mappedVariables.size() == 1);
    REQUIRE(preview.entries[0].mapped);
    REQUIRE(preview.entries[0].variable.semanticKind == rws::SemanticKind::BaseTz);
    REQUIRE(preview.entries[0].binding.ownerAdapterId == "BasePlacementAdapter");
    REQUIRE(preview.entries[0].variable.unit == rws::DesignVariableUnit::Metres);
    REQUIRE(preview.entries[1].disposition == "legacy/unbound");
    REQUIRE(preview.entries[2].disposition == "legacy/projection-only");

    // 非法单位的分歧特征:JSON 迁移直接抛 std::invalid_argument 拒绝导出,
    // adapter 则记 LEGACY_VARIABLE_UNIT_UNSUPPORTED 诊断并保持变量 unbound。
    // (单位检查只发生在已语义映射的变量上,因此用 BaseHeight 类别触发。)
    rws::StructureDesignVariable badUnit = baseHeight;
    badUnit.id = "legacy-bad-unit";
    badUnit.unit = "furlong";
    rws::StructureOptimizationProblem badUnitProblem;
    badUnitProblem.context = problem.context;
    badUnitProblem.variables = {badUnit};
    bool jsonRejectedBadUnit = false;
    try {
        rws::StructureOptimizationJson::currentEnvelopeToJson(badUnitProblem);
    } catch (const std::invalid_argument& error) {
        jsonRejectedBadUnit =
            std::string(error.what()).find("furlong") != std::string::npos;
    }
    REQUIRE(jsonRejectedBadUnit);
    const rws::LegacyDesignSpaceMigrationPreview badUnitPreview =
        rws::LegacyDesignSpaceAdapter::preview(badUnitProblem);
    REQUIRE(badUnitPreview.entries.size() == 1);
    REQUIRE(!badUnitPreview.entries[0].mapped);
    REQUIRE(badUnitPreview.entries[0].disposition == "legacy/unbound");
    REQUIRE(std::any_of(badUnitPreview.diagnostics.begin(),
                        badUnitPreview.diagnostics.end(),
                        [](const rws::StructureOptimizationDiagnostic& diagnostic) {
                            return diagnostic.code == "LEGACY_VARIABLE_UNIT_UNSUPPORTED";
                        }));

    // 第二个分歧特征:JSON 出口把 DhA 当长度量纲,拒绝 DhA+deg(自然旧单位组合);
    // adapter 侧 DhA/DhD 根本不参与绑定(只读投影),不存在单位问题。
    rws::StructureDesignVariable dhDeg = dhA;
    dhDeg.id = "legacy-dh-deg";
    dhDeg.unit = "deg";
    rws::StructureOptimizationProblem dhDegProblem;
    dhDegProblem.context = problem.context;
    dhDegProblem.variables = {dhDeg};
    bool jsonRejectedDhDeg = false;
    try {
        rws::StructureOptimizationJson::currentEnvelopeToJson(dhDegProblem);
    } catch (const std::invalid_argument&) {
        jsonRejectedDhDeg = true;
    }
    REQUIRE(jsonRejectedDhDeg);
    const rws::LegacyDesignSpaceMigrationPreview dhDegPreview =
        rws::LegacyDesignSpaceAdapter::preview(dhDegProblem);
    REQUIRE(dhDegPreview.entries.size() == 1);
    REQUIRE(!dhDegPreview.entries[0].mapped);
    REQUIRE(dhDegPreview.entries[0].disposition == "legacy/projection-only");

    // 分歧门禁:两条路径的绑定集合不等价(数量与 adapter 语义都不同)。
    // 等价性成立时此断言会失败,届时必须重新执行迁移接线决策。
    REQUIRE(jsonBindings.size() != static_cast<int>(preview.bindings.size()));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// S61：旧文档只能迁入当前权威 Envelope。迁移不回写输入，也不能让未绑定变量
// 在迁移过程中被静默启用；legacy 字段仅作为审计扩展保留。
static void testLegacyJsonMigration()
{
    std::printf("testLegacyJsonMigration ... ");

    rws::StructureOptimizationProblem legacyProblem;
    legacyProblem.context.projectName = "legacy";
    legacyProblem.context.robotName = "robot";
    rws::StructureDesignVariable legacyVariable;
    legacyVariable.id = "dh-a";
    legacyVariable.label = "DH A";
    legacyVariable.unit = "mm";
    legacyVariable.kind = rws::StructureVariableKind::DhA;
    legacyVariable.minimum = 100.0;
    legacyVariable.maximum = 300.0;
    legacyVariable.step = 10.0;
    legacyProblem.variables.push_back(legacyVariable);
    rws::StructureConstraint legacyConstraint;
    legacyConstraint.id = "must";
    legacyProblem.constraints.push_back(legacyConstraint);
    QJsonDocument legacyDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(rws::StructureOptimizationJson::problemToJson(legacyProblem)));
    QJsonObject legacyObject = legacyDocument.object();
    legacyObject.insert(QStringLiteral("thirdParty"), QJsonObject{{QStringLiteral("audit"),
                                                                   QStringLiteral("retain")}});
    const std::string legacy = QJsonDocument(legacyObject).toJson(QJsonDocument::Compact).toStdString();

    rws::StructureOptimizationMigrationResult migrated;
    std::string error;
    const bool migratedOk = rws::StructureOptimizationMigration::migrate(legacy, migrated, &error);
    REQUIRE(migratedOk);
    if (!migratedOk) {
        std::printf("(%s)\n", error.c_str());
        return;
    }
    REQUIRE(migrated.source == rws::StructureOptimizationMigrationSource::Legacy);
    REQUIRE(migrated.dirty);
    REQUIRE(!migrated.currentJson.empty());
    REQUIRE(migrated.problem.variables.size() == 1);
    REQUIRE(!migrated.problem.variables.front().enabled);
    const QJsonObject migratedRoot = QJsonDocument::fromJson(
        QByteArray::fromStdString(migrated.currentJson)).object();
    REQUIRE(migratedRoot.value("designSpace").toObject().value("variables").toArray()
                .at(0).toObject().value("unit").toString() == "m");
    REQUIRE(migrated.problem.extensions.contains("legacy"));

    rws::StructureOptimizationMigrationResult repeated;
    REQUIRE(rws::StructureOptimizationMigration::migrate(migrated.currentJson, repeated, &error));
    REQUIRE(repeated.source == rws::StructureOptimizationMigrationSource::Current);
    REQUIRE(!repeated.dirty);
    REQUIRE(repeated.currentJson == migrated.currentJson);

    rws::StructureOptimizationMigrationResult malformed;
    REQUIRE(!rws::StructureOptimizationMigration::migrate("{\"schemaVersion\":2}", malformed,
                                                           &error));
    REQUIRE(!error.empty());

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 异步控制器状态:用假任务循环驱动 StructureOptimizationController,验证
// running/paused/completed 信号时序、暂停期间进度不再推进、恢复后继续推进、
// 取消后 completed 携带 canceled 且控制器退出运行态。
// 项目关闭/切换回归:先构造带优化变量、任务与约束的旧问题并开启项目文档,
// 再调用 clearProjectDocumentContext(),校验任务与约束全部清空且脏标志复位,
// 确保新项目不会继承旧项目的优化会话。
static void testWidgetProjectCloseClearsOptimizationSession()
{
    rws::StructureOptimizerWidget widget;
    rws::StructureOptimizationProblem problem;
    problem.context.projectName = "old-project";
    problem.variables.push_back({"joint1_x", "Joint 1 X", "joint1", "m",
                                 rws::StructureVariableKind::JointPositionX,
                                 0.0, -1.0, 1.0, 0.1});
    rws::OptimizationTaskPoint task;
    task.point.id = "old-task";
    task.point.name = "Old task";
    problem.tasks.push_back(task);
    rws::StructureConstraint constraint;
    constraint.id = "old-constraint";
    problem.constraints.push_back(constraint);
    widget.setProblem(problem);
    widget.beginGeneratedProjectDocument(QStringLiteral("old/optimizations/main.json"));
    REQUIRE(widget.collectProblem().tasks.size() == 1);
    REQUIRE(widget.collectProblem().constraints.size() == 1);

    widget.clearProjectDocumentContext();

    const rws::StructureOptimizationProblem cleared = widget.collectProblem();
    REQUIRE(cleared.tasks.empty());
    REQUIRE(cleared.constraints.empty());
    REQUIRE(!widget.isProjectDocumentDirty());
}

static void testStructureOptimizationControllerAsyncState()
{
    std::printf("testStructureOptimizationControllerAsyncState ... ");

    struct SharedState {
        int progressCount = 0;
        bool canceled = false;
    };
    std::shared_ptr<SharedState> shared(new SharedState());

    rws::StructureOptimizationController controller(
        [shared](const rws::StructureOptimizationProblem&,
                 const rws::StructureOptimizationCallbacks& callbacks) {
            rws::StructureOptimizationResult result;
            for (int i = 0; i < 200; ++i) {
                if (callbacks.isCancellationRequested &&
                    callbacks.isCancellationRequested()) {
                    result.canceled = true;
                    shared->canceled = true;
                    return result;
                }
                if (callbacks.waitIfPaused)
                    callbacks.waitIfPaused();
                rws::StructureProgress progress;
                progress.stage = "Fake";
                progress.completed = i + 1;
                progress.planned = 200;
                progress.bestScore = static_cast<double>(i);
                if (callbacks.onProgress)
                    callbacks.onProgress(progress);
                ++shared->progressCount;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return result;
        });

    bool sawRunning = false;
    bool sawPaused = false;
    bool sawCompleted = false;
    bool completedCanceled = false;

    QObject::connect(&controller, &rws::StructureOptimizationController::runningChanged,
                     [&](bool running) { if (running) sawRunning = true; });
    QObject::connect(&controller, &rws::StructureOptimizationController::pausedChanged,
                     [&](bool paused) { if (paused) sawPaused = true; });
    QObject::connect(&controller, &rws::StructureOptimizationController::completed,
                     [&](const rws::StructureOptimizationResult& result) {
                         sawCompleted = true;
                         completedCanceled = result.canceled;
                     });

    rws::StructureOptimizationProblem problem;
    problem.run.candidateCount = 200;
    REQUIRE(controller.start(problem));
    REQUIRE(sawRunning);
    REQUIRE(controller.runState() == rws::OptimizationRunState::Running);

    QEventLoop waitForProgress;
    QTimer::singleShot(80, &waitForProgress, SLOT(quit()));
    waitForProgress.exec();
    controller.pause();
    REQUIRE(sawPaused);
    REQUIRE(controller.runState() == rws::OptimizationRunState::Paused);
    const int pausedCount = shared->progressCount;

    QEventLoop pausedLoop;
    QTimer::singleShot(80, &pausedLoop, SLOT(quit()));
    pausedLoop.exec();
    REQUIRE(shared->progressCount <= pausedCount + 1);

    controller.resume();
    REQUIRE(controller.runState() == rws::OptimizationRunState::Running);
    QEventLoop resumedLoop;
    QTimer::singleShot(80, &resumedLoop, SLOT(quit()));
    resumedLoop.exec();
    REQUIRE(shared->progressCount > pausedCount);

    controller.cancel();
    REQUIRE(controller.runState() == rws::OptimizationRunState::CancelRequested);
    controller.cancel(); // 项目关闭与用户取消同时到达时必须安全幂等。
    QEventLoop finishedLoop;
    QObject::connect(&controller, &rws::StructureOptimizationController::completed,
                     &finishedLoop, [&finishedLoop](const rws::StructureOptimizationResult&) {
                         finishedLoop.quit();
                     });
    QTimer::singleShot(5000, &finishedLoop, SLOT(quit()));
    if (!sawCompleted)
        finishedLoop.exec();

    REQUIRE(sawCompleted);
    REQUIRE(completedCanceled);
    REQUIRE(!controller.isRunning());
    REQUIRE(controller.runState() == rws::OptimizationRunState::Completed);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  main
// =============================================================================

int main(int argc, char** argv)
{
    // 测试程序可能在底层 XML/WorkCell 加载器触发运行库终止；关闭 stdout 缓冲，
    // 使 CI 和本地调试日志始终保留最后一个已进入的测试用例名称，避免 abort() 将
    // 缓冲区中的诊断信息一并丢失。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::printf("=== StructureOptimizer Test Suite ===\n\n");
    std::fflush(stdout);

    const std::string suite = argc > 1 ? argv[1] : std::string();

    if (suite == "abi") {
        QCoreApplication app(argc, argv);
        testHistoricalStructureOptimizerAbiRemainsLinkable();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "contracts") {
        QCoreApplication app(argc, argv);
        testOptimizationResultContract();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "kinematic_conventions") {
        QCoreApplication app(argc, argv);
        testKinematicConventions();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "canonical_model") {
        QCoreApplication app(argc, argv);
        testCanonicalKinematicModelValidation();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "canonical_fk") {
        QCoreApplication app(argc, argv);
        testCanonicalForwardKinematics();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "canonical_importer") {
        QCoreApplication app(argc, argv);
        testKinematicModelImporter();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "dh_projection") {
        QCoreApplication app(argc, argv);
        testDhProjection();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "kinematic_fingerprint") {
        QCoreApplication app(argc, argv);
        testKinematicFingerprint();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "canonical_shadow") {
        QCoreApplication app(argc, argv);
        testCanonicalModelShadow();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "design_variable") {
        QCoreApplication app(argc, argv);
        testTypedDesignVariableAndBinding();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "design_registry") {
        QCoreApplication app(argc, argv);
        testDesignSpaceRegistryCapabilities();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "parameterization") {
        QCoreApplication app(argc, argv);
        testParameterizationAndWriteSetValidation();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "derived_expression") {
        QCoreApplication app(argc, argv);
        testDerivedExpressionsAndDependencyGraph();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "design_space_compiler") {
        QCoreApplication app(argc, argv);
        testDesignSpaceCompiler();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "design_vector") {
        QCoreApplication app(argc, argv);
        testDesignVector();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "legacy_design_space") {
        QCoreApplication app(argc, argv);
        testLegacyDesignSpaceMigrationPreview();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "adapter_registry") {
        QCoreApplication app(argc, argv);
        testAdapterRegistryAndCandidatePatch();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "joint_origin_link_adapter") {
        QCoreApplication app(argc, argv);
        testJointOriginAndParameterizedLinkAdapters();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "joint_axis_adapter") {
        QCoreApplication app(argc, argv);
        testJointAxisAdapter();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "joint_zero_limit_adapter") {
        QCoreApplication app(argc, argv);
        testJointZeroAndLimitAdapters();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "base_flange_tcp_adapter") {
        QCoreApplication app(argc, argv);
        testBaseFlangeAndTcpAdapters();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "parameterized_geometry_collision_adapter") {
        QCoreApplication app(argc, argv);
        testParameterizedGeometryAndCollisionAdapters();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "candidate_patch_merge_apply") {
        QCoreApplication app(argc, argv);
        testCandidatePatchMergeAndApply();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "candidate_compiler") {
        QCoreApplication app(argc, argv);
        testCandidateCompiler();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "s38_projection") {
        QCoreApplication app(argc, argv);
        testS38ProjectionAndEvaluationDevice();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "canonical_baseline_bridge") {
        QCoreApplication app(argc, argv);
    testCanonicalBaselineEvaluationBridge();
    testStructureOptimizationControllerBaselineBridge();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "baseline_shadow_rebuild") {
        QCoreApplication app(argc, argv);
    testBaselineRebuildsMissingCanonicalShadow();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "evaluation_plan") {
        QCoreApplication app(argc, argv);
        testEvaluationPlanCompiler();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "evaluation_pipeline") {
        QCoreApplication app(argc, argv);
        testEvaluationPipelineAndMetricRegistry();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "constraint_objective") {
        QCoreApplication app(argc, argv);
        testConstraintObjectiveAggregation();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "task_evaluation_stage") {
        QCoreApplication app(argc, argv);
        testTaskEvaluationStage();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "candidate_result") {
        QCoreApplication app(argc, argv);
        testCandidateResultAssembly();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "optimization_checkpoint") {
        QCoreApplication app(argc, argv);
        testOptimizationCheckpoint();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "controller_state") {
        QCoreApplication app(argc, argv);
        testStructureOptimizationControllerAsyncState();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "json_safety") {
        QCoreApplication app(argc, argv);
        testJsonSafetyContract();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "json_roundtrip") {
        QCoreApplication app(argc, argv);
        testJsonRoundTrip();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "current_json_envelope") {
        QCoreApplication app(argc, argv);
        testCurrentJsonEnvelope();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "legacy_json_migration") {
        QCoreApplication app(argc, argv);
        testLegacyAdapterJsonBindingDivergenceGate();
        testLegacyJsonMigration();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "run_snapshot") {
        QCoreApplication app(argc, argv);
        testOptimizationRunSnapshot();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "run_store") {
        QCoreApplication app(argc, argv);
        testOptimizationRunStore();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "workflow_resolver") {
        QCoreApplication app(argc, argv);
        testStructureOptimizationWorkflowResolver();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "preflight_core") {
        QCoreApplication app(argc, argv);
        testValidationHasCompleteModel();
        testOptimizationPreflightCore();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "phase8_acceptance") {
        QCoreApplication app(argc, argv);
        testPhase8Acceptance();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "phase8_performance") {
        QCoreApplication app(argc, argv);
        testPhase8PerformanceAudit();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "phase8_resource") {
        QCoreApplication app(argc, argv);
        testPhase8ResourceAudit();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "phase8_manifest") {
        QCoreApplication app(argc, argv);
        testPhase8ReleaseManifest();
        return g_testFailures == 0 ? 0 : 1;
    }

    // Phase 8/S86：发布门聚合四个纯核心审计，便于本地和 CI 一次收集完整证据。
    if (suite == "phase8_release_gate") {
        QCoreApplication app(argc, argv);
        testPhase8Acceptance();
        testPhase8PerformanceAudit();
        testPhase8ResourceAudit();
        testPhase8ReleaseManifest();
        if (g_testFailures == 0) {
            std::printf("Phase 8 release gate passed.\n");
            return 0;
        }
        std::printf("Phase 8 release gate FAILED (%d test(s)).\n", g_testFailures);
        return 1;
    }

    // Phase 6/S65：集成门把持久化、迁移、快照、失效和启动前置检查
    // 放在同一进程中回归，确保各边界契约可以连续协作且不会互相污染。
    if (suite == "phase6_integration") {
        QCoreApplication app(argc, argv);
        testCurrentJsonEnvelope();
        testLegacyJsonMigration();
        testOptimizationRunSnapshot();
        testOptimizationRunStore();
        testStructureOptimizationWorkflowResolver();
        testOptimizationPreflightCore();
        testProjectFactoryProvenance();
        if (g_testFailures == 0) {
            std::printf("Phase 6 integration gate passed.\n");
            return 0;
        }
        std::printf("Phase 6 integration gate FAILED (%d test(s)).\n", g_testFailures);
        return 1;
    }

    if (suite == "frozen_adapter") {
        QCoreApplication app(argc, argv);
        testFrozenEngineeringRequirementArtifactAdapter();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "variable_table") {
        QCoreApplication app(argc, argv);
        testStructureVariableTableDisplayRoles();
        testStructureVariableFilterProxy();
        testStructureVariableAdvancedColumns();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "variable_actions") {
        QApplication app(argc, argv);
        testStructureVariableTableActions();
        testStructureVariableTableBoundaries();
        return g_testFailures == 0 ? 0 : 1;
    }

    // Phase 7/S75 独立 GUI 预览门：只启动一次宿主模拟，验证清理/恢复幂等性。
    if (suite == "preview") {
        QApplication app(argc, argv);
        testCandidatePreviewController();
        return g_testFailures == 0 ? 0 : 1;
    }

    // Phase 7/S76 报告门：单独覆盖文本报告、CSV 和审计输出，避免 GUI 全套件阻塞。
    if (suite == "report") {
        QCoreApplication app(argc, argv);
        testCsvExport();
        testAuditableEvidenceOutput();
        testExportService();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "accepted_ur") {
        QCoreApplication app(argc, argv);
        testAcceptedUr6585AProject();
        if (g_testFailures == 0) {
            std::printf("Accepted UR project test passed.\n");
            return 0;
        }
        std::printf("Accepted UR project test FAILED.\n");
        return 1;
    }

    if (suite == "phase1_core") {
        QCoreApplication app(argc, argv);
        testPhaseOneTemplatesAndPreflight();
        testPhaseOneCandidateComparison();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "robot_file_acceptance") {
        QApplication app(argc, argv);
        testPortable300kgRobotFileProjectAcceptance();
        if (g_testFailures == 0) {
            std::printf("Robot file acceptance test passed.\n");
            return 0;
        }
        std::printf("Robot file acceptance test FAILED.\n");
        return 1;
    }

    if (suite == "resource_dependencies") {
        QApplication app(argc, argv);
        testStructureOptimizerResourceDependencies();
        if (g_testFailures == 0) {
            std::printf("Resource dependency test passed.\n");
            return 0;
        }
        std::printf("Resource dependency test FAILED.\n");
        return 1;
    }

    if (suite == "project_context") {
        QCoreApplication app(argc, argv);
        testProjectAdapterRestoresManagedScenarioRoot();
        if (g_testFailures == 0) {
            std::printf("Project context test passed.\n");
            return 0;
        }
        std::printf("Project context test FAILED.\n");
        return 1;
    }

    // 独立运行开关:仅执行"项目关闭清空优化会话"回归,便于快速验证而无需跑整个 widget 套件。
    if (suite == "project_close") {
        QApplication app(argc, argv);
        testWidgetProjectCloseClearsOptimizationSession();
        if (g_testFailures == 0) {
            std::printf("Project close test passed.\n");
            return 0;
        }
        std::printf("Project close test FAILED.\n");
        return 1;
    }

    if (suite == "widget") {
        QApplication app(argc, argv);
        testStructureOptimizerWidgetState();
        testStructureOptimizerModelStatusGuidance();
        testStructureOptimizerWidgetUsesEnglishCopy();
        testStructureOptimizerWidgetVariableEfficiencyControls();
        testStructureOptimizerWidgetPhaseOneControls();
        std::fflush(stdout);
        if (g_testFailures == 0)
        {
            std::printf("All tests passed.\n");
            return 0;
        }
        std::printf("%d test(s) FAILED.\n", g_testFailures);
        return 1;
    }

    if (suite == "managed_project_gate") {
        QApplication app(argc, argv);
        testManagedRobotProjectRequiresPublishedWorkCell();
        if (g_testFailures == 0) {
            std::printf("Managed project gate test passed.\n");
            return 0;
        }
        std::printf("Managed project gate test FAILED.\n");
        return 1;
    }

    if (suite == "copy") {
        QApplication app(argc, argv);
        testStructureOptimizerWidgetUsesEnglishCopy();
        return g_testFailures == 0 ? 0 : 1;
    }

    QApplication app(argc, argv);

    testHistoricalStructureOptimizerAbiRemainsLinkable();

    if (suite == "model_staleness") {
        testProjectFactoryProvenance();
        if (g_testFailures == 0) {
            std::printf("Model staleness tests passed.\n");
            return 0;
        }
        std::printf("Model staleness tests FAILED.\n");
        return 1;
    }

    if (suite == "model_factory") {
        testModelFactory();
        if (g_testFailures == 0) {
            std::printf("Model factory test passed.\n");
            return 0;
        }
        std::printf("Model factory test FAILED.\n");
        return 1;
    }

    if (suite == "frozen_requirements") {
        testFrozenEngineeringRequirementArtifactAdapter();
        testFrozenRequirementProjectImportCreatesAuditableProblem();
        testManagedFrozenRequirementImportUsesExplicitProjectRoot();
        if (g_testFailures == 0) {
            std::printf("All frozen requirement tests passed.\n");
            return 0;
        }
        std::printf("%d frozen requirement test(s) FAILED.\n", g_testFailures);
        return 1;
    }

    if (suite == "evaluator_consistency") {
        testEvaluateCandidateMatchesLegacyWrapper();
        testSharedTargetEvaluatorConsistency();
        testVerifiedRegionUsesSharedEvaluator();
        testVerifiedRegionPreservesPositionCoverage();
        if (g_testFailures == 0) {
            std::printf("Evaluator consistency test passed.\n");
            return 0;
        }
        std::printf("Evaluator consistency test FAILED.\n");
        return 1;
    }

    if (suite == "cache") {
        testCache();
        if (g_testFailures == 0) {
            std::printf("Cache test passed.\n");
            return 0;
        }
        std::printf("Cache test FAILED.\n");
        return 1;
    }

    if (suite == "ui") {
        testUiTableModelsAndSuggestions();
        testStructureVariableTableDisplayRoles();
        testStructureVariableTableActions();
        testStructureVariableTableBoundaries();
        testConstraintModelAndProjectAdapter();
        testProjectFactory();
        testProjectFactoryProvenance();
        testExportService();
        testAcceptedUr6585AProject();
        testCandidatePreviewController();
        std::fflush(stdout);
        testStructureOptimizationControllerAsyncState();
        std::fflush(stdout);

        if (g_testFailures == 0)
        {
            std::printf("All tests passed.\n");
            return 0;
        }
        std::printf("%d test(s) FAILED.\n", g_testFailures);
        return 1;
    }

    testProblemDefaultsAndValidation();

    printf("\n");

    testGenerator();

    printf("\n");

    testCache();
    testMutator();
    testScorer();
    testScorerWithConstraints();
    testGenericObjectivesAndConstraints();
    testHardConstraints();

    printf("\n");

    testModelFactory();

    printf("\n");

    testEvaluator();
    testWorkspaceCoverage();
    testWorkspaceCoverageEvaluator();
    testTcpBareNameFallback();
    testSharedTargetEvaluatorConsistency();
    testVerifiedRegionUsesSharedEvaluator();
    testVerifiedRegionPreservesPositionCoverage();
    testWorkspaceCoverageDataInsufficient();
    testWorkspaceCoverageCancellation();
    testEngineeringEvaluatorPipeline();
    testSystemEngineeringOptimizer();
    testOptimizer();
    testHybridVerificationAndSensitivityWorkflow();
    testNoFeasibleCandidateLeavesSensitivityUnknown();

    printf("\n");

    testSensitivity();
    testSensitivityStopsAfterCancellation();
    testJsonRoundTrip();
    testAuditableEvidenceOutput();
    testCsvExport();
    testUiTableModelsAndSuggestions();
    testStructureVariableTableDisplayRoles();
    testStructureVariableTableActions();
    testStructureVariableTableBoundaries();
    testStructureOptimizerWidgetUsesEnglishCopy();
    testConstraintModelAndProjectAdapter();
    testFrozenEngineeringRequirementArtifactAdapter();
    testFrozenRequirementProjectImportCreatesAuditableProblem();
    testManagedFrozenRequirementImportUsesExplicitProjectRoot();
    testProjectFactory();
    testProjectFactoryProvenance();
    testExportService();
    testAcceptedUr6585AProject();
    testCandidatePreviewController();
    testStructureOptimizationControllerAsyncState();

    std::printf("\n");

    if (g_testFailures == 0)
    {
        std::printf("All tests passed.\n");
        return 0;
    }
    else
    {
        std::printf("%d test(s) FAILED.\n", g_testFailures);
        return 1;
    }
}
