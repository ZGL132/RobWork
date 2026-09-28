/**
 * @file   HostMigrationProviders.hpp
 * @brief  需求域宿主迁移三接入面（方案 B.1／SA-18 D11）——TreeNodesProvider
 *         （需求对象入工业项目树）＋PropertyPagesProvider（常用字段入共享
 *         检查器；区域定义/CSV·JSON 导入向导等复杂编辑收口域面板——D6
 *         分野）＋SelectionAdapter（选择联动：树选→域面板高亮；本域三维
 *         拾取→选择服务上报）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/WP-14-T10.json acceptance 1~4（三接入面
 *     落位、迁移链路端到端、Provider 增量面——无整体重写、共享 UI 装配面
 *     零改动——只消费 UI-T21/T22 冻结的注册协议）；
 *   - B1-SPEC §5.1（迁移交付面三接入面的职责原文）、§5.2（迁移期双形态
 *     并存——本域自持导航 deprecated 标记保留可用）、§5.3（共享 UI 文件
 *     互斥——本文件全部位于 requirements/plugin/ 本域目录，零共享面触碰）；
 *   - ui 注册协议（UI-T21/T22 冻结形状——只消费不改签名）：
 *     IndustrialProjectTree.hpp（IUiTreeNodesProvider/ProjectTreeNode/
 *     ProjectTreeGroup::RequirementObjects）、PropertyInspector.hpp
 *     （IUiPropertyPagesProvider/CommonFieldsPage/ComplexPageEntry/
 *     ComplexPageActivationReport/kMaxCommonFieldsPerObject 哨兵）、
 *     SelectionService.hpp（IUiSelectionObserver/SelectionChange/
 *     SelectionSource::View3DPick）、FormEditCommon.hpp（QuantityFieldSpec/
 *     IFormEditOutlet/ParamEditSet——常用字段页的编辑移交面）；
 *   - units/requirements.md §9.8（面板信息架构——本域复杂编辑页的落点、
 *     L-R1/L-R2 选中与编辑既有数据面——本文件复用不重写）；
 *   - modeling 先例：WP-13-T20 modeling/plugin/HostMigrationProviders（同
 *     型迁移第一棒——本文件为其 requirements 侧同构实现，协议消费面与
 *     装配形态逐点对齐，域语义替换）；
 *   - knownPitfalls：P-REQ-4（IModuleDraftSource 签名漂移防护——本文件不
 *     触草稿源面；协议头发现不兼容按增量同步登记单元卡，不私改对端）；
 *     O-43（宿主融合边界——本文件不触碰 Dock 拓扑/宿主装配层，宿主挂位
 *     归 UI-T23/WP-24-T08）。
 *
 * 背景说明（为什么三接入面都是"现取现拼"的薄适配）：
 *   方案 B.1 的渐进迁移（D11）要求域侧以 Provider 值供给共享呈现面，而
 *   ui 对业务域零编译依赖（R-1）——共享模型只认 ui 自有协议值，域对象
 *   的投影与判定全部留在域内。本文件因此是"协议值 ← 编辑器工作集投影"
 *   的搬运层：树节点从编辑器工作集现取（与方案 A 自持树同一份工作集，
 *   零第二套树语义），常用字段/复杂页入口从同一工作集解析；编辑提交复用
 *   PanelEditFlow::submitEntryEdit（域裁决唯一）。任何"域判定入检查器/
 *   树"的形态都由协议形状阻止（协议值不带名称、不带字段集——见对端头注）。
 *
 * 增量面声明（D11——acceptance 3 的边界，供验收对账）：
 *   - 树接入面覆盖需求全域对象：需求集根＋任务点＋区域＋工况＋采样计划
 *     （acceptance 1 词表全集——四集合容器对象是闭包结构载体，不属呈现
 *     词表，不入共享树；四分组折叠由共享树组行承载）；
 *   - 页面接入面 v1 覆盖 D6 分野点名的对象：任务点（常用字段＝受约束
 *     位置三分量＋双容差；复杂页＝任务点编辑——位姿规则/三段/要求值等
 *     大批量字段收口域面板）、区域（复杂页＝区域定义——D6 点名收口页）、
 *     需求集根（复杂页＝CSV/JSON 导入向导——D6 点名收口页，根级操作）；
 *     工况/计划提供复杂页入口（域面板既有编辑页面的定位入口），其常用
 *     字段随域编辑任务扩展（本任务不虚构字段——诚实边界，登记单元卡
 *     §14.6）；
 *   - 方案 A 四区面板零删除零重写：仅新增 deprecated 导航标记（B1-SPEC
 *     §5.2"标记 deprecated 保留可用"）与 focusObject 定位入口（适配器
 *     高亮/复杂页激活的执行器）。
 *
 * 线程约束：全部类仅 UI 线程构造与访问（§3.4——编辑器工作集/选中态是
 *   会话对象；与 PanelTreeModel/PanelSelection 同口径）。非线程安全。
 *
 * 生命周期/所有权：三个 Provider/Adapter 由装配层（RequirementsUiModule
 *   门面或测试）以 shared_ptr/unique_ptr 持有并注册进共享模型（模型持强
 *   引用）；Deps 内全部回调/裸指针为非 owning——调用方保证存活期覆盖
 *   注册期（对端头注同款惯例）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_HOSTMIGRATIONPROVIDERS_HPP
#define IRD_REQUIREMENTS_PLUGIN_HOSTMIGRATIONPROVIDERS_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>          // core::IEventSubscription（订阅 RAII 句柄——⑤事件端口）
#include <sdurws/ird/core/Identity.hpp>        // core::ObjectId（节点/选中身份——CON-01）
#include <sdurws/ird/requirements/Editor.hpp>  // IRequirementEditor/RequirementWorkingSet（现取数据源）
#include "PanelEditFlow.hpp"                   // IRequirementEditSink/EditRejection（编辑分流出口复用）
#include <sdurws/ird/ui/FormEditCommon.hpp>    // ui::IFormEditOutlet/ParamEditSet（编辑移交面）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>  // ui::IUiTreeNodesProvider/ProjectTreeNode（树协议）
#include <sdurws/ird/ui/PropertyInspector.hpp>      // ui::IUiPropertyPagesProvider 及页面值（页协议）
#include <sdurws/ird/ui/SelectionService.hpp>       // ui::IUiSelectionObserver/SelectionService（选择协议）

class QWidget;  // 前置声明：activateComplexPage 宿装父控件（头文件不拖入 Widgets）

namespace sdurws {
namespace ird {
namespace requirements {

// =====================================================================
// 装配依赖（三接入面共用的会话现取面——装配层/测试一次性给出）
// =====================================================================

/**
 * @brief 三接入面的装配依赖（非 owning——调用方保证存活期覆盖注册期）。
 *
 * editor 必填（会话权威——工作集唯一载体的现取入口；生产装配由
 * RequirementsUiModule 绑定 attachEditor 注入的编辑器指针，测试自持
 * 编辑器）；其余执行器可空＝对应能力缺失的显式声明（可空性语义逐字段
 * 注明——UI-T11"可空注入显式声明"先例），运行期缺失走诚实拒绝/降级，
 * 不虚构。rootObjectId 现取入口必填——根身份是会话态权威
 * （RequirementsModuleSessionState.rootObjectId，修订闭包内 req-set 的
 * oid；nullopt＝首次应用前，条目以组直属顶层供给）。
 */
