/**
 * @file   ContinuousProcessTest.cpp
 * @brief  连续工艺段边界行为校验的运行期测试（WP-16-T16——TRJ-08-S3）
 *         ——解析黄金算例（分段匀速替身的零残差回推测量／真实 quintic
 *         管线的连续/驻留/隐式停驻三态）＋边界行为断言（速度连续＋切
 *         换语义）＋违例素材（TRJ-CONTINUITY-BROKEN 比较型＋边界定位）
 *         ＋取消观测＋请求违约族＋空边界合法＋确定性重放＋ContinuousPro-
 *         cess 段结构守卫（TrjTypes 第三分支）。
 *
 * 设计依据：
 *   - units/trajectory.md §19.1（S3 行交接物——SegmentSpaceType 词表扩
 *     展＋段间边界行为定义）、§9.1（速度连续维 1×10⁻⁹ 相对容差——附录
 *     D 第 10 项同源；驻留/不停驻边界 R2 行）、§9.2（速度不连续/驻留切
 *     换非零反例族——TRJ-CONTINUITY-BROKEN 码面）、§12.1（驻留结点两
 *     侧导数为零；内部结点中心差分——隐式停驻反例的构造依据）、§15.0
 *     （错误二分/取消/确定性）
 *   - 需求 TRJ-08-S3（P1/R2——"连续段边界行为断言：段间速度连续、驻
 *     留/不停驻切换明确"，本套件即其验收执行面）、TRJ-06（比较型素材
 *     定位）、NFR-COR-01（解析对照——黄金参照独立书写）、NFR-COR-02
 *     （确定性）、NFR-COR-03（违约拒绝）
 *   - 任务契约 tasks/foundation/WP-16-T16.json（acceptance 1——连续段
 *     边界行为断言；acceptance 2 的词表面归 EvaluationDims 套件）
 *
 * 黄金算例的解析基准（独立书写——与实现 TU 无共享代码，NFR-COR-01）：
 *   - 分段匀速替身：q(t) = v1·t（t≤T1）；q(T1)+v2·(t−T1)（t>T1）。qd
 *     两侧解析恒 v1/v2、qdd 恒 0 ⇒ 一阶回推残差位级为零（qdd=0 使回
 *     推项消失——测量值即解析值，浮点噪声内位级对照）；速度差 |v2−v1|
 *     解析可控（连续/不连续/隐式停驻三态独立构造）。
 *   - 真实 quintic 管线（timeParameterize）：两段等长等距关节线性几何
 *     （q: 0→1、q: 1→2）——边界结点导数＝中心差分＝1/T（T＝段初值时
 *     长，两段斜率同 1/T）>0 ⇒ 连续切换黄金断言（速度连续 Ok＋两侧
 *     非零）；折返几何（q: 0→1、q: 1→0）——中心差分分子 (q_{k+1}−
 *     q_{k−1}) = (0−0) = 0 精确 ⇒ 边界结点速度精确零，声明连续切换即
 *     隐式停驻反例（真实管线产物上的"切换不明确"检出）；
 *   - 驻留切换（真实管线）：运动段＋驻留段——驻留结点两侧速度/加速度
 *     恒 0（§12.1 构造纪律）⇒ Dwell 断言黄金通过；回推残差 O(jerk·δ²)
 *     ：quintic 剖面 jerk 峰值闭式 60·Δq/T³（T08 批登记），Δ 取 1×10⁻⁷
 *     s 时残差 ≈60×(1e-7)² = 6×10⁻¹³ rad/s ≪ C7 ε_abs 1×10⁻⁹——判定
 *     精度声明（neighborDeltaS 的声明义务兑现）。
 *
 * 本套件为**集成模式专属**（消费 ITimeCurve/timeParameterize 的 Q 构造
 * 面＋rw::math 符号——TimeParamTest 同款 gating；冒烟模式不编译本文件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/trajectory/ContinuousProcess.hpp>
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/TimeParam.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/math/Q.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trj = sdurws::ird::trajectory;
namespace core = sdurws::ird::core;  // ObjectId（素材 subject 投影——ARC-04）

namespace {

// =====================================================================
// 时间曲线替身（ITimeCurve 实现——解析黄金曲线的连续求值面）
// =====================================================================

/**
 * @brief 分段匀速 1DOF 替身（q 单位 m；黄金算例主载体——qd 两侧解析
 *        恒 v1/v2、qdd 恒 0 ⇒ 一阶回推残差位级为零，测量值即解析值）。
 *
 * 两段在 t=T1 处拼接：q(t) = v1·t（t≤T1）；q(T1)+v2·(t−T1)（t>T1）。
 * v1==v2 ⇒ 边界 C¹ 连续（速度连续黄金）；v1≠v2 ⇒ C¹ 破缺（速度不连续
 * 反例——差 |v2−v1| 解析可控）；v1==v2==0 ⇒ 停驻（隐式停驻反例）。
 */
