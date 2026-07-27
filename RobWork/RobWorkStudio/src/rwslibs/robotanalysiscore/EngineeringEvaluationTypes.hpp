#ifndef RWS_ROBOTANALYSISCORE_ENGINEERINGEVALUATIONTYPES_HPP
#define RWS_ROBOTANALYSISCORE_ENGINEERINGEVALUATIONTYPES_HPP

#include "RobotAnalysisTypes.hpp"

#include <functional>
#include <string>
#include <vector>

namespace rws {

enum class EngineeringEvaluationStatus
{
    Success,
    Infeasible,
    DataInsufficient,
    Failed,
    Cancelled
};

enum class EngineeringMetricStatus
{
    Valid,
    Warning,
    Invalid
};

enum class EngineeringEvaluationStage
{
    Quick,
    Verified
};

struct EngineeringInputSnapshot
{
    std::string modelHash;
    std::string taskEnvironmentHash;
    std::string configurationHash;
};

struct EngineeringMetric
{
    std::string metricId;
    double value = 0.0;
    std::string unit;
    EngineeringMetricStatus status = EngineeringMetricStatus::Valid;
    std::string providerId;
};

struct EngineeringConstraintResult
{
    std::string metricId;
    std::string comparison;
    double threshold = 0.0;
    double observedValue = 0.0;
    bool hard = true;
    bool satisfied = false;
    std::string failureReason;
};

struct EngineeringArtifact
{
    std::string artifactId;
    std::string mimeType;
    std::string payload;
};

struct CandidateEvaluationContext
{
    RobotDesignContext designContext;
    std::vector<std::string> variableIds;
    std::vector<double> variableValues;
    EngineeringInputSnapshot inputSnapshot;
};

struct EvaluationRequest
{
    EngineeringEvaluationStage stage = EngineeringEvaluationStage::Quick;
    std::vector<std::string> requestedMetricIds;
    std::vector<EngineeringArtifact> inputArtifacts;
    std::string configurationHash;
    bool allowCachedArtifacts = true;
};

struct EngineeringEvaluationCacheIdentity
{
    EngineeringInputSnapshot inputSnapshot;
    std::string evaluatorId;
    std::string evaluatorVersion;
    std::string configurationHash;
    EngineeringEvaluationStage stage = EngineeringEvaluationStage::Quick;
};

struct EvaluationCallbacks
{
    std::function<bool()> isCancellationRequested;
    std::function<void(const std::string&)> onDiagnostic;
};

struct EngineeringEvaluationResult
{
    std::string providerId;
    std::string providerVersion;
    EngineeringEvaluationStatus status = EngineeringEvaluationStatus::Failed;
    EngineeringInputSnapshot inputSnapshot;
    double elapsedSeconds = 0.0;
    std::vector<EngineeringMetric> metrics;
    std::vector<EngineeringConstraintResult> constraints;
    std::vector<AnalysisWarning> warnings;
    std::vector<EngineeringArtifact> artifacts;
};

} // namespace rws

#endif // RWS_ROBOTANALYSISCORE_ENGINEERINGEVALUATIONTYPES_HPP
