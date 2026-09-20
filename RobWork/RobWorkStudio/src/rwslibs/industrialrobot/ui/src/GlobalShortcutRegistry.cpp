/**
 * @file   GlobalShortcutRegistry.cpp
 * @brief  全局快捷键唯一注册点实现（IGlobalShortcutRegistry 的唯一实现）
 *         ——键→命令映射单表＋冲突拒绝＋UI-HOTKEY-CONFLICT＋QShortcut 承载。
 *
 * 设计依据：
 *   - units/ui.md §7.3（注册/改绑规则原文：registerDefault 同键占用→拒绝＋
 *     UI-HOTKEY-CONFLICT（context 携带冲突键与已占用命令 id；数值比较字段
 *     ＝"不适用"）；rebind 冲突同诊断；解绑总是允许；键→命令映射全局唯一；
 *     实际 Qt 快捷键对象只由本注册点创建（QShortcut，WindowShortcut 上下文
 *     ——业务插件不得自建全局作用域 QShortcut，静态扫描项）；绑定持久化
 *     用户级设置（PM-14），Default 集随产品版本冻结）、§10.4（契约表）、
 *     §3.5（UI-HOTKEY-CONFLICT 码语义）；
 *   - 需求 UX-13（快捷键×面板可达性兜底）、PM-14（用户级设置）、SA-16/
 *     ARCH §11.2-1（注册边界拒绝，不覆盖不静默）、NFR-MNT-07（UI-HKY-3
 *     静态扫描——本文件是全产品唯一允许"QShortcut＋全局作用域"的源文件，
 *     BuildRedLineTest::NoGlobalShortcutPrivatization_SA16_UI_BUILD 把该
 *     豁免面钉为本文件独占）；
 *   - P-UI-10 处置（契约 acceptance 3 维持）：冲突诊断码值以 diagnostics
 *     码表收编表现为准——出线走 IDiagnosticFactory::create 唯一入口。
 *
 * 线程模型：全部方法 UI 线程（§10.4 契约表）；attach/detach 涉及 Widget
 * 树，同样仅 UI 线程（§3.4 M-1）。非线程安全。
 */

#include <sdurws/ird/ui/IGlobalShortcutRegistry.hpp>

#include <QShortcut>
#include <QWidget>  // 完整类型（QShortcut 父控件入参的 QObject 转换需要——attach 宿主面）
#include <QtGlobal>

#include <algorithm>
#include <map>
#include <utility>

#include <sdurws/ird/core/DiagData.hpp>   // core::DiagnosticRecord/ComparativeFields（冲突诊断装配）
#include <sdurws/ird/core/Identity.hpp>   // core::ObjectId（用户级码 subject——同 CommandRegistry 装配口径）
#include <sdurws/ird/core/Provenance.hpp> // core::SourcedValue（比较字段「不适用」四态承载）

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 键的规范化映射形态（PortableText——跨布局稳定：Ctrl+S 恒 "Ctrl+S"，
/// 与平台 NATIVE 文本（macOS ⌘ 等）解耦；空键→空串，入参边界已拒绝空键）。
QString canonicalKeyText(const QKeySequence& key)
{
    return key.toString(QKeySequence::PortableText);
}

// =====================================================================
// GlobalShortcutRegistryImpl——唯一实现
// =====================================================================

/// RAII 订阅句柄（core::IEventSubscription 的本地面——退订幂等）。
class FeedSubscription final : public core::IEventSubscription {
public:
    FeedSubscription(std::vector<diagnostics::IDiagObserver*>* observers,
                     diagnostics::IDiagObserver* observer)
        : m_observers(observers), m_observer(observer) {}
    ~FeedSubscription() override { unsubscribe(); }
    FeedSubscription(const FeedSubscription&) = delete;
    FeedSubscription& operator=(const FeedSubscription&) = delete;

    void unsubscribe() override
    {
        if (m_observers != nullptr) {
            const auto it = std::find(m_observers->begin(), m_observers->end(), m_observer);
            if (it != m_observers->end()) {
                m_observers->erase(it);
            }
            m_observers = nullptr;
        }
    }

private:
    std::vector<diagnostics::IDiagObserver*>* m_observers;  ///< 宿主的观察者表（非拥有）
    diagnostics::IDiagObserver* m_observer;                 ///< 本句柄对应的观察者
};

class GlobalShortcutRegistryImpl final : public IGlobalShortcutRegistry {
public:
    explicit GlobalShortcutRegistryImpl(GlobalShortcutRegistryDeps deps)
        : m_deps(std::move(deps))
    {
        // commands 必须非空（§10.4 前置校验的载体面）——调用方错误 fail-fast。
        Q_ASSERT(m_deps.commands != nullptr
                 && "GlobalShortcutRegistryDeps.commands 为空（调用方契约违约）");
    }

