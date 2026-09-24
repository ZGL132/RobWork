/**
 * @file   WorkbenchContent.cpp
 * @brief  工作台内容装配层实现：五区内容组装、命令设施、跨会话布局记忆
 *         （含损坏回退＋Dev 诊断）、无项目首页（PM-10 三入口）、PM-11 状态
 *         行格式与壳层命令可用性门控——顶层窗口宿主（harness）与嵌入式
 *         Dock 宿主（sdurws_ird_ui_plugin）共用的同一装配面。
 *
 * 设计依据：
 *   - units/ui.md §4.1~§4.6（五区布局总图/区域交互边界/空项目首页/响应式
 *     尺寸与最小可用布局/布局状态归属/用户级设置项）、§7.1~§7.6（命令注册
 *     表登记协议/快捷键表/命令面板/谓词/只读条件）、§10.1 v1.10 增量
 *     （壳拆「内容装配层＋顶层窗口宿主层」——O-38 裁决②：harness 与插件
 *     共用同一内容装配面，五区交互/布局记忆/命令门控语义不变）、§10.3/
 *     §10.4（两注册表接口）、§3.4（线程模型与后台落盘线程）、§3.5
 *     （UI-LAYOUT-RESTORE-FAILED 出线口径）、§11.4（不虚构业务能力——
 *     占位说明口径）、§11.5（触发时机编排归装配层——会话入口覆写面）；
 *   - 需求 UX-09/UX-13/UX-14/PM-07/PM-10/PM-11/PM-14；任务契约 tasks/
 *     foundation/UI-T16.json（本拆分任务的实现面）与 UI-T03/UI-T06/UI-T07/
 *     UI-T10（被搬移语义的原始任务卡——行为基准）。
 *
 * 搬移与行为保持声明（harness 回归零变化的实现依据）：
 *   本文件的主体逻辑自原 WorkbenchShellImpl（UI-T03~T15 形态）逐行搬移；
 *   仅在两处按宿主形态分派（差异登记 ui.md §10.1 v1.10）：
 *   ①布局记忆：TopLevelWindow 形态＝全量五键（几何/位形经宿主层钩子承载，
 *     序列与损坏处置逐行保持）；EmbeddedDock 形态＝仅三区可见性键（同组
 *     同键——插件无顶层窗口，几何/位形属框架主窗口）；
 *   ②§4.4 尺寸折叠：仅 TopLevelWindow 形态启用（该规则量测顶层窗口尺寸；
 *     嵌入式宿主的量测对象是 Dock 面板，量测对象不同＝形态误配，随实现
 *     登记关闭）。
 *   ③三维视图区域页：TopLevelWindow 形态＝占位面板（UI-T05 阶段 A 契约
 *     形态不变）；EmbeddedDock 形态＝让位页（三维视图由宿主主窗口承载，
 *     占位面板不冒名——O-38 裁决③"让位形态"）。
 */

#include "WorkbenchContent_p.hpp"
#include "CommandPalette_p.hpp"

#include <QDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <string_view>

#include <sdurws/ird/ui/AboutDialog.hpp>  // 关于框装配/工厂/手册入口（UI-T10——§11.4）
#include <sdurws/ird/ui/UiProjections.hpp>

namespace sdurws {
namespace ird {
namespace ui {
namespace detail {

// =====================================================================
// 生命周期（两段装配——§10.1 v1.10 时序契约）
// =====================================================================

WorkbenchContentImpl::WorkbenchContentImpl(WorkbenchContentDeps deps)
    : m_deps(std::move(deps))
{
}

WorkbenchContentImpl::~WorkbenchContentImpl()
{
    // 兜底有界拆卸：宿主层未显式 shutdown 即析构时保证布局落盘尝试与
    // 设施收口仍发生（正常路径 shutdown 已幂等收口——此处为空操作）。
    if (m_built && !m_shutdownDone) {
        shutdown();
    }
}

bool WorkbenchContentImpl::build()
{
    // 恰好一次（两段装配时序契约）：二次调用＝调用方契约违约，走返回值轨
    // fail-fast（§10.1 错误类型行"返回 bool/report，不抛"同口径）。
    if (m_built) {
        return false;
    }
    // 必填校验（§10.1 前置条件行逐条承袭——eventBus 之外一律非空；devLog 为
    // 可空成员——与 eventBus 同款"允许为空＝无日志测试场景，须显式声明"）。
    if (!m_deps.wiring.diagSink || !m_deps.wiring.redaction
        || !m_deps.wiring.policySource || !m_deps.wiring.nameResolver) {
        return false;
    }
    // 宿主控件必填：QShortcut attach、命令面板与对话框的父窗口、内容 Widget
    // 的初始父对象都依赖它（空＝没有安放目标，装配无从谈起）。
    if (m_deps.hostWidget == nullptr) {
        return false;
    }
    // 几何/位形钩子必须成组：顶层形态提供全量四钩子，嵌入式形态全缺省——
    // 半有半无＝装配描述自相矛盾（调用方错误），拒绝而不是猜测意图。
    const bool hasWindowHooks = static_cast<bool>(m_deps.saveWindowGeometry)
                                && static_cast<bool>(m_deps.saveWindowState)
                                && static_cast<bool>(m_deps.restoreWindowState)
                                && static_cast<bool>(m_deps.restoreFactoryWindowState);
    const bool hasNoWindowHooks = !m_deps.saveWindowGeometry
                                  && !m_deps.saveWindowState
                                  && !m_deps.restoreWindowState
                                  && !m_deps.restoreFactoryWindowState;
    if (!hasWindowHooks && !hasNoWindowHooks) {
        return false;
    }

    // 内容构建序（每步产物是下一步的输入；与原 WorkbenchShellImpl 的
    // buildWindow 拆分一致——五区内容先于命令设施，命令设施先于
    // activate 期的任何刷新触达点）：
    //   五区内容 → 命令设施装配（§7.1 登记＋seal）。
    //   （UI-T18：①状态行环节移除——QStatusBar* 出口改双观测钩子投影
    //   宿主层，本层零状态栏 Widget。）
    // 所有内容 Widget 以宿主控件为初始父对象（Qt 父子树托管生命周期——
    // 宿主层随后的 Dock 包裹会自动重挂父子）。

    buildTopBar();
    buildSideContents();
    buildBottomContent();
    buildCentralArea();

    // 可见性目标缺省＝内容 Widget 自身（嵌入式宿主直接可用；顶层宿主在
    // activate 前以 QDockWidget 覆盖登记）。
    for (std::size_t i = 0; i < m_regionWidgets.size(); ++i) {
        m_regionTargets[i] = m_regionWidgets[i];
    }

    assembleCommandSystem();

    m_built = true;
    return true;
}

void WorkbenchContentImpl::activate()
{
    // 时序契约：build 之后恰好一次（违约＝未定义序列，DT 断言＋安全返回）。
    if (!m_built || m_activated || m_shutdownDone) {
        Q_ASSERT(false && "activate 违反两段装配时序（未 build／重复 activate／已拆卸）");
        return;
    }

    {
        // 读盘（UI 线程、写线程未启动——§3.4 读写不重叠）：最近项目与布局
        // 同源用户级设置，但分属不同键组（布局损坏整段丢弃不波及相邻组）。
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           kSettingsOrg, kSettingsApp);
        m_recent.load(settings.value(QString(kRecentGroup) + '/' + kRecentKey)
                          .toStringList());
        m_pendingUserBindings = loadUserShortcutBindings(settings);
        m_pendingPaletteRecent = loadPaletteRecent(settings);
    }

    // 布局记忆装载（宿主形态分派见 restorePersistedLayout；损坏回退不阻塞
    // 启动——§4.5）→ 最近项目列表就位。
    restorePersistedLayout();
    refreshRecentList();

    // 启动即无项目首页（PM-10：壳启动时无会话，首页三入口就位；快照同步
    // 推送注册表——命令可用性求值与状态行/首页消费同一份）。
    presentProjectContext(ProjectContextProjection{});

    // 写线程在读盘完成后启动（§3.4 后台落盘线程；此后所有设置写入经队列）。
    m_settingsWriter.start();

    // 快捷键历史回放（PM-14：User 绑定覆盖默认集；冲突条目丢弃＋Dev 日志
    // ——不阻塞启动）→ QShortcut attach（唯一创建点，宿主＝宿主控件）→
    // 命令面板创建（workbench.commandPalette 处理器的打开目标）→ 面板近期
    // 使用回放（§7.4 置顶分组的持久化半区）。
    applyShortcutAndPaletteState();

