/**
 * @file   PtpSequenceTest.cpp
 * @brief  关节空间 PTP 与任务序列展开用例组（TrjPtpSequence）——TRJ-01
 *         "关节空间点到点路径和由任务点组成的有序作业序列"的模型测试
 *         （任务契约 WP-16-T04 acceptance 1；黄金基准＝解析算例，对照
 *         容差按附录 D 第 9 项声明——标量相对 1×10⁻⁹，经 core::close-
 *         Within/allCloseWithin 逐元素判定）。
 *
 * 设计依据：
 *   - units/trajectory.md §7.3（逐轴线性＋同步系数）、§7.4（限位凸组合
 *     性质——黄金算例覆盖该性质；端点守卫）、§7.5（构型选择三键全序）、
 *     §7.6（失败语义——TRJ-LIMIT-EXCEEDED/TRJ-NO-PATH/TRJ-INPUT-INVALID
 *     素材归类）、§5.4（序列展开四步骤）、§17.2 V-01/02/03（本组覆盖其
 *     解析算例半区——V-02 的时间侧"各轴同时到达"归 WP-16-T08 时间化
 *     用例，本组断言几何同步系数一致）
 *   - REQUIREMENTS TRJ-01（模型语义）、TRJ-06（越限定位）、AT-06（PTP
 *     基准——解析算例口径）、附录 D 第 9 项（解析对照容差）
 *   - 先例：kinematics test（gtest＋IRD_TEST_INFO 需求追溯形态）
 *
 * 容差声明（附录 D C7 测试对照口径——逐例声明处）：解析算例对照使用
 * Tolerance{relative=1e-9, absolute=0}（第 9 项"标量相对 1×10⁻⁹"）；
 * 端点还原另以 C7 运行校验量纲（角度 1×10⁻¹² rad）复核——s=0/1 时插值
 * 公式浮点下精确还原端点，两档容差均应满足。
 */

#include <sdurws/ird/trajectory/Ptp.hpp>
#include <sdurws/ird/trajectory/Sequence.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

using sdurws::ird::core::ObjectId;
using sdurws::ird::core::QuantityKind;
using sdurws::ird::core::Tolerance;
using sdurws::ird::core::allCloseWithin;
using sdurws::ird::core::closeWithin;
using sdurws::ird::trajectory::FailedSegmentRecord;
using sdurws::ird::trajectory::PlannedSegment;
using sdurws::ird::trajectory::PtpCandidate;
using sdurws::ird::trajectory::PtpPlanResult;
using sdurws::ird::trajectory::PtpRequest;
using sdurws::ird::trajectory::PtpStatus;
using sdurws::ird::trajectory::SegmentConstraint;
using sdurws::ird::trajectory::SegmentSpaceType;
using sdurws::ird::trajectory::SequencePlan;
using sdurws::ird::trajectory::SequenceRequest;
using sdurws::ird::trajectory::SequenceSegmentAxis;
using sdurws::ird::trajectory::SequenceSegmentRole;
using sdurws::ird::trajectory::SequenceStationInput;
using sdurws::ird::trajectory::SequenceTaskLevel;
using sdurws::ird::trajectory::SequenceTaskPoint;
using sdurws::ird::trajectory::TrajectoryError;
using sdurws::ird::trajectory::TrajectorySegment;
using sdurws::ird::trajectory::Waypoint;
using sdurws::ird::trajectory::WaypointKind;
using sdurws::ird::trajectory::choosePtpCandidate;
using sdurws::ird::trajectory::expandSequence;
using sdurws::ird::trajectory::interpolateJointLinear;
using sdurws::ird::trajectory::kTrjInputInvalid;
using sdurws::ird::trajectory::kTrjLimitExceeded;
using sdurws::ird::trajectory::kTrjNoPath;
using sdurws::ird::trajectory::kTrjBranchJump;
using sdurws::ird::trajectory::planPtpSegment;

namespace {

/// 附录 D 第 9 项解析算例对照容差（本测试文件的黄金对照声明——C7"黄金
/// 算例可自带更严值并在数据集内声明"；标量相对 1×10⁻⁹）。
Tolerance goldenTolerance()
{
    return Tolerance::make(1e-9, 0.0);
}

/// 构造权威关节向量（转动关节——rad；AGENTS §2.5 单位显式）。
rw::math::Q makeQ(std::initializer_list<double> vals)
{
    rw::math::Q q(vals.size());
    std::size_t i = 0;
    for (const double v : vals) {
        q[i++] = v;
    }
    return q;
}

/// 构造 PTP 请求基线（3 轴解析算例——评价区间 [(-1,-1,-1),(2,2,2)] rad；
/// 各用例微调字段构造特定场景）。
PtpRequest makePtpRequest(const rw::math::Q& start, const std::vector<PtpCandidate>& cands)
{
    PtpRequest req;
    req.startQ = start;
    req.lowerBoundQ = makeQ({-1.0, -1.0, -1.0});
    req.upperBoundQ = makeQ({2.0, 2.0, 2.0});
    req.candidates = cands;
    req.ikContinuityThreshold = 1e-6;              // rad 逐轴上界
    req.constraint = SegmentConstraint{};          // 默认合法（scale=1.0）
    req.tcpRef = ObjectId::generate();             // 合法对象身份（§6.3）
    req.sourceTaskPoint = ObjectId::generate();    // 到达站（终点路点来源——§6.3）
    req.segmentIndex = 0;
    return req;
}

/// 单候选便利构造（q＋stableIndex＋margin——§7.5 三键）。
PtpCandidate cand(std::initializer_list<double> q, std::uint32_t idx, double margin)
{
    PtpCandidate c;
    c.q = makeQ(q);
    c.stableIndex = idx;
    c.minimumJointMargin = margin;
    return c;
}

/// 构造站输入（纯关节站——approach/retract 均禁用；§5.4 投影注入值）。
SequenceStationInput station(const std::string& name, const ObjectId& oid,
                             std::vector<PtpCandidate> cands,
                             const std::string& prev = std::string(),
                             bool enabled = true)
{
    SequenceStationInput st;
    st.point.pointOid = oid;
    st.point.name = name;
    st.point.level = SequenceTaskLevel::Must;
    st.point.enabled = enabled;
    if (!prev.empty()) {
        st.point.sequenceKey = prev;  // 前驱条目名引用（I-REQ-7 语义）
    }
    st.candidates = std::move(cands);
    return st;
}

/// 构造序列展开请求基线（2 轴；区间 [(-5,-5),(5,5)] rad；起始 (0,0)）。
SequenceRequest makeSequenceRequest(std::vector<SequenceStationInput> stations)
{
    SequenceRequest req;
    req.startQ = makeQ({0.0, 0.0});                // rad（startStateRef 解析投影）
    req.lowerBoundQ = makeQ({-5.0, -5.0});         // rad
    req.upperBoundQ = makeQ({5.0, 5.0});           // rad
    req.tcpRef = ObjectId::generate();
    req.constraint = SegmentConstraint{};
    req.ikContinuityThreshold = 1e-6;              // rad 逐轴上界
    req.stations = std::move(stations);
    return req;
}

/// 注入编排端口替身：返回结构合法的几何和构型，只验证 Sequence 的
/// 结果消费与游标推进；不调用真实 IK，不构成 CartesianLine 整链证据。
void installCartesianPlanner(SequenceRequest& req)
{
    req.cartesianPlanner = [](const SequenceRequest::CartesianPlanRequest& input) {
        SequenceRequest::CartesianPlanOutcome outcome;
        TrajectorySegment geometry;
        geometry.segmentIndex = input.segmentIndex;
        geometry.spaceType = SegmentSpaceType::CartesianLine;
        geometry.tcpRef = input.tcpRef;
        geometry.frameRef = input.frameRef;
        geometry.constraint = input.constraint;
        geometry.sourceTaskPoint = input.sourceTaskPoint;
        const rw::math::Transform3D<double> pose =
            input.workPose.value_or(rw::math::Transform3D<double>());
        Waypoint start;
        start.kind = WaypointKind::Via;
        start.segmentIndex = input.segmentIndex;
        start.target = pose;
        Waypoint end;
        end.kind = WaypointKind::TaskPoint;
        end.segmentIndex = input.segmentIndex;
        end.target = pose;
        end.sourceTaskPoint = input.sourceTaskPoint;
        geometry.waypoints = {start, end};
        geometry.pathLengthTcp = input.segment.distanceM;
        outcome.geometry = geometry;
        outcome.startQ = input.branchSeedQ;
        outcome.endQ = input.branchSeedQ;
        return outcome;
    };
}

}  // namespace

// =====================================================================
// 关节空间插值（§7.3——V-01/V-02 几何半区）
// =====================================================================

/** 端点还原：s=0/1 时插值精确回到两端（附录 D 角度量纲 1e-12 内）。 */
TEST(TrjPtp, InterpolateEndpointsRestore_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{"AT-06"});
    const rw::math::Q qa = makeQ({0.1, -0.2, 0.3});   // rad
    const rw::math::Q qb = makeQ({1.1, 0.8, -0.7});   // rad
    // 端点还原容差＝附录 D C7 运行校验口径（角度 ε_abs=1e-12 rad；零参考
    // 退化项不适用——参考值非零，相对项取 0 纯绝对口径）。
    const Tolerance endpointTol = Tolerance::make(0.0, 1e-12);
    std::vector<double> atZero, atOne;
    const rw::math::Q s0 = interpolateJointLinear(qa, qb, 0.0);
    const rw::math::Q s1 = interpolateJointLinear(qa, qb, 1.0);
    for (std::size_t i = 0; i < 3U; ++i) {
        atZero.push_back(s0[i]);
        atOne.push_back(s1[i]);
    }
    EXPECT_TRUE(allCloseWithin(atZero, {qa[0], qa[1], qa[2]}, endpointTol));
    EXPECT_TRUE(allCloseWithin(atOne, {qb[0], qb[1], qb[2]}, endpointTol));
}

/** 解析算例：中点 s=0.5 逐轴线性（附录 D 第 9 项——相对 1e-9 逐元素）。 */
TEST(TrjPtp, InterpolateMidpointAnalytic_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{"AT-06"});
    const rw::math::Q qa = makeQ({0.1, -0.2, 0.3});    // rad
    const rw::math::Q qb = makeQ({1.1, 0.8, -0.7});    // rad
    // 解析期望：中点＝(qa+qb)/2＝(0.6, 0.3, -0.2) rad（手工解析值）。
    const rw::math::Q mid = interpolateJointLinear(qa, qb, 0.5);
    EXPECT_TRUE(allCloseWithin({mid[0], mid[1], mid[2]}, {0.6, 0.3, -0.2},
                               goldenTolerance()));
}

/** 多关节同步（V-02 几何半区）：三轴差量不同，同一 s 对各轴一致——
 *  几何同步系数；时间侧"同时到达"归 WP-16-T08 时间化用例。 */
TEST(TrjPtp, InterpolateSynchronizedAcrossAxes_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{});
    const rw::math::Q qa = makeQ({0.0, 1.0, -2.0});    // rad
    const rw::math::Q qb = makeQ({4.0, 3.0, 2.0});     // rad（差量 4/2/4）
    const rw::math::Q q25 = interpolateJointLinear(qa, qb, 0.25);
    // 解析期望（s=0.25）：qa + 0.25·(qb-qa) = (1.0, 1.5, -1.0) rad。
    EXPECT_TRUE(allCloseWithin({q25[0], q25[1], q25[2]}, {1.0, 1.5, -1.0},
                               goldenTolerance()));
}

/** 插值违约面：维度不一致与 s 越界——fail-fast（§7.3 定义域）。 */
TEST(TrjPtp, InterpolateRejectsContractViolations_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    const rw::math::Q qa = makeQ({0.0, 0.0});
    const rw::math::Q qb = makeQ({1.0, 1.0, 1.0});
    EXPECT_THROW(interpolateJointLinear(qa, qb, 0.5), TrajectoryError);
    const rw::math::Q qc = makeQ({0.0, 0.0, 0.0});
    EXPECT_THROW(interpolateJointLinear(qc, qc, 1.5), TrajectoryError);
    EXPECT_THROW(interpolateJointLinear(qc, qc, -0.1), TrajectoryError);
}

// =====================================================================
// 构型选择（§7.5 三键全序）
// =====================================================================

/** 规则 1：延续性优先——逐轴 |Δq|≤阈值的候选胜出（即使裕量更小）。 */
TEST(TrjPtp, ChoosePrefersContinuity_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "NFR-COR-02"}, std::vector<std::string>{});
    const rw::math::Q start = makeQ({0.0, 0.0});                       // rad
    const std::vector<PtpCandidate> cands = {
        cand({2.0, 2.0}, 0U, 0.9),              // 跳跃解（裕量大）
        cand({1e-6, 1e-6}, 1U, 0.5),            // 延续解（逐轴差=1e-6≤阈值）
    };
    // 阈值 1e-6（rad 逐轴上界）——延续集={候选 2}，规则 2/3 在集内定序。
    EXPECT_EQ(choosePtpCandidate(start, cands, 1e-6), 1U);
}

/** 规则 2：无延续候选→裕量降序（与 kinematics 稳定排序第一键一致）。 */
TEST(TrjPtp, ChoosePrefersMarginWhenNoContinuity_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{});
    const rw::math::Q start = makeQ({0.0, 0.0});                       // rad
    const std::vector<PtpCandidate> cands = {
        cand({1.0, 1.0}, 0U, 0.4),              // 均超阈——无延续
        cand({2.0, 2.0}, 1U, 0.9),              // 裕量最大者
    };
    EXPECT_EQ(choosePtpCandidate(start, cands, 1e-6), 1U);
}

/** 规则 3：裕量精确并列→stableIndex 升序兜底（全序保证）。 */
TEST(TrjPtp, ChooseTieBreaksByStableIndex_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});
    const rw::math::Q start = makeQ({0.0, 0.0});                       // rad
    const std::vector<PtpCandidate> cands = {
        cand({1.0, 1.0}, 1U, 0.7),              // stableIndex 大（向量下标 0）
        cand({-1.0, -1.0}, 0U, 0.7),            // stableIndex 小（向量下标 1）
    };
    // 裕量精确并列→stableIndex 0 者胜＝向量下标 1（返回值是候选向量下标）。
    EXPECT_EQ(choosePtpCandidate(start, cands, 1e-6), 1U);
}

/** 选择违约面：空候选集（§15.1 非法示例——fail-fast）与 NaN 裕量。 */
TEST(TrjPtp, ChooseRejectsContractViolations_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    const rw::math::Q start = makeQ({0.0, 0.0});
    EXPECT_THROW(choosePtpCandidate(start, {}, 1e-6), TrajectoryError);
    PtpCandidate bad = cand({1.0, 1.0}, 0U, 0.5);
    bad.minimumJointMargin = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(choosePtpCandidate(start, {bad}, 1e-6), TrajectoryError);
}

// =====================================================================
// PTP 段规划（§7——V-01/V-02/V-03）
// =====================================================================

/** V-01 基准（解析算例）：段几何＝JointLinear；端点还原；路径长度解析值。 */
TEST(TrjPtp, PlanPtpGoldenTwoPoint_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{"AT-06"});
    const rw::math::Q start = makeQ({0.1, -0.2, 0.3});                 // rad
    const PtpRequest req = makePtpRequest(start, {cand({1.1, 0.8, -0.7}, 0U, 0.8)});
    const PtpPlanResult out = planPtpSegment(req);

    // 结局与段类型（§7.1——关节空间逐轴线性）。
    ASSERT_EQ(out.status, PtpStatus::Ok);
    ASSERT_TRUE(out.selectedCandidateIndex.has_value());
    EXPECT_EQ(*out.selectedCandidateIndex, 0U);
    EXPECT_EQ(out.segment.spaceType, SegmentSpaceType::JointLinear);

    // 端点还原（附录 D C7 角度量纲 1e-12 rad）与路点种类（§6.2）。
    ASSERT_EQ(out.segment.waypoints.size(), 2U);
    EXPECT_EQ(out.segment.waypoints[0].kind, WaypointKind::Start);
    EXPECT_EQ(out.segment.waypoints[1].kind, WaypointKind::TaskPoint);
    const Tolerance endpointTol = Tolerance::make(0.0, 1e-12);
    const rw::math::Q& endQ = req.candidates[0].q;
    for (std::size_t i = 0; i < 3U; ++i) {
        EXPECT_TRUE(closeWithin(out.segment.waypoints[0].q.value()[i], start[i], endpointTol));
        EXPECT_TRUE(closeWithin(out.segment.waypoints[1].q.value()[i], endQ[i], endpointTol));
    }
    // 来源任务点缺席（服务直调未绑定站）不伪造；段结构守卫通过（Ok 产出
    // 前已调 validateTrajectorySegment——此处断言为留痕复核）。
    EXPECT_NO_THROW(sdurws::ird::trajectory::validateTrajectorySegment(out.segment));

    // 路径长度解析值：Σ|Δq| = 1.0+1.0+1.0 = 3.0（rad|m 混合计量——§6.2；
    // 附录 D 第 9 项相对 1e-9 对照）。
    EXPECT_TRUE(closeWithin(out.segment.pathLengthJoint, 3.0, goldenTolerance()));
    // TCP 路径长度本批不计算（FK 派生观察——§7.6 不冒充笛卡尔证据）。
    EXPECT_EQ(out.segment.pathLengthTcp, 0.0);
}

