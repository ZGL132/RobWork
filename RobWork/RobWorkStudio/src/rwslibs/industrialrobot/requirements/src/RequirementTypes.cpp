/**
 * @file   RequirementTypes.cpp
 * @brief  requirements 值模型的实现半区——词表 token 转发表、引用/位姿/
 *         容差/区域/覆盖率/姿态规则的校验函数与集合级不变量（I-REQ-2
 *         跨集合半区）＋四类条目的逐字段等值。
 *
 * 设计依据：
 *   - units/requirements.md §4.7（I-REQ-1~10 不变量——本文件校验函数为
 *     其实现面）、§4.3/§4.4/§4.5/§5.2/§5.3/§6.1（各字段表与词表括注）、
 *     §4.2（集合对象）、§3.4（确定性总约定）
 *   - 先例：core/src/Provenance.cpp（token 转发 switch 全枚举形态）、
 *     modeling/src/Parts.cpp（值类型等值逐字段形态）
 *   - 任务契约 tasks/foundation/WP-14-T03.json acceptance 1（I-REQ-1~10）、
 *     4（构造校验错误语义）
 *
 * 线程安全：全部纯函数（无共享可变状态），并发只读安全。
 * 确定性（NFR-COR-01/02）：全部比较为值精确比较/字典序；无 locale/环境
 * 依赖；数值格式化用定点十进制（std::to_string——不经 locale 全局面）。
 */

#include <sdurws/ird/requirements/RequirementTypes.hpp>

#include <cmath>
#include <limits>

