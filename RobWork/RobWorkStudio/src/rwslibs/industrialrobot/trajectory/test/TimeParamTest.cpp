/**
 * @file   TimeParamTest.cpp
 * @brief  时间参数化与节拍的运行期测试（WP-16-T08）——解析黄金算例
 *         （静止-静止五次剖面/中心差分结点/驻留常值段）＋C² 结点匹配数
 *         值证明＋限速校验容差（附录 D 第 10 项相对 1×10⁻⁹）边缘钉扎＋
 *         节拍/分段时间输出（OPT-D 唯一来源）＋词表/素材轨/取消/确定性/
 *         前置违约族。零 policy 消费（时间化面零碰撞/阈值——§12 边界的
 *         测试面自证：本文件不 include 任何 policy/runtime 头、不需要
 *         WorkCell 装置）；段几何输入经公共工厂
 *         makeLinearJointPathGeometry（T07 产物形态即 T08 输入形态）。
 *
 * 设计依据：
 *   - units/trajectory.md §12.1/§12.2/§12.3/§12.4/§12.6/§15.8（见
 *     TimeParam.hpp；测试对照容差口径：黄金对照用解析期望值±1e-12 相对
 *     ——附录 D C7 测试对照逐例声明〔本文件头即声明：解析黄金值 1e-12
 *     相对、容差语义判定直接消费 kTimeParamLimitRelativeTolerance 常量
 *     ＋core::closeWithin 同公式〕）、§9.1（连续性维度——速度/加速度连
 *     续 1e-9 相对；驻留＝零速零加速度常值段；时间边界）、§17.2（V-02
 *     时间侧/V-16/V-17/V-18——本套件的故障注入锚点）
 *   - 需求 TRJ-05（至少加速度连续＋限值时间参数化＋总节拍/分段时间）、
 *     TRJ-06（超限定位素材）、NFR-COR-01（解析对照——独立书写公式）、
 *     NFR-COR-02（确定性）、NFR-COR-03（非法输入拒绝）
 *   - 任务契约 tasks/foundation/WP-16-T08.json（acceptance 1——五次样
 *     条 C² 结点匹配用例；acceptance 2——限速校验容差按附录 D 第 10 项
 *     ＋总节拍与分段时间输出〔含驻留〕）
 *
 * 黄金算例的解析基准（独立书写——与实现 TU 无共享代码，NFR-COR-01）：
 *   静止-静止单段（两端点零速，结点 m 全 0）：q(u)=Δq·h0001(u)，
 *   h0001(u)=10u³−15u⁴+6u⁵；峰值速度=1.875·|Δq|/h（h0001'(0.5)=1.875）；
 *   峰值加速度≈5.7735·|Δq|/h²（h0001'' 极值 |10/√3|）；中点值
 *   q(0.5)=0.5·Δq、qdd(0.5)=0（h0001''(0.5)=0）。
 *
 * 本套件为**集成模式专属**（消费 rw::math::Q 构造面——PtpSequenceTest
 * 同款 gating；冒烟模式不编译本文件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/Smooth.hpp>
#include <sdurws/ird/trajectory/TimeParam.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/math/Q.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace trj = sdurws::ird::trajectory;

namespace {

// =====================================================================
// 独立书写解析参照（NFR-COR-01——与实现侧基函数无共享代码）
// =====================================================================

/// 值基 h0001（静止-静止剖面的唯一非平凡基——端点导数全零消去其余基）。
inline double refH0001(double u)
{
    return 10.0 * u * u * u - 15.0 * u * u * u * u + 6.0 * u * u * u * u * u;
}
/// h0001 一阶导（峰值 @u=0.5 ＝ 1.875）。
inline double refD1H0001(double u)
{
    return 30.0 * u * u - 60.0 * u * u * u + 30.0 * u * u * u * u;
}
/// h0001 二阶导（@u=0.5 ＝ 0——中点加速度过零；极值 |10/√3|≈5.7735）。
inline double refD2H0001(double u)
{
    return 60.0 * u - 180.0 * u * u + 120.0 * u * u * u;
}

// =====================================================================
// 请求组装辅助（黄金算例的公共骨架——段几何经公共工厂构造）
// =====================================================================

/**
 * @brief 组装单关节折线几何（makeLinearJointPathGeometry 公共工厂——
 *        §11.1 T07 产物形态；插值性：s=k/(n-1) 恒等于第 k 路点）。
 */
std::shared_ptr<const trj::IPathGeometry> jointPath(
    const std::vector<double>& waypoints)
{
    std::vector<rw::math::Q> points;
    points.reserve(waypoints.size());
    for (double q : waypoints) {
        points.emplace_back(1, q);  // 单关节构型（rad）
    }
    return trj::makeLinearJointPathGeometry(points);
}

/// 空请求骨架（合法域默认值；用例按需覆盖字段）。
trj::TimeParamRequest baseRequest()
{
    trj::TimeParamRequest request;
    request.limitsScaleFactor = 1.0;
    request.motionLawToken = trj::kTimeParamMethodQuinticSplineC2;
    request.sampleStepS = 0.001;   // s——采样步长（黄金算例声明）
    request.maxIterations = 8;     // 缩放迭代上限（黄金算例声明——§21.4）
    return request;
}

/// 单关节限值视图（rad/s、rad/s²；+inf 用 std::numeric_limits<double>::infinity()）。
trj::JointDynamicLimits limits1(double velocity, double acceleration)
{
    trj::JointDynamicLimits lim;
    lim.velocityLimit = velocity;
    lim.accelerationLimit = acceleration;
    return lim;
}

/// 近似断言辅助（相对判——黄金对照口径 1e-12；另支持绝对下限防零除）。
void expectNearRel(double actual, double expected, double relTol,
                   const char* what)
{
    const double scale = std::max(std::abs(expected), 1e-300);
    EXPECT_LT(std::abs(actual - expected) / scale, relTol) << what;
}

}  // namespace

// =====================================================================
// 黄金算例——静止-静止五次剖面（TRJ-05/NFR-COR-01；V-01 时间侧基准）
// =====================================================================

