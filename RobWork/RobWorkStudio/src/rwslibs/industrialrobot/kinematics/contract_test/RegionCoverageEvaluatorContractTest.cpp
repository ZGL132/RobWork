/**
 * @file   RegionCoverageEvaluatorContractTest.cpp
 * @brief  kin.region-coverage 契约用例组（KinRegionCoverageEval）——
 *         区域覆盖评估器的评估通道行为：descriptor 形态（九条依赖含
 *         collision-models 条件依赖——V13-01 无布尔开关）、宿主注入工厂
 *         （create 无参——O-37）、装配期 fail-fast 轨、TCP 缺失零素材
 *         轨、载荷绑定块解析（§5.6 六要素）、确定性（逐位一致）、独立
 *         入口（generateSamples/computeCoverage——§9.2）与能力声明。
 *         任务契约 WP-15-T06 acceptance 逐条具名自证。
 *
 * 设计依据：
 *   - units/kinematics.md §4.3（kin.region-coverage 依赖声明行）、§7.2
 *     （覆盖率图——对账轨/零样本轨）、§8.3（能力声明——样本 watermark）、
 *     §9.2（IWorkspaceSampler 接口契约原文——三方法＋宿主注入形态）、
 *     §9.3（评估器不自我注册）
 *   - 治理裁决 O-37（宿主注入——TestView/替身求解器经工厂闭包注入，
 *     R-KIN-1"测试替身先行"口径）；O-38/P-KIN-3（冻结前以快照
 *     SamplingPlanRef 对账为准）；P-KIN-7（检查点通道最小端口）
 *   - 任务契约 tasks/foundation/WP-15-T06.json
 *
 * 测试策略：可控求解器替身（恒可达谓词——通道行为与几何解耦）；输出
 * 形状经 evaluate 端到端断言，采样语义经独立入口直调断言。
 */

#include "../test/KinFkFixture.hpp"

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>
#include <sdurws/ird/kinematics/Coverage.hpp>
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKin* 码值常量（禁拼码断言面）
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/kinematics/Sampling.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
namespace execution = sdurws::ird::execution;
namespace kin = sdurws::ird::kinematics;
using kin::RegionCoverageQuery;
using kin::RegionSamplingBudget;
using kin::SamplingPlan;

