/**
 * @file   MappingCore.cpp
 * @brief  传动映射核心唯一实现的落位（units/drivetrain.md §6/§7/§8/§9/
 *         §10/§13）——阻断面检查序、R1 对角精确虚功映射、R2 耦合矩阵块
 *         对角精确映射（τ_motor＝Cᵀ·τ_joint＋J_rotor·θ̈_motor、
 *         θ̈_motor＝C⁺·q̈_joint——§7.1 公式组冻结口径，交叉耦合逐元素
 *         保留）、虚功/功率一致性逐元素精确对照、反射惯量与惯量比（R1
 *         对角式＋R2 完整矩阵/窗口投影）、效率方向折算与降级、电机工作
 *         点统计（峰值/RMS/负载率/四象限/能量分项）、R2 数值核（单侧
 *         Jacobi SVD 奇异值/Gauss-Jordan 求逆）与 R2 耦合模型工厂、
 *         矩阵内容身份核对原语。
 *
 * 设计依据：
 *   - units/drivetrain.md §6.2（R1 映射定义表——逐样本逐轴全列公式）、
 *     §6.3（阻断面表——顺序执行、首个命中即阻止；阻断不伪装"传动不可
 *     行"）、§7.1～§7.4（R2 公式组/必须阻止的矩阵形态/良态阈值 P-RT-7
 *     对齐/C⁺ 使用前提——方阵良态下 C⁺≡C⁻¹，伪逆不得放行非法矩阵）、
 *     §8.1～§8.4（虚功/功率一致性四口径——运行侧精确判据无私设阈值）、
 *     §9.2/§9.3/§9.5（反射惯量 R1 对角式/R2 完整矩阵＋对角视图/窗口投
 *     影标记）、§10.2～§10.7（效率方向折算/峰值/RMS/能量分项/四象限/
 *     数据不足降级）、§12.1（序列处理规则）、§13.0～§13.5（接口契约与
 *     错误语义）
 *   - 需求 DYN-04（M-12 精确虚功映射——不采用对角化或准静态近似、不丢
 *     弃交叉耦合项）、MDL-21（R2 耦合矩阵消费侧）、AT-38（高速多轴联动
 *     交叉耦合项保留＋R1 阻断反例）、NFR-COR-01/02/03、SEL-09、ERR-01
 *     （不适用显式标记）、P-DT-2（惯量比阈值不内嵌；条件数阈值与编译
 *     侧同源留痕）、P-DT-5（R1 负传动比不经本路径放开）
 *   - 任务契约 tasks/foundation/WP-18-T05.json（acceptance 1/2/3）
 *
 * 确定性（NFR-COR-02）：全部计算为封闭代数运算（无随机、无隐藏状态、
 *   无时钟/随机源）；R2 数值核为**确定性迭代**——单侧 Jacobi SVD 固定
 *   列对扫描序（p 升序×q 降序嵌套）＋相对收敛阈＋固定轮数上限，
 *   Gauss-Jordan 求逆取**首个**最大主元（并列不摇摆）；同一输入必得位
 *   等输出。映射本身不存在算法截断误差（卡 §6.2"映射误差"行）——黄金
 *   算例对照容差仅为浮点求值路径差异留（附录 D 第 9 项标量相对 1×10⁻⁹）。
 *
 * 线程安全：无静态可变状态——全部可重入纯函数（卡 §13.9）。
 */

#include <sdurws/ird/drivetrain/MappingCore.hpp>

#include <sdurws/ird/drivetrain/DiagCodes.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::drivetrain {
namespace {

// =====================================================================
// 内部工具：有限性判断与诊断记录构造
// =====================================================================

/// 非有限守卫（NFR-COR-03"非有限即拒绝"的精确判据——NaN/±Inf 任一命中）。
bool isFinite(double v) noexcept
{
    return std::isfinite(v);
}

/**
 * @brief 构造一条 DT-* 诊断记录（ERR-01 字段面——稳定码＋主体＋局部名＋
 *        上下文＋原因＋建议动作）。
 *
 * @param code     [in] 稳定码（DiagCodes.hpp 常量——唯一书写点引用）
 * @param subject  [in] 主体对象（关节/电机轴 ID；可空＝集合级问题）
 * @param localName [in] 局部名（诊断呈现；可空）
 * @param context  [in] 上下文描述（非空——DiagnosticRecord::make C-3）
 * @param cause    [in] 原因（非空）
 * @param action   [in] 建议动作（非空）
 * @param comparison [in] 比较型三要素（非比较型为空）
 */
core::DiagnosticRecord makeDtRecord(std::string_view code,
                                    std::optional<core::ObjectId> subject,
                                    std::optional<std::string> localName,
                                    std::string context,
                                    std::string cause,
                                    std::string action,
                                    std::optional<core::ComparativeFields> comparison = {})
{
    return core::DiagnosticRecord::make(std::string(code), std::move(subject),
                                        std::move(localName), std::nullopt,
                                        std::move(context), std::move(cause),
                                        std::move(action), std::move(comparison));
}

/**
 * @brief 比较型三要素快捷构造（实际值/期望值＋单位符号——ERR-01/AT-27：
 *        单位进入诊断比较字段）。
 *
 * 来源标记取 DerivedReadOnly（映射派生值——core.md §4.3 五类之一）；
 * unitSymbol 必须是 core 注册表内已注册符号（"N*m"/"W"/"rad/s"/"1" 等
 * ——src/Units.cpp kUnitTable；查不到时兜底无量纲 "1"，不中断诊断装配）。
 */
core::ComparativeFields compareOf(double actual, double expected, const char* unitSymbol)
{
    const auto unit = core::UnitToken::find(unitSymbol);
    const auto fallbackUnit = core::UnitToken::find("1");
    core::ComparativeValue actualValue;
    actualValue.quantity = core::SourcedValue<double>::provided(
        actual, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly));
    actualValue.unit = unit ? *unit : *fallbackUnit;
    core::ComparativeValue expectedValue;
    expectedValue.quantity = core::SourcedValue<double>::provided(
        expected, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly));
    expectedValue.unit = unit ? *unit : *fallbackUnit;
    core::ComparativeFields fields;
    fields.actual = actualValue;
    fields.expected = expectedValue;
    return fields;
}

/// fail-fast 快捷抛出（消息以 DT-* 码开头——卡 §13.8 示例形态）。
[[noreturn]] void throwContract(std::string_view code, const std::string& detail)
{
    throw std::invalid_argument(std::string(code) + "：" + detail);
}

/// 单条样本数值有限性核对（NFR-COR-03——非有限样本属调用方契约违约）。
void requireFiniteSample(const JointDriveSample& s, std::size_t i, std::size_t jIdx)
{
    if (!isFinite(s.t) || !isFinite(s.q) || !isFinite(s.qd) || !isFinite(s.qdd)
        || !isFinite(s.tauJoint)) {
        throwContract(kDtMatrixNonfinite,
                      "上游序列样本非有限（sample=" + std::to_string(i)
                          + "，jointIndex=" + std::to_string(jIdx)
                          + "）——NFR-COR-03 非有限即拒绝，不得静默转 0");
    }
}

/// 对角矩阵解析条件数（无量纲）＝max|c|/min|c|——谱条件数在对角阵上的
/// 精确形态（奇异值＝|对角元|）；min==0 由 DT-RATIO-ZERO 先行排除。
double diagonalConditionNumber(const std::vector<TransmissionRatio>& ratios)
{
    double maxAbs = 0.0;
    double minAbs = std::numeric_limits<double>::infinity();
    for (const TransmissionRatio& r : ratios) {
        const double a = std::fabs(r.c);
        maxAbs = std::max(maxAbs, a);
        minAbs = std::min(minAbs, a);
    }
    return maxAbs / minAbs;
}

// =====================================================================
// R2 数值核（WP-18-T05——确定性优先的通用方阵算法；零第三方库，纯 std）
// =====================================================================

/// 耦合矩阵"数值奇异"判定分界：σmin ≤ σmax×1×10⁻¹²（⇔ κ ≥ 1×10¹²）视为
/// det≈0（双精度有效秩为零）。★来源留痕（P-DT-2 同族设计默认纪律）：
/// 与 runtime 编译校验器（runtime/src/DescriptionValidator.cpp
/// kSingularSigmaRatio＝1e-12）及 modeling 重算复核（src/CouplingMath.hpp
/// kCouplingSingularSigmaRatio＝1e-12）同族同值——drivetrain 依赖白名单
/// 不含 runtime/modeling（卡 §3.2 表外边），故在本单元内以同一设计默认
/// 单点承载（本常量即唯一书写点），裁决变更时三处同步。
inline constexpr double kDtSingularSigmaRatio = 1e-12;

/// 单侧 Jacobi（Hestenes）SVD 相对收敛阈：|γ| ≤ 1e-16·√(α·β)（γ＝列对
/// 内积、α/β＝列范数平方）视为已正交。机器精度层（再转只引入噪音）——
/// 与 modeling/runtime 同族设计默认（确定性优先：同输入同轮数同结果）。
inline constexpr double kDtJacobiOrthogonalRelative = 1e-16;

/// 单侧 Jacobi 扫描轮数上限（固定轮数保证确定性终止——即使病态矩阵迭代
/// 缓慢也在有限步内给出判档精度奇异值；与 modeling 同族设计默认 60）。
inline constexpr std::size_t kDtJacobiMaxSweeps = 60;

/**
 * @brief 计算方阵的奇异值降序序列（单侧 Jacobi/Hestenes SVD——确定性）。
 *
 * 算法（逐步）：
 *   第 1 步：工作副本 W←A（不修改输入——纯函数纪律）。
 *   第 2 步：逐轮固定扫描序（p∈[0,n) 升序外层、q∈(p,n) 升序内层——
 *           不依赖运行期状态，保证同输入同旋转序列）对列对 (p,q) 做
 *           Jacobi 旋转：α＝‖W:,p‖²、β＝‖W:q‖²、γ＝W:,:p·W:,:q；
 *           |γ| ≤ 阈值（相对正交判据，α·β 任一为 0 时跳过——零列无需
 *           旋转）则已正交跳过；否则 ζ＝(β−α)/(2γ)、
 *           t＝sign(ζ)/(|ζ|+√(1+ζ²))（数值稳定正切）、c＝1/√(1+t²)、
 *           s＝c·t，对两列做旋转组合。
 *   第 3 步：任一轮内无旋转执行（全部列对已正交）→ 提前收敛终止；
 *           或达到 kDtJacobiMaxSweeps 轮硬终止（病态矩阵此时已收敛到
 *           判档精度——奇异/病态分界在 1e±12 量级，60 轮远超需要）。
 *   第 4 步：奇异值＝正交化后各列的欧氏范数（Hestenes 定理——W＝UΣVᵀ，
 *           列正交化后 WᵀW 对角，对角元＝σᵢ²）；按降序选择排序（并列
 *           不交换——稳定确定序）。
 *
 * @param a [in] 输入方阵（rows==cols 且 wellFormed——调用方保证）
 * @return 奇异值降序序列（n 个；σᵢ ≥ 0，无量纲——C 元素无量纲）
 *
 * 线程安全：可重入纯函数（无共享状态）。
 */
std::vector<double> singularValuesJacobi(const RowMatrix& a)
{
    const std::size_t n = a.rows;
    std::vector<double> w = a.data; // 工作副本（列将逐步正交化）
    auto entry = [n, &w](std::size_t r, std::size_t c) -> double& {
        return w[r * n + c];
    };

    for (std::size_t sweep = 0; sweep < kDtJacobiMaxSweeps; ++sweep) {
        bool rotated = false; // 本轮是否执行过旋转（提前收敛判据）
        for (std::size_t p = 0; p + 1 < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                // 列对 (p,q) 的内积与范数平方（按行序累加——固定求和序）。
                double alpha = 0.0;
                double beta = 0.0;
                double gamma = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    const double wp = entry(i, p);
                    const double wq = entry(i, q);
                    alpha += wp * wp;
                    beta += wq * wq;
                    gamma += wp * wq;
                }
                // 已正交（含零列）跳过：相对判据 |γ| ≤ 1e-16·√(α·β)——
                // 零列（α·β＝0）不参与旋转（旋转无意义且除零）。
                if (alpha == 0.0 || beta == 0.0
                    || std::fabs(gamma) <= kDtJacobiOrthogonalRelative
                                             * std::sqrt(alpha * beta)) {
                    continue;
                }
                // 数值稳定 Jacobi 旋转角（Golub & Van Loan 标准形态）。
                const double zeta = (beta - alpha) / (2.0 * gamma);
                const double t = (zeta >= 0.0 ? 1.0 : -1.0)
                                     / (std::fabs(zeta) + std::sqrt(1.0 + zeta * zeta));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double s = c * t;
                // 对两列做旋转（逐行——固定行序）。
                for (std::size_t i = 0; i < n; ++i) {
                    const double wp = entry(i, p);
                    const double wq = entry(i, q);
                    entry(i, p) = c * wp - s * wq;
                    entry(i, q) = s * wp + c * wq;
                }
                rotated = true;
            }
        }
        if (!rotated) {
            break; // 全部列对已正交——提前收敛（确定性终止点）
        }
    }

    // 奇异值＝各列欧氏范数；降序选择排序（并列不交换——稳定确定序，
    // NFR-COR-02：同输入必得同序列）。
    std::vector<double> sigma(n, 0.0);
    for (std::size_t c = 0; c < n; ++c) {
        double norm = 0.0;
        for (std::size_t r = 0; r < n; ++r) {
            norm += w[r * n + c] * w[r * n + c];
        }
        sigma[c] = std::sqrt(norm);
    }
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t maxIdx = i;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (sigma[j] > sigma[maxIdx]) {
                maxIdx = j; // 严格大于——并列保持原序（稳定）
            }
        }
        if (maxIdx != i) {
            std::swap(sigma[i], sigma[maxIdx]);
        }
    }
    return sigma;
}

/**
 * @brief 求方阵逆（Gauss-Jordan 消元＋部分主元——确定性）。
 *
 * 主元选择：每列在未消元行中取**绝对值最大者，并列取最小行号**（首个
 * 最大——不摇摆，NFR-COR-02）。调用前提：矩阵已过 §7.2 全表检查（方阵/
 * 有限/非奇异/良态）——奇异/病态矩阵由映射入口校验先行阻止（§7.3 伪逆
 * 使用边界：本函数只服务已批准公式的计算表示，不得把非法矩阵变成"可
 * 计算"），函数内零主元仍按防御抛 DT-MATRIX-SINGULAR。
 *
 * @param a [in] 输入方阵（rows==cols 且 wellFormed）
 * @return 逆矩阵（同维度）
 * @throws std::invalid_argument 遇数值零主元（防御面——正常流不可达）
 *
 * 线程安全：可重入纯函数。
 */
RowMatrix invertSquareMatrix(const RowMatrix& a)
{
    const std::size_t n = a.rows;
    // 增广矩阵 [A | I]（2n 列——行主序连续承载）。
    std::vector<double> aug(n * 2 * n, 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            aug[r * 2 * n + c] = a(r, c);
        }
        aug[r * 2 * n + n + r] = 1.0;
    }
    auto augAt = [n, &aug](std::size_t r, std::size_t c) -> double& {
        return aug[r * 2 * n + c];
    };

    for (std::size_t col = 0; col < n; ++col) {
        // 部分主元：未消元行中 |aug(r,col)| 最大者（并列取最小行号）。
        std::size_t pivotRow = col;
        double pivotMag = std::fabs(augAt(col, col));
        for (std::size_t r = col + 1; r < n; ++r) {
            const double mag = std::fabs(augAt(r, col));
            if (mag > pivotMag) {
                pivotMag = mag;
                pivotRow = r;
            }
        }
        if (pivotMag == 0.0) {
            // 防御面：调用前提已由 §7.2 校验排除奇异——到达此处＝实现
            // 缺陷或极端舍入，fail-fast 暴露而非产出垃圾逆（AGENTS §3）。
            throwContract(kDtMatrixSingular,
                          "求逆遇零主元（列 " + std::to_string(col)
                              + "）——矩阵未过 §7.2 校验或实现缺陷");
        }
        if (pivotRow != col) {
            for (std::size_t c = 0; c < 2 * n; ++c) {
                std::swap(augAt(col, c), augAt(pivotRow, c));
            }
        }
        // 归一化主元行（主元变 1）。
        const double pivot = augAt(col, col);
        for (std::size_t c = 0; c < 2 * n; ++c) {
            augAt(col, c) /= pivot;
        }
        // 消去其余行（Gauss-Jordan——消元后左半＝单位阵，右半＝逆）。
        for (std::size_t r = 0; r < n; ++r) {
            if (r == col) {
                continue;
            }
            const double factor = augAt(r, col);
            if (factor == 0.0) {
                continue;
            }
            for (std::size_t c = 0; c < 2 * n; ++c) {
                augAt(r, c) -= factor * augAt(col, c);
            }
        }
    }

    RowMatrix inv;
    inv.rows = n;
    inv.cols = n;
    inv.data.assign(n * n, 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            inv.data[r * n + c] = augAt(r, n + c);
        }
    }
    return inv;
}

/// 矩阵-向量积（行主序：result[r]＝Σ_c M(r,c)·v[c]——固定列升序求和）。
std::vector<double> matVec(const RowMatrix& m, const std::vector<double>& v)
{
    std::vector<double> out(m.rows, 0.0);
    for (std::size_t r = 0; r < m.rows; ++r) {
        double sum = 0.0;
        for (std::size_t c = 0; c < m.cols; ++c) {
            sum += m(r, c) * v[c];
        }
        out[r] = sum;
    }
    return out;
}

/// 理想虚功对偶的窗口形态（τ_ideal＝C_wᵀ·τ_joint：result[k]＝Σ_j
/// C_w(j,k)·τ_w[j]——固定行升序求和；映射与自检共用同一函数，保证
/// §8.4 ②自检"同表达式路径位等"前提）。
std::vector<double> windowIdealMotorTorque(const RowMatrix& cw,
                                           const std::vector<double>& tauJointW)
{
    const std::size_t w = cw.rows;
    std::vector<double> out(w, 0.0);
    for (std::size_t k = 0; k < w; ++k) {
        double sum = 0.0;
        for (std::size_t j = 0; j < w; ++j) {
            sum += cw(j, k) * tauJointW[j];
        }
        out[k] = sum;
    }
    return out;
}

/// 能量/积分的梯形求和（§10.5——非均匀采样按逐对区间积分，缺样本区间不
/// 外推；序列时间戳已保证升序——统计入口检查）。
double trapezoidIntegral(const std::vector<double>& t, const std::vector<double>& y)
{
    double sum = 0.0;
    for (std::size_t i = 1; i < t.size(); ++i) {
        sum += 0.5 * (y[i - 1] + y[i]) * (t[i] - t[i - 1]);
    }
    return sum;
}

}  // namespace

// =====================================================================
// 矩阵良态校验（§6.3 阻断面——检查序固定，首个命中即阻止）
// =====================================================================

CouplingValidationResult DriveTrainMappingCore::validate(const DriveTrainModel& model,
                                                         StageCapability stage) const
{
    CouplingValidationResult result;

    // ---- 卡 §6.3 表序①：能力门控（R1 能力下窗口存在即阻断——R1 阻断
    // 反例保留，AT-38；"不提前放开 R1 阻断"红线在 T05 落位后继续生效：
    // R1 能力永远不接受耦合窗口）。显示/配置中的 R2 标签不改变计算能力
    // （能力由装配清单与算法版本决定——调用方经 stage 注入真实能力位）。
    // R2 能力下窗口进入下方 §7.2 矩阵路径（WP-18-T05 落位的数值路径）。
    if (stage == StageCapability::R1Capability && model.window.has_value()) {
        result.diagnostics.push_back(makeDtRecord(
            kDtCouplingStageLocked, std::nullopt, std::nullopt,
            "drivetrain 映射能力门控",
            "模型携带 R2 耦合窗口而阶段能力为 R1（MDL-21 属 R2；UI/配置中"
            "的 R2 标签不改变计算能力）",
            "在 R1 能力下改用对角传动模型；或在 R2 能力装配（装配清单含"
            " R2 映射算法版本）下消费耦合矩阵映射"));
        result.conditionNumber = 0.0;
        return result; // 首个命中即阻止
    }

    // ---- 卡 §6.3 表序⑤前移的最低前置：空轴表（轴表空时后续检查不可判定
    // ——先阻断以免空引用；语义＝表序⑤"空输入"行的轴表半区）。R1/R2 公共。
    if (model.jointAxes.empty() || model.motorAxes.empty()) {
        result.diagnostics.push_back(makeDtRecord(
            kDtInputEmpty, std::nullopt, std::nullopt,
            "drivetrain 映射输入结构检查",
            "关节轴表或电机轴表为空（空模型没有评估意义）",
            "检查 robot-drivetrain 配置与组装方的轴表构造（不得以空表调用映射）"));
        return result;
    }

    // ---- 卡 §6.3 表序③：链型/关节类型范围外——Prismatic 轴不产出电机
    // 侧结果，不静默套用旋转传动（SEL-09/MDL-12；mimic/闭环在 modeling
    // 侧已被阻断，此处为映射侧第二道防线）。R1/R2 公共——直线传动映射
    // 属 §16.2 独立扩展（SEL-09-S1），不随 R2 耦合矩阵放开。
    for (std::size_t j = 0; j < model.jointAxes.size(); ++j) {
        if (model.jointAxes[j].kind == JointKind::Prismatic) {
            result.diagnostics.push_back(makeDtRecord(
                kDtAxisTypeOutOfScope, model.jointAxes[j].jointId,
                model.jointAxes[j].localName,
                "drivetrain 映射关节类型检查（关节下标 " + std::to_string(j) + "）",
                "移动关节（prismatic）在传动映射范围外（SEL-09——直线传动"
                "目录与工作点映射未启用；R2 耦合矩阵不改变本边界）",
                "将该轴移出目标链或在 R2 直线传动扩展（SEL-09-S1）落位后重评"));
            result.conditionNumber = 0.0;
            return result;
        }
    }

    // ---- 分支：窗口存在→R2 矩阵路径（stage 必为 R2——R1+窗口已被①拦
    // 截）；窗口不存在→对角路径（R1/R2 同构检查——R2 能力是对角链的超集，
    // 无耦合链经同一对角检查；未声明窗口的非对角结构同样阻断——交叉耦合
    // 必须经窗口声明，不静默拆轴）。
    if (model.window.has_value()) {
        return validateCoupledWindow(model);
    }
    return validateDiagonalModel(model);
}

// =====================================================================
// 对角路径校验（无窗口——既有 §6.3 检查序原样承载，行为冻结）
// =====================================================================

CouplingValidationResult DriveTrainMappingCore::validateDiagonalModel(
    const DriveTrainModel& model) const
{
    CouplingValidationResult result;
    const std::size_t nJoints = model.jointAxes.size();
    const std::size_t nMotors = model.motorAxes.size();

    // ---- 卡 §6.3 表序④＋§7.2：结构有效性——维度/轴序/非有限/零传动比。
    if (nJoints != nMotors || !model.chat.wellFormed() || model.chat.rows != nJoints
        || model.chat.cols != nMotors || model.ratios.size() != nMotors) {
        result.diagnostics.push_back(makeDtRecord(
            kDtInputDimensionMismatch, std::nullopt, std::nullopt,
            "drivetrain 映射维度检查",
            "维度不匹配（关节轴 " + std::to_string(nJoints) + "，电机轴 "
                + std::to_string(nMotors) + "，chat " + std::to_string(model.chat.rows)
                + "×" + std::to_string(model.chat.cols)
                + "，ratios " + std::to_string(model.ratios.size())
                + "）——对角路径要求对角方阵且逐轴视图齐备",
            "修正组装方的归一化模型构造（makeDiagonalDriveTrainModel 工厂已强制）"));
        result.conditionNumber = 0.0;
        return result;
    }
    for (std::size_t k = 0; k < nMotors; ++k) {
        if (model.motorAxes[k].jointIndex != k) {
            result.diagnostics.push_back(makeDtRecord(
                kDtInputAxisOrderMismatch, model.motorAxes[k].motorId, std::nullopt,
                "drivetrain 映射轴序检查（电机轴下标 " + std::to_string(k) + "）",
                "电机轴排列与对应关节串联序不一致（jointIndex="
                    + std::to_string(model.motorAxes[k].jointIndex) + "）——"
                    "不允许静默重排（重排等价于改输入，须由组装方显式完成）",
                "按对应关节串联序重排电机轴表后重新组装模型"));
            result.conditionNumber = 0.0;
            return result;
        }
    }
    for (std::size_t r = 0; r < model.chat.rows; ++r) {
        for (std::size_t c = 0; c < model.chat.cols; ++c) {
            if (!isFinite(model.chat(r, c))) {
                result.diagnostics.push_back(makeDtRecord(
                    kDtMatrixNonfinite, std::nullopt, std::nullopt,
                    "drivetrain 映射矩阵检查（行 " + std::to_string(r)
                        + "，列 " + std::to_string(c) + "）",
                    "归一化矩阵含非有限元素（NaN/±Inf）——NFR-COR-03 非有限即"
                    "拒绝，不得静默转 0 或默认通过",
                    "修正 robot-drivetrain 配置数据后重新编译组装"));
                result.conditionNumber = 0.0;
                return result;
            }
        }
    }
    // 非对角阻断（在有限性之后——先排除 NaN 使非对角比较无歧义；任一非
    // 对角元素非零即阻断：不对角化绕过、不静默拆成独立轴——保留交叉耦合
    // 语义属 R2 窗口路径；未声明窗口的非对角输入在 R1/R2 两能力位下同码
    // 阻断——结构语义与能力位无关，AT-38 反例 nondiag-2axis 同款）。
    for (std::size_t r = 0; r < model.chat.rows; ++r) {
        for (std::size_t c = 0; c < model.chat.cols; ++c) {
            if (r != c && model.chat(r, c) != 0.0) {
                result.diagnostics.push_back(makeDtRecord(
                    kDtMatrixNondiagonalLocked, std::nullopt, std::nullopt,
                    "drivetrain 映射矩阵形态检查（行 " + std::to_string(r)
                        + "，列 " + std::to_string(c) + "）",
                    "归一化矩阵含非零非对角元素（交叉耦合必须经 R2 耦合窗口"
                    "声明——未声明窗口的非对角结构阻断：不对角化绕过、不静默"
                    "拆成独立轴）",
                    "在 R1 能力下改用对角传动模型；耦合链场景以 R2 能力装配并"
                    "经 makeCoupledDriveTrainModel 声明耦合窗口"));
                result.conditionNumber = 0.0;
                return result;
            }
        }
    }
    for (std::size_t k = 0; k < nMotors; ++k) {
        if (model.chat(k, k) == 0.0) {
            result.diagnostics.push_back(makeDtRecord(
                kDtRatioZero, model.motorAxes[k].motorId, std::nullopt,
                "drivetrain 映射传动比检查（电机轴下标 " + std::to_string(k) + "）",
                "对角传动比 c=0（c＝Δq_joint/Δθ_motor——除法无意义，非法）",
                "修正该轴传动比配置（有限非零值）"));
            result.conditionNumber = 0.0;
            return result;
        }
    }

    // ---- 卡 §7.3：良态条件数（对角解析式 max|c|/min|c|——谱条件数在对
    // 角阵上的精确形态；阈值来源 P-RT-7 设计默认，P-DT-2 登记归属）。
    // 比较型诊断：实际条件数/阈值/无量纲（ERR-01/AT-27）。
    const double cond = diagonalConditionNumber(model.ratios);
    result.conditionNumber = cond;
    if (!(cond <= kWellConditionedLimit)) {
        core::DiagnosticRecord rec = makeDtRecord(
            kDtMatrixIllConditioned, std::nullopt, std::nullopt,
            "drivetrain 映射矩阵良态检查",
            "对角传动条件数超限（病态矩阵——映射数值不稳定，阻止不降级；"
            "阈值＝P-RT-7 设计默认 1×10⁸，无量纲）",
            "调整传动比配置使条件数回到良态域；阈值归属随 P-DT-2/P-RT-7 "
            "裁决（若归 EngineeringPolicySet 则改经 Policy 条目声明消费）");
        rec.comparison = compareOf(cond, kWellConditionedLimit, "1");
        result.diagnostics.push_back(std::move(rec));
        result.accepted = false;
        return result;
    }

    // ---- 全部检查通过（对角良态模型——R1/R2 均接受）。
    result.accepted = true;
    return result;
}

// =====================================================================
// R2 矩阵路径校验（窗口存在——§7.2 全表，WP-18-T05）
// =====================================================================

