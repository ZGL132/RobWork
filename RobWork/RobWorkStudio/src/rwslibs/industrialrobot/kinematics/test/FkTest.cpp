/**
 * @file   FkTest.cpp
 * @brief  FK 与位姿指标用例组（KinFk）——解析黄金算例（附录 D 第 4/9 项
 *         容差）、基座—世界唯一来源（V-02 kinematics 侧）、非法输入
 *         fail-fast（V-03 前半/NFR-COR-03）、超限位两分语义与确定性
 *         字节输出（NFR-COR-01）。
 *
 * 设计依据：
 *   - units/kinematics.md §5.1/§5.2（FK 契约——D-KIN-2 统一尺度规则）、
 *     §9.2 IFkEvaluator（@pre/@错误 行）、§10.1/§10.2（V-01/V-02/V-03——
 *     本单元侧承载面；黄金全量数据集随 T13 收口，本文件落解析算例先行批）
 *   - REQUIREMENTS 附录 D 第 4 项（FK 等价 1×10⁻⁹ m/rad）、第 9 项
 *     （解析算例/独立参考实现：标量相对 1×10⁻⁹＋逐例 ε_abs 声明——
 *     夹具 kGoldenRel/kGoldenAbs/kPoseTol 即容差档案声明的测试面）
 *   - 任务契约 tasks/foundation/WP-15-T03.json acceptance 1/2/3
 *
 * 测试替身边界声明（R-KIN-1 处置口径）：TestView 为宿主注入视图的测试
 * 替身（IKinRuntimeView 最小实现，值持有规范模型）；黄金对照全部针对
 * 真实产品代码（FkEvaluator）——替身只承载"注入"语义，不伪造任何计算。
 */

#include "KinFkFixture.hpp"

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
using sdurws::ird::kinematics::FkEvaluator;
using sdurws::ird::kinematics::KinematicsError;
using sdurws::ird::kinematics::KinematicsErrorCode;
using sdurws::ird::kinematics::PoseMetrics;
using sdurws::ird::kinematics::TcpRef;
using sdurws::ird::kinematics::encodePoseMetricsCanonical;

namespace {

/// TCP 引用便捷构造（canonical TCP——tcpKey 空串）。
TcpRef tcpRef()
{
    TcpRef r;
    r.toolObject = idFrom<core::ObjectId>("kin-tool");
    r.tcpKey.clear();
    return r;
}

/// 取默认 TCP 的工具 localName（非空键匹配用）。
std::string toolName()
{
    return "tool_1";
}

/// 评估便捷封装：成功则返回指标，失败则 GTEST 失败并返回 false
/// （辅助函数非 void——不能用 ASSERT；以 ADD_FAILURE＋false 兜底）。
bool evaluateOk(const rt::CanonicalModel& model,
                const std::vector<double>& q,
                PoseMetrics& out)
{
    TestView view(model);
    const FkEvaluator fk;
    auto r = fk.evaluate(view, tcpRef(), q);
    if (!r.ok()) {
        ADD_FAILURE() << "评估意外失败: " << r.error().detail;
        return false;
    }
    out = r.get();
    return true;
}

}  // namespace

// =====================================================================
// acceptance 1——解析黄金算例（V-01 先行批；附录 D 第 4/9 项容差）
// =====================================================================

/// 平面二连杆零位：TCP 位姿解析值 (L1+L2, 0, 0)＋旋转恒等（第 4 项）。
TEST(KinFk, PlanarTwoLinkZeroPoseAnalytic_WP15T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01", "NFR-COR-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    PoseMetrics m;
    ASSERT_TRUE(evaluateOk(model, {0.0, 0.0}, m));

    EXPECT_NEAR(m.tcpInBase.P()[0], 1.1 + 0.7, kPoseTol);
    EXPECT_NEAR(m.tcpInBase.P()[1], 0.0, kPoseTol);
    EXPECT_NEAR(m.tcpInBase.P()[2], 0.0, kPoseTol);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            EXPECT_NEAR(m.tcpInBase.R()(i, j), identityR()(i, j), kPoseTol)
                << "旋转元素 (" << i << "," << j << ")";
        }
    }
}

