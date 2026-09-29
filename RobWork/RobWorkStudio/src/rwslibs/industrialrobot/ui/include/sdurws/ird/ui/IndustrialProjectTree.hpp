/**
 * @file   IndustrialProjectTree.hpp
 * @brief  工业项目树（IndustrialProjectTree）——业务主导航（D3）：五分组
 *         封闭清单、节点身份一律 ObjectId、显示名经名称端口解析（UX-02）、
 *         树自身零第二套命名缓存；域节点经 TreeNodesProvider 注册协议供给
 *         （本任务冻结协议形状，供 UI-T22 与三域迁移消费）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T21.json acceptance 1/3/5（树落位与
 *     五分组封闭清单/联动 L1 基线与 L3 树定位/INV-B1~B3 边界断言）；
 *   - B1-SPEC §3.1（工业项目树承载项目修订数据：五分组封闭清单——项目
 *     结构〔项目/方案/草稿〕、建模对象、需求对象、分析配置、结果/证据/
 *     报告；树节点身份一律 ObjectId〔CON-01〕；显示名经名称端口解析
 *     〔UX-02〕，树自身不缓存第二套命名）、§3.3（INV-B1 项目树零运行时
 *     结构节点；INV-B3 两树互不持有对方选中状态）、§5.1（迁移交付面
 *     TreeNodesProvider——向工业项目树供给本域树节点〔ObjectId→节点
 *     模型〕；本任务冻结协议形状）、§5.3（共享 UI 互斥——本头属
 *     UI-T21/T22/T23 共享面）、§4.2 L3（反解成功的树定位落点）；
 *   - units/ui.md §13 UI-T21 行（"双树节点清单封闭〔规格 §3，增量须
 *     B1-SPEC 修订〕；TreeView 注入业务节点/项目树注入运行时节点均
 *     禁止"）、§6.6（UX-02 对象定位：subjectObjectId → 名称端口 →
 *     localName；解析失败显示占位，不自行拼接名称——R-4）；
 *   - ARCHITECTURE §7.12/SA-18（D3 业务主导航；D4 TreeView 只承载运行时
 *     结构——两树互不注入）；
 *   - SelectionService.hpp（本树的选中唯一汇聚点——树选择经服务写入，
 *     服务反解定位经树回调落点，INV-B3 结构闭环）；
 *   - knownPitfalls：O-43（宿主 Dock 拓扑已由 UI-T18 冻结——本组件只
 *     交付内容面板，宿主挂位归集成收口任务〔UI-T23/WP-24-T08〕，不动
 *     拓扑）；O-38（TreeView 为框架组件零修改——本头与框架 TreeView
 *     零 API 交集，L4 防过度交付）。
 *
 * 背景说明（为什么树模型是"提供者驱动＋名称旁路"形态）：
 *   五个分组的节点分散在 project/modeling/requirements/kinematics 等域，
 *   而 ui 对业务域零编译依赖（R-1）——树内容只能由域侧以 Provider 值供
 *   给（B1-SPEC §5.1 渐进迁移的三接入面之一）。显示名则相反：树的职责
 *   是导航呈现，而命名权威在 runtime RuntimeNameMap（SA-05）——树若缓存
 *   名称就有第二套命名（UX-02 红线），所以模型层只存 ObjectId，显示名
 *   在渲染时刻经 IUiNameResolver 现取（解析失败显示占位）。这两个"只存
 *   身份、现取名称"的结构选择，就是 INV-B1（无业务身份的运行时结构节点
 *   在模型上不可表达）与"零第二套命名缓存"的类型级承载。
 *
 * 线程模型：模型层全部入口只在 UI 线程调用（ui.md §3.4 M-1——树重建/
 *   查询由面板渲染路径驱动）；面板构建经工厂在 UI 线程执行。非线程安全。
 *
 * 生命周期/所有权：ProjectTreeModel 由装配层持有（shared_ptr——模型与
 *   面板共享）；Provider 由装配层持有（模型只持引用登记，析构前须先从
 *   模型摘除或保证存活期覆盖）；面板 QWidget 所有权随 Qt 父子树（工厂
 *   返回裸指针、由调用方指定 parent 接管——ParamTablePanel 同款惯例）。
 */