struct RequirementsSharedSurfaceDeps {
    /// 编辑器现取入口（必填；返回 nullptr＝无会话——树供空集、页面应答
    /// nullopt/空集）。工作集读取恒经 editor->workingSet()（PA-1：编辑器
    /// 是工作集唯一权威——本文件零工作集副本）。
    std::function<IRequirementEditor*()> editor;
    /// 需求集根身份现取入口（必填——树根节点与根级页面应答的寻址依据；
    /// nullopt＝根未取号〔首应用前〕，此时树只供条目）。
    std::function<std::optional<core::ObjectId>()> rootObjectId;
    /// 编辑分流出口（L-R2 三路回调——常用字段页编辑提交的落点；生产装配
    /// 绑定需求面板〔RequirementsPanelWidget 即 IRequirementEditSink〕，
    /// 测试供记录替身；空＝编辑提交无出口——出口构造为 nullptr 的纯呈现
    /// 页）。
    IRequirementEditSink* editSink = nullptr;
    /// 域面板高亮/定位执行器（SelectionAdapter 消费选中事件后的呈现动作
    /// ——"树选→面板高亮"半区；生产绑定 RequirementsPanelWidget::
    /// focusObject；nullopt 入参＝清除高亮〔多选/清空选中〕。空＝高亮
    /// 动作静默跳过）。
    std::function<void(const std::optional<core::ObjectId>&)> panelHighlight;
    /// D6 复杂编辑页"域自持打开"执行器（hosted=false 页的激活动作——
    /// 打开/聚焦域面板内编辑视图；生产绑定 focusObject 定位到目标对象。
    /// 空＝激活诚实拒绝〔reason "domain-surface-unavailable"〕，不伪造
    /// 打开成功）。
    std::function<void(const core::ObjectId&)> complexPageActivator;
};

