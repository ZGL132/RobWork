/**
 * @file   ModelingPluginAssembly.hpp
 * @brief  建模插件装配门面——首版装配（WP-24-T03）中宿主装配层消费建模
 *         插件的**唯一公共入口**。
 *
 * 设计依据：
 *   - units/ui.md §10.9/§11.1（registerPluginUi 入参形状；白名单 token；
 *     面板工厂语义）、§11.2（IPluginUiModule 三方法）；
 *   - units/modeling.md §9.7.3（命令目录/装配描述符——PanelCommandCatalog
 *     的建模侧登记数据经本门面直通装配层）、§14.6 R-MDL-1（P-MDL-8 消账
 *     路径：ui 侧头落位→继承切换→装配接线）；
 *   - O-31 裁决（装配器同时看见两边写单行适配器）——本头即建模侧给装配
 *     器的"单行适配器"面：装配层只见本头与 ui 公共头，零建模私有头依赖
 *     （R-2；plugin/ 私有实现细节全部封装在本门面之后）。
 *
 * 落位形态说明：本头位于 modeling 单元的 `assembly/` 目录（plugin 目标的
 * PUBLIC include 面——插件界面目标的装配契约头，非产品 include/ 扫描域；
 * 与 plugin/ 同理在零 Qt 红线的文件域之外）。实现（plugin/
 * ModelingPluginAssembly.cpp）编入 `sdurws_ird_modeling_plugin` 目标。
 *
 * 线程模型：本门面全部函数 UI 线程调用（模块/面板同约束——§3.4）。
 */
#ifndef IRD_MODELING_ASSEMBLY_MODELINGPLUGINASSEMBLY_HPP
#define IRD_MODELING_ASSEMBLY_MODELINGPLUGINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QString>                                 // 文案解析返回值（bindTextResolver）
#include <sdurws/ird/core/Identity.hpp>            // core::BranchId/RevisionId/ObjectId（会话锚/回执值面）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>    // ui::PluginUiDescriptor（§10.9 装配描述符）
#include <sdurws/ird/ui/UiProjections.hpp> // PM-04完整固定修订草稿投影，沿既有源端口值面。
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::CommandId（命令提交出口入参）
#include <sdurws/ird/ui/ICommandRegistry.hpp>     // ui::CommandAvailability（按钮门控）

