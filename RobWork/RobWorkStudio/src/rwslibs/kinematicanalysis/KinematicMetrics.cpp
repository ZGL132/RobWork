#include "KinematicMetrics.hpp"

#include <Eigen/Dense>
#include <Eigen/SVD>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

std::vector< double > rws::calculateJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    std::vector< double > margins;
    const std::size_t n = q.size ();
    if (bounds.first.size () != n || bounds.second.size () != n)
        return margins;
    margins.reserve (n);
    const double eps = std::numeric_limits< double >::epsilon ();
    for (std::size_t i = 0; i < n; ++i) {
        const double lo = bounds.first (i);
        const double hi = bounds.second (i);
        const double span = std::max (hi - lo, eps);
        const double d_lo = q (i) - lo;
        const double d_hi = hi - q (i);
        const double d = std::min (d_lo, d_hi);
        margins.push_back (d / span);
    }
    return margins;
}

double rws::minimumJointLimitMargin (const std::vector< double >& margins)
{
    if (margins.empty ())
        return 0.0;
    return *std::min_element (margins.begin (), margins.end ());
}

AnalysisStatus rws::classifyJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    const KinematicThresholds& thresholds,
    std::vector< AnalysisWarning >* warnings)
{
    AnalysisStatus overall = AnalysisStatus::Pass;
    bool inLimit   = false;
    bool nearLimit = false;
    const std::size_t n = q.size ();
    if (bounds.first.size () == n && bounds.second.size () == n) {
        for (std::size_t i = 0; i < n; ++i) {
            const double lo = bounds.first (i);
            const double hi = bounds.second (i);
            if (q (i) < lo || q (i) > hi) {
                inLimit = true;
                if (warnings != nullptr) {
                    AnalysisWarning w;
                    w.code     = "KIN_JOINT_LIMIT";
                    w.message  = "Joint " + std::to_string (i) + " is outside its limits.";
                    w.source   = "KinematicAnalyzer";
                    w.severity = AnalysisStatus::Fail;
                    warnings->push_back (w);
                }
            }
        }
    }

    const std::vector< double > margins = calculateJointLimitMargins (q, bounds);
    if (!margins.empty ()) {
        const double minMargin = minimumJointLimitMargin (margins);
        if (!inLimit && minMargin < thresholds.nearJointLimitRatio) {
            nearLimit = true;
            if (warnings != nullptr) {
                AnalysisWarning w;
                w.code     = "KIN_NEAR_JOINT_LIMIT";
                w.message  = "Minimum joint-limit margin (" +
                             std::to_string (minMargin) + ") is below threshold " +
                             std::to_string (thresholds.nearJointLimitRatio) + ".";
                w.source   = "KinematicAnalyzer";
                w.severity = AnalysisStatus::Warning;
                warnings->push_back (w);
            }
        }
    }

    if (inLimit)
        overall = AnalysisStatus::Fail;
    else if (nearLimit)
        overall = AnalysisStatus::Warning;
    else
        overall = AnalysisStatus::Pass;
    return overall;
}

SingularMetrics rws::calculateSingularMetrics (
    const rw::math::Jacobian& jacobian,
    const KinematicThresholds& thresholds)
{
    SingularMetrics result;
    const Eigen::MatrixXd m = jacobian.e ();
    if (m.rows () == 0 || m.cols () == 0)
        return result;

    Eigen::JacobiSVD< Eigen::MatrixXd > svd (m);
    const Eigen::VectorXd sigma = svd.singularValues ();
    const std::size_t n = static_cast< std::size_t > (sigma.size ());
    result.singularValues.assign (n, 0.0);
    result.manipulability = 1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = sigma (static_cast< Eigen::Index > (i));
        result.singularValues[i] = s;
        result.manipulability *= s;
    }

    if (n == 0)
        return result;

    const double sigmaMax = result.singularValues.front ();
    const double sigmaMin = result.singularValues.back ();
    if (sigmaMin < 1e-12) {
        result.conditionNumber = std::numeric_limits< double >::infinity ();
    }
    else {
        result.conditionNumber = sigmaMax / sigmaMin;
    }

    const bool infCondition  = std::isinf (result.conditionNumber);
    const bool failCondition =
        infCondition || result.conditionNumber >= thresholds.conditionFail;
    const bool warnCondition =
        result.conditionNumber >= thresholds.conditionWarning ||
        sigmaMin < thresholds.singularValueWarning ||
        result.manipulability < thresholds.manipulabilityWarning;
    if (failCondition)
        result.status = AnalysisStatus::Fail;
    else if (warnCondition)
        result.status = AnalysisStatus::Warning;
    else
        result.status = AnalysisStatus::Pass;

    if (failCondition) {
        AnalysisWarning w;
        w.code     = "KIN_SINGULAR";
        w.message  = "Configuration is near singular (condition " +
                     std::to_string (result.conditionNumber) + ").";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
    }
    else if (warnCondition) {
        AnalysisWarning w;
        w.code     = "KIN_NEAR_SINGULAR";
        w.message  = "Configuration has poor conditioning (condition " +
                     std::to_string (result.conditionNumber) + ").";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Warning;
        result.warnings.push_back (w);
    }
    return result;
}