class PiecewiseUniformCurve final : public trj::ITimeCurve {
public:
    PiecewiseUniformCurve(double totalS, double switchS, double vBefore,
                          double vAfter)
        : m_totalS(totalS), m_switchS(switchS), m_v1(vBefore), m_v2(vAfter)
    {
    }

    trj::TimedSample sampleAt(double t) const override
    {
        // 求值契约（ITimeCurve——越界 fail-fast；替身同规）。
        if (!(t >= 0.0 && t <= m_totalS)) {
            throw trj::TrajectoryError(
                "trajectory/test/curve-range",
                "替身曲线越界求值：t=" + std::to_string(t) + " s，域 [0, "
                    + std::to_string(m_totalS) + "] s");
        }
        trj::TimedSample sample;
        sample.t = t;
        // 位置（m）：边界点连续（段2 从段1 末位置起算——折返反例除外，
        // 本替身不做折返；折返由真实管线用例承载）。
        const double q1 = m_v1 * m_switchS;
        const double q = (t <= m_switchS) ? m_v1 * t
                                          : q1 + m_v2 * (t - m_switchS);
        // 速度（m/s）：两侧解析常值——边界时刻取左段值（ITimeCurve 结
        // 点契约；本面经两侧内点回推测两侧，替身内点即解析值）。
        const double qd = (t <= m_switchS) ? m_v1 : m_v2;
        sample.q = rw::math::Q(1, q);
        sample.qd = rw::math::Q(1, qd);
        sample.qdd = rw::math::Q(1, 0.0);  // 匀速——二阶导恒 0（回推零残差）
        return sample;
    }

private:
    double m_totalS;   ///< 总时长（s）
    double m_switchS;  ///< 段边界时刻（s）
    double m_v1;       ///< 前段速度（m/s——解析真值）
    double m_v2;       ///< 后段速度（m/s——解析真值）
};

/// 静止替身（q(t) = q0 恒值——驻留切换黄金载体，§12.3 驻留口径）。
class StationaryCurve final : public trj::ITimeCurve {
public:
    explicit StationaryCurve(double totalS, double q0M = 0.5)
        : m_totalS(totalS), m_q0(q0M)
    {
    }

    trj::TimedSample sampleAt(double t) const override
    {
        if (!(t >= 0.0 && t <= m_totalS)) {
            throw trj::TrajectoryError("trajectory/test/curve-range",
                                       "静止替身越界求值");
        }
        trj::TimedSample sample;
        sample.t = t;
        sample.q = rw::math::Q(1, m_q0);
        sample.qd = rw::math::Q(1, 0.0);   // m/s——驻留零速
        sample.qdd = rw::math::Q(1, 0.0);  // m/s²——驻留零加速度
        return sample;
    }

private:
    double m_totalS;
    double m_q0;  ///< 恒值关节坐标（m）
};

/// 双轴分段匀速替身（范数计量的黄金载体——轴 1 速度 0.5、轴 2 速度
/// 0.02，‖qd‖_∞ 取 0.5；连续性差异在两轴同时发生）。
class TwoAxisPiecewiseCurve final : public trj::ITimeCurve {
public:
    TwoAxisPiecewiseCurve(double totalS, double switchS, double v1Axis1,
                          double v1Axis2, double v2Axis1, double v2Axis2)
        : m_totalS(totalS), m_switchS(switchS)
        , m_v1{v1Axis1, v1Axis2}, m_v2{v2Axis1, v2Axis2}
    {
    }

    trj::TimedSample sampleAt(double t) const override
    {
        if (!(t >= 0.0 && t <= m_totalS)) {
            throw trj::TrajectoryError("trajectory/test/curve-range",
                                       "双轴替身越界求值");
        }
        const double* v = (t <= m_switchS) ? m_v1 : m_v2;
        trj::TimedSample sample;
        sample.t = t;
        sample.q = rw::math::Q(2, 0.0, 0.0);
        sample.qd = rw::math::Q(2, v[0], v[1]);      // rad/s（转动轴——量纲声明）
        sample.qdd = rw::math::Q(2, 0.0, 0.0);       // 匀速——零残差口径
        return sample;
    }

private:
    double m_totalS;
    double m_switchS;
    double m_v1[2];  ///< 前段逐轴速度（rad/s）
    double m_v2[2];  ///< 后段逐轴速度（rad/s）
};

// =====================================================================
// 真实管线黄金算例构造（timeParameterize 产物——T08 既有管线零改动的
 // 消费面证明）
// =====================================================================

/**
 * @brief 两段等长关节线性几何的时间参数化（1DOF，m——移动关节黄金装
 *        置；limits 全有限使 Ok 达标；初值时长由管线确定性分配）。
 *
 * @param qMid  [in] 段边界构型（m；等长等距＝两段 |Δq| 相等——直进
 *              0→1→2 与折返 0→1→0 均满足）
 * @param qEnd  [in] 第二段终点构型（m；2.0＝直进、0.0＝折返）
 * @return 时间参数化结果（断言 Ok 后取 timeCurve）
 */