/** 凸组合限位性质（§7.4 黄金算例）：端点在区间内→行程采样全部在区间内。 */
TEST(TrjPtp, PlanPtpConvexCombinationStaysInBounds_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{});
    const rw::math::Q start = makeQ({-0.9, 0.5, 1.9});                 // rad（近界）
    const PtpRequest req = makePtpRequest(start, {cand({1.9, -0.9, -0.5}, 0U, 0.6)});
    const PtpPlanResult out = planPtpSegment(req);
    ASSERT_EQ(out.status, PtpStatus::Ok);

    // 凸组合数学性质（§7.4"线性插值保持凸组合→段内不越限位"）的采样
    // 验证：s∈{0,0.1,…,1} 逐点逐轴核对评价区间（浮点下端点邻域用闭区间
    // 比较——凸组合内点严格在界内，端点由第 3 步守卫已保证）。
    const rw::math::Q& qa = req.startQ;
    const rw::math::Q& qb = out.segment.waypoints[1].q.value();
    for (int step = 0; step <= 10; ++step) {
        const double s = step / 10.0;
        const rw::math::Q qs = interpolateJointLinear(qa, qb, s);
        for (std::size_t i = 0; i < 3U; ++i) {
            ASSERT_GE(qs[i], req.lowerBoundQ[i]) << "s=" << s << " 轴 " << i;
            ASSERT_LE(qs[i], req.upperBoundQ[i]) << "s=" << s << " 轴 " << i;
        }
    }
}

/** V-03 越限守卫：起点越限→NoPath＋TRJ-LIMIT-EXCEEDED 素材（比较型齐备）。 */
TEST(TrjPtp, PlanPtpRejectsStartOutOfBounds_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "TRJ-06"}, std::vector<std::string>{"AT-06"});
    PtpRequest req = makePtpRequest(makeQ({-0.5, 0.0, 0.0}),          // rad（x 轴越下界）
                                    {cand({0.5, 0.0, 0.0}, 0U, 0.5)});
    req.lowerBoundQ = makeQ({0.0, -1.0, -1.0});                       // 下界 0
    const PtpPlanResult out = planPtpSegment(req);

    // 素材轨（非异常）：NoPath＋TRJ-LIMIT-EXCEEDED（码常量比对——禁拼码）。
    ASSERT_EQ(out.status, PtpStatus::NoPath);
    ASSERT_TRUE(out.failure.has_value());
    EXPECT_EQ(out.failure->reasonToken, std::string(kTrjLimitExceeded));
    EXPECT_EQ(out.failure->pathParameter.value_or(-1.0), 0.0);        // 端点 s=0
    ASSERT_TRUE(out.failure->comparison.has_value());                  // UX-03 三要素
    EXPECT_TRUE(out.failure->comparison->actual.quantity.state()
                == sdurws::ird::core::FieldState::Provided);
    EXPECT_TRUE(out.failure->comparison->expected.quantity.state()
                == sdurws::ird::core::FieldState::Provided);
}

/** V-03 越限守卫：终点越限→NoPath＋素材定位到 s=1。 */
TEST(TrjPtp, PlanPtpRejectsEndOutOfBounds_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06"}, std::vector<std::string>{});
    PtpRequest req = makePtpRequest(makeQ({0.0, 0.0, 0.0}),
                                    {cand({2.5, 0.0, 0.0}, 0U, 0.5)}); // 越上界 2.0
    const PtpPlanResult out = planPtpSegment(req);
    ASSERT_EQ(out.status, PtpStatus::NoPath);
    ASSERT_TRUE(out.failure.has_value());
    EXPECT_EQ(out.failure->reasonToken, std::string(kTrjLimitExceeded));
    EXPECT_EQ(out.failure->pathParameter.value_or(-1.0), 1.0);         // 端点 s=1
}

TEST(TrjPtp, UsesLinearUnitForPrismaticLimitDiagnostic_WP16T04_ACC1)
{
    // TRJ-06：移动关节越限的比较型诊断必须使用 m，而不是沿用转动轴 rad。
    PtpRequest request = makePtpRequest(makeQ({0.0}), {cand({2.0}, 1U, 0.8)});
    request.lowerBoundQ = makeQ({-1.0});
    request.upperBoundQ = makeQ({1.0});
    request.jointQuantityKinds = {QuantityKind::Length};

    const PtpPlanResult result = planPtpSegment(request);
    ASSERT_EQ(result.status, PtpStatus::NoPath);
    ASSERT_TRUE(result.failure.has_value());
    ASSERT_TRUE(result.failure->comparison.has_value());
    EXPECT_EQ(result.failure->comparison->actual.unit.symbol(), "m");
    EXPECT_EQ(result.failure->comparison->expected.unit.symbol(), "m");
}

/** 零长 PTP 段（§7.2 退化）：起终点相同→合法零长段（路径长度 0）。 */
TEST(TrjPtp, PlanPtpZeroLengthSegmentLegal_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{});
    const rw::math::Q q = makeQ({0.5, -0.5, 0.25});                    // rad
    const PtpPlanResult out = planPtpSegment(makePtpRequest(q, {cand({0.5, -0.5, 0.25}, 0U, 0.9)}));
    ASSERT_EQ(out.status, PtpStatus::Ok);
    EXPECT_EQ(out.segment.waypoints.size(), 2U);                       // 段端点仍在
    EXPECT_TRUE(closeWithin(out.segment.pathLengthJoint, 0.0, Tolerance::make(0.0, 1e-12)));
}

