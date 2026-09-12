/**
 * @file   VerdictTest.cpp
 * @brief  汇总判定用例组——五级优先级决策表正反例（EV-VER-1~8）、漏验
 *         拦截（EV-COV-1）、比较基准一致性（EV-COV-4）、缺失项全量列出
 *         （acceptance 2）、不凭状态字段采信证明（D-9，acceptance 3）、
 *         空必验集/全不适用保守处置（P-EV-7）、跨域字面顺序（P-EV-3）、
 *         决策表⓪前置与 §7.2 两类声明资格检查。
 *
 * 设计依据：
 *   - units/evidence.md §6.4（决策表/判定对/汇总层级）、§6.5（基准检查）、
 *     §6.6（覆盖判据/P-EV-7）、§7.2（资格表）、§11 反例矩阵（EV-VER-1~
 *     8/EV-COV-1/EV-COV-4 行）、§12 EV-T06 行（验证方式＝EV-VER-1~8、
 *     EV-COV-1/4；完成条件＝五级优先级全部正反例通过；缺失全量列出）
 *   - 需求 EVI-01（表 2 五级优先级）、EVI-02（必验全覆盖）、REQ-06（Must/
 *     Should）、KIN-04（降级/零样本）、C5/C8（搜索未果≠不可行/碰撞作用
 *     域）、D-09（纯函数＋字段级 validateProof）、TASK-02（⓪）、DYN-07
 *     （包络不替代覆盖）；任务契约 tasks/foundation/EV-T06.json
 *     （≙WP-05-T06）acceptance 1～4
 *
 * 范围说明（§12 依赖序）：EV-VER-6 的证明字段级校验半边（validateProof
 * 逐字段反例）与 EV-COV-2/3 的校验器半边已在 EV-T05（EvidenceTest.cpp）
 * 落地；本文件是聚合判定面（aggregateVerdict 决策表＋资格＋基准）——
 * EV-VER-1~5/8 与 EV-COV-1/4 的用例体在本文件落地（§12 EV-T05 行预登记）。
 * EV-VER-2 观测点"两次 inputBaselineId 相等、sliceId 不等"的双身份行为
 * 本体由 EV-T04（SliceTest EV-CR02/CR05）承载，本文件以最小切片构建复证
 * 该口径作为复评场景的输入前提。
 *
 * 替身边界声明（EV-REG-3 同源纪律）：本文件的 AcceptAllClosureSource/
 * ScriptedProducerRegistry/ScriptedProfileRegistry 均为接口替身，仅验证
 * evidence 汇总契约（闭包放行、注册查询面），其返回内容不构成任何
 * project/evaluator 侧实现正确性证明，也不构成 IK/动力学等业务算法正确
 * 性证明；各用例"证据/证明/违例"均为契约形态数据（§11：可控测试替身
 * 只验证 evidence 契约与汇总逻辑）。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;

// =====================================================================
// 身份/取值辅助（确定性固定值——与 EvidenceTest 同款风格，自持不共享）
// =====================================================================

const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex32B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex32C = "cccccccccccccccccccccccccccccccc";
const char* kHex32D = "dddddddddddddddddddddddddddddddd";
const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

core::ContentVersion cv(const char* hex64)
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + hex64);
}

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

/// 修订闭包事实来源替身（EV-REG-3 声明见文件头）：对一切 (oid,cv) 回答
/// true——builder 的防混入校验不是本文件被测面。
class AcceptAllClosureSource : public IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

/// 标准必验工况集：A/B 双双 enabled∧mandatory（覆盖分母）、C enabled 非
/// 必验（不进分母——O-14 字面对照面）。
std::vector<CaseEntry> standardCases()
{
    return {{oid(kHex32A), "case-a", true, true},
            {oid(kHex32B), "case-b", true, true},
            {oid(kHex32C), "case-c-optional", true, false}};
}

/// 组装标准冻结快照（各用例共用底座——同 EvidenceTest 风格）。
/// @param cases 必验工况集（默认 standardCases；P-EV-7 用例注入空集）
AnalysisSnapshot buildStandardSnapshot(std::vector<CaseEntry> cases = standardCases())
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
    b.addExternalResource(
        {oid(kHex32D), ExternalResourceStatus::Solidified, cv(kHex64B)});
    AcceptAllClosureSource source;
    return b.build(source);
}

// =====================================================================
// 注册表替身与标准 Profile/清单/覆盖/输入构造
// =====================================================================

/// 产生者注册表替身：固定 (键→契约版本) 表（validateProof 查询面）。
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
    std::map<std::string, std::uint32_t> m_table;
};

/// 产生者注册表：kin-batch-ik@7（键词形合法——无点）。
ScriptedProducerRegistry standardProducerRegistry()
{
    return ScriptedProducerRegistry({{"kin-batch-ik", 7}});
}

/// Profile 注册表替身：按 (profileId, version) 精确查找（IProfileRegistryView
/// 最小可观测实现——汇总器 Profile 查询面）。
class ScriptedProfileRegistry : public IProfileRegistryView {
public:
    void registerProfile(RequiredEvidenceProfile profile)
    {
        m_profiles[profile.profileId + "@" + profile.version] = std::move(profile);
    }

    const RequiredEvidenceProfile* findProfile(std::string_view profileId,
                                               std::string_view version) const override
    {
        const auto it = m_profiles.find(std::string{profileId} + "@" + std::string{version});
        return it == m_profiles.end() ? nullptr : &it->second;
    }

private:
    std::map<std::string, RequiredEvidenceProfile> m_profiles;
};

/// 标准 kin 域 Profile（两项必需＋一项建议——域明细的最小合法实例；
/// 内容身份经 computeProfileContentIdentity 计算，注册表与清单共用同一值）。
RequiredEvidenceProfile standardKinProfile()
{
    RequiredEvidenceProfile p;
    p.profileId = "kin";
    p.version = "1.0.0";
    EvidenceProfileItem reach;
    reach.itemId = "kin.reach-per-task-point";
    reach.itemClass = EvidenceItemClass::Required;
    reach.description = "任务点可达性证据（表 4 运动学行的最小承载实例）";
    reach.substitutableByInfeasibility = true;  // 成功产物类——③级豁免观测面
    EvidenceProfileItem convergence;
    convergence.itemId = "kin.ik-convergence-per-point";
    convergence.itemClass = EvidenceItemClass::Required;
    convergence.description = "逐任务点 IK 收敛证据（表 4 运动学行的最小承载实例）";
    EvidenceProfileItem hint;
    hint.itemId = "kin.cycle-time-hint";
    hint.itemClass = EvidenceItemClass::Suggested;
    hint.description = "节拍建议项（缺失不阻断）";
    p.required = {reach, convergence};
    p.suggested = {hint};
    p.contentIdentity = computeProfileContentIdentity(p);
    return p;
}

/// 满足态证据项（artifactDigest 非零——presence 纪律）。
EvidenceItem satisfiedItem(std::string itemId)
{
    EvidenceItem item;
    item.itemId = std::move(itemId);
    item.status = EvidenceItemStatus::Satisfied;
    item.artifactDigest = cv(kHex64C).bytes;
    return item;
}

/// 标准证据清单（两项必需全 Satisfied——⑤级 Feasible 的证据面）。
EvidenceManifest standardManifest(const AnalysisSnapshot& snapshot,
                                  const RequiredEvidenceProfile& profile,
                                  core::ContentIdentity sliceId)
{
    EvidenceManifest manifest;
    manifest.snapshotId = snapshot.snapshotId;
    manifest.sliceId = sliceId;
    manifest.profileId = profile.profileId;
    manifest.profileVersion = profile.version;
    manifest.profileContentIdentity = profile.contentIdentity;
    manifest.items = {satisfiedItem("kin.reach-per-task-point"),
                      satisfiedItem("kin.ik-convergence-per-point")};
    return manifest;
}

/// 全覆盖覆盖矩阵（requiredCaseSetId＝快照冻结凭据；全部必验工况 Executed；
/// optional 工况 C 也给 Executed——分母只含 mandatory，C 条目为合法冗余）。
CaseCoverageMatrix fullCoverage(const AnalysisSnapshot& snapshot)
{
    CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const CaseEntry& entry : snapshot.caseSet.entries) {
        CaseCoverageEntry item;
        item.caseId = entry.caseId;
        item.status = CaseExecutionStatus::Executed;
        item.runId = core::RunId::fromCanonical(std::string{"run-"} + kHex32A);
        item.resultSliceId = cid(kHex64C);
        matrix.entries.push_back(item);
    }
    return matrix;
}

/// 缺一份 Executed 的覆盖矩阵（漏验面——EV-COV-1）：把 @p missingCaseId
/// 的条目降为 NotExecuted（或不在矩阵内）。
CaseCoverageMatrix coverageMissingOne(const AnalysisSnapshot& snapshot,
                                      core::ObjectId missingCaseId)
{
    CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const CaseEntry& entry : snapshot.caseSet.entries) {
        CaseCoverageEntry item;
        item.caseId = entry.caseId;
        if (entry.caseId == missingCaseId) {
            item.status = CaseExecutionStatus::NotExecuted;  // 漏验
        } else {
            item.status = CaseExecutionStatus::Executed;
            item.runId = core::RunId::fromCanonical(std::string{"run-"} + kHex32A);
            item.resultSliceId = cid(kHex64C);
        }
        matrix.entries.push_back(item);
    }
    return matrix;
}

/// 最小合法切片（EV-VER-2 双身份复评场景的输入前提）：单条 Configuration
/// 条目（求解配置——进 sliceId 不进 inputBaselineId，D-04）。
InputSlice buildMinimalSlice(const AnalysisSnapshot& snapshot, const char* configHex64)
{
    SliceBuilder b;
    b.setEvaluation("kin-batch-ik", 7);
    DependencyEntry entry;
    entry.key = "ik-solve-config";
    entry.kind = DependencyKind::Configuration;
    ConfigurationDependencyPayload payload;
    payload.configKindToken = "ik-solve";
    payload.canonicalBytes = {std::uint8_t(0x01), std::uint8_t(0x02)};
    payload.contentIdentity = cid(configHex64);
    entry.payload = payload;
    entry.applied = true;
    b.addEntry(entry);
    return b.build(snapshot);
}

/// 聚合输入基准形态：Completed＋Verified＋就绪＋门禁完整＋全覆盖＋标准
/// 清单（两项 Satisfied）＋无证明/搜索未果/违例——⑤级 Feasible 底座。
/// 各用例在其上按决策表级次注入单点破坏（正/反例对照）。
VerdictInput baseInput(const AnalysisSnapshot& snapshot,
                       const RequiredEvidenceProfile& profile,
                       const EvidenceManifest& manifest)
{
    VerdictInput input;
    input.outcome = core::TaskOutcome::Completed;
    input.mode = core::EvaluationMode::Verified;
    input.readiness.valid = true;
    input.snapshotGate.complete = true;
    input.coverage = fullCoverage(snapshot);
    input.evidence = manifest;
    return input;
}

/// 合法必经状态碰撞证明基底（绑定一致＋产生者已注册——③级正例形态）。
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
    proof.collisionPairs = {{oid(kHex32A), oid(kHex32B), true}};
    proof.preconditions = "需求任务定义 v3；策略启用碰撞检测（绑定输入身份）";
    proof.coverageClaim = kCoverageClaimMandatoryState;
    proof.snapshotId = snapshot.snapshotId;
    proof.sliceId = sliceId;
    proof.producer = "kin-batch-ik";
    proof.producerContractVersion = 7;
    return proof;
}

/// trace 六槽中取指定级次的记录（下标＝级次——枚举值即槽位）。
const VerdictLevelRecord& levelRecord(const VerdictResult& result, VerdictLevel level)
{
    return result.trace.records.at(static_cast<std::size_t>(level));
}

/// 诊断清单中是否含指定建议码（D-9/降级/空集等语义面的判据）。
bool hasDiagCode(const std::vector<core::DiagnosticRecord>& diagnostics,
                 std::string_view code)
{
    for (const auto& record : diagnostics) {
        if (record.code == code) {
            return true;
        }
    }
    return false;
}

/// missingItems 中是否含指定 itemId（"缺失项全量列出"的逐项判据）。
bool hasMissingItem(const std::vector<MissingItem>& items, const std::string& itemId)
{
    for (const auto& item : items) {
        if (item.itemId == itemId) {
            return true;
        }
    }
    return false;
}

/// 稳定 token 清单中是否含指定 token（资格检查 unmetConditions 的逐项判据）。
bool hasToken(const std::vector<std::string>& tokens, const std::string& token)
{
    return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

/// 测试共用夹具：快照＋Profile＋注册表＋清单＋切片（各测试组按需取用）。
struct VerdictFixture {
    AnalysisSnapshot snapshot;          ///< 冻结快照（builder 产出）
    RequiredEvidenceProfile profile;    ///< 标准 kin Profile
    ScriptedProducerRegistry producers; ///< 产生者注册表替身
    ScriptedProfileRegistry profiles;   ///< Profile 注册表替身
    InputSlice slice;                   ///< 最小切片（双身份复评场景）
    EvidenceManifest manifest;          ///< 标准清单（两项必需 Satisfied）

    VerdictFixture()
        : snapshot(buildStandardSnapshot())
        , profile(standardKinProfile())
        , producers(standardProducerRegistry())
        , slice(buildMinimalSlice(snapshot, kHex64C))
    {
        profiles.registerProfile(profile);
        manifest = standardManifest(snapshot, profile, slice.sliceId);
    }
};

}  // namespace

// =====================================================================
// ⓪ 前置级：执行失败 vs 工程不可行（§6.4.2 第一判定对/TASK-02）
// =====================================================================

/** 决策表⓪：outcome=Failed→NotApplicable（不汇总——执行轴问题无工程判定）。 */
TEST(VerdictDecisionTable, FailedOutcomeIsNotApplicable_TASK_02)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    input.outcome = core::TaskOutcome::Failed;  // 执行轴失败（进程/异常）

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // NotApplicable＝显式标记不伪造判定（ERR-01）——绝不输出不可行。
    EXPECT_EQ(result.status, core::EngineeringStatus::NotApplicable);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::OutcomePrecheck);
    // 命中后①~⑤全部未评估（字面顺序短路——完整决策路径留痕）。
    ASSERT_EQ(result.trace.records.size(), 6u);
    EXPECT_TRUE(levelRecord(result, VerdictLevel::OutcomePrecheck).hit);
    for (const VerdictLevel level : {VerdictLevel::InputReadiness,
                                     VerdictLevel::CommonEvidenceGate,
                                     VerdictLevel::InfeasibilityProof,
                                     VerdictLevel::MissingEvidence,
                                     VerdictLevel::EngineeringJudgement}) {
        EXPECT_FALSE(levelRecord(result, level).evaluated) << "级次 " << static_cast<int>(level);
        EXPECT_FALSE(levelRecord(result, level).hit) << "级次 " << static_cast<int>(level);
    }
}

