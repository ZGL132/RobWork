/**
 * @file   DomainAssembly.cpp
 * @brief  域插件装配实现（UI-T23 三域集成收口形态＋ASM-UI/ASM-STUDIO 六域
 *         承接）——registrar 装配＋域门面消费＋域模块登记表填充＋单域
 *         失败隔离＋UiText 文案接线＋关于框数据源实装（消费面＝Domain
 *         Assembly.hpp 契约）。前三域（modeling/requirements/kinematics）
 *         随本 TU 编入 ui_plugin 与 studio 两同源目标；三新域（dynamics/
 *         selection/optimization）承接段由 IRD_UI_PLUGIN_EXTRA_DOMAINS
 *         编译定义区分——两宿主目标（ASM-STUDIO 批起 studio 同样携带该
 *         定义）与测试目标直编形态（无定义——直调 assembleExtraDomains）
 *         的装配段单一书写点。
 */

#include "DomainAssembly.hpp"

#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
#include "ExtraDomainAssembly.hpp"  // ASM-UI/ASM-STUDIO 三新域承接（携带本编译定义的宿主目标编入的定义 TU）
#endif

#include <QString>
#include <QWidget>

#include <sdurws/ird/modeling/ObjectTypes.hpp>       // modeling::kRobotDesignObjectType（建模闭包的根身份回填过滤——域知识在闭包内）
#include <sdurws/ird/requirements/ObjectTypes.hpp>   // requirements::kReqSetObjectType（UI-T35——需求回执根身份回填的同构过滤）
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandDescriptor 完整类型（UI-T23 域命令登记——descriptor.commands 规模遍历的 vector 实例化面）
#include <sdurws/ird/ui/IPluginUiModule.hpp>     // ui::IPluginUiModule 完整类型（§11.2）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // createPluginUiRegistrar/RegistrationOutcome（§10.9）
#include <sdurws/ird/ui/UiPorts.hpp>             // ui::IUiAboutDataSource 完整类型（§11.4 数据源端口）

#include "DomainAssemblyShared.hpp"  // 报告行/文案绑定共享原语（ASM-UI 拆出——与 ExtraDomainAssembly 单一书写点）

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 静态白名单（§11.1 冻结八 token——装配顺序＝白名单序；三域在产，其余
/// token 占位登记照常入白名单——§11.1"白名单先行登记占位"）。
const char* const kPluginWhitelist[] = {
    "modeling", "requirements", "kinematics", "trajectory",
    "dynamics", "selection", "optimization", "workflow",
};

