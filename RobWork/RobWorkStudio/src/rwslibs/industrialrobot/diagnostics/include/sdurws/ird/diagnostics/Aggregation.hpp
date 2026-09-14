/**
 * @file   Aggregation.hpp
 * @brief  诊断聚合器——CauseLink 原因链遍历（§6.3）、聚合规则与聚合视图
 *         （AggregationScope/AggregatedEntry，§6.4）、IDiagnosticAggregator
 *         接口与无状态实现（§9.4）。
 *
 * 设计依据：
 *   - units/diagnostics.md §6.3（原因链——causedBy 单父 DAG/根因不被聚合吞没/
 *     错误转换不得丢失根因）、§6.4（聚合与去重规则——聚合键/保留原始证据/
 *     occurrences 实际次数/局部碰撞不自动升级/稳定排序/跨任务不聚合）、
 *     §6.5（诊断关系图——聚合仅为呈现折叠）、§9.4（IDiagnosticAggregator
 *     接口签名逐字＋契约表：纯函数/并发安全/无副作用/确定性）
 *   - 需求 ERR-01（诊断字段/绑定对象——成员明细携带作用对象）、EVI-01
 *     （证据缺失全量列出——聚合不吞缺失项，§8.1 表 2④）、NFR-COR-02
 *     （确定性——orderKey 稳定排序，同输入同序）、C5/C8 作用域（§8.1 表——
 *     搜索未果与构型级碰撞均不构成任务级不可行，聚合无判定权）
 *   - 任务契约 tasks/foundation/DIAG-T06.json（§11 DIAG-T06 行产物
 *     `Aggregation.*`＝CauseLink/规则/稳定排序；knownPitfalls P-DIAG-1）
 *
 * 背景说明（为什么聚合是"纯查询视图"而不是目录内的自动重写）：
 *   目录条目是原始事实流（§6.4 触发行"不在创建时自动聚合"），PA-1 权威唯一
 *   ——去重与条目生命周期归 DiagCatalog（DIAG-T04/T09），聚合只做**呈现层
 *   分组**（UI 诊断表折叠/报告摘要的视图请求），对输入集合零写路径、零副作用
 *   （§9.4 契约表"接口无写路径"）。因此本模块全部接口为 const 纯函数：同输入
 *   同输出（DT-AGG-5/NFR-COR-02），进程级共享无状态。
 *
 * 聚合无工程判定权（C8/C5 边界——§9.4 契约表"非法调用"行）：
 *   聚合结果只携带成员明细分组，**不产生任何任务级不可行结论**（C8：任务级
 *   仅必经状态碰撞/解析界限/约束矛盾三类证明，证明由域产生、evidence 校验
 *   ——本单元无此判定权；DT-AGG-4 反例钉住）。搜索未果口径同表核对（C5）：
 *   DataInsufficient 类条目聚合后仍是数据不足呈现分组，不升级为不可行。
 *   AggregatedEntry 类型层面即无"证明/不可行"字段——期望聚合产出工程判定
 *   在类型上不可表达（§9.4"无此能力"的机制承载）。
 *
 * 陷阱处置锚点（契约 knownPitfalls）：
 *   - P-DIAG-1：AggregatedEntry::MemberDetail 内嵌 core::ObjectId（core.md
 *     v0.1 Draft 未冻结基线）——以 core 契约为基线消费，core 冻结 diff 后
 *     增量同步，不私改 core；编译期类型钉住见测试套件（DIAG-T04 先例）。
 *
 * 实现口径登记（§9.4 未定义判据，DTB §5.4——随单元卡 §14.4 v0.7 登记）：
 *   ①聚合键的成员归属单组化：条目有 causedBy → 归"同根因"组（键＝沿单父链
 *     的集内可达最深祖先，链断裂时键＝断点条目自身）；无 causedBy → 归
 *     "同 code＋同 scopeKind"组，且按 DiagContext.task 五元组分区（跨任务
 *     不聚合——§6.4 多工况/多对象/多任务行）。每条目恰归一组（确定性）。
 *   ②根因条目本身不入"同根因"组成员（成员＝共享根因的派生条目；根因以
 *     causeRootEntryId 导航呈现，不被聚合吞没——§6.3 注记）。
 *   ③relatedTo/supersedes 不参与聚合判定（§6.4——relatedTo 明文"不参与"；
 *     supersedes 语义＝新条目取代旧条目的导航，目录不可变纪律已由条目本体
 *     承载，聚合视图原样呈现两代条目，不做合并）。
 *   ④minGroupSize＝折叠阈值（达到方成组，低于阈值逐条以单成员视图项通过）；
 *     默认 2（显式视图折叠任意重复）——§6.4 触发行"目录内自动聚合阈值
 *     默认 ≥3"是目录侧触发（DIAG-T09 容量/清理面），与本视图参数无涉。
 *
 * 线程安全：全部接口为 const 纯函数、无共享可变状态（§9.4 契约表"线程/
 * 副作用"行）——同一实例可并发调用；实现类无状态（进程级共享）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_AGGREGATION_HPP
#define SDURWS_IRD_DIAGNOSTICS_AGGREGATION_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>          // core::ObjectId/TaskIdentity（P-DIAG-1 基线）
#include <sdurws/ird/diagnostics/Catalog.hpp>    // DiagEntryId/DedupKey/ScopeKind/OrderKey/DiagnosticEntry
#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // DiagnosticCategory/DiagnosticSeverity（码表词表）

namespace sdurws::ird::diagnostics {

// =====================================================================
// CauseLink（§6.3 原因链的遍历设施——DT-CHAIN-1 观测面）
// =====================================================================

/**
 * @namespace CauseLink
 * @brief 原因链遍历（§6.3——causedBy 单父 DAG 的根因求取与全链展开）。
 *
 * 三种关系的语义承载与遍历设施的关系（§6.3 关系表逐行）：
 *   - causedBy：单父链（本命名空间 traversal 的唯一对象——链形关系才有
 *     "遍历到根因"的语义；DT-CHAIN-1"链遍历至 EX-WORKER-CRASHED"）；
 *   - relatedTo：多值、无方向的关联导航（明文"不参与聚合判定"——无链序
 *     可遍历，语义由 DiagnosticEntry::relatedTo 字段原样承载，不设设施）；
 *   - supersedes：单值取代导航（旧条目保留不删的不可变纪律由条目本体与
 *     目录承载；聚合视图不做新旧合并，原样呈现——本命名空间不预建无消费
 *     者的查询，NFR-MNT-04）。
 *
 * 错误转换不得丢失根因（§6.3/§8.1 规则 1）：转换器产出派生条目时必须链接
 * causedBy 根因条目，禁止"翻译后只剩最后一层"——本命名空间即该纪律的
 * 观测面：链上任何一层的 causedBy 缺失都会使 rootOf/chainOf 无法抵达真根因。
 *
 * 线程安全：纯函数；对输入集合只读。
 */
