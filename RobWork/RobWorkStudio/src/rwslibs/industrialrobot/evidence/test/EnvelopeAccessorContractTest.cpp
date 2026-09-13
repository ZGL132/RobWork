/**
 * @file   EnvelopeAccessorContractTest.cpp
 * @brief  包络访问器特化契约用例组（EV-T11 产物，跨单元契约面）——把
 *         evidence::ResultEnvelope 喂入 testkit 泛型谓词
 *         checkEnvelopeCombination（testkit.md §5.5/§10.2 evidence 行交接
 *         的承接），与 evidence 自有校验器 validateCombination 双面对账。
 *
 * 设计依据：
 *   - units/testkit.md §5.5（checkEnvelopeCombination 签名与访问器五面：
 *     outcome/engineeringStatus/hasEvidenceList/hasMissingItemsList/
 *     hasFormalConclusion）、§10.2 evidence 行（"须自行提供：envelope
 *     访问器特化（checkEnvelopeCombination 消费）"）、§8.1 表 3
 *   - units/evidence.md §7.1（表 3 合法组合矩阵——访问器必须如实投影
 *     同一规则面）、§12 EV-T11 行（"envelope 访问器特化与切片契约夹具
 *     数据集（_contract_test）"）、§13 testkit 行
 *   - 职责边界（testkit/ContractCheck.hpp 头注释原文）：testkit 不依赖
 *     evidence 等产品单元——evidence 契约测试在自己的 _contract_test
 *     目标内将其类型喂入泛型断言（本文件即该承接的落点）
 *   - 任务契约 tasks/foundation/EV-T11.json acceptance 4（数据集/访问器
 *     特化按 §12 产物行交付）
 *
 * 双面对账说明（本文件的方法论）：testkit 泛型谓词是"包络组合规则的
 * 独立实现"（testkit 侧冻结），evidence 的 validateCombination 是产品侧
 * 权威校验器——两者消费同一事实（ResultEnvelope/Draft 的三轴与载荷面）
 * 必须同判：合法组合"双过"、非法组合"双拒"。若访问器投影失真（如把
 * 空清单投影为"存在"），双面会在同一输入上分叉——本文件逐格钉住。
 *
 * 替身边界声明（EV-REG-3 同源纪律）：本文件的 AcceptAllClosureSource/
 * ScriptedProducerRegistry 为接口替身，仅验证 evidence 构造边界契约
 * （闭包放行、产生者注册查询面），其返回内容不构成任何 project/评估器
 * 侧实现正确性证明；"证明/证据"均为契约形态数据（§11：可控测试替身
 * 只验证 evidence 契约）。
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
#include <sdurws/ird/testkit/ContractCheck.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;

// =====================================================================
// 身份/取值辅助（确定性固定值——与 EnvelopeTest 同款风格，自持不共享）
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

/// 组装标准冻结快照（表 3 快照绑定事实的最小底座）。
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
    AcceptAllClosureSource source;
    return b.build(source);
}

/// 产生者注册表替身：kin-batch-ik@7（validateProof 查询面——快照绑定
/// 重载的"有效证明"核对消费；EV-REG-3 声明见文件头）。
class ScriptedProducerRegistry : public IProducerRegistryView {
public:
    bool isRegistered(std::string_view key) const override { return key == "kin-batch-ik"; }
    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        return key == "kin-batch-ik" && version == 7;
    }
};

// =====================================================================
// 包络底座构造（表 3 各合法行的最小形态——EnvelopeTest 同款风格）
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

/// 表 3 行 1 合法底座（Completed×Feasible——各用例在其上做单点变异）。
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

/// 行 3 合法底座：Canceled×NotApplicable（无正式结论字段、保留诊断与
/// partialData——取消/失败/中断的显式标记形态）。
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
    d.diagnostics = {core::DiagnosticRecord::make(
        std::string{kDiagOutcomeNotCompleted}, std::nullopt, std::nullopt,
        std::nullopt, "verdict-aggregate", "运行被用户取消，无工程判定",
        "查看运行日志确认取消原因")};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return d;
}

/// 合格解析界限证明（对 snapshot/slice/产生者全部绑定正确——快照绑定
/// 重载 validateProof 通过；EngineeringInfeasible 行的凭据）。
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

// =====================================================================
// 访问器特化（本任务的交付物——testkit §10.2 evidence 行交接的承接）
// =====================================================================

/**
 * @brief evidence 结果包络对 testkit 泛型谓词的访问器特化（§10.2 交接
 *        字面——checkEnvelopeCombination<EnvelopeT, Accessors> 的
 *        Accessors 实参）。
 *
 * 五个访问器面（testkit §5.5 访问器集合注释逐一对应——语义投影在
 * evidence 侧，testkit 不依赖本单元类型）：
 *   - outcome/engineeringStatus：三轴之执行轴/判定轴（core 词表——CR-01）
 *     的直读投影；
 *   - hasEvidenceList：表 3"Completed ⇒ 须含证据清单"格的判定面＝清单
 *     非空（EvidenceManifest.items 的有无——清单结构本体在在但空集，
 *     对"含清单"格同样不成立）；
 *   - hasMissingItemsList：表 3"DataInsufficient ⇒ 缺失项全量清单存在"
 *     格的判定面＝missingItems 非空；
 *   - hasFormalConclusion：表 3 行 3"取消/失败/中断不得含正式结论字段"
 *     的判定面＝正式结论载荷在场（DomainPayload——§7.1"正式结论载荷"
 *     家族的机器可判别面；testkit 泛型谓词当前格序不消费本面，特化
 *     仍按 §5.5 五面全量提供——交接契约面完整，谓词演进不破约）。
 *
 * 该特化定义在测试侧（本文件）而非产品头：testkit 的消费契约经由
 * _contract_test 目标兑现（ContractCheck.hpp 头注释的职责边界原文），
 * 产品库不引入对 testkit 的任何依赖（evidence §3.2 依赖红线）。
 */
