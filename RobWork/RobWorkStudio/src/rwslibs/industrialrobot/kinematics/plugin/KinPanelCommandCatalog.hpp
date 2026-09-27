/**
 * @file   KinPanelCommandCatalog.hpp
 * @brief  kinematics 域命令登记目录与装配登记记录（零 Widgets）——§9.8
 *         清单七行（八个 CommandId）与 §10.9 装配描述符数据的唯一产出点。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（域命令登记清单表七行逐行——id/语义/
 *     readOnlyAllowed 权威；面板组成表五行的挂位面）；
 *   - units/ui.md §10.9/§11.1/§11.2（PluginUiDescriptor/PanelRegistration/
 *     IPluginUiModule；白名单 token="kinematics"）；§7.3（快捷键一律经
 *     HotkeyBindingTable——SA-16 插件不私占全局）；§7.6（只读条件）；
 *   - units/modeling.md §9.7.3 先例（PanelCommandCatalog 同构——WP-13-T15
 *     落位形态）；ui 公共头均已落位（IPluginUiRegistrar/ICommandRegistry，
 *     P-KIN-7 对端冻结）——本目录直接以 ui::CommandDescriptor/
 *     ui::PluginUiDescriptor 冻结形状承载（无 P-MDL-8 期的"暂持"过渡）；
 *   - 需求 UX-04/UX-05、SA-16；任务契约 tasks/foundation/WP-15-T12.json
 *     acceptance 3（域命令注册）。
 *
 * 线程约束：全部函数纯函数（无共享可变状态），可重入。确定性：同调用同
 * 清单（行序＝卡 §9.8 表行序，登记序即白名单序呈现依据——NFR-COR-02）。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINPANELCOMMANDCATALOG_HPP
#define IRD_KINEMATICS_PLUGIN_KINPANELCOMMANDCATALOG_HPP

#include <string>
#include <vector>

#include <sdurws/ird/ui/ICommandRegistry.hpp>  // ui::CommandDescriptor/CommandScope/CommandCategory（冻结形状）
#include <sdurws/ird/ui/UiTypes.hpp>           // ui::StageId/DomainReadinessItem/TextKey（§6.5 单侧冻结形状）

namespace sdurws {
namespace ird {
namespace kinematics {

// =====================================================================
// §9.8 域命令清单（表七行八个 id——acceptance 3 的权威逐行对照）
// =====================================================================

/**
 * @brief kinematics 域命令描述符清单（§9.8 表全量七行——set-default-tcp 与
 *        set-default-device 同行两 id，共八条独立命令）。
 *
 * 逐行对照（id→readOnlyAllowed，卡面权威——测试逐条断言）：
 *   kinematics.analyze-pose→true（当前位姿指标——会话级）、
 *   kinematics.solve-ik→true（单点 IK——会话级；正式验证走批量任务）、
 *   kinematics.validate-task-points→false（批量验证提交——写 results 正式）、
 *   kinematics.evaluate-coverage→false（区域覆盖提交——写 results 正式）、
 *   kinematics.set-default-tcp→false（KIN-14 经①端口——新修订）、
 *   kinematics.set-default-device→false（KIN-14 经①端口——新修订）、
 *   kinematics.export-results→true（JSON/CSV 副本导出）、
 *   kinematics.reset-session-pose→true（复位 Home——会话）。
 *
 * 其余登记要素：ownerUnit="kinematics"（白名单 token——§11.1 词表）；
 * 作用域＝卡面"会话级"→ui::CommandScope::Session、"写 results/修订/导出"→
 * ui::CommandScope::Project（项目作用域）；category=Stage（阶段面命令族，
 * modeling 先例同案）；快捷键＝defaultShortcut 全空＋bindable=true（SA-16：
 * 插件不私占全局快捷键——需要绑定时一律经 ui HotkeyBindingTable 用户级
 * 配置，默认零绑定）；titleKey 按 §3.5 键约定 "cmd.<id>.title"。
 *
 * @return 八条描述符（行序＝卡表行序；每次调用现产清单，无缓存）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<ui::CommandDescriptor> kinematicsDomainCommands();

/**
 * @brief 命令 id 词表（点分）与 project commandType 词表（无点）的命名
 *        空间分离守卫值（modeling 先例同款交叉断言面）。
 *
 * @return kinematics 域的 project 侧 commandType token 清单（kFacadedModeling
 *         Command 唯一值"apply-robot-design"——Commands.hpp 冻结常量直用）。
 *         供测试做交叉断言：任一 kinematics.* CommandId 不得出现在本清单、
 *         反之亦然（词表交叉即违约）。
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<std::string> kinematicsProjectCommandTokens();

/**
 * @brief 写类命令判定（L-K11 只读门控的 id 半区——readOnlyAllowed=false
 *        的命令族）。
 *
 * @param commandId [in] 命令 id（点分小写）
 * @return true＝写类命令（只读会话禁用＋提交拒绝）；false＝只读会话可用
 *
 * 纯函数；线程安全；确定性。
 */
bool isKinWriteCommand(const std::string& commandId);

// =====================================================================
// 装配登记记录（§10.9 冻结形状——ui 对端已落位，直用 ui 类型）
// =====================================================================

