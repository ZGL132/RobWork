/**
 * @file   EvidenceTest.cpp
 * @brief  证据契约用例组——validateProof 逐字段反例（EV-PROOF，含 EV-VER-6
 *         必经状态证明正反例）、证据项状态与清单（EV-ITEM，含 EV-VER-7
 *         不适用不计缺失）、Profile 承载与注册校验（EV-PROFILE，D-14）、
 *         覆盖矩阵（EV-COVM，EV-COV-2）、区域采样证据（EV-COVR，
 *         EV-COV-3）、Verified 前置固化（EV-MODE，CON-03）、搜索未果
 *         记录承载（EV-SEARCH）。
 *
 * 设计依据：
 *   - units/evidence.md §6.1～§6.3、§6.6、§9.5、§11 反例矩阵（EV-VER-6/
 *     EV-VER-7/EV-COV-2/EV-COV-3 行）、§12 EV-T05 行（验证方式＝
 *     "EV-VER-6/7、EV-COV-2/3；validateProof 逐字段反例通过"）
 *   - 需求 EVI-01/EVI-02、CON-03、ERR-01、KIN-04 R8、NFR-COR-02/03；
 *     任务契约 tasks/foundation/EV-T05.json（≙WP-05-T05）acceptance 1～4：
 *     ①EV-VER-6/7、EV-COV-2/3 用例通过；②validateProof 逐字段反例全部
 *     拒绝；③Profile 明细归需求 §8.1 表 4、不复制双账本（D-14）；④O-14
 *     未决保守字面——覆盖证据校验按 §6.6 字面实现（分母＝快照冻结态
 *     必验工况集）
 *
 * 范围说明（§12 依赖序）：EV-VER-6 的"③命中→EngineeringInfeasible→缺
 * 证明时④ DataInsufficient"与 EV-COV-1/3 的"汇总裁定 DataInsufficient"
 * 属 §6.4 决策表（aggregateVerdict，EV-T06）；本文件验证 EV-T05 交付面＝
 * 证明字段级校验、证据状态语义、覆盖/区域证据校验——汇总层消费的
 * 全部事实面。EV-VER-1~5/8 与 EV-COV-1/4 的用例体随 EV-T06/EV-T11 落地。
 *
 * 替身边界声明（EV-REG-3 同源纪律）：本文件的 AcceptAllClosureSource
 * （修订闭包事实来源替身）与 ScriptedProducerRegistry（产生者注册表
 * 替身）仅验证 evidence 契约（builder 闭包调用面、validateProof 注册
 * 查询面），其返回内容不构成任何 project/evaluator 侧实现正确性证明，
 * 也不构成 IK/动力学等业务算法正确性证明。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;

// =====================================================================
// 身份/取值辅助（确定性固定值——与 SnapshotTest 同款风格，自持不共享）
// =====================================================================

/// 32 个 'a'（Id128 合法 hex——对象/项目等身份用）。
const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
/// 32 个 'b'。
const char* kHex32B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
/// 32 个 'c'。
const char* kHex32C = "cccccccccccccccccccccccccccccccc";
/// 64 个 'a'（Digest 合法 hex——内容身份/版本用）。
const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
/// 64 个 'b'。
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
/// 64 个 'c'。
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

/// 内容身份（"cid-<64hex>" 严格解析）。
core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

/// 内容版本（"cv-<64hex>" 严格解析）。
core::ContentVersion cv(const char* hex64)
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + hex64);
}

/// 固定对象身份（"obj-<32hex>"——测试内确定性，不依赖生成器）。
core::ObjectId oid(const char* hex32)
{
    return core::ObjectId::fromCanonical(std::string{"obj-"} + hex32);
}

core::ProjectId fixedProject()
{
    return core::ProjectId::fromCanonical(std::string{"prj-"} + kHex32A);
}

core::BranchId fixedBranch()
{
    return core::BranchId::fromCanonical(std::string{"brn-"} + kHex32B);
}

core::RevisionId fixedRevision()
{
    return core::RevisionId::fromCanonical(std::string{"rev-"} + kHex32C);
}

/// 合法复现块（必填标量非空——builder 通过的最小形态）。
ReproductionBlock validReproduction()
{
    ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    return r;
}

/// 合法对象引用条目（digest 与 contentVersion 同源——builder 一致性闸门）。
ObjectRefEntry validObjectRef(core::ObjectId id, std::string typeToken)
{
    ObjectRefEntry e;
    e.objectId = id;
    e.contentVersion = cv(kHex64A);
    e.objectTypeToken = std::move(typeToken);
    e.digest = e.contentVersion.bytes;
    return e;
}

/**
 * @brief 修订闭包事实来源替身（EV-REG-3 声明见文件头）：对一切 (oid,cv)
 *        回答 true——builder 的防混入校验不是本文件被测面，替身放行使
 *        快照可冻结。
 */
class AcceptAllClosureSource : public IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

/// 标准必验工况集：A/B 双双 enabled∧mandatory（覆盖矩阵用例的分母）、
/// C enabled 但非必验（不进分母——O-14 字面承载的对照面）。
std::vector<CaseEntry> standardCases()
{
    return {{oid(kHex32A), "case-a", true, true},
            {oid(kHex32B), "case-b", true, true},
            {oid(kHex32C), "case-c-optional", true, false}};
}

/**
 * @brief 组装标准冻结快照（EV-T05 各用例的共用底座）：固定身份三元组＋
 *        策略/名称映射＋复现块＋1 对象闭包＋外部资源/采样计划/工况按参注入。
 *
 * @param resources 外部资源状态（默认单条已固化——Verified 门禁通过形态）
 * @param plans     冻结采样计划（默认无——区域覆盖用例按需注入）
 * @param cases     必验工况集（默认 standardCases）
 * @return 冻结快照（snapshotId/requiredCaseSetId 为 builder 计算值）
 */
AnalysisSnapshot buildStandardSnapshot(std::vector<ExternalResourceState> resources
                                       = {{oid(kHex32A), ExternalResourceStatus::Solidified,
                                           cv(kHex64B)}},
                                       std::vector<SamplingPlanRef> plans = {},
                                       std::vector<CaseEntry> cases = standardCases())
{
    SnapshotBuilder b;
    b.setIdentity(fixedProject(), fixedBranch(), fixedRevision(), 5);
    b.setPolicyRef(PolicyRef{cid(kHex64A)});
    b.setNameMapRef(NameMapRef{cid(kHex64B)});
    b.setReproduction(validReproduction());
    b.addObjectRef(validObjectRef(oid(kHex32A), "robot-design"));
    for (const CaseEntry& c : cases) {
        b.addCase(c);
    }
    for (const ExternalResourceState& r : resources) {
        b.addExternalResource(r);
    }
    for (const SamplingPlanRef& p : plans) {
        b.addSamplingPlan(p);
    }
    AcceptAllClosureSource source;
    return b.build(source);
}

/**
 * @brief 产生者注册表替身（EV-REG-3 声明见文件头）：固定 (键→契约版本)
 *        表——validateProof 的"已注册且契约版本相符"查询面的最小可观测
 *        实现；表内容不构成任何真实评估器注册状态的断言。
 */
class ScriptedProducerRegistry : public IProducerRegistryView {
public:
    explicit ScriptedProducerRegistry(std::map<std::string, std::uint32_t> table)
        : m_table(std::move(table))
    {
    }

    bool isRegistered(std::string_view key) const override
    {
        return m_table.count(std::string{key}) > 0;
    }

    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        const auto it = m_table.find(std::string{key});
        return it != m_table.end() && it->second == version;
    }

private:
    std::map<std::string, std::uint32_t> m_table; ///< 键→注册契约版本（替身数据）
};

/// 产生者注册表：kin-batch-ik@7 / trj-smooth-recheck@2（键词形合法——
/// 评估键不含点，Slice.hpp isValidEvaluationKey 词形）。
ScriptedProducerRegistry standardRegistry()
{
    return ScriptedProducerRegistry({{"kin-batch-ik", 7}, {"trj-smooth-recheck", 2}});
}

