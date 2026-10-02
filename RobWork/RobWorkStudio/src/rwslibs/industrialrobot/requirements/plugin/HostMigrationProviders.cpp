/**
 * @file   HostMigrationProviders.cpp
 * @brief  需求域宿主迁移三接入面实现——协议值 ← 编辑器工作集投影的搬运层
 *         （头注为契约权威；本文件只做"现取现拼"与"键→域编辑流"转译，
 *         零业务判定——插件零计算逻辑红线，DTB §5.3）。
 */

#include "HostMigrationProviders.hpp"

#include <stdexcept>
#include <utility>

#include <sdurws/ird/core/Units.hpp>  // core::UnitToken::find（字段 SI/显示单位 token——SA-12 换算入口的装配面）

namespace sdurws {
namespace ird {
namespace requirements {
namespace {

// ---- 域注册键（三协议统一词表——常量字面，跨调用稳定）------------------
constexpr const char* kRequirementsDomainKey = "requirements";

// ---- 页面冻结文案（文案改动＝契约改动，须升单元卡修订——对端同款惯例）--
// UI-T39（审核 P1"双树/双检查器职责"）：共享检查器的本域页面标题统一
// 携带『（需求草稿）』标注——共享检查器呈现的是需求草稿工作集中的对象
// （尚未应用为项目修订），标题自释"这里看到的不是已应用事实"；复杂编辑
// 页入口（hosted=false）激活即跳转需求面板，标注同时提示真正的编辑入口。
constexpr const char* kPointCommonPageTitle = "任务点常用参数（需求草稿）";
constexpr const char* kPointEditorTitle = "任务点编辑（在需求面板打开）";
constexpr const char* kRegionDefinitionTitle = "区域定义（在需求面板打开）";
constexpr const char* kConditionEditorTitle = "工况编辑（在需求面板打开）";
constexpr const char* kPlanEditorTitle = "采样计划编辑（在需求面板打开）";
constexpr const char* kImportWizardTitle = "CSV/JSON 导入向导（在需求面板打开）";

// ---- 字段稳定键（基线注入与批量粘贴的寻址锚——小写连字符词法）----------
constexpr const char* kPosXFieldKey = "point-x";
constexpr const char* kPosYFieldKey = "point-y";
constexpr const char* kPosZFieldKey = "point-z";
constexpr const char* kPositionToleranceFieldKey = "position-tolerance";
constexpr const char* kOrientationToleranceFieldKey = "orientation-tolerance";

/**
 * @brief 需求闭包目标的解析结果（纯读——供三接入面共用的一致归属判定）。
 *
 * kind＝条目种类；index＝对应集合 entries 内下标（Root 无下标语义——
 * 恒 0）。与方案 A 自持树/检查器的既有投影同源（同一工作集扫描——零
 * 第二套归属判定语义）。
 */
struct ResolvedTarget {
    enum class Kind { Root, Point, Region, Condition, Plan };
    Kind kind = Kind::Root;
    std::size_t index = 0;  ///< 集合内条目下标（Root 时无意义，恒 0）
};

/**
 * @brief 按对象身份解析需求闭包归属（线性扫描——现取零缓存；工作集条目
 *        按 ObjectId 字典序存放〔I-REQ-1〕，线性序即稳定序）。
 *
 * 解析序：先根（会话态身份比对——根 ObjectId 不在四集合内，须显式比对），
 * 后四集合条目（点→区域→工况→计划，工作集序）。未命中＝nullopt（域外
 * 对象——三接入面统一据此走"非本域应答"二态）。
 *
 * 纯函数（对入参只读）；确定性；不抛。
 */
std::optional<ResolvedTarget>
resolveRequirementTarget(const RequirementWorkingSet& ws,
                         const std::optional<core::ObjectId>& rootOid,
                         const core::ObjectId& object)
{
    // 根比对在前：根是修订闭包锚（会话态身份），选择根节点应答根级页面
    // （导入向导入口——D6 根级操作）。
    if (rootOid.has_value() && *rootOid == object) {
        return ResolvedTarget{ResolvedTarget::Kind::Root, 0};
    }
    for (std::size_t i = 0; i < ws.points.entries.size(); ++i) {
        if (ws.points.entries[i].objectId == object) {
            return ResolvedTarget{ResolvedTarget::Kind::Point, i};
        }
    }
    for (std::size_t i = 0; i < ws.regions.entries.size(); ++i) {
        if (ws.regions.entries[i].objectId == object) {
            return ResolvedTarget{ResolvedTarget::Kind::Region, i};
        }
    }
    for (std::size_t i = 0; i < ws.conditions.entries.size(); ++i) {
        if (ws.conditions.entries[i].objectId == object) {
            return ResolvedTarget{ResolvedTarget::Kind::Condition, i};
        }
    }
    for (std::size_t i = 0; i < ws.plans.entries.size(); ++i) {
        if (ws.plans.entries[i].objectId == object) {
            return ResolvedTarget{ResolvedTarget::Kind::Plan, i};
        }
    }
    return std::nullopt;  // 闭包外身份——域外对象（检查器顺延询问下一域）
}

/// @brief 出口拒绝投递（editSink 缺席＝静默丢弃——纯呈现装配形态的
///        显式声明语义，见 Deps.editSink 字段注）。
void rejectViaSink(IRequirementEditSink* sink, const char* codeToken, std::string detail)
{
    if (sink == nullptr) { return; }
    EditRejection r;
    r.codeToken = codeToken;
    r.detail = std::move(detail);
    sink->onEditRejected(r);
}

}  // namespace

// =====================================================================
// RequirementsTreeNodesProvider
// =====================================================================

RequirementsTreeNodesProvider::RequirementsTreeNodesProvider(RequirementsSharedSurfaceDeps deps)
    : m_deps(std::move(deps))
{
    // 装配缺陷 fail-fast：无编辑器/根身份现取入口的供给者是"永远供空集"
    // 的死面——构造期拒绝优于运行期静默（对端 ProjectTreeModel 同款口径）。
    if (!m_deps.editor || !m_deps.rootObjectId) {
        throw std::invalid_argument(
            "RequirementsTreeNodesProvider: deps.editor/deps.rootObjectId 必填"
            "（空现取入口＝装配缺陷）");
    }
}

std::string RequirementsTreeNodesProvider::domainKey() const
{
    return kRequirementsDomainKey;
}

std::vector<ui::ProjectTreeNode> RequirementsTreeNodesProvider::treeNodes() const
{
    // 无会话＝空集（协议注释的合法常态——不产生组内占位节点）。
    const IRequirementEditor* editor = m_deps.editor();
    if (editor == nullptr) { return {}; }
    const RequirementWorkingSet& ws = editor->workingSet();

    // 条目供给序＝工作集序（点→区域→工况→计划，集合内 ObjectId 字典序
    // ——I-REQ-1，编辑器载入即规范化：单一排序权威，投影不重排）。
    const std::size_t pointCount = ws.points.entries.size();
    const std::size_t regionCount = ws.regions.entries.size();
    const std::size_t conditionCount = ws.conditions.entries.size();
    const std::size_t planCount = ws.plans.entries.size();
    const std::size_t entryTotal = pointCount + regionCount + conditionCount + planCount;

    const std::optional<core::ObjectId> rootOid = m_deps.rootObjectId();
    std::vector<ui::ProjectTreeNode> nodes;
    nodes.reserve(entryTotal + (rootOid.has_value() ? 1u : 0u));

    // 根节点在首位（有根身份时——修订闭包锚；首应用前条目组直属顶层）。
    if (rootOid.has_value()) {
        ui::ProjectTreeNode root;
        root.objectId = *rootOid;
        root.group = ui::ProjectTreeGroup::RequirementObjects;
        root.depth = 0;
        nodes.push_back(std::move(root));
    }

    // 四集合条目按工作集序入树（集合容器对象不入树——acceptance 1 词表
    // 只含"任务点/区域/工况/需求集"；分组折叠由共享树组行承载）。
    for (std::size_t i = 0; i < pointCount; ++i) {
        ui::ProjectTreeNode node;
        node.objectId = ws.points.entries[i].objectId;
        node.group = ui::ProjectTreeGroup::RequirementObjects;
        nodes.push_back(std::move(node));
    }
    for (std::size_t i = 0; i < regionCount; ++i) {
        ui::ProjectTreeNode node;
        node.objectId = ws.regions.entries[i].objectId;
        node.group = ui::ProjectTreeGroup::RequirementObjects;
        nodes.push_back(std::move(node));
    }
    for (std::size_t i = 0; i < conditionCount; ++i) {
        ui::ProjectTreeNode node;
        node.objectId = ws.conditions.entries[i].objectId;
        node.group = ui::ProjectTreeGroup::RequirementObjects;
        nodes.push_back(std::move(node));
    }
    for (std::size_t i = 0; i < planCount; ++i) {
        ui::ProjectTreeNode node;
        node.objectId = ws.plans.entries[i].objectId;
        node.group = ui::ProjectTreeGroup::RequirementObjects;
        nodes.push_back(std::move(node));
    }

    // 根存在时：根 childObjectIds＝全部条目（子引用闭合——条目全部在
    // 本轮供给集内，重建边界 dangling 检查恒过）；条目统一降一级
    // （depth 提示——面板按此缩进）。
    if (rootOid.has_value() && nodes.size() > 1) {
        auto& root = nodes.front();
        root.childObjectIds.reserve(nodes.size() - 1);
        for (std::size_t i = 1; i < nodes.size(); ++i) {
            root.childObjectIds.push_back(nodes[i].objectId);
            nodes[i].depth = 1;
        }
    }
    return nodes;
}

// =====================================================================
// RequirementsPropertyPagesProvider
// =====================================================================

RequirementsPropertyPagesProvider::RequirementsPropertyPagesProvider(
    RequirementsSharedSurfaceDeps deps)
    : m_deps(std::move(deps)), m_outlet(m_deps, *this)
{
    if (!m_deps.editor || !m_deps.rootObjectId) {
        throw std::invalid_argument(
            "RequirementsPropertyPagesProvider: deps.editor/deps.rootObjectId 必填"
            "（空现取入口＝装配缺陷）");
    }
}

std::string RequirementsPropertyPagesProvider::domainKey() const
{
    return kRequirementsDomainKey;
}

std::optional<ui::CommonFieldsPage>
RequirementsPropertyPagesProvider::commonFieldsPage(const core::ObjectId& object) const
{
    const IRequirementEditor* editor = m_deps.editor();
    if (editor == nullptr) { return std::nullopt; }  // 无会话——非本域应答（诚实二态）

    // 域判定在域：对象归属由工作集自答（命中任务点才应答常用字段页——
    // 检查器零需求类型知识，first-wins 询问序由检查器编排）。
    const RequirementWorkingSet& ws = editor->workingSet();
    const auto target = resolveRequirementTarget(ws, m_deps.rootObjectId(), object);
    if (!target.has_value() || target->kind != ResolvedTarget::Kind::Point) {
        // 区域/工况/计划/根：v1 无常用字段页（区域定义/导入向导收口复杂
        // 页——D6 点名；工况/计划不虚构字段，登记单元卡 §14.6）。
        return std::nullopt;
    }

    const TaskPoint& p = ws.points.entries.at(target->index);
    const core::UnitToken m = core::UnitToken::find("m").value();
    const core::UnitToken rad = core::UnitToken::find("rad").value();

    ui::CommonFieldsPage page;
    page.title = kPointCommonPageTitle;
    page.readOnly = false;  // 草稿编辑流可达（出口转译域编辑——见 outlet）

    // 受约束位置三分量（SourcedValue 四态——"未提供"合法不转零〔MDL-06
    // 同源〕：未提供时不供位置字段，页面以双容差保底非空；坐标语义＝
    // refFrame 参考系下表示〔I-REQ-4 token 匹配——字段标签注明〕）。
    if (const auto position = p.pose.position.tryValue()) {
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kPosXFieldKey, "位置 X（m，参考系内）", core::QuantityKind::Length, m, m));
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kPosYFieldKey, "位置 Y（m，参考系内）", core::QuantityKind::Length, m, m));
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kPosZFieldKey, "位置 Z（m，参考系内）", core::QuantityKind::Length, m, m));
        page.values.push_back({kPosXFieldKey, (*position)[0]});
        page.values.push_back({kPosYFieldKey, (*position)[1]});
        page.values.push_back({kPosZFieldKey, (*position)[2]});
    }
    // 双容差（要求值——I-REQ-5：>0 且有限；默认值由模型给定，恒已设）。
    page.fields.push_back(ui::makeQuantityFieldSpec(
        kPositionToleranceFieldKey, "位置容差（m）", core::QuantityKind::Length, m, m));
    page.fields.push_back(ui::makeQuantityFieldSpec(
        kOrientationToleranceFieldKey, "姿态容差（rad）", core::QuantityKind::Angle, rad, rad));
    page.values.push_back({kPositionToleranceFieldKey, p.tolerance.positionTolerance});
    page.values.push_back({kOrientationToleranceFieldKey, p.tolerance.orientationTolerance});

    page.editOutlet = m_deps.editSink != nullptr ? &m_outlet : nullptr;
    if (m_deps.editSink != nullptr) {
        m_outlet.bindTarget(target->index);  // 出口绑定供给时刻的目标任务点
    }
    return page;
}

