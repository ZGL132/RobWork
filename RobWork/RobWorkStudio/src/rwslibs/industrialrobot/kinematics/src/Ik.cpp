/**
 * @file   Ik.cpp
 * @brief  多初值 IK 求解实现（KIN-02）——请求校验、初值策略生成、阻尼
 *         最小二乘迭代、解析界限检查与五类结局组装（§5.3 时序）；FK 复算
 *         复算失败走求解设施失败值面（F-746③——非终局形态，不抛）。
 *         ★ KIN-P2（审查 A02）：迭代与收敛判据任务维度感知——IkTaskSpace
 *         PositionOnly（仅位置任务）只优化 3 维位置误差及 Jacobian 位置
 *         子块（姿态不进目标函数），FullPose（完整位姿）保持 v1 行为
 *         逐位不变；求解器契约版本随行为集扩展升 v2（CON-04）。
 *
 * 设计依据（契约面见 Ik.hpp 文件头）：units/kinematics.md §5.3/§5.4/
 * §6.1/§6.3/§9.2/§3.4；治理登记 O-40（Eigen PRIVATE 仅实现文件——本
 * 翻译单元与 Fk.cpp 同为 Eigen 消费点）；任务契约 WP-15-T04
 * acceptance 1/2/3/4。
 *
 * 实现纪律（两模式可链接＋确定性——Fk.cpp 同款登记）：
 *   - rw::math 值类型仅头内 inline 面；旋转/线性代数在 Eigen 组件域，
 *     出界前一次性转回；
 *   - 全部浮点运算定序固定（逐初值串行、单线程、无并行归约、无时钟/
 *     环境依赖）——同请求同字节输出（NFR-COR-01）；
 *   - 取消探针的调用不影响任何数值结果（只决定提前返回——确定性不被
 *     探针时机破坏）；
 *   - 本单元对 RW 雅可比/SVD 零第二实现——FK 复算唯一经 FkEvaluator
 *     （计算面单一实现点；Evaluators.cpp 同款口径）。
 */

#include <sdurws/ird/kinematics/Ik.hpp>

#include <sdurws/ird/kinematics/Fk.hpp>          // FkEvaluator/PoseMetrics——FK 复算唯一实现点
#include <sdurws/ird/kinematics/SolutionSet.hpp>  // sortSolutions——§6.3 四键排序唯一实现点

#include <Eigen/Core>
#include <Eigen/Cholesky>  // LLT——DLS 正规方程 (JJᵀ＋λ²I) 求解（O-40 Eigen PRIVATE）

#include <cmath>
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 请求校验（fail-fast 轨——IIkSolver 类注错误面；§9.1"调用方错误
// fail-fast"；NFR-COR-03 不钳制不置零）
// =====================================================================

/// 调用方契约违约出口（std::invalid_argument——不进入结果对象）。
[[noreturn]] void failCaller(const std::string& what)
{
    throw std::invalid_argument(what);
}

/// 组装"求解设施失败"值面返回（F-746③——三处 FK 复算失败共用的单一形态
/// 组装点；语义口径见 Ik.hpp IkFacilityFailure 注：与 cancelled 同族的
/// 非终局形态——设施失败不是结局，不虚构任何搜索结局，D-KIN-7 词表不扩）。
IkOutcome makeFacilityFailure(IkFacilityFailureStage stage, const IkRequest& request,
                              const IkSearchRecord& partialSearch, std::string detail)
{
    IkOutcome out;
    // cancelled=false、outcomeKind 保持默认——两者都无终局语义（调用方
    // 解读序：先 cancelled、再 facilityFailure、最后才允许解读 outcomeKind
    // ——IkOutcome 注）。
    out.solutionSet.targetRef = request.targetRef;
    out.solutionSet.requestIdentity = request.requestIdentity;
    // 部分搜索统计如实交付（素材不吞错——NFR-COR-03）：iterationsPerInit
    // 仅含已完成初值的落账，进行中初值不补记（其迭代次数未计入）。
    out.solutionSet.searchRecord = partialSearch;
    out.facilityFailure = IkFacilityFailure{stage, std::move(detail)};
    return out;
}

/// 数值全有限校验（逐分量；返回首个违例下标，全有限返回 nullopt）。
std::optional<std::size_t> firstNonFinite(const std::vector<double>& v)
{
    for (std::size_t k = 0; k < v.size(); ++k) {
        if (!std::isfinite(v[k])) {
            return k;
        }
    }
    return std::nullopt;
}

/// 目标位姿 12 个分量（R 9＋p 3）全有限校验（违例＝KIN-TARGET-ILLEGAL
 /// "目标非法：非有限"——§5.5）。
bool targetAllFinite(const rw::math::Transform3D<double>& t)
{
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            if (!std::isfinite(t.R()(i, j))) {
                return false;
            }
        }
        if (!std::isfinite(t.P()[i])) {
            return false;
        }
    }
    return true;
}

// =====================================================================
// TCP 解析（KinTypes.hpp 解析规则的 Ik 侧执行面——Fk.cpp/Bounds.cpp 同
// 规则；单一语义以服务解耦为界，黄金算例两侧互证——Evaluators.cpp 先例）
// =====================================================================

/// TCP 未配置/悬空（§9.6 KIN-NO-TCP 两分语义）——消息锚见 resolveTcpOrThrow。

/// 解析 TCP 引用（空键＝默认TCP；非空先命中原tcpKey再允许localName旧别名——违例
/// FrameUnresolved）。求解器 fail-fast 轨：解析失败以 invalid_argument
/// 抛出（消息携带 KIN-NO-TCP/FrameUnresolved 语义锚——§5.5 错误组）。
const runtime::CanonicalTool& resolveTcpOrThrow(const IKinRuntimeView& view,
                                                const TcpRef& tcp)
{
    const runtime::CanonicalModel& model = view.model();
    const std::vector<runtime::CanonicalTool>& tools = model.tools();
    if (tools.empty()) {
        failCaller("IkRequest.tcp：TCP 未配置——快照模型无工具"
                   "（KIN-NO-TCP 语义锚——§5.5/§9.6；调用方错误 fail-fast）");
    }
    const auto loc = model.findObject(tcp.toolObject);
    if (!loc.has_value() || loc->kind != runtime::CanonicalModel::ObjectKind::Tool) {
        failCaller("IkRequest.tcp：TCP 引用悬空——toolObject 未解析到快照工具"
                   "（KIN-NO-TCP 语义锚——§5.5/§9.6；调用方错误 fail-fast）");
    }
    const runtime::CanonicalTool& tool = tools.at(loc->index);
    // A-15：只验证同一快照的键；实际变换由FK和解析界限各自消费，不在UI造真值。
    const auto named = std::find_if(tool.namedTcpOffsets.begin(), tool.namedTcpOffsets.end(),
        [&](const auto& entry) { return entry.first == tcp.tcpKey; });
    if (!tcp.tcpKey.empty() && named == tool.namedTcpOffsets.end() && tcp.tcpKey != tool.localName) {
        failCaller("IkRequest.tcp：tcpKey '" + tcp.tcpKey + "' 不命中工具 '"
                   + tool.localName + "' 的 canonical TCP"
                   "（FrameUnresolved 语义锚——§9.2；调用方错误 fail-fast）");
    }
    return tool;
}

// =====================================================================
// 几何/线性代数（Eigen 组件域——两模式可链接）
// =====================================================================

/// 6 维误差向量 e＝[p_target−p_tcp（m）; rotvec(R_t·R_tcpᵀ)（rad）]。
/// 姿态误差取旋转矩阵对数映射的旋转向量（Eigen AngleAxis 标准算法——
/// 近 0/π 稳定）；返回值同时给出位置/姿态残差模长（比较型量——§5.5）。
Eigen::Matrix<double, 6, 1> poseError(const rw::math::Transform3D<double>& target,
                                      const rw::math::Transform3D<double>& tcpInBase,
                                      double& posResidualOut,
                                      double& oriResidualOut)
{
    Eigen::Matrix<double, 6, 1> e;
    // 位置误差（m——基座系逐分量差后取模）。
    const double dx = target.P()[0] - tcpInBase.P()[0];
    const double dy = target.P()[1] - tcpInBase.P()[1];
    const double dz = target.P()[2] - tcpInBase.P()[2];
    e(0) = dx;
    e(1) = dy;
    e(2) = dz;
    posResidualOut = std::sqrt(dx * dx + dy * dy + dz * dz);

    // 姿态误差（rad）：R_err＝R_target·R_tcpᵀ 的旋转向量——模长即转角。
    Eigen::Matrix3d rTcp;
    Eigen::Matrix3d rTarget;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            rTcp(i, j) = tcpInBase.R()(static_cast<std::size_t>(i),
                                       static_cast<std::size_t>(j));
            rTarget(i, j) = target.R()(static_cast<std::size_t>(i),
                                       static_cast<std::size_t>(j));
        }
    }
    const Eigen::AngleAxisd aa(rTarget * rTcp.transpose());
    const Eigen::Vector3d rotvec = aa.axis() * aa.angle();
    e(3) = rotvec(0);
    e(4) = rotvec(1);
    e(5) = rotvec(2);
    oriResidualOut = aa.angle() < 0.0 ? -aa.angle() : aa.angle();
    return e;
}

/**
 * @brief Z-Y-X 欧拉提取（参考系内 roll/pitch/yaw，rad——残差与欧拉率
 *        Jacobian 共用的单一真值，批34 提升为文件级）。
 *
 * ★ 批34（评审 F3）：万向锁 ±90° 两分支同号——ZYX 提取在 cp=0 时
 * r(0,1)/r(1,1)＝sin/cos(r∓y)（+90°为 r−y、−90°为 r+y），roll 固定 0
 * 后两者都给出 yaw＝−atan2(r(0,1), r(1,1))；旧实现按 sinPitch 翻号使
 * +90° 分支 yaw 反号（评审反例"正90°俯仰分支的yaw符号错误"）。
 */
std::array<double, 3> zyxEulerAngles(const rw::math::Rotation3D<double>& r)
{
    constexpr double kGimbalEpsilon = 1e-5;
    constexpr double kHalfPi = 1.57079632679489661923;
    const double cosPitch = std::sqrt(r(0, 0) * r(0, 0) + r(1, 0) * r(1, 0));
    const double sinPitch = -r(2, 0);
    std::array<double, 3> a{}; // roll(X), pitch(Y), yaw(Z)，单位 rad
    if (cosPitch < kGimbalEpsilon) {
        // 万向锁时固定 roll=0，yaw 从剩余矩阵项确定（两分支同号——见上注）。
        a[0] = 0.0;
        a[1] = sinPitch < 0.0 ? -kHalfPi : kHalfPi;
        a[2] = -std::atan2(r(0, 1), r(1, 1));
    } else {
        a[1] = std::atan2(sinPitch, cosPitch);
        a[0] = std::atan2(r(2, 1), r(2, 2));
        a[2] = std::atan2(r(1, 0), r(0, 0));
    }
    return a;
}

/// Z-Y-Z 欧拉提取（R＝Rz(α)Ry(β)Rz(γ)，β∈[0,π]——工具扭转族分解专用；
/// 批35 P1-1/P1-4：γ＝绕目标 z 的工具扭转角，β≈0 简并时 γ≡α+γ 取和）。
std::array<double, 3> zyzEulerAngles(const rw::math::Rotation3D<double>& R)
{
    constexpr double kSingularEpsilon = 1e-10;
    const double sinBeta = std::sqrt(R(0, 2) * R(0, 2) + R(1, 2) * R(1, 2));
    const double cosBeta = R(2, 2);
    std::array<double, 3> a{};  // (α, β, γ)
    if (sinBeta < kSingularEpsilon) {
        // β≈0 简并：仅 α+γ 可观（R10/R00＝sin/cos(α+γ)）——扭转语义不变。
        a[0] = 0.0;
        a[1] = cosBeta >= 0.0 ? 0.0 : 3.14159265358979323846;
        a[2] = std::atan2(R(1, 0), R(0, 0));
    } else {
        a[0] = std::atan2(R(1, 2), R(0, 2));
        a[1] = std::atan2(sinBeta, cosBeta);
        a[2] = std::atan2(R(2, 1), -R(2, 0));
    }
    return a;
}

/// 跨周最短角差（wrap 到 (−π,π]——残差与区间偏移共用）。
double wrapAngle(double angle)
{
    return std::atan2(std::sin(angle), std::cos(angle));
}

/// 参考系转置（Rᵀ——基座系→参考系的旋转算子；批34 提升共用）。
rw::math::Rotation3D<double> transposeOf(const rw::math::Rotation3D<double>& r)
{
    return rw::math::Rotation3D<double>(
        r(0, 0), r(1, 0), r(2, 0),
        r(0, 1), r(1, 1), r(2, 1),
        r(0, 2), r(1, 2), r(2, 2));
}

/// 逐元素 3×3 乘（out＝a·b——冒烟纪律内联实现，A-16 注释同源）。
rw::math::Rotation3D<double> composeOf(const rw::math::Rotation3D<double>& a,
                                       const rw::math::Rotation3D<double>& b)
{
    return rw::math::Rotation3D<double>(
        a(0, 0) * b(0, 0) + a(0, 1) * b(1, 0) + a(0, 2) * b(2, 0),
        a(0, 0) * b(0, 1) + a(0, 1) * b(1, 1) + a(0, 2) * b(2, 1),
        a(0, 0) * b(0, 2) + a(0, 1) * b(1, 2) + a(0, 2) * b(2, 2),
        a(1, 0) * b(0, 0) + a(1, 1) * b(1, 0) + a(1, 2) * b(2, 0),
        a(1, 0) * b(0, 1) + a(1, 1) * b(1, 1) + a(1, 2) * b(2, 1),
        a(1, 0) * b(0, 2) + a(1, 1) * b(1, 2) + a(1, 2) * b(2, 2),
        a(2, 0) * b(0, 0) + a(2, 1) * b(1, 0) + a(2, 2) * b(2, 0),
        a(2, 0) * b(0, 1) + a(2, 1) * b(1, 1) + a(2, 2) * b(2, 1),
        a(2, 0) * b(0, 2) + a(2, 1) * b(1, 2) + a(2, 2) * b(2, 2));
}

/**
 * @brief 按任务参考系和六分量掩码构造真实约束残差。
 *
 * 位置残差先由基座系转到referenceInBase系；姿态误差采用该参考系内
 * Z-Y-X 欧拉分量的最短角差。allowToolRoll允许绕参考系+Z的滚转分量自由，
 * 避免把规则错误降级为固定姿态。返回的六行仍按x/y/z/roll/pitch/yaw固定排序。
 */
namespace {

/// 参考系姿态误差统一出口：target 系轴角向量 → 参考系（R_ref_target =
/// rrᵀ·R_target——与 DLS 角速度行同系，批35 P1-1/P1-4 的目标族残差与测
/// 地迭代误差共用此映射）。
Eigen::Matrix<double, 6, 1> attitudeErrorInReference(
    const rw::math::Rotation3D<double>& rr,
    const rw::math::Rotation3D<double>& targetR,
    const rw::math::Rotation3D<double>& residualRot)
{
    const rw::math::Rotation3D<double> rRefTarget = composeOf(transposeOf(rr), targetR);
    const double tr = residualRot(0, 0) + residualRot(1, 1) + residualRot(2, 2);
    const double angle = std::acos(std::max(-1.0, std::min(1.0, (tr - 1.0) / 2.0)));
    rw::math::Vector3D<double> axisT(residualRot(2, 1) - residualRot(1, 2),
                                     residualRot(0, 2) - residualRot(2, 0),
                                     residualRot(1, 0) - residualRot(0, 1));
    const double axisNorm = std::sqrt(axisT.norm2());
    if (axisNorm > 1e-12) {
        axisT *= (angle / axisNorm);
    } else {
        axisT = rw::math::Vector3D<double>(0.0, 0.0, 0.0);  // 近恒等——零残差
    }
    Eigen::Matrix<double, 6, 1> out;
    out(3) = out(4) = out(5) = 0.0;
    for (int k = 0; k < 3; ++k) {
        double refRow = 0.0;
        for (int j = 0; j < 3; ++j) {
            refRow += rRefTarget(j, k) * axisT[j];
        }
        out(3 + k) = refRow;
    }
    return out;
}

}  // namespace

/**
 * @brief 验收残差（契约面）——收敛判据与解记录的残差口径。
 *
 * allowToolRoll=false：参考系内 ZYX 欧拉字面逐项最短角差（REQ-01/09 契
 * 约；双侧各自按 zyxEulerAngles 提取，万向锁分支 roll 钉 0——批35 P1-3：
 * 不做任何参数化替换，掩码语义逐分量保持）。
 * allowToolRoll=true：目标族 {R_target·Rz(θ)} 的最近元轴角残差（批35
 * P1-1/P1-4——绕目标 z＝工具轴的扭转 θ 投影到区间，界内全吸收、界外拉
 * 回最近界；倾斜工具的扭转不再误用参考系 Euler yaw 承载）。
 */
Eigen::Matrix<double, 6, 1> constrainedPoseError(
    const rw::math::Transform3D<double>& target,
    const rw::math::Transform3D<double>& actual,
    const TaskPoseConstraint& constraint,
    double& posResidual,
    double& oriResidual)
{
    const rw::math::Rotation3D<double>& rr = constraint.referenceInBase;
    const rw::math::Vector3D<double> dp = target.P() - actual.P();
    rw::math::Vector3D<double> localP;
    for (int i = 0; i < 3; ++i) {
        localP[i] = rr(0, i) * dp[0] + rr(1, i) * dp[1] + rr(2, i) * dp[2];
    }
    Eigen::Matrix<double, 6, 1> full = poseError(target, actual, posResidual, oriResidual);

    const rw::math::Rotation3D<double> refT = transposeOf(rr);
    const rw::math::Rotation3D<double> targetRef = composeOf(refT, target.R());
    const rw::math::Rotation3D<double> actualRef = composeOf(refT, actual.R());
    if (!constraint.allowToolRoll) {
        // ---- 欧拉残差（REQ-01/REQ-09 契约面——批35 P1-3：不做参数化替
        // 换，掩码各分量语义保持；跨极连续性由最近等价表示承载——同一旋
        // 转的 ZYX 双支 (r,p,y)≡(r+π, π−p, y+π)，取离目标最近者，残差跨
        // 万向锁连续且残差/导数同分支）。
        const std::array<double, 3> targetRpy = zyxEulerAngles(targetRef);
        std::array<double, 3> actualRpy = zyxEulerAngles(actualRef);
        const auto distTo = [&targetRpy](const std::array<double, 3>& c) {
            double d = 0.0;
            for (int i = 0; i < 3; ++i) {
                const double e = wrapAngle(targetRpy[static_cast<std::size_t>(i)]
                                           - c[static_cast<std::size_t>(i)]);
                d += e * e;
            }
            return d;
        };
        std::array<double, 3> alt = {wrapAngle(actualRpy[0] + 3.14159265358979323846),
                                     wrapAngle(3.14159265358979323846 - actualRpy[1]),
                                     wrapAngle(actualRpy[2] + 3.14159265358979323846)};
        if (distTo(alt) < distTo(actualRpy)) { actualRpy = alt; }
        for (int i = 0; i < 3; ++i) {
            full(3 + i) = wrapAngle(targetRpy[static_cast<std::size_t>(i)]
                                    - actualRpy[static_cast<std::size_t>(i)]);
        }
    } else {
        // ---- 目标族残差（批35 P1-1/P1-4）：滚转自由＝绕**目标 z（工具
        // 轴）**的扭转 θ，合法集＝{R_target·Rz(θ), θ∈区间}。残差＝实际到
        // 族内最近元（θ 投影到区间）的旋转的轴角向量（target 系→参考系）：
        // 扭转界内被吸收（工具轴方向自由）、界外携带拉回最近界的分量参与
        // 迭代——残差/区间投影/DLS 角速度行三者同一分支（评审 P1-1"统一"
        // 要求；倾斜工具的扭转不再误用参考系 Euler yaw 承载——P1-4）。
        const rw::math::Rotation3D<double> eRot =
            composeOf(transposeOf(target.R()), actual.R());
        const std::array<double, 3> twist = zyzEulerAngles(eRot);  // (α,β,γ)
        double gammaC = twist[2];
        const double span = constraint.rollRange[1] - constraint.rollRange[0];
        if (span < 2.0 * 3.14159265358979323846) {
            const double offset = wrapAngle(gammaC - constraint.rollRange[0]);
            if (offset < 0.0) {
                gammaC = constraint.rollRange[0];          // 界下侧——吸收到 min
            } else if (offset > span) {
                gammaC = constraint.rollRange[0] + span;   // 界上侧——吸收到 max
            }
        }
        // 剩余旋转＝E·Rz(γc)⁻¹（吸收钳制扭转后）＝Rz(α)Ry(β)Rz(γ−γc)。
        const double cg = std::cos(gammaC);
        const double sg = std::sin(gammaC);
        const rw::math::Rotation3D<double> minusRz(cg, sg, 0, -sg, cg, 0, 0, 0, 1);
        const rw::math::Rotation3D<double> residualRot = composeOf(eRot, minusRz);
        // 轴角向量（target 系）→参考系（R_ref_target = rrᵀ·R_target）。
        const rw::math::Rotation3D<double> rRefTarget = composeOf(refT, target.R());
        const double tr = residualRot(0, 0) + residualRot(1, 1) + residualRot(2, 2);
        const double angle =
            std::acos(std::max(-1.0, std::min(1.0, (tr - 1.0) / 2.0)));
        rw::math::Vector3D<double> axisT(residualRot(2, 1) - residualRot(1, 2),
                                         residualRot(0, 2) - residualRot(2, 0),
                                         residualRot(1, 0) - residualRot(0, 1));
        const double axisNorm = std::sqrt(axisT.norm2());
        if (axisNorm > 1e-12) {
            axisT *= (angle / axisNorm);
        } else {
            axisT = rw::math::Vector3D<double>(0.0, 0.0, 0.0);  // 近恒等——零残差
        }
        for (int k = 0; k < 3; ++k) {
            double refRow = 0.0;
            for (int j = 0; j < 3; ++j) {
                refRow += rRefTarget(j, k) * axisT[j];
            }
            full(3 + k) = refRow;
        }
    }
    full(0) = localP[0]; full(1) = localP[1]; full(2) = localP[2];
    double p2 = 0.0;
    double r2 = 0.0;
    // 残差范数按掩码逐行（批35 P1-1：allowToolRoll 的扭转界外拉回分量
    // 已在 full(5) 参与迭代，不再无条件扣行）。
    for (int i = 0; i < 3; ++i) {
        if (constraint.constrainedDof[static_cast<std::size_t>(i)]) {
            p2 += full(i) * full(i);
        }
        if (constraint.constrainedDof[static_cast<std::size_t>(i + 3)]) {
            r2 += full(i + 3) * full(i + 3);
        }
    }
    posResidual = std::sqrt(p2);
    oriResidual = std::sqrt(r2);
    for (int i = 0; i < 6; ++i) {
        if (!constraint.constrainedDof[static_cast<std::size_t>(i)]) full(i) = 0.0;
    }
    return full;
}

/// @brief 约束 DLS 步长（批35 P1-1/P1-2/P1-3 终版）。
/// 行选择＝掩码直取（扭转界外拉回行参与迭代——P1-1）；姿态行按残差分
/// 支二分：allowToolRoll（目标族轴角残差，target 系→参考系）＝角速度行
/// （同一分支）；其余（欧拉差残差）＝ZYX 欧拉率映射行，映射评位点用与
/// 残差同一的最近等价表示，ωx/ωy/ωz 逐项显式组合（批34 的分量拾取错
/// 误随重写消除——P1-2）。cθ 钳制 0.05（万向锁附近限幅，DLS 阻尼吸收）。
Eigen::VectorXd constrainedDampedLeastSquaresStep(
    const std::vector<double>& jacobianRowMajor, std::size_t dof,
    const TaskPoseConstraint& constraint,
    const rw::math::Transform3D<double>& actualInBase,
    const rw::math::Transform3D<double>& targetInBase,
    const Eigen::Matrix<double, 6, 1>& e)
{
    std::vector<int> rows;
    for (int i = 0; i < 6; ++i) {
        if (constraint.constrainedDof[static_cast<std::size_t>(i)]) rows.push_back(i);
    }
    const rw::math::Rotation3D<double> refT = transposeOf(constraint.referenceInBase);
    const rw::math::Rotation3D<double> targetRef = composeOf(refT, targetInBase.R());
    const rw::math::Rotation3D<double> actualRef = composeOf(refT, actualInBase.R());
    const std::array<double, 3> targetRpy = zyxEulerAngles(targetRef);
    std::array<double, 3> actualRpy = zyxEulerAngles(actualRef);
    if (!constraint.allowToolRoll) {
        // 与残差同一最近等价表示（残差/导数同分支——P1-3）。
        const auto distTo = [&targetRpy](const std::array<double, 3>& c) {
            double d = 0.0;
            for (int i = 0; i < 3; ++i) {
                const double e = wrapAngle(targetRpy[static_cast<std::size_t>(i)]
                                           - c[static_cast<std::size_t>(i)]);
                d += e * e;
            }
            return d;
        };
        std::array<double, 3> alt = {wrapAngle(actualRpy[0] + 3.14159265358979323846),
                                     wrapAngle(3.14159265358979323846 - actualRpy[1]),
                                     wrapAngle(actualRpy[2] + 3.14159265358979323846)};
        if (distTo(alt) < distTo(actualRpy)) { actualRpy = alt; }
    }
    const double sPitch = std::sin(actualRpy[1]);
    const double cPitch = std::max(std::fabs(std::cos(actualRpy[1])), 0.05);
    const double sYaw = std::sin(actualRpy[2]);
    const double cYaw = std::cos(actualRpy[2]);

    Eigen::MatrixXd J(static_cast<Eigen::Index>(rows.size()), static_cast<Eigen::Index>(dof));
    const rw::math::Rotation3D<double>& rr = constraint.referenceInBase;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const int row = rows[r];
        for (std::size_t c = 0; c < dof; ++c) {
            double value = 0.0;
            if (row < 3) {
                for (int k = 0; k < 3; ++k) {
                    value += rr(k, row) * jacobianRowMajor[k * dof + c];
                }
            } else if (constraint.allowToolRoll) {
                // 目标族轴角残差（参考系）——角速度行线性化（同分支）。
                const int local = row - 3;
                for (int k = 0; k < 3; ++k) {
                    value += rr(k, local) * jacobianRowMajor[(k + 3) * dof + c];
                }
            } else {
                // 欧拉率映射行（P1-2：ωx/ωy/ωz 逐项显式，分量不拾取错）。
                double wx = 0.0;
                double wy = 0.0;
                double wz = 0.0;
                for (int k = 0; k < 3; ++k) {
                    wx += rr(k, 0) * jacobianRowMajor[(k + 3) * dof + c];
                    wy += rr(k, 1) * jacobianRowMajor[(k + 3) * dof + c];
                    wz += rr(k, 2) * jacobianRowMajor[(k + 3) * dof + c];
                }
                if (row == 3) {
                    value = (cYaw * wx + sYaw * wy) / cPitch;          // roll 行
                } else if (row == 4) {
                    value = -sYaw * wx + cYaw * wy;                    // pitch 行
                } else {
                    value = wz + (sPitch / cPitch) * (cYaw * wx + sYaw * wy);  // yaw 行
                }
            }
            J(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)) = value;
        }
    }
    Eigen::MatrixXd A = J * J.transpose();
    const double lambda2 = kIkDampingLambda * kIkDampingLambda;
    for (Eigen::Index i = 0; i < A.rows(); ++i) A(i, i) += lambda2;
    Eigen::VectorXd selected(static_cast<Eigen::Index>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        selected(static_cast<Eigen::Index>(i)) = e(rows[i]);
    }
    return J.transpose() * A.llt().solve(selected);
}

/// 阻尼最小二乘步长 Δq＝Jₜᵀ(JₜJₜᵀ＋λ²I_m)⁻¹eₜ（λ＝kIkDampingLambda——设计
/// 默认，黄金锁定 T13；行优先雅可比显式映射拷贝——Eigen 默认列主序，
/// 存储序耦合禁用，Fk.cpp 同款取舍）。
/// ★ m＝任务维度（KIN-P2/A02 表尾追加 taskDim 参数）：完整位姿任务
///   m=6（Jₜ＝完整 6×dof Jacobian）；仅位置任务 m=3（Jₜ＝Jacobain 的前
///   3 行位置子块——姿态行不进目标函数，A02 根因修复的核心：旧实现
///   6 维误差＋完整 J 迭代时姿态项把位置可达解牵制出局）。正规方程
///   对应为 m×m 对称正定阵。
Eigen::VectorXd dampedLeastSquaresStep(const std::vector<double>& jacobianRowMajor,
                                       std::size_t dof,
                                       std::size_t taskDim,
                                       const Eigen::VectorXd& e)
{
    Eigen::MatrixXd J(static_cast<Eigen::Index>(taskDim),
                      static_cast<Eigen::Index>(dof));
    for (std::size_t r = 0; r < taskDim; ++r) {
        for (std::size_t c = 0; c < dof; ++c) {
            J(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)) =
                jacobianRowMajor[r * dof + c];
        }
    }
    // 正规方程矩阵 A＝JₜJₜᵀ＋λ²I_m（m×m 对称正定——λ>0 保证可逆；LLT
    // 单线程固定算法——确定性）。
    Eigen::MatrixXd A = J * J.transpose();
    const double lambda2 = kIkDampingLambda * kIkDampingLambda;
    for (Eigen::Index i = 0; i < static_cast<Eigen::Index>(taskDim); ++i) {
        A(i, i) += lambda2;
    }
    const Eigen::VectorXd x = A.llt().solve(e);
    return J.transpose() * x;
}

/// 收敛判据（§5.3"每个收敛候选 q̂（残差 ≤ 两容差，逐项）"——逐项比较，
/// 不做合模长）。★ 任务维度感知（KIN-P2/A02）：仅位置任务的收敛判据
/// 只查位置残差（姿态残差是信息性记录、不参与判定——IkTaskSpace 注）；
/// 完整位姿任务保持 v1 双残差逐项语义逐位不变。
bool convergedForTask(IkTaskSpace taskSpace, double posResidual,
                      double oriResidual, double posTol, double oriTol)
{
    if (taskSpace == IkTaskSpace::PositionOnly) {
        return posResidual <= posTol;
    }
    return posResidual <= posTol && oriResidual <= oriTol;
}

/// 硬过滤记录构造（构型级指标照抄解草稿——§6.1"原因、对象对、指标"）。
FilteredSolutionRecord makeFilteredRecord(const KinematicSolution& s,
                                          SolutionFilterReason reason,
                                          double posResidual, double oriResidual)
{
    FilteredSolutionRecord r;
    r.q = s.q;
    r.reason = reason;
    r.positionResidual = posResidual;
    r.orientationResidual = oriResidual;
    r.minimumJointMargin = s.minimumJointMargin;
    r.manipulability = s.manipulability;
    r.sourceInitIndex = s.sourceInitIndex;
    r.iterations = s.iterations;
    r.signature = s.signature;
    return r;
}

}  // namespace

// =====================================================================
// evaluationIntervals——逐自由度评价区间（§6.1 三态唯一实现点）
// =====================================================================

