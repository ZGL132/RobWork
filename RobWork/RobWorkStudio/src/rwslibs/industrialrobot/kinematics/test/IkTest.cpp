/**
 * @file   IkTest.cpp
 * @brief  多初值 IK 求解用例组（KinIk，T04 提交 1「求解」批）——可达
 *         目标收敛（V-05）、同位姿双构型保留（V-04 前半/AT-03）、多初值
 *         未收敛搜索未果（V-06）、非法请求 fail-fast（V-03/
 *         KIN-TARGET-ILLEGAL）、解析界限证明素材与 validateProof 可校验
 *         （V-10 前半）、初值策略确定性、取消无终局字段。
 *
 * 设计依据：
 *   - units/kinematics.md §5.3/§5.4/§5.5/§5.6、§10.2（V-03/05/06/10 行）、
 *     §6.1（初值策略三态与去重口径的求解侧前置）
 *   - 黄金几何：KinFkFixture 两套解析算例（平面二连杆——闭式逆解可独立
 *     参考对照，附录 D 第 9 项独立参考类别）
 *   - 任务契约 tasks/foundation/WP-15-T04.json acceptance 2/4（提交 1
 *     覆盖半区；V-04/07/08/09 全量断言随「过滤去重排序」提交）
 *
 * 独立参考：目标位姿由夹具 referenceFk（4×4 齐次直算，与产品 Eigen
 * 实现零共享代码）产生；双构型闭式逆解按平面二连杆几何公式独立推导。
 */

#include "../test/KinFkFixture.hpp"

#include <sdurws/ird/evidence/Evidence.hpp>   // validateProof/IProducerRegistryView（V-10 前半素材自证）
#include <sdurws/ird/evidence/Snapshot.hpp>   // AnalysisSnapshot（validateProof 绑定核对面）
#include <sdurws/ird/kinematics/Bounds.hpp>
#include <sdurws/ird/kinematics/Evaluators.hpp>  // encodeTaskPointIkPayloadCanonical（payload 字节面）
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/kinematics/SolutionSet.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
namespace kin = sdurws::ird::kinematics;
using kin::AnalyticReachBound;
using kin::FilteredSolutionRecord;
using kin::IkCollisionVerdict;
using kin::IkOutcome;
using kin::IkOutcomeKind;
using kin::IkRequest;
using kin::IkSolutionSet;
using kin::IkSolver;
using kin::InitialValueStrategy;
using kin::KinematicSolution;
using kin::KinematicSolutionSet;
using kin::SolutionFilterReason;
using kin::WorstMetric;
using kin::kIkSolverContractVersion;
using kin::kTaskPointIkEvaluationKey;

namespace {

/// Mat4（夹具独立参考 FK 输出）→ rw 位姿（测试换形——不触碰产品代码）。
rw::math::Transform3D<double> transformOf(const Mat4& m)
{
    const rw::math::Rotation3D<double> r(
        m[0][0], m[0][1], m[0][2],
        m[1][0], m[1][1], m[1][2],
        m[2][0], m[2][1], m[2][2]);
    const rw::math::Vector3D<double> p(m[0][3], m[1][3], m[2][3]);
    return rw::math::Transform3D<double>(p, r);
}

/// 平面二连杆闭式逆解（独立参考——附录 D 第 9 项类别；L1/L2 取夹具
/// twoLinkModel 常量；返回 elbow 两分支；目标须在可达环带内）。
std::vector<std::vector<double>> twoLinkClosedForm(double L1, double L2, double x, double y)
{
    const double c2 = (x * x + y * y - L1 * L1 - L2 * L2) / (2.0 * L1 * L2);
    const double s2 = std::sqrt(1.0 - c2 * c2);
    std::vector<std::vector<double>> out;
    for (const int sign : {1, -1}) {
        const double q2 = sign * std::atan2(s2, c2);
        const double q1 = std::atan2(y, x) - std::atan2(L2 * (sign * s2), L1 + L2 * c2);
        out.push_back({q1, q2});
    }
    return out;
}

/// 平面三连杆模型（V-04 同位姿异构型的几何载体——平面 3R 臂的全位姿
/// 逆解有**两个**肘型分支；2R 臂全位姿逆解唯一，不能作该用例的载体）。
/// 几何：L1=1.0（j1→j2）、L2=0.6（j2→j3）、L3=0.4（j3→TCP），全 z 轴。
inline rt::CanonicalModel threeLinkModel()
{
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("t1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -2.97, 2.97));
    joints.push_back(revolute("t2", rw::math::Vector3D<double>(0, 0, 1),
                              trans(1.0, 0, 0), -3.14159265358979323846,
                              3.14159265358979323846));
    joints.push_back(revolute("t3", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0.6, 0, 0), -3.14159265358979323846,
                              3.14159265358979323846));
    return makeModel(joints, trans(0.4, 0, 0));
}

/// 平面三连杆全位姿闭式双支（独立参考）：腕心＝TCP−L3·e(φ)（φ＝末端
/// 姿态角），对腕心做 2R 位置闭式解得 (q1,q2) 两分支，q3＝φ−q1−q2。
std::vector<std::vector<double>> threeLinkPoseBranches(double L1, double L2,
                                                       double L3, double x,
                                                       double y, double phi)
{
    const double wx = x - L3 * std::cos(phi);
    const double wy = y - L3 * std::sin(phi);
    std::vector<std::vector<double>> out;
    for (const std::vector<double>& wrist : twoLinkClosedForm(L1, L2, wx, wy)) {
        out.push_back({wrist[0], wrist[1], phi - wrist[0] - wrist[1]});
    }
    return out;
}

/// 请求壳（确定性绑定面用固定派生身份——零值亦可，非本组断言对象）。
/// referenceQ＝零位构型（维度随视图自由度——两用例模型族共用）。
IkRequest makeRequest(const TestView& view, const rw::math::Transform3D<double>& target,
                      const std::vector<std::vector<double>>& initials)
{
    IkRequest req;
    req.targetInBase = target;
    req.modelView = &view;
    req.tcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    req.initialValues = initials;
    req.iterationLimit = 200U;
    req.intervals = sdurws::ird::kinematics::evaluationIntervals(view);
    req.referenceQ.assign(sdurws::ird::kinematics::evaluationIntervals(view).size(), 0.0);
    req.targetRef.pointOid = idFrom<core::ObjectId>("kin-point-1");
    req.requestIdentity.snapshotId.bytes = digestOf("kin-ik-snap");
    req.requestIdentity.sliceId.bytes = digestOf("kin-ik-slice");
    return req;
}

/// validateProof 的产生者注册表替身（只承载"键已注册＋契约版本相符"——
/// 真实注册表行为归 evidence EV-T10 用例，V-10 属联合观测的本单元半区）。
class StubProducerRegistry final : public evidence::IProducerRegistryView {
public:
    bool isRegistered(std::string_view key) const override
    {
        return key == kTaskPointIkEvaluationKey;
    }
    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        return key == kTaskPointIkEvaluationKey && version == kIkSolverContractVersion;
    }
};

}  // namespace

// =====================================================================
// V-05——IK 有效解（可达目标收敛；残差 ≤ 容差；指标面齐备）
// =====================================================================

TEST(KinIk, ConvergesOnReachableTarget_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 目标＝独立参考 FK 在 q*=(0.3,−0.5) 的输出（黄金对照——位姿含
    // 姿态分量 Rz(−0.2)，非纯位置目标）。
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;
    const rw::math::Transform3D<double> target = transformOf(golden);

    IkRequest req = makeRequest(view, target, {{0.1, 0.1}});
    const IkOutcome out = IkSolver().solve(req);

    // 结局 1：解非空、残差逐项 ≤ 两容差（§5.3 收敛判据）。
    ASSERT_EQ(out.outcomeKind, IkOutcomeKind::SolutionsFound);
    ASSERT_EQ(out.solutionSet.solutions.size(), 1U);
    const auto& s = out.solutionSet.solutions.front();
    EXPECT_LE(s.positionResidual, req.positionTolerance);
    EXPECT_LE(s.orientationResidual, req.orientationTolerance);
    // 指标面齐备（§6.1 值模型行）——裕量/条件数/版本；可操作度按 T03
    // 锁定的 D-KIN-2 规则 n<6 → w≡0（本算例 2 自由度，恰验证该规则面）。
    EXPECT_EQ(s.jointMargins.size(), 2U);
    EXPECT_EQ(s.manipulability, 0.0);
    EXPECT_GE(s.conditionNumber, 1.0);
    EXPECT_EQ(s.solverContractVersion, kIkSolverContractVersion);
    EXPECT_FALSE(s.signature.empty());
    // 迭代在预算内发生且统计闭合（rawCount=1、converged 同步）。
    EXPECT_GT(s.iterations, 0U);
    EXPECT_EQ(out.solutionSet.statistics.rawCount, 1U);
    EXPECT_EQ(out.solutionSet.statistics.convergedCount, 1U);
    EXPECT_FALSE(out.cancelled);
    // 本请求未携带碰撞会话——必须如实标记 collisionNotEvaluated
    // （证据缺失≠无碰撞，KIN-05 口径；提交 2 的 V-08 用例补会话在场面）。
    EXPECT_TRUE(out.collisionNotEvaluated);
    // 解的碰撞状态未评价（evaluated=false——不解读为无碰撞）。
    EXPECT_FALSE(s.collisionStatus.evaluated);
}

// =====================================================================
// V-04 前半（AT-03）——同末端位姿异构型均保留（去重对象＝构型）
// =====================================================================

TEST(KinIk, SamePoseDistinctConfigurationsRetained_WP15T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = threeLinkModel();
    TestView view(model);
    constexpr double L1 = 1.0;
    constexpr double L2 = 0.6;
    constexpr double L3 = 0.4;

    // 目标＝q*=(0.2, 0.4, 0.0) 的全位姿（平面 3R——肘型两分支闭式推导；
    // 姿态角 φ＝q1+q2+q3=0.6）。
    const Mat4 golden = referenceFk(model, {0.2, 0.4, 0.0}).tcp;
    const std::vector<std::vector<double>> branches = threeLinkPoseBranches(
        L1, L2, L3, golden[0][3], golden[1][3], 0.6);
    ASSERT_EQ(branches.size(), 2U);
    // 两分支构型确异（去重对象＝构型的前提 sanity）。
    EXPECT_GT(std::fabs(branches[0][1] - branches[1][1]), 0.1);

    // 两初值各取一分支——同一位姿的两个构型都须保留（V-04：求解＋去重
    // 全管线后仍在）。
    IkRequest req = makeRequest(view, transformOf(golden), branches);
    const IkOutcome out = IkSolver().solve(req);

    ASSERT_EQ(out.outcomeKind, IkOutcomeKind::SolutionsFound);
    ASSERT_EQ(out.solutionSet.solutions.size(), 2U)
        << "同末端位姿的两个构型都必须保留（KIN-02/AT-03 去重反例前提）";
    for (const auto& s : out.solutionSet.solutions) {
        EXPECT_LE(s.positionResidual, req.positionTolerance);
        EXPECT_LE(s.orientationResidual, req.orientationTolerance);
    }
    // 构型签名互异（记录键区分构型——I-KIN-3 记录面）。
    EXPECT_NE(out.solutionSet.solutions[0].signature,
              out.solutionSet.solutions[1].signature);
}

