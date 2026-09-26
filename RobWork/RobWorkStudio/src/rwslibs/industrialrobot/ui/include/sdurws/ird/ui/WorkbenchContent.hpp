/**
 * @file   WorkbenchContent.hpp
 * @brief  工作台内容装配层（WorkbenchContent）——五区内容/命令设施/布局记忆
 *         /上下文投影的同一装配面，供「顶层窗口宿主层」（IWorkbenchShell，
 *         harness 形态）与「嵌入式 Dock 宿主层」（sdurws_ird_ui_plugin，宿主
 *         插件形态）共同消费。
 *
 * 设计依据：
 *   - units/ui.md §10.1（IWorkbenchShell 装配门面与 ShellWiring 注入包——
 *     本头是其 v1.10 增量登记的代码落点：壳拆「内容装配层＋顶层窗口宿主层」
 *     两层，harness 与插件共用同一内容装配面——O-38 裁决①②）、§4.1~§4.6
 *     （五区布局/区域交互边界/最小可用布局/布局状态归属/用户级设置项）、
 *     §7.1~§7.6（命令设施语义不变）、§11.5（分工表：打开协议的触发时机
 *     编排归装配层，状态机推进归 UiSessionController）；
 *   - units/ui.md §10.1 v1.14 增量注⑦（UI-T18 契约面变更——statusBarWidget()
 *     QStatusBar* 出口移除，改 setStatusTextObserver/setStatusMessageObserver
 *     双状态观测钩子把 PM-11 永久文本与瞬态消息投影给宿主层状态栏；内容层
 *     零状态栏 Widget——本头即该契约面变更的代码落点）；
 *   - O-38 裁决（DTB §4.2，2026-09-23，所有者融合方案评估）：开发期宿主
 *     插件验证通道——sdurws_ird_ui_plugin 经框架 Plugins→Load plugin 动态
 *     加载做界面级操作验证；壳拆两层后五区交互/布局记忆/命令门控语义不变，
 *     harness 行为回归零变化为硬验收项；禁止把顶层 QMainWindow 嵌套进宿主
 *     Dock（本头即"同一内容装配面"的契约承载）；
 *   - 需求 UX-09/PM-10/PM-11/PM-14（经 IWorkbenchShell 门面承接，本头不另
 *     立语义——投影与词表复用 IWorkbenchShell.hpp/UiProjections.hpp 既有
 *     冻结形态，NFR-MNT-03 单一权威）。
 *
 * 背景说明（为什么要拆两层）：
 *   壳原先是一个不可拆的整体（QMainWindow＋菜单栏＋五区 Dock＋状态栏），
 *   只能以独立顶层窗口形态运行；宿主插件需要把同一套五区内容装进框架
 *   RobWorkStudio 的主窗口 Dock，而**顶层 QMainWindow 不得嵌套进宿主 Dock**
 *   （双菜单栏/双状态栏/双 Dock 管理反模式——O-38 裁决明文禁止）。因此把
 *   "内容是什么"（本头：内容装配层）与"内容装进哪种窗口骨架"（宿主层：
 *   顶层窗口壳＝WorkbenchShell 实现，或插件 Dock 容器）分离。宿主层只做
 *   安放（chrome），一切界面语义都在内容装配层——两种宿主形态下行为同源。
 *
 * 线程模型（ui.md §3.4）：build/activate/shutdown 允许在装配线程调用（与
 *   IWorkbenchShell::initialize/shutdown 同口径）；其余方法只允许 UI 线程
 *   调用（违规＝未定义行为＋DT 断言）。非线程安全：仅 UI 线程访问。
 */

#ifndef SDURWS_IRD_UI_WORKBENCHCONTENT_HPP
#define SDURWS_IRD_UI_WORKBENCHCONTENT_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/ui/ICommandRegistry.hpp>   // ui::ICommandRegistry::CommandHandler（会话入口覆写载体）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>    // ui::ShellWiring/WorkbenchRegion/ShellCommandAvailability/RecentProjectEntry（§10.1 冻结词表——本头零新词表）

class QSize;      // 前置声明：notifyHostResized 入参（消费者按需自含）
class QString;    // 前置声明：标题文本观察回调入参
class QWidget;    // 前置声明：宿主 Widget 与内容出口（头文件不拖入 Widgets）

