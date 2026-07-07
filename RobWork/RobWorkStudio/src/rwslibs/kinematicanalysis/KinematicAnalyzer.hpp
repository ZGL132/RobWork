#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP

#include "KinematicAnalysisTypes.hpp"
#include "KinematicMetrics.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/models/Device.hpp>
#include <rw/proximity/CollisionDetector.hpp>

namespace rws {

class KinematicAnalyzer
{
  public:
    KinematicAnalyzer ();

    void setThresholds (const KinematicThresholds& thresholds);
    const KinematicThresholds& thresholds () const;

    KinematicCurrentPoseResult analyzeCurrentPose (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state) const;

    KinematicIkAnalysisResult analyzeIk (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const TaskPoint& target,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

  private:
    KinematicThresholds _thresholds;
};

void sortIkSolutionsForDisplay (std::vector< KinematicIkSolution >& solutions);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP
