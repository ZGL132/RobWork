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
#include <sdurws/ird/ui/IWorkbenchShell.hpp> // ui::IWorkbenchShell（onShellReady 入参）
#include <sdurws/ird/ui/UiTypes.hpp>         // ui::DomainReadinessItem（§6.5 汇聚值）
#include "KinPanelTypes.hpp"                  // 会话态/服务缝（同目录私有头）

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

private:
    KinPanelServices m_services;                 ///< 服务缝（面板数据源）
    std::unique_ptr<KinModuleSessionState> m_session; ///< 会话权威态
    KinematicsPanelWidget* m_panel = nullptr;    ///< 主面板引用（非 owning）
    QWidget* m_configPanel = nullptr;            ///< 高级面板引用（非 owning）
    ui::IWorkbenchShell* m_shell = nullptr;      ///< 壳门面引用（非 owning）
    CommandSubmitFn m_pendingSubmit;             ///< 面板创建前暂存（创建时应用）
    TextResolver m_textResolver;                 ///< 文案解析（创建时应用）
};

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINEMATICSUIMODULE_HPP
