#ifndef RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONRUNSNAPSHOT_HPP
#define RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONRUNSNAPSHOT_HPP

#include "CandidateResult.hpp"
#include "StructureOptimizationTypes.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rws {

enum class OptimizationRunSnapshotStatus { Partial, Canceled, Failed, Completed };
enum class OptimizationResourceAvailability { Available, DataUnavailable, Corrupt, Invalid };

struct OptimizationRunInputFingerprint {
    std::string projectEnvelopeFingerprint;
    std::string modelFingerprint;
    std::string environmentFingerprint;
    std::string requirementFingerprint;
    std::string designSpaceFingerprint;
    std::string evaluationPlanFingerprint;
    std::string finalValidationPlanFingerprint;
    std::string toolFingerprint;
    std::string adapterRegistryFingerprint;
    std::string compilerVersion;
    std::string evaluatorId;
    std::string evaluatorVersion;
};

struct OptimizationRunResourceRef {
    std::string resourceId;
    std::string kind;
    int schemaVersion = 1;
    std::string relativePath;
    std::string sha256;
    std::size_t byteSize = 0;
};

struct OptimizationRunSnapshot {
    int schemaVersion = 1;
    std::string runId;
    std::string startedAt;
    std::string completedAt;
    OptimizationRunSnapshotStatus status = OptimizationRunSnapshotStatus::Partial;
    OptimizationRunInputFingerprint input;
    std::string currentEnvelopeJson;
    std::string evaluationPlanJson;
    std::string finalValidationPlanJson;
    unsigned int randomSeed = 0;
    std::size_t requestedCandidateCount = 0;
    std::size_t generatedCandidateCount = 0;
    std::size_t completedCandidateCount = 0;
    std::size_t nextCandidateIndex = 0;
    bool canceled = false;
    std::string terminalDiagnostic;
    std::vector< OptimizationRunResourceRef > candidateResults;
    std::vector< OptimizationRunResourceRef > evidence;
    std::string baselineCandidateId;
    std::string bestCandidateId;
    std::string snapshotSha256;
};

const char* toString(OptimizationRunSnapshotStatus status);
bool optimizationRunSnapshotStatusFromString(const std::string& text,
                                              OptimizationRunSnapshotStatus& status,
                                              std::string* error = nullptr);
const char* toString(OptimizationResourceAvailability availability);
bool optimizationRunSnapshotValid(const OptimizationRunSnapshot& snapshot,
                                  std::string* error = nullptr);

OptimizationRunSnapshot makeOptimizationRunSnapshot(
    const std::string& runId,
    const StructureOptimizationProblem& problem,
    const std::string& evaluationPlanJson,
    const std::string& evaluationPlanFingerprint,
    const std::string& finalValidationPlanJson,
    const std::string& finalValidationPlanFingerprint,
    const std::string& modelFingerprint,
    const std::string& environmentFingerprint,
    const std::string& requirementFingerprint,
    const std::string& toolFingerprint,
    const std::string& adapterRegistryFingerprint);

} // namespace rws

#endif