std::vector<JointInterval> evaluationIntervals(const IKinRuntimeView& view)
{
    const std::vector<runtime::CanonicalJoint>& joints = view.model().chain().joints;
    std::vector<JointInterval> out;
    out.reserve(joints.size());
    for (const auto& j : joints) {
        if (j.type == runtime::JointType::Fixed) {
            continue;  // Fixed 不消耗 q（DOF 定义——FkEvaluator 同口径）。
        }
        JointInterval itv;
        // 三态：bounds（Revolute/Prismatic 必有）→ workingRange
        // （Continuous 可有——MDL-12 分析消费属性）→ 均无＝无分析限位。
        if (j.bounds.has_value()) {
            itv.lower = j.bounds->lower;
            itv.upper = j.bounds->upper;
            itv.bounded = true;
        } else if (j.workingRange.has_value()) {
            itv.lower = j.workingRange->lower;
            itv.upper = j.workingRange->upper;
            itv.bounded = true;
        }
        out.push_back(itv);
    }
    return out;
}

// =====================================================================
// makeInitialValues——初值策略生成（确定性；逐策略语义见 Ik.hpp 注）
// =====================================================================

namespace {

/// splitmix64（确定性伪随机序列源——§3.4"随机性唯一来源＝seed 派生的
/// 确定性序列"；无时钟/环境熵；算法公开定版、跨平台位级一致）。
std::uint64_t splitmix64(std::uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// (seed, 序号) 二元组的完整 64 位流种子派生（KIN-P5/审查 A06 修复——
/// 唯一混合点：SeededRandom 初值流的每初值状态由此产出）。
///
/// 为什么不用旧式 "seed·2³²＋k"：uint64 乘法按模 2⁶⁴ 回卷，seed 的
/// 高 32 位与 k 写进同一加法域——seed=1 与 seed=2³²＋1 的第 k 项状态
/// 完全相同（1·2³²＋k ≡ (2³²＋1)·2³²＋k mod 2⁶⁴），整个初值序列对
/// seed 高 32 位不敏感（审查 A06 反例：不同 64 位种子生成相同序列，
/// 多样性来源被静默丢弃）。
///
/// 派生规则（确定性——同 (seed, index) 同输出；跨平台位级一致）：
///   a ＝ splitmix64(seed)——seed 全 64 位 avalanche（高位翻转雪崩到
///       全字宽，任何 1 位 seed 差异扩散为约半数位差异）；
///   b ＝ splitmix64(index ⊕ 黄金常量)——序号独立 avalanche（⊕常量
///       把 index=0 拉离 a 的恒等点，防 a⊕0=a 的平凡回归）；
///   输出 ＝ splitmix64(a ⊕ b)——再 avalanche 消除异或的线性结构。
/// 三步后 (seed, index) 任一输入位的改变都以约 1/2 概率翻转输出的任
/// 一位——两个不同 64 位 seed 生成不同序列（除非发生 SHA 级碰撞，对
/// splitmix64 双射 avalanche 实际不可构造）。
std::uint64_t streamSeedFor(std::uint64_t seed, std::uint64_t index)
{
    std::uint64_t s = seed;
    const std::uint64_t a = splitmix64(s);
    std::uint64_t t = index ^ 0x9E3779B97F4A7C15ull;
    const std::uint64_t b = splitmix64(t);
    std::uint64_t u = a ^ b;
    return splitmix64(u);
}

/// u64 → [0,1) 均匀归一化（53 位有效——双精度无偏下界）。
double unitFromU64(std::uint64_t v)
{
    return static_cast<double>(v >> 11) * (1.0 / 9007199254740992.0);  // 2^53
}

/// 无分析限位轴的采样名义区间（[−π,π] rad——§6.1"无工作范围 continuous"
/// 的采样落值；登记随卡 §14.6 v0.4）。
constexpr double kNominalLower = -3.14159265358979323846;
constexpr double kNominalUpper = 3.14159265358979323846;

/// 单轴采样区间（bounded 用真实区间，否则名义区间）。
void axisRange(const JointInterval& itv, double& lo, double& hi)
{
    if (itv.bounded) {
        lo = itv.lower;
        hi = itv.upper;
    } else {
        lo = kNominalLower;
        hi = kNominalUpper;
    }
}

}  // namespace

std::vector<std::vector<double>> makeInitialValues(InitialValueStrategy strategy,
                                                   std::uint32_t count,
                                                   std::uint64_t seed,
                                                   const std::vector<JointInterval>& intervals,
                                                   const std::vector<double>& referenceQ)
{
    std::vector<std::vector<double>> out;
    switch (strategy) {
    case InitialValueStrategy::ReferenceQ: {
        // 单初值＝显式参考构型（D-KIN-4 同源输入——初值与排序参考一致）。
        out.push_back(referenceQ);
        break;
    }
    case InitialValueStrategy::SeededRandom: {
        // seed 派生确定性序列：第 k 个初值状态＝(seed, k) 完整 64 位混合
        // （streamSeedFor——KIN-P5/审查 A06：旧"seed·2³²＋k"在模 2⁶⁴
        // 算术下丢弃 seed 高 32 位，不同高位种子假借同序列；混合派生后
        // 逐轴 u_k,i∈[0,1)（splitmix64 按轴序连续派生——同 seed 同矩阵）。
        out.reserve(count);
        for (std::uint32_t k = 0; k < count; ++k) {
            std::uint64_t state = streamSeedFor(seed, k);  // 初值序入混合
            std::vector<double> q;
            q.reserve(intervals.size());
            for (const auto& itv : intervals) {
                double lo = 0.0;
                double hi = 0.0;
                axisRange(itv, lo, hi);
                q.push_back(lo + unitFromU64(splitmix64(state)) * (hi - lo));
            }
            out.push_back(std::move(q));
        }
        break;
    }
    case InitialValueStrategy::JointGrid: {
        // 限位空间均分：t_k＝k/(count−1)（count=1 → 中点 0.5）；沿评价
        // 区间对角线取点——不做自由度组合展开（登记随卡 §14.6 v0.4）。
        const std::uint32_t n = count == 0U ? 1U : count;
        out.reserve(n);
        for (std::uint32_t k = 0; k < n; ++k) {
            const double t = (n == 1U) ? 0.5
                                       : static_cast<double>(k)
                                             / static_cast<double>(n - 1U);
            std::vector<double> q;
            q.reserve(intervals.size());
            for (const auto& itv : intervals) {
                double lo = 0.0;
                double hi = 0.0;
                axisRange(itv, lo, hi);
                q.push_back(lo + t * (hi - lo));
            }
            out.push_back(std::move(q));
        }
        break;
    }
    }
    return out;
}

// =====================================================================
// IkSolver::solve——主流程（类注释列纲，逐步对号）
// =====================================================================

IkOutcome IkSolver::solve(const IkRequest& request) const
{
    // ---- 第 0 步：请求校验（fail-fast 轨——调用方错误，不进结果对象；
    // 逐项独立可辨——消息定位字段，NFR-COR-03 拒绝不钳制）。----
    if (request.modelView == nullptr) {
        failCaller("IkRequest.modelView：空指针（宿主注入契约违约——O-37）");
    }
    const runtime::CanonicalModel& model = request.modelView->model();
    const std::vector<runtime::CanonicalJoint>& joints = model.chain().joints;
    if (joints.empty()) {
        failCaller("IkRequest.modelView：无可用设备链（视图契约违约——"
                   "KIN-NO-DEVICE 语义锚；CanonicalModelBuilder 禁空链，"
                   "真实模型不可达）");
    }
    std::size_t dof = 0;
    for (const auto& j : joints) {
        if (j.type != runtime::JointType::Fixed) {
            ++dof;
        }
    }

    // 目标与容差（KIN-TARGET-ILLEGAL——"目标非法：非有限/容差非法"）。
    if (!targetAllFinite(request.targetInBase)) {
        failCaller("IkRequest.targetInBase：目标位姿含非有限分量"
                   "（KIN-TARGET-ILLEGAL 语义锚——§5.5；调用方错误 fail-fast）");
    }
    if (!std::isfinite(request.positionTolerance) || request.positionTolerance <= 0.0) {
        failCaller("IkRequest.positionTolerance：位置容差非法（须有限且>0，"
                   "单位 m）——KIN-TARGET-ILLEGAL 语义锚（§5.5）");
    }
    if (!std::isfinite(request.orientationTolerance)
        || request.orientationTolerance <= 0.0) {
        failCaller("IkRequest.orientationTolerance：姿态容差非法（须有限且>0，"
                   "单位 rad）——KIN-TARGET-ILLEGAL 语义锚（§5.5）");
    }

    // 任务空间模式值域校验（KIN-P2/A02 表尾追加字段的词表面——uint8
    // 位型强转可造出词表外值，NFR-COR-03 拒绝不静默归约）。注意两容差
    // 的合法域校验不随任务模式放宽：orientationTolerance 在
    // PositionOnly 任务下不参与收敛判定，但仍须是合法容差值（请求合法
    // 域恒全量校验——身份面/记录面一致性不因消费路径裁剪）。
    if (request.taskSpace != IkTaskSpace::FullPose
        && request.taskSpace != IkTaskSpace::PositionOnly) {
        failCaller("IkRequest.taskSpace：任务空间模式词表外值（static_cast<"
                   "std::uint8_t>="
                   + std::to_string(static_cast<unsigned>(request.taskSpace))
                   + "）——调用方错误 fail-fast（NFR-COR-03）");
    }
    if (request.taskConstraint.has_value()) {
        const TaskPoseConstraint& c = *request.taskConstraint;
        bool any = false;
        for (bool bit : c.constrainedDof) any = any || bit;
        if (!any) failCaller("IkRequest.taskConstraint：六分量不能全部自由");
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
            if (!std::isfinite(c.referenceInBase(i, j)))
                failCaller("IkRequest.taskConstraint.referenceInBase：旋转含非有限分量");
        }
        for (int i = 0; i < 3; ++i) {
            double norm = 0.0;
            for (int k = 0; k < 3; ++k) norm += c.referenceInBase(k, i) * c.referenceInBase(k, i);
            if (std::fabs(norm - 1.0) > 1e-9) failCaller("IkRequest.taskConstraint.referenceInBase：旋转非正交");
            for (int j = i + 1; j < 3; ++j) {
                double dot = 0.0;
                for (int k = 0; k < 3; ++k) dot += c.referenceInBase(k, i) * c.referenceInBase(k, j);
                if (std::fabs(dot) > 1e-9) failCaller("IkRequest.taskConstraint.referenceInBase：旋转非正交");
            }
        }
        if (!std::isfinite(c.rollRange[0]) || !std::isfinite(c.rollRange[1])
            || c.rollRange[0] >= c.rollRange[1]) {
            failCaller("IkRequest.taskConstraint.rollRange：区间非法（单位rad）");
        }
    }
    // 任务维度（A02 修复核心——目标函数与 Jacobian 子块的维数来源）：
    // 仅位置任务取 3（只位置误差进目标函数）；完整位姿任务取 6（v1
    // 行为逐位保持）。
    const std::size_t taskDim = request.taskSpace == IkTaskSpace::PositionOnly
                                    ? 3U
                                    : 6U;

