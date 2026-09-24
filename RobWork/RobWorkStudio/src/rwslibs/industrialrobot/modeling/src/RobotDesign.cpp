/**
 * @file   RobotDesign.cpp
 * @brief  RobotDesign 值模型的实现——枚举 token 表、值相等、双权威编辑
 *         权限守卫（C-1/C-2）与不变量核查（I-MDL-1～I-MDL-10）。
 *
 * 设计依据：units/modeling.md §4.3（字段表）、§4.10（不变量）、§7.2/§7.3
 * （参数来源表与冲突矩阵）、§14.2 D-MDL-5；core.md §4.3（SourcedValue）；
 * 任务契约 tasks/foundation/WP-13-T03.json acceptance 2/3/5。
 *
 * 实现纪律（AGENTS.md §2）：每个语义段落注释说明业务含义与出处；物理量
 * 单位、容差来源、确定性策略逐处标注。全部函数为纯函数（无共享可变状态，
 * 并发安全；确定性 NFR-COR-02）。
 */

#include <sdurws/ird/modeling/RobotDesign.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // rotationFromCustomEaa——EAA→R 唯一权威换算点（I-MDL-7）
#include "InertiaMath.hpp"  // 单元私有：惯量 SPD＋三角不等式单一实现（I-MDL-5——R-2：src/ 内私有头）

namespace sdurws::ird::modeling {

// =====================================================================
// 枚举 token 表（switch 全枚举、无 default——新增枚举值漏登记时编译器
// 告警暴露；返回静态存储期字面量，线程安全）
// =====================================================================

std::string_view authorityModeToken(AuthorityMode mode) noexcept
{
    switch (mode) {
    case AuthorityMode::Explicit: return "Explicit";
    case AuthorityMode::StandardDH: return "StandardDH";
    }
    return "unknown";  // 不可达（全枚举已覆盖；防御性返回，满足无警告路径）
}

std::string_view jointTypeToken(JointType type) noexcept
{
    switch (type) {
    case JointType::Revolute: return "Revolute";
    case JointType::Continuous: return "Continuous";
    case JointType::Prismatic: return "Prismatic";
    case JointType::Fixed: return "Fixed";
    }
    return "unknown";
}

std::string_view geometryKindToken(GeometryKind kind) noexcept
{
    switch (kind) {
    case GeometryKind::Mesh: return "Mesh";
    case GeometryKind::Primitive: return "Primitive";
    }
    return "unknown";
}

std::string_view resourceStateToken(ResourceState state) noexcept
{
    switch (state) {
    case ResourceState::Recorded: return "Recorded";
    case ResourceState::Solidified: return "Solidified";
    }
    return "unknown";
}

std::string_view authorityLockedFieldToken(AuthorityLockedField field) noexcept
{
    switch (field) {
    case AuthorityLockedField::Axis: return "axis";
    case AuthorityLockedField::Origin: return "origin";
    case AuthorityLockedField::DhDerived: return "dhDerived";
    }
    return "unknown";
}

std::string_view invariantIdToken(InvariantId id) noexcept
{
    switch (id) {
    case InvariantId::IMdl1: return "I-MDL-1";
    case InvariantId::IMdl2: return "I-MDL-2";
    case InvariantId::IMdl3: return "I-MDL-3";
    case InvariantId::IMdl4: return "I-MDL-4";
    case InvariantId::IMdl5: return "I-MDL-5";
    case InvariantId::IMdl6: return "I-MDL-6";
    case InvariantId::IMdl7: return "I-MDL-7";
    case InvariantId::IMdl8: return "I-MDL-8";
    case InvariantId::IMdl9: return "I-MDL-9";
    case InvariantId::IMdl10: return "I-MDL-10";
    case InvariantId::IMdl11: return "I-MDL-11";
    case InvariantId::IMdl12: return "I-MDL-12";
    case InvariantId::IMdl13: return "I-MDL-13";
    }
    return "unknown";
}

// =====================================================================
// 值相等（逐字段、逐元素比较——rw::math 值类型经元素访问器比较，不依赖
// 其 operator== 的浮点语义差异；附录 D 第 12 项：身份类比较无容差）
// =====================================================================

namespace {

/// Rotation3D 逐元素相等（3×3 位级相等——无容差，ARC-04 身份比较口径）。
bool rotationEqual(const rw::math::Rotation3D<double>& a,
                   const rw::math::Rotation3D<double>& b) noexcept
{
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            // 逐元素位级比较（NaN 含其中即不等——NaN 早已被 I-MDL-3 拒绝，
            // 相等语义保持 IEEE 严格性）
            if (!(a(r, c) == b(r, c))) { return false; }
        }
    }
    return true;
}