// =====================================================================
// V-06——多初值未收敛（搜索未果素材；不得输出不可行结论）
// =====================================================================

TEST(KinIk, MultiInitNoConvergenceCarriesSearchRecord_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 目标在可达环带内孔（‖p‖=0.2 < |L1−L2|=0.4）——数值不可达但**在**
    // 解析外界限内（结局 5 不触发，进入迭代→结局 2）。
    rw::math::Transform3D<double> target = trans(0.2, 0.0, 0.0);
    IkRequest req = makeRequest(view, target, {});
    req.initialValues = sdurws::ird::kinematics::makeInitialValues(
        InitialValueStrategy::JointGrid, 4U, 7U, req.intervals, req.referenceQ);
    ASSERT_EQ(req.initialValues.size(), 4U);

    const IkOutcome out = IkSolver().solve(req);

    // 结局 2：零候选＋搜索未果记录必附（预算/初值数/迭代统计三要素）。
    EXPECT_EQ(out.outcomeKind, IkOutcomeKind::MultiInitNoConvergence);
    EXPECT_TRUE(out.solutionSet.solutions.empty());
    ASSERT_TRUE(out.solutionSet.searchRecord.has_value());
    EXPECT_EQ(out.solutionSet.searchRecord->initialGuessesTried, 4U);
    EXPECT_EQ(out.solutionSet.searchRecord->iterationsPerInit.size(), 4U);
    std::uint64_t sumIters = 0;
    for (const std::uint32_t it : out.solutionSet.searchRecord->iterationsPerInit) {
        sumIters += it;
    }
    EXPECT_EQ(out.solutionSet.searchRecord->searchBudgetUsed, sumIters);
    // 铁律：结局 2 不得携带任何"不可行"素材（无 proof——§8.1 C5）。
    EXPECT_FALSE(out.proofMaterial.has_value());
    // 复评可翻转的语义面：换初值/扩预算按同一冻结输入复评（V-07 由
    // 提交 2 承载翻转对；本用例锁定"搜索未果≠不可行"的记录形态）。
}

// =====================================================================
// V-03——非法请求 fail-fast（不钳制不置零；KIN-TARGET-ILLEGAL 语义锚）
// =====================================================================

TEST(KinIk, IllegalRequestFailsFast_WP15T04_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const rw::math::Transform3D<double> okTarget = trans(1.0, 0.2, 0.0);

    // 目标位姿含 NaN → 拒绝（消息携带 KIN-TARGET-ILLEGAL 锚）。
    {
        IkRequest req = makeRequest(view, okTarget, {{0.0, 0.0}});
        rw::math::Transform3D<double> bad = req.targetInBase;
        bad.P()[0] = std::numeric_limits<double>::quiet_NaN();
        req.targetInBase = bad;
        EXPECT_THROW(
            {
                try {
                    (void)IkSolver().solve(req);
                } catch (const std::invalid_argument& e) {
                    EXPECT_NE(std::string(e.what()).find("KIN-TARGET-ILLEGAL"),
                              std::string::npos)
                        << "非法目标必须锚定 KIN-TARGET-ILLEGAL（§5.5）";
                    throw;
                }
            },
            std::invalid_argument);
    }
    // 位置容差 ≤0 → 拒绝。
    {
        IkRequest req = makeRequest(view, okTarget, {{0.0, 0.0}});
        req.positionTolerance = 0.0;
        EXPECT_THROW(
            {
                try {
                    (void)IkSolver().solve(req);
                } catch (const std::invalid_argument& e) {
                    EXPECT_NE(std::string(e.what()).find("KIN-TARGET-ILLEGAL"),
                              std::string::npos);
                    throw;
                }
            },
            std::invalid_argument);
    }
    // 姿态容差 NaN → 拒绝。
    {
        IkRequest req = makeRequest(view, okTarget, {{0.0, 0.0}});
        req.orientationTolerance = std::numeric_limits<double>::quiet_NaN();
        EXPECT_THROW((void)IkSolver().solve(req), std::invalid_argument);
    }
    // 空初值集 → 拒绝。
    {
        IkRequest req = makeRequest(view, okTarget, {});
        EXPECT_THROW((void)IkSolver().solve(req), std::invalid_argument);
    }
    // 迭代上限 0 → 拒绝。
    {
        IkRequest req = makeRequest(view, okTarget, {{0.0, 0.0}});
        req.iterationLimit = 0U;
        EXPECT_THROW((void)IkSolver().solve(req), std::invalid_argument);
    }
    // referenceQ 维度不符 → 拒绝（D-KIN-4 显式输入的契约面）。
    {
        IkRequest req = makeRequest(view, okTarget, {{0.0, 0.0}});
        req.referenceQ = {0.0};
        EXPECT_THROW((void)IkSolver().solve(req), std::invalid_argument);
    }
    // TCP 引用悬空 → 拒绝（KIN-NO-TCP 语义锚）。
    {
        IkRequest req = makeRequest(view, okTarget, {{0.0, 0.0}});
        req.tcp.toolObject = idFrom<core::ObjectId>("kin-dangling");
        EXPECT_THROW(
            {
                try {
                    (void)IkSolver().solve(req);
                } catch (const std::invalid_argument& e) {
                    EXPECT_NE(std::string(e.what()).find("KIN-NO-TCP"),
                              std::string::npos);
                    throw;
                }
            },
            std::invalid_argument);
    }
}

// =====================================================================
// V-10 前半——解析界限证明素材（仅素材不裁定；validateProof 可校验）
// =====================================================================

