/**
 * @file   ModelDiff.cpp
 * @brief  IModelDiffService 唯一实现——两个 RobotDesign 的逐字段比较编排
 *         与差异增量表装配（MDL-08；WP-13-T14）。
 *
 * 设计依据：units/modeling.md §9.4.9（IModelDiffService 签名行）、§4.3/
 * §4.3-A/§4.3-B（字段表行序＝组内排序的"字段序"）、§4.8（引用表/资源
 * 清单的 canonical 集合规范化语义）、§14.2 D-MDL-5（派生字段不入差异面）、
 * 需求 MDL-08/NFR-COR-02/ARC-04；任务契约 WP-13-T14 acceptance 1～4。
 *
 * 实现结构（三段，全部确定性）：
 *   ① 值渲染层（匿名 namespace renderValue 重载族）——各字段值的确定性
 *      文本摘要：classic locale（与全局 locale 解耦，§3.4 不读 locale）＋
 *      17 位有效十进制（IEEE754 double 的无损往返位数——同值必同串）；
 *   ② 比较层（compareSourced 模板＋各对象段比较函数）——权威受管字段
 *      仅在两侧同为权威时比较（D-MDL-5 差异面=编码身份面）；集合语义
 *      字段（toolRefs/sceneRefs/resourceManifest）按 canonical 键集比较
 *      （§4.8——顺序在编码层即被规范化，顺序差异不是身份差异）；
 *   ③ 装配层（DiffBuilder）——条目统一入组，最后按（对象 id 规范文本→
 *      字段序→定位路径→变化态）stable_sort（键含路径与三态，键全等的
 *      条目只能是重复发射的同一条目，稳定排序保证输出确定）。
 */

#include <sdurws/ird/modeling/ModelDiff.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <rw/math/Rotation3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/Provenance.hpp>  // FieldState/ValueProvenance/toToken（报告渲染面）

namespace sdurws::ird::modeling {

// ---- 稳定 token（switch 全枚举、无 default——新增枚举值漏登记时编译器
// 告警暴露；词表见 ModelDiff.hpp 各枚举注）----

std::string_view modelDiffGroupToken(ModelDiffGroup group) noexcept
{
    switch (group) {
    case ModelDiffGroup::Structure: return "structure";
    case ModelDiffGroup::Parameters: return "parameters";
    case ModelDiffGroup::Properties: return "properties";
    }
    return "structure";  // 不可达（全枚举已穷尽——防御性收敛，消除告警路径）
}

std::string_view modelDiffChangeKindToken(ModelDiffChangeKind kind) noexcept
{
    switch (kind) {
    case ModelDiffChangeKind::Added: return "added";
    case ModelDiffChangeKind::Removed: return "removed";
    case ModelDiffChangeKind::Modified: return "modified";
    }
    return "modified";  // 不可达（同上）
}

namespace {

// =====================================================================
// 字段序（组内排序键第二段——§4.3/§4.3-A/§4.3-B 字段表行序的序数承载；
// 数值间隔仅为人读可辨，比较只用相对次序。用例以期望条目序列机械钉住，
// 表序调整即用例失败——与 Codec"字段声明序=编解码字段序"同一防漂移纪律）
// =====================================================================

enum FieldOrder : unsigned {
    kRootSchemaVersion = 10,
    kRootDisplayName = 20,
    kRootAuthority = 30,
    kBasePreset = 40,
    kBaseCustomEaa = 50,
    kBasePosition = 60,
    kJointMembership = 70,   // 整关节增删（条目即对象本身）
    kJointChainIndex = 75,   // 链序位变化（匹配对象两侧下标不同）
    kJointLocalName = 90,
    kJointType = 100,
    kJointAxis = 110,
    kJointOrigin = 120,
    kJointZeroOffset = 130,
    kJointBounds = 140,
    kJointWorkingRange = 150,
    kJointDhDerived = 160,
    kLinkMembership = 170,
    kLinkChainIndex = 175,
    kLinkLocalName = 180,
    kLinkMass = 190,
    kLinkCenterOfMass = 200,
    kLinkInertia = 210,
    kLinkMaterial = 220,
    kLinkVisual = 230,
    kLinkCollision = 240,
    kDefaultTcp = 250,
    kToolRefs = 260,
    kSceneRefs = 270,
    kPoseSetRef = 280,
    kDrivetrainRef = 290,
    kResourceManifest = 300,
    kNotes = 310,
};

// =====================================================================
// 值渲染层（确定性文本摘要——固定模板；空串约定＝"该侧不存在"）
// =====================================================================

/// double→文本：classic locale（十进制点不随系统 locale——§3.4 确定性）＋
/// 17 位有效数字（IEEE754 double 无损往返上限——同值必同串、异值必异串）。
std::string fmtDouble(double v)
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::setprecision(17) << v;
    return os.str();
}

/// 整数→文本（十进制；classic locale 同上——用于 schemaVersion/链序位）。
std::string fmtUint(unsigned long long v)
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << v;
    return os.str();
}

