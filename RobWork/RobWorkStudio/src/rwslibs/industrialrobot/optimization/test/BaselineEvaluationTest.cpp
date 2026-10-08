/**
 * @file   BaselineEvaluationTest.cpp
 * @brief  基线方案评估（Evaluate Baseline，OPT-11）模型测试（WP-20-T08
 *         acceptance 2）——基线候选（空补丁）与候选**同管线同基准**；基线
 *         数据不足**不伪造完整比较**；基线评估失败**不包装为候选淘汰**
 *         （OPT-VER-134 三语义面）。
 *
 * 追溯：OPT-11（"支持基线方案评估（Evaluate Baseline）作为候选比较基准"）、
 * units/optimization.md §8.2（"基线候选：空补丁候选……总是生成并参与评估
 * ——基线评估与候选同管线、同需求/工况/策略/评估器基准；基线行在结果中
 * 标记 isBaseline，参与 Pareto；基线自身数据不足时不能伪造完整比较（基线
 * 候选按 §7.5 同判）；基线评估失败不能包装成候选淘汰（独立失败记录）"）、
 * §13.1 OPT-VER-134 行。
 *
 * 执行形态（诚实登记）：Evaluate Baseline 的执行件在 WP-20-T06 已落位
 * （generateSeededLhsCandidates 批首恒基线＋TwoStageEvaluationOrchestrator
 * 同管线评估＋基线 Quick-Feasible 恒占一席）——本文件不加第二套基线评估
 * 代码，以 T06 编排公共接口消费其三语义面（OPT-VER-134 验收钉扎）。测试
 * 形态与 EvaluatorPortsTest 同款：③端口脚本化评估器进真注册表、投影/
 * 探针/编译可控替身注入、接口路径天然钉住。
 */

#include <sdurws/ird/optimization/EvaluatorPorts.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/optimization/CandidatePatch.hpp>
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
#include <string>
#include <utility>
#include <vector>

namespace optimization = sdurws::ird::optimization;
namespace evidence = sdurws::ird::evidence;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 通用替身与构造辅助（EvaluatorPortsTest 同款形态精简——本文件自持）
// =====================================================================

/// 修订闭包来源替身（全部 (oid,cv) 声明在册）。
class AllPresentClosure final : public evidence::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 评估调用上下文替身（零取消/零进度——基线用例不涉取消）。
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

/// 非零内容身份。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// 闭包条目（digest=cv 字节——builder 一致性闸门）。
evidence::ObjectRefEntry makeClosureEntry(std::string typeToken)
{
    evidence::ObjectRefEntry entry;
    entry.objectId = core::ObjectId::generate();
    entry.contentVersion = core::ContentVersion::fromCanonical(
        "cv-0000000000000000000000000000000000000000000000000000000000000001");
    entry.objectTypeToken = std::move(typeToken);
    entry.digest = entry.contentVersion.bytes;
    return entry;
}

/// 冻结快照（基线评估的输入基准——两必验工况；工件面与编排无交叉检查，
/// 最小集即可）。
evidence::AnalysisSnapshot makeSnapshot()
{
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    builder.addObjectRef(makeClosureEntry("robot-design"));
    evidence::PolicyRef policy;
    policy.policyContentIdentity = makeContentIdentity(4);
    builder.setPolicyRef(policy);
    evidence::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = makeContentIdentity(5);
    builder.setNameMapRef(nameMap);
    for (const auto& c : {core::ObjectId::generate(), core::ObjectId::generate()}) {
        builder.addCase({c, "必验工况", true, true});
    }
    evidence::ReproductionBlock repro;
    repro.productVersion = "test";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    return builder.build(AllPresentClosure{});
}

/// 覆盖矩阵（全工况执行——EVI-02 覆盖完备；基线与候选共享同一矩阵＝
/// "同基准"的工况面）。
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

/// ③端口脚本化 kin 评估器（空输出——Feasible 由覆盖完备＋限位合法得到）。
class ScriptedKinEvaluator final : public evidence::IEngineeringEvaluator {
public:
    ScriptedKinEvaluator(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override
    {
        return m_descriptor;
    }
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override
    {
        return {};
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
    const evidence::EvaluatorDescriptor& descriptor() const override
    {
        return m_descriptor;
    }
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<ScriptedKinEvaluator>(m_descriptor);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
};

/// 合法 kin 评估器描述符（契约版本 1；Profile kin/1.0）。
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

// ---- 探针/编译替身（T04/T06 同款）----

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
    compile(const optimization::CandidatePatch&, const std::vector<optimization::VariableBinding>&) const override
    {
        return result;
    }
};

/// 候选投影替身——包络随补丁值线性变化（候选间互异）、质量/裕量恒定；
/// `dropMarginsForBaseline` 脚本：空补丁（基线）时缺失裕量事实——
/// "基线数据不足"场景的注入点（OPT-VER-134 第二语义面）。
class ScriptedProjector final : public optimization::ICandidateProjector {
public:
    mutable int calls = 0;                 ///< 投影调用计数（基线也被投影＝
                                            ///  同管线的观测面）
    bool topologyOutOfScope = false;
    bool dropMarginsForBaseline = false;   ///< 仅基线（空补丁）缺裕量事实
    double massKg = 10.0;                  // kg（质量目标无区分度）
    double margin = 0.4;                   // D-KIN-2 归一化裕量（无量纲）

    optimization::CandidateProjection
    project(const optimization::CandidatePatch& patch,
            const std::vector<optimization::VariableBinding>&) const override
    {
        ++calls;
        optimization::CandidateProjection p;
        p.baselineChainInEnabledScope = !topologyOutOfScope;
        optimization::JointLimitSpecRecord joint;
        joint.jointSubject = "obj-probe-joint";
        joint.qmin = -3.14;  // rad（转动关节下限）
        joint.qmax = 3.14;   // rad（转动关节上限）
        p.jointLimits.joints = {joint};
        p.jointLimits.configurations = {{0.0}};  // 关节角 0 rad 单构型
        double v = 0.5;
        if (!patch.items.empty()) {
            v = patch.items.front().scalarValue;
        }
        p.metricFacts.envelope = optimization::EnvelopeFacts{-v, -v, -v, v, v, v};
        if (massKg > 0.0) {
            optimization::LinkMassFact lm;
            lm.linkSubject = "obj-link";
            lm.massKg = massKg;
            lm.provenanceToken = "provided";
            p.metricFacts.linkMasses = {lm};
        }
        const bool baselinePatch = patch.items.empty();
        if (!(dropMarginsForBaseline && baselinePatch)) {
            optimization::PointMarginFact pm;
            pm.caseIdText = "case-margin";
            pm.minMargin = margin;
            pm.unitSymbol = "1";
            p.metricFacts.pointMargins = {pm};
        }
        return p;
    }
};

// ---- 编排装配夹具（EvaluatorPortsTest OrchestratorFixture 同款）----

struct BaselineFixture {
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    FakeJointLimitProbe jointProbe;
    FakeCollisionProbe collisionProbe;
    FakeCompiler compiler;
    NopEvaluationContext ctx;
    ScriptedProjector projector;
    optimization::ParetoFrontBuilder paretoBuilder;
    core::ContentIdentity profileIdentity;
    optimization::StaticHardConstraintPipeline pipeline;

    BaselineFixture()
        : profileIdentity(makeContentIdentity(0))
        , pipeline(optimization::OptimizationStage::StageB, pipelineDeps())
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

    optimization::StaticHardConstraintDeps pipelineDeps()
    {
        optimization::StaticHardConstraintDeps d;
        d.evaluators = &evaluators;
        d.jointLimitProbe = &jointProbe;
        d.collisionProbe = &collisionProbe;
        d.compiler = &compiler;
        d.producers = &evaluators;
        d.profiles = &profiles;
        return d;
    }

