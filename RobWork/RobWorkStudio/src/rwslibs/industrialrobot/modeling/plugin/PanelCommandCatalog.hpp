/**
 * @file   PanelCommandCatalog.hpp
 * @brief  建模域命令登记目录（零 Widgets）——十条 CommandId 与装配描述符
 *         （卡 §9.7.3；ui.md §10.9/§11.2）。
 *
 * 设计依据：
 *   - units/modeling.md §9.7.3（域命令登记清单——装配期经 IPluginUiRegistrar；
 *     CommandId 点分小写＝ui CommandId 词表，与 project commandType 无点
 *     词表是两套命名空间，不混用）、§9.7.2 L-7（只读模式 writable=false→
 *     编辑控件禁用、域命令 readOnlyAllowed=false 禁用）
 *   - units/ui.md §10.9（PluginUiDescriptor/PanelRegistration/插件能力
 *     声明）、§11.1（静态白名单 token="modeling"）、§11.2（IPluginUiModule
 *     三方法）、§7.3（快捷键一律经 HotkeyBindingTable——SA-16 插件不私占
 *     全局）
 *   - 需求 MDL-07、SA-16；任务契约 tasks/foundation/WP-13-T15.json
 *     acceptance 4
 *
 * ★ P-MDL-8 处置（契约 note ③）：ui 的 IPluginUiRegistrar.hpp 尚未落位
 * （ui.md §3.3 已登记该头为 §10.9 的落点；磁盘核对不存在——装配任务
 * 后续落地）。本目录以**已落位的 ui 公共类型**为承载：
 *   - 十条命令＝ui::CommandDescriptor（ICommandRegistry.hpp——UI-T06 落位，
 *     冻结形状）；命令装配数据面与 §10.9 PluginUiDescriptor.commands 逐
 *     字段对齐（id/ownerUnit/titleKey/category/scope/readOnlyAllowed/
 *     bindable/defaultShortcut/menuPath）；
 *   - 面板/能力登记＝PanelRegistrationRecord（本头承载的 §10.9 形状值——
 *     同 AboutDialog.hpp 承载 PluginAssemblyReport 的"冻结形状暂持"先例，
 *     NFR-MNT-03：对端落位后迁移至 ui 类型，R-MDL-1 增量同步）。
 *
 * 线程约束：全部函数纯函数（无共享可变状态），可重入。确定性：同调用同
 * 清单（NFR-COR-02——行序＝卡 §9.7.3 表行序，登记序即白名单序呈现依据）。
 */

#ifndef IRD_MODELING_PLUGIN_PANELCOMMANDCATALOG_HPP
#define IRD_MODELING_PLUGIN_PANELCOMMANDCATALOG_HPP

#include <string>
#include <vector>

#include <sdurws/ird/modeling/Readiness.hpp>  // ModelReadinessReport（域就绪投影数据源）
#include "PanelModel.hpp"                     // PropertyFieldRow（L-7 只读门控的输入行——同目录插件私有头）
#include <sdurws/ird/ui/ICommandRegistry.hpp> // ui::CommandDescriptor/CommandId/CommandScope/CommandCategory（UI-T06 冻结形状）
#include <sdurws/ird/ui/UiTypes.hpp>          // ui::StageId/DomainReadinessItem/TextKey（§6.5 单侧冻结形状）

