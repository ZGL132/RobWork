/**
 * @file   HostMigrationProviders.cpp
 * @brief  建模域宿主迁移三接入面实现——协议值 ← 工作集投影的搬运层
 *         （头注为契约权威；本文件只做"现取现拼"与"键→域编辑流"转译，
 *         零业务判定——插件零计算逻辑红线，DTB §5.3）。
 */

#include "HostMigrationProviders.hpp"

#include <stdexcept>
#include <utility>

#include <sdurws/ird/core/Units.hpp>  // core::UnitToken::find（字段 SI/显示单位 token——SA-12 换算入口的装配面）

namespace sdurws {
namespace ird {
namespace modeling {
namespace {

// ---- 域注册键（三协议统一词表——常量字面，跨调用稳定）------------------
constexpr const char* kModelingDomainKey = "modeling";

// ---- 页面冻结文案（文案改动＝契约改动，须升单元卡修订——对端同款惯例）--
constexpr const char* kJointCommonPageTitle = "关节常用参数";
constexpr const char* kLinkCommonPageTitle = "连杆物性（只读）";
constexpr const char* kDhPageTitle = "DH 参数";
constexpr const char* kPropertiesPageTitle = "物性编辑";

// ---- 字段稳定键（基线注入与批量粘贴的寻址锚——小写连字符词法）----------
constexpr const char* kZeroOffsetFieldKey = "zero-offset";
constexpr const char* kLowerLimitFieldKey = "joint-lower-limit";
constexpr const char* kUpperLimitFieldKey = "joint-upper-limit";
constexpr const char* kMassFieldKey = "mass";

/**
 * @brief 关节平移/角度量纲与单位（按类型分流——Prismatic 为移动 m，
 *        其余〔Revolute/Continuous/Fixed〕为转动 rad；与 PanelModel 的
 *        零位/限位单位呈现同一分流规则——单一语义两处复用同源判定）。
 */
core::QuantityKind jointQuantityKind(JointType type) noexcept
{
    return type == JointType::Prismatic ? core::QuantityKind::Length
                                        : core::QuantityKind::Angle;
}

/// @brief 量纲对应的 SI/显示单位 token（SI 恒同显示——建模字段默认
///        m/rad/kg 制式，KIN-12 显示切换是检查器会话面不在本供给面）。
core::UnitToken unitTokenFor(core::QuantityKind kind)
{
    switch (kind) {
    case core::QuantityKind::Length: return core::UnitToken::find("m").value();
    case core::QuantityKind::Angle: return core::UnitToken::find("rad").value();
    case core::QuantityKind::Mass: return core::UnitToken::find("kg").value();
    default: break;
    }
    // 词表内建模字段只出现上述三量纲——不可达分支以 Dimensionless 兜底
    // （fail-fast 交给 makeQuantityFieldSpec 的量纲核对）。
    return core::UnitToken::find("1").value();
}

/// @brief 出口拒绝投递（editSink 缺席＝静默丢弃——纯呈现装配形态的
///        显式声明语义，见 Deps.editSink 字段注）。
void rejectViaSink(IPanelEditSink* sink, const char* codeToken, std::string detail)
{
    if (sink == nullptr) { return; }
    EditRejection r;
    r.codeToken = codeToken;
    r.detail = std::move(detail);
    sink->onEditRejected(r);
}

}  // namespace

// =====================================================================
// ModelingTreeNodesProvider
// =====================================================================

ModelingTreeNodesProvider::ModelingTreeNodesProvider(ModelingSharedSurfaceDeps deps)
    : m_deps(std::move(deps))
{
    // 装配缺陷 fail-fast：无工作集现取入口的供给者是"永远供空集"的死面
    // ——构造期拒绝优于运行期静默（对端 ProjectTreeModel 同款口径）。
    if (!m_deps.workingSet) {
        throw std::invalid_argument(
            "ModelingTreeNodesProvider: deps.workingSet 必填（空现取入口＝装配缺陷）");
    }
}

std::string ModelingTreeNodesProvider::domainKey() const
{
    return kModelingDomainKey;
}

std::vector<ui::ProjectTreeNode> ModelingTreeNodesProvider::treeNodes() const
{
    // 无会话＝空集（协议注释的合法常态——不产生组内占位节点）。
    const ModelingWorkingSet* ws = m_deps.workingSet();
    if (ws == nullptr) { return {}; }

    // 与方案 A 自持树同一份投影（PanelModel::buildStructureTree——零第二
    // 套树语义；行序即呈现序，NFR-COR-02）。分组行/基座行无对象身份
    // （objectId=nullopt）——共享树只承载对象节点，逐行过滤。
    const std::vector<StructureNode> rows = buildStructureTree(*ws);

    std::vector<ui::ProjectTreeNode> nodes;
    nodes.reserve(rows.size());
    for (const StructureNode& row : rows) {
        if (!row.objectId.has_value()) { continue; }  // 分组/基座行——仅方案 A 呈现折叠
        ui::ProjectTreeNode node;
        node.objectId = *row.objectId;
        node.group = ui::ProjectTreeGroup::ModelingObjects;  // 建模域固定分组（五分组封闭词表）
        node.depth = 0;  // 先按组直属顶层填——根存在时下方统一降一级
        nodes.push_back(std::move(node));
    }
    if (nodes.empty()) { return nodes; }

    // 根节点＝投影首行（ModelRoot 携带 rootObjectId 时才有身份——模板
    // 初始草稿 rootObjectId=nullopt，此时子对象保持组直属顶层，无根）。
    if (rows.front().kind == StructureNodeKind::ModelRoot
        && rows.front().objectId.has_value()) {
        // 根的子对象＝除根外全部节点（按投影序：关节链→连杆链→工具→
        // 场景→位姿集→传动——与自持树分组行序一致）。子引用闭合：全部
        // 子对象都在本轮供给集内（重建边界 dangling 检查恒过）。
        auto& root = nodes.front();
        root.childObjectIds.reserve(nodes.size() - 1);
        for (std::size_t i = 1; i < nodes.size(); ++i) {
            root.childObjectIds.push_back(nodes[i].objectId);
            nodes[i].depth = 1;  // 子对象统一一级（深度提示——面板按此缩进）
        }
    }
    return nodes;
}

// =====================================================================
// ModelingPropertyPagesProvider
// =====================================================================

ModelingPropertyPagesProvider::ModelingPropertyPagesProvider(ModelingSharedSurfaceDeps deps)
    : m_deps(std::move(deps)), m_outlet(m_deps, *this)
{
    if (!m_deps.workingSet) {
        throw std::invalid_argument(
            "ModelingPropertyPagesProvider: deps.workingSet 必填（空现取入口＝装配缺陷）");
    }
}

std::string ModelingPropertyPagesProvider::domainKey() const
{
    return kModelingDomainKey;
}

std::optional<ui::CommonFieldsPage>
ModelingPropertyPagesProvider::commonFieldsPage(const core::ObjectId& object) const
{
    const ModelingWorkingSet* ws = m_deps.workingSet();
    if (ws == nullptr) { return std::nullopt; }  // 无会话——非本域应答（诚实二态）

    // 域判定在域：对象归属由既有投影自答（命中关节/连杆才应答——检查器
    // 零建模类型知识，first-wins 询问序由检查器编排）。
    const auto target = resolveSelection(*ws, object);
    if (!target.has_value()) { return std::nullopt; }

    ui::CommonFieldsPage page;
    if (target->kind == SelectedTarget::Kind::Joint) {
        // ---- 关节页（D5 常用编辑字段：零位＋限位；axis/origin/DH 等
        //      大批量/权威敏感字段一律不进共享检查器——D6 收口域面板）。
        const JointEntry& j = ws->design.joints.at(target->index);
        const core::QuantityKind kind = jointQuantityKind(j.type);
        const core::UnitToken unit = unitTokenFor(kind);
        page.title = kJointCommonPageTitle;
        page.readOnly = false;  // 草稿编辑流可达（出口转译域编辑——见 outlet）
        // 零位偏置：两权威模式均权威（§7.2）——恒在常用面。
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kZeroOffsetFieldKey, "零位偏置", kind, unit, unit));
        page.values.push_back({kZeroOffsetFieldKey, j.zeroOffset});
        // 限位：Revolute/Prismatic 必填面（Continuous＝NotApplicable——
        // I-MDL-4，此时不供限位字段，页面以零位字段保底非空）。
        if (j.bounds.state() == core::FieldState::Provided) {
            const auto& limits = j.bounds.value();
            page.fields.push_back(ui::makeQuantityFieldSpec(
                kLowerLimitFieldKey, "限位下限", kind, unit, unit));
            page.fields.push_back(ui::makeQuantityFieldSpec(
                kUpperLimitFieldKey, "限位上限", kind, unit, unit));
            page.values.push_back({kLowerLimitFieldKey, limits.first});
            page.values.push_back({kUpperLimitFieldKey, limits.second});
        }
        page.editOutlet = m_deps.editSink != nullptr ? &m_outlet : nullptr;
        if (m_deps.editSink != nullptr) {
            m_outlet.bindTarget(target->index);  // 出口绑定供给时刻的目标关节
        }
        return page;
    }
    if (target->kind == SelectedTarget::Kind::Link) {
        // ---- 连杆页（D5 只读事实：质量＋来源；质心/惯量编辑需 L-8 平行
        //      轴二选一确认流——D6 收口域面板，不在共享检查器展开）。
        const LinkEntry& l = ws->design.links.at(target->index);
        const core::UnitToken kg = unitTokenFor(core::QuantityKind::Mass);
        page.title = kLinkCommonPageTitle;
        page.readOnly = true;  // 只读事实呈现（P-UI-6 单侧纪律——检查器只呈现）
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kMassFieldKey, "质量", core::QuantityKind::Mass, kg, kg));
        if (const auto mass = l.body.mass.tryValue()) {
            page.values.push_back({kMassFieldKey, *mass});  // 未提供＝值缺省（占位呈现，不伪造 0）
        }
        page.editOutlet = nullptr;  // 只读页不提供移交面（保守收口——对端契约）
        return page;
    }
    // 其余建模对象（根/基座/工具/场景/位姿集/传动）：v1 无页面供给
    // （增量面边界——不虚构字段，登记单元卡 §14.6）。
    return std::nullopt;
}

