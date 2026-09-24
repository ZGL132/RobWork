/**
 * @file   WorkbenchContent_p.hpp
 * @brief  工作台内容装配层私有实现头（R-2：不进 include/、不跨单元暴露）。
 *
 * 设计依据：
 *   - units/ui.md §10.1 v1.10 增量（壳拆「内容装配层＋顶层窗口宿主层」——
 *     O-38 裁决②；公共契约面＝WorkbenchContent.hpp，本头是其唯一实现）；
 *   - WorkbenchShell_p.hpp（既有私有设施复用——WorkbenchText 文案表/
 *     RecentProjectsModel/UiSettingsWriter/LayoutMemory/
 *     createView3DPlaceholder/createPolicySummaryCard 与常量：本实现不复制
 *     任何一份语义，全部同源引用——NFR-MNT-03 单一权威）。
 *
 * 背景说明（本类承载什么）：
 *   从 WorkbenchShellImpl 拆出的全部界面语义——五区内容组装、命令设施
 *   （注册表/快捷键表/面板）、布局记忆编排、上下文投影呈现、最近项目、
 *   门控快照。宿主差异只在两处：
 *   ①布局记忆面：TopLevelWindow 形态经 WorkbenchContentDeps 的几何/位形
 *     钩子承载窗口半区（全量五键，与 UI-T03 以来逐字节一致）；EmbeddedDock
 *     形态缺省钩子＝只读写三区可见性键（同组同键，几何/位形属框架主窗口）；
 *   ②§4.4 尺寸折叠：仅 TopLevelWindow 形态生效（嵌入式宿主的尺寸是 Dock
 *     面板尺寸，量测对象不同——登记 ui.md §10.1 v1.10）。
 *
 * 线程模型：与公共契约面一致——build/activate/shutdown 允许装配线程，其余
 *   仅 UI 线程；UiSettingsWriter 的工作线程纪律（值任务、禁触 Widget）不变。
 */

#ifndef SDURWS_IRD_UI_WORKBENCHCONTENT_P_HPP
#define SDURWS_IRD_UI_WORKBENCHCONTENT_P_HPP

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSize>
#include <QStackedWidget>
#include <QString>
#include <QStatusBar>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/ui/ICommandRegistry.hpp>
#include <sdurws/ird/ui/IGlobalShortcutRegistry.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>
#include <sdurws/ird/ui/WorkbenchContent.hpp>

#include "WorkbenchShell_p.hpp"  // 既有壳私有设施（文案表/最近项目/写盘线程/布局记忆/占位面板工厂）

namespace sdurws {
namespace ird {
namespace ui {
namespace detail {

class CommandPalettePanel;  // 前置声明（面板成员指针——完整定义在 CommandPalette_p.hpp）

// =====================================================================
// WorkbenchContentImpl——IWorkbenchContent 唯一实现（R-2 私有）
// =====================================================================

/**
 * @brief 工作台内容装配层实现（顶层窗口宿主与嵌入式 Dock 宿主共用）。
 *
 * 前身＝UI-T03~T15 的 WorkbenchShellImpl 语义主体（逐行搬移，行为保持——
 * harness 回归零变化的实现面）；宿主层（WorkbenchShellImpl 新形态与
 * sdurws_ird_ui_plugin）只做安放。非线程安全：仅 UI 线程（两段装配除外）。
 */
class WorkbenchContentImpl final : public IWorkbenchContent {
public:
    /// @brief 装配依赖在工厂时给出（构造后不可替换——两段装配的输入冻结）。
    explicit WorkbenchContentImpl(WorkbenchContentDeps deps);
    ~WorkbenchContentImpl() override;  ///< 兜底收口（未 shutdown 即析构→有界拆卸）

