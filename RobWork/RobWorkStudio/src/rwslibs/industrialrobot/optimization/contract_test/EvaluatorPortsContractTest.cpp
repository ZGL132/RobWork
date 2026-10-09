/**
 * @file   EvaluatorPortsContractTest.cpp
 * @brief  批量编排缓存消费面契约测试（OptEvaluatorPortsContract 组）——
 *         optimization 编排边界 ↔ execution 缓存治理 ↔ evidence judgeCacheHit
 *         判定单点的跨单元联合验证：Quick↔Verified 缓存双向 Incompatible
 *         （D-13/EV-CPA-1——无隐式升降级）、会话内 FullHit 短路径回放
 *         （OPT-VER-137——命中≠Current）、部分结果不作正式命中（CON-04）、
 *         配置变化 ⇒ slice-mismatch 重算（OPT-VER-138）——任务契约
 *         WP-20-T06 acceptance 1/2。
 *
 * 设计依据：
 *   - units/optimization.md §8.4（缓存键与命中判定"不复制第二套兼容逻辑"
 *     ——命中判定唯一归 evidence judgeCacheHit；存储治理归 execution
 *     IExecutionCacheCoordinator；optimization 只做编排查找请求/消费命中
 *     结果/统计命中计数；"Quick 缓存命中不能自动升级为 Verified（mode-
 *     mismatch ⇒ Incompatible）"；"失败、取消、中断和部分结果不能作为
 *     完整缓存命中"）、§4.5（"缓存命中不代表结果 Current——两回事：命中
 *     只声明对该键可复用；当前性由 evidence computeCurrentness 另行计算"
 *     ——Superseded 的历史缓存照样可命中同键请求）
 *   - 需求 OPT-06（B 子集缓存语义）、CON-04（缓存按契约判定；部分/失败
 *     结果不得作为正式缓存命中）、NFR-COR-02（D-13 模式不隐式升降级）
 *   - evidence 公共契约 Compatibility.hpp（judgeCacheHit 三档六原因——
 *     本测试零复制判定，直接消费单点并经编排边界观测消费行为）
 *   - 用例矩阵：OPT-VER-129/137/138（§13.1——契约测试类型）
 *
 * 测试形态（契约测试＝跨单元联合）：真 evidence::EvaluatorRegistry（评估器
 * 替身注册）＋真 execution::RunRegistry/ExecutionCacheCoordinator（缓存条目
 * 经接纳镜像门槛登记——写入双门槛 CON-04 的真实路径）＋T04 管线（StageB）
 * ＋脚本化候选投影。缓存写入面（storeResult）由测试模拟 execution 接纳
 * 编排完成（登记→Completed 镜像→Archived 镜像→storeResult）——optimization
 * 编排器自身零存储调用（§8.4 编排三步边界的负向断言由结构承载）。
 */

#include <sdurws/ird/optimization/EvaluatorPorts.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Compatibility.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/execution/CacheCoordinator.hpp>
#include <sdurws/ird/execution/RunRegistry.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Export.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Pareto.hpp>
#include <sdurws/ird/optimization/Run.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::CandidateProjection;
using optimization::CandidateStatus;
using optimization::OptimizationConfiguration;
using optimization::OptimizationStage;
using optimization::StaticHardConstraintDeps;
using optimization::StaticHardConstraintPipeline;
using optimization::TwoStageEvaluationOrchestrator;
using optimization::TwoStageOrchestratorDeps;
using optimization::TwoStageRunRecord;
using optimization::TwoStageRunRequest;
using optimization::TwoStageRunResult;
using optimization::VariableBinding;
using optimization::VariableKind;

namespace {

namespace fs = std::filesystem;

// =====================================================================
// 通用替身（模型测试 EvaluatorPortsTest 同款形态——契约测试自持）
// =====================================================================

class AllPresentClosure final : public evidence::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

class NopEvaluationContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }
};

core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

evidence::AnalysisSnapshot makeSnapshot(const std::vector<evidence::CaseEntry>& cases)
{
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    evidence::ObjectRefEntry design;
    design.objectId = core::ObjectId::generate();
    design.contentVersion = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000a1");
    design.objectTypeToken = "robot-design";
    design.digest = design.contentVersion.bytes;
    builder.addObjectRef(design);
    evidence::PolicyRef policy;
    policy.policyContentIdentity = makeContentIdentity(4);
    builder.setPolicyRef(policy);
    evidence::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = makeContentIdentity(5);
    builder.setNameMapRef(nameMap);
    for (const auto& c : cases) {
        builder.addCase(c);
    }
    evidence::ReproductionBlock repro;
    repro.productVersion = "test";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    return builder.build(AllPresentClosure{});
}

evidence::CaseCoverageMatrix
makeCoverage(const evidence::AnalysisSnapshot& snapshot)
{
    evidence::CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const auto& entry : snapshot.caseSet.entries) {
        evidence::CaseCoverageEntry row;
        row.caseId = entry.caseId;
        row.status = evidence::CaseExecutionStatus::Executed;
        row.runId = core::RunId::generate();
        row.resultSliceId = makeContentIdentity(6);
        matrix.entries.push_back(std::move(row));
    }
    return matrix;
}

