/**
 * @file   MappingCore.cpp
 * @brief  传动映射核心唯一实现的落位（units/drivetrain.md §6/§7/§8/§9/
 *         §10/§13）——阻断面检查序、R1 对角精确虚功映射、虚功/功率一致性
 *         逐元素精确对照、反射惯量与惯量比（数值事实）、效率方向折算与
 *         降级、电机工作点统计（峰值/RMS/负载率/四象限/能量分项）。
 *
 * 设计依据：
 *   - units/drivetrain.md §6.2（R1 映射定义表——逐样本逐轴全列公式）、
 *     §6.3（阻断面表——顺序执行、首个命中即阻止；阻断不伪装"传动不可
 *     行"）、§7.3（良态阈值 P-RT-7 对齐——kWellConditionedLimit）、
 *     §8.1～§8.4（虚功/功率一致性四口径——运行侧精确判据无私设阈值）、
 *     §9.2/§9.5（反射惯量 J/c² 与惯量比）、§10.2～§10.7（效率方向折算/
 *     峰值/RMS/能量分项/四象限/数据不足降级）、§12.1（序列处理规则）、
 *     §13.0～§13.5（接口契约与错误语义）
 *   - 需求 DYN-04（M-12 精确虚功映射）、NFR-COR-01/02/03、SEL-09、
 *     ERR-01（不适用显式标记）、P-DT-2（惯量比阈值不内嵌）
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1/2/3）
 *
 * 确定性（NFR-COR-02）：全部计算为封闭代数运算（无迭代、无近似、无隐藏
 *   状态、无时钟/随机源）；同一输入必得位等输出。映射本身不存在算法截
 *   断误差（卡 §6.2"映射误差"行）——黄金算例对照容差仅为浮点求值路径
 *   差异留（附录 D 第 9 项标量相对 1×10⁻⁹）。
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

    // ---- 卡 §6.3 表序①：能力门控——耦合窗口存在即阻断（R1 能力未启用；
    // R2Capability 下通用矩阵数值路径未在本任务交付面——T05 承接，同样
    // 保守阻止，文案区分）。显示/配置中的 R2 标签不改变计算能力（能力由
    // 装配清单与算法版本决定）。
    if (model.window.has_value()) {
        const bool r2Stage = (stage == StageCapability::R2Capability);
        result.diagnostics.push_back(makeDtRecord(
            kDtCouplingStageLocked, std::nullopt, std::nullopt,
            "drivetrain 映射能力门控",
            r2Stage ? "模型携带 R2 耦合窗口，但本装配算法版本未含通用非对角"
                      "矩阵数值路径（WP-18-T05 承接——不提前放开 R1 阻断）"
                    : "模型携带 R2 耦合窗口而阶段能力为 R1（MDL-21 属 R2；"
                      "UI/配置中的 R2 标签不改变计算能力）",
            "在 R1 能力下改用对角传动模型；或在 R2 阶段（WP-18-T05 落位后）"
            "以 R2 能力装配并消费耦合矩阵映射"));
        result.conditionNumber = 0.0;
        return result; // 首个命中即阻止
    }

    // ---- 卡 §6.3 表序⑤前移的最低前置：空轴表（轴表空时②③④行不可判定
    // ——先阻断以免空引用；语义＝表序⑤"空输入"行的轴表半区）。
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
    // 侧已被 R1 阻断，此处为映射侧第二道防线）。
    for (std::size_t j = 0; j < model.jointAxes.size(); ++j) {
        if (model.jointAxes[j].kind == JointKind::Prismatic) {
            result.diagnostics.push_back(makeDtRecord(
                kDtAxisTypeOutOfScope, model.jointAxes[j].jointId,
                model.jointAxes[j].localName,
                "drivetrain 映射关节类型检查（关节下标 " + std::to_string(j) + "）",
                "移动关节（prismatic）在 R1 传动映射范围外（SEL-09——直线传动"
                "目录与工作点映射未启用）",
                "将该轴移出目标链或在 R2 直线传动扩展（SEL-09-S1）落位后重评"));
            result.conditionNumber = 0.0;
            return result;
        }
    }

    // ---- 卡 §6.3 表序②：非对角阻断（放在维度检查后——非对角判定要求
    // 矩阵形态可索引；先维度后元素是可判定的最小顺序，且两码同属"结构
    // 有效性"组，表内序不改变阻断语义）。
    // 卡 §6.3 表序④＋§7.2：结构有效性——维度/轴序/非有限/零传动比。
    const std::size_t nJoints = model.jointAxes.size();
    const std::size_t nMotors = model.motorAxes.size();
    if (nJoints != nMotors || !model.chat.wellFormed() || model.chat.rows != nJoints
        || model.chat.cols != nMotors || model.ratios.size() != nMotors) {
        result.diagnostics.push_back(makeDtRecord(
            kDtInputDimensionMismatch, std::nullopt, std::nullopt,
            "drivetrain 映射维度检查",
            "维度不匹配（关节轴 " + std::to_string(nJoints) + "，电机轴 "
                + std::to_string(nMotors) + "，chat " + std::to_string(model.chat.rows)
                + "×" + std::to_string(model.chat.cols)
                + "，ratios " + std::to_string(model.ratios.size())
                + "）——R1 要求对角方阵且逐轴视图齐备",
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
    // 语义属 R2，R1 阶段明确列为不适用）。
    for (std::size_t r = 0; r < model.chat.rows; ++r) {
        for (std::size_t c = 0; c < model.chat.cols; ++c) {
            if (r != c && model.chat(r, c) != 0.0) {
                result.diagnostics.push_back(makeDtRecord(
                    kDtMatrixNondiagonalLocked, std::nullopt, std::nullopt,
                    "drivetrain 映射矩阵形态检查（行 " + std::to_string(r)
                        + "，列 " + std::to_string(c) + "）",
                    "归一化矩阵含非零非对角元素（交叉耦合属 R2/MDL-21——R1 "
                    "不对角化绕过、不静默拆成独立轴）",
                    "在 R1 能力下改用对角传动模型；耦合链场景随 R2 阶段"
                    "（WP-18-T05）评估"));
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

    // ---- 全部检查通过（对角良态模型——R1 接受）。
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
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < m; ++k) {
            // 理想虚功对偶（对角）：τ_m,j ＝ c_j·τ_joint,j（§8.1——同一
            // 表达式路径下映射核心自产值与此位等；独立供给值的对照供黄金
            // 数据集与诊断定位使用）。精确位等比较（无阈值——§8.4）。
            const double expected = model.ratios[k].c * series.samples[i].tauJoint;
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
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < m; ++k) {
            // 理想机械功率一致性（§8.2 口径②——P_motor_ideal＝τ_m·θ̇＝
            // τ_j·C·θ̇＝τ_j·q̇＝P_joint，逐样本逐轴恒等；逐元素断言不以
            // 总和替代——防正负抵消，附录 D C4）。
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
    }
    return findings;
}

// =====================================================================
// 反射惯量与惯量比（§9.2/§9.5——数值事实，无阈值判定）
// =====================================================================

ReflectedInertiaResult DriveTrainMappingCore::evaluate(
    const DriveTrainModel& model, const std::vector<LoadInertiaEntry>& load) const
{
    // 矩阵/轴表防御面（与 §6.3 同码——直接构造模型绕过工厂时的第二道
    // 防线；阻断面全表检查复用同一实现路径）。
    const CouplingValidationResult gate = validate(model, StageCapability::R1Capability);
    if (!gate.accepted) {
        throwContract(gate.diagnostics.front().code,
                      "反射惯量评估前置矩阵校验未通过（" + gate.diagnostics.front().context
                          + "：" + gate.diagnostics.front().cause + "）");
    }

    ReflectedInertiaResult result;
    result.axes.reserve(model.motorAxes.size());
    for (std::size_t k = 0; k < model.motorAxes.size(); ++k) {
        ReflectedInertiaAxis axis;
        axis.jointIndex = model.motorAxes[k].jointIndex;
        const double c = model.ratios[k].c;
        // 转子条目缺失→反射惯量不可得（§10.7——数据类降级）：J_reflected
        // 保持 0，由 evaluate 主管线以 missingItems 标注（本步骤接口不携
        // 带清单）。转子条目值非法在构造期已被拒（DT-INERTIA-INVALID），
        // 此处按缺失同轨处理保持函数总有界。
        if (k < model.rotor.size()) {
            // 对角反射惯量（关节轴系）：J_reflected ＝ J_rotor/c²——即
            // DYN-04 记法 J·i²（i＝1/c 换算，卡 §5.3/§9.2）。单位 kg·m²。
            axis.jReflectedJointSide = model.rotor[k].rotorInertia / (c * c);
            // 惯量比（§9.5 对角式）：J_load@motor / J_rotor ＝ c²·J_load@joint
            // / J_rotor（无量纲；负载折算惯量缺失→不适用——显式 optional；
            // 阈值判定归 selection——P-DT-2，本卡只输出数值）。
            if (k < load.size()) {
                axis.inertiaRatio = (c * c) * load[k].loadInertiaJointSide
                                    / model.rotor[k].rotorInertia;
            }
        }
        result.axes.push_back(axis);
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
// 主管线：单工况完整映射评估（§13.1）
// =====================================================================

DriveTrainMappingOutput DriveTrainMappingCore::evaluate(const DriveTrainModel& model,
                                                        const JointSeriesView& series,
                                                        ICancellation* ctx)
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

    // ---- 第一步：阻断面全表检查（§6.3——顺序执行、首个命中即阻止；
    // 调用方错误 fail-fast，不返回半结果）。
    const CouplingValidationResult gate = validate(model, StageCapability::R1Capability);
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

    // ---- 第三步：逐样本逐轴 R1 对角映射（§6.2 全列——封闭代数运算）。
    const std::size_t m = model.motorAxes.size();
    std::vector<MotorSeries> motorSeries(m);
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

    // 时间非单调（数据类）：统计降级诊断在 summarize 内给——这里补整体
    // 完整性标记。
    if (!timeMonotonic) {
        missingItems.push_back("monotonic-time");
    }

    // ---- 第五步：反射惯量（§9——数值事实）。负载折算惯量不属于序列输入
    // （P-DT-4——组装方值传递，随结果装配注入惯量比）；本管线无负载惯量
    // 输入时惯量比＝不适用（显式标记，不伪造）。
    const ReflectedInertiaResult inertia = evaluate(model, {});

    // ---- 第六步：映射自检（§8.4 ②——理想部分与 c·τ_j 位等：同一表达式
    // 路径构造必然通过；此检查作为内部不变量守护，未来实现演进时失同步
    // 即暴露）。
    for (std::size_t k = 0; k < m; ++k) {
        const double c = model.ratios[k].c;
        for (std::size_t i = 0; i < motorSeries[k].samples.size(); ++i) {
            const MotorDriveSample& s = motorSeries[k].samples[i];
            const double expected = c * series.samples[i].tauJoint;
            if (!(s.tauIdeal == expected)) {
                // 不变量被破坏＝实现缺陷（非调用方错误也非环境错误）——
                // fail-fast 暴露而非吞错（AGENTS §3 错误处理纪律）。
                throwContract("DT-INTERNAL-INVARIANT",
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