namespace {

constexpr double kPi = 3.14159265358979323846;

core::ObjectId regionId(const std::string& seed)
{
    return idFrom<core::ObjectId>("kin-rg-" + seed);
}
core::ContentIdentity planIdentityFrom(const std::string& seed)
{
    core::ContentIdentity id;
    id.bytes = digestOf("kin-rp-" + seed);
    return id;
}

/// 采样计划投影（默认 Grid 2×1×1＝2 位置样本；D=R=1）。
SamplingPlan makePlan(const std::string& seed)
{
    SamplingPlan plan;
    plan.regionObjectId = regionId(seed);
    plan.planContentIdentity = planIdentityFrom(seed);
    plan.box.center = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    plan.box.size = rw::math::Vector3D<double>(1.0, 1.0, 1.0);
    plan.position.gridCounts = {2U, 1U, 1U};
    return plan;
}

RegionSamplingBudget makeBudget(std::uint64_t seed = 42U)
{
    RegionSamplingBudget budget;
    budget.seed = seed;
    return budget;
}

/// 恒可达求解器替身（通道行为与几何解耦）。
class AlwaysFoundSolver final : public kin::IIkSolver {
public:
    kin::IkOutcome solve(const kin::IkRequest& request) const override
    {
        kin::IkOutcome outcome;
        outcome.outcomeKind = kin::IkOutcomeKind::SolutionsFound;
        kin::KinematicSolution s;
        s.q = {0.1, -0.2};
        s.positionResidual = 1e-9;
        s.orientationResidual = 1e-9;
        outcome.solutionSet.solutions.push_back(s);
        return outcome;
    }
};

/// 良构查询（二连杆视图下的默认——solver 必注入替身或内置求解器）。
RegionCoverageQuery makeQuery(std::vector<SamplingPlan> plans, kin::IIkSolver* solver)
{
    RegionCoverageQuery q;
    q.plans = std::move(plans);
    q.defaultTcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    q.referenceQ = {0.0, 0.0};
    q.budget = makeBudget();
    q.solver = solver;
    return q;
}

/// 与评估器重算一致的快照冻结凭据。
evidence::SamplingPlanRef makeRef(const SamplingPlan& plan,
                                  const RegionSamplingBudget& budget)
{
    evidence::SamplingPlanRef ref;
    ref.regionObjectId = plan.regionObjectId;
    ref.planContentIdentity = plan.planContentIdentity;
    ref.plannedPositionSamples = kin::plannedPositionSampleCount(plan);
    ref.plannedPoseSamples = kin::plannedPoseSampleCount(plan);
    ref.sampleSetIdentity = kin::sampleSetIdentity(plan.planContentIdentity, budget);
    return ref;
}

evidence::EvaluationRequest makeRequest(std::vector<evidence::SamplingPlanRef> refs)
{
    evidence::EvaluationRequest req;
    req.mode = core::EvaluationMode::Verified;
    req.snapshot.snapshotId.bytes = digestOf("kin-cov-snap");
    req.snapshot.samplingPlans = std::move(refs);
    req.slice.sliceId.bytes = digestOf("kin-cov-slice");
    req.task.attempt.value = 5U;
    return req;
}

/// 载荷字节流顺序读取器（同 RegionCoverageTest——布局漂移即断言失败）。
class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& bytes) : m_bytes(bytes) {}

    std::uint8_t u8() { return m_bytes.at(m_pos++); }
    std::uint32_t u32()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(m_bytes.at(m_pos++)) << (8 * i);
        }
        return v;
    }
    std::uint64_t u64()
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(m_bytes.at(m_pos++)) << (8 * i);
        }
        return v;
    }
    std::vector<std::uint8_t> raw(std::size_t n)
    {
        std::vector<std::uint8_t> out(m_bytes.begin() + m_pos,
                                      m_bytes.begin() + m_pos + n);
        m_pos += n;
        return out;
    }
    std::string str(std::size_t n)
    {
        std::string out(m_bytes.begin() + m_pos, m_bytes.begin() + m_pos + n);
        m_pos += n;
        return out;
    }

private:
    const std::vector<std::uint8_t>& m_bytes;
    std::size_t m_pos = 0;
};

}  // namespace

// =====================================================================
// descriptor——§4.3 kin.region-coverage 行（九条依赖含条件依赖/双模式/
// 无状态/完全线程安全；键词形 kebab 随附同步偏差——T03~T05 同口径）
// =====================================================================