CouplingValidationResult DriveTrainMappingCore::validateCoupledWindow(
    const DriveTrainModel& model) const
{
    CouplingValidationResult result;
    const CouplingWindow& win = *model.window;
    const std::size_t nJoints = model.jointAxes.size();
    const std::size_t nMotors = model.motorAxes.size();

    // ---- §7.2 行 1：非方矩阵（窗口矩阵自身形态前提——行＝适用关节数、
    // 列＝对应电机轴数，二者必须相等；如差动/冗余驱动——R2 按
    // MDL-21/runtime 口径不支持）。结构精确判据（无阈值）。
    if (!win.C.wellFormed() || win.C.rows != win.C.cols || win.C.rows != win.jointRange.size()) {
        result.diagnostics.push_back(makeDtRecord(
            kDtMatrixNonsquare, std::nullopt, std::nullopt,
            "drivetrain 映射矩阵形态检查（R2 耦合窗口）",
            "耦合矩阵非方（C " + std::to_string(win.C.rows) + "×"
                + std::to_string(win.C.cols) + "，窗口关节数 "
                + std::to_string(win.jointRange.size())
                + "）——电机轴数≠适用关节数（如差动/冗余驱动），R2 按 "
                "MDL-21/runtime 口径不支持，阻止不降级",
            "修正耦合矩阵与窗口关节集合（方阵前提——MDL-21）"));
        result.conditionNumber = 0.0;
        return result;
    }
    const std::size_t w = win.C.rows;

    // ---- 窗口关节集合非空（空窗口＝退化声明——0×0 矩阵在"方阵"判定上
    // 空真通过，必须在奇异值计算前显式拦截；R2 窗口必须声明至少一个适用
    // 关节，违约同码 DT-INPUT-DIMENSION-MISMATCH，阻断不降级）。
    if (w == 0 || win.jointRange.empty()) {
        result.diagnostics.push_back(makeDtRecord(
            kDtInputDimensionMismatch, std::nullopt, std::nullopt,
            "drivetrain 映射窗口关节集合检查（R2）",
            "耦合窗口为空声明（0×0 矩阵/空 jointRange）——R2 窗口必须声明"
                "至少一个适用关节（退化窗口不是合法 R2 形态）",
            "以 makeCoupledDriveTrainModel 声明非空窗口，或移除 window 改走"
                "对角路径"));
        result.conditionNumber = 0.0;
        return result;
    }

    // ---- 窗口关节集合合法性：逐项存在于关节轴表、互不相同且按全局下标
    // 严格升序（窗口内串联序）。违约同码 DT-INPUT-DIMENSION-MISMATCH
    // （§7.2 行 5——集合不一致）。
    std::vector<std::size_t> windowIdx; // 窗口关节全局下标（升序）
    windowIdx.reserve(w);
    for (std::size_t i = 0; i < w; ++i) {
        const core::ObjectId& id = win.jointRange[i];
        std::size_t found = nJoints; // nJoints＝"未找到"哨兵
        for (std::size_t j = 0; j < nJoints; ++j) {
            if (model.jointAxes[j].jointId == id) {
                found = j;
                break;
            }
        }
        if (found == nJoints || (i > 0 && windowIdx[i - 1] >= found)) {
            // 未找到（窗口引用不在轴表）或非严格升序（重复/乱序）——
            // 窗口与自由轴集合的互补划分被破坏（§7.1：不得重叠）。
            result.diagnostics.push_back(makeDtRecord(
                kDtInputDimensionMismatch, id, std::nullopt,
                "drivetrain 映射窗口关节集合检查（窗口位次 " + std::to_string(i) + "）",
                "窗口关节引用非法（不在关节轴表）或窗口序非严格升序（重复/"
                    "乱序）——窗口与自由轴集合必须互补且按串联序（§7.1，"
                    "重叠→维度违约）",
                "修正耦合窗口的 jointRange（升序、互异、全部命中轴表）"));
            result.conditionNumber = 0.0;
            return result;
        }
        windowIdx.push_back(found);
    }

    // ---- chat 与块对角组合的结构前提：方阵（nJoints==nMotors——自由轴
    // 1:1＋窗口 w:w 的划分下总数必相等）、维度齐备。
    if (nJoints != nMotors || !model.chat.wellFormed() || model.chat.rows != nJoints
        || model.chat.cols != nMotors || model.ratios.size() != nMotors) {
        result.diagnostics.push_back(makeDtRecord(
            kDtInputDimensionMismatch, std::nullopt, std::nullopt,
            "drivetrain 映射维度检查（R2）",
            "维度不匹配（关节轴 " + std::to_string(nJoints) + "，电机轴 "
                + std::to_string(nMotors) + "，chat " + std::to_string(model.chat.rows)
                + "×" + std::to_string(model.chat.cols)
                + "，ratios " + std::to_string(model.ratios.size())
                + "）——R2 块对角组合要求 Ĉ 为方阵且逐轴视图齐备",
            "修正组装方的归一化模型构造（makeCoupledDriveTrainModel 工厂已强制）"));
        result.conditionNumber = 0.0;
        return result;
    }

    // ---- 轴序纪律（§5.3——电机轴按对应关节串联序；不静默重排）。
    for (std::size_t k = 0; k < nMotors; ++k) {
        if (model.motorAxes[k].jointIndex != k) {
            result.diagnostics.push_back(makeDtRecord(
                kDtInputAxisOrderMismatch, model.motorAxes[k].motorId, std::nullopt,
                "drivetrain 映射轴序检查（电机轴下标 " + std::to_string(k) + "）",
                "电机轴排列与对应关节串联序不一致（jointIndex="
                    + std::to_string(model.motorAxes[k].jointIndex) + "）——"
                    "不允许静默重排（重排等价于改输入，须由组装方显式完成）",
                "按对应关节串联序重排电机轴表后重新组装模型"));
            result.conditionNumber = 0.0;
            return result;
        }
    }

    // ---- 非有限检查（整个 Ĉ——**先于**块对角一致性比较：NaN 任一元素
    // 即拒绝，NFR-COR-03 精确判据；先排除非有限使一致性等值比较无歧义，
    // 与对角路径"有限性在形态比较之前"同一纪律）。
    for (std::size_t r = 0; r < model.chat.rows; ++r) {
        for (std::size_t c = 0; c < model.chat.cols; ++c) {
            if (!isFinite(model.chat(r, c))) {
                result.diagnostics.push_back(makeDtRecord(
                    kDtMatrixNonfinite, std::nullopt, std::nullopt,
                    "drivetrain 映射矩阵检查（行 " + std::to_string(r)
                        + "，列 " + std::to_string(c) + "）",
                    "归一化矩阵含非有限元素（NaN/±Inf）——NFR-COR-03 非有限即"
                    "拒绝，不得静默转 0 或默认通过",
                    "修正 robot-drivetrain 配置数据后重新编译组装"));
                result.conditionNumber = 0.0;
                return result;
            }
        }
    }

    // ---- chat 与块对角组合 diag(c_free)⊕C_w 逐元素一致（§7.1——归一化
    // 矩阵是窗口 C 与自由轴传动比的唯一规范化视图；不一致＝组装方构造
    // 破坏归一化前提，维度/结构违约同码）。
    {
        std::vector<char> inWindow(nJoints, 0);
        for (const std::size_t idx : windowIdx) {
            inWindow[idx] = 1;
        }
        // 自由轴对角元＝ratios[k]；窗口块＝C_w；其余（跨块）元素＝0。
        for (std::size_t r = 0; r < nJoints; ++r) {
            for (std::size_t c = 0; c < nMotors; ++c) {
                const double expected = [&] {
                    // 期望值：窗口块内取 C_w 对应元；其余位置对角元取
                    // ratios 视图、跨块/窗口外非对角取 0。
                    if (inWindow[r] && inWindow[c]) {
                        std::size_t ri = 0;
                        std::size_t ci = 0;
                        for (std::size_t i = 0; i < windowIdx.size(); ++i) {
                            if (windowIdx[i] == r) ri = i;
                            if (windowIdx[i] == c) ci = i;
                        }
                        return win.C(ri, ci);
                    }
                    return (r == c) ? model.ratios[r].c : 0.0;
                }();
                if (!(model.chat(r, c) == expected)) {
                    result.diagnostics.push_back(makeDtRecord(
                        kDtInputDimensionMismatch, std::nullopt, std::nullopt,
                        "drivetrain 映射块对角一致性检查（行 " + std::to_string(r)
                            + "，列 " + std::to_string(c) + "）",
                        "归一化矩阵 Ĉ 与块对角组合 diag(c_free)⊕C_w 不一致"
                            "（§7.1——Ĉ 是窗口与自由轴传动比的唯一规范化视图）",
                        "以 makeCoupledDriveTrainModel 工厂重构归一化模型"
                            "（禁手工拼装 Ĉ）"));
                    result.conditionNumber = 0.0;
                    return result;
                }
            }
        }
    }

    // ---- 自由轴零传动比（对角路径同码；窗口轴的对角**投影值**不适用
    // 本码——窗口映射按 C_w 全矩阵执行，可逆性由下方奇异检查把守，
    // C_w 对角元为 0 的置换形耦合合法）。
    {
        std::vector<char> inWindow(nJoints, 0);
        for (const std::size_t idx : windowIdx) {
            inWindow[idx] = 1;
        }
        for (std::size_t k = 0; k < nMotors; ++k) {
            if (!inWindow[k] && model.ratios[k].c == 0.0) {
                result.diagnostics.push_back(makeDtRecord(
                    kDtRatioZero, model.motorAxes[k].motorId, std::nullopt,
                    "drivetrain 映射传动比检查（自由轴下标 " + std::to_string(k) + "）",
                    "自由轴对角传动比 c=0（c＝Δq_joint/Δθ_motor——除法无意义，"
                        "非法）",
                    "修正该自由轴传动比配置（有限非零值）"));
                result.conditionNumber = 0.0;
                return result;
            }
        }
    }

    // ---- §7.2 行 2：奇异矩阵（σmin ≤ σmax×1×10⁻¹² ⇔ κ ≥ 1×10¹² 视为
    // det≈0——双精度有效秩为零；**不得以伪逆放行**，§7.3 伪逆使用边界）。
    // 奇异值经单侧 Jacobi SVD（确定性——文件头注），比较型诊断携带
    // σmin/σmax 比值（与 modeling I-MDL-11 重算复核同族设计默认——
    // P-DT-2 留痕，映射侧为 runtime 编译校验后的第二道防线）。
    const std::vector<double> sigma = singularValuesJacobi(win.C);
    const double sigmaMax = sigma.front(); // 降序首元＝σmax
    const double sigmaMin = sigma.back();  // 降序末元＝σmin
    if (sigmaMax == 0.0 || sigmaMin <= sigmaMax * kDtSingularSigmaRatio) {
        core::DiagnosticRecord rec = makeDtRecord(
            kDtMatrixSingular, std::nullopt, std::nullopt,
            "drivetrain 映射矩阵形态检查（R2 耦合窗口）",
            "耦合矩阵奇异（σmin/σmax 落入奇异分界带——det≈0/不可逆；不得以"
                "伪逆放行，θ̈＝C⁺·q̈ 无良态意义，阻止不降级）",
            "修正耦合矩阵配置（满秩——MDL-21/runtime 编译口径同源）；"
                "阈值归属随 P-DT-2/P-RT-7 裁决");
        rec.comparison = compareOf(sigmaMax == 0.0 ? 0.0 : sigmaMin / sigmaMax,
                                   kDtSingularSigmaRatio, "1");
        result.diagnostics.push_back(std::move(rec));
        result.conditionNumber
            = (sigmaMax == 0.0) ? std::numeric_limits<double>::infinity()
                                : sigmaMax / sigmaMin;
        result.accepted = false;
        return result;
    }

    // ---- §7.2 行 3：病态矩阵（谱条件数 κ＝σmax/σmin 超限——映射数值
    // 不稳定，阻止不降级；阈值单点 kWellConditionedLimit＝P-RT-7 设计
    // 默认 1×10⁸，与 runtime 编译校验同源——P-DT-2 留痕）。比较型诊断：
    // 实际条件数/阈值/无量纲（ERR-01/AT-27）。
    const double kappa = sigmaMax / sigmaMin;
    result.conditionNumber = kappa;
    if (!(kappa <= kWellConditionedLimit)) {
        core::DiagnosticRecord rec = makeDtRecord(
            kDtMatrixIllConditioned, std::nullopt, std::nullopt,
            "drivetrain 映射矩阵良态检查（R2 耦合窗口）",
            "耦合矩阵条件数超限（病态——θ̈＝C⁺·q̈ 数值不稳定，阻止不降级；"
                "阈值＝P-RT-7 设计默认 1×10⁸，无量纲）",
            "调整耦合矩阵配置使条件数回到良态域；阈值归属随 P-DT-2/P-RT-7 "
                "裁决（若归 EngineeringPolicySet 则改经 Policy 条目声明消费）");
        rec.comparison = compareOf(kappa, kWellConditionedLimit, "1");
        result.diagnostics.push_back(std::move(rec));
        result.accepted = false;
        return result;
    }

    // ---- 全部检查通过（窗口常矩阵良态——R2 接受；常矩阵前提由本值形态
    // 结构性保证——DriveTrainModel 单一常矩阵字段无按工况变化载体，
    // DT-MATRIX-TIME-VARYING-UNSUPPORTED 在本表示中无触发面）。
    result.accepted = true;
    return result;
}

// =====================================================================
// 输入快照校验（§13.2——try 轨，逐项诊断不抛）
// =====================================================================

