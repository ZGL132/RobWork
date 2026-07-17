#include "TaskPointUiLogic.hpp"

#include <rw/math/RPY.hpp>

using namespace rws;

std::vector< TaskPointReachabilityResult > rws::analyzeSelectedTaskPointRows (
    const KinematicAnalyzer& analyzer,
    rw::models::WorkCell* workcell,
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& allPoints,
    const std::vector< int >& selectedRows,
    const std::vector< TaskPointReachabilityResult >& previousResults,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector)
{
    std::vector< TaskPointReachabilityResult > results = previousResults;
    results.resize (allPoints.size ());
    for (std::size_t i = 0; i < allPoints.size (); ++i) {
        if (results[i].taskPoint.id.empty ())
            results[i].taskPoint = allPoints[i];
    }

    for (const int row : selectedRows) {
        if (row < 0 || static_cast< std::size_t > (row) >= allPoints.size ())
            continue;
        results[static_cast< std::size_t > (row)] =
            analyzer.analyzeTaskPoint (
                workcell, device, defaultTcpFrame, state,
                allPoints[static_cast< std::size_t > (row)], collisionDetector);
    }
    return results;
}

TaskPoint rws::taskPointFromCurrentTcpPose (
    const std::string& id,
    const std::string& tcpFrameName,
    const std::string& deviceBaseFrameName,
    const rw::math::Transform3D<>& baseTtcp,
    const KinematicThresholds& thresholds)
{
    const rw::math::RPY<> rpy (baseTtcp.R ());
    const double toDeg = 180.0 / rw::math::Pi;

    TaskPoint p;
    p.id          = id;
    p.name        = id;
    p.type        = TaskPointType::Generic;
    p.refFrame    = deviceBaseFrameName;
    p.tcpFrame    = tcpFrameName;
    p.position    = {{baseTtcp.P ()[0], baseTtcp.P ()[1], baseTtcp.P ()[2]}};
    p.rpyDeg      = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    p.tolerance.positionMeters = thresholds.positionToleranceMeters;
    p.tolerance.orientationDeg = thresholds.orientationToleranceDeg;
    p.tolerance.allowToolRollFree = false;
    p.weight      = 1.0;
    p.enabled     = true;
    p.note        = "imported from current TCP pose";
    return p;
}
