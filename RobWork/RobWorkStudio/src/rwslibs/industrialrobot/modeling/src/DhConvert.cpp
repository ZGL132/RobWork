/**
 * @file   DhConvert.cpp
 * @brief  DH↔显式转换器的实现——§7.4 无损展开、§7.5 五状态确定性求解、
 *         §7.6 编译链 FK 对照等价验证与权威切换域门。
 *
 * 设计依据：
 *   - units/modeling.md §7.4/§7.5/§7.6（三节原文的逐步落地——各函数内
 *     注释标注"算法第几步"与失败走向）、§9.4.7（接口契约）、§9.5（T09
 *     行三码——产码唯一经 DiagCodes.hpp 常量，禁字符串拼码）
 *   - REQUIREMENTS.md §25 附录 D 第 4/5 项（判定与对照容差——固定类，
 *     常量定义于 DhConvert.hpp 唯一书写点）
 *   - 任务契约 tasks/foundation/WP-13-T09.json acceptance 1～5
 *
 * 实现纪律（确定性——NFR-COR-01/02）：
 *   - 求解器为确定性 Levenberg-Marquardt：固定初值（单位参数＝中性恒等
 *     θ=d=a=α=0）、固定数值微分步长与迭代上限、固定阻尼界限与停滞判据；
 *     不读时钟/环境/locale——同输入逐位同解。以下"过程常数"是数值程序
 *     参数（只影响求解过程，不影响判定结论——判定只用附录 D 阈值），
 *     与工程阈值严格分属两类（AGENTS §2.5 确定性来源注记义务）。
 *   - 线性代数为自持实现（高斯消元＋部分主元）：不引第三方依赖（AGENTS
 *     §3 第三方纪律），规模 4n≤28（R1 链长上限——七轴模板）内精度充足；
 *     旋转矩阵一律逐元素构造（rw::math 模板头 header-only 纪律——不调用
 *     Rotation3D::identity()/EAA 构造等框架外联符号，冒烟两模式语义一致）。
 *
 * 线程安全：无共享可变状态（全部状态在栈上）——并发只读可重入。
 */

#include <sdurws/ird/modeling/DhConvert.hpp>

#include <sdurws/ird/modeling/DiagCodes.hpp>  // MDL-DH-* 三码常量（产码唯一书写点）

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace sdurws::ird::modeling {

// =====================================================================
// 稳定 token（switch 全枚举——新增值漏登记编译器告警）
// =====================================================================

std::string_view dhErrorCodeToken(DhErrorCode code) noexcept
{
    switch (code) {
        case DhErrorCode::ChainEmpty: return "ChainEmpty";
        case DhErrorCode::DegenerateBase: return "DegenerateBase";
    }
    return "unknown";  // 不可达（全枚举已覆盖——防御性兜底，io 同款口径）
}

std::string_view dhDeterminationToken(DhDetermination determination) noexcept
{
    switch (determination) {
        case DhDetermination::Exact: return "Exact";
        case DhDetermination::ExactNonUnique: return "ExactNonUnique";
        case DhDetermination::Approximate: return "Approximate";
        case DhDetermination::NotExpressible: return "NotExpressible";
        case DhDetermination::AnalysisFailed: return "AnalysisFailed";
    }
    return "unknown";
}

std::string_view dhConvergenceToken(DhConvergenceState state) noexcept
{
    switch (state) {
        case DhConvergenceState::Converged: return "Converged";
        case DhConvergenceState::Diverged: return "Diverged";
    }
    return "unknown";
}

bool DhConversionResult::operator==(const DhConversionResult& o) const
{
    return determination == o.determination && parameters == o.parameters
        && solutionSet == o.solutionSet && freeCoordinates == o.freeCoordinates
        && errorMetricE == o.errorMetricE && convergence == o.convergence
        && deviations == o.deviations;
}

bool EquivalenceReport::operator==(const EquivalenceReport& o) const
{
    return inputsValid == o.inputsValid && compileOk == o.compileOk
        && equivalent == o.equivalent
        && maxPositionDeviation == o.maxPositionDeviation
        && maxOrientationDeviation == o.maxOrientationDeviation
        && jointDeviations == o.jointDeviations && failureDetail == o.failureDetail;
}

namespace {

// =====================================================================
// 数值程序常数（求解过程参数——非工程阈值；判定只用附录 D 阈值）
// =====================================================================

/// 数值微分中心差分布长（对 θ/α rad 与 d/a m 同步长——量级同阶；过程常数）。
constexpr double kDerivativeStep = 1e-6;
/// LM 迭代上限（§7.5"固定迭代上限"的落值；过程常数）。
constexpr std::size_t kMaxIterations = 200;
/// LM 阻尼初值与界限（过程常数——界限防除零/无界循环）。
constexpr double kLambdaInit = 1e-3;
constexpr double kLambdaMin = 1e-12;
constexpr double kLambdaMax = 1e8;
/// 单次迭代的阻尼加重尝试上限（1e-3→1e8 需 11 次量级；40 充裕——过程
/// 常数；超过即线性求解失败＝Diverged，绝不无界重试）。
constexpr std::size_t kMaxDampingAttempts = 40;
/// 停滞判据：连续 kStallLimit 步无可接受下降→已达数值极小点＝收敛（§7.5
/// "固定收敛判据"的落值；过程常数）。
constexpr std::size_t kStallLimit = 3;
/// 收敛的步长下界（‖δ‖∞ ≤ 此值→收敛；过程常数）。
constexpr double kStepConvergenceBound = 1e-14;
/// 线性求解与秩判定的主元相对容差（相对矩阵最大幅值；过程常数——只影响
/// 数值秩的辨识，不参与附录 D 判定）。
constexpr double kPivotRelativeTolerance = 1e-8;
/// 解去重容差＝附录 D 第 5 项同尺度 1×10⁻⁹（rad/m）：两解逐坐标差全部
/// ≤此值＝"去重容差内无可辨识第二解"（§7.5 Exact 唯一性口径——阈值
/// 尺度唯一来自附录 D，不私设第二尺度）。
constexpr double kSolutionDeduplicationTolerance = 1e-9;

/// 规范分支判据的偏距容差（数值程序常数——吸收求解的浮点噪声，m；
/// 声明于常数区、供 canonicalForm 使用）。
constexpr double kBranchToleranceM = 1e-12;

// =====================================================================
// 基础数学辅助（逐元素构造——零框架外联符号，冒烟两模式语义一致）
// =====================================================================

/// π（实现内独立抄写——测试期望不引实现常量，反向亦然）。
constexpr double kPi = 3.14159265358979323846;

/// 单位阵旋转（逐元素——不用 Rotation3D::identity() 外联符号）。
rw::math::Rotation3D<double> identityRotation()
{
    return rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                        0.0, 1.0, 0.0,
                                        0.0, 0.0, 1.0);
}

/// 恒等变换（逐元素——同 runtime Description detail::identityTransform3D 纪律）。
rw::math::Transform3D<double> identityTransform()
{
    return rw::math::Transform3D<double>(rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                                         identityRotation());
}

/// R·v（旋转乘向量——逐元素三循环；R·v 运算符同为外联符号面，冒烟纪律）。
rw::math::Vector3D<double> rotVec(const rw::math::Rotation3D<double>& r,
                                 const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v[0] + r(0, 1) * v[1] + r(0, 2) * v[2],
        r(1, 0) * v[0] + r(1, 1) * v[1] + r(1, 2) * v[2],
        r(2, 0) * v[0] + r(2, 1) * v[1] + r(2, 2) * v[2]);
}

/// R·R（旋转乘旋转——逐元素三重循环：rw 的 Rotation3D::operator* 内经
/// multiply() 外联符号，冒烟模式不可链接——本单元全部矩阵合成一律走
/// 逐元素算术，两模式数学一致（BaseWorldTransform 同款纪律））。
rw::math::Rotation3D<double> rotMul(const rw::math::Rotation3D<double>& ra,
                                    const rw::math::Rotation3D<double>& rb)
{
    rw::math::Rotation3D<double> out;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < 3; ++k) { s += ra(i, k) * rb(k, j); }
            out(i, j) = s;
        }
    }
    return out;
}

/// T·T（变换乘变换——R 部逐元素乘、平移＝Ra·pb＋pa；冒烟纪律同上）。
rw::math::Transform3D<double> transformMul(const rw::math::Transform3D<double>& ta,
                                           const rw::math::Transform3D<double>& tb)
{
    const rw::math::Rotation3D<double> r = rotMul(ta.R(), tb.R());
    const rw::math::Vector3D<double> pb = tb.P();
    const rw::math::Vector3D<double> p = rotVec(ta.R(), pb) + ta.P();
    return rw::math::Transform3D<double>(p, r);
}

/// Rot_z(ψ)——绕 z 轴旋转 ψ rad（逐元素右手系）。
rw::math::Rotation3D<double> rotZ(double psi)
{
    const double c = std::cos(psi);
    const double s = std::sin(psi);
    return rw::math::Rotation3D<double>(c, -s, 0.0,
                                        s, c, 0.0,
                                        0.0, 0.0, 1.0);
}

/// Rot_x(α)——绕 x 轴旋转 α rad（逐元素右手系）。
rw::math::Rotation3D<double> rotX(double alpha)
{
    const double c = std::cos(alpha);
    const double s = std::sin(alpha);
    return rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                        0.0, c, -s,
                                        0.0, s, c);
}

/// Rodrigues 轴角旋转（axis 须为单位向量、angle 单位 rad）——逐元素公式
/// R = I + sinθ·[k]× ＋ (1−cosθ)·[k]×²。自持实现：rw 轴角构造为外联符号
/// （冒烟纪律），本函数只用标量算术（确定性同规——FK 对照两侧共用）。
rw::math::Rotation3D<double> axisAngleRotation(const rw::math::Vector3D<double>& axis,
                                               double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double kx = axis[0];
    const double ky = axis[1];
    const double kz = axis[2];
    return rw::math::Rotation3D<double>(
        c + t * kx * kx,      t * kx * ky - s * kz, t * kx * kz + s * ky,
        t * kx * ky + s * kz, c + t * ky * ky,      t * ky * kz - s * kx,
        t * kx * kz - s * ky, t * ky * kz + s * kx, c + t * kz * kz);
}