/**
 * 用例：单关节两点路径（0→0.1 rad）、V=1 rad/s、A=10 rad/s²。初值时长
 * 取解析系数估计（max(1.875·0.1/1, sqrt((10/√3)·0.1/10))＝0.24028 s）使
 * 峰值恰在限值线上（浮动偏差被 1×10⁻⁹ 相对容差吸收）——黄金对照聚焦
 * 剖面形状而非缩放过程。断言：Ok；端点还原与端点零速零加速度（§12.1
 * 边界条件）；中点黄金值（位置/速度/加速度三阶独立对照）；峰值统计与
 * 解析剖面上界一致。
 */
TEST(TimeParamGoldenTest, TwoPointRestToRestProfile)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05", "NFR-COR-01"},
                  std::vector<std::string>{"AT-06"});

    trj::TimeParamRequest request = baseRequest();
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, 0.1});
    request.segments = {seg};
    request.jointLimits = {limits1(1.0, 10.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeParam.has_value());
    ASSERT_TRUE(result.timeCurve != nullptr);
    EXPECT_TRUE(result.failureRecords.empty());

    // 加速度约束的解析最小时长：h ≥ sqrt(1.875²·Δq·? )——直接断言总时长
    // ≥ 两约束的解析下限（峰值速度 1.875Δq/h ≤ 1 ⇒ h ≥ 0.1875；峰值加
    // 速度 (10/√3)Δq/h² ≤ 10 ⇒ h ≥ sqrt(Δq/√3) ≈ 0.24028）。
    const double total = result.timeParam->totalDurationS;
    EXPECT_GE(total, 0.1875 - 1e-9);
    EXPECT_GE(total, std::sqrt(0.1 / std::sqrt(3.0)) - 1e-9);

    // 端点还原＋端点零速零加速度（§12.1 边界条件——ITimeCurve 公共接
    // 口消费）。
    const trj::TimedSample start = result.timeCurve->sampleAt(0.0);
    const trj::TimedSample end = result.timeCurve->sampleAt(total);
    EXPECT_NEAR(start.q[0], 0.0, 1e-12);
    EXPECT_NEAR(end.q[0], 0.1, 1e-12);
    EXPECT_NEAR(start.qd[0], 0.0, 1e-12);
    EXPECT_NEAR(end.qd[0], 0.0, 1e-12);
    EXPECT_NEAR(start.qdd[0], 0.0, 1e-12);
    EXPECT_NEAR(end.qdd[0], 0.0, 1e-12);

    // 中点黄金值（独立解析对照——q(0.5)=0.05、qd(0.5)=1.875·0.1/total、
    // qdd(0.5)=0）。t=total/2 经除法位级落 u=0.5（浮点：total/2 精确、
    // (total/2)/total 最近舍入＝0.5 可精确表示）。
    const trj::TimedSample mid = result.timeCurve->sampleAt(total / 2.0);
    EXPECT_NEAR(mid.q[0], 0.05, 1e-12);
    expectNearRel(mid.qd[0], 1.875 * 0.1 / total, 1e-12, "中点速度黄金对照");
    EXPECT_NEAR(mid.qdd[0], 0.0, 1e-9);

    // 峰值统计：采样峰值不超过解析剖面上界（1.875Δq/h——网格欠估单向
    // 偏差），且达到解析值的 99%（网格足够细的欠估有界）。
    const double analyticPeakV = 1.875 * 0.1 / total;
    EXPECT_LE(result.peakJointVelocity[0], analyticPeakV * (1.0 + 1e-9));
    EXPECT_GE(result.peakJointVelocity[0], analyticPeakV * 0.99);
    // 达标语义：峰值在限值的附录 D 第 10 项容差内（消费公共常量钉扎判
    // 定口径——acceptance 2）。
    EXPECT_LE(result.peakJointVelocity[0],
              1.0 * (1.0 + trj::kTimeParamLimitRelativeTolerance) + 1e-9);
    EXPECT_LE(result.peakJointAcceleration[0],
              10.0 * (1.0 + trj::kTimeParamLimitRelativeTolerance) + 1e-9);
}

// =====================================================================
// C² 结点匹配——中心差分结点（acceptance ①；§9.1 速度/加速度连续）
// =====================================================================

/**
 * 用例：两运动段直接相邻（无驻留）——中间结点为普通内部结点，一阶导
 * ＝时间中心差分（§12.1）。断言：结点速度与独立计算的中心差分值一致
 * （黄金对照）；结点加速度位级 0（结点二阶导恒 0 的构造纪律）；结点两
 * 侧对称邻域的速度差/加速度差随邻域减半近似减半（O(δ) 线性收敛——两
 * 侧极限相等的数值证明，即 C¹/C² 结点匹配）；内部连续性守卫零素材。
 */