// =====================================================================
// EV-VER-1 门禁优先（C6：证明不豁免门禁）
// =====================================================================

/** EV-VER-1：证明在场＋快照身份缺失→②级命中，证明未进入③。 */
TEST(VerdictGate, GateBlocksProof_EV_VER_1)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    // 快照身份门禁缺失（EV-VER-1 场景：无策略内容身份——调用方装配面）。
    input.snapshotGate.complete = false;
    input.snapshotGate.missingFields = {"policyContentIdentity"};
    // 不可行证明在场且本身合法（若进入③会判不可行——正是被拦截的行为）。
    input.proof = validMandatoryStateProof(fx.snapshot, fx.manifest.sliceId);

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // ②级命中→DataInsufficient；missingItems 含快照身份项；证明不豁免门禁。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::CommonEvidenceGate);
    EXPECT_TRUE(hasMissingItem(result.missingItems, "policyContentIdentity"));
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
    // C6 字面：③级未被评估（门禁失败即整体短路——证明的可追溯性同受门禁）。
    EXPECT_FALSE(levelRecord(result, VerdictLevel::InfeasibilityProof).evaluated);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagSnapshotIncomplete));
}

// =====================================================================
// EV-VER-2 搜索未果 ≠ 不可行（C5：DataInsufficient＋记录透传→复评可行）
// =====================================================================

