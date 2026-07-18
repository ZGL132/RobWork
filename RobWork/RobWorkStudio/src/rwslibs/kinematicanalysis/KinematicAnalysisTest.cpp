#include "KinematicAnalysisTypes.hpp"
#include "KinematicMetrics.hpp"
#include "KinematicAnalyzer.hpp"
#include "TaskPointResolver.hpp"
#include "TaskPointUiLogic.hpp"
#include "TaskPointTableModel.hpp"
#include "KinematicAnalysisVisualizationTypes.hpp"
#include "KinematicAnalysisWorkspace.hpp"
#include "KinematicAnalysisPoseReachability.hpp"

#include <QCoreApplication>

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Jacobian.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/SerialDevice.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

// 测试程序:不依赖完整 RobWorkStudio 渲染环境,只验证分析器/指标/类型层
// 的纯算法正确性(QCoreApplication 仅为 Q_OBJECT 机制 / 元对象而存在)。
// 用 main 的首个命令行参数选择子套件,默认 "all" 跑全套。
// 每个子套件返回 0 表示通过,非 0 表示有失败用例。

// 浮点近似比较:绝对差 ≤ eps。eps 默认 1e-12,适合整型解析/无浮点误差场景。
static bool nearlyEqual (double lhs, double rhs, double eps = 1e-12)
{
    return std::fabs (lhs - rhs) <= eps;
}

// 失败:打 stderr 并返回 1,便于 CTest 直接收到非 0 退出码。
static int fail (const std::string& message)
{
    std::cerr << message << std::endl;
    return 1;
}

// 通用断言:失败时附带 "what" 描述。
static int require (bool condition, const std::string& what)
{
    if (!condition)
        return fail ("requirement failed: " + what);
    return 0;
}

// 浮点断言:失败时同时打印 expected / actual。
static int assertNear (double actual, double expected, double eps, const std::string& what)
{
    if (!nearlyEqual (actual, expected, eps))
        return fail ("expected " + what + " = " + std::to_string (expected) +
                     " but got " + std::to_string (actual));
    return 0;
}

static rw::models::SerialDevice::Ptr makeTestKukaIIWA (
    rw::kinematics::StateStructure& stateStructure)
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    const Frame::Ptr base =
        rw::core::ownedPtr (new FixedFrame ("Base", Transform3D<>::identity ()));
    const Joint::Ptr joint1 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint1", Transform3D<> (Vector3D<> (0, 0, 0.158))));
    const Joint::Ptr joint2 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint2", Transform3D<> (Vector3D<> (0, 0, 0.182),
                                                                        RPY<> (0, 0, -Pi / 2.0))));
    const Joint::Ptr joint3 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint3", Transform3D<> (Vector3D<> (0, -0.182, 0),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Joint::Ptr joint4 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint4", Transform3D<> (Vector3D<> (0, 0, 0.218),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Joint::Ptr joint5 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint5", Transform3D<> (Vector3D<> (0, 0.182, 0),
                                                                        RPY<> (0, 0, -Pi / 2.0))));
    const Joint::Ptr joint6 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint6", Transform3D<> (Vector3D<> (0, 0, 0.218),
                                                                        RPY<> (0, 0, -Pi / 2.0))));
    const Joint::Ptr joint7 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint7", Transform3D<> (Vector3D<>::zero (),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Frame::Ptr end =
        rw::core::ownedPtr (new FixedFrame ("TCP", Transform3D<> (Vector3D<> (0, 0, 0.126))));

    stateStructure.addFrame (base);
    stateStructure.addFrame (joint1, base);
    stateStructure.addFrame (joint2, joint1);
    stateStructure.addFrame (joint3, joint2);
    stateStructure.addFrame (joint4, joint3);
    stateStructure.addFrame (joint5, joint4);
    stateStructure.addFrame (joint6, joint5);
    stateStructure.addFrame (joint7, joint6);
    stateStructure.addFrame (end, joint7);

    rw::kinematics::State state = stateStructure.getDefaultState ();
    const rw::models::SerialDevice::Ptr device =
        rw::core::ownedPtr (new rw::models::SerialDevice (base.get (), end.get (), "KukaIIWA", state));
    std::pair< Q, Q > bounds;
    bounds.first = Q (7, -170 * Deg2Rad, -120 * Deg2Rad, -170 * Deg2Rad, -120 * Deg2Rad,
                      -170 * Deg2Rad, -120 * Deg2Rad, -175 * Deg2Rad);
    bounds.second = -bounds.first;
    device->setBounds (bounds);
    return device;
}

static rw::models::SerialDevice::Ptr makeGenericSixAxis (
    rw::kinematics::StateStructure& stateStructure)
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    const Frame::Ptr base =
        rw::core::ownedPtr (new FixedFrame ("Base", Transform3D<>::identity ()));
    const Joint::Ptr joint1 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint1", Transform3D<> (Vector3D<> (0, 0, 0.35))));
    const Joint::Ptr joint2 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint2", Transform3D<> (Vector3D<> (0.12, 0, 0),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Joint::Ptr joint3 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint3", Transform3D<> (Vector3D<> (0.52, 0, 0))));
    const Joint::Ptr joint4 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint4", Transform3D<> (Vector3D<> (0.42, 0, 0),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Joint::Ptr joint5 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint5", Transform3D<> (Vector3D<> (0, 0, 0.38),
                                                                        RPY<> (0, 0, -Pi / 2.0))));
    const Joint::Ptr joint6 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint6", Transform3D<> (Vector3D<> (0, 0, 0.12),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Frame::Ptr end =
        rw::core::ownedPtr (new FixedFrame ("TCP", Transform3D<>::identity ()));

    stateStructure.addFrame (base);
    stateStructure.addFrame (joint1, base);
    stateStructure.addFrame (joint2, joint1);
    stateStructure.addFrame (joint3, joint2);
    stateStructure.addFrame (joint4, joint3);
    stateStructure.addFrame (joint5, joint4);
    stateStructure.addFrame (joint6, joint5);
    stateStructure.addFrame (end, joint6);

    rw::kinematics::State state = stateStructure.getDefaultState ();
    const rw::models::SerialDevice::Ptr device =
        rw::core::ownedPtr (new rw::models::SerialDevice (base.get (), end.get (),
                                                          "GenericSixAxis", state));
    std::pair< Q, Q > bounds;
    bounds.first = Q (6, -Pi, -120.0 * Deg2Rad, -150.0 * Deg2Rad,
                      -Pi, -120.0 * Deg2Rad, -2.0 * Pi);
    bounds.second = Q (6, Pi, 120.0 * Deg2Rad, 150.0 * Deg2Rad,
                       Pi, 120.0 * Deg2Rad, 2.0 * Pi);
    device->setBounds (bounds);
    return device;
}

// 子套件 1:基础类型默认值 + toString。
//   - 校验 KinematicThresholds 的默认值与 KinematicAnalysisTypes 注释一致;
//   - 校验 KinematicFailureReason 的相等语义;
//   - 校验 toString 的几个代表值。
static int testTypes ()
{
    rws::KinematicThresholds thresholds;
    if (const int rc = assertNear (thresholds.nearJointLimitRatio, 0.05, 1e-12, "nearJointLimitRatio"))
        return rc;
    if (const int rc = assertNear (thresholds.conditionWarning, 100.0, 1e-12, "conditionWarning"))
        return rc;
    if (const int rc = assertNear (thresholds.conditionFail, 1000.0, 1e-12, "conditionFail"))
        return rc;
    if (const int rc = assertNear (thresholds.singularValueWarning, 1e-4, 1e-12, "singularValueWarning"))
        return rc;
    if (const int rc = assertNear (thresholds.manipulabilityWarning, 1e-5, 1e-12, "manipulabilityWarning"))
        return rc;
    if (const int rc =
            assertNear (thresholds.positionToleranceMeters, 0.001, 1e-12, "positionToleranceMeters"))
        return rc;
    if (const int rc =
            assertNear (thresholds.orientationToleranceDeg, 1.0, 1e-12, "orientationToleranceDeg"))
        return rc;

    rws::KinematicAnalysisResult result;
    result.header.pluginName = "KinematicAnalysis";
    result.reachableRate    = 0.75;
    result.workspaceSamples.push_back (rws::WorkspaceSample ());
    rws::KinematicFailureReason reason = rws::KinematicFailureReason::Collision;
    if (reason != rws::KinematicFailureReason::Collision)
        return fail ("KinematicFailureReason equality comparison broke.");

    if (const int rc = require (result.header.pluginName == "KinematicAnalysis", "result.pluginName"))
        return rc;
    if (const int rc = assertNear (result.reachableRate, 0.75, 1e-12, "result.reachableRate"))
        return rc;
    if (const int rc = require (result.workspaceSamples.size () == 1, "result.workspaceSamples size"))
        return rc;
    if (const int rc = require (std::string (rws::toString (rws::KinematicFailureReason::Singular)) ==
                                "Singular",
                                "toString Singular"))
        return rc;
    if (const int rc = require (
            std::string (rws::toString (rws::KinematicFailureReason::TargetResidual)) ==
                "TargetResidual",
            "toString TargetResidual"))
        return rc;
    return 0;
}

