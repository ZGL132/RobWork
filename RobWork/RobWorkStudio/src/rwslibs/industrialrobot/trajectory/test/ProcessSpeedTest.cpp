/**
 * @file   ProcessSpeedTest.cpp
 * @brief  工艺速度约束校验的运行期测试（WP-16-T15——TRJ-08-S2）——
 *         解析黄金算例（匀速直线 TCP 的零误差差分测量／正弦变速的一阶
 *         对照）＋约束满足断言（受限峰值≤限值／恒定带内逐点）＋超限诊
 *         断（TRJ-LIMIT-EXCEEDED 比较型素材＋段定位）＋§12.3 驻留口径
 *         ＋端口层分流（PortError 显性失败／Canceled 非错误）＋取消观
 *         测＋请求违约族＋确定性重放＋逐段观测（节拍分解细化面）＋FK
 *         端口消费纪律（逐采样点恰 2 次调用的直穿计数）。
 *
 * 设计依据：
 *   - units/trajectory.md §19.1（S2 行交接物——段约束扩展版本＋节拍分
 *     解细化）、§6.5（扩展版本结构承载——SegmentConstraint 本体零槽
 *     位）、§12.3（驻留＝零速常值段的文档化行为钉扎）、§14.4（TRJ-
 *     LIMIT-EXCEEDED 比较型码复用）、§15.0/§15.4（错误二分/③端口消费
 *     ——PortError 原词形显性失败先例＝CartesianLine.cpp 分流）
 *   - 需求 TRJ-08-S2（P1/R2——"工艺速度约束满足断言＋超限诊断"，本套
 *     件即其验收执行面）、TRJ-06（比较型素材）、NFR-COR-01（解析对照
 *     ——黄金参照独立书写）、NFR-COR-02（确定性）、NFR-COR-03（违约拒
 *     绝）、KIN-05 家族（未校验绝不冒充满足——端口缺失 fail-fast）
 *   - 任务契约 tasks/foundation/WP-16-T15.json（acceptance 1——约束满
 *     足断言＋超限诊断；acceptance 2 的词表面归 EvaluationDims 套件）
 *
 * 黄金算例的解析基准（独立书写——与实现 TU 无共享代码，NFR-COR-01）：
 *   - 匀速直线：q(t) = q0 + v·t（单移动关节，m）＋ FK 替身 P=(q,0,0)
 *     （基座系 m）⇒ TCP 线速度恒 v（m/s）；线性函数的中心差分无截断
 *     误差 ⇒ 测量值与解析值在浮点噪声内位级可对照。
 *   - 正弦变速：q(t) = A·sin(2π·f·t) ⇒ TCP 速度 v(t) = A·2π·f·|cos(2π
 *     f·t)|；t=0 处（span 起点，差分退化为单侧 O(δ)）测量值
 *     = A·sin(2πf·δ)/δ = A·2πf·(1 − (2πf·δ)²/6 − …)——本套件以相对容
 *     差 1×10⁻⁴ 对照解析值（δ=1e-3 时二阶项 ≈6.6×10⁻⁶，容差充分）。
 *   - 静止（驻留口径）：q(t) = q0 ⇒ TCP 速度恒 0。
 *
 * 本套件为**集成模式专属**（消费 ITimeCurve 的 Q 构造面＋rw::math 符号
 * ——TimeParamTest 同款 gating；冒烟模式不编译本文件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/KinematicsPort.hpp>
#include <sdurws/ird/trajectory/ProcessSpeed.hpp>
#include <sdurws/ird/trajectory/TimeParam.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/math/Quaternion.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trj = sdurws::ird::trajectory;
namespace core = sdurws::ird::core;  // ObjectId（素材 subject 投影——ARC-04）

namespace {

// =====================================================================
// FK 端口替身（1DOF 移动关节黄金装置——TCP 位置＝关节位置，基座系 m）
// =====================================================================

/**
 * @brief 黄金 FK 端口替身（端口/状态/错误传播的受控承载——替身纪律：
 *        只证明端口与错误传播面，轨迹速度的"真实机器人"语义由解析黄金
 *        算例的数学结构保证——1DOF 移动关节的 TCP 即滑杆，FK 是恒等投
 *        影，无需真实装置）。
 *
 * 三种注入模式（构造参数选定）：
 *   - Ok：正常测量面——tcpInBase = (q[0], 0, 0)（基座系 {B}，m/rad；
 *     姿态恒单位——速度只取平移部，姿态面与本套件判定无关）；
 *   - PortError：evaluateFk 恒回 PortError（端口层故障注入——显性失败
 *     分流的观测面）；
 *   - Canceled：evaluateFk 恒回 Canceled（端口侧取消观测——UX-03 面）。
 */
class GoldenFkPort final : public trj::IKinematicsComputePort {
public:
    enum class Mode { Ok, PortError, Canceled };

