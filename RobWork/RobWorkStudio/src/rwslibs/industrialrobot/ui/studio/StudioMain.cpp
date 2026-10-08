/**
 * @file   StudioMain.cpp
 * @brief  正式产品主程序（sdurws_ird_studio）——宿主融合形态装配路径的
 *         启动入口（WP-24-T08：方案 B.1，SA-18 D1）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/WP-24-T08.json acceptance 1~6；
 *   - B1-SPEC §2.1 v1.2 实现路径闭合：本目标是 industrialrobot/ui 内的
 *     **独立宿主包装目标**（executable）——产品 main 直接构造
 *     rws::RobWorkStudio 主窗口，链接框架 rws 公共库与 Qt；宿主官方组件
 *     （TreeView/Jog/Playback/Log）经框架公开 API（addPlugin＋静态插件
 *     对象装配）在本目标内显式装配；框架源码与框架装配文件零修改、框架
 *     侧条件装配开关不被触碰（SA-02——ird_gates SA-02 补丁核对零新增）；
 *   - ARCHITECTURE §7.12（SA-18）：RobWorkStudio 是唯一产品主窗口——方案 A
 *     顶层窗口路径（sdurws_ird_ui_app harness）在正式装配中不再可达；
 *   - WP-24-T03 T03a/T03b 装配面为基座：工业业务插件静态白名单装配、
 *     宿主 Dock 控制器簇挂载、About 插件清单全部复用
 *     IrdWorkbenchHostPlugin::initialize 的同一装配序列（本文件只做宿主
 *     构造、官方组件装配与菜单处置，零业务语义——域功能实现不属本任务，
 *     契约 acceptance 6）。
 *
 * 装配序列（acceptance 1 的骨架路径，自上而下）：
 *   启动 → ①框架运行时初始化（RobWork::init——插件与工作单元装载依赖）
 *        → ②构造 rws::RobWorkStudio 宿主主窗口（中央区 RWStudioView3D
 *          由宿主构造函数自建——D7 唯一三维视图）
 *        → ③宿主官方组件静态装配（处置表保留四项——B1-SPEC §2.1 v1.1；
 *          PropertyView/WorkcellEditorPlugin 的排除＝装配清单不含其静态
 *          装配调用＋框架侧不生成其独立动态插件产物，两判据同时成立）
 *        → ④工业装配基座静态挂载（IrdWorkbenchHostPlugin——框架 addPlugin
 *          流程回调 setRobWorkStudio→setupMenu→initialize，与开发期动态
 *          装载走完全相同的装配协议；运行时动态 Load/Unload Plugin 禁止，
 *          本目标不调用 loadPlugin/loadPluginFolder，亦不装配动态加载入口）
 *        → ⑤宿主 chrome 菜单处置（B1-SPEC §2.1"本表处置一律经正式产品的
 *          装配选择执行，不改框架装配代码"——原生 WorkCell New/Open/
 *          Save/Reload 及其最近文件、Plugins 菜单动态 Load/Unload 入口、
 *          File 工具栏原生 WorkCell 图标全部移出正式产品菜单显示面，
 *          acceptance 3/4/5；处置只经 QMainWindow 公开 API，SA-02 不变）
 *        → ⑥进入事件循环（About 插件清单随装配基座的 help.about 命令
 *          可达——清单＝装配报告现取，与装配一致，UX-14）。
 *
 * 诚实边界（骨架范围，契约 acceptance 6）：
 *   - 域功能实现不属本任务——本文件不含任何业务语义；
 *   - 不调用 loadSettingsSetupPlugins/loadSettingsWorkcell（框架 ini 的
 *     插件装载段即动态加载通道——产品装配面不提供；宿主窗口几何/状态
 *     恢复随 rwsettings.xml 的框架既有行为保留，见 RobWorkStudio 构造）；
 *   - 框架 Help/Tools 菜单的既有非 WorkCell 生命周期项（Help Contents/
 *     About/Print Colliding Frames）保留为宿主 chrome（不在处置表排除
 *     清单内；IRD 关于框经命令面板 help.about 承载装配清单）。
 *
 * 线程模型：全部装配在 main 线程（Qt UI 线程）顺序执行——与框架
 *   RobWorkStudioApp::run 的 AppRunner 同构；插件回调由 addPlugin 同步
 *   触发，§3.4 M-1 线程纪律自然成立。
 */

#include <rws/RobWorkStudio.hpp>  // 宿主主窗口（rws::RobWorkStudio——D1 唯一产品主窗口）

#include <rw/core/PropertyMap.hpp>  // rw::core::PropertyMap（宿主构造参数）
#include <rw/core/RobWork.hpp>      // rw::core::RobWork::init（框架运行时初始化）

