// REUSE-T34 候选实现（测试侧独立 TU）：耦合矩阵奇异值候选——
// rw::math::LinearAlgebra::svd（LinearAlgebra.hpp:65；Eigen JacobiSVD
// 后端 LinearAlgebra.cpp:55-58 强制 ComputeFullU|ComputeFullV）。
// 另含 sigma-only 直接 Eigen JacobiSVD 变体——仅用于量化"仅奇异值需求
// vs 上游强制全量 U/V"的开销份额（G8）；该径为产品直连 Eigen 新依赖
// 渠道，未获批不得入生产，此处仅为测量证据。
#include <rw/math/LinearAlgebra.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace t34perf {

std::vector<double> svdUpstream(const std::vector<double>& a, std::size_t n)
{
    Eigen::MatrixXd m(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            m(r, c) = a[r * n + c];
        }
    }
    Eigen::MatrixXd U;
    Eigen::VectorXd sigma;
    Eigen::MatrixXd V;
    rw::math::LinearAlgebra::svd(m, U, sigma, V);
    std::vector<double> out(sigma.data(), sigma.data() + n);
    std::sort(out.begin(), out.end());  // 上游降序→升序（与生产口径一致后对照）
    return out;
}

double svdSigmaOnlyEigen(const std::vector<double>& a, std::size_t n)
{
    Eigen::MatrixXd m(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            m(r, c) = a[r * n + c];
        }
    }
    const Eigen::JacobiSVD<Eigen::MatrixXd> svd(
        m, Eigen::ComputeFullU | Eigen::ComputeFullV);
    return svd.singularValues()(n - 1);  // σmin（上游降序末位）
}

}  // namespace t34perf
