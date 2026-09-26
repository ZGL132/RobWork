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
 * ★ P-REQ-8 处置（modeling P-MDL-8 同款先例——ModelingUiModule 对齐）：
 *   ui 的 IPluginUiRegistrar/IPluginUiModule 头尚未落位（ui.md §3.3 已
 *   登记落点、磁盘核对不存在——装配任务后续兑现）。本类按 §11.2 冻结文本
 *   **逐方法同形**实现（方法名/签名/语义对齐卡文；值面全部用已落位类型
 *   ——ui::DomainReadinessItem/project::CommandEnvelope），ui 侧头落位后
 *   本类改为继承 ui::IPluginUiModule（override 不变，语义零变化）——
 *   R-REQ-1 增量同步，登记于单元卡 §14.6。装配面（registrar 调用点）随
 *   L5 装配任务（WP-24-T03）接线，本单元不自行装配（PA-1 命令入口权威
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
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>            // core::BranchId/RevisionId（信封身份面）
#include <sdurws/ird/project/CommandService.hpp>   // project::CommandEnvelope（§8.5 返回值面——登记边）
#include <sdurws/ird/requirements/Editor.hpp>      // IRequirementEditor（草稿唯一写目标——工作集权威）
#include <sdurws/ird/requirements/Readiness.hpp>   // RequirementReadinessReport（就绪投影数据源）
#include <sdurws/ird/ui/IDraftController.hpp>      // ui::IModuleDraftSource（P-REQ-4 冻结面——UI-T12）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>       // ui::IWorkbenchShell（onShellReady 入参——壳门面）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::DomainReadinessItem（§6.5 汇聚值面）
#include "PanelCommandCatalog.hpp"                 // requirementsReadinessProjection（§11.2 数据面——同目录私有头）
#include "PanelRefresh.hpp"                        // PanelUiThreadGuard（§3.4 线程守卫——零 Qt，同目录私有头）

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
 * @brief 需求插件界面模块（ui.md §11.2 同形实现——三方法逐一对齐卡文）。
 *
 * 生命周期：装配期由插件装配点创建（UI 线程），存活至壳拆除（§10.9
 * "IPluginUiModule 存活至壳拆除"——装配层保证）。面板工厂经装配描述符
 * 提供（requirementsPanelRegistration——PanelCommandCatalog.hpp）。
 */
class RequirementsUiModule final {
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
    void attachPanel(RequirementsPanelWidget* panel) { m_panel = panel; }

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
    void onShellReady(ui::IWorkbenchShell& shell) { m_shell = &shell; }

    /**
     * @brief 只读就绪投影（§11.2 行"§6.5 汇聚源"——StageStatusModel 汇聚
     *        的 requirements 行；值语义直投 requirementsReadinessProjection）。
     *
     * @return 单元素投影（会话就绪报告未载＝输入不完整空态行——不伪造
     *         可行性；判定权威在 IRequirementReadinessChecker；P-REQ-6：
     *         本投影是数据事实，门控动作归 workflow/ui）
     */
    std::vector<ui::DomainReadinessItem> readonlyProjections() const;

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
    std::optional<project::CommandEnvelope> buildDraftCommand(const std::string& moduleId);

    /// 编辑器访问（草稿源/面板流程共用——非 owning；未注入＝nullopt）。
    IRequirementEditor* editor() const noexcept { return m_editor; }

private:
    PanelUiThreadGuard m_guard;  ///< §3.4 UI 线程守卫（构造线程绑定）
    RequirementsModuleSessionState m_session;  ///< 会话权威态（装配层更新）
    RequirementsPanelWidget* m_panel = nullptr;  ///< 面板引用（非 owning——归装配层）
    IRequirementEditor* m_editor = nullptr;      ///< 编辑器引用（非 owning——工作集权威）
    ui::IWorkbenchShell* m_shell = nullptr;      ///< 壳门面引用（非 owning——§10.9 生命周期）
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
