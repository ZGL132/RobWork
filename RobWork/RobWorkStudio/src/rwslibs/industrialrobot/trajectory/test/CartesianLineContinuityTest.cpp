/**
 * @file   CartesianLineContinuityTest.cpp
 * @brief  笛卡尔直线段与沿途 IK 连续性用例组（TrjCartesianLine/TrjContinuity）
 *         ——TRJ-02"支持笛卡尔直线接近/撤离段，并对沿途 IK 连续性进行检
 *         查"的模型测试（任务契约 WP-16-T05 acceptance 1/2；黄金基准＝解
 *         析算例，对照容差按附录 D 第 9 项声明——标量相对 1×10⁻⁹，经
 *         core::closeWithin/allCloseWithin 逐元素判定；端点还原另以附录 D
 *         C7 运行校验量纲——长度 1×10⁻¹² m、角度 1×10⁻¹² rad——复核）。
 *
 * 设计依据：
 *   - units/trajectory.md §8.2（几何构造/姿态插值口径）、§8.3（采样计划/
 *     IK 端口消费/分支连续性——V-04/V-05/V-06）、§8.4（采样失败——搜索
 *     未果口径）、§8.5（奇异邻域——V-07）、§9.1~§9.3（段边界几何连续）、
 *     §13.3/§8.1 表 4 C2（适用条件双路——V-09）、§17.2（V-04~V-09 行——
 *     本组覆盖其解析算例/替身可行半区；V-06 注明"语义需真实 IK 复核"
 *     的全链真实后端验证归 T13 黄金数据集）
 *   - 需求 TRJ-02/TRJ-06、AT-06（解析算例口径）、NFR-COR-01/02/03
 *   - 先例：PtpSequenceTest（gtest＋IRD_TEST_INFO 需求追溯＋解析黄金）
 *
 * 测试替身纪律（§17.1）：本文件的 IKinematicsComputePort 假实现只证明
 * **端口、状态与错误传播**与解析算例的几何/判定逻辑——不证明真实
 * kinematics IK 后端正确性（该面归 T13 黄金数据集与真实 IK 复核）。
 */

#include <rw/math/RPY.hpp>                  // RPY 姿态构造（测试黄金算例）

#include <sdurws/ird/trajectory/CartesianLine.hpp>
#include <sdurws/ird/trajectory/Continuity.hpp>
#include <sdurws/ird/trajectory/KinematicsPort.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Identity.hpp>
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
using sdurws::ird::core::Tolerance;
using sdurws::ird::core::closeWithin;
using sdurws::ird::trajectory::BoundaryContinuityEntry;
using sdurws::ird::trajectory::CartesianIkContinuityApplicability;
using sdurws::ird::trajectory::CartesianLinePlanResult;
using sdurws::ird::trajectory::CartesianLineRequest;
using sdurws::ird::trajectory::CartesianLineRole;
using sdurws::ird::trajectory::CartesianLineStatus;
using sdurws::ird::trajectory::ContinuityReport;
using sdurws::ird::trajectory::FailedSegmentRecord;
using sdurws::ird::trajectory::FkPortMetrics;
using sdurws::ird::trajectory::FkPortReply;
using sdurws::ird::trajectory::FkPortRequest;
using sdurws::ird::trajectory::GeometricContinuityTolerances;
using sdurws::ird::trajectory::IkContinuityRecord;
using sdurws::ird::trajectory::IkPortFilterReason;
using sdurws::ird::trajectory::IkPortFilterRecord;
using sdurws::ird::trajectory::IkPortOutcome;
using sdurws::ird::trajectory::IkPortReply;
using sdurws::ird::trajectory::IkPortRequest;
using sdurws::ird::trajectory::IkPortResult;
using sdurws::ird::trajectory::IkPortSolution;
using sdurws::ird::trajectory::IKinematicsComputePort;
using sdurws::ird::trajectory::KinPortCallStatus;
using sdurws::ird::trajectory::SegmentConstraint;
using sdurws::ird::trajectory::SegmentSpaceType;
using sdurws::ird::trajectory::SequenceSegmentAxis;
using sdurws::ird::trajectory::TrajectoryError;
using sdurws::ird::trajectory::TrajectorySegment;
using sdurws::ird::trajectory::Waypoint;
using sdurws::ird::trajectory::WaypointKind;
using sdurws::ird::trajectory::kPhasePlanLine;
using sdurws::ird::trajectory::cartesianIkContinuityApplicability;
using sdurws::ird::trajectory::cartesianSamplePlan;
using sdurws::ird::trajectory::checkGeometricContinuity;
using sdurws::ird::trajectory::interpolateCartesianLine;
using sdurws::ird::trajectory::kTrjBranchJump;
using sdurws::ird::trajectory::kTrjContinuityBroken;
using sdurws::ird::trajectory::kTrjNoPath;
using sdurws::ird::trajectory::planCartesianLine;