namespace sdurws::ird::requirements {

// =====================================================================
// 词表 token 转发表（switch 全枚举、无 default——新增枚举值未登记表项
// 时编译器告警暴露遗漏；try 轨精确等值、无大小写折叠——"不猜测"纪律，
// ObjectTypes.hpp 同款）
// =====================================================================

std::string_view requirementLevelToken(RequirementLevel level) noexcept
{
    switch (level) {
    case RequirementLevel::Must: return "Must";
    case RequirementLevel::Should: return "Should";
    }
    return {};
}

std::optional<RequirementLevel> tryRequirementLevel(std::string_view token) noexcept
{
    // 两级词表精确匹配；"Info" 等旧词表值不承接（§2.5——信息归 note）。
    if (token == "Must") { return RequirementLevel::Must; }
    if (token == "Should") { return RequirementLevel::Should; }
    return std::nullopt;
}

std::string_view segmentAxisToken(SegmentAxis axis) noexcept
{
    switch (axis) {
    case SegmentAxis::ToolZ: return "ToolZ";
    case SegmentAxis::ReferenceZ: return "ReferenceZ";
    }
    return {};
}

std::optional<SegmentAxis> trySegmentAxis(std::string_view token) noexcept
{
    if (token == "ToolZ") { return SegmentAxis::ToolZ; }
    if (token == "ReferenceZ") { return SegmentAxis::ReferenceZ; }
    return std::nullopt;
}

std::string_view orientationRuleKindToken(OrientationRuleKind kind) noexcept
{
    switch (kind) {
    case OrientationRuleKind::Fixed: return "Fixed";
    case OrientationRuleKind::AlignFrame: return "AlignFrame";
    case OrientationRuleKind::AlignGeometryNormal: return "AlignGeometryNormal";
    case OrientationRuleKind::PointAtTarget: return "PointAtTarget";
    case OrientationRuleKind::ToolRollFree: return "ToolRollFree";
    }
    return {};
}

std::optional<OrientationRuleKind> tryOrientationRuleKind(std::string_view token) noexcept
{
    if (token == "Fixed") { return OrientationRuleKind::Fixed; }
    if (token == "AlignFrame") { return OrientationRuleKind::AlignFrame; }
    if (token == "AlignGeometryNormal") { return OrientationRuleKind::AlignGeometryNormal; }
    if (token == "PointAtTarget") { return OrientationRuleKind::PointAtTarget; }
    if (token == "ToolRollFree") { return OrientationRuleKind::ToolRollFree; }
    return std::nullopt;
}

std::string_view orientationFeatureToken(OrientationFeature feature) noexcept
{
    switch (feature) {
    case OrientationFeature::FrameOrigin: return "FrameOrigin";
    case OrientationFeature::FramePlaneNormal: return "FramePlaneNormal";
    }
    return {};
}

std::optional<OrientationFeature> tryOrientationFeature(std::string_view token) noexcept
{
    if (token == "FrameOrigin") { return OrientationFeature::FrameOrigin; }
    if (token == "FramePlaneNormal") { return OrientationFeature::FramePlaneNormal; }
    return std::nullopt;
}

std::string_view conditionEventTypeToken(ConditionEventType type) noexcept
{
    switch (type) {
    case ConditionEventType::Grasp: return "Grasp";
    case ConditionEventType::Release: return "Release";
    case ConditionEventType::Dwell: return "Dwell";
    }
    return {};
}

std::optional<ConditionEventType> tryConditionEventType(std::string_view token) noexcept
{
    if (token == "Grasp") { return ConditionEventType::Grasp; }
    if (token == "Release") { return ConditionEventType::Release; }
    if (token == "Dwell") { return ConditionEventType::Dwell; }
    return std::nullopt;
}

std::string_view appliesToScopeToken(AppliesToScope scope) noexcept
{
    switch (scope) {
    case AppliesToScope::AllStations: return "AllStations";
    case AppliesToScope::Stations: return "Stations";
    case AppliesToScope::None: return "None";
    }
    return {};
}

std::optional<AppliesToScope> tryAppliesToScope(std::string_view token) noexcept
{
    if (token == "AllStations") { return AppliesToScope::AllStations; }
    if (token == "Stations") { return AppliesToScope::Stations; }
    if (token == "None") { return AppliesToScope::None; }
    return std::nullopt;
}

std::string_view requirementRefKindToken(RequirementRefKind kind) noexcept
{
    switch (kind) {
    case RequirementRefKind::World: return "World";
    case RequirementRefKind::ModelFrame: return "ModelFrame";
    case RequirementRefKind::SceneObject: return "SceneObject";
    case RequirementRefKind::Tool: return "Tool";
    case RequirementRefKind::DefaultTcp: return "DefaultTcp";
    }
    return {};
}

std::optional<RequirementRefKind> tryRequirementRefKind(std::string_view token) noexcept
{
    if (token == "World") { return RequirementRefKind::World; }
    if (token == "ModelFrame") { return RequirementRefKind::ModelFrame; }
    if (token == "SceneObject") { return RequirementRefKind::SceneObject; }
    if (token == "Tool") { return RequirementRefKind::Tool; }
    if (token == "DefaultTcp") { return RequirementRefKind::DefaultTcp; }
    return std::nullopt;
}

std::string_view positionSamplingMethodToken(PositionSamplingMethod method) noexcept
{
    switch (method) {
    case PositionSamplingMethod::Grid: return "Grid";
    case PositionSamplingMethod::GridBySpacing: return "GridBySpacing";
    case PositionSamplingMethod::Random: return "Random";
    }
    return {};
}

std::optional<PositionSamplingMethod> tryPositionSamplingMethod(std::string_view token) noexcept
{
    if (token == "Grid") { return PositionSamplingMethod::Grid; }
    if (token == "GridBySpacing") { return PositionSamplingMethod::GridBySpacing; }
    if (token == "Random") { return PositionSamplingMethod::Random; }
    return std::nullopt;
}

// =====================================================================
// I-REQ-4：引用匹配表单点实现（token 字面＝modeling 侧对象 token 的元数据
// 交叉引用——R-1 禁 include 其头文件，§12 交接行同口径）
// =====================================================================

std::string_view expectedTargetToken(RequirementRefKind kind) noexcept
{
    switch (kind) {
    case RequirementRefKind::ModelFrame: return "robot-design";   // 模型坐标系承载对象
    case RequirementRefKind::SceneObject: return "scene-object";  // MDL-15 场景对象
    case RequirementRefKind::Tool: return "tool-definition";      // 工具定义对象
    case RequirementRefKind::World:
    case RequirementRefKind::DefaultTcp:
        return {};  // 无目标——不参与 token 匹配（恒合法缺省种）
    }
    return {};
}

bool RequirementReference::wellFormed() const noexcept
{
    // 有载荷种：ObjectId 必须有效（非全零保留值）；Tool 另要求 tcpKey
    // 非空（工具 TCP 定位键——§4.3 Tool{oid, tcpKey} 括注）。
    switch (kind) {
    case RequirementRefKind::ModelFrame:
    case RequirementRefKind::SceneObject:
        return objectId.has_value() && objectId->isValid() && tcpKey.empty();
    case RequirementRefKind::Tool:
        return objectId.has_value() && objectId->isValid() && !tcpKey.empty();
    case RequirementRefKind::World:
    case RequirementRefKind::DefaultTcp:
        // 无载荷种：不得携带任何载荷（静默携带＝结构违约——拒绝）。
        return !objectId.has_value() && tcpKey.empty();
    }
    return false;
}

bool RequirementReference::operator==(const RequirementReference& o) const
{
    return kind == o.kind && objectId == o.objectId && tcpKey == o.tcpKey;
}

// =====================================================================
// 校验函数（§4.7 I-REQ 实现面；短路——首个违例即返回，确定性报首）
// =====================================================================

namespace {

/// 有限 double 判定（NaN/±Inf 一律非法——附录 D"非有限即非法"口径）。
bool isFinite(double v) noexcept
{
    return std::isfinite(v);
}

/// 非法参数错误构造（域错误表"参数非法"族就近承载——码面语义见各
/// validate* 函数注释；params 携 field/原值定位，数值定点十进制）。
RequirementError illegalField(std::string field, double value, std::string detail)
{
    RequirementError e;
    e.code = RequirementErrorCode::IllegalTolerance;
    e.params.emplace_back("field", std::move(field));
    e.params.emplace_back("value", std::to_string(value));
    e.detail = std::move(detail);
    return e;
}

}  // namespace

std::optional<RequirementError> validateOrientationRule(const OrientationRule& rule)
{
    // 逐规则核对载荷（§5.3 五规则——acceptance 4"五规则姿态字段模型非法
    // 参数构造边界拒绝"的实现点；每分支注释给业务反例语义）。
    switch (rule.kind) {
    case OrientationRuleKind::Fixed:
        // 非有限角（NaN/Inf）＝无法解析的参数字面——拒绝不改写（§5.3
        // "非法旋转"行；等价角不归一——NFR-COR-03）。
        if (!isFinite(rule.fixedRpy[0]) || !isFinite(rule.fixedRpy[1])
            || !isFinite(rule.fixedRpy[2])) {
            return illegalField("fixedRpy", rule.fixedRpy[0],
                                "requirements/types: Fixed 规则欧拉角含非有限值"
                                "（rad；§5.3 非法旋转拒绝）");
        }
        return std::nullopt;

    case OrientationRuleKind::AlignFrame:
        // 对齐目标必须是一个结构自洽的参考系引用（ModelFrame/SceneObject）。
        if (!rule.targetFrame.wellFormed()
            || (rule.targetFrame.kind != RequirementRefKind::ModelFrame
                && rule.targetFrame.kind != RequirementRefKind::SceneObject)) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "targetFrame");
            e.detail = "requirements/types: AlignFrame 目标参考系引用结构非法"
                       "（§5.3——须为 ModelFrame/SceneObject 且载荷自洽）";
            return e;
        }
        return std::nullopt;