TEST(KinRegionCoverageEval, DescriptorMatchesSection43Row_WP15T06_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{});

    const evidence::EvaluatorDescriptor d = kin::makeRegionCoverageDescriptor();

    // 键（实现键 kebab 形态——卡面点形键的随附同步偏差）与契约版本。
    EXPECT_EQ(d.key, std::string(kin::kRegionCoverageEvaluationKey));
    EXPECT_EQ(d.key, "kin-region-coverage");
    EXPECT_EQ(d.contractVersion, kin::kRegionCoverageContractVersion);

    // 依赖声明九条（§4.3 行原文：八条 Required＋collision-models 一条
    // Conditional——条件依赖语义＝碰撞启用状态只读自 policy，策略未启用
    // 碰撞时该键不进切片；本单元不设碰撞布尔开关，V13-01）。
    ASSERT_EQ(d.inputs.size(), 9U);
    EXPECT_EQ(d.inputs[0].key, "model.robot-design");
    EXPECT_EQ(d.inputs[1].key, "tcp");
    EXPECT_EQ(d.inputs[2].key, "req.regions");
    EXPECT_EQ(d.inputs[3].key, "req.sampling-plans");
    EXPECT_EQ(d.inputs[4].key, "req.conditions");
    EXPECT_EQ(d.inputs[5].key, "policy.resolved");
    EXPECT_EQ(d.inputs[6].key, "namemap");
    EXPECT_EQ(d.inputs[7].key, "config.ik");
    EXPECT_EQ(d.inputs[8].key, "collision-models");
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(d.inputs[i].requiredness,
                  evidence::DependencyRequiredness::Required);
    }
    EXPECT_EQ(d.inputs[8].requiredness,
              evidence::DependencyRequiredness::Conditional);
    // 条件语义在 resolutionNote 登记可读（人工评审面）。
    EXPECT_NE(d.inputs[8].resolutionNote.find("V13-01"), std::string::npos);

    // Profile 声明（域不可申报——contentIdentity 零值，evidence §9.5/R-3）。
    EXPECT_EQ(d.profile.profileId, "kin");
    EXPECT_EQ(d.profile.version, "1");
    EXPECT_FALSE(d.profile.contentIdentity.isValid());

    // 模式集两值（Quick/Verified——§4.3 行；覆盖通道不做 Preview）。
    ASSERT_EQ(d.supportedModes.size(), 2U);
    EXPECT_EQ(d.supportedModes[0], core::EvaluationMode::Quick);
    EXPECT_EQ(d.supportedModes[1], core::EvaluationMode::Verified);

    // stateless＋FullyThreadSafe（逐样本并行分片要求可重入——§9.2 头注）。
    EXPECT_TRUE(d.stateless);
    EXPECT_EQ(d.threadSafety, evidence::ThreadSafety::FullyThreadSafe);
}

// =====================================================================
// O-37 宿主注入工厂——create() 无参签名；每次调用产出独立实例
// =====================================================================

TEST(KinRegionCoverageEval, FactoryHostInjectionCreateNoArg_WP15T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    AlwaysFoundSolver solver;
    const SamplingPlan plan = makePlan("factory");
    const RegionCoverageQuery query = makeQuery({plan}, &solver);

    kin::WorkspaceSamplerFactory factory(&view, query);
    const evidence::EvaluatorDescriptor desc = factory.descriptor();
    EXPECT_EQ(desc.key, std::string(kin::kRegionCoverageEvaluationKey));

    // create() 无参签名（O-37 裁决——注册表路径兼容）；产出实例的
    // descriptor 与工厂一致。
    std::unique_ptr<evidence::IEngineeringEvaluator> a = factory.create();
    std::unique_ptr<evidence::IEngineeringEvaluator> b = factory.create();
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    EXPECT_EQ(a->descriptor().key, desc.key);
    EXPECT_EQ(a->descriptor().contractVersion, desc.contractVersion);
    EXPECT_NE(a.get(), b.get());  // 独立实例（无共享可变状态的前提面）
}

// =====================================================================
// 装配期 fail-fast 轨（调用方错误不进入评估输出面——NFR-COR-03）
// =====================================================================

TEST(KinRegionCoverageEval, AssemblyFailFastInvalidQueries_WP15T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    AlwaysFoundSolver solver;
    const SamplingPlan plan = makePlan("failfast");

    // 空视图＝宿主注入契约违约（O-37）。
    EXPECT_THROW(kin::WorkspaceSampler(nullptr, makeQuery({plan}, &solver)),
                 std::invalid_argument);

    // seed=0（I-KIN-4——采样种子是样本集身份的决定输入，拒绝不静默替换）。
    RegionCoverageQuery zeroSeed = makeQuery({plan}, &solver);
    zeroSeed.budget.seed = 0U;
    EXPECT_THROW(kin::WorkspaceSampler(&view, zeroSeed), std::invalid_argument);

    // Box 非退化违例（I-REQ-6——size 分量 ≤0/非有限）。
    SamplingPlan degenerate = makePlan("degenerate");
    degenerate.box.size = rw::math::Vector3D<double>(0.0, 1.0, 1.0);
    EXPECT_THROW(kin::WorkspaceSampler(&view, makeQuery({degenerate}, &solver)),
                 std::invalid_argument);

    // 姿态采样计数 ≥1（requirements 字段表原文）。
    SamplingPlan noDirection = makePlan("nodir");
    noDirection.orientation.directionSamples = 0U;
    EXPECT_THROW(kin::WorkspaceSampler(&view, makeQuery({noDirection}, &solver)),
                 std::invalid_argument);

    // referenceQ 维度违约（D-KIN-4 显式输入契约面）。
    RegionCoverageQuery badRef = makeQuery({plan}, &solver);
    badRef.referenceQ = {0.0};
    EXPECT_THROW(kin::WorkspaceSampler(&view, badRef), std::invalid_argument);

    // tcpKey 不命中（帧未解析——装配期 fail-fast，工具可解析时）。
    RegionCoverageQuery badKey = makeQuery({plan}, &solver);
    badKey.defaultTcp.tcpKey = "no-such-tcp";
    EXPECT_THROW(kin::WorkspaceSampler(&view, badKey), std::invalid_argument);
}