namespace sdurws {
namespace ird {
namespace runtime {
class RuntimeNameMap;  // 前向声明（bindRuntimeNameMap 入参——⑥端口映射真身）
}  // namespace runtime
namespace ui {
class IModuleDraftSource;       // 前向声明（§10.5 模块草稿源——draftSource 返回类型）
class IUiDomainReadinessSource; // 前向声明（§6.5 汇聚源端口——readinessSource 返回类型）
class IUiTreeNodesProvider;     // 前向声明（WP-13-T20 树接入面——注册原料类型）
class IUiPropertyPagesProvider; // 前向声明（WP-13-T20 页面接入面——注册原料类型）
class SelectionService;         // 前向声明（WP-13-T20 选择服务——适配器接线入参）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

namespace sdurws {
namespace ird {
namespace policy {
class EngineeringPolicySet;  // 前向声明（bindPolicyProvider 提供器返回的指针面）
}  // namespace policy
}  // namespace ird
}  // namespace sdurws

namespace sdurws {
namespace ird {
namespace modeling {

class ModelingUiModule;  // 前置声明（内部具体类型——消费方面只见 IPluginUiModule 接口）
class ObjectClosureView; // A-14：既有公共闭包查询端口，同步恢复时借用。

/**
 * @brief MDL-17/AT-04：恢复已应用机器人会话姿态的纯值请求，不属于项目命令。
 *
 * 值由UI模块拥有；回调同步借用、不保留引用，仅UI线程使用。jointIds与
 * jointConfiguration按根对象关节序一一对应，含Fixed占位。q为RobWork增量
 * 坐标（旋转rad、移动m）：模块用已应用baseline的原始zeroOffset从命名条目
 * 权威q换算，未应用的零偏不参与；宿主仍须对账当前快照/模型身份与限位。
 */
struct ModelingSessionPoseRequest {
    core::BranchId branch;                 ///< 当前已应用分支；无效身份不得提交。
    core::ObjectId rootObjectId;           ///< 当前已应用机器人根身份。
    std::vector<core::ObjectId> jointIds;   ///< 全部关节身份，按基座到末端排列。
    std::vector<double> jointConfiguration;///< 逐关节q_rw（rad/m）；有限，Fixed槽不产生运动。
};

/// 模块句柄（ModuleDraftHandle 词表——与 descriptor.pluginId 同 token；
/// 宿主装配层 attachModule 首参取此值）。
inline constexpr char kModuleHandle[] = "modeling";

/**
 * @brief 建模插件装配产物（createModelingPluginAssembly 的返回值）。
 *
 * 消费序（宿主装配层的标准用法）：
 *   ①`bindTextResolver`（接 ui::resolveText——命令按钮呈现工程用语）；
 *   ②`bindCommandSubmit`（接宿主提交面——首版为状态行回显，收口接
 *     CommandRegistry）；
 *   ③`seedTemplateSession`（首版会话种子——generic-6r 真实草稿）；
 *   ④`registrar.registerPluginUi(descriptor, *module)`；
 *   ⑤`descriptor.panels.front().factory()` 取面板挂位。
 *
 * 所有权：module 由本结构 unique_ptr 持有；内部实现指针非 owning（指向
 * module 的具体对象——随 module 生存，消费方不解引用）。可移动（装配层
 * 转移持有）；不可拷贝。
 */
struct ModelingPluginAssembly {
    std::unique_ptr<ui::IPluginUiModule> module;  ///< 模块（接口面——三方法契约）
    ui::PluginUiDescriptor descriptor;            ///< 装配描述符（§10.9 形状直通）

    /// 命令提交出口绑定（转发模块内部——面板创建前后皆可）。
    void bindCommandSubmit(std::function<void(const ui::CommandId&)> submit);
    void bindCommandAvailability(std::function<ui::CommandAvailability(const ui::CommandId&)> availability);

    /// 文案解析绑定（titleKey→工程用语；转发模块内部）。
    void bindTextResolver(std::function<QString(const std::string& titleKey)> resolve);

    /**
     * @brief 绑定真实宿主会话姿态写入出口（MDL-17），不触项目历史。
     * @param writer [in] UI线程同步回调；空＝未装配，恢复诚实拒绝。模块持有
     *        闭包，宿主保证目标存活至模块拆除；不得缓存请求/帧的借用引用。
     * @return 无返回；writer返回true表示已一次更新宿主State，false必须给出
     *         可定位原因且旧State不变。只读项目允许会话更新，禁止提交项目命令。
     */
    void bindSessionPoseWriter(std::function<bool(const ModelingSessionPoseRequest&,
                                                 std::string&)> writer);

    /// WC/DWC XML 外供导出回调绑定（UI-T56——转发模块内部；m_impl 缺位＝
    /// 静默零操作。UI-T59 补登：本转发器在 UI-T56 落位时漏登装配面——
    /// sdurws_ird_ui_plugin 自该批起断链〔F-518〕，随 UI-T59 修复）。
    void bindWorkCellExport(
        std::function<bool(const std::string& targetPath, std::string& summary)> exportFn);

    /// 预览内存导出回调绑定（UI-T59——F-498 预览半区；转发模块内部；
    /// m_impl 缺位＝静默零操作）。
    void bindWorkCellPreview(
        std::function<bool(const std::string& kind, std::string& headerLine,
                           std::string& sourceObject, std::string& text,
                           std::string& reason)> previewFn);