namespace sdurws {
namespace ird {
namespace ui {

class IWorkbenchContent;  // 前置声明（工厂返回类型）

// =====================================================================
// 宿主形态词表（同一内容装配面在两种宿主层的承载差异——O-38 裁决②）
// =====================================================================

/**
 * @brief 内容装配面的宿主形态（决定布局记忆面与尺寸折叠语义的差异）。
 *
 * 差异登记（§10.1 v1.10 增量的实现口径）：
 *   - TopLevelWindow（harness 形态）：宿主层拥有顶层 QMainWindow，布局记忆
 *     为全量五键（窗口几何＋停靠位形＋三区可见性——§4.5 原文），尺寸折叠
 *     （§4.4 1280×720 阈值）随宿主 resize 生效——与 UI-T03 以来的行为逐字节
 *     一致（harness 回归零变化的承载面）；
 *   - EmbeddedDock（宿主插件形态）：宿主层是框架主窗口里的一个 Dock 面板，
 *     内容装配层**不拥有任何顶层窗口**——窗口几何/停靠位形属框架主窗口
 *     （RobWorkStudio 自持久化其 QtMainWindowState），布局记忆只承载三区
 *     用户可见性键（与顶层形态同组同键，跨形态共享用户级设置——PM-14）；
 *     §4.4 尺寸折叠按宿主形态关闭（该规则量测的是顶层窗口尺寸，Dock 面板
 *     与宿主中央视图并置、天然窄幅——自动折叠会在宿主模式恒触发，属形态
 *     误配；用户手动开关三区不受影响）。
 */
enum class WorkbenchHostKind : std::uint8_t {
    TopLevelWindow,  ///< 顶层窗口宿主（harness sdurws_ird_ui_app——IWorkbenchShell 承载）
    EmbeddedDock,    ///< 嵌入式 Dock 宿主（sdurws_ird_ui_plugin——框架主窗口内 Dock 面板）
};

// =====================================================================
// WorkbenchContentDeps——内容装配面的一次性注入包
// =====================================================================

/**
 * @brief 内容装配层的装配依赖（createWorkbenchContent 时一次性给出）。
 *
 * 成员语义：
 *   - wiring＝§10.1 ShellWiring 注入包原样转发（可空成员"须显式声明"纪律
 *     不变——build() 内做与 IWorkbenchShell::initialize 相同的必填校验）；
 *   - hostWidget＝QShortcut attach 宿主、命令面板父窗口、对话框父窗口的
 *     统一宿主控件（TopLevelWindow 形态＝顶层主窗口；EmbeddedDock 形态＝
 *     插件 Dock 的内容体控件）。必填（空＝调用方错误，build() 拒绝）；
 *   - 窗口几何/位形钩子＝顶层窗口宿主层提供的承载面（布局记忆的全量半区
 *     ——几何与停靠位形是窗口事实，归属宿主层；内容装配层只编排读写时序
 *     与损坏处置）。四个钩子**必须成组提供或成组缺省**：缺省＝嵌入式宿主
 *     形态的"仅区域可见性"记忆面（见 WorkbenchHostKind 注释）；
 *   - newProjectHandler/openProjectHandler＝会话入口处理器覆写（§11.5"触发
 *     时机编排归装配层"的注入面）：§7.1 阶段 A 里 project.new/project.open
 *     由壳登记占位说明处理器（契约显式形态，harness 形态不变——不注入即
 *     占位）；宿主插件注入真实编排（文件对话框→UiSessionController 打开
 *     协议）后，壳入口（首页/菜单/顶栏/面板/快捷键五处同路由）即走真实
 *     打开协议。覆写只换处理器，不换描述符/谓词/登记序（§7.2 门控语义
 *     零变化——命令注册表静态白名单无运行期改写，覆写发生在装配期登记
 *     之前）。
 */
struct WorkbenchContentDeps {
    /// §10.1 注入包（原样转发——可空成员语义见 ShellWiring 注释）。
    ShellWiring wiring;
    /// 宿主形态（决定布局记忆面与尺寸折叠语义——见枚举注释）。
    WorkbenchHostKind hostKind = WorkbenchHostKind::TopLevelWindow;
    /// 宿主控件（快捷键/面板/对话框宿主；必填——空＝build() 拒绝）。
    QWidget* hostWidget = nullptr;

