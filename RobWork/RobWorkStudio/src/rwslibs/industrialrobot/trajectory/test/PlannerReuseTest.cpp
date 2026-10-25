/**
 * @file   PlannerReuseTest.cpp
 * @brief  WP-11 其余路径轨迹工具复用收口的测试面（REUSE-T32）——Planner
 *         度量换核（上游 MetricFactory/PathAnalyzer 直接消费）的可零模型
 *         观测面：直连采纳 L1 独立 oracle，以及 Continuity
 *         rotationAngleBetween 保留 2·atan2 的近 π/分支边界反例证据。
 *
 * 设计依据：
 *   - units/trajectory.md §10.4（稳定选择序"延续性→裕量→路径长度→候选
 *     序号"全序——度量换核不得改变全序与黄金算例结论）、§9.1（姿态维
 *     最短弧角与 C7 容差量纲）
 *   - REUSE-T32 契约 acceptance：度量手写循环期望对照（独立参考实现作
 *     oracle 而非上游自身）；被否候选（EAA::angle、Rotation3DAngleMetric）
 *     以真实测量登记
 *   - 零模型构造红线（IRD-GATE-AD——治理自测 grant-business-construction-
 *     rejected 在案）：业务域单元测试不构造 rw::models 对象，故真实装置
 *     夹具（RRT 重规划/采样计数替身）不落本文件；其观测面由既有
 *     PlannerAvoidance 黄金算例（直连筛查分辨率敏感性、同种子重放确定性）
 *     与断开红例承载——登记为覆盖限制，见任务 README
 *   - 数值 oracle 全部测试内独立重写，不调用产品匿名命名空间内部函数
 *
 * 集成模式条件源：构造 rw::math::Q/Transform3D 并消费 Planner/Continuity
 * 公共接口（链接产品库的非模板符号）——与 PtpSequenceTest 同款 gating
 * （冒烟模式不编译，真实执行归集成模式测试运行）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/trajectory/Continuity.hpp>
#include <sdurws/ird/trajectory/Planner.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/math/EAA.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Quaternion.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace core = sdurws::ird::core;
namespace trj = sdurws::ird::trajectory;

namespace {

// =====================================================================
// 独立 oracle（测试内重写，不进产品目标——"度量手写循环期望对照"的
// 参考实现；算式与上游 MetricUtil::dist1/dist2 同序但独立书写，用于钉
// 住产品换核后的逐位等价）
// =====================================================================

/// 单对 Manhattan 距离参考（Σ|Δq|，轴升序——MetricUtil::dist1 同序）。
double referenceManhattan(const rw::math::Q& a, const rw::math::Q& b)
{
    double result = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        result += std::fabs(a[i] - b[i]);
    }
    return result;
}

/// 单对 L2 距离参考（平方和→sqrt，轴升序——MetricUtil::dist2 同序）。
double referenceEuclidean(const rw::math::Q& a, const rw::math::Q& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double delta = a[i] - b[i];
        sum += delta * delta;
    }
    return std::sqrt(sum);
}

/// 手工构造的非全零对象身份（种子摘要前 16 字节——PlannerAvoidanceTest
/// 同款，种子串不同避免身份撞车）。
template <typename Id>
Id seededObjectId(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    const core::Digest256 digest = d.finalize();
    Id id;
    std::copy(digest.begin(), digest.begin()
                + static_cast<std::ptrdiff_t>(id.bytes.size()),
              id.bytes.begin());
    return id;
}

// =====================================================================
// 连续性角差反例的段构造辅助（§9.1 手工两段——TaskPoint 路点补来源
// 义务，§6.3 可追溯性）
// =====================================================================

/// 由轴角构造位姿（Quaternion 半角分量直写——绕开上游 EAA::toRotation3D
/// 经 axis() 的 theta<1e-6 微角死区，保证被比较路径读到同一精确矩阵；
/// 位置固定 (0.5,0,0.4)——本套件只关心姿态维差）。
rw::math::Transform3D<double> axisAnglePose(const rw::math::Vector3D<double>& axis, double angle)
{
    const double half = 0.5 * angle;
    const double s = std::sin(half);
    const rw::math::Quaternion<double> q(axis[0] * s, axis[1] * s, axis[2] * s,
                                         std::cos(half));
    return rw::math::Transform3D<double>(rw::math::Vector3D<double>(0.5, 0.0, 0.4),
                                         q.toRotation3D());
}

/// 构造两段路径：边界两端位姿分别 poseA/poseB（段结构满足 validate）。
std::vector<trj::TrajectorySegment> twoSegmentBoundary(
    const rw::math::Transform3D<double>& poseA, const rw::math::Transform3D<double>& poseB)
{
    const core::ObjectId sourceOid = core::ObjectId::generate();
    trj::TrajectorySegment seg0;
    seg0.segmentIndex = 0;
    seg0.spaceType = trj::SegmentSpaceType::CartesianLine;
    trj::Waypoint w0;
    w0.kind = trj::WaypointKind::Via;
    w0.segmentIndex = 0;
    w0.target = axisAnglePose(rw::math::Vector3D<double>::z(), 0.0);
    trj::Waypoint w1;
    w1.kind = trj::WaypointKind::TaskPoint;
    w1.segmentIndex = 0;
    w1.target = poseA;
    w1.sourceTaskPoint = sourceOid;
    seg0.waypoints = {w0, w1};

    trj::TrajectorySegment seg1;
    seg1.segmentIndex = 1;
    seg1.spaceType = trj::SegmentSpaceType::CartesianLine;
    trj::Waypoint w2;
    w2.kind = trj::WaypointKind::TaskPoint;
    w2.segmentIndex = 1;
    w2.target = poseB;
    w2.sourceTaskPoint = sourceOid;
    trj::Waypoint w3;
    w3.kind = trj::WaypointKind::Via;
    w3.segmentIndex = 1;
    w3.target = axisAnglePose(rw::math::Vector3D<double>::z(), 0.0);
    seg1.waypoints = {w2, w3};
    return {seg0, seg1};
}

/// 边界姿态角差的解析期望（最短弧：min(|Δ|, 2π−|Δ|)——独立闭式 oracle）。
double expectedShortestAngle(double delta)
{
    const double pi = 3.14159265358979323846;
    const double absolute = std::fabs(delta);
    return std::min(absolute, 2.0 * pi - absolute);
}

}  // namespace

// =====================================================================
// acceptance 1——度量换核的独立 oracle（无碰撞直连采纳路径）
// =====================================================================

/**
 * 直连采纳（碰撞检查不适用）段的 pathLengthJoint 期望＝两端点单对
 * Manhattan 距离。独立参考实现逐位对照（EXPECT_DOUBLE_EQ——构造性同
 * 序算式），同时钉住端点还原与两路点段结构：换核不得改变段几何契约。
 * L2 供给（makeEuclidean 单对 distance）经同函数的分段数间接受观测于
 * PlannerAvoidance 黄金算例（直连筛查分辨率敏感性）——多重折叠与采样
 * 网格的装置级观测面受零模型构造红线限制，登记为覆盖限制（README）。
 */
