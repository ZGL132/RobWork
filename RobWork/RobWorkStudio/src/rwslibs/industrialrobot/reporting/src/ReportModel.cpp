/**
 * @file   ReportModel.cpp
 * @brief  ReviewReport 数据模型实现——状态/渲染提示/限定语 token 表、
 *         §4.2 非法实例字段校验原语（firstFieldViolation）、身份计算与
 *         make() 唯一生产者。
 *
 * 设计依据：
 *   - units/reporting.md §4.2（字段表＋非法实例矩阵＋"生成状态/归档状态
 *     的表达方式"）、§4.3 引用类型字段表、§4.6 合法组合矩阵、§5.3/§9.2
 *     （token 词表）、§6.3（当前性 presence 纪律）、§6.4（限定语词表）、
 *     §4.4（身份计算归 ReportCodec——调用方不可申报）
 *   - 需求 RPT-01（明确 ID/只读）、NFR-COR-04（追溯锚）、NFR-COR-02
 *     （同输入同结论）、ERR-01（不伪造）、RPT-05（限定语数据驱动）
 *   - 任务契约 tasks/foundation/RPT-T03.json acceptance 1（身份确定性）、
 *     acceptance 2（身份与路径/名称分离）、acceptance 3（不可变＋字段
 *     校验原语——端到端构建拒绝路径随 RPT-T05 构建器收口，本任务交付
 *     字段校验形态）
 *
 * 错误语义（AGENTS.md）：字段违约＝数据错误（DataInvalid/LevelConflict/
 * EvidenceRefInvalid/SourceMissing/ScopeInsufficient——§3.5 码面），以
 * ReportError fail-fast 表达；detail 携带字段定位（供开发诊断）。
 * 检查面为**上下文无关字段校验**：需要外部事实面的拒绝（结果未 finalize/
 * envelope 绑定不一致/占用词表全量校验）随 RPT-T05 构建器收口——契约
 * acceptance 3 原文口径，本文件不越权提前实现构建器语义。
 *
 * 确定性（NFR-COR-02）：token 表为编译期固定 switch 全枚举；校验为纯
 * 函数（同输入同首个违例码）；make() 的身份计算经 ReportCodec 纯函数。
 *
 * 线程安全：全部函数无共享可变状态（可重入）。
 */

#include <sdurws/ird/reporting/ReportModel.hpp>

#include <cmath>
#include <map>
#include <utility>

#include "ReportCodec.hpp"   // 单元内私有头（§3.1 src/ 行——身份计算唯一入口）