    // TCP 解析（KIN-NO-TCP 两分/FrameUnresolved——fail-fast 轨）。
    resolveTcpOrThrow(*request.modelView, request.tcp);

    // 迭代上限/去重阈值（求解参数面）。
    if (request.iterationLimit == 0U) {
        failCaller("IkRequest.iterationLimit：迭代上限须 ≥1（当前 0）");
    }
    if (!std::isfinite(request.dedupThresholdPerAxis)
        || request.dedupThresholdPerAxis <= 0.0) {
        failCaller("IkRequest.dedupThresholdPerAxis：去重阈值非法（须有限且"
                   ">0，rad|m 逐轴——附录 D 第 3 项默认 1e-6）");
    }

    // 参考构型（D-KIN-4——排序与身份的显式输入）。
    if (request.referenceQ.size() != dof) {
        failCaller("IkRequest.referenceQ：维度 " + std::to_string(request.referenceQ.size())
                   + " != 设备自由度 " + std::to_string(dof));
    }
    if (const auto bad = firstNonFinite(request.referenceQ)) {
        failCaller("IkRequest.referenceQ：分量[" + std::to_string(*bad)
                   + "] 非有限（NFR-COR-03：拒绝，不置零）");
    }

    // 初值集（非空＋逐项维度/有限性）。
    if (request.initialValues.empty()) {
        failCaller("IkRequest.initialValues：空初值集（多初值求解至少 1 个初值）");
    }
    for (std::size_t k = 0; k < request.initialValues.size(); ++k) {
        const std::vector<double>& q0 = request.initialValues[k];
        if (q0.size() != dof) {
            failCaller("IkRequest.initialValues[" + std::to_string(k)
                       + "]：维度 " + std::to_string(q0.size()) + " != 设备自由度 "
                       + std::to_string(dof));
        }
        if (const auto bad = firstNonFinite(q0)) {
            failCaller("IkRequest.initialValues[" + std::to_string(k)
                       + "]：分量[" + std::to_string(*bad)
                       + "] 非有限（NFR-COR-03：拒绝，不置零）");
        }
    }

    // 评价区间（与 modelView 同快照——调用方义务的维度面校验）。
    if (request.intervals.size() != dof) {
        failCaller("IkRequest.intervals：维度 " + std::to_string(request.intervals.size())
                   + " != 设备自由度 " + std::to_string(dof)
                   + "（须取自同一快照——evaluationIntervals(view)）");
    }
    for (const auto& itv : request.intervals) {
        if (itv.bounded
            && (!std::isfinite(itv.lower) || !std::isfinite(itv.upper)
                || itv.lower > itv.upper)) {
            failCaller("IkRequest.intervals：有界区间非法（须有限且 lower≤upper）");
        }
    }

    // ---- 第 1 步：解析界限检查（§5.4 结局 5——静态、覆盖全部可能解；
    // 仅产证明素材不裁定。界限计算在本请求已验证的 (view, tcp) 上不可
    // 失败——防御分支按内部违约 fail-fast，不静默）。----
    const Expected<AnalyticReachBound> bound =
        computeAnalyticReachBound(*request.modelView, request.tcp);
    if (!bound.ok()) {
        throw std::logic_error(
            "IkSolver：已解析 TCP 的界限计算失败（内部契约违约——fail-fast）");
    }
    double targetDistance = 0.0;
    for (int i = 0; i < 3; ++i) {
        const double v = request.targetInBase.P()[static_cast<std::size_t>(i)];
        targetDistance += v * v;
    }
    targetDistance = std::sqrt(targetDistance);

    IkOutcome outcome;  // 绑定面与版本先填——结局分支共用。
    outcome.solutionSet.targetRef = request.targetRef;
    outcome.solutionSet.requestIdentity = request.requestIdentity;
    outcome.collisionNotEvaluated = request.collisionSession == nullptr;

    const bool fullPositionConstraint = !request.taskConstraint.has_value()
        || (request.taskConstraint->constrainedDof[0]
            && request.taskConstraint->constrainedDof[1]
            && request.taskConstraint->constrainedDof[2]);
    if (fullPositionConstraint && targetDistance > bound.get().totalRadius) {
        // 结局 5：目标位置超出解析工作半径上界——零迭代，仅产素材
        // （V-10 前半：成立与否归 evidence validateProof）。
        outcome.outcomeKind = IkOutcomeKind::AnalyticBoundExceeded;
        outcome.proofMaterial = makeAnalyticBoundMaterial(
            bound.get(), request.targetInBase, request.requestIdentity,
            request.targetRef);
        outcome.solutionSet.statistics.rawCount = request.initialValues.size();
        return outcome;
    }