    optimization::TwoStageOrchestratorDeps deps()
    {
        optimization::TwoStageOrchestratorDeps d;
        d.pipeline = &pipeline;
        d.projector = &projector;
        d.paretoBuilder = &paretoBuilder;
        d.cache = nullptr;
        return d;
    }
};

/// 单连续绑定（DH a，m——R1 权威字段）。
optimization::VariableBinding makeLengthBinding()
{
    optimization::VariableBinding b;
    b.bindingId = "mdl.joint[2].dh.a";
    b.kind = optimization::VariableKind::Continuous;
    b.unit = core::UnitToken::find("m").value();
    b.lowerBound = 0.2;   // m
    b.upperBound = 0.8;   // m
    b.defaultValue = 0.5; // m（基线值）
    b.authorized = true;
    b.locked = false;
    b.authorityFieldPath = "robot-design/joints[2]/dh/a";
    b.diagSubject = core::ObjectId::generate().toCanonical();
    return b;
}

/// 已校验配置（三项静态目标；maxCandidates=4——基线＋3 采样）。
optimization::OptimizationConfiguration makeConfig()
{
    optimization::OptimizationConfiguration c;
    c.stage = optimization::OptimizationStage::StageB;
    c.seed = 7;
    c.budget.maxCandidates = 4;
    c.budget.maxVerifiedCandidates = 4;
    c.strategyId = std::string(optimization::kStrategySeededLhs);
    c.objectives
        = optimization::defaultObjectives(optimization::OptimizationStage::StageB);
    c.variables = {makeLengthBinding()};
    optimization::validateConfiguration(c);
    return c;
}

/// 冻结请求（身份面非保留值；Profile 权威值回填）。
optimization::TwoStageRunRequest
makeReadyRequest(const BaselineFixture& f, const evidence::AnalysisSnapshot& s,
                 const optimization::OptimizationConfiguration& cfg)
{
    optimization::TwoStageRunRequest r;
    r.config = cfg;
    r.baselineRoot = core::ObjectId::generate();
    r.baselineCv = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000bb");
    r.snapshot = s;
    r.task = {core::ProjectId::generate(), core::BranchId::generate(),
              core::RevisionId::generate(), core::RunId::generate(),
              core::AttemptId{1U}};
    r.profile.profileId = "kin";
    r.profile.version = "1.0";
    r.profile.contentIdentity = f.profileIdentity;
    r.coverage = makeCoverage(s);
    r.quickCoverage = makeCoverage(s);
    return r;
}

/// 按基线标记查找记录。
const optimization::TwoStageRunRecord*
findBaseline(const std::vector<optimization::TwoStageRunRecord>& records)
{
    for (const auto& r : records) {
        if (r.isBaseline) {
            return &r;
        }
    }
    return nullptr;
}

}  // namespace

// =====================================================================
// 语义面一：基线与候选同管线同基准（OPT-VER-134 / §8.2 基线候选行）
// =====================================================================

TEST(OptBaselineEvaluation, BaselineSamePipelineAndBasis_WP20T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    BaselineFixture f;
    const auto snapshot = makeSnapshot();
    const auto cfg = makeConfig();
    const auto request = makeReadyRequest(f, snapshot, cfg);
    optimization::TwoStageEvaluationOrchestrator orchestrator(
        optimization::OptimizationStage::StageB, f.deps());

    const auto result = orchestrator.run(request, f.ctx);

    // 基线恒为批首且标记 isBaseline、补丁为空（空补丁候选——§8.2）。
    ASSERT_FALSE(result.quickRecords.empty());
    const auto& first = result.quickRecords.front();
    EXPECT_TRUE(first.isBaseline) << "批首必须是基线候选";
    EXPECT_TRUE(first.patch.items.empty());
    // 基线身份＝candidateIdOf(基线根, 基线 cv, 空补丁)（§4.2 公式——确定性）。
    EXPECT_TRUE(first.candidateId
                == optimization::candidateIdOf(request.baselineRoot,
                                               request.baselineCv,
                                               optimization::CandidatePatch{}));
    // 同管线：基线与候选都经同一投影/管线评估（投影调用计数 ≥ 候选批规模
    // ——基线被投影即证明它走了同一管线，而非旁路特殊通道）。
    const std::size_t batch = result.quickRecords.size();
    EXPECT_GE(f.projector.calls, static_cast<int>(batch));
    // 同基准：基线与候选共享同一请求的快照/覆盖矩阵/评估器集（编排层
    // 单一请求面——TwoStageRunRequest 只有一份 snapshot/coverage/Profile，
    // 结构上不可能给基线第二基准；此处钉扎基线记录的 sliceId 均非保留值
    // ＝同一冻结切片机制为其计算）。
    for (const auto& record : result.quickRecords) {
        EXPECT_TRUE(record.sliceId.isValid())
            << "每个候选（含基线）必须有冻结切片身份（同机制同基准）";
    }
    // 基线 Quick-Feasible 时恒占一席进入 Verified（OPT-11"基线评估作比较
    // 基准"的编排侧保守化——比较基准不得被预算线筛掉）。
    const auto* quickBaseline = findBaseline(result.quickRecords);
    ASSERT_NE(quickBaseline, nullptr);
    if (quickBaseline->status == optimization::CandidateStatus::Feasible) {
        const auto* verifiedBaseline = findBaseline(result.verifiedRecords);
        ASSERT_NE(verifiedBaseline, nullptr)
            << "Quick-Feasible 基线必须占一席进入 Verified 复核";
        EXPECT_EQ(verifiedBaseline->status, optimization::CandidateStatus::Feasible);
    } else {
        ADD_FAILURE() << "正例投影下基线应为 Feasible";
    }
}

