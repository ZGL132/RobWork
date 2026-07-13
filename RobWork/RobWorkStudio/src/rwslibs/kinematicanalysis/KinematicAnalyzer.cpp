#include "KinematicAnalyzer.hpp"

// 引入 IK 求解器和必要的运动学/数学工具。
#include <rw/core/Ptr.hpp>
#include <rw/invkin/JacobianIKSolver.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/Jacobian.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

using namespace rws;

// 默认阈值由 KinematicThresholds 自身的成员初始值给出。
KinematicAnalyzer::KinematicAnalyzer () : _thresholds () {}

namespace {

// =============================================================================
//  worstStatus — 状态半格聚合
// =============================================================================
// 把两个 AnalysisStatus 合并成一个"更糟"的状态,优先级
// Fail > Warning > Pass > Unknown。该半格是 total order(可任意两两合并),
// 没有"最小上界冲突"。两个 Unknown 合并仍为 Unknown。
// 主要用于 buildAggregateResult 把 currentPose / 任务点 / workspace / pose 四
// 类子结果合并为单一 status,以及 analyzeIk 把多解的状态折叠成一个。
AnalysisStatus worstStatus (AnalysisStatus lhs, AnalysisStatus rhs)
{
    if (lhs == AnalysisStatus::Fail || rhs == AnalysisStatus::Fail)
        return AnalysisStatus::Fail;
    if (lhs == AnalysisStatus::Warning || rhs == AnalysisStatus::Warning)
        return AnalysisStatus::Warning;
    if (lhs == AnalysisStatus::Pass || rhs == AnalysisStatus::Pass)
        return AnalysisStatus::Pass;
    return AnalysisStatus::Unknown;
}

// =============================================================================
//  taskPointToTransform — TaskPoint → Transform3D
// =============================================================================
// TaskPoint 在 UI/CSV 层用 "位置 (m) + RPY (度)" 描述,而 RobWork 内部使用
// Transform3D<Vector3D, RPY>(Vector3D, RPY in rad)。本函数只做单位换算与
// 封装:不解释、不做有效性检查(NaN/Inf 应在更上层拦截)。
//   - toRad = π/180,固定常量;直接 180.0 / rw::math::Pi 也可以,但用 toRad 更直观;
//   - RPY 顺序 RobWork 默认是 extrinsic XYZ,即绕固定坐标轴 X→Y→Z 依次旋转,
//     与 TaskPoint 的约定一致。
rw::math::Transform3D<> taskPointToTransform (const TaskPoint& target)
{
    const double toRad = rw::math::Pi / 180.0;
    return rw::math::Transform3D<> (
        rw::math::Vector3D<> (target.position[0], target.position[1], target.position[2]),
        rw::math::RPY<> (target.rpyDeg[0] * toRad,
                         target.rpyDeg[1] * toRad,
                         target.rpyDeg[2] * toRad));
}

// =============================================================================
//  qDistance — 关节空间 L2 距离
// =============================================================================
//   d = ||q_lhs - q_rhs||_2
// 用途:在 IK 评分中表示"当前解与当前 state 的距离",用作"路径长度偏好"。
// 维度不一致返回 +inf(而非 NaN 或抛异常),让上层评分始终保持有限数。
double qDistance (const rw::math::Q& lhs, const rw::math::Q& rhs)
{
    if (lhs.size () != rhs.size ())
        return std::numeric_limits< double >::infinity ();
    return (lhs - rhs).norm2 ();
}

// =============================================================================
//  positionError — 位置误差
// =============================================================================
//   e_pos = ||P_actual - P_target||_2  (单位:m)
// 直接用 Transform3D 的平移分量做差。RobWork 的 Vector3D::norm2() 即 L2 长度。
double positionError (const rw::math::Transform3D<>& actual,
                      const rw::math::Transform3D<>& target)
{
    return (actual.P () - target.P ()).norm2 ();
}

// =============================================================================
//  orientationErrorDeg — 姿态误差(度)
// =============================================================================
// 数学上:
//   R_diff = R_target^T · R_actual
//   e_ori  = |angle(EAA(R_diff))| · 180/π
// EAA(等效轴角)将旋转矩阵映射为 (axis, angle) 对,|angle| 就是从 R_target
// 转到 R_actual 所需的最短旋转角度。该公式等价于 ||log(R_target^T · R_actual)||。
// 注意:fabs 保证取正值(最短旋转方向),不会出现"绕远路到目标"。
double orientationErrorDeg (const rw::math::Transform3D<>& actual,
                            const rw::math::Transform3D<>& target)
{
    const rw::math::Rotation3D<> diff = inverse (target.R ()) * actual.R ();
    const rw::math::EAA<> eaa (diff);
    return std::fabs (eaa.angle ()) * 180.0 / rw::math::Pi;
}

// =============================================================================
//  qToVector — Q → std::vector<double>
// =============================================================================
// 用于把关节值传入不依赖 RobWork 的接口(QTableWidget / Qt::UserRole / JSON)
// 或共享给下游模块。预 reserve 一次,避免 push_back 时的多次扩容。
std::vector< double > qToVector (const rw::math::Q& q)
{
    std::vector< double > values;
    values.reserve (q.size ());
    for (std::size_t i = 0; i < q.size (); ++i)
        values.push_back (q (i));
    return values;
}

// =============================================================================
//  lexicographicQLess — 关节向量字典序
// =============================================================================
// 在 sortIkSolutionsForDisplay 中作为最后一道 tie-breaker:当所有数值
// 指标都相同时,按 q0→q1→... 的顺序给出稳定排序,这样 UI 的"同一份数据
// 重排后顺序一致"。
bool lexicographicQLess (const std::vector< double >& lhs, const std::vector< double >& rhs)
{
    return std::lexicographical_compare (lhs.begin (), lhs.end (), rhs.begin (), rhs.end ());
}

// =============================================================================
//  makeWarning — 告警构造器
// =============================================================================
// 简化告警构造:固定 source 字段为 "KinematicAnalyzer",这样下游无论
// 是 UI 列表、CSV、JSON 都能识别本插件产生的告警。
AnalysisWarning makeWarning (const std::string& code,
                             const std::string& message,
                             AnalysisStatus severity)
{
    AnalysisWarning w;
    w.code     = code;
    w.message  = message;
    w.source   = "KinematicAnalyzer";
    w.severity = severity;
    return w;
}

bool validateTaskPointTarget (const TaskPoint& target, std::string* error)
{
    for (double value : target.position) {
        if (!std::isfinite (value)) {
            if (error != nullptr)
                *error = "Target position contains a non-finite value.";
            return false;
        }
    }
    for (double value : target.rpyDeg) {
        if (!std::isfinite (value)) {
            if (error != nullptr)
                *error = "Target orientation contains a non-finite value.";
            return false;
        }
    }
    if (!std::isfinite (target.tolerance.positionMeters) ||
        target.tolerance.positionMeters < 0.0) {
        if (error != nullptr)
            *error = "Target position tolerance must be finite and non-negative.";
        return false;
    }
    if (!std::isfinite (target.tolerance.orientationDeg) ||
        target.tolerance.orientationDeg < 0.0) {
        if (error != nullptr)
            *error = "Target orientation tolerance must be finite and non-negative.";
        return false;
    }
    if (!std::isfinite (target.weight)) {
        if (error != nullptr)
            *error = "Target weight must be finite.";
        return false;
    }
    return true;
}

double effectiveTolerance (double taskTolerance, double defaultTolerance)
{
    return taskTolerance > 0.0 ? taskTolerance : defaultTolerance;
}

bool hasFailureReason (const KinematicIkSolution& solution, KinematicFailureReason reason)
{
    return std::find (solution.failureReasons.begin (), solution.failureReasons.end (), reason) !=
           solution.failureReasons.end ();
}

double qInfDistance (const rw::math::Q& lhs, const rw::math::Q& rhs)
{
    if (lhs.size () != rhs.size ())
        return std::numeric_limits< double >::infinity ();
    double distance = 0.0;
    for (std::size_t i = 0; i < lhs.size (); ++i)
        distance = std::max (distance, std::fabs (lhs (i) - rhs (i)));
    return distance;
}

double finiteBoundOrFallback (double bound, double fallback)
{
    return std::isfinite (bound) ? bound : fallback;
}

double interpolateBound (const rw::math::Q& lower,
                         const rw::math::Q& upper,
                         const rw::math::Q& current,
                         std::size_t index,
                         double fraction)
{
    double lo = finiteBoundOrFallback (lower (index), current (index) - rw::math::Pi);
    double hi = finiteBoundOrFallback (upper (index), current (index) + rw::math::Pi);
    if (!(hi > lo)) {
        lo = current (index) - rw::math::Pi;
        hi = current (index) + rw::math::Pi;
    }
    return lo + fraction * (hi - lo);
}

rw::math::Q clampedZeroSeed (const rw::math::Q& lower,
                             const rw::math::Q& upper,
                             const rw::math::Q& current)
{
    rw::math::Q q (current.size ());
    for (std::size_t i = 0; i < current.size (); ++i) {
        const double lo = finiteBoundOrFallback (lower (i), current (i) - rw::math::Pi);
        const double hi = finiteBoundOrFallback (upper (i), current (i) + rw::math::Pi);
        if (hi > lo)
            q (i) = std::min (hi, std::max (lo, 0.0));
        else
            q (i) = current (i);
    }
    return q;
}

std::vector< rw::math::Q > deterministicIkSeeds (
    const rw::math::Q& current,
    const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    std::vector< rw::math::Q > seeds;
    if (current.size () == 0)
        return seeds;

    const double seedProximity = 1e-6;
    rws::addUniqueIkCandidate (seeds, current, seedProximity);

    rw::math::Q center (current.size ());
    for (std::size_t i = 0; i < current.size (); ++i)
        center (i) = interpolateBound (bounds.first, bounds.second, current, i, 0.5);
    rws::addUniqueIkCandidate (seeds, center, seedProximity);
    rws::addUniqueIkCandidate (seeds, clampedZeroSeed (bounds.first, bounds.second, current),
                               seedProximity);

    const std::size_t dof = current.size ();
    const std::size_t exactMaskCount =
        dof < 16 ? (static_cast< std::size_t > (1) << dof) : 0;
    const std::size_t maskCount =
        exactMaskCount == 0 ? static_cast< std::size_t > (128) :
        std::min< std::size_t > (exactMaskCount, 128);
    for (std::size_t mask = 0; mask < maskCount; ++mask) {
        rw::math::Q seed (dof);
        for (std::size_t joint = 0; joint < dof; ++joint) {
            const bool highSide =
                joint < 8 * sizeof (std::size_t) ?
                ((mask & (static_cast< std::size_t > (1) << joint)) != 0) :
                (((mask + joint) % 2) != 0);
            seed (joint) = interpolateBound (
                bounds.first, bounds.second, current, joint, highSide ? 0.75 : 0.25);
        }
        rws::addUniqueIkCandidate (seeds, seed, seedProximity);
    }
    return seeds;
}

// =============================================================================
//  sampleUnitDirections — Fibonacci 球面采样
// =============================================================================
// 数学:对 i ∈ [0, count) 计算
//   z_i       = 1 − 2·(i + 0.5) / count    ∈ (-1, 1)   均匀分布
//   θ_i       = golden_angle · i            黄金角递增(≈ 137.508°)
//   r_i       = √(max(0, 1 − z_i²))         球面等 z 圆周半径
//   x_i, y_i  = r_i · cos/sin(θ_i)
//   d_i       = (x_i, y_i, z_i)             单位向量
//
// golden_angle = π·(3 − √5),由"向日葵种子排列"的极限角度决定。
// 相对经纬度网格,Fibonacci 螺旋避免了"两极聚集"现象:
//   - 经纬度法:赤道方向数 ≈ cos(θ)·count,极方向数趋近于 0;
//   - Fibonacci:任意环形带内方向数几乎一致。
//
// 这对"位姿可达性"至关重要:必须让所有方向都被同等采样,否则 coverage 指标
// 会被人为低估。
std::vector< rw::math::Vector3D<> > sampleUnitDirections (int count)
{
    std::vector< rw::math::Vector3D<> > directions;
    if (count <= 0)
        return directions;
    directions.reserve (static_cast< std::size_t > (count));
    const double goldenAngle = rw::math::Pi * (3.0 - std::sqrt (5.0));
    for (int i = 0; i < count; ++i) {
        // +0.5 让 z 落在两极中点而非极点上,防止 r=0 时 cos/sin 退化。
        const double z = 1.0 - 2.0 * (static_cast< double > (i) + 0.5) /
                                  static_cast< double > (count);
        // max(0, ·) 防止浮点误差下 z² 略大于 1 产生 NaN sqrt。
        const double radius = std::sqrt (std::max (0.0, 1.0 - z * z));
        const double theta  = goldenAngle * static_cast< double > (i);
        directions.push_back (rw::math::Vector3D<> (
            radius * std::cos (theta), radius * std::sin (theta), z));
    }
    return directions;
}

// =============================================================================
//  toolZDirectionToRotation — 给定 Z 方向 + roll,构造完整旋转矩阵
// =============================================================================
// 问题:IK 需要 baseTtcp = baseTposition · positionTtcp,而 positionTtcp 由
// 工具 Z 方向 + 绕 Z 的 roll 决定。
//
// 步骤:
//   1) 规范化 rawDirection → z;
//   2) 选一个不与 z 共线的参考向量 reference:
//        - 通常用世界 Z,这样 x 与世界 XY 平面平行;
//        - 但如果 z 接近 ±Z(|z(2)| ≥ 0.9),就换成世界 Y,避免 cross ≈ 0;
//   3) x = reference × z,归一化;这样 (x, y, z) 构成右手正交基;
//   4) base = Rotation3D(x, y, z) = [x | y | z] 作为列向量构成的矩阵;
//   5) 绕 z 旋转 roll 弧度:R = base · Rot_z(roll);
//   6) rollIndex/rollSamples 决定绕 z 的等分角,rollSamples 至少 1,均匀分布。
//
// 这是经典的"从一个方向反推完整姿态"的 Gram-Schmidt 风格构造。
rw::math::Rotation3D<> toolZDirectionToRotation (
    const rw::math::Vector3D<>& rawDirection, int rollIndex, int rollSamples)
{
    using rw::math::Vector3D;
    rw::math::Vector3D<> z = rawDirection;
    // 零向量兜底:理论上 sampleUnitDirections 不会产生 0,但 API 是公开的。
    if (z.norm2 () < 1e-12)
        z = Vector3D<>::z ();
    z = normalize (z);

    // reference 选择:0.9 的阈值给 ±Z 附近留出"安全区",避免 |reference·z| ≈ 1。
    const rw::math::Vector3D<> reference =
        std::fabs (z (2)) < 0.9 ? Vector3D<>::z () : Vector3D<>::y ();
    // x = reference × z,若 reference 与 z 共线(cross=0)则 fallback 到世界 X。
    rw::math::Vector3D<> x = cross (reference, z);
    if (x.norm2 () < 1e-12)
        x = Vector3D<>::x ();
    x = normalize (x);
    // y = z × x,自动右手系且正交(无需重新归一化,但代码里做了数值保险)。
    const rw::math::Vector3D<> y = normalize (cross (z, x));
    const rw::math::Rotation3D<> base (x, y, z);

    // roll 角:把 [0, 2π) 等分成 rollSamples 份;rollIndex 从 0 开始。
    const int rolls = std::max (1, rollSamples);
    const double roll = 2.0 * rw::math::Pi * static_cast< double > (rollIndex) /
                        static_cast< double > (rolls);
    // 注意复合顺序:base * Rz(roll) 表示"先做 Rz,再做 base"。
    return base * rw::math::EAA<> (Vector3D<>::z (), roll).toRotation3D ();
}

// =============================================================================
//  poseReachabilityTarget — 把 (position, rotation) 打包为 TaskPoint
// =============================================================================
// analyzeIk 接受 TaskPoint,所以这里把"内部用的 Vector3D + Rotation3D"反向
// 转换为 "TaskPoint(position, rpyDeg)"。note 字段写上 (direction, roll) 索引,
// 一旦某次 IK 失败,日志/警告可以立刻定位是哪个方向的问题。
TaskPoint poseReachabilityTarget (const std::array< double, 3 >& position,
                                  const rw::math::Rotation3D<>& rotation,
                                  int directionIndex,
                                  int rollIndex)
{
    TaskPoint target;
    target.id       = "pose_reachability";
    target.name     = "Pose reachability target";
    target.position = position;
    const rw::math::RPY<> rpy (rotation);
    const double toDeg = 180.0 / rw::math::Pi;
    target.rpyDeg = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    target.note = std::string ("direction=") + std::to_string (directionIndex) +
                  ", roll=" + std::to_string (rollIndex);
    return target;
}

// =============================================================================
//  meanValue — 算术平均
// =============================================================================
// 求均值;空 vector 返回 0.0(而不是 NaN),让 buildAggregateResult 的
// "manipulability_mean" 始终是有限数。
double meanValue (const std::vector< double >& values)
{
    if (values.empty ())
        return 0.0;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / static_cast< double > (values.size ());
}

// 构造单个 Q 对应的 WorkspaceSample:FK + 关节裕度 + Jacobian 指标 + 碰撞检测 +
// 状态分类。匿名命名空间内,只依赖 KinematicMetrics.h 与 RobWork 几何。

}    // namespace

void KinematicAnalyzer::setThresholds (const KinematicThresholds& thresholds)
{
    _thresholds = thresholds;
}

const KinematicThresholds& KinematicAnalyzer::thresholds () const
{
    return _thresholds;
}

// =============================================================================
//  analyzeCurrentPose
// =============================================================================
// 评估"当前 state 在选定 device / TCP 帧下的运动学质量"。
// 流程:
//   1) 校验 device / TCP 帧(空 device 直接 Fail,空 TCP 退化到 device 末端);
//   2) 读 q,跑 FK 得到 TCP 位姿;
//   3) 计算 baseJframe 的 6×n Jacobian;
//   4) 关节裕度 + 奇异指标(KinematicMetrics);
//   5) worstStatus 合并两类状态。
//
// 整个方法对调用方的 state 是 const 的,内部不修改它。
KinematicCurrentPoseResult KinematicAnalyzer::analyzeCurrentPose (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state) const
{
    KinematicCurrentPoseResult result;
    result.status = AnalysisStatus::Unknown;   // 默认 Unknown,遇到降级条件会被覆盖。

    // --------------------------------------------------------------------
    // 1) 没有设备 → 立即返回 Fail,不发部分指标(无 device 谈 FK 没意义)。
    // --------------------------------------------------------------------
    if (device == NULL) {
        result.status                       = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_NO_DEVICE";
        w.message  = "No device available for kinematic analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // --------------------------------------------------------------------
    // 2) TCP 帧解析:传 NULL 时回退到 device 末端帧(Warning 而非 Fail)。
    //    只有当 device->getEnd() 也为 NULL 时才真正 Fail。
    // --------------------------------------------------------------------
    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL) {
        resolvedTcpFrame = device->getEnd ();
        AnalysisWarning w;
        w.code     = "KIN_TCP_FALLBACK";
        w.message  = "No TCP frame provided; using device end as fallback.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Warning;
        result.warnings.push_back (w);
    }
    if (resolvedTcpFrame == NULL) {
        result.status = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_NO_TCP";
        w.message  = "Device has no end frame; cannot compute forward kinematics.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // 把 device/TCP 名写到结果里,UI 表格显示时直接用。
    result.deviceName  = device->getName ();
    result.tcpFrameName = resolvedTcpFrame->getName ();

    // --------------------------------------------------------------------
    // 拷贝关节值到 std::vector 便于跨接口(QTableWidget / JSON)。
    // q.e() 返回底层 Eigen 向量;用 assign+resize 而不是 assign(e.begin, e.end),
    // 是为了在 q 是空 Q 时也保证 result.q 至少是 size=0 的 vector。
    // --------------------------------------------------------------------
    const rw::math::Q q = device->getQ (state);
    result.q.assign (q.e ().begin (), q.e ().end ());
    result.q.resize (static_cast< std::size_t > (q.size ()));

    // --------------------------------------------------------------------
    // 3) 正运动学:baseTtcp = Kinematics::frameTframe(base, tcp, state)。
    //    这是 IK 的逆运算:给定关节值,求末端位姿。
    //    失败通常是 WorkCell 配置错乱(帧未挂到树上 / 设备基坐标系异常)。
    //    catch 住 std::exception 但不吞所有异常 —— 真正的非 std 异常
    //    (bad_alloc 等)继续向上抛。
    // --------------------------------------------------------------------
    try {
        const rw::math::Transform3D<> tcpTf =
            rw::kinematics::Kinematics::frameTframe (device->getBase (), resolvedTcpFrame, state);
        // 平移分量写入位置(米)。
        result.tcpPosition = {{tcpTf.P () (0), tcpTf.P () (1), tcpTf.P () (2)}};
        // 旋转分量转 RPY 度数,UI 显示直观。
        const rw::math::RPY<> rpy (tcpTf.R ());
        const double toDeg = 180.0 / rw::math::Pi;
        result.tcpRpyDeg = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    }
    catch (const std::exception&) {
        AnalysisWarning w;
        w.code     = "KIN_FK_FAILED";
        w.message  = "Forward kinematics failed for the selected device / TCP frame.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        result.status = AnalysisStatus::Fail;
        return result;
    }

    // --------------------------------------------------------------------
    // 4) 雅可比 J ∈ R^{6×n}:把"关节速度"映射到"末端线速度/角速度"。
    //    异常位形下某些 device 会抛错,记录告警但继续(没有 J 也能算裕度)。
    // --------------------------------------------------------------------
    rw::math::Jacobian jac;
    try {
        jac = device->baseJframe (resolvedTcpFrame, state);
    }
    catch (const std::exception&) {
        AnalysisWarning w;
        w.code     = "KIN_JACOBIAN_FAILED";
        w.message  = "Failed to compute base-to-TCP Jacobian.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
    }
    if (jac.size1 () > 0 && jac.size2 () > 0) {
        // 把 J 拷贝到 std::vector<double>(行优先),便于 JSON / UI 序列化。
        // 6×n 矩阵 → 6 行 × n 列展开,jacobianRowMajor.size() == rows*cols。
        result.jacobianRows = static_cast< int > (jac.size1 ());
        result.jacobianCols = static_cast< int > (jac.size2 ());
        result.jacobianRowMajor.assign (jac.e ().data (),
                                       jac.e ().data () + jac.e ().size ());
    }

    // --------------------------------------------------------------------
    // 5) 关节裕度:每个关节的归一化"距限位距离"。
    //    classifyJointLimitMargins 会向 result.warnings 追加 KIN_JOINT_LIMIT /
    //    KIN_NEAR_JOINT_LIMIT。
    // --------------------------------------------------------------------
    std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    result.jointLimitMargins = calculateJointLimitMargins (q, bounds);
    result.minJointLimitMargin =
        result.jointLimitMargins.empty () ? 0.0
                                         : minimumJointLimitMargin (result.jointLimitMargins);
    const AnalysisStatus limitStatus =
        classifyJointLimitMargins (q, bounds, _thresholds, &result.warnings);

    // --------------------------------------------------------------------
    // 奇异指标:J 的 SVD → σ_max / σ_min / 可操作度。
    //    注意此处 singular.warnings 是"额外的奇异告警",与上面 limitWarnings
    //    分别 push 到 result.warnings,避免互相覆盖。
    // --------------------------------------------------------------------
    const SingularMetrics singular = calculateSingularMetrics (jac, _thresholds);
    result.singularValues  = singular.singularValues;
    result.conditionNumber = singular.conditionNumber;
    result.manipulability  = singular.manipulability;
    for (const AnalysisWarning& w : singular.warnings)
        result.warnings.push_back (w);

    // --------------------------------------------------------------------
    // 状态合并:关节裕度和奇异度任一为 Fail → 整体 Fail;任一为 Warning →
    // Warning;否则 Pass。这正是 worstStatus 的两两合并语义。
    // --------------------------------------------------------------------
    if (limitStatus == AnalysisStatus::Fail || singular.status == AnalysisStatus::Fail)
        result.status = AnalysisStatus::Fail;
    else if (limitStatus == AnalysisStatus::Warning || singular.status == AnalysisStatus::Warning)
        result.status = AnalysisStatus::Warning;
    else
        result.status = AnalysisStatus::Pass;

    return result;
}

// =============================================================================
//  analyzeIk
// =============================================================================
// 对一个目标 TaskPoint 解 IK,并对每个原始解计算综合指标 + 评分。
//
// 流程:
//   1) 解析 device / TCP 帧(降级逻辑同 analyzeCurrentPose);
//   2) 用 JacobianIKSolver + 固定 seed 列表求解:
//        - JacobianIKSolver 是基于雅可比伪逆的迭代 IK,seed 决定起点;
//        - seed 列表由当前 Q、关节中心、零位和关节限位内固定组合构成;
//        - 不使用全局随机源,保证同一 target / state 下重复 Solve 结果稳定;
//   3) 对每个候选 q:
//        a) 在副本 state 上 setQ → 副本 state 用来验算 FK / 雅可比 / 碰撞;
//        b) 关节裕度、关节状态(失败原因);
//        c) FK 与目标位姿的位置 L2 / 姿态 EAA 误差;
//        d) 在该 q 处重算 Jacobian 的奇异指标;
//        e) 可选碰撞检查;
//        f) 综合评分(见下方公式);
//        g) 该解的 status:Pass → 叠加 limitStatus → 叠加 singular → 碰撞降级为 Fail;
//   4) sortIkSolutionsForDisplay 按 UI 偏好排序;
//   5) 解集总 status:存在 Pass → Pass,否则存在 Warning → Warning,否则 Fail。
//
// 注:整个方法对 state 是 const 的——所有 setQ 都在副本 solutionState 上。
KinematicIkAnalysisResult KinematicAnalyzer::analyzeIk (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const TaskPoint& target,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    KinematicIkAnalysisResult result;
    result.target = target;          // 把目标点也写到结果里,UI 列表不用额外维护
    result.status = AnalysisStatus::Unknown;

    std::string validationError;
    if (!validateTaskPointTarget (target, &validationError) ||
        !std::isfinite (_thresholds.positionToleranceMeters) ||
        _thresholds.positionToleranceMeters < 0.0 ||
        !std::isfinite (_thresholds.orientationToleranceDeg) ||
        _thresholds.orientationToleranceDeg < 0.0) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::InvalidTarget;
        result.warnings.push_back (makeWarning (
            "KIN_INVALID_TARGET",
            validationError.empty () ? "Kinematic target thresholds are invalid." : validationError,
            AnalysisStatus::Fail));
        return result;
    }

    if (device == NULL) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::NoDevice;
        AnalysisWarning w;
        w.code     = "KIN_NO_DEVICE";
        w.message  = "No device available for IK analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL)
        resolvedTcpFrame = device->getEnd ();
    if (resolvedTcpFrame == NULL) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::NoTcpFrame;
        AnalysisWarning w;
        w.code     = "KIN_NO_TCP";
        w.message  = "No TCP frame available for IK analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // 把目标点(UI 度数)转到 RobWork 的 Transform3D。
    const rw::math::Transform3D<> targetBaseTtcp = taskPointToTransform (target);
    std::vector< rw::math::Q > rawSolutions;
    const double duplicateQThreshold =
        std::isfinite (_thresholds.ikDuplicateQThreshold) &&
        _thresholds.ikDuplicateQThreshold >= 0.0 ?
        _thresholds.ikDuplicateQThreshold : 1e-4;

    // ---- IK 求解 ----
    // 两层 try:
    //   - 内层 catch std::exception:已知异常,带上 ex.what() 帮助定位;
    //   - 外层 catch (...):未知异常兜底,不让 UI 崩溃。
    try {
        // ownedPtr 构造堆上 JacobianIKSolver;Ptr 是 RobWork 的引用计数智能指针。
        rw::invkin::JacobianIKSolver::Ptr solver =
            rw::core::ownedPtr (new rw::invkin::JacobianIKSolver (device, resolvedTcpFrame, state));
        const rw::math::Q seedCurrentQ = device->getQ (state);
        const std::pair< rw::math::Q, rw::math::Q > seedBounds = device->getBounds ();
        const std::vector< rw::math::Q > seeds =
            deterministicIkSeeds (seedCurrentQ, seedBounds);
        for (const rw::math::Q& seed : seeds) {
            rw::kinematics::State seedState = state;
            device->setQ (seed, seedState);
            const std::vector< rw::math::Q > seedSolutions =
                solver->solve (targetBaseTtcp, seedState);
            result.rawCandidateCount += seedSolutions.size ();
            for (const rw::math::Q& q : seedSolutions)
                addUniqueIkCandidate (rawSolutions, q, duplicateQThreshold);
        }
    }
    catch (const std::exception& ex) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::SolverError;
        AnalysisWarning w;
        w.code     = "KIN_IK_SOLVER_ERROR";
        w.message  = std::string ("IK solver failed: ") + ex.what ();
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }
    catch (...) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::SolverError;
        AnalysisWarning w;
        w.code     = "KIN_IK_SOLVER_ERROR";
        w.message  = "IK solver failed with an unknown error.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    if (rawSolutions.empty ()) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::IkNoSolution;
        AnalysisWarning w;
        w.code     = "KIN_IK_NO_SOLUTION";
        w.message  = "No IK solution found for the target pose.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // 缓存当前 q 与 bounds,循环里多次复用。
    const rw::math::Q currentQ = device->getQ (state);
    if (currentQ.size () != device->getDOF ()) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::SolverError;
        result.warnings.push_back (makeWarning (
            "KIN_CURRENT_Q_DIMENSION",
            "Current joint vector dimension does not match the selected device DOF.",
            AnalysisStatus::Fail));
        return result;
    }
    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const double positionTolerance = effectiveTolerance (
        target.tolerance.positionMeters, _thresholds.positionToleranceMeters);
    const double orientationTolerance = effectiveTolerance (
        target.tolerance.orientationDeg, _thresholds.orientationToleranceDeg);
    for (const rw::math::Q& q : rawSolutions) {
        if (q.size () != device->getDOF ()) {
            result.warnings.push_back (makeWarning (
                "KIN_IK_Q_DIMENSION",
                "IK solver returned a joint vector with an unexpected dimension.",
                AnalysisStatus::Fail));
            continue;
        }
        KinematicIkSolution solution;
        solution.q = qToVector (q);
        solution.distanceToCurrentQ = qDistance (currentQ, q);

        // 关键:在副本 state 上跑 setQ,绝不污染调用方传入的 state。
        rw::kinematics::State solutionState = state;
        try {
            device->setQ (q, solutionState);
        }
        catch (const std::exception& ex) {
            result.warnings.push_back (makeWarning (
                "KIN_IK_STATE_ERROR",
                std::string ("Could not apply an IK solution: ") + ex.what (),
                AnalysisStatus::Fail));
            continue;
        }

        // ---- (a) 关节裕度 + 失败原因 ----
        const std::vector< double > margins = calculateJointLimitMargins (q, bounds);
        solution.minJointLimitMargin =
            margins.empty () ? 0.0 : minimumJointLimitMargin (margins);

        std::vector< AnalysisWarning > limitWarnings;
        const AnalysisStatus limitStatus =
            classifyJointLimitMargins (q, bounds, _thresholds, &limitWarnings);
        if (limitStatus == AnalysisStatus::Fail)
            solution.failureReasons.push_back (KinematicFailureReason::JointLimit);
        else if (limitStatus == AnalysisStatus::Warning)
            solution.failureReasons.push_back (KinematicFailureReason::NearJointLimit);

        // ---- (c) 用副本 state 验算 FK,与目标位姿比较 ----
        // 重要:即便 IK 求解器声称"已收敛",由于数值误差,FK 与目标仍可能
        // 有微小偏差。这两个字段就是给用户看"实际能到多准"的指标。
        rw::math::Transform3D<> actualBaseTtcp;
        try {
            actualBaseTtcp = rw::kinematics::Kinematics::frameTframe (
                device->getBase (), resolvedTcpFrame, solutionState);
            solution.positionErrorMeters = positionError (actualBaseTtcp, targetBaseTtcp);
            solution.orientationErrorDeg = orientationErrorDeg (actualBaseTtcp, targetBaseTtcp);
        }
        catch (const std::exception& ex) {
            result.warnings.push_back (makeWarning (
                "KIN_IK_FK_ERROR",
                std::string ("Could not validate an IK solution with FK: ") + ex.what (),
                AnalysisStatus::Fail));
            continue;
        }
        std::vector< AnalysisWarning > residualWarnings;
        const AnalysisStatus residualStatus = classifyTargetResidual (
            solution.positionErrorMeters, solution.orientationErrorDeg,
            positionTolerance, orientationTolerance,
            &solution.failureReasons, &residualWarnings);
        result.warnings.insert (
            result.warnings.end (), residualWarnings.begin (), residualWarnings.end ());

        // ---- (d) 在该 q 处重新算雅可比的奇异指标 ----
        SingularMetrics singular;
        try {
            singular = calculateSingularMetrics (
                device->baseJframe (resolvedTcpFrame, solutionState), _thresholds);
        }
        catch (const std::exception& ex) {
            result.warnings.push_back (makeWarning (
                "KIN_IK_JACOBIAN_ERROR",
                std::string ("Could not evaluate an IK solution Jacobian: ") + ex.what (),
                AnalysisStatus::Fail));
            continue;
        }
        solution.manipulability  = singular.manipulability;
        solution.conditionNumber = singular.conditionNumber;
        if (singular.status == AnalysisStatus::Fail)
            solution.failureReasons.push_back (KinematicFailureReason::Singular);
        else if (singular.status == AnalysisStatus::Warning)
            solution.failureReasons.push_back (KinematicFailureReason::NearSingular);

        // ---- (e) 碰撞检查(可选)----
        // 注意:inCollision 标志决定该解是否计入"reachable"。
        AnalysisStatus collisionStatus = AnalysisStatus::Pass;
        if (collisionDetector != NULL) {
            try {
                rw::proximity::CollisionDetector::QueryResult queryResult;
                solution.inCollision = collisionDetector->inCollision (solutionState, &queryResult);
                if (solution.inCollision)
                    solution.failureReasons.push_back (KinematicFailureReason::Collision);
            }
            catch (const std::exception& ex) {
                collisionStatus = AnalysisStatus::Fail;
                solution.failureReasons.push_back (KinematicFailureReason::SolverError);
                result.warnings.push_back (makeWarning (
                    "KIN_COLLISION_CHECK_ERROR",
                    std::string ("Collision checking failed: ") + ex.what (),
                    AnalysisStatus::Fail));
            }
        }

        // ---- (f) 评分(越小越优)----
        // 公式各项的物理含义:
        //   1e6 (碰撞?)        —— 碰撞是硬否决,加巨额常数让排序时排到最后;
        //   1000 · e_pos       —— 位置误差,放大到与姿态误差同量级(米 × 1000);
        //   e_ori (度)         —— 姿态误差,1:1 计入;
        //   dist_to_q          —— 与当前 q 的 L2 距离,鼓励"少动";
        //   -min_margin        —— 越大越好,所以减去;
        //   -manipulability    —— 越大越好,所以减去。
        // 这只是线性加权和,实际是工程经验值;要更严谨可以归一化后再加权。
        solution.score =
            (solution.inCollision ? 1000000.0 : 0.0) +
            solution.positionErrorMeters * 1000.0 +
            solution.orientationErrorDeg +
            solution.distanceToCurrentQ -
            solution.minJointLimitMargin -
            solution.manipulability;

        // ---- (g) 该解的 status ----
        // 初始 Pass,然后叠加 limitStatus / singular.status / 碰撞(强制 Fail)。
        // worstStatus(A, B) 选 A、B 中"更糟"的那个。
        solution.status = AnalysisStatus::Pass;
        solution.status = worstStatus (solution.status, residualStatus);
        solution.status = worstStatus (solution.status, limitStatus);
        solution.status = worstStatus (solution.status, singular.status);
        solution.status = worstStatus (solution.status, collisionStatus);
        if (solution.inCollision)
            solution.status = AnalysisStatus::Fail;

        result.solutions.push_back (solution);
    }

    if (result.solutions.empty ()) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::SolverError;
        result.warnings.push_back (makeWarning (
            "KIN_IK_NO_VALID_SOLUTIONS",
            "IK returned candidates, but none could be validated safely.",
            AnalysisStatus::Fail));
        return result;
    }

    // 按 UI 偏好排序(详见 sortIkSolutionsForDisplay 的注释)。
    sortIkSolutionsForDisplay (result.solutions);
    result.usableSolutionCount = countUsableIkSolutions (result.solutions);

    // 解集总状态:Pass 优先 → 否则 Warning → 否则 Fail。
    // 这里不用 worstStatus,因为 worstStatus 在 Pass + Pass 时也是 Pass,
    // 但只要有一个 Pass 就足够——所以用"首个 Pass 即胜出"的短路逻辑。
    result.status = AnalysisStatus::Fail;
    for (const KinematicIkSolution& solution : result.solutions) {
        if (solution.status == AnalysisStatus::Pass) {
            result.status = AnalysisStatus::Pass;
            break;
        }
        if (solution.status == AnalysisStatus::Warning)
            result.status = AnalysisStatus::Warning;
    }
    return result;
}