std::vector<evidence::CaseEntry> makeTwoMandatoryCases()
{
    return {{core::ObjectId::generate(), "额定工况", true, true},
            {core::ObjectId::generate(), "极限工况", true, true}};
}

/// 带评估调用计数的脚本化 kin 评估器（计数经共享原子——factory 每次新建
/// 实例，计数落在脚本对象上以统计全链调用次数）。
class CountingKinEvaluator final : public evidence::IEngineeringEvaluator {
public:
    CountingKinEvaluator(evidence::EvaluatorDescriptor descriptor,
                         std::shared_ptr<std::atomic<int>> counter)
        : m_descriptor(std::move(descriptor)), m_counter(std::move(counter))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override
    {
        m_counter->fetch_add(1);  // 评估调用计数（缓存回放不触发的观测面）
        return {};
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
    std::shared_ptr<std::atomic<int>> m_counter;
};

class CountingKinFactory final : public evidence::IEvaluatorFactory {
public:
    CountingKinFactory(evidence::EvaluatorDescriptor descriptor,
                       std::shared_ptr<std::atomic<int>> counter)
        : m_descriptor(std::move(descriptor)), m_counter(std::move(counter))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<CountingKinEvaluator>(m_descriptor, m_counter);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
    std::shared_ptr<std::atomic<int>> m_counter;
};

evidence::EvaluatorDescriptor makeKinDescriptor(const std::string& key)
{
    evidence::EvaluatorDescriptor d;
    d.key = key;
    d.contractVersion = 1U;
    d.profile.profileId = "kin";
    d.profile.version = "1.0";
    d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
    return d;
}

void registerKinProfile(evidence::EvidenceProfileRegistry& profiles)
{
    evidence::RequiredEvidenceProfile profile;
    profile.profileId = "kin";
    profile.version = "1.0";
    profiles.registerProfile(profile);
}

class FakeJointLimitProbe final : public optimization::IJointLimitProbe {
public:
    optimization::JointLimitProbeResult result;
    optimization::JointLimitProbeResult
    probeJointLimits(const optimization::JointLimitProbeRequest&) const override
    {
        return result;
    }
};

class FakeCollisionProbe final : public optimization::IStaticCollisionProbe {
public:
    optimization::StaticCollisionProbeResult result;
    optimization::StaticCollisionProbeResult probeSampleSet(
        const std::vector<std::vector<double>>&) const override
    {
        return result;
    }
};

class FakeCompiler final : public optimization::ICandidateCompiler {
public:
    optimization::CandidateCompileResult result;
    FakeCompiler()
    {
        result.ok = true;
    }
    optimization::CandidateCompileResult
    compile(const CandidatePatch&, const std::vector<VariableBinding>&) const override
    {
        return result;
    }
};

/// 候选投影替身（包络随补丁值线性变化——候选可区分）。
class ScriptedProjector final : public optimization::ICandidateProjector {
public:
    mutable int calls = 0;

    CandidateProjection
    project(const CandidatePatch& patch, const std::vector<VariableBinding>&) const override
    {
        ++calls;
        CandidateProjection p;
        p.baselineChainInEnabledScope = true;
        optimization::JointLimitSpecRecord joint;
        joint.jointSubject = "obj-probe-joint";
        joint.qmin = -3.14;  // rad
        joint.qmax = 3.14;   // rad
        p.jointLimits.joints = {joint};
        p.jointLimits.configurations = {{0.0}};
        double v = 0.5;
        if (!patch.items.empty()) {
            v = patch.items.front().scalarValue;
        }
        p.metricFacts.envelope = optimization::EnvelopeFacts{-v, -v, -v, v, v, v};
        optimization::LinkMassFact lm;
        lm.linkSubject = "obj-link";
        lm.massKg = 10.0;  // kg
        lm.provenanceToken = "provided";
        p.metricFacts.linkMasses = {lm};
        optimization::PointMarginFact pm;
        pm.caseIdText = "case-margin";
        pm.minMargin = 0.4;  // D-KIN-2 归一化裕量（无量纲）
        pm.unitSymbol = "1";
        p.metricFacts.pointMargins = {pm};
        return p;
    }
};

// =====================================================================
// execution 缓存底座（RunRegistry/协调器接纳镜像门槛——EX-CCH 存储面真链）
// =====================================================================

/// 开发诊断收集 sink（门槛拒绝留痕观测——CON-04 执行侧的旁证）。
class CollectingSink final : public execution::IExecutionDiagnosticsSink {
public:
    std::vector<std::string> devChannels;
    void report(const core::DiagnosticRecord&) override {}
    void reportDev(const std::string& channel, const std::string&) override
    {
        devChannels.push_back(channel);
    }
    bool hasDevChannel(std::string_view channel) const
    {
        return std::any_of(devChannels.begin(), devChannels.end(),
                           [&](const std::string& c) { return c == channel; });
    }
};

/// 归档网关桩（storeResult 路径不触碰归档端口——登记面要求非空而已；
/// execution CheckpointCacheTest 同款形态）。
class NullGateway final : public execution::IRunArchiveGateway {
public:
    project::IResultArchivePort& port() const override
    {
        static NullArchivePort port;
        return port;
    }
    bool reacquireWriteAuthority() override { return false; }

private:
    class NullArchivePort final : public project::IResultArchivePort {
    public:
        project::ArchiveSessionRef begin(const project::ArchiveRequest&) override
        {
            throw std::runtime_error("null gateway port 不应被调用");
        }
        project::ArchiveStatus writeBatch(project::ArchiveSessionRef,
                                          const project::ArchiveBatch&) override
        {
            return {false, std::nullopt};
        }
        project::ArchiveStatus finalize(project::ArchiveSessionRef,
                                        const project::RunManifest&) override
        {
            return {false, std::nullopt};
        }
        void abandon(project::ArchiveSessionRef, project::ArchiveEndReason) override {}
    };
};

// =====================================================================
// 契约 fixture（编排底座＋缓存底座＋接纳模拟）
// =====================================================================

struct CacheOrchestratorFixture {
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    std::shared_ptr<std::atomic<int>> evalCalls = std::make_shared<std::atomic<int>>(0);
    FakeJointLimitProbe jointProbe;
    FakeCollisionProbe collisionProbe;
    FakeCompiler compiler;
    NopEvaluationContext ctx;
    ScriptedProjector projector;
    optimization::ParetoFrontBuilder paretoBuilder;
    core::ContentIdentity profileIdentity;
    StaticHardConstraintPipeline pipeline;
    CollectingSink sink;
    execution::RunRegistry registry;
    execution::ExecutionCacheCoordinator cache;

