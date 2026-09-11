/**
 * @file   DescriptionValidator.cpp
 * @brief  Description 结构与单位校验器实现（S3 段）——逐项硬校验＋量级
 *         抽样警告＋耦合矩阵奇异/病态判定（对称 Jacobi 条件数）。
 *
 * 设计依据：
 *   - units/runtime.md §5.2 S3 行、§4.2/§4.4/§4.3.3/§4.3.4/§6.6（各检查
 *     项与阈值/错误码的原文出处，逐处随代码标注）
 *   - 需求 MDL-21/M-6/M-12（耦合矩阵比较型诊断）、MDL-06（断言分域——
 *     编译器复核 Provided 值合法性）、NFR-COR-03（非有限拒绝/缺失不伪造）、
 *     NFR-COR-02（确定性）
 *   - 任务契约 tasks/foundation/RT-T03.json（acceptance 1～3 被测主体；
 *     RT-CPX-3/RT-CPL-1/RT-BW-6 校验器部分）
 *
 * 确定性来源（NFR-COR-02）：检查次序固定（下标升序）；全部阈值为编译期
 * 常量（出处随常量注释）；条件数经对称 Jacobi 迭代——固定扫描次序＋固定
 * 收敛阈值＋固定最大轮数，同一输入在同一二进制内浮点运算序列逐位一致。
 */

#include "DescriptionValidator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sdurws::ird::runtime {

