/**
 * @file   HostMigrationProviders.hpp
 * @brief  建模域宿主迁移三接入面（方案 B.1／SA-18 D11）——TreeNodesProvider
 *         （建模对象入工业项目树）＋PropertyPagesProvider（常用字段入共享
 *         检查器；DH 参数/物性编辑入复杂编辑页——D5/D6 分野）＋
 *         SelectionAdapter（选择联动：树选→域面板高亮；本域三维拾取→选择
 *         服务上报）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/WP-13-T20.json acceptance 1~4（三接入面
 *     落位、迁移链路端到端、Provider 增量面——无整体重写、共享 UI 装配面
 *     零改动——只消费 UI-T21/T22 冻结的注册协议）；
 *   - B1-SPEC §5.1（迁移交付面三接入面的职责原文）、§5.2（迁移期双形态
 *     并存——本域自持导航 deprecated 标记保留可用）、§5.3（共享 UI 文件
 *     互斥——本文件全部位于 modeling/plugin/ 本域目录，零共享面触碰）；
 *   - ui 注册协议（UI-T21/T22 冻结形状——只消费不改签名）：
 *     IndustrialProjectTree.hpp（IUiTreeNodesProvider/ProjectTreeNode/
 *     ProjectTreeGroup::ModelingObjects）、PropertyInspector.hpp
 *     （IUiPropertyPagesProvider/CommonFieldsPage/ComplexPageEntry/
 *     ComplexPageActivationReport/kMaxCommonFieldsPerObject 哨兵）、
 *     SelectionService.hpp（IUiSelectionObserver/SelectionChange/
 *     SelectionSource::View3DPick）、FormEditCommon.hpp（QuantityFieldSpec/
 *     IFormEditOutlet/ParamEditSet——常用字段页的编辑移交面）；
 *   - units/modeling.md §9.7（域面板信息架构——本域复杂编辑页的落点）、
 *     §9.7.2 L-1/L-2（选中联动与编辑流的既有数据面——本文件复用不重写）；
 *   - knownPitfalls：P-MDL-8（对端协议漂移防护——本文件只消费 UI-T21/T22
 *     已冻结的公共头形状，发现不兼容按增量同步登记单元卡，不私改对端）；
 *     O-43（宿主融合边界——本文件不触碰 Dock 拓扑/宿主装配层，宿主挂位
 *     归 UI-T23/WP-24-T08）。
 *
 * 背景说明（为什么三接入面都是"现取现拼"的薄适配）：
 *   方案 B.1 的渐进迁移（D11）要求域侧以 Provider 值供给共享呈现面，而
 *   ui 对业务域零编译依赖（R-1）——共享模型只认 ui 自有协议值，域对象
 *   的投影与判定全部留在域内。本文件因此是"协议值 ← 工作集投影"的搬运
 *   层：树节点从 PanelModel::buildStructureTree 既有投影取（与方案 A
 *   面板同一份信息架构——零第二套树语义），常用字段/复杂页入口从
 *   resolveSelection/propertyFieldsFor 同源解析；编辑提交复用
 *   PanelEditFlow::submitJointFieldEdit（域裁决唯一）。任何"域判定入
 *   检查器/树"的形态都由协议形状阻止（协议值不带名称、不带字段集——
 *   见对端头注）。
 *
 * 增量面声明（D11——acceptance 3 的边界，供验收对账）：
 *   - 树接入面覆盖建模全域对象：模型根＋关节链＋连杆链＋工具/场景/命名
 *     位姿/传动（acceptance 1 词表全集）；
 *   - 页面接入面 v1 覆盖 D6 分野点名的两类对象：关节（常用字段＝零位/
 *     限位；复杂页＝DH 参数——axis/origin 等大批量字段收口在域面板）、
 *     连杆（常用字段＝质量只读事实；复杂页＝物性编辑——质心/惯量的
 *     L-8 平行轴确认流收口在域面板）；工具/场景/位姿集/传动的检查器页
 *     面随其域编辑任务扩展（本任务不虚构字段——诚实边界，登记单元卡
 *     §14.6）；
 *   - 方案 A 五区面板零删除零重写：仅新增 deprecated 导航标记（B1-SPEC
 *     §5.2"标记 deprecated 保留可用"）与 focusObject 定位入口（适配器
 *     高亮/复杂页激活的执行器）。
 *
 * 线程约束：全部类仅 UI 线程构造与访问（§3.4——工作集/选中态是会话
 *   对象；与 PanelModel/PanelSelection 同口径）。非线程安全。
 *
 * 生命周期/所有权：三个 Provider/Adapter 由装配层（ModelingUiModule/
 *   ModelingPluginAssembly 门面或测试）以 shared_ptr 持有并注册进共享
 *   模型（模型持强引用）；Deps 内全部回调/裸指针为非 owning——调用方
 *   保证存活期覆盖注册期（对端头注同款惯例）。
 */