    /**
     * @brief 会话可写性切换（UI-T43——L-7 门控输入的宿主接线面；转发模块
     *        内部，需求域 RequirementsPluginAssembly::setWritable 同构）。
     *
     * 门控事实源＝ui 只读横幅同源的宿主打开报告；面板创建前后皆可调用
     * （创建前＝模块暂存初值，createPanel 应用——装配序无关）。宿主在
     * 项目打开/切换/只读降级（openViaSessionController 统一接线点）驱动。
     *
     * @param writable [in] 会话可写性（true＝可写；false＝只读/降级只读）
     */
    void setWritable(bool writable);

    /// 首版会话种子（generic-6r 真实草稿；转发模块内部）。
    void seedTemplateSession();

    /// 会话锚绑定（打开成功后——分支＋tip；仅UI线程）。已有脏草稿同分支
    /// 保留旧锚接受project的Stale裁决；跨分支未处置抛std::runtime_error且原值不变。
    /// 首次/干净绑定沿原路径推进，绑定本身不代表基线重放或用户确认。
    void bindSessionAnchor(const sdurws::ird::core::BranchId& branch,
                           const sdurws::ird::core::RevisionId& base);

    /**
     * @brief A-14／REQ-10：从已接受项目修订恢复完整可编辑建模基线。
     * @param closure [in] 同一修订只读闭包；调用方持有，仅本次UI线程同步借用。
     * @param rootObjectId [in] 修订中唯一robot-design的有效身份，不重新分配。
     * @param branch [in] 已接受上下文的有效活动分支，不得传候选预打开分支。
     * @param revision [in] 对应闭包的有效修订；恢复不写项目或增加修订。
     * @param errorText [out] 成功清空；缺对象、坏版本/字节、身份不符或草稿冲突时定位原因。
     * @return true＝完整基线已采纳，或同根/分支/修订幂等保持；false＝失败且
     *         原会话不变（模块缺席亦false）。所有SI值及来源保留，changes为空；
     *         同一会话重复投影保留未应用编辑，不覆盖草稿。只读项目也可恢复浏览。
     * @throws std::invalid_argument 身份无效，属调用方违约；其它数据/读取错误走false。
     * 仅UI线程；模块拥有恢复值和基线，调用后不保留closure引用，无新共享状态。
     */
    bool restoreCommittedSession(const ObjectClosureView& closure,
        const core::ObjectId& rootObjectId, const core::BranchId& branch,
        const core::RevisionId& revision, std::string& errorText);

    /**
     * @brief PM-04/AT-21：成功提交后从该不可变修订完整采纳正式身份和部件值。
     * @param closure [in] 本次CommandResult.newRevision的只读闭包，同步借用。
     * @param rootObjectId [in] 该修订唯一根的有效持久ID。
     * @param branch [in] 已提交命令的有效分支；不得用其他会话冒充回执。
     * @param revision [in] 成功结果的有效修订；无量纲身份，不读取可变HEAD。
     * @param errorText [out] 成功清空，失败给出定位信息。
     * @return true时完整工作集替换并建立干净基线；false时草稿、基线和锚均不变。
     * 与普通重开不同，此入口允许消费本次已提交的脏草稿；不得在拒绝/取消时调用。
     * @throws std::invalid_argument 任一身份无效，属装配契约违约。
     * 仅UI线程；模块拥有解码值，不保留closure引用，所有物理量沿Codec的SI单位。
     */
    bool acceptAppliedSession(const ObjectClosureView& closure,
        const core::ObjectId& rootObjectId, const core::BranchId& branch,
        const core::RevisionId& revision, std::string& errorText);