namespace {

// =====================================================================
// 阈值常量（全部带出处——AGENTS.md §2.4"魔法数字必须解释来源"）。
// =====================================================================

/// 旋转矩阵正交性容差：max|RᵀR−I| ≤ 1×10⁻¹²（§6.6/§4.3.2 原文；附录 D
/// 第 6 项对称性容差同尺度）。
constexpr double kOrthoTolerance = 1e-12;

/// 惯量张量对称性容差：|I_ij−I_ji| ≤ 1×10⁻¹²（§4.3.3"对称性容差附录 D
/// 第 6 项"同尺度——与正交性容差同一量级）。
constexpr double kSymmetryTolerance = 1e-12;

/// 耦合矩阵"数值奇异"判定分界：σmin ≤ σmax×1×10⁻¹²（⇔ 条件数 ≥1×10¹²）
/// 视为 det≈0（双精度有效秩为零——奇异）。
/// 语义：奇异与病态（P-RT-7 的 1×10⁸）是两档——RT-CPL-1 的条件数 1×10¹⁰
/// 反例必须落在"病态"而非"奇异"，本分界保证二者可区分（比较型诊断的
/// 阈值列不同）。1×10¹² 为本单元设计默认（警告级错误码同 InputInvalid、
/// 同为阻止边界；若所有者后续将其并入策略集，随 P-RT-7 同批登记）。
constexpr double kSingularSigmaRatio = 1e-12;

/// 耦合矩阵"病态"条件数上限：κ ≤ 1×10⁸（MDL-21/§4.3.4——设计默认，
/// 登记 P-RT-7 待策略侧确认归属）。处置约束（§15.3 P-RT-7）：runtime
/// 维持 1×10⁸ 默认并随诊断输出实际条件数；归属裁决不私裁。
constexpr double kConditionNumberLimit = 1e8;

/// 旋转关节限位量级警告阈：|q| > 4π×10 rad ≈ 125.7 rad（≈ 7200°）——
/// §4.4 原文（"旋转关节限位量级复核（|q|>4π×10 等异常量级给警告——
/// 不阻断）"）。工程限位在 ±2π 量级；±180 一类读数是 deg 误作 rad 的
/// 典型特征，抽样警告供用户复核。
constexpr double kAngleMagnitudeWarn = 4.0 * 3.14159265358979323846 * 10.0;

/// 移动关节限位量级警告阈：|q| > 1×10² m——§4.4 同段"等异常量级"家族的
/// 移动量纲取值（工程行程远小于百米；±1000 一类读数是 mm 误作 m 的典型
/// 特征）。警告级不阻断、非 P-RT-7 类阻止边界（本单元抽样设计默认）。
constexpr double kLengthMagnitudeWarn = 1e2;

/// 对称 Jacobi 最大扫描轮数：收敛上界保护（正规小矩阵 6～10 轮内收敛；
/// 60 轮对 64 阶以内矩阵远超充分——固定轮数保证确定性终止）。
constexpr int kJacobiMaxSweeps = 60;

/// 对称 Jacobi 收敛阈：非对角元素平方和 ≤ 1e-30×max(1, 对角平方和)——
/// 达到即认为对角化完成（相对判据，免受矩阵整体缩放影响）。
constexpr double kJacobiConvergence = 1e-30;

/// 单侧 Jacobi（Hestenes）正交收敛阈：两列内积 |γ| ≤ 1e-16×√(αβ) 视为
/// 已正交（机器精度层；再转只引入噪音——奇异值相对精度因此 ~eps，
/// 与条件数无关，见 singularValuesOneSidedJacobi 函数头）。
constexpr double kJacobiOrthogonality = 1e-16;

// =====================================================================
// 小工具（全部纯函数，无状态）。
// =====================================================================

/// 是否有限数（NaN/±Inf 拒绝判据——NFR-COR-03 的字面实现）。
bool finite(double v) noexcept
{
    return std::isfinite(v);
}

/// 向量是否全部分量有限（rw::math::Vector3D——三个 double 分量）。
bool finite(const rw::math::Vector3D<double>& v) noexcept
{
    return finite(v[0]) && finite(v[1]) && finite(v[2]);
}

/// 3×3 旋转矩阵的行列式（余子式展开——逐项确定，无算法分支）。
double determinant3x3(const rw::math::Rotation3D<double>& r) noexcept
{
    return r(0, 0) * (r(1, 1) * r(2, 2) - r(1, 2) * r(2, 1))
         - r(0, 1) * (r(1, 0) * r(2, 2) - r(1, 2) * r(2, 0))
         + r(0, 2) * (r(1, 0) * r(2, 1) - r(1, 1) * r(2, 0));
}

/// 报告一条阻断级问题（Error——ok() 翻 false）。
void addError(ValidationReport& report, RuntimeErrorCode code, std::string fieldPath,
              std::string detail)
{
    ValidationIssue issue;
    issue.severity = ValidationSeverity::Error;
    issue.code = code;
    issue.fieldPath = std::move(fieldPath);
    issue.detail = std::move(detail);
    report.issues.push_back(std::move(issue));
}

/// 报告一条警告级问题（Warning——不阻断，§4.4 抽样纪律）。
void addWarning(ValidationReport& report, RuntimeErrorCode code, std::string fieldPath,
                std::string detail)
{
    ValidationIssue issue;
    issue.severity = ValidationSeverity::Warning;
    issue.code = code;
    issue.fieldPath = std::move(fieldPath);
    issue.detail = std::move(detail);
    report.issues.push_back(std::move(issue));
}

/// 报告一条带比较型三要素的阻断级问题（M-6/M-12——实际值/阈值随诊断）。
void addComparisonError(ValidationReport& report, RuntimeErrorCode code,
                        std::string fieldPath, std::string detail, double actual,
                        double threshold, std::string quantity)
{
    ValidationIssue issue;
    issue.severity = ValidationSeverity::Error;
    issue.code = code;
    issue.fieldPath = std::move(fieldPath);
    issue.detail = std::move(detail);
    ValidationComparison cmp;
    cmp.actual = actual;
    cmp.threshold = threshold;
    cmp.quantity = std::move(quantity);
    issue.comparison = std::move(cmp);
    report.issues.push_back(std::move(issue));
}

/**
 * @brief 刚体变换合法性检查（RT-BW-6 校验器面的共用实现）。
 *
 * 检查序与错误归属：
 *   ①非有限（NFR-COR-03）：R 任一分量或平移任一分量为 NaN/±Inf——
 *     定位到 <base>.R / <base>.P；
 *   ②正交性（§6.6 容差 1×10⁻¹²）：max|RᵀR−I| 超差——比较型诊断
 *     （实测最大偏差 vs 容差），定位 <base>.R；
 *   ③反射（RT-BW-6）：det(R) ≤ 0——合法旋转须 det=+1，反射矩阵虽正交
 *     但 det=−1——比较型诊断（实测 det vs 0），定位 <base>.R。
 * 前置说明：①失败时 ②③ 的数值无意义（NaN 传播），跳过——同一变换
 * 只报非有限，不叠报（诊断可读性）。
 *
 * @param base [in] 字段路径前缀（如 "joints[0].origin"——拼 ".R"/".P"）
 */
void checkTransform(ValidationReport& report, const rw::math::Transform3D<double>& t,
                    const std::string& base)
{
    // 第①步：非有限分量检查（R 9 项＋平移 3 项；NaN 与 ±Inf 同拒——
    // 两者都无法参与后续任何数值校验，静默放行＝NFR-COR-03 禁止的
    // "转换为默认通过"）。
    bool rFinite = true;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!finite(t.R()(i, j))) { rFinite = false; }
        }
    }
    const bool pFinite = finite(t.P());
    if (!rFinite) {
        addError(report, RuntimeErrorCode::InputInvalid, base + ".R",
                 base + ".R：旋转矩阵含非有限分量（NaN/±Inf），拒绝编译"
                 "（NFR-COR-03：非有限不得静默通过）");
    }
    if (!pFinite) {
        addError(report, RuntimeErrorCode::InputInvalid, base + ".P",
                 base + ".P：平移向量含非有限分量（NaN/±Inf），拒绝编译"
                 "（NFR-COR-03；RT-BW-6 反例字段）");
    }
    if (!rFinite || !pFinite) {
        return; // 数值无效时正交性/行列式无意义——不叠报（见函数注释前置说明）
    }

    // 第②步：正交性——max|RᵀR−I|（逐元素绝对偏差最大值）。合法旋转的
    // RᵀR=I；超差说明矩阵被错误缩放/混入投影，反射检查在正交通过后
    // 才有意义（非正交矩阵的 det 判读会被缩放污染）。
    double maxDev = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double dot = 0.0;
            for (int k = 0; k < 3; ++k) {
                dot += t.R()(k, i) * t.R()(k, j); // (RᵀR)(i,j)＝第 i 列与第 j 列内积
            }
            const double dev = std::fabs(dot - (i == j ? 1.0 : 0.0));
            maxDev = std::max(maxDev, dev);
        }
    }
    if (maxDev > kOrthoTolerance) {
        addComparisonError(report, RuntimeErrorCode::InputInvalid, base + ".R",
                           base + ".R：旋转矩阵非正交（max|RᵀR−I| 超容差）——"
                           "RT-BW-6 反例字段；实测最大偏差见比较值",
                           maxDev, kOrthoTolerance, "rotation-orthogonality-max-deviation");
        return; // 非正交已报；det 判读被缩放污染，不再叠加（见上）
    }

    // 第③步：反射检查——det(R) ≤ 0 即非法（合法旋转 det=+1；det=−1 的
    // 正交阵是反射，会导致手性翻转：RT-BW-6 反例）。正交性已通过时
    // det ∈ {+1,−1}（数值上接近），阈值取 0 判别即可。
    const double det = determinant3x3(t.R());
    if (det <= 0.0) {
        addComparisonError(report, RuntimeErrorCode::InputInvalid, base + ".R",
                           base + ".R：旋转矩阵为反射（det ≤ 0，手性翻转）——"
                           "RT-BW-6 反例字段；实测行列式见比较值",
                           det, 0.0, "rotation-determinant");
    }
}

