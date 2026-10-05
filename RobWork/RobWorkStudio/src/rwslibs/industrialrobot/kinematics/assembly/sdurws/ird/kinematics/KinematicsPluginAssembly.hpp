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
#include <sdurws/ird/ui/ICommandRegistry.hpp>     // ui::CommandAvailability（按钮门控）
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // AnalysisConfiguration（会话事实基线——kinematics 公共头）
#include <sdurws/ird/kinematics/KinematicsPanelChannels.hpp>  // UI-T64 装配通道值面（同目录公共头——R-2 装配层零私有头）

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
    void bindCommandAvailability(
        std::function<ui::CommandAvailability(const std::string&)> availability);

    /// 服务缝注入（面板创建前调用生效——真实模型视图/求解器/后台缝的
    /// 装配接线点；转发模块内部）。
    void setServices(KinPanelServices services);

    // ---- 覆盖评估执行通道（UI-T64——F-490① 上游批的装配半区）----

    /**
     * @brief 装配覆盖评估执行通道（产品装配层的产品注入点——通道值面
     *        翻译进插件私有 KinPanelServices 对应缝，翻译在本门面实现
     *        TU 单点执行）。
     *
     * R-2 纪律的落位形态：KinPanelServices 及其往返值类型是插件私有
     * 类型，ui 装配层不可见也不可构造——装配层经本方法以公共通道值面
     * （KinematicsAssemblyChannels）注入，等效于私有缝注入（空缝降级
     * 语义逐字保持）。面板创建前后皆可调用（面板读缝时机＝能力判定，
     * 与 setServices 同纪律：建议面板创建前注入生效）。
     *
     * @param channels [in] 通道聚合（指针缝非 owning——存活期契约见值
     *                面类注；function 缝值持有）
     */
    void installAssemblyChannels(KinematicsAssemblyChannels channels);

    /**
     * @brief 后台完成通知入口（装配层执行器任务终态的投递面——通道
     *        ResultNote 翻译为插件私有 KinBackgroundResultNote 后经
     *        KinPanelFlows::noteBackgroundResult 消费：迟到判定/中断
     *        如实/状态反馈）。
     *
     * 线程义务在调用方：本方法须在 UI 线程调用（执行器侧经
     * QMetaObject::invokeMethod 回投——harness 的同步直调形态不可照抄
     * 到产品多线程面）。
     *
     * @param note [in] 完成通知值（通道投影——原值面语义见类型注）
     * @return 状态反馈文本（noteBackgroundResult 产出——迟到丢弃/中断/
     *         正常三态的如实文案；调用方可呈现或留痕）
     */
    std::string noteAssemblyBackgroundResult(
        const KinChannelBackgroundResultNote& note);

    /**
     * @brief 会话事实批量写入（UI-T64——装配层发布消费点的绑定面同步：
     *        snapshotId/纪元/可写性/配置基线四值一次写入模块会话态）。
     *
     * R-2 落位说明：KinModuleSessionState 是插件私有类型——装配层经
     * 本公共方法写会话事实，不解引用私有聚合（成员语义见模块侧类型注
     * ——snapshotId＝结果绑定快照内容身份；epoch＝迟到判定锚；writable
     * ＝L-K11 门控输入；savedConfig＝求解配置基线，须恒过
     * validateAnalysisConfiguration——I-KIN-4，合法性由调用方保证）。
     *
     * 线程约束：仅 UI 线程（§9.4 会话态纪律）。
     *
     * @param snapshotId [in] 结果绑定快照内容身份（全零＝未绑定）
     * @param epoch      [in] 会话纪元（调用方为主锚——执行器同源值）
     * @param writable   [in] 会话可写性（项目打开拍的事实重放）
     * @param config     [in] 求解配置基线（合法——seed≥1）
     */
    void bindSessionFacts(core::ContentIdentity snapshotId, std::uint64_t epoch,
                          bool writable,
                          const AnalysisConfiguration& config);

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