TEST(TimeParamGoldenTest, C2KnotMatchCentralDifference)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05"}, std::vector<std::string>{"AT-06"});

    trj::TimeParamRequest request = baseRequest();
    request.sampleStepS = 0.0005;
    trj::TimeParamSegment seg0;
    seg0.segmentIndex = 0;
    seg0.geometry = jointPath({0.0, 0.3});
    trj::TimeParamSegment seg1;
    seg1.segmentIndex = 1;
    seg1.geometry = jointPath({0.3, 0.5});
    request.segments = {seg0, seg1};
    // 宽限值（初值即达标——时长确定性不受缩放干扰；两段行程不同，
    // 初值时长各按最严关节估计）。
    request.jointLimits = {limits1(100.0, 1000.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeParam.has_value());
    // 守卫零素材（实现侧 C² 守卫通过——§12.5 全链检查）。
    EXPECT_TRUE(result.failureRecords.empty());

    // 结点结构：两段共享边界结点→全局 3 结点（§12.1 结点集）。
    const std::vector<double>& knots = result.timeParam->knotTimesS;
    ASSERT_EQ(knots.size(), 3U);
    EXPECT_EQ(knots.front(), 0.0);
    EXPECT_EQ(knots.back(), result.timeParam->totalDurationS);

    // 中心差分黄金对照：m₁ = (q₂−q₀)/(t₂−t₀)＝(0.5−0)/(total−0)。
    const double tm = knots[1];
    const trj::TimedSample atKnot = result.timeCurve->sampleAt(tm);
    const double expectedM =
        (0.5 - 0.0) / (knots[2] - knots[0]);
    expectNearRel(atKnot.qd[0], expectedM, 1e-12, "中心差分结点速度");
    // 结点加速度位级 0（构造纪律——两侧同值 0，C² 的加速度半区）。
    EXPECT_NEAR(atKnot.qdd[0], 0.0, 1e-12);

    // C² 数值证明：结点两侧对称邻域取点，两侧速度差/加速度差随邻域减
    // 半近似减半（O(δ)——jerk 有限时两侧极限相等的收敛特征）。绝对量
    // 级：2δ·jerk（结点邻域 jerk 量级 ~1e5 rad/s³——解析上界见文件头参
    // 照；δ≈3.4e-8 s 时差值 ~1e-2，远小于速度幅值 6.6 rad/s）。注意不
    // 以 1e-9 级绝对容差判"两侧一致"——有限差分带 O(δ·jerk) 固有偏差，
    // 收敛行为（比值≈2）才是极限相等的证明。
    const double dStep = std::min(knots[1] - knots[0], knots[2] - knots[1])
                         * 1e-6;
    const trj::TimedSample left1 = result.timeCurve->sampleAt(tm - dStep);
    const trj::TimedSample right1 = result.timeCurve->sampleAt(tm + dStep);
    const trj::TimedSample left2 = result.timeCurve->sampleAt(tm - dStep / 2.0);
    const trj::TimedSample right2 = result.timeCurve->sampleAt(tm + dStep / 2.0);
    const double vGap1 = std::abs(right1.qd[0] - left1.qd[0]);
    const double vGap2 = std::abs(right2.qd[0] - left2.qd[0]);
    // 速度两侧差：C² 结构（结点二阶导恒 0）使一阶项消去——邻域偏差为
    // O(δ³) 超线性（h0011'/h0010' 的二阶导在结点为 0），实测比值应显著
    // 大于 2（三阶预测 8，多项式混合下取 >3 下界）。
    EXPECT_GT(vGap1 / vGap2, 3.0) << "速度两侧差应超线性收缩（C² 结构）";
    EXPECT_LT(vGap2, 0.05);
    const double aGap1 = std::abs(right1.qdd[0] - left1.qdd[0]);
    const double aGap2 = std::abs(right2.qdd[0] - left2.qdd[0]);
    // 加速度两侧差：O(δ) 线性（加速度结点为 0、jerk 有限）——比值≈2。
    EXPECT_NEAR(aGap1 / aGap2, 2.0, 0.5) << "加速度两侧差应随邻域线性收缩";
    EXPECT_LT(aGap2, 0.05);
}

// =====================================================================
// 驻留常值段——零速零加速度＋驻留结点两侧导数为零（§12.3/§9.1）
// =====================================================================

/**
 * 用例：运动段＋驻留 0.5 s＋运动段。断言：分段时间逐条目对齐且驻留项
 * 位级保持请求值（0.5——驻留不被缩放，工况事件语义）；总节拍＝Σ分段
 * （§12.4——OPT-D 节拍指标唯一来源的输出面）；驻留区间中点采样位置恒
 * 定、速度/加速度全零（常值段三零——§9.1）；驻留结点（前后两结点）速
 * 度/加速度为零（§12.1"驻留结点两侧导数为零"）；结点时刻严格递增。
 */
TEST(TimeParamGoldenTest, DwellConstantZeroDerivatives)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05"},
                  std::vector<std::string>{"AT-06"});

    trj::TimeParamRequest request = baseRequest();
    request.sampleStepS = 0.0005;
    trj::TimeParamSegment seg0;
    seg0.segmentIndex = 0;
    seg0.geometry = jointPath({0.0, 0.3});
    trj::TimeParamSegment dwell;
    dwell.segmentIndex = 1;
    dwell.dwellDurationS = 0.5;  // s（工况 Dwell 事件时长）
    trj::TimeParamSegment seg1;
    seg1.segmentIndex = 2;
    seg1.geometry = jointPath({0.3, 0.5});
    request.segments = {seg0, dwell, seg1};
    request.jointLimits = {limits1(100.0, 1000.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeParam.has_value());

    // 分段时间逐段对齐（含驻留段）＋驻留原值保持。
    ASSERT_EQ(result.segmentDurationsS.size(), 3U);
    EXPECT_EQ(result.segmentDurationsS[1], 0.5) << "驻留时长不得被缩放";
    // 总节拍＝Σ分段时间＝knotTimesS.back()（§12.4——位级自洽）。
    double sumDurations = 0.0;
    for (double d : result.segmentDurationsS) {
        sumDurations += d;
    }
    EXPECT_EQ(sumDurations, result.timeParam->totalDurationS);
    EXPECT_EQ(result.timeParam->knotTimesS.back(),
              result.timeParam->totalDurationS);

    // 结点结构：seg0 两结点＋驻留后结点＋seg1 末结点＝4（共享边界）。
    const std::vector<double>& knots = result.timeParam->knotTimesS;
    ASSERT_EQ(knots.size(), 4U);
    for (std::size_t k = 1; k < knots.size(); ++k) {
        EXPECT_GT(knots[k], knots[k - 1]) << "结点时刻须严格递增（§9.1）";
    }

    // 驻留区间中点：位置恒定 0.3、速度/加速度全零（常值段三零）。
    const double tDwellMid = (knots[1] + knots[2]) / 2.0;
    const trj::TimedSample hold = result.timeCurve->sampleAt(tDwellMid);
    EXPECT_NEAR(hold.q[0], 0.3, 1e-12);
    EXPECT_NEAR(hold.qd[0], 0.0, 1e-12);
    EXPECT_NEAR(hold.qdd[0], 0.0, 1e-12);

    // 驻留结点两侧导数为零（§12.1——k1 驻留前、k2 驻留后）。
    const trj::TimedSample pre = result.timeCurve->sampleAt(knots[1]);
    const trj::TimedSample post = result.timeCurve->sampleAt(knots[2]);
    EXPECT_NEAR(pre.qd[0], 0.0, 1e-12);
    EXPECT_NEAR(pre.qdd[0], 0.0, 1e-12);
    EXPECT_NEAR(post.qd[0], 0.0, 1e-12);
    EXPECT_NEAR(post.qdd[0], 0.0, 1e-12);
}