TEST(KinIk, AnalyticBoundExceededMaterialValidatable_WP15T04_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 目标距离 2.5 m > 上界 L1+L2+TCP=1.8 m（解析可判——零迭代）。
    IkRequest req = makeRequest(view, trans(2.5, 0.0, 0.0), {{0.0, 0.0}});
    const IkOutcome out = IkSolver().solve(req);

    ASSERT_EQ(out.outcomeKind, IkOutcomeKind::AnalyticBoundExceeded);
    EXPECT_TRUE(out.solutionSet.solutions.empty());
    ASSERT_TRUE(out.proofMaterial.has_value());

    // 推导输入与上界（黄金值——连杆长度和；闭式可验）。
    const AnalyticReachBound& bound = out.proofMaterial->bound;
    expectGoldenNear(bound.totalRadius, 1.1 + 0.7, "工作半径上界");
    expectGoldenNear(out.proofMaterial->targetDistance, 2.5, "目标距离");
    EXPECT_FALSE(out.proofMaterial->proof.boundExpression.empty())
        << "素材必须附推导输入（§8.2）";

    // 证明素材字段面（evidence §6.3——validateProof 五查的素材半区）。
    const auto& proof = out.proofMaterial->proof;
    EXPECT_EQ(proof.category, evidence::ProofCategory::AnalyticBound);
    EXPECT_EQ(proof.claimToken,
              std::string(sdurws::ird::kinematics::kReachBeyondLinkSumClaim));
    EXPECT_EQ(proof.snapshotId, req.requestIdentity.snapshotId)
        << "expectedSliceId 绑定：素材身份取自请求身份（D-09 可校验）";
    EXPECT_EQ(proof.sliceId, req.requestIdentity.sliceId);
    EXPECT_EQ(proof.producer, std::string(kTaskPointIkEvaluationKey));
    EXPECT_EQ(proof.producerContractVersion, kIkSolverContractVersion);
    EXPECT_EQ(proof.coverageClaim,
              std::string(evidence::kCoverageClaimAllAlternatives));

    // 素材自证：以替身注册表＋同身份快照跑 evidence validateProof——
    // 零问题（成立与否的裁定仍归 evidence 汇总，本用例只证"素材合格"）。
    StubProducerRegistry registry;
    evidence::AnalysisSnapshot snapshot;
    snapshot.snapshotId = req.requestIdentity.snapshotId;
    const auto issues =
        evidence::validateProof(proof, registry, snapshot, req.requestIdentity.sliceId);
    EXPECT_TRUE(issues.empty()) << "解析界限素材必须通过 validateProof 字段级校验";
}

// =====================================================================
// 初值策略确定性（§3.4 随机性唯一来源；§5.3 初值集语义）
// =====================================================================

TEST(KinIk, InitialValueStrategiesDeterministic_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "NFR-COR-02"},
                  std::vector<std::string>{});

    namespace kin = sdurws::ird::kinematics;
    const std::vector<kin::JointInterval> intervals = {
        kin::JointInterval{-2.97, 2.97, true},
        kin::JointInterval{-0.5, 0.5, true},
    };
    const std::vector<double> ref = {0.1, -0.2};

    // ReferenceQ：单初值＝参考构型本身。
    const auto refInit = kin::makeInitialValues(kin::InitialValueStrategy::ReferenceQ,
                                                5U, 0U, intervals, ref);
    ASSERT_EQ(refInit.size(), 1U);
    EXPECT_EQ(refInit[0], ref);

    // SeededRandom：同 seed 同矩阵（确定性序列）；区间内取值。
    const auto r1 = kin::makeInitialValues(kin::InitialValueStrategy::SeededRandom,
                                           8U, 42U, intervals, ref);
    const auto r2 = kin::makeInitialValues(kin::InitialValueStrategy::SeededRandom,
                                           8U, 42U, intervals, ref);
    ASSERT_EQ(r1.size(), 8U);
    EXPECT_EQ(r1, r2) << "同 seed 必得同序列（§3.4——随机性唯一来源）";
    for (const auto& q : r1) {
        ASSERT_EQ(q.size(), 2U);
        EXPECT_GE(q[0], -2.97);
        EXPECT_LE(q[0], 2.97);
        EXPECT_GE(q[1], -0.5);
        EXPECT_LE(q[1], 0.5);
    }

    // JointGrid：端点/中点黄金——count=2 取两端、count=1 取中点。
    const auto g2 = kin::makeInitialValues(kin::InitialValueStrategy::JointGrid,
                                           2U, 0U, intervals, ref);
    ASSERT_EQ(g2.size(), 2U);
    expectGoldenNear(g2[0][0], -2.97, "网格 t=0 下界");
    expectGoldenNear(g2[1][0], 2.97, "网格 t=1 上界");
    const auto g1 = kin::makeInitialValues(kin::InitialValueStrategy::JointGrid,
                                           1U, 0U, intervals, ref);
    ASSERT_EQ(g1.size(), 1U);
    expectGoldenNear(g1[0][0], 0.0, "网格单点取中点");
}

// =====================================================================
// 取消语义（§9.2 @取消——返回 cancelled=true，无终局字段）
// =====================================================================

