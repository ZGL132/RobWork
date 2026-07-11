#include "KinematicMetrics.hpp"

// 这里只依赖 Eigen,只用到了 Dense + SVD 两个模块。
// Eigen::JacobiSVD 对小矩阵足够快、对病态矩阵数值稳定,适合离线分析场景
// (实时控制不会在 UI 线程里反复跑 SVD)。
#include <Eigen/Dense>
#include <Eigen/SVD>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

// =============================================================================
//  calculateJointLimitMargins
// =============================================================================
//
// 目标:为每个关节 i 计算一个归一化的"距限位裕度",取值在 [0, 0.5]:
//   - 0   → 关节贴在 lo 或 hi 上(已经在物理限位);
//   - 0.5 → 关节恰好在 (lo+hi)/2,离两端距离最大;
//   - > 0.5 → 仅当 lo > hi 时才会出现(配置错误,实际不会出现)。
//
// 公式:
//   margin[i] = min(q_i - lo_i, hi_i - q_i) / max(hi_i - lo_i, eps)
//
// 关键设计点:
//   1) 用"距离两端较小者"而不是"距离某一端的相对值",这样无论关节离 lo
//      还是离 hi 更近,数值都连续单调;
//   2) 分母用 max(span, eps) 而不是直接用 span,防止关节被锁死
//      (lo_i == hi_i) 时除零;
//   3) 维度不匹配时返回空 vector 而非抛异常,调用方(analyzeIk 等)
//      自己根据 margins.empty() 走降级路径。
//
// 该函数被 analyzeCurrentPose / analyzeIk / makeWorkspaceSample 三处复用,
// 所以实现尽量简单,避免引入额外的 q 拷贝。
std::vector< double > rws::calculateJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    std::vector< double > margins;
    const std::size_t n = q.size ();
    // 维度校验:任何一侧长度对不上都直接返回空,而不是截断或抛异常。
    if (bounds.first.size () != n || bounds.second.size () != n)
        return margins;
    margins.reserve (n);
    // epsilon 兜底:double 精度上限,等价于"几乎为 0"。用 numeric_limits
    // 拿到机器 epsilon 而不是写死 1e-12,跨平台数值更稳定。
    const double eps = std::numeric_limits< double >::epsilon ();
    for (std::size_t i = 0; i < n; ++i) {
        const double lo = bounds.first (i);
        const double hi = bounds.second (i);
        // span:关节的总活动范围。用 max 而不是 abs 是因为:
        //   - 如果 hi > lo(正常情况),span = hi - lo;
        //   - 如果 hi == lo(关节被锁死),span = eps,避免 0/0 = NaN;
        //   - 如果 hi < lo(配置错误,限位反了),span 仍可能为负数,
        //     但 max(负数, eps) 也会强制返回 eps,fallback 到常数裕度。
        const double span = std::max (hi - lo, eps);
        // d_lo / d_hi:到下端、上端的距离。如果 q 已经出限,这两者之一会是负数。
        const double d_lo = q (i) - lo;
        const double d_hi = hi - q (i);
        // d 取较小者,正好对应"更靠近的一端"。margin 会随 q 移动而
        // 单调变化,在跨过中点时达到峰值 0.5。
        const double d = std::min (d_lo, d_hi);
        margins.push_back (d / span);
    }
    return margins;
}

// 取所有关节裕度的最小值,表征"整个机械臂最危险的关节"。
// 空 vector 时返回 0.0 而不是 NaN,调用方可以无脑 .min() 而不必判空;
// 这是测试 testMetrics 中"q=0.2 → margin=0.02"用例的依据。
double rws::minimumJointLimitMargin (const std::vector< double >& margins)
{
    if (margins.empty ())
        return 0.0;
    return *std::min_element (margins.begin (), margins.end ());
}

