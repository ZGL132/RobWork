/**
 * @file   PanelRefresh.hpp
 * @brief  requirements 面板刷新协调（零 Qt）——事件驱动刷新＋未应用编辑
 *         保序重演＋UI 线程守卫（卡 §9.8 线程与刷新约束行）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8 线程与刷新约束行（"同 modeling §9.7.4：
 *     编辑器仅 UI 线程；命令异步；确认/拾取经 ui 桥 marshal；事件驱动刷
 *     新禁轮询；面板不缓存权威数据——防第二真值"）、§4.6（编辑态/草稿态/
 *     已应用修订三态——修订产生后编辑态以"基线＋未应用编辑"重演保持）
 *   - units/modeling.md §9.7.4（事件驱动刷新的对端锚——本卡同源约束）
 *   - 任务契约 tasks/foundation/WP-14-T08.json acceptance 1（"面板不互读
 *     控件、不缓存权威数据"）/2（L-R2 编辑→草稿链路的刷新半区）
 *
 * 背景说明：修订事件的**投递面**归 ui（事件总线 UI 线程投递——ui.md
 * §6.1；未落位面以卡文本为基线，P-REQ-8 同款接缝）。本协调器承接投递后
 * 的 requirements 侧编排：收到"修订已产生/失效"通知→以新基线闭包重建
 * 编辑器工作集（loadBaseline）→把用户未应用编辑按发生序逐条重演
 * （applyEdit 域裁决）→失败即停并标记手工处置（失败编辑与后续编辑保留
 * 在待重演队列——不静默丢弃，用户可见可处置）→全面板刷新。无轮询定时
 * 器、无后台线程——刷新只由显式通知触发（§9.8 结构性实现）。
 *
 * 线程约束：仅 UI 线程访问（§3.4）；本头自带 UI 线程守卫（std::thread::id
 * 对比——调试期违约即抛，见 PanelUiThreadGuard）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELREFRESH_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELREFRESH_HPP

#include <functional>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include <sdurws/ird/requirements/Editor.hpp>  // IRequirementEditor/RequirementEdit（重演的域入口）

namespace sdurws::ird::requirements {

// =====================================================================
// UI 线程守卫（§3.4"编辑器仅 UI 线程访问"的运行期执行面——modeling 同款）
// =====================================================================

/**
 * @brief UI 线程守卫（零 Qt——std::thread::id 对比；widget 构造时绑定
 *        所属线程，跨线程进入编辑/刷新面即 fail-fast）。
 *
 * 为什么需要运行期守卫：§3.4 的"仅 UI 线程"是会话对象约束（违例是数据
 * 竞争，属调用方契约违约）；AGENTS 错误语义：调用方错误 fail-fast（抛
 * 异常），不静默容错。release 行为与 debug 一致（违例不可恢复——宁可
 * 崩溃不静默竞争）。线程安全：本类自身只读（构造后 owner 不变）。
 */
class PanelUiThreadGuard {
public:
    /// 绑定 owner 线程（widget 构造线程＝UI 线程——装配期一次）。
    PanelUiThreadGuard() : m_owner(std::this_thread::get_id()) {}

    PanelUiThreadGuard(const PanelUiThreadGuard&) = delete;
    PanelUiThreadGuard& operator=(const PanelUiThreadGuard&) = delete;

    /// 在 owner 线程上（恒 true——检查面）。
    bool onUiThread() const noexcept { return m_owner == std::this_thread::get_id(); }

    /// 断言当前线程＝owner 线程；违约抛 std::runtime_error（fail-fast）。
    void assertOnUiThread() const
    {
        if (!onUiThread()) {
            throw std::runtime_error(
                "requirements 面板：编辑器/工作集仅 UI 线程访问（§3.4）——检测到跨线程调用");
        }
    }

private:
    std::thread::id m_owner;  ///< owner 线程（构造线程——UI 线程锚）
};

// =====================================================================
// 刷新协调器（事件驱动——修订产生/失效后的 requirements 侧编排）
// =====================================================================

/**
 * @brief 重演结果（两态——分流供调用方/测试判别）。
 */
enum class ReplayOutcome {
    Replayed,     ///< 全部待重演编辑按序落位（工作集＝新基线＋全部编辑）
    BlockedAtEdit ///< 第 failedIndex 条重演被域拒绝（其后编辑保留——手工处置）
};

/**
 * @brief 刷新协调器（面板持有——UI 线程对象；非线程安全，§3.4）。
 *
 * 使用时序（widget 接线约定）：
 *   1. 装配期 setSinks（基线闭包提供器＋全面板刷新出口）；
 *   2. 用户每次编辑接受后 recordPending（入队——保序；编辑意图值）；
 *   3. 收到修订产生/失效通知 → onRevisionEvent：loadBaseline 重建（编辑
 *      器域内重置）→保序重演→全面板刷新；
 *   4. 手工处置：用户对 blocked 状态决议（放弃该编辑起的所有未应用编辑
 *      ／基于新基线重编辑）→ discardPendingFrom/clearPending——入口只在
 *      此，无自动丢弃路径。
 *
 * ★ 零缓存纪律（§9.8）：本类不保存工作集/报告的任何副本——基线一律经
 *   编辑器 loadBaseline 现建（闭包重建归装配层提供的 closure view，PA-1）；
 *   待重演队列是**编辑意图**（用户输入的 RequirementEdit 值），不是模型
 *   权威数据——其为面板私有会话态，不违"第二真值"红线（权威仍在编辑器
 *   工作集）。
 */
class RequirementsRefreshCoordinator {
public:
    /// 基线闭包提供器（loadBaseline 入参源——L5 装配层注入；非 owning，
    /// 调用期存活；闭包域纪律由实现方保证——Editor.hpp RequirementObject
    /// ClosureView 契约）。
    using ClosureProvider = std::function<const RequirementObjectClosureView&()>;
    /// 全面板刷新出口（重演完成后调用——widget 现取重投影）。
    using RefreshSink = std::function<void()>;

    RequirementsRefreshCoordinator() = default;
    RequirementsRefreshCoordinator(const RequirementsRefreshCoordinator&) = delete;
    RequirementsRefreshCoordinator& operator=(const RequirementsRefreshCoordinator&) = delete;

    /// 注入闭包提供器与刷新出口（装配期一次；均必须非空——装配缺陷）。
    void setSinks(ClosureProvider provider, RefreshSink refreshSink)
    {
        m_provider = std::move(provider);
        m_refreshSink = std::move(refreshSink);
    }

    /// 编辑接受后的入队（保序——submitEntryEdit/submitBatchEdit Applied
    /// 分支回传；入参为引发该次接受的编辑值——重演输入）。
    void recordPending(const RequirementEdit& edit) { m_pending.push_back(edit); }

    /// 待重演队列只读投影（呈现"未应用编辑"清单——const& 零复制语义）。
    const std::vector<RequirementEdit>& pending() const noexcept { return m_pending; }

    /// 手工处置：从下标 begin 起丢弃待重演编辑（用户决议出口——唯一删除路径）。
    void discardPendingFrom(std::size_t begin)
    {
        if (begin < m_pending.size()) {
            m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(begin),
                            m_pending.end());
        }
    }

    /// 手工处置：清空待重演队列（用户放弃全部未应用编辑）。
    void clearPending() { m_pending.clear(); }

    /// 手工处置标记（BlockedAtEdit 后置位——呈现"需手工处置"横幅的数据源）。
    bool manualInterventionRequired() const noexcept { return m_manualIntervention; }
    /// 被拒的编辑下标（BlockedAtEdit 时有效；其余态为 nullopt）。
    const std::optional<std::size_t>& blockedAtIndex() const noexcept { return m_blockedAt; }

    /**
     * @brief 修订事件入口（重演主干——重建基线→保序重演→全面板刷新）。
     *
     * 编排序（§4.6 三态语义逐段落位）：
     *   ① assertOnUiThread（§3.4——违约 fail-fast）；
     *   ② provider() 现取闭包 → editor.loadBaseline（域内重置工作集/栈/
     *      计数为基线态——零缓存：每次事件现场重建）；
     *   ③ 保序重演：逐条 editor.applyEdit（域裁决）——接受即继续，拒绝即
     *      停：置 manualIntervention＋blockedAt（失败编辑与其后编辑**保留**
     *      在队列——不静默丢弃），失败事实经返回值上呈；
     *   ④ 重演完毕（全成或停在失败处）→ refreshSink()（全面板刷新——
     *      widget 现取工作集重投影；失败态同样刷新——横幅与队列可见）。
     *
     * @return 重演结果（Replayed／BlockedAtEdit）
     *
     * @throws std::runtime_error 跨线程调用（§3.4 fail-fast）／sinks 未注入
     *               （装配缺陷）／loadBaseline 失败（基线闭包违约——域错误
     *               上抛语义：面板不吞基线级失败，交由装配层处置）
     */
    ReplayOutcome onRevisionEvent(IRequirementEditor& editor)
    {
        m_guard.assertOnUiThread();  // ①仅 UI 线程（编辑面约束——§3.4）

        if (!m_provider || !m_refreshSink) {
            throw std::runtime_error("requirements 面板：刷新协调器 sinks 未注入（装配缺陷）");
        }

        // ②重建基线（loadBaseline 失败＝基线闭包违约——RequirementLoadOutcome
        //   err 不吞：面板不虚构基线，fail-fast 交装配层处置）。
        const RequirementLoadOutcome load = editor.loadBaseline(m_provider());
        if (!load.ok) {
            throw std::runtime_error("requirements 面板：基线重建失败（"
                                     + load.error.detail + "）——不吞基线级失败");
        }

        // ③保序重演（失败即停＋保留现场——"重演失败提示手工处置，不静默
        //   丢弃编辑"）。
        for (std::size_t i = 0; i < m_pending.size(); ++i) {
            if (!editor.applyEdit(m_pending[i]).accepted) {
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
    PanelUiThreadGuard m_guard;          ///< UI 线程守卫（构造线程绑定）
    ClosureProvider m_provider;          ///< 基线闭包提供器（非 owning——装配层注入）
    RefreshSink m_refreshSink;           ///< 全面板刷新出口（非 owning）
    std::vector<RequirementEdit> m_pending;  ///< 待重演编辑（保序——编辑意图队列）
    bool m_manualIntervention = false;   ///< 手工处置标记（BlockedAtEdit 置位）
    std::optional<std::size_t> m_blockedAt;  ///< 被拒编辑下标（BlockedAtEdit 时有值）
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELREFRESH_HPP