/// issue 清单中是否含指定码（逐字段反例的"被拒绝"判据）。
template <typename IssueRange, typename Code>
bool hasCode(const IssueRange& issues, Code code)
{
    for (const auto& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

/// 合法必经状态碰撞证明基底（EV-VER-6 正例形态——各反例在其上单字段破坏）。
DeterministicInfeasibilityProof validMandatoryStateProof(const AnalysisSnapshot& snapshot,
                                                         core::ContentIdentity sliceId)
{
    DeterministicInfeasibilityProof proof;
    proof.category = ProofCategory::MandatoryStateCollision;
    proof.claimToken = "kin.task-point-config-collision";
    proof.subject = oid(kHex32A);
    proof.mandatoryState.stateKind = "TaskPointConfig";
    proof.mandatoryState.nonSelectabilityBasis =
        "任务点构型是任务定义强制要求的末端状态，无替代选择（必经性依据）";
    proof.mandatoryState.objectId = oid(kHex32A);
    proof.collisionPairs = {{oid(kHex32A), oid(kHex32B), true},
                            {oid(kHex32A), oid(kHex32C), true}};
    proof.preconditions = "需求任务定义 v3；策略启用碰撞检测（绑定输入身份）";
    proof.coverageClaim = kCoverageClaimMandatoryState;
    proof.snapshotId = snapshot.snapshotId;
    proof.sliceId = sliceId;
    proof.producer = "kin-batch-ik";
    proof.producerContractVersion = 7;
    return proof;
}

}  // namespace

// =====================================================================
// EV-PROOF：validateProof 字段级校验（acceptance 2——逐字段反例全部拒绝）
// =====================================================================

/** EV-VER-6 正例：必经状态证明（mandatoryState+collisionPairs 齐备）逐字段校验通过。 */
TEST(EvidenceProof, MandatoryStateProofValid_EV_VER_6)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);
    const DeterministicInfeasibilityProof proof = validMandatoryStateProof(snapshot, sliceId);

    // 三类证明的字段级校验：全部问题一次列出（不短路）——合法证明必须零问题。
    const std::vector<ProofIssue> issues = validateProof(proof, registry, snapshot, sliceId);
    EXPECT_TRUE(issues.empty())
        << "合法必经状态证明不得报任何字段问题（EV-VER-6 正例半边）";
}

/** EV-PROOF 三类正例：解析界限/约束矛盾证明（条件字段齐备）同样零问题。 */
TEST(EvidenceProof, AnalyticAndContradictionProofsValid)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // 解析界限：界限表达＋"覆盖全部允许选择"（§6.3 覆盖范围两规范值之一）。
    DeterministicInfeasibilityProof analytic = validMandatoryStateProof(snapshot, sliceId);
    analytic.category = ProofCategory::AnalyticBound;
    analytic.boundExpression = "目标距离 > Σ连杆长（工作半径上界）";
    analytic.coverageClaim = kCoverageClaimAllAlternatives;
    EXPECT_TRUE(validateProof(analytic, registry, snapshot, sliceId).empty());

    // 约束矛盾：矛盾表达＋"覆盖全部允许选择"。
    DeterministicInfeasibilityProof contradiction = validMandatoryStateProof(snapshot, sliceId);
    contradiction.category = ProofCategory::ConstraintContradiction;
    contradiction.contradictionExpression = "必验工况 D 位姿与关节限位显式矛盾";
    contradiction.coverageClaim = kCoverageClaimAllAlternatives;
    EXPECT_TRUE(validateProof(contradiction, registry, snapshot, sliceId).empty());
}

/** EV-VER-6 反例：缺必经性依据的同款证明被拒（Invalid→汇总④级判定归 EV-T06）。 */
TEST(EvidenceProof, MandatoryStateProofMissingBasisRejected_EV_VER_6)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // "缺 mandatoryState 的同款证明"（§11 EV-VER-6 行）——不可选择性依据
    // 空缺：字段级校验逐项观测到 MandatoryStateBasisMissing。
    DeterministicInfeasibilityProof broken = validMandatoryStateProof(snapshot, sliceId);
    broken.mandatoryState.nonSelectabilityBasis.clear();
    const std::vector<ProofIssue> issues = validateProof(broken, registry, snapshot, sliceId);
    EXPECT_TRUE(hasCode(issues, ProofIssueCode::MandatoryStateBasisMissing))
        << "缺必经性依据的必经状态证明必须被拒（EV-VER-6 反例半边；"
           "Invalid→④ DataInsufficient 的判定归 EV-T06 决策表）";
}

/** EV-PROOF ①类别：三类之外（如"多初值全发散"）拒绝——EV-VER-2/3/4 拒绝面。 */
TEST(EvidenceProof, CategoryOutsideTripleRejected)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // 数值搜索未果不得冒充证明（§6.3：仅三类；越界值只能经非法转换到达
    // ——防御性范围检查覆盖）。
    DeterministicInfeasibilityProof bogus = validMandatoryStateProof(snapshot, sliceId);
    bogus.category = static_cast<ProofCategory>(3);
    const std::vector<ProofIssue> issues = validateProof(bogus, registry, snapshot, sliceId);
    EXPECT_TRUE(hasCode(issues, ProofIssueCode::CategoryInvalid))
        << "三类之外的 category 必须拒绝（EV-VER-2/3/4：搜索未果不是证明）";
}

/** EV-PROOF ②条件字段：解析界限缺界限表达拒绝；补齐后通过。 */
TEST(EvidenceProof, AnalyticBoundMissingExpressionRejected)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    DeterministicInfeasibilityProof proof = validMandatoryStateProof(snapshot, sliceId);
    proof.category = ProofCategory::AnalyticBound;
    proof.boundExpression.clear();
    proof.coverageClaim = kCoverageClaimAllAlternatives;
    EXPECT_TRUE(hasCode(validateProof(proof, registry, snapshot, sliceId),
                        ProofIssueCode::BoundExpressionMissing))
        << "AnalyticBound 缺界限表达必须拒绝（条件必填——§6.3）";

    proof.boundExpression = "目标距离 > Σ连杆长";
    EXPECT_TRUE(validateProof(proof, registry, snapshot, sliceId).empty())
        << "条件字段补齐后同一证明应通过（拒绝面只在字段缺失）";
}

/** EV-PROOF ②条件字段：约束矛盾缺矛盾表达拒绝。 */
TEST(EvidenceProof, ContradictionMissingExpressionRejected)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    DeterministicInfeasibilityProof proof = validMandatoryStateProof(snapshot, sliceId);
    proof.category = ProofCategory::ConstraintContradiction;
    proof.contradictionExpression.clear();
    proof.coverageClaim = kCoverageClaimAllAlternatives;
    EXPECT_TRUE(hasCode(validateProof(proof, registry, snapshot, sliceId),
                        ProofIssueCode::ContradictionExpressionMissing))
        << "ConstraintContradiction 缺矛盾表达必须拒绝（条件必填——§6.3）";
}

/** EV-PROOF ②条件字段：必经状态三要素（stateKind/依据/objectId）逐项缺失拒绝。 */
TEST(EvidenceProof, MandatoryStateTripleFieldsRejected)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // stateKind 空缺（域登记词表 token 必填）。
    DeterministicInfeasibilityProof noKind = validMandatoryStateProof(snapshot, sliceId);
    noKind.mandatoryState.stateKind.clear();
    EXPECT_TRUE(hasCode(validateProof(noKind, registry, snapshot, sliceId),
                        ProofIssueCode::MandatoryStateKindMissing));

    // objectId 保留值（空身份——无法定位必经状态对象）。
    DeterministicInfeasibilityProof noObject = validMandatoryStateProof(snapshot, sliceId);
    noObject.mandatoryState.objectId = core::ObjectId{};
    EXPECT_TRUE(hasCode(validateProof(noObject, registry, snapshot, sliceId),
                        ProofIssueCode::MandatoryStateObjectInvalid));
}