    /**
     * @brief PM-04：绑定“基于当前版本重新编辑”的完整固定修订读取器。
     * @param reader [in] UI线程同步回调；参数为有效分支/指定修订，返回该修订
     *        无编辑摘要的完整域草稿（module/base须精确匹配、根身份有效）。
     *        nullopt仅代表该修订无建模根；读错/歧义/坏数据须抛std::runtime_error，
     *        不得伪装无根。空回调表示未装配，重新编辑拒绝且保留原状态。
     * @return 无返回。模块持有闭包，宿主保证借用目标存活或校验会话已关闭；
     *         不允许回调写项目或修改本模块，不自动提交/重试。SI和身份沿唯一Codec。
     */
    void bindRevisionDraftReader(std::function<std::optional<ui::DraftDocumentProjection>(
        const core::BranchId&, const core::RevisionId&)> reader);

    /**
     * @brief 读取成功重建前保留的旧完整草稿，供用户核对和手动迁移。
     * @return 含原base及完整根/部件/摘要的值副本；尚未重建或会话脱离为空。
     * 仅UI线程；不写项目/草稿磁盘、不把旧内容套上新锚，也不清除保留值。
     */
    std::optional<ui::DraftDocumentProjection> retainedConflictDraft() const;

    /**
     * @brief PM-04/PM-11：订阅完整草稿帧变化，供宿主通知控制器脏代次。
     * @param callback [in] 默认空；仅UI线程同步调用，不能抛错或递归修改草稿。
     * 绑定不回放历史事件，首次绑定以当前帧建观察点；有效会话内字段、结构、
     * 部件和局部undo/redo均通知，同内容刷新不通知。脱会话自动解绑，宿主
     * 在挂接源成功后绑定；回调和值观察点归模块持有，无项目写入或线程转移。
     */
    void bindDraftChanged(std::function<void()> callback);


    /// 应用回执回写（submit Committed 后——基线前移＋根身份回填；T03b-2）。
    void noteAppliedRevision(const sdurws::ird::core::RevisionId& newBase,
                             const std::optional<sdurws::ird::core::ObjectId>& rootObjectId);

    /// 会话刷新（restoreOnOpen 后由宿主调用——面板同步；T03b-2）。
    void refreshFromSession();

    /// 执行一条本域命令的 UI 流程（UI-T41 A2——宿主注册表处理器落点；
    /// 转发模块。false＝用户取消；域内拒绝＝true＋面板状态行原因）。
    bool executeDomainCommand(const std::string& commandId);

    // ---- T03b 收口装配面（策略/名称/会话同步/汇聚源）------------------

    /// 绑定运行时名称映射（⑥端口——nullptr＝未绑定如实空值轨；见模块
    /// 同名方法）。绑定即触发就绪重算。
    void bindRuntimeNameMap(const sdurws::ird::runtime::RuntimeNameMap* map);

    /// 草稿对象名解析（UI-T74/F-546 出路①——草稿名感知名称上下文的宿主
    /// 装配缝）：对象身份→当前草稿工作集内的名称（joints/links/tools/
    /// scene 逐表行取 localName；未命中＝false——闭包外身份不猜测，
    /// ARC-04）。转发模块同名方法；仅读投影、零修订。
    bool tryDraftObjectName(const sdurws::ird::core::ObjectId& object,
                            std::string& name) const;

    /// 绑定策略提供器（已装载 EngineeringPolicySet 现取入口；nullptr 返回
    /// 值＝未装载→L11 如实 Blocking。绑定即触发就绪重算）。
    void bindPolicyProvider(
        std::function<const policy::EngineeringPolicySet*()> provider);

    /// 会话脱离（项目关闭/切换——锚清空＋工作集复位＋面板空态；磁盘草稿
    /// 零触碰。宿主 presentContext 无项目态时调用）。
    void onSessionDetached();

    /// 修订提交事件（撤销/重做等非 apply 路径——非锚定分支的事件忽略；
    /// 基线前移到新 tip＋就绪重算；编辑记录不清零，与 noteAppliedRevision
    /// 语义区分）。
    void onRevisionCommitted(const sdurws::ird::core::BranchId& branch,
                             const sdurws::ird::core::RevisionId& newTip);