    // ---- 第 2 步：逐初值阻尼最小二乘迭代（§5.3 时序——纯函数服务委托
    // FkEvaluator 复算 FK；周期查询取消探针）。----
    const FkEvaluator fkService;
    std::vector<KinematicSolution> convergedCandidates;  // 通过全部硬过滤的候选
    std::uint64_t stage1Survivors = 0;  // 阶段①残差复验通过数（结局 2/3 区分——
                                        //   独立于②③过滤结果，语义见统计口径）
    std::vector<FilteredSolutionRecord> filteredRecords;  // 硬过滤逐解记录
    IkSearchRecord search;
    // 碰撞评价缺失追踪（WP-15-T07——§8.1 取消/失败行）：缺失计数入结局
    // 标记（outcome.collisionEvidenceMissing），末次原因素材仅作实现侧
    // 观测（诊断 cause 的逐解素材在评估器面由解记录的 evaluated=false
    // 承载——本求解器不产诊断，只产素材）。
    std::uint64_t collisionEvidenceMissingCount = 0;
    std::string lastEvidenceMissingDetail;
    search.initialGuessesTried = request.initialValues.size();
    convergedCandidates.reserve(request.initialValues.size());
    search.iterationsPerInit.reserve(request.initialValues.size());

    const TcpRef& tcp = request.tcp;
    for (std::size_t k = 0; k < request.initialValues.size(); ++k) {
        // 2a. 初值起点的取消探针（§8.3 响应粒度——初值边界即粒度边界）。
        if (request.cancellationProbe && request.cancellationProbe()) {
            // 取消不是结局（§9.2）——立即返回无终局字段载荷。
            IkOutcome cancelledOut;
            cancelledOut.cancelled = true;
            cancelledOut.solutionSet.targetRef = request.targetRef;
            cancelledOut.solutionSet.requestIdentity = request.requestIdentity;
            return cancelledOut;
        }

        std::vector<double> q = request.initialValues[k];
        Expected<PoseMetrics> m = fkService.evaluate(*request.modelView, tcp, q);
        if (!m.ok()) {
            // FK 首评失败＝求解设施失败（F-746③ 转值面——audit F-647③ 原为
            // std::logic_error，与"solve 不抛——除非调用方契约违约"承诺矛盾）：
            // 请求在第 0 步已全量验证（q 有限/维度符、TCP 已解析），此处
            // 失败＝注入视图面在求解期间失效（模型内容漂移等设施事实）——
            // 不是调用方契约违约（不进 §5.5 fail-fast 轨），也不能虚构结局
            // （五类结局均有搜索判定语义）。按"设施失败不是结局"返回
            // facilityFailure 形态，整个求解停止（后续初值同面不可信——
            // 不做部分解猜测）。
            return makeFacilityFailure(
                IkFacilityFailureStage::InitialFkEvaluation, request, search,
                "initialValues[" + std::to_string(k) + "] FK 复算失败："
                    + m.error().detail);
        }

        double posResidual = 0.0;
        double oriResidual = 0.0;
        std::uint32_t iterations = 0;
        bool isConverged = false;
        while (true) {
            // 2b. 收敛判据（残差 ≤ 容差逐项——§5.3；任务维度感知：
            // PositionOnly 只查位置残差——A02；FullPose 双残差逐项不变）。
            // e 恒按 6 维产出（姿态残差仍如实计算供记录——PositionOnly
            // 下仅前 taskDim 维进目标函数）。
            // 验收残差（契约面——欧拉/目标族）供收敛判据与解记录；迭代
            // 残差（测地——批35 P1-3 迭代/验收分离）供 DLS 步进。
            const Eigen::Matrix<double, 6, 1> e =
                request.taskConstraint.has_value()
                    ? constrainedPoseError(request.targetInBase, m.get().tcpInBase,
                                           *request.taskConstraint, posResidual,
                                           oriResidual)
                    : poseError(request.targetInBase, m.get().tcpInBase,
                                posResidual, oriResidual);
            const bool converged = request.taskConstraint.has_value()
                ? (posResidual <= request.positionTolerance
                   && oriResidual <= request.orientationTolerance)
                : convergedForTask(request.taskSpace, posResidual, oriResidual,
                                   request.positionTolerance,
                                   request.orientationTolerance);
            if (converged) {
                isConverged = true;
                break;
            }
            // 2c. 预算/取消（迭代上限→该初值未收敛计入统计——§5.4 结局 2
            // 素材；取消探针每 N 次迭代一次）。
            if (iterations >= request.iterationLimit) {
                break;
            }
            if (iterations % kIkCancellationProbeInterval == 0U
                && request.cancellationProbe && request.cancellationProbe()) {
                IkOutcome cancelledOut;
                cancelledOut.cancelled = true;
                cancelledOut.solutionSet.targetRef = request.targetRef;
                cancelledOut.solutionSet.requestIdentity = request.requestIdentity;
                return cancelledOut;
            }
            // 2d. 阻尼最小二乘更新（Δq＝Jₜᵀ(JₜJₜᵀ＋λ²I)⁻¹eₜ——定序固定；
            // taskDim 决定 e 的有效维与 Jacobian 子块行数——A02）。
            Eigen::VectorXd dq = request.taskConstraint.has_value()
                ? constrainedDampedLeastSquaresStep(m.get().jacobian, dof,
                                                    *request.taskConstraint,
                                                    m.get().tcpInBase,
                                                    request.targetInBase, e)
                : dampedLeastSquaresStep(m.get().jacobian, dof, taskDim, e.head(taskDim));
            // ★ 批34：约束求解步长上限（调试实测跨欧拉分支时巨步 29 rad
            //   ——欧拉提取在 ±90° 分支边界残差跳变，巨步与跳变互激发散；
            //   步长上限＋DLS 阻尼使跨分支迭代有界可回收）。
            if (request.taskConstraint.has_value()) {
                const double dqNorm = dq.norm();
                constexpr double kConstrainedMaxStep = 0.35;
                if (dqNorm > kConstrainedMaxStep) {
                    dq *= kConstrainedMaxStep / dqNorm;
                }
            }
            for (std::size_t i = 0; i < dof; ++i) {
                q[i] += dq(static_cast<Eigen::Index>(i));
            }
            ++iterations;
            m = fkService.evaluate(*request.modelView, tcp, q);
            if (!m.ok()) {
                // FK 再评失败＝求解设施失败（F-746③ 转值面——口径同初评
                // 分支：视图面设施事实，非调用方违约、不虚构结局）。该初值
                // 的迭代链中断，整个求解按设施失败返回（其迭代次数未落账
                // ——部分搜索统计口径见 makeFacilityFailure）。
                return makeFacilityFailure(
                    IkFacilityFailureStage::IterationFkEvaluation, request, search,
                    "initialValues[" + std::to_string(k) + "] 迭代内 FK 复算失败"
                        "（第 " + std::to_string(iterations) + " 次迭代后）："
                        + m.error().detail);
            }
        }

        // 2e. 迭代统计（结局 2 的素材——逐初值迭代次数与总预算）。
        search.iterationsPerInit.push_back(iterations);
        search.searchBudgetUsed += iterations;

        if (!isConverged) {
            continue;  // 未收敛：零候选语义（不做部分解猜测——§5.4）。
        }

        // 2f. 收敛候选 → 解记录草稿（指标取自收敛构型的 FK 全套输出）。
        KinematicSolution s;
        s.q = std::move(q);
        s.positionResidual = posResidual;
        s.orientationResidual = oriResidual;
        s.jointMargins = m.get().jointMargins;
        s.minimumJointMargin = m.get().minimumJointMargin;
        s.manipulability = m.get().manipulability;
        s.conditionNumber = m.get().conditionNumber;
        s.sourceInitIndex = static_cast<std::uint32_t>(k);
        s.iterations = iterations;
        s.signature = configurationSignature(s.q);
        s.solverContractVersion = kIkSolverContractVersion;

        // ---- 第 3 步：硬过滤（顺序固定①残差复验→②限位→③碰撞——§5.3；
        // 记录取**首个命中的阶段**，顺序即语义；每个被过滤解记录原因——
        // 构型级记录，不下结论）。----
        // 3a. ①残差复验（FK 复算——独立于迭代内收敛判据的一次新鲜复算；
        // 换求解判据/注入替身时本阶段是残差超容差解的拦截点，比较型量：
        // 位置 m／姿态 rad）。
        {
            Expected<PoseMetrics> recheck =
                fkService.evaluate(*request.modelView, tcp, s.q);
            if (!recheck.ok()) {
                // 残差复验 FK 失败＝求解设施失败（F-746③ 转值面——口径同
                // 上两分支；收敛候选不可复验，整个求解按设施失败返回。本
                // 初值的迭代次数已在 2e 落账，随部分搜索统计如实交付）。
                return makeFacilityFailure(
                    IkFacilityFailureStage::ResidualRecheck, request, search,
                    "initialValues[" + std::to_string(k) + "] 残差复验 FK 复算失败："
                        + recheck.error().detail);
            }
            // 复验残差（比较型量——覆写当次残差变量，供记录承载）。
            if (request.taskConstraint.has_value()) {
                constrainedPoseError(request.targetInBase, recheck.get().tcpInBase,
                                     *request.taskConstraint, posResidual, oriResidual);
            } else {
                poseError(request.targetInBase, recheck.get().tcpInBase,
                          posResidual, oriResidual);
            }
            // 复验收敛判据与迭代内同任务维度（A02——PositionOnly 只查
            // 位置残差；两判据必须同源，防"迭代放行、复验拦截"的判据
            // 漂移——残差复验的本意是独立 FK 复算，不是换一套判据）。
            const bool recheckConverged = request.taskConstraint.has_value()
                ? (posResidual <= request.positionTolerance
                   && oriResidual <= request.orientationTolerance)
                : convergedForTask(request.taskSpace, posResidual, oriResidual,
                                   request.positionTolerance,
                                   request.orientationTolerance);
            if (!recheckConverged) {
                filteredRecords.push_back(
                    makeFilteredRecord(s, SolutionFilterReason::ResidualRecheck,
                                       posResidual, oriResidual));
                continue;
            }
            // 阶段①幸存计数（结局 2/3 区分的判据——②③过滤结果不影响：
            // "有收敛候选但全被②③过滤"＝结局 3 而非结局 2）。
            ++stage1Survivors;
        }

        // 3b. ②限位（有界关节出 bounds／continuous 出工作范围——§6.1
        // 直接比较、无跨周取模；逐轴检查任一越界即记录）。
        bool limitViolated = false;
        for (std::size_t i = 0; i < s.q.size(); ++i) {
            const JointInterval& itv = request.intervals[i];
            if (itv.bounded && (s.q[i] < itv.lower || s.q[i] > itv.upper)) {
                limitViolated = true;
                break;
            }
        }
        if (limitViolated) {
            filteredRecords.push_back(
                makeFilteredRecord(s, SolutionFilterReason::JointLimit,
                                   posResidual, oriResidual));
            continue;
        }

        // 3c. ③碰撞（policy 会话在场时——构型级判定仅过滤该解；未启用
        // →跳过并标记 collisionNotEvaluated，绝不解读为无碰撞——KIN-05）。
        // ★ T07 三态（WP-15-T07——§8.1 取消/失败行）：会话应答按评价状态
        // 分轨——仅 Evaluated 态参与过滤/放行；EvidenceMissing/FacilityFailed
        // 态该解保留但 collisionStatus.evaluated=false（证据缺失素材，
        // 诊断由评估器面产 KIN-COLLISION-UNAVAILABLE），**不中断**其余
        // 候选的循环（整批语义——一解评价失败不污染他解）。
        if (request.collisionSession != nullptr) {
            const IkCollisionVerdict verdict =
                request.collisionSession->evaluate(s.q);
            switch (verdict.state) {
            case IkCollisionEvaluationState::Evaluated:
                if (verdict.inCollision) {
                    FilteredSolutionRecord r =
                        makeFilteredRecord(s, SolutionFilterReason::Collision,
                                           posResidual, oriResidual);
                    r.objectIdPairs = verdict.objectIdPairs;
                    filteredRecords.push_back(std::move(r));
                    continue;
                }
                s.collisionStatus.evaluated = true;
                s.collisionStatus.inCollision = false;
                break;
            case IkCollisionEvaluationState::EvidenceMissing:
            case IkCollisionEvaluationState::FacilityFailed:
                // 证据缺失/设施异常：解保留入后续去重排序（其碰撞状态
                // 保持未评价——调用方不得当作可行凭据，KIN-05）；缺失
                // 计数入结局标记，评估器面据此产诊断。
                s.collisionStatus.evaluated = false;
                collisionEvidenceMissingCount += 1;
                lastEvidenceMissingDetail = verdict.statusDetail;
                break;
            }
        } else {
            // 未启用碰撞：解的碰撞状态保持未评价（证据缺失语义）。
            s.collisionStatus.evaluated = false;
        }

        convergedCandidates.push_back(std::move(s));
    }

