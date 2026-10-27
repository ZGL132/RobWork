/**
 * @file CouplingMath.hpp
 * @brief MDL-21 耦合矩阵到 RobWork SVD 的私有薄适配与业务阈值。
 *
 * 设计依据：units/modeling.md §8.1、I-MDL-11、P-MDL-7；REUSE-T07/H1—H3。
 * 分解唯一来源为 rw::math::LinearAlgebra::svd（sdurw_math），不保留自写
 * Jacobi 或备用算法。适配只处理行主序输入、极端有限尺度和返回值容器。
 * checkCouplingMatrix 仍负责结构/有限性/窗口校验及稳定诊断，不把业务
 * 阈值移入上游；runtime 的独立迁移由其能力任务负责，禁止跨单元私有头。
 *
 * 阈值仍为 P-RT-7 设计默认、modeling 域内单点；policy/附录 D 冻结时按
 * P-MDL-7 同步，不在本次数值来源替换中改变归属或范围。
 * 全部函数使用调用栈私有值，无共享状态，可重入；固定构建环境内可复现，
 * 不承诺不同编译器/上游版本间逐位一致。分配异常直接传播，不伪造成功。
 */

#ifndef IRD_MODELING_SRC_COUPLINGMATH_HPP
#define IRD_MODELING_SRC_COUPLINGMATH_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <rw/math/LinearAlgebra.hpp>

namespace sdurws::ird::modeling {
namespace couplingmath {

// =====================================================================
// 阈值单点（P-MDL-7——本单元内唯一书写点；禁第二处字面）
// =====================================================================

/// 耦合矩阵"病态"条件数上限：κ ≤ 1×10⁸（无量纲——I-MDL-11/§8.1）。
/// 数值＝runtime P-RT-7 设计默认（units/runtime.md P-RT-7）；归属裁决
/// （并入附录 D 冻结 / 改归 EngineeringPolicySet）未决前 runtime 维持该
/// 默认，modeling 单点跟随——P-RT-7 冻结时同步本常量（P-MDL-7 处置约束；
/// 单元卡 §14.3/§15 登记）。
inline constexpr double kCouplingConditionNumberLimit = 1e8;

/// 耦合矩阵"数值奇异"判定分界：σmin ≤ σmax×1×10⁻¹²（⇔ κ ≥ 1×10¹²）视为
/// det≈0（双精度有效秩为零）。语义：奇异与病态是**两档**——V-18 反例
/// （奇异阵/条件数 1×10⁹ 病态阵）必须落档可区分（比较型诊断的 actual/
/// expected 语义不同：奇异档比较 σmin/σmax 比值、病态档比较 κ）；分界值
/// 与 runtime 校验器奇异分界同口径（同族设计默认，1×10¹² 为本单元设计
/// 默认——若所有者后续将其并入策略集，随 P-RT-7 同批登记）。
inline constexpr double kCouplingSingularSigmaRatio = 1e-12;

/**
 * @brief 经上游 SVD 取得方阵奇异值降序序列，不改变调用方数据。
 * @param c [in] 行主序 n*n 个元素，量纲与返回奇异值相同；生产入口先拒非有限值。
 * @param n [in] 方阵阶数；0 或尺寸不符返回空序列。
 * @return n 个非负奇异值；零矩阵返回全零；无限尺度保留原防御性无穷返回。
 * @throws std::bad_alloc 工作矩阵或输出分配失败；不吞错，不回退旧算法。
 *
 * 缩放只改变量纲尺度，乘回后供原业务计算无量纲条件数；分解、收敛与排序
 * 的数值能力复用上游，不在产品实现旋转迭代。线程安全，无模型所有权转移。
 */
inline std::vector<double> couplingSingularValues(const std::vector<double>& c,
                                                  std::size_t n)
{
    if (n == 0 || c.size() != n * n) {
        return {};  // 维度不符＝调用方契约违约——空序列防御（不猜测不伪造）
    }
    // 保留既有量纲缩放。分解中的统计量涉及矩阵元素平方；不缩放时
    // 1e-200 级合法耦合系数会平方下溢为 0，1e200 级有限系数会平方上溢为
    // Inf。奇异值整体乘回 scale 不改变条件数和奇异/病态判定。
    double scale = 0.0;
    for (const double value : c) {
        scale = std::max(scale, std::fabs(value));
    }
    if (!(scale > 0.0)) {
        return std::vector<double>(n, 0.0);  // 零矩阵：所有奇异值均为 0
    }
    if (!std::isfinite(scale)) {
        return std::vector<double>(n, std::numeric_limits<double>::infinity());
    }
    // 行主序业务容器转换为上游既有 Eigen 参数；除以 scale 避免极端有限
    // 输入的平方上溢/下溢。模型、单位与诊断均留在调用方既有边界。
    const auto dimension = static_cast<Eigen::Index>(n);
    Eigen::MatrixXd matrix(dimension, dimension);
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t col = 0; col < n; ++col) {
            matrix(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col))
                = c[row * n + col] / scale;
        }
    }
    Eigen::MatrixXd u, v;
    Eigen::VectorXd sigma;
    rw::math::LinearAlgebra::svd(matrix, u, sigma, v);

    // 只消费奇异值，U/V 由上游接口要求提供，不产生新的分解/求逆入口。
    std::vector<double> singularValues(n);
    for (std::size_t i = 0; i < n; ++i) {
        singularValues[i] = sigma[static_cast<Eigen::Index>(i)] * scale;
    }
    return singularValues;
}

/**
 * @brief 由奇异值序列计算 2-范数条件数 κ=σmax/σmin（无量纲）。
 *
 * @param singularValues [in] 降序奇异值序列（couplingSingularValues 产出；
 *                     空序列＝维度违约——返回 0 由调用方按结构非法处置）
 * @return κ ≥ 1；σmin==0（数值奇异）时返回无穷（std::numeric_limits
 *         <double>::infinity()——调用方不得将其放入 SourcedValue（Provided
 *         态须有限，I-MDL-3），比较型诊断的 actual 侧应改用 σmin/σmax
 *         比值承载（有限可比值——ERR-01 不伪造数值）
 *
 * 纯函数；线程安全；确定性。
 */
inline double couplingConditionNumber(const std::vector<double>& singularValues)
{
    if (singularValues.empty()) { return 0.0; }
    const double sigmaMax = singularValues.front();
    const double sigmaMin = singularValues.back();
    if (sigmaMin <= 0.0) {
        // 严格零列（数值奇异极限）→κ 无穷（见注——消费方以 σ 比值承载）。
        return std::numeric_limits<double>::infinity();
    }
    return sigmaMax / sigmaMin;
}

}  // namespace couplingmath
}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_SRC_COUPLINGMATH_HPP
