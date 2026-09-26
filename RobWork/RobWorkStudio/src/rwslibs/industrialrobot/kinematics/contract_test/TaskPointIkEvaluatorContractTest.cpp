/**
 * @file   TaskPointIkEvaluatorContractTest.cpp
 * @brief  kin.task-point-ik 评估器契约用例组（KinTaskPointIkEval，T04）——
 *         descriptor 依赖声明（§4.3 行六条）、评估键词形闸门、宿主注入
 *         工厂（O-37）、装配期 fail-fast（KIN-TARGET-ILLEGAL 轨）、结局
 *         映射（payload/搜索未果记录＋诊断/证明素材/取消零素材）、载荷
 *         绑定五元组与模式敏感性（screening-only 承载）。
 *
 * 设计依据：
 *   - units/kinematics.md §4.3/§5.4/§5.6/§8.4/§9.2/§9.3、§10.2（V-16 的
 *     本单元半区——mode 入身份；V-06/09/10 的评估器交付面）
 *   - evidence 冻结契约（Evaluator.hpp/Slice.hpp/Evidence.hpp）；core
 *     ContentDigester（CR-02 摘要唯一算法）
 *   - 任务契约 tasks/foundation/WP-15-T04.json acceptance 1/2/4/5（评估
 *     器交付面；求解管线断言在 IkTest.cpp）
 *
 * 测试替身边界声明：NoopContext/TestView（KinFkFixture——宿主语义替身）
 * 与本地 CancellingContext（取消语义替身）；断言全部针对真实产品代码。
 */

#include "../test/KinFkFixture.hpp"

#include <sdurws/ird/evidence/Slice.hpp>  // isValidEvaluationKey——evidence 冻结词形闸门
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKinSearchExhausted 码值常量（唯一书写点）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
namespace kin = sdurws::ird::kinematics;
using kin::TaskPointIkEvaluator;
using kin::TaskPointIkEvaluatorFactory;
using kin::TaskPointIkQuery;
using kin::kTaskPointIkPayloadToken;
using kin::makeTaskPointIkDescriptor;

namespace {

/// Mat4（夹具独立参考 FK 输出）→ rw 位姿（本 TU 本地换形——与 IkTest.cpp
/// 同名工具互不影响，各自文件局部）。
rw::math::Transform3D<double> transformOf(const Mat4& m)
{
    const rw::math::Rotation3D<double> r(
        m[0][0], m[0][1], m[0][2],
        m[1][0], m[1][1], m[1][2],
        m[2][0], m[2][1], m[2][2]);
    const rw::math::Vector3D<double> p(m[0][3], m[1][3], m[2][3]);
    return rw::math::Transform3D<double>(p, r);
}

/// 查询便捷构造（canonical TCP＋可达目标——二连杆黄金几何）。
TaskPointIkQuery makeQuery(const rw::math::Transform3D<double>& target)
{
    TaskPointIkQuery q;
    q.tcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    q.pointOid = idFrom<core::ObjectId>("kin-point-1");
    q.targetInBase = target;
    q.referenceQ = {0.0, 0.0};
    q.initialStrategy = kin::InitialValueStrategy::ReferenceQ;
    q.initialValuesCount = 1U;
    q.iterationLimit = 200U;
    return q;
}

/// 请求壳（snapshotId/sliceId 填充固定派生身份——绑定五元组断言面）。
evidence::EvaluationRequest makeRequest(core::EvaluationMode mode)
{
    evidence::EvaluationRequest req;
    req.mode = mode;
    req.snapshot.snapshotId.bytes = digestOf("kin-eval-snap");
    req.slice.sliceId.bytes = digestOf("kin-eval-slice");
    return req;
}

/// 取消语义上下文替身（cancellationRequested 恒真——取消轨用）。
class CancellingContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return true; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }
};

/// 重算 SHA-256（CR-02 唯一算法面——载荷完整性凭据核对）。
core::ContentIdentity digestOfBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    core::ContentIdentity id;
    id.bytes = d.finalize();
    return id;
}

}  // namespace

