#ifndef RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP

#include "KinematicAnalysisTypes.hpp"

#include <rw/math/Q.hpp>
#include <rw/math/Jacobian.hpp>

#include <utility>
#include <vector>

namespace rws {

struct SingularMetrics
{
    std::vector< double > singularValues;
    double conditionNumber = 0.0;
    double manipulability  = 0.0;
    AnalysisStatus status  = AnalysisStatus::Unknown;
    std::vector< AnalysisWarning > warnings;
};

std::vector< double > calculateJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds);

double minimumJointLimitMargin (const std::vector< double >& margins);

AnalysisStatus classifyJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    const KinematicThresholds& thresholds,
    std::vector< AnalysisWarning >* warnings);

SingularMetrics calculateSingularMetrics (
    const rw::math::Jacobian& jacobian,
    const KinematicThresholds& thresholds);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP
