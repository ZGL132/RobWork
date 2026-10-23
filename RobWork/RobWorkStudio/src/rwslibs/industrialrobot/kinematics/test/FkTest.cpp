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

#include <rw/math/Jacobian.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/SerialDevice.hpp>  // REUSE-T18 upstreamComparison 直接调用面

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
/// §5.1）——调用方视角偏置映射零影响（REUSE-T18 语义：权威 q 在输入侧减
/// zeroOffset 映射为 WC 坐标，上游关节内烘焙的偏置在编译侧加回——两侧
/// 相消，同几何双模型在容差内同结果）。T03 旧版为跨模型字节级断言（值域
/// FK 不消费 zeroOffset 字段）；T18 起偏置由上游关节承载，浮点复合路径
/// 不同——按附录 D 第 9 项标量容差对照。
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
        j2.zeroOffset = zeroOffset;  // rad——权威↔运行时坐标换算分量（§5.1）
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
    // 权威 q 直入：偏置映射相消——位姿逐元素＋雅可比逐元素容差内一致。
    const PoseMetrics& a = r1.get();
    const PoseMetrics& b = r2.get();
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(a.tcpInBase.P()[i], b.tcpInBase.P()[i], kPoseTol);
        for (std::size_t j = 0; j < 3; ++j) {
            EXPECT_NEAR(a.tcpInBase.R()(i, j), b.tcpInBase.R()(i, j), kPoseTol);
        }
    }
    ASSERT_EQ(a.jacobian.size(), b.jacobian.size());
    for (std::size_t k = 0; k < a.jacobian.size(); ++k) {
        expectGoldenNear(a.jacobian[k], b.jacobian[k], "偏置映射相消·雅可比元素");
    }
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

/// ★ 审查 G13 登记性用例（WP-15-T20 第三批）：零行程区间（hi==lo）的
/// 裕量防御为"不可达防御"——上游不变式 builder 强制 lower<upper（runtime
/// /CanonicalModel 严格不等）且 CanonicalModel::chain() 仅 const 访问
/// （公共面不存在构造违约态的路径）。FK 侧防御（half<=0 → 边界值 0，
/// 不产 NaN——Fk.cpp 同批变更）作为下游渲染安全的结构兜底存在；无公共
/// API 可驱动，不可达性论证随卡 §14.6 第三批（NoDevice 守卫同款形态）。
TEST(KinFk, MarginZeroSpanDefensiveDocumentedUnreachable_WP15T20)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{});
    // 该用例为文档化登记（不可达性论证随卡 §14.6）——无运行断言。
    SUCCEED();
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

/** @brief REQ-01/09、KIN-14：非零关节下，命名TCP必须改变真实FK位置与雅可比。 */
TEST(KinFk, NamedTcpOffsetsUseExactKeyAndLegacyAlias_REQ01_A15)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-01", "KIN-14"}, std::vector<std::string>{"AT-23"});
    const auto original = twoLinkModel();
    auto tools = original.tools();
    // 法兰到安装座为Rz(π/2)、平移(0.2,0.3,0)，再挂两个不同工具系偏移。
    // 合成结果独立手算：tip=(0.2,0.8,0)，同工具名键=(−0.2,0.3,0)。
    tools[0].namedTcpOffsets = {{"tip", trans(0.2, 0.8, 0)},
                                {"tool_1", trans(-0.2, 0.3, 0)}};
    const auto model = rt::CanonicalModelBuilder().setHeader(original.header())
        .setWorld(original.world()).setChain(original.chain()).setTools(tools)
        .setDefaultTcpIndex(0U).build();
    TestView view(model);
    constexpr double pi = 3.14159265358979323846;
    const std::vector<double> q{pi / 2, -pi / 2};
    TcpRef ref = tcpRef();
    for (const auto& row : tools[0].namedTcpOffsets) {
        ref.tcpKey = row.first;
        const auto value = FkEvaluator().evaluate(view, ref, q);
        ASSERT_TRUE(value.ok()) << value.error().detail;
        EXPECT_NEAR(value.get().tcpInBase.P()[0], row.second.P()[0], 1e-12);
        EXPECT_NEAR(value.get().tcpInBase.P()[1], 1.1 + row.second.P()[1], 1e-12);
    }
    // 空键保持原默认0.7m；未知键显式失败。真实同名键已在上面优先命中。
    ref.tcpKey.clear();
    const auto defaultValue = FkEvaluator().evaluate(view, ref, q);
    ASSERT_TRUE(defaultValue.ok());
    EXPECT_NEAR(defaultValue.get().tcpInBase.P()[0], 0.7, 1e-12);
    ref.tcpKey = "missing";
    EXPECT_FALSE(FkEvaluator().evaluate(view, ref, q).ok());
}

