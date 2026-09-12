/**
 * @file   EnvelopeTest.cpp
 * @brief  结果包络用例组——表 3 全组合矩阵（EV-ENV-1）、诊断性部分数据
 *         不可复用（EV-ENV-2）、快照绑定面校验（工况子集/证明有效性/
 *         清单绑定）与构造确定性（NFR-COR-02）。
 *
 * 设计依据：
 *   - units/evidence.md §7.1（数据结构与合法组合矩阵〔表 3〕——构造边界
 *     逐条强制）、§7.2（make/validateCombination 纯函数；接纳侧复用同一
 *     校验器）、§11 反例矩阵（EV-ENV-1/EV-ENV-2 行）、§12 EV-T07 行
 *     （验证方式＝EV-ENV-1/2；完成条件＝表 3 全组合矩阵用例通过）
 *   - 需求 TASK-02（取消/失败/中断不得含正式结论字段）、ERR-01
 *     （NotApplicable 显式标记）、EVI-01（表 1：Preview 不产生结果对象）、
 *     CON-04/05（身份与版本绑定面）、CON-02（历史不可改写——包络不可变
 *     纪律的载体面）；任务契约 tasks/foundation/EV-T07.json（≙WP-05-T07）
 *     acceptance 1
 *   - CR-02（载荷摘要唯一经 core::ContentDigester——本文件以手工重算
 *     钉住 make() 计算的载荷摘要）
 *
 * 范围说明（§12 依赖序）：EV-ENV-2 的缓存判定半边（judgeCacheHit →
 * DiagnosticOnly、reasons 含 outcome-not-completed）归 §8.2/EV-T09——
 * 本文件承载其包络侧数据面前提（partialData.reusable 恒 false 由构造
 * 边界强制）；资格判定（RPT-05）已在 EV-T06（VerdictTest §7.2 用例）
 * 落地，本文件不重复。
 *
 * 替身边界声明（EV-REG-3 同源纪律）：本文件的 AcceptAllClosureSource/
 * ScriptedProducerRegistry 均为接口替身，仅验证 evidence 构造边界契约
 * （闭包放行、产生者注册查询面），其返回内容不构成任何 project/评估器
 * 侧实现正确性证明，也不构成 IK/动力学等业务算法正确性证明；各用例的
 * "证明/证据"均为契约形态数据（§11：可控测试替身只验证 evidence 契约）。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;

// =====================================================================
// 身份/取值辅助（确定性固定值——与 VerdictTest 同款风格，自持不共享）
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

/// 合法任务五元组（execution 分配形态——五字段全部 isValid）。
core::TaskIdentity validTask()
{
    core::TaskIdentity t;
    t.project = fixedProject();
    t.branch = fixedBranch();
    t.revision = fixedRevision();
    t.run = core::RunId::fromCanonical(std::string{"run-"} + kHex32D);
    t.attempt = core::AttemptId{2};
    return t;
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

/// 标准必验工况集：A/B 双双 enabled∧mandatory、C enabled 非必验（与
/// VerdictTest 同构——子集核对的"界内/界外"事实面）。
std::vector<CaseEntry> standardCases()
{
    return {{oid(kHex32A), "case-a", true, true},
            {oid(kHex32B), "case-b", true, true},
            {oid(kHex32C), "case-c-optional", true, false}};
}

/// 组装标准冻结快照（各用例共用底座——同 VerdictTest 风格）。
AnalysisSnapshot buildStandardSnapshot()
{
    SnapshotBuilder b;
    b.setIdentity(fixedProject(), fixedBranch(), fixedRevision(), 5);
    b.setPolicyRef(PolicyRef{cid(kHex64A)});
    b.setNameMapRef(NameMapRef{cid(kHex64B)});
    ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    b.setReproduction(r);
    ObjectRefEntry obj;
    obj.objectId = oid(kHex32A);
    obj.contentVersion = cv(kHex64A);
    obj.objectTypeToken = "robot-design";
    obj.digest = obj.contentVersion.bytes;
    b.addObjectRef(obj);
    for (const CaseEntry& c : standardCases()) {
        b.addCase(c);
    }
    b.addExternalResource(
        {oid(kHex32D), ExternalResourceStatus::Solidified, cv(kHex64B)});
    AcceptAllClosureSource source;
    return b.build(source);
}

/// 产生者注册表替身：固定 (键→契约版本) 表（validateProof 查询面——
/// 快照绑定重载的证明有效性核对消费）。
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

// =====================================================================
// 标准草稿构造（表 3 行 1 的合法底座——各用例在其上做单点变异）
// =====================================================================

/// 绑定面结构有效的证据清单（三元组与包络同名绑定一致＋一条 Satisfied 项
/// ——presence 纪律合格：Satisfied 必带产物摘要，§6.2）。
EvidenceManifest legalManifest(const core::ContentIdentity& snapshotId,
                               const core::ContentIdentity& sliceId)
{
    EvidenceManifest m;
    m.snapshotId = snapshotId;
    m.sliceId = sliceId;
    m.profileId = "kin";
    m.profileVersion = "1.0.0";
    m.profileContentIdentity = cid(kHex64B);
    EvidenceItem ok;
    ok.itemId = "kin.reach-per-task-point";
    ok.status = EvidenceItemStatus::Satisfied;
    ok.artifactDigest = cv(kHex64A).bytes;
    m.items = {ok};
    return m;
}

/// 表 3 行 1 合法底座：Completed×Feasible（证据清单＋工况标识齐备、
/// 缺失清单为空、payload 在场——构造应成功且计算载荷摘要）。
ResultEnvelopeDraft legalFeasibleDraft(const AnalysisSnapshot& snapshot)
{
    ResultEnvelopeDraft d;
    d.task = validTask();
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = snapshot.snapshotId;
    d.sliceId = cid(kHex64C);
    d.inputBaselineId = cid(kHex64B);
    d.caseScope.caseIds = {oid(kHex32A)};
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = cid(kHex64B);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::Feasible;
    d.evidence = legalManifest(d.snapshotId, d.sliceId);
    d.payload = DomainPayloadDraft{"kin.batch-ik.v1", {0x01, 0x02, 0x03}};
    d.producer.producedIn = ProducerProcess::MainProcess;
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return d;
}

/// 行 3 合法底座：非 Completed×NotApplicable（无正式结论字段、保留诊断
/// 与 partialData——取消/失败/中断的显式标记形态）。
ResultEnvelopeDraft legalCanceledDraft()
{
    ResultEnvelopeDraft d;
    d.task = validTask();
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = cid(kHex64A);
    d.sliceId = cid(kHex64C);
    d.inputBaselineId = cid(kHex64B);
    d.outcome = core::TaskOutcome::Canceled;
    d.engineeringStatus = core::EngineeringStatus::NotApplicable;
    d.partialData = PartialDataRef{"runs/run-x/partial", false};
    core::DiagnosticRecord diag = core::DiagnosticRecord::make(
        std::string{kDiagOutcomeNotCompleted}, std::nullopt, std::nullopt,
        std::nullopt, "verdict-aggregate", "运行被用户取消，无工程判定",
        "查看运行日志确认取消原因");
    d.diagnostics = {diag};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return d;
}

/// 合格确定性不可行证明（解析界限类——对 snapshot/slice/产生者全部绑定
/// 正确；快照绑定重载的 validateProof 应通过）。
DeterministicInfeasibilityProof validAnalyticProof(const AnalysisSnapshot& snapshot,
                                                   const core::ContentIdentity& sliceId)
{
    DeterministicInfeasibilityProof p;
    p.category = ProofCategory::AnalyticBound;
    p.claimToken = "kin.reach-beyond-link-sum";
    p.subject = oid(kHex32A);
    p.boundExpression = "目标距离 > Σ连杆长（对全部允许选择覆盖）";
    p.preconditions = "标准安装姿态＋额定负载";
    p.coverageClaim = std::string{kCoverageClaimAllAlternatives};
    p.snapshotId = snapshot.snapshotId;
    p.sliceId = sliceId;
    p.producer = "kin-batch-ik";
    p.producerContractVersion = 7;
    return p;
}

/// Must 违例判定记录（EV-T06 ⑤级产出形态——kDiagMustViolation 码面；
/// 表 3 行 1"或 Must 违例记录"的凭据识别对象）。
core::DiagnosticRecord mustViolationRecord()
{
    return core::DiagnosticRecord::make(
        std::string{kDiagMustViolation}, std::nullopt, std::nullopt,
        std::nullopt, "verdict-aggregate",
        "REQ-06 Must 条目 kin.payload-limit 被违例（载荷超限 12%）",
        "调整负载或改选更大额定机型后重评");
}

// =====================================================================
// 断言辅助
// =====================================================================

/// 断言 make() 拒绝该草稿：稳定错误码＋消息含指定字段文本（EV-ENV-1
/// 观测点"错误码逐条对应表 3；Draft 校验失败消息含字段"）。
void expectRejected(ResultEnvelopeDraft draft, const std::string& mustContain)
{
    bool thrown = false;
    try {
        ResultEnvelope::make(std::move(draft));
    } catch (const EvidenceError& e) {
        thrown = true;
        EXPECT_EQ(e.code(), EvidenceErrorCode::EnvelopeIllegalCombination)
            << "非法组合必须走稳定错误码 envelope-illegal-combination";
        const std::string what = e.what();
        EXPECT_NE(what.find(mustContain), std::string::npos)
            << "异常消息应含 \"" << mustContain << "\"，实际: " << what;
    }
    ASSERT_TRUE(thrown) << "非法组合未被拒绝，期望消息含: " << mustContain;
}

/// 断言校验问题清单含指定问题码（表 3 逐格区分——EnvelopeIssueCode
/// 观测面；逐条独立可判别）。
void expectIssue(const std::vector<EnvelopeIssue>& issues, EnvelopeIssueCode code)
{
    bool found = false;
    for (const auto& issue : issues) {
        if (issue.code == code) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "问题清单缺少问题码（枚举值 "
                       << static_cast<int>(code) << "）";
}

}  // namespace

// =====================================================================
// EV-ENV-1 组合矩阵——合法格（逐一构造成功）
// =====================================================================

/** 行 1：Completed×Feasible——构造成功；载荷摘要由 make() 经
 *  core::ContentDigester 计算（CR-02：以手工重算钉住，非申报值）。 */
