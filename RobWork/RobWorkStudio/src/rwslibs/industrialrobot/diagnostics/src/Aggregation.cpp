/**
 * @file   Aggregation.cpp
 * @brief  诊断聚合器实现——CauseLink 链遍历、聚合分组与稳定排序（§6.3/
 *         §6.4/§9.4；头文件为契约文档，本文件注释聚焦算法步骤）。
 *
 * 设计依据：
 *   - units/diagnostics.md §6.3（原因链单父 DAG）、§6.4（聚合键/计数语义/
 *     稳定排序/跨任务不聚合/局部碰撞不升级）、§9.4（IDiagnosticAggregator
 *     契约表——纯函数/确定性/无写路径）
 *   - 需求 NFR-COR-02（确定性——排序键与遍历全部字典序/单调序，无哈希序、
 *     无指针序）、EVI-01（缺失项全量列出——成员明细全保留）
 *   - 任务契约 tasks/foundation/DIAG-T06.json（acceptance 1~4 逐条自证见
 *     test/AggregationTest.cpp）
 *
 * 确定性来源（NFR-COR-02——实现纪律，逐处标注）：
 *   - 条目索引与分组容器一律 std::map（字典序），不用 unordered 容器；
 *   - 成员排序键＝条目 orderKey（时间戳计数, entryId, code 字典序）；
 *   - 视图项排序键＝成员最早 orderKey（成员 orderKey 含唯一 entryId 分量
 *     且每条目恰归一组 → 视图项键互异，输出全序无并列）。
 *
 * 线程安全：全部函数为 const 纯函数、局部状态自持——并发安全（§9.4 契约表）。
 */

#include <sdurws/ird/diagnostics/Aggregation.hpp>

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

#include <sdurws/ird/diagnostics/Errors.hpp>  // DiagnosticsError/DiagnosticsErrorCode（单元唯一异常轨）

namespace sdurws::ird::diagnostics {

namespace {

// ---------------------------------------------------------------------
// 内部原语（匿名命名空间——实现细节不跨翻译单元暴露）
// ---------------------------------------------------------------------

/// 条目按身份的只读索引（std::map 字典序——确定性遍历，NFR-COR-02）。
using EntryIndex = std::map<DiagEntryId, const DiagnosticEntry*>;

/**
 * @brief 严重级别的"级别秩"（§4.3 传播规则"聚合取成员最高 severity"）。
 *
 * 为什么不能直接比较枚举值：DiagnosticSeverity 声明序为 Error/Warning/Info/
 * Dev（数值 0~3），而级别高低序恰为其逆（Error 最高、Dev 最低——§4.3 表的
 * 渠道列：Dev 只进开发级日志）。显式映射表避免"枚举序＝级别序"的误读，也
 * 不依赖枚举数值的稳定性（P-DIAG-3：实现承载词表，值不进持久化契约）。
 *
 * @param severity [in] 级别值（4 值全覆盖）
 * @return 级别秩（Error=3 ＞ Warning=2 ＞ Info=1 ＞ Dev=0；仅用于组内取最大，
 *         无跨单元语义）
 */
int severityRank(DiagnosticSeverity severity) noexcept
{
    switch (severity) {
    case DiagnosticSeverity::Error:   return 3;
    case DiagnosticSeverity::Warning: return 2;
    case DiagnosticSeverity::Info:    return 1;
    case DiagnosticSeverity::Dev:     return 0;
    }
    // 全枚举 switch 已覆盖 4 值；防御性兜底（不可达——值域闭合）按最低秩。
    return 0;
}

/**
 * @brief 输入集合的合法性检查与索引构建（aggregate 前置的机制承载）。
 *
 * 检查序（固定，首错即停——调用方错误 fail-fast，AGENTS.md 错误语义）：
 *   ①entryId 非 0（0＝空值占位，工厂产出条目恒 ≥1——§4.2）；
 *   ②集合内 entryId 唯一（目录子集天然唯一；重复使成员归属与排序含混）；
 *   ③非 Dev 级（§4.3 传播规则"Dev 不参与用户级聚合"；Dev 码不入目录——
 *     §6.2，出现在聚合输入即调用方违约）。
 *
 * @throws DiagnosticsError Usage——上述任一违约（detail 携带违约场景 token）
 */
EntryIndex buildIndexAndValidate(const std::vector<DiagnosticEntry>& entries)
{
    EntryIndex index;
    for (const DiagnosticEntry& entry : entries) {
        // 检查①：空身份条目（未经工厂产出的占位值）。
        if (entry.entryId == 0) {
            throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                   "diagnostics/aggregation/entry-id-zero");
        }
        // 检查②：身份重复（map::emplace 第二返回值为 false 即已存在）。
        if (!index.emplace(entry.entryId, &entry).second) {
            throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                   "diagnostics/aggregation/entry-id-duplicate");
        }
        // 检查③：Dev 级条目（§4.3 传播规则——用户级聚合面不含 Dev）。
        if (entry.severity == DiagnosticSeverity::Dev) {
            throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                   "diagnostics/aggregation/dev-entry-not-aggregatable");
        }
    }
    return index;
}

/**
 * @brief 在输入集合内沿 causedBy 单父链走至"可达最深祖先"（容差遍历）。
 *
 * 与 CauseLink::rootOf（严格契约：断链抛 Usage）不同，聚合分组面对"消费方
 * 过滤子集切断链"的合法输入（§9.4 前置）采用容差口径：逐跳前进，直到当前
 * 条目无 causedBy（真根因）**或**其 causedBy 指向集外条目（断点）——返回
 * 停止位置的条目身份。呈现事实＝集内可见部分（实现口径①，不伪造根因；
 * 链完整性判定归 CauseLink 严格查询）。
 *
 * 环防御：条目环会使本函数死循环——构造路径保证无环（单调 entryId 下
 * causedBy 只指向更早条目，DiagCatalog::append 校验指向已存在条目），此处
 * 仍以跳数上限（＝集合大小）防御：无环单父链至多每条目访问一次，超限且
 * 停不下来即环，抛 Usage（与 CauseLink 同码面：环＝条目被绕过工厂手改，
 * fail-fast 不静默）。
 *
 * @param index [in] 输入集合索引（已过合法性检查）
 * @param start [in] 起点条目身份（须已在索引内——调用点保证）
 * @return 可达最深祖先（或断点）的条目身份
 *
 * @throws DiagnosticsError Usage——链成环（防御性检查，见上）
 */
DiagEntryId walkToReachableAncestor(const EntryIndex& index, DiagEntryId start)
{
    // 起点合法性：调用点（groupKeyOf）以索引内条目身份调用——at 越界即
    // std::out_of_range＝实现内部缺陷，fail-fast 暴露，不吞。
    const DiagnosticEntry* current = index.at(start);

    // 逐跳前进；跳数上限＝集合大小（环防御——见函数注释）。
    for (std::size_t hops = 0; hops < index.size(); ++hops) {
        if (!current->causedBy.has_value()) {
            // 真根因：链尾（无 causedBy）——遍历自然终止。
            break;
        }
        const auto parentIt = index.find(*current->causedBy);
        if (parentIt == index.end()) {
            // 断链：父条目不在输入子集内——停在断点（容差口径，不抛；
            // 断点的 causedBy 事实仍在条目本体上，未被改写）。
            break;
        }
        current = parentIt->second;  // 前进一跳（指向已存在条目——索引内必达）
    }
    // 跳数耗尽仍未停＝环：当前条目仍有 causedBy 且其父在集内——此刻必然
    // 在环上（构造保证不触发；触发即条目被手改——Usage fail-fast）。
    if (current->causedBy.has_value() && index.count(*current->causedBy) != 0u) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "diagnostics/aggregation/cause-cycle-detected");
    }
    return current->entryId;
}

/**
 * @brief 分组键（实现口径①——每条目恰归一组的全序键）。
 *
 * 用 std::tuple 承载三类键并保证全序（std::map 键要求）：
 *   - kind=1（cause-root 组）：按根因条目身份排（第二分量）；
 *   - kind=0（code+scopeKind 组）：按 {code, scopeKind, 有任务, 任务五元组} 排；
 *   - kind=2（单成员私有组）：按条目自身身份排（低阈值逐条通过/无键可归时
 *     的独立成组——视图项即单条目原样呈现）。
 * kind 分量互异 → 三类键不会碰撞；同键条目归同组（map 遍历即确定性分组序）。
 * 任务分区：跨任务不聚合（§6.4——不同 TaskIdentity＝不同 scope）。
 */
using GroupKey = std::tuple<int,             ///< 键类（0=code+scope，1=cause-root，2=单成员私有）
                            DiagEntryId,     ///< cause-root/单成员：条目身份；code 组：0
                            std::string,     ///< code 组：聚合码；其余：空
                            std::uint8_t,    ///< code 组：scopeKind 枚举值；其余：0
                            bool,            ///< code 组：是否有任务分区；其余：false
                            core::TaskIdentity>;  ///< code 组：任务五元组（无任务时为默认值，
                                                  ///  比较语义由前一 bool 分量隔离：false 时该
                                                  ///  分量恒为默认值——同键比较结果稳定）