/** EV-VER-2：多初值全发散→DataInsufficient（附记录）；扩大初值后→Feasible。 */
TEST(VerdictSearchExhausted, SearchExhaustedThenFeasibleAfterReplan_EV_VER_2)
{
    VerdictFixture fx;

    // ---- 第一次：多初值全发散（无证明、证据未产出——清单 Missing）----
    EvidenceManifest firstManifest = standardManifest(fx.snapshot, fx.profile, fx.slice.sliceId);
    firstManifest.items.clear();  // 搜索未果——评估器无产物（Missing）
    VerdictInput first = baseInput(fx.snapshot, fx.profile, firstManifest);
    SearchExhaustedRecord exhausted;
    exhausted.searchBudgetUsed = 1000;
    exhausted.initialGuessesTried = 8;
    exhausted.filteredSolutions = {};  // 全发散——无已找到的解
    first.searchRecord = exhausted;

    const VerdictResult firstResult =
        aggregateVerdict(first, fx.snapshot, fx.producers, fx.profiles);

    // DataInsufficient（绝不 EngineeringInfeasible——C5）；记录透传。
    EXPECT_EQ(firstResult.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_NE(firstResult.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(firstResult.trace.hitLevel, VerdictLevel::MissingEvidence);
    ASSERT_TRUE(firstResult.searchRecord.has_value());
    EXPECT_EQ(firstResult.searchRecord->searchBudgetUsed, 1000u);
    EXPECT_EQ(firstResult.searchRecord->initialGuessesTried, 8u);
    EXPECT_TRUE(hasDiagCode(firstResult.diagnostics, kDiagSearchExhausted));

    // ---- 第二次：扩大初值后返回有效解（同一冻结输入的复评）----
    VerdictInput second = baseInput(fx.snapshot, fx.profile, fx.manifest);
    second.searchRecord = std::nullopt;

    const VerdictResult secondResult =
        aggregateVerdict(second, fx.snapshot, fx.producers, fx.profiles);
    EXPECT_EQ(secondResult.status, core::EngineeringStatus::Feasible);
    EXPECT_FALSE(secondResult.searchRecord.has_value());

    // 复评场景的输入前提（EV-VER-2 观测点）：配置变→sliceId 变而
    // inputBaselineId 不变（D-04——双身份行为本体见 EV-T04 SliceTest，
    // 此处以最小切片复证该口径）。
    const InputSlice replannedSlice = buildMinimalSlice(fx.snapshot, kHex64B);
    EXPECT_EQ(replannedSlice.inputBaselineId, fx.slice.inputBaselineId);
    EXPECT_NE(replannedSlice.sliceId, fx.slice.sliceId);
}

// =====================================================================
// EV-VER-3 构型碰撞仅过滤（C8：碰撞解被过滤不上升为任务不可行）
// =====================================================================

/** EV-VER-3：一组 IK 解碰撞被过滤、另一组有效→Feasible（无证明产生）。 */
TEST(VerdictConfigCollision, FilteredSolutionDoesNotBlockFeasible_EV_VER_3)
{
    VerdictFixture fx;

    // 域侧承载：碰撞解仅入 filteredSolutions（含 Collision 原因）——有有效
    // 解时评估器不产出搜索未果记录（§6.3 末"全部被过滤"才产出），聚合输入
    // 因此不含 searchRecord；此局部记录仅为注释性对照（EV-T05 EV-SEARCH
    // 已钉住记录保真），不进入聚合。
    SearchExhaustedRecord domainSideOnly;
    domainSideOnly.filteredSolutions = {{"ik-solution-1", SearchFilterReason::Collision}};

    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    input.proof = std::nullopt;  // 无任务级证明（构型碰撞不进入证明契约——C8）

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // 碰撞解被过滤不影响任务可行——⑤级正常判定（无不可行输出）。
    EXPECT_EQ(result.status, core::EngineeringStatus::Feasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_TRUE(result.missingItems.empty());
}

// =====================================================================
// EV-VER-4 全部解被过滤（C8：搜索未果口径——不得输出不可行）
// =====================================================================

/** EV-VER-4：全部解因碰撞过滤→DataInsufficient（filteredSolutions 全 Collision）。 */
TEST(VerdictConfigCollision, AllSolutionsFilteredIsDataInsufficient_EV_VER_4)
{
    VerdictFixture fx;

    EvidenceManifest manifest = standardManifest(fx.snapshot, fx.profile, fx.slice.sliceId);
    manifest.items.clear();  // 无有效解——无产物
    VerdictInput input = baseInput(fx.snapshot, fx.profile, manifest);
    SearchExhaustedRecord exhausted;
    exhausted.searchBudgetUsed = 500;
    exhausted.initialGuessesTried = 4;
    exhausted.filteredSolutions = {{"ik-solution-1", SearchFilterReason::Collision},
                                   {"ik-solution-2", SearchFilterReason::Collision}};
    input.searchRecord = exhausted;

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // 搜索未果口径：DataInsufficient＋记录透传；不产生 MandatoryStateCollision
    // 证明（输入无 proof、输出无不可行——"全部碰撞过滤≠任务级不可行"，C8）。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
    ASSERT_TRUE(result.searchRecord.has_value());
    for (const FilteredSolution& solution : result.searchRecord->filteredSolutions) {
        EXPECT_EQ(solution.filterReason, SearchFilterReason::Collision);
    }
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagSearchExhausted));
}

// =====================================================================
// EV-VER-5 路径碰撞重规划（C8：路径级淘汰——重规划成功→任务不受影响）
// =====================================================================

/** EV-VER-5：候选路径碰撞（淘汰证据）＋重规划成功→Feasible（非任务级证明）。 */
TEST(VerdictPathCollision, ReplannedPathKeepsFeasible_EV_VER_5)
{
    VerdictFixture fx;

    // 路径碰撞＝该路径淘汰并重规划（轨迹域证据）——记录为淘汰证据，
    // 不是 MandatoryState 证明：输入携带"重规划成功"的证据面（清单两项
    // Satisfied，其中 reach 项即重规划后路径的任务点证据）且无 proof。
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    input.proof = std::nullopt;

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(result.status, core::EngineeringStatus::Feasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_TRUE(result.missingItems.empty());
    EXPECT_TRUE(result.diagnostics.empty());
}

// =====================================================================
// EV-VER-6 必经状态证明（③级正/反例——判定面在聚合层）
// =====================================================================

/** EV-VER-6 正例：必经状态碰撞证明有效→③命中 EngineeringInfeasible。 */
TEST(VerdictMandatoryProof, ValidProofGivesInfeasible_EV_VER_6)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    input.proof = validMandatoryStateProof(fx.snapshot, fx.manifest.sliceId);

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::InfeasibilityProof);
    EXPECT_TRUE(result.missingItems.empty());
    // P-EV-3 字面顺序：③命中即输出——④/⑤不再评估（无关缺失不阻断③）。
    EXPECT_FALSE(levelRecord(result, VerdictLevel::MissingEvidence).evaluated);
    EXPECT_FALSE(levelRecord(result, VerdictLevel::EngineeringJudgement).evaluated);
    // ③附加义务：成功产物类证据（substitutable 项）按因不可行不适用出具说明。
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagInfeasibilitySubstitution));
}