static int testTargetValidationAndResidual ()
{
    rws::KinematicThresholds thresholds;
    std::vector< rws::KinematicFailureReason > reasons;
    std::vector< rws::AnalysisWarning > warnings;

    rws::AnalysisStatus status = rws::classifyTargetResidual (
        0.0005, 0.5,
        thresholds.positionToleranceMeters,
        thresholds.orientationToleranceDeg,
        &reasons, &warnings);
    if (const int rc = require (status == rws::AnalysisStatus::Pass,
                                "residual inside tolerance passes"))
        return rc;
    if (const int rc = require (reasons.empty () && warnings.empty (),
                                "passing residual has no diagnostics"))
        return rc;

    status = rws::classifyTargetResidual (
        0.002, 0.5,
        thresholds.positionToleranceMeters,
        thresholds.orientationToleranceDeg,
        &reasons, &warnings);
    if (const int rc = require (status == rws::AnalysisStatus::Fail,
                                "position residual outside tolerance fails"))
        return rc;
    if (const int rc = require (
            reasons.size () == 1 &&
                reasons.front () == rws::KinematicFailureReason::TargetResidual,
            "target residual failure reason is recorded"))
        return rc;
    if (const int rc = require (!warnings.empty () && warnings.front ().code == "KIN_TARGET_RESIDUAL",
                                "target residual warning is recorded"))
        return rc;

    reasons.clear ();
    warnings.clear ();
    status = rws::classifyTargetResidual (
        std::numeric_limits< double >::quiet_NaN (), 0.0,
        thresholds.positionToleranceMeters,
        thresholds.orientationToleranceDeg,
        &reasons, &warnings);
    if (const int rc = require (status == rws::AnalysisStatus::Fail,
                                "non-finite residual fails closed"))
        return rc;

    rws::KinematicAnalyzer analyzer;
    rw::kinematics::State state;
    rws::TaskPoint invalidTarget;
    invalidTarget.position[0] = std::numeric_limits< double >::quiet_NaN ();
    const rws::KinematicIkAnalysisResult invalid =
        analyzer.analyzeIk (NULL, NULL, state, invalidTarget, NULL);
    if (const int rc = require (invalid.status == rws::AnalysisStatus::Fail,
                                "non-finite target fails"))
        return rc;
    if (const int rc = require (
            invalid.failureReason == rws::KinematicFailureReason::InvalidTarget,
            "non-finite target preserves InvalidTarget"))
        return rc;

    rws::TaskPoint validTarget;
    const rws::KinematicIkAnalysisResult noDevice =
        analyzer.analyzeIk (NULL, NULL, state, validTarget, NULL);
    if (const int rc = require (
            noDevice.failureReason == rws::KinematicFailureReason::NoDevice,
            "valid target without a device preserves NoDevice"))
        return rc;
    return 0;
}

// 子套件 2:analyzeCurrentPose 在 NULL device 下的降级路径,以及阈值存取。
static int testPoseUnitConversions ()
{
    if (const int rc = assertNear (
            rws::displayLengthFromMeters (0.125, rws::KinematicLengthUnit::Millimeters),
            125.0, 1e-12, "meters to millimeters"))
        return rc;
    if (const int rc = assertNear (
            rws::metersFromDisplayLength (12.5, rws::KinematicLengthUnit::Centimeters),
            0.125, 1e-12, "centimeters to meters"))
        return rc;
    if (const int rc = assertNear (
            rws::displayAngleFromDegrees (180.0, rws::KinematicAngleUnit::Radians),
            rw::math::Pi, 1e-12, "degrees to radians"))
        return rc;
    if (const int rc = assertNear (
            rws::degreesFromDisplayAngle (0.25, rws::KinematicAngleUnit::Turns),
            90.0, 1e-12, "turns to degrees"))
        return rc;
    if (const int rc = require (
            std::string (rws::toString (rws::KinematicLengthUnit::Millimeters)) ==
                "Millimeters",
            "length unit string"))
        return rc;
    if (const int rc = require (
            std::string (rws::unitSuffix (rws::KinematicAngleUnit::Radians)) == "rad",
            "angle unit suffix"))
        return rc;
    return 0;
}

static int testCurrentPose ()
{
    rws::KinematicAnalyzer analyzer;
    rw::kinematics::State emptyState;
    const rws::KinematicCurrentPoseResult result =
        analyzer.analyzeCurrentPose (NULL, NULL, emptyState);
    if (const int rc =
            require (result.status == rws::AnalysisStatus::Fail, "null device triggers Fail"))
        return rc;
    if (const int rc = require (!result.warnings.empty (), "null device emits a warning"))
        return rc;
    if (const int rc = require (result.deviceName.empty (), "no device name set"))
        return rc;

    analyzer.setThresholds (rws::KinematicThresholds ());
    const rws::KinematicThresholds& t = analyzer.thresholds ();
    if (const int rc = assertNear (t.nearJointLimitRatio, 0.05, 1e-12, "thresholds preserved"))
        return rc;
    return 0;
}

// 子套件 3:关节裕度 + SVD 指标的纯算法正确性。
//   - q=5   → margin=0.5 → Pass,无警告;
//   - q=0.2 → margin=0.02 → Warning,至少一条警告;
//   - q=-1  → 超出 [0,10] → Fail,至少一条警告;
//   - 对角 J=[4,2]  → σ={4,2}, κ=2, manipulability=8, Pass;
//   - 对角 J=[4,0]  → κ=∞, Fail, 至少一条警告。
static int testMetrics ()
{
    using namespace rw::math;
    rws::KinematicThresholds thresholds;

    // Joint-limit margins: bounds [0, 10], q [5] => margin 0.5; q [0.2] => 0.02.
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, 5.0);
        std::pair< Q, Q > bounds = {lo, hi};
        const std::vector< double > margins = rws::calculateJointLimitMargins (q, bounds);
        if (const int rc = require (margins.size () == 1, "margin size"))
            return rc;
        if (const int rc = assertNear (margins.front (), 0.5, 1e-9, "margin[0.5]"))
            return rc;
        if (const int rc =
                assertNear (rws::minimumJointLimitMargin (margins), 0.5, 1e-9, "min margin[0.5]"))
            return rc;
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Pass, "classify Pass for q=5"))
            return rc;
        if (const int rc = require (warnings.empty (), "no warnings for q=5"))
            return rc;
    }
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, 0.2);
        std::pair< Q, Q > bounds = {lo, hi};
        const std::vector< double > margins = rws::calculateJointLimitMargins (q, bounds);
        if (const int rc = require (margins.size () == 1, "margin size"))
            return rc;
        if (const int rc = assertNear (margins.front (), 0.02, 1e-9, "margin[0.02]"))
            return rc;
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Warning, "classify Warning for q=0.2"))
            return rc;
        if (const int rc = require (!warnings.empty (), "warning emitted for q=0.2"))
            return rc;
    }
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, -1.0);
        std::pair< Q, Q > bounds = {lo, hi};
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Fail, "classify Fail outside bounds"))
            return rc;
        if (const int rc = require (!warnings.empty (), "fail warning emitted outside bounds"))
            return rc;
    }

    // SVD: condition 2 / manipulability 8 for diag [4, 2].
    {
        Jacobian j = Jacobian::zero (3, 2);
        // Set up a diagonal-ish 2-row x 2-col proxy by setting two rows to the identity.
        // We use a 3x2 zero Jacobian and only validate condition via direct matrix
        // construction. Cleaner: build a 2x2 Jacobian via Math::zero and assign.
        Jacobian j2(2, 2);
        j2 (0, 0) = 4.0;
        j2 (0, 1) = 0.0;
        j2 (1, 0) = 0.0;
        j2 (1, 1) = 2.0;
        (void) j;
        const rws::SingularMetrics m = rws::calculateSingularMetrics (j2, thresholds);
        if (const int rc =
                require (m.singularValues.size () == 2, "singularValues size"))
            return rc;
        if (const int rc = assertNear (m.singularValues[0], 4.0, 1e-6, "sigmaMax"))
            return rc;
        if (const int rc = assertNear (m.singularValues[1], 2.0, 1e-6, "sigmaMin"))
            return rc;
        if (const int rc = assertNear (m.conditionNumber, 2.0, 1e-6, "conditionNumber"))
            return rc;
        if (const int rc = assertNear (m.manipulability, 8.0, 1e-6, "manipulability"))
            return rc;
        if (const int rc = require (m.status == rws::AnalysisStatus::Pass, "metric status Pass"))
            return rc;
    }
    // SVD: singular config [4, 0] => infinite condition + Fail.
    {
        Jacobian j(2, 2);
        j (0, 0) = 4.0;
        j (0, 1) = 0.0;
        j (1, 0) = 0.0;
        j (1, 1) = 0.0;
        const rws::SingularMetrics m = rws::calculateSingularMetrics (j, thresholds);
        if (const int rc =
                require (std::isinf (m.conditionNumber), "infinite conditionNumber"))
            return rc;
        if (const int rc = require (m.status == rws::AnalysisStatus::Fail, "metric status Fail"))
            return rc;
        if (const int rc =
                require (!m.warnings.empty (), "warning emitted when singular"))
            return rc;
    }
    return 0;
}