namespace {

/// 附录 D 第 9 项解析算例对照容差（本文件黄金对照声明——标量相对 1e-9）。
Tolerance goldenTolerance()
{
    return Tolerance::make(1e-9, 0.0);
}

/// 附录 D C7 端点还原量纲（长度 1e-12 m——笛卡尔端点还原断言）。
Tolerance endpointTolM()
{
    return Tolerance::make(0.0, 1e-12);
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

/// 构造基座系位姿（平移 m＋绕基座 Z 轴的偏航角 rad——AGENTS §2.5 单位/
/// 坐标系显式）。★ rw::math::RPY(r,p,y).toRotation3D()＝Rz(r)·Ry(p)·
/// Rx(y)（第一参数绕 Z、第三参数绕 X——与常见 RPY 命名相反，实测口径）；
/// 本辅助显式以"绕 Z 偏航"单角构造，避免欧拉序歧义。
rw::math::Transform3D<double> makePose(double x, double y, double z, double yawAboutZRad)
{
    const rw::math::RPY<double> rpy(yawAboutZRad, 0.0, 0.0);   // = Rz(yawAboutZRad)
    return rw::math::Transform3D<double>(rw::math::Vector3D<double>(x, y, z),
                                         rpy.toRotation3D());
}

/// 提取旋转矩阵的 Rz 等价角（rad）——解析对照用（姿态绕 z 单轴样例：
/// R(0,1)=-sinθ、R(0,0)=cosθ，θ=atan2(-R(0,1), R(0,0))）。
double yawOf(const rw::math::Rotation3D<double>& R)
{
    return std::atan2(-R(0, 1), R(0, 0));
}

/// 单个 IK 端口解便利构造（q＋stableIndex＋裕量——§7.5 三键镜像）。
IkPortSolution sol(std::initializer_list<double> q, std::uint32_t idx, double margin)
{
    IkPortSolution s;
    for (const double v : q) {
        s.q.push_back(v);
    }
    s.stableIndex = idx;
    s.minimumJointMargin = margin;
    return s;
}

/**
 * @brief IK/FK 端口假实现（测试替身——§17.1 纪律：只证明端口/状态/错误
 *        传播与解析算例逻辑；脚本化逐次响应＋请求记录）。
 *
 * 行为：
 *   - solveIk：记录请求 → 按注入状态返回 PortError/Canceled/Ok；Ok 时
 *     从 ikScript 弹出下一响应（脚本耗尽＝测试缺陷，gtest 断言失败）；
 *   - evaluateFk：记录请求 → 按注入状态返回；Ok 时从 fkScript 弹出。
 */
class FakeKinPort final : public IKinematicsComputePort {
public:
    std::vector<IkPortRequest> ikRequests;   ///< solveIk 请求记录（透传钉扎面）
    std::vector<FkPortRequest> fkRequests;   ///< evaluateFk 请求记录
    std::vector<IkPortResult> ikScript;      ///< solveIk 逐次响应脚本
    std::vector<FkPortMetrics> fkScript;     ///< evaluateFk 逐次响应脚本
    KinPortCallStatus ikStatus = KinPortCallStatus::Ok;   ///< solveIk 壳结局注入
    KinPortCallStatus fkStatus = KinPortCallStatus::Ok;   ///< evaluateFk 壳结局注入
    std::string errorToken = "trajectory/kin-port-solve-ik";  ///< PortError token 注入
    std::string errorMessage = "适配器契约不匹配（假实现注入）";  ///< PortError 文案注入
    std::uint32_t attemptedInitialValues = 8U;  ///< 附带响应的已试初值数

    IkPortReply solveIk(const IkPortRequest& request) override
    {
        ikRequests.push_back(request);
        IkPortReply reply;
        if (ikStatus != KinPortCallStatus::Ok) {
            reply.status = ikStatus;
            reply.errorToken = errorToken;
            reply.errorMessage = errorMessage;
            return reply;
        }
        EXPECT_FALSE(ikScript.empty()) << "IK 响应脚本耗尽（测试脚本缺陷）";
        if (ikScript.empty()) {
            reply.status = KinPortCallStatus::PortError;
            reply.errorToken = "trajectory/kin-port-script-exhausted";
            reply.errorMessage = "测试脚本耗尽";
            return reply;
        }
        reply.status = KinPortCallStatus::Ok;
        reply.result = ikScript.front();
        ikScript.erase(ikScript.begin());
        reply.result.attemptedInitialValues = attemptedInitialValues;
        return reply;
    }

    FkPortReply evaluateFk(const FkPortRequest& request) override
    {
        fkRequests.push_back(request);
        FkPortReply reply;
        if (fkStatus != KinPortCallStatus::Ok) {
            reply.status = fkStatus;
            reply.errorToken = "trajectory/kin-port-fk";
            reply.errorMessage = "FK 适配失败（假实现注入）";
            return reply;
        }
        EXPECT_FALSE(fkScript.empty()) << "FK 响应脚本耗尽（测试脚本缺陷）";
        if (fkScript.empty()) {
            reply.status = KinPortCallStatus::PortError;
            reply.errorToken = "trajectory/kin-port-script-exhausted";
            reply.errorMessage = "测试脚本耗尽";
            return reply;
        }
        reply.status = KinPortCallStatus::Ok;
        reply.metrics = fkScript.front();
        fkScript.erase(fkScript.begin());
        return reply;
    }
};

/**
 * @brief 构造黄金算例请求基线（V-04 主算例）：任务点位姿 (0.5,0,0.4) m
 *        ＋绕基座 Z 偏航 30°、ToolZ、approach、段长 0.3 m、采样步长 0.1 m
 *        （→3 段 4 样本）、连续阈值 1e-2 rad、种子构型 (0.1,0.2,0.3) rad；
 *        端口由调用方注入（非空义务——§15.4 前置）。
 */
CartesianLineRequest makeGoldenRequest(FakeKinPort* port)
{
    CartesianLineRequest req;
    req.workPose = makePose(0.5, 0.0, 0.4, 30.0 * 3.14159265358979323846 / 180.0);
    req.axis = SequenceSegmentAxis::ToolZ;
    req.referenceAxisDirectionInBase.reset();
    req.distanceM = 0.3;                            // m
    req.role = CartesianLineRole::Approach;
    req.tcpRef = ObjectId::generate();
    req.sourceTaskPoint = ObjectId::generate();
    req.constraint = SegmentConstraint{};
    req.constraint.cartesianSampleStep = 0.1;       // m（config.trj 投影）
    req.segmentIndex = 3;                           // 非零段号——定位键透传钉扎
    req.ikContinuityThreshold = 1e-2;               // rad（逐轴上界）
    req.conditionNumberWarning.reset();             // 奇异检查缺省不适用
    req.branchSeedQ = makeQ({0.1, 0.2, 0.3});       // rad（构型游标）
    req.lowerBoundQ = makeQ({-2.0, -2.0, -2.0});    // rad（评价区间投影）
    req.upperBoundQ = makeQ({2.0, 2.0, 2.0});       // rad
    req.planningSeed = 20260907U;                   // 无量纲（>0）
    req.kinPort = port;
    return req;
}

/// 填充"同分支连续"IK 脚本：样本 i（0..3）解＝种子＋(0.001·i, 0, 0) rad
/// ——相邻逐轴差 0.001 ≤ 阈值 1e-2（V-06 连续路径）。
void fillContinuousIkScript(FakeKinPort& port)
{
    port.ikScript.clear();
    for (int i = 0; i < 4; ++i) {
        IkPortResult r;
        r.outcome = IkPortOutcome::SolutionsFound;
        r.solutions.push_back(sol({0.1 + 0.001 * i, 0.2, 0.3}, 0U, 0.8));
        port.ikScript.push_back(r);
    }
}

/// 填充 FK 脚本：4 采样全部条件数 10（低于阈值——无 warning 路径）。
void fillBenignFkScript(FakeKinPort& port)
{
    port.fkScript.clear();
    for (int i = 0; i < 4; ++i) {
        FkPortMetrics m;
        m.tcpInBase = makePose(0.0, 0.0, 0.0, 0.0);
        m.singularValues = {1.0, 1.0, 0.1};
        m.conditionNumber = 10.0;                   // 无量纲
        m.conditionNumberIsFinite = true;
        port.fkScript.push_back(m);
    }
}

}  // namespace

// =====================================================================
// 采样计划（§8.3——解析算例）
// =====================================================================

/** 采样计划：L=0.05、step=0.02 → n=ceil(2.5)=3 → 4 样本 {0,1/3,2/3,1}
 *  （端点必含＋等步长——§8.3）；实际相邻弧长 0.05/3 ≤ 0.02 恒成立。 */
TEST(TrjCartesianLine, SamplePlanAnalytic_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{"AT-06"});
    const std::vector<double> s = cartesianSamplePlan(0.05, 0.02);
    ASSERT_EQ(s.size(), 4U);
    EXPECT_DOUBLE_EQ(s[0], 0.0);
    EXPECT_NEAR(s[1], 1.0 / 3.0, 1e-15);
    EXPECT_NEAR(s[2], 2.0 / 3.0, 1e-15);
    EXPECT_DOUBLE_EQ(s[3], 1.0);
    // 步长内边界：n=max(1,ceil(L/step))——L=0.04、step=0.02 恰 2 段 3 样本。
    const std::vector<double> exact = cartesianSamplePlan(0.04, 0.02);
    ASSERT_EQ(exact.size(), 3U);
    EXPECT_DOUBLE_EQ(exact[2], 1.0);
    // 违约面：非正段长/步长 fail-fast（§8.3 采样计划前置）。
    EXPECT_THROW(cartesianSamplePlan(0.0, 0.02), TrajectoryError);
    EXPECT_THROW(cartesianSamplePlan(-1.0, 0.02), TrajectoryError);
    EXPECT_THROW(cartesianSamplePlan(0.05, 0.0), TrajectoryError);
}