/** 取消（UX-03）：取消观测命中→Canceled 且零失败素材（取消不是错误）。 */
TEST(TrjPtp, PlanPtpCancelYieldsZeroMaterial_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-01", "UX-03"}, std::vector<std::string>{});
    PtpRequest req = makePtpRequest(makeQ({0.0, 0.0, 0.0}),
                                    {cand({1.0, 1.0, 1.0}, 0U, 0.5)});
    req.cancel = [] { return true; };                                  // 入口即取消
    const PtpPlanResult out = planPtpSegment(req);
    EXPECT_EQ(out.status, PtpStatus::Canceled);
    EXPECT_FALSE(out.failure.has_value());                             // 零错误素材
    EXPECT_FALSE(out.selectedCandidateIndex.has_value());
}

/** 前置违约面：空候选集/无效 tcpRef/维度不符——fail-fast（§15.1）。 */
TEST(TrjPtp, PlanPtpRejectsContractViolations_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    // 空候选集（§15.1 非法示例原文）。
    EXPECT_THROW(planPtpSegment(makePtpRequest(makeQ({0.0, 0.0, 0.0}), {})), TrajectoryError);
    // tcpRef 非法（§6.3 必填合法对象 ID）。
    PtpRequest noTcp = makePtpRequest(makeQ({0.0, 0.0, 0.0}),
                                      {cand({1.0, 1.0, 1.0}, 0U, 0.5)});
    noTcp.tcpRef = ObjectId{};
    EXPECT_THROW(planPtpSegment(noTcp), TrajectoryError);
    // 评价区间维度不符。
    PtpRequest badDim = makePtpRequest(makeQ({0.0, 0.0, 0.0}),
                                       {cand({1.0, 1.0, 1.0}, 0U, 0.5)});
    badDim.lowerBoundQ = makeQ({0.0, 0.0});
    EXPECT_THROW(planPtpSegment(badDim), TrajectoryError);
    // 候选维度不符（投影面数据违约）。
    EXPECT_THROW(planPtpSegment(makePtpRequest(makeQ({0.0, 0.0, 0.0}),
                                               {cand({1.0, 1.0}, 0U, 0.5)})),
                 TrajectoryError);
}

/** 多候选选择记录：Ok 时 selectedCandidateIndex 指向延续候选（§7.5 第 4 条
 *  ——选择结果可回放）。 */
TEST(TrjPtp, PlanPtpRecordsSelectedCandidate_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});
    PtpRequest req = makePtpRequest(makeQ({0.0, 0.0, 0.0}),
                                    {cand({2.0, 2.0, 2.0}, 0U, 0.9),    // 跳跃解
                                     cand({5e-7, 5e-7, 5e-7}, 1U, 0.4)}); // 延续解
    const PtpPlanResult out = planPtpSegment(req);
    ASSERT_EQ(out.status, PtpStatus::Ok);
    ASSERT_TRUE(out.selectedCandidateIndex.has_value());
    EXPECT_EQ(*out.selectedCandidateIndex, 1U);                        // 延续性优先
    // 段终点＝被选构型（逐轴一致——端点还原口径 1e-12 rad）。
    const rw::math::Q& endQ = out.segment.waypoints[1].q.value();
    EXPECT_TRUE(allCloseWithin({endQ[0], endQ[1], endQ[2]},
                               {5e-7, 5e-7, 5e-7}, Tolerance::make(0.0, 1e-12)));
}

// =====================================================================
// 任务序列展开（§5.4——TRJ-01 有序作业序列）
// =====================================================================

/** 正常链：三站链式 sequenceKey、乱序输入→拓扑序展开＋站间 PTP 几何。 */
TEST(TrjSequence, ExpandTopologicalChain_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{"AT-06"});
    const ObjectId oidA = ObjectId::generate();
    const ObjectId oidB = ObjectId::generate();
    const ObjectId oidC = ObjectId::generate();
    // 输入乱序（C、A、B）——展开序由 sequenceKey 拓扑决定（C←B←A）。
    std::vector<SequenceStationInput> stations = {
        station("C", oidC, {cand({0.5, 2.0}, 2U, 0.6)}, "B"),
        station("A", oidA, {cand({0.5, 0.0}, 0U, 0.8)}),
        station("B", oidB, {cand({0.5, 1.0}, 1U, 0.7)}, "A"),
    };
    const SequencePlan plan = expandSequence(makeSequenceRequest(std::move(stations)));

    // 展开成功：无素材、站序＝拓扑序 A→B→C。
    ASSERT_TRUE(plan.failures.empty()) << (plan.failures.empty() ? "" : plan.failures[0].cause);
    ASSERT_FALSE(plan.canceled);
    ASSERT_EQ(plan.stationOrder.size(), 3U);
    EXPECT_EQ(plan.stationOrder[0], oidA);
    EXPECT_EQ(plan.stationOrder[1], oidB);
    EXPECT_EQ(plan.stationOrder[2], oidC);

    // 段链：Transfer+Work ×3（纯关节序列——每站两段）＝6 段，序号连续。
    ASSERT_EQ(plan.segments.size(), 6U);
    for (std::uint32_t i = 0; i < 6U; ++i) {
        EXPECT_EQ(plan.segments[i].segmentIndex, i);
    }

    // 段 0（Transfer→A）：起点=起始构型（kind=Start）＋终点=A 构型
    // （kind=TaskPoint，来源=oidA——可追溯性）。
    {
        const PlannedSegment& seg = plan.segments[0];
        ASSERT_EQ(seg.role, SequenceSegmentRole::TransferPtp);
        ASSERT_TRUE(seg.ptpGeometry.has_value());
        const TrajectorySegment& geo = *seg.ptpGeometry;
        ASSERT_EQ(geo.waypoints.size(), 2U);
        EXPECT_EQ(geo.waypoints[0].kind, WaypointKind::Start);
        EXPECT_TRUE(allCloseWithin({geo.waypoints[0].q.value()[0], geo.waypoints[0].q.value()[1]},
                                   {0.0, 0.0}, goldenTolerance()));
        EXPECT_EQ(geo.waypoints[1].kind, WaypointKind::TaskPoint);
        ASSERT_TRUE(geo.waypoints[1].sourceTaskPoint.has_value());
        EXPECT_EQ(*geo.waypoints[1].sourceTaskPoint, oidA);
        // 路径长度解析值：|0.5-0|+|0-0| = 0.5（rad|m 混合计量）。
        EXPECT_TRUE(closeWithin(geo.pathLengthJoint, 0.5, goldenTolerance()));
    }
    // 段 1（Work A）：来源=oidA、无驻留（未给 dwellDurationS）。
    {
        const PlannedSegment& seg = plan.segments[1];
        EXPECT_EQ(seg.role, SequenceSegmentRole::Work);
        EXPECT_EQ(seg.sourceTaskPoint, oidA);
        EXPECT_FALSE(seg.dwellDurationS.has_value());
    }
    // 段 2（Transfer B→…）：游标推进＝上一站选定构型（§7.2"起点：上一段
    // 终点构型"）——起点=B 的前驱构型 (0.5,0)。
    {
        const PlannedSegment& seg = plan.segments[2];
        ASSERT_TRUE(seg.ptpGeometry.has_value());
        const TrajectorySegment& geo = *seg.ptpGeometry;
        EXPECT_TRUE(allCloseWithin({geo.waypoints[0].q.value()[0], geo.waypoints[0].q.value()[1]},
                                   {0.5, 0.0}, goldenTolerance()));
        EXPECT_EQ(geo.waypoints[1].kind, WaypointKind::TaskPoint);     // 站间转移终点
    }
}