#ifndef IRD_MODELING_PLUGIN_HOSTMIGRATIONPROVIDERS_HPP
#define IRD_MODELING_PLUGIN_HOSTMIGRATIONPROVIDERS_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId（节点/选中身份——CON-01）
#include <sdurws/ird/modeling/Template.hpp>   // modeling::ModelingWorkingSet（现取数据源）
#include "PanelEditFlow.hpp"                  // IPanelEditFlow/EditRejection（编辑分流出口复用）
#include "PanelModel.hpp"                     // buildStructureTree/resolveSelection（同源投影复用）
#include <sdurws/ird/ui/FormEditCommon.hpp>   // ui::IFormEditOutlet/ParamEditSet（编辑移交面）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>  // ui::IUiTreeNodesProvider/ProjectTreeNode（树协议）
#include <sdurws/ird/ui/PropertyInspector.hpp>      // ui::IUiPropertyPagesProvider 及页面值（页协议）
#include <sdurws/ird/ui/SelectionService.hpp>       // ui::IUiSelectionObserver/SelectionService（选择协议）

namespace sdurws {
namespace ird {
namespace modeling {

// =====================================================================
// 装配依赖（三接入面共用的会话现取面——装配层/测试一次性给出）
// =====================================================================

/**
 * @brief 三接入面的装配依赖（非 owning——调用方保证存活期覆盖注册期）。
 *
 * workingSet 必填（现取权威工作集——三接入面的唯一数据源；生产装配由
 * ModelingUiModule 绑定 ModuleSessionState.draft，测试自持工作集）；
 * 其余执行器可空＝对应能力缺失的显式声明（可空性语义逐字段注明——
 * UI-T11"可空注入显式声明"先例），运行期缺失走诚实拒绝/降级，不虚构。
 */
struct ModelingSharedSurfaceDeps {
    /// 权威工作集现取入口（必填；可变指针——编辑出口复用同一入口，与
    /// 面板 EditTargetProvider 同款形态；供给面只读消费，出口提交可变——
    /// 零缓存现取，ACC5 纪律；返回 nullptr＝无会话——树供空集、页面应答
    /// nullopt）。
    std::function<ModelingWorkingSet*()> workingSet;
    /// 编辑分流出口（L-2 三路回调——常用字段页编辑提交的落点；生产装配
    /// 绑定建模面板〔ModelingPanelWidget 即 IPanelEditSink〕，测试供记录
    /// 替身；空＝编辑提交无出口——出口构造为 nullptr 的纯呈现页）。
    IPanelEditSink* editSink = nullptr;
    /// 域面板高亮/定位执行器（SelectionAdapter 消费选中事件后的呈现动作
    /// ——"树选→面板高亮"半区；生产绑定 ModelingPanelWidget::focusObject；
    /// nullopt 入参＝清除高亮〔多选/清空选中〕。空＝高亮动作静默跳过）。
    std::function<void(const std::optional<core::ObjectId>&)> panelHighlight;
    /// D6 复杂编辑页"域自持打开"执行器（hosted=false 页的激活动作——
    /// 打开/聚焦域面板内编辑视图；生产绑定 focusObject 定位到目标对象。
    /// 空＝激活诚实拒绝〔reason "domain-surface-unavailable"〕，不伪造
    /// 打开成功）。
    std::function<void(const core::ObjectId&)> complexPageActivator;
};

// =====================================================================
// TreeNodesProvider——建模对象入工业项目树（B1-SPEC §5.1 接入面一）
// =====================================================================

/**
 * @brief 建模域树节点供给者（ui::IUiTreeNodesProvider 实现——协议形状
 *        UI-T21 冻结，零签名改动）。
 *
 * 供给语义（acceptance 1）：
 *   - domainKey()＝"modeling"（三协议统一域键词表——注册边界查重用）；
 *   - treeNodes() 现取现拼：模型根（rootObjectId 已回填时）＋关节链
 *     （串联序）＋连杆链（串联序）＋工具/场景/命名位姿/传动对象——全部
 *     挂 ProjectTreeGroup::ModelingObjects 分组；根节点 childObjectIds
 *     按投影序携带全部子对象（悬空子引用由重建边界整体拒绝——本供给面
 *     保证子引用闭合：子对象全部在供给集内）；
 *   - 节点零显示名（协议结构面无名称字段——UX-02 零第二套命名；显示名
 *     归树面板渲染时刻经 IUiNameResolver 现取）；
 *   - 节点序＝PanelModel::buildStructureTree 投影序（与方案 A 自持树同
 *     一份信息架构——零第二套树语义，NFR-COR-02 稳定序）。
 *
 * 会话边界：无会话（workingSet 返回 nullptr）＝空集（协议注释的合法
 *   常态——空集不产生组内占位节点）；根身份未回填（模板初始草稿
 *   rootObjectId=nullopt）＝子对象以组直属顶层（depth 0）供给——对象
 *   锚在会话内仍可用（PanelModel 同款"临时句柄不外泄"口径）。
 */
class ModelingTreeNodesProvider final : public ui::IUiTreeNodesProvider {
public:
    /**
     * @brief 构造树节点供给者。
     *
     * @param deps [in] 装配依赖（workingSet 必填——空 function 属装配
     *             缺陷，抛 std::invalid_argument fail-fast）
     *
     * @throws std::invalid_argument deps.workingSet 为空
     */
    explicit ModelingTreeNodesProvider(ModelingSharedSurfaceDeps deps);