// =============================================================================
//  sortIkSolutionsForDisplay — UI 排序
// =============================================================================
// 优先级链(从强到弱):
//   1) 无碰撞优先 (inCollision=false 排前)
//   2) 位置误差小者优先
//   3) 姿态误差小者优先
//   4) 关节裕度大者优先
//   5) 可操作度大者优先
//   6) 与当前 q 的距离小者优先
//   7) 关节向量字典序(全部相同时保证稳定排序)
//
// 前 6 条都与 IK 评分公式的方向一致,确保 UI 显示顺序与"机器自动选择最
// 优解"的预期一致。
void rws::sortIkSolutionsForDisplay (std::vector< KinematicIkSolution >& solutions)
{
    std::sort (solutions.begin (), solutions.end (),
               [] (const KinematicIkSolution& lhs, const KinematicIkSolution& rhs) {
                   if (lhs.inCollision != rhs.inCollision)
                       return !lhs.inCollision;
                   if (lhs.positionErrorMeters != rhs.positionErrorMeters)
                       return lhs.positionErrorMeters < rhs.positionErrorMeters;
                   if (lhs.orientationErrorDeg != rhs.orientationErrorDeg)
                       return lhs.orientationErrorDeg < rhs.orientationErrorDeg;
                   if (lhs.minJointLimitMargin != rhs.minJointLimitMargin)
                       return lhs.minJointLimitMargin > rhs.minJointLimitMargin;
                   if (lhs.manipulability != rhs.manipulability)
                       return lhs.manipulability > rhs.manipulability;
                   if (lhs.distanceToCurrentQ != rhs.distanceToCurrentQ)
                       return lhs.distanceToCurrentQ < rhs.distanceToCurrentQ;
                   return lexicographicQLess (lhs.q, rhs.q);
               });
}