/** 字典序并列消解：两站均无顺序键→按 name 字典序展开（确定性补全）。 */
TEST(TrjSequence, ExpandDeterministicNameOrder_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});
    const ObjectId oidAlpha = ObjectId::generate();
    const ObjectId oidBeta = ObjectId::generate();
    std::vector<SequenceStationInput> stations = {
        station("beta", oidBeta, {cand({1.0, 1.0}, 1U, 0.5)}),
        station("alpha", oidAlpha, {cand({0.5, 0.5}, 0U, 0.5)}),
    };
    const SequencePlan plan = expandSequence(makeSequenceRequest(std::move(stations)));
    ASSERT_TRUE(plan.failures.empty());
    ASSERT_EQ(plan.stationOrder.size(), 2U);
    EXPECT_EQ(plan.stationOrder[0], oidAlpha);                         // 字典序在前
    EXPECT_EQ(plan.stationOrder[1], oidBeta);
}

/** 环/悬空/重复键/无启用站——四类输入非法均以 TRJ-INPUT-INVALID 素材终止
 *  （§5.4"不自行修复顺序"＋§7.2 无启用点）。 */
TEST(TrjSequence, ExpandRejectsInvalidOrderings_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "TRJ-06"}, std::vector<std::string>{});
    const ObjectId oidA = ObjectId::generate();
    const ObjectId oidB = ObjectId::generate();
    const ObjectId oidC = ObjectId::generate();
    const auto oneCand = [](double v) { return std::vector<PtpCandidate>{cand({v, v}, 0U, 0.5)}; };

    // 环：A←B←A（回边）。
    {
        std::vector<SequenceStationInput> stations = {
            station("A", oidA, oneCand(1.0), "B"),
            station("B", oidB, oneCand(2.0), "A"),
        };
        const SequencePlan plan = expandSequence(makeSequenceRequest(std::move(stations)));
        ASSERT_EQ(plan.failures.size(), 1U);
        EXPECT_EQ(plan.failures[0].reasonToken, std::string(kTrjInputInvalid));
    }
    // 悬空键：引用缺席名。
    {
        std::vector<SequenceStationInput> stations = {
            station("A", oidA, oneCand(1.0), "ghost"),
        };
        const SequencePlan plan = expandSequence(makeSequenceRequest(std::move(stations)));
        ASSERT_EQ(plan.failures.size(), 1U);
        EXPECT_EQ(plan.failures[0].reasonToken, std::string(kTrjInputInvalid));
    }
    // 重复键：同一前驱被两站声明（分支＝顺序歧义——I-REQ-7）。
    {
        std::vector<SequenceStationInput> stations = {
            station("A", oidA, oneCand(1.0)),
            station("B", oidB, oneCand(2.0), "A"),
            station("C", oidC, oneCand(3.0), "A"),
        };
        const SequencePlan plan = expandSequence(makeSequenceRequest(std::move(stations)));
        ASSERT_EQ(plan.failures.size(), 1U);
        EXPECT_EQ(plan.failures[0].reasonToken, std::string(kTrjInputInvalid));
    }
    // 无启用站（§7.2——输入形态问题，非不可行）。
    {
        std::vector<SequenceStationInput> stations = {
            station("A", oidA, oneCand(1.0), std::string(), false),    // enabled=false
        };
        const SequencePlan plan = expandSequence(makeSequenceRequest(std::move(stations)));
        ASSERT_EQ(plan.failures.size(), 1U);
        EXPECT_EQ(plan.failures[0].reasonToken, std::string(kTrjInputInvalid));
        EXPECT_TRUE(plan.segments.empty());
    }
}

