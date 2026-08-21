#ifndef RWS_STRUCTUREOPTIMIZATION_EVALUATIONDEVICEBUILDER_HPP
#define RWS_STRUCTUREOPTIMIZATION_EVALUATIONDEVICEBUILDER_HPP

#include "CandidateModelFactory.hpp"
#include "RobotModelSpecProjectionAdapter.hpp"

namespace rws {

struct EvaluationDeviceBuildRequest
{
    const CanonicalKinematicModel* model = nullptr;
    std::string deviceName;
    std::string tcpFrame;
    const RobotModelSpec* sourceSnapshot = nullptr;
    bool checkCollision = true;
};

struct EvaluationDeviceBuildResult
{
    bool ok = false;
    CandidateModelArtifact artifact;
    RobotModelSpec projectedSpec;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Builds an isolated worker WorkCell/Device from a canonical candidate. */
class EvaluationDeviceBuilder
{
  public:
    static EvaluationDeviceBuildResult build(const EvaluationDeviceBuildRequest& request);
};

}    // namespace rws

#endif