// =============================================================================
//  classifyJointLimitMargins
// =============================================================================
//
// 综合两步判断:
//   (a) 硬超限(q 落在 lo/hi 之外):立即判定 Fail;
//   (b) 软警告(最小裕度 < nearJointLimitRatio,但未超限):判定 Warning。
//
// 两者是 OR 的关系:超限了就不再判定 Warning(避免双重告警)。
// 这与 README 里 "Pass and Warning count as reachable" 的策略一致。
//
// 告警设计:
//   - "KIN_JOINT_LIMIT" + Fail   :硬错误,需立即关注;
//   - "KIN_NEAR_JOINT_LIMIT" + Warning:接近限位,设计提醒;
//   - warnings 指针允许为空,调用方如果不需要逐条告警(如采样场景)
//     可传入 nullptr,本函数仅在 nullptr 时跳过 push_back。
AnalysisStatus rws::classifyJointLimitMargins (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    const KinematicThresholds& thresholds,
    std::vector< AnalysisWarning >* warnings)
{
    // 默认 Pass;遇到任何降级条件再翻成 Warning / Fail。
    AnalysisStatus overall = AnalysisStatus::Pass;
    bool inLimit   = false;   // 任一关节硬超限?
    bool nearLimit = false;   // 任一关节接近限位?
    const std::size_t n = q.size ();
    // 第一步:硬超限检查。bounds 维度对得上才走这个分支。
    if (bounds.first.size () == n && bounds.second.size () == n) {
        for (std::size_t i = 0; i < n; ++i) {
            const double lo = bounds.first (i);
            const double hi = bounds.second (i);
            // q(i) < lo 或 q(i) > hi 都算越界。注意:浮点比较直接用 < / >
            // 即可,因为 hi/lo 通常是 WorkCell XML 中明确的有限数,不会出现 NaN。
            if (q (i) < lo || q (i) > hi) {
                inLimit = true;
                if (warnings != nullptr) {
                    AnalysisWarning w;
                    w.code     = "KIN_JOINT_LIMIT";
                    w.message  = "Joint " + std::to_string (i) + " is outside its limits.";
                    w.source   = "KinematicAnalyzer";
                    w.severity = AnalysisStatus::Fail;
                    warnings->push_back (w);
                }
            }
        }
    }

    // 第二步:裕度软警告。复用 calculateJointLimitMargins 而非重复代码。
    const std::vector< double > margins = calculateJointLimitMargins (q, bounds);
    if (!margins.empty ()) {
        const double minMargin = minimumJointLimitMargin (margins);
        // 注意:仅当 inLimit==false 时才告警"接近限位",否则越界已经覆盖了这种情况。
        if (!inLimit && minMargin < thresholds.nearJointLimitRatio) {
            nearLimit = true;
            if (warnings != nullptr) {
                AnalysisWarning w;
                w.code     = "KIN_NEAR_JOINT_LIMIT";
                w.message  = "Minimum joint-limit margin (" +
                             std::to_string (minMargin) + ") is below threshold " +
                             std::to_string (thresholds.nearJointLimitRatio) + ".";
                w.source   = "KinematicAnalyzer";
                w.severity = AnalysisStatus::Warning;
                warnings->push_back (w);
            }
        }
    }

    // 状态合成:硬超限优先,其次软警告,最后 Pass。
    if (inLimit)
        overall = AnalysisStatus::Fail;
    else if (nearLimit)
        overall = AnalysisStatus::Warning;
    else
        overall = AnalysisStatus::Pass;
    return overall;
}

AnalysisStatus rws::classifyTargetResidual (
    double positionErrorMeters,
    double orientationErrorDeg,
    double positionToleranceMeters,
    double orientationToleranceDeg,
    std::vector< KinematicFailureReason >* failureReasons,
    std::vector< AnalysisWarning >* warnings)
{
    const bool invalidInput =
        !std::isfinite (positionErrorMeters) || !std::isfinite (orientationErrorDeg) ||
        !std::isfinite (positionToleranceMeters) || !std::isfinite (orientationToleranceDeg) ||
        positionToleranceMeters < 0.0 || orientationToleranceDeg < 0.0;
    const bool positionFailed =
        invalidInput || positionErrorMeters > positionToleranceMeters;
    const bool orientationFailed =
        invalidInput || orientationErrorDeg > orientationToleranceDeg;
    if (!positionFailed && !orientationFailed)
        return AnalysisStatus::Pass;

    if (failureReasons != nullptr)
        failureReasons->push_back (KinematicFailureReason::TargetResidual);
    if (warnings != nullptr) {
        AnalysisWarning warning;
        warning.code = "KIN_TARGET_RESIDUAL";
        warning.message =
            "IK solution residual exceeds tolerance: position=" +
            std::to_string (positionErrorMeters) + " m (limit " +
            std::to_string (positionToleranceMeters) + "), orientation=" +
            std::to_string (orientationErrorDeg) + " deg (limit " +
            std::to_string (orientationToleranceDeg) + ").";
        warning.source = "KinematicAnalyzer";
        warning.severity = AnalysisStatus::Fail;
        warnings->push_back (warning);
    }
    return AnalysisStatus::Fail;
}

