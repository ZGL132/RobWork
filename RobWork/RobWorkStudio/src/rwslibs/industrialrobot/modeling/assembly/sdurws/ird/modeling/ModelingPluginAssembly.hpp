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

#include <QString>                                 // 文案解析返回值（bindTextResolver）
#include <sdurws/ird/core/Identity.hpp>            // core::BranchId/RevisionId/ObjectId（会话锚/回执值面）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>    // ui::PluginUiDescriptor（§10.9 装配描述符）
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

    /// 首版会话种子（generic-6r 真实草稿；转发模块内部）。
    void seedTemplateSession();

    /// 会话锚绑定（打开成功后——分支＋tip；转发模块内部，T03b-2）。
    void bindSessionAnchor(const sdurws::ird::core::BranchId& branch,
                           const sdurws::ird::core::RevisionId& base);

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
