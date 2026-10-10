/**
 * @file   SmoothTest.cpp
 * @brief  简化/平滑的运行期测试（WP-16-T07 平滑半区）——解析黄金算例
 *         （NFR-COR-01）＋四态/取消词表＋TCP 派生维门控＋FK 端口错误传
 *         播。零 policy 消费（平滑面零碰撞/阈值——§11.4 边界的测试面自
 *         证：本文件不 include 任何 policy/runtime 头、不需要 WorkCell 装
 *         置）；复检半区与编排闭环在 test/RecheckTest.cpp。
 *
 * 设计依据：
 *   - units/trajectory.md §11.1/§11.4/§15.7（简化/平滑模型与几何保持——
 *     贪心逐点剔除（保留端点与必经点）；五次样条重拟合端点强制不变；几
 *     何保持验证偏差≤双域容差，超容差作废）、§15.0（取消非错误/端口错
 *     误显性失败）、§17.2 V-14 平滑半区前置（平滑产物交复检）
 *   - 需求 TRJ-04（简化与平滑）、NFR-COR-01（解析对照——五次 Hermite 曲
 *     线与独立书写的解析公式逐点对照）、NFR-COR-02（λ 收缩序列确定性）、
 *     NFR-COR-03（非法输入拒绝）
 *   - 任务契约 tasks/foundation/WP-16-T07.json（acceptance 1 平滑半区；
 *     acceptance 3——平滑/复检两提交拆分的平滑侧测试）
 *
 * 本套件为**集成模式专属**（消费 rw::math::Q 构造面——PtpSequenceTest 同
 * 款 gating；冒烟模式不编译本文件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/KinematicsPort.hpp>
#include <sdurws/ird/trajectory/Smooth.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/math/Q.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace trj = sdurws::ird::trajectory;

namespace {

// =====================================================================
// 受控替身——FK 端口（TCP 派生维检查的解析装置；替身证明端口/错误传播，
// 不构成真实 FK 正确性证明——§17.1 替身纪律）
// =====================================================================

/**
 * @brief 解析 FK 替身：tcp(q) = (exp(q₀)·0.1, 0, 0) m——非线性映射，构造
 *        "关节维偏差在容差内、TCP 派生维超容差"的剔除判据分叉场景。
 */
class ExpTcpFkPort final : public trj::IKinematicsComputePort {
public:
    trj::IkPortReply solveIk(const trj::IkPortRequest&) override
    {
        ADD_FAILURE() << "平滑面不应消费 IK 半区";
        trj::IkPortReply reply;
        reply.status = trj::KinPortCallStatus::PortError;
        reply.errorToken = "trajectory/kin-port-unexpected";
        return reply;
    }
    trj::FkPortReply evaluateFk(const trj::FkPortRequest& request) override
    {
        trj::FkPortReply reply;
        if (request.cancel && request.cancel()) {
            reply.status = trj::KinPortCallStatus::Canceled;
            return reply;
        }
        reply.status = trj::KinPortCallStatus::Ok;
        const double q0 = request.q.empty() ? 0.0 : request.q.front();
        reply.metrics.tcpInBase = rw::math::Transform3D<double>(
            rw::math::Vector3D<double>(0.1 * std::exp(q0), 0.0, 0.0));
        return reply;
    }
};

/**
 * @brief 端口错误替身：evaluateFk 恒 PortError——错误传播断言面（§15.0
 *        "端口层错误显性失败，不吞错"）。
 */
class FailingFkPort final : public trj::IKinematicsComputePort {
public:
    trj::IkPortReply solveIk(const trj::IkPortRequest&) override
    {
        ADD_FAILURE() << "平滑面不应消费 IK 半区";
        trj::IkPortReply reply;
        reply.status = trj::KinPortCallStatus::PortError;
        reply.errorToken = "trajectory/kin-port-unexpected";
        return reply;
    }
    trj::FkPortReply evaluateFk(const trj::FkPortRequest&) override
    {
        trj::FkPortReply reply;
        reply.status = trj::KinPortCallStatus::PortError;
        reply.errorToken = "trajectory/kin-port-failed";
        reply.errorMessage = "注入的端口错误（测试）";
        return reply;
    }
};

// =====================================================================
// 解析参照——五次 Hermite 曲线的独立公式（黄金算例对照基准；与实现文件
// 无共享代码——测试侧独立书写，NFR-COR-01 解析对照纪律）
// =====================================================================

