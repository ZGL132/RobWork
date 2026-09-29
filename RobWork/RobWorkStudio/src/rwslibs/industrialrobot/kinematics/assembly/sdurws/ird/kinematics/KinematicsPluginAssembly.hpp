/**
 * @file   KinematicsPluginAssembly.hpp
 * @brief  kinematics 插件装配门面——宿主装配层（WP-24-T03 首版装配及其
 *         收口）消费 kinematics 插件的**唯一公共入口**。
 *
 * 设计依据：
 *   - units/ui.md §10.9/§11.1（registerPluginUi 入参形状；白名单 token；
 *     面板工厂语义）、§11.2（IPluginUiModule 三方法）；
 *   - units/kinematics.md §9.8（面板组成五行/域命令清单/装配挂位——
 *     KinPanelCommandCatalog 登记数据经本门面直通装配层）、§3.2（"插件
 *     目标 → 本计算库＋sdurws_ird_ui"链接面——本头即 O-31 装配器单行
 *     适配面：装配层只见本头与 ui 公共头，零 kinematics 插件私有头依赖）；
 *   - modeling 先例 ModelingPluginAssembly 同构（WP-24-T03 落位形态）；
 *   - 任务契约 tasks/foundation/WP-15-T12.json（§3.2"_plugin 随 WP-15-T12"
 *     的落位承诺——本头与插件目标同批交付）。
 *
 * 落位形态说明：本头位于 kinematics 单元的 `assembly/` 目录（plugin 目标
 * 的 PUBLIC include 面——插件界面目标的装配契约头，非产品 include/ 扫描
 * 域；与 plugin/ 同理在零 Qt 红线的文件域之外）。实现
 * （plugin/KinematicsPluginAssembly.cpp）编入 sdurws_ird_kinematics_plugin
 * 目标。
 *
 * 线程模型：本门面全部函数 UI 线程调用（模块/面板同约束——§3.4）。
 */
#ifndef IRD_KINEMATICS_ASSEMBLY_KINEMATICSPLUGINASSEMBLY_HPP
#define IRD_KINEMATICS_ASSEMBLY_KINEMATICSPLUGINASSEMBLY_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QString>                                 // 文案解析返回值（bindTextResolver）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>    // ui::PluginUiDescriptor（§10.9 装配描述符）
#include <sdurws/ird/ui/SelectionService.hpp>      // ui::SelectionService（attachSelectionService 入参——ui 公共头）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::CommandId（命令提交出口入参）

namespace sdurws {
namespace ird {
namespace ui {
class IUiTreeNodesProvider;       // 前置声明（迁移三接入面句柄成员——完整类型
class IUiPropertyPagesProvider;   //  在 ui 公共头；本头仅以 shared_ptr 携带）
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

namespace sdurws {
namespace ird {
namespace kinematics {

class KinematicsUiModule;      // 前置声明（内部具体类型——消费方面只见接口）
class KinModuleSessionState;   // 前置声明（会话态访问面——装配层注入会话事实）
struct KinPanelServices;       // 前置声明（服务缝聚合——setServices 转发入参）

/**
 * @brief 迁移三接入面句柄（WP-15-T18——树节点供给者＋属性页供给者的门面
 *        出口形态；自持结构避免门面头拖入插件私有头——"装配层只见本头与
 *        ui 公共头"纪律，O-31 同源）。
 *
 * v1 语义（DTB §4.2 O-44）：树面＝结构接缝＋恒空集供给；页面＝结构接缝
 *   ＋无应答面——注册本身即接缝落位（UI-T23 集成收口按 domainKey 消费）。
 *   shared_ptr 稳定地址（模块侧缓存同序——重复调用返回同一实例）。
 */
struct KinematicsSharedSurfaceHandles {
    std::shared_ptr<ui::IUiTreeNodesProvider> treeNodes;        ///< 树接入面
    std::shared_ptr<ui::IUiPropertyPagesProvider> propertyPages; ///< 页面接入面
};

/**
 * @brief kinematics 插件装配产物（createKinematicsPluginAssembly 返回值）。
 *
 * 消费序（宿主装配层的标准用法——modeling 先例同构）：
 *   ①`bindTextResolver`（接 ui::resolveText——命令/按钮呈现工程用语）；
 *   ②`bindCommandSubmit`（接宿主提交面——ui ICommandRegistry.submit）；
 *   ③`session()` 注入会话事实（快照绑定/配置基线/可写性）；
 *   ④`registrar.registerPluginUi(descriptor, *module)`；
 *   ⑤`descriptor.panels` 各 factory 取面板挂位（主面板＋高级面板）。
 *
 * 所有权：module 由本结构 unique_ptr 持有；内部实现指针非 owning。可移动
 * （装配层转移持有）；不可拷贝。
 */
struct KinematicsPluginAssembly {
    std::unique_ptr<ui::IPluginUiModule> module;  ///< 模块（接口面——三方法契约）
    ui::PluginUiDescriptor descriptor;            ///< 装配描述符（§10.9 形状直通）

