/**
 * @file   DomainAssembly.cpp
 * @brief  域插件装配实现（WP-24-T03b 收口形态）——registrar 装配＋建模面板
 *         工厂消费＋UiText 文案接线＋关于框数据源实装（消费面＝
 *         DomainAssembly.hpp 契约）。
 */

#include "DomainAssembly.hpp"

#include <QString>
#include <QWidget>

#include <sdurws/ird/ui/IPluginUiModule.hpp>     // ui::IPluginUiModule 完整类型（§11.2）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // createPluginUiRegistrar/RegistrationOutcome（§10.9）
#include <sdurws/ird/ui/UiPorts.hpp>             // ui::IUiAboutDataSource 完整类型（§11.4 数据源端口）
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

/**
 * @brief 关于框数据源（§11.4 IUiAboutDataSource 的装配报告半区实装——
 *        WP-24-T03b 收口；退役 HarnessAboutSource 占位）。
 *
 * 纪律：assemblyReports() 现取现拼（转调 registrar 报告表——ACC5 零缓存，
 * 关于框每次打开现取装配事实）；versionBaseline 恒 available=false（版本
 * 呈现值源未接线——"未装载"占位如实，不虚构基线值，NFR-DEP-05 呈现纪律）。
 * 线程：UI 线程调用（§3.4 M-1 消费点——help.about 处理器）。
 */
class RegistrarAboutSource final : public IUiAboutDataSource {
public:
    /// @param registrar [in] 注册端口（非 owning——存活期由 bundle 保证）
    explicit RegistrarAboutSource(const IPluginUiRegistrar* registrar)
        : m_registrar(registrar)
    {
    }

    std::vector<PluginAssemblyReport> assemblyReports() const override
    {
        // 现取（不缓存——§11.4"打开时现取现用"消费形态；未登记＝空集，
        // 关于框退化为白名单占位行——§11.4 合法形态，不虚构装配事实）。
        if (m_registrar == nullptr) {
            return {};
        }
        return m_registrar->assemblyReports();
    }

    AboutVersionBaseline versionBaseline() const override
    {
        // available=false＝基线呈现值未装载（WP-24-T01 基线文档已落盘，
        // 呈现值注入面随正式装配任务接线——不虚构版本值）。
        return AboutVersionBaseline{};
    }

private:
    const IPluginUiRegistrar* m_registrar;  ///< 注册端口（非 owning——见类注释）
};

}  // namespace

DomainPluginAssembly::~DomainPluginAssembly() = default;

const char* const kModelingDockTitle = "IRD 建模";

std::unique_ptr<DomainPluginAssembly> assembleDomainPlugins(
    QDockWidget& /*pluginDock*/,
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

    // ④首版会话种子（generic-6r 真实草稿——L-1/L-2 编辑流可交互）。
    //    （收口变更：首版步骤④的"提交出口→状态栏受理反馈"桩退役——域
    //    命令改经宿主接 content 命令注册表路由执行（§7.2），见 UiPlugin。）
    bundle->modeling.seedTemplateSession();

    // ⑤登记（§10.9 装配期一次；失败隔离——不抛不中止，报告行留痕）。
    const RegistrationOutcome outcome = bundle->registrar->registerPluginUi(
        bundle->modeling.descriptor, *bundle->modeling.module);
    for (const PluginAssemblyReport& report : bundle->registrar->assemblyReports()) {
        reportLines.push_back("domain assembly: plugin=" + report.pluginId +
                              " ok=" + (report.ok ? "1" : "0") +
                              " panels=" + std::to_string(report.panelsLoaded) +
                              " commands=" + std::to_string(report.commandsRegistered));
    }
    if (outcome != RegistrationOutcome::Ok) {
        // §11.3 失败隔离：登记失败不中止启动——报告行留痕（该插件不进
        // 关于框交集呈现——§11.4 报告对位充实以 ok 行为准）。
        reportLines.push_back("domain assembly: modeling outcome=" +
                              std::to_string(static_cast<int>(outcome)));
    }
    return bundle;
}

ui::IUiAboutDataSource* bundleAboutSource(DomainPluginAssembly& bundle)
{
    // 适配器惰性构造入 bundle（存活期随 bundle——宿主持有至壳拆除；
    // 只读消费 registrar，装配完成后宿主取用）。
    if (!bundle.aboutSource) {
        bundle.aboutSource = std::make_unique<RegistrarAboutSource>(
            bundle.registrar.get());
    }
    return bundle.aboutSource.get();
}

QWidget* modelingPanelWidget(const DomainPluginAssembly& bundle)
{
    const ui::PanelRegistration& panel = bundle.modeling.descriptor.panels.front();
    return panel.factory();
}

ui::IModuleDraftSource& modelingDraftSource(DomainPluginAssembly& bundle)
{
    return bundle.modeling.modelingDraftSource();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
