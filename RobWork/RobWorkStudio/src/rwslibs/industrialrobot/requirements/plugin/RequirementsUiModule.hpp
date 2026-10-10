/**
 * @file   RequirementsUiModule.hpp
 * @brief  需求插件界面模块——ui.md §11.2 IPluginUiModule 的 requirements 侧
 *         同形实现（装配缝）＋草稿源适配（P-REQ-4：ui IModuleDraftSource
 *         冻结面）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（IPluginUiModule 三方法：onShellReady／
 *     readonlyProjections／buildDraftCommand）、§10.9（装配期经
 *     IPluginUiRegistrar 注册——装配期一次、白名单校验）、§8.5
 *     （buildDraftCommand＝draft.apply 的域侧命令组装）、§10.5（
 *     IModuleDraftSource 四方法——UI-T12 实现冻结登记）、§8.2/§8.3-5
 *     （草稿文档组装与恢复承接）
 *   - units/requirements.md §9.8（L-R3"draft.apply→buildDraftCommand(
 *     "requirements")→①端口；就绪 Blocking 在 prepare 现场重估（ui 侧仅
 *     呈现拒绝诊断，不本地复判——P-REQ-6 边界）"、L-R12、命令表）、§9.1
 *     （命令族 token 与载荷编解码权威——CommandHandlers.hpp）
 *   - 需求 UX-05、PM-04；任务契约 tasks/foundation/WP-14-T08.json
 *     acceptance 1/2/4（P-REQ-4 处置：按 ui 现行 DraftController 契约消费）
 *
 * ★ P-REQ-8 消账（WP-14-T11 落位——O-45 装配门面补建的同批前置）：ui 的
 *   IPluginUiRegistrar/IPluginUiModule 头已随 WP-24-T03 落位（磁盘核对在
 *   ——modeling/kinematics 两域 UiModule 均已真实继承）。本类按上段预设
 *   路径切换为**真实继承** ui::IPluginUiModule：三方法（onShellReady/
 *   readonlyProjections/buildDraftCommand）签名与冻结接口逐一同形，仅加
 *   override 标注——语义零变化（同形期经门面适配的调用点改直呼接口面，
 *   行为一致），R-REQ-1 增量同步，登记于单元卡 §14.6。装配面（registrar
 *   调用点）经 assembly/RequirementsPluginAssembly（O-45 装配门面——
 *   WP-14-T11）交付宿主装配层，本单元不自行装配（PA-1 命令入口权威
 *   归 ui）。
 *
 * ★ P-REQ-4 处置（契约 note）：IModuleDraftSource 方法级签名已在 ui.md
 *   §10.5 随 UI-T12 实现冻结（IDraftController.hpp 落位——磁盘核对在），
 *   RequirementsDraftSource 直接继承 ui::IModuleDraftSource（真实 override，
 *   非同形暂持）；ui 卡冻结签名若变更即转 blocked 同步。
 *
 * 线程约束：仅 UI 线程访问（§3.4——模块持会话态与编辑器引用）。确定性：
 * buildDraftCommand 同会话态→同信封（编码确定性——Codec/CommandHandlers
 * 已钉）；面板不缓存权威数据（工作集唯一权威在编辑器——本模块零工作集
 * 副本，工作集读取经注入的编辑器现取）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSUIMODULE_HPP
#define IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSUIMODULE_HPP

#include <memory>
#include <optional>
#include <functional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>            // core::BranchId/RevisionId（信封身份面）
#include <sdurws/ird/project/CommandService.hpp>   // project::CommandEnvelope（§8.5 返回值面——登记边）
#include <sdurws/ird/requirements/Editor.hpp>      // IRequirementEditor（草稿唯一写目标——工作集权威）
#include <sdurws/ird/requirements/Readiness.hpp>   // RequirementReadinessReport（就绪投影数据源）
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // RequirementsView3DSeams（UI-T33——本单元门面消费面）
#include "RequirementsPanelWidget.hpp"             // 面板嵌套 sink 类型（UI-T33——StationMarkersSink/RegionPreviewSink 值面；同单元私有头，无循环——面板头不反向依赖本头）
#include <sdurws/ird/ui/IDraftController.hpp>      // ui::IModuleDraftSource（P-REQ-4 冻结面——UI-T12）
#include <sdurws/ird/ui/IPluginUiModule.hpp>       // ui::IPluginUiModule（P-REQ-8 消账——WP-24-T03 落位的冻结接口）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>       // ui::IWorkbenchShell（onShellReady 入参——壳门面）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::DomainReadinessItem（§6.5 汇聚值面）
#include <sdurws/ird/ui/ICommandRegistry.hpp>      // ui::CommandAvailability（按钮门控）
#include "HostMigrationProviders.hpp"              // 迁移三接入面（WP-14-T10——同目录私有头）
#include "PanelCommandCatalog.hpp"                 // requirementsReadinessProjection（§11.2 数据面——同目录私有头）
#include "PanelRefresh.hpp"                        // PanelUiThreadGuard（§3.4 线程守卫——零 Qt，同目录私有头）

namespace sdurws {
namespace ird {
namespace ui {
class SelectionService;  // 前置声明（attachSelectionService 入参——完整类型随 HostMigrationProviders 传递）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

namespace sdurws::ird::requirements {

class RequirementsPanelWidget;

// =====================================================================
// 模块会话态（buildDraftCommand/readonlyProjections 的输入面——装配层在
// 会话建立/草稿变更时更新；面板刷新同源）
// =====================================================================

/**
 * @brief 模块会话态（modeling ModuleSessionState 同构——requirements 侧
 *        权威落点）。
 *
 * 零缓存纪律（"面板不缓存权威数据"）的边界说明：本结构持有的是**会话
 * 权威事实本身**（分支/基线/根身份/最近就绪报告），不是工作集副本——
 * 工作集的唯一权威载体在编辑器（attachEditor 注入），本结构零 Requirement
 * WorkingSet 成员（结构性防第二真值）。所有权：值持有（就绪报告按值存）。
 */
struct RequirementsModuleSessionState {
    core::BranchId branch{};  ///< 目标分支（信封 branch——brn- 规范身份）
    /// 草稿基线修订（信封 expectedRevision——nullopt＝提交期解析 tip，§6.2）。
    std::optional<core::RevisionId> baseRevision;
    /// 根对象存储身份（修订闭包内 req-set 的 oid——装配层从闭包回填；
    /// nullopt＝首次应用（根取号，PA-1））。
    std::optional<core::ObjectId> rootObjectId;
    /// 最近一次就绪报告（readonlyProjections 源——呈现数据；判定权威在
    /// IRequirementReadinessChecker，本报告是 check 产出的直投值）。
    std::optional<RequirementReadinessReport> readiness;
    /// 恢复草稿待应用（adoptRestoredDocument 置位——恢复的工作集在编辑器
    /// 内，应用资格随此位；draft.apply 后由装配层清位）。
    bool restoredDraftPending = false;
};

// =====================================================================
// RequirementsUiModule——§11.2 三方法（同形面）＋会话接线
// =====================================================================

/**
 * @brief 需求插件界面模块（ui.md §11.2 三方法——P-REQ-8 消账后为
 *        IPluginUiModule 真实实现；会话接线与迁移三接入面同面承载）。
 *
 * 生命周期：装配期由插件装配点创建（UI 线程），存活至壳拆除（§10.9
 * "IPluginUiModule 存活至壳拆除"——装配层保证）。面板工厂经装配描述符
 * 提供（requirementsPanelRegistration——PanelCommandCatalog.hpp）。
 */
class RequirementsUiModule final : public ui::IPluginUiModule {
public:
    RequirementsUiModule() = default;
    /// 不可拷贝/不可移动（会话态与壳/编辑器引用绑定生命周期——§10.9）。
    RequirementsUiModule(const RequirementsUiModule&) = delete;
    RequirementsUiModule& operator=(const RequirementsUiModule&) = delete;

