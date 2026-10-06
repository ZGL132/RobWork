/**
 * @file   WorkflowUiModule.hpp
 * @brief  workflow 插件界面模块——ui.md §11.2 IPluginUiModule 的 workflow
 *         侧最小实现（WP-22-T02 构建落位批次的"最小可注册实现"）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（IPluginUiModule 三方法冻结签名：onShellReady／
 *     readonlyProjections／buildDraftCommand）、§10.9（装配期经
 *     IPluginUiRegistrar 注册——装配期一次、白名单校验、存活至壳拆除）
 *   - units/workflow.md §3.2（sdurws_ird_workflow_plugin＝L4 插件界面目标，
 *     WP-22-T02～T12 落位）、§2.4 非所有权表（N1/N2——workflow 不拥有领域
 *     就绪计算与命令设施；域就绪投影由各业务域自报，workflow 是编排单元
 *     而非评估域）
 *   - 任务契约 tasks/foundation/WP-22-T02.json（acceptance 1：创建 _plugin
 *     目标——最小可注册实现；界面功能随 WP-22-T04+ 向导/首页/比较视图
 *     批次补齐；零业务计算逻辑）
 *   - 先例：requirements/plugin/RequirementsUiModule.hpp（WP-14-T08 同款
 *     三方法真实继承形态；modeling/kinematics 更早先例）
 *
 * ★ T02 最小实现边界（读代码前先读）：
 *   本类只兑现"可注册"的契约面——三方法签名与冻结接口逐一同形（真实
 *   override，非同形暂持），方法体为该接口在 workflow 编排定位下的诚实
 *   空态（空向量＝无投影内容、nullopt＝无可应用草稿——都不是占位欺骗，
 *   而是 §11.2 契约明文允许的合法返回）。向导/首页/比较视图/确认对话框
 *   等 Qt Widgets 界面面随 WP-22-T04~T12 落位，届时经装配描述符的
 *   panels/commands 登记（本批次 descriptor 三能力声明全 false——§10.9
 *   "描述性，非判定性"），本头随之增量扩展，不回改已冻结签名。
 *
 * 线程约束：三方法调用线程契约随接口（onShellReady＝装配线程；
 *   readonlyProjections/buildDraftCommand＝UI 线程）——本实现零会话态
 *   （仅持壳引用），无额外同步需求；后续批次引入会话态时按 §3.4 加
 *   UI 线程守卫（requirements PanelUiThreadGuard 先例）。
 */

#ifndef IRD_WORKFLOW_PLUGIN_WORKFLOWUIMODULE_HPP
#define IRD_WORKFLOW_PLUGIN_WORKFLOWUIMODULE_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/project/CommandService.hpp>  // project::CommandEnvelope（§8.5 返回值面——workflow→project 登记边公共头）
#include <sdurws/ird/ui/IPluginUiModule.hpp>      // ui::IPluginUiModule（三方法冻结接口）
#include <sdurws/ird/ui/UiTypes.hpp>              // ui::DomainReadinessItem（§6.5 汇聚值面）

namespace sdurws {
namespace ird {
namespace ui {
class IWorkbenchShell;  // 前向声明（onShellReady 入参——完整类型声明面不需
                        // 要；实现只存引用不调用壳方法，断环不 include）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

namespace sdurws::ird::workflow {

/**
 * @brief workflow 插件界面模块（ui.md §11.2 三方法的最小真实实现）。
 *
 * 生命周期：装配期由插件装配点创建（createWorkflowPluginAssembly——
 * assembly/ 门面），以 unique_ptr 交装配产物结构持有；经
 * IPluginUiRegistrar::registerPluginUi 登记后 registrar 只持弱引用（§10.9），
 * 模块存活至壳拆除——无注销（静态白名单 SA-01）。
 *
 * 所有权：壳引用非 owning（存活期由壳侧保证——§10.9 前置条件）。
 */
class WorkflowUiModule final : public ui::IPluginUiModule {
public:
    WorkflowUiModule() = default;
    /// 不可拷贝/不可移动（壳引用绑定生命周期——§10.9 存活至壳拆除）。
    WorkflowUiModule(const WorkflowUiModule&) = delete;
    WorkflowUiModule& operator=(const WorkflowUiModule&) = delete;

    // ---- ui.md §11.2 三方法（逐方法对齐卡文冻结签名）------------------

    /**
     * @brief 壳就绪回调（§11.2 行"注册回调后初始化〔订阅/面板内容〕"）。
     *
     * workflow 侧 T02 最小语义：只持有壳引用（只读消费）——七阶段导航
     * 订阅、横幅/建议呈现接线随 WP-22-T03/T09 批次落位（届时在本回调
     * 完成订阅初始化）；域命令/面板注册经装配描述符在装配期一次完成
     * （§10.9"装配期一次"），本回调零重复注册。
     *
     * @param shell [in] 工作台壳门面（非 owning——调用方保证存活期覆盖模块）
     */
    void onShellReady(ui::IWorkbenchShell& shell) override;

    /**
     * @brief 只读业务投影（§11.2 行"§6.5 汇聚源"）。
     *
     * workflow 侧恒为空向量的业务原因：§6.5 的 DomainReadinessItem 由
     * **各评估域插件**（modeling/requirements/kinematics/…）经
     * IUiDomainReadinessSource 自报——workflow 是消费这些投影计算门控的
     * 编排单元（卡 §2.3 O1），自身不是评估域，无域就绪行可上报。§11.2
     * 契约明文"可空向量＝域无投影内容"——空即诚实态，不伪造数据行。
     *
     * @return 空向量（T02 最小形态；后续批次亦不改变——编排单元恒无域行）
     */
    std::vector<ui::DomainReadinessItem> readonlyProjections() const override;

    /**
     * @brief 草稿应用命令组装（§11.2 行"§8.5 应用时命令组装〔域侧〕"）。
     *
     * workflow 侧恒为 nullopt 的业务原因（两种情形语义不同，返回值相同）：
     *   - moduleId != "workflow"：域外请求（§11.2 参数语义——壳侧逐模块
     *     征询，非本域调用）；
     *   - moduleId == "workflow"：workflow 是生命周期入口的**编排**单元，
     *     不持有任何草稿工作集（编辑草稿归各业务域编辑器——requirements
     *     Editor 先例；存储与提交语义归 project，PA-1 权威唯一）——编排
     *     单元恒无"可应用变更"可组装（§8.5"无可应用内容返回 nullopt，
     *     不产生空修订"）。
     *
     * @param moduleId [in] 域注册键（如 "modeling"/"workflow"）
     * @return 恒 nullopt（T02 最小形态；见上——两情形均合法无信封）
     */
    std::optional<project::CommandEnvelope> buildDraftCommand(
        const std::string& moduleId) override;

private:
    /// 工作台壳门面引用（非 owning——onShellReady 注入；T02 零消费面，
    /// 后续批次据此接线导航订阅与横幅呈现）。
    ui::IWorkbenchShell* m_shell = nullptr;
};

}  // namespace sdurws::ird::workflow

#endif  // IRD_WORKFLOW_PLUGIN_WORKFLOWUIMODULE_HPP