/// 平面二连杆 q=(π/2, π/2)：TCP (−L2, L1, 0)＋R=Rz(π)——解析三角黄金值。
TEST(KinFk, PlanarTwoLinkQuarterPoseAnalytic_WP15T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{"AT-03"});

    constexpr double pi = 3.14159265358979323846;
    const rt::CanonicalModel model = twoLinkModel();
    PoseMetrics m;
    ASSERT_TRUE(evaluateOk(model, {pi / 2.0, pi / 2.0}, m));

    // p_tcp＝(L1·cos q1＋L2·cos(q1+q2), L1·sin q1＋L2·sin(q1+q2), 0)。
    EXPECT_NEAR(m.tcpInBase.P()[0], 0.0 - 0.7, kPoseTol);
    EXPECT_NEAR(m.tcpInBase.P()[1], 1.1, kPoseTol);
    EXPECT_NEAR(m.tcpInBase.P()[2], 0.0, kPoseTol);
    // 旋转＝Rz(q1+q2)＝Rz(π)。
    const rw::math::Rotation3D<double> rzPi = rotZ(pi);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            EXPECT_NEAR(m.tcpInBase.R()(i, j), rzPi(i, j), kPoseTol);
        }
    }
}

/// 平面二连杆雅可比解析对照（零位与 π/2 位两配置；第 9 项独立参考）。
TEST(KinFk, PlanarTwoLinkJacobianAnalytic_WP15T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{"AT-03"});

    constexpr double pi = 3.14159265358979323846;
    const rt::CanonicalModel model = twoLinkModel();

    // 零位：col1=((0, L1+L2, 0),(0,0,1))，col2=((0, L2, 0),(0,0,1))。
    {
        PoseMetrics m;
        ASSERT_TRUE(evaluateOk(model, {0.0, 0.0}, m));
        ASSERT_EQ(m.jacobian.size(), 12U);
        EXPECT_NEAR(m.jacobian[0 * 2 + 0], 0.0, kPoseTol);   // Jv1x
        EXPECT_NEAR(m.jacobian[1 * 2 + 0], 1.8, kPoseTol);   // Jv1y＝L1+L2
        EXPECT_NEAR(m.jacobian[2 * 2 + 0], 0.0, kPoseTol);   // Jv1z
        EXPECT_NEAR(m.jacobian[3 * 2 + 0], 0.0, kPoseTol);   // Jw1x
        EXPECT_NEAR(m.jacobian[4 * 2 + 0], 0.0, kPoseTol);
        EXPECT_NEAR(m.jacobian[5 * 2 + 0], 1.0, kPoseTol);   // Jw1z
        EXPECT_NEAR(m.jacobian[0 * 2 + 1], 0.0, kPoseTol);   // Jv2x
        EXPECT_NEAR(m.jacobian[1 * 2 + 1], 0.7, kPoseTol);   // Jv2y＝L2
        EXPECT_NEAR(m.jacobian[5 * 2 + 1], 1.0, kPoseTol);   // Jw2z
    }
    // π/2 位：与独立参考实现逐元素对照（参考＝夹具 4×4 直算路径）。
    {
        PoseMetrics m;
        ASSERT_TRUE(evaluateOk(model, {pi / 2.0, pi / 2.0}, m));
        const RefChainResult ref = referenceFk(model, {pi / 2.0, pi / 2.0});
        ASSERT_EQ(ref.jacobian6xN.size(), m.jacobian.size());
        for (std::size_t k = 0; k < m.jacobian.size(); ++k) {
            expectGoldenNear(m.jacobian[k], ref.jacobian6xN[k], "雅可比元素");
        }
    }
}