// =====================================================================
// TreeNodesProvider——需求对象入工业项目树（B1-SPEC §5.1 接入面一）
// =====================================================================

/**
 * @brief 需求域树节点供给者（ui::IUiTreeNodesProvider 实现——协议形状
 *        UI-T21 冻结，零签名改动）。
 *
 * 供给语义（acceptance 1）：
 *   - domainKey()＝"requirements"（三协议统一域键词表——注册边界查重用）；
 *   - treeNodes() 现取现拼：需求集根（rootObjectId 已回填时）＋任务点
 *     ＋区域＋工况＋采样计划条目——全部挂 ProjectTreeGroup::
 *     RequirementObjects 分组；根节点 childObjectIds 按工作集序
 *     （点→区域→工况→计划，集合内 ObjectId 字典序——I-REQ-1）携带全部
 *     条目（子引用闭合：条目全部在供给集内——重建边界 dangling 检查恒过；
 *     四集合容器对象不在词表，不入树）；
 *   - 节点零显示名（协议结构面无名称字段——UX-02 零第二套命名；显示名
 *     归树面板渲染时刻经 IUiNameResolver 现取）；
 *   - 条目序＝工作集既有序（编辑器载入即规范化——单一排序权威，
 *     NFR-COR-02 稳定序）。
 *
 * 会话边界：无会话（editor 返回 nullptr）＝空集（协议注释的合法常态——
 *   空集不产生组内占位节点）；根身份未回填（rootObjectId 返回 nullopt）
 *   ＝条目以组直属顶层（depth 0）供给——对象锚在会话内仍可用
 *   （PanelTreeModel 同款"临时句柄不外泄"口径）。
 */
class RequirementsTreeNodesProvider final : public ui::IUiTreeNodesProvider {
public:
    /**
     * @brief 构造树节点供给者。
     *
     * @param deps [in] 装配依赖（editor/rootObjectId 必填——空 function
     *             属装配缺陷，抛 std::invalid_argument fail-fast）
     *
     * @throws std::invalid_argument deps.editor 或 deps.rootObjectId 为空
     */
    explicit RequirementsTreeNodesProvider(RequirementsSharedSurfaceDeps deps);

    /// @brief 域注册键（"requirements"——三协议统一词表，常量字面跨调用稳定）。
    std::string domainKey() const override;

    /**
     * @brief 供给本域树节点集（模型重建时现调——值拷贝，零缓存）。
     *
     * @return 节点集（根在首位〔有根身份时〕＋四集合条目按工作集序；
     *         全部 RequirementObjects 分组；子引用闭合——见类注）
     */
    std::vector<ui::ProjectTreeNode> treeNodes() const override;

private:
    RequirementsSharedSurfaceDeps m_deps;  ///< 装配依赖（非 owning——见结构注）
};

// =====================================================================
// PropertyPagesProvider——常用字段入共享检查器＋复杂编辑页（接入面二）
// =====================================================================