/**
 * @brief 计算单条目的分组键（实现口径①——键选择与分区判据）。
 *
 * @param entry       [in] 待分组条目
 * @param index       [in] 输入集合索引（cause-root 键的容差遍历用）
 * @param causeRootOn [in] 是否启用 cause-root 键（AggregationRule 投影）；
 *                    false 时有 causedBy 的条目按键二（code+scope）分组——
 *                    "仅 code+scope"窄视图的确定性语义
 * @return 该条目的全序分组键
 */
GroupKey groupKeyOf(const DiagnosticEntry& entry, const EntryIndex& index, bool causeRootOn)
{
    // 键优先序（实现口径①）：条目有 causedBy 且键启用 → cause-root 组。
    if (causeRootOn && entry.causedBy.has_value()) {
        // 容差遍历至集内可达最深祖先（断链停断点——walkToReachableAncestor）。
        const DiagEntryId rootId = walkToReachableAncestor(index, entry.entryId);
        return GroupKey{1, rootId, std::string{}, std::uint8_t{0}, false, core::TaskIdentity{}};
    }
    // 键二：同 code＋同 scopeKind（条目码与去重键的作用域类别——工厂已赋值），
    // 并按任务五元组分区（跨任务不聚合——§6.4；无任务条目同属"无任务域"）。
    const bool hasTask = entry.context.task.has_value();
    const core::TaskIdentity taskPartition
        = hasTask ? *entry.context.task : core::TaskIdentity{};
    return GroupKey{0, 0, entry.record.code, static_cast<std::uint8_t>(entry.dedupKey.scopeKind),
                    hasTask, taskPartition};
}

/**
 * @brief 单成员私有键（低阈值逐条通过/无键可归时的独立成组——kind=2）。
 *
 * 视图项按"条目自身"呈现：组码＝自身码、无根因导航、共同分类/作用域即自身
 * 值——与整组折叠项共用同一构建路径（buildGroupView），字段语义自然退化一致。
 *
 * @param entry [in] 呈现对象条目
 * @return 该条目的私有分组键（entryId 分量保证唯一）
 */
GroupKey standaloneKeyOf(const DiagnosticEntry& entry)
{
    return GroupKey{2, entry.entryId, std::string{}, std::uint8_t{0}, false,
                    core::TaskIdentity{}};
}

/**
 * @brief 由组键与成员集构建视图项（第四步的字段投影，确定性逐字段）。
 *
 * @param key           [in] 组键（决定组码与根因导航字段）
 * @param memberIndices [in] 成员在输入序列中的下标集合（非空——调用点保证）
 * @param entries       [in] 输入集合（成员条目来源）
 * @param index         [in] 输入集合索引（cause-root 组取根因条目码用）
 * @return 视图项（members 已按成员 orderKey 升序；orderKey＝成员最早值）
 */