namespace CauseLink {

/**
 * @brief 求原因链根因（沿 causedBy 单父链走至链尾条目）。
 *
 * 遍历语义：从 start 出发逐跳取 entry.causedBy，直到某条目无 causedBy
 * （真根因——§6.3"指向根因方向"的链尾）。目录侧已保证"指向已存在条目"
 * （DiagCatalog::append 校验）且单调 entryId 下天然无环（DAG 由构造保证）
 * ——本函数对"环"与"断链"仍做防御性检查（见 @throws），不依赖调用环境。
 *
 * @param entries [in] 目录条目子集（只读；须为**链完整**子集——start 的
 *                每一跳祖先都在集合内。经消费方过滤的子集若切断链，属调用方
 *                契约违约：显式链查询要么给全集、要么给含根的子集；聚合视图
 *                对断链另有容差口径——见 DiagnosticAggregator::aggregate）
 * @param start   [in] 起点条目身份（≥1，须在 entries 内）
 *
 * @return 根因条目身份（start 自身无 causedBy 时返回 start 自身——自身即根）
 *
 * @throws DiagnosticsError Usage——start 不在集合内／start.entryId 为 0／
 *         链上某跳的 causedBy 指向集外条目（断链）／链成环（防御性检查，
 *         构造保证不触发；触发即说明条目被绕过工厂手改——fail-fast 不静默）
 */
DiagEntryId rootOf(const std::vector<DiagnosticEntry>& entries, DiagEntryId start);

/**
 * @brief 展开完整原因链（起点 → … → 根因，含两端，链序排列）。
 *
 * 与 rootOf 同一遍历判据，返回沿途全部条目身份（首个元素＝start，末个元素
 * ＝rootOf 的返回值）。DT-CHAIN-1 的观测点即本函数：派生条目的链必须逐层
 * 完整（中间层在链上），"翻译后只剩最后一层"会使返回链长度不足或断链抛错。
 *
 * @param entries [in] 同 rootOf（链完整子集）
 * @param start   [in] 同 rootOf
 *
 * @return 链上条目身份序列（起点→根因；长度 ≥1——至少含 start 自身）
 *
 * @throws DiagnosticsError Usage——同 rootOf 四种触发条件
 */
std::vector<DiagEntryId> chainOf(const std::vector<DiagnosticEntry>& entries,
                                 DiagEntryId start);

}  // namespace CauseLink

