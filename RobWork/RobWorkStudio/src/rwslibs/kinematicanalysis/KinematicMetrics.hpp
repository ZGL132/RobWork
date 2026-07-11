#ifndef RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP

// 引入结果类型,SingularMetrics 直接复用其中的 AnalysisStatus / AnalysisWarning。
#include "KinematicAnalysisTypes.hpp"

#include <rw/math/Q.hpp>
#include <rw/math/Jacobian.hpp>

#include <utility>
#include <vector>

namespace rws {

// 一个位形下的"雅可比质量"打包结果:
//  - singularValues: SVD 得到的奇异值(降序)
//  - conditionNumber: σ_max / σ_min(奇异时为 +inf)
//  - manipulability: ∏ σ_i(Yoshikawa 可操作度)
//  - status: 综合 Pass / Warning / Fail
//  - warnings: 当 Fail 时附带 KIN_SINGULAR、Warning 时附带 KIN_NEAR_SINGULAR
struct SingularMetrics
{
    std::vector< double > singularValues;
    double conditionNumber = 0.0;
    double manipulability  = 0.0;
    AnalysisStatus status  = AnalysisStatus::Unknown;
    std::vector< AnalysisWarning > warnings;
};

// 计算各关节的归一化"距限位裕度"。
// 定义为 min(q - lo, hi - q) / (hi - lo),即关节到两端限位的最小距离与总跨度的比值。
// q 与 bounds 维度不匹配时返回空 vector。
std::vector< double > calculateJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds);

// 返回 margins 的最小元素;空时返回 0.0。
double minimumJointLimitMargin (const std::vector< double >& margins);

// 综合判定关节裕度状态:超限 → Fail;未超但低于阈值 → Warning;否则 Pass。
// 若 warnings 非空,会按需追加 KIN_JOINT_LIMIT / KIN_NEAR_JOINT_LIMIT 两条告警。
AnalysisStatus classifyJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    const KinematicThresholds& thresholds,
    std::vector< AnalysisWarning >* warnings);

// 对一个 Jacobian 做 SVD,计算条件数 / 可操作度,并按 thresholds 给出状态。
SingularMetrics calculateSingularMetrics (
    const rw::math::Jacobian& jacobian,
    const KinematicThresholds& thresholds);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICMETRICS_HPP