    /// 命令提交出口绑定（转发模块内部——面板创建前后皆可）。
    void bindCommandSubmit(std::function<void(const ui::CommandId&)> submit);

    /// 服务缝注入（面板创建前调用生效——真实模型视图/求解器/后台缝的
    /// 装配接线点；转发模块内部）。
    void setServices(KinPanelServices services);

    /// 文案解析绑定（titleKey→工程用语；转发模块内部）。
    void bindTextResolver(std::function<QString(const std::string& titleKey)> resolve);

    /// 会话态访问（装配层注入快照绑定/配置基线等会话事实的入口）。
    KinModuleSessionState& session() const;

    /// 会话刷新（restoreOnOpen 后由宿主调用——面板同步）。
    void refreshFromSession();

    // ---- 宿主迁移三接入面（WP-15-T18——UI-T23 集成收口的消费面）----

    /**
     * @brief 迁移三接入面句柄（宿主注册进 ui::ProjectTreeModel/
     *        ui::PropertyInspectorModel 的原料——转发模块同名词柄）。
     *
     * @return 句柄对（shared_ptr 稳定地址——模型持强引用后存活期由注册
     *         关系保证）
     */
    KinematicsSharedSurfaceHandles sharedSurfaceProviders();

    /**
     * @brief 订阅选择服务（下行"树选任务点→结果面板高亮"的启用——转发
     *        模块 SelectionAdapter 接线；服务存活期契约见适配器类注）。
     *
     * @param service [in] 选择服务（引用入参无空态；存活期须覆盖订阅期）
     */
    void attachSelectionService(ui::SelectionService& service);

    /**
     * @brief 宿主关节状态承接（D8/D9 会话姿态桥的域侧半区——Jog/Playback
     *        的宿主 State 变化经装配适配后调本入口写入会话姿态；零修订/
     *        零失效/零缓存，KIN-06/AT-04——转发模块同名方法）。
     *
     * @param q [in] 权威关节向量（rad／m；链序）；非有限分量属调用方
     *           违约（异常 fail-fast 透传——NFR-COR-03）
     * @return true＝已写入；false＝会话姿态缝未装配（诚实降级）
     */
    bool applyHostJointState(const std::vector<double>& q);

    KinematicsPluginAssembly();
    ~KinematicsPluginAssembly();
    KinematicsPluginAssembly(KinematicsPluginAssembly&&) noexcept;
    KinematicsPluginAssembly& operator=(KinematicsPluginAssembly&&) noexcept;
    KinematicsPluginAssembly(const KinematicsPluginAssembly&) = delete;
    KinematicsPluginAssembly& operator=(const KinematicsPluginAssembly&) = delete;

private:
    class KinematicsUiModule* m_impl = nullptr;  ///< 具体模块（非 owning——module 持有）

    friend KinematicsPluginAssembly createKinematicsPluginAssembly();  ///< 唯一装配点
};

/**
 * @brief 创建 kinematics 插件装配产物（每次调用全新实例——descriptor 由
 *        kinematicsPanelRegistration/kinematicsDomainCommands 现产，无缓存）。
 *
 * descriptor 装配规则（§9.8 面板表五行→两条 PanelRegistration 的映射，
 * 随本门面登记）：
 *   - 行 1~4（位姿指标/任务点验证/区域覆盖/结果与可视化）→ 主面板一条
 *     登记记录（四面板合一 Tab 容器——页序＝表行序；advanced=false）；
 *   - 行 5（求解配置）→ 高级面板一条登记记录（UX-04 高级参数收拢——
 *     advanced=true，独立挂位工作台高级面板位）。
 *
 * @return 装配产物（UI 线程构造）
 */
KinematicsPluginAssembly createKinematicsPluginAssembly();

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_ASSEMBLY_KINEMATICSPLUGINASSEMBLY_HPP
