/**
 * @file CouplingReuseTest.cpp
 * @brief REUSE-T07：耦合 SVD 的生产接线、独立解析值和上游对照。
 *
 * MDL-21/I-MDL-11 要求编辑与就绪校验消费重算值；不能用申报条件数绕过。
 * 本文件沿用 GTest/testkit，只在测试中生成解析矩阵，不实现第二套 SVD。
 * 夹具均为栈上值，无模型句柄、无共享状态，可按用例独立执行。
 */
#include <sdurws/ird/modeling/Parts.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include "../src/CouplingMath.hpp"
#include <rw/math/LinearAlgebra.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace sdurws::ird::modeling;

/**
 * @brief 构造二轴无量纲常耦合矩阵，窗口 [0,1]，申报 κ 故意固定为 1。
 * @param values [in] 恰四个有限行主序元素；所有权不转移。
 * @return 独立值对象；判定必须重算，不能信任 conditionNumber。
 * @throws std::bad_alloc 内存分配失败时向测试框架传播。
 */
CouplingDesign coupling2(const std::vector<double>& values)
{
    CouplingDesign c;
    c.rows = c.cols = 2;
    c.c = values;
    c.jointRangeFirst = 0;
    c.jointRangeLast = 1;
    c.conditionNumber = 1.0;
    return c;
}
}  // namespace

TEST(CouplingReuse, ProductionSourceUsesUpstream_REUSE_T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02", "MDL-21"},
                  std::vector<std::string>{"H1", "H2"});
    // 静态接线是补充证据；另有真实生产断开实验，不能以此代替结果消费检查。
    const auto path = std::filesystem::path(IRD_MODELING_UNIT_ROOT)
                      / "modeling/src/CouplingMath.hpp";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good()) << "必须读取本次构建的生产源码";
    const std::string source{std::istreambuf_iterator<char>(file), {}};
    EXPECT_NE(source.find("rw::math::LinearAlgebra::svd("), std::string::npos)
        << "生产适配必须直接消费上游 API";
    EXPECT_EQ(source.find("kCouplingJacobiMaxSweeps"), std::string::npos)
        << "旧扫描内核不得留作回退";
}

TEST(CouplingReuse, IndependentSpectrumAndExtremeScale_REUSE_T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21", "NFR-COR-02"},
                  std::vector<std::string>{"I-MDL-11"});
    // 对称 2×2 块的特征值为 4/1，第三轴为 -2，故奇异值手算为 4/2/1。
    // 同比缩放不改变条件数；容差 2e-14 留给双精度分解与乘回尺度的舍入。
    for (const double scale : {1e-200, 1.0, 1e200}) {
        std::vector<double> matrix{2.5, 1.5, 0.0, 1.5, 2.5, 0.0, 0.0, 0.0, -2.0};
        for (double& value : matrix) { value *= scale; }
        const auto before = matrix;
        const auto sigma = couplingmath::couplingSingularValues(matrix, 3);
        ASSERT_EQ(sigma.size(), 3U);
        EXPECT_EQ(matrix, before) << "适配不得写入调用方的耦合事实";
        EXPECT_NEAR(sigma[0] / scale, 4.0, 2e-14);
        EXPECT_NEAR(sigma[1] / scale, 2.0, 2e-14);
        EXPECT_NEAR(sigma[2] / scale, 1.0, 2e-14);
        EXPECT_NEAR(couplingmath::couplingConditionNumber(sigma), 4.0, 2e-14);
        EXPECT_EQ(sigma, couplingmath::couplingSingularValues(matrix, 3))
            << "固定编译环境内相同输入可复现；不承诺跨编译器逐位一致";
    }
}