    // ---- IWorkbenchContent（契约注释见公共头——不复制）----
    bool build() override;
    void activate() override;
    QWidget* topBarWidget() override;
    QWidget* leftWidget() override;
    QWidget* centralWidget() override;
    QWidget* rightWidget() override;
    QWidget* bottomWidget() override;
    QStatusBar* statusBarWidget() override;
    void setRegionVisibilityTarget(WorkbenchRegion region, QWidget* target) override;
    bool regionVisible(WorkbenchRegion region) const override;
    void setRegionVisible(WorkbenchRegion region, bool visible) override;
    void resetLayout() override;
    void notifyHostResized(const QSize& hostSize) override;
    void presentProjectContext(const ProjectContextProjection& context) override;
    ShellCommandAvailability commandAvailability(const std::string& commandId) const override;
    void submitCommand(const std::string& commandId) override;
    std::vector<RecentProjectEntry> recentProjects() const override;
    void noteRecentProject(const std::string& canonicalPath) override;
    void removeRecentProject(const std::string& canonicalPath) override;
    void setCommandStateObserver(std::function<void()> observer) override;
    void setTitleTextObserver(std::function<void(const QString&)> observer) override;
    bool shutdown() override;

private:
    // ---- 构建段（build 内部步骤——各函数单一职责，原 WorkbenchShellImpl 同名拆出）----
    void buildTopBar();                   ///< 顶栏内容（§4.1 顶栏行）
    void buildSideContents();             ///< 左/右栏占位内容（阶段 A）
    void buildBottomContent();            ///< 底部页签区（阶段 A 占位页签）
    void buildCentralArea();              ///< 中央区：首页页＋三维视图区域页
    QWidget* buildHomePage();             ///< 无项目首页（PM-10 三入口＋摘要）
    QWidget* buildHostYieldPage();        ///< 嵌入宿主的三维让位页（O-38 裁决③）
    void assembleCommandSystem();         ///< 命令设施装配（§7.1 登记＋seal＋默认集）
    void applyShortcutAndPaletteState();  ///< 用户改绑回放＋QShortcut attach＋面板创建
    static std::vector<HotkeyBinding> loadUserShortcutBindings(QSettings& settings); ///< 快捷键历史读取
    static std::vector<CommandId> loadPaletteRecent(QSettings& settings);            ///< 面板近期使用读取
    void openCommandPalette();            ///< 命令面板打开（workbench.commandPalette 处理器）
    void openAboutDialog();               ///< 关于对话框打开（help.about 处理器——UI-T10 §11.4）
    void openUserManualEntry();           ///< 用户手册入口（help.contents 处理器——UI-T10 §11.4）

    // ---- 布局记忆（§4.5；宿主形态差异的分派点）----
    void restorePersistedLayout();        ///< 顶层：全量五键装载（原语义逐行保持）
    void restoreRegionFlagsEmbedded();    ///< 嵌入：仅三区可见性键装载（同组同键）
    void applyFactoryLayout();            ///< 出厂位形（§4.5 回退基准——顶层带几何/位形）
    void persistLayoutAsync();            ///< 布局落盘任务提交（顶层全量/嵌入式仅键）
    void persistRecentAsync();            ///< 最近项目落盘任务提交
    void persistShortcutsAsync();         ///< 快捷键用户改绑落盘任务提交（PM-14）
    void persistPaletteRecentAsync();     ///< 面板近期使用落盘任务提交（§7.4/PM-14）

    // ---- 呈现刷新（原 WorkbenchShellImpl 同名拆出）----
    void refreshCommandStates();          ///< 命令可用性 → 顶栏按钮/观察者
    void refreshStatusBar();              ///< PM-11 状态行文本刷新（＋标题观察者）
    void refreshRecentList();             ///< 最近项目列表控件重建（PM-10）
    void refreshPolicySummaryCard();      ///< 策略摘要卡重拉端口快照（UI-T07——§6.7）
    void updateCollapseBySize();          ///< §4.4 尺寸折叠（仅顶层宿主启用）
    void emitDev(const std::string& message); ///< Dev 日志出线（devLog 为空时静默）