namespace sdurws::ird::modeling {

// =====================================================================
// 十条域命令（卡 §9.7.3 表逐行——acceptance 4 的权威清单）
// =====================================================================

/**
 * @brief 建模域命令清单（§9.7.3 表全量——十条独立命令；export-package 与
 *        import-package 为两条，不得合并）。
 *
 * 逐行对照（id→readOnlyAllowed，卡面权威）：
 *   modeling.new-from-template→false、modeling.import-urdf→false、
 *   modeling.import-xacro→false、modeling.switch-authority→false、
 *   modeling.estimate-properties→false、
 *   modeling.generate-placeholder-geometry→false、
 *   modeling.diff-baseline→true、modeling.export-package→true、
 *   modeling.import-package→false、modeling.reset-home-zero→true。
 *
 * 其余登记要素：ownerUnit="modeling"（白名单 token——§11.1 词表）；作用域
 * 映射＝卡面"Stage/Project"→ui::CommandScope::Project（项目作用域）、
 * "Session"→ui::CommandScope::Session（会话态）；category=Stage（阶段面
 * 命令族——§7.1 分类词表）；快捷键＝defaultShortcut 全空＋bindable=true
 * （SA-16：插件不私占全局快捷键——需要绑定时一律经 ui HotkeyBindingTable
 * 用户级配置，默认零绑定）；titleKey 按 §3.5 键约定 "cmd.<id>.title"。
 *
 * @return 十条描述符（行序＝卡表行序——登记序；每次调用现产清单，无缓存）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<ui::CommandDescriptor> modelingDomainCommands();

/**
 * @brief 命令 id 词表（点分小写）与 project commandType 词表（无点）的
 *        命名空间分离守卫值。
 *
 * 背景（卡 §9.7.3 括注）：CommandId 词表（ui）与 project commandType 词表
 * （project §4.4.4 语法 ^[a-z0-9-]{3,64}）是两套命名空间，不混用。本函数
 * 返回 modeling 域的 project 侧 commandType token 清单（kCmdApply* 五命令
 * ——CommandHandlers.hpp 冻结常量直用），供测试做交叉断言：任一 CommandId
 * 不得出现在 commandType 清单、反之亦然（词表交叉即违约）。
 *
 * @return project 侧 token 清单（apply-robot-design/apply-tool-definition/
 *         apply-scene-objects/apply-named-poses——四条本单元已落位命令族）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<std::string> modelingProjectCommandTokens();

// =====================================================================
// 装配描述符（§10.9 形状承载——P-MDL-8：对端落位后迁移，R-MDL-1）
// =====================================================================

/**
 * @brief 插件能力声明（§10.9 PluginCapabilities 形状——描述性，非判定性）。
 */
struct PanelCapabilities {
    bool providesStagePanel = false;        ///< 提供阶段面板（modeling＝true）
    bool providesReadonlyProjection = false;///< 参与StageStatusModel汇聚（modeling＝true）
    bool registersCommands = false;         ///< 登记命令（modeling＝true）
};

/**
 * @brief 面板登记记录（§10.9 PanelRegistration＋PluginUiDescriptor 的合并
 *        形状承载——ui.md 冻结文本的值化；P-MDL-8 暂持先例见文件头注）。
 *
 * panelFactory 语义＝§10.9 PanelRegistration.factory（创建 QWidget 族面板，
 * ui 线程调用）——形状以工厂返回类型 widget 层类型表达（本头零 Widgets：
 * 工厂经 widget 层函数指针注入，见 ModelingUiModule）。advanced=false
 * （建模面板为 UX-09 主面板——高级面板标记不置位）。
 */
struct PanelRegistrationRecord {
    std::string pluginId;          ///< 白名单 token（"modeling"——§11.1 八 token 词表）
    ui::TextKey titleKey;          ///< 标题文案键（"stage.modeling.title"——§3.5 键约定）
    ui::StageId stage = ui::StageId::Modeling;  ///< 挂位阶段（StageId=modeling——卡 §9.7.1）
    PanelCapabilities capabilities;             ///< 能力声明（三能力全置位）
    bool advanced = false;                      ///< UX-04 高级面板标记（false——主面板）
};

/**
 * @brief 建模插件的装配登记记录（§10.9 registerPluginUi 入参的建模侧
 *        值——装配期由 L5 经 IPluginUiRegistrar 提交；单例形状值）。
 *
 * @return 登记记录（pluginId="modeling"；stage=Modeling；能力三全）
 *
 * 纯函数；线程安全；确定性。
 */
PanelRegistrationRecord modelingPanelRegistration();

// =====================================================================
// 域就绪投影（§11.2 readonlyProjections 的数据面——§6.5 汇聚源）
// =====================================================================

/**
 * @brief 建模域的只读就绪投影（IPluginUiModule::readonlyProjections 的
 *        建模行——StageStatusModel 汇聚源，§6.5"汇聚不判定"纪律）。
 *
 * 字段来源（零判定——全部直投）：
 *   - domainKey="modeling"（域注册键词表）；
 *   - verdict/inputComplete＝就绪报告汇总态直投（Ready→EngineringStatus
 *     正常值＋true；其余→输入不完整）；
 *   - missingItemKeys＝报告 notes 中呈现级缺项的稳定键（UX-02：界面只见
 *     键与局部名，零哈希——键词表固定 "missing.<layer-token>"）；
 *   - hasActiveTask=false（建模域无在途任务呈现面——任务呈现归 ui
 *     ITaskPresentationModel，N-11 不双建）。
 *
 * @param report [in] 就绪报告（IModelReadinessChecker 产出）
 * @return 单元素投影（建模域一行——汇聚序由 ui 侧注册序决定）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<ui::DomainReadinessItem> modelingReadinessProjection(const ModelReadinessReport& report);

// =====================================================================
// L-7 只读门控（writable=false→编辑控件禁用——§9.7.2 L-7）
// =====================================================================

/**
 * @brief 只读会话下的属性区使能投影（L-7 的模型层半区——writable=false
 *        时一切可编辑字段行降级为灰显只读）。
 *
 * 规则（卡 §9.7.2 L-7 行原文"writable=false→编辑控件禁用、域命令
 * readOnlyAllowed=false；浏览/选中/单位切换/预览可用"）：
 *   - 输入行使能＝Editable 且 writable=false → ReadOnlyGrey；
 *   - ReadOnlyGrey/Hidden 行不受影响（灰显不因可写性"复活"）；
 *   - 浏览/选中/预览面（树/就绪条/预览页）不在本投影范围（恒可用）。
 *
 * @param rows     [in] 属性区投影行（propertyFieldsFor 产出）
 * @param writable [in] 会话可写性（ui 会话态——PM-07 只读横幅同源事实）
 * @return 降级后的行序列（同序同键——只变 enablement）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<PropertyFieldRow> applyReadOnlyGate(std::vector<PropertyFieldRow> rows,
                                                bool writable);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_PANELCOMMANDCATALOG_HPP