    m_activated = true;
}

// =====================================================================
// 内容 Widget 出口（宿主层安放面）
// =====================================================================

QWidget* WorkbenchContentImpl::topBarWidget()
{
    return m_regionWidgets[static_cast<std::size_t>(WorkbenchRegion::Top)];
}

QWidget* WorkbenchContentImpl::leftWidget()
{
    return m_regionWidgets[static_cast<std::size_t>(WorkbenchRegion::Left)];
}

QWidget* WorkbenchContentImpl::centralWidget()
{
    return m_centralStack;
}

QWidget* WorkbenchContentImpl::rightWidget()
{
    return m_regionWidgets[static_cast<std::size_t>(WorkbenchRegion::Right)];
}

QWidget* WorkbenchContentImpl::bottomWidget()
{
    return m_regionWidgets[static_cast<std::size_t>(WorkbenchRegion::Bottom)];
}

void WorkbenchContentImpl::setStatusTextObserver(
    std::function<void(const QString&)> observer)
{
    // PM-11 永久投影钩子（UI-T18——O-43 ③契约面变更）：文本仍以
    // formatProjectStatusText 为唯一权威（refreshStatusBar 现取现投），
    // 本层不再持有任何状态栏 Widget。空回调＝清除（无宿主测试场景）。
    m_statusTextObserver = std::move(observer);
}

void WorkbenchContentImpl::setStatusMessageObserver(
    std::function<void(const QString& message, int timeoutMs)> observer)
{
    // 瞬态消息投影钩子（UI-T18 同上）：原 QStatusBar::showMessage 语义
    // 原样移交宿主层（文本＋超时 ms），本层零加工零排队。
    m_statusMessageObserver = std::move(observer);
}

// =====================================================================
// 五区可见性（§4.1/§4.4；宿主形态语义差异见 WorkbenchContentDeps 注释）
// =====================================================================

void WorkbenchContentImpl::setRegionVisibilityTarget(WorkbenchRegion region,
                                                     QWidget* target)
{
    if (!m_built || target == nullptr) {
        return;  // 未构建/空目标＝调用方错误（安全轨）；Top/Central 的登记同为无操作
    }
    bool* flag = userVisibilityFlag(region);
    if (flag == nullptr) {
        return;  // Top/Central 恒在（§4.4）——无可见性目标语义
    }
    m_regionTargets[static_cast<std::size_t>(region)] = target;
    // 登记即施加当前有效可见性（chrome 安放发生在 activate 之前——无可见
    // 中间态；折叠态按折叠语义呈现）。
    target->setVisible(*flag && !m_collapsedBySize);
}

bool* WorkbenchContentImpl::userVisibilityFlag(WorkbenchRegion region)
{
    switch (region) {
    case WorkbenchRegion::Left: return &m_visibleLeft;
    case WorkbenchRegion::Right: return &m_visibleRight;
    case WorkbenchRegion::Bottom: return &m_visibleBottom;
    default: return nullptr;  // Top/Central 恒在——无用户可见性存储位
    }
}

QWidget* WorkbenchContentImpl::visibilityTarget(WorkbenchRegion region) const
{
    return m_regionTargets[static_cast<std::size_t>(region)];
}

bool WorkbenchContentImpl::regionVisible(WorkbenchRegion region) const
{
    // Top/Central 恒在（§4.4 最小可用布局三要素之二——第三要素为状态行，
    // 状态行无隐藏入口）。
    if (region == WorkbenchRegion::Top || region == WorkbenchRegion::Central) {
        return true;
    }
    // 折叠态返回实际呈现（§4.4 折叠＝用户当前看不到该区）；非折叠返回
    // 用户意愿位。用模型位而非 Widget isVisible：窗口未显示阶段语义仍确定。
    const bool* flag = const_cast<WorkbenchContentImpl*>(this)->userVisibilityFlag(region);
    if (m_collapsedBySize) {
        return false;
    }
    return flag != nullptr && *flag;
}

void WorkbenchContentImpl::setRegionVisible(WorkbenchRegion region, bool visible)
{
    // shutdown 后调用＝契约"非法"行（安全轨：安全返回不崩溃，DT 断言面）。
    if (!m_built || m_shutdownDone) {
        Q_ASSERT(false && "shutdown 后调用 setRegionVisible（契约非法调用）");
        return;
    }
    bool* flag = userVisibilityFlag(region);
    if (flag == nullptr) {
        return;  // Top/Central 恒在：不接受隐藏（§4.4）——无操作
    }
    *flag = visible;
    // 折叠态不立即显示（展开时按用户意愿恢复——折叠不覆盖用户意愿）。
    visibilityTarget(region)->setVisible(visible && !m_collapsedBySize);
    // 嵌入式宿主：可见性变更即时落盘（该形态没有"关窗落盘"时机——插件
    // 不拥有应用生命周期；单键微写，§3.4 后台线程纪律不变）。
    // 顶层宿主：保持原落盘点（resetLayout/shutdown——harness 行为零变化）。
    if (m_deps.hostKind == WorkbenchHostKind::EmbeddedDock) {
        persistLayoutAsync();
    }
    refreshCommandStates();  // 视图开关勾选态同步（经观察者回调宿主层）
}

void WorkbenchContentImpl::resetLayout()
{
    if (!m_built || m_shutdownDone) {
        Q_ASSERT(false && "shutdown 后调用 resetLayout（契约非法调用）");
        return;
    }
    // 出厂位形恢复（§4.4/§4.5）按宿主形态分派：
    //   顶层＝几何＋停靠位形经出厂钩子回放（restoreState 会把浮动窗口重新
    //   停靠＝"清除该会话的浮动窗口记忆"原文），随后按当前尺寸重判折叠；
    //   嵌入式＝无窗口半区，只恢复三区可见性出厂值。
    if (m_deps.restoreFactoryWindowState) {
        m_deps.restoreFactoryWindowState();
    }
    m_visibleLeft = true;
    m_visibleRight = true;
    m_visibleBottom = true;
    m_collapsedBySize = false;
    updateCollapseBySize();       // 按当前尺寸重判折叠（出厂恢复≠免折叠——顶层语义）
    // 嵌入式宿主：折叠机制关闭，直接把三区目标恢复为可见。
    if (m_deps.hostKind == WorkbenchHostKind::EmbeddedDock) {
        for (const WorkbenchRegion region :
             {WorkbenchRegion::Left, WorkbenchRegion::Right, WorkbenchRegion::Bottom}) {
            visibilityTarget(region)->setVisible(true);
        }
    }
    refreshCommandStates();
    persistLayoutAsync();         // 用户级设置同步（PM-14——复位即持久化）
}

void WorkbenchContentImpl::notifyHostResized(const QSize& hostSize)
{
    if (!m_built || m_shutdownDone) {
        return;
    }
    // §4.4 尺寸折叠只对顶层窗口宿主启用（嵌入式宿主的尺寸是 Dock 面板
    // 尺寸——量测对象不同，登记 ui.md §10.1 v1.10 后随形态关闭）。
    if (m_deps.hostKind != WorkbenchHostKind::TopLevelWindow) {
        return;
    }
    updateCollapseBySize();
}

void WorkbenchContentImpl::updateCollapseBySize()
{
    if (!m_built || m_deps.hostWidget == nullptr) {
        return;
    }
    // §4.4 尺寸折叠只对顶层窗口宿主启用（嵌入式宿主的尺寸是 Dock 面板
    // 尺寸——量测对象不同，登记 ui.md §10.1 v1.10 后随形态关闭；嵌入式
    // 恒不折叠，m_collapsedBySize 恒 false）。
    if (m_deps.hostKind != WorkbenchHostKind::TopLevelWindow) {
        return;
    }
    // 低于最小窗口（1280×720）时左/右栏自动折叠、底栏折叠为单行
    // （顶栏＋中央区＋状态行恒在——最小可用布局定义）。尺寸取宿主控件
    // 当前值（顶层形态下宿主控件＝主窗口，与原实现读取对象一致）。
    const bool below = m_deps.hostWidget->width() < kCollapseWidth
                       || m_deps.hostWidget->height() < kCollapseHeight;
    if (below == m_collapsedBySize) {
        return;  // 状态无变化（resize 拖动过程中的重复事件——无操作）
    }
    m_collapsedBySize = below;
    for (const WorkbenchRegion region :
         {WorkbenchRegion::Left, WorkbenchRegion::Right, WorkbenchRegion::Bottom}) {
        const bool* flag = userVisibilityFlag(region);
        visibilityTarget(region)->setVisible(flag != nullptr && *flag && !below);
    }
}

// =====================================================================
// 布局记忆装载与持久化（§4.5/PM-14；宿主形态分派点）
// =====================================================================

void WorkbenchContentImpl::restorePersistedLayout()
{
    // 读盘在 UI 线程（写线程未启动——§3.4 读写不重叠）。
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       kSettingsOrg, kSettingsApp);

    // 嵌入式宿主：只承载三区可见性半区（几何/位形属框架主窗口——登记
    // ui.md §10.1 v1.10；损坏处置与顶层同纪律）。
    if (!m_deps.restoreWindowState) {
        restoreRegionFlagsEmbedded();
        return;
    }