    explicit GoldenFkPort(Mode mode = Mode::Ok) : m_mode(mode) {}

    trj::FkPortReply evaluateFk(const trj::FkPortRequest& request) override
    {
        ++m_fkCalls;  // 直穿计数——端口消费纪律的观测面（用例 12 断言）
        trj::FkPortReply reply;
        switch (m_mode) {
        case Mode::Ok: {
            // 1DOF 移动关节黄金 FK：TCP 位置＝关节位置（基座系 {B}，m；
            // 恒等投影使解析黄金算例零映射误差）。
            const double x =
                request.q.empty() ? 0.0 : request.q.front();
            reply.status = trj::KinPortCallStatus::Ok;
            reply.metrics.tcpInBase =
                rw::math::Transform3D<double>(
                    rw::math::Vector3D<double>(x, 0.0, 0.0),
                    rw::math::Rotation3D<double>::identity());
            break;
        }
        case Mode::PortError:
            reply.status = trj::KinPortCallStatus::PortError;
            reply.errorToken = std::string(trj::kKinPortErrorPrefix)
                               + "injected";
            reply.errorMessage = "注入的端口层故障（测试——显性失败分流面）";
            break;
        case Mode::Canceled:
            reply.status = trj::KinPortCallStatus::Canceled;
            break;
        }
        return reply;
    }

    trj::IkPortReply solveIk(const trj::IkPortRequest& /*request*/) override
    {
        // 本套件零 IK 消费（工艺速度校验只消费 FK）——误用即测试缺陷。
        ADD_FAILURE() << "工艺速度校验不应消费 solveIk（FK-only 端口面）";
        trj::IkPortReply reply;
        reply.status = trj::KinPortCallStatus::PortError;
        reply.errorToken = std::string(trj::kKinPortErrorPrefix) + "misuse";
        reply.errorMessage = "solveIk 不属于工艺速度校验的端口消费面";
        return reply;
    }

    /// evaluateFk 累计调用数（直穿计数观测面——无量纲计数）。
    int fkCalls() const { return m_fkCalls; }

private:
    Mode m_mode;
    int m_fkCalls = 0;
};

// =====================================================================
// 时间曲线替身（ITimeCurve 实现——解析黄金曲线的连续求值面）
// =====================================================================

/**
 * @brief 1DOF 解析时间曲线基座（q(t) 由子类给出；qd/qdd 供完整性——
 *        工艺速度校验只消费 q）。
 *
 * 单自由度移动关节：q 单位 m（权威关节坐标）、qd 单位 m/s、qdd 单位
 * m/s²。TimedSample 的 tcpPose 恒 nullopt（§7.6——本域时间化不产 FK 派
 * 生面；TCP 位置由校验器经 FK 端口消费）。
 */
class Curve1D : public trj::ITimeCurve {
public:
    explicit Curve1D(double durationS) : m_durationS(durationS) {}

    trj::TimedSample sampleAt(double t) const override
    {
        // 求值契约（ITimeCurve——越界 fail-fast；替身同规）。
        if (!(t >= 0.0 && t <= m_durationS)) {
            throw trj::TrajectoryError(
                "trajectory/test/curve-range",
                "替身曲线越界求值：t=" + std::to_string(t) + " s，域 [0, "
                    + std::to_string(m_durationS) + "] s");
        }
        trj::TimedSample sample;
        sample.t = t;
        sample.q = rw::math::Q(1, positionAt(t));        // m
        sample.qd = rw::math::Q(1, velocityAt(t));       // m/s
        sample.qdd = rw::math::Q(1, accelerationAt(t));  // m/s²
        return sample;
    }

protected:
    virtual double positionAt(double t) const = 0;
    virtual double velocityAt(double) const { return 0.0; }
    virtual double accelerationAt(double) const { return 0.0; }

private:
    double m_durationS;
};

/// 匀速直线曲线：q(t) = q0 + v·t（黄金算例 1——TCP 线速度恒 v m/s）。
class UniformCurve final : public Curve1D {
public:
    UniformCurve(double durationS, double vMps, double q0M = 0.0)
        : Curve1D(durationS), m_v(vMps), m_q0(q0M)
    {
    }

protected:
    double positionAt(double t) const override { return m_q0 + m_v * t; }
    double velocityAt(double) const override { return m_v; }

private:
    double m_v;    ///< 匀速度（m/s——解析 TCP 速度真值）
    double m_q0;   ///< 起点关节坐标（m）
};

/// 静止曲线：q(t) = q0（黄金算例 3——TCP 速度恒 0，§12.3 驻留口径面）。
class StationaryCurve final : public Curve1D {
public:
    explicit StationaryCurve(double durationS, double q0M = 0.5)
        : Curve1D(durationS), m_q0(q0M)
    {
    }

protected:
    double positionAt(double) const override { return m_q0; }

private:
    double m_q0;  ///< 恒值关节坐标（m）
};

