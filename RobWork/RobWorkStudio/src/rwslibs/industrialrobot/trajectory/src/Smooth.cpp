/**
 * @file   Smooth.cpp
 * @brief  简化/平滑算法的唯一实现翻译单元（§11.4 模型＋§15.7 管线；
 *         WP-16-T07 批——平滑半区）。
 *
 * 设计依据（头文件 Smooth.hpp 的设计依据此处不重复；本注登记实现补全）：
 *   - **DTB §5.4 实现补全/偏差登记（平滑半区三项）**：
 *     ①收缩因子 λ 序列＝2⁻ᵏ（k=0..maxIterations-1）：卡面 §15.7"迭代上限"
 *     未展开迭代对象——本实现取"段级切线向段斜率收缩"为迭代面（m_左/右(k)
 *     =Δ_k+λ·(d−Δ_k)，d 为中心差分；λ=1 完整五次 Hermite 样条〔C¹＋结点
 *     二阶导同值 C²〕；λ=0 每段线性精确重构→整条退化为折线——偏差单调不
 *     增，收敛性有构造保证；路径端点导数恒等于折线端斜率——§11.4"端点位
 *     置/姿态/导数边界不变"）；
 *     ②偏差估计探针密度＝每段 64 点（kDeviationProbeSamplesPerSpan）：
 *     几何保持验证需要数值估计五次多项式对折线的峰值偏差——该密度是**偏
 *     差估计的实现参数**（非复检协议数值，不属 P-06 零自设范围；复检协议
 *     数值仍零字面量），峰值漏检的安全网＝复检强制边（Recheck 以双上界
 *     重采样真实几何）；
 *     ③简化贪心带回溯：剔除成功后扫描位回退一格（新邻居组合下前一位置可
 *     能重新可剔）——终止性由序列单调缩短保证，确定性不受影响。
 *   - 简化判据的"替代几何"＝线性插值（§11.1 步骤①"删除可省路点"后被剔
 *     点由两保留点的连线替代——关节维逐轴 lerp；TCP 维经 FK 端口对
 *     "被剔点构型 vs 替代构型"求位姿位置差）。
 *   - 零变化判定（§11.1"几何或采样是否变化？——否——沿用既有复检结论"）：
 *     平滑曲线对折线的偏差位级为 0（共线路径的样条线性重构恒等式）时改
 *     判 NoChange——几何未变，调用方可沿用既有复检结论（零变化零复检）。
 *   - 需求 TRJ-04、NFR-COR-02/03（见 Smooth.hpp）；错误二分见 Errors.hpp。
 *
 * 集成模式条件源（Ptp.cpp 同款 gating）：Q/Transform3D 构造析构面消费
 * rw::math::Q 的库内虚析构符号——冒烟模式无框架库可链，不编译本 TU；
 * Smooth.hpp 的值类型/接口声明两模式皆可编译。
 */

#include <sdurws/ird/trajectory/Smooth.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/Provenance.hpp>   // core::SourcedValue/ValueProvenance（比较型素材值面）
#include <sdurws/ird/core/Units.hpp>        // core::UnitToken（比较型素材单位口径）
#include <sdurws/ird/trajectory/DiagCodes.hpp>  // TRJ-* 码常量（素材 reasonToken 唯一书写点）
#include <sdurws/ird/trajectory/Errors.hpp>     // TrajectoryError（fail-fast 载体）
#include <sdurws/ird/trajectory/KinematicsPort.hpp>  // FK 端口完整类型（evaluateFk 消费）