    // ---- 顶层窗口宿主的几何/位形承载钩子（成组提供或成组缺省）----

    /// 序列化当前窗口几何（QMainWindow::saveGeometry——UI 线程值快照）。
    std::function<QByteArray()> saveWindowGeometry;
    /// 序列化当前停靠位形（QMainWindow::saveState——含页签序/工具栏可见性）。
    std::function<QByteArray()> saveWindowState;
    /// 恢复窗口几何＋位形（返回 false＝字节在但宿主判不可恢复——§4.5 损坏
    /// 路径的判别输入；两段都成功才返回 true）。
    std::function<bool(const QByteArray& geometry, const QByteArray& state)> restoreWindowState;
    /// 恢复出厂几何＋位形（§4.4"恢复出厂位形"——宿主层构建期快照的回放）。
    std::function<void()> restoreFactoryWindowState;

    // ---- 会话入口处理器覆写（§11.5；nullopt＝§7.1 阶段 A 占位——harness 形态）----

    /// project.new 的装配期处理器覆写（宿主插件的新建编排）。
    std::optional<ICommandRegistry::CommandHandler> newProjectHandler;
    /// project.open 的装配期处理器覆写（宿主插件的打开编排）。
    std::optional<ICommandRegistry::CommandHandler> openProjectHandler;
    /// draft.save 的装配期处理器覆写（UI-T17 增量——宿主插件的保存编排：
    /// DraftController saveAll(Manual) 真实落盘链路；nullopt＝阶段 A 占位）。
    std::optional<ICommandRegistry::CommandHandler> saveProjectHandler;
    /// workbench.closeProject 的装配期处理器覆写（UI-T17 增量——宿主插件的
    /// 关闭编排：beginClose→统一确认对话框→resolveCloseDialog→Draining
    /// 防线驱动；nullopt＝阶段 A 占位）。
    std::optional<ICommandRegistry::CommandHandler> closeProjectHandler;
    /// draft.apply 的装配期处理器覆写（WP-24-T03b-2 增量——§8.5 应用编排：
    /// 域信封组装→命令网关提交→回执回写 onCommandResult；未注入＝§7.1
    /// 占位说明处理器，harness 形态不变。覆写只换处理器，描述符/谓词/
    /// 门控零变化——同上方三覆写的既有模式）。
    std::optional<ICommandRegistry::CommandHandler> applyDraftHandler;
};

// =====================================================================
// IWorkbenchContent——内容装配层契约面（§10.1 v1.10 增量）
// =====================================================================

/**
 * @brief 工作台内容装配层（两种宿主层共用的同一装配面——O-38 裁决②）。
 *
 * 生命周期/所有权：实例由宿主层独占持有（unique_ptr，经工厂取得）；注入
 *   实例（ShellWiring 各端口）所有权仍在宿主层/装配层，本层只持共享引用
 *   （§10.1 所有权行同口径）。
 *
 * 两段装配序（宿主层 chrome 与内容装配的时序契约——五区 Dock 需要先有
 *   内容 Widget 才能包裹，布局记忆恢复需要宿主层先登记可见性目标/出厂
 *   快照，因此拆为 build/activate 两段）：
 *   ①build()＝内容构建＋命令设施装配（§7.1 登记＋seal）——产物：五区内容
 *     Widget/状态行/命令注册表/快捷键表（未 attach）；
 *   ②宿主层安放 chrome（Dock 包裹/菜单/可见性目标登记/观察钩子注册）；
 *   ③activate()＝布局记忆装载（含损坏回退）＋无项目首页初始上下文（PM-10）
 *     ＋写盘线程启动＋快捷键 attach＋命令面板创建。
 * 前置条件：build 恰好一次且先于 activate；activate 恰好一次。非法使用：
 *   二次 build/activate；未 build 即 activate；shutdown 后调用除 shutdown
 *   外任何方法（违约＝Q_ASSERT＋安全空返回——调用方错误 fail-fast 的安全轨）。
 *
 * 与 IWorkbenchShell 门面的关系：门面（顶层窗口宿主层）的方法把五区/上下文/
 *   最近项目请求逐条转发到本接口——门面自身只余窗口 chrome，语义权威仍在
 *   §4/§7 的内容装配层（单一口径，harness 与插件不会漂移）。
 */
class IWorkbenchContent {
public:
    virtual ~IWorkbenchContent() = default;

