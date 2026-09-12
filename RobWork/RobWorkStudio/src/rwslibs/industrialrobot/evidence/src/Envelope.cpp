/**
 * @file   Envelope.cpp
 * @brief  结果包络的实现——表 3 合法组合矩阵校验器（上下文无关面＋快照
 *         绑定面）与 make() 构造边界（SA-13）。
 *
 * 实现纪律（与 Envelope.hpp 契约注释一一对应，此处只记实现要点）：
 *   - 校验器为纯函数、无共享可变状态；issue 顺序＝检查序（固定）——同
 *     一坏组装必报同一首错、必得同一 issue 序列（NFR-COR-02）。
 *   - 表 3 逐格强制（EV-ENV-1 acceptance 1）：非法组合逐一对应独立的
 *     EnvelopeIssueCode；异常轨统一走 EvidenceErrorCode::EnvelopeIllegal-
 *     Combination（Errors.hpp 全表设计），逐格区分由 issue 码＋含字段名
 *     的 detail 承载。
 *   - 载荷摘要由 make() 经 core::ContentDigester 重算（CR-02：摘要算法
 *     唯一——本单元不引入第二摘要实现，不接受调用方申报）。
 *   - CR-01：outcome/engineeringStatus/mode 三轴类型即 core 词表类型
 *     （编译期已锁定——公共头字段类型）；本翻译单元不定义任何本地枚举。
 *
 * 线程安全：可重入纯函数（无静态可变状态）。
 */

#include <sdurws/ird/evidence/Envelope.hpp>

#include <sdurws/ird/evidence/Errors.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

namespace sdurws::ird::evidence {

namespace {

// =====================================================================
// 内部辅助（局部纯函数——无共享状态）
// =====================================================================

/**
 * @brief 判断 core::ContentIdentity 是否为非零有效身份（绑定面校验原语）。
 *
 * 保留值（全零）＝"未绑定"——包络的三个身份字段（快照/切片/基准）都
 * 是结果可信性的绑定锚，任何一个为保留值都使包络无法定位其输入事实
 * （§7.1 结构注释"snapshotId, sliceId, inputBaselineId"绑定面）。
 */
bool isValidIdentity(const core::ContentIdentity& id)
{
    return id.isValid();
}

/**
 * @brief 判断字符串是否满足"非空＋无 NUL"编码安全下限（域 token/版本串
 *        的统一词形闸门——Evidence.hpp O-13 保守字面同口径）。
 *
 * @param text [in] 待检字符串
 * @return 非空且不含 NUL 字符 true
 */
bool isSafeToken(const std::string& text)
{
    return !text.empty() && text.find('\0') == std::string::npos;
}

/**
 * @brief 判断 EvidenceProfileRef 是否为结构有效的绑定三元组。
 *
 * 三条件（与 §6.1/§9.5 注册面同词表）：profileId 在五域词表
 * （isDomainProfileId——不私裁域外 id）、version 非空无 NUL、内容身份
 * 非零。语义核对（三元组是否对应已注册 Profile）归消费侧查表
 * （§8.2 缓存兼容/EV-T06 汇总门禁）——本函数只查结构。
 */
bool isValidProfileRef(const EvidenceProfileRef& ref)
{
    return isDomainProfileId(ref.profileId) && isSafeToken(ref.version)
        && isValidIdentity(ref.contentIdentity);
}

/**
 * @brief 判断证据清单的绑定三元组是否结构有效（表 3 行 1"必含证据清单"
 *        的清单侧结构面）。
 *
 * manifest.snapshotId/sliceId 非零、profileId 在五域词表、version 非空、
 * profileContentIdentity 非零——清单必须"记了什么评估"才能作为证据
 * 面被采信（§6.2 记录面原文）。与包络同名绑定的一致性在
 * validateCombination 单独核对（CompletedManifestNotCoherent）。
 */
bool isManifestTripleValid(const EvidenceManifest& manifest)
{
    return isValidIdentity(manifest.snapshotId)
        && isValidIdentity(manifest.sliceId)
        && isDomainProfileId(manifest.profileId)
        && isSafeToken(manifest.profileVersion)
        && isValidIdentity(manifest.profileContentIdentity);
}

/**
 * @brief 拼接问题清单为 make() 异常 detail（逐条含字段名——EV-ENV-1
 *        "Draft 校验失败消息含字段"观测面）。
 *
 * @param issues [in] 校验问题清单（非空）
 * @return "；"连接的逐条消息串（顺序＝检查序，确定性）
 */
std::string joinIssueMessages(const std::vector<EnvelopeIssue>& issues)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (i != 0) {
            out << "；";
        }
        out << issues[i].message;
    }
    return out.str();
}

}  // namespace