    // ---- 顶层窗口宿主：全量五键装载（UI-T03 以来语义逐行保持）----
    const LayoutMemory::LoadResult loaded = LayoutMemory::load(settings);

    if (loaded.kind == LayoutMemory::LoadResult::Kind::Corrupt) {
        // §4.5 原文三件事：①回退出厂默认布局；②UI-LAYOUT-RESTORE-FAILED
        // （Dev 诊断——经开发日志通道出线，不阻塞启动）；③损坏段整段丢弃。
        LayoutMemory::discard(settings);
        applyFactoryLayout();
        emitDev(std::string(WorkbenchText::kLayoutRestoreFailedCode)
                + ": 布局记忆损坏或版本不识别——已回退出厂默认布局，"
                  "损坏的用户设置段已整段丢弃（不阻塞启动）");
        return;
    }
    if (loaded.kind == LayoutMemory::LoadResult::Kind::Absent) {
        applyFactoryLayout();  // 出厂首次：静默取出厂位形（不制造告警）
        return;
    }

    // 正常恢复：先几何/停靠位形（restoreState 按 objectName 匹配 Dock——
    // 宿主层已构建），再叠加用户可见性键。
    const bool stateOk = m_deps.restoreWindowState(loaded.geometry, loaded.state);
    if (!stateOk) {
        // 字节在但 Qt 判不可恢复＝损坏同路径（§4.5"设置损坏"）。
        LayoutMemory::discard(settings);
        applyFactoryLayout();
        emitDev(std::string(WorkbenchText::kLayoutRestoreFailedCode)
                + ": 布局记忆解析失败——已回退出厂默认布局，损坏的用户设置段"
                  "已整段丢弃（不阻塞启动）");
        return;
    }
    m_visibleLeft = loaded.visibleLeft;
    m_visibleRight = loaded.visibleRight;
    m_visibleBottom = loaded.visibleBottom;
    for (const WorkbenchRegion region :
         {WorkbenchRegion::Left, WorkbenchRegion::Right, WorkbenchRegion::Bottom}) {
        visibilityTarget(region)->setVisible(*userVisibilityFlag(region));
    }
}

void WorkbenchContentImpl::restoreRegionFlagsEmbedded()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       kSettingsOrg, kSettingsApp);
    const LayoutMemory::FlagsLoadResult loaded = LayoutMemory::loadFlags(settings);

    if (loaded.kind == LayoutMemory::FlagsLoadResult::Kind::Corrupt) {
        // §4.5 同纪律的嵌入式适配面：损坏＝回退三区全可见＋Dev 诊断＋旗标
        // 半区整段丢弃（几何/位形键不属本形态读写范围，不动）。
        LayoutMemory::discard(settings);
        emitDev(std::string(WorkbenchText::kLayoutRestoreFailedCode)
                + ": 布局记忆损坏或版本不识别（嵌入宿主形态，旗标半区）——"
                  "已回退三区全可见，损坏的用户设置段已整段丢弃（不阻塞启动）");
        return;
    }
    if (loaded.kind == LayoutMemory::FlagsLoadResult::Kind::Absent) {
        return;  // 出厂首次：默认全可见（成员初值），不制造告警
    }

    // 正常恢复：旗标生效到可见性目标（Dock 面板常驻宿主窗口，无折叠半区）。
    m_visibleLeft = loaded.visibleLeft;
    m_visibleRight = loaded.visibleRight;
    m_visibleBottom = loaded.visibleBottom;
    for (const WorkbenchRegion region :
         {WorkbenchRegion::Left, WorkbenchRegion::Right, WorkbenchRegion::Bottom}) {
        visibilityTarget(region)->setVisible(*userVisibilityFlag(region));
    }
}

void WorkbenchContentImpl::applyFactoryLayout()
{
    // 出厂位形（宿主层构建期快照）：顶层＝几何＋停靠位形经钩子回放＋三区
    // 全可见＋按当前尺寸重判折叠；嵌入式＝仅三区全可见（无窗口半区）。
    if (m_deps.restoreFactoryWindowState) {
        m_deps.restoreFactoryWindowState();
    }
    m_visibleLeft = true;
    m_visibleRight = true;
    m_visibleBottom = true;
    m_collapsedBySize = false;
    updateCollapseBySize();
    if (m_deps.hostKind == WorkbenchHostKind::EmbeddedDock) {
        for (const WorkbenchRegion region :
             {WorkbenchRegion::Left, WorkbenchRegion::Right, WorkbenchRegion::Bottom}) {
            visibilityTarget(region)->setVisible(true);
        }
    }
    refreshCommandStates();
}

void WorkbenchContentImpl::persistLayoutAsync()
{
    if (!m_built || m_shutdownDone) {
        return;
    }
    // 嵌入式宿主：只写版本键＋三旗标（不触碰顶层形态的几何/位形键——
    // LayoutMemory::storeFlags 的键面边界）。
    if (!m_deps.saveWindowGeometry) {
        const bool visibleLeft = m_visibleLeft;
        const bool visibleRight = m_visibleRight;
        const bool visibleBottom = m_visibleBottom;
        const bool accepted = m_settingsWriter.enqueue([visibleLeft, visibleRight,
                                                        visibleBottom](QSettings& s) {
            LayoutMemory::storeFlags(s, visibleLeft, visibleRight, visibleBottom);
        });
        if (!accepted) {
            // 写线程未运行（shutdown 后）——如实留痕不静默。
            emitDev("布局旗标落盘任务被拒（写线程未运行）——本次可见性变更未持久化");
        }
        return;
    }

    // 顶层宿主：值序列化在 UI 线程完成（§3.4 纪律：落盘线程不触碰 Widget
    // ——任务体只拿已拷贝的字节与布尔位；几何/位形经宿主层钩子取得）。
    const QByteArray geometry = m_deps.saveWindowGeometry();
    const QByteArray state = m_deps.saveWindowState();
    const bool visibleLeft = m_visibleLeft;
    const bool visibleRight = m_visibleRight;
    const bool visibleBottom = m_visibleBottom;
    const bool accepted = m_settingsWriter.enqueue([geometry, state, visibleLeft,
                                                    visibleRight, visibleBottom](QSettings& s) {
        LayoutMemory::store(s, geometry, state, visibleLeft, visibleRight, visibleBottom);
    });
    if (!accepted) {
        // 写线程未运行（shutdown 后）——如实留痕不静默（layoutPersisted
        // 将为 false，下次启动回到上次成功落盘位形）。
        emitDev("布局落盘任务被拒（写线程未运行）——本次布局变更未持久化");
    }
}

void WorkbenchContentImpl::persistRecentAsync()
{
    const QStringList snapshot = m_recent.stored();  // 值快照（禁触 Widget）
    m_settingsWriter.enqueue([snapshot](QSettings& s) {
        s.setValue(QString(kRecentGroup) + '/' + kRecentKey, snapshot);
    });
}

void WorkbenchContentImpl::persistShortcutsAsync()
{
    // 值序列化在 UI 线程（§3.4 纪律）：User 绑定集→"commandId\t键" 行表。
    const std::vector<HotkeyBinding> bindings =
        m_shortcuts ? m_shortcuts->bindings() : std::vector<HotkeyBinding>{};
    QStringList lines;
    for (const HotkeyBinding& binding : bindings) {
        if (binding.origin == BindingOrigin::User) {
            // Default 集随产品版本冻结（§7.3）——持久化只落 User 集。
            lines << QString::fromStdString(binding.command) + QLatin1Char('\t')
                           + binding.key.toString(QKeySequence::PortableText);
        }
    }
    m_settingsWriter.enqueue([lines](QSettings& s) {
        s.setValue(QString(kShortcutGroup) + '/' + kShortcutKey, lines);
    });
}

void WorkbenchContentImpl::persistPaletteRecentAsync()
{
    const QStringList snapshot = [this] {
        QStringList list;
        if (m_commands) {
            for (const CommandId& id : m_commands->recentUsed()) {
                list << QString::fromStdString(id);
            }
        }
        return list;
    }();
    m_settingsWriter.enqueue([snapshot](QSettings& s) {
        s.setValue(QString(kPaletteRecentGroup) + '/' + kPaletteRecentKey, snapshot);
    });
}

// =====================================================================
// 内容构建：五区组装（§4.1 布局总图）
// =====================================================================