    // ---- 第 4 步：去重（§5.3/§6.1——关节空间逐轴容差成对比较，去重
    // 对象是构型而非位姿（同位姿异构型均保留）；continuous 按工作范围
    // 直接比较、无跨周取模（附录 D 第 3 项/C1）；ConfigurationSignature
    // 仅作记录键不参与去重判定（I-KIN-3）。按初值序遍历——组内代表＝
    // sourceInitIndex 最小者（先到先留，稳定）。被合并的重复构型不产生
    // 过滤记录（去重≠硬过滤——统计口径分离）。实现＝
    // deduplicateSolutions 唯一实现点（SolutionSet.cpp——§6.2 视图面
    // 共享同语义）。----
    // 碰撞评价缺失标记（T07——§8.1：设施异常/策略侧不可判→该解证据缺失
    // 素材；>0 即结局级标记，诊断由评估器面产 KIN-COLLISION-UNAVAILABLE）。
    outcome.collisionEvidenceMissing = collisionEvidenceMissingCount > 0;
    std::vector<KinematicSolution> deduped =
        deduplicateSolutions(convergedCandidates, request.dedupThresholdPerAxis);

    // ---- 第 5 步：稳定排序（§6.3 四键——sortSolutions 唯一实现点，
    // 与解集视图共享同一语义）＋统计与结局判定（§5.4）。----
    sortSolutions(deduped, request.referenceQ);

    outcome.solutionSet.solutions = std::move(deduped);
    outcome.solutionSet.filteredRecords = std::move(filteredRecords);
    IkSolutionSetStatistics& stats = outcome.solutionSet.statistics;
    stats.rawCount = request.initialValues.size();
    stats.convergedCount = stage1Survivors;
    stats.dedupedCount = outcome.solutionSet.solutions.size();
    stats.filteredCount = outcome.solutionSet.filteredRecords.size();

    if (outcome.solutionSet.solutions.empty()) {
        // 结局 2/3：零可行解——搜索未果记录**必附**（预算/初值数/迭代
        // 统计；逐解过滤记录在 filteredRecords 同批交付）→DataInsufficient
        // 素材；两者都**不得输出不可行**（无 proofMaterial——§8.1 C5/C8）。
        // 区分判据＝阶段①幸存数：有收敛候选但全被②③过滤＝结局 3；
        // 零收敛候选＝结局 2。
        const bool hadCandidates = stage1Survivors > 0;
        outcome.outcomeKind = hadCandidates ? IkOutcomeKind::AllCandidatesFiltered
                                            : IkOutcomeKind::MultiInitNoConvergence;
        outcome.solutionSet.searchRecord = std::move(search);
    } else {
        // 结局 1/4：有可行解——存在碰撞原因的过滤记录＝PartialCollision
        // （§5.4 行 4"部分解碰撞、其余有效"的判定落值，登记随卡
        // §14.6 v0.4）；否则 SolutionsFound。
        bool anyCollisionFiltered = false;
        for (const FilteredSolutionRecord& r : outcome.solutionSet.filteredRecords) {
            if (r.reason == SolutionFilterReason::Collision) {
                anyCollisionFiltered = true;
                break;
            }
        }
        outcome.outcomeKind = anyCollisionFiltered ? IkOutcomeKind::PartialCollision
                                                   : IkOutcomeKind::SolutionsFound;
    }
    return outcome;
}

}  // namespace sdurws::ird::kinematics