// =====================================================================
// descriptor——§4.3 kin.task-point-ik 行（pose-metrics 五条＋req.points）
// =====================================================================

TEST(KinTaskPointIkEval, DescriptorMatchesSection43Row_WP15T04_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"},
                  std::vector<std::string>{});

    const evidence::EvaluatorDescriptor d = makeTaskPointIkDescriptor();

    // 键与契约版本（Ik.hpp 唯一书写点常量——descriptor/证明素材同源）。
    EXPECT_EQ(d.key, std::string(kin::kTaskPointIkEvaluationKey));
    EXPECT_EQ(d.contractVersion, kin::kIkSolverContractVersion);
    EXPECT_GT(d.contractVersion, 0U);

    // 依赖声明六条（§4.3 行："上述〔pose-metrics 五条〕＋req.points"）。
    ASSERT_EQ(d.inputs.size(), 6U);
    EXPECT_EQ(d.inputs[0].key, "model.robot-design");
    EXPECT_EQ(d.inputs[0].kind, evidence::DependencyKind::Object);
    EXPECT_EQ(d.inputs[1].key, "tcp");
    EXPECT_EQ(d.inputs[1].kind, evidence::DependencyKind::Object);
    EXPECT_EQ(d.inputs[2].key, "policy.resolved");
    EXPECT_EQ(d.inputs[2].kind, evidence::DependencyKind::Policy);
    EXPECT_EQ(d.inputs[3].key, "namemap");
    EXPECT_EQ(d.inputs[3].kind, evidence::DependencyKind::NameMap);
    EXPECT_EQ(d.inputs[4].key, "config.ik");
    EXPECT_EQ(d.inputs[4].kind, evidence::DependencyKind::Configuration);
    EXPECT_EQ(d.inputs[5].key, "req.points");
    EXPECT_EQ(d.inputs[5].kind, evidence::DependencyKind::Object);
    for (const auto& decl : d.inputs) {
        EXPECT_EQ(decl.requiredness, evidence::DependencyRequiredness::Required);
        EXPECT_FALSE(decl.applicability.has_value());
    }

    // 模式集全三值＋无状态＋Profile 声明（kin 域、身份零值不申报）。
    ASSERT_EQ(d.supportedModes.size(), 3U);
    EXPECT_EQ(d.supportedModes[0], core::EvaluationMode::Preview);
    EXPECT_EQ(d.supportedModes[1], core::EvaluationMode::Quick);
    EXPECT_EQ(d.supportedModes[2], core::EvaluationMode::Verified);
    EXPECT_TRUE(d.stateless);
    EXPECT_EQ(d.profile.profileId, "kin");
    EXPECT_FALSE(d.profile.contentIdentity.isValid());
}

/// 评估键过 evidence 冻结词形闸门（kebab 实现形态——卡面点形键随附同步）。
TEST(KinTaskPointIkEval, KeyWordFormGateCompliant_WP15T04_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-04"},
                  std::vector<std::string>{});

    EXPECT_TRUE(evidence::isValidEvaluationKey(kin::kTaskPointIkEvaluationKey));
}

// =====================================================================
// 宿主注入工厂（O-37）与装配期 fail-fast（KIN-TARGET-ILLEGAL 轨）
// =====================================================================

TEST(KinTaskPointIkEval, HostInjectionFactoryProducesEvaluator_WP15T04_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;

    TaskPointIkEvaluatorFactory factory(&view, makeQuery(transformOf(golden)));
    EXPECT_EQ(factory.descriptor().key, std::string(kin::kTaskPointIkEvaluationKey));

    // create() 无参签名（O-37 裁决——注册表路径兼容）产出独立实例。
    auto evaluator = factory.create();
    ASSERT_NE(evaluator.get(), nullptr);
    EXPECT_EQ(evaluator->descriptor().key, std::string(kin::kTaskPointIkEvaluationKey));
}

