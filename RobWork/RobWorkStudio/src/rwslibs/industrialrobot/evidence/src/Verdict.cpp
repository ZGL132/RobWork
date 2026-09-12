/**
 * @file   Verdict.cpp
 * @brief  工程判定汇总的实现——五级决策表（§6.4.1）、§7.2 两类声明资格
 *         检查与 §6.5 比较基准一致性检查。
 *
 * 实现纪律（与 Verdict.hpp 契约注释一一对应，此处只记实现要点）：
 *   - 纯函数、无副作用、无共享可变状态；全部输出（含 trace.note 与诊断
 *     cause 文本）由输入决定——同输入必得同输出（NFR-COR-02/COR-04）。
 *   - 决策表严格按 §6.4.1 字面顺序短路（P-EV-3）；②级门禁与④级数据
 *     不足判定内部**全量收集**不短路（acceptance 2：缺失项全量列出）。
 *   - 证明只信 validateProof（D-09，acceptance 3）：存在性不等于有效性；
 *     校验失败的证明按 Invalid 语义落④级数据不足，绝不输出不可行。
 *   - 覆盖矩阵分母恒取快照冻结态 RequiredCaseSet（acceptance 4 / O-14
 *     保守字面）；空必验集（无 enabled∧mandatory 工况）按 P-EV-7 保守
 *     处置落④级 DataInsufficient。
 *
 * 线程安全：可重入纯函数（无静态可变状态）。
 */

#include <sdurws/ird/evidence/Verdict.hpp>

#include <algorithm>
#include <utility>

