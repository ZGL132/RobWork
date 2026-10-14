/**
 * @file   JointStrategyTest.cpp
 * @brief  分层联合策略用例组（WP-21-T03——§8.5 内层 Quick→Verified→器件
 *         匹配反馈闭环＋opt-joint-full 登记值逐字钉扎＋upstream.* 门面
 *         切片身份传播＋StageD 组合空间采样确定性）。
 *
 * 设计依据：
 *   - units/optimization.md §8.5/§6.3/§6.7（WP-21-T02 登记值——联合评估
 *     键/契约/payload/magic/upstream.* 依赖链逐字冻结）、§7.5（六种情形
 *     语义独立）、§8.2（组合空间采样）、§8.3（阶段能力检查/失败语义）、
 *     §13.2（OPT-VER-202/203~208/247/248/253 用例锚）
 *   - 任务契约 tasks/foundation/WP-21-T03.json acceptance 1（内层闭环
 *     用例通过——本文件主体）
 *   - 判定阈值边界（P-04 表④）：本文件零判定阈值数值、零鲁棒性判定
 *     结论——鲁棒性三模式归 WP-21-T05（判定阈值两槽位待所有者补充确认，
 *     涉及面一律登记待补充确认——依赖诚实，不私设数值）
 *
 * 测试形态：模型层替身注入（真 evidence 注册表＋脚本化内层评估器/层切片
 * 供给——与 EvaluatorPortsTest/ConstraintTest 同款自持替身纪律；测试零
 * Qt 消费）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Errors.hpp>  // evidence::EvidenceError——脚本化失败注入
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/JointStrategy.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Preflight.hpp>  // kOptProfileId——域 Profile id 常量
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace sdurws::ird;
using optimization::buildJointFacadeSlice;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::CandidateStatus;
using optimization::generateStageDJointCandidates;
using optimization::JointLayeredChecker;
using optimization::JointLayeredCheckDeps;
using optimization::JointLayeredCheckInput;
using optimization::JointLayerOutcome;
using optimization::JointUpstreamRef;
using optimization::makeJointFacadeDescriptor;
using optimization::OptimizationConfiguration;
using optimization::OptimizationError;
using optimization::OptimizationStage;
using optimization::StageDCombination;
using optimization::VariableBinding;
using optimization::VariableKind;

namespace {

// =====================================================================
// 通用替身与构造辅助（模型测试自持——EvaluatorPortsTest 同款形态精简）
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

/// 评估调用上下文替身（零取消/零进度——层间取消用独立计数替身）。
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

/// 第 N 次查询后取消的上下文替身（层间协作取消——TASK-01 编排侧观测）。
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
    std::uint32_t m_after;               ///< 放行查询数（之后即取消）
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

/// 两工况（均 enabled+mandatory——覆盖矩阵分母）。
std::vector<evidence::CaseEntry> makeTwoMandatoryCases()
{
    return {{core::ObjectId::generate(), "额定工况", true, true},
            {core::ObjectId::generate(), "极限工况", true, true}};
}

/// 覆盖矩阵（全必验工况执行——EVI-02 完备分母投影）。
evidence::CaseCoverageMatrix makeCoverage(const evidence::AnalysisSnapshot& snapshot)
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

/// StageD 已校验基线配置（八项目标默认激活面＋预算可调）。
OptimizationConfiguration makeStageDConfig(std::uint64_t seed = 7)
{
    OptimizationConfiguration c;
    c.stage = OptimizationStage::StageD;
    c.seed = seed;
    c.budget.maxCandidates = 4;
    c.budget.maxVerifiedCandidates = 2;
    c.strategyId = std::string(optimization::kStrategySeededLhs);
    c.objectives = optimization::defaultObjectives(OptimizationStage::StageD);
    optimization::validateConfiguration(c);
    return c;
}

/// 基线根身份（CandidateId 公式输入——非保留值）。
CandidateId baselineCandidateId()
{
    return optimization::candidateIdOf(
        core::ObjectId::generate(),
        core::ContentVersion::fromCanonical(
            "cv-00000000000000000000000000000000000000000000000000000000000000cc"),
        CandidatePatch{});
}

/// 四条上游引用（合法基线——sliceId 逐条互异）。
std::vector<JointUpstreamRef> makeUpstreams()
{
    static constexpr std::string_view tokens[4]
        = {optimization::kUpstreamTrjPlanToken, optimization::kUpstreamDynRneaToken,
           optimization::kUpstreamDtMappingToken,
           optimization::kUpstreamSelCombinationToken};
    static constexpr std::string_view keys[4]
        = {optimization::kInnerTrjPlanKey, optimization::kInnerDynRneaKey,
           optimization::kInnerDtMappingKey, optimization::kInnerSelCombinationKey};
    std::vector<JointUpstreamRef> refs;
    for (int i = 0; i < 4; ++i) {
        JointUpstreamRef ref;
        ref.dependencyToken = std::string(tokens[i]);
        ref.upstreamKey = std::string(keys[i]);
        ref.upstreamSliceId = makeContentIdentity(static_cast<unsigned char>(0x10 + i));
        refs.push_back(std::move(ref));
    }
    return refs;
}

// ---- ③端口脚本化内层评估器（记录派发序＋可注入违例/失败/搜索未果）----

/// 一次层派发的观测（评估键＋请求模式——内层闭环的派发序断言面）。
struct DispatchObservation {
    std::string evaluationKey;
    core::EvaluationMode mode;
};

/**
 * @brief 内层脚本状态（fixture 持有——评估器实例每次 create 新建、共享
 *        同一脚本；why：create() 契约返回独占所有权实例〔默认删除器〕，
 *        脚本状态不能驻留实例本体——每次派发的新实例消费同一脚本面）。
 */
