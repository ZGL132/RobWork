/**
 * @file   DomainAssemblyShared.hpp
 * @brief  宿主域装配面的内部共享件（ASM-UI 拆出——DomainAssembly.cpp 与
 *         ExtraDomainAssembly.cpp 两 TU 的共用报告行/文案绑定原语；防
 *         两份副本漂移的单一书写点）。
 *
 * ★ 文件域：plugin/ 内部头（非公共交付面——不进 include/，不跨单元；
 *   仅本单元装配 TU 消费，与 plugin/HostPresentationAdapters.hpp 同域
 *   形态）。ird_gates 产品面 include 扫描域为 include/**＋src/**，
 *   plugin/ 面的 Qt 使用合法（R-3 例外承载面）。
 *
 * 设计依据：UI-T23 装配报告行格式（"domain assembly: plugin=… ok=…
 * panels=… commands=…"——Dev 通道观测面）＋§11.3 失败隔离稳定码
 * UI-PLUGIN-ASSEMBLY-FAILED＋UX-02 resolveText 唯一文案出口。
 */

#ifndef IRD_UI_PLUGIN_DOMAINASSEMBLYSHARED_HPP
#define IRD_UI_PLUGIN_DOMAINASSEMBLYSHARED_HPP

#include <functional>
#include <QString>
#include <string>
#include <vector>

#include <sdurws/ird/ui/UiText.hpp>  // ui::resolveText（§3.5 唯一文案出口——UX-02）

namespace sdurws {
namespace ird {
namespace ui {

/// 装配失败隔离的稳定诊断码文本（§11.3 冻结码——失败域报告行与共享树
/// 占位文案共用；登记面 diagnostics StableCodeRegistry，此处只引用
/// token 文本，不加工语义——SA-12 权威分工）。
inline constexpr const char* kAssemblyFailedCode = "UI-PLUGIN-ASSEMBLY-FAILED";

/// UiText 文案解析的标准绑定（多域共用——resolveText 唯一出口；解析空/
/// 失败回退键名原文——呈现面不空洞，建模装配既有先例逐字同源）。
/// QString 版＝modeling/kinematics 门面 bindTextResolver 的签名面
/// （ModelingPluginAssembly.hpp:94 同款——两代门面的 resolver 签名分叉
/// 如实承载：QString 版承载既有消费，std::string 版承载 ASM-UI 三新域）。
inline std::function<QString(const std::string&)> standardTextResolver()
{
    return [](const std::string& key) {
        const std::string text = resolveText(key);
        return QString::fromStdString(text.empty() ? key : text);
    };
}

/// UiText 文案解析的标准绑定（std::string 版——dynamics/selection/
/// optimization 三新域门面 bindTextResolver 的签名面
/// 〔如 DynamicsPluginAssembly.hpp bindTextResolver 注〕；同一 resolveText
/// 出口同一回退语义——零第二文案语义源，UX-02）。
inline std::function<std::string(const std::string&)> standardTextResolverUtf8()
{
    return [](const std::string& key) {
        const std::string text = resolveText(key);
        return text.empty() ? key : text;
    };
}

/**
 * @brief 登记/报告行的统一输出（域装配的收尾步骤——ok 与失败两形态
 *        逐域一行；失败行携带稳定码文本——§11.3 失败隔离的可观测面）。
 *
 * @param reportLines [out] 报告行收集器（宿主日志/状态栏呈现——Dev 通道）
 * @param domainKey [in] 域键（＝白名单 token，如 "dynamics"）
 * @param ok [in] 装配＋登记是否成功（RegistrationOutcome==Ok）
 * @param panels [in] 已登记面板数（描述符面计数——报告观测值）
 * @param commands [in] 已登记命令数（描述符面计数——报告观测值）
 * @param failureDetail [in] 失败原因（异常 what／outcome 数值——Dev 排障）
 */
inline void emitDomainReportLine(std::vector<std::string>& reportLines,
                                 const std::string& domainKey,
                                 bool ok,
                                 std::size_t panels,
                                 std::size_t commands,
                                 const std::string& failureDetail)
{
    if (ok) {
        reportLines.push_back("domain assembly: plugin=" + domainKey +
                              " ok=1 panels=" + std::to_string(panels) +
                              " commands=" + std::to_string(commands));
    } else {
        // §11.3 失败隔离：登记失败不中止启动——稳定码＋原因一行留痕
        // （该域不进关于框交集呈现——§11.4 报告对位充实以 ok 行为准）。
        reportLines.push_back("domain assembly: plugin=" + domainKey + " " +
                              std::string(kAssemblyFailedCode) + " detail=" +
                              failureDetail);
    }
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_DOMAINASSEMBLYSHARED_HPP