    CacheOrchestratorFixture()
        : profileIdentity(makeContentIdentity(0))
        , pipeline(OptimizationStage::StageB, makeDeps())
        , registry(nullptr)
        , cache(registry, sink, nullptr)
    {
        registerKinProfile(profiles);
        evaluators.registerEvaluator(
            std::make_unique<CountingKinFactory>(
                makeKinDescriptor(std::string(optimization::kKinTaskPointsBatchKey)),
                evalCalls),
            {});
        evaluators.registerEvaluator(
            std::make_unique<CountingKinFactory>(
                makeKinDescriptor(std::string(optimization::kKinRegionCoverageKey)),
                evalCalls),
            {});
        jointProbe.result.completed = true;
        collisionProbe.result.completed = true;
        profileIdentity = profiles.findProfile("kin", "1.0")->contentIdentity;
    }

    StaticHardConstraintDeps makeDeps()
    {
        StaticHardConstraintDeps d;
        d.evaluators = &evaluators;
        d.jointLimitProbe = &jointProbe;
        d.collisionProbe = &collisionProbe;
        d.compiler = &compiler;
        d.producers = &evaluators;
        d.profiles = &profiles;
        return d;
    }

    TwoStageOrchestratorDeps deps()
    {
        TwoStageOrchestratorDeps d;
        d.pipeline = &pipeline;
        d.projector = &projector;
        d.paretoBuilder = &paretoBuilder;
        d.cache = &cache;  // 契约测试接入真协调器（存储治理归 execution）
        return d;
    }

    /// 模拟 execution 接纳路径登记一条正式缓存条目（registerRun→Completed
    /// 镜像→Archived 镜像→storeResult——写入双门槛 CON-04 的真实链）。
    /// @param baselineId [in] 登记的输入基准身份（命中≠Current 用例以此
    ///        构造"旧基准条目"——evidence Superseded 场景的缓存侧形态）。
    void admitCacheEntry(core::EvaluationMode mode, const core::ContentIdentity& sliceId,
                         const core::ContentIdentity& baselineId)
    {
        core::TaskIdentity identity{core::ProjectId::generate(), core::BranchId::generate(),
                                    core::RevisionId::generate(), core::RunId::generate(),
                                    core::AttemptId{1U}};
        execution::RegistrationInput in;
        in.identity = identity;
        in.evaluatorKey = std::string(optimization::kOptStaticScreenKey);
        in.contractVersion = optimization::kOptStaticScreenContractVersion;
        in.snapshotId = makeContentIdentity(7);
        in.sliceId = sliceId;
        in.inputBaselineId = baselineId;
        in.policyIdentity = makeContentIdentity(8);
        in.nameMapIdentity = makeContentIdentity(9);
        in.mode = mode;
        in.runDir = fs::path{"results"} / identity.run.toCanonical();
        in.runKind = "opt-eval";
        in.allowedResultKinds = {execution::ResultKind::FinalEnvelope};
        in.currentState = core::TaskState::Running;

        auto materials = std::make_shared<execution::RunEvaluationMaterials>();
        materials->producers = std::make_shared<NoopProducers>();
        materials->profiles = std::make_shared<NoopProfiles>();
        materials->manifestProfile
            = evidence::EvidenceProfileRef{"kin", "1.0", profileIdentity};
        in.resources.evaluation = std::move(materials);
        in.resources.archive = std::make_shared<NullGateway>();
        registry.registerRun(execution::TaskId::generate(), in);

        registry.noteAdmissionCompleted(identity.run);
        registry.noteArchivePhase(identity.run, execution::ArchivePhase::Archived);
        cache.storeResult(identity.run);  // 双门槛通过——条目入正式缓存
    }