void rws::addUniqueIkCandidate (std::vector< rw::math::Q >& candidates,
                                const rw::math::Q& candidate,
                                double proximityLimit)
{
    if (proximityLimit <= 0.0) {
        candidates.push_back (candidate);
        return;
    }
    for (const rw::math::Q& existing : candidates) {
        if (qInfDistance (existing, candidate) <= proximityLimit)
            return;
    }
    candidates.push_back (candidate);
}

std::size_t rws::countUsableIkSolutions (
    const std::vector< KinematicIkSolution >& solutions)
{
    std::size_t count = 0;
    for (const KinematicIkSolution& solution : solutions) {
        if (!solution.inCollision && solution.status != AnalysisStatus::Fail)
            ++count;
    }
    return count;
}

// summarizeIkSolutions:单次遍历统计 5 个计数,避免 UI 多次遍历。
// 与 countUsableIkSolutions 行为一致:usable = !inCollision && status != Fail;
// 同时按 status 单独计数。
KinematicIkSummary rws::summarizeIkSolutions (
    const std::vector< KinematicIkSolution >& solutions)
{
    KinematicIkSummary summary;
    summary.totalCount = solutions.size ();
    for (const KinematicIkSolution& solution : solutions) {
        if (!solution.inCollision && solution.status != AnalysisStatus::Fail)
            ++summary.usableCount;
        if (solution.status == AnalysisStatus::Pass)
            ++summary.passCount;
        else if (solution.status == AnalysisStatus::Warning)
            ++summary.warningCount;
        else if (solution.status == AnalysisStatus::Fail)
            ++summary.failCount;
    }
    return summary;
}