TEST(KinIk, CancellationYieldsNoTerminalFields_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{"AT-34"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;

    IkRequest req = makeRequest(view, transformOf(golden), {{0.1, 0.1}});
    req.cancellationProbe = [] { return true; };  // 首次探针即取消。

    const IkOutcome out = IkSolver().solve(req);

    EXPECT_TRUE(out.cancelled);
    // 无终局字段：解集空、无搜索未果、无证明素材（取消不是结局）。
    EXPECT_TRUE(out.solutionSet.solutions.empty());
    EXPECT_FALSE(out.solutionSet.searchRecord.has_value());
    EXPECT_FALSE(out.proofMaterial.has_value());
}

// =====================================================================
// 确定性（V-04 两次输出逐位比对的服务面前置——同请求两次求解逐位一致）
// =====================================================================

TEST(KinIk, RepeatedSolveBitwiseIdentical_WP15T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-COR-01"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, 0.6}).tcp;
    const std::vector<std::vector<double>> branches =
        twoLinkClosedForm(1.1, 0.7, golden[0][3], golden[1][3]);

    IkRequest req = makeRequest(view, transformOf(golden), branches);
    const IkOutcome a = IkSolver().solve(req);
    const IkOutcome b = IkSolver().solve(req);

    ASSERT_EQ(a.solutionSet.solutions.size(), b.solutionSet.solutions.size());
    for (std::size_t i = 0; i < a.solutionSet.solutions.size(); ++i) {
        const auto& sa = a.solutionSet.solutions[i];
        const auto& sb = b.solutionSet.solutions[i];
        EXPECT_EQ(sa.q, sb.q) << "逐位一致（NFR-COR-01——浮点布局含于输出）";
        EXPECT_EQ(sa.positionResidual, sb.positionResidual);
        EXPECT_EQ(sa.orientationResidual, sb.orientationResidual);
        EXPECT_EQ(sa.manipulability, sb.manipulability);
        EXPECT_EQ(sa.minimumJointMargin, sb.minimumJointMargin);
        EXPECT_EQ(sa.iterations, sb.iterations);
        EXPECT_EQ(sa.signature, sb.signature);
    }
}

// =====================================================================
// 以下为 T04 提交 2「过滤去重排序」用例组（acceptance 1/2/3 全量断言）
// =====================================================================

namespace {

/// 碰撞会话替身（§10.1"替身碰撞评估器（脚本化判定）"——V-08/V-09 与
/// 过滤顺序用例的脚本面；真实④端口会话组装归 T07）。
class StubCollisionSession final : public kin::IKinCollisionSession {
public:
    using Predicate = std::function<bool(const std::vector<double>&)>;

    StubCollisionSession(Predicate predicate, std::vector<core::ObjectId> pairs)
        : m_predicate(std::move(predicate)), m_pairs(std::move(pairs))
    {
    }

    IkCollisionVerdict evaluate(const std::vector<double>& q) const override
    {
        IkCollisionVerdict v;
        v.inCollision = m_predicate(q);
        if (v.inCollision) {
            v.objectIdPairs = m_pairs;
        }
        return v;
    }

private:
    Predicate m_predicate;               ///< 脚本化判定（构型级）
    std::vector<core::ObjectId> m_pairs; ///< 碰撞对象对（成对展平）
};

/// 解便捷构造（视图级四键用例——手工黄金值，绕过求解管线）。
KinematicSolution handSolution(std::vector<double> q, double minMargin,
                               double manipulability, std::uint32_t initIndex)
{
    KinematicSolution s;
    s.q = std::move(q);
    s.minimumJointMargin = minMargin;
    s.manipulability = manipulability;
    s.positionResidual = 0.0;
    s.orientationResidual = 0.0;
    s.conditionNumber = 1.0;
    s.sourceInitIndex = initIndex;
    s.signature = kin::configurationSignature(s.q);
    return s;
}

}  // namespace

// ---------------------------------------------------------------------
// V-04/AT-03（续）——去重＝构型级成对容差；I-KIN-3 两向语义
// ---------------------------------------------------------------------

TEST(KinIk, DedupMergesWithinPerAxisTolerance_WP15T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;

    // 两初值逐轴相差 5e-7（≤1e-6 阈值）——去重等价（合并为 1 个解）。
    IkRequest req = makeRequest(view, transformOf(golden),
                                {{0.3, -0.5}, {0.3 + 5e-7, -0.5 + 5e-7}});
    const IkOutcome out = IkSolver().solve(req);

    ASSERT_EQ(out.outcomeKind, IkOutcomeKind::SolutionsFound);
    EXPECT_EQ(out.solutionSet.statistics.convergedCount, 2U)
        << "两个初值都应收敛（阶段①复验通过）";
    ASSERT_EQ(out.solutionSet.solutions.size(), 1U)
        << "去重后保留 1 个代表（§6.1 成对容差比较）";
    EXPECT_EQ(out.solutionSet.statistics.dedupedCount, 1U);
    EXPECT_EQ(out.solutionSet.statistics.filteredCount, 0U)
        << "去重合并不产生过滤记录（统计口径分离）";
    // 组内代表＝sourceInitIndex 最小者（§6.3 键 4 注）。
    EXPECT_EQ(out.solutionSet.solutions[0].sourceInitIndex, 0U);
}

TEST(KinIk, SignaturesDifferWhileDedupEquivalent_IKIN3_WP15T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{});

    // I-KIN-3：相差 1e-9 rad 的两个构型——签名不同（精确编码）但去重
    // 等价（容差比较）；"哈希等价≠去重等价"的编码面锁定。
    const std::vector<double> a = {0.3, -0.5};
    const std::vector<double> b = {0.3 + 1e-9, -0.5};
    EXPECT_NE(kin::configurationSignature(a), kin::configurationSignature(b));

    // 经求解管线验证去重合并（容差面）。
    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, a).tcp;
    IkRequest req = makeRequest(view, transformOf(golden), {a, b});
    const IkOutcome out = IkSolver().solve(req);
    EXPECT_EQ(out.solutionSet.statistics.convergedCount, 2U);
    EXPECT_EQ(out.solutionSet.solutions.size(), 1U);
}

