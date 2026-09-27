/**
 * @file   KinematicsUiModule.cpp
 * @brief  kinematics 插件界面模块的实现（KinematicsUiModule.hpp 全部落点）。
 *
 * 设计依据：KinematicsUiModule.hpp 文件头；readonlyProjections 数据面＝
 * KinPanelCommandCatalog::kinematicsReadinessProjection（零判定直投）。
 */

#include "KinematicsUiModule.hpp"

#include "KinPanelCommandCatalog.hpp"              // kinematicsReadinessProjection
#include "KinematicsPanelWidget.hpp"               // 两面板 widget（创建/接线）

namespace sdurws {
namespace ird {
namespace kinematics {

KinematicsUiModule::KinematicsUiModule(KinPanelServices services)
    : m_services(std::move(services))
    // 会话权威态（模块持有——快照绑定/配置基线由装配层经 session() 注入）。
    , m_session(std::make_unique<KinModuleSessionState>())
{
}

KinematicsUiModule::~KinematicsUiModule() = default;

void KinematicsUiModule::onShellReady(ui::IWorkbenchShell& shell)
{
    // §11.2"注册回调后初始化"——持有壳引用（只读消费面）；域命令的注册
    // 经装配描述符在装配期完成（§10.9"装配期一次"），本回调零重复注册。
    m_shell = &shell;
}

std::vector<ui::DomainReadinessItem> KinematicsUiModule::readonlyProjections() const
{
    // kinematics 行（P-UI-6：服从 ui 卡原登记裁决——冻结形状直投零判定）。
    // 在途任务事实：服务缝任务投影非空＝有在途（投影权威在 ui
    // ITaskPresentationModel，此处只读事实不判定）。
    const bool hasActiveTask =
        m_services.taskRows != nullptr && !m_services.taskRows().empty();
    // 输入完整性：会话无任务点数据源＝输入不完整（缺省行不伪造可行性）。
    const bool inputComplete = m_services.taskPoints != nullptr;
    return kinematicsReadinessProjection(inputComplete, hasActiveTask);
}

std::optional<project::CommandEnvelope> KinematicsUiModule::buildDraftCommand(
    const std::string& /*moduleId*/)
{
    // kinematics 无草稿（卡 §12 交接表"本单元无草稿——分析配置用户级"）：
    // §8.5"无可应用草稿的模块返回 nullopt（不产生空修订）"——恒 nullopt
    // 是语义实现而非占位（域内 moduleId 同样无可应用草稿）。
    return std::nullopt;
}

void KinematicsUiModule::bindCommandSubmit(CommandSubmitFn submitFn)
{
    m_pendingSubmit = std::move(submitFn);
    // 后绑定：面板已在——即时转发（前绑定语义与面板 setCommandSubmit 同）。
    // 主面板的命令出口经服务缝注入（构造时拷贝缝——运行期以可变包装承载）。
}

void KinematicsUiModule::bindTextResolver(TextResolver resolve)
{
    m_textResolver = std::move(resolve);
    if (m_panel != nullptr && m_textResolver != nullptr) {
        m_panel->setCommandTitleResolver(m_textResolver);
    }
}

QWidget* KinematicsUiModule::createPanel()
{
    // 服务缝的命令出口绑定（模块级提交出口转发到面板缝——后绑定以最新值
    // 生效：缝为 std::function，此处重建副本捕获转发器）。
    KinPanelServices wired = m_services;
    wired.commandSubmit = [this](const std::string& id) {
        if (m_pendingSubmit != nullptr) {
            m_pendingSubmit(id);  // 装配层出口（ui ICommandRegistry.submit）
        }
    };
    m_panel = new KinematicsPanelWidget(std::move(wired), *m_session);
    if (m_textResolver != nullptr) {
        m_panel->setCommandTitleResolver(m_textResolver);
    }
    return m_panel;
}

QWidget* KinematicsUiModule::createAdvancedConfigPanel()
{
    // 高级面板（§9.8 行 5——advanced=true 挂位；与主面板共享会话态与服务缝
    // ——权威唯一，双面板零独立状态）。
    m_configPanel = new KinematicsConfigPanel(m_services, *m_session);
    return m_configPanel;
}

void KinematicsUiModule::refreshFromSession()
{
    // 会话事件（打开/恢复）后的面板同步——现取重投影（面板未创建＝空操作）。
    if (m_panel != nullptr) {
        m_panel->refreshAll();
        m_panel->refreshTaskArea();
    }
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
