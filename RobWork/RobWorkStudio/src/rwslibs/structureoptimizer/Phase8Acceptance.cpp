#include "Phase8Acceptance.hpp"

#include <cmath>
#include <set>

namespace rws {

bool Phase8AcceptanceResult::hasCode(const std::string& code) const
{
    for (const Phase8AcceptanceFinding& finding : findings)
        if (finding.code == code)
            return true;
    return false;
}

namespace {

void add(Phase8AcceptanceResult& result, const char* code, const char* message)
{
    result.findings.push_back({code, message});
}

bool finiteCandidate(const StructureCandidateResult& candidate)
{
    if (!std::isfinite(candidate.totalScore))
        return false;
    for (double value : candidate.values)
        if (!std::isfinite(value))
            return false;
    return true;
}

const StructureCandidateResult* findCandidate(const StructureOptimizationResult& result,
                                              int index)
{
    for (const StructureCandidateResult& candidate : result.candidates)
        if (candidate.index == index)
            return &candidate;
    return nullptr;
}

} // namespace

Phase8AcceptanceResult Phase8Acceptance::validateResult(
    const StructureOptimizationResult& result)
{
    Phase8AcceptanceResult output;
    std::set<int> indexes;
    for (const StructureCandidateResult& candidate : result.candidates) {
        if (!indexes.insert(candidate.index).second)
            add(output, "Phase8.Candidate.DuplicateIndex", "Candidate stable indexes must be unique.");
        if (!finiteCandidate(candidate))
            add(output, "Phase8.Candidate.NonFinite", "Candidate values and score must be finite.");
        if (candidate.status == StructureCandidateStatus::Feasible && !candidate.feasible)
            add(output, "Phase8.Candidate.FeasibilityMismatch",
                "A Feasible candidate must expose feasible=true.");
        if (candidate.status != StructureCandidateStatus::Feasible && candidate.feasible)
            add(output, "Phase8.Candidate.FalseFeasible",
                "Only a Feasible candidate may expose feasible=true.");
    }
    if (!result.candidates.empty()) {
        if (findCandidate(result, result.baselineCandidateIndex) == nullptr)
            add(output, "Phase8.Baseline.Missing", "The result baseline candidate is missing.");
        if (result.bestCandidateIndex >= 0) {
            const StructureCandidateResult* best = findCandidate(result, result.bestCandidateIndex);
            if (best == nullptr)
                add(output, "Phase8.Best.Missing", "The result best candidate is missing.");
            else if (!best->feasible || best->status != StructureCandidateStatus::Feasible)
                add(output, "Phase8.Best.NotFeasible", "The published best candidate must be feasible.");
        }
    }
    if (result.diagnostics.generatedCandidates < 0 || result.diagnostics.evaluatedCandidates < 0 ||
        result.diagnostics.cacheHits < 0 || result.diagnostics.totalSeconds < 0.0 ||
        !std::isfinite(result.diagnostics.totalSeconds))
        add(output, "Phase8.Diagnostics.Invalid", "Run diagnostics contain invalid counters or timing.");
    for (const AnalysisWarning& warning : result.warnings) {
        if (warning.severity == AnalysisStatus::Fail && !result.canceled)
            add(output, "Phase8.Warning.Fatal", "A non-canceled result contains a fatal warning.");
    }
    output.passed = output.findings.empty();
    return output;
}

Phase8AcceptanceResult Phase8Acceptance::compareDeterministic(
    const StructureOptimizationResult& first,
    const StructureOptimizationResult& second)
{
    Phase8AcceptanceResult output;
    const Phase8AcceptanceResult firstValidation = validateResult(first);
    const Phase8AcceptanceResult secondValidation = validateResult(second);
    output.findings.insert(output.findings.end(), firstValidation.findings.begin(),
                           firstValidation.findings.end());
    output.findings.insert(output.findings.end(), secondValidation.findings.begin(),
                           secondValidation.findings.end());
    if (first.candidates.size() != second.candidates.size())
        add(output, "Phase8.Determinism.CountMismatch", "Repeated runs produced different candidate counts.");
    const std::size_t count = std::min(first.candidates.size(), second.candidates.size());
    for (std::size_t i = 0; i < count; ++i) {
        const StructureCandidateResult& left = first.candidates[i];
        const StructureCandidateResult& right = second.candidates[i];
        if (left.index != right.index || left.feasible != right.feasible ||
            left.status != right.status || left.stage != right.stage ||
            left.values != right.values || left.totalScore != right.totalScore)
            add(output, "Phase8.Determinism.CandidateMismatch",
                "Repeated runs produced different candidate order or values.");
    }
    if (first.baselineCandidateIndex != second.baselineCandidateIndex ||
        first.bestCandidateIndex != second.bestCandidateIndex)
        add(output, "Phase8.Determinism.SelectionMismatch",
            "Repeated runs selected different baseline or best candidate indexes.");
    output.passed = output.findings.empty();
    return output;
}

} // namespace rws