    // ---- 会话接线（装配层调用——面板/编辑器/会话态的权威落点）-----------

    /**
     * @brief 注册域面板工厂产物（装配期——面板 widget 由装配层创建后移交
     *        本模块弱语义引用〔非 owning——面板归 CentralAreaHost〕）。
     *
     * @param panel [in] 面板 widget（非 owning——调用方保证存活期覆盖模块）
     */
    void attachPanel(RequirementsPanelWidget* panel);
    using CommandSubmitFn = std::function<void(const ui::CommandId&)>;
    using CommandAvailabilityFn =
        std::function<ui::CommandAvailability(const ui::CommandId&)>;
    void bindCommandSubmit(CommandSubmitFn submitFn);
    void bindCommandAvailability(CommandAvailabilityFn availability);

    /// 编辑后动作钩子形态（UI-T29 最小校验——装配层接就绪重算编排；
    /// modeling 门面 setPostEditAction 同名方法语义同构）。
    using PostEditAction = std::function<void()>;

    /**
     * @brief 注入编辑后动作（每次 L-2 接受后触发——宿主动作重估＋bind-
     *        Readiness 先行，随后本模块以会话最新报告 refreshPanel——
     *        校验页实时化的组合子归模块，装配层零面板指针依赖）。
     */
    void setPostEditAction(PostEditAction action);

    /**
     * @brief 执行一条域命令的 UI 流程（UI-T32 C 批次——宿主 handler 经
     *        门面调用；转发面板 executeDomainCommand。命令路由唯一：宿主
     *        submitCommand→handler→本出口→面板 flows）。
     *
     * @param commandId [in] 命令 id（九条词表）
     * @return 流程完成与否（面板缺位/无会话＝false＋面板状态行不可达时静默）
     */
    bool executeDomainCommand(const std::string& commandId);

    /**
     * @brief 绑定三维视图缝（UI-T33——宿主网关装配后调用；capture-tcp/
     *        pick-feature 随之解除降级。lastPicked 缝在本模块内重写为
     *        自引用闭包——本模块即拾取登记面）。
     */
    void bindView3DSeams(RequirementsView3DSeams seams);

    /**
     * @brief 绑定工位标记出口（UI-T33 收口——宿主预览装配后调用；面板
     *        重载时全量投递非禁用工位标记。面板未创建＝暂存，创建后随
     *        attachPanel 转发〔与 bindCommandSubmit 同一时序语义〕）。
     */
    void bindStationMarkersSink(
        RequirementsPanelWidget::StationMarkersSink sink);

    /**
     * @brief 绑定区域预览出口（UI-T33 收口——同上暂存/转发双形态；面板
     *        区域页投影时投递预览几何〔refFrame 系——投影方变换〕）。
     */
    void bindRegionPreviewSink(RequirementsPanelWidget::RegionPreviewSink sink);

    /// 最近一次本域三维拾取（观测面——拾取命令输入；nullopt＝未拾取）。
    std::optional<core::ObjectId> lastView3DPick() const
    {
        return m_lastView3DPick;
    }

    /**
     * @brief 注入需求编辑器（草稿唯一写目标与工作集权威——装配层注入；
     *        本模块零工作集副本，一切读取现取）。
     *
     * @param editor [in] 需求编辑器（非 owning——调用方保证存活期覆盖模块）
     */
    void attachEditor(IRequirementEditor* editor) { m_editor = editor; }

    /// 会话态访问（装配层更新入口——权威事实载体；仅 UI 线程）。
    RequirementsModuleSessionState& session() noexcept
    {
        m_guard.assertOnUiThread();
        return m_session;
    }

    // ---- 会话事实接入面（UI-T39——审核 P1/P2 的模块侧转发半区）--------

