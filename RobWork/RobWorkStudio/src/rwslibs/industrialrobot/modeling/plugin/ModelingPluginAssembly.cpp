/**
 * @file   ModelingPluginAssembly.cpp
 * @brief  建模插件装配门面实现（plugin/ 私有实现——编入
 *         sdurws_ird_modeling_plugin；消费面仅见 assembly/ 公共头）。
 *
 * 设计依据：assembly/sdurws/ird/modeling/ModelingPluginAssembly.hpp 文件头
 * （本 TU 是其全部方法的实现落点——WP-24-T03 首版装配）。
 */

#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>

// 同单元私有头（R-2 不跨单元——本 TU 编入建模插件目标自身）。
#include "ModelingUiModule.hpp"
#include "PanelCommandCatalog.hpp"  // modelingPanelRegistration/modelingDomainCommands（§9.7.3 登记数据）

namespace sdurws {
namespace ird {
namespace modeling {

ModelingPluginAssembly::ModelingPluginAssembly() = default;

ModelingPluginAssembly::~ModelingPluginAssembly() = default;

ModelingPluginAssembly::ModelingPluginAssembly(ModelingPluginAssembly&&) noexcept = default;

ModelingPluginAssembly& ModelingPluginAssembly::operator=(ModelingPluginAssembly&&) noexcept = default;

void ModelingPluginAssembly::bindCommandSubmit(
    std::function<void(const ui::CommandId&)> submit)
{
    if (m_impl != nullptr) {
        m_impl->bindCommandSubmit(std::move(submit));
    }
}

void ModelingPluginAssembly::bindTextResolver(
    std::function<QString(const std::string& titleKey)> resolve)
{
    if (m_impl != nullptr) {
        m_impl->bindTextResolver(std::move(resolve));
    }
}

void ModelingPluginAssembly::seedTemplateSession()
{
    if (m_impl != nullptr) {
        m_impl->seedTemplateSession();
    }
}

void ModelingPluginAssembly::bindSessionAnchor(
    const sdurws::ird::core::BranchId& branch,
    const sdurws::ird::core::RevisionId& base)
{
    if (m_impl != nullptr) {
        m_impl->bindSessionAnchor(branch, base);
    }
}

void ModelingPluginAssembly::noteAppliedRevision(
    const sdurws::ird::core::RevisionId& newBase,
    const std::optional<sdurws::ird::core::ObjectId>& rootObjectId)
{
    if (m_impl != nullptr) {
        m_impl->noteAppliedRevision(newBase, rootObjectId);
    }
}

ui::IModuleDraftSource& ModelingPluginAssembly::modelingDraftSource() const
{
    // 多重继承静态转换（IPluginUiModule＋IModuleDraftSource 双基——同一
    // 对象的两个接口视图）。
    return *m_impl;
}

ModelingPluginAssembly createModelingPluginAssembly()
{
    ModelingPluginAssembly result;
    // 具体模块（接口 unique_ptr 持有——消费方只见 IPluginUiModule 三方法）。
    auto concrete = std::make_unique<ModelingUiModule>();
    result.m_impl = concrete.get();
    result.module = std::move(concrete);

    // 装配描述符（§10.9 形状——登记数据权威在 PanelCommandCatalog 现产）：
    //   pluginId＝白名单 token；stages/capabilities＝modelingPanelRegistration
    //   值；commands＝十条域命令描述符；panels＝单面板（工厂闭包转接模块
    //   createPanel——内部完成会话提供器/提交出口/文案解析接线）。
    const PanelRegistrationRecord registration = modelingPanelRegistration();
    result.descriptor.pluginId = registration.pluginId;
    result.descriptor.titleKey = registration.titleKey;
    result.descriptor.stages = {registration.stage};
    result.descriptor.capabilities.providesStagePanel =
        registration.capabilities.providesStagePanel;
    result.descriptor.capabilities.providesReadonlyProjection =
        registration.capabilities.providesReadonlyProjection;
    result.descriptor.capabilities.registersCommands =
        registration.capabilities.registersCommands;
    result.descriptor.commands = modelingDomainCommands();
    ui::PanelRegistration panel;
    panel.stage = registration.stage;
    panel.titleKey = registration.titleKey;
    panel.advanced = registration.advanced;
    ui::IPluginUiModule* moduleRef = result.module.get();
    panel.factory = [moduleRef]() -> QWidget* {
        // 静态转换安全面：本门面产出的模块恒为建模具体模块（单一来源）。
        return static_cast<ModelingUiModule*>(moduleRef)->createPanel();
    };
    result.descriptor.panels.push_back(std::move(panel));
    return result;
}

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws
