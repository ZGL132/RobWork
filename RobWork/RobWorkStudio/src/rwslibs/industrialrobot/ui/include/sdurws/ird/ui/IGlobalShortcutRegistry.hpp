/**
 * @file   IGlobalShortcutRegistry.hpp
 * @brief  全局快捷键唯一注册点（GlobalShortcutRegistry）——键→命令映射的
 *         唯一权威表；实际 QShortcut 对象仅由本组件创建（SA-16/N-11）。
 *
 * 设计依据：
 *   - units/ui.md §7.3（GlobalShortcutRegistry：默认绑定/用户改绑/解绑/
 *     冲突拒绝＋UI-HOTKEY-CONFLICT/唯一 QShortcut 创建点/绑定持久化 PM-14）、
 *     §10.4（接口契约表：HotkeyResult 词表/冲突诊断携带比较型字段"不适用"/
 *     副作用＝键按下触发 ICommandRegistry::submit）、§7.1（默认绑定表）、
 *     §3.5（UI-HOTKEY-CONFLICT 码语义）；
 *   - 需求 UX-13（快捷键与命令面板互为可达性兜底）、PM-14（用户级设置
 *     持久化——rebind/解绑不入项目）、NFR-MNT-07（插件私占全局快捷键＝
 *     静态扫描违例，UI-HKY-3）、SA-16/ARCH §11.2-1（注册边界拒绝冲突，
 *     不覆盖不静默）。
 *
 * 背景说明（为什么需要"唯一注册点"红线）：Qt 侧快捷键的物理载体是
 * QShortcut 对象——若各插件自建全局作用域 QShortcut，键→命令映射就有
 * N 张表，冲突检测/改绑持久化/面板呈现全部失真（N-11"无第二套"）。
 * 本组件是全局键表的唯一持有者：所有 WindowShortcut 上下文的 QShortcut
 * 都由本组件的实现创建（src/GlobalShortcutRegistry.cpp——静态扫描把该
 * 文件登记为唯一豁免点，UI-HKY-3 扫描见 BuildRedLineTest）；插件局部
 * 快捷键（WidgetWithChildrenShortcut 上下文）不在此列——局部键仅在焦点
 * 位于该面板时生效且优先于全局（Qt 焦点语义，§7.3），不进全局表。
 *
 * 线程模型（§10.4 契约表"线程"行）：全部方法 UI 线程调用（§3.4 M-1）。
 * 非线程安全：仅 UI 线程访问。
 */

#ifndef SDURWS_IRD_UI_IGLOBALSHORTCUTREGISTRY_HPP
#define SDURWS_IRD_UI_IGLOBALSHORTCUTREGISTRY_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QKeySequence>
#include <QString>

#include <sdurws/ird/core/Events.hpp>            // core::IEventSubscription（订阅句柄契约类型——表内登记边）
#include <sdurws/ird/diagnostics/Catalog.hpp>    // diagnostics::IDiagnosticSink/IDiagObserver（表内登记边）
#include <sdurws/ird/diagnostics/Factory.hpp>    // diagnostics::IDiagnosticFactory/IDevLogSink（同 ICommandRegistryDeps 纪律）
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandId（§10.4 副作用＝submit 的目标面）

class QWidget;  // 前置声明（attach 的宿主控件——头文件不拖入 Widgets）

