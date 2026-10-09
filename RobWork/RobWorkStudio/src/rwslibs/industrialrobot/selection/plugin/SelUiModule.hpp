/**
 * @file   SelUiModule.hpp
 * @brief  selection 插件界面模块——ui::IPluginUiModule（ui.md §11.2）的
 *         selection 侧实现（宿主装配批次 ASM-PLUG 收口——P-SEL-10 消账
 *         形态：装配面自持描述符翻译为真实注册的模块半区载体）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（IPluginUiModule 三方法冻结签名：onShellReady／
 *     readonlyProjections／buildDraftCommand）、§10.9（装配期经
 *     IPluginUiRegistrar 注册——装配期一次、白名单校验、存活至壳拆除）、
 *     §11.1（白名单 token "selection"——第 6 token，真实注册登记值）；
 *   - units/selection.md §19.2/§19.5 P-SEL-10（插件真实注册端口消费缺口
 *     ——ASM-PLUG 收口批兑现：真实 IPluginUiRegistrar/PluginUiRegistrar::
 *     whitelist() 端口消费归宿主装配批次收口的明文义务）；§3.4（插件零
 *     计算红线——本模块只转接呈现，零筛选/校核语义）；
 *   - 先例：workflow/plugin/WorkflowUiModule.hpp（WP-22-T02 最小真实继承
 *     形态）、dynamics/plugin/DynUiModule.hpp（ASM-PLUG 收口批同构——
 *     三域一体收口）；selection/plugin/SelPanelModule.hpp（T10 落位的纯
 *     装配缝容器——本类经转接其会话态，零第二会话真值——PA-1 权威唯一）。
 *
 * ★ 转接纪律（读代码前先读）：本类**不持有任何新会话真值**——域会话
 *   事实唯一归属 SelPanelModule::session（T10 落位形态），本模块只持其
 *   指针做只读转接（non-owning，存活期由装配门面保证——§10.9 同款纪
 *   律：宿主保持门面存活至壳拆除）。readonlyProjections 经模型层
 *   readinessProjection（L-S1 纯透传流）现取现译，零缓存零第二真值。
 *
 * 线程约束：onShellReady 在装配线程调用；readonlyProjections/
 *   buildDraftCommand 只允许 UI 线程调用（ui.md §11.2 线程行——会话态
 *   与面板引用的既有纪律，卡 §3.4）。
 */

#ifndef IRD_SELECTION_PLUGIN_SELUIMODULE_HPP
#define IRD_SELECTION_PLUGIN_SELUIMODULE_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/ui/IPluginUiModule.hpp>  // ui::IPluginUiModule（§11.2 三方法冻结接口）
#include <sdurws/ird/ui/UiTypes.hpp>          // ui::DomainReadinessItem（§6.5 汇聚值面）

namespace sdurws {
namespace ird {
namespace ui {
class IWorkbenchShell;  // 前置声明（onShellReady 入参——实现只存引用不
                        //   调用壳方法，断环不 include 其头）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

namespace sdurws::ird::selection {

class SelPanelModule;  // 前置声明（转接目标——完整类型在 plugin/ 私有头）

/**
 * @brief selection 插件界面模块（ui::IPluginUiModule 的 selection 侧
 *        真实继承实现——ASM-PLUG 收口批的注册模块半区）。
 *
 * 生命周期：装配激活路径（registerWithHostRegistrar——assembly/ 门面）
 * 创建并归装配门面 unique_ptr 持有；经 IPluginUiRegistrar::registerPluginUi
 * 登记后 registrar 只持弱引用（§10.9 所有权行）——宿主保持装配门面存
 * 活至壳拆除，无注销（静态白名单 SA-01）。
 *
 * 所有权：面板模块指针与壳引用均非 owning（存活期分别由装配门面与壳侧
 * 保证——§10.9 前置条件）。
 */
class SelUiModule final : public ui::IPluginUiModule {
public:
    /**
     * @brief 构造模块（装配激活路径调用——绑定域面板模块的会话态）。
     *
     * @param module [in] 域面板模块（非 owning——会话态真值唯一归属；
     *               空指针属装配期程序违约，由激活路径保证非空，本构造
     *               不接受空——调用方契约）
     */
    explicit SelUiModule(SelPanelModule* module);

    ~SelUiModule() override;
    SelUiModule(const SelUiModule&) = delete;
    SelUiModule& operator=(const SelUiModule&) = delete;

    // ---- ui.md §11.2 三方法（真实 override——签名与冻结接口逐一同形）----

    /**
     * @brief 壳就绪回调（§11.2"注册回调后初始化〔订阅/面板内容〕"）。
     *
     * selection 侧语义：只持有壳引用（只读消费）——本域面板内容的初始
     * 化由面板工厂与 refreshFromSession 通道承载（T10 落位形态），域命
     * 令注册经装配描述符在装配期一次完成（§10.9"装配期一次"），本回调
     * 零重复注册、零订阅（UI-T21/T22 树/属性页域供给协议等壳消费面随
     * 其装配批次落位，如实最小形态——不预建空订阅，NFR-MNT-04）。
     *
     * @param shell [in] 工作台壳门面（非 owning——调用方保证存活期覆盖
     *              模块存活期）
     */
    void onShellReady(ui::IWorkbenchShell& shell) override;

    /**
     * @brief 只读业务投影（§11.2"§6.5 汇聚源"——StageStatusModel 汇聚
     *        的 selection 行）。
     *
     * 数据面＝模型层 L-S1 纯透传流（readinessProjection——会话事实
     * verdict/inputComplete/missingItemKeys/hasActiveTask 全透传，零判
     * 定零缓存），本方法只做 SelReadinessRow→ui::DomainReadinessItem
     * 的字段一一对应翻译（宿主汇聚翻译对账锚＝domainKey，恒 "selection"
     * ——§6.5 域注册词表）。
     *
     * @return 单元素投影行（缺省行不伪造可行性——UX-02/ERR-01 同源纪
     *         律；面板模块缺位属装配违约不可达，见类注）
     */
    std::vector<ui::DomainReadinessItem> readonlyProjections() const override;

    /**
     * @brief 草稿应用命令组装（§8.5 应用时命令组装的域侧半区——恒
     *        nullopt 的本域语义实现，非占位）。
     *
     * 业务原因（卡 §12.1/§12.3——T09 落位登记）：selection 的写语义唯
     * 一经①命令端口（回填意图 token→宿主命令管线→project 命令服务产
     * 生恰一新修订），不经 §8.5 draft.apply 草稿流；本域不持有任何草稿
     * 工作集——§8.5"无可应用变更返回 nullopt（不产生空修订）"的语义落
     * 点。域内 moduleId 同样无可应用草稿（§11.2 参数语义：壳侧逐模块
     * 征询，域外请求亦 nullopt）。
     *
     * @param moduleId [in] 域注册键（任何值——域内外一致 nullopt）
     * @return nullopt（恒——域无草稿语义）
     */
    std::optional<project::CommandEnvelope> buildDraftCommand(
        const std::string& moduleId) override;

private:
    SelPanelModule* m_module = nullptr;      ///< 域面板模块（非 owning——
                                             ///<   会话态真值唯一归属；存
                                             ///<   活期由装配门面保证）
    ui::IWorkbenchShell* m_shell = nullptr;  ///< 壳门面引用（非 owning——
                                             ///<   onShellReady 注入）
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_PLUGIN_SELUIMODULE_HPP
