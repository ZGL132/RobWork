/**
 * @file   Continuity.cpp
 * @brief  路径连接与连续性检查的实现翻译单元——段边界几何连续守卫（§9.1
 *         位置/姿态维）＋ trj.cartesian-ik-continuity 适用性双路判定（§8.1
 *         C2；WP-16-T05 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §9.1（维度表与容差来源合法性——附录 D C7 引用，
 *     本文件零第二数值）、§9.2（非法连接诊断示例——TRJ-CONTINUITY-BROKEN
 *     素材字段）、§9.3（检查执行点——几何连续即时检查）、§15.3（接口
 *     前置与输出）、§13.3（适用条件＝路径含笛卡尔段）
 *   - REQUIREMENTS 附录 D C4（比较公式 |a−b| ≤ ε_rel·|ref|＋ε_abs——经
 *     core::closeWithin 逐元素）与 C7（运行校验 ε_abs：长度/角度
 *     1×10⁻¹²；§8.1 表 4 C2（不适用显式标记不计缺失）
 *   - 需求 TRJ-02/TRJ-06；任务契约 tasks/foundation/WP-16-T05.json
 *     （acceptance 1——双路用例）
 *
 * 确定性（NFR-COR-02）：边界遍历定序、比较逐元素、素材按边界×维度序
 * 产出——同输入同报告。
 */

#include <sdurws/ird/trajectory/Continuity.hpp>

#include <rw/math/Quaternion.hpp>           // 姿态角差（相对旋转四元数化）
#include <rw/math/Rotation3D.hpp>           // inverse（相对旋转构造）
#include <rw/math/Vector3D.hpp>             // 位置逐分量差