namespace sdurws::ird::trajectory {
namespace {

// =====================================================================
// 算法内部常量（实现参数——非协议数值，依据见文件头注登记②）
// =====================================================================

/// 几何保持偏差估计的每段探针密度（无量纲计数；五次多项式峰宽≥1/段，
/// 64 点/段的网格对峰值的欠估可忽略——登记②）。
inline constexpr std::size_t kDeviationProbeSamplesPerSpan = 64;

// =====================================================================
// 取消信号专用异常（文件私有——深栈 FK 调用把取消外逸到主流程的通道；
// 定义先于全部使用者——C++ 先声明后使用纪律）
// =====================================================================

/// 平滑内部取消信号载体（主流程捕获后转顶层 Canceled 态——UX-03 零素材；
/// 不继承 std::exception——它不是错误，绝不能被"捕获 std::exception 吞错"
/// 的路径误吞）。
struct SmoothCanceledSignal {};

// =====================================================================
// TCP 派生维检查（FK 端口消费——§15.4 evaluateFk 半区的平滑侧消费点）
// =====================================================================

/**
 * @brief 单构型的 TCP 位置（基座系 {B} 平移部，m）。
 *
 * 端口错误二分（§15.0/§15.4）：PortError＝适配失败（环境类）——平滑管
 * 线无素材轨承载它（几何保持检查是质量面非证据面），按"显性失败不吞错"
 * 以 TrajectoryError 转抛（token "trajectory/smooth/fk-port"——调用方可
 * 捕获后按端口错误面处置）；Canceled＝取消观测命中——以专用异常类型外
 * 逸出数值比较路径，由 smooth 主流程捕获转顶层 Canceled 态（UX-03）。
 */
rw::math::Vector3D<double> tcpPositionOrThrow(IKinematicsComputePort& port,
                                              const rw::math::Q& q,
                                              const CancelSignal& cancel)
{
    FkPortRequest fkRequest;
    fkRequest.q.assign(&q[0], &q[0] + q.size());
    fkRequest.cancel = cancel;
    const FkPortReply reply = port.evaluateFk(fkRequest);
    if (reply.status == KinPortCallStatus::Ok) {
        return reply.metrics.tcpInBase.P();
    }
    if (reply.status == KinPortCallStatus::Canceled) {
        throw SmoothCanceledSignal{};
    }
    // PortError——端口层失败不吞（错误语义见函数注）。
    throw TrajectoryError("trajectory/smooth/fk-port",
                          "FK 端口返回错误（TCP 派生维检查不可执行）: "
                              + reply.errorToken + " " + reply.errorMessage);
}

// =====================================================================
// 五次 Hermite 基函数（t∈[0,1] 归一化段参数——解析多项式，逐 t 直接求值）
// =====================================================================

/// 值基——端点 0 处取 1、端点 1 处取 0（h000）。
inline double hermiteH000(double t)
{
    return 1.0 + t * t * t * (-10.0 + t * (15.0 - 6.0 * t));
}

/// 一阶导基——端点 0 处导数 1（h100）。
inline double hermiteH100(double t)
{
    return t + t * t * t * (-6.0 + t * (8.0 - 3.0 * t));
}

/// 值基——端点 1 处取 1（h001）。
inline double hermiteH001(double t)
{
    const double t2 = t * t;
    return t2 * t * (10.0 + t * (-15.0 + 6.0 * t));
}

/// 一阶导基——端点 1 处导数 1（h101）。
inline double hermiteH101(double t)
{
    const double t2 = t * t;
    return t2 * t * (-4.0 + t * (7.0 - 3.0 * t));
}

// =====================================================================
// 段几何求值器（本文件两个 IPathGeometry 产品实现）
// =====================================================================

/**
 * @brief 把归一化参数 s 分解为（段索引 k，段内参数 t）。
 *
 * 分解规则：u = s·(n-1)；k = floor(u)；k 越界（s==1 时 u 恰为段数）则
 * 钳到最后一段并把 t 抬到 1——保证 sampleAt(1) 位级还原末路点（端点强
 * 制不变的求值侧承诺）。s 恰落结点时 t==0，自然取段起点结点（插值性）。
 *
 * @param s        [in] 归一化参数（已过 [0,1] 守卫）
 * @param spanCount [in] 段数（路点数−1，≥1）
 * @param k        [out] 段索引 ∈[0, spanCount-1]
 * @param t        [out] 段内参数 ∈[0,1]
 */
void decomposeParameter(double s, std::size_t spanCount, std::size_t& k, double& t)
{
    const double u = s * static_cast<double>(spanCount);
    k = static_cast<std::size_t>(u);
    if (k >= spanCount) {
        k = spanCount - 1;
        t = 1.0;  // s==1 末点——位级还原末路点
        return;
    }
    t = u - static_cast<double>(k);
}

/**
 * @brief 平滑产物曲线求值器（分段五次 Hermite——结点＝简化后路点，端点
 *        值/导数强制不变，λ=1 时结点二阶导恒 0→整体 C²）。
 *
 * 无状态纯计算（采样只读成员——并发只读安全）；确定性（解析多项式——
 * 同 s 位级等价输出，NFR-COR-02）。构造期一次冻结全部系数输入（路点＋段
 * 级左右导数表），采样期零可变状态。
 */
class QuinticHermiteGeometry final : public IPathGeometry {
public:
    /**
     * @brief 构造求值器（假定输入已过校验——工厂与平滑管线保证）。
     *
     * @param knots  [in] 插值结点（简化后路点；≥2 个同维度）
     * @param segTangents [in] 段级左右导数表（2(n−1) 个：段 k 的左右导数
     *                取 [2k]/[2k+1]；rad|m 每单位参数——λ=1 时为完整样条
     *                切线，λ<1 时贴向各段折线斜率，λ=0 恒等于段斜率）
     */
    QuinticHermiteGeometry(std::vector<rw::math::Q> knots,
                           std::vector<rw::math::Q> segTangents)
        : m_knots(std::move(knots)), m_segTangents(std::move(segTangents))
    {
    }