// 子套件 4:sortIkSolutionsForDisplay 的优先级链。
// 准备 4 条解:colliding / worseResidual / betterMargin / lowerDistance;
// 排序后应当是 lowerDistance → betterMargin → worseResidual → colliding。
static int testIkRanking ()
{
    std::vector< rws::KinematicIkSolution > solutions;

    rws::KinematicIkSolution colliding;
    colliding.inCollision = true;
    colliding.positionErrorMeters = 0.0;
    colliding.orientationErrorDeg = 0.0;
    colliding.minJointLimitMargin = 0.8;
    colliding.manipulability = 10.0;
    colliding.distanceToCurrentQ = 0.1;
    colliding.q = {9.0};

    rws::KinematicIkSolution worseResidual;
    worseResidual.inCollision = false;
    worseResidual.positionErrorMeters = 0.1;
    worseResidual.orientationErrorDeg = 0.0;
    worseResidual.minJointLimitMargin = 0.9;
    worseResidual.manipulability = 20.0;
    worseResidual.distanceToCurrentQ = 0.1;
    worseResidual.q = {2.0};

    rws::KinematicIkSolution betterMargin;
    betterMargin.inCollision = false;
    betterMargin.positionErrorMeters = 0.0;
    betterMargin.orientationErrorDeg = 0.0;
    betterMargin.minJointLimitMargin = 0.7;
    betterMargin.manipulability = 1.0;
    betterMargin.distanceToCurrentQ = 0.5;
    betterMargin.q = {1.0};

    rws::KinematicIkSolution lowerDistance;
    lowerDistance.inCollision = false;
    lowerDistance.positionErrorMeters = 0.0;
    lowerDistance.orientationErrorDeg = 0.0;
    lowerDistance.minJointLimitMargin = 0.7;
    lowerDistance.manipulability = 1.0;
    lowerDistance.distanceToCurrentQ = 0.2;
    lowerDistance.q = {0.0};

    solutions.push_back (colliding);
    solutions.push_back (worseResidual);
    solutions.push_back (betterMargin);
    solutions.push_back (lowerDistance);

    rws::sortIkSolutionsForDisplay (solutions);

    if (const int rc = require (solutions.size () == 4, "IK ranking preserves size"))
        return rc;
    if (const int rc = assertNear (solutions[0].q[0], 0.0, 1e-12, "lower distance first"))
        return rc;
    if (const int rc = assertNear (solutions[1].q[0], 1.0, 1e-12, "same quality next"))
        return rc;
    if (const int rc = assertNear (solutions[2].q[0], 2.0, 1e-12, "worse residual after"))
        return rc;
    if (const int rc = assertNear (solutions[3].q[0], 9.0, 1e-12, "colliding last"))
        return rc;

    std::vector< rw::math::Q > candidates;
    rws::addUniqueIkCandidate (candidates, rw::math::Q (2, 0.0, 1.0), 1e-4);
    rws::addUniqueIkCandidate (candidates, rw::math::Q (2, 0.0, 1.0 + 5e-5), 1e-4);
    rws::addUniqueIkCandidate (candidates, rw::math::Q (2, 0.0, 1.1), 1e-4);
    if (const int rc = require (candidates.size () == 2,
                                "near-duplicate IK candidates are merged"))
        return rc;

    std::vector< rws::KinematicIkSolution > validity;
    rws::KinematicIkSolution pass;
    pass.status = rws::AnalysisStatus::Pass;
    validity.push_back (pass);
    rws::KinematicIkSolution warning;
    warning.status = rws::AnalysisStatus::Warning;
    validity.push_back (warning);
    rws::KinematicIkSolution fail;
    fail.status = rws::AnalysisStatus::Fail;
    validity.push_back (fail);
    if (const int rc = require (rws::countUsableIkSolutions (validity) == 2,
                                "usable IK count excludes Fail candidates"))
        return rc;

    // Task 1 Step 1:summarizeIkSolutions 的状态计数。
    const rws::KinematicIkSummary summary = rws::summarizeIkSolutions (validity);
    if (const int rc = require (summary.passCount == 1, "IK summary pass count"))
        return rc;
    if (const int rc = require (summary.warningCount == 1, "IK summary warning count"))
        return rc;
    if (const int rc = require (summary.failCount == 1, "IK summary fail count"))
        return rc;
    if (const int rc = require (summary.usableCount == 2, "IK summary usable count"))
        return rc;
    return 0;
}

static int testIkIncludesCurrentQForCurrentTcpTarget ()
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeTestKukaIIWA (stateStructure);
    rw::kinematics::State state = stateStructure.getDefaultState ();

    const rw::math::Q currentQ (7, 0.4, -0.7, 0.6, 0.8, -0.5, 0.9, 0.3);
    device->setQ (currentQ, state);
    const rw::math::Transform3D<> currentTcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), device->getEnd (), state);
    const rw::math::RPY<> rpy (currentTcp.R ());

    rws::TaskPoint target;
    target.position = {{currentTcp.P ()[0], currentTcp.P ()[1], currentTcp.P ()[2]}};
    target.rpyDeg = {{rpy (0) * 180.0 / rw::math::Pi,
                      rpy (1) * 180.0 / rw::math::Pi,
                      rpy (2) * 180.0 / rw::math::Pi}};

    rws::KinematicThresholds thresholds;
    thresholds.conditionWarning = 1e12;
    thresholds.conditionFail = 1e13;
    thresholds.singularValueWarning = 0.0;
    thresholds.manipulabilityWarning = 0.0;
    rws::KinematicAnalyzer analyzer;
    analyzer.setThresholds (thresholds);

    const rws::KinematicIkAnalysisResult result =
        analyzer.analyzeIk (device, device->getEnd (), state, target, NULL);

    bool foundCurrentQ = false;
    for (const rws::KinematicIkSolution& solution : result.solutions) {
        if (solution.distanceToCurrentQ <= 1e-10) {
            foundCurrentQ = solution.status == rws::AnalysisStatus::Pass &&
                            solution.positionErrorMeters <= thresholds.positionToleranceMeters &&
                            solution.orientationErrorDeg <= thresholds.orientationToleranceDeg;
            break;
        }
    }
    if (const int rc = require (foundCurrentQ,
                                "IK includes current Q as a passing solution for current TCP target"))
        return rc;
    return 0;
}

// 子套件 5:calculateReachableRate 的边界:
//   - 2 Pass + 1 Warning + 1 Fail + 1 disabled = 3/4 = 0.75;
//   - 全部 disabled → 0.0(避免除零);
//   - 全部 Pass    → 1.0。
static int testIkDuplicateThresholdControlsCandidateMerging ()
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    rw::kinematics::State state = stateStructure.getDefaultState ();
    const rw::math::Q currentQ (6, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    device->setQ (currentQ, state);

    const rw::math::Transform3D<> currentTcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), device->getEnd (), state);
    const rw::math::RPY<> rpy (currentTcp.R ());
    rws::TaskPoint target;
    target.position = {{currentTcp.P ()[0], currentTcp.P ()[1], currentTcp.P ()[2]}};
    target.rpyDeg = {{rpy (0) * 180.0 / rw::math::Pi,
                      rpy (1) * 180.0 / rw::math::Pi,
                      rpy (2) * 180.0 / rw::math::Pi}};

    rws::KinematicAnalyzer defaultAnalyzer;
    const rws::KinematicIkAnalysisResult defaultResult =
        defaultAnalyzer.analyzeIk (device, device->getEnd (), state, target, NULL);

    rws::KinematicThresholds thresholds;
    if (const int rc = assertNear (
            thresholds.ikDuplicateQThreshold, 1e-4, 1e-12, "default IK duplicate threshold"))
        return rc;
    thresholds.ikDuplicateQThreshold = 0.01;
    rws::KinematicAnalyzer mergedAnalyzer;
    mergedAnalyzer.setThresholds (thresholds);
    const rws::KinematicIkAnalysisResult mergedResult =
        mergedAnalyzer.analyzeIk (device, device->getEnd (), state, target, NULL);

    if (const int rc = require (!defaultResult.solutions.empty (),
                                "default duplicate threshold yields IK candidates"))
        return rc;
    if (const int rc = require (mergedResult.solutions.size () < defaultResult.solutions.size (),
                                "larger duplicate threshold merges nearby IK candidates"))
        return rc;
    return 0;
}

static int testTaskPointReachableRate ()
{
    // 2 pass + 1 warning + 1 fail + 1 disabled:
    // reachable count = 2 + 1 = 3; enabled = 4; rate = 3/4 = 0.75.
    auto makeResult =
        [] (const std::string& id, rws::AnalysisStatus status, bool enabled) {
            rws::TaskPointReachabilityResult r;
            r.taskPoint.id      = id;
            r.taskPoint.enabled = enabled;
            r.status            = status;
            return r;
        };
    std::vector< rws::TaskPointReachabilityResult > results;
    results.push_back (makeResult ("P1", rws::AnalysisStatus::Pass, true));
    results.push_back (makeResult ("P2", rws::AnalysisStatus::Pass, true));
    results.push_back (makeResult ("P3", rws::AnalysisStatus::Warning, true));
    results.push_back (makeResult ("P4", rws::AnalysisStatus::Fail, true));
    results.push_back (makeResult ("P5", rws::AnalysisStatus::Unknown, false));

    rws::KinematicAnalyzer analyzer;
    const double rate = analyzer.calculateReachableRate (results);
    if (const int rc = assertNear (rate, 0.75, 1e-12, "reachable rate = 3/4"))
        return rc;

    // All disabled: rate should be 0.0 with no divide-by-zero.
    std::vector< rws::TaskPointReachabilityResult > allDisabled;
    allDisabled.push_back (makeResult ("D1", rws::AnalysisStatus::Pass, false));
    allDisabled.push_back (makeResult ("D2", rws::AnalysisStatus::Warning, false));
    const double rate2 = analyzer.calculateReachableRate (allDisabled);
    if (const int rc = assertNear (rate2, 0.0, 1e-12, "all disabled rate = 0"))
        return rc;

    // All pass: rate should be 1.0.
    std::vector< rws::TaskPointReachabilityResult > allPass;
    allPass.push_back (makeResult ("A1", rws::AnalysisStatus::Pass, true));
    allPass.push_back (makeResult ("A2", rws::AnalysisStatus::Pass, true));
    const double rate3 = analyzer.calculateReachableRate (allPass);
    if (const int rc = assertNear (rate3, 1.0, 1e-12, "all pass rate = 1"))
        return rc;

    return 0;
}