    /// @brief 域注册键（"modeling"——三协议统一词表，常量字面跨调用稳定）。
    std::string domainKey() const override;

    /**
     * @brief 供给本域树节点集（模型重建时现调——值拷贝，零缓存）。
     *
     * @return 节点集（根在首位〔有根身份时〕＋子对象按投影序；全部
     *         ModelingObjects 分组；子引用闭合——见类注）
     */
    std::vector<ui::ProjectTreeNode> treeNodes() const override;

private:
    ModelingSharedSurfaceDeps m_deps;  ///< 装配依赖（非 owning——见结构注）
};

// =====================================================================
// PropertyPagesProvider——常用字段入共享检查器＋复杂编辑页（接入面二）
// =====================================================================

/**
 * @brief 建模域属性页供给者（ui::IUiPropertyPagesProvider 实现——协议
 *        形状 UI-T22 冻结，零签名改动；D5/D6 分野的建模侧落位）。
 *
 * 页面供给语义（acceptance 1 的 D5/D6 分野）：
 *   - 关节（Joint）：常用字段页＝零位偏置＋限位下/上限（少量高频编辑
 *     字段——可编辑，编辑出口转译为域编辑流）；复杂页入口＝"DH 参数"
 *     （dh-parameters——axis/origin/DH 大批量字段收口域面板，D6）；
 *   - 连杆（Link）：常用字段页＝质量（只读事实——含来源徽标语义的
 *     只读呈现；物性编辑需 L-8 平行轴确认流，不在共享检查器展开）；
 *     复杂页入口＝"物性编辑"（properties——质心/惯量编辑收口域面板，
 *     D6）；
 *   - 其余建模对象（根/基座/工具/场景/位姿集/传动）：v1 无页面供给
 *     （commonFieldsPage 返回 nullopt 且 complexPageEntries 空集——协议
 *     的"非本域应答"二态；诚实边界：不虚构字段，登记单元卡 §14.6）。
 *
 * 域判定在域（协议纪律）：对象归属由 resolveSelection 既有投影自答——
 *   命中关节/连杆才应答，检查器零建模类型知识。
 *
 * 编辑移交面（D5 出口）：关节页携带 JointCommonFieldsOutlet（本类内聚
 *   的 ui::IFormEditOutlet 实现）——把 ParamEditSet 按键转译为
 *   PanelEditFlow::submitJointFieldEdit 域编辑流（域裁决唯一；接受/
 *   拒绝分流经 Deps.editSink 呈现）。出口绑定"供给时刻"的选中关节
 *   （检查器呈现期即该目标——选中变更即重供给，旧页随视图替换失效，
 *   出口无跨目标悬存语义，注记于出口类）。
 */
class ModelingPropertyPagesProvider final : public ui::IUiPropertyPagesProvider {
public:
    /// 复杂页稳定键（D6 激活寻址锚——小写连字符词法，协议建议风格）。
    static constexpr const char* kDhParametersPageKey = "dh-parameters";
    static constexpr const char* kPropertiesPageKey = "properties";

