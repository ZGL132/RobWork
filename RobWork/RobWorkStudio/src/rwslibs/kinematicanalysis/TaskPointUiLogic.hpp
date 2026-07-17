#ifndef RWS_KINEMATICANALYSIS_TASKPOINTUILOGIC_HPP
#define RWS_KINEMATICANALYSIS_TASKPOINTUILOGIC_HPP

#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalyzer.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <string>
#include <vector>

namespace rws {

std::vector< TaskPointReachabilityResult > analyzeSelectedTaskPointRows (
    const KinematicAnalyzer& analyzer,
    rw::models::WorkCell* workcell,
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& allPoints,
    const std::vector< int >& selectedRows,
    const std::vector< TaskPointReachabilityResult >& previousResults,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL);

TaskPoint taskPointFromCurrentTcpPose (
    const std::string& id,
    const std::string& tcpFrameName,
    const std::string& deviceBaseFrameName,
    const rw::math::Transform3D<>& baseTtcp,
    const KinematicThresholds& thresholds);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_TASKPOINTUILOGIC_HPP
