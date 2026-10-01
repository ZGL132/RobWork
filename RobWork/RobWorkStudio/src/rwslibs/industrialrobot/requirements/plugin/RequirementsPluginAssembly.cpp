/**
 * @file   RequirementsPluginAssembly.cpp
 * @brief  requirements 插件装配门面实现（plugin/ 私有实现——编入
 *         sdurws_ird_requirements_plugin；消费面仅见 assembly/ 公共头）。
 *
 * 设计依据：assembly/sdurws/ird/requirements/RequirementsPluginAssembly.hpp
 * 文件头（本 TU 是其全部方法的实现落点——O-45 裁决 WP-14-T11 补建缝）。
 * 实现纪律：**只出线不重写**——全部方法均为模块既有已验收面的薄转发
 * （三接入面/会话接线）或登记数据直通（descriptor），零业务语义新增；
 * 会话接线方法是对 RequirementsModuleSessionState 既有字段语义的具名化
 * （字段注释即权威——"基线前移＋根身份回填＋恢复草稿资格位清位"与
 * modeling 门面 T03b-2 同名方法语义同构）。
 */

#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>

// 同单元私有头（R-2 不跨单元——本 TU 编入 requirements 插件目标自身；
// 宿主装配层消费面只见 assembly/ 公共头，零本清单外溢）。
#include "PanelCommandCatalog.hpp"      // requirementsPanelRegistration/requirementsDomainCommands（§9.8 登记数据）
#include "RequirementsPanelWidget.hpp"  // 面板类型（工厂闭包创建＋编辑目标提供器接线）
#include "RequirementsUiModule.hpp"     // 具体模块（构造/转发面——会话态权威载体）

