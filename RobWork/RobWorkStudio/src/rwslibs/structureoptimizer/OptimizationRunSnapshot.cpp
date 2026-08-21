#include "OptimizationRunSnapshot.hpp"

#include "StructureOptimizationJson.hpp"

#include <algorithm>

namespace rws {

const char* toString(OptimizationRunSnapshotStatus status)
{
    switch (status) {
    case OptimizationRunSnapshotStatus::Partial: return "Partial";
    case OptimizationRunSnapshotStatus::Canceled: return "Canceled";
    case OptimizationRunSnapshotStatus::Failed: return "Failed";
    case OptimizationRunSnapshotStatus::Completed: return "Completed";
    }
    return "Unknown";
}

bool optimizationRunSnapshotStatusFromString(const std::string& text,
                                             OptimizationRunSnapshotStatus& status,
                                             std::string* error)
{
    if (text == "Partial") status = OptimizationRunSnapshotStatus::Partial;
    else if (text == "Canceled") status = OptimizationRunSnapshotStatus::Canceled;
    else if (text == "Failed") status = OptimizationRunSnapshotStatus::Failed;
    else if (text == "Completed") status = OptimizationRunSnapshotStatus::Completed;
    else {
        if (error) *error = "Unknown optimization snapshot status: " + text;
        return false;
    }
    if (error) error->clear();
    return true;
}

const char* toString(OptimizationResourceAvailability availability)
{
    switch (availability) {
    case OptimizationResourceAvailability::Available: return "Available";
    case OptimizationResourceAvailability::DataUnavailable: return "DataUnavailable";
    case OptimizationResourceAvailability::Corrupt: return "Corrupt";
    case OptimizationResourceAvailability::Invalid: return "Invalid";
    }
    return "Invalid";
}

bool optimizationRunSnapshotValid(const OptimizationRunSnapshot& snapshot, std::string* error)
{
    auto fail = [error](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (snapshot.schemaVersion != 1) return fail("Unsupported optimization snapshot schema.");
    if (snapshot.runId.empty()) return fail("Optimization snapshot runId is required.");
    if (snapshot.input.projectEnvelopeFingerprint.empty())
        return fail("Optimization snapshot envelope fingerprint is required.");
    if (snapshot.input.modelFingerprint.empty() || snapshot.input.environmentFingerprint.empty() ||
        snapshot.input.requirementFingerprint.empty())
        return fail("Optimization snapshot input fingerprints are incomplete.");
    if (snapshot.completedCandidateCount > snapshot.generatedCandidateCount ||
        snapshot.generatedCandidateCount > snapshot.requestedCandidateCount)
        return fail("Optimization snapshot candidate counts are contradictory.");
    if (snapshot.status == OptimizationRunSnapshotStatus::Completed && snapshot.completedAt.empty())
        return fail("Completed optimization snapshot requires completedAt.");
    for (const OptimizationRunResourceRef& ref : snapshot.candidateResults) {
        if (ref.resourceId.empty() || ref.kind != "CandidateResult" || ref.relativePath.empty() ||
            ref.sha256.empty())
            return fail("Optimization snapshot contains an incomplete candidate resource reference.");
    }
    if (error) error->clear();
    return true;
}

OptimizationRunSnapshot makeOptimizationRunSnapshot(
    const std::string& runId, const StructureOptimizationProblem& problem,
    const std::string& evaluationPlanJson, const std::string& evaluationPlanFingerprint,
    const std::string& finalValidationPlanJson, const std::string& finalValidationPlanFingerprint,
    const std::string& modelFingerprint, const std::string& environmentFingerprint,
    const std::string& requirementFingerprint, const std::string& toolFingerprint,
    const std::string& adapterRegistryFingerprint)
{
    OptimizationRunSnapshot snapshot;
    snapshot.runId = runId;
    snapshot.currentEnvelopeJson = StructureOptimizationJson::currentEnvelopeToJson(problem);
    snapshot.input.projectEnvelopeFingerprint =
        StructureOptimizationJson::currentEnvelopeFingerprint(snapshot.currentEnvelopeJson);
    snapshot.input.designSpaceFingerprint = snapshot.input.projectEnvelopeFingerprint;
    snapshot.input.evaluationPlanFingerprint = evaluationPlanFingerprint;
    snapshot.input.finalValidationPlanFingerprint = finalValidationPlanFingerprint;
    snapshot.input.modelFingerprint = modelFingerprint;
    snapshot.input.environmentFingerprint = environmentFingerprint;
    snapshot.input.requirementFingerprint = requirementFingerprint;
    snapshot.input.toolFingerprint = toolFingerprint;
    snapshot.input.adapterRegistryFingerprint = adapterRegistryFingerprint;
    snapshot.input.compilerVersion = "1";
    snapshot.input.evaluatorId = problem.evaluation.evaluatorId;
    snapshot.input.evaluatorVersion = problem.evaluation.evaluatorVersion;
    snapshot.evaluationPlanJson = evaluationPlanJson;
    snapshot.finalValidationPlanJson = finalValidationPlanJson;
    snapshot.randomSeed = problem.run.randomSeed;
    snapshot.requestedCandidateCount = static_cast<std::size_t>(std::max(0, problem.run.candidateCount));
    return snapshot;
}

} // namespace rws