// 子套件 6a:KinematicAnalysisWorkspace helper — sanitize config / planned count / summary。
static int testWorkspaceHelpers ()
{
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = -7;
        config.gridStepsPerJoint = 0;
        config.randomSeed = 0;

        rws::WorkspaceSamplingDiagnostics diagnostics;
        const rws::WorkspaceSamplingConfig sanitized =
            rws::sanitizeWorkspaceSamplingConfig (config, &diagnostics);

        if (const int rc = require (sanitized.sampleCount == 0,
                                    "workspace sanitize clamps negative sample count"))
            return rc;
        if (const int rc = require (sanitized.gridStepsPerJoint == 1,
                                    "workspace sanitize clamps grid steps"))
            return rc;
        if (const int rc = require (sanitized.randomSeed == 1,
                                    "workspace sanitize adjusts zero seed"))
            return rc;
        if (const int rc = require (diagnostics.sampleCountClamped,
                                    "workspace diagnostics sample count clamped"))
            return rc;
        if (const int rc = require (diagnostics.gridStepsClamped,
                                    "workspace diagnostics grid steps clamped"))
            return rc;
        if (const int rc = require (diagnostics.randomSeedAdjusted,
                                    "workspace diagnostics random seed adjusted"))
            return rc;
    }

    {
        rws::WorkspaceSamplingConfig config;
        config.mode = rws::WorkspaceSamplingMode::Grid;
        config.sampleCount = 100;
        config.gridStepsPerJoint = 4;

        rws::WorkspaceSamplingDiagnostics diagnostics;
        const std::size_t planned =
            rws::plannedWorkspaceSampleCount (config, 6, &diagnostics);

        if (const int rc = require (planned == 100,
                                    "workspace grid planning respects sample cap"))
            return rc;
        if (const int rc = require (diagnostics.theoreticalGridSamples == 4096,
                                    "workspace grid theoretical count"))
            return rc;
        if (const int rc = require (diagnostics.gridCountTruncated,
                                    "workspace grid diagnostics truncated"))
            return rc;
    }

    {
        rws::WorkspaceSample pass;
        pass.status = rws::AnalysisStatus::Pass;
        pass.manipulability = 10.0;
        pass.conditionNumber = 20.0;
        pass.minJointLimitMargin = 0.3;

        rws::WorkspaceSample warning;
        warning.status = rws::AnalysisStatus::Warning;
        warning.manipulability = 2.0;
        warning.conditionNumber = 100.0;
        warning.minJointLimitMargin = 0.1;

        rws::WorkspaceSample fail;
        fail.status = rws::AnalysisStatus::Fail;
        fail.inCollision = true;
        fail.manipulability = std::numeric_limits< double >::infinity ();
        fail.conditionNumber = std::numeric_limits< double >::infinity ();
        fail.minJointLimitMargin = 0.0;

        const rws::WorkspaceSummary summary = rws::summarizeWorkspaceSamples (
            std::vector< rws::WorkspaceSample > {pass, warning, fail});

        if (const int rc = require (summary.totalCount == 3, "workspace summary total"))
            return rc;
        if (const int rc = require (summary.passCount == 1, "workspace summary pass"))
            return rc;
        if (const int rc = require (summary.warningCount == 1, "workspace summary warning"))
            return rc;
        if (const int rc = require (summary.failCount == 1, "workspace summary fail"))
            return rc;
        if (const int rc = require (summary.collisionCount == 1,
                                    "workspace summary collision"))
            return rc;
        if (const int rc = assertNear (summary.avgManipulability, 6.0, 1e-12,
                                       "workspace summary avg manipulability"))
            return rc;
        if (const int rc = assertNear (summary.p10Manipulability, 2.0, 1e-12,
                                       "workspace summary p10 manipulability"))
            return rc;
        if (const int rc = assertNear (summary.maxCondition, 100.0, 1e-12,
                                       "workspace summary finite max condition"))
            return rc;
        if (const int rc = assertNear (summary.minJointLimitMargin, 0.0, 1e-12,
                                       "workspace summary min margin"))
            return rc;
    }

    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = -1;
        rws::WorkspaceSamplingDiagnostics diagnostics;
        const std::size_t planned =
            rws::plannedWorkspaceSampleCount (config, 6, &diagnostics);
        if (const int rc = require (planned == 0,
                                    "workspace planned count handles negative count"))
            return rc;
        if (const int rc = require (diagnostics.sampleCountClamped,
                                    "workspace planned count reports clamped negative count"))
            return rc;
    }

    return 0;
}

// 子套件 7a:PoseReachability 辅助— sanitize / planned count / summary。
static int testPoseReachabilityHelpers ()
{
    {
        rws::PoseReachabilityConfig config;
        config.directionSamples = -5;
        config.rollSamples = 0;
        rws::PoseReachabilityDiagnostics diagnostics;
        const rws::PoseReachabilityConfig sanitized =
            rws::sanitizePoseReachabilityConfig (config, &diagnostics);
        if (const int rc = require (sanitized.directionSamples == 0,
                                    "pose direction samples clamped low"))
            return rc;
        if (const int rc = require (sanitized.rollSamples == 1,
                                    "pose roll samples clamped low"))
            return rc;
        if (const int rc = require (diagnostics.directionSamplesClamped,
                                    "pose direction clamp diagnostic"))
            return rc;
        if (const int rc = require (diagnostics.rollSamplesClamped,
                                    "pose roll clamp diagnostic"))
            return rc;
    }

    {
        rws::PoseReachabilityConfig config;
        config.directionSamples = 24;
        config.rollSamples = 3;
        rws::PoseReachabilityDiagnostics diagnostics;
        const std::size_t planned =
            rws::plannedPoseReachabilityTargetCount (config, 10, &diagnostics);
        if (const int rc = require (planned == 720,
                                    "pose planned target count"))
            return rc;
        if (const int rc = require (diagnostics.plannedDirectionsPerPosition == 72,
                                    "pose planned directions per position"))
            return rc;
    }

    {
        rws::PoseReachabilitySample pass;
        pass.status = rws::AnalysisStatus::Pass;
        pass.sampledDirections = 10;
        pass.reachableDirections = 10;
        pass.coverage = 1.0;
        pass.plannedIkTargets = 10;
        pass.completedIkTargets = 10;
        pass.partial = false;
        rws::PoseReachabilitySample warning;
        warning.status = rws::AnalysisStatus::Warning;
        warning.sampledDirections = 10;
        warning.reachableDirections = 4;
        warning.coverage = 0.4;
        warning.plannedIkTargets = 10;
        warning.completedIkTargets = 4;
        warning.partial = true;
        const rws::PoseReachabilitySummary summary =
            rws::summarizePoseReachabilitySamples (
                std::vector< rws::PoseReachabilitySample > {pass, warning});
        if (const int rc = require (summary.totalPositions == 2,
                                    "pose summary total positions"))
            return rc;
        if (const int rc = assertNear (summary.averageCoverage, 0.7, 1e-12,
                                       "pose average coverage"))
            return rc;
        if (const int rc = require (summary.partialCount == 1,
                                    "pose summary partial count"))
            return rc;
        if (const int rc = require (summary.plannedIkTargets == 20,
                                    "pose summary planned IK targets"))
            return rc;
        if (const int rc = require (summary.completedIkTargets == 14,
                                    "pose summary completed IK targets"))
            return rc;
    }

    return 0;
}