/// Vector3D 逐元素相等。
bool vectorEqual(const rw::math::Vector3D<double>& a,
                 const rw::math::Vector3D<double>& b) noexcept
{
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(a[i] == b[i])) { return false; }
    }
    return true;
}

/// Transform3D 相等＝旋转＋平移全等（T_ab 位姿的精确等值——无容差）。
bool transformEqual(const rw::math::Transform3D<double>& a,
                    const rw::math::Transform3D<double>& b) noexcept
{
    return rotationEqual(a.R(), b.R()) && vectorEqual(a.P(), b.P());
}

}  // namespace

bool GeometryRef::operator==(const GeometryRef& o) const
{
    return resourceRefId == o.resourceRefId
        && transformEqual(localTransform, o.localTransform)
        && kind == o.kind;
}

bool BodyData::operator==(const BodyData& o) const
{
    return mass == o.mass
        && centerOfMass == o.centerOfMass
        && inertia == o.inertia
        && material == o.material;
}

bool BasePlacement::operator==(const BasePlacement& o) const
{
    return preset == o.preset
        && customEaa == o.customEaa
        && basePosition == o.basePosition;
}

bool ResourceRef::operator==(const ResourceRef& o) const
{
    return resourceId == o.resourceId
        && contentDigest == o.contentDigest
        && state == o.state
        && externalRecord == o.externalRecord
        && solidifiedObject == o.solidifiedObject;
}

bool JointEntry::operator==(const JointEntry& o) const
{
    return objectId == o.objectId
        && localName == o.localName
        && type == o.type
        && axis == o.axis
        && origin == o.origin
        && zeroOffset == o.zeroOffset
        && bounds == o.bounds
        && workingRange == o.workingRange
        && dhDerived == o.dhDerived;
}

bool LinkEntry::operator==(const LinkEntry& o) const
{
    return objectId == o.objectId
        && localName == o.localName
        && body == o.body
        && visual == o.visual
        && collision == o.collision
        && selfCollisionHints == o.selfCollisionHints;
}

bool RobotDesign::operator==(const RobotDesign& o) const
{
    return schemaVersion == o.schemaVersion
        && displayName == o.displayName
        && authority == o.authority
        && basePlacement == o.basePlacement
        && joints == o.joints
        && links == o.links
        && defaultTcp == o.defaultTcp
        && toolRefs == o.toolRefs
        && sceneRefs == o.sceneRefs
        && poseSetRef == o.poseSetRef
        && drivetrainRef == o.drivetrainRef
        && resourceManifest == o.resourceManifest
        && notes == o.notes;
}

// =====================================================================
// 双权威编辑权限守卫（§7.3 C-1/C-2；MDL-09——acceptance 5"Axis 编辑权限
// 随权威模式"的判定点）
// =====================================================================

