/**
 * @file   ConstraintTest.cpp
 * @brief  静态硬约束编排模型测试（OptConstraint 组）——R1 约束词表/阶段锁
 *         解析/静态硬约束先行管线（§6.2 十步执行序）与候选分类判定流
 *         （§7.5，消费 evidence aggregateVerdict）——任务契约 WP-20-T04
 *         acceptance 1~4（OPT-VER-109~119/121/122 观测点＋P-EV-7/C5/C8
 *         保守语义钉扎）。
 *
 * 设计依据：
 *   - units/optimization.md §6.2（R1 执行序——硬约束先行，失败的候选不进
 *     可行集）、§6.5（阶段锁＝运行启动阻塞非候选淘汰）、§7.5（判定流与
 *     区分表——Infeasible/DataInsufficient/EvaluationFailed 三态不冒充）、
 *     §8.3 第 7/9 步（约束事实/淘汰原因记录面）、§13.1（OPT-VER-109~122
 *     用例矩阵）、§6.7（③端口内部消费链）
 *   - 需求 OPT-03/EVI-01/EVI-02、§8.1 C5/C8（搜索未果/构型碰撞不判任务
 *     级不可行）、AT-09（不可行候选不进可行集＋漏验不出正式通过反例）
 *   - 用例名与断言注释带需求/AT 追溯（AGENTS §2.7）；数值断言给解析期望
 *
 * 测试形态（模型测试＝直调计算库，NFR-MNT-01）：③端口评估器以脚本化
 * 替身工厂注册进**真 evidence::EvaluatorRegistry**（完整注册期校验——
 * Profile 先行＋descriptor 校验，消费路径与生产一致）；限位/碰撞/编译
 * 以可控替身注入（O-37 裁决同款注入口；生产适配器＝policy/runtime 唯一
 * 实现薄投影，随 P-OPT-2 落位）。管线经 deps 接口指针调用替身——接口
 * 路径（虚函数）被天然钉住（WP-20-T03 B-1 教训：不留只测自由函数的盲区）。
 */

#include <sdurws/ird/optimization/Constraint.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::CandidateStatus;
using optimization::ConstraintFact;
using optimization::ConstraintId;
using optimization::ConstraintSpec;
using optimization::ConstraintVerdict;
using optimization::IOptimizationConstraintProvider;
using optimization::OptimizationConstraintProvider;
using optimization::OptimizationError;
using optimization::OptimizationStage;
using optimization::PatchItem;
using optimization::StaticHardConstraintDeps;
using optimization::StaticHardConstraintInput;
using optimization::StaticHardConstraintPipeline;
using optimization::VariableBinding;
using optimization::VariableKind;

namespace {

// =====================================================================
// 通用替身与构造辅助（模型测试自持——不依赖 testkit 替身库）
// =====================================================================

/// 修订闭包来源替身（快照组装协议的测试承载——全部 (oid,cv) 声明在册；
/// 测试对象不做混入反例，那归 evidence EV-T03 自身测试）。
class AllPresentClosure final : public evidence::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 评估调用上下文替身（零取消/零进度/零对象读取——③端口调用约定的最小
/// 实现；取消语义的协作查询归 kin 评估器自身测试）。
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

/// 非零内容身份（测试构造——避免保留值；填字节法与 T03 契约测试同款）。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// 构造冻结快照（SnapshotBuilder 唯一合法生产者——requiredCaseSetId 与
/// snapshotId 均为 builder 计算值；用例覆盖矩阵对账依赖计算值而非手填）。
evidence::AnalysisSnapshot makeSnapshot(const std::vector<evidence::CaseEntry>& cases)
{
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    // 对象闭包：基线 RobotDesign 一条（builder 要求 ≥1——CON-01 闭包完整性；
    // digest 与 contentVersion 同源一致——I-2 一致性闸门，两者同为对象
    // 载荷摘要字节）。
    evidence::ObjectRefEntry design;
    design.objectId = core::ObjectId::generate();
    design.contentVersion = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000a1");
    design.objectTypeToken = "robot-design";
    design.digest = design.contentVersion.bytes;
    builder.addObjectRef(design);
    // CON-06：策略与名称映射内容身份非空（builder 冻结校验项）。
    evidence::PolicyRef policy;
    policy.policyContentIdentity = makeContentIdentity(4);
    builder.setPolicyRef(policy);
    evidence::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = makeContentIdentity(5);
    builder.setNameMapRef(nameMap);
    for (const auto& c : cases) {
        builder.addCase(c);
    }
    // 复现块完整性（NFR-COR-02 复现凭据——builder 必填校验项）。
    evidence::ReproductionBlock repro;
    repro.productVersion = "test";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    return builder.build(AllPresentClosure{});
}
/// 快照全部工况 id（覆盖完备构造用——漏验反例用例保持空清单，勿混用）。
std::vector<evidence::CaseId> makeAllCaseIds(const evidence::AnalysisSnapshot& snapshot)
{
    std::vector<evidence::CaseId> ids;
    ids.reserve(snapshot.caseSet.entries.size());
    for (const auto& entry : snapshot.caseSet.entries) {
        ids.push_back(entry.caseId);
    }
    return ids;
}
evidence::CaseCoverageMatrix
makeCoverage(const evidence::AnalysisSnapshot& snapshot,
             const std::vector<evidence::CaseId>& executedCaseIds)
{
    evidence::CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const auto& entry : snapshot.caseSet.entries) {
        evidence::CaseCoverageEntry row;
        row.caseId = entry.caseId;
        const bool executed = std::find(executedCaseIds.begin(), executedCaseIds.end(),
                                        entry.caseId)
                              != executedCaseIds.end();
        if (executed) {
            row.status = evidence::CaseExecutionStatus::Executed;
            row.runId = core::RunId::generate();            // 追溯面非零（EntryRunRefInvalid 防线）
            row.resultSliceId = makeContentIdentity(6);      // 同上
        } else {
            row.status = evidence::CaseExecutionStatus::NotExecuted;
        }
        matrix.entries.push_back(std::move(row));
    }
    return matrix;
}

