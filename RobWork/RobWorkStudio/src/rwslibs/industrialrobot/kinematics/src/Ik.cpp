/**
 * @file   Ik.cpp
 * @brief  多初值 IK 求解实现（KIN-02，REUSE-T51 上游复用版）——请求校验、
 *         初值策略生成、解析界限检查与五类结局组装（§5.3 时序）；完整位
 *         姿普通模式迭代核＝上游 rw::invkin::JacobianIKSolver（经 T17 锚
 *         消费已编译 WorkCell——solveLocal 单步循环驱动＋独立双容差残差
 *         复核，见下方 REUSE-T51 装配段）；仅位置/任务约束两模式仍走本
 *         单元阻尼最小二乘链（GAP-IK-01：上游掩码入口待 runtime 视图权
 *         限审查/Euler≠EAA 掩码反例在案——归 WP-05）。FK 复算失败走求解
 *         设施失败值面（F-746③——非终局形态，不抛）。
 *         ★ KIN-P2（审查 A02）：迭代与收敛判据任务维度感知——IkTaskSpace
 *         PositionOnly（仅位置任务）只优化 3 维位置误差及 Jacobian 位置
 *         子块（姿态不进目标函数）；求解器契约版本随行为集扩展升 v2
 *         （CON-04），REUSE-T51 换核再升 v5。
 *
 * 设计依据（契约面见 Ik.hpp 文件头）：units/kinematics.md §5.3/§5.4/
 * §6.1/§6.3/§9.2/§3.4；治理登记 O-40（Eigen PRIVATE 仅实现文件——本
 * 翻译单元与 Fk.cpp 同为 Eigen 消费点）；任务契约 WP-15-T04
 * acceptance 1/2/3/4；migration §13.3 WP-04 行（REUSE-T51）。
 *
 * REUSE-T51 上游复用边界（migration §13.3 WP-04——所有者批准的迁移计划
 * 行；H1 直接 API，不复制/不改上游）：
 *   - JacobianIKSolver（RobWork/RobWork/src/rw/invkin/JacobianIKSolver.
 *     hpp:117-129 构造、solve baseTend/state→vector<Q>、solveLocal
 *     bTed/maxError/state&/maxIter→bool）——迭代核唯一来源；默认 SVD 伪
 *     逆步（JacobianIKSolver.cpp:124-131）、maxIterations=15、不钳限位、
 *     不查限位（.cpp:37-38）——本适配显式保持三个默认并登记：限位评价
 *     归产品硬过滤②（契约区间直比，§6.1），不钳制（钳制会扭曲上游收敛
 *     语义）；
 *   - 上游停判据＝normInf(6 维旋量)（位置分量 m＋EAA 分量 rad 混合单位）
 *     ——不承担验收语义：maxError=min(位置容差,姿态容差)，解的最终验收
 *     ＝阶段①独立 FK 复算按产品双容差逐项复核（GAP-IK-01 §1"优先原
 *     API，结束后以任务实际位置/姿态容差独立复核，不另写 DLS"）；
 *   - 上游不回传真实迭代数/停止原因（IterativeIK 只有上限配置）——本适
 *     配经 solveLocal(maxIter=1) 单步循环驱动：每调用＝上游循环体恰一次
 *     （停判＋至多一步 SVD 步），迭代数逐步实测、取消探针保持 §8.3 周期；
 *     单步/多步调用轨迹同一性有测试钉（GAP-IK-01 §1"不能用配置上限伪
 *     造实际迭代数"）；
 *   - 任务私有 State：每初值经锚 makeState() 取独立值拷贝（§8.7/§9.2）；
 *     权威 q↔上游 WC 坐标单点映射见 src/JointMapping.hpp（两方向同头互
 *     逆——REUSE-T51 起）；
 *   - 上游假成功拦截：solveLocal 对不可达目标可误报成功（GAP-IK-01 §1
 *     结果正确性行）——上游收敛后仍须经 FkEvaluator 独立 FK 复算双容差
 *     判定，不过判＝该初值未收敛（不产候选）。
 *
 * 实现纪律（两模式可链接＋确定性——Fk.cpp 同款登记）：
 *   - rw::math 值类型仅头内 inline 面；旋转/线性代数在 Eigen 组件域，
 *     出界前一次性转回（上游迭代核消费经 sdurw_invkin 导出目标——两模
 *     式链接面见 kinematics/CMakeLists.txt 上游链接段）；
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
#include <sdurws/ird/runtime/Snapshot.hpp>        // IRuntimeModelView 完整类型（锚消费面）
#include "JointMapping.hpp"                       // q_rw↔q_auth 单点映射（单元私有纯函数——REUSE-T51 两方向）

#include <rw/invkin/JacobianIKSolver.hpp>  // 上游完整位姿迭代核（REUSE-T51——H1 直接 API）
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/Device.hpp>        // Device::setQ/getQ（任务私有 State 面）
#include <rw/models/SerialDevice.hpp>  // SerialDevice 完整类型（设备查询面）

#include <Eigen/Core>
#include <Eigen/Cholesky>  // LLT——DLS 正规方程 (JJᵀ＋λ²I) 求解（O-40 Eigen PRIVATE；仅位置/约束模式）

#include <cmath>
#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "SeedMix.hpp"  // 共享种子派生（G12 收拢——唯一算法权威点）

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

/// 跨周最短角差（rad），固定到[-pi,pi]，用于残差及差分角展开。
double wrapAngle(double angle)
{
    return std::atan2(std::sin(angle), std::cos(angle));
}

/// 参考系转置：输入参考轴在基座系的旋转，返回基座系到参考系的旋转。
rw::math::Rotation3D<double> transposeOf(const rw::math::Rotation3D<double>& r)
{
    return rw::math::Rotation3D<double>(
        r(0,0),r(1,0),r(2,0), r(0,1),r(1,1),r(2,1), r(0,2),r(1,2),r(2,2));
}

/// 逐项矩阵乘法，避免独立冒烟模式引入框架库外联符号；输入输出均为旋转值。
rw::math::Rotation3D<double> composeOf(const rw::math::Rotation3D<double>& a,
                                     const rw::math::Rotation3D<double>& b)
{
    return rw::math::Rotation3D<double>(
        a(0,0)*b(0,0)+a(0,1)*b(1,0)+a(0,2)*b(2,0),
        a(0,0)*b(0,1)+a(0,1)*b(1,1)+a(0,2)*b(2,1),
        a(0,0)*b(0,2)+a(0,1)*b(1,2)+a(0,2)*b(2,2),
        a(1,0)*b(0,0)+a(1,1)*b(1,0)+a(1,2)*b(2,0),
        a(1,0)*b(0,1)+a(1,1)*b(1,1)+a(1,2)*b(2,1),
        a(1,0)*b(0,2)+a(1,1)*b(1,2)+a(1,2)*b(2,2),
        a(2,0)*b(0,0)+a(2,1)*b(1,0)+a(2,2)*b(2,0),
        a(2,0)*b(0,1)+a(2,1)*b(1,1)+a(2,2)*b(2,1),
        a(2,0)*b(0,2)+a(2,1)*b(1,2)+a(2,2)*b(2,2));
}

/**
 * @brief 在目标工具Z轴旋转族中选离实际姿态最近的合法旋转（A-16）。
 * @param target [in] 基座系目标姿态，只读借用。
 * @param actual [in] 同一FK快照的基座系实际姿态，只读借用。
 * @param range [in] 合法工具轴扭转区间，rad，已由请求校验保证有限且递增。
 * @return 基座系最近目标族旋转值。反向轴退化时固定零扭转参考后再投影区间。
 *
 * 最小测地角等价于最大化trace(E*Rz(-theta))，E=target^T*actual；
 * 其theta相关项为(E00+E11)cos(theta)+(E10-E01)sin(theta)。
 * 因而直接atan2得到最优扭转，不把ZYZ的gamma误认为完整swing/twist扭转。
 * 只投影目标，不把反向180度的零反对称部分错误视为已满足。
 */