AggregatedEntry buildGroupView(const GroupKey& key, const std::vector<std::size_t>& memberIndices,
                               const std::vector<DiagnosticEntry>& entries,
                               const EntryIndex& index)
{
    AggregatedEntry view;

    // 第一步：成员下标按成员 orderKey 升序（后续所有"最早/共同值"扫描都以
    // 该确定顺序进行——成员 orderKey 含唯一 entryId，排序无并列）。
    std::vector<std::size_t> ordered = memberIndices;
    std::sort(ordered.begin(), ordered.end(),
              [&entries](std::size_t a, std::size_t b) {
                  return entries[a].orderKey < entries[b].orderKey;
              });

    // 第二步：组码与根因导航字段——cause-root 组＝根因条目的码＋根因身份
    // （§6.3"根因不被聚合吞没"的呈现锚）；code+scope 组＝键内共同码；
    // 单成员私有组＝自身码。
    if (std::get<0>(key) == 1) {
        const DiagEntryId rootId = std::get<1>(key);
        view.causeRootEntryId = rootId;
        view.code = index.at(rootId)->record.code;  // 根因条目必在集内（键来源即索引）
    } else if (std::get<0>(key) == 0) {
        view.code = std::get<2>(key);
    } else {
        view.code = entries[ordered.front()].record.code;
    }

    // 第三步：成员明细（§6.4 保留原始证据行——entryId/subject/localName
    // 逐条保留，关键作用对象不丢失——DT-AGG-3/EVI-01 全量列出）。
    view.members.reserve(ordered.size());
    for (const std::size_t memberIndex : ordered) {
        const DiagnosticEntry& member = entries[memberIndex];
        AggregatedEntry::MemberDetail detail;
        detail.entryId = member.entryId;
        detail.subject = member.record.subject;      // core::ObjectId 内嵌（P-DIAG-1 基线）
        detail.localName = member.record.localName;  // 缺失即 nullopt（不伪造）
        view.members.push_back(std::move(detail));
    }

    // 第四步：计数与排序键——occurrences＝成员实际次数总和（每成员计 1，
    // §6.4 计数语义行，不是"≥N"截断）；视图 orderKey＝成员最早 orderKey
    // （第一步排序后的首成员即最早——min 语义）。
    view.occurrences = view.members.size();
    view.orderKey = entries[ordered.front()].orderKey;

    // 第五步：成员最高严重级别（§4.3 传播规则——按级别秩取最大，非枚举序；
    // Dev 已被入口检查排除，此处秩比较仅为规则完备性）。
    view.severity = entries[ordered.front()].severity;
    for (const std::size_t memberIndex : ordered) {
        if (severityRank(entries[memberIndex].severity) > severityRank(view.severity)) {
            view.severity = entries[memberIndex].severity;
        }
    }

    // 第六步：共同分类——全部成员同分类时投影为其值（呈现分组）；不一致＝
    // nullopt（不私造汇总分类——呈现层按明细自行细分；恒不产生 InfeasibilityProof
    // ——C8/DT-AGG-4，类型层面亦无该字段可写）。
    view.category = entries[ordered.front()].category;
    for (const std::size_t memberIndex : ordered) {
        if (entries[memberIndex].category != view.category) {
            view.category = std::optional<DiagnosticCategory>{};  // 不一致→nullopt
            break;
        }
    }

    // 第七步：共同作用域——同第六步口径（同根因组跨作用域时允许 nullopt）。
    view.scopeKind = entries[ordered.front()].dedupKey.scopeKind;
    for (const std::size_t memberIndex : ordered) {
        if (entries[memberIndex].dedupKey.scopeKind != view.scopeKind) {
            view.scopeKind = std::optional<ScopeKind>{};  // 不一致→nullopt
            break;
        }
    }

    return view;
}

}  // namespace

// =====================================================================
// CauseLink（§6.3——严格契约的链遍历；与聚合的容差遍历判据对照见头文件）
// =====================================================================

namespace CauseLink {

namespace {

/**
 * @brief 严格链遍历（rootOf/chainOf 的共同实现）。
 *
 * 遍历判据与容差版（walkToReachableAncestor）的差异——两种输入违约从"容差
 * 停止"升级为 Usage fail-fast（显式链查询的契约＝链完整子集）：
 *   - 断链：某跳 causedBy 指向集外 → Usage（不静默返回半截链——半截链会让
 *     "翻译后只剩最后一层"这类缺陷从观测面上消失，正是 DT-CHAIN-1 要抓的）；
 *   - 环：跳数超过集合大小 → Usage（构造保证无环，防御性检查）。
 *
 * @throws DiagnosticsError Usage——start 缺失／断链／环（detail 注明场景）
 */
std::vector<DiagEntryId> walkChain(const std::vector<DiagnosticEntry>& entries,
                                   DiagEntryId start)
{
    // 只读索引（字典序构建——与聚合侧同构，确定性遍历）。
    EntryIndex index;
    for (const DiagnosticEntry& entry : entries) {
        index.emplace(entry.entryId, &entry);
    }

    // 起点必须在集合内（0＝空身份同样缺失——显式查询的前置）。
    const auto startIt = index.find(start);
    if (start == 0 || startIt == index.end()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "diagnostics/causelink/start-not-in-set");
    }

    std::vector<DiagEntryId> chain{start};
    const DiagnosticEntry* current = startIt->second;

    // 跳数上限＝集合大小：无环单父链至多访问每个条目一次；超限即环。
    for (std::size_t hops = 0; hops < index.size(); ++hops) {
        if (!current->causedBy.has_value()) {
            // 真根因：链尾到达（自身无 causedBy 的起点返回单元素链）。
            return chain;
        }
        const auto parentIt = index.find(*current->causedBy);
        if (parentIt == index.end()) {
            // 断链：链上某跳祖先不在集合——显式查询契约违约，不返回半截链。
            throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                   "diagnostics/causelink/chain-broken-at-set-boundary");
        }
        chain.push_back(parentIt->first);  // 链序追加（起点→…→根因）
        current = parentIt->second;
    }
    // 跳数耗尽仍未到链尾＝环（构造保证不触发；触发即条目被手改——fail-fast）。
    throw DiagnosticsError(DiagnosticsErrorCode::Usage, "diagnostics/causelink/cycle-detected");
}

}  // namespace