#ifndef SDURWS_IRD_UI_INDUSTRIALPROJECTTREE_HPP
#define SDURWS_IRD_UI_INDUSTRIALPROJECTTREE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>   // core::ObjectId（节点唯一身份——CON-01，表内登记边直用）
#include <sdurws/ird/ui/SelectionService.hpp>  // ui::SelectionService（面板装配依赖——选中唯一汇聚点，INV-B3）
#include <sdurws/ird/ui/UiPorts.hpp>      // ui::IUiNameResolver（显示名解析端口——UX-02，C-11 既有冻结形状零改动）

class QWidget;  // 前置声明：面板工厂返回（头文件不拖入 Widgets——FormEditCommon 同款）

namespace sdurws {
namespace ird {
namespace ui {

class ProjectTreeModel;  // 前置声明（面板工厂入参——完整类型在下方定义）

// =====================================================================
// 五分组封闭词表（B1-SPEC §3.1——新增分组须 B1-SPEC 增量修订）
// =====================================================================

/**
 * @brief 工业项目树分组词表（B1-SPEC §3.1 五行——枚举序即呈现序，固定
 *        不随 Provider 注册序变化）。
 *
 * 封闭纪律：这是双树边界（§3"节点类型清单为封闭清单"）的分组半区——
 * 第六个分组的引入＝产品形态变更，必须走 B1-SPEC 增量修订（枚举扩值
 * 在代码评审中无法被静默绕过——IHostController ProductMainWindow 同款
 * 机制）。INV-B1 的承载面之一：词表中不存在任何"运行时结构"分组
 * （Frame/Joint/Device/Drawable 在本树上不可表达——B1-SPEC §3.2 它们
 * 只属于官方 TreeView）。
 */
enum class ProjectTreeGroup : std::uint8_t {
    /// 项目结构（项目/方案〔分支〕/草稿〔draft 状态标记〕——B1-SPEC
    /// §3.1 行 1；projectId/BranchId/Draft 的 ObjectId 形态承载）。
    ProjectStructure,
    /// 建模对象（RobotDesign 及其关节/连杆/工具/场景/命名位姿等子对象
    /// ——modeling 域 ObjectId；B1-SPEC §3.1 行 2）。
    ModelingObjects,
    /// 需求对象（任务点、区域、工况、需求集——requirements 域 ObjectId；
    /// B1-SPEC §3.1 行 3）。
    RequirementObjects,
    /// 分析配置（AnalysisConfiguration 及各域分析配置节点——独立持久化
    /// 对象；B1-SPEC §3.1 行 4）。
    AnalysisConfigurations,
    /// 结果/证据/报告（各域结果对象、ResultEnvelope/AnalysisSnapshot
    /// 证据节点、ReviewReport 报告节点；B1-SPEC §3.1 行 5）。
    ResultsEvidenceReports,
};

/// @brief 分组总数（五——封闭词表的编译期钉；遍历/断言用）。
constexpr std::uint8_t kProjectTreeGroupCount = 5;

/// @brief 分组序遍历（枚举序＝B1-SPEC §3.1 行序＝呈现序——稳定）。
/// @param group [in] 任一分组值
/// @return 其序号（0~4——越界值不可能存在于封闭枚举）
constexpr std::uint8_t projectTreeGroupIndex(ProjectTreeGroup group) noexcept
{
    return static_cast<std::uint8_t>(group);
}

// =====================================================================
// 树节点值与域供给协议（B1-SPEC §5.1 TreeNodesProvider——形状本任务冻结）
// =====================================================================

/**
 * @brief 项目树节点值（Provider 供给的只读值——模型重建时的输入）。
 *
 * 身份纪律（acceptance 1）：objectId 是节点唯一身份（CON-01）——模型
 * 以 ObjectId 建立索引、去重与定位；**节点不携带显示名**（UX-02 零第二
 * 套命名——名称在渲染时刻经 IUiNameResolver 现取）。INV-B1 的类型级
 * 承载：节点必须有有效 ObjectId 才可登记（全零保留值在重建边界整体拒
 * 绝），而运行时结构对象（Frame/Joint/Device/Drawable）没有业务
 * ObjectId——它们在模型上**不可表达**（不是"被过滤"，是类型面无位）。
 */
struct ProjectTreeNode {
    /// 节点业务身份（CON-01——有效值由重建边界强制）。
    core::ObjectId objectId;
    /// 归属分组（五分组封闭词表——决定呈现的组挂位）。
    ProjectTreeGroup group = ProjectTreeGroup::ProjectStructure;
    /// 子对象身份序（同域父子关系；子对象自身也必须是可解析到的节点
    /// ——悬空子引用在重建边界整体拒绝；空＝叶子）。保持 Provider 供给
    /// 序（NFR-COR-02 稳定序）。
    std::vector<core::ObjectId> childObjectIds;
    /// 深度层级提示（0＝组直属顶层；面板按此缩进渲染——模型不建多级
    /// 索引，父子展开由面板按 childObjectIds 组织）。取值上限 8 层
    /// （B1-SPEC §3.1 五分组的自然深度≤5——8 为防御上限，超限重建拒绝）。
    std::uint8_t depth = 0;
};

/**
 * @brief 域树节点供给协议（B1-SPEC §5.1 迁移交付面之一——形状随本任务
 *        冻结，供 WP-13-T20/WP-14-T10/WP-15-T18 三域迁移消费）。
 *
 * 协议纪律（冻结面——三域迁移按此实现，不改签名）：
 *   - domainKey：域注册键（"modeling"/"requirements"/"kinematics"——
 *     与 IUiDomainReadinessSource.domainKey 同一词表口径）；同键重复
 *     注册在登记边界拒绝（装配缺陷 fail-fast）；
 *   - treeNodes()：现取现拼的本域节点集（值拷贝）——模型重建时调用，
 *     每次重建现调（不缓存域侧数据——域修订后 rebuild 即见新内容）；
 *     返回集内的 ObjectId 必须全局唯一（跨 Provider 撞号在重建边界整体
 *     拒绝，见 ProjectTreeModel::rebuild）。
 *
 * 实现方（域迁移任务）义务：只供给**本域**对象（跨域节点由该域自己的
 * Provider 供给）；节点 ObjectId 必须是该域真实业务对象（伪造身份＝
 * 范围越界，验收对抗项）；显示名不在供给面内（渲染时刻经名称端口解析
 * ——协议不携带名称字段，结构上杜绝第二套命名）。
 */
class IUiTreeNodesProvider {
public:
    virtual ~IUiTreeNodesProvider() = default;