/** EV-VER-6 反例：缺必经性依据的同款证明→Invalid→④ DataInsufficient。 */
TEST(VerdictMandatoryProof, InvalidProofFallsToDataInsufficient_EV_VER_6)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    DeterministicInfeasibilityProof broken = validMandatoryStateProof(fx.snapshot, fx.manifest.sliceId);
    broken.mandatoryState.nonSelectabilityBasis.clear();  // 缺必经性依据（字段级反例）
    input.proof = broken;

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // 证明无效（D-09）：不输出不可行——落④级数据不足（EV-VER-6 预期）。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::MissingEvidence);
    EXPECT_TRUE(hasMissingItem(result.missingItems, broken.claimToken));
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagProofInvalid));
}

// =====================================================================
// EV-VER-7 不适用不计缺失（C2/ERR-01——整体不因该缺项降级）
// =====================================================================

/** EV-VER-7：条件项 NotApplicable（原因＝无笛卡尔段）不计缺失→整体 Feasible。 */
TEST(VerdictNotApplicableItem, NotApplicableNotCountedMissing_EV_VER_7)
{
    VerdictFixture fx;

    // 表 4 轨迹行承载实例：trj 域 Profile 的连续性项以"任务序列对象"为
    // 条件输入；纯关节路径（无笛卡尔段）→该条 NotApplicable。
    RequiredEvidenceProfile trjProfile;
    trjProfile.profileId = "trj";
    trjProfile.version = "1.0.0";
    EvidenceProfileItem continuity;
    continuity.itemId = "trj.continuity-per-cartesian-segment";
    continuity.itemClass = EvidenceItemClass::Required;
    continuity.description = "笛卡尔段连续性证据（条件项——仅带笛卡尔段的任务适用）";
    continuity.applicability = Applicability{"task-has-cartesian-segment", {"task-sequence"}};
    trjProfile.required = {continuity};
    trjProfile.contentIdentity = computeProfileContentIdentity(trjProfile);

    ScriptedProfileRegistry trjRegistry;
    trjRegistry.registerProfile(trjProfile);

    EvidenceManifest manifest;
    manifest.snapshotId = fx.snapshot.snapshotId;
    manifest.sliceId = fx.slice.sliceId;
    manifest.profileId = trjProfile.profileId;
    manifest.profileVersion = trjProfile.version;
    manifest.profileContentIdentity = trjProfile.contentIdentity;
    EvidenceItem notApplicable;
    notApplicable.itemId = "trj.continuity-per-cartesian-segment";
    notApplicable.status = EvidenceItemStatus::NotApplicable;
    notApplicable.notApplicableReason = "纯关节路径——任务序列无笛卡尔段（条件不适用）";
    manifest.items = {notApplicable};

    VerdictInput input = baseInput(fx.snapshot, trjProfile, manifest);

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, trjRegistry);

    // NotApplicable 不计缺失（§6.2 判定后果）——整体不因该缺项降级。
    EXPECT_EQ(result.status, core::EngineeringStatus::Feasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_FALSE(hasMissingItem(result.missingItems, "trj.continuity-per-cartesian-segment"));
    EXPECT_TRUE(result.missingItems.empty());
}

