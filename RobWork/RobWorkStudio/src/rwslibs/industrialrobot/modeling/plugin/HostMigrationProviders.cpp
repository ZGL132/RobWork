/**
 * @file   HostMigrationProviders.cpp
 * @brief  建模域宿主迁移三接入面实现——协议值 ← 工作集投影的搬运层
 *         （头注为契约权威；本文件只做"现取现拼"与"键→域编辑流"转译，
 *         零业务判定——插件零计算逻辑红线，DTB §5.3）。
 */

#include "HostMigrationProviders.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/core/Provenance.hpp>  // core::ProvenanceKind/SourcedValue（来源标记呈现——UI-T50 acceptance 4）
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
constexpr const char* kObjectPropertiesPageTitle = "在域面板编辑";
// UI-T50 四对象扩展页（§9.7.5 增量边界兑现——"v1 无页面供给"诚实二态
// 对四对象撤牌；readOnly 事实页形态与连杆页同款——P-UI-6 单侧纪律）。
constexpr const char* kToolCommonPageTitle = "工具常用参数（只读）";
constexpr const char* kSceneCommonPageTitle = "场景对象常用参数（只读）";
constexpr const char* kPoseSetCommonPageTitle = "命名位姿集概要（只读）";
constexpr const char* kDrivetrainCommonPageTitle = "传动设计概要（只读·首关节）";

// ---- 字段稳定键（基线注入与批量粘贴的寻址锚——小写连字符词法）----------
constexpr const char* kZeroOffsetFieldKey = "zero-offset";
constexpr const char* kLowerLimitFieldKey = "joint-lower-limit";
constexpr const char* kUpperLimitFieldKey = "joint-upper-limit";
constexpr const char* kMassFieldKey = "mass";
// UI-T50 四页字段键（页内唯一——跨页允许复用同键名〔每页独立寻址空间〕，
// 但为 Dev 日志可读性仍带对象前缀）。
constexpr const char* kToolMountXKey = "tool-mount-x";
constexpr const char* kToolMountYKey = "tool-mount-y";
constexpr const char* kToolMountZKey = "tool-mount-z";
constexpr const char* kToolMountRollKey = "tool-mount-r";
constexpr const char* kToolMountPitchKey = "tool-mount-p";
constexpr const char* kToolMountYawKey = "tool-mount-y";
constexpr const char* kToolTcpCountKey = "tool-tcp-count";
constexpr const char* kScenePoseXKey = "scene-pose-x";
constexpr const char* kScenePoseYKey = "scene-pose-y";
constexpr const char* kScenePoseZKey = "scene-pose-z";
constexpr const char* kScenePoseRollKey = "scene-pose-r";
constexpr const char* kScenePosePitchKey = "scene-pose-p";
constexpr const char* kScenePoseYawKey = "scene-pose-y";
constexpr const char* kPoseEntryCountKey = "pose-entry-count";
constexpr const char* kPoseHomeSetKey = "pose-home-set";
constexpr const char* kPoseZeroSetKey = "pose-zero-set";
constexpr const char* kDtJointCountKey = "dt-joint-count";
constexpr const char* kDtJ1RatioKey = "dt-j1-ratio";
constexpr const char* kDtJ1CoulombKey = "dt-j1-coulomb";
constexpr const char* kDtJ1RatedKey = "dt-j1-rated";
constexpr const char* kDtJ1PeakKey = "dt-j1-peak";

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
    case core::QuantityKind::Torque: return core::UnitToken::find("N*m").value();
    default: break;
    }
    // 词表内建模字段只出现上述量纲——不可达分支以 Dimensionless 兜底
    // （fail-fast 交给 makeQuantityFieldSpec 的量纲核对）。
    return core::UnitToken::find("1").value();
}

/**
 * @brief 旋转矩阵 → ZYX 欧拉角反解（RPY——与 Import.cpp rpyToRotation
 *        正解互逆；UI-T50 只读事实页的位姿六值呈现用）。
 *
 * ★ 逐元素解析实现——不调用 rw::math::RPY 构造（冒烟 header-only 纪律：
 *   RPY 构造是框架外联符号，F-480 同族——requirements 侧已实测链接失败，
 *   本文件禁重蹈）。正解约定 R＝Rz(yaw)·Ry(pitch)·Rx(roll)（GeometryLinkEdit
 *   的正解同式），反解：pitch＝-asin(R20)，roll＝atan2(R21,R22)，
 *   yaw＝atan2(R10,R00)；|R20|≈1 奇异（万向锁）时 roll＝0、yaw 改由
 *   atan2(-R01,R11) 确定（确定性特例——与 Template.cpp 占位圆柱特例同款
 *   纪律）。
 *
 * @param R [in] 旋转矩阵（连杆/工具系约定内使用——本函数不涉参考系语义）
 * @return {roll, pitch, yaw}，单位 rad
 */