/// DH 单步变换 T_{i-1,i} = Rot_z(θ总)·Trans_z(d)·Trans_x(a)·Rot_x(α)
/// （§7.4 原文公式；θ总＝θ_offset＋zeroOffset——零位对齐的几何落点）。
rw::math::Transform3D<double> dhStepTransform(double thetaTotal, double d, double a,
                                              double alpha)
{
    // 乘积分解：Rot_z·Trans_z·Trans_x 的平移＝Rz(θ总)·(a,0,d)（旋转在左
    // ——平移被 Rot_z 携带），旋转部分＝Rz(θ总)·Rx(α)。运算序固定（确定性）。
    const rw::math::Vector3D<double> p = rotVec(rotZ(thetaTotal),
                                                rw::math::Vector3D<double>(a, 0.0, d));
    const rw::math::Rotation3D<double> r = rotMul(rotZ(thetaTotal), rotX(alpha));
    return rw::math::Transform3D<double>(p, r);
}

/// 向量是否逐分量有限（NaN/Inf 任一即 false——I-MDL-3 同口径）。
bool isFinite(const rw::math::Vector3D<double>& v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

/// DH 四元组是否全有限（I-MDL-3）。
bool isFinite(const DhParameters& dh)
{
    return std::isfinite(dh.thetaOffset) && std::isfinite(dh.d)
        && std::isfinite(dh.a) && std::isfinite(dh.alpha);
}

/// 变换是否有限（平移＋旋转九元素）。
bool isFinite(const rw::math::Transform3D<double>& t)
{
    if (!isFinite(t.P())) { return false; }
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            if (!std::isfinite(t.R()(i, j))) { return false; }
        }
    }
    return true;
}

/// 归一化单位向量（前置：输入非零有限——调用方保证；零向量返回原值，
/// 由调用方先做可归一化判定）。
rw::math::Vector3D<double> normalized(const rw::math::Vector3D<double>& v)
{
    const double n = v.norm2();
    if (!(n > 0.0)) { return v; }  // 不可归一化——调用方契约，防御性原样返回
    return rw::math::Vector3D<double>(v[0] / n, v[1] / n, v[2] / n);
}

/// 累乘链帧的 z 轴方向（§7.4"轴线 z_i＝T_{0,i}·(0,0,1)"，归一化输出）。
rw::math::Vector3D<double> zAxisOf(const rw::math::Transform3D<double>& t)
{
    const rw::math::Rotation3D<double>& r = t.R();
    return normalized(rw::math::Vector3D<double>(r(0, 2), r(1, 2), r(2, 2)));
}

/// 两单位向量的方向角偏差（rad）——atan2(|a×b|, a·b)：精确处理近 0/近 π。
double axisAngleBetween(const rw::math::Vector3D<double>& a,
                        const rw::math::Vector3D<double>& b)
{
    return std::atan2(a.cross(b).norm2(), a.dot(b));
}

/// 旋转矩阵的姿态偏差角（rad）：s=‖vee(Rrel−Rrelᵀ)‖/2（=sinθ）、
/// c=(tr−1)/2（=cosθ）→atan2——近 0 精确（acos 在小角区病态，不用）。
double rotationAngleBetween(const rw::math::Rotation3D<double>& ra,
                            const rw::math::Rotation3D<double>& rb)
{
    // Rrel = RAᵀ·RB（转置手工逐元素——Rotation3D::inverse 为外联符号面，
    // 冒烟纪律；正交阵逆＝转置）。
    rw::math::Rotation3D<double> rat;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            rat(i, j) = ra(j, i);
        }
    }
    const rw::math::Rotation3D<double> rel = rotMul(rat, rb);
    const double s = std::sqrt(
        (rel(2, 1) - rel(1, 2)) * (rel(2, 1) - rel(1, 2))
        + (rel(0, 2) - rel(2, 0)) * (rel(0, 2) - rel(2, 0))
        + (rel(1, 0) - rel(0, 1)) * (rel(1, 0) - rel(0, 1))) / 2.0;
    const double c = (rel(0, 0) + rel(1, 1) + rel(2, 2) - 1.0) / 2.0;
    return std::atan2(s, c);
}

/// 数值的稳定文本形态（%.17g——位级可往返；诊断 cause 文案用，无 locale）。
std::string formatDouble(double v)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

// =====================================================================
// 求解域数据结构（匿名命名空间内部——R-2 不跨单元暴露）
// =====================================================================

/**
 * @brief 求解目标：显式权威链的逐关节累积原点位置与 z 轴方向（§7.5 第二
 *        阶"目标＝DH 链重建的轴线/原点 vs 显式权威轴线/原点"的左半——
 *        显式侧按同一累积规则构造，保证两侧可比）。
 */
struct SolverTargets {
    std::vector<rw::math::Vector3D<double>> originPositions;  ///< T_{0,i} 原点（m）
    std::vector<rw::math::Vector3D<double>> axisDirections;   ///< z_i 单位向量（无量纲）
    std::vector<double> zeroOffsets;                          ///< 逐关节零位偏置（rad——固定输入）
    std::size_t size() const noexcept { return originPositions.size(); }
};

/// 参数向量（x[4i+k]，k=0:θ_offset 1:d 2:a 3:α）→ 逐关节 DH 参数（链序）。
std::vector<DhParameters> parametersOf(const std::vector<double>& x)
{
    std::vector<DhParameters> out;
    out.reserve(x.size() / 4);
    for (std::size_t i = 0; i + 3 < x.size(); i += 4) {
        DhParameters dh;
        dh.thetaOffset = x[i];
        dh.d = x[i + 1];
        dh.a = x[i + 2];
        dh.alpha = x[i + 3];
        out.push_back(dh);
    }
    return out;
}

/// 逐关节 DH 参数 → 参数向量（inverse of parametersOf）。
std::vector<double> flattenParameters(const std::vector<DhParameters>& params)
{
    std::vector<double> x;
    x.reserve(params.size() * 4);
    for (const DhParameters& dh : params) {
        x.push_back(dh.thetaOffset);
        x.push_back(dh.d);
        x.push_back(dh.a);
        x.push_back(dh.alpha);
    }
    return x;
}

/**
 * @brief 残差向量（§7.5 目标函数——确定性固定运算序）。
 *
 * 重建规则与 dhToExplicit 逐位同式（单一语义源——展开/求解/等价验证三面
 * 共用，NFR-MNT-04 精神）：originRec_i = Rot_z(θ_i＋q0_i)·Trans_z(d_i)·
 * Trans_x(a_i)·Rot_x(α_i)；T_{0,i} = Π originRec_k；残差每关节六分量：
 *   [T_{0,i}.p − P_i]（原点位置，m）＋ [cross(z_i^rec, A_i)]（轴偏差的
 *   垂直分量＝sin(角偏差)·法向——近解区线性度好，最小二乘收敛快）。
 */
std::vector<double> residualOf(const std::vector<double>& x, const SolverTargets& targets)
{
    std::vector<double> r;
    r.reserve(targets.size() * 6);
    rw::math::Transform3D<double> acc = identityTransform();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        // 第 i 步：重建相对变换并累乘（与 expandDhChain 同式——含 zeroOffset）。
        const rw::math::Transform3D<double> step = dhStepTransform(
            x[4 * i] + targets.zeroOffsets[i], x[4 * i + 1], x[4 * i + 2], x[4 * i + 3]);
        acc = transformMul(acc, step);
        // 原点位置残差（3 分量，m）。
        r.push_back(acc.P()[0] - targets.originPositions[i][0]);
        r.push_back(acc.P()[1] - targets.originPositions[i][1]);
        r.push_back(acc.P()[2] - targets.originPositions[i][2]);
        // 轴方向残差（3 分量——cross(z_rec, A_i)）。
        const rw::math::Vector3D<double> cz = zAxisOf(acc).cross(targets.axisDirections[i]);
        r.push_back(cz[0]);
        r.push_back(cz[1]);
        r.push_back(cz[2]);
    }
    return r;
}

/// 残差向量的平方和（代价；非有限→NaN 传播由调用方检测）。
double costOf(const std::vector<double>& r)
{
    double c = 0.0;
    for (const double v : r) { c += v * v; }
    return c;
}

/**
 * @brief 高斯消元解线性方程组（部分主元；n×n 降维内精度充足）。
 * @param a [in] 行主序 n×n 系数（函数内消毁）
 * @param b [in] 右端 n 维（函数内消毁）
 * @param n [in] 阶
 * @param relativeTol [in] 奇异判定的主元相对容差（相对 ‖A‖∞ 最大幅值）
 * @param out [out] 解向量（成功时）
 * @return true＝求得唯一解；false＝奇异/近奇异（调用方加大阻尼重试）
 */
bool solveLinearSystem(std::vector<double> a, std::vector<double> b, std::size_t n,
                       double relativeTol, std::vector<double>& out)
{
    // 主元判据的绝对阈值＝系数矩阵最大绝对值×相对容差（确定性——比较序固定）。
    double maxAbs = 0.0;
    for (const double v : a) { maxAbs = std::max(maxAbs, std::abs(v)); }
    const double pivotTol = maxAbs * relativeTol;
    for (std::size_t col = 0; col < n; ++col) {
        // 部分主元：当前列在剩余行中选最大幅值行（确定性比较：严格大于才换序）。
        std::size_t pivot = col;
        double best = std::abs(a[col * n + col]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double v = std::abs(a[row * n + col]);
            if (v > best) {
                best = v;
                pivot = row;
            }
        }
        if (!(best > pivotTol)) { return false; }  // 近奇异——主元不足
        if (pivot != col) {
            for (std::size_t j = 0; j < n; ++j) { std::swap(a[col * n + j], a[pivot * n + j]); }
            std::swap(b[col], b[pivot]);
        }
        // 消元（固定运算序——行优先、逐列）。
        for (std::size_t row = col + 1; row < n; ++row) {
            const double factor = a[row * n + col] / a[col * n + col];
            if (factor == 0.0) { continue; }
            for (std::size_t j = col; j < n; ++j) { a[row * n + j] -= factor * a[col * n + j]; }
            b[row] -= factor * b[col];
        }
    }
    // 回代（列逆序固定）。
    out.assign(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double s = b[i];
        for (std::size_t j = i + 1; j < n; ++j) { s -= a[i * n + j] * out[j]; }
        out[i] = s / a[i * n + i];
    }
    return true;
}