// =====================================================================
// REUSE-T18——已编译模型消费面（锚缺失值面拒绝＋跨视图任务隔离）
// =====================================================================

/// 纯数值视图（runtimeModelView()==nullptr——T17 锚缺省体）→ NoDevice
/// 值面拒绝：FK 消费已编译 WorkCell，无编译产物即无设备可用（§9.2 @pre
/// 的运行期承载；错误码面不变——4 值语义内承载）。
TEST(KinFk, AnchorlessViewRejectedAsNoDevice_REUSE_T18)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{});

    /// 无锚替身：值面完整、锚返回空（T17 缺省体形态的显式载体）。
    struct AnchorlessView final : public sdurws::ird::kinematics::IKinRuntimeView {
        rt::CanonicalModel m_model;
        explicit AnchorlessView(rt::CanonicalModel model)
            : m_model(std::move(model)),
              m_wtb(m_model.world().T_world_base)
        {
        }
        const rt::CanonicalModel& model() const override { return m_model; }
        rw::math::Transform3D<double> worldToBase() const override { return m_wtb; }
        rw::math::Transform3D<double> m_wtb;
    };

    AnchorlessView view(twoLinkModel());
    const FkEvaluator fk;
    const auto r = fk.evaluate(view, tcpRef(), {0.0, 0.0});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, KinematicsErrorCode::NoDevice);
    // 非法 q 守卫仍先于锚消费（可辨识性——维度不符先报 IllegalQ）。
    const auto rBad = fk.evaluate(view, tcpRef(), {0.1, 0.2, 0.3});
    ASSERT_FALSE(rBad.ok());
    EXPECT_EQ(rBad.error().code, KinematicsErrorCode::IllegalQ);
}

/// 跨视图任务隔离（真实生产消费面）：两不同模型的真实编译快照交替评估，
/// 各自结果与独立参考逐元素一致——任务私有 State＋每次 setQ 保证无跨
/// 任务污染（§9.2；T17 提交面冻结语义的评估侧验证）。
TEST(KinFk, CrossViewIsolationRealSnapshots_REUSE_T18)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01", "NFR-COR-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel planar = twoLinkModel();
    const rt::CanonicalModel six = sixAxisModel();
    TestView planarView(planar);
    TestView sixView(six);
    const FkEvaluator fk;
    const std::vector<double> qPlanar = {0.3, -0.7};
    const std::vector<double> qSix = {0.3, -0.5, 0.8, 0.2, -0.4, 1.0};

    // 交替评估三轮——锚/State/雅可比路径在两快照间往返。
    for (int round = 0; round < 3; ++round) {
        const auto rp = fk.evaluate(planarView, tcpRef(), qPlanar);
        const auto rs = fk.evaluate(sixView, tcpRef(), qSix);
        ASSERT_TRUE(rp.ok()) << rp.error().detail;
        ASSERT_TRUE(rs.ok()) << rs.error().detail;

        const RefChainResult refP = referenceFk(planar, qPlanar);
        const RefChainResult refS = referenceFk(six, qSix);
        for (std::size_t i = 0; i < 3; ++i) {
            EXPECT_NEAR(rp.get().tcpInBase.P()[i], refP.tcp[i][3], kPoseTol);
            EXPECT_NEAR(rs.get().tcpInBase.P()[i], refS.tcp[i][3], kPoseTol);
        }
        ASSERT_EQ(rp.get().jacobian.size(), refP.jacobian6xN.size());
        for (std::size_t k = 0; k < rp.get().jacobian.size(); ++k) {
            expectGoldenNear(rp.get().jacobian[k], refP.jacobian6xN[k], "平面链雅可比");
        }
        ASSERT_EQ(rs.get().jacobian.size(), refS.jacobian6xN.size());
        for (std::size_t k = 0; k < rs.get().jacobian.size(); ++k) {
            expectGoldenNear(rs.get().jacobian[k], refS.jacobian6xN[k], "六轴雅可比");
        }
    }
    // 同视图重复评估仍与首轮逐字节一致（同快照同输入同字节——NFR-COR-01）。
    const auto again = fk.evaluate(sixView, tcpRef(), qSix);
    ASSERT_TRUE(again.ok());
    const auto first = fk.evaluate(sixView, tcpRef(), qSix);
    ASSERT_TRUE(first.ok());
    EXPECT_EQ(encodePoseMetricsCanonical(again.get()),
              encodePoseMetricsCanonical(first.get()));
}