/// 转动＋移动正交列算例：奇异值全解析 σ＝{√(L²+1), 1}、条件数＝√(L²+1)
/// （D-KIN-2 σmax/σmin 的解析锚）；n<6 → w＝√det(JJᵀ)=0。
TEST(KinFk, RevPrismSingularValuesAnalytic_WP15T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01", "NFR-COR-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = revPrismModel();
    PoseMetrics m;
    ASSERT_TRUE(evaluateOk(model, {0.0, 0.0}, m));

    // 零位列正交：col1=((0,L,0),(0,0,1))、col2=((1,0,0),(0,0,0))。
    ASSERT_EQ(m.singularValues.size(), 2U);
    EXPECT_NEAR(m.singularValues[0], std::sqrt(1.3 * 1.3 + 1.0), kPoseTol);
    EXPECT_NEAR(m.singularValues[1], 1.0, kPoseTol);
    // 降序（Eigen 契约——奇异值不增）。
    EXPECT_GE(m.singularValues[0], m.singularValues[1]);
    // 条件数＝σmax/σmin（有限——D-KIN-2；isFinite 标记为真）。
    EXPECT_TRUE(m.conditionNumberIsFinite);
    expectGoldenNear(m.conditionNumber, std::sqrt(1.3 * 1.3 + 1.0) / 1.0, "条件数");
    // n<6：det(J·Jᵀ)=0（行秩不足）→ w=0——统一尺度规则的退化分支。
    EXPECT_EQ(m.manipulability, 0.0);
}

/// 六轴臂独立参考实现对照（附录 D 第 9 项"独立参考实现"类别）：位姿
/// 逐元素＋雅可比逐元素＋奇异值恒等式（Σσ²＝‖J‖_F²、∏σ＝√det(JJᵀ)=w）。
TEST(KinFk, SixAxisAgainstIndependentReference_WP15T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01", "NFR-COR-01"},
                  std::vector<std::string>{"AT-03"});

    const std::vector<double> q = {0.3, -0.5, 0.8, 0.2, -0.4, 1.0};
    const rt::CanonicalModel model = sixAxisModel();
    PoseMetrics m;
    ASSERT_TRUE(evaluateOk(model, q, m));

    const RefChainResult ref = referenceFk(model, q);

    // 位姿逐元素（第 4 项容差）。
    EXPECT_NEAR(m.tcpInBase.P()[0], ref.tcp[0][3], kPoseTol);
    EXPECT_NEAR(m.tcpInBase.P()[1], ref.tcp[1][3], kPoseTol);
    EXPECT_NEAR(m.tcpInBase.P()[2], ref.tcp[2][3], kPoseTol);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            EXPECT_NEAR(m.tcpInBase.R()(i, j), ref.tcp[i][j], kPoseTol)
                << "旋转元素 (" << i << "," << j << ")";
        }
    }
    // 雅可比逐元素（行优先 6×6）。
    ASSERT_EQ(m.jacobian.size(), 36U);
    for (std::size_t k = 0; k < 36U; ++k) {
        expectGoldenNear(m.jacobian[k], ref.jacobian6xN[k], "雅可比元素");
    }

    // 奇异值恒等式（独立参考 J 的不变量）：
    //   Σσᵢ² ＝ ‖J‖_F²（Frobenius 定理）；∏σᵢ² ＝ det(J·Jᵀ) → w＝∏σᵢ。
    ASSERT_EQ(m.singularValues.size(), 6U);
    double frobenius2 = 0.0;
    for (double v : ref.jacobian6xN) { frobenius2 += v * v; }
    double sv2sum = 0.0;
    double svProduct = 1.0;
    for (std::size_t i = 0; i < m.singularValues.size(); ++i) {
        sv2sum += m.singularValues[i] * m.singularValues[i];
        svProduct *= m.singularValues[i];
        if (i > 0) {
            EXPECT_GE(m.singularValues[i - 1], m.singularValues[i]) << "降序";
        }
    }
    expectGoldenNear(sv2sum, frobenius2, "Σσ²＝‖J‖_F²");
    expectGoldenNear(svProduct, m.manipulability, "w＝∏σ（n=6 恒等式）");
    // 条件数＝σmax/σmin（良态配置——有限）。
    EXPECT_TRUE(m.conditionNumberIsFinite);
    expectGoldenNear(m.conditionNumber,
                     m.singularValues.front() / m.singularValues.back(), "条件数");
}