DiagEntryId rootOf(const std::vector<DiagnosticEntry>& entries, DiagEntryId start)
{
    return walkChain(entries, start).back();  // 链尾元素＝根因（walkChain 保证链非空）
}

std::vector<DiagEntryId> chainOf(const std::vector<DiagnosticEntry>& entries, DiagEntryId start)
{
    return walkChain(entries, start);
}

}  // namespace CauseLink

// =====================================================================
// DiagnosticAggregator（§9.4 实现——算法步骤见类注释与行内注释）
// =====================================================================

std::vector<AggregatedEntry> DiagnosticAggregator::aggregate(
    const std::vector<DiagnosticEntry>& entries, AggregationScope scope) const
{
    // 前置检查①：折叠阈值 ≥1（0＝无意义配置——任何正成员数都满足"≥0"，
    // 阈值语义失效，属调用方配置违约）。
    if (scope.minGroupSize == 0) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "diagnostics/aggregation/min-group-size-zero");
    }

    // 第一步：输入合法性检查与索引构建（entryId 0/重复/Dev 级——Usage）。
    // 空输入在此直接得到空索引，后续步骤自然产出空视图（空集聚合＝空视图，
    // 合法——§9.4 无"空输入拒绝"的契约行）。
    const EntryIndex index = buildIndexAndValidate(entries);

    // 第二步：按规则投影确定各键是否启用。
    //   Auto＝双键并用（§6.4 聚合键行"或"）；CauseRoot＝仅键一；CodeAndScope
    //   ＝仅键二。有 causedBy 的条目走键一（若启用），否则走键二（若启用）；
    //   对应键未启用时条目单成员私有成组（kind=2——不跨键混组，确定性）。
    const bool causeRootOn = scope.rule != AggregationRule::CodeAndScope;
    const bool codeScopeOn = scope.rule != AggregationRule::CauseRoot;

    // 第三步：逐条目计算分组键并聚组（std::map——键全序，组间遍历确定）。
    //   组值＝成员在输入序列中的下标集合（用下标不用指针——不依赖地址序，
    //   NFR-COR-02）。
    std::map<GroupKey, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const DiagnosticEntry& entry = entries[i];
        const bool entryIsDerived = entry.causedBy.has_value();
        // 键归属：派生条目且键一启用 → 键一；非派生且键二启用 → 键二；
        // 其余（对应键未启用）→ 单成员私有键（kind=2）。
        GroupKey key = standaloneKeyOf(entry);
        if (entryIsDerived ? causeRootOn : codeScopeOn) {
            key = groupKeyOf(entry, index, causeRootOn);
        }
        groups[key].push_back(i);
    }

    // 第四步：组 → 视图项（折叠阈值过滤——达到阈值整组折叠；低于阈值逐条
    // 以单成员视图项通过。呈现不丢任何条目——聚合视图是全集视图，不是
    // "只显示大组"的过滤器）。
    std::vector<AggregatedEntry> view;
    view.reserve(groups.size());
    for (const auto& [key, memberIndices] : groups) {
        if (memberIndices.size() >= scope.minGroupSize) {
            view.push_back(buildGroupView(key, memberIndices, entries, index));
        } else {
            for (const std::size_t memberIndex : memberIndices) {
                view.push_back(buildGroupView(standaloneKeyOf(entries[memberIndex]),
                                              std::vector<std::size_t>{memberIndex}, entries,
                                              index));
            }
        }
    }

    // 第五步：视图项按视图 orderKey（成员最早 orderKey）升序排序。
    //   成员 orderKey 含唯一 entryId 分量且每条目恰归一组 → 视图项键互异，
    //   std::sort 即确定全序（无并列——稳定性不影响结果，DT-AGG-2）。
    std::sort(view.begin(), view.end(),
              [](const AggregatedEntry& a, const AggregatedEntry& b) {
                  return a.orderKey < b.orderKey;
              });
    return view;
}

std::size_t DiagnosticAggregator::occurrenceCount(const std::vector<DiagnosticEntry>& entries,
                                                  const DedupKey& key) const
{
    // 纯计数查询：四元组全等线性扫描（§9.4"目录侧增量维护；此处为纯查询"
    // ——不做去重折叠（PA-1：折叠计数权威归 DiagCatalog），只回答"集合内
    // 有多少条目命中该键"这一事实）。
    std::size_t count = 0;
    for (const DiagnosticEntry& entry : entries) {
        if (entry.dedupKey == key) {
            ++count;
        }
    }
    return count;
}

}  // namespace sdurws::ird::diagnostics