    /// @brief 域注册键（"modeling"/"requirements"/"kinematics"——注册
    ///         边界按此查重；返回值约定为常量字面词，跨调用稳定）。
    virtual std::string domainKey() const = 0;

    /**
     * @brief 供给本域树节点集（模型重建时现调——值拷贝，域侧状态自持）。
     *
     * @return 本域节点集（可空＝本域暂无可入树对象——合法常态；空集
     *         不产生组内占位节点）
     *
     * @note UI 线程调用（重建路径）；应快速返回（现取现拼——长查询由
     *       域侧自行缓存，NFR-PERF-01）。
     */
    virtual std::vector<ProjectTreeNode> treeNodes() const = 0;
};

// =====================================================================
// 重建报告（rebuild 的结果值——整体成功或整体拒绝，无半更新）
// =====================================================================

/**
 * @brief 树重建结果（rebuild 的返回值——封闭词表 reason）。
 *
 * 整体性纪律：任一 Provider 数据违约（无效 ObjectId/跨域撞号/悬空子
 * 引用/深度超限）→ 整次重建拒绝，树内容保持重建前状态（导航呈现不允
 * 许半新半旧——拒绝优于残缺），reason 携带封闭词表 token 供 Dev 留痕。
 */
struct TreeRebuildReport {
    /// true＝重建完成（树内容已替换为本轮 Provider 供给）。
    bool ok = false;
    /// 拒绝原因（ok==false 时非空；封闭词表）：
    ///   "invalid-object-id"（无效 ObjectId——全零保留值）；
    ///   "duplicate-object-id"（跨 Provider 撞号——CON-01 唯一性违约）；
    ///   "dangling-child-reference"（子引用指向本域集外/未供给对象）；
    ///   "depth-limit-exceeded"（depth > 8 防御上限）；
    ///   "provider-unregistered"（注册表中 Provider 已析构——生命周期
    ///   违约的防御检出）。
    std::string reason;
    /// 本轮入树节点总数（ok==true 时有效——呈现侧对账/测试断言用）。
    std::size_t nodeCount = 0;
};

// =====================================================================
// 定位结果（L3 反解成功的树定位数据——acceptance 3/4 的落点载体）
// =====================================================================

/**
 * @brief 树内定位结果（locate 的返回值——面板按此展开并高亮）。
 *
 * 行语义：rowInGroup 为该节点在组内的呈现序位（0 起——与 nodesInGroup
 * 序一致）；parentId 为逻辑父（组直属顶层＝nullopt）。面板按
 * (group, rowInGroup) 定位 Qt 行，模型层零 Widget 知识（O-43）。
 */
struct TreeLocation {
    /// 目标节点归属分组。
    ProjectTreeGroup group = ProjectTreeGroup::ProjectStructure;
    /// 组内呈现序位（0 起——nodesInGroup 同序的位次）。
    std::size_t rowInGroup = 0;
    /// 逻辑父身份（组直属顶层＝nullopt——面板按此展开路径）。
    std::optional<core::ObjectId> parentId;
};

// =====================================================================
// ProjectTreeModel——树模型（身份索引＋分组序；零 Widget 零名称缓存）
// =====================================================================

/**
 * @brief 工业项目树模型（五分组封闭清单的内存承载——Provider 驱动）。
 *
 * 状态与不变量（acceptance 1/5 的模型半区）：
 *   - 节点索引唯一键＝ObjectId（CON-01——重建边界强制全局唯一，撞号
 *     整体拒绝）；
 *   - 分组序固定（B1-SPEC §3.1 行序——与 Provider 注册序无关）；组内
 *     节点序＝Provider 注册序→Provider 供给序（NFR-COR-02 稳定）；
 *   - 零名称缓存：模型不存任何显示名（结构承载——节点值无名称字段，
 *     模型 API 无名称出口；渲染名称归面板经 IUiNameResolver 现取）；
 *   - 零运行时结构节点（INV-B1）：无效 ObjectId 在重建边界整体拒绝，
 *     运行时结构对象类型面不可表达（见 ProjectTreeNode 注释）；
 *   - 零选中状态（INV-B3）：模型不持有选中——选中唯一归
 *     SelectionService（本模型无任何选中字段/选中 API，类型面承载）。
 *
 * 错误语义：注册违约（空 Provider/重复 domainKey）＝调用方错误
 *   fail-fast（std::invalid_argument）；数据违约（重建报告四 token）＝
 *   返回值轨整体拒绝（环境轨——域侧数据缺陷是可修复常态，不抛）。
 *
 * 线程约束：仅 UI 线程（§3.4 M-1）。生命周期：shared_ptr（模型与面板
 * 共享——装配层保证 Provider 存活期覆盖注册期）。
 */
class ProjectTreeModel {
public:
    ProjectTreeModel() = default;

