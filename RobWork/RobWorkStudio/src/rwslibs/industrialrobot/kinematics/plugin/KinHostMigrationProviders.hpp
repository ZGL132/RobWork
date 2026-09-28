/**
 * @file   KinHostMigrationProviders.hpp
 * @brief  运动学域宿主迁移三接入面（方案 B.1／SA-18 D11）——TreeNodesProvider
 *         ＋PropertyPagesProvider＋SelectionAdapter（WP-15-T18；接入面 v1
 *         诚实边界——DTB §4.2 O-44 裁决）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/WP-15-T18.json acceptance 1/4（v1.2——O-44
 *     裁决收窄后的接入面 v1 诚实边界与"任务点选择→结果面板高亮→D6 求解
 *     配置页经面板直达"链路）、acceptance 2/3（Jog/Playback 沿用既有会话
 *     态设施）、acceptance 6（共享 UI 装配面零改动——只消费注册协议）；
 *   - B1-SPEC §5.1（迁移交付面三接入面职责原文）、§5.2（迁移期双形态
 *     并存——本域自持导航标记保留可用）、§5.3（共享 UI 文件互斥——本
 *     文件全部位于 kinematics/plugin/ 本域目录，零共享面触碰）；
 *   - DTB §4.2 O-44（2026-09-29 所有者"解决阻塞"口令裁决）：运动学域当前
 *     无可诚实入树的本域 ObjectId 对象——任务点/工况/区域＝requirements
 *     域对象（WP-14-T10 Provider 已供给，跨域供给＝范围越界）；求解配置
 *     ＝用户级持久化（AnalysisConfig.hpp 冻结红线"配置无项目对象身份……
 *     不伪造 ObjectId"）；结果＝runId 归档工件＋会话对象（requestIdentity/
 *     ContentIdentity），全仓库无任何域结果对象持有 ObjectId（B1-SPEC
 *     §3.1 行 4/5 为前瞻形态）。故本域树/页面两面按**结构接缝注册＋诚实
 *     空供给/无应答**落位，端到端链路经选择服务下行联动承载；
 *   - ui 注册协议（UI-T21/T22 冻结形状——只消费不改签名）：
 *     IndustrialProjectTree.hpp（IUiTreeNodesProvider/ProjectTreeNode——
 *     协议明文"可空＝本域暂无可入树对象——合法常态；空集不产生组内占位
 *     节点"）、PropertyInspector.hpp（IUiPropertyPagesProvider——"非本域
 *     对象 → nullopt／空集＝合法二态"）、SelectionService.hpp
 *     （IUiSelectionObserver/SelectionChange）；
 *   - requirements 先例：WP-14-T10 requirements/plugin/HostMigrationProviders
 *     （同型迁移第二棒——本文件为其 kinematics 侧同构实现，协议消费面与
 *     装配形态逐点对齐；差异＝v1 无应答面，见各类注）；
 *   - knownPitfalls：P-KIN-7（对端契约增量同步义务——协议头发现不兼容按
 *     影响面增量同步登记单元卡，不私改对端）；O-43（宿主融合边界——本
 *     文件不触碰 Dock 拓扑/宿主装配层，宿主挂位归 UI-T23/WP-24-T08）。
 *
 * 背景说明（为什么 v1 两面是"空供给/无应答"而不是"补身份"）：
 *   树/检查器的节点身份一律为 ObjectId（CON-01），而 ObjectId 的分配纪律
 *   是"对象创建→project"（core Identity.hpp）——域侧自造会话节点身份＝
 *   伪造身份（协议头明文"伪造身份＝范围越界，验收对抗项"），把 requirements
 *   域的任务点代供进本域 Provider＝跨域供给（同禁）。诚实边界因此是唯一
 *   合规落位：接缝就位（domainKey 注册、重建路径全通），内容等对象升格
 *   项目身份（B1-SPEC §3.1 前瞻形态经所有者裁决）后原样填充——树面词表
 *   扩展走 B1-SPEC 增量修订通道，本文件零改动预期。
 *
 * 线程约束：全部类仅 UI 线程构造与访问（§3.4——会话态/面板指针是会话
 *   对象；与 RequirementsSharedSurfaceDeps 同口径）。非线程安全。
 *
 * 生命周期/所有权：三个 Provider/Adapter 由装配层（KinematicsUiModule 门
 *   面或测试）以 shared_ptr/unique_ptr 持有并注册进共享模型（模型持强引
 *   用）；Deps 内回调为非 owning——调用方保证存活期覆盖注册期（对端头注
 *   同款惯例）。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINHOSTMIGRATIONPROVIDERS_HPP
#define IRD_KINEMATICS_PLUGIN_KINHOSTMIGRATIONPROVIDERS_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>          // core::IEventSubscription（订阅 RAII 句柄——⑤事件端口）
#include <sdurws/ird/core/Identity.hpp>        // core::ObjectId（选中身份——CON-01）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>  // ui::IUiTreeNodesProvider/ProjectTreeNode（树协议）
#include <sdurws/ird/ui/PropertyInspector.hpp>      // ui::IUiPropertyPagesProvider 及页面值（页协议）
#include <sdurws/ird/ui/SelectionService.hpp>       // ui::IUiSelectionObserver/SelectionService（选择协议）

class QWidget;  // 前置声明：activateComplexPage 宿装父控件（头文件不拖入 Widgets）

namespace sdurws {
namespace ird {
namespace kinematics {

// =====================================================================
// 装配依赖（三接入面共用的呈现执行器——装配层/测试一次性给出）
// =====================================================================

/**
 * @brief 三接入面的装配依赖（非 owning——调用方保证存活期覆盖注册期）。
 *
 * v1 诚实边界（O-44）下本依赖只有**呈现执行器**一员——requirements 先例
 *   的 editor/rootObjectId 现取入口在本域无对应物（运动学无编辑器工作集，
 *   无本域树对象身份），结构上不设即是"零伪造数据源"的类型级承载。
 *
 * panelHighlight 可空＝下行联动的呈现半区缺失（事件消费照常，高亮静默
 *   跳过——"可空注入显式声明"先例）；生产装配绑定面板
 *   KinematicsPanelWidget::focusTaskPoint（树选任务点→任务点表定位/高亮
 *   ——acceptance 4 v1 链路第二棒）。
 */
