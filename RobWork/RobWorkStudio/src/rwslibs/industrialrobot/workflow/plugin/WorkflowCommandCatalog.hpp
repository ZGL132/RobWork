/**
 * @file   WorkflowCommandCatalog.hpp
 * @brief  workflow 命令贡献目录（WP-22-T12）——14 条命令的 ui::Command-
 *         Descriptor 组装、IWorkflowCommandContributor Draft 兑现与注册
 *         装配函数（插件 Qt 面；本头随 plugin 目标交付，不在 include/
 *         产品扫描域）。
 *
 * 设计依据：
 *   - units/workflow.md §8.3（最小命令集注册与工业高频命令——本单元在装
 *     配期经 ui ICommandRegistry 注册端口登记命令项；命令注册表与全局快
 *     捷键唯一注册点归 ui——SA-16，本单元是贡献者不是注册设施〔O8/N2〕）、
 *     §10.2（IWorkflowCommandContributor Draft 签名：contributedCommands()
 *     返回 vector<ui::CommandDescriptor>）
 *   - units/ui.md §7.1（最小命令集冻结表——id/scope/readOnlyAllowed/默认
 *     快捷键逐列权威；描述符行与 ui 壳层 kMinimalCommandRows 同值——两卡
 *     增量同步义务）、§7.2（注册协议：owner 白名单＋重复 id 注册边界拒
 *     绝＋UI-CMD-DUPLICATE 诊断）、§7.3（插件不得自建全局作用域
 *     QShortcut——SA-16/NFR-MNT-07 静态检查延伸）
 *   - 需求 UX-13（命令面板首版 ≥10 条＋模糊搜索可达＋快捷键契约 M-13）
 *   - 先例：modeling/plugin/PanelCommandCatalog.hpp（ui::CommandDescriptor
 *     承载于插件目录的 P-MDL-8 先例；本头同款文件域选择）
 *
 * ★ 落位偏差（DTB §5.4 登记，单元卡 §10.2 v1.3）：§10.2 Draft 将
 *   IWorkflowCommandContributor 记于 Types.hpp——该落位不可兑现：
 *   ui::CommandDescriptor 携带 std::optional<QKeySequence>（Qt Gui 类型），
 *   而 workflow 计算库公共头零 Qt（R-3/NFR-MNT-01 红线，include/** 零
 *   Qt 包含）。故接口与实现落位本头（plugin/ 目录——Qt 允许面，R-3 红
 *   线的文件域隔离：门禁 R-3 产品面扫描域为 include/**＋src/**）。接口
 *   签名与 Draft 逐字一致（contributedCommands()），仅落位文件变化；
 *   消费方（宿主装配层）经本头消费（assembly/ 门面同款交付通道）。
 *
 * 命令执行链（一条命令从提交到落地的完整路径）：
 *   ui 注册表 submit(id) → 本目录处理器（makeWorkflowCommandHandlers 产
 *   出）→ workflow::runWorkflowCommand 编排核（Commands.hpp——五类分流）
 *   → 各端口（L5 装配桥接）→ WorkflowCommandOutcome 折叠回 ui::Command-
 *   Outcome。处理器是薄适配层：只做" outcome 值折叠 + 端口组合传递"，
 *   零分流逻辑（分流唯一实现点在编排核——NFR-MNT-03）。
 *
 * SA-16 边界自证：本目录零 QShortcut/零快捷键设施消费——描述符的
 * defaultShortcut 只是**默认绑定数据**（ui §7.1 冻结表的描述符半区），
 * 真实键注册/冲突拒绝/改绑一律归 ui HotkeyBindingTable（装配期经
 * registerDefault——快捷键设施在 ui 侧）；bindable 全 true＝可经用户设
 * 置改绑（零私占）。未绑定默认键的命令经命令面板模糊搜索可达（UX-13
 * 兜底——keywordKeys 全集齐备）。
 *
 * 线程约束：全部函数纯函数或薄适配（无共享可变状态）；处理器在 UI 线
 * 程启动（ICommandRegistry 契约）——端口组合的线程约束由其实现声明。
 */

#ifndef IRD_WORKFLOW_PLUGIN_WORKFLOWCOMMANDCATALOG_HPP
#define IRD_WORKFLOW_PLUGIN_WORKFLOWCOMMANDCATALOG_HPP

#include <map>
#include <string>
#include <vector>

#include <sdurws/ird/ui/ICommandRegistry.hpp>   // ui::CommandDescriptor/ICommandRegistry/
                                                //   RegistrationResult/CommandHandler（UI-T06 冻结形状）
#include <sdurws/ird/workflow/Commands.hpp>     // workflow::WorkflowCommandPorts/runWorkflowCommand/
                                                //   WorkflowCommandOutcome（零 Qt 编排核——本目录是 Qt 适配面）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// IWorkflowCommandContributor——§10.2 Draft 接口（落位偏差见文件头注）
// =====================================================================

/**
 * @brief workflow 命令贡献者接口（§10.2 Draft 逐字——返回本单元贡献的
 *        命令项清单〔最小集＋工业高频，§8.3〕）。
 *
 * 消费方：宿主装配层（装配期经 ui ICommandRegistry 注册端口登记——
 * §8.3 注册编排）与测试（接口消费路径钉扎——WP-20-T03 首轮接口盲区
 * 教训的常设对正面：公共接口承诺的每个方法至少一条经虚派发的用例）。
 */
class IWorkflowCommandContributor {
public:
    virtual ~IWorkflowCommandContributor() = default;

