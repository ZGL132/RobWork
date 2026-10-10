/**
 * @file   ExtraDomainAssembly.cpp
 * @brief  三新域（dynamics/selection/optimization）宿主装配承接的实现
 *         （ASM-UI 收口批——消费面＝ExtraDomainAssembly.hpp 契约）。
 *
 * ★ 编入目标：sdurws_ird_ui_plugin（集成树 MODULE——白名单
 *   IRD_TARGET_LEVEL_EDGES 三条预登记边的运行期消费 TU；ASM-UI 批）与
 *   sdurws_ird_studio（正式产品主程序——ASM-STUDIO 承接批 2026-10-10
 *   起，studio→三新域三条 unit 级白名单边随该批增登后的编入；六域在
 *   正式主程序装配齐备）；另有 sdurws_ird_ui_contract_test 测试目标同
 *   源直编（ASM-STUDIO 批分布钉扎消账——六域聚合注册断言的无人值守
 *   直测通道）。边界详见头文件"落位边界"。
 */

#include "ExtraDomainAssembly.hpp"

#include <exception>
#include <functional>
#include <stdexcept>
#include <utility>

#include "DomainAssemblyShared.hpp"  // 报告行/文案绑定共享原语（单一书写点——与 DomainAssembly.cpp 同一 emitDomainReportLine/standardTextResolverUtf8）