/// 手填合法输入切片（管线不校验切片身份——冻结契约由调用方保证；清单
/// 绑定校验只查非零保留值，故身份面手填非零计算值——SliceBuilder 冻结
/// 协议本身归 evidence EV-T02 自身测试，不在此重复）。
evidence::InputSlice makeSlice()
{
    evidence::InputSlice slice;
    slice.evaluationKey = std::string(optimization::kKinTaskPointsBatchKey);
    slice.evaluatorContractVersion = 1U;
    slice.snapshotId = makeContentIdentity(7);
    slice.sliceId = makeContentIdentity(8);
    slice.inputBaselineId = makeContentIdentity(9);
    return slice;
}

// ---- ③端口脚本化评估器替身（注册进真注册表——消费路径与生产一致）----

/// 场景脚本：替身评估器 evaluate() 的返回输出（按键区分脚本）。
struct KinScript {
    evidence::EvaluationOutput output;
};

/// 脚本化 kin 评估器（descriptor 与 evaluate 均按装配脚本回应——III 端口
/// 调用约定的真实执行面，管线 find→create→evaluate 全链消费）。
class ScriptedKinEvaluator final : public evidence::IEngineeringEvaluator {
public:
    ScriptedKinEvaluator(evidence::EvaluatorDescriptor descriptor, KinScript script)
        : m_descriptor(std::move(descriptor)), m_script(std::move(script))
    {
    }

    const evidence::EvaluatorDescriptor& descriptor() const override
    {
        return m_descriptor;
    }

    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override
    {
        // 同（请求切片，环境）→等价输出（确定性替身——NFR-COR-02 消费面）。
        return m_script.output;
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
    KinScript m_script;
};

/// 脚本化评估器工厂（真注册表注册面——descriptor() 由注册期校验消费）。
class ScriptedKinFactory final : public evidence::IEvaluatorFactory {
public:
    ScriptedKinFactory(evidence::EvaluatorDescriptor descriptor, KinScript script)
        : m_descriptor(std::move(descriptor)), m_script(std::move(script))
    {
    }

    const evidence::EvaluatorDescriptor& descriptor() const override
    {
        return m_descriptor;
    }

    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<ScriptedKinEvaluator>(m_descriptor, m_script);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
    KinScript m_script;
};

/// 组装合法 kin 评估器描述符（Profile 先行注册——§13 接入顺序；契约版本
/// 1；依赖声明空集＝闭包平凡闭合）。
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

/// 注册一个 kin 替身评估器（Profile "kin" v1.0 必须已注册——接入顺序；
/// Profile 注册由 PipelineFixture 构造期完成，本函数只挂评估器工厂）。
void registerKinScript(evidence::EvaluatorRegistry& registry, const std::string& key,
                       KinScript script)
{
    registry.registerEvaluator(
        std::make_unique<ScriptedKinFactory>(makeKinDescriptor(key), std::move(script)),
        {});
}

/// 注册 "kin" v1.0 Profile（空必需项集——完备性核对平凡通过，不干扰
/// 判定流断言；Profile 明细权威在需求表 4，测试不复制明细）。
void registerKinProfile(evidence::EvidenceProfileRegistry& profiles)
{
    evidence::RequiredEvidenceProfile profile;
    profile.profileId = "kin";
    profile.version = "1.0";
    profiles.registerProfile(profile);
}

// ---- 探针/编译替身（管线 deps 接口指针消费——虚函数路径天然钉住）----

/// 限位探针替身：返回预设结果并记录调用次数（I-C6 短路验证用；方法 const
/// ＝接口纯计算契约，计数用 mutable 承载）。
class FakeJointLimitProbe final : public optimization::IJointLimitProbe {
public:
    optimization::JointLimitProbeResult result;
    mutable int calls = 0;

    optimization::JointLimitProbeResult
    probeJointLimits(const optimization::JointLimitProbeRequest&) const override
    {
        ++calls;
        return result;
    }
};

/// 碰撞探针替身：记录查询构型（SampleSet 形态传递验证）并返回预设结果。
class FakeCollisionProbe final : public optimization::IStaticCollisionProbe {
public:
    optimization::StaticCollisionProbeResult result;
    mutable std::vector<std::vector<double>> queriedConfigurations;
    mutable int calls = 0;

    optimization::StaticCollisionProbeResult probeSampleSet(
        const std::vector<std::vector<double>>& configurations) const override
    {
        ++calls;
        queriedConfigurations = configurations;
        return result;
    }
};

/// 编译替身：可脚本成功/失败（失败携带 RT-* 透传码）并记录调用次数。
class FakeCompiler final : public optimization::ICandidateCompiler {
public:
    optimization::CandidateCompileResult result;  // 默认 ok=true
    mutable int calls = 0;

    FakeCompiler()
    {
        result.ok = true;
    }

    optimization::CandidateCompileResult
    compile(const CandidatePatch&, const std::vector<VariableBinding>&) const override
    {
        ++calls;
        return result;
    }
};

// ---- 管线输入装配（合法正例基线——各用例按场景覆写局部）----

/// 两工况（均 enabled+mandatory——覆盖矩阵分母）。
std::vector<evidence::CaseEntry> makeTwoMandatoryCases()
{
    return {{core::ObjectId::generate(), "额定工况", true, true},
            {core::ObjectId::generate(), "极限工况", true, true}};
}

/// 合法正例输入（全约束满足基线：空补丁基线候选＋覆盖完备＋无违例脚本）。
StaticHardConstraintInput makeBaselineInput(const evidence::AnalysisSnapshot& snapshot,
                                            const evidence::InputSlice& slice,
                                            const evidence::CaseCoverageMatrix& coverage)
{
    StaticHardConstraintInput input;
    const CandidatePatch emptyPatch;  // 空补丁＝基线候选（§5.5 ③——有确定身份）
    input.candidateId = optimization::candidateIdOf(
        core::ObjectId::generate(),
        core::ContentVersion::fromCanonical(
            "cv-00000000000000000000000000000000000000000000000000000000000000bb"),
        emptyPatch);
    input.isBaseline = true;
    input.patch = emptyPatch;
    input.mode = core::EvaluationMode::Verified;
    input.baselineChainInEnabledScope = true;
    // 限位查询：单关节单构型（区间合法——policy 发现投影的合法基线）。
    optimization::JointLimitSpecRecord joint;
    joint.jointSubject = core::ObjectId::generate().toCanonical();
    joint.qmin = -3.14;   // rad（转动关节下限）
    joint.qmax = 3.14;    // rad（转动关节上限）
    input.jointLimits.joints = {joint};
    input.jointLimits.configurations = {{0.0}};  // 一个构型，关节角 0 rad
    input.snapshot = snapshot;
    input.slice = slice;
    input.task = {core::ProjectId::generate(), core::BranchId::generate(),
                  core::RevisionId::generate(), core::RunId::generate(),
                  core::AttemptId{1U}};
    input.coverage = coverage;
    // manifest：与真注册表的三元组绑定（用例内按注册权威值回填 contentIdentity）。
    input.manifest.profileId = "kin";
    input.manifest.profileVersion = "1.0";
    input.manifest.snapshotId = snapshot.snapshotId;
    input.manifest.sliceId = slice.sliceId;
    return input;
}

/// 装配完整依赖面（真注册表＋替身探针——deps 指针即接口指针）。
struct PipelineFixture {
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    FakeJointLimitProbe jointProbe;
    FakeCollisionProbe collisionProbe;
    FakeCompiler compiler;
    NopEvaluationContext ctx;

    PipelineFixture()
    {
        registerKinProfile(profiles);
        // 限位探针合法基线（评估完成、无发现——各用例按场景覆写）；
        // 碰撞探针默认缺席语义由 deps(withCollision) 表达，结果默认值即
        // "评估完成、无发现"的正例基线。
        jointProbe.result.completed = true;
        collisionProbe.result.completed = true;
        // manifest 三元组的权威 contentIdentity 在此回填（注册表计算值）。
    }

    StaticHardConstraintDeps deps(bool withCollision = true)
    {
        StaticHardConstraintDeps d;
        d.evaluators = &evaluators;
        d.jointLimitProbe = &jointProbe;
        d.collisionProbe = withCollision ? &collisionProbe : nullptr;
        d.compiler = &compiler;
        d.producers = &evaluators;   // EvaluatorRegistry 实现 IProducerRegistryView
        d.profiles = &profiles;      // EvidenceProfileRegistry 实现 IProfileRegistryView
        return d;
    }
};

}  // namespace

// =====================================================================
// 词表钉扎（token 冻结——消费面漂移防线）
// =====================================================================

TEST(OptConstraint, ConstraintTokensFollowRegisteredVocabulary_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{"AT-09"});

    // §6.2 十步约束 token 逐一钉扎（枚举序＝执行序；token 文本冻结）。
    EXPECT_EQ(optimization::toToken(ConstraintId::InputPrecondition),
              "opt.constraint.input-precondition");
    EXPECT_EQ(optimization::toToken(ConstraintId::StageCapability),
              "opt.constraint.stage-capability");
    EXPECT_EQ(optimization::toToken(ConstraintId::VariableLock),
              "opt.constraint.variable-lock");
    EXPECT_EQ(optimization::toToken(ConstraintId::PatchValidity),
              "opt.constraint.patch-validity");
    EXPECT_EQ(optimization::toToken(ConstraintId::CandidateCompile),
              "opt.constraint.candidate-compile");
    EXPECT_EQ(optimization::toToken(ConstraintId::Topology),
              "opt.constraint.topology");
    EXPECT_EQ(optimization::toToken(ConstraintId::JointLimit),
              "opt.constraint.joint-limit");
    EXPECT_EQ(optimization::toToken(ConstraintId::MustStationReachability),
              "opt.constraint.must-station-reachability");
    EXPECT_EQ(optimization::toToken(ConstraintId::MustRegionCoverage),
              "opt.constraint.must-region-coverage");
    EXPECT_EQ(optimization::toToken(ConstraintId::StaticCollision),
              "opt.constraint.static-collision");
    // 联合家族引用词表（只承载引用事实——非可执行约束）。
    EXPECT_EQ(optimization::toToken(optimization::JointConstraintFamily::Trajectory),
              "opt.constraint.trajectory");
    EXPECT_EQ(optimization::toToken(optimization::JointConstraintFamily::Dynamics),
              "opt.constraint.dynamics");
    EXPECT_EQ(optimization::toToken(
                  optimization::JointConstraintFamily::DrivetrainPerformance),
              "opt.constraint.drivetrain-performance");
    EXPECT_EQ(optimization::toToken(optimization::JointConstraintFamily::DeviceJoint),
              "opt.constraint.device-joint");
    // 判定四值 token。
    EXPECT_EQ(optimization::toToken(ConstraintVerdict::Satisfied), "satisfied");
    EXPECT_EQ(optimization::toToken(ConstraintVerdict::Violated), "violated");
    EXPECT_EQ(optimization::toToken(ConstraintVerdict::NotApplicable), "not-applicable");
    EXPECT_EQ(optimization::toToken(ConstraintVerdict::DataInsufficient),
              "data-insufficient");
    // 候选状态七值 token（Types.hpp T04 追加——词表冻结）。
    EXPECT_EQ(optimization::toToken(CandidateStatus::Pending), "pending");
    EXPECT_EQ(optimization::toToken(CandidateStatus::ScreenedOut), "screened-out");
    EXPECT_EQ(optimization::toToken(CandidateStatus::Infeasible), "infeasible");
    EXPECT_EQ(optimization::toToken(CandidateStatus::DataInsufficient),
              "data-insufficient");
    EXPECT_EQ(optimization::toToken(CandidateStatus::EvaluationFailed),
              "evaluation-failed");
    EXPECT_EQ(optimization::toToken(CandidateStatus::Feasible), "feasible");
    EXPECT_EQ(optimization::toToken(CandidateStatus::ParetoNondominated),
              "pareto-nondominated");
}