    /// 部分/失败条目登记（CON-04 诊断账户——outcome=Canceled＋未封账）。
    void admitPartialEntry(const core::ContentIdentity& sliceId)
    {
        execution::CacheEntrySummary entry;
        entry.run = core::RunId::generate();
        entry.summary.mode = core::EvaluationMode::Quick;
        entry.summary.outcome = core::TaskOutcome::Canceled;   // 未完成——CON-04
        entry.summary.manifestFinalized = false;               // 未封账——CON-04
        entry.summary.sliceId = sliceId;
        entry.summary.evaluatorContractVersion
            = optimization::kOptStaticScreenContractVersion;
        entry.summary.profileContentIdentity = profileIdentity;
        cache.storePartialDiagnostic(std::move(entry));
    }

    VariableBinding lengthBinding()
    {
        VariableBinding b;
        b.bindingId = "mdl.joint[2].dh.a";
        b.kind = VariableKind::Continuous;
        b.unit = core::UnitToken::find("m").value();
        b.lowerBound = 0.2;   // m
        b.upperBound = 0.8;   // m
        b.defaultValue = 0.5; // m
        b.authorized = true;
        b.locked = false;
        b.authorityFieldPath = "robot-design/joints[2]/dh/a";
        b.diagSubject = core::ObjectId::generate().toCanonical();
        return b;
    }

    OptimizationConfiguration makeConfig(std::uint32_t maxCandidates,
                                         std::uint32_t maxVerified, std::uint64_t seed)
    {
        OptimizationConfiguration c;
        c.stage = OptimizationStage::StageB;
        c.seed = seed;
        c.budget.maxCandidates = maxCandidates;
        c.budget.maxVerifiedCandidates = maxVerified;
        c.strategyId = std::string(optimization::kStrategySeededLhs);
        c.objectives = optimization::defaultObjectives(OptimizationStage::StageB);
        c.variables = {lengthBinding()};
        optimization::validateConfiguration(c);
        return c;
    }

    TwoStageRunRequest readyRequest(const OptimizationConfiguration& cfg)
    {
        const auto cases = makeTwoMandatoryCases();
        TwoStageRunRequest r;
        r.config = cfg;
        r.baselineRoot = core::ObjectId::generate();
        r.baselineCv = core::ContentVersion::fromCanonical(
            "cv-00000000000000000000000000000000000000000000000000000000000000bb");
        r.snapshot = makeSnapshot(cases);
        r.task = {core::ProjectId::generate(), core::BranchId::generate(),
                  core::RevisionId::generate(), core::RunId::generate(),
                  core::AttemptId{1U}};
        r.profile.profileId = "kin";
        r.profile.version = "1.0";
        r.profile.contentIdentity = profileIdentity;
        r.coverage = makeCoverage(r.snapshot);
        r.quickCoverage = makeCoverage(r.snapshot);
        return r;
    }

private:
    /// 注册表投影空实现（接纳事实面登记必填——本测试接纳路径不走证明校验）。
    class NoopProducers final : public evidence::IProducerRegistryView {
    public:
        bool isRegistered(std::string_view) const override { return false; }
        bool contractVersionMatches(std::string_view, std::uint32_t) const override
        {
            return false;
        }
    };
    class NoopProfiles final : public evidence::IProfileRegistryView {
    public:
        const evidence::RequiredEvidenceProfile*
        findProfile(std::string_view, std::string_view) const override
        {
            return nullptr;
        }
    };
};

}  // namespace

// =====================================================================
// OPT-VER-129 / D-13 / EV-CPA-1：Quick↔Verified 缓存双向 Incompatible
// =====================================================================

