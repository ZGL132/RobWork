/**
 * @file   PanelRefresh.hpp
 * @brief  建模面板刷新协调（零 Qt）——事件驱动刷新＋工作集保序重演＋
 *         UI 线程守卫（卡 §9.7.4；L-4）。
 *
 * 设计依据：
 *   - units/modeling.md §9.7.2 L-4（订阅⑤事件〔修订产生/失效通知〕→重载
 *     基线闭包→工作集按"基线＋未应用编辑"保序重演〔重演失败提示手工
 *     处置，不静默丢弃编辑〕→全面板刷新）、§9.7.4（刷新一律事件驱动，
 *     禁止轮询；面板不缓存模型权威数据；编辑器仅 UI 线程访问——§3.4）
 *   - §4.9（编辑态/草稿态/已应用修订三态——重演的语义边界）
 *   - 任务契约 tasks/foundation/WP-13-T15.json acceptance 3/5
 *
 * 背景说明：⑤修订事件的**投递面**归 ui（事件总线 UI 线程投递——ui.md
 * §6.1；P-MDL-8：未落位面以卡文本为基线）。本协调器承接投递后的**建模
 * 侧编排**：收到"修订已产生/失效"通知→以新基线闭包重建工作集→把用户
 * 未应用编辑按发生序逐条重演（域函数逐条裁决）→失败即停并标记手工处置
 * （失败编辑与后续编辑保留在待重演队列——不静默丢弃，用户可见可处置）。
 * 无轮询定时器、无后台线程——刷新只由显式通知触发（§9.7.4 结构性实现）。
 *
 * 线程约束：仅 UI 线程访问（§3.4）；本头自带 UI 线程守卫（std::thread::id
 * 对比——调试期违约即抛，见 PanelUiThreadGuard）。
 */

#ifndef IRD_MODELING_PLUGIN_PANELREFRESH_HPP
#define IRD_MODELING_PLUGIN_PANELREFRESH_HPP

#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>  // core::RevisionId（基线修订身份）
#include <sdurws/ird/modeling/Template.hpp>  // ModelingWorkingSet/JointEditField/JointEditValue（重演的域入口）

namespace sdurws::ird::modeling {

// =====================================================================
// UI 线程守卫（§3.4"编辑器仅 UI 线程访问"的运行期执行面）
// =====================================================================

/**
 * @brief UI 线程守卫（零 Qt——std::thread::id 对比；widget 构造时绑定
 *        所属线程，跨线程进入编辑/刷新面即 fail-fast）。
 *
 * 为什么需要运行期守卫：§3.4 的"仅 UI 线程"是会话对象约束（非线程安全
 * ≠线程安全缺失警告——违例是数据竞争，属调用方契约违约）；AGENTS 错误
 * 语义：调用方错误 fail-fast（抛异常），不静默容错。release 行为与 debug
 * 一致（违例不可恢复——宁可崩溃不静默竞争）。
 *
 * 线程安全：本类自身只读（构造后 owner 不变）——可在任意线程检查。
 */
class PanelUiThreadGuard {
public:
    /// 绑定 owner 线程（widget 构造线程＝UI 线程——装配期一次）。
    PanelUiThreadGuard() : m_owner(std::this_thread::get_id()) {}

    /// 不可拷贝/不可移动（owner 绑定构造线程语义）。
    PanelUiThreadGuard(const PanelUiThreadGuard&) = delete;
    PanelUiThreadGuard& operator=(const PanelUiThreadGuard&) = delete;

    /// 在 owner 线程上（恒 true——检查面）。
    bool onUiThread() const noexcept { return m_owner == std::this_thread::get_id(); }

