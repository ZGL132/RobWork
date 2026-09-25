/**
 * @file   ModelingUiModule.hpp
 * @brief  建模插件界面模块——ui.md §11.2 IPluginUiModule 的建模侧实现
 *         （装配缝；P-MDL-8 暂持形状）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（IPluginUiModule 三方法：onShellReady／
 *     readonlyProjections／buildDraftCommand）、§10.9（装配期经
 *     IPluginUiRegistrar 注册——装配期一次、白名单校验）、§8.5
 *     （buildDraftCommand＝draft.apply 的域侧命令组装）
 *   - units/modeling.md §9.7.2 L-3（draft.apply→IPluginUiModule.
 *     buildDraftCommand("modeling")→①端口 submit）、§9.3（命令处理器族
 *     ——commandType token 与载荷编解码的权威）
 *   - 需求 MDL-07、SA-15；任务契约 tasks/foundation/WP-13-T15.json
 *     acceptance 3/4
 *
 * ★ P-MDL-8 处置（契约 note ③）：ui 的 IPluginUiRegistrar/IPluginUiModule
 * 头尚未落位（ui.md §3.3 已登记落点、磁盘核对不存在——装配任务后续兑现）。
 * 本类按 §11.2 冻结文本**逐方法同形**实现（方法名/签名/语义对齐卡文；
 * 值面全部用已落位类型——ui::DomainReadinessItem/project::CommandEnvelope），
 * ui 侧头落位后本类改为继承 ui::IPluginUiModule（override 不变，语义零
 * 变化）——R-MDL-1 增量同步，登记于单元卡 §14.6。装配面（registrar 调用
 * 点）随 L5 装配任务（WP-24-T03）接线，本单元不自行装配（PA-1 命令入口
 * 权威归 ui）。
 *
 * 线程约束：仅 UI 线程访问（§3.4——模块持会话态与面板引用）。确定性：
 * buildDraftCommand 同会话态→同信封（编码确定性——Codec/CommandHandlers
 * 已钉）。
 */

#ifndef IRD_MODELING_PLUGIN_MODELINGUIMODULE_HPP
#define IRD_MODELING_PLUGIN_MODELINGUIMODULE_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>            // core::BranchId/RevisionId（信封身份面）
#include <sdurws/ird/modeling/CommandHandlers.hpp> // kCmdApplyRobotDesign/kCommandPayloadVersion/encodeCommandPayload（域命令面权威）
#include "PanelCommandCatalog.hpp"                // modelingReadinessProjection（§11.2 readonlyProjections 数据面——同目录私有头）
#include "PanelRefresh.hpp"                       // PanelUiThreadGuard（§3.4 线程守卫——零 Qt，同目录私有头）
#include <sdurws/ird/modeling/Readiness.hpp>       // ModelReadinessReport（就绪投影数据源）
#include <sdurws/ird/modeling/Template.hpp>        // ModelingWorkingSet（草稿态值）
#include <sdurws/ird/project/CommandService.hpp>   // project::CommandEnvelope（§8.5 返回值面——登记边）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>       // ui::IWorkbenchShell（onShellReady 入参——壳门面）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::DomainReadinessItem（§6.5 汇聚值面）

namespace sdurws::ird::modeling {

class ModelingPanelWidget;

/**
 * @brief 模块会话态（buildDraftCommand 的输入面——装配层在会话建立/草稿
 *        变更时更新；面板刷新同源）。
 *
 * 零缓存纪律（ACC5）的边界说明：本结构持有的是**会话权威态本身**（分支/
 * 草稿工作集），不是投影副本——工作集的唯一权威载体在此（装配层注册），
 * 面板每次投影经提供器现取（ModelingPanelWidget::setEditTargetProvider
 * 返回本结构的 draft 指针）。所有权：值持有（工作集按值存于本结构）。
 */
struct ModuleSessionState {
    core::BranchId branch{};                    ///< 目标分支（信封 branch——brn- 规范身份）
    ModelingWorkingSet draft{};                 ///< 当前草稿工作集（编辑态——§4.9）
    std::optional<core::RevisionId> baseRevision;  ///< 草稿基线修订（nullopt＝提交期解析 tip——§6.2）
    std::optional<ModelReadinessReport> readiness; ///< 最近一次就绪报告（readonlyProjections 源——判定权威在 checker）
};

/**
 * @brief 建模插件界面模块（ui.md §11.2 同形实现——三方法逐一对齐卡文）。
 *
 * 生命周期：装配期由插件装配点创建（UI 线程），存活至壳拆除（§10.9
 * "IPluginUiModule 存活至壳拆除"——装配层保证）。面板工厂经装配描述符
 * 提供（modelingPanelRegistration——PanelCommandCatalog.hpp）。
 */
class ModelingUiModule final {
public:
    ModelingUiModule() = default;
    /// 不可拷贝/不可移动（会话态与壳引用绑定生命周期——§10.9）。
    ModelingUiModule(const ModelingUiModule&) = delete;
    ModelingUiModule& operator=(const ModelingUiModule&) = delete;

