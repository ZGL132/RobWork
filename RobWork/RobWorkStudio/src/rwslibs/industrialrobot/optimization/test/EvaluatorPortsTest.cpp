/**
 * @file   EvaluatorPortsTest.cpp
 * @brief  批量编排与缓存/种子消费面模型测试（OptEvaluatorPorts 组）——
 *         config.opt 校验与 canonical、确定性候选生成（种子）、Quick/Verified
 *         两级编排（保守淘汰→复核）、搜索空与取消语义——任务契约 WP-20-T06
 *         acceptance 1/3（OPT-VER-120/127/128 模型面/129 词表面/130 观测点）。
 *
 * 设计依据：
 *   - units/optimization.md §4.3（config.opt 载荷与 canonical——"任何字段
 *     变化 ⇒ 新 sliceId ⇒ 缓存不命中"）、§4.4 I-OPT-2/I-OPT-3、§8.2（基线
 *     恒生成；种子采样域＝锁定集外）、§8.4（Quick 保守淘汰 ScreenedOut＋
 *     screening-only；Verified 复核进可行集→Pareto；确定性承诺）、
 *     §13.1（OPT-VER-120/127/128/130/131 用例矩阵）
 *   - 需求 OPT-06（B 子集：缓存与确定性种子——缓存判定面归契约测试，
 *     本文件专注编排/生成/校验面）、NFR-COR-02（同种子同线程配置 ⇒ 等价
 *     候选集合与稳定排序——AT-09~11）、NFR-COR-03（0 非法不静默替换）
 *   - 并行与检查点不作 B 期承诺（REQUIREMENTS §15.0——threadCount>1 显式
 *     拒绝用例钉住；不预建并行编排）
 *
 * 测试形态（模型测试＝直调计算库，NFR-MNT-01）：③端口评估器以脚本化替身
 * 注册进**真 evidence::EvaluatorRegistry**（T04 ConstraintTest 同款形态——
 * 消费路径与生产一致）；候选投影以可控替身注入（ICandidateProjector——
 * P-OPT-2 裁决前接缝）；编排器 cache=nullptr（无缓存会话——缓存判定链归
 * 契约测试 EvaluatorPortsContractTest 的真协调器装配）。接口路径（虚函数）
 * 天然钉住（WP-20-T03 B-1 教训：编排器/管线/投影全部经接口消费）。
 */

#include <sdurws/ird/optimization/EvaluatorPorts.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Pareto.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
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
using optimization::EnvelopeFacts;
using optimization::LinkMassFact;
using optimization::ObjectiveSet;
using optimization::OptimizationConfiguration;
using optimization::OptimizationStage;
using optimization::PointMarginFact;
using optimization::RejectionReason;
using optimization::RunPhase;
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

// =====================================================================
// 通用替身与构造辅助（模型测试自持——T04 ConstraintTest 同款形态精简）
// =====================================================================

/// 修订闭包来源替身（快照组装协议——全部 (oid,cv) 声明在册）。
class AllPresentClosure final : public evidence::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 评估调用上下文替身（零取消/零进度——两级编排的批间取消用独立计数替身）。
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

/// 第 N 次查询后取消的上下文替身（批边界协作取消——TASK-01 的编排侧观测）。
class CancelAfterContext final : public evidence::IEvaluationContext {
public:
    explicit CancelAfterContext(std::uint32_t afterQueries)
        : m_after(afterQueries)
    {
    }
    bool cancellationRequested() const override
    {
        ++m_queries;
        return m_queries > m_after;  // 第 m_after+1 次起返回 true
    }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }

private:
    std::uint32_t m_after;              ///< 放行查询数（之后即取消）
    mutable std::uint32_t m_queries = 0; ///< 已发生查询数
};

/// 非零内容身份（测试构造——避免保留值）。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// 构造冻结快照（SnapshotBuilder 唯一合法生产者——身份为 builder 计算值）。
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

/// 覆盖矩阵（executedAll=false 时仅首工况执行——Quick 口径缩减投影形态）。
evidence::CaseCoverageMatrix
makeCoverage(const evidence::AnalysisSnapshot& snapshot, bool executedAll)
{
    evidence::CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    bool first = true;
    for (const auto& entry : snapshot.caseSet.entries) {
        evidence::CaseCoverageEntry row;
        row.caseId = entry.caseId;
        if (executedAll || first) {
            row.status = evidence::CaseExecutionStatus::Executed;
            row.runId = core::RunId::generate();
            row.resultSliceId = makeContentIdentity(6);
        } else {
            row.status = evidence::CaseExecutionStatus::NotExecuted;
        }
        first = false;
        matrix.entries.push_back(std::move(row));
    }
    return matrix;
}

/// 两工况（均 enabled+mandatory——覆盖矩阵分母）。
std::vector<evidence::CaseEntry> makeTwoMandatoryCases()
{
    return {{core::ObjectId::generate(), "额定工况", true, true},
            {core::ObjectId::generate(), "极限工况", true, true}};
}

// ---- ③端口脚本化评估器替身（真注册表——T04 同款精简）----

