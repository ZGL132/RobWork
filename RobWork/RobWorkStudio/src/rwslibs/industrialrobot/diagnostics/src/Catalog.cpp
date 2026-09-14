/**
 * @file   Catalog.cpp
 * @brief  诊断目录与信封实现——词表 token、条目组装原语、DiagCatalog（去重
 *         计数/稳定投影/订阅/安全摘要导出）与 DiagnosticsSinkImpl。
 *
 * 设计依据（与 Catalog.hpp 头注同源，此处只登记实现口径）：
 *   - §4.4 actionKind 动作族映射＝矩阵"处理动作族"列首动作（机器锚点——
 *     多动作行的第二动作为呈现层可选细分，不进机器字段）；
 *   - §6.4 去重键派生：task 存在→Task（scopeId＝五元组规范串）；否则
 *     subject→Object（scopeId＝subject 规范串）；否则 None（实现口径——
 *     登记于单元卡 v0.5；DT-DUP-2"不同 subject 绝不同键"由键含 subject 保证）；
 *   - §6.4 稳定序：目录条目按入目录序保存（＝orderKey 升序——entryId 单调），
 *     snapshot 原序输出；
 *   - §9.7 append 校验：Dev 拒绝（Usage）、空/重复 entryId 拒绝（Usage）、
 *     链接字段（causedBy/relatedTo/supersedes）指向不存在条目拒绝（Usage
 *     ——指向性校验在目录侧落地：目录持有条目集合，工厂无此知识；单调
 *     entryId 下"指向更早条目"⇒无环，§6.3 DAG 由构造保证）；
 *   - §9.7 订阅：回调在触发 append 的调用方线程同步派发（阶段 A 口径），
 *     先改状态后锁外通知（观察者可安全调用 snapshot）；
 *   - §8.10 exportSafeSummary 字段面：{code, titleKey, 参数, subject,
 *     severity, category}（不含原始文本字段）；键值文本格式（§1.4）；
 *     §9.5 safeSummary 的脱敏双保险接线随 DIAG-T08（当前参数值为登记表
 *     schema 内的调用方提供的占位值，路径/凭据类内容不应进入——防线登记
 *     于单元卡 v0.5）。
 *
 * 线程安全：DiagCatalog 内部一把互斥覆盖条目/去重/订阅三张表（append 并发
 * 安全；snapshot 锁内拷贝投影——值拷贝语义，锁外无共享）；订阅退订同样
 * 加锁（句柄析构与 append 并发安全；句柄析构必须早于目录析构——头注契约）。
 */

#include <sdurws/ird/diagnostics/Catalog.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "EntryDetail.hpp"                      // 码小写形/去重键派生（私有单点）
#include <sdurws/ird/diagnostics/Factory.hpp>   // DiagnosticsSinkImpl 的 report 用 create
#include <sdurws/ird/diagnostics/Redaction.hpp> // IRedactionService（exportSafeSummary 双保险——DIAG-T08）

namespace sdurws::ird::diagnostics {

// =====================================================================
// 词表 token（§6.4 scopeKind；§4.4 actionKind——switch 全枚举）
// =====================================================================

std::string_view scopeKindToken(ScopeKind kind) noexcept
{
    switch (kind) {
    case ScopeKind::None:   return "none";
    case ScopeKind::Task:   return "task";
    case ScopeKind::Case:   return "case";
    case ScopeKind::Object: return "object";
    case ScopeKind::Batch:  return "batch";
    }
    return "none";   // 全枚举不可达；保守取首值
}

std::string_view actionKindToken(DiagnosticCategory category) noexcept
{
    // §4.4 矩阵逐行首动作（注释列上游锚点——呈现分组与动作建议的机器锚点；
    // 与 DiagCodes.cpp categoryDefaultRetry 同源于 §4.4 动作族列）。
    switch (category) {
    case DiagnosticCategory::InputInvalid:        return "fix-input";           // 定位对象/字段→编辑
    case DiagnosticCategory::FormatOrVersion:     return "open-upgrade-guide";  // PM-06 升级指引
    case DiagnosticCategory::PermissionOrLock:    return "retry-readonly";      // PM-07 只读横幅
    case DiagnosticCategory::ResourceMissing:     return "relink";              // PM-09 流程入口
    case DiagnosticCategory::PolicyDenied:        return "adjust-policy";       // UX-08 策略入口
    case DiagnosticCategory::Confirmable:         return "confirm-or-fix";      // SA-15 确认对话
    case DiagnosticCategory::ExecutionFailed:     return "retry-task";          // UX-10 失败态
    case DiagnosticCategory::Canceled:            return "none";                // UX-03 正常取消无动作
    case DiagnosticCategory::Interrupted:         return "rerun-interrupted";   // NFR-REL-03 可重跑
    case DiagnosticCategory::Timeout:             return "retry-task";          // 失败态＋worker 检视
    case DiagnosticCategory::DataInsufficient:    return "supply-evidence";     // C5 同基准复评
    case DiagnosticCategory::EvidenceMissing:     return "supply-evidence";     // 表 2④ 全量列出
    case DiagnosticCategory::InfeasibilityProof:  return "review-proof";        // RPT-05 评审记录
    case DiagnosticCategory::Internal:            return "report-bug";          // 开发日志/恢复横幅
    case DiagnosticCategory::SecurityOrRedaction: return "inspect-resource";    // NFR-SEC-01~03
    }
    return "fix-input";   // 全枚举不可达；保守取首值
}

// =====================================================================
// DiagnosticEntry 等值（测试/观测面——字段序＝§4.2 字段表行序）
// =====================================================================

bool DiagnosticEntry::operator==(const DiagnosticEntry& o) const
{
    return entryId == o.entryId
        && record == o.record
        && category == o.category
        && severity == o.severity
        && context.project == o.context.project
        && context.branch == o.context.branch
        && context.revision == o.context.revision
        && context.task == o.context.task
        && context.snapshotId == o.context.snapshotId
        && context.sliceId == o.context.sliceId
        && context.policyContentId == o.context.policyContentId
        && context.commandType == o.context.commandType
        && context.commandDigest == o.context.commandDigest
        && context.sourceUnit == o.context.sourceUnit
        && context.sourceInterface == o.context.sourceInterface
        && context.contractVersions == o.context.contractVersions
        && context.params == o.context.params
        && emittedAtUtc == o.emittedAtUtc
        && threadTag == o.threadTag
        && workerId == o.workerId
        && causedBy == o.causedBy
        && relatedTo == o.relatedTo
        && supersedes == o.supersedes
        && dedupKey == o.dedupKey
        && orderKey == o.orderKey
        && redactedContextSnapshot == o.redactedContextSnapshot;
}

// =====================================================================
// DiagCatalog——pimpl 实现体
// =====================================================================

struct DiagCatalog::Impl {
    /// 变更通知的 RAII 订阅体（析构时退订——§9.7 句柄语义）。
    struct Subscription final : ISubscription {
        Subscription(DiagCatalog* owner, IDiagObserver* obs)
            : catalog(owner), observer(obs) {}
        ~Subscription() override
        {
            // 退订加锁（与 append 并发安全）；catalog 生命周期由契约保证
            // （句柄析构必须早于目录析构——Catalog.hpp ISubscription 注释）。
            catalog->m_impl->removeObserver(observer);
        }
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        DiagCatalog* catalog;   ///< 回指目录（非拥有——生命周期契约见上）
        IDiagObserver* observer; ///< 观察者（非拥有——订阅方持有）
    };

    void removeObserver(IDiagObserver* observer)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = std::find(observers.begin(), observers.end(), observer);
        if (it != observers.end()) {
            observers.erase(it);   // 重复退订静默容忍（RAII 双持有防御）
        }
    }

    std::mutex mutex;                          ///< 条目/去重/订阅三表共用锁（短临界区）
    std::vector<DiagnosticEntry> entries;      ///< 目录条目（入目录序＝orderKey 升序）
    std::unordered_map<DiagEntryId, std::size_t> idToIndex;  ///< id→entries 下标（链接校验/查找）
    std::map<DedupKey, DiagEntryId> dedupIndex; ///< 去重键→首条目 id（§6.4 折叠计数）
    std::unordered_map<DiagEntryId, std::size_t> occurrences; ///< 首条目 id→命中计数（≥1）
    std::vector<IDiagObserver*> observers;     ///< 订阅中的观察者（通知于锁外派发）