rw::math::Rotation3D<double> nearestToolRollTarget(
    const rw::math::Rotation3D<double>& target,
    const rw::math::Rotation3D<double>& actual,
    const std::array<double,2>& range)
{
    constexpr double kTwoPi=6.28318530717958647692;
    const auto relative=composeOf(transposeOf(target),actual);
    const double sine=relative(1,0)-relative(0,1);
    const double cosine=relative(0,0)+relative(1,1);
    // 轴反向时整族等距，固定theta=0避免atan2的有符号零影响确定性。
    double theta=std::hypot(sine,cosine)>1e-12 ? std::atan2(sine,cosine) : 0.0;
    const double span=range[1]-range[0];
    if(span<kTwoPi) {
        double offset=std::fmod(theta-range[0],kTwoPi);
        if(offset<0.0) offset+=kTwoPi;
        if(offset<=span) {
            theta=range[0]+offset; // 同一姿态的周期等价元确在区间内。
        } else {
            // 周期意义下比较两端，跨±pi和非零周数区间不能直接数值clamp。
            theta=std::fabs(wrapAngle(theta-range[0]))
                <=std::fabs(wrapAngle(theta-range[1])) ? range[0] : range[1];
        }
    }
    const double c=std::cos(theta),s=std::sin(theta);
    return composeOf(target,rw::math::Rotation3D<double>(c,-s,0,s,c,0,0,0,1));
}