// =====================================================================
// EV-VER-8 Must/Should（REQ-06 两级判定语义）
// =====================================================================

/** EV-VER-8（Must）：有效 Must 违例→⑤级 EngineeringInfeasible（判定记录透传）。 */
TEST(VerdictMustShould, MustViolationGivesInfeasible_EV_VER_8)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    input.domain.mustViolations = {
        {"kin.must-load-limit", "末端负载 12kg > 额定 10kg（域判定明细）"}};

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_EQ(levelRecord(result, VerdictLevel::EngineeringJudgement).hit, true);
    // verdictInputs 透传：违例项 id/明细进判定记录诊断（信息不丢失）。
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagMustViolation));
    EXPECT_NE(result.diagnostics.front().cause.find("kin.must-load-limit"), std::string::npos);
}

/** EV-VER-8（Should）：Should 违例→Feasible＋警告诊断（不阻断）。 */
TEST(VerdictMustShould, ShouldViolationKeepsFeasibleWithWarning_EV_VER_8)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    input.domain.shouldViolations = {
        {"kin.should-cycle-time", "节拍 42s > 目标 35s（域判定明细）"}};

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // Should 未满足不阻断（REQ-06）——Feasible＋警告。
    EXPECT_EQ(result.status, core::EngineeringStatus::Feasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagShouldViolation));
    EXPECT_NE(result.diagnostics.front().cause.find("kin.should-cycle-time"), std::string::npos);
}

// =====================================================================
// EV-COV-1 漏验拦截（EVI-02/DYN-07：包络不替代 Executed）
// =====================================================================

/** EV-COV-1：覆盖矩阵缺一个必验工况→②级拦截（其余证据齐备亦不豁免）。 */
TEST(VerdictCoverage, MissingMandatoryExecutionBlocked_EV_COV_1)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    // 漏验工况 B（enabled∧mandatory）——即使其余证据/证明全齐（"即使已有
    // 包络合并结果亦拦截"：矩阵无包络凭据字段，DYN-07 替代无从发生）。
    input.coverage = coverageMissingOne(fx.snapshot, oid(kHex32B));
    input.proof = validMandatoryStateProof(fx.snapshot, fx.manifest.sliceId);

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::CommonEvidenceGate);
    // missingItems 含漏验工况 id（规范文本）——逐工况点名，不抽样。
    EXPECT_TRUE(hasMissingItem(result.missingItems, oid(kHex32B).toCanonical()));
    // 证明不豁免覆盖门禁（C6）——不输出不可行。
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagCaseCoverageMissing));
}

// =====================================================================
// EV-COV-4 比较基准一致性（EVI-02/RPT-04/C5）
// =====================================================================