/// 值基 h000（与 Smooth.cpp 同一数学对象、独立书写——对照的价值所在）。
inline double refH000(double t) { return 1.0 - 10.0 * t * t * t + 15.0 * t * t * t * t - 6.0 * t * t * t * t * t; }
/// 导数基 h100。
inline double refH100(double t) { return t - 6.0 * t * t * t + 8.0 * t * t * t * t - 3.0 * t * t * t * t * t; }
/// 值基 h001。
inline double refH001(double t) { return 10.0 * t * t * t - 15.0 * t * t * t * t + 6.0 * t * t * t * t * t; }
/// 导数基 h101。
inline double refH101(double t) { return -4.0 * t * t * t + 7.0 * t * t * t * t - 3.0 * t * t * t * t * t; }

/**
 * @brief 平滑曲线的解析参照求值（三点单轴算例 q=[0, 0.1, 0.3]、收缩 λ）。
 *
 * 段 k∈{0,1}：Δ₀=0.1、Δ₁=0.2；全局导数 d₀=Δ₀、d₁=(Δ₀+Δ₁)/2=0.15、d₂=Δ₁。
 * 段切线（收缩语义——m=Δ+λ·(d−Δ)）：段 0 m₀=0.1（恒）、m₁=0.1+0.05λ；
 * 段 1 m₀=0.2−0.05λ、m₁=0.2（恒）。解析偏差：段 0 D(t)=0.05λ·h101(t)
 * （峰 |h101|max=16/81 于 t=2/3）；段 1 D(t)=−0.05λ·h100(t)（峰 ≈0.197）。
 */
double refSampleThreePoint(double s, double lambda)
{
    const double knots[3] = {0.0, 0.1, 0.3};
    const double u = s * 2.0;
    std::size_t k = static_cast<std::size_t>(u);
    if (k >= 2) { k = 1; }
    const double t = (k == 1 && s >= 1.0) ? 1.0 : u - static_cast<double>(k);
    const double p0 = knots[k];
    const double p1 = knots[k + 1];
    const double delta = p1 - p0;  // 段斜率（0.1 或 0.2）
    double m0;
    double m1;
    if (k == 0) {
        m0 = delta;                            // d₀=Δ₀——端点导数（收缩无效差）
        m1 = delta + lambda * (0.15 - delta);  // d₁=0.15 中心差分
    } else {
        m0 = delta + lambda * (0.15 - delta);
        m1 = delta;                            // d₂=Δ₁——端点导数
    }
    return refH000(t) * p0 + refH100(t) * m0 + refH001(t) * p1 + refH101(t) * m1;
}

}  // namespace

// =====================================================================
// 平滑半区（§15.7——解析黄金算例＋四态词表＋TCP 派生维）
// =====================================================================

/**
 * 平滑解析黄金算例（NFR-COR-01）：三点单轴 q=[0,0.1,0.3]、tol=0.001、
 * 迭代上限 5——λ 序列 1→1/2→1/4→1/8→1/16，解析偏差峰
 * trueMax(λ)=λ·0.05·16/81≈0.009877λ（段 0：0.05λ·max|h101|；段 1 同量级），
 * λ=1/8 时 0.00123>0.001 仍超、λ=1/16 时 0.000617≤0.001 达标→Smoothed。
 * 断言：①产物曲线与独立解析公式逐点对照（位级 1e-12）；②偏差报告 ∈
 * [0.9·解析峰, 解析峰]（64 探针欠估 <10%）；③端点位级还原；④结点插值性。
 */