TEST(OptEvaluatorPortsContract, QuickCacheNeverUpgradesToVerified_WP20T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "CON-04"},
                  std::vector<std::string>{"AT-09"});

    // OPT-VER-129：Quick 缓存结果请求 Verified → Incompatible（mode-
    // mismatch）；无隐式升降级。判定面（evidence judgeCacheHit 单点）与
    // 编排消费面（编排器审计计数）双层观测。
    CacheOrchestratorFixture f;
    const auto cfg = f.makeConfig(3, 2, 5);
    auto request = f.readyRequest(cfg);

    // 第一次 run（Quick 批评估；Verified 批对同 sliceId 撞 Quick 条目——
    // 条目尚未登记，本批 cache=nullptr？否——cache 在场但无条目 → 全部
    // miss。此时先手工登记 Quick 条目再跑 Verified 消费演练）：
    // 为精确演练"Quick 条目 → Verified 请求"，直接构造同身份四要素查询：
    // 同 sliceId/契约/Profile、仅模式不同——判定的唯一失配原因必须是
    // ModeMismatch（证明"无隐式升降级"是唯一拒绝面，非其他身份失配）。
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());
    const TwoStageRunResult first = orch.run(request, f.ctx);
    ASSERT_FALSE(first.quickRecords.empty());

    // 把 Quick 批条目经接纳门槛登记入缓存（Quick 模式）。
    for (const auto& rec : first.quickRecords) {
        f.admitCacheEntry(core::EvaluationMode::Quick, rec.sliceId,
                          request.snapshot.snapshotId);
    }

    // 判定单点直证：同 sliceId/契约/Profile 下 Quick 条目 vs Verified 请求
    // ⇒ Incompatible 且唯一原因＝ModeMismatch（双向对称——D-13 字面）。
    const core::ContentIdentity sliceId = first.quickRecords.front().sliceId;
    evidence::CacheHitQuery verifiedQuery;
    verifiedQuery.requestedMode = core::EvaluationMode::Verified;
    verifiedQuery.requestSliceId = sliceId;
    verifiedQuery.requestContractVersion = optimization::kOptStaticScreenContractVersion;
    verifiedQuery.requestProfileIdentity = request.profile.contentIdentity;

    evidence::CachedResultSummary quickSummary;
    quickSummary.mode = core::EvaluationMode::Quick;
    quickSummary.outcome = core::TaskOutcome::Completed;
    quickSummary.manifestFinalized = true;
    quickSummary.sliceId = sliceId;
    quickSummary.evaluatorContractVersion = optimization::kOptStaticScreenContractVersion;
    quickSummary.profileContentIdentity = request.profile.contentIdentity;

    const evidence::CacheHitResult up
        = evidence::judgeCacheHit(verifiedQuery, quickSummary);
    EXPECT_EQ(up.verdict, evidence::CacheHitResult::Verdict::Incompatible);
    ASSERT_FALSE(up.reasons.empty());
    EXPECT_EQ(up.reasons.front(), evidence::CacheMissReason::ModeMismatch)
        << "Quick 条目对 Verified 请求的唯一失配＝模式（D-13 不升降级）";

    // 反向：Verified 条目对 Quick 请求——同判（双向 Incompatible）。
    evidence::CacheHitQuery quickQuery = verifiedQuery;
    quickQuery.requestedMode = core::EvaluationMode::Quick;
    evidence::CachedResultSummary verifiedSummary = quickSummary;
    verifiedSummary.mode = core::EvaluationMode::Verified;
    const evidence::CacheHitResult down
        = evidence::judgeCacheHit(quickQuery, verifiedSummary);
    EXPECT_EQ(down.verdict, evidence::CacheHitResult::Verdict::Incompatible);
    ASSERT_FALSE(down.reasons.empty());
    EXPECT_EQ(down.reasons.front(), evidence::CacheMissReason::ModeMismatch);

    // 编排消费面：登记后重跑（同 request）——Quick 批命中 Quick 条目回放；
    // 无"升级为 Verified"通道：Verified 批条目未登记 → miss 重算（如实），
    // 审计分账不出现"Verified 消费 Quick 条目"的 FullHit。
    const int callsBefore = f.evalCalls->load();
    const TwoStageRunResult second = orch.run(request, f.ctx);
    EXPECT_GT(f.evalCalls->load(), callsBefore)
        << "Verified 批未登记条目必须重算（Quick 缓存不得自动升级）";
    EXPECT_EQ(second.audit.cacheFullHits,
              static_cast<std::uint32_t>(first.quickRecords.size()))
        << "仅 Quick 批命中（Verified 批如实重算——无隐式升降级）";
}

// =====================================================================
// OPT-VER-137：会话内 FullHit 短路径（命中≠Current）
// =====================================================================