trj::TimeParamResult timeParamTwoSegments(double qMid, double qEnd)
{
    using namespace trj;
    // 两段几何：关节空间线性（Smooth.hpp 的 makeLinearJointPathGeometry
    // ——T04 产物形态即 T08 输入形态；路点向量形式，首末即段端点）。
    TimeParamRequest request;
    TimeParamSegment seg0;
    seg0.segmentIndex = 0;
    seg0.geometry =
        makeLinearJointPathGeometry({rw::math::Q(1, 0.0), rw::math::Q(1, qMid)});
    TimeParamSegment seg1;
    seg1.segmentIndex = 1;
    seg1.geometry = makeLinearJointPathGeometry(
        {rw::math::Q(1, qMid), rw::math::Q(1, qEnd)});
    request.segments = {seg0, seg1};
    // 限值视图：速度 2.0 m/s、加速度 4.0 m/s²（黄金装置——有限正值使
    // Ok 达标；jerkLimit 缺省 +inf＝未设值〔v1 运动律不消费〕）。
    JointDynamicLimits limits;
    limits.velocityLimit = 2.0;
    limits.accelerationLimit = 4.0;
    request.jointLimits = {limits};
    request.limitsScaleFactor = 1.0;
    request.motionLawToken = std::string(kTimeParamMethodQuinticSplineC2);
    request.sampleStepS = 0.01;
    request.maxIterations = 32;
    const TimeParamResult result = timeParameterize(request);
    return result;
}

/**
 * @brief 运动段＋驻留段的混合序列时间参数化（真实驻留切换黄金载体—
 *        —§12.3 驻留段＝独立条目，TimeParamSegment 二分语义）。
 */
trj::TimeParamResult timeParamMotionPlusDwell()
{
    using namespace trj;
    TimeParamRequest request;
    TimeParamSegment seg0;
    seg0.segmentIndex = 0;
    seg0.geometry = makeLinearJointPathGeometry(
        {rw::math::Q(1, 0.0), rw::math::Q(1, 1.0)});
    TimeParamSegment dwell;
    dwell.segmentIndex = 1;
    dwell.dwellDurationS = 0.5;  // s——驻留时长（工况事件语义，§5.4 步骤 3）
    TimeParamSegment seg2;
    seg2.segmentIndex = 2;
    seg2.geometry = makeLinearJointPathGeometry(
        {rw::math::Q(1, 1.0), rw::math::Q(1, 2.0)});
    request.segments = {seg0, dwell, seg2};
    JointDynamicLimits limits;
    limits.velocityLimit = 2.0;
    limits.accelerationLimit = 4.0;
    request.jointLimits = {limits};
    request.motionLawToken = std::string(kTimeParamMethodQuinticSplineC2);
    request.sampleStepS = 0.01;
    request.maxIterations = 32;
    return timeParameterize(request);
}

/// 组装单边界投影条目（黄金算例公共骨架——默认 1DOF 前段 0）。
trj::ProcessBoundarySpan boundary(std::uint32_t boundaryIndex,
                                  double tBoundaryS,
                                  trj::BoundarySwitchKind expected,
                                  double totalS)
{
    trj::ProcessBoundarySpan span;
    span.boundaryIndex = boundaryIndex;
    span.beforeSegmentIndex = boundaryIndex;
    span.afterSegmentIndex = boundaryIndex + 1;
    span.tBoundaryS = tBoundaryS;
    span.expectedSwitch = expected;
    (void)totalS;
    return span;
}

/// 黄金请求骨架（分段匀速替身＋边界条目；δ=1e-3 s、阈值 0.1——替身
/// qdd=0 零残差口径，δ 只需域内）。
trj::ProcessBoundaryCheckRequest goldenRequest(
    std::shared_ptr<const trj::ITimeCurve> curve, double totalS,
    trj::ProcessBoundarySpan span)
{
    trj::ProcessBoundaryCheckRequest request;
    request.timeCurve = std::move(curve);
    request.totalDurationS = totalS;
    request.boundaries = {std::move(span)};
    request.neighborDeltaS = 1e-3;
    request.minContinuousSpeed = 0.1;
    return request;
}

}  // namespace

// =====================================================================
// 验收标准前半：段间速度连续断言（TRJ-08-S3——黄金 Ok 面＋反例检出面）
// =====================================================================

/**
 * 黄金 Ok 面：两段同速（v1=v2=0.5 m/s）——速度连续（差 0 ≤ 容差）＋
 * 连续切换断言两侧范数 0.5 ≥ 阈值 0.1 ⇒ Ok 零素材；观测对照解析值
 * （位级——qdd=0 零残差口径）。
 */