namespace sdurws::ird::evidence {

namespace {

// =====================================================================
// 内部辅助：诊断构造与缺失项登记（全部为局部纯函数——无共享状态）
// =====================================================================

/**
 * @brief 构造一条汇总诊断记录（码面＝kDiag* 建议码；码值权威归 diagnostics
 *        StableCodeRegistry——本函数只承载记录本体，不承担注册职责，PA-1）。
 *
 * context 固定 "verdict-aggregate"（诊断来源上下文——ui/reporting 消费时
 * 可据此区分汇总层诊断与评估器层诊断）；localName/runtimeName 置空（名称
 * 反解归消费方经 runtime⑥端口——CON-06，汇总器不拼接名称，R-4）。
 *
 * @param code    [in] 建议码（EVI-* 形态——DiagCode 句法合法）
 * @param subject [in] 主体对象（可空——快照字段级/清单级问题无对象锚）
 * @param cause   [in] 原因描述（人读中文——必须非空，C-3 校验）
 * @param action  [in] 建议动作（人读中文——必须非空，C-3 校验）
 * @return 诊断记录（make() 工厂产出——句法/必填校验通过）
 */
core::DiagnosticRecord makeVerdictDiag(std::string_view code,
                                       std::optional<core::ObjectId> subject,
                                       std::string cause,
                                       std::string action)
{
    return core::DiagnosticRecord::make(std::string{code}, std::move(subject),
                                        std::nullopt, std::nullopt,
                                        "verdict-aggregate", std::move(cause),
                                        std::move(action));
}

/// 证据项五值状态的中文区分文案（④级 missingItems.reason 的状态语义面
/// ——Missing/Invalid/Unverified 判定后果不同：§6.2 表逐值原文）。
std::string_view evidenceGapStatusText(EvidenceItemStatus status)
{
    switch (status) {
    case EvidenceItemStatus::Missing:
        return "必需证据缺失（Missing——无产物）";
    case EvidenceItemStatus::Invalid:
        return "必需证据无效（Invalid——绑定校验失败）";
    case EvidenceItemStatus::Unverified:
        return "必需证据未验证（Unverified——不满足正式判定条件）";
    case EvidenceItemStatus::Satisfied:
    case EvidenceItemStatus::NotApplicable:
        // 不可达：checkEvidenceCompleteness 的 requiredGaps 只含三不满足态
        // （Satisfied＝满足、NotApplicable＝不计缺失——EV-VER-7 判定面）。
        // 防御式文案：若上游契约变化此处显式暴露而非静默。
        return "必需证据状态异常（非不满足态出现在缺口清单——契约违约）";
    }
    return "必需证据状态异常（非不满足态出现在缺口清单——契约违约）";
}

// =====================================================================
// ②级通用证据门禁（§6.4.1 ②——固定检查序全量收集，任一命中即本级命中）
// =====================================================================

/**
 * @brief ②级门禁收集结果（命中标志＋缺失明细＋诊断——aggregateVerdict
 *        消费明细，资格检查只消费命中标志）。
 */
struct CommonGateOutcome {
    bool failed = false;                            ///< 任一组成命中
    std::vector<MissingItem> missingItems;          ///< 缺失/无效明细（全量、检查序）
    std::vector<core::DiagnosticRecord> diagnostics; ///< 各组成一条汇总诊断（检查序）
};

/**
 * @brief ②级通用证据门禁全量收集（§6.4.1 ②——检查序即登记序，固定）。
 *
 * 检查序（I-4，确定性——同坏输入必得同明细序列，NFR-COR-02）：
 *   a. 快照身份门禁（调用方装配的 snapshotGate——含策略/名称映射内容
 *      身份、复现块、模式与证据等级标识的缺失/无效）；
 *   b. Verified 前置固化校验（validateSnapshotForMode——CON-03：Recorded
 *      资源阻断正式结论；复现块完整性对快照值复核）；
 *   c. 清单绑定 Profile 注册核对（findProfile 精确 (profileId, version)
 *      ＋内容身份三元组一致——§6.1 绑定面）；
 *   d. 证据清单对快照绑定校验（validateEvidenceManifestBinding——防错误
 *      工况/对象引用，§6.2 汇总器校验义务）；
 *   e. 必验工况覆盖矩阵校验（validateCaseCoverageMatrix——矩阵非法与
 *      漏验两面逐条列出；包络不替代 Executed，DYN-07）；
 *   f. 区域覆盖证据形状校验（validateRegionCoverageEvidence——形状非法
 *      即门禁失败；零样本/降级不是形状问题，归④级 I-5）。
 *
 * C6 红线：本级命中即整体 DataInsufficient，不可行证明不豁免（调用方
 * 在③级之前调用本函数即字面保证）。
 */
CommonGateOutcome collectCommonGateFailures(const VerdictInput& input,
                                            const AnalysisSnapshot& snapshot,
                                            const IProfileRegistryView& profiles)
{
    CommonGateOutcome out;

    // ---- a. 快照身份门禁（调用方装配结果——逐字段全量列出）----
    if (!input.snapshotGate.complete) {
        for (const std::string& field : input.snapshotGate.missingFields) {
            out.missingItems.push_back(
                {field, "快照身份门禁缺失/无效（②级通用必需项）：" + field});
        }
        out.diagnostics.push_back(makeVerdictDiag(
            kDiagSnapshotIncomplete, std::nullopt,
            "快照身份门禁不完整：缺失/无效字段 " + std::to_string(input.snapshotGate.missingFields.size())
                + " 项（全量见 missingItems）",
            "补齐快照身份（策略/名称映射内容身份、复现块、模式与证据等级标识）后重新评估"));
        out.failed = true;
    }

    // ---- b. Verified 前置固化校验（CON-03——对快照值直接复核，防线不因
    // 调用路径而缺；Quick/Preview 不强制固化但复现块完整性仍查——§6.2）----
    const std::vector<SnapshotModeIssue> modeIssues =
        validateSnapshotForMode(snapshot, input.mode);
    if (!modeIssues.empty()) {
        for (const SnapshotModeIssue& issue : modeIssues) {
            if (issue.code == SnapshotModeIssueCode::ExternalResourceNotSolidified) {
                out.missingItems.push_back(
                    {"externalResources[" + std::to_string(issue.index) + "]",
                     "外部资源未固化（Recorded——Verified 模式阻断正式结论，CON-03）"});
            } else {
                // ReproductionIncomplete：复现块版本要素不完整（快照有效性
                // 通用面——与模式无关，故任何模式都进门禁）。
                out.missingItems.push_back(
                    {"reproduction", "复现块版本要素不完整（productVersion/evidenceContractVersion）"});
            }
        }
        out.diagnostics.push_back(makeVerdictDiag(
            kDiagSnapshotIncomplete, std::nullopt,
            "快照模式前置校验存在问题 " + std::to_string(modeIssues.size())
                + " 项（固化/复现块——全量见 missingItems）",
            "固化全部被消费外部资源（CON-03）并补全复现块版本要素后重新评估"));
        out.failed = true;
    }

    // ---- c. 清单绑定 Profile 注册核对（三元组绑定面——§6.1 表：
    // profileId+version+contentIdentity 必须能对上注册值，否则证据清单
    // 绑定的是未注册的 Profile 形态，保守拒绝）----
    const RequiredEvidenceProfile* profile =
        profiles.findProfile(input.evidence.profileId, input.evidence.profileVersion);
    if (profile == nullptr) {
        out.missingItems.push_back(
            {input.evidence.profileId,
             "证据清单绑定的 Profile 未注册（" + input.evidence.profileId + "@"
                 + input.evidence.profileVersion + "）"});
        out.diagnostics.push_back(makeVerdictDiag(
            kDiagProfileUnresolved, std::nullopt,
            "Profile 未注册：" + input.evidence.profileId + "@"
                + input.evidence.profileVersion,
            "先注册域 Profile（§13：评估器注册前 Profile 必须先注册）再产出证据"));
        out.failed = true;
    } else if (!(profile->contentIdentity == input.evidence.profileContentIdentity)) {
        out.missingItems.push_back(
            {input.evidence.profileId,
             "Profile 内容身份与注册值错配（清单申报值≠注册计算值）"});
        out.diagnostics.push_back(makeVerdictDiag(
            kDiagProfileUnresolved, std::nullopt,
            "Profile 内容身份错配：" + input.evidence.profileId,
            "以注册内容身份为准核对证据清单绑定（Profile 内容变化→旧清单不可直接复用——§8.2）"));
        out.failed = true;
    }

    // ---- d. 证据清单对快照绑定校验（§6.2 汇总器义务：caseScope ⊆ 必验集、
    // subject ∈ objectClosure——防错误工况/对象引用，EV-COV-2 同源）----
    const std::vector<ManifestBindingIssue> bindingIssues =
        validateEvidenceManifestBinding(input.evidence, snapshot);
    if (!bindingIssues.empty()) {
        for (const ManifestBindingIssue& issue : bindingIssues) {
            // 涉事指称：项级问题用涉事 itemId，清单级问题（身份面残缺）
            // 用 profileId 定位。
            const std::string who =
                issue.itemId.empty() ? input.evidence.profileId : issue.itemId;
            out.missingItems.push_back(
                {who, "证据清单绑定校验失败（引用闭包外工况/对象或身份残缺）：" + issue.message});
        }
        out.diagnostics.push_back(makeVerdictDiag(
            kDiagManifestBindingInvalid, std::nullopt,
            "证据清单绑定校验失败 " + std::to_string(bindingIssues.size()) + " 项（全量见 missingItems）",
            "评估器只能在快照冻结闭包内引用工况/对象——修正清单绑定后重新评估"));
        out.failed = true;
    }

    // ---- e. 必验工况覆盖矩阵校验（§6.6——分母＝快照冻结态 RequiredCaseSet，
    // acceptance 4 保守字面；矩阵非法与漏验两面分离逐条列出，EV-COV-1/2）----
    const CoverageCheckResult coverage = validateCaseCoverageMatrix(input.coverage, snapshot);
    if (!coverage.issues.empty()) {
        for (const CoverageIssue& issue : coverage.issues) {
            // 漏验（MissingMandatoryExecution）与其余矩阵非法类的缺失文案
            // 区分——漏验是 EVI-02 的核心拦截面（即使已有包络合并结果，
            // DYN-07：包络不替代任何工况的 Executed 条目）。
            std::string reason;
            if (issue.code == CoverageIssueCode::MissingMandatoryExecution) {
                reason = "必验工况漏验（enabled∧mandatory 工况无 Executed 条目——包络不替代，DYN-07）";
            } else {
                reason = "覆盖矩阵非法：" + issue.message;
            }
            out.missingItems.push_back({issue.caseId, std::move(reason)});
        }
        out.diagnostics.push_back(makeVerdictDiag(
            kDiagCaseCoverageMissing, std::nullopt,
            "工况覆盖门禁失败：问题 " + std::to_string(coverage.issues.size()) + " 项（矩阵合法="
                + (coverage.matrixLegal ? "true" : "false")
                + "，覆盖完备=" + (coverage.coverageComplete ? "true" : "false") + "）",
            "为全部 enabled∧mandatory 工况补齐 Executed 条目并修正矩阵非法条目后重新评估"));
        out.failed = true;
    }

    // ---- f. 区域覆盖证据形状校验（§6.6——形状非法＝门禁失败；零样本/
    // 降级是形状自洽下的数据不足语义，归④级，I-5 分流）----
    for (const RegionCoverageEvidence& region : input.regionCoverages) {
        const RegionCoverageCheckResult check = validateRegionCoverageEvidence(region, snapshot);
        if (check.valid) {
            continue;  // 形状自洽——零样本/降级留待④级裁定
        }
        out.missingItems.push_back(
            {region.sampleSetIdentity.toCanonical(),
             "区域采样证据形状非法（分母完整性/样本集锚核对失败，KIN-04 R8）"});
        out.diagnostics.push_back(makeVerdictDiag(
            kDiagCaseCoverageMissing, std::nullopt,
            "区域采样证据形状非法：样本集 " + region.sampleSetIdentity.toCanonical()
                + "（问题 " + std::to_string(check.issues.size()) + " 项）",
            "按冻结采样计划修正分母声明与分类计数（不可达保留分母）后重新评估"));
        out.failed = true;
    }

    return out;
}

// =====================================================================
// ④级数据不足判定组成（§6.4.1 ④＋I-5/I-6/P-EV-7——全量收集）
// =====================================================================

/**
 * @brief ④级"必需证据缺失/数据不足"判定（§6.4.1 ④的字面组成＋I-5/I-6/
 *        P-EV-7 实现口径；填充明细并返回命中标志）。
 *
 * 命中条件（任一即④级命中——全部逐项评估不短路，缺失全量列出）：
 *   a. 必需项缺口非空（checkEvidenceCompleteness——Missing/Invalid/
 *      Unverified 全量；NotApplicable 不计缺失，EV-VER-7）；
 *   b. 证明存在但字段级校验失败（无效证明＝Invalid 语义——D-09：
 *      不凭存在性采信，EV-VER-6 反例半边）；
 *   c. 搜索未果记录在场（§6.3 末硬规则：DataInsufficient＋透传记录，
 *      不得输出不可行——EV-VER-2/4）；
 *   d. 区域证据零样本（覆盖率不定义——不得输出 0%/100%，EV-COV-3）
 *      或降级必要（dataInsufficient>0→整体降级，KIN-04 R8）；
 *   e. 快照必验集无 enabled∧mandatory 工况（P-EV-7 保守处置：覆盖矩阵
 *      平凡完备但"依赖工况证据的判定项"无从满足——不得输出正式通过）。
 *
 * @param proofIssues [in] 证明字段级校验结果（aggregateVerdict 已算——
 *                    无 proof 时为空且 proofPresent==false）
 * @param proofPresent [in] 输入是否携带证明
 */
bool collectDataInsufficient(const VerdictInput& input,
                             const AnalysisSnapshot& snapshot,
                             const RequiredEvidenceProfile& profile,
                             bool proofPresent,
                             const std::vector<ProofIssue>& proofIssues,
                             VerdictResult& result)
{
    bool hit = false;

    // ---- a. 必需项缺口（④级主条件——"必需项（适用者）存在 Missing/
    // Invalid/Unverified 且无有效证明"；③级未命中即"无有效证明"）----
    const EvidenceCompletenessResult completeness =
        checkEvidenceCompleteness(profile, input.evidence);
    if (!completeness.requiredGaps.empty()) {
        for (const EvidenceCompletenessResult::Gap& gap : completeness.requiredGaps) {
            result.missingItems.push_back(
                {gap.itemId, std::string{evidenceGapStatusText(gap.status)}});
        }
        result.diagnostics.push_back(makeVerdictDiag(
            kDiagEvidenceMissing, std::nullopt,
            "必需证据不满足 " + std::to_string(completeness.requiredGaps.size()) + " 项（全量见 missingItems；"
                "NotApplicable 不计缺失——C2/ERR-01）",
            "补齐或修正必需证据后重新评估；建议项缺失不阻断（单独标注）"));
        hit = true;
    }

    // ---- b. 证明存在但无效（D-09：字段级校验失败＝不可采信——既不输出
    // 不可行，也不当作"无证明"而放行⑤级，保守落数据不足）----
    if (proofPresent && !proofIssues.empty()) {
        result.missingItems.push_back(
            {input.proof->claimToken,
             "确定性不可行证明字段级校验失败（" + std::to_string(proofIssues.size())
                 + " 项问题）——无效证明不可采信（D-09）"});
        result.diagnostics.push_back(makeVerdictDiag(
            kDiagProofInvalid, input.proof->subject,
            "证明字段级校验失败：" + proofIssues.front().message + "（共 "
                + std::to_string(proofIssues.size()) + " 项）",
            "修正证明条件必填字段/产生者注册/快照切片绑定/覆盖声明后重新提交"));
        hit = true;
    }

    // ---- c. 搜索未果（§6.3 末硬规则：汇总层判 DataInsufficient（附该
    // 记录），不得输出不可行结论——EV-VER-2 第一次/全解被过滤场景）----
    if (input.searchRecord.has_value()) {
        result.diagnostics.push_back(makeVerdictDiag(
            kDiagSearchExhausted, std::nullopt,
            "搜索未果：已用预算 " + std::to_string(input.searchRecord->searchBudgetUsed)
                + "、初值 " + std::to_string(input.searchRecord->initialGuessesTried)
                + " 个、被过滤解 " + std::to_string(input.searchRecord->filteredSolutions.size())
                + " 个（记录透传——扩大初值/预算后按同一冻结输入复评，C5）",
            "扩大初值数量或搜索预算后按 §4.2.4 复评（新 sliceId、同 inputBaselineId）"));
        hit = true;
    }

    // ---- d. 区域证据零样本/降级（形状已合格——I-5 分流到本级的语义面；
    // EV-COV-3：零样本不输出 0%/100%、降级时覆盖率数值仅作参考值）----
    for (const RegionCoverageEvidence& region : input.regionCoverages) {
        const RegionCoverageCheckResult check = validateRegionCoverageEvidence(region, snapshot);
        if (!check.valid) {
            continue;  // 形状非法已在②级列出——不重复计
        }
        if (check.zeroSample) {
            // 零样本：覆盖率不定义（分母为 0——EV-COV-3 反例面）。诊断以
            // 样本集身份定位涉事区域证据。
            result.diagnostics.push_back(makeVerdictDiag(
                kDiagRegionCoverageDowngraded, std::nullopt,
                "区域采样证据零样本（样本集 " + region.sampleSetIdentity.toCanonical()
                    + "，plannedTotal==0）——覆盖率不定义，不得输出 0%/100%（EV-COV-3）",
                "检查采样计划是否为空/区域是否退化——修正后按同一冻结样本集重评"));
            hit = true;
        }
        if (check.downgradedRequired) {
            result.diagnostics.push_back(makeVerdictDiag(
                kDiagRegionCoverageDowngraded, std::nullopt,
                "区域采样证据存在数据不足样本（dataInsufficient>0）——结论整体降级 DataInsufficient，"
                "覆盖率数值仅作参考值（KIN-04 R8；补全后按同一冻结样本集复评）",
                "补全数据不足样本的证据后按同一冻结样本集复评（inputBaselineId 凭据）"));
            hit = true;
        }
    }

    // ---- e. P-EV-7 空必验集保守处置（acceptance 4/O-14：分母＝快照冻结态
    // 必验工况集；空分母时覆盖矩阵平凡完备，但"依赖工况证据的判定项"全部
    // 无从满足——按④级判数据不足，不得输出正式通过）----
    const bool hasMandatoryCase = std::any_of(
        snapshot.caseSet.entries.cbegin(), snapshot.caseSet.entries.cend(),
        [](const CaseEntry& entry) { return entry.enabled && entry.mandatory; });
    if (!hasMandatoryCase) {
        result.diagnostics.push_back(makeVerdictDiag(
            kDiagCaseCoverageMissing, std::nullopt,
            "无启用必验工况（快照必验集为空或全为非必验）——覆盖矩阵平凡完备但正式通过判定不成立（P-EV-7 保守处置）",
            "在需求侧启用/登记必验工况后重新评估；如需\"空集合法通过\"语义，走需求变更（P-EV-7）"));
        hit = true;
    }

    return hit;
}

}  // namespace

// =====================================================================
// 汇总决策表本体（§6.4.1）
// =====================================================================

VerdictResult aggregateVerdict(const VerdictInput& input,
                               const AnalysisSnapshot& snapshot,
                               const IProducerRegistryView& producers,
                               const IProfileRegistryView& profiles)
{
    VerdictResult result;

    // trace 预置固定六槽（⓪~⑤，level 升序）——evaluated/hit/note 随判定
    // 推进填写；前级命中后未评估的级保留槽位（完整决策路径，NFR-COR-04）。
    result.trace.records = {
        {VerdictLevel::OutcomePrecheck, false, false, {}},
        {VerdictLevel::InputReadiness, false, false, {}},
        {VerdictLevel::CommonEvidenceGate, false, false, {}},
        {VerdictLevel::InfeasibilityProof, false, false, {}},
        {VerdictLevel::MissingEvidence, false, false, {}},
        {VerdictLevel::EngineeringJudgement, false, false, {}},
    };
    VerdictLevelRecord* const records = result.trace.records.data();

    // ============================ ⓪ 前置 ============================
    // 执行轴与工程判定轴正交（CON-02）：取消/失败/中断没有工程判定可言
    // （TASK-02）——envelope 侧强制 NotApplicable（§7），汇总器在此直接
    // 短路，绝不把执行失败折算成不可行（§6.4.2 第一判定对）。
    if (input.outcome != core::TaskOutcome::Completed) {
        records[0] = {VerdictLevel::OutcomePrecheck, true, true,
                      "任务结果非 Completed（取消/失败/中断）——执行轴问题无工程判定（TASK-02）"};
        result.status = core::EngineeringStatus::NotApplicable;
        result.trace.hitLevel = VerdictLevel::OutcomePrecheck;
        result.diagnostics.push_back(makeVerdictDiag(
            kDiagOutcomeNotCompleted, std::nullopt,
            "任务结果非 Completed——不汇总工程判定（envelope 强制 NotApplicable）",
            "处理执行轴问题（取消/失败/中断原因）后重新发起评估"));
        return result;
    }
    records[0] = {VerdictLevel::OutcomePrecheck, true, false,
                  "任务结果 Completed——进入工程判定链"};

    // ============================ ① 输入非法 ============================
    // REQ-06：任一启用 Must 条目非法＝输入未完成——评估不该被派发；若结果
    // 对象仍产生（评估中检出），以此为由判数据不足（§6.4.1 ①行原文）。
    if (!input.readiness.valid) {
        records[1] = {VerdictLevel::InputReadiness, true, true,
                      "输入未完成（REQ-06：存在非法的启用 Must 条目）——不运行正式评估"};
        result.status = core::EngineeringStatus::DataInsufficient;
        result.trace.hitLevel = VerdictLevel::InputReadiness;
        // input-invalid 清单全量列出（不抽样——acceptance 2）。
        for (const std::string& item : input.readiness.invalidMustItems) {
            result.missingItems.push_back({item, "输入未完成（input-invalid）：启用 Must 条目非法"});
        }
        result.diagnostics.push_back(makeVerdictDiag(
            kDiagInputNotReady, std::nullopt,
            "输入未完成：非法的启用 Must 条目 " + std::to_string(input.readiness.invalidMustItems.size())
                + " 项（全量见 missingItems）",
            "在需求侧补全全部启用 Must 条目后重新发起评估"));
        return result;
    }
    records[1] = {VerdictLevel::InputReadiness, true, false,
                  "输入就绪（REQ-06 校验通过）"};

    // ============================ ② 通用证据门禁 ============================
    // C6 红线落点：不可行证明自身的可追溯性同受此门禁——本级命中即整体
    // DataInsufficient，证明不进入③（调用序保证字面顺序）。
    const CommonGateOutcome gate = collectCommonGateFailures(input, snapshot, profiles);
    if (gate.failed) {
        records[2] = {VerdictLevel::CommonEvidenceGate, true, true,
                      "通用证据门禁失败（快照身份/固化/Profile 绑定/清单绑定/覆盖矩阵/区域证据形状）——"
                      "证明不豁免门禁（C6）"};
        result.status = core::EngineeringStatus::DataInsufficient;
        result.trace.hitLevel = VerdictLevel::CommonEvidenceGate;
        result.missingItems = std::move(gate.missingItems);
        result.diagnostics = std::move(gate.diagnostics);
        return result;
    }
    records[2] = {VerdictLevel::CommonEvidenceGate, true, false,
                  "通用证据门禁通过（快照身份/固化/Profile 绑定/清单绑定/覆盖矩阵/区域证据形状）"};

    // Profile 在②级已核实为已注册且内容身份一致——④级完备性核对直接取用
    // （findProfile 二次查询：指针在②级分支未被存储，此处重新解析；同一
    // 注册表同一键两次查询结果一致由实现方契约保证——只读注册表）。
    const RequiredEvidenceProfile* profile =
        profiles.findProfile(input.evidence.profileId, input.evidence.profileVersion);

    // ============================ ③ 确定性不可行证明 ============================
    // D-09（acceptance 3）：证明的字段级校验在此执行——存在性（optional
    // 有值）绝不等于有效性；绑定一致性以快照与清单切片身份为事实面
    // （expectedSliceId＝manifest.sliceId——"与被汇总结果一致"）。
    std::vector<ProofIssue> proofIssues;
    if (input.proof.has_value()) {
        proofIssues = validateProof(*input.proof, producers, snapshot,
                                    input.evidence.sliceId);
    }

    if (input.proof.has_value() && proofIssues.empty()) {
        // ③命中：证明即该域必需产物——EngineeringInfeasible（P-EV-3 字面
        // 顺序：即使他域/后续级存在缺失，本级先命中先输出）。
        records[3] = {VerdictLevel::InfeasibilityProof, true, true,
                      "存在有效确定性不可行证明（validateProof 字段级校验通过）——EngineeringInfeasible"};
        result.status = core::EngineeringStatus::EngineeringInfeasible;
        result.trace.hitLevel = VerdictLevel::InfeasibilityProof;
        // §6.4.1 ③附加义务（I-6）：成功产物类证据按"因不可行而不适用"
        // 记录——汇总器出具说明性诊断，列出 substitutable 项（清单本身的
        // NotApplicable 标记义务在评估器侧，汇总器不重写清单；通用门禁类
        // 〔substitutable==false〕不豁免——但本级命中后通用门禁已在②级
        // 通过，无须再豁免）。
        std::string substitutableIds;
        if (profile != nullptr) {
            const auto appendSubstitutable =
                [&substitutableIds](const std::vector<EvidenceProfileItem>& items) {
                    for (const EvidenceProfileItem& item : items) {
                        if (item.substitutableByInfeasibility) {
                            if (!substitutableIds.empty()) {
                                substitutableIds += "、";
                            }
                            substitutableIds += item.itemId;
                        }
                    }
                };
            appendSubstitutable(profile->required);
            appendSubstitutable(profile->suggested);
        }
        result.diagnostics.push_back(makeVerdictDiag(
            kDiagInfeasibilitySubstitution,
            input.proof->subject,
            substitutableIds.empty()
                ? "有效不可行证明成立——无 substitutable 成功产物类证据项需要豁免"
                : "有效不可行证明成立——成功产物类证据按\"因不可行而不适用\"记录（substitutable 项："
                      + substitutableIds + "）",
            "按 RPT-05 评审记录资格呈现不可行结论及其原因（§7.2）"));
        return result;
    }
    if (input.proof.has_value()) {
        // 证明在场但无效：不输出不可行（D-09），落④级数据不足——无效
        // 明细在④级统一列出（见下）。
        records[3] = {VerdictLevel::InfeasibilityProof, true, false,
                      "证明在场但字段级校验失败（" + std::to_string(proofIssues.size())
                          + " 项问题）——无效证明不可采信（D-09），落④级数据不足"};
    } else {
        records[3] = {VerdictLevel::InfeasibilityProof, true, false,
                      "无确定性不可行证明（搜索未果不是证明——C5/C8）"};
    }

    // ============================ ④ 必需证据缺失/数据不足 ============================
    // 全量收集（不短路——acceptance 2）：gaps/无效证明/搜索未果/区域零样本
    // 与降级/空必验集（P-EV-7）任一命中即 DataInsufficient，明细全量列出。
    if (profile != nullptr
        && collectDataInsufficient(input, snapshot, *profile,
                                   input.proof.has_value(), proofIssues, result)) {
        records[4] = {VerdictLevel::MissingEvidence, true, true,
                      "必需证据缺失或数据不足（gaps/无效证明/搜索未果/区域零样本与降级/空必验集）"};
        result.status = core::EngineeringStatus::DataInsufficient;
        result.trace.hitLevel = VerdictLevel::MissingEvidence;
        // 搜索未果记录透传（§6.3 末：附该记录——数据不足凭据，EV-VER-2/4）。
        result.searchRecord = input.searchRecord;
        return result;
    }
    // 防御：②级通过时 profile 必非空（未注册/错配已在②级拦截）——若注册
    // 表实现违反"同一键查询稳定"契约导致此处为空，属于调用方违约，保守
    // 判数据不足而非越级放行（宁可数据不足不可虚通过，P-EV-7 同向）。
    if (profile == nullptr) {
        records[4] = {VerdictLevel::MissingEvidence, true, true,
                      "Profile 注册查询不稳定（②级通过后无法再次解析）——保守判数据不足"};
        result.status = core::EngineeringStatus::DataInsufficient;
        result.trace.hitLevel = VerdictLevel::MissingEvidence;
        result.diagnostics.push_back(makeVerdictDiag(
            kDiagProfileUnresolved, std::nullopt,
            "Profile 注册查询不稳定：" + input.evidence.profileId + "@"
                + input.evidence.profileVersion,
            "检查 Profile 注册表实现（同一键查询必须稳定——只读契约）"));
        return result;
    }
    records[4] = {VerdictLevel::MissingEvidence, true, false,
                  "必需证据齐备（无 gaps/无效证明/搜索未果/区域降级，必验工况集非空）"};

    // ============================ ⑤ 工程判定 ============================
    // 证据齐备后的域判定汇总（REQ-06）：Must 违例→不可行（判定记录）；
    // Should 违例→可行＋警告（不阻断）；否则可行。verdictInputs 透传进
    // 诊断 cause（EV-VER-8 观测面——违例明细不丢失）。
    records[5] = {VerdictLevel::EngineeringJudgement, true, true, {}};
    result.trace.hitLevel = VerdictLevel::EngineeringJudgement;
    if (!input.domain.mustViolations.empty()) {
        records[5].note = "有效 Must 未满足 " + std::to_string(input.domain.mustViolations.size())
                              + " 项——EngineeringInfeasible（REQ-06 判定记录）";
        result.status = core::EngineeringStatus::EngineeringInfeasible;
        for (const DomainVerdictViolation& violation : input.domain.mustViolations) {
            result.diagnostics.push_back(makeVerdictDiag(
                kDiagMustViolation, std::nullopt,
                "Must 违例：" + violation.itemId + "——" + violation.detail,
                "满足该 Must 条目或修订需求后重新评估"));
        }
        return result;
    }
    if (!input.domain.shouldViolations.empty()) {
        // Should 未满足不阻断（REQ-06）——Feasible＋逐条警告诊断。
        records[5].note = "Should 未满足 " + std::to_string(input.domain.shouldViolations.size())
                              + " 项——Feasible＋警告诊断（不阻断，REQ-06）";
        result.status = core::EngineeringStatus::Feasible;
        for (const DomainVerdictViolation& violation : input.domain.shouldViolations) {
            result.diagnostics.push_back(makeVerdictDiag(
                kDiagShouldViolation, std::nullopt,
                "Should 违例（警告，不阻断）：" + violation.itemId + "——" + violation.detail,
                "评估是否满足该 Should 条目（工程建议——不阻断正式判定）"));
        }
        return result;
    }
    records[5].note = "证据齐备且无 Must/Should 违例——Feasible";
    result.status = core::EngineeringStatus::Feasible;
    return result;
}

// =====================================================================
// 两类声明资格检查（§7.2——实现口径 I-7）
// =====================================================================

EligibilityCheck checkFormalPassEligibility(const VerdictInput& input,
                                            const AnalysisSnapshot& snapshot,
                                            const VerdictResult& verdict,
                                            const IProfileRegistryView& profiles)
{
    EligibilityCheck out;

    // 条件 1：mode==Verified（§7.2 表——正式通过结论只可能来自 Verified）。
    if (input.mode != core::EvaluationMode::Verified) {
        out.unmetConditions.push_back("mode-not-verified");
    }
    // 条件 2：outcome==Completed（执行轴前置——与决策表⓪同源）。
    if (input.outcome != core::TaskOutcome::Completed) {
        out.unmetConditions.push_back("outcome-not-completed");
    }
    // 条件 3：覆盖矩阵完备（矩阵合法且全部 enabled∧mandatory 工况 Executed
    // ——EVI-02；分母＝快照冻结态必验集，acceptance 4）。注意 §7.2 本条件
    // 是"覆盖矩阵完备"字面，不含门禁其余组成（清单绑定等由正式结果链在
    // aggregateVerdict②级把关——资格检查按表逐条件独立核对）。
    const CoverageCheckResult coverage = validateCaseCoverageMatrix(input.coverage, snapshot);
    if (!coverage.matrixLegal || !coverage.coverageComplete) {
        out.unmetConditions.push_back("coverage-incomplete");
    }
    // 条件 4：必需证据齐备（requiredGaps 空；Profile 未注册＝无法核对＝
    // 视为不齐备——保守方向）。
    const RequiredEvidenceProfile* profile =
        profiles.findProfile(input.evidence.profileId, input.evidence.profileVersion);
    bool evidenceComplete = false;
    if (profile != nullptr
        && profile->contentIdentity == input.evidence.profileContentIdentity) {
        evidenceComplete =
            checkEvidenceCompleteness(*profile, input.evidence).requiredGaps.empty();
    }
    if (!evidenceComplete) {
        out.unmetConditions.push_back("evidence-incomplete");
    }
    // 条件 5：engineeringStatus==Feasible。
    if (verdict.status != core::EngineeringStatus::Feasible) {
        out.unmetConditions.push_back("status-not-feasible");
    }

    out.eligible = out.unmetConditions.empty();
    return out;
}

EligibilityCheck checkReviewRecordEligibility(const VerdictInput& input,
                                              const AnalysisSnapshot& snapshot,
                                              const VerdictResult& verdict,
                                              const IProducerRegistryView& producers,
                                              const IProfileRegistryView& profiles)
{
    EligibilityCheck out;

    // 条件 1：mode==Verified。
    if (input.mode != core::EvaluationMode::Verified) {
        out.unmetConditions.push_back("mode-not-verified");
    }
    // 条件 2：outcome==Completed。
    if (input.outcome != core::TaskOutcome::Completed) {
        out.unmetConditions.push_back("outcome-not-completed");
    }
    // 条件 3：②级门禁通过（与 aggregateVerdict ②级同一事实面——快照身份/
    // 固化/Profile 绑定/清单绑定/覆盖矩阵合法且完备/区域证据形状）。
    if (collectCommonGateFailures(input, snapshot, profiles).failed) {
        out.unmetConditions.push_back("common-gate-failed");
    }
    // 条件 4：engineeringStatus==EngineeringInfeasible。
    if (verdict.status != core::EngineeringStatus::EngineeringInfeasible) {
        out.unmetConditions.push_back("status-not-infeasible");
    }
    // 条件 5：携带有效证明（D-09：必须字段级校验通过——不是"携带证明"）
    // 或 Must 违例记录（§7.2 表条件列原文括注"或"的两分支）。
    const bool hasValidProof =
        input.proof.has_value()
        && validateProof(*input.proof, producers, snapshot, input.evidence.sliceId).empty();
    if (!hasValidProof && input.domain.mustViolations.empty()) {
        out.unmetConditions.push_back("no-valid-proof-or-must-violation");
    }

    out.eligible = out.unmetConditions.empty();
    return out;
}

// =====================================================================
// 比较基准一致性检查（§6.5——实现口径 I-8）
// =====================================================================

BaselineConsistencyResult
checkComparisonBaselinesConsistent(const std::vector<ComparisonBaseline>& baselines)
{
    BaselineConsistencyResult out;

    // 空集/单条：无可比差异——平凡通过（§6.5 语义只约束"两两比较"场景；
    // I-8 登记口径）。用差集类型记录差异维度：任两条在某维度不等即该维度
    // 入清单（去重——"差异维度逐项输出"按维度聚合，不按条目对罗列）。
    if (baselines.size() < 2) {
        out.consistent = true;
        return out;
    }

    bool diffProject = false;
    bool diffBranch = false;
    bool diffInputBaseline = false;
    bool diffRequiredCaseSet = false;
    bool diffSampleSets = false;

    // 以首条为参照两两核对（差异维度与参照对无关——不等关系在维度上
    // 传递：任一对不等即该维度不一致）。
    const ComparisonBaseline& first = baselines.front();
    for (const ComparisonBaseline& other : baselines) {
        diffProject = diffProject || !(other.project == first.project);
        diffBranch = diffBranch || !(other.branch == first.branch);
        diffInputBaseline =
            diffInputBaseline || !(other.inputBaselineId == first.inputBaselineId);
        diffRequiredCaseSet =
            diffRequiredCaseSet || !(other.requiredCaseSetId == first.requiredCaseSetId);
        diffSampleSets = diffSampleSets || !(other.sampleSetIds == first.sampleSetIds);
    }

    // 按枚举声明序输出（＝§6.5 字段序——确定性，NFR-COR-02）。
    if (diffProject) {
        out.differingDimensions.push_back(BaselineDifferenceDimension::Project);
    }
    if (diffBranch) {
        out.differingDimensions.push_back(BaselineDifferenceDimension::Branch);
    }
    if (diffInputBaseline) {
        out.differingDimensions.push_back(BaselineDifferenceDimension::InputBaseline);
    }
    if (diffRequiredCaseSet) {
        out.differingDimensions.push_back(BaselineDifferenceDimension::RequiredCaseSet);
    }
    if (diffSampleSets) {
        out.differingDimensions.push_back(BaselineDifferenceDimension::SampleSets);
    }

    out.consistent = out.differingDimensions.empty();
    return out;
}

}  // namespace sdurws::ird::evidence