    /**
     * @brief 构造属性页供给者。
     *
     * @param deps [in] 装配依赖（workingSet 必填——空 function 抛
     *             std::invalid_argument；editSink 可空——空时关节页
     *             退化为纯呈现〔editOutlet=nullptr，apply 禁用〕）
     *
     * @throws std::invalid_argument deps.workingSet 为空
     */
    explicit ModelingPropertyPagesProvider(ModelingSharedSurfaceDeps deps);

    /// @brief 域注册键（"modeling"——三协议统一词表）。
    std::string domainKey() const override;

    /**
     * @brief 供给选中对象的常用字段页（D5——现取现拼，值拷贝）。
     *
     * @param object [in] 当前选中对象身份
     * @return 关节/连杆＝对应页面（完整性三查由检查器复核——本供给面
     *         保证非空、不超 kMaxCommonFieldsPerObject 哨兵、基线键闭合）；
     *         其余对象＝nullopt（合法二态——协议注释原文）
     */
    std::optional<ui::CommonFieldsPage>
    commonFieldsPage(const core::ObjectId& object) const override;

    /**
     * @brief 声明选中对象的复杂编辑页入口集（D6——零字段内容）。
     *
     * @param object [in] 当前选中对象身份
     * @return 关节＝[{dh-parameters, "DH 参数", hosted=false}]；连杆＝
     *         [{properties, "物性编辑", hosted=false}]；其余＝空集
     */
    std::vector<ui::ComplexPageEntry>
    complexPageEntries(const core::ObjectId& object) const override;

    /**
     * @brief 激活复杂编辑页（D6 编排出口——hosted=false 域自持打开形态）。
     *
     * 编排序：①解析目标（非关节/连杆或未知 pageKey→"activation-unknown-
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
     * @brief 关节常用字段页的编辑移交出口（ui::IFormEditOutlet 实现）。
     *
     * 转译规则（键→域编辑流——域裁决唯一在 applyJointFieldEdit）：
     *   - "zero-offset"→JointEditField::ZeroOffset（单值）；
     *   - "joint-lower-limit"/"joint-upper-limit"→读取当前限位后单侧
     *     替换，整对经 JointEditField::Bounds 提交（qmin<qmax 域裁决；
     *     当前限位未提供→出口拒绝 "bounds-not-provided"，不虚构整对）；
     *   - 未知键→出口拒绝 "unknown-field"（防御——检查器表单只会回传
     *     本页字段的键）。
     *
     * 绑定语义：bindTarget 在每次关节页供给时刷新为该页的关节下标——
     * 出口提交恒作用于"当前呈现页"的目标（见外层类注）；域拒绝经
     * Deps.editSink.onEditRejected 就地呈现（非模态——UX-03/07）。
     *
     * 线程约束：仅 UI 线程（applyEdits 由检查器呈现路径调用——§3.4）。
     */
    class JointCommonFieldsOutlet final : public ui::IFormEditOutlet {
    public:
        JointCommonFieldsOutlet(const ModelingSharedSurfaceDeps& deps,
                                const ModelingPropertyPagesProvider& owner);

        /// @brief 绑定当前呈现页的目标关节下标（页供给时刷新——见类注）。
        void bindTarget(std::size_t jointIndex) { m_jointIndex = jointIndex; }

        /// @brief 接收确认的修改集并转译为域编辑流（键→域入口见类注）。
        void applyEdits(const ui::ParamEditSet& editSet) override;

    private:
        /// 单键提交（zero-offset/限位单侧——返回是否全部提交成功）。
        bool submitKey(const std::string& key, double newSi);