// =====================================================================
// TCP 缺失批量级零素材轨（T03/T04/T05 两分口径同源——模型侧缺失≠工程
// 不可行，判定留 evidence）
// =====================================================================

TEST(KinRegionCoverageEval, TcpMissingZeroMaterialTrack_WP15T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{});

    // 无工具模型（KIN-NO-TCP"未配置"分支的测试面）。
    const rt::CanonicalModel model = makeModel(
        {revolute("j1", rw::math::Vector3D<double>(0, 0, 1), trans(0, 0, 0),
                  -2.0, 2.0),
         revolute("j2", rw::math::Vector3D<double>(0, 0, 1), trans(1.0, 0, 0),
                  -3.14, 3.14)},
        trans(0.5, 0, 0), trans(0, 0, 0), /*withTool=*/false);
    TestView view(model);
    NoopContext context;

    AlwaysFoundSolver solver;
    const SamplingPlan plan = makePlan("notcp");
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    kin::WorkspaceSampler evaluator(&view, query);
    const evidence::EvaluationOutput out =
        evaluator.evaluate(makeRequest({makeRef(plan, query.budget)}), context);

    // 零素材：KIN-NO-TCP 诊断一条，零 payload/零证据行。
    ASSERT_EQ(out.diagnostics.size(), 1U);
    EXPECT_EQ(out.diagnostics.front().code, std::string(kin::kKinNoTcp));
    EXPECT_FALSE(out.payload.has_value());
    EXPECT_TRUE(out.evidence.empty());
}

// =====================================================================
// ACC3（契约面）——身份对账失败＝评估器零素材轨（V-15"错误码；无覆盖
// 率输出"）；对照一致凭据正常产出
// =====================================================================

TEST(KinRegionCoverageEval, IdentityReconciliationContractTrack_WP15T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "CON-04"},
                  std::vector<std::string>{"V-15"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    NoopContext context;
    AlwaysFoundSolver solver;
    const SamplingPlan plan = makePlan("acc3");
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    kin::WorkspaceSampler evaluator(&view, query);

    // 未冻结（快照无 ref）→ KIN-SAMPLE-IDENTITY-MISMATCH＋零素材。
    const evidence::EvaluationOutput outUnfrozen =
        evaluator.evaluate(makeRequest({}), context);
    ASSERT_EQ(outUnfrozen.diagnostics.size(), 1U);
    EXPECT_EQ(outUnfrozen.diagnostics.front().code,
              std::string(kin::kKinSampleIdentityMismatch));
    EXPECT_FALSE(outUnfrozen.payload.has_value());
    EXPECT_TRUE(outUnfrozen.evidence.empty());

    // 一致凭据 → 正常产出且零诊断（对账轨不触发）。
    const evidence::EvaluationOutput outOk =
        evaluator.evaluate(makeRequest({makeRef(plan, query.budget)}), context);
    EXPECT_TRUE(outOk.payload.has_value());
    EXPECT_EQ(outOk.diagnostics.size(), 0U);
    ASSERT_EQ(outOk.evidence.size(), 1U);
    EXPECT_EQ(outOk.evidence.front().subject, plan.regionObjectId);
}