namespace sdurws {
namespace ird {
namespace ui {

std::shared_ptr<ExtraDomainAssemblies> assembleExtraDomains(
    DomainPluginAssembly& bundle, std::vector<std::string>& reportLines)
{
    // 前置：registrar 须已创建（assembleDomainPlugins 第①步产物——
    // 空指针＝调用序违约，fail-fast 不虚构登记）。
    if (bundle.registrar == nullptr) {
        throw std::invalid_argument(
            "assembleExtraDomains: bundle.registrar 未创建（调用序违约）");
    }

    // 产物容器（shared_ptr 实型删除器——挂 bundle.extraAssemblies 后
    // 存活期随 bundle：三新域门面的 registrar 弱引用与已入登记表的
    // module 裸指针都以容器存活期为担保，§10.9 所有权行）。
    auto products = std::make_shared<ExtraDomainAssemblies>();

    // ①dynamics（§11.3 隔离域——失败登记状态不中止其余域）：门面→
    //    文案绑定→registerWithHostRegistrar（真实 IPluginUiRegistrar
    //    传经域装配激活路径——"运行期宿主传参"的本批承接点）。
    try {
        products->dynamics = dynamics::createDynamicsPluginAssembly();
        products->dynamics->bindTextResolver(standardTextResolverUtf8());
        const RegistrationOutcome outcome = dynamics::registerWithHostRegistrar(
            *products->dynamics, bundle.registrar.get());
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "dynamics";
        status.ok = (outcome == RegistrationOutcome::Ok);
        bundle.statuses.push_back(status);
        emitDomainReportLine(reportLines, "dynamics", status.ok,
                             products->dynamics->descriptor.panels.size(),
                             products->dynamics->descriptor.commands.size(),
                             "outcome=" +
                                 std::to_string(static_cast<int>(outcome)));
        if (status.ok) {
            // 登记表（draft.apply 遍历输入）：dynamics 无草稿域（域侧
            // uiModule 的 buildDraftCommand 恒 nullopt——asm-plug 契约
            // 测试钉扎面），闭包全空条目＝遍历对其记 NoDraft 行，零域
            // 知识（kinematics 先例同构）。module 裸指针存活期＝容器
            // 存活期（挂 bundle 至壳拆除）。
            DomainModuleEntry entry;
            entry.moduleId = "dynamics";
            entry.module = products->dynamics->uiModule();
            bundle.applyEntries.push_back(std::move(entry));
        }
    } catch (const std::exception& dynamicsError) {
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "dynamics";
        status.ok = false;
        status.detail = dynamicsError.what();
        bundle.statuses.push_back(status);
        emitDomainReportLine(reportLines, "dynamics", false, 0, 0,
                             status.detail);
    }

    // ②selection（隔离域）：同上形态。★ RUL-TOK 命名词形裁决销案批
    //    （2026-10-10，units/ui.md §16.7 v1.92）后注册 Ok——selection
    //    域侧已把回填命令 ui 命令 id 修订为点分 selection.apply-device-
    //    backfill（§7.2 点分句法），翻译函数零改动自然过宿主校验（原
    //    "预期 InvalidDescriptor"诚实钉扎兑现翻转；六域注册全 Ok 形态，
    //    本承接面零 token 改写零本地判定的纪律保持）。
    try {
        products->selection = selection::createSelectionPluginAssembly();
        products->selection->bindTextResolver(standardTextResolverUtf8());
        const RegistrationOutcome outcome = selection::registerWithHostRegistrar(
            *products->selection, bundle.registrar.get());
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "selection";
        status.ok = (outcome == RegistrationOutcome::Ok);
        bundle.statuses.push_back(status);
        emitDomainReportLine(reportLines, "selection", status.ok,
                             products->selection->descriptor.panels.size(),
                             products->selection->descriptor.commands.size(),
                             "outcome=" +
                                 std::to_string(static_cast<int>(outcome)));
        if (status.ok) {
            DomainModuleEntry entry;
            entry.moduleId = "selection";
            entry.module = products->selection->uiModule();
            bundle.applyEntries.push_back(std::move(entry));
        }
    } catch (const std::exception& selectionError) {
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "selection";
        status.ok = false;
        status.detail = selectionError.what();
        bundle.statuses.push_back(status);
        emitDomainReportLine(reportLines, "selection", false, 0, 0,
                             status.detail);
    }

    // ③optimization（隔离域）：同上形态（描述符零命令字段——R1 域命令
    //    词表未随卡面登记的诚实缺席，ASM-PLUG 收口批门面注原文；登记表
    //    条目同 dynamics/selection 无草稿形态）。
    try {
        products->optimization = optimization::createOptimizationPluginAssembly();
        products->optimization->bindTextResolver(standardTextResolverUtf8());
        const RegistrationOutcome outcome =
            optimization::registerWithHostRegistrar(*products->optimization,
                                                    bundle.registrar.get());
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "optimization";
        status.ok = (outcome == RegistrationOutcome::Ok);
        bundle.statuses.push_back(status);
        // 报告行命令数恒 0：optimization 描述符**无命令字段**（不预建
        // 占位，NFR-MNT-04）。
        emitDomainReportLine(reportLines, "optimization", status.ok,
                             products->optimization->descriptor.panels.size(),
                             0,
                             "outcome=" +
                                 std::to_string(static_cast<int>(outcome)));
        if (status.ok) {
            DomainModuleEntry entry;
            entry.moduleId = "optimization";
            entry.module = products->optimization->uiModule();
            bundle.applyEntries.push_back(std::move(entry));
        }
    } catch (const std::exception& optimizationError) {
        DomainPluginAssembly::DomainAssemblyStatus status;
        status.domainKey = "optimization";
        status.ok = false;
        status.detail = optimizationError.what();
        bundle.statuses.push_back(status);
        emitDomainReportLine(reportLines, "optimization", false, 0, 0,
                             status.detail);
    }

    return products;
}

// =====================================================================
// ASM-PANEL 收口批（2026-10-10，所有者授权装配批次）——三新域面板取件
// 实现（消费面＝ExtraDomainAssembly.hpp 尾部三声明；UiPlugin buildDock
// Body 挂位段调用——ui_plugin/studio 两宿主目标同源 TU，单一书写点）。
// 形态先例：DomainAssembly.cpp 的 modelingPanelWidget/
// requirementsPanelWidget（前三域取件——bundle 成员直取 descriptors
// panels.front().factory）；本组函数的容器形态差异见各函数注。
// =====================================================================

/// 动力学面板 Dock 标题（chrome 字面——UiText plugin.dynamics.title 值
/// "动力学"同源，见头注；定义点＝取件函数同 TU 单一书写）。
const char* const kDynamicsDockTitle = "IRD 动力学";
/// 选型面板 Dock 标题（同上——plugin.selection.title 值"选型"同源）。
const char* const kSelectionDockTitle = "IRD 选型";
/// 优化面板 Dock 标题（同上——plugin.optimization.title 值"优化"同源）。
const char* const kOptimizationDockTitle = "IRD 优化";

QWidget* dynamicsPanelWidget(const ExtraDomainAssemblies& extra)
{
    // 失败隔离缺席＝nullptr（§11.3——宿主跳过挂位，占位呈现由共享树
    // 承担；不虚构空面板不抛异常）。三域面板记录恰一条（主面板——
    // 装配门面工厂注释"面板面形态"；空集＝描述符结构违约的防御面，
    // 与 requirementsPanelWidget 同款口径）。
    if (!extra.dynamics.has_value()
        || extra.dynamics->descriptor.panels.empty()) {
        return nullptr;
    }
    return extra.dynamics->descriptor.panels.front().factory();
}

QWidget* selectionPanelWidget(const ExtraDomainAssemblies& extra)
{
    // 缺席/空面板防御语义同上（selection 注册 Ok——RUL-TOK 销案兑现，
    // 正常态与 dynamics/optimization 一致）。
    if (!extra.selection.has_value()
        || extra.selection->descriptor.panels.empty()) {
        return nullptr;
    }
    return extra.selection->descriptor.panels.front().factory();
}

QWidget* optimizationPanelWidget(const ExtraDomainAssemblies& extra)
{
    // 缺席/空面板防御语义同上。
    if (!extra.optimization.has_value()
        || extra.optimization->descriptor.panels.empty()) {
        return nullptr;
    }
    return extra.optimization->descriptor.panels.front().factory();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