/**
 * @brief 按A-16唯一契约构造参考系六分量目标减实际残差。
 * @param target [in] 基座系目标，位置m、旋转无量纲，借用。
 * @param actual [in] 同一快照真实FK，位置m，借用。
 * @param constraint [in] 参考系旋转、六分量掩码和工具轴扭转区间，借用。
 * @param posResidual [out] 启用位置分量的二范数，m。
 * @param oriResidual [out] 启用欧拉分量的二范数，rad。
 * @return 固定x/y/z/roll/pitch/yaw顺序，未启用行置零；无共享状态且不抛异常。
 *
 * 绕轴自由只改变允许的目标族；之后仍用同一参考系ZYX分量与掩码，
 * 不把Euler分量替换为轴角分量，也不依据自由分量选择另一套Euler表示。
 * 迭代、收敛及独立FK复验均调用本函数，防止“求得的解”与验收语义分裂。
 */
Eigen::Matrix<double,6,1> constrainedPoseError(
    const rw::math::Transform3D<double>& target,
    const rw::math::Transform3D<double>& actual,
    const TaskPoseConstraint& constraint,double& posResidual,double& oriResidual)
{
    const auto& rr=constraint.referenceInBase;
    const auto refT=transposeOf(rr);
    const auto desired=constraint.allowToolRoll
        ?nearestToolRollTarget(target.R(),actual.R(),constraint.rollRange):target.R();
    const auto targetRpy=zyxEulerAngles(composeOf(refT,desired));
    const auto actualRpy=zyxEulerAngles(composeOf(refT,actual.R()));
    const auto dp=target.P()-actual.P();
    Eigen::Matrix<double,6,1> e=Eigen::Matrix<double,6,1>::Zero();
    double p2=0.0,r2=0.0;
    for(int i=0;i<3;++i) {
        if(constraint.constrainedDof[static_cast<std::size_t>(i)]) {
            e(i)=rr(0,i)*dp[0]+rr(1,i)*dp[1]+rr(2,i)*dp[2];
            p2+=e(i)*e(i);
        }
        if(constraint.constrainedDof[static_cast<std::size_t>(3+i)]) {
            e(3+i)=wrapAngle(targetRpy[static_cast<std::size_t>(i)]
                            -actualRpy[static_cast<std::size_t>(i)]);
            r2+=e(3+i)*e(3+i);
        }
    }
    posResidual=std::sqrt(p2);oriResidual=std::sqrt(r2);
    return e;
}

/**
 * @brief 真实FK中心差分构造J=-de/dq，再求固定阻尼最小二乘步长（A-16）。
 * @param request [in] 当前只读快照、TCP、目标及非空约束；借用且不跨调用保存。
 * @param q [in] 当前关节向量，转轴rad、移动轴m，顺序与CanonicalModel一致。
 * @param e [in] 同一q的目标减实际六维残差，m/rad。
 * @param failure [out] 差分FK失败时的定位文本；调用方转为既有设施失败值。
 * @return 成功为dq值；失败为nullopt，不以零步长掩盖设施错误。
 *
 * 固定每轴先+h再-h、h=1e-7 rad/m。差分整个残差而非几何角速度，
 * 同时包含参考系投影、Euler提取、目标族周期投影及掩码的导数。
 * 参数误差本身跨周，差分时也先wrap角差，避免±pi相邻值被误读为2pi跳跃。
 */