struct InnerScript {
    /// Must 违例脚本（注入面——非空即随产出返回）。
    evidence::DomainVerdictInputs mustScript;
    /// 搜索未果脚本（注入面——有值即随产出返回）。
    std::optional<evidence::SearchExhaustedRecord> searchScript;
    /// 失败脚本（注入面——true 即抛 EvidenceError）。
    bool failScript = false;
    /// 派发记录（fixture 的 dispatchLog 引用——脚本写回观测面）。
    std::vector<DispatchObservation>* log = nullptr;
    /// 本域评估键（记录面——实例描述符同值）。
    std::string evaluationKey;
};

/**
 * @brief 脚本化内层评估器：记录每次派发（键＋模式）；按共享脚本返回
 *        Must 违例/搜索未果或抛 EvidenceError（各域失败注入面）。
 */
class ScriptedInnerEvaluator final : public evidence::IEngineeringEvaluator {
public:
    ScriptedInnerEvaluator(evidence::EvaluatorDescriptor descriptor,
                           const InnerScript& script)
        : m_descriptor(std::move(descriptor))
        , m_script(script)
    {
    }

    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }

    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext&) override
    {
        m_script.log->push_back({m_descriptor.key, request.mode});
        if (m_script.failScript) {
            // 脚本化注入：环境轨失败（EvidenceError——I-C2 错误轨；码值
            // 取 EvidenceMissing 近似语义——脚本注入不承载域真实码面）。
            throw evidence::EvidenceError(evidence::EvidenceErrorCode::EvidenceMissing,
                                          "脚本化注入：内层评估失败（环境轨）");
        }
        evidence::EvaluationOutput output;
        output.verdictInputs = m_script.mustScript;
        output.searchRecord = m_script.searchScript;
        return output;
    }

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor 引用所指）
    const InnerScript& m_script;                ///< 共享脚本（fixture 持有，非拥有）
};

/**
 * @brief 脚本化内层评估器工厂（descriptor 注册面；create 每次新建实例
 *        ——独占所有权契约成立；脚本状态经引用共享〔fixture 生命周期〕）。
 */
class ScriptedInnerFactory final : public evidence::IEvaluatorFactory {
public:
    ScriptedInnerFactory(evidence::EvaluatorDescriptor descriptor,
                         const InnerScript& script)
        : m_descriptor(std::move(descriptor))
        , m_script(script)
    {
    }

    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<ScriptedInnerEvaluator>(m_descriptor, m_script);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（注册面）
    const InnerScript& m_script;                ///< 共享脚本（非拥有）
};

/**
 * @brief 脚本化层切片供给（SliceBuilder 唯一合法生产者——每层评估键＋
 *        定位 token＋上游引用状态进 Environment 条目 ⇒ 切片身份随层与
 *        上游状态确定且互异；请求序记录为断言面）。
 */
class ScriptedLayerSlices final : public optimization::IJointLayerSliceProvider {
public:
    const evidence::AnalysisSnapshot* snapshot = nullptr; ///< 冻结快照（build 基准）
    mutable std::vector<std::string> layerTokens;         ///< 请求序（layerToken）

    evidence::InputSlice layerSlice(const std::string& layerToken,
                                    const std::string& evaluationKey,
                                    const CandidatePatch&,
                                    const std::vector<VariableBinding>&,
                                    const std::vector<JointUpstreamRef>& upstreams) const override
    {
        layerTokens.push_back(layerToken);
        evidence::SliceBuilder builder;
        builder.setEvaluation(evaluationKey, 1U);
        // 探针条目：定位 token＋上游引用数＋末条上游 sliceId 规范文本——
        // 层序与上游状态变化都反映进切片身份（测试面互异性；生产适配器
        // 按各域 descriptor 组装真实条目——本替身只保证可冻结可区分）。
        evidence::DependencyEntry probe;
        probe.key = "test.layer-probe";
        probe.kind = evidence::DependencyKind::Environment;
        evidence::EnvironmentDependencyPayload payload;
        payload.token = "test.layer-probe";
        std::string value = layerToken + "|" + std::to_string(upstreams.size());
        if (!upstreams.empty()) {
            value += "|" + upstreams.back().upstreamSliceId.toCanonical();
        }
        payload.valueToken = value;
        probe.payload = payload;
        builder.addEntry(std::move(probe));
        return builder.build(*snapshot);
    }
};

/// 联合闭环完整依赖面（真注册表＋opt Profile＋脚本化内层评估器组）。
struct JointFixture {
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    std::vector<DispatchObservation> dispatchLog;
    ScriptedLayerSlices layerSlices;
    NopEvaluationContext ctx;
    core::ContentIdentity optProfileIdentity;  ///< 注册权威值（构造期回填）
    evidence::AnalysisSnapshot snapshotHolder; ///< 冻结快照（fixture 单例）