// =====================================================================
// Must 违例记录识别（表 3 行 1"或 Must 违例记录"的识别面）
// =====================================================================

bool isMustViolationRecord(const core::DiagnosticRecord& record)
{
    // 识别面＝EV-T06 汇总层 kDiagMustViolation 建议码（Verdict.hpp——
    // ⑤级判定记录；CR-01 同源纪律：不私造 token，码值权威归 diagnostics）。
    return record.code == std::string{kDiagMustViolation};
}

// =====================================================================
// 上下文无关组合校验（make() 的拒绝依据——表 3 全部不依赖快照的格子）
// =====================================================================

std::vector<EnvelopeIssue> validateCombination(const ResultEnvelopeDraft& draft)
{
    std::vector<EnvelopeIssue> issues;

    // ---- 第①段：无条件边界（先于组合行核对——表 3 行 4"任意×Preview"
    // 是最外层的模式边界：Preview 根本不产生结果对象〔表 1/EVI-01〕，
    // 因此无论其余字段为何值，mode==Preview 一票拒绝且排首位——保证
    // "Preview 构造边界拒绝"的可观测顺序确定性）。 ----
    if (draft.mode == core::EvaluationMode::Preview) {
        issues.push_back({EnvelopeIssueCode::PreviewModeForbidden,
                          "mode=Preview：Preview 不产生结果对象（表 1/表 3 行 4）——"
                          "禁止构造 ResultEnvelope"});
    }

    // 任务五元组（execution 分配的身份组——任一字段为保留值即无法把
    // 结果锚定到一次真实运行，§7.2 接纳核对的事实面）。
    if (!draft.task.isValid()) {
        issues.push_back({EnvelopeIssueCode::TaskIdentityInvalid,
                          "task 五元组存在保留值字段（project/branch/revision/"
                          "run/attempt 必须全部 isValid）"});
    }

    // 评估键词形（进缓存身份面——与 InputSlice 同一闸门 isValidEvaluationKey；
    // 词形坏则缓存键/诊断引用无歧义性破坏，CON-04）。
    if (!isValidEvaluationKey(draft.evaluationKey)) {
        issues.push_back({EnvelopeIssueCode::EvaluationKeyInvalid,
                          "evaluationKey 词形非法（须为 [a-z][a-z0-9-]{1,63}"
                          "——不含点）：\"" + draft.evaluationKey + "\""});
    }

    // 三个身份绑定面（快照/切片/基准——保留值＝未绑定，包络无法定位
    // 其输入事实与比较基准，CON-05/D-04）。
    if (!isValidIdentity(draft.snapshotId) || !isValidIdentity(draft.sliceId)
        || !isValidIdentity(draft.inputBaselineId)) {
        issues.push_back({EnvelopeIssueCode::InputIdentityInvalid,
                          "snapshotId/sliceId/inputBaselineId 存在保留值（全零）"
                          "——绑定面必须全部非零"});
    }

    // 产出者版本串（复现要素家族——编码安全下限：非空＋无 NUL）。
    if (!isSafeToken(draft.producer.productVersion)) {
        issues.push_back({EnvelopeIssueCode::ProducerInfoInvalid,
                          "producer.productVersion 为空串或含 NUL（非空＋无 NUL "
                          "编码安全下限）"});
    }

    // 载荷域 token（词表归域——evidence 只查编码安全下限，N-5/O-13）。
    if (draft.payload.has_value() && !isSafeToken(draft.payload->kindToken)) {
        issues.push_back({EnvelopeIssueCode::PayloadKindTokenInvalid,
                          "payload.kindToken 为空串或含 NUL（域登记 token "
                          "编码安全下限）"});
    }

    // partialData 复用标记（§7.1"恒 false：诊断价值保留，不得正式复用"
    // ——EV-ENV-2 的数据面：部分数据只可读诊断，绝不进入正式复用通道）。
    if (draft.partialData.has_value() && draft.partialData->reusable) {
        issues.push_back({EnvelopeIssueCode::PartialDataReusableFlag,
                          "partialData.reusable 必须为 false（诊断性部分数据"
                          "不得正式复用——§7.1/EV-ENV-2）"});
    }

    // ---- 第②段：Completed 组合行（表 3 行 1 合法格＋行 2 非法格）。 ----
    if (draft.outcome == core::TaskOutcome::Completed) {
        // 行 2：Completed×NotApplicable 拒绝（ERR-01——NotApplicable 只配
        // 非 Completed，显式标记语义与"计算完成"互斥）。
        if (draft.engineeringStatus == core::EngineeringStatus::NotApplicable) {
            issues.push_back(
                {EnvelopeIssueCode::CompletedStatusNotApplicable,
                 "engineeringStatus=NotApplicable 与 outcome=Completed 组合非法"
                 "（表 3 行 2/ERR-01——NotApplicable 只配非 Completed）"});
        }
        else {
            // 行 1 公共面：必含证据清单与工况标识（caseScope 非空＝工况
            // 标识；profile 三元组＋清单绑定三元组结构有效＝证据清单被
            // 绑定——二者缺一即"无凭据的正式结论"）。
            const bool caseScopeOk = !draft.caseScope.caseIds.empty();
            const bool profileOk = isValidProfileRef(draft.profile);
            const bool manifestOk = isManifestTripleValid(draft.evidence);
            if (!caseScopeOk || !profileOk || !manifestOk) {
                std::ostringstream what;
                what << "Completed 必含证据清单与工况标识（表 3 行 1）：";
                if (!caseScopeOk) {
                    what << "caseScope.caseIds 为空；";
                }
                if (!profileOk) {
                    what << "profile 三元组结构无效（profileId 须在五域词表/"
                         << "version 非空/contentIdentity 非零）；";
                }
                if (!manifestOk) {
                    what << "evidence 清单绑定三元组结构无效；";
                }
                issues.push_back(
                    {EnvelopeIssueCode::CompletedEvidenceOrCaseMissing,
                     what.str()});
            }

            // 清单与包络绑定一致（§6.2 记录面：清单记录的 snapshotId/
            // sliceId 必须就是本包络的绑定——指向别处评估的清单混入本
            // 结果＝证据张冠李戴）。
            if (manifestOk
                && (draft.evidence.snapshotId != draft.snapshotId
                    || draft.evidence.sliceId != draft.sliceId)) {
                issues.push_back(
                    {EnvelopeIssueCode::CompletedManifestNotCoherent,
                     "evidence 清单绑定与包络不一致（manifest.snapshotId/"
                     "sliceId 必须等于包络同名绑定——§6.2 记录面）"});
            }

            // 清单项 presence 纪律（§6.2 各态必填面——EV-T05 校验器复用：
            // Satisfied 必带摘要、Invalid 必带原因、NotApplicable 必带原因；
            // 复用而非重写＝同一纪律单一实现点）。
            const auto itemIssues = validateEvidenceItems(draft.evidence.items);
            if (!itemIssues.empty()) {
                issues.push_back(
                    {EnvelopeIssueCode::CompletedEvidenceItemDiscipline,
                     "evidence 清单项 presence 纪律违例（validateEvidenceItems "
                     "共 " + std::to_string(itemIssues.size()) + " 条——首条: "
                     + itemIssues.front().message + "）"});
            }

            // DataInsufficient：缺失项**全量**清单必含（空清单＋
            // DataInsufficient→拒绝——表 3 行 1 原文；"数据不足"必须说清
            // 缺什么，EV-ENV-1 点名反例）。
            if (draft.engineeringStatus == core::EngineeringStatus::DataInsufficient
                && draft.missingItems.empty()) {
                issues.push_back(
                    {EnvelopeIssueCode::DataInsufficientNoMissingItems,
                     "engineeringStatus=DataInsufficient 而 missingItems 为空"
                     "（表 3 行 1：缺失项全量清单必含——空清单拒绝）"});
            }

            // Feasible：无未解决缺失项（表 3 行 1——"可行"与"有缺"互斥；
            // 缺什么就报 DataInsufficient，不允许带缺通过）。
            if (draft.engineeringStatus == core::EngineeringStatus::Feasible
                && !draft.missingItems.empty()) {
                issues.push_back(
                    {EnvelopeIssueCode::FeasibleWithMissingItems,
                     "engineeringStatus=Feasible 而 missingItems 非空（共 "
                     + std::to_string(draft.missingItems.size())
                     + " 条）——Feasible 须无未解决缺失项（表 3 行 1）"});
            }

            // EngineeringInfeasible：凭据在场（上下文无关面）——确定性
            // 不可行证明**或** Must 违例记录二者有其一（表 3 行 1"或"）。
            // 注意此处只查"在场"：证明的"有效"（validateProof 逐字段
            // 通过，D-09）需要快照/注册表事实面，在快照绑定重载核对；
            // Must 违例记录的识别面＝kDiagMustViolation 码（EV-T06 ⑤级
            // 产出的唯一词表——CR-01 同源不私造 token）。
            if (draft.engineeringStatus
                    == core::EngineeringStatus::EngineeringInfeasible) {
                const bool proofPresent = draft.infeasibilityProof.has_value();
                const bool mustRecordPresent = std::any_of(
                    draft.diagnostics.begin(), draft.diagnostics.end(),
                    [](const core::DiagnosticRecord& r) {
                        return isMustViolationRecord(r);
                    });
                if (!proofPresent && !mustRecordPresent) {
                    issues.push_back(
                        {EnvelopeIssueCode::EngineeringInfeasibleNoBasis,
                         "engineeringStatus=EngineeringInfeasible 缺凭据：既无 "
                         "infeasibilityProof 也无 Must 违例记录（表 3 行 1——"
                         "须含有效证明或 Must 违例记录）"});
                }
            }
        }
    }
    // ---- 第③段：非 Completed 组合行（表 3 行 3 及其反面）。 ----
    else {
        // 行 3 反面：取消/失败/中断必须显式标记 NotApplicable（不伪造
        // 工程判定——ERR-01；EV-ENV-1 点名反例 Canceled×Feasible）。
        if (draft.engineeringStatus != core::EngineeringStatus::NotApplicable) {
            issues.push_back(
                {EnvelopeIssueCode::NonCompletedStatusNotNotApplicable,
                 "outcome≠Completed 时 engineeringStatus 必须为 NotApplicable"
                 "（表 3 行 3/ERR-01——显式标记，不伪造判定）"});
        }

        // 行 3：正式结论字段逐项禁止——payload 必须为空（取消/失败/中断
        // 没有"正式结论"，EV-ENV-1 点名反例 Failed+payload）。
        if (draft.payload.has_value()) {
            issues.push_back(
                {EnvelopeIssueCode::NonCompletedPayloadPresent,
                 "payload 必须为空（表 3 行 3：非 Completed 不得含正式结论"
                 "字段）"});
        }

        // 行 3：清单不得含 Satisfied 判定声明（"已满足的证据"是正式结论
        // 家族——诊断性保留不等于判定声明）。
        const bool hasSatisfied = std::any_of(
            draft.evidence.items.begin(), draft.evidence.items.end(),
            [](const EvidenceItem& item) {
                return item.status == EvidenceItemStatus::Satisfied;
            });
        if (hasSatisfied) {
            issues.push_back(
                {EnvelopeIssueCode::NonCompletedSatisfiedEvidence,
                 "evidence 清单含 Satisfied 判定声明（表 3 行 3：非 Completed "
                 "不得含正式结论字段）"});
        }

        // 行 3：missingItems 不适用（缺失清单是工程判定的组成——非
        // Completed 行保留诊断即可，不得携带）。
        if (!draft.missingItems.empty()) {
            issues.push_back(
                {EnvelopeIssueCode::NonCompletedMissingItemsPresent,
                 "missingItems 不适用（表 3 行 3：非 Completed 只保留诊断与 "
                 "partialData）"});
        }
    }

    return issues;
}

