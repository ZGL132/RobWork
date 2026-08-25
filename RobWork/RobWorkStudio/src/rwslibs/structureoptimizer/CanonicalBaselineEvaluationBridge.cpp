#include "CanonicalBaselineEvaluationBridge.hpp"

#include "CandidateCompiler.hpp"
#include "KinematicFingerprint.hpp"
#include "KinematicBaselineSnapshot.hpp"
#include "TaskEvaluationStage.hpp"
#include "VerifiedRegionStage.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>

#include <algorithm>

namespace rws {
namespace {

void addError(BaselineEvaluationResult& result, const std::string& code,
              const std::string& fieldPath, const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "canonical-baseline";
    diagnostic.stage = "baseline-verified";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

void appendPlanDiagnostics(BaselineEvaluationResult& result, const EvaluationPlan& plan)
{
    for (const EvaluationPlanDiagnostic& planDiagnostic : plan.diagnostics) {
        StructureOptimizationDiagnostic diagnostic;
        diagnostic.code = planDiagnostic.code;
        diagnostic.severity = planDiagnostic.blocking ? "Error" : "Warning";
        diagnostic.subsystem = "canonical-baseline";
        diagnostic.stage = "evaluation-plan";
        diagnostic.fieldPath = planDiagnostic.field;
        diagnostic.message = planDiagnostic.message;
        result.diagnostics.push_back(std::move(diagnostic));
    }
}

CandidateResult failedCandidate(const std::string& diagnostic)
{
    CandidateAssemblyInput input;
    input.compileSucceeded = false;
    input.compileDiagnostic = diagnostic;
    input.evidenceStage = AnalysisEvidenceStage::Verified;
    return CandidateResultAssembler::assemble(input);
}

Feasibility combine(Feasibility left, Feasibility right)
{
    if (left == Feasibility::DataInsufficient || right == Feasibility::DataInsufficient)
        return Feasibility::DataInsufficient;
    if (left == Feasibility::Infeasible || right == Feasibility::Infeasible)
        return Feasibility::Infeasible;
    if (left == Feasibility::Feasible || right == Feasibility::Feasible)
        return Feasibility::Feasible;
    return Feasibility::NotEvaluated;
}

bool requiresCollision(const EvaluationPlan& plan)
{
    return std::any_of(plan.tasks.begin(), plan.tasks.end(), [](const EvaluationPlanTask& task) {
               return task.source.collisionFreeRequired;
           }) ||
           std::any_of(plan.regions.begin(), plan.regions.end(), [](const EvaluationPlanRegion& region) {
               return region.source.collisionFreeRequired;
           });
}

}    // namespace

BaselineEvaluationResult CanonicalBaselineEvaluationBridge::evaluate(
    const CanonicalBaselineEvaluationRequest& request)
{
    BaselineEvaluationResult result;
    if (request.problem == nullptr) {
        addError(result, "S52_PROBLEM_REQUIRED", "problem",
                 "Canonical baseline evaluation requires an immutable problem snapshot.");
        result.candidateResult = failedCandidate(result.diagnostics.back().code);
        return result;
    }
    const StructureOptimizationProblem& problem = *request.problem;
    if (problem.canonicalModelShadow.status != CanonicalModelShadowStatus::Current ||
        !problem.canonicalModelShadow.hasSnapshot()) {
        addError(result, "S52_CANONICAL_BASELINE_UNAVAILABLE", "canonicalModelShadow",
                 "Baseline Verified requires a current canonical model shadow.");
        result.candidateResult = failedCandidate(result.diagnostics.back().code);
        return result;
    }
    if (request.cancellation.cancellationRequested()) {
        CandidateAssemblyInput input;
        input.evaluationSucceeded = false;
        input.evidenceStage = AnalysisEvidenceStage::Verified;
        input.completion.canceled = true;
        input.evaluationDiagnostic = "S52_BASELINE_CANCELED";
        result.candidateResult = CandidateResultAssembler::assemble(input);
        return result;
    }

    const KinematicBaselineSnapshot& baseline = *problem.canonicalModelShadow.snapshot;
    const DesignSpaceRegistry defaultRegistry = DesignSpaceRegistry::firstPhase();
    const AdapterRegistry defaultAdapters;
    const AdapterCapabilityQuery defaultCapabilities;
    const DesignSpaceCompileResult space = DesignSpaceCompiler::compile(
        {&baseline.model,
         request.designSpaceRegistry != nullptr ? request.designSpaceRegistry : &defaultRegistry,
         request.adapterCapabilities != nullptr ? request.adapterCapabilities : &defaultCapabilities,
         request.adapterRegistry != nullptr ? request.adapterRegistry : &defaultAdapters,
         request.variables,
         request.bindings,
         request.parameterizationSelections,
         request.derivedExpressions});
    result.diagnostics.insert(result.diagnostics.end(), space.diagnostics.begin(), space.diagnostics.end());
    if (!space.ok) {
        result.candidateResult = failedCandidate("S52_DESIGN_SPACE_COMPILE_FAILED");
        return result;
    }

    std::vector< EngineeringDesignValue > nominalValues;
    nominalValues.reserve(space.designSpace.independentVariables.size());
    for (const DesignVariableDefinition& variable : space.designSpace.independentVariables)
        nominalValues.push_back(
            {variable.id, variable.unit, variable.nominalValue, std::string()});
    const DesignVectorResult vector =
        DesignVectorCodec::fromEngineering(space.designSpace, nominalValues);
    result.diagnostics.insert(result.diagnostics.end(), vector.diagnostics.begin(), vector.diagnostics.end());
    if (!vector.ok) {
        result.candidateResult = failedCandidate("S52_NOMINAL_VECTOR_INVALID");
        return result;
    }
    result.designVector = vector.vector;

    const CandidateCompileResult compiled = CandidateCompiler::compile(
        {&baseline.model,
         &space.designSpace,
         &result.designVector,
         request.adapterRegistry != nullptr ? request.adapterRegistry : &defaultAdapters,
         request.adapterCapabilities != nullptr ? request.adapterCapabilities : &defaultCapabilities});
    result.diagnostics.insert(result.diagnostics.end(), compiled.diagnostics.begin(),
                              compiled.diagnostics.end());
    if (!compiled.ok) {
        result.candidateResult = failedCandidate("S52_BASELINE_CANDIDATE_COMPILE_FAILED");
        return result;
    }
    result.candidateFingerprint = compiled.candidate.fingerprint;

    const KinematicFingerprintResult modelFingerprint =
        KinematicFingerprint::forModel(compiled.candidate.kinematicModel);
    const KinematicFingerprintResult environmentFingerprint =
        KinematicFingerprint::forEnvironment(compiled.candidate.kinematicModel);
    const KinematicFingerprintResult toolFingerprint =
        KinematicFingerprint::forTool(compiled.candidate.kinematicModel);
    if (!modelFingerprint.ok || !environmentFingerprint.ok || !toolFingerprint.ok) {
        addError(result, "S52_CANDIDATE_FINGERPRINT_FAILED", "candidate",
                 "The compiled baseline candidate could not be fingerprinted.");
        result.candidateResult = failedCandidate(result.diagnostics.back().code);
        return result;
    }
    result.modelFingerprint = modelFingerprint.value;
    result.environmentFingerprint = environmentFingerprint.value;
    result.toolFingerprint = toolFingerprint.value;

    EvaluationPlanCompilerOptions options = request.planOptions;
    if (request.checkCollision)
        options.capabilities.insert("collision");
    // The execution contract records the frozen RobotModelSpec/WorkCell
    // identities (SHA-256).  The candidate fingerprints above are FNV hashes
    // of the compiled canonical model and must remain a separate audit stream.
    // Comparing either pair directly rejects an unchanged project merely
    // because it crosses the source-model -> canonical-model boundary.
    const RequirementExecutionProvenance& requirementProvenance =
        problem.requirementExecution.provenance;
    options.modelFingerprint = result.modelFingerprint;
    if (!requirementProvenance.robotModelFingerprint.empty()) {
        const std::string sourceModelFingerprint =
            RobotModelFingerprint::canonicalSha256(problem.context.modelSpec);
        if (sourceModelFingerprint.empty() ||
            sourceModelFingerprint != requirementProvenance.robotModelFingerprint) {
            addError(result, "MODEL_FINGERPRINT_MISMATCH", "modelFingerprint",
                     "Frozen requirements belong to a different robot model.");
            result.candidateResult = failedCandidate(result.diagnostics.back().code);
            return result;
        }
        options.modelFingerprint = sourceModelFingerprint;
    }
    options.environmentFingerprint = result.environmentFingerprint;
    if (!requirementProvenance.environmentFingerprint.empty()) {
        if (!problem.scenarioSnapshot.environmentFingerprint.empty() &&
            problem.scenarioSnapshot.environmentFingerprint !=
                requirementProvenance.environmentFingerprint) {
            addError(result, "ENVIRONMENT_FINGERPRINT_MISMATCH", "environmentFingerprint",
                     "Frozen requirements belong to a different environment.");
            result.candidateResult = failedCandidate(result.diagnostics.back().code);
            return result;
        }
        options.environmentFingerprint = requirementProvenance.environmentFingerprint;
    }
    options.toolFingerprint = result.toolFingerprint;
    result.plan = EvaluationPlanCompiler::compile(problem.requirementExecution, options);
    result.planFingerprint = result.plan.fingerprint;
    appendPlanDiagnostics(result, result.plan);
    if (!result.plan.valid()) {
        CandidateAssemblyInput input;
        input.evaluationSucceeded = false;
        input.evidenceStage = AnalysisEvidenceStage::Verified;
        input.evaluationDiagnostic = "S52_EVALUATION_PLAN_INVALID";
        result.candidateResult = CandidateResultAssembler::assemble(input);
        return result;
    }

    const bool collisionRequired = requiresCollision(result.plan);
    EvaluationDeviceBuildRequest deviceRequest;
    deviceRequest.model = &compiled.candidate.kinematicModel;
    deviceRequest.deviceName = request.deviceName.empty() ? problem.context.deviceName : request.deviceName;
    deviceRequest.tcpFrame = request.tcpFrame;
    deviceRequest.sourceSnapshot = &problem.context.modelSpec;
    deviceRequest.scenarioSnapshot = problem.scenarioSnapshot.available() ? &problem.scenarioSnapshot : nullptr;
    deviceRequest.scenarioBaseDirectory = problem.scenarioSnapshot.baseDirectory;
    deviceRequest.checkCollision = request.checkCollision || collisionRequired;
    const EvaluationDeviceBuildResult device = EvaluationDeviceBuilder::build(deviceRequest);
    result.diagnostics.insert(result.diagnostics.end(), device.diagnostics.begin(),
                              device.diagnostics.end());
    if (!device.ok) {
        CandidateAssemblyInput input;
        input.evaluationSucceeded = false;
        input.evidenceStage = AnalysisEvidenceStage::Verified;
        input.evaluationDiagnostic = "S52_EVALUATION_DEVICE_BUILD_FAILED";
        result.candidateResult = CandidateResultAssembler::assemble(input);
        return result;
    }

    AnalysisContext context;
    AnalysisContextInput contextInput;
    contextInput.workcell = device.artifact.workcell;
    contextInput.device = device.artifact.device;
    contextInput.tcpFrame = device.artifact.tcpFrame;
    contextInput.baseState = device.artifact.state;
    contextInput.collisionDetector = device.artifact.collisionDetector;
    contextInput.deviceName = deviceRequest.deviceName;
    contextInput.tcpFrameName = deviceRequest.tcpFrame;
    contextInput.modelFingerprint = result.modelFingerprint;
    contextInput.environmentFingerprint = result.environmentFingerprint;
    contextInput.thresholds = request.thresholds;
    contextInput.collisionRequired = collisionRequired;
    std::string contextError;
    if (!makeAnalysisContext(contextInput, context, &contextError)) {
        addError(result, "S52_ANALYSIS_CONTEXT_INVALID", "analysisContext", contextError);
        CandidateAssemblyInput input;
        input.evaluationSucceeded = false;
        input.evidenceStage = AnalysisEvidenceStage::Verified;
        input.evaluationDiagnostic = result.diagnostics.back().code;
        result.candidateResult = CandidateResultAssembler::assemble(input);
        return result;
    }

    const TaskEvaluationResult tasks =
        TaskEvaluationStage().evaluate(context, result.plan, request.cancellation);
    const VerifiedRegionResult regions = request.cancellation.cancellationRequested()
                                              ? VerifiedRegionResult()
                                              : VerifiedRegionStage().evaluate(
                                                    context, result.plan, request.cancellation);
    CandidateAssemblyInput input;
    input.candidateId = compiled.candidate.candidateId;
    input.evidenceStage = AnalysisEvidenceStage::Verified;
    input.stages.push_back(tasks.stage);
    input.stages.push_back(regions.stage);
    input.completion.requestedCount = tasks.stage.requestedCount + regions.stage.requestedCount;
    input.completion.completedCount = tasks.stage.completedCount + regions.stage.completedCount;
    input.completion.canceled = request.cancellation.cancellationRequested() ||
                                tasks.stage.status == EvaluationStageStatus::Canceled ||
                                regions.stage.status == EvaluationStageStatus::Canceled;
    input.feasibility = tasks.mustFeasibility;
    for (const RegionCoverageResult& region : regions.regions)
        input.feasibility = combine(input.feasibility, region.feasibility);
    if (input.completion.canceled) {
        input.evaluationSucceeded = false;
        input.evaluationDiagnostic = "S52_BASELINE_CANCELED";
    }
    result.candidateResult = CandidateResultAssembler::assemble(input);
    if (result.candidateResult.feasibility == Feasibility::Infeasible)
        result.candidateResult.warnings.push_back(
            "S52_BASELINE_INFEASIBLE: optimization may continue under the configured policy.");
    result.ok = result.candidateResult.lifecycle == CandidateLifecycle::Completed;
    return result;
}

}    // namespace rws