TEST(TrjPlannerReuse, DirectAdoptPathLengthMatchesIndependentOracle_REUSE_T32)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03"},
                  std::vector<std::string>{"REUSE-T32"});

    // 六轴非对称端点（无理量级分量——混合计量下的求和/舍入敏感输入；
    // 上限内且 lower<upper）。
    const rw::math::Q startQ(0.1, -0.75, 1.0 / 3.0, 0.9, -1.25, 0.05);
    const rw::math::Q endQ(-0.4, 0.85, 2.0 / 7.0, -1.1, 0.6, -0.35);

    trj::AvoidancePlanRequest request;
    request.startQ = startQ;
    request.endQ = endQ;
    request.lowerBoundQ = rw::math::Q(startQ.size(), -6.5);
    request.upperBoundQ = rw::math::Q(startQ.size(), 6.5);
    request.constraint.limitsScaleFactor = 1.0;
    request.constraint.cartesianSampleStep = 0.05;
    request.constraint.collisionCheckApplicable = false;  // 零会话/视图义务
    request.startKind = trj::WaypointKind::Start;
    request.endKind = trj::WaypointKind::End;
    request.tcpRef = seededObjectId<core::ObjectId>("trj-reuse-tcp");
    request.plannerFamilyToken = trj::kTrjPlannerFamilyRrtConnect;
    request.plannerParams = {{trj::kTrjPlannerParamExtend, "0.3"},
                             {trj::kTrjPlannerParamEdgeResolution, "0.05"},
                             {trj::kTrjPlannerParamCandidateAttempts, "3"}};
    request.timeBudgetS = 5.0;
    request.seed = 7U;

    auto adapter = trj::makePathPlannerAdapter();
    const trj::AvoidancePlanResult result =
        trj::planPtpWithObstacleAvoidance(request, *adapter);

    ASSERT_EQ(result.status, trj::AvoidanceStatus::Ok);
    EXPECT_TRUE(result.directPathAdopted);
    ASSERT_EQ(result.segment.waypoints.size(), 2U);
    EXPECT_EQ(result.segment.waypoints.front().q, startQ);
    EXPECT_EQ(result.segment.waypoints.back().q, endQ);
    // L1 逐位对照：单对距离＝Σ|Δq|（与 MetricUtil::dist1 同序的独立参考）。
    EXPECT_DOUBLE_EQ(result.segment.pathLengthJoint, referenceManhattan(endQ, startQ));
    // 采样 L2 与 L1 的算式区分（同一对输入下 Linf/L1/L2 互异——防止度量
    // 供给串线）。
    EXPECT_NE(referenceEuclidean(endQ, startQ), referenceManhattan(endQ, startQ));
}

