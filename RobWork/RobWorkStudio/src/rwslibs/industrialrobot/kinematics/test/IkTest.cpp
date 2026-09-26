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
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
using sdurws::ird::kinematics::AnalyticReachBound;
using sdurws::ird::kinematics::IkOutcome;
using sdurws::ird::kinematics::IkOutcomeKind;
using sdurws::ird::kinematics::IkRequest;
using sdurws::ird::kinematics::IkSolver;
using sdurws::ird::kinematics::InitialValueStrategy;
using sdurws::ird::kinematics::kIkSolverContractVersion;
using sdurws::ird::kinematics::kTaskPointIkEvaluationKey;

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

/// 请求壳（确定性绑定面用固定派生身份——零值亦可，非本组断言对象）。
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
    req.referenceQ = {0.0, 0.0};
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

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    constexpr double L1 = 1.1;
    constexpr double L2 = 0.7;

    // 目标＝q*=(0.3, 0.6) 的位姿（肘型二分支——闭式逆解独立推导）。
    const Mat4 golden = referenceFk(model, {0.3, 0.6}).tcp;
    const double tx = golden[0][3];
    const double ty = golden[1][3];
    const std::vector<std::vector<double>> branches = twoLinkClosedForm(L1, L2, tx, ty);
    ASSERT_EQ(branches.size(), 2U);
    // 两分支构型确异（去重对象＝构型的前提 sanity）。
    EXPECT_GT(std::fabs(branches[0][1] - branches[1][1]), 0.1);

    // 两初值各取一分支——同一位姿的两个构型都须保留。
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
