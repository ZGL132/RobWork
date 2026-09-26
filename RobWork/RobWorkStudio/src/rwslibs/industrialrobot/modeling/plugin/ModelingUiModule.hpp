/**
 * @file   ModelingUiModule.hpp
 * @brief  建模插件界面模块——ui::IPluginUiModule（ui.md §11.2）的建模侧
 *         实现（装配缝；WP-24-T03 首版装配起为正式继承形态）。
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
 *     acceptance 3/4；装配落位＝WP-24-T03 首版（owner 指示 2026-09-26
 *     提前启动）
 *
 * ★ P-MDL-8 消账（WP-24-T03 首版装配）：ui 的 IPluginUiRegistrar/
 * IPluginUiModule 头已随首版装配落位（ui/include/sdurws/ird/ui/），本类
 * 自"逐方法同形实现"切换为**继承 ui::IPluginUiModule**（三方法 override，
 * 签名/语义零变化——R-MDL-1 增量同步预告的兑现，登记于单元卡 §14.6）。
 * 装配面（registrar 调用点）＝宿主插件装配序列（ui 插件 DomainAssembly）
 * 经装配门面 assembly/…/ModelingPluginAssembly.hpp 消费本模块——本单元
 * 不自行装配（PA-1 命令入口权威归 ui）。
 *
 * 线程约束：仅 UI 线程访问（§3.4——模块持会话态与面板引用）。确定性：
 * buildDraftCommand 同会话态→同信封（编码确定性——Codec/CommandHandlers
 * 已钉）。
 */

#ifndef IRD_MODELING_PLUGIN_MODELINGUIMODULE_HPP
#define IRD_MODELING_PLUGIN_MODELINGUIMODULE_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QString>                                // 文案解析器返回值（bindTextResolver——插件目标 Qt 面）

#include <sdurws/ird/core/Identity.hpp>            // core::BranchId/RevisionId（信封身份面）
#include <sdurws/ird/modeling/CommandHandlers.hpp> // kCmdApplyRobotDesign/kCommandPayloadVersion/encodeCommandPayload（域命令面权威）
#include <sdurws/ird/ui/IDraftController.hpp>      // ui::IModuleDraftSource（§10.5 模块草稿源——T03b-1 接入）
#include <sdurws/ird/ui/IPluginUiModule.hpp>       // ui::IPluginUiModule（§11.2 接口——P-MDL-8 消账后本类正式继承）
#include "PanelCommandCatalog.hpp"                // modelingReadinessProjection（§11.2 readonlyProjections 数据面——同目录私有头）
#include "PanelRefresh.hpp"                       // PanelUiThreadGuard（§3.4 线程守卫——零 Qt，同目录私有头）
#include <sdurws/ird/modeling/Readiness.hpp>       // ModelReadinessReport（就绪投影数据源）
#include <sdurws/ird/modeling/Template.hpp>        // ModelingWorkingSet（草稿态值）
#include <sdurws/ird/modeling/CommandHandlers.hpp> // AssertionSuite（就绪真判定——T03b-2b）
#include <sdurws/ird/policy/Contexts.hpp>          // policy::IPolicyNameContext（R-4 注入面——草稿桩）
#include <sdurws/ird/policy/JointLimits.hpp>       // policy::makeJointLimitEvaluator（行程评估器装配）
#include <sdurws/ird/project/CommandService.hpp>   // project::CommandEnvelope（§8.5 返回值面——登记边）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>       // ui::IWorkbenchShell（onShellReady 入参——壳门面）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::DomainReadinessItem（§6.5 汇聚值面）

class QWidget;  // 前置声明：createPanel 返回类型（全局域——插件目标 Widgets 面可用）

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
 * @brief 建模插件界面模块（ui::IPluginUiModule 的建模侧实现——WP-24-T03
 *        首版装配起为正式继承形态；P-MDL-8 同形面消账）。
 *
 * 生命周期：装配期由插件装配点创建（UI 线程），存活至壳拆除（§10.9
 * "IPluginUiModule 存活至壳拆除"——装配层保证）。面板工厂经装配描述符
 * 提供（modelingPanelRegistration——PanelCommandCatalog.hpp）。
 */