// =============================================================================
//  calculateSingularMetrics
// =============================================================================
//
// 用 JacobiSVD 分解 6×n 雅可比矩阵 J ∈ R^{6×n},得到:
//   J = U · diag(σ_1, ..., σ_r) · V^T,  σ_1 ≥ σ_2 ≥ ... ≥ σ_r ≥ 0
//
// 工程意义:
//   - σ_max = 主轴最大速度增益(力 → 末端速度的最强方向);
//   - σ_min = 最弱方向,σ_min → 0 即接近奇异(丢失一个自由度);
//   - 条件数 κ = σ_max / σ_min 衡量"各向同性",κ=1 最理想;
//   - 可操作度 Yoshikawa = ∏ σ_i,数值上等价于 |det(J·J^T)|^{1/2},
//     反映机器人整体的"形状灵活度"。
//
// 阈值默认:
//   - σ_min < 1e-12 → κ 记为 +inf(数值上无法表示);
//   - κ ≥ 1000 或 σ_min 过小 → Fail(KIN_SINGULAR);
//   - κ ≥ 100 或 σ_min < 1e-4 或 可操作度 < 1e-5 → Warning(KIN_NEAR_SINGULAR);
//   - 否则 Pass。
SingularMetrics rws::calculateSingularMetrics (
    const rw::math::Jacobian& jacobian,
    const KinematicThresholds& thresholds)
{
    SingularMetrics result;
    // jacobian.e() 返回 Eigen::MatrixXd 引用;空矩阵直接返回默认结果
    // (status=Unknown,所有数值 0),由调用方走降级分支。
    const Eigen::MatrixXd m = jacobian.e ();
    if (m.rows () == 0 || m.cols () == 0)
        return result;

    // JacobiSVD 默认 ComputeThin:对 6×n 当 n>6 时只计算前 6 列,
    // 与 SVD 数学定义一致,奇异值数量等于 min(rows, cols)。
    Eigen::JacobiSVD< Eigen::MatrixXd > svd (m);
    const Eigen::VectorXd sigma = svd.singularValues ();
    const std::size_t n = static_cast< std::size_t > (sigma.size ());
    result.singularValues.assign (n, 0.0);
    // manipulability 用累乘(初始为 1,空集时保持 1)。
    // 累乘本身数值上容易溢出/下溢,但对 UI 显示够用;真正严谨的场景
    // 应该用 log-sum-exp 或者直接用 Yoshikawa 解析定义 J·J^T 的特征值积。
    result.manipulability = 1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = sigma (static_cast< Eigen::Index > (i));
        result.singularValues[i] = s;
        result.manipulability *= s;
    }

    if (n == 0)
        return result;

    // SVD 返回的奇异值在 Eigen 里是降序的,front() 是 σ_max,back() 是 σ_min。
    const double sigmaMax = result.singularValues.front ();
    const double sigmaMin = result.singularValues.back ();
    // σ_min < 1e-12 时记为 +inf:避免 σ_max / σ_min 数值上溢 / 触发 IEEE
    // 的 inf 警告;同时告诉上层"这其实没法算条件数"。
    if (sigmaMin < 1e-12) {
        result.conditionNumber = std::numeric_limits< double >::infinity ();
    }
    else {
        result.conditionNumber = sigmaMax / sigmaMin;
    }

    // 触发条件:用 infCondition 单独保留一个布尔变量,避免多次 std::isinf。
    const bool infCondition  = std::isinf (result.conditionNumber);
    // Fail 优先级最高:inf 或者 κ ≥ conditionFail。
    const bool failCondition =
        infCondition || result.conditionNumber >= thresholds.conditionFail;
    // Warning 次之:κ ≥ conditionWarning、或 σ_min 过小、或可操作度过低。
    // 注意:即使 failCondition=true,warnCondition 也可能为 true,但下方
    // if/else if 的链路会让 Fail 优先覆盖 Warning(只发 KIN_SINGULAR)。
    const bool warnCondition =
        result.conditionNumber >= thresholds.conditionWarning ||
        sigmaMin < thresholds.singularValueWarning ||
        result.manipulability < thresholds.manipulabilityWarning;
    if (failCondition)
        result.status = AnalysisStatus::Fail;
    else if (warnCondition)
        result.status = AnalysisStatus::Warning;
    else
        result.status = AnalysisStatus::Pass;

    // 告警:在已经确定 Fail / Warning 之后,再追加一条带具体数值的告警。
    // 这样 result.status 和 result.warnings 总是一致的,不会出现 status=Pass
    // 但带 Fail 告警的反直觉情况。
    if (failCondition) {
        AnalysisWarning w;
        w.code     = "KIN_SINGULAR";
        w.message  = "Configuration is near singular (condition " +
                     std::to_string (result.conditionNumber) + ").";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
    }
    else if (warnCondition) {
        AnalysisWarning w;
        w.code     = "KIN_NEAR_SINGULAR";
        w.message  = "Configuration has poor conditioning (condition " +
                     std::to_string (result.conditionNumber) + ").";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Warning;
        result.warnings.push_back (w);
    }
    return result;
}