    // ---- exportSafeSummary 脱敏双保险（§7.7——DIAG-T08 接线，§14.4 v0.9）----
    // 复用 mutex（exportSafeSummary 本就持锁迭代；attach 与导出互斥即可）。
    // shared_ptr 共享所有权——挂接后目录存活期内服务不析构（服务为进程级，
    // 生命周期纪律见 Redaction.hpp）。
    std::shared_ptr<const IRedactionService> redaction; ///< 脱敏服务（可空＝未接线）
};

DiagCatalog::DiagCatalog()
    : m_impl(new Impl)
{
}

DiagCatalog::~DiagCatalog() = default;

void DiagCatalog::append(DiagnosticEntry entry)
{
    // ---- 入口校验（§9.7 前置＋§6.3 链完整性；首错即停）----

    // ①Dev 条目拒绝（§9.7"若 Dev 码误入→Usage"——Dev 走开发日志不入目录，
    // §6.2"临时诊断 vs 正式诊断"行）。
    if (entry.severity == DiagnosticSeverity::Dev) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "Dev 级条目不得入目录（码 " + entry.record.code
                                   + "）——开发诊断只走日志（NFR-REL-05）");
    }
    // ②空 id 拒绝（entryId 由工厂分配且 ≥1——0＝空保留值，绕过工厂的未登记
    // 构造路径在此拦截）。
    if (entry.entryId == 0) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "条目 entryId 为空（目录条目必须经工厂创建——§9.2 唯一入口）");
    }

    std::vector<IDiagObserver*> toNotify;
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);

        // ③重复 id 拒绝（同 id 二次 append＝工厂分配器被绕过/复用——目录内
        // 唯一身份违约）。
        if (m_impl->idToIndex.find(entry.entryId) != m_impl->idToIndex.end()) {
            throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                   "条目 entryId 重复（" + std::to_string(entry.entryId)
                                       + "）——目录内唯一身份违约");
        }

        // ④去重折叠（§6.4/DT-DUP-1）：同键命中不再占位，首条目计数递增；
        // 不覆盖首条目的任何字段（"目录保留首条，后续命中计数"）。
        if (auto it = m_impl->dedupIndex.find(entry.dedupKey);
            it != m_impl->dedupIndex.end()) {
            ++m_impl->occurrences[it->second];
        } else {
            // ⑤链接完整性（§6.3"指向已存在条目"——新条目的链只能指向已在
            // 目录中的更早条目；单调 id 下无环，DAG 由构造保证）。
            const auto idExists = [this](DiagEntryId id) {
                return m_impl->idToIndex.find(id) != m_impl->idToIndex.end();
            };
            if (entry.causedBy.has_value() && !idExists(*entry.causedBy)) {
                throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                       "causedBy 指向不存在条目（" + entry.record.code
                                           + "）——根因链必须链接目录内条目（§6.3）");
            }
            for (const DiagEntryId id : entry.relatedTo) {
                if (!idExists(id)) {
                    throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                           "relatedTo 指向不存在条目（" + entry.record.code
                                               + "）");
                }
            }
            if (entry.supersedes.has_value() && !idExists(*entry.supersedes)) {
                throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                       "supersedes 指向不存在条目（" + entry.record.code
                                           + "）——取代关系指向旧条目（旧条目保留不删）");
            }
            // 登记新条目（值搬移；条目构造后不可变——无 setter）。
            const std::size_t index = m_impl->entries.size();
            m_impl->idToIndex.emplace(entry.entryId, index);
            m_impl->dedupIndex.emplace(entry.dedupKey, entry.entryId);
            m_impl->occurrences.emplace(entry.entryId, 1);
            m_impl->entries.push_back(std::move(entry));
        }
        toNotify = m_impl->observers;   // 通知清单在锁内拷贝、锁外派发
    }

    // 变更通知（§9.7"追加＋去重计数＋变更通知"——去重命中同样通知：投影的
    // occurrences 已变化）。锁外派发（观察者可安全 snapshot）；阶段 A 在
    // 调用方线程同步派发（§9.8——异步化随 DIAG-T07 日志线程）。
    for (IDiagObserver* observer : toNotify) {
        observer->onCatalogChanged();
    }
}

