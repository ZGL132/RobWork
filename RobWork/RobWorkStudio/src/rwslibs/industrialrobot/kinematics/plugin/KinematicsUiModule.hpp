/**
 * @file   KinematicsUiModule.hpp
 * @brief  kinematics 插件界面模块——ui::IPluginUiModule（ui.md §11.2）的
 *         kinematics 侧实现（装配缝；P-KIN-7 对端冻结后的正式继承形态）。
 *
 * 设计依据：
 *   - units/ui.md §11.2（IPluginUiModule 三方法：onShellReady／
 *     readonlyProjections／buildDraftCommand）、§10.9（装配期经
 *     IPluginUiRegistrar 注册——装配期一次、白名单校验）；
 *   - units/kinematics.md §9.8（面板工厂/域命令/就绪投影）、§12 交接表
 *     （"本单元无草稿——分析配置用户级"：buildDraftCommand 恒 nullopt 的
 *     语义锚）；§14.6（P-KIN-7 处置：ui 对端 IPluginUiRegistrar/
 *     IPluginUiModule 已随 WP-24-T03 落位冻结——直接继承消费，无过渡）；
 *   - modeling 先例 ModelingUiModule 同构（WP-13-T15/WP-24-T03 落位形态）；
 *   - 需求 UX-04/UX-05；任务契约 tasks/foundation/WP-15-T12.json。
 *
 * 线程约束：仅 UI 线程访问（§3.4——模块持会话态与面板引用）。确定性：
 * readonlyProjections 同会话事实→同行集（NFR-COR-02）。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINEMATICSUIMODULE_HPP
#define IRD_KINEMATICS_PLUGIN_KINEMATICSUIMODULE_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QString>                          // 文案解析器返回值（bindTextResolver）
#include <sdurws/ird/project/CommandService.hpp> // project::CommandEnvelope 完整类型——
                                            // 声明点解析一致性义务（modeling 先例
                                            // ModelingUiModule 同款：本头与 ui 接口头
                                            // 的 project:: 名字必须解析到同一实体——
                                            // sdurws::ird::project，否则 override 签名
                                            // 漂移即 C2556）
#include <sdurws/ird/ui/IPluginUiModule.hpp> // ui::IPluginUiModule（§11.2 接口）
#include <sdurws/ird/ui/ICommandRegistry.hpp> // ui::CommandAvailability（统一按钮门控）
#include <sdurws/ird/ui/IWorkbenchShell.hpp> // ui::IWorkbenchShell（onShellReady 入参）
#include <sdurws/ird/ui/UiTypes.hpp>         // ui::DomainReadinessItem（§6.5 汇聚值）
#include "KinHostMigrationProviders.hpp"     // 迁移三接入面（WP-15-T18——同目录私有头）
#include "KinPanelTypes.hpp"                  // 会话态/服务缝（同目录私有头）

namespace sdurws {
namespace ird {
namespace ui {
class SelectionService;  // 前置声明（attachSelectionService 入参——完整类型随
                         // KinHostMigrationProviders 传递）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

class QWidget;  // 前置声明：createPanel 返回类型（全局域——插件目标 Widgets 面）

namespace sdurws {
namespace ird {
namespace kinematics {

class KinematicsPanelWidget;   // 前置声明（面板引用面）

/**
 * @brief kinematics 插件界面模块（ui::IPluginUiModule 的 kinematics 侧
 *        正式继承实现——P-KIN-7 对端冻结的消账形态）。
 *
 * 生命周期：装配期由插件装配点创建（UI 线程），存活至壳拆除（§10.9）。
 * 面板工厂经装配描述符提供（KinematicsPluginAssembly——assembly/ 公共门面）。
 */
class KinematicsUiModule final : public ui::IPluginUiModule {
public:
    /// 命令提交出口形态（面板同款——装配层经本转发绑定面板）。
    using CommandSubmitFn = std::function<void(const std::string& commandId)>;
    using CommandAvailabilityFn = std::function<ui::CommandAvailability(const std::string&)>;

    /// 文案解析器形态（titleKey→工程用语——宿主接 ui::resolveText）。
    using TextResolver = std::function<QString(const std::string& titleKey)>;

    /**
     * @brief 构造模块（UI 线程——会话态绑定构造线程）。
     *
     * @param services [in] 服务缝聚合（缺省空缝——装配门面先建模块后经
     *                 setServices 注入；面板创建前注入生效）
     */
    explicit KinematicsUiModule(KinPanelServices services = KinPanelServices{});
    ~KinematicsUiModule() override;
    KinematicsUiModule(const KinematicsUiModule&) = delete;
    KinematicsUiModule& operator=(const KinematicsUiModule&) = delete;

    /**
     * @brief 注入/替换服务缝（面板创建前调用生效——创建后替换仅影响
     *        后建面板；装配门面 setServices 转发的落点）。
     */
    void setServices(KinPanelServices services) { m_services = std::move(services); }

    // ---- ui.md §11.2 三方法 ------------------------------------------------

    /**
     * @brief 壳就绪回调（§11.2"注册回调后初始化"——持有壳引用；域命令
     *        注册经装配描述符在装配期完成，本回调零重复注册）。
     *
     * @param shell [in] 工作台壳门面（非 owning——存活期由壳侧保证）
     */
    void onShellReady(ui::IWorkbenchShell& shell) override;

    /**
     * @brief 只读就绪投影（§6.5 汇聚源——kinematics 行；P-UI-6 处置：
     *        服从 ui 卡原登记裁决，按 DomainReadinessItem 冻结形状直投）。
     *
     * @return 单元素投影（输入不完整缺省行——不伪造可行性；在途任务
     *         事实取自服务缝任务投影）
     */
    std::vector<ui::DomainReadinessItem> readonlyProjections() const override;

    /**
     * @brief 草稿应用命令组装（§8.5 域侧半区——**kinematics 无草稿**：
     *        分析配置为用户级（非项目草稿），恒 nullopt 不产生空修订；
     *        卡 §12 交接表"本单元无草稿"的接口面落点）。
     *
     * @param moduleId [in] 域注册键（任何值——域内外一致 nullopt）
     * @return nullopt（恒——域无草稿语义，非占位实现）
     */
    std::optional<project::CommandEnvelope> buildDraftCommand(
        const std::string& moduleId) override;

    // ---- 装配 API（装配门面 KinematicsPluginAssembly 转发）----------------

    /// 绑定命令提交出口（面板创建前后皆可——后绑定在面板创建时应用）。
    void bindCommandSubmit(CommandSubmitFn submitFn);
    void bindCommandAvailability(CommandAvailabilityFn availability);

    /// 绑定文案解析器（面板创建时应用；不绑定＝按钮呈现键名原文）。
    void bindTextResolver(TextResolver resolve);

    /**
     * @brief 创建主面板（四面板合一 Tab 容器——§10.9 PanelRegistration
     *        工厂的模块半区；内部完成提交出口/文案解析接线；每次调用
     *        新建，装配层恰调一次）。
     *
     * @return 面板 widget（归调用方接管——宿主层持有）
     */
    QWidget* createPanel();

    /**
     * @brief 创建求解配置高级面板（§9.8 面板表行 5——advanced=true 挂位；
     *        ui 表单公共件消费面 UI-T08 对端）。
     *
     * @return 面板 widget（归调用方接管）
     */
    QWidget* createAdvancedConfigPanel();

    /// 会话刷新（restoreOnOpen 等会话事件后的面板同步——现取重投影；
    /// 面板未创建＝空操作）。
    void refreshFromSession();

    /// 会话态访问（装配层注入快照绑定/配置基线等会话事实的唯一入口）。
    KinModuleSessionState& session() noexcept { return *m_session; }