TEST(OptConstraint, StageBPlanFollowsPipelineOrder_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{"AT-09"});

    // §6.2 R1 全序内置清单（拓扑/输入前置→关节限位→Must 工位可达→Must
    // 区域覆盖→碰撞静态子集——顺序即执行序，acceptance 1 的管线次序锚）。
    const auto plan = optimization::stageBConstraintPlan();
    ASSERT_EQ(plan.size(), 10U);
    EXPECT_EQ(plan[0].constraintId, ConstraintId::InputPrecondition);
    EXPECT_EQ(plan[1].constraintId, ConstraintId::StageCapability);
    EXPECT_EQ(plan[2].constraintId, ConstraintId::VariableLock);
    EXPECT_EQ(plan[3].constraintId, ConstraintId::PatchValidity);
    EXPECT_EQ(plan[4].constraintId, ConstraintId::CandidateCompile);
    EXPECT_EQ(plan[5].constraintId, ConstraintId::Topology);
    EXPECT_EQ(plan[6].constraintId, ConstraintId::JointLimit);
    EXPECT_EQ(plan[7].constraintId, ConstraintId::MustStationReachability);
    EXPECT_EQ(plan[8].constraintId, ConstraintId::MustRegionCoverage);
    EXPECT_EQ(plan[9].constraintId, ConstraintId::StaticCollision);
    // 依据评估键（③端口消费项——kebab 注册键）与依据标识。
    EXPECT_EQ(plan[7].evaluationKey, std::string(optimization::kKinTaskPointsBatchKey));
    EXPECT_EQ(plan[8].evaluationKey, std::string(optimization::kKinRegionCoverageKey));
    EXPECT_EQ(plan[9].evaluationKey, "policy.collision-session");
}

// =====================================================================
// 阶段锁（acceptance 2——OPT-STAGE-LOCKED 不静默降级不呈现为候选淘汰）
// =====================================================================

TEST(OptConstraint, StageDConstraintRequestThrowsStageLocked_WP20T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // 接口路径（基类引用消费虚函数——WP-20-T03 B-1 教训钉扎）。
    const OptimizationConstraintProvider provider;
    const IOptimizationConstraintProvider& iface = provider;
    // StageD 联合约束清单未随本任务登记（WP-21-T02 前置）——请求即抛。
    bool threw = false;
    try {
        (void)iface.constraintsFor(OptimizationStage::StageD);
    } catch (const OptimizationError& e) {
        threw = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptStageLocked));
    }
    EXPECT_TRUE(threw) << "StageD 约束请求必须 OPT-STAGE-LOCKED（§5.7/§6.5）";
}

TEST(OptConstraint, StageBRequestingJointFamiliesThrowsStageLocked_WP20T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{"AT-09"});

    // OPT-VER-109/110/111：StageB 引用轨迹/动力学/器件联合约束即拒
    // （drivetrain 性能族一并覆盖——§5.7"StageB 激活 R2 约束"行）。
    const OptimizationConstraintProvider provider;
    const IOptimizationConstraintProvider& iface = provider;
    const std::vector<std::string> jointFamilies = {
        std::string(optimization::toToken(optimization::JointConstraintFamily::Trajectory)),
        std::string(optimization::toToken(optimization::JointConstraintFamily::Dynamics)),
        std::string(optimization::toToken(
            optimization::JointConstraintFamily::DrivetrainPerformance)),
        std::string(optimization::toToken(optimization::JointConstraintFamily::DeviceJoint)),
    };
    for (const auto& family : jointFamilies) {
        bool threw = false;
        try {
            // ★ 不静默降级的执行面：抛异常而非返回剔除后的清单/空表
            // （§6.5——拒绝即终止该研究启动，UI 呈现为阻塞诊断）。
            (void)iface.resolveConstraintPlan(OptimizationStage::StageB, {family});
        } catch (const OptimizationError& e) {
            threw = true;
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptStageLocked));
            EXPECT_NE(e.what(), nullptr);
        }
        EXPECT_TRUE(threw) << "联合约束族 " << family << " 在 StageB 必须阶段锁拒绝";
    }
    // 对照组：传动比变量（V12-02/DOPT-15）不在约束词表——其 StageB 放行
    // 是变量面（WP-20-T03 已钉），本用例确认约束面词表不含传动比条目
    // （参数可编辑≠性能可评估的两阶段口径分离在约束面同样成立）。
    const auto resolved = iface.resolveConstraintPlan(
        OptimizationStage::StageB, {std::string(optimization::toToken(ConstraintId::JointLimit))});
    ASSERT_EQ(resolved.size(), 1U);
    EXPECT_EQ(resolved[0].constraintId, ConstraintId::JointLimit);
}

TEST(OptConstraint, UnknownConstraintTokenThrowsInputInvalid_WP20T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // 未知 token ≠ 阶段锁（语义分立——防 AT-09 同型语义混同；T03 落位
    // 登记②的约束面同款防线）。
    const OptimizationConstraintProvider provider;
    bool threw = false;
    try {
        (void)provider.resolveConstraintPlan(OptimizationStage::StageB, {"opt.constraint.no-such"});
    } catch (const OptimizationError& e) {
        threw = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
    }
    EXPECT_TRUE(threw);
}

TEST(OptConstraint, PipelineStageDRequestThrowsStageLocked_WP20T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // §6.5：管线对 StageD 请求＝运行启动阻塞（抛 OPT-STAGE-LOCKED），
    // 不是候选淘汰（不返回任何候选记录）。
    PipelineFixture fixture;
    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageD, fixture.deps());
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    const auto coverage = makeCoverage(snapshot, {});
    auto input = makeBaselineInput(snapshot, slice, coverage);
    NopEvaluationContext ctx;
    bool threw = false;
    try {
        (void)pipeline.evaluate(input, ctx);
    } catch (const OptimizationError& e) {
        threw = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptStageLocked));
    }
    EXPECT_TRUE(threw) << "StageD 管线请求必须运行启动阻塞（§6.5 呈现边界）";
}

// =====================================================================
// 管线前置段（§6.2 第 3~6 步——锁定/补丁/编译/拓扑）
// =====================================================================

TEST(OptConstraint, LockedBindingPatchRejectedBeforePipeline_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{"AT-09"});

    // OPT-VER-103 管线半区：触及锁定绑定的补丁——生成阶段拒绝语义的
    // 防御面（候选 Infeasible＋逐项淘汰原因；I-C6 短路——编译器不被调用）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    const auto coverage = makeCoverage(snapshot, {});
    auto input = makeBaselineInput(snapshot, slice, coverage);
    // 锁定绑定＋补丁项。
    VariableBinding locked;
    locked.bindingId = "mdl.joint[2].dh.a";
    locked.kind = VariableKind::Continuous;
    locked.lowerBound = 0.05;   // m
    locked.upperBound = 0.40;   // m
    locked.authorized = false;  // 改型未授权 ⇒ 锁定（§5.4）
    locked.locked = true;
    input.bindings = {locked};
    // 原始补丁项直填（管线防御面消费未规范化 items——正常路径由生成器
    // 先经 makeCandidatePatch 校验，锁定拒绝在生成阶段发生；本用例钉住
    // 管线侧的同语义防御）。
    input.patch.items = {{"mdl.joint[2].dh.a", 0.30, 0, {}}};

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    // 淘汰语义：Infeasible＋不进可行集（AT-09）＋OPT-VAR-LOCKED 定位。
    EXPECT_EQ(record.status, CandidateStatus::Infeasible);
    ASSERT_FALSE(record.rejections.empty());
    bool hasLocked = false;
    for (const auto& r : record.rejections) {
        if (r.subject == "mdl.joint[2].dh.a") {
            hasLocked = true;
        }
    }
    EXPECT_TRUE(hasLocked) << "淘汰原因必须定位锁定绑定（比较型：绑定 token）";
    // I-C6 短路：编译器与下游探针不被调用（硬约束先行——失败即终止）。
    EXPECT_EQ(fixture.compiler.calls, 0);
    EXPECT_EQ(fixture.jointProbe.calls, 0);
}

TEST(OptConstraint, CompileFailureYieldsEvaluationFailedWithRtPassthrough_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // OPT-VER-114：候选编译失败——EvaluationFailed（环境错误轨，I-C2）＋
    // RT-* 码透传（cause 链保留）＋不进可行集；不判"约束违例"（§7.5：
    // 评估器失败≠约束失败——状态词不同）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    fixture.compiler.result.ok = false;
    fixture.compiler.result.stableCode = "RT-WC-COMPILE-FAILED";  // 透传 RT- 码
    fixture.compiler.result.detail = "注入的编译链故障";
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::EvaluationFailed);
    ASSERT_EQ(record.rejections.size(), 1U);
    EXPECT_EQ(record.rejections[0].reasonToken,
              std::string(optimization::kRejectCandidateCompileFailed));
    EXPECT_NE(record.rejections[0].sourceId.find("RT-WC-COMPILE-FAILED"),
              std::string::npos)
        << "RT-* 透传码必须保留在淘汰定位中（§6.6 cause 链）";
    // 编译失败的候选同样不进可行集（OPT-VER-114 观测点 ❌）。
    EXPECT_NE(record.status, CandidateStatus::Feasible);
    // 下游探针不被调用（I-C6）。
    EXPECT_EQ(fixture.jointProbe.calls, 0);
}

TEST(OptConstraint, TopologyOutOfScopeMakesInfeasible_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // OPT-VER-113 管线半区：基线链型超出 R1 启用范围（快照事实投影为假）
    // → 拓扑约束违例 → Infeasible（OPT-TOPOLOGY-REJECTED 语义——§6.6）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;
    input.baselineChainInEnabledScope = false;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::Infeasible);
    // 淘汰原因定位到拓扑约束（OPT-TOPOLOGY-REJECTED 语义的编排承载）。
    bool topologyRejected = false;
    for (const auto& r : record.rejections) {
        if (r.sourceId == std::string(optimization::toToken(ConstraintId::Topology))) {
            topologyRejected = true;
        }
    }
    EXPECT_TRUE(topologyRejected);
}

// =====================================================================
// 关节限位（§6.2 第 7 步——policy 探针投影；OPT-VER-118 管线半区）
// =====================================================================

TEST(OptConstraint, JointLimitMustFindingMakesInfeasible_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // 限位违例（Must）→ mustViolations → EngineeringInfeasible → Infeasible
    // （不进可行集）；比较型三要素（实际/要求/单位）落约束事实（ERR-01）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    optimization::JointLimitFindingRecord violated;
    violated.jointSubject = "obj-joint-2";
    violated.kindToken = "limit-violated";     // policy JointLimitFindingKind 投影
    violated.levelToken = "must";
    violated.actualValue = 3.5;                // rad（越上限 3.14）
    violated.thresholdValue = 3.14;            // rad（被越过的限位端）
    violated.unitSymbol = "rad";
    fixture.jointProbe.result.completed = true;
    fixture.jointProbe.result.findings = {violated};
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::Infeasible);
    EXPECT_EQ(record.engineeringStatus, core::EngineeringStatus::EngineeringInfeasible);
    // 约束事实：Violated＋比较型三元组（rad）。
    bool factFound = false;
    for (const auto& f : record.constraintFacts) {
        if (f.constraintId == ConstraintId::JointLimit) {
            factFound = true;
            EXPECT_EQ(f.verdict, ConstraintVerdict::Violated);
            EXPECT_DOUBLE_EQ(std::stod(f.actualText), 3.5);
            EXPECT_DOUBLE_EQ(std::stod(f.requiredText), 3.14);
            EXPECT_EQ(f.unitSymbol, "rad");
        }
    }
    EXPECT_TRUE(factFound);
    // 淘汰原因定位到限位约束（§8.3 第 9 步逐约束记录）。
    bool rejectionFound = false;
    for (const auto& r : record.rejections) {
        if (r.sourceId == std::string(optimization::toToken(ConstraintId::JointLimit))) {
            rejectionFound = true;
        }
    }
    EXPECT_TRUE(rejectionFound);
}

TEST(OptConstraint, JointLimitNearLimitWarnsButDoesNotBlock_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // Should 级（近限位警告）不阻断——Feasible＋警告事实（§7.5 ⑤级：
    // Should 违例→Feasible＋警告，REQ-06）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    optimization::JointLimitFindingRecord nearLimit;
    nearLimit.jointSubject = "obj-joint-3";
    nearLimit.kindToken = "near-limit";
    nearLimit.levelToken = "should";           // policy：NearLimit 恒 Should
    nearLimit.actualValue = 0.05;              // 近限位比 r（无量纲）
    nearLimit.thresholdValue = 0.10;           // 策略 nearLimitRatio
    nearLimit.unitSymbol = "1";
    fixture.jointProbe.result.completed = true;
    fixture.jointProbe.result.findings = {nearLimit};
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice,
                                   makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    // 不阻断：状态仍 Feasible（警告不产生淘汰原因——§7.5 区分表）。
    EXPECT_EQ(record.status, CandidateStatus::Feasible);
    EXPECT_TRUE(record.rejections.empty());
    bool warnFound = false;
    for (const auto& f : record.constraintFacts) {
        if (f.constraintId == ConstraintId::JointLimit) {
            EXPECT_EQ(f.verdict, ConstraintVerdict::Satisfied);
            EXPECT_NE(f.detail.find("近限位警告"), std::string::npos);
            warnFound = true;
        }
    }
    EXPECT_TRUE(warnFound);
}

TEST(OptConstraint, JointLimitProbeFailureYieldsEvaluationFailed_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // OPT-VER-123 半区：policy 限位评估级失败（名称不可解析/位置非有限）
    // ＝环境错误轨 → EvaluationFailed，**不判**约束违例（§7.5 区分表：
    // 评估器失败≠约束失败——状态词不同）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    fixture.jointProbe.result.completed = false;  // 评估未完成（Failed）
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::EvaluationFailed);
    EXPECT_TRUE(record.rejections.empty()) << "评估失败不是淘汰（无原因 token——I-C1）";
    EXPECT_EQ(fixture.collisionProbe.calls, 0) << "失败后下游短路（I-C6）";
}

// =====================================================================
// ③端口消费（§6.2 第 8/9 步——OPT-VER-115/116；C5/C8 搜索未果口径）
// =====================================================================

TEST(OptConstraint, ReachabilityMustViolationMakesInfeasibleViaPort3_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{"AT-09"});

    // OPT-VER-115：Must 工位可达违例——③端口评估器 verdictInputs 透传为
    // 判定素材（PA-1：违例语义归 kin 域）→ Infeasible；消费路径＝真注册表
    // find(优化侧常量键)→create→evaluate。
    PipelineFixture fixture;
    evidence::DomainVerdictViolation reachViolation;
    reachViolation.itemId = "kin.must-station-reachable";
    reachViolation.detail = "任务点 P1 超出工作半径（解析界限）";
    KinScript script;
    script.output.verdictInputs.mustViolations = {reachViolation};
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey),
                      std::move(script));
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;
    // manifest 绑定三元组的权威 contentIdentity（注册表计算值）。
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::Infeasible);
    // kin 域违例逐条透传进淘汰原因（O10 逐约束记录）。
    bool kinRejection = false;
    for (const auto& r : record.rejections) {
        if (r.sourceId == "kin.must-station-reachable") {
            kinRejection = true;
        }
    }
    EXPECT_TRUE(kinRejection);
    // 可达约束事实 Violated＋依据评估键（事实表溯源锚——§8.3 第 7 步）。
    bool factFound = false;
    for (const auto& f : record.constraintFacts) {
        if (f.constraintId == ConstraintId::MustStationReachability) {
            factFound = true;
            EXPECT_EQ(f.verdict, ConstraintVerdict::Violated);
            EXPECT_EQ(f.evaluationKey, std::string(optimization::kKinTaskPointsBatchKey));
        }
    }
    EXPECT_TRUE(factFound);
}