struct EvidenceEnvelopeAccessors {
    static core::TaskOutcome outcome(const ResultEnvelope& e) { return e.outcome; }
    static core::EngineeringStatus engineeringStatus(const ResultEnvelope& e)
    {
        return e.engineeringStatus;
    }
    static bool hasEvidenceList(const ResultEnvelope& e) { return !e.evidence.items.empty(); }
    static bool hasMissingItemsList(const ResultEnvelope& e) { return !e.missingItems.empty(); }
    static bool hasFormalConclusion(const ResultEnvelope& e) { return e.payload.has_value(); }
};

/**
 * @brief 草稿形态的同面投影（同一五访问器面作用于 ResultEnvelopeDraft
 *        ——字段名一一对应的结构，§7.1"字段与 ResultEnvelope 一一对应"；
 *        供"非法组合双拒"对账使用：非法草稿经 make() 即抛，无法以
 *        ResultEnvelope 形态进入谓词，草稿面是非法格的唯一可喂入形态）。
 */
struct DraftEnvelopeAccessors {
    static core::TaskOutcome outcome(const ResultEnvelopeDraft& d) { return d.outcome; }
    static core::EngineeringStatus engineeringStatus(const ResultEnvelopeDraft& d)
    {
        return d.engineeringStatus;
    }
    static bool hasEvidenceList(const ResultEnvelopeDraft& d)
    {
        return !d.evidence.items.empty();
    }
    static bool hasMissingItemsList(const ResultEnvelopeDraft& d)
    {
        return !d.missingItems.empty();
    }
    static bool hasFormalConclusion(const ResultEnvelopeDraft& d)
    {
        return d.payload.has_value();
    }
};

}  // namespace

// =====================================================================
// 合法组合（表 3 逐行）——泛型谓词与产品校验器双过
// =====================================================================

/** 表 3 合法行逐一构造（make() 通过）→checkEnvelopeCombination 判过
 *  （访问器特化的正向对账——四行覆盖四轴格面）。 */
TEST(EnvelopeAccessorContract, LegalCombinationsPassGenericPredicate)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();

    // 行：Completed×Feasible（载荷在场——⑤级通过的正式结论形态）。
    const ResultEnvelope feasible
        = ResultEnvelope::make(legalFeasibleDraft(snapshot));
    // 行：Completed×EngineeringInfeasible（解析界限证明在场——凭据面）。
    ResultEnvelopeDraft infeasibleDraft = legalFeasibleDraft(snapshot);
    infeasibleDraft.engineeringStatus = core::EngineeringStatus::EngineeringInfeasible;
    infeasibleDraft.payload = std::nullopt;
    infeasibleDraft.infeasibilityProof
        = validAnalyticProof(snapshot, infeasibleDraft.sliceId);
    const ResultEnvelope infeasible = ResultEnvelope::make(std::move(infeasibleDraft));
    // 行：Completed×DataInsufficient（缺失项全量清单非空——④级形态）。
    ResultEnvelopeDraft insufficientDraft = legalFeasibleDraft(snapshot);
    insufficientDraft.engineeringStatus = core::EngineeringStatus::DataInsufficient;
    insufficientDraft.payload = std::nullopt;
    insufficientDraft.missingItems = {{"kin.reach-per-task-point",
                                       "可达性证据缺失（采样预算耗尽）"}};
    const ResultEnvelope insufficient = ResultEnvelope::make(std::move(insufficientDraft));
    // 行：Canceled×NotApplicable（诊断性部分数据——显式标记形态）。
    const ResultEnvelope canceled = ResultEnvelope::make(legalCanceledDraft());

    // 泛型谓词逐行判过（testkit §8.1 表 3 独立实现与产品 make 边界同判）。
    EXPECT_TRUE(tk::checkEnvelopeCombination(feasible, EvidenceEnvelopeAccessors{}).passed);
    EXPECT_TRUE(tk::checkEnvelopeCombination(infeasible, EvidenceEnvelopeAccessors{}).passed);
    EXPECT_TRUE(
        tk::checkEnvelopeCombination(insufficient, EvidenceEnvelopeAccessors{}).passed);
    EXPECT_TRUE(tk::checkEnvelopeCombination(canceled, EvidenceEnvelopeAccessors{}).passed);

    // 双面对账：产品权威校验器对同一批合法包络上下文无关面零问题。
    EXPECT_TRUE(validateCombination(legalFeasibleDraft(snapshot)).empty());
}