// 宿主官方组件（B1-SPEC §2.1 处置表保留四项——静态插件对象装配）。
// 头路径经四个框架组件目标的 INTERFACE include 面（RWS_ROOT/src）可见；
// 链接目标名见 ui/CMakeLists.txt 本产品目标段（ird_gates 命中集登记面）。
#include <rwslibs/jog/Jog.hpp>              // Jog——会话姿态操作入口（D8）
#include <rwslibs/log/ShowLog.hpp>          // Log——框架与开发日志（定位注明归装配基座）
#include <rwslibs/playback/PlayBack.hpp>    // Playback——TimedStatePath 播放（D9）
#include <rwslibs/treeview/TreeView.hpp>    // TreeView——已应用 WorkCell 辅助运行时树（D4）

// 工业装配基座（WP-24-T03 T03a/T03b 装配面——控制器簇／白名单装配／About
// 接线的同一装配序列）。plugin/ 为 ui 单元私有装配面头（不跨单元暴露），
// 经产品目标的 PRIVATE include 目录（ui 目录根）解析——与 sdurws_ird_ui_plugin
// 的解析方式一致。
#include "plugin/UiPlugin.hpp"

#include <QAction>
#include <QApplication>
#ifdef RW_HAVE_GLUT
#include <GL/freeglut.h>  // GLUT 承载渲染件（RenderText 位图字体度量——F-554 链）
#endif
#include <QLibraryInfo>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QToolBar>
#include <QTranslator>

#ifdef _WIN32
#include <windows.h>
#endif

// =====================================================================
// 宿主 chrome 菜单处置（acceptance 3/4/5——装配选择，零框架修改）
// =====================================================================
namespace {

/**
 * @brief 从菜单中移除文本命中的动作（处置表的装配选择执行面）。
 *
 * 文本匹配用框架动作的构造字面量（tr 源文未翻译形态，如 "&New"）——
 * 与 RobWorkStudio::setupFileActions/setupPluginsMenu 的 addAction 字面量
 * 一一对应；不命中即不动作（处置清单是封闭清单，不泛化匹配）。
 *
 * @param menu   [in] 目标菜单（非空——调用方保证）
 * @param titles [in] 待移除动作的精确文本清单
 */
void removeMenuActions(QMenu* menu, const std::vector<QString>& titles)
{
    // 逐动作比对文本后 removeAction（动作本体由框架父对象管理，此处只
    // 摘除出菜单显示面——不 delete，避免与框架后续动作生命周期耦合）。
    const QList<QAction*> actions = menu->actions();
    for (QAction* action : actions) {
        for (const QString& title : titles) {
            if (action->text() == title) {
                menu->removeAction(action);
                break;
            }
        }
    }
}

/**
 * @brief 收拢菜单首尾与连续的分隔符（处置后的显示面清理）。
 *
 * 动作摘除后会留下孤儿/连续分隔符（如 File 菜单原生五动作＋两个分隔符
 * 全部移除后，项目子菜单与 Exit 之间可能出现双分隔符）；逐对扫描相邻
 * 动作，移除"行首分隔符、连续分隔符、行尾分隔符"中的后者。
 */
void collapseMenuSeparators(QMenu* menu)
{
    bool removed = true;
    while (removed) {
        removed = false;
        const QList<QAction*> actions = menu->actions();
        for (int i = 0; i < actions.size(); ++i) {
            const bool firstOrAfterSep = (i == 0) || actions[i - 1]->isSeparator();
            const bool lastOrBeforeSep = (i == actions.size() - 1) || actions[i + 1]->isSeparator();
            if (actions[i]->isSeparator() && (firstOrAfterSep || lastOrBeforeSep)) {
                menu->removeAction(actions[i]);
                removed = true;
                break;  // 列表已失效——重取后继续（菜单动作数为个位，成本可忽略）
            }
        }
    }
}

/**
 * @brief 执行宿主 chrome 处置（acceptance 3/4/5 的装配选择总入口）。
 *
 * 处置范围（封闭清单——超出清单的框架 chrome 一律保留，诚实边界见文件头）：
 *   ①File 菜单：原生 WorkCell 生命周期动作（New/Open/Close/Save/Reload）、
 *     Preferences、最近 WorkCell 文件条目（"N: 文件名"形态——框架
 *     updateLastFiles 在构造期自 rwsettings.xml 重建）全部移出显示面；
 *     保留工业项目子菜单（装配基座注入）与 Exit；
 *   ②Plugins 菜单：Load plugin/Unload plugin 动态装载入口移除——SA-01
 *     "正式产品不显示、不提供该入口"（B1-SPEC §2.1 处置表"运行时动态
 *     Load/Unload Plugin＝禁止"行）；插件显隐开关项保留（框架基类语义）；
 *   ③File 工具栏：原生 WorkCell 图标组随菜单语义一并移除（同一处置的
 *     工具栏半区——显示面口径不含菜单栏以外的原生 WorkCell 入口）。
 *
 * 时序契约：必须在全部 addPlugin 之后调用——装配基座的 setupMenu 在
 * addPlugin 流程内把工业项目子菜单锚插进 File 菜单，先行处置会把锚点
 * （Preferences 前分隔符）一并摘除、破坏基座的按位插入语义。
 */
void curateProductChrome(rws::RobWorkStudio* studio)
{
    QMenuBar* menuBar = studio->menuBar();

    // ---- ①File 菜单：原生 WorkCell 生命周期移出显示面 ----
    QMenu* fileMenu = nullptr;
    for (QAction* menuAction : menuBar->actions()) {
        if (QMenu* m = menuAction->menu(); m != nullptr && m->title() == QString::fromLatin1("&File")) {
            fileMenu = m;
            break;
        }
    }
    if (fileMenu != nullptr) {
        removeMenuActions(fileMenu,
                          {QString::fromLatin1("&New"), QString::fromLatin1("&Open..."),
                           QString::fromLatin1("&Close"), QString::fromLatin1("&Save"),
                           QString::fromLatin1("&Reload"), QString::fromLatin1("&Preferences")});
        // 最近 WorkCell 文件条目（updateLastFiles 形态："N: 文件名"）——
        // 原生 WorkCell Open 路径的显示半区，与五动作同批处置。
        const QList<QAction*> fileActions = fileMenu->actions();
        for (QAction* action : fileActions) {
            static const QRegularExpression recentPattern(QStringLiteral("^[0-9]+: "));
            if (recentPattern.match(action->text()).hasMatch()) {
                fileMenu->removeAction(action);
            }
        }
        collapseMenuSeparators(fileMenu);
    }

    // ---- ②Plugins 菜单：动态装载入口移除（SA-01）----
    QMenu* pluginsMenu = nullptr;
    for (QAction* menuAction : menuBar->actions()) {
        if (QMenu* m = menuAction->menu(); m != nullptr && m->title() == QString::fromLatin1("&Plugins")) {
            pluginsMenu = m;
            break;
        }
    }
    if (pluginsMenu != nullptr) {
        removeMenuActions(pluginsMenu, {QString::fromLatin1("Load plugin"),
                                        QString::fromLatin1("Unload plugin")});
        collapseMenuSeparators(pluginsMenu);
    }

    // ---- ③File 工具栏：原生 WorkCell 图标组移除 ----
    // objectName "FileToolBar"＝框架 setupFileActions 的注册名（公开
    // QObject 对象名定位，SA-02 不变——只摘除不修改框架代码）。
    if (QToolBar* fileToolBar = studio->findChild<QToolBar*>(QString::fromLatin1("FileToolBar"));
        fileToolBar != nullptr) {
        studio->removeToolBar(fileToolBar);
        fileToolBar->deleteLater();
    }
}

}  // namespace

