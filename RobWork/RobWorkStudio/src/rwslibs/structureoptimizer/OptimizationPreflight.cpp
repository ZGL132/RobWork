#include "OptimizationPreflight.hpp"

#include "StructureOptimizationUiLogic.hpp"

namespace rws {

bool OptimizationPreflightResult::hasCode(const std::string& code) const
{
    for (const PreflightFinding& finding : findings)
        if (finding.code == code) return true;
    return false;
}

OptimizationPreflightResult OptimizationPreflight::run(const OptimizationPreflightInput& input)
{
    OptimizationPreflightResult result;
    const auto add = [&result](const char* code, const char* message, const char* remediation,
                               OptimizationPreflightSeverity severity = OptimizationPreflightSeverity::Error) {
        result.findings.push_back({severity, code, {}, {}, message, remediation});
    };
    if (!input.hasModel) add("MODEL_MISSING", "Robot model evidence is missing.", "Load a managed model.");
    if (!input.hasRequirements) add("REQUIREMENT_MISSING", "Frozen requirements are missing.", "Freeze the requirement execution set.");
    if (!input.hasKinematicValidation) add("KINEMATIC_VALIDATION_MISSING", "Kinematic validation evidence is missing.", "Run kinematic validation.");
    if (!input.fingerprintsCurrent) add("FINGERPRINT_STALE", "One or more input fingerprints are stale.", "Refresh upstream evidence.");
    if (!input.designSpaceValid) add("DESIGN_SPACE_INVALID", "The design space is invalid.", "Repair variable ranges and bindings.");
    if (input.independentVariableCount <= 0) add("NO_INDEPENDENT_VARIABLE", "No independent design variable is enabled.", "Enable at least one independent variable.");
    if (!input.adaptersAvailable) add("ADAPTER_MISSING", "A required parameter adapter is unavailable.", "Register the required adapter.");
    if (!input.metricsAvailable) add("METRIC_MISSING", "A required metric is unavailable.", "Select an available metric.");
    if (!input.evaluatorAvailable) add("EVALUATOR_MISSING", "The evaluator is unavailable.", "Load the evaluator implementation.");
    if (!input.normalizationValid) add("NORMALIZATION_INVALID", "Objective normalization is invalid.", "Repair good/bad normalization bounds.");
    if (!input.evidenceStagePossible) add("EVIDENCE_STAGE_IMPOSSIBLE", "The requested evidence stage cannot be produced.", "Choose a supported validation stage.");
    if (!input.baselineAvailable) add("BASELINE_UNAVAILABLE", "The baseline evaluation is unavailable.", "Evaluate and freeze a baseline.");
    if (input.estimatedGridSize > 1000000) add("GRID_OVERSIZED", "The estimated grid is oversized; sampling is required.", "Reduce ranges or increase the step.", OptimizationPreflightSeverity::Warning);
    if (input.candidateCount <= 0 || input.finalVerificationCount < 0 || input.finalVerificationCount > input.candidateCount)
        add("COUNT_CONTRADICTORY", "Candidate and verification counts are contradictory.", "Repair run budgets.");
    result.canStart = true;
    for (const PreflightFinding& finding : result.findings)
        if (finding.severity == OptimizationPreflightSeverity::Error) result.canStart = false;
    return result;
}

OptimizationPreflightResult OptimizationPreflight::run(const StructureOptimizationProblem& problem)
{
    OptimizationPreflightResult result;
    for (const StructurePreflightFinding& finding : StructureOptimizationUiLogic::preflight(problem)) {
        const OptimizationPreflightSeverity severity = finding.severity == AnalysisStatus::Fail
                                                            ? OptimizationPreflightSeverity::Error
                                                            : finding.severity == AnalysisStatus::Warning
                                                                  ? OptimizationPreflightSeverity::Warning
                                                                  : OptimizationPreflightSeverity::Info;
        result.findings.push_back({severity, finding.code, {}, {}, finding.message, finding.remediation});
    }
    OptimizationPreflightInput input;
    input.hasModel = !problem.context.modelSpec.transformJoints.empty();
    input.independentVariableCount = 0;
    for (const auto& variable : problem.variables) if (variable.enabled) ++input.independentVariableCount;
    input.candidateCount = problem.run.candidateCount;
    input.finalVerificationCount = problem.run.finalVerificationCount;
    const OptimizationPreflightResult basic = run(input);
    result.findings.insert(result.findings.end(), basic.findings.begin(), basic.findings.end());
    result.canStart = true;
    for (const auto& finding : result.findings) if (finding.severity == OptimizationPreflightSeverity::Error) result.canStart = false;
    return result;
}

} // namespace rws