TEST(OptConstraint, SearchExhaustedIsDataInsufficientNeverInfeasible_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // OPT-VER-121（C5/C8）：搜索未果/多初值未收敛——DataInsufficient
    // （附搜索未果凭据透传），**不判任务级确定性不可行**（§6.2 铁律）。
    PipelineFixture fixture;
    KinScript script;
    evidence::SearchExhaustedRecord exhausted;
    exhausted.initialGuessesTried = 8U;  // 已试初值数（"扩大初值"复评对照基线）
    exhausted.searchBudgetUsed = 4096U;  // 已用搜索预算（迭代消耗量）
    script.output.searchRecord = exhausted;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey),
                      std::move(script));
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice,
                                   makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    // 搜索未果＝数据不足（非工程不可行）——两态不冒充（§7.5 区分表）。
    EXPECT_EQ(record.status, CandidateStatus::DataInsufficient);
    EXPECT_NE(record.status, CandidateStatus::Infeasible);
    EXPECT_TRUE(record.rejections.empty());
    // 约束事实：DataInsufficient（不读作"通过"——KIN-05 防伪口径的编排侧）。
    for (const auto& f : record.constraintFacts) {
        if (f.constraintId == ConstraintId::MustStationReachability) {
            EXPECT_EQ(f.verdict, ConstraintVerdict::DataInsufficient);
        }
    }
    // 判定追溯命中④级（MissingEvidence——P-EV-3 字面顺序的可核查证据；
    // 覆盖完备前提下④级命中＝搜索未果凭据的数据不足，非②级门禁短路）。
    EXPECT_EQ(record.verdictTrace.hitLevel, evidence::VerdictLevel::MissingEvidence);
}

TEST(OptConstraint, MissingPortEvaluatorFailsFast_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // ③端口必需评估器未注册（装配不一致）——kOptEvaluatorMissing fail-fast
    // （§6.4 检查项 #7 同源码面；不静默跳过约束）。
    PipelineFixture fixture;
    // 只注册覆盖评估器——可达评估器缺席。
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    bool threw = false;
    try {
        (void)pipeline.evaluate(input, fixture.ctx);
    } catch (const OptimizationError& e) {
        threw = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptEvaluatorMissing));
    }
    EXPECT_TRUE(threw);
}

// =====================================================================
// 静态碰撞（§6.2 第 10 步——C8 构型级作用域；SampleSet 消费形态）
// =====================================================================

TEST(OptConstraint, CollisionConfigLevelFilteredNotTaskInfeasible_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // OPT-VER-117（C8）：构型级碰撞样本——只记录过滤，不上升任务不可行
    // （候选其余全绿仍 Feasible——碰撞作用域是构型级，§8.1 原文）；
    // SampleSet 查询形态的构型传递逐位一致（AT-19 消费面）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    optimization::StaticCollisionSampleFinding hit0;
    hit0.sampleIndex = 0;
    hit0.inCollision = true;
    hit0.pairText = "obj-link-1|obj-env-1";
    optimization::StaticCollisionSampleFinding hit2;
    hit2.sampleIndex = 2;
    hit2.inCollision = true;
    hit2.pairText = "obj-link-2|obj-fixture";
    fixture.collisionProbe.result.completed = true;
    fixture.collisionProbe.result.findings = {hit0, hit2};
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;
    input.jointLimits.configurations = {{0.0}, {1.0}, {2.0}};  // 3 构型（rad）

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    // 候选不因构型级碰撞不可行（C8）——状态保持由其余约束决定（正例基线
    // 下为 Feasible）。
    EXPECT_EQ(record.status, CandidateStatus::Feasible);
    EXPECT_TRUE(record.rejections.empty());
    // 碰撞约束事实：Satisfied＋构型级过滤记录（2 例——detail 携带）。
    bool factFound = false;
    for (const auto& f : record.constraintFacts) {
        if (f.constraintId == ConstraintId::StaticCollision) {
            factFound = true;
            EXPECT_EQ(f.verdict, ConstraintVerdict::Satisfied);
            EXPECT_NE(f.detail.find("2 例已过滤"), std::string::npos);
        }
    }
    EXPECT_TRUE(factFound);
    // SampleSet 传递：探针收到全部 3 个构型（rad 逐轴一致）。
    ASSERT_EQ(fixture.collisionProbe.queriedConfigurations.size(), 3U);
    EXPECT_DOUBLE_EQ(fixture.collisionProbe.queriedConfigurations[2][0], 2.0);
}

TEST(OptConstraint, CollisionDisabledIsNotApplicable_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // 策略未启用碰撞（探针缺席——V13-01 无布尔开关）：约束 NotApplicable
    // （不读作"无碰撞"——KIN-05 防伪口径的编排侧对应）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    const auto snapshot = makeSnapshot(makeTwoMandatoryCases());
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice,
                                   makeCoverage(snapshot, makeAllCaseIds(snapshot)));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB,
                                                fixture.deps(false));  // 无碰撞探针
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::Feasible);
    for (const auto& f : record.constraintFacts) {
        if (f.constraintId == ConstraintId::StaticCollision) {
            EXPECT_EQ(f.verdict, ConstraintVerdict::NotApplicable);
        }
    }
}

// =====================================================================
// 判定汇总（§7.5 判定流——覆盖门禁/P-EV-7/正例/确定性）
// =====================================================================

TEST(OptConstraint, MissedMandatoryCaseNeverFormalPass_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03", "EVI-02"},
                  std::vector<std::string>{"AT-09"});

    // AT-09 反例（OPT-VER-119）：漏一个启用必验工况——②级覆盖门禁命中 →
    // DataInsufficient＋正式通过资格不成立（unmetConditions 含
    // coverage-incomplete）——**绝不输出正式通过**（EVI-02）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    const auto cases = makeTwoMandatoryCases();
    const auto snapshot = makeSnapshot(cases);
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, {}));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::DataInsufficient);
    EXPECT_FALSE(record.formalPassEligible) << "漏验不得出正式通过（EVI-02/AT-09）";
    bool coverageUnmet = false;
    for (const auto& token : record.formalPassUnmetConditions) {
        if (token == "coverage-incomplete") {
            coverageUnmet = true;
        }
    }
    EXPECT_TRUE(coverageUnmet);
    // 追溯命中②级通用门禁（P-EV-3 字面顺序——③/⑤级不再评估）。
    EXPECT_EQ(record.verdictTrace.hitLevel, evidence::VerdictLevel::CommonEvidenceGate);
}