    // ---- 宿主迁移三接入面（WP-15-T18——requirements 先例 RequirementsUiModule
    //      同构暴露面；装配层注册进共享模型的选择面）--------------------

    /**
     * @brief 迁移三接入面句柄（树节点供给者＋属性页供给者——共享模型的
     *        注册原料；选择适配器经 attachSelectionService 独立接线）。
     *
     * v1 语义（DTB §4.2 O-44 裁决——接入面 v1 诚实边界）：树面＝结构接
     * 缝注册＋恒空集供给（本域无持有 ObjectId 的对象，协议"可空＝合法常
     * 态"）；页面＝结构接缝注册＋无应答面（nullopt/空集合法二态）。惰性
     * 构造并缓存（shared_ptr 稳定地址——requirements 先例同款）。
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
     * @brief 订阅选择服务（SelectionAdapter 接线——下行"树选任务点→结果
     *        面板高亮"的启用；服务存活期须覆盖订阅期且晚于本模块析构，
     *        适配器 RAII 句柄语义见 KinHostMigrationProviders.hpp 类注）。
     */
    void attachSelectionService(ui::SelectionService& service);

    /// @brief 显式退订选择服务（幂等——未订阅时空操作）。
    void detachSelectionService();

    /**
     * @brief 宿主关节状态承接（D8/D9 会话姿态桥的域侧半区——Jog 关节/
     *        笛卡尔点动与 Playback 播放帧共同的运动学侧入口；宿主 State
     *        变化经装配层适配后调本方法写入会话姿态）。
     *
     * 零修订/零失效/零缓存（KIN-06/AT-04 口径——acceptance 2/3）：本方法
     * 只写 KinSessionPose（结构化零端口容器——写操作在类型面不存在产生
     * 修订或失效的通道）并触发面板现取重投影；不触命令出口、不触后台缝、
     * 不触任何缓存身份（会话姿态身份外——D-KIN-4，求解身份只随显式输入
     * 变化）。零 Qt 计算（重投影走既有刷新路径——NFR-PERF-01）。
     *
     * @param q [in] 权威关节向量（rad／m；链序——宿主 State 桥投影的链
     *           序值；非有限分量属调用方违约，经 KinSessionPose 异常
     *           fail-fast 透传——NFR-COR-03 不钳制不置零）
     * @return true＝已写入会话姿态；false＝会话姿态缝未装配
     *         （KinPanelServices.sessionPose 为空——宿主桥未接线的诚实
     *         降级，零虚构写入成功）
     */
    bool applyHostJointState(const std::vector<double>& q);

private:
    KinPanelServices m_services;                 ///< 服务缝（面板数据源）
    std::unique_ptr<KinModuleSessionState> m_session; ///< 会话权威态
    KinematicsPanelWidget* m_panel = nullptr;    ///< 主面板引用（非 owning）
    QWidget* m_configPanel = nullptr;            ///< 高级面板引用（非 owning）
    ui::IWorkbenchShell* m_shell = nullptr;      ///< 壳门面引用（非 owning）
    CommandSubmitFn m_pendingSubmit;             ///< 面板创建前暂存（创建时应用）
    CommandAvailabilityFn m_commandAvailability;  ///< 注册表命令可用性查询
    TextResolver m_textResolver;                 ///< 文案解析（创建时应用）

    // 迁移三接入面缓存（惰性构造——shared_ptr 稳定地址；适配器 unique_ptr
    // 随模块生命周期；requirements 先例 RequirementsUiModule 同款）。
    std::shared_ptr<ui::IUiTreeNodesProvider> m_treeProvider;        ///< 树接入面缓存
    std::shared_ptr<ui::IUiPropertyPagesProvider> m_pageProvider;    ///< 页面接入面缓存
    std::unique_ptr<KinematicsSelectionAdapter> m_adapter;           ///< 选择适配器缓存
};

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINEMATICSUIMODULE_HPP