    case OrientationRuleKind::AlignGeometryNormal:
        // feature 必须显式给出（"缺 feature"反例——acceptance 4；不猜
        // 缺省特征——NFR-COR-03 不静默补全）。
        if (!rule.feature.has_value()) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "feature");
            e.detail = "requirements/types: AlignGeometryNormal 缺 feature"
                       "（§5.3——几何特征必须显式登记）";
            return e;
        }
        if (!rule.targetSceneObject.has_value() || !rule.targetSceneObject->isValid()) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "targetSceneObject");
            e.detail = "requirements/types: AlignGeometryNormal 目标场景对象缺失"
                       "或无效（§5.3——MDL-15 场景对象浅引用）";
            return e;
        }
        return std::nullopt;

    case OrientationRuleKind::PointAtTarget:
        // 零向量目标＝"工具轴指向原点"的退化语义——拒绝（acceptance 4
        // "零向量目标"反例；ZeroVectorTarget 是域错误表既有值）。
        if (rule.targetPoint[0] == 0.0 && rule.targetPoint[1] == 0.0
            && rule.targetPoint[2] == 0.0) {
            RequirementError e;
            e.code = RequirementErrorCode::ZeroVectorTarget;
            e.detail = "requirements/types: PointAtTarget 目标为零向量"
                       "（§5.3——位姿目标不退化为零向量点）";
            return e;
        }
        if (!isFinite(rule.targetPoint[0]) || !isFinite(rule.targetPoint[1])
            || !isFinite(rule.targetPoint[2])) {
            return illegalField("targetPoint", rule.targetPoint[0],
                                "requirements/types: PointAtTarget 目标点含非有限"
                                "坐标（m；§5.3 非法参数拒绝）");
        }
        return std::nullopt;

    case OrientationRuleKind::ToolRollFree:
        // 区间须有序且非空（逆序/等值均拒绝——acceptance 4"rollRange 逆序"
        // 反例；rad，默认 [−π,π]）。
        if (!rule.rollRange.wellFormed()) {
            return illegalField("rollRange", rule.rollRange.min,
                                "requirements/types: ToolRollFree rollRange 逆序或"
                                "空区间（rad，须 min<max 且有限——§5.3 第五规则）");
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<RequirementError> validateTolerance(const ToleranceSpec& tolerance)
{
    // I-REQ-5 后半：两分量 >0 且有限（m/rad——"要求值"，评估按此校验残差）。
    if (!isFinite(tolerance.positionTolerance) || tolerance.positionTolerance <= 0.0) {
        return illegalField("positionTolerance", tolerance.positionTolerance,
                            "requirements/types: 位置容差须为正有限值"
                            "（m；I-REQ-5）");
    }
    if (!isFinite(tolerance.orientationTolerance) || tolerance.orientationTolerance <= 0.0) {
        return illegalField("orientationTolerance", tolerance.orientationTolerance,
                            "requirements/types: 姿态容差须为正有限值"
                            "（rad；I-REQ-5）");
    }
    return std::nullopt;
}

std::optional<RequirementError> validatePoseConstraint(const PoseConstraint& pose)
{
    // ①I-REQ-5 前半：至少约束一个分量（全 false＝解空间无界——任务点
    // 失去位姿语义）。
    if (!pose.constrainedDof.any()) {
        RequirementError e;
        e.code = RequirementErrorCode::AllDofFree;
        e.detail = "requirements/types: constrainedDof 全 false（任务点至少"
                   "约束一个分量——I-REQ-5）";
        return e;
    }
    // ②position Provided 时三分量必须有限（§4.3 约束行；NotProvided 合法
    // ——MDL-06 缺失不转零，此处不做值核对）。
    if (const auto pos = pose.position.tryValue()) {
        if (!isFinite((*pos)[0]) || !isFinite((*pos)[1]) || !isFinite((*pos)[2])) {
            return illegalField("position", (*pos)[0],
                                "requirements/types: 受约束位置含非有限分量"
                                "（m；§4.3 Provided 三分量有限）");
        }
    }
    // ③姿态规则参数（五规则逐条——validateOrientationRule）。
    return validateOrientationRule(pose.orientation);
}

std::optional<RequirementError> validateBoundingBox(const BoundingBox& box)
{
    // I-REQ-6：size 三分量 >0 且有限（零体积/负尺寸＝退化区域——零样本
    // 计划无意义；center 仅查有限——位置自由）。
    const double comps[3] = {box.size[0], box.size[1], box.size[2]};
    static const char* axes[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        if (!isFinite(comps[i]) || comps[i] <= 0.0) {
            RequirementError e;
            e.code = RequirementErrorCode::DegenerateRegion;
            e.params.emplace_back("axis", axes[i]);
            e.params.emplace_back("value", std::to_string(comps[i]));
            e.detail = "requirements/types: 区域盒尺寸退化（m；三分量>0 且"
                       "有限——I-REQ-6 非退化）";
            return e;
        }
    }
    if (!isFinite(box.center[0]) || !isFinite(box.center[1])
        || !isFinite(box.center[2])) {
        RequirementError e;
        e.code = RequirementErrorCode::DegenerateRegion;
        e.params.emplace_back("axis", "center");
        e.detail = "requirements/types: 区域盒中心含非有限坐标（m）";
        return e;
    }
    return std::nullopt;
}

std::optional<RequirementError> validateCoverageTargets(const CoverageTargets& targets)
{
    // I-REQ-6：覆盖率 ∈[0,1]（闭区间——0/1 是合法目标：0＝不设要求、
    // 1＝全覆盖要求）。
    if (!isFinite(targets.minPositionCoverage) || targets.minPositionCoverage < 0.0
        || targets.minPositionCoverage > 1.0) {
        RequirementError e;
        e.code = RequirementErrorCode::DegenerateRegion;
        e.params.emplace_back("field", "minPositionCoverage");
        e.params.emplace_back("value", std::to_string(targets.minPositionCoverage));
        e.detail = "requirements/types: 位置覆盖率目标越界（∈[0,1]——I-REQ-6）";
        return e;
    }
    if (targets.minOrientationCoverage.has_value()) {
        const double v = *targets.minOrientationCoverage;
        if (!isFinite(v) || v < 0.0 || v > 1.0) {
            RequirementError e;
            e.code = RequirementErrorCode::DegenerateRegion;
            e.params.emplace_back("field", "minOrientationCoverage");
            e.params.emplace_back("value", std::to_string(v));
            e.detail = "requirements/types: 姿态覆盖率目标越界（∈[0,1]——I-REQ-6）";
            return e;
        }
    }
    return std::nullopt;
}

std::optional<RequirementError> validateRequirementReference(const RequirementReference& ref,
                                                             bool forScene)
{
    // 场景词表（I-REQ-4 的使用场景半区）：refFrame 只允许世界/模型系/
    // 场景对象；tcpRef 只允许工具/缺省 TCP。
    const bool sceneKind = ref.kind == RequirementRefKind::World
                        || ref.kind == RequirementRefKind::ModelFrame
                        || ref.kind == RequirementRefKind::SceneObject;
    const bool tcpKind = ref.kind == RequirementRefKind::Tool
                      || ref.kind == RequirementRefKind::DefaultTcp;
    if (forScene && !sceneKind) {
        RequirementError e;
        e.code = RequirementErrorCode::IllegalTolerance;
        e.params.emplace_back("ref-kind", std::string(requirementRefKindToken(ref.kind)));
        e.params.emplace_back("allowed-kinds", "World|ModelFrame|SceneObject");
        e.detail = "requirements/types: refFrame 场景出现 tcpRef 专用引用种"
                   "（I-REQ-4 场景词表）";
        return e;
    }
    if (!forScene && !tcpKind) {
        RequirementError e;
        e.code = RequirementErrorCode::IllegalTolerance;
        e.params.emplace_back("ref-kind", std::string(requirementRefKindToken(ref.kind)));
        e.params.emplace_back("allowed-kinds", "Tool|DefaultTcp");
        e.detail = "requirements/types: tcpRef 场景出现 refFrame 专用引用种"
                   "（I-REQ-4 场景词表）";
        return e;
    }
    // 结构半区：载荷与 kind 匹配（wellFormed——有载荷种须有效 id 等）。
    if (!ref.wellFormed()) {
        RequirementError e;
        e.code = RequirementErrorCode::IllegalTolerance;
        e.params.emplace_back("ref-kind", std::string(requirementRefKindToken(ref.kind)));
        e.detail = "requirements/types: 引用载荷与 kind 不匹配（I-REQ-4 结构半区"
                   "——目标 id 缺失/无效或 tcpKey 缺失或无载荷种携载荷）";
        return e;
    }
    return std::nullopt;
}

// =====================================================================
// 条目级校验入口（Services/Codec 共用语义单源——NFR-MNT-04）
// =====================================================================

std::optional<RequirementError> validateTaskPoint(const TaskPoint& point)
{
    // 校验序＝字段表阅读序：引用（refFrame/tcpRef）→位姿（含姿态规则）
    // →容差→三段（启用段距离>0）。短路返回首个违例。
    if (auto e = validateRequirementReference(point.refFrame, /*forScene=*/true)) {
        return e;
    }
    if (point.tcpRef.has_value()) {
        if (auto e = validateRequirementReference(*point.tcpRef, /*forScene=*/false)) {
            return e;
        }
    }
    if (auto e = validatePoseConstraint(point.pose)) {
        return e;
    }
    if (auto e = validateTolerance(point.tolerance)) {
        return e;
    }
    // 三段校验：仅核对启用段的距离（m，>0 有限）——work 段恒启用（即
    // 任务点本身，§4.3）恒过距离门；approach/retract 可关闭（关闭段不
    // 进入 TRJ 路径构造，距离值不核）。
    const TaskSegment* segs[3] = {&point.approach, &point.work, &point.retract};
    static const char* segNames[3] = {"approach", "work", "retract"};
    for (int i = 0; i < 3; ++i) {
        if (segs[i]->enabled
            && (!isFinite(segs[i]->distanceM) || segs[i]->distanceM <= 0.0)) {
            return illegalField(std::string(segNames[i]) + ".distanceM",
                                segs[i]->distanceM,
                                "requirements/types: 启用段距离须为正有限值"
                                "（m；§5.1 进入/离开语义）");
        }
    }
    return std::nullopt;
}

std::optional<RequirementError> validateWorkRegion(const WorkRegion& region)
{
    // 校验序：引用→盒（非退化）→覆盖率目标→采样定义。短路同上。
    if (auto e = validateRequirementReference(region.refFrame, /*forScene=*/true)) {
        return e;
    }
    if (region.tcpRef.has_value()) {
        if (auto e = validateRequirementReference(*region.tcpRef, /*forScene=*/false)) {
            return e;
        }
    }
    if (auto e = validateBoundingBox(region.box)) {
        return e;
    }
    if (auto e = validateCoverageTargets(region.coverageTargets)) {
        return e;
    }
    // 位置采样定义：GridBySpacing 间距三分量>0 有限（编辑态表达——规范
    // 化在 buildPlan；间距非法则规范化无定义）；Grid/Random 计数天然
    // uint32 非负（I-REQ-6"计数≥0"由类型面保证——负数不可表达）。
    if (region.positionSampling.method == PositionSamplingMethod::GridBySpacing) {
        for (int i = 0; i < 3; ++i) {
            if (!isFinite(region.positionSampling.spacing[static_cast<std::size_t>(i)])
                || region.positionSampling.spacing[static_cast<std::size_t>(i)] <= 0.0) {
                return illegalField("positionSampling.spacing",
                                    region.positionSampling.spacing[static_cast<std::size_t>(i)],
                                    "requirements/types: 间距式采样间距须为正有限值"
                                    "（m；§5.2 GridBySpacing）");
            }
        }
    }
    // 姿态采样：两组计数 ≥1（§5.2"≥1"——零样本仅位置侧表达）。
    if (region.orientationSampling.directionSamples < 1
        || region.orientationSampling.rollSamples < 1) {
        RequirementError e;
        e.code = RequirementErrorCode::NegativeCount;
        e.params.emplace_back("field", "orientationSampling");
        e.detail = "requirements/types: 姿态采样计数须 ≥1（§5.2——零样本仅"
                   "位置侧 counts 乘积=0 表达）";
        return e;
    }
    return std::nullopt;
}

std::optional<RequirementError> validateOperatingCondition(const OperatingCondition& condition)
{
    // 引用组：环境（SceneObject 浅引用）＋工具（Tool 引用）逐个结构核对。
    for (const auto& env : condition.environmentRefs) {
        if (!env.isValid()) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "environmentRefs");
            e.detail = "requirements/types: 环境障碍引用无效（scene-object 浅引用"
                       "须有效 ObjectId）";
            return e;
        }
    }
    for (const auto& tool : condition.toolRefs) {
        if (auto e = validateRequirementReference(tool, /*forScene=*/false)) {
            return e;
        }
    }
    // 负载组：挂载工具引用核对；物性 Provided 时须有限且质量>0（NotProvided
    // 合法——DataInsufficient 降级预告，§6.1）。
    for (const auto& payload : condition.payloads) {
        if (auto e = validateRequirementReference(payload.toolRef, /*forScene=*/false)) {
            return e;
        }
        if (const auto m = payload.mass.tryValue()) {
            if (!isFinite(*m) || *m < 0.0) {
                return illegalField("payload.mass", *m,
                                    "requirements/types: 负载质量须为非负有限值"
                                    "（kg；§4.5 物性四态）");
            }
        }
        if (const auto c = payload.com.tryValue()) {
            if (!isFinite((*c)[0]) || !isFinite((*c)[1]) || !isFinite((*c)[2])) {
                return illegalField("payload.com", (*c)[0],
                                    "requirements/types: 负载质心含非有限分量"
                                    "（m）");
            }
        }
        if (const auto j = payload.inertia.tryValue()) {
            if (!isFinite(*j) || *j < 0.0) {
                return illegalField("payload.inertia", *j,
                                    "requirements/types: 负载惯量须为非负有限值"
                                    "（kg·m²）");
            }
        }
    }
    // 事件组：绑定任务点引用有效；驻留时长（s）Provided 时须非负有限。
    for (const auto& event : condition.events) {
        if (!event.stationRef.isValid()) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "events.stationRef");
            e.detail = "requirements/types: 事件绑定任务点引用无效（§4.5）";
            return e;
        }
        if (event.durationS.has_value()
            && (!isFinite(*event.durationS) || *event.durationS < 0.0)) {
            return illegalField("events.durationS", *event.durationS,
                                "requirements/types: 事件时长须为非负有限值（s）");
        }
    }
    // 节拍：目标节拍（s）Provided 时须正有限。
    if (condition.targetCycleTimeS.has_value()
        && (!isFinite(*condition.targetCycleTimeS) || *condition.targetCycleTimeS <= 0.0)) {
        return illegalField("targetCycleTimeS", *condition.targetCycleTimeS,
                            "requirements/types: 目标节拍须为正有限值（s；§4.5）");
    }
    // 适用范围：Stations 显式清单须非空（§4.7 非法组合行——空清单＋
    // Stations＝无效组合；None＋enabled 合法——§6.1 不适用标记）。
    if (condition.appliesTo.scope == AppliesToScope::Stations
        && condition.appliesTo.stations.empty()) {
        RequirementError e;
        e.code = RequirementErrorCode::IllegalTolerance;
        e.params.emplace_back("field", "appliesTo.stations");
        e.detail = "requirements/types: appliesTo.scope=Stations 且清单为空"
                   "（§4.7 非法组合）";
        return e;
    }
    for (const auto& s : condition.appliesTo.stations) {
        if (!s.isValid()) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "appliesTo.stations");
            e.detail = "requirements/types: appliesTo 工位引用无效";
            return e;
        }
    }
    // 名称非空（集合唯一性归 checkEntryNameAndIdUniqueness——此处只挡空名，
    // 与该函数的空名分支同语义；工况单条构造路径经服务到达时保证一致）。
    if (condition.name.empty()) {
        RequirementError e;
        e.code = RequirementErrorCode::DuplicateName;
        e.params.emplace_back("index", "0");
        e.detail = "requirements/types: 工况名称为空（§8.1 R0 非空）";
        return e;
    }
    return std::nullopt;
}