/**
 * @brief 数值雅可比（中心差分，固定步长；m×4n 行主序）。
 *
 * 中心差分而非前向：截断误差 O(h²)——同一步长下精度高一个量级，改善
 * 1×10⁻⁹ m/rad 判定区的收敛质量（过程选择，不影响判定语义）。
 */
std::vector<double> numericJacobian(const std::vector<double>& x,
                                    const SolverTargets& targets)
{
    const std::vector<double> r0 = residualOf(x, targets);
    const std::size_t m = r0.size();
    const std::size_t n = x.size();
    std::vector<double> j(m * n, 0.0);
    std::vector<double> xa = x;
    std::vector<double> xb = x;
    for (std::size_t jCol = 0; jCol < n; ++jCol) {
        xa = x;
        xb = x;
        xa[jCol] += kDerivativeStep;
        xb[jCol] -= kDerivativeStep;
        const std::vector<double> ra = residualOf(xa, targets);
        const std::vector<double> rb = residualOf(xb, targets);
        for (std::size_t iRow = 0; iRow < m; ++iRow) {
            j[iRow * n + jCol] = (ra[iRow] - rb[iRow]) / (2.0 * kDerivativeStep);
        }
    }
    return j;
}

/**
 * @brief 确定性 Levenberg-Marquardt 求解（§7.5 第二阶"确定性非线性最小
 *        二乘"的落位）。
 *
 * @param targets     [in] 求解目标（显式侧累积链几何）
 * @param x0          [in] 初值（固定＝单位参数；钉住坐标的当前值）
 * @param optimizable [in] 逐坐标可优化开关（false＝钉住不动——退化族
 *                    字典序定值的约束重解通道）
 * @return status＝Converged（停滞判据/步长判据触发——达到数值极小点）
 *         ｜Diverged（NaN/Inf/线性求解反复失败——AnalysisFailed 伴随态）；
 *         x＝终值参数向量
 *
 * 收敛判据（固定——§7.5）：连续 kStallLimit 步无可接受下降，或步长
 * ‖δ‖∞ ≤ 1e-14。迭代耗尽仍未触发判据＝"不收敛"（预算内未达稳定点）→
 * Diverged——MDL-10"求解器数值失败（不收敛/发散/资源异常）"原文。
 */
struct LmResult {
    DhConvergenceState status = DhConvergenceState::Diverged;
    std::vector<double> x;
};

LmResult lmSolve(const SolverTargets& targets, std::vector<double> x0,
                 const std::vector<bool>& optimizable)
{
    const std::size_t n = x0.size();
    const std::size_t m = targets.size() * 6;

    std::vector<double> r = residualOf(x0, targets);
    double cost = costOf(r);
        if (!std::isfinite(cost)) {
#ifdef IRD_DH_DEBUG
            std::fprintf(stderr, "[dh-debug] init cost nonfinite\n");
#endif
            return {DhConvergenceState::Diverged, x0};  // 初值即溢出——发散
        }

    double lambda = kLambdaInit;
    std::size_t stall = 0;
    for (std::size_t iter = 0; iter < kMaxIterations; ++iter) {
        // ---- 步 1：数值雅可比与正规方程系数（固定运算序）----
        const std::vector<double> j = numericJacobian(x0, targets);
        std::vector<double> jtj(n * n, 0.0);
        std::vector<double> jtr(n, 0.0);
        for (std::size_t iRow = 0; iRow < m; ++iRow) {
            for (std::size_t jc = 0; jc < n; ++jc) {
                const double v = j[iRow * n + jc];
                if (v == 0.0) { continue; }  // 稀疏跳零——值恒等，仅省乘法
                jtr[jc] += v * r[iRow];
                for (std::size_t kc = jc; kc < n; ++kc) {
                    jtj[jc * n + kc] += v * j[iRow * n + kc];
                }
            }
        }
        // 对称补全（上三角已累计——含自身转置项，确定性）。
        for (std::size_t jc = 0; jc < n; ++jc) {
            for (std::size_t kc = 0; kc < jc; ++kc) { jtj[jc * n + kc] = jtj[kc * n + jc]; }
        }
        // 有限性前置守卫：正规方程含 NaN/Inf（雅可比/残差溢出——数值
        // 失败形态之一）即终止为 Diverged（AnalysisFailed——MDL-10），
        // 绝不带非有限值进入求解（内层阻尼重试无界化的根源防线）。
        bool finiteSystem = true;
        for (std::size_t k = 0; finiteSystem && k < n; ++k) {
            finiteSystem = std::isfinite(jtr[k]) && std::isfinite(jtj[k * n + k]);
        }
        if (!finiteSystem) {
            return {DhConvergenceState::Diverged, x0};
        }
        // ---- 步 2：阻尼线搜索（λ 加速失败回退——有界尝试，确定性）----
        std::vector<double> delta;
        bool solved = false;
        for (std::size_t attempt = 0; attempt < kMaxDampingAttempts; ++attempt) {
            // LM 阻尼矩阵＝λ·diag(JᵀJ)（对角占优缩放；零对角元兜底 1——
            // 防全零行（钉住坐标列恒零）致奇异）。
            std::vector<double> damped = jtj;
            for (std::size_t k = 0; k < n; ++k) {
                const double diag = jtj[k * n + k];
                damped[k * n + k] = diag + lambda * (diag > 0.0 ? diag : 1.0);
            }
            std::vector<double> negJtr(n, 0.0);
            for (std::size_t k = 0; k < n; ++k) { negJtr[k] = -jtr[k]; }
            if (solveLinearSystem(std::move(damped), std::move(negJtr), n,
                                  kPivotRelativeTolerance, delta)) {
                solved = true;
                break;
            }
            lambda = std::min(lambda * 10.0, kLambdaMax);  // 奇异→加大阻尼重试
        }
        if (!solved) {
            return {DhConvergenceState::Diverged, x0};  // 线性求解反复失败——发散
        }
        // 步长有限性守卫（奇异系统的病态解——非有限步长＝发散）。
        for (std::size_t k = 0; k < n; ++k) {
            if (!std::isfinite(delta[k])) {
                return {DhConvergenceState::Diverged, x0};
            }
        }
        // ---- 步 3：候选步评估（接受/拒绝——固定判据）----
        std::vector<double> xNew = x0;
        double maxStep = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            if (!optimizable[k]) { continue; }  // 钉住坐标不动（字典序定值约束）
            xNew[k] += delta[k];
            maxStep = std::max(maxStep, std::abs(delta[k]));
        }
        if (!std::isfinite(costOf(residualOf(xNew, targets)))) {
            return {DhConvergenceState::Diverged, x0};  // 步进溢出——发散
        }
        const std::vector<double> rNew = residualOf(xNew, targets);
        const double costNew = costOf(rNew);
        if (!std::isfinite(costNew)) {
            return {DhConvergenceState::Diverged, x0};
        }
        if (costNew < cost) {
            // 接受：代价下降（严格比较——确定性）。
            x0 = std::move(xNew);
            r = std::move(rNew);
            cost = costNew;
            lambda = std::max(lambda / 10.0, kLambdaMin);
            stall = 0;
            if (maxStep <= kStepConvergenceBound || cost <= 0.0) {
                return {DhConvergenceState::Converged, x0};  // 步长收敛
            }
        } else {
            // 拒绝：加大阻尼收缩步长；连续停滞→数值极小点＝收敛。
            lambda = std::min(lambda * 10.0, kLambdaMax);
            if (++stall >= kStallLimit) {
                return {DhConvergenceState::Converged, x0};
            }
        }
    }
    // 迭代预算耗尽仍未触发收敛判据＝不收敛（MDL-10"不收敛/发散"族——
    // AnalysisFailed，不产出语义结论）。
    return {DhConvergenceState::Diverged, x0};
}

/**
 * @brief 解处数值雅可比的列秩与自由列（Exact/ExactNonUnique 的唯一性判据
 *        ——§7.5"解唯一（在去重容差内无可辨识第二解）"的可执行形态）。
 *
 * 算法：列序贪心消元（列按字典序处理——自由列的发现序即字典序）；主元
 * 相对容差见 kPivotRelativeTolerance（近零列＝该坐标的残差敏感度≈0＝局部
 * 自由方向）。秩亏⟺存在连续解族⟺ExactNonUnique。
 *
 * @return (rank, freeColumns)——freeColumns 升序（字典序）
 */
std::pair<std::size_t, std::vector<std::size_t>> rankAndFreeColumns(
    const std::vector<double>& jacobian, std::size_t m, std::size_t n)
{
    std::vector<double> a = jacobian;  // 消元工作副本
    double maxAbs = 0.0;
    for (const double v : a) { maxAbs = std::max(maxAbs, std::abs(v)); }
    const double pivotTol = maxAbs * kPivotRelativeTolerance;

    std::vector<std::size_t> freeColumns;
    std::size_t rank = 0;
    std::size_t nextRow = 0;
    for (std::size_t col = 0; col < n; ++col) {
        // 在剩余行中找该列最大主元（确定性——严格大于才换序）。
        std::size_t pivot = m;
        double best = pivotTol;  // 低于容差＝无主元→自由列
        for (std::size_t row = nextRow; row < m; ++row) {
            const double v = std::abs(a[row * n + col]);
            if (v > best) {
                best = v;
                pivot = row;
            }
        }
        if (pivot == m) {
            freeColumns.push_back(col);  // 该列无线性独立方向——自由
            continue;
        }
        if (pivot != nextRow) {
            for (std::size_t jc = 0; jc < n; ++jc) {
                std::swap(a[nextRow * n + jc], a[pivot * n + jc]);
            }
        }
        // 消去下方行对该列的分量（固定序）。
        for (std::size_t row = nextRow + 1; row < m; ++row) {
            const double factor = a[row * n + col] / a[nextRow * n + col];
            if (factor == 0.0) { continue; }
            for (std::size_t jc = col; jc < n; ++jc) {
                a[row * n + jc] -= factor * a[nextRow * n + jc];
            }
        }
        ++rank;
        ++nextRow;
    }
    return {rank, freeColumns};
}