/** DataInsufficient 行的缺省清单格（行 1"空缺失清单→拒绝"）：泛型谓词
 *  在草稿面（非法形态无法经 make() 存活）与 validateCombination 双拒
 *  ——访问器投影"清单存在"面失真时此格率先分叉。 */
TEST(EnvelopeAccessorContract, DataInsufficientWithoutMissingListRejectedBothFaces)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.engineeringStatus = core::EngineeringStatus::DataInsufficient;
    draft.payload = std::nullopt;
    draft.missingItems = {};   // 空缺失清单（表 3 行 1 拒绝格）

    // 泛型谓词（草稿面投影）判拒。
    const tk::CheckResult predicate
        = tk::checkEnvelopeCombination(draft, DraftEnvelopeAccessors{});
    EXPECT_FALSE(predicate.passed);
    ASSERT_GE(predicate.failures.size(), 1u);
    EXPECT_NE(predicate.failures[0].fieldPath.find("missingItems"), std::string::npos)
        << "违例描述应定位缺失项清单（表 3 格面），实际: "
        << predicate.failures[0].fieldPath;

    // 双面对账：产品校验器同判拒绝（DataInsufficientNoMissingItems 格）。
    const std::vector<EnvelopeIssue> issues = validateCombination(draft);
    ASSERT_FALSE(issues.empty());
    bool found = false;
    for (const auto& issue : issues) {
        if (issue.code == EnvelopeIssueCode::DataInsufficientNoMissingItems) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "产品校验器应命中 DataInsufficientNoMissingItems 格";
}

/** 行 3 反例格（Canceled×Feasible——不伪造判定，ERR-01）：泛型谓词在
 *  草稿面判拒，产品校验器命中 NonCompletedStatusNotNotApplicable 格。 */
TEST(EnvelopeAccessorContract, CanceledFeasibleRejectedBothFaces)
{
    ResultEnvelopeDraft draft = legalCanceledDraft();
    draft.engineeringStatus = core::EngineeringStatus::Feasible;  // 伪造判定

    const tk::CheckResult predicate
        = tk::checkEnvelopeCombination(draft, DraftEnvelopeAccessors{});
    EXPECT_FALSE(predicate.passed);
    ASSERT_GE(predicate.failures.size(), 1u);
    EXPECT_NE(predicate.failures[0].fieldPath.find("engineeringStatus"),
              std::string::npos)
        << "违例描述应定位工程判定轴（表 3 行 3），实际: "
        << predicate.failures[0].fieldPath;

    const std::vector<EnvelopeIssue> issues = validateCombination(draft);
    ASSERT_FALSE(issues.empty());
    bool found = false;
    for (const auto& issue : issues) {
        if (issue.code == EnvelopeIssueCode::NonCompletedStatusNotNotApplicable) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "产品校验器应命中 NonCompletedStatusNotNotApplicable 格";
}

/** Completed 空证据清单格（行 1"须含证据清单"）——谓词单面筛查：
 *  checkEnvelopeCombination 以 hasEvidenceList（清单非空投影）判拒；
 *  产品构造边界在该格的分工面是**绑定结构**（isManifestTripleValid——
 *  三元组有效即通过 make()），"Feasible 而无任何证据"的语义拦截归汇总
 *  ④级完备性核对（checkEvidenceCompleteness——必需项全 Missing）。两
 *  面分工如实登记（本用例不做"双拒"伪对账）：谓词面是消费侧快速筛查
 *  （testkit §5.5 TK-CTR 的规则子集），产品权威面在 make()＋汇总层。 */
TEST(EnvelopeAccessorContract, CompletedWithEmptyEvidenceListFlaggedByPredicate)
{
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    ResultEnvelopeDraft draft = legalFeasibleDraft(snapshot);
    draft.evidence.items = {};   // 清单空集（"含证据清单"格不成立）

    // 谓词面（草稿投影）：判拒＋违例定位 evidenceList（表 3 行 1 格面）。
    const tk::CheckResult predicate
        = tk::checkEnvelopeCombination(draft, DraftEnvelopeAccessors{});
    EXPECT_FALSE(predicate.passed);
    ASSERT_GE(predicate.failures.size(), 1u);
    EXPECT_NE(predicate.failures[0].fieldPath.find("evidenceList"), std::string::npos)
        << "违例描述应定位证据清单（表 3 行 1），实际: "
        << predicate.failures[0].fieldPath;

    // 产品面（同格的分工面）：绑定三元组结构有效时构造边界不在此格拒绝
    // （上下文无关问题清单为空）——该结果的不可信性由汇总层完备性核对
    // 兜底（登记于 Verdict.hpp aggregateVerdict ④级；此处仅钉住两面的
    // 分工边界，防止未来把"谓词过"误读为"产品校验器过"）。
    EXPECT_TRUE(validateCombination(draft).empty());
}