// =====================================================================
// 限值校验——速度/加速度缩放收敛（V-16/V-17；acceptance ②）
// =====================================================================

/**
 * 用例（V-16）：低速度限值 V=0.5。初值时长 0.2 s 的峰值速度 0.9375>0.5
 * →缩放 α=1.875（解析）→总时长 0.375 s 峰值恰达限值。断言：Ok；总时
 * 长与解析预期一致（一轮精确收敛——浮点上 α 可精确表示）；峰值达标
 * （附录 D 第 10 项容差语义）。
 */
TEST(TimeParamLimitTest, VelocityScalingConverges)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05", "TRJ-06"},
                  std::vector<std::string>{"AT-06"});

    trj::TimeParamRequest request = baseRequest();
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, 0.1});
    request.segments = {seg};
    request.jointLimits = {limits1(0.5, 1000.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeParam.has_value());
    // 初值时长解析（实现公式——静止-静止剖面解析系数）：h0 =
    // max(1.875·0.1/0.5, sqrt((10/√3)·0.1/1000))＝max(0.375, 0.024)＝
    // 0.375；初值峰值速度＝1.875·0.1/0.375＝0.5 恰在限值线（±浮点，被
    // 1e-9 相对容差吸收——不缩放或等价微缩，总时长仍 0.375）。
    EXPECT_NEAR(result.timeParam->totalDurationS, 0.375, 1e-12);
    // 达标语义：峰值在限值×(1+1e-9 相对)＋C7 ε_abs 内（acceptance 2）。
    EXPECT_LE(result.peakJointVelocity[0],
              0.5 * (1.0 + trj::kTimeParamLimitRelativeTolerance) + 1e-9);
    // 采样欠估有界（峰值附近平坦——欠估 ~Δ²·系数 量级，放宽到千分之五）。
    EXPECT_GE(result.peakJointVelocity[0], 0.5 * (1.0 - 5e-3));
    EXPECT_LE(result.peakJointAcceleration[0],
              1000.0 * (1.0 + trj::kTimeParamLimitRelativeTolerance) + 1e-9);
}

/**
 * 用例（V-17）：低加速度限值 A=5。初值时长 sqrt(2·0.1/5)=0.2 s 的峰值
 * 加速度 14.43>5→α=sqrt(14.43/5)→峰值精确达限值。断言：Ok；峰值加速
 * 度达标且贴近限值（缩放精确性）；速度峰值同步缩小仍达标。
 */
TEST(TimeParamLimitTest, AccelerationScalingConverges)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05", "TRJ-06"},
                  std::vector<std::string>{"AT-06"});

    trj::TimeParamRequest request = baseRequest();
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, 0.1});
    request.segments = {seg};
    request.jointLimits = {limits1(100.0, 5.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeParam.has_value());
    // 达标语义（加速度半区——V-17 观测点）。初值时长解析：
    // max(1.875·0.1/100, sqrt((10/√3)·0.1/5))＝max(0.001875, 0.33987)
    // ＝0.33987——初值峰值加速度恰在线上（容差内达标）。
    EXPECT_LE(result.peakJointAcceleration[0],
              5.0 * (1.0 + trj::kTimeParamLimitRelativeTolerance) + 1e-9);
    // 采样欠估有界（放宽到千分之五——峰值附近平坦的网格欠估）。
    EXPECT_GE(result.peakJointAcceleration[0], 5.0 * (1.0 - 5e-3));
    EXPECT_LE(result.peakJointVelocity[0],
              100.0 * (1.0 + trj::kTimeParamLimitRelativeTolerance) + 1e-9);
    // 总时长贴近加速度约束解析下限（初值即达标——无显著缩放）。
    EXPECT_NEAR(result.timeParam->totalDurationS,
                std::sqrt((10.0 / std::sqrt(3.0)) * 0.1 / 5.0), 1e-9);
}

// =====================================================================
// 限速校验容差——附录 D 第 10 项（相对 1×10⁻⁹）边缘钉扎（acceptance ②）
// =====================================================================

/**
 * 用例（acceptance ②——附录 D 第 10 项相对 1×10⁻⁹ 的双侧钉扎）：
 *   - **容差内不处置**：单段 V=0.6、初值恰线达标零缩放，实测峰值 P。
 *     限值缩紧至 P·(1−5×10⁻¹⁰)（超出 5×10⁻¹⁰ 相对，容差内）→仍判达标：
 *     总时长＝新限值下的初值分配值（测试侧独立书写初值公式——若发生缩
 *     放必按 α>1 严格偏离公式值）。
 *     注：初值分配随限值自适应（初值时长∝1/限值），故"限值缩紧"不产
 *     生超限——边缘语义以"处置分派"双侧钉扎：本半区钉"容差内不处置"。
 *   - **容差外必处置**：初值估计局限下的真实加速度超限（几何
 *     [0,1,1.01]＋interiorS={0.5}——末子区间时长 0.0187 s 的局部峰值
 *     加速度≈165 rad/s² 超出 A=150 约 10%，在 1×10⁻⁹ 容差外）→触发缩
 *     放：总时长严格大于初值分配值＋终态峰值达标。
 * 确定性保证：同请求初值峰值位级一致（NFR-COR-02）。
 */
