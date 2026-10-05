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
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <QString>                                // 文案解析器返回值（bindTextResolver——插件目标 Qt 面）

#include <sdurws/ird/core/Identity.hpp>            // core::BranchId/RevisionId（信封身份面）
#include <sdurws/ird/modeling/CommandHandlers.hpp> // kCmdApplyRobotDesign/kCommandPayloadVersion/encodeCommandPayload（域命令面权威）
#include <sdurws/ird/ui/IDraftController.hpp>      // ui::IModuleDraftSource（§10.5 模块草稿源——T03b-1 接入）
#include <sdurws/ird/ui/IPluginUiModule.hpp>       // ui::IPluginUiModule（§11.2 接口——P-MDL-8 消账后本类正式继承）
#include <sdurws/ird/ui/UiPorts.hpp>               // ui::IUiDomainReadinessSource（§6.5 汇聚源端口——T03b 收口）
#include "PanelCommandCatalog.hpp"                // modelingReadinessProjection（§11.2 readonlyProjections 数据面——同目录私有头）
#include "PanelRefresh.hpp"                       // PanelUiThreadGuard（§3.4 线程守卫——零 Qt，同目录私有头）
#include <sdurws/ird/modeling/Readiness.hpp>       // ModelReadinessReport（就绪投影数据源）
#include <sdurws/ird/modeling/Template.hpp>        // ModelingWorkingSet（草稿态值）
#include <sdurws/ird/modeling/CommandHandlers.hpp> // AssertionSuite（就绪真判定——T03b-2b）
#include <sdurws/ird/policy/Contexts.hpp>          // policy::IPolicyNameContext（R-4 注入面——T03b 起为映射适配器）
#include <sdurws/ird/policy/JointLimits.hpp>       // policy::makeJointLimitEvaluator（行程评估器装配）
#include <sdurws/ird/policy/PolicySet.hpp>         // policy::EngineeringPolicySet（策略装载注入面——T03b 收口）
#include <sdurws/ird/project/CommandService.hpp>   // project::CommandEnvelope（§8.5 返回值面——登记边）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>       // ui::IWorkbenchShell（onShellReady 入参——壳门面）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::DomainReadinessItem（§6.5 汇聚值面）
#include "HostMigrationProviders.hpp"              // 迁移三接入面（WP-13-T20——同目录私有头）