    /**
     * @brief 会话可写性切换（L-R12 门控输入的宿主接线——ui 只读横幅同源
     *        事实经本面直达面板；转发面零判定。项目打开成功/降级只读/
     *        切换由宿主按打开报告驱动）。
     *
     * 面板缺位（Dock 尚未首次创建）＝暂存本模块（与 bindCommandSubmit
     * 暂存同一时序语义）——createPanel/attachPanel 时以缓存值生效；此前
     * 直接丢弃该态＝"只读项目先开、需求 Dock 后开"装配序下面板恒可写
     * （L-R12 违约——只读初始化缺陷的修复面）。
     *
     * @param writable [in] 当前项目会话是否可写
     */
    void setWritable(bool writable);

    /**
     * @brief 面板初始可写性（装配工厂创建面板时的构造参数来源——取本模块
     *        缓存的最近 setWritable 值；缺省 true＝可写项目先行的既有
     *        语义。与 attachPanel 的缓存回放互为双保险：无论工厂取值与
     *        setWritable 时序如何，面板创建后必被拉齐到模块缓存态）。
     */
    bool initialWritable() const noexcept { return m_writable; }

    /**
     * @brief 会话刷新（就绪重估已由宿主完成 bindReadiness 后的呈现收口
     *        ——以会话最新报告驱动面板全面板刷新。打开成功首刷/外部修订
     *        重导线后/项目级撤销提交后共用；面板缺位＝空操作）。
     */
    void refreshFromSession();

    /**
     * @brief 基线重载登记（编辑器 loadBaseline 重建会话后由宿主调用——
     *        草稿撤销记账随编辑器局部栈复位归零，撤销键随新记账刷新；
     *        漏登记＝"幽灵可撤销"——记账指向已不存在的栈条目）。
     */
    void noteBaselineReloaded();

    /**
     * @brief 应用回执（draft.apply 提交 Committed 的域侧对账——modeling
     *        门面 noteAppliedRevision 同名方法语义同构，两件事）：
     *
     *   ①会话态回填：基线前移（下一轮信封 expectedRevision＝新 tip）＋根
     *     对象身份回填（下一轮根槽走"既有对象替换"——allocateNew 不再重
     *     复建根，恰一根不变量）＋恢复草稿资格位清位（应用即消费）；
     *   ②工作集根引用表回填（F-460）：把本次修订落库的四集合存储身份回
     *     填进编辑器工作集根引用表（rewireWorksetRootRefsAfterApply）。
     *
     *   为什么②必须存在：buildDraftCommand 的集合槽挂载态取工作集根引用
     *   表（§4.2——refs 是集合 oid 的权威），而首应用前的合法工作集是
     *   "集合有条目、refs 全空"（挂载增量只存在于命令候选根，落库后回
     *   流仅达会话态）。②缺席时第二次 draft.apply 仍按"未挂载"组装
     *   allocateNew 槽，与存储端已挂载事实失配——被 prepare 挂载核对拒
     *   绝（F-460 阻断：需求域二连 draft.apply 必拒）。
     *
     * @param newBase      [in] 新基线修订（tip）
     * @param rootObjectId [in] 根对象存储身份（nullopt＝未回填〔首应用前〕）
     *
     * @throws std::runtime_error 工作集回填的基线重建失败（实现/数据缺陷
     *               ——回填闭包取自编辑器当前工作集自身，提交时刻已通过
     *               编码与校验链；fail-fast 不吞，与会话态回填的静默语义
     *               有意区分——②失败意味着会话与存储失配，宁可崩溃暴露）
     *
     * 仅 UI 线程（§3.4——会话态与编辑器工作集均为 UI 线程权威态）。
     */
    void noteAppliedRevision(const core::RevisionId& newBase,
                             const std::optional<core::ObjectId>& rootObjectId);

    /**
     * @brief 会话脱离的面板复位（项目关闭/切换的呈现收口——转发面板
     *        resetForSessionDetached：会话选中锚/撤销记账归零＋控件投影
     *        收拢为无会话空态。面板缺位＝空操作）。
     */
    void resetPanelForDetach();