    // ---- 两段装配（时序契约见类注释）----

    /**
     * @brief 阶段 A：内容构建＋命令设施装配。
     *
     * 校验（返回值轨 fail-fast，不抛——§10.1 错误类型同口径）：wiring 必填
     * 指针非空（diagSink/redaction/policySource/nameResolver——eventBus/
     * devLog 允许为空但须显式声明）；hostWidget 非空；几何/位形钩子成组。
     *
     * @return true＝构建成功；false＝校验被拒（调用方错误）或已 build 过
     */
    virtual bool build() = 0;

    /**
     * @brief 阶段 B：布局记忆装载＋初始上下文＋写线程＋快捷键/面板。
     *
     * 后置：五区按出厂位形或用户级布局记忆就位（损坏→回退出厂＋
     * UI-LAYOUT-RESTORE-FAILED（Dev，经 wiring.devLog）＋损坏段整段丢弃，
     * 不阻塞启动——§4.5）；无项目首页呈现（PM-10）；命令设施进入运行态。
     */
    virtual void activate() = 0;

    // ---- 内容 Widget 出口（宿主层安放面——ARC-02 的宿主层侧收口）----

    /// @brief 顶栏内容（§4.1 顶栏行：项目入口/写命令按钮/只读徽标）。
    virtual QWidget* topBarWidget() = 0;
    /// @brief 左栏内容（§4.2 左栏行占位——最小内容宽 240 px）。
    virtual QWidget* leftWidget() = 0;
    /// @brief 中央区页栈（页 0 无项目首页/页 1 三维视图区域——宿主形态差异
    ///        见 WorkbenchHostKind 与 createHostYield 语义；中央区不可隐藏）。
    virtual QWidget* centralWidget() = 0;
    /// @brief 右栏内容（§4.2 右栏行＋工程策略摘要卡——最小内容宽 280 px）。
    virtual QWidget* rightWidget() = 0;
    /// @brief 底部任务和状态区内容（§4.2 底部行五页签——最小内容高 160 px）。
    virtual QWidget* bottomWidget() = 0;

    // ---- 状态出口观测钩子（UI-T18 契约面变更——O-43 ③：QStatusBar*
    //      出口移除，状态投影改由宿主层承载；登记 ui.md §10.1 v1.14）----

    /**
     * @brief 注册 PM-11 永久状态文本观察者（formatProjectStatusText 唯一
     *        权威的投影面——与 setTitleTextObserver 同源同格式，宿主层据此
     *        渲染自己的状态栏永久位；不注册＝无投影（无宿主测试场景）。
     *
     * @param observer [in] 回调（UI 线程；入参＝PM-11 格式文本；空＝清除；
     *                  生命周期≤本实例）
     */
    virtual void setStatusTextObserver(std::function<void(const QString&)> observer) = 0;

    /**
     * @brief 注册瞬态状态消息观察者（showMessage 语义的投影面——命令反馈/
     *        即时可见性补偿等非阻断消息；宿主层转发自身状态栏）。
     *
     * @param observer [in] 回调（UI 线程；入参＝消息文本＋超时毫秒
     *                  [单位 ms，0＝驻留至下一条]；空＝清除）
     */
    virtual void setStatusMessageObserver(
        std::function<void(const QString& message, int timeoutMs)> observer) = 0;

    // ---- 五区可见性（§4.1/§4.4；宿主层语义差异见 WorkbenchHostKind）----

    /**
     * @brief 登记五区可见性目标（缺省＝内容 Widget 自身）。
     *
     * 顶层窗口宿主登记五区 QDockWidget（隐藏 Dock＝今日 harness 行为）；
     * 嵌入式宿主可不登记（隐藏内容 Widget 本身，栅格布局回流）。登记动作
     * 立即把当前可见性施加到新目标（chrome 安放在 activate 之前，无可见
     * 中间态）。
     *
     * @param region [in] 五区标识（Top/Central 恒在——登记为无操作）
     * @param target [in] 可见性目标控件（非 owning——宿主层持有）
     */
    virtual void setRegionVisibilityTarget(WorkbenchRegion region, QWidget* target) = 0;

    /// @brief 查询五区当前可见性（语义同 IWorkbenchShell::regionVisible）。
    virtual bool regionVisible(WorkbenchRegion region) const = 0;