// =====================================================================
// 语义面二：基线数据不足不伪造完整比较（OPT-VER-134 / §8.2）
// =====================================================================

TEST(OptBaselineEvaluation, BaselineInsufficientNotFabricated_WP20T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "NFR-COR-03"},
                  std::vector<std::string>{"AT-09"});

    BaselineFixture f;
    f.projector.dropMarginsForBaseline = true;  // 仅基线（空补丁）缺裕量事实
    const auto snapshot = makeSnapshot();
    const auto cfg = makeConfig();
    const auto request = makeReadyRequest(f, snapshot, cfg);
    optimization::TwoStageEvaluationOrchestrator orchestrator(
        optimization::OptimizationStage::StageB, f.deps());

    const auto result = orchestrator.run(request, f.ctx);

    // 基线激活指标缺失 → DataInsufficient（§7.5 同判——不按零值合成、
    // 不伪造完整比较；候选状态≠可行）。
    const auto* baseline = findBaseline(result.quickRecords);
    ASSERT_NE(baseline, nullptr);
    EXPECT_EQ(baseline->status, optimization::CandidateStatus::DataInsufficient)
        << "基线数据不足必须按 §7.5 同判为 DataInsufficient";
    EXPECT_FALSE(baseline->formalPassEligible);
    // 不进 Pareto：Pareto 输入只取 Verified-Feasible 且指标完整——数据不足
    // 的基线绝不参与支配比较（"不伪造完整比较"的直接观测）。
    const auto* verifiedBaseline = findBaseline(result.verifiedRecords);
    EXPECT_EQ(verifiedBaseline, nullptr)
        << "DataInsufficient 基线不得进入 Verified 复核批（更不得进 Pareto）";
}

// =====================================================================
// 语义面三：基线评估失败不包装为候选淘汰（OPT-VER-134/123 / §7.5）
// =====================================================================

TEST(OptBaselineEvaluation, BaselineFailureNotScreenedOut_WP20T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    BaselineFixture f;
    f.compiler.result.ok = false;  // 编译链故障——全部候选（含基线）评估失败
    const auto snapshot = makeSnapshot();
    const auto cfg = makeConfig();
    const auto request = makeReadyRequest(f, snapshot, cfg);
    optimization::TwoStageEvaluationOrchestrator orchestrator(
        optimization::OptimizationStage::StageB, f.deps());

    const auto result = orchestrator.run(request, f.ctx);

    // 基线评估失败 → EvaluationFailed 独立失败记录（环境错误语义——
    // §7.5 区分表"评估器失败≠约束失败"），**绝不**包装为 ScreenedOut
    // （Quick 保守淘汰）或 Infeasible（工程判定）。
    const auto* baseline = findBaseline(result.quickRecords);
    ASSERT_NE(baseline, nullptr);
    EXPECT_EQ(baseline->status, optimization::CandidateStatus::EvaluationFailed)
        << "基线评估失败必须保持 EvaluationFailed（独立失败记录）";
    EXPECT_NE(baseline->status, optimization::CandidateStatus::ScreenedOut)
        << "评估失败不得包装为候选淘汰（ScreenedOut）";
    EXPECT_NE(baseline->status, optimization::CandidateStatus::Infeasible)
        << "评估失败不得包装为工程不可行（Infeasible）";
    // 淘汰原因面：编译失败原因在记录中（OPT-CANDIDATE-COMPILE-FAILED
    // 包装＋RT-* 透传——T04 I-C2 语义），不是预算线筛选原因。
    bool hasCompileRejection = false;
    for (const auto& reason : baseline->evaluation.rejections) {
        if (reason.reasonToken == std::string(optimization::kRejectCandidateCompileFailed)) {
            hasCompileRejection = true;
        }
    }
    EXPECT_TRUE(hasCompileRejection)
        << "基线失败记录必须携带编译失败淘汰原因（独立失败语义）";
    // 无幸存集 → 搜索空正常完成（OPT-SEARCH-EMPTY warning——非任务不可行）。
    EXPECT_TRUE(result.verifiedRecords.empty());
}