// 子套件 7:sampleWorkspace 在 NULL / 0 / 负 sampleCount / Grid 模式下的快速返回路径。
static int testWorkspaceSampling ()
{
    rws::KinematicAnalyzer analyzer;
    rw::kinematics::State state;

    // Zero sample count: return empty regardless of mode.
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = 0;
        const std::vector< rws::WorkspaceSample > samples =
            analyzer.sampleWorkspace (NULL, NULL, state, config, NULL);
        if (const int rc =
                require (samples.empty (), "workspace sampling returns empty for sampleCount=0"))
            return rc;
    }

    // Negative sample count: return empty.
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = -1;
        const std::vector< rws::WorkspaceSample > samples =
            analyzer.sampleWorkspace (NULL, NULL, state, config, NULL);
        if (const int rc =
                require (samples.empty (), "workspace sampling returns empty for negative count"))
            return rc;
    }

    // Null device: return empty.
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount            = 10;
        config.mode                    = rws::WorkspaceSamplingMode::RandomUniform;
        config.randomSeed              = 42;
        const std::vector< rws::WorkspaceSample > samples =
            analyzer.sampleWorkspace (NULL, NULL, state, config, NULL);
        if (const int rc = require (samples.empty (), "workspace sampling handles null device"))
            return rc;
    }

    // Null device + Grid: also returns empty.
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount       = 10;
        config.mode               = rws::WorkspaceSamplingMode::Grid;
        config.gridStepsPerJoint = 0;
        const std::vector< rws::WorkspaceSample > samples =
            analyzer.sampleWorkspace (NULL, NULL, state, config, NULL);
        if (const int rc =
                require (samples.empty (), "workspace sampling handles null device under Grid"))
            return rc;
    }

    return 0;
}

// 子套件 7:analyzePoseReachability 在 NULL device / directionSamples=0 时的兜底。
static int testPoseReachability ()
{
    rws::PoseReachabilityConfig config;
    if (const int rc = require (config.directionSamples == 24, "default direction samples"))
        return rc;
    if (const int rc = require (config.rollSamples == 1, "default roll samples"))
        return rc;
    if (const int rc = require (config.checkCollision, "default pose collision check"))
        return rc;

    rws::KinematicAnalyzer analyzer;
    rw::kinematics::State state;

    std::vector< std::array< double, 3 > > positions;
    positions.push_back (std::array< double, 3 > {{1.0, 2.0, 3.0}});

    const std::vector< rws::PoseReachabilitySample > noDevice =
        analyzer.analyzePoseReachability (NULL, NULL, state, positions, config, NULL);
    if (const int rc = require (noDevice.size () == 1, "no-device pose sample count"))
        return rc;
    if (const int rc = require (noDevice.front ().sampledDirections == 24,
                                "no-device sampled directions"))
        return rc;
    if (const int rc = require (noDevice.front ().reachableDirections == 0,
                                "no-device reachable directions"))
        return rc;
    if (const int rc = assertNear (noDevice.front ().coverage, 0.0, 1e-12,
                                   "no-device coverage"))
        return rc;
    if (const int rc = require (noDevice.front ().status == rws::AnalysisStatus::Fail,
                                "no-device status"))
        return rc;

    rws::PoseReachabilityConfig zero;
    zero.directionSamples = 0;
    zero.rollSamples      = 2;
    const std::vector< rws::PoseReachabilitySample > zeroResult =
        analyzer.analyzePoseReachability (NULL, NULL, state, positions, zero, NULL);
    if (const int rc = require (zeroResult.size () == 1, "zero pose sample count"))
        return rc;
    if (const int rc = require (zeroResult.front ().sampledDirections == 0,
                                "zero sampled directions"))
        return rc;
    if (const int rc = assertNear (zeroResult.front ().coverage, 0.0, 1e-12,
                                   "zero coverage"))
        return rc;

    // P4:负 rollSamples 应被 sanitize 为 1,使 sampledDirections = directionSamples × 1。
    {
        rws::PoseReachabilityConfig negativeRoll;
        negativeRoll.directionSamples = 4;
        negativeRoll.rollSamples = -9;
        const std::vector< rws::PoseReachabilitySample > negativeRollResult =
            analyzer.analyzePoseReachability (NULL, NULL, state, positions, negativeRoll, NULL);
        if (const int rc = require (negativeRollResult.front ().sampledDirections == 4,
                                    "negative roll is sanitized to one roll"))
            return rc;
    }

    // P5:取消测试:预取消的 alwaysCancel 回调应产生 1 个 position 的 Fail。
    {
        rws::PoseReachabilityRunCallbacks cancelCb;
        cancelCb.isCancellationRequested = [] (void*) -> bool { return true; };
        const std::vector< rws::PoseReachabilitySample > canceled =
            analyzer.analyzePoseReachability (
                NULL, NULL, state, positions, config, NULL, cancelCb);
        if (const int rc = require (canceled.size () == 1,
                                    "canceled pose result preserves current position"))
            return rc;
    }

    // P5:取消必须在单个 position 内的 IK target 之间生效,而不是只在 position 边界检查。
    {
        rw::kinematics::StateStructure stateStructure;
        const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
        rw::kinematics::State deviceState = stateStructure.getDefaultState ();

        rws::PoseReachabilityConfig cancelInsidePosition;
        cancelInsidePosition.directionSamples = 2;
        cancelInsidePosition.rollSamples = 2;
        cancelInsidePosition.checkCollision = false;

        struct CancelAfterFirstCheck {
            int checks = 0;
        } cancelState;
        rws::PoseReachabilityRunCallbacks cancelCb;
        cancelCb.userData = &cancelState;
        cancelCb.isCancellationRequested = [] (void* userData) -> bool {
            CancelAfterFirstCheck* state =
                static_cast< CancelAfterFirstCheck* > (userData);
            ++state->checks;
            return state->checks >= 2;
        };

        const std::vector< rws::PoseReachabilitySample > canceled =
            analyzer.analyzePoseReachability (
                device, device->getEnd (), deviceState, positions,
                cancelInsidePosition, NULL, cancelCb);
        if (const int rc = require (cancelState.checks >= 2,
                                    "pose cancellation checked inside IK target loop"))
            return rc;
        if (const int rc = require (canceled.size () == 1,
                                    "inner-loop canceled pose result count"))
            return rc;
        if (const int rc = require (canceled.front ().sampledDirections == 4,
                                    "inner-loop canceled sampled directions"))
            return rc;
        if (const int rc = require (canceled.front ().plannedIkTargets == 4,
                                    "inner-loop canceled planned IK targets"))
            return rc;
        if (const int rc = require (canceled.front ().completedIkTargets < 4,
                                    "inner-loop canceled completed IK targets"))
            return rc;
        if (const int rc = require (canceled.front ().partial,
                                    "inner-loop canceled sample marked partial"))
            return rc;
    }

    // P5:进度回调测试:每个 IK target 完成后回调一次,最后完成数 = 计划数。
    {
        rw::kinematics::StateStructure stateStructure;
        const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
        rw::kinematics::State deviceState = stateStructure.getDefaultState ();

        rws::PoseReachabilityConfig progressConfig;
        progressConfig.directionSamples = 2;
        progressConfig.rollSamples = 2;
        progressConfig.checkCollision = false;

        struct ProgressState {
            std::size_t calls = 0;
            std::size_t lastCompleted = 0;
            std::size_t lastPlanned = 0;
        } progressState;

        rws::PoseReachabilityRunCallbacks progressCb;
        progressCb.userData = &progressState;
        progressCb.onProgress = [] (std::size_t completed,
                                    std::size_t planned,
                                    void* userData) {
            ProgressState* state = static_cast< ProgressState* > (userData);
            ++state->calls;
            state->lastCompleted = completed;
            state->lastPlanned = planned;
        };

        const std::vector< rws::PoseReachabilitySample > progressResult =
            analyzer.analyzePoseReachability (
                device, device->getEnd (), deviceState, positions,
                progressConfig, NULL, progressCb);

        if (const int rc = require (progressState.calls == 4,
                                    "pose progress callback per IK target"))
            return rc;
        if (const int rc = require (progressState.lastCompleted == 4,
                                    "pose progress last completed target"))
            return rc;
        if (const int rc = require (progressState.lastPlanned == 4,
                                    "pose progress planned target count"))
            return rc;
        if (const int rc = require (progressResult.front ().plannedIkTargets == 4,
                                    "pose sample planned IK targets"))
            return rc;
        if (const int rc = require (progressResult.front ().completedIkTargets == 4,
                                    "pose sample completed IK targets"))
            return rc;
        if (const int rc = require (!progressResult.front ().partial,
                                    "pose complete sample is not partial"))
            return rc;
    }

    return 0;
}

// 子套件 8:buildAggregateResult 综合:
//   - 包含 1 个 Fail 任务点时,总 status 是 Fail;
//   - reachableRate = 0.5(1 Pass + 1 Fail);
//   - manipulabilityMap 至少有 min/max/mean。
static int testAggregateResult ()
{
    rws::KinematicCurrentPoseResult current;
    current.status = rws::AnalysisStatus::Pass;
    current.manipulability = 3.0;
    current.conditionNumber = 4.0;

    rws::TaskPointReachabilityResult pass;
    pass.taskPoint.enabled = true;
    pass.status = rws::AnalysisStatus::Pass;
    rws::TaskPointReachabilityResult fail;
    fail.taskPoint.enabled = true;
    fail.status = rws::AnalysisStatus::Fail;

    rws::WorkspaceSample ws1;
    ws1.status = rws::AnalysisStatus::Pass;
    ws1.manipulability = 2.0;
    rws::WorkspaceSample ws2;
    ws2.status = rws::AnalysisStatus::Warning;
    ws2.manipulability = 4.0;

    rws::PoseReachabilitySample pose;
    pose.sampledDirections = 8;
    pose.reachableDirections = 6;
    pose.coverage = 0.75;
    pose.status = rws::AnalysisStatus::Warning;

    rws::KinematicAnalyzer analyzer;
    const rws::KinematicAnalysisResult result = analyzer.buildAggregateResult (
        current,
        std::vector< rws::TaskPointReachabilityResult > {pass, fail},
        std::vector< rws::WorkspaceSample > {ws1, ws2},
        std::vector< rws::PoseReachabilitySample > {pose});

    if (const int rc = require (result.header.pluginName == "KinematicAnalysis",
                                "aggregate plugin name"))
        return rc;
    if (const int rc = require (result.status == rws::AnalysisStatus::Fail,
                                "aggregate worst status"))
        return rc;
    if (const int rc = assertNear (result.reachableRate, 0.5, 1e-12,
                                   "aggregate reachable rate"))
        return rc;
    if (const int rc = require (result.workspaceSamples.size () == 2,
                                "aggregate workspace sample count"))
        return rc;
    if (const int rc = require (result.poseReachability.size () == 1,
                                "aggregate pose sample count"))
        return rc;
    if (const int rc = require (result.manipulabilityMap.size () >= 3,
                                "aggregate manipulability metrics"))
        return rc;
    return 0;
}