    /**
     * @brief 求参数 s 处构型（IPathGeometry 契约实现）。
     *
     * 步骤（复检细分采样的热路径，零分配）：
     *   1. s 越界拒绝（fail-fast——调用方违约，不静默钳位——NFR-COR-03）；
     *   2. 归一化参数→段索引/段内参数（decomposeParameter——端点位级还
     *      原的守卫点）；
     *   3. 五次 Hermite 四基加权逐轴合成（二阶导项恒 0——构造语义）。
     */
    rw::math::Q sampleAt(double s) const override
    {
        // 第 1 步：参数域守卫（NaN/越界一律拒绝——NaN 比较恒 false 会
        // 穿过 range 检查，须显式 isfinite）。
        if (!std::isfinite(s) || s < 0.0 || s > 1.0) {
            throw TrajectoryError("trajectory/smooth/geometry-param",
                                  "路径参数 s 必须在 [0,1] 内，实际: "
                                      + std::to_string(s));
        }
        // 第 2 步：参数→（段索引，段内参数）。
        std::size_t k = 0;
        double t = 0.0;
        decomposeParameter(s, m_knots.size() - 1, k, t);
        // 第 3 步：四基加权合成（h000·p0 + h100·m0 + h001·p1 + h101·m1；
        // 二阶导基项权重恒 0——见类注；m0/m1 取段级左右导数——λ 收缩语
        // 义的载体，见 buildTangents 注）。
        const rw::math::Q& p0 = m_knots[k];
        const rw::math::Q& p1 = m_knots[k + 1];
        const rw::math::Q& m0 = m_segTangents[2 * k];
        const rw::math::Q& m1 = m_segTangents[2 * k + 1];
        const double b000 = hermiteH000(t);
        const double b100 = hermiteH100(t);
        const double b001 = hermiteH001(t);
        const double b101 = hermiteH101(t);
        rw::math::Q out(p0.size());
        for (std::size_t j = 0; j < p0.size(); ++j) {
            out[j] = b000 * p0[j] + b100 * m0[j] + b001 * p1[j] + b101 * m1[j];
        }
        return out;
    }

private:
    /// 插值结点（简化后路点——构造期冻结，rad|m）。
    std::vector<rw::math::Q> m_knots;
    /// 段级左右导数表（2(n−1) 个——构造期冻结；rad|m 每单位参数）。
    std::vector<rw::math::Q> m_segTangents;
};

/**
 * @brief 线性折线求值器（makeLinearJointPathGeometry 的产品实现——均匀
 *        参数划分＋段内线性插值；无状态纯计算——并发只读安全）。
 */
class LinearJointPathGeometry final : public IPathGeometry {
public:
    explicit LinearJointPathGeometry(std::vector<rw::math::Q> waypoints)
        : m_waypoints(std::move(waypoints))
    {
    }