    /**
     * @param registerSelection [in] false＝不注册 selection 评估器（缺键
     *                          装配漂移场景注入——MissingInnerEvaluator 用例）
     */
    explicit JointFixture(bool registerSelection = true)
        : snapshotHolder(makeSnapshot(makeTwoMandatoryCases()))
    {
        layerSlices.snapshot = &snapshotHolder;
        // Profile 注册在前（evidence §13 时序——opt 域空必需项集：完备性
        // 核对平凡通过；逐项对齐归 WP-21-T04 装配面）。
        evidence::RequiredEvidenceProfile profile;
        profile.profileId = std::string(optimization::kOptProfileId);
        profile.version = "1.0";
        profiles.registerProfile(profile);
        optProfileIdentity
            = profiles.findProfile(std::string(optimization::kOptProfileId), "1.0")
                  ->contentIdentity;
        // 四内层评估器注册（各域登记键——kebab 落位偏差消费拼写；描述符
        // 双模式支持面同真实域声明；脚本状态 fixture 持有、实例 create
        // 时新建〔独占所有权契约〕）。
        for (const auto* key :
             {&optimization::kInnerTrjPlanKey, &optimization::kInnerDynRneaKey,
              &optimization::kInnerDtMappingKey,
              &optimization::kInnerSelCombinationKey}) {
            if (!registerSelection && *key == optimization::kInnerSelCombinationKey) {
                continue;  // 缺键场景（装配漂移注入）
            }
            auto script = std::make_unique<InnerScript>();
            script->log = &dispatchLog;
            script->evaluationKey = std::string(*key);
            evidence::EvaluatorDescriptor d;
            d.key = std::string(*key);
            d.contractVersion = 1U;
            d.profile.profileId = std::string(optimization::kOptProfileId);
            d.profile.version = "1.0";
            d.supportedModes = {core::EvaluationMode::Quick,
                                core::EvaluationMode::Verified};
            d.stateless = true;
            d.threadSafety = evidence::ThreadSafety::SingleThread;
            evaluators.registerEvaluator(
                std::make_unique<ScriptedInnerFactory>(d, *script), {});
            registeredKeys.push_back(std::string(*key));
            scripts.push_back(std::move(script));
        }
    }

    /// 按评估键定位脚本（脚本注入面——注册键表线性查）。
    InnerScript& script(std::string_view key)
    {
        for (std::size_t i = 0; i < registeredKeys.size(); ++i) {
            if (registeredKeys[i] == key) {
                return *scripts[i];
            }
        }
        throw std::logic_error("fixture: 未注册评估器键 " + std::string(key));
    }

    std::vector<std::string> registeredKeys;      ///< 注册序键表
    std::vector<std::unique_ptr<InnerScript>> scripts; ///< 脚本状态（fixture 持有）

    /// 联合编排依赖面。
    JointLayeredCheckDeps deps()
    {
        JointLayeredCheckDeps d;
        d.evaluators = &evaluators;
        d.layerSlices = &layerSlices;
        d.producers = &evaluators;  // EvaluatorRegistry 实现 IProducerRegistryView
        d.profiles = &profiles;     // EvidenceProfileRegistry 实现 IProfileRegistryView
        return d;
    }

    /// 就绪联合请求（快照/覆盖/任务面就绪——正例基线）。
    JointLayeredCheckInput readyInput(const OptimizationConfiguration& config)
    {
        JointLayeredCheckInput in;
        in.candidateId = baselineCandidateId();
        in.isBaseline = true;
        in.patch = CandidatePatch{};
        in.config = config;
        in.jointMode = core::EvaluationMode::Verified;
        in.snapshot = snapshotHolder;
        in.task = {core::ProjectId::generate(), core::BranchId::generate(),
                   core::RevisionId::generate(), core::RunId::generate(),
                   core::AttemptId{1U}};
        in.coverage = makeCoverage(snapshotHolder);
        return in;
    }
};

}  // namespace

// =====================================================================
// 登记值逐字钉扎（§6.7——OPT-VER-253 键名/契约/payload/magic 面）
// =====================================================================

TEST(OptJointStrategy, FacadeRegistrationValuesMatchCard_WP21T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-05", "P-OPT-9"},
                  std::vector<std::string>{"AT-35"});

    // §6.7 登记值逐字一致（WP-21-T02 冻结值——漂移即消费面破坏；本断言
    // 是"登记值一次冻结"的机械锚）。
    EXPECT_EQ(optimization::kOptJointFullKey, "opt-joint-full");
    EXPECT_EQ(optimization::kOptJointFullContractVersion, 1U);
    EXPECT_EQ(optimization::kOptJointFullPayloadToken, "opt.joint-full.v1");
    EXPECT_EQ(optimization::kOptJointFullCanonicalMagic, "IRDOPTJ1");
    EXPECT_EQ(optimization::kUpstreamTrjPlanToken, "upstream.trj-sequence-plan");
    EXPECT_EQ(optimization::kUpstreamDynRneaToken, "upstream.dyn-rnea-analysis");
    EXPECT_EQ(optimization::kUpstreamDtMappingToken, "upstream.dt.mapping");
    EXPECT_EQ(optimization::kUpstreamSelCombinationToken,
              "upstream.sel.combination-check");
    // 内层消费键（各域登记值——trj/dyn 与域卡逐字一致，dt/sel 为各域
    // 已登记的 kebab 落位偏差拼写）。
    EXPECT_EQ(optimization::kInnerTrjPlanKey, "trj-sequence-plan");
    EXPECT_EQ(optimization::kInnerDynRneaKey, "dyn-rnea-analysis");
    EXPECT_EQ(optimization::kInnerDtMappingKey, "dt-mapping");
    EXPECT_EQ(optimization::kInnerSelCombinationKey, "sel-combination-check");

    // descriptor 字段落值（§6.7 行原文——八条依赖/Profile/模式/状态面）。
    const auto d = makeJointFacadeDescriptor();
    EXPECT_EQ(d.key, "opt-joint-full");
    EXPECT_EQ(d.contractVersion, 1U);
    ASSERT_EQ(d.inputs.size(), 8U);
    EXPECT_EQ(d.inputs[0].key, "config.opt");
    EXPECT_EQ(d.inputs[0].kind, evidence::DependencyKind::Configuration);
    EXPECT_EQ(d.inputs[1].key, "policy.resolved");
    EXPECT_EQ(d.inputs[1].kind, evidence::DependencyKind::Policy);
    EXPECT_EQ(d.inputs[2].key, "namemap");
    EXPECT_EQ(d.inputs[2].kind, evidence::DependencyKind::NameMap);
    EXPECT_EQ(d.inputs[3].key, "catalog.lock");
    EXPECT_EQ(d.inputs[3].kind, evidence::DependencyKind::Object);
    for (std::size_t i = 4; i < 8; ++i) {
        EXPECT_EQ(d.inputs[i].kind, evidence::DependencyKind::UpstreamResult);
        EXPECT_EQ(d.inputs[i].requiredness,
                  evidence::DependencyRequiredness::Required);
    }
    EXPECT_EQ(d.inputs[4].key, "upstream.trj-sequence-plan");
    EXPECT_EQ(d.inputs[5].key, "upstream.dyn-rnea-analysis");
    EXPECT_EQ(d.inputs[6].key, "upstream.dt.mapping");
    EXPECT_EQ(d.inputs[7].key, "upstream.sel.combination-check");
    EXPECT_EQ(d.profile.profileId, "opt");
    EXPECT_EQ(d.profile.version, "1.0");
    ASSERT_EQ(d.supportedModes.size(), 2U);
    EXPECT_EQ(d.supportedModes[0], core::EvaluationMode::Quick);
    EXPECT_EQ(d.supportedModes[1], core::EvaluationMode::Verified);
    EXPECT_TRUE(d.stateless);
    EXPECT_EQ(d.threadSafety, evidence::ThreadSafety::SingleThread);

    // 真注册表注册通过（Profile 先行——evidence §13；登记值过注册期校验
    // ＝登记面自洽的可执行证明）。
    evidence::EvidenceProfileRegistry profiles;
    evidence::RequiredEvidenceProfile profile;
    profile.profileId = std::string(optimization::kOptProfileId);
    profile.version = "1.0";
    profiles.registerProfile(profile);
    evidence::EvaluatorRegistry registry(profiles);
    class FacadeFactory final : public evidence::IEvaluatorFactory {
    public:
        explicit FacadeFactory(evidence::EvaluatorDescriptor d)
            : m_d(std::move(d))
        {
        }
        const evidence::EvaluatorDescriptor& descriptor() const override { return m_d; }
        std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
        {
            return nullptr;  // 注册期校验不触 create——本测试面不需要实例
        }

    private:
        evidence::EvaluatorDescriptor m_d;
    };
    registry.registerEvaluator(
        std::make_unique<FacadeFactory>(makeJointFacadeDescriptor()), {});
    EXPECT_NE(registry.find(std::string(optimization::kOptJointFullKey)), nullptr);
}

// =====================================================================
// upstream.* 门面切片身份传播（CON-04——OPT-VER-253 sliceId 面）
// =====================================================================

TEST(OptJointStrategy, FacadeSliceIdentityPropagatesUpstreamChange_WP21T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-05", "P-OPT-9", "CON-04"},
                  std::vector<std::string>{"AT-35"});

    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto config = makeStageDConfig();
    const CandidatePatch patch;
    const auto refs = makeUpstreams();

    // 同输入 ⇒ 同 sliceId（确定性——缓存命中前提）。
    const auto a = buildJointFacadeSlice(snapshot, config, patch, refs);
    const auto b = buildJointFacadeSlice(snapshot, config, patch, refs);
    EXPECT_EQ(a.sliceId.bytes, b.sliceId.bytes);
    EXPECT_EQ(a.evaluationKey, "opt-joint-full");
    EXPECT_EQ(a.evaluatorContractVersion, 1U);

    // 任一条上游 sliceId 变更 ⇒ 门面 sliceId 变更（CON-04 失效传播——
    // 四条逐一钉扎；其余三条不变时差异只能来自该条——身份敏感面）。
    for (std::size_t i = 0; i < 4; ++i) {
        auto changed = refs;
        changed[i].upstreamSliceId
            = makeContentIdentity(static_cast<unsigned char>(0x50 + i));
        const auto s = buildJointFacadeSlice(snapshot, config, patch, changed);
        EXPECT_NE(s.sliceId.bytes, a.sliceId.bytes)
            << "上游引用 " << i << " 的 sliceId 变更未传播进门面切片身份";
    }

    // 补丁身份变更 ⇒ sliceId 变更（候选面缓存键要素）。
    VariableBinding discrete;
    discrete.bindingId = "mdl.drivetrain.motor-key[1]";
    discrete.kind = VariableKind::DiscreteDevice;
    discrete.step = 0.0;
    discrete.authorized = true;
    discrete.locked = false;
    optimization::PatchItem item;
    item.bindingId = discrete.bindingId;
    item.discreteRef = "sel-combo-aa11";
    const auto patch2 = optimization::makeCandidatePatch(
        {discrete}, OptimizationStage::StageD, {item}, "p2");
    auto configWithVar = config;
    configWithVar.variables = {discrete};
    const auto c = buildJointFacadeSlice(snapshot, configWithVar, patch2, refs);
    EXPECT_NE(c.sliceId.bytes, a.sliceId.bytes);

    // config 变更（种子）⇒ sliceId 变更（config.opt canonical 进切片）。
    const auto d = buildJointFacadeSlice(snapshot, makeStageDConfig(9), patch, refs);
    EXPECT_NE(d.sliceId.bytes, a.sliceId.bytes);

    // 结构违约（调用方契约——fail-fast 轨）。
    EXPECT_THROW(buildJointFacadeSlice(snapshot, config, patch, {}),
                 std::invalid_argument);
    auto dup = refs;
    dup[1] = dup[0];  // token 重复
    EXPECT_THROW(buildJointFacadeSlice(snapshot, config, patch, dup),
                 std::invalid_argument);
    auto wrongKey = refs;
    wrongKey[0].upstreamKey = "dt-mapping";  // 键键配对违约（trj 槽挂 dt 键）
    EXPECT_THROW(buildJointFacadeSlice(snapshot, config, patch, wrongKey),
                 std::invalid_argument);
    auto zeroSlice = refs;
    zeroSlice[2].upstreamSliceId = core::ContentIdentity{};  // 保留值身份
    EXPECT_THROW(buildJointFacadeSlice(snapshot, config, patch, zeroSlice),
                 std::invalid_argument);
}