TEST(KinIk, DedupThresholdIsPerAxis_WP15T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{"AT-03"});

    // 逐轴阈值语义（附录 D 第 3 项/C1）的直接单元面——deduplicateSolutions
    // 唯一实现点（求解管线内同函数）：
    //   两构型逐轴差 (0.5, 0.5)、阈值 0.7——逐轴规则判"同构型"（全部轴
    //   ≤阈值）；若实现误用合模长/范数（0.707>0.7）会误判"异构型"。
    KinematicSolution a = handSolution({0.1, 5.0}, 1.0, 1.0, 0U);
    KinematicSolution b = handSolution({0.6, 5.0}, 1.0, 1.0, 1U);
    auto merged = kin::deduplicateSolutions({a, b}, 0.7);
    EXPECT_EQ(merged.size(), 1U) << "逐轴阈值：全轴 ≤阈值＝同构型（合并）";
    EXPECT_EQ(merged[0].sourceInitIndex, 0U) << "组内代表＝先到者（最小 init 序）";

    // 任一轴超阈即不同构型：单轴差 0.8 > 0.7。
    b.q = {0.9, 5.0};
    EXPECT_EQ(kin::deduplicateSolutions({a, b}, 0.7).size(), 2U);

    // 阈值边界（≤ 阈值即合并——闭式比较 |Δ| ≤ T；取二进制精确值避免
    // 浮点表示误差干扰边界语义：0.75−0.25=0.5 精确）。
    a.q = {0.25, 5.0};
    b.q = {0.75, 5.0};
    EXPECT_EQ(kin::deduplicateSolutions({a, b}, 0.5).size(), 1U);
    b.q = {0.76, 5.0};
    EXPECT_EQ(kin::deduplicateSolutions({a, b}, 0.5).size(), 2U);

    // 管线级回归：三连杆双分支构型（大间距）经求解管线去重后均保留
    // （去重对象＝构型——同位姿双构型不被误并）。
    const rt::CanonicalModel model = threeLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.2, 0.4, 0.0}).tcp;
    const std::vector<std::vector<double>> branches =
        threeLinkPoseBranches(1.0, 0.6, 0.4, golden[0][3], golden[1][3], 0.6);
    IkRequest req = makeRequest(view, transformOf(golden), branches);
    const IkOutcome out = IkSolver().solve(req);
    EXPECT_EQ(out.outcomeKind, IkOutcomeKind::SolutionsFound);
    EXPECT_EQ(out.solutionSet.solutions.size(), 2U);
}

// ---------------------------------------------------------------------
// V-07——换初值/扩预算后按同一冻结输入复评可翻转
// ---------------------------------------------------------------------

TEST(KinIk, ExpandedInitialValuesFlipOutcome_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;
    const rw::math::Transform3D<double> target = transformOf(golden);

    // 复评 A：远离解的单一初值＋小预算（迭代上限 1）——未收敛
    // （搜索未果 C5）。
    IkRequest reqA = makeRequest(view, target, {{3.0, -2.5}});
    reqA.iterationLimit = 1U;
    const IkOutcome outA = IkSolver().solve(reqA);
    EXPECT_EQ(outA.outcomeKind, IkOutcomeKind::MultiInitNoConvergence);
    ASSERT_TRUE(outA.solutionSet.searchRecord.has_value());
    EXPECT_EQ(outA.solutionSet.searchRecord->initialGuessesTried, 1U);

    // 复评 B：同一冻结输入（同目标/容差/上限）扩大初值集——加入靠近
    // 解的初值后翻转为 SolutionsFound（搜索未果≠不可行）。
    IkRequest reqB = reqA;
    reqB.initialValues = {{3.0, -2.5}, {0.3 + 1e-4, -0.5}};
    const IkOutcome outB = IkSolver().solve(reqB);
    EXPECT_EQ(outB.outcomeKind, IkOutcomeKind::SolutionsFound);
    ASSERT_EQ(outB.solutionSet.solutions.size(), 1U);
    EXPECT_LE(outB.solutionSet.solutions[0].positionResidual,
              reqB.positionTolerance);
}

// ---------------------------------------------------------------------
// V-08——一构型碰撞另一构型有效（构型级碰撞仅过滤该解）
// ---------------------------------------------------------------------

TEST(KinIk, CollisionFilteredKeepsValidConfiguration_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-05"},
                  std::vector<std::string>{"AT-19"});

    const rt::CanonicalModel model = threeLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.2, 0.4, 0.0}).tcp;
    const std::vector<std::vector<double>> branches = threeLinkPoseBranches(
        1.0, 0.6, 0.4, golden[0][3], golden[1][3], 0.6);

    // 替身脚本：第二轴为正的构型碰撞（肘型分支之一——两分支 q2 反号）。
    const core::ObjectId pairA = idFrom<core::ObjectId>("kin-obs-a");
    const core::ObjectId pairB = idFrom<core::ObjectId>("kin-obs-b");
    StubCollisionSession session(
        [](const std::vector<double>& q) { return q[1] > 0.0; }, {pairA, pairB});

    IkRequest req = makeRequest(view, transformOf(golden), branches);
    req.collisionSession = &session;
    const IkOutcome out = IkSolver().solve(req);

    // 结局 4（1 的子形态）：碰撞解被过滤、另一构型在解集。
    EXPECT_EQ(out.outcomeKind, IkOutcomeKind::PartialCollision);
    EXPECT_FALSE(out.collisionNotEvaluated);
    ASSERT_EQ(out.solutionSet.solutions.size(), 1U);
    EXPECT_EQ(out.solutionSet.solutions[0].collisionStatus.evaluated, true);
    EXPECT_EQ(out.solutionSet.solutions[0].collisionStatus.inCollision, false);

    // 过滤记录（构型级——KIN-COLLISION-FILTERED 的记录面；诊断码归 T07）。
    ASSERT_EQ(out.solutionSet.filteredRecords.size(), 1U);
    EXPECT_EQ(out.solutionSet.filteredRecords[0].reason,
              SolutionFilterReason::Collision);
    ASSERT_EQ(out.solutionSet.filteredRecords[0].objectIdPairs.size(), 2U);
    EXPECT_EQ(out.solutionSet.filteredRecords[0].objectIdPairs[0], pairA);
    EXPECT_EQ(out.solutionSet.filteredRecords[0].objectIdPairs[1], pairB);
    // 任务可行素材不受单解影响（C8——不上升为任务不可行）。
    EXPECT_FALSE(out.proofMaterial.has_value());
}