class ScriptedKinEvaluator final : public evidence::IEngineeringEvaluator {
public:
    ScriptedKinEvaluator(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override
    {
        return {};  // 空输出（无违例素材——Feasible 由覆盖完备＋限位合法得到）
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
};

class ScriptedKinFactory final : public evidence::IEvaluatorFactory {
public:
    explicit ScriptedKinFactory(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<ScriptedKinEvaluator>(m_descriptor);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
};

/// 组装合法 kin 评估器描述符（契约版本 1；依赖声明空集）。
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

/// 注册 "kin" v1.0 Profile（空必需项集——完备性核对平凡通过）。
void registerKinProfile(evidence::EvidenceProfileRegistry& profiles)
{
    evidence::RequiredEvidenceProfile profile;
    profile.profileId = "kin";
    profile.version = "1.0";
    profiles.registerProfile(profile);
}

// ---- 探针/编译替身（T04 同款精简）----

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

// ---- 候选投影替身（ICandidateProjector 接口路径——包络随补丁值线性变化，
//      质量/裕量恒定 ⇒ 候选仅由包络区分 ⇒ 支配关系与排序确定可控）----

class ScriptedProjector final : public optimization::ICandidateProjector {
public:
    mutable int calls = 0;                 ///< 投影调用计数（缓存回放不触投影的观测面）
    bool topologyOutOfScope = false;       ///< 拓扑违例脚本（OPT-VER-120 场景）
    bool dropMargins = false;              ///< 裕量事实缺失脚本（§7.4 编排执行点场景）
    double massKg = 10.0;                  ///< 恒定连杆质量（kg——质量目标无区分度）
    double margin = 0.4;                   ///< 恒定工位裕量（D-KIN-2 归一化，无量纲）

    CandidateProjection
    project(const CandidatePatch& patch, const std::vector<VariableBinding>&) const override
    {
        ++calls;
        CandidateProjection p;
        p.baselineChainInEnabledScope = !topologyOutOfScope;
        optimization::JointLimitSpecRecord joint;
        joint.jointSubject = "obj-probe-joint";
        joint.qmin = -3.14;  // rad（转动关节下限）
        joint.qmax = 3.14;   // rad（转动关节上限）
        p.jointLimits.joints = {joint};
        p.jointLimits.configurations = {{0.0}};  // 关节角 0 rad 单构型
        // 包络随补丁首个标量值线性变化（基线取默认 0.5）：值 v ⇒ 半宽 v，
        // 三向尺寸之和 = 6v（m）——候选间包络值互异（采样值不重复）。本
        // fixture 的消费用例全部为标量维度（枚举项 scalarValue 规范化为 0，
        // 不用于区分度场景）。
        double v = 0.5;
        if (!patch.items.empty()) {
            v = patch.items.front().scalarValue;
        }
        p.metricFacts.envelope = EnvelopeFacts{-v, -v, -v, v, v, v};
        if (massKg > 0.0) {
            LinkMassFact lm;
            lm.linkSubject = "obj-link";
            lm.massKg = massKg;
            lm.provenanceToken = "provided";
            p.metricFacts.linkMasses = {lm};
        }
        if (!dropMargins) {
            PointMarginFact pm;
            pm.caseIdText = "case-margin";
            pm.minMargin = margin;
            pm.unitSymbol = "1";
            p.metricFacts.pointMargins = {pm};
        }
        return p;
    }
};

// ---- 会话缓存替身（F-640 用例——IExecutionCacheCoordinator 脚本化）----

/// 脚本化会话缓存协调器（§10.7 四方法最小实现）：判定面归 evidence 的
/// 生产语义由脚本开关表达——fullHit=false 恒 DispatchNormal（首轮全重算）；
/// true 恒 ShortPath（后续轮全命中，倒逼会话底账回放路径）。查询模式序
/// 记账供断言（底账键补 mode 的查找面观测）。
class ScriptedSessionCache final : public execution::IExecutionCacheCoordinator {
public:
    bool fullHit = false;  ///< 脚本开关（true ⇒ lookup 一律 ShortPath）
    int lookups = 0;       ///< lookup 累计次数（观测面）
    std::vector<core::EvaluationMode> queriedModes;  ///< 查询模式序（按发生序）

    execution::CacheLookup lookup(const execution::CacheLookupQuery& query) override
    {
        ++lookups;
        queriedModes.push_back(query.requestedMode);
        execution::CacheLookup look;  // 默认 DispatchNormal（未命中形态）
        if (fullHit) {
            look.guidance = execution::CacheLookup::Guidance::ShortPath;
        }
        return look;
    }

    // 登记治理归 execution 生产面（编排器不消费本面——替身空实现）。
    void storeResult(core::RunId) override {}
    void setEvictionPolicy(const execution::EvictionPolicy&) override {}
    execution::CacheStats stats() const noexcept override { return {}; }
};

// ---- 编排装配 fixture ----

/// 两级编排完整依赖面（真注册表＋替身探针/编译/投影＋T05 Pareto——
/// deps 指针即接口指针；pipeline 以成员持有，声明序保证依赖先构造）。
struct OrchestratorFixture {
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    FakeJointLimitProbe jointProbe;
    FakeCollisionProbe collisionProbe;
    FakeCompiler compiler;
    NopEvaluationContext ctx;
    ScriptedProjector projector;
    optimization::ParetoFrontBuilder paretoBuilder;
    core::ContentIdentity profileIdentity;  ///< 注册权威值（构造期回填）
    StaticHardConstraintPipeline pipeline;

    OrchestratorFixture()
        : profileIdentity(makeContentIdentity(0))  // 先占位——构造体内回填
        , pipeline(OptimizationStage::StageB, pipelineDeps())
    {
        registerKinProfile(profiles);
        evaluators.registerEvaluator(
            std::make_unique<ScriptedKinFactory>(
                makeKinDescriptor(std::string(optimization::kKinTaskPointsBatchKey))),
            {});
        evaluators.registerEvaluator(
            std::make_unique<ScriptedKinFactory>(
                makeKinDescriptor(std::string(optimization::kKinRegionCoverageKey))),
            {});
        jointProbe.result.completed = true;
        collisionProbe.result.completed = true;
        profileIdentity = profiles.findProfile("kin", "1.0")->contentIdentity;
    }

    /// T04 管线依赖面（③端口注册表＋探针/编译替身——StaticHardConstraintDeps）。
    StaticHardConstraintDeps pipelineDeps()
    {
        StaticHardConstraintDeps d;
        d.evaluators = &evaluators;
        d.jointLimitProbe = &jointProbe;
        d.collisionProbe = &collisionProbe;
        d.compiler = &compiler;
        d.producers = &evaluators;   // EvaluatorRegistry 实现 IProducerRegistryView
        d.profiles = &profiles;      // EvidenceProfileRegistry 实现 IProfileRegistryView
        return d;
    }

    /// 两级编排依赖面（pipeline/projector/pareto 必填；cache 空＝无缓存会话）。
    TwoStageOrchestratorDeps deps()
    {
        TwoStageOrchestratorDeps d;
        d.pipeline = &pipeline;
        d.projector = &projector;
        d.paretoBuilder = &paretoBuilder;
        d.cache = nullptr;  // 模型测试无缓存会话——缓存链归契约测试
        return d;
    }
};

/// 单连续绑定（DH a，m——R1 §5.3 权威字段）。
VariableBinding makeLengthBinding()
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

/// 已校验基线配置（三项静态目标＋预算可调）。
OptimizationConfiguration makeConfig(std::uint32_t maxCandidates = 4,
                                     std::uint32_t maxVerified = 2,
                                     std::uint64_t seed = 7)
{
    OptimizationConfiguration c;
    c.stage = OptimizationStage::StageB;
    c.seed = seed;
    c.budget.maxCandidates = maxCandidates;
    c.budget.maxVerifiedCandidates = maxVerified;
    c.strategyId = std::string(optimization::kStrategySeededLhs);
    c.objectives = optimization::defaultObjectives(OptimizationStage::StageB);
    c.variables = {makeLengthBinding()};
    optimization::validateConfiguration(c);
    return c;
}

/// 冻结请求（快照/身份/覆盖——两工况全执行；Quick 口径同集以保 Quick-Feasible）。
TwoStageRunRequest makeRequest(const OptimizationConfiguration& cfg,
                               const evidence::AnalysisSnapshot& snapshot)
{
    TwoStageRunRequest r;
    r.config = cfg;
    r.baselineRoot = core::ObjectId::generate();
    r.baselineCv = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000bb");
    r.snapshot = snapshot;
    r.task = {core::ProjectId::generate(), core::BranchId::generate(),
              core::RevisionId::generate(), core::RunId::generate(), core::AttemptId{1U}};
    r.profile.profileId = "kin";
    r.profile.version = "1.0";
    r.profile.contentIdentity = makeContentIdentity(0);  // 用例内回填注册权威值
    r.coverage = makeCoverage(snapshot, true);
    r.quickCoverage = makeCoverage(snapshot, true);
    return r;
}

/// 组装就绪请求（fixture 的 Profile 权威值回填）。
TwoStageRunRequest readyRequest(OrchestratorFixture& f, const OptimizationConfiguration& cfg)
{
    const auto cases = makeTwoMandatoryCases();
    const auto snapshot = makeSnapshot(cases);
    TwoStageRunRequest r = makeRequest(cfg, snapshot);
    r.profile.contentIdentity = f.profileIdentity;
    return r;
}

}  // namespace

// =====================================================================
// 词表钉扎与配置校验（I-OPT-2/§15.0 B 期承诺边界）
// =====================================================================

TEST(OptEvaluatorPorts, RunPhaseTokensFollowVocabulary_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"}, std::vector<std::string>{"AT-34"});

    // 卡 §4.3/§4.4 RunPhase 九值 token 逐一钉扎（枚举序＝卡面登记契约——
    // token 文本冻结，改名即导出/审计消费面漂移）。
    EXPECT_EQ(optimization::toToken(RunPhase::Draft), "draft");
    EXPECT_EQ(optimization::toToken(RunPhase::Preflight), "preflight");
    EXPECT_EQ(optimization::toToken(RunPhase::QuickScreening), "quick-screening");
    EXPECT_EQ(optimization::toToken(RunPhase::VerifiedReview), "verified-review");
    EXPECT_EQ(optimization::toToken(RunPhase::RobustnessReview), "robustness-review");
    EXPECT_EQ(optimization::toToken(RunPhase::Completed), "completed");
    EXPECT_EQ(optimization::toToken(RunPhase::Canceled), "canceled");
    EXPECT_EQ(optimization::toToken(RunPhase::Failed), "failed");
    EXPECT_EQ(optimization::toToken(RunPhase::Interrupted), "interrupted");
}

TEST(OptEvaluatorPorts, ZeroSeedRejectedWithoutSubstitution_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "NFR-COR-03"},
                  std::vector<std::string>{"AT-09"});