TEST(EnvelopeMatrix, CompletedFeasibleConstructsAndComputesPayloadDigest_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    const core::ContentIdentity envelopeSliceId = draft.sliceId;

    core::ContentDigester digester;
    const std::vector<std::uint8_t> bytes{0x01, 0x02, 0x03};
    digester.update(bytes.data(), bytes.size());
    const core::Digest256 expectedDigest = digester.finalize();

    const ResultEnvelope envelope = ResultEnvelope::make(std::move(draft));

    // 字段逐一保真（构造不篡改声明值）。
    EXPECT_TRUE(envelope.task.isValid());
    EXPECT_EQ(envelope.evaluationKey, "kin-batch-ik");
    EXPECT_EQ(envelope.mode, core::EvaluationMode::Verified);
    EXPECT_EQ(envelope.snapshotId, snapshot.snapshotId);
    EXPECT_EQ(envelope.sliceId, envelopeSliceId);
    EXPECT_EQ(envelope.outcome, core::TaskOutcome::Completed);
    EXPECT_EQ(envelope.engineeringStatus, core::EngineeringStatus::Feasible);
    ASSERT_TRUE(envelope.payload.has_value());
    EXPECT_EQ(envelope.payload->kindToken, "kin.batch-ik.v1");

    // 载荷摘要＝SHA-256(canonicalBytes)——与手工重算逐字节一致（CR-02
    // 唯一摘要算法；追溯/完整性凭据——§7.1 DomainPayload.digest 注释）。
    EXPECT_EQ(envelope.payload->digest.bytes, expectedDigest);
}