/**
 * @brief 需求域属性页供给者（ui::IUiPropertyPagesProvider 实现——协议
 *        形状 UI-T22 冻结，零签名改动；D5/D6 分野的需求侧落位）。
 *
 * 页面供给语义（acceptance 1 的 D5/D6 分野）：
 *   - 任务点（TaskPoint）：常用字段页＝受约束位置三分量（position 已
 *     提供时；未提供＝不供位置字段，页面以双容差字段保底非空——"未提供
 *     该分量值"是合法态，MDL-06 缺失不转零，检查器呈现占位不伪造 0）
 *     ＋位置/姿态双容差（少量高频编辑字段——可编辑，编辑出口转译为域
 *     编辑流）；复杂页入口＝"任务点编辑"（point-editor——位姿规则/
 *     三段/要求值等大批量字段收口域面板，D6）；
 *   - 区域（WorkRegion）：无常用字段页（v1——区域定义是大批量采样/覆盖
 *     率编辑，收口域面板）；复杂页入口＝"区域定义"（region-definition
 *     ——契约 acceptance 1 D6 点名收口页）；
 *   - 需求集根（RequirementSet）：无常用字段页；复杂页入口＝"CSV/JSON
 *     导入向导"（import-wizard——契约 acceptance 1 D6 点名收口页，根级
 *     操作入口，打开域面板向导流〔L-R10〕）；
 *   - 工况（OperatingCondition）/采样计划（SamplingPlan）：无常用字段页
 *     （v1——不虚构字段，登记单元卡 §14.6）；复杂页入口＝"工况编辑"/
 *     "采样计划编辑"（condition-editor/plan-editor——域面板既有编辑页
 *     的定位入口，与任务点同型）。
 *
 * 域判定在域（协议纪律）：对象归属由编辑器工作集自答——命中本域闭包
 *   （根/四集合条目）才应答，检查器零需求类型知识。
 *
 * 编辑移交面（D5 出口）：任务点页携带 PointCommonFieldsOutlet（本类内聚
 *   的 ui::IFormEditOutlet 实现）——把 ParamEditSet 按键转译为
 *   PanelEditFlow::submitEntryEdit 域编辑流（域裁决唯一；接受/拒绝分流
 *   经 Deps.editSink 呈现）。出口绑定"供给时刻"的选中任务点（检查器呈现
 *   期即该目标——选中变更即重供给，旧页随视图替换失效，出口无跨目标
 *   悬存语义，注记于出口类）。
 */
class RequirementsPropertyPagesProvider final : public ui::IUiPropertyPagesProvider {
public:
    /// 复杂页稳定键（D6 激活寻址锚——小写连字符词法，协议建议风格）。
    static constexpr const char* kPointEditorPageKey = "point-editor";
    static constexpr const char* kRegionDefinitionPageKey = "region-definition";
    static constexpr const char* kConditionEditorPageKey = "condition-editor";
    static constexpr const char* kPlanEditorPageKey = "plan-editor";
    static constexpr const char* kImportWizardPageKey = "import-wizard";

    /**
     * @brief 构造属性页供给者。
     *
     * @param deps [in] 装配依赖（editor/rootObjectId 必填——空 function
     *             抛 std::invalid_argument；editSink 可空——空时任务点页
     *             退化为纯呈现〔editOutlet=nullptr，apply 禁用〕）
     *
     * @throws std::invalid_argument deps.editor 或 deps.rootObjectId 为空
     */
    explicit RequirementsPropertyPagesProvider(RequirementsSharedSurfaceDeps deps);

    /// @brief 域注册键（"requirements"——三协议统一词表）。
    std::string domainKey() const override;

    /**
     * @brief 供给选中对象的常用字段页（D5——现取现拼，值拷贝）。
     *
     * @param object [in] 当前选中对象身份
     * @return 任务点（位置已提供）＝五字段页／任务点（位置未提供）＝双
     *         容差页（完整性三查由检查器复核——本供给面保证非空、不超
     *         kMaxCommonFieldsPerObject 哨兵、基线键闭合）；其余本域对象
     *         ＝nullopt（v1 无常用字段——诚实边界）；非本域对象＝nullopt
     *         （合法二态——协议注释原文）
     */
    std::optional<ui::CommonFieldsPage>
    commonFieldsPage(const core::ObjectId& object) const override;

