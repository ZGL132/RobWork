/**
 * @file   OptimizationPluginAssembly.cpp
 * @brief  optimization 插件装配门面的实现翻译单元——描述符现产＋模块装配
 *         API 的落点（WP-20-T02 最小可注册形态＋WP-20-T10 面板登记面）。
 *
 * 设计依据：
 *   - units/optimization.md §3.1/§3.2（目标布局与二分结构——四页界面面
 *     随 WP-20-T10 落位；"只消费端口与只读投影，不持算法/判定真值"红线
 *     ——本 TU 零优化域计算类符号〔词表见契约测试；其扫描为全文扫描，
 *     本注释不书写词表符号字样〕）、§9.1（运行控制面板的 UI 线程约束）
 *   - units/ui.md §11.1（白名单 token "optimization"——AboutDialog.cpp
 *     在册）、§3.5（键族 plugin.<id>.title）、§10.9（装配时序——工厂/
 *     注入在装配期，面板工厂仅 UI 线程）
 *   - 先例：dynamics/plugin/DynamicsPluginAssembly.cpp（WP-17-T09 门面
 *     转发纪律同款——门面零业务语义，模块是缝/会话态的唯一归属）
 *
 * 线程模型：工厂/bind 系/setServices 在装配线程；面板工厂闭包仅 UI
 * 线程（ui.md §10.9 线程行）。
 *
 * Qt 说明（诚实登记）：本 TU 消费 QWidget（面板工厂产物类型——经
 * OptPanelModule.hpp 前置声明与 OptimizationPanelWidget 完整类型到达）；
 * Qt6 链接在 sdurws_ird_optimization_plugin 目标层面建立（卡 §3.2 二分
 * 结构"Qt Widgets 插件"例外载体面）。零 Q_OBJECT 类故目标不开
 * AUTOMOC（DTB §5.1 v0.17 行口径——面板为 QWidget 派生纯虚 override
 * 形态，无信号槽声明需求〔按钮经 lambda connect〕）。
 */

#include <sdurws/ird/optimization/OptimizationPluginAssembly.hpp>

// 同单元私有头（R-2 不跨单元——本 TU 编入 optimization 插件目标自身）。
#include "OptPanelModule.hpp"      // OptPanelModule（完整类型——析构/
                                   //   createPanel/refreshFromSession）
#include "OptPanelTypes.hpp"       // OptPanelServices/OptModuleSessionState
                                   //   （API 签名完整型）
#include "OptimizationPanelWidget.hpp" // OptimizationPanelWidget（面板工厂产物）

namespace sdurws::ird::optimization {

// =====================================================================
// 门面装配 API（转发模块——dynamics 先例同款转发纪律：门面零业务
// 语义，模块是缝/会话态的唯一归属）。
// =====================================================================

OptimizationPluginAssembly::~OptimizationPluginAssembly() = default;
// 移动特殊成员同样在完整类型可见处默认（unique_ptr 成员——Pimpl 手法
// 与析构同口径）。
OptimizationPluginAssembly::OptimizationPluginAssembly(
    OptimizationPluginAssembly&&) noexcept = default;
OptimizationPluginAssembly& OptimizationPluginAssembly::operator=(
    OptimizationPluginAssembly&&) noexcept = default;

void OptimizationPluginAssembly::setServices(const OptPanelServices& services)
{
    if (m_module == nullptr) {
        return;  // 模块缺位＝装配缺陷（门面构造即持有模块——防御面
                 // 静默返回与既有 bind* 同纪律：不虚构注入成功）
    }
    m_module->services = services;
}

void OptimizationPluginAssembly::bindRunStart(std::function<bool()> start)
{
    if (m_module != nullptr) {
        m_module->services.runStart = std::move(start);
    }
}

void OptimizationPluginAssembly::bindRunCancel(std::function<bool()> cancel)
{
    if (m_module != nullptr) {
        m_module->services.runCancel = std::move(cancel);
    }
}

void OptimizationPluginAssembly::bindTextResolver(
    std::function<std::string(const std::string& titleKey)> resolve)
{
    if (m_module != nullptr) {
        m_module->services.textResolver = std::move(resolve);
    }
}

OptModuleSessionState& OptimizationPluginAssembly::session()
{
    // 会话权威态访问（装配层注入事实投影——模块内唯一载体；模块缺位
    // 不可达：工厂即建模块，null 解引用属装配期程序缺陷而非环境态，
    // 由 unique_ptr 契约保证）。
    return m_module->session;
}

void OptimizationPluginAssembly::refreshFromSession()
{
    if (m_module != nullptr) {
        m_module->refreshFromSession();
    }
}

// =====================================================================
// 模块面板工厂落点（OptPanelModule——面板创建与会话刷新的执行面）。
// =====================================================================

QWidget* OptPanelModule::createPanel()
{
    // 每次调用新建面板（归调用方接管——宿主层持有；构造即整面刷新；
    // 仅 UI 线程——卡 §9.1 线程约束）。
    m_panel = new OptimizationPanelWidget(*this);
    return m_panel;
}

void OptPanelModule::refreshFromSession()
{
    if (m_panel != nullptr) {
        m_panel->refreshFromSession();
    }
}

// =====================================================================
// 工厂（描述符现产——登记值与 ui.md 出处逐条对应，零私造词表）。
// =====================================================================

OptimizationPluginAssembly createOptimizationPluginAssembly()
{
    OptimizationPluginAssembly result;
    // 具体模块（unique_ptr 持有——消费方经门面 API 与 module() 访问）。
    result.m_module = std::make_unique<OptPanelModule>();

    // ---- 描述符两登记字段（T02 契约值逐字保留——既有契约测试不动）。 -
    OptimizationPluginDescriptor& descriptor = result.descriptor;
    descriptor.pluginId = "optimization";  // ui.md §11.1 白名单 token（
                                           //   AboutDialog.cpp 在册）
    descriptor.titleKey =
        "plugin.optimization.title";  // ui.md §3.5 键族（值归 ui 文案资源）

    // ---- 挂位/域键词表（T10 新增——assembly 头常量直拷）。 -----------
    descriptor.stageToken = kOptStageToken;        // §6.4 七阶段第 6
    descriptor.readinessDomainKey = kOptDomainKey; // §6.5 域注册键

    // ---- 面板登记面（一条主面板——四页合一 Tab：变量表/约束页/运行
    //      控制〔取消/进度漏斗〕/候选表与对比；工厂闭包转接模块
    //      createPanel，模块指针存活期由装配层保证）。 -----------------
    OptPanelRegistration panel;
    panel.stageToken = kOptStageToken;
    panel.titleKey = kOptPanelTitleKey;  // 主面板标题键
    panel.advanced = false;              // 主面板位（UX-04 非 advanced）
    OptPanelModule* moduleRaw = result.m_module.get();
    panel.factory = [moduleRaw]() -> QWidget* {
        // 闭包捕获模块裸指针（ui.md §10.9 装配时序——工厂调用期与面板
        // 存活期由装配层保证门面存活；每次调用新建面板）。
        return moduleRaw->createPanel();
    };
    descriptor.panels.push_back(std::move(panel));
    return result;
}

}  // namespace sdurws::ird::optimization
