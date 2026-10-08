/**
 * @file   RequirementsPluginAssembly.hpp
 * @brief  requirements 插件装配门面——宿主装配层（UI-T23 三域集成收口）
 *         消费 requirements 插件的**唯一公共入口**（O-45 裁决补建缝）。
 *
 * 设计依据：
 *   - DTB §4.2 O-45 裁决（2026-09-29 所有者"解决阻塞"口令）：需求域三接入
 *     面（RequirementsUiModule/HostMigrationProviders——WP-14-T10 已验收）
 *     全在 requirements/plugin/ 私有头，UI-T23 allowedFiles 仅 ui/** 无法
 *     诚实消费（R-2 禁跨单元私有头）——授权本单元补建与 modeling/
 *     kinematics 双先例同构的 assembly/ 公共装配门面（已验收语义只出线
 *     不重写、零行为变化）；
 *   - units/ui.md §10.9/§11.1（registerPluginUi 入参形状；白名单 token；
 *     面板工厂语义）、§11.2（IPluginUiModule 三方法——RequirementsUiModule
 *     本批切换为真实继承，P-REQ-8 消账）；
 *   - units/requirements.md §9.8（面板组成/域命令清单——九条 CommandId
 *     登记数据经本门面直通装配层）、§11（WP-14-T11 行——落位形态）；
 *   - 双先例同构：modeling/assembly/ModelingPluginAssembly.hpp（WP-24-T03
 *     落位形态）、kinematics/assembly/KinematicsPluginAssembly.hpp（
 *     WP-15-T12/T18 落位形态）——O-31 装配器单行适配面：宿主装配层只见
 *     本头与 ui 公共头，零 requirements 插件私有头依赖（R-2）。
 *
 * 落位形态说明：本头位于 requirements 单元的 `assembly/` 目录（plugin
 * 目标的 PUBLIC include 面——插件界面目标的装配契约头，非产品 include/
 * 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外）。实现
 * （plugin/RequirementsPluginAssembly.cpp）编入 sdurws_ird_requirements_
 * plugin 目标。
 *
 * 线程模型：本门面全部函数 UI 线程调用（模块/面板同约束——§3.4；转发
 * 路径内含 PanelUiThreadGuard 断言）。
 */
#ifndef IRD_REQUIREMENTS_ASSEMBLY_REQUIREMENTSPLUGINASSEMBLY_HPP
#define IRD_REQUIREMENTS_ASSEMBLY_REQUIREMENTSPLUGINASSEMBLY_HPP

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <functional>
#include <vector>