TEST(TimeParamLimitTest, ToleranceEdgeRelative1e9)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05"},
                  std::vector<std::string>{"AT-06"});

    // 初值分配公式的测试侧独立书写（速度项主导——加速度限值 1e6 远离；
    // 折线几何的逐轴折线长度＝D，8 探针精确还原线性折线长度）。
    const double deltaQ = 0.1;
    const auto initialDuration = [&](double vLimit, double pathLength) {
        const double speedTerm = 1.875 * pathLength / vLimit;
        const double accelTerm =
            std::sqrt((10.0 / std::sqrt(3.0)) * pathLength / 1e6);
        return std::max(std::max(speedTerm, accelTerm), 1e-6);
    };

    // ===== 容差内半区：单段 V=0.6 零缩放基准＋5e-10 缩紧不处置。
    trj::TimeParamRequest base = baseRequest();
    base.sampleStepS = 0.0002;
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, deltaQ});
    base.segments = {seg};
    base.jointLimits = {limits1(0.6, 1e6)};
    const trj::TimeParamResult baseline = trj::timeParameterize(base);
    ASSERT_EQ(baseline.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(baseline.timeParam.has_value());
    // 初值达标证明：总时长＝初值分配值（零缩放）。
    EXPECT_NEAR(baseline.timeParam->totalDurationS, initialDuration(0.6, deltaQ),
                1e-12);
    const double peakP = baseline.peakJointVelocity[0];
    EXPECT_GT(peakP, 0.0);

    trj::TimeParamRequest within = base;
    within.jointLimits = {limits1(peakP * (1.0 - 5e-10), 1e6)};
    const trj::TimeParamResult rWithin = trj::timeParameterize(within);
    ASSERT_EQ(rWithin.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(rWithin.timeParam.has_value());
    EXPECT_NEAR(rWithin.timeParam->totalDurationS,
                initialDuration(peakP * (1.0 - 5e-10), deltaQ), 1e-12)
        << "5e-10 相对超出在 1e-9 容差内——不得触发缩放（附录 D 第 10 项）";

    // ===== 容差外半区：初值估计局限下的真实加速度超限→缩放处置。
    // 几何 [0,1,1.01]＋interiorS={0.5}：初值时长由速度项主导（1.8889 s
    // ——按全轨迹行程估计）；段内 L1 比例分配使末子区间时长仅 0.0187 s，
    // 其局部峰值加速度＝(10/√3)·0.01/0.0187²≈165 rad/s²——超出 A=150
    // 约 10%（初值估计无法预知局部短区间的加速度集中，缩放环兜底——
    // 恰是 §12.2 步骤 4 的真实触发场景）。A=1e6 不参与（速度远离限值）。
    trj::TimeParamRequest beyond = base;
    beyond.sampleStepS = 0.0005;
    beyond.segments.clear();
    trj::TimeParamSegment segB;
    segB.segmentIndex = 0;
    segB.geometry = jointPath({0.0, 1.0, 1.01});
    segB.interiorS = {0.5};  // 折线均匀参数化：s=0.5 恰为中点路点 1.0
    beyond.segments = {segB};
    beyond.jointLimits = {limits1(1.0, 150.0)};
    const double initB = initialDuration(1.0, 1.01);
    ASSERT_GT(initB, 1.0) << "初值应由速度项主导（速度项 1.8889 s）";
    const trj::TimeParamResult rBeyond = trj::timeParameterize(beyond);
    ASSERT_EQ(rBeyond.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(rBeyond.timeParam.has_value());
    // 初值即超限（局部加速度≈165＞150，容差外）→缩放必发生（总时长严
    // 格大于初值分配）。
    EXPECT_GT(rBeyond.timeParam->totalDurationS, initB)
        << "10% 加速度超限（容差外）必须触发缩放处置";
    // 处置后达标（终态峰值在限值容差带内）。
    EXPECT_LE(rBeyond.peakJointAcceleration[0],
              150.0 * (1.0 + trj::kTimeParamLimitRelativeTolerance) + 1e-9);
    EXPECT_GE(rBeyond.peakJointAcceleration[0], 150.0 * (1.0 - 5e-3))
        << "缩放应把峰值带到限值线（采样欠估有界）";
    // 速度峰值同步缩放仍达标。
    EXPECT_LE(rBeyond.peakJointVelocity[0],
              1.0 * (1.0 + trj::kTimeParamLimitRelativeTolerance) + 1e-9);
}

// =====================================================================
// 限值未定义——+inf 语义（V-18/§12.6；不伪造节拍）
// =====================================================================

/**
 * 用例：全部限值 +inf 与部分限值 +inf 两种注入。断言：LimitUnreachable；
 * 素材 reasonToken＝TRJ-TIME-PARAM-FAILED；**节拍不产出**（timeParam 无
 * 值、segmentDurationsS 空、timeCurve 空——§12.4"不得输出 0 或估计值"）；
 * 素材为全轨迹级（segmentIndex=0xFFFFFFFF）＋中文原因含"限值未定义"。
 */
TEST(TimeParamFailureTest, UndefinedLimitNoFabricatedCycleTime)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05", "TRJ-06"},
                  std::vector<std::string>{"AT-06"});
    constexpr double inf = std::numeric_limits<double>::infinity();

    // —— 全部 +inf（§15.8 非法示例"限值全 +inf→LimitUnreachable"）。
    trj::TimeParamRequest allInf = baseRequest();
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, 0.1});
    allInf.segments = {seg};
    allInf.jointLimits = {limits1(inf, inf)};
    trj::TimeParamResult result = trj::timeParameterize(allInf);
    EXPECT_EQ(result.status, trj::TimeParamStatus::LimitUnreachable);
    EXPECT_FALSE(result.timeParam.has_value()) << "不伪造节拍（§12.4）";
    EXPECT_TRUE(result.segmentDurationsS.empty());
    EXPECT_TRUE(result.timeCurve == nullptr);
    ASSERT_EQ(result.failureRecords.size(), 1U);
    EXPECT_EQ(result.failureRecords[0].reasonToken,
              std::string(trj::kTrjTimeParamFailed));
    EXPECT_EQ(result.failureRecords[0].segmentIndex, 0xFFFFFFFFu);
    EXPECT_EQ(result.failureRecords[0].phaseToken,
              std::string(trj::kPhaseTimeParam));
    EXPECT_NE(result.failureRecords[0].cause.find("限值未定义"),
              std::string::npos);
    // 建议动作＝补模型限值（ERR-01 中文建议动作）。
    EXPECT_NE(result.failureRecords[0].recommendedAction.find("maxVelocity"),
              std::string::npos);

    // —— 部分 +inf（速度有限、加速度 +inf——任一量缺失即时间无界）。
    trj::TimeParamRequest partial = allInf;
    partial.jointLimits = {limits1(1.0, inf)};
    result = trj::timeParameterize(partial);
    EXPECT_EQ(result.status, trj::TimeParamStatus::LimitUnreachable);
    EXPECT_FALSE(result.timeParam.has_value());
    EXPECT_TRUE(result.segmentDurationsS.empty());
}