/** EV-PROOF ②条件字段：碰撞对象对——空集/保留值/自对/否定判定逐项拒绝（带下标）。 */
TEST(EvidenceProof, CollisionPairsCounterexamplesRejected)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // 空集：无证据来源的碰撞主张（§6.3 collisionPairs 必填）。
    DeterministicInfeasibilityProof noPairs = validMandatoryStateProof(snapshot, sliceId);
    noPairs.collisionPairs.clear();
    EXPECT_TRUE(hasCode(validateProof(noPairs, registry, snapshot, sliceId),
                        ProofIssueCode::CollisionPairsMissing));

    // 保留值身份：对象对无法定位。
    DeterministicInfeasibilityProof zeroId = validMandatoryStateProof(snapshot, sliceId);
    zeroId.collisionPairs = {{core::ObjectId{}, oid(kHex32B), true}};
    std::vector<ProofIssue> issues = validateProof(zeroId, registry, snapshot, sliceId);
    ASSERT_TRUE(hasCode(issues, ProofIssueCode::CollisionPairInvalid));
    EXPECT_EQ(issues.back().index, static_cast<std::size_t>(0))
        << "对象对问题必须带条目下标（逐字段定位）";

    // 自对：A==B 不构成"对象 ID 对"（§6.3 CollisionPair 实现口径）。
    DeterministicInfeasibilityProof selfPair = validMandatoryStateProof(snapshot, sliceId);
    selfPair.collisionPairs = {{oid(kHex32B), oid(kHex32B), true}};
    EXPECT_TRUE(hasCode(validateProof(selfPair, registry, snapshot, sliceId),
                        ProofIssueCode::CollisionPairInvalid));

    // 否定判定：判定为"未碰撞"的对不能支撑碰撞主张（NFR-COR-03 不伪造）。
    DeterministicInfeasibilityProof negative = validMandatoryStateProof(snapshot, sliceId);
    negative.collisionPairs = {{oid(kHex32A), oid(kHex32B), false}};
    EXPECT_TRUE(hasCode(validateProof(negative, registry, snapshot, sliceId),
                        ProofIssueCode::CollisionPairInvalid));
}

/** EV-PROOF ③产生者：词形非法/未注册/契约版本不符逐项拒绝。 */
TEST(EvidenceProof, ProducerCounterexamplesRejected)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // 词形非法（评估键含点——与依赖键词形不同，Slice.hpp 词形闸门口径）。
    DeterministicInfeasibilityProof badKey = validMandatoryStateProof(snapshot, sliceId);
    badKey.producer = "kin.batch-ik";
    EXPECT_TRUE(hasCode(validateProof(badKey, registry, snapshot, sliceId),
                        ProofIssueCode::ProducerKeyInvalid));

    // 未注册（未注册产生者的"证明"不可追溯——§6.3）。
    DeterministicInfeasibilityProof unknown = validMandatoryStateProof(snapshot, sliceId);
    unknown.producer = "kin-never-registered";
    EXPECT_TRUE(hasCode(validateProof(unknown, registry, snapshot, sliceId),
                        ProofIssueCode::ProducerNotRegistered));

    // 契约版本不符（同键不同版本＝算法契约已变——CON-04）。
    DeterministicInfeasibilityProof staleVersion = validMandatoryStateProof(snapshot, sliceId);
    staleVersion.producerContractVersion = 8;  // 注册表为 7
    EXPECT_TRUE(hasCode(validateProof(staleVersion, registry, snapshot, sliceId),
                        ProofIssueCode::ContractVersionMismatch));
}

/** EV-PROOF ④绑定：snapshotId/sliceId 空身份与错配逐项拒绝。 */
TEST(EvidenceProof, SnapshotSliceBindingCounterexamplesRejected)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // snapshotId 空身份（证明未绑定输入快照）。
    DeterministicInfeasibilityProof noSnapshot = validMandatoryStateProof(snapshot, sliceId);
    noSnapshot.snapshotId = core::ContentIdentity{};
    EXPECT_TRUE(hasCode(validateProof(noSnapshot, registry, snapshot, sliceId),
                        ProofIssueCode::SnapshotIdMissing));

    // snapshotId 与被汇总快照错配（证明针对别的输入）。
    DeterministicInfeasibilityProof wrongSnapshot = validMandatoryStateProof(snapshot, sliceId);
    wrongSnapshot.snapshotId = cid(kHex64B);
    EXPECT_TRUE(hasCode(validateProof(wrongSnapshot, registry, snapshot, sliceId),
                        ProofIssueCode::SnapshotIdMismatch));

    // sliceId 空身份。
    DeterministicInfeasibilityProof noSlice = validMandatoryStateProof(snapshot, sliceId);
    noSlice.sliceId = core::ContentIdentity{};
    EXPECT_TRUE(hasCode(validateProof(noSlice, registry, snapshot, sliceId),
                        ProofIssueCode::SliceIdMissing));

    // sliceId 与被汇总结果切片错配（expectedSliceId 事实面）。
    DeterministicInfeasibilityProof wrongSlice = validMandatoryStateProof(snapshot, sliceId);
    wrongSlice.sliceId = cid(kHex64B);
    EXPECT_TRUE(hasCode(validateProof(wrongSlice, registry, snapshot, sliceId),
                        ProofIssueCode::SliceIdMismatch));
}

/** EV-PROOF ⑤覆盖声明：与类别规范声明不符逐项拒绝（句法＝作用域声明核对）。 */
TEST(EvidenceProof, CoverageClaimCounterexamplesRejected)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // 必经状态证明误声明"覆盖全部允许选择"（C8 作用域越界）。
    DeterministicInfeasibilityProof overclaim = validMandatoryStateProof(snapshot, sliceId);
    overclaim.coverageClaim = kCoverageClaimAllAlternatives;
    EXPECT_TRUE(hasCode(validateProof(overclaim, registry, snapshot, sliceId),
                        ProofIssueCode::CoverageClaimInvalid));

    // 解析界限证明误声明"该必经状态"（同类反向错配）。
    DeterministicInfeasibilityProof underclaim = validMandatoryStateProof(snapshot, sliceId);
    underclaim.category = ProofCategory::AnalyticBound;
    underclaim.boundExpression = "目标距离 > Σ连杆长";
    underclaim.coverageClaim = kCoverageClaimMandatoryState;
    EXPECT_TRUE(hasCode(validateProof(underclaim, registry, snapshot, sliceId),
                        ProofIssueCode::CoverageClaimInvalid));

    // 自由文本（§6.3：coverageClaim 句法合法＝两规范声明之一）。
    DeterministicInfeasibilityProof freetext = validMandatoryStateProof(snapshot, sliceId);
    freetext.coverageClaim = "覆盖工作区域内全部采样点（自拟措辞）";
    EXPECT_TRUE(hasCode(validateProof(freetext, registry, snapshot, sliceId),
                        ProofIssueCode::CoverageClaimInvalid));

    // 空声明同样拒绝（句法非法的下界）。
    DeterministicInfeasibilityProof empty = validMandatoryStateProof(snapshot, sliceId);
    empty.coverageClaim.clear();
    EXPECT_TRUE(hasCode(validateProof(empty, registry, snapshot, sliceId),
                        ProofIssueCode::CoverageClaimInvalid));
}