TEST(SmoothRecheckTest, SmoothAnalyticGoldenCase_WP16T07_NFRCOR01)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04", "NFR-COR-01"},
                  std::vector<std::string>{"AT-06"});

    trj::SmoothRequest request;
    request.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.1),
                         rw::math::Q(1, 0.3)};
    request.smoothToleranceJoint = 0.001;
    request.smoothToleranceTcp = 0.001;
    request.maxIterations = 5;  // λ 序列 1, 1/2, 1/4, 1/8, 1/16
    request.fkPort = nullptr;

    const trj::SmoothOutcome outcome = trj::smooth(request);
    ASSERT_EQ(outcome.status, trj::SmoothStatus::Smoothed);
    EXPECT_DOUBLE_EQ(outcome.deviation.adoptedShrinkFactor, 1.0 / 16.0);

    // 解析峰（λ=1/16）：trueMax = (1/16)·0.05·(16/81)。
    const double trueMax = (1.0 / 16.0) * 0.05 * (16.0 / 81.0);
    // ②偏差报告（64 探针网格）欠估但接近：[0.9·trueMax, trueMax]。
    EXPECT_GT(outcome.deviation.jointAxisMaxDeviation, 0.9 * trueMax);
    EXPECT_LE(outcome.deviation.jointAxisMaxDeviation, trueMax);

    // ①产物曲线 vs 独立解析公式（密集网格逐点——位级 1e-12）。
    ASSERT_NE(outcome.geometry, nullptr);
    const double lambda = outcome.deviation.adoptedShrinkFactor;
    for (int i = 0; i <= 400; ++i) {
        const double s = static_cast<double>(i) / 400.0;
        const rw::math::Q sampled = outcome.geometry->sampleAt(s);
        ASSERT_EQ(sampled.size(), 1U);
        EXPECT_NEAR(sampled[0], refSampleThreePoint(s, lambda), 1e-12)
            << "s=" << s;
    }
    // ③端点位级还原（端点强制不变——硬条件）。
    EXPECT_EQ(outcome.geometry->sampleAt(0.0)[0], 0.0);
    EXPECT_EQ(outcome.geometry->sampleAt(1.0)[0], 0.3);
    // ④结点插值性（s=i/(n-1) 处恒等于第 i 路点）。
    EXPECT_EQ(outcome.geometry->sampleAt(0.5)[0], 0.1);
}

/**
 * 零变化分支（§11.1"几何或采样是否变化？——否——沿用既有复检结论"）：
 * 共线路径的样条线性重构恒等式→偏差位级 0→NoChange（零复检）。
 */
TEST(SmoothRecheckTest, SmoothCollinearIsNoChange_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    trj::SmoothRequest request;
    request.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.05),
                         rw::math::Q(1, 0.1)};
    request.smoothToleranceJoint = 0.01;
    request.smoothToleranceTcp = 0.01;
    request.maxIterations = 2;

    const trj::SmoothOutcome outcome = trj::smooth(request);
    EXPECT_EQ(outcome.status, trj::SmoothStatus::NoChange);
    EXPECT_EQ(outcome.geometry, nullptr);  // 零变化——无可复检新几何
    EXPECT_EQ(outcome.deviation.jointAxisMaxDeviation, 0.0);
}

/**
 * 两点路径（无内部点）→NoChange；共线五点剔除三内部点→路径缩为两端点
 * （§11.4 简化——贪心逐点剔除）。
 */
TEST(SmoothRecheckTest, SmoothSimplifyDropsRedundant_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    // 两点——无平滑对象。
    trj::SmoothRequest two;
    two.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.1)};
    two.smoothToleranceJoint = 0.01;
    two.smoothToleranceTcp = 0.01;
    two.maxIterations = 1;
    EXPECT_EQ(trj::smooth(two).status, trj::SmoothStatus::NoChange);

    // 共线五点——三个内部点全部可剔（替代线性插值偏差 0 ≤ 容差）。
    trj::SmoothRequest five;
    five.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.025),
                      rw::math::Q(1, 0.05), rw::math::Q(1, 0.075),
                      rw::math::Q(1, 0.1)};
    five.smoothToleranceJoint = 0.01;
    five.smoothToleranceTcp = 0.01;
    five.maxIterations = 1;
    const trj::SmoothOutcome outcome = trj::smooth(five);
    EXPECT_EQ(outcome.status, trj::SmoothStatus::NoChange);  // 剔尽后仅剩端点
    ASSERT_EQ(outcome.smoothPath.size(), 2U);
    EXPECT_EQ(outcome.smoothPath.front()[0], 0.0);
    EXPECT_EQ(outcome.smoothPath.back()[0], 0.1);
}

/**
 * 必经点保护（§5.6 必经状态——Via 才可剔）：偏折点标记必经后不可剔除；
 * 同一几何未标记时被剔除。
 */
