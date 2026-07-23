#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEMODELFACTORY_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEMODELFACTORY_HPP

#include "StructureOptimizationTypes.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <memory>
#include <QTemporaryDir>
#include <string>
#include <vector>

namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; } }

namespace rws {

struct CandidateModelArtifact {
    rw::core::Ptr<rw::models::WorkCell> workcell;
    rw::core::Ptr<rw::models::Device> device;
    rw::kinematics::State state;
    rw::core::Ptr<const rw::kinematics::Frame> tcpFrame;
    rw::core::Ptr<rw::proximity::CollisionDetector> collisionDetector;
    std::shared_ptr<QTemporaryDir> temporaryDirectory;
};

struct CandidateModelBuildRequest {
    RobotModelSpec spec;
    std::string deviceName;
    std::string tcpFrame;
    bool checkCollision = true;
};

struct CandidateModelBuildResult {
    bool ok = false;
    CandidateModelArtifact artifact;
    std::vector<AnalysisWarning> warnings;
};

class CandidateModelFactory {
  public:
    CandidateModelBuildResult build(const CandidateModelBuildRequest& request);
};

} // namespace rws
#endif