/** EV-PROOF 确定性：同一坏证明多次校验必得同一 issue 序列（NFR-COR-02）。 */
TEST(EvidenceProof, IssueOrderDeterministic_NFR_COR_02)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ScriptedProducerRegistry registry = standardRegistry();
    const core::ContentIdentity sliceId = cid(kHex64C);

    // 多缺陷证明（类别条件缺失＋版本不符＋绑定错配＋声明错配）——
    // 检查序固定（ProofIssueCode 声明序＝校验序），两次结果逐条相等。
    DeterministicInfeasibilityProof broken = validMandatoryStateProof(snapshot, sliceId);
    broken.mandatoryState.nonSelectabilityBasis.clear();
    broken.collisionPairs.clear();
    broken.producerContractVersion = 9;
    broken.sliceId = cid(kHex64B);
    broken.coverageClaim.clear();

    const std::vector<ProofIssue> first = validateProof(broken, registry, snapshot, sliceId);
    const std::vector<ProofIssue> second = validateProof(broken, registry, snapshot, sliceId);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].code, second[i].code) << "第 " << i << " 条 issue 码漂移";
        EXPECT_EQ(first[i].message, second[i].message);
    }
    // 检查序锚定：必经状态依据（②组）先于契约版本（③组）再于切片绑定（④组）。
    ASSERT_GE(first.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(first.front().code, ProofIssueCode::MandatoryStateBasisMissing);
    EXPECT_TRUE(hasCode(first, ProofIssueCode::CollisionPairsMissing));
}

// =====================================================================
// EV-ITEM：证据项状态与清单（§6.2；EV-VER-7 判定面）
// =====================================================================

/** EV-ITEM presence 纪律：各状态必填字段缺失逐项拒绝，齐备形态通过。 */
TEST(EvidenceItems, PresenceDiscipline)
{
    const core::Digest256 digest = cv(kHex64A).bytes;

    // Satisfied 缺产物摘要（伪造面——NFR-COR-03）。
    EvidenceItem satisfiedNoDigest;
    satisfiedNoDigest.itemId = "kin.ik-convergence-per-point";
    satisfiedNoDigest.status = EvidenceItemStatus::Satisfied;
    std::vector<EvidenceItemIssue> issues = validateEvidenceItems({satisfiedNoDigest});
    EXPECT_TRUE(hasCode(issues, EvidenceItemIssueCode::SatisfiedDigestMissing));

    // Invalid 缺原因诊断（ERR-01：不伪造、留痕）。
    EvidenceItem invalidNoReason;
    invalidNoReason.itemId = "kin.ik-convergence-per-point";
    invalidNoReason.status = EvidenceItemStatus::Invalid;
    issues = validateEvidenceItems({invalidNoReason});
    EXPECT_TRUE(hasCode(issues, EvidenceItemIssueCode::InvalidReasonMissing));

    // NotApplicable 缺原因（C2：不适用必须显式解释——EV-VER-7 观测面）。
    EvidenceItem naNoReason;
    naNoReason.itemId = "kin.ik-convergence-per-point";
    naNoReason.status = EvidenceItemStatus::NotApplicable;
    issues = validateEvidenceItems({naNoReason});
    EXPECT_TRUE(hasCode(issues, EvidenceItemIssueCode::NotApplicableReasonMissing));

    // itemId 词形违约。
    EvidenceItem badId;
    badId.itemId = "kin_ik_convergence";
    badId.status = EvidenceItemStatus::Missing;
    issues = validateEvidenceItems({badId});
    EXPECT_TRUE(hasCode(issues, EvidenceItemIssueCode::ItemIdSyntax));

    // 五状态齐备形态全部通过（Missing/Unverified 无附加必填）。
    EvidenceItem ok1;
    ok1.itemId = "kin.ik-convergence-per-point";
    ok1.status = EvidenceItemStatus::Satisfied;
    ok1.artifactDigest = digest;
    EvidenceItem ok2;
    ok2.itemId = "kin.hard-filter-record";
    ok2.status = EvidenceItemStatus::Missing;
    EvidenceItem ok3;
    ok3.itemId = "kin.collision-evidence";
    ok3.status = EvidenceItemStatus::Unverified;
    EvidenceItem ok4;
    ok4.itemId = "kin.ik-continuity-per-cartesian-segment";
    ok4.status = EvidenceItemStatus::NotApplicable;
    ok4.notApplicableReason = "纯关节空间路径不含笛卡尔段";
    EvidenceItem ok5;
    ok5.itemId = "kin.task-infeasible-proof";
    ok5.status = EvidenceItemStatus::Invalid;
    ok5.invalidReason = core::DiagnosticRecord::make(
        "EVI-PROOF-INVALID", core::ObjectId{}, std::nullopt, std::nullopt,
        "证据绑定校验", "证明 sliceId 与被汇总结果不一致", "重跑该域评估并重挂证明");
    EXPECT_TRUE(validateEvidenceItems({ok1, ok2, ok3, ok4, ok5}).empty())
        << "五状态齐备形态必须全部通过（各态必填面各就各位）";
}

/**
 * EV-VER-7：不适用不计缺失——纯关节路径下 TRJ-02 连续性项 NotApplicable
 * （原因非空），完备性核对不产生缺失（"整体不因该缺项降级"的判定归
 * EV-T06 决策表）；同项改判 Missing/Unverified 则全量列出。
 */