std::vector<DiagProjectionItem> DiagCatalog::snapshot(DiagQuery query) const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<DiagProjectionItem> result;
    result.reserve(m_impl->entries.size());
    for (const DiagnosticEntry& entry : m_impl->entries) {
        // 过滤（§9.7 DiagQuery——各字段等值命中方纳入；全空＝全量）。
        if (query.task.has_value()
            && !(entry.context.task.has_value() && *entry.context.task == *query.task)) {
            continue;
        }
        if (query.revision.has_value()
            && !(entry.context.revision.has_value()
                 && *entry.context.revision == *query.revision)) {
            continue;
        }
        if (query.category.has_value() && entry.category != *query.category) {
            continue;
        }
        if (query.severity.has_value() && entry.severity != *query.severity) {
            continue;
        }

        // 投影组装（§9.7 字段面——不含 redactedContextSnapshot 与原始文本；
        // actionKind 自 §4.4 矩阵映射；occurrences 取目录计数；文案键按
        // P-DIAG-9 冻结约定派生——注册边界已保证登记键即该约定形）。
        DiagProjectionItem item;
        item.entryId = entry.entryId;
        item.code = entry.record.code;
        const std::string lower = detail::codeLower(entry.record.code);
        item.titleKey = "diag." + lower + ".title";
        item.detailKey = "diag." + lower + ".detail";
        item.category = entry.category;
        item.severity = entry.severity;
        item.subject = entry.record.subject;
        item.localName = entry.record.localName;
        item.runtimeName = entry.record.runtimeName;
        item.actionKind = std::string(actionKindToken(entry.category));
        item.comparison = entry.record.comparison;
        item.context = entry.context;
        item.occurrences = m_impl->occurrences.at(entry.entryId);
        // aggregatedUnder：聚合视图派生字段——DIAG-T06 产物，阶段 A 恒空。
        result.push_back(std::move(item));
    }
    return result;
}

std::unique_ptr<ISubscription> DiagCatalog::subscribe(IDiagObserver& observer)
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    // 同一观察者重复订阅去重（订阅两次通知两次＝实现噪声；句柄各自 RAII）。
    if (std::find(m_impl->observers.begin(), m_impl->observers.end(), &observer)
        == m_impl->observers.end()) {
        m_impl->observers.push_back(&observer);
    }
    return std::make_unique<Impl::Subscription>(this, &observer);
}

std::string DiagCatalog::exportSafeSummary(DiagQuery query, std::size_t maxEntries) const
{
    // 安全摘要导出（§8.10 reporting 行：{code, titleKey, 参数, subject,
    // severity, category}——不含原始文本字段；键值文本格式 §1.4）。
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    // 脱敏双保险快照（§7.7"输出前强制再过一遍脱敏"——DIAG-T08 接线）：
    // Dev 档＝NFR-SEC-07 全量且不做内部十六进制遮蔽（subject 规范身份是
    // reporting 的机器可读锚点——R-7 防误伤，详见头文件 attach 注释）。
    const std::shared_ptr<const IRedactionService> red = m_impl->redaction;
    std::string out;
    std::size_t emitted = 0;
    for (const DiagnosticEntry& entry : m_impl->entries) {
        if (maxEntries != 0 && emitted >= maxEntries) {
            break;   // maxEntries=0 表示不限（§9.7 签名注释口径）
        }
        // 复用 snapshot 的过滤语义（等值四轴）。
        if (query.task.has_value()
            && !(entry.context.task.has_value() && *entry.context.task == *query.task)) {
            continue;
        }
        if (query.revision.has_value()
            && !(entry.context.revision.has_value()
                 && *entry.context.revision == *query.revision)) {
            continue;
        }
        if (query.category.has_value() && entry.category != *query.category) {
            continue;
        }
        if (query.severity.has_value() && entry.severity != *query.severity) {
            continue;
        }

        // 行格式：code|severity|category|titleKey|subject|occurrences|params
        // （subject 规范串；params 为 k:v 逗号串——值为登记表 schema 占位值；
        // 每行一条、'|' 分隔、'\n' 结束——确定性文本，NFR-COR-02）。
        std::string line;
        line += entry.record.code;
        line += '|';
        line += severityToken(entry.severity);
        line += '|';
        line += categoryToken(entry.category);
        line += "|diag.";
        line += detail::codeLower(entry.record.code);
        line += ".title|";
        line += entry.record.subject.has_value() ? entry.record.subject->toCanonical() : "-";
        line += '|';
        line += std::to_string(m_impl->occurrences.at(entry.entryId));
        line += '|';
        bool firstParam = true;
        for (const auto& [key, value] : entry.context.params) {
            if (!firstParam) {
                line += ',';
            }
            firstParam = false;
            line += key;
            line += ":";
            line += value;
        }
        // 双保险出口：挂接了脱敏服务时每行强制再过一遍脱敏（§7.7 与
        // reporting 行——报告外发，DT-SEC-4；脱敏服务绝不抛出、输出必为
        // 脱敏后文本，§9.5——降级由服务侧 [REDACTED:redaction-failed] 承载）。
        if (red != nullptr) {
            line = red->redact(line, LogTier::Dev);
        }
        line += '\n';
        out += line;
        ++emitted;
    }
    return out;
}