// =====================================================================
// 笛卡尔直线插值（§8.2——V-04 端点/V-05 姿态）
// =====================================================================

/** V-04 端点还原：s=0/1 精确回两端（附录 D C7 长度量纲 1e-12 m 内）。 */
TEST(TrjCartesianLine, InterpolateEndpointsRestore_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{"AT-06"});
    const rw::math::Transform3D<double> ta = makePose(0.5, -0.2, 0.1, 0.3);
    const rw::math::Transform3D<double> tb = makePose(0.7, 0.4, 0.9, 0.5);
    const rw::math::Transform3D<double> s0 = interpolateCartesianLine(ta, tb, 0.0);
    const rw::math::Transform3D<double> s1 = interpolateCartesianLine(ta, tb, 1.0);
    const Tolerance endpointTol = endpointTolM();
    for (std::size_t k = 0; k < 3; ++k) {
        // 端点还原（附录 D C7 长度 ε_abs=1e-12 m——零参考退化项不适用，
        // 参考值非零、相对项 0 纯绝对口径）。
        EXPECT_TRUE(closeWithin(s0.P()[k], ta.P()[k], endpointTol));
        EXPECT_TRUE(closeWithin(s1.P()[k], tb.P()[k], endpointTol));
    }
    // 姿态端点：旋转矩阵逐元素一致（端点还原——恒等插值分支）。
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            EXPECT_DOUBLE_EQ(s0.R()(r, c), ta.R()(r, c));
            EXPECT_DOUBLE_EQ(s1.R()(r, c), tb.R()(r, c));
        }
    }
}

/** V-05 姿态最短弧 slerp：与显式四元数对照（NFR-COR-01 解析口径）——
 *  小弧（10°→50° 中点＝30°）与大弧（350°→10° 最短弧经 0°，中点＝0°）。 */
TEST(TrjCartesianLine, InterpolateSlerpShortestArc_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02", "NFR-COR-01"},
                  std::vector<std::string>{"AT-06"});
    const double pi = 3.14159265358979323846;
    const auto deg = [pi](double d) { return d * pi / 180.0; };

    // 小弧样例：Rz(10°)→Rz(50°)，s=0.5 → Rz(30°)（解析黄金值）。
    const rw::math::Transform3D<double> ta = makePose(0.0, 0.0, 0.0, deg(10.0));
    const rw::math::Transform3D<double> tb = makePose(0.0, 0.0, 0.0, deg(50.0));
    const rw::math::Transform3D<double> mid = interpolateCartesianLine(ta, tb, 0.5);
    EXPECT_NEAR(yawOf(mid.R()), deg(30.0), 1e-12);

    // 大弧样例：Rz(350°)→Rz(10°)——点积为负翻转分支，最短弧经 0°：
    // s=0.5 → Rz(0°)（若走长弧会得 180°——分支钉扎）。
    const rw::math::Transform3D<double> ta2 = makePose(0.0, 0.0, 0.0, deg(350.0));
    const rw::math::Transform3D<double> tb2 = makePose(0.0, 0.0, 0.0, deg(10.0));
    const rw::math::Transform3D<double> mid2 = interpolateCartesianLine(ta2, tb2, 0.5);
    EXPECT_NEAR(yawOf(mid2.R()), deg(0.0), 1e-12);

    // s=0.25 解析对照：Rz(350°)→最短弧 20°行程 → Rz(355°)（atan2 值域
    // (−π,π]——355° 等价表示为 −5°，两者同角）。
    const rw::math::Transform3D<double> q25 = interpolateCartesianLine(ta2, tb2, 0.25);
    EXPECT_NEAR(yawOf(q25.R()), deg(-5.0), 1e-12);
    EXPECT_NEAR(yawOf(q25.R()), deg(355.0 - 360.0), 1e-12);

    // 违约面：s 越界 fail-fast（§8.2 定义域）。
    EXPECT_THROW(interpolateCartesianLine(ta, tb, 1.5), TrajectoryError);
    EXPECT_THROW(interpolateCartesianLine(ta, tb, -0.1), TrajectoryError);
}

// =====================================================================
// 笛卡尔段规划——V-04 几何基准＋V-06 连续性（黄金算例）
// =====================================================================

/** V-06＋V-04 合算例：ToolZ approach 0.3 m、4 样本全分支连续 → Ok；
 *  几何（起点回退/pathLengthTcp/路点 kind）、连续性记录逐字段、端口
 *  调用形态（IK 4 次＋不启用奇异时不调 FK）全部断言。 */