TEST(EvidenceCompleteness, NotApplicableNotCountedMissing_EV_VER_7)
{
    // 轨迹域 Profile（表 4 轨迹行的三行承载实例——测试内域侧实例化，
    // 非 evidence 内建：D-14）。
    RequiredEvidenceProfile trj;
    trj.profileId = "trj";
    trj.version = "1.0.0";
    EvidenceProfileItem pathRecord;
    pathRecord.itemId = "trj.segment-path-and-time-param";
    pathRecord.itemClass = EvidenceItemClass::Required;
    pathRecord.description = "分段路径与时间参数化记录（含速度/加速度限制校验）";
    EvidenceProfileItem continuity;
    continuity.itemId = "trj.ik-continuity-per-cartesian-segment";
    continuity.itemClass = EvidenceItemClass::Required;
    continuity.description = "段内 IK 连续性检查（TRJ-02——适用条件＝路径含笛卡尔段）";
    Applicability cartCond;
    cartCond.conditionToken = "path-contains-cartesian-segment";
    cartCond.referencedKeys = {"trj.path-segments"};
    continuity.applicability = cartCond;
    EvidenceProfileItem smoothRecheck;
    smoothRecheck.itemId = "trj.smoothness-recheck";
    smoothRecheck.itemClass = EvidenceItemClass::Required;
    smoothRecheck.description = "平滑复检证据（TRJ-04：细分步长、段内碰撞判定、预算占用）";
    trj.required = {pathRecord, continuity, smoothRecheck};
    // 注册期语法面（§9.5）：该 Profile 须通过校验（条件词形合法）。
    EXPECT_TRUE(validateEvidenceProfile(trj).empty());

    // 纯关节路径场景：TRJ-02 项 NotApplicable（原因＝无笛卡尔段），其余满足。
    EvidenceManifest pureJoint;
    pureJoint.snapshotId = cid(kHex64A);
    pureJoint.sliceId = cid(kHex64B);
    pureJoint.profileId = "trj";
    pureJoint.profileVersion = "1.0.0";
    pureJoint.profileContentIdentity = cid(kHex64C);
    EvidenceItem satisfiedPath;
    satisfiedPath.itemId = "trj.segment-path-and-time-param";
    satisfiedPath.status = EvidenceItemStatus::Satisfied;
    satisfiedPath.artifactDigest = cv(kHex64A).bytes;
    EvidenceItem notApplicableContinuity;
    notApplicableContinuity.itemId = "trj.ik-continuity-per-cartesian-segment";
    notApplicableContinuity.status = EvidenceItemStatus::NotApplicable;
    notApplicableContinuity.notApplicableReason = "纯关节空间路径不含笛卡尔段";
    EvidenceItem satisfiedSmooth;
    satisfiedSmooth.itemId = "trj.smoothness-recheck";
    satisfiedSmooth.status = EvidenceItemStatus::Satisfied;
    satisfiedSmooth.artifactDigest = cv(kHex64B).bytes;
    pureJoint.items = {satisfiedPath, notApplicableContinuity, satisfiedSmooth};

    const EvidenceCompletenessResult clean = checkEvidenceCompleteness(trj, pureJoint);
    EXPECT_TRUE(clean.requiredGaps.empty())
        << "NotApplicable 项不计缺失（C2/ERR-01——EV-VER-7 判定面）";
    EXPECT_TRUE(clean.suggestedGaps.empty());

    // 反例半边：同一项改判 Missing/Unverified——必须进缺失清单（全量列出）。
    EvidenceManifest withMissing = pureJoint;
    withMissing.items[1].status = EvidenceItemStatus::Missing;
    withMissing.items[1].notApplicableReason = std::nullopt;
    EvidenceCompletenessResult gaps = checkEvidenceCompleteness(trj, withMissing);
    ASSERT_EQ(gaps.requiredGaps.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(gaps.requiredGaps[0].itemId, "trj.ik-continuity-per-cartesian-segment");
    EXPECT_EQ(gaps.requiredGaps[0].status, EvidenceItemStatus::Missing);

    withMissing.items[1].status = EvidenceItemStatus::Unverified;
    gaps = checkEvidenceCompleteness(trj, withMissing);
    ASSERT_EQ(gaps.requiredGaps.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(gaps.requiredGaps[0].status, EvidenceItemStatus::Unverified)
        << "Unverified 不满足且诊断区别于 Missing（§6.2 五值后果表）";

    // 完全无清单：三项全部 Missing——全量列出、不因首个缺失短路（表 2 ④）。
    const EvidenceCompletenessResult allGaps
        = checkEvidenceCompleteness(trj, EvidenceManifest{});
    ASSERT_EQ(allGaps.requiredGaps.size(), static_cast<std::size_t>(3));
}

/** EV-ITEM 清单绑定：caseScope ⊆ 必验集、subject ∈ 对象闭包（防错误引用）。 */
TEST(EvidenceManifest, BindingChecksAgainstSnapshot)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();

    EvidenceManifest manifest;
    manifest.snapshotId = snapshot.snapshotId;
    manifest.sliceId = cid(kHex64C);
    manifest.profileId = "kin";
    manifest.profileVersion = "1.0.0";
    manifest.profileContentIdentity = cid(kHex64A);

    EvidenceItem scoped;
    scoped.itemId = "kin.ik-convergence-per-point";
    scoped.status = EvidenceItemStatus::Satisfied;
    scoped.artifactDigest = cv(kHex64A).bytes;
    scoped.caseScope = std::vector<CaseId>{oid(kHex32A)};  // 必验集内——合法
    scoped.subject = oid(kHex32A);                          // 闭包内——合法
    manifest.items = {scoped};
    EXPECT_TRUE(validateEvidenceManifestBinding(manifest, snapshot).empty())
        << "合法绑定（工况在必验集、对象在闭包）必须通过";

    // 错误工况引用：引用必验集外的工况（快照 caseSet 只有 A/B/C）。
    EvidenceItem badCase = scoped;
    badCase.caseScope = std::vector<CaseId>{oid(kHex32A), core::ObjectId::generate()};
    manifest.items = {badCase};
    std::vector<ManifestBindingIssue> issues
        = validateEvidenceManifestBinding(manifest, snapshot);
    EXPECT_TRUE(hasCode(issues, ManifestBindingIssueCode::CaseScopeNotInSnapshot))
        << "必验集外工况引用必须拒绝（EV-COV-2 同源绑定面）";

    // 错误对象引用：subject 不在 objectClosure。
    EvidenceItem badSubject = scoped;
    badSubject.subject = core::ObjectId::generate();
    manifest.items = {badSubject};
    issues = validateEvidenceManifestBinding(manifest, snapshot);
    EXPECT_TRUE(hasCode(issues, ManifestBindingIssueCode::SubjectNotInObjectClosure));

    // 清单身份面残缺：sliceId 保留值。
    EvidenceManifest badIdentity = manifest;
    badIdentity.items = {scoped};
    badIdentity.sliceId = core::ContentIdentity{};
    issues = validateEvidenceManifestBinding(badIdentity, snapshot);
    EXPECT_TRUE(hasCode(issues, ManifestBindingIssueCode::ManifestIdentityInvalid));
}

// =====================================================================
// EV-PROFILE：Profile 承载（D-14——明细归需求表 4，不复制双账本）
// =====================================================================

/** EV-PROFILE 通用必需项：恰表 4 通用必需项三行、全部 Common、不豁免（D-14/C6）。 */
TEST(EvidenceProfile, CommonRequiredItemsMatchTable4GenericRows_D14)
{
    const std::vector<EvidenceProfileItem> common = commonRequiredItems();
    // 恰三行：①快照身份 ②必验工况覆盖矩阵 ③模式与证据等级标识
    // （表 4"通用必需项"原文行数——不增不减）。
    ASSERT_EQ(common.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(common[0].itemId, "common.snapshot-identity");
    EXPECT_EQ(common[1].itemId, "common.case-coverage-matrix");
    EXPECT_EQ(common[2].itemId, "common.mode-evidence-grade");
    for (const EvidenceProfileItem& item : common) {
        EXPECT_EQ(item.itemClass, EvidenceItemClass::Common);
        EXPECT_FALSE(item.substitutableByInfeasibility)
            << "通用门禁类替代标志必须 false（C6 不豁免）";
        EXPECT_FALSE(item.applicability.has_value()) << "通用必需项恒适用（Always）";
        EXPECT_FALSE(item.description.empty());
        // D-14 负向锚定：evidence 内建清单只有 common 域——五域明细行
        // （kin/trj/dyn/sel/opt）不得出现在 evidence 侧（域按表 4 自行
        // 实例化注册，§13 交接；内容权威唯一在 REQUIREMENTS 表 4）。
        const std::size_t dot = item.itemId.find('.');
        ASSERT_NE(dot, std::string::npos);
        EXPECT_EQ(item.itemId.substr(0, dot), "common")
            << "evidence 内建项不得冒充域明细（D-14 防双账本）";
    }
}

/** EV-PROFILE 注册期校验：九个反例逐一拒绝，合法 Profile 通过（§6.1 注册行）。 */
TEST(EvidenceProfile, RegistrationCounterexamplesRejected)
{
    // 合法基底：trj 域两行（一行带条件）。
    RequiredEvidenceProfile valid;
    valid.profileId = "trj";
    valid.version = "1.0.0";
    EvidenceProfileItem plain;
    plain.itemId = "trj.segment-path-and-time-param";
    plain.itemClass = EvidenceItemClass::Required;
    plain.description = "分段路径与时间参数化记录";
    EvidenceProfileItem conditional;
    conditional.itemId = "trj.ik-continuity-per-cartesian-segment";
    conditional.itemClass = EvidenceItemClass::Required;
    conditional.description = "段内 IK 连续性检查（TRJ-02）";
    Applicability cond;
    cond.conditionToken = "path-contains-cartesian-segment";
    cond.referencedKeys = {"trj.path-segments"};
    conditional.applicability = cond;
    valid.required = {plain, conditional};
    EXPECT_TRUE(validateEvidenceProfile(valid).empty())
        << "合法域 Profile（五域词表＋词形合规＋无 Common）必须通过";

    // profileId 出五域词表（"motion" 不是表 4 域 id）。
    RequiredEvidenceProfile badDomain = valid;
    badDomain.profileId = "motion";
    EXPECT_TRUE(hasCode(validateEvidenceProfile(badDomain), ProfileIssueCode::ProfileIdInvalid));

    // 版本串空。
    RequiredEvidenceProfile noVersion = valid;
    noVersion.version.clear();
    EXPECT_TRUE(hasCode(validateEvidenceProfile(noVersion), ProfileIssueCode::ProfileVersionInvalid));

    // itemId 词形（多点——域/项边界歧义）。
    RequiredEvidenceProfile badId = valid;
    badId.required[0].itemId = "trj.segment.path";
    std::vector<ProfileIssue> issues = validateEvidenceProfile(badId);
    ASSERT_TRUE(hasCode(issues, ProfileIssueCode::ItemIdSyntax));
    EXPECT_EQ(issues.front().index, static_cast<std::size_t>(0)) << "项级 issue 必带合并序列下标";

    // description 空（表 4 行文锚定缺失）。
    RequiredEvidenceProfile noDesc = valid;
    noDesc.required[0].description.clear();
    EXPECT_TRUE(hasCode(validateEvidenceProfile(noDesc), ProfileIssueCode::ItemDescriptionInvalid));

    // Common 类禁止域登记（单一权威——D-14/NFR-MNT-03）。
    RequiredEvidenceProfile commonInDomain = valid;
    commonInDomain.required[0].itemClass = EvidenceItemClass::Common;
    issues = validateEvidenceProfile(commonInDomain);
    EXPECT_TRUE(hasCode(issues, ProfileIssueCode::CommonItemInDomainProfile));

    // Common 类替代标志必须 false（C6 通用门禁不豁免）。
    RequiredEvidenceProfile commonSubstitutable = valid;
    commonSubstitutable.required[0].itemClass = EvidenceItemClass::Common;
    commonSubstitutable.required[0].substitutableByInfeasibility = true;
    issues = validateEvidenceProfile(commonSubstitutable);
    EXPECT_TRUE(hasCode(issues, ProfileIssueCode::SubstitutableFlagInconsistent));

    // conditionToken 空（词形下限）。
    RequiredEvidenceProfile badToken = valid;
    badToken.required[1].applicability->conditionToken.clear();
    EXPECT_TRUE(hasCode(validateEvidenceProfile(badToken),
                        ProfileIssueCode::ApplicabilityTokenInvalid));

    // referencedKeys 空集（条件不由任何键决定）。
    RequiredEvidenceProfile noKeys = valid;
    noKeys.required[1].applicability->referencedKeys.clear();
    EXPECT_TRUE(hasCode(validateEvidenceProfile(noKeys), ProfileIssueCode::ReferencedKeysEmpty));

    // referencedKey 语法非法（§9.5 Profile 注册期语法校验面）。
    RequiredEvidenceProfile badKey = valid;
    badKey.required[1].applicability->referencedKeys = {"Trj.Path-Segments"};
    EXPECT_TRUE(hasCode(validateEvidenceProfile(badKey), ProfileIssueCode::ReferencedKeySyntax));

    // 建议列表中的项同样受检（合并序列——suggested 段的项问题同样报出）。
    RequiredEvidenceProfile badSuggested = valid;
    EvidenceProfileItem suggested;
    suggested.itemId = "trj.CycleTime-Phase-Breakdown";
    suggested.itemClass = EvidenceItemClass::Suggested;
    suggested.description = "节拍分阶段分解";
    badSuggested.suggested = {suggested};
    issues = validateEvidenceProfile(badSuggested);
    ASSERT_TRUE(hasCode(issues, ProfileIssueCode::ItemIdSyntax));
    EXPECT_EQ(issues.front().index, static_cast<std::size_t>(2))
        << "suggested 段下标接续 required 段（合并序列语义）";
}

/** EV-PROFILE 内容身份：同内容同身份（乱序不变）、任一内容变化即变（域不可申报）。 */
TEST(EvidenceProfile, ContentIdentityDeterministicAndSensitive)
{
    // 同一内容的两种登记序（required 内两行交换）——身份必须相同
    // （编码前按 itemId 规范化排序，NFR-COR-02）。
    RequiredEvidenceProfile first;
    first.profileId = "trj";
    first.version = "1.0.0";
    EvidenceProfileItem a;
    a.itemId = "trj.segment-path-and-time-param";
    a.itemClass = EvidenceItemClass::Required;
    a.description = "分段路径与时间参数化记录";
    EvidenceProfileItem b;
    b.itemId = "trj.smoothness-recheck";
    b.itemClass = EvidenceItemClass::Required;
    b.description = "平滑复检证据（TRJ-04）";
    first.required = {a, b};

    RequiredEvidenceProfile second = first;
    second.required = {b, a};  // 仅登记序不同
    EXPECT_EQ(computeProfileContentIdentity(first), computeProfileContentIdentity(second))
        << "同内容任意登记序必得同 Profile 身份（注册器计算——域不可申报）";

    // 内容任一变化（描述文案）→身份变化（"profile 内容变化→旧结果不可
    // 直接复用"的凭据面，§8.2 消费）。
    RequiredEvidenceProfile changed = first;
    changed.required[0].description += "（修订）";
    EXPECT_NE(computeProfileContentIdentity(first), computeProfileContentIdentity(changed));

    // 版本变化→身份变化（version 参与编码）。
    RequiredEvidenceProfile reversioned = first;
    reversioned.version = "1.1.0";
    EXPECT_NE(computeProfileContentIdentity(first), computeProfileContentIdentity(reversioned));

    // 建议列表变化→身份变化。
    RequiredEvidenceProfile withSuggested = first;
    withSuggested.suggested = {a};
    EXPECT_NE(computeProfileContentIdentity(first), computeProfileContentIdentity(withSuggested));

    // 身份非零（可作绑定三元组成员——§6.1 表）。
    EXPECT_TRUE(computeProfileContentIdentity(first).isValid());
}

// =====================================================================
// EV-COVM：覆盖矩阵（EV-COV-2 重复/错误引用；漏验核对面）
// =====================================================================

/** EV-COV-2：重复 caseId 与必验集外引用逐条拒绝并列出具体条目。 */
TEST(EvidenceCoverageMatrix, DuplicateAndUnknownReferenceRejected_EV_COV_2)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();

    // 矩阵：A Executed＋A 重复（下标 1）＋必验集外工况 Executed（下标 2）。
    CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    matrix.entries = {{oid(kHex32A), CaseExecutionStatus::Executed, core::RunId::generate(),
                       cid(kHex64B), std::nullopt},
                      {oid(kHex32A), CaseExecutionStatus::Executed, core::RunId::generate(),
                       cid(kHex64B), std::nullopt},
                      {core::ObjectId::generate(), CaseExecutionStatus::Executed,
                       core::RunId::generate(), cid(kHex64B), std::nullopt}};

    const CoverageCheckResult result = validateCaseCoverageMatrix(matrix, snapshot);
    EXPECT_FALSE(result.matrixLegal) << "重复/错误引用使矩阵非法（EV-COV-2）";
    EXPECT_FALSE(result.coverageComplete) << "非法矩阵不得宣称覆盖完备（保守方向）";
    // 逐条目定位（"校验错误列出具体条目"——EV-COV-2 观测点）。
    ASSERT_TRUE(hasCode(result.issues, CoverageIssueCode::DuplicateCaseEntry));
    bool sawDuplicateAt1 = false;
    for (const CoverageIssue& issue : result.issues) {
        if (issue.code == CoverageIssueCode::DuplicateCaseEntry) {
            EXPECT_EQ(issue.index, static_cast<std::size_t>(1));
            EXPECT_EQ(issue.caseId, oid(kHex32A).toCanonical());
            sawDuplicateAt1 = true;
        }
    }
    EXPECT_TRUE(sawDuplicateAt1);
    ASSERT_TRUE(hasCode(result.issues, CoverageIssueCode::UnknownCaseReference));
    for (const CoverageIssue& issue : result.issues) {
        if (issue.code == CoverageIssueCode::UnknownCaseReference) {
            EXPECT_EQ(issue.index, static_cast<std::size_t>(2));
        }
    }
}

/** EV-COVM 与计划核对＋NotApplicable 原因＋追溯面：身份错配/缺原因/保留值拒绝。 */
TEST(EvidenceCoverageMatrix, PlanIdentityAndEntryDiscipline)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();

    // requiredCaseSetId 与快照冻结必验集身份不符（与计划核对——§6.6 第 1 条）。
    CaseCoverageMatrix mismatch;
    mismatch.requiredCaseSetId = cid(kHex64C);
    mismatch.entries = {{oid(kHex32A), CaseExecutionStatus::Executed, std::nullopt,
                         std::nullopt, std::nullopt},
                        {oid(kHex32B), CaseExecutionStatus::Executed, std::nullopt,
                         std::nullopt, std::nullopt}};
    CoverageCheckResult result = validateCaseCoverageMatrix(mismatch, snapshot);
    EXPECT_FALSE(result.matrixLegal);
    EXPECT_TRUE(hasCode(result.issues, CoverageIssueCode::RequiredCaseSetIdMismatch));

    // NotApplicable 条目缺非空原因（§6.6 约束行）。
    CaseCoverageMatrix naNoReason;
    naNoReason.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    naNoReason.entries = {{oid(kHex32A), CaseExecutionStatus::Executed, std::nullopt,
                           std::nullopt, std::nullopt},
                          {oid(kHex32B), CaseExecutionStatus::NotApplicable, std::nullopt,
                           std::nullopt, std::nullopt}};
    result = validateCaseCoverageMatrix(naNoReason, snapshot);
    EXPECT_FALSE(result.matrixLegal);
    EXPECT_TRUE(hasCode(result.issues, CoverageIssueCode::NotApplicableReasonMissing));
    // 补上原因后矩阵合法；但 NotApplicable≠Executed——必验工况的覆盖按
    // §6.6 判据字面仍不完备（"全不适用"的保守处置属汇总层，P-EV-7/EV-T06）。
    naNoReason.entries[1].notApplicableReason = "该工况对象已禁用（需求冻结态标记）";
    result = validateCaseCoverageMatrix(naNoReason, snapshot);
    EXPECT_TRUE(result.matrixLegal);
    EXPECT_TRUE(hasCode(result.issues, CoverageIssueCode::MissingMandatoryExecution));

    // 追溯面残缺：runId 保留值。
    CaseCoverageMatrix badRun;
    badRun.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    badRun.entries = {{oid(kHex32A), CaseExecutionStatus::Executed, core::RunId{},
                       std::nullopt, std::nullopt},
                      {oid(kHex32B), CaseExecutionStatus::Executed, std::nullopt,
                       std::nullopt, std::nullopt}};
    result = validateCaseCoverageMatrix(badRun, snapshot);
    EXPECT_FALSE(result.matrixLegal);
    EXPECT_TRUE(hasCode(result.issues, CoverageIssueCode::EntryRunRefInvalid));

    // resultSliceId 保留值。
    CaseCoverageMatrix badSlice = badRun;
    badSlice.entries[0].runId = core::RunId::generate();
    badSlice.entries[0].resultSliceId = core::ContentIdentity{};
    result = validateCaseCoverageMatrix(badSlice, snapshot);
    EXPECT_TRUE(hasCode(result.issues, CoverageIssueCode::EntryResultSliceRefInvalid));
}

