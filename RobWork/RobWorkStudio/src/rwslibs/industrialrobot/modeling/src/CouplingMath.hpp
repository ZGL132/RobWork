/**
 * @file   CouplingMath.hpp
 * @brief  modeling 单元私有：传动耦合矩阵（MDL-21，R2）的数值数学核——
 *         奇异值计算（单侧 Jacobi/Hestenes SVD）、条件数重算与阈值单点。
 *
 * 设计依据：
 *   - units/modeling.md §8.1（"病态/非常矩阵"行：非方阵、奇异、条件数
 *     >1×10⁸→编辑边界比较型诊断＋应用阻断；"与 runtime InputInvalid 同
 *     口径"）、§4.10 I-MDL-11（R2 下 C 须方阵、可逆、条件数 ≤1×10⁸）
 *   - 需求 MDL-21（M-12：非常矩阵/病态给诊断并阻止，v1.8）
 *   - P-MDL-7（阈值登记）：条件数阈值 1×10⁸ 为 runtime 设计默认（P-RT-7，
 *     units/runtime.md P-RT-7），非附录 D 冻结值；处置约束"modeling 引用
 *     runtime/常量单源（不私设），P-RT-7 冻结时同步"。
 *
 * 背景说明（为什么本单元要有自己的 SVD，以及阈值"单源"的现实落位——
 * 第一读者须知）：耦合矩阵的条件数/可逆性**重算复核**是 I-MDL-11 的建模
 * 侧履行点（runtime DescriptionValidator 在编译期同样重算——"校验器一律
 * 以重算值为准"；两处判定互为防线纵深，建模侧先拦、编译侧兜底）。本头
 * 的数学核与 runtime 的实现互不可见（R-2：跨单元私有头禁止；runtime 的
 * SVD 位于其 src/ 私有实现，未从公共头导出——本单元无法 include），故
 * 按"算法各自实现、阈值语义同源登记"的口径落位：
 *   1) 算法：单侧 Jacobi（Hestenes）——与 runtime 同族算法、同档收敛
 *      纪律（固定扫描序/相对收敛阈/轮数上限），同输入同奇异值（确定性
 *      NFR-COR-02）；良好条件下两实现的 κ 在机器精度内一致，阻断判定
 *      （κ 与阈值的比较）对良态/病态矩阵结论一致。
 *   2) 阈值：1×10⁸ 在本单元内**单点承载**（kCouplingConditionNumberLimit
 *      ——modeling 域内唯一书写点，Parts.cpp 历史字面已收敛于此）；该值
 *      与 runtime P-RT-7 设计默认同值，P-RT-7 冻结（并入附录 D 或改归
 *      EngineeringPolicySet）时本常量随之同步——同步义务登记于单元卡
 *      §14.3 P-MDL-7 行与 §15 增量（DTB §5.4 实现细节登记）。在 runtime
 *      导出公共阈值常量之前，本单元以单一常量点＋行为测试钉扎（病态反例
 *      κ=1×10⁹ 阻断）保证与 runtime 口径一致；本单元域内不出现第二处
 *      阈值字面（ Parts.cpp 既有 1e8 字面已随 WP-13-T18 收敛）。
 *
 * 线程安全：全部纯函数（无共享可变状态）——并发只读可重入（§3.4）。
 * 确定性：固定列对扫描序（p 升序 × q 升序）、固定收敛阈与轮数上限——
 * 同输入逐位同输出（NFR-COR-02）。
 */

#ifndef IRD_MODELING_SRC_COUPLINGMATH_HPP
#define IRD_MODELING_SRC_COUPLINGMATH_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <vector>

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

/// 单侧 Jacobi 最大扫描轮数：收敛上界保护（正规小矩阵 2～4 轮内收敛；
/// 耦合矩阵工程规模为腕部窗口 2～3 阶、上限按机械臂可动关节数个位数阶——
/// 固定轮数保证确定性终止，即使病态矩阵迭代缓慢也在有限步内给出奇异值）。
inline constexpr int kCouplingJacobiMaxSweeps = 60;

/// 单侧 Jacobi 相对收敛阈：列对内积 |γ| ≤ 1×10⁻¹⁶×√(αβ) 视为已正交——
/// 机器精度层（再转只引入噪音，奇异值相对精度因此 ~eps）。
inline constexpr double kCouplingJacobiOrthogonality = 1e-16;

// =====================================================================
// 奇异值与条件数（纯函数；确定性）
// =====================================================================

