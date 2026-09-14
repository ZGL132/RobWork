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
 *   - §6.2 容量护栏与分级淘汰（DIAG-T09 落地，实现口径）：超限淘汰判据＝
 *     severity==Info ∧ 已消费（markConsumed 显式确认）∧ 非活动任务关联 ∧
 *     未被目录内其他条目链接引用（链保护——append 校验"指向已存在条目"
 *     的不变式不得被淘汰制造悬挂引用破坏，§6.3）；淘汰序＝入目录序（最旧
 *     先行）；引用计数按"淘汰遍开始时"的快照判定，引用者与被引用者同批
 *     可淘汰时多遍收敛（正确性优先于遍数）；无可淘汰候选＝软溢出仍追加
 *     （护栏不得牺牲证据完整性——§6.4"去重只影响呈现"同精神），溢出事实
 *     一律经 kCatalogInternalChannel 出 DIAG-CATALOG-OVERFLOW 开发诊断
 *     （Dev 不入目录——§6.2；出口未挂接＝静默，装配前合法降态）；
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
 * DIAG-T09 扩展面同锁覆盖：护栏配置/消费确认集/活动任务保护集/开发日志
 * 出口指针均在该互斥内读写；溢出开发诊断在锁外派发（与订阅回调同一时点，
 * sink 不得回调目录——Catalog.hpp attachDevLogSink 注释）。
 */