namespace sdurws {
namespace ird {
namespace ui {

class ICommandRegistry;          // 前置声明（Deps 的非拥有指针——完整定义在 ICommandRegistry.hpp，已 include 其别名面）
class IGlobalShortcutRegistry;   // 前置声明（工厂返回类型——完整定义见下）

// =====================================================================
// 绑定值类型（§7.3 HotkeyBinding）
// =====================================================================

/**
 * @brief 绑定来源（§7.3 BindingOrigin：默认装配 vs 用户改绑）。
 *
 * Default 集随产品版本冻结（§7.3"默认表变更视同接口变更"）；User 集
 * 为用户级设置（PM-14），随 rebind/解绑更新并持久化。
 */
enum class BindingOrigin : std::uint8_t {
    Default, ///< 装配期默认绑定（§7.1 默认绑定表）
    User,    ///< 用户改绑（rebind——设置界面/命令面板内）
};

/**
 * @brief 一条快捷键绑定（§7.3 原文——规范化序列文本＋命令＋来源）。
 *
 * key 以 QKeySequence 承载（规范化语义见 registry 的 lookup 注释——
 * PortableText 规范形为映射键）；command 必为已注册命令（注册边界保证）。
 */
struct HotkeyBinding {
    /// 绑定键（规范化序列——构造自注册/改绑入参）。
    QKeySequence key;
    /// 绑定的命令 id（已注册——§10.4 前置条件）。
    CommandId command;
    /// 来源（Default/User——持久化只落 User 集，PM-14）。
    BindingOrigin origin = BindingOrigin::Default;
};

// =====================================================================
// 注入依赖与工厂
// =====================================================================

/**
 * @brief 全局快捷键注册表的装配依赖（create 注入——所有权在装配层）。
 *
 * 诊断语义与 CommandRegistryDeps 同款纪律（见其注释）：UI-HOTKEY-CONFLICT
 * （Warning，用户可见）经 factory.create＋sink.append 入目录——冲突键＋
 * 已占用命令在记录 context、数值比较字段标「不适用」（ERR-01——非数值
 * 判定不伪造数值，SA-16/ARCH §11.2-1）；factory/sink 为空＝无目录测试
 * 场景（拒绝行为不受影响——HotkeyResult 值轨恒在）。
 *
 * 持久化语义（PM-14）：User 绑定集变化（rebind 成功/解绑）后回调
 * onUserBindingsChanged——注册表不触碰存储（PA-1 权威唯一：用户级设置
 * 写路径归装配层的后台落盘线程），回调载荷＝当前 User 绑定全集（写盘
 * 快照值）。
 */
struct GlobalShortcutRegistryDeps {
    /// 命令注册表引用（非拥有——前置校验与键触发 submit 的目标面；须先于
    /// 本注册表装配：registerDefault/rebind 前置"命令已注册且 bindable"，
    /// §10.4 契约表）。
    ICommandRegistry* commands = nullptr;
    /// 诊断工厂（UI-HOTKEY-CONFLICT 的 create 唯一入口；可空＝无目录场景）。
    std::shared_ptr<diagnostics::IDiagnosticFactory> diagFactory;
    /// 诊断目录 sink（create 产物的记录面；可空＝同上）。
    std::shared_ptr<diagnostics::IDiagnosticSink> diagSink;
    /// 开发日志通道（持久化历史中冲突条目丢弃的 Dev 出线；可空＝无日志）。
    std::shared_ptr<diagnostics::IDevLogSink> devLog;
    /// User 绑定集变化回调（rebind/解绑成功后调用——PM-14 持久化的装配侧
    /// 落盘钩子；可空＝不持久化，须显式声明）。
    std::function<void(const std::vector<HotkeyBinding>&)> onUserBindingsChanged;
};

/**
 * @brief 快捷键操作结果（§10.4 HotkeyResult＝Ok | Conflict{existingCommand}
 *        | UnknownCommand | NotBindable）。
 *
 * 值形态：kind＋existingCommand（仅 Conflict 时非空——已占用命令 id，
 * 冲突诊断 context 的同源事实）。拒绝不覆盖不静默（SA-16）。
 */
class HotkeyResult {
public:
    /// 结果类别（§10.4 词表）。
    enum class Kind : std::uint8_t {
        Ok,             ///< 成功（键→命令映射已建立/更新/解除）
        Conflict,       ///< 键冲突被拒（existingCommand＝已占用命令）
        UnknownCommand, ///< 命令未注册（§10.4 前置违约——非绑定问题）
        NotBindable,    ///< 命令不可绑定（bindable=false）或键序列为空
    };