    /// 可见性目标解析（未登记＝内容 Widget 自身；Top/Central 无隐藏面）。
    QWidget* visibilityTarget(WorkbenchRegion region) const;
    bool* userVisibilityFlag(WorkbenchRegion region);  ///< 用户可见性存储位（可隐藏三区）

    // ---- 装配依赖（工厂注入冻结）----
    WorkbenchContentDeps m_deps;           ///< 注入包（build 校验后持有；wiring 在内）

    // ---- 门控事实与模型（原 WorkbenchShellImpl 同名拆出）----
    UiContextSnapshot m_gate;              ///< 当前门控快照（§7.5——presentProjectContext 维护）
    RecentProjectsModel m_recent;          ///< 最近项目模型（PM-10）
    UiSettingsWriter m_settingsWriter;     ///< 用户级设置写盘线程（§3.4）
    bool m_layoutPersisted = false;        ///< shutdown 落盘结论（幂等拆卸的返回值）

    // ---- 命令设施（SA-16 唯一入口；所有权在本层）----
    std::unique_ptr<ICommandRegistry> m_commands;        ///< 命令注册表（§10.3）
    std::unique_ptr<IGlobalShortcutRegistry> m_shortcuts;///< 全局快捷键表（§10.4）
    CommandPalettePanel* m_palette = nullptr;            ///< 命令面板（宿主控件 Qt 父子树管理）
    std::vector<HotkeyBinding> m_pendingUserBindings;    ///< 装配期读取的快捷键历史（activate 回放）
    std::vector<CommandId> m_pendingPaletteRecent;       ///< 装配期读取的面板近期使用（同上回放）

    // ---- 策略摘要卡（UI-T07——§6.7；钩子为重建闭包）----
    std::function<void()> m_refreshPolicyCard;  ///< 卡刷新钩子（UI 线程调用）

    // ---- 宿主层 chrome 同步观察钩子 ----
    std::function<void()> m_commandStateObserver;                 ///< 命令状态观察（菜单使能同步）
    std::function<void(const QString&)> m_titleTextObserver;      ///< PM-11 标题文本观察

    // ---- 内容 Widget 树（shutdown 后全部置空防悬垂）----
    QStatusBar* m_statusBar = nullptr;         ///< 状态行（PM-11 永久标签的承载者）
    QStackedWidget* m_centralStack = nullptr;  ///< 中央区页栈（首页/三维视图区域）
    QLabel* m_statusText = nullptr;            ///< PM-11 文本标签（状态行内永久位）
    QLabel* m_readonlyBadge = nullptr;         ///< 顶栏只读徽标
    QListWidget* m_homeRecentList = nullptr;   ///< 首页最近项目列表
    std::array<QWidget*, 5> m_regionWidgets{}; ///< 五区内容 Widget（WorkbenchRegion 枚举序）
    std::array<QWidget*, 5> m_regionTargets{}; ///< 五区可见性目标（缺省＝内容 Widget 自身）

    // ---- 状态位 ----
    bool m_built = false;                      ///< build 已成功（恰好一次判据）
    bool m_activated = false;                  ///< activate 已执行（恰好一次判据）
    bool m_shutdownDone = false;               ///< 拆卸已完成（幂等判据）
    bool m_collapsedBySize = false;            ///< 当前处于尺寸折叠态（§4.4——仅顶层）
    /// 三区用户可见性意愿位（§4.4 折叠不覆盖用户意愿；Top/Central 恒在无此位）。
    bool m_visibleLeft = true;
    bool m_visibleRight = true;
    bool m_visibleBottom = true;
    ProjectContextProjection m_context{};      ///< 最近一次注入的上下文快照
    std::vector<std::pair<QPushButton*, std::string>> m_topButtons;  ///< 顶栏按钮→命令 id
};

}  // namespace detail
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_WORKBENCHCONTENT_P_HPP
