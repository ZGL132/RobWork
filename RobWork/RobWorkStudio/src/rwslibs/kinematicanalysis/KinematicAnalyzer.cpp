#include "KinematicAnalyzer.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/invkin/IKMetaSolver.hpp>
#include <rw/invkin/JacobianIKSolver.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/Jacobian.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

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

AnalysisWarning makeWarning (const std::string& code,
                             const std::string& message,
                             AnalysisStatus severity)
{
    AnalysisWarning w;
    w.code     = code;
    w.message  = message;
    w.source   = "KinematicAnalyzer";
    w.severity = severity;
    return w;
}

std::vector< rw::math::Vector3D<> > sampleUnitDirections (int count)
{
    std::vector< rw::math::Vector3D<> > directions;
    if (count <= 0)
        return directions;
    directions.reserve (static_cast< std::size_t > (count));
    const double goldenAngle = rw::math::Pi * (3.0 - std::sqrt (5.0));
    for (int i = 0; i < count; ++i) {
        const double z = 1.0 - 2.0 * (static_cast< double > (i) + 0.5) /
                                  static_cast< double > (count);
        const double radius = std::sqrt (std::max (0.0, 1.0 - z * z));
        const double theta  = goldenAngle * static_cast< double > (i);
        directions.push_back (rw::math::Vector3D<> (
            radius * std::cos (theta), radius * std::sin (theta), z));
    }
    return directions;
}

rw::math::Rotation3D<> toolZDirectionToRotation (
    const rw::math::Vector3D<>& rawDirection, int rollIndex, int rollSamples)
{
    using rw::math::Vector3D;
    rw::math::Vector3D<> z = rawDirection;
    if (z.norm2 () < 1e-12)
        z = Vector3D<>::z ();
    z = normalize (z);

    const rw::math::Vector3D<> reference =
        std::fabs (z (2)) < 0.9 ? Vector3D<>::z () : Vector3D<>::y ();
    rw::math::Vector3D<> x = cross (reference, z);
    if (x.norm2 () < 1e-12)
        x = Vector3D<>::x ();
    x = normalize (x);
    const rw::math::Vector3D<> y = normalize (cross (z, x));
    const rw::math::Rotation3D<> base (x, y, z);

    const int rolls = std::max (1, rollSamples);
    const double roll = 2.0 * rw::math::Pi * static_cast< double > (rollIndex) /
                        static_cast< double > (rolls);
    return base * rw::math::EAA<> (Vector3D<>::z (), roll).toRotation3D ();
}

TaskPoint poseReachabilityTarget (const std::array< double, 3 >& position,
                                  const rw::math::Rotation3D<>& rotation,
                                  int directionIndex,
                                  int rollIndex)
{
    TaskPoint target;
    target.id       = "pose_reachability";
    target.name     = "Pose reachability target";
    target.position = position;
    const rw::math::RPY<> rpy (rotation);
    const double toDeg = 180.0 / rw::math::Pi;
    target.rpyDeg = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    target.note = std::string ("direction=") + std::to_string (directionIndex) +
                  ", roll=" + std::to_string (rollIndex);
    return target;
}