/// 三维向量→"(x,y,z)"（分量经 fmtDouble——单位由字段语义承载，不入串）。
std::string renderVector(const rw::math::Vector3D<double>& v)
{
    return "(" + fmtDouble(v[0]) + "," + fmtDouble(v[1]) + "," + fmtDouble(v[2]) + ")";
}

/// 位姿→"T{p=(..),R=(9 元行主序)}"（JointPose——平移 m/旋转无量纲，
/// core.md §4.6 T_ab 约定；逐元素渲染保证同位姿必同串）。
std::string renderPose(const JointPose& t)
{
    std::string out = "T{p=" + renderVector(t.d()) + ",R=(";
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (r != 0 || c != 0) { out += ","; }
            out += fmtDouble(t.r()(r, c));
        }
    }
    out += ")}";
    return out;
}

/// 限位/工作范围→"[qmin,qmax]"（rad 或 m——随关节类型，§4.3-A）。
std::string renderLimits(const JointLimits& lim)
{
    return "[" + fmtDouble(lim.first) + "," + fmtDouble(lim.second) + "]";
}

/// DH 参数→"DH{theta=..,d=..,a=..,alpha=..}"（thetaOffset/alpha 单位 rad、
/// d/a 单位 m——§7.4；ASCII 字段名便于机器判读）。
std::string renderDh(const DhParameters& dh)
{
    return "DH{theta=" + fmtDouble(dh.thetaOffset) + ",d=" + fmtDouble(dh.d)
        + ",a=" + fmtDouble(dh.a) + ",alpha=" + fmtDouble(dh.alpha) + "}";
}

/// 惯量张量→"I{ixx=..,..}"（kg·m²——§4.3-B；六分量全渲染）。
std::string renderInertia(const InertiaTensor& i)
{
    return "I{ixx=" + fmtDouble(i.ixx) + ",iyy=" + fmtDouble(i.iyy)
        + ",izz=" + fmtDouble(i.izz) + ",ixy=" + fmtDouble(i.ixy)
        + ",ixz=" + fmtDouble(i.ixz) + ",iyz=" + fmtDouble(i.iyz) + "}";
}

/// 来源记录→"<kind>[/<methodTag>][+src=<oid>][@<cv>]"（kind 经 core::toToken
/// 单一权威；可选段按存在性追加——同记录必同串）。
std::string renderProvenance(const core::ValueProvenance& p)
{
    std::string out = core::toToken(p.kind);
    if (p.methodTag.has_value()) { out += "/" + *p.methodTag; }
    if (p.sourceObject.has_value()) { out += "+src=" + p.sourceObject->toCanonical(); }
    if (p.sourceVersion.has_value()) { out += "@" + p.sourceVersion->toCanonical(); }
    return out;
}

/// 四态 token（core.md §4.3 冻结词表——core 侧 stateToken 为私有实现细节，
/// 此处为报告渲染面的同词表只读呈现，非第二权威定义）。
std::string_view fieldStateToken(core::FieldState s) noexcept
{
    switch (s) {
    case core::FieldState::Provided: return "provided";
    case core::FieldState::NotProvided: return "not-provided";
    case core::FieldState::NotApplicable: return "not-applicable";
    case core::FieldState::Invalid: return "invalid";
    }
    return "invalid";  // 不可达（全枚举已穷尽）
}

/// 值渲染分派（SourcedValue 载荷类型的重载族——新增载荷类型漏重载即
/// 编译失败，防"静默通用渲染"漂移）。
std::string renderValue(double v) { return fmtDouble(v); }
std::string renderValue(const rw::math::Vector3D<double>& v) { return renderVector(v); }
std::string renderValue(const JointPose& t) { return renderPose(t); }
std::string renderValue(const JointLimits& lim) { return renderLimits(lim); }
std::string renderValue(const InertiaTensor& i) { return renderInertia(i); }

/// SourcedValue→确定性文本：Provided 渲染"值 @ 来源"（来源变化可观察——
/// acceptance 2"来源标记变更入表"的文本面）；Invalid 保留原串（NFR-COR-03）。
template <class T>
std::string renderSourced(const core::SourcedValue<T>& v)
{
    switch (v.state()) {
    case core::FieldState::Provided:
        return renderValue(v.value()) + " @ " + renderProvenance(v.provenance());
    case core::FieldState::NotProvided:
    case core::FieldState::NotApplicable:
        return std::string(fieldStateToken(v.state()));
    case core::FieldState::Invalid:
        return std::string(fieldStateToken(v.state())) + "(" + v.invalidRawInput() + ")";
    }
    return std::string(fieldStateToken(v.state()));  // 不可达（同上）
}

/// 材料引用→"material{id=..,density=<SourcedValue 渲染>}"（kg/m³——§4.3-B）。
std::string renderMaterial(const MaterialRef& m)
{
    return "material{id=" + m.materialId + ",density=" + renderSourced(m.density) + "}";
}

/// 几何引用→"geom{res=..,kind=..,T{..}}"（res 指 resourceManifest 键——
/// §4.3-B；几何本体不入对象字节，CON-03 三段边界）。
std::string renderGeometry(const GeometryRef& g)
{
    return "geom{res=" + g.resourceRefId + ",kind="
        + std::string(geometryKindToken(g.kind)) + "," + renderPose(JointPose(g.localTransform)) + "}";
}

