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

void ModelingPluginAssembly::bindCommandAvailability(
    std::function<ui::CommandAvailability(const ui::CommandId&)> availability)
{
    if (m_impl != nullptr) {
        m_impl->bindCommandAvailability(std::move(availability));
    }
}

void ModelingPluginAssembly::bindTextResolver(
    std::function<QString(const std::string& titleKey)> resolve)
{
    if (m_impl != nullptr) {
        m_impl->bindTextResolver(std::move(resolve));
    }
}

void ModelingPluginAssembly::bindWorkCellExport(
    std::function<bool(const std::string& targetPath, std::string& summary)> exportFn)
{
    // WC/DWC XML 外供导出回调宿主接线面（UI-T56——UI-T59 补登转发器：
    // UI-T56 落位时漏登本装配面，sdurws_ird_ui_plugin 自该批起断链〔F-518〕
    // ——该目标不在受影响测试目标集内，潜伏两轮验收未被发现）。m_impl
    // 缺位＝失败隔离缺席域，静默零操作（setWritable 同款口径）。
    if (m_impl != nullptr) {
        m_impl->bindWorkCellExport(std::move(exportFn));
    }
}

void ModelingPluginAssembly::bindWorkCellPreview(
    std::function<bool(const std::string& kind, std::string& headerLine,
                       std::string& sourceObject, std::string& text,
                       std::string& reason)> previewFn)
{
    // 预览内存导出回调宿主接线面（UI-T59——F-498 预览半区；转发模块；
    // m_impl 缺位＝失败隔离缺席域，静默零操作）。
    if (m_impl != nullptr) {
        m_impl->bindWorkCellPreview(std::move(previewFn));
    }
}

void ModelingPluginAssembly::setWritable(bool writable)
{
    // L-7 门控输入宿主接线面（UI-T43——需求域 RequirementsPluginAssembly
    // ::setWritable 同构转发；m_impl 缺位＝失败隔离缺席域，静默零操作）。
    if (m_impl != nullptr) {
        m_impl->setWritable(writable);
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

void ModelingPluginAssembly::refreshFromSession()
{
    if (m_impl != nullptr) {
        m_impl->refreshFromSession();
    }
}

bool ModelingPluginAssembly::executeDomainCommand(const std::string& commandId)
{
    if (m_impl != nullptr) {
        return m_impl->executeDomainCommand(commandId);
    }
    return false;  /* 模块缺位＝失败隔离缺席域 */
}

void ModelingPluginAssembly::bindRuntimeNameMap(
    const sdurws::ird::runtime::RuntimeNameMap* map)
{
    if (m_impl != nullptr) {
        m_impl->bindRuntimeNameMap(map);
    }
}

bool ModelingPluginAssembly::tryDraftObjectName(
    const sdurws::ird::core::ObjectId& object, std::string& name) const
{
    if (m_impl != nullptr) {
        return m_impl->tryDraftObjectName(object, name);
    }
    return false;  /* 模块缺位＝解析缺席（诚实 false——不猜测） */
}

void ModelingPluginAssembly::bindPolicyProvider(
    std::function<const policy::EngineeringPolicySet*()> provider)
{
    if (m_impl != nullptr) {
        m_impl->bindPolicyProvider(std::move(provider));
    }
}

void ModelingPluginAssembly::onSessionDetached()
{
    if (m_impl != nullptr) {
        m_impl->onSessionDetached();
    }
}

void ModelingPluginAssembly::onRevisionCommitted(
    const sdurws::ird::core::BranchId& branch,
    const sdurws::ird::core::RevisionId& newTip)
{
    if (m_impl != nullptr) {
        m_impl->onRevisionCommitted(branch, newTip);
    }
}

ui::IModuleDraftSource& ModelingPluginAssembly::modelingDraftSource() const
{
    // 多重继承静态转换（IPluginUiModule＋IModuleDraftSource 双基——同一
    // 对象的两个接口视图）。
    return *m_impl;
}

ui::IUiDomainReadinessSource& ModelingPluginAssembly::readinessSource() const
{
    // 多重继承静态转换（T03b 收口新增第三基 IUiDomainReadinessSource——
    // 同一对象的汇聚源接口视图；§6.5 汇聚输入注册用）。
    return *m_impl;
}

// =====================================================================
// WP-13-T20 宿主迁移三接入面（B1-SPEC §5.1——转发模块；消费面零建模
// 私有头依赖，类型完备性由 ModelingUiModule.hpp 传递的注册协议头保证）
// =====================================================================

ModelingPluginAssembly::SharedSurfaceHandles
ModelingPluginAssembly::sharedSurfaceProviders() const
{
    SharedSurfaceHandles handles;
    if (m_impl != nullptr) {
        // 模块侧与门面侧是两个同名句柄结构（门面消费面只见 ui 接口类型
        // ——R-2 门面纪律）——逐成员转交（shared_ptr 同址）。
        const auto fromModule = m_impl->sharedSurfaceProviders();
        handles.treeNodes = fromModule.treeNodes;
        handles.propertyPages = fromModule.propertyPages;
    }
    return handles;
}

void ModelingPluginAssembly::attachSelectionService(ui::SelectionService& service)
{
    if (m_impl != nullptr) {
        m_impl->attachSelectionService(service);
    }
}

void ModelingPluginAssembly::detachSelectionService()
{
    if (m_impl != nullptr) {
        m_impl->detachSelectionService();
    }
}

bool ModelingPluginAssembly::reportView3DPick(const sdurws::ird::core::ObjectId& oid)
{
    if (m_impl != nullptr) {
        return m_impl->reportView3DPick(oid);
    }
    return false;
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