TEST(TrjCartesianLine, PlanApproachGoldenContinuous_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{"AT-06"});
    FakeKinPort port;
    fillContinuousIkScript(port);
    CartesianLineRequest req = makeGoldenRequest(&port);

    const CartesianLinePlanResult out = planCartesianLine(req);
    ASSERT_EQ(out.status, CartesianLineStatus::Ok);
    EXPECT_FALSE(out.failure.has_value());
    ASSERT_TRUE(out.continuity.has_value());

    // —— 几何（§8.2 黄金解析值）——
    const TrajectorySegment& seg = out.segment;
    EXPECT_EQ(seg.spaceType, SegmentSpaceType::CartesianLine);
    EXPECT_EQ(seg.segmentIndex, 3U);
    ASSERT_EQ(seg.waypoints.size(), 2U);
    // approach：生成端（Via）在前＝任务点沿 ToolZ（基座系 Z——Rz(30°) 不
    // 改变 z 轴）负向回退 0.3 m：p_start=(0.5,0,0.1)。
    EXPECT_EQ(seg.waypoints[0].kind, WaypointKind::Via);
    ASSERT_TRUE(seg.waypoints[0].target.has_value());
    EXPECT_TRUE(closeWithin(seg.waypoints[0].target->P()[0], 0.5, endpointTolM()));
    EXPECT_TRUE(closeWithin(seg.waypoints[0].target->P()[1], 0.0, endpointTolM()));
    EXPECT_TRUE(closeWithin(seg.waypoints[0].target->P()[2], 0.1, endpointTolM()));
    // 任务点端（TaskPoint）在后＝workPose 精确还原（位置与姿态）。
    EXPECT_EQ(seg.waypoints[1].kind, WaypointKind::TaskPoint);
    ASSERT_TRUE(seg.waypoints[1].target.has_value());
    EXPECT_TRUE(seg.waypoints[1].sourceTaskPoint == req.sourceTaskPoint);
    EXPECT_TRUE(seg.waypoints[1].target->P() == req.workPose.P());
    EXPECT_TRUE(seg.waypoints[1].target->R() == req.workPose.R());
    // TCP 路径长度＝段长（几何定义）；关节路径长度不冒充（恒 0）。
    EXPECT_DOUBLE_EQ(seg.pathLengthTcp, 0.3);
    EXPECT_DOUBLE_EQ(seg.pathLengthJoint, 0.0);
    EXPECT_EQ(seg.tcpRef, req.tcpRef);
    EXPECT_TRUE(seg.sourceTaskPoint == req.sourceTaskPoint);

    // —— 连续性记录（§8.3 证据绑定字段：解索引/逐轴偏差/判定）——
    const IkContinuityRecord& rec = *out.continuity;
    ASSERT_EQ(rec.samples.size(), 4U);
    EXPECT_DOUBLE_EQ(rec.threshold, 1e-2);
    EXPECT_NEAR(rec.sampleStepActualM, 0.1, 1e-15);   // 0.3/3＝步长恰等
    EXPECT_TRUE(rec.allContinuous);
    for (std::size_t i = 0; i < rec.samples.size(); ++i) {
        EXPECT_NEAR(rec.samples[i].s, static_cast<double>(i) / 3.0, 1e-15);
        EXPECT_EQ(rec.samples[i].solutionCount, 1U);
        EXPECT_EQ(rec.samples[i].branchSolutionIndex, 0U);
        EXPECT_TRUE(rec.samples[i].branchContinuous);
        // 逐轴最大偏差解析值：样本 i 与前一分支解差＝0.001 rad（i=0 为 0）。
        EXPECT_NEAR(rec.samples[i].maxAxisDelta, i == 0 ? 0.0 : 0.001, 1e-15);
    }

    // —— 端口调用形态（§8.3/§8.5：4 次 IK；奇异检查未启用→零 FK 调用）——
    ASSERT_EQ(port.ikRequests.size(), 4U);
    EXPECT_TRUE(port.fkRequests.empty());
    // 首采样请求的透传钉扎：目标位姿＝T_start（采样 s=0）、限位/种子透传。
    EXPECT_TRUE(closeWithin(port.ikRequests[0].targetPose.P()[2], 0.1, endpointTolM()));
    // 评价区间投影逐轴透传（限位硬过滤②的判定基准——§8.4）。
    ASSERT_EQ(port.ikRequests[0].lowerBoundQ.size(), 3U);
    ASSERT_EQ(port.ikRequests[0].upperBoundQ.size(), 3U);
    for (std::size_t k = 0; k < 3; ++k) {
        EXPECT_DOUBLE_EQ(port.ikRequests[0].lowerBoundQ[k], -2.0);
        EXPECT_DOUBLE_EQ(port.ikRequests[0].upperBoundQ[k], 2.0);
    }
    EXPECT_EQ(port.ikRequests[0].seed, 20260907U);
    // 末采样目标位姿＝任务点位姿（端点必含——T_end 还原）。
    EXPECT_TRUE(port.ikRequests[3].targetPose.P() == req.workPose.P());
    EXPECT_TRUE(port.ikRequests[3].targetPose.R() == req.workPose.R());
    // 无奇异 warning。
    EXPECT_TRUE(out.singularWarnings.empty());
}

/** ReferenceZ/retract 双向样例：参考轴基座系 X 正向、retract 外推——
 *  方向语义（REQ-02：retract 沿轴正向离开）与路点序（TaskPoint 在前）。 */
TEST(TrjCartesianLine, PlanRetractReferenceAxis_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    CartesianLineRequest req = makeGoldenRequest(&port);
    req.axis = SequenceSegmentAxis::ReferenceZ;
    req.referenceAxisDirectionInBase = rw::math::Vector3D<double>(1.0, 0.0, 0.0);
    req.role = CartesianLineRole::Retract;
    req.distanceM = 0.2;   // m

    const CartesianLinePlanResult out = planCartesianLine(req);
    ASSERT_EQ(out.status, CartesianLineStatus::Ok);
    ASSERT_EQ(out.segment.waypoints.size(), 2U);
    // retract：TaskPoint（任务点位姿）在前、Via（撤离点）在后。
    EXPECT_EQ(out.segment.waypoints[0].kind, WaypointKind::TaskPoint);
    EXPECT_EQ(out.segment.waypoints[1].kind, WaypointKind::Via);
    // 撤离点＝任务点沿基座系 X 正向外推 0.2 m：(0.7, 0, 0.4)。
    ASSERT_TRUE(out.segment.waypoints[1].target.has_value());
    EXPECT_TRUE(closeWithin(out.segment.waypoints[1].target->P()[0], 0.7, endpointTolM()));
    EXPECT_TRUE(closeWithin(out.segment.waypoints[1].target->P()[2], 0.4, endpointTolM()));
    EXPECT_DOUBLE_EQ(out.segment.pathLengthTcp, 0.2);
}

/** V-06 反例——分支跳变：第 2 采样点解集远离当前分支 → BranchJump 终态
 *  ＋TRJ-BRANCH-JUMP 素材（附 s 与两端解摘要）；跳变前记录完整回带。 */