    /// 断言当前线程＝owner 线程；违约抛 std::runtime_error（fail-fast）。
    void assertOnUiThread() const
    {
        if (!onUiThread()) {
            throw std::runtime_error(
                "modeling 面板：编辑器/工作集仅 UI 线程访问（§3.4）——检测到跨线程调用");
        }
    }

private:
    std::thread::id m_owner;  ///< owner 线程（构造线程——UI 线程锚）
};

// =====================================================================
// 刷新协调器（L-4——事件驱动的建模侧编排）
// =====================================================================

/**
 * @brief 一条待重演的未应用编辑（保序队列的元素——编辑发生序＝数组序）。
 *
 * 与 ModelingChangeRecord 的分工：变更记录是域侧"已发生"摘要（append-only
 * 留痕）；本值是面板侧"未应用"意图（重演输入）。字段四元组恰为
 * applyJointFieldEdit 的入参形状——重演即逐条原样回放（零重新解释）。
 */
struct PendingEdit {
    std::size_t jointIndex = 0;  ///< 关节下标（链序——域函数入参）
    JointEditField field = JointEditField::Type;  ///< 待编辑字段（四值词表）
    JointEditValue value{};      ///< 新值载荷（备择与 field 匹配——契约同域函数）
};

/**
 * @brief 重演结果（L-4 三态——分流供调用方/测试判别）。
 */
enum class ReplayOutcome {
    Replayed,     ///< 全部待重演编辑按序落位（工作集＝基线＋全部编辑）
    BlockedAtEdit ///< 第 failedIndex 条重演被域拒绝（其后编辑保留——手工处置）
};

/**
 * @brief 刷新协调器（面板持有——UI 线程对象；非线程安全，§3.4）。
 *
 * 使用时序（widget 接线约定）：
 *   1. 装配期 setReplaySink（全面板刷新出口——重演完成后 widget 现取
 *      工作集重投影各分区）；
 *   2. 用户每次编辑接受后 recordPending（入队——保序）；
 *   3. 收到⑤通知（修订产生/失效）→ onRevisionEvent：重载基线闭包（由
 *      注入的 provider 现取——零缓存）→保序重演→全面板刷新；
 *   4. 手工处置：用户对 blocked 状态决议（放弃该编辑/基于新基线重编辑）
 *      → discardPendingFrom/clearPending——入口只在此，无自动丢弃路径。
 *
 * ★ 零缓存纪律（§9.7.4）：本类不保存工作集/报告的任何副本——基线一律
 * 经 provider 现取（闭包重建归装配层，PA-1）；待重演队列是**编辑意图**
 * （用户输入），不是模型权威数据——其为面板私有会话态，不违"第二真值"
 * 红线（权威仍在工作集）。
 */
class PanelRefreshCoordinator {
public:
    /// 基线工作集提供器（⑤后重载入口——L5 装配层注入；返回会话权威工作
    /// 集的引用〔UI 线程单例——重演直接落在权威工作集上，重演被域接受的
    /// 前缀编辑即真实编辑〕；非 owning，调用期存活）。
    using BaselineProvider = std::function<ModelingWorkingSet&()>;
    /// 全面板刷新出口（重演完成后调用——widget 现取重投影）。
    using RefreshSink = std::function<void()>;

    /// 绑定 UI 线程（构造线程——widget 构造期创建）。
    PanelRefreshCoordinator() = default;

    /// 不可拷贝/不可移动（线程守卫与队列绑定会话生命周期）。
    PanelRefreshCoordinator(const PanelRefreshCoordinator&) = delete;
    PanelRefreshCoordinator& operator=(const PanelRefreshCoordinator&) = delete;

    /// 注入基线提供器与刷新出口（装配期一次；均必须非空——否则装配缺陷）。
    void setSinks(BaselineProvider provider, RefreshSink refreshSink)
    {
        m_provider = std::move(provider);
        m_refreshSink = std::move(refreshSink);
    }

    /// 编辑接受后的入队（保序——submitJointFieldEdit Applied 分支回传）。
    void recordPending(const PendingEdit& edit) { m_pending.push_back(edit); }

    /// 待重演队列只读投影（呈现"未应用编辑"清单——零复制语义由 const& 承担）。
    const std::vector<PendingEdit>& pending() const noexcept { return m_pending; }

    /// 手工处置：从下标 begin 起丢弃待重演编辑（用户决议出口——唯一删除路径）。
    void discardPendingFrom(std::size_t begin)
    {
        if (begin < m_pending.size()) { m_pending.erase(m_pending.begin() + begin, m_pending.end()); }
    }