// =====================================================================
// 快照绑定组合校验（表 3 全量面——接纳侧复用的同一校验器）
// =====================================================================

std::vector<EnvelopeIssue> validateCombination(const ResultEnvelopeDraft& draft,
                                               const AnalysisSnapshot& snapshot,
                                               const IProducerRegistryView& producers)
{
    // 先跑上下文无关全量面：结构非法的草稿没有绑定核对意义（快照绑定
    // 检查的语义前提是"包络自身成立"）——首错优先，检查序确定。
    std::vector<EnvelopeIssue> issues = validateCombination(draft);
    if (!issues.empty()) {
        return issues;
    }

    // ---- 工况子集核对（§7.1 caseScope 注释"⊆ 快照 caseSet"）：逐条
    // 核对覆盖工况是否在来源快照冻结必验工况集内；越界 id 逐条点名
    // （可定位——错误工况引用的观测面，EV-COV-2 同源纪律）。 ----
    for (const CaseId& caseId : draft.caseScope.caseIds) {
        const bool inSnapshot = std::any_of(
            snapshot.caseSet.entries.begin(), snapshot.caseSet.entries.end(),
            [&caseId](const CaseEntry& entry) { return entry.caseId == caseId; });
        if (!inSnapshot) {
            issues.push_back(
                {EnvelopeIssueCode::CaseScopeNotInSnapshot,
                 "caseScope 含快照冻结必验工况集外的工况 id: "
                 + caseId.toCanonical() + "（§7.1——caseScope ⊆ 快照 caseSet）"});
        }
    }

    // ---- 证明有效性核对（表 3 行 1"有效证明（validateProof 通过）"）：
    // 仅在"Completed+EngineeringInfeasible 且凭据为证明"路径执行——
    // D-09 存在性≠有效性，字段级校验对（快照, 本包络切片, 注册表）核对；
    // 凭据为 Must 违例记录时不需证明（表 3 行 1"或"）。 ----
    if (draft.outcome == core::TaskOutcome::Completed
        && draft.engineeringStatus
               == core::EngineeringStatus::EngineeringInfeasible
        && draft.infeasibilityProof.has_value()) {
        const bool mustRecordPresent = std::any_of(
            draft.diagnostics.begin(), draft.diagnostics.end(),
            [](const core::DiagnosticRecord& r) {
                return isMustViolationRecord(r);
            });
        if (!mustRecordPresent) {
            // expectedSliceId＝本包络的切片绑定（EV-T05 I-5 口径——校验
            // 不可静默跳过）；问题清单逐条拼接进 issue 消息（可定位）。
            const auto proofIssues = validateProof(*draft.infeasibilityProof,
                                                   producers, snapshot,
                                                   draft.sliceId);
            if (!proofIssues.empty()) {
                std::ostringstream what;
                what << "infeasibilityProof 未通过 validateProof（表 3 行 1"
                     << "——须为有效证明；共 " << proofIssues.size()
                     << " 条问题: ";
                for (std::size_t i = 0; i < proofIssues.size(); ++i) {
                    if (i != 0) {
                        what << "；";
                    }
                    what << proofIssues[i].message;
                }
                what << "）";
                issues.push_back(
                    {EnvelopeIssueCode::ProofInvalidAgainstSnapshot,
                     what.str()});
            }
        }
    }

    // ---- 清单对快照绑定核对（§6.2"防错误工况/对象引用"）：Completed
    // 行的清单在接纳侧用快照事实面复核（EV-T06 ②级门禁同一校验器——
    // 汇总段已挡一次，接纳段复检＝纵深防御，§7.1"各校验一次"）。 ----
    if (draft.outcome == core::TaskOutcome::Completed) {
        const auto bindingIssues =
            validateEvidenceManifestBinding(draft.evidence, snapshot);
        if (!bindingIssues.empty()) {
            std::ostringstream what;
            what << "evidence 清单对快照绑定校验失败（§6.2；共 "
                 << bindingIssues.size() << " 条: ";
            for (std::size_t i = 0; i < bindingIssues.size(); ++i) {
                if (i != 0) {
                    what << "；";
                }
                what << bindingIssues[i].message;
            }
            what << "）";
            issues.push_back(
                {EnvelopeIssueCode::CompletedManifestBindingInvalid,
                 what.str()});
        }
    }

    return issues;
}