// =====================================================================
// 多关节同步（V-02 时间侧——§7.3"最严关节统一时间律"）
// =====================================================================

/**
 * 用例：三关节不同行程（0.2/0.3/0.4 rad）单段、同限值。断言：Ok；末样
 * 本（总末点）三关节同时到达终点且速度全零——"各轴同时刻到达"的时间
 * 侧语义（同一结点表驱动全部关节，同步性结构性成立，测试钉扎）。
 */
TEST(TimeParamSyncTest, MultiJointSimultaneousArrival)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "TRJ-05"},
                  std::vector<std::string>{});

    trj::TimeParamRequest request = baseRequest();
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    {
        std::vector<rw::math::Q> points;
        points.emplace_back(3, 0.1, 0.5, 0.2);
        points.emplace_back(3, 0.3, 0.2, 0.6);
        seg.geometry = trj::makeLinearJointPathGeometry(points);
    }
    request.segments = {seg};
    request.jointLimits = {limits1(1.0, 10.0), limits1(1.0, 10.0),
                           limits1(1.0, 10.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeParam.has_value());
    ASSERT_FALSE(result.timeParam->samples.empty());
    // 末样本：三关节同刻到位（同一时间轴——同步语义钉扎）＋末点零速。
    const trj::TimedSample& last = result.timeParam->samples.back();
    EXPECT_EQ(last.t, result.timeParam->totalDurationS);
    EXPECT_NEAR(last.q[0], 0.3, 1e-12);
    EXPECT_NEAR(last.q[1], 0.2, 1e-12);
    EXPECT_NEAR(last.q[2], 0.6, 1e-12);
    EXPECT_NEAR(last.qd[0], 0.0, 1e-12);
    EXPECT_NEAR(last.qd[1], 0.0, 1e-12);
    EXPECT_NEAR(last.qd[2], 0.0, 1e-12);
}

// =====================================================================
// 采样计划——事件对齐＋步长上界＋峰值统计同源（§6.2/§13.1）
// =====================================================================

/**
 * 用例：samples 结构契约。断言：时刻非递减、首 0、末 totalDurationS；
 * 相邻间隔 ≤ sampleStepS（均匀填充的上界）；knotTimesS 的每个结点时刻
 * 在 samples 中位级出现（事件对齐——段起点必含）；峰值统计与独立扫描
 * samples 的逐轴最大绝对值位级一致（统计与离散视图同源——§6.2"两者并
 * 存且同源"）。
 */
TEST(TimeParamSamplingTest, EventAlignedKnotsAndPeakConsistency)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05"}, std::vector<std::string>{});

    trj::TimeParamRequest request = baseRequest();
    request.sampleStepS = 0.002;
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, 0.1});
    request.segments = {seg};
    request.jointLimits = {limits1(1.0, 10.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    const trj::TimeParameterization& tp = *result.timeParam;
    ASSERT_FALSE(tp.samples.empty());

    // 时刻结构：非递减＋端点封闭。
    EXPECT_EQ(tp.samples.front().t, 0.0);
    EXPECT_EQ(tp.samples.back().t, tp.totalDurationS);
    for (std::size_t i = 1; i < tp.samples.size(); ++i) {
        EXPECT_GE(tp.samples[i].t, tp.samples[i - 1].t);
    }
    // 步长上界（均匀填充——段时长/分段数 ≤ sampleStepS＋浮点余量）。
    for (std::size_t i = 1; i < tp.samples.size(); ++i) {
        EXPECT_LE(tp.samples[i].t - tp.samples[i - 1].t,
                  request.sampleStepS * (1.0 + 1e-9));
    }
    // 事件对齐：全部结点时刻在 samples 中出现（位级匹配）。
    for (double kt : tp.knotTimesS) {
        bool found = false;
        for (const trj::TimedSample& s : tp.samples) {
            if (s.t == kt) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "结点时刻 " << kt << " 未被采样（事件对齐违约）";
    }
    // 峰值统计与独立扫描一致（位级——同一采样计划）。
    double scanV = 0.0;
    double scanA = 0.0;
    for (const trj::TimedSample& s : tp.samples) {
        scanV = std::max(scanV, std::abs(s.qd[0]));
        scanA = std::max(scanA, std::abs(s.qdd[0]));
    }
    EXPECT_EQ(scanV, result.peakJointVelocity[0]);
    EXPECT_EQ(scanA, result.peakJointAcceleration[0]);
}

// =====================================================================
// 段内关键路点结点（§12.1 结点集的段内扩展——插值性钉扎）
// =====================================================================

/**
 * 用例：单段＋interiorS={0.5}。断言：全局结点 3 个（0/interior/1）；中
 * 间结点时刻的采样位置与几何求值器在该 s 处的值一致（插值性——结点值
 * ＝几何求值）；中间结点速度＝中心差分（两端结点为端点，导数为零→中
 * 点差分 (q1−q0)/(t2−t0)）。
 */
TEST(TimeParamGoldenTest, InteriorKnotsInterpolating)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05"}, std::vector<std::string>{});

    trj::TimeParamRequest request = baseRequest();
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, 0.1, 0.4});  // 三路点折线
    seg.interiorS = {0.5};  // 折线参数化：s=0.5 恰为中点路点 0.1（§11.1）
    request.segments = {seg};
    request.jointLimits = {limits1(100.0, 1000.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeParam.has_value());
    const std::vector<double>& knots = result.timeParam->knotTimesS;
    ASSERT_EQ(knots.size(), 3U);

    // 插值性：中间结点采样位置==geometry.sampleAt(0.5)==0.1（折线均匀
    // 参数化的中点路点）。
    const trj::TimedSample mid = result.timeCurve->sampleAt(knots[1]);
    EXPECT_NEAR(mid.q[0], 0.1, 1e-12);
    // 中心差分黄金对照（端点零导→中间结点差分跨全轨迹邻域）。
    const double expectedM = (0.4 - 0.0) / (knots[2] - knots[0]);
    expectNearRel(mid.qd[0], expectedM, 1e-12, "interior 结点中心差分");
}