// =====================================================================
// 聚合规则与作用域（§6.4 规则表的参数承载）
// =====================================================================

/**
 * @brief 聚合键选择（§6.4 聚合键行"同 causedBy 根因 或 同 code＋同
 *        scopeKind"的两键机器承载）。
 *
 * 两键不可同时作用于同一条目（每条目恰归一组——实现口径①，确定性）；
 * 本枚举控制 aggregate 启用哪类键：双键并用（默认，卡原文口径）或只用其一
 * （消费方只关心某类折叠的窄视图场景）。键的判据细节见 DiagnosticAggregator::
 * aggregate 实现注释。
 */
enum class AggregationRule : std::uint8_t {
    Auto,          ///< auto——双键并用（§6.4 聚合键行原文"或"的默认落地）
    CauseRoot,     ///< cause-root——仅"同 causedBy 根因"键
    CodeAndScope,  ///< code-and-scope——仅"同 code＋同 scopeKind"键（含跨任务分区）
};

/**
 * @brief 聚合视图请求的作用域与规则参数（§9.4 aggregate 签名的 scope 形状
 *        ——卡未定形状，实现口径④登记）。
 *
 * 值语义；默认值＝"双键并用＋成组阈值 2"的显式折叠视图。
 */
struct AggregationScope {
    /// 聚合键选择（实现口径①——每条目恰归一组；默认双键并用）。
    AggregationRule rule = AggregationRule::Auto;
    /// 成组折叠阈值：成员数达到该值方折叠为一条聚合视图项，低于阈值逐条以
    /// 单成员项通过（§6.4 触发行"阈值可配"的视图侧承载；默认 2——显式视图
    /// 折叠任意重复；目录侧自动聚合阈值 ≥3 归 DIAG-T09，与本参数无涉）。
    /// 取值 ≥1；0＝调用方契约违约（Usage）。
    std::size_t minGroupSize = 2;
};

// =====================================================================
// AggregatedEntry（§6.4/§9.4——呈现层汇总条目，成员引用全保留）
// =====================================================================

/**
 * @brief 聚合视图项（§6.4 聚合表/§9.4 前置声明——目录条目的呈现分组，
 *        **不是**目录条目：无 entryId、不入目录、不持久化）。
 *
 * 与目录条目的边界（§6.4"保留原始证据"行——DT-AGG-3 反例钉住）：
 *   - 聚合**不吞**任何成员：members 逐条携带 entryId＋subject＋localName，
 *     展开即得逐对象明细（关键作用对象全量可列——EVI-01"全量列出"口径）；
 *   - occurrences＝成员实际次数总和（不是"≥N"截断）；
 *   - 分类/严重只是成员元数据的**共同值投影**（取不到共同值即 nullopt，
 *     不私造汇总分类）——绝不产生成员分类之外的新语义（局部碰撞聚合后仍是
 *     "策略拒绝"类 Warning——C8/DT-AGG-4）；
 *   - 本类型**没有**任何证明/不可行字段（§9.4 契约表"非法调用"行"期望
 *     聚合产出工程判定（无此能力）"的机制承载）。
 *
 * P-DIAG-1：MemberDetail.subject 内嵌 core::ObjectId（core.md v0.1 基线，
 * Draft 未冻结）——core 冻结 diff 后增量同步，不私改 core。
 *
 * 线程安全：纯值（不可变共享安全——聚合视图产出后由消费方持有）。
 */
struct AggregatedEntry {
    /**
     * @brief 逐成员明细（§6.4"持全部成员 entryId 引用＋各成员 subject/
     *        localName"行的承载——聚合摘要不丢失关键作用对象）。
     */
    struct MemberDetail {
        DiagEntryId entryId = 0;                 ///< 成员目录条目身份（≥1）
        std::optional<core::ObjectId> subject;   ///< 作用对象（用户级成员必有——ERR-01；
                                                 ///  P-DIAG-1：core 契约值内嵌）
        std::optional<std::string> localName;    ///< 局部名（UX-10 对象定位；缺失＝nullopt 不伪造）