// =====================================================================
// make()——构造边界（SA-13：唯一入口；非法组合抛 EvidenceError）
// =====================================================================

ResultEnvelope ResultEnvelope::make(ResultEnvelopeDraft&& draft)
{
    // 第一步：全量上下文无关校验（表 3 上下文无关格子——检查序固定）。
    // 任一问题即 fail-fast 抛 EvidenceError（调用方契约违约——§2.1 异常
    // 轨；detail 携带逐条字段名消息，EV-ENV-1 观测面）。草稿此时不承诺
    // 保留内容（移动语义——需要重试的调用方自备副本）。
    const std::vector<EnvelopeIssue> issues = validateCombination(draft);
    if (!issues.empty()) {
        throw EvidenceError(EvidenceErrorCode::EnvelopeIllegalCombination,
                            joinIssueMessages(issues));
    }

    // 第二步：组装正式包络（草稿字段逐一移动——make 是唯一生产者；
    // 此后包络按不可变对待，§7.2"永不回写"）。
    ResultEnvelope envelope;
    envelope.task = std::move(draft.task);
    envelope.evaluationKey = std::move(draft.evaluationKey);
    envelope.evaluatorContractVersion = draft.evaluatorContractVersion;
    envelope.mode = draft.mode;
    envelope.snapshotId = draft.snapshotId;
    envelope.sliceId = draft.sliceId;
    envelope.inputBaselineId = draft.inputBaselineId;
    envelope.caseScope = std::move(draft.caseScope);
    envelope.profile = std::move(draft.profile);
    envelope.outcome = draft.outcome;
    envelope.engineeringStatus = draft.engineeringStatus;
    envelope.evidence = std::move(draft.evidence);
    envelope.infeasibilityProof = std::move(draft.infeasibilityProof);
    envelope.searchRecord = std::move(draft.searchRecord);
    envelope.missingItems = std::move(draft.missingItems);
    envelope.partialData = std::move(draft.partialData);
    envelope.diagnostics = std::move(draft.diagnostics);
    envelope.producer = std::move(draft.producer);

    // 第三步：载荷摘要计算（CR-02：摘要算法唯一＝core::ContentDigester；
    // §7.1 digest 注释"evidence 计算摘要"——不接受申报值，对 canonical
    // 字节重算，消费方可重算比对作为完整性凭据。空字节合法——SHA-256
    // 对空串有确定摘要，域载荷是否允许空编码由域契约裁决）。
    if (draft.payload.has_value()) {
        DomainPayload payload;
        payload.kindToken = std::move(draft.payload->kindToken);
        payload.canonicalBytes = std::move(draft.payload->canonicalBytes);
        core::ContentDigester digester;
        digester.update(payload.canonicalBytes.data(),
                        payload.canonicalBytes.size());
        payload.digest.bytes = digester.finalize();
        envelope.payload = std::move(payload);
    }

    return envelope;
}

}  // namespace sdurws::ird::evidence