/**
 * @brief 判定解的逐关节逐项偏差（附录 D 第 5 项 C3——重建链 vs 目标链）。
 *
 * 与 residualOf 同式的重建（单一语义源）；两条独立判定：轴线方向角
 * （rad）与原点位置（m）——逐关节逐项，不用总和阈值（C3 明文）。
 */
std::vector<DhJointDeviation> deviationsOf(const std::vector<double>& x,
                                           const SolverTargets& targets)
{
    std::vector<DhJointDeviation> out;
    out.reserve(targets.size());
    rw::math::Transform3D<double> acc = identityTransform();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const rw::math::Transform3D<double> step = dhStepTransform(
            x[4 * i] + targets.zeroOffsets[i], x[4 * i + 1], x[4 * i + 2], x[4 * i + 3]);
        acc = transformMul(acc, step);
        DhJointDeviation dev;
        dev.jointIndex = i;
        dev.axisAngleDeviation = axisAngleBetween(zAxisOf(acc), targets.axisDirections[i]);
        dev.originPositionDeviation = (acc.P() - targets.originPositions[i]).norm2();
        out.push_back(dev);
    }
    return out;
}

/// 逐关节逐项是否全部满足附录 D 第 5 项上界（判定与 E 度量的分界——C3）。
bool withinExactTolerance(const std::vector<DhJointDeviation>& devs)
{
    for (const DhJointDeviation& dev : devs) {
        if (dev.axisAngleDeviation > kDhExactAxisAngleToleranceRad
            || dev.originPositionDeviation > kDhExactOriginPositionToleranceM) {
            return false;
        }
    }
    return true;
}

/// E 度量＝Σᵢ(轴线角偏差[rad]＋原点位置偏差[m])——仅 Approximate 的呈现
/// 指标（MDL-10/C3 明文"不参与判定"）。
double errorMetricEOf(const std::vector<DhJointDeviation>& devs)
{
    double e = 0.0;
    for (const DhJointDeviation& dev : devs) {
        e += dev.axisAngleDeviation + dev.originPositionDeviation;
    }
    return e;
}

/// 两参数向量逐坐标最大差（去重判据——kSolutionDeduplicationTolerance 的
/// 比较面；长度不等＝不可比，返回无穷）。
double maxAbsDiff(const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() != b.size()) { return std::numeric_limits<double>::infinity(); }
    double d = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) { d = std::max(d, std::abs(a[i] - b[i])); }
    return d;
}

/// 角度规范化的稳定包装（wrap 到 (−π,π]——atan2 构造，确定性；应用于
/// 判定结果的 θ_offset/α 输出面：几何等价的 2π 周期解规范化为同一代表，
/// 保证 roundtrip 参数级比对的确定性）。
double wrapPi(double angle)
{
    if (!std::isfinite(angle)) { return angle; }
    return std::atan2(std::sin(angle), std::cos(angle));
}

/// 角度族坐标是否需要包装（k=0:θ_offset、k=3:α——rad 量纲；d/a 为 m 不包装）。
bool isAngleCoordinate(std::size_t k)
{
    return k == 0U || k == 3U;
}

/// Rᵀ·v（转置乘向量——Rotation3D::inverse 的逐元素替身，冒烟纪律）。
rw::math::Vector3D<double> rotateTranspose(const rw::math::Rotation3D<double>& r,
                                           const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v[0] + r(1, 0) * v[1] + r(2, 0) * v[2],
        r(0, 1) * v[0] + r(1, 1) * v[1] + r(2, 1) * v[2],
        r(0, 2) * v[0] + r(1, 2) * v[1] + r(2, 2) * v[2]);
}

/// Rᵀ·Rb（转置乘旋转——逐元素三重循环，固定 k 序——确定性；冒烟纪律）。
rw::math::Rotation3D<double> rotateTransposeFrame(const rw::math::Rotation3D<double>& r,
                                                  const rw::math::Rotation3D<double>& rb)
{
    rw::math::Rotation3D<double> out;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < 3; ++k) { s += r(k, i) * rb(k, j); }
            out(i, j) = s;
        }
    }
    return out;
}

/// 对参数向量做角度规范化（输出面——θ/α wrap 到 (−π,π]）。
std::vector<double> wrapped(const std::vector<double>& x)
{
    std::vector<double> out = x;
    for (std::size_t k = 0; k + 3 < out.size(); k += 4) {
        out[k] = wrapPi(out[k]);
        out[k + 3] = wrapPi(out[k + 3]);
    }
    return out;
}

/**
 * @brief 判定解的规范分支（输出面——确定性等价类代表选取）。
 *
 * 依据（标准 DH 恒等关系）：T(θ,d,a,α) = T(θ+π, −d, −a, α+π)——同一
 * 几何存在两支参数化解。本函数选取"偏距非负"支（d<0 或 d=0 且 a<0 时
 * 翻转；θ/α 随恒等式平移 π 后 wrap）——确定性（同输入同代表，NFR-COR-
 * 02）、几何无损（恒等式精确成立）；roundtrip 与等价验证均以本代表为
 * 比对形态（分支约定登记单元卡 §15 v0.10，黄金数据集 WP-13-T16 同规）。
 */
std::vector<double> canonicalForm(const std::vector<double>& x)
{
    std::vector<double> out = wrapped(x);
    for (std::size_t k = 0; k + 3 < out.size(); k += 4) {
        const double d = out[k + 1];
        const double a = out[k + 2];
        // 分支判据容差（数值程序常数）：d 的浮点噪声（如 ±1e-17）不得
        // 阻断"d≈0 且 a<0"的翻转判定。
        if (d < -kBranchToleranceM
            || (std::abs(d) <= kBranchToleranceM && a < 0.0)) {
            out[k] = wrapPi(out[k] + kPi);
            out[k + 1] = -d;
            out[k + 2] = -a;
            out[k + 3] = wrapPi(out[k + 3] + kPi);
        }
    }
    return out;
}

/**
 * @brief SolverTargets 的扩展载体：显式侧累积链的完整目标帧（原点＋全旋转
 *        ＋z 轴＋零位）——解析种子需要全旋转读取 θ 的帧相位。
 */
struct SolverFrameTargets {
    SolverTargets base;                                    ///< 原点/轴/零位（求解残差用）
    std::vector<rw::math::Rotation3D<double>> rotations;   ///< T_{0,i} 全旋转（种子用）
    std::size_t size() const noexcept { return base.size(); }
};

/**
 * @brief 从显式权威关节序列构造求解目标（§7.5 第二阶目标的显式半边——
 *        与 dhToExplicit 同一累积规则：T_{0,i}=Π origin_k，z_i=T_{0,i}·z）。
 *
 * 前置：每关节 axis/origin 为 Provided 且有限、轴可归一化——由
 * explicitToDh 的第一阶结构检查先行保证（单一检查面）。
 */
SolverFrameTargets targetsFromExplicit(const std::vector<JointEntry>& joints)
{
    SolverFrameTargets t;
    t.base.originPositions.reserve(joints.size());
    t.base.axisDirections.reserve(joints.size());
    t.base.zeroOffsets.reserve(joints.size());
    t.rotations.reserve(joints.size());
    rw::math::Transform3D<double> acc = identityTransform();
    for (const JointEntry& joint : joints) {
        // 显式侧相对变换入链（JointPose→Transform3D 同构转换——零语义差）。
        acc = transformMul(acc, static_cast<rw::math::Transform3D<double>>(joint.origin.value()));
        t.base.originPositions.push_back(acc.P());
        t.base.axisDirections.push_back(zAxisOf(acc));
        t.base.zeroOffsets.push_back(joint.zeroOffset);
        t.rotations.push_back(acc.R());
    }
    return t;
}


/**
 * @brief 解析种子（确定性闭式构造——两阶段求解的第二阶段初值）。
 *
 * 背景（为什么需要第二阶段）：LM 从单位参数出发是确定性起点（§7.5 固定
 * 初值策略的字面执行），但 DH 参数化存在大量局部极小（UR 类混合平行/
 * 垂直轴链上尤为明显），单位初值的相位 1 可能停滞在非零残差——而 V-11
 * 的 roundtrip 要求（DH→展开→再求 DH 参数级一致 ≤第 5 项容差）隐含
 * "对 DH 可表达链必须全局收敛"。
 *
 * 构造（逐关节，父帧局部坐标——对 DH 一致目标**逐位精确**）：
 *   局部目标帧 L = R_{i-1}ᵀ·R_i^target（对一致链恰＝Rz(θ')·Rx(α)）；
 *   局部平移 t = R_{i-1}ᵀ·(P_i − O_{i-1})（对一致链恰＝Rz(θ')·(a,0,d)）。
 *   直接读出：θ' = atan2(L(1,0), L(0,0))；α = atan2(−L(1,2), L(2,2))；
 *             a = t_x·cosθ' + t_y·sinθ'；d = t_z；θ_offset = θ' − zeroOffset。
 *   ★ 用**全旋转**（而非仅 z 轴）读取 θ/α：末关节（及一切零偏距关节）的
 *     帧内相位不受轴/原点目标约束——仅凭 z 轴定 θ 会落入相位差 π 的等价
 *     支，被 §7.6 全帧 FK 对照正确拒绝（相位差是真实的法兰相位差异）。
 *   对非一致（近似）目标：读出值仍为确定性合理起点（LM 相位 2 精化）。
 *
 * @param targets [in] 显式侧累积链目标（含全旋转）
 * @return 种子参数向量；目标含非有限值（溢出样例）时返回空——调用方跳过
 *         相位 2（保持相位 1 结论）
 */