struct KinematicsSharedSurfaceDeps {
    /// 下行联动执行器（业务选中变更→域面板定位/高亮；nullopt 入参＝清除
    /// 高亮〔多选/清空选中〕；空＝高亮动作静默跳过）。
    std::function<void(const std::optional<core::ObjectId>&)> panelHighlight;
};

// =====================================================================
// TreeNodesProvider——结构接缝注册＋v1 恒空集供给（B1-SPEC §5.1 接入面一）
// =====================================================================

/**
 * @brief 运动学域树节点供给者（ui::IUiTreeNodesProvider 实现——协议形状
 *        UI-T21 冻结，零签名改动；**v1 恒空集供给**——O-44 裁决）。
 *
 * 供给语义（acceptance 1 v1）：
 *   - domainKey()＝"kinematics"（三协议统一域键词表——注册边界查重用；
 *     注册本身即结构接缝的落位证明：与 modeling/requirements 同表登记，
 *     跨 Provider 撞号防御对本域同样生效）；
 *   - treeNodes() 恒返回空集——协议明文"可空＝本域暂无可入树对象——合法
 *     常态；空集不产生组内占位节点"。为什么恒空（O-44 事实链）：本域当前
 *     不存在任何持有 ObjectId 的业务对象可诚实入树——任务点/工况/区域是
 *     requirements 域对象（其树面由该域自己的 Provider 供给，本域代供即
 *     跨域供给违约）；求解配置是用户级独立持久化（AnalysisConfig.hpp 冻结
 *     红线"不伪造 ObjectId"）；结果是 runId 归档工件＋会话对象（无项目
 *     对象身份）。本方法不接受任何数据源注入（构造依赖只有呈现执行器）——
 *     "没有可供给的数据"在类型面成立，而非运行期恰好为空；
 *   - 前瞻形态：B1-SPEC §3.1 行 4/5（分析配置/结果入树）经所有者裁决补
 *     齐对象身份模型后，本类是唯一填充点（接缝已注册、重建路径全通）；
 *     词表扩展走 B1-SPEC 增量修订，本类签名零改动预期。
 */
class KinematicsTreeNodesProvider final : public ui::IUiTreeNodesProvider {
public:
    /**
     * @brief 构造树节点供给者（依赖留参＝装配形态与 requirements 先例
     *        同构——v1 下无成员消费它，为前瞻填充保留同型构造面）。
     *
     * @param deps [in] 装配依赖（可空成员合法——见结构注）
     */
    explicit KinematicsTreeNodesProvider(KinematicsSharedSurfaceDeps deps);

    /// @brief 域注册键（"kinematics"——三协议统一词表，常量字面跨调用稳定）。
    std::string domainKey() const override;

