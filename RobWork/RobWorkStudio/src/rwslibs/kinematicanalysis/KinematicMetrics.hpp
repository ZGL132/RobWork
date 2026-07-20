#ifndef RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP

// 引入结果类型,SingularMetrics 直接复用其中的 AnalysisStatus / AnalysisWarning。
#include "KinematicAnalysisTypes.hpp"

// RobWork 数学类型:Q 是关节值向量,Jacobian 是 6×n 雅可比矩阵。
#include <rw/math/Q.hpp>
#include <rw/math/Jacobian.hpp>

#include <utility>
#include <vector>

namespace rws {

// =============================================================================
//  SingularMetrics:一个位形下的"雅可比质量"打包结果
// =============================================================================
//
// 由 SVD 分解得到,描述当前位姿下的局部运动学质量。
//   - singularValues   :SVD 得到的奇异值序列(降序排列,σ_1 ≥ σ_2 ≥ ... ≥ σ_n);
//   - conditionNumber  :σ_max / σ_min;奇异时为 +inf(σ_min 极小或为 0);
//   - manipulability   :∏ σ_i(Yoshikawa 可操作度,反映该方向上的速度/力转换能力);
//   - status           :综合 Pass / Warning / Fail(基于条件数和可操作度阈值);
//   - warnings         :Fail 时附带 KIN_SINGULAR;Warning 时附带 KIN_NEAR_SINGULAR。
// 供 KinematicCurrentPoseResult / KinematicIkSolution 共同使用。
struct SingularMetrics
{
    std::vector< double > singularValues;
    double conditionNumber = 0.0;
    double manipulability  = 0.0;
    AnalysisStatus status  = AnalysisStatus::Unknown;
    std::vector< AnalysisWarning > warnings;
};

// =============================================================================
//  calculateJointLimitMargins:计算各关节的归一化"距限位裕度"
// =============================================================================
//
// 定义为 min(q - lo, hi - q) / (hi - lo),
// 即关节到两端限位的最小距离与总跨度的比值,范围 [0, 0.5]。
// 值越小说明关节越接近限位,值 0 表示已到达限位。
// 若 q 与 bounds 维度不匹配(异常状态),返回空 vector。
//
// @param q       当前关节值
// @param bounds  std::pair<lower, upper> 关节上下限(从 device->getBounds() 获取)
// @return 各关节裕度列表(维度与 q 一致)
std::vector< double > calculateJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds);

// 返回 margins 的最小元素;空时返回 0.0。
// 用途:报告"整个机械臂最紧迫的关节裕度"。
double minimumJointLimitMargin (const std::vector< double >& margins);

// =============================================================================
//  classifyJointLimitMargins:综合判定关节裕度状态
// =============================================================================
//
// 判定规则:
//   - 任何关节 ≤ 0.0(到达或超出限位)→ Fail;
//   - 任何关节 < thresholds.nearJointLimitRatio → Warning;
//   - 其余 → Pass。
//
// 若 warnings 非空,会按需追加 KIN_JOINT_LIMIT / KIN_NEAR_JOINT_LIMIT 两条告警。
// 注:告警以第一个越界关节的信息为主,不会逐关节都发出。
AnalysisStatus classifyJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    const KinematicThresholds& thresholds,
    std::vector< AnalysisWarning >* warnings);

// =============================================================================
//  classifyTargetResidual:用 FK 验算残差判定 IK 解是否满足目标容差
// =============================================================================
//
// 判定规则:
//   - 位置残差 > positionToleranceMeters → 失败,reason 追加 TargetResidual;
//   - 姿态残差 > orientationToleranceDeg → 失败,reason 追加 TargetResidual;
//   - 都满足 → Pass。
//
// 输入残差可来自 FK(q) 与目标位姿的对比,单位分别为米/度。
AnalysisStatus classifyTargetResidual (
    double positionErrorMeters,
    double orientationErrorDeg,
    double positionToleranceMeters,
    double orientationToleranceDeg,
    std::vector< KinematicFailureReason >* failureReasons,
    std::vector< AnalysisWarning >* warnings);

// =============================================================================
//  calculateSingularMetrics:对一个 Jacobian 做 SVD,计算条件数/可操作度
// =============================================================================
//
// 计算流程:
//   1) 对 Jacobian 做 SVD 得到奇异值序列;
//   2) σ_max / σ_min = conditionNumber(奇异时为 +inf);
//   3) ∏ σ_i = manipulability;
//   4) 按 thresholds 给出状态:
//      - 条件数 ≥ conditionFail   → Fail + KIN_SINGULAR;
//      - 条件数 ≥ conditionWarning → Warning + KIN_NEAR_SINGULAR;
//      - 可操作度 < manipulabilityWarning → Warning + KIN_NEAR_SINGULAR;
//      - 最小奇异值 < singularValueWarning → Warning + KIN_NEAR_SINGULAR;
//      - 其余 → Pass。
SingularMetrics calculateSingularMetrics (
    const rw::math::Jacobian& jacobian,
    const KinematicThresholds& thresholds);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP
