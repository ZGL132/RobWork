/**
 * @file   ScriptedResultSource.hpp
 * @brief  ScriptedResultSource——归档结果注入源（IReportResultSource）的具名
 *         脚本化替身（§10 可控替身四具名之一；RPT-T11 落位——本头为本任务
 *         从 RPT-T05/T10 各测试文件局部夹具收敛的唯一正本，局部副本随
 *         RPT-T11 拆除）。
 *
 * 设计依据：
 *   - units/reporting.md §10（可控替身清单原文："ScriptedResultSource（按
 *     脚本返回 envelope/当前性——含替身 envelope 构造经 evidence
 *     ResultEnvelope::make，合法组合-only）"）、§9.1（IReportResultSource
 *     契约四方法——替身与真实提供方同一契约，§9.2 后置行）、§11 RPT-T11
 *     行（产物列原文——四具名替身＋RP-* 用例体；涉及文件 reporting/test/*）
 *   - units/testkit.md §2.4（T-1 允许依赖形态——替身头仅被测试目标包含）、
 *     §7.4（与产品报告的边界——替身输出不构成业务正确性证明）
 *   - 任务契约 tasks/foundation/RPT-T11.json acceptance 2/3/5
 *
 * 替身边界声明（§10.1 RP-STATE-4／任务约束§八——四具名替身共用，全文亦
 *   登记于 test/README.md，此处为具名正本之一）：
 *   本替身输出的 envelope/当前性/资格/复现块**仅验证 reporting 侧契约**
 *   （绑定校验/状态呈现/一致性/幂等——§10 替身边界声明原文），**不构成
 *   任何运动学/轨迹/动力学/选型结果的业务正确性证明**（EV-REG-3 同源）。
 *   三个不可逾越的构造纪律：
 *   ①替身 envelope 一律经 evidence ResultEnvelope::make 构造（合法组合
 *     -only）——非法组合在 evidence 构造边界即被拒绝，reporting 不自造
 *     非法样本（§10 替身边界声明原文；P-RPT-9：envelope 契约以 evidence
 *     v0.1 Draft 为基线，冻结出 diff 后按影响面同步本替身脚本面）；
 *   ②唯一例外＝Preview 包络（§8.1 表 1/表 3 行 4——evidence 构造边界本就
 *     拒绝，无法经 make() 产出），仅供 RP-MDL-2"Preview 结果到达注入面"
 *     的第二道闸用例以聚合初始化受控构造，用例内显式注明（RPT-T05 先例
 *     形态；不构成对 evidence 校验器的替代验证）；
 *   ③脚本数值（评估键/载荷字节/版本号）为确定性固定值——禁随机，同脚本
 *     同 contentIdentity（RP-MDL-1 身份确定性断言的前提）。
 *
 * 与真实提供方的同一契约（§9.1/§9.2 后置行）：tryEnvelope 对同 runId 幂等
 *   （一致读取视图）；currentnessOf 纯投影（不写回归档）；四方法并发只读
 *   安全（测试单线程使用——并发用例仅经注册表 find()，见类注）。
 *
 * 线程约束：单线程使用（被测构建器会话单线程——§9.1；调用计数为 mutable
 *   观测面，产品代码无此形态）。
 */

#ifndef SDURWS_IRD_REPORTING_TEST_SCRIPTEDRESULTSOURCE_HPP
#define SDURWS_IRD_REPORTING_TEST_SCRIPTEDRESULTSOURCE_HPP

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>  // FAIL()——脚本违约 fail-fast 断言面（仅测试目标编译）

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/reporting/Builder.hpp>

namespace sdurws::ird::reporting::test_fakes {

// =====================================================================
// 脚本值工厂（合法组合-only 的唯一构造通道——替身边界声明①）
// =====================================================================

/// 铺位式 16 字节 Id128 夹具（确定性固定值——同 seed 同值，禁随机）。
template <typename T> T scriptedId(std::uint8_t seed)
{
    T id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

/// 64 hex 字符的 ContentIdentity（铺位 seed——身份链固定值；规范词形
/// "cid-<64 小写 hex>"，core 严格解析——不足 64 位即 CoreError）。
inline core::ContentIdentity scriptedCid(std::uint8_t seed)
{
    std::string canonical = "cid-";
    for (int i = 0; i < 64; ++i) {
        // 逐位铺位（seed+i 低 4 位）——同 seed 同值、异 seed 异值、位内
        // 不全同（非退化身份）。
        char hex[2];
        const unsigned nibble = (static_cast<unsigned>(seed) + static_cast<unsigned>(i)) & 0xFu;
        hex[0] = static_cast<char>("0123456789abcdef"[nibble]);
        hex[1] = '\0';
        canonical += hex;
    }
    return core::ContentIdentity::fromCanonical(canonical);
}

/// 包络绑定面三元组（snapshot/slice/baseline——与报告 resultRefs 同源的
/// 追溯链一致面；RP-TRACE-1 逐项核对的基准值）。
struct ScriptedBindingIds {
    core::ContentIdentity snapshotId = scriptedCid(0x10);
    core::ContentIdentity sliceId = scriptedCid(0x30);
    core::ContentIdentity inputBaselineId = scriptedCid(0x50);
};

/// 结构有效的证据清单（三元组与包络同名绑定一致；items 缺省＝一条
/// Satisfied 项——presence 纪律合格；调用方可追加五态项验证 O-13 词表）。
inline evidence::EvidenceManifest scriptedManifest(const ScriptedBindingIds& ids)
{
    evidence::EvidenceManifest m;
    m.snapshotId = ids.snapshotId;
    m.sliceId = ids.sliceId;
    m.profileId = "kin";
    m.profileVersion = "1.0.0";
    m.profileContentIdentity = scriptedCid(0x60);
    evidence::EvidenceItem ok;
    ok.itemId = "kin.reach-per-task-point";
    ok.status = evidence::EvidenceItemStatus::Satisfied;
    ok.artifactDigest = scriptedCid(0x10).bytes;   // Digest256＝ContentIdentity 字节面
    m.items = {ok};
    return m;
}

/// 合法任务五元组（project/branch/revision 固定、run/attempt 由参数区分）。
inline core::TaskIdentity scriptedTask(const core::RunId& run)
{
    core::TaskIdentity t;
    t.project = scriptedId<core::ProjectId>(0x11);
    t.branch = scriptedId<core::BranchId>(0x12);
    t.revision = scriptedId<core::RevisionId>(0x13);
    t.run = run;
    t.attempt = core::AttemptId{1};
    return t;
}

/**
 * @brief 表 3 行 1 合法底座：Completed×Feasible（证据清单＋工况标识齐备，
 *        载荷在场——正式结论形态）。
 *
 * 工程判定的其余合法形态（DataInsufficient/EngineeringInfeasible）用
 * dataInsufficientEnvelope/infeasibleEnvelope 工厂——本函数不收纳非
 * Feasible 参数，避免一个全能工厂把表 3 组合矩阵的合法性淹没在开关里。
 *
 * @param run  [in] 运行身份（五元组区分面）
 * @param ids  [in] 绑定面三元组（缺省固定值）
 * @param caseIds [in] 覆盖工况集（缺省单工况——RP-COV-1 用多工况脚本覆写）
 * @return 经 evidence ResultEnvelope::make 校验的正式结果包络
 */
inline evidence::ResultEnvelope
feasibleEnvelope(const core::RunId& run, ScriptedBindingIds ids = {},
                 std::vector<core::ObjectId> caseIds = {})
{
    if (caseIds.empty()) {
        caseIds = {scriptedId<core::ObjectId>(0x21)};
    }
    evidence::ResultEnvelopeDraft d;
    d.task = scriptedTask(run);
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = ids.snapshotId;
    d.sliceId = ids.sliceId;
    d.inputBaselineId = ids.inputBaselineId;
    d.caseScope.caseIds = std::move(caseIds);
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = scriptedCid(0x60);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::Feasible;
    d.evidence = scriptedManifest(ids);
    d.payload = evidence::DomainPayloadDraft{"kin.batch-ik.v1", {0x01, 0x02, 0x03}};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

/**
 * @brief 表 3 行 3 合法底座：outcome∈{Canceled,Failed,Interrupted}×
 *        NotApplicable（无正式结论字段、诊断保留——TASK-02/RP-STATE-1 的
 *        被测形态；ERR-01 不伪造判定）。
 *
 * @param run     [in] 运行身份
 * @param outcome [in] 执行轴词值（仅取消/失败/中断三值合法——Completed 会
 *                在 evidence 构造边界被拒：NonCompleted 行约束，fail-fast）
 * @return 经 make() 校验的非 Completed 包络（诊断含 kDiagOutcomeNotCompleted）
 */
inline evidence::ResultEnvelope
notCompletedEnvelope(const core::RunId& run, core::TaskOutcome outcome)
{
    evidence::ResultEnvelopeDraft d;
    d.task = scriptedTask(run);
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = scriptedCid(0x10);
    d.sliceId = scriptedCid(0x30);
    d.inputBaselineId = scriptedCid(0x50);
    d.outcome = outcome;
    d.engineeringStatus = core::EngineeringStatus::NotApplicable;
    d.partialData = evidence::PartialDataRef{"runs/run-x/partial", false};
    d.diagnostics = {core::DiagnosticRecord::make(
        std::string{evidence::kDiagOutcomeNotCompleted}, std::nullopt, std::nullopt,
        std::nullopt, "verdict-aggregate", "运行未完成（取消/失败/中断），无工程判定",
        "查看运行日志确认原因")};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

/**
 * @brief Completed×DataInsufficient（表 3 行 1：缺失清单必含**全量**——
 *        空清单在构造边界被拒；RP-STATE-2 的被测形态）。
 * @param run       [in] 运行身份
 * @param itemIds   [in] 缺失项全量清单（≥1——make() 边界强制）
 * @param missingAll [in] 逐缺失项的用户可读原因（与 itemIds 等长对齐）
 */
inline evidence::ResultEnvelope
dataInsufficientEnvelope(const core::RunId& run,
                         const std::vector<std::string>& itemIds,
                         const std::vector<std::string>& missingAll)
{
    evidence::ResultEnvelopeDraft d;
    d.task = scriptedTask(run);
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = scriptedCid(0x10);
    d.sliceId = scriptedCid(0x30);
    d.inputBaselineId = scriptedCid(0x50);
    d.caseScope.caseIds = {scriptedId<core::ObjectId>(0x21)};
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = scriptedCid(0x60);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::DataInsufficient;
    d.evidence = scriptedManifest(ScriptedBindingIds{});
    for (std::size_t i = 0; i < itemIds.size(); ++i) {
        evidence::MissingItem item;
        item.itemId = itemIds[i];
        item.reason = i < missingAll.size() ? missingAll[i] : std::string{};
        d.missingItems.push_back(std::move(item));
    }
    d.payload = evidence::DomainPayloadDraft{"kin.batch-ik.v1", {0x01}};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

/**
 * @brief Completed×EngineeringInfeasible＋确定性不可行证明在场（表 3 行 1：
 *        凭据＝证明在场〔上下文无关面〕——"有效"面的快照绑定核对归接纳侧，
 *        本替身构造的上下文无关面合法样本足够呈现层用例；RP-STATE-3）。
 */
inline evidence::ResultEnvelope infeasibleEnvelope(const core::RunId& run)
{
    const ScriptedBindingIds ids;
    evidence::ResultEnvelopeDraft d;
    d.task = scriptedTask(run);
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = ids.snapshotId;
    d.sliceId = ids.sliceId;
    d.inputBaselineId = ids.inputBaselineId;
    d.caseScope.caseIds = {scriptedId<core::ObjectId>(0x21)};
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = scriptedCid(0x60);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::EngineeringInfeasible;
    d.evidence = scriptedManifest(ids);
    // 确定性不可行证明（§6.3 解析界限类——AnalyticBound 类按其条件必填族
    // 逐字段填写：boundExpression＋coverageClaim 规范值＋绑定身份快照/切片
    // ＋产生者评估键。make() 的上下文无关面只查"凭据在场"；字段级"有效"
    // 核对（validateProof）需要快照/注册表事实面，归接纳侧——本替身处
    // reporting 呈现层（RP-STATE-3 措辞断言），合法组合-only 体现为字段
    // 填写与 §6.3 词面一致，不依赖上下文事实面）。
    evidence::DeterministicInfeasibilityProof proof;
    proof.category = evidence::ProofCategory::AnalyticBound;
    proof.claimToken = "kin.reach-beyond-link-sum";
    proof.subject = scriptedId<core::ObjectId>(0x22);
    proof.boundExpression = "目标点距离 > Σ连杆长（解析界限）";
    proof.preconditions = "额定关节限位与连杆长参数（快照绑定输入）";
    proof.coverageClaim = std::string{evidence::kCoverageClaimAllAlternatives};
    proof.snapshotId = ids.snapshotId;
    proof.sliceId = ids.sliceId;
    proof.producer = "kin-batch-ik";
    proof.producerContractVersion = 7;
    d.infeasibilityProof = std::move(proof);
    d.payload = evidence::DomainPayloadDraft{"kin.batch-ik.v1", {0x01}};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

/// 合法复现块（§4.1.2——版本族齐备的最小形态；复现要素随运行编址）。
inline evidence::ReproductionBlock scriptedReproduction()
{
    evidence::ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    return r;
}

/// 资格纯检查脚本值（eligible 位＋未达标原因 token 清单——§7.2 五条件）。
inline evidence::EligibilityCheck scriptedEligibility(bool eligible)
{
    evidence::EligibilityCheck c;
    c.eligible = eligible;
    if (!eligible) {
        c.unmetConditions = {"status-not-feasible"};
    }
    return c;
}

/// 当前性投影脚本值（Current/Superseded/不可判定三形态——§6.3；status=
/// nullopt 即"不可判定计算形态"，消费方不得默认 Current〔P-EV-4〕）。
inline evidence::CurrentnessResult
scriptedCurrentness(std::optional<evidence::CurrentnessStatus> status,
                    std::vector<evidence::InvalidationReason> reasons = {})
{
    evidence::CurrentnessResult r;
    r.status = status;
    if (!status.has_value()) {
        r.unevaluableCause = evidence::UnevaluableCause::UnresolvedDependency;
    }
    r.reasons = std::move(reasons);
    return r;
}

// ---------------------------------------------------------------------
// 覆盖矩阵脚本值（RPT-T15——§6.6 CaseCoverageMatrix 的合法结构形态；
// 替身边界声明同源：矩阵为 evidence 汇总层的**呈现投影源**，脚本值只验证
// reporting 的注入消费与报告级聚合契约，不构成覆盖判定语义的正确性证明
// ——覆盖完备性判定权威在 evidence 汇总门禁②级，§2.1 C-2）
// ---------------------------------------------------------------------

/// 缺省覆盖矩阵的必验工况集身份（全部缺省矩阵同 id＝同基准对账通过——
/// 多结果缺省场景的一致性前提；显式脚本可覆写）。
inline core::ContentIdentity scriptedRequiredCaseSetId()
{
    return scriptedCid(0x80);
}

/**
 * @brief 结构合法的覆盖矩阵脚本值（§6.6 CaseCoverageMatrix——caseId 唯一、
 *        NotApplicable 必填原因；"漏验/全覆盖"两路脚本的构造面，RP-COV-1）。
 *
 * @param caseIds    [in] 工况身份序（矩阵条目序＝调用方提供序——矩阵内
 *                   caseId 唯一由调用方保证，重复即脚本错误 fail-fast）
 * @param statuses   [in] 逐工况执行状态（与 caseIds 等长对齐）
 * @param requiredSet [in] 必验工况集身份（缺省＝scriptedRequiredCaseSetId()
 *                   ——多结果同基准对账通过面；基准不一致用例显式覆写）
 * @return 覆盖矩阵（结构合法性自检——重复 caseId 即脚本违约 FAIL）
 */
inline evidence::CaseCoverageMatrix
scriptedCoverageMatrix(const std::vector<core::ObjectId>& caseIds,
                       const std::vector<evidence::CaseExecutionStatus>& statuses,
                       core::ContentIdentity requiredSet = scriptedRequiredCaseSetId())
{
    // 非返回 void 的辅助函数禁用 FAIL()（携带 return 语义）——脚本违约
    // 以 ADD_FAILURE() 记账＋防御返回空矩阵（用例必因该记账失败，防御值
    // 仅为可编译性服务）。
    if (caseIds.size() != statuses.size()) {
        ADD_FAILURE() << "scriptedCoverageMatrix: caseIds 与 statuses 长度不一致（脚本违约）";
        return evidence::CaseCoverageMatrix{};
    }
    evidence::CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = std::move(requiredSet);
    for (std::size_t i = 0; i < caseIds.size(); ++i) {
        evidence::CaseCoverageEntry entry;
        entry.caseId = caseIds[i];
        entry.status = statuses[i];
        if (entry.status == evidence::CaseExecutionStatus::NotApplicable) {
            // NotApplicable 必填原因（§6.6 约束行——presence 纪律的结构
            // 合法性面；文案为脚本铺位值，RP-COV-1 断言不依赖其内容）。
            entry.notApplicableReason = "该工况对本域评估显式不适用（脚本铺位）";
        }
        // 重复 caseId 即矩阵非法（§6.6"caseId 重复→矩阵非法"）——脚本
        // 违约记账（不产出非法矩阵样本，替身边界声明①同源；该条目跳过
        // ——用例必因 ADD_FAILURE 失败，跳过仅为不扩散非法样本）。
        bool duplicated = false;
        for (const evidence::CaseCoverageEntry& existing : matrix.entries) {
            if (existing.caseId == entry.caseId) {
                ADD_FAILURE() << "scriptedCoverageMatrix: 重复 caseId（§6.6 矩阵非法）";
                duplicated = true;
                break;
            }
        }
        if (duplicated) {
            continue;
        }
        matrix.entries.push_back(std::move(entry));
    }
    return matrix;
}

/// 缺省全覆盖矩阵（按包络 caseScope 全 Executed——"全部已覆盖"的最常用
/// 形态；单点变异纪律：漏验/不适用脚本由用例显式覆写）。
inline evidence::CaseCoverageMatrix defaultCoverageMatrixFor(
    const evidence::ResultEnvelope& envelope)
{
    std::vector<evidence::CaseExecutionStatus> allExecuted(
        envelope.caseScope.caseIds.size(), evidence::CaseExecutionStatus::Executed);
    return scriptedCoverageMatrix(envelope.caseScope.caseIds, allExecuted);
}

// =====================================================================
// 具名替身本体
// =====================================================================

/**
 * @brief 归档结果注入源替身（§9.1 IReportResultSource 四方法逐字实现——
 *        与真实提供方同一契约）。
 *
 * 脚本面：四张按 runId 规范文本索引的脚本表＋缺省行为。缺省行为取
 * "最常用合法形态"（Feasible/Current/正式通过成立/复现块可解析）——测试
 * 单点变异（只覆写关注项）；脚本表显式登记的条目永远优先于缺省。
 *
 * 观测面：逐方法逐 runId 调用计数——"每份报告构建时重查、不缓存跨报告
 * 复用"（§6.2）与"锚定失败结果源零调用"（RP-MDL-2 FakeSink 零调用的
 * 结果源对偶）的机械断言面。
 */
class ScriptedResultSource final : public IReportResultSource {
public:
    // ---- 脚本表（键＝runId 规范文本；显式条目优先于缺省行为） ----

    /// envelope 脚本（缺条目＝解码失败面——tryEnvelope nullopt，§7.2 步③
    /// SourceMissing 触发）。
    std::map<std::string, evidence::ResultEnvelope> envelopes;
    /// 当前性脚本（缺条目＝Current 缺省——单点变异纪律下的最常用形态）。
    std::map<std::string, evidence::CurrentnessResult> currentness;
    /// 资格脚本（缺条目＝formalPass 成立/reviewRecord 不成立缺省）。
    std::map<std::string, ReportEligibilityChecks> eligibility;
    /// 复现块脚本（缺条目＝scriptedReproduction() 缺省）。
    std::map<std::string, evidence::ReproductionBlock> reproductions;
    /// 复现块缺项开关（RP-MDL-2 的"复现要素缺失"注入面——reproduction
    /// 必填，缺项＝SourceMissing，§4.2）。
    bool reproductionAvailable = true;
    /// 覆盖矩阵脚本（RPT-T15——缺条目＝按包络 caseScope 全 Executed 缺省
    /// 〔defaultCoverageMatrixFor〕；"未执行/不适用"等 RP-COV-1 变异由
    /// withCoverageMatrix 显式登记；显式登记 nullopt＝矩阵缺失面
    /// 〔SourceMissing 注入〕）。
    std::map<std::string, std::optional<evidence::CaseCoverageMatrix>> coverageMatrices;

    // ---- 观测面（const 接口内记账故为 mutable——测试桩观测面） ----

    mutable std::map<std::string, int> envelopeCalls;      ///< tryEnvelope 计数
    mutable std::map<std::string, int> currentnessCalls;   ///< currentnessOf 计数
    mutable std::map<std::string, int> eligibilityCalls;   ///< eligibilityOf 计数
    mutable std::map<std::string, int> reproductionCalls;  ///< tryReproduction 计数
    mutable std::map<std::string, int> coverageCalls;      ///< tryCoverageMatrix 计数（RPT-T15）

    /// 登记 envelope 脚本（链式装配辅助——等同 envelopes.emplace）。
    ScriptedResultSource& withEnvelope(const core::RunId& run,
                                       evidence::ResultEnvelope envelope)
    {
        envelopes.emplace(run.toCanonical(), std::move(envelope));
        return *this;
    }

    /// 登记当前性脚本。
    ScriptedResultSource& withCurrentness(const core::RunId& run,
                                          evidence::CurrentnessResult result)
    {
        currentness.emplace(run.toCanonical(), std::move(result));
        return *this;
    }

    /// 登记资格脚本。
    ScriptedResultSource& withEligibility(const core::RunId& run,
                                          ReportEligibilityChecks checks)
    {
        eligibility.emplace(run.toCanonical(), std::move(checks));
        return *this;
    }

    /// 登记覆盖矩阵脚本（RPT-T15——std::nullopt 即"矩阵缺失"注入面，
    /// RP-COV-1 数据源缺位拒绝路径的触发器）。
    ScriptedResultSource& withCoverageMatrix(
        const core::RunId& run, std::optional<evidence::CaseCoverageMatrix> matrix)
    {
        coverageMatrices.emplace(run.toCanonical(), std::move(matrix));
        return *this;
    }

    // ---- IReportResultSource（§9.1 契约逐方法） ----

    std::optional<evidence::ResultEnvelope> tryEnvelope(core::RunId run) const override
    {
        ++envelopeCalls[run.toCanonical()];
        const auto it = envelopes.find(run.toCanonical());
        if (it == envelopes.end()) {
            return std::nullopt;   // 解码失败面（不抛——try* 可恢复查询路径）
        }
        return it->second;   // 同 runId 幂等（底层数据不可变——§7.1 一致读取视图）
    }

    evidence::CurrentnessResult currentnessOf(const evidence::ResultEnvelope& envelope,
                                              core::RevisionId) const override
    {
        ++currentnessCalls[envelope.task.run.toCanonical()];
        const auto it = currentness.find(envelope.task.run.toCanonical());
        // 缺省＝Current（单点变异：只覆写关注非 Current 形态的用例）。
        return it != currentness.end() ? it->second
                                       : scriptedCurrentness(
                                           evidence::CurrentnessStatus::Current);
    }

    ReportEligibilityChecks eligibilityOf(const evidence::ResultEnvelope& envelope) const override
    {
        ++eligibilityCalls[envelope.task.run.toCanonical()];
        const auto it = eligibility.find(envelope.task.run.toCanonical());
        if (it != eligibility.end()) {
            return it->second;
        }
        // 缺省＝正式通过成立／评审记录不成立（B 级 Feasible 报告的最常用
        // 组合——单点变异纪律）。
        ReportEligibilityChecks def;
        def.formalPass = scriptedEligibility(true);
        def.reviewRecord = scriptedEligibility(false);
        return def;
    }

    std::optional<evidence::ReproductionBlock> tryReproduction(core::RunId run) const override
    {
        ++reproductionCalls[run.toCanonical()];
        if (!reproductionAvailable) {
            return std::nullopt;   // 复现要素缺失（§4.2 必填——SourceMissing 面）
        }
        const auto it = reproductions.find(run.toCanonical());
        return it != reproductions.end() ? std::optional{it->second}
                                         : std::optional{scriptedReproduction()};
    }

    std::optional<evidence::CaseCoverageMatrix> tryCoverageMatrix(
        core::RunId run) const override
    {
        ++coverageCalls[run.toCanonical()];
        const auto it = coverageMatrices.find(run.toCanonical());
        if (it != coverageMatrices.end()) {
            return it->second;   // 显式脚本（含 nullopt＝矩阵缺失注入面）
        }
        // 缺省＝全覆盖矩阵——按该运行的包络 caseScope 全 Executed（单点
        // 变异纪律：B 级既有场景零改写持续覆盖通过面；漏验/不适用变异由
        // withCoverageMatrix 显式登记——RP-COV-1）。包络不在脚本表＝本
        // 方法不会被构建器在 tryEnvelope 成功之前调用（步③顺序），防御
        // 面直接给 nullopt（不伪造矩阵）。
        const auto envelopeIt = envelopes.find(run.toCanonical());
        if (envelopeIt == envelopes.end()) {
            return std::nullopt;
        }
        return defaultCoverageMatrixFor(envelopeIt->second);
    }
};

}  // namespace sdurws::ird::reporting::test_fakes

#endif  // SDURWS_IRD_REPORTING_TEST_SCRIPTEDRESULTSOURCE_HPP