    /**
     * @brief 供给本域树节点集（模型重建时现调——**v1 恒空集**，见类注）。
     *
     * @return 空集（协议"合法常态"语义——不产生组内占位节点；重建校验
     *         四查对空集恒过——零违约面）
     */
    std::vector<ui::ProjectTreeNode> treeNodes() const override;

private:
    KinematicsSharedSurfaceDeps m_deps;  ///< 装配依赖（非 owning——v1 未消费，前瞻保留）
};

// =====================================================================
// PropertyPagesProvider——结构接缝注册＋v1 无应答面（接入面二）
// =====================================================================

/**
 * @brief 运动学域属性页供给者（ui::IUiPropertyPagesProvider 实现——协议
 *        形状 UI-T22 冻结，零签名改动；**v1 无应答面**——O-44 裁决）。
 *
 * 页面供给语义（acceptance 1 v1）：
 *   - commonFieldsPage(object) 恒 nullopt——D5 常用字段通道的本域应答在
 *     v1 不存在：应答的前提是"选中的是本域对象"，而本域无持有 ObjectId
 *     的对象（O-44 事实链见 TreeNodesProvider 类注）。对外域对象（含
 *     requirements 任务点）应答＝跨域代供，同禁。nullopt 是协议"合法二
 *     态"——检查器顺延询问下一注册者，本域注册不改变 first-wins 裁决序；
 *   - complexPageEntries(object) 恒空集——与 nullopt 同答（协议纪律：
 *     "非本域对象返回空集——与 commonFieldsPage 的 nullopt 同答"）；
 *   - 求解配置/初值策略的 D6 收口位＝域面板高级面板（KinematicsConfigPanel
 *     ——WP-15-T10/T12 既有落位），**不经**共享检查器展开：D6 分野的本域
 *     形态本来就是"收口在域面板"，v1 无应答面不改变该收口（acceptance 4
 *     v1 链路第三棒"经面板直达"的落点）；
 *   - activateComplexPage 恒拒绝（reason "activation-object-not-owned"）
 *     ——激活编排要求对象仍被本 Provider 应答，而本 Provider 恒不应答，
 *     检查器编排路径（入口按钮→模型→应答域）在 v1 不可能路由到本类；
 *     本实现是协议完整性的防御半区（直接调用＝调用方违约，诚实拒绝不
 *     伪造打开成功）。
 */
class KinematicsPropertyPagesProvider final : public ui::IUiPropertyPagesProvider {
public:
    /**
     * @brief 构造属性页供给者（依赖留参同 TreeNodesProvider——前瞻保留）。
     *
     * @param deps [in] 装配依赖（可空成员合法——见结构注）
     */
    explicit KinematicsPropertyPagesProvider(KinematicsSharedSurfaceDeps deps);

    /// @brief 域注册键（"kinematics"——三协议统一词表）。
    std::string domainKey() const override;

    /**
     * @brief 供给选中对象的常用字段页（D5——**v1 恒 nullopt**，见类注）。
     *
     * @param object [in] 当前选中对象身份（任意值——含外域对象）
     * @return nullopt（协议"非本域对象 → nullopt 合法二态"——检查器顺延
     *         询问下一注册者）
     */
    std::optional<ui::CommonFieldsPage>
    commonFieldsPage(const core::ObjectId& object) const override;

    /**
     * @brief 声明选中对象的复杂编辑页入口集（D6——**v1 恒空集**，见类注）。
     *
     * @param object [in] 当前选中对象身份（任意值）
     * @return 空集（与 commonFieldsPage 的 nullopt 同答——协议纪律）
     */
    std::vector<ui::ComplexPageEntry>
    complexPageEntries(const core::ObjectId& object) const override;

    /**
     * @brief 激活复杂编辑页（D6 编排出口——**v1 恒诚实拒绝**，见类注）。
     *
     * @param object  [in] 目标对象身份（任意值）
     * @param pageKey [in] 目标页稳定键（任意值）
     * @param parent  [in] 宿装父控件（v1 忽略——无宿装页）
     * @return {ok=false, reason="activation-object-not-owned", hostedWidget=
     *         nullptr}（封闭词表 token——对象无本域应答即无激活面）
     */
    ui::ComplexPageActivationReport
    activateComplexPage(const core::ObjectId& object, const std::string& pageKey,
                        QWidget* parent) override;

private:
    KinematicsSharedSurfaceDeps m_deps;  ///< 装配依赖（非 owning——v1 未消费，前瞻保留）
};

// =====================================================================
// SelectionAdapter——选择联动（B1-SPEC §5.1 接入面三；v1 全功能下行）
// =====================================================================

