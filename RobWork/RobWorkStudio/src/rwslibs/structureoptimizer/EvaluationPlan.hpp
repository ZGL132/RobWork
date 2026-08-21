#ifndef RWS_STRUCTUREOPTIMIZATION_EVALUATIONPLAN_HPP
#define RWS_STRUCTUREOPTIMIZATION_EVALUATIONPLAN_HPP

#include <rwslibs/robotanalysiscore/RequirementExecutionTypes.hpp>

#include <set>
#include <string>
#include <vector>

namespace rws {

enum class EvaluationPlanStatus { Valid, Invalid };

struct EvaluationPlanDiagnostic {
    std::string code;
    std::string field;
    std::string message;
    bool blocking = true;
};

struct EvaluationPlanTask {
    RequirementExecutionTask source;
    bool hardConstraint = false;
    bool evidenceRequired = false;
};

struct EvaluationPlanRegion {
    RequirementExecutionRegion source;
    bool hardConstraint = false;
    bool evidenceRequired = false;
};

/** Immutable execution-only projection of a frozen requirement contract. */
struct EvaluationPlan {
    int schemaVersion = 1;
    EvaluationPlanStatus status = EvaluationPlanStatus::Invalid;
    std::string modelFingerprint;
    std::string environmentFingerprint;
    std::string toolFingerprint;
    std::string requirementFingerprint;
    std::string evaluatorId;
    std::string evaluatorVersion;
    std::vector<EvaluationPlanTask> tasks;
    std::vector<EvaluationPlanRegion> regions;
    std::vector<std::string> metricIds;
    std::set<std::string> capabilities;
    std::vector<EvaluationPlanDiagnostic> diagnostics;
    std::string fingerprint;

    bool valid() const { return status == EvaluationPlanStatus::Valid; }
    bool hasBlockingDiagnostics() const;
};

struct EvaluationPlanCompilerOptions {
    int outputSchemaVersion = 1;
    std::string modelFingerprint;
    std::string environmentFingerprint;
    std::string toolFingerprint;
    std::string evaluatorId = "structure.kinematics";
    std::string evaluatorVersion = "1";
    std::set<std::string> capabilities;
    std::set<std::string> knownMetricIds;
    std::vector<std::string> metricIds;
};

class EvaluationPlanCompiler {
  public:
    static EvaluationPlan compile(const RequirementExecutionSet& requirements,
                                  const EvaluationPlanCompilerOptions& options = {});
};

const char* toString(EvaluationPlanStatus status);

} // namespace rws

#endif