/** EV-COVM 漏验核对（EV-COV-1 的校验面）：缺一条 Executed→不完备并列出工况 id。 */
TEST(EvidenceCoverageMatrix, MissingMandatoryExecutionListed)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();

    // 只覆盖 A：B（enabled∧mandatory）漏验——矩阵本身合法（无重复/错误
    // 引用）但覆盖不完备（EV-COV-1 校验面；②级判定归 EV-T06）。
    CaseCoverageMatrix partial;
    partial.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    partial.entries = {{oid(kHex32A), CaseExecutionStatus::Executed, std::nullopt,
                        std::nullopt, std::nullopt}};
    const CoverageCheckResult result = validateCaseCoverageMatrix(partial, snapshot);
    EXPECT_TRUE(result.matrixLegal) << "漏验是覆盖不完备而非矩阵非法（两面分离）";
    EXPECT_FALSE(result.coverageComplete);
    ASSERT_EQ(result.issues.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(result.issues[0].code, CoverageIssueCode::MissingMandatoryExecution);
    EXPECT_EQ(result.issues[0].caseId, oid(kHex32B).toCanonical())
        << "漏验 issue 必须点名工况 id（missingItems 数据源）";

    // 非 mandatory 工况（C）不进分母：只覆盖 A/B 即完备（O-14 字面——
    // 分母＝快照冻结态 enabled∧mandatory 工况集）。
    CaseCoverageMatrix full = partial;
    full.entries.push_back({oid(kHex32B), CaseExecutionStatus::Executed, std::nullopt,
                            std::nullopt, std::nullopt});
    const CoverageCheckResult complete = validateCaseCoverageMatrix(full, snapshot);
    EXPECT_TRUE(complete.matrixLegal);
    EXPECT_TRUE(complete.coverageComplete) << "全部 enabled∧mandatory 工况 Executed＝覆盖完备";
    EXPECT_TRUE(complete.issues.empty());
}