#include <sdurws/ird/diagnostics/Catalog.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
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

    /// 溢出事实通知（append 锁内捕获、锁外派发——与订阅回调同一时点纪律；
    /// 每次触发容量护栏的 append 至多一条）。
    struct OverflowNotice {
        std::size_t evicted = 0;        ///< 本次腾位淘汰条数
        std::size_t size = 0;           ///< 派发时目录条目数（观测上下文）
        bool retentionProtected = false; ///< true＝无可淘汰候选（保留规则压过护栏）
    };

    void removeObserver(IDiagObserver* observer)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = std::find(observers.begin(), observers.end(), observer);
        if (it != observers.end()) {
            observers.erase(it);   // 重复退订静默容忍（RAII 双持有防御）
        }
    }

    // ---- 容量护栏与分级淘汰（§6.2——DIAG-T09；调用方须已持 mutex）----

    /// 条目是否受"活动任务关联"保护（§6.2"活动任务关联条目不淘汰"）。
    /// 任务状态权威归 execution（PA-1）——本目录只对 activeTasks 保护集做
    /// 等值匹配，不判定任务真状态。
    bool taskActiveProtected(const DiagnosticEntry& entry) const
    {
        if (!entry.context.task.has_value()) {
            return false;   // 无任务锚定的条目无"活动任务关联"可言
        }
        for (const core::TaskIdentity& active : activeTasks) {
            if (active == *entry.context.task) {
                return true;
            }
        }
        return false;
    }

    /// 条目是否落入淘汰判据（§6.2"超限淘汰已消费且非 Warning/Error 的最旧
    /// Info 条目"——三判据的合取；链保护由调用方以引用计数另行判定）。
    bool isEvictableCandidate(const DiagnosticEntry& entry) const
    {
        // ①严重级别＝Info（Warning/Error 永不淘汰——错误追溯优先于内存护栏，
        //   §6.2/D-17）；
        // ②已消费（markConsumed 显式确认——未消费条目可能尚未被用户看到，
        //   淘汰即丢信息）；
        // ③非活动任务关联（运行未收口的任务，其诊断仍在分批消费——§6.2
        //   "任务运行中的分批诊断"行）。
        return entry.severity == DiagnosticSeverity::Info
            && consumedIds.count(entry.entryId) != 0
            && !taskActiveProtected(entry);
    }

    /**
     * @brief 执行容量腾位（分级淘汰——§6.2；前置：调用方持锁）。
     *
     * 目标：淘汰至 entries.size() ≤ maxEntries - minFreeSlots（为新条目腾位）。
     * 算法（多遍收敛）：
     *   每遍开始构建**引用计数快照**（全目录 causedBy/relatedTo/supersedes 的
     *   被指条目计数）——被指者本遍不淘汰（append 校验"链接指向已存在条目"
     *   的不变式不得被淘汰制造悬挂引用，§6.3）；随后自最旧向新单遍标记候选
     *   （淘汰序＝入目录序，§6.2"最旧淘汰"）。引用者与被引用者同批可淘汰时
     *   （链整体已消费且均为 Info），被引用者要到下一遍才解除保护——多遍
     *   收敛，每遍至少淘汰一条否则停（无候选＝软溢出，由调用方登记）。
     *
     * 复杂度：每遍 O(n·L)（n＝条目数，L＝平均链接数）；护栏路径（容量压力）
     * 才执行，不在诊断追加热区（§6.2 性能护栏定位，WP-23 校准面）。
     *
     * @param minFreeSlots [in] 需腾出的空位下限（≥1——append 路径恒传 1）
     * @return 实际淘汰条数（0＝无可淘汰候选——保留保护，软溢出）
     */
    std::size_t evictForCapacity(std::size_t minFreeSlots)
    {
        std::size_t totalEvicted = 0;
        while (true) {
            // 还需淘汰数：当前规模超出"上限－空位"的部分（调用前置保证 ≥1）。
            const std::size_t needed =
                entries.size() + minFreeSlots - capacity.maxEntries;
            if (needed == 0 || entries.empty()) {
                break;   // 已腾出足够空位／目录已空
            }
            // 本遍引用计数快照：被链接指向的条目不淘汰（链保护——§6.3）。
            std::unordered_map<DiagEntryId, std::size_t> refCount;
            for (const DiagnosticEntry& e : entries) {
                if (e.causedBy) {
                    ++refCount[*e.causedBy];
                }
                for (const DiagEntryId id : e.relatedTo) {
                    ++refCount[id];
                }
                if (e.supersedes) {
                    ++refCount[*e.supersedes];
                }
            }
            // 自最旧向新标记候选（快照语义：引用计数不随本遍标记递减——被
            // 跳过者下一遍重评，正确性优先于遍数；不会死循环：每遍无淘汰即停）。
            std::vector<DiagEntryId> victims;
            victims.reserve(needed);
            for (const DiagnosticEntry& e : entries) {
                if (victims.size() >= needed) {
                    break;   // 已标记足量——收手（不超汰）
                }
                if (!isEvictableCandidate(e)) {
                    continue;   // 分级判据不满足（级别/未消费/活动任务保护）
                }
                if (refCount.count(e.entryId) != 0) {
                    continue;   // 链保护：仍有幸存条目链接指向它
                }
                victims.push_back(e.entryId);
            }
            if (victims.empty()) {
                break;   // 无可淘汰候选——保留保护（软溢出，调用方登记）
            }
            applyEvictions(victims);
            totalEvicted += victims.size();
        }
        return totalEvicted;
    }

    /// 一次性应用淘汰（前置：调用方持锁；victims 非空且互不重复）。entries
    /// 保序过滤＋三张索引表随幸存者整体重建——避免逐条 vector 中段删除的
    /// O(n²) 搬移（质量守恒：淘汰只减不增，幸存者字段原样保留——CON-02
    /// "目录条目永不改写"在淘汰面的对偶：删除整条，绝不部分改写）。
    void applyEvictions(const std::vector<DiagEntryId>& victims)
    {
        const std::unordered_set<DiagEntryId> victimSet(victims.begin(),
                                                        victims.end());
        std::vector<DiagnosticEntry> kept;
        kept.reserve(entries.size() - victimSet.size());
        for (DiagnosticEntry& e : entries) {
            if (victimSet.count(e.entryId) != 0) {
                // 淘汰首条目＝其去重键一并失效（同键后续到达＝新首条重新
                // 占位，计数从 1 起算——被淘汰的呈现事实已消费完毕）；计数
                /// 与消费确认随条目同亡（无悬挂引用）。
                dedupIndex.erase(e.dedupKey);
                occurrences.erase(e.entryId);
                consumedIds.erase(e.entryId);
            } else {
                kept.push_back(std::move(e));
            }
        }
        entries = std::move(kept);
        idToIndex.clear();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            idToIndex.emplace(entries[i].entryId, i);
        }
    }

    std::mutex mutex;                          ///< 条目/去重/订阅三表共用锁（短临界区）
    std::vector<DiagnosticEntry> entries;      ///< 目录条目（入目录序＝orderKey 升序）
    std::unordered_map<DiagEntryId, std::size_t> idToIndex;  ///< id→entries 下标（链接校验/查找）
    std::map<DedupKey, DiagEntryId> dedupIndex; ///< 去重键→首条目 id（§6.4 折叠计数）
    std::unordered_map<DiagEntryId, std::size_t> occurrences; ///< 首条目 id→命中计数（≥1）
    std::vector<IDiagObserver*> observers;     ///< 订阅中的观察者（通知于锁外派发）

    // ---- 容量护栏（§6.2——DIAG-T09；P-DIAG-7 默认值登记于 Catalog.hpp）----
    CatalogCapacityConfig capacity;            ///< 护栏配置（默认 10,000 条可配——P-DIAG-7）
    std::unordered_set<DiagEntryId> consumedIds; ///< 已消费确认集（markConsumed——淘汰判据②）
    std::vector<core::TaskIdentity> activeTasks; ///< 活动任务保护集（setTaskActive——淘汰判据③；
                                                 ///  线性检索：并发活动任务个位数，不值得建索引）
    IDevLogSink* devLog = nullptr;             ///< 开发日志路由（非拥有——溢出事实出口；空＝静默）

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
    // 溢出事实（锁内捕获、锁外派发——开发诊断出口可能入队 I/O，不得持目录
    // 锁调用；与订阅回调同一锁外时点纪律）。容量值/出口指针同为锁内快照
    // （configureCapacity/attachDevLogSink 与 append 并发安全的前提）。
    Impl::OverflowNotice overflow;
    IDevLogSink* overflowSink = nullptr;
    std::size_t overflowCapacity = 0;
    bool hasOverflow = false;
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
            // ⑥容量护栏（§6.2"诊断清理策略"行——DIAG-T09）：已满（≥上限）
            // 先腾位再追加。淘汰只发生在这里（压力驱动——markConsumed/
            // setTaskActive 不主动触发，§6.2"超限淘汰"的判据语义）。
            if (m_impl->capacity.enabled
                && m_impl->entries.size() >= m_impl->capacity.maxEntries) {
                // 腾 1 个空位（分级淘汰自最旧起；返回 0＝无候选——保留保护）。
                overflow.evicted = m_impl->evictForCapacity(1);
                if (m_impl->entries.size() >= m_impl->capacity.maxEntries) {
                    // 软溢出：无可淘汰候选（Warning/Error/活动关联/未消费/
                    // 链保护全部在场）——仍然追加（护栏不得牺牲证据完整性；
                    // 与"清理永不触碰已持久化诊断"同为 CON-02 侧的保留优先）。
                    overflow.retentionProtected = true;
                }
                hasOverflow = true;   // 触发过护栏＝溢出事实成立（淘汰或软溢出）
            }
            // 登记新条目（值搬移；条目构造后不可变——无 setter）。
            const std::size_t index = m_impl->entries.size();
            m_impl->idToIndex.emplace(entry.entryId, index);
            m_impl->dedupIndex.emplace(entry.dedupKey, entry.entryId);
            m_impl->occurrences.emplace(entry.entryId, 1);
            m_impl->entries.push_back(std::move(entry));
        }
        overflow.size = m_impl->entries.size();
        overflowSink = m_impl->devLog;                    // 锁内快照（锁外使用）
        overflowCapacity = m_impl->capacity.maxEntries;   // 同上
        toNotify = m_impl->observers;   // 通知清单在锁内拷贝、锁外派发
    }

    // 溢出事实登记（§6.2"DIAG-CATALOG-OVERFLOW 开发诊断登记溢出事实"——
    // Dev 级自省不入目录，走开发日志通道 diag/catalog；出口未挂接＝静默）。
    // 淘汰腾位与保留保护两种溢出都登记：前者含淘汰数，后者显式标记保留保护
    // （工程侧据此发现"容量护栏被保留规则压过"的运行形态）。锁外派发的理由：
    // 出口可能入队 I/O，不得持目录锁调用（与订阅回调同一时点纪律）。
    if (hasOverflow && overflowSink != nullptr) {
        // 消息首 token＝稳定码（对齐 Redaction.cpp 降级行的码前置形态——
        // 码值权威在登记表，消息文本仅为开发观测载体）；数值面供 WP-23
        // 性能观测（容量/淘汰数/当前规模）。
        std::string message = "DIAG-CATALOG-OVERFLOW capacity=";
        message += std::to_string(overflowCapacity);
        message += " evicted=";
        message += std::to_string(overflow.evicted);
        message += " retention-protected=";
        message += overflow.retentionProtected ? "1" : "0";
        message += " size=";
        message += std::to_string(overflow.size);
        try {
            overflowSink->logDev(kCatalogInternalChannel, std::move(message));
        } catch (...) {
            // 溢出登记自身失败一并吞掉——护栏路径绝不向 append 调用方抛出
            // （§9.7 append 后置只承诺"容量策略执行"，诊断失败非调用方错误；
            // 同 Redaction degrade 的吞错纪律）。
        }
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

// ---- 容量护栏与分级淘汰（§6.2——DIAG-T09）----

void DiagCatalog::configureCapacity(const CatalogCapacityConfig& config)
{
    // 配置不变量（CatalogCapacityConfig 注释）：0 条容量无意义——关闭护栏
    // 须显式 enabled=false，不允许 0 值半关闭形态（调用方契约违约 fail-fast，
    // AGENTS.md 错误语义）。
    if (config.enabled && config.maxEntries == 0) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "容量护栏 maxEntries==0 无意义——关闭护栏应使用 "
                               "enabled=false（§6.2 配置语义）");
    }
    // 快照切换：下一条 append 起生效（在途 append 按旧配置完成——持锁写入，
    // 与 append 的护栏读同锁互斥，无撕裂读）。
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->capacity = config;
}