namespace sdurws {
namespace ird {
namespace runtime {
class RuntimeNameMap;  // 前置声明（bindRuntimeNameMap 入参——完整类型仅 cpp 消费；R-4 零拼装转发面）
}  // namespace runtime
namespace ui {
class SelectionService;  // 前置声明（attachSelectionService 入参——完整类型随 HostMigrationProviders 传递）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

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
    /// 最近应用的基线快照（UI-T41——diff-baseline 的 baseline 侧；应用时刻
    /// 的草稿内容定格，编辑不回写。会话态非面板缓存——§9.7.4 不违 ACC5）。
    std::optional<ModelingWorkingSet> baselineSnapshot;
    /// 已应用修订预览视图（UI-T41 A3——AppliedRevisionView 定格于应用时刻；
    /// nullopt＝无已应用修订——预览页空态。撤销/重做事件即失效——内容未知）。
    std::optional<AppliedRevisionView> appliedPreview;
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
                               public ui::IModuleDraftSource,
                               public ui::IUiDomainReadinessSource {
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
    void attachPanel(ModelingPanelWidget* panel)
    {
        m_panel = panel;
        attachPanelWiring();  // 预览供给与已定格预览的接线（UI-T59——面板
                              // 晚于绑定/定格的装配序补齐）
    }

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
     * @brief 域就绪汇聚源（ui::IUiDomainReadinessSource——§6.5
     *        StageStatusModel 汇聚的端口半区；T03b 收口落位）。
     *
     * 线程契约（UiPorts.hpp 端口行）：readinessSnapshot 供 workflow 任意
     * 线程拉取——本实现经互斥锁保护的就绪快照应答（值拷贝、短临界区），
     * 与 UI 线程的 recomputeReadiness 写入并发安全；纯查询不抛，零判定
     * （N-11——汇聚输入不加工，判定权威在 IModelReadinessChecker）。
     *
     * @param stage [in] 目标阶段；非建模阶段＝空清单（该阶段无本源投影）
     * @return 建模域就绪项（域未校验＝DataInsufficient 缺省行——不伪造）
     */
    std::vector<ui::DomainReadinessItem> domainReadiness(
        ui::StageId stage) const override;

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
    using CommandAvailabilityFn = std::function<ui::CommandAvailability(const ui::CommandId&)>;

    /**
     * @brief 绑定命令提交出口（面板创建前后皆可——后绑定在面板创建时
     *        应用，前绑定即时转发面板；与面板 setCommandSubmit 同语义）。
     */
    void bindCommandSubmit(CommandSubmitFn submitFn);
    void bindCommandAvailability(CommandAvailabilityFn availability);

    /**
     * @brief 绑定文案解析器（titleKey→工程用语——宿主接 ui::resolveText；
     *        面板创建时应用。不绑定＝按钮呈现键名原文——不虚构文案）。
     */
    void bindTextResolver(std::function<QString(const std::string&)> resolve);

    /**
     * @brief 绑定 WC/DWC XML 外供导出回调（UI-T56——export-workcell-xml
     *        落点；宿主侧取编译快照→域 exportWorkCellXml→摘要形成）。
     *
     * 快照缺席（未应用修订/编译失败）＝回调返回 false＋原因入 summary——
     * 模块与流程零判定（诚实缺席不虚构产物）；不绑定＝命令执行报装配
     * 缺陷（fail-closed 同 kAssembled 口径）。
     */
    void bindWorkCellExport(
        std::function<bool(const std::string& targetPath, std::string& summary)> exportFn);

    /**
     * @brief 绑定预览内存导出回调（UI-T59——F-498 预览半区；宿主侧取编译
     *        快照→域 exportPreviewXml→来源头行组装）。
     *
     * 回调契约（与 bindWorkCellExport 同形——模块零 runtime 类型依赖）：
     * 入参 kindToken＝域 previewExportKindToken 词表（"serial-device-xml" 等
     * 四值）；返回 false＝快照缺席/能力缺席（原因入 reason——诚实缺席不
     * 虚构产物）；返回 true＝headerLine（来源修订号/模型身份/生成时间——
     * 宿主组装，非 XML）/sourceObject（域侧来源对象摘要）/text（域导出
     * XML）三值有效。不绑定＝面板 XML 类预览诚实呈现"未接线"（fail-closed
     * ——UI 不自行拼 XML 的强制点）。
     */
    void bindWorkCellPreview(
        std::function<bool(const std::string& kind, std::string& headerLine,
                           std::string& sourceObject, std::string& text,
                           std::string& reason)> previewFn);


    /**
     * @brief 会话可写性切换（UI-T43——L-7 门控输入的模块转发面，需求域
     *        RequirementsUiModule::setWritable 同构先例）。
     *
     * 门控事实源＝ui 只读横幅同源的宿主打开报告（report.opened.metadata
     * .writable——L-R12 同款纪律），本转发面零判定：面板已创建＝即时转发
     * （面板内即时重算属性行灰显与命令按钮使能）；面板未创建＝暂存初值，
     * createPanel 时作为面板初始可写态应用（装配序无关——与命令提交出口
     * 暂存同一时序语义，杜绝"面板恒按可写创建"的初值失实）。
     *
     * @param writable [in] 会话可写性（true＝可写会话；false＝只读/降级只读）
     */
    void setWritable(bool writable);

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

    /**
     * @brief 会话刷新（restoreOnOpen 等会话事件后的面板同步——现取重投影
     *        ＋就绪条刷新；面板未创建＝空操作）。
     */
    void refreshFromSession();

    /**
     * @brief 执行一条本域命令的 UI 流程（UI-T41 A2——宿主注册表处理器的
     *        建模侧落点；路由唯一经 ModelingCommandFlows。反馈经面板状态
     *        行；草稿变更后就绪重算＋面板刷新）。
     *
     * @param commandId [in] 域命令 id（点分小写——§9.7.3 词表）
     * @return true＝流程执行完成（含域内拒绝的诚实应答）；false＝用户取消
     */
    bool executeDomainCommand(const std::string& commandId);

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

    // ---- 会话事件全同步（T03b 收口——§5.2/§5.4/§6.2 的模块半区）-------

    /**
     * @brief 会话脱离（项目关闭/切换后由宿主经 presentContext 调用——
     *        §8.6 表"模块表/局部栈清空"的模块状态半区：分支锚清空、草稿
     *        工作集复位为空、就绪报告作废＋面板复位空态）。
     *
     * 纪律：磁盘草稿零触碰（落盘归 DraftController 处置链——本方法只清
     * 会话内存态）；此后 buildDraftCommand 恒 nullopt（无锚无变更——
     * Stale 判据不可能被旧会话污染新会话，§6.2 纪元过滤的模块对位）。
     */
    void onSessionDetached();

    /**
     * @brief 修订提交事件（非 draft.apply 路径——撤销/重做/其他会话入口
     *        产生新修订时由宿主事件桥调用：编辑基线前移到新 tip＋就绪
     *        重算＋面板刷新；编辑记录不清零——与 noteAppliedRevision 的
     *        "应用即消费"语义区分，§8.5 重建基线的会话对位）。
     *
     * @param branch [in] 提交所在分支（非当前锚定分支的事件忽略——跨分支
     *                修订不触碰编辑基线，§6.2 纪元过滤的模块对位）
     * @param newTip [in] 新 tip 修订
     */
    void onRevisionCommitted(const core::BranchId& branch,
                             const core::RevisionId& newTip);

    // ---- 策略装载与 runtime 名称适配（T03b 收口——就绪 L 行程全量）----

    /**
     * @brief 绑定运行时名称映射（⑥端口真身——装配期由宿主注入；nullptr
     *        ＝未绑定，名称上下文如实 nullopt/全零。映射来源＝确定性编译
     *        产物，随 UI-T20 宿主运行时发布桥到位）。
     *
     * @param map [in] 映射（非 owning——调用方保证存活期覆盖模块）
     */
    void bindRuntimeNameMap(const runtime::RuntimeNameMap* map);

    /**
     * @brief 绑定策略提供器（已装载 EngineeringPolicySet 的现取入口——
     *        装配期由宿主注入；返回 nullptr＝策略未装载，就绪 L11 层如实
     *        Blocking（行程校验未执行的事实随行），不伪造可行）。
     *
     * @param provider [in] 提供器（UI 线程调用；空 function＝卸载绑定）
     */
    void bindPolicyProvider(
        std::function<const policy::EngineeringPolicySet*()> provider);

    // ---- 宿主迁移三接入面（WP-13-T20——B1-SPEC §5.1；只消费 UI-T21/T22
    //      冻结协议，共享 UI 装配面零改动——PIPE §6.2/§6.3 互斥红线）----

    /**
     * @brief 迁移三接入面句柄（树节点供给者＋属性页供给者——共享模型的
     *        注册原料；选择适配器经 attachSelectionService 独立接线）。
     *
     * 惰性构造（首次调用创建并缓存——shared_ptr 稳定地址，重复调用返回
     * 同一实例）。**调用时序**：须在 createPanel 之后调用——Deps 的编辑
     * 分流出口与高亮/激活执行器绑定面板指针；先于面板调用＝编辑出口缺位
     * （页面退化为纯呈现）与联动静默跳过的诚实降级形态（不崩溃，但功能
     * 缺位——装配层按序调用即可）。
     *
     * @return 句柄对（shared_ptr——宿主注册进 ui::ProjectTreeModel/
     *         ui::PropertyInspectorModel，模型持强引用；本模块同持缓存，
         任一持有序均保证存活期覆盖注册期）
     */
    struct SharedSurfaceHandles {
        std::shared_ptr<ui::IUiTreeNodesProvider> treeNodes;        ///< 树接入面
        std::shared_ptr<ui::IUiPropertyPagesProvider> propertyPages; ///< 页面接入面
    };
    SharedSurfaceHandles sharedSurfaceProviders();

    /**
     * @brief 订阅选择服务（SelectionAdapter 接线——下行"树选→面板高亮"
     *        的启用；服务存活期须覆盖订阅期且晚于本模块析构，适配器
     *        RAII 句柄语义见 HostMigrationProviders.hpp 类注）。
     */
    void attachSelectionService(ui::SelectionService& service);

    /// @brief 显式退订选择服务（幂等——未订阅时空操作）。
    void detachSelectionService();

    /**
     * @brief 上报一次本域三维拾取（上行——View3DPick 来源汇入选择服务
     *        唯一写入口；闭包外/无效身份拒绝上报返回 false，不出诊断——
     *        拾取未命中本域是常态）。
     */
    bool reportView3DPick(const core::ObjectId& oid);

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
     * 策略与名称来源（T03b 收口——全量行程）：CheckContext.resolvedPolicy
     * 取自策略提供器现取（未装载＝nullptr→L11 如实 Blocking）；名称上下文
     * 为 RuntimeMapPolicyNameContext（RuntimeNameMap 转发形——未绑定映射
     * 时如实 nullopt，绑定后 L 行名称解析全量可用）。
     */
    void recomputeReadiness();

private:
    PanelUiThreadGuard m_guard;              ///< §3.4 UI 线程守卫（构造线程绑定）
    ModuleSessionState m_session;            ///< 会话权威态（工作集唯一载体——装配层更新）
    ModelingPanelWidget* m_panel = nullptr;  ///< 面板引用（非 owning——归装配层）
    ui::IWorkbenchShell* m_shell = nullptr;  ///< 壳门面引用（非 owning——§10.9 生命周期）
    CommandSubmitFn m_pendingSubmit;         ///< 面板创建前的提交出口暂存（创建时应用）
    CommandAvailabilityFn m_pendingAvailability; ///< 面板创建前的可用性查询暂存
    std::function<QString(const std::string&)> m_textResolver;  ///< 文案解析（创建时应用）
    /// WC/DWC XML 导出回调（UI-T56——宿主 bindWorkCellExport 注入；空＝
    /// 命令执行报装配缺陷，fail-closed）。
    std::function<bool(const std::string& targetPath, std::string& summary)>
        m_workCellExport;
    /// 预览内存导出回调（UI-T59——宿主 bindWorkCellPreview 注入；空＝面板
    /// XML 类预览诚实呈现"未接线"，fail-closed 不自行拼 XML）。
    std::function<bool(const std::string& kind, std::string& headerLine,
                       std::string& sourceObject, std::string& text,
                       std::string& reason)>
        m_workCellPreview;

    /**
     * @brief 把会话已定格的已应用修订视图推送到面板（UI-T59 接线缺口修复
     *        ——UI-T41 A3 残留：m_session.appliedPreview 有存有清但从未
     *        推送，预览页在产品中恒停留空态）。
     *
     * 消费位点＝预览态的每个变迁点（seedTemplateSession/resetSession/
     * onRevisionCommitted 复位、noteAppliedRevision 定格、面板接入）——
     * 面板侧 refreshPreview 单一渲染出口自行分辨空态/摘要/XML 轨。
     */
    void pushAppliedPreviewToPanel();

    /**
     * @brief 面板接入后的预览供给接线（attachPanel/createPanel 共用——
     *        provider 转发〔回调已绑定时〕＋已定格预览推送）。
     */
    void attachPanelWiring();
    bool m_writable = true;                  ///< 会话可写性（L-7 初值——setWritable
                                             ///  更新；createPanel 作为面板初始态。
                                             ///  默认 true＝装配期无项目会话的可写
                                             ///  草稿态，宿主打开只读项目后经转发
                                             ///  面纠正——UI-T43 前该初值在面板上
                                             ///  不可纠正，是接线缺口）
    std::unique_ptr<policy::IJointLimitEvaluator> m_evaluator{
        policy::makeJointLimitEvaluator()};  ///< 行程评估器（policy 唯一实现——T03b-2b）
    std::unique_ptr<policy::IPolicyNameContext> m_nameContext;  ///< 名称上下文（T03b 起＝映射适配器——绑定切换经重建）
    std::function<const policy::EngineeringPolicySet*()> m_policyProvider;  ///< 策略提供器（现取——bindPolicyProvider 注入）
    /// 就绪快照互斥锁（domainReadiness 跨线程拉取与 UI 线程重算的并发保护
    /// ——§6.5 端口线程契约；mutable＝const 拉取路径加锁）
    mutable std::mutex m_readinessMutex;
    /// 跨线程就绪快照（recomputeReadiness 写/domainReadiness 读——值拷贝
    /// 短临界区；与 m_session.readiness 同源同写点）
    std::optional<ModelReadinessReport> m_readinessSnapshot;

    // ---- 宿主迁移三接入面（惰性构造缓存——shared_ptr 稳定地址）--------
    std::shared_ptr<ModelingTreeNodesProvider> m_treeProvider;      ///< 树接入面
    std::shared_ptr<ModelingPropertyPagesProvider> m_pageProvider;  ///< 页面接入面
    std::unique_ptr<ModelingSelectionAdapter> m_adapter;            ///< 选择适配器
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_MODELINGUIMODULE_HPP