    // ---- Provider 注册（装配期静态登记——SA-01 精神，无运行期注销通道）----

    /**
     * @brief 注册一个域节点供给者（装配期一次——重建时现调其供给）。
     *
     * @param provider [in] 供给者（shared_ptr 保活由装配层负责——模型持
     *                 强引用，析构序天然安全）；空指针抛
     *                 std::invalid_argument
     * @throws std::invalid_argument provider 为空，或 domainKey 与已注册
     *         者重复（装配缺陷——同域双供给源的合并语义未定义，禁收）
     */
    void addProvider(std::shared_ptr<IUiTreeNodesProvider> provider);

    /// @brief 已注册供给者数（装配自证/测试观测面）。
    std::size_t providerCount() const noexcept;

    // ---- 重建（唯一内容写点——整体成功或整体拒绝）----

    /**
     * @brief 执行树重建（现调全部 Provider 供给并整体校验后一次替换）。
     *
     * 校验序（任一不过即整体拒绝——保持旧内容，报告 reason）：
     *   ①逐节点 ObjectId 有效性（全零保留值→invalid-object-id）；
     *   ②跨 Provider 全局唯一性（撞号→duplicate-object-id）；
     *   ③子引用闭合（childObjectIds 必须指向本轮供给集内节点→
     *     dangling-child-reference）；
     *   ④深度上限（depth>8→depth-limit-exceeded）。
     *
     * @return 重建报告（ok==false 时树内容不变——拒绝优于残缺）
     */
    TreeRebuildReport rebuild();