/// 正弦变速曲线：q(t) = A·sin(2π·f·t)（黄金算例 2——非恒定速度剖面）。
class SinusoidCurve final : public Curve1D {
public:
    SinusoidCurve(double durationS, double amplitudeM, double freqHz)
        : Curve1D(durationS), m_a(amplitudeM), m_w(2.0 * 3.14159265358979323846 * freqHz)
    {
    }

protected:
    double positionAt(double t) const override { return m_a * std::sin(m_w * t); }
    double velocityAt(double t) const override
    {
        return m_a * m_w * std::cos(m_w * t);
    }
    double accelerationAt(double t) const override
    {
        return -m_a * m_w * m_w * std::sin(m_w * t);
    }

public:
    /// 速度解析峰值（m/s——A·ω；黄金对照用）。
    double peakVelocity() const { return m_a * m_w; }

private:
    double m_a;   ///< 振幅（m）
    double m_w;   ///< 角频率（rad/s——2π·f）
};

// =====================================================================
// 请求组装辅助（黄金算例公共骨架）
// =====================================================================

/// 单受检段骨架（默认 [0.05, 0.95] s 的区间——不贴全局端点，中心差分
/// 主路径受测；端点口径由驻留用例覆盖）。
trj::ProcessSpeedSegmentSpan span(std::uint32_t segmentIndex, double tStart,
                                  double tEnd,
                                  trj::ProcessSpeedConstraint constraint,
                                  std::optional<core::ObjectId> subject =
                                      std::nullopt)
{
    trj::ProcessSpeedSegmentSpan s;
    s.segmentIndex = segmentIndex;
    s.tStartS = tStart;
    s.tEndS = tEnd;
    s.constraint = constraint;
    s.subject = std::move(subject);
    return s;
}

/// 受限约束（上限 m/s——Limited）。
trj::ProcessSpeedConstraint limited(double limitMps)
{
    trj::ProcessSpeedConstraint c;
    c.mode = trj::ProcessSpeedMode::Limited;
    c.tcpSpeedMps = limitMps;
    c.constantBandRelative = 0.0;
    return c;
}

/// 恒定约束（设定值 m/s＋相对半宽——Constant）。
trj::ProcessSpeedConstraint constantBand(double setMps, double band)
{
    trj::ProcessSpeedConstraint c;
    c.mode = trj::ProcessSpeedMode::Constant;
    c.tcpSpeedMps = setMps;
    c.constantBandRelative = band;
    return c;
}

/// 合法请求骨架（曲线/端口/区间由用例注入）。
trj::ProcessSpeedCheckRequest baseRequest(
    std::shared_ptr<const trj::ITimeCurve> curve,
    trj::IKinematicsComputePort* port, double totalS)
{
    trj::ProcessSpeedCheckRequest request;
    request.timeCurve = std::move(curve);
    request.fkPort = port;
    request.totalDurationS = totalS;
    request.sampleStepS = 0.01;  // s——采样步长（黄金算例声明）
    return request;
}

/// 近似断言辅助（相对判——黄金对照口径 1e-12；另支持绝对下限防零除）。
void expectNearRel(double actual, double expected, double relTol,
                   const char* what)
{
    const double absTol = relTol * std::abs(expected);
    EXPECT_NEAR(actual, expected, std::max(absTol, 1e-300)) << what;
}

}  // namespace

// =====================================================================
// TRJ-08-S2 验收面——约束满足断言（acceptance 1 前半）
// =====================================================================

/**
 * 受限模式满足断言（TRJ-08-S2——匀速直线黄金算例）：TCP 速度 0.05 m/s
 * 的轨迹，工艺上限 0.1 m/s ⇒ Ok、零违例素材、逐段观测满足标记与峰值
 * （解析对照——线性函数中心差分零截断，测量值＝真值 0.05 m/s）。
 */
TEST(TrjProcessSpeed, LimitedSatisfiedGoldenUniform_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    const double kTrueSpeed = 0.05;  // m/s（解析真值）
    auto curve = std::make_shared<UniformCurve>(1.0, kTrueSpeed);
    GoldenFkPort port;
    trj::ProcessSpeedCheckRequest request =
        baseRequest(curve, &port, 1.0);
    request.spans = {span(3U, 0.05, 0.95, limited(0.1))};

    const trj::ProcessSpeedCheckResult result = trj::checkProcessSpeed(request);

    // 满足断言（验收前半）：Ok＋零素材＋观测满足。
    ASSERT_EQ(result.status, trj::ProcessSpeedStatus::Ok);
    EXPECT_TRUE(result.violationRecords.empty());
    ASSERT_EQ(result.segmentObservations.size(), 1U);
    EXPECT_TRUE(result.segmentObservations[0].satisfied);
    EXPECT_EQ(result.segmentObservations[0].segmentIndex, 3U);
    EXPECT_DOUBLE_EQ(result.segmentObservations[0].tStartS, 0.05);
    EXPECT_DOUBLE_EQ(result.segmentObservations[0].tEndS, 0.95);
    // 峰值解析对照（NFR-COR-01——线性函数中心差分零截断；1e-12 相对）。
    expectNearRel(result.segmentObservations[0].peakTcpSpeedMps, kTrueSpeed,
                  1e-12, "受限段 TCP 峰值速度应等于解析真值（黄金算例）");
}