    // ---- ui.md §11.2 三方法（逐方法对齐卡文——P-REQ-8 同形面）----------

    /**
     * @brief 壳就绪回调（§11.2 行"注册回调后初始化〔订阅/面板内容〕"）。
     *
     * requirements 侧初始化面：持有壳引用（只读消费）；域命令的注册经装
     * 配描述符（requirementsPanelRegistration＋requirementsDomainCommands）
     * 在装配期完成，本回调零重复注册（§10.9"装配期一次"）。
     *
     * @param shell [in] 工作台壳门面（非 owning——存活期由壳侧保证）
     */
    void onShellReady(ui::IWorkbenchShell& shell) override { m_shell = &shell; }

    /**
     * @brief 只读就绪投影（§11.2 行"§6.5 汇聚源"——StageStatusModel 汇聚
     *        的 requirements 行；值语义直投 requirementsReadinessProjection）。
     *
     * @return 单元素投影（会话就绪报告未载＝输入不完整空态行——不伪造
     *         可行性；判定权威在 IRequirementReadinessChecker；P-REQ-6：
     *         本投影是数据事实，门控动作归 workflow/ui）
     */
    std::vector<ui::DomainReadinessItem> readonlyProjections() const override;

    /**
     * @brief 草稿应用命令组装（§11.2 行"§8.5 应用时命令组装〔域侧〕"——
     *        L-R3 draft.apply 流的 requirements 半区；acceptance 2）。
     *
     * 组装规则（§9.1/§8.5——真实信封，非占位；零就绪判定——P-REQ-6 边界：
     * Blocking 的现场重估唯一在命令 prepare，本组装不调用校验器）：
     *   - moduleId≠"requirements"→nullopt（域外请求——调用方错误面）；
     *   - 编辑器未注入/草稿无变更（edits==0 且无恢复草稿待应用）→
     *     nullopt（无可应用内容——§8.5"无草稿可应用"态；不产生空修订）；
     *   - 否则→CommandEnvelope{branch=会话分支、expectedRevision=会话基线
     *     （nullopt＝tip——§6.2）、commandType=apply-requirement-set、
     *     payloadFormatVersion=kRequirementCommandPayloadVersion、payload=
     *     五对象槽〔根槽 allocateNew＝根身份未回填（首应用——prepare 取号
     *     回填，PA-1）；集合槽 allocateNew＝根引用表该槽未挂载；字节＝
     *     RequirementCodec 确定性编码〕}——就绪 Blocking 拒绝（若有）在
     *     prepare 现场重估后经命令回执呈现（ui 侧仅呈现拒绝诊断）。
     *
     * @param moduleId [in] 域注册键（"requirements"——§11.2 原文参数形态）
     * @return 信封（有草稿变更时）；nullopt＝域外请求／无可应用变更
     *
     * @throws std::runtime_error 草稿编码失败（模型 schema 违约——不产出
     *               畸形信封，fail-fast）
     *
     * 仅 UI 线程（§3.4——读取会话权威态与编辑器工作集）。
     */
    std::optional<project::CommandEnvelope> buildDraftCommand(
        const std::string& moduleId) override;

    /// 编辑器访问（草稿源/面板流程共用——非 owning；未注入＝nullopt）。
    IRequirementEditor* editor() const noexcept { return m_editor; }

    // ---- 宿主迁移三接入面（WP-14-T10——B1-SPEC §5.1；只消费 UI-T21/T22
    //      冻结协议，共享 UI 装配面零改动——PIPE §6.2/§6.3 互斥红线）----