void WorkbenchContentImpl::buildTopBar()
{
    // 顶栏内容（§4.1 顶栏行）：宿主层决定容器形态（顶层＝Dock 包裹＋空
    // 标题栏；嵌入式＝栅格行）——本层只构建内容条。
    auto* bar = new QWidget(m_deps.hostWidget);
    bar->setObjectName("ird_top_bar_content");
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 2, 4, 2);

    // 项目入口▾（下拉：新建/打开/关闭——与文件菜单同命令，§4.2 路由红线
    // 同样适用：只经壳层提交路径）。
    auto* projectEntry = new QToolButton(bar);
    projectEntry->setText(QString::fromUtf8(WorkbenchText::kMenuFile) + QString::fromUtf8(u8"入口"));
    projectEntry->setPopupMode(QToolButton::InstantPopup);
    QMenu* entryMenu = new QMenu(projectEntry);
    entryMenu->addAction(QString::fromUtf8(WorkbenchText::kCmdNewProject), projectEntry,
                         [this] { submitCommand("project.new"); });
    entryMenu->addAction(QString::fromUtf8(WorkbenchText::kCmdOpenProject), projectEntry,
                         [this] { submitCommand("project.open"); });
    entryMenu->addAction(QString::fromUtf8(WorkbenchText::kCmdCloseProject), projectEntry,
                         [this] { submitCommand("workbench.closeProject"); });
    projectEntry->setMenu(entryMenu);
    layout->addWidget(projectEntry);

    // 阶段导航条/当前方案工况指示/任务状态指示：§4.1 顶栏行的阶段 A 占位
    // （阶段导航归 UI-T09、方案工况随项目流程任务、任务状态随 UI-T13——
    // 占位说明不虚构能力，§11.4）。三处共用同一占位说明文本。
    const QString topPlaceholder =
        QString::fromUtf8(u8"阶段导航（本阶段将在后续版本提供）");
    QLabel* stageNav = new QLabel(topPlaceholder, bar);
    layout->addWidget(stageNav);
    QLabel* scheme = new QLabel(topPlaceholder, bar);
    layout->addWidget(scheme);

    // 写命令按钮组（§4.2 顶栏行：保存草稿/应用修改/撤销/重做——全部路由
    // 命令板，可用性随上下文刷新）。
    const std::pair<const char*, const char*> buttons[] = {
        {WorkbenchText::kCmdSaveDraft, "draft.save"},
        {WorkbenchText::kCmdApplyDraft, "draft.apply"},
        {WorkbenchText::kCmdUndo, "project.undo"},
        {WorkbenchText::kCmdRedo, "project.redo"},
    };
    for (const auto& [title, commandId] : buttons) {
        auto* button = new QPushButton(QString::fromUtf8(title), bar);
        QObject::connect(button, &QPushButton::clicked, bar,
                [this, commandId] { submitCommand(commandId); });
        m_topButtons.emplace_back(button, commandId);
        layout->addWidget(button);
    }

    QLabel* taskState = new QLabel(topPlaceholder, bar);
    layout->addWidget(taskState);

    // 只读徽标（PM-07/§4.2 顶栏行）：可见性随上下文（refreshCommandStates）。
    m_readonlyBadge = new QLabel(QString::fromUtf8(u8"只读"), bar);
    m_readonlyBadge->setObjectName("ird_readonly_badge");
    m_readonlyBadge->setVisible(false);
    layout->addWidget(m_readonlyBadge);

    layout->addStretch(1);  // 其余控件靠左，徽标后弹性收尾
    bar->setLayout(layout);
    m_regionWidgets[static_cast<std::size_t>(WorkbenchRegion::Top)] = bar;
}

void WorkbenchContentImpl::buildSideContents()
{
    // 左栏（§4.2 左栏行：项目对象树＋阶段任务列表——对象树需查询端口＋
    // 名称解析消费（阶段 B）、任务列表由阶段就绪投影驱动（UI-T09）；阶段 A
    // 占位说明，§11.4 不虚构能力）。最小内容宽 240 px（§4.4 原文数值）。
    auto* leftContent = new QWidget(m_deps.hostWidget);
    leftContent->setObjectName("ird_left_content");
    leftContent->setMinimumWidth(kLeftDockMinWidth);
    auto* leftLayout = new QVBoxLayout(leftContent);
    leftLayout->addWidget(new QLabel(u8"项目对象树（本阶段将在后续版本提供）", leftContent));
    leftLayout->addWidget(new QLabel(u8"阶段任务列表（本阶段将在后续版本提供）", leftContent));
    leftLayout->addStretch(1);
    m_regionWidgets[static_cast<std::size_t>(WorkbenchRegion::Left)] = leftContent;

    // 右栏（§4.2 右栏行：属性编辑区＋诊断与设置区——属性编辑阶段 B 域
    // 编辑器、诊断摘要 UI-T13、策略摘要入口 UI-T07）。最小内容宽 280 px。
    auto* rightContent = new QWidget(m_deps.hostWidget);
    rightContent->setObjectName("ird_right_content");
    rightContent->setMinimumWidth(kRightDockMinWidth);
    auto* rightLayout = new QVBoxLayout(rightContent);
    rightLayout->addWidget(new QLabel(u8"属性编辑区（本阶段将在后续版本提供）", rightContent));
    rightLayout->addWidget(new QLabel(u8"诊断与设置区（本阶段将在后续版本提供）", rightContent));
    // 工程策略摘要只读卡（UI-T07——§6.7"诊断与设置区"内嵌策略摘要入口）：
    // 数据源＝ShellWiring.policySource（O-31 ui 自有端口，L5 适配
    // policy::IPolicyProvider——§6.7 摘要只读面语义不变；build 前置
    // 已校验非空，此处恒可解引用）；行装配与分组异名语义在
    // PolicySummaryCard 模型层（GUI 只渲染）。右侧既有的"策略摘要入口
    // 占位"标签由真实卡取代——策略未装载时卡内呈占位行（不虚构数值），
    // 仍是契约显式设计而非未完成实现。
    rightLayout->addWidget(createPolicySummaryCard(*m_deps.wiring.policySource,
                                                   &m_refreshPolicyCard, rightContent));
    rightLayout->addStretch(1);
    m_regionWidgets[static_cast<std::size_t>(WorkbenchRegion::Right)] = rightContent;
}

void WorkbenchContentImpl::buildBottomContent()
{
    // 底部任务和状态区（§4.2 底部行：五页签——结果/任务进度/诊断/日志/
    // 下一步建议。呈现模型随 UI-T13、状态词随 UI-T04、下一步建议由
    // workflow 提供（阶段 A 占位——§4.2 底部行明示）。最小内容高 160 px。
    auto* tabs = new QTabWidget(m_deps.hostWidget);
    tabs->setObjectName("ird_bottom_tabs");
    tabs->setMinimumHeight(kBottomDockMinHeight);
    const char* tabTitles[] = {
        WorkbenchText::kTabResults, WorkbenchText::kTabTasks, WorkbenchText::kTabDiagnostics,
        WorkbenchText::kTabLog, WorkbenchText::kTabAdvice,
    };
    for (const char* title : tabTitles) {
        auto* page = new QLabel(u8"本阶段将在后续版本提供", tabs);
        page->setAlignment(Qt::AlignCenter);
        tabs->addTab(page, QString::fromUtf8(title));
    }
    m_regionWidgets[static_cast<std::size_t>(WorkbenchRegion::Bottom)] = tabs;
}

void WorkbenchContentImpl::buildCentralArea()
{
    // 中央区＝页栈：页 0 无项目首页（PM-10）、页 1 三维视图区域页（宿主
    // 形态分派——O-38 裁决③"让位形态"）：
    //   顶层窗口宿主＝三维视图占位面板（§4.1 阶段 A 契约显式设计——UI-T05
    //   阶段 A 起承载三维视图区域身份与 UX-11 交互清单＋KIN-06 会话姿态
    //   语义的契约登记，交互实现归阶段 B）；
    //   嵌入式宿主＝让位页——三维视图本体由宿主主窗口承载（框架
    //   RWStudioView3D 与本工作台 Dock 共存），占位面板不得在该形态下
    //   冒名"三维视图区域"（不虚构能力，§11.4）；纯只读呈现零交互控件。
    // 中央区不可隐藏（§4.4）。
    m_centralStack = new QStackedWidget(m_deps.hostWidget);
    m_centralStack->setObjectName("ird_central_stack");
    m_centralStack->addWidget(buildHomePage());
    m_centralStack->addWidget(m_deps.hostKind == WorkbenchHostKind::EmbeddedDock
                                  ? buildHostYieldPage()
                                  : createView3DPlaceholder(m_centralStack));
}