/**
 * 恒定模式满足断言（TRJ-08-S2——匀速直线在容差带内）：设定 0.05 m/s、
 * 带 ±5% ⇒ 全部采样点在带内 ⇒ Ok 零素材。
 */
TEST(TrjProcessSpeed, ConstantSatisfiedWithinBand_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    const double kSetSpeed = 0.05;  // m/s（恒定设定值＝轨迹真值）
    auto curve = std::make_shared<UniformCurve>(1.0, kSetSpeed);
    GoldenFkPort port;
    trj::ProcessSpeedCheckRequest request =
        baseRequest(curve, &port, 1.0);
    request.spans = {span(0U, 0.05, 0.95, constantBand(kSetSpeed, 0.05))};

    const trj::ProcessSpeedCheckResult result = trj::checkProcessSpeed(request);

    ASSERT_EQ(result.status, trj::ProcessSpeedStatus::Ok);
    EXPECT_TRUE(result.violationRecords.empty());
    ASSERT_EQ(result.segmentObservations.size(), 1U);
    EXPECT_TRUE(result.segmentObservations[0].satisfied);
    expectNearRel(result.segmentObservations[0].peakTcpSpeedMps, kSetSpeed,
                  1e-12, "恒定段 TCP 峰值速度应等于设定值（黄金算例）");
}

// =====================================================================
// TRJ-08-S2 验收面——超限诊断（acceptance 1 后半）
// =====================================================================

/**
 * 受限模式超限诊断（TRJ-08-S2）：TCP 速度 0.05 m/s 对上限 0.02 m/s ⇒
 * Violated＋TRJ-LIMIT-EXCEEDED 比较型素材（实际/期望/单位 m/s 三要素
 * ——UX-03）＋段定位（segmentIndex/pathParameter/subject）＋phase 词形
 * "process-speed-check"＋违例段观测 satisfied=false。
 */
TEST(TrjProcessSpeed, LimitedViolatedComparisonMaterial_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    const double kTrueSpeed = 0.05;  // m/s（轨迹真值）
    const double kLimit = 0.02;      // m/s（工艺上限——低于真值即超限）
    auto curve = std::make_shared<UniformCurve>(1.0, kTrueSpeed);
    GoldenFkPort port;
    const core::ObjectId taskPoint = core::ObjectId::generate();  // 素材 subject 投影
    trj::ProcessSpeedCheckRequest request =
        baseRequest(curve, &port, 1.0);
    request.spans = {span(2U, 0.05, 0.95, limited(kLimit), taskPoint)};

    const trj::ProcessSpeedCheckResult result = trj::checkProcessSpeed(request);

    // 超限诊断（验收后半）：Violated＋恰一条比较型素材。
    ASSERT_EQ(result.status, trj::ProcessSpeedStatus::Violated);
    ASSERT_EQ(result.violationRecords.size(), 1U);
    const trj::FailedSegmentRecord& rec = result.violationRecords[0];
    // 定位三要素（TRJ-06"具体段落与原因"）：段序号＋段内参数＋subject。
    EXPECT_EQ(rec.segmentIndex, 2U);
    ASSERT_TRUE(rec.pathParameter.has_value());
    EXPECT_GE(*rec.pathParameter, 0.0);
    EXPECT_LE(*rec.pathParameter, 1.0);
    EXPECT_EQ(rec.subject, taskPoint);
    // 稳定码与阶段（码复用零新增——"限制超标"语义本体；§14.4 行 5）。
    EXPECT_EQ(rec.reasonToken, std::string(trj::kTrjLimitExceeded));
    EXPECT_EQ(rec.phaseToken, "process-speed-check");
    // 比较型三要素（UX-03）：实际＝实测峰值、期望＝限值、单位＝m/s。
    ASSERT_TRUE(rec.comparison.has_value());
    ASSERT_TRUE(rec.comparison->actual.quantity.tryValue().has_value());
    ASSERT_TRUE(rec.comparison->expected.quantity.tryValue().has_value());
    expectNearRel(*rec.comparison->actual.quantity.tryValue(), kTrueSpeed,
                  1e-12, "素材实际值应为实测 TCP 峰值速度（黄金算例）");
    expectNearRel(*rec.comparison->expected.quantity.tryValue(), kLimit, 0.0,
                  "素材期望值应为工艺上限");
    EXPECT_EQ(std::string(rec.comparison->actual.unit.symbol()), "m/s");
    EXPECT_EQ(std::string(rec.comparison->expected.unit.symbol()), "m/s");
    // 中文文案非空（ERR-01 字段义务）。
    EXPECT_FALSE(rec.cause.empty());
    EXPECT_FALSE(rec.recommendedAction.empty());
    // 违例段观测仍然完整（TRJ-06 段落事实全披露）。
    ASSERT_EQ(result.segmentObservations.size(), 1U);
    EXPECT_FALSE(result.segmentObservations[0].satisfied);
    expectNearRel(result.segmentObservations[0].peakTcpSpeedMps, kTrueSpeed,
                  1e-12, "违例段观测峰值应等于解析真值");
}