    // I-OPT-2：种子 0 非法——不做静默替换（NFR-COR-03）。静默替换会让
    // 不同意图的运行共享 sliceId（缓存错误命中的直接来源）。
    OptimizationConfiguration c = makeConfig();
    c.seed = 0;
    bool thrown = false;
    try {
        optimization::validateConfiguration(c);
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        EXPECT_NE(std::string(e.what()).find("≥1"), std::string::npos)
            << "消息必须携带合法域说明（ERR-01 比较型定位）";
    }
    ASSERT_TRUE(thrown) << "种子 0 必须 fail-fast（I-OPT-2）";

    // canonical 入口同样拦截（绕过校验直调的防御复验）。
    thrown = false;
    try {
        optimization::canonicalizeRunConfiguration(c);
    } catch (const optimization::OptimizationError&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown) << "canonicalize 防御复验同面拦截";
}

TEST(OptEvaluatorPorts, ParallelBeyondStageBPromiseRejected_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"}, std::vector<std::string>{});

    // §15.0：并行与检查点不作 B 期承诺——threadCount>1 显式拒绝（不静默
    // 降级为串行）；maxGenerations>1 同理（R1 固定单批）。
    OptimizationConfiguration c = makeConfig();
    c.parallel.threadCount = 2;
    bool thrown = false;
    try {
        optimization::validateConfiguration(c);
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        EXPECT_NE(std::string(e.what()).find("OPT-D"), std::string::npos)
            << "消息必须指向 OPT-D/R2 落位（承诺边界可定位）";
    }
    ASSERT_TRUE(thrown);

    c = makeConfig();
    c.budget.maxGenerations = 2;
    thrown = false;
    try {
        optimization::validateConfiguration(c);
    } catch (const optimization::OptimizationError&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown) << "多代生成属 OPT-D/R2 策略扩展（R1 恒 1）";
}