double meanValue (const std::vector< double >& values)
{
    if (values.empty ())
        return 0.0;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / static_cast< double > (values.size ());
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

KinematicFailureReason primaryFailureFromIk (const KinematicIkAnalysisResult& ik)
{
    if (ik.solutions.empty ())
        return KinematicFailureReason::IkNoSolution;
    bool anyCollisionFree = false;
    for (const KinematicIkSolution& s : ik.solutions) {
        if (s.inCollision)
            continue;
        anyCollisionFree = true;
        for (KinematicFailureReason r : s.failureReasons) {
            if (r == KinematicFailureReason::JointLimit)
                return KinematicFailureReason::JointLimit;
            if (r == KinematicFailureReason::Singular)
                return KinematicFailureReason::Singular;
            if (r == KinematicFailureReason::NearJointLimit)
                return KinematicFailureReason::NearJointLimit;
            if (r == KinematicFailureReason::NearSingular)
                return KinematicFailureReason::NearSingular;
        }
        return KinematicFailureReason::None;
    }
    if (!anyCollisionFree)
        return KinematicFailureReason::Collision;
    return KinematicFailureReason::None;
}

std::vector< TaskPointReachabilityResult > KinematicAnalyzer::analyzeTaskPoints (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& taskPoints,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    std::vector< TaskPointReachabilityResult > results;
    results.reserve (taskPoints.size ());
    for (const TaskPoint& point : taskPoints) {
        TaskPointReachabilityResult r;
        r.taskPoint = point;
        r.status    = AnalysisStatus::Unknown;

        if (!point.enabled) {
            r.primaryFailure = KinematicFailureReason::None;
            AnalysisWarning w;
            w.code     = "KIN_TASK_DISABLED";
            w.message  = "Task point is disabled; skipped from reachability denominator.";
            w.source   = "KinematicAnalyzer";
            w.severity = AnalysisStatus::Warning;
            r.ik.warnings.push_back (w);
            r.ik.target = point;
            results.push_back (r);
            continue;
        }

        r.ik = analyzeIk (device, tcpFrame, state, point, collisionDetector);

        if (r.ik.solutions.empty ()) {
            r.status         = AnalysisStatus::Fail;
            r.primaryFailure = KinematicFailureReason::IkNoSolution;
            r.failureReasons.push_back (KinematicFailureReason::IkNoSolution);
        }
        else {
            r.primaryFailure = primaryFailureFromIk (r.ik);
            bool allCollide   = true;
            bool anyWarn       = false;
            for (const KinematicIkSolution& s : r.ik.solutions) {
                if (!s.inCollision) {
                    allCollide = false;
                    if (s.status == AnalysisStatus::Warning)
                        anyWarn = true;
                }
            }
            if (allCollide) {
                r.status         = AnalysisStatus::Fail;
                if (r.primaryFailure == KinematicFailureReason::None)
                    r.primaryFailure = KinematicFailureReason::Collision;
                if (r.failureReasons.empty ())
                    r.failureReasons.push_back (KinematicFailureReason::Collision);
            }
            else if (r.primaryFailure == KinematicFailureReason::JointLimit ||
                     r.primaryFailure == KinematicFailureReason::Singular) {
                r.status = AnalysisStatus::Fail;
                r.failureReasons.push_back (r.primaryFailure);
            }
            else if (r.primaryFailure == KinematicFailureReason::NearJointLimit ||
                     r.primaryFailure == KinematicFailureReason::NearSingular) {
                r.status = AnalysisStatus::Warning;
                r.failureReasons.push_back (r.primaryFailure);
            }
            else {
                r.status = AnalysisStatus::Pass;
            }
            if (r.status == AnalysisStatus::Warning && !anyWarn)
                r.status = AnalysisStatus::Pass;
        }
        results.push_back (r);
    }
    return results;
}

double KinematicAnalyzer::calculateReachableRate (
    const std::vector< TaskPointReachabilityResult >& results) const
{
    std::size_t reachable = 0;
    std::size_t enabled   = 0;
    for (const TaskPointReachabilityResult& r : results) {
        if (!r.taskPoint.enabled)
            continue;
        ++enabled;
        if (r.status == AnalysisStatus::Pass || r.status == AnalysisStatus::Warning)
            ++reachable;
    }
    if (enabled == 0)
        return 0.0;
    return static_cast< double > (reachable) / static_cast< double > (enabled);
}

namespace {

// 构造单个 Q 对应的 WorkspaceSample:FK + 关节裕度 + Jacobian 指标 + 碰撞检测 +
// 状态分类。匿名命名空间内,只依赖 KinematicMetrics.h 与 RobWork 几何。
WorkspaceSample makeWorkspaceSample (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& baseState,
    const rw::math::Q& q,
    const KinematicThresholds& thresholds,
    bool checkCollision,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector)
{
    WorkspaceSample sample;

    sample.q.reserve (q.size ());
    for (std::size_t i = 0; i < q.size (); ++i)
        sample.q.push_back (q (i));

    rw::kinematics::State sampleState = baseState;
    device->setQ (q, sampleState);

    try {
        const rw::math::Transform3D<> transform =
            rw::kinematics::Kinematics::frameTframe (device->getBase (), tcpFrame.get (), sampleState);
        sample.tcpPosition[0] = transform.P () (0);
        sample.tcpPosition[1] = transform.P () (1);
        sample.tcpPosition[2] = transform.P () (2);
    }
    catch (...) {
        // FK 失败:保留 q,位置 0,inCollision 留给后续 collider 决定,status 直接 Fail。
        sample.status = AnalysisStatus::Fail;
        return sample;
    }

    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const std::vector< double > margins = calculateJointLimitMargins (q, bounds);
    sample.minJointLimitMargin =
        margins.empty () ? 0.0 : minimumJointLimitMargin (margins);

    SingularMetrics singular;
    try {
        singular = calculateSingularMetrics (device->baseJframe (tcpFrame.get (), sampleState), thresholds);
    }
    catch (...) {
        singular.status = AnalysisStatus::Fail;
    }
    sample.manipulability  = singular.manipulability;
    sample.conditionNumber = singular.conditionNumber;

    sample.inCollision = false;
    if (checkCollision && collisionDetector != NULL) {
        try {
            sample.inCollision = collisionDetector->inCollision (sampleState);
        }
        catch (...) {
            sample.inCollision = false;
        }
    }

    if (sample.inCollision)
        sample.status = AnalysisStatus::Fail;
    else if (singular.status == AnalysisStatus::Fail)
        sample.status = AnalysisStatus::Fail;
    else if (singular.status == AnalysisStatus::Warning ||
             sample.minJointLimitMargin < thresholds.nearJointLimitRatio)
        sample.status = AnalysisStatus::Warning;
    else
        sample.status = AnalysisStatus::Pass;

    return sample;
}

}    // namespace

std::vector< WorkspaceSample > KinematicAnalyzer::sampleWorkspace (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceSamplingConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    std::vector< WorkspaceSample > samples;

    if (device == NULL)
        return samples;
    if (config.sampleCount <= 0)
        return samples;
    if (tcpFrame == NULL)
        return samples;

    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const rw::math::Q& lower = bounds.first;
    const rw::math::Q& upper = bounds.second;
    const std::size_t dof = device->getDOF ();
    if (dof == 0)
        return samples;
    if (lower.size () != dof || upper.size () != dof)
        return samples;
    for (std::size_t i = 0; i < dof; ++i) {
        if (!std::isfinite (lower (i)) || !std::isfinite (upper (i)) || upper (i) <= lower (i))
            return samples;
    }

    if (config.mode == WorkspaceSamplingMode::RandomUniform) {
        std::mt19937 rng (config.randomSeed == 0 ? 1u : config.randomSeed);
        std::vector< std::uniform_real_distribution< double > > distributions;
        distributions.reserve (dof);
        for (std::size_t i = 0; i < dof; ++i)
            distributions.emplace_back (lower (i), upper (i));

        samples.reserve (static_cast< std::size_t > (config.sampleCount));
        for (int sampleIndex = 0; sampleIndex < config.sampleCount; ++sampleIndex) {
            rw::math::Q q (dof);
            for (std::size_t j = 0; j < dof; ++j)
                q (j) = distributions[j] (rng);
            samples.push_back (makeWorkspaceSample (
                device, tcpFrame, state, q, _thresholds,
                config.checkCollision, collisionDetector));
        }
        return samples;
    }

    // WorkspaceSamplingMode::Grid:每关节等距 steps,steps<=1 时取中点;总组合
    // 过大时按字典序截断到 config.sampleCount。
    const int steps = std::max (1, config.gridStepsPerJoint);
    std::size_t total = 1;
    for (std::size_t i = 0; i < dof; ++i) {
        if (total > static_cast< std::size_t > (config.sampleCount))
            break;
        total *= static_cast< std::size_t > (steps);
    }
    const std::size_t target =
        std::min (static_cast< std::size_t > (config.sampleCount), total);

    samples.reserve (target);
    for (std::size_t index = 0; index < target; ++index) {
        std::size_t cursor = index;
        rw::math::Q q (dof);
        for (std::size_t joint = 0; joint < dof; ++joint) {
            const std::size_t stepIndex = steps <= 1 ? 0u : (cursor % static_cast< std::size_t > (steps));
            cursor /= static_cast< std::size_t > (steps);
            if (steps <= 1) {
                q (joint) = 0.5 * (lower (joint) + upper (joint));
            }
            else {
                const double ratio = static_cast< double > (stepIndex) /
                                     static_cast< double > (steps - 1);
                q (joint) = lower (joint) + ratio * (upper (joint) - lower (joint));
            }
        }
        samples.push_back (makeWorkspaceSample (
            device, tcpFrame, state, q, _thresholds,
            config.checkCollision, collisionDetector));
    }
    return samples;
}

std::vector< PoseReachabilitySample > KinematicAnalyzer::analyzePoseReachability (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< std::array< double, 3 > >& positions,
    const PoseReachabilityConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    std::vector< PoseReachabilitySample > results;
    results.reserve (positions.size ());

    const int directionCount = std::max (0, config.directionSamples);
    const int rollCount      = directionCount == 0 ? 0 : std::max (1, config.rollSamples);
    const int totalDirections = directionCount * rollCount;
    const std::vector< rw::math::Vector3D<> > directions =
        sampleUnitDirections (directionCount);

    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL && device != NULL)
        resolvedTcpFrame = device->getEnd ();

    for (const std::array< double, 3 >& position : positions) {
        PoseReachabilitySample sample;
        sample.position          = position;
        sample.sampledDirections = totalDirections;

        if (device == NULL || resolvedTcpFrame == NULL || totalDirections == 0) {
            sample.status = AnalysisStatus::Fail;
            results.push_back (sample);
            continue;
        }

        for (int directionIndex = 0; directionIndex < directionCount; ++directionIndex) {
            for (int rollIndex = 0; rollIndex < rollCount; ++rollIndex) {
                const rw::math::Rotation3D<> rotation =
                    toolZDirectionToRotation (
                        directions[static_cast< std::size_t > (directionIndex)],
                        rollIndex, rollCount);
                const TaskPoint target =
                    poseReachabilityTarget (position, rotation, directionIndex, rollIndex);
                const KinematicIkAnalysisResult ik = analyzeIk (
                    device, resolvedTcpFrame, state, target,
                    config.checkCollision ? collisionDetector : NULL);

                bool reachable = false;
                for (const KinematicIkSolution& solution : ik.solutions) {
                    if (solution.inCollision)
                        continue;
                    if (solution.status == AnalysisStatus::Pass ||
                        solution.status == AnalysisStatus::Warning) {
                        reachable = true;
                        break;
                    }
                }
                if (reachable)
                    ++sample.reachableDirections;
            }
        }

        sample.coverage =
            totalDirections == 0 ? 0.0 :
            static_cast< double > (sample.reachableDirections) /
                static_cast< double > (totalDirections);
        if (sample.reachableDirections == 0)
            sample.status = AnalysisStatus::Fail;
        else if (sample.reachableDirections == totalDirections)
            sample.status = AnalysisStatus::Pass;
        else
            sample.status = AnalysisStatus::Warning;
        results.push_back (sample);
    }
    return results;
}