QWidget* WorkbenchContentImpl::buildHomePage()
{
    // 无项目首页（PM-10：新建/打开/最近项目三入口＋项目状态摘要）。
    auto* home = new QWidget(m_centralStack);
    home->setObjectName("ird_homepage");
    auto* layout = new QVBoxLayout(home);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* title = new QLabel(u8"工程工作台", home);
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);
    auto* subtitle = new QLabel(u8"未打开项目——从下方入口开始", home);
    subtitle->setAlignment(Qt::AlignHCenter);
    layout->addWidget(subtitle);

    // 入口一/二：新建、打开（点击经壳层提交路径路由 project.new/open；
    // 编排归装配层——§11.5，宿主插件的会话入口覆写即落在这条路由的终点）。
    auto* entryRow = new QHBoxLayout();
    auto* newButton = new QPushButton(u8"新建项目", home);
    newButton->setObjectName("ird_home_new");
    QObject::connect(newButton, &QPushButton::clicked, home,
            [this] { submitCommand("project.new"); });
    auto* openButton = new QPushButton(u8"打开项目", home);
    openButton->setObjectName("ird_home_open");
    QObject::connect(openButton, &QPushButton::clicked, home,
            [this] { submitCommand("project.open"); });
    entryRow->addStretch(1);
    entryRow->addWidget(newButton);
    entryRow->addWidget(openButton);
    entryRow->addStretch(1);
    layout->addLayout(entryRow);

    // 入口三：最近项目（PM-10：上限/去重/失效保留＋"项目位置不可用"提示
    // ＋重新选择＋移除）。数据源＝RecentProjectsModel（用户级设置承载）。
    auto* recentGroup = new QWidget(home);
    recentGroup->setObjectName("ird_home_recent");
    auto* recentLayout = new QVBoxLayout(recentGroup);
    recentLayout->addWidget(new QLabel(u8"最近项目", recentGroup));
    m_homeRecentList = new QListWidget(recentGroup);
    m_homeRecentList->setObjectName("ird_home_recent_list");
    // 可用项点击＝打开入口（重新打开该项目）；失效项仅右键操作（重新选择
    // ／移除）——双击不触发（PM-10 失效项提示语义）。
    QObject::connect(m_homeRecentList, &QListWidget::itemClicked, m_homeRecentList,
            [this](QListWidgetItem* item) {
                if (!item->data(Qt::UserRole).toBool()) {
                    submitCommand("project.open");  // 可用项：打开该项目的入口
                }
            });
    // 右键菜单：重新选择／移除（对失效项与可用项均可移除——PM-10 原文）。
    m_homeRecentList->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(m_homeRecentList, &QWidget::customContextMenuRequested, m_homeRecentList,
            [this](const QPoint& pos) {
                QListWidgetItem* item = m_homeRecentList->itemAt(pos);
                if (!item) {
                    return;  // 空白处无上下文菜单
                }
                QMenu menu(m_homeRecentList);
                menu.addAction(u8"重新选择", m_homeRecentList, [this] {
                    submitCommand("project.open");  // 重新选择＝打开流（重选位置）
                });
                menu.addAction(u8"移除", m_homeRecentList, [this, item] {
                    removeRecentProject(item->toolTip().toStdString());
                });
                menu.exec(m_homeRecentList->mapToGlobal(pos));
            });
    recentLayout->addWidget(m_homeRecentList);
    layout->addWidget(recentGroup);

    // 项目状态摘要（PM-10 首页要素之一）：无项目态的摘要说明（打开项目后
    // 由后续任务接入真实摘要投影——阶段 A 不虚构数据）。
    auto* summary = new QLabel(u8"项目状态摘要：打开项目后在此显示", home);
    summary->setAlignment(Qt::AlignHCenter);
    layout->addWidget(summary);

    layout->addStretch(1);
    return home;
}

QWidget* WorkbenchContentImpl::buildHostYieldPage()
{
    // 嵌入宿主的三维让位页（O-38 裁决③）：三维视图本体由宿主主窗口承载
    // （框架 RWStudioView3D 中央视图与工作台 Dock 共存），本页只声明让位
    // 事实。红线：纯 QLabel 呈现、零按钮/输入等可交互控件——不虚构能力、
    // 不重复宿主的三维功能（§11.4；完整三维交互归 WP-10-T05 阶段 B）。
    auto* page = new QWidget(m_centralStack);
    page->setObjectName("ird_view3d_host_yield");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* title = new QLabel(u8"三维视图", page);
    title->setObjectName("ird_view3d_host_yield_title");
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);
    auto* notice = new QLabel(
        u8"三维视图由宿主主窗口承载（框架中央视图与本工作台面板共存）。\n"
          "在本工作台内打开项目后，阶段面板将在此区域呈现。",
        page);
    notice->setObjectName("ird_view3d_host_yield_notice");
    notice->setAlignment(Qt::AlignHCenter);
    layout->addWidget(notice);

    layout->addStretch(1);
    return page;
}

// =====================================================================
// 命令设施装配（UI-T06——§7.1 最小命令集＋§7.3 默认绑定＋§7.4 面板）
// =====================================================================

namespace {

/// §7.1 最小命令集的登记行（表行的编译期形态——行序＝§7.1 表行序，即
/// registrationOrder 的稳定排序锚，NFR-COR-02 界面延伸）。
struct ShellCommandRow {
    const char* id;              ///< 命令 id（点分小写——§7.1 语法）
    const char* titleKey;        ///< 标题文案键（cmd.<id>.title——§3.5）
    const char* keywordKeys[2];  ///< 关键字键（cmd.<id>.kw.<n>——UX-13；不足以 nullptr 填充）
    CommandCategory category;    ///< 分类（面板分组轴）
    ShellCommandScope scope;     ///< 作用域（§7.1 CommandScope）
    bool readOnlyAllowed;        ///< 只读会话是否可用（§7.6）
    const char* defaultShortcut; ///< 默认键（nullptr＝无默认——面板可达，UX-13 兜底）
    const char* menuPath;        ///< 菜单与面板分组（"文件/新建"）
};

/// §7.1 表逐行（readOnlyAllowed/默认键严格按表列；归属列只影响处理器
/// 语义——阶段 A 全部命令项由壳登记，未接线处理器以占位说明反馈）。
constexpr ShellCommandRow kMinimalCommandRows[] = {
    {"project.new",     "cmd.project.new.title",     {"cmd.project.new.kw.0", "cmd.project.new.kw.1"}, CommandCategory::Project,   ShellCommandScope::Session, true,  "Ctrl+N",        "文件/新建"},
    {"project.open",    "cmd.project.open.title",    {"cmd.project.open.kw.0", nullptr},               CommandCategory::Project,   ShellCommandScope::Session, true,  "Ctrl+O",        "文件/打开"},
    {"draft.save",      "cmd.draft.save.title",      {"cmd.draft.save.kw.0", nullptr},                 CommandCategory::Edit,      ShellCommandScope::Project, false, "Ctrl+S",        "文件/保存草稿"},
    {"draft.apply",     "cmd.draft.apply.title",     {"cmd.draft.apply.kw.0", nullptr},                CommandCategory::Edit,      ShellCommandScope::Project, false, "Ctrl+Return",   "文件/应用修改"},
    {"project.undo",    "cmd.project.undo.title",    {"cmd.project.undo.kw.0", nullptr},               CommandCategory::Edit,      ShellCommandScope::Project, false, "Ctrl+Z",        "编辑/撤销"},
    {"project.redo",    "cmd.project.redo.title",    {"cmd.project.redo.kw.0", nullptr},               CommandCategory::Edit,      ShellCommandScope::Project, false, "Ctrl+Y",        "编辑/重做"},
    {"scheme.switch",   "cmd.scheme.switch.title",   {"cmd.scheme.switch.kw.0", nullptr},              CommandCategory::Stage,     ShellCommandScope::Project, false, nullptr,         "阶段/切换方案"},
    {"project.saveAs",  "cmd.project.saveAs.title",  {"cmd.project.saveAs.kw.0", nullptr},             CommandCategory::Project,   ShellCommandScope::Project, false, nullptr,         "文件/项目另存为"},
    {"package.export",  "cmd.package.export.title",  {"cmd.package.export.kw.0", nullptr},             CommandCategory::Project,   ShellCommandScope::Project, false, nullptr,         "文件/导出评估包"},
    {"report.export",   "cmd.report.export.title",   {"cmd.report.export.kw.0", nullptr},              CommandCategory::Report,    ShellCommandScope::Project, true,  nullptr,         "文件/导出报告"},
    {"analysis.collisionCheck", "cmd.analysis.collisionCheck.title", {"cmd.analysis.collisionCheck.kw.0", "cmd.analysis.collisionCheck.kw.1"}, CommandCategory::Analysis, ShellCommandScope::Project, false, nullptr, "工具/碰撞检查"},
    {"view.displayMode", "cmd.view.displayMode.title", {"cmd.view.displayMode.kw.0", nullptr},         CommandCategory::View,      ShellCommandScope::View,    true,  nullptr,         "视图/显示模式"},
    {"view.resetHome",  "cmd.view.resetHome.title",  {"cmd.view.resetHome.kw.0", "cmd.view.resetHome.kw.1"}, CommandCategory::View, ShellCommandScope::View,    true,  nullptr,         "视图/复位到 home 位"},
    {"view.resetZero",  "cmd.view.resetZero.title",  {"cmd.view.resetZero.kw.0", "cmd.view.resetZero.kw.1"}, CommandCategory::View, ShellCommandScope::View,    true,  nullptr,         "视图/复位到零位"},
    {"workbench.commandPalette", "cmd.workbench.commandPalette.title", {"cmd.workbench.commandPalette.kw.0", "cmd.workbench.commandPalette.kw.1"}, CommandCategory::Workbench, ShellCommandScope::Session, true, "Ctrl+Shift+P", "工具/命令面板"},
    {"workbench.closeProject",   "cmd.workbench.closeProject.title",   {"cmd.workbench.closeProject.kw.0", nullptr}, CommandCategory::Workbench, ShellCommandScope::Project, true,  "Ctrl+W",        "文件/关闭项目"},
    {"view.resetLayout", "cmd.view.resetLayout.title", {"cmd.view.resetLayout.kw.0", nullptr},         CommandCategory::View,      ShellCommandScope::View,    true,  nullptr,         "视图/恢复默认布局"},
    {"help.about",      "cmd.help.about.title",      {"cmd.help.about.kw.0", nullptr},                 CommandCategory::Help,      ShellCommandScope::Session, true,  nullptr,         "帮助/关于"},
    {"help.contents",   "cmd.help.contents.title",   {"cmd.help.contents.kw.0", nullptr},              CommandCategory::Help,      ShellCommandScope::Session, true,  "F1",            "帮助/帮助手册"},
};

}  // namespace