/**
 * @brief 运动学域选择适配器（ui::IUiSelectionObserver 实现——选择服务
 *        UI-T21 冻结协议的消费侧；acceptance 4 v1 链路"任务点选择→结果
 *        面板高亮"的下行承载）。
 *
 * 联动语义（acceptance 1/4 v1）：
 *   - 下行（树选→结果面板高亮）：onSelectionChanged——业务单选→
 *     Deps.panelHighlight(oid) 驱动域面板定位/高亮（生产装配＝任务点表
 *     定位：KinTaskPointRow.pointOid 是宿主投影的选中锚——任务点对象虽
 *     属 requirements 域，其结果列在本域任务点验证面板呈现，"选点→看
 *     结果"是本域面板的既有消费语义 L-K1 的共享面延伸）；多选/清空→
 *     panelHighlight(nullopt) 对称清除；runtimeOnly 变更（L3 反解失败
 *     暂态）→零触碰（业务选中未变——SelectionChange 两态语义，不误清
 *     既有高亮）；
 *   - 上行（本域三维拾取→选择服务上报）：**v1 诚实不上报**——上报值
 *     必须是本域持有 ObjectId 的业务对象（协议 selectBusiness 的无效身
 *     份契约会 fail-fast，域侧先过滤），而本域 v1 无此类对象（O-44）；
 *     reportView3DPick 因此恒返回 false（拾取未命中本域对象是常态而非
 *     错误——不出诊断零伪造）。前瞻：对象身份模型补齐后本方法即上行
 *     填充点（接缝同型于 requirements 先例）。
 *
 * 为什么下行消费外域对象身份不算越界：跨域供给禁令约束的是**供给面**
 *   （Provider 把他域对象供进共享呈现面）；选择联动是 B1-SPEC §5.1 明文
 *   的消费面（"消费 SelectionService 事件，驱动本域面板高亮/过滤"）——
 *   SelectionService 是全域唯一选中汇聚点（§4.1），域适配器按本域消费
 *   语义响应业务选中，正是该设计的预期形态。
 *
 * 订阅生命周期：attach(service) 订阅（RAII 句柄由本类持有）；detach()
 *   显式退订；析构自动退订——**service 必须晚于本对象析构**（句柄退订
 *   触及服务内部——装配层持有序：适配器先亡，服务后亡；生产装配与测试
 *   均按此序，类注即契约）。
 */
class KinematicsSelectionAdapter final : public ui::IUiSelectionObserver {
public:
    /**
     * @brief 构造选择适配器。
     *
     * @param deps [in] 装配依赖（panelHighlight 可空——空高亮执行器＝
     *             联动呈现静默跳过，事件消费照常）
     */
    explicit KinematicsSelectionAdapter(KinematicsSharedSurfaceDeps deps);

    /// 不可拷贝/不可移动（订阅句柄绑定生命周期——RAII 语义）。
    KinematicsSelectionAdapter(const KinematicsSelectionAdapter&) = delete;
    KinematicsSelectionAdapter& operator=(const KinematicsSelectionAdapter&) = delete;

    /**
     * @brief 订阅选择服务（装配期一次；重复 attach 先退订再订阅——
     *        幂等收口）。
     *
     * @param service [in] 选择服务（引用入参无空态；非 owning——存活期
     *                须覆盖订阅期，且晚于本对象析构，见类注）
     */
    void attach(ui::SelectionService& service);

    /// @brief 显式退订（幂等——未订阅时空操作；析构路径复用）。
    void detach() noexcept;

    /// @brief 选择已变更（下行联动——语义见类注；UI 线程同步回调）。
    void onSelectionChanged(const ui::SelectionChange& change) override;

    /**
     * @brief 上报一次本域三维拾取（上行——**v1 恒 false**，见类注）。
     *
     * @param oid [in] 拾取命中的对象身份（任意值——v1 无本域可上报对象，
     *            本参数被诚实忽略）
     * @return false＝未上报（v1 诚实边界——本域无持有 ObjectId 的业务
     *         对象可上报；零伪造、零诊断）
     */
    bool reportView3DPick(const core::ObjectId& oid);

private:
    KinematicsSharedSurfaceDeps m_deps;         ///< 装配依赖（非 owning——见结构注）
    ui::SelectionService* m_service = nullptr;  ///< 已订阅服务（attach/detach 管理）
    std::unique_ptr<core::IEventSubscription> m_subscription;  ///< RAII 订阅句柄
};

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINHOSTMIGRATIONPROVIDERS_HPP