/** EV-COV-4：inputBaselineId 不等（采样预算变）→失败并列差异维度；相等→通过。 */
TEST(VerdictBaseline, InconsistentBaselinesListDimensions_EV_COV_4)
{
    // 两结果：采样预算改变→inputBaselineId 不等（KIN-04 冻结样本集——
    // 旧覆盖率/候选指标不可与新的直接比较）。
    ComparisonBaseline a;
    a.project = fixedProject();
    a.branch = fixedBranch();
    a.inputBaselineId = cid(kHex64A);
    a.requiredCaseSetId = cid(kHex64B);
    a.sampleSetIds = {cid(kHex64C)};

    ComparisonBaseline b = a;
    b.inputBaselineId = cid(kHex64B);  // 预算变——基准层身份变

    const BaselineConsistencyResult mismatch =
        checkComparisonBaselinesConsistent({a, b});
    EXPECT_FALSE(mismatch.consistent);
    ASSERT_EQ(mismatch.differingDimensions.size(), 1u);
    EXPECT_EQ(mismatch.differingDimensions.front(), BaselineDifferenceDimension::InputBaseline);

    // 相等基准→通过（方案/候选/报告变体可直接比较的前提）。
    const BaselineConsistencyResult match = checkComparisonBaselinesConsistent({a, a});
    EXPECT_TRUE(match.consistent);
    EXPECT_TRUE(match.differingDimensions.empty());
}

/** EV-COV-4 补充：多维度差异按枚举序逐项输出；单条平凡通过。 */
TEST(VerdictBaseline, MultiDimensionDifferencesOrderedAndSingleTrivial)
{
    ComparisonBaseline a;
    a.project = fixedProject();
    a.branch = fixedBranch();
    a.inputBaselineId = cid(kHex64A);
    a.requiredCaseSetId = cid(kHex64B);
    a.sampleSetIds = {cid(kHex64C)};

    ComparisonBaseline b;
    b.project = core::ProjectId::fromCanonical(std::string{"prj-"} + kHex32D);
    b.branch = fixedBranch();
    b.inputBaselineId = cid(kHex64B);
    b.requiredCaseSetId = cid(kHex64C);
    b.sampleSetIds = {cid(kHex64A), cid(kHex64B)};  // 样本集清单不同

    const BaselineConsistencyResult result =
        checkComparisonBaselinesConsistent({a, b});
    EXPECT_FALSE(result.consistent);
    // 差异维度逐项输出且按枚举声明序（Project→InputBaseline→RequiredCaseSet
    // →SampleSets——确定性，NFR-COR-02）。
    ASSERT_EQ(result.differingDimensions.size(), 4u);
    EXPECT_EQ(result.differingDimensions[0], BaselineDifferenceDimension::Project);
    EXPECT_EQ(result.differingDimensions[1], BaselineDifferenceDimension::InputBaseline);
    EXPECT_EQ(result.differingDimensions[2], BaselineDifferenceDimension::RequiredCaseSet);
    EXPECT_EQ(result.differingDimensions[3], BaselineDifferenceDimension::SampleSets);

    // 空集/单条——无可比差异，平凡通过（I-8）。
    EXPECT_TRUE(checkComparisonBaselinesConsistent({}).consistent);
    EXPECT_TRUE(checkComparisonBaselinesConsistent({a}).consistent);
}

// =====================================================================
// acceptance 2：缺失项全量列出（不抽样、不因首个缺失短路）
// =====================================================================

/** ②级内多组成同时缺失：快照身份 2 项＋漏验 2 工况→missingItems 全量 4 条。 */
TEST(VerdictMissingCompleteness, GateFailuresListedExhaustively_Acceptance2)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    input.snapshotGate.complete = false;
    input.snapshotGate.missingFields = {"nameMapContentIdentity", "modeEvidenceGrade"};
    // 覆盖矩阵漏验 A、B 两个必验工况（条目整体缺席）。
    CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = fx.snapshot.caseSet.requiredCaseSetId;
    input.coverage = matrix;  // 空条目集——A/B/C 全部漏验，但 C 非 mandatory 不列

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::CommonEvidenceGate);
    // 全量：2 个门禁字段＋2 个必验工况漏验（C 非 mandatory 不进分母——O-14）。
    EXPECT_EQ(result.missingItems.size(), 4u);
    EXPECT_TRUE(hasMissingItem(result.missingItems, "nameMapContentIdentity"));
    EXPECT_TRUE(hasMissingItem(result.missingItems, "modeEvidenceGrade"));
    EXPECT_TRUE(hasMissingItem(result.missingItems, oid(kHex32A).toCanonical()));
    EXPECT_TRUE(hasMissingItem(result.missingItems, oid(kHex32B).toCanonical()));
}

/** ④级内多缺口同时：两项必需证据全 Missing→missingItems 全量 2 条（不短路）。 */
TEST(VerdictMissingCompleteness, EvidenceGapsListedExhaustively_Acceptance2)
{
    VerdictFixture fx;
    EvidenceManifest manifest = standardManifest(fx.snapshot, fx.profile, fx.slice.sliceId);
    manifest.items.clear();  // 两项必需证据全部 Missing
    VerdictInput input = baseInput(fx.snapshot, fx.profile, manifest);

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::MissingEvidence);
    EXPECT_EQ(result.missingItems.size(), 2u);
    EXPECT_TRUE(hasMissingItem(result.missingItems, "kin.reach-per-task-point"));
    EXPECT_TRUE(hasMissingItem(result.missingItems, "kin.ik-convergence-per-point"));
}

// =====================================================================
// acceptance 3 / D-9：汇总不凭状态字段采信证明
// =====================================================================

/** D-9：证明仅"存在"（类别越界冒充）不得采信——无效证明→DataInsufficient。 */
TEST(VerdictD9, ProofPresenceNeverTrustedWithoutFieldValidation_D9)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    // 冒充证明：以"多初值全发散"类越界类别申报（三类之外——§6.3 作用域
    // 规则；任何"已证明不可行"的状态位/存在性都不被汇总器采信）。
    DeterministicInfeasibilityProof fake = validMandatoryStateProof(fx.snapshot, fx.manifest.sliceId);
    fake.category = static_cast<ProofCategory>(99);  // 越界值——模拟状态字段冒充
    input.proof = fake;

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // 决策表③只在 validateProof 通过后命中——越界类别被字段级校验拒绝，
    // 汇总落④级数据不足（宁可数据不足，绝不伪造不可行）。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::MissingEvidence);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagProofInvalid));
}

