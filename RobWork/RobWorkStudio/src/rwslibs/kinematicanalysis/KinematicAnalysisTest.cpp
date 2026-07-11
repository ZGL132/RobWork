#include "KinematicAnalysisTypes.hpp"
#include "KinematicMetrics.hpp"
#include "KinematicAnalyzer.hpp"

#include <QCoreApplication>

#include <rw/math/Q.hpp>
#include <rw/math/Jacobian.hpp>

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
    return 0;
}

// 子套件 5:calculateReachableRate 的边界:
//   - 2 Pass + 1 Warning + 1 Fail + 1 disabled = 3/4 = 0.75;
//   - 全部 disabled → 0.0(避免除零);
//   - 全部 Pass    → 1.0。
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

// 子套件 6:sampleWorkspace 在 NULL / 0 / 负 sampleCount / Grid 模式下的快速返回路径。
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

// runAll:把所有子套件串行跑一遍,首个失败立即返回。
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
    if (const int rc = testTaskPointReachableRate ())
        return rc;
    if (const int rc = testWorkspaceSampling ())
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
    else if (suite == "task_points")
        rc = testTaskPointReachableRate ();
    else if (suite == "workspace")
        rc = testWorkspaceSampling ();
    else if (suite == "pose_reachability")
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