std::vector<double> analyticSeed(const SolverFrameTargets& targets)
{
    const std::size_t n = targets.size();
    std::vector<double> seed(4 * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        // 前一帧：i=0 用基座恒等帧（原点 0）；i>0 用前一目标帧（与求解
        // 目标同源——逐位确定性）。
        const rw::math::Rotation3D<double> rPrev =
            (i == 0) ? identityRotation() : targets.rotations[i - 1];
        const rw::math::Vector3D<double> oPrev =
            (i == 0) ? rw::math::Vector3D<double>(0.0, 0.0, 0.0)
                     : targets.base.originPositions[i - 1];
        const rw::math::Vector3D<double> localT = rotateTranspose(
            rPrev, targets.base.originPositions[i] - oPrev);
        const rw::math::Rotation3D<double> localFrame = rotateTransposeFrame(
            rPrev, targets.rotations[i]);
        if (!isFinite(localT)) { return {}; }
        // θ' 与 α：一致链的局部帧＝Rz(θ')Rx(α)——逐元素读出（确定性）。
        const double thetaTotal = std::atan2(localFrame(1, 0), localFrame(0, 0));
        const double alpha = std::atan2(-localFrame(1, 2), localFrame(2, 2));
        const double a = localT[0] * std::cos(thetaTotal)
                         + localT[1] * std::sin(thetaTotal);
        const double d = localT[2];
        if (!std::isfinite(thetaTotal) || !std::isfinite(alpha) || !std::isfinite(a)
            || !std::isfinite(d)) {
            return {};
        }
        seed[4 * i] = thetaTotal - targets.base.zeroOffsets[i];
        seed[4 * i + 1] = d;
        seed[4 * i + 2] = a;
        seed[4 * i + 3] = alpha;
    }
    return seed;
}

/// 证人投影幅度（ExactNonUnique 解集的见证构造——数值程序常数，非阈值）。
constexpr double kWitnessProjectionOffset = 0.1;

// =====================================================================
// 展开（dhToExplicit 的共享实现——verifyEquivalent 的 DH 侧同式消费）
// =====================================================================

/**
 * @brief DH 链展开为显式关节（§7.4 算法的单一实现——IDhExplicitConverter::
 *        dhToExplicit 与 DH 权威侧 Description 构造共用，NFR-MNT-04）。
 *
 * 错误语义与公共接口版一致：ChainEmpty/DegenerateBase 走错误态；关节类型
 * 非 DH 可参数化（Prismatic/Fixed）与值非有限＝调用方契约违约→fail-fast
 * （std::invalid_argument——AGENTS 错误纪律；五状态的结构检查只属于
 * explicitToDh 方向）。
 */
ExpandOutcome expandDhChain(const DhChain& chain)
{
    // 步① 输入契约检查（§9.4.7 @错误 两值——顺序固定）。
    if (chain.joints.empty()) {
        ExpandOutcome out;
        out.ok = false;
        out.errorCode = DhErrorCode::ChainEmpty;
        return out;  // 空链——调用方契约违约（值面）
    }
    if (chain.mixedInBaseTransform.has_value()) {
        ExpandOutcome out;
        out.ok = false;
        out.errorCode = DhErrorCode::DegenerateBase;
        return out;  // 基座—世界变换混入——§7.6 隔离声明拒绝（M-11）
    }

    // 值有限与类型合法性（调用方契约——fail-fast；不静默修复 NFR-COR-03）。
    for (const DhChainJoint& joint : chain.joints) {
        if (!isFinite(joint.dh) || !std::isfinite(joint.zeroOffset)) {
            throw std::invalid_argument(
                "dhToExplicit: DH 参数含非有限值（joints[" + joint.localName
                + "]）——调用方契约违约（I-MDL-3）");
        }
        if (joint.type != JointType::Revolute && joint.type != JointType::Continuous) {
            throw std::invalid_argument(
                "dhToExplicit: DH 链关节类型不可参数化（joints[" + joint.localName
                + "]）——标准 DH 只参数化旋转关节（§7.4）");
        }
    }

    // 步②③ 逐级累乘＋构造产物（§7.4 原文：origin=相对变换、axis=T_{0,i}·z）。
    ExpandOutcome out;
    out.ok = true;
    out.joints.reserve(chain.joints.size());
    rw::math::Transform3D<double> acc = identityTransform();
    for (const DhChainJoint& joint : chain.joints) {
        // 步② 当前步相对变换：Rot_z(θ_offset＋zeroOffset)·Trans_z(d)·
        // Trans_x(a)·Rot_x(α)——Rot_z 内含 zeroOffset 即"q=0 零位对齐"的
        // 几何落点（q_rw=0 时显式位姿＝DH 派生位姿，θ_offset 与 zeroOffset
        // 字段保持分离、zeroOffset 原样透传）。
        const rw::math::Transform3D<double> step = dhStepTransform(
            joint.dh.thetaOffset + joint.zeroOffset, joint.dh.d, joint.dh.a,
            joint.dh.alpha);
        acc = transformMul(acc, step);  // T_{0,i}（基座→关节 i 累积——z_i 的载体）
        // 步③ 产物装配：axis=归一化 z_i（§9.4.7 @post 单位向量）；origin=
        // 当前步相对变换（T_parent_joint——core.md §4.6 读法）。
        JointEntry entry;
        entry.objectId = joint.objectId;
        entry.localName = joint.localName;
        entry.type = joint.type;
        entry.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            zAxisOf(acc),
            core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                        std::nullopt, std::nullopt,
                                        std::string("dh-to-explicit")));
        entry.origin = core::SourcedValue<JointPose>::provided(
            JointPose(step),
            core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                        std::nullopt, std::nullopt,
                                        std::string("dh-to-explicit")));
        entry.zeroOffset = joint.zeroOffset;  // 权威零位透传（两态均权威）
        entry.bounds = joint.bounds;          // 限位透传（两态均权威——§7.2）
        entry.workingRange = joint.workingRange;
        entry.dhDerived = joint.dh;           // 派生展示值（Explicit 态不入编码——D-MDL-5）
        out.joints.push_back(std::move(entry));
    }
    return out;
}

// =====================================================================
// 求解目标构造与链几何提取
// =====================================================================

// =====================================================================
// MDL-DH-* 三码产码点（§9.5 T09 行——码常量唯一书写点在 DiagCodes.hpp）
// =====================================================================

/// 比较型单侧值（数值＋单位 token——CommandHandlers 同款口径，本文件独立
/// 小工具：产码面唯一性约束的是码值，非文本工具函数）。
core::ComparativeValue comparativeValue(double v, std::string_view unitSymbol)
{
    core::ComparativeValue out;
    out.quantity = core::SourcedValue<double>::provided(
        v, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                       std::nullopt, std::nullopt,
                                       std::string("mdl-dh-conversion")));
    const auto unit = core::UnitToken::find(unitSymbol);
    if (!unit.has_value()) {
        throw std::logic_error("mdl: 未注册单位 token（实现缺陷）: "
                               + std::string(unitSymbol));
    }
    out.unit = *unit;
    return out;
}

/// 逐关节定位文本（诊断 context 用——"joints[2]「J3」"形态）。
std::string jointContext(std::size_t index, const std::string& localName)
{
    if (localName.empty()) { return "joints[" + std::to_string(index) + "]"; }
    return "joints[" + std::to_string(index) + "]「" + localName + "」";
}

}  // namespace

// =====================================================================
// IDhExplicitConverter 产品实现
// =====================================================================

ExpandOutcome DhExplicitConverter::dhToExplicit(const DhChain& chain,
                                                std::vector<core::DiagnosticRecord>& /*diags*/) const
{
    // 本方法两个状态都不产诊断（见头注——输入契约违约的错误面＝DhErrorCode
    // 值面；无登记码不产诊断，禁私定/拼码）；diags 参数为签名契约预留位。
    return expandDhChain(chain);
}