// =============================================================================
//  primaryFailureFromIk — 主要失败原因归类
// =============================================================================
// 在 IK 任务点的失败原因归纳中,优先级:
//   IkNoSolution (无解)
//   > Collision  (全部碰撞)
//   > JointLimit > Singular > NearJointLimit > NearSingular(首个匹配)
//   > None       (成功)
//
// 算法:
//   - 没有解 → IkNoSolution;
//   - 扫所有解,只要遇到无碰撞解就标记 anyCollisionFree,并在它的
//     failureReasons 中按优先级挑首个匹配;
//   - 如果解集全碰撞(anyCollisionFree 仍为 false)→ Collision;
//   - 否则 None。
KinematicFailureReason primaryFailureFromIk (const KinematicIkAnalysisResult& ik)
{
    if (ik.solutions.empty ())
        return ik.failureReason == KinematicFailureReason::None ?
            KinematicFailureReason::IkNoSolution : ik.failureReason;
    bool anyCollisionFree = false;
    for (const KinematicIkSolution& s : ik.solutions) {
        if (s.inCollision)
            continue;
        anyCollisionFree = true;
        if (s.status == AnalysisStatus::Pass)
            return KinematicFailureReason::None;
    }
    if (!anyCollisionFree)
        return KinematicFailureReason::Collision;

    const KinematicFailureReason priority[] = {
        KinematicFailureReason::SolverError,
        KinematicFailureReason::TargetResidual,
        KinematicFailureReason::JointLimit,
        KinematicFailureReason::Singular,
        KinematicFailureReason::NearJointLimit,
        KinematicFailureReason::NearSingular
    };
    for (KinematicFailureReason reason : priority) {
        for (const KinematicIkSolution& solution : ik.solutions) {
            if (!solution.inCollision && hasFailureReason (solution, reason))
                return reason;
        }
    }
    return KinematicFailureReason::None;
}