    /// @brief 设置五区可见性（语义同 IWorkbenchShell::setRegionVisible）。
    virtual void setRegionVisible(WorkbenchRegion region, bool visible) = 0;

    /// @brief 恢复出厂布局（顶层＝几何/位形＋三区全可见；嵌入式＝三区全可见
    ///        ＋持久化——§4.5 按宿主形态的适配面）。
    virtual void resetLayout() = 0;

    /**
     * @brief 宿主尺寸反馈（§4.4 尺寸折叠的入口——顶层宿主从 resize 事件
     *        转发；嵌入式宿主不转发＝自动折叠关闭，见 WorkbenchHostKind）。
     *
     * @param hostSize [in] 宿主窗口当前尺寸（px）
     */
    virtual void notifyHostResized(const QSize& hostSize) = 0;

    // ---- 工作台上下文投影输入（PM-10/PM-11/§7.5——语义同门面）----

    /// @brief 注入上下文投影（语义同 IWorkbenchShell::presentProjectContext）。
    virtual void presentProjectContext(const ProjectContextProjection& context) = 0;

    /// @brief 查询壳层命令可用性（语义同 IWorkbenchShell::commandAvailability）。
    virtual ShellCommandAvailability commandAvailability(const std::string& commandId) const = 0;

    /**
     * @brief 提交壳层命令（§4.2 路由红线/§7.7 统一提交路径——宿主层菜单/
     *        入口的唯一触发路径；拒绝反馈经状态行呈现）。
     *
     * @param commandId [in] 命令 id（点分小写——§7.1）
     */
    virtual void submitCommand(const std::string& commandId) = 0;

    // ---- 最近项目（PM-10/PM-14——语义同门面）----

    /// @brief 取最近项目列表（语义同 IWorkbenchShell::recentProjects）。
    virtual std::vector<RecentProjectEntry> recentProjects() const = 0;
    /// @brief 登记一次成功打开（语义同 IWorkbenchShell::noteRecentProject）。
    virtual void noteRecentProject(const std::string& canonicalPath) = 0;
    /// @brief 移除一项（语义同 IWorkbenchShell::removeRecentProject）。
    virtual void removeRecentProject(const std::string& canonicalPath) = 0;

    // ---- 宿主层 chrome 同步观察钩子（宿主层在 activate 前注册）----

    /**
     * @brief 注册命令状态观察者（内容装配层每次刷新命令使能态后回调——
     *        宿主层据此同步菜单动作/视图开关的勾选与使能）。
     *
     * @param observer [in] 回调（UI 线程调用；空＝清除；生命周期≤本实例）
     */
    virtual void setCommandStateObserver(std::function<void()> observer) = 0;

    /**
     * @brief 注册标题文本观察者（PM-11"标题栏与状态栏"双面——每次状态
     *        文本刷新回调宿主层；顶层宿主转发 setWindowTitle，嵌入式宿主
     *        可不注册——PM-11 投影由状态行承载）。
     *
     * @param observer [in] 回调（入参＝PM-11 格式文本；UI 线程；空＝清除）
     */
    virtual void setTitleTextObserver(std::function<void(const QString&)> observer) = 0;

    // ---- 有界拆卸（§10.1/§5.6 同口径；幂等）----

    /**
     * @brief 有界拆卸：布局/设置落盘、写线程收口、快捷键 detach、命令设施
     *        停用（幂等——重复调用为空操作）。
     *
     * @return 布局/设置是否全部成功落盘（false＝写队列为空或落盘被拒——
     *         宿主层据此填充 ShellTeardownReport.layoutPersisted）
     */
    virtual bool shutdown() = 0;
};

// =====================================================================
// 装配入口
// =====================================================================

/**
 * @brief 创建内容装配层实例（宿主层独占持有——unique_ptr 所有权即刻移交）。
 *
 * 为什么是工厂函数：具体实现类封闭在库内（R-2 同 createWorkbenchShell
 * 惯例——WorkbenchContent_p.hpp 留在 src/）；两种宿主层只见本契约面。
 *
 * @param deps [in] 装配依赖（build() 内校验；构造后可复用）
 * @return 未构建的内容装配层实例（build 前 Widget 出口为 nullptr）
 */
std::unique_ptr<IWorkbenchContent> createWorkbenchContent(WorkbenchContentDeps deps);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_WORKBENCHCONTENT_HPP