std::optional<RequirementError> validateSamplingPlan(const SamplingPlan& plan)
{
    // 计划条目恒为规范化形态（Grid 权威计数——D-REQ-2）：GridBySpacing
    // 在字节面/计划面出现＝绕过 buildPlan 的违约（buildPlan 是唯一规范
    // 化入口，§5.2）。
    if (plan.positionSampling.method != PositionSamplingMethod::Grid) {
        RequirementError e;
        e.code = RequirementErrorCode::MalformedPayload;
        e.params.emplace_back("field", "positionSampling.method");
        e.detail = "requirements/types: 计划条目位置采样须为规范化 Grid 形态"
                   "（D-REQ-2——GridBySpacing 在 buildPlan 规范化，计划面不"
                   "承载原始间距）";
        return e;
    }
    // 姿态采样 ≥1（同区域侧口径——§5.2）。
    if (plan.orientationSampling.directionSamples < 1
        || plan.orientationSampling.rollSamples < 1) {
        RequirementError e;
        e.code = RequirementErrorCode::NegativeCount;
        e.params.emplace_back("field", "orientationSampling");
        e.detail = "requirements/types: 计划姿态采样计数须 ≥1（§5.2）";
        return e;
    }
    // 区域引用（WorkRegion 子条目 ObjectId）须有效——跨集合模型内锚
    // （O-36 口径），悬空核对归就绪层 R6（T05）。
    if (!plan.regionRef.isValid()) {
        RequirementError e;
        e.code = RequirementErrorCode::IllegalTolerance;
        e.params.emplace_back("field", "regionRef");
        e.detail = "requirements/types: 计划区域引用无效";
        return e;
    }
    return std::nullopt;
}