std::vector<ui::ComplexPageEntry>
ModelingPropertyPagesProvider::complexPageEntries(const core::ObjectId& object) const
{
    const ModelingWorkingSet* ws = m_deps.workingSet();
    if (ws == nullptr) { return {}; }

    const auto target = resolveSelection(*ws, object);
    if (!target.has_value()) { return {}; }  // 非本域应答＝空集（协议二态）

    std::vector<ui::ComplexPageEntry> entries;
    if (target->kind == SelectedTarget::Kind::Joint) {
        ui::ComplexPageEntry e;
        e.pageKey = kDhParametersPageKey;
        e.title = kDhPageTitle;
        e.hosted = false;  // D6"域面板内"形态——激活聚焦建模面板该关节
        entries.push_back(std::move(e));
    } else if (target->kind == SelectedTarget::Kind::Link) {
        ui::ComplexPageEntry e;
        e.pageKey = kPropertiesPageKey;
        e.title = kPropertiesPageTitle;
        e.hosted = false;  // 同上——质心/惯量编辑流（L-8）在域面板内联呈现
        entries.push_back(std::move(e));
    }
    return entries;
}

ui::ComplexPageActivationReport
ModelingPropertyPagesProvider::activateComplexPage(const core::ObjectId& object,
                                                   const std::string& pageKey,
                                                   QWidget* parent)
{
    (void)parent;  // 自持页（hosted=false）忽略宿装父——签名兼容保留

    ui::ComplexPageActivationReport report;
    const ModelingWorkingSet* ws = m_deps.workingSet();
    const auto target = ws != nullptr ? resolveSelection(*ws, object) : std::nullopt;

    // ①寻址核对：pageKey 必须与该对象的声明入口一致（检查器编排面已查，
    //   本处为 Provider 侧防御——两处同 token，拒绝语义一致）。
    const bool isDh = target.has_value()
        && target->kind == SelectedTarget::Kind::Joint
        && pageKey == kDhParametersPageKey;
    const bool isProperties = target.has_value()
        && target->kind == SelectedTarget::Kind::Link
        && pageKey == kPropertiesPageKey;
    if (!isDh && !isProperties) {
        report.reason = "activation-unknown-page";
        return report;
    }
    // ②执行器核对：无"域自持打开"执行器＝无法真实打开——诚实拒绝，
    //   不伪造激活成功（Deps.complexPageActivator 可空性声明的运行语义）。
    if (!m_deps.complexPageActivator) {
        report.reason = "domain-surface-unavailable";
        return report;
    }
    // ③域自持打开（hosted=false 语义——打开动作在本调用内完成；视图
    //   本体是域面板内编辑区，检查器零宿装）。
    m_deps.complexPageActivator(object);
    report.ok = true;
    report.hostedWidget = nullptr;  // 自持页恒空——返回非空即契约违约（对端核对面）
    return report;
}