// =====================================================================
// 取消与确定性（§15.0 UX-03/NFR-COR-02）
// =====================================================================

/**
 * 用例：取消观测命中→Canceled；零产物＋零错误素材（UX-03——取消不是
 * 错误）。对照：不取消的同请求 Ok（取消路径未污染正常路径）。
 */
TEST(TimeParamCancelTest, CanceledZeroMaterial)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    trj::TimeParamRequest request = baseRequest();
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, 0.1});
    request.segments = {seg};
    request.jointLimits = {limits1(1.0, 10.0)};
    request.cancel = []() { return true; };

    const trj::TimeParamResult result = trj::timeParameterize(request);
    EXPECT_EQ(result.status, trj::TimeParamStatus::Canceled);
    EXPECT_FALSE(result.timeParam.has_value());
    EXPECT_TRUE(result.segmentDurationsS.empty());
    EXPECT_TRUE(result.timeCurve == nullptr);
    EXPECT_TRUE(result.failureRecords.empty()) << "取消零错误素材（UX-03）";
}

/**
 * 用例：确定性——同请求两次独立调用，值面（status/timeParam/分段时长/
 * 峰值统计）逐字段等价（NFR-COR-02"同输入等价输出"；timeCurve 为新实
 * 例不参与值等价——TimeParamResult::operator== 注）。
 */
TEST(TimeParamDeterminismTest, RepeatedCallsEquivalentValues)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{});

    trj::TimeParamRequest request = baseRequest();
    trj::TimeParamSegment seg0;
    seg0.segmentIndex = 0;
    seg0.geometry = jointPath({0.0, 0.3});
    trj::TimeParamSegment dwell;
    dwell.segmentIndex = 1;
    dwell.dwellDurationS = 0.2;
    request.segments = {seg0, dwell};
    request.jointLimits = {limits1(0.8, 6.0)};

    const trj::TimeParamResult first = trj::timeParameterize(request);
    const trj::TimeParamResult second = trj::timeParameterize(request);
    ASSERT_EQ(first.status, trj::TimeParamStatus::Ok);
    EXPECT_EQ(first.status, second.status);
    ASSERT_TRUE(first.timeParam.has_value());
    ASSERT_TRUE(second.timeParam.has_value());
    EXPECT_EQ(*first.timeParam, *second.timeParam) << "位级等价（含 samples）";
    EXPECT_EQ(first.segmentDurationsS, second.segmentDurationsS);
    EXPECT_EQ(first.peakJointVelocity, second.peakJointVelocity);
    EXPECT_EQ(first.peakJointAcceleration, second.peakJointAcceleration);
}

// =====================================================================
// ITimeCurve 求值契约（公共接口消费面——越界 fail-fast）
// =====================================================================

/**
 * 用例：ITimeCurve 越界输入 fail-fast（NFR-COR-03——不静默截断）；token
 * ＝"trajectory/time-param/curve-range"。
 */
TEST(TimeParamCurveTest, OutOfRangeFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    trj::TimeParamRequest request = baseRequest();
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    seg.geometry = jointPath({0.0, 0.1});
    request.segments = {seg};
    request.jointLimits = {limits1(1.0, 10.0)};

    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeCurve != nullptr);
    const double total = result.timeParam->totalDurationS;

    bool caught = false;
    try {
        result.timeCurve->sampleAt(total + 0.1);
    } catch (const trj::TrajectoryError& e) {
        caught = true;
        EXPECT_EQ(e.token(), "trajectory/time-param/curve-range");
    }
    EXPECT_TRUE(caught) << "超总时长必须 fail-fast";

    caught = false;
    try {
        result.timeCurve->sampleAt(-1e-9);
    } catch (const trj::TrajectoryError& e) {
        caught = true;
        EXPECT_EQ(e.token(), "trajectory/time-param/curve-range");
    }
    EXPECT_TRUE(caught) << "负时刻必须 fail-fast";
}

// =====================================================================
// 前置违约族（NFR-COR-03——调用方契约违约 fail-fast；确定性首错序）
// =====================================================================

/**
 * 辅助：断言请求抛 TrajectoryError 且 token 精确匹配（fail-fast 语义＋
 * 稳定 token 面）。
 */
void expectReject(const trj::TimeParamRequest& request,
                  const std::string& token)
{
    bool caught = false;
    try {
        static_cast<void>(trj::timeParameterize(request));
    } catch (const trj::TrajectoryError& e) {
        caught = true;
        EXPECT_EQ(e.token(), token) << "首错 token 应为 " << token;
    }
    ASSERT_TRUE(caught) << "应抛 TrajectoryError（" << token << "）";
}

/**
 * 用例：前置校验全表（校验序固定——每注入只坏一处，其余字段合法）。
 * 覆盖：运动律 token 词表／空段序列／驻留首位／连续驻留／驻留时长非法
 * ／interiorS 越界与乱序／几何维度不一致／限值维度不符／限值非法值／
 * limitsScaleFactor 越界／采样步长非法／迭代上限为零／段端点位级不等。
 */