TEST(OptEvaluatorPortsContract, SessionCacheFullHitSkipsReEvaluation_WP20T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "CON-04"},
                  std::vector<std::string>{"AT-09"});

    // OPT-VER-137：同键双请求（同会话）→ 第二次 FullHit 短路径（不重算）；
    // 命中计数+1；命中≠Current 另判（结论面等价——命中只是性能面）。
    CacheOrchestratorFixture f;
    const auto cfg = f.makeConfig(4, 2, 17);
    auto request = f.readyRequest(cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());

    const TwoStageRunResult first = orch.run(request, f.ctx);
    EXPECT_EQ(first.audit.cacheFullHits, 0U);   // 首跑无条目——全 miss
    EXPECT_EQ(first.audit.cacheLookups,
              static_cast<std::uint32_t>(first.quickRecords.size()
                                         + first.verifiedRecords.size()));
    const int callsAfterFirst = f.evalCalls->load();
    EXPECT_GT(callsAfterFirst, 0);

    // 模拟接纳：Quick＋Verified 全部条目入正式缓存（写入双门槛真链）。
    for (const auto& rec : first.quickRecords) {
        f.admitCacheEntry(core::EvaluationMode::Quick, rec.sliceId,
                          request.snapshot.snapshotId);
    }
    for (const auto& rec : first.verifiedRecords) {
        f.admitCacheEntry(core::EvaluationMode::Verified, rec.sliceId,
                          request.snapshot.snapshotId);
    }

    // 第二次 run（同实例——会话底账在同编排器；同 request 同键请求）：
    // 全部候选两级均 FullHit → 回放（评估调用计数不增）＋命中记账。
    const TwoStageRunResult second = orch.run(request, f.ctx);
    EXPECT_EQ(f.evalCalls->load(), callsAfterFirst)
        << "FullHit 短路径不重算（评估器调用计数不增——OPT-VER-137）";
    const std::uint32_t expectedLookups
        = static_cast<std::uint32_t>(second.quickRecords.size()
                                     + second.verifiedRecords.size());
    EXPECT_EQ(second.audit.cacheLookups, expectedLookups);
    EXPECT_EQ(second.audit.cacheFullHits, expectedLookups) << "命中计数+1（逐请求记账）";
    // 审计口径（G-1 前批验收登记义务——本批对齐钉扎）：quickEvaluated/
    // verifiedEvaluated＝"实际评估数（命中回放不计）"——第二跑全部记录为
    // FullHit 回放 ⇒ 实际评估数不增（仍为各自批的记录数只出现在首跑）；
    // 回放单计入 cacheFullHits，两计数器分账不重叠（AT-34 对账口径唯一）。
    EXPECT_EQ(second.audit.quickEvaluated, 0U)
        << "全命中回放 ⇒ 实际评估数不计（G-1：命中回放不计）";
    EXPECT_EQ(second.audit.verifiedEvaluated, 0U)
        << "Verified 同口径（G-1）";
    EXPECT_EQ(first.audit.quickEvaluated,
              static_cast<std::uint32_t>(first.quickRecords.size()))
        << "首跑无回放 ⇒ 实际评估数＝批记录数";
    EXPECT_EQ(first.audit.verifiedEvaluated,
              static_cast<std::uint32_t>(first.verifiedRecords.size()));
    for (const auto& rec : second.quickRecords) {
        EXPECT_TRUE(rec.cacheHit) << "命中记录带 cacheHit 标记（审计）";
    }
    for (const auto& rec : second.verifiedRecords) {
        EXPECT_TRUE(rec.cacheHit);
    }

    // 结论面等价（命中只是性能面——I-OPT-3 的候选集合与排序不受影响）。
    ASSERT_EQ(first.pareto.entries.size(), second.pareto.entries.size());
    for (std::size_t i = 0; i < first.pareto.entries.size(); ++i) {
        EXPECT_EQ(first.pareto.entries[i].candidateId, second.pareto.entries[i].candidateId);
    }
}

TEST(OptEvaluatorPortsContract, CacheHitIsNotCurrentness_WP20T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-04", "CON-02"},
                  std::vector<std::string>{});

    // §4.5 字面："缓存命中不代表结果 Current（两回事）：命中只声明对该键
    // 可复用；当前性由 evidence computeCurrentness 另行计算——Superseded
    // 的历史缓存照样可命中同键请求。"——旧基准（inputBaselineId 不同）
    // 条目对同 sliceId 请求仍 FullHit（inputBaselineId 不参与判定）。
    CacheOrchestratorFixture f;
    const auto cfg = f.makeConfig(3, 2, 23);
    auto request = f.readyRequest(cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());

    const TwoStageRunResult first = orch.run(request, f.ctx);
    ASSERT_FALSE(first.quickRecords.empty());

    // 以"旧基准身份"登记（当前快照已是新基准——Superseded 场景的缓存侧
    // 形态：inputBaselineId 不参与 FullHit 判定，evidence §8.2）。
    const core::ContentIdentity staleBaseline = makeContentIdentity(0xEE);
    for (const auto& rec : first.quickRecords) {
        f.admitCacheEntry(core::EvaluationMode::Quick, rec.sliceId, staleBaseline);
    }

    const int callsBefore = f.evalCalls->load();
    const TwoStageRunResult second = orch.run(request, f.ctx);
    // Quick 批全命中（旧基准不阻命中——命中≠Current）；Verified 批条目本
    // 用例未登记 → 如实重算（2 候选 × 2 评估器键＝4 次调用，精确对账）。
    EXPECT_EQ(f.evalCalls->load() - callsBefore,
              2 * static_cast<int>(second.verifiedRecords.size()))
        << "旧基准条目照样命中（命中≠Current——两套语义不混同）；仅未登记面重算";
    EXPECT_EQ(second.audit.cacheFullHits,
              static_cast<std::uint32_t>(second.quickRecords.size()));
}

// =====================================================================
// CON-04：失败/取消/部分结果不作正式缓存命中
// =====================================================================

TEST(OptEvaluatorPortsContract, PartialResultNeverFormalCacheHit_WP20T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-04"}, std::vector<std::string>{"AT-09"});

    // CON-04：取消/部分结果只可诊断性读取（DiagnosticOnly）——编排器不回
    // 放、如实重算；审计分账 cacheDiagnosticOnly 观测。
    CacheOrchestratorFixture f;
    const auto cfg = f.makeConfig(3, 2, 31);
    auto request = f.readyRequest(cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());

    const TwoStageRunResult first = orch.run(request, f.ctx);
    ASSERT_FALSE(first.quickRecords.empty());

    // 登记部分条目（Quick 模式、同 sliceId、outcome=Canceled＋未封账——
    // 诊断账户；永不正式命中）。
    f.admitPartialEntry(first.quickRecords.front().sliceId);

    const int callsBefore = f.evalCalls->load();
    const TwoStageRunResult second = orch.run(request, f.ctx);
    EXPECT_GT(f.evalCalls->load(), callsBefore)
        << "部分结果不作正式命中——编排器重算（CON-04）";
    EXPECT_GE(second.audit.cacheDiagnosticOnly, 1U)
        << "诊断性读取分账观测（CacheHitResult::DiagnosticOnly 消费面）";
    // 判定单点直证：同身份四要素下 outcome≠Completed ⇒ DiagnosticOnly。
    evidence::CacheHitQuery query;
    query.requestedMode = core::EvaluationMode::Quick;
    query.requestSliceId = first.quickRecords.front().sliceId;
    query.requestContractVersion = optimization::kOptStaticScreenContractVersion;
    query.requestProfileIdentity = request.profile.contentIdentity;
    evidence::CachedResultSummary partial = evidence::CachedResultSummary{};
    partial.mode = core::EvaluationMode::Quick;
    partial.outcome = core::TaskOutcome::Canceled;
    partial.manifestFinalized = false;
    partial.sliceId = query.requestSliceId;
    partial.evaluatorContractVersion = query.requestContractVersion;
    partial.profileContentIdentity = query.requestProfileIdentity;
    const evidence::CacheHitResult verdict = evidence::judgeCacheHit(query, partial);
    EXPECT_EQ(verdict.verdict, evidence::CacheHitResult::Verdict::DiagnosticOnly);
    EXPECT_FALSE(verdict.reasons.empty());
    EXPECT_NE(std::find(verdict.reasons.begin(), verdict.reasons.end(),
                        evidence::CacheMissReason::OutcomeNotCompleted),
              verdict.reasons.end())
        << "未完成原因是 CON-04 的判定面承载";
}

// =====================================================================
// OPT-VER-138：配置变化 ⇒ slice-mismatch 重算
// =====================================================================

TEST(OptEvaluatorPortsContract, ChangedConfigYieldsSliceMismatchRecalc_WP20T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "CON-04"},
                  std::vector<std::string>{"AT-09"});

    // OPT-VER-138：变更 config 后请求 → judgeCacheHit Incompatible
    // （slice-mismatch）→ 重算。种子是 config.opt canonical 的身份要素
    // （§4.5 失效映射表）——种子变化 ⇒ 每候选 sliceId 变 ⇒ 全量重算。
    CacheOrchestratorFixture f;
    const auto cfg = f.makeConfig(3, 2, 41);
    auto request = f.readyRequest(cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());

    const TwoStageRunResult first = orch.run(request, f.ctx);
    for (const auto& rec : first.quickRecords) {
        f.admitCacheEntry(core::EvaluationMode::Quick, rec.sliceId,
                          request.snapshot.snapshotId);
    }
    const int callsAfterFirst = f.evalCalls->load();

    // 变更种子（config 变化）后请求：新 config ⇒ 新 canonical ⇒ 新 sliceId
    // （旧条目对查询为身份面失配）——全量重算＋不兼容分账。
    auto changedCfg = cfg;
    changedCfg.seed = cfg.seed + 1;
    auto changedRequest = request;
    changedRequest.config = changedCfg;
    const TwoStageRunResult second = orch.run(changedRequest, f.ctx);
    EXPECT_GT(f.evalCalls->load(), callsAfterFirst) << "身份面失配必须重算";
    EXPECT_EQ(second.audit.cacheIncompatible, second.audit.cacheLookups)
        << "全部查找均为 Incompatible（slice-mismatch——OPT-VER-138）";
    EXPECT_EQ(second.audit.cacheFullHits, 0U);

    // 判定单点直证（契约版本变更的 contract-mismatch——OPT-VER-138 的另
    // 一失效通道；纯函数面零复制断言）。
    evidence::CacheHitQuery query;
    query.requestedMode = core::EvaluationMode::Quick;
    query.requestSliceId = makeContentIdentity(0xAB);
    query.requestContractVersion = 2U;  // 升版契约
    query.requestProfileIdentity = request.profile.contentIdentity;
    evidence::CachedResultSummary stale;
    stale.mode = core::EvaluationMode::Quick;
    stale.outcome = core::TaskOutcome::Completed;
    stale.manifestFinalized = true;
    stale.sliceId = query.requestSliceId;
    stale.evaluatorContractVersion = 1U;  // 旧契约
    stale.profileContentIdentity = query.requestProfileIdentity;
    const evidence::CacheHitResult verdict = evidence::judgeCacheHit(query, stale);
    EXPECT_EQ(verdict.verdict, evidence::CacheHitResult::Verdict::Incompatible);
    EXPECT_NE(std::find(verdict.reasons.begin(), verdict.reasons.end(),
                        evidence::CacheMissReason::ContractMismatch),
              verdict.reasons.end());
}