/// 摘要字节→64 位小写十六进制（资源 contentDigest 的报告渲染——格式化
/// 而非身份实现；Digest256 本体为原始 32 字节）。
std::string renderDigestBytes(const core::Digest256& d)
{
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(d.size() * 2);
    for (std::uint8_t b : d) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

/// 资源条目→"res{digest=..,state=..,ext=<登记摘要|->,solid=<oid@cv|->}"
/// （§4.3 resourceManifest 行：contentDigest 是身份要素；路径不是身份——
/// §4.8，故外部记录只渲染 recordedDigest，absPath 不入差异文本）。
std::string renderResource(const ResourceRef& r)
{
    std::string out = "res{digest=" + renderDigestBytes(r.contentDigest)
        + ",state=" + std::string(resourceStateToken(r.state));
    if (r.externalRecord.has_value()) {
        out += ",ext=" + renderDigestBytes(r.externalRecord->recordedDigest);
    } else {
        out += ",ext=-";
    }
    if (r.solidifiedObject.has_value()) {
        out += ",solid=" + r.solidifiedObject->objectId.toCanonical()
            + "@" + r.solidifiedObject->contentVersion.toCanonical();
    } else {
        out += ",solid=-";
    }
    return out + "}";
}

/// 安装预设→报告侧 token（"ground|inverted|wall|custom"——§4.3 preset 行
/// 冻结词表的只读呈现；runtime 未提供枚举→token 公共函数，此处为渲染面
/// 而非第二权威定义）。
std::string_view presetToken(runtime::InstallationPresetToken preset) noexcept
{
    switch (preset) {
    case runtime::InstallationPresetToken::Ground: return "ground";
    case runtime::InstallationPresetToken::Inverted: return "inverted";
    case runtime::InstallationPresetToken::Wall: return "wall";
    case runtime::InstallationPresetToken::Custom: return "custom";
    }
    return "custom";  // 不可达（全枚举已穷尽）
}

/// 整关节摘要（集合成员增删条目的值面——身份＋名称＋类型足以定位回溯）。
std::string renderJointSummary(const JointEntry& j)
{
    return "joint{id=" + j.objectId.toCanonical() + ",name=" + j.localName
        + ",type=" + std::string(jointTypeToken(j.type)) + "}";
}

/// 整连杆摘要（同上——物性明细属逐字段差异面，不在成员条目重复展开）。
std::string renderLinkSummary(const LinkEntry& l)
{
    return "link{id=" + l.objectId.toCanonical() + ",name=" + l.localName + "}";
}

// =====================================================================
// 装配层（DiffBuilder——条目统一入组＋终态稳定排序）
// =====================================================================

/// 一次 SourcedValue 字段差异的两面标记（值面/来源面独立判定——见
/// compareSourced 注）。
struct SourcedChangeFlags {
    bool valueChanged = false;
    bool provenanceChanged = false;
    bool differs = false;  ///< 任一面不同（发射判据）
};

/// 报告装配器：按组收条目（条目与字段序序数成对追加），finalize 时逐组
/// 按（对象 id 规范文本→字段序→定位路径→三态）stable_sort 后移交报告
/// （键全部确定，输出与发射序无关——确定性收敛；stable_sort 兜底键全等的
/// 退化情形——同键条目只能是重复发射的同一条目，其相对序无观测意义）。
class DiffBuilder {
public:
    /// 追加一条完整差异条目（order＝字段序序数——排序键第二段）。
    void push(ModelDiffGroup group, ModelDiffChangeKind kind, core::ObjectId oid,
              unsigned order, std::string path, std::string field,
              bool valueChanged, bool provenanceChanged,
              std::string baseText, std::string candText)
    {
        ModelDiffEntry e;
        e.group = group;
        e.kind = kind;
        e.objectId = std::move(oid);
        e.subjectPath = std::move(path);
        e.field = std::move(field);
        e.valueChanged = valueChanged;
        e.provenanceChanged = provenanceChanged;
        e.baselineText = std::move(baseText);
        e.candidateText = std::move(candText);
        ordersOf(group).push_back(order);
        groupOf(group).push_back(std::move(e));
    }

    /// 终态：逐组排序并移交报告（三组与三序数队列按组一一对应）。
    void finalize(ModelDiffReport& report)
    {
        report.structure = sortGroup(std::move(m_structure), m_structureOrders);
        report.parameters = sortGroup(std::move(m_parameters), m_parameterOrders);
        report.properties = sortGroup(std::move(m_properties), m_propertyOrders);
    }

private:
    std::vector<ModelDiffEntry>& groupOf(ModelDiffGroup g)
    {
        switch (g) {
        case ModelDiffGroup::Structure: return m_structure;
        case ModelDiffGroup::Parameters: return m_parameters;
        case ModelDiffGroup::Properties: return m_properties;
        }
        return m_structure;  // 不可达（全枚举已穷尽）
    }

    std::vector<unsigned>& ordersOf(ModelDiffGroup g)
    {
        switch (g) {
        case ModelDiffGroup::Structure: return m_structureOrders;
        case ModelDiffGroup::Parameters: return m_parameterOrders;
        case ModelDiffGroup::Properties: return m_propertyOrders;
        }
        return m_structureOrders;  // 不可达（同上）
    }

    /// 单组排序：条目与序数按追加位次配对，间接序稳定排序后按序输出。
    static std::vector<ModelDiffEntry> sortGroup(std::vector<ModelDiffEntry> items,
                                                 std::vector<unsigned> orders)
    {
        std::vector<std::string> idKeys;
        idKeys.reserve(items.size());
        for (const ModelDiffEntry& e : items) { idKeys.push_back(e.objectId.toCanonical()); }
        std::vector<std::size_t> idx(items.size());
        for (std::size_t i = 0; i < idx.size(); ++i) { idx[i] = i; }
        std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
            if (idKeys[a] != idKeys[b]) { return idKeys[a] < idKeys[b]; }
            if (orders[a] != orders[b]) { return orders[a] < orders[b]; }
            if (items[a].subjectPath != items[b].subjectPath) {
                return items[a].subjectPath < items[b].subjectPath;
            }
            return items[a].kind < items[b].kind;  // 枚举序＝Added<Removed<Modified
        });
        std::vector<ModelDiffEntry> sorted;
        sorted.reserve(items.size());
        for (std::size_t i : idx) { sorted.push_back(std::move(items[i])); }
        return sorted;
    }

    std::vector<ModelDiffEntry> m_structure;
    std::vector<ModelDiffEntry> m_parameters;
    std::vector<ModelDiffEntry> m_properties;
    std::vector<unsigned> m_structureOrders;  ///< 与 m_structure 追加位次对齐
    std::vector<unsigned> m_parameterOrders;  ///< 与 m_parameters 追加位次对齐
    std::vector<unsigned> m_propertyOrders;   ///< 与 m_properties 追加位次对齐
};