TEST(KinTaskPointIkEval, AssemblyFailsFastOnIllegalQuery_WP15T04_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-COR-03"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;
    const rw::math::Transform3D<double> okTarget = transformOf(golden);

    // 目标位姿 NaN → KIN-TARGET-ILLEGAL 语义锚 fail-fast（装配期）。
    {
        TaskPointIkQuery q = makeQuery(okTarget);
        q.targetInBase = trans(std::numeric_limits<double>::quiet_NaN(), 0, 0);
        EXPECT_THROW(
            {
                try {
                    TaskPointIkEvaluator evaluator(&view, q);
                    (void)evaluator;
                } catch (const std::invalid_argument& e) {
                    EXPECT_NE(std::string(e.what()).find("KIN-TARGET-ILLEGAL"),
                              std::string::npos);
                    throw;
                }
            },
            std::invalid_argument);
    }
    // 容差 ≤0 → 拒绝。
    {
        TaskPointIkQuery q = makeQuery(okTarget);
        q.positionTolerance = 0.0;
        EXPECT_THROW(TaskPointIkEvaluator evaluator(&view, q), std::invalid_argument);
    }
    // referenceQ 维度不符 → 拒绝（D-KIN-4 显式输入契约面）。
    {
        TaskPointIkQuery q = makeQuery(okTarget);
        q.referenceQ = {0.0};
        EXPECT_THROW(TaskPointIkEvaluator evaluator(&view, q), std::invalid_argument);
    }
    // SeededRandom 且 seed=0 → 拒绝（I-KIN-4 不静默替换）。
    {
        TaskPointIkQuery q = makeQuery(okTarget);
        q.initialStrategy = kin::InitialValueStrategy::SeededRandom;
        q.seed = 0U;
        EXPECT_THROW(TaskPointIkEvaluator evaluator(&view, q), std::invalid_argument);
    }
    // 空视图 → 拒绝（宿主注入契约违约）。
    {
        EXPECT_THROW(TaskPointIkEvaluator evaluator(nullptr, makeQuery(okTarget)),
                     std::invalid_argument);
    }
}

// =====================================================================
// 结局映射——成功 payload（绑定五元组＋CR-02 摘要）
// =====================================================================

TEST(KinTaskPointIkEval, EvaluateSuccessProducesBoundPayload_WP15T04_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;

    TaskPointIkEvaluator evaluator(&view, makeQuery(transformOf(golden)));
    NoopContext context;
    const evidence::EvaluationRequest req = makeRequest(core::EvaluationMode::Verified);
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    // 成功轨：payload 在场＋token＋摘要＝对字节重算 SHA-256（CR-02）。
    ASSERT_TRUE(out.payload.has_value());
    EXPECT_EQ(out.payload->kindToken, kTaskPointIkPayloadToken);
    EXPECT_FALSE(out.payload->canonicalBytes.empty());
    EXPECT_EQ(out.payload->digest, digestOfBytes(out.payload->canonicalBytes));
    // 成功轨无搜索未果/证明（互斥组合——§5.4）。
    EXPECT_FALSE(out.searchRecord.has_value());
    EXPECT_FALSE(out.proof.has_value());
}

// =====================================================================
// 结局映射——搜索未果（V-06/V-09 交付面：记录＋诊断，无 payload）
// =====================================================================

TEST(KinTaskPointIkEval, SearchExhaustedDeliversRecordAndDiag_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01", "EVI-02"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 内孔目标（‖p‖=0.2 < 0.4）——数值不可达 → 结局 2。
    TaskPointIkQuery q = makeQuery(trans(0.2, 0.0, 0.0));
    q.initialStrategy = kin::InitialValueStrategy::JointGrid;
    q.initialValuesCount = 4U;
    q.seed = 7U;
    TaskPointIkEvaluator evaluator(&view, q);
    NoopContext context;
    const evidence::EvaluationRequest req = makeRequest(core::EvaluationMode::Verified);
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    // 搜索未果记录在场（C5 素材——不得输出不可行：无 proof 无 payload）。
    ASSERT_TRUE(out.searchRecord.has_value());
    EXPECT_EQ(out.searchRecord->initialGuessesTried, 4U);
    EXPECT_FALSE(out.payload.has_value());
    EXPECT_FALSE(out.proof.has_value());
    // KIN-SEARCH-EXHAUSTED 诊断在场（§9.6 行 10——码值经注册常量）。
    bool hasSearchDiag = false;
    for (const auto& d : out.diagnostics) {
        if (d.code == std::string(kin::kKinSearchExhausted)) {
            hasSearchDiag = true;
        }
    }
    EXPECT_TRUE(hasSearchDiag);
}