TEST(TrjContinuousProcess, ContinuousSwitchGoldenOk_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<PiecewiseUniformCurve>(2.0, 1.0, 0.5, 0.5);
    trj::ProcessBoundaryCheckResult result = trj::checkProcessBoundaries(
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Continuous,
                               2.0)));

    // Ok 态三要素：零素材＋观测逐字段对齐＋satisfied 全通过。
    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Ok);
    EXPECT_TRUE(result.violationRecords.empty());
    ASSERT_EQ(result.boundaryObservations.size(), 1U);
    const trj::ProcessBoundaryObservation& obs =
        result.boundaryObservations.front();
    EXPECT_EQ(obs.boundaryIndex, 0U);
    EXPECT_DOUBLE_EQ(obs.tBoundaryS, 1.0);
    // 解析对照（位级——qdd=0 回推零残差，NFR-COR-01 独立书写基准）。
    EXPECT_DOUBLE_EQ(obs.speedBefore, 0.5);
    EXPECT_DOUBLE_EQ(obs.speedAfter, 0.5);
    EXPECT_DOUBLE_EQ(obs.speedDelta, 0.0);
    EXPECT_TRUE(obs.speedContinuousOk);
    EXPECT_TRUE(obs.switchSemanticsOk);
    EXPECT_TRUE(obs.satisfied);
}

/**
 * 速度不连续反例（§9.2 行 4 速度维反例族的校验面检出）：两段异速
 * （v1=0.5、v2=0.3 m/s）——两侧差 0.2 m/s >> 容差（≈1.5×10⁻⁹）⇒
 * Violated＋TRJ-CONTINUITY-BROKEN 比较型素材（actual/expected/单位
 * 三要素＋边界定位 segmentIndex/pathParameter/phase）。
 */
TEST(TrjContinuousProcess, VelocityDiscontinuityDetected_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<PiecewiseUniformCurve>(2.0, 1.0, 0.5, 0.3);
    trj::ProcessBoundaryCheckResult result = trj::checkProcessBoundaries(
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Continuous,
                               2.0)));

    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Violated);
    // 素材：恰一条（速度连续维——切换语义本例达标，无隐式停驻素材）。
    ASSERT_EQ(result.violationRecords.size(), 1U);
    const trj::FailedSegmentRecord& rec = result.violationRecords.front();
    EXPECT_EQ(rec.reasonToken, std::string(trj::kTrjContinuityBroken));
    EXPECT_EQ(rec.segmentIndex, 0U);            // 前段定位
    EXPECT_EQ(rec.phaseToken, "boundary-check");  // §14.1.6 表尾追加词形
    ASSERT_TRUE(rec.pathParameter.has_value());
    EXPECT_DOUBLE_EQ(*rec.pathParameter, 1.0);  // 前段末点 s=1.0
    // 比较型三要素（UX-03）：actual＝实测差 0.2 m/s；expected＝判定容差
    // （相对 1e-9×0.5＋C7 ε_abs 1e-9——量级断言）＋单位 rad/s。
    ASSERT_TRUE(rec.comparison.has_value());
    EXPECT_NEAR((*rec.comparison->actual.quantity.tryValue()), 0.2, 1e-12);
    EXPECT_NEAR((*rec.comparison->expected.quantity.tryValue()),
                1e-9 * 0.5 + 1e-9, 1e-15);
    EXPECT_EQ(std::string(rec.comparison->actual.unit.symbol()), "rad/s");
    // 观测仍完整（违例边界可观测——TRJ-06 事实全披露）。
    ASSERT_EQ(result.boundaryObservations.size(), 1U);
    EXPECT_FALSE(result.boundaryObservations.front().speedContinuousOk);
    EXPECT_DOUBLE_EQ(result.boundaryObservations.front().speedDelta, 0.2);
}

// =====================================================================
// 验收标准后半：驻留/不停驻切换明确断言（三类黄金＋反例）
// =====================================================================

/**
 * 连续切换黄金（真实管线）：两段直进等长几何（q: 0→1→2 m）——边界结
 * 点导数＝中心差分＝1/T >0 且 C² 连续 ⇒ Continuous 声明断言通过（Ok＋
 * 两侧非零＋速度连续）。
 */
TEST(TrjContinuousProcess, RealPipelineContinuousSwitchOk_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    const trj::TimeParamResult tp = timeParamTwoSegments(1.0, 2.0);
    ASSERT_EQ(tp.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(tp.timeParam.has_value());

    trj::ProcessBoundaryCheckRequest request;
    request.timeCurve = tp.timeCurve;
    request.totalDurationS = tp.timeParam->totalDurationS;
    // 边界时刻＝首段时长（segmentDurationsS 逐条目对齐——T15 同款投影
    // 义务：装配面以分段口径累加得边界时刻）。
    const double tBoundary = tp.segmentDurationsS.front();
    request.boundaries = {
        boundary(0, tBoundary, trj::BoundarySwitchKind::Continuous,
                 request.totalDurationS)};
    request.neighborDeltaS = 1e-7;  // s——quintic jerk 量级下的残差声明
    request.minContinuousSpeed = 1e-4;  // 速度量级 1/T≈0.5 m/s——阈值语义有效
    const trj::ProcessBoundaryCheckResult result =
        trj::checkProcessBoundaries(request);

    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Ok);
    EXPECT_TRUE(result.violationRecords.empty());
    ASSERT_EQ(result.boundaryObservations.size(), 1U);
    const trj::ProcessBoundaryObservation& obs =
        result.boundaryObservations.front();
    // 连续切换黄金断言：两侧速度非零（中心差分 1/T）且连续。
    EXPECT_GT(obs.speedBefore, 0.0);
    EXPECT_GT(obs.speedAfter, 0.0);
    EXPECT_TRUE(obs.speedContinuousOk);
    EXPECT_TRUE(obs.switchSemanticsOk);
    EXPECT_TRUE(obs.satisfied);
}

/**
 * 隐式停驻反例（真实管线——"切换不明确"的工艺违约检出）：折返几何
 * （q: 0→1→0 m，等长等距）——边界结点中心差分分子 (q_{k+1}−q_{k−1})
 * = (0−0) = 0 精确 ⇒ 边界速度零，声明连续切换 ⇒ 隐式停驻违例（
 * TRJ-CONTINUITY-BROKEN 素材，expected＝不停驻阈值）。
 */
TEST(TrjContinuousProcess, ImplicitDwellDetected_RealPipeline_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    const trj::TimeParamResult tp = timeParamTwoSegments(1.0, 0.0);  // 折返 0→1→0
    ASSERT_EQ(tp.status, trj::TimeParamStatus::Ok);

    trj::ProcessBoundaryCheckRequest request;
    request.timeCurve = tp.timeCurve;
    request.totalDurationS = tp.timeParam->totalDurationS;
    request.boundaries = {
        boundary(0, tp.segmentDurationsS.front(),
                 trj::BoundarySwitchKind::Continuous, request.totalDurationS)};
    request.neighborDeltaS = 1e-7;
    request.minContinuousSpeed = 0.1;
    const trj::ProcessBoundaryCheckResult result =
        trj::checkProcessBoundaries(request);

    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Violated);
    ASSERT_EQ(result.violationRecords.size(), 1U);
    const trj::FailedSegmentRecord& rec = result.violationRecords.front();
    EXPECT_EQ(rec.reasonToken, std::string(trj::kTrjContinuityBroken));
    EXPECT_EQ(rec.phaseToken, "boundary-check");
    ASSERT_TRUE(rec.comparison.has_value());
    // actual＝两侧范数取严侧（折返边界速度精确零——中心差分分子为零；
    // 回推残差 O(jerk·δ²) 量级 ≪ 阈值 0.1）。
    EXPECT_LT((*rec.comparison->actual.quantity.tryValue()), 1e-9);
    EXPECT_DOUBLE_EQ((*rec.comparison->expected.quantity.tryValue()), 0.1);
    EXPECT_EQ(std::string(rec.comparison->actual.unit.symbol()), "rad/s");
    EXPECT_FALSE(result.boundaryObservations.front().switchSemanticsOk);
    // 速度连续性本例成立（两侧同零——连续；违例仅在切换语义维）。
    EXPECT_TRUE(result.boundaryObservations.front().speedContinuousOk);
}

/**
 * 驻留切换黄金（真实管线）：运动段＋驻留段（0.5 s）——驻留结点两侧速
 * 度恒 0（§12.1 构造纪律）⇒ Dwell 声明断言通过（Ok＋两侧零速——回推
 * 残差 ≈6×10⁻¹³ ≪ ε_abs 1×10⁻⁹，δ=1e-7 s 声明口径）。
 */
TEST(TrjContinuousProcess, RealPipelineDwellSwitchOk_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    const trj::TimeParamResult tp = timeParamMotionPlusDwell();
    ASSERT_EQ(tp.status, trj::TimeParamStatus::Ok);
    ASSERT_EQ(tp.segmentDurationsS.size(), 3U);  // 运动段＋驻留段＋运动段

    trj::ProcessBoundaryCheckRequest request;
    request.timeCurve = tp.timeCurve;
    request.totalDurationS = tp.timeParam->totalDurationS;
    // 两处驻留切换边界：运动→驻留（t=首段时长）、驻留→运动（t=前两段
    // 累加）——均声明 Dwell。
    const double t0 = tp.segmentDurationsS[0];
    const double t1 = t0 + tp.segmentDurationsS[1];
    request.boundaries = {
        boundary(0, t0, trj::BoundarySwitchKind::Dwell, request.totalDurationS),
        boundary(1, t1, trj::BoundarySwitchKind::Dwell, request.totalDurationS)};
    request.neighborDeltaS = 1e-7;
    request.minContinuousSpeed = 0.1;
    const trj::ProcessBoundaryCheckResult result =
        trj::checkProcessBoundaries(request);

    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Ok);
    EXPECT_TRUE(result.violationRecords.empty());
    ASSERT_EQ(result.boundaryObservations.size(), 2U);
    for (const trj::ProcessBoundaryObservation& obs :
         result.boundaryObservations) {
        // 两侧零速黄金断言（回推残差 ≪ ε_abs——观测值应低于 1×10⁻¹¹）。
        EXPECT_LT(obs.speedBefore, 1e-11);
        EXPECT_LT(obs.speedAfter, 1e-11);
        EXPECT_TRUE(obs.speedContinuousOk);
        EXPECT_TRUE(obs.switchSemanticsOk);
        EXPECT_TRUE(obs.satisfied);
    }
}

/**
 * 驻留切换速度非零反例（§9.2 行 5 反例族的校验面检出）：匀速替身声明
 * Dwell ⇒ 违例（actual＝速度范数、expected＝C7 ε_abs、单位 rad/s）。
 */
TEST(TrjContinuousProcess, DwellSwitchSpeedNonzeroDetected_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<PiecewiseUniformCurve>(2.0, 1.0, 0.5, 0.5);
    trj::ProcessBoundaryCheckResult result = trj::checkProcessBoundaries(
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Dwell, 2.0)));

    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Violated);
    ASSERT_EQ(result.violationRecords.size(), 1U);
    const trj::FailedSegmentRecord& rec = result.violationRecords.front();
    EXPECT_EQ(rec.reasonToken, std::string(trj::kTrjContinuityBroken));
    EXPECT_EQ(rec.phaseToken, "boundary-check");
    ASSERT_TRUE(rec.comparison.has_value());
    EXPECT_DOUBLE_EQ((*rec.comparison->actual.quantity.tryValue()), 0.5);
    EXPECT_DOUBLE_EQ((*rec.comparison->expected.quantity.tryValue()), 1e-9);
    EXPECT_EQ(std::string(rec.comparison->actual.unit.symbol()), "rad/s");
    EXPECT_FALSE(result.boundaryObservations.front().switchSemanticsOk);
}

/**
 * 驻留替身声明 Dwell 黄金通过（替身面——StationaryCurve 零速零加速度，
 * §12.3 驻留口径的替身承载）。
 */
TEST(TrjContinuousProcess, StationaryDwellGoldenOk_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<StationaryCurve>(2.0, 0.5);
    trj::ProcessBoundaryCheckResult result = trj::checkProcessBoundaries(
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Dwell, 2.0)));

    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Ok);
    EXPECT_TRUE(result.violationRecords.empty());
    ASSERT_EQ(result.boundaryObservations.size(), 1U);
    EXPECT_DOUBLE_EQ(result.boundaryObservations.front().speedBefore, 0.0);
    EXPECT_DOUBLE_EQ(result.boundaryObservations.front().speedAfter, 0.0);
    EXPECT_TRUE(result.boundaryObservations.front().satisfied);
}

/**
 * 多轴范数计量黄金：双轴替身（前轴速度 0.5/0.02、后轴 0.3/0.5 rad/s）
 * ——‖qd‖_∞ 两侧均 0.5 ≥ 阈值 0.1（切换语义达标）但两轴同时跳变
 * （差 0.2/0.48 rad/s）⇒ 速度不连续检出——范数取最大轴的计量口径
 * （混合关节链的声明口径钉扎）。
 */
TEST(TrjContinuousProcess, TwoAxisInfinityNormSemantics_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<TwoAxisPiecewiseCurve>(2.0, 1.0, 0.5, 0.02,
                                                         0.3, 0.5);
    trj::ProcessBoundaryCheckResult result = trj::checkProcessBoundaries(
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Continuous,
                               2.0)));

    // 切换语义达标（两侧范数 0.5——取最大轴）；速度连续违例（逐轴差
    // 的范数 0.48——轴 2 主导）。
    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Violated);
    ASSERT_EQ(result.violationRecords.size(), 1U);
    ASSERT_TRUE(result.violationRecords.front().comparison.has_value());
    EXPECT_NEAR((*result.violationRecords.front()
                                 .comparison->actual.quantity.tryValue()),
                0.48, 1e-12);
    ASSERT_EQ(result.boundaryObservations.size(), 1U);
    EXPECT_DOUBLE_EQ(result.boundaryObservations.front().speedBefore, 0.5);
    EXPECT_DOUBLE_EQ(result.boundaryObservations.front().speedAfter, 0.5);
    EXPECT_FALSE(result.boundaryObservations.front().speedContinuousOk);
    EXPECT_TRUE(result.boundaryObservations.front().switchSemanticsOk);
}

/**
 * 双违例素材序（确定性组装序契约）：两段异速且后段低于阈值（v1=0.5、
 * v2=0.04 m/s，阈值 0.1）——速度连续素材在前、隐式停驻素材在后（维度
 * 序＝速度连续→切换语义；实现 TU 头注登记）。
 */
