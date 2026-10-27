// REUSE-T34 性能基线（测试侧独立 TU——非生产代码）：生产
// singularValuesOneSidedJacobi 的实施前等义副本（runtime/src/
// DescriptionValidator.cpp 换核前版本逐行同义），供 old/new 交替实测。
// 仅作测量基线与正确性对照源，不进入任何产品/测试目标。
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace t34perf {

constexpr int kJacobiMaxSweeps = 60;
constexpr double kJacobiOrthogonality = 1e-16;

std::vector<double> singularValuesOneSidedJacobi(const std::vector<double>& a, std::size_t n)
{
    std::vector<double> m(a);
    if (n == 0) { return m; }
    for (int sweep = 0; sweep < kJacobiMaxSweeps; ++sweep) {
        bool allOrthogonal = true;
        for (std::size_t p = 0; p + 1 < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                double alpha = 0.0;
                double beta = 0.0;
                double gamma = 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    const double x = m[k * n + p];
                    const double y = m[k * n + q];
                    alpha += x * x;
                    beta += y * y;
                    gamma += x * y;
                }
                if (gamma == 0.0 || alpha == 0.0 || beta == 0.0) { continue; }
                if (std::fabs(gamma) <= kJacobiOrthogonality * std::sqrt(alpha * beta)) {
                    continue;
                }
                allOrthogonal = false;
                const double zeta = (beta - alpha) / (2.0 * gamma);
                const double sign = zeta >= 0.0 ? 1.0 : -1.0;
                const double tTan = sign / (std::fabs(zeta) + std::sqrt(zeta * zeta + 1.0));
                const double c = 1.0 / std::sqrt(tTan * tTan + 1.0);
                const double s = c * tTan;
                for (std::size_t k = 0; k < n; ++k) {
                    const double x = m[k * n + p];
                    const double y = m[k * n + q];
                    m[k * n + p] = c * x - s * y;
                    m[k * n + q] = s * x + c * y;
                }
            }
        }
        if (allOrthogonal) { break; }
    }
    std::vector<double> sigma(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        double norm2 = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            norm2 += m[k * n + i] * m[k * n + i];
        }
        sigma[i] = std::sqrt(norm2);
    }
    std::sort(sigma.begin(), sigma.end());
    return sigma;
}

}  // namespace t34perf