    /**
     * @brief 声明选中对象的复杂编辑页入口集（D6——零字段内容）。
     *
     * @param object [in] 当前选中对象身份
     * @return 任务点＝[{point-editor}]；区域＝[{region-definition}]；
     *         工况＝[{condition-editor}]；计划＝[{plan-editor}]；根＝
     *         [{import-wizard}]；其余＝空集（全部 hosted=false——D6
     *         "域面板内"形态）
     */
    std::vector<ui::ComplexPageEntry>
    complexPageEntries(const core::ObjectId& object) const override;

    /**
     * @brief 激活复杂编辑页（D6 编排出口——hosted=false 域自持打开形态）。
     *
     * 编排序：①解析目标（闭包外对象或未知 pageKey→"activation-unknown-
     * page" 拒绝）；②Deps.complexPageActivator 缺失→"domain-surface-
     * unavailable" 诚实拒绝（不伪造打开）；③执行器打开/聚焦域面板内
     * 编辑视图（hosted=false 语义——打开动作在本调用内完成），返回
     * {ok=true, hostedWidget=nullptr}（自持页恒空——协议一致性由检查器
     * 核对）。
     *
     * @param object  [in] 目标对象身份
     * @param pageKey [in] 目标页稳定键
     * @param parent  [in] 宿装父控件（自持页忽略——签名兼容保留）
     * @return 激活报告（封闭词表 reason——见类注编排序）
     */
    ui::ComplexPageActivationReport
    activateComplexPage(const core::ObjectId& object, const std::string& pageKey,
                        QWidget* parent) override;

private:
    /**
     * @brief 任务点常用字段页的编辑移交出口（ui::IFormEditOutlet 实现）。
     *
     * 转译规则（键→域编辑流——域裁决唯一在编辑器校验链）：
     *   - "point-x"/"point-y"/"point-z"→读当前任务点：position 已提供＝
     *     替换单分量后整体提交（upsert——编辑器按 objectId 替换）；
     *     position 未提供＝出口拒绝 "position-not-provided"（不虚构另外
     *     两分量——完整位置在域面板定义，与"缺失不转零"同源）；
     *   - "position-tolerance"/"orientation-tolerance"→单值替换后整体
     *     提交（>0 且有限的域裁决在编辑器校验链——I-REQ-5）；
     *   - 未知键→出口拒绝 "unknown-field"（防御——检查器表单只会回传
     *     本页字段的键）。
     *
     * 绑定语义：bindTarget 在每次任务点页供给时刷新为该页的任务点下标
     * ——出口提交恒作用于"当前呈现页"的目标（见外层类注）；域拒绝经
     * Deps.editSink.onEditRejected 就地呈现（非模态——UX-03/07）。
     *
     * 线程约束：仅 UI 线程（applyEdits 由检查器呈现路径调用——§3.4）。
     */
    class PointCommonFieldsOutlet final : public ui::IFormEditOutlet {
    public:
        PointCommonFieldsOutlet(const RequirementsSharedSurfaceDeps& deps,
                                const RequirementsPropertyPagesProvider& owner);

        /// @brief 绑定当前呈现页的目标任务点下标（页供给时刷新——见类注）。
        void bindTarget(std::size_t pointIndex) { m_pointIndex = pointIndex; }

        /// @brief 接收确认的修改集并转译为域编辑流（键→域入口见类注）。
        void applyEdits(const ui::ParamEditSet& editSet) override;

    private:
        /// 单键提交（位置分量/双容差——返回是否提交成功；域裁决分流由
        /// submitEntryEdit 经 sink 完成，本返回值仅表单侧对账用）。
        bool submitKey(const std::string& key, double newSi);

        RequirementsSharedSurfaceDeps m_deps;  ///< 会话现取＋分流出口（非 owning）
        const RequirementsPropertyPagesProvider& m_owner;  ///< 域键等常量（诊断用）
        std::size_t m_pointIndex = 0;  ///< 当前绑定任务点下标（页供给时刷新）
    };

    RequirementsSharedSurfaceDeps m_deps;       ///< 装配依赖（非 owning——见结构注）
    mutable PointCommonFieldsOutlet m_outlet;   ///< 任务点页编辑出口（供给方持有
                                                ///< ——页内裸指针的存活担保者；
                                                ///< mutable＝const 供给路径绑定目标）
};

// =====================================================================
// SelectionAdapter——选择联动（B1-SPEC §5.1 接入面三）
// =====================================================================