// =============================================================================
//  analyzeTaskPoints — 批量 IK + 任务级状态归类
// =============================================================================
// 对每个 TaskPoint:
//   1) 若 disabled:挂一条 KIN_TASK_DISABLED 警告,IK 留空,推到结果里;
//   2) 否则调 analyzeIk → 根据 IK 结果归类:
//
//      IK 整体状态           → 任务级 status
//      ------------------------------------------------------------
//      无解                  → Fail + IkNoSolution
//      全碰撞                → Fail + Collision
//      primary=JointLimit    → Fail + JointLimit
//      primary=Singular      → Fail + Singular
//      primary=NearJointLim  → Warning + NearJointLimit
//      primary=NearSingular  → Warning + NearSingular
//      primary=None          → Pass
//
//   3) 后校正:若本应 Warning,但实际不存在"无碰撞的 Warning 解"
//      (即所有无碰撞解都 Pass),降级为 Pass,避免"primary 看着像 Warning
//      但实际只是某些解被选为首选"的假阳性。
std::vector< TaskPointReachabilityResult > KinematicAnalyzer::analyzeTaskPoints (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& taskPoints,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    std::vector< TaskPointReachabilityResult > results;
    results.reserve (taskPoints.size ());
    for (const TaskPoint& point : taskPoints) {
        TaskPointReachabilityResult r;
        r.taskPoint = point;
        r.status    = AnalysisStatus::Unknown;

        // ---- disabled 任务点:不计入可达率分母 ----
        if (!point.enabled) {
            r.primaryFailure = KinematicFailureReason::None;
            AnalysisWarning w;
            w.code     = "KIN_TASK_DISABLED";
            w.message  = "Task point is disabled; skipped from reachability denominator.";
            w.source   = "KinematicAnalyzer";
            w.severity = AnalysisStatus::Warning;
            // 警告放进 IK 容器,这样 UI 在查看结果时仍能看到上下文。
            r.ik.warnings.push_back (w);
            r.ik.target = point;
            results.push_back (r);
            continue;
        }

        r.ik = analyzeIk (device, tcpFrame, state, point, collisionDetector);

        if (r.ik.solutions.empty ()) {
            r.status         = AnalysisStatus::Fail;
            r.primaryFailure = primaryFailureFromIk (r.ik);
            r.failureReasons.push_back (r.primaryFailure);
        }
        else {
            r.primaryFailure = primaryFailureFromIk (r.ik);
            r.status = r.ik.status;
            if (r.primaryFailure != KinematicFailureReason::None)
                r.failureReasons.push_back (r.primaryFailure);
        }
        results.push_back (r);
    }
    return results;
}

// =============================================================================
//  calculateReachableRate — 任务点可达率
// =============================================================================
// 分子:Pass 或 Warning 的任务点数(都算"reachable");
// 分母:启用的任务点总数(disabled 不计入分母);
// 全 disabled → 返回 0.0(避免除零)。
//
// 这就是 README 中"Task point reachable rate counts Pass and Warning as
// reachable and excludes disabled task points" 的实现位置。
double KinematicAnalyzer::calculateReachableRate (
    const std::vector< TaskPointReachabilityResult >& results) const
{
    std::size_t reachable = 0;
    std::size_t enabled   = 0;
    for (const TaskPointReachabilityResult& r : results) {
        if (!r.taskPoint.enabled)
            continue;
        ++enabled;
        if (r.status == AnalysisStatus::Pass || r.status == AnalysisStatus::Warning)
            ++reachable;
    }
    if (enabled == 0)
        return 0.0;
    return static_cast< double > (reachable) / static_cast< double > (enabled);
}

namespace {

// =============================================================================
//  makeWorkspaceSample(匿名命名空间)
// =============================================================================
// 构造单个 Q 对应的 WorkspaceSample:
//   1) 在副本 state 上跑 FK,得到 TCP 位置;
//   2) 关节裕度;
//   3) Jacobian 的奇异指标;
//   4) 可选碰撞检查;
//   5) 按优先级合并状态:inCollision > 奇异 Fail > 奇异 Warning 或 近限位 > Pass。
//
// 设计要点:
//   - 始终在副本 state 上 setQ,不污染调用方;
//   - FK / Jacobian / 碰撞三处都可能抛错,这里用 catch(...) 兜底,
//     但只降级 status,不让一次坏样本阻断整个采样循环;
//   - 状态优先级"碰撞 > 奇异 Fail > ..."是工程经验:即便奇异,只要碰了,
//     物理上也不可达,所以 Fail 优先。
WorkspaceSample makeWorkspaceSample (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& baseState,
    const rw::math::Q& q,
    const KinematicThresholds& thresholds,
    bool checkCollision,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector)
{
    WorkspaceSample sample;

    // 先把 q 拷贝到 std::vector<double>(序列化用)。
    sample.q.reserve (q.size ());
    for (std::size_t i = 0; i < q.size (); ++i)
        sample.q.push_back (q (i));

    // 关键:副本 state,绝不污染 baseState。
    rw::kinematics::State sampleState = baseState;
    device->setQ (q, sampleState);

    // ---- FK:失败直接返回 Fail,保留 q 用于排查 ----
    try {
        const rw::math::Transform3D<> transform =
            rw::kinematics::Kinematics::frameTframe (device->getBase (), tcpFrame.get (), sampleState);
        sample.tcpPosition[0] = transform.P () (0);
        sample.tcpPosition[1] = transform.P () (1);
        sample.tcpPosition[2] = transform.P () (2);
    }
    catch (...) {
        sample.status = AnalysisStatus::Fail;
        return sample;
    }

    // ---- 关节裕度 ----
    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const std::vector< double > margins = calculateJointLimitMargins (q, bounds);
    sample.minJointLimitMargin =
        margins.empty () ? 0.0 : minimumJointLimitMargin (margins);

    // ---- Jacobian 奇异指标 ----
    // 退化位形可能抛错,记 Fail 但不让整个循环挂掉。
    SingularMetrics singular;
    try {
        singular = calculateSingularMetrics (device->baseJframe (tcpFrame.get (), sampleState), thresholds);
    }
    catch (...) {
        singular.status = AnalysisStatus::Fail;
    }
    sample.manipulability  = singular.manipulability;
    sample.conditionNumber = singular.conditionNumber;

    // ---- 碰撞检查(可选)----
    sample.inCollision = false;
    if (checkCollision && collisionDetector != NULL) {
        try {
            sample.inCollision = collisionDetector->inCollision (sampleState);
        }
        catch (...) {
            sample.inCollision = false;
        }
    }

    // 状态合并:优先级 碰撞 > 奇异 Fail > (奇异 Warning 或 近限位) > Pass。
    if (sample.inCollision)
        sample.status = AnalysisStatus::Fail;
    else if (singular.status == AnalysisStatus::Fail)
        sample.status = AnalysisStatus::Fail;
    else if (singular.status == AnalysisStatus::Warning ||
             sample.minJointLimitMargin < thresholds.nearJointLimitRatio)
        sample.status = AnalysisStatus::Warning;
    else
        sample.status = AnalysisStatus::Pass;

    return sample;
}

}    // namespace

