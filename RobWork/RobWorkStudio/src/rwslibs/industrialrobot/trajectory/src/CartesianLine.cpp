/**
 * @file   CartesianLine.cpp
 * @brief  笛卡尔直线段规划的实现翻译单元——§8.2 几何构造（轴向解析/起点
 *         生成/最短弧 slerp）、§8.3 采样计划与 IK ③端口逐采样消费（分支
 *         跟踪）、§8.4 采样失败素材、§8.5 奇异邻域 warning（WP-16-T05）。
 *
 * 设计依据：
 *   - units/trajectory.md §8.2~§8.5（几何/采样/连续性/失败/奇异逐条——
 *     与头文件同名章节对应）、§15.2（接口前置/非法示例）、§15.4（端口
 *     消费纪律）、§7.5（首采样分支延续＝choosePtpCandidate）
 *   - 需求 TRJ-02/TRJ-06、REQ-02（approach 负向/retract 正向——几何解释
 *     口径）、REQUIREMENTS §8.1 C5/C8（搜索未果口径——不判不可行）、
 *     NFR-COR-01/02/03
 *   - 任务契约 tasks/foundation/WP-16-T05.json（acceptance 1/2）
 *
 * 实现决策登记（DTB §5.4——偏差与补全，均已在单元卡 §21.5 增量登记）：
 *   - ToolZ 轴向统一以 workPose 旋转部解析（§8.2 原文"经 T_end（或
 *     T_start）旋转部"——approach 的 T_end 与 retract 的 T_start 同为
 *     workPose，单源消歧）；
 *   - 采样点 IK 初值策略恒 JointGrid（无显式参考构型的确定性策略）；
 *     首次调用初值数取端口请求缺省（8），解集空且结局∈{2,3}时按 §8.4
 *     "初值策略扩充"扩至 4 倍重试一次；结局 5（解析界限）不重试——
 *     确定性界限与初值无关；
 *   - IK 收敛容差/去重阈值不显式赋值——随 IkPortRequest 缺省值透传
 *     （缺省权威＝kin.task-point-ik 契约缺省，本域零第二字面量）。
 *
 * 确定性（NFR-COR-02）：采样计划解析式决定；分支比较谓词全序（偏差和
 * 最小→stableIndex 升序）；无浮点归约顺序歧义。
 */

#include <sdurws/ird/trajectory/CartesianLine.hpp>

#include <rw/math/Quaternion.hpp>           // 姿态插值（§8.2 slerp 的四元数载体）
#include <rw/math/Rotation3D.hpp>           // inverse（姿态角差）