// =====================================================================
// 内层 Quick→Verified→器件匹配反馈闭环（acceptance 1 主用例）
// =====================================================================

TEST(OptJointStrategy, InnerLoopQuickVerifiedThenDeviceMatching_WP21T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-05", "OPT-03"},
                  std::vector<std::string>{"AT-35"});

    JointFixture f;
    JointLayeredChecker checker(OptimizationStage::StageD, f.deps());
    const auto config = makeStageDConfig();
    auto input = f.readyInput(config);
    const auto result = checker.check(input, f.ctx);

    // 派发序＝§8.5 内层结构图固定序：轨迹 Quick→轨迹 Verified→动力学
    // Quick→动力学 Verified→dt 映射→selection 器件匹配（两级是两次独立
    // 派发——Quick 结果绝不复用为 Verified，D-13）。
    ASSERT_EQ(f.dispatchLog.size(), 6U);
    EXPECT_EQ(f.dispatchLog[0].evaluationKey, "trj-sequence-plan");
    EXPECT_EQ(f.dispatchLog[0].mode, core::EvaluationMode::Quick);
    EXPECT_EQ(f.dispatchLog[1].evaluationKey, "trj-sequence-plan");
    EXPECT_EQ(f.dispatchLog[1].mode, core::EvaluationMode::Verified);
    EXPECT_EQ(f.dispatchLog[2].evaluationKey, "dyn-rnea-analysis");
    EXPECT_EQ(f.dispatchLog[2].mode, core::EvaluationMode::Quick);
    EXPECT_EQ(f.dispatchLog[3].evaluationKey, "dyn-rnea-analysis");
    EXPECT_EQ(f.dispatchLog[3].mode, core::EvaluationMode::Verified);
    EXPECT_EQ(f.dispatchLog[4].evaluationKey, "dt-mapping");
    EXPECT_EQ(f.dispatchLog[4].mode, core::EvaluationMode::Verified);
    EXPECT_EQ(f.dispatchLog[5].evaluationKey, "sel-combination-check");
    EXPECT_EQ(f.dispatchLog[5].mode, core::EvaluationMode::Verified);

    // 六层全 Completed＋层序与管线序一致＋四条槽位层上游引用齐备。
    ASSERT_EQ(result.layers.size(), 6U);
    EXPECT_TRUE(result.allLayersCompleted);
    EXPECT_FALSE(result.canceled);
    static constexpr std::string_view expectedTokens[6]
        = {optimization::kJointLayerTrjQuick, optimization::kJointLayerTrjVerified,
           optimization::kJointLayerDynQuick, optimization::kJointLayerDynVerified,
           optimization::kJointLayerDtMapping,
           optimization::kJointLayerSelCombination};
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(result.layers[i].outcome, JointLayerOutcome::Completed);
        EXPECT_EQ(result.layers[i].layerToken, expectedTokens[i]);
    }
    std::vector<JointUpstreamRef> refsFromLayers;
    for (const auto& layer : result.layers) {
        if (layer.upstreamRef.has_value()) {
            refsFromLayers.push_back(*layer.upstreamRef);
        }
    }
    ASSERT_EQ(refsFromLayers.size(), 4U);
    EXPECT_EQ(refsFromLayers[0].dependencyToken, "upstream.trj-sequence-plan");
    EXPECT_EQ(refsFromLayers[1].dependencyToken, "upstream.dyn-rnea-analysis");
    EXPECT_EQ(refsFromLayers[2].dependencyToken, "upstream.dt.mapping");
    EXPECT_EQ(refsFromLayers[3].dependencyToken, "upstream.sel.combination-check");
    EXPECT_EQ(refsFromLayers[0].upstreamKey, "trj-sequence-plan");
    EXPECT_EQ(refsFromLayers[3].upstreamKey, "sel-combination-check");
    // 上游 sliceId＝各层评估请求切片身份（层间绑定的事实面——Verified
    // 层的上游引用即其请求切片身份）。
    EXPECT_EQ(refsFromLayers[0].upstreamSliceId.bytes,
              result.layers[1].upstreamRef->upstreamSliceId.bytes);

    // 门面切片身份＝以层记录上游引用复算的 buildJointFacadeSlice 身份
    //（身份面复算一致——门面缓存身份的机械可验证性）。
    const auto recomputed
        = buildJointFacadeSlice(input.snapshot, config, input.patch, refsFromLayers);
    EXPECT_EQ(result.facadeSliceId.bytes, recomputed.sliceId.bytes);

    // 判定面：无违例＋覆盖完备 ⇒ Feasible＋正式通过资格（五条件全过——
    // mode=Verified/outcome=Completed/覆盖完备/Profile 必需项齐/状态可行）。
    EXPECT_EQ(result.record.status, CandidateStatus::Feasible);
    EXPECT_EQ(result.record.engineeringStatus, core::EngineeringStatus::Feasible);
    EXPECT_TRUE(result.record.formalPassEligible);
    EXPECT_TRUE(result.record.rejections.empty());
}