/**
 * @brief 关于框数据源（§11.4 IUiAboutDataSource 的装配报告半区实装——
 *        WP-24-T03b 收口形态；退役 HarnessAboutSource 占位）。
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

const DomainPluginAssembly::DomainAssemblyStatus*
DomainPluginAssembly::statusOf(const std::string& domainKey) const
{
    for (const DomainAssemblyStatus& status : statuses) {
        if (status.domainKey == domainKey) {
            return &status;
        }
    }
    return nullptr;
}

const char* const kModelingDockTitle = "IRD 建模";
const char* const kRequirementsDockTitle = "IRD 需求";
const char* const kKinematicsDockTitle = "IRD 运动学";
const char* const kKinematicsAdvancedDockTitle = "IRD 运动学（求解配置）";

std::unique_ptr<DomainPluginAssembly> assembleDomainPlugins(
    QDockWidget& /*pluginDock*/,
    std::vector<std::string>& reportLines)
{
    auto bundle = std::make_unique<DomainPluginAssembly>();

    // ①注册端口（白名单八 token——编译期/装配期常量的运行时载体）。
    bundle->registrar = createPluginUiRegistrar(
        std::vector<std::string>(std::begin(kPluginWhitelist),
                                 std::end(kPluginWhitelist)));

    // ②建模装配（必成域——失败上抛＝宿主装配失败，WP-24-T03 首版语义
    //    保持：模板种子＋文案绑定＋登记＋登记表/状态全套）。
    bundle->modeling = modeling::createModelingPluginAssembly();
    bundle->modeling.bindTextResolver(standardTextResolver());
    // 首版会话种子（generic-6r 真实草稿——L-1/L-2 编辑流可交互）。
    bundle->modeling.seedTemplateSession();
    RegistrationOutcome modelingOutcome = RegistrationOutcome::Ok;
    try {
        modelingOutcome = bundle->registrar->registerPluginUi(
            bundle->modeling.descriptor, *bundle->modeling.module);
    } catch (const std::exception& modelingError) {
        // 必成域失败＝宿主装配失败（首版语义——上抛由宿主插件呈现装载
        // 失败；此处留痕后原样上抛，禁止吞错）。
        emitDomainReportLine(reportLines, "modeling", false, 0, 0,
                             modelingError.what());
        throw;
    }
    {
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "modeling";
        status.ok = (modelingOutcome == RegistrationOutcome::Ok);
        bundle->statuses.push_back(status);
        emitDomainReportLine(reportLines, "modeling", status.ok,
                             bundle->modeling.descriptor.panels.size(),
                             bundle->modeling.descriptor.commands.size(),
                             "outcome=" +
                                 std::to_string(static_cast<int>(modelingOutcome)));
        if (status.ok) {
            // 登记表（acceptance 2——draft.apply 遍历输入）：建模域闭包
            // 全套（锚绑定/锚前移/回执回写——DraftController 挂接形态，
            // UiPlugin 经此回写 onCommandResult("modeling", …)）。
            DomainModuleEntry entry;
            entry.moduleId = modeling::kModuleHandle;
            entry.module = bundle->modeling.module.get();
            // 闭包捕获＝堆对象裸指针（UI-T39 修正——此前捕获 &bundle：栈上
            // unique_ptr 形参的地址，assembleDomainPlugins 返回即悬挂；
            // bindAnchor 每次 draft.apply 都会调用、onCommitted 在首次真实
            // committed 修订时调用——悬挂解引用＝use-after-free，冒烟 tour
            // 二连应用实证崩溃 0xC0000005）。bundle 由 unique_ptr 持有至
            // DomainPluginAssembly 析构，裸指针存活期覆盖闭包调用期。
            entry.bindAnchor =
                [bundleRaw = bundle.get()](
                    const core::BranchId& branch, const core::RevisionId& base) {
                    bundleRaw->modeling.bindSessionAnchor(branch, base);
                };
            entry.onCommitted =
                [bundleRaw = bundle.get()](const project::CommandResult& result) {
                    // 锚前移＋根身份回填（T03b-2——rootId 取自新 HEAD
                    // objectRefs 中建模根 token 条目；域知识在闭包内，
                    // 遍历单元零建模知识）。
                    std::optional<core::ObjectId> rootId;
                    if (result.newHeadState.has_value()) {
                        for (const auto& ref : result.newHeadState->objectRefs) {
                            if (ref.objectTypeToken ==
                                std::string(modeling::kRobotDesignObjectType)) {
                                rootId = ref.objectId;
                                break;
                            }
                        }
                    }
                    if (result.newRevision.has_value()) {
                        bundleRaw->modeling.noteAppliedRevision(
                            *result.newRevision, rootId);
                    }
                };
            // onResult 由宿主补填（DraftController 挂接后的回写闭包——
            // 见 UiPlugin::assembleDomainModules 的补填段；此处留空＝
            // 未挂接形态的诚实缺席）。
            bundle->applyEntries.push_back(std::move(entry));
        }
    }

    // ③需求装配（try/catch——§11.3 多域失败隔离：失败登记状态不中止，
    //    其余域照常）。
    try {
        bundle->requirements = requirements::createRequirementsPluginAssembly();
        // 显式无会话（诚实边界——编辑器会话数据面随需求域宿主会话任务
        // 接续；门面契约：nullptr＝显式无会话，buildDraftCommand 如实
        // nullopt，树供给空集）。
        bundle->requirements->attachEditor(nullptr);
        // 文案绑定：requirements 门面无 bindTextResolver（面板工厂内部
        // 自持文案——O-45 门面"只出线不重写"，装配层零文案注入面）。
        const RegistrationOutcome outcome = bundle->registrar->registerPluginUi(
            bundle->requirements->descriptor, *bundle->requirements->module);
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "requirements";
        status.ok = (outcome == RegistrationOutcome::Ok);
        bundle->statuses.push_back(status);
        emitDomainReportLine(reportLines, "requirements", status.ok,
                             bundle->requirements->descriptor.panels.size(),
                             bundle->requirements->descriptor.commands.size(),
                             "outcome=" + std::to_string(static_cast<int>(outcome)));
        if (status.ok) {
            // 登记表：需求域闭包（锚绑定/锚前移真实；onResult 空＝草稿源
            // 未挂接的诚实缺席——回执投影不回写 DraftController）。闭包
            // 捕获＝堆对象裸指针（UI-T39——建模域同款修正，悬挂分析见上）。
            DomainModuleEntry entry;
            entry.moduleId = "requirements";
            entry.module = bundle->requirements->module.get();
            entry.bindAnchor =
                [bundleRaw = bundle.get()](
                    const core::BranchId& branch, const core::RevisionId& base) {
                    bundleRaw->requirements->bindSessionAnchor(branch, base);
                };
            entry.onCommitted =
                [bundleRaw = bundle.get()](const project::CommandResult& result) {
                    // UI-T35 P1-2（R2 审核整改）：锚前移＋根身份回填——
                    // 扫描新 HEAD objectRefs 的 req-set 条目（建模域
                    // onCommitted 同构先例）。此前传 nullopt＝UI-T29 前
                    // 骨架遗留占位：首应用后根身份被清空→后续应用按
                    // allocateNew 重复建根（恰一根不变量破坏）。
                    std::optional<core::ObjectId> rootId;
                    if (result.newHeadState.has_value()) {
                        for (const auto& ref : result.newHeadState->objectRefs) {
                            if (ref.objectTypeToken ==
                                std::string(requirements::kReqSetObjectType)) {
                                rootId = ref.objectId;
                                break;
                            }
                        }
                    }
                    if (result.newRevision.has_value()) {
                        bundleRaw->requirements->noteAppliedRevision(
                            *result.newRevision, rootId);
                    }
                };
            bundle->applyEntries.push_back(std::move(entry));
        }
    } catch (const std::exception& requirementsError) {
        // §11.3 失败隔离（acceptance 3 注入面）：登记失败状态＋稳定码
        // 报告行，requirements 缺席，其余域照常。
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "requirements";
        status.ok = false;
        status.detail = requirementsError.what();
        bundle->statuses.push_back(status);
        emitDomainReportLine(reportLines, "requirements", false, 0, 0,
                             status.detail);
    }

    // ④运动学装配（try/catch——同上；覆盖评估执行通道服务缝由
    // UiPlugin::wireKinematicsEvaluationChannel 经公共通道值面注入
    // （UI-T64——F-490① 上游批），本装配面零私有头、零缝构造——
    // 其余缝维持降级语义，见头文件诚实边界注）。
    try {
        bundle->kinematics = kinematics::createKinematicsPluginAssembly();
        bundle->kinematics->bindTextResolver(standardTextResolver());
        const RegistrationOutcome outcome = bundle->registrar->registerPluginUi(
            bundle->kinematics->descriptor, *bundle->kinematics->module);
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "kinematics";
        status.ok = (outcome == RegistrationOutcome::Ok);
        bundle->statuses.push_back(status);
        emitDomainReportLine(reportLines, "kinematics", status.ok,
                             bundle->kinematics->descriptor.panels.size(),
                             bundle->kinematics->descriptor.commands.size(),
                             "outcome=" + std::to_string(static_cast<int>(outcome)));
        if (status.ok) {
            // 登记表：运动学域闭包全空（无草稿域——buildDraftCommand 恒
            // nullopt 的接口面消费；遍历对其记 NoDraft 行，零域知识）。
            DomainModuleEntry entry;
            entry.moduleId = "kinematics";
            entry.module = bundle->kinematics->module.get();
            bundle->applyEntries.push_back(std::move(entry));
        }
    } catch (const std::exception& kinematicsError) {
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "kinematics";
        status.ok = false;
        status.detail = kinematicsError.what();
        bundle->statuses.push_back(status);
        emitDomainReportLine(reportLines, "kinematics", false, 0, 0,
                             status.detail);
    }

#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
    // ⑤~⑦dynamics/selection/optimization 三新域承接（ASM-UI 收口批——
    // asm-plug 预登记边 IRD_TARGET_LEVEL_EDGES 三条的运行期消费）：门面
    // 创建→文案绑定→registerWithHostRegistrar（真实 IPluginUiRegistrar
    // 传经三域装配激活路径——UI-T23 同机制）→登记表/状态/报告行，详见
    // ExtraDomainAssembly TU。该 TU 编入携带本定义的两同源宿主目标
    // （sdurws_ird_ui_plugin——ASM-UI 批；sdurws_ird_studio——ASM-STUDIO
    // 承接批 2026-10-10 起，studio→三新域三条 unit 级白名单边随该批增
    // 登后的编入，六域在正式主程序装配齐备，登记 ui.md §13 ASM-UI/
    // ASM-STUDIO 行）。产物容
    // 器挂 extraAssemblies（类型擦除——三域类型依赖收敛在承接 TU 内，
    // 本 TU 所在两目标共享的本结构只见句柄）；三新域均按 §11.3 隔离域
    // 承载（失败登记状态不中止，selection 的注册被宿主权威拒绝＝P-SEL-3
    // 待裁决面的诚实钉扎，不本地绕过）。
    bundle->extraAssemblies = assembleExtraDomains(*bundle, reportLines);
#endif

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

QWidget* requirementsPanelWidget(const DomainPluginAssembly& bundle)
{
    // 失败隔离缺席＝nullptr（宿主跳过挂位，占位呈现由共享树承担——
    // §11.3；不虚构空面板）。
    if (!bundle.requirements.has_value()
        || bundle.requirements->descriptor.panels.empty()) {
        return nullptr;
    }
    const ui::PanelRegistration& panel =
        bundle.requirements->descriptor.panels.front();
    return panel.factory();
}

QWidget* kinematicsPanelWidget(const DomainPluginAssembly& bundle)
{
    // 主面板＝descriptor.panels 首条（装配规则：行 1~4 合一 Tab 容器——
    // 门面注释原文）。失败隔离缺席＝nullptr（同需求域口径）。
    if (!bundle.kinematics.has_value()
        || bundle.kinematics->descriptor.panels.empty()) {
        return nullptr;
    }
    const ui::PanelRegistration& panel =
        bundle.kinematics->descriptor.panels.front();
    return panel.factory();
}

QWidget* kinematicsAdvancedPanelWidget(const DomainPluginAssembly& bundle)
{
    // 高级面板＝descriptor.panels 第二条（装配规则：行 5 求解配置
    // advanced=true 独立登记——门面注释原文；缺席＝nullptr）。
    if (!bundle.kinematics.has_value()
        || bundle.kinematics->descriptor.panels.size() < 2) {
        return nullptr;
    }
    const ui::PanelRegistration& panel = bundle.kinematics->descriptor.panels[1];
    return panel.factory();
}

ui::IModuleDraftSource& modelingDraftSource(DomainPluginAssembly& bundle)
{
    return bundle.modeling.modelingDraftSource();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
