/**
 * @file ReviewRegressionTest.cpp
 * @brief EV-T13/EVI-01/CON-01/05：首轮审查独立反例转正式回归。
 * 期望来自需求决策表及手工字节，不以产品校验器作为判定 oracle。
 * Closure/Producers/Facts 仅替代修订及注册查询端口，不提供业务数学证明。
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <sdurws/ird/evidence/Compatibility.hpp>
#include <sdurws/ird/evidence/Currentness.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>
namespace
{
using namespace sdurws::ird::evidence;
namespace c = sdurws::ird::core;
/** @brief 固定非零身份，无随机源。 */
template <class T>
T id(unsigned char x)
{
    T t;
    t.bytes.fill(x);
    return t;
}
/** @brief 内容身份测试值，仅用于不透明身份端口。 */
c::ContentIdentity ci(unsigned char x) { return id<c::ContentIdentity>(x); }
/** @brief 以 core 唯一摘要供给计算实际字节承诺。 */
c::Digest256 digest(std::vector<std::uint8_t> b)
{
    c::ContentDigester d;
    d.update(b.data(), b.size());
    return d.finalize();
}
/** @brief 修订事实查询替身；不用于验证业务证明。 */
struct Closure : IRevisionClosureSource
{
    bool objectInRevision(c::RevisionId, c::ObjectId, c::ContentVersion) const override
    {
        return true;
    }
};
/** @brief 仅注册 kin-ik@1 的产生者查询端口替身。 */
struct Producers : IProducerRegistryView
{
    bool isRegistered(std::string_view s) const override { return s == "kin-ik"; }
    bool contractVersionMatches(std::string_view s, std::uint32_t v) const override
    {
        return s == "kin-ik" && v == 1;
    }
};
/** @brief 通过真实 builder 生成合法快照，参数仅改变修订/预算。 */
AnalysisSnapshot snap(unsigned char rev = 3, unsigned char cfg = 1)
{
    SnapshotBuilder b;
    b.setIdentity(id<c::ProjectId>(1), id<c::BranchId>(2), id<c::RevisionId>(rev), 1);
    ObjectRefEntry o;
    o.objectId = id<c::ObjectId>(4);
    o.objectTypeToken = "model";
    o.digest = digest({7});
    o.contentVersion.bytes = o.digest;
    b.addObjectRef(o);
    b.addCase({id<c::ObjectId>(5), "load", true, true});
    b.setPolicyRef({ci(6)});
    b.setNameMapRef({ci(7)});
    b.setReproduction({"1", "1", {"snapshot/1", "slice/1"}, {}, {}});
    ConfigEntry conf{"ik", {cfg}, {}};
    conf.contentIdentity.bytes = digest(conf.canonicalBytes);
    b.addConfiguration(conf);
    return b.build(Closure{});
}
/** @brief 引用给定快照内真实对象版本。 */
DependencyEntry object(const AnalysisSnapshot& s)
{
    const auto& o = s.objectClosure[0];
    return {"model.robot",
            DependencyKind::Object,
            ObjectDependencyPayload{o.objectId, o.contentVersion, o.objectTypeToken},
            true,
            {}};
}
/** @brief 沿真实公共入口冻结模型与可选预算依赖。 */
InputSlice slice(const AnalysisSnapshot& s, bool cfg = false)
{
    SliceBuilder b;
    b.setEvaluation("kin-ik", 1).addEntry(object(s));
    if (cfg)
    {
        const auto& x = s.configurationRefs[0];
        b.addEntry(
            {"solver.config",
             DependencyKind::Configuration,
             ConfigurationDependencyPayload{x.configKindToken, x.canonicalBytes, x.contentIdentity},
             true,
             {}});
    }
    return b.build(s);
}
/** @brief 单线程测试底座；工程期望由各用例直接给出。 */
struct Fixture
{
    AnalysisSnapshot s = snap();
    InputSlice sl = slice(s);
    EvidenceProfileRegistry profiles;
    Producers producers;
    VerdictInput v;
    /** @brief 登记最小领域 Profile 并产生一份满足态清单。 */
    Fixture()
    {
        RequiredEvidenceProfile p;
        p.profileId = "kin";
        p.version = "1";
        p.required.push_back(
            {"kin.residual", EvidenceItemClass::Required, "required residual", {}, false});
        profiles.registerProfile(p);
        const auto* rp = profiles.findProfile("kin", "1");
        v.coverage.requiredCaseSetId = s.caseSet.requiredCaseSetId;
        v.coverage.entries.push_back(
            {id<c::ObjectId>(5), CaseExecutionStatus::Executed, {}, {}, {}});
        v.evidence = {s.snapshotId, sl.sliceId, "kin", "1", rp->contentIdentity, {}};
        EvidenceItem item;
        item.itemId = "kin.residual";
        item.status = EvidenceItemStatus::Satisfied;
        item.artifactDigest = digest({9});
        v.evidence.items.push_back(item);
    }
    /** @brief 执行实际汇总，不代替断言 oracle。 */
    VerdictResult verdict() { return aggregateVerdict(v, s, producers, profiles); }
    /** @brief 将汇总结果沿生产契约装配为待接纳值。 */
    ResultEnvelopeDraft draft(const VerdictResult& r)
    {
        ResultEnvelopeDraft d;
        d.task = {s.project, s.branch, s.revision, id<c::RunId>(8), c::AttemptId{9}};
        d.evaluationKey = "kin-ik";
        d.evaluatorContractVersion = 1;
        d.snapshotId = s.snapshotId;
        d.sliceId = sl.sliceId;
        d.inputBaselineId = sl.inputBaselineId;
        d.caseScope.caseIds = {id<c::ObjectId>(5)};
        d.profile = {"kin", "1", profiles.findProfile("kin", "1")->contentIdentity};
        d.engineeringStatus = r.status;
        d.evidence = v.evidence;
        d.missingItems = r.missingItems;
        d.searchRecord = r.searchRecord;
        d.producer.productVersion = "1";
        return d;
    }
};
TEST(ReviewR1_Controls, ValidFeasibleAndIncomplete)
{
    Fixture f;
    EXPECT_EQ(f.verdict().status, c::EngineeringStatus::Feasible);
    auto d = f.draft(f.verdict());
    EXPECT_TRUE(validateCombination(d, f.s, f.producers).empty());
    EXPECT_NO_THROW(ResultEnvelope::make(std::move(d)));
    f.v.evidence.items.clear();
    EXPECT_EQ(f.verdict().status, c::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(f.verdict().missingItems.size(), 1);
}
TEST(ReviewR1_Identity, UnrelatedRevisionKeepsSlice)
{
    auto a = snap(3), b = snap(10);
    EXPECT_EQ(slice(a).sliceId, slice(b).sliceId);
}
TEST(ReviewR1_Identity, BudgetChangeKeepsBaseline)
{
    auto a = snap(3, 1), b = snap(10, 2);
    EXPECT_NE(slice(a, true).sliceId, slice(b, true).sliceId);
    EXPECT_EQ(slice(a, true).inputBaselineId, slice(b, true).inputBaselineId);
}
TEST(ReviewR1_Identity, ForgedSnapshotIsRejectedAtSliceBoundary)
{
    auto s = snap();
    s.policyRef.policyContentIdentity = ci(99);
    EXPECT_THROW(slice(s), EvidenceError);
}
TEST(ReviewR1_Identity, ForeignPolicyRejected)
{
    auto s = snap();
    SliceBuilder b;
    b.setEvaluation("kin-ik", 1)
        .addEntry({"policy", DependencyKind::Policy, PolicyDependencyPayload{ci(99)}, true, {}});
    EXPECT_THROW(b.build(s), EvidenceError);
}
TEST(ReviewR1_Dependency, UnknownEnumsRejected)
{
    DependencyDeclaration d{"model.robot",
                            static_cast<DependencyKind>(255),
                            static_cast<DependencyRequiredness>(255),
                            {},
                            ""};
    EXPECT_FALSE(validateDependencyDeclarations({d}, {}).empty());
    DependencyEntry e{"model.robot", static_cast<DependencyKind>(255), {}, true, {}};
    EXPECT_FALSE(validateDependencyEntries({e}).empty());
}
TEST(ReviewR1_Profile, DuplicateAndCrossDomainRejected)
{
    RequiredEvidenceProfile p;
    p.profileId = "kin";
    p.version = "1";
    EvidenceProfileItem i{"dyn.torque", EvidenceItemClass::Required, "torque", {}, false};
    p.required = {i, i};
    EXPECT_FALSE(validateEvidenceProfile(p).empty());
}
TEST(ReviewR1_Verdict, UnconditionalRequiredCannotBeNA)
{
    Fixture f;
    f.v.evidence.items[0].status = EvidenceItemStatus::NotApplicable;
    f.v.evidence.items[0].artifactDigest.reset();
    f.v.evidence.items[0].notApplicableReason = "skip";
    auto r = f.verdict();
    EXPECT_EQ(r.status, c::EngineeringStatus::DataInsufficient);
    EXPECT_FALSE(checkFormalPassEligibility(f.v, f.s, r, f.profiles).eligible);
}
TEST(ReviewR1_Verdict, SatisfiedWithoutArtifactCannotPass)
{
    Fixture f;
    f.v.evidence.items[0].artifactDigest.reset();
    EXPECT_EQ(f.verdict().status, c::EngineeringStatus::DataInsufficient);
}
TEST(ReviewR1_Verdict, ForeignManifestSnapshotCannotPass)
{
    Fixture f;
    f.v.evidence.snapshotId = ci(99);
    EXPECT_EQ(f.verdict().status, c::EngineeringStatus::DataInsufficient);
}
TEST(ReviewR1_Verdict, DuplicateEvidenceOrderIndependent)
{
    Fixture f;
    auto item = f.v.evidence.items[0];
    item.status = EvidenceItemStatus::Missing;
    item.artifactDigest.reset();
    f.v.evidence.items.push_back(item);
    auto a = f.verdict();
    std::reverse(f.v.evidence.items.begin(), f.v.evidence.items.end());
    auto b = f.verdict();
    EXPECT_EQ(a.status, b.status);
    EXPECT_EQ(a.status, c::EngineeringStatus::DataInsufficient);
}
TEST(ReviewR1_Verdict, SearchInsufficientCanBeEnveloped)
{
    Fixture f;
    f.v.searchRecord = SearchExhaustedRecord{20, 5, {{"q1", SearchFilterReason::Collision}}};
    auto r = f.verdict();
    ASSERT_EQ(r.status, c::EngineeringStatus::DataInsufficient);
    EXPECT_FALSE(r.missingItems.empty());
    auto d = f.draft(r);
    EXPECT_TRUE(validateCombination(d, f.s, f.producers).empty());
    EXPECT_NO_THROW(ResultEnvelope::make(std::move(d)));
}
DeterministicInfeasibilityProof proof(const Fixture& f)
{
    DeterministicInfeasibilityProof p;
    p.category = ProofCategory::MandatoryStateCollision;
    p.claimToken = "mandatory-collision";
    p.subject = id<c::ObjectId>(99);
    p.mandatoryState = {"start", "task requires", id<c::ObjectId>(98)};
    p.collisionPairs = {{id<c::ObjectId>(97), id<c::ObjectId>(96), true}};
    p.coverageClaim = std::string(kCoverageClaimMandatoryState);
    p.snapshotId = f.s.snapshotId;
    p.sliceId = f.sl.sliceId;
    p.producer = "kin-ik";
    p.producerContractVersion = 1;
    return p;
}
TEST(ReviewR1_Verdict, ProofCannotBypassMissingPolicyIdentity)
{
    Fixture f;
    f.s.policyRef.policyContentIdentity = {};
    f.v.proof = proof(f);
    EXPECT_EQ(f.verdict().status, c::EngineeringStatus::DataInsufficient);
}
TEST(ReviewR1_Envelope, ProfileMismatchRejectedByStrongBoundary)
{
    Fixture f;
    auto d = f.draft(f.verdict());
    d.profile.contentIdentity = ci(99);
    EXPECT_FALSE(validateCombination(d, f.s, f.producers).empty());
}
TEST(ReviewR1_Envelope, TaskRevisionMismatchRejectedByStrongBoundary)
{
    Fixture f;
    auto d = f.draft(f.verdict());
    d.task.revision = id<c::RevisionId>(99);
    EXPECT_FALSE(validateCombination(d, f.s, f.producers).empty());
}
TEST(ReviewR1_Codec, IndependentInvalidSnapshotVectorRejected)
{
    std::vector<std::uint8_t> b = {'I', 'R', 'D', 'S', 'N', 'A', 'P', '1', 1, 0};
    b.resize(10 + 48 + 4 + 4 + 64 + 4 + 4 + 4, 0);
    b.insert(b.end(), {0, 1, 'p', 0, 1, 'e', 0, 0, 0, 0, 0, 0});
    EXPECT_THROW(SnapshotCodec::parseRefsOnly(b), EvidenceError);
}
TEST(ReviewR1_Codec, InvalidSliceEntryRejected)
{
    auto s = snap();
    auto sl = slice(s);
    sl.entries[0].key = "BAD";
    EXPECT_THROW(SliceCodec::parse(SliceCodec::encodeFull(sl)), EvidenceError);
}
TEST(ReviewR1_Codec, F64IndependentByteVector)
{
    const std::array<std::uint8_t, 8> one{{0x3f, 0xf0, 0, 0, 0, 0, 0, 0}};
    EXPECT_EQ(SliceCodec::canonicalF64(1.0), one);
    EXPECT_EQ(SliceCodec::parseCanonicalF64(one.data()), 1.0);
    EXPECT_NE(SliceCodec::canonicalF64(0.0), SliceCodec::canonicalF64(-0.0));
}
TEST(ReviewR1_Compatibility, MissingIdentitiesCannotFullHit)
{
    CacheHitQuery q;
    CachedResultSummary s;
    s.manifestFinalized = true;
    EXPECT_NE(judgeCacheHit(q, s).verdict, CacheHitResult::FullHit);
}
TEST(ReviewR1_Compatibility, PreviewCannotFullHit)
{
    CacheHitQuery q{c::EvaluationMode::Preview, ci(1), 1, ci(2)};
    CachedResultSummary s;
    s.mode = c::EvaluationMode::Preview;
    s.sliceId = ci(1);
    s.evaluatorContractVersion = 1;
    s.profileContentIdentity = ci(2);
    s.manifestFinalized = true;
    EXPECT_NE(judgeCacheHit(q, s).verdict, CacheHitResult::FullHit);
}
TEST(ReviewR1_Compatibility, CheckpointKeyMustMatchRequestedEvaluator)
{
    auto sl = slice(snap());
    CacheHitQuery q{c::EvaluationMode::Verified, sl.sliceId, 1, ci(1)};
    q.requestEvaluatorKey = "kin-ik";
    CheckpointSummary s{"dyn-other", sl.sliceId, 1, 1, true};
    EXPECT_EQ(judgeCheckpointCompatibility(q, s).verdict,
              CheckpointCompatibilityResult::Incompatible);
}

TEST(ReviewR1_AdditionalProfile, DuplicateSingleDomainRejected)
{
    RequiredEvidenceProfile p;
    p.profileId = "kin";
    p.version = "1";
    EvidenceProfileItem i{"kin.residual", EvidenceItemClass::Required, "residual", {}, false};
    p.required = {i, i};
    EXPECT_FALSE(validateEvidenceProfile(p).empty());
}
TEST(ReviewR1_AdditionalProfile, RequiredListRejectsSuggestedClass)
{
    RequiredEvidenceProfile p;
    p.profileId = "kin";
    p.version = "1";
    p.required = {{"kin.residual", EvidenceItemClass::Suggested, "residual", {}, false}};
    EXPECT_FALSE(validateEvidenceProfile(p).empty());
}
TEST(ReviewR1_AdditionalProof, EmptyPreconditionsRejected)
{
    Fixture f;
    DeterministicInfeasibilityProof p;
    p.category = ProofCategory::AnalyticBound;
    p.claimToken = "kin.reach-bound";
    p.subject = id<c::ObjectId>(4);
    p.boundExpression = "distance > bound";
    p.preconditions = "fixed model";
    p.coverageClaim = std::string(kCoverageClaimAllAlternatives);
    p.snapshotId = f.s.snapshotId;
    p.sliceId = f.sl.sliceId;
    p.producer = "kin-ik";
    p.producerContractVersion = 1;
    ASSERT_TRUE(validateProof(p, f.producers, f.s, f.sl.sliceId).empty());
    p.preconditions.clear();
    EXPECT_FALSE(validateProof(p, f.producers, f.s, f.sl.sliceId).empty());
}
TEST(ReviewR1_AdditionalProof, EmptyClaimAndSubjectRejected)
{
    Fixture f;
    DeterministicInfeasibilityProof p;
    p.category = ProofCategory::AnalyticBound;
    p.boundExpression = "distance > bound";
    p.preconditions = "fixed model";
    p.coverageClaim = std::string(kCoverageClaimAllAlternatives);
    p.snapshotId = f.s.snapshotId;
    p.sliceId = f.sl.sliceId;
    p.producer = "kin-ik";
    p.producerContractVersion = 1;
    EXPECT_FALSE(validateProof(p, f.producers, f.s, f.sl.sliceId).empty());
}
TEST(ReviewR1_AdditionalSnapshot, ConfigIdentityComputedFromBytes)
{
    auto s = snap();
    SnapshotBuilder b;
    b.setIdentity(s.project, s.branch, s.revision, 1);
    b.addObjectRef(s.objectClosure[0]);
    b.setPolicyRef(s.policyRef);
    b.setNameMapRef(s.nameMapRef);
    b.setReproduction(s.reproduction);
    b.addConfiguration({"ik", {5}, ci(99)});
    const auto result = b.build(Closure{});
    EXPECT_EQ(result.configurationRefs[0].contentIdentity.bytes, digest({5}));
}
TEST(ReviewR1_AdditionalSlice, ConfigurationMustBelongToSnapshot)
{
    auto s = snap();
    SliceBuilder b;
    b.setEvaluation("kin-ik", 1)
        .addEntry({"solver.config",
                   DependencyKind::Configuration,
                   ConfigurationDependencyPayload{"ik", {9}, ci(99)},
                   true,
                   {}});
    EXPECT_THROW(b.build(s), EvidenceError);
}
TEST(ReviewR1_AdditionalEnvelope, UnknownModeRejected)
{
    Fixture f;
    auto d = f.draft(f.verdict());
    d.mode = static_cast<c::EvaluationMode>(255);
    EXPECT_FALSE(validateCombination(d, f.s, f.producers).empty());
}
TEST(ReviewR1_AdditionalEvaluator, UnknownSupportedModeRejected)
{
    Fixture f;
    EvaluatorDescriptor d;
    d.key = "kin-ik";
    d.contractVersion = 1;
    d.inputs = {{"model.robot", DependencyKind::Object, DependencyRequiredness::Required, {}, ""}};
    d.profile = {"kin", "1", {}};
    d.supportedModes = {static_cast<c::EvaluationMode>(255)};
    d.threadSafety = ThreadSafety::SingleThread;
    EXPECT_THROW(requireValidEvaluatorRegistration(d, f.profiles, {}), EvidenceError);
}
TEST(ReviewR1_AdditionalVerdict, EmptyDomainResultsAreNotComplete)
{
    Fixture f;
    f.v.evidence.items[0].status = static_cast<EvidenceItemStatus>(255);
    EXPECT_EQ(f.verdict().status, c::EngineeringStatus::DataInsufficient);
}
TEST(ReviewR1_ReviewChain, MissingProofPreconditionsCannotQualifyAsFormalReview)
{
    Fixture f;
    DeterministicInfeasibilityProof p;
    p.category = ProofCategory::AnalyticBound;
    p.claimToken = "kin.reach-bound";
    p.subject = id<c::ObjectId>(4);
    p.boundExpression = "distance > bound";
    p.preconditions = "fixed model";
    p.coverageClaim = std::string(kCoverageClaimAllAlternatives);
    p.snapshotId = f.s.snapshotId;
    p.sliceId = f.sl.sliceId;
    p.producer = "kin-ik";
    p.producerContractVersion = 1;
    ASSERT_TRUE(validateProof(p, f.producers, f.s, f.sl.sliceId).empty());
    p.preconditions.clear();
    f.v.proof = p;
    auto result = f.verdict();
    EXPECT_EQ(result.status, c::EngineeringStatus::DataInsufficient);
    EXPECT_FALSE(checkReviewRecordEligibility(f.v, f.s, result, f.producers, f.profiles).eligible);
}
TEST(ReviewR1_ReviewChain, InvalidIndependentSnapshotBytesCannotProduceFrozenSlice)
{
    std::vector<std::uint8_t> bytes = {'I', 'R', 'D', 'S', 'N', 'A', 'P', '1', 1, 0};
    bytes.resize(10 + 48 + 4 + 4 + 64 + 4 + 4 + 4, 0);
    bytes.insert(bytes.end(), {0, 1, 'p', 0, 1, 'e', 0, 0, 0, 0, 0, 0});
    const auto buildInvalid = [&]
    {
        auto snapshot = SnapshotCodec::parseRefsOnly(bytes);
        SliceBuilder builder;
        builder.setEvaluation("kin-ik", 1)
            .addEntry({"policy", DependencyKind::Policy, PolicyDependencyPayload{ci(1)}, true, {}});
        return builder.build(snapshot);
    };
    EXPECT_THROW(buildInvalid(), EvidenceError);
}

/** EV-T13：有条件的未消费输入不强制解析无关配置。 */
TEST(ReviewR1_Slice, InactiveConfigurationDoesNotRequireSnapshotReference)
{
    auto snapshot = snap();
    SliceBuilder b;
    b.setEvaluation("kin-ik", 1).addEntry(object(snapshot));
    b.addEntry({"optional.config", DependencyKind::Configuration,
                ConfigurationDependencyPayload{"unused", {5}, ci(1)}, false, "条件不适用"});
    EXPECT_NO_THROW(b.build(snapshot));
}
/** EV-T13：同名不同实例的全部缺项可定位，不让首条满足遮蔽其余实例。 */
TEST(ReviewR1_Evidence, EveryMissingInstanceIsListed)
{
    Fixture f;
    auto item = f.v.evidence.items[0];
    item.status = EvidenceItemStatus::Missing;
    item.artifactDigest = std::nullopt;
    item.subject = id<c::ObjectId>(4);
    auto second = item;
    second.caseScope = std::vector<CaseId>{id<c::ObjectId>(5)};
    f.v.evidence.items = {item, second};
    const auto* profile = f.profiles.findProfile("kin", "1");
    const auto result = checkEvidenceCompleteness(*profile, f.v.evidence);
    ASSERT_EQ(result.requiredGaps.size(), 2);
    EXPECT_EQ(result.requiredGaps[0].manifestIndex, 0);
    EXPECT_EQ(result.requiredGaps[1].manifestIndex, 1);
    EXPECT_EQ(result.requiredGaps[0].status, EvidenceItemStatus::Missing);
}
/** EV-T13：物化入口与 refs-only 使用同一冻结不变量。 */
TEST(ReviewR1_Codec, MaterializedSnapshotCannotBypassInvariantChecks)
{
    auto snapshot = snap();
    snapshot.project = {};
    const auto bytes = SnapshotCodec::encodeMaterialized(snapshot, {});
    EXPECT_THROW(SnapshotCodec::parseMaterialized(bytes), EvidenceError);
}
/** EV-T13：结构相同但枚举/序列不合法的解码输入不能获得冻结身份。 */
TEST(ReviewR1_Codec, UnknownVersionAndUnsortedSliceRejected)
{
    auto s = slice(snap(), true);
    s.codecVersion = 255;
    EXPECT_THROW(SliceCodec::encodeFull(s), EvidenceError);
    s.codecVersion = 2;
    std::reverse(s.entries.begin(), s.entries.end());
    EXPECT_THROW(SliceCodec::parse(SliceCodec::encodeFull(s)), EvidenceError);
}
/** EV-T13：readiness 错误没有调用方明细时仍给出可归档的数据不足原因。 */
TEST(ReviewR1_Verdict, InvalidReadinessGetsMissingDetail)
{
    Fixture f;
    f.v.readiness.valid = false;
    const auto verdict = f.verdict();
    EXPECT_EQ(verdict.status, c::EngineeringStatus::DataInsufficient);
    EXPECT_FALSE(verdict.missingItems.empty());
    EXPECT_NO_THROW(ResultEnvelope::make(f.draft(verdict)));
}
}  // namespace