class ModelingUiModule final : public ui::IPluginUiModule,
                               public ui::IModuleDraftSource {
public:
    ModelingUiModule();  // cpp 定义——装配草稿名称桩与行程评估器（T03b-2b）
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
    void onShellReady(ui::IWorkbenchShell& shell) override { m_shell = &shell; }

    /**
     * @brief 只读就绪投影（§11.2 行"§6.5 汇聚源"——StageStatusModel 汇聚
     *        的建模行；值语义直投 modelingReadinessProjection）。
     *
     * @return 单元素投影（会话就绪报告未载＝输入不完整空态行——不伪造
     *         可行性；判定权威在 IModelReadinessChecker）
     */
    std::vector<ui::DomainReadinessItem> readonlyProjections() const override
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
    std::optional<project::CommandEnvelope> buildDraftCommand(const std::string& moduleId) override;

    // ---- 首版装配 API（WP-24-T03——装配门面 ModelingPluginAssembly 转发）--

    /// 命令提交出口形态（面板同款——装配层经本转发绑定面板）。
    using CommandSubmitFn = std::function<void(const ui::CommandId&)>;

    /**
     * @brief 绑定命令提交出口（面板创建前后皆可——后绑定在面板创建时
     *        应用，前绑定即时转发面板；与面板 setCommandSubmit 同语义）。
     */
    void bindCommandSubmit(CommandSubmitFn submitFn);

    /**
     * @brief 绑定文案解析器（titleKey→工程用语——宿主接 ui::resolveText；
     *        面板创建时应用。不绑定＝按钮呈现键名原文——不虚构文案）。
     */
    void bindTextResolver(std::function<QString(const std::string&)> resolve);

    /**
     * @brief 首版装配会话种子（generic-6r 草稿经真实 createDraft 产出——
     *        会话工作集权威载体；readiness 不预置＝投影呈 DataInsufficient
     *        缺省行，判定接线随收口任务）。
     */
    void seedTemplateSession();

    /**
     * @brief 创建面板（§10.9 PanelRegistration.factory 的模块半区——内部
     *        完成会话提供器/提交出口/文案解析接线与 attachPanel；每次调用
     *        新建，装配层恰调一次）。
     *
     * @return 面板 widget（归调用方接管——宿主层持有）
     */
    QWidget* createPanel();

    // ---- 会话锚定与应用回执（T03b-2——apply 网关的模块半区）------------

    /**
     * @brief 绑定会话锚（打开成功后由装配层调用——分支＋当前 tip；此后
     *        buildDraftCommand 产出的信封携带真实分支，baseRevision 锚定
     *        该 tip＝Stale 判据有效）。
     */
    void bindSessionAnchor(const core::BranchId& branch,
                           const core::RevisionId& base);

    /**
     * @brief 应用回执回写（submit Committed 后由装配层调用——编辑基线
     *        前移到新修订；根对象身份按提交结果回填（替换 allocateNew
     *        语义——重复应用不再二次分配）；已应用编辑记录清零）。
     */
    void noteAppliedRevision(const core::RevisionId& newBase,
                             const std::optional<core::ObjectId>& rootObjectId);

    // ---- 模块草稿源（T03b-1——ui::IModuleDraftSource 四方法；§10.5）----

    /// 模块句柄（ModuleDraftHandle 词表——与 descriptor.pluginId 同 token）。
    static constexpr char kModuleHandle[] = "modeling";

    /// 显示名（UX-02 工程用语——草稿表/横幅/冲突对话框呈现值源）。
    std::string displayName() const override;

    /// 组装当前草稿文档（落盘分派时控制器取值——payload＝根对象 canonical
    /// 字节，与命令 payload 同源编码；归属三元组/时刻/origin 由控制器补齐）。
    ui::DraftDocumentProjection buildDraftDocument() const override;

    /// 承接恢复的草稿文档（打开期 restoreOnOpen——payload 解码回写工作集
    /// design；解码失败保持现状不中断打开，诚实边界登记于 §15）。
    void adoptRestoredDocument(const ui::DraftDocumentProjection& document) override;

    /// 以指定修订重建编辑基线（§8.5[基于当前版本重新编辑]——Stale 冲突
    /// 对话的用户迁移半区；会话基线改写为 tip）。
    void rebuildOnRevision(const std::string& tipRevisionCanonical) override;

    // ---- 就绪真判定（T03b-2b——真实 ModelReadinessChecker 装配）--------

    /**
     * @brief 重算就绪报告（真实判定——AssertionSuite＋policy 评估器；
     *        结果写会话 readiness 并驱动面板就绪条刷新）。编辑落点/
     *        锚定/种子后调用。
     *
     * 诚实边界：草稿阶段 CheckContext 取缺省（无已装载策略）——L11 层
     * 对"策略不可解析"如实产出 Blocking 行（行程校验未执行的事实随行）
     * ；名称上下文为草稿桩（runtime 名在编译前不存在，返回 nullopt）。
     */
    void recomputeReadiness();

private:
    PanelUiThreadGuard m_guard;              ///< §3.4 UI 线程守卫（构造线程绑定）
    ModuleSessionState m_session;            ///< 会话权威态（工作集唯一载体——装配层更新）
    ModelingPanelWidget* m_panel = nullptr;  ///< 面板引用（非 owning——归装配层）
    ui::IWorkbenchShell* m_shell = nullptr;  ///< 壳门面引用（非 owning——§10.9 生命周期）
    CommandSubmitFn m_pendingSubmit;         ///< 面板创建前的提交出口暂存（创建时应用）
    std::function<QString(const std::string&)> m_textResolver;  ///< 文案解析（创建时应用）
    std::unique_ptr<policy::IJointLimitEvaluator> m_evaluator{
        policy::makeJointLimitEvaluator()};  ///< 行程评估器（policy 唯一实现——T03b-2b）
    std::unique_ptr<policy::IPolicyNameContext> m_nameContext;  ///< 草稿名称桩（见 cpp——nullopt 语义）
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_MODELINGUIMODULE_HPP
