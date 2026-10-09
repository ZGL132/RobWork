/**
 * @file   SelectionPluginAssembly.cpp
 * @brief  selection 插件装配门面的实现翻译单元——描述符现产＋模块装配
 *         API 的落点（WP-19-T02 最小可注册形态＋WP-19-T10 面板/命令
 *         登记面）。
 *
 * 设计依据：
 *   - units/selection.md §3.1（插件组成——界面面随 WP-19-T10）、§3.2
 *     （插件依赖仅本计算库＋Qt Widgets）、§3.4（插件零计算红线——本
 *     TU 无选型计算符号〔词表见契约测试；其扫描为全文扫描，本注释不
 *     书写词表符号字样〕）、§16（WP-19-T10 行——工作流页/目录管理/
 *     候选表的登记面）
 *   - units/ui.md §11.1（白名单 token "selection"）、§3.5（键族
 *     plugin.<id>.title／cmd.<id>.title）、§6.4（七阶段词表第 5）、
 *     §6.5（域注册键）、§10.9（装配时序——工厂/注入在装配期，面板
 *     工厂仅 UI 线程）
 *   - 先例：dynamics/plugin/DynamicsPluginAssembly.cpp（WP-17-T09
 *     门面＋面板工厂闭包转接模块的同款形态——selection 零 ui 边收缩）
 *
 * 线程模型：工厂/bind 系/setServices 在装配线程；面板工厂闭包仅 UI 线程。
 *
 * Qt 说明（诚实登记）：本 TU 消费 QWidget（面板工厂产物类型——经
 * SelPanelModule.hpp 前置声明与 SelCatalogPanelWidget 完整类型到达）；
 * Qt6 链接在 sdurws_ird_selection_plugin 目标层面建立（卡 §3.2 二分
 * 结构"Qt Widgets 插件"例外载体面）。零 Q_OBJECT 类故目标不开
 * AUTOMOC（DTB §5.1 v0.17 行口径——面板均为 QWidget 派生纯虚
 * override 形态，无信号槽需求）。
 */

#include <sdurws/ird/selection/SelectionPluginAssembly.hpp>

// 同单元私有头（R-2 不跨单元——本 TU 编入 selection 插件目标自身）。
#include "SelPanelCommandCatalog.hpp" // selDomainCommands（命令目录现产）
#include "SelPanelModule.hpp"         // SelPanelModule（完整类型——析构/
                                      //   createPanel/refreshFromSession）
#include "SelPanelTypes.hpp"          // SelPanelServices/SelModuleSessionState
                                      //   （API 签名完整型）
#include "SelCatalogPanelWidget.hpp"  // SelCatalogPanelWidget（面板工厂产物）

namespace sdurws::ird::selection {

// =====================================================================
// 门面装配 API（转发模块——dynamics 先例同款转发纪律：门面零业务
// 语义，模块是缝/会话态的唯一归属）。
// =====================================================================

SelectionPluginAssembly::~SelectionPluginAssembly() = default;
// 移动特殊成员同样在完整类型可见处默认（unique_ptr 成员——Pimpl 手法
// 与析构同口径）。
SelectionPluginAssembly::SelectionPluginAssembly(SelectionPluginAssembly&&) noexcept = default;
SelectionPluginAssembly& SelectionPluginAssembly::operator=(
    SelectionPluginAssembly&&) noexcept = default;

void SelectionPluginAssembly::setServices(const SelPanelServices& services)
{
    if (m_module == nullptr) {
        return;  // 模块缺位＝装配缺陷（门面构造即持有模块——防御面
                 // 静默返回与既有 bind* 同纪律：不虚构注入成功）
    }
    m_module->services = services;
}

void SelectionPluginAssembly::bindBackfillSubmit(
    std::function<void(const std::string&)> submit)
{
    if (m_module != nullptr) {
        m_module->services.backfillSubmit = std::move(submit);
    }
}

void SelectionPluginAssembly::bindBackfillAvailability(
    std::function<bool(const std::string&)> availability)
{
    if (m_module != nullptr) {
        m_module->services.backfillAvailability = std::move(availability);
    }
}

void SelectionPluginAssembly::bindTextResolver(
    std::function<std::string(const std::string& titleKey)> resolve)
{
    if (m_module != nullptr) {
        m_module->services.textResolver = std::move(resolve);
    }
}

SelModuleSessionState& SelectionPluginAssembly::session()
{
    // 会话权威态访问（装配层注入事实投影——模块内唯一载体；模块缺位
    // 不可达：工厂即建模块，null 解引用属装配期程序缺陷而非环境态，
    // 由 unique_ptr 契约保证）。
    return m_module->session;
}

void SelectionPluginAssembly::refreshFromSession()
{
    if (m_module != nullptr) {
        m_module->refreshFromSession();
    }
}

// =====================================================================
// 模块面板工厂落点（SelPanelModule——面板创建与会话刷新的执行面）。
// =====================================================================

QWidget* SelPanelModule::createPanel()
{
    // 每次调用新建面板（归调用方接管——宿主层持有；构造即整面刷新）。
    m_panel = new SelCatalogPanelWidget(*this);
    return m_panel;
}

void SelPanelModule::refreshFromSession()
{
    if (m_panel != nullptr) {
        m_panel->refreshFromSession();
    }
}

// =====================================================================
// 工厂（描述符现产——登记值与 ui.md 出处逐条对应，零私造词表）。
// =====================================================================

SelectionPluginAssembly createSelectionPluginAssembly()
{
    SelectionPluginAssembly result;
    // 具体模块（unique_ptr 持有——消费方经门面 API 与 module() 访问）。
    result.m_module = std::make_unique<SelPanelModule>();

    // ---- 描述符两登记字段（T02 契约值逐字保留——既有契约测试不动）。
    SelectionPluginDescriptor& descriptor = result.descriptor;
    descriptor.pluginId = "selection";  // ui.md §11.1 白名单第 6 token
    descriptor.titleKey =
        "plugin.selection.title";  // ui.md §3.5 键族（值归 ui 文案资源）

    // ---- 挂位/域键词表（T10 新增——assembly 头常量直拷）。 ---------
    descriptor.stageToken = kSelStageToken;        // §6.4 七阶段第 5
    descriptor.readinessDomainKey = kSelDomainKey; // §6.5 域注册键

    // ---- 命令登记面（回填入口一条——目录现产，token 自持常量在
    //      SelPanelCommandCatalog.hpp；键族派生规则在目录实现 TU）。 ---
    descriptor.commands = selDomainCommands();

    // ---- 面板登记面（一条主面板——工作流页＋目录管理页＋候选表页
    //      合一 Tab；工厂闭包转接模块 createPanel，模块指针存活期由
    //      装配层保证）。 ---------------------------------------------
    SelPanelRegistration panel;
    panel.stageToken = kSelStageToken;
    panel.titleKey = kSelWorkflowPageKey;  // 主面板标题键（工作流页——
                                           //   目录管理/候选表为页半区）
    panel.advanced = false;                // 主面板位（UX-04 非 advanced）
    SelPanelModule* moduleRaw = result.m_module.get();
    panel.factory = [moduleRaw]() -> QWidget* {
        // 闭包捕获模块裸指针（ui.md §10.9 装配时序——工厂调用期与面板
        // 存活期由装配层保证门面存活；每次调用新建面板）。
        return moduleRaw->createPanel();
    };
    descriptor.panels.push_back(std::move(panel));
    return result;
}

}  // namespace sdurws::ird::selection
