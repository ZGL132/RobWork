/**
 * @file DockWorkspace_p.hpp
 * @brief UI-T82：宿主业务工作区的停靠拓扑、滚动边界与显隐事件解释。
 *
 * 设计依据：units/ui.md §4.4/§4.5、UX-09、PM-14。
 * 六域最小高度纵向相加可超过物理屏幕；窗口最大化受屏幕约束，不能靠
 * 最小窗口高度保证子件可用。因此把域放入同一标签组，并用视口隔离内容
 * 最小尺寸和 Dock 最小尺寸。纯 UI 装配，不读业务控件、不改变草稿。
 * 无共享可变状态；所有函数仅在 Qt UI 线程调用；控件归 Qt 父子树持有。
 */
#ifndef SDURWS_IRD_UI_DOCKWORKSPACE_P_HPP
#define SDURWS_IRD_UI_DOCKWORKSPACE_P_HPP

#include <QDockWidget>
#include <QMainWindow>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QVector>

namespace sdurws::ird::ui::detail {

/**
 * @brief 将含指定插件标题的停靠标签栏设为完整标题＋滚动，不触碰其他栏。
 * @param host [in,out] 借用的 Qt 主窗口，仅 UI 线程；不改变页签拓扑或选择。
 * @param title [in] 本插件 Dock 的完整标题；当拍栏尚未创建/尚未显示则无操作。
 * @return 无；幂等。无显式异常，不保存控件指针、不取得宿主布局所有权。
 */
inline void configureWorkspaceTabs(QMainWindow& host, const QString& title)
{
    for (QTabBar* tabs : host.findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == title) {
                tabs->setElideMode(Qt::ElideNone);
                tabs->setUsesScrollButtons(true);
                break; // 仅匹配本插件标题；每栏一次，宽度与选择仍由 Qt 管理。
            }
        }
    }
}

/**
 * @brief 内容部件最小高变化的重协商过滤器（MA-04——主线功能审计
 *        2026-10-21 的布局缺口修复面）。
 *
 * 缺陷机理（requirements-tour 几何取证）：QScrollArea::setWidgetResizable
 * 只在**视口 resize** 时按 max(视口, 内容 minimumSizeHint) 重设内容部件
 * 高。两类缺口使内容高停留在旧协商值（实测 917 对回落后的 455）：①内部
 * 状态切换（需求面板四页签当前页变化——区域页表格最小高远大于工位页）；
 * ②更隐蔽的内部状态变化（表单行随树选择收起/展开）被**内层滚动区吸收**，
 * 最小高提示改变不再产生任何到达本层的事件（事件类型实测留痕：
 * LayoutRequest 仅在 917 期间到达，最小高回落后零事件）。差值全部成为
 * 滚到底后的空白尾区：用户/取证通道滚到底只见树与表单的拉伸空白，
 * "滚动条到位≠内容可达"。
 *
 * 本过滤器在内容部件的 LayoutRequest 与 **Paint** 拍重申高度不变式：
 * 内容高 == max(视口高, 当前 minimumSizeHint)（与 Qt widgetResizable 同
 * 式）。Paint 拍自愈是确定性闭环：任何状态变化后的第一次重绘即修正
 * （resize 触发重排与重绘，二次绘制时不变式已成立，无循环）；每次重绘
 * 一次整数比较，热路径可忽略。只影响高度协商，不触碰任何业务数值、
 * 不提交草稿/修订；宽度仍交 Qt（widgetResizable 原语义）。UI 线程限定
 * （事件派发即 UI 线程）。
 */