std::optional<ModelingError> authorityEditGuard(AuthorityMode authority,
                                                AuthorityLockedField field)
{
    // 权威侧映射（§7.2 参数来源表）：axis/origin 在 Explicit 权威、
    // dhDerived 在 StandardDH 权威——其余组合即"编辑派生只读字段"。
    const bool fieldIsAuthoritative =
        (field == AuthorityLockedField::Axis && authority == AuthorityMode::Explicit)
        || (field == AuthorityLockedField::Origin && authority == AuthorityMode::Explicit)
        || (field == AuthorityLockedField::DhDerived && authority == AuthorityMode::StandardDH);

    // 可编辑：该字段在当前模式下就是权威——无拒绝语义。
    if (fieldIsAuthoritative) { return std::nullopt; }

    // 拒绝（C-1：DH 权威下编辑 axis/origin；C-2：显式权威下编辑 dhDerived）。
    // 调用方错误走值面（编辑器 EditOutcome 轨道——§9.4.1），错误码固定
    // AuthorityViolation（V-14 期望值）；params 逐项登记 field/mode 供
    // 就地定位；detail 为"派生只读"提示文案底稿（用户可见文案的最终
    // 组织归 diagnostics 层——PA-1 文案权威，此处仅供开发诊断与 UI 底稿）。
    ModelingError e;
    e.code = ModelingErrorCode::AuthorityViolation;
    e.params.emplace_back("field", std::string(authorityLockedFieldToken(field)));
    e.params.emplace_back("authority", std::string(authorityModeToken(authority)));
    e.detail = "该字段为派生只读（权威模式=" + std::string(authorityModeToken(authority))
             + "），编辑被拒绝（C-1/C-2）；切换权威模式需经转换判定（§7.6，T09）";
    return e;
}

// =====================================================================
// 不变量核查（§4.10 I-MDL-1～I-MDL-10；全部违例一次报出——比较型定位
// 的调用方按 subject 逐项处置，不做"首个违例即短路"以免用户来回修）
// =====================================================================

namespace {

/// 追加一条违例（编号＋定位路径——输出顺序＝调用序，确定性）。
void addViolation(std::vector<InvariantViolation>& out, InvariantId id, std::string subject)
{
    out.push_back(InvariantViolation{id, std::move(subject)});
}

/// double 有限性（NaN/Inf 均非法——I-MDL-3"不静默置 0"的判定面）。
bool finite(double v) noexcept { return std::isfinite(v); }

// ---- 逐类型有限性谓词：allFiniteOf＝"这个值类型的全部 double 都有限吗"。
// ---- 供下方模板分派——必须先于模板声明（依赖名在定义点查找，后置重载
// ---- 对依赖调用不可见）。

bool allFiniteOf(double v) noexcept { return finite(v); }

bool allFiniteOf(const JointLimits& lim) noexcept
{
    return finite(lim.first) && finite(lim.second);
}

bool allFiniteOf(const rw::math::Vector3D<double>& v) noexcept
{
    return finite(v[0]) && finite(v[1]) && finite(v[2]);
}

bool allFiniteOf(const JointPose& p) noexcept
{
    // T_parent_joint 的 12 个 double（旋转 9＋平移 3——m/rad）
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (!finite(p.r()(r, c))) { return false; }
        }
    }
    return finite(p.d()[0]) && finite(p.d()[1]) && finite(p.d()[2]);
}

bool allFiniteOf(const InertiaTensor& it) noexcept
{
    return finite(it.ixx) && finite(it.iyy) && finite(it.izz)
        && finite(it.ixy) && finite(it.ixz) && finite(it.iyz);
}

/// SourcedValue 在 Provided 态下的有限性核查：违例即登记。
/// 只查 Provided 态：NotProvided＝缺失（MDL-06 降级语义，不触发断言）、
/// NotApplicable＝显式不适用、Invalid＝保留原串待修正——三者无数值可查。
template <class T>
void checkSourcedFinite(std::vector<InvariantViolation>& out, InvariantId id,
                        const core::SourcedValue<T>& v, const std::string& subject)
{
    const auto val = v.tryValue();
    if (!val.has_value()) { return; }
    if (!allFiniteOf(*val)) {
        addViolation(out, id, subject);
    }
}

// 对称 3×3 特征值与惯量断言②③（I-MDL-5）——单一实现在单元私有头
// InertiaMath.hpp（连杆与工具共用，防两处判定漂移；算法/容差注释见该头），
// checkBodyPhysical 直接调用 inertiamath::checkInertiaAssertions。