TEST(OptEvaluatorPorts, ConfigContradictionsRejected_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"}, std::vector<std::string>{});

    // 配置矛盾逐类拒绝（确定性首错——检查序固定）：
    // ① 幸存集上界超总预算；② 未登记策略；③ 空目标集；④ 重复目标；
    // ⑤ 负容差。
    OptimizationConfiguration c = makeConfig();
    c.budget.maxVerifiedCandidates = c.budget.maxCandidates + 1U;
    EXPECT_THROW(optimization::validateConfiguration(c), optimization::OptimizationError);

    c = makeConfig();
    c.strategyId = "opt.strategy.unknown";
    EXPECT_THROW(optimization::validateConfiguration(c), optimization::OptimizationError);

    c = makeConfig();
    c.objectives.entries.clear();
    EXPECT_THROW(optimization::validateConfiguration(c), optimization::OptimizationError);

    c = makeConfig();
    c.objectives.entries.push_back(c.objectives.entries.front());  // 重复 MetricId
    EXPECT_THROW(optimization::validateConfiguration(c), optimization::OptimizationError);

    c = makeConfig();
    c.objectives.entries.front().tolerance.relative = -1.0;
    EXPECT_THROW(optimization::validateConfiguration(c), optimization::OptimizationError);
}

// =====================================================================
// config.opt canonical（§4.3——缓存键要素的身份承载面）
// =====================================================================

TEST(OptEvaluatorPorts, CanonicalConfigDeterministicAndFieldSensitive_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "NFR-COR-02"},
                  std::vector<std::string>{"AT-09"});

    // 同配置（变量绑定任意录入序）⇒ 同字节（确定性——§4.3"按 bindingToken
    // 字典序"）；逐字段变化 ⇒ 字节变化（§4.5 失效映射全表——任一要素变化
    // 即新 sliceId ⇒ 缓存不命中）。
    OptimizationConfiguration base = makeConfig();
    const auto canonicalBase = optimization::canonicalizeRunConfiguration(base);

    OptimizationConfiguration copyFlipped = base;
    std::reverse(copyFlipped.variables.begin(), copyFlipped.variables.end());
    const auto canonicalFlipped = optimization::canonicalizeRunConfiguration(copyFlipped);
    EXPECT_EQ(canonicalBase, canonicalFlipped)
        << "同配置任意成员录入序必得同字节（确定性 NFR-COR-02）";

    // magic 黄金核对（卡 §4.3 原文——"IRDOPTC1"）。
    ASSERT_GE(canonicalBase.size(), 8U);
    EXPECT_EQ(std::string(canonicalBase.begin(), canonicalBase.begin() + 8), "IRDOPTC1");

    // 逐字段敏感（每一项变化都改变字节——§4.5 失效映射的 canonical 承载）：
    const auto expectSensitive = [&](OptimizationConfiguration mutated,
                                     const char* field) {
        const auto bytes = optimization::canonicalizeRunConfiguration(mutated);
        EXPECT_NE(bytes, canonicalBase) << "字段 " << field << " 变化必须改变 canonical";
    };
    {
        auto m = base;
        m.seed = base.seed + 1;  // 种子（I-OPT-3 确定性来源）
        expectSensitive(m, "seed");
    }
    {
        auto m = base;
        m.budget.maxCandidates = base.budget.maxCandidates + 1;  // 预算
        expectSensitive(m, "maxCandidates");
    }
    {
        auto m = base;
        m.strategyId = "opt.strategy.seeded-lhs";  // 同 token——不变
        auto bytes = optimization::canonicalizeRunConfiguration(m);
        EXPECT_EQ(bytes, canonicalBase) << "同 token 不变";
    }
    {
        auto m = base;
        m.variables.front().upperBound = 0.9;  // 变量边界（§4.5 绑定边界失效行）
        expectSensitive(m, "upperBound");
    }
    {
        auto m = base;
        m.variables.front().locked = true;  // 锁定状态（§4.5 绑定/锁定失效行）
        expectSensitive(m, "locked");
    }
    {
        auto m = base;
        m.objectives.entries.front().tolerance.absolute = 1e-9;  // 支配容差
        expectSensitive(m, "tolerance");
    }
}

// =====================================================================
// 确定性候选生成（§8.2 seeded-lhs——I-OPT-3）
// =====================================================================