namespace sdurws::ird::reporting {

// =====================================================================
// token 表（编译期固定 switch 全枚举——同值同串，NFR-COR-02）
// =====================================================================

std::string_view token(SectionStatus status) noexcept
{
    // §5.3 token 列原文（kebab 串）——持久化契约，不改名。
    switch (status) {
    case SectionStatus::Populated:        return "populated";
    case SectionStatus::NoFormalResult:   return "no-formal-result";
    case SectionStatus::DataInsufficient: return "data-insufficient";
    case SectionStatus::NotApplicable:    return "not-applicable";
    }
    return "unknown";   // 防御（全枚举 switch 不可达——新增值未登记时编译告警暴露）
}

std::optional<SectionStatus> trySectionStatusFromToken(std::string_view token) noexcept
{
    // 恰接受四规范字面（大小写敏感——与 Id128 小写 hex 同口径：非规范输入
    // 在解析边界拒绝而非归一化，避免持久化文本二义）。
    if (token == "populated") { return SectionStatus::Populated; }
    if (token == "no-formal-result") { return SectionStatus::NoFormalResult; }
    if (token == "data-insufficient") { return SectionStatus::DataInsufficient; }
    if (token == "not-applicable") { return SectionStatus::NotApplicable; }
    return std::nullopt;
}

SectionStatus sectionStatusFromToken(std::string_view token)
{
    // 解码边界（持久化文本回读）fail-fast——可恢复路径用 try 轨（§1.4）。
    if (const auto status = trySectionStatusFromToken(token)) {
        return *status;
    }
    throw ReportError(ReportErrorCode::DataInvalid,
                      "ReportModel: 章节状态 token 非规范字面（期望 populated/"
                      "no-formal-result/data-insufficient/not-applicable）");
}

std::string_view token(RenderHint hint) noexcept
{
    // §9.2 注释列 kebab 形（table/curve-ref/diff-table/metadata-block）。
    switch (hint) {
    case RenderHint::Table:         return "table";
    case RenderHint::CurveRef:      return "curve-ref";
    case RenderHint::DiffTable:     return "diff-table";
    case RenderHint::MetadataBlock: return "metadata-block";
    }
    return "unknown";
}

std::string_view token(QualifierToken qualifier) noexcept
{
    // §6.4 token 列原文——RPT-05 措辞冻结的词面（渲染器映射目标词表）。
    switch (qualifier) {
    case QualifierToken::Estimated:                    return "estimated";
    case QualifierToken::DataInsufficient:             return "data-insufficient";
    case QualifierToken::ExternalValidationIncomplete: return "external-validation-incomplete";
    case QualifierToken::DowngradedReferenceValue:     return "downgraded-reference-value";
    case QualifierToken::ScreeningOnly:                return "screening-only";
    case QualifierToken::HistoricalSuperseded:         return "historical-superseded";
    case QualifierToken::NotApplicable:                return "not-applicable";
    case QualifierToken::Interrupted:                  return "interrupted";
    case QualifierToken::Canceled:                     return "canceled";
    case QualifierToken::Failed:                       return "failed";
    }
    return "unknown";
}

// =====================================================================
// 字段校验原语（acceptance 3——§4.2 非法实例判定的非抛出核心）
// =====================================================================

namespace {

/// 全零判定（Digest256 保留值纪律——core §4.2：全零＝空）。
bool isZeroDigest(const core::Digest256& d)
{
    for (const std::uint8_t b : d) {
        if (b != 0) {
            return false;
        }
    }
    return true;
}

/// 记录违例定位（detail 非空时写入——firstFieldViolation 传 nullptr 时
/// 跳过拼装，纯判定路径零字符串开销）。
void note(std::string* detail, std::string text)
{
    if (detail != nullptr) {
        *detail = std::move(text);
    }
}

/// entryKey 词形闸门（§9.2 原文语法 [a-z0-9.-]{2,63}——节内稳定键的
/// 持久化下限；词表内容归 §5.1/各域，本闸门只做词形）。
bool isValidEntryKey(std::string_view key)
{
    if (key.size() < 2 || key.size() > 63) {
        return false;
    }
    for (const char c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'
                        || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// 当前性快照 presence 纪律（§6.3 字段表——字段校验原语的共享子例程）：
///   - status==nullopt ⇒ unevaluableNote 必填（不得默认 Current——EV-CUR-2）
///     且 reasons 恒空；
///   - status==Current ⇒ reasons 恒空；
///   - status==Superseded ⇒ reasons 任意（失效原因清单）。
/// 返回 nullopt＝通过。
std::optional<ReportErrorCode> checkCurrentness(const CurrentnessSnapshot& c,
                                                std::string* detail, const std::string& where)
{
    // 判定所相对的 HEAD 修订必须是明确 ID（§6.3——相对哪个上下文计算）。
    if (!c.evaluatedAgainst.headRevision.isValid()) {
        note(detail, where + ": currentness.evaluatedAgainst.headRevision 为空保留值");
        return ReportErrorCode::DataInvalid;
    }
    if (!c.status.has_value()) {
        // 不可判定（计算形态）——必须以诊断表达"无法判定"，且无失效原因。
        if (!c.unevaluableNote.has_value()) {
            note(detail, where + ": currentness.status 为空（不可判定）时 unevaluableNote 必填"
                            "（不得默认 Current——§6.3/EV-CUR-2）");
            return ReportErrorCode::DataInvalid;
        }
        if (!c.reasons.empty()) {
            note(detail, where + ": currentness 不可判定时 reasons 必须为空"
                            "（reasons 仅 Superseded 非空——§6.3）");
            return ReportErrorCode::DataInvalid;
        }
    } else if (*c.status == evidence::CurrentnessStatus::Current && !c.reasons.empty()) {
        // Current 恒无失效原因（§6.3"reasons 仅在 status==Superseded 时非空"）。
        note(detail, where + ": currentness=Current 时 reasons 必须为空（§6.3）");
        return ReportErrorCode::DataInvalid;
    }
    return std::nullopt;
}

/// 诊断引用的下限校验（§4.3.3——code/sourceSection 是稳定定位面）。
std::optional<ReportErrorCode> checkDiagRef(const DiagRefEntry& d, std::string* detail,
                                            const std::string& where)
{
    if (d.code.empty()) {
        note(detail, where + ": 诊断引用 code 为空（稳定码必填——§4.3.3）");
        return ReportErrorCode::DataInvalid;
    }
    if (d.sourceSection.empty()) {
        note(detail, where + ": 诊断引用 sourceSection 为空（来源章节必填——§4.3.3）");
        return ReportErrorCode::DataInvalid;
    }
    return std::nullopt;
}

/// 从报告字段抽取 §4.4 ReportCodec-Data 编码主体（dataIdentity 进入编码
/// 字段集——逐字段对应 §4.4 字段表"进入编码"列，排除列字段不进入）。
ReportSourceSpec extractDataSpec(const ReviewReportFields& f)
{
    ReportSourceSpec spec;
    spec.project = f.project;
    spec.branch = f.branch;
    spec.revision = f.revision;
    spec.revisionSeq = f.revisionSeq;
    spec.level = f.level;
    spec.snapshotId = f.snapshotId;
    spec.inputSliceId = f.inputSliceId;
    spec.resultRefs.reserve(f.resultRefs.size());
    for (const auto& r : f.resultRefs) {
        // §4.4：resultRefs 只取 (runId, snapshotId, sliceId, inputBaselineId,
        // caseScope) 子集——mode/outcome/资格/当前性等结果事实不入数据源
        // 身份（数据基准≠评估结论，§4.1 dataIdentity 行）。
        ResultDataSourceRef ref;
        ref.runId = r.runId;
        ref.snapshotId = r.snapshotId;
        ref.sliceId = r.sliceId;
        ref.inputBaselineId = r.inputBaselineId;
        ref.caseScope = r.caseScope;
        spec.resultRefs.push_back(std::move(ref));
    }
    spec.unitPreference = f.unitPreference;
    // 选中章节 (sectionId, selected) 集——按报告章节存储序（order 严格递增
    // 已由校验保证，序确定）。
    spec.selectedSections.reserve(f.sections.size());
    for (const auto& s : f.sections) {
        spec.selectedSections.push_back(SelectedSectionEntry{s.sectionId, s.selected});
    }
    return spec;
}

/// §4.2 非法实例字段校验的核心实现（一次实现两个面：detail==nullptr 时
/// 为纯判定原语 firstFieldViolation；非空时携带定位文本供 make() 抛出）。
std::optional<ReportErrorCode> validateImpl(const ReviewReportFields& f,
                                            const ReportLevelRule& rule, std::string* detail)
{
    // ---- 检查序＝§4.2 字段表行序（首个违例确定——NFR-COR-02 同输入同结论） ----

    // reportId：构建成功时分配的非零身份（§4.2/§4.1 保留值纪律）。
    if (!f.reportId.isValid()) {
        note(detail, "reportId 为空保留值（构建成功时分配——§4.2）");
        return ReportErrorCode::DataInvalid;
    }

    // level：枚举域即合法域（B/C 两值——token 校验在解析边界）。

    // 身份三元组：明确 ID 纪律（§4.2——禁止"当前/tip"占位；文本占位在
    // 请求解析边界拒绝〔SourceAmbiguous——RPT-T05〕，二进制空值在此拒绝）。
    if (!f.project.isValid() || !f.branch.isValid() || !f.revision.isValid()) {
        note(detail, "身份三元组（project/branch/revision）含空保留值——"
                     "报告只引用明确 ID（§4.2/RPT-01）");
        return ReportErrorCode::DataInvalid;
    }

    // 报告级统一快照/切片（可选——在场即必须非零）。
    if ((f.snapshotId.has_value() && !f.snapshotId->isValid())
        || (f.inputSliceId.has_value() && !f.inputSliceId->isValid())) {
        note(detail, "snapshotId/inputSliceId 在场但为空保留值（§4.2）");
        return ReportErrorCode::DataInvalid;
    }

    // resultRefs：≥1 且逐条明确（§4.2"报告绑定的明确结果集"）。
    if (f.resultRefs.empty()) {
        note(detail, "resultRefs 为空（≥1——§4.2；无结果＝无可引用的冻结事实）");
        return ReportErrorCode::DataInvalid;
    }
    for (std::size_t i = 0; i < f.resultRefs.size(); ++i) {
        const ResultRefSnapshot& r = f.resultRefs[i];
        const std::string where = "resultRefs[" + std::to_string(i) + "]";
        if (!r.runId.isValid()) {
            note(detail, where + ": runId 为空保留值");
            return ReportErrorCode::DataInvalid;
        }
        if (!r.task.isValid()) {
            note(detail, where + ": task 五元组不完整（TASK-03——evidence envelope 值拷贝）");
            return ReportErrorCode::DataInvalid;
        }
        if (r.evaluationKey.empty()) {
            note(detail, where + ": evaluationKey 为空（评估键必填——词形权威在 evidence）");
            return ReportErrorCode::DataInvalid;
        }
        if (r.mode == core::EvaluationMode::Preview) {
            // §4.6 行 5：Preview 不产生正式证据与结果对象（§8.1 表 1）——
            // 进入 resultRefs 即引用不存在的结果，SourceMissing 拒绝。
            note(detail, where + ": Preview 结果拒绝进入报告（§4.6/§8.1 表 1——不产生结果对象）");
            return ReportErrorCode::SourceMissing;
        }
        if (!r.snapshotId.isValid() || !r.sliceId.isValid() || !r.inputBaselineId.isValid()) {
            note(detail, where + ": snapshotId/sliceId/inputBaselineId 含空保留值"
                            "（envelope→snapshot 追溯链锚点——NFR-COR-04）");
            return ReportErrorCode::DataInvalid;
        }
        for (std::size_t k = 0; k < r.caseScope.size(); ++k) {
            if (!r.caseScope[k].isValid()) {
                note(detail, where + ": caseScope[" + std::to_string(k) + "] 为空保留值");
                return ReportErrorCode::DataInvalid;
            }
        }
        if (const auto bad = checkCurrentness(r.currentness, detail, where)) {
            return bad;
        }
    }

    // evidenceRefs：引用 presence 纪律（§4.3.2——evidence §6.2 呈现侧）。
    for (std::size_t i = 0; i < f.evidenceRefs.size(); ++i) {
        const EvidenceRefEntry& e = f.evidenceRefs[i];
        const std::string where = "evidenceRefs[" + std::to_string(i) + "]";
        if (e.itemId.empty()) {
            note(detail, where + ": itemId 为空（\"<域>.<项>\"词形必填）");
            return ReportErrorCode::DataInvalid;
        }
        if (e.sourceSection.empty()) {
            note(detail, where + ": sourceSection 为空（来源章节必填——可反向导航）");
            return ReportErrorCode::DataInvalid;
        }
        if (e.status == evidence::EvidenceItemStatus::Satisfied
            && (!e.artifactDigest.has_value() || isZeroDigest(*e.artifactDigest))) {
            // Satisfied＝绑定校验通过——产物摘要是其唯一凭据（§4.3.2）。
            note(detail, where + ": Satisfied 状态 artifactDigest 必填非零（§4.3.2）");
            return ReportErrorCode::DataInvalid;
        }
        if (e.status == evidence::EvidenceItemStatus::NotApplicable
            && (!e.notApplicableReason.has_value() || e.notApplicableReason->empty())) {
            // 不适用必须显式附原因（C2/ERR-01——不伪造、不计缺失的凭据）。
            note(detail, where + ": NotApplicable 状态 notApplicableReason 必填（C2/ERR-01）");
            return ReportErrorCode::DataInvalid;
        }
        if (e.status == evidence::EvidenceItemStatus::Invalid
            && (!e.invalidReasonCode.has_value() || e.invalidReasonCode->empty())) {
            // Invalid 附诊断码引用（ERR-01——有产物但绑定校验失败须可定位）。
            note(detail, where + ": Invalid 状态 invalidReasonCode 必填（§4.3.2/ERR-01）");
            return ReportErrorCode::DataInvalid;
        }
        if (e.caseScope.has_value()) {
            for (std::size_t k = 0; k < e.caseScope->size(); ++k) {
                if (!(*e.caseScope)[k].isValid()) {
                    note(detail, where + ": caseScope[" + std::to_string(k) + "] 为空保留值");
                    return ReportErrorCode::DataInvalid;
                }
            }
        }
        if (e.subject.has_value() && !e.subject->isValid()) {
            note(detail, where + ": subject 为空保留值");
            return ReportErrorCode::DataInvalid;
        }
    }

    // diagRefs：下限校验（§4.3.3；reportable 过滤归诊断投影源）。
    for (std::size_t i = 0; i < f.diagRefs.size(); ++i) {
        if (const auto bad = checkDiagRef(f.diagRefs[i], detail,
                                          "diagRefs[" + std::to_string(i) + "]")) {
            return bad;
        }
    }

    // currentnessSummary：逐结果当前性快照（§4.2——生成时刻冻结）。
    for (std::size_t i = 0; i < f.currentnessSummary.perResult.size(); ++i) {
        const ResultCurrentnessEntry& e = f.currentnessSummary.perResult[i];
        const std::string where = "currentnessSummary.perResult[" + std::to_string(i) + "]";
        if (!e.runId.isValid()) {
            note(detail, where + ": runId 为空保留值");
            return ReportErrorCode::DataInvalid;
        }
        if (const auto bad = checkCurrentness(e.currentness, detail, where)) {
            return bad;
        }
    }

    // sections：≥1、order 严格递增、sectionId 唯一（§4.2/§4.7）。
    if (f.sections.empty()) {
        note(detail, "sections 为空（≥1——§4.2；报告至少含章节骨架）");
        return ReportErrorCode::DataInvalid;
    }
    std::map<std::string_view, std::size_t> sectionIndex;
    for (std::size_t i = 0; i < f.sections.size(); ++i) {
        const ReviewReportSection& s = f.sections[i];
        const std::string where = "sections[" + std::to_string(i) + "]";
        if (s.sectionId.empty()) {
            note(detail, where + ": sectionId 为空（§5.1 词表 token）");
            return ReportErrorCode::DataInvalid;
        }
        if (i > 0 && f.sections[i - 1].order >= s.order) {
            // 严格递增（§4.7——渲染与 CSV/JSON 输出序唯一依据；编码入口
            // 二次把关，此处为语义源）。
            note(detail, where + ": order 未严格递增（§4.7——前一章节 order "
                              + std::to_string(f.sections[i - 1].order) + "≥本章节 "
                              + std::to_string(s.order) + "）");
            return ReportErrorCode::DataInvalid;
        }
        if (!sectionIndex.emplace(s.sectionId, i).second) {
            note(detail, where + ": sectionId 重复 '" + s.sectionId + "'（§4.7 唯一性）");
            return ReportErrorCode::DataInvalid;
        }
        // 状态-内容对应（§4.3.4/§5.3）：
        if (s.status == SectionStatus::Populated && s.entries.empty()) {
            // Populated＝有正式内容——至少 1 条目（§4.3.4 entries 必填）。
            note(detail, where + ": Populated 状态 entries 必填非空（§4.3.4/§5.3）");
            return ReportErrorCode::DataInvalid;
        }
        if ((s.status == SectionStatus::NoFormalResult
             || s.status == SectionStatus::DataInsufficient)
            && s.missingItems.empty()) {
            // 缺正式结果/数据不足——缺项**全量**清单必填（不因首个缺失
            // 短路，§8.1 表 2④ 呈现口径）。
            note(detail, where + ": NoFormalResult/DataInsufficient 状态 missingItems 必填"
                            "（缺项全量清单——§4.3.4）");
            return ReportErrorCode::DataInvalid;
        }
        // 缺项条目：itemId＋原因（§4.3.4——ERR-01 不伪造）。
        for (std::size_t k = 0; k < s.missingItems.size(); ++k) {
            if (s.missingItems[k].itemId.empty() || s.missingItems[k].reason.empty()) {
                note(detail, where + ": missingItems[" + std::to_string(k)
                                  + "] itemId/reason 不得为空（§4.3.4）");
                return ReportErrorCode::DataInvalid;
            }
        }
        // 章节诊断（同 §4.3.3 下限）。
        for (std::size_t k = 0; k < s.diagnostics.size(); ++k) {
            if (const auto bad =
                    checkDiagRef(s.diagnostics[k], detail,
                                 where + ".diagnostics[" + std::to_string(k) + "]")) {
                return bad;
            }
        }
        // 章节级当前性快照（在场即校验）。
        if (s.currentness.has_value()) {
            if (const auto bad = checkCurrentness(*s.currentness, detail, where)) {
                return bad;
            }
        }
    }

    // 关系约束（§4.7）：sourceResults ⊆ resultRefs；绑定运行可解析；
    // 证据绑定 caseScope ⊆ 所属结果 caseScope；引用 Superseded 结果的
    // 章节必填 currentness。runId→resultRefs 下标索引（字节字典序——
    // 仅查表用）。
    std::map<core::RunId, std::size_t> runIndex;
    for (std::size_t i = 0; i < f.resultRefs.size(); ++i) {
        runIndex.emplace(f.resultRefs[i].runId, i);
    }
    bool anySupersededSource = false;   // 任意章节引用 Superseded 结果（currentness 必填总闸）
    for (std::size_t i = 0; i < f.sections.size(); ++i) {
        const ReviewReportSection& s = f.sections[i];
        const std::string where = "sections[" + std::to_string(i) + "]";
        bool sectionReferencesSuperseded = false;
        for (std::size_t k = 0; k < s.sourceResults.size(); ++k) {
            const auto it = runIndex.find(s.sourceResults[k]);
            if (it == runIndex.end()) {
                // 章节引用了报告未绑定的运行——引用错位（§4.7 关系约束；
                // 稳定码面同 §4.6 行 7 引用不符族）。
                note(detail, where + ": sourceResults[" + std::to_string(k)
                                  + "] 不在 resultRefs 内（§4.7 sourceResults ⊆ resultRefs）");
                return ReportErrorCode::EvidenceRefInvalid;
            }
            if (f.resultRefs[it->second].currentness.status
                    == evidence::CurrentnessStatus::Superseded) {
                sectionReferencesSuperseded = true;
            }
        }
        if (sectionReferencesSuperseded) {
            // §4.3.4 currentness 行："引用 Superseded 结果时必填"。
            if (!s.currentness.has_value()) {
                note(detail, where + ": 引用 Superseded 结果但 currentness 缺席（§4.3.4/§6.3）");
                return ReportErrorCode::DataInvalid;
            }
            anySupersededSource = true;
        }
        for (std::size_t k = 0; k < s.entries.size(); ++k) {
            const SectionEntryView& e = s.entries[k];
            const std::string ewhere = where + ".entries[" + std::to_string(k) + "]";
            if (!isValidEntryKey(e.entryKey)) {
                // §9.2 语法 [a-z0-9.-]{2,63}——节内稳定键持久化下限。
                note(detail, ewhere + ": entryKey 词形违约（§9.2 [a-z0-9.-]{2,63}）");
                return ReportErrorCode::DataInvalid;
            }
            for (std::size_t m = 0; m < k; ++m) {
                if (s.entries[m].entryKey == e.entryKey) {
                    // 节内唯一（§9.2"entryKey 重复→构建器拒绝"——结构违约）。
                    note(detail, ewhere + ": entryKey 重复 '" + e.entryKey + "'（§9.2）");
                    return ReportErrorCode::DataInvalid;
                }
            }
            // 字段值：NaN/非有限拒绝（§4.2 非法实例矩阵——DataInvalid）；
            // 数值/文本两侧互斥（§9.2 "{key, SourcedValue<double>|文本}"）。
            for (std::size_t m = 0; m < e.fields.size(); ++m) {
                const FieldValue& fld = e.fields[m];
                const std::string fwhere = ewhere + ".fields[" + std::to_string(m) + "]";
                if (fld.key.empty()) {
                    note(detail, fwhere + ": 字段 key 为空（字段矩阵对齐键——AT-22）");
                    return ReportErrorCode::DataInvalid;
                }
                if (fld.quantity.state() == core::FieldState::Provided
                    && !std::isfinite(fld.quantity.tryValue().value_or(0.0))) {
                    note(detail, fwhere + ": 数值字段非有限（NaN/±Inf——§4.2 DataInvalid）");
                    return ReportErrorCode::DataInvalid;
                }
                if (fld.text.has_value()
                    && fld.quantity.state() == core::FieldState::Provided) {
                    note(detail, fwhere + ": text 与 Provided 数值互斥（§9.2 字段形状）");
                    return ReportErrorCode::DataInvalid;
                }
                if (fld.unit.has_value() && !fld.unit->isValid()) {
                    note(detail, fwhere + ": 单位 token 未注册（core §4.4 唯一注册表）");
                    return ReportErrorCode::DataInvalid;
                }
            }
            // 结果绑定：可解析到 resultRefs（追溯链第一跳——RP-TRACE-1）。
            const auto bindIt = runIndex.find(e.result.runId);
            if (!e.result.runId.isValid() || e.result.fieldPath.empty()) {
                note(detail, ewhere + ": result 绑定不完整（runId/fieldPath 必填——§9.2）");
                return ReportErrorCode::DataInvalid;
            }
            if (bindIt == runIndex.end()) {
                note(detail, ewhere + ": result.runId 不可解析到 resultRefs（绑定越界——§9.2）");
                return ReportErrorCode::EvidenceRefInvalid;
            }
            const ResultRefSnapshot& bound = f.resultRefs[bindIt->second];
            // 证据绑定：caseScope ⊆ 所属结果 caseScope（§4.7 关系约束——
            // 防引用错位）。
            for (std::size_t m = 0; m < e.evidence.size(); ++m) {
                const EvidenceBinding& b = e.evidence[m];
                const std::string bwhere = ewhere + ".evidence[" + std::to_string(m) + "]";
                if (b.itemId.empty()) {
                    note(detail, bwhere + ": itemId 为空");
                    return ReportErrorCode::DataInvalid;
                }
                if (b.status == evidence::EvidenceItemStatus::Satisfied
                    && (!b.digest.has_value() || isZeroDigest(*b.digest))) {
                    note(detail, bwhere + ": Satisfied 绑定 digest 必填非零（§9.2）");
                    return ReportErrorCode::DataInvalid;
                }
                for (std::size_t n = 0; n < b.caseScope.size(); ++n) {
                    const core::ObjectId& cid = b.caseScope[n];
                    if (!cid.isValid()) {
                        note(detail, bwhere + ": caseScope[" + std::to_string(n)
                                          + "] 为空保留值");
                        return ReportErrorCode::DataInvalid;
                    }
                    bool contained = false;
                    for (const auto& scope : bound.caseScope) {
                        if (scope == cid) {
                            contained = true;
                            break;
                        }
                    }
                    if (!contained) {
                        note(detail, bwhere + ": caseScope 越界（⊆ 所属结果 caseScope——§4.7）");
                        return ReportErrorCode::EvidenceRefInvalid;
                    }
                }
            }
        }
    }
    (void)anySupersededSource;   // 总闸仅用于可读性——逐章节已就地强制

    // 级别×章节合法组合（§4.6 矩阵——词表经 ReportLevelRule 注入，词表
    // 权威归 Sections.hpp/RPT-T04，本处不复制 §5.1 清单）。
    std::size_t selectedCSections = 0;
    for (const auto& s : f.sections) {
        bool isCExclusive = false;
        for (const auto& cId : rule.cExclusiveSectionIds) {
            if (s.sectionId == cId) {
                isCExclusive = true;
                break;
            }
        }
        if (!isCExclusive) {
            continue;
        }
        if (f.level == ReportLevel::B) {
            // §4.6 行 2：B 级不能伪造 C 级章节（含 C 专属章节即拒绝——
            // 结构性防伪造，非运行期开关）。
            note(detail, "B 级报告携带 C 专属章节 '" + s.sectionId
                             + "'（LevelConflict——§4.6）");
            return ReportErrorCode::LevelConflict;
        }
        if (s.selected) {
            ++selectedCSections;
        }
    }
    if (f.level == ReportLevel::C && selectedCSections == 0) {
        // §4.6 行 4：C 级零 C 章节选中＝C 级无意义（宁可拒绝不可虚级——
        // §5.4 保守处置；降级建议诊断 RPT-SCOPE-INSUFFICIENT 随构建器上报）。
        note(detail, "C 级报告零 C 章节选中（ScopeInsufficient——§4.6/§5.4）");
        return ReportErrorCode::ScopeInsufficient;
    }

    // 评审元数据（§4.5）。
    if (!f.review.basisRevision.isValid()) {
        note(detail, "review.basisRevision 为空保留值（依据修订必填——§4.5）");
        return ReportErrorCode::DataInvalid;
    }
    if (f.review.basisSnapshot.has_value() && !f.review.basisSnapshot->isValid()) {
        note(detail, "review.basisSnapshot 在场但为空保留值（§4.5）");
        return ReportErrorCode::DataInvalid;
    }
    for (std::size_t i = 0; i < f.review.comments.size(); ++i) {
        const ReviewComment& c = f.review.comments[i];
        // text≤4KiB（§4.5 字段表原文——4096 字节上限）。
        if (c.text.size() > 4096) {
            note(detail, "review.comments[" + std::to_string(i)
                             + "]: text 超过 4KiB 上限（§4.5）");
            return ReportErrorCode::DataInvalid;
        }
    }
    // 签署 presence 纪律（§4.5——Signed 必带凭据、Unsigned 无凭据残留）。
    if (f.review.signOff.state == SignOffState::State::Signed) {
        if (f.review.signOff.signer.empty() || isZeroDigest(f.review.signOff.statementDigest)) {
            note(detail, "review.signOff=Signed 但 signer/statementDigest 缺席（§4.5）");
            return ReportErrorCode::DataInvalid;
        }
    } else {
        if (!f.review.signOff.signer.empty() || !isZeroDigest(f.review.signOff.statementDigest)) {
            note(detail, "review.signOff=Unsigned 但携带签署凭据（presence 纪律——§4.5）");
            return ReportErrorCode::DataInvalid;
        }
    }
    if (f.review.variantDiff.has_value()) {
        const VariantDiffBlock& v = *f.review.variantDiff;
        if (!v.baselineRevision.isValid() || !v.candidateRevision.isValid()
            || !v.modelDiffRef.isValid()) {
            note(detail, "review.variantDiff: 修订/对象身份含空保留值（§4.5）");
            return ReportErrorCode::DataInvalid;
        }
        for (std::size_t i = 0; i < v.tradeOffs.size(); ++i) {
            if (v.tradeOffs[i].topic.empty() || v.tradeOffs[i].rationale.empty()) {
                note(detail, "review.variantDiff.tradeOffs[" + std::to_string(i)
                                 + "]: topic/rationale 不得为空（RPT-04 取舍理由呈现义务）");
                return ReportErrorCode::DataInvalid;
            }
        }
    }
    for (std::size_t i = 0; i < f.review.changeLog.size(); ++i) {
        const ReportVersionEntry& e = f.review.changeLog[i];
        if (!e.reportId.isValid() || e.reportVersion < 1) {
            note(detail, "review.changeLog[" + std::to_string(i)
                             + "]: reportId/reportVersion 违约（§4.1 版本 ≥1）");
            return ReportErrorCode::DataInvalid;
        }
    }

    // 单位选择：值必须为已注册 token（§4.2 unitPreference——core 唯一
    // 注册表；编码入口亦有同样闸门，此处为语义源）。
    for (const auto& [kind, unit] : f.unitPreference.displayUnits) {
        if (!unit.isValid()) {
            note(detail, "unitPreference: 量纲显示单位未注册（core §4.4）");
            return ReportErrorCode::DataInvalid;
        }
    }

    // 演化链（§4.1——supersedes 指向被本版取代的报告）。
    if (f.supersedes.has_value() && !f.supersedes->isValid()) {
        note(detail, "supersedes 在场但为空保留值（§4.5 演化链）");
        return ReportErrorCode::DataInvalid;
    }
    if (f.reportVersion < 1) {
        // reportVersion（≥1——§4.1 字段表）。
        note(detail, "reportVersion < 1（§4.1）");
        return ReportErrorCode::DataInvalid;
    }

    // 章节契约版本：恰为冻结常量（§4.2——词表升级走单元卡变更记录）。
    if (f.sectionModelVersion != kSectionModelVersion) {
        note(detail, "sectionModelVersion 非当前契约版本（期望 "
                         + std::string(kSectionModelVersion) + "）");
        return ReportErrorCode::DataInvalid;
    }

    return std::nullopt;
}

}  // namespace

std::optional<ReportErrorCode> firstFieldViolation(const ReviewReportFields& fields,
                                                   const ReportLevelRule& rule)
{
    // 纯判定面：不拼装定位文本（detail==nullptr）。
    return validateImpl(fields, rule, nullptr);
}

// =====================================================================
// ReviewReport——make() 唯一生产者
// =====================================================================

ReviewReport ReviewReport::make(ReviewReportFields fields, const ReportLevelRule& rule)
{
    // 第一步：§4.2 非法实例字段校验（任一违例 fail-fast——不存在"半份
    // 报告"，§4.2 生成状态表达方式①）。
    std::string detail;
    if (const auto code = validateImpl(fields, rule, &detail)) {
        throw ReportError(*code, "ReviewReport::make: " + detail);
    }

    // 第二步：身份计算（§4.2"dataIdentity/contentIdentity 由构建器计算，
    // 调用方不可申报"——make() 内部经 ReportCodec 纯函数计算，初值类型
    // 不携带身份字段，申报通道在类型层面切断）。
    //   dataIdentity ＝ ReportCodec-Data（§4.4 数据源字段集——评审演化链
    //   上不变）；contentIdentity ＝ ReportCodec-Full（§4.4 全语义内容——
    //   幂等导出与冲突判定唯一依据）。生成时间/者/生成器版本不入任何
    //   身份（§4.4 排除列——同内容重建身份一致的基础，RP-MDL-1/3 的
    //   类型级前提）。
    const ReportSourceSpec spec = extractDataSpec(fields);
    ReportFullFields full;
    full.data = spec;
    full.sections = fields.sections;
    full.review = fields.review;
    full.sectionModelVersion = fields.sectionModelVersion;
    const core::ContentIdentity dataIdentity = ReportCodec::digestData(spec);
    const core::ContentIdentity contentIdentity = ReportCodec::digestFull(full);

    // 第三步：身份非零自证（§4.2 合法实例"contentIdentity 非零"；SHA-256
    // 全零在实践上不可达——防御性检查保留，违者视为实现违约 fail-fast）。
    if (!dataIdentity.isValid() || !contentIdentity.isValid()) {
        throw ReportError(ReportErrorCode::DataInvalid,
                          "ReviewReport::make: 身份摘要为全零（实现违约——SHA-256 不应产出全零）");
    }

    return ReviewReport(std::move(fields), dataIdentity, contentIdentity);
}

ReviewReport::ReviewReport(ReviewReportFields&& fields, core::ContentIdentity dataIdentity,
                           core::ContentIdentity contentIdentity)
    : m_reportId(fields.reportId)
    , m_level(fields.level)
    , m_project(fields.project)
    , m_branch(fields.branch)
    , m_revision(fields.revision)
    , m_revisionSeq(fields.revisionSeq)
    , m_snapshotId(std::move(fields.snapshotId))
    , m_inputSliceId(std::move(fields.inputSliceId))
    , m_resultRefs(std::move(fields.resultRefs))
    , m_evidenceRefs(std::move(fields.evidenceRefs))
    , m_diagRefs(std::move(fields.diagRefs))
    , m_currentnessSummary(std::move(fields.currentnessSummary))
    , m_coverageSummary(fields.coverageSummary)
    , m_externalResourceSummary(std::move(fields.externalResourceSummary))
    , m_reproduction(std::move(fields.reproduction))
    , m_sections(std::move(fields.sections))
    , m_review(std::move(fields.review))
    , m_unitPreference(std::move(fields.unitPreference))
    , m_generatedAtUtc(fields.generatedAtUtc)
    , m_generatedBy(std::move(fields.generatedBy))
    , m_generatorVersion(std::move(fields.generatorVersion))
    , m_supersedes(std::move(fields.supersedes))
    , m_reportVersion(fields.reportVersion)
    , m_dataIdentity(dataIdentity)
    , m_contentIdentity(contentIdentity)
    , m_sectionModelVersion(std::move(fields.sectionModelVersion))
{
}

bool ReviewReport::operator==(const ReviewReport& o) const
{
    // 全字段精确相等（身份为字节等值；生成信息参与对象全等——身份相等
    // 与对象全等按 §4.1 两概念区分使用）。
    return m_reportId == o.m_reportId && m_level == o.m_level && m_project == o.m_project
           && m_branch == o.m_branch && m_revision == o.m_revision
           && m_revisionSeq == o.m_revisionSeq && m_snapshotId == o.m_snapshotId
           && m_inputSliceId == o.m_inputSliceId && m_resultRefs == o.m_resultRefs
           && m_evidenceRefs == o.m_evidenceRefs && m_diagRefs == o.m_diagRefs
           && m_currentnessSummary == o.m_currentnessSummary
           && m_coverageSummary == o.m_coverageSummary
           && m_externalResourceSummary == o.m_externalResourceSummary
           && m_reproduction == o.m_reproduction && m_sections == o.m_sections
           && m_review == o.m_review && m_unitPreference == o.m_unitPreference
           && m_generatedAtUtc == o.m_generatedAtUtc && m_generatedBy == o.m_generatedBy
           && m_generatorVersion == o.m_generatorVersion && m_supersedes == o.m_supersedes
           && m_reportVersion == o.m_reportVersion && m_dataIdentity == o.m_dataIdentity
           && m_contentIdentity == o.m_contentIdentity
           && m_sectionModelVersion == o.m_sectionModelVersion;
}

}  // namespace sdurws::ird::reporting