// ============================================================================
//  TaskPointResolver 单元测试
//  使用 makeGenericSixAxis 构造 device + StateStructure,
//  再 addFrame 到 StateStructure 上,然后用 WorkCell 包装,验证
//  refFrame / tcpFrame 在不同 frame 下的解析行为。
// ============================================================================
static int testTaskPointResolver ()
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    StateStructure::Ptr stateStructure = rw::core::ownedPtr (new StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);

    // 多挂几个 named frame 供 resolver 查找:
    const Frame::Ptr fixtureA =
        rw::core::ownedPtr (new FixedFrame (
            "FixtureA", Transform3D<> (Vector3D<> (0.5, 0.0, 0.0))));
    const Frame::Ptr toolTip =
        rw::core::ownedPtr (new FixedFrame (
            "ToolTip", Transform3D<> (Vector3D<> (0.0, 0.0, 0.05))));
    stateStructure->addFrame (fixtureA, device->getBase ());
    stateStructure->addFrame (toolTip, device->getEnd ());

    rw::models::WorkCell::Ptr workcell =
        rw::core::ownedPtr (new rw::models::WorkCell (
            stateStructure, "TestWorkCell", ""));
    const rw::kinematics::State state = workcell->getDefaultState ();

    // 1) WORLD refFrame:valid + 目标在 base 下
    {
        rws::TaskPoint p;
        p.id = "P1";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "TCP";
        p.position = {{0.5, 0.0, 0.3}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (r.valid, "WORLD refFrame resolves to valid"))
            return rc;
        if (const int rc = require (r.tcpFrame == device->getEnd (),
                                    "WORLD uses row-level TCP"))
            return rc;
        if (const int rc = require (r.targetInDeviceBase.refFrame == device->getBase ()->getName (),
                                    "WORLD output refFrame = device base name"))
            return rc;
        if (const int rc = require (r.warnings.empty (), "WORLD success has no warnings"))
            return rc;
    }

    // 2) device base refFrame:valid + refFrame 重写为 base
    {
        rws::TaskPoint p;
        p.id = "P2";
        p.refFrame = device->getBase ()->getName ();
        p.tcpFrame = "TCP";
        p.position = {{1.0, 2.0, 3.0}};
        p.rpyDeg   = {{10.0, 20.0, 30.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (r.valid, "device base refFrame resolves to valid"))
            return rc;
        if (const int rc = require (r.targetInDeviceBase.refFrame == device->getBase ()->getName (),
                                    "device base output refFrame stays base name"))
            return rc;
        // 已声明在 base 下,数值应当原样保留(无变换)。
        if (const int rc = assertNear (r.targetInDeviceBase.position[0], 1.0, 1e-9,
                                       "base refFrame preserves x"))
            return rc;
        if (const int rc = assertNear (r.targetInDeviceBase.position[1], 2.0, 1e-9,
                                       "base refFrame preserves y"))
            return rc;
    }

    // 3) named frame refFrame:valid + 数值被变换到 base
    {
        rws::TaskPoint p;
        p.id = "P3";
        p.refFrame = "FixtureA";   // 在 (0.5, 0, 0) 偏移
        p.tcpFrame = "ToolTip";
        p.position = {{0.0, 0.0, 0.2}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (r.valid, "named frame refFrame resolves to valid"))
            return rc;
        if (const int rc = require (r.tcpFrame != nullptr && r.tcpFrame->getName () == "ToolTip",
                                    "named frame uses row-level TCP"))
            return rc;
        // FixtureA 在 base 的 (0.5, 0, 0) 偏移,ref 下 (0, 0, 0.2) 应转到 base 下 (0.5, 0, 0.2)。
        if (const int rc = assertNear (r.targetInDeviceBase.position[0], 0.5, 1e-9,
                                       "named frame transform x"))
            return rc;
        if (const int rc = assertNear (r.targetInDeviceBase.position[2], 0.2, 1e-9,
                                       "named frame transform z"))
            return rc;
    }

    // 4) unknown refFrame:invalid + KIN_TASK_REF_NOT_FOUND warning
    {
        rws::TaskPoint p;
        p.id = "P4";
        p.refFrame = "MissingFrame";
        p.tcpFrame = "TCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (!r.valid, "unknown refFrame is invalid"))
            return rc;
        if (const int rc = require (r.failure == rws::KinematicFailureReason::InvalidTarget,
                                    "unknown refFrame -> InvalidTarget"))
            return rc;
        bool found = false;
        for (const rws::AnalysisWarning& w : r.warnings) {
            if (w.code == "KIN_TASK_REF_NOT_FOUND") {
                found = true;
                break;
            }
        }
        if (const int rc = require (found, "KIN_TASK_REF_NOT_FOUND warning emitted"))
            return rc;
    }

    // 5) unknown tcpFrame:invalid + KIN_TASK_TCP_NOT_FOUND warning
    {
        rws::TaskPoint p;
        p.id = "P5";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "MissingTCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (!r.valid, "unknown tcpFrame is invalid"))
            return rc;
        if (const int rc = require (r.failure == rws::KinematicFailureReason::NoTcpFrame,
                                    "unknown tcpFrame -> NoTcpFrame"))
            return rc;
        bool found = false;
        for (const rws::AnalysisWarning& w : r.warnings) {
            if (w.code == "KIN_TASK_TCP_NOT_FOUND") {
                found = true;
                break;
            }
        }
        if (const int rc = require (found, "KIN_TASK_TCP_NOT_FOUND warning emitted"))
            return rc;
    }

    // 6) 空 tcpFrame:fallback 到 default TCP
    {
        rws::TaskPoint p;
        p.id = "P6";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame.clear ();        // 空 → 用 default
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (r.valid, "empty tcpFrame falls back to default"))
            return rc;
        if (const int rc = require (r.tcpFrame == device->getEnd (),
                                    "empty tcpFrame uses default TCP"))
            return rc;
    }

    // 7) 空 tcpFrame + 空 default TCP:invalid + NoTcpFrame
    {
        rws::TaskPoint p;
        p.id = "P7";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame.clear ();
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, nullptr, state, p);
        if (const int rc = require (!r.valid, "empty tcpFrame + null default is invalid"))
            return rc;
        if (const int rc = require (r.failure == rws::KinematicFailureReason::NoTcpFrame,
                                    "no TCP at all -> NoTcpFrame"))
            return rc;
    }

    return 0;
}