// =====================================================================
// ModelingPropertyPagesProvider::JointCommonFieldsOutlet
// =====================================================================

ModelingPropertyPagesProvider::JointCommonFieldsOutlet::JointCommonFieldsOutlet(
    const ModelingSharedSurfaceDeps& deps, const ModelingPropertyPagesProvider& owner)
    : m_deps(deps), m_owner(owner)
{
}

bool ModelingPropertyPagesProvider::JointCommonFieldsOutlet::submitKey(
    const std::string& key, double newSi)
{
    // 现取工作集与分流出口（任一缺失＝编辑通道未装配——出口拒绝，不虚构；
    // editSink 缺席时本出口根本不会暴露在页面上——此为防御面）。
    ModelingWorkingSet* ws = m_deps.workingSet ? m_deps.workingSet() : nullptr;
    if (ws == nullptr) {
        rejectViaSink(m_deps.editSink, "no-session", "无会话工作集，编辑未应用");
        return false;
    }
    if (m_deps.editSink == nullptr) {
        return false;  // 无分流出口＝编辑通道未装配（纯呈现形态——不提交）
    }

    if (key == kZeroOffsetFieldKey) {
        // 零位：单值直投域编辑流（接受/拒绝分流与就地呈现由
        // PanelEditFlow 经 sink 完成——域裁决唯一，本出口零判定）。
        submitJointFieldEdit(*ws, *m_deps.editSink, m_jointIndex,
                             JointEditField::ZeroOffset, newSi);
        return true;
    }
    if (key == kLowerLimitFieldKey || key == kUpperLimitFieldKey) {
        // 限位：单侧替换整对提交（域入口是 JointLimits 对——先读当前
        // 限位，替换单侧后经 Bounds 裁决 qmin<qmax）。
        const JointEntry& j = ws->design.joints.at(m_jointIndex);
        const auto current = j.bounds.tryValue();
        if (!current.has_value()) {
            rejectViaSink(m_deps.editSink, "bounds-not-provided",
                          "该关节限位未提供（Continuous 不适用或未填），无法单侧修改");
            return false;
        }
        JointLimits updated = *current;
        if (key == kLowerLimitFieldKey) {
            updated.first = newSi;
        } else {
            updated.second = newSi;
        }
        submitJointFieldEdit(*ws, *m_deps.editSink, m_jointIndex,
                             JointEditField::Bounds, updated);
        return true;
    }
    // 未知键：检查器表单只会回传本页字段的键——此分支为防御面。
    rejectViaSink(m_deps.editSink, "unknown-field", "未知字段键：" + key);
    return false;
}