// =====================================================================
// 集合级不变量（I-REQ-2 跨集合半区）
// =====================================================================

std::optional<RequirementError> checkCrossSetIdUniqueness(const std::vector<TaskPoint>& points,
                                                          const std::vector<WorkRegion>& regions,
                                                          const std::vector<OperatingCondition>& conditions,
                                                          const std::vector<SamplingPlan>& plans)
{
    // 收集全部条目 id 后逐对核对（规模小——五集合合计数千 id，O(n²) 的
    // 常数可接受；保持扫描序确定性）。身份不混用（I-REQ-2）：同一
    // ObjectId 只能属于一个集合（token 与所属集合一致的字节面表达）。
    std::vector<core::ObjectId> ids;
    ids.reserve(points.size() + regions.size() + conditions.size() + plans.size());
    for (const auto& p : points) { ids.push_back(p.objectId); }
    for (const auto& r : regions) { ids.push_back(r.objectId); }
    for (const auto& c : conditions) { ids.push_back(c.objectId); }
    for (const auto& pl : plans) { ids.push_back(pl.objectId); }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (ids[i] == ids[j]) {
                RequirementError e;
                e.code = RequirementErrorCode::MalformedPayload;
                e.params.emplace_back("object-id", ids[i].toCanonical());
                e.detail = "requirements/types: 同一 ObjectId 跨集合重复"
                           "（I-REQ-2 身份不混用——构造边界拒绝）";
                return e;
            }
        }
    }
    return std::nullopt;
}

