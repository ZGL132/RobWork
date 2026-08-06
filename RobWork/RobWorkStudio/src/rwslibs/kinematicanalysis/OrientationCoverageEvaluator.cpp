// =============================================================================
//  OrientationCoverageEvaluator.cpp —— 姿态覆盖评估实现
// =============================================================================
//
// 实现三部分功能:
//   1. generateOrientationTargetSamples:基于斐波那契螺旋均匀采样单位球方向,
//      再与绕工具 Z 轴的滚动采样做笛卡尔积,得到一组离散姿态目标;
//   2. isDirectionTargetReachable:宽松判定 —— 只要求工具 Z 轴对齐到目标方向;
//   3. isOrientationTargetReachable:严格判定 —— 要求完整姿态(含滚动)对齐。
//
// 两类判定都只消费已完成的 TargetEvaluation 候选解,不再重复求解 IK,
// 因此适合被区域覆盖率评估在大量网格单元上高频复用。
#include "OrientationCoverageEvaluator.hpp"

#include "KinematicAnalysisPoseReachability.hpp"

#include <rw/math/EAA.hpp>
#include <rw/math/Math.hpp>
#include <rw/math/Vector3D.hpp>

#include <algorithm>
#include <cmath>

namespace {

// finiteNonCollidingCandidate:过滤"可用于覆盖判定的候选解"。
// 只有同时满足"配置可行、无碰撞、位置残差在容差内"的候选才能代表一次
// 真正可达的姿态 —— 否则说明该姿态可能撞上障碍或位置漂移,不应计入覆盖。
bool finiteNonCollidingCandidate(const rws::TargetCandidate& candidate,
                                 double positionToleranceMeters)
{
    return candidate.configuration.feasibility == rws::Feasibility::Feasible &&
           !candidate.configuration.inCollision &&
           candidate.positionErrorMeters <= positionToleranceMeters;
}

}

using namespace rws;

namespace {

// sampleUnitDirections:在单位球面上均匀采样 count 个单位向量。
// 采用"斐波那契螺旋"分布(黄金角旋转):相比经纬度网格,它在球面上分布更均匀,
// 且不会在南北两极聚集,适合作为工具 Z 轴方向的离散采样。
std::vector< rw::math::Vector3D<> > sampleUnitDirections (int count)
{
    std::vector< rw::math::Vector3D<> > directions;
    if (count <= 0)
        return directions;
    directions.reserve (static_cast< std::size_t > (count));
    const double goldenAngle = rw::math::Pi * (3.0 - std::sqrt (5.0));
    for (int index = 0; index < count; ++index) {
        const double z = 1.0 - 2.0 * (static_cast< double > (index) + 0.5) /
                                  static_cast< double > (count);
        const double radius = std::sqrt (std::max (0.0, 1.0 - z * z));
        const double theta = goldenAngle * static_cast< double > (index);
        directions.push_back (rw::math::Vector3D<> (
            radius * std::cos (theta), radius * std::sin (theta), z));
    }
    return directions;
}

// toolZDirectionToRotation:由"工具 Z 轴方向"构造一个完整的目标旋转矩阵。
// 做法:取与 Z 轴不平行的参考轴叉乘得到正交基(x, y, z),再把绕 z 轴旋转
// 一个滚动角(rollIndex/rollSamples 均匀分布)叠加其上,从而覆盖自转自由度。
// 注意:方向与自身平行时不会出现(0 向量已回退为 +Z),这是构造正交基的关键。
rw::math::Rotation3D<> toolZDirectionToRotation (
    const rw::math::Vector3D<>& rawDirection, int rollIndex, int rollSamples)
{
    using rw::math::Vector3D;
    rw::math::Vector3D<> z = rawDirection;
    if (z.norm2 () < 1e-12)
        z = Vector3D<>::z ();
    z = normalize (z);

    const rw::math::Vector3D<> reference =
        std::fabs (z (2)) < 0.9 ? Vector3D<>::z () : Vector3D<>::y ();
    rw::math::Vector3D<> x = cross (reference, z);
    if (x.norm2 () < 1e-12)
        x = Vector3D<>::x ();
    x = normalize (x);
    const rw::math::Vector3D<> y = normalize (cross (z, x));
    const rw::math::Rotation3D<> base (x, y, z);

    const int rolls = std::max (1, rollSamples);
    const double roll = 2.0 * rw::math::Pi * static_cast< double > (rollIndex) /
                        static_cast< double > (rolls);
    return base * rw::math::EAA<> (Vector3D<>::z (), roll).toRotation3D ();
}

}    // namespace