std::optional<Eigen::VectorXd> constrainedDampedLeastSquaresStep(
    const IkRequest& request,const std::vector<double>& q,
    const Eigen::Matrix<double,6,1>& e,std::string& failure)
{
    constexpr double kDifferenceStep=1e-7;
    const auto dof=q.size();
    Eigen::MatrixXd j=Eigen::MatrixXd::Zero(6,static_cast<Eigen::Index>(dof));
    const FkEvaluator fk;
    for(std::size_t column=0;column<dof;++column) {
        auto plus=q,minus=q;
        plus[column]+=kDifferenceStep;minus[column]-=kDifferenceStep;
        const auto fp=fk.evaluate(*request.modelView,request.tcp,plus);
        const auto fm=fk.evaluate(*request.modelView,request.tcp,minus);
        if(!fp.ok()||!fm.ok()) {
            failure="约束Jacobian差分FK失败，轴"+std::to_string(column)+": "
                +(!fp.ok()?fp.error().detail:fm.error().detail);
            return std::nullopt;
        }
        double unusedPosition=0.0,unusedOrientation=0.0;
        const auto ep=constrainedPoseError(request.targetInBase,fp.get().tcpInBase,
            *request.taskConstraint,unusedPosition,unusedOrientation);
        const auto em=constrainedPoseError(request.targetInBase,fm.get().tcpInBase,
            *request.taskConstraint,unusedPosition,unusedOrientation);
        for(int row=0;row<6;++row) {
            const double difference=row>=3?wrapAngle(ep(row)-em(row)):ep(row)-em(row);
            j(row,static_cast<Eigen::Index>(column))=-difference/(2.0*kDifferenceStep);
        }
    }
    Eigen::MatrixXd normal=j*j.transpose();
    const double lambda2=kIkDampingLambda*kIkDampingLambda;
    for(int i=0;i<6;++i) normal(i,i)+=lambda2;
    return Eigen::VectorXd(j.transpose()*normal.llt().solve(e));
}


/// 阻尼最小二乘步长 Δq＝J₃ᵀ(J₃J₃ᵀ＋λ²I₃)⁻¹e₃（λ＝kIkDampingLambda——设计
/// 默认，黄金锁定 T13；行优先雅可比显式映射拷贝——Eigen 默认列主序，
/// 存储序耦合禁用，Fk.cpp 同款取舍）。
/// ★ REUSE-T51：本函数自完整位姿迁移后收缩为**仅位置模式专用**——3 维
///   位置子块（Jacobian 前 3 行，A02 根因修复的任务子空间：姿态行不进
///   目标函数）；完整位姿普通模式迭代核已迁上游 JacobianIKSolver（v1 的
///   6 维正规方程链随之删除——migration §13.3 WP-04"删除该模式自写
///   DLS/正规方程"）。上游仅位置入口（ParallelIKSolver Target 掩码）的
///   消费被 runtime 视图权限审查阻断（GAP-IK-01 §1），本保留项归 WP-05
///   处置；保留理由＝该模式唯一迭代核，非隐藏 fallback。
Eigen::VectorXd dampedLeastSquaresStep(const std::vector<double>& jacobianRowMajor,
                                       std::size_t dof,
                                       const Eigen::Vector3d& e)
{
    constexpr std::size_t kTaskDim = 3U;  // 仅位置任务维度（A02）
    Eigen::MatrixXd J(static_cast<Eigen::Index>(kTaskDim),
                      static_cast<Eigen::Index>(dof));
    for (std::size_t r = 0; r < kTaskDim; ++r) {
        for (std::size_t c = 0; c < dof; ++c) {
            J(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)) =
                jacobianRowMajor[r * dof + c];
        }
    }
    // 正规方程矩阵 A＝J₃J₃ᵀ＋λ²I₃（3×3 对称正定——λ>0 保证可逆；LLT
    // 单线程固定算法——确定性）。
    Eigen::MatrixXd A = J * J.transpose();
    const double lambda2 = kIkDampingLambda * kIkDampingLambda;
    for (Eigen::Index i = 0; i < static_cast<Eigen::Index>(kTaskDim); ++i) {
        A(i, i) += lambda2;
    }
    const Eigen::VectorXd x = A.llt().solve(e);
    return J.transpose() * x;
}

/// 收敛判据（§5.3"每个收敛候选 q̂（残差 ≤ 两容差，逐项）"——逐项比较，
/// 不做合模长）。★ 任务维度感知（KIN-P2/A02）：仅位置任务的收敛判据
/// 只查位置残差（姿态残差是信息性记录、不参与判定——IkTaskSpace 注）；
/// 完整位姿任务保持双残差逐项语义——REUSE-T51 起该判据在完整位姿模式
/// 的调用点＝上游收敛后的独立复核（每初值一次，替代旧迭代内逐步判定；
/// 语义不变——双容差逐项）。
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

// ★ 审查 G12 收拢（WP-15-T20 第三批）：splitmix64/streamSeedFor 算法与
// 派生规则注释的唯一权威点＝src/SeedMix.hpp（此前四 TU 自持副本存在换
// 代漂移风险）。此处 using 引入匿名命名空间——本 TU 调用点零改动。
using kseed::splitmix64;
using kseed::streamSeedFor;

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
    // REUSE-T51 起仅位置任务走本单元 3 维阻尼最小二乘链（姿态行不进目
    // 标函数——阻尼最小二乘步内固定 3 维）；完整位姿普通模式
    // 迭代核迁移上游 JacobianIKSolver（见下方装配段），6 维任务子块随
    // 该模式退出本单元（v1 正规方程链已删）。

    // TCP 解析（KIN-NO-TCP 两分/FrameUnresolved——fail-fast 轨）。工具
    // 引用在 REUSE-T51 上游装配段复用（默认/命名 TCP 决定上游参考帧）。
    const runtime::CanonicalTool& ikTool = resolveTcpOrThrow(*request.modelView, request.tcp);

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

    // ---- 第 2 步：逐初值迭代（§5.3 时序）。迭代核按任务模式两分：
    //   完整位姿普通模式＝上游 JacobianIKSolver（REUSE-T51——下方装配）；
    //   仅位置/任务约束模式＝本单元阻尼最小二乘链（FK 复算经 FkEvaluator
    //   唯一计算点；周期查询取消探针）。----
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

    // ---- REUSE-T51 上游装配（完整位姿普通模式专用——migration §13.3
    // WP-04 行）。装配失败（锚缺失/设备未命中/自由度不符/参考帧未解析/
    // 上游设施异常）＝求解设施事实→facilityFailure 值面（不抛承诺——
    // F-746③ 同轨；禁止回退旧自写核作隐藏 fallback——KinTypes.hpp T17
    // 锚注）。阶段归 InitialFkEvaluation：装配不通则任何初值连起点位姿
    // 都不可评（与既有"初值起点 FK 首评失败"同位语义）。----
    const bool fullPosePlain = request.taskSpace == IkTaskSpace::FullPose
        && !request.taskConstraint.has_value();
    // 上游停判据（solveLocal normInf——位置分量 m＋EAA 分量 rad 混合单位
    // 的 6 维旋量无穷范数）：maxError=min(位置容差,姿态容差)。位置分量
    // ≤maxError≤位置容差恒成立；姿态转角≤√3·maxError 可能超姿态容差——
    // 上游停判据不承担验收语义，最终验收＝阶段①独立 FK 复算的双容差逐
    // 项判定（GAP-IK-01 §1"结束后以任务实际位置/姿态容差独立复核"）。
    const double upstreamMaxError = fullPosePlain
        ? std::min(request.positionTolerance, request.orientationTolerance)
        : 0.0;
    std::shared_ptr<const runtime::IRuntimeModelView> upstreamAnchor;
    rw::core::Ptr<const rw::models::SerialDevice> upstreamDevice;
    rw::core::Ptr<rw::invkin::JacobianIKSolver> upstreamSolver;
    rw::math::Transform3D<double> upstreamFoiTarget =
        runtime::detail::identityTransform3D();  // foi 系目标（装配段覆写）
    if (fullPosePlain) {
        try {
            upstreamAnchor = request.modelView->runtimeModelView();
            if (!upstreamAnchor) {
                return makeFacilityFailure(
                    IkFacilityFailureStage::InitialFkEvaluation, request, search,
                    "完整位姿上游求解装配失败：视图未绑定编译产物锚"
                    "（runtimeModelView()==nullptr——§9.2 @pre：上游迭代核"
                    "消费已编译 WorkCell，纯数值视图无设备可用）");
            }
            // 设备查找：deviceName 是名称端口生成物（R-4 纪律——不做拼名/
            // 扫描兜底）；自由度防御同 Fk.cpp（同一编译产物的两侧投影）。
            upstreamDevice = upstreamAnchor->workCell().findDevice(model.chain().deviceName);
            if (!upstreamDevice) {
                return makeFacilityFailure(
                    IkFacilityFailureStage::InitialFkEvaluation, request, search,
                    "完整位姿上游求解装配失败：设备名未命中编译产物（deviceName="
                        + model.chain().deviceName + "——快照 S8 交叉校验后不可达）");
            }
            if (static_cast<std::size_t>(upstreamDevice->getDOF()) != dof) {
                return makeFacilityFailure(
                    IkFacilityFailureStage::InitialFkEvaluation, request, search,
                    "完整位姿上游求解装配失败：设备自由度与快照模型不符（编译产物 "
                        + std::to_string(upstreamDevice->getDOF()) + " vs 规范链 "
                        + std::to_string(dof) + "——快照内部一致性防御）");
            }
            // 求解参考帧（foi）与 foi 系目标（与 Fk.cpp 的 TCP 消费面互逆
            // ——同一编译事实 REUSE-T13）：
            //   默认 TCP＝ObjectId 经名称端口解析的编译产物帧（帧已含偏置
            //   ——R-4 权威路径，Fk.cpp 6a 同源），目标即该帧系位姿；
            //   命名 TCP＝法兰（编译链终点 device->getEnd()），目标一次逆
            //   偏置复合回法兰系：T_flange_target＝T_tcp_target·tcpOffset⁻¹
            //   （Fk.cpp 命名 TCP 位姿＝法兰位姿·tcpOffset 的消费侧复合的
            //   逆运算；inverse/operator* 为 rw/math/Transform3D.hpp 头内
            //   模板——冒烟链接面安全）。
            const rw::kinematics::Frame* foi = nullptr;
            const bool defaultTcp = request.tcp.tcpKey.empty()
                || request.tcp.tcpKey == ikTool.localName;
            if (defaultTcp) {
                const auto resolved =
                    upstreamAnchor->nameResolver().resolveObjectId(ikTool.objectId);
                if (!resolved.ok()) {
                    return makeFacilityFailure(
                        IkFacilityFailureStage::InitialFkEvaluation, request,
                        search,
                        "完整位姿上游求解装配失败：默认 TCP 帧未解析（objectId="
                            + ikTool.objectId.toCanonical()
                            + "——名称端口权威路径未命中）");
                }
                foi = upstreamAnchor->workCell().findFrame(resolved.get().fullName);
                if (foi == nullptr) {
                    return makeFacilityFailure(
                        IkFacilityFailureStage::InitialFkEvaluation, request,
                        search,
                        "完整位姿上游求解装配失败：默认 TCP 帧未命中编译产物（"
                            + resolved.get().fullName + "）");
                }
                upstreamFoiTarget = request.targetInBase;
            } else {
                const auto named = std::find_if(
                    ikTool.namedTcpOffsets.begin(), ikTool.namedTcpOffsets.end(),
                    [&](const auto& entry) { return entry.first == request.tcp.tcpKey; });
                if (named == ikTool.namedTcpOffsets.end()) {
                    return makeFacilityFailure(
                        IkFacilityFailureStage::InitialFkEvaluation, request,
                        search,
                        "完整位姿上游求解装配失败：命名 TCP 偏置缺失（tcpKey="
                            + request.tcp.tcpKey + "——第 0 步解析后不可达，防御）");
                }
                foi = upstreamDevice->getEnd();
                upstreamFoiTarget = request.targetInBase * rw::math::inverse(named->second);
            }
            // 上游求解器：每请求构造一次（FKRange/JacobianCalculator 只绑
            // 结构——求解按传入 State 逐次计算）；构造用一次独立默认 State
            // （初值循环内每初值再取任务私有 State）。三个默认显式登记：
            //   SVD 伪逆步（上游最稳路径——JacobianIKSolver.hpp 类注）、
            //   不钳限位（钳制会扭曲收敛语义）、不查限位（限位评价归产品
            //   硬过滤②契约区间直比——§6.1，不复制上游 bounds 判定）。
            upstreamSolver = rw::core::Ptr<rw::invkin::JacobianIKSolver>(
                new rw::invkin::JacobianIKSolver(
                    upstreamDevice,
                    rw::core::Ptr<const rw::kinematics::Frame>(foi),
                    upstreamAnchor->makeState()));
            upstreamSolver->setSolverType(rw::invkin::JacobianIKSolver::SVD);
            upstreamSolver->setClampToBounds(false);
            upstreamSolver->setCheckJointLimits(false);
        } catch (const std::exception& e) {
            // 上游/锚设施异常转值面（rw::core::Exception 派生自
            // std::exception；solve 不抛承诺——F-746③ 口径）。
            return makeFacilityFailure(
                IkFacilityFailureStage::InitialFkEvaluation, request, search,
                std::string("完整位姿上游求解装配异常（设施事实，非调用方"
                            "契约违约）：") + e.what());
        }
    }

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

        std::vector<double> q;
        std::optional<PoseMetrics> metrics;  // 收敛候选的 FK 指标面（所在分支填充——2f 草稿）
        double posResidual = 0.0;
        double oriResidual = 0.0;
        std::uint32_t iterations = 0;
        bool isConverged = false;
        if (fullPosePlain) {
            // ---- REUSE-T51 上游分支：JacobianIKSolver solveLocal 单步循环
            // （migration §13.3 WP-04——迭代核唯一来源，不自写第二套步进）。
            try {
                // 每初值独立任务私有 State（§8.7/§9.2——WC 结构跨线程只读、
                // State 每任务；初值间零共享可变面）。
                rw::kinematics::State state = upstreamAnchor->makeState();
                // 初值入上游坐标：q_rw＝q_auth−zeroOffset（单点映射——
                // JointMapping.hpp；Fixed 关节不消耗 q——DOF 定义同 Fk.cpp）。
                rw::math::Q qRw(dof);
                {
                    std::size_t axis = 0;
                    for (const auto& j : joints) {
                        if (j.type == runtime::JointType::Fixed) {
                            continue;
                        }
                        qRw[axis] = jointmapping::qRuntimeFromAuthoritative(
                            j, request.initialValues[k][axis]);
                        ++axis;
                    }
                }
                upstreamDevice->setQ(qRw, state);
                // solveLocal(maxIter=1) 单步循环：每调用＝上游循环体恰一次
                // （normInf 停判＋至多一次 SVD 伪逆步）——与单次
                // solveLocal(maxIter=N) 轨迹逐位同一（本基线实证，测试钉），
                // 迭代数逐步实测（GAP-IK-01 §1：不能用配置上限伪造实际迭
                // 代数）；取消探针保持 §8.3 周期（初值起点已探，此处覆盖
                // 迭代内节奏；探针只决定提前返回——确定性不受时机影响）。
                while (iterations < request.iterationLimit) {
                    if (iterations % kIkCancellationProbeInterval == 0U
                        && request.cancellationProbe && request.cancellationProbe()) {
                        IkOutcome cancelledOut;
                        cancelledOut.cancelled = true;
                        cancelledOut.solutionSet.targetRef = request.targetRef;
                        cancelledOut.solutionSet.requestIdentity =
                            request.requestIdentity;
                        return cancelledOut;
                    }
                    if (upstreamSolver->solveLocal(upstreamFoiTarget,
                                                   upstreamMaxError, state, 1)) {
                        isConverged = true;  // 上游停判据满足——产品验收仍须下方独立复核
                        break;
                    }
                    ++iterations;
                }
                // 收敛解提取＋产品独立复核（正解运动学回代误差 oracle 面
                // ——GAP-IK-01 §1"优先原API，结束后以任务实际位置/姿态容
                // 差独立复核"：上游对不可达目标可误报成功）。无论上游停
                // 判还是预算耗尽都经本复核：预算耗尽而残差已入双容差时按
                // 产品验收放行——对齐既有引擎"第 budget 次更新后的复评收
                // 敛"预算语义（上限计更新数，末次复评免费）；不过判＝该
                // 初值未收敛（不产候选、不入过滤记录——与迭代预算耗尽同
                // 语义，保持 D-KIN-7 词表不扩）。
                // 收敛解回权威角：q_auth＝q_rw＋zeroOffset（单点逆映射——
                // JointMapping.hpp；产品解集/指标链恒权威 q——§5.1）。
                const rw::math::Q qUpstream = upstreamDevice->getQ(state);
                q.assign(dof, 0.0);
                {
                    std::size_t axis = 0;
                    for (const auto& j : joints) {
                        if (j.type == runtime::JointType::Fixed) {
                            continue;
                        }
                        q[axis] = jointmapping::qAuthoritativeFromRuntime(
                            j, qUpstream[axis]);
                        ++axis;
                    }
                }
                // 指标面（复用唯一 FK 计算点——数值与既有指标链同源）。
                Expected<PoseMetrics> mUp =
                    fkService.evaluate(*request.modelView, tcp, q);
                if (!mUp.ok()) {
                    return makeFacilityFailure(
                        IkFacilityFailureStage::IterationFkEvaluation,
                        request, search,
                        "initialValues[" + std::to_string(k)
                            + "] 上游收敛解 FK 复算失败："
                            + mUp.error().detail);
                }
                poseError(request.targetInBase, mUp.get().tcpInBase,
                          posResidual, oriResidual);
                isConverged = convergedForTask(
                    IkTaskSpace::FullPose, posResidual, oriResidual,
                    request.positionTolerance, request.orientationTolerance);
                if (isConverged) {
                    metrics = mUp.get();
                }
            } catch (const std::exception& e) {
                // 上游/锚设施异常转值面（rw::core::Exception 派生自
                // std::exception；非调用方契约违约、不虚构结局——F-746③
                // 口径）：初值起点异常＝起点不可评（InitialFkEvaluation），
                // 收敛后取值异常＝迭代面处置（IterationFkEvaluation）。
                return makeFacilityFailure(
                    isConverged
                        ? IkFacilityFailureStage::IterationFkEvaluation
                        : IkFacilityFailureStage::InitialFkEvaluation,
                    request, search,
                    "initialValues[" + std::to_string(k) + "] 上游求解异常："
                        + e.what());
            }
        } else {
            q = request.initialValues[k];
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

                while (true) {
                // 2b. 收敛判据（残差 ≤ 容差逐项——§5.3；任务维度感知：
                // PositionOnly 只查位置残差——A02）。
                // e 恒按 6 维产出（姿态残差仍如实计算供记录——PositionOnly
                // 下仅前 3 维进目标函数）。
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
                // 本分支仅位置模式（3 维任务子块——A02）；完整位姿普通模式
                // 已迁上游 JacobianIKSolver——REUSE-T51，6 维正规方程链删除）。
                Eigen::VectorXd dq;
                if (request.taskConstraint.has_value()) {
                    // A-16差分仍复用唯一FK；设施失效必须终止整次请求，不能假装无解。
                    std::string differenceFailure;
                    const auto step=constrainedDampedLeastSquaresStep(request,q,e,differenceFailure);
                    if (!step.has_value()) {
                        return makeFacilityFailure(IkFacilityFailureStage::IterationFkEvaluation,
                            request,search,differenceFailure);
                    }
                    dq=*step;
                } else {
                    // 仅位置模式沿用原几何 Jacobian 位置子块、阻尼和数值路径。
                    dq = dampedLeastSquaresStep(m.get().jacobian, dof, e.head(3));
                }
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
            if (isConverged) {
                metrics = m.get();  // 2f 草稿指标面（收敛构型 FK 全套输出）
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
        s.jointMargins = metrics->jointMargins;
        s.minimumJointMargin = metrics->minimumJointMargin;
        s.manipulability = metrics->manipulability;
        s.conditionNumber = metrics->conditionNumber;
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