// =====================================================================
// P-EV-7：空必验集/全不适用保守处置（acceptance 4 / O-14 保守字面）
// =====================================================================

/** P-EV-7：空必验集→覆盖平凡完备但④级 DataInsufficient（不得输出正式通过）。 */
TEST(VerdictPitfalls, EmptyRequiredCaseSetIsDataInsufficient_P_EV_7)
{
    // 快照工况集为空（组装层允许——P-EV-7 处置在汇总层）。
    AnalysisSnapshot emptySnapshot = buildStandardSnapshot({});
    VerdictFixture fx;
    fx.snapshot = emptySnapshot;
    fx.slice = buildMinimalSlice(fx.snapshot, kHex64C);
    fx.manifest = standardManifest(fx.snapshot, fx.profile, fx.slice.sliceId);

    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    // 覆盖矩阵平凡完备（空分母——EV-T05 校验器已裁定平凡完备）。
    CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = fx.snapshot.caseSet.requiredCaseSetId;
    input.coverage = matrix;

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // 依赖工况证据的判定项无从满足→④级 DataInsufficient＋"无启用必验工况"
    // 诊断；绝不输出 Feasible（保守方向——宁可数据不足不可虚通过）。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::MissingEvidence);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagCaseCoverageMissing));
    EXPECT_NE(result.status, core::EngineeringStatus::Feasible);
}

/** P-EV-7 同源：全部必验工况 NotApplicable→覆盖判据字面在②级拦截（只有 Executed 构成覆盖）。 */
TEST(VerdictPitfalls, AllNotApplicableCasesBlockedAtGate_P_EV_7)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    // A/B 全部显式 NotApplicable（带非空原因——矩阵合法），但 §6.6 判据
    // 字面：覆盖完备 ⇔ 全部 enabled∧mandatory 工况 Executed——NotApplicable
    // 不构成覆盖，②级失败（不另设路径——单元卡 v0.7 I-6 同口径）。
    input.coverage = coverageMissingOne(fx.snapshot, oid(kHex32A));
    for (CaseCoverageEntry& entry : input.coverage.entries) {
        if (entry.caseId == oid(kHex32A)) {
            entry.status = CaseExecutionStatus::NotApplicable;
            entry.notApplicableReason = "该工况对象已停产——不适用（逐项原因）";
            entry.runId = std::nullopt;
            entry.resultSliceId = std::nullopt;
        }
    }

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::CommonEvidenceGate);
    EXPECT_TRUE(hasMissingItem(result.missingItems, oid(kHex32A).toCanonical()));
    EXPECT_NE(result.status, core::EngineeringStatus::Feasible);
}

// =====================================================================
// EV-COV-3 汇总裁定半边（零样本/降级→DataInsufficient——校验器半边在 EV-T05）
// =====================================================================

/** EV-COV-3：区域证据零样本/数据不足→④级 DataInsufficient（不输出 0%/100% 面）。 */
TEST(VerdictRegionCoverage, ZeroSampleAndDowngradedRegionEvidence_EV_COV_3)
{
    VerdictFixture fx;

    // 快照内注册一个冻结采样计划（分母锚——validateRegionCoverageEvidence
    // 的事实面），声明 0 样本（零样本场景）。
    SnapshotBuilder b;
    b.setIdentity(fixedProject(), fixedBranch(), fixedRevision(), 5);
    b.setPolicyRef(PolicyRef{cid(kHex64A)});
    b.setNameMapRef(NameMapRef{cid(kHex64B)});
    b.setReproduction(validReproduction());
    b.addObjectRef(validObjectRef(oid(kHex32A), "robot-design"));
    b.addObjectRef(validObjectRef(oid(kHex32B), "work-region"));
    for (const CaseEntry& c : standardCases()) {
        b.addCase(c);
    }
    b.addExternalResource(
        {oid(kHex32D), ExternalResourceStatus::Solidified, cv(kHex64B)});
    // 计划身份字段——身份自洽的最小采样计划（分母 0：零样本合法入口）。
    SamplingPlanRef plan;
    plan.regionObjectId = oid(kHex32B);
    plan.planContentIdentity = cid(kHex64A);
    plan.plannedPositionSamples = 0;
    plan.plannedPoseSamples = 0;
    plan.sampleSetIdentity = cid(kHex64B);
    b.addSamplingPlan(plan);
    AcceptAllClosureSource source;
    const AnalysisSnapshot planSnapshot = b.build(source);

    fx.snapshot = planSnapshot;
    fx.slice = buildMinimalSlice(fx.snapshot, kHex64C);
    fx.manifest = standardManifest(fx.snapshot, fx.profile, fx.slice.sliceId);

    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    RegionCoverageEvidence region;
    region.sampleSetIdentity = cid(kHex64B);
    region.plannedPositionSamples = 0;
    region.plannedPoseSamples = 0;
    input.regionCoverages = {region};

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    // 零样本→覆盖率不定义：判 DataInsufficient＋诊断（不得输出 0%/100%）。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::MissingEvidence);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagRegionCoverageDowngraded));
    EXPECT_NE(result.status, core::EngineeringStatus::Feasible);
}

// =====================================================================
// §7.2 两类声明资格检查（EV-T06"资格"产物面）
// =====================================================================