#include <rw/math/Transform3D.hpp>               // TCP 世界系位姿值面（RequirementsView3DSeams）
#include <sdurws/ird/core/Identity.hpp>          // core::BranchId/RevisionId/ObjectId（会话锚/回执值面）
#include <sdurws/ird/project/QueryPort.hpp>      // project::ObjectRef（会话闭包引用元数据——UI-T33 浅核对来源；requirements→project 已登记边公共面消费）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // RequirementReference（会话预览投影值面——UI-T33）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // ui::PluginUiDescriptor（§10.9 装配描述符——值成员需完整类型）
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandAvailability（按钮门控）
#include <sdurws/ird/ui/SelectionService.hpp>    // ui::SelectionService（attachSelectionService 入参——ui 公共头，kinematics 先例同款）

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
namespace requirements {

class RequirementsUiModule;              // 前置声明（内部具体类型——消费方面只见接口）
struct RequirementsModuleSessionState;   // 前置声明（会话态访问面——引用返回，完整类型在插件私有头）
class IRequirementEditor;                // 前置声明（attachEditor 入参——requirements 公共头 Editor.hpp）
struct RequirementReadinessReport;       // 前置声明（bindReadiness 入参——requirements 公共头 Readiness.hpp）

/**
 * @brief 迁移三接入面句柄（WP-14-T10 已验收接入面的门面出口形态；自持
 *        结构避免门面头拖入插件私有头——"装配层只见本头与 ui 公共头"
 *        纪律，与 KinematicsSharedSurfaceHandles 同构）。
 *
 * shared_ptr 稳定地址（模块侧惰性构造缓存——重复调用返回同一实例）。
 */
struct RequirementsSharedSurfaceHandles {
    std::shared_ptr<ui::IUiTreeNodesProvider> treeNodes;         ///< 树接入面
    std::shared_ptr<ui::IUiPropertyPagesProvider> propertyPages; ///< 页面接入面
};

/**
 * @brief requirements 插件装配产物（createRequirementsPluginAssembly
 *        返回值）。
 *
 * 消费序（宿主装配层的标准用法——modeling/kinematics 双先例同构）：
 *   ①`attachEditor`（注入需求编辑器——草稿唯一写目标与工作集权威）；
 *   ②`bindSessionAnchor`/`noteAppliedRevision`/`bindReadiness`（会话
 *     事实注入——随项目打开/修订提交/就绪校验时点更新）；
 *   ③`registrar.registerPluginUi(descriptor, *module)`；
 *   ④`descriptor.panels.front().factory()` 取面板挂位（工厂内部完成
 *     面板创建与模块接线——宿主零 requirements 私有头）；
 *   ⑤`sharedSurfaceProviders()`＋`attachSelectionService`（迁移三接入
 *     面注册进共享树/检查器/选择服务——须在④之后调用：Deps 的编辑分流
 *     出口与高亮/激活执行器绑定面板指针）。
 *
 * 所有权：module 由本结构 unique_ptr 持有；内部实现指针非 owning。可移动
 * （装配层转移持有）；不可拷贝。
 */

/**
 * @brief 三维视图缝（UI-T33——捕获 TCP/拾取收口的宿主真数据源；定义于
 *        本门面＝O-45 消费面：宿主装配层构造、bindView3DSeams 注入后经
 *        模块下传命令流。全部缝可空/缺省＝诚实降级形态——命令流对空缝
 *        按"数据源不可得"原文呈现，不虚构能力）。
 */
struct RequirementsView3DSeams {
    /// 宿主 WorkCell 设备名清单（现取零缓存；空＝无设备）。
    std::function<std::vector<std::string>()> listDevices;
    /// 设备 TCP 世界系位姿（设备名→位姿；nullopt＝不可得——诚实失败）。
    std::function<std::optional<rw::math::Transform3D<>>(
        const std::string& deviceName)>
        tcpWorldPose;
    /// 帧名→业务对象身份（网关名称映射共享实例——拾取收口反解；当前
    /// 宿主＝HostEmptyNameMapPort 诚实空映射，WP-24-T08 替换即激活）。
    std::function<std::optional<core::ObjectId>(const std::string& frameName)>
        resolveFrameObjectId;
    /// 最近一次本域三维拾取（网关分发落点——拾取命令的输入面；空＝未拾取）。
    std::function<std::optional<core::ObjectId>()> lastPickedObjectId;
    /// 会话基线修订 id（捕获 STALE 对账键；空＝无基线——Capture.hpp
    /// CapturedTcpPose.sessionRevisionId 语义同源）。
    std::string sessionRevisionId;
    /// 会话闭包引用元数据（UI-T33 收口——拾取写回浅核对的 CheckContext
    /// 来源：picked 目标 ObjectId 存在＋token 匹配在 project 修订视图的
    /// objectRefs 核对。空返回/缺省＝空上下文——浅核对如实拒绝〔不虚构
    /// 通过〕；生产绑定＝宿主查询端口 head().objectRefs）。
    std::function<std::vector<project::ObjectRef>()> sessionClosureRefs;
};

struct RequirementsPluginAssembly {
    std::unique_ptr<ui::IPluginUiModule> module;  ///< 模块（接口面——三方法契约）
    ui::PluginUiDescriptor descriptor;            ///< 装配描述符（§10.9 形状直通）

    // ---- 会话接线（requirements 形态——模块会话权威态的具名注入面）----

    /**
     * @brief 注入需求编辑器（草稿唯一写目标与工作集权威——L-R2/L-R3 的
     *        数据前提；转发模块 attachEditor）。
     *
     * @param editor [in] 需求编辑器（非 owning——调用方保证存活期覆盖模块；
     *               nullptr＝显式无会话，buildDraftCommand 如实 nullopt）
     */
    void attachEditor(IRequirementEditor* editor);

    /**
     * @brief 会话锚绑定（项目打开成功后——分支＋草稿基线；语义同 modeling
     *        门面 bindSessionAnchor——转发模块会话态字段）。
     *
     * @param branch       [in] 目标分支（信封 branch——brn- 规范身份）
     * @param baseRevision [in] 草稿基线修订（nullopt＝提交期解析 tip，§6.2）
     */
    void bindSessionAnchor(const core::BranchId& branch,
                           const std::optional<core::RevisionId>& baseRevision);

    /**
     * @brief 应用回执回写（draft.apply 提交 Committed 后——基线前移＋根
     *        身份回填；语义同 modeling 门面 noteAppliedRevision——转发模块
     *        会话态字段）。
     *
     * @param newBase      [in] 新基线修订（tip）
     * @param rootObjectId [in] 根对象存储身份（nullopt＝未回填〔首应用前〕）
     */
    void noteAppliedRevision(const core::RevisionId& newBase,
                             const std::optional<core::ObjectId>& rootObjectId);