// =====================================================================
// 结局映射——解析界限证明素材（V-10 前半交付面：expectedSliceId 绑定）
// =====================================================================

TEST(KinTaskPointIkEval, AnalyticBoundDeliversProofMaterial_WP15T04_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    TaskPointIkEvaluator evaluator(&view, makeQuery(trans(2.5, 0.0, 0.0)));
    NoopContext context;
    const evidence::EvaluationRequest req = makeRequest(core::EvaluationMode::Verified);
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    // 证明素材在场并绑定请求切片（expectedSliceId——D-09 可校验）；
    // 无 payload 无搜索记录（互斥组合）。
    ASSERT_TRUE(out.proof.has_value());
    EXPECT_EQ(out.proof->category, evidence::ProofCategory::AnalyticBound);
    EXPECT_EQ(out.proof->sliceId, req.slice.sliceId);
    EXPECT_EQ(out.proof->snapshotId, req.snapshot.snapshotId);
    EXPECT_EQ(out.proof->producer, std::string(kin::kTaskPointIkEvaluationKey));
    EXPECT_FALSE(out.payload.has_value());
    EXPECT_FALSE(out.searchRecord.has_value());
}

// =====================================================================
// 取消轨——零素材输出（取消不是结局——§9.2）
// =====================================================================

TEST(KinTaskPointIkEval, CancellationYieldsZeroMaterial_WP15T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{"AT-34"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;

    TaskPointIkEvaluator evaluator(&view, makeQuery(transformOf(golden)));
    CancellingContext context;  // 首次探针即取消。
    const evidence::EvaluationRequest req = makeRequest(core::EvaluationMode::Verified);
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    EXPECT_FALSE(out.payload.has_value());
    EXPECT_FALSE(out.searchRecord.has_value());
    EXPECT_FALSE(out.proof.has_value());
    EXPECT_TRUE(out.diagnostics.empty());
    EXPECT_TRUE(out.evidence.empty());
}

// =====================================================================
// 确定性与模式敏感性（V-04 逐位＋V-16 本单元半区：mode 入身份）
// =====================================================================

TEST(KinTaskPointIkEval, DeterministicAndModeSensitivePayload_WP15T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-COR-01", "EVI-01"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const Mat4 golden = referenceFk(model, {0.3, -0.5}).tcp;
    TaskPointIkEvaluator evaluator(&view, makeQuery(transformOf(golden)));

    NoopContext context;
    const evidence::EvaluationOutput verified1 =
        evaluator.evaluate(makeRequest(core::EvaluationMode::Verified), context);
    const evidence::EvaluationOutput verified2 =
        evaluator.evaluate(makeRequest(core::EvaluationMode::Verified), context);
    const evidence::EvaluationOutput quick =
        evaluator.evaluate(makeRequest(core::EvaluationMode::Quick), context);

    // 同模式两次评估 → 字节逐位一致（V-04）。
    ASSERT_TRUE(verified1.payload.has_value());
    ASSERT_TRUE(verified2.payload.has_value());
    EXPECT_TRUE(verified1.payload->canonicalBytes == verified2.payload->canonicalBytes);

    // 模式差异反映在身份字节（mode 入请求身份——Quick/Preview 载荷以
    // mode 承载 screening-only 语义，§8.4；效力门禁在汇总/包络层）。
    ASSERT_TRUE(quick.payload.has_value());
    EXPECT_FALSE(verified1.payload->canonicalBytes == quick.payload->canonicalBytes)
        << "mode 必须进入结果身份字节（screening-only 承载面）";
}