/// dof=0（全固定链）边界：刚体位姿可算＋空指标面＋条件数按奇异口径
/// （+∞/isFinite=false——空奇异值集的 D-KIN-2 承载）。
TEST(KinFk, AllFixedChainDegenerateMetrics_WP15T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = allFixedModel();
    PoseMetrics m;
    ASSERT_TRUE(evaluateOk(model, {}, m));

    // 刚体位姿＝∏origin·tcpOffset＝(0,0,0.9)+(0,0,0.1)。
    EXPECT_NEAR(m.tcpInBase.P()[2], 1.0, kPoseTol);
    EXPECT_TRUE(m.jacobian.empty());
    EXPECT_TRUE(m.singularValues.empty());
    EXPECT_FALSE(m.conditionNumberIsFinite);
    EXPECT_EQ(m.conditionNumber, std::numeric_limits<double>::infinity());
    EXPECT_EQ(m.manipulability, 0.0);
    EXPECT_TRUE(m.jointMargins.empty());
    EXPECT_EQ(m.minimumJointMargin, std::numeric_limits<double>::infinity());
}

// =====================================================================
// acceptance 2——基座—世界唯一来源（V-02 kinematics 侧输入/输出面）
// =====================================================================

/// 倒挂安装：tcpInWorld＝T_world_base·tcpInBase（与独立 4×4 复合对照）；
/// 重力面不在 T03 输出面（结构性满足"仅经 gravityBase"——AT-37 观测点
/// 的位姿侧承载；重力消费随 dynamics 域任务走 view.gravityBase()）。
TEST(KinFk, BaseWorldInvertedMount_V02_WP15T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22", "MDL-06"},
                  std::vector<std::string>{"AT-37"});

    // 倒挂安装：基座上移 1.2 m＋绕 X 翻转 180°（T_world_base 唯一存储）。
    const rw::math::Transform3D<double> inverted(
        rw::math::Vector3D<double>(0.5, 0, 1.2),
        rw::math::Rotation3D<double>(1, 0, 0, 0, -1, 0, 0, 0, -1));
    constexpr double pi = 3.14159265358979323846;
    // 与黄金算例 A 同几何，仅 world 块为倒挂安装（makeModel world 参数）。
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -2.97, 2.97));
    joints.push_back(revolute("j2", rw::math::Vector3D<double>(0, 0, 1),
                              trans(1.1, 0, 0), -pi, pi));
    const rt::CanonicalModel model = makeModel(joints, trans(0.7, 0, 0), inverted);

    TestView view(model);
    const FkEvaluator fk;
    const auto r = fk.evaluate(view, tcpRef(), {0.3, -0.8});
    ASSERT_TRUE(r.ok());
    const PoseMetrics& m = r.get();

    // 独立参考：tcpInWorld_ref＝T_world_base·tcpInBase（4×4 直算）。
    Mat4 twb = identity4();
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            twb[i][j] = inverted.R()(i, j);
        }
        twb[i][3] = inverted.P()[i];
    }
    Mat4 tbt = identity4();
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            tbt[i][j] = m.tcpInBase.R()(i, j);
        }
        tbt[i][3] = m.tcpInBase.P()[i];
    }
    const Mat4 twt = mul4(twb, tbt);
    EXPECT_NEAR(m.tcpInWorld.P()[0], twt[0][3], kPoseTol);
    EXPECT_NEAR(m.tcpInWorld.P()[1], twt[1][3], kPoseTol);
    EXPECT_NEAR(m.tcpInWorld.P()[2], twt[2][3], kPoseTol);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            EXPECT_NEAR(m.tcpInWorld.R()(i, j), twt[i][j], kPoseTol);
        }
    }
    // 倒挂下世界系与基座系位姿必须实际不同（防"二次旋转"空转的对照面
    // ——若实现错误地返回 tcpInBase，倒挂旋转差不会被 1×10⁻⁹ 容差吸收）。
    EXPECT_NEAR(m.tcpInWorld.P()[2], 1.2 - m.tcpInBase.P()[2], 1e-6);
}