TEST(OptJointStrategy, SelectionRejectionsFeedBackJointInfeasible_WP21T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03", "OPT-05"},
                  std::vector<std::string>{"AT-35"});

    JointFixture f;
    // 器件匹配层逐项淘汰（Must 违例×2——实际值/阈值来源随 detail 透传）＋
    // 轨迹 Verified 层一条违例（多域联合汇总面）。
    auto& sel = f.script(optimization::kInnerSelCombinationKey);
    evidence::DomainVerdictViolation v1;
    v1.itemId = "sel.motor-torque-headroom";
    v1.detail = "电机 1 额定转矩 2.0 N·m < 工作点 2.4 N·m（thresholdSource=目录列 "
                "rated-torque；P-POL-3 未裁决维度按域语义透传）";
    evidence::DomainVerdictViolation v2;
    v2.itemId = "sel.gearbox-ratio-out";
    v2.detail = "减速比 25 不在目录覆盖区间 [10,20]（thresholdSource=目录列 ratio-range）";
    sel.mustScript.mustViolations = {v1, v2};
    auto& trj = f.script(optimization::kInnerTrjPlanKey);
    evidence::DomainVerdictViolation v3;
    v3.itemId = "trj.deadline-exceeded";
    v3.detail = "节拍 12.5 s 超上限 10.0 s（thresholdSource=必验工况 deadline）";
    trj.mustScript.mustViolations = {v3};

    JointLayeredChecker checker(OptimizationStage::StageD, f.deps());
    auto input = f.readyInput(makeStageDConfig());
    const auto result = checker.check(input, f.ctx);

    // 各域 Must 违例 ⇒ Infeasible（§6.3 联合硬约束汇总语义）；六层仍全
    // 执行（违例是判定素材不是执行失败——层 Completed）。
    EXPECT_TRUE(result.allLayersCompleted);
    EXPECT_EQ(f.dispatchLog.size(), 6U);
    EXPECT_EQ(result.record.status, CandidateStatus::Infeasible);
    EXPECT_EQ(result.record.engineeringStatus,
              core::EngineeringStatus::EngineeringInfeasible);

    // 逐层逐约束淘汰原因（O10——sourceId＝违例项 id，detail 透传文案；
    // 轨迹域与选型域分账齐备、互不覆盖。轨迹层脚本对 Quick/Verified 两次
    // 派发同源生效——两级各记一条〔层事实不合并改写，§6.3 域隔离〕；
    // 选型层仅 Verified 一次派发——两项违例各一条。总数＝轨迹 2＋选型 2
    // ＝4，发生模式按层区分）。
    ASSERT_EQ(result.record.rejections.size(), 4U);
    std::vector<std::string> sources;
    std::vector<core::EvaluationMode> rejectionModes;
    for (const auto& r : result.record.rejections) {
        sources.push_back(r.sourceId);
        rejectionModes.push_back(r.mode);
    }
    EXPECT_EQ(std::count(sources.begin(), sources.end(), "trj.deadline-exceeded"),
              2L);  // 轨迹层 Quick＋Verified 两派发各记一条
    EXPECT_EQ(std::count(sources.begin(), sources.end(), "sel.motor-torque-headroom"),
              1L);  // 选型层仅 Verified 一次派发
    EXPECT_EQ(std::count(sources.begin(), sources.end(), "sel.gearbox-ratio-out"),
              1L);
    EXPECT_FALSE(result.record.formalPassEligible);  // 不可行 ⇒ 无正式通过资格
    // 器件匹配逐项淘汰原因细节（thresholdSource 等域内字段随 detail 保留
    //——不整体阻断的域语义透传不丢失）。
    bool detailKept = false;
    for (const auto& r : result.record.rejections) {
        if (r.sourceId == "sel.motor-torque-headroom"
            && r.subject.find("thresholdSource") != std::string::npos) {
            detailKept = true;
        }
    }
    EXPECT_TRUE(detailKept);
}

TEST(OptJointStrategy, LayerFailureStaysEvaluationFailed_WP21T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"},
                  std::vector<std::string>{"AT-35"});

    JointFixture f;
    // 动力学 Quick 层注入评估器失败（环境轨——EvidenceError）。
    f.script(optimization::kInnerDynRneaKey).failScript = true;

    JointLayeredChecker checker(OptimizationStage::StageD, f.deps());
    auto input = f.readyInput(makeStageDConfig());
    const auto result = checker.check(input, f.ctx);

    // 评估器失败 ≠ 约束失败：候选 EvaluationFailed（不是 Infeasible）。
    EXPECT_EQ(result.record.status, CandidateStatus::EvaluationFailed);
    EXPECT_FALSE(result.allLayersCompleted);
    ASSERT_EQ(result.record.rejections.size(), 1U);
    EXPECT_EQ(result.record.rejections.front().reasonToken,
              std::string(optimization::kRejectJointLayerFailed));
    EXPECT_NE(result.record.rejections.front().sourceId.find("joint-layer.dyn-quick"),
              std::string::npos);

    // 下游层停止派发（动力学 Quick 之后的 Verified/dt/sel 无上游事实——
    // 线性序截断）：派发序止于动力学 Quick（第 3 次）。
    ASSERT_EQ(f.dispatchLog.size(), 3U);
    ASSERT_EQ(result.layers.size(), 3U);
    EXPECT_EQ(result.layers.back().outcome, JointLayerOutcome::LayerFailed);
}

