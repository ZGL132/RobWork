/**
 * @file   RequirementsCommandFlows.hpp
 * @brief  需求域九条命令的 UI 流程编排（UI-T32 C 批次——对话框/表单/
 *         文件选择与域纯函数的装配层；域逻辑零重写，全部经
 *         ITemplateArrayService/IRequirementImporter 既有纯函数）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T32.json（九命令 assembled 流程——
 *     实施序组1：导出副本/模板/阵列/镜像/重生成＋解除关联）；
 *   - units/requirements.md §7.1（模板六类）/§7.2（镜像/四类阵列）/
 *     §7.4（导出原子写）/§9.5（重生成/解除）；
 *   - 面板命令按钮→setCommandSubmit 出口→宿主 handler→门面
 *     executeDomainCommand 的唯一路由（PA-1——本文件是被调用端）。
 *
 * 线程模型：全部 UI 线程（对话框模态＋编辑器同步提交）。
 * 反馈面：成功/失败经返回的中文摘要（面板状态行呈现——UX-02）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSCOMMANDFLOWS_HPP
#define IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSCOMMANDFLOWS_HPP

#include <string>

namespace sdurws::ird::requirements {

class RequirementsPanelWidget;
class IRequirementEditor;
struct IRequirementEditSink;

/**
 * @brief 执行一条需求域命令的 UI 流程（九命令统一入口）。
 *
 * 已知命令 id（PanelCommandCatalog 九条）；未知 id 返回 false＋提示
 * （不抛——命令路由层的防御面）。
 *
 * @param commandId [in] 命令 id（如 "requirements.apply-template"）
 * @param panel     [in] 对话框父窗口与状态行宿主（非 owning）
 * @param editor    [in] 会话权威编辑器（批量经 submitBatchEdit 单快照）
 * @param sink      [in] 编辑流出口（onEditApplied/Rejected 分流）
 * @return true＝流程完成（应用或用户取消——摘要经面板状态行）；
 *         false＝流程失败/未知命令（就地错误已呈现）
 */
bool executeRequirementCommand(const std::string& commandId,
                               RequirementsPanelWidget& panel,
                               IRequirementEditor& editor,
                               IRequirementEditSink& sink);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSCOMMANDFLOWS_HPP
