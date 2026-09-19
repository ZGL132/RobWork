/**
 * @file   Builder.cpp
 * @brief  报告构建器实现——§7.2 步①~⑦编排（锚定→结果解析→资格与当前性
 *         快照→章节组装与绑定校验→C 级范围判定→冻结）＋会话状态机。
 *
 * 设计依据：
 *   - units/reporting.md §7.1（生成主流程/状态机/逐项明确表——锚定纪律、
 *     零项目写、冻结时刻、取消与清理、内容幂等）、§7.2（流程图步①~⑦）、
 *     §9.1（契约表：错误全集/评审演化/确定性/副作用零 I/O）、§4.6（级别×
 *     章节合法组合矩阵——Preview/非 Completed 结果/EvidenceRefInvalid 的
 *     构建期拒绝）、§6.2/§6.3（资格与当前性冻结——不自算、不默认 Current、
 *     非 Completed 结果仅诊断/状态呈现）、§5.4（C 级保守处置——P-RPT-5）、
 *     §5.3（章节状态进入条件与缺项表达）、§4.3.2/§4.3.3（证据/诊断引用
 *     并集的 presence 纪律）
 *   - 需求 RPT-01-B、NFR-COR-04、TASK-02、CON-02（任务契约 requirements
 *     列——各红线在拒绝点的注释逐条标注）
 *   - 任务契约 tasks/foundation/RPT-T05.json acceptance 1~6（逐条拒绝点/
 *     冻结点/锚定点在代码内以 "acceptance N" 标注，供验收对照）
 *
 * 实现要点（对应单元卡 §7.2 流程图步号）：
 *   ①锚定：queryPort.tryRevision(revision) 恰一次（修订视图不可变——PA-2/
 *     §7.1"一致读取视图"行）；②结果集：listRuns(revision) 恰一次（构建
 *     时刻快照——§7.1"生成期间项目后台写入"行"构建前枚举的 finalize 运行
 *     集"；此后 HEAD 前进/迟到结果〔AT-10〕不影响已锚定视图、不入本报告，
 *     acceptance 4）；③逐结果：envelope→资格→当前性（三个注入源调用，
 *     取消检查点每结果一个）；④模型摘要：随 RPT-T12（IModelSummaryProvider
 *     未落位——框架章节边界登记于 Builder.hpp 类注与单元卡 §14.4 v0.8）；
 *     ⑤快照一致性；⑥章节组装＋逐条目绑定校验；⑦冻结：ReviewReport::make
 *     （字段校验→ReportCodec 身份计算→不可变对象——冻结时刻＝build() 返回）。
 *
 * 错误语义（AGENTS.md 错误二分在本单元的落点）：
 *   - 调用方契约违约（请求字段非法/演化种子不一致/对终态会话推进）→
 *     ReportError(Usage) fail-fast；
 *   - 来源/数据错误（修订不存在/结果未 finalize/绑定错位/范围不足）→
 *     对应稳定码 ReportError＋构建诊断（NFR-COR-03 不静默——每个失败路径
 *     同时携带 error 与诊断；取消除外〔UX-03 正常取消非错误——error 缺席〕）。
 *
 * 线程契约：build 并发安全（无共享可变状态——全部中间态为栈上局部值）；
 * ReportBuildSession 单线程（每次调用独立实例——§9.1 维度表）。
 * 确定性：同输入同 contentIdentity（结果集/章节按词表与规范文本排序——
 * 集合语义规范化；生成时间/者/ReportId 不入身份——§4.4 排除列）。
 */

#include <sdurws/ird/reporting/Builder.hpp>
// ReportCodec 为单元内私有实现头（src/ 相对路径包含——R-2 红线：私有头
// 不出 include/，跨单元只暴露公共头）。消费点：评审演化种子的 dataIdentity
// 一致性校验（§9.1 维度表"构建器校验旧报告 dataIdentity 与本次数据源一致"）。
#include "ReportCodec.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <set>
#include <utility>

namespace sdurws::ird::reporting {

namespace {

// =====================================================================
// 内部常量（构建期一次性使用——静态存储期字面量，NFR-COR-02 同值同串）
// =====================================================================

/// 构建器版本（ReviewReport.generatorVersion——生成信息记录于工件清单、
/// 不入任何身份〔§4.4 排除列/D-04〕；token 形态与单元版本串同风格）。
constexpr std::string_view kGeneratorVersion = "ird-report-builder/1";

/// 报告级诊断的来源章节（§4.3.3 sourceSection 必填——报告级诊断〔结果当前
/// 性不可判定/包络诊断等〕在诊断章节呈现："诊断引用汇总（诊断章节与各章节
/// 诊断的并集）"，§4.2 diagRefs 行；token＝§5.1 词表 kSectionDiagnostics）。
constexpr std::string_view kReportLevelDiagSection = "diagnostics";

/// 构建器判定的"缺正式结果"缺项原因（§5.3 NoFormalResult 进入条件"该域
/// 无任何可引用的已完成结果"——提供方在册但过滤后无结果时的构建器判定
/// 文案；确定性文本）。
constexpr std::string_view kNoFormalResultReason =
    "缺正式结果（无已 finalize 的该域评估运行——listRuns 口径）";

// =====================================================================
// 小工具（纯函数——确定性）
// =====================================================================

/// 进度上报（progress 为空即跳过——§9.1 签名可选参数；回调在 build() 调用
/// 线程同步执行，stage 为静态存储期字面量，实现方不得留存指针）。
void emitProgress(IReportProgressSink* progress, std::uint64_t done, std::uint64_t total,
                  const char* stage)
{
    if (progress != nullptr) {
        ReportProgress snapshot;
        snapshot.done = done;
        snapshot.total = total;
        snapshot.stage = stage;
        progress->progress(snapshot);
    }
}

/// 工况 id 是否在给定范围内（§4.7 关系约束"caseScope ⊆ 所属结果 caseScope"
/// 的线性核对——单结果工况集为小集合，构建期一次性，非热点）。
bool caseContained(const std::vector<core::ObjectId>& scope, const core::ObjectId& caseId)
{
    for (const core::ObjectId& candidate : scope) {
        if (candidate == caseId) {
            return true;
        }
    }
    return false;
}

/// 报告级稳定诊断记录工厂（构建失败路径的诊断上报——NFR-COR-03"任何失败
/// 都不得静默转换为默认通过"；码面＝diagcodes 登记清单引用，码值权威归
/// diagnostics StableCodeRegistry）。
///
/// @param code   [in] RPT-* 稳定码字面（diagcodes::* 常量）
/// @param cause  [in] 失败原因（非空——core make() 的 C-3 校验）
/// @param action [in] 建议动作（非空——C-3 校验）
/// @return 诊断记录（context 固定标注构建阶段——可定位到 §7.2 流程）
core::DiagnosticRecord buildDiagnostic(std::string_view code, std::string cause,
                                       std::string action)
{
    // core make() 的 C-3 校验（码句法＋必填串非空）在此兜底：常量码面＋
    // 调用点提供的文案，违约即实现缺陷 fail-fast。
    return core::DiagnosticRecord::make(std::string(code), std::nullopt, std::nullopt,
                                        std::nullopt, "报告构建（§7.2 流程）",
                                        std::move(cause), std::move(action), std::nullopt);
}

/// 诊断引用的码表元数据解析结果（reportable 过滤＋severity/category 补全
/// ——§4.3.3"severity/category＝码表元数据"的注册表消费面）。
struct CodeMeta {
    bool reportable = true;                     ///< 是否允许进入报告（§4.3.3）
    diagnostics::DiagnosticSeverity severity{}; ///< 码表登记严重级别
    diagnostics::DiagnosticCategory category{}; ///< 码表登记分类
};

/// 码表元数据查询（diagnostics StableCodeRegistry 只读——C-3 消费行；
/// 注册表未装配（registry==nullptr）或码未登记时返回默认值并 reportable=
/// true（保守放行——码值权威在注册表，缺席＝装配面问题而非报告数据错误，
/// 按 diagnostics §4.5"码未注册→占位＋开发诊断（不崩溃）"口径处理；
/// 渲染层以注册表补全元数据，本单元不私设第二权威——PA-1）。
CodeMeta lookupCodeMeta(const diagnostics::IDiagnosticRegistry* registry,
                        const core::DiagCode& code)
{
    CodeMeta meta;
    if (registry == nullptr) {
        return meta;
    }
    const diagnostics::CodeDescriptor* descriptor = registry->find(code);
    if (descriptor == nullptr) {
        return meta;
    }
    meta.reportable = descriptor->reportable;
    meta.severity = descriptor->severity;
    meta.category = descriptor->category;
    return meta;
}

/// 由 core 诊断记录构造报告诊断引用（§4.3.3 字段表——码表元数据经注册表
/// 解析；reportable=false 的码不进报告——diagnostics §4.5，返回 false 表示
/// 已过滤）。
bool makeDiagRef(const diagnostics::IDiagnosticRegistry* registry,
                 const core::DiagnosticRecord& record, std::string sourceSection,
                 std::optional<core::RunId> sourceRun, DiagRefEntry& out)
{
    const CodeMeta meta = lookupCodeMeta(registry, record.code);
    if (!meta.reportable) {
        return false;   // §4.3.3：reportable=false 的码不进报告（消费侧过滤）
    }
    out = DiagRefEntry{};
    out.code = record.code;
    out.severity = meta.severity;
    out.category = meta.category;
    out.subject = record.subject;
    out.localName = record.localName;
    out.runtimeName = record.runtimeName;
    out.comparison = record.comparison;
    out.sourceSection = std::move(sourceSection);
    out.sourceRun = std::move(sourceRun);
    return true;
}

/// 诊断引用去重并集容器（§4.2 diagRefs"诊断引用汇总……并集"＋§4.3.3
/// occurrences"去重计数（聚合不吞缺失项）"）。
///
/// 去重键＝诊断引用的全部定位字段（码/主体/局部名/运行时名/来源章节/
/// 来源运行）；同键合并 occurrences 累加（不吞任何一条——每条记录都计入
/// 计数）。比较三要素不进键：同码同主体的比较型诊断逐实例独立（三要素是
/// 事实差异，不是同一诊断的重复发生）。合并序＝首次出现序（确定性——
/// 输入序由结果集排序与章节词表序保证，NFR-COR-02）。
class DiagRefMerger {
public:
    /// 追加一条引用（单条 occurrences=1；同键合并累加）。
    void add(DiagRefEntry entry)
    {
        entry.occurrences = 1;
        const std::string key = mergeKey(entry);
        const auto it = m_index.find(key);
        if (it == m_index.end()) {
            m_index.emplace(key, m_entries.size());
            m_entries.push_back(std::move(entry));
            return;
        }
        m_entries[it->second].occurrences += 1;   // 同键去重——计数累加不吞项
    }