TEST(OptJointStrategy, SearchExhaustedStaysDataInsufficient_WP21T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03", "C5", "C8"},
                  std::vector<std::string>{"AT-35"});

    JointFixture f;
    // 轨迹层注入搜索未果（路径搜索未找到有效解——C5/C8 口径；脚本对
    // Quick/Verified 两派发同源生效——第一次派发〔trj Quick〕即截断）。
    auto& trj = f.script(optimization::kInnerTrjPlanKey);
    evidence::SearchExhaustedRecord exhausted;
    exhausted.searchBudgetUsed = 128;   // 已用搜索预算（脚本值）
    exhausted.initialGuessesTried = 3;  // 已试初值数（脚本值）
    trj.searchScript = exhausted;

    JointLayeredChecker checker(OptimizationStage::StageD, f.deps());
    auto input = f.readyInput(makeStageDConfig());
    const auto result = checker.check(input, f.ctx);

    // 搜索未果 ≠ 确定性无解：候选 DataInsufficient（附搜索记录——不产出
    // 不可行、无淘汰原因）。
    EXPECT_EQ(result.record.status, CandidateStatus::DataInsufficient);
    EXPECT_TRUE(result.record.rejections.empty());
    EXPECT_FALSE(result.allLayersCompleted);
    ASSERT_EQ(result.layers.size(), 1U);
    EXPECT_EQ(result.layers.front().outcome, JointLayerOutcome::SearchExhausted);
    EXPECT_TRUE(result.layers.front().searchRecord.has_value());
    // 下游层停止派发（trj Verified/dyn/dt/sel 无上游事实）。
    EXPECT_EQ(f.dispatchLog.size(), 1U);
}

TEST(OptJointStrategy, CancellationBetweenLayersPreservesPrefix_WP21T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-01", "OPT-05"},
                  std::vector<std::string>{"AT-35"});

    JointFixture f;
    CancelAfterContext cancelCtx(5U);  // 放行 5 次查询——第 6 层前观测取消
    JointLayeredChecker checker(OptimizationStage::StageD, f.deps());
    auto input = f.readyInput(makeStageDConfig());
    const auto result = checker.check(input, cancelCtx);

    // 协作取消：已执行层前缀保留（5 层）＋canceled 标记＋候选状态不动
    //（Pending——取消不是任何一种工程判定，§7.5/TASK-02）。
    EXPECT_TRUE(result.canceled);
    EXPECT_FALSE(result.allLayersCompleted);
    ASSERT_EQ(result.layers.size(), 5U);
    EXPECT_EQ(f.dispatchLog.size(), 5U);
    EXPECT_EQ(result.record.status, CandidateStatus::Pending);
}

// =====================================================================
// 装配不一致 fail-fast 面（§5.7——Preflight 同源码面）
// =====================================================================

TEST(OptJointStrategy, MissingInnerEvaluatorFailsFast_WP21T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"},
                  std::vector<std::string>{"AT-35"});

    // 撤销 selection 评估器注册（装配漂移面——§5.7 Preflight #7 同源）。
    JointFixture f(/*registerSelection=*/false);
    JointLayeredChecker checker(OptimizationStage::StageD, f.deps());
    auto input = f.readyInput(makeStageDConfig());
    bool thrown = false;
    try {
        const auto r = checker.check(input, f.ctx);
        (void)r;
    } catch (const OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptEvaluatorMissing));
    }
    EXPECT_TRUE(thrown);
    // 截断点＝sel 层（第 6 次派发尝试）——前五层已执行。
    EXPECT_EQ(f.dispatchLog.size(), 5U);
}

TEST(OptJointStrategy, StageBRequestRejectedAtStart_WP21T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"},
                  std::vector<std::string>{"AT-35"});

    // StageB 请求联合闭环＝运行启动阻塞（§5.7 阶段锁——不是候选淘汰）。
    JointFixture f;
    JointLayeredChecker checker(OptimizationStage::StageB, f.deps());
    auto input = f.readyInput(makeStageDConfig());
    bool thrown = false;
    try {
        const auto r = checker.check(input, f.ctx);
        (void)r;
    } catch (const OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptStageLocked));
    }
    EXPECT_TRUE(thrown);
    EXPECT_TRUE(f.dispatchLog.empty());  // 启动阻塞——零层派发
}

// =====================================================================
// StageD 组合空间采样（§8.5 外层探索——OPT-VER-202 面）
// =====================================================================