// ---------------------------------------------------------------------
// V-09——全部候选被过滤（含全部因碰撞）→搜索未果记录，不得输出不可行
// ---------------------------------------------------------------------

TEST(KinIk, AllCollisionFilteredYieldsSearchRecordNotInfeasible_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01", "EVI-02"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = threeLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.2, 0.4, 0.0}).tcp;
    const std::vector<std::vector<double>> branches = threeLinkPoseBranches(
        1.0, 0.6, 0.4, golden[0][3], golden[1][3], 0.6);

    // 替身脚本：全部构型碰撞（C8 反例面）。
    StubCollisionSession session(
        [](const std::vector<double>&) { return true; },
        {idFrom<core::ObjectId>("kin-obs-a"), idFrom<core::ObjectId>("kin-obs-b")});

    IkRequest req = makeRequest(view, transformOf(golden), branches);
    req.collisionSession = &session;
    const IkOutcome out = IkSolver().solve(req);

    EXPECT_EQ(out.outcomeKind, IkOutcomeKind::AllCandidatesFiltered);
    EXPECT_TRUE(out.solutionSet.solutions.empty());
    ASSERT_TRUE(out.solutionSet.searchRecord.has_value())
        << "结局 3 必附搜索未果记录（预算/初值数/迭代统计）";
    EXPECT_EQ(out.solutionSet.searchRecord->initialGuessesTried, 2U);
    ASSERT_EQ(out.solutionSet.filteredRecords.size(), 2U);
    for (const auto& r : out.solutionSet.filteredRecords) {
        EXPECT_EQ(r.reason, SolutionFilterReason::Collision);
    }
    // 铁律：不得输出不可行（无证明素材——判定归 evidence DataInsufficient）。
    EXPECT_FALSE(out.proofMaterial.has_value());
    EXPECT_EQ(out.solutionSet.statistics.filteredCount, 2U);
    EXPECT_EQ(out.solutionSet.statistics.dedupedCount, 0U);
}

// ---------------------------------------------------------------------
// 硬过滤顺序固定①→②→③（限位先于碰撞——记录取首个命中阶段）
// ---------------------------------------------------------------------

TEST(KinIk, FilterOrderLimitBeforeCollision_WP15T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 目标需要 q2≈3.5（> j2 上限 π）——收敛候选必越限；碰撞替身对一切
    // 构型报碰撞：若顺序错误（③先于②）记录会被标为 Collision。
    const Mat4 golden = referenceFk(model, {0.2, 3.5}).tcp;
    StubCollisionSession session(
        [](const std::vector<double>&) { return true; },
        {idFrom<core::ObjectId>("kin-obs-a"), idFrom<core::ObjectId>("kin-obs-b")});

    IkRequest req = makeRequest(view, transformOf(golden), {{0.2, 3.4}});
    req.collisionSession = &session;
    const IkOutcome out = IkSolver().solve(req);

    EXPECT_EQ(out.outcomeKind, IkOutcomeKind::AllCandidatesFiltered);
    ASSERT_EQ(out.solutionSet.filteredRecords.size(), 1U);
    EXPECT_EQ(out.solutionSet.filteredRecords[0].reason,
              SolutionFilterReason::JointLimit)
        << "硬过滤顺序固定①②③——限位命中即记录，不再进入碰撞阶段";
}

// ---------------------------------------------------------------------
// V-04 逐位可复现（payload 字节面）＋referenceQ 显式入身份（D-KIN-4）
// ---------------------------------------------------------------------