/**
 * 恒定模式越带诊断（TRJ-08-S2——非恒定速度剖面）：正弦曲线 v(0)=A·ω
 * 对设定 0.3 m/s、带 ±10% ⇒ 首个采样点即越上带 ⇒ Violated＋素材定位
 * 首越差点（s=0——最小 t 确定性）＋实测值对照解析峰值（单侧差分口径，
 * 1e-4 相对——O(δ) 端点截断的黄金容差声明）。
 */
TEST(TrjProcessSpeed, ConstantViolatedFirstOutlier_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    SinusoidCurve* raw = nullptr;
    std::shared_ptr<trj::ITimeCurve> curve;
    {
        auto owned = std::make_shared<SinusoidCurve>(1.0, 0.1, 1.0);
        raw = owned.get();
        curve = owned;
    }
    const double kAnalyticPeak = raw->peakVelocity();  // A·ω（≈0.62832 m/s）
    const double kSet = 0.3;                           // m/s（恒定设定值）
    GoldenFkPort port;
    trj::ProcessSpeedCheckRequest request = baseRequest(curve, &port, 1.0);
    request.spans = {span(5U, 0.0, 0.9, constantBand(kSet, 0.10))};

    const trj::ProcessSpeedCheckResult result = trj::checkProcessSpeed(request);

    ASSERT_EQ(result.status, trj::ProcessSpeedStatus::Violated);
    ASSERT_EQ(result.violationRecords.size(), 1U);
    const trj::FailedSegmentRecord& rec = result.violationRecords[0];
    EXPECT_EQ(rec.segmentIndex, 5U);
    // 首越差点定位：t=0（span 起点采样——确定性最小 t）。
    ASSERT_TRUE(rec.pathParameter.has_value());
    EXPECT_DOUBLE_EQ(*rec.pathParameter, 0.0);
    // 实测值对照解析峰值（t=0 单侧差分 O(δ)——1e-4 相对容差声明）。
    ASSERT_TRUE(rec.comparison.has_value());
    ASSERT_TRUE(rec.comparison->actual.quantity.tryValue().has_value());
    expectNearRel(*rec.comparison->actual.quantity.tryValue(), kAnalyticPeak,
                  1e-4, "首越差点实测值应对照解析速度峰值（单侧差分口径）");
    ASSERT_TRUE(rec.comparison->expected.quantity.tryValue().has_value());
    expectNearRel(*rec.comparison->expected.quantity.tryValue(), kSet, 0.0,
                  "素材期望值应为恒定设定值");
    EXPECT_EQ(std::string(rec.comparison->actual.unit.symbol()), "m/s");
    EXPECT_FALSE(rec.cause.empty());
    EXPECT_FALSE(rec.recommendedAction.empty());
    EXPECT_FALSE(result.segmentObservations[0].satisfied);
}

// =====================================================================
// §12.3 驻留口径（文档化行为钉扎）
// =====================================================================

/**
 * 驻留区间的速度事实原样披露（§12.3——驻留＝零速常值段）：静止曲线的
 * 受检区间上——受限模式 0 ≤ 限值恒满足（Ok，观测峰值 0）；恒定模式
 * 0 越出 [0.0475, 0.0525] 带（Violated——把恒定约束投到驻留区间是调用
 * 方错误，由诊断暴露而非静默吞掉；头注登记的文档化行为）。
 */