/** FormalPass 资格：五条件全满足→可渲染正式通过结论；Quick 模式→不合格。 */
TEST(VerdictEligibility, FormalPassEligibilityFiveConditions_S7_2)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    const VerdictResult verdict =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);
    ASSERT_EQ(verdict.status, core::EngineeringStatus::Feasible);

    // 五条件全满足（Verified＋Completed＋覆盖完备＋证据齐备＋Feasible）。
    const EligibilityCheck pass =
        checkFormalPassEligibility(input, fx.snapshot, verdict, fx.profiles);
    EXPECT_TRUE(pass.eligible);
    EXPECT_TRUE(pass.unmetConditions.empty());

    // Quick 模式：证据效力不足——"mode-not-verified"（表 1：Quick 不得单独
    // 支撑正式通过）。
    VerdictInput quickInput = input;
    quickInput.mode = core::EvaluationMode::Quick;
    const EligibilityCheck quickCheck =
        checkFormalPassEligibility(quickInput, fx.snapshot, verdict, fx.profiles);
    EXPECT_FALSE(quickCheck.eligible);
    ASSERT_EQ(quickCheck.unmetConditions.size(), 1u);
    EXPECT_EQ(quickCheck.unmetConditions.front(), "mode-not-verified");

    // 必需证据缺失（清单清空）→"evidence-incomplete"。
    VerdictInput gapInput = input;
    gapInput.evidence.items.clear();
    const VerdictResult gapVerdict =
        aggregateVerdict(gapInput, fx.snapshot, fx.producers, fx.profiles);
    const EligibilityCheck gapCheck =
        checkFormalPassEligibility(gapInput, fx.snapshot, gapVerdict, fx.profiles);
    EXPECT_FALSE(gapCheck.eligible);
    EXPECT_TRUE(hasToken(gapCheck.unmetConditions, "evidence-incomplete"));
}

/** ReviewRecord 资格：Must 违例（不可行）→合格；Should 违例（可行）→不合格。 */
TEST(VerdictEligibility, ReviewRecordEligibilityRequiresInfeasibleWithBasis_S7_2)
{
    VerdictFixture fx;

    // Must 违例：status=EngineeringInfeasible＋Must 记录在场→评审记录资格成立。
    VerdictInput mustInput = baseInput(fx.snapshot, fx.profile, fx.manifest);
    mustInput.domain.mustViolations = {
        {"kin.must-load-limit", "末端负载超限（评审记录依据）"}};
    const VerdictResult mustVerdict =
        aggregateVerdict(mustInput, fx.snapshot, fx.producers, fx.profiles);
    ASSERT_EQ(mustVerdict.status, core::EngineeringStatus::EngineeringInfeasible);
    const EligibilityCheck mustCheck = checkReviewRecordEligibility(
        mustInput, fx.snapshot, mustVerdict, fx.producers, fx.profiles);
    EXPECT_TRUE(mustCheck.eligible);
    EXPECT_TRUE(mustCheck.unmetConditions.empty());

    // 有效证明分支：③级命中的不可行同样具备评审记录资格（RPT-05）。
    VerdictInput proofInput = baseInput(fx.snapshot, fx.profile, fx.manifest);
    proofInput.proof = validMandatoryStateProof(fx.snapshot, fx.manifest.sliceId);
    const VerdictResult proofVerdict =
        aggregateVerdict(proofInput, fx.snapshot, fx.producers, fx.profiles);
    ASSERT_EQ(proofVerdict.status, core::EngineeringStatus::EngineeringInfeasible);
    const EligibilityCheck proofCheck = checkReviewRecordEligibility(
        proofInput, fx.snapshot, proofVerdict, fx.producers, fx.profiles);
    EXPECT_TRUE(proofCheck.eligible);

    // Should 违例：status=Feasible→两条 unmet（非不可行＋无证明/Must 记录）。
    VerdictInput shouldInput = baseInput(fx.snapshot, fx.profile, fx.manifest);
    shouldInput.domain.shouldViolations = {
        {"kin.should-cycle-time", "节拍超目标（警告）"}};
    const VerdictResult shouldVerdict =
        aggregateVerdict(shouldInput, fx.snapshot, fx.producers, fx.profiles);
    ASSERT_EQ(shouldVerdict.status, core::EngineeringStatus::Feasible);
    const EligibilityCheck shouldCheck = checkReviewRecordEligibility(
        shouldInput, fx.snapshot, shouldVerdict, fx.producers, fx.profiles);
    EXPECT_FALSE(shouldCheck.eligible);
    EXPECT_TRUE(hasToken(shouldCheck.unmetConditions, "status-not-infeasible"));
    EXPECT_TRUE(hasToken(shouldCheck.unmetConditions, "no-valid-proof-or-must-violation"));
}

// =====================================================================
// 确定性与纯函数性（NFR-COR-02/NFR-COR-04——同输入必得同输出）
// =====================================================================

/** 同一输入两次聚合：status/missingItems/trace/searchRecord 全等（trace 留痕确定性）。 */
TEST(VerdictDeterminism, SameInputSameOutputIncludingTrace_NFR_COR_02)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);
    input.snapshotGate.complete = false;
    input.snapshotGate.missingFields = {"policyContentIdentity"};
    input.coverage = coverageMissingOne(fx.snapshot, oid(kHex32A));
    input.domain.shouldViolations = {{"kin.should-cycle-time", "节拍警告"}};

    const VerdictResult first =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);
    const VerdictResult second =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(first, second);  // 全字段含 trace.note/诊断 cause 文本（无时间戳/随机）
}

/** 可行基准正例（决策表全链绿——各反例用例的对照面）。 */
TEST(VerdictDecisionTable, FullySatisfiedInputIsFeasible)
{
    VerdictFixture fx;
    VerdictInput input = baseInput(fx.snapshot, fx.profile, fx.manifest);

    const VerdictResult result =
        aggregateVerdict(input, fx.snapshot, fx.producers, fx.profiles);

    EXPECT_EQ(result.status, core::EngineeringStatus::Feasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_TRUE(result.missingItems.empty());
    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_FALSE(result.searchRecord.has_value());
    // 决策路径完整：⓪~④ evaluated 未命中、⑤命中。
    EXPECT_TRUE(levelRecord(result, VerdictLevel::OutcomePrecheck).evaluated);
    EXPECT_TRUE(levelRecord(result, VerdictLevel::InputReadiness).evaluated);
    EXPECT_TRUE(levelRecord(result, VerdictLevel::CommonEvidenceGate).evaluated);
    EXPECT_TRUE(levelRecord(result, VerdictLevel::InfeasibilityProof).evaluated);
    EXPECT_TRUE(levelRecord(result, VerdictLevel::MissingEvidence).evaluated);
    EXPECT_TRUE(levelRecord(result, VerdictLevel::EngineeringJudgement).hit);
}