/**
 * @brief 对称 Jacobi 特征值（小对称矩阵——惯量正定性复核用）。
 *
 * 算法（数值教科书经典循环 Jacobi）：反复用平面旋转把非对角元素平方和
 * 压向零，收敛后对角元即特征值。确定性保障（NFR-COR-02）：扫描次序
 * 固定（行主序上三角枚举）＋收敛阈/最大轮数固定（文件头常量）——同一
 * 输入在同一二进制内产生逐位一致的结果。工程规模：惯量恒 3 阶，轮数
 * 上界充分（kJacobiMaxSweeps）。
 *
 * ★ 为何不用于耦合矩阵条件数：两侧算法（经 CᵀC）把动态范围平方化——
 *   κ=1e10 的矩阵其 CᵀC 条件数达 1e20，小特征值完全沉入舍入噪声，
 *   奇异（κ≥1e12）与病态（κ>1e8）将无法可靠分档甚至漏阻（详见
 *   singularValuesOneSidedJacobi 的注释）。惯量复核不涉小奇异值分辨
 *   （SPD 判定只有"最小特征值是否＞0"一档，物理惯量远离奇异），仍用本算法。
 *
 * @param a [in] 行主序 n×n 对称矩阵（调用方持有存储；本函数只读）
 * @param n [in]     阶数（≥1）
 * @return 特征值升序向量（长度 n；不抛）
 */
std::vector<double> symmetricEigenvalues(const std::vector<double>& a, std::size_t n)
{
    // 工作副本（迭代就地修改）；n=0 防御性返回空（调用方保证 ≥1）。
    std::vector<double> m(a);
    if (n == 0) { return m; }

    // 对角平方和的缩放基准（相对收敛判据用——免受矩阵整体缩放影响）。
    double diagScale = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        diagScale += m[i * n + i] * m[i * n + i];
    }
    const double tol = kJacobiConvergence * std::max(1.0, diagScale);

    for (int sweep = 0; sweep < kJacobiMaxSweeps; ++sweep) {
        // 收敛判定：非对角元素平方和足够小即停止（特征值已稳定在对角元）。
        double off = 0.0;
        for (std::size_t p = 0; p < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                off += m[p * n + q] * m[p * n + q];
            }
        }
        if (off <= tol) { break; }

        // 一轮扫描：对每个上三角非对角位置构造零化旋转（固定次序——确定性）。
        for (std::size_t p = 0; p + 1 < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                const double apq = m[p * n + q];
                if (apq == 0.0) { continue; } // 已零化——跳过（不扰动收敛）

                // 经典 Jacobi 旋转角：t=c²（tan）的最小解——数值稳定分支
                // （大 theta 时避免平方溢出的是 theta 倒数形式）。
                const double theta = (m[q * n + q] - m[p * n + p]) / (2.0 * apq);
                const double sign = theta >= 0.0 ? 1.0 : -1.0;
                const double tTan = sign / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(tTan * tTan + 1.0);
                const double s = tTan * c;

                // 行列同步更新 p、q 两行两列（对称矩阵只需改 2×2 交叉块与
                // 对角元——标准原地公式）。
                for (std::size_t k = 0; k < n; ++k) {
                    const double mkp = m[k * n + p];
                    const double mkq = m[k * n + q];
                    m[k * n + p] = c * mkp - s * mkq;
                    m[k * n + q] = s * mkp + c * mkq;
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double mpk = m[p * n + k];
                    const double mqk = m[q * n + k];
                    m[p * n + k] = c * mpk - s * mqk;
                    m[q * n + k] = s * mpk + c * mqk;
                }
            }
        }
    }

    // 对角元即特征值；升序排序（比较用 min/max 的取法确定）。
    std::vector<double> eig(n);
    for (std::size_t i = 0; i < n; ++i) { eig[i] = m[i * n + i]; }
    std::sort(eig.begin(), eig.end());
    return eig;
}

/**
 * @brief 单侧 Jacobi（Hestenes）奇异值——耦合矩阵奇异/病态判定的数值基础。
 *
 * 算法：对 C 的**列**反复施加平面旋转使其两两正交；收敛后各列的范数即
 * 奇异值 σᵢ。确定性保障（NFR-COR-02）：扫描次序固定＋正交收敛阈/最大
 * 轮数固定（文件头常量）——同一输入在同一二进制内逐位一致。
 *
 * ★ 为何必须单侧（而不是经 CᵀC 的两侧法）：小奇异值的**相对精度**。
 *   两侧法把问题平方化（κ=1×10¹⁰ ⇒ CᵀC 条件数 1×10²⁰），小奇异值淹没在
 *   sqrt(舍入噪声) 层，奇异与病态无法分档、甚至奇异矩阵可能被误判良态
 *   放行（RT-CPL-1 直接漏阻——不可接受）。单侧法的奇异值＝正交化后列的
 *   范数（无相消求和），相对误差 ~eps 与条件数无关（Demmel–Veselić 精度
 *   结论；本处阶数为腕部数阶，工程上充分）——σmin=5×10⁻¹⁵ 与分界
 *   2×10⁻¹² 之间四个量级的判距可靠可辨。
 *
 * @param a [in] 行主序 n×n 矩阵（调用方持有存储；本函数只读）
 * @param n [in]     阶数（≥1）
 * @return 奇异值降序→升序排列后的升序向量（长度 n；不抛）
 */