    /**
     * @brief 就绪报告注入（readonlyProjections 的数据源——IRequirement-
     *        ReadinessChecker 产出的直投值；转发模块会话态字段。判定权威
     *        在 checker，本注入零判定——P-REQ-6 边界）。
     *
     * @param report [in] 最近一次就绪报告（按值存入会话态——呈现数据）
     */
    void bindReadiness(const RequirementReadinessReport& report);

    /**
     * @brief 会话脱离（项目关闭/切换——会话态全清＋选择服务退订；磁盘
     *        草稿零触碰。语义同 modeling 门面 onSessionDetached——UI-T23
     *        关闭清理核查的选择服务半区在此收口）。
     */
    void onSessionDetached();

    /// 绑定宿主命令提交出口（面板按钮经此转发到 ui 注册表）。
    void bindCommandSubmit(std::function<void(const ui::CommandId&)> submit);

    /// 绑定宿主命令可用性查询（按钮与菜单共用注册表快照）。
    void bindCommandAvailability(
        std::function<ui::CommandAvailability(const ui::CommandId&)> availability);

    /// 注入编辑后动作（UI-T29 最小校验——每次 L-2 接受后触发；装配层接
    /// 就绪重算＋refreshPanel 编排。转发模块 setPostEditAction——O-45
    /// 『只出线不重写』门面先例同款）。
    void setPostEditAction(std::function<void()> action);

    // ---- 会话事实接入面（UI-T39——审核 P1/P2 只读接线与呈现收口）----

    /**
     * @brief 会话可写性切换（L-R12 门控输入的宿主接线——项目打开成功/
     *        降级只读/切换时由宿主按打开报告的真实 writable 驱动；转发
     *        模块/面板 setWritable，门控事实源＝ui 只读横幅同源报告）。
     *
     * @param writable [in] 当前项目会话是否可写
     */
    void setWritable(bool writable);

    /**
     * @brief 绑定三维视图缝（装配期一次；网关装配完成后由宿主调用——
     *        命令流 requirements.capture-tcp/pick-feature 随之解除降级；
     *        未绑定＝两命令保持诚实降级原文）。
     */
    void bindView3DSeams(RequirementsView3DSeams seams);

    // ---- 会话预览投影出口（UI-T33 收口——acceptance 1/2/3 的公共值面）----

    /// 工位标记投影值（中性公共面——label＋参考系原值；宿主帧名解析与
    /// 世界系变换归投影方〔ui 侧全缝〕，域类型不出本头）。
    struct StationMarkerView {
        std::string label;              ///< 工位名（场景节点名后缀——UX-02）
        RequirementReference refFrame;  ///< 参考系原值（World 缺省合法）
    rw::math::Vector3D<double> position{};  ///< 标记位置（m；refFrame 系——UI-T76/F-555 增补：工位坐标进三维投影）
    };

    /// 区域预览投影值（中性公共面——refFrame 系几何＋参考系原值；世界系
    /// 变换归投影方。角序＝regionPreviewGeometry 同款零重排直投）。
    struct RegionPreviewView {
        std::array<rw::math::Vector3D<double>, 8> corners{};  ///< 盒八角点（m；refFrame 系）
        RequirementReference refFrame;  ///< 参考系原值（World 缺省合法）
        std::string summaryText;        ///< 预览摘要（失败可见面承载——投影方追注）
        /// 采样格骨架线段（UI-T52——m；refFrame 系；世界系变换归投影方。
        /// 空＝零样本区域仅轮廓〔V-02 口径〕）。
        std::vector<std::pair<rw::math::Vector3D<double>, rw::math::Vector3D<double>>>
            gridLines;
        /// 区域锚（UI-T65——F-495 消费卡的逐区域过滤键：宿主投影方据此
        /// 从评估账面过滤本区域样本；表尾追加零破坏）。
        core::ObjectId regionObjectId;
        /// 位置覆盖率目标下限（UI-T65——∈[0,1]，REQ-03；nullopt＝区域
        /// 未设目标→投影方框色无映射。呈现对照输入，非判定权威）。
        std::optional<double> minPositionCoverage;
    };

    /**
     * @brief 绑定工位标记出口（UI-T33——面板重载时全量投递非禁用工位；
     *        未绑定＝无三维标记投递。转换在门面实现——面板投影值不出
     *        插件边界，本头值面为跨域消费的唯一形态〔R-2〕）。
     */
    void bindStationMarkersSink(
        std::function<void(const std::vector<StationMarkerView>&)> sink);

    /**
     * @brief 绑定区域预览出口（UI-T33——区域页投影时投递；未绑定＝预览
     *        仅文本摘要。同上转换在门面实现）。
     */
    void bindRegionPreviewSink(
        std::function<void(const RegionPreviewView&)> sink);