TEST(TrjProcessSpeed, DwellSemanticsStationaryInterval_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    auto curve = std::make_shared<StationaryCurve>(1.0, 0.5);
    GoldenFkPort port;

    // —— 受限模式：零速 ≤ 正上限 ⇒ Ok（峰值观测 0——黄金对照）。
    {
        trj::ProcessSpeedCheckRequest request =
            baseRequest(curve, &port, 1.0);
        request.spans = {span(1U, 0.1, 0.4, limited(0.05))};
        const trj::ProcessSpeedCheckResult result =
            trj::checkProcessSpeed(request);
        ASSERT_EQ(result.status, trj::ProcessSpeedStatus::Ok);
        ASSERT_EQ(result.segmentObservations.size(), 1U);
        EXPECT_TRUE(result.segmentObservations[0].satisfied);
        EXPECT_DOUBLE_EQ(result.segmentObservations[0].peakTcpSpeedMps, 0.0);
    }
    // —— 恒定模式：零速越出正设定值容差带 ⇒ Violated（真实语义）。
    {
        trj::ProcessSpeedCheckRequest request =
            baseRequest(curve, &port, 1.0);
        request.spans = {span(1U, 0.1, 0.4, constantBand(0.05, 0.05))};
        const trj::ProcessSpeedCheckResult result =
            trj::checkProcessSpeed(request);
        ASSERT_EQ(result.status, trj::ProcessSpeedStatus::Violated);
        ASSERT_EQ(result.violationRecords.size(), 1U);
        ASSERT_TRUE(result.violationRecords[0].comparison.has_value());
        ASSERT_TRUE(result.violationRecords[0]
                        .comparison->actual.quantity.tryValue()
                        .has_value());
        EXPECT_DOUBLE_EQ(
            *result.violationRecords[0].comparison->actual.quantity.tryValue(),
            0.0);
    }
}

// =====================================================================
// 端口层分流与取消（§15.4/§15.0——显性失败/取消不是错误）
// =====================================================================

/**
 * 端口层故障显性失败（§15.4 PortError 分流——CartesianLine 先例）：FK
 * 替身注入 PortError ⇒ TrajectoryError 且 token 保留原端口前缀
 * "trajectory/kin-port-*"（不吞错、不转素材、不静默降级）。
 */
TEST(TrjProcessSpeed, PortErrorFailsFastOriginalToken_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    auto curve = std::make_shared<UniformCurve>(1.0, 0.05);
    GoldenFkPort port(GoldenFkPort::Mode::PortError);
    trj::ProcessSpeedCheckRequest request = baseRequest(curve, &port, 1.0);
    request.spans = {span(0U, 0.05, 0.95, limited(0.1))};

    EXPECT_THROW(
        {
            try {
                (void)trj::checkProcessSpeed(request);
            } catch (const trj::TrajectoryError& e) {
                // 原 token 透传（端口前缀——§15.4"端口层错误 token
                // trajectory/kin-port-*"）。
                EXPECT_EQ(e.token(),
                          std::string(trj::kKinPortErrorPrefix) + "injected");
                throw;
            }
        },
        trj::TrajectoryError);
}

/**
 * 端口侧取消＝结果 Canceled（UX-03——取消不是错误）：FK 替身注入
 * Canceled ⇒ status=Canceled、零素材零观测（不产出违例结论）。
 */
TEST(TrjProcessSpeed, PortCanceledResultNoMaterial_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    auto curve = std::make_shared<UniformCurve>(1.0, 0.05);
    GoldenFkPort port(GoldenFkPort::Mode::Canceled);
    trj::ProcessSpeedCheckRequest request = baseRequest(curve, &port, 1.0);
    request.spans = {span(0U, 0.05, 0.95, constantBand(0.05, 0.05))};

    const trj::ProcessSpeedCheckResult result = trj::checkProcessSpeed(request);
    EXPECT_EQ(result.status, trj::ProcessSpeedStatus::Canceled);
    EXPECT_TRUE(result.violationRecords.empty());
    EXPECT_TRUE(result.segmentObservations.empty());
}

/**
 * 取消观测命中＝Canceled 零素材（§15.1——循环边界轮询）：计数取消信
 * 号（第 3 次轮询命中）⇒ Canceled；零错误素材（取消不是错误）。
 */
TEST(TrjProcessSpeed, CancelSignalMidCheckZeroMaterial_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    auto curve = std::make_shared<UniformCurve>(1.0, 0.05);
    GoldenFkPort port;
    trj::ProcessSpeedCheckRequest request = baseRequest(curve, &port, 1.0);
    request.spans = {span(0U, 0.05, 0.95, limited(0.1))};
    int polls = 0;
    request.cancel = [&polls] { return ++polls >= 3; };

    const trj::ProcessSpeedCheckResult result = trj::checkProcessSpeed(request);
    EXPECT_EQ(result.status, trj::ProcessSpeedStatus::Canceled);
    EXPECT_TRUE(result.violationRecords.empty());
    EXPECT_TRUE(result.segmentObservations.empty());
}

// =====================================================================
// 请求违约族（NFR-COR-03——fail-fast 不钳制不静默）
// =====================================================================

/**
 * 调用方契约违约族（校验序固定——同一坏请求必报同一首错）：曲线缺失/
 * 总时长非法/采样步长非法/端口缺失/区间非法（逆序/越界/起点负）/速度
 * 非正/恒定带越域/Limited 携带非零带——逐项抛 TrajectoryError 且 token
 * 前缀 "trajectory/process-speed/"。
 */