TEST(KinIk, ReferenceQEntersIdentityAndDeterminesBytes_WP15T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-06"},
                  std::vector<std::string>{"AT-04"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;

    IkRequest req = makeRequest(view, transformOf(golden), {{0.1, 0.1}});
    req.requestIdentity.snapshotId.bytes = digestOf("kin-id-snap");
    req.requestIdentity.sliceId.bytes = digestOf("kin-id-slice");

    // 同请求（含 referenceQ）两次求解 → payload 字节逐位一致（V-04）。
    const IkOutcome outA = IkSolver().solve(req);
    const IkOutcome outB = IkSolver().solve(req);
    const core::TaskIdentity task{};  // 绑定面同值——只考察 referenceQ 差异
    const std::vector<std::uint8_t> bytesA =
        kin::encodeTaskPointIkPayloadCanonical(outA, task);
    const std::vector<std::uint8_t> bytesB =
        kin::encodeTaskPointIkPayloadCanonical(outB, task);
    ASSERT_EQ(bytesA.size(), bytesB.size());
    EXPECT_TRUE(bytesA == bytesB) << "两次输出逐位一致（NFR-COR-01）";

    // 仅改显式 referenceQ（同解同构型）→ 身份字节变化（referenceQ 显式
    // 入请求身份——D-KIN-4；本单元无任何会话姿态读取面，身份只随显式
    // 输入变化——KIN-06/AT-04 边界）。
    IkRequest reqC = req;
    reqC.referenceQ = {0.5, -0.5};
    reqC.requestIdentity.referenceQ = {0.5, -0.5};
    const IkOutcome outC = IkSolver().solve(reqC);
    const std::vector<std::uint8_t> bytesC =
        kin::encodeTaskPointIkPayloadCanonical(outC, task);
    EXPECT_FALSE(bytesA == bytesC)
        << "referenceQ 变化必须反映在结果身份字节中（显式入身份）";
    // 解本身不受参考构型影响（同构型——排序键未引入容差漂移）。
    ASSERT_EQ(outA.solutionSet.solutions.size(),
              outC.solutionSet.solutions.size());
    EXPECT_EQ(outA.solutionSet.solutions[0].q, outC.solutionSet.solutions[0].q);
}

// ---------------------------------------------------------------------
// 视图级四键稳定排序（§6.3 全键覆盖——手工黄金值）
// ---------------------------------------------------------------------

TEST(KinIk, SolutionSetViewSortsByFourKeys_WP15T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-COR-02"},
                  std::vector<std::string>{});

    // 手工四解（构造序故意打乱），覆盖全部四键（期望序＝键序逐级裁决）：
    //  margin 0.5（init1）→ 键 1 直接居首；
    //  margin 0.3 组内：manip 0.9 三解先于 manip 0.1 一解（键 2）；
    //   三解中距离 0.283 的两解先于距离 1.414 的一解（键 3）；
    //   距离平手的两解按 sourceInitIndex 升序（5 < 8，键 4 兜底）。
    IkSolutionSet set;
    set.requestIdentity.referenceQ = {0.0, 0.0};
    set.solutions.push_back(handSolution({1.0, 1.0}, 0.3, 0.9, 3U));   // 距离 √2
    set.solutions.push_back(handSolution({0.2, 0.2}, 0.3, 0.9, 8U));   // 距离近
    set.solutions.push_back(handSolution({0.5, 0.5}, 0.5, 0.1, 1U));   // margin 最高
    set.solutions.push_back(handSolution({0.4, 0.4}, 0.3, 0.1, 2U));   // manip 低
    set.solutions.push_back(handSolution({0.2, 0.2}, 0.3, 0.9, 5U));   // 与上全平

    const KinematicSolutionSet view(set);
    const auto& sorted = view.sorted();
    ASSERT_EQ(sorted.size(), 5U);
    EXPECT_EQ(sorted[0].sourceInitIndex, 1U) << "键 1：margin 降序居首";
    EXPECT_EQ(sorted[1].sourceInitIndex, 5U)
        << "键 2/3：manip 0.9 组内距离近者优先，平手键 4 取小 init 序";
    EXPECT_EQ(sorted[2].sourceInitIndex, 8U) << "键 4：与上全平 init 8 殿后";
    EXPECT_EQ(sorted[3].sourceInitIndex, 3U) << "键 3：距离 1.414 靠后";
    EXPECT_EQ(sorted[4].sourceInitIndex, 2U) << "键 2：manip 0.1 居末";

    // 幂等：同一集合再次构造视图排序结果逐位一致（V-04 前提）。
    const KinematicSolutionSet viewAgain(set);
    ASSERT_EQ(viewAgain.sorted().size(), sorted.size());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        EXPECT_EQ(viewAgain.sorted()[i].q, sorted[i].q);
        EXPECT_EQ(viewAgain.sorted()[i].sourceInitIndex,
                  sorted[i].sourceInitIndex);
    }

    // 统计透传（构造值持有）。
    EXPECT_EQ(view.statistics().rawCount, 0U);
}

TEST(KinIk, SolutionSetViewWorstByAndFiltered_WP15T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-08"},
                  std::vector<std::string>{});

    IkSolutionSet set;
    set.requestIdentity.referenceQ = {0.0, 0.0};
    set.solutions.push_back(handSolution({0.1, 0.1}, 0.8, 2.0, 0U));
    set.solutions.push_back(handSolution({0.3, 0.3}, 0.2, 1.0, 1U));  // 裕量最小
    set.solutions.push_back(handSolution({0.5, 0.5}, 0.5, 9.0, 2U));  // 条件数最大
    set.solutions[2].conditionNumber = 42.0;
    set.solutions[1].positionResidual = 0.05;                          // 残差最大
    set.statistics.rawCount = 3U;

    const KinematicSolutionSet view(set);

    // 视图排序序（margin 降序）：[init0(0.8), init2(0.5), init1(0.2)]；
    // worstBy 返回 sorted() 序上的下标（SolutionRef——非 sourceInitIndex）。
    ASSERT_EQ(view.sorted().size(), 3U);
    EXPECT_EQ(view.sorted()[0].sourceInitIndex, 0U);
    EXPECT_EQ(view.sorted()[1].sourceInitIndex, 2U);
    EXPECT_EQ(view.sorted()[2].sourceInitIndex, 1U);

    // worstBy 三度量（§6.2——KIN-08 规范来源；同值取 sorted 序先者）。
    // sorted 序＝[init0(margin0.8), init2(margin0.5/cond42), init1
    // (margin0.2/residual0.05)]：
    const auto worstMargin = view.worstBy(WorstMetric::MinimumJointMargin);
    ASSERT_TRUE(worstMargin.has_value());
    EXPECT_EQ(worstMargin->solutionIndex, 2U);  // init1 裕量最小（sorted 下标 2）
    const auto worstCond = view.worstBy(WorstMetric::ConditionNumber);
    ASSERT_TRUE(worstCond.has_value());
    EXPECT_EQ(worstCond->solutionIndex, 1U);  // init2 条件数最大（sorted 下标 1）
    const auto worstResidual = view.worstBy(WorstMetric::PositionResidual);
    ASSERT_TRUE(worstResidual.has_value());
    EXPECT_EQ(worstResidual->solutionIndex, 2U);  // init1 残差最大（sorted 下标 2）

    // filtered（谓词由调用方给出——保持 sorted 序的子序列）。
    const auto reachable = view.filtered(
        [](const KinematicSolution& s) { return s.minimumJointMargin > 0.3; });
    ASSERT_EQ(reachable.size(), 2U);
    EXPECT_EQ(reachable[0].sourceInitIndex, 0U);
    EXPECT_EQ(reachable[1].sourceInitIndex, 2U);
}