/** EV-COVM 空必验集（P-EV-7 保守处置的校验面）：平凡完备、无漏验 issue。 */
TEST(EvidenceCoverageMatrix, EmptyRequiredSetTriviallyComplete_P_EV_7)
{
    // 空工况集快照（EV-T03 起 builder 允许空集——P-EV-7：判定面在汇总层）。
    const AnalysisSnapshot snapshot = buildStandardSnapshot({}, {}, {});

    CaseCoverageMatrix empty;
    empty.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    const CoverageCheckResult result = validateCaseCoverageMatrix(empty, snapshot);
    EXPECT_TRUE(result.matrixLegal);
    EXPECT_TRUE(result.coverageComplete) << "无必验工况＝无漏验（平凡完备——"
                                           "依赖工况证据的判定项不满足归汇总④级，EV-T06）";
    EXPECT_TRUE(result.issues.empty());
}

// =====================================================================
// EV-COVR：区域采样证据（EV-COV-3 零样本与分母）
// =====================================================================

/** EV-COV-3：零样本——覆盖率不定义（不得输出 0%/100%；裁定归 EV-T06）。 */
TEST(EvidenceRegionCoverage, ZeroSampleUndefined_EV_COV_3)
{
    // 快照冻结计划：位置 0＋位姿 0（零样本计划合法——§4.1.4）。
    SamplingPlanRef zeroPlan;
    zeroPlan.regionObjectId = oid(kHex32A);
    zeroPlan.planContentIdentity = cid(kHex64B);
    zeroPlan.plannedPositionSamples = 0;
    zeroPlan.plannedPoseSamples = 0;
    zeroPlan.sampleSetIdentity = cid(kHex64C);
    const AnalysisSnapshot snapshot = buildStandardSnapshot({}, {zeroPlan}, {});

    RegionCoverageEvidence evidence;
    evidence.sampleSetIdentity = zeroPlan.sampleSetIdentity;
    evidence.plannedPositionSamples = 0;
    evidence.plannedPoseSamples = 0;
    // 全零计数——与零分母自洽。
    evidence.position = {0, 0, 0};
    evidence.pose = {0, 0, 0};
    evidence.downgraded = false;

    const RegionCoverageCheckResult result = validateRegionCoverageEvidence(evidence, snapshot);
    EXPECT_TRUE(result.valid) << "零样本是自洽形状而非畸形（分母 0＝和 0）";
    EXPECT_TRUE(result.zeroSample)
        << "零样本必须暴露（覆盖率不定义——汇总判 DataInsufficient、"
           "不得输出 0%/100%——EV-COV-3 反例面，裁定归 EV-T06）";
    EXPECT_TRUE(result.issues.empty());
}

