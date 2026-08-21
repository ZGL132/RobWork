#include "Phase8ResourceAudit.hpp"

namespace rws {

bool Phase8ResourceAuditResult::hasCode(const std::string& code) const
{
    for (const std::string& error : errors)
        if (error.rfind(code + ":", 0) == 0)
            return true;
    return false;
}

namespace {

void add(Phase8ResourceAuditResult& result, const char* code, const char* message)
{
    result.errors.push_back(std::string(code) + ":" + message);
}

bool containsTemporaryPreviewPath(const std::string& value)
{
    return value.find("structure-optimizer-preview-") != std::string::npos ||
           value.find("QTemporaryDir") != std::string::npos;
}

} // namespace

Phase8ResourceAuditResult Phase8ResourceAudit::auditController(
    const Phase8ControllerSnapshot& snapshot)
{
    Phase8ResourceAuditResult result;
    if (!snapshot.running && snapshot.paused)
        add(result, "Phase8.Controller.PausedWhileIdle", "paused cannot remain true while idle");
    if (snapshot.running && snapshot.state == OptimizationRunState::Idle)
        add(result, "Phase8.Controller.RunningStateMismatch", "running requires a non-idle state");
    if (!snapshot.running && (snapshot.state == OptimizationRunState::Running ||
                              snapshot.state == OptimizationRunState::Paused ||
                              snapshot.state == OptimizationRunState::CancelRequested))
        add(result, "Phase8.Controller.StateWhileStopped", "stopped controller exposes an active state");
    if (snapshot.baselineRunning && snapshot.running)
        add(result, "Phase8.Controller.ConcurrentRuns", "baseline and optimization runs must be mutually exclusive");
    result.passed = result.errors.empty();
    return result;
}

Phase8ResourceAuditResult Phase8ResourceAudit::auditResult(
    const StructureOptimizationResult& result)
{
    Phase8ResourceAuditResult output;
    for (const AnalysisWarning& warning : result.warnings) {
        if (containsTemporaryPreviewPath(warning.code) || containsTemporaryPreviewPath(warning.message))
            add(output, "Phase8.Result.TemporaryPath", "global warning contains a preview temporary path");
    }
    for (const StructureCandidateResult& candidate : result.candidates) {
        for (const std::string& warning : candidate.warnings) {
            if (containsTemporaryPreviewPath(warning))
                add(output, "Phase8.Result.TemporaryPath", "candidate warning contains a preview temporary path");
        }
    }
    if (result.diagnostics.evaluatedCandidates > result.diagnostics.generatedCandidates)
        add(output, "Phase8.Result.CounterMismatch", "evaluated candidates exceed generated candidates");
    output.passed = output.errors.empty();
    return output;
}

} // namespace rws