    ~GlobalShortcutRegistryImpl() override
    {
        // Qt 对象树兜底：宿主窗口销毁会带走子 QShortcut；显式 detach 保证
        // 无宿主场景（attach 未调用）下也无悬挂回调（lambda 捕获 this）。
        detach();
    }

    // ---- 装配期 ----

    HotkeyResult registerDefault(const CommandId& id, const QKeySequence& key) override
    {
        return bindInternal(id, key, BindingOrigin::Default);
    }

    HotkeyResult rebind(const CommandId& id, std::optional<QKeySequence> key) override
    {
        // 解绑总是允许（§7.3 原文——该命令退回面板可达，UX-13 兜底）。
        // 但命令须已注册：未注册 id 不是"解绑"而是拼写违约（UnknownCommand
        // ——与 bindInternal 的前置校验同口径）。
        if (!key.has_value()) {
            if (!isRegistered(id)) {
                return HotkeyResult::unknownCommand();
            }
            return unbindInternal(id);
        }
        return bindInternal(id, *key, BindingOrigin::User);
    }

    // ---- 查询面 ----

    std::vector<HotkeyBinding> bindings() const override
    {
        // Default 在前按装配序、User 在后按改绑序（§10.4 bindings 契约排序）。
        std::vector<HotkeyBinding> out = m_defaults;
        out.insert(out.end(), m_userBindings.begin(), m_userBindings.end());
        return out;
    }