// =====================================================================
// 四类条目逐字段等值（附录 D 第 12 项——值精确比较，double 无容差：
// 内容身份按字面计算，§5.3"等价姿态与内容身份"行同源）
// =====================================================================

bool RollRange::wellFormed() const noexcept
{
    // 有序且非空区间＋两侧有限（rad）。
    return isFinite(min) && isFinite(max) && min < max;
}

bool OrientationRule::operator==(const OrientationRule& o) const
{
    const double* a = &fixedRpy[0];
    const double* b = &o.fixedRpy[0];
    const double* c = &targetPoint[0];
    const double* d = &o.targetPoint[0];
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i] || c[i] != d[i]) { return false; }
    }
    return kind == o.kind && targetFrame == o.targetFrame
        && targetSceneObject == o.targetSceneObject && feature == o.feature
        && invertNormal == o.invertNormal && rollRange == o.rollRange;
}

bool PoseConstraint::operator==(const PoseConstraint& o) const
{
    return position == o.position && constrainedDof == o.constrainedDof
        && orientation == o.orientation;
}

bool RequirementDemand::operator==(const RequirementDemand& o) const
{
    // 裕量比较：同缺省（均 nullopt）相等；均有值则精确比较（NaN 视不等
    // ——NaN 与任何值比较为 false，天然不通过 ==；NaN 输入在校验边界
    // 已拒绝，此处为防御面）。
    if (collisionFreeRequired != o.collisionFreeRequired) { return false; }
    if (minimumJointMargin.has_value() != o.minimumJointMargin.has_value()) {
        return false;
    }
    return !minimumJointMargin.has_value() || *minimumJointMargin == *o.minimumJointMargin;
}