    /**
     * @brief 迁移三接入面句柄（树节点供给者＋属性页供给者——共享模型的
     *        注册原料；选择适配器经 attachSelectionService 独立接线）。
     *
     * 惰性构造（首次调用创建并缓存——shared_ptr 稳定地址，重复调用返回
     * 同一实例）。**调用时序**：须在 attachPanel 之后调用——Deps 的编辑
     * 分流出口与高亮/激活执行器绑定面板指针；先于面板调用＝编辑出口缺位
     * （页面退化为纯呈现）与联动静默跳过的诚实降级形态（不崩溃，但功能
     * 缺位——装配层按序调用即可）。
     *
     * @return 句柄对（shared_ptr——宿主注册进 ui::ProjectTreeModel/
     *         ui::PropertyInspectorModel，模型持强引用；本模块同持缓存，
     *         任一持有序均保证存活期覆盖注册期）
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
     *
     * @param oid [in] 拾取命中的对象身份
     * @return true＝已上报（服务已广播）；false＝未接线/闭包外/无效身份
     */
    bool reportView3DPick(const core::ObjectId& oid);

private:
    PanelUiThreadGuard m_guard;  ///< §3.4 UI 线程守卫（构造线程绑定）
    RequirementsModuleSessionState m_session;  ///< 会话权威态（装配层更新）
    RequirementsPanelWidget* m_panel = nullptr;  ///< 面板引用（非 owning——归装配层）
    bool m_writable = true;  ///< 会话可写性缓存（L-R12 门控输入——面板缺位时暂存，创建/挂接时生效）
    CommandSubmitFn m_pendingSubmit;             ///< 面板创建前暂存的宿主出口
    CommandAvailabilityFn m_pendingAvailability;  ///< 面板创建前暂存的可用性查询
    RequirementsPanelWidget::StationMarkersSink m_pendingMarkersSink;  ///< 面板创建前暂存的工位标记出口（UI-T33）
    RequirementsPanelWidget::RegionPreviewSink m_pendingRegionSink;    ///< 面板创建前暂存的区域预览出口（UI-T33）
    PostEditAction m_hostPostEdit;  ///< 宿主编辑后动作（UI-T29——重估＋bindReadiness；组合子承载）
    /// 三维视图缝（UI-T33——bindView3DSeams 注入；未绑定＝命令流降级原文）。
    RequirementsView3DSeams m_view3dSeams;
    bool m_view3dSeamsBound = false;             ///< 缝绑定旗标（透传判定面）
    std::optional<core::ObjectId> m_lastView3DPick;  ///< 最近本域拾取（网关分发登记——零修订会话态）

    /// 编辑后组合子挂面板（UI-T29——宿主重估先行＋最新报告 refreshPanel；
    /// 面板缺位＝仅暂存，attachPanel 时补挂）。
    void wirePanelPostEdit();

    /**
     * @brief 应用回执的工作集根引用表回填（F-460——noteAppliedRevision
     *        第二半；私有实现细节见 .cpp 同名函数注释）。
     *
     * 数据来源＝宿主绑定的会话闭包引用元数据缝（m_view3dSeams.
     * sessionClosureRefs——生产绑定＝查询端口 head().objectRefs，F-558
     * 延迟现取）；缝缺位/空闭包＝诚实跳过（不虚构、不剥蚀既有挂载——
     * 与缝未绑定时二连应用维持修复前行为的降级语义一致）。
     */
    void rewireWorksetRootRefsAfterApply();

    IRequirementEditor* m_editor = nullptr;      ///< 编辑器引用（非 owning——工作集权威）
    ui::IWorkbenchShell* m_shell = nullptr;      ///< 壳门面引用（非 owning——§10.9 生命周期）

    // 迁移三接入面缓存（惰性构造——shared_ptr 稳定地址；适配器 unique_ptr
    // 随模块生命周期）。
    std::shared_ptr<ui::IUiTreeNodesProvider> m_treeProvider;        ///< 树接入面缓存
    std::shared_ptr<ui::IUiPropertyPagesProvider> m_pageProvider;    ///< 页面接入面缓存
    std::unique_ptr<RequirementsSelectionAdapter> m_adapter;         ///< 选择适配器缓存
};

// =====================================================================
// RequirementsDraftSource——ui IModuleDraftSource 冻结面实现（P-REQ-4）
// =====================================================================

