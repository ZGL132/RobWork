/**
 * @file   PanelCommandCatalog.hpp
 * @brief  requirements 域命令登记目录（零 Widgets）——九条 CommandId 与
 *         装配描述符（卡 §9.8 命令表；ui.md §10.9/§11.1/§11.2）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（域命令登记清单——九条 CommandId＋
 *     readOnlyAllowed 逐条权威；CommandId 点分小写＝ui CommandId 词表，
 *     与 project commandType 无点词表是两套命名空间，不混用；"快捷键经
 *     HotkeyBindingTable"）、§8（就绪投影——DomainReadinessItem 数据面）、
 *     §12（ui 交接行——DomainReadinessItem 恰三字段呈现数据，P-REQ-6
 *     边界：报告不含门控动作语义）
 *   - units/ui.md §10.9（PluginUiDescriptor/PanelRegistration/插件能力
 *     声明）、§11.1（静态白名单 token="requirements"）、§11.2（
 *     IPluginUiModule 三方法）、§7.3（快捷键一律经 HotkeyBindingTable
 *     ——SA-16 插件不私占全局）、§7.6/L-R12（只读门控）
 *   - 需求 UX-05、SA-16；任务契约 tasks/foundation/WP-14-T08.json
 *     acceptance 1/4/5
 *
 * ★ P-REQ-8 处置（同 modeling P-MDL-8 先例）：ui 的 IPluginUiRegistrar.hpp
 *   尚未落位（ui.md §10.9 为冻结文本；AboutDialog.hpp 登记"注册端口随
 *   装配任务落位时迁移该形状"——磁盘核对不存在）。本目录以**已落位的
 *   ui 公共类型**为承载：
 *   - 九条命令＝ui::CommandDescriptor（ICommandRegistry.hpp——UI-T06 落位，
 *     冻结形状）；命令装配数据面与 §10.9 PluginUiDescriptor.commands 逐
 *     字段对齐（id/ownerUnit/titleKey/category/scope/readOnlyAllowed/
 *     bindable/defaultShortcut/menuPath）；
 *   - 面板/能力登记＝PanelRegistrationRecord（本头承载的 §10.9 形状值
 *     ——AboutDialog.hpp 承载 PluginAssemblyReport 的"冻结形状暂持"同案，
 *     NFR-MNT-03：对端落位后迁移至 ui 类型，R-REQ-1 增量同步，单元卡
 *     §14.6 登记）。
 *
 * 线程约束：全部函数纯函数（无共享可变状态），可重入。确定性：同调用同
 * 清单（NFR-COR-02——行序＝卡 §9.8 命令表行序，登记序即白名单序呈现依据）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELCOMMANDCATALOG_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELCOMMANDCATALOG_HPP

#include <string>
#include <vector>

#include <sdurws/ird/requirements/Readiness.hpp>  // RequirementReadinessReport（域就绪投影数据源）
#include "PanelStationModel.hpp"              // StationFieldRow（L-R12 只读门控的输入行——同目录插件私有头）
#include <sdurws/ird/ui/ICommandRegistry.hpp> // ui::CommandDescriptor/CommandId/CommandScope/CommandCategory（UI-T06 冻结形状）
#include <sdurws/ird/ui/UiTypes.hpp>          // ui::StageId/DomainReadinessItem/TextKey（§6.5 单侧冻结形状）

namespace sdurws::ird::requirements {

// =====================================================================
// 九条域命令（卡 §9.8 命令表逐行——acceptance 5 的权威清单）
// =====================================================================

/**
 * @brief requirements 域命令清单（§9.8 命令表全量——九条独立命令）。
 *
 * 逐行对照（id→readOnlyAllowed，卡面权威——acceptance 5 逐条一致）：
 *   requirements.import-csv→false、requirements.import-json→false、
 *   requirements.export-copy→true、requirements.capture-tcp→true、
 *   requirements.pick-feature→true、requirements.mirror-stations→false、
 *   requirements.create-array→false、requirements.apply-template→false、
 *   requirements.regenerate-linked→false。
 *
 * 其余登记要素：ownerUnit="requirements"（白名单 token——§11.1 八 token
 * 词表）；作用域＝ui::CommandScope::Project（项目作用域域命令族）；category
 * =Stage（阶段面命令族——§7.1 分类词表）；快捷键＝defaultShortcut 全空＋
 * bindable=true（SA-16：插件不私占全局快捷键——需要绑定时一律经 ui
 * HotkeyBindingTable 用户级配置，默认零绑定→与任何既有绑定构造性无冲突，
 * acceptance 5"无冲突登记"的结构保证）；titleKey 按 §3.5 键约定
 * "cmd.<id>.title"。
 *
 * @return 九条描述符（行序＝卡 §9.8 命令表行序——登记序；每次调用现产
 *         清单，无缓存）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<ui::CommandDescriptor> requirementsDomainCommands();

/**
 * @brief 命令 id 词表（点分小写）与 project commandType 词表（无点）的
 *        命名空间分离守卫值（modeling 同款——ACC5 交叉断言面）。
 *
 * @return project 侧 token 清单（apply-requirement-set/apply-requirement-
 *         import——CommandHandlers.hpp 冻结常量直用，不私写字面量）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<std::string> requirementsProjectCommandTokens();

// =====================================================================
// 装配描述符（§10.9 形状承载——P-REQ-8：对端落位后迁移，R-REQ-1）
// =====================================================================

/**
 * @brief 插件能力声明（§10.9 PluginCapabilities 形状——描述性，非判定性）。
 */