    // ---- 查询（呈现与定位的只读面）----

    /// @brief 节点总数（重建成功后的当前内容规模）。
    std::size_t nodeCount() const noexcept;

    /**
     * @brief 组内节点身份序（呈现数据源——渲染时刻面板逐个经名称端口
     *        现取显示名；模型零名称参与）。
     *
     * @param group [in] 目标分组（五分组封闭词表值）
     * @return 该组节点 ObjectId 序（Provider 注册序→供给序——NFR-COR-02；
     *         空组返回空序）
     */
    std::vector<core::ObjectId>
    nodesInGroup(ProjectTreeGroup group) const;

    /**
     * @brief 按身份查节点（L3 定位与消费面查询的寻址原语）。
     *
     * @param id [in] 目标 ObjectId
     * @return 节点值拷贝；不存在（未入树/已重建移除）→ nullopt
     */
    std::optional<ProjectTreeNode> node(const core::ObjectId& id) const;

    /**
     * @brief 树内定位（L3 反解成功的落点数据——acceptance 3/4）。
     *
     * @param id [in] 目标 ObjectId
     * @return 定位结果（分组＋组内序位＋逻辑父）；不存在→nullopt
     *         （调用方〔SelectionService.treeLocator 装配〕以返回值自证
     *         定位是否命中——未命中不伪造选中，见服务 ④ 分支）
     */
    std::optional<TreeLocation> locate(const core::ObjectId& id) const;

private:
    /// 已注册供给者（注册序＝组内贡献序——NFR-COR-02）。
    std::vector<std::shared_ptr<IUiTreeNodesProvider>> m_providers;
    /// 全量节点（ObjectId→节点值——重建整体替换的唯一写点产物）。
    std::vector<ProjectTreeNode> m_nodes;
    /// 分组→组内节点位序（指向 m_nodes 的下标序——呈现序的权威面）。
    std::vector<std::vector<std::size_t>> m_groupRows;
};

// =====================================================================
// 面板装配缝（Qt 渲染层——模型/服务的 Widget 呈现；工厂形态零 Q_OBJECT）
// =====================================================================

class IndustrialProjectTreePanel;  // 前置声明（工厂返回类型——实现封闭在库内，R-2）

/**
 * @brief 面板装配依赖（createIndustrialProjectTreePanel 一次性给出）。
 *
 * 全部必填非空（工厂校验——空依赖的面板是"看不到内容也报不了选中"的
 * 死面板，构造期拒绝优于运行期空转）；nameResolver 用于渲染时刻的显示
 * 名现取（UX-02——解析失败显示占位，不拼接名称）。
 */
struct IndustrialProjectTreePanelDeps {
    /// 树模型（shared 共享——装配层重建后面板刷新即见新内容）。
    std::shared_ptr<ProjectTreeModel> model;
    /// 选择服务（树选择写入的唯一出口——INV-B3 汇聚纪律的装配面）。
    std::shared_ptr<SelectionService> selection;
    /// 名称解析端口（渲染显示名唯一来源——C-11 既有端口零新形状）。
    std::shared_ptr<IUiNameResolver> nameResolver;
};

/**
 * @brief 工业项目树面板把手（内容控件的类型化出口——实现类封闭在库内）。
 *
 * 面板行为（联动 L1/L3 的呈现半区）：
 *   - 渲染：五个固定组行（封闭清单可视不变量——组数恒为 5，INV-B1 的
 *     呈现面断言锚）＋各组内节点行；显示名＝nameResolver 现取（失败→
 *     占位文案，零名称拼接——UX-02/R-4）；模型 rebuild 后调 refresh()
 *     重渲染（不自动监听——重建时机归装配层编排）；
 *   - 选中：用户点击/键盘选择节点行 → selection->selectBusiness
 *     ({id}, ProjectTree)（L1 基线——检查器 UI-T22 订阅服务即得刷新）；
 *     组行不可选中（分组是结构不是对象）；
 *   - L3 落点：locateAndHighlight(id) 供装配层注册为 SelectionService
 *     Deps.treeLocator——按 (group,row) 展开并选中对应行（选中事件经
 *     Qt 信号回流 selectBusiness——单一写入路径，服务侧不重复广播）。
 *
 * 生命周期/所有权：实例 unique_ptr（工厂移交）；widget() 出口的 Qt 控件
 *   所有权随实例（实例析构即控件析构——挂入宿主容器时由调用方保证顺序）。
 */
class IndustrialProjectTreePanel {
public:
    virtual ~IndustrialProjectTreePanel() = default;