// =====================================================================
// 比较层（SourcedValue 两面判定＋各对象段比较）
// =====================================================================

/// SourcedValue 差异判定（两面独立——acceptance 2"权威字段变更与来源标记
/// 变更均入表"的判定基点）：
///   - 四态不同：值面变化（状态迁移本身是权威事实——缺失↔提供改变模型
///     语义）；来源面不比（core 相等语义：来源仅 Provided 态有定义）；
///   - 双 Provided：值不等→值面；来源不等→来源面（与 core operator==
///     同基准——kind/sourceObject/sourceVersion/methodTag 四字段全等）；
///   - 双 Invalid：原串不等→值面（core 相等语义只比原串）；
///   - 双 NotProvided / 双 NotApplicable：无差异。
template <class T>
SourcedChangeFlags compareSourced(const core::SourcedValue<T>& a,
                                  const core::SourcedValue<T>& b)
{
    SourcedChangeFlags f;
    if (a.state() != b.state()) {
        f.differs = true;
        f.valueChanged = true;
        return f;
    }
    switch (a.state()) {
    case core::FieldState::Provided:
        f.valueChanged = !(a.value() == b.value());
        f.provenanceChanged = !(a.provenance() == b.provenance());
        break;
    case core::FieldState::Invalid:
        f.valueChanged = !(a.invalidRawInput() == b.invalidRawInput());
        break;
    case core::FieldState::NotProvided:
    case core::FieldState::NotApplicable:
        break;  // 空态无载荷——恒等
    }
    f.differs = f.valueChanged || f.provenanceChanged;
    return f;
}

/// 发射一条 SourcedValue 字段差异（仅 differs 时入表——逐项＝有差才有项）。
template <class T>
void emitSourced(DiffBuilder& b, ModelDiffGroup group, const core::ObjectId& oid,
                 unsigned order, const std::string& path, const std::string& field,
                 const core::SourcedValue<T>& a, const core::SourcedValue<T>& c)
{
    const SourcedChangeFlags f = compareSourced(a, c);
    if (!f.differs) { return; }
    b.push(group, ModelDiffChangeKind::Modified, oid, order, path, field,
           f.valueChanged, f.provenanceChanged, renderSourced(a), renderSourced(c));
}

/// 可选字段差异判定（存在性差异或载荷差异）。
template <class T>
bool optionalDiffers(const std::optional<T>& a, const std::optional<T>& b)
{
    if (a.has_value() != b.has_value()) { return true; }
    return a.has_value() && !(*a == *b);
}

/// 双侧存在时的可选字段比较（前置：两侧均有值）。
template <class T>
bool optionalValueDiffers(const std::optional<T>& a, const std::optional<T>& b)
{
    return a.has_value() && b.has_value() && !(*a == *b);
}

}  // namespace

// =====================================================================
// diff 主编排（比较面＝两工作集的 design 根对象——范围决策见 ModelDiff.hpp
// 文件头"比较面与范围"）
// =====================================================================