/// 权威关节角：FK 消费 q 为权威值（q_authoritative＝q_zeroOffset＋q_rw，
/// §5.1）——内部不叠加 zeroOffset（同几何双模型字节级同输出）。
TEST(KinFk, AuthoritativeQZeroOffsetNotAdded_WP15T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-09"},
                  std::vector<std::string>{});

    // 双模型：几何完全一致，仅 j2.zeroOffset 0 → 0.5。
    auto build = [](double zeroOffset) {
        std::vector<rt::CanonicalJoint> joints;
        joints.push_back(revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                                  trans(0, 0, 0), -2.97, 2.97));
        rt::CanonicalJoint j2 = revolute("j2", rw::math::Vector3D<double>(0, 0, 1),
                                         trans(1.1, 0, 0), -3.14, 3.14);
        j2.zeroOffset = zeroOffset;  // rad——仅建模层权威换算用（§5.1）
        joints.push_back(j2);
        return makeModel(joints, trans(0.7, 0, 0));
    };
    const rt::CanonicalModel withOffset = build(0.5);
    const rt::CanonicalModel withoutOffset = build(0.0);

    TestView v1(withOffset);
    TestView v2(withoutOffset);
    const FkEvaluator fk;
    const std::vector<double> q = {0.3, 0.25};  // 权威 q——两模型同输入
    const auto r1 = fk.evaluate(v1, tcpRef(), q);
    const auto r2 = fk.evaluate(v2, tcpRef(), q);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    // zeroOffset 不参与 FK 计算（q 即权威值）——字节级一致。
    EXPECT_EQ(encodePoseMetricsCanonical(r1.get()),
              encodePoseMetricsCanonical(r2.get()));
}

// =====================================================================
// acceptance 3——非法输入 fail-fast＋结构化错误素材＋两分语义
// =====================================================================

/// q 维度不符：IllegalQ 拒绝（params expected-dof/actual-dof；不钳制）。
TEST(KinFk, IllegalQDimensionRejected_WP15T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const FkEvaluator fk;
    const auto r = fk.evaluate(view, tcpRef(), {0.1, 0.2, 0.3});  // 3≠2
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, KinematicsErrorCode::IllegalQ);
    // params 键面＝T03 生产者登记（expected-dof/actual-dof——保序）。
    ASSERT_EQ(r.error().params.size(), 2U);
    EXPECT_EQ(r.error().params[0].first, "expected-dof");
    EXPECT_EQ(r.error().params[0].second, "2");
    EXPECT_EQ(r.error().params[1].first, "actual-dof");
    EXPECT_EQ(r.error().params[1].second, "3");
}

/// q 含 NaN：IllegalQ 拒绝（params nonfinite-index 定位首违例；不置零
/// ——NFR-COR-03 的字面拒绝面）。
TEST(KinFk, IllegalQNonFiniteRejected_WP15T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const FkEvaluator fk;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto r = fk.evaluate(view, tcpRef(), {0.1, nan});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, KinematicsErrorCode::IllegalQ);
    ASSERT_EQ(r.error().params.size(), 1U);
    EXPECT_EQ(r.error().params[0].first, "nonfinite-index");
    EXPECT_EQ(r.error().params[0].second, "1");
}

/// TCP 未配置/悬空：NoTcp 两分支结构化拒绝（§9.6 KIN-NO-TCP 两分语义）。
TEST(KinFk, NoTcpTwoBranches_WP15T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const FkEvaluator fk;

    // 分支 1：引用悬空——toolObject 未注册进快照。
    TcpRef dangling = tcpRef();
    dangling.toolObject = idFrom<core::ObjectId>("ghost-tool");
    const auto r1 = fk.evaluate(view, dangling, {0.0, 0.0});
    ASSERT_FALSE(r1.ok());
    EXPECT_EQ(r1.error().code, KinematicsErrorCode::NoTcp);

    // 分支 2：键不命中 canonical TCP 身份 → FrameUnresolved（非空键必须
    // 与工具 localName 精确相等——KinTypes.hpp 解析规则）。
    TcpRef wrongKey = tcpRef();
    wrongKey.tcpKey = "no_such_tcp";
    const auto r2 = fk.evaluate(view, wrongKey, {0.0, 0.0});
    ASSERT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, KinematicsErrorCode::FrameUnresolved);

    // 合法键（＝工具 localName）通过——键匹配语义的正例半边。
    TcpRef goodKey = tcpRef();
    goodKey.tcpKey = toolName();
    const auto r3 = fk.evaluate(view, goodKey, {0.0, 0.0});
    EXPECT_TRUE(r3.ok()) << "非空但精确匹配的 tcpKey 应解析通过";
}