std::vector<double> singularValuesOneSidedJacobi(const std::vector<double>& a, std::size_t n)
{
    // 工作副本：旋转只作用于**列**（行主序下即对下标 k*n+p / k*n+q 的
    // 跨行同列元素对做旋转）。
    std::vector<double> m(a);
    if (n == 0) { return m; }

    for (int sweep = 0; sweep < kJacobiMaxSweeps; ++sweep) {
        bool allOrthogonal = true;
        // 一轮扫描：对每对列 (p,q) 做正交化旋转（固定次序——确定性）。
        for (std::size_t p = 0; p + 1 < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                // 列内积三件套：α=‖colP‖²、β=‖colQ‖²、γ=colP·colQ。
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
                // 收敛判据：两列已足够正交（|γ| 相对几何均值 ≤ 1e-16——
                // 机器精度层；继续旋转只会引入噪音而无正交化增益）。
                if (std::fabs(gamma) <= kJacobiOrthogonality * std::sqrt(alpha * beta)) {
                    continue;
                }
                allOrthogonal = false;
                // Hestenes 旋转角（与对称 Jacobi 同构的数值稳定公式）。
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

    // 奇异值＝收敛后各列的范数（正交化后列范数即 σ；无相消求和——
    // 相对精度与条件数无关，见函数头）。
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

/**
 * @brief 耦合矩阵奇异性与条件数检查（RT-CPL-1 的实现主体）。
 *
 * 判定链（§4.3.4/MDL-21/P-RT-7）：
 *   σmin ≤ σmax×1×10⁻¹²（⇔ κ ≥ 1×10¹²，det≈0 的数值意义）⇒ 奇异——
 *   比较型诊断给 σmin vs 奇异分界；否则 κ=σmax/σmin，κ > 1×10⁸ ⇒ 病态——
 *   比较型诊断给 κ vs 1×10⁸（P-RT-7 要求随诊断输出实际条件数）。
 * 奇异值经单侧 Jacobi 直接对 C 计算（小奇异值相对精度见该函数注释——
 * 这是奇异/病态两档可靠分档、且 κ≈1×10⁸ 阻止边界可信的前提）。
 *
 * @param rowsCols [in] 矩阵阶数（前置：调用方已保证方阵＋数据量吻合＋
 *                      全部有限）
 * @param c        [in] 行主序扁平数据（恰 rowsCols² 个元素）
 * @param base     [in] 字段路径前缀（"drivetrain.coupling.C"）
 */
void checkCouplingRankAndCondition(ValidationReport& report, std::size_t rowsCols,
                                   const std::vector<double>& c, const std::string& base)
{
    const std::size_t n = rowsCols;

    // 第一步：单侧 Jacobi 取奇异值（升序）。
    const std::vector<double> sigma = singularValuesOneSidedJacobi(c, n);
    const double sigmaMin = sigma.front();
    const double sigmaMax = sigma.back();

    // 第二步：奇异判定（det≈0）。分界 σmin ≤ σmax×1×10⁻¹² ⇔ κ ≥ 1×10¹²：
    // 该条件下矩阵在双精度下数值不可逆。诊断给出 σmin（比较值）与分界
    // （阈值），detail 携带 σmax/σmin 全值（review 不看文档也能复核）。
    // 注：分界远高于单侧法的 σmin 噪声底（~eps×σmax≈1e-15 量级）——
    // 真奇异/近奇异（κ≥1e12）必然落入本分支，无误判良态的通道。
    const double singularThreshold = sigmaMax * kSingularSigmaRatio;
    if (sigmaMin <= singularThreshold) {
        addComparisonError(report, RuntimeErrorCode::InputInvalid, base,
                           base + "：耦合矩阵奇异（det≈0：σmin ≤ σmax×1e-12）——"
                           "MDL-21 阻止编译；σmax=" + std::to_string(sigmaMax)
                               + "，σmin=" + std::to_string(sigmaMin)
                               + "（比较型诊断：σmin 对奇异分界）",
                           sigmaMin, singularThreshold, "coupling-min-singular-value");
        return; // 奇异已定——κ 分档失去意义（σmin 已在噪声层），不再叠报
    }

    // 第三步：病态判定（P-RT-7 设计默认 1×10⁸）。κ=σmax/σmin——此处
    // σmin > σmax×1e-12 且 σmax>0，商必然有限且 < 1×10¹²。
    const double kappa = sigmaMax / sigmaMin;
    if (kappa > kConditionNumberLimit) {
        addComparisonError(report, RuntimeErrorCode::InputInvalid, base,
                           base + "：耦合矩阵病态（条件数超限）——MDL-21/M-6 阻止"
                           "编译；实际条件数=" + std::to_string(kappa)
                               + "，阈值=1e+08（P-RT-7 设计默认，归属待策略侧确认）",
                           kappa, kConditionNumberLimit, "coupling-condition-number");
    }
    // 其余：良态可逆 C——通过（RT-CPL-1 第三分支：良态 C 正常进入模型）。
}

/**
 * @brief 惯量张量合法性检查（Provided 前置——调用方已判 state==Provided）。
 *
 * 检查序（§4.3.3 CanonicalLink 合法与非法实例行）：
 *   ①非有限（NFR-COR-03）——9 分量逐一；
 *   ②对称性——|I_ij−I_ji| ≤ 1×10⁻¹²（附录 D 第 6 项同尺度），超差给
 *     比较型诊断（实测最大不对称 vs 容差）；
 *   ③正定（SPD）——对称化后最小特征值 > 0；非正定给比较型诊断
 *     （最小特征值 vs 0）。物理含义：惯量张量须正定（任意轴转动惯量
 *     为正）——半正定（含 0）即质量分布退化，非法。
 * ①失败跳过②③（NaN 传播使后续判读无意义）。
 *
 * @param base [in] 字段路径（如 "links[1].inertia"）
 */
void checkInertia(ValidationReport& report, const rw::math::InertiaMatrix<double>& m,
                  const std::string& base)
{
    // 第①步：非有限。
    bool allFinite = true;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!finite(m(i, j))) { allFinite = false; }
        }
    }
    if (!allFinite) {
        addError(report, RuntimeErrorCode::InputInvalid, base,
                 base + "：惯量张量含非有限分量（NaN/±Inf）——NFR-COR-03 拒绝");
        return;
    }

    // 第②步：对称性（最大不对称分量）。惯量张量物理上必对称；不对称
    // 说明输入侧填装错误（如把惯性积塞错位置）。
    double asym = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            asym = std::max(asym, std::fabs(m(i, j) - m(j, i)));
        }
    }
    if (asym > kSymmetryTolerance) {
        addComparisonError(report, RuntimeErrorCode::InputInvalid, base,
                           base + "：惯量张量非对称（|I−Iᵀ| 超容差）——§4.3.3 非法"
                           "实例；实测最大不对称见比较值",
                           asym, kSymmetryTolerance, "inertia-asymmetry-max");
        return; // 非对称时 SPD 判读无意义（Jacobi 要求对称输入）——先修对称
    }

    // 第③步：正定性——对称化（已对称，取值即用）后最小特征值 > 0。
    std::vector<double> a(9, 0.0);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            a[i * 3 + j] = m(i, j);
        }
    }
    const std::vector<double> eig = symmetricEigenvalues(a, 3);
    const double minEig = eig.front();
    if (minEig <= 0.0) {
        addComparisonError(report, RuntimeErrorCode::InputInvalid, base,
                           base + "：惯量张量非正定（SPD 失败）——§4.3.3 非法实例"
                           "（m≤0/非对称/非正定→InputInvalid）；实测最小特征值见比较值",
                           minEig, 0.0, "inertia-min-eigenvalue");
    }
}