namespace {

/// 根对象构成字段段（§4.3 表行序：schemaVersion/displayName/authority/
/// basePlacement——全部 Structure 组，映射决策见 ModelDiffGroup 注）。
void diffRootFields(const RobotDesign& a, const RobotDesign& c, DiffBuilder& b)
{
    const core::ObjectId root;  // 根对象字段条目的排序占位身份（全零＝最小）

    if (a.schemaVersion != c.schemaVersion) {  // 对象 schema 主版本（只增不减）
        b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, root,
               kRootSchemaVersion, "schemaVersion", "schemaVersion", true, false,
               fmtUint(a.schemaVersion), fmtUint(c.schemaVersion));
    }
    if (a.displayName != c.displayName) {  // 仅呈现名（变更产生修订不改 Description）
        b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, root,
               kRootDisplayName, "displayName", "displayName", true, false,
               a.displayName, c.displayName);
    }
    if (a.authority != c.authority) {  // 权威模式开关（MDL-02 互斥——切换时
        // 受管三字段全跳过，本条目是唯一语义事实——见文件头派生字段规则）
        b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, root,
               kRootAuthority, "authority", "authority", true, false,
               std::string(authorityModeToken(a.authority)),
               std::string(authorityModeToken(c.authority)));
    }
    if (a.basePlacement.preset != c.basePlacement.preset) {  // 安装预设（基座—
        // 世界结构性配置——P-RT-4：modeling 只存参数不存矩阵）
        b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, root,
               kBasePreset, "basePlacement.preset", "preset", true, false,
               std::string(presetToken(a.basePlacement.preset)),
               std::string(presetToken(c.basePlacement.preset)));
    }
    emitSourced(b, ModelDiffGroup::Structure, root, kBaseCustomEaa,
                "basePlacement.customEaa", "customEaa",
                a.basePlacement.customEaa, c.basePlacement.customEaa);  // EAA rad
    emitSourced(b, ModelDiffGroup::Structure, root, kBasePosition,
                "basePlacement.basePosition", "basePosition",
                a.basePlacement.basePosition, c.basePlacement.basePosition);  // m
}

/// 关节链段（成员增删＋链序位＋逐字段——§4.3-A 表行序）。
///
/// @param authorityBothExplicit [in] 两侧 authority 均为 Explicit（受管
///        字段 axis/origin 的比较门——D-MDL-5，见文件头规则）。
void diffJoints(const RobotDesign& a, const RobotDesign& c,
                bool authorityBothExplicit, DiffBuilder& b)
{
    // 按身份匹配（ARC-04：objectId 跨修订稳定——同名不同 id 是两个对象）。
    // 首次出现匹配：合法模型内 id 唯一（I-MDL-2，构造边界保证）；违例输入
    // 按首次出现确定性处理（文件头错误语义——不校验不修复）。
    std::vector<std::size_t> candIndexOf(a.joints.size(), static_cast<std::size_t>(-1));
    std::vector<std::size_t> baseIndexOf(c.joints.size(), static_cast<std::size_t>(-1));
    for (std::size_t i = 0; i < a.joints.size(); ++i) {
        for (std::size_t j = 0; j < c.joints.size(); ++j) {
            if (baseIndexOf[j] == static_cast<std::size_t>(-1)
                && a.joints[i].objectId == c.joints[j].objectId) {
                candIndexOf[i] = j;
                baseIndexOf[j] = i;
                break;  // 基线第 i 个关节匹配候选侧首个未占用的同 id 关节
            }
        }
    }

    // 成员增删（Added：仅 candidate；Removed：仅 baseline——三态方向语义
    // 见 ModelDiffChangeKind 注）。
    for (std::size_t j = 0; j < c.joints.size(); ++j) {
        if (baseIndexOf[j] == static_cast<std::size_t>(-1)) {
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Added,
                   c.joints[j].objectId, kJointMembership,
                   "joints[" + fmtUint(j) + "]", "joint", true, false,
                   "", renderJointSummary(c.joints[j]));
        }
    }
    for (std::size_t i = 0; i < a.joints.size(); ++i) {
        if (candIndexOf[i] == static_cast<std::size_t>(-1)) {
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Removed,
                   a.joints[i].objectId, kJointMembership,
                   "joints[" + fmtUint(i) + "]", "joint", true, false,
                   renderJointSummary(a.joints[i]), "");
        }
    }

    // 匹配对逐字段（观察方向定位：subjectPath 取 candidate 下标）。
    for (std::size_t i = 0; i < a.joints.size(); ++i) {
        const std::size_t j = candIndexOf[i];
        if (j == static_cast<std::size_t>(-1)) { continue; }
        const JointEntry& ja = a.joints[i];
        const JointEntry& jc = c.joints[j];
        const core::ObjectId oid = jc.objectId;
        const std::string prefix = "joints[" + fmtUint(j) + "]";

        if (i != j) {  // 链序位变化（数组下标是权威语义——串联有序，§4.3）
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, oid,
                   kJointChainIndex, prefix, "chainIndex", true, false,
                   "joints[" + fmtUint(i) + "]", "joints[" + fmtUint(j) + "]");
        }
        if (ja.localName != jc.localName) {  // 改名＝新内容（runtime §4.3.6）
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, oid,
                   kJointLocalName, prefix + ".localName", "localName", true, false,
                   ja.localName, jc.localName);
        }
        if (ja.type != jc.type) {  // 类型词表变化改变运动学结构（Structure 组）
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, oid,
                   kJointType, prefix + ".type", "type", true, false,
                   std::string(jointTypeToken(ja.type)),
                   std::string(jointTypeToken(jc.type)));
        }
        // 受管字段（axis/origin）：仅两侧 Explicit 比较（D-MDL-5——文件头
        // 规则；StandardDH 侧为派生只读，混权威时两字段各在一侧派生）。
        if (authorityBothExplicit) {
            emitSourced(b, ModelDiffGroup::Parameters, oid, kJointAxis,
                        prefix + ".axis", "axis", ja.axis, jc.axis);    // 单位向量
            emitSourced(b, ModelDiffGroup::Parameters, oid, kJointOrigin,
                        prefix + ".origin", "origin", ja.origin, jc.origin);  // m/rad
        }
        if (ja.zeroOffset != jc.zeroOffset) {  // 零位偏置（rad/m；两态均权威）
            b.push(ModelDiffGroup::Parameters, ModelDiffChangeKind::Modified, oid,
                   kJointZeroOffset, prefix + ".zeroOffset", "zeroOffset", true, false,
                   fmtDouble(ja.zeroOffset), fmtDouble(jc.zeroOffset));
        }
        emitSourced(b, ModelDiffGroup::Parameters, oid, kJointBounds,
                    prefix + ".bounds", "bounds", ja.bounds, jc.bounds);  // rad/m
        emitSourced(b, ModelDiffGroup::Parameters, oid, kJointWorkingRange,
                    prefix + ".workingRange", "workingRange",
                    ja.workingRange, jc.workingRange);  // rad；仅 Continuous
        // 受管字段 dhDerived：仅两侧 StandardDH 比较（Explicit 侧为派生展示值）。
        if (!authorityBothExplicit && a.authority == AuthorityMode::StandardDH
            && c.authority == AuthorityMode::StandardDH) {
            if (optionalDiffers(ja.dhDerived, jc.dhDerived)) {
                b.push(ModelDiffGroup::Parameters, ModelDiffChangeKind::Modified, oid,
                       kJointDhDerived, prefix + ".dhDerived", "dhDerived", true, false,
                       ja.dhDerived.has_value() ? renderDh(*ja.dhDerived) : "",
                       jc.dhDerived.has_value() ? renderDh(*jc.dhDerived) : "");
            }
        }
    }
}