// =============================================================================
//  sampleWorkspace — 关节空间采样
// =============================================================================
// 两种模式:
//   - RandomUniform:每关节一个 uniform_real_distribution,用固定种子的
//     std::mt19937 抽样 N 个 q;
//   - Grid:每关节 steps 等距分,总组合 steps^dof;高 DOF 时总组合会爆,
//     故对 steps^dof 增长做 early break,截断到 config.sampleCount
//     (等价于取字典序前 sampleCount 个网格点)。
//
// 入口校验(返回空 vector 而非抛异常):
//   - device / tcpFrame 不能为 NULL;
//   - sampleCount > 0;
//   - dof > 0 且 bounds 维度与 dof 一致;
//   - 每个关节 lo/hi 都是有限数且 hi > lo。
//
// 固定种子的好处:同一份 WorkCell + 同一阈值,在不同机器、不同时间跑出来的
// 样本序列一致,方便做回归测试与对比分析。
std::vector< WorkspaceSample > KinematicAnalyzer::sampleWorkspace (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceSamplingConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    std::vector< WorkspaceSample > samples;

    // ---- 入口校验 ----
    if (device == NULL)
        return samples;
    if (config.sampleCount <= 0)
        return samples;
    if (tcpFrame == NULL)
        return samples;

    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const rw::math::Q& lower = bounds.first;
    const rw::math::Q& upper = bounds.second;
    const std::size_t dof = device->getDOF ();
    if (dof == 0)
        return samples;
    if (lower.size () != dof || upper.size () != dof)
        return samples;
    // NaN / Inf / 反向限位都视为配置错误,直接放弃。
    for (std::size_t i = 0; i < dof; ++i) {
        if (!std::isfinite (lower (i)) || !std::isfinite (upper (i)) || upper (i) <= lower (i))
            return samples;
    }

    // ---- RandomUniform ----
    if (config.mode == WorkspaceSamplingMode::RandomUniform) {
        // seed==0 视作 1,避免 mt19937(0) 在某些实现下行为退化。
        std::mt19937 rng (config.randomSeed == 0 ? 1u : config.randomSeed);
        std::vector< std::uniform_real_distribution< double > > distributions;
        distributions.reserve (dof);
        for (std::size_t i = 0; i < dof; ++i)
            distributions.emplace_back (lower (i), upper (i));

        samples.reserve (static_cast< std::size_t > (config.sampleCount));
        for (int sampleIndex = 0; sampleIndex < config.sampleCount; ++sampleIndex) {
            rw::math::Q q (dof);
            for (std::size_t j = 0; j < dof; ++j)
                q (j) = distributions[j] (rng);  // 每个关节独立均匀抽样
            samples.push_back (makeWorkspaceSample (
                device, tcpFrame, state, q, _thresholds,
                config.checkCollision, collisionDetector));
        }
        return samples;
    }

    // ---- Grid ----
    // 总组合 steps^dof;一旦超过 sampleCount,后续连乘都没必要,
    // 直接 early break 节省计算。
    const int steps = std::max (1, config.gridStepsPerJoint);
    std::size_t total = 1;
    for (std::size_t i = 0; i < dof; ++i) {
        if (total > static_cast< std::size_t > (config.sampleCount))
            break;
        total *= static_cast< std::size_t > (steps);
    }
    const std::size_t target =
        std::min (static_cast< std::size_t > (config.sampleCount), total);

    samples.reserve (target);
    for (std::size_t index = 0; index < target; ++index) {
        // 把 0..target 范围内的线性 index 展开为 base-steps 数字。
        // 例如 6 自由度 + steps=3:index=10 → 001011(从 joint 0 开始读),
        // 即每关节的步索引 [1, 0, 2] 等。
        std::size_t cursor = index;
        rw::math::Q q (dof);
        for (std::size_t joint = 0; joint < dof; ++joint) {
            const std::size_t stepIndex = steps <= 1 ? 0u : (cursor % static_cast< std::size_t > (steps));
            cursor /= static_cast< std::size_t > (steps);
            if (steps <= 1) {
                // steps<=1 退化:每个关节只取一次,固定到中点。
                q (joint) = 0.5 * (lower (joint) + upper (joint));
            }
            else {
                // 把 stepIndex ∈ [0, steps) 映射到关节值的 [lo, hi] 区间。
                // ratio = stepIndex / (steps - 1) ∈ [0, 1]。
                const double ratio = static_cast< double > (stepIndex) /
                                     static_cast< double > (steps - 1);
                q (joint) = lower (joint) + ratio * (upper (joint) - lower (joint));
            }
        }
        samples.push_back (makeWorkspaceSample (
            device, tcpFrame, state, q, _thresholds,
            config.checkCollision, collisionDetector));
    }
    return samples;
}

