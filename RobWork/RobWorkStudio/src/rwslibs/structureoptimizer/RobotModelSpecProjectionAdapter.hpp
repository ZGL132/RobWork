#ifndef RWS_STRUCTUREOPTIMIZATION_ROBOTMODELSPECPROJECTIONADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_ROBOTMODELSPECPROJECTIONADAPTER_HPP

#include "CanonicalKinematicModel.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelSpec.hpp>

#include <string>
#include <vector>

namespace rws {

struct RobotModelSpecProjectionRequest
{
    const CanonicalKinematicModel* model = nullptr;
    std::string robotName;
    std::string saveDirectory;
    const RobotModelSpec* sourceSnapshot = nullptr;
};

struct RobotModelSpecProjectionResult
{
    bool ok = false;
    RobotModelSpec spec;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Read-only canonical-to-legacy output projection.  DH fields are never a
 * source of truth and are intentionally left empty. */
class RobotModelSpecProjectionAdapter
{
  public:
    static RobotModelSpecProjectionResult project(
        const RobotModelSpecProjectionRequest& request);
};

}    // namespace rws

#endif