/** EV-COV-3：分母完整性——60+40+0 对 100 通过；缺额/降级标记逐项核对。 */
TEST(EvidenceRegionCoverage, DenominatorCompletenessAndDowngrade_EV_COV_3)
{
    // 快照冻结计划：位置 100＋位姿 50。
    SamplingPlanRef plan;
    plan.regionObjectId = oid(kHex32A);
    plan.planContentIdentity = cid(kHex64B);
    plan.plannedPositionSamples = 100;
    plan.plannedPoseSamples = 50;
    plan.sampleSetIdentity = cid(kHex64C);
    const AnalysisSnapshot snapshot = buildStandardSnapshot({}, {plan}, {});

    // (a) 固定计数 60+40+0（位置）与 30+20+0（位姿）：分母完整通过，
    //     60% 可作参考值（无数据不足、无降级必要）。
    RegionCoverageEvidence complete;
    complete.sampleSetIdentity = plan.sampleSetIdentity;
    complete.plannedPositionSamples = 100;
    complete.plannedPoseSamples = 50;
    complete.position = {60, 40, 0};
    complete.pose = {30, 20, 0};
    complete.downgraded = false;
    RegionCoverageCheckResult result = validateRegionCoverageEvidence(complete, snapshot);
    EXPECT_TRUE(result.valid);
    EXPECT_FALSE(result.zeroSample);
    EXPECT_FALSE(result.downgradedRequired);

    // (b) 分母残缺：位置 60+30+0=90≠100→Invalid（EV-COV-3 分母完整性）。
    RegionCoverageEvidence shortDenominator = complete;
    shortDenominator.position = {60, 30, 0};
    result = validateRegionCoverageEvidence(shortDenominator, snapshot);
    EXPECT_FALSE(result.valid);
    ASSERT_TRUE(hasCode(result.issues, RegionCoverageIssueCode::DenominatorIncomplete));
    for (const RegionCoverageIssue& issue : result.issues) {
        if (issue.code == RegionCoverageIssueCode::DenominatorIncomplete) {
            EXPECT_EQ(issue.column, RegionCoverageColumn::Position);
        }
    }

    // (c) 数据不足且已降级：dataInsufficient>0＋downgraded=true→形状合格
    //     ＋降级必要暴露（覆盖率仅参考值、结论整体降级——§6.6 downgraded 行）。
    RegionCoverageEvidence downgraded = complete;
    downgraded.position = {60, 30, 10};
    downgraded.downgraded = true;
    result = validateRegionCoverageEvidence(downgraded, snapshot);
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.downgradedRequired);

    // (d) 数据不足但未标记降级→拒绝（§6.6：dataInsufficient>0 ⇒ downgraded）。
    RegionCoverageEvidence unmarked = complete;
    unmarked.position = {60, 30, 10};
    result = validateRegionCoverageEvidence(unmarked, snapshot);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(hasCode(result.issues, RegionCoverageIssueCode::DowngradedFlagInconsistent));

    // (e) 分母来源错配：样本集身份不在快照冻结计划中。
    RegionCoverageEvidence foreignSet = complete;
    foreignSet.sampleSetIdentity = cid(kHex64A);
    result = validateRegionCoverageEvidence(foreignSet, snapshot);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(hasCode(result.issues, RegionCoverageIssueCode::SampleSetNotFoundInSnapshot));

    // (f) 声明分母与冻结计划不符（位置 99≠100——分母被调包）。
    RegionCoverageEvidence tampered = complete;
    tampered.plannedPositionSamples = 99;
    result = validateRegionCoverageEvidence(tampered, snapshot);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(hasCode(result.issues, RegionCoverageIssueCode::PlannedCountMismatch));

    // (g) 保留值样本集身份（无分母锚）。
    RegionCoverageEvidence noAnchor = complete;
    noAnchor.sampleSetIdentity = core::ContentIdentity{};
    result = validateRegionCoverageEvidence(noAnchor, snapshot);
    EXPECT_TRUE(hasCode(result.issues, RegionCoverageIssueCode::SampleSetIdentityInvalid));
}

// =====================================================================
// EV-MODE：Verified 前置固化校验（§6.2/CON-03）
// =====================================================================

/** EV-MODE 固化门禁：Recorded 资源阻断 Verified、不阻断 Quick（CON-03）。 */
TEST(EvidenceSnapshotMode, VerifiedRequiresSolidifiedResources_CON_03)
{
    // 一条 Recorded＋一条 Solidified：Verified 逐条列出 Recorded、放过
    // Solidified；Quick/Preview 不强制固化（表 1：其产物不支撑正式结论）。
    const std::vector<ExternalResourceState> mixed
        = {{oid(kHex32A), ExternalResourceStatus::Recorded, std::nullopt},
           {oid(kHex32B), ExternalResourceStatus::Solidified, cv(kHex64B)}};
    const AnalysisSnapshot snapshot = buildStandardSnapshot(mixed, {}, {});

    const std::vector<SnapshotModeIssue> verifiedIssues
        = validateSnapshotForMode(snapshot, core::EvaluationMode::Verified);
    ASSERT_EQ(verifiedIssues.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(verifiedIssues[0].code, SnapshotModeIssueCode::ExternalResourceNotSolidified);
    EXPECT_EQ(verifiedIssues[0].index, static_cast<std::size_t>(0))
        << "未固化资源逐条定位（下标 0＝Recorded 条目）";

    EXPECT_TRUE(validateSnapshotForMode(snapshot, core::EvaluationMode::Quick).empty())
        << "Quick 不强制固化（§6.2 原文）";
    EXPECT_TRUE(validateSnapshotForMode(snapshot, core::EvaluationMode::Preview).empty());

    // 全部固化＋Verified：通过（CON-03 三段边界闭合）。
    const AnalysisSnapshot solid
        = buildStandardSnapshot({{oid(kHex32A), ExternalResourceStatus::Solidified, cv(kHex64B)}},
                                {}, {});
    EXPECT_TRUE(validateSnapshotForMode(solid, core::EvaluationMode::Verified).empty());
}

/** EV-MODE 复现块完整性：手工构造的残缺快照被拦（防线不因调用路径而缺）。 */
TEST(EvidenceSnapshotMode, ReproductionCompletenessChecked)
{
    // 手工构造（绕过 builder）的快照：复现块版本要素空——validateForMode
    // 对快照值复核（builder 之外的构造路径同受约束）。
    AnalysisSnapshot handBuilt{};
    handBuilt.reproduction.productVersion.clear();
    handBuilt.reproduction.evidenceContractVersion.clear();

    const std::vector<SnapshotModeIssue> issues
        = validateSnapshotForMode(handBuilt, core::EvaluationMode::Quick);
    ASSERT_EQ(issues.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(issues[0].code, SnapshotModeIssueCode::ReproductionIncomplete);
    EXPECT_EQ(issues[0].index, SnapshotModeIssue::npos) << "复现块问题非资源条目（npos）";
}

// =====================================================================
// EV-SEARCH：搜索未果记录承载（§6.3 末——判定语义归 EV-T06）
// =====================================================================

/** EV-SEARCH 记录承载：预算/初值数/过滤清单（含全部碰撞过滤形态）保真。 */
TEST(EvidenceSearchExhausted, RecordCarriesBudgetGuessesAndFilters)
{
    // EV-VER-4 数据形态：全部已找到的解因碰撞被过滤（C8——构型级碰撞仅
    // 过滤该解；DataInsufficient 判定与"不得转不可行"归 EV-T06 决策表）。
    SearchExhaustedRecord allCollision;
    allCollision.searchBudgetUsed = 5000;
    allCollision.initialGuessesTried = 8;
    allCollision.filteredSolutions = {{"ik-solution#1", SearchFilterReason::Collision},
                                      {"ik-solution#2", SearchFilterReason::Collision}};

    EXPECT_EQ(allCollision.searchBudgetUsed, static_cast<std::uint64_t>(5000));
    EXPECT_EQ(allCollision.initialGuessesTried, static_cast<std::uint64_t>(8));
    ASSERT_EQ(allCollision.filteredSolutions.size(), static_cast<std::size_t>(2));
    for (const FilteredSolution& f : allCollision.filteredSolutions) {
        EXPECT_EQ(f.filterReason, SearchFilterReason::Collision);
    }

    // EV-VER-2 数据形态：多初值全发散（无解可过滤——清单为空合法）。
    SearchExhaustedRecord diverged;
    diverged.searchBudgetUsed = 1200;
    diverged.initialGuessesTried = 4;
    EXPECT_TRUE(diverged.filteredSolutions.empty());

    // 值语义：记录相等按成员比较（含过滤原因逐值）。
    SearchExhaustedRecord copy = allCollision;
    EXPECT_EQ(copy, allCollision);
    copy.filteredSolutions[0].filterReason = SearchFilterReason::Residual;
    EXPECT_NE(copy, allCollision);
}