/**
 * @brief 关节限位的量级抽样警告（§4.4——不阻断）。
 *
 * 判据：旋转关节 |q| > 4π×10 rad（deg 误作 rad 的典型量级）；移动关节
 * |q| > 1×10² m（mm 误作 m 的典型量级）。只对 Provided 且有限的值生效
 * （非法值已另行报错，不重复警告）。
 *
 * @param type      [in] 关节类型（决定量纲期望）
 * @param nameLower [in] 下限字段路径
 * @param nameUpper [in] 上限字段路径
 */
void checkBoundMagnitude(ValidationReport& report, JointType type,
                         const core::SourcedValue<double>& lower,
                         const core::SourcedValue<double>& upper,
                         const std::string& nameLower, const std::string& nameUpper)
{
    // 量纲期望：旋转类（Revolute/Continuous）阈值取 rad 阈；移动类取 m 阈。
    // Continuous 有限区间同理抽样（工程工作范围同样是 rad 语义）。
    const bool isLength = (type == JointType::Prismatic);
    const double warn = isLength ? kLengthMagnitudeWarn : kAngleMagnitudeWarn;
    const char* unit = isLength ? "m" : "rad";

    // 逐端独立检查（Provided＋有限才判量级——NotProvided/非法值不进本检查）。
    auto checkOne = [&](const core::SourcedValue<double>& value, const std::string& path) {
        const auto v = value.tryValue();
        if (!v || !finite(*v)) { return; }
        if (std::fabs(*v) > warn) {
            addWarning(report, RuntimeErrorCode::UnitMismatch, path,
                       path + "：限位量级异常（|" + std::to_string(*v) + "| > "
                           + std::to_string(warn) + " " + unit + "）——疑似 deg/mm"
                           " 误作 SI 单位；§4.4 量级抽样警告，不阻断，请复核");
        }
    };
    checkOne(lower, nameLower);
    checkOne(upper, nameUpper);
}

/// Provided 值的有限性与非负性检查（限速/传动比/摩擦共用骨架）。
/// @param negativeAllowed 恒 false——本函数只服务"须有限且 ≥0/＞0"语义；
///        严格为正（＞0）与非负（≥0）由 lowerBound 参数区分。
void checkProvidedNonNegative(ValidationReport& report,
                              const core::SourcedValue<double>& value,
                              const std::string& path, bool strictlyPositive,
                              const char* what)
{
    const auto v = value.tryValue();
    if (!v) { return; } // NotProvided/NotApplicable/Invalid——S3 不判（缺失＝
                        // 能力缺失 §5.6；Invalid 态由 reader/上层处置）
    if (!finite(*v)) {
        addError(report, RuntimeErrorCode::InputInvalid, path,
                 path + "：" + what + "含非有限值——NFR-COR-03 拒绝（不静默转 0）");
        return;
    }
    if (strictlyPositive ? (*v <= 0.0) : (*v < 0.0)) {
        addError(report, RuntimeErrorCode::InputInvalid, path,
                 path + "：" + what + (strictlyPositive ? "须为正有限值，实测 "
                                                        : "须非负，实测 ")
                     + std::to_string(*v) + "（§4.3.3/§4.3.4 合法实例口径）");
    }
}

}  // namespace

// =====================================================================
// 主入口——检查次序固定（确定性），全量收集不短路（§5.2"诊断全量"）。
// =====================================================================