/// 超限位 q 可计算（§5.2 两分语义：FK 失败≠限位违例）＋负裕量＝调用方
/// 违例素材。
TEST(KinFk, OverLimitQComputableWithNegativeMargin_WP15T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    PoseMetrics m;
    // q1=3.5 > upper 2.97：FK 照常计算（几何有效）。
    ASSERT_TRUE(evaluateOk(model, {3.5, 0.0}, m));
    ASSERT_EQ(m.jointMargins.size(), 2U);
    EXPECT_LT(m.jointMargins[0], 0.0) << "超限裕量为负——违例素材交调用方";
}

/// 裕量归一化（D-KIN-6 设计默认）：有界中点 1／限位 0、连续带工作范围
/// 同式、连续无工作范围 +∞、minimumJointMargin 取有界最小。
TEST(KinFk, MarginNormalization_WP15T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = marginModel();
    PoseMetrics m;
    // 四自由度：q={1, 2, 1, 0}——分别落中点/上限位/工作范围超限/无界。
    ASSERT_TRUE(evaluateOk(model, {1.0, 2.0, 1.0, 0.0}, m));
    ASSERT_EQ(m.jointMargins.size(), 4U);

    // m1：bounds (-1,3)、q=1＝中点 → margin 1。
    EXPECT_NEAR(m.jointMargins[0], 1.0, kPoseTol);
    // m2：bounds (0,2)、q=2＝上限位 → margin 0。
    EXPECT_NEAR(m.jointMargins[1], 0.0, kPoseTol);
    // m3：continuous 工作范围 (-0.5,0.5)、q=1 超限 → min(1.5,−0.5)/0.5＝−1。
    EXPECT_NEAR(m.jointMargins[2], -1.0, kPoseTol);
    // m4：continuous 无工作范围 → +∞（无分析限位）。
    EXPECT_EQ(m.jointMargins[3], std::numeric_limits<double>::infinity());
    // 最小裕量＝有界关节的最小值（+∞ 不参与——全无界时才为 +∞）。
    EXPECT_NEAR(m.minimumJointMargin, -1.0, kPoseTol);
}

/// 无设备链守卫的边界说明：CanonicalModel 构造器禁止空链（runtime
/// builder 不变量），NoDevice 在 T03 经真实模型不可达——守卫为注入视图
/// 面的契约防线（替身无法持有空链 CanonicalModel，同一类型系统保证）；
/// 结构化素材路径（NoTcp→诊断映射）由评估器契约测试
/// PoseMetricsEvaluatorContractTest 覆盖。
TEST(KinFk, NoDeviceGuardDocumentedUnreachable_WP15T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{});
    // 该用例为文档化登记（不可达性论证随卡 §14.6）——无运行断言。
    SUCCEED();
}

// =====================================================================
// 确定性（NFR-COR-01——同 (snapshot, q, tcp) 同字节输出）
// =====================================================================

/// 同输入两次评估字节级一致；不同 q 字节不同（canonical 载荷面）。
TEST(KinFk, DeterministicByteOutput_WP15T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = sixAxisModel();
    TestView view(model);
    const FkEvaluator fk;
    const std::vector<double> q = {0.3, -0.5, 0.8, 0.2, -0.4, 1.0};

    const auto r1 = fk.evaluate(view, tcpRef(), q);
    const auto r2 = fk.evaluate(view, tcpRef(), q);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    const auto bytes1 = encodePoseMetricsCanonical(r1.get());
    const auto bytes2 = encodePoseMetricsCanonical(r2.get());
    ASSERT_FALSE(bytes1.empty());
    EXPECT_EQ(bytes1, bytes2) << "同 (snapshot,q,tcp) 必须同字节（NFR-COR-01）";

    const std::vector<double> q2 = {0.3, -0.5, 0.8, 0.2, -0.4, 1.1};
    const auto r3 = fk.evaluate(view, tcpRef(), q2);
    ASSERT_TRUE(r3.ok());
    EXPECT_NE(bytes1, encodePoseMetricsCanonical(r3.get()))
        << "不同 q 的载荷字节必须可分辨";
}