TEST(TrjContinuousProcess, ViolationRecordDimensionOrder_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<PiecewiseUniformCurve>(2.0, 1.0, 0.5, 0.04);
    trj::ProcessBoundaryCheckResult result = trj::checkProcessBoundaries(
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Continuous,
                               2.0)));

    ASSERT_EQ(result.status, trj::ProcessBoundaryStatus::Violated);
    ASSERT_EQ(result.violationRecords.size(), 2U);
    // 素材 1＝速度连续（actual≈0.46）；素材 2＝隐式停驻（actual=0.04
    // ——expected=阈值 0.1）。序＝维度序（确定性，NFR-COR-02）。
    ASSERT_TRUE(result.violationRecords[0].comparison.has_value());
    EXPECT_NEAR((*result.violationRecords[0].comparison->actual.quantity.tryValue()),
                0.46, 1e-12);
    ASSERT_TRUE(result.violationRecords[1].comparison.has_value());
    EXPECT_DOUBLE_EQ(
        (*result.violationRecords[1].comparison->actual.quantity.tryValue()), 0.04);
    EXPECT_DOUBLE_EQ(
        (*result.violationRecords[1].comparison->expected.quantity.tryValue()), 0.1);
}

// =====================================================================
// 通用契约：取消／空边界合法／违约族／确定性
// =====================================================================

/// 取消观测命中（UX-03）：Canceled 零素材零观测——取消不是错误。
TEST(TrjContinuousProcess, CanceledZeroMaterial_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<PiecewiseUniformCurve>(2.0, 1.0, 0.5, 0.5);
    trj::ProcessBoundaryCheckRequest request =
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Continuous,
                               2.0));
    request.cancel = [] { return true; };
    const trj::ProcessBoundaryCheckResult result =
        trj::checkProcessBoundaries(request);

    EXPECT_EQ(result.status, trj::ProcessBoundaryStatus::Canceled);
    EXPECT_TRUE(result.violationRecords.empty());
    EXPECT_TRUE(result.boundaryObservations.empty());
}

/// 空边界序列＝合法空结果（零断言零素材零观测——Ok 态空观测）。
TEST(TrjContinuousProcess, EmptyBoundariesLegalOk_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<PiecewiseUniformCurve>(2.0, 1.0, 0.5, 0.5);
    trj::ProcessBoundaryCheckRequest request =
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Continuous,
                               2.0));
    request.boundaries.clear();
    const trj::ProcessBoundaryCheckResult result =
        trj::checkProcessBoundaries(request);

    EXPECT_EQ(result.status, trj::ProcessBoundaryStatus::Ok);
    EXPECT_TRUE(result.violationRecords.empty());
    EXPECT_TRUE(result.boundaryObservations.empty());
}

/// 请求违约族 fail-fast（调用方契约违约——token 前缀族逐点断言）。
TEST(TrjContinuousProcess, RequestViolationFailFastFamily_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<PiecewiseUniformCurve>(2.0, 1.0, 0.5, 0.5);
    const trj::ProcessBoundarySpan okSpan =
        boundary(0, 1.0, trj::BoundarySwitchKind::Continuous, 2.0);

    // 1. timeCurve 缺失。
    {
        trj::ProcessBoundaryCheckRequest request =
            goldenRequest(nullptr, 2.0, okSpan);
        try {
            (void)trj::checkProcessBoundaries(request);
            FAIL() << "timeCurve 缺失应 fail-fast";
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token(), "trajectory/boundary/curve-missing");
        }
    }
    // 2. 总时长非法（0）。
    {
        trj::ProcessBoundaryCheckRequest request =
            goldenRequest(curve, 0.0, okSpan);
        try {
            (void)trj::checkProcessBoundaries(request);
            FAIL() << "总时长 0 应 fail-fast";
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token(), "trajectory/boundary/duration");
        }
    }
    // 3. 邻域半步非法（0——本域零自设默认，强制显式）。
    {
        trj::ProcessBoundaryCheckRequest request =
            goldenRequest(curve, 2.0, okSpan);
        request.neighborDeltaS = 0.0;
        try {
            (void)trj::checkProcessBoundaries(request);
            FAIL() << "邻域半步 0 应 fail-fast";
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token(), "trajectory/boundary/neighbor-delta");
        }
    }
    // 4. 不停驻阈值非法（0——工艺语义量强制显式）。
    {
        trj::ProcessBoundaryCheckRequest request =
            goldenRequest(curve, 2.0, okSpan);
        request.minContinuousSpeed = 0.0;
        try {
            (void)trj::checkProcessBoundaries(request);
            FAIL() << "阈值 0 应 fail-fast";
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token(), "trajectory/boundary/min-speed");
        }
    }
    // 5. 边界时刻越界（恰在端点——严格内语义）。
    {
        trj::ProcessBoundaryCheckRequest request =
            goldenRequest(curve, 2.0,
                          boundary(0, 0.0, trj::BoundarySwitchKind::Continuous,
                                   2.0));
        try {
            (void)trj::checkProcessBoundaries(request);
            FAIL() << "边界时刻 0 应 fail-fast";
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token(), "trajectory/boundary/time");
        }
    }
    {
        trj::ProcessBoundaryCheckRequest request =
            goldenRequest(curve, 2.0,
                          boundary(0, 2.0, trj::BoundarySwitchKind::Continuous,
                                   2.0));
        try {
            (void)trj::checkProcessBoundaries(request);
            FAIL() << "边界时刻＝总时长应 fail-fast";
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token(), "trajectory/boundary/time");
        }
    }
}