ValidationReport validateRobotDesignDescription(const RobotDesignDescription& description)
{
    ValidationReport report;

    // ---- 块 0：顶层值域（descriptionContractVersion/robotLocalName）----
    // 契约版本合法域 ≥1（§4.3.1；0＝未显式置值——reader 契约违约的编译侧
    // 复核，版本进缓存键 §9.4，非法版本不得放行）。
    if (description.descriptionContractVersion == 0) {
        addError(report, RuntimeErrorCode::InputInvalid, "descriptionContractVersion",
                 "descriptionContractVersion：契约版本须 ≥1（§4.3.1），实测 0"
                 "——版本进入编译缓存键，非法版本不得编译");
    }
    // 机器人局部名：空/含 '/' 非法（§4.3.3——名进入 RuntimeNameMap 设备
    // 作用域，'/' 是名称层级分隔符，混入即破坏名称解析唯一性）。
    if (description.robotLocalName.empty() || description.robotLocalName.find('/') != std::string::npos) {
        addError(report, RuntimeErrorCode::InputInvalid, "robotLocalName",
                 "robotLocalName：机器人局部名不得为空或含 '/'（§4.3.3），实测 \""
                     + description.robotLocalName + "\"");
    }

    // ---- 块 1：链结构（StructureInvalid——§5.2 S3 第一类失败）----
    // 关节链 ≥1（§4.3.3 joints 是（≥1）；编译器机械支持 1..N，产品 6/7 轴
    // 口径归 modeling 门控）。
    const std::size_t nJoints = description.joints.size();
    if (nJoints == 0) {
        addError(report, RuntimeErrorCode::StructureInvalid, "joints",
                 "joints：关节链为空——合法链至少 1 个关节（§4.3.3）");
    }
    // 连杆数量失配（§4.3.3 links 引用约束：links.size()==joints.size()+1，
    // 含基座连杆）。
    if (description.links.size() != nJoints + 1) {
        addError(report, RuntimeErrorCode::StructureInvalid, "links",
                 "links：连杆数量失配（实测 " + std::to_string(description.links.size())
                     + "，须＝joints.size()+1＝" + std::to_string(nJoints + 1)
                     + "，含基座连杆——§4.3.3）");
    }
    // 逐关节向量口径：摩擦清单要么空（全缺省），要么恰＝关节数（§4.2）。
    if (!description.friction.empty() && description.friction.size() != nJoints) {
        addError(report, RuntimeErrorCode::StructureInvalid, "friction",
                 "friction：摩擦清单长度失配（实测 " + std::to_string(description.friction.size())
                     + "，非空时须＝joints.size()＝" + std::to_string(nJoints)
                     + "，逐关节口径——§4.2）");
    }

    // ---- 块 2：逐关节（值非法——InputInvalid；次序固定）----
    for (std::size_t i = 0; i < nJoints; ++i) {
        const JointDescription& joint = description.joints[i];
        const std::string idx = "joints[" + std::to_string(i) + "]";

        // 局部名非空（§4.3.3——进入名称映射与诊断定位；重复名不在此拒绝，
        // 消歧归 S8/RT-T05——RT-NM-2 口径）。
        if (joint.localName.empty()) {
            addError(report, RuntimeErrorCode::InputInvalid, idx + ".localName",
                     idx + ".localName：关节局部名为空（§4.3.3 非法实例）");
        }

        // 轴向：非有限→拒绝；零向量→拒绝（§4.3.3 axis 非法：零向量/非有限；
        // 合法轴不要求单位长——编译器规格化）。先判有限（NaN 无方向可言）。
        if (!finite(joint.axis)) {
            addError(report, RuntimeErrorCode::InputInvalid, idx + ".axis",
                     idx + ".axis：轴向含非有限分量——NFR-COR-03 拒绝");
        } else if (joint.axis[0] == 0.0 && joint.axis[1] == 0.0 && joint.axis[2] == 0.0) {
            addError(report, RuntimeErrorCode::InputInvalid, idx + ".axis",
                     idx + ".axis：轴向为零向量（§4.3.3 非法；MDL-09 权威轴必须有方向）");
        }

        // T_parent_joint 变换合法性（非有限/正交/反射——RT-BW-6 字段）。
        checkTransform(report, joint.origin, idx + ".origin");

        // 限位按类型分派（§4.3.3 bounds 行：Revolute/Prismatic 必填且
        // qmin<qmax；Continuous 必无（工作范围替代）；Fixed 未约束——
        // 若提供则按有限＋有序复核）。
        const bool boundedType = (joint.type == JointType::Revolute)
                              || (joint.type == JointType::Prismatic);
        if (boundedType) {
            // 必填端检查（缺失即 InputInvalid——限位是硬边界不是能力缺失）。
            if (joint.lower.state() != core::FieldState::Provided) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".lower",
                         idx + ".lower：旋转/移动关节限位下界必填（§4.3.3；"
                         "缺失走 InputInvalid 而非能力降级——硬边界语义）");
            }
            if (joint.upper.state() != core::FieldState::Provided) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".upper",
                         idx + ".upper：旋转/移动关节限位上界必填（§4.3.3）");
            }
        }
        if (joint.type == JointType::Continuous) {
            // Continuous 的限位必为 NotProvided（§4.2 原文——"Continuous＝
            // NotProvided（工作范围另载）"）；提供即自相矛盾输入。
            if (joint.lower.state() == core::FieldState::Provided) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".lower",
                         idx + ".lower：Continuous 关节不得携带限位（§4.2——工作"
                         "范围另载于 workingRange）");
            }
            if (joint.upper.state() == core::FieldState::Provided) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".upper",
                         idx + ".upper：Continuous 关节不得携带限位（§4.2）");
            }
        }

        // 两端 Provided 时的值合法性（无论类型——Fixed 提供限位同样复核）。
        if (joint.lower.state() == core::FieldState::Provided
            && joint.upper.state() == core::FieldState::Provided) {
            const auto lo = joint.lower.tryValue();
            const auto hi = joint.upper.tryValue();
            // 逐端非有限（NFR-COR-03——单位校验切面的硬拒绝面）。
            if (lo && !finite(*lo)) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".lower",
                         idx + ".lower：限位含非有限值（NaN/±Inf）——NFR-COR-03 拒绝");
            }
            if (hi && !finite(*hi)) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".upper",
                         idx + ".upper：限位含非有限值（NaN/±Inf）——NFR-COR-03 拒绝");
            }
            // qmin ≥ qmax → InputInvalid（§4.3.3 bounds 行明文；硬断言 qmin<qmax
            // 的建模侧归属不变——此处是编译器一致性复核）。
            if (lo && hi && finite(*lo) && finite(*hi) && *lo >= *hi) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".upper",
                         idx + ".upper：限位区间非法（qmin ≥ qmax：下界="
                             + std::to_string(*lo) + "，上界=" + std::to_string(*hi)
                             + "，单位 rad/m 视类型）——§4.3.3");
                // 路径取 upper：上界是"区间成立"的最后承诺端。
            }
        }

        // 工作范围（§4.3.3：仅 Continuous；有限且 qmin<qmax）。
        if (joint.workingRange) {
            if (joint.type != JointType::Continuous) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".workingRange",
                         idx + ".workingRange：仅 Continuous 关节可有工作范围"
                         "（§4.3.3），实测类型非 Continuous");
            } else {
                // 有限性＋有序性（MDL-12：确认值必须为有限区间）。
                const WorkingRange& wr = *joint.workingRange;
                if (!finite(wr.lower) || !finite(wr.upper)) {
                    addError(report, RuntimeErrorCode::InputInvalid, idx + ".workingRange",
                             idx + ".workingRange：工作范围须有限区间（MDL-12）");
                } else if (wr.lower >= wr.upper) {
                    addError(report, RuntimeErrorCode::InputInvalid, idx + ".workingRange",
                             idx + ".workingRange：工作范围区间非法（qmin ≥ qmax："
                                 + std::to_string(wr.lower) + " ≥ "
                                 + std::to_string(wr.upper) + "，单位 rad）");
                }
            }
        }

        // 限速/限加速度：Provided 时有限且非负（§4.3.3"provided(负数)→
        // InputInvalid"；缺失＝能力缺失降级，此处不判）。
        checkProvidedNonNegative(report, joint.maxVelocity, idx + ".maxVelocity", false,
                                 "最大速度");
        checkProvidedNonNegative(report, joint.maxAcceleration, idx + ".maxAcceleration",
                                 false, "最大加速度");

        // 量级抽样警告（§4.4——不阻断；仅对已 Provided 且有限的限位生效）。
        checkBoundMagnitude(report, joint.type, joint.lower, joint.upper,
                            idx + ".lower", idx + ".upper");
    }

    // ---- 块 3：逐连杆（含基座连杆；RT-CPX-3 的落点）----
    for (std::size_t i = 0; i < description.links.size(); ++i) {
        const LinkDescription& link = description.links[i];
        const std::string idx = "links[" + std::to_string(i) + "]";

        if (link.localName.empty()) {
            addError(report, RuntimeErrorCode::InputInvalid, idx + ".localName",
                     idx + ".localName：连杆局部名为空（§4.3.3 同关节口径）");
        }

        // 质量：Provided 时有限且 >0（RT-CPX-3：mass=−1→Failed＋InputInvalid
        // 定位连杆；NotProvided＝能力缺失降级，严格区分不在此报错——§5.6）。
        const auto mass = link.mass.tryValue();
        if (mass) {
            if (!finite(*mass)) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".mass",
                         idx + ".mass：质量含非有限值——NFR-COR-03 拒绝（单位 kg）");
            } else if (*mass <= 0.0) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".mass",
                         idx + ".mass：质量须＞0（单位 kg），实测 "
                             + std::to_string(*mass) + "——RT-CPX-3 反例口径"
                             "（m≤0→InputInvalid，与能力缺失严格区分）");
            }
        }

        // 质心：Provided 时全分量有限（单位 m，连杆系——M-2 惯量基准）。
        if (const auto com = link.centerOfMass.tryValue()) {
            if (!finite(*com)) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".centerOfMass",
                         idx + ".centerOfMass：质心含非有限分量（连杆系，单位 m）——"
                         "NFR-COR-03 拒绝");
            }
        }

        // 惯量：Provided 时对称＋正定（§4.3.3——编译器复核已提供值）。
        if (const auto inertia = link.inertia.tryValue()) {
            checkInertia(report, *inertia, idx + ".inertia");
        }
    }

    // ---- 块 4：逐工具（首项为默认 TCP——KIN-14 权威来源，合法性必检）----
    for (std::size_t i = 0; i < description.tools.size(); ++i) {
        const ToolDescription& tool = description.tools[i];
        const std::string idx = "tools[" + std::to_string(i) + "]";

        if (tool.localName.empty()) {
            addError(report, RuntimeErrorCode::InputInvalid, idx + ".localName",
                     idx + ".localName：工具局部名为空");
        }

        // 工具变换（法兰→TCP）——同关节 origin 口径（反射/非正交/非有限）。
        checkTransform(report, tool.tcpOffset, idx + ".tcpOffset");

        // 物性三件套（与连杆同口径——m≤0/非有限/非对称/非正定）。
        const auto mass = tool.mass.tryValue();
        if (mass) {
            if (!finite(*mass)) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".mass",
                         idx + ".mass：质量含非有限值（单位 kg）——NFR-COR-03 拒绝");
            } else if (*mass <= 0.0) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".mass",
                         idx + ".mass：质量须＞0（单位 kg），实测 "
                             + std::to_string(*mass));
            }
        }
        if (const auto com = tool.centerOfMass.tryValue()) {
            if (!finite(*com)) {
                addError(report, RuntimeErrorCode::InputInvalid, idx + ".centerOfMass",
                         idx + ".centerOfMass：质心含非有限分量——NFR-COR-03 拒绝");
            }
        }
        if (const auto inertia = tool.inertia.tryValue()) {
            checkInertia(report, *inertia, idx + ".inertia");
        }
    }

    // ---- 块 5：逐场景对象（世界系固连位姿——RT-BW-6"场景位姿非有限"）----
    for (std::size_t i = 0; i < description.scene.size(); ++i) {
        const SceneObjectDescription& obj = description.scene[i];
        const std::string idx = "scene[" + std::to_string(i) + "]";

        if (obj.localName.empty()) {
            addError(report, RuntimeErrorCode::InputInvalid, idx + ".localName",
                     idx + ".localName：场景对象局部名为空");
        }
        // 世界系位姿合法性（"不得预乘安装旋转"的检测在 S6/S9——此处只判
        // 数值/正交/反射；§6.4 禁止项的归属见函数头不覆盖面说明）。
        checkTransform(report, obj.worldPose, idx + ".worldPose");
    }

    // ---- 块 6：基座布置（MDL-22 编辑表示）----
    // 基座原点位置：全分量有限（单位 m，世界系）。
    if (!finite(description.base.basePosition)) {
        addError(report, RuntimeErrorCode::InputInvalid, "base.basePosition",
                 "base.basePosition：基座位置含非有限分量（世界系，单位 m）——"
                 "NFR-COR-03 拒绝（§6.6 单位错误行的数值切面）");
    }
    // Custom 预设的旋转矢量必填（§4.2"Custom 时必填"）；Provided 时须有限。
    if (description.base.preset == InstallationPresetToken::Custom
        && description.base.customEaa.state() != core::FieldState::Provided) {
        addError(report, RuntimeErrorCode::InputInvalid, "base.customEaa",
                 "base.customEaa：Custom 预设必须提供旋转矢量 EAA（§4.2/§6.2）");
    } else if (const auto eaa = description.base.customEaa.tryValue()) {
        if (!finite(*eaa)) {
            addError(report, RuntimeErrorCode::InputInvalid, "base.customEaa",
                     "base.customEaa：旋转矢量含非有限分量（单位 rad）——NFR-COR-03 拒绝");
        }
    }

    // ---- 块 7：传动（逐关节传动比＋耦合矩阵）----
    // 逐关节口径：非空时长度必须＝关节数（§4.3.4"逐关节"）。
    if (!description.drivetrain.ratioPerJoint.empty()
        && description.drivetrain.ratioPerJoint.size() != nJoints) {
        addError(report, RuntimeErrorCode::StructureInvalid, "drivetrain.ratioPerJoint",
                 "drivetrain.ratioPerJoint：长度失配（实测 "
                     + std::to_string(description.drivetrain.ratioPerJoint.size())
                     + "，非空时须＝joints.size()＝" + std::to_string(nJoints) + "）");
    }
    for (std::size_t i = 0; i < description.drivetrain.ratioPerJoint.size(); ++i) {
        // 合法实例＝正有限值（§4.3.4"合法：正有限值"——传动比无量纲）。
        checkProvidedNonNegative(report, description.drivetrain.ratioPerJoint[i],
                                 "drivetrain.ratioPerJoint[" + std::to_string(i) + "]",
                                 true, "传动比");
    }

    // 耦合矩阵（可空；存在时走完整校验链——RT-CPL-1 落点）。
    if (description.drivetrain.coupling) {
        const CouplingMatrix& cm = *description.drivetrain.coupling;
        const std::string base = "drivetrain.coupling.C";

        // 适用范围结构检查（StructureInvalid——范围是引用结构不是数值）。
        if (cm.jointRange.count == 0) {
            addError(report, RuntimeErrorCode::StructureInvalid,
                     "drivetrain.coupling.jointRange",
                     "drivetrain.coupling.jointRange：适用关节数为 0——耦合矩阵"
                     "至少适用 1 个关节（MDL-21）");
        }
        // 越界检查（uint64 防回绕——firstIndex+count 都是 uint32，直接相加
        // 理论可回绕；提升后再加是防溢出的既定做法）。
        const std::uint64_t end = static_cast<std::uint64_t>(cm.jointRange.firstIndex)
                                + static_cast<std::uint64_t>(cm.jointRange.count);
        if (end > nJoints) {
            addError(report, RuntimeErrorCode::StructureInvalid,
                     "drivetrain.coupling.jointRange",
                     "drivetrain.coupling.jointRange：适用范围越出关节链（["
                         + std::to_string(cm.jointRange.firstIndex) + ", "
                         + std::to_string(end) + ") 越出 joints.size()="
                         + std::to_string(nJoints) + "）");
        }

        // 数据量吻合（扁平数组长度＝rows*cols；先于方阵判定——数据残缺时
        // 任何矩阵解读都无意义）。uint64 防乘法回绕。
        const std::uint64_t expect = static_cast<std::uint64_t>(cm.rows)
                                   * static_cast<std::uint64_t>(cm.cols);
        if (cm.c.size() != expect) {
            addError(report, RuntimeErrorCode::InputInvalid, base,
                     base + "：数据量失配（c.size()=" + std::to_string(cm.c.size())
                         + "，须＝rows×cols=" + std::to_string(expect) + "，行主序）");
        } else if (cm.rows != cm.cols) {
            // 非方阵（§4.3.4 非法实例第三项——线性耦合映射要求方阵）。
            addError(report, RuntimeErrorCode::InputInvalid, base,
                     base + "：耦合矩阵须为方阵（rows=" + std::to_string(cm.rows)
                         + "，cols=" + std::to_string(cm.cols) + "）——§4.3.4");
        } else if (cm.rows != cm.jointRange.count) {
            // 方阵维度＝适用关节数（§4.3.4"编译校验：方阵维度＝适用关节数"）。
            addError(report, RuntimeErrorCode::InputInvalid, base,
                     base + "：方阵维度须＝适用关节数（rows=" + std::to_string(cm.rows)
                         + "，jointRange.count=" + std::to_string(cm.jointRange.count)
                         + "）——§4.3.4");
        } else {
            // 形状合法→逐元素有限性→奇异性/条件数（比较型诊断——RT-CPL-1）。
            bool allFinite = true;
            for (const double v : cm.c) {
                if (!finite(v)) { allFinite = false; break; }
            }
            if (!allFinite) {
                addError(report, RuntimeErrorCode::InputInvalid, base,
                         base + "：耦合矩阵含非有限分量——NFR-COR-03 拒绝");
            } else if (cm.rows > 0) {
                // 行阶 ≥1 才有矩阵意义（count==0 已在结构块报过，此处防御）。
                checkCouplingRankAndCondition(report, static_cast<std::size_t>(cm.rows),
                                              cm.c, base);
            }
        }
        // 说明：cm.conditionNumber 申报值不参与判定（防申报失真绕过病态
        // 阻止——校验器一律重算，见 Description.hpp CouplingMatrix 注释）。
    }

    // ---- 块 8：逐关节摩擦（MDL-16——runtime 只承载；合法性复核 Provided 值）----
    for (std::size_t i = 0; i < description.friction.size(); ++i) {
        const JointFrictionDescription& f = description.friction[i];
        const std::string idx = "friction[" + std::to_string(i) + "]";
        // 合法实例＝NotProvided 或有限正值（§4.3.3 friction 行）。
        checkProvidedNonNegative(report, f.viscous, idx + ".viscous", true, "粘性摩擦系数");
        checkProvidedNonNegative(report, f.coulomb, idx + ".coulomb", true, "库仑摩擦");
        checkProvidedNonNegative(report, f.bias, idx + ".bias", true, "摩擦偏置");
    }

    // 资源清单（description.resourceRefs）：内容与摘要一致性归 S4（资源
    // 读取与完整性校验，RT-T11＋io 注入）——S3 无 provider 可查，不判。

    return report;
}

bool ValidationReport::ok() const noexcept
{
    // 通过＝无阻断级问题（警告不阻断——§4.4 量级抽样"给警告——不阻断"）。
    for (const ValidationIssue& issue : issues) {
        if (issue.severity == ValidationSeverity::Error) { return false; }
    }
    return true;
}

}  // namespace sdurws::ird::runtime