/// 上游直接对照（REUSE-T18 正向证据 upstreamComparison 面）：同模型同
/// State 语义下，FkEvaluator 输出与直接调用上游 Device（setQ/baseTframe/
/// baseJframe）的结果逐元素一致——评估器消费的就是上游计算，不重建第二套
/// FK/Jacobian（H2；q_auth−zeroOffset 映射与 src/JointMapping.hpp 同式）。
TEST(KinFk, UpstreamDirectDeviceComparison_REUSE_T18)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = sixAxisModel();
    TestView view(model);
    const FkEvaluator fk;
    const std::vector<double> q = {0.3, -0.5, 0.8, 0.2, -0.4, 1.0};
    const auto r = fk.evaluate(view, tcpRef(), q);
    ASSERT_TRUE(r.ok()) << r.error().detail;
    const PoseMetrics& m = r.get();

    // 直接上游调用（生产同一入口——经锚的受控视图，任务私有 State）。
    const auto anchor = view.runtimeModelView();
    ASSERT_TRUE(anchor != nullptr);
    const auto device = anchor->workCell().findDevice(model.chain().deviceName);
    ASSERT_TRUE(device != nullptr);
    rw::kinematics::State state = anchor->makeState();
    std::vector<double> qRw;  // q_rw＝q_auth−zeroOffset（与 JointMapping 同式）
    for (const auto& j : model.chain().joints) {
        if (j.type != rt::JointType::Fixed) { qRw.push_back(q[qRw.size()] - j.zeroOffset); }
    }
    device->setQ(rw::math::Q(qRw), state);
    const auto resolved =
        anchor->nameResolver().resolveObjectId(model.tools().at(0).objectId);
    ASSERT_TRUE(resolved.ok());
    const rw::kinematics::Frame* tcpFrame =
        anchor->workCell().findFrame(resolved.get().fullName);
    ASSERT_TRUE(tcpFrame != nullptr);
    const rw::core::Ptr<const rw::kinematics::Frame> fptr(tcpFrame);
    const rw::math::Transform3D<double> t = device->baseTframe(fptr, state);
    const rw::math::Jacobian j = device->baseJframe(fptr, state);

    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(m.tcpInBase.P()[i], t.P()[i]);
        for (std::size_t k = 0; k < 3; ++k) {
            EXPECT_DOUBLE_EQ(m.tcpInBase.R()(i, k), t.R()(i, k));
        }
    }
    ASSERT_EQ(m.jacobian.size(), 36U);
    for (std::size_t row = 0; row < 6; ++row) {
        for (std::size_t col = 0; col < 6; ++col) {
            EXPECT_DOUBLE_EQ(m.jacobian[row * 6 + col],
                             j.e()(static_cast<Eigen::Index>(row),
                                   static_cast<Eigen::Index>(col)))
                << "J(" << row << "," << col << ")";
        }
    }
}