void DiagCatalog::attachRedactionService(std::shared_ptr<const IRedactionService> service)
{
    // 快照切换（§14.4 v0.9）：下一行导出起生效，无在途回溯问题（导出为
    // 同步调用——持锁期间快照不变）。复用三表共用锁：attach 与 export 的
    // 互斥即可保证 shared_ptr 读写不并发。
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->redaction = std::move(service);
}

std::size_t DiagCatalog::size() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->entries.size();
}

// =====================================================================
// DiagnosticsSinkImpl——report/reportDev 形态兼容（P-DIAG-5）
// =====================================================================

DiagnosticsSinkImpl::DiagnosticsSinkImpl(IDiagnosticFactory& factory, DiagCatalog& catalog,
                                         IDevLogSink& devLog, std::string hostUnit,
                                         std::string hostInterface)
    : m_factory(&factory)
    , m_catalog(&catalog)
    , m_devLog(&devLog)
    , m_hostUnit(std::move(hostUnit))
    , m_hostInterface(std::move(hostInterface))
{
    // 装配期校验（§4.2 token 边界：sourceUnit ≤32——空/越界即构造失败，
    // fail-fast 不带病装配；hostInterface 允许为空的口径与 §4.2"必填"一致地
    // 拒绝——默认值 "sink.report" 已给合法形态）。
    if (m_hostUnit.empty() || m_hostUnit.size() > 32) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "hostUnit 须为非空且 ≤32 字符的单元 token（§4.2）");
    }
    if (m_hostInterface.empty() || m_hostInterface.size() > 64) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "hostInterface 须为非空且 ≤64 字符的接口 token（§4.2）");
    }
}

void DiagnosticsSinkImpl::report(const core::DiagnosticRecord& record)
{
    // §9.7 尾注："report＝factory.create＋catalog.append（无上下文时
    // sourceUnit 取注入的宿主标识）"——本快捷形态的上下文即宿主身份；
    // 需要任务/命令/修订等丰富锚定的产码路径应直用 create 全参形态。
    DiagContext context;
    context.sourceUnit = m_hostUnit;
    context.sourceInterface = m_hostInterface;
    DiagnosticEntry entry = m_factory->create(record, context);
    m_catalog->append(std::move(entry));   // 去重计数语义同目录（§6.4）
}

void DiagnosticsSinkImpl::reportDev(const std::string& channel, const std::string& message)
{
    // §9.7 尾注："reportDev＝logger.logDev"——透传开发日志路由（脱敏归日志
    // 管线 §7.3；Dev 不入目录 §6.2）。
    m_devLog->logDev(channel, message);
}

void DiagnosticsSinkImpl::append(DiagnosticEntry entry)
{
    m_catalog->append(std::move(entry));
}

std::vector<DiagProjectionItem> DiagnosticsSinkImpl::snapshot(DiagQuery query) const
{
    return m_catalog->snapshot(std::move(query));
}

std::unique_ptr<ISubscription> DiagnosticsSinkImpl::subscribe(IDiagObserver& observer)
{
    return m_catalog->subscribe(observer);
}

std::string DiagnosticsSinkImpl::exportSafeSummary(DiagQuery query, std::size_t maxEntries) const
{
    return m_catalog->exportSafeSummary(std::move(query), maxEntries);
}

}  // namespace sdurws::ird::diagnostics