// =============================================================================
//  analyzePoseReachability — 位姿可达性
// =============================================================================
// 目标:在给定的若干空间位置周围,工具能在多大程度上旋转到任意朝向。
//
// 流程:
//   - sampleUnitDirections 在单位球面上均匀采 directionCount 个方向;
//   - 每个方向绕 Z 轴等分成 rollCount 份滚动;
//   - 总共 directionCount × rollCount 个姿态目标;
//   - 每个姿态构造成 TaskPoint 后调 analyzeIk;
//   - 只要 IK 至少有一个无碰撞 Pass/Warning 解,该方向就算 reachable。
//
// 结果:
//   - sampled       = directionCount × rollCount;
//   - reachable     = 至少一个 Pass/Warning 解的方向数;
//   - coverage      = reachable / sampled ∈ [0, 1];
//   - 状态:reachable=0 → Fail,reachable=sampled → Pass,部分 → Warning。
//
// 用 Fibonacci 螺旋而不是经纬度网格,是为了避免"两极聚集",
// 保证所有方向被同等采样,这样 coverage 指标反映真实可达性而非采样偏差。
std::vector< PoseReachabilitySample > KinematicAnalyzer::analyzePoseReachability (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< std::array< double, 3 > >& positions,
    const PoseReachabilityConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    std::vector< PoseReachabilitySample > results;
    results.reserve (positions.size ());

    // 防御性归零:directionSamples<=0 直接返回零样本。
    const int directionCount = std::max (0, config.directionSamples);
    const int rollCount      = directionCount == 0 ? 0 : std::max (1, config.rollSamples);
    const int totalDirections = directionCount * rollCount;
    const std::vector< rw::math::Vector3D<> > directions =
        sampleUnitDirections (directionCount);

    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL && device != NULL)
        resolvedTcpFrame = device->getEnd ();

    for (const std::array< double, 3 >& position : positions) {
        PoseReachabilitySample sample;
        sample.position          = position;
        sample.sampledDirections = totalDirections;

        // 兜底:device / TCP / 总方向数任一为 0,直接报 Fail。
        if (device == NULL || resolvedTcpFrame == NULL || totalDirections == 0) {
            sample.status = AnalysisStatus::Fail;
            results.push_back (sample);
            continue;
        }

        // 双层循环:(方向, 滚动) → 一次 IK。
        for (int directionIndex = 0; directionIndex < directionCount; ++directionIndex) {
            for (int rollIndex = 0; rollIndex < rollCount; ++rollIndex) {
                // 给定 Z 方向 + 滚动角,反推完整旋转矩阵。
                const rw::math::Rotation3D<> rotation =
                    toolZDirectionToRotation (
                        directions[static_cast< std::size_t > (directionIndex)],
                        rollIndex, rollCount);
                // 把 (position, rotation) 包成 TaskPoint。note 字段会记录
                // direction / roll 索引,IK 失败时日志可以直接定位。
                const TaskPoint target =
                    poseReachabilityTarget (position, rotation, directionIndex, rollIndex);
                // 注意:config.checkCollision 为 false 时这里传 NULL,
                // analyzeIk 内部会跳过碰撞检查,使该方向的可达性仅由 IK 决定。
                const KinematicIkAnalysisResult ik = analyzeIk (
                    device, resolvedTcpFrame, state, target,
                    config.checkCollision ? collisionDetector : NULL);

                // "该方向可达"的判定:至少一个无碰撞的 Pass/Warning 解。
                bool reachable = false;
                for (const KinematicIkSolution& solution : ik.solutions) {
                    if (solution.inCollision)
                        continue;
                    if (solution.status == AnalysisStatus::Pass ||
                        solution.status == AnalysisStatus::Warning) {
                        reachable = true;
                        break;
                    }
                }
                if (reachable)
                    ++sample.reachableDirections;
            }
        }

        // coverage = reachable / sampled;totalDirections==0 时上面已经 continue,
        // 但仍保留三元判断以防调用方改了逻辑。
        sample.coverage =
            totalDirections == 0 ? 0.0 :
            static_cast< double > (sample.reachableDirections) /
                static_cast< double > (totalDirections);
        if (sample.reachableDirections == 0)
            sample.status = AnalysisStatus::Fail;
        else if (sample.reachableDirections == totalDirections)
            sample.status = AnalysisStatus::Pass;
        else
            sample.status = AnalysisStatus::Warning;
        results.push_back (sample);
    }
    return results;
}

// =============================================================================
//  buildAggregateResult — 报告聚合
// =============================================================================
// 把四种分析结果聚合成一个总报告:
//   1) 写 header(pluginName + pluginVersion);
//   2) reachableRate = calculateReachableRate(taskPointResults);
//   3) status 用 worstStatus 合并四类子结果(从 currentPose.status 起步,
//      依次与任务点 / workspace / poseReachability 合并);
//   4) manipulabilityMap:收集 currentPose 与所有 workspace 样本的可操作度,
//      排序后取 min / max / mean / p10(p10 取 10% 分位);
//   5) 按 warning.code 子串匹配把奇异 / 关节告警分桶到
//      singularityWarnings / jointLimitWarnings;
//   6) workspace 样本中:有碰撞的 Fail → 通用警告 KIN_WORKSPACE_COLLISION;
//      状态为 Warning 的 → jointLimitWarnings 中追加 KIN_WORKSPACE_QUALITY_WARNING。
//
// 注意:这里用 warning.code 子串匹配分桶是一种"松散约定",
// 字符串不变则分类就不变。如果将来重命名告警 code,需同步改这里。
KinematicAnalysisResult KinematicAnalyzer::buildAggregateResult (
    const KinematicCurrentPoseResult& currentPose,
    const std::vector< TaskPointReachabilityResult >& taskPointResults,
    const std::vector< WorkspaceSample >& workspaceSamples,
    const std::vector< PoseReachabilitySample >& poseReachability) const
{
    KinematicAnalysisResult result;
    result.header.pluginName    = "KinematicAnalysis";
    result.header.pluginVersion = "1.0.0";
    // 四类结果原样塞进聚合体,JSON / CSV 导出按这些字段读取。
    result.currentPose          = currentPose;
    result.taskPointResults     = taskPointResults;
    result.workspaceSamples     = workspaceSamples;
    result.poseReachability     = poseReachability;
    // 任务点可达率放在 result 顶层(便于 Report tab 一行展示)。
    result.reachableRate        = calculateReachableRate (taskPointResults);

    // worstStatus 从 currentPose 开始,逐步合并四类结果的状态。
    result.status = currentPose.status;
    for (const TaskPointReachabilityResult& task : taskPointResults)
        result.status = worstStatus (result.status, task.status);
    for (const WorkspaceSample& sample : workspaceSamples)
        result.status = worstStatus (result.status, sample.status);
    for (const PoseReachabilitySample& sample : poseReachability)
        result.status = worstStatus (result.status, sample.status);

    // 收集可操作度样本:currentPose + 所有 workspace(过滤掉 0 值)。
    std::vector< double > manipulabilityValues;
    manipulabilityValues.reserve (workspaceSamples.size () + 1);
    if (currentPose.manipulability > 0.0)
        manipulabilityValues.push_back (currentPose.manipulability);
    for (const WorkspaceSample& sample : workspaceSamples) {
        if (sample.manipulability > 0.0)
            manipulabilityValues.push_back (sample.manipulability);

        // workspace 碰撞:即便单个样本 Fail,仍作为"工程提醒"以 Warning 上报。
        if (sample.status == AnalysisStatus::Fail && sample.inCollision)
            result.warnings.push_back (makeWarning (
                "KIN_WORKSPACE_COLLISION",
                "At least one workspace sample is in collision.",
                AnalysisStatus::Warning));
        // workspace 警告样本:归到 jointLimitWarnings 桶(名称虽含 limit,
        // 实际承载"关节限位 / 奇异"两类质量退化告警)。
        if (sample.status == AnalysisStatus::Warning)
            result.jointLimitWarnings.push_back (makeWarning (
                "KIN_WORKSPACE_QUALITY_WARNING",
                "A workspace sample is near a joint limit or singularity.",
                AnalysisStatus::Warning));
    }

    if (!manipulabilityValues.empty ()) {
        // 排序后用索引取 min / max / p10,mean 用单独函数。
        // p10 取 10% 分位:在已排序数组上用线性插值近似,
        // 这里简化为 floor(0.1 * (n - 1)) 直接索引,精度够 UI 显示。
        std::sort (manipulabilityValues.begin (), manipulabilityValues.end ());
        MetricValue minMetric;
        minMetric.name  = "manipulability_min";
        minMetric.value = manipulabilityValues.front ();
        MetricValue maxMetric;
        maxMetric.name  = "manipulability_max";
        maxMetric.value = manipulabilityValues.back ();
        MetricValue meanMetric;
        meanMetric.name  = "manipulability_mean";
        meanMetric.value = meanValue (manipulabilityValues);
        MetricValue p10Metric;
        p10Metric.name = "manipulability_p10";
        const std::size_t p10Index =
            static_cast< std::size_t > (0.1 * static_cast< double > (manipulabilityValues.size () - 1));
        p10Metric.value = manipulabilityValues[p10Index];
        result.manipulabilityMap.push_back (minMetric);
        result.manipulabilityMap.push_back (maxMetric);
        result.manipulabilityMap.push_back (meanMetric);
        result.manipulabilityMap.push_back (p10Metric);
    }

    // currentPose 自带的告警:既入 result.warnings,又按 code 子串分桶。
    for (const AnalysisWarning& warning : currentPose.warnings) {
        result.warnings.push_back (warning);
        if (warning.code.find ("SINGULAR") != std::string::npos ||
            warning.code.find ("CONDITION") != std::string::npos)
            result.singularityWarnings.push_back (warning);
        if (warning.code.find ("JOINT") != std::string::npos ||
            warning.code.find ("LIMIT") != std::string::npos)
            result.jointLimitWarnings.push_back (warning);
    }

    return result;
}
