/**
 * @file   KinematicsPluginAssembly.cpp
 * @brief  kinematics 插件装配门面实现（plugin/ 私有实现——编入
 *         sdurws_ird_kinematics_plugin；消费面仅见 assembly/ 公共头）。
 *
 * 设计依据：assembly/sdurws/ird/kinematics/KinematicsPluginAssembly.hpp
 * 文件头（本 TU 是其全部方法的实现落点——§3.2"_plugin 随 WP-15-T12"的
 * 装配承诺落地面）。
 */

#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>

// 同单元私有头（R-2 不跨单元——本 TU 编入 kinematics 插件目标自身）。
#include "KinPanelCommandCatalog.hpp"  // kinematicsPanelRegistration/kinematicsDomainCommands
#include "KinematicsUiModule.hpp"      // 具体模块（构造/转发面）

namespace sdurws {
namespace ird {
namespace kinematics {

KinematicsPluginAssembly::KinematicsPluginAssembly() = default;

KinematicsPluginAssembly::~KinematicsPluginAssembly() = default;

KinematicsPluginAssembly::KinematicsPluginAssembly(KinematicsPluginAssembly&&) noexcept = default;

KinematicsPluginAssembly& KinematicsPluginAssembly::operator=(
    KinematicsPluginAssembly&&) noexcept = default;

void KinematicsPluginAssembly::bindCommandSubmit(
    std::function<void(const ui::CommandId&)> submit)
{
    if (m_impl != nullptr) {
        m_impl->bindCommandSubmit(std::move(submit));
    }
}

void KinematicsPluginAssembly::setServices(KinPanelServices services)
{
    if (m_impl != nullptr) {
        m_impl->setServices(std::move(services));
    }
}

void KinematicsPluginAssembly::bindTextResolver(
    std::function<QString(const std::string& titleKey)> resolve)
{
    if (m_impl != nullptr) {
        m_impl->bindTextResolver(std::move(resolve));
    }
}

KinModuleSessionState& KinematicsPluginAssembly::session() const
{
    // 会话权威态访问（装配层注入快照绑定/配置基线——模块内唯一载体）。
    return m_impl->session();
}

void KinematicsPluginAssembly::refreshFromSession()
{
    if (m_impl != nullptr) {
        m_impl->refreshFromSession();
    }
}

KinematicsSharedSurfaceHandles KinematicsPluginAssembly::sharedSurfaceProviders()
{
    // 转发模块句柄（门面自持结构与模块内结构逐字段同形——转换零语义）。
    const KinematicsUiModule::SharedSurfaceHandles handles =
        m_impl != nullptr ? m_impl->sharedSurfaceProviders()
                          : KinematicsUiModule::SharedSurfaceHandles{};
    KinematicsSharedSurfaceHandles out;
    out.treeNodes = handles.treeNodes;
    out.propertyPages = handles.propertyPages;
    return out;
}

void KinematicsPluginAssembly::attachSelectionService(ui::SelectionService& service)
{
    if (m_impl != nullptr) {
        m_impl->attachSelectionService(service);  // 适配器接线（RAII 句柄随模块）
    }
}

bool KinematicsPluginAssembly::applyHostJointState(const std::vector<double>& q)
{
    // D8/D9 会话姿态承接（零修订结构性保证——模块方法注释为权威语义）。
    return m_impl != nullptr && m_impl->applyHostJointState(q);
}

KinematicsPluginAssembly createKinematicsPluginAssembly()
{
    KinematicsPluginAssembly result;
    // 具体模块（接口 unique_ptr 持有——消费方只见 IPluginUiModule 三方法）。
    auto concrete = std::make_unique<KinematicsUiModule>();
    result.m_impl = concrete.get();
    result.module = std::move(concrete);

    // 装配描述符（§10.9 形状——登记数据权威在 KinPanelCommandCatalog 现产）：
    //   pluginId/stages/capabilities＝kinematicsPanelRegistration 值；
    //   commands＝八条域命令描述符（§9.8 表七行八个 id）；
    //   panels＝两条登记记录（主面板＝面板表行 1~4 的合一 Tab 容器；
    //   高级面板＝行 5，advanced=true——映射规则见 assembly 公共头注）。
    const KinPanelRegistration registration = kinematicsPanelRegistration();
    result.descriptor.pluginId = registration.pluginId;
    result.descriptor.titleKey = registration.titleKey;
    result.descriptor.stages = {registration.stage};
    result.descriptor.capabilities.providesStagePanel =
        registration.providesStagePanel;
    result.descriptor.capabilities.providesReadonlyProjection =
        registration.providesReadonlyProjection;
    result.descriptor.capabilities.registersCommands =
        registration.registersCommands;
    result.descriptor.commands = kinematicsDomainCommands();
    // 主面板登记记录（行 1~4 合一——工厂闭包转接模块 createPanel）。
    ui::PanelRegistration mainPanel;
    mainPanel.stage = registration.stage;
    mainPanel.titleKey = registration.panels.at(0).titleKey;
    mainPanel.advanced = false;
    KinematicsUiModule* moduleRaw =
        static_cast<KinematicsUiModule*>(result.module.get());
    ui::IPluginUiModule* moduleRef = result.module.get();
    mainPanel.factory = [moduleRef]() -> QWidget* {
        // 静态转换安全面：本门面产出的模块恒为 kinematics 具体模块（单一
        // 来源）——createPanel 经接口面不可达，需具体类型。
        return static_cast<KinematicsUiModule*>(moduleRef)->createPanel();
    };
    result.descriptor.panels.push_back(std::move(mainPanel));

    // 高级面板登记记录（行 5——求解配置，advanced=true；工厂闭包同构）。
    ui::PanelRegistration advancedPanel;
    advancedPanel.stage = registration.stage;
    advancedPanel.titleKey = registration.panels.at(4).titleKey;
    advancedPanel.advanced = true;
    advancedPanel.factory = [moduleRaw]() -> QWidget* {
        return moduleRaw->createAdvancedConfigPanel();
    };
    result.descriptor.panels.push_back(std::move(advancedPanel));
    return result;
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