bool GenerationProvenance::operator==(const GenerationProvenance& o) const
{
    return generatorId == o.generatorId && instanceId == o.instanceId
        && linked == o.linked && parameters == o.parameters;
}

bool TaskPoint::operator==(const TaskPoint& o) const
{
    return objectId == o.objectId && name == o.name && processTag == o.processTag
        && level == o.level && enabled == o.enabled && source == o.source
        && generation == o.generation && importProvenance == o.importProvenance
        && refFrame == o.refFrame && tcpRef == o.tcpRef && pose == o.pose
        && tolerance == o.tolerance && approach == o.approach && work == o.work
        && retract == o.retract && demands == o.demands
        && sequenceKey == o.sequenceKey && note == o.note;
}

bool WorkRegion::operator==(const WorkRegion& o) const
{
    return objectId == o.objectId && name == o.name && level == o.level
        && enabled == o.enabled && source == o.source && generation == o.generation
        && importProvenance == o.importProvenance && refFrame == o.refFrame
        && tcpRef == o.tcpRef && box == o.box && positionSampling == o.positionSampling
        && orientationSampling == o.orientationSampling
        && coverageTargets == o.coverageTargets && demands == o.demands
        && sequenceKey == o.sequenceKey && note == o.note;
}

bool ConditionPayload::operator==(const ConditionPayload& o) const
{
    return toolRef == o.toolRef && mass == o.mass && com == o.com && inertia == o.inertia;
}