TEST(SmoothRecheckTest, SmoothSimplifyRespectsMandatory_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    // 偏折点 q1=0.13（连线中点 0.15——偏差 0.02 在容差 0.05 内，本可剔）。
    trj::SmoothRequest request;
    request.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.13),
                         rw::math::Q(1, 0.3)};
    request.mandatoryIndices = {1};  // 中间点必经——保护
    request.smoothToleranceJoint = 0.05;
    request.smoothToleranceTcp = 0.05;
    request.maxIterations = 1;
    const trj::SmoothOutcome outcome = trj::smooth(request);
    // 必经保护→路径保留 3 点（剔除被拒；平滑照常执行）。
    ASSERT_EQ(outcome.smoothPath.size(), 3U);

    // 同一几何去掉必经标记——剔除生效（缩为 2 点→NoChange）。
    trj::SmoothRequest unmarked = request;
    unmarked.mandatoryIndices.clear();
    const trj::SmoothOutcome dropped = trj::smooth(unmarked);
    EXPECT_EQ(dropped.smoothPath.size(), 2U);
}

/**
 * 作废分支（§11.4"偏差超容差→该次平滑作废"）：单次尝试（maxIterations=
 * 1）超容差→ToleranceViolated；产物＝简化后路径；素材比较型三要素齐备。
 */
TEST(SmoothRecheckTest, SmoothToleranceViolatedSingleAttempt_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{"AT-06"});

    trj::SmoothRequest request;
    request.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.1),
                         rw::math::Q(1, 0.3)};
    request.smoothToleranceJoint = 0.001;
    request.smoothToleranceTcp = 0.001;
    request.maxIterations = 1;  // 仅 λ=1 一次——超差即作废
    const trj::SmoothOutcome outcome = trj::smooth(request);
    ASSERT_EQ(outcome.status, trj::SmoothStatus::ToleranceViolated);
    EXPECT_EQ(outcome.geometry, nullptr);
    ASSERT_EQ(outcome.smoothPath.size(), 3U);  // 产物＝原路径（平滑弃用）
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->phaseToken, std::string(trj::kPhaseSmooth));
    EXPECT_EQ(outcome.failure->reasonToken, std::string(trj::kTrjLimitExceeded));
    ASSERT_TRUE(outcome.failure->comparison.has_value());
    // 比较型三要素：实际偏差（解析峰 0.05·16/81≈0.009877 量级）对照容差。
    EXPECT_NEAR(outcome.failure->comparison->actual.quantity.value(),
                0.05 * (16.0 / 81.0), 5e-4);
    EXPECT_NEAR(outcome.failure->comparison->expected.quantity.value(), 0.001, 1e-12);
}

/**
 * 重试放弃分支（§11.3 连续平滑失败的迭代上限语义面）：maxIterations=4
 * （λ 序列 1..1/8 全超差）→GiveUpAfterRetries。
 */
TEST(SmoothRecheckTest, SmoothGiveUpAfterRetries_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    trj::SmoothRequest request;
    request.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.1),
                         rw::math::Q(1, 0.3)};
    request.smoothToleranceJoint = 0.001;
    request.smoothToleranceTcp = 0.001;
    request.maxIterations = 4;  // λ=1/8 偏差≈0.00123 仍>0.001——序列耗尽
    const trj::SmoothOutcome outcome = trj::smooth(request);
    ASSERT_EQ(outcome.status, trj::SmoothStatus::GiveUpAfterRetries);
    EXPECT_EQ(outcome.geometry, nullptr);
    ASSERT_TRUE(outcome.failure.has_value());
}

/**
 * 取消分支（UX-03——取消不是错误）：入口取消观测命中→Canceled＋零素材
 * （smoothPath 空/geometry 空/failure 空）。
 */
TEST(SmoothRecheckTest, SmoothCanceledZeroMaterial_WP16T07_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{"AT-34"});

    trj::SmoothRequest request;
    request.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.1),
                         rw::math::Q(1, 0.3)};
    request.smoothToleranceJoint = 0.01;
    request.smoothToleranceTcp = 0.01;
    request.maxIterations = 1;
    request.cancel = []() { return true; };
    const trj::SmoothOutcome outcome = trj::smooth(request);
    EXPECT_EQ(outcome.status, trj::SmoothStatus::Canceled);
    EXPECT_TRUE(outcome.smoothPath.empty());
    EXPECT_EQ(outcome.geometry, nullptr);
    EXPECT_FALSE(outcome.failure.has_value());  // 零错误素材（UX-03）
}