std::array<double, 3> rotationToRpy(const rw::math::Rotation3D<double>& R)
{
    const double pitch = -std::asin(std::clamp(R(2, 0), -1.0, 1.0));
    const double cp = std::cos(pitch);
    double roll;
    double yaw;
    if (std::abs(cp) > 1e-12) {
        roll = std::atan2(R(2, 1), R(2, 2));
        yaw = std::atan2(R(1, 0), R(0, 0));
    } else {
        // 万向锁：roll/yaw 共线不可分——确定性取 roll=0（工程惯例）。
        roll = 0.0;
        yaw = std::atan2(-R(0, 1), R(1, 1));
    }
    return {roll, pitch, yaw};
}

/**
 * @brief SourcedValue 来源标记的标签后缀（acceptance 4"逐项 SourcedValue
 *        来源标记呈现"的承载——D5 协议无来源徽标字段〔QuantityFieldSpec
 *        是数值面〕，标记经 label 文本呈现：供给时刻按值态现拼，检查器
 *        零感知）。
 *
 * 中文映射（UX-02 工程用语——provenanceKindToken 的呈现面翻译）。
 */
std::string provenanceLabelSuffix(const core::SourcedValue<double>& v)
{
    // 非提供态诚实呈现（Invalid＝原始输入保留态——与"未提供"区分，
    // 不把非法值粉饰成缺值）。
    if (v.state() == core::FieldState::Invalid) { return "（值非法）"; }
    if (v.state() != core::FieldState::Provided) { return "（未提供）"; }
    switch (v.provenance().kind) {
    case core::ProvenanceKind::UserProvided: return "（来源＝用户输入）";
    case core::ProvenanceKind::ImportMapped: return "（来源＝导入映射）";
    case core::ProvenanceKind::CatalogBackfill: return "（来源＝目录回填）";
    case core::ProvenanceKind::GeometricEstimate: return "（来源＝几何估算）";
    case core::ProvenanceKind::DerivedReadOnly: return "（来源＝派生只读）";
    }
    return "（来源＝未知）";
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
    // =================================================================
    // UI-T50 四对象扩展页（§9.7.5 增量边界兑现——"v1 无页面供给"诚实二态
    // 对工具/场景/位姿集/传动撤牌）。形态统一＝**只读事实页**（readOnly=
    // true＋零移交面——P-UI-6 单侧纪律，与连杆页同款）：四对象的编辑流
    // 是列表/合并/命令流结构（TCP 列表 MDL-13 不变量、命名位姿合并流
    // D-MDL-3、传动逐关节批量），不是"少量高频标量"——D5 分野语义下不
    // 在共享检查器展开，D6 入口"在域面板编辑"收口（complexPageEntries
    // 同步扩展）。D-MDL-10：位姿集/传动页零即时编译语义（只读呈现零编
    // 译触发，结构面即证）。
    // =================================================================
    if (target->kind == SelectedTarget::Kind::Tool) {
        // ---- 工具页（§4.4：安装接口位姿六值＋TCP 计数＋质量——TCP 明细/
        //      物性编辑收口域面板）。
        const ToolDefinition& t = ws->toolObjects.at(target->index);
        const core::UnitToken m = unitTokenFor(core::QuantityKind::Length);
        const core::UnitToken rad = unitTokenFor(core::QuantityKind::Angle);
        const core::UnitToken kg = unitTokenFor(core::QuantityKind::Mass);
        page.title = kToolCommonPageTitle;
        page.readOnly = true;
        const auto rpy = rotationToRpy(t.mountInterface.R());
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kToolMountXKey, "安装接口 X", core::QuantityKind::Length, m, m));
        page.values.push_back({kToolMountXKey, t.mountInterface.P()[0]});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kToolMountYKey, "安装接口 Y", core::QuantityKind::Length, m, m));
        page.values.push_back({kToolMountYKey, t.mountInterface.P()[1]});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kToolMountZKey, "安装接口 Z", core::QuantityKind::Length, m, m));
        page.values.push_back({kToolMountZKey, t.mountInterface.P()[2]});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kToolMountRollKey, "安装接口 R（roll）", core::QuantityKind::Angle, rad, rad));
        page.values.push_back({kToolMountRollKey, std::get<0>(rpy)});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kToolMountPitchKey, "安装接口 P（pitch）", core::QuantityKind::Angle, rad, rad));
        page.values.push_back({kToolMountPitchKey, std::get<1>(rpy)});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kToolMountYawKey, "安装接口 Y（yaw）", core::QuantityKind::Angle, rad, rad));
        page.values.push_back({kToolMountYawKey, std::get<2>(rpy)});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kToolTcpCountKey, "TCP 数量", core::QuantityKind::Dimensionless,
            unitTokenFor(core::QuantityKind::Dimensionless),
            unitTokenFor(core::QuantityKind::Dimensionless), std::nullopt, true));
        page.values.push_back(
            {kToolTcpCountKey, static_cast<double>(t.tcpList.size())});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kMassFieldKey, "质量", core::QuantityKind::Mass, kg, kg));
        if (const auto mass = t.body.mass.tryValue()) {
            page.values.push_back({kMassFieldKey, *mass});
        }
        page.editOutlet = nullptr;
        return page;
    }
    if (target->kind == SelectedTarget::Kind::Scene) {
        // ---- 场景页（§4.5：世界系固连位姿六值——M-11 不预乘安装旋转，
        //      呈现即字节原值；role 词表是枚举非数值面，D6 入口承载）。
        const SceneObject& s = ws->sceneObjects.at(target->index);
        const core::UnitToken m = unitTokenFor(core::QuantityKind::Length);
        const core::UnitToken rad = unitTokenFor(core::QuantityKind::Angle);
        page.title = kSceneCommonPageTitle;
        page.readOnly = true;
        const auto rpy = rotationToRpy(s.worldPose.R());
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kScenePoseXKey, "世界位姿 X", core::QuantityKind::Length, m, m));
        page.values.push_back({kScenePoseXKey, s.worldPose.P()[0]});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kScenePoseYKey, "世界位姿 Y", core::QuantityKind::Length, m, m));
        page.values.push_back({kScenePoseYKey, s.worldPose.P()[1]});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kScenePoseZKey, "世界位姿 Z", core::QuantityKind::Length, m, m));
        page.values.push_back({kScenePoseZKey, s.worldPose.P()[2]});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kScenePoseRollKey, "世界位姿 R（roll）", core::QuantityKind::Angle, rad, rad));
        page.values.push_back({kScenePoseRollKey, std::get<0>(rpy)});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kScenePosePitchKey, "世界位姿 P（pitch）", core::QuantityKind::Angle, rad, rad));
        page.values.push_back({kScenePosePitchKey, std::get<1>(rpy)});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kScenePoseYawKey, "世界位姿 Y（yaw）", core::QuantityKind::Angle, rad, rad));
        page.values.push_back({kScenePoseYawKey, std::get<2>(rpy)});
        page.editOutlet = nullptr;
        return page;
    }
    if (target->kind == SelectedTarget::Kind::PoseSet) {
        // ---- 位姿集页（§4.6：条目计数＋保留键存在性——条目关节角向量
        //      是不定长向量非 D5 数值面，编辑走 apply-named-poses 合并流
        //      〔D-MDL-3 保留键语义——域面板收口〕）。
        const PoseSet& p = *ws->poseSetObject;
        const core::UnitToken one = unitTokenFor(core::QuantityKind::Dimensionless);
        page.title = kPoseSetCommonPageTitle;
        page.readOnly = true;
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kPoseEntryCountKey, "位姿条目数", core::QuantityKind::Dimensionless,
            one, one, std::nullopt, true));
        page.values.push_back(
            {kPoseEntryCountKey, static_cast<double>(p.entries.size())});
        // 保留键存在性（0/1 计数呈现——homeConfiguration/zeroConfiguration
        // 是编辑器"复位 Home/Zero"会话命令的目标参考，KIN-06）。
        const bool homeSet = std::any_of(
            p.entries.begin(), p.entries.end(),
            [](const PoseSetEntry& e) { return e.key == "homeConfiguration"; });
        const bool zeroSet = std::any_of(
            p.entries.begin(), p.entries.end(),
            [](const PoseSetEntry& e) { return e.key == "zeroConfiguration"; });
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kPoseHomeSetKey, "Home 保留键已设（0/1）", core::QuantityKind::Dimensionless,
            one, one, std::nullopt, true));
        page.values.push_back({kPoseHomeSetKey, homeSet ? 1.0 : 0.0});
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kPoseZeroSetKey, "Zero 保留键已设（0/1）", core::QuantityKind::Dimensionless,
            one, one, std::nullopt, true));
        page.values.push_back({kPoseZeroSetKey, zeroSet ? 1.0 : 0.0});
        page.editOutlet = nullptr;
        return page;
    }
    if (target->kind == SelectedTarget::Kind::Drivetrain) {
        // ---- 传动页（§4.7：链计数＋首关节样例——逐关节全链呈现受 D5
        //      16 字段哨兵约束〔六轴链 ratio6＋摩擦18＋力矩12 远超〕，取
        //      J1 样例＋计数，全链编辑收口域面板）。逐项来源标记经 label
        //      后缀呈现（acceptance 4——SourcedValue 态现拼，协议零改动）。
        //      ★ 黏滞摩擦 fv（N·m·s/rad）量纲不在 core QuantityKind 词表
        //      ——不虚构量纲，本页缺席（F-483 登记，随 core 单位表扩展
        //      任务补齐）。
        const DrivetrainDesign& d = *ws->drivetrainObject;
        const core::UnitToken one = unitTokenFor(core::QuantityKind::Dimensionless);
        const core::UnitToken nm = unitTokenFor(core::QuantityKind::Torque);
        page.title = kDrivetrainCommonPageTitle;
        page.readOnly = true;
        page.fields.push_back(ui::makeQuantityFieldSpec(
            kDtJointCountKey, "传动链关节数", core::QuantityKind::Dimensionless,
            one, one, std::nullopt, true));
        page.values.push_back(
            {kDtJointCountKey, static_cast<double>(d.ratioPerJoint.size())});
        if (!d.ratioPerJoint.empty()) {
            page.fields.push_back(ui::makeQuantityFieldSpec(
                kDtJ1RatioKey,
                "J1 减速比" + provenanceLabelSuffix(d.ratioPerJoint.front()),
                core::QuantityKind::Dimensionless, one, one));
            if (const auto ratio = d.ratioPerJoint.front().tryValue()) {
                page.values.push_back({kDtJ1RatioKey, *ratio});
            }
        }
        if (!d.frictionPerJoint.empty()) {
            page.fields.push_back(ui::makeQuantityFieldSpec(
                kDtJ1CoulombKey,
                "J1 库仑摩擦 fc" + provenanceLabelSuffix(d.frictionPerJoint.front().coulomb),
                core::QuantityKind::Torque, nm, nm));
            if (const auto fc = d.frictionPerJoint.front().coulomb.tryValue()) {
                page.values.push_back({kDtJ1CoulombKey, *fc});
            }
        }
        if (!d.torqueLimitsPerJoint.empty()) {
            page.fields.push_back(ui::makeQuantityFieldSpec(
                kDtJ1RatedKey,
                "J1 额定力矩" + provenanceLabelSuffix(d.torqueLimitsPerJoint.front().rated),
                core::QuantityKind::Torque, nm, nm));
            if (const auto rated = d.torqueLimitsPerJoint.front().rated.tryValue()) {
                page.values.push_back({kDtJ1RatedKey, *rated});
            }
            page.fields.push_back(ui::makeQuantityFieldSpec(
                kDtJ1PeakKey,
                "J1 峰值力矩" + provenanceLabelSuffix(d.torqueLimitsPerJoint.front().peak),
                core::QuantityKind::Torque, nm, nm));
            if (const auto peak = d.torqueLimitsPerJoint.front().peak.tryValue()) {
                page.values.push_back({kDtJ1PeakKey, *peak});
            }
        }
        page.editOutlet = nullptr;
        return page;
    }
    // 其余建模对象（根/基座）：v1 无页面供给维持（UI-T50 实施段决议——
    // G8 增量词表只点名工具/场景/位姿集/传动四对象；根的 displayName/
    // 权威模式与基座安装布置的编辑面在域面板模板区/基座区既有，非"少量
    // 高频标量"D5 语义——诚实边界维持，决议登记 ui.md §13 UI-T50 行）。
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
    } else if (target->kind == SelectedTarget::Kind::Tool
               || target->kind == SelectedTarget::Kind::Scene
               || target->kind == SelectedTarget::Kind::PoseSet
               || target->kind == SelectedTarget::Kind::Drivetrain) {
        // UI-T50 四对象统一入口："在域面板编辑"——列表/合并/命令流编辑
        // （TCP 列表/role 词表/位姿合并/传动逐关节）收口域面板属性区。
        ui::ComplexPageEntry e;
        e.pageKey = kObjectPropertiesPageKey;
        e.title = kObjectPropertiesPageTitle;
        e.hosted = false;  // 同上——激活聚焦建模面板该对象
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
    //   本处为 Provider 侧防御——两处同 token，拒绝语义一致）。UI-T50 增
    //   四对象统一入口 object-properties（工具/场景/位姿集/传动——域面板
    //   编辑收口，激活语义与 DH/物性页同款 focusObject 定位）。
    const bool isDh = target.has_value()
        && target->kind == SelectedTarget::Kind::Joint
        && pageKey == kDhParametersPageKey;
    const bool isProperties = target.has_value()
        && target->kind == SelectedTarget::Kind::Link
        && pageKey == kPropertiesPageKey;
    const bool isObjectProperties = target.has_value()
        && (target->kind == SelectedTarget::Kind::Tool
            || target->kind == SelectedTarget::Kind::Scene
            || target->kind == SelectedTarget::Kind::PoseSet
            || target->kind == SelectedTarget::Kind::Drivetrain)
        && pageKey == kObjectPropertiesPageKey;
    if (!isDh && !isProperties && !isObjectProperties) {
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
