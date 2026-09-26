/**
 * @file   DomainAssembly.cpp
 * @brief  域插件首版装配实现（WP-24-T03）——registrar 装配＋建模面板工厂
 *         消费＋UiText 文案接线（消费面＝DomainAssembly.hpp 契约）。
 */

#include "DomainAssembly.hpp"

#include <QString>
#include <QWidget>

#include <sdurws/ird/ui/IPluginUiModule.hpp>     // ui::IPluginUiModule 完整类型（§11.2）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // createPluginUiRegistrar/RegistrationOutcome（§10.9）
#include <sdurws/ird/ui/UiText.hpp>              // ui::resolveText（§3.5 唯一文案出口——UX-02）

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 静态白名单（§11.1 冻结八 token——装配顺序＝白名单序；首版仅 modeling
/// 在产，其余 token 占位登记照常入白名单——§11.1"白名单先行登记占位"）。
const char* const kPluginWhitelist[] = {
    "modeling", "requirements", "kinematics", "trajectory",
    "dynamics", "selection", "optimization", "workflow",
};

}  // namespace

const char* const kModelingDockTitle = "IRD 建模";

std::unique_ptr<DomainPluginAssembly> assembleDomainPlugins(
    QDockWidget& /*pluginDock*/,
    std::function<void(const std::string&)> statusFeedback,
    std::vector<std::string>& reportLines)
{
    auto bundle = std::make_unique<DomainPluginAssembly>();

    // ①注册端口（白名单八 token——编译期/装配期常量的运行时载体）。
    bundle->registrar = createPluginUiRegistrar(
        std::vector<std::string>(std::begin(kPluginWhitelist),
                                 std::end(kPluginWhitelist)));

    // ②建模装配产物（门面——descriptor/module/面板工厂一次到位）。
    bundle->modeling = modeling::createModelingPluginAssembly();

    // ③文案解析绑定（ui::resolveText 唯一出口——按钮呈现工程用语中文；
    //    解析空/失败回退键名原文——呈现面不空洞，见面板 resolver 实现）。
    bundle->modeling.bindTextResolver([](const std::string& key) {
        const std::string text = resolveText(key);
        return QString::fromStdString(text.empty() ? key : text);
    });

    // ④命令提交出口绑定（首版可见反馈面——宿主状态栏瞬态消息）：域命令
    //    的执行语义（模板创建/导入向导/权威切换……经处理器族＋project
    //    管线）随收口任务接线；此处如实呈现"已受理＋待接线"，不虚构执行
    //    成功（UX-02/ERR-01 同源纪律）。
    bundle->modeling.bindCommandSubmit(
        [statusFeedback](const ui::CommandId& id) {
            if (!statusFeedback) { return; }
            statusFeedback("已受理域命令 " + id +
                           "（域执行面随装配收口任务接线）");
        });

    // ⑤首版会话种子（generic-6r 真实草稿——L-1/L-2 编辑流可交互）。
    bundle->modeling.seedTemplateSession();

    // ⑥登记（§10.9 装配期一次；失败隔离——不抛不中止，报告行留痕）。
    const RegistrationOutcome outcome = bundle->registrar->registerPluginUi(
        bundle->modeling.descriptor, *bundle->modeling.module);
    for (const PluginAssemblyReport& report : bundle->registrar->assemblyReports()) {
        reportLines.push_back("domain assembly: plugin=" + report.pluginId +
                              " ok=" + (report.ok ? "1" : "0") +
                              " panels=" + std::to_string(report.panelsLoaded) +
                              " commands=" + std::to_string(report.commandsRegistered));
    }
    if (outcome != RegistrationOutcome::Ok) {
        // §11.3 失败隔离：登记失败不中止启动——面板工厂仍可消费（降级面
        // 随收口任务接 UI-PLUGIN-ASSEMBLY-FAILED 占位；首版如实留痕）。
        reportLines.push_back("domain assembly: modeling outcome=" +
                              std::to_string(static_cast<int>(outcome)));
    }
    return bundle;
}

QWidget* modelingPanelWidget(const DomainPluginAssembly& bundle)
{
    const ui::PanelRegistration& panel = bundle.modeling.descriptor.panels.front();
    return panel.factory();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