// =====================================================================
// AT-34 导出面：审计 CSV 与重放统计一致（WP-20-T09 acceptance 1 的
// 重放半区——宿主于本文件的缓存底座：真协调器＋真重放链）
// =====================================================================

TEST(OptExportContract, AuditCsvConsistentWithReplayStatistics_WP20T09)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12"}, std::vector<std::string>{"AT-34"});

    // AT-34 字面"审计计数与重放一致"的重放对账：同会话双跑（第二跑全量
    // FullHit 回放）→ 回放统计组装运行聚合 → 导出审计 CSV → 逐行值与
    // 重放统计逐一相等（quick/verified 实际评估数因回放而为 0——G-1 对齐
    // 后的口径；命中数＝批记录数）。判定唯一归 evidence/存储唯一归
    // execution 的边界在 OPT-VER-137 用例已钉，此处只对账导出面。
    CacheOrchestratorFixture f;
    const auto cfg = f.makeConfig(4, 2, 41);
    auto request = f.readyRequest(cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());

    const TwoStageRunResult first = orch.run(request, f.ctx);
    ASSERT_FALSE(first.quickRecords.empty());
    for (const auto& rec : first.quickRecords) {
        f.admitCacheEntry(core::EvaluationMode::Quick, rec.sliceId,
                          request.snapshot.snapshotId);
    }
    for (const auto& rec : first.verifiedRecords) {
        f.admitCacheEntry(core::EvaluationMode::Verified, rec.sliceId,
                          request.snapshot.snapshotId);
    }
    const TwoStageRunResult replay = orch.run(request, f.ctx);
    // 重放统计基线（G-1 口径——回放不计实际评估数）。
    ASSERT_EQ(replay.runPhase, optimization::RunPhase::Completed);
    EXPECT_EQ(replay.audit.quickEvaluated, 0U);
    EXPECT_EQ(replay.audit.verifiedEvaluated, 0U);
    EXPECT_EQ(replay.audit.cacheFullHits,
              static_cast<std::uint32_t>(replay.quickRecords.size()
                                         + replay.verifiedRecords.size()));

    // 回放统计 → 运行聚合（归档态）→ 一站式导出（限定语请求）。
    const optimization::OptimizationRunResult run = optimization::assembleRunResult(
        optimization::OptimizationRunId::generate(), request.task.project,
        request.task.branch, request.task.revision, request.snapshot.snapshotId,
        request.baselineRoot, request.baselineCv, request.config, replay, {},
        optimization::ArchivePhase::Archived);
    optimization::ExportRequest exportReq;
    exportReq.run = run;
    exportReq.spec.project = request.task.project;
    exportReq.spec.branch = request.task.branch;
    exportReq.spec.revision = request.task.revision;
    exportReq.spec.snapshotId = request.snapshot.snapshotId;
    exportReq.spec.config = request.config;
    exportReq.spec.profile.profileId = "opt";
    exportReq.spec.profile.version = "1.0";
    exportReq.spec.profile.contentIdentity
        = core::ContentIdentity::fromCanonical(std::string("cid-")
                                               + std::string(62, '0') + "d1");
    optimization::validateRunSpec(exportReq.spec);
    exportReq.currentProfile = exportReq.spec.profile;
    exportReq.evaluatorContractVersionAtRun
        = optimization::kOptStaticScreenContractVersion;
    exportReq.exportContractVersionAtRun = optimization::kOptExportContractVersion;
    exportReq.formal = false;
    optimization::OptimizationExportProvider provider;
    const optimization::ExportBundleData bundle = provider.buildExportBundle(exportReq);

    // 审计 CSV 逐行值 ↔ 重放统计（count_name → value 检索对账）。
    std::vector<std::pair<std::string, std::int64_t>> audit;
    for (const auto& row : bundle.auditCsvRows) {
        ASSERT_EQ(row.size(), 3U);
        if (row[1].kind == io::CsvCell::Kind::Int) {
            audit.emplace_back(row[0].text, row[1].integer);
        }
    }
    const auto valueOf = [&audit](const std::string& name) -> std::int64_t {
        for (const auto& kv : audit) {
            if (kv.first == name) {
                return kv.second;
            }
        }
        return -1;
    };
    EXPECT_EQ(valueOf("quick_evaluated"),
              static_cast<std::int64_t>(replay.audit.quickEvaluated))
        << "审计 CSV＝重放统计（实际评估数——回放不计，AT-34）";
    EXPECT_EQ(valueOf("verified_evaluated"),
              static_cast<std::int64_t>(replay.audit.verifiedEvaluated));
    EXPECT_EQ(valueOf("cache_full_hits"),
              static_cast<std::int64_t>(replay.audit.cacheFullHits))
        << "命中数分账对账";
    EXPECT_EQ(valueOf("cache_lookups"),
              static_cast<std::int64_t>(replay.audit.cacheLookups));
    EXPECT_EQ(valueOf("candidates_generated"),
              static_cast<std::int64_t>(replay.audit.candidatesGenerated));
    EXPECT_EQ(valueOf("duplicates_dropped"),
              static_cast<std::int64_t>(replay.pareto.duplicatesDropped))
        << "去重数（T05 审计入口）对账";
}
