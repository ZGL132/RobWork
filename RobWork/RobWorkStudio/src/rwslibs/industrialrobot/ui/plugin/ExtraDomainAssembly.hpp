/**
 * @file   ExtraDomainAssembly.hpp
 * @brief  三新域（dynamics/selection/optimization）的宿主装配承接面
 *         （ASM-UI 收口批）——把三域装配门面的激活注册接进宿主装配
 *         序列（assembleDomainPlugins 的尾段），完成白名单预登记边
 *         （IRD_TARGET_LEVEL_EDGES 三条 ui_plugin→三域 _plugin）的
 *         运行期消费。
 *
 * 设计依据：
 *   - units/ui.md §11.1（静态白名单八 token——装配顺序＝白名单序；
 *     §10.9 装配期一次 registerPluginUi）；
 *   - ASM-PLUG 收口批预登记（ird_gates_whitelist.cmake
 *     IRD_TARGET_LEVEL_EDGES 注——"ui_plugin 链接段源面修改归 ui 宿主
 *     面批次"）与三域装配门面的激活函数契约
 *     （dynamics/selection/optimization assembly/ 公共头
 *     registerWithHostRegistrar——域侧零本地判定，校验全走宿主实现）；
 *   - UI-T23 先例（DomainAssembly.cpp ②~④段——宿主装配层把真实
 *     IPluginUiRegistrar 传经域装配路径的同机制；本 TU 为其三新域
 *     推广，消费面仅三域公共 assembly/ 门面头，域私有头零触碰——R-2）。
 *
 * ★ 落位边界（诚实登记，登记 units/ui.md §13 ASM-UI 行）：
 *   1. **本头与实现 TU 仅编入/仅被 sdurws_ird_ui_plugin 目标消费**
 *      （集成树专属 MODULE）。正式产品主程序 sdurws_ird_studio 同源消
 *      费 DomainAssembly.* 但**不见本头**（include 收敛在 DomainAssembly
 *      .cpp 的 IRD_UI_PLUGIN_EXTRA_DOMAINS 编译定义段内）——studio 的
 *      装配序列维持 UI-T23 三域不变：studio→dynamics/selection/
 *      optimization 三条 unit 级白名单边未登记（本批白名单零新边约束，
 *      asm-plug 预登记仅覆盖 ui_plugin 目标级三条），studio 的三新域
 *      承接随该三条边的登记批次收口（登记义务见单元卡）。三域门面头
 *      的 include 面由此收敛（studio 编译单元无三域头路径——不链接三
 *      域 plugin 即无传递 include 面）。
 *   2. **三域产物容器独立于 DomainPluginAssembly**（ExtraDomainAssem
 *      blies——本头定义；经 shared_ptr<void> 类型擦除挂 bundle.
 *      extraAssemblies）：.DomainPluginAssembly 被 studio 同源消费，
 *      携带三域类型成员会把三域头拖进 studio 编译面——容器类型与本头
 *      同收敛，bundle 只见擦除句柄（析构删除器在构造点绑定实型）。
 *   3. **selection 注册被宿主权威拒绝的诚实钉扎**（asm-plug 验收建议
 *      级 1 保持不本地绕过）：selection 回填命令 token
 *      apply-device-backfill 为无点 kebab 词形（P-SEL-3/P-PR-9 待所有
 *      者裁决），不满足 ui.md §7.2 命令 id 点分句法——registerWithHost
 *      Registrar 如实透传 RegistrationOutcome::InvalidDescriptor，本
 *      承接面按 §11.3 失败隔离登记（status.ok=false＋稳定码报告行），
 *      **零 token 改写、零本地判定**（裁决后翻译产物自然过校验）。
 *
 * 线程模型：全部函数 UI 线程（initialize 装配线程——§10.9 同口径）。
 */