    /// 取并集（首次出现序——确定性输出；容器被搬空）。
    std::vector<DiagRefEntry> take() { return std::move(m_entries); }

private:
    /// 合并键（全部定位字段以单元分隔符拼接——token 词表不含控制字符，
    /// 无歧义）。
    static std::string mergeKey(const DiagRefEntry& e)
    {
        std::string key = e.code;
        key += '\x1f';
        key += e.subject.has_value() ? e.subject->toCanonical() : std::string{};
        key += '\x1f';
        key += e.localName.value_or(std::string{});
        key += '\x1f';
        key += e.runtimeName.value_or(std::string{});
        key += '\x1f';
        key += e.sourceSection;
        key += '\x1f';
        key += e.sourceRun.has_value() ? e.sourceRun->toCanonical() : std::string{};
        return key;
    }

    std::map<std::string, std::size_t> m_index;  ///< 合并键→条目下标
    std::vector<DiagRefEntry> m_entries;          ///< 并集本体（首次出现序）
};

/// 证据引用去重并集（§4.2 evidenceRefs"逐章节证据绑定的去重并集，含来源
/// 章节"——§4.3.2 EvidenceRefEntry 字段表）。
///
/// 组装口径（§4.6 行 7 绑定校验已通过的条目——引用事实以所属结果包络
/// 清单为权威，绑定仅声明"哪一章节引用了它"）：
///   - itemId/status/artifactDigest/caseScope/subject/notApplicableReason/
///     invalidReasonCode 取包络清单项（归档事实——状态与摘要已经绑定校验
///     核对一致）；
///   - sourceSection＝引用来源章节（§4.3.2"引用关系可反向导航"）；
///   - itemClass：包络清单项无类别承载（evidence EvidenceItem 无 class
///     字段——类别语义归 RequiredEvidenceProfile，reporting 无注册表边），
///     以枚举默认 Required 承载，类别呈现面随 Profile 注册表接入收口
///     （§14.4 v0.8 范围登记；不伪造类别判定——默认值即词表成员）。
///
/// 去重键＝（itemId＋状态＋来源章节＋清单事实字段全组）：同一章节内重复
/// 绑定去重为一条；同一证据被不同章节引用时各保留一条（各自携带来源章节
/// ——反向导航完整性优先于条目数最小化）。
std::vector<EvidenceRefEntry> collectEvidenceRefs(
    const std::vector<ReviewReportSection>& sections,
    const std::map<std::string, const evidence::ResultEnvelope*>& envelopeIndex)
{
    std::vector<EvidenceRefEntry> merged;
    std::set<std::string> seen;
    for (const ReviewReportSection& section : sections) {
        for (const SectionEntryView& entry : section.entries) {
            const auto boundIt = envelopeIndex.find(entry.result.runId.toCanonical());
            if (boundIt == envelopeIndex.end()) {
                continue;   // 绑定校验已拒绝越界引用——此分支不可达（防御性跳过）
            }
            const evidence::ResultEnvelope& envelope = *boundIt->second;
            for (const EvidenceBinding& binding : entry.evidence) {
                const evidence::EvidenceItem* manifestItem = nullptr;
                for (const evidence::EvidenceItem& item : envelope.evidence.items) {
                    if (item.itemId == binding.itemId) {
                        manifestItem = &item;
                        break;
                    }
                }
                if (manifestItem == nullptr) {
                    continue;   // 同上——绑定校验保证清单命中（防御性跳过）
                }
                EvidenceRefEntry ref;
                ref.itemId = manifestItem->itemId;
                ref.itemClass = evidence::EvidenceItemClass::Required;   // 见函数注（类别承载边界）
                ref.status = manifestItem->status;
                ref.artifactDigest = manifestItem->artifactDigest;
                if (manifestItem->caseScope.has_value()) {
                    ref.caseScope = *manifestItem->caseScope;
                }
                ref.subject = manifestItem->subject;
                ref.notApplicableReason = manifestItem->notApplicableReason;
                // Invalid 附诊断码引用（§4.3.2 Invalid 必填——包络清单项的
                // invalidReason 诊断记录的码面即"引用"的来源）。
                if (manifestItem->invalidReason.has_value()) {
                    ref.invalidReasonCode = manifestItem->invalidReason->code;
                }
                ref.sourceSection = section.sectionId;

                // 去重键（含来源章节——见函数注）：itemId＋状态＋来源章节＋
                // 对象范围。摘要/原因字段不进键——同一（itemId, section）的
                // 清单事实唯一（绑定校验已核对状态一致），键冲突不可能发生。
                std::string key = ref.itemId;
                key += '\x1f';
                key += std::to_string(static_cast<unsigned>(ref.status));
                key += '\x1f';
                key += ref.sourceSection;
                key += '\x1f';
                key += ref.subject.has_value() ? ref.subject->toCanonical() : std::string{};
                if (!seen.insert(key).second) {
                    continue;   // 同章节同证据重复绑定——去重为一条
                }
                merged.push_back(std::move(ref));
            }
        }
    }
    return merged;
}

// =====================================================================
// 三形态结果组装（ReportBuildOutcome 成功/失败/取消——§9.1 三字段冻结形状）
// =====================================================================

/// 成功形态：report 唯一非空、零诊断、零错误（冻结报告独占交接）。
ReportBuildOutcome assembleSuccessOutcome(ReviewReport&& frozen)
{
    ReportBuildOutcome outcome;
    outcome.report = std::make_unique<ReviewReport>(std::move(frozen));
    return outcome;
}

/// 取消形态：report 为空＋error 缺席＋零诊断（UX-03——正常取消非错误；
/// 调用方持取消令牌可判别取消与失败）。
ReportBuildOutcome assembleCanceledOutcome()
{
    ReportBuildOutcome outcome;
    return outcome;   // 三字段全部空/缺席——取消的形态表达
}

/// 失败形态：report 为空＋error（会话失败记录）＋已收集诊断（NFR-COR-03
/// 不静默——诊断与错误同时交付）。
ReportBuildOutcome assembleFailureOutcome(const ReportBuildSession& session,
                                          std::vector<core::DiagnosticRecord>&& diagnostics)
{
    ReportBuildOutcome outcome;
    outcome.diagnostics = std::move(diagnostics);
    outcome.error = session.failure();   // 仅 Failed 终态非空——fail() 已先于本调用
    return outcome;
}

}  // namespace

// =====================================================================
// 会话状态机（Builder.hpp 契约的实现——§7.1）
// =====================================================================

std::string_view token(ReportBuildStage stage) noexcept
{
    // 稳定 kebab token（§7.1 状态名；switch 全枚举——新增枚举值未登记时
    // 编译告警暴露遗漏，NFR-COR-02 同码同串）。
    switch (stage) {
    case ReportBuildStage::Requested:  return "requested";
    case ReportBuildStage::Resolving:  return "resolving";
    case ReportBuildStage::Building:   return "building";
    case ReportBuildStage::Rendering:  return "rendering";
    case ReportBuildStage::Verifying:  return "verifying";
    case ReportBuildStage::Publishing: return "publishing";
    case ReportBuildStage::Completed:  return "completed";
    case ReportBuildStage::Failed:     return "failed";
    case ReportBuildStage::Canceled:   return "canceled";
    }
    return "unknown";
}

void ReportBuildSession::advanceTo(ReportBuildStage next)
{
    // 终态不可变（§7.1 状态机图"终态后不可变"）——对终态的任何推进都是
    // 调用方契约违约，fail-fast（AGENTS.md 错误语义）。
    if (isTerminal()) {
        throw ReportError(ReportErrorCode::Usage,
                          std::string("ReportBuildSession: 已处终态 ")
                              + std::string(token(m_stage)) + "，不可再推进");
    }
    // 主链严格逐步（§7.1 图原文顺序）：唯一合法后继表——跳段/回退一律
    // 拒绝（状态机语义单点，测试逐边锁定）。
    ReportBuildStage successor;
    switch (m_stage) {
    case ReportBuildStage::Requested:  successor = ReportBuildStage::Resolving;  break;
    case ReportBuildStage::Resolving:  successor = ReportBuildStage::Building;   break;
    case ReportBuildStage::Building:   successor = ReportBuildStage::Rendering;  break;
    case ReportBuildStage::Rendering:  successor = ReportBuildStage::Verifying;  break;
    case ReportBuildStage::Verifying:  successor = ReportBuildStage::Publishing; break;
    case ReportBuildStage::Publishing: successor = ReportBuildStage::Completed;  break;
    // 终态已在上方拦截——default 不可达（防御性兜底：触发即实现缺陷）。
    default:
        throw ReportError(ReportErrorCode::Usage,
                          "ReportBuildSession: 终态推进未被前置拦截（实现违约）");
    }
    if (next != successor) {
        throw ReportError(ReportErrorCode::Usage,
                          std::string("ReportBuildSession: 非法推进 ")
                              + std::string(token(m_stage)) + " -> "
                              + std::string(token(next)) + "（主链后继应为 "
                              + std::string(token(successor)) + "——§7.1）");
    }
    m_stage = next;
}

void ReportBuildSession::fail(ReportErrorCode code, std::string detail)
{
    if (isTerminal()) {
        throw ReportError(ReportErrorCode::Usage,
                          std::string("ReportBuildSession: 已处终态 ")
                              + std::string(token(m_stage)) + "，不可再失败");
    }
    m_failure = ReportError(code, std::move(detail));
    m_stage = ReportBuildStage::Failed;   // 任一步失败→Failed（§7.1 状态机图）
}

void ReportBuildSession::cancel()
{
    if (isTerminal()) {
        throw ReportError(ReportErrorCode::Usage,
                          std::string("ReportBuildSession: 已处终态 ")
                              + std::string(token(m_stage)) + "，不可再取消");
    }
    m_stage = ReportBuildStage::Canceled;   // 协作取消→Canceled（正常取消非错误——UX-03）
}

ReportBuildSession::~ReportBuildSession()
{
    // RAII 清理守卫（§7.1"取消→中止→清理本会话临时物（RAII）→Canceled"）：
    // 非终态析构＝会话被中途放弃（异常展开/调用方放弃）——无人再能推进，
    // 按正常取消收尾（不产生失败记录——UX-03 非错误）。已 release 的会话
    // ＝构建阶段已交接（报告冻结返回），不误标取消。
    if (!isTerminal() && !m_released) {
        m_stage = ReportBuildStage::Canceled;
    }
}

// =====================================================================
// ReviewReportBuilder——§7.2 步①~⑦编排
// =====================================================================

ReviewReportBuilder::ReviewReportBuilder(const project::IProjectQueryPort& queryPort,
                                         IReportResultSource& resultSource,
                                         const SectionRegistry& sections,
                                         const diagnostics::IDiagnosticRegistry* codeRegistry)
    : m_queryPort(&queryPort)
    , m_resultSource(&resultSource)
    , m_sections(&sections)
    , m_codeRegistry(codeRegistry)
{
}

ReportBuildOutcome ReviewReportBuilder::build(const ReportBuildRequest& request,
                                              const ReportCancelToken* cancel,
                                              IReportProgressSink* progress)
{
    // 独立会话（§9.1"每次调用独立会话"——build 并发安全的承载）；RAII
    // 守卫保证异常路径收尾为 Canceled（非终态析构——§7.1 清理纪律）。
    ReportBuildSession session;
    std::vector<core::DiagnosticRecord> collectedDiags;   // 构建过程诊断（失败随 outcome 交付）

    try {
        // =============================================================
        // 请求面校验（§9.1 前置行"请求字段合法（身份合法、RunId 非空）；
        // sectionOverrides 词表内"——调用方契约违约 fail-fast（Usage）；
        // 唯一例外：revision 占位＝来源指定不明确（SourceAmbiguous——
        // §4.2"禁止'当前/tip'占位值"的请求边界落点）。
        // =============================================================
        if (!request.project.isValid() || !request.branch.isValid()) {
            throw ReportError(ReportErrorCode::Usage,
                              "build: project/branch 为空保留值（明确 ID 纪律——§4.2）");
        }
        if (!request.revision.isValid()) {
            // 空保留值与"当前/tip"占位同判（SourceAmbiguous——§9.1 错误行
            // "来源指定不明确（占位'当前'值）"；报告只引用明确修订）。
            throw ReportError(ReportErrorCode::SourceAmbiguous,
                              "build: revision 为空/占位（当前/tip 禁止——§4.2 明确 ID）");
        }
        if (request.resultRuns.empty()) {
            throw ReportError(ReportErrorCode::Usage,
                              "build: resultRuns 为空（≥1——§9.1 前置；无结果集即无报告）");
        }
        {
            std::set<std::string> seen;
            for (std::size_t i = 0; i < request.resultRuns.size(); ++i) {
                const core::RunId& run = request.resultRuns[i];
                if (!run.isValid()) {
                    throw ReportError(ReportErrorCode::Usage,
                                      "build: resultRuns[" + std::to_string(i)
                                          + "] 为空保留值（明确 RunId——§9.1）");
                }
                if (!seen.insert(run.toCanonical()).second) {
                    throw ReportError(ReportErrorCode::Usage,
                                      "build: resultRuns 重复 runId '"
                                          + run.toCanonical() + "'（结果集语义歧义）");
                }
            }
        }
        for (std::size_t i = 0; i < request.sectionOverrides.size(); ++i) {
            const SectionSelection& overrideSel = request.sectionOverrides[i];
            if (!isValidSectionId(overrideSel.sectionId)) {
                // 词表外 token 永不合法（§5.1 词表冻结——§9.1 前置行）。
                throw ReportError(ReportErrorCode::Usage,
                                  "build: sectionOverrides[" + std::to_string(i)
                                      + "] 章节 '" + overrideSel.sectionId
                                      + "' 不在 §5.1 词表");
            }
            // acceptance 1（§4.6 行 2）：B 级请求携带 C 专属章节覆盖＝
            // LevelConflict——"B 级报告不能伪造 C 级章节"（结构性防伪造，
            // 非运行期开关）。C 级携带 B 章节覆盖合法（C＝B 全部＋追加，
            // §5.2 范围图）。
            const std::optional<ReportLevel> overrideLevel =
                trySectionMinimumLevel(overrideSel.sectionId);
            if (request.level == ReportLevel::B && overrideLevel.has_value()
                && *overrideLevel == ReportLevel::C) {
                throw ReportError(ReportErrorCode::LevelConflict,
                                  "build: B 级请求携带 C 专属章节 '" + overrideSel.sectionId
                                      + "'（LevelConflict——§4.6 行 2）");
            }
        }
        if (request.reviewSeed.has_value()
            && (!request.reviewSeed->priorReportId.isValid()
                || !request.reviewSeed->priorDataIdentity.isValid()
                || request.reviewSeed->priorReportVersion < 1)) {
            throw ReportError(ReportErrorCode::Usage,
                              "build: reviewSeed 身份字段含空保留值/版本 <1（§4.5 演化链）");
        }
        if (request.reviewSeed.has_value()
            && !(request.reviewSeed->metadata.basisRevision == request.revision)) {
            // §4.5 basisRevision＝"报告数据源身份的复核字段"——种子声明
            // 的依据修订与请求修订不一致即种子组装违约（渲染期校验失败
            // 的构建期前置拦截）。
            throw ReportError(ReportErrorCode::Usage,
                              "build: reviewSeed.metadata.basisRevision 与请求修订不一致"
                              "（§4.5 复核字段）");
        }

        session.advanceTo(ReportBuildStage::Resolving);
        emitProgress(progress, 0, request.resultRuns.size(), "resolving");

        // =============================================================
        // Resolving——§7.2 步①~⑤（锚定纪律：head/tryRevision/listRuns 各
        // 恰一次；此后 HEAD 前进/迟到结果不影响已锚定视图——acceptance 4）
        // =============================================================

        // 构建时刻 HEAD（恰一次——§6.3 evaluatedAgainst.headRevision"报告
        // 生成时刻的当时 HEAD"；当前性判定的目标上下文锚，会话内不变）。
        const project::RevisionView head = m_queryPort->head();

        // 步①锚定：修订视图恰一次（修订不可变——PA-2；此后一切解析只对
        // 该视图与构建前枚举的 finalize 运行集，§7.1"一致读取视图"行）。
        const std::optional<project::RevisionView> view =
            m_queryPort->tryRevision(request.revision);
        if (!view.has_value()) {
            // 步①"缺失→SourceMissing"（§7.2 流程图）＋诊断（NFR-COR-03）。
            collectedDiags.push_back(buildDiagnostic(
                diagcodes::kSourceMissing,
                "锚定修订不存在或闭包外：" + request.revision.toCanonical(),
                "核对修订 ID（须为已提交且在会话闭包内）后重试"));
            session.fail(ReportErrorCode::SourceMissing,
                         "build: 锚定失败——修订 " + request.revision.toCanonical()
                             + " 不存在（§7.2 步①）");
            return assembleFailureOutcome(session, std::move(collectedDiags));
        }

        // 步②结果集：finalize 运行清单恰一次（构建时刻快照——D-13"只认
        // 完整运行"；迟到的 finalize 运行不入本报告，AT-10 观测点）。
        const std::vector<project::RunInfo> finalizeRuns =
            m_queryPort->listRuns(request.revision);
        std::map<std::string, const project::RunInfo*> finalizeIndex;
        for (const project::RunInfo& info : finalizeRuns) {
            finalizeIndex.emplace(info.runId.toCanonical(), &info);
        }
        // 请求的每个 runId 必须 ∈ finalize 清单（listRuns 仅 finalize 口径
        // ——未 finalize/不存在＝SourceMissing，§7.2 步②）。
        for (std::size_t i = 0; i < request.resultRuns.size(); ++i) {
            if (finalizeIndex.count(request.resultRuns[i].toCanonical()) == 0) {
                collectedDiags.push_back(buildDiagnostic(
                    diagcodes::kSourceMissing,
                    "结果不在 finalize 运行清单（未 finalize 或不存在）："
                        + request.resultRuns[i].toCanonical(),
                    "等待运行 finalize 后重试，或核对 RunId"));
                session.fail(ReportErrorCode::SourceMissing,
                             "build: 结果未 finalize/不存在——"
                                 + request.resultRuns[i].toCanonical() + "（§7.2 步②）");
                return assembleFailureOutcome(session, std::move(collectedDiags));
            }
        }

        // 步③逐结果：envelope→资格→当前性（取消检查点每结果一个——§7.1
        // "取消检查点（章节/结果解析）"）。
        struct ResolvedResult {
            evidence::ResultEnvelope envelope;  ///< 只读包络（构造边界产出）
            EligibilitySnapshot eligibility;    ///< 资格快照（evidence 纯检查冻结——§6.2）
            CurrentnessSnapshot currentness;    ///< 当前性快照（生成时刻冻结——§6.3）
        };
        std::vector<ResolvedResult> resolved;
        resolved.reserve(request.resultRuns.size());
        std::optional<evidence::ReproductionBlock> reproduction;  ///< 复现块（首个可解析者——§4.2）

        for (std::size_t i = 0; i < request.resultRuns.size(); ++i) {
            // 取消检查点（结果解析粒度——§7.1 取消行/acceptance 5）。
            if (cancel != nullptr && cancel->isCancelled()) {
                session.cancel();
                return assembleCanceledOutcome();   // UX-03：正常取消非错误
            }
            const core::RunId run = request.resultRuns[i];

            // 步③ envelope：解码失败→SourceMissing＋诊断（§7.2 流程图）。
            std::optional<evidence::ResultEnvelope> envelope = m_resultSource->tryEnvelope(run);
            if (!envelope.has_value()) {
                collectedDiags.push_back(buildDiagnostic(
                    diagcodes::kSourceMissing,
                    "结果包络不可解析（归档工件解码失败）：run=" + run.toCanonical(),
                    "核对 results/<run-id>/ 归档工件完整性（evidence/execution 适配面）"));
                session.fail(ReportErrorCode::SourceMissing,
                             "build: envelope 解码失败——run=" + run.toCanonical()
                                 + "（§7.2 步③）");
                return assembleFailureOutcome(session, std::move(collectedDiags));
            }

            // acceptance 1（D-16/§4.6 行 5）：Preview 结果拒绝——"Preview
            // 不产生结果对象"，存在即异常，构建边界显性化（D-16）。
            if (envelope->mode == core::EvaluationMode::Preview) {
                collectedDiags.push_back(buildDiagnostic(
                    diagcodes::kSourceMissing,
                    "Preview 结果拒绝进入报告（§8.1 表 1：不产生正式证据与结果对象）：run="
                        + run.toCanonical(),
                    "以 Verified/Quick 模式重评后引用其结果"));
                session.fail(ReportErrorCode::SourceMissing,
                             "build: Preview 结果拒绝（D-16/§4.6 行 5）——run="
                                 + run.toCanonical());
                return assembleFailureOutcome(session, std::move(collectedDiags));
            }

            // 锚定一致性：包络的任务五元组必须落在请求的三元组内（跨修订/
            // 跨分支/跨项目的包络到达本构建＝来源指定不明确——SourceAmbiguous，
            // §9.1 错误行"来源指定不明确"）。
            if (!(envelope->task.project == request.project)
                || !(envelope->task.branch == request.branch)
                || !(envelope->task.revision == request.revision)) {
                throw ReportError(ReportErrorCode::SourceAmbiguous,
                                  "build: envelope 任务三元组与请求不一致——run="
                                      + run.toCanonical() + "（来源歧义）");
            }

            // 步⑤快照一致性：报告级统一快照与逐结果快照（§7.2 步⑤
            // "不一致→SourceAmbiguous"）。
            if (request.snapshotId.has_value()
                && !(envelope->snapshotId == *request.snapshotId)) {
                throw ReportError(ReportErrorCode::SourceAmbiguous,
                                  "build: 报告级 snapshotId 与 run=" + run.toCanonical()
                                      + " 的包络快照不一致（§7.2 步⑤）");
            }

            // 步③资格：evidence 纯检查（§7.2/§6.2——不自算、每报告重查、
            // 不缓存跨报告复用；只冻结 eligible 位——unmetConditions 由
            // 适配器结果保留给渲染层的五条件核对表）。
            const ReportEligibilityChecks checks = m_resultSource->eligibilityOf(*envelope);

            // 步③当前性：投影快照（§6.3——status==nullopt＝不可判定计算
            // 形态，**不得默认 Current**〔P-EV-4/EV-CUR-2〕；逐条失效原因
            // 原样消费——evidence reasons 的呈现投影）。
            const evidence::CurrentnessResult currentnessResult =
                m_resultSource->currentnessOf(*envelope, head.id);

            ResolvedResult item;
            item.envelope = std::move(*envelope);
            item.eligibility.formalPass = checks.formalPass.eligible;
            item.eligibility.reviewRecord = checks.reviewRecord.eligible;
            item.currentness.status = currentnessResult.status;  // nullopt 原样——不默认 Current
            item.currentness.evaluatedAgainst.headRevision = head.id;
            // 上下文摘要（人读、确定性文本——无时钟/locale 依赖；"两者一致
            // 的常见情形如实记录"，§6.3；不入身份——§4.4 排除列）。
            item.currentness.evaluatedAgainst.contextSummary =
                "HEAD " + head.id.toCanonical() + "（报告生成时刻）";
            // 计算时刻＝快照属性（§6.3；记录性字段不入身份——D-04）。
            item.currentness.computedAtUtc = std::chrono::system_clock::now();
            // 失效原因仅 Superseded 非空（Current/不可判定恒空——evidence
            // CurrentnessResult 的 presence 纪律，原样冻结）。
            item.currentness.reasons = currentnessResult.reasons;

            // 不可判定诊断（P-EV-4/acceptance 3）：status==nullopt ⇒ 必填
            // unevaluableNote（"当前性无法判定"以诊断表达，不默认 Current）；
            // 同一步入报告级诊断并集（acceptance 3"RPT-CURRENTNESS-
            // UNEVALUABLE 诊断"）。
            if (!item.currentness.status.has_value()) {
                const core::DiagnosticRecord record = buildDiagnostic(
                    diagcodes::kCurrentnessUnevaluable,
                    "当前性无法判定（run=" + run.toCanonical() + "）——"
                        + (currentnessResult.unevaluableCause.has_value()
                               ? (currentnessResult.unevaluableCause
                                          == evidence::UnevaluableCause::CrossContext
                                      ? "跨上下文"
                                      : "依赖无法解析")
                               : "原因未注明")
                        + "；不默认 Current（§6.3/P-EV-4）",
                    "复核依赖条目或上下文后重评；查看前不沿用原通过结论（AT-30）");
                DiagRefEntry note;
                if (!makeDiagRef(m_codeRegistry, record, std::string(kReportLevelDiagSection),
                                 run, note)) {
                    // reportable 过滤路径的兜底：不可判定呈现码必须进报告
                    // （P-EV-4 呈现义务——presence 纪律），以默认元数据承载；
                    // 码面来自 diagcodes 登记常量（码值权威仍归注册表）。
                    note = DiagRefEntry{};
                    note.code = std::string(diagcodes::kCurrentnessUnevaluable);
                    note.sourceSection = std::string(kReportLevelDiagSection);
                }
                note.sourceRun = run;
                note.occurrences = 1;
                item.currentness.unevaluableNote = note;
            }

            // 复现块（§4.2 reproduction 必填——首个可解析者；全部不可解析
            // ＝来源缺失，见步③循环后的统一拒绝）。
            if (!reproduction.has_value()) {
                reproduction = m_resultSource->tryReproduction(run);
            }

            resolved.push_back(std::move(item));
            emitProgress(progress, i + 1, request.resultRuns.size(), "resolving");
        }

        // 复现块缺失＝§4.2 必填字段无来源（报告没有"无复现要素"的合法
        // 形态——NFR-COR-04 追溯与 RPT-03 证据包的数据源）。
        if (!reproduction.has_value()) {
            collectedDiags.push_back(buildDiagnostic(
                diagcodes::kSourceMissing,
                "复现要素不可解析（results/<run-id>/ 归档工件不含复现块）",
                "核对归档工件完整性（evidence/execution 适配面）后重试"));
            session.fail(ReportErrorCode::SourceMissing,
                         "build: 复现要素不可解析（§4.2 reproduction 必填）");
            return assembleFailureOutcome(session, std::move(collectedDiags));
        }

        // 集合语义规范化：按 runId 规范文本排序（§4.4"resultRefs 集"——
        // 同一结果集以任意请求顺序构建必得同一 dataIdentity/contentIdentity，
        // §9.1 确定性行）。
        std::sort(resolved.begin(), resolved.end(),
                  [](const ResolvedResult& a, const ResolvedResult& b) {
                      return a.envelope.task.run.toCanonical() < b.envelope.task.run.toCanonical();
                  });

        // 结果引用快照＋当前性汇总（perResult 与 resultRefs 一一对应——
        // ReportModel 契约，构建器保证；同序输出）。
        std::vector<ResultRefSnapshot> resultRefs;
        resultRefs.reserve(resolved.size());
        ReportCurrentnessSummary currentnessSummary;
        currentnessSummary.perResult.reserve(resolved.size());
        for (ResolvedResult& item : resolved) {
            ResultRefSnapshot ref;
            ref.runId = item.envelope.task.run;
            ref.task = item.envelope.task;                    // 五元组值拷贝（TASK-03 呈现）
            ref.evaluationKey = item.envelope.evaluationKey;
            ref.evaluatorContractVersion = item.envelope.evaluatorContractVersion;
            ref.mode = item.envelope.mode;
            ref.outcome = item.envelope.outcome;              // 三轴之执行轴（CON-02 正交）
            ref.engineeringStatus = item.envelope.engineeringStatus;  // 判定轴（当前性不混排）
            ref.snapshotId = item.envelope.snapshotId;        // 追溯链（NFR-COR-04）
            ref.sliceId = item.envelope.sliceId;
            ref.inputBaselineId = item.envelope.inputBaselineId;
            ref.caseScope = item.envelope.caseScope.caseIds;  // CaseId ≙ core::ObjectId（强类型别名）
            ref.eligibility = item.eligibility;               // 资格快照（冻结——§6.2）
            ref.currentness = item.currentness;               // 当前性快照（冻结——§6.3）
            ref.producer = item.envelope.producer;            // 产生侧信息（§4.3.1 两列）
            resultRefs.push_back(std::move(ref));

            ResultCurrentnessEntry entry;
            entry.runId = item.envelope.task.run;
            entry.currentness = item.currentness;
            currentnessSummary.perResult.push_back(std::move(entry));
        }

        // runId→包络/引用索引（章节绑定校验的事实面——步⑥；规范文本键）。
        std::map<std::string, const evidence::ResultEnvelope*> envelopeIndex;
        std::map<std::string, const ResultRefSnapshot*> refIndex;
        for (std::size_t i = 0; i < resolved.size(); ++i) {
            const std::string key = resolved[i].envelope.task.run.toCanonical();
            envelopeIndex.emplace(key, &resolved[i].envelope);
            refIndex.emplace(key, &resultRefs[i]);
        }

        session.advanceTo(ReportBuildStage::Building);
        // 章节进度分母＝级别范围内域章节数（§5.2 范围图——确定性计数）。
        const std::uint64_t sectionTotal =
            static_cast<std::uint64_t>(domainSectionsInScope(request.level).size());
        emitProgress(progress, 0, sectionTotal, "building");

        // =============================================================
        // Building——§7.2 步⑥章节组装＋级别校验（取消检查点每章节一个）
        // =============================================================

        // 报告级诊断并集（§4.2 diagRefs——章节诊断＋报告级诊断；occurrences
        // 去重计数不吞项）。结果级诊断先行入并（§6.2"其诊断照常呈现（失败
        // 可定位）"——Canceled/Failed/Interrupted 结果仅诊断/状态呈现，
        // TASK-02/acceptance 3）。
        DiagRefMerger diagMerger;
        for (ResolvedResult& item : resolved) {
            for (const core::DiagnosticRecord& record : item.envelope.diagnostics) {
                DiagRefEntry ref;
                if (makeDiagRef(m_codeRegistry, record, std::string(kReportLevelDiagSection),
                                item.envelope.task.run, ref)) {
                    diagMerger.add(std::move(ref));
                }
            }
            // 不可判定当前性诊断同步入并（acceptance 3——报告级呈现面）。
            if (item.currentness.unevaluableNote.has_value()) {
                diagMerger.add(*item.currentness.unevaluableNote);
            }
        }

        std::vector<ReviewReportSection> sections;    // 报告章节（词表序——§4.7 order 严格递增）
        std::vector<SectionStatusEntry> scopeStates;  // C 级范围判定的状态投影（checkSectionScope 输入）
        const UnitPreference& units = request.units.value_or(UnitPreference{});

        for (const std::string& sectionId : domainSectionsInScope(request.level)) {
            // 取消检查点（章节解析粒度——§7.1 取消行/acceptance 5）。
            if (cancel != nullptr && cancel->isCancelled()) {
                session.cancel();
                return assembleCanceledOutcome();
            }

            // 词表内 token 的行号必在（domainSectionsInScope 输出即词表成员
            // ——nullopt 不可能；解引用前不判空即文档化的前置事实）。
            const std::uint16_t order = *trySectionOrder(sectionId);
            IReportSectionProvider* provider = m_sections->find(sectionId);

            // ---- 未注册域章节：缺项表达，非空壳（§5.1 词表规则——"章节
            // 不可用（提供方未注册）"缺项文案冻结于 RPT-T04 原语）——章节
            // 以 NoFormalResult＋缺项清单入报告（缺正式结果默认不选＋显示
            // 缺项，§16 验收要点），状态投影 NoFormalResult（C 级范围判定
            // 的输入面）。
            if (provider == nullptr) {
                ReviewReportSection section;
                section.sectionId = sectionId;
                section.sectionVersion = 1;
                section.selected = false;   // 无内容默认不选（defaultSelectionForStatus 同判）
                section.status = SectionStatus::NoFormalResult;
                section.missingItems.push_back(unregisteredSectionMissingItem(sectionId));
                section.renderHint = RenderHint::Table;
                section.order = order;
                sections.push_back(std::move(section));
                scopeStates.push_back(SectionStatusEntry{sectionId, SectionStatus::NoFormalResult});
                continue;
            }

            // ---- 已注册域章节：按 requiredEvaluationKeys 过滤结果子集
            //（§9.2 前置"request.results 非空（构建器已按声明过滤）"；
            // 精确匹配 evaluationKey——无前缀展开语义，注册声明即完整键，
            // 键词形权威归 evidence isValidEvaluationKey）。
            std::vector<evidence::ResultEnvelope> filtered;
            ReportCurrentnessSummary filteredCurrentness;
            const std::vector<std::string> requiredKeys = provider->requiredEvaluationKeys();
            for (std::size_t i = 0; i < resolved.size(); ++i) {
                const bool wanted = std::find(requiredKeys.begin(), requiredKeys.end(),
                                              resolved[i].envelope.evaluationKey)
                                    != requiredKeys.end();
                if (!wanted) {
                    continue;
                }
                filtered.push_back(resolved[i].envelope);
                ResultCurrentnessEntry entry;
                entry.runId = resolved[i].envelope.task.run;
                entry.currentness = resolved[i].currentness;
                filteredCurrentness.perResult.push_back(std::move(entry));
            }

            // 过滤后空集＝该域无任何可引用结果——构建器判定 NoFormalResult
            //（§5.3 进入条件"提供方声明或构建器判定"的构建器半边），不调
            // 用投影（§9.2 前置 request.results 非空）。
            if (filtered.empty()) {
                ReviewReportSection section;
                section.sectionId = sectionId;
                section.sectionVersion = 1;
                section.selected = false;
                section.status = SectionStatus::NoFormalResult;
                MissingItemView missing;
                missing.itemId = sectionId;
                missing.reason = std::string(kNoFormalResultReason);
                section.missingItems.push_back(std::move(missing));
                section.renderHint = RenderHint::Table;
                section.order = order;
                sections.push_back(std::move(section));
                scopeStates.push_back(SectionStatusEntry{sectionId, SectionStatus::NoFormalResult});
                continue;
            }

            // 步⑥投影：SectionRequest 组装（值拷贝——提供方不得持有，§9.2；
            // results 即提供方全部输入——禁止回查项目/读请求外数据）。
            SectionRequest sectionRequest;
            sectionRequest.level = request.level;
            sectionRequest.revision = request.revision;
            sectionRequest.snapshotId = request.snapshotId;
            sectionRequest.results = filtered;
            sectionRequest.currentness = filteredCurrentness;
            sectionRequest.units = units;

            SectionContent content;
            try {
                content = provider->project(sectionRequest);
            } catch (const std::exception& ex) {
                // 提供方以 SectionContent 表达缺项（§9.2 错误行"不抛业务
                // 结论"）——抛异常即提供方实现违约，构建器转数据错误拒绝
                //（不吞错、不静默降级，定位到章节）。
                throw ReportError(ReportErrorCode::DataInvalid,
                                  std::string("build: 章节 '") + sectionId
                                      + "' 提供方投影异常（§9.2 纯投影契约违约）：" + ex.what());
            }

            // 结构自检（§5.3 presence 纪律＋entryKey 词形/节内唯一——
            // RPT-T04 交付原语 firstSectionContentViolation 的构建器消费面；
            // 违例码 DataInvalid）。
            if (const auto violation = firstSectionContentViolation(content)) {
                throw ReportError(*violation,
                                  std::string("build: 章节 '") + sectionId
                                      + "' 内容结构违约（§5.3/§9.2）");
            }

            // 逐条目上下文校验（§9.2"绑定越界由构建器拒绝"＋§4.6 行 6/7
            // ——结构自检原语不覆盖的请求上下文面，RPT-T04 交接口径）。
            for (std::size_t e = 0; e < content.entries.size(); ++e) {
                const SectionEntryView& entry = content.entries[e];
                const auto boundIt = envelopeIndex.find(entry.result.runId.toCanonical());
                if (boundIt == envelopeIndex.end()) {
                    // 结果绑定越界（runId 不在本构建结果集＝引用错位）。
                    throw ReportError(ReportErrorCode::EvidenceRefInvalid,
                                      "build: 章节 '" + sectionId + "' 条目 '"
                                          + entry.entryKey + "' 的结果绑定越界（§9.2）");
                }
                const evidence::ResultEnvelope& boundEnvelope = *boundIt->second;

                // acceptance 1（§4.6 行 6/TASK-02）：Canceled/Failed/
                // Interrupted 结果不构成章节结论条目——仅诊断/状态呈现。
                // 码面 DataInvalid（结论条目绑定了不构成结论的结果＝报告
                // 字段非法——§9.1 错误行 DataInvalid 面）。
                if (boundEnvelope.outcome != core::TaskOutcome::Completed) {
                    throw ReportError(ReportErrorCode::DataInvalid,
                                      "build: 章节 '" + sectionId + "' 条目 '"
                                          + entry.entryKey + "' 绑定非 Completed 结果（TASK-02/"
                                          "§4.6 行 6——取消/失败/中断仅诊断/状态呈现）");
                }

                // 条目工况范围 ⊆ 所属结果工况范围（§4.7 关系约束——防引用
                // 错位）。
                for (const core::ObjectId& caseId : entry.caseScope) {
                    if (!caseContained(boundEnvelope.caseScope.caseIds, caseId)) {
                        throw ReportError(ReportErrorCode::EvidenceRefInvalid,
                                          "build: 章节 '" + sectionId + "' 条目 '"
                                              + entry.entryKey + "' 工况范围越界（§4.7）");
                    }
                }

                // acceptance 1（§4.6 行 7）：证据引用与所属结果 envelope
                // 绑定不符＝EvidenceRefInvalid（防引用错位）——四查：itemId
                // 在清单、状态一致、Satisfied 摘要一致、工况范围不越界。
                for (std::size_t b = 0; b < entry.evidence.size(); ++b) {
                    const EvidenceBinding& binding = entry.evidence[b];
                    const evidence::EvidenceItem* manifestItem = nullptr;
                    for (const evidence::EvidenceItem& item : boundEnvelope.evidence.items) {
                        if (item.itemId == binding.itemId) {
                            manifestItem = &item;
                            break;
                        }
                    }
                    if (manifestItem == nullptr) {
                        throw ReportError(ReportErrorCode::EvidenceRefInvalid,
                                          "build: 章节 '" + sectionId + "' 条目 '"
                                              + entry.entryKey + "' 证据引用 '"
                                              + binding.itemId
                                              + "' 不在所属结果清单（§4.6 行 7）");
                    }
                    if (!(manifestItem->status == binding.status)) {
                        throw ReportError(ReportErrorCode::EvidenceRefInvalid,
                                          "build: 章节 '" + sectionId + "' 条目 '"
                                              + entry.entryKey + "' 证据 '"
                                              + binding.itemId
                                              + "' 状态与包络清单不一致（§4.6 行 7）");
                    }
                    if (binding.status == evidence::EvidenceItemStatus::Satisfied
                        && manifestItem->artifactDigest.has_value()
                        && binding.digest.has_value()
                        && !(*manifestItem->artifactDigest == *binding.digest)) {
                        throw ReportError(ReportErrorCode::EvidenceRefInvalid,
                                          "build: 章节 '" + sectionId + "' 条目 '"
                                              + entry.entryKey + "' 证据 '"
                                              + binding.itemId
                                              + "' 产物摘要与包络清单不一致（§4.6 行 7）");
                    }
                    for (const core::ObjectId& caseId : binding.caseScope) {
                        if (!caseContained(boundEnvelope.caseScope.caseIds, caseId)) {
                            throw ReportError(ReportErrorCode::EvidenceRefInvalid,
                                              "build: 章节 '" + sectionId + "' 条目 '"
                                                  + entry.entryKey + "' 证据 '"
                                                  + binding.itemId + "' 工况范围越界（§4.7）");
                        }
                    }
                }
            }

            // ---- 冻结章节视图（§4.3.4 字段表——提供方产出经校验后入报告）。
            ReviewReportSection section;
            section.sectionId = sectionId;
            section.sectionVersion = content.providerContractVersion;
            section.status = content.status;
            section.entries = content.entries;
            section.missingItems = content.missingItems;
            section.renderHint = content.renderHint;
            section.order = order;

            // 选择：显式覆盖优先，缺省＝§5 默认规则（defaultSelectionForStatus
            // ——NoFormalResult 不选、Populated/DataInsufficient/NotApplicable
            // 选中——§16 验收要点"缺正式结果的章节默认不选"的契约承载）。
            section.selected = defaultSelectionForStatus(content.status);
            for (const SectionSelection& overrideSel : request.sectionOverrides) {
                if (overrideSel.sectionId == sectionId) {
                    section.selected = overrideSel.selected;   // 用户显式覆盖（§9.1）
                    break;
                }
            }

            // 来源对象/结果：条目跳转对象与结果绑定的去重并集（首次出现序
            // ——条目序确定，NFR-COR-02；sourceResults ⊆ resultRefs 由
            // 绑定校验保证——§4.7）。
            for (const SectionEntryView& entry : content.entries) {
                if (entry.jump.objectId.has_value()) {
                    const core::ObjectId& obj = *entry.jump.objectId;
                    const bool present =
                        std::any_of(section.sourceObjects.begin(), section.sourceObjects.end(),
                                    [&obj](const core::ObjectId& o) { return o == obj; });
                    if (!present) {
                        section.sourceObjects.push_back(obj);
                    }
                }
                const std::string runKey = entry.result.runId.toCanonical();
                const bool present = std::any_of(
                    section.sourceResults.begin(), section.sourceResults.end(),
                    [&runKey](const core::RunId& r) { return r.toCanonical() == runKey; });
                if (!present) {
                    section.sourceResults.push_back(entry.result.runId);
                }
            }

            // 章节级当前性（§4.3.4"引用 Superseded 结果时必填"——取首个
            // Superseded 引用的生成时刻快照；无 Superseded 引用＝缺席）。
            for (const core::RunId& run : section.sourceResults) {
                const ResultRefSnapshot* ref = refIndex.at(run.toCanonical());
                if (ref->currentness.status.has_value()
                    && *ref->currentness.status == evidence::CurrentnessStatus::Superseded) {
                    section.currentness = ref->currentness;
                    break;
                }
            }

            // 章节级资格说明（§4.3.4——由所引结果资格聚合：全部所引结果
            // 资格成立方可；AND 聚合＝保守方向，单一结果不成立即整体不
            // 允许渲染对应声明——§6.2 两类声明独立、绝不互换措辞）。
            // note 附加说明为空（无附加文本义务——渲染层按两布尔呈现）。
            if (!section.sourceResults.empty()) {
                EligibilityNote note;
                note.formalPassAllowed = true;
                note.reviewRecordAllowed = true;
                for (const core::RunId& run : section.sourceResults) {
                    const ResultRefSnapshot* ref = refIndex.at(run.toCanonical());
                    note.formalPassAllowed =
                        note.formalPassAllowed && ref->eligibility.formalPass;
                    note.reviewRecordAllowed =
                        note.reviewRecordAllowed && ref->eligibility.reviewRecord;
                }
                section.eligibilityNote = std::move(note);
            }

            // 章节诊断入并（§4.2 diagRefs 并集；reportable 过滤在 makeDiagRef
            // ——§4.3.3"reportable=false 的码不进报告"）。
            for (const core::DiagnosticRecord& record : content.diagnostics) {
                DiagRefEntry ref;
                if (makeDiagRef(m_codeRegistry, record, sectionId, std::nullopt, ref)) {
                    diagMerger.add(std::move(ref));
                }
            }

            sections.push_back(std::move(section));
            scopeStates.push_back(SectionStatusEntry{sectionId, content.status});
            emitProgress(progress, static_cast<std::uint64_t>(sections.size()), sectionTotal,
                         "building");
        }

        // ---- 级别×章节范围判定（§5.4/P-RPT-5 保守处置——acceptance 1）。
        // 两路同码面：①内容路（checkSectionScope——C 级全部 C 专属章节缺
        // 正式结果，含零 C 章节空集情形）；②选择路（C 级零 C 章节选中——
        // 用户覆盖全部取消选中；与 ReviewReport::make 的 §4.6 行 4 校验同
        // 语义，此处前置拦截以携带降级建议诊断）。任一命中＝拒绝生成（不
        // 产生报告对象）＋RPT-SCOPE-INSUFFICIENT 诊断＋建议生成 B 级（用户
        // 显式确认后重建，非静默降级——P-RPT-5 逐字落位不放宽）。
        const SectionScopeVerdict scopeVerdict = checkSectionScope(request.level, scopeStates);
        std::size_t selectedCSections = 0;
        if (request.level == ReportLevel::C) {
            for (const ReviewReportSection& section : sections) {
                const std::optional<ReportLevel> sectionLevel =
                    trySectionMinimumLevel(section.sectionId);
                if (sectionLevel.has_value() && *sectionLevel == ReportLevel::C
                    && section.selected) {
                    ++selectedCSections;   // C 专属章节（词表级别＝C）且被选中
                }
            }
        }
        if (scopeVerdict.scopeInsufficient
            || (request.level == ReportLevel::C && selectedCSections == 0)) {
            const core::DiagnosticRecord advice =
                makeScopeInsufficientDiagnostic(scopeVerdict.noFormalResultCSections);
            collectedDiags.push_back(advice);
            session.fail(ReportErrorCode::ScopeInsufficient,
                         "build: C 级请求无可引用的已完成 C 章节内容（§5.4/P-RPT-5 保守处置——"
                         "宁可拒绝不可虚级；降级建议见 RPT-SCOPE-INSUFFICIENT 诊断）");
            return assembleFailureOutcome(session, std::move(collectedDiags));
        }

        // =============================================================
        // 步⑦冻结（§7.2——dataIdentity/contentIdentity 计算于 make()，
        // 调用方不可申报；冻结时刻＝本函数返回，§7.1 逐项明确表）
        // =============================================================

        // 数据源规格（§4.4 ReportCodec-Data 编码主体）先于字段组装——
        // 评审演化种子的一致性校验需要本次数据源摘要（§9.1 维度表"构建器
        // 校验旧报告 dataIdentity 与本次数据源一致"）。
        ReportSourceSpec spec;
        spec.project = request.project;
        spec.branch = request.branch;
        spec.revision = request.revision;
        spec.revisionSeq = view->seq;        // 展示排序（入 Data 编码——§4.4）
        spec.level = request.level;
        spec.snapshotId = request.snapshotId;
        spec.inputSliceId = std::nullopt;    // 请求形状无报告级切片（§9.1）——逐结果切片在快照内
        spec.resultRefs.reserve(resultRefs.size());
        for (const ResultRefSnapshot& ref : resultRefs) {
            ResultDataSourceRef dataRef;
            dataRef.runId = ref.runId;
            dataRef.snapshotId = ref.snapshotId;
            dataRef.sliceId = ref.sliceId;
            dataRef.inputBaselineId = ref.inputBaselineId;
            dataRef.caseScope = ref.caseScope;
            spec.resultRefs.push_back(std::move(dataRef));
        }
        spec.unitPreference = units;
        for (const ReviewReportSection& section : sections) {
            spec.selectedSections.push_back(
                SelectedSectionEntry{section.sectionId, section.selected});
        }

        // 评审演化（§9.1 维度表）：旧报告数据源身份与本次数据源不一致＝
        // 数据已变，须按新报告处理（Usage——调用方以错误的演化种子复用；
        // 不采信调用方申报，重算比对——§4.4 同源算法）。
        const core::ContentIdentity dataIdentity = ReportCodec::digestData(spec);
        if (request.reviewSeed.has_value()
            && !(request.reviewSeed->priorDataIdentity == dataIdentity)) {
            throw ReportError(ReportErrorCode::Usage,
                              "build: reviewSeed 数据源身份与本次不一致（数据已变，须按新报告"
                              "处理——§9.1 评审演化；旧 dataIdentity="
                                  + request.reviewSeed->priorDataIdentity.toCanonical()
                                  + " 本次=" + dataIdentity.toCanonical() + "）");
        }

        ReviewReportFields fields;
        fields.reportId = ReportId::generate();  // 构建成功时一次分配（§4.1——随机非零；不入身份）
        fields.level = request.level;
        fields.project = request.project;
        fields.branch = request.branch;
        fields.revision = request.revision;
        fields.revisionSeq = view->seq;
        fields.snapshotId = request.snapshotId;
        fields.inputSliceId = std::nullopt;
        fields.resultRefs = std::move(resultRefs);
        fields.evidenceRefs = collectEvidenceRefs(sections, envelopeIndex);  // 证据引用并集（§4.2）
        fields.diagRefs = diagMerger.take();                                 // 诊断引用并集（§4.2）
        fields.currentnessSummary = std::move(currentnessSummary);
        // coverageSummary：报告级聚合语义（多结果覆盖矩阵归并规则）未冻结
        // ——RP-COV-1（EVI-02）收口；本任务不消费覆盖矩阵、不私设归并规则
        //（自行裁决归 RP-COV-1/所有者），字段以零计数承载并随 RP-COV-1
        // 落地刷新（§14.4 v0.8 范围登记）。
        fields.coverageSummary = CaseCoverageSummary{};
        // externalResourceSummary：数据源＝runtime 摘要资源清单（ModelSummary
        // ——RPT-T12 落位面）；本任务无来源注入，以空集承载（§4.2"可空集"
        // 合法值——空＝本构建器未获知任何外部资源状态，非"确认无外部资源"）。
        fields.externalResourceSummary = {};
        fields.reproduction = std::move(*reproduction);
        fields.sections = std::move(sections);
        if (request.reviewSeed.has_value()) {
            // 演化链新版本（§4.5）：supersedes＝旧 ReportId、版本＝旧+1、
            // 元数据取种子初值（basisRevision 一致性已在前置校验；元数据
            // 入 contentIdentity——§4.4，dataIdentity 不变＝同数据基准的
            // 评审演化）。
            fields.supersedes = request.reviewSeed->priorReportId;
            fields.reportVersion = request.reviewSeed->priorReportVersion + 1;
            fields.review = request.reviewSeed->metadata;
        } else {
            fields.reportVersion = 1;
            // 全新报告元数据（§4.5 默认形态）：依据修订/快照＝请求三元组；
            // 未签署（签署仅记录事实——D-12：未签署报告可导出，签署非门禁）。
            ReviewMetadata review;
            review.basisRevision = request.revision;
            review.basisSnapshot = request.snapshotId;
            review.signOff = SignOffState::unsignedState();
            fields.review = std::move(review);
        }
        fields.unitPreference = units;
        // 生成信息（不入任何身份——§4.4 排除列/D-04；同内容重建身份一致
        // 的前提；生成者缺省批量形态，principal 采集归 ui——§4.2）。
        fields.generatedAtUtc = std::chrono::system_clock::now();
        fields.generatedBy = "batch";
        fields.generatorVersion = std::string(kGeneratorVersion);
        fields.sectionModelVersion = std::string(kSectionModelVersion);

        // 冻结：字段校验→身份计算→不可变对象（make() 唯一生产者——§4.2；
        // 级别×章节词表注入 frozenReportLevelRule——判定权威单点在 Sections，
        // ReportModel 侧零复制）。冻结时刻＝make() 返回＋本函数返回（§7.1
        // "报告对象何时冻结"行）。
        ReviewReport frozen = ReviewReport::make(std::move(fields), frozenReportLevelRule());

        // 构建阶段交接：报告已冻结并即将交付（§7.1"报告对象何时冻结＝
        // Building 步末返回"）——析构守卫不再把本会话误标 Canceled。
        session.release();
        emitProgress(progress, sectionTotal, sectionTotal, "building");
        return assembleSuccessOutcome(std::move(frozen));
    } catch (const ReportError& err) {
        // 统一失败收尾（§7.1"任一步失败：诊断＋原项目零修改"）：会话转
        // Failed；已收集诊断随 outcome 交付（NFR-COR-03 不静默）。零项目写
        // 由注入面结构保证（本类不持有任何 project 写端口——§7.1 逐项
        // 明确表"生成失败是否修改原项目＝否"）。
        session.fail(err.code(), err.what());
        return assembleFailureOutcome(session, std::move(collectedDiags));
    }
}

}  // namespace sdurws::ird::reporting