KinematicAnalysisResult KinematicAnalyzer::buildAggregateResult (
    const KinematicCurrentPoseResult& currentPose,
    const std::vector< TaskPointReachabilityResult >& taskPointResults,
    const std::vector< WorkspaceSample >& workspaceSamples,
    const std::vector< PoseReachabilitySample >& poseReachability) const
{
    KinematicAnalysisResult result;
    result.header.pluginName    = "KinematicAnalysis";
    result.header.pluginVersion = "1.0.0";
    result.currentPose          = currentPose;
    result.taskPointResults     = taskPointResults;
    result.workspaceSamples     = workspaceSamples;
    result.poseReachability     = poseReachability;
    result.reachableRate        = calculateReachableRate (taskPointResults);

    result.status = currentPose.status;
    for (const TaskPointReachabilityResult& task : taskPointResults)
        result.status = worstStatus (result.status, task.status);
    for (const WorkspaceSample& sample : workspaceSamples)
        result.status = worstStatus (result.status, sample.status);
    for (const PoseReachabilitySample& sample : poseReachability)
        result.status = worstStatus (result.status, sample.status);

    std::vector< double > manipulabilityValues;
    manipulabilityValues.reserve (workspaceSamples.size () + 1);
    if (currentPose.manipulability > 0.0)
        manipulabilityValues.push_back (currentPose.manipulability);
    for (const WorkspaceSample& sample : workspaceSamples) {
        if (sample.manipulability > 0.0)
            manipulabilityValues.push_back (sample.manipulability);
        if (sample.status == AnalysisStatus::Fail && sample.inCollision)
            result.warnings.push_back (makeWarning (
                "KIN_WORKSPACE_COLLISION",
                "At least one workspace sample is in collision.",
                AnalysisStatus::Warning));
        if (sample.status == AnalysisStatus::Warning)
            result.jointLimitWarnings.push_back (makeWarning (
                "KIN_WORKSPACE_QUALITY_WARNING",
                "A workspace sample is near a joint limit or singularity.",
                AnalysisStatus::Warning));
    }

    if (!manipulabilityValues.empty ()) {
        std::sort (manipulabilityValues.begin (), manipulabilityValues.end ());
        MetricValue minMetric;
        minMetric.name  = "manipulability_min";
        minMetric.value = manipulabilityValues.front ();
        MetricValue maxMetric;
        maxMetric.name  = "manipulability_max";
        maxMetric.value = manipulabilityValues.back ();
        MetricValue meanMetric;
        meanMetric.name  = "manipulability_mean";
        meanMetric.value = meanValue (manipulabilityValues);
        MetricValue p10Metric;
        p10Metric.name = "manipulability_p10";
        const std::size_t p10Index =
            static_cast< std::size_t > (0.1 * static_cast< double > (manipulabilityValues.size () - 1));
        p10Metric.value = manipulabilityValues[p10Index];
        result.manipulabilityMap.push_back (minMetric);
        result.manipulabilityMap.push_back (maxMetric);
        result.manipulabilityMap.push_back (meanMetric);
        result.manipulabilityMap.push_back (p10Metric);
    }

    for (const AnalysisWarning& warning : currentPose.warnings) {
        result.warnings.push_back (warning);
        if (warning.code.find ("SINGULAR") != std::string::npos ||
            warning.code.find ("CONDITION") != std::string::npos)
            result.singularityWarnings.push_back (warning);
        if (warning.code.find ("JOINT") != std::string::npos ||
            warning.code.find ("LIMIT") != std::string::npos)
            result.jointLimitWarnings.push_back (warning);
    }

    return result;
}