CatalogCapacityConfig DiagCatalog::capacityConfig() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->capacity;
}

void DiagCatalog::markConsumed(DiagEntryId entryId)
{
    // 0＝空保留值（Catalog.hpp DiagEntryId 注释）——传 0 属调用方契约违约
    // （正常 id 来自投影项，恒 ≥1），fail-fast。
    if (entryId == 0) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "markConsumed 传入空 entryId（0）——消费确认须引用"
                               "投影项携带的合法条目 id");
    }
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    // 未知/已淘汰 id 容忍无操作（advisory 语义——Catalog.hpp 方法注释：
    // 淘汰只移除已消费条目，迟到确认是良性竞态，不升级为异常）。对
    // Warning/Error 条目的确认同样登记（判据在淘汰侧——登记"已消费事实"
    // 与"是否可淘汰"分离，语义以事实为准）。
    m_impl->consumedIds.insert(entryId);
}

void DiagCatalog::setTaskActive(const core::TaskIdentity& task, bool active)
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (active) {
        // 重复登记幂等（同一任务双份保护登记＝实现噪声）。
        for (const core::TaskIdentity& existing : m_impl->activeTasks) {
            if (existing == task) {
                return;
            }
        }
        m_impl->activeTasks.push_back(task);
    } else {
        // 注销未登记任务幂等（对称集合语义）。
        for (auto it = m_impl->activeTasks.begin(); it != m_impl->activeTasks.end();
             ++it) {
            if (*it == task) {
                m_impl->activeTasks.erase(it);
                break;
            }
        }
    }
    // 不立即触发淘汰：注销只改变后续 append 压力下的候选判定（终结任务的
    // 诊断照常保留于会话——DT-LIFE-2/NFR-REL-03；淘汰须容量压力推动）。
}

void DiagCatalog::attachDevLogSink(IDevLogSink* devLog)
{
    // 快照切换：下一次溢出登记起生效；指针非拥有（Catalog.hpp 方法注释——
    // 调用方持有，生命周期须覆盖目录；nullptr＝解除挂接＝静默降态）。
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->devLog = devLog;
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