// =====================================================================
// 载荷绑定块解析（§5.6 六要素逐字段——布局漂移即断言失败）
// =====================================================================

TEST(KinRegionCoverageEval, PayloadBindingBlockParse_WP15T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "CON-05"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    NoopContext context;
    AlwaysFoundSolver solver;
    const SamplingPlan plan = makePlan("bind");
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    const evidence::EvaluationRequest req =
        makeRequest({makeRef(plan, query.budget)});
    kin::WorkspaceSampler evaluator(&view, query);
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    ASSERT_TRUE(out.payload.has_value());
    ByteReader r(out.payload->canonicalBytes);

    // 头（magic＋codec）＋标记块。
    EXPECT_EQ(r.str(7), "IRDCV01");
    EXPECT_EQ(r.u32(), 1U);
    EXPECT_EQ(r.u8(), 0U);  // incomplete
    EXPECT_EQ(r.u8(), 0U);  // downgraded（恒可达、无碰撞要求）
    EXPECT_EQ(r.u8(), 1U);  // positionDefined
    EXPECT_EQ(r.u8(), 1U);  // orientationDefined

    // 绑定块（§5.6 六要素——逐字段与请求同源比对）。
    std::vector<std::uint8_t> snap = r.raw(32);
    EXPECT_EQ(snap, std::vector<std::uint8_t>(req.snapshot.snapshotId.bytes.begin(),
                                              req.snapshot.snapshotId.bytes.end()));
    std::vector<std::uint8_t> slice = r.raw(32);
    EXPECT_EQ(slice, std::vector<std::uint8_t>(req.slice.sliceId.bytes.begin(),
                                               req.slice.sliceId.bytes.end()));
    std::vector<std::uint8_t> cfg = r.raw(32);  // configDigest（T10 前零值）
    EXPECT_EQ(cfg, std::vector<std::uint8_t>(32, 0U));
    EXPECT_EQ(r.u8(), static_cast<std::uint8_t>(core::EvaluationMode::Verified));
    EXPECT_EQ(r.u64(), query.budget.seed);
    EXPECT_EQ(r.u32(), 2U);  // referenceQ 计数＝设备自由度（makeQuery 已设 {0,0}）
    r.raw(2 * 8);            // referenceQ 值（逐自由度 f64——布局占位读取）
    // 任务五元组（4×16B＋attempt u64）。
    r.raw(64 + 8);
    // evaluationKey＋契约版本。
    const std::uint32_t keyLen = r.u32();
    EXPECT_EQ(r.str(keyLen), "kin-region-coverage");
    EXPECT_EQ(r.u32(), kin::kRegionCoverageContractVersion);

    // 计划块（1 计划：身份三元组＋matched＋分母两值）。
    EXPECT_EQ(r.u32(), 1U);
    std::vector<std::uint8_t> oid = r.raw(16);
    EXPECT_EQ(oid, std::vector<std::uint8_t>(plan.regionObjectId.bytes.begin(),
                                             plan.regionObjectId.bytes.end()));
    std::vector<std::uint8_t> pid = r.raw(32);
    EXPECT_EQ(pid, std::vector<std::uint8_t>(plan.planContentIdentity.bytes.begin(),
                                             plan.planContentIdentity.bytes.end()));
    EXPECT_EQ(r.raw(32).size(), 32U);  // computedIdentity（重算值在场）
    EXPECT_EQ(r.u8(), 1U);             // matched
    EXPECT_EQ(r.u64(), 2U);            // plannedPositionSamples
    EXPECT_EQ(r.u64(), 2U);            // plannedPoseSamples

    // 覆盖计数块（双口径各六计数）。
    EXPECT_EQ(r.u64(), 2U);  // position.planned
    EXPECT_EQ(r.u64(), 2U);  // position.reached
    EXPECT_EQ(r.u64(), 0U);
    EXPECT_EQ(r.u64(), 0U);
    EXPECT_EQ(r.u64(), 0U);
    EXPECT_EQ(r.u64(), 0U);
    EXPECT_EQ(r.u64(), 2U);  // orientation.planned
    EXPECT_EQ(r.u64(), 2U);  // orientation.reached
    EXPECT_EQ(r.u64(), 0U);
    EXPECT_EQ(r.u64(), 0U);
    EXPECT_EQ(r.u64(), 0U);
    EXPECT_EQ(r.u64(), 0U);

    // 逐样本状态表（4 条：2 位置＋2 位姿——全 Reached）。
    EXPECT_EQ(r.u32(), 4U);
}

