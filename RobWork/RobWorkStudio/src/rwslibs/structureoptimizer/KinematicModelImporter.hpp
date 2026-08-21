#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICMODELIMPORTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICMODELIMPORTER_HPP

#include "KinematicImportResult.hpp"

#include <string>

namespace rw {
namespace kinematics {
class Frame;
}
namespace models {
class Device;
class WorkCell;
}
}    // namespace rw

namespace rws {

struct RobotModelSpec;

/** Explicit formal source selection. The importer never guesses a Device or TCP. */
struct KinematicImportRequest
{
    const rw::models::WorkCell* workcell = nullptr;
    const rw::models::Device* device = nullptr;
    const rw::kinematics::Frame* tcpFrame = nullptr;
    const RobotModelSpec* sourceSnapshot = nullptr;
    std::string modelId;
    std::string sourceFingerprint;
    std::string environmentFingerprint;
};

/**
 * Converts a supported, explicitly selected RobWork serial chain into the
 * canonical SE(3) representation. This operation is read-only and retains no
 * borrowed WorkCell, Device, or Frame pointers in its result.
 */
class KinematicModelImporter
{
  public:
    static KinematicImportResult import(const KinematicImportRequest& request);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_KINEMATICMODELIMPORTER_HPP