TEST(OptConstraint, EmptyRequiredCaseSetConservativeDataInsufficient_WP20T04_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02"}, std::vector<std::string>{"AT-09"});

    // P-EV-7 同源（acceptance 4）：空必验工况集合——覆盖矩阵平凡完备但
    // ④级保守处置（DataInsufficient），绝不输出正式通过。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    const auto snapshot = makeSnapshot({});  // 空必验工况集（组装层合法入口）
    const auto slice = makeSlice();
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, {}));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::DataInsufficient);
    EXPECT_FALSE(record.formalPassEligible) << "空必验集合绝不输出正式通过（P-EV-7）";
    EXPECT_EQ(record.verdictTrace.hitLevel, evidence::VerdictLevel::MissingEvidence);
    EXPECT_TRUE(record.rejections.empty()) << "保守处置不是淘汰（I-C1）";
}

TEST(OptConstraint, FullyFeasibleCandidateIsFormalEligible_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03", "EVI-01"},
                  std::vector<std::string>{"AT-09"});

    // 正例：全约束满足＋覆盖完备（Verified）→ Feasible＋正式通过资格成立
    // （五条件同时满足——§7.2；"非支配≠工程通过"的候选侧前提）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    fixture.collisionProbe.result.completed = true;  // 碰撞评估完成、无发现
    const auto cases = makeTwoMandatoryCases();
    const auto snapshot = makeSnapshot(cases);
    const auto slice = makeSlice();
    std::vector<evidence::CaseId> executed{cases[0].caseId, cases[1].caseId};
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, executed));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto record = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(record.status, CandidateStatus::Feasible);
    EXPECT_EQ(record.engineeringStatus, core::EngineeringStatus::Feasible);
    EXPECT_TRUE(record.formalPassEligible);
    EXPECT_TRUE(record.formalPassUnmetConditions.empty());
    EXPECT_TRUE(record.rejections.empty());
    // 追溯命中⑤级（工程判定——P-EV-3 字面顺序：前级全过）。
    EXPECT_EQ(record.verdictTrace.hitLevel, evidence::VerdictLevel::EngineeringJudgement);
    // 约束事实表完整（十步各一条——§6.2 顺序审计面）。
    ASSERT_EQ(record.constraintFacts.size(), 10U);
    for (std::size_t i = 0; i < record.constraintFacts.size(); ++i) {
        EXPECT_EQ(record.constraintFacts[i].constraintId,
                  optimization::stageBConstraintPlan()[i].constraintId)
            << "约束事实必须按 §6.2 执行序排列（下标 " << i << "）";
    }
}

TEST(OptConstraint, SameInputYieldsIdenticalRecord_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});

    // 确定性（NFR-COR-02）：同输入同 deps ⇒ 同候选评估记录（纯函数面）。
    PipelineFixture fixture;
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinTaskPointsBatchKey), {});
    registerKinScript(fixture.evaluators,
                      std::string(optimization::kKinRegionCoverageKey), {});
    fixture.collisionProbe.result.completed = true;
    const auto cases = makeTwoMandatoryCases();
    const auto snapshot = makeSnapshot(cases);
    const auto slice = makeSlice();
    std::vector<evidence::CaseId> executed{cases[0].caseId, cases[1].caseId};
    auto input = makeBaselineInput(snapshot, slice, makeCoverage(snapshot, executed));
    input.manifest.profileContentIdentity
        = fixture.profiles.findProfile("kin", "1.0")->contentIdentity;

    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    const auto first = pipeline.evaluate(input, fixture.ctx);
    const auto second = pipeline.evaluate(input, fixture.ctx);

    EXPECT_EQ(first.status, second.status);
    EXPECT_EQ(first.engineeringStatus, second.engineeringStatus);
    EXPECT_EQ(first.formalPassEligible, second.formalPassEligible);
    ASSERT_EQ(first.constraintFacts.size(), second.constraintFacts.size());
    for (std::size_t i = 0; i < first.constraintFacts.size(); ++i) {
        EXPECT_EQ(first.constraintFacts[i].constraintId,
                  second.constraintFacts[i].constraintId);
        EXPECT_EQ(first.constraintFacts[i].verdict, second.constraintFacts[i].verdict);
        EXPECT_EQ(first.constraintFacts[i].detail, second.constraintFacts[i].detail);
    }
    EXPECT_EQ(first.verdictTrace, second.verdictTrace);
}

TEST(OptConstraint, InvalidCandidateIdFailsFast_WP20T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // 调用方契约违约（候选身份保留值）——fail-fast 异常轨（§12.3）。
    PipelineFixture fixture;
    const StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, fixture.deps());
    StaticHardConstraintInput input;  // candidateId 默认全零保留值
    NopEvaluationContext ctx;
    bool threw = false;
    try {
        (void)pipeline.evaluate(input, ctx);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(OptConstraint, DepsMissingRequiredMemberFailsAtConstruction_WP20T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // 装配违约（必填依赖空）——构造期 fail-fast；collisionProbe 缺席合法
    // （V13-01——EmptyRequiredCaseSet 用例同款已覆盖正例半区）。
    StaticHardConstraintDeps empty;
    bool threw = false;
    try {
        StaticHardConstraintPipeline pipeline(OptimizationStage::StageB, empty);
        (void)pipeline;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}