// =====================================================================
// 确定性（§8.4/acceptance 4）——同输入两次评估 payload 逐位一致
// =====================================================================

TEST(KinRegionCoverageEval, DeterminismPayloadBitwise_WP15T06_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    AlwaysFoundSolver solver;
    const SamplingPlan plan = makePlan("determ");
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    const evidence::EvaluationRequest req =
        makeRequest({makeRef(plan, query.budget)});

    NoopContext ctx1;
    NoopContext ctx2;
    kin::WorkspaceSampler evaluator(&view, query);
    const evidence::EvaluationOutput a = evaluator.evaluate(req, ctx1);
    const evidence::EvaluationOutput b = evaluator.evaluate(req, ctx2);
    ASSERT_TRUE(a.payload.has_value());
    ASSERT_TRUE(b.payload.has_value());
    ASSERT_EQ(a.payload->canonicalBytes.size(), b.payload->canonicalBytes.size());
    EXPECT_TRUE(a.payload->canonicalBytes == b.payload->canonicalBytes);
    EXPECT_TRUE(a.payload->digest == b.payload->digest);
}

// =====================================================================
// §9.2 独立入口——generateSamples（单计划面/局部序）＋computeCoverage
// （唯一实现点委托）；Expected ok 轨
// =====================================================================

TEST(KinRegionCoverageEval, StandaloneEntriesGenerateAndCoverage_WP15T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    AlwaysFoundSolver solver;
    const SamplingPlan plan = makePlan("standalone");
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    kin::WorkspaceSampler sampler(&view, query);

    // generateSamples：单计划面——计划内局部序 0 起（ok 轨值面）。
    const auto generated = sampler.generateSamples(plan, query.budget);
    ASSERT_TRUE(generated.ok());
    EXPECT_EQ(generated.get().samples.size(), 4U);
    EXPECT_EQ(generated.get().plannedPositionSamples, 2U);
    EXPECT_EQ(generated.get().plannedPoseSamples, 2U);
    EXPECT_EQ(generated.get().samples.front().sampleIndex, 0U);

    // computeCoverage：唯一实现点委托（全 Reached——双口径全达）。
    kin::SampleResultSet results;
    for (const auto& s : generated.get().samples) {
        kin::SampleResultRecord r;
        r.sampleIndex = s.sampleIndex;
        r.state = kin::SampleState::Reached;
        results.results.push_back(r);
    }
    const kin::CoverageResult coverage =
        sampler.computeCoverage(generated.get(), results);
    EXPECT_EQ(coverage.position.reached, 2U);
    EXPECT_EQ(coverage.orientation.reached, 2U);
    EXPECT_TRUE(coverage.positionDefined);
    EXPECT_TRUE(coverage.orientationDefined);
    EXPECT_FALSE(coverage.downgraded);
}

// =====================================================================
// 能力声明（§8.3 提交行"样本 watermark（coverage）"——execution 通道）
// =====================================================================

TEST(KinRegionCoverageEval, CapabilityDeclarationValues_WP15T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-02"},
                  std::vector<std::string>{});

    const execution::TaskCapability capability = kin::regionCoverageCapability();
    EXPECT_FALSE(capability.supportsPause);  // 暂停不支持（R1 如实声明）
    EXPECT_EQ(capability.checkpointGranularity,
              execution::CheckpointGranularity::Sample);  // 样本 watermark
    EXPECT_EQ(capability.forceTerminateCost,
              execution::ForceTerminateCost::Cheap);  // 样本批边界即安全中止点
}