TEST(TrjCartesianLine, PlanDetectsBranchJump_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02", "TRJ-06"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    // 第 2 采样点（i=1）替换为远离解（与种子逐轴差 0.5 rad > 阈值 1e-2）。
    IkPortResult farResult;
    farResult.outcome = IkPortOutcome::SolutionsFound;
    farResult.solutions.push_back(sol({0.6, 0.2, 0.3}, 0U, 0.9));
    port.ikScript[1] = farResult;

    const CartesianLinePlanResult out = planCartesianLine(makeGoldenRequest(&port));
    ASSERT_EQ(out.status, CartesianLineStatus::BranchJump);
    ASSERT_TRUE(out.failure.has_value());
    EXPECT_EQ(out.failure->reasonToken, std::string(kTrjBranchJump));
    EXPECT_EQ(out.failure->phaseToken, std::string(kPhasePlanLine));
    EXPECT_EQ(out.failure->segmentIndex, 3U);
    ASSERT_TRUE(out.failure->pathParameter.has_value());
    EXPECT_NEAR(*out.failure->pathParameter, 1.0 / 3.0, 1e-15);
    // 素材文案含两端解摘要（当前分支解 0.100000…与断裂样本首解 0.600000…）。
    EXPECT_NE(out.failure->cause.find("0.100000"), std::string::npos);
    EXPECT_NE(out.failure->cause.find("0.600000"), std::string::npos);
    EXPECT_FALSE(out.failure->recommendedAction.empty());
    // 无完整段（失败不产段——§15.2 后置）。
    EXPECT_EQ(out.segment.waypoints.size(), 0U);
    // 跳变前记录回带：2 行（i=0 连续、i=1 断裂行）。
    ASSERT_TRUE(out.continuity.has_value());
    ASSERT_EQ(out.continuity->samples.size(), 2U);
    EXPECT_TRUE(out.continuity->samples[0].branchContinuous);
    EXPECT_FALSE(out.continuity->samples[1].branchContinuous);
    EXPECT_FALSE(out.continuity->allContinuous);
}

/** §8.4 采样失败（搜索未果口径）：首采样点全部解被过滤（初值扩充重试后
 *  仍失败）→ SampleUnreachable＋TRJ-NO-PATH 素材（附 s/已试初值数/过滤
 *  原因分布）——不判不可行（不抛异常，素材轨）。 */
TEST(TrjCartesianLine, PlanSampleUnreachableSearchExhausted_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02", "TRJ-06"},
                  std::vector<std::string>{"AT-06"});
    FakeKinPort port;
    // 两次调用（8 初值＋32 扩充）均返回"全部被过滤"——残差 1/限位 1。
    for (int k = 0; k < 2; ++k) {
        IkPortResult r;
        r.outcome = IkPortOutcome::AllCandidatesFiltered;
        r.filtered.push_back(IkPortFilterRecord{{0.0, 0.0, 0.0}, IkPortFilterReason::ResidualRecheck});
        r.filtered.push_back(IkPortFilterRecord{{1.0, 1.0, 1.0}, IkPortFilterReason::JointLimit});
        port.ikScript.push_back(r);
    }
    port.attemptedInitialValues = 40U;  // 8＋32——素材"已试初值数"

    const CartesianLinePlanResult out = planCartesianLine(makeGoldenRequest(&port));
    ASSERT_EQ(out.status, CartesianLineStatus::SampleUnreachable);
    ASSERT_TRUE(out.failure.has_value());
    EXPECT_EQ(out.failure->reasonToken, std::string(kTrjNoPath));
    ASSERT_TRUE(out.failure->pathParameter.has_value());
    EXPECT_DOUBLE_EQ(*out.failure->pathParameter, 0.0);   // 首采样 s=0
    EXPECT_NE(out.failure->cause.find("40"), std::string::npos);   // 已试初值数
    EXPECT_NE(out.failure->cause.find("残差"), std::string::npos);
    EXPECT_NE(out.failure->cause.find("限位"), std::string::npos);
    EXPECT_NE(out.failure->cause.find("不构成任务不可行"), std::string::npos);
    // 端口确实重试了一次（2 次调用——§8.4 初值策略扩充）。
    ASSERT_EQ(port.ikRequests.size(), 2U);
    EXPECT_EQ(port.ikRequests[0].maxInitialValues, 8U);
    EXPECT_EQ(port.ikRequests[1].maxInitialValues, 32U);
}

/** §8.4 中途失败：第 3 采样点无解 → 素材定位 s=2/3（段内可定位）。 */
TEST(TrjCartesianLine, PlanMidSampleUnreachableLocatedByS_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    // 第 3 采样点（i=2）两次响应均替换为"全部初值未收敛"（结局 2——
    // 首次 8 初值＋扩充 32 初值；i=3 不再消费）。
    IkPortResult noConv;
    noConv.outcome = IkPortOutcome::MultiInitNoConvergence;
    port.ikScript[2] = noConv;
    port.ikScript[3] = noConv;   // i=2 的扩充重试响应
    port.attemptedInitialValues = 32U;

    const CartesianLinePlanResult out = planCartesianLine(makeGoldenRequest(&port));
    ASSERT_EQ(out.status, CartesianLineStatus::SampleUnreachable);
    ASSERT_TRUE(out.failure.has_value());
    ASSERT_TRUE(out.failure->pathParameter.has_value());
    EXPECT_NEAR(*out.failure->pathParameter, 2.0 / 3.0, 1e-15);
    // 调用序：i=0、i=1 各 1 次（连续）＋i=2 首次＋扩充重试 1 次＝4 次。
    ASSERT_EQ(port.ikRequests.size(), 4U);
    EXPECT_EQ(port.ikRequests[2].maxInitialValues, 8U);
    EXPECT_EQ(port.ikRequests[3].maxInitialValues, 32U);
}

/** §8.5 奇异邻域（V-07）：阈值启用＋条件数超限 → warning 素材逐采样记录
 *  （附 s/条件数/阈值/比较型）且不阻断（status Ok）。 */