TEST(OptEvaluatorPorts, SeededGenerationReproducibleAndSeedSensitive_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "NFR-COR-02"},
                  std::vector<std::string>{"AT-09"});

    // 同种子双跑 ⇒ 逐字节同批（等价候选集合的生成面——OPT-VER-130 前置）；
    // 不同种子 ⇒ 不同批（种子参与候选值——采样多样性的来源）。
    const auto cfgA = makeConfig(8, 2, 42);
    const auto batchA1 = optimization::generateSeededLhsCandidates(cfgA);
    const auto batchA2 = optimization::generateSeededLhsCandidates(cfgA);
    ASSERT_EQ(batchA1.size(), batchA2.size());
    ASSERT_EQ(batchA1.size(), 8U);  // 预算 8 ⇒ 基线＋7 采样
    for (std::size_t i = 0; i < batchA1.size(); ++i) {
        EXPECT_EQ(optimization::canonicalize(batchA1[i]),
                  optimization::canonicalize(batchA2[i]))
            << "候选 " << i << " 双跑必逐字节一致（同种子 I-OPT-3）";
    }

    const auto cfgB = makeConfig(8, 2, 43);
    const auto batchB = optimization::generateSeededLhsCandidates(cfgB);
    bool anyDifferent = false;
    for (std::size_t i = 1; i < batchB.size(); ++i) {  // 跳过基线（恒同）
        if (optimization::canonicalize(batchA1[i]) != optimization::canonicalize(batchB[i])) {
            anyDifferent = true;
        }
    }
    EXPECT_TRUE(anyDifferent) << "不同种子必产生不同采样候选（种子敏感性）";
}

TEST(OptEvaluatorPorts, SeededGenerationRespectsLocksBoundsEnums_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02", "OPT-06"},
                  std::vector<std::string>{"AT-09"});

    // 采样域纪律：①锁定/未授权维度不进补丁（§8.2"锁定集外扰动"）；②连续
    // 值在界内；③量化对齐后仍在界内；④枚举在封闭值域内。
    OptimizationConfiguration cfg = makeConfig();
    VariableBinding quantized = makeLengthBinding();
    quantized.bindingId = "mdl.joint[3].dh.a";
    quantized.kind = VariableKind::Quantized;
    quantized.step = 0.1;  // m（网格步长——0.2~0.8 界内 7 格）
    quantized.defaultValue = 0.5;

    VariableBinding locked = makeLengthBinding();
    locked.bindingId = "mdl.joint[4].dh.a";
    locked.locked = true;  // 锁定（§5.4）——不采样

    VariableBinding unauthorized = makeLengthBinding();
    unauthorized.bindingId = "mdl.joint[5].dh.a";
    unauthorized.authorized = false;  // 未授权（§5.4 改型默认）——不采样

    VariableBinding enumerated = makeLengthBinding();
    enumerated.bindingId = "mdl.link[2].material";
    enumerated.kind = VariableKind::Enumeration;
    enumerated.unit = core::UnitToken{};  // 枚举值非物理量——无效句柄合法
    enumerated.enumValues = {"aluminum", "steel"};
    enumerated.defaultValueIndex = 0;

    cfg.variables = {quantized, locked, unauthorized, enumerated};
    optimization::validateConfiguration(cfg);

    const auto batch = optimization::generateSeededLhsCandidates(cfg);
    ASSERT_EQ(batch.size(), cfg.budget.maxCandidates);
    for (std::size_t i = 1; i < batch.size(); ++i) {  // 跳过基线
        const auto& patch = batch[i];
        for (const auto& item : patch.items) {
            EXPECT_NE(item.bindingId, locked.bindingId)
                << "锁定维度不得出现在补丁（§5.4/OPT-VAR-LOCKED 生成期防线）";
            EXPECT_NE(item.bindingId, unauthorized.bindingId)
                << "未授权维度不得出现在补丁（§5.4）";
            if (item.bindingId == cfg.variables[0].bindingId) {
                EXPECT_GE(item.scalarValue, cfg.variables[0].lowerBound);
                EXPECT_LE(item.scalarValue, cfg.variables[0].upperBound);
            }
            if (item.bindingId == quantized.bindingId) {
                EXPECT_GE(item.scalarValue, quantized.lowerBound);
                EXPECT_LE(item.scalarValue, quantized.upperBound);
            }
            if (item.bindingId == enumerated.bindingId) {
                ASSERT_LT(item.enumIndex, enumerated.enumValues.size());
            }
        }
    }
}

TEST(OptEvaluatorPorts, BaselineCandidateAlwaysFirstAndEmpty_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "OPT-11"},
                  std::vector<std::string>{"AT-09"});

    // §8.2：基线候选（空补丁）恒生成且为批首（Evaluate Baseline 的执行
    // 形态——基线与候选同管线同基准）。
    const auto batch = optimization::generateSeededLhsCandidates(makeConfig(5, 2, 9));
    ASSERT_FALSE(batch.empty());
    EXPECT_TRUE(batch.front().items.empty()) << "批首恒为基线空补丁";
    EXPECT_EQ(batch.size(), 5U);
}

// =====================================================================
// Quick/Verified 两级编排（§8.4——OPT-VER-120/127/128/130）
// =====================================================================