    HotkeyResult() = default;  ///< 默认＝Conflict（"未显式构造即拒绝"的安全缺省——不静默放行）
    static HotkeyResult ok() { HotkeyResult r; r.m_kind = Kind::Ok; return r; }
    static HotkeyResult unknownCommand() { HotkeyResult r; r.m_kind = Kind::UnknownCommand; return r; }
    static HotkeyResult notBindable() { HotkeyResult r; r.m_kind = Kind::NotBindable; return r; }
    static HotkeyResult conflict(CommandId existing)
    {
        HotkeyResult r;
        r.m_kind = Kind::Conflict;
        r.m_existing = std::move(existing);
        return r;
    }

    /// 结果类别。
    Kind kind() const noexcept { return m_kind; }
    /// 已占用命令 id（仅 Conflict 非空——冲突拒绝的 context 事实）。
    const std::optional<CommandId>& existingCommand() const noexcept { return m_existing; }
    /// 便捷判定（Ok）。
    bool isOk() const noexcept { return m_kind == Kind::Ok; }

private:
    Kind m_kind = Kind::Conflict;  ///< 默认拒绝（安全缺省——结果必须显式构造）
    std::optional<CommandId> m_existing;
};

/**
 * @brief 创建全局快捷键注册表（装配层独占持有——unique_ptr 所有权即刻移交）。
 *
 * @param deps [in] 装配依赖（commands 必须非空——调用方错误 fail-fast：
 *             空指针在 create 即拒绝，Debug 构型断言）
 * @return 空注册表（装配期；attach 前 QShortcut 尚未创建——模型面先行）
 */
std::unique_ptr<IGlobalShortcutRegistry> createGlobalShortcutRegistry(GlobalShortcutRegistryDeps deps);

// =====================================================================
// IGlobalShortcutRegistry——接口（§10.4）
// =====================================================================

/**
 * @brief 全局快捷键唯一注册点接口（§10.4 契约表原文逐条承载）。
 *
 * 非法使用（§10.4"非法"行）：插件在自身面板目标内创建 WindowShortcut/
 * ApplicationShortcut 作用域快捷键（静态扫描违例——UI-HKY-3，BuildRed
 * LineTest 常驻自证）；重复键注册（拒绝，不覆盖不静默）。
 */
class IGlobalShortcutRegistry {
public:
    virtual ~IGlobalShortcutRegistry() = default;

    /**
     * @brief 装配期默认绑定注册（§10.4 registerDefault）。
     *
     * 前置（§10.4 契约表）：命令已注册且 bindable。同键已被占用→拒绝＋
     * UI-HOTKEY-CONFLICT（context 携带冲突键与已占用命令 id；数值比较
     * 字段＝不适用，ERR-01），不覆盖既有绑定。
     *
     * @param id  [in] 命令 id（须已注册——未注册→UnknownCommand）
     * @param key [in] 默认键（空序列→NotBindable——空键不是绑定）
     * @return Ok｜Conflict{existingCommand}｜UnknownCommand｜NotBindable
     */
    virtual HotkeyResult registerDefault(const CommandId& id, const QKeySequence& key) = 0;

    /**
     * @brief 用户改绑/解绑（§10.4 rebind——设置界面/命令面板内）。
     *
     * 同键冲突→拒绝＋同一诊断（UI-HOTKEY-CONFLICT）；解绑（key=nullopt）
     * 总是允许（§7.3 原文——该命令退回面板可达，UX-13 兜底）。改绑成功
     * 后：绑定表以 User 来源更新、attach 态下实际 QShortcut 同步重建、
     * onUserBindingsChanged 回调触发持久化（PM-14）。
     *
     * @param id  [in] 命令 id（须已注册——未注册→UnknownCommand）
     * @param key [in] 新键（nullopt＝解绑）
     * @return Ok｜Conflict{existingCommand}｜UnknownCommand｜NotBindable
     *         （命令 bindable=false→NotBindable）
     */
    virtual HotkeyResult rebind(const CommandId& id, std::optional<QKeySequence> key) = 0;