/** 站候选空集→TRJ-NO-PATH 素材终止（§7.6 行 5——用户数据面失败）。 */
TEST(TrjSequence, ExpandStopsWhenStationUnsolvable_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06"}, std::vector<std::string>{});
    const ObjectId oidA = ObjectId::generate();
    const ObjectId oidB = ObjectId::generate();
    std::vector<SequenceStationInput> stations = {
        station("A", oidA, {cand({0.5, 0.5}, 0U, 0.5)}),
        station("B", oidB, {}),                        // B 无解（空候选集）
    };
    stations[1].point.sequenceKey = "A";
    const SequencePlan plan = expandSequence(makeSequenceRequest(std::move(stations)));
    ASSERT_EQ(plan.failures.size(), 1U);
    EXPECT_EQ(plan.failures[0].reasonToken, std::string(kTrjNoPath));
    ASSERT_TRUE(plan.failures[0].subject.has_value());
    EXPECT_EQ(*plan.failures[0].subject, oidB);                        // 定位到失败站
}

/** 笛卡尔站缺编排服务时须明确失败，不能把 lineSpec 占位认定为完整几何。 */
TEST(TrjSequence, ExpandCartesianStationPlansLineEntries_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "TRJ-02"}, std::vector<std::string>{});
    const ObjectId oidD = ObjectId::generate();
    SequenceStationInput st = station("D", oidD, {cand({0.5, 0.5}, 0U, 0.5)});
    st.point.approach.enabled = true;
    st.point.approach.axis = SequenceSegmentAxis::ToolZ;
    st.point.approach.distanceM = 0.1;                                 // m
    st.point.retract.enabled = true;
    st.point.retract.axis = SequenceSegmentAxis::ReferenceZ;
    st.point.retract.distanceM = 0.08;                                 // m
    st.dwellDurationS = 1.5;                                           // s（驻留投影）

    const SequencePlan plan = expandSequence(makeSequenceRequest({std::move(st)}));
    // 真实编排服务未注入时必须显式失败；旧实现曾返回 lineSpec 占位并
    // 把不可执行序列误报为成功，这个回归钉住该缺口。
    ASSERT_EQ(plan.failures.size(), 1U);
    EXPECT_EQ(plan.failures[0].reasonToken, std::string(kTrjNoPath));
    EXPECT_EQ(plan.failures[0].segmentIndex, 1U);
    EXPECT_TRUE(plan.segments.empty());
}

/** 端口替身已装配时，返回几何必须进入计划并推动游标；仅验证结果消费，
 * 不把替身端点当作真实求解器或正式宿主整链的通过证据。 */
TEST(TrjSequence, ExpandCartesianStationConsumesPlannerGeometry_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "TRJ-02"},
                   std::vector<std::string>{"AT-06"});
    const ObjectId oid = ObjectId::generate();
    SequenceStationInput st = station("D", oid, {cand({0.5, 0.5}, 0U, 0.5)});
    st.point.workPose = rw::math::Transform3D<double>();
    st.point.approach.enabled = true;
    st.point.approach.distanceM = 0.1;
    st.point.retract.enabled = true;
    st.point.retract.axis = SequenceSegmentAxis::ReferenceZ;
    st.point.retract.distanceM = 0.08;
    SequenceRequest req = makeSequenceRequest({std::move(st)});
    installCartesianPlanner(req);
    const SequencePlan plan = expandSequence(req);
    ASSERT_TRUE(plan.failures.empty())
        << (plan.failures.empty() ? "" : plan.failures.front().cause);
    ASSERT_EQ(plan.segments.size(), 4U);
    EXPECT_TRUE(plan.segments[0].ptpGeometry.has_value());
    EXPECT_TRUE(plan.segments[1].cartesianGeometry.has_value());
    EXPECT_TRUE(plan.segments[3].cartesianGeometry.has_value());
    for (std::size_t i = 0; i < plan.segments.size(); ++i) {
        EXPECT_EQ(plan.segments[i].segmentIndex, static_cast<std::uint32_t>(i));
    }
}

/** F-798 后续回归：遇到未装配的进退段立即定位失败；不能把旧游标
 * 或缺几何的占位条目带入后续 C/D 站，冒充完整序列。 */
TEST(TrjSequence, ExpandCursorInvalidStationKeepsWorkAndRetract_F_798)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "TRJ-06"}, std::vector<std::string>{});
    const ObjectId oidA = ObjectId::generate();
    const ObjectId oidB = ObjectId::generate();
    const ObjectId oidC = ObjectId::generate();
    const ObjectId oidD = ObjectId::generate();

    // A 已可规划，B 缺笛卡尔服务；C/D 用于证明不会越过 B 继续拼段。
    SequenceStationInput stB = station("B", oidB, {cand({0.5, 0.5}, 0U, 0.5)}, "A");
    stB.point.approach.enabled = true;
    stB.point.approach.axis = SequenceSegmentAxis::ToolZ;
    stB.point.approach.distanceM = 0.1;                                // m
    stB.point.retract.enabled = true;
    stB.point.retract.axis = SequenceSegmentAxis::ReferenceZ;
    stB.point.retract.distanceM = 0.08;                                // m
    stB.dwellDurationS = 1.5;                                          // s（驻留投影）

    SequenceStationInput stC = station("C", oidC, {cand({1.0, 1.0}, 1U, 0.5)}, "B");
    stC.point.retract.enabled = true;
    stC.point.retract.axis = SequenceSegmentAxis::ToolZ;
    stC.point.retract.distanceM = 0.05;                                // m
    stC.dwellDurationS = 0.8;                                          // s

    std::vector<SequenceStationInput> stations = {
        station("A", oidA, {cand({0.2, 0.2}, 0U, 0.5)}),
        std::move(stB),
        std::move(stC),
        station("D", oidD, {cand({1.5, 1.5}, 2U, 0.5)}, "C"),
    };
    const SequencePlan plan = expandSequence(makeSequenceRequest(std::move(stations)));
    // 服务缺失在 B 站 approach 首段即被定位，后续站不能静默生成占位段。
    ASSERT_EQ(plan.failures.size(), 1U);
    EXPECT_EQ(plan.failures[0].reasonToken, std::string(kTrjNoPath));
    ASSERT_TRUE(plan.failures[0].subject.has_value());
    EXPECT_EQ(*plan.failures[0].subject, oidB);
}