/**
 * @brief 需求域选择适配器（ui::IUiSelectionObserver 实现——选择服务
 *        UI-T21 冻结协议的消费侧；B1-SPEC §5.1"消费 SelectionService
 *        事件，驱动本域面板高亮/过滤；向选择服务上报本域三维拾取结果"
 *        的需求落位）。
 *
 * 联动语义（acceptance 1/2 的选择半区）：
 *   - 下行（树选→面板高亮）：onSelectionChanged——业务单选且命中需求
 *     闭包（根/四集合条目命中）→Deps.panelHighlight(oid) 驱动域面板
 *     定位/高亮；多选/清空→panelHighlight(nullopt) 对称清除；runtimeOnly
 *     变更（L3 反解失败暂态）→零触碰（业务选中未变——SelectionChange
 *     注释的两态语义，不误清既有高亮）；
 *   - 上行（本域三维拾取→选择服务上报）：reportView3DPick——仅上报
 *     需求闭包内对象（域外/无效身份拒绝上报＝false，零伪造——协议
 *     selectBusiness 的无效身份契约会 fail-fast，域侧先过滤）；以
 *     SelectionSource::View3DPick 汇入唯一写入口（L2 高亮判定随服务
 *     编排——本类不做正向存在性判定，那是 nameMap 端口的事）。
 *
 * 订阅生命周期：attach(service) 订阅（RAII 句柄由本类持有）；detach()
 * 显式退订；析构自动退订——**service 必须晚于本对象析构**（句柄退订
 * 触及服务内部——装配层持有序：适配器先亡，服务后亡；生产装配与测试
 * 均按此序，类注即契约）。
 */
class RequirementsSelectionAdapter final : public ui::IUiSelectionObserver {
public:
    /**
     * @brief 构造选择适配器。
     *
     * @param deps [in] 装配依赖（editor/rootObjectId 必填——空 function
     *             抛 std::invalid_argument；panelHighlight 可空——空高亮
     *             执行器＝联动呈现静默跳过，事件消费照常）
     *
     * @throws std::invalid_argument deps.editor 或 deps.rootObjectId 为空
     */
    explicit RequirementsSelectionAdapter(RequirementsSharedSurfaceDeps deps);

    /// 不可拷贝/不可移动（订阅句柄绑定生命周期——RAII 语义）。
    RequirementsSelectionAdapter(const RequirementsSelectionAdapter&) = delete;
    RequirementsSelectionAdapter& operator=(const RequirementsSelectionAdapter&) = delete;

    /**
     * @brief 订阅选择服务（装配期一次；重复 attach 先退订再订阅——
     *        幂等收口）。
     *
     * @param service [in] 选择服务（非 owning——存活期须覆盖订阅期，
     *                且晚于本对象析构，见类注）
     *
     * @throws std::invalid_argument service 为空指针（解引用前校验）
     */
    void attach(ui::SelectionService& service);

    /// @brief 显式退订（幂等——未订阅时空操作；析构路径复用）。
    void detach() noexcept;

    /// @brief 选择已变更（下行联动——语义见类注；UI 线程同步回调）。
    void onSelectionChanged(const ui::SelectionChange& change) override;

    /**
     * @brief 上报一次本域三维拾取（上行——View3DPick 来源汇入选择服务
     *        唯一写入口）。
     *
     * @param oid [in] 拾取命中的对象身份（须为需求闭包内有效对象——
     *            域外/无效身份拒绝上报并返回 false，不出诊断——拾取
     *            未命中本域对象是常态而非错误）
     * @return true＝已上报（服务已广播 View3DPick 选中）；false＝未
     *         上报（未 attach／无效身份／闭包外对象）
     */
    bool reportView3DPick(const core::ObjectId& oid);

private:
    RequirementsSharedSurfaceDeps m_deps;       ///< 装配依赖（非 owning——见结构注）
    ui::SelectionService* m_service = nullptr;  ///< 已订阅服务（attach/detach 管理）
    std::unique_ptr<core::IEventSubscription> m_subscription;  ///< RAII 订阅句柄
};

}  // namespace requirements
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_REQUIREMENTS_PLUGIN_HOSTMIGRATIONPROVIDERS_HPP