bool ConditionEvent::operator==(const ConditionEvent& o) const
{
    return type == o.type && stationRef == o.stationRef
        && durationS == o.durationS;
}

bool AppliesTo::operator==(const AppliesTo& o) const
{
    if (scope != o.scope || stations.size() != o.stations.size()) { return false; }
    for (std::size_t i = 0; i < stations.size(); ++i) {
        if (!(stations[i] == o.stations[i])) { return false; }
    }
    return true;
}

bool OperatingCondition::operator==(const OperatingCondition& o) const
{
    if (objectId != o.objectId || name != o.name || level != o.level
        || enabled != o.enabled || targetCycleTimeS != o.targetCycleTimeS
        || !(demands == o.demands)) {
        return false;
    }
    // 向量字段逐个比较（size 相同＋逐元素相等——vector::operator== 语义）。
    return environmentRefs == o.environmentRefs && toolRefs == o.toolRefs
        && payloads == o.payloads && events == o.events
        && verificationOrderHint == o.verificationOrderHint
        && appliesTo == o.appliesTo && note == o.note;
}

bool SamplingPlan::operator==(const SamplingPlan& o) const
{
    return objectId == o.objectId && regionRef == o.regionRef
        && positionSampling == o.positionSampling
        && orientationSampling == o.orientationSampling && note == o.note;
}

bool PositionSampling::operator==(const PositionSampling& o) const
{
    for (int i = 0; i < 3; ++i) {
        if (counts[static_cast<std::size_t>(i)] != o.counts[static_cast<std::size_t>(i)]) {
            return false;
        }
        if (spacing[static_cast<std::size_t>(i)]
            != o.spacing[static_cast<std::size_t>(i)]) {
            return false;
        }
    }
    return method == o.method && count == o.count;
}

bool CoverageTargets::operator==(const CoverageTargets& o) const
{
    if (minPositionCoverage != o.minPositionCoverage) { return false; }
    if (minOrientationCoverage.has_value() != o.minOrientationCoverage.has_value()) {
        return false;
    }
    return !minOrientationCoverage.has_value()
        || *minOrientationCoverage == *o.minOrientationCoverage;
}

bool BoundingBox::operator==(const BoundingBox& o) const
{
    const double* cc = &center[0];
    const double* co = &o.center[0];
    const double* sc = &size[0];
    const double* so = &o.size[0];
    for (int i = 0; i < 3; ++i) {
        if (cc[i] != co[i] || sc[i] != so[i]) { return false; }
    }
    return true;
}

bool RequirementSet::operator==(const RequirementSet& o) const
{
    return schemaVersion == o.schemaVersion && name == o.name
        && pointSetRef == o.pointSetRef && regionSetRef == o.regionSetRef
        && conditionSetRef == o.conditionSetRef && planSetRef == o.planSetRef
        && note == o.note;
}

bool PointSet::operator==(const PointSet& o) const
{
    return schemaVersion == o.schemaVersion && entries == o.entries;
}

bool RegionSet::operator==(const RegionSet& o) const
{
    return schemaVersion == o.schemaVersion && entries == o.entries;
}

bool ConditionSet::operator==(const ConditionSet& o) const
{
    return schemaVersion == o.schemaVersion && entries == o.entries;
}

bool PlanSet::operator==(const PlanSet& o) const
{
    return schemaVersion == o.schemaVersion && entries == o.entries;
}

bool RequiredCaseResolution::operator==(const RequiredCaseResolution& o) const
{
    return entries == o.entries && requiredCount == o.requiredCount;
}

bool RequirementProfile::operator==(const RequirementProfile& o) const
{
    return requiredCases == o.requiredCases && mustCount == o.mustCount
        && shouldCount == o.shouldCount && minPositionCoverage == o.minPositionCoverage
        && contentIdentity == o.contentIdentity;
}

}  // namespace sdurws::ird::requirements