    /// 域就绪汇聚源视图（§6.5 IUiDomainReadinessSource——宿主装配注册进
    /// StageNavigationModelDeps.domainSources；存活期随 module）。
    ui::IUiDomainReadinessSource& readinessSource() const;

    /// 模块草稿源视图（§10.5 attachModule 第二参数——本对象实现四方法；
    /// 存活期随 module）。const 语义：源接口四方法中两 const 两非 const，
    /// 引用可变性随对象本身，不受本访问器限定。
    ui::IModuleDraftSource& modelingDraftSource() const;

    // ---- WP-13-T20 宿主迁移三接入面（B1-SPEC §5.1——共享面只消费；
    //      UI-T21/T22 冻结协议的注册原料经本门面出线，消费面零建模私有头
    //      依赖——R-2 门面纪律延伸）----

    /// 三接入面句柄（树节点供给者＋属性页供给者——宿主注册进
    /// ui::ProjectTreeModel/ui::PropertyInspectorModel 的原料；选择适配器
    /// 经 attachSelectionService 独立接线）。
    struct SharedSurfaceHandles {
        std::shared_ptr<ui::IUiTreeNodesProvider> treeNodes;        ///< 树接入面
        std::shared_ptr<ui::IUiPropertyPagesProvider> propertyPages; ///< 页面接入面
    };

    /**
     * @brief 取迁移三接入面句柄（转发模块惰性构造缓存——见模块头注：
     *        须在面板工厂执行后调用，编辑出口/联动执行器绑定面板）。
     */
    SharedSurfaceHandles sharedSurfaceProviders() const;

    /// @brief 订阅选择服务（下行"树选→面板高亮"启用——转发模块）。
    void attachSelectionService(ui::SelectionService& service);

    /// @brief 显式退订选择服务（幂等——转发模块）。
    void detachSelectionService();

    /// @brief 上报一次本域三维拾取（上行 View3DPick——转发模块；
    ///        false＝未接线/非本域对象，不出诊断）。
    bool reportView3DPick(const sdurws::ird::core::ObjectId& oid);

    ModelingPluginAssembly();
    ~ModelingPluginAssembly();
    ModelingPluginAssembly(ModelingPluginAssembly&&) noexcept;
    ModelingPluginAssembly& operator=(ModelingPluginAssembly&&) noexcept;
    ModelingPluginAssembly(const ModelingPluginAssembly&) = delete;
    ModelingPluginAssembly& operator=(const ModelingPluginAssembly&) = delete;

private:
    class ModelingUiModule* m_impl = nullptr;  ///< 具体模块（非 owning——module 持有）

    friend ModelingPluginAssembly createModelingPluginAssembly();  ///< 唯一装配点（实现侧回填 m_impl）
};

/**
 * @brief 创建建模插件装配产物（每次调用全新实例——descriptor 由
 *        modelingPanelRegistration/modelingDomainCommands 现产，无缓存）。
 *
 * @return 装配产物（UI 线程构造——模块会话态绑定构造线程）
 */
ModelingPluginAssembly createModelingPluginAssembly();

/**
 * @brief 已装配域命令的权威判定（UI-T42——F-466 消账：宿主注册表路由集
 *        的唯一事实源在 modeling 命令流层〔ModelingCommandFlows 同词表〕，
 *        经本门面出线供 ui 插件转发消费——R-2 合规：ui 只见本装配契约头，
 *        零 modeling 私有头依赖；装配集∧目录集一致性可被 modeling_test
 *        断言，宿主侧零第二词表）。
 *
 * @param commandId [in] 域命令 id（点分小写——§9.7.3 卡表词表）
 * @return true＝真实执行链在位（宿主按 assembled 放行）；false＝注册期
 *         禁用（fail-closed——不产生"可点但执行失败"的中间态）
 */
bool isAssembledModelingCommand(const std::string& commandId);

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_MODELING_ASSEMBLY_MODELINGPLUGINASSEMBLY_HPP