namespace sdurws {
namespace ird {
namespace requirements {

RequirementsPluginAssembly::RequirementsPluginAssembly() = default;

RequirementsPluginAssembly::~RequirementsPluginAssembly() = default;

RequirementsPluginAssembly::RequirementsPluginAssembly(RequirementsPluginAssembly&&) noexcept = default;

RequirementsPluginAssembly& RequirementsPluginAssembly::operator=(
    RequirementsPluginAssembly&&) noexcept = default;

// =====================================================================
// 会话接线（转发模块既有面——零语义新增）
// =====================================================================

void RequirementsPluginAssembly::attachEditor(IRequirementEditor* editor)
{
    if (m_impl != nullptr) {
        m_impl->attachEditor(editor);  // 草稿唯一写目标注入（模块头注为权威语义）
    }
}

void RequirementsPluginAssembly::bindSessionAnchor(
    const core::BranchId& branch,
    const std::optional<core::RevisionId>& baseRevision)
{
    if (m_impl != nullptr) {
        // 会话权威态直写（modeling 门面 bindSessionAnchor 同构语义——
        // 信封 branch 与草稿基线两个锚位；字段注释即权威）。
        auto& session = m_impl->session();
        session.branch = branch;
        session.baseRevision = baseRevision;
    }
}

void RequirementsPluginAssembly::noteAppliedRevision(
    const core::RevisionId& newBase,
    const std::optional<core::ObjectId>& rootObjectId)
{
    if (m_impl != nullptr) {
        // 应用回执三件事（modeling 门面 noteAppliedRevision 同构）：
        // 基线前移＋根身份回填＋恢复草稿资格位清位（draft.apply 后由
        // 装配层清位——RequirementsModuleSessionState 字段注释的装配层
        // 动作落点即本方法）。
        auto& session = m_impl->session();
        session.baseRevision = newBase;
        session.rootObjectId = rootObjectId;
        session.restoredDraftPending = false;
    }
}

void RequirementsPluginAssembly::bindReadiness(const RequirementReadinessReport& report)
{
    if (m_impl != nullptr) {
        // 就绪报告直投会话态（readonlyProjections 的数据源——判定权威在
        // IRequirementReadinessChecker，本注入零判定，P-REQ-6 边界）。
        m_impl->session().readiness = report;
    }
}

void RequirementsPluginAssembly::onSessionDetached()
{
    if (m_impl != nullptr) {
        // 项目关闭/切换的会话清理半区：选择服务退订（幂等——适配器 RAII
        // 句柄收口，UI-T23 关闭清理核查的域侧对端）＋会话权威态全字段
        // 清空（磁盘草稿零触碰——模块零持久化路径，结构保证）。
        detachSelectionService();
        auto& session = m_impl->session();
        session.branch = core::BranchId{};
        session.baseRevision = std::nullopt;
        session.rootObjectId = std::nullopt;
        session.readiness = std::nullopt;
        session.restoredDraftPending = false;
    }
}

void RequirementsPluginAssembly::bindCommandSubmit(
    std::function<void(const ui::CommandId&)> submit)
{
    if (m_impl != nullptr) {
        m_impl->bindCommandSubmit(std::move(submit));
    }
}

void RequirementsPluginAssembly::bindCommandAvailability(
    std::function<ui::CommandAvailability(const ui::CommandId&)> availability)
{
    if (m_impl != nullptr) {
        m_impl->bindCommandAvailability(std::move(availability));
    }
}

void RequirementsPluginAssembly::setPostEditAction(
    std::function<void()> action)
{
    if (m_impl != nullptr) {
        m_impl->setPostEditAction(std::move(action));
    }
}

RequirementsModuleSessionState& RequirementsPluginAssembly::session() const
{
    // 会话权威态访问（装配期调用——m_impl 恒在；modeling/kinematics 门面
    // session() 无判空先例同构：装配产物的唯一持有方是宿主装配层）。
    return m_impl->session();
}

std::optional<core::RevisionId> RequirementsPluginAssembly::sessionBaseRevision() const
{
    // UI-T35 P2：会话基线只读（外部修订同步的自身回执比对面——零写面）。
    if (m_impl == nullptr) {
        return std::nullopt;
    }
    return m_impl->session().baseRevision;
}

// =====================================================================
// 宿主迁移三接入面（WP-14-T10 已验收面出线——转发模块同名词柄）
// =====================================================================

RequirementsSharedSurfaceHandles RequirementsPluginAssembly::sharedSurfaceProviders()
{
    // 转发模块句柄（门面自持结构与模块嵌套结构逐字段同形——转换零语义；
    // shared_ptr 同址稳定，重复调用返回同一实例）。
    const RequirementsUiModule::SharedSurfaceHandles handles =
        m_impl != nullptr ? m_impl->sharedSurfaceProviders()
                          : RequirementsUiModule::SharedSurfaceHandles{};
    RequirementsSharedSurfaceHandles out;
    out.treeNodes = handles.treeNodes;
    out.propertyPages = handles.propertyPages;
    return out;
}

void RequirementsPluginAssembly::attachSelectionService(ui::SelectionService& service)
{
    if (m_impl != nullptr) {
        m_impl->attachSelectionService(service);  // 适配器接线（RAII 句柄随模块）
    }
}

void RequirementsPluginAssembly::detachSelectionService()
{
    if (m_impl != nullptr) {
        m_impl->detachSelectionService();  // 幂等退订（未订阅时空操作）
    }
}

bool RequirementsPluginAssembly::reportView3DPick(const core::ObjectId& oid)
{
    // 上行拾取上报（View3DPick 唯一写入口的域侧半区——模块方法注释为
    // 权威语义；未接线＝诚实 false，零伪造）。
    return m_impl != nullptr && m_impl->reportView3DPick(oid);
}

// =====================================================================
// 唯一装配点（descriptor 登记数据直通——零业务语义）
// =====================================================================

RequirementsPluginAssembly createRequirementsPluginAssembly()
{
    RequirementsPluginAssembly result;
    // 具体模块（接口 unique_ptr 持有——消费方只见 IPluginUiModule 三方法；
    // P-REQ-8 消账后 RequirementsUiModule 为真实继承，零适配层）。
    auto concrete = std::make_unique<RequirementsUiModule>();
    result.m_impl = concrete.get();
    result.module = std::move(concrete);

    // 装配描述符（§10.9 形状——登记数据权威在 PanelCommandCatalog 现产）：
    //   pluginId/stage/能力三位＝requirementsPanelRegistration 值；
    //   commands＝九条域命令描述符（§9.8 命令表全量）；
    //   panels＝一条主面板登记记录（五面板区合一 Tab 容器——advanced=false
    //   既定值，映射规则见 assembly 公共头注）。
    const PanelRegistrationRecord registration = requirementsPanelRegistration();
    result.descriptor.pluginId = registration.pluginId;
    result.descriptor.titleKey = registration.titleKey;
    result.descriptor.stages = {registration.stage};
    result.descriptor.capabilities.providesStagePanel =
        registration.capabilities.providesStagePanel;
    result.descriptor.capabilities.providesReadonlyProjection =
        registration.capabilities.providesReadonlyProjection;
    result.descriptor.capabilities.registersCommands =
        registration.capabilities.registersCommands;
    result.descriptor.commands = requirementsDomainCommands();

    // 主面板登记记录（工厂闭包内部完成面板创建与模块接线——宿主零
    // requirements 私有头，O-31 单行适配面纪律）。
    ui::PanelRegistration panel;
    panel.stage = registration.stage;
    panel.titleKey = registration.titleKey;
    panel.advanced = registration.advanced;
    RequirementsUiModule* moduleRaw =
        static_cast<RequirementsUiModule*>(result.module.get());
    panel.factory = [moduleRaw]() -> QWidget* {
        // 静态转换安全面：本门面产出的模块恒为 requirements 具体模块
        // （单一来源）。面板初始可写（L-R12 门控初值——只读态由宿主会话
        // 驱动，非本装配缝范围）。
        auto* widget = new RequirementsPanelWidget(true);
        // 编辑目标提供器：每次编辑提交现取权威编辑器（零缓存——PA-1；
        // 惰性安全：工厂执行时编辑器可尚未 attachEditor，提供器返回
        // nullptr＝编辑禁用的诚实态，不虚构可编辑性）。
        widget->setEditTargetProvider([moduleRaw]() -> IRequirementEditor* {
            return moduleRaw->editor();
        });
        moduleRaw->attachPanel(widget);  // 模块弱语义引用（面板归宿主层持有）
        return widget;
    };
    result.descriptor.panels.push_back(std::move(panel));
    return result;
}

bool RequirementsPluginAssembly::executeDomainCommand(const std::string& commandId)
{
    if (m_impl != nullptr) {
        return m_impl->executeDomainCommand(commandId);
    }
    return false;
}

}  // namespace requirements

}  // namespace ird
}  // namespace sdurws