    std::optional<CommandId> lookup(const QKeySequence& key) const override
    {
        const auto it = m_byKey.find(canonicalKeyText(key));
        if (it == m_byKey.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::unique_ptr<core::IEventSubscription>
    subscribeConflictFeed(diagnostics::IDiagObserver& observer) override
    {
        m_feedObservers.push_back(&observer);
        return std::make_unique<FeedSubscription>(&m_feedObservers, &observer);
    }

    // ---- QShortcut 承载（唯一创建点——§7.3/NFR-MNT-07/UI-HKY-3 豁免面）----

    void attach(QWidget* shortcutParent) override
    {
        // 重建式刷新：先清理旧对象（rebind 后重复 attach 的同步路径）。
        detach();
        if (shortcutParent == nullptr) {
            return;  // 无宿主＝模型面先行（attach 前物理键不创建）
        }
        m_shortcutParent = shortcutParent;
        for (const HotkeyBinding& binding : bindings()) {
            createShortcutFor(binding);
        }
    }

    void detach() override
    {
        // Qt 父子纪律：deleteLater 由事件循环收尾（不在快捷键自身回调栈内
        // 立即 delete——防回调中拆除自身）。
        for (auto& [command, shortcut] : m_shortcutByCommand) {
            (void)command;
            if (shortcut != nullptr) {
                shortcut->deleteLater();
            }
        }
        m_shortcutByCommand.clear();
        m_shortcutParent = nullptr;
    }

    void restoreUserBindings(const std::vector<HotkeyBinding>& userBindings) override
    {
        // 逐条按 rebind 语义应用（§7.3 持久化装载）：失效/违约条目丢弃＋Dev
        // 日志——历史可能跨版本失效（命令改名/键表冻结变更），不阻塞启动；
        // 坏一条丢一条，不炸整表（§4.5"损坏段整段丢弃"的快捷键侧同纪律）。
        for (const HotkeyBinding& binding : userBindings) {
            if (binding.key.isEmpty()) {
                continue;  // 空键条目＝历史损坏（丢弃）
            }
            const HotkeyResult result = bindInternal(binding.command, binding.key,
                                                     BindingOrigin::User);
            if (!result.isOk()) {
                emitDev("快捷键历史条目丢弃: '" + binding.command + "'（"
                            + hotkeyResultName(result) + "）");
            }
        }
    }

private:
    // ---- 注册判据（§10.4 前置"命令已注册"）----

    /// 已注册判定：availability().registered 位（§10.3 UI-T06 增量登记——
    /// 存在性与可见性正交：被可见谓词塌缩的命令仍可持有/解绑快捷键）。
    bool isRegistered(const CommandId& id) const
    {
        return m_deps.commands != nullptr && m_deps.commands->availability(id).registered;
    }

    /// 可绑定判定：描述符 bindable 位（经只读投影——CommandView.bindable，
    /// UI-T06 增量登记）。
    bool isBindable(const CommandId& id) const
    {
        if (m_deps.commands == nullptr) {
            return false;
        }
        const std::vector<CommandView> views = m_deps.commands->query({});
        const auto it = std::find_if(views.begin(), views.end(),
                                     [&id](const CommandView& v) { return v.id == id; });
        return it != views.end() && it->bindable;
    }

    /**
     * @brief 绑定主路径（registerDefault/rebind/历史装载共用——冲突判定/
     *        诊断/持久化回调单点，避免多口径漂移）。
     */
    HotkeyResult bindInternal(const CommandId& id, const QKeySequence& key,
                              BindingOrigin origin)
    {
        if (key.isEmpty()) {
            return HotkeyResult::notBindable();  // 空键不是绑定（入参违约）
        }
        // 前置①：命令已注册（§10.4——未注册→UnknownCommand）。
        if (!isRegistered(id)) {
            return HotkeyResult::unknownCommand();
        }
        // 前置②：bindable（§7.3——不可绑定命令退回面板可达；禁用态与绑定
        // 无关：禁用命令的键可占用，按下时 submit 走 NOT-EXECUTABLE 拒绝）。
        if (!isBindable(id)) {
            return HotkeyResult::notBindable();
        }
        const QString keyText = canonicalKeyText(key);

        // 前置③同键占用→拒绝＋UI-HOTKEY-CONFLICT（不覆盖不静默——§7.3）。
        // 占用判定排除该命令自身（rebind 到同键＝显式覆盖为 User 来源，
        // 不是自冲突）。
        const auto occupant = m_byKey.find(keyText);
        if (occupant != m_byKey.end() && occupant->second != id) {
            emitConflictDiagnostic(keyText, id, occupant->second);
            notifyConflictFeed();
            return HotkeyResult::conflict(occupant->second);
        }

        // 一键一命令、一命令一键（双向唯一映射）：先解除该命令旧键的表项
        // （Default 行随 rebind 转为 User 行——"Default 集随产品版本冻结"
        // 指装配期默认表不变，用户覆盖以 User 行承载，PM-14）。
        removeFromTables(id);

        HotkeyBinding binding;
        binding.key = key;
        binding.command = id;
        binding.origin = origin;
        if (origin == BindingOrigin::Default) {
            m_defaults.push_back(binding);
        } else {
            m_userBindings.push_back(binding);
        }
        m_byKey.emplace(keyText, id);

        if (origin == BindingOrigin::User) {
            persistUserBindings();  // PM-14——改绑成功即持久化回调
        }

        // attach 态下同步物理键（逻辑表与 QShortcut 面一致——重建式）。
        if (m_shortcutParent != nullptr) {
            attach(m_shortcutParent);
        }
        return HotkeyResult::ok();
    }

    HotkeyResult unbindInternal(const CommandId& id)
    {
        const bool hadKey = keyOfCommand(id).isEmpty() == false;
        const bool hadUserBinding =
            std::any_of(m_userBindings.begin(), m_userBindings.end(),
                        [&id](const HotkeyBinding& b) { return b.command == id; });
        removeFromTables(id);
        // User 行移除即持久化（解绑改变 User 集——PM-14；Default 行移除
        // 不进持久化载荷——Default 集随版本冻结，不入用户设置）。
        if (hadUserBinding) {
            persistUserBindings();
        }
        if (hadKey && m_shortcutParent != nullptr) {
            attach(m_shortcutParent);  // 物理键同步拆除（重建式）
        }
        return HotkeyResult::ok();
    }

    QString keyOfCommand(const CommandId& id) const
    {
        for (const auto& [keyText, command] : m_byKey) {
            if (command == id) {
                return keyText;
            }
        }
        return {};
    }

    void removeFromTables(const CommandId& id)
    {
        // 键映射反向清理（一键一命令）。
        for (auto it = m_byKey.begin(); it != m_byKey.end();) {
            if (it->second == id) {
                it = m_byKey.erase(it);
            } else {
                ++it;
            }
        }
        // 来源行清理（Default/User 两表——绑定行与映射表一致）。
        const auto dropFrom = [&id](std::vector<HotkeyBinding>& table) {
            table.erase(std::remove_if(table.begin(), table.end(),
                                       [&id](const HotkeyBinding& b) { return b.command == id; }),
                        table.end());
        };
        dropFrom(m_defaults);
        dropFrom(m_userBindings);
    }

    void persistUserBindings()
    {
        if (m_deps.onUserBindingsChanged) {
            m_deps.onUserBindingsChanged(m_userBindings);  // 写盘快照值（PM-14）
        }
    }

    void notifyConflictFeed()
    {
        // 同步派发（UI 线程——冲突拒绝本就发生在 UI 线程，§3.4 无跨界）；
        // 拷贝观察者表再派发——回调内退订不破坏遍历。
        const std::vector<diagnostics::IDiagObserver*> observers = m_feedObservers;
        for (diagnostics::IDiagObserver* observer : observers) {
            observer->onCatalogChanged();
        }
    }

    // ---- QShortcut 物理（唯一创建点——本文件是 UI-HKY-3 扫描的唯一豁免面）----

    void createShortcutFor(const HotkeyBinding& binding)
    {
        if (m_shortcutParent == nullptr || binding.key.isEmpty()) {
            return;
        }
        // 唯一 QShortcut 创建点：WindowShortcut 上下文（§7.3 原文——上下文
        // 显式书写，静态扫描以"QShortcut＋WindowShortcut 同现"为违例形态，
        // 本行为全产品唯一豁免点）；触发经 ICommandRegistry::submit 统一
        // 路径，不经旁路（§10.4 副作用行——可用性拒绝由注册表出
        // UI-CMD-NOT-EXECUTABLE，与菜单/面板同口径）。
        auto* shortcut = new QShortcut(binding.key, m_shortcutParent);
        shortcut->setContext(Qt::WindowShortcut);
        const CommandId command = binding.command;
        QObject::connect(shortcut, &QShortcut::activated, m_shortcutParent, [this, command] {
            if (m_deps.commands != nullptr) {
                (void)m_deps.commands->submit(command);
            }
        });
        m_shortcutByCommand.emplace(command, shortcut);
    }

    // ---- 诊断出线（P-UI-10：码值权威归 diagnostics 码表；subject/空场景
    //      口径与 CommandRegistry 出线同案）----

    void emitConflictDiagnostic(const QString& keyText, const CommandId& rejected,
                                const CommandId& occupied)
    {
        // Dev 通道恒出线（冲突是装配/改绑违约的可观测面——无论是否有目录，
        // 开发侧都应可见；空 devLog＝显式声明的无日志场景）。rejected/
        // occupied 已是 std::string（CommandId 别名）——直接拼接。
        emitDev("UI-HOTKEY-CONFLICT: 快捷键 '" + keyText.toStdString() + "' 已被命令 '"
                    + occupied + "' 占用，对 '" + rejected
                    + "' 的绑定被拒绝（不覆盖，§7.3）");
        if (!m_deps.diagFactory || !m_deps.diagSink) {
            return;  // 无目录场景——HotkeyResult 值轨已承载拒绝
        }
        // 比较型字段标「不适用」（§3.5/ERR-01：键冲突是非数值判定，不伪造
        // 数值——两侧 quantity 均为 NotApplicable 四态，单位句柄为无效缺省）。
        core::ComparativeFields comparison;
        comparison.actual.quantity = core::SourcedValue<double>::notApplicable();
        comparison.expected.quantity = core::SourcedValue<double>::notApplicable();
        core::DiagnosticRecord record = core::DiagnosticRecord::make(
            "UI-HOTKEY-CONFLICT", core::ObjectId::generate(), std::nullopt, std::nullopt,
            // context 携带冲突键＋已占用命令（acceptance 1 的 context 要求）。
            "快捷键 '" + keyText.toStdString() + "' 已被命令 '" + occupied
                + "' 占用（对 '" + rejected + "' 的绑定被拒绝）",
            "全局快捷键映射全局唯一（§7.3），一键不得绑定两个命令",
            "为 '" + rejected + "' 另选未占用按键，或先解除 '" + occupied
                + "' 的绑定",
            comparison);
        diagnostics::DiagContext context;
        context.sourceUnit = "ui";  // §4.2 sourceUnit 词表含 ui
        context.sourceInterface = "shortcuts.register";
        m_deps.diagSink->append(m_deps.diagFactory->create(record, context));
    }

    void emitDev(const std::string& message)
    {
        if (m_deps.devLog) {
            m_deps.devLog->logDev("diag/ui", message);
        }
    }

    static const char* hotkeyResultName(const HotkeyResult& result)
    {
        switch (result.kind()) {
        case HotkeyResult::Kind::Ok: return "Ok";
        case HotkeyResult::Kind::Conflict: return "Conflict";
        case HotkeyResult::Kind::UnknownCommand: return "UnknownCommand";
        case HotkeyResult::Kind::NotBindable: return "NotBindable";
        }
        return "?";
    }

    GlobalShortcutRegistryDeps m_deps;         ///< 装配依赖（所有权在外）
    std::vector<HotkeyBinding> m_defaults;     ///< Default 绑定（装配序）
    std::vector<HotkeyBinding> m_userBindings; ///< User 绑定（改绑序——持久化载荷）
    std::map<QString, CommandId> m_byKey;      ///< 规范化键→命令（全局唯一映射）
    std::vector<diagnostics::IDiagObserver*> m_feedObservers; ///< 冲突流观察者表
    QWidget* m_shortcutParent = nullptr;       ///< attach 宿主（非拥有——Qt 树管理）
    std::map<CommandId, QShortcut*> m_shortcutByCommand; ///< 命令→物理键（本表创建）
};

}  // namespace

std::unique_ptr<IGlobalShortcutRegistry> createGlobalShortcutRegistry(GlobalShortcutRegistryDeps deps)
{
    return std::make_unique<GlobalShortcutRegistryImpl>(std::move(deps));
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
