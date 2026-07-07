#include "KinematicAnalyzer.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/invkin/IKMetaSolver.hpp>
#include <rw/invkin/JacobianIKSolver.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

KinematicAnalyzer::KinematicAnalyzer () : _thresholds () {}

namespace {

AnalysisStatus worstStatus (AnalysisStatus lhs, AnalysisStatus rhs)
{
    if (lhs == AnalysisStatus::Fail || rhs == AnalysisStatus::Fail)
        return AnalysisStatus::Fail;
    if (lhs == AnalysisStatus::Warning || rhs == AnalysisStatus::Warning)
        return AnalysisStatus::Warning;
    if (lhs == AnalysisStatus::Pass || rhs == AnalysisStatus::Pass)
        return AnalysisStatus::Pass;
    return AnalysisStatus::Unknown;
}

rw::math::Transform3D<> taskPointToTransform (const TaskPoint& target)
{
    const double toRad = rw::math::Pi / 180.0;
    return rw::math::Transform3D<> (
        rw::math::Vector3D<> (target.position[0], target.position[1], target.position[2]),
        rw::math::RPY<> (target.rpyDeg[0] * toRad,
                         target.rpyDeg[1] * toRad,
                         target.rpyDeg[2] * toRad));
}

double qDistance (const rw::math::Q& lhs, const rw::math::Q& rhs)
{
    if (lhs.size () != rhs.size ())
        return std::numeric_limits< double >::infinity ();
    return (lhs - rhs).norm2 ();
}

double positionError (const rw::math::Transform3D<>& actual,
                      const rw::math::Transform3D<>& target)
{
    return (actual.P () - target.P ()).norm2 ();
}

double orientationErrorDeg (const rw::math::Transform3D<>& actual,
                            const rw::math::Transform3D<>& target)
{
    const rw::math::Rotation3D<> diff = inverse (target.R ()) * actual.R ();
    const rw::math::EAA<> eaa (diff);
    return std::fabs (eaa.angle ()) * 180.0 / rw::math::Pi;
}

std::vector< double > qToVector (const rw::math::Q& q)
{
    std::vector< double > values;
    values.reserve (q.size ());
    for (std::size_t i = 0; i < q.size (); ++i)
        values.push_back (q (i));
    return values;
}

bool lexicographicQLess (const std::vector< double >& lhs, const std::vector< double >& rhs)
{
    return std::lexicographical_compare (lhs.begin (), lhs.end (), rhs.begin (), rhs.end ());
}

}    // namespace

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