#ifndef IRD_UI_PLUGIN_EXTRADOMAINASSEMBLY_HPP
#define IRD_UI_PLUGIN_EXTRADOMAINASSEMBLY_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp>          // dynamics 装配门面（公共 assembly/ 头——R-2 零域私有头）
#include <sdurws/ird/optimization/OptimizationPluginAssembly.hpp>  // optimization 装配门面（同上）
#include <sdurws/ird/selection/SelectionPluginAssembly.hpp>        // selection 装配门面（同上）

#include "DomainAssembly.hpp"  // DomainPluginAssembly（bundle 聚合体——宿主持有）

namespace sdurws {
namespace ird {
namespace ui {

/**
 * @brief 三新域装配产物容器（ASM-UI——类型随本头收敛在 ui_plugin 编译
 *        面；DomainPluginAssembly 经 shared_ptr<void> 持有本容器，存活
 *        期＝bundle 存活期〔宿主持有至壳拆除〕——registrar 弱引用与
 *        登记表 module 裸指针的存活期由此保证，§10.9 所有权行）。
 *
 * 产物形态与前三域一致（§11.3 隔离域值语义）：装配异常缺席＝nullopt；
 * selection 注册被宿主权威拒绝（InvalidDescriptor——P-SEL-3 待裁决面）
 * ＝装配在册但登记未 Ok（status.ok=false，报告不入列）。
 */
struct ExtraDomainAssemblies {
    /// 动力学装配产物（缺席＝装配异常隔离）。
    std::optional<dynamics::DynamicsPluginAssembly> dynamics;
    /// 选型装配产物（预期登记未 Ok——P-SEL-3 待裁决；缺席＝装配异常）。
    std::optional<selection::SelectionPluginAssembly> selection;
    /// 优化装配产物（缺席＝装配异常隔离）。
    std::optional<optimization::OptimizationPluginAssembly> optimization;
};

/**
 * @brief 执行三新域插件装配（ASM-UI——initialize 装配期恰调一次，仅在
 *        sdurws_ird_ui_plugin 目标的 DomainAssembly.cpp 尾段被调用）。
 *
 * 步骤（白名单序 dynamics→selection→optimization；单域失败隔离——
 * §11.3，与 DomainAssembly.cpp ③④段同构）：
 *   ①dynamics：门面→UiText 文案绑定→registerWithHostRegistrar（真实
 *     registrar 传经域装配激活路径）→登记表/状态；
 *   ②selection：门面→文案绑定→registerWithHostRegistrar→**预期
 *     InvalidDescriptor**（P-SEL-3 待裁决面——诚实登记不本地绕过）；
 *   ③optimization：门面→文案绑定→registerWithHostRegistrar→登记表/
 *     状态（无命令域——描述符零命令字段的诚实缺席随门面承载）。
 *   ④报告行输出（逐域一行，失败域附 UI-PLUGIN-ASSEMBLY-FAILED 稳定码
 *     ——与前三域同一 emitDomainReportLine 通道）。
 *
 * @param bundle [in,out] 装配产物（registrar 须已创建——assembleDomain
 *        Plugins 第①步的产物；statuses/applyEntries 由本函数填充，
 *        产物容器经返回值交调用方挂 bundle.extraAssemblies）
 * @param reportLines [out] 装配报告行（宿主日志/状态栏呈现——Dev 通道）
 * @return 三新域产物容器（shared_ptr 实型删除器——调用方挂
 *         bundle.extraAssemblies 后存活期随 bundle；registry 弱引用与
 *         已入表 module 裸指针的存活期由此保证）
 *
 * @throws std::invalid_argument bundle.registrar 为空（调用序违约——
 *         fail-fast 不虚构登记）；三新域各自的 create/register 异常按
 *         §11.3 隔离域 try/catch 承载（不上抛——失败可见面＝status.ok
 *         =false＋稳定码报告行）
 */
std::shared_ptr<ExtraDomainAssemblies> assembleExtraDomains(
    DomainPluginAssembly& bundle, std::vector<std::string>& reportLines);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_EXTRADOMAINASSEMBLY_HPP