/** 行 1：Completed×EngineeringInfeasible——凭据为合格证明（在场即过
 *  上下文无关面；快照绑定重载 validateProof 通过＝"有效证明"全量面）。 */
TEST(EnvelopeMatrix, CompletedEngineeringInfeasibleWithValidProof_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.engineeringStatus = core::EngineeringStatus::EngineeringInfeasible;
    draft.missingItems.clear();
    draft.payload.reset();  // 纯证明型不可行：结论即证明，不携带成功类载荷
    draft.infeasibilityProof = validAnalyticProof(snapshot, draft.sliceId);

    // 上下文无关面：证明在场即有凭据（"有效"面需快照事实面——两段式）。
    EXPECT_TRUE(validateCombination(draft).empty());

    // 快照绑定面：validateProof 对（快照/切片/注册表）字段级核对通过。
    ScriptedProducerRegistry registry = standardProducerRegistry();
    EXPECT_TRUE(validateCombination(draft, snapshot, registry).empty());

    const ResultEnvelope envelope = ResultEnvelope::make(std::move(draft));
    EXPECT_EQ(envelope.engineeringStatus,
              core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_FALSE(envelope.payload.has_value());
}

/** 行 1：Completed×EngineeringInfeasible——凭据为 Must 违例记录（无证明；
 *  "或 Must 违例记录"半格——识别面＝EV-T06 kDiagMustViolation 码）。 */
TEST(EnvelopeMatrix, CompletedEngineeringInfeasibleWithMustRecordOnly_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.engineeringStatus = core::EngineeringStatus::EngineeringInfeasible;
    draft.missingItems.clear();
    draft.diagnostics = {mustViolationRecord()};

    EXPECT_TRUE(validateCombination(draft).empty());
    const ResultEnvelope envelope = ResultEnvelope::make(std::move(draft));
    EXPECT_EQ(envelope.engineeringStatus,
              core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_FALSE(envelope.infeasibilityProof.has_value());
}

/** 行 1：Completed×DataInsufficient——缺失项**全量**清单在场（计算完成
 *  但证据不满足正式判定＝合法且常见——§7.1"分批数据"段注释）。 */
TEST(EnvelopeMatrix, CompletedDataInsufficientWithFullMissingList_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.engineeringStatus = core::EngineeringStatus::DataInsufficient;
    draft.missingItems = {MissingItem{"kin.ik-convergence-per-point", "无收敛记录"},
                          MissingItem{"common.case-coverage-matrix", "工况 b 未执行"}};

    EXPECT_TRUE(validateCombination(draft).empty());
    const ResultEnvelope envelope = ResultEnvelope::make(std::move(draft));
    EXPECT_EQ(envelope.missingItems.size(), 2u);
}

/** 行 3（EV-ENV-2 包络侧）：Canceled×NotApplicable＋partialData——构造
 *  成功且 reusable 恒 false（部分数据只可读诊断；缓存判定面 judgeCacheHit
 *  →DiagnosticOnly 消费本标记，判定本体归 §8.2/EV-T09）。 */
TEST(EnvelopeMatrix, CanceledDiagnosticOnlyEnvelopeKeepsReusableFalse_EV_ENV_2)
{
    ResultEnvelopeDraft draft = legalCanceledDraft();
    const ResultEnvelope envelope = ResultEnvelope::make(std::move(draft));

    EXPECT_EQ(envelope.outcome, core::TaskOutcome::Canceled);
    EXPECT_EQ(envelope.engineeringStatus, core::EngineeringStatus::NotApplicable);
    ASSERT_TRUE(envelope.partialData.has_value());
    // EV-ENV-2 包络侧观测点：诊断价值保留、正式复用被构造边界锁死。
    EXPECT_FALSE(envelope.partialData->reusable);
    EXPECT_FALSE(envelope.payload.has_value());
    EXPECT_TRUE(envelope.missingItems.empty());
}

/** 行 3 全 outcome 扫描：Failed/Interrupted 与 Canceled 同一显式标记
 *  形态（表 3 行 3 对三个非 Completed 结果一视同仁）。 */
TEST(EnvelopeMatrix, FailedAndInterruptedDiagnosticOnlyConstruct_EV_ENV_1)
{
    for (const core::TaskOutcome outcome :
         {core::TaskOutcome::Failed, core::TaskOutcome::Interrupted}) {
        ResultEnvelopeDraft draft = legalCanceledDraft();
        draft.outcome = outcome;
        const ResultEnvelope envelope = ResultEnvelope::make(std::move(draft));
        EXPECT_EQ(envelope.outcome, outcome);
        EXPECT_EQ(envelope.engineeringStatus,
                  core::EngineeringStatus::NotApplicable);
    }
}

// =====================================================================
// EV-ENV-1 组合矩阵——非法格（逐一构造拒绝；错误码/消息逐格可判别）
// =====================================================================

/** 行 4：Preview 构造边界（表 1：Preview 不产生结果对象）——对合法底座
 *  仅改 mode 即拒绝；取消形态同样拒绝（"任意"行的无条件性）。 */
TEST(EnvelopeMatrix, PreviewRejectedUnconditionally_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft completedPreview = legalFeasibleDraft(snapshot);
    completedPreview.mode = core::EvaluationMode::Preview;
    expectRejected(std::move(completedPreview), "Preview");

    ResultEnvelopeDraft canceledPreview = legalCanceledDraft();
    canceledPreview.mode = core::EvaluationMode::Preview;
    expectRejected(std::move(canceledPreview), "Preview");
}

/** 行 4 检查序确定性：Preview 与多点违例并存时首错恒为 Preview（表 3
 *  行 4 是最外层无条件边界——同坏组装必报同一首错，NFR-COR-02）。 */
TEST(EnvelopeMatrix, PreviewIsFirstErrorAmongMultipleViolations_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.mode = core::EvaluationMode::Preview;
    draft.task = core::TaskIdentity{};              // 叠加：五元组保留值
    draft.engineeringStatus =
        core::EngineeringStatus::NotApplicable;     // 叠加：行 2 非法格

    const std::vector<EnvelopeIssue> issues = validateCombination(draft);
    ASSERT_FALSE(issues.empty());
    EXPECT_EQ(issues.front().code, EnvelopeIssueCode::PreviewModeForbidden);
    expectRejected(std::move(draft), "Preview");
}

/** 行 2：Completed×NotApplicable 拒绝（ERR-01——NotApplicable 只配非
 *  Completed；EV-ENV-1 点名反例面）。 */
TEST(EnvelopeMatrix, CompletedNotApplicableRejected_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.engineeringStatus = core::EngineeringStatus::NotApplicable;
    expectRejected(std::move(draft), "NotApplicable");
}

/** 行 3 反面：Canceled×Feasible 拒绝（EV-ENV-1 点名反例——不伪造工程
 *  判定，ERR-01）。 */
TEST(EnvelopeMatrix, CanceledFeasibleRejected_EV_ENV_1)
{
    ResultEnvelopeDraft draft = legalCanceledDraft();
    draft.engineeringStatus = core::EngineeringStatus::Feasible;
    expectRejected(std::move(draft), "NotApplicable");
}

/** 行 1 反例：Completed+DataInsufficient＋空缺失清单拒绝（EV-ENV-1 点名
 *  反例——"数据不足"必须携带缺失全量清单）。 */
TEST(EnvelopeMatrix, CompletedDataInsufficientWithEmptyMissingRejected_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.engineeringStatus = core::EngineeringStatus::DataInsufficient;
    draft.missingItems.clear();
    expectRejected(std::move(draft), "missingItems");
}

/** 行 1 反例：Completed+Feasible 带未解决缺失项拒绝（"可行"与"有缺"
 *  互斥——缺什么就报 DataInsufficient，不允许带缺通过）。 */
TEST(EnvelopeMatrix, FeasibleWithUnresolvedMissingRejected_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.missingItems = {MissingItem{"kin.reach-per-task-point", "工况 b 不可达"}};
    expectRejected(std::move(draft), "missingItems");
}

/** 行 1 反例：Completed+EngineeringInfeasible 无凭据拒绝（既无证明也无
 *  Must 违例记录——"不可行"必须有依据，防状态字段伪造）。 */
TEST(EnvelopeMatrix, EngineeringInfeasibleWithoutBasisRejected_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.engineeringStatus = core::EngineeringStatus::EngineeringInfeasible;
    expectRejected(std::move(draft), "infeasibilityProof");
}

/** 行 3 反例：Failed＋payload 拒绝（EV-ENV-1 点名反例——取消/失败/中断
 *  不得含正式结论字段，TASK-02）。 */
TEST(EnvelopeMatrix, FailedWithPayloadRejected_EV_ENV_1)
{
    ResultEnvelopeDraft draft = legalCanceledDraft();
    draft.outcome = core::TaskOutcome::Failed;
    draft.payload = DomainPayloadDraft{"kin.batch-ik.v1", {0x01}};
    expectRejected(std::move(draft), "payload");
}

/** 行 3 反例：Canceled 的清单含 Satisfied 判定声明拒绝（"已满足的证据"
 *  是正式结论家族——诊断性保留不等于判定声明）。 */
TEST(EnvelopeMatrix, CanceledWithSatisfiedEvidenceRejected_EV_ENV_1)
{
    ResultEnvelopeDraft draft = legalCanceledDraft();
    EvidenceItem satisfied;
    satisfied.itemId = "kin.reach-per-task-point";
    satisfied.status = EvidenceItemStatus::Satisfied;
    satisfied.artifactDigest = cv(kHex64A).bytes;
    draft.evidence.items = {satisfied};
    expectRejected(std::move(draft), "Satisfied");
}

/** 行 3 反例：Canceled 携带 missingItems 拒绝（缺失清单是工程判定组成
 *  ——非 Completed 行只保留诊断与 partialData）。 */
TEST(EnvelopeMatrix, CanceledWithMissingItemsRejected_EV_ENV_1)
{
    ResultEnvelopeDraft draft = legalCanceledDraft();
    draft.missingItems = {MissingItem{"kin.reach-per-task-point", "未执行"}};
    expectRejected(std::move(draft), "missingItems");
}

/** EV-ENV-2 反例：partialData.reusable==true 拒绝（"恒 false：诊断价值
 *  保留，不得正式复用"——构造边界锁死复用标记）。 */
TEST(EnvelopeMatrix, PartialDataReusableTrueRejected_EV_ENV_2)
{
    ResultEnvelopeDraft draft = legalCanceledDraft();
    draft.partialData->reusable = true;
    expectRejected(std::move(draft), "reusable");
}

/** 结构面：task 五元组存在保留值拒绝（§7.1 注释"make 校验 isValid"——
 *  无法锚定到一次真实运行的包络不得存在）。 */
TEST(EnvelopeMatrix, TaskIdentityInvalidRejected_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.task = core::TaskIdentity{};
    expectRejected(std::move(draft), "task");
}

/** 结构面：评估键词形非法拒绝（isValidEvaluationKey 词形闸门——评估键
 *  进缓存身份面，词形坏则缓存键无歧义性破坏，CON-04）。 */
TEST(EnvelopeMatrix, EvaluationKeyInvalidRejected_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.evaluationKey = "Kin.batch.ik";  // 大写＋点——词表外
    expectRejected(std::move(draft), "evaluationKey");
}

/** 结构面：绑定身份保留值拒绝（snapshotId/sliceId/inputBaselineId 任一
 *  全零＝未绑定——包络无法定位其输入事实，CON-05/D-04）。 */
TEST(EnvelopeMatrix, InputIdentityZeroRejected_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.inputBaselineId = core::ContentIdentity{};
    expectRejected(std::move(draft), "inputBaselineId");
}

/** 行 1 反例：Completed 的清单与包络绑定不一致拒绝（清单指向另一次评估
 *  的快照/切片＝证据张冠李戴——§6.2 记录面）。 */
TEST(EnvelopeMatrix, CompletedManifestNotCoherentRejected_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.evidence = legalManifest(cid(kHex64A), draft.sliceId);  // 快照身份错位
    expectRejected(std::move(draft), "不一致");
}

// =====================================================================
// 快照绑定面（validateCombination 重载——接纳侧复用的同一校验器）
// =====================================================================

/** 工况子集核对（§7.1"caseScope ⊆ 快照 caseSet"）：越界工况逐条点名，
 *  界内范围零问题（上下文无关面无此事实面——两段式边界的直接观测）。 */
TEST(EnvelopeSnapshotBound, CaseScopeOutsideSnapshotReported_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();

    // 界内：全部通过（上下文无关面与快照绑定面皆空）。
    ResultEnvelopeDraft legal = legalFeasibleDraft(snapshot);
    ScriptedProducerRegistry registry = standardProducerRegistry();
    EXPECT_TRUE(validateCombination(legal).empty());
    EXPECT_TRUE(validateCombination(legal, snapshot, registry).empty());

    // 越界：caseD 是快照外部资源 id、不在必验工况集——快照绑定面点名，
    // 上下文无关面不报（结构上 caseScope 非空即可）。
    ResultEnvelopeDraft cross = legalFeasibleDraft(snapshot);
    cross.caseScope.caseIds = {oid(kHex32A), oid(kHex32D)};
    EXPECT_TRUE(validateCombination(cross).empty());
    const std::vector<EnvelopeIssue> bound =
        validateCombination(cross, snapshot, registry);
    expectIssue(bound, EnvelopeIssueCode::CaseScopeNotInSnapshot);
}

/** "有效证明"面（D-09 存在性≠有效性）：仅改产生者契约版本（9≠注册值 7）
 *  ——上下文无关面放行（在场即凭据）、快照绑定面以 validateProof 拒绝
 *  （表 3 行 1"validateProof 通过"的全量面）。 */
TEST(EnvelopeSnapshotBound, InvalidProofRejectedInSnapshotBoundCheck_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.engineeringStatus = core::EngineeringStatus::EngineeringInfeasible;
    draft.infeasibilityProof = validAnalyticProof(snapshot, draft.sliceId);
    draft.infeasibilityProof->producerContractVersion = 9;  // 与注册值 7 不符

    // 上下文无关面：证明在场（有效性核对需要快照/注册表事实面）。
    EXPECT_TRUE(validateCombination(draft).empty());

    // 快照绑定面：validateProof 字段级核对拒绝（CON-04 契约版本不符）。
    ScriptedProducerRegistry registry = standardProducerRegistry();
    const std::vector<EnvelopeIssue> bound =
        validateCombination(draft, snapshot, registry);
    expectIssue(bound, EnvelopeIssueCode::ProofInvalidAgainstSnapshot);
}

/** 清单对快照绑定复核（§6.2"防错误工况/对象引用"——接纳侧纵深防御）：
 *  清单项引用快照必验集外工况，在快照绑定面被点名。 */
TEST(EnvelopeSnapshotBound, ManifestBindingToForeignCaseReported_EV_ENV_1)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    EvidenceItem foreign = draft.evidence.items.front();
    foreign.caseScope = std::vector<CaseId>{oid(kHex32D)};  // 必验集外工况
    draft.evidence.items = {foreign};

    // 上下文无关面：结构纪律全部合格（presence/三元组/一致性）。
    EXPECT_TRUE(validateCombination(draft).empty());

    // 快照绑定面：validateEvidenceManifestBinding 点名错误工况引用。
    ScriptedProducerRegistry registry = standardProducerRegistry();
    const std::vector<EnvelopeIssue> bound =
        validateCombination(draft, snapshot, registry);
    expectIssue(bound, EnvelopeIssueCode::CompletedManifestBindingInvalid);
}

// =====================================================================
// 构造确定性（NFR-COR-02——同草稿必得同输出）
// =====================================================================

/** 同一坏组装两次校验必得同一 issue 序列（码序＋消息文本逐条一致）。 */
TEST(EnvelopeDeterminism, SameDraftSameIssueSequence_NFR_COR_02)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft first = legalFeasibleDraft(snapshot);
    first.mode = core::EvaluationMode::Preview;
    first.engineeringStatus = core::EngineeringStatus::NotApplicable;

    ResultEnvelopeDraft second = legalFeasibleDraft(snapshot);
    second.mode = core::EvaluationMode::Preview;
    second.engineeringStatus = core::EngineeringStatus::NotApplicable;

    const std::vector<EnvelopeIssue> a = validateCombination(first);
    const std::vector<EnvelopeIssue> b = validateCombination(second);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].code, b[i].code);
        EXPECT_EQ(a[i].message, b[i].message);
    }
}

/** 两份相等的合法草稿经 make() 产出逐字段相等的包络（含载荷摘要——
 *  确定性 NFR-COR-02；历史包络逐字节可复算，EV-CUR-3 前提）。 */
TEST(EnvelopeDeterminism, MakeFromEqualDraftsGivesEqualEnvelopes_NFR_COR_02)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    const ResultEnvelope a = ResultEnvelope::make(legalFeasibleDraft(snapshot));
    const ResultEnvelope b = ResultEnvelope::make(legalFeasibleDraft(snapshot));
    EXPECT_EQ(a, b);
}