TransmissionValidationResult DriveTrainMappingCore::validate(const DriveTrainModel& model,
                                                             const JointSeriesView& series) const
{
    TransmissionValidationResult result;
    result.ok = true;

    // ---- 空输入（数据类返回诊断——evaluate 路径再 fail-fast）。
    if (model.jointAxes.empty() || model.motorAxes.empty() || series.samples.empty()) {
        result.diagnostics.push_back(makeDtRecord(
            kDtInputEmpty, std::nullopt, std::nullopt,
            "drivetrain 输入快照校验",
            "关节轴表/电机轴表/上游序列为空（空模型没有评估意义）",
            "提供非空轴表与非空样本序列"));
        result.ok = false;
    }

    // ---- 序列关节集合与模型轴表一致（§12.1——不同序列混用＝调用方错误；
    // 此处按 try 轨给 DT-SERIES-LENGTH-MISMATCH 诊断——P-DT-6 键名对齐前
    // 的本卡承载）。
    if (series.jointIds.size() != model.jointAxes.size()) {
        result.diagnostics.push_back(makeDtRecord(
            kDtSeriesLengthMismatch, std::nullopt, std::nullopt,
            "drivetrain 输入快照校验",
            "序列关节数与模型轴表不一致（序列 " + std::to_string(series.jointIds.size())
                + "，模型 " + std::to_string(model.jointAxes.size()) + "）",
            "按模型轴表重组上游序列视图"));
        result.ok = false;
    }

    // ---- 时间戳严格单调递增（§12.1——时间非单调是数据类问题：不排序吞
    // 错，返回诊断；统计面据此降级——不输出"部分 RMS 冒充完整循环"）。
    for (std::size_t i = 1; i < series.samples.size(); ++i) {
        if (!(series.samples[i - 1].t < series.samples[i].t)) {
            result.diagnostics.push_back(makeDtRecord(
                kDtInputTimeNonmonotonic, std::nullopt, std::nullopt,
                "drivetrain 输入快照校验（样本下标 " + std::to_string(i) + "）",
                "采样时间非严格递增（上游序列必须严格单调——本卡不重采样、不排序）",
                "修正上游 dynamics 序列的时间戳（t 单位 s）"));
            result.ok = false;
            break; // 首处命中即报（确定性首错——NFR-COR-02）
        }
    }

    // ---- 样本值有限性（NFR-COR-03——非有限拒绝；try 轨逐处记录，
    // evaluate 路径对首处 fail-fast）。
    for (std::size_t i = 0; i < series.samples.size(); ++i) {
        const JointDriveSample& s = series.samples[i];
        if (!isFinite(s.t) || !isFinite(s.q) || !isFinite(s.qd) || !isFinite(s.qdd)
            || !isFinite(s.tauJoint)) {
            result.diagnostics.push_back(makeDtRecord(
                kDtMatrixNonfinite, std::nullopt, std::nullopt,
                "drivetrain 输入快照校验（样本下标 " + std::to_string(i) + "）",
                "上游序列样本含非有限值（t/q/q̇/q̈/τ_joint 任一）",
                "修正上游 dynamics 序列（非有限值不得进入映射）"));
            result.ok = false;
        }
    }

    // ---- 效率/转子条目的值合法性（构造工厂已拦；此处为直接构造模型的
    // 防御面——值非法属调用方契约违约的 try 轨记录）。
    for (std::size_t k = 0; k < model.efficiency.size(); ++k) {
        const EfficiencyModel& e = model.efficiency[k];
        if (!isFinite(e.etaForward) || !isFinite(e.etaBackward) || e.etaForward <= 0.0
            || e.etaForward > 1.0 || e.etaBackward <= 0.0 || e.etaBackward > 1.0) {
            core::DiagnosticRecord rec = makeDtRecord(
                kDtEfficiencyInvalid,
                k < model.motorAxes.size()
                    ? std::optional<core::ObjectId>(model.motorAxes[k].motorId)
                    : std::nullopt,
                std::nullopt,
                "drivetrain 输入快照校验（电机轴下标 " + std::to_string(k) + "）",
                "效率值超出合法域 (0,1]（η⁺/η⁻ 有限且在域内为前提——实际值"
                "取 η⁺ 侧，期望侧＝合法域上界 1）",
                "修正效率模型来源值（无量纲 (0,1]）");
            rec.comparison = compareOf(e.etaForward, 1.0, "1");
            result.diagnostics.push_back(std::move(rec));
            result.ok = false;
        }
    }
    for (std::size_t k = 0; k < model.rotor.size(); ++k) {
        const RotorInertiaModel& r = model.rotor[k];
        if (!isFinite(r.rotorInertia) || r.rotorInertia <= 0.0) {
            result.diagnostics.push_back(makeDtRecord(
                kDtInertiaInvalid,
                k < model.motorAxes.size()
                    ? std::optional<core::ObjectId>(model.motorAxes[k].motorId)
                    : std::nullopt,
                std::nullopt,
                "drivetrain 输入快照校验（电机轴下标 " + std::to_string(k) + "）",
                "转子等效惯量非法（必须＞0 且有限，单位 kg·m²——卡 §9.2）",
                "修正 SEL-10 回填/组装方提供的转子字段值（kg·m²）"));
            result.ok = false;
        }
    }

    return result;
}

// =====================================================================
// 虚功/功率一致性检查（§8——理想口径逐元素精确对照）
// =====================================================================

std::vector<ConsistencyFinding> DriveTrainMappingCore::checkVirtualWork(
    const DriveTrainModel& model, const JointSeriesView& series,
    const std::vector<double>& actual) const
{
    std::vector<ConsistencyFinding> findings;
    const std::size_t n = series.samples.size();
    const std::size_t m = model.motorAxes.size();
    // 维度核对：actual 按"样本×轴"行主序扁平承载（不足时逐元素报告无法
    // 成立——返回空清单由调用方维度断言发现，检查器不抛）。
    if (actual.size() < n * m) {
        return findings;
    }

    // 窗口关节全局下标（模型无窗口则空——纯对角路径）。
    std::vector<std::size_t> windowIdx;
    if (model.window.has_value()) {
        for (const core::ObjectId& id : model.window->jointRange) {
            for (std::size_t j = 0; j < model.jointAxes.size(); ++j) {
                if (model.jointAxes[j].jointId == id) {
                    windowIdx.push_back(j);
                    break;
                }
            }
        }
    }
    const std::size_t w = windowIdx.size();

    for (std::size_t i = 0; i < n; ++i) {
        // 期望值（理想虚功对偶）：窗口轴按 C_wᵀ·τ_w（§7.1——交叉耦合逐
        // 元素保留）；自由轴 c_j·τ_j（§8.1 对角形）。同一表达式路径下
        // 映射核心自产值与此位等；独立供给值的对照供黄金数据集与诊断
        // 定位使用。精确位等比较（无阈值——§8.4）。
        // 注：JointSeriesView 逐时刻单样本、各轴共用该时刻值（既有语义
        // ——样本结构为单时刻承载），窗口 τ 向量逐轴同值。
        std::vector<double> idealW;
        if (w > 0) {
            std::vector<double> tauWindow(w, series.samples[i].tauJoint); // N·m
            idealW = windowIdealMotorTorque(model.window->C, tauWindow);
        }
        for (std::size_t k = 0; k < m; ++k) {
            double expected;
            if (w > 0) {
                // 窗口轴：k 的窗口内位次＝windowIdx 中与 motorAxes[k].
                // jointIndex 相等者的下标（不在窗口的轴走自由轴公式）。
                std::size_t wi = w; // 哨兵＝不在窗口
                for (std::size_t j = 0; j < w; ++j) {
                    if (windowIdx[j] == model.motorAxes[k].jointIndex) {
                        wi = j;
                        break;
                    }
                }
                expected = (wi < w) ? idealW[wi]
                                    : model.ratios[k].c * series.samples[i].tauJoint;
            } else {
                expected = model.ratios[k].c * series.samples[i].tauJoint;
            }
            const double got = actual[i * m + k];
            if (!(got == expected)) {
                ConsistencyFinding f;
                f.sampleIndex = i;
                f.axisIndex = k;
                f.t = series.samples[i].t;
                f.field = "tau_ideal_motor[N*m]";
                f.actual = got;
                f.expected = expected;
                findings.push_back(std::move(f));
            }
        }
    }
    return findings;
}

std::vector<ConsistencyFinding> DriveTrainMappingCore::checkPowerBalance(
    const DriveTrainModel& model, const JointSeriesView& series,
    const std::vector<double>& actualMotorPower) const
{
    std::vector<ConsistencyFinding> findings;
    const std::size_t n = series.samples.size();
    const std::size_t m = model.motorAxes.size();
    if (actualMotorPower.size() < n * m) {
        return findings;
    }

    // 窗口关节全局下标（模型无窗口则空——纯对角路径）。
    std::vector<std::size_t> windowIdx;
    if (model.window.has_value()) {
        for (const core::ObjectId& id : model.window->jointRange) {
            for (std::size_t j = 0; j < model.jointAxes.size(); ++j) {
                if (model.jointAxes[j].jointId == id) {
                    windowIdx.push_back(j);
                    break;
                }
            }
        }
    }
    const std::size_t w = windowIdx.size();

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < m; ++k) {
            // 窗口轴跳过逐轴对照（下方按窗口和恒等核对——耦合窗口内单轴
            // 的关节侧对应功率无唯一定义，§8.2 ②恒等式在窗口粒度成立：
            // Σ_{k∈W} τ_ideal,k·θ̇_k ＝ τ_wᵀ·C·C⁻¹·q̇_w ＝ τ_wᵀ·q̇_w）。
            bool inWindow = false;
            for (std::size_t j = 0; j < w; ++j) {
                if (windowIdx[j] == model.motorAxes[k].jointIndex) {
                    inWindow = true;
                    break;
                }
            }
            if (inWindow) {
                continue;
            }
            // 自由轴：理想机械功率＝τ_j·q̇（§8.2 口径②——逐样本逐轴恒等；
            // 逐元素断言不以总和替代——防正负抵消，附录 D C4）。
            const double expected = series.samples[i].tauJoint * series.samples[i].qd;
            const double got = actualMotorPower[i * m + k];
            if (!(got == expected)) {
                ConsistencyFinding f;
                f.sampleIndex = i;
                f.axisIndex = k;
                f.t = series.samples[i].t;
                f.field = "power_ideal_motor[W]";
                f.actual = got;
                f.expected = expected;
                findings.push_back(std::move(f));
            }
        }
        // 窗口和恒等（窗口粒度——本检查器对照的独立供给值按"样本×轴"
        // 行主序承载，对窗口内各轴 actual 求和后与窗口关节功率核对）。
        if (w > 0) {
            double actualSum = 0.0;
            double expectedSum = 0.0;
            for (std::size_t k = 0; k < m; ++k) {
                for (std::size_t j = 0; j < w; ++j) {
                    if (windowIdx[j] == model.motorAxes[k].jointIndex) {
                        actualSum += actualMotorPower[i * m + k]; // W（独立供给）
                        break;
                    }
                }
            }
            for (std::size_t j = 0; j < w; ++j) {
                expectedSum += series.samples[i].tauJoint * series.samples[i].qd; // W
            }
            if (!(actualSum == expectedSum)) {
                ConsistencyFinding f;
                f.sampleIndex = i;
                f.axisIndex = windowIdx.front(); // 窗口首轴（定位面——窗口级发现）
                f.t = series.samples[i].t;
                f.field = "power_ideal_window_sum[W]";
                f.actual = actualSum;
                f.expected = expectedSum;
                findings.push_back(std::move(f));
            }
        }
    }
    return findings;
}

// =====================================================================
// 反射惯量与惯量比（§9.2/§9.5——数值事实，无阈值判定）
// =====================================================================