/// 连杆/工具共用的物性断言①②③（§4.4"断言①②③同连杆"——单一实现点）。
/// subject 须为完整体路径（如 "links[0].body"——与 I-MDL-3 有限性扫描的
/// subject 同一定位规则，诊断 subject 全表一致）。
void checkBodyPhysical(std::vector<InvariantViolation>& out,
                       const BodyData& body, const std::string& subject)
{
    // 断言①：已提供质量必须 m>0（kg）；缺失＝NotProvided 不触发（MDL-06）
    if (const auto m = body.mass.tryValue(); m.has_value() && !(*m > 0.0)) {
        addViolation(out, InvariantId::IMdl5, subject + ".mass");
    }
    // 断言②③：惯量 SPD＋三角不等式（kg·m²——质心基准、连杆系参考姿态）
    inertiamath::checkInertiaAssertions(out, body.inertia, subject);
}

}  // namespace

std::vector<InvariantViolation> checkInvariants(const RobotDesign& design)
{
    std::vector<InvariantViolation> out;

    // ---- I-MDL-1（结构）：joints≥1；links==joints+1 ----
    // "链单串联无环"由有序数组表示层结构性满足：链序＝数组下标，环
    // （父指针回路）在 vector 表示中不可表达——无运行时检查面（设计注释，
    // 不是省略）。Fixed 关节可存在于链上（表 4.3-A type 行注）。
    if (design.joints.empty()) {
        addViolation(out, InvariantId::IMdl1, "joints");
    }
    if (design.links.size() != design.joints.size() + 1) {
        // runtime StructureInvalid 同口径（§4.3 links 行原文）
        addViolation(out, InvariantId::IMdl1, "links");
    }

    // ---- I-MDL-2（身份唯一）：ObjectId/localName 同模型内不得重复 ----
    // 重复＝调用方错误（构造/编辑边界拒绝，不静默重命名——NFR-COR-03）。
    // 检查面：关节∪连杆的 objectId 与 localName（同作用域＝同一设备命名
    // 空间——runtime 名称映射按 (device, localName) 解析）。
    {
        std::unordered_set<std::string> seenText;  // objectId 规范文本（十六进制串，跨类型同域）
        seenText.reserve(design.joints.size() + design.links.size());
        for (std::size_t i = 0; i < design.joints.size(); ++i) {
            const std::string key = design.joints[i].objectId.toCanonical();
            if (!seenText.insert(key).second) {
                addViolation(out, InvariantId::IMdl2, "joints[" + std::to_string(i) + "].objectId");
            }
        }
        for (std::size_t i = 0; i < design.links.size(); ++i) {
            const std::string key = design.links[i].objectId.toCanonical();
            if (!seenText.insert(key).second) {
                addViolation(out, InvariantId::IMdl2, "links[" + std::to_string(i) + "].objectId");
            }
        }
        std::unordered_set<std::string> seenNames;
        seenNames.reserve(design.joints.size() + design.links.size());
        for (std::size_t i = 0; i < design.joints.size(); ++i) {
            if (!seenNames.insert(design.joints[i].localName).second) {
                addViolation(out, InvariantId::IMdl2, "joints[" + std::to_string(i) + "].localName");
            }
        }
        for (std::size_t i = 0; i < design.links.size(); ++i) {
            if (!seenNames.insert(design.links[i].localName).second) {
                addViolation(out, InvariantId::IMdl2, "links[" + std::to_string(i) + "].localName");
            }
        }
    }

    // ---- I-MDL-3（单位合法）：Provided 值全部有限（NaN/Inf 非法）----
    {
        // 基座：customEaa（rad）/basePosition（m）
        checkSourcedFinite(out, InvariantId::IMdl3, design.basePlacement.customEaa,
                           "basePlacement.customEaa");
        checkSourcedFinite(out, InvariantId::IMdl3, design.basePlacement.basePosition,
                           "basePlacement.basePosition");
        for (std::size_t i = 0; i < design.joints.size(); ++i) {
            const JointEntry& j = design.joints[i];
            const std::string p = "joints[" + std::to_string(i) + "]";
            checkSourcedFinite(out, InvariantId::IMdl3, j.axis, p + ".axis");
            checkSourcedFinite(out, InvariantId::IMdl3, j.origin, p + ".origin");
            // zeroOffset 是裸 double（rad/m）——无 SourcedValue 外壳，直接查
            if (!finite(j.zeroOffset)) { addViolation(out, InvariantId::IMdl3, p + ".zeroOffset"); }
            checkSourcedFinite(out, InvariantId::IMdl3, j.bounds, p + ".bounds");
            checkSourcedFinite(out, InvariantId::IMdl3, j.workingRange, p + ".workingRange");
            // dhDerived 的四个 double（rad/m）——Explicit 态下为派生缓存，同样不得含 NaN
            if (j.dhDerived.has_value()) {
                const DhParameters& dh = *j.dhDerived;
                if (!finite(dh.thetaOffset) || !finite(dh.d) || !finite(dh.a) || !finite(dh.alpha)) {
                    addViolation(out, InvariantId::IMdl3, p + ".dhDerived");
                }
            }
        }
        for (std::size_t i = 0; i < design.links.size(); ++i) {
            const LinkEntry& l = design.links[i];
            const std::string p = "links[" + std::to_string(i) + "]";
            checkSourcedFinite(out, InvariantId::IMdl3, l.body.mass, p + ".body.mass");
            checkSourcedFinite(out, InvariantId::IMdl3, l.body.centerOfMass, p + ".body.centerOfMass");
            checkSourcedFinite(out, InvariantId::IMdl3, l.body.inertia, p + ".body.inertia");
            if (l.body.material.has_value()) {
                checkSourcedFinite(out, InvariantId::IMdl3, l.body.material->density,
                                   p + ".body.material.density");
            }
        }
    }

    // ---- I-MDL-4（限位有序/适用）＋ §7.3 非法组合 ----
    for (std::size_t i = 0; i < design.joints.size(); ++i) {
        const JointEntry& j = design.joints[i];
        const std::string p = "joints[" + std::to_string(i) + "]";
        if (j.type == JointType::Continuous) {
            // 非法组合"continuous 带有限 bounds"（§7.3——Continuous＝NotApplicable）
            if (j.bounds.state() == core::FieldState::Provided) {
                addViolation(out, InvariantId::IMdl4, p + ".bounds.continuous");
            }
            // workingRange 必须有限区间 qmin'<qmax'（表 4.3-A 行原文）；
            // 非有限值已由 I-MDL-3 报告，这里查序
            if (const auto wr = j.workingRange.tryValue();
                wr.has_value() && !(wr->first < wr->second)) {
                addViolation(out, InvariantId::IMdl4, p + ".workingRange.order");
            }
        } else if (j.type == JointType::Revolute || j.type == JointType::Prismatic) {
            // 断言④前半：提供限位时 qmin<qmax（rad/m——类型决定单位）
            if (const auto b = j.bounds.tryValue(); b.has_value() && !(b->first < b->second)) {
                addViolation(out, InvariantId::IMdl4, p + ".bounds.order");
            }
            // 非法组合"prismatic 带 workingRange"（§7.3——workingRange 仅
            // Continuous；Revolute 同禁，表 4.3-A"仅 Continuous"原文）
            if (j.workingRange.state() == core::FieldState::Provided) {
                addViolation(out, InvariantId::IMdl4, p + ".workingRange.nonContinuous");
            }
        }
        // Fixed：限位/工作范围均无语义——表 4.3-A 未给约束，不发明检查
    }

    // ---- I-MDL-5（物性合法）：断言①②③（连杆体——§4.3-B）----
    for (std::size_t i = 0; i < design.links.size(); ++i) {
        // subject 完整体路径——I-MDL-3/I-MDL-5 同一定位规则（.body 段齐全）
        checkBodyPhysical(out, design.links[i].body,
                          "links[" + std::to_string(i) + "].body");
    }

    // ---- I-MDL-6（轴有效）：可动关节 axis 非零、有限、可归一化 ----
    for (std::size_t i = 0; i < design.joints.size(); ++i) {
        const JointEntry& j = design.joints[i];
        if (j.type == JointType::Fixed) { continue; }  // Fixed 无可动轴——不适用
        const auto ax = j.axis.tryValue();
        if (!ax.has_value()) {
            // DH 权威态下 axis 为派生待重算（NotProvided）——轴有效性由
            // DH 参数的退化检查承担（§7.5/T09），本处无数值可查
            continue;
        }
        const rw::math::Vector3D<double>& v = *ax;
        // 有限性已由 I-MDL-3 报告；此处只处理有限值（避免 NaN 传播出误报）
        if (!allFiniteOf(v)) { continue; }
        // 稳定范数：先按最大分量缩放再求模——避免大分量溢出/小分量下溢
        const double m = std::max({std::abs(v[0]), std::abs(v[1]), std::abs(v[2])});
        if (m == 0.0) {
            // 零轴＝非法（MDL-11 导入侧仅报告；构造/编辑边界直接拒——§4.10）
            addViolation(out, InvariantId::IMdl6, "joints[" + std::to_string(i) + "].axis.zero");
        } else if (m < std::numeric_limits<double>::min()) {
            // 次正规极小轴：归一化会在除法中下溢为 0/Inf——"可归一化"不成立
            // （std::numeric_limits<double>::min()＝最小正规正数，非自设魔数）
            addViolation(out, InvariantId::IMdl6, "joints[" + std::to_string(i) + "].axis.subnormal");
        }
    }

    // ---- I-MDL-7（基座合法）：custom 必填 customEaa 且旋转正交（1×10⁻¹²）----
    if (design.basePlacement.preset == runtime::InstallationPresetToken::Custom) {
        const auto eaa = design.basePlacement.customEaa.tryValue();
        if (!eaa.has_value()) {
            // Custom 无预设矩阵——customEaa 缺失即非法（runtime §6.1 同口径）
            addViolation(out, InvariantId::IMdl7, "basePlacement.customEaa.missing");
        } else if (allFiniteOf(*eaa)) {
            // 旋转矩阵经 runtime::rotationFromCustomEaa（EAA→R 唯一权威换算
            // 点——P-RT-4，modeling 不私设第二换算）产出后查正交性：R·Rᵀ 对
            // 角=1、非对角=0，逐元素偏差 ≤1×10⁻¹²（§4.10 容差，runtime
            // InputInvalid 同口径）。Rodrigues 构造下恒满足——本检查是
            // 防御性一致性闸（换算实现若被替换/污染即在此暴露）。
            const rw::math::Rotation3D<double> R =
                runtime::rotationFromCustomEaa(
                    rw::math::Vector3D<double>((*eaa)[0], (*eaa)[1], (*eaa)[2]));
            constexpr double kOrthoTol = 1e-12;  // 正交容差（无量纲——§4.10 I-MDL-7 行）
            bool orthogonal = true;
            for (std::size_t r = 0; r < 3 && orthogonal; ++r) {
                for (std::size_t c = 0; c < 3 && orthogonal; ++c) {
                    double dot = 0.0;  // (R·Rᵀ)(r,c)＝R 行 r 与 R 行 c 的点积
                    for (std::size_t k = 0; k < 3; ++k) {
                        dot += R(r, k) * R(c, k);
                    }
                    const double expected = (r == c) ? 1.0 : 0.0;
                    if (std::abs(dot - expected) > kOrthoTol) { orthogonal = false; }
                }
            }
            if (!orthogonal) {
                addViolation(out, InvariantId::IMdl7, "basePlacement.customEaa.orthogonality");
            }
        }
        // customEaa 非有限：I-MDL-3 已报告，此处跳过换算（rotationFromCustomEaa
        // 对非有限输入抛错——不在此触发，保持校验函数无异常轨）
    }
    // "preset≠ground 而 R=I 在映射层拒绝"（§7.7）＝导入映射层规则（T05/T11），
    // 不属本值模型校验——范围注记见头文件与单元卡 §15 增量。

    // ---- I-MDL-8（权威互斥）：StandardDH 态 axis/origin 不得为非派生来源 ----
    if (design.authority == AuthorityMode::StandardDH) {
        for (std::size_t i = 0; i < design.joints.size(); ++i) {
            const JointEntry& j = design.joints[i];
            const std::string p = "joints[" + std::to_string(i) + "]";
            // 违例＝Provided 且来源非 DerivedReadOnly（§4.10 原文"必须为派生值"；
            // §7.3 非法组合＝"axis 为 UserProvided 来源"）。NotProvided＝派生
            // 待重算（编解码后的合法载态——D-MDL-5），不违例。
            if (j.axis.state() == core::FieldState::Provided
                && j.axis.provenance().kind != core::ProvenanceKind::DerivedReadOnly) {
                addViolation(out, InvariantId::IMdl8, p + ".axis.provenance");
            }
            if (j.origin.state() == core::FieldState::Provided
                && j.origin.provenance().kind != core::ProvenanceKind::DerivedReadOnly) {
                addViolation(out, InvariantId::IMdl8, p + ".origin.provenance");
            }
        }
    }

    // ---- I-MDL-9（引用完整，值模型可判部分）----
    {
        // defaultTcp.toolOid ∈ toolRefs（表 4.3 defaultTcp 行/I-MDL-9）
        if (design.defaultTcp.has_value()) {
            bool found = false;
            for (const core::ObjectId& oid : design.toolRefs) {
                if (oid == design.defaultTcp->toolOid) { found = true; break; }
            }
            if (!found) {
                addViolation(out, InvariantId::IMdl9, "defaultTcp.toolOid");
            }
        }
        // 引用表无重复（I-MDL-9"无重复"原文——按规范文本判重，确定性）
        auto checkRefTable = [&](const std::vector<core::ObjectId>& refs, const char* table) {
            std::unordered_set<std::string> seen;
            seen.reserve(refs.size());
            for (std::size_t i = 0; i < refs.size(); ++i) {
                if (!seen.insert(refs[i].toCanonical()).second) {
                    addViolation(out, InvariantId::IMdl9,
                                 std::string(table) + "[" + std::to_string(i) + "].duplicate");
                }
            }
        };
        checkRefTable(design.toolRefs, "toolRefs");
        checkRefTable(design.sceneRefs, "sceneRefs");
        // "指向对象存在于闭包且 token 匹配"半段需要修订闭包视图（②查询
        // 端口）——归 T08 就绪校验 L1 层（§8.2），值模型层无闭包上下文
        // （范围注记见头文件；单元卡 §15 增量登记）。
    }

    // ---- I-MDL-10（资源状态机）----
    for (std::size_t i = 0; i < design.resourceManifest.size(); ++i) {
        const ResourceRef& res = design.resourceManifest[i];
        const std::string p = "resourceManifest[" + std::to_string(i) + "]";
        // Recorded 必须带 externalRecord（absPath＋recordedDigest——NFR-SEC-01）
        if (res.state == ResourceState::Recorded && !res.externalRecord.has_value()) {
            addViolation(out, InvariantId::IMdl10, p + ".externalRecord");
        }
        // Solidified 必须带 solidifiedObject（固化引用——CON-03）
        if (res.state == ResourceState::Solidified && !res.solidifiedObject.has_value()) {
            addViolation(out, InvariantId::IMdl10, p + ".solidifiedObject");
        }
        // "状态只可 Recorded→Solidified"：状态为单字段枚举，反向迁移在
        // 表示层不可表达（无历史字段）——编辑器接受固化请求时单向赋值，
        // 本处无可检查面（设计注释，非省略）。
    }

    return out;
}

}  // namespace sdurws::ird::modeling