TEST(OptEvaluatorPorts, QuickScreenedOutWithReasonAndScreeningOnly_WP20T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"}, std::vector<std::string>{"AT-09"});

    // OPT-VER-127：Quick 保守淘汰——预算线外的 Quick-Feasible 候选标
    // ScreenedOut＋原因记录（kRejectQuickScreenedBudget）；screening-only
    // 标记；不进 Verified 复核批（不进正式可行集）。
    OrchestratorFixture f;
    auto cfg = makeConfig(/*maxCandidates=*/4, /*maxVerified=*/2);
    auto request = readyRequest(f, cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());
    const TwoStageRunResult result = orch.run(request, f.ctx);

    // 运行完成；Quick 批 4 条（基线＋3），幸存 2（基线席＋最优非基线），
    // 淘汰 2（预算线）。
    EXPECT_TRUE(result.runCompleted);
    EXPECT_EQ(result.runPhase, RunPhase::Completed);
    ASSERT_EQ(result.quickRecords.size(), 4U);
    EXPECT_EQ(result.audit.quickScreenedOut, 2U);
    EXPECT_EQ(result.verifiedRecords.size(), 2U);

    for (auto& rec : result.quickRecords) {
        EXPECT_TRUE(rec.screeningOnly) << "Quick 记录恒 screening-only（EVI-01 表 1）";
        EXPECT_FALSE(rec.formalPassEligible)
            << "Quick 记录不得具正式通过资格（mode-not-verified——evidence 单点）";
        if (rec.status == CandidateStatus::ScreenedOut) {
            ASSERT_FALSE(rec.extraRejections.empty());
            const RejectionReason& r = rec.extraRejections.front();
            EXPECT_EQ(r.reasonToken, std::string(optimization::kRejectQuickScreenedBudget));
            EXPECT_EQ(r.mode, core::EvaluationMode::Quick);
            EXPECT_EQ(r.stage, OptimizationStage::StageB);
        }
    }

    // 保守方向：被筛掉者的包络（唯一区分指标，min 方向）不得优于幸存
    // 非基线（支配序上劣于预算线——"边缘候选"的编排语义）。
    double survivorEnvelope = 0.0;
    std::vector<double> screenedEnvelopes;
    for (auto& rec : result.quickRecords) {
        const auto v = rec.metrics.valueOf(optimization::MetricId::Envelope);
        if (rec.status == CandidateStatus::ScreenedOut) {
            screenedEnvelopes.push_back(*v);
        }
        if (rec.mode == core::EvaluationMode::Verified && rec.status == CandidateStatus::Feasible
            && !rec.isBaseline) {
            survivorEnvelope = *rec.metrics.valueOf(optimization::MetricId::Envelope);
        }
    }
    for (const double e : screenedEnvelopes) {
        EXPECT_GE(e, survivorEnvelope) << "被筛候选包络不得优于幸存非基线（保守淘汰）";
    }
    // ScreenedOut 不进 Verified 批（不自动升级——§8.4 表缓存行同源纪律）。
    for (auto& rec : result.verifiedRecords) {
        EXPECT_EQ(rec.status, CandidateStatus::Feasible);
        EXPECT_TRUE(rec.formalPassEligible) << "Verified 全条件投影下正式资格成立";
    }
    // 可行集＝Verified 批 Feasible 候选（Quick 记录绝不进入——类型学屏障）。
    EXPECT_EQ(result.pareto.feasibleIds.size(), 2U);
}

TEST(OptEvaluatorPorts, VerifiedSurvivorsProducePareto_WP20T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "OPT-04"},
                  std::vector<std::string>{"AT-09"});

    // OPT-VER-128：Verified 复核——幸存集完整预算复核（全必验工况覆盖），
    // Feasible 候选进可行集并产出非支配前沿（稳定排序）。
    OrchestratorFixture f;
    auto cfg = makeConfig(6, 3);
    auto request = readyRequest(f, cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());
    const TwoStageRunResult result = orch.run(request, f.ctx);

    EXPECT_TRUE(result.runCompleted);
    EXPECT_EQ(result.runPhase, RunPhase::Completed);
    EXPECT_FALSE(result.searchEmpty);
    ASSERT_EQ(result.verifiedRecords.size(), 3U);
    EXPECT_EQ(result.audit.verifiedEvaluated, 3U);
    // 非支配集 ⊆ 可行集（双标记——§7.4）；基线在可行集中（§8.2 基线参与
    // Pareto 作为可行方案之一）。
    EXPECT_FALSE(result.pareto.feasibleIds.empty());
    EXPECT_FALSE(result.pareto.nondominatedIds.empty());
    ASSERT_FALSE(result.pareto.entries.empty());
    // 稳定排序：entries 序＝(rank 升序→目标序→CandidateId)——包络为唯一
    // 区分指标时第一元素必为全局最优（rank 0）。
    EXPECT_EQ(result.pareto.entries.front().nondominationRank, 0U);
}

TEST(OptEvaluatorPorts, QuickFailureStaysEvaluationFailed_WP20T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"}, std::vector<std::string>{});

    // §8.4 失败语义行：Quick 失败不自动转确定性不可行——EvaluationFailed
    // 保持独立状态，不冒充 ScreenedOut（§7.5 区分表——状态词不同）。
    OrchestratorFixture f;
    f.compiler.result.ok = false;  // 编译失败全候选（EvaluationFailed 轨）
    f.compiler.result.stableCode = "RT-WC-COMPILE-FAILED";
    auto cfg = makeConfig(4, 2);
    auto request = readyRequest(f, cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());
    const TwoStageRunResult result = orch.run(request, f.ctx);

    ASSERT_EQ(result.quickRecords.size(), 4U);
    for (auto& rec : result.quickRecords) {
        EXPECT_EQ(rec.status, CandidateStatus::EvaluationFailed)
            << "编译失败候选保持 EvaluationFailed（不转不可行/不转 ScreenedOut）";
        EXPECT_TRUE(rec.extraRejections.empty())
            << "EvaluationFailed 不是淘汰——无 ScreenedOut 原因记录";
    }
    // 幸存集空 → 搜索空（Completed 非任务不可行——失败候选不是不可行）。
    EXPECT_TRUE(result.searchEmpty);
    EXPECT_TRUE(result.runCompleted);
}