ReflectedInertiaResult DriveTrainMappingCore::evaluate(
    const DriveTrainModel& model, const std::vector<LoadInertiaEntry>& load) const
{
    // 矩阵/轴表防御面（与 §6.3/§7.2 同码——直接构造模型绕过工厂时的第二
    // 道防线；阻断面全表检查复用同一实现路径）。★能力位按模型形态取：
    // 窗口存在＝R2 声明形态（§7.2 矩阵检查面），无窗口＝对角形态——本步
    // 骤接口是映射管线的内部对照点，装配能力把关在 evaluate() 主入口
    // （卡 §13.4 @throws"矩阵未过 §7.2 校验"按矩阵形态选择检查路径）。
    const StageCapability gateStage = model.window.has_value()
                                          ? StageCapability::R2Capability
                                          : StageCapability::R1Capability;
    const CouplingValidationResult gate = validate(model, gateStage);
    if (!gate.accepted) {
        throwContract(gate.diagnostics.front().code,
                      "反射惯量评估前置矩阵校验未通过（" + gate.diagnostics.front().context
                          + "：" + gate.diagnostics.front().cause + "）");
    }

    // 窗口关节全局下标（R2 时非空）。
    std::vector<std::size_t> windowIdx;
    if (model.window.has_value()) {
        for (const core::ObjectId& id : model.window->jointRange) {
            for (std::size_t j = 0; j < model.jointAxes.size(); ++j) {
                if (model.jointAxes[j].jointId == id) {
                    windowIdx.push_back(j);
                    break;
                }
            }
        }
    }
    const std::size_t w = windowIdx.size();
    const std::size_t n = model.motorAxes.size();

    // 窗口轴转子条目齐备性（§9.3 完整矩阵以 diag(J_rotor)≻0 为构造前提
    // ——任一窗口转子条目缺失时完整矩阵不产出（部分数据下的"完整"矩阵
    // 会以零对角元破坏正定性——不伪造）；缺失面由主管线 DT-ROTOR-MISSING
    // 缺清单承载，本步骤只决定产出形态）。
    bool windowRotorComplete = true;
    if (w > 0) {
        for (const std::size_t idx : windowIdx) {
            if (idx >= model.rotor.size()) {
                windowRotorComplete = false;
                break;
            }
        }
    }

    // ---- R2 前置：窗口逆矩阵 C⁻¹（校验通过⟹可逆良态——§7.3 使用前提；
    // Gauss-Jordan 部分主元，确定性——文件头注）。
    RowMatrix cinv;
    if (w > 0) {
        cinv = invertSquareMatrix(model.window->C);
    }

    ReflectedInertiaResult result;
    result.axes.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        ReflectedInertiaAxis axis;
        axis.jointIndex = model.motorAxes[k].jointIndex;
        // 轴的窗口位次（哨兵 n＝自由轴）。
        std::size_t wi = n;
        for (std::size_t j = 0; j < w; ++j) {
            if (windowIdx[j] == k) {
                wi = j;
                break;
            }
        }
        // 转子条目缺失→反射惯量不可得（§10.7——数据类降级）：J_reflected
        // 保持 0，由 evaluate 主管线以 missingItems 标注（本步骤接口不携
        // 带清单）。转子条目值非法在构造期已被拒（DT-INERTIA-INVALID），
        // 此处按缺失同轨处理保持函数总有界。
        if (k < model.rotor.size()) {
            if (wi == n) {
                // 自由轴（对角）：J_reflected ＝ J_rotor/c²——即 DYN-04 记法
                // J·i²（i＝1/c 换算，卡 §5.3/§9.2）。单位 kg·m²。
                const double c = model.ratios[k].c;
                axis.jReflectedJointSide = model.rotor[k].rotorInertia / (c * c);
                // 惯量比（§9.5 对角式）：c²·J_load@joint / J_rotor（无量纲）。
                if (k < load.size()) {
                    axis.inertiaRatio = (c * c) * load[k].loadInertiaJointSide
                                        / model.rotor[k].rotorInertia;
                }
                axis.windowProjected = false;
            } else if (windowRotorComplete) {
                // 窗口轴（R2，§9.3/§9.5）：对角视图为完整反射惯量矩阵的
                // 对角元投影——(C⁻¹ᵀ·diag(J_rotor)·C⁻¹)(wi,wi)
                //   ＝Σ_m J_rotor,m·C⁻¹(wi,m)²（固定列升序求和）；
                // 附窗口投影限定标记（§9.5——单轴比值为投影值，交叉项保留
                // 在完整矩阵中）。★投影求和遍历全部窗口转子条目——任一
                // 窗口转子缺失（windowRotorComplete==false）时本轴视图同样
                // 按缺失降级（保持 0——与缺条目同轨，不产部分投影冒充实值）。
                double jDiag = 0.0;
                for (std::size_t mm = 0; mm < w; ++mm) {
                    const double g = cinv(mm, wi); // C⁻¹(mm, wi)——窗口内列
                    jDiag += model.rotor[windowIdx[mm]].rotorInertia * g * g;
                }
                axis.jReflectedJointSide = jDiag; // kg·m²（关节轴系）
                axis.windowProjected = true;
                // 惯量比（§9.5 R2 窗口式）：(C⁻¹ᵀ·J_load·C⁻¹)(wi,wi)
                //   / J_rotor,wi——负载折算惯量矩阵同形投影（load 按电机轴
                // 全局下标配对——条目 j 对应电机轴 j）。
                if (windowIdx[wi] < load.size()) {
                    double loadDiag = 0.0;
                    for (std::size_t mm = 0; mm < w; ++mm) {
                        const double g = cinv(mm, wi);
                        loadDiag += load[windowIdx[mm]].loadInertiaJointSide * g * g;
                    }
                    axis.inertiaRatio
                        = loadDiag / model.rotor[windowIdx[wi]].rotorInertia;
                }
            }
        }
        result.axes.push_back(axis);
    }

    // ---- R2 完整关节轴系反射惯量矩阵（§9.3——不默认对角化：交叉惯量项
    // 保留在完整矩阵中随结果归档；块对角形态：自由轴对角 J/c²＋窗口块
    // C⁻¹ᵀ·diag(J_rotor)·C⁻¹。窗口转子条目任一缺失时不产出——见上）。
    if (w > 0 && windowRotorComplete) {
        RowMatrix jref;
        jref.rows = n;
        jref.cols = n;
        jref.data.assign(n * n, 0.0);
        std::vector<char> inWindow(n, 0);
        for (const std::size_t idx : windowIdx) {
            inWindow[idx] = 1;
        }
        for (std::size_t r = 0; r < n; ++r) {
            for (std::size_t c = 0; c < n; ++c) {
                if (inWindow[r] && inWindow[c]) {
                    // 窗口块：(C⁻¹ᵀ·D·C⁻¹)(r,c)＝Σ_m D(m)·C⁻¹(m,r)·C⁻¹(m,c)
                    // （D＝窗口转子对角——固定 m 升序求和；构造保证对称——
                    // 同一表达式交换 r/c 求和序一致）。
                    std::size_t ri = 0;
                    std::size_t ci = 0;
                    for (std::size_t i = 0; i < w; ++i) {
                        if (windowIdx[i] == r) ri = i;
                        if (windowIdx[i] == c) ci = i;
                    }
                    double sum = 0.0;
                    for (std::size_t m2 = 0; m2 < w; ++m2) {
                        sum += model.rotor[windowIdx[m2]].rotorInertia
                               * cinv(m2, ri) * cinv(m2, ci);
                    }
                    jref.data[r * n + c] = sum; // kg·m²
                } else if (r == c) {
                    // 自由轴对角：J_rotor/c²。
                    const double cc = model.ratios[r].c;
                    jref.data[r * n + c] = model.rotor[r].rotorInertia / (cc * cc);
                }
                // 跨块元素保持 0（块对角构造）。
            }
        }
        // 正定性经 Cholesky 判定（§9.3——失败→DT-INERTIA-NOT-POSITIVE-
        // DEFINITE，属输入/矩阵非法，阻止并诊断）。构造保证（J_rotor≻0、
        // C 可逆⟹J_ref≻0）下失败只可能来自极端舍入——防御面 fail-fast。
        {
            bool positiveDefinite = true;
            std::vector<double> chol(n * n, 0.0); // 下三角 L（J_ref＝L·Lᵀ）
            for (std::size_t i = 0; i < n && positiveDefinite; ++i) {
                for (std::size_t j = 0; j <= i; ++j) {
                    double sum = jref(i, j);
                    for (std::size_t p = 0; p < j; ++p) {
                        sum -= chol[i * n + p] * chol[j * n + p];
                    }
                    if (i == j) {
                        if (!(sum > 0.0)) { // 主元非正→非正定（含 NaN 防御）
                            positiveDefinite = false;
                            break;
                        }
                        chol[i * n + j] = std::sqrt(sum);
                    } else {
                        chol[i * n + j] = sum / chol[j * n + j];
                    }
                }
            }
            if (!positiveDefinite) {
                throwContract(kDtInertiaNotPositiveDefinite,
                              "R2 关节侧反射惯量矩阵 Cholesky 正定性判定失败"
                              "（§9.3——J_ref＝(C⁻¹)ᵀ·diag(J_rotor)·C⁻¹ 在"
                              " J_rotor≻0、C 可逆前提下应恒正定；到达此处＝"
                              "极端舍入或实现缺陷）");
            }
        }
        result.jointSideFullMatrix = std::move(jref);
    }
    return result;
}

// =====================================================================
// 效率折算（§10.2/§10.3——方向精确符号判据＋降级）
// =====================================================================

void DriveTrainMappingCore::apply(const DriveTrainModel& model,
                                  const JointSeriesView& series,
                                  std::vector<MotorSeries>& motorSeries,
                                  std::vector<core::DiagnosticRecord>& diagnostics,
                                  std::vector<std::string>& missingItems,
                                  bool& estimatedSeen) const
{
    (void)series; // 折算只依赖逐样本 pJoint（已写入 MotorDriveSample）与模型效率条目
    const std::size_t m = model.motorAxes.size();
    for (std::size_t k = 0; k < m && k < motorSeries.size(); ++k) {
        MotorSeries& ms = motorSeries[k];
        const core::ObjectId axisId = model.motorAxes[k].motorId;

        // ---- 轴效率条目缺失→功率/能量/四象限降级素材（§10.7——不以
        // η＝1 静默替代；力矩/速度/位置映射照常输出）。缺失项列入清单
        // （"efficiency[j=k]" 形态——卡 §13.8 示例）；数据缺失不产稳定
        // 码诊断（码表只有值非法码 DT-EFFICIENCY-INVALID）。
        if (k >= model.efficiency.size()) {
            missingItems.push_back("efficiency[j=" + std::to_string(k) + "]");
            for (MotorDriveSample& s : ms.samples) {
                s.efficiencyApplicable = false; // 下游统计据此识别降级
            }
            continue;
        }

        const EfficiencyModel& eta = model.efficiency[k];
        if (eta.source == SourcedValueTag::Estimated) {
            estimatedSeen = true; // 估算来源贯穿结果（§10.7——限定语素材）
        }

        for (MotorDriveSample& s : ms.samples) {
            // 方向判据＝关节侧机械功率符号的**精确**测试（§10.2——不需要
            // 阈值；方向按能量流向而非转向符号，倒转运行〔ω＜0〕仍由功率
            // 符号决定，与四象限统计正交）。
            if (s.pJoint > 0.0) {
                // 驱动方向：功率自电机流向负载——电机侧输出更多机械功率
                //（传动箱消耗 ΔP＞0）：P_trans ＝ P_joint / η⁺。
                s.pTransmission = s.pJoint / eta.etaForward;
                s.efficiencyApplicable = true;
            } else if (s.pJoint < 0.0) {
                // 再生方向：功率自负载经传动流回电机侧——|P_motor|＜
                // |P_joint|（传动箱折损）：P_trans ＝ P_joint · η⁻。
                s.pTransmission = s.pJoint * eta.etaBackward;
                s.efficiencyApplicable = true;
            } else {
                // 精确零：效率不适用（显式标记——ERR-01，不伪造数值）；
                // P_trans ＝ 0，该样本不计入能量分项与四象限统计的有效样本
                // 集合（§8.3/§10.6）。
                s.pTransmission = 0.0;
                s.efficiencyApplicable = false;
            }
            // 电机侧总机械功率＝效率折算传动功率＋转子功率项（§10.3——
            // 分项分别报告不混写）。
            s.pMotor = s.pTransmission + s.pRotor;
        }
    }
}

// =====================================================================
// 电机工作点统计（§10.4/§10.6/§11）
// =====================================================================

std::vector<MotorOperatingPoint> DriveTrainMappingCore::summarize(
    const DriveTrainModel& model,
    const JointSeriesView& series,
    const std::vector<MotorSeries>& motorSeries,
    std::vector<core::DiagnosticRecord>& diagnostics,
    std::vector<std::string>& missingItems) const
{
    std::vector<MotorOperatingPoint> points;
    points.reserve(motorSeries.size());

    // ---- 循环完整性（§10.4 数据不足行）：样本数＜2 无法积分；时间非单调
    // 拒绝统计——不输出"部分 RMS 冒充完整循环 RMS"。
    bool cycleComplete = series.samples.size() >= 2;
    for (std::size_t i = 1; cycleComplete && i < series.samples.size(); ++i) {
        if (!(series.samples[i - 1].t < series.samples[i].t)) {
            cycleComplete = false;
        }
    }
    if (!cycleComplete) {
        missingItems.push_back("cycle-interval");
        if (!series.samples.empty()) {
            diagnostics.push_back(makeDtRecord(
                kDtInputSampleMissing, std::nullopt, std::nullopt,
                "drivetrain 工作点统计",
                "完整任务循环不成立（样本数＜2 或时间非单调）——峰值/RMS/"
                "能量/四象限按数据不足降级",
                "补充完整循环的上游序列后重评（循环边界＝上游序列完整区间）"));
        }
    }
    // 循环时长（s）＝上游序列完整循环区间（§10.4——RMS 分母；降级时为 0
    // 且统计不产出）。
    const double cycleT = cycleComplete
                              ? (series.samples.back().t - series.samples.front().t)
                              : 0.0;

    for (std::size_t k = 0; k < motorSeries.size(); ++k) {
        const MotorSeries& ms = motorSeries[k];
        MotorOperatingPoint p;
        p.axisId = ms.axisId;
        if (ms.jointIndex < model.jointAxes.size()) {
            p.jointId = model.jointAxes[ms.jointIndex].jointId;
        }
        p.jointIndex = ms.jointIndex;
        p.caseId = series.caseId;

        const bool axisEfficiencyPresent = (k < model.efficiency.size());
        if (axisEfficiencyPresent) {
            p.etaApplied = model.efficiency[k];
            if (model.efficiency[k].source == SourcedValueTag::Estimated) {
                p.estimatedSource = true; // 估算限定语（§10.7）
            }
        }
        if (k < model.rotor.size()) {
            // 反射惯量（§9.2 对角式 J/c²，关节轴系，kg·m²——数值事实照常，
            // 效率降级不影响本面）。
            const double c = model.ratios[k].c;
            p.reflectedInertia = model.rotor[k].rotorInertia / (c * c);
        }

        // ---- 峰值扫描（§10.4——正/负 τ 分列不混取绝对值；|θ̇|、|P| 取
        // 带符号实测值报告；时刻/段/工况全携带——"峰值不带来源即非法"）。
        if (!ms.samples.empty()) {
            std::size_t iMax = 0;
            std::size_t iMin = 0;
            std::size_t iOmega = 0;
            std::size_t iPower = 0;
            for (std::size_t i = 1; i < ms.samples.size(); ++i) {
                if (ms.samples[i].tauMotor > ms.samples[iMax].tauMotor) iMax = i;
                if (ms.samples[i].tauMotor < ms.samples[iMin].tauMotor) iMin = i;
                if (std::fabs(ms.samples[i].thetaDot)
                    > std::fabs(ms.samples[iOmega].thetaDot)) iOmega = i;
                if (std::fabs(ms.samples[i].pMotor)
                    > std::fabs(ms.samples[iPower].pMotor)) iPower = i;
            }
            // 段标注从上游同下标样本取（电机样本与上游样本同序同刻——
            // §12.1 逐时间戳对齐，本卡不重采样）。
            const std::string& segMax = (iMax < series.samples.size())
                                            ? series.samples[iMax].segmentId
                                            : std::string{};
            const std::string& segMin = (iMin < series.samples.size())
                                            ? series.samples[iMin].segmentId
                                            : std::string{};
            if (ms.samples[iMax].tauMotor > 0.0) {
                p.tauPeakPos.present = true;
                p.tauPeakPos.value = ms.samples[iMax].tauMotor; // N·m
                p.tauPeakPos.t = ms.samples[iMax].t;            // s
                p.tauPeakPos.segmentId = segMax;
                p.tauPeakPos.caseId = series.caseId;
            }
            if (ms.samples[iMin].tauMotor < 0.0) {
                p.tauPeakNeg.present = true;
                p.tauPeakNeg.value = ms.samples[iMin].tauMotor; // N·m（负值报告）
                p.tauPeakNeg.t = ms.samples[iMin].t;
                p.tauPeakNeg.segmentId = segMin;
                p.tauPeakNeg.caseId = series.caseId;
            }
            p.omegaPeak.present = true;
            p.omegaPeak.value = ms.samples[iOmega].thetaDot; // rad/s（带符号）
            p.omegaPeak.t = ms.samples[iOmega].t;
            p.omegaPeak.segmentId = (iOmega < series.samples.size())
                                        ? series.samples[iOmega].segmentId
                                        : std::string{};
            p.omegaPeak.caseId = series.caseId;
            if (axisEfficiencyPresent) {
                // 功率峰值在效率降级轴不产出（P 列属降级面——§10.7）。
                p.powerPeak.present = true;
                p.powerPeak.value = ms.samples[iPower].pMotor; // W（带符号）
                p.powerPeak.t = ms.samples[iPower].t;
                p.powerPeak.segmentId = (iPower < series.samples.size())
                                            ? series.samples[iPower].segmentId
                                            : std::string{};
                p.powerPeak.caseId = series.caseId;
            }
        }

        // ---- RMS（§10.4——完整循环含驻留：驻留段力矩保持计入热负载；
        // RMS(x)＝√(∫x²dt/T)，梯形积分）。效率降级轴 τ/ω 两列照常输出。
        if (cycleComplete && !ms.samples.empty()) {
            std::vector<double> ts;
            std::vector<double> tau2;
            std::vector<double> omega2;
            ts.reserve(ms.samples.size());
            tau2.reserve(ms.samples.size());
            omega2.reserve(ms.samples.size());
            for (const MotorDriveSample& s : ms.samples) {
                ts.push_back(s.t);
                tau2.push_back(s.tauMotor * s.tauMotor);
                omega2.push_back(s.thetaDot * s.thetaDot);
            }
            p.tauRms = std::sqrt(trapezoidIntegral(ts, tau2) / cycleT);   // N·m
            p.omegaRms = std::sqrt(trapezoidIntegral(ts, omega2) / cycleT); // rad/s
        }

        // ---- 负载率（§10.4——参考值呈现，硬筛选归 selection；额定值缺失
        // →不适用显式标记〔optional 空〕——P-DT-2 同源：不内嵌阈值）。
        if (k < model.ratedTorque.size() && model.ratedTorque[k].has_value()
            && *model.ratedTorque[k] > 0.0 && cycleComplete) {
            p.loadRatio = p.tauRms / *model.ratedTorque[k]; // 无量纲
        }

        // ---- 效率降级轴：功率/能量/四象限统计不产出（§10.7——DataInsuf-
        // ficient 素材；上方已完成的 τ/ω 峰值与 RMS 照常），标 Partial 后
        // 进入下一轴。
        if (!axisEfficiencyPresent) {
            p.quality = CompletenessState::Partial;
            points.push_back(std::move(p));
            continue;
        }

        // ---- 能量分项（§10.5——梯形积分；零功率〔精确零〕样本不计入分项
        // 有效集：以该样本置 0 功率进入积分实现区间剔除；eLoss＝∫(pTrans−
        // pJoint)dt 恒＞0——两方向折算均消耗，见 Series.hpp 口径声明）。
        if (cycleComplete && !ms.samples.empty()) {
            std::vector<double> ts;
            std::vector<double> pMotorV;
            std::vector<double> pJointV;
            std::vector<double> pLossV;
            std::vector<double> pRegenV;
            std::vector<double> pPosV;
            std::vector<double> pRotorV;
            ts.reserve(ms.samples.size());
            pMotorV.reserve(ms.samples.size());
            pJointV.reserve(ms.samples.size());
            pLossV.reserve(ms.samples.size());
            pRegenV.reserve(ms.samples.size());
            pPosV.reserve(ms.samples.size());
            pRotorV.reserve(ms.samples.size());
            for (const MotorDriveSample& s : ms.samples) {
                ts.push_back(s.t);
                if (!s.efficiencyApplicable) {
                    // 零功率样本不计入能量分项（§8.3）——全部列置 0。
                    pMotorV.push_back(0.0);
                    pJointV.push_back(0.0);
                    pLossV.push_back(0.0);
                    pRegenV.push_back(0.0);
                    pPosV.push_back(0.0);
                    pRotorV.push_back(0.0);
                    continue;
                }
                pMotorV.push_back(s.pMotor);                                   // W
                pJointV.push_back(s.pJoint);                                   // W
                pLossV.push_back(s.pTransmission - s.pJoint);                  // W（恒＞0）
                pRegenV.push_back(std::max(0.0, -s.pJoint));                   // W
                pPosV.push_back(std::max(0.0, s.pJoint));                      // W
                pRotorV.push_back(s.pRotor);                                   // W
            }
            p.energy.eMotor = trapezoidIntegral(ts, pMotorV);   // J
            p.energy.eJoint = trapezoidIntegral(ts, pJointV);   // J
            p.energy.eLoss = trapezoidIntegral(ts, pLossV);     // J
            p.energy.eRegen = trapezoidIntegral(ts, pRegenV);   // J
            p.energy.ePos = trapezoidIntegral(ts, pPosV);       // J
            p.energy.eRotor = trapezoidIntegral(ts, pRotorV);   // J
        }

        // ---- 四象限统计（§10.6——ω＝θ̇、P＝τ_m·θ̇ 符号组合；零速/零功率
        // 样本只计时间占比与样本计数，不计象限能量）。象限判定功率用
        // τ_m·θ̇（含转子项——卡 §10.6 表头口径），与 pMotor 字段（效率
        // 折算口径）区分。时间占比＝样本区间时长/T（区间归属左样本——
        // 与象限能量的左矩形约定一致；占比与能量同约定可互核）。
        if (cycleComplete && !ms.samples.empty()) {
            for (std::size_t i = 0; i < ms.samples.size(); ++i) {
                const MotorDriveSample& s = ms.samples[i];
                // 样本区间＝[t_i, t_{i+1})（末样本区间长 0——占比归一中不
                // 贡献时长，仅计数）。
                const double span = (i + 1 < ms.samples.size())
                                        ? (ms.samples[i + 1].t - s.t)
                                        : 0.0;
                const double pQuad = s.tauMotor * s.thetaDot; // W（象限判定口径）
                const bool zero = (s.thetaDot == 0.0) || (pQuad == 0.0)
                                  || (!s.efficiencyApplicable);
                QuadrantStats* target = nullptr;
                if (zero) {
                    target = &p.zeroDwell; // 驻留/保持——不计象限能量
                } else if (s.thetaDot > 0.0 && pQuad > 0.0) {
                    target = &p.q1; // 正转电动
                } else if (s.thetaDot > 0.0 && pQuad < 0.0) {
                    target = &p.q2; // 正转再生
                } else if (s.thetaDot < 0.0 && pQuad < 0.0) {
                    target = &p.q3; // 反转电动
                } else {
                    target = &p.q4; // 反转再生
                }
                target->sampleCount += 1;
                target->timeShare += span / cycleT; // 无量纲占比
                if (!zero) {
                    // 象限能量＝该象限样本区间 ∫pJoint dt（左矩形——与占
                    // 比同区间约定可互核；象限边界交叉项的物理意义本就模糊，
                    // 保守取左矩形并在注释留痕）。
                    target->energy += s.pJoint * span; // J
                }
            }
        }

        points.push_back(std::move(p));
    }
    return points;
}