    /**
     * @brief 会话刷新（宿主 bindReadiness 后的呈现收口——面板以会话最新
     *        报告全面板重投影。打开首刷/外部修订重导线后/项目级撤销提交
     *        后共用；转发模块 refreshFromSession）。
     */
    void refreshFromSession();

    /**
     * @brief 基线重载登记（编辑器 loadBaseline 重建会话后调用——草稿撤销
     *        记账随编辑器栈复位归零＋撤销键刷新；转发模块 noteBaselineReloaded）。
     */
    void noteBaselineReloaded();

    /**
     * @brief 执行一条域命令的 UI 流程（UI-T32 C 批次——宿主命令 handler
     *        的执行出口；转发模块 executeDomainCommand→面板 flows）。
     *
     * @param commandId [in] 命令 id（九条词表）
     * @return 流程完成与否
     */
    bool executeDomainCommand(const std::string& commandId);

    /// 会话态访问（装配层注入/排障入口——完整类型在插件私有头，宿主侧
    /// 仅引用传递；仅 UI 线程）。
    RequirementsModuleSessionState& session() const;

    /**
     * @brief 会话基线修订只读取（UI-T35 P2——外部修订同步的"自身回执"
     *        比对面；nullopt＝未绑定。零写面——仅供宿主事件同步判定）。
     */
    std::optional<core::RevisionId> sessionBaseRevision() const;

    // ---- 宿主迁移三接入面（WP-14-T10 已验收面出线——UI-T23 集成收口的
    //      消费面；零语义重写，全部转发模块同名词柄）----

    /**
     * @brief 迁移三接入面句柄（宿主注册进 ui::ProjectTreeModel/
     *        ui::PropertyInspectorModel 的原料——转发模块同名词柄）。
     *
     * @return 句柄对（shared_ptr 稳定地址；**调用时序**：须在面板工厂
     *         执行之后调用——Deps 的编辑分流出口与高亮/激活执行器绑定
     *         面板指针，先于面板调用＝编辑出口缺位的诚实降级形态）
     */
    RequirementsSharedSurfaceHandles sharedSurfaceProviders();

    /**
     * @brief 订阅选择服务（下行"树选→面板高亮"的启用——转发模块
     *        SelectionAdapter 接线；服务存活期须覆盖订阅期且晚于模块
     *        析构——适配器 RAII 句柄语义见 HostMigrationProviders.hpp）。
     *
     * @param service [in] 选择服务（引用入参无空态；存活期契约见上）
     */
    void attachSelectionService(ui::SelectionService& service);

    /// @brief 显式退订选择服务（幂等——未订阅时空操作；转发模块）。
    void detachSelectionService();

    /**
     * @brief 上报一次本域三维拾取（上行——View3DPick 来源汇入选择服务
     *        唯一写入口；转发模块——闭包外/无效身份拒绝上报返回 false，
     *        不出诊断）。
     *
     * @param oid [in] 拾取命中的对象身份
     * @return true＝已上报（服务已广播）；false＝未接线/闭包外/无效身份
     */
    bool reportView3DPick(const core::ObjectId& oid);

    RequirementsPluginAssembly();
    ~RequirementsPluginAssembly();
    RequirementsPluginAssembly(RequirementsPluginAssembly&&) noexcept;
    RequirementsPluginAssembly& operator=(RequirementsPluginAssembly&&) noexcept;
    RequirementsPluginAssembly(const RequirementsPluginAssembly&) = delete;
    RequirementsPluginAssembly& operator=(const RequirementsPluginAssembly&) = delete;

private:
    class RequirementsUiModule* m_impl = nullptr;  ///< 具体模块（非 owning——module 持有）

    friend RequirementsPluginAssembly createRequirementsPluginAssembly();  ///< 唯一装配点
};

/**
 * @brief 创建 requirements 插件装配产物（每次调用全新实例——descriptor
 *        由 requirementsPanelRegistration/requirementsDomainCommands 现产，
 *        无缓存）。
 *
 * descriptor 装配规则（§9.8 面板表→一条 PanelRegistration 的映射，随本
 * 门面登记）：五面板区（需求对象树/工位/区域/工况/校验）合一为**主面板
 * 一条登记记录**（Tab 容器——页序＝表行序；advanced=false，UX-04 高级
 * 面板标记不置位——PanelRegistrationRecord.advanced 既定值）；工厂闭包
 * 内部完成面板创建、编辑目标提供器接线（现取 attachEditor 注入的权威
 * 编辑器——惰性安全）与模块 attachPanel 移交（宿主零私有头）。
 *
 * @return 装配产物（UI 线程构造——模块会话态绑定构造线程）
 */
RequirementsPluginAssembly createRequirementsPluginAssembly();

}  // namespace requirements
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_REQUIREMENTS_ASSEMBLY_REQUIREMENTSPLUGINASSEMBLY_HPP