/** 取消：站边界取消观测命中→canceled==true＋零失败素材（UX-03）。 */
TEST(TrjSequence, ExpandCancelYieldsZeroMaterial_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-01", "UX-03"}, std::vector<std::string>{});
    const ObjectId oidA = ObjectId::generate();
    SequenceRequest req = makeSequenceRequest({station("A", oidA, {cand({0.5, 0.5}, 0U, 0.5)})});
    req.cancel = [] { return true; };
    const SequencePlan plan = expandSequence(std::move(req));
    EXPECT_TRUE(plan.canceled);
    EXPECT_TRUE(plan.failures.empty());                                // 取消不是错误
}

/** 展开违约面：重名站（I-REQ-3 集合级唯一性）——fail-fast（调用方违约）。 */
TEST(TrjSequence, ExpandRejectsDuplicateNames_WP16T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    const ObjectId oidA = ObjectId::generate();
    const ObjectId oidB = ObjectId::generate();
    std::vector<SequenceStationInput> stations = {
        station("A", oidA, {cand({0.5, 0.5}, 0U, 0.5)}),
        station("A", oidB, {cand({1.0, 1.0}, 1U, 0.5)}),               // 重名
    };
    EXPECT_THROW(expandSequence(makeSequenceRequest(std::move(stations))), TrajectoryError);
}

/** @brief TRJ-01：任务位姿数值变化必须参与值比较，防止漏掉重规划输入变化。 */
TEST(TrjSequence, WorkPoseEqualityIncludesValues_TRJ01)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01"}, std::vector<std::string>{});
    SequenceTaskPoint original;
    original.workPose=rw::math::Transform3D<double>();
    auto changed=original;
    changed.workPose=rw::math::Transform3D<double>(rw::math::Vector3D<double>(0.0,0.0,0.15));
    EXPECT_NE(original, changed);
    EXPECT_EQ(original,original);
}

/** @brief TRJ-06：非法关节量纲属于输入违约，即使构型不越限也要拒绝。 */
TEST(TrjPtp, RejectsInvalidUnitEvenWithoutLimitViolation_TRJ06)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06"}, std::vector<std::string>{});
    auto req=makePtpRequest(makeQ({0.0,0.0,0.0}),{cand({0.1,0.2,0.3},0U,0.5)});
    req.jointQuantityKinds={QuantityKind::Angle, QuantityKind::Mass, QuantityKind::Angle};
    EXPECT_THROW(planPtpSegment(req),TrajectoryError);
}

/** @brief TRJ-02/06：端口结果的维度、有限性与段身份必须在消费边界复验。 */
TEST(TrjSequence, RejectsMalformedCartesianServiceResults_TRJ02)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02", "TRJ-06"}, std::vector<std::string>{});
    for(int defect=0;defect<3;++defect) {
        auto st=station("A",ObjectId::generate(),{cand({0.5,0.5},0U,0.5)});
        st.point.approach.enabled=true; st.point.approach.distanceM=0.1;
        auto req=makeSequenceRequest({st}); installCartesianPlanner(req);
        const auto planner=req.cartesianPlanner;
        req.cartesianPlanner=[planner,defect](const auto& input) {
            auto result=planner(input);
            if(defect==0){result.endQ=makeQ({0.0});}
            if(defect==1){result.endQ=makeQ({std::numeric_limits<double>::quiet_NaN(),0.0});}
            if(defect==2){result.geometry->segmentIndex=input.segmentIndex+10;}
            return result;
        };
        EXPECT_THROW(expandSequence(req),TrajectoryError) << "损坏类型=" << defect;
    }
}

/** @brief TRJ-02：进退段长度无穷大不是合法几何，不能交给端口继续执行。 */
TEST(TrjSequence, RejectsInfiniteCartesianDistance_TRJ02)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    auto st=station("A",ObjectId::generate(),{cand({0.5,0.5},0U,0.5)});
    st.point.approach.enabled=true;
    st.point.approach.distanceM=std::numeric_limits<double>::infinity();
    auto req=makeSequenceRequest({st}); installCartesianPlanner(req);
    EXPECT_THROW(expandSequence(req),TrajectoryError);
}

/** @brief TRJ-01/02：撤离首构型须延续作业端点，不能在段边界瞬间跳变。 */
TEST(TrjSequence, RejectsDiscontinuousRetractStart_TRJ02)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "TRJ-02"},
                  std::vector<std::string>{"AT-06"});
    auto st = station("A", ObjectId::generate(), {cand({0.5, 0.5}, 0U, 0.5)});
    st.point.retract.enabled = true; st.point.retract.distanceM = 0.1;
    auto req = makeSequenceRequest({st}); installCartesianPlanner(req);
    const auto planner = req.cartesianPlanner;
    req.cartesianPlanner = [planner](const auto& input) {
        auto result = planner(input);
        (*result.startQ)[0] += 0.1; // rad；超过显式连续性阈值 1e-6 rad。
        return result;
    };
    const auto plan = expandSequence(req);
    ASSERT_EQ(plan.failures.size(), 1U);
    EXPECT_EQ(plan.failures.front().reasonToken, std::string(kTrjBranchJump));
    EXPECT_EQ(plan.failures.front().segmentIndex, 2U);
    EXPECT_EQ(plan.failures.front().subject, st.point.pointOid);
    EXPECT_FALSE(plan.canceled);
}
