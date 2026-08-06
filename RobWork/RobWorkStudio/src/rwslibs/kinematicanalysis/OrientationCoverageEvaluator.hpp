// =============================================================================
//  OrientationCoverageEvaluator.hpp —— 姿态覆盖评估(声明)
// =============================================================================
//
// 本组件评估"机械臂在某个空间位置能否覆盖给定的工具朝向 / 完整姿态"。
// 它独立于具体区域扫描,直接回答两类判定问题:
//   - 方向可达性:工具的 Z 轴(工具主方向)能否对齐到指定方向(允许绕轴滚动);
//   - 完整姿态可达性:是否存在一个无碰撞、可行且残差在容差内的候选解,
//     使 TCP 的完整姿态(含滚动)与目标姿态足够接近。
//
// 依赖:判定基于已完成的 TargetEvaluation(由 TargetEvaluator 产出)的候选解
// 集合,而不是自行再做一遍 IK,因此本组件是"纯判定"层,计算代价极低。
// 生成离散方向样本的任务由 generateOrientationTargetSamples 承担,供区域
// 覆盖率评估复用同一套均匀采样策略。
#ifndef RWS_KINEMATICANALYSIS_ORIENTATIONCOVERAGEEVALUATOR_HPP
#define RWS_KINEMATICANALYSIS_ORIENTATIONCOVERAGEEVALUATOR_HPP

#include "KinematicAnalysisTypes.hpp"

#include <rw/math/Rotation3D.hpp>

#include <vector>

namespace rws {

// =============================================================================
//  OrientationTargetSample —— 单个离散姿态采样
// =============================================================================
//
// 描述一次"目标姿态"采样:由工具 Z 轴方向索引 + 绕 Z 轴滚动索引唯一确定。
// directionIndex / rollIndex 用于回溯采样网格(便于与覆盖率统计对齐);
// rotation 是该采样对应的完整目标旋转矩阵,可直接作为 IK 目标使用。
struct OrientationTargetSample
{
    // 单位球面上的方向索引(见 sampleUnitDirections 的斐波那契螺旋编号)。
    int directionIndex = -1;
    // 绕工具 Z 轴的滚动索引;未参与时保持 -1。
    int rollIndex = -1;
    // 由方向 + 滚动合成的完整目标旋转矩阵。
    rw::math::Rotation3D<> rotation;
};

// 根据位姿可达性配置生成一组合适的目标姿态采样:
// directionSamples 个方向 × rollSamples 个滚动的笛卡尔积。
// 输入先经 sanitizePoseReachabilityConfig 规整(夹紧到合法范围)。
std::vector< OrientationTargetSample > generateOrientationTargetSamples (
    const PoseReachabilityConfig& config);

// 判定:是否存在一个可行 / 无碰撞 / 位置残差可接受的候选解,其工具 Z 轴与
// targetRotation 的 Z 轴夹角不超过 toolAxisToleranceDeg(不要求滚动对齐)。
// 注意:用于"方向可达性"宽松判定,不校验完整姿态。
bool isDirectionTargetReachable(const TargetEvaluation& evaluation,
                                const rw::math::Rotation3D<>& targetRotation,
                                double positionToleranceMeters,
                                double toolAxisToleranceDeg);

// 判定:是否存在一个可行 / 无碰撞 / 位置残差可接受的候选解,其完整姿态
// (FK 残差 orientationErrorDeg)在 orientationToleranceDeg 之内。
// 用于"完整姿态可达性"严格判定。
bool isOrientationTargetReachable(const TargetEvaluation& evaluation,
                                  double positionToleranceMeters,
                                  double orientationToleranceDeg);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_ORIENTATIONCOVERAGEEVALUATOR_HPP