/**
 * @brief 计算方阵的奇异值降序序列（单侧 Jacobi/Hestenes SVD）。
 *
 * 算法（逐步——供 review 对照数值代数教材 Hestenes 1958 形态）：
 *   第 1 步：工作副本按行主序展开（a[i*n+j]＝元素 (i,j)）；
 *   第 2 步：反复扫描全部列对 (p,q)（p<q，外层 p 升序、内层 q 升序——
 *            固定扫描序保证同输入同旋转序列），对每对计算
 *            α=‖a_p‖²、β=‖a_q‖²、γ=a_p·a_q（a_p/a_q 为第 p/q 列向量）；
 *   第 3 步：|γ| 已达机器精度正交（|γ| ≤ ε·√(αβ)）则跳过该对；否则对
 *            两列施加 Jacobi 平面旋转（消去列间内积——ζ=(β−α)/(2γ)、
 *            t=sign(ζ)/(|ζ|+√(1+ζ²))（较小倾角根——数值稳定）、
 *            c=1/√(1+t²)、s=c·t），旋转后 A→A·J（J 为该平面旋转矩阵）；
 *   第 4 步：整轮零旋转＝已收敛，提前退出（确定性路径）；未收敛达轮数
 *            上限同样终止（病态矩阵的奇异值已收敛到判档精度——分界
 *            1×10⁻¹² 相对量级远大于迭代残差）；
 *   第 5 步：奇异值＝正交化后各列的欧氏范数（Hestenes 定理——A 的奇异
 *            值即 A·J 序列极限的列范数），降序排列返回。
 *
 * @param c [in] 方阵元素（行主序；恰 n*n 个——调用方保证，非方阵属
 *            调用方契约违约，本函数不校验维度合法性）
 * @param n [in] 方阵阶数（≥1；0 阶＝无意义输入，返回空序列）
 * @return 奇异值降序序列（σmax 在首；n 个——σi ≥ 0，单位与矩阵元素同
 *         量纲；条件数消费方以 σmax/σmin 比值使用，量纲相消无量纲）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02）。
 */
inline std::vector<double> couplingSingularValues(const std::vector<double>& c,
                                                  std::size_t n)
{
    if (n == 0 || c.size() != n * n) {
        return {};  // 维度不符＝调用方契约违约——空序列防御（不猜测不伪造）
    }
    std::vector<double> a = c;  // 工作副本（原矩阵不被修改——值语义）
    for (int sweep = 0; sweep < kCouplingJacobiMaxSweeps; ++sweep) {
        bool rotated = false;  // 本轮是否发生过旋转（零旋转＝收敛信号）
        for (std::size_t p = 0; p + 1 < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                // 列对统计量（α/β＝两列范数平方、γ＝两列内积）。
                double alpha = 0.0;
                double beta = 0.0;
                double gamma = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    const double ap = a[i * n + p];
                    const double aq = a[i * n + q];
                    alpha += ap * ap;
                    beta += aq * aq;
                    gamma += ap * aq;
                }
                // 零列或已正交（相对判据）→跳过该对（继续其余对——部分
                // 收敛面；整轮全跳过才触发提前退出）。
                if (gamma == 0.0) { continue; }
                const double normProduct = std::sqrt(alpha * beta);
                if (normProduct == 0.0) { continue; }
                if (std::fabs(gamma)
                    <= kCouplingJacobiOrthogonality * normProduct) {
                    continue;
                }
                // Jacobi 平面旋转参数（较小倾角根——|t|≤1 保证数值稳定；
                // ζ 的符号决定旋转方向）。
                const double zeta = (beta - alpha) / (2.0 * gamma);
                const double t =
                    (zeta >= 0.0 ? 1.0 : -1.0)
                    / (std::fabs(zeta) + std::sqrt(1.0 + zeta * zeta));
                const double cs = 1.0 / std::sqrt(1.0 + t * t);
                const double sn = cs * t;
                // 列更新（A←A·J——仅两列线性组合，逐行原位写回）。
                for (std::size_t i = 0; i < n; ++i) {
                    const double ap = a[i * n + p];
                    const double aq = a[i * n + q];
                    a[i * n + p] = cs * ap - sn * aq;
                    a[i * n + q] = sn * ap + cs * aq;
                }
                rotated = true;
            }
        }
        if (!rotated) { break; }  // 整轮零旋转＝已收敛（确定性提前退出）
    }
    // 奇异值＝各列欧氏范数；降序排列（σmax 在首——条件数取首尾比）。
    std::vector<double> singularValues(n, 0.0);
    for (std::size_t q = 0; q < n; ++q) {
        double norm2 = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            norm2 += a[i * n + q] * a[i * n + q];
        }
        singularValues[q] = std::sqrt(norm2);
    }
    std::sort(singularValues.begin(), singularValues.end(),
              std::greater<double>());
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