void WorkbenchContentImpl::assembleCommandSystem()
{
    // 命令注册表（§10.3）：白名单＝壳层设施（"ui"——SA-01 静态白名单的
    // 命令侧；插件命令经 IPluginUiRegistrar 随装配任务增量接入）。诊断面
    // 按可空成员纪律接线：devLog 恒转接（Dev 码出线）；diagFactory/diagSink
    // 为 UI-T06 wiring 增量（L5 注入工厂时用户级码经 create 唯一入口入目
    // 录——§9.2；装配层未注入＝无目录场景，拒绝语义由返回值与状态行反馈
    // 承载——登记 ui.md §10.1 v0.8）。
    CommandRegistryDeps commandDeps;
    commandDeps.diagFactory = m_deps.wiring.diagFactory;
    commandDeps.diagSink = m_deps.wiring.diagSink;
    commandDeps.devLog = m_deps.wiring.devLog;
    commandDeps.ownerWhitelist = {"ui"};
    m_commands = createCommandRegistry(std::move(commandDeps));

    // §7.1 最小命令集登记（默认谓词＝作用域/只读规则——与壳门控快照同源，
    // §7.5/§7.6；默认谓词内零 IO）。处理器按归属接线：
    //   - 壳自持语义（视图复位/面板打开）＝完整实现；
    //   - project.new/project.open/draft.save/workbench.closeProject＝装配层
    //     覆写面（§11.5 触发时机编排归装配层——宿主插件注入真实编排后，五处
    //     入口同走真实协议；未注入＝§7.1 阶段 A 占位说明处理器，harness 形态
    //     不变）；
    //   - 其余归属 workflow/project/io/reporting 的命令＝占位说明处理器
    //     （§11.4/§14.1 契约显式的阶段 A 形态——不虚构业务能力，接线随
    //     归属任务落地；非空实现：触发有状态行反馈且进入近期使用）。
    for (const ShellCommandRow& row : kMinimalCommandRows) {
        CommandDescriptor desc;
        desc.id = row.id;
        desc.ownerUnit = "ui";
        desc.titleKey = row.titleKey;
        for (const char* keywordKey : row.keywordKeys) {
            if (keywordKey != nullptr) {
                desc.keywordKeys.emplace_back(keywordKey);  // 定长数组到投影行（nullptr 填充段跳过）
            }
        }
        desc.category = row.category;
        desc.scope = row.scope;
        desc.readOnlyAllowed = row.readOnlyAllowed;
        desc.bindable = true;
        if (row.defaultShortcut != nullptr) {
            desc.defaultShortcut = QKeySequence(QString::fromLatin1(row.defaultShortcut));
        }
        desc.menuPath = row.menuPath;

        ICommandRegistry::CommandHandler handler;
        if (std::string_view(row.id) == "view.resetLayout") {
            // 壳自持：恢复出厂位形（§4.5）＋即时反馈。
            handler = [this](const std::vector<CommandParameter>&) {
                resetLayout();
                showStatusFeedback(u8"已恢复默认布局", 4000);
                CommandOutcome out;
                out.accepted = true;
                return out;
            };
        } else if (std::string_view(row.id) == "workbench.commandPalette") {
            // 壳自持：打开命令面板（§7.4——UX-13 的键盘可达入口）。
            handler = [this](const std::vector<CommandParameter>&) {
                openCommandPalette();
                CommandOutcome out;
                out.accepted = true;
                return out;
            };
        } else if (std::string_view(row.id) == "help.about") {
            // 壳自持：打开关于对话框（UI-T10——§11.4 关于页＝白名单∩报告
            // ＋版本基线，数据经 IUiAboutDataSource 端口现取）。
            handler = [this](const std::vector<CommandParameter>&) {
                openAboutDialog();
                CommandOutcome out;
                out.accepted = true;
                return out;
            };
        } else if (std::string_view(row.id) == "help.contents") {
            // 壳自持：打开用户手册（UI-T10——§11.4 帮助入口链接用户手册；
            // 反馈见 openUserManualEntry——成功/缺失二态均用户可见）。
            handler = [this](const std::vector<CommandParameter>&) {
                openUserManualEntry();
                CommandOutcome out;
                out.accepted = true;
                return out;
            };
        } else if (std::string_view(row.id) == "project.new"
                   && m_deps.newProjectHandler.has_value()) {
            // 装配层覆写（§11.5）：宿主插件的新建编排（真实协议——描述符/
            // 谓词/登记序零变化，覆写只发生在装配期登记之前）。
            handler = m_deps.newProjectHandler.value();
        } else if (std::string_view(row.id) == "project.open"
                   && m_deps.openProjectHandler.has_value()) {
            // 装配层覆写（§11.5）：宿主插件的打开编排（同上）。
            handler = m_deps.openProjectHandler.value();
        } else if (std::string_view(row.id) == "draft.save"
                   && m_deps.saveProjectHandler.has_value()) {
            // 装配层覆写（§11.5 同型——UI-T17）：宿主插件的保存编排
            // （DraftController saveAll 真实落盘链路；未注入＝阶段 A 占位）。
            handler = m_deps.saveProjectHandler.value();
        } else if (std::string_view(row.id) == "workbench.closeProject"
                   && m_deps.closeProjectHandler.has_value()) {
            // 装配层覆写（§11.5 同型——UI-T17）：宿主插件的关闭编排
            // （§5.4 统一确认对话框＋Draining 防线驱动；未注入＝占位）。
            handler = m_deps.closeProjectHandler.value();
        } else {
            // 占位说明处理器（阶段 A 契约显式形态——accepted=true：提交
            // 链路真实走通，能力面以 §11.4 说明呈现）。
            handler = [this](const std::vector<CommandParameter>&) {
                showStatusFeedback(
                    QString::fromUtf8(WorkbenchText::kEntryDeferredNotice), 4000);
                CommandOutcome out;
                out.accepted = true;
                out.messageKey = std::string{"notice.entry-deferred"};
                return out;
            };
        }
        const RegistrationResult result = m_commands->registerCommand(desc, handler);
        Q_ASSERT(result == RegistrationResult::Ok
                 && "§7.1 最小命令集登记被拒（表内冲突＝装配 bug）");
        (void)result;
    }

    // 装配收口（§7.2——运行期只读；此后 registerCommand 拒绝）。
    m_commands->seal();

    // 全局快捷键表（§10.4/§7.3）：默认集登记（§7.1 默认键列——Default 随
    // 产品版本冻结）＋用户改绑持久化回调（PM-14——写盘经后台线程）。
    GlobalShortcutRegistryDeps shortcutDeps;
    shortcutDeps.commands = m_commands.get();
    shortcutDeps.diagFactory = m_deps.wiring.diagFactory;
    shortcutDeps.diagSink = m_deps.wiring.diagSink;
    shortcutDeps.devLog = m_deps.wiring.devLog;
    shortcutDeps.onUserBindingsChanged =
        [this](const std::vector<HotkeyBinding>&) { persistShortcutsAsync(); };
    m_shortcuts = createGlobalShortcutRegistry(std::move(shortcutDeps));
    for (const ShellCommandRow& row : kMinimalCommandRows) {
        if (row.defaultShortcut != nullptr) {
            const HotkeyResult r = m_shortcuts->registerDefault(
                row.id, QKeySequence(QString::fromLatin1(row.defaultShortcut)));
            Q_ASSERT(r.isOk() && "§7.1 默认键注册被拒（表内冲突＝装配 bug）");
            (void)r;
        }
    }
}

