/**
 * @file   TrajUiModule.hpp
 * @brief  trajectory 插件界面模块——ui::IPluginUiModule（ui.md §11.2）的
 *         trajectory 侧实现（WP-16-T12 落位——dynamics 先例 DynUiModule
 *         同款形态：装配面自持描述符翻译为真实注册的模块半区载体）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（IPluginUiModule 三方法冻结签名：onShellReady／
 *     readonlyProjections／buildDraftCommand）、§10.9（装配期经
 *     IPluginUiRegistrar 注册——装配期一次、白名单校验、存活至壳拆除）、
 *     §11.1（白名单 token "trajectory"——真实注册登记值）；
 *   - units/trajectory.md §14.5.2（投影与会话态——DomainReadinessItem
 *     （domainKey="trajectory"）供七态汇聚——域只供数，呈现映射归 ui）、
 *     §14.5.1（七命令修订列全部"无"——草稿应用命令组装恒空的本域语义
 *     锚）、§16.2（零修订与零计算红线）；
 *   - 先例：dynamics/plugin/DynUiModule.hpp（WP-17-T09/ASM-PLUG 收口批
 *     的模块半区实现——转接纪律与恒 nullopt 草稿语义同款）；
 *   - 任务契约 tasks/foundation/WP-16-T12.json（acceptance 1）。
 *
 * ★ 转接纪律（读代码前先读）：本类**不持有任何新会话真值**——域会话
 *   事实唯一归属 TrajPanelModule::session（T12 落位形态），本模块只持其
 *   指针做只读转接（non-owning，存活期由装配门面保证——§10.9 同款纪
 *   律：宿主保持门面存活至壳拆除）。readonlyProjections 现取现译，零
 *   缓存零第二真值。
 *
 * 线程约束：onShellReady 在装配线程调用；readonlyProjections/
 *   buildDraftCommand 只允许 UI 线程调用（ui.md §11.2 线程行——会话态
 *   与面板引用的既有纪律，卡 §3.4）。
 */

#ifndef IRD_TRAJECTORY_PLUGIN_TRAJUIMODULE_HPP
#define IRD_TRAJECTORY_PLUGIN_TRAJUIMODULE_HPP

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

namespace sdurws::ird::trajectory {

class TrajPanelModule;  // 前置声明（转接目标——完整类型在 plugin/ 私有头）

/**
 * @brief trajectory 插件界面模块（ui::IPluginUiModule 的 trajectory 侧
 *        真实继承实现——T12 批的注册模块半区）。
 *
 * 生命周期：装配激活路径（registerWithHostRegistrar——assembly/ 门面）
 * 创建并归装配门面 unique_ptr 持有；经 IPluginUiRegistrar::registerPluginUi
 * 登记后 registrar 只持弱引用（§10.9 所有权行）——宿主保持装配门面存
 * 活至壳拆除，无注销（静态白名单 SA-01）。
 *
 * 所有权：面板模块指针与壳引用均非 owning（存活期分别由装配门面与壳侧
 * 保证——§10.9 前置条件）。
 */
class TrajUiModule final : public ui::IPluginUiModule {
public:
    /**
     * @brief 构造模块（装配激活路径调用——绑定域面板模块的会话态）。
     *
     * @param module [in] 域面板模块（非 owning——会话态真值唯一归属；
     *               空指针属装配期程序违约，由激活路径保证非空，本构造
     *               不接受空——调用方契约）
     */
    explicit TrajUiModule(TrajPanelModule* module);

    ~TrajUiModule() override;
    TrajUiModule(const TrajUiModule&) = delete;
    TrajUiModule& operator=(const TrajUiModule&) = delete;

    // ---- ui.md §11.2 三方法（真实 override——签名与冻结接口逐一同形）----

    /**
     * @brief 壳就绪回调（§11.2"注册回调后初始化〔订阅/面板内容〕"）。
     *
     * trajectory 侧语义：只持有壳引用（只读消费）——本域面板内容的初
     * 始化由面板工厂与 refreshFromSession 通道承载（§14.5.2 落位形态），
     * 域命令注册经装配描述符在装配期一次完成（§10.9"装配期一次"），本
     * 回调零重复注册、零订阅（动画播放的会话姿态驱动经服务缝 session-
     * PoseSink 承载——宿主 View3D 桥接线归宿主装配批，此处不预建空订
     * 阅，NFR-MNT-04）。
     *
     * @param shell [in] 工作台壳门面（非 owning——调用方保证存活期覆盖
     *              模块存活期）
     */
    void onShellReady(ui::IWorkbenchShell& shell) override;

    /**
     * @brief 只读业务投影（§11.2"§6.5 汇聚源"——StageStatusModel 汇聚
     *        的 trajectory 行）。
     *
     * 数据面＝会话态纯透传流（TrajPanelModule::session 的四呈现字段—
     * —verdict/inputComplete/missingItemKeys/hasActiveTask 全透传，零判
     * 定零缓存），本方法只做 TrajModuleSessionState→
     * ui::DomainReadinessItem 的字段一一对应翻译（宿主汇聚翻译对账锚
     * ＝domainKey，恒 "trajectory"——§6.5 域注册词表；§14.5.2"域只供
     * 数，呈现映射归 ui"）。
     *
     * @return 单元素投影行（缺省行不伪造可行性——UX-02/ERR-01 同源纪
     *         律；面板模块缺位属装配违约不可达，见类注）
     */
    std::vector<ui::DomainReadinessItem> readonlyProjections() const override;

    /**
     * @brief 草稿应用命令组装（§8.5 应用时命令组装的域侧半区——恒
     *        nullopt 的本域语义实现，非占位）。
     *
     * 业务原因（§14.5.1 表"修订"列全部"无"）：trajectory 七命令族全部
     * 为会话命令（零修订——kTrajectorySessionContract 四常量），本域不
     * 持有任何草稿工作集——§8.5"无可应用变更返回 nullopt（不产生空修
     * 订）"的语义落点。域内 moduleId 同样无可应用草稿（§11.2 参数语义：
     * 壳侧逐模块征询，域外请求亦 nullopt）。
     *
     * @param moduleId [in] 域注册键（任何值——域内外一致 nullopt）
     * @return nullopt（恒——域无草稿语义）
     */
    std::optional<project::CommandEnvelope> buildDraftCommand(
        const std::string& moduleId) override;

private:
    TrajPanelModule* m_module = nullptr;     ///< 域面板模块（非 owning——
                                             ///<   会话态真值唯一归属；存
                                             ///<   活期由装配门面保证）
    ui::IWorkbenchShell* m_shell = nullptr;  ///< 壳门面引用（非 owning——
                                             ///<   onShellReady 注入）
};

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PLUGIN_TRAJUIMODULE_HPP
