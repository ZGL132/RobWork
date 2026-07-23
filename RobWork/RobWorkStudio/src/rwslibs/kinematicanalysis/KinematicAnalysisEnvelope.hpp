#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISENVELOPE_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISENVELOPE_HPP

#include "KinematicAnalysisVisualizationTypes.hpp"

#include <rw/kinematics/State.hpp>
#include <rw/math/Q.hpp>

namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; } }

namespace rws {

struct WorkspaceEnvelopeConfig
{
    VisualProjection projection = VisualProjection::XY;
    int angularDirections = 180;
    int coordinateIterations = 6;
};

QPointF projectEnvelopePosition (const std::array< double, 3 >& position,
                                 VisualProjection projection);

AnalysisEnvelopeData computeWorkspaceEnvelope (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceEnvelopeConfig& config);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISENVELOPE_HPP