TEST(TrjCartesianLine, PlanSingularNeighborhoodWarnsNotBlocks_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02", "TRJ-06"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    fillBenignFkScript(port);
    for (std::size_t i = 0; i < port.fkScript.size(); ++i) {
        port.fkScript[i].conditionNumber = 100.0;   // > 阈值 50（无量纲 1）
    }
    CartesianLineRequest req = makeGoldenRequest(&port);
    req.conditionNumberWarning = 50.0;              // policy 投影（单位 1）

    const CartesianLinePlanResult out = planCartesianLine(req);
    // warning 不阻断——Ok＋连续记录完整（§8.5 原文）。
    ASSERT_EQ(out.status, CartesianLineStatus::Ok);
    EXPECT_TRUE(out.continuity->allContinuous);
    ASSERT_EQ(out.singularWarnings.size(), 4U);
    for (std::size_t i = 0; i < out.singularWarnings.size(); ++i) {
        EXPECT_NEAR(out.singularWarnings[i].s, static_cast<double>(i) / 3.0, 1e-15);
        EXPECT_DOUBLE_EQ(out.singularWarnings[i].conditionNumber, 100.0);
        EXPECT_DOUBLE_EQ(out.singularWarnings[i].threshold, 50.0);
        EXPECT_TRUE(out.singularWarnings[i].conditionNumberIsFinite);
        EXPECT_FALSE(out.singularWarnings[i].cause.empty());
        ASSERT_TRUE(out.singularWarnings[i].comparison.has_value());
        // 比较型三要素（实际 100/阈值 50/单位 1——码表 requiresComparison）。
        EXPECT_DOUBLE_EQ(out.singularWarnings[i].comparison->actual.quantity.value(), 100.0);
        EXPECT_DOUBLE_EQ(out.singularWarnings[i].comparison->expected.quantity.value(), 50.0);
    }
    // FK 端口逐采样调用（每样本 1 次——§8.3 FK 消费以奇异检查为目的）。
    ASSERT_EQ(port.fkRequests.size(), 4U);
    // FK 请求的 q＝当前分支解（透传钉扎——首样本＝种子延续解 0.1）。
    EXPECT_DOUBLE_EQ(port.fkRequests[0].q[0], 0.1);
}

/** §8.5 NotApplicable 路：policy 未启用（nullopt）→ 零 FK 调用、零
 *  warning（检查显式不适用——P-POL-2，不计缺失）。 */
TEST(TrjCartesianLine, PlanSingularCheckNotApplicableWithoutThreshold_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    // fkScript 故意留空——nullopt 下不应有任何 FK 调用（脚本耗尽断言
    // 会把违规调用暴露为失败）。
    CartesianLineRequest req = makeGoldenRequest(&port);
    req.conditionNumberWarning.reset();

    const CartesianLinePlanResult out = planCartesianLine(req);
    EXPECT_EQ(out.status, CartesianLineStatus::Ok);
    EXPECT_TRUE(out.singularWarnings.empty());
    EXPECT_TRUE(port.fkRequests.empty());
}

/** §8.5 有限性路：条件数 +∞（conditionNumberIsFinite=false）→ 命中
 *  （不静默截断——D-KIN-2 透传）。 */
TEST(TrjCartesianLine, PlanSingularInfiniteConditionNumberHits_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    fillBenignFkScript(port);
    port.fkScript[0].conditionNumber = std::numeric_limits<double>::infinity();
    port.fkScript[0].conditionNumberIsFinite = false;

    CartesianLineRequest req = makeGoldenRequest(&port);
    req.conditionNumberWarning = 50.0;
    const CartesianLinePlanResult out = planCartesianLine(req);
    EXPECT_EQ(out.status, CartesianLineStatus::Ok);
    ASSERT_EQ(out.singularWarnings.size(), 1U);
    EXPECT_FALSE(out.singularWarnings[0].conditionNumberIsFinite);
    EXPECT_TRUE(out.singularWarnings[0].comparison.has_value());
}

/** §8.3 首采样分支延续：种子近解与远解并存 → 首采样按 PTP 构型选择规则
 *  选延续解（choosePtpCandidate 延续性优先——§8.3 原文）。 */
TEST(TrjCartesianLine, PlanFirstSampleBranchContinuesFromSeed_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    // 首采样点追加远解（裕量更大）——延续解（差 0.001 ≤ 1e-2）应胜出。
    port.ikScript[0].solutions.push_back(sol({5.0, 5.0, 5.0}, 1U, 0.95));
    // 其后样本与延续解链连续（0.1+0.001·i 距种子 0.001·i ≤ 阈值）。

    const CartesianLinePlanResult out = planCartesianLine(makeGoldenRequest(&port));
    ASSERT_EQ(out.status, CartesianLineStatus::Ok);
    EXPECT_EQ(out.continuity->samples[0].solutionCount, 2U);
    EXPECT_EQ(out.continuity->samples[0].branchSolutionIndex, 0U);  // 延续解胜出
    EXPECT_TRUE(out.continuity->allContinuous);
}

/** 取消路：入口命中 → Canceled 零素材（取消不是错误——UX-03）；端口侧
 *  取消传播同样以 Canceled 收尾。 */
TEST(TrjCartesianLine, PlanCancelYieldsZeroMaterial_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-01"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    CartesianLineRequest req = makeGoldenRequest(&port);
    req.cancel = [] { return true; };
    const CartesianLinePlanResult out = planCartesianLine(req);
    EXPECT_EQ(out.status, CartesianLineStatus::Canceled);
    EXPECT_FALSE(out.failure.has_value());
    EXPECT_FALSE(out.continuity.has_value());
}

/** 端口层错误传播：PortError → TrajectoryError（token 恒
 *  trajectory/kin-port-* 前缀——§15.4 显性失败不吞错）。 */
TEST(TrjCartesianLine, PlanPortErrorSurfacesTrajectoryError_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    FakeKinPort port;
    port.ikStatus = KinPortCallStatus::PortError;
    CartesianLineRequest req = makeGoldenRequest(&port);
    try {
        (void) planCartesianLine(req);
        FAIL() << "端口层错误应显性抛出（§15.4）";
    } catch (const TrajectoryError& e) {
        EXPECT_EQ(e.token().rfind("trajectory/kin-port-", 0), 0U);
    }
}

/** 确定性（NFR-COR-02）：同输入两次规划 → 端口请求序列逐字段一致＋
 *  结果等价（§15.4 合法示例"同种子同配置→等价解序"的消费面呈现）。 */