/**
 * @brief kinematics 插件的装配登记记录值（§9.8 面板表五行的挂位面——
 *        四主面板＋求解配置高级面板）。
 *
 * 字段与测试断言锚：
 *   - pluginId="kinematics"（白名单 token——§11.1 八 token 词表）；
 *   - titleKey="stage.kinematics.title"（§3.5 键约定）；
 *   - stage=StageId::Kinematics（§6.4 七阶段第 3 位）；
 *   - 能力声明三全（提供阶段面板/参与汇聚/登记命令——§9.8 三事实）；
 *   - panels 五行（§9.8 面板表行序）：位姿指标/任务点验证/区域覆盖/
 *     结果与可视化四主面板（advanced=false）＋求解配置高级面板
 *     （advanced=true——UX-04 高级参数收拢）。
 */
struct KinPanelRegistration {
    /// 白名单 token（§11.1 词表）。
    std::string pluginId = "kinematics";
    /// 插件标题文案键（"stage.kinematics.title"）。
    ui::TextKey titleKey = "stage.kinematics.title";
    /// 挂位阶段（StageId=Kinematics）。
    ui::StageId stage = ui::StageId::Kinematics;
    /// 能力声明（§10.9 PluginCapabilities——描述性非判定性）。
    bool providesStagePanel = true;          ///< 提供阶段面板
    bool providesReadonlyProjection = true;  ///< 参与 StageStatusModel 汇聚
    bool registersCommands = true;           ///< 登记命令
    /// 面板登记五行（id 键/标题键/advanced 标记——工厂经装配门面转接模块）。
    struct PanelEntry {
        /// 面板稳定键（装配报告/测试定位锚——小写连字符词形）。
        std::string key;
        /// 面板标题文案键（"stage.kinematics.panel.<key>.title"——§3.5 键
        /// 约定的面板子键扩展）。
        ui::TextKey titleKey;
        /// UX-04 高级面板标记（false＝主面板）。
        bool advanced = false;
    };
    /// 面板五行（行序＝§9.8 面板表行序——确定性登记序）。
    std::vector<PanelEntry> panels;
};

/**
 * @brief 产出装配登记记录（§9.8 面板表值化——装配门面转 ui
 *        PluginUiDescriptor 的唯一数据源）。
 *
 * @return 登记记录（每次调用现产，无缓存）
 *
 * 纯函数；线程安全；确定性。
 */
KinPanelRegistration kinematicsPanelRegistration();

// =====================================================================
// 域就绪投影（§11.2 readonlyProjections 的数据面——§6.5 汇聚源）
// =====================================================================

/**
 * @brief kinematics 域的只读就绪投影（IPluginUiModule::readonlyProjections
 *        的 kinematics 行——StageStatusModel 汇聚源，§6.5"汇聚不判定"）。
 *
 * P-UI-6 处置（服从 ui 卡原登记裁决）：workflow 门控三方契约未定稿前按
 * ui::DomainReadinessItem §6.5 冻结形状直投（UiTypes.hpp 头注的谈判起点
 * 口径——不私改对端）。
 *
 * 字段来源（零判定——全部直投）：
 *   - domainKey="kinematics"（域注册键词表）；
 *   - verdict/inputComplete＝在途评估的工程判定投影：域无正式判定在途/
 *     无归档结果消费时呈 DataInsufficient 缺省行（输入不完整——不伪造
 *     可行性，UX-02/ERR-01；判定权威在 evidence 汇总，插件零自判）；
 *   - missingItemKeys＝缺项稳定键（"missing.<layer-token>" 词形——UX-02）；
 *   - hasActiveTask＝域在途任务事实（L-K3/L-K6 提交受理后置位——ui
 *     ITaskPresentationModel 为权威呈现，此处只供汇聚）。
 *
 * @param inputComplete [in] 就绪校验结论（域就绪事实——插件透传不判定）
 * @param hasActiveTask [in] 在途任务事实
 * @return 单元素投影（kinematics 域一行——汇聚序由 ui 侧注册序决定）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<ui::DomainReadinessItem> kinematicsReadinessProjection(
    bool inputComplete, bool hasActiveTask);

// =====================================================================
// L-K11 只读门控（writable=false→写类命令禁用——§9.8 L-K11 行）
// =====================================================================

/**
 * @brief 只读会话下的命令使能投影（L-K11 的模型层半区——writable=false
 *        时写类命令降级禁用，只读类命令保持可用）。
 *
 * 规则（§9.8 L-K11 行原文"writable=false→提交正式评估〔写 results〕拒绝
 * ＋诊断；会话级 FK 预览/单位切换可用"）：
 *   - 写类命令（isKinWriteCommand==true）且 writable=false → 禁用；
 *   - 只读类命令（analyze-pose/solve-ik/export-results/reset-session-pose）
 *     不受可写性影响——export-results 是副本导出（AT-04 零修订结构保证）。
 *
 * @param commandId [in] 命令 id
 * @param writable  [in] 会话可写性（ui 会话投影同源事实）
 * @return true＝当前会话下该命令可激活
 *
 * 纯函数；线程安全；确定性。
 */
bool kinCommandEnabledInSession(const std::string& commandId, bool writable);

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELCOMMANDCATALOG_HPP