// ============================================================================
//  P1:workcell-aware analyzeTaskPoint 行为
//    - disabled 不跑 resolver,不计 reachable;
//    - unknown refFrame / tcpFrame → resolver invalid → Fail;
//    - WORLD / device base 成功路径 → 调用旧 analyzeIk。
// ============================================================================
static int testWorkcellAwareAnalyzeTaskPoint ()
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    StateStructure::Ptr stateStructure = rw::core::ownedPtr (new StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    rw::models::WorkCell::Ptr workcell =
        rw::core::ownedPtr (new rw::models::WorkCell (stateStructure, "TestWC", ""));
    const rw::kinematics::State state = workcell->getDefaultState ();

    rws::KinematicAnalyzer analyzer;
    analyzer.setThresholds (rws::KinematicThresholds ());

    // 1) disabled → status Warning, status 文字通过 KIN_TASK_DISABLED 警告体现,
    //    且 r.ik.solutions 为空(不影响 reachable rate)。
    {
        rws::TaskPoint p;
        p.id = "P_disabled";
        p.enabled = false;
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "TCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        const auto r = analyzer.analyzeTaskPoint (workcell.get (), device,
                                                  device->getEnd (), state, p, NULL);
        if (const int rc = require (r.status == rws::AnalysisStatus::Warning,
                                    "disabled -> Warning"))
            return rc;
        if (const int rc = require (r.ik.solutions.empty (), "disabled -> no solutions"))
            return rc;
        bool saw = false;
        for (const rws::AnalysisWarning& w : r.ik.warnings) {
            if (w.code == "KIN_TASK_DISABLED") { saw = true; break; }
        }
        if (const int rc = require (saw, "disabled -> KIN_TASK_DISABLED warning"))
            return rc;
    }

    // 2) unknown refFrame → resolver InvalidTarget → Fail + warning。
    {
        rws::TaskPoint p;
        p.id = "P_missing_ref";
        p.enabled = true;
        p.refFrame = "NoSuchFrame";
        p.tcpFrame = "TCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        const auto r = analyzer.analyzeTaskPoint (workcell.get (), device,
                                                  device->getEnd (), state, p, NULL);
        if (const int rc = require (r.status == rws::AnalysisStatus::Fail,
                                    "unknown refFrame -> Fail"))
            return rc;
        if (const int rc = require (r.primaryFailure == rws::KinematicFailureReason::InvalidTarget,
                                    "unknown refFrame -> InvalidTarget"))
            return rc;
        bool saw = false;
        for (const rws::AnalysisWarning& w : r.ik.warnings) {
            if (w.code == "KIN_TASK_REF_NOT_FOUND") { saw = true; break; }
        }
        if (const int rc = require (saw, "unknown refFrame -> KIN_TASK_REF_NOT_FOUND"))
            return rc;
    }

    // 3) unknown tcpFrame → NoTcpFrame → Fail。
    {
        rws::TaskPoint p;
        p.id = "P_missing_tcp";
        p.enabled = true;
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "NoSuchTCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        const auto r = analyzer.analyzeTaskPoint (workcell.get (), device,
                                                  device->getEnd (), state, p, NULL);
        if (const int rc = require (r.status == rws::AnalysisStatus::Fail,
                                    "unknown tcpFrame -> Fail"))
            return rc;
        if (const int rc = require (r.primaryFailure == rws::KinematicFailureReason::NoTcpFrame,
                                    "unknown tcpFrame -> NoTcpFrame"))
            return rc;
    }

    // 4) WORLD + 已知 TCP → 走 analyzeIk,rawCandidateCount > 0 或 NoSolution(可能)。
    {
        rws::TaskPoint p;
        p.id = "P_world";
        p.enabled = true;
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "TCP";
        p.position = {{0.3, 0.0, 0.4}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.01;
        p.tolerance.orientationDeg = 5.0;
        const auto r = analyzer.analyzeTaskPoint (workcell.get (), device,
                                                  device->getEnd (), state, p, NULL);
        // r.status 至少应不是 Unknown,可能 Pass / Warning / Fail。
        if (const int rc = require (r.status != rws::AnalysisStatus::Unknown,
                                    "WORLD run produced a status"))
            return rc;
        // 批量可达率:disabled 不计,enabled 计分母。
        std::vector< rws::TaskPoint > batch;
        batch.push_back (p);
        rws::TaskPoint p2 = p; p2.id = "P_disabled_2"; p2.enabled = false;
        batch.push_back (p2);
        const auto results = analyzer.analyzeTaskPoints (
            workcell.get (), device, device->getEnd (), state, batch, NULL);
        if (const int rc = require (results.size () == 2,
                                    "workcell-aware batch returns 2 results"))
            return rc;
        if (const int rc = require (results[0].status != rws::AnalysisStatus::Unknown,
                                    "enabled world task point receives a concrete status"))
            return rc;
        if (const int rc = require (results[1].status == rws::AnalysisStatus::Warning,
                                    "disabled batch point remains skipped warning"))
            return rc;
    }

    return 0;
}

// runAll:把所有子套件串行跑一遍,首个失败立即返回。
static int testTaskPointUiLogic ()
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    StateStructure::Ptr stateStructure = rw::core::ownedPtr (new StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    rw::models::WorkCell::Ptr workcell =
        rw::core::ownedPtr (new rw::models::WorkCell (stateStructure, "TestWC", ""));
    const rw::kinematics::State state = workcell->getDefaultState ();

    rws::KinematicAnalyzer analyzer;

    rws::TaskPoint keep;
    keep.id = "keep";
    keep.name = "keep";
    keep.enabled = true;
    keep.refFrame = device->getBase ()->getName ();
    keep.tcpFrame = "TCP";
    keep.position = {{0.0, 0.0, 0.0}};

    rws::TaskPoint selected = keep;
    selected.id = "selected";
    selected.name = "selected";
    selected.refFrame = "MissingFrame";

    rws::TaskPointReachabilityResult previous;
    previous.taskPoint = keep;
    previous.status = rws::AnalysisStatus::Pass;
    previous.primaryFailure = rws::KinematicFailureReason::None;

    const std::vector< rws::TaskPointReachabilityResult > updated =
        rws::analyzeSelectedTaskPointRows (
            analyzer, workcell.get (), device, device->getEnd (), state,
            std::vector< rws::TaskPoint > {keep, selected},
            std::vector< int > {1},
            std::vector< rws::TaskPointReachabilityResult > {previous},
            NULL);

    if (const int rc = require (updated.size () == 2,
                                "selected analysis keeps result vector aligned to rows"))
        return rc;
    if (const int rc = require (updated[0].status == rws::AnalysisStatus::Pass,
                                "selected analysis preserves unselected previous result"))
        return rc;
    if (const int rc = require (updated[1].status == rws::AnalysisStatus::Fail,
                                "selected analysis uses workcell-aware resolver failure"))
        return rc;
    if (const int rc = require (updated[1].primaryFailure == rws::KinematicFailureReason::InvalidTarget,
                                "selected analysis reports missing refFrame as InvalidTarget"))
        return rc;

    const Transform3D<> baseTtcp (Vector3D<> (1.0, 2.0, 3.0),
                                  RPY<> (0.1, 0.2, 0.3));
    const rws::TaskPoint imported = rws::taskPointFromCurrentTcpPose (
        "TP_001", "TCP", device->getBase ()->getName (), baseTtcp,
        rws::KinematicThresholds ());
    if (const int rc = require (imported.refFrame == device->getBase ()->getName (),
                                "current TCP import stores base refFrame for base coordinates"))
        return rc;
    if (const int rc = assertNear (imported.position[0], 1.0, 1e-12,
                                   "current TCP import preserves base x"))
        return rc;
    if (const int rc = require (imported.tcpFrame == "TCP",
                                "current TCP import stores selected TCP name"))
        return rc;
    return 0;
}

// 子套件:P3 TaskPointTableModel 数据层单测。
// 覆盖:列数 27、header 文本、insertRows / removeRows、setData 字段、
// validation 行为、result 列只读、Q_OBJECT 兼容(QModelIndex)。
static int testTaskPointModel ()
{
    using namespace rws;

    TaskPointTableModel model;

    // 1) 初始状态:0 行 27 列。
    if (const int rc = require (model.rowCount () == 0, "model starts empty"))
        return rc;
    if (const int rc = require (model.columnCount () == 27,
                                "model column count is 27"))
        return rc;

    // 2) header 与当前 UI 一致(只检查代表性列)。
    const QStringList headers = model.allHeaderTexts ();
    if (const int rc = require (headers.size () == 27, "header list size matches"))
        return rc;
    if (const int rc = require (headers[ColEnabled]  == QStringLiteral ("Enabled"),
                                "header[ColEnabled]"))
        return rc;
    if (const int rc = require (headers[ColType]    == QStringLiteral ("type"),
                                "header[ColType]"))
        return rc;
    if (const int rc = require (headers[ColTcpFrame]== QStringLiteral ("tcpFrame"),
                                "header[ColTcpFrame]"))
        return rc;
    if (const int rc = require (headers[ColCollision] == QStringLiteral ("collision"),
                                "header[ColCollision]"))
        return rc;

    // 3) insertRows:默认行的 id / name / type / refFrame / tcpFrame。
    if (const int rc = !model.insertRows (0, 1) ? 1 : 0)
        return rc;
    if (const int rc = require (model.rowCount () == 1, "insert one row"))
        return rc;
    const QModelIndex e0 = model.index (0, ColEnabled);
    const QModelIndex x0 = model.index (0, ColX);
    const QModelIndex id0 = model.index (0, ColId);
    if (const int rc = require (e0.data (Qt::CheckStateRole).toInt () == int (Qt::Checked),
                                "default row enabled is checked"))
        return rc;
    if (const int rc = require (model.data (id0, Qt::DisplayRole).toString () == QStringLiteral ("P1"),
                                "default id P1"))
        return rc;
    if (const int rc = require (x0.data (Qt::DisplayRole).toString () == QStringLiteral ("0"),
                                "default x = 0"))
        return rc;

    // 4) setData:改 x / type / freeRoll,确认 taskPointAt 返回正确。
    if (const int rc = !model.setData (x0, QStringLiteral ("0.42")) ? 1 : 0)
        return rc;
    if (const int rc = !model.setData (model.index (0, ColType),
                                       QStringLiteral ("Pick")) ? 1 : 0)
        return rc;
    if (const int rc = !model.setData (model.index (0, ColFreeRoll),
                                       QStringLiteral ("true")) ? 1 : 0)
        return rc;
    {
        const TaskPoint p = model.taskPointAt (0);
        if (const int rc = require (nearlyEqual (p.position[0], 0.42, 1e-12),
                                    "x = 0.42 round-trip"))
            return rc;
        if (const int rc = require (p.type == TaskPointType::Pick,
                                    "type Pick round-trip"))
            return rc;
        if (const int rc = require (p.tolerance.allowToolRollFree == true,
                                    "freeRoll true round-trip"))
            return rc;
    }

    // 5) result 列(17..26)flags 不应包含 editable。
    for (int c = ColStatus; c < TaskPointColumnCount; ++c) {
        if (const int rc = require (
                !((model.flags (model.index (0, c))) & Qt::ItemIsEditable),
                QStringLiteral ("result column %1 is read-only").arg (c).toStdString ()))
            return rc;
    }

    // 6) 非法数值输入返回 false,不污染已有值。
    const double before = model.taskPointAt (0).position[1];
    if (const int rc = model.setData (model.index (0, ColY), QStringLiteral ("not a number")) ? 1 : 0)
        return rc;
    if (const int rc = require (nearlyEqual (model.taskPointAt (0).position[1], before, 0),
                                "invalid y input leaves value unchanged"))
        return rc;

    // 7) removeRows。
    if (const int rc = !model.removeRows (0, 1) ? 1 : 0)
        return rc;
    if (const int rc = require (model.rowCount () == 0, "remove one row"))
        return rc;

    // 8) setRowsFromTaskPoints:覆盖式导入,validateAll 立刻跑。
    TaskPoint a; a.id = "A"; a.name = "A";
    a.enabled = true; a.refFrame = "WORLD"; a.tcpFrame = "TCP";
    a.position = {{1.0, 2.0, 3.0}};
    TaskPoint b = a; b.id = "B"; b.refFrame = "";  // invalid: empty refFrame
    std::vector< TaskPoint > rows = {a, b};
    model.setRowsFromTaskPoints (rows);
    if (const int rc = require (model.rowCount () == 2, "import 2 rows"))
        return rc;
    QString taskPointError;
    const std::vector< TaskPoint > roundTrip = model.taskPoints (&taskPointError);
    if (const int rc = require (roundTrip.size () == 2,
                                "taskPoints returns all imported rows"))
        return rc;
    if (const int rc = require (roundTrip[0].id == "A" && roundTrip[1].id == "B",
                                "taskPoints preserves row ids"))
        return rc;
    QString summary;
    if (const int rc = require (!model.validateAll (&summary),
                                "imported invalid row reported"))
        return rc;
    if (const int rc = require (summary.contains (QStringLiteral ("Row 2")),
                                "summary points to row 2"))
        return rc;

    return 0;
}