    /**
     * @brief 绑定表只读快照（§10.4 bindings——含 Default/User 来源；Default
     *        在前按装配序、User 在后按改绑序）。
     */
    virtual std::vector<HotkeyBinding> bindings() const = 0;

    /**
     * @brief 键→命令查询（§10.4 lookup——全局唯一映射的读半区）。
     *
     * @param key [in] 查询键（按规范化形匹配——QKeySequence 相等语义＋
     *            PortableText 规范文本，大小写/修饰键书写差异不敏感）
     * @return 占用该键的命令 id；无绑定→nullopt
     */
    virtual std::optional<CommandId> lookup(const QKeySequence& key) const = 0;

    /**
     * @brief 订阅冲突诊断流（§10.4 subscribeConflictFeed）。
     *
     * 冲突拒绝（registerDefault/rebind 返回 Conflict）发生后回调观察者
     * （呈现层据此刷新快捷键设置面）。观察者接口复用 diagnostics::
     * IDiagObserver（onCatalogChanged 无载荷通知——观察者经 bindings()/
     * 诊断目录快照拉取明细，与目录订阅的"无载荷推送"口径一致）。回调在
     * UI 线程同步派发（冲突拒绝本就发生在 UI 线程——§3.4 M-1 无跨界）。
     *
     * @param observer [in] 观察者（非拥有——生命周期须短于本注册表）
     * @return RAII 订阅句柄（析构/显式 unsubscribe 即退订；幂等。句柄自身
     *         生命周期亦须短于本注册表——销毁顺序违约＝未定义行为，与
     *         diagnostics::ISubscription"目录销毁后句柄无意义"同口径）
     */
    virtual std::unique_ptr<core::IEventSubscription>
    subscribeConflictFeed(diagnostics::IDiagObserver& observer) = 0;

    // ---- QShortcut 承载面（§7.3"实际 Qt 快捷键对象只由本注册点创建"）----

    /**
     * @brief 把当前绑定表落到宿主窗口（创建 WindowShortcut 上下文 QShortcut，
     *        唯一创建点——全产品仅本实现文件允许该形态，UI-HKY-3 静态扫描
     *        豁免面）。
     *
     * 每个 Ok 绑定一个 QShortcut（父＝shortcutParent，上下文显式
     * Qt::WindowShortcut——§7.3 原文），触发即 ICommandRegistry::submit
     * （§10.4 副作用行：经命令统一路径，不经旁路；可用性拒绝由注册表
     * 出 UI-CMD-NOT-EXECUTABLE——与菜单/面板同口径）。重复 attach＝先
     * 清理旧对象再重建（rebind 后同步调用来刷新物理键）。shutdown 前由
     * 装配层 detach（或随宿主窗口销毁自动失效——QShortcut 父子析构纪律）。
     *
     * @param shortcutParent [in] 宿主控件（通常＝主窗口；非拥有——Qt 父子
     *                       所有权，创建的 QShortcut 由 Qt 树管理）
     */
    virtual void attach(QWidget* shortcutParent) = 0;

    /**
     * @brief 销毁本注册表创建的全部 QShortcut（拆卸路径；幂等）。
     */
    virtual void detach() = 0;

    /**
     * @brief 装载持久化的用户改绑（启动路径——PM-14）。
     *
     * 装配层从用户级设置读出后一次性注入。逐条按 rebind 语义应用：
     * 命令已不存在/不可绑定/与后续默认键冲突的条目丢弃（Dev 日志——历史
     * 可能跨版本失效，不阻塞启动；§4.5 损坏段整段丢弃的快捷键侧同纪律）。
     * 须在 registerDefault 之后、attach 之前调用（默认集先冻结，用户覆盖
     * 才有明确的冲突判定基准）。
     *
     * @param userBindings [in] 持久化的 User 绑定（来源标记由本表统一置
     *                     User——载荷只承载 command+key 事实）
     */
    virtual void restoreUserBindings(const std::vector<HotkeyBinding>& userBindings) = 0;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_IGLOBALSHORTCUTREGISTRY_HPP