KinematicIkAnalysisResult KinematicAnalyzer::analyzeIk (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const TaskPoint& target,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    KinematicIkAnalysisResult result;
    result.target = target;
    result.status = AnalysisStatus::Unknown;

    if (device == NULL) {
        result.status = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_NO_DEVICE";
        w.message  = "No device available for IK analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL)
        resolvedTcpFrame = device->getEnd ();
    if (resolvedTcpFrame == NULL) {
        result.status = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_NO_TCP";
        w.message  = "No TCP frame available for IK analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    const rw::math::Transform3D<> targetBaseTtcp = taskPointToTransform (target);
    std::vector< rw::math::Q > rawSolutions;
    try {
        rw::invkin::JacobianIKSolver::Ptr solver =
            rw::core::ownedPtr (new rw::invkin::JacobianIKSolver (device, resolvedTcpFrame, state));
        rw::invkin::IKMetaSolver metaSolver (solver, device, collisionDetector);
        metaSolver.setStopAtFirst (false);
        rawSolutions = metaSolver.solve (targetBaseTtcp, state);
    }
    catch (const std::exception& ex) {
        result.status = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_IK_SOLVER_ERROR";
        w.message  = std::string ("IK solver failed: ") + ex.what ();
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }
    catch (...) {
        result.status = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_IK_SOLVER_ERROR";
        w.message  = "IK solver failed with an unknown error.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    if (rawSolutions.empty ()) {
        result.status = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_IK_NO_SOLUTION";
        w.message  = "No IK solution found for the target pose.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    const rw::math::Q currentQ = device->getQ (state);
    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    for (const rw::math::Q& q : rawSolutions) {
        KinematicIkSolution solution;
        solution.q = qToVector (q);
        solution.distanceToCurrentQ = qDistance (currentQ, q);

        rw::kinematics::State solutionState = state;
        device->setQ (q, solutionState);

        const std::vector< double > margins = calculateJointLimitMargins (q, bounds);
        solution.minJointLimitMargin =
            margins.empty () ? 0.0 : minimumJointLimitMargin (margins);

        std::vector< AnalysisWarning > limitWarnings;
        const AnalysisStatus limitStatus =
            classifyJointLimitMargins (q, bounds, _thresholds, &limitWarnings);
        if (limitStatus == AnalysisStatus::Fail)
            solution.failureReasons.push_back (KinematicFailureReason::JointLimit);
        else if (limitStatus == AnalysisStatus::Warning)
            solution.failureReasons.push_back (KinematicFailureReason::NearJointLimit);

        const rw::math::Transform3D<> actualBaseTtcp =
            rw::kinematics::Kinematics::frameTframe (device->getBase (), resolvedTcpFrame, solutionState);
        solution.positionErrorMeters = positionError (actualBaseTtcp, targetBaseTtcp);
        solution.orientationErrorDeg = orientationErrorDeg (actualBaseTtcp, targetBaseTtcp);

        const SingularMetrics singular =
            calculateSingularMetrics (device->baseJframe (resolvedTcpFrame, solutionState), _thresholds);
        solution.manipulability  = singular.manipulability;
        solution.conditionNumber = singular.conditionNumber;
        if (singular.status == AnalysisStatus::Fail)
            solution.failureReasons.push_back (KinematicFailureReason::Singular);
        else if (singular.status == AnalysisStatus::Warning)
            solution.failureReasons.push_back (KinematicFailureReason::NearSingular);

        if (collisionDetector != NULL) {
            rw::proximity::CollisionDetector::QueryResult queryResult;
            solution.inCollision = collisionDetector->inCollision (solutionState, &queryResult);
            if (solution.inCollision)
                solution.failureReasons.push_back (KinematicFailureReason::Collision);
        }

        solution.score =
            (solution.inCollision ? 1000000.0 : 0.0) +
            solution.positionErrorMeters * 1000.0 +
            solution.orientationErrorDeg +
            solution.distanceToCurrentQ -
            solution.minJointLimitMargin -
            solution.manipulability;

        solution.status = AnalysisStatus::Pass;
        solution.status = worstStatus (solution.status, limitStatus);
        solution.status = worstStatus (solution.status, singular.status);
        if (solution.inCollision)
            solution.status = AnalysisStatus::Fail;

        result.solutions.push_back (solution);
    }

    sortIkSolutionsForDisplay (result.solutions);
    result.status = AnalysisStatus::Fail;
    for (const KinematicIkSolution& solution : result.solutions) {
        if (solution.status == AnalysisStatus::Pass) {
            result.status = AnalysisStatus::Pass;
            break;
        }
        if (solution.status == AnalysisStatus::Warning)
            result.status = AnalysisStatus::Warning;
    }
    return result;
}

void rws::sortIkSolutionsForDisplay (std::vector< KinematicIkSolution >& solutions)
{
    std::sort (solutions.begin (), solutions.end (),
               [] (const KinematicIkSolution& lhs, const KinematicIkSolution& rhs) {
                   if (lhs.inCollision != rhs.inCollision)
                       return !lhs.inCollision;
                   if (lhs.positionErrorMeters != rhs.positionErrorMeters)
                       return lhs.positionErrorMeters < rhs.positionErrorMeters;
                   if (lhs.orientationErrorDeg != rhs.orientationErrorDeg)
                       return lhs.orientationErrorDeg < rhs.orientationErrorDeg;
                   if (lhs.minJointLimitMargin != rhs.minJointLimitMargin)
                       return lhs.minJointLimitMargin > rhs.minJointLimitMargin;
                   if (lhs.manipulability != rhs.manipulability)
                       return lhs.manipulability > rhs.manipulability;
                   if (lhs.distanceToCurrentQ != rhs.distanceToCurrentQ)
                       return lhs.distanceToCurrentQ < rhs.distanceToCurrentQ;
                   return lexicographicQLess (lhs.q, rhs.q);
               });
}