/// 连杆链段（成员增删＋链序位＋物性逐字段——§4.3-B 表行序）。
void diffLinks(const RobotDesign& a, const RobotDesign& c, DiffBuilder& b)
{
    // 关节同构的身份匹配（首现匹配——I-MDL-2 由构造边界保证）。
    std::vector<std::size_t> candIndexOf(a.links.size(), static_cast<std::size_t>(-1));
    std::vector<std::size_t> baseIndexOf(c.links.size(), static_cast<std::size_t>(-1));
    for (std::size_t i = 0; i < a.links.size(); ++i) {
        for (std::size_t j = 0; j < c.links.size(); ++j) {
            if (baseIndexOf[j] == static_cast<std::size_t>(-1)
                && a.links[i].objectId == c.links[j].objectId) {
                candIndexOf[i] = j;
                baseIndexOf[j] = i;
                break;
            }
        }
    }

    for (std::size_t j = 0; j < c.links.size(); ++j) {
        if (baseIndexOf[j] == static_cast<std::size_t>(-1)) {
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Added,
                   c.links[j].objectId, kLinkMembership,
                   "links[" + fmtUint(j) + "]", "link", true, false,
                   "", renderLinkSummary(c.links[j]));
        }
    }
    for (std::size_t i = 0; i < a.links.size(); ++i) {
        if (candIndexOf[i] == static_cast<std::size_t>(-1)) {
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Removed,
                   a.links[i].objectId, kLinkMembership,
                   "links[" + fmtUint(i) + "]", "link", true, false,
                   renderLinkSummary(a.links[i]), "");
        }
    }

    for (std::size_t i = 0; i < a.links.size(); ++i) {
        const std::size_t j = candIndexOf[i];
        if (j == static_cast<std::size_t>(-1)) { continue; }
        const LinkEntry& la = a.links[i];
        const LinkEntry& lc = c.links[j];
        const core::ObjectId oid = lc.objectId;
        const std::string prefix = "links[" + fmtUint(j) + "]";

        if (i != j) {
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, oid,
                   kLinkChainIndex, prefix, "chainIndex", true, false,
                   "links[" + fmtUint(i) + "]", "links[" + fmtUint(j) + "]");
        }
        if (la.localName != lc.localName) {
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, oid,
                   kLinkLocalName, prefix + ".localName", "localName", true, false,
                   la.localName, lc.localName);
        }
        // 物性组（Properties——MDL-05 分层的物性面；缺失≠零，四态迁移入值面）。
        emitSourced(b, ModelDiffGroup::Properties, oid, kLinkMass,
                    prefix + ".body.mass", "mass", la.body.mass, lc.body.mass);  // kg
        emitSourced(b, ModelDiffGroup::Properties, oid, kLinkCenterOfMass,
                    prefix + ".body.centerOfMass", "centerOfMass",
                    la.body.centerOfMass, lc.body.centerOfMass);  // m（连杆系）
        emitSourced(b, ModelDiffGroup::Properties, oid, kLinkInertia,
                    prefix + ".body.inertia", "inertia",
                    la.body.inertia, lc.body.inertia);  // kg·m²（质心基准/连杆系）
        // 材料：materialId + 密度（SourcedValue）合成一个条目——物性面的
        // 独立登记字段（§4.3-B body.material 行）；仅来源变化时
        // provenanceChanged 单独为真。
        {
            const std::optional<MaterialRef>& ma = la.body.material;
            const std::optional<MaterialRef>& mc = lc.body.material;
            bool differs = optionalDiffers(ma, mc);
            bool valueChanged = differs;
            bool provenanceChanged = false;
            if (ma.has_value() && mc.has_value()) {
                valueChanged = ma->materialId != mc->materialId;
                const SourcedChangeFlags df = compareSourced(ma->density, mc->density);
                valueChanged = valueChanged || df.valueChanged;
                provenanceChanged = df.provenanceChanged;
                differs = valueChanged || provenanceChanged;
            }
            if (differs) {
                b.push(ModelDiffGroup::Properties, ModelDiffChangeKind::Modified, oid,
                       kLinkMaterial, prefix + ".body.material", "material",
                       valueChanged, provenanceChanged,
                       ma.has_value() ? renderMaterial(*ma) : "",
                       mc.has_value() ? renderMaterial(*mc) : "");
            }
        }
        // 几何引用（Structure——形状构成面；非物性，MDL-05 分层的几何半段）。
        if (optionalDiffers(la.visual, lc.visual)) {
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, oid,
                   kLinkVisual, prefix + ".visual", "visual", true, false,
                   la.visual.has_value() ? renderGeometry(*la.visual) : "",
                   lc.visual.has_value() ? renderGeometry(*lc.visual) : "");
        }
        if (optionalDiffers(la.collision, lc.collision)) {  // 碰撞几何≠碰撞判定
            b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, oid,
                   kLinkCollision, prefix + ".collision", "collision", true, false,
                   la.collision.has_value() ? renderGeometry(*la.collision) : "",
                   lc.collision.has_value() ? renderGeometry(*lc.collision) : "");
        }
        // selfCollisionHints 不入表——不入根对象编码权威语义（§4.3-B 行原文；
        // 与 D-MDL-5 身份外字段不作差异项同口径，见 ModelDiff.hpp 文件头）。
    }
}

/// 引用表与资源清单段（§4.3 表行序后半——全部 Structure 组）。
void diffReferencesAndResources(const RobotDesign& a, const RobotDesign& c, DiffBuilder& b)
{
    const core::ObjectId root;  // 引用表条目：defaultTcp 定位到被引工具；
    // 其余根级引用/资源键无 ObjectId 锚——用全零占位（排序稳定居前）。

    // defaultTcp（optional<TcpRef>）——定位锚取被引工具 oid（点击定位语义：
    // UX-13 跳到工具对象本身；两侧均设置但不同时取 candidate 侧锚——观察
    // 方向定位，与 subjectPath 下标口径一致）。
    if (optionalDiffers(a.defaultTcp, c.defaultTcp)) {
        core::ObjectId anchor;
        if (c.defaultTcp.has_value()) {
            anchor = c.defaultTcp->toolOid;
        } else if (a.defaultTcp.has_value()) {
            anchor = a.defaultTcp->toolOid;
        }
        auto renderTcp = [](const TcpRef& t) {
            return "tcp{tool=" + t.toolOid.toCanonical() + ",key=" + t.tcpKey + "}";
        };
        b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, anchor,
               kDefaultTcp, "defaultTcp", "defaultTcp", true, false,
               a.defaultTcp.has_value() ? renderTcp(*a.defaultTcp) : "",
               c.defaultTcp.has_value() ? renderTcp(*c.defaultTcp) : "");
    }

    // toolRefs/sceneRefs——canonical 集合语义（§4.8：编码层按规范文本字典序
    // 规范化；顺序差异不是身份差异，不产生条目）。逐引用增删，锚＝被引用
    // 对象 id（点击定位到工具/场景对象）。
    auto diffRefSet = [&b](const std::vector<core::ObjectId>& aRefs,
                           const std::vector<core::ObjectId>& cRefs, unsigned order,
                           const char* fieldName) {
        auto keyOf = [](const core::ObjectId& id) { return id.toCanonical(); };
        std::vector<std::string> aKeys, cKeys;
        aKeys.reserve(aRefs.size());
        cKeys.reserve(cRefs.size());
        for (const core::ObjectId& id : aRefs) { aKeys.push_back(keyOf(id)); }
        for (const core::ObjectId& id : cRefs) { cKeys.push_back(keyOf(id)); }
        std::sort(aKeys.begin(), aKeys.end());
        std::sort(cKeys.begin(), cKeys.end());
        // 候选侧有而基线无→Added；基线有而候选无→Removed（键去重语义由
        // 引用表"无重复"约束承载——I-MDL-9，构造边界保证）。
        for (const std::string& k : cKeys) {
            if (!std::binary_search(aKeys.begin(), aKeys.end(), k)) {
                auto parsed = core::ObjectId::tryFromCanonical(k);
                if (parsed.has_value()) {
                    b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Added,
                           *parsed, order, std::string(fieldName) + "[" + k + "]",
                           fieldName, true, false, "", k);
                }
            }
        }
        for (const std::string& k : aKeys) {
            if (!std::binary_search(cKeys.begin(), cKeys.end(), k)) {
                auto parsed = core::ObjectId::tryFromCanonical(k);
                if (parsed.has_value()) {
                    b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Removed,
                           *parsed, order, std::string(fieldName) + "[" + k + "]",
                           fieldName, true, false, k, "");
                }
            }
        }
    };
    diffRefSet(a.toolRefs, c.toolRefs, kToolRefs, "toolRefs");
    diffRefSet(a.sceneRefs, c.sceneRefs, kSceneRefs, "sceneRefs");

    if (optionalDiffers(a.poseSetRef, c.poseSetRef)) {  // 至多一份（§4.6）
        core::ObjectId anchor;
        if (c.poseSetRef.has_value()) { anchor = *c.poseSetRef; }
        else if (a.poseSetRef.has_value()) { anchor = *a.poseSetRef; }
        b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, anchor,
               kPoseSetRef, "poseSetRef", "poseSetRef", true, false,
               a.poseSetRef.has_value() ? a.poseSetRef->toCanonical() : "",
               c.poseSetRef.has_value() ? c.poseSetRef->toCanonical() : "");
    }
    if (optionalDiffers(a.drivetrainRef, c.drivetrainRef)) {  // 回填目标 SEL-10
        core::ObjectId anchor;
        if (c.drivetrainRef.has_value()) { anchor = *c.drivetrainRef; }
        else if (a.drivetrainRef.has_value()) { anchor = *a.drivetrainRef; }
        b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, anchor,
               kDrivetrainRef, "drivetrainRef", "drivetrainRef", true, false,
               a.drivetrainRef.has_value() ? a.drivetrainRef->toCanonical() : "",
               c.drivetrainRef.has_value() ? c.drivetrainRef->toCanonical() : "");
    }

    // resourceManifest——按 resourceId 键集（§4.8：清单键唯一、编码层字典序
    // 规范化）；两侧同键任一字段不同→Modified（整体一个条目，文本全渲染）。
    {
        auto findRes = [](const std::vector<ResourceRef>& list,
                          const std::string& id) -> const ResourceRef* {
            for (const ResourceRef& r : list) {
                if (r.resourceId == id) { return &r; }
            }
            return nullptr;
        };
        for (const ResourceRef& rc : c.resourceManifest) {
            const ResourceRef* ra = findRes(a.resourceManifest, rc.resourceId);
            if (ra == nullptr) {
                b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Added, root,
                       kResourceManifest, "resourceManifest[" + rc.resourceId + "]",
                       "resource", true, false, "", renderResource(rc));
            } else if (!(*ra == rc)) {
                b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, root,
                       kResourceManifest, "resourceManifest[" + rc.resourceId + "]",
                       "resource", true, false, renderResource(*ra), renderResource(rc));
            }
        }
        for (const ResourceRef& ra : a.resourceManifest) {
            if (findRes(c.resourceManifest, ra.resourceId) == nullptr) {
                b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Removed, root,
                       kResourceManifest, "resourceManifest[" + ra.resourceId + "]",
                       "resource", true, false, renderResource(ra), "");
            }
        }
    }

    if (a.notes != c.notes) {  // 纯备注（不入编译身份；变更产生修订——§4.3）
        b.push(ModelDiffGroup::Structure, ModelDiffChangeKind::Modified, root,
               kNotes, "notes", "notes", true, false, a.notes, c.notes);
    }
}

}  // namespace

ModelDiffReport ModelDiffService::diff(const ModelingWorkingSet& baseline,
                                       const ModelingWorkingSet& candidate) const
{
    ModelDiffReport report;
    // 方向字段：两侧工作集根对象身份原样带入（nullopt＝模板草稿——§4.9
    // 编辑态无身份承诺，报告照实承载）。
    report.baselineObjectId = baseline.rootObjectId;
    report.candidateObjectId = candidate.rootObjectId;

    // 权威受管字段的比较门（D-MDL-5——文件头规则）：axis/origin 仅两侧
    // Explicit 比较；dhDerived 仅两侧 StandardDH 比较；权威不同→受管字段
    // 全跳过，authority 差异条目由 diffRootFields 承载。
    const bool authorityBothExplicit = baseline.design.authority == AuthorityMode::Explicit
        && candidate.design.authority == AuthorityMode::Explicit;

    DiffBuilder builder;
    diffRootFields(baseline.design, candidate.design, builder);
    diffJoints(baseline.design, candidate.design, authorityBothExplicit, builder);
    diffLinks(baseline.design, candidate.design, builder);
    diffReferencesAndResources(baseline.design, candidate.design, builder);
    builder.finalize(report);
    return report;
}

}  // namespace sdurws::ird::modeling