TEST(TrjProcessSpeed, FailFastFamily_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    auto curve = std::make_shared<UniformCurve>(1.0, 0.05);
    GoldenFkPort port;
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // 违约注入辅助：跑请求、断言 TrajectoryError＋token 前缀。
    const auto expectViolation = [](const trj::ProcessSpeedCheckRequest& req,
                                    const char* what) {
        try {
            (void)trj::checkProcessSpeed(req);
            FAIL() << "应拒绝的请求被接受: " << what;
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token().find("trajectory/process-speed/"), 0U)
                << "token 前缀违约: " << e.token() << "（" << what << "）";
        }
    };

    // 1. 曲线缺失（无时间化产物无校验语义）。
    {
        trj::ProcessSpeedCheckRequest request = baseRequest(curve, &port, 1.0);
        request.timeCurve = nullptr;
        request.spans = {span(0U, 0.05, 0.95, limited(0.1))};
        expectViolation(request, "timeCurve 缺失");
    }
    // 2. 总时长非法（0/负/NaN）。
    for (double bad : {0.0, -1.0, nan}) {
        trj::ProcessSpeedCheckRequest request =
            baseRequest(curve, &port, bad);
        request.spans = {span(0U, 0.05, 0.95, limited(0.1))};
        expectViolation(request, "totalDurationS 非法");
    }
    // 3. 采样步长非法（0/负/NaN）。
    for (double bad : {0.0, -0.01, nan}) {
        trj::ProcessSpeedCheckRequest request =
            baseRequest(curve, &port, 1.0);
        request.sampleStepS = bad;
        request.spans = {span(0U, 0.05, 0.95, limited(0.1))};
        expectViolation(request, "sampleStepS 非法");
    }
    // 4. 端口缺失（spans 非空——未校验绝不冒充满足，KIN-05 家族）。
    {
        trj::ProcessSpeedCheckRequest request = baseRequest(curve, nullptr, 1.0);
        request.spans = {span(0U, 0.05, 0.95, limited(0.1))};
        expectViolation(request, "fkPort 缺失");
    }
    // 5. 区间非法（逆序/零长/起点负/终点越界/NaN）。
    {
        struct BadRange { double start, end; const char* what; };
        const BadRange cases[] = {
            {0.95, 0.05, "逆序区间"},
            {0.30, 0.30, "零长区间"},
            {-0.01, 0.50, "起点负"},
            {0.05, 1.01, "终点越界"},
            {nan, 0.50, "起点 NaN"},
        };
        for (const BadRange& c : cases) {
            trj::ProcessSpeedCheckRequest request =
                baseRequest(curve, &port, 1.0);
            request.spans = {span(0U, c.start, c.end, limited(0.1))};
            expectViolation(request, c.what);
        }
    }
    // 6. 速度规格值非法（0/负/NaN/inf）。
    for (double bad : {0.0, -0.1, nan, inf}) {
        trj::ProcessSpeedCheckRequest request =
            baseRequest(curve, &port, 1.0);
        request.spans = {span(0U, 0.05, 0.95, limited(bad))};
        expectViolation(request, "速度规格非法");
    }
    // 7. 恒定带越域（0/1/1.5/NaN——Constant 强制显式 ∈(0,1)）。
    for (double bad : {0.0, 1.0, 1.5, nan}) {
        trj::ProcessSpeedCheckRequest request =
            baseRequest(curve, &port, 1.0);
        request.spans = {span(0U, 0.05, 0.95, constantBand(0.05, bad))};
        expectViolation(request, "恒定带非法");
    }
    // 8. Limited 携带非零带（不消费面携带语义值＝违约——NFR-COR-03）。
    {
        trj::ProcessSpeedConstraint c = limited(0.1);
        c.constantBandRelative = 0.05;
        trj::ProcessSpeedCheckRequest request =
            baseRequest(curve, &port, 1.0);
        request.spans = {span(0U, 0.05, 0.95, c)};
        expectViolation(request, "Limited 携带非零恒定带");
    }
}

/**
 * 空受检段序列＝合法空结果（T09"空素材序列＝合法空结果"同口径）：
 * Ok 态、零素材、零观测——空段不要求 FK 端口（端口缺失仅在 spans 非空
 * 时违约的边界钉扎）。
 */
TEST(TrjProcessSpeed, EmptySpansLegalEmptyResult_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    auto curve = std::make_shared<UniformCurve>(1.0, 0.05);
    trj::ProcessSpeedCheckRequest request = baseRequest(curve, nullptr, 1.0);
    // spans 空＋端口空＝合法（零受检段无 FK 消费）。

    const trj::ProcessSpeedCheckResult result = trj::checkProcessSpeed(request);
    EXPECT_EQ(result.status, trj::ProcessSpeedStatus::Ok);
    EXPECT_TRUE(result.violationRecords.empty());
    EXPECT_TRUE(result.segmentObservations.empty());
}