        bool operator==(const MemberDetail& o) const
        {
            return entryId == o.entryId && subject == o.subject && localName == o.localName;
        }
        bool operator!=(const MemberDetail& o) const { return !(*this == o); }
    };

    /// 聚合码（呈现标题的主码）：code+scopeKind 组＝成员共同码；cause-root
    /// 组＝键根因条目的码（成员各自码在明细中仍逐条可查——组码只是呈现主键）。
    std::string code;
    /// 同根因组的根因条目身份（§6.3"根因不被聚合吞没"的导航锚——展开明细后
    /// 可跳到根因；code+scopeKind 组＝nullopt）。
    std::optional<DiagEntryId> causeRootEntryId;
    /// 成员最高严重级别（§4.3 传播规则"聚合取成员最高 severity"；级别序
    /// Error＞Warning＞Info＞Dev——枚举声明序不是级别序，实现按级别秩取最大）。
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    /// 成员共同分类（全部成员同分类时为其值；不一致＝nullopt——呈现层按明细
    /// 自行细分，聚合器不私造汇总分类）；恒不产生 InfeasibilityProof（C8）。
    std::optional<DiagnosticCategory> category;
    /// 成员共同作用域类别（全部成员同 scopeKind 时为其值；不一致＝nullopt
    /// ——同根因组跨作用域时允许发生）。
    std::optional<ScopeKind> scopeKind;
    /// 成员实际次数总和（§6.4 计数语义行——每成员计 1、总和即成员数；
    /// 不是"≥N"截断值。目录侧去重计数归 DiagCatalog，聚合器不重计——PA-1）。
    std::size_t occurrences = 1;
    /// 视图排序键＝成员最早 orderKey（§6.4 稳定排序行——聚合视图输出序按
    /// 本键升序；成员 orderKey 含 entryId 分量保证组间键唯一，输出全序确定）。
    OrderKey orderKey{};
    /// 全部成员明细（按成员 orderKey 升序——组内顺序同样确定，NFR-COR-02）。
    std::vector<MemberDetail> members;

    bool operator==(const AggregatedEntry& o) const
    {
        return code == o.code && causeRootEntryId == o.causeRootEntryId
            && severity == o.severity && category == o.category && scopeKind == o.scopeKind
            && occurrences == o.occurrences && orderKey == o.orderKey && members == o.members;
    }
    bool operator!=(const AggregatedEntry& o) const { return !(*this == o); }
};

// =====================================================================
// IDiagnosticAggregator（§9.4 接口签名逐字）与实现
// =====================================================================

/**
 * @brief 诊断聚合器接口（§9.4 原文签名——聚合视图与去重计数的纯查询面）。
 *
 * 行为契约（§9.4 契约表逐行）：
 *   - 线程/副作用：纯函数、并发安全、无副作用、确定性（同输入同输出——
 *     DT-AGG-5）；不改写输入集合（接口无写路径——"期望聚合改写原条目"非法）。
 *   - 生命周期：无状态；进程级共享。
 *   - 合法调用：UI 折叠视图、报告摘要、目录容量统计（occurrenceCount）。
 *   - 非法调用：期望聚合改写原条目；期望聚合产出工程判定（无此能力——C8
 *     边界，类型层面无字段可承载）。
 */
class IDiagnosticAggregator {
public:
    virtual ~IDiagnosticAggregator() = default;

    /**
     * @brief 聚合视图（§9.4 原文签名——纯函数式查询；不改目录）。
     *
     * 前置：entries 为目录子集（按消费方过滤：任务/修订/全部）；条目均经
     * 工厂产出（entryId ≥1 且集合内唯一）且非 Dev 级（Dev 不参与用户级聚合
     * ——§4.3 传播规则；Dev 码本不入目录，出现在输入即调用方契约违约）；
     * scope.minGroupSize ≥1。
     * 后置：返回聚合视图项列表（稳定排序；成员 subject 明细保留；局部碰撞
     * 不升级——仅呈现分组）；同输入同输出（逐元素相等）。
     *
     * @param entries [in] 目录条目子集（只读；本函数不修改、不接管）
     * @param scope   [in] 聚合规则与阈值（AggregationScope）
     *
     * @return 聚合视图项（按视图 orderKey 升序；输入为空集时返回空集）
     *
     * @throws DiagnosticsError Usage——条目 entryId 为 0／集合内 entryId 重复／
     *         含 Dev 级条目／minGroupSize 为 0（均属调用方契约违约，fail-fast）
     */
    virtual std::vector<AggregatedEntry> aggregate(const std::vector<DiagnosticEntry>& entries,
                                                   AggregationScope scope) const = 0;