void WorkbenchContentImpl::applyShortcutAndPaletteState()
{
    // ①用户改绑回放（PM-14 持久化半区——冲突/失效条目由注册表丢弃＋Dev
    //   日志，不阻塞启动；§4.5 坏一条丢一条的快捷键侧纪律）。
    m_shortcuts->restoreUserBindings(m_pendingUserBindings);
    m_pendingUserBindings.clear();
    // ②QShortcut attach（§7.3 唯一创建点——宿主＝宿主控件，WindowShortcut
    //   上下文；两种宿主形态下快捷键都在宿主窗口激活范围内生效）。
    m_shortcuts->attach(m_deps.hostWidget);
    // ③命令面板（§7.4——面板是注册表的只读投影；父子归宿主控件 Qt 树）。
    m_palette = createCommandPalette(m_commands.get(), m_shortcuts.get(), m_deps.hostWidget);
    // ④面板近期使用回放（§7.4 置顶分组的持久化半区——未注册 id 丢弃）。
    m_commands->restoreRecentUsed(m_pendingPaletteRecent);
    m_pendingPaletteRecent.clear();
}

void WorkbenchContentImpl::openCommandPalette()
{
    if (m_palette != nullptr) {
        m_palette->open();  // 打开即构建快照（§7.4"打开时构建快照"）
    }
}

// =====================================================================
// 帮助入口（UI-T10——§11.4 帮助入口与关于对话框）
// =====================================================================

void WorkbenchContentImpl::openAboutDialog()
{
    if (!m_built || m_shutdownDone) {
        Q_ASSERT(false && "shutdown 后调用 openAboutDialog（契约非法调用）");
        return;
    }
    // 第 1 步：现取端口数据（§11.4 数据＝白名单 ∩ 报告＋版本基线；端口
    // 为空＝无关于数据测试场景——wiring 显式声明语义，清单/版本双占位，
    // 零虚构数据）。报告与基线各取一次快照，对话框呈现该快照（打开期间
    // 不订阅不刷新——关于框是装配期事实的静态呈现面）。
    const std::vector<PluginAssemblyReport> reports =
        m_deps.wiring.aboutSource != nullptr ? m_deps.wiring.aboutSource->assemblyReports()
                                             : std::vector<PluginAssemblyReport>{};
    const AboutVersionBaseline baseline =
        m_deps.wiring.aboutSource != nullptr ? m_deps.wiring.aboutSource->versionBaseline()
                                             : AboutVersionBaseline{};

    // 第 2 步：模型装配＋对话框构建（同源纪律——清单/版本行全部出自
    // AboutDialog.hpp 的装配函数，本层零过滤零加工逻辑；父窗口＝宿主控件
    // ——顶层形态下即主窗口，与原实现一致）。
    QDialog* dialog = createAboutDialog(baseline, aboutPluginRows(reports),
                                        m_deps.hostWidget);

    // 第 3 步：非阻塞打开（QDialog::open＝窗口模态呈现、不进嵌套事件
    // 循环——§7.7 提交时序不可重入；对话框生命周期交 Qt 父子树，随宿主
    // 窗口销毁或用户关闭即回收）。
    dialog->open();
}

void WorkbenchContentImpl::openUserManualEntry()
{
    if (!m_built || m_shutdownDone) {
        Q_ASSERT(false && "shutdown 后调用 openUserManualEntry（契约非法调用）");
        return;
    }
    // 打开动作（§11.4"链接用户手册"语义本体在 openUserManual——存在才
    // 启动系统打开器）；本方法只承载二态的用户可见反馈（入口点了没反应
    // 属可见性违例）。反馈走状态行（即时、非阻断）；缺失的解析路径细节
    // 走 Dev 日志（排障面——§3.5 码表无此事件码，不臆造稳定码；呈现面
    // 零内部路径）。
    if (openUserManual()) {
        showStatusFeedback(
            QString::fromUtf8(WorkbenchText::kHelpManualOpenedNotice), 4000);
        return;
    }
    emitDev("用户手册入口文件缺失（share 帮助文件未部署）：" + userManualPath());
    showStatusFeedback(
        QString::fromUtf8(WorkbenchText::kHelpManualMissingNotice), 6000);
}

std::vector<HotkeyBinding>
WorkbenchContentImpl::loadUserShortcutBindings(QSettings& settings)
{
    // 载荷读取（UI 线程、写线程未启动——读写不重叠）：QStringList 每条
    // "commandId\t键 PortableText"；格式不符的条目丢弃（损坏一条不炸整表
    // ——§4.5 同纪律；来源标记统一 User——载荷只承载 command+key 事实）。
    std::vector<HotkeyBinding> out;
    const QStringList stored =
        settings.value(QString(kShortcutGroup) + '/' + kShortcutKey).toStringList();
    for (const QString& line : stored) {
        const int sep = line.indexOf(QLatin1Char('\t'));
        if (sep <= 0) {
            continue;  // 缺命令段（损坏条目——丢弃）
        }
        HotkeyBinding binding;
        binding.command = line.left(sep).toStdString();
        binding.key = QKeySequence(line.mid(sep + 1));
        binding.origin = BindingOrigin::User;
        if (!binding.key.isEmpty()) {
            out.push_back(std::move(binding));  // 空键＝损坏条目（丢弃）
        }
    }
    return out;
}

std::vector<CommandId> WorkbenchContentImpl::loadPaletteRecent(QSettings& settings)
{
    // 近期使用读取（§7.4"用户级持久化最近 20 条"——QStringList 最新在前；
    // 未注册 id 由 restoreRecentUsed 收敛丢弃）。
    QStringList stored =
        settings.value(QString(kPaletteRecentGroup) + '/' + kPaletteRecentKey).toStringList();
    std::vector<CommandId> out;
    out.reserve(static_cast<std::size_t>(stored.size()));
    for (const QString& id : stored) {
        out.push_back(id.toStdString());
    }
    return out;
}

// =====================================================================
// 上下文投影注入与呈现刷新（PM-10/PM-11/PM-07/§7.5）
// =====================================================================

void WorkbenchContentImpl::presentProjectContext(const ProjectContextProjection& context)
{
    if (!m_built || m_shutdownDone) {
        Q_ASSERT(false && "shutdown 后调用 presentProjectContext（契约非法调用）");
        return;
    }
    m_context = context;
    // 门控快照（§7.5 UiContextSnapshot）一次性刷新并推送注册表——命令可用
    // 性求值只看这份快照（谓词内零端口查询/IO，NFR-PERF-01）；状态行/徽标
    // 消费同一份（单一口径）。注册表未装配（build 装配序保证先于本调用）
    // 防御性跳过推送。
    m_gate.hasActiveProject = context.project.has_value();
    m_gate.writable = context.project.has_value() && context.project->writable;
    m_gate.drafts = context.drafts;
    if (m_commands) {
        m_commands->presentContext(m_gate);
    }

    // 中央区切换：无项目→首页（PM-10）；有项目→三维视图区域页（顶层＝
    // 占位面板/嵌入式＝让位页——宿主形态在 build 时已定页，此处只切索引）。
    m_centralStack->setCurrentIndex(m_gate.hasActiveProject ? 1 : 0);
    refreshStatusBar();
    refreshCommandStates();
    // 策略摘要卡随上下文注入重拉端口快照（UI-T07——§6.7；阶段 A 的刷新
    // 锚点＝本唯一上下文入口，事件驱动刷新随投影管线 §6.1/UI-T09 接入）。
    refreshPolicySummaryCard();
}

void WorkbenchContentImpl::refreshPolicySummaryCard()
{
    // 卡未装配（build 装配序之前的 presentProjectContext 首调不可能——
    // activate 序保证；已 shutdown＝钩子置空）＝空操作。
    if (m_refreshPolicyCard) {
        m_refreshPolicyCard();
    }
}

void WorkbenchContentImpl::refreshStatusBar()
{
    // PM-11 唯一权威＝formatProjectStatusText（UiProjections.hpp）——标题
    // 与状态行同格式（PM-11"标题栏与状态栏"双面）：UI-T18 起两半都经观察
    // 者回调宿主层（状态文本→宿主状态栏永久位；标题→顶层宿主 setWindow
    // Title；不注册＝无投影——无宿主测试场景，本层零 Widget）。
    const QString text = QString::fromStdString(formatProjectStatusText(m_context));
    if (m_statusTextObserver) {
        m_statusTextObserver(text);
    }
    if (m_titleTextObserver) {
        m_titleTextObserver(text);
    }
    // 只读徽标（PM-07）：仅项目态且不可写时呈现。
    if (m_readonlyBadge) {
        m_readonlyBadge->setVisible(m_gate.hasActiveProject && !m_gate.writable);
    }
}

