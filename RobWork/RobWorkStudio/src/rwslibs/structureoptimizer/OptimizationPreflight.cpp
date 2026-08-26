#include "OptimizationPreflight.hpp"

#include "StructureOptimizationUiLogic.hpp"
#include "StructureOptimizationValidation.hpp"

#include <cmath>
#include <limits>

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
    if (input.estimatedGridSize > 1000000)
        add("StructureOptimization.Run.SearchSpaceLarge",
            "The estimated grid is oversized; sampling is required.",
            "Reduce ranges or increase the step.", OptimizationPreflightSeverity::Warning);
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
    // 核心门禁只调用核心验证器；UI 不得重新实现一套可运行性判断。
    for (const AnalysisWarning& warning : StructureOptimizationValidation::validateProblem(problem)) {
        const OptimizationPreflightSeverity severity =
            warning.severity == AnalysisStatus::Fail
                ? OptimizationPreflightSeverity::Error
                : warning.severity == AnalysisStatus::Warning
                      ? OptimizationPreflightSeverity::Warning
                      : OptimizationPreflightSeverity::Info;
        result.findings.push_back({severity, warning.code, {}, {}, warning.message,
                                   "Review the highlighted optimization input."});
    }

    OptimizationPreflightInput input;
    input.hasModel = !problem.context.modelSpec.transformJoints.empty() ||
                     !problem.context.modelSpec.dhJoints.empty();
    input.independentVariableCount = 0;
    long double gridSize = 1.0L;
    bool hasSearchDimension = false;
    for (const auto& variable : problem.variables) {
        if (variable.enabled) {
            ++input.independentVariableCount;
            if (std::isfinite(variable.minimum) && std::isfinite(variable.maximum) &&
                std::isfinite(variable.step) && variable.step > 0.0 &&
                variable.maximum >= variable.minimum) {
                hasSearchDimension = true;
                const long double values =
                    std::floor((static_cast<long double>(variable.maximum) - variable.minimum) /
                               variable.step) + 1.0L;
                gridSize = std::min(gridSize * std::max(values, 1.0L),
                                    static_cast<long double>(std::numeric_limits<long long>::max()));
            }
        }
    }
    input.estimatedGridSize = hasSearchDimension ? static_cast<long long>(gridSize) : 1;
    input.candidateCount = problem.run.candidateCount;
    input.finalVerificationCount = problem.run.finalVerificationCount;
    // 运行数量和变量数量是结构化门禁的一部分，Start 与 banner 共享同一结果。
    const OptimizationPreflightResult basic = run(input);
    result.findings.insert(result.findings.end(), basic.findings.begin(), basic.findings.end());
    // C1.1/D1: 冻结契约一致性属于结构化门禁——stale 或未验证都必须阻断，
    // 使 Preflight/Start/Banner 与 Controller 拒绝共享同一结论。
    if (StructureOptimizationUiLogic::frozenContractStale(problem)) {
        const bool hasReference =
            !StructureOptimizationUiLogic::frozenReferenceFingerprint(problem).empty();
        result.findings.push_back(
            {OptimizationPreflightSeverity::Error,
             "StructureOptimization.FrozenContract.Stale", {}, {},
             hasReference
                 ? std::string("Frozen requirements are stale: edits diverge from the frozen "
                               "execution contract.")
                 : std::string("Frozen execution contract is unverified: no editable-contract "
                               "reference fingerprint."),
             std::string("Re-freeze the requirements from their source, then reload the project.")});
    }
    result.canStart = true;
    for (const auto& finding : result.findings) if (finding.severity == OptimizationPreflightSeverity::Error) result.canStart = false;
    return result;
}

} // namespace rws