TEST(CouplingReuse, RankAndConditionBoundariesThroughProduction_REUSE_T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21"},
                  std::vector<std::string>{"I-MDL-11"});
    // 真正编辑边界入口：阈值附近各留 1e-6 相对距离，避免把舍入争议当契约。
    EXPECT_TRUE(checkCouplingMatrix(coupling2({1.0, 0.0, 0.0, 1e-8}), 2).ok());
    EXPECT_TRUE(checkCouplingMatrix(coupling2({1.0, 0.0, 0.0, 1e-8 * (1 + 1e-6)}), 2).ok());
    EXPECT_EQ(checkCouplingMatrix(coupling2({1.0, 0.0, 0.0, 1e-8 * (1 - 1e-6)}), 2).violationKind,
              "ill-conditioned");
    EXPECT_EQ(checkCouplingMatrix(coupling2({1.0, 0.0, 0.0, 1e-12 * (1 + 1e-6)}), 2).violationKind,
              "ill-conditioned");
    EXPECT_EQ(checkCouplingMatrix(coupling2({1.0, 0.0, 0.0, 1e-12}), 2).violationKind,
              "singular");
    EXPECT_EQ(checkCouplingMatrix(coupling2({1.0, 0.0, 0.0, 1e-12 * (1 - 1e-6)}), 2).violationKind,
              "singular");
    EXPECT_EQ(checkCouplingMatrix(coupling2({1.0, 2.0, 2.0, 4.0}), 2).violationKind,
              "singular");
    EXPECT_EQ(checkCouplingMatrix(coupling2({0.0, 0.0, 0.0, 0.0}), 2).violationKind,
              "singular");
}

TEST(CouplingReuse, ProductionConsumesRecomputedCondition_REUSE_T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21"},
                  std::vector<std::string>{"I-MDL-11"});
    // C 的奇异值为 3/1，申报 κ=1；断开真实适配后此断言必须失败。
    const auto result = checkCouplingMatrix(coupling2({2.0, 1.0, 1.0, 2.0}), 2);
    ASSERT_TRUE(result.ok());
    // κ 正确是继续比较谱比值的前提；断开时先报根因，避免派生重复失败。
    ASSERT_NEAR(result.recomputedConditionNumber, 3.0, 2e-14);
    EXPECT_NEAR(result.singularSigmaRatio, 1.0 / 3.0, 2e-14);
}

TEST(CouplingReuse, ExistingInvalidInputContract_REUSE_T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21"},
                  std::vector<std::string>{"I-MDL-11"});
    EXPECT_TRUE(couplingmath::couplingSingularValues({}, 0).empty());
    EXPECT_TRUE(couplingmath::couplingSingularValues({1.0}, 2).empty());
    // 非有限输入由公共入口拒绝，不把非法数据交给上游分解或默默修复。
    EXPECT_EQ(checkCouplingMatrix(coupling2({1.0, 0.0, 0.0,
              std::numeric_limits<double>::quiet_NaN()}), 2).violationKind,
              "element-not-finite");
}

TEST(CouplingReuse, UpstreamComparisonAndReconstruction_REUSE_T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21"},
                  std::vector<std::string>{"I-MDL-11", "H6"});
    // 非对角稠密矩阵：直接上游分解并重构，补充独立解析 oracle，二者不可互替。
    const std::vector<double> values{1, 2, -3, 4, 2, 5, 7, -1,
                                      4, -2, 6, 3, 9, 1, 2, 8};
    Eigen::MatrixXd matrix(4, 4), u, v;
    for (Eigen::Index i = 0; i < 4; ++i) {
        for (Eigen::Index j = 0; j < 4; ++j) { matrix(i, j) = values[i * 4 + j]; }
    }
    Eigen::VectorXd reference;
    rw::math::LinearAlgebra::svd(matrix, u, reference, v);
    const auto sigma = couplingmath::couplingSingularValues(values, 4);
    ASSERT_EQ(sigma.size(), 4U);
    for (std::size_t i = 0; i < sigma.size(); ++i) {
        EXPECT_NEAR(sigma[i], reference[static_cast<Eigen::Index>(i)], 1e-12);
    }
    EXPECT_LT((matrix - u * reference.asDiagonal() * v.transpose()).norm(), 1e-12);
}