TEST(TrjCartesianLine, PlanDeterministicReplays_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});
    FakeKinPort portA;
    fillContinuousIkScript(portA);
    fillBenignFkScript(portA);
    CartesianLineRequest reqA = makeGoldenRequest(&portA);
    reqA.conditionNumberWarning = 50.0;
    const CartesianLinePlanResult a = planCartesianLine(reqA);

    FakeKinPort portB;
    fillContinuousIkScript(portB);
    fillBenignFkScript(portB);
    // reqB 从 reqA 拷贝（身份字段含 tcpRef 保持同一——确定性对照要求
    // 全字段一致，仅端口指向不同实例）。
    CartesianLineRequest reqB = reqA;
    reqB.kinPort = &portB;
    const CartesianLinePlanResult b = planCartesianLine(reqB);

    ASSERT_EQ(portA.ikRequests.size(), portB.ikRequests.size());
    for (std::size_t i = 0; i < portA.ikRequests.size(); ++i) {
        EXPECT_TRUE(portA.ikRequests[i].targetPose.P() == portB.ikRequests[i].targetPose.P());
        EXPECT_TRUE(portA.ikRequests[i].targetPose.R() == portB.ikRequests[i].targetPose.R());
        EXPECT_EQ(portA.ikRequests[i].seed, portB.ikRequests[i].seed);
        EXPECT_EQ(portA.ikRequests[i].maxInitialValues, portB.ikRequests[i].maxInitialValues);
    }
    EXPECT_EQ(a.status, b.status);
    EXPECT_TRUE(a.segment == b.segment);
    ASSERT_TRUE(a.continuity.has_value() && b.continuity.has_value());
    EXPECT_TRUE(a.continuity->samples.size() == b.continuity->samples.size());
}

/** §15.2 非法示例组（fail-fast 面）：distanceM≤0／ReferenceZ 方向缺失/
 *  非单位/ToolZ 双源/空端口/零种子/非法阈值/空种子链/tcpRef 无效。 */
TEST(TrjCartesianLine, PlanRejectsContractViolations_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);

    // distanceM≤0（§15.2 非法示例原文）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.distanceM = 0.0;
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
        req.distanceM = -0.5;
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // ReferenceZ 方向解析产物缺失（§15.2"axis 对象未在闭包"的请求面）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.axis = SequenceSegmentAxis::ReferenceZ;
        req.referenceAxisDirectionInBase.reset();
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // ReferenceZ 方向非单位向量（|v|=2——拒绝不归一化）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.axis = SequenceSegmentAxis::ReferenceZ;
        req.referenceAxisDirectionInBase = rw::math::Vector3D<double>(2.0, 0.0, 0.0);
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // ToolZ 双源违约（域内解析面不得另携注入向量）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.referenceAxisDirectionInBase = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // 端口缺失（§15.4 前置——装配失败不运行）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.kinPort = nullptr;
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // 零种子（I-KIN-4 同款——确定性要素不缺省替换）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.planningSeed = 0U;
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // 连续阈值非正。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.ikContinuityThreshold = 0.0;
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // 奇异阈值非正（有值时合法域）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.conditionNumberWarning = -1.0;
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // 空种子链（维度 0）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.branchSeedQ = rw::math::Q(0);
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
    // tcpRef 无效（ARC-04 对象 ID 义务）。
    {
        CartesianLineRequest req = makeGoldenRequest(&port);
        req.tcpRef = ObjectId{};
        EXPECT_THROW(planCartesianLine(req), TrajectoryError);
    }
}

// =====================================================================
// C2 适用性双路（§8.1 表 4/§13.3——V-09）
// =====================================================================

/** V-09 双路：含笛卡尔段→适用；纯关节路径→NotApplicable（原因=无笛卡尔
 *  段，不计缺失）；空序列→不适用。 */
TEST(TrjContinuity, CartesianIkContinuityApplicabilityDualPath_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{"AT-06"});

    // 路 1：纯关节路径（两段 JointLinear）→ 不适用＋原因非空（§9.2 行 6）。
    TrajectorySegment joint1;
    joint1.segmentIndex = 0;
    joint1.spaceType = SegmentSpaceType::JointLinear;
    TrajectorySegment joint2 = joint1;
    joint2.segmentIndex = 1;
    const CartesianIkContinuityApplicability pureJoint =
        cartesianIkContinuityApplicability({joint1, joint2});
    EXPECT_FALSE(pureJoint.applicable);
    EXPECT_NE(pureJoint.notApplicableReason.find("笛卡尔"), std::string::npos);

    // 路 2：含笛卡尔段（approach 段——黄金算例产物）→ 适用。
    FakeKinPort port;
    fillContinuousIkScript(port);
    const CartesianLinePlanResult cart = planCartesianLine(makeGoldenRequest(&port));
    ASSERT_EQ(cart.status, CartesianLineStatus::Ok);
    const CartesianIkContinuityApplicability withCart =
        cartesianIkContinuityApplicability({joint1, cart.segment});
    EXPECT_TRUE(withCart.applicable);
    EXPECT_TRUE(withCart.notApplicableReason.empty());

    // 空序列 → 不适用（无段即无笛卡尔段）。
    EXPECT_FALSE(cartesianIkContinuityApplicability({}).applicable);
}

// =====================================================================
// 段边界几何连续（§9.1/§9.2——守卫面）
// =====================================================================

/** 合法连接（§9.2 行 1）：approach 段终点＝retract 段起点（同为任务点
 *  位姿）→ 位置/姿态两维 Ok、零违规。 */
TEST(TrjContinuity, BoundaryContinuousApproachToRetract_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    // 段号 0 基连续（§15.3 前置）——两段请求段号 0/1。
    CartesianLineRequest approachReq = makeGoldenRequest(&port);
    approachReq.segmentIndex = 0;
    const CartesianLinePlanResult approach = planCartesianLine(approachReq);
    ASSERT_EQ(approach.status, CartesianLineStatus::Ok);

    CartesianLineRequest retractReq = makeGoldenRequest(&port);
    retractReq.role = CartesianLineRole::Retract;
    retractReq.segmentIndex = 1;
    fillContinuousIkScript(port);
    const CartesianLinePlanResult retract = planCartesianLine(retractReq);
    ASSERT_EQ(retract.status, CartesianLineStatus::Ok);

    const ContinuityReport report =
        checkGeometricContinuity({approach.segment, retract.segment},
                                 GeometricContinuityTolerances{});
    ASSERT_EQ(report.boundaries.size(), 1U);
    EXPECT_TRUE(report.boundaries[0].cartesianApplicable);
    ASSERT_TRUE(report.boundaries[0].positionOk.has_value());
    EXPECT_TRUE(*report.boundaries[0].positionOk);
    EXPECT_TRUE(*report.boundaries[0].orientationOk);
    EXPECT_NEAR(*report.boundaries[0].positionDeltaM, 0.0, 1e-15);
    EXPECT_TRUE(report.violations.empty());
}

/** 非法连接（§9.2 行 2）：后段起点姿态错开 1e-6 rad（> 容差 1e-12）→
 *  TRJ-CONTINUITY-BROKEN 素材（比较型实际差/容差/单位 rad）；位置维
 *  单独错开同样逐维度产出。 */