// =====================================================================
// 确定性与多段观测（NFR-COR-02/§19.1"节拍分解细化"）
// =====================================================================

/**
 * 确定性重放（NFR-COR-02——同请求等价输出）：两次独立构造的同参数依赖
 * 链（曲线/端口/请求）各自完整存活到校验返回，结果全量相等（观测值等
 * ＋素材等——violated 场景；值等价面而非实例同一面）。
 */
TEST(TrjProcessSpeed, DeterministicReplayEquivalentOutput_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    // 两次独立构造（同参数——确定性对照的两个世界）。
    auto curveA = std::make_shared<SinusoidCurve>(1.0, 0.1, 1.0);
    GoldenFkPort portA;
    trj::ProcessSpeedCheckRequest requestA = baseRequest(curveA, &portA, 1.0);
    requestA.spans = {span(0U, 0.05, 0.95, constantBand(0.3, 0.10)),
                      span(1U, 0.95, 1.0, limited(2.0))};

    auto curveB = std::make_shared<SinusoidCurve>(1.0, 0.1, 1.0);
    GoldenFkPort portB;
    trj::ProcessSpeedCheckRequest requestB = baseRequest(curveB, &portB, 1.0);
    requestB.spans = {span(0U, 0.05, 0.95, constantBand(0.3, 0.10)),
                      span(1U, 0.95, 1.0, limited(2.0))};

    const trj::ProcessSpeedCheckResult first =
        trj::checkProcessSpeed(requestA);
    const trj::ProcessSpeedCheckResult second =
        trj::checkProcessSpeed(requestB);
    EXPECT_EQ(first.status, trj::ProcessSpeedStatus::Violated);
    EXPECT_EQ(first.status, second.status);
    ASSERT_EQ(first.violationRecords.size(),
              second.violationRecords.size());
    for (std::size_t i = 0; i < first.violationRecords.size(); ++i) {
        EXPECT_EQ(first.violationRecords[i], second.violationRecords[i])
            << "违例素材重放不一致（index " << i << "）";
    }
    ASSERT_EQ(first.segmentObservations.size(),
              second.segmentObservations.size());
    for (std::size_t i = 0; i < first.segmentObservations.size(); ++i) {
        EXPECT_EQ(first.segmentObservations[i], second.segmentObservations[i])
            << "段观测重放不一致（index " << i << "）";
    }
}

/**
 * 多段观测与 FK 消费纪律（§19.1"节拍分解细化"交接物——逐受检段时长与
 * 速度峰值可辨识；R-1 端口纪律的直穿计数观测）：两段不同区间对同一匀
 * 速曲线 ⇒ 观测逐段对齐且峰值一致（同曲线同速度）＋evaluateFk 调用数
 * ＝2×采样点总数（每采样点恰 2 次 FK——中心/单侧差分各取两个位置）。
 */
TEST(TrjProcessSpeed, MultiSpanObservationsAndPortDiscipline_WP16T15_ACC1)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    const double kTrueSpeed = 0.04;  // m/s
    auto curve = std::make_shared<UniformCurve>(1.0, kTrueSpeed);
    GoldenFkPort port;
    trj::ProcessSpeedCheckRequest request = baseRequest(curve, &port, 1.0);
    request.sampleStepS = 0.05;  // s——放大步长使采样计数可手算
    // 段 0：[0, 0.5] s（端点贴全局端点——t=0 单侧差分路径）；段 1：
    // [0.5, 1.0] s（末端单侧路径）。两段 divisions 各=10、各 11 采样点。
    request.spans = {span(0U, 0.0, 0.5, limited(0.1)),
                     span(1U, 0.5, 1.0, limited(0.1))};

    const trj::ProcessSpeedCheckResult result = trj::checkProcessSpeed(request);

    ASSERT_EQ(result.status, trj::ProcessSpeedStatus::Ok);
    ASSERT_EQ(result.segmentObservations.size(), 2U);
    // 逐段观测与 spans 同序对齐（节拍分解细化的数据面——区间＋峰值）。
    EXPECT_EQ(result.segmentObservations[0].segmentIndex, 0U);
    EXPECT_DOUBLE_EQ(result.segmentObservations[0].tStartS, 0.0);
    EXPECT_DOUBLE_EQ(result.segmentObservations[0].tEndS, 0.5);
    expectNearRel(result.segmentObservations[0].peakTcpSpeedMps, kTrueSpeed,
                  1e-12, "段 0 峰值应为解析真值");
    EXPECT_EQ(result.segmentObservations[1].segmentIndex, 1U);
    expectNearRel(result.segmentObservations[1].peakTcpSpeedMps, kTrueSpeed,
                  1e-12, "段 1 峰值应为解析真值");
    // FK 直穿计数：两段各 11 采样点 × 每点 2 次位置请求 = 44 次。
    EXPECT_EQ(port.fkCalls(), 2 * (11 + 11));
}