TEST(OptEvaluatorPorts, SearchEmptyCompletedNotInfeasible_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-04", "OPT-06"},
                  std::vector<std::string>{"AT-09"});

    // OPT-VER-120：全部候选被硬约束淘汰 → OPT-SEARCH-EMPTY（warning）；
    // RunPhase=Completed 但 Pareto 空——**非任务不可行**。
    OrchestratorFixture f;
    f.projector.topologyOutOfScope = true;  // 基线链型范围外（全候选拓扑违例）
    auto cfg = makeConfig(4, 2);
    auto request = readyRequest(f, cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());
    const TwoStageRunResult result = orch.run(request, f.ctx);

    EXPECT_TRUE(result.runCompleted);
    EXPECT_EQ(result.runPhase, RunPhase::Completed);
    EXPECT_TRUE(result.searchEmpty);
    EXPECT_EQ(result.searchEmptyToken, std::string(optimization::kOptSearchEmpty));
    EXPECT_TRUE(result.pareto.feasibleIds.empty());
    for (auto& rec : result.quickRecords) {
        EXPECT_EQ(rec.status, CandidateStatus::Infeasible);
    }
}

TEST(OptEvaluatorPorts, SameSeedSameConfigEquivalentSetAndStableOrder_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "NFR-COR-02"},
                  std::vector<std::string>{"AT-09", "AT-10", "AT-11"});

    // OPT-VER-130：同种子同线程配置 ⇒ 等价候选集合与稳定排序（双跑重放——
    // 结论面逐字段等价；缓存缺席（无缓存会话）下全重算路径等价）。
    OrchestratorFixture f;
    auto cfg = makeConfig(6, 3, 11);
    auto request = readyRequest(f, cfg);

    TwoStageEvaluationOrchestrator first(OptimizationStage::StageB, f.deps());
    TwoStageEvaluationOrchestrator second(OptimizationStage::StageB, f.deps());
    const TwoStageRunResult r1 = first.run(request, f.ctx);
    const TwoStageRunResult r2 = second.run(request, f.ctx);

    // 等价候选集合（CandidateId 集合相等——跨运行稳定）。
    std::set<CandidateId> ids1;
    std::set<CandidateId> ids2;
    for (auto& rec : r1.quickRecords) {
        ids1.insert(rec.candidateId);
    }
    for (auto& rec : r2.quickRecords) {
        ids2.insert(rec.candidateId);
    }
    EXPECT_EQ(ids1, ids2) << "同种子同配置双跑必得等价候选集合（I-OPT-3）";

    // 稳定排序（Quick 记录状态序＋Pareto entries 序逐字段等价）。
    ASSERT_EQ(r1.quickRecords.size(), r2.quickRecords.size());
    for (std::size_t i = 0; i < r1.quickRecords.size(); ++i) {
        EXPECT_EQ(r1.quickRecords[i].candidateId, r2.quickRecords[i].candidateId);
        EXPECT_EQ(r1.quickRecords[i].status, r2.quickRecords[i].status)
            << "候选状态序一致（筛选确定性）";
    }
    ASSERT_EQ(r1.pareto.entries.size(), r2.pareto.entries.size());
    for (std::size_t i = 0; i < r1.pareto.entries.size(); ++i) {
        EXPECT_EQ(r1.pareto.entries[i].candidateId, r2.pareto.entries[i].candidateId);
        EXPECT_EQ(r1.pareto.entries[i].nondominationRank,
                  r2.pareto.entries[i].nondominationRank)
            << "Pareto 稳定排序一致（NFR-COR-02/DOPT-8）";
    }
}

TEST(OptEvaluatorPorts, MissingActiveMetricNeverEntersPareto_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-07", "NFR-COR-03"},
                  std::vector<std::string>{"AT-09"});

    // §7.4 编排侧执行点：激活目标槽位缺失（裕量事实全缺）的 Feasible 候选
    // → DataInsufficient（不按 0 合成、不参与支配比较）→ 幸存集空 →
    // 搜索空——"缺失指标候选不进 Pareto"的状态映射观测。
    OrchestratorFixture f;
    f.projector.dropMargins = true;  // MinJointMargin（激活目标）事实缺失
    auto cfg = makeConfig(4, 2);
    auto request = readyRequest(f, cfg);
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());
    const TwoStageRunResult result = orch.run(request, f.ctx);

    for (auto& rec : result.quickRecords) {
        EXPECT_EQ(rec.status, CandidateStatus::DataInsufficient)
            << "激活指标缺失的候选不冒充 Feasible（NFR-COR-03）";
        EXPECT_FALSE(rec.metrics.valueOf(optimization::MetricId::MinJointMargin).has_value())
            << "缺失指标输出'—'（不按 0 合成）";
    }
    EXPECT_TRUE(result.searchEmpty) << "无完整可行候选——搜索空（非不可行）";
    EXPECT_TRUE(result.runCompleted);
    EXPECT_TRUE(result.pareto.feasibleIds.empty());
}