    /**
     * @brief 去重计数（§9.4 原文签名——目录侧增量维护；此处为纯查询）。
     *
     * 语义：线性统计输入集合中 dedupKey 与给定键全等的条目数（§6.4 去重键
     * 四元组等值）。目录内的去重折叠计数由 DiagCatalog 增量维护（DIAG-T04
     * ——PA-1 权威唯一，聚合器不重计目录状态）；本查询面向"持有原始条目批"
     * 的消费方（如分批回传合并前的容量统计——§9.4 合法调用"目录容量统计"）。
     *
     * @param entries [in] 条目集合（只读；可含任意 dedupKey——计数即事实）
     * @param key     [in] 去重键（四元组全等匹配）
     *
     * @return 集合内命中该键的条目数（0＝无命中）
     */
    virtual std::size_t occurrenceCount(const std::vector<DiagnosticEntry>& entries,
                                        const DedupKey& key) const = 0;
};

/**
 * @brief 聚合器实现（§9.4——无状态纯函数实现；ErrorCodeTranslator 一体化
 *        先例的反面：聚合与目录/工厂无共享状态，独立实现类不别名——§3.1
 *        卡行"IDiagnosticAggregator"即本类型与接口两实体的总称）。
 *
 * aggregate 分组判据（实现口径①，登记于单元卡 §14.4 v0.7）：
 *   - 键一（cause-root）：条目有 causedBy → 从该条目沿单父链在**输入集合内**
 *     走至可达最深祖先；祖先无 causedBy（真根因在集内）→ 键根因＝该祖先
 *     （其身份进 causeRootEntryId）；链在集内断裂（某跳祖先不在集合）→ 键
 *     ＝断点条目自身（causeRootEntryId＝断点身份）。容差理由：§9.4 前置
 *     明示输入可为消费方过滤子集，断链是合法输入形态——聚合视图对断链按
 *     "集内可见部分"分组（呈现事实），不抛错不伪造根因；链完整性判定归
 *     CauseLink::rootOf/chainOf（显式链查询的严格契约）。
 *   - 键二（code+scopeKind）：条目无 causedBy → 键＝{record.code, dedupKey.
 *     scopeKind}，并按 context.task 五元组分区（有 task 用五元组等值分区；
 *     无 task 视为同一"无任务域"——§6.4"跨任务不聚合"仅约束有任务归属的
 *     条目）。多对象折叠即此键：同策略对 N 个关节的 N 条 → 一条"N 个对象
 *     受影响"视图项，N 个 subject 全在明细（DT-AGG-3）。
 *   - 单组化：每条目恰归一组（有 causedBy 走键一，否则键二）；Auto＝双键
 *     并用，CauseRoot/CodeAndScope＝仅对应键（只启用键一时，无 causedBy
 *     条目各自单成员成组——不跨键混组）。
 *   - 折叠：组成员数 ≥ scope.minGroupSize → 折叠为一条视图项；低于阈值
 *     逐条以单成员视图项通过（§6.4 触发行阈值语义）。
 *   - 排序：视图项按视图 orderKey（成员最早 orderKey）升序；组内明细按成员
 *     orderKey 升序。成员 orderKey 含 entryId 分量且每条目恰归一组 → 视图项
 *     键互异，输出为确定全序（DT-AGG-2"两次构建同序"）。
 */
class DiagnosticAggregator final : public IDiagnosticAggregator {
public:
    /// @brief 构造（无状态——所有成员函数为 const 纯函数，实例可进程级共享）。
    DiagnosticAggregator() = default;

    // 纯值语义（无资源、无禁拷贝需求——拷贝/移动即无操作语义）。
    DiagnosticAggregator(const DiagnosticAggregator&) = default;
    DiagnosticAggregator& operator=(const DiagnosticAggregator&) = default;

    // ---- IDiagnosticAggregator（§9.4 两方法——行为契约见接口注释）----
    std::vector<AggregatedEntry> aggregate(const std::vector<DiagnosticEntry>& entries,
                                           AggregationScope scope) const override;
    std::size_t occurrenceCount(const std::vector<DiagnosticEntry>& entries,
                                const DedupKey& key) const override;
};

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_AGGREGATION_HPP