// =====================================================================
// acceptance 2——rotationAngleBetween 分支边界反例（保留 2·atan2 的
// 真实测量证据；EAA::angle/Rotation3DAngleMetric 为被否候选）
// =====================================================================

/**
 * 被否候选反例（真实测量，登记于任务 README）：
 *  - 轴对齐 Δ=1e-6（acos 分支边界）：EAA(Rrel).angle() 实测偏差≈4.4e-11；
 *  - 一般轴 Δ=π−3e-6（acos 近 π 损失带）：实测偏差≈1.6e-10（邻域扫描
 *    π−{1.5,2,3,5}e-6 为 8.1e-11～1.6e-10——系统性非个例）。
 * 两点均超 C7 角度容差 1e-12 约 40~160 倍；产品 2·atan2(‖vec‖,|w|)
 * 全网格（0/微角/一般角/近π/恰π×4轴）1e-12 内命中解析期望（atan2 两端
 * 稳健，无 acos 死区）。注意：EAA 的 0/π 邻域一阶补偿分支在补偿带内精
 * 确（π−1e-9 实测偏差 0），反例在其边界带——原矩阵行"候选EAA::angle；
 * 须核0、微小角、pi及相对旋转方向"就此结案。
 */
TEST(TrjPlannerReuse, RotationAngleCounterExampleKeepsAtan2_REUSE_T32)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02", "TRJ-06"},
                  std::vector<std::string>{"REUSE-T32"});

    const trj::GeometricContinuityTolerances tolerances{};
    // 输入矩阵经 Quaternion 半角分量精确构造（绕开上游 EAA::toRotation3D
    // 的 theta<1e-6 死区）——两条被比较路径读同一矩阵，测量才可归因。

    // ---- 被否候选反例一：轴对齐 Δ=1e-6（acos 分支边界，实测 err≈4.4e-11）----
    {
        const double delta = 1e-6;
        const auto segments = twoSegmentBoundary(
            axisAnglePose(rw::math::Vector3D<double>::z(), 0.0),
            axisAnglePose(rw::math::Vector3D<double>::z(), delta));
        const trj::ContinuityReport report =
            trj::checkGeometricContinuity(segments, tolerances);
        ASSERT_EQ(report.boundaries.size(), 1U);
        ASSERT_TRUE(report.boundaries[0].orientationDeltaRad.has_value());
        // 产品 2·atan2 在 1e-12 内命中解析期望（C7 角度量纲）。
        EXPECT_NEAR(*report.boundaries[0].orientationDeltaRad, delta, 1e-12);
        // 候选路径 EAA(Rrel).angle() 同矩阵实测超 C7 容差——保留手写的依据。
        const rw::math::Rotation3D<double> rrel =
            segments[0].waypoints[1].target->R()
            * rw::math::inverse(segments[1].waypoints[0].target->R());
        const double eaaAngle = rw::math::EAA<double>(rrel).angle();
        EXPECT_GT(std::fabs(eaaAngle - delta), 1e-12)
            << "EAA acos 分支边界实测偏差应超 1e-12（反例前提失效须重审候选）";
    }

    // ---- 被否候选反例二：一般轴 Δ=π−3e-6（acos 损失带，实测 dev≈1.6e-10）----
    {
        const double delta = 3.14159265358979323846 - 3e-6;
        const rw::math::Vector3D<double> axis =
            rw::math::Vector3D<double>(1.0, 2.0, 3.0) / std::sqrt(14.0);
        const auto segments = twoSegmentBoundary(axisAnglePose(axis, 0.0),
                                                 axisAnglePose(axis, delta));
        const trj::ContinuityReport report =
            trj::checkGeometricContinuity(segments, tolerances);
        ASSERT_EQ(report.boundaries.size(), 1U);
        ASSERT_TRUE(report.boundaries[0].orientationDeltaRad.has_value());
        EXPECT_NEAR(*report.boundaries[0].orientationDeltaRad, delta, 1e-12);
        const rw::math::Rotation3D<double> rrel =
            segments[0].waypoints[1].target->R()
            * rw::math::inverse(segments[1].waypoints[0].target->R());
        const double eaaAngle = rw::math::EAA<double>(rrel).angle();
        EXPECT_GT(std::fabs(eaaAngle - delta), 1e-12)
            << "EAA 一般轴近 pi 实测偏差应超 1e-12（反例前提失效须重审候选）";
    }

    // ---- 常规域网格：轴×角的解析闭式一致（0/微角/一般角/近 π/恰 π）----
    // 产品 2·atan2 全网格 1e-12 内命中（atan2 两端稳健，无 acos 死区）。
    const std::vector<rw::math::Vector3D<double>> axes{
        rw::math::Vector3D<double>::x(),
        rw::math::Vector3D<double>::y(),
        rw::math::Vector3D<double>::z(),
        rw::math::Vector3D<double>(1.0, 2.0, 3.0) / std::sqrt(14.0)};
    const std::vector<double> angles{
        0.0, 1e-12, 1e-9, 1e-6, 1e-3, 0.1, 0.5, 1.2, 2.5,
        3.14159265358979323846 - 1e-9, 3.14159265358979323846 - 1e-6,
        3.14159265358979323846};
    for (const rw::math::Vector3D<double>& axis : axes) {
        for (const double angle : angles) {
            const auto segments = twoSegmentBoundary(
                axisAnglePose(axis, 0.0), axisAnglePose(axis, angle));
            const trj::ContinuityReport report =
                trj::checkGeometricContinuity(segments, tolerances);
            ASSERT_EQ(report.boundaries.size(), 1U);
            ASSERT_TRUE(report.boundaries[0].orientationDeltaRad.has_value());
            EXPECT_NEAR(*report.boundaries[0].orientationDeltaRad,
                        expectedShortestAngle(angle), 1e-12)
                << "轴角网格偏差：angle=" << angle;
        }
    }

    // ---- 零差（完全同姿态）边界：恰 0 且通过 ----
    {
        const rw::math::Transform3D<double> same =
            axisAnglePose(rw::math::Vector3D<double>::z(), 0.3);
        const auto segments = twoSegmentBoundary(same, same);
        const trj::ContinuityReport report =
            trj::checkGeometricContinuity(segments, tolerances);
        ASSERT_EQ(report.boundaries.size(), 1U);
        ASSERT_TRUE(report.boundaries[0].orientationDeltaRad.has_value());
        EXPECT_DOUBLE_EQ(*report.boundaries[0].orientationDeltaRad, 0.0);
        EXPECT_TRUE(*report.boundaries[0].orientationOk);
    }
}