#include <sdurws/ird/core/Units.hpp>        // core::UnitToken（比较型素材单位——"1" 无量纲）
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace sdurws::ird::trajectory {

namespace {

/// 有限性判定（NaN 与 ±inf 均拒绝——NFR-COR-03；与 Ptp.cpp 同判据）。
bool isFiniteDouble(double v)
{
    return v == v && v - v == 0.0;
}

/// acos 安全包裹：入参钳制到 [-1,1]（浮点点积可能越出 ±1 一个 ULP）。
double safeAcos(double x)
{
    if (x > 1.0) {
        return 0.0;
    }
    if (x < -1.0) {
        return std::acos(-1.0);
    }
    return std::acos(x);
}

/// 旋转矩阵正交性核验（构造点 fail-fast 的判定式）：三列范数平方与 1 的
/// 偏差 ≤1×10⁻⁹，且两两列点积绝对值 ≤1×10⁻⁹（无量纲——旋转无量纲）。
bool isOrthogonal(const rw::math::Rotation3D<double>& R)
{
    constexpr double kOrthoEps = 1e-9;  // 正交性残差上界（无量纲；浮点 1e-15 级的正常余量的宽裕界）
    rw::math::Vector3D<double> c0(R(0, 0), R(1, 0), R(2, 0));
    rw::math::Vector3D<double> c1(R(0, 1), R(1, 1), R(2, 1));
    rw::math::Vector3D<double> c2(R(0, 2), R(1, 2), R(2, 2));
    const auto n2 = [](const rw::math::Vector3D<double>& v) {
        return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    };
    const auto dot = [](const rw::math::Vector3D<double>& a,
                        const rw::math::Vector3D<double>& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    return std::fabs(n2(c0) - 1.0) <= kOrthoEps
        && std::fabs(n2(c1) - 1.0) <= kOrthoEps
        && std::fabs(n2(c2) - 1.0) <= kOrthoEps
        && std::fabs(dot(c0, c1)) <= kOrthoEps
        && std::fabs(dot(c0, c2)) <= kOrthoEps
        && std::fabs(dot(c1, c2)) <= kOrthoEps;
}

/// 四元数点积（x·x'+y·y'+z·z'+w·w'——分量经 getQx..getQw 取，避免
/// Eigen 存储顺序歧义直用 .e()）。
double quatDot(const rw::math::Quaternion<double>& a,
               const rw::math::Quaternion<double>& b)
{
    return a.getQx() * b.getQx() + a.getQy() * b.getQy()
         + a.getQz() * b.getQz() + a.getQw() * b.getQw();
}

/// 归一化后的四元数分量组（构造自旋转矩阵后再归一——旋转矩阵构造已
/// 归一，此处防御非单位输入的数值漂移）。
struct QuatComponents {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

QuatComponents normalizedQuat(const rw::math::Rotation3D<double>& R)
{
    const rw::math::Quaternion<double> q(R);
    QuatComponents c{q.getQx(), q.getQy(), q.getQz(), q.getQw()};
    const double n2 = c.x * c.x + c.y * c.y + c.z * c.z + c.w * c.w;
    if (n2 > 0.0) {
        const double inv = 1.0 / std::sqrt(n2);
        c.x *= inv;
        c.y *= inv;
        c.z *= inv;
        c.w *= inv;
    }
    return c;
}

/// 姿态角差（rad）：相对旋转 R_rel = R_a · R_bᵀ 的旋转角
/// θ = 2·atan2(‖vec(q_rel)‖, |q_rel.w|)——以 |w| 保证落在最短弧 [0,π]。
double rotationAngleBetween(const rw::math::Rotation3D<double>& Ra,
                            const rw::math::Rotation3D<double>& Rb)
{
    // 相对旋转的姿态四元数：先构造 Rb 的逆旋转再复合（Rotation3D 无
    // 直接 transpose 转 Quaternion 的开销敏感路径，走 Rotation3D 代数）。
    const rw::math::Rotation3D<double> Rrel = Ra * rw::math::inverse(Rb);
    const QuatComponents q = normalizedQuat(Rrel);
    const double vecNorm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    return 2.0 * std::atan2(vecNorm, std::fabs(q.w));
}

/// 两端解摘要（素材文案用——逐轴数值列表；rad|m 逐轴混合计量口径在
/// cause 文案中显式写明）。
std::string qSummary(const std::vector<double>& q)
{
    std::string s = "(";
    for (std::size_t i = 0; i < q.size(); ++i) {
        if (i != 0) {
            s += ", ";
        }
        s += std::to_string(q[i]);
    }
    s += ")";
    return s;
}

/// IkPortSolution → PtpCandidate 投影（§7.5 三键同名同义——§15.4 稳定
/// 排序视图序透传；维度核验由调用方完成）。
PtpCandidate toPtpCandidate(const IkPortSolution& s)
{
    PtpCandidate c;
    c.q = rw::math::Q(static_cast<int>(s.q.size()));
    for (std::size_t i = 0; i < s.q.size(); ++i) {
        c.q[i] = s.q[i];
    }
    c.stableIndex = s.stableIndex;
    c.minimumJointMargin = s.minimumJointMargin;
    return c;
}

/// 构造奇异 warning 比较型字段（实际条件数/阈值/单位 1——UX-03 三要素；
/// TRJ-SINGULAR-NEIGHBORHOOD 码表 requiresComparison 的素材侧承载）。
core::ComparativeFields singularComparison(double conditionNumber, double threshold)
{
    core::ComparativeFields cmp;
    // 来源标记：DerivedReadOnly——条件数是 FK 端口对构型的派生测量
    // （非用户直输入；ValueProvenance 五类词表 DerivedReadOnly 承载）。
    const auto provenance = core::ValueProvenance::make(
        core::ProvenanceKind::DerivedReadOnly, {}, {}, "cartesian-singular");
    cmp.actual.quantity = core::SourcedValue<double>::provided(conditionNumber, provenance);
    // 单位"1"（无量纲）——core Units 注册表冻结项（find 失败属 core 缺陷，
    // Units 测试已钉住；此处断言后取值）。
    const auto unitOne = core::UnitToken::find("1");
    cmp.actual.unit = *unitOne;
    cmp.expected.quantity = core::SourcedValue<double>::provided(threshold, provenance);
    cmp.expected.unit = *unitOne;
    return cmp;
}

}  // namespace

// =====================================================================
// 笛卡尔直线插值（§8.2）
// =====================================================================

rw::math::Transform3D<double> interpolateCartesianLine(
    const rw::math::Transform3D<double>& ta,
    const rw::math::Transform3D<double>& tb,
    double s)
{
    // 前置：行程参数 ∈[0,1]（越界外推属几何语义破坏——§8.2 定义域）。
    if (!(s >= 0.0 && s <= 1.0)) {
        throw TrajectoryError("trajectory/cartesian/interpolate-range",
                              "行程参数 s 须 ∈[0,1]，实际 " + std::to_string(s));
    }

    // 端点快速路径：s=0/1 直接返回端点位姿——保证端点**位级**精确还原
    // （V-04 断言依托；通用 slerp 公式在 s=0/1 时 sin(θ)/sinθ 的浮点除
    // 不保证精确等于 1/0，会有 1 ULP 级姿态漂移，违反端点还原义务）。
    if (s == 0.0) {
        return ta;
    }
    if (s == 1.0) {
        return tb;
    }

    // 位置：逐分量凸组合 p(s)=(1-s)·p_a+s·p_b——s=0/1 浮点下精确还原
    // 端点（V-04 端点还原断言依托；与 PTP 插值同式的笛卡尔侧对应）。
    const rw::math::Vector3D<double>& pa = ta.P();
    const rw::math::Vector3D<double>& pb = tb.P();
    const rw::math::Vector3D<double> p((1.0 - s) * pa[0] + s * pb[0],
                                       (1.0 - s) * pa[1] + s * pb[1],
                                       (1.0 - s) * pa[2] + s * pb[2]);

    // 姿态：四元数最短路径 slerp（§8.2"姿态最短弧 slerp（四元数最短
    // 路径）"）。手写显式实现以锁定口径（V-05 与显式四元数对照）：
    // 两端各取单位四元数→点积为负时翻转一端（±q 同旋转成对，取非负
    // 点积一支保证最短弧）→θ=acos(dot)→标准 slerp 公式。
    QuatComponents qa = normalizedQuat(ta.R());
    QuatComponents qb = normalizedQuat(tb.R());
    double dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
    if (dot < 0.0) {
        // 最短路径翻转：q 与 -q 表示同一旋转，翻转后插值走短弧（V-05
        // 大弧样例锁定该分支——Rz(350°)→Rz(10°) 经 0° 而非绕行 340°）。
        qb.x = -qb.x;
        qb.y = -qb.y;
        qb.z = -qb.z;
        qb.w = -qb.w;
        dot = -dot;
    }
    const double theta = safeAcos(dot);
    QuatComponents qo = qa;
    if (theta > 1e-12) {
        // 常规 slerp：θ 大于极小角阈值——sin(θ) 远离 0，公式稳定。
        const double sinTheta = std::sin(theta);
        const double wa = std::sin((1.0 - s) * theta) / sinTheta;
        const double wb = std::sin(s * theta) / sinTheta;
        qo.x = wa * qa.x + wb * qb.x;
        qo.y = wa * qa.y + wb * qb.y;
        qo.z = wa * qa.z + wb * qb.z;
        qo.w = wa * qa.w + wb * qb.w;
    } else {
        // 近共线分支：θ<1e-12 rad（约 5.7×10⁻¹¹°）时 sin(θ)≈θ 数值不稳，
        // 回退线性插值后归一化（数学极限一致——θ→0 时 slerp→nlerp）。
        qo.x = (1.0 - s) * qa.x + s * qb.x;
        qo.y = (1.0 - s) * qa.y + s * qb.y;
        qo.z = (1.0 - s) * qa.z + s * qb.z;
        qo.w = (1.0 - s) * qa.w + s * qb.w;
        const double len = std::sqrt(qo.x * qo.x + qo.y * qo.y + qo.z * qo.z + qo.w * qo.w);
        if (len > 0.0) {
            const double inv = 1.0 / len;
            qo.x *= inv;
            qo.y *= inv;
            qo.z *= inv;
            qo.w *= inv;
        }
    }

    rw::math::Transform3D<double> out;
    out.P() = p;
    out.R() = rw::math::Quaternion<double>(qo.x, qo.y, qo.z, qo.w).toRotation3D();
    return out;
}

// =====================================================================
// 采样计划（§8.3）
// =====================================================================

std::vector<double> cartesianSamplePlan(double lengthM, double stepM)
{
    // 前置：段长与步长均须有限且 >0（0 长笛卡尔段无 IK 语义——§8.2 起点
    // 生成本身要求 distanceM>0；step 来源于 config.trj 合法域校验）。
    if (!isFiniteDouble(lengthM) || lengthM <= 0.0) {
        throw TrajectoryError("trajectory/cartesian/sample-plan-length",
                              "笛卡尔段长须有限且 >0（m），实际 " + std::to_string(lengthM));
    }
    if (!isFiniteDouble(stepM) || stepM <= 0.0) {
        throw TrajectoryError("trajectory/cartesian/sample-plan-step",
                              "采样步长须有限且 >0（m），实际 " + std::to_string(stepM));
    }

    // 等步长等分：分段数 n=max(1, ceil(L/step))——n≥1 保证零除防御与
    // 退化短段（L≤step）至少两端点采样（端点必含——§8.3）；样本
    // s_i=i/n，相邻样本弧长 L/n ≤ step 恒成立。
    const double rawSegments = std::ceil(lengthM / stepM);
    const long long n = static_cast<long long>(std::max(1.0, rawSegments));
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(n) + 1U);
    for (long long i = 0; i <= n; ++i) {
        // s_i = i/n ∈[0,1]；末样本 i==n 时 s 精确置 1（避免浮点累加
        // 1.0000000000000002 之类的越界——端点还原精度的确定性保障）。
        samples.push_back(i == n ? 1.0 : static_cast<double>(i) / static_cast<double>(n));
    }
    return samples;
}

// =====================================================================
// 段规划（执行序见 CartesianLine.hpp 函数注）
// =====================================================================

CartesianLinePlanResult planCartesianLine(const CartesianLineRequest& request)
{
    // ---- 第 1 步：前置校验（调用方契约违约 → fail-fast，§15.2）----
    if (request.kinPort == nullptr) {
        throw TrajectoryError("trajectory/cartesian/kin-port",
                              "IK/FK 注入端口为空（§15.4 前置——端口缺失属"
                              "装配失败，不运行）");
    }
    if (!request.tcpRef.isValid()) {
        throw TrajectoryError("trajectory/cartesian/tcp-ref",
                              "tcpRef 必须为合法对象身份（§6.3——禁止名称"
                              "直存，ARC-04）");
    }
    if (!request.sourceTaskPoint.has_value() || !request.sourceTaskPoint->isValid()) {
        throw TrajectoryError("trajectory/cartesian/task-point-source",
                              "approach/retract 段恒归属任务点站——"
                              "sourceTaskPoint 必须为合法对象身份（§6.2）");
    }
    if (!isFiniteDouble(request.distanceM) || request.distanceM <= 0.0) {
        // §15.2 非法示例原文："distanceM≤0（输入非法）"——fail-fast。
        throw TrajectoryError("trajectory/cartesian/distance",
                              "段长 distanceM 须有限且 >0（m），实际 "
                                  + std::to_string(request.distanceM));
    }
    if (!isFiniteDouble(request.ikContinuityThreshold)
        || request.ikContinuityThreshold <= 0.0) {
        throw TrajectoryError("trajectory/cartesian/continuity-threshold",
                              "ikContinuityThreshold 须有限且 >0（rad|m），实际 "
                                  + std::to_string(request.ikContinuityThreshold));
    }
    if (request.conditionNumberWarning.has_value()
        && (!isFiniteDouble(*request.conditionNumberWarning)
            || *request.conditionNumberWarning <= 0.0)) {
        // policy 投影的合法域（无量纲 1；>0）——违约属装配面投影缺陷。
        throw TrajectoryError("trajectory/cartesian/condition-threshold",
                              "conditionNumberWarning 须有限且 >0（单位 1），实际 "
                                  + std::to_string(*request.conditionNumberWarning));
    }
    // workPose 数值核验：平移三分量有限＋旋转正交（构造点 fail-fast——
    // 非正交旋转无位姿语义）。
    const rw::math::Vector3D<double>& pWork = request.workPose.P();
    for (std::size_t i = 0; i < 3; ++i) {
        if (!isFiniteDouble(pWork[i])) {
            throw TrajectoryError("trajectory/cartesian/workpose-nonfinite",
                                  "任务点位姿平移分量非有限（分量 "
                                      + std::to_string(i) + "）");
        }
    }
    if (!isOrthogonal(request.workPose.R())) {
        throw TrajectoryError("trajectory/cartesian/workpose-rotation",
                              "任务点位姿旋转部非正交（残差 >1×10⁻⁹——"
                              "无位姿语义，调用方数据违约）");
    }
    // 轴向解析产物的单源纪律（§8.2）：ToolZ 域内解析（workPose 旋转部
    // 第三列）；ReferenceZ 须调用方注入基座系单位向量，双源并存即违约。
    if (request.axis == SequenceSegmentAxis::ToolZ
        && request.referenceAxisDirectionInBase.has_value()) {
        throw TrajectoryError("trajectory/cartesian/axis-dual-source",
                              "axis==ToolZ 时轴向由 workPose 旋转部域内解析，"
                              "不得另携 referenceAxisDirectionInBase（轴向"
                              "单源纪律——防双源不一致）");
    }
    rw::math::Vector3D<double> axisDir(0.0, 0.0, 0.0);
    if (request.axis == SequenceSegmentAxis::ToolZ) {
        // 工具系 Z 轴换算至基座系：z_B = R_work · (0,0,1)＝R 的第三列
        // （§8.2——"工具系 Z 轴方向经 T_end（或 T_start）旋转部换算至
        // 基座系"；T_end（approach）/T_start（retract）同为 workPose，
        // 单源取 workPose——实现决策登记见文件头注）。
        const rw::math::Rotation3D<double>& R = request.workPose.R();
        axisDir = rw::math::Vector3D<double>(R(0, 2), R(1, 2), R(2, 2));
    } else {
        if (!request.referenceAxisDirectionInBase.has_value()) {
            // §15.2 非法示例原文："axis 对象未在闭包（输入非法）"——
            // ReferenceZ 的解析产物缺失即请求面落点。
            throw TrajectoryError("trajectory/cartesian/axis-missing",
                                  "axis==ReferenceZ 时 referenceAxisDirection"
                                  "InBase 必填（frameRef Z 轴的基座系解析"
                                  "产物——调用方未注入）");
        }
        axisDir = *request.referenceAxisDirectionInBase;
        for (std::size_t i = 0; i < 3; ++i) {
            if (!isFiniteDouble(axisDir[i])) {
                throw TrajectoryError("trajectory/cartesian/axis-nonfinite",
                                      "参考轴方向分量非有限（分量 "
                                          + std::to_string(i) + "）");
            }
        }
        // 单位向量核验（|v| 偏差 ≤1×10⁻⁹——无量纲；非单位向量会静默
        // 改变段长语义，拒绝不归一化——NFR-COR-03）。
        const double norm = std::sqrt(axisDir[0] * axisDir[0]
                                      + axisDir[1] * axisDir[1]
                                      + axisDir[2] * axisDir[2]);
        if (std::fabs(norm - 1.0) > 1e-9) {
            throw TrajectoryError("trajectory/cartesian/axis-not-unit",
                                  "参考轴方向须为单位向量（|v| 偏差 ≤1×10⁻⁹），"
                                  "实际 |v|=" + std::to_string(norm));
        }
    }
    // 分支种子构型：全分量有限（构型游标——§8.3 首采样分支延续参照）。
    for (std::size_t i = 0; i < static_cast<std::size_t>(request.branchSeedQ.size()); ++i) {
        if (!isFiniteDouble(request.branchSeedQ[i])) {
            throw TrajectoryError("trajectory/cartesian/seed-nonfinite",
                                  "branchSeedQ 含非有限分量（轴 "
                                      + std::to_string(i) + "）");
        }
    }
    const std::size_t dof = static_cast<std::size_t>(request.branchSeedQ.size());
    if (dof == 0U) {
        throw TrajectoryError("trajectory/cartesian/empty-seed",
                              "branchSeedQ 维度为 0（无自由度链无 IK 语义）");
    }
    // 评价区间投影核验（同维度＋逐轴有限＋lower<upper——限位硬过滤②的
    // 判定基准；违约＝调用方投影缺陷 fail-fast）。
    if (static_cast<std::size_t>(request.lowerBoundQ.size()) != dof
        || static_cast<std::size_t>(request.upperBoundQ.size()) != dof) {
        throw TrajectoryError("trajectory/cartesian/bounds-dim",
                              "评价区间维度与种子链不一致（lower="
                                  + std::to_string(request.lowerBoundQ.size())
                                  + "，upper="
                                  + std::to_string(request.upperBoundQ.size())
                                  + "，seed=" + std::to_string(dof) + "）");
    }
    for (std::size_t i = 0; i < dof; ++i) {
        const double lo = request.lowerBoundQ[i];
        const double hi = request.upperBoundQ[i];
        if (!isFiniteDouble(lo) || !isFiniteDouble(hi) || !(lo < hi)) {
            throw TrajectoryError("trajectory/cartesian/bounds-order",
                                  "评价区间须逐轴有限且 lower<upper（轴 "
                                      + std::to_string(i) + "）");
        }
    }
    if (request.planningSeed == 0U) {
        // 种子 0 非法（I-KIN-4 同款拒绝——确定性复现要素不设缺省替换）。
        throw TrajectoryError("trajectory/cartesian/seed-zero",
                              "planningSeed 须 >0（0 非法——确定性复现要素，"
                              "I-KIN-4 同款拒绝）");
    }

    // ---- 第 2 步：取消轮询（入口一次——§15.1 循环边界纪律；取消不是
    // 错误：零素材零诊断，UX-03/§6.7）。
    if (request.cancel && request.cancel()) {
        CartesianLinePlanResult out;
        out.status = CartesianLineStatus::Canceled;
        return out;
    }

    // ---- 第 3 步：几何构造（§8.2 起点生成）----
    // approach：沿轴负向回退 distanceM 生成 T_start（接近点），段终点＝
    // 任务点位姿；retract：沿轴正向外推 distanceM 生成 T_end（撤离点），
    // 段起点＝任务点位姿（REQ-02 方向语义——approach 沿轴负向趋近、
    // retract 沿轴正向离开；本域只做几何解释）。两端旋转部相同（纯平移
    // 生成）——slerp 在段内退化为恒等，接口仍按通用直线段插值。
    rw::math::Transform3D<double> tStart = request.workPose;
    rw::math::Transform3D<double> tEnd = request.workPose;
    switch (request.role) {
    case CartesianLineRole::Approach:
        tStart.P() = pWork - request.distanceM * axisDir;
        break;
    case CartesianLineRole::Retract:
        tEnd.P() = pWork + request.distanceM * axisDir;
        break;
    default:
        // 枚举封闭词表防御（位型越界值拒绝——解码面防御同款纪律）。
        throw TrajectoryError("trajectory/cartesian/role",
                              "段角色词表值非法（Approach/Retract 二值外）");
    }

    // ---- 第 4 步：采样计划（§8.3——端点必含；步长上限来自段约束的
    // config.trj 投影）。
    const std::vector<double> samples = cartesianSamplePlan(
        request.distanceM, request.constraint.cartesianSampleStep);
    const std::size_t nSamples = samples.size();
    const double stepActualM = request.distanceM / static_cast<double>(nSamples - 1U);

    // IK 端口请求骨架（字段透传口径见文件头注实现决策：容差/去重阈值随
    // IkPortRequest 缺省——kin.task-point-ik 契约缺省，零第二字面量；
    // 限位区间＝评价区间投影逐轴拷贝（限位硬过滤②）；种子＝规划种子透传）。
    IkPortRequest ikReq;
    ikReq.lowerBoundQ.resize(dof);
    ikReq.upperBoundQ.resize(dof);
    for (std::size_t i = 0; i < dof; ++i) {
        ikReq.lowerBoundQ[i] = request.lowerBoundQ[i];
        ikReq.upperBoundQ[i] = request.upperBoundQ[i];
    }
    ikReq.seed = request.planningSeed;

    // 逐采样状态：解集＋当前分支解（§8.3"跟踪当前分支取链式一致解"）。
    std::vector<std::vector<PtpCandidate>> solutionsPerSample(nSamples);
    std::vector<std::vector<double>> branchQPerSample(nSamples);
    IkContinuityRecord continuity;
    continuity.threshold = request.ikContinuityThreshold;
    continuity.sampleStepActualM = stepActualM;
    continuity.allContinuous = false;
    std::vector<SingularNeighborhoodWarning> singularWarnings;

    // 素材构造器（BranchJump/SampleUnreachable 共用壳——phase/序号/
    // 定位逐例填）。
    const auto makeFailure = [&](const std::string& reasonToken, double s,
                                 const std::string& cause,
                                 const std::string& action) {
        FailedSegmentRecord rec;
        rec.segmentIndex = request.segmentIndex;
        rec.phaseToken = kPhasePlanLine;
        rec.reasonToken = reasonToken;
        rec.pathParameter = s;
        rec.cause = cause;
        rec.recommendedAction = action;
        return rec;
    };

    for (std::size_t i = 0; i < nSamples; ++i) {
        // ---- 第 5 步：逐采样 IK 端口消费（§8.3——trajectory 不实现 IK）。
        // 取消轮询（逐采样循环边界——§15.1）。
        if (request.cancel && request.cancel()) {
            CartesianLinePlanResult out;
            out.status = CartesianLineStatus::Canceled;
            return out;
        }

        // 采样点目标位姿（§8.3 采样计划——插值口径见 interpolateCartesianLine）。
        ikReq.targetPose = interpolateCartesianLine(tStart, tEnd, samples[i]);

        // 首次求解：初值数取端口缺省（8——JointGrid 确定性策略）。
        ikReq.maxInitialValues = 8U;
        IkPortReply reply = request.kinPort->solveIk(ikReq);

        // 端口壳结局分流（§15.4——PortError 显性失败不吞错；Canceled
        // 非错误零素材）。
        if (reply.status == KinPortCallStatus::PortError) {
            // 端口层错误＝环境故障（适配失败/契约不匹配）——按 §15.0
            // 以原 token 显性抛出（不转素材：素材轨只承载用户数据的
            // 合法失败结局，基础设施故障必须显性失败）。
            throw TrajectoryError(reply.errorToken, reply.errorMessage);
        }
        if (reply.status == KinPortCallStatus::Canceled
            || (request.cancel && request.cancel())) {
            CartesianLinePlanResult out;
            out.status = CartesianLineStatus::Canceled;
            return out;
        }

        // §8.4 单点失败：解集空且结局∈{2,3}（搜索未果口径）→初值策略
        // 扩充（4 倍重试一次）；结局 5（解析界限）不重试——确定性界限
        // 与初值无关（文件头注实现决策）。
        if (reply.result.solutions.empty()
            && reply.result.outcome != IkPortOutcome::AnalyticBoundExceeded) {
            ikReq.maxInitialValues = 32U;
            reply = request.kinPort->solveIk(ikReq);
            if (reply.status == KinPortCallStatus::PortError) {
                throw TrajectoryError(reply.errorToken, reply.errorMessage);
            }
            if (reply.status == KinPortCallStatus::Canceled
                || (request.cancel && request.cancel())) {
                CartesianLinePlanResult out;
                out.status = CartesianLineStatus::Canceled;
                return out;
            }
        }

        if (reply.result.solutions.empty()) {
            // ---- §8.4 段失败定位：采样点全部解被过滤/未收敛——素材附
            // s、已试初值数、过滤原因分布；**不判不可行**（搜索未果口径
            // ——REQUIREMENTS §8.1 C5/C8；DataInsufficient 判定归 evidence）。
            std::size_t nResidual = 0;
            std::size_t nJointLimit = 0;
            std::size_t nCollision = 0;
            for (const IkPortFilterRecord& f : reply.result.filtered) {
                switch (f.reason) {
                case IkPortFilterReason::ResidualRecheck: ++nResidual; break;
                case IkPortFilterReason::JointLimit:      ++nJointLimit; break;
                case IkPortFilterReason::Collision:       ++nCollision; break;
                }
            }
            CartesianLinePlanResult out;
            out.status = CartesianLineStatus::SampleUnreachable;
            out.failure = makeFailure(
                std::string(kTrjNoPath), samples[i],
                "笛卡尔段采样点 s=" + std::to_string(samples[i])
                    + " 处 IK 无可用解（结局码 "
                    + std::to_string(static_cast<unsigned>(reply.result.outcome))
                    + "；已试初值 "
                    + std::to_string(reply.result.attemptedInitialValues)
                    + " 个；过滤原因分布——残差 " + std::to_string(nResidual)
                    + "、限位 " + std::to_string(nJointLimit) + "、碰撞 "
                    + std::to_string(nCollision) + "）——搜索未找到有效解，"
                    "不构成任务不可行结论",
                "扩大初值策略/数量或迭代预算后按同一冻结输入复评"
                "（KIN-13）；或调整该采样点可达性（任务点/段长/限位视图）");
            return out;
        }

        // 解集投影（稳定排序视图序透传——三键同名同义；维度核验：解与
        // 种子同链，维度不一致＝端口数据违约 fail-fast）。
        std::vector<PtpCandidate> cands;
        cands.reserve(reply.result.solutions.size());
        for (const IkPortSolution& sol : reply.result.solutions) {
            if (sol.q.size() != dof) {
                throw TrajectoryError(std::string(kKinPortErrorPrefix) + "solution-dim",
                                      "端口解维度与分支种子不一致（解 q="
                                          + std::to_string(sol.q.size())
                                          + "，seed=" + std::to_string(dof)
                                          + "——端口数据违约）");
            }
            cands.push_back(toPtpCandidate(sol));
        }
        solutionsPerSample[i] = std::move(cands);

        // ---- 第 6 步：分支跟踪（§8.3 链式一致解）。
        std::vector<double> branchQ;
        double maxAxisDelta = 0.0;
        bool branchContinuous = true;
        std::uint32_t branchIdx = 0;
        if (i == 0U) {
            // 首采样点：分支取 PTP 构型选择规则延续（§8.3 原文——三键
            // 全序 choosePtpCandidate；参照点＝branchSeedQ 构型游标）。
            const std::size_t chosen = choosePtpCandidate(
                request.branchSeedQ, solutionsPerSample[i],
                request.ikContinuityThreshold);
            branchIdx = static_cast<std::uint32_t>(chosen);
        } else {
            // 后续采样：在解集中找与当前分支解逐轴 |Δq| ≤ 阈值的延续解
            // ——可能多个时取逐轴偏差和最小者，并列取 stableIndex 升序
            // （确定性全序——NFR-COR-02；IK 去重保证解间距离远大于常规
            // 连续阈值，多解同阈值属奇异邻域邻接的工程场景，全序消解）。
            const std::vector<double>& prevQ = branchQPerSample[i - 1U];
            bool found = false;
            double bestSum = 0.0;
            for (std::size_t k = 0; k < solutionsPerSample[i].size(); ++k) {
                const rw::math::Q& q = solutionsPerSample[i][k].q;
                double maxDelta = 0.0;
                double sumDelta = 0.0;
                for (std::size_t j = 0; j < dof; ++j) {
                    const double d = std::fabs(q[static_cast<int>(j)] - prevQ[j]);
                    maxDelta = std::max(maxDelta, d);
                    sumDelta += d;
                }
                if (maxDelta > request.ikContinuityThreshold) {
                    continue;  // 逐轴上界判定——任一轴超阈值即不延续
                }
                if (!found || sumDelta < bestSum
                    || (sumDelta == bestSum
                        && solutionsPerSample[i][k].stableIndex
                               < solutionsPerSample[i][branchIdx].stableIndex)) {
                    found = true;
                    bestSum = sumDelta;
                    maxAxisDelta = maxDelta;
                    branchIdx = static_cast<std::uint32_t>(k);
                }
            }
            if (!found) {
                // ---- 分支断裂（§8.3）：TRJ-BRANCH-JUMP 素材（附采样点
                // s 与两端解摘要——当前分支解与断裂样本解集首解）；
                // 跳变前样本的连续性记录完整回带（可回放定位）。
                IkContinuitySampleRecord row;
                row.s = samples[i];
                row.solutionCount =
                    static_cast<std::uint32_t>(solutionsPerSample[i].size());
                row.branchSolutionIndex = 0;
                row.maxAxisDelta = 0.0;
                row.branchContinuous = false;
                continuity.samples.push_back(row);
                // allContinuous 维持 false（断裂——§15.2"Ok 时逐采样分支
                // 连续记录完整"不成立）。
                continuity.allContinuous = false;

                CartesianLinePlanResult out;
                out.status = CartesianLineStatus::BranchJump;
                out.continuity = std::move(continuity);
                out.singularWarnings = std::move(singularWarnings);
                const std::vector<double> brokenQ =
                    solutionsPerSample[i].empty()
                        ? std::vector<double>{}
                        : solutionsPerSample[i].front().q.toStdVector();
                const std::vector<double> prevQv = branchQPerSample[i - 1U];
                out.failure = makeFailure(
                    std::string(kTrjBranchJump), samples[i],
                    "笛卡尔段采样点 s=" + std::to_string(samples[i])
                        + " 处 IK 分支断裂：当前分支解 " + qSummary(prevQv)
                        + " 与该采样点解集（" + std::to_string(row.solutionCount)
                        + " 解，首解 " + qSummary(brokenQ) + "）无逐轴偏差 ≤"
                        + std::to_string(request.ikContinuityThreshold)
                        + "（rad|m）的延续解——关节分支跳变",
                    "检查该段可达构型分布（增大连续阈值属配置变更——须重评"
                    "身份）；或经避障/重规划衔接（§10）后重评");
                return out;
            }
            branchContinuous = true;
        }
        branchQ.resize(dof);
        for (std::size_t j = 0; j < dof; ++j) {
            branchQ[j] = solutionsPerSample[i][branchIdx].q[j];
        }
        branchQPerSample[i] = branchQ;

        // ---- 第 7 步：奇异邻域判定（§8.5——阈值有值才消费 FK；nullopt
        // ＝检查显式不适用，不计缺失，P-POL-2；warning 不阻断）。
        if (request.conditionNumberWarning.has_value()) {
            FkPortRequest fkReq;
            fkReq.q = branchQ;
            fkReq.cancel = request.cancel;
            FkPortReply fkReply = request.kinPort->evaluateFk(fkReq);
            if (fkReply.status == KinPortCallStatus::PortError) {
                // 端口层错误同 IK 侧——显性失败不吞错。
                throw TrajectoryError(fkReply.errorToken, fkReply.errorMessage);
            }
            if (fkReply.status == KinPortCallStatus::Canceled
                || (request.cancel && request.cancel())) {
                CartesianLinePlanResult out;
                out.status = CartesianLineStatus::Canceled;
                return out;
            }
            // 命中判定：条件数超阈值，或实测 +∞（conditionNumberIsFinite
            // ==false——不静默截断，D-KIN-2 透传）。
            const bool hit = !fkReply.metrics.conditionNumberIsFinite
                          || (fkReply.metrics.conditionNumber
                              > *request.conditionNumberWarning);
            if (hit) {
                SingularNeighborhoodWarning warn;
                warn.s = samples[i];
                warn.conditionNumber = fkReply.metrics.conditionNumber;
                warn.conditionNumberIsFinite = fkReply.metrics.conditionNumberIsFinite;
                warn.threshold = *request.conditionNumberWarning;
                warn.cause = "笛卡尔段采样点 s=" + std::to_string(samples[i])
                           + " 处条件数 "
                           + (fkReply.metrics.conditionNumberIsFinite
                                  ? std::to_string(fkReply.metrics.conditionNumber)
                                  : std::string("+∞（奇异）"))
                           + " 超过策略阈值 "
                           + std::to_string(*request.conditionNumberWarning)
                           + "（单位 1）——奇异邻域工程警告，不阻断段规划";
                warn.recommendedAction =
                    "复核该区段工艺可行性（奇异邻域附近关节速度可能放大）"
                    "——或调整任务点/段长避开邻域";
                warn.comparison = singularComparison(
                    fkReply.metrics.conditionNumber, *request.conditionNumberWarning);
                singularWarnings.push_back(std::move(warn));
            }
        }

        // 逐采样连续性记录（§8.3 证据绑定字段：解索引/逐轴偏差/判定）。
        IkContinuitySampleRecord row;
        row.s = samples[i];
        row.solutionCount = static_cast<std::uint32_t>(solutionsPerSample[i].size());
        row.branchSolutionIndex = branchIdx;
        row.maxAxisDelta = maxAxisDelta;
        row.branchContinuous = branchContinuous;
        continuity.samples.push_back(row);
    }

    // ---- 第 8 步：几何产物组装（CartesianLine 段——端点路点 kind 按
    // 角色：任务点端 TaskPoint 带来源、生成端 Via；§6.2 双域纪律——
    // 全路点携 target 不携 q）。
    TrajectorySegment segment;
    segment.segmentIndex = request.segmentIndex;
    segment.spaceType = SegmentSpaceType::CartesianLine;
    segment.tcpRef = request.tcpRef;
    segment.frameRef = request.frameRef;
    segment.constraint = request.constraint;
    segment.sourceTaskPoint = request.sourceTaskPoint;

    // 生成端/任务点端按角色定位（§8.2：approach 起点＝回退生成点、终点
    // ＝任务点；retract 起点＝任务点、终点＝外推生成点）。
    const bool approach = request.role == CartesianLineRole::Approach;
    const rw::math::Transform3D<double>& tGenerated = approach ? tStart : tEnd;
    const rw::math::Transform3D<double>& tWorkSide = request.workPose;

    Waypoint wpGenerated;
    wpGenerated.kind = WaypointKind::Via;  // 接近点/撤离点——规划引入的
                                           // 非必经状态（§6.2 WaypointKind）
    wpGenerated.segmentIndex = request.segmentIndex;
    wpGenerated.target = tGenerated;

    Waypoint wpWork;
    wpWork.kind = WaypointKind::TaskPoint;
    wpWork.segmentIndex = request.segmentIndex;
    wpWork.target = tWorkSide;
    wpWork.sourceTaskPoint = request.sourceTaskPoint;  // 可追溯（NFR-COR-04）

    if (approach) {
        segment.waypoints.push_back(std::move(wpGenerated));
        segment.waypoints.push_back(std::move(wpWork));
    } else {
        segment.waypoints.push_back(std::move(wpWork));
        segment.waypoints.push_back(std::move(wpGenerated));
    }

    // TCP 参考点路径长度＝段长（m——几何定义值：位置沿直线匀比，姿态
    // 变化不计入参考点弧长；§6.2 pathLengthTcp 单位 m。与单元卡
    // "CartesianLine 段由 T05 填写"口径一致——JointLinear 段的 TCP 长度
    // 才是 FK 派生观察，随 T08）。
    segment.pathLengthTcp = request.distanceM;
    // pathLengthJoint 恒 0：笛卡尔段无关节路径长度语义（逐采样构型是
    // IK 解不是路径几何——不冒充关节路径长度）。

    // ---- 第 9 步：结构守卫（段构造保证的自证——失败即实现缺陷）。
    validateTrajectorySegment(segment);

    continuity.allContinuous = true;  // 全采样通过——§15.2 后置成立
    CartesianLinePlanResult out;
    out.status = CartesianLineStatus::Ok;
    out.segment = std::move(segment);
    out.continuity = std::move(continuity);
    out.singularWarnings = std::move(singularWarnings);
    return out;
}

}  // namespace sdurws::ird::trajectory