    /**
     * @brief 本单元贡献的命令项清单（14 条描述符——§8.3 最小集 10＋
     *        工业高频 4 id；行序＝ui.md §7.1 冻结表行序）。
     * @return 描述符全集（每次现产、无缓存——确定性 NFR-COR-02；调用
     *         方不取得词表所有权，值拷贝）
     */
    virtual std::vector<ui::CommandDescriptor> contributedCommands() const = 0;
};

/**
 * @brief 贡献者标准实现（无状态——描述符工厂直通）。
 *
 * 生命周期：装配期构造（L5 或测试），存活期覆盖注册窗口即可——
 * contributedCommands() 无会话态，可任意时机调用。
 */
class WorkflowCommandContributor final : public IWorkflowCommandContributor {
public:
    std::vector<ui::CommandDescriptor> contributedCommands() const override;
};

// =====================================================================
// 描述符组装（ui.md §7.1 冻结表同值行——两卡增量同步义务）
// =====================================================================

/**
 * @brief workflow 贡献的 14 条命令描述符（§8.3 清单的 ui.md §7.1 冻结
 *        表同值实现）。
 *
 * 逐字段口径（与 ui 壳层登记行逐列一致——同一命令在壳层与贡献清单两处
 * 的描述符必须同值，测试逐字段钉扎）：
 *   - id/titleKey/keywordKeys/category/scope/readOnlyAllowed/menuPath＝
 *     ui.md §7.1 冻结表原文（titleKey＝"cmd.<id>.title"、keywordKeys＝
 *     "cmd.<id>.kw.<n>"——SA-12 词表复用，值〔中文〕归 ui 文案表）；
 *   - ownerUnit="workflow"（ui §11.1 静态白名单第 8 token——§7.2 第 1 步
 *     owner 校验的放行依据）；
 *   - defaultShortcut＝冻结表默认键列（Ctrl+N/Ctrl+O/Ctrl+S/Ctrl+Return/
 *     Ctrl+Z/Ctrl+Y 六键——数据面；真实注册归 ui HotkeyBindingTable）；
 *     其余八条无默认键——经命令面板模糊搜索可达（UX-13 兜底）；
 *   - bindable=true 全体（SA-16：可经 ui 快捷键表用户级改绑，不私占）；
 *   - iconKey/params 空（阶段 A 不消费——ui §7.1 注）。
 *
 * @return 14 条描述符（行序＝冻结表行序——registrationOrder 稳定排序锚）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<ui::CommandDescriptor> workflowCommandDescriptors();

// =====================================================================
// 处理器装配与注册（编排核的薄适配——ui::CommandOutcome 折叠点）
// =====================================================================

/**
 * @brief 把编排核结果折叠为 ui 提交结果（WorkflowCommandOutcome→
 *        ui::CommandOutcome 的唯一折叠点——同构直转：accepted 直通、
 *        messageKey 装入 optional<TextKey>〔空串＝nullopt 无消息〕）。
 *
 * @param outcome [in] 编排核结果（runWorkflowCommand 产出）
 * @return ui 提交结果（revisionResult 恒 nullopt——修订事实的机器承载
 *         归 IRevisionCommandPort 的 RevisionOutcome 链路，命令折叠层
 *         不重复包装 C-4 投影）
 */
ui::CommandOutcome foldToCommandOutcome(const WorkflowCommandOutcome& outcome);

/**
 * @brief 组装 14 条命令处理器（id→处理器的薄适配表——每条处理器调用
 *        runWorkflowCommand 编排核并折叠结果，零分流逻辑）。
 *
 * @param ports [in] 编排核端口组合（指针集非 owning——调用方保证存活期
 *              覆盖注册表生命周期；装配违约由编排核入口 fail-fast）
 * @return 处理器表（14 键全量——键集与 workflowCommandIds() 相同；有序
 *         map＝遍历确定性 NFR-COR-02）
 */
std::map<std::string, ui::ICommandRegistry::CommandHandler> makeWorkflowCommandHandlers(
    const WorkflowCommandPorts& ports);

/**
 * @brief 装配函数：把贡献清单＋处理器经注册端口登记（§8.3 注册编排的
 *        可执行形态——宿主装配层与契约测试共用）。
 *
 * 注册协议（ui §7.2）：逐条 registerCommand（默认谓词——作用域/只读规
 * 则在注册表内），行序＝冻结表序（registrationOrder 锚）。**重复 id 在
 * 注册边界拒绝**（ui 设施行为：DuplicateId＋UI-CMD-DUPLICATE 诊断，不
 * 覆盖不静默）——本函数不吞结果，逐条结果原样返回由调用方裁决（真实
 * 宿主融合形态下同 id 壳层命令走覆写通道〔WorkbenchContentDeps 处理器
 * 覆写——UI-T17 先例〕，不经本函数二次注册；本函数服务于"贡献清单独
 * 立注册"的装配场景与测试验证）。
 *
 * @param registry [in] ui 命令注册表（装配期、seal 前——非 owning）
 * @param ports    [in] 编排核端口组合（透传处理器装配——见上）
 * @return 逐条注册结果（行序＝workflowCommandIds() 序；全 Ok＝14 条全
 *         登记成功——调用方校验）
 */
std::vector<ui::RegistrationResult> registerWorkflowCommands(
    ui::ICommandRegistry& registry, const WorkflowCommandPorts& ports);

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_WORKFLOW_PLUGIN_WORKFLOWCOMMANDCATALOG_HPP