/**
 * TCP 派生维剔除判据（§11.4 双域判据的 TCP 半区）：非线性 FK 替身下三点
 * 的关节维偏差在容差内（本可剔），TCP 维偏差 ≈1.06×10⁻³ m 超 1×10⁻⁴ 容
 * 差→剔除被拒；FK 端口缺席时同一路径被剔（TCP 检查 NotApplicable 不阻
 * 塞）。
 */
TEST(SmoothRecheckTest, SmoothTcpDeviationGatesSimplify_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    ExpTcpFkPort fkPort;
    trj::SmoothRequest request;
    // 非共线三点：被剔点 0.06 与替代中点 0.05 的关节偏差 0.01（容差内），
    // TCP 偏差 |0.1·e^0.06 − 0.1·e^0.05| ≈ 1.06×10⁻³ m（超 1×10⁻⁴ 容差）。
    request.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.06),
                         rw::math::Q(1, 0.1)};
    request.smoothToleranceJoint = 0.01;   // 关节维偏差 0.01——通过（≤ 判据）
    request.smoothToleranceTcp = 0.0001;   // TCP 维偏差 ≈1.06e-3——拒绝
    request.maxIterations = 4;  // λ 收缩序列——TCP 界在 λ=1/4 时达标（平滑成功）
    request.fkPort = &fkPort;
    const trj::SmoothOutcome gated = trj::smooth(request);
    // TCP 门控→剔除被拒→3 点保留（平滑照常执行——非共线三点偏差在容差
    // 内，Smoothed）。
    ASSERT_EQ(gated.status, trj::SmoothStatus::Smoothed);
    ASSERT_EQ(gated.smoothPath.size(), 3U);

    // FK 缺席——TCP 检查 NotApplicable→剔除生效（缩为 2 点）。
    trj::SmoothRequest noFk = request;
    noFk.fkPort = nullptr;
    const trj::SmoothOutcome dropped = trj::smooth(noFk);
    EXPECT_EQ(dropped.smoothPath.size(), 2U);
    EXPECT_FALSE(dropped.deviation.tcpChecked);
}

/**
 * FK 端口错误传播（§15.0"端口层失败不吞错"）：PortError→TrajectoryError
 * token "trajectory/smooth/fk-port"。
 */
TEST(SmoothRecheckTest, SmoothFkPortErrorPropagates_WP16T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    FailingFkPort fkPort;
    trj::SmoothRequest request;
    request.waypoints = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.13),
                         rw::math::Q(1, 0.3)};
    request.smoothToleranceJoint = 0.05;
    request.smoothToleranceTcp = 0.05;
    request.maxIterations = 1;
    request.fkPort = &fkPort;
    try {
        (void)trj::smooth(request);
        FAIL() << "端口错误应显性失败";
    } catch (const trj::TrajectoryError& e) {
        EXPECT_EQ(e.token(), "trajectory/smooth/fk-port");
    }
}

/**
 * 折线求值器工厂（公共接口消费路径——IPathGeometry 交付面）：结点插值性
 * s=i/(n-1) 位级还原＋非有限/越界参数 fail-fast（NFR-COR-03）。
 */
TEST(SmoothRecheckTest, LinearGeometryFactoryContract_WP16T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    const std::vector<rw::math::Q> waypoints = {rw::math::Q(1, 0.0),
                                                rw::math::Q(1, 0.5),
                                                rw::math::Q(1, 1.0)};
    const auto geometry = trj::makeLinearJointPathGeometry(waypoints);
    EXPECT_EQ(geometry->sampleAt(0.0)[0], 0.0);
    EXPECT_EQ(geometry->sampleAt(0.5)[0], 0.5);   // 结点插值
    EXPECT_EQ(geometry->sampleAt(1.0)[0], 1.0);
    EXPECT_NEAR(geometry->sampleAt(0.25)[0], 0.25, 1e-15);  // 段内线性
    // 单点路数 <2 拒绝；越界参数拒绝。
    EXPECT_THROW(trj::makeLinearJointPathGeometry({rw::math::Q(1, 0.0)}),
                 trj::TrajectoryError);
    EXPECT_THROW(geometry->sampleAt(-0.1), trj::TrajectoryError);
    EXPECT_THROW(geometry->sampleAt(1.1), trj::TrajectoryError);
}