    /**
     * @brief 求参数 s 处构型（均匀划分——n 路点 n-1 等参子段；段内线性
     *        插值）。参数守卫与末点钳位语义同 QuinticHermiteGeometry。
     */
    rw::math::Q sampleAt(double s) const override
    {
        if (!std::isfinite(s) || s < 0.0 || s > 1.0) {
            throw TrajectoryError("trajectory/smooth/geometry-param",
                                  "路径参数 s 必须在 [0,1] 内，实际: "
                                      + std::to_string(s));
        }
        std::size_t k = 0;
        double t = 0.0;
        decomposeParameter(s, m_waypoints.size() - 1, k, t);
        const rw::math::Q& a = m_waypoints[k];
        const rw::math::Q& b = m_waypoints[k + 1];
        rw::math::Q out(a.size());
        for (std::size_t j = 0; j < a.size(); ++j) {
            out[j] = (1.0 - t) * a[j] + t * b[j];  // 逐轴线性（rad|m）
        }
        return out;
    }

private:
    /// 路点序列（构造期冻结——rad|m）。
    std::vector<rw::math::Q> m_waypoints;
};

// =====================================================================
// 校验与公共小工具
// =====================================================================

/**
 * @brief 请求前置校验（SmoothRequest 注的逐项落点；违约抛 TrajectoryError
 *        ——调用方契约违约 fail-fast，token "trajectory/smooth/..."）。
 *
 * 校验序固定（确定性——同一坏请求必报同一首错，NFR-COR-02）：
 *   1. 路点数 ≥2；2. 维度一致＋全分量有限；3. 必经下标严格升序且在界内；
 *   4. 双域容差有限且 >0；5. 迭代上限 ≥1。
 */
void validateSmoothRequest(const SmoothRequest& request)
{
    const std::size_t n = request.waypoints.size();
    if (n < 2) {
        throw TrajectoryError("trajectory/smooth/waypoints",
                              "候选路径至少 2 个路点（含端点），实际: "
                                  + std::to_string(n));
    }
    const std::size_t dof = request.waypoints.front().size();
    for (std::size_t i = 0; i < n; ++i) {
        const rw::math::Q& q = request.waypoints[i];
        if (q.size() != dof) {
            throw TrajectoryError("trajectory/smooth/waypoints",
                                  "路点维度不一致（路点 0 为 " + std::to_string(dof)
                                      + " 轴），路点 " + std::to_string(i) + " 为 "
                                      + std::to_string(q.size()) + " 轴");
        }
        for (std::size_t j = 0; j < dof; ++j) {
            if (!std::isfinite(q[j])) {
                throw TrajectoryError("trajectory/smooth/waypoints",
                                      "路点含非有限分量（rad|m），路点 "
                                          + std::to_string(i) + " 轴 "
                                          + std::to_string(j));
            }
        }
    }
    std::size_t previous = 0;
    for (std::size_t idx = 0; idx < request.mandatoryIndices.size(); ++idx) {
        const std::size_t mandatory = request.mandatoryIndices[idx];
        if (mandatory >= n) {
            throw TrajectoryError("trajectory/smooth/mandatory",
                                  "必经点下标越界（路点数 " + std::to_string(n)
                                      + "），实际: " + std::to_string(mandatory));
        }
        if (idx > 0 && mandatory <= previous) {
            throw TrajectoryError("trajectory/smooth/mandatory",
                                  "必经点下标必须严格升序且不重复，前 "
                                      + std::to_string(previous) + " 后 "
                                      + std::to_string(mandatory));
        }
        previous = mandatory;
    }
    if (!(request.smoothToleranceJoint > 0.0) || !std::isfinite(request.smoothToleranceJoint)) {
        throw TrajectoryError("trajectory/smooth/tolerance",
                              "关节维容差必须为有限正数（rad|m），实际: "
                                  + std::to_string(request.smoothToleranceJoint));
    }
    if (!(request.smoothToleranceTcp > 0.0) || !std::isfinite(request.smoothToleranceTcp)) {
        throw TrajectoryError("trajectory/smooth/tolerance",
                              "TCP 派生维容差必须为有限正数（m），实际: "
                                  + std::to_string(request.smoothToleranceTcp));
    }
    if (request.maxIterations < 1U) {
        throw TrajectoryError("trajectory/smooth/iterations",
                              "收缩重试上限必须 ≥1，实际: "
                                  + std::to_string(request.maxIterations));
    }
}

/**
 * @brief 失败素材构造（TRJ-06 定位载体；phase 恒 kPhaseSmooth——本批
 *        TrjTypes 词表第四值）。
 */
FailedSegmentRecord makeSmoothFailure(const std::string& reasonToken,
                                      const std::string& cause,
                                      const std::string& recommendedAction)
{
    FailedSegmentRecord record;
    record.segmentIndex = 0xFFFFFFFFu;  // 全轨迹级（平滑面不持段索引——段定位由调用方补绑）
    record.phaseToken = kPhaseSmooth;
    record.reasonToken = reasonToken;
    record.cause = cause;
    record.recommendedAction = recommendedAction;
    return record;
}

// =====================================================================
// 简化（§11.4——贪心逐点剔除，保留端点与必经点）
// =====================================================================

/**
 * @brief 单点可剔除判定（贪心剔除的判据面）。
 *
 * 判据：把路点 q_k 从折线中删除后，它由前后保留点的线性插值替代——
 * 替代构型 lerp(q_{k-1}, q_{k+1}, 0.5)（单点剔除时 k 恰在中点参数）。
 * 偏差：关节维逐轴 |q_k[j] − lerp[j]| ≤ smoothToleranceJoint；FK 端口
 * 在场时叠加 |tcp(q_k) − tcp(lerp)| ≤ smoothToleranceTcp（TCP 派生维
 * ——§11.4"剔除后几何偏差（关节维逐轴＋TCP 派生维）"）。
 *
 * 为什么单点贪心而非区间折叠：卡面"贪心逐点剔除"的字面语义——每步只
 * 尝试剔除当前一个点（剔除后序列缩短，后续点继续尝试），实现简单且确
 * 定性直观（从左到右单遍扫描）。
 */
bool canDropPoint(const std::vector<rw::math::Q>& points,
                  std::size_t k,
                  const SmoothRequest& request,
                  IKinematicsComputePort* fkPort,
                  const CancelSignal& cancel)
{
    // 替代构型：两保留点的中点（单点剔除——参数位置 0.5）。
    const rw::math::Q& a = points[k - 1];
    const rw::math::Q& b = points[k + 1];
    rw::math::Q replacement(a.size());
    for (std::size_t j = 0; j < a.size(); ++j) {
        replacement[j] = 0.5 * (a[j] + b[j]);  // 逐轴线性中点（rad|m）
    }
    const rw::math::Q& original = points[k];
    for (std::size_t j = 0; j < a.size(); ++j) {
        if (std::abs(original[j] - replacement[j]) > request.smoothToleranceJoint) {
            return false;  // 关节维超容差——不可剔
        }
    }
    // TCP 派生维（可选——端口缺席时检查 NotApplicable，不阻塞剔除判定；
    // §11.4 双域判据的 TCP 半区仅在能力在场时生效，偏差报告显式标记）。
    if (fkPort != nullptr) {
        const rw::math::Vector3D<double> tcpOriginal =
            tcpPositionOrThrow(*fkPort, original, cancel);
        const rw::math::Vector3D<double> tcpReplacement =
            tcpPositionOrThrow(*fkPort, replacement, cancel);
        if ((tcpOriginal - tcpReplacement).norm2() > request.smoothToleranceTcp) {
            return false;  // TCP 派生维超容差——不可剔
        }
    }
    return true;
}

// =====================================================================
// 平滑与几何保持验证（§11.4——五次 Hermite＋λ 收缩重试）
// =====================================================================

/**
 * @brief 构造段级左右导数表（五次 Hermite 的切线输入；λ 收缩语义）。
 *
 * 返回 2(n−1) 个导数：段 k 的左右导数分别取 segTangents[2k]/segTangents
 * [2k+1]。收缩语义（文件头注登记①的精确形式）：
 *   m_左(k) = Δ_k + λ·(d_k − Δ_k)；m_右(k) = Δ_k + λ·(d_{k+1} − Δ_k)
 * 其中 Δ_k＝段斜率（q_{k+1}−q_k——折线在该段的导数），d_i＝结点 i 的中心
 * 差分（d₀=Δ₀、d_i=(Δ_{i-1}+Δ_i)/2、d_n=Δ_{n-1}）。λ=1 时 m=d（完整五次
 * Hermite 样条——C¹ 且结点二阶导同值→C²）；λ=0 时 m=Δ_k（每段线性精确
 * 重构→整条退化为折线——收缩序列的收敛方向）。路径端点处 d₀=Δ₀/dₙ=Δₙ₋₁，
 * 端点导数 m 与 λ 无关恒等于折线端斜率——§11.4"端点导数边界不变"。
 */
std::vector<rw::math::Q> buildTangents(const std::vector<rw::math::Q>& knots, double lambda)
{
    const std::size_t n = knots.size();
    const std::size_t spanCount = n - 1;
    std::vector<rw::math::Q> segTangents;
    segTangents.reserve(spanCount * 2);
    for (std::size_t k = 0; k < spanCount; ++k) {
        const rw::math::Q delta = knots[k + 1] - knots[k];  // Δ_k（rad|m）
        // 结点 k 的中心差分 d_k（起点取折线端斜率）。
        const rw::math::Q dLeft = (k == 0)
                                      ? delta
                                      : (knots[k + 1] - knots[k - 1]) * 0.5;
        // 结点 k+1 的中心差分 d_{k+1}（末端取折线端斜率）。
        const rw::math::Q dRight = (k + 1 == n - 1)
                                       ? delta
                                       : (knots[k + 2] - knots[k]) * 0.5;
        segTangents.push_back(delta + (dLeft - delta) * lambda);   // m_左(k)
        segTangents.push_back(delta + (dRight - delta) * lambda);  // m_右(k)
    }
    return segTangents;
}

/**
 * @brief 度量平滑曲线对折线的几何保持偏差（一次 λ 尝试的验证面）。
 *
 * 度量协议：逐段在固定探针网格（64 点/段，段内参数 t=(j+1)/64——不含
 * 端点，端点处曲线与折线重合偏差恒 0）上取曲线构型与折线构型（线性
 * 插值），关节维逐轴最大绝对偏差＋TCP 位置最大偏差（FK 端口在场时）。
 * 探针密度依据见文件头注登记②。
 *
 * @return 关节维最大偏差（rad|m）与 TCP 维最大偏差（m；fkPort 空时恒 0
 *         且 tcpChecked=false——检查 NotApplicable）
 */
struct DeviationMeasurement {
    double jointAxisMax = 0.0;
    double tcpMax = 0.0;
    bool tcpChecked = false;
};

DeviationMeasurement measureDeviation(const std::vector<rw::math::Q>& knots,
                                      const std::vector<rw::math::Q>& tangents,
                                      IKinematicsComputePort* fkPort,
                                      const CancelSignal& cancel)
{
    DeviationMeasurement m;
    m.tcpChecked = (fkPort != nullptr);
    const std::size_t spanCount = knots.size() - 1;
    const QuinticHermiteGeometry curve(knots, tangents);
    for (std::size_t k = 0; k < spanCount; ++k) {
        for (std::size_t p = 1; p <= kDeviationProbeSamplesPerSpan; ++p) {
            const double t = static_cast<double>(p)
                                 / static_cast<double>(kDeviationProbeSamplesPerSpan);
            // 曲线构型（复用求值器解析多项式——与复检消费面同一实现点，
            // 避免"验证的几何≠交付的几何"）。
            const double s = (static_cast<double>(k) + t) / static_cast<double>(spanCount);
            const rw::math::Q sampled = curve.sampleAt(s);
            // 折线构型（段内线性插值——平滑前的基准几何）。
            rw::math::Q reference(knots[k].size());
            for (std::size_t j = 0; j < knots[k].size(); ++j) {
                reference[j] = (1.0 - t) * knots[k][j] + t * knots[k + 1][j];
            }
            for (std::size_t j = 0; j < reference.size(); ++j) {
                m.jointAxisMax = std::max(m.jointAxisMax,
                                          std::abs(sampled[j] - reference[j]));
            }
            // TCP 派生维（可选——端口缺席时跳过，偏差报告显式标记）。
            if (fkPort != nullptr) {
                const rw::math::Vector3D<double> tcpSampled =
                    tcpPositionOrThrow(*fkPort, sampled, cancel);
                const rw::math::Vector3D<double> tcpReference =
                    tcpPositionOrThrow(*fkPort, reference, cancel);
                m.tcpMax = std::max(m.tcpMax, (tcpSampled - tcpReference).norm2());
            }
        }
    }
    return m;
}

}  // namespace

// =====================================================================
// §15.7 平滑管线主流程
// =====================================================================

SmoothOutcome smooth(const SmoothRequest& request)
{
    // ---- 第 1 步：前置校验（调用方契约违约 fail-fast——token 见校验器）。
    validateSmoothRequest(request);

    SmoothOutcome outcome;

    // ---- 第 2 步：取消轮询（入口——命中即 Canceled，零素材，UX-03）。
    // FK 深栈外逸的取消以 SmoothCanceledSignal 专用异常在此统一转译。
    try {
        if (request.cancel && request.cancel()) {
            outcome.status = SmoothStatus::Canceled;
            return outcome;
        }

        // ---- 第 3 步：简化——贪心逐点剔除（保留端点与必经点；从左到右
        // 单遍扫描，剔除成功则序列缩短并原地继续）。
        std::vector<rw::math::Q> simplified = request.waypoints;
        std::vector<std::size_t> mandatory = request.mandatoryIndices;
        {
            std::size_t k = 1;
            while (k + 1 < simplified.size()) {
                // 取消轮询（逐点边界——§15.1 轮询纪律；命中即收尾）。
                if (request.cancel && request.cancel()) {
                    outcome.status = SmoothStatus::Canceled;
                    return outcome;
                }
                // 必经点跳过（保护集——§5.6 必经状态不可绕行，Via 才可剔）。
                const bool mandatoryHere =
                    std::binary_search(mandatory.begin(), mandatory.end(), k);
                if (mandatoryHere) {
                    ++k;
                    continue;
                }
                if (canDropPoint(simplified, k, request, request.fkPort, request.cancel)) {
                    // 剔除当前点（序列缩短——保留点 k-1 与 k+1 直接相邻）。
                    simplified.erase(simplified.begin()
                                     + static_cast<std::ptrdiff_t>(k));
                    // 必经保护集整体前移一位（被剔点之后的下标全部左移）。
                    for (std::size_t& idx : mandatory) {
                        if (idx > k) {
                            --idx;
                        }
                    }
                    // 回退一格（贪心回溯）：剔除改变了几何替代关系，前一
                    // 位置在新邻居组合下可能重新可剔（如共线序列剔中段后
                    // 首内部点回归可剔）——不回退会漏剔（确定性单遍+回溯，
                    // 终止性由序列单调缩短保证）。
                    if (k > 1) {
                        --k;
                    }
                    // 不前移 k——新到的点（原 k+1）同样接受剔除尝试（贪心）。
                } else {
                    ++k;
                }
            }
        }

        // ---- 第 4 步：零对象判定（路点 ≤2——仅剩端点）。样条对 2 结点
        // 的插值恒等于折线（四基线性重构恒等式——解析性质），平滑无几何
        // 效果 → NoChange（§11.1"零变化零复检"分支的静态形态）。
        if (simplified.size() <= 2) {
            outcome.status = SmoothStatus::NoChange;
            outcome.smoothPath = simplified;
            outcome.deviation.jointAxisMaxDeviation = 0.0;
            outcome.deviation.toleranceJoint = request.smoothToleranceJoint;
            outcome.deviation.tcpChecked = (request.fkPort != nullptr);
            outcome.deviation.tcpMaxDeviation = 0.0;
            outcome.deviation.toleranceTcp = request.smoothToleranceTcp;
            outcome.deviation.adoptedShrinkFactor = 0.0;
            return outcome;
        }

        // ---- 第 5~7 步：平滑＋几何保持验证＋λ 收缩重试。
        // λ 序列固定（2⁻ᵏ，登记①）——确定性重放锚（NFR-COR-02）。
        double lastJointDeviation = 0.0;
        double lastTcpDeviation = 0.0;
        double lastLambda = 0.0;
        for (std::uint32_t iteration = 0; iteration < request.maxIterations; ++iteration) {
            // 取消轮询（迭代边界——命中即 Canceled，零素材）。
            if (request.cancel && request.cancel()) {
                outcome.status = SmoothStatus::Canceled;
                return outcome;
            }
            const double lambda =
                std::ldexp(1.0, -static_cast<int>(iteration));  // λ=2⁻ᵏ
            const std::vector<rw::math::Q> tangents = buildTangents(simplified, lambda);
            const DeviationMeasurement m =
                measureDeviation(simplified, tangents, request.fkPort, request.cancel);
            lastJointDeviation = m.jointAxisMax;
            lastTcpDeviation = m.tcpMax;
            lastLambda = lambda;
            // 几何保持判定（§11.4——双域容差独立配置；TCP 维仅在能力在场
            // 时参与判定，缺席时不冒充通过/不伪造成分）。
            const bool jointOk = m.jointAxisMax <= request.smoothToleranceJoint;
            const bool tcpOk = (!m.tcpChecked) || m.tcpMax <= request.smoothToleranceTcp;
            if (jointOk && tcpOk) {
                // 达标。偏差位级为 0（共线路径的样条线性重构恒等式）→几何
                // 零变化 → 改判 NoChange（§11.1"零变化零复检"的实测形态：
                // 调用方可沿用既有复检结论，无需重复复检）。
                if (m.jointAxisMax == 0.0 && (!m.tcpChecked || m.tcpMax == 0.0)) {
                    outcome.status = SmoothStatus::NoChange;
                    outcome.smoothPath = simplified;
                    outcome.deviation.jointAxisMaxDeviation = 0.0;
                    outcome.deviation.toleranceJoint = request.smoothToleranceJoint;
                    outcome.deviation.tcpChecked = m.tcpChecked;
                    outcome.deviation.tcpMaxDeviation = 0.0;
                    outcome.deviation.toleranceTcp = request.smoothToleranceTcp;
                    outcome.deviation.adoptedShrinkFactor = lambda;
                    return outcome;
                }
                // 首个达标强度——采纳（Smoothed；复检强制边由调用方执行）。
                outcome.status = SmoothStatus::Smoothed;
                outcome.smoothPath = simplified;
                outcome.geometry =
                    std::make_shared<QuinticHermiteGeometry>(simplified, tangents);
                outcome.deviation.jointAxisMaxDeviation = m.jointAxisMax;
                outcome.deviation.toleranceJoint = request.smoothToleranceJoint;
                outcome.deviation.tcpChecked = m.tcpChecked;
                outcome.deviation.tcpMaxDeviation = m.tcpMax;
                outcome.deviation.toleranceTcp = request.smoothToleranceTcp;
                outcome.deviation.adoptedShrinkFactor = lambda;
                return outcome;
            }
        }

        // ---- 迭代耗尽仍未达标——作废（§11.4"偏差超容差→该次平滑作废"；
        // 产物＝简化后路点，偏差记录末次实测值供证据面构造比较型素材）。
        // 单次尝试（maxIterations==1）失败即 ToleranceViolated（"该次平滑
        // 作废"的字面分支）；多次重试全部失败为 GiveUpAfterRetries（"连
        // 续平滑失败"的迭代上限语义面——编排面据此累计 m 次计数）。
        outcome.status = (request.maxIterations == 1U) ? SmoothStatus::ToleranceViolated
                                                       : SmoothStatus::GiveUpAfterRetries;
        outcome.smoothPath = simplified;
        outcome.deviation.jointAxisMaxDeviation = lastJointDeviation;
        outcome.deviation.toleranceJoint = request.smoothToleranceJoint;
        outcome.deviation.tcpChecked = (request.fkPort != nullptr);
        outcome.deviation.tcpMaxDeviation = lastTcpDeviation;
        outcome.deviation.toleranceTcp = request.smoothToleranceTcp;
        outcome.deviation.adoptedShrinkFactor = lastLambda;
        // 失败素材（TRJ-06 定位——比较型三要素：实际偏差/容差/单位；关节
        // 维是失败主因面，TCP 维差异并入 cause 文案）。
        FailedSegmentRecord failure = makeSmoothFailure(
            std::string(kTrjLimitExceeded),
            "平滑几何保持失败：曲线对折线的关节维最大偏差 "
                + std::to_string(lastJointDeviation) + " 超出容差 "
                + std::to_string(request.smoothToleranceJoint)
                + "（rad|m；收缩重试 " + std::to_string(request.maxIterations)
                + " 次耗尽，末次强度 λ=" + std::to_string(lastLambda) + "）",
            "保持未平滑候选路径并按 TRJ-04 复检原路径；如需平滑请放宽 "
            "smoothTolerance 或细化路点后重试");
        core::ComparativeFields comparison;
        // 来源标记：DerivedReadOnly＋方法短标记——偏差是本域对平滑产物的
        // 派生对照量（非用户直输入；ValueProvenance 五类词表 DerivedReadOnly）。
        const auto provenance =
            core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {}, {},
                                        "smooth-deviation");
        // 单位 token（rad——core 注册表编译期冻结；find 失败属 core 注册表
        // 缺陷，Ptp.cpp axisUnitToken 同款 fail-fast 口径）。
        const auto radToken = core::UnitToken::find("rad");
        if (!radToken.has_value()) {
            throw TrajectoryError("trajectory/smooth/unit-token",
                                  "core 单位注册表缺少 rad token（环境缺陷，"
                                  "fail-fast——比较型素材单位口径不可用）");
        }
        comparison.actual.quantity =
            core::SourcedValue<double>::provided(lastJointDeviation, provenance);
        comparison.actual.unit = *radToken;
        comparison.expected.quantity =
            core::SourcedValue<double>::provided(request.smoothToleranceJoint, provenance);
        comparison.expected.unit = *radToken;
        failure.comparison = comparison;
        outcome.failure = std::move(failure);
        return outcome;
    } catch (const SmoothCanceledSignal&) {
        // FK 深栈外逸的取消——统一转顶层 Canceled（UX-03：零错误素材）。
        outcome.status = SmoothStatus::Canceled;
        outcome.smoothPath.clear();
        outcome.geometry.reset();
        return outcome;
    }
}

// =====================================================================
// 折线求值器工厂（Smooth.hpp 工厂声明的实现——未平滑路径的段几何）
// =====================================================================

std::shared_ptr<const IPathGeometry> makeLinearJointPathGeometry(
    const std::vector<rw::math::Q>& waypoints)
{
    // 结构守卫（与请求校验的路点半区同一违约同一 token——两处错误面一致）。
    if (waypoints.size() < 2) {
        throw TrajectoryError("trajectory/smooth/waypoints",
                              "折线至少 2 个路点，实际: " + std::to_string(waypoints.size()));
    }
    const std::size_t dof = waypoints.front().size();
    for (std::size_t i = 0; i < waypoints.size(); ++i) {
        if (waypoints[i].size() != dof) {
            throw TrajectoryError("trajectory/smooth/waypoints",
                                  "路点维度不一致（路点 0 为 " + std::to_string(dof)
                                      + " 轴），路点 " + std::to_string(i) + " 为 "
                                      + std::to_string(waypoints[i].size()) + " 轴");
        }
        for (std::size_t j = 0; j < dof; ++j) {
            if (!std::isfinite(waypoints[i][j])) {
                throw TrajectoryError("trajectory/smooth/waypoints",
                                      "路点含非有限分量（rad|m），路点 "
                                          + std::to_string(i) + " 轴 "
                                          + std::to_string(j));
            }
        }
    }
    return std::make_shared<LinearJointPathGeometry>(waypoints);
}

}  // namespace sdurws::ird::trajectory
