/**
 * @file   DynamicsPluginAssembly.cpp
 * @brief  dynamics 插件装配门面的实现翻译单元——描述符现产＋模块装配
 *         API 的落点（WP-17-T02 最小可注册形态＋WP-17-T09 面板/命令
 *         登记面）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作——五命令族与"UI 线程不得执行
 *     动力学计算"红线——本 TU 零动力学计算类符号〔词表见契约测试；
 *     其扫描为全文扫描，本注释不书写词表符号字样〕）、§11.5（GUI
 *     手动点验流程的装配入口——sdurws_ird_dynamics_app 消费本工厂）
 *   - units/ui.md §11.1（白名单 token "dynamics"）、§3.5（键族
 *     plugin.<id>.title／cmd.<id>.title）、§10.9（装配时序——工厂/
 *     注入在装配期，面板工厂仅 UI 线程）
 *   - 先例：kinematics/plugin/KinematicsPluginAssembly.cpp（WP-15-T12
 *     门面＋面板工厂闭包转接模块的同款形态——dynamics 零 ui 边收缩）
 *
 * 线程模型：工厂/bind 系/setServices 在装配线程；面板工厂闭包仅 UI 线程。
 *
 * Qt 说明（诚实登记）：本 TU 消费 QWidget（面板工厂产物类型——经
 * DynPanelModule.hpp 前置声明与 DynamicsPanelWidget 完整类型到达）；
 * Qt6 链接在 sdurws_ird_dynamics_plugin 目标层面建立（卡 §3.2 二分
 * 结构"Qt Widgets 插件"例外载体面）。零 Q_OBJECT 类故目标不开
 * AUTOMOC（DTB §5.1 v0.17 行口径——面板/曲线视图均为 QWidget 派生
 * 纯虚 override 形态，无信号槽需求）。
 */

#include <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp>

// 同单元私有头（R-2 不跨单元——本 TU 编入 dynamics 插件目标自身）。
#include "DynPanelCommandCatalog.hpp" // dynDomainCommands（五命令目录现产）
#include "DynPanelModule.hpp"         // DynPanelModule（完整类型——析构/
                                      //   createPanel/refreshFromSession）
#include "DynPanelTypes.hpp"          // DynPanelServices/DynModuleSessionState
                                      //   （API 签名完整型）
#include "DynamicsPanelWidget.hpp"    // DynamicsPanelWidget（面板工厂产物）

namespace sdurws::ird::dynamics {

// =====================================================================
// 门面装配 API（转发模块——kinematics 先例同款转发纪律：门面零业务
// 语义，模块是缝/会话态的唯一归属）。
// =====================================================================

DynamicsPluginAssembly::~DynamicsPluginAssembly() = default;
// 移动特殊成员同样在完整类型可见处默认（unique_ptr 成员——Pimpl 手法
// 与析构同口径）。
DynamicsPluginAssembly::DynamicsPluginAssembly(DynamicsPluginAssembly&&) noexcept = default;
DynamicsPluginAssembly& DynamicsPluginAssembly::operator=(
    DynamicsPluginAssembly&&) noexcept = default;

void DynamicsPluginAssembly::setServices(const DynPanelServices& services)
{
    if (m_module == nullptr) {
        return;  // 模块缺位＝装配缺陷（门面构造即持有模块——防御面
                 // 静默返回与既有 bind* 同纪律：不虚构注入成功）
    }
    m_module->services = services;
}

void DynamicsPluginAssembly::bindCommandSubmit(
    std::function<void(const std::string&)> submit)
{
    if (m_module != nullptr) {
        m_module->services.commandSubmit = std::move(submit);
    }
}

void DynamicsPluginAssembly::bindCommandAvailability(
    std::function<bool(const std::string&)> availability)
{
    if (m_module != nullptr) {
        m_module->services.commandAvailability = std::move(availability);
    }
}

void DynamicsPluginAssembly::bindTextResolver(
    std::function<std::string(const std::string& titleKey)> resolve)
{
    if (m_module != nullptr) {
        m_module->services.textResolver = std::move(resolve);
    }
}

DynModuleSessionState& DynamicsPluginAssembly::session()
{
    // 会话权威态访问（装配层注入事实投影——模块内唯一载体；模块缺位
    // 不可达：工厂即建模块，null 解引用属装配期程序缺陷而非环境态，
    // 由 unique_ptr 契约保证）。
    return m_module->session;
}

void DynamicsPluginAssembly::refreshFromSession()
{
    if (m_module != nullptr) {
        m_module->refreshFromSession();
    }
}

// =====================================================================
// 模块面板工厂落点（DynPanelModule——面板创建与会话刷新的执行面）。
// =====================================================================

QWidget* DynPanelModule::createPanel()
{
    // 每次调用新建面板（归调用方接管——宿主层持有；构造即整面刷新）。
    m_panel = new DynamicsPanelWidget(*this);
    return m_panel;
}

void DynPanelModule::refreshFromSession()
{
    if (m_panel != nullptr) {
        m_panel->refreshFromSession();
    }
}

// =====================================================================
// 工厂（描述符现产——登记值与 ui.md 出处逐条对应，零私造词表）。
// =====================================================================

DynamicsPluginAssembly createDynamicsPluginAssembly()
{
    DynamicsPluginAssembly result;
    // 具体模块（unique_ptr 持有——消费方经门面 API 与 module() 访问）。
    result.m_module = std::make_unique<DynPanelModule>();

    // ---- 描述符两登记字段（T02 契约值逐字保留——既有契约测试不动）。
    DynamicsPluginDescriptor& descriptor = result.descriptor;
    descriptor.pluginId = "dynamics";  // ui.md §11.1 白名单 token（
                                       //   AboutDialog.cpp 在册）
    descriptor.titleKey =
        "plugin.dynamics.title";  // ui.md §3.5 键族（值归 ui 文案资源）

    // ---- 挂位/域键词表（T09 新增——assembly 头常量直拷）。 ---------
    descriptor.stageToken = kDynStageToken;          // §6.4 七阶段第 4
    descriptor.readinessDomainKey = kDynDomainKey;   // §6.5 域注册键

    // ---- 命令登记面（§9.5 五命令——目录现产，词表书写点在
    //      Commands.hpp；键族派生规则在目录实现 TU）。 ---------------
    descriptor.commands = dynDomainCommands();

    // ---- 面板登记面（一条主面板——工作流页＋曲线视图合一 Tab；
    //      工厂闭包转接模块 createPanel，模块指针存活期由装配层保证）。 -
    DynPanelRegistration panel;
    panel.stageToken = kDynStageToken;
    panel.titleKey = kDynWorkflowPageKey;  // 主面板标题键（工作流页——
                                           //   曲线视图为页半区）
    panel.advanced = false;                // 主面板位（UX-04 非 advanced）
    DynPanelModule* moduleRaw = result.m_module.get();
    panel.factory = [moduleRaw]() -> QWidget* {
        // 闭包捕获模块裸指针（ui.md §10.9 装配时序——工厂调用期与面板
        // 存活期由装配层保证门面存活；每次调用新建面板）。
        return moduleRaw->createPanel();
    };
    descriptor.panels.push_back(std::move(panel));
    return result;
}

}  // namespace sdurws::ird::dynamics