std::vector<ui::ComplexPageEntry>
RequirementsPropertyPagesProvider::complexPageEntries(const core::ObjectId& object) const
{
    const IRequirementEditor* editor = m_deps.editor();
    if (editor == nullptr) { return {}; }  // 无会话＝非本域应答（协议二态）

    const RequirementWorkingSet& ws = editor->workingSet();
    const auto target = resolveRequirementTarget(ws, m_deps.rootObjectId(), object);
    if (!target.has_value()) { return {}; }

    // 每类对象一个收口页入口（hosted=false——D6"域面板内"形态：激活即
    // 聚焦域面板该对象的编辑视图；字段内容零进协议——入口只声明"有页
    // 可激活"）。
    ui::ComplexPageEntry e;
    switch (target->kind) {
    case ResolvedTarget::Kind::Point:
        e.pageKey = kPointEditorPageKey;
        e.title = kPointEditorTitle;
        break;
    case ResolvedTarget::Kind::Region:
        e.pageKey = kRegionDefinitionPageKey;
        e.title = kRegionDefinitionTitle;
        break;
    case ResolvedTarget::Kind::Condition:
        e.pageKey = kConditionEditorPageKey;
        e.title = kConditionEditorTitle;
        break;
    case ResolvedTarget::Kind::Plan:
        e.pageKey = kPlanEditorPageKey;
        e.title = kPlanEditorTitle;
        break;
    case ResolvedTarget::Kind::Root:
        e.pageKey = kImportWizardPageKey;
        e.title = kImportWizardTitle;
        break;
    }
    e.hosted = false;
    std::vector<ui::ComplexPageEntry> entries;
    entries.push_back(std::move(e));
    return entries;
}

