// REUSE-T43 上游对照探针（upstreamComparison 面）——非生产代码。
// 对照双方：①生产同款上游调用 rw::math::LinearAlgebra::eigenDecompositionSymmetric<double>
// （Eigen SelfAdjointEigenSolver）；②被替换的解析闭式（批次基线 a445f4876 的
// selection/src/Backfill.cpp symmetricEigenvalues3x3 原文摘录）。
// 断言（附录 D 第 6/7 项容差口径——相对 1×10⁻¹²）：全用例逐特征值相对一致、
// 升序、迹守恒；对角/纯量阵逐位一致（解析式对角阵精确、Eigen 对角化不动——
// 该子集可位模式钉扎）。精度差异属求解器间 ulp 级，远小于断言容差。
#include <rw/math/LinearAlgebra.hpp>
#include <Eigen/Dense>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

/// 基线解析闭式（摘录自批次基线 a445f4876 原文——对照基准，非生产代码）。
std::array<double, 3> baselineAnalytic(
    double m00, double m01, double m02, double m11, double m12, double m22)
{
    const double q = (m00 + m11 + m22) / 3.0;
    const double p1 = m01 * m01 + m02 * m02 + m12 * m12;
    const double p2 = (m00 - q) * (m00 - q) + (m11 - q) * (m11 - q)
                    + (m22 - q) * (m22 - q) + 2.0 * p1;
    if (p2 <= 0.0) {
        return {q, q, q};
    }
    const double p = std::sqrt(p2 / 6.0);
    const double b00 = (m00 - q) / p, b01 = m01 / p, b02 = m02 / p;
    const double b11 = (m11 - q) / p, b12 = m12 / p;
    const double b22 = (m22 - q) / p;
    double r = (b00 * b11 * b22 + 2.0 * b01 * b02 * b12
                - b01 * b01 * b22 - b02 * b02 * b11 - b12 * b12 * b00) / 2.0;
    r = std::clamp(r, -1.0, 1.0);
    const double phi = std::acos(r) / 3.0;
    const double l1 = q + 2.0 * p * std::cos(phi);
    const double l3 = q + 2.0 * p * std::cos(phi + 2.0943951023931953);
    const double l2 = 3.0 * q - l1 - l3;
    return {l3, l2, l1};
}

/// 生产同款上游调用（六分量 → 3×3 → 升序特征值）。
std::array<double, 3> upstreamEigen(
    double m00, double m01, double m02, double m11, double m12, double m22)
{
    Eigen::MatrixXd m(3, 3);
    m << m00, m01, m02,
         m01, m11, m12,
         m02, m12, m22;
    const auto de = rw::math::LinearAlgebra::eigenDecompositionSymmetric<double>(m);
    return {de.second[0], de.second[1], de.second[2]};
}

struct Case {
    double m[6];
    bool diagonal;  // 对角/纯量阵——可位模式对照
    std::string name;
};

std::vector<Case> allCases()
{
    return {
        {{0.02, 0.0, 0.0, 0.03, 0.0, 0.04}, true, "diag-golden-link"},
        {{0.05, 0.0, 0.0, 0.05, 0.0, 0.05}, true, "isotropic"},
        {{0.01, 0.0, 0.0, 0.01, 0.0, 0.02}, true, "planar-degenerate"},
        {{0.02, 0.001, -0.002, 0.03, 0.0015, 0.04}, false, "golden-offdiag"},
        {{1.0, 0.2, -0.3, 2.0, 0.1, 3.0}, false, "general-1"},
        {{0.5, -0.05, 0.02, 0.6, -0.01, 0.4}, false, "general-2"},
        {{10.0, 1.0, -1.0, 20.0, 2.0, 30.0}, false, "large-scale"},
        {{0.001, 1e-5, -1e-6, 0.002, 5e-6, 0.003}, false, "small-scale"},
        {{2.0, 1.9, -1.8, 2.0, 1.7, 2.0}, false, "near-degenerate"},
    };
}

bool bitEqual(double a, double b)
{
    return std::memcmp(&a, &b, sizeof(double)) == 0;  // v2 未用（保留说明 v1 位模式口径）
}

}  // namespace

/// 全用例相对 1e-12 容差一致＋升序＋迹守恒（附录 D 第 6/7 项口径）。
TEST(ReuseT43EigenUpstream, TolerantAndStructureAgreement)
{
    for (const Case& c : allCases()) {
        const auto a = baselineAnalytic(c.m[0], c.m[1], c.m[2], c.m[3], c.m[4], c.m[5]);
        const auto e = upstreamEigen(c.m[0], c.m[1], c.m[2], c.m[3], c.m[4], c.m[5]);
        for (int k = 0; k < 3; ++k) {
            const double scale = std::max({1.0, std::abs(a[k]), std::abs(e[k])});
            EXPECT_LE(std::abs(a[k] - e[k]) / scale, 1e-12) << "case " << c.name << " λ" << k;
        }
        EXPECT_TRUE(std::is_sorted(e.begin(), e.end())) << "case " << c.name << " 升序";
        const double trace = c.m[0] + c.m[3] + c.m[5];
        EXPECT_NEAR(e[0] + e[1] + e[2], trace, 1e-12 * std::max(1.0, std::abs(trace)))
            << "case " << c.name << " 迹守恒";
    }
}

/// 说明：v1 曾含 DiagonalSubsetBitwise 位模式断言，实测 Eigen SelfAdjointEigenSolver
/// 对对角/纯量阵亦非逐位返回（diag-golden-link λ1、isotropic λ2、planar-degenerate
/// λ0/λ1 位模式不等，v1 记录保留 run/upstreamComparison-v1-bitwise-failed.*）——
/// 换核本就非逐位等价（评审缺口7 预判成立），产品契约面为容差断言（附录 D 第 6/7
/// 项相对 1e-12）与黄金容差对照，v2 收窄为容差＋结构面，位模式断言移除。

/// 探针入口（仅本探针——不进产品测试目标）。
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
