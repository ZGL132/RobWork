#include "KinematicAnalyzer.hpp"

#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/RPY.hpp>

#include <limits>

using namespace rws;

KinematicAnalyzer::KinematicAnalyzer () : _thresholds () {}

void KinematicAnalyzer::setThresholds (const KinematicThresholds& thresholds)
{
    _thresholds = thresholds;
}

const KinematicThresholds& KinematicAnalyzer::thresholds () const
{
    return _thresholds;
}

KinematicCurrentPoseResult KinematicAnalyzer::analyzeCurrentPose (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state) const
{
    KinematicCurrentPoseResult result;
    result.status = AnalysisStatus::Unknown;

    if (device == NULL) {
        result.status                       = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_NO_DEVICE";
        w.message  = "No device available for kinematic analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL) {
        resolvedTcpFrame = device->getEnd ();
        AnalysisWarning w;
        w.code     = "KIN_TCP_FALLBACK";
        w.message  = "No TCP frame provided; using device end as fallback.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Warning;
        result.warnings.push_back (w);
    }
    if (resolvedTcpFrame == NULL) {
        result.status = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_NO_TCP";
        w.message  = "Device has no end frame; cannot compute forward kinematics.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    result.deviceName  = device->getName ();
    result.tcpFrameName = resolvedTcpFrame->getName ();

    const rw::math::Q q = device->getQ (state);
    result.q.assign (q.e ().begin (), q.e ().end ());
    result.q.resize (static_cast< std::size_t > (q.size ()));

    try {
        const rw::math::Transform3D<> tcpTf =
            rw::kinematics::Kinematics::frameTframe (device->getBase (), resolvedTcpFrame, state);
        result.tcpPosition = {{tcpTf.P () (0), tcpTf.P () (1), tcpTf.P () (2)}};
        const rw::math::RPY<> rpy (tcpTf.R ());
        const double toDeg = 180.0 / rw::math::Pi;
        result.tcpRpyDeg = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    }
    catch (const std::exception&) {
        AnalysisWarning w;
        w.code     = "KIN_FK_FAILED";
        w.message  = "Forward kinematics failed for the selected device / TCP frame.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        result.status = AnalysisStatus::Fail;
        return result;
    }

    rw::math::Jacobian jac;
    try {
        jac = device->baseJframe (resolvedTcpFrame, state);
    }
    catch (const std::exception&) {
        AnalysisWarning w;
        w.code     = "KIN_JACOBIAN_FAILED";
        w.message  = "Failed to compute base-to-TCP Jacobian.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
    }
    if (jac.size1 () > 0 && jac.size2 () > 0) {
        result.jacobianRows = static_cast< int > (jac.size1 ());
        result.jacobianCols = static_cast< int > (jac.size2 ());
        result.jacobianRowMajor.assign (jac.e ().data (),
                                       jac.e ().data () + jac.e ().size ());
    }

    std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    result.jointLimitMargins = calculateJointLimitMargins (q, bounds);
    result.minJointLimitMargin =
        result.jointLimitMargins.empty () ? 0.0
                                         : minimumJointLimitMargin (result.jointLimitMargins);
    const AnalysisStatus limitStatus =
        classifyJointLimitMargins (q, bounds, _thresholds, &result.warnings);

    const SingularMetrics singular = calculateSingularMetrics (jac, _thresholds);
    result.singularValues  = singular.singularValues;
    result.conditionNumber = singular.conditionNumber;
    result.manipulability  = singular.manipulability;
    for (const AnalysisWarning& w : singular.warnings)
        result.warnings.push_back (w);

    if (limitStatus == AnalysisStatus::Fail || singular.status == AnalysisStatus::Fail)
        result.status = AnalysisStatus::Fail;
    else if (limitStatus == AnalysisStatus::Warning || singular.status == AnalysisStatus::Warning)
        result.status = AnalysisStatus::Warning;
    else
        result.status = AnalysisStatus::Pass;

    return result;
}