DhConversionResult DhExplicitConverter::explicitToDh(
    const std::vector<JointEntry>& explicitJoints,
    std::vector<core::DiagnosticRecord>& diags) const
{
    DhConversionResult result;
    result.determination = DhDetermination::NotExpressible;  // 终判缺省（第一阶）

    // ---- 第一阶·结构适用性检查（解析，先于一切数值求解——MDL-10）----
    // 任何违规＝NotExpressible 终判：不进入求解、无参数产出（互斥化）。
    // 空链：链本身不存在（结构前提不满足的最简形态）→终判。
    if (explicitJoints.empty()) {
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kMdlDhNotExpressible), std::nullopt, std::nullopt,
            std::nullopt, std::string("explicit-to-dh 结构适用性检查"),
            std::string("链为空——不存在可判定的关节链（终判，不进入求解）"),
            std::string("维持显式权威，或先建立关节链")));
        return result;
    }
    for (std::size_t i = 0; i < explicitJoints.size(); ++i) {
        const JointEntry& joint = explicitJoints[i];
        // 检查 1：单自由度旋转（Revolute；Continuous 按 MDL-12 **已确认
        // 工程工作范围**后视同旋转——未确认/非有限/无序不视同）。Prismatic
        // /Fixed 不满足（§7.5 原文"含 prismatic 关节→NotExpressible"）。
        bool rotatable = false;
        std::string violation;
        if (joint.type == JointType::Revolute) {
            rotatable = true;
        } else if (joint.type == JointType::Continuous) {
            const auto range = joint.workingRange.tryValue();
            if (range.has_value() && std::isfinite(range->first)
                && std::isfinite(range->second) && range->first < range->second) {
                rotatable = true;  // 已确认有限范围——视同旋转（MDL-12）
            } else {
                violation = "continuous 工程工作范围未确认（MDL-12 确认后方视同旋转）";
            }
        } else if (joint.type == JointType::Prismatic) {
            violation = "prismatic 关节不在 DH 结构前提内（终判）";
        } else {  // Fixed
            violation = "fixed 连接非单自由度旋转（终判）";
        }
        // 检查 2：轴有效（Provided、有限、可归一化——I-MDL-6 同口径）。
        const auto axis = joint.axis.tryValue();
        if (rotatable) {
            if (!axis.has_value()) {
                rotatable = false;
                violation = "axis 权威值缺失（Explicit 态须 Provided）";
            } else if (!isFinite(*axis) || !((*axis).norm2() > 0.0)) {
                rotatable = false;
                violation = "轴非有限或不可归一化（零轴/NaN/Inf——I-MDL-6）";
            }
        }
        // 检查 3：原点可用（Provided 且有限——T_parent_joint 是重建目标的
        // 组成分量）。
        if (rotatable) {
            const auto origin = joint.origin.tryValue();
            if (!origin.has_value()) {
                rotatable = false;
                violation = "origin 权威值缺失（Explicit 态须 Provided）";
            } else if (!isFinite(static_cast<rw::math::Transform3D<double>>(*origin))) {
                rotatable = false;
                violation = "原点位姿含非有限值（I-MDL-3）";
            }
        }
        if (!rotatable) {
            // 终判出口：MDL-DH-NOT-EXPRESSIBLE（error 级；subject＝首个
            // 违规关节——精确定位；不进入第二阶）。
            diags.push_back(core::DiagnosticRecord::make(
                std::string(kMdlDhNotExpressible), joint.objectId, joint.localName,
                std::nullopt, jointContext(i, joint.localName)
                                  + " 结构适用性检查",
                violation + "——链不满足 DH 结构前提（终判，不进入求解）",
                std::string("维持显式权威（§7.6 锁定）；或修改链结构后重试")));
            return result;
        }
    }

    // ---- 第二阶·数值求解（两阶段确定性策略——§7.5"固定初值/迭代上限/
    // 收敛判据"＋V-11 roundtrip 的全局收敛要求；§15 v0.10 登记）----
    // 相位 1：从单位参数（中性恒等）出发的确定性 LM——§7.5 固定初值策略
    // 的字面执行；
    // 相位 2：仅当相位 1 未达第 5 项上界时启用——解析种子（确定性闭式
    // 构造，见 analyticSeed 注）作初值的 LM 精化。两相位均无迭代随机性、
    // 不读时钟/环境——同输入同解（NFR-COR-02）；择优规则确定（达容差者
    // 优，其次 E 小者优，平手取相位 2——种子更接近结构一致解）。
    const SolverFrameTargets fullTargets = targetsFromExplicit(explicitJoints);
    const SolverTargets& targets = fullTargets.base;
    const std::vector<bool> allOptimizable(4 * explicitJoints.size(), true);
    const LmResult phase1 = lmSolve(targets, std::vector<double>(4 * explicitJoints.size(), 0.0),
                                    allOptimizable);
    LmResult chosen = phase1;
    if (phase1.status == DhConvergenceState::Converged
        && withinExactTolerance(deviationsOf(phase1.x, targets))) {
        // 相位 1 已达第 5 项上界——无需相位 2（固定初值策略充分）。
    } else {
        const std::vector<double> seed = analyticSeed(fullTargets);
        if (!seed.empty()) {
            const LmResult phase2 = lmSolve(targets, seed, allOptimizable);
            const bool p1Converged = phase1.status == DhConvergenceState::Converged;
            const bool p2Converged = phase2.status == DhConvergenceState::Converged;
            const double p1E = p1Converged ? errorMetricEOf(deviationsOf(phase1.x, targets))
                                           : std::numeric_limits<double>::infinity();
            const double p2E = p2Converged ? errorMetricEOf(deviationsOf(phase2.x, targets))
                                           : std::numeric_limits<double>::infinity();
            // 择优（确定性）：收敛者优；都收敛则 E 小者优；平手取相位 2。
            if (p2Converged && (!p1Converged || p2E <= p1E)) {
                chosen = phase2;
            }
        }
        // 种子不可得（目标溢出）→保持相位 1 结论。
    }
    if (chosen.status != DhConvergenceState::Converged) {
        // 求解器数值失败：不收敛/发散/资源异常→AnalysisFailed（不构成
        // 语义结论——MDL-10；不产出任何参数）。
        result.determination = DhDetermination::AnalysisFailed;
        result.convergence = DhConvergenceState::Diverged;
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kMdlDhAnalysisFailed), std::nullopt, std::nullopt,
            std::nullopt, std::string("explicit-to-dh 数值求解"),
            std::string("确定性 LM 求解器数值失败（不收敛/发散）——迭代上限=")
                + std::to_string(kMaxIterations)
                + "；不构成语义结论（调整输入后可重试）",
            std::string("调整后重试；如复现请登记（求解器数值鲁棒性——R-MDL-4）")));
        return result;
    }

    // ---- 判定（附录 D 第 5 项逐关节逐项上界——C3）----
    const std::vector<DhJointDeviation> devs = deviationsOf(chosen.x, targets);
    result.deviations = devs;
    result.convergence = DhConvergenceState::Converged;
    if (!withinExactTolerance(devs)) {
        // 收敛但任一项超容差→Approximate：附 E 度量与收敛状态（§7.5 原文）；
        // 不得成为权威 DH（C-4）——parameters 为近似参考值（无权威资格）。
        result.determination = DhDetermination::Approximate;
        result.parameters = parametersOf(canonicalForm(chosen.x));
        result.errorMetricE = errorMetricEOf(devs);
        // 最坏关节定位（逐项偏差最大者——按轴角/原点两项的相对超差比）。
        std::size_t worst = 0;
        double worstRatio = 0.0;
        for (std::size_t i = 0; i < devs.size(); ++i) {
            const double ratio = std::max(
                devs[i].axisAngleDeviation / kDhExactAxisAngleToleranceRad,
                devs[i].originPositionDeviation / kDhExactOriginPositionToleranceM);
            if (ratio > worstRatio) {
                worstRatio = ratio;
                worst = i;
            }
        }
        // 诊断比较型三要素＝最坏关节的两项偏差（actual）对第 5 项上界
        // （expected）——单位 rad／m 各一条语义写在 cause（记录级单
        // comparison 面取轴角项；原点项入 cause 文本）。
        core::ComparativeFields cmp;
        cmp.actual = comparativeValue(devs[worst].axisAngleDeviation, "rad");
        cmp.expected = comparativeValue(kDhExactAxisAngleToleranceRad, "rad");
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kMdlDhApproximate),
            explicitJoints[worst].objectId, explicitJoints[worst].localName,
            std::nullopt, jointContext(worst, explicitJoints[worst].localName)
                              + " 近似判定（最坏关节）",
            std::string("收敛但逐项偏差超附录 D 第 5 项上界：E=")
                + formatDouble(result.errorMetricE)
                + "（Σ 轴角 rad＋原点 m）；收敛=" + std::string(dhConvergenceToken(result.convergence))
                + "；最坏轴角偏差=" + formatDouble(devs[worst].axisAngleDeviation)
                + " rad、最坏原点偏差=" + formatDouble(devs[worst].originPositionDeviation)
                + " m——不得成为权威 DH（C-4）",
            std::string("维持显式权威；或修正显式模型后重试"),
            std::move(cmp)));
        return result;
    }

    // ---- 唯一性判定（去重容差内无可辨识第二解→Exact；否则 ExactNonUnique）----
    const std::vector<double> jacobian = numericJacobian(chosen.x, targets);
    const auto [rank, freeColumns] = rankAndFreeColumns(
        jacobian, targets.size() * 6, chosen.x.size());
    if (freeColumns.empty()) {
        // 满秩：解局部唯一（去重容差内无可辨识第二解）→Exact。
        result.determination = DhDetermination::Exact;
        result.parameters = parametersOf(canonicalForm(chosen.x));
        return result;
    }

    // ---- 退化族：ExactNonUnique（字典序规则定值＋解集报告——禁止随机挑选）----
    // freeCoordinates＝雅可比秩亏列（局部自由方向，升序＝字典序）；定值
    // ＝逐坐标钉中性值 0 约束重解（收敛且在容差内即定值）；解集＝首解＋
    // 定值后选解＋逐自由坐标的投影证人（偏移 kWitnessProjectionOffset 重
    // 投影——证明非唯一性的可分辨成员）。
    result.determination = DhDetermination::ExactNonUnique;
    result.freeCoordinates = freeColumns;
    std::vector<double> canonical = chosen.x;
    std::vector<std::size_t> pinned;
    result.solutionSet.push_back(parametersOf(canonicalForm(chosen.x)));  // 证人 1：首解
    for (const std::size_t jCol : freeColumns) {
        std::vector<bool> optimizable(canonical.size(), true);
        for (const std::size_t p : pinned) { optimizable[p] = false; }
        optimizable[jCol] = false;  // 本轮钉住：自由坐标取中性值 0
        std::vector<double> start = canonical;
        start[jCol] = 0.0;
        const LmResult trial = lmSolve(targets, start, optimizable);
        // 接受门＝语义门（残差在附录 D 第 5 项容差内）：定值重解从已达
        // 最优的解出发，LM 可能因预算耗尽返回 Diverged 而解仍然有效——
        // 判定只看残差事实（NaN/Inf 经 withinExactTolerance 比较天然拒绝）。
        if (!withinExactTolerance(deviationsOf(trial.x, targets))) {
            continue;  // 该坐标在全局约束下不可达中性值——保留当前值（保守）
        }
        // 定值生效：与定值前解可分辨（>去重容差）才追加约束解证人。
        if (maxAbsDiff(trial.x, canonical) > kSolutionDeduplicationTolerance) {
            result.solutionSet.push_back(parametersOf(canonicalForm(trial.x)));
        }
        canonical = trial.x;  // 字典序定值落地（中性值 0）
        pinned.push_back(jCol);
    }
    // 逐自由坐标投影证人（固定偏移重投影——演示解族的可分辨成员；
    // 全部自由坐标按字典序处理，收敛且在容差内才收录）。
    for (const std::size_t jCol : freeColumns) {
        std::vector<bool> optimizable(canonical.size(), true);
        optimizable[jCol] = false;  // 证人坐标钉在偏移值上
        std::vector<double> start = canonical;
        start[jCol] += kWitnessProjectionOffset;
        const LmResult witness = lmSolve(targets, start, optimizable);
        // 证人接受门＝语义门（同上——残差容差判定，不问求解器状态）。
        if (!withinExactTolerance(deviationsOf(witness.x, targets))) {
            continue;
        }
        const std::vector<DhParameters> witnessParams = parametersOf(canonicalForm(witness.x));
        bool distinct = true;
        for (const std::vector<DhParameters>& existing : result.solutionSet) {
            if (maxAbsDiff(flattenParameters(witnessParams),
                           flattenParameters(existing))
                <= kSolutionDeduplicationTolerance) {
                distinct = false;
                break;
            }
        }
        if (distinct) { result.solutionSet.push_back(witnessParams); }
    }
    result.parameters = parametersOf(canonicalForm(canonical));  // 稳定选解（字典序规则）
    return result;
}

namespace {

// =====================================================================
// 等价验证的链级 Description 构造（§7.6 内部面——与 T12 全量构造器分工，
// 见 DhConvert.hpp verifyEquivalent 范围注记）
// =====================================================================

/// modeling::JointType → runtime::JointType（同词表四值映射——两枚举独立
/// 定义于各自单元，值序一致；switch 全枚举无 default）。
runtime::JointType toRuntimeJointType(JointType type)
{
    switch (type) {
        case JointType::Revolute: return runtime::JointType::Revolute;
        case JointType::Continuous: return runtime::JointType::Continuous;
        case JointType::Prismatic: return runtime::JointType::Prismatic;
        case JointType::Fixed: return runtime::JointType::Fixed;
    }
    return runtime::JointType::Revolute;  // 不可达（防御——保持确定性）
}

/**
 * @brief 由工作集构造链级 Description（等价验证专用）。
 *
 * 构造面（同链对照所需最小集）：关节（身份/名称/类型/轴/原点/零位/限位/
 * 工作范围）＋连杆（身份/名称——物性 NotProvided：FK 对照是运动学结论，
 * 物性不入对照面）＋基座预设（MDL-22——两侧同源）。工具/场景/摩擦/传动
 * 置缺省（两侧同缺省→对照不受影响）。速度/加速度 NotProvided（§4.3-A
 * schema 无落点——v0.6 ②a 登记，不发明字段）。
 *
 * @param ws      [in] 工作集（Explicit：直映 axis/origin；StandardDH：经
 *                expandDhChain 展开——同一展开路径＝语义单一源）
 * @param failure [out] 前置违例说明（返回 nullopt 时有义）
 * @return nullopt＝前置不满足（权威态与可用数据不匹配）；否则 Description
 *
 * 纯函数；确定性（同输入同 Description——含对象身份透传）。
 */
std::optional<runtime::RobotDesignDescription> buildChainDescription(
    const ModelingWorkingSet& ws, std::string& failure)
{
    const RobotDesign& design = ws.design;
    runtime::RobotDesignDescription desc;
    desc.descriptionContractVersion = 1;  // ≥1（§4.3.1——等价验证内部构造恒 1）
    // 机器人局部名：displayName 非空即用（呈现名不入编译身份——但名称面
    // 须非空合法）；空则确定性回退 "robot"（两侧同规——对照不受影响）。
    desc.robotLocalName = design.displayName.empty() ? std::string("robot")
                                                     : design.displayName;

    // ---- 关节链：按权威态取几何（Explicit 直映／StandardDH 展开）----
    std::vector<JointEntry> geometry;
    if (design.authority == AuthorityMode::Explicit) {
        geometry.reserve(design.joints.size());
        for (const JointEntry& joint : design.joints) {
            // 前置：显式权威链的 axis/origin 必须可用（§7.2——Explicit 态
            // 权威一等字段；缺失＝数据状态不满足对照前提）。
            if (!joint.axis.tryValue().has_value() || !joint.origin.tryValue().has_value()) {
                failure = "Explicit 权威链关节「" + joint.localName
                          + "」axis/origin 缺失——等价验证前置不满足";
                return std::nullopt;
            }
            geometry.push_back(joint);
        }
    } else {
        // StandardDH 权威：DH 参数为权威（dhDerived 必填）——经 §7.4 展开
        // 得显式几何（与 dhToExplicit 同一实现——语义单一源）。
        DhChain chain;
        chain.joints.reserve(design.joints.size());
        for (const JointEntry& joint : design.joints) {
            if (!joint.dhDerived.has_value()) {
                failure = "StandardDH 权威链关节「" + joint.localName
                          + "」dhDerived 缺失——等价验证前置不满足";
                return std::nullopt;
            }
            DhChainJoint entry;
            entry.dh = *joint.dhDerived;
            entry.zeroOffset = joint.zeroOffset;
            entry.type = joint.type;
            entry.objectId = joint.objectId;
            entry.localName = joint.localName;
            entry.bounds = joint.bounds;
            entry.workingRange = joint.workingRange;
            chain.joints.push_back(std::move(entry));
        }
        const ExpandOutcome expanded = expandDhChain(chain);
        if (!expanded.ok) {
            // 展开失败（空链/基座混入——合法工作集不可达；防御面）。
            failure = std::string("DH 权威链展开失败：")
                      + std::string(dhErrorCodeToken(expanded.errorCode));
            return std::nullopt;
        }
        geometry = std::move(expanded.joints);
    }

    // ---- 关节→JointDescription 映射（★ 零位折叠纪律——见下）----
    // runtime §4.2 Description 无零位偏置承载：CompilerImpl S5 组装恒置
    // CanonicalJoint.zeroOffset=0（"显式表示已折叠进限位/原点——
    // q_authoritative 即 q_rw"，units/runtime.md §15.4 v0.12 登记）。故
    // modeling→Description 映射必须完成折叠：
    //   a) origin_desc = origin_模型 · R(axis, zeroOffset)——权威零位旋转
    //      入几何（q_rw=0 构型＝权威零位构型——与 §7.4 展开式 Rot_z(θ+q0)
    //      的零位对齐语义一致）；
    //   b) bounds/workingRange 平移 −zeroOffset（权威 q→RobWork q）。
    for (std::size_t i = 0; i < geometry.size(); ++i) {
        const JointEntry& src = geometry[i];
        runtime::JointDescription jd;
        jd.objectId = src.objectId;
        jd.localName = src.localName;
        jd.type = toRuntimeJointType(src.type);
        jd.axis = src.axis.value();  // 单位向量（展开产物已归一化／显式权威入参）
        // a) 零位折叠：origin_desc = origin · R(axis, zeroOffset)（旋转在
        // 关节局部轴上——与 runtime jointStaticTransform 的
        // T_parent_joint·R_axis(zeroOffset)·R_align 同侧同序）。
        const rw::math::Transform3D<double> originModel =
            static_cast<rw::math::Transform3D<double>>(src.origin.value());
        const rw::math::Rotation3D<double> folded =
            rotMul(originModel.R(), axisAngleRotation(src.axis.value(), src.zeroOffset));
        jd.origin = rw::math::Transform3D<double>(originModel.P(), folded);
        // b) 限位平移（Revolute/Prismatic；权威 q→Description q）。
        if (src.type == JointType::Revolute || src.type == JointType::Prismatic) {
            if (const auto b = src.bounds.tryValue(); b.has_value()) {
                const auto provenance = []() {
                    return core::ValueProvenance::make(
                        core::ProvenanceKind::DerivedReadOnly, std::nullopt,
                        std::nullopt, std::string("dh-equivalence"));
                };
                jd.lower = core::SourcedValue<double>::provided(b->first - src.zeroOffset,
                                                                provenance());
                jd.upper = core::SourcedValue<double>::provided(b->second - src.zeroOffset,
                                                                provenance());
            }
        }
        // Continuous 的工程工作范围同规平移（MDL-12 分析消费属性）。
        if (src.type == JointType::Continuous) {
            if (const auto r = src.workingRange.tryValue(); r.has_value()) {
                jd.workingRange = runtime::WorkingRange{r->first - src.zeroOffset,
                                                        r->second - src.zeroOffset};
            }
        }
        desc.joints.push_back(std::move(jd));
    }

    // ---- 连杆（n+1，含基座——身份/名称透传；物性 NotProvided 见函数注）----
    desc.links.reserve(design.links.size());
    for (const LinkEntry& link : design.links) {
        runtime::LinkDescription ld;
        ld.objectId = link.objectId;
        ld.localName = link.localName;
        desc.links.push_back(std::move(ld));
    }

    // ---- 基座布置（MDL-22——两侧同源同规；基座—世界矩阵不由 modeling
    // 计算/缓存（M-11）：只传编辑表示，矩阵归 runtime 编译产物）----
    desc.base.preset = design.basePlacement.preset;
    if (const auto eaa = design.basePlacement.customEaa.tryValue(); eaa.has_value()) {
        desc.base.customEaa = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            *eaa,
            core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                        std::nullopt, std::nullopt,
                                        std::string("dh-equivalence")));
    }
    if (const auto pos = design.basePlacement.basePosition.tryValue(); pos.has_value()) {
        desc.base.basePosition = *pos;
    }
    return desc;
}

/**
 * @brief RobWork 零位构型（q_rw=0）FK 逐关节累积（等价验证的对照面——双方同规）。
 *
 * 约定（固定、两侧一致）：T_i = T_{i-1}·origin_i·R(axis_i, zeroOffset_i)。
 * 零位语义注：Description 显式表示已折叠零位（origin 含权威零位旋转、
 * CanonicalJoint.zeroOffset 恒 0——CompilerImpl S5 v0.12 登记），故本函数
 * 的 zeroOffset 旋转对 S1～S5 产物为恒等占位——保留该因子是为与 runtime
 * 契约面语义对齐（若对端后续承载真实零位，对照面无需变更）；权威零位
 * 对齐的检验落在两侧折叠后的 origin 一致性上。
 *
 * @param model [in] 分段编译产物（S5 CanonicalModel——只读）
 * @return 逐关节世界位姿（链序；joints 空则空）
 */
std::vector<rw::math::Transform3D<double>> zeroConfigFK(const runtime::CanonicalModel& model)
{
    std::vector<rw::math::Transform3D<double>> frames;
    const runtime::RobotChain& chain = model.chain();
    frames.reserve(chain.joints.size());
    rw::math::Transform3D<double> acc = model.world().T_world_base;
    for (const runtime::CanonicalJoint& joint : chain.joints) {
        // 零位旋转（轴角 Rodrigues——与 axisAngleRotation 同一实现，双方
        // 同规；axis 为 S5 规格化单位向量）。
        const rw::math::Rotation3D<double> zeroRot = axisAngleRotation(
            joint.axis, joint.zeroOffset);
        acc = transformMul(
            transformMul(acc, rw::math::Transform3D<double>(joint.origin.P(),
                                                            joint.origin.R())),
            rw::math::Transform3D<double>(rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                                          zeroRot));
        frames.push_back(acc);
    }
    return frames;
}

}  // namespace