TEST(OptEvaluatorPorts, CancellationPreservesPartialBatch_WP20T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "TASK-01"},
                  std::vector<std::string>{"AT-34"});

    // 批边界协作取消（TASK-01/§12.2 取消行）：取消后停止派发、已回传批
    // 保留、runPhase=Canceled、runCompleted=false（正常取消非错误）。
    OrchestratorFixture f;
    auto cfg = makeConfig(5, 2);
    auto request = readyRequest(f, cfg);
    CancelAfterContext cancelCtx(3);  // 放行 3 次查询（基线＋2 采样后取消）
    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, f.deps());
    const TwoStageRunResult result = orch.run(request, cancelCtx);

    EXPECT_FALSE(result.runCompleted);
    EXPECT_EQ(result.runPhase, RunPhase::Canceled);
    EXPECT_FALSE(result.searchEmpty);
    const std::size_t partial = result.quickRecords.size();
    EXPECT_GE(partial, 1U);
    EXPECT_LT(partial, 5U) << "取消后不得派发全部候选（批边界停止）";
}

// =====================================================================
// F-640：会话底账键补 mode 维度（Quick/Verified 同 sliceId 不互覆）
// =====================================================================

TEST(OptEvaluatorPorts, SessionLedgerReplayIsModeAware_WP20T06_F640)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "EVI-01"},
                  std::vector<std::string>{});

    // F-640（缺陷草案号）：会话底账曾以 sliceId 单键索引——同候选 Quick/
    // Verified 两批共用 sliceId（切片键要素只有 config/补丁/契约/Profile，
    // 不含 mode），后评估的 Verified 记录覆盖 Quick 记录；带缓存会话的
    // 第二次 run() 中 Quick FullHit 回放取出 screeningOnly=false 的
    // Verified 记录混入 quickRecords（EVI-01"Quick 不得产生正式效力"被
    // 击穿）。修复：底账键补 mode 维度（(sliceId, mode) 复合键——与缓存
    // 判定面 D-13"不升降级"同构），回放永远取同 mode 记录。
    OrchestratorFixture f;
    ScriptedSessionCache cache;
    TwoStageOrchestratorDeps d = f.deps();
    d.cache = &cache;
    auto cfg = makeConfig(/*maxCandidates=*/4, /*maxVerified=*/2);
    auto request = readyRequest(f, cfg);

    TwoStageEvaluationOrchestrator orch(OptimizationStage::StageB, d);

    // ---- 第一轮（DispatchNormal 脚本）：全重算并登记底账。 -------------
    // 同候选两条记录（Quick 效力面＋Verified 正式面）同 sliceId 并存——
    // 缺陷形态下后者覆盖前者。
    const TwoStageRunResult first = orch.run(request, f.ctx);
    ASSERT_TRUE(first.runCompleted);
    ASSERT_EQ(first.quickRecords.size(), 4U);
    ASSERT_EQ(first.verifiedRecords.size(), 2U);
    EXPECT_EQ(first.audit.cacheFullHits, 0U) << "首轮无命中（全重算）";
    EXPECT_EQ(first.audit.cacheLookups, 6U) << "4 Quick＋2 Verified 查找";
    for (const auto& rec : first.quickRecords) {
        EXPECT_TRUE(rec.screeningOnly);
        EXPECT_FALSE(rec.cacheHit);
    }

    // ---- 第二轮（ShortPath 脚本）：全部 FullHit 走会话底账回放。 -------
    cache.fullHit = true;
    const TwoStageRunResult second = orch.run(request, f.ctx);
    ASSERT_TRUE(second.runCompleted);
    ASSERT_EQ(second.quickRecords.size(), 4U);
    for (const auto& rec : second.quickRecords) {
        // 缺陷断言核心：Quick 回放必须取回 Quick 记录——不得混入同
        // sliceId 的 Verified 记录（缺陷形态下 screeningOnly/mode/资格
        // 三位全是 Verified 记录的值）。
        EXPECT_TRUE(rec.cacheHit) << "Quick 批 FullHit 回放（缓存有效性不回归）";
        EXPECT_TRUE(rec.screeningOnly)
            << "quickRecords 不得混入 Verified 记录（F-640 缺陷形态）";
        EXPECT_EQ(rec.mode, core::EvaluationMode::Quick);
        EXPECT_FALSE(rec.formalPassEligible)
            << "回放的 Quick 记录保持无正式资格（mode-not-verified）";
    }
    // 正向半区：Verified 批同键回放仍 FullHit（同 (sliceId, Verified) 键
    // 命中——mode 维度不影响既有缓存有效性）。
    ASSERT_EQ(second.verifiedRecords.size(), 2U);
    for (const auto& rec : second.verifiedRecords) {
        EXPECT_TRUE(rec.cacheHit);
        EXPECT_FALSE(rec.screeningOnly);
        EXPECT_EQ(rec.mode, core::EvaluationMode::Verified);
    }
    // 审计对账（G-1）：FullHit＝两批查找总数；回放不计实际评估；零
    // "回放不可用"（同 mode 键全部可取——修复语义的正向面）。
    EXPECT_EQ(second.audit.cacheLookups, 6U);
    EXPECT_EQ(second.audit.cacheFullHits, 6U);
    EXPECT_EQ(second.audit.quickEvaluated, 0U);
    EXPECT_EQ(second.audit.verifiedEvaluated, 0U);
    EXPECT_EQ(second.audit.cacheReplayUnavailable, 0U);
    // 查找模式序＝4 Quick 在前＋2 Verified 在后（两批编排序——底账键的
    // mode 维度与请求模式一一对应）。
    ASSERT_EQ(cache.queriedModes.size(), 12U);  // 两轮各 6 次
    for (std::size_t i = 0; i < 4U; ++i) {
        EXPECT_EQ(cache.queriedModes[6U + i], core::EvaluationMode::Quick);
    }
    for (std::size_t i = 4U; i < 6U; ++i) {
        EXPECT_EQ(cache.queriedModes[6U + i], core::EvaluationMode::Verified);
    }
}
