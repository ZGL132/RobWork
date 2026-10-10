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
 * ★ 落位边界（诚实登记，登记 units/ui.md §13 ASM-UI/ASM-STUDIO 行）：
 *   1. **本头与实现 TU 编入两同源宿主目标**：sdurws_ird_ui_plugin（集成
 *      树 MODULE——ASM-UI 批承接）与 sdurws_ird_studio（正式产品主程序
 *      ——ASM-STUDIO 承接批 2026-10-10 起编入：studio→dynamics/
 *      selection/optimization 三条 unit 级白名单边已随该批增登
 *      〔ird_gates_whitelist.cmake IRD_ALLOWED_UNIT_EDGES，DTB §2.27
 *      studio 承接批预登记行的落地〕，studio 目标以同款
 *      IRD_UI_PLUGIN_EXTRA_DOMAINS 编译定义使 DomainAssembly.cpp 尾段
 *      承接段生效——六域在正式主程序装配齐备，两同源目标装配序列保持
 *      单一书写点防漂移）。另有第三消费面＝sdurws_ird_ui_contract_test
 *      的测试目标同源直编（ASM-STUDIO 批分布钉扎消账——六域聚合注册
 *      断言的无人值守直测通道，acc/asm-ui/1 建议级 1；HostCompilePort
 *      被测 TU 同源直编先例形态）。ui_plugin 为 MODULE 的"测试目标不可
 *      链接"结构性限制不波及本 TU（直编≠链接）。
 *   2. **三域产物容器独立于 DomainPluginAssembly**（ExtraDomainAssem
 *      blies——本头定义；经 shared_ptr<void> 类型擦除挂 bundle.
 *      extraAssemblies）：DomainPluginAssembly 是两同源宿主目标共享
 *      的 bundle 结构，携带三域类型成员会把三域头拖进无定义 TU 的编译
 *      面——容器类型与本头同收敛，bundle 只见擦除句柄（析构删除器在
 *      构造点绑定实型）。
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
 * @brief 执行三新域插件装配（ASM-UI——initialize 装配期恰调一次，在
 *        携带 IRD_UI_PLUGIN_EXTRA_DOMAINS 编译定义的宿主目标（ui_plugin
 *        ／ASM-STUDIO 批起含 studio）的 DomainAssembly.cpp 尾段被调用；
 *        另有 ui_contract_test 测试目标直调（六域聚合注册断言——直测
 *        通道）。
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
