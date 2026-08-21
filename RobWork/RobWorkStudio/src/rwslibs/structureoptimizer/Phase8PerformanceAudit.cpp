#include "Phase8PerformanceAudit.hpp"

#include <cmath>

namespace rws {

bool Phase8PerformanceAuditResult::hasCode(const std::string& code) const
{
    for (const Phase8PerformanceFinding& finding : findings)
        if (finding.code == code)
            return true;
    return false;
}

namespace {

void add(Phase8PerformanceAuditResult& result, Phase8PerformanceSeverity severity,
         const char* code, const char* message)
{
    result.findings.push_back({severity, code, message});
}

bool finiteNonNegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

Phase8PerformanceAuditResult Phase8PerformanceAudit::audit(
    const StructureOptimizationResult& result, const Phase8PerformanceBudget& budget)
{
    Phase8PerformanceAuditResult output;
    const StructureRunDiagnostics& diagnostics = result.diagnostics;
    if (budget.maxGeneratedCandidates < 0 || !finiteNonNegative(budget.maxTotalSeconds) ||
        !finiteNonNegative(budget.maxModelBuildSeconds) ||
        !finiteNonNegative(budget.maxEvaluationSeconds) ||
        !finiteNonNegative(budget.minimumCacheHitRate) || budget.minimumCacheHitRate > 1.0) {
        add(output, Phase8PerformanceSeverity::Error, "Phase8.Performance.BudgetInvalid",
            "Performance budget contains invalid values.");
    }
    if (diagnostics.generatedCandidates < 0 || diagnostics.evaluatedCandidates < 0 ||
        diagnostics.cacheHits < 0 || diagnostics.cacheHits > diagnostics.evaluatedCandidates ||
        !finiteNonNegative(diagnostics.totalSeconds) ||
        !finiteNonNegative(diagnostics.modelBuildSeconds) ||
        !finiteNonNegative(diagnostics.kinematicEvaluationSeconds) ||
        !finiteNonNegative(diagnostics.workspaceEvaluationSeconds)) {
        add(output, Phase8PerformanceSeverity::Error, "Phase8.Performance.DiagnosticsInvalid",
            "Run diagnostics contain invalid counters or timing values.");
    }
    if (diagnostics.generatedCandidates > budget.maxGeneratedCandidates)
        add(output, Phase8PerformanceSeverity::Warning, "Phase8.Performance.CandidateBudgetExceeded",
            "Generated candidate count exceeds the release budget.");
    if (diagnostics.totalSeconds > budget.maxTotalSeconds)
        add(output, Phase8PerformanceSeverity::Warning, "Phase8.Performance.TotalBudgetExceeded",
            "Total run time exceeds the release budget.");
    if (diagnostics.modelBuildSeconds > budget.maxModelBuildSeconds)
        add(output, Phase8PerformanceSeverity::Warning, "Phase8.Performance.ModelBuildBudgetExceeded",
            "Model build time exceeds the release budget.");
    const double evaluationSeconds = diagnostics.kinematicEvaluationSeconds +
                                     diagnostics.workspaceEvaluationSeconds;
    if (evaluationSeconds > budget.maxEvaluationSeconds)
        add(output, Phase8PerformanceSeverity::Warning, "Phase8.Performance.EvaluationBudgetExceeded",
            "Evaluation time exceeds the release budget.");
    if (diagnostics.evaluatedCandidates > 0) {
        const double hitRate = static_cast<double>(diagnostics.cacheHits) /
                               static_cast<double>(diagnostics.evaluatedCandidates);
        if (hitRate < budget.minimumCacheHitRate)
            add(output, Phase8PerformanceSeverity::Warning,
                "Phase8.Performance.CacheHitRateLow", "Cache hit rate is below the budget.");
    }
    output.valid = true;
    output.withinBudget = true;
    for (const Phase8PerformanceFinding& finding : output.findings) {
        if (finding.severity == Phase8PerformanceSeverity::Error)
            output.valid = false;
        else
            output.withinBudget = false;
    }
    return output;
}

} // namespace rws
