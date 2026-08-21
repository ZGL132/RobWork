#ifndef RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONPREFLIGHT_HPP
#define RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONPREFLIGHT_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>
#include <vector>

namespace rws {

enum class OptimizationPreflightSeverity { Info, Warning, Error };

struct PreflightFinding {
    OptimizationPreflightSeverity severity = OptimizationPreflightSeverity::Info;
    std::string code;
    std::string objectId;
    std::string fieldPath;
    std::string message;
    std::string remediation;
};

struct OptimizationPreflightInput {
    bool hasModel = true;
    bool hasRequirements = true;
    bool hasKinematicValidation = true;
    bool fingerprintsCurrent = true;
    bool designSpaceValid = true;
    bool adaptersAvailable = true;
    bool metricsAvailable = true;
    bool evaluatorAvailable = true;
    bool normalizationValid = true;
    bool evidenceStagePossible = true;
    bool baselineAvailable = true;
    int independentVariableCount = 1;
    long long estimatedGridSize = 1;
    int candidateCount = 1;
    int finalVerificationCount = 1;
};

struct OptimizationPreflightResult {
    bool canStart = false;
    std::vector<PreflightFinding> findings;
    bool hasCode(const std::string& code) const;
};

class OptimizationPreflight {
  public:
    static OptimizationPreflightResult run(const OptimizationPreflightInput& input);
    static OptimizationPreflightResult run(const StructureOptimizationProblem& problem);
};

} // namespace rws

#endif