struct PanelCapabilities {
    bool providesStagePanel = false;         ///< 提供阶段面板（requirements＝true）
    bool providesReadonlyProjection = false; ///< 参与StageStatusModel汇聚（requirements＝true）
    bool registersCommands = false;          ///< 登记命令（requirements＝true）
};

/**
 * @brief 面板登记记录（§10.9 PanelRegistration＋PluginUiDescriptor 的合并
 *        形状承载——ui.md 冻结文本的值化；P-REQ-8 暂持先例见文件头注）。
 *
 * panelFactory 语义＝§10.9 PanelRegistration.factory（创建 QWidget 族面板，
 * ui 线程调用）——形状以工厂返回类型 widget 层类型表达（本头零 Widgets：
 * 工厂经 widget 层函数指针注入，见 RequirementsUiModule）。advanced=false
 * （需求面板为 UX-09 主面板——高级面板标记不置位）。
 */
struct PanelRegistrationRecord {
    std::string pluginId;  ///< 白名单 token（"requirements"——§11.1 八 token 词表）
    ui::TextKey titleKey;  ///< 标题文案键（"stage.requirements.title"——§3.5 键约定）
    ui::StageId stage = ui::StageId::Requirements;  ///< 挂位阶段（StageId=requirements——§9.8）
    PanelCapabilities capabilities;  ///< 能力声明（三能力全置位）
    bool advanced = false;           ///< UX-04 高级面板标记（false——主面板）
};

/**
 * @brief requirements 插件的装配登记记录（§10.9 registerPluginUi 入参的
 *        requirements 侧值——装配期由 L5 经 IPluginUiRegistrar 提交；
 *        单例形状值；"装配期一次注册"的登记数据面——acceptance 1）。
 *
 * @return 登记记录（pluginId="requirements"；stage=Requirements；能力三全）
 *
 * 纯函数；线程安全；确定性。
 */
PanelRegistrationRecord requirementsPanelRegistration();

// =====================================================================
// 域就绪投影（§11.2 readonlyProjections 的数据面——§6.5 汇聚源）
// =====================================================================

/**
 * @brief requirements 域的只读就绪投影（IPluginUiModule::readonlyProjections
 *        的 requirements 行——StageStatusModel 汇聚源；§6.5"汇聚不判定"
 *        纪律）。
 *
 * 字段来源（零判定——全部直投，P-REQ-6 边界：数据事实非门控动作）：
 *   - domainKey="requirements"（域注册键词表）；
 *   - inputComplete＝!report.hasBlocking()（事实查询直投——报告不存在
 *     Blocking 发现即输入完整；该位的**消费动作**归 workflow/ui 门控，
 *     本单元不判"能否进入某阶段"）；
 *   - verdict＝无 Blocking→core::EngineeringStatus::NotApplicable（工程
 *     判定非本单元所有权——需求未就绪≠工程不可行，§8.3 正交表；正式
 *     判定在评估域，本投影不以"无 Blocking"冒充"可行"）；
 *     有 Blocking→DataInsufficient（输入不完整——①级门禁数据源语义）；
 *   - missingItemKeys＝Warning 级发现的呈现键（"missing.<层 token>"——
 *     UX-02：键与局部名，零哈希；明细全文在校验面板逐项行，不重复入键）；
 *   - hasActiveTask=false（requirements 域无在途任务呈现面——任务呈现归
 *     ui ITaskPresentationModel，N-11 不双建）。
 *
 * @param report [in] 就绪报告（IRequirementReadinessChecker 产出——判定
 *               权威在计算库，本投影零判定）
 * @return 单元素投影（requirements 域一行——汇聚序由 ui 侧注册序决定）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<ui::DomainReadinessItem> requirementsReadinessProjection(
    const RequirementReadinessReport& report);

// =====================================================================
// L-R12 只读门控（writable=false→编辑禁用；浏览/预览/副本导出可用）
// =====================================================================

/**
 * @brief 只读会话下的检查器行使能投影（L-R12 的模型层半区——writable=
 *        false 时一切可编辑字段行降级为灰显只读）。
 *
 * 规则（卡 §9.8 L-R12 行原文"writable=false→编辑禁用、域命令
 * readOnlyAllowed=false、浏览/预览/副本导出可用"）：
 *   - 输入行使能＝Editable 且 writable=false → ReadOnlyGrey；
 *   - ReadOnlyGrey 行不受影响（灰显不因可写性"复活"——只读会话与溯源
 *     字段是两个独立灰显源，正交）。
 *   - "浏览/预览/副本导出可用"半区＝命令侧 readOnlyAllowed=true 的
 *     export-copy/capture-tcp/pick-feature（命令目录已逐条登记）——本
 *     函数只管行面，命令面在 requirementsDomainCommands（单一登记点）。
 *
 * @param rows     [in] 检查器行投影（stationFieldsFor 等产出）
 * @param writable [in] 会话可写性（ui 会话态——PM-07 只读横幅同源事实）
 * @return 降级后的行序列（同序同键——只变 enablement）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<StationFieldRow> applyReadOnlyGate(std::vector<StationFieldRow> rows,
                                               bool writable);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELCOMMANDCATALOG_HPP