TEST(TrjContinuity, BoundaryBrokenYieldsComparativeMaterial_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06"}, std::vector<std::string>{});
    // 手工构造两段：段 0 终点位姿 P0、段 1 起点位姿 P0＋姿态偏 1e-6 rad
    // （TaskPoint 路点补来源义务——§6.3 可追溯性）。
    const rw::math::Transform3D<double> pose = makePose(0.5, 0.0, 0.4, 0.3);
    const ObjectId sourceOid = ObjectId::generate();
    TrajectorySegment seg0;
    seg0.segmentIndex = 0;
    seg0.spaceType = SegmentSpaceType::CartesianLine;
    Waypoint w0;
    w0.kind = WaypointKind::Via;
    w0.segmentIndex = 0;
    w0.target = makePose(0.5, 0.0, 0.1, 0.3);
    Waypoint w1;
    w1.kind = WaypointKind::TaskPoint;
    w1.segmentIndex = 0;
    w1.target = pose;
    w1.sourceTaskPoint = sourceOid;
    seg0.waypoints = {w0, w1};

    TrajectorySegment seg1;
    seg1.segmentIndex = 1;
    seg1.spaceType = SegmentSpaceType::CartesianLine;
    Waypoint w2;
    w2.kind = WaypointKind::TaskPoint;
    w2.segmentIndex = 1;
    w2.target = makePose(0.5, 0.0, 0.4, 0.3 + 1e-6);   // 姿态错开
    w2.sourceTaskPoint = sourceOid;
    Waypoint w3;
    w3.kind = WaypointKind::Via;
    w3.segmentIndex = 1;
    w3.target = makePose(0.5, 0.0, 0.7, 0.3 + 1e-6);
    seg1.waypoints = {w2, w3};

    const ContinuityReport report =
        checkGeometricContinuity({seg0, seg1}, GeometricContinuityTolerances{});
    ASSERT_EQ(report.boundaries.size(), 1U);
    EXPECT_TRUE(report.boundaries[0].cartesianApplicable);
    EXPECT_TRUE(*report.boundaries[0].positionOk);      // 位置精确相等
    EXPECT_FALSE(*report.boundaries[0].orientationOk);  // 姿态超差
    ASSERT_EQ(report.violations.size(), 1U);
    EXPECT_EQ(report.violations[0].reasonToken, std::string(kTrjContinuityBroken));
    ASSERT_TRUE(report.violations[0].comparison.has_value());
    EXPECT_NEAR(report.violations[0].comparison->actual.quantity.value(), 1e-6, 1e-12);
    EXPECT_NEAR(report.violations[0].comparison->expected.quantity.value(), 1e-12, 1e-15);
}

/** 纯关节边界：前段末路点只携 q → 笛卡尔维度 NotApplicable（无判定值、
 *  无违规——§9.2 行 6 同口径；本检查不消费 FK）。 */
TEST(TrjContinuity, JointBoundaryNotApplicable_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    TrajectorySegment jointSeg;
    jointSeg.segmentIndex = 0;
    jointSeg.spaceType = SegmentSpaceType::JointLinear;
    Waypoint jq0;
    jq0.kind = WaypointKind::Start;
    jq0.segmentIndex = 0;
    jq0.q = makeQ({0.0, 0.0, 0.0});
    Waypoint jq1;
    jq1.kind = WaypointKind::TaskPoint;
    jq1.segmentIndex = 0;
    jq1.q = makeQ({0.1, 0.2, 0.3});
    jq1.sourceTaskPoint = ObjectId::generate();   // §6.3 TaskPoint 来源义务
    jointSeg.waypoints = {jq0, jq1};
    jointSeg.tcpRef = ObjectId::generate();

    FakeKinPort port;
    fillContinuousIkScript(port);
    // 笛卡尔段号取 1（与纯关节段 0 构成连续序——§15.3 前置）。
    CartesianLineRequest cartReq = makeGoldenRequest(&port);
    cartReq.segmentIndex = 1;
    const CartesianLinePlanResult cart = planCartesianLine(cartReq);
    ASSERT_EQ(cart.status, CartesianLineStatus::Ok);

    const ContinuityReport report =
        checkGeometricContinuity({jointSeg, cart.segment},
                                 GeometricContinuityTolerances{});
    ASSERT_EQ(report.boundaries.size(), 1U);
    EXPECT_FALSE(report.boundaries[0].cartesianApplicable);
    EXPECT_FALSE(report.boundaries[0].positionOk.has_value());
    EXPECT_FALSE(report.boundaries[0].orientationOk.has_value());
    EXPECT_TRUE(report.violations.empty());
}

/** 前置违约面：段序号不连续／容差非法 → fail-fast（§15.3 前置原文）。 */
TEST(TrjContinuity, CheckRejectsContractViolations_WP16T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    FakeKinPort port;
    fillContinuousIkScript(port);
    const CartesianLinePlanResult cart = planCartesianLine(makeGoldenRequest(&port));
    ASSERT_EQ(cart.status, CartesianLineStatus::Ok);

    // 段序号不连续（段号 3 的段被放在位 0——定位键违约）。
    EXPECT_THROW(checkGeometricContinuity({cart.segment},
                                          GeometricContinuityTolerances{}),
                 TrajectoryError);

    // 单段无边界——空报告（非错误）。路点 segmentIndex 与段同步改写
    // （§6.2 路点-段定位键一致性——validate 守卫面）。
    TrajectorySegment single = cart.segment;
    single.segmentIndex = 0;
    for (Waypoint& wp : single.waypoints) {
        wp.segmentIndex = 0;
    }
    const ContinuityReport empty =
        checkGeometricContinuity({single}, GeometricContinuityTolerances{});
    EXPECT_TRUE(empty.boundaries.empty());
    EXPECT_TRUE(empty.violations.empty());

    // 容差非法（非正——fail-fast；用两个段号合法的段隔离前置序）。
    TrajectorySegment second = single;
    second.segmentIndex = 1;
    for (Waypoint& wp : second.waypoints) {
        wp.segmentIndex = 1;
    }
    GeometricContinuityTolerances bad;
    bad.positionAbsM = 0.0;
    EXPECT_THROW(checkGeometricContinuity({single, second}, bad), TrajectoryError);
    bad.positionAbsM = 1e-12;
    bad.orientationAbsRad = std::numeric_limits<double>::infinity();
    EXPECT_THROW(checkGeometricContinuity({single, second}, bad), TrajectoryError);
}