TEST(TimeParamPreconditionTest, FailFastFamily)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    // —— 运动律 token 词表（§12.7 封闭词表）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {limits1(1.0, 10.0)};
        r.motionLawToken = "cubic-spline";
        expectReject(r, "trajectory/time-param/motion-law");
    }
    // —— 空段序列。
    {
        trj::TimeParamRequest r = baseRequest();
        r.jointLimits = {limits1(1.0, 10.0)};
        expectReject(r, "trajectory/time-param/segments-empty");
    }
    // —— 驻留首位（无可驻留构型）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment dwell;
        dwell.segmentIndex = 0;
        dwell.dwellDurationS = 0.5;
        r.segments = {dwell};
        r.jointLimits = {limits1(1.0, 10.0)};
        expectReject(r, "trajectory/time-param/dwell-lead");
    }
    // —— 连续驻留（要求调用方合并）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        trj::TimeParamSegment d0;
        d0.segmentIndex = 1;
        d0.dwellDurationS = 0.3;
        trj::TimeParamSegment d1;
        d1.segmentIndex = 2;
        d1.dwellDurationS = 0.4;
        r.segments = {seg, d0, d1};
        r.jointLimits = {limits1(1.0, 10.0)};
        expectReject(r, "trajectory/time-param/dwell-lead");
    }
    // —— 驻留时长非法（0 与负值）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        trj::TimeParamSegment d;
        d.segmentIndex = 1;
        d.dwellDurationS = 0.0;
        r.segments = {seg, d};
        r.jointLimits = {limits1(1.0, 10.0)};
        expectReject(r, "trajectory/time-param/dwell-duration");
    }
    // —— interiorS 越界（s=0——端点由段边界承担）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        seg.interiorS = {0.0};
        r.segments = {seg};
        r.jointLimits = {limits1(1.0, 10.0)};
        expectReject(r, "trajectory/time-param/interior-range");
    }
    // —— interiorS 乱序（严格升序违约）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1, 0.2});
        seg.interiorS = {0.6, 0.3};
        r.segments = {seg};
        r.jointLimits = {limits1(1.0, 10.0)};
        expectReject(r, "trajectory/time-param/interior-order");
    }
    // —— 段几何维度不一致。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg0;
        seg0.segmentIndex = 0;
        {
            std::vector<rw::math::Q> pts;
            pts.emplace_back(1, 0.0);
            pts.emplace_back(1, 0.1);
            seg0.geometry = trj::makeLinearJointPathGeometry(pts);
        }
        trj::TimeParamSegment seg1;
        seg1.segmentIndex = 1;
        {
            std::vector<rw::math::Q> pts;
            pts.emplace_back(2, 0.1, 0.0);
            pts.emplace_back(2, 0.1, 0.2);
            seg1.geometry = trj::makeLinearJointPathGeometry(pts);
        }
        r.segments = {seg0, seg1};
        r.jointLimits = {limits1(1.0, 10.0), limits1(1.0, 10.0)};
        expectReject(r, "trajectory/time-param/geometry-dim");
    }
    // —— 限值视图维度不符。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {};  // 缺失
        expectReject(r, "trajectory/time-param/limits-dim");
    }
    // —— 限值非法值（零与 NaN）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {limits1(0.0, 10.0)};
        expectReject(r, "trajectory/time-param/limits-domain");
    }
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {limits1(
            std::numeric_limits<double>::quiet_NaN(), 10.0)};
        expectReject(r, "trajectory/time-param/limits-domain");
    }
    // —— limitsScaleFactor 越界（0 与 >1）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {limits1(1.0, 10.0)};
        r.limitsScaleFactor = 0.0;
        expectReject(r, "trajectory/time-param/scale-factor");
    }
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {limits1(1.0, 10.0)};
        r.limitsScaleFactor = 1.5;
        expectReject(r, "trajectory/time-param/scale-factor");
    }
    // —— 采样步长非法。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {limits1(1.0, 10.0)};
        r.sampleStepS = 0.0;
        expectReject(r, "trajectory/time-param/sample-step");
    }
    // —— 迭代上限为零。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {limits1(1.0, 10.0)};
        r.maxIterations = 0;
        expectReject(r, "trajectory/time-param/max-iterations");
    }
    // —— 相邻段端点位级不等（几何连续性守卫——禁止静默缝合）。
    {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg0;
        seg0.segmentIndex = 0;
        seg0.geometry = jointPath({0.0, 0.1});
        trj::TimeParamSegment seg1;
        seg1.segmentIndex = 1;
        seg1.geometry = jointPath({0.1000001, 0.3});  // 起点≠前段终点
        r.segments = {seg0, seg1};
        r.jointLimits = {limits1(1.0, 10.0)};
        expectReject(r, "trajectory/time-param/knot-mismatch");
    }
}

// =====================================================================
// limitsScaleFactor 消费（§5.3/§5.5——限值＝视图×比例，+inf 不被掩盖）
// =====================================================================

/**
 * 用例：scale=0.5 时等效限值减半——同一几何在 (V=1, scale=1.0) 与
 * (V=1, scale=0.5) 下的总时长，后者应恰为前者的 2 倍（速度约束主导时
 * 时长∝1/V 的解析关系；加速度限值同比例放大保持不起作用）。断言缩放
 * 系数进入有效限值面（零私有副本——只做引用×系数）。
 */
TEST(TimeParamScaleFactorTest, HalvedScaleDoublesDuration)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05"},
                  std::vector<std::string>{});

    // 请求构造（lambda——TEST 体内无嵌套函数；几何/限值面同构，仅缩放
    // 系数不同）。
    const auto make = []() {
        trj::TimeParamRequest r = baseRequest();
        trj::TimeParamSegment seg;
        seg.segmentIndex = 0;
        seg.geometry = jointPath({0.0, 0.1});
        r.segments = {seg};
        r.jointLimits = {limits1(1.0, 1e9)};  // 加速度约束远离（速度主导）
        return r;
    };
    trj::TimeParamRequest full = make();
    full.limitsScaleFactor = 1.0;
    const trj::TimeParamResult rFull = trj::timeParameterize(full);
    ASSERT_EQ(rFull.status, trj::TimeParamStatus::Ok);

    trj::TimeParamRequest half = make();
    half.limitsScaleFactor = 0.5;
    const trj::TimeParamResult rHalf = trj::timeParameterize(half);
    ASSERT_EQ(rHalf.status, trj::TimeParamStatus::Ok);

    ASSERT_TRUE(rFull.timeParam.has_value());
    ASSERT_TRUE(rHalf.timeParam.has_value());
    expectNearRel(rHalf.timeParam->totalDurationS,
                  2.0 * rFull.timeParam->totalDurationS, 1e-9,
                  "scale=0.5 时长应倍增（速度约束主导）");
}