ui::ComplexPageActivationReport
RequirementsPropertyPagesProvider::activateComplexPage(const core::ObjectId& object,
                                                       const std::string& pageKey,
                                                       QWidget* parent)
{
    (void)parent;  // 自持页（hosted=false）忽略宿装父——签名兼容保留

    ui::ComplexPageActivationReport report;
    const IRequirementEditor* editor = m_deps.editor();
    const auto target = editor != nullptr
        ? resolveRequirementTarget(editor->workingSet(), m_deps.rootObjectId(), object)
        : std::nullopt;

    // ①寻址核对：pageKey 必须与该对象已声明的入口一致（检查器编排面已查，
    //   本处为 Provider 侧防御——两处同 token，拒绝语义一致）。
    bool known = false;
    if (target.has_value()) {
        switch (target->kind) {
        case ResolvedTarget::Kind::Point:
            known = pageKey == kPointEditorPageKey;
            break;
        case ResolvedTarget::Kind::Region:
            known = pageKey == kRegionDefinitionPageKey;
            break;
        case ResolvedTarget::Kind::Condition:
            known = pageKey == kConditionEditorPageKey;
            break;
        case ResolvedTarget::Kind::Plan:
            known = pageKey == kPlanEditorPageKey;
            break;
        case ResolvedTarget::Kind::Root:
            known = pageKey == kImportWizardPageKey;
            break;
        }
    }
    if (!known) {
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
// RequirementsPropertyPagesProvider::PointCommonFieldsOutlet
// =====================================================================

RequirementsPropertyPagesProvider::PointCommonFieldsOutlet::PointCommonFieldsOutlet(
    const RequirementsSharedSurfaceDeps& deps, const RequirementsPropertyPagesProvider& owner)
    : m_deps(deps), m_owner(owner)
{
}

bool RequirementsPropertyPagesProvider::PointCommonFieldsOutlet::submitKey(
    const std::string& key, double newSi)
{
    // 现取编辑器与分流出口（任一缺失＝编辑通道未装配——出口拒绝，不虚构；
    // editSink 缺席时本出口根本不会暴露在页面上——此为防御面）。
    IRequirementEditor* editor = m_deps.editor ? m_deps.editor() : nullptr;
    if (editor == nullptr) {
        rejectViaSink(m_deps.editSink, "no-session", "无会话工作集，编辑未应用");
        return false;
    }
    if (m_deps.editSink == nullptr) {
        return false;  // 无分流出口＝编辑通道未装配（纯呈现形态——不提交）
    }

    // 读当前任务点（按绑定下标——页供给时刻刷新；工作集是编辑器权威，
    // 零副本现取）。
    const RequirementWorkingSet& ws = editor->workingSet();
    if (m_pointIndex >= ws.points.entries.size()) {
        // 选中漂移防御：供给后条目被删除（他路编辑）——下标失效即拒绝，
        // 不越界不虚构（检查器随选中刷新会重供给新页）。
        rejectViaSink(m_deps.editSink, "target-missing", "目标任务点已不存在，编辑未应用");
        return false;
    }
    TaskPoint updated = ws.points.entries[m_pointIndex];  // 值拷贝——整体提交的底稿

    if (key == kPosXFieldKey || key == kPosYFieldKey || key == kPosZFieldKey) {
        // 位置分量：单分量替换整组提交（position 是整体 SourcedValue——
        // 三分量一次提供〔Provided 时三分量必须有限〕；未提供＝拒绝，
        // 完整位置在域面板定义，不虚构另外两分量）。
        const auto currentPosition = updated.pose.position.tryValue();
        if (!currentPosition.has_value()) {
            rejectViaSink(m_deps.editSink, "position-not-provided",
                          "该任务点受约束位置未提供，请先在域面板完整定义位置");
            return false;
        }
        rw::math::Vector3D<double> next = *currentPosition;
        // 分量替换经下标访问（Vector3D 的 x()/y()/z() 是静态轴向量工厂
        // 而非分量访问器——框架 API 事实，改值走 operator[] 可变重载）。
        if (key == kPosXFieldKey) {
            next[0] = newSi;
        } else if (key == kPosYFieldKey) {
            next[1] = newSi;
        } else {
            next[2] = newSi;
        }
        // 改值保源（provided 工厂重建——来源标记沿用原值 provenance：
        // 就地编辑不改变 UserProvided/ImportMapped 语义，导入溯源不被
        // 覆写——I-REQ-8 无回写纪律的出口侧落实）。
        updated.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            next, updated.pose.position.provenance());
        // 单值直投域编辑流（接受/拒绝分流与就地呈现由 PanelEditFlow 经
        // sink 完成——域裁决唯一，本出口零判定）。
        submitEntryEdit(*editor, *m_deps.editSink, updated);
        return true;
    }
    if (key == kPositionToleranceFieldKey) {
        updated.tolerance.positionTolerance = newSi;
        submitEntryEdit(*editor, *m_deps.editSink, updated);
        return true;
    }
    if (key == kOrientationToleranceFieldKey) {
        updated.tolerance.orientationTolerance = newSi;
        submitEntryEdit(*editor, *m_deps.editSink, updated);
        return true;
    }
    // 未知键：检查器表单只会回传本页字段的键——此分支为防御面。
    rejectViaSink(m_deps.editSink, "unknown-field", "未知字段键：" + key);
    return false;
}

void RequirementsPropertyPagesProvider::PointCommonFieldsOutlet::applyEdits(
    const ui::ParamEditSet& editSet)
{
    // 逐字段转译（键序＝确认区注册序；单字段失败不阻断其余字段——
    // 与 ParamEditModel 的逐行编辑语义一致；域拒绝就地呈现）。
    for (const ui::ParamChange& change : editSet.changes) {
        submitKey(change.key, change.newSi);
    }
}

// =====================================================================
// RequirementsSelectionAdapter
// =====================================================================

RequirementsSelectionAdapter::RequirementsSelectionAdapter(RequirementsSharedSurfaceDeps deps)
    : m_deps(std::move(deps))
{
    if (!m_deps.editor || !m_deps.rootObjectId) {
        throw std::invalid_argument(
            "RequirementsSelectionAdapter: deps.editor/deps.rootObjectId 必填"
            "（空现取入口＝装配缺陷）");
    }
}

void RequirementsSelectionAdapter::attach(ui::SelectionService& service)
{
    detach();  // 幂等收口：重复 attach 先退订既有订阅
    m_service = &service;
    m_subscription = service.subscribe(*this);  // RAII 句柄——析构/detach 即退订
}

void RequirementsSelectionAdapter::detach() noexcept
{
    m_subscription.reset();  // 句柄析构即退订（幂等）
    m_service = nullptr;
}

void RequirementsSelectionAdapter::onSelectionChanged(const ui::SelectionChange& change)
{
    // runtimeOnly（L3 反解失败暂态）：业务选中集未变——零触碰（不误清
    // 既有面板高亮，SelectionChange 两态语义的适配器侧落实）。
    if (change.runtimeOnly) { return; }

    // 业务单选且命中需求闭包→面板高亮/定位；多选/清空→对称清除。
    // 闭包外对象（他域选中）→不动面板（域间联动零串扰——各域适配器
    // 只对本域对象动作）。
    if (change.selectedObjectIds.size() == 1 && m_deps.editor()) {
        const auto target = resolveRequirementTarget(m_deps.editor()->workingSet(),
                                                     m_deps.rootObjectId(),
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

bool RequirementsSelectionAdapter::reportView3DPick(const core::ObjectId& oid)
{
    // 前置过滤：未订阅/无效身份（全零保留值）/闭包外对象一律不上报
    // ——selectBusiness 对无效身份 fail-fast，域侧先过滤（拾取未命中
    // 本域是常态，不出诊断——协议 selectBusiness 契约的调用方义务）。
    if (m_service == nullptr || !m_deps.editor() || !oid.isValid()) {
        return false;
    }
    if (!resolveRequirementTarget(m_deps.editor()->workingSet(), m_deps.rootObjectId(),
                                  oid).has_value()) {
        return false;
    }
    m_service->selectBusiness({oid}, ui::SelectionSource::View3DPick);
    return true;
}

}  // namespace requirements
}  // namespace ird
}  // namespace sdurws
