/**
 * @file   compare-candidate.cpp
 * @brief  GAP-CORE-C4-01 修订候选 R2 实验副本——仅存在于实验现场，不进入产品目标。
 *
 * 与生产 core/src/Compare.cpp（提交 6fb26b31d 版本）的差异仅限 closeWithin：
 *   1) 非有限检测改用 std::isfinite（原方案 A 批准范围内已含此项，语义等价）；
 *   2) 新增"diff 与 limit 同时为 +Inf"溢出分支：fma 残差 + TwoSum 精确折叠，
 *      按精确实数语义判定 |value−reference| ≤ relative·|reference| + absolute。
 * 其余函数（allCloseWithin/runtimeAbsoluteTolerance）与生产逐字一致。
 * 文件头以下的原注释保留原样，便于对照审查。
 */

#include <sdurws/ird/core/Compare.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

// 实验统计埋点（仅本实验副本；生产补丁不含）。
int g_maxExactSignIter = 0;
int g_sentinelCount = 0;

namespace sdurws::ird::core {

namespace {

// Knuth/Dekker TwoSum：s = fl(a+b)，e = a+b−s 精确（错误免费变换，无 fma 依赖）。
inline void twoSum(double a, double b, double& s, double& e) noexcept
{
    s = a + b;
    const double bb = s - a;
    e = (a - (s - bb)) + (b - bb);
}

// 返回 ≥ ulp(v) 的 2 的幂（v>0；次正规域钳到 denorm_min，保守方向只增不减）。
inline double ulpBound(double v) noexcept
{
    const int e = std::ilogb(v);
    const double b = std::ldexp(1.0, e - 51);
    return b > 0.0 ? b : std::numeric_limits<double>::denorm_min();
}

// 精确符号判定：D = Σ t[0..n)。全部 t 为 binary64，即 2^-1074 的整数倍，
// 因此 D 的精确值为该量子格点；合并最大两项（TwoSum 保持精确和不变式），
// 直到 |主导项| 超过其余项的保守上界（上界只放大、不缩小——推迟判定，
// 不可能误判）。256 轮上限远超量子耗尽论证所需（提案引理：从 2^971 尺度
// 每轮合并引入 ≤2^-52 相对残差，至 2^-2148 量子约 ≤60 轮）。
// 返回 −1/0/+1；−2 为不可达哨兵（实验断言计数为 0）。
namespace {
int exactSignInner(double* t, int n, int& iterCount) noexcept
{
    for (int iter = 0; iter < 256; ++iter) {
        iterCount = iter + 1;
        int mi = 0;
        for (int i = 1; i < n; ++i) {
            if (std::fabs(t[i]) > std::fabs(t[mi])) {
                mi = i;
            }
        }
        if (t[mi] == 0.0) {
            return 0;
        }
        double rest = 0.0;
        for (int i = 0; i < n; ++i) {
            if (i != mi) {
                rest += std::fabs(t[i]);
            }
        }
        if (rest == 0.0) {
            return t[mi] > 0.0 ? 1 : -1;
        }
        const double bound = rest + 8.0 * ulpBound(rest) + ulpBound(std::fabs(t[mi]));
        if (std::fabs(t[mi]) > bound) {
            return t[mi] > 0.0 ? 1 : -1;
        }
        int mj = -1;
        double best = 0.0;
        for (int i = 0; i < n; ++i) {
            if (i != mi) {
                const double a = std::fabs(t[i]);
                if (a > best) {
                    best = a;
                    mj = i;
                }
            }
        }
        double s = 0.0, e = 0.0;
        twoSum(t[mi], t[mj], s, e);
        t[mi] = s;
        t[mj] = e;
    }
    return -2;
}
}  // namespace

int exactSign(double* t, int n) noexcept
{
    int iterCount = 0;
    const int r = exactSignInner(t, n, iterCount);
    if (iterCount > g_maxExactSignIter) g_maxExactSignIter = iterCount;
    if (r == -2) ++g_sentinelCount;
    return r;
}

}  // namespace

bool closeWithin(double value, double reference, Tolerance t) noexcept
{
    // 非有限输入 → false（不抛、不静默通过——NFR-COR-03；noexcept 契约）。
    // std::isfinite 与原自减检测逐语义等价（NaN/±Inf 均拒，有限值均过）。
    if (!std::isfinite(value) || !std::isfinite(reference)) {
        return false;
    }
    // C4 公式：|value−reference| ≤ relative·|reference| + absolute。
    // 零参考：|reference|=0 时相对项退化为 0——公式自然退化为纯 absolute（§4.5
    // "不要求精确零"——无需分支特判）。
    const double diff = value - reference;
    const double limit = t.relative * (reference < 0 ? -reference : reference)
                       + t.absolute;
    // 溢出分支入口：diff 与 limit 同时为 +Inf。limit 为 −Inf/NaN 或仅单侧溢出
    // 时一律走原路径，逐位保持既有行为（含聚合绕过 make 的非法 tolerance）。
    if (!(std::isinf(diff) && std::isinf(limit) && limit > 0.0)) {
        return (diff <= limit) && (-diff <= limit);
    }
    // ===== GAP-CORE-C4-01 修订候选 R2：溢出分支的精确实数判定 =====
    // 前提（提案引理1）：diff=±Inf ⟹ value、reference 异号且
    // |value|+|reference| ≥ DBL_MAX + 2^970，且 |value|、|reference| 均 > 2^970
    // （否则另一侧须 > DBL_MAX，矛盾）。记 A=|v|+|r|、B=rel·|r|+a，判 A ≤ B。
    const double av = std::fabs(value);
    const double ar = std::fabs(reference);
    // absolute=+Inf（聚合绕过 make 可达）⟹ B=+Inf > A；rel<0 时 limit=+Inf
    // 仅能由 +Inf 绝对项贡献，同在此覆盖。
    if (t.absolute > std::numeric_limits<double>::max()) {
        return true;
    }
    // 减半精确：av、ar 均 > 2^970 ≫ 最小正规数，2 的幂缩放不舍入（引理1）。
    const double x = std::scalbn(av, -1);
    const double y = std::scalbn(ar, -1);
    const double p = t.relative * y;
    if (std::isinf(p)) {
        // rel·ar = 2·rel·y > 2·DBL_MAX ≥ A ⟹ A < B（引理2，等号不可能）。
        return true;
    }
    // fma 残差（2Product 定理）：rel·y = p + ep 精确（p 有限、无下溢——引理3）。
    const double ep = std::fma(t.relative, y, -p);
    // D = A/2 − rel·y − a/2 = (x+y) − p − ep − a/2；close ⟺ D ≤ 0（精确）。
    // 两侧同除 2 时绝对项同样减半；a 为正规数时 a/2 精确，a 次正规时 halving
    // 舍入偏差 ≤2^-1075（次正规绝对项＋2^-1000 量级 rational 对齐的构造性
    // 组合才可达——提案精度声明中如实记载，记录在案的 6 组失败边界均不受影响）。
    double leaf[5] = {0.0, 0.0, -p, -ep, -std::scalbn(t.absolute, -1)};
    twoSum(x, y, leaf[0], leaf[1]);
    return exactSign(leaf, 5) <= 0;
}

bool allCloseWithin(const std::vector<double>& a, const std::vector<double>& b,
                    Tolerance t)
{
    // 长度不等＝契约违约显性化（CoreError，core/compare/length 前缀）——
    // 静默 false 会把形状错误伪装成数值不匹配。
    if (a.size() != b.size()) {
        throw CoreError("core/compare/length: 长度不等（" + std::to_string(a.size())
                        + " vs " + std::to_string(b.size()) + "）");
    }
    // 逐元素应用 C4（防正负抵消——不以差值总和替代，§4.5 明文）。
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!closeWithin(a[i], b[i], t)) {
            return false;
        }
    }
    return true;
}

#ifdef IRD_CORE_HAS_RW_Q
bool allCloseWithin(const rw::math::Q& a, const rw::math::Q& b, Tolerance t)
{
    if (a.size() != b.size()) {
        throw CoreError("core/compare/length: Q 长度不等（" + std::to_string(a.size())
                        + " vs " + std::to_string(b.size()) + "）");
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!closeWithin(a[i], b[i], t)) {
            return false;
        }
    }
    return true;
}
#endif

std::optional<double> runtimeAbsoluteTolerance(QuantityKind k) noexcept
{
    // 附录 D C7 机械转写（D-06：core 是唯一转写点；修改只能随需求变更）。
    switch (k) {
    case QuantityKind::Angle:                 return 1e-12;
    case QuantityKind::Length:                return 1e-12;
    case QuantityKind::LinearVelocity:        return 1e-9;
    case QuantityKind::AngularVelocity:       return 1e-9;
    case QuantityKind::LinearAcceleration:    return 1e-9;
    case QuantityKind::AngularAcceleration:   return 1e-9;
    case QuantityKind::Torque:                return 1e-9;
    case QuantityKind::Dimensionless:         return 1e-12;
    // 附录 D 未声明默认的量纲：nullopt（"无默认且无配置来源的量不得引入产品侧
    // 相对校验"——C7 原文语义；mass/time/force/inertia/power/voltage）。
    default:                                  return std::nullopt;
    }
}

}  // namespace sdurws::ird::core