    // ---- 会话接线（装配层调用——面板与工作集的权威落点）---------------

    /**
     * @brief 注册域面板工厂产物（装配期——面板 widget 由装配层创建后移交
     *        本模块弱语义引用〔非 owning——面板归 CentralAreaHost〕）。
     *
     * @param panel [in] 面板 widget（非 owning——调用方保证存活期覆盖模块）
     */
    void attachPanel(ModelingPanelWidget* panel) { m_panel = panel; }

    /// 会话态访问（装配层更新入口——工作集权威载体；仅 UI 线程）。
    ModuleSessionState& session() noexcept { m_guard.assertOnUiThread(); return m_session; }

    // ---- ui.md §11.2 三方法（逐方法对齐卡文——P-MDL-8 同形面）----------

    /**
     * @brief 壳就绪回调（§11.2 行"注册回调后初始化〔订阅/面板内容〕"）。
     *
     * 建模侧初始化面：持有壳引用（只读消费——recentProjects 等不涉及）；
     * 域命令的注册经装配描述符（modelingPanelRegistration＋
     * modelingDomainCommands）在装配期完成，本回调零重复注册（§10.9
     * "装配期一次"）。
     *
     * @param shell [in] 工作台壳门面（非 owning——存活期由壳侧保证）
     */
    void onShellReady(ui::IWorkbenchShell& shell) { m_shell = &shell; }

    /**
     * @brief 只读就绪投影（§11.2 行"§6.5 汇聚源"——StageStatusModel 汇聚
     *        的建模行；值语义直投 modelingReadinessProjection）。
     *
     * @return 单元素投影（会话就绪报告未载＝输入不完整空态行——不伪造
     *         可行性；判定权威在 IModelReadinessChecker）
     */
    std::vector<ui::DomainReadinessItem> readonlyProjections() const
    {
        // 无报告（会话未校验）＝输入不完整缺省行（零判定——默认值面）。
        if (!m_session.readiness.has_value()) {
            ui::DomainReadinessItem empty;
            empty.domainKey = "modeling";
            empty.verdict = core::EngineeringStatus::DataInsufficient;
            empty.inputComplete = false;
            return {empty};
        }
        return modelingReadinessProjection(*m_session.readiness);
    }

    /**
     * @brief 草稿应用命令组装（§11.2 行"§8.5 应用时命令组装〔域侧〕"——
     *        L-3 draft.apply 流的建模半区）。
     *
     * 组装规则（§9.3/§8.5——真实信封，非占位）：
     *   - moduleId≠"modeling"→nullopt（域外请求——调用方错误面）；
     *   - 草稿无变更记录（changes 空）→nullopt（无可应用内容——§8.5
     *     "无草稿可应用"态；不产生空修订）；
     *   - 否则→CommandEnvelope{branch=会话分支、expectedRevision=会话
     *     基线（nullopt＝tip——§6.2）、commandType=apply-robot-design、
     *     payloadFormatVersion=kCommandPayloadVersion(2)、payload=根对象
     *     槽〔allocateNew＝根身份未回填；字节＝RobotDesignCodec 确定性
     *     编码〕}——对象 canonical 字节经编码器产出（解码门在校验链④
     *     ——合法性由断言分域在 prepare 现场重估，本组装零判定）。
     *
     * @param moduleId [in] 域注册键（"modeling"——§11.2 原文参数形态）
     * @return 信封（有草稿变更时）；nullopt＝域外请求／无可应用变更
     *
 * @throws ui::ensureNoInternalIdentity 透传（displayName 哈希形态
 *               ——非法名不进信封，构造边界已拒的兜底守卫）
 */
    std::optional<project::CommandEnvelope> buildDraftCommand(const std::string& moduleId);

private:
    PanelUiThreadGuard m_guard;              ///< §3.4 UI 线程守卫（构造线程绑定）
    ModuleSessionState m_session;            ///< 会话权威态（工作集唯一载体——装配层更新）
    ModelingPanelWidget* m_panel = nullptr;  ///< 面板引用（非 owning——归装配层）
    ui::IWorkbenchShell* m_shell = nullptr;  ///< 壳门面引用（非 owning——§10.9 生命周期）
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_MODELINGUIMODULE_HPP