// =====================================================================
// 主管线：单工况完整映射评估（§13.1——四参能力位入口）
// =====================================================================

DriveTrainMappingOutput DriveTrainMappingCore::evaluate(const DriveTrainModel& model,
                                                        const JointSeriesView& series,
                                                        ICancellation* ctx)
{
    // 三参形态＝R1 能力（语义冻结——既有调用方行为零变化；卡 §15 T05
    // 红线"不提前放开 R1 阻断"：R1 能力永远不接受耦合窗口）。
    return evaluate(model, series, ctx, StageCapability::R1Capability);
}

DriveTrainMappingOutput DriveTrainMappingCore::evaluate(const DriveTrainModel& model,
                                                        const JointSeriesView& series,
                                                        ICancellation* ctx,
                                                        StageCapability stage)
{
    // ---- 批次边界取消查询（§12.4——工况批次边界；单工况入口即批次边界。
    // 取消后不发布完整结果：返回空素材＋取消诊断，消费方按 TASK-02 处置）。
    if (ctx != nullptr && ctx->cancellationRequested()) {
        DriveTrainMappingOutput cancelled;
        cancelled.identity = model.identity;
        cancelled.upstreamSliceId = series.upstreamSliceId;
        cancelled.algorithmVersion = model.identity.algorithmVersion;
        cancelled.contractVersion = model.identity.contractVersion;
        cancelled.caseId = series.caseId;
        cancelled.completeness = CompletenessState::Partial;
        cancelled.missingItems.push_back("cancelled");
        cancelled.diagnostics.push_back(makeDtRecord(
            kDtEvaluationCancelled, std::nullopt, std::nullopt,
            "drivetrain 映射评估（工况批次边界）",
            "宿主在批次边界请求取消——不发布完整结果（TASK-02：取消后不得"
            "进入正式报告或可行集）",
            "重新派发该工况批次"));
        return cancelled;
    }

    // ---- 第一步：阻断面全表检查（§6.3/§7.2——顺序执行、首个命中即阻止；
    // 调用方错误 fail-fast，不返回半结果）。能力位由调用方注入（R1＝对角
    // 路径＋窗口拒绝；R2＝窗口经 §7.2 全表检查后进入矩阵映射）。
    const CouplingValidationResult gate = validate(model, stage);
    if (!gate.accepted) {
        const core::DiagnosticRecord& d = gate.diagnostics.front();
        throwContract(d.code, d.context + "：" + d.cause);
    }

    // ---- 第二步：输入快照校验（§13.2——空输入/维度/非有限为调用方错误
    // fail-fast；时间非单调为数据类降级）。
    const TransmissionValidationResult inputCheck = validate(model, series);
    for (const core::DiagnosticRecord& d : inputCheck.diagnostics) {
        // 空输入、序列维度违约与非有限样本＝契约违约（fail-fast）；时间
        // 非单调＝数据类（降级继续——逐样本映射与时间序无关，统计面拒绝）。
        if (d.code == kDtInputEmpty || d.code == kDtMatrixNonfinite
            || d.code == kDtSeriesLengthMismatch || d.code == kDtEfficiencyInvalid
            || d.code == kDtInertiaInvalid) {
            throwContract(d.code, d.cause);
        }
    }
    const bool timeMonotonic
        = std::none_of(inputCheck.diagnostics.begin(), inputCheck.diagnostics.end(),
                       [](const core::DiagnosticRecord& rec) {
                           return rec.code == kDtInputTimeNonmonotonic;
                       });

    // ---- 第三步：逐样本映射（封闭代数运算）——按窗口形态分派：
    // 无窗口＝R1 对角路径（§6.2，逐轴标量）；有窗口＝R2 块对角路径
    // （§7.1：自由轴同 R1、窗口轴矩阵映射——交叉耦合逐元素保留）。
    const std::size_t m = model.motorAxes.size();
    std::vector<MotorSeries> motorSeries(m);

    if (!model.window.has_value()) {
        // ============ R1 对角路径（行为冻结——与 T03 落位逐位一致）============
        for (std::size_t k = 0; k < m; ++k) {
            MotorSeries& ms = motorSeries[k];
            ms.axisId = model.motorAxes[k].motorId;
            ms.jointIndex = model.motorAxes[k].jointIndex;
            ms.samples.reserve(series.samples.size());

            const double c = model.ratios[k].c;          // 带符号传动比（无量纲）
            const double thetaOff = (k < model.zeroOffsetMotor.size())
                                        ? model.zeroOffsetMotor[k]
                                        : 0.0;           // 零位偏置（rad；组装方保证长度，兜底 0）
            const bool hasRotor = (k < model.rotor.size());
            const double jRotor = hasRotor ? model.rotor[k].rotorInertia : 0.0; // kg·m²（电机轴系）

            for (std::size_t i = 0; i < series.samples.size(); ++i) {
                const JointDriveSample& js = series.samples[i];
                requireFiniteSample(js, i, ms.jointIndex);
                MotorDriveSample s;
                s.t = js.t;
                // 位置/速度/加速度映射（常矩阵 C 的对角形）：θ＝θ_off＋q/c；
                // θ̇＝q̇/c；θ̈＝q̈/c（§6.2 前三行——偏置只影响绝对位置，不影响
                // 速度/加速度/力矩/功率）。
                s.theta = thetaOff + js.q / c;   // rad
                s.thetaDot = js.qd / c;          // rad/s
                s.thetaDDot = js.qdd / c;        // rad/s²
                // 理想虚功对偶：τ_m ＝ c·τ_joint（口径①，N·m）。
                s.tauIdeal = c * js.tauJoint;
                // 关节侧机械功率（逐元素 τ·q̇——§8.3 复核口径，W）。
                s.pJoint = js.tauJoint * js.qd;
                // 转子功率项 J_rotor·θ̈·θ̇（§10.3 分项——转子缺失为 0，W）。
                s.pRotor = hasRotor ? (jRotor * s.thetaDDot * s.thetaDot) : 0.0;
                // 含转子项电机力矩（M-12 全量口径④）：τ_m＝c·τ_j＋J_rotor·θ̈。
                // 转子缺失→按理想口径输出（§6.2——降级标注由缺清单承载，N·m）。
                s.tauMotor = hasRotor ? (s.tauIdeal + jRotor * s.thetaDDot) : s.tauIdeal;
                // 效率折算与总功率在效率步骤（方向按 pJoint 符号）填充——
                // 此处保持 0/false，效率降级轴它们也保持 0（不伪造）。
                s.pTransmission = 0.0;   // W
                s.pMotor = 0.0;          // W
                s.efficiencyApplicable = false;
                s.quadrant = Quadrant::ZeroDwell;
                ms.samples.push_back(s);
            }
        }
    } else {
        // ============ R2 块对角路径（§7.1——WP-18-T05）============
        const CouplingWindow& win = *model.window;
        const std::size_t w = win.jointRange.size();
        // 窗口关节全局下标（升序——校验已保证合法；此处重复解析以避免
        // 校验器私有态，与 validateCoupledWindow 同规则）。
        std::vector<std::size_t> windowIdx;
        windowIdx.reserve(w);
        for (const core::ObjectId& id : win.jointRange) {
            for (std::size_t j = 0; j < model.jointAxes.size(); ++j) {
                if (model.jointAxes[j].jointId == id) {
                    windowIdx.push_back(j);
                    break;
                }
            }
        }
        std::vector<char> inWindow(m, 0);
        for (const std::size_t idx : windowIdx) {
            inWindow[idx] = 1;
        }

        // C⁻¹（校验通过⟹方阵良态可逆——§7.3 使用前提：C⁺ 仅作已批准公式
        // θ̈_motor＝C⁺·q̈_joint 的计算表示，方阵良态下 C⁺≡C⁻¹；求逆一次、
        // 全序列复用——纯函数内局部量，无跨调用状态）。
        const RowMatrix cinv = invertSquareMatrix(win.C);

        // 逐轴初始化（轴序纪律⟹电机轴下标＝关节下标）。
        for (std::size_t k = 0; k < m; ++k) {
            motorSeries[k].axisId = model.motorAxes[k].motorId;
            motorSeries[k].jointIndex = model.motorAxes[k].jointIndex;
            motorSeries[k].samples.reserve(series.samples.size());
        }

        for (std::size_t i = 0; i < series.samples.size(); ++i) {
            const JointDriveSample& js = series.samples[i];

            // ---- 自由轴：与 R1 完全相同的标量公式（同一表达式——自由轴
            // 输出与既有对角路径逐位一致，构成 R1 等价回归锚的实现面）。
            for (std::size_t k = 0; k < m; ++k) {
                if (inWindow[k]) {
                    continue;
                }
                requireFiniteSample(js, i, motorSeries[k].jointIndex);
                const double c = model.ratios[k].c; // 带符号传动比（无量纲）
                const double thetaOff = (k < model.zeroOffsetMotor.size())
                                            ? model.zeroOffsetMotor[k]
                                            : 0.0; // rad
                const bool hasRotor = (k < model.rotor.size());
                const double jRotor = hasRotor ? model.rotor[k].rotorInertia : 0.0; // kg·m²

                MotorDriveSample s;
                s.t = js.t;
                s.theta = thetaOff + js.q / c;   // rad（§6.2——偏置只影响绝对位置）
                s.thetaDot = js.qd / c;          // rad/s
                s.thetaDDot = js.qdd / c;        // rad/s²
                s.tauIdeal = c * js.tauJoint;    // N·m（口径①）
                s.pJoint = js.tauJoint * js.qd;  // W（逐元素 τ·q̇）
                s.pRotor = hasRotor ? (jRotor * s.thetaDDot * s.thetaDot) : 0.0; // W
                s.tauMotor = hasRotor ? (s.tauIdeal + jRotor * s.thetaDDot)
                                      : s.tauIdeal; // N·m（口径④）
                s.pTransmission = 0.0;   // W（效率步骤填充）
                s.pMotor = 0.0;          // W
                s.efficiencyApplicable = false;
                s.quadrant = Quadrant::ZeroDwell;
                motorSeries[k].samples.push_back(s);
            }

            // ---- 窗口轴：矩阵映射（§7.1 冻结口径）。窗口关节的 q/q̇/q̈/τ
            // 按窗口内串联序组装（本序列视图逐时刻单样本、各轴共用该时刻
            // 值——既有 JointSeriesView 语义），经 C_w⁻¹/C_wᵀ 映射后逐窗口
            // 电机轴写回（窗口内电机轴按窗口关节序＝全局下标升序）。
            std::vector<double> qW(w, 0.0);
            std::vector<double> qdW(w, 0.0);
            std::vector<double> qddW(w, 0.0);
            std::vector<double> tauW(w, 0.0);
            for (std::size_t j = 0; j < w; ++j) {
                requireFiniteSample(js, i, windowIdx[j]);
                qW[j] = js.q;          // rad
                qdW[j] = js.qd;        // rad/s
                qddW[j] = js.qdd;      // rad/s²
                tauW[j] = js.tauJoint; // N·m
            }
            // 位置/速度/加速度：θ＝C⁻¹·(q−q_ref)（q_ref 默认 0——卡 §5.3
            // 零位偏置行；θ_off 逐电机轴另加）、θ̇＝C⁻¹·q̇、θ̈＝C⁺·q̈＝C⁻¹·q̈。
            const std::vector<double> thetaW = matVec(cinv, qW);       // rad
            const std::vector<double> thetaDotW = matVec(cinv, qdW);   // rad/s
            const std::vector<double> thetaDDotW = matVec(cinv, qddW); // rad/s²
            // 理想虚功对偶：τ_ideal＝C_wᵀ·τ_w（口径①——交叉项逐元素保留，
            // AT-38；与自检共用同一函数保证位等）。
            const std::vector<double> tauIdealW = windowIdealMotorTorque(win.C, tauW); // N·m

            for (std::size_t j = 0; j < w; ++j) {
                const std::size_t k = windowIdx[j]; // 窗口电机轴全局下标
                const double thetaOff = (k < model.zeroOffsetMotor.size())
                                            ? model.zeroOffsetMotor[k]
                                            : 0.0; // rad
                const bool hasRotor = (k < model.rotor.size());
                const double jRotor = hasRotor ? model.rotor[k].rotorInertia : 0.0; // kg·m²

                MotorDriveSample s;
                s.t = js.t;
                s.theta = thetaOff + thetaW[j];       // rad（偏置只影响绝对位置）
                s.thetaDot = thetaDotW[j];            // rad/s
                s.thetaDDot = thetaDDotW[j];          // rad/s²
                s.tauIdeal = tauIdealW[j];            // N·m（含交叉耦合项）
                // 关节侧机械功率（窗口轴取虚功元素口径：pJoint,k＝τ_ideal,k·
                // θ̇_k——耦合窗口内单轴的"对应关节功率"无唯一定义，取电机侧
                // 理想虚功元素承载方向语义；窗口和恒等窗口关节功率
                // Στ_ideal,k·θ̇_k＝τ_wᵀ·q̇_w——§8.2 ②窗口粒度，偏差登记单元卡
                // §18.3）。自由轴保持 τ·q̇ 原式（位等回归锚）。
                s.pJoint = s.tauIdeal * s.thetaDot;   // W
                s.pRotor = hasRotor ? (jRotor * s.thetaDDot * s.thetaDot) : 0.0; // W
                s.tauMotor = hasRotor ? (s.tauIdeal + jRotor * s.thetaDDot)
                                      : s.tauIdeal; // N·m（M-12 口径④——转子项
                                                    // 只加在电机侧，§9.4）
                s.pTransmission = 0.0;   // W（效率步骤填充）
                s.pMotor = 0.0;          // W
                s.efficiencyApplicable = false;
                s.quadrant = Quadrant::ZeroDwell;
                motorSeries[k].samples.push_back(s);
            }
        }
    }

    // ---- 第四步：效率折算（§10.2/§10.3——数据类降级收集缺失清单）。
    std::vector<core::DiagnosticRecord> diagnostics;
    std::vector<std::string> missingItems;
    bool estimatedSeen = false;
    apply(model, series, motorSeries, diagnostics, missingItems, estimatedSeen);

    // 转子缺失标注（§6.2/§10.7——力矩按理想口径输出＋DT-ROTOR-MISSING；
    // 数据缺失走缺失清单＋码面提示——码表语义即"缺失降级"）。
    for (std::size_t k = 0; k < m; ++k) {
        if (k >= model.rotor.size()) {
            missingItems.push_back("rotor[j=" + std::to_string(k) + "]");
            diagnostics.push_back(makeDtRecord(
                kDtRotorMissing, model.motorAxes[k].motorId, std::nullopt,
                "drivetrain 映射（电机轴下标 " + std::to_string(k) + "）",
                "转子等效惯量条目缺失——含转子项力矩不可得，力矩按理想口径"
                "输出（标注未含转子项）",
                "补充 SEL-10 独立转子字段（kg·m²）后重评"));
        }
    }

    // 时间非单调（数据类）：validate 的数据类诊断（DT-INPUT-TIME-NONMONO-
    // TONIC）并入输出诊断——数据类问题走诊断＋降级，不抛不吞；统计面由
    // summarize 拒绝（DT-INPUT-SAMPLE-MISSING＋"cycle-interval" 缺清单）。
    if (!timeMonotonic) {
        missingItems.push_back("monotonic-time");
        for (const core::DiagnosticRecord& rec : inputCheck.diagnostics) {
            if (rec.code == kDtInputTimeNonmonotonic) {
                diagnostics.push_back(rec);
            }
        }
    }

    // ---- 第五步：反射惯量（§9——数值事实）。负载折算惯量不属于序列输入
    // （P-DT-4——组装方值传递，随结果装配注入惯量比）；本管线无负载惯量
    // 输入时惯量比＝不适用（显式标记，不伪造）。
    const ReflectedInertiaResult inertia = evaluate(model, {});

    // ---- 第六步：映射自检（§8.4 ②——理想部分与映射公式重算位等：同一
    // 表达式路径构造必然通过；此检查作为内部不变量守护，未来实现演进时
    // 失同步即暴露）。窗口轴期望按 C_wᵀ·τ_w 重算（共用 windowIdealMotor-
    // Torque——位等前提），自由轴按 c·τ_j（对角原式）。
    if (model.window.has_value()) {
        const CouplingWindow& win = *model.window;
        const std::size_t w = win.jointRange.size();
        std::vector<std::size_t> windowIdx;
        windowIdx.reserve(w);
        for (const core::ObjectId& id : win.jointRange) {
            for (std::size_t j = 0; j < model.jointAxes.size(); ++j) {
                if (model.jointAxes[j].jointId == id) {
                    windowIdx.push_back(j);
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < series.samples.size(); ++i) {
            std::vector<double> tauW(w, series.samples[i].tauJoint); // N·m
            const std::vector<double> expectedW = windowIdealMotorTorque(win.C, tauW);
            for (std::size_t j = 0; j < w; ++j) {
                const std::size_t k = windowIdx[j];
                if (k >= m || i >= motorSeries[k].samples.size()) {
                    continue; // 防御（校验已保证维度一致——不可达）
                }
                const MotorDriveSample& s = motorSeries[k].samples[i];
                if (!(s.tauIdeal == expectedW[j])) {
                    throwContract(kDtInternalInvariant,
                                  "理想映射自检失同步（窗口轴 " + std::to_string(k)
                                      + " 样本 " + std::to_string(i) + "）");
                }
            }
        }
    }
    for (std::size_t k = 0; k < m; ++k) {
        // 自由轴逐轴位等自检（窗口轴已按窗口形态核对——跳过；无窗口时
        // 全轴对角原式）。
        if (model.window.has_value()) {
            bool isWindowAxis = false;
            for (const core::ObjectId& id : model.window->jointRange) {
                if (model.jointAxes[k].jointId == id) {
                    isWindowAxis = true;
                    break;
                }
            }
            if (isWindowAxis) {
                continue;
            }
        }
        const double c = model.ratios[k].c;
        for (std::size_t i = 0; i < motorSeries[k].samples.size(); ++i) {
            const MotorDriveSample& s = motorSeries[k].samples[i];
            const double expected = c * series.samples[i].tauJoint;
            if (!(s.tauIdeal == expected)) {
                // 不变量被破坏＝实现缺陷（非调用方错误也非环境错误）——
                // fail-fast 暴露而非吞错（AGENTS §3 错误处理纪律）。
                throwContract(kDtInternalInvariant,
                              "理想映射自检失同步（轴 " + std::to_string(k)
                                  + " 样本 " + std::to_string(i) + "）");
            }
        }
    }

    // ---- 第七步：工作点统计（§10.4/§10.6/§11——峰值/RMS/负载率/四象限/
    // 能量分项；降级轴由 summarize 内标注）。
    std::vector<MotorOperatingPoint> points
        = summarize(model, series, motorSeries, diagnostics, missingItems);

    // ---- 反射惯量注入工作点（points 侧——reflectedInertia 字段填充；
    // 惯量比在本管线（无负载惯量输入）保持不适用）。
    for (std::size_t k = 0; k < points.size() && k < inertia.axes.size(); ++k) {
        points[k].reflectedInertia = inertia.axes[k].jReflectedJointSide;
    }

    // ---- 装配总输出（身份绑定——§6.2"结果身份"行）。
    DriveTrainMappingOutput out;
    out.motorSeries = std::move(motorSeries);
    out.points = std::move(points);
    out.inertia = inertia;
    out.diagnostics = std::move(diagnostics);
    out.completeness = missingItems.empty() ? CompletenessState::Complete
                                            : CompletenessState::Partial;
    out.missingItems = std::move(missingItems);
    if (estimatedSeen) {
        out.missingItems.push_back("estimated-source"); // 估算限定语素材（§10.7）
    }
    out.identity = model.identity;
    out.upstreamSliceId = series.upstreamSliceId;
    out.algorithmVersion = model.identity.algorithmVersion;
    out.contractVersion = model.identity.contractVersion;
    out.caseId = series.caseId;
    return out;
}

// =====================================================================
// 对角归一化模型工厂（MappingTypes.hpp 声明的构造入口——构造即校验；
// 实现聚合于本翻译单元：与阻断面检查共用同一错误语义与码值书写点）
// =====================================================================

DriveTrainModel makeDiagonalDriveTrainModel(
    std::vector<JointDriveAxis> jointAxes,
    std::vector<MotorDriveAxis> motorAxes,
    std::vector<TransmissionRatio> ratios,
    std::vector<double> zeroOffsetMotor,
    DriveTrainIdentity identity,
    std::vector<EfficiencyModel> efficiency,
    std::vector<RotorInertiaModel> rotor,
    std::vector<std::optional<double>> ratedTorque)
{
    // ---- 校验 1：轴表非空且两侧尺寸相等（空输入 DT-INPUT-EMPTY；维度
    // 不匹配 DT-INPUT-DIMENSION-MISMATCH——卡 §6.3 表序④/⑤）。
    if (jointAxes.empty() || motorAxes.empty()) {
        throwContract(kDtInputEmpty, "关节轴表或电机轴表为空（空模型没有评估意义）");
    }
    const std::size_t n = jointAxes.size();
    if (motorAxes.size() != n || ratios.size() != n || zeroOffsetMotor.size() != n) {
        throwContract(kDtInputDimensionMismatch,
                      "关节轴/电机轴/传动比/零位偏置数量不一致（须同为轴数 "
                          + std::to_string(n) + "）");
    }

    // ---- 校验 2：关节类型范围外（Prismatic——SEL-09/MDL-12，卡 §6.3 表序③）。
    for (std::size_t j = 0; j < n; ++j) {
        if (jointAxes[j].kind == JointKind::Prismatic) {
            throwContract(kDtAxisTypeOutOfScope,
                          "关节下标 " + std::to_string(j)
                              + " 为移动关节——R1 传动映射范围外（不静默套用旋转传动）");
        }
    }

    // ---- 校验 3：轴序纪律（电机轴按对应关节串联序——不静默重排）。
    for (std::size_t k = 0; k < n; ++k) {
        if (motorAxes[k].jointIndex != k) {
            throwContract(kDtInputAxisOrderMismatch,
                          "电机轴下标 " + std::to_string(k) + " 的 jointIndex="
                              + std::to_string(motorAxes[k].jointIndex)
                              + " 与串联序不符（重排须由组装方显式完成）");
        }
    }

    // ---- 校验 4：逐轴传动比有限且非零（DT-MATRIX-NONFINITE/DT-RATIO-ZERO）
    //——工厂由 ratios 构造对角 chat，天然保证非对角为零（非对角输入走
    // 耦合窗口/组装方路径，工厂不接受）。
    for (std::size_t k = 0; k < n; ++k) {
        if (!isFinite(ratios[k].c)) {
            throwContract(kDtMatrixNonfinite,
                          "电机轴下标 " + std::to_string(k) + " 的传动比非有限"
                          "（NFR-COR-03 非有限即拒绝）");
        }
        if (ratios[k].c == 0.0) {
            throwContract(kDtRatioZero,
                          "电机轴下标 " + std::to_string(k)
                              + " 的传动比 c=0（Δq_joint/Δθ_motor 除法无意义）");
        }
    }

    // ---- 校验 5：零位偏置有限（rad——θ_off 进绝对位置映射）。
    for (std::size_t k = 0; k < n; ++k) {
        if (!isFinite(zeroOffsetMotor[k])) {
            throwContract(kDtMatrixNonfinite,
                          "电机轴下标 " + std::to_string(k) + " 的零位偏置非有限（rad）");
        }
    }

    // ---- 校验 6：效率条目值合法（η∈(0,1] 且有限——DT-EFFICIENCY-INVALID；
    // 条目可空/短于轴数＝缺失降级语义，不是非法）。
    for (std::size_t k = 0; k < efficiency.size(); ++k) {
        const EfficiencyModel& e = efficiency[k];
        if (!isFinite(e.etaForward) || !isFinite(e.etaBackward) || e.etaForward <= 0.0
            || e.etaForward > 1.0 || e.etaBackward <= 0.0 || e.etaBackward > 1.0) {
            throwContract(kDtEfficiencyInvalid,
                          "电机轴下标 " + std::to_string(k)
                              + " 的效率值超出合法域 (0,1]（无量纲）");
        }
    }

    // ---- 校验 7：转子条目值合法（J_rotor＞0 且有限，kg·m²——卡 §9.2；
    // 条目缺失＝DT-ROTOR-MISSING 降级语义）。
    for (std::size_t k = 0; k < rotor.size(); ++k) {
        if (!isFinite(rotor[k].rotorInertia) || rotor[k].rotorInertia <= 0.0) {
            throwContract(kDtInertiaInvalid,
                          "电机轴下标 " + std::to_string(k)
                              + " 的转子等效惯量非法（必须＞0 且有限，kg·m²）");
        }
    }

    // ---- 校验 8：额定/参考力矩条目（若有值）有限且＞0（N·m，电机轴系；
    // nullopt＝负载率不适用）。
    for (std::size_t k = 0; k < ratedTorque.size(); ++k) {
        if (ratedTorque[k].has_value()
            && (!isFinite(*ratedTorque[k]) || *ratedTorque[k] <= 0.0)) {
            throwContract(kDtInputDimensionMismatch,
                          "电机轴下标 " + std::to_string(k)
                              + " 的额定力矩参考值非法（必须＞0 且有限，N·m）");
        }
    }

    // ---- 校验 9：身份版本字段＞0（0＝保留值——CON-04 身份面退化）。
    if (identity.algorithmVersion == 0 || identity.contractVersion == 0) {
        throwContract(kDtInputDimensionMismatch,
                      "身份版本字段含 0（algorithmVersion/contractVersion 必须＞0"
                      "——CON-04 保留值）");
    }

    // ---- 装配对角归一化模型（chat＝diag(c)；非对角元素全零——D-DT-6：
    // 对角形式逐轴携带自身符号）。
    DriveTrainModel model;
    model.jointAxes = std::move(jointAxes);
    model.motorAxes = std::move(motorAxes);
    model.chat.rows = n;
    model.chat.cols = n;
    model.chat.data.assign(n * n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        model.chat.data[k * n + k] = ratios[k].c;
    }
    model.ratios = std::move(ratios);
    // R1 工厂不产耦合窗口（window 保持 nullopt——耦合输入被能力门控阻断）。
    model.efficiency = std::move(efficiency);
    model.rotor = std::move(rotor);
    model.zeroOffsetMotor = std::move(zeroOffsetMotor);
    model.ratedTorque = std::move(ratedTorque);
    model.identity = identity;
    return model;
}

// =====================================================================
// 耦合归一化模型工厂（MappingTypes.hpp 声明的 R2 构造入口——构造即校验；
// 实现聚合于本翻译单元：与阻断面检查共用同一错误语义与码值书写点）
// =====================================================================

DriveTrainModel makeCoupledDriveTrainModel(
    std::vector<JointDriveAxis> jointAxes,
    std::vector<MotorDriveAxis> motorAxes,
    std::vector<std::size_t> windowJointIndices,
    RowMatrix windowMatrix,
    std::vector<TransmissionRatio> freeAxisRatios,
    std::vector<double> zeroOffsetMotor,
    DriveTrainIdentity identity,
    std::vector<EfficiencyModel> efficiency,
    std::vector<RotorInertiaModel> rotor,
    std::vector<std::optional<double>> ratedTorque)
{
    // ---- 校验 1：轴表非空且两侧尺寸相等＋无 Prismatic＋轴序纪律（与对角
    // 工厂同码——R1/R2 公共结构前提；直线传动不随 R2 耦合放开——§16.2）。
    if (jointAxes.empty() || motorAxes.empty()) {
        throwContract(kDtInputEmpty, "关节轴表或电机轴表为空（空模型没有评估意义）");
    }
    const std::size_t n = jointAxes.size();
    if (motorAxes.size() != n || zeroOffsetMotor.size() != n) {
        throwContract(kDtInputDimensionMismatch,
                      "关节轴/电机轴/零位偏置数量不一致（关节/电机/偏置须同为轴数 "
                          + std::to_string(n) + "）");
    }
    for (std::size_t j = 0; j < n; ++j) {
        if (jointAxes[j].kind == JointKind::Prismatic) {
            throwContract(kDtAxisTypeOutOfScope,
                          "关节下标 " + std::to_string(j)
                              + " 为移动关节——传动映射范围外（SEL-09；R2 耦合"
                                "矩阵不改变本边界，直线传动属 §16.2 扩展）");
        }
    }
    for (std::size_t k = 0; k < n; ++k) {
        if (motorAxes[k].jointIndex != k) {
            throwContract(kDtInputAxisOrderMismatch,
                          "电机轴下标 " + std::to_string(k) + " 的 jointIndex="
                              + std::to_string(motorAxes[k].jointIndex)
                              + " 与串联序不符（重排须由组装方显式完成）");
        }
    }

    // ---- 校验 2：窗口关节集合（非空、严格升序、逐项越界检查——与自由轴
    // 互补划分的合法性前提，§7.1 块对角组合行）。
    if (windowJointIndices.empty()) {
        throwContract(kDtInputDimensionMismatch,
                      "耦合窗口关节集合为空（R2 窗口必须声明至少一个适用关节"
                          "——空窗口＝退化声明，DT-INPUT-DIMENSION-MISMATCH）");
    }
    for (std::size_t i = 0; i < windowJointIndices.size(); ++i) {
        if (windowJointIndices[i] >= n
            || (i > 0 && windowJointIndices[i - 1] >= windowJointIndices[i])) {
            throwContract(kDtInputDimensionMismatch,
                          "耦合窗口关节下标非法（位次 " + std::to_string(i)
                              + "）——须严格升序且＜轴数 " + std::to_string(n)
                              + "（互不重复、与自由轴互补）");
        }
    }

    // ---- 校验 3：窗口矩阵结构自洽（方阵 w×w、wellFormed——非方
    // DT-MATRIX-NONSQUARE；结构性失配 DT-INPUT-DIMENSION-MISMATCH）。
    if (!windowMatrix.wellFormed()) {
        throwContract(kDtInputDimensionMismatch,
                      "耦合矩阵结构失配（rows×cols 与 data.size() 不一致）");
    }
    if (windowMatrix.rows != windowMatrix.cols
        || windowMatrix.rows != windowJointIndices.size()) {
        throwContract(kDtMatrixNonsquare,
                      "耦合矩阵非方（C " + std::to_string(windowMatrix.rows) + "×"
                          + std::to_string(windowMatrix.cols) + "，窗口关节数 "
                          + std::to_string(windowJointIndices.size())
                          + "）——电机轴数≠适用关节数（R2 按 MDL-21/runtime 口径"
                            "不支持）");
    }
    const std::size_t w = windowMatrix.rows;
    const std::size_t nFree = n - w;
    if (freeAxisRatios.size() != nFree) {
        throwContract(kDtInputDimensionMismatch,
                      "自由轴传动比数量不符（给出 " + std::to_string(freeAxisRatios.size())
                          + "，须为轴数−窗口数＝" + std::to_string(nFree) + "）");
    }

    // ---- 校验 4/5：元素有限（C_w）＋自由轴 c 有限且≠0（NFR-COR-03/
    // DT-RATIO-ZERO——窗口轴对角投影值可为 0，不适用本码）。奇异/病态的
    // 数值检查归映射入口（§7.3——工厂只做精确判据结构校验）。
    for (std::size_t r = 0; r < w; ++r) {
        for (std::size_t c = 0; c < w; ++c) {
            if (!isFinite(windowMatrix(r, c))) {
                throwContract(kDtMatrixNonfinite,
                              "耦合矩阵元素非有限（行 " + std::to_string(r) + "，列 "
                                  + std::to_string(c) + "）——NFR-COR-03 非有限即拒绝");
            }
        }
    }
    for (std::size_t f = 0; f < nFree; ++f) {
        if (!isFinite(freeAxisRatios[f].c)) {
            throwContract(kDtMatrixNonfinite,
                          "自由轴下标 " + std::to_string(f) + " 的传动比非有限"
                              "（NFR-COR-03 非有限即拒绝）");
        }
        if (freeAxisRatios[f].c == 0.0) {
            throwContract(kDtRatioZero,
                          "自由轴下标 " + std::to_string(f)
                              + " 的传动比 c=0（Δq_joint/Δθ_motor 除法无意义）");
        }
    }

    // ---- 校验 6～9：偏置/效率/转子/额定力矩/身份值面（与对角工厂同码——
    // 公共值对象语义，见 makeDiagonalDriveTrainModel 校验 5～9 注）。
    for (std::size_t k = 0; k < n; ++k) {
        if (!isFinite(zeroOffsetMotor[k])) {
            throwContract(kDtMatrixNonfinite,
                          "电机轴下标 " + std::to_string(k) + " 的零位偏置非有限（rad）");
        }
    }
    for (std::size_t k = 0; k < efficiency.size(); ++k) {
        const EfficiencyModel& e = efficiency[k];
        if (!isFinite(e.etaForward) || !isFinite(e.etaBackward) || e.etaForward <= 0.0
            || e.etaForward > 1.0 || e.etaBackward <= 0.0 || e.etaBackward > 1.0) {
            throwContract(kDtEfficiencyInvalid,
                          "电机轴下标 " + std::to_string(k)
                              + " 的效率值超出合法域 (0,1]（无量纲）");
        }
    }
    for (std::size_t k = 0; k < rotor.size(); ++k) {
        if (!isFinite(rotor[k].rotorInertia) || rotor[k].rotorInertia <= 0.0) {
            throwContract(kDtInertiaInvalid,
                          "电机轴下标 " + std::to_string(k)
                              + " 的转子等效惯量非法（必须＞0 且有限，kg·m²）");
        }
    }
    for (std::size_t k = 0; k < ratedTorque.size(); ++k) {
        if (ratedTorque[k].has_value()
            && (!isFinite(*ratedTorque[k]) || *ratedTorque[k] <= 0.0)) {
            throwContract(kDtInputDimensionMismatch,
                          "电机轴下标 " + std::to_string(k)
                              + " 的额定力矩参考值非法（必须＞0 且有限，N·m）");
        }
    }
    if (identity.algorithmVersion == 0 || identity.contractVersion == 0) {
        throwContract(kDtInputDimensionMismatch,
                      "身份版本字段含 0（algorithmVersion/contractVersion 必须＞0"
                      "——CON-04 保留值）");
    }

    // ---- 装配块对角归一化模型：chat＝diag(c_free)⊕C_w（§7.1——窗口块按
    // 全局下标落位；跨块元素 0）；ratios 逐轴视图：自由轴＝c（映射原式），
    // 窗口轴＝chat(k,k)＝C_w(i,i)（对角**投影**视图——可为 0，映射按 C_w
    // 全矩阵执行）；window 携带窗口关节的 ObjectId（升序——与窗口矩阵行序
    // 一致）＋申报条件数（传递面——映射入口以重算值为准）。
    DriveTrainModel model;
    model.jointAxes = std::move(jointAxes);
    model.motorAxes = std::move(motorAxes);
    model.chat.rows = n;
    model.chat.cols = n;
    model.chat.data.assign(n * n, 0.0);
    model.ratios.assign(n, TransmissionRatio{0.0, SourcedValueTag::ModelingField});
    {
        std::vector<char> inWindow(n, 0);
        for (const std::size_t idx : windowJointIndices) {
            inWindow[idx] = 1;
        }
        std::size_t freeCursor = 0; // 自由轴传动比游标（按全局下标升序消费）
        for (std::size_t k = 0; k < n; ++k) {
            if (inWindow[k]) {
                continue;
            }
            model.chat.data[k * n + k] = freeAxisRatios[freeCursor].c;
            model.ratios[k] = freeAxisRatios[freeCursor];
            ++freeCursor;
        }
        for (std::size_t i = 0; i < w; ++i) {
            const std::size_t kr = windowJointIndices[i];
            for (std::size_t j = 0; j < w; ++j) {
                const std::size_t kc = windowJointIndices[j];
                model.chat.data[kr * n + kc] = windowMatrix(i, j);
            }
            // 窗口轴对角投影视图（C_w(i,i)——可为 0）。
            model.ratios[kr].c = windowMatrix(i, i);
        }
    }
    CouplingWindow win;
    win.C = std::move(windowMatrix);
    win.jointRange.reserve(w);
    for (const std::size_t idx : windowJointIndices) {
        win.jointRange.push_back(model.jointAxes[idx].jointId);
    }
    win.conditionNumber = 0.0; // 申报值由组装方填充；0＝未申报（映射入口重算）
    model.window = std::move(win);
    model.efficiency = std::move(efficiency);
    model.rotor = std::move(rotor);
    model.zeroOffsetMotor = std::move(zeroOffsetMotor);
    model.ratedTorque = std::move(ratedTorque);
    model.identity = identity;
    return model;
}

// =====================================================================
// 矩阵内容身份核对原语（§7.4-4——三方消费同一矩阵内容身份）
// =====================================================================

MatrixIdentityAlignment checkMatrixContentIdentityAlignment(
    const std::vector<DriveTrainIdentity>& consumedIdentities)
{
    MatrixIdentityAlignment out;
    // 空清单按不一致处置（无消费切片可核对——防调用方误用空集放行；
    // 语义登记见 MappingTypes.hpp 函数注）。
    if (consumedIdentities.empty()) {
        out.aligned = false;
        out.firstDivergence = 0;
        return out;
    }
    const DriveTrainIdentity& base = consumedIdentities.front();
    for (std::size_t i = 1; i < consumedIdentities.size(); ++i) {
        // 字节等值比较（evidence"三种等价"纪律——数值容差严禁作为身份
        // 等价关系；operator== 为字段级位等）。
        if (!(consumedIdentities[i] == base)) {
            out.aligned = false;
            out.firstDivergence = i;
            return out;
        }
    }
    out.aligned = true;
    out.firstDivergence = 0;
    return out;
}

// =====================================================================
// RowMatrix::at（越界 fail-fast——值类型边界）
// =====================================================================

double RowMatrix::at(std::size_t r, std::size_t c) const
{
    if (r >= rows || c >= cols) {
        throw std::out_of_range("drivetrain RowMatrix 索引越界（r=" + std::to_string(r)
                                + "，c=" + std::to_string(c) + "，维度 "
                                + std::to_string(rows) + "×" + std::to_string(cols) + "）");
    }
    return data[r * cols + c];
}

}  // namespace sdurws::ird::drivetrain