EquivalenceReport DhExplicitConverter::verifyEquivalent(const ModelingWorkingSet& a,
                                                        const ModelingWorkingSet& b,
                                                        const CompileProbe& probe) const
{
    EquivalenceReport report;

    // ---- 前置：同链（关节数一致）＋两侧几何可用 ----
    if (a.design.joints.size() != b.design.joints.size()) {
        report.failureDetail = "两工作集关节数不一致（非同链）——等价验证前置不满足";
        return report;  // inputsValid=false（其余字段缺省占位——不伪造数值）
    }
    std::string failureA;
    std::string failureB;
    const auto descA = buildChainDescription(a, failureA);
    const auto descB = buildChainDescription(b, failureB);
    if (!descA.has_value() || !descB.has_value()) {
        report.failureDetail = !descA.has_value() ? failureA : failureB;
        return report;
    }
    report.inputsValid = true;

    // ---- 分段编译（S1～S5 只读——不发布快照、不产生修订，§5.2/§7.6）----
    const auto compiledA = probe.buildCanonicalModel(*descA);
    if (!compiledA.ok()) {
        report.failureDetail = std::string("参数化甲分段编译失败：")
                               + compiledA.error().what();
        return report;  // compileOk=false
    }
    const auto compiledB = probe.buildCanonicalModel(*descB);
    if (!compiledB.ok()) {
        report.failureDetail = std::string("参数化乙分段编译失败：")
                               + compiledB.error().what();
        return report;
    }
    report.compileOk = true;

    // ---- FK 对照（附录 D 第 4 项：位置 ≤1×10⁻⁹ m 且姿态 ≤1×10⁻⁹ rad）----
    const std::vector<rw::math::Transform3D<double>> framesA = zeroConfigFK(compiledA.get());
    const std::vector<rw::math::Transform3D<double>> framesB = zeroConfigFK(compiledB.get());
    if (framesA.size() != framesB.size()) {
        // S5 产物链长应与输入一致（runtime 编译器不变量）——不一致＝编译
        // 语义缺陷，按编译失败面报告（不静默对照）。
        report.compileOk = false;
        report.failureDetail = "两侧编译产物链长不一致（编译器不变量违例）";
        return report;
    }
    bool equivalent = true;
    for (std::size_t i = 0; i < framesA.size(); ++i) {
        DhJointDeviation dev;
        dev.jointIndex = i;
        dev.originPositionDeviation = (framesA[i].P() - framesB[i].P()).norm2();
        dev.axisAngleDeviation = rotationAngleBetween(framesA[i].R(), framesB[i].R());
        report.maxPositionDeviation = std::max(report.maxPositionDeviation,
                                               dev.originPositionDeviation);
        report.maxOrientationDeviation = std::max(report.maxOrientationDeviation,
                                                  dev.axisAngleDeviation);
        report.jointDeviations.push_back(dev);
        // 全称判定：任一关节任一项超差即不等价（逐关节逐项——与第 5 项
        // 同纪律，防误差互相掩盖）。
        if (dev.originPositionDeviation > kFkEquivalencePositionToleranceM
            || dev.axisAngleDeviation > kFkEquivalenceOrientationToleranceRad) {
            equivalent = false;  // 不短路——全量偏差明细随报告（全量列出纪律）
        }
    }
    report.equivalent = equivalent;
    return report;
}

// =====================================================================
// 权威切换域门（§7.6——四道门按序短路）
// =====================================================================

AuthoritySwitchDecision prepareAuthoritySwitch(const ModelingWorkingSet& draft,
                                               const ModelingWorkingSet& baseline,
                                               const IDhExplicitConverter& converter,
                                               const CompileProbe& probe,
                                               std::vector<core::DiagnosticRecord>& diags)
{
    AuthoritySwitchDecision decision;

    // 调用方契约：draft 与 baseline 必须同链（切换不改变链结构——链长不
    // 一致＝违约，fail-fast）。
    if (draft.design.joints.size() != baseline.design.joints.size()) {
        throw std::invalid_argument(
            "prepareAuthoritySwitch: draft 与 baseline 链长不一致——调用方契约违约");
    }

    // ---- 门① 前置：基线须处于 Explicit 权威态（重复切换＝权限违规）----
    if (baseline.design.authority != AuthorityMode::Explicit) {
        ModelingError err;
        err.code = ModelingErrorCode::AuthorityViolation;
        err.params.emplace_back("mode", "StandardDH");
        err.detail = "基线已处于 StandardDH 权威态——权威切换只对显式权威链开放（MDL-02）";
        decision.error = std::move(err);
        return decision;
    }

    // ---- 门② 先决断既有编辑（C-6）：draft 存在未决断编辑→拒绝 ----
    // "编辑器要求先决断既有编辑（提交或撤销）再执行切换（切换＝独立命令，
    // 摘要单独留痕）"（§7.3 C-6 原文）。
    if (!draft.changes.empty()) {
        ModelingError err;
        err.code = ModelingErrorCode::AuthorityViolation;
        err.params.emplace_back("pending-changes", std::to_string(draft.changes.size()));
        err.detail = "存在未决断既有编辑——先提交或撤销后再执行权威切换（C-6）";
        decision.error = std::move(err);
        return decision;
    }

    // ---- 门③ 转换判定（C-3/C-4）：显式→DH 五状态 ----
    // 显式权威几何提取（axis/origin 权威值——缺失＝数据状态不满足，值面
    // 拒绝；语义扩展登记单元卡 §15 增量）。
    std::vector<JointEntry> explicitJoints;
    explicitJoints.reserve(baseline.design.joints.size());
    for (const JointEntry& joint : baseline.design.joints) {
        if (!joint.axis.tryValue().has_value() || !joint.origin.tryValue().has_value()) {
            ModelingError err;
            err.code = ModelingErrorCode::DhExpandFailed;
            err.params.emplace_back("joint", joint.localName);
            err.detail = "显式权威链关节「" + joint.localName
                         + "」axis/origin 缺失——切换判定前置不满足";
            decision.error = std::move(err);
            return decision;
        }
        explicitJoints.push_back(joint);
    }
    decision.conversion = converter.explicitToDh(explicitJoints, diags);
    decision.determination = decision.conversion.determination;
    if (decision.determination != DhDetermination::Exact
        && decision.determination != DhDetermination::ExactNonUnique) {
        // NotExpressible（C-3 终判诊断已在 diags——error 级）/Approximate
        // （C-4——MDL-DH-APPROXIMATE Warning 已在 diags）/AnalysisFailed
        // （MDL-DH-ANALYSIS-FAILED 已在 diags）——全部拒绝切换，不产生修订。
        ModelingError err;
        err.code = ModelingErrorCode::AuthorityViolation;
        err.params.emplace_back("determination",
                                std::string(dhDeterminationToken(decision.determination)));
        err.detail = std::string("权威切换被判定拒绝：")
                     + std::string(dhDeterminationToken(decision.determination));
        decision.error = std::move(err);
        return decision;
    }

    // ---- 组装候选工作集（authority=StandardDH＋dhDerived＝选定解＋axis/
    // origin 重算派生缓存——D-MDL-5：派生侧不入编码身份）----
    ModelingWorkingSet candidate = baseline;
    candidate.design.authority = AuthorityMode::StandardDH;
    candidate.changes.clear();  // 切换候选＝独立命令状态（摘要归命令层）
    DhChain chain;
    chain.joints.reserve(candidate.design.joints.size());
    for (std::size_t i = 0; i < candidate.design.joints.size(); ++i) {
        JointEntry& joint = candidate.design.joints[i];
        joint.dhDerived = decision.conversion.parameters[i];  // 权威 DH 参数落地
        DhChainJoint entry;
        entry.dh = *joint.dhDerived;
        entry.zeroOffset = joint.zeroOffset;
        entry.type = joint.type;
        entry.objectId = joint.objectId;
        entry.localName = joint.localName;
        entry.bounds = joint.bounds;
        entry.workingRange = joint.workingRange;
        chain.joints.push_back(std::move(entry));
    }
    const ExpandOutcome expanded = expandDhChain(chain);
    if (!expanded.ok
        || expanded.joints.size() != candidate.design.joints.size()) {
        // 判定选定解回展开失败＝实现缺陷（Exact 解必可展开——防御面，
        // fail-fast 不静默）。
        throw std::logic_error(
            "prepareAuthoritySwitch: 判定选定解回展开失败（实现缺陷）——"
            "Exact/ExactNonUnique 解必须可无损展开");
    }
    for (std::size_t i = 0; i < candidate.design.joints.size(); ++i) {
        // 派生只读缓存：来源标记 DerivedReadOnly＋"dh-to-explicit"（与
        // GeometricEstimate"来源标记、同为权威值"纪律一致；编码排除归
        // Codec——D-MDL-5）。
        candidate.design.joints[i].axis = expanded.joints[i].axis;
        candidate.design.joints[i].origin = expanded.joints[i].origin;
    }
    decision.candidate = std::move(candidate);

    // ---- 门④ 等价验证（附录 D 第 4 项 FK 对照——S1～S5 只读分段）----
    decision.equivalence = converter.verifyEquivalent(baseline, decision.candidate, probe);
    if (!decision.equivalence.compileOk || !decision.equivalence.equivalent) {
        // 验证失败不产生修订（§7.6 原文）——值面拒绝；偏差明细在报告内
        // （无等价失败独立登记码——禁私定/拼码，§9.5 产码纪律）。
        ModelingError err;
        err.code = ModelingErrorCode::DhExpandFailed;
        err.params.emplace_back("max-position-deviation-m",
                                formatDouble(decision.equivalence.maxPositionDeviation));
        err.params.emplace_back("max-orientation-deviation-rad",
                                formatDouble(decision.equivalence.maxOrientationDeviation));
        err.detail = decision.equivalence.compileOk
                         ? "编译链 FK 对照超差（附录 D 第 4 项）——权威切换被拒"
                         : "编译链分段验证失败——权威切换被拒："
                               + decision.equivalence.failureDetail;
        decision.candidate = ModelingWorkingSet{};  // 拒绝态候选不可消费（清空）
        decision.error = std::move(err);
        return decision;
    }

    decision.allowed = true;
    return decision;
}

}  // namespace sdurws::ird::modeling