void WorkbenchContentImpl::refreshCommandStates()
{
    if (!m_built || !m_commands) {
        return;  // 内容未建/命令设施未装配（build 装配序保证二者先于本调用）
    }
    // 顶栏按钮使能态＝命令注册表求值结果（§7.6"三处一致禁用"——菜单/
    // 顶栏/面板同源单一口径，无第二套求值；菜单半区经观察者由宿主层同步
    // ——本层零菜单语义）。
    for (auto& [button, commandId] : m_topButtons) {
        const CommandAvailability a = m_commands->availability(commandId);
        button->setEnabled(a.enabled);
        // 禁用原因随按钮 tooltip 呈现（§7.4"禁用＋说明"——发现性保留）。
        button->setToolTip(a.enabled
                               ? QString()
                               : QString::fromUtf8(a.disableReasonKey == WorkbenchText::kReasonReadOnly
                                                       ? WorkbenchText::kReasonReadOnlyText
                                                       : WorkbenchText::kReasonNoProjectText));
    }
    // 宿主层 chrome 同步（菜单动作/视图开关勾选态——未注册＝无 chrome，
    // 纯内容场景同样成立）。
    if (m_commandStateObserver) {
        m_commandStateObserver();
    }
}

void WorkbenchContentImpl::submitCommand(const std::string& commandId)
{
    if (!m_built || m_shutdownDone) {
        Q_ASSERT(false && "shutdown 后调用 submitCommand（契约非法调用）");
        return;
    }
    // 统一提交路径（§7.7/§10.4 副作用行）：菜单/顶栏/首页/快捷键/面板全部
    // 收口 registry.submit——前置校验（未知/不可执行/只读）与诊断出线在
    // 注册表内完成（UI-CMD-UNKNOWN/UI-CMD-NOT-EXECUTABLE），处理器在
    // registerCommand 时绑定。本路径只做拒绝的用户可见反馈（状态行说明）。
    const CommandOutcome outcome = m_commands->submit(commandId);
    if (outcome.accepted) {
        return;  // 处理器自行反馈（会话命令的状态行说明/面板打开等）
    }
    // 拒绝反馈（§7.4"禁用＋说明"）：未注册与不可执行分别呈现——诊断条目
    // （工厂注入时）已由注册表出线，此处是即时可见性补偿。
    const CommandAvailability a = m_commands->availability(commandId);
    if (!a.registered) {
        showStatusFeedback(
            QString::fromUtf8(u8"未知命令：") + QString::fromStdString(commandId), 4000);
        return;
    }
    showStatusFeedback(
        QString::fromUtf8(a.disableReasonKey == WorkbenchText::kReasonReadOnly
                              ? WorkbenchText::kReasonReadOnlyText
                              : WorkbenchText::kReasonNoProjectText),
        4000);
}

// =====================================================================
// 最近项目（PM-10/PM-14）
// =====================================================================

std::vector<RecentProjectEntry> WorkbenchContentImpl::recentProjects() const
{
    std::vector<RecentProjectEntry> out;
    out.reserve(m_recent.entries().size());
    for (const auto& path : m_recent.entries()) {
        RecentProjectEntry entry;
        entry.canonicalPath = path;
        // 可用性按当前文件系统事实计算（失效≠删除——PM-10 失效保留）。
        entry.available = QFileInfo::exists(QString::fromStdString(path));
        out.push_back(std::move(entry));
    }
    return out;
}

void WorkbenchContentImpl::noteRecentProject(const std::string& canonicalPath)
{
    // 入口即规范化：非规范输入收敛到去重键口径（PM-10"按规范路径去重"）。
    m_recent.note(RecentProjectsModel::canonicalize(canonicalPath));
    persistRecentAsync();
    refreshRecentList();
}

void WorkbenchContentImpl::removeRecentProject(const std::string& canonicalPath)
{
    m_recent.remove(RecentProjectsModel::canonicalize(canonicalPath));
    persistRecentAsync();
    refreshRecentList();
}

void WorkbenchContentImpl::showStatusFeedback(const QString& message, int timeoutMs)
{
    // 瞬态消息唯一出线（UI-T18）：原 QStatusBar::showMessage 的语义投影——
    // 文本与超时 [单位 ms] 原样移交宿主层；未注册观察者＝无宿主呈现面，
    // 静默丢弃（与 setTitleTextObserver 的"嵌入式可不注册"同纪律；调用方
    // 不判空——本方法即判空收口点）。
    if (m_statusMessageObserver) {
        m_statusMessageObserver(message, timeoutMs);
    }
}

void WorkbenchContentImpl::refreshRecentList()
{
    if (!m_homeRecentList) {
        return;  // shutdown 后/构建前——无列表可刷
    }
    m_homeRecentList->clear();
    for (const RecentProjectEntry& entry : recentProjects()) {
        const QString path = QString::fromStdString(entry.canonicalPath);
        auto* item = new QListWidgetItem(m_homeRecentList);
        if (entry.available) {
            item->setText(path);
        } else {
            // 失效项保留＋"项目位置不可用"提示（PM-10 原文措辞）。
            item->setText(path + QString::fromUtf8(WorkbenchText::kRecentUnavailableSuffix));
            item->setForeground(Qt::gray);
        }
        item->setToolTip(path);  // 完整路径承载于 tooltip（移除操作的键）
        item->setData(Qt::UserRole, entry.available);  // 点击判别的可用位
        m_homeRecentList->addItem(item);
    }
}

// =====================================================================
// 门面出口与辅助
// =====================================================================

ShellCommandAvailability WorkbenchContentImpl::commandAvailability(
    const std::string& commandId) const
{
    // 门面映射：注册表可用性快照（§10.3）→ 壳层可用性词表（UI-T03 门面
    // 形态不变——观测方零迁移；registered 位与注册表同源）。
    const CommandAvailability a = m_commands ? m_commands->availability(commandId)
                                             : CommandAvailability{};
    ShellCommandAvailability out;
    out.registered = a.registered;
    out.visible = a.visible;
    out.enabled = a.enabled;
    out.reasonKey = a.disableReasonKey;
    return out;
}

void WorkbenchContentImpl::setCommandStateObserver(std::function<void()> observer)
{
    m_commandStateObserver = std::move(observer);
}

void WorkbenchContentImpl::setTitleTextObserver(
    std::function<void(const QString&)> observer)
{
    m_titleTextObserver = std::move(observer);
}

void WorkbenchContentImpl::emitDev(const std::string& message)
{
    // Dev 码唯一出线＝开发日志通道（diagnostics §6.2"Dev 走日志"）；为空
    // ＝wiring 显式声明的无日志场景——恢复行为不依赖日志可用性（§4.5）。
    if (m_deps.wiring.devLog) {
        m_deps.wiring.devLog->logDev(kUiDevChannel, message);
    }
}

bool WorkbenchContentImpl::shutdown()
{
    // 幂等（契约：重复调用为空操作；返回末次落盘结论）。
    if (m_shutdownDone) {
        return m_layoutPersisted;
    }
    if (!m_built || !m_activated) {
        // 未完成装配即拆卸（build 前析构等）：无布局可落盘——按"完整走完
        // 的空拆卸"处理（不谎报落盘成功）。
        m_shutdownDone = true;
        m_layoutPersisted = false;
        return m_layoutPersisted;
    }

    // ①布局/快捷键/面板近期落盘：值序列化在 UI 线程（Widget 尚存活），写盘
    //   交给后台线程，随后有界收口（"有界拆除"——队列里全是微秒级写
    //   任务，join 有界）。快捷键 User 绑定与面板近期使用随关停补一次落盘
    //   （改绑/使用路径已有即时落盘——此处是关停位的兜底快照，PM-14）。
    persistLayoutAsync();
    persistShortcutsAsync();
    persistPaletteRecentAsync();
    m_layoutPersisted = m_settingsWriter.drainAndStop();

    // ②设施停用（§10.1 后置语义的内容半区：宿主层销毁窗口发生在本调用
    //   返回之后——Widget 指针随窗口树消亡，此处全部置空防悬垂）。
    m_shortcuts->detach();  // QShortcut 唯一创建点的对称拆卸（物理键随窗口树销毁——此处清表）
    m_palette = nullptr;    // 面板为宿主控件树子——随宿主销毁（指针置空防悬垂）
    m_commands.reset();     // 注册表/快捷键表在拆卸后停用（处理器只经本层触发）
    m_shortcuts.reset();
    m_centralStack = nullptr;
    m_readonlyBadge = nullptr;
    m_homeRecentList = nullptr;
    m_regionWidgets.fill(nullptr);
    m_regionTargets.fill(nullptr);
    m_topButtons.clear();
    // 策略摘要卡随右栏窗口树销毁（UI-T07）——刷新钩子捕获卡容器指针与
    // wiring 端口引用，窗口销毁后一并失效：此处清空钩子防悬垂调用。
    m_refreshPolicyCard = nullptr;
    m_commandStateObserver = nullptr;
    m_titleTextObserver = nullptr;

    m_shutdownDone = true;
    return m_layoutPersisted;
}

}  // namespace detail

// =====================================================================
// 装配入口（WorkbenchContent.hpp 工厂声明的实现）
// =====================================================================

std::unique_ptr<IWorkbenchContent> createWorkbenchContent(WorkbenchContentDeps deps)
{
    return std::make_unique<detail::WorkbenchContentImpl>(std::move(deps));
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