// -----------------------------------------------------------------------------
// generateOrientationTargetSamples —— 生成离散姿态目标集
// -----------------------------------------------------------------------------
//
// 对每个方向与每个滚动组合生成一个 OrientationTargetSample;
// 采样数为 directionSamples × rollSamples,顺序按"方向外层、滚动内层"排列,
// 便于调用方按 directionIndex 分批处理。
std::vector< OrientationTargetSample > rws::generateOrientationTargetSamples (
    const PoseReachabilityConfig& config)
{
    const PoseReachabilityConfig sanitized =
        sanitizePoseReachabilityConfig (config, nullptr);
    if (sanitized.directionSamples <= 0)
        return std::vector< OrientationTargetSample > ();

    const std::vector< rw::math::Vector3D<> > directions =
        sampleUnitDirections (sanitized.directionSamples);
    std::vector< OrientationTargetSample > samples;
    samples.reserve (directions.size () *
                     static_cast< std::size_t > (sanitized.rollSamples));
    for (int directionIndex = 0; directionIndex < sanitized.directionSamples;
         ++directionIndex) {
        for (int rollIndex = 0; rollIndex < sanitized.rollSamples; ++rollIndex) {
            OrientationTargetSample sample;
            sample.directionIndex = directionIndex;
            sample.rollIndex = rollIndex;
            sample.rotation = toolZDirectionToRotation (
                directions[static_cast< std::size_t > (directionIndex)],
                rollIndex, sanitized.rollSamples);
            samples.push_back (sample);
        }
    }
    return samples;
}

// -----------------------------------------------------------------------------
// isDirectionTargetReachable —— 方向可达性判定(宽松)
// -----------------------------------------------------------------------------
//
// 语义:只要存在一个合格候选解,其 TCP 的 Z 轴与目标方向的夹角 <= 容差,
// 就判定该方向可达。滚动自由度不参与比较,因此这是"指向性"覆盖;
// 求夹角时用点积夹到 [-1,1] 再 acos,规避浮点误差导致的 acos 域外。
bool rws::isDirectionTargetReachable(
    const TargetEvaluation& evaluation,
    const rw::math::Rotation3D<>& targetRotation,
    double positionToleranceMeters,
    double toolAxisToleranceDeg)
{
    if (!std::isfinite (positionToleranceMeters) || positionToleranceMeters < 0.0 ||
        !std::isfinite (toolAxisToleranceDeg) || toolAxisToleranceDeg < 0.0)
        return false;
    const rw::math::Vector3D<> targetZ = targetRotation.getCol (2);
    for (const TargetCandidate& candidate : evaluation.candidates) {
        if (!finiteNonCollidingCandidate (candidate, positionToleranceMeters))
            continue;
        const rw::math::Vector3D<> actualZ = candidate.configuration.tcpPose.R ().getCol (2);
        const double cosine = std::max (-1.0, std::min (1.0, dot (targetZ, actualZ)));
        const double axisErrorDeg = std::acos (cosine) * 180.0 / rw::math::Pi;
        if (axisErrorDeg <= toolAxisToleranceDeg)
            return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// isOrientationTargetReachable —— 完整姿态可达性判定(严格)
// -----------------------------------------------------------------------------
//
// 语义:要求存在一个合格候选解,其 FK 姿态残差(orientationErrorDeg,已含滚动
// 自由度)<= 容差。该判定用于"该位置能否摆出这个完整姿态"的严格覆盖统计。
bool rws::isOrientationTargetReachable(const TargetEvaluation& evaluation,
                                       double positionToleranceMeters,
                                       double orientationToleranceDeg)
{
    if (!std::isfinite (positionToleranceMeters) || positionToleranceMeters < 0.0 ||
        !std::isfinite (orientationToleranceDeg) || orientationToleranceDeg < 0.0)
        return false;
    for (const TargetCandidate& candidate : evaluation.candidates) {
        if (finiteNonCollidingCandidate (candidate, positionToleranceMeters) &&
            candidate.orientationErrorDeg <= orientationToleranceDeg)
            return true;
    }
    return false;
}