static int testVisualizationData ()
{
    using namespace rws;

    TaskPointReachabilityResult task;
    task.status = AnalysisStatus::Warning;
    task.taskPoint.id = "TP_A";
    task.taskPoint.name = "Task A";
    task.taskPoint.position = {{1.0, 2.0, 3.0}};
    task.failureReasons.push_back (KinematicFailureReason::NearJointLimit);
    task.ik.rawCandidateCount = 3;
    task.ik.usableSolutionCount = 1;
    KinematicIkSolution taskSolution;
    taskSolution.status = AnalysisStatus::Warning;
    taskSolution.manipulability = 0.25;
    taskSolution.conditionNumber = 42.0;
    taskSolution.minJointLimitMargin = 0.08;
    taskSolution.positionErrorMeters = 0.0005;
    taskSolution.orientationErrorDeg = 0.2;
    taskSolution.inCollision = false;
    task.ik.solutions.push_back (taskSolution);

    const AnalysisVisualData taskData = visualDataFromTaskPointResults (
        std::vector< TaskPointReachabilityResult > {task},
        VisualScalarMode::Condition);
    if (const int rc = require (taskData.points.size () == 1,
                                "task result creates one visual point"))
        return rc;
    if (const int rc = require (taskData.hasFiniteScalar,
                                "task visual data has finite scalar"))
        return rc;
    if (const int rc = assertNear (taskData.points[0].position[0], 1.0, 1e-12,
                                   "task visual x"))
        return rc;
    if (const int rc = assertNear (taskData.points[0].scalar, 42.0, 1e-12,
                                   "task condition scalar"))
        return rc;
    if (const int rc = require (taskData.points[0].label == QStringLiteral ("TP_A"),
                                "task label uses id"))
        return rc;
    if (const int rc = require (taskData.points[0].tooltip.contains (QStringLiteral ("NearJointLimit")),
                                "task tooltip contains failure reason"))
        return rc;

    WorkspaceSample workspace;
    workspace.tcpPosition = {{-1.0, 0.5, 2.5}};
    workspace.status = AnalysisStatus::Pass;
    workspace.manipulability = 0.75;
    workspace.conditionNumber = 12.0;
    workspace.minJointLimitMargin = 0.2;
    workspace.inCollision = true;
    const AnalysisVisualData workspaceData = visualDataFromWorkspaceSamples (
        std::vector< WorkspaceSample > {workspace},
        VisualScalarMode::Collision);
    if (const int rc = require (workspaceData.points.size () == 1,
                                "workspace creates one visual point"))
        return rc;
    if (const int rc = require (workspaceData.points[0].inCollision,
                                "workspace collision flag is preserved"))
        return rc;
    if (const int rc = assertNear (workspaceData.points[0].scalar, 1.0, 1e-12,
                                   "workspace collision scalar"))
        return rc;

    PoseReachabilitySample pose;
    pose.position = {{4.0, 5.0, 6.0}};
    pose.status = AnalysisStatus::Fail;
    pose.sampledDirections = 10;
    pose.reachableDirections = 3;
    pose.coverage = 0.3;
    const AnalysisVisualData poseData = visualDataFromPoseReachabilitySamples (
        std::vector< PoseReachabilitySample > {pose},
        VisualScalarMode::Coverage);
    if (const int rc = require (poseData.points.size () == 1,
                                "pose reachability creates one visual point"))
        return rc;
    if (const int rc = assertNear (poseData.points[0].scalar, 0.3, 1e-12,
                                   "pose coverage scalar"))
        return rc;

    const QPointF projected = projectVisualPoint (poseData.points[0], VisualProjection::XZ);
    if (const int rc = assertNear (projected.x (), 4.0, 1e-12, "XZ projection x"))
        return rc;
    if (const int rc = assertNear (projected.y (), 6.0, 1e-12, "XZ projection z"))
        return rc;
    return 0;
}

static int runAll ()
{
    if (const int rc = testTypes ())
        return rc;
    if (const int rc = testMetrics ())
        return rc;
    if (const int rc = testCurrentPose ())
        return rc;
    if (const int rc = testTargetValidationAndResidual ())
        return rc;
    if (const int rc = testPoseUnitConversions ())
        return rc;
    if (const int rc = testIkRanking ())
        return rc;
    if (const int rc = testIkIncludesCurrentQForCurrentTcpTarget ())
        return rc;
    if (const int rc = testIkDuplicateThresholdControlsCandidateMerging ())
        return rc;
    if (const int rc = testTaskPointReachableRate ())
        return rc;
    if (const int rc = testTaskPointResolver ())
        return rc;
    if (const int rc = testWorkcellAwareAnalyzeTaskPoint ())
        return rc;
    if (const int rc = testTaskPointUiLogic ())
        return rc;
    if (const int rc = testTaskPointModel ())
        return rc;
    if (const int rc = testVisualizationData ())
        return rc;
    if (const int rc = testWorkspaceHelpers ())
        return rc;
    if (const int rc = testWorkspaceSampling ())
        return rc;
    if (const int rc = testPoseReachabilityHelpers ())
        return rc;
    if (const int rc = testPoseReachability ())
        return rc;
    return testAggregateResult ();
}

// main:argv[1] 选子套件("all" / 各具体名),默认 "all"。
// QCoreApplication 是为了让 Q_OBJECT 相关初始化(QFile/QString)能正常工作。
int main (int argc, char** argv)
{
    QCoreApplication app (argc, argv);
    const std::string suite = argc > 1 ? argv[1] : "all";
    int rc                 = 0;
    if (suite == "all")
        rc = runAll ();
    else if (suite == "types")
        rc = testTypes ();
    else if (suite == "metrics")
        rc = testMetrics ();
    else if (suite == "current_pose")
        rc = testCurrentPose ();
    else if (suite == "target_validation")
        rc = testTargetValidationAndResidual ();
    else if (suite == "pose_units")
        rc = testPoseUnitConversions ();
    else if (suite == "ik")
        rc = testIkRanking ();
    else if (suite == "ik_current_target")
        rc = testIkIncludesCurrentQForCurrentTcpTarget ();
    else if (suite == "ik_dedup")
        rc = testIkDuplicateThresholdControlsCandidateMerging ();
    else if (suite == "task_points")
        rc = testTaskPointReachableRate ();
    else if (suite == "task_point_resolver")
        rc = testTaskPointResolver ();
    else if (suite == "task_point_workcell")
        rc = testWorkcellAwareAnalyzeTaskPoint ();
    else if (suite == "task_point_ui")
        rc = testTaskPointUiLogic ();
    else if (suite == "task_point_model")
        rc = testTaskPointModel ();
    else if (suite == "visualization_data")
        rc = testVisualizationData ();
    else if (suite == "workspace_helpers")
        rc = testWorkspaceHelpers ();
    else if (suite == "workspace")
        rc = testWorkspaceSampling ();
    else if (suite == "pose_reachability")
        rc = testPoseReachabilityHelpers ();
    else if (suite == "pose")
        rc = testPoseReachability ();
    else if (suite == "aggregate")
        rc = testAggregateResult ();
    else
        return fail ("Unknown KinematicAnalysis test suite: " + suite);
    if (rc != 0)
        return rc;
    std::cout << "KinematicAnalysis " << suite << " test passed." << std::endl;
    return 0;
}