        ModelingSharedSurfaceDeps m_deps;  ///< 会话现取＋分流出口（非 owning）
        const ModelingPropertyPagesProvider& m_owner;  ///< 域键等常量（诊断用）
        std::size_t m_jointIndex = 0;  ///< 当前绑定关节下标（页供给时刷新）
    };

    ModelingSharedSurfaceDeps m_deps;       ///< 装配依赖（非 owning——见结构注）
    mutable JointCommonFieldsOutlet m_outlet;  ///< 关节页编辑出口（供给方持有
                                               ///< ——页内裸指针的存活担保者；
                                               ///< mutable＝const 供给路径绑定目标）
};

// =====================================================================
// SelectionAdapter——选择联动（B1-SPEC §5.1 接入面三）
// =====================================================================

/**
 * @brief 建模域选择适配器（ui::IUiSelectionObserver 实现——选择服务
 *        UI-T21 冻结协议的消费侧；B1-SPEC §5.1"消费 SelectionService
 *        事件，驱动本域面板高亮/过滤；向选择服务上报本域三维拾取结果"
 *        的建模落位）。
 *
 * 联动语义（acceptance 1/2 的选择半区）：
 *   - 下行（树选→面板高亮）：onSelectionChanged——业务单选且命中建模
 *     闭包（resolveSelection 命中）→Deps.panelHighlight(oid) 驱动域
 *     面板定位/高亮；多选/清空→panelHighlight(nullopt) 对称清除；
 *     runtimeOnly 变更（L3 反解失败暂态）→零触碰（业务选中未变——
 *     SelectionChange 注释的两态语义，不误清既有高亮）；
 *   - 上行（本域三维拾取→选择服务上报）：reportView3DPick——仅上报
 *     建模闭包内对象（域外/无效身份拒绝上报＝false，零伪造——协议
 *     selectBusiness 的无效身份契约会 fail-fast，域侧先过滤）；以
 *     SelectionSource::View3DPick 汇入唯一写入口（L2 高亮判定随服务
 *     编排——本类不做正向存在性判定，那是 nameMap 端口的事）。
 *
 * 订阅生命周期：attach(service) 订阅（RAII 句柄由本类持有）；detach()
 * 显式退订；析构自动退订——**service 必须晚于本对象析构**（句柄退订
 * 触及服务内部——装配层持有序：适配器先亡，服务后亡；生产装配与测试
 * 均按此序，类注即契约）。
 */
class ModelingSelectionAdapter final : public ui::IUiSelectionObserver {
public:
    /**
     * @brief 构造选择适配器。
     *
     * @param deps [in] 装配依赖（workingSet 必填——空 function 抛
     *             std::invalid_argument；panelHighlight/devLog 可空——
     *             空高亮执行器＝联动呈现静默跳过，事件消费照常）
     *
     * @throws std::invalid_argument deps.workingSet 为空
     */
    explicit ModelingSelectionAdapter(ModelingSharedSurfaceDeps deps);

    /// 不可拷贝/不可移动（订阅句柄绑定生命周期——RAII 语义）。
    ModelingSelectionAdapter(const ModelingSelectionAdapter&) = delete;
    ModelingSelectionAdapter& operator=(const ModelingSelectionAdapter&) = delete;

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
     * @brief 上报一次本域三维拾取（上行——View3DPick 来源汇入唯一写入口）。
     *
     * @param oid [in] 拾取命中的对象身份（须为建模闭包内有效对象——
     *            域外/无效身份拒绝上报并返回 false，不出诊断——拾取
     *            未命中本域对象是常态而非错误）
     * @return true＝已上报（服务已广播 View3DPick 选中）；false＝未
     *         上报（未 attach／无效身份／闭包外对象）
     */
    bool reportView3DPick(const core::ObjectId& oid);

private:
    ModelingSharedSurfaceDeps m_deps;       ///< 装配依赖（非 owning——见结构注）
    ui::SelectionService* m_service = nullptr;  ///< 已订阅服务（attach/detach 管理）
    std::unique_ptr<core::IEventSubscription> m_subscription;  ///< RAII 订阅句柄
};

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_MODELING_PLUGIN_HOSTMIGRATIONPROVIDERS_HPP