/**
 * @brief 需求域草稿源（ui.md §10.5 IModuleDraftSource 的 requirements 实现
 *        ——随装配经 IDraftController::attachModule 挂接；模块 id 词表＝
 *        "requirements"（ModuleDraftHandle 注册表键同词表））。
 *
 * 草稿文档载荷语义（域侧自治——ui 只搬运不解析，CR-02/D-10）：
 *   payload＝RequirementCommandPayload{Apply, 五对象槽}的 canonical 字节
 *   （与 draft.apply 命令载荷同构同版本——一套编码两处消费，NFR-MNT-03；
 *   schemaVersion=kRequirementCommandPayloadVersion）。恢复＝解码五对象→
 *   以内存闭包视图重建编辑器基线（工作集即恢复的草稿内容）→置
 *   restoredDraftPending（应用资格位）。
 *
 * 线程约束：四方法均仅 UI 线程被控制器调用（§10.5 契约表）。
 */
class RequirementsDraftSource final : public ui::IModuleDraftSource {
public:
    /**
     * @brief 构造（装配期——模块与编辑器注入）。
     *
     * @param module [in] 需求模块（非 owning——会话态权威面）
     * @param editor [in] 需求编辑器（非 owning——草稿唯一写目标）
     */
    RequirementsDraftSource(RequirementsUiModule& module, IRequirementEditor& editor)
        : m_module(module), m_editor(editor)
    {}

    /**
     * @brief 模块显示名（UX-02 工程用语——汇总视图/横幅/冲突对话框呈现值源）。
     * @return "任务需求"（固定词——零哈希/内部名）
     */
    std::string displayName() const override;

    /**
     * @brief 组装当前草稿文档（§8.2——定时/手动落盘时控制器取文档）。
     *
     * 编辑器未载入基线→抛 std::logic_error（控制器不会对未载入模块落盘
     * ——到达即装配违约 fail-fast，不产出空文档）。
     *
     * @return 草稿文档投影（moduleId="requirements"；payload＝五对象载荷
     *         canonical 字节；baseRevisionId 取编辑器草稿态〔未标注＝空
     *         保留值〕；savedAtUtc 留空——控制器在分派时刻填写）
     *
     * @throws std::logic_error 编辑器未载入基线（装配违约）
     * @throws std::runtime_error 草稿编码失败（模型非法——不产出畸形文档）
     */
    ui::DraftDocumentProjection buildDraftDocument() const override;

    /**
     * @brief 承接恢复的草稿文档（§8.3-5——打开期 restoreOnOpen 把磁盘内容
     *        交回域侧；Loaded/RecoveredFromBackup 对域侧无区别）。
     *
     * @param document [in] 恢复的文档值（payload 语义见类注）
     *
     * @throws std::runtime_error 载荷破损/版本不受理/对象解码失败/根引用
     *               表不完整（草稿文件级数据缺陷——fail-fast 不吞：损坏
     *               恢复横幅面由控制器按 RestoreOutcome 呈现，域侧不虚构
     *               可用工作集）
     */
    void adoptRestoredDocument(const ui::DraftDocumentProjection& document) override;

    /**
     * @brief 以指定修订重建编辑基线（§8.5"基于当前版本重新编辑"的域侧
     *        半区——控制器同时重置会话脏标记）。
     *
     * requirements 侧语义：更新会话基线锚（tip 规范文本→RevisionId；不
     * 可解析＝保持 nullopt——提交期解析 tip）；工作集重建本身由装配层的
     * 基线闭包提供器在下一轮刷新协调器重演中完成（PA-1：闭包权威在装配
     * 层——本方法不代取）。
     *
     * @param tipRevisionCanonical [in] 分支当前 tip（rev- canonical 文本）
     */
    void rebuildOnRevision(const std::string& tipRevisionCanonical) override;

private:
    RequirementsUiModule& m_module;  ///< 需求模块（非 owning——会话态权威面）
    IRequirementEditor& m_editor;    ///< 需求编辑器（非 owning——草稿唯一写目标）
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_REQUIREMENTSUIMODULE_HPP