void ModelingPropertyPagesProvider::JointCommonFieldsOutlet::applyEdits(
    const ui::ParamEditSet& editSet)
{
    // 逐字段转译（键序＝确认区注册序；单字段失败不阻断其余字段——
    // 与 ParamEditModel 的逐行编辑语义一致；域拒绝就地呈现）。
    for (const ui::ParamChange& change : editSet.changes) {
        submitKey(change.key, change.newSi);
    }
}

// =====================================================================
// ModelingSelectionAdapter
// =====================================================================

ModelingSelectionAdapter::ModelingSelectionAdapter(ModelingSharedSurfaceDeps deps)
    : m_deps(std::move(deps))
{
    if (!m_deps.workingSet) {
        throw std::invalid_argument(
            "ModelingSelectionAdapter: deps.workingSet 必填（空现取入口＝装配缺陷）");
    }
}

void ModelingSelectionAdapter::attach(ui::SelectionService& service)
{
    detach();  // 幂等收口：重复 attach 先退订既有订阅
    m_service = &service;
    m_subscription = service.subscribe(*this);  // RAII 句柄——析构/detach 即退订
}

void ModelingSelectionAdapter::detach() noexcept
{
    m_subscription.reset();  // 句柄析构即退订（幂等）
    m_service = nullptr;
}

void ModelingSelectionAdapter::onSelectionChanged(const ui::SelectionChange& change)
{
    // runtimeOnly（L3 反解失败暂态）：业务选中集未变——零触碰（不误清
    // 既有面板高亮，SelectionChange 两态语义的适配器侧落实）。
    if (change.runtimeOnly) { return; }

    // 业务单选且命中建模闭包→面板高亮/定位；多选/清空→对称清除。
    // 闭包外对象（他域选中）→不动面板（域间联动零串扰——各域适配器
    // 只对本域对象动作）。
    if (change.selectedObjectIds.size() == 1 && m_deps.workingSet()) {
        const auto target = resolveSelection(*m_deps.workingSet(),
                                             change.selectedObjectIds.front());
        if (target.has_value()) {
            if (m_deps.panelHighlight) {
                m_deps.panelHighlight(change.selectedObjectIds.front());
            }
            return;
        }
    }
    if (m_deps.panelHighlight) {
        m_deps.panelHighlight(std::nullopt);  // 多选/清空/他域——清除高亮
    }
}

bool ModelingSelectionAdapter::reportView3DPick(const core::ObjectId& oid)
{
    // 前置过滤：未订阅/无效身份（全零保留值）/闭包外对象一律不上报
    // ——selectBusiness 对无效身份 fail-fast，域侧先过滤（拾取未命中
    // 本域是常态，不出诊断——协议 selectBusiness 契约的调用方义务）。
    if (m_service == nullptr || !m_deps.workingSet() || !oid.isValid()) {
        return false;
    }
    if (!resolveSelection(*m_deps.workingSet(), oid).has_value()) {
        return false;
    }
    m_service->selectBusiness({oid}, ui::SelectionSource::View3DPick);
    return true;
}

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws
