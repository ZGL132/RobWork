/**
 * @file   Ik.cpp
 * @brief  多初值 IK 求解实现（KIN-02）——请求校验、初值策略生成、阻尼
 *         最小二乘迭代、解析界限检查与五类结局组装（§5.3 时序）。
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

/// 解析 TCP 引用（空键＝canonical TCP；非空精确匹配 localName——违例
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
    if (!tcp.tcpKey.empty() && tcp.tcpKey != tool.localName) {
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

/// 阻尼最小二乘步长 Δq＝Jᵀ(JJᵀ＋λ²I₆)⁻¹e（λ＝kIkDampingLambda——设计
/// 默认，黄金锁定 T13；行优先雅可比显式映射拷贝——Eigen 默认列主序，
/// 存储序耦合禁用，Fk.cpp 同款取舍）。
Eigen::VectorXd dampedLeastSquaresStep(const std::vector<double>& jacobianRowMajor,
                                       std::size_t dof,
                                       const Eigen::Matrix<double, 6, 1>& e)
{
    Eigen::MatrixXd J(6, static_cast<Eigen::Index>(dof));
    for (std::size_t r = 0; r < 6; ++r) {
        for (std::size_t c = 0; c < dof; ++c) {
            J(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)) =
                jacobianRowMajor[r * dof + c];
        }
    }
    // 正规方程矩阵 A＝JJᵀ＋λ²I₆（6×6 对称正定——λ>0 保证可逆；LLT
    // 单线程固定算法——确定性）。
    Eigen::Matrix<double, 6, 6> A = J * J.transpose();
    const double lambda2 = kIkDampingLambda * kIkDampingLambda;
    for (int i = 0; i < 6; ++i) {
        A(i, i) += lambda2;
    }
    const Eigen::Matrix<double, 6, 1> x = A.llt().solve(e);
    return J.transpose() * x;
}

/// 收敛判据（§5.3"每个收敛候选 q̂（残差 ≤ 两容差，逐项）"——逐项比较，
/// 不做合模长）。
bool converged(double posResidual, double oriResidual,
               double posTol, double oriTol)
{
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
        // seed 派生确定性序列：第 k 个初值逐轴 u_k,i∈[0,1)（splitmix64
        // 按初值序→轴序派生——同 seed 同矩阵；混入 k 防逐轴重复序列）。
        out.reserve(count);
        for (std::uint32_t k = 0; k < count; ++k) {
            std::uint64_t state = seed * 0x100000000ull + k;  // 混入初值序
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

    if (targetDistance > bound.get().totalRadius) {
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
            // 输入已全量验证（q 有限/维度符、TCP 已解析）——FK 失败即
            // 内部不变量破坏（KIN-SOLVER-INTERNAL 轨——fail-fast 不静默）。
            throw std::logic_error(
                "IkSolver：迭代内 FK 复算失败（求解器内部错误——"
                "KIN-SOLVER-INTERNAL 语义锚，§5.5 fail-fast 轨）");
        }

        double posResidual = 0.0;
        double oriResidual = 0.0;
        std::uint32_t iterations = 0;
        bool isConverged = false;
        while (true) {
            // 2b. 收敛判据（残差 ≤ 两容差逐项——§5.3）。
            const Eigen::Matrix<double, 6, 1> e =
                poseError(request.targetInBase, m.get().tcpInBase,
                          posResidual, oriResidual);
            if (converged(posResidual, oriResidual, request.positionTolerance,
                          request.orientationTolerance)) {
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
            // 2d. 阻尼最小二乘更新（Δq＝Jᵀ(JJᵀ＋λ²I)⁻¹e——定序固定）。
            const Eigen::VectorXd dq = dampedLeastSquaresStep(
                m.get().jacobian, dof, e);
            for (std::size_t i = 0; i < dof; ++i) {
                q[i] += dq(static_cast<Eigen::Index>(i));
            }
            ++iterations;
            m = fkService.evaluate(*request.modelView, tcp, q);
            if (!m.ok()) {
                throw std::logic_error(
                    "IkSolver：迭代内 FK 复算失败（求解器内部错误——"
                    "KIN-SOLVER-INTERNAL 语义锚，§5.5 fail-fast 轨）");
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
                throw std::logic_error(
                    "IkSolver：残差复验 FK 复算失败（求解器内部错误——"
                    "KIN-SOLVER-INTERNAL 语义锚，§5.5 fail-fast 轨）");
            }
            // 复验残差（比较型量——覆写当次残差变量，供记录承载）。
            poseError(request.targetInBase, recheck.get().tcpInBase,
                      posResidual, oriResidual);
            if (!converged(posResidual, oriResidual, request.positionTolerance,
                           request.orientationTolerance)) {
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
        if (request.collisionSession != nullptr) {
            const IkCollisionVerdict verdict =
                request.collisionSession->evaluate(s.q);
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