/// 确定性重放（NFR-COR-02）：同请求两次调用等价输出（值等价——素材
/// 与观测逐字段同值）。
TEST(TrjContinuousProcess, DeterministicReplay_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    auto curve = std::make_shared<PiecewiseUniformCurve>(2.0, 1.0, 0.5, 0.3);
    trj::ProcessBoundaryCheckRequest request =
        goldenRequest(curve, 2.0,
                      boundary(0, 1.0, trj::BoundarySwitchKind::Continuous,
                               2.0));
    // request.operator== 不比较 cancel（函数对象无等值语义）——两次调用
    // 均用无 cancel 的同构造请求。
    const trj::ProcessBoundaryCheckResult first =
        trj::checkProcessBoundaries(request);
    const trj::ProcessBoundaryCheckResult second =
        trj::checkProcessBoundaries(request);
    EXPECT_EQ(first, second);  // ProcessBoundaryCheckResult::operator==
}

// =====================================================================
// 段表达面：ContinuousProcess 段结构守卫（TrjTypes 第三分支——§19.1 S3
// 交接物"SegmentSpaceType 词表扩展"的执行面钉扎）
// =====================================================================

/**
 * ContinuousProcess 段结构守卫双路：同段域一致（全 q）合法通过；混带
 * （部分 q 部分 target）拒绝——validateTrajectorySegment 校验 2 第三分
 * 支（词表第三值的段级 variant 纪律）。
 */
TEST(TrjContinuousProcess, ContinuousProcessSegmentGuard_TRJ08S3_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    // 合法面：ContinuousProcess 段全 q 路点（域一致——几何域由调用方
    // 求值器决定，段数据模型只要求一致）。
    {
        trj::TrajectorySegment segment;
        segment.segmentIndex = 0;
        segment.spaceType = trj::SegmentSpaceType::ContinuousProcess;
        trj::Waypoint wp0;
        wp0.kind = trj::WaypointKind::Start;
        wp0.segmentIndex = 0;
        wp0.q = rw::math::Q(1, 0.0);
        trj::Waypoint wp1;
        wp1.kind = trj::WaypointKind::TaskPoint;
        wp1.segmentIndex = 0;
        wp1.q = rw::math::Q(1, 1.0);
        wp1.sourceTaskPoint = core::ObjectId{};
        segment.waypoints = {wp0, wp1};
        EXPECT_NO_THROW(trj::validateTrajectorySegment(segment));
    }
    // 合法面：全 target 同理（笛卡尔域一致）。
    {
        trj::TrajectorySegment segment;
        segment.segmentIndex = 0;
        segment.spaceType = trj::SegmentSpaceType::ContinuousProcess;
        trj::Waypoint wp0;
        wp0.kind = trj::WaypointKind::Start;
        wp0.segmentIndex = 0;
        wp0.target = rw::math::Transform3D<double>();
        trj::Waypoint wp1;
        wp1.kind = trj::WaypointKind::TaskPoint;
        wp1.segmentIndex = 0;
        wp1.target = rw::math::Transform3D<double>();
        wp1.sourceTaskPoint = core::ObjectId{};
        segment.waypoints = {wp0, wp1};
        EXPECT_NO_THROW(trj::validateTrajectorySegment(segment));
    }
    // 违例面：混带（首点 q、末点 target）——段级 variant 违约拒绝。
    {
        trj::TrajectorySegment segment;
        segment.segmentIndex = 0;
        segment.spaceType = trj::SegmentSpaceType::ContinuousProcess;
        trj::Waypoint wp0;
        wp0.kind = trj::WaypointKind::Start;
        wp0.segmentIndex = 0;
        wp0.q = rw::math::Q(1, 0.0);
        trj::Waypoint wp1;
        wp1.kind = trj::WaypointKind::TaskPoint;
        wp1.segmentIndex = 0;
        wp1.target = rw::math::Transform3D<double>();
        wp1.sourceTaskPoint = core::ObjectId{};
        segment.waypoints = {wp0, wp1};
        try {
            trj::validateTrajectorySegment(segment);
            FAIL() << "ContinuousProcess 段混带路点域应 fail-fast";
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token(), "trajectory/segment/domain-mismatch");
        }
    }
}