    /// 手工处置：清空待重演队列（用户放弃全部未应用编辑）。
    void clearPending() { m_pending.clear(); }

    /// 手工处置标记（BlockedAtEdit 后置位——呈现"需手工处置"横幅的数据源）。
    bool manualInterventionRequired() const noexcept { return m_manualIntervention; }
    /// 被拒的编辑下标（BlockedAtEdit 时有效；其余态为 nullopt）。
    const std::optional<std::size_t>& blockedAtIndex() const noexcept { return m_blockedAt; }

    /**
     * @brief ⑤修订事件入口（L-4 主干——重载基线→保序重演→全面板刷新）。
     *
     * 编排序（§9.7.2 L-4 行逐段落位）：
     *   ① assertOnUiThread（§3.4——违约 fail-fast）；
     *   ② provider() 现取基线闭包（零缓存——每次事件现场重载）；
     *   ③ 保序重演：逐条 applyJointFieldEdit（域裁决）——接受即继续，
     *      拒绝即停：置 manualIntervention＋blockedAt（失败编辑与其后
     *      编辑**保留**在队列——不静默丢弃），失败事实经返回值上呈；
     *   ④ 重演完毕（全成或停在失败处）→ refreshSink()（全面板刷新——
     *      widget 现取工作集重投影；失败态同样刷新——横幅与队列可见）。
     *
     * @param newBaseline [in] 事件携带的基线修订身份（留痕/对账用——工作
     *                    集内容以 provider 现取为准，防迟到事件错配；
     *                    nullopt＝失效通知等无修订身份事件）
     * @return 重演结果（Replayed／BlockedAtEdit）
     *
     * @throws std::runtime_error 跨线程调用（§3.4 fail-fast）／sinks 未注入
     *               （装配缺陷）
     */
    ReplayOutcome onRevisionEvent(const std::optional<core::RevisionId>& newBaseline)
    {
        m_guard.assertOnUiThread();  // ①仅 UI 线程（编辑面约束——§3.4）
        (void)newBaseline;           // 身份随调用留痕（日志面归 widget/dev 通道——本层零存储）

        if (!m_provider || !m_refreshSink) {
            throw std::runtime_error("modeling 面板：刷新协调器 sinks 未注入（装配缺陷）");
        }

        // ②重载基线闭包（provider 现取——零缓存；重演落在权威工作集上，
        //   PA-1：闭包重建权威在装配层，本层不持有工作集）。
        ModelingWorkingSet& ws = m_provider();

        // ③保序重演（失败即停＋保留现场——L-4"重演失败提示手工处置，
        //   不静默丢弃编辑"）。
        for (std::size_t i = 0; i < m_pending.size(); ++i) {
            const PendingEdit& e = m_pending[i];
            if (applyJointFieldEdit(ws, e.jointIndex, e.field, e.value).has_value()) {
                // 域拒绝：停在此条——队列原样保留（含失败条与其后条），
                // 手工处置标记置位；用户决议走 discardPendingFrom/重编辑。
                m_manualIntervention = true;
                m_blockedAt = i;
                m_refreshSink();  // ④失败态同样全面板刷新（横幅可见）
                return ReplayOutcome::BlockedAtEdit;
            }
        }

        // 全部落位：清手工处置标记（上一轮失败经处置后恢复），全面板刷新。
        m_manualIntervention = false;
        m_blockedAt = std::nullopt;
        m_refreshSink();
        return ReplayOutcome::Replayed;
    }

private:
    PanelUiThreadGuard m_guard;            ///< UI 线程守卫（构造线程绑定）
    BaselineProvider m_provider;           ///< 基线提供器（非 owning——装配层注入）
    RefreshSink m_refreshSink;             ///< 全面板刷新出口（非 owning）
    std::vector<PendingEdit> m_pending;    ///< 待重演编辑（保序——编辑意图队列）
    bool m_manualIntervention = false;     ///< 手工处置标记（BlockedAtEdit 置位）
    std::optional<std::size_t> m_blockedAt; ///< 被拒编辑下标（BlockedAtEdit 时有值）
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_PANELREFRESH_HPP