#include <sdurws/ird/core/Compare.hpp>      // core::closeWithin/Tolerance（附录 D C4 比较公式）
#include <sdurws/ird/core/Units.hpp>        // core::UnitToken（比较型素材单位——m/rad）
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace sdurws::ird::trajectory {

namespace {

/// 有限性判定（NaN 与 ±inf 均拒绝——NFR-COR-03）。
bool isFiniteDouble(double v)
{
    return v == v && v - v == 0.0;
}

/// 姿态角差（rad）：相对旋转 R_rel = R_a · R_bᵀ 的最短弧角——以相对
/// 姿态四元数的 2·atan2(‖vec‖, |w|) 计算（落在 [0,π]，与 CartesianLine
/// 的插值最短弧口径同源；旋转无量纲）。
double rotationAngleBetween(const rw::math::Rotation3D<double>& Ra,
                            const rw::math::Rotation3D<double>& Rb)
{
    const rw::math::Rotation3D<double> Rrel = Ra * rw::math::inverse(Rb);
    const rw::math::Quaternion<double> q(Rrel);
    const double x = q.getQx();
    const double y = q.getQy();
    const double z = q.getQz();
    const double w = q.getQw();
    const double n2 = x * x + y * y + z * z + w * w;
    if (n2 <= 0.0) {
        return 0.0;  // 退化四元数防御（正交输入下不可达——实现缺陷护栏）
    }
    const double inv = 1.0 / std::sqrt(n2);
    const double vecNorm = std::sqrt(x * x + y * y + z * z) * inv;
    return 2.0 * std::atan2(vecNorm, std::fabs(w) * inv);
}

/// 单位 token 安全取值（core 注册表冻结项——find 失败属 core 缺陷，
/// Units 测试已钉住；此处断言语义化取值）。
core::UnitToken unitOrThrow(const char* symbol)
{
    auto token = core::UnitToken::find(symbol);
    if (!token.has_value()) {
        // fail-fast：core 注册表缺陷属环境错误——显性失败不吞错。
        throw TrajectoryError("trajectory/continuity/unit-registry",
                              std::string("单位注册表缺少冻结项 ") + symbol
                                  + "（core Units 缺陷——环境故障）");
    }
    return *token;
}

/// 比较型素材构造（实际差/容差/单位——UX-03 三要素；C4 逐元素口径）。
core::ComparativeFields deltaComparison(double actualDelta, double tolerance,
                                        core::UnitToken unit)
{
    core::ComparativeFields cmp;
    // 来源标记：DerivedReadOnly——边界差是本检查对段几何的派生对照量。
    const auto provenance = core::ValueProvenance::make(
        core::ProvenanceKind::DerivedReadOnly, {}, {}, "continuity-boundary");
    cmp.actual.quantity = core::SourcedValue<double>::provided(actualDelta, provenance);
    cmp.actual.unit = std::move(unit);
    cmp.expected.quantity = core::SourcedValue<double>::provided(tolerance, provenance);
    cmp.expected.unit = cmp.actual.unit;
    return cmp;
}

}  // namespace

// =====================================================================
// C2 适用性双路（§13.3）
// =====================================================================

CartesianIkContinuityApplicability cartesianIkContinuityApplicability(
    const std::vector<TrajectorySegment>& segments)
{
    // 判定规则（§13.3 原文口径）：任一段为 CartesianLine → 适用；否则
    // （含空序列）→ 显式不适用（不计缺失——C2）。
    for (const TrajectorySegment& seg : segments) {
        if (seg.spaceType == SegmentSpaceType::CartesianLine) {
            CartesianIkContinuityApplicability out;
            out.applicable = true;
            out.notApplicableReason.clear();
            return out;
        }
    }
    CartesianIkContinuityApplicability out;
    out.applicable = false;
    out.notApplicableReason =
        "路径不含笛卡尔段（纯关节路径——表 4 C2 例，显式标记不适用，"
        "不计缺失）";
    return out;
}

// =====================================================================
// 段边界几何连续检查（§9.1/§15.3）
// =====================================================================

ContinuityReport checkGeometricContinuity(
    const std::vector<TrajectorySegment>& segments,
    const GeometricContinuityTolerances& tolerances)
{
    // ---- 第 1 步：前置校验（§15.3 前置原文；fail-fast）----
    // 容差合法域（有限且 >0——"容差来源已解析"的合法域面；调用方可传
    // 更严值但不得为非正/非有限）。
    if (!isFiniteDouble(tolerances.positionAbsM) || tolerances.positionAbsM <= 0.0) {
        throw TrajectoryError("trajectory/continuity/pos-tol",
                              "位置连续容差须有限且 >0（m），实际 "
                                  + std::to_string(tolerances.positionAbsM));
    }
    if (!isFiniteDouble(tolerances.orientationAbsRad)
        || tolerances.orientationAbsRad <= 0.0) {
        throw TrajectoryError("trajectory/continuity/ori-tol",
                              "姿态连续容差须有限且 >0（rad），实际 "
                                  + std::to_string(tolerances.orientationAbsRad));
    }
    for (std::size_t i = 0; i < segments.size(); ++i) {
        // 段序号 0 基连续（§15.3 前置——定位键单调，诊断/复检共用）。
        if (segments[i].segmentIndex != i) {
            throw TrajectoryError("trajectory/continuity/segment-index",
                                  "段序号须 0 基连续（位 " + std::to_string(i)
                                      + " 实际 segmentIndex="
                                      + std::to_string(segments[i].segmentIndex)
                                      + "）");
        }
        // 段结构不变量守卫（§6.2 双域纪律/路点数量——validate 复用）。
        validateTrajectorySegment(segments[i]);
    }

    // ---- 第 2 步：逐边界判定（0/1 段无边界——空报告，非错误）----
    ContinuityReport report;
    if (segments.size() < 2U) {
        return report;
    }

    // 比较容差（C4 公式：ε_rel=0 → 退化为纯 ε_abs——C7 运行校验口径）。
    const core::Tolerance posTol = core::Tolerance::make(0.0, tolerances.positionAbsM);
    const core::Tolerance oriTol = core::Tolerance::make(0.0, tolerances.orientationAbsRad);
    const core::UnitToken unitM = unitOrThrow("m");
    const core::UnitToken unitRad = unitOrThrow("rad");

    for (std::size_t b = 0; b + 1U < segments.size(); ++b) {
        const TrajectorySegment& before = segments[b];
        const TrajectorySegment& after = segments[b + 1U];
        // 段至少 2 路点（validate 已保证）——边界两端＝前段末/后段首。
        const Waypoint& wpBefore = before.waypoints.back();
        const Waypoint& wpAfter = after.waypoints.front();

        BoundaryContinuityEntry entry;
        entry.boundaryIndex = static_cast<std::uint32_t>(b);
        entry.beforeSegmentIndex = before.segmentIndex;
        entry.afterSegmentIndex = after.segmentIndex;

        const bool bothCartesian = wpBefore.target.has_value()
                                && wpAfter.target.has_value();
        entry.cartesianApplicable = bothCartesian;
        if (!bothCartesian) {
            // 任一侧无 target（纯关节边界）→ 笛卡尔维度 NotApplicable
            // （显式标记——非通过非缺失；§9.2 行 6 同口径。JointLinear
            // 路点的 TCP 位姿是 FK 派生观察，本检查不消费 FK 端口）。
            report.boundaries.push_back(std::move(entry));
            continue;
        }

        // ---- 位置维：逐分量差（m）经 closeWithin（附录 D C4——逐元素
        // 防正负抵消）；实测差取最大逐分量绝对差（素材"实际值"口径）。
        const rw::math::Vector3D<double>& pa = wpBefore.target->P();
        const rw::math::Vector3D<double>& pb = wpAfter.target->P();
        double maxPosDelta = 0.0;
        bool positionOk = true;
        for (std::size_t k = 0; k < 3; ++k) {
            // 逐分量判定：参考值取连接目标侧（后段边界 pb[k]）——C4 公式
            // 以 ref 承载相对项（ε_rel=0 时公式退化为 |a−b| ≤ ε_abs）。
            if (!core::closeWithin(pa[k], pb[k], posTol)) {
                positionOk = false;
            }
            maxPosDelta = std::max(maxPosDelta, std::fabs(pa[k] - pb[k]));
        }
        entry.positionOk = positionOk;
        entry.positionDeltaM = maxPosDelta;

        // ---- 姿态维：相对旋转最短弧角（rad）对标量容差判定（ref=0
        // 退化为 ε_abs——附录 D C7 角度量纲）。
        const double oriDelta = rotationAngleBetween(wpBefore.target->R(),
                                                     wpAfter.target->R());
        const bool orientationOk = core::closeWithin(oriDelta, 0.0, oriTol);
        entry.orientationOk = orientationOk;
        entry.orientationDeltaRad = oriDelta;

        // ---- 违规素材（§9.2：段序号、维度、实际差/容差比较型；逐维度
        // 一条——缺失清单全量列出口径同源，不因首个违规短路）。
        if (!positionOk) {
            FailedSegmentRecord rec;
            rec.segmentIndex = after.segmentIndex;
            rec.phaseToken = kPhasePlanLine;
            rec.reasonToken = std::string(kTrjContinuityBroken);
            rec.pathParameter = 0.0;  // 连接点＝后段首端（s=0 定位）
            rec.cause = "段边界位置连续破坏：边界 " + std::to_string(b)
                      + "（段 " + std::to_string(before.segmentIndex) + "→段 "
                      + std::to_string(after.segmentIndex) + "）两端 TCP 位置"
                      "最大逐分量差 " + std::to_string(maxPosDelta)
                      + " m 超过容差 " + std::to_string(tolerances.positionAbsM)
                      + " m（附录 D C7 运行校验量纲）";
            rec.recommendedAction =
                "检查段构造的连接点生成（approach/retract 起点解析与任务点"
                "位姿一致性）——连续性属段构造保证的守卫面（§9.1）";
            rec.comparison = deltaComparison(maxPosDelta,
                                             tolerances.positionAbsM, unitM);
            report.violations.push_back(std::move(rec));
        }
        if (!orientationOk) {
            FailedSegmentRecord rec;
            rec.segmentIndex = after.segmentIndex;
            rec.phaseToken = kPhasePlanLine;
            rec.reasonToken = std::string(kTrjContinuityBroken);
            rec.pathParameter = 0.0;
            rec.cause = "段边界姿态连续破坏：边界 " + std::to_string(b)
                      + "（段 " + std::to_string(before.segmentIndex) + "→段 "
                      + std::to_string(after.segmentIndex) + "）两端 TCP 姿态"
                      "最短弧角 " + std::to_string(oriDelta)
                      + " rad 超过容差 " + std::to_string(tolerances.orientationAbsRad)
                      + " rad（附录 D C7 运行校验量纲）";
            rec.recommendedAction =
                "检查段构造的姿态解析（OrientationRule 解析值随快照冻结"
                "——§5.2）与连接点姿态一致性";
            rec.comparison = deltaComparison(oriDelta,
                                             tolerances.orientationAbsRad, unitRad);
            report.violations.push_back(std::move(rec));
        }

        report.boundaries.push_back(std::move(entry));
    }
    return report;
}

}  // namespace sdurws::ird::trajectory