class DockContentResizeFilter : public QObject
{
public:
    explicit DockContentResizeFilter(QScrollArea& owner)
        : m_owner(owner)
    {
    }

protected:
    /// @brief LayoutRequest/Paint 拦截：重申内容高不变式。
    /// @param watched [in] 被过滤对象（安装时绑定内容部件）。
    /// @param event [in] 事件；不消费任何事件（全部照常分发——重协商
    /// 经 resize 自然生效，不与 Qt 竞争滚动条状态）。
    /// @return QObject::eventFilter 的透传结果。
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_owner.widget() && m_owner.widgetResizable()
            && (event->type() == QEvent::LayoutRequest
                || event->type() == QEvent::Paint)) {
            QWidget* content = m_owner.widget();
            const int targetHeight = std::max(m_owner.viewport()->height(),
                                              content->minimumSizeHint().height());
            if (content->height() != targetHeight
                || content->maximumHeight() != targetHeight) {
                // 最大高夹紧（与 resize 同拍）：widgetResizable 的回写路径
                // 以内容 sizeHint 为目标时会把高度顶回旧协商值（实测
                // 917↔455 拉锯）——maximumHeight 是 Qt 硬约束，resize 钳制
                // 于 [minimumHeight, maximumHeight]，回写被确定性压制。
                // 视口增高时本拍即放宽上界，不阻断正常伸展。
                content->setMaximumHeight(std::max(targetHeight, content->minimumHeight()));
                content->resize(m_owner.viewport()->width(), targetHeight);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    /// 滚动区本体（构造绑定——比 qobject_cast 持内容指针更稳：setWidget
    /// 换内容时过滤器随旧内容失效，不悬垂）。
    QScrollArea& m_owner;
};

/**
 * @brief 给宿主 Dock 安装可滚动内容，使长表单的最小高度不传播到整个窗口。
 * @param dock [in,out] 宿主 Dock，UI 线程借用；本函数不持有其引用。
 * @param panel [in] 域工厂产物；非空时所有权交给滚动视口，空则保持空 Dock。
 * @param minWidth [in] 视口最小宽度，Qt 逻辑像素；左/底默认 240，右栏传 280。
 * @param minHeight [in] 视口最小高度，默认 120 逻辑像素；底栏传 160 保持约定。
 * @return 无；不重建 panel，不改数值或编辑状态。Qt 分配失败按运行库行为抛出。
 */
inline void installDockViewport(QDockWidget& dock, QWidget* panel,
                                int minWidth = 240, int minHeight = 120)
{
    if (panel == nullptr) { return; } // 失败隔离：没有域内容时不虚构可用空表单。
    auto* scroll = new QScrollArea(&dock);
    scroll->setObjectName(dock.objectName() + QStringLiteral("_scroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // 宽度 240/280、底栏高度 160 来自 ui.md §4.4；业务视口高度 120 是
    // UI-T82 的窄屏预算：200% 缩放后 1200px 屏仅有约 565 逻辑像素客户区，
    // 保留项目树、标签栏、底栏和菜单后，不能再让每个视口硬占 160。
    // 原表单最小尺寸不变，全部内容以滚动访问，避免标签栏被挤到任务区背后。
    scroll->setMinimumSize(minWidth, minHeight);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // MA-04：内容最小高重协商过滤器。过滤器对象以 scroll 为父（同生命
    // 周期——滚动区析构时过滤器同亡，无泄漏）；监听对象＝内容部件
    //（setWidget 后建立父子，widget() 恒非空）。
    auto* resizeFilter = new DockContentResizeFilter(*scroll);
    resizeFilter->setParent(scroll);
    scroll->setWidget(panel);
    panel->installEventFilter(resizeFilter);
    dock.setWidget(scroll);
}

/**
 * @brief 装载或复位时建立「项目树＋六域标签组」，避免所有域垂直争抢高度。
 * @param host [in,out] 宿主主窗口，仅调用 Qt 公共停靠 API，不修改框架源码。
 * @param tree [in,out] 项目树 Dock，保留独立导航区；归 host 对象树持有。
 * @param domains [in] 有序域 Dock，允许空项（单域装配失败），不接管列表所有权。
 * @param inspectors [in] 右侧检查器 Dock，默认空；同组呈现避免高级配置另占一列。
 * @return 无；保留各 Dock 显隐状态，浮动域重新入位；之后尺寸交给 Qt 布局。
 * @throws 无显式异常。仅 UI 线程；不持久化、不触碰研发模型或项目修订。
 */
inline void arrangeDomainWorkspace(QMainWindow& host, QDockWidget& tree,
                                   const QVector<QDockWidget*>& domains,
                                   const QVector<QDockWidget*>& inspectors = {})
{
    // 只在装载/用户复位时调用。缩放时由 Qt 维护标签组，不用 resize 定时器
    // 重排/抢焦点；旧会话的六域竖排几何在此一次性迁移为有界工作区。
    host.setDockOptions(host.dockOptions() | QMainWindow::AllowTabbedDocks);
    host.setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::South);
    host.setTabPosition(Qt::RightDockWidgetArea, QTabWidget::South);
    host.addDockWidget(Qt::LeftDockWidgetArea, &tree);
    tree.setFloating(false);
    QDockWidget* first = nullptr;
    for (QDockWidget* dock : domains) {
        if (dock == nullptr) { continue; } // 缺席域不打断其他域的布局。
        host.addDockWidget(Qt::LeftDockWidgetArea, dock);
        dock->setFloating(false);
        if (first == nullptr) {
            first = dock;
            host.splitDockWidget(&tree, first, Qt::Vertical);
        } else {
            host.tabifyDockWidget(first, dock);
        }
    }
    // 标签只消耗一份工作区高度，项目树保持约 1/4；Qt 会按各内容下限协商。
    // 首选尺寸非硬上限，DPI 由 Qt 逻辑像素换算，不用物理像素写死几何。
    if (first != nullptr) {
        host.resizeDocks({&tree, first}, {180, 540}, Qt::Vertical);
    }
    // 属性和运动学高级配置共用右侧检查区。否则旧布局把二者排成两列，
    // 小屏下中央保护器会把左侧业务表单永久压到最小宽度，放大后仍难使用。
    QDockWidget* firstInspector = nullptr;
    for (QDockWidget* dock : inspectors) {
        if (dock == nullptr) { continue; } // 高级面板可能未装配，属性区仍独立可用。
        host.addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->setFloating(false);
        if (firstInspector == nullptr) { firstInspector = dock; }
        else { host.tabifyDockWidget(firstInspector, dock); }
    }
    // Qt 在布局落定后创建停靠标签栏。仅匹配本插件页签，不改宿主其他页签；
    // 窄栏时滚动完整标题，避免六个标签都被压成「...」而无法辨认模块。
    QStringList titles;
    for (QDockWidget* dock : domains + inspectors) {
        if (dock != nullptr) { titles.push_back(dock->windowTitle()); }
    }
    QTimer::singleShot(0, &host, [&host, titles] {
        for (const QString& title : titles) { configureWorkspaceTabs(host, title); }
    }); // host 是定时任务接收者，宿主提前销毁会自动取消，不留悬空引用。
}

/**
 * @brief 解释 Dock 的用户打开意愿；切换标签的遮挡与显式关闭必须分开。
 * @param dock [in] 仍存活的 Dock，UI 线程只读借用。
 * @return true＝未显式 hide/close（含非当前标签）；false＝已关闭/隐藏。
 * @throws 无。不能使用 visibilityChanged 的 bool：Qt 对非当前标签也发 false。
 */
inline bool dockRequestedOpen(const QDockWidget& dock) noexcept
{
    return !dock.isHidden();
}

} // namespace sdurws::ird::ui::detail
#endif // SDURWS_IRD_UI_DOCKWORKSPACE_P_HPP