    /// @brief 内容控件出口（挂入宿主容器/测试驱动用；实例存活期内有效）。
    virtual QWidget* widget() = 0;

    /**
     * @brief 按模型当前内容全量重渲染（rebuild 后的呈现同步动作）。
     *
     * 全量重建组行/节点行并恢复既有选中高亮（选中对象仍在树中时）——
     * 渲染层零状态假设：内容以模型为唯一事实源（名称也现取）。
     */
    virtual void refresh() = 0;

    /**
     * @brief 定位并高亮指定对象（L3 反解成功的树落点——装配层把本方法
     *        注册为 SelectionService Deps.treeLocator）。
     *
     * 定位路径：model->locate(id) → 展开 (group,row) 对应行 → 以 Qt 选中
     * 信号自然回流 selection->selectBusiness({id}, ProjectTree)（单一写
     * 入路径——本方法不直接调服务，避免双广播）。
     *
     * @param id [in] 目标业务对象 ObjectId
     * @return true＝已定位并选中（树确实呈现了该对象——服务 ④ 分支的
     *         一致性自证值）；false＝树中无该对象（内容漂移——不伪造
     *         选中不动树，Dev 留痕由服务侧完成）
     */
    virtual bool locateAndHighlight(const core::ObjectId& id) = 0;

    /**
     * @brief 设置某分组的装配失败占位呈现（UI-T23——§11.3 失败降级隔离
     *        在多域树形态的呈现半区）。
     *
     * 语义：装配失败的域不注册 Provider（其分组无节点供给），装配层经本
     * 方法在该分组组行下呈现一行"装配失败"占位文案——用户能看到该域
     * 应当存在且当前不可用，其余域照常（隔离不掩盖）。占位行不是树节点：
     * 不携带 ObjectId（kNodeIdRole 等价数据缺省）、不可选中、不进业务
     * 选中——INV-B1/B3 的呈现面边界不受影响（占位是呈现装饰，模型零
     * 污染：ProjectTreeModel 无任何占位知识）。
     *
     * @param group [in] 目标分组（五分组封闭词表值）
     * @param text  [in] 占位文案（UTF-8；空串＝清除该分组占位——域恢复
     *              后由装配层清除）。文案含稳定码文本由调用方组装（本
     *              面板不做诊断语义加工——SA-12 呈现/权威分工）
     */
    virtual void setGroupPlaceholder(ProjectTreeGroup group,
                                     const std::string& text) = 0;
};

/**
 * @brief 构建工业项目树面板（Qt 渲染层——五分组固定组行＋Provider 节点
 *        行；选中经 SelectionService 汇聚；L3 定位回调经面板把手注册）。
 *
 * @param deps    [in] 装配依赖（model/selection/nameResolver 必填非空，
 *                缺失抛 std::invalid_argument）
 * @param parent  [in] Qt 父控件（可空——widget() 出口所有权随实例；
 *                挂入宿主容器时把 widget() 重挂父子即可）
 * @return 面板把手（unique_ptr——实现类封闭在库内，R-2 同工厂惯例；
 *         零 Q_OBJECT——信号接线全经 lambda，AUTOMOC 口径不变）
 *
 * @throws std::invalid_argument deps 任一成员为空
 */
std::unique_ptr<IndustrialProjectTreePanel> createIndustrialProjectTreePanel(
    const IndustrialProjectTreePanelDeps& deps, QWidget* parent);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_INDUSTRIALPROJECTTREE_HPP