// =====================================================================
// 产品 main（启动→静态装配→菜单处置→事件循环）
// =====================================================================

int main (int argc, char** argv)
{
    // ①框架运行时初始化（插件/装载器/日志依赖——框架 RobWorkStudioApp
    //   同款；产品无命令行选项面，按无参形态初始化）。
    rw::core::RobWork::init ();

    // ②框架资源注册（静态链接下的强制拉入：sdurws 为静态库，rcc 生成
    //   的资源初始化对象若无符号引用会被链接器丢弃——Q_INIT_RESOURCE
    //   显式引用 qInitResources_rwstudio_resources，宿主图标/中央区工具
    //   条图像资源得以注册。框架 RobWorkStudioApp::initReasource 同款）。
    Q_INIT_RESOURCE (rwstudio_resources);

    QApplication app (argc, argv);

#ifdef RW_HAVE_GLUT
    // GLUT 一次性初始化（框架 RobWorkStudioApp 同款先例——RenderText 等
    // GLUT 承载渲染件依赖 freeglut 全局态；Qt 应用不自动初始化，缺位即
    // 位图字体度量调用崩溃——所有者十步验收实录：新增工位后工位名标签
    // 构造即进程退出，宿主 dev log 与 WER c0000005 偏移在案）。
    glutInit (&argc, argv);
#endif

    // ②b Qt 标准部件文案本地化（UI-T25——界面全中文口径的补齐面）：
    //   QFileDialog/QInputDialog/QMessageBox 等标准对话框的内置按钮
    //   （OK/Cancel/Open/Save…）文案来自 Qt 自带的翻译文件，不装载时
    //   恒为英文——与产品全中文界面冲突（新建项目命名对话框"OK/Cancel"
    //   即实测实例）。经 QLibraryInfo 翻译目录解析装载 qt_zh_CN：
    //   开发机构建树解析到 Qt 安装树 translations/；staging 部署树解析
    //   到随包 translations/（windeployqt 携带）。装载失败（部署面未携带）
    //   静默降级英文——文案本地化不是启动契约，不阻断、不上抛、不弹窗
    //   （降级边界在验收记录如实登记）。
    QTranslator* qtBaseTranslator = new QTranslator (&app);
    if (qtBaseTranslator->load (
            QStringLiteral ("qt_zh_CN.qm"),
            QLibraryInfo::path (QLibraryInfo::TranslationsPath))) {
        app.installTranslator (qtBaseTranslator);
    }

    // 异常呈现面（框架 AppRunner 同构）：装配失败 fail-fast 属契约语义
    // （禁止吞错），但正式产品不应无窗崩溃——呈现错误对话框后以非零码
    // 退出，与框架 RobWorkStudioApp::run 的 catch 分支同型。
    try {
        // ③构造宿主主窗口（空 PropertyMap——产品启动面零框架选项解析；
        //   中央区 RWStudioView3D、File/Tools/Plugins/Help 菜单与 File
        //   工具栏由宿主构造函数自建，随后由菜单处置收口）。
        rw::core::PropertyMap emptyOptions;
        rws::RobWorkStudio studio (emptyOptions);

        // ④宿主官方组件静态装配（B1-SPEC §2.1 处置表保留四项；停泊区
        //   与框架 RobWorkStudioApp 的静态装配段逐一相同）：
        //   - PropertyView/WorkcellEditorPlugin **不在此装配**（排除判据
        //     第一半："正式产品装配清单不含其静态装配调用"；第二半"框架
        //     侧不生成其独立动态插件产物"由框架静态链接形态保证——
        //     RWS_USE_STATIC_LINK_PLUGINS=ON 下官方组件均为静态库，无
        //     独立动态插件产物）。
        //   - **Log 定位注明（B1-SPEC §2.1 处置表 Log 行——正式装配＋定位
        //     注明）**：ShowLog 承载的是框架日志与开发排障日志（含诊断栈
        //     Dev 出线的开发通道镜像）；正式工程的诊断权威始终是
        //     diagnostics 稳定码＋诊断目录＋用户/开发两级日志（SA-12），
        //     本面板不承担工程诊断语义——该定位同时登记于任务留痕
        //     traceability/builds/wp24-t08/README.md。
        studio.addPlugin (new rws::ShowLog (), false, Qt::BottomDockWidgetArea);
        studio.addPlugin (new rws::Jog (), false, Qt::LeftDockWidgetArea);
        studio.addPlugin (new rws::TreeView (), false, Qt::LeftDockWidgetArea);
        studio.addPlugin (new rws::PlayBack (), false, Qt::BottomDockWidgetArea);

        // ⑤工业装配基座静态挂载（WP-24-T03 装配面——addPlugin 流程同步
        //   回调 setupMenu→initialize：静态白名单八 token 登记＋建模插件
        //   挂位＋宿主 Dock 控制器簇（主 Dock＋属性/任务同级 Dock）＋
        //   宿主状态栏投影＋命令面板＋About 数据源接线，与开发期动态
        //   装载走同一装配协议；装载呈现自证由插件内 singleShot 重申）。
        //   visible=true：产品装配明示主 Dock 呈现意图（框架 addPlugin
        //   以 PluginVisible_<名> 设置项为最优先，缺省回落本实参；插件
        //   的 reassertEmbeddedPresentation 仍为最终一致性保证）。
        studio.addPlugin (new sdurws::ird::ui::IrdWorkbenchHostPlugin (), true,
                          Qt::LeftDockWidgetArea);

        // ⑥宿主 chrome 菜单处置（全部 addPlugin 之后——时序契约见函数头）。
        curateProductChrome (&studio);

        studio.show ();
        const int exitCode = app.exec ();
        return exitCode;
    }
    catch (const rw::core::Exception& e) {
        // RobWork 异常族（RW_THROW）——呈现后非零退出（禁止静默吞错）。
        QMessageBox::critical (nullptr, QStringLiteral("工业机械臂设计平台"),
                               QString::fromLatin1(e.getMessage().getText().c_str()));
        return -1;
    }
    catch (const std::exception& e) {
        QMessageBox::critical (nullptr, QStringLiteral("工业机械臂设计平台"),
                               QString::fromUtf8(e.what()));
        return -1;
    }
}

#ifdef _WIN32
// GUI 子系统入口（框架 main.cpp 同款 shim——WIN32 目标无控制台，入口
// 转交标准 main；__argc/__argv 为 CRT 提供的全局形态）。
int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;
    return main (__argc, __argv);
}
#endif