TEST(OptJointStrategy, StageDGeneratorDeterministicWithCombos_WP21T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-05", "OPT-02"},
                  std::vector<std::string>{"AT-35"});

    // StageD 词表启用绑定：连续维＝mdl.drivetrain.ratio[j]（§5.3 #9——
    // V12-02，StageD 启用的唯一连续族）；离散维＝motor-key/reducer-key
    // （§5.6——R2 离散器件变量。§5.3 其余连续/枚举族 enabledInStageD=
    // false——StageD 补丁构造即阶段锁拒绝，测试如实按启用词表构造）。
    VariableBinding ratio;
    ratio.bindingId = "mdl.drivetrain.ratio[1]";
    ratio.kind = VariableKind::Continuous;
    ratio.unit = core::UnitToken::find("1").value();
    ratio.lowerBound = 2.0;    // 传动比下界（无量纲，>0——I-MDL-11）
    ratio.upperBound = 20.0;   // 传动比上界（无量纲）
    ratio.defaultValue = 8.0;  // 无量纲
    ratio.authorized = true;
    ratio.locked = false;
    VariableBinding motorKey;
    motorKey.bindingId = "mdl.drivetrain.motor-key[1]";
    motorKey.kind = VariableKind::DiscreteDevice;
    motorKey.step = 0.0;
    motorKey.authorized = true;
    motorKey.locked = false;
    VariableBinding reducerKey;
    reducerKey.bindingId = "mdl.drivetrain.reducer-key[1]";
    reducerKey.kind = VariableKind::DiscreteDevice;
    reducerKey.step = 0.0;
    reducerKey.authorized = true;
    reducerKey.locked = false;

    // 组合空间：3 条组合（每条完整赋值两离散绑定——组合空间的规范成员）。
    std::vector<StageDCombination> combos;
    for (const char* motorRef : {"motor-cc", "motor-aa", "motor-bb"}) {
        StageDCombination combo;
        combo.bindings = {{motorKey.bindingId, motorRef},
                          {reducerKey.bindingId, "gearbox-x"}};
        combos.push_back(std::move(combo));
    }

    auto config = makeStageDConfig();
    config.variables = {ratio, motorKey, reducerKey};

    // 确定性：同 config＋同组合集 ⇒ 同批（身份等价——NFR-COR-02）。
    const auto batch1 = generateStageDJointCandidates(config, combos);
    const auto batch2 = generateStageDJointCandidates(config, combos);
    ASSERT_EQ(batch1.size(), batch2.size());
    ASSERT_EQ(batch1.size(), 4U);  // 基线＋maxCandidates−1
    for (std::size_t i = 0; i < batch1.size(); ++i) {
        EXPECT_EQ(optimization::patchIdentity(batch1[i]).bytes,
                  optimization::patchIdentity(batch2[i]).bytes)
            << "候选 " << i << " 两跑身份不等（确定性违约）";
    }

    // 基线候选恒为批首（空补丁——§8.2）。
    EXPECT_TRUE(batch1.front().items.empty());

    // 组合空间轮转：3 组合 × 3 采样候选——每组合（按电机引用区分）至少
    // 出现一次（轮转覆盖；离散项引用即声明组合引用——组合合法性归 sel
    // 层，N5）。
    std::vector<std::string> seenRefs;
    for (std::size_t i = 1; i < batch1.size(); ++i) {
        for (const auto& it : batch1[i].items) {
            if (it.bindingId == motorKey.bindingId) {
                seenRefs.push_back(it.discreteRef);
            }
        }
    }
    EXPECT_NE(std::find(seenRefs.begin(), seenRefs.end(), "motor-aa"),
              seenRefs.end());
    EXPECT_NE(std::find(seenRefs.begin(), seenRefs.end(), "motor-bb"),
              seenRefs.end());
    EXPECT_NE(std::find(seenRefs.begin(), seenRefs.end(), "motor-cc"),
              seenRefs.end());

    // 种子敏感：不同种子 ⇒ 不同候选集（身份集互异——采样面有效）。
    auto other = config;
    other.seed = 99;
    const auto batch3 = generateStageDJointCandidates(other, combos);
    bool differs = false;
    for (std::size_t i = 1; i < batch1.size(); ++i) {
        if (optimization::patchIdentity(batch1[i]).bytes
            != optimization::patchIdentity(batch3[i]).bytes) {
            differs = true;
        }
    }
    EXPECT_TRUE(differs);

    // 阶段锁：StageB 请求 R2 采样面即拒（OPT-STAGE-LOCKED）。
    auto stageB = config;
    stageB.stage = OptimizationStage::StageB;
    bool thrown = false;
    try {
        (void)generateStageDJointCandidates(stageB, combos);
    } catch (const OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptStageLocked));
    }
    EXPECT_TRUE(thrown);

    // 结构违约：组合引用未登记绑定 / 类别非离散 / 锁定绑定 / 空引用 /
    // 组合内 bindingId 重复——全部 OPT-INPUT-INVALID（fail-fast）。
    auto expectInvalid = [&](std::vector<StageDCombination> bad) {
        try {
            (void)generateStageDJointCandidates(config, bad);
            return false;
        } catch (const OptimizationError& e) {
            return e.stableCode() == std::string(optimization::kOptInputInvalid);
        }
    };
    StageDCombination unknown;
    unknown.bindings = {{"mdl.drivetrain.motor-key[9]", "ref-x"}};
    EXPECT_TRUE(expectInvalid({unknown}));
    StageDCombination notDiscrete;
    notDiscrete.bindings = {{ratio.bindingId, "ref-x"}};  // 连续绑定进组合＝类别违约
    EXPECT_TRUE(expectInvalid({notDiscrete}));
    auto locked = motorKey;
    locked.locked = true;
    locked.authorized = false;  // 未授权必须锁定（配对纪律）——组合面同拦
    auto configLocked = config;
    configLocked.variables = {ratio, locked, reducerKey};
    StageDCombination lockedCombo;
    lockedCombo.bindings = {{locked.bindingId, "ref-x"}};
    try {
        (void)generateStageDJointCandidates(configLocked, {lockedCombo});
        ADD_FAILURE() << "锁定绑定组合未被拒绝";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
    }
    StageDCombination emptyRef;
    emptyRef.bindings = {{motorKey.bindingId, ""}};
    EXPECT_TRUE(expectInvalid({emptyRef}));
    StageDCombination dupBinding;
    dupBinding.bindings
        = {{motorKey.bindingId, "ref-a"}, {motorKey.bindingId, "ref-b"}};
    EXPECT_TRUE(expectInvalid({dupBinding}));

    // 空组合集＝离散维缺席（退化连续采样批——如实不伪造多样性）。
    const auto degenerate = generateStageDJointCandidates(config, {});
    ASSERT_EQ(degenerate.size(), 4U);
    for (std::size_t i = 1; i < degenerate.size(); ++i) {
        for (const auto& it : degenerate[i].items) {
            EXPECT_NE(it.bindingId, motorKey.bindingId);  // 无离散项
            EXPECT_NE(it.bindingId, reducerKey.bindingId);
        }
    }
}
