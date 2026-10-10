/**
 * @file   Screening.cpp
 * @brief  电机/减速器硬筛选实现（WP-19-T04）——§7 电机九维＋§8 减速器
 *         九维的逐维独立判定、全量原因保留、稳定排序与取消截断。
 *
 * 设计依据：
 *   - units/selection.md §7.1（电机筛选流程——逐能力维度判定清单）、
 *     §7.2（筛选纪律——逐维独立不短路；筛选条件进切片身份；缺数据标
 *     数据不足）、§8.1（减速器筛选流程）、§8.2（筛选纪律——ω_m＝ω_j/c
 *     是唯一允许的自算映射量）、§10.2（短路边界——仅致命输入错误
 *     fail-fast；候选能力筛选不短路）、§10.4（稳定排序）、§14.4（接口
 *     契约）、§6.4（固定额定值口径与缺失曲线——不用额定值伪造曲线，
 *     缺失且无固定值依据→数据不足）
 *   - 需求 SEL-03/SEL-04（筛选维度全表）、SEL-06/ERR-01（逐项淘汰原因
 *     含实际值/阈值/单位/来源）、NFR-COR-01/02（确定性——同输入恒同
 *     输出、不依赖遍历序）、NFR-COR-03（非有限值不静默通过）
 *   - 任务契约 tasks/foundation/WP-19-T04.json（acceptance 1：SEL-03/04
 *     全维度覆盖；acceptance 2：黄金表——每个淘汰项含实际值和阈值）
 *
 * 判定公式的落位登记（单元卡 §19.3 T04 落位细化 ③~⑨，review 对照面）：
 *   ③ mounting-incompatible 为电机/减速器共用安装兼容 token（词表分组
 *     读法——减速器组 token 均带 gearbox- 前缀而本 token 无，语义通用）；
 *   ④ 温度降额 v1 档位公式：超出档数 n＝ceil(max(0, T_env−T_ref))（档距
 *     ＝1 档位单位），折减系数 f＝factorPerRef^n（f<1 时对连续/峰值转矩
 *     复判——折减后能力＝目录值×f）；
 *   ⑤ 功率维度能力口径：条目声明 speed→power 能力曲线时以工作点转速
 *     查询曲线（曲线口径优先）；无曲线时退固定额定值口径（目录
 *     rated_power_w——§6.4 显式来源，不伪造曲线点）；曲线查询拒绝＝
 *     数据不足（分轨，不判淘汰）；
 *   ⑥ 外载荷力臂核算：F_allow＝F_rated×L_rated/L_actual（仅当
 *     L_actual＞L_rated＞0 时折减；作用点未标注 L_rated＝0 不折减）；
 *     轴向力直接比较；
 *   ⑦ verdict 汇总：reasons 非空→Rejected；否则 gaps 非空→
 *     DataInsufficient；否则 Feasible；
 *   ⑧ 条件侧缺失＝维度不适用（跳过）vs 目录/工作点侧缺失＝DataGap 的
 *     分界；电压匹配经 core closeWithin（附录 D C4，相对容差由筛选条件
 *     给出）；安全系数 ≥1 且有限（调用方错误 fail-fast）；速比范围
 *     闭区间；
 *   ⑨ AxisWorkpointFacts 为 P-SEL-1 提议契约的 v1 承载（dynamics 卡
 *     未产出——R-SEL-1）。
 *
 * 移动关节范围外阻断的落位登记（WP-19-T08——单元卡 §19.3 T08 落位
 * 细化，review 对照面）：
 *   ① AxisWorkpointFacts.jointKind（JointKind 枚举，默认 Revolute）为
 *     轴关节类型的值传递承载（§17.1 modeling/runtime→selection 交接行；
 *     权威判定与链型支持矩阵归 modeling MDL-12——selection 只消费声明）；
 *   ② Prismatic 轴在候选×轴遍历内先于全部维度判定输出"范围外"记录
 *     （verdict=DataInsufficient、reasons 恒空、gaps 恰一条
 *     axis-out-of-scope 缺口〔diagCode=SEL-INPUT-AXIS-OUT-OF-SCOPE〕）；
 *     记录数不变量（候选×轴）保持——截断感知计数不受影响；
 *   ③ 同轴多条 facts 的 jointKind 矛盾＝调用方契约违约 fail-fast
 *     （轴类型是轴级属性——既旋转又移动属物理矛盾，§10.2 校验边界）。
 *
 * 线程安全：全部为无状态纯函数（可重入——卡 §14.10）。
 */

#include <sdurws/ird/selection/Screening.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/selection/Curve.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>

namespace sdurws::ird::selection {

namespace {

// =====================================================================
// 内部工具（全部文件局部——不外溢，R-2 私有实现纪律）
// =====================================================================

/// 有限性校验（NFR-COR-03：非有限数不静默通过——调用方契约违约 fail-fast）。
/// @param value  [in] 待校验数值（任意量纲）
/// @param what   [in] 字段名（异常消息定位用——人读上下文）
/// @throws std::invalid_argument value 为 NaN 或 ±Inf
void requireFinite(double value, const char* what)
{
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string("selection/screening: 非有限输入（")
                                    + what + "）——调用方契约违约（NFR-COR-03 fail-fast）");
    }
}

/// 能力不足判定（阈值比较的唯一口径）。
///
/// 语义：actual 超出限值 limit 且超出量大于该量纲的附录 D C7 绝对容差
/// 时才判"不足"——边界上"刚好满足"（差值在浮点噪声级）不误淘汰
/// （CapabilityCurveTest 头注口径：附录 D 容差的筛选层消费点在本函数）。
/// 容差来源＝core::runtimeAbsoluteTolerance（C7 单点转写：torque 1e-9、
/// 角速度 1e-9、angle/dimensionless 1e-12）；C7 未声明默认的量纲（功率/
/// 电压/时间/力等）返回 nullopt→容差 0＝精确比较（不得引入产品侧相对
/// 校验——C7 纪律）。
///
/// @param actual [in] 工作点侧值（SI 域）
/// @param limit  [in] 能力限值（SI 域；"≤ limit 为满足"语义）
/// @param kind   [in] 比较量纲（容差查表键）
/// @return true＝能力不足（淘汰侧）；false＝满足或容差内
bool exceedsLimit(double actual, double limit, core::QuantityKind kind)
{
    const double tol = core::runtimeAbsoluteTolerance(kind).value_or(0.0);
    return actual > limit + tol;
}

/// 反向不足判定（要求值下限语义：目录值 actual 低于要求 limit 判不足——
/// 效率/寿命等"目录 ≥ 要求"维度；容差口径同 exceedsLimit）。
bool belowLimit(double actual, double limit, core::QuantityKind kind)
{
    const double tol = core::runtimeAbsoluteTolerance(kind).value_or(0.0);
    return actual < limit - tol;
}

/// 淘汰原因构造（IRejectionReasonProvider 的 T04 内部前置——独立供给接口
/// 随 WP-19-T06 落位，本函数只服务筛选器内部，签名私有）。
///
/// 全部字段一次填齐（ERR-01 比较型四要素：actual/required/unit/
/// thresholdSource；文本类比较走 actualText/requiredText，unit 留空）；
/// inputSliceId/mappingId 由调用方按 T04 直调路径统一置全零（诚实标记
 /// ——无 evidence 切片/映射身份可携带），diagRef 恒 nullopt（SEL-* 稳定
/// 码映射随 WP-19-T06 注册——DiagCodes.hpp 头注口径，不预建）。
RejectionReason makeReason(ReasonToken token, const ModelId& modelId,
                           const core::ObjectId& axisId, const CaseId& caseId,
                           double atTime, const std::string& segmentId,
                           double actual, double required, std::string unit,
                           std::string thresholdSource, const CatalogIdentity& catalog)
{
    RejectionReason r;
    r.token = token;
    r.candidateModelId = modelId;
    r.axisId = axisId;
    r.caseId = caseId;
    r.atTime = atTime;
    r.segmentId = segmentId;
    r.actual = actual;
    r.required = required;
    r.unit = std::move(unit);
    r.thresholdSource = std::move(thresholdSource);
    r.catalog = catalog;
    return r;
}

/// 数据缺口构造（DataGap 的统一入口——维度名/说明/工况定位）。
DataGap makeGap(const std::string& dimension, const std::string& detail,
                const core::ObjectId& axisId, const CaseId& caseId,
                const std::string& diagCode = {})
{
    DataGap g;
    g.dimension = dimension;
    g.detail = detail;
    g.axisId = axisId;
    g.caseId = caseId;
    g.diagCode = diagCode;
    return g;
}

/// 淘汰原因稳定排序（§10.4：token 词表序 → 工况 ID → 时刻；stable_sort
/// 保留同键的产生序——子维度固定执行序即同 token 多条的次序）。
void sortReasons(std::vector<RejectionReason>& reasons)
{
    std::stable_sort(reasons.begin(), reasons.end(),
                     [](const RejectionReason& a, const RejectionReason& b) {
                         if (a.token != b.token) {
                             return a.token < b.token;  // 枚举序＝词表序（头文件契约）
                         }
                         if (a.caseId != b.caseId) {
                             return a.caseId < b.caseId;
                         }
                         return a.atTime < b.atTime;
                     });
}

/// verdict 汇总（落位细化 ⑦）：淘汰优先——有硬原因即淘汰；否则有缺口
/// 即数据不足；全通过才可行。三态收敛保证"不默认通过"（§7.2）。
VerdictKind summarize(const std::vector<RejectionReason>& reasons,
                      const std::vector<DataGap>& gaps)
{
    if (!reasons.empty()) {
        return VerdictKind::Rejected;
    }
    if (!gaps.empty()) {
        return VerdictKind::DataInsufficient;
    }
    return VerdictKind::Feasible;
}

/// 温度降额折减系数（落位细化 ④ 的公式实现）。
///
/// v1 目录模板以无量纲档位承载温度降额（T03 落位细化 ③——°C 未入 core
/// 单位词表）：refTemp 为参考档位值（°C 语义）、factorPerRef 为每档能力
/// 系数（∈(0,1]，导入期范围校验保证）。环境温度不高于参考档位时不折减
/// （f＝1）；超出部分按每 1 档位单位一档、逐档乘 factorPerRef（向上取整
/// ——半档按一档保守处理：折减只会更严，不会把能力放大）。
///
/// @param thermal [in] 目录温度降额特性（范围合法性由导入校验保证）
/// @param ambientTemp [in] 筛选条件环境温度（档位值，°C 语义；已校验有限）
/// @return 能力折减系数，无量纲，∈(0,1]
double deratingFactor(const ThermalDerating& thermal, double ambientTemp)
{
    const double over = ambientTemp - thermal.refTemp; // 超出参考档位的量，档位单位
    if (over <= 0.0) {
        return 1.0;  // 环境不高于参考档位——能力不折减
    }
    // 超出档数（半档保守向上取整）；factorPerRef ∈ (0,1]，指数增大只会
    // 让系数单调趋 0（double 下溢为 0，无未定义行为——极限降额语义成立）。
    const int steps = static_cast<int>(std::ceil(over));
    return std::pow(thermal.factorPerRef, steps);
}

// =====================================================================
// 输入校验（§10.2 短路边界——致命输入错误在筛选开始前整批拒绝）
// =====================================================================

/// 筛选条件合法性（调用方契约：全部数值有限＋语义范围）。
/// @throws std::invalid_argument 任一条件非有限/安全系数＜1/最低效率
///         ∉(0,1]/速比范围非法
void validateCriteria(const ScreeningCriteria& c)
{
    requireFinite(c.safetyFactor, "safetyFactor");
    if (c.safetyFactor < 1.0) {
        // 安全系数语义是"对工作点加严复核"——<1 的要求值会把工作点缩小，
        // 不是安全系数语义（调用方错误，fail-fast 而非静默按 1 处理）。
        throw std::invalid_argument(
            "selection/screening: safetyFactor < 1.0——安全系数须 ≥1（调用方契约违约）");
    }
    requireFinite(c.voltageRelativeTolerance, "voltageRelativeTolerance");
    if (c.voltageRelativeTolerance < 0.0) {
        throw std::invalid_argument(
            "selection/screening: voltageRelativeTolerance < 0——相对容差须 ≥0（调用方契约违约）");
    }
    if (c.requiredVoltage) {
        requireFinite(*c.requiredVoltage, "requiredVoltage");
    }
    if (c.ambientTemp) {
        requireFinite(*c.ambientTemp, "ambientTemp");
    }
    if (c.maxBacklash) {
        requireFinite(*c.maxBacklash, "maxBacklash");
    }
    if (c.requiredLife) {
        requireFinite(*c.requiredLife, "requiredLife");
    }
    if (c.minEfficiency) {
        requireFinite(*c.minEfficiency, "minEfficiency");
        if (*c.minEfficiency <= 0.0 || *c.minEfficiency > 1.0) {
            // 效率物理域 (0,1]（卡 §4.4 单位表）——域外要求值无工程意义。
            throw std::invalid_argument(
                "selection/screening: minEfficiency ∉ (0,1]——效率要求须在 (0,1] 内（调用方契约违约）");
        }
    }
    if (c.ratioRange) {
        requireFinite(c.ratioRange->minRatio, "ratioRange.minRatio");
        requireFinite(c.ratioRange->maxRatio, "ratioRange.maxRatio");
        if (c.ratioRange->minRatio <= 0.0 || c.ratioRange->minRatio > c.ratioRange->maxRatio) {
            // 速比为正（卡 §4.1——目录速比 >0），允许范围下界 ≤0 或上下
            // 倒置都是条件构造错误。
            throw std::invalid_argument(
                "selection/screening: ratioRange 非法——须 0 < minRatio ≤ maxRatio（调用方契约违约）");
        }
    }
    if (!c.requiredDutyClass.empty()) {
        // 工作制为词表文本（无数值约束）——非空即启用维度，无额外校验。
    }
}

/// 工作点事实合法性（调用方契约：present 的数值一律有限）。
/// 另校验同轴 jointKind 一致性（WP-19-T08）：关节类型是轴级属性——
/// 同一根轴既声明旋转又声明移动属物理矛盾（modeling MDL-12 链型判定的
/// 上游数据被破坏），属调用方契约违约，在校验边界 fail-fast（§10.2
/// 短路边界——致命输入错误整批拒绝），静默取首现会掩盖上游缺陷。
/// @throws std::invalid_argument 任一 present 字段非有限／同轴 jointKind 矛盾
void validateFacts(const std::vector<AxisWorkpointFacts>& facts)
{
    // 同轴 jointKind 一致性（O(n²) 逐对核对——facts 规模为轴×工况的
    // 小集合，O(n²) 可接受且保持输入序无关的判定语义）。
    for (std::size_t i = 0; i < facts.size(); ++i) {
        for (std::size_t j = i + 1; j < facts.size(); ++j) {
            if (facts[i].jointId == facts[j].jointId
                && facts[i].jointKind != facts[j].jointKind) {
                throw std::invalid_argument(
                    "selection/screening: 同轴 jointKind 矛盾（轴 "
                    + facts[i].jointId.toCanonical()
                    + " 的多条事实声明了不同关节类型——轴类型是轴级属性，"
                      "调用方契约违约）");
            }
        }
    }
    for (const AxisWorkpointFacts& f : facts) {
        if (f.jointTorqueRms) { requireFinite(*f.jointTorqueRms, "jointTorqueRms"); }
        if (f.jointTorquePeak) { requireFinite(*f.jointTorquePeak, "jointTorquePeak"); }
        if (f.jointSpeedPeak) { requireFinite(*f.jointSpeedPeak, "jointSpeedPeak"); }
        if (f.motorTorqueRms) { requireFinite(*f.motorTorqueRms, "motorTorqueRms"); }
        if (f.motorTorquePeak) { requireFinite(*f.motorTorquePeak, "motorTorquePeak"); }
        if (f.motorSpeedPeak) { requireFinite(*f.motorSpeedPeak, "motorSpeedPeak"); }
        if (f.motorSpeedRms) { requireFinite(*f.motorSpeedRms, "motorSpeedRms"); }
        if (f.motorPowerPeak) { requireFinite(*f.motorPowerPeak, "motorPowerPeak"); }
        if (f.motorPowerRms) { requireFinite(*f.motorPowerRms, "motorPowerRms"); }
        if (f.peakDuration) { requireFinite(*f.peakDuration, "peakDuration"); }
        if (f.requiredHoldingTorque) {
            requireFinite(*f.requiredHoldingTorque, "requiredHoldingTorque");
        }
        if (f.externalLoad) {
            requireFinite(f.externalLoad->radial, "externalLoad.radial");
            requireFinite(f.externalLoad->axial, "externalLoad.axial");
            requireFinite(f.externalLoad->distance, "externalLoad.distance");
        }
        requireFinite(f.atTime, "atTime");
    }
}

// =====================================================================
// 候选×轴遍历框架（两筛选共用——遍历序/取消截断/记录键的唯一实现）
// =====================================================================

/// 唯一轴收集（facts 首现序——确定性，不排序：轴的身份序由调用方输入序
/// 决定，与哈希表遍历无关，NFR-COR-02"不依赖哈希表遍历顺序"）。
std::vector<core::ObjectId> uniqueAxes(const std::vector<AxisWorkpointFacts>& facts)
{
    std::vector<core::ObjectId> axes;
    for (const AxisWorkpointFacts& f : facts) {
        const bool seen = std::any_of(axes.begin(), axes.end(),
                                      [&](const core::ObjectId& id) {
                                          return id == f.jointId;
                                      });
        if (!seen) {
            axes.push_back(f.jointId);
        }
    }
    return axes;
}

/// 取消查询（批次边界——候选条目边界；观测到取消返回 false 通知调用方
/// 停止，已完成记录保留——落位细化 T04 ④ 截断语义）。
bool cancellationRequested(const evidence::IEvaluationContext* ctx)
{
    return ctx != nullptr && ctx->cancellationRequested();
}

// =====================================================================
// 移动关节范围外阻断（WP-19-T08——SEL-09/卡 §2.2 R1 纪律/D-SEL-15）
// =====================================================================

/// 轴关节类型查询（同轴一致性已由 validateFacts 保证——取首条 facts
/// 的声明；axes 列表来自同一 facts 的唯一轴收集，找不到属内部不变量
/// 破坏，防御分支按 Revolute 处理不会放大错误——调用点在此之前已由
/// validateFacts/uniqueAxes 保证轴必在 facts 中）。
JointKind axisJointKindOf(const std::vector<AxisWorkpointFacts>& facts,
                          const core::ObjectId& axis)
{
    for (const AxisWorkpointFacts& f : facts) {
        if (f.jointId == axis) {
            return f.jointKind;
        }
    }
    return JointKind::Revolute;  // 防御分支（正常路径不可达）
}

/// 范围外记录构造（移动关节轴的逐候选记录——每候选×该轴恰一条，保持
/// "记录数＝候选数×轴数"的截断感知不变量；记录内不执行任何 §7/§8
/// 旋转传动维度——reasons 恒空、gaps 恰一条范围外缺口，verdict 由
/// 汇总规则收敛为 DataInsufficient：范围外是数据/边界类事实，不是候选
 /// 能力淘汰，不升级整机不可行——卡 §2.2/D-SEL-15）。
FeasibilityRecord makeAxisOutOfScopeRecord(const ModelId& modelId,
                                           const core::ObjectId& axis,
                                           const CatalogIdentity& catalog,
                                           DeviceKind kind)
{
    FeasibilityRecord rec;
    rec.id = modelId + "|" + axis.toCanonical();  // 记录键口径同正常路径（T04 ①）
    rec.deviceKind = kind;
    rec.candidateModelId = modelId;
    rec.axisId = axis;
    rec.catalog = catalog;
    // inputSliceId/mappingId 保持全零（T04 直调路径诚实标记——同正常路径）。
    rec.gaps.push_back(makeAxisOutOfScopeGap(axis, std::string(kSelInputAxisOutOfScope)));
    rec.verdict = summarize(rec.reasons, rec.gaps);  // ⇒ DataInsufficient
    return rec;
}

}  // namespace

// =====================================================================
// 词表文本映射（§10.3 唯一映射点）
// =====================================================================

std::string_view reasonTokenText(ReasonToken token)
{
    switch (token) {
        case ReasonToken::TorqueContinuousInsufficient: return "torque-continuous-insufficient";
        case ReasonToken::TorquePeakInsufficient:       return "torque-peak-insufficient";
        case ReasonToken::SpeedInsufficient:            return "speed-insufficient";
        case ReasonToken::PowerInsufficient:            return "power-insufficient";
        case ReasonToken::OverloadTimeInsufficient:     return "overload-time-insufficient";
        case ReasonToken::DutyMismatch:                 return "duty-mismatch";
        case ReasonToken::VoltageMismatch:              return "voltage-mismatch";
        case ReasonToken::ThermalDeratingInsufficient:  return "thermal-derating-insufficient";
        case ReasonToken::BrakeInsufficient:            return "brake-insufficient";
        case ReasonToken::HoldingInsufficient:          return "holding-insufficient";
        case ReasonToken::SafetyFactorInsufficient:     return "safety-factor-insufficient";
        case ReasonToken::GearboxRatedTorqueInsufficient: return "gearbox-rated-torque-insufficient";
        case ReasonToken::GearboxPeakTorqueInsufficient:  return "gearbox-peak-torque-insufficient";
        case ReasonToken::InputSpeedExceeded:           return "input-speed-exceeded";
        case ReasonToken::RatioMismatch:                return "ratio-mismatch";
        case ReasonToken::EfficiencyInsufficient:       return "efficiency-insufficient";
        case ReasonToken::BacklashExceeded:             return "backlash-exceeded";
        case ReasonToken::LifeInsufficient:             return "life-insufficient";
        case ReasonToken::MountingIncompatible:         return "mounting-incompatible";
        case ReasonToken::ExternalLoadExceeded:         return "external-load-exceeded";
        case ReasonToken::ComboIncompatible:            return "combo-incompatible";
        case ReasonToken::AxisMappingIncomplete:        return "axis-mapping-incomplete";
        case ReasonToken::InertiaRatioPolicyUnsettled:  return "inertia-ratio-policy-unsettled";
        case ReasonToken::IdentityMismatch:             return "identity-mismatch";
        case ReasonToken::CatalogVersionIncompatible:   return "catalog-version-incompatible";
        case ReasonToken::MappingVersionIncompatible:   return "mapping-version-incompatible";
        case ReasonToken::DynamicsMissing:              return "dynamics-missing";
        case ReasonToken::DrivetrainMissing:            return "drivetrain-missing";
        case ReasonToken::CaseCoverageGap:              return "case-coverage-gap";
        case ReasonToken::InputInvalid:                 return "input-invalid";
        case ReasonToken::ComputeFailed:                return "compute-failed";
        case ReasonToken::AxisOutOfScope:               return "axis-out-of-scope";
        case ReasonToken::R2CapabilityDisabled:         return "r2-capability-disabled";
        case ReasonToken::UserPreferenceFiltered:       return "user-preference-filtered";
        // ---- 直线传动能力组（T12 批——WP-19-T12/§17.2；表尾追加序＝
        //      枚举追加序；词表文本 kebab 小写与既有组同款词形）----
        case ReasonToken::LinearForceContinuousInsufficient:
            return "linear-force-continuous-insufficient";
        case ReasonToken::LinearForcePeakInsufficient:  return "linear-force-peak-insufficient";
        case ReasonToken::LinearSpeedInsufficient:      return "linear-speed-insufficient";
        case ReasonToken::LinearPowerInsufficient:      return "linear-power-insufficient";
    }
    // 枚举外整数值（防御分支——正常路径不可达；词表封闭性由测试钉住）。
    return "unknown-reason-token";
}

// =====================================================================
// 值相等（头文件声明的 operator== 定义落位）
// =====================================================================

bool RejectionReason::operator==(const RejectionReason& o) const
{
    return token == o.token && candidateModelId == o.candidateModelId
        && axisId == o.axisId && caseId == o.caseId && atTime == o.atTime
        && segmentId == o.segmentId && actual == o.actual && required == o.required
        && unit == o.unit && thresholdSource == o.thresholdSource
        && actualText == o.actualText && requiredText == o.requiredText
        && catalog == o.catalog && inputSliceId == o.inputSliceId
        && mappingId == o.mappingId && suggestion == o.suggestion
        && diagRef == o.diagRef;
}

bool FeasibilityRecord::operator==(const FeasibilityRecord& o) const
{
    return id == o.id && deviceKind == o.deviceKind
        && candidateModelId == o.candidateModelId && axisId == o.axisId
        && verdict == o.verdict && catalog == o.catalog
        && inputSliceId == o.inputSliceId && mappingId == o.mappingId
        && reasons == o.reasons && gaps == o.gaps;
}

bool ScreeningCriteria::operator==(const ScreeningCriteria& o) const
{
    return safetyFactor == o.safetyFactor && requiredDutyClass == o.requiredDutyClass
        && requiredVoltage == o.requiredVoltage
        && voltageRelativeTolerance == o.voltageRelativeTolerance
        && ambientTemp == o.ambientTemp && maxBacklash == o.maxBacklash
        && requiredLife == o.requiredLife && minEfficiency == o.minEfficiency
        && ratioRange == o.ratioRange;
}

bool AxisWorkpointFacts::operator==(const AxisWorkpointFacts& o) const
{
    return jointId == o.jointId && caseId == o.caseId && jointKind == o.jointKind
        && jointTorqueRms == o.jointTorqueRms && jointTorquePeak == o.jointTorquePeak
        && jointSpeedPeak == o.jointSpeedPeak && motorTorqueRms == o.motorTorqueRms
        && motorTorquePeak == o.motorTorquePeak && motorSpeedPeak == o.motorSpeedPeak
        && motorSpeedRms == o.motorSpeedRms && motorPowerPeak == o.motorPowerPeak
        && motorPowerRms == o.motorPowerRms && peakDuration == o.peakDuration
        && requiredHoldingTorque == o.requiredHoldingTorque
        && externalLoad == o.externalLoad && mountRequirement == o.mountRequirement
        && atTime == o.atTime && segmentId == o.segmentId;
}

namespace {

// =====================================================================
// 电机维度判定（§7.1 逐能力维度——每工况一次全维度执行，不短路）
// =====================================================================

/// 安装接口兼容（§7.1 ②——兼容性判定，非能力值）。
/// 电机仅法兰/轴伸两个子项（安装方向子项为减速器专用——§8.1 ②）。
void screenMotorMounting(const MotorCatalogEntry& motor, const AxisWorkpointFacts& f,
                         const CatalogIdentity& catalog,
                         std::vector<RejectionReason>& reasons,
                         std::vector<DataGap>& gaps)
{
    const JointMountRequirement& req = *f.mountRequirement;
    // 子项一：法兰接口（要求非空才判——空＝不限）。
    if (!req.flangeKind.empty()) {
        if (motor.mounting.flangeKind.empty()) {
            // 条目未提供法兰接口而关节有要求——数据不足（不默认兼容）。
            gaps.push_back(makeGap("mounting-flange",
                                   "关节要求法兰接口词表值，目录条目未提供 mounting_flange_kind",
                                   f.jointId, f.caseId));
        } else if (motor.mounting.flangeKind != req.flangeKind) {
            RejectionReason r = makeReason(
                ReasonToken::MountingIncompatible, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, 0.0, 0.0, "",
                "关节安装关系（轴侧事实 mountRequirement.flangeKind）", catalog);
            // 文本类比较：数值侧无意义（0），词表值入 actualText/requiredText。
            r.actualText = motor.mounting.flangeKind;
            r.requiredText = req.flangeKind;
            r.suggestion = "更换法兰接口匹配的电机型号或调整关节安装关系";
            reasons.push_back(std::move(r));
        }
    }
    // 子项二：轴伸接口（同口径）。
    if (!req.shaftKind.empty()) {
        if (motor.mounting.shaftKind.empty()) {
            gaps.push_back(makeGap("mounting-shaft",
                                   "关节要求轴伸接口词表值，目录条目未提供 mounting_shaft_kind",
                                   f.jointId, f.caseId));
        } else if (motor.mounting.shaftKind != req.shaftKind) {
            RejectionReason r = makeReason(
                ReasonToken::MountingIncompatible, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, 0.0, 0.0, "",
                "关节安装关系（轴侧事实 mountRequirement.shaftKind）", catalog);
            r.actualText = motor.mounting.shaftKind;
            r.requiredText = req.shaftKind;
            r.suggestion = "更换轴伸接口匹配的电机型号或调整关节安装关系";
            reasons.push_back(std::move(r));
        }
    }
}

/// 功率能力值查询（§7.1 功率维度——落位细化 ⑤ 双口径）。
///
/// 口径一（曲线优先）：条目声明 speed→power 能力曲线时，以工作点转速
/// omega 查询曲线上限（分段线性插值——T03 冻结语义）；
/// 口径二（固定额定值）：无该类曲线引用时退目录额定功率字段（§6.4
/// "固定额定值口径"——来源显式为目录列，不伪造曲线点）。
///
/// @param motor    [in] 候选电机条目
/// @param snapshot [in] 目录快照（曲线表查源）
/// @param omega    [in] 工作点转速（查询横坐标；rad/s——无量纲曲线口径
///                 下不使用）
/// @param limit    [out] 能力值（W）
/// @param source   [out] 阈值来源描述（入淘汰原因 thresholdSource）
/// @param gap      [out] 查询拒绝时的缺口（曲线区间外等——数据不足分轨）
/// @return true＝取得能力值；false＝数据不足（gap 已填充）
bool motorPowerLimit(const MotorCatalogEntry& motor, const CatalogPackageSnapshot& snapshot,
                     double omega, double& limit, std::string& source, DataGap& gap)
{
    // 在条目曲线引用中找 speed→power 曲线（§6.4：同 quantity 组合唯一
    // ——导入期已拒绝歧义声明，此处按首条匹配即可）。
    for (const CurveRef& ref : motor.curves) {
        if (ref.xQuantity != kQuantitySpeed || ref.yQuantity != kQuantityPower) {
            continue;  // 非功率曲线（如转矩-转速曲线）——功率维度不消费
        }
        const PerformanceCurve* curve = nullptr;
        for (const PerformanceCurve& c : snapshot.curves) {
            if (c.curveId == ref.curveId) {
                curve = &c;
                break;
            }
        }
        if (curve == nullptr) {
            // 防御分支：导入期 REF-DANGLING 已拒绝悬空引用；直接构造的
            // 快照可能绕过导入——按数据不足处理，不猜测能力值。
            // （轴/工况定位字段由调用方回填——本函数无轴上下文。）
            gap = makeGap("power-curve",
                          "条目引用的功率曲线在快照中不存在（curveId=" + ref.curveId + "）",
                          core::ObjectId{}, CaseId{}, std::string(kSelCurveExtrapolationDenied));
            return false;
        }
        LinearCurveEvaluator evaluator;
        const CurveQueryResult qr = evaluator.evaluate(*curve, omega);
        if (!qr.ok()) {
            // 曲线区间外/坏曲线＝数据不足（分轨——不是能力不足；
            // SEL-CURVE-EXTRAPOLATION-DENIED 随缺口登记，不伪装成淘汰）。
            gap = makeGap("power-curve",
                          "功率能力曲线查询被拒绝：" + qr.rejectMessage,
                          core::ObjectId{}, CaseId{}, std::string(kSelCurveExtrapolationDenied));
            return false;
        }
        limit = qr.value;
        source = "能力曲线 " + ref.curveId + "（speed→power，工作点转速查询）";
        return true;
    }
    // 无功率曲线——固定额定值口径（§6.4：来源显式＝目录额定字段）。
    limit = motor.ratedPower;
    source = "目录 rated_power_w（额定功率——固定额定值口径）";
    return true;
}

/// 电机单工况全维度判定（§7.1 ③——逐维独立执行，全部维度跑完才返回；
/// 任一维度的失败不阻断后续维度——§7.2"不因第一个失败丢失其他独立
/// 淘汰原因"）。
void screenMotorCase(const MotorCatalogEntry& motor, const AxisWorkpointFacts& f,
                     const ScreeningCriteria& criteria,
                     const CatalogPackageSnapshot& snapshot,
                     std::vector<RejectionReason>& reasons,
                     std::vector<DataGap>& gaps)
{
    const CatalogIdentity& catalog = motor.catalog;

    // ---- 维度 1：连续转矩（SEL-03）----
    // τ_rms ≤ 额定连续转矩；工作点缺失→数据缺口（不把缺动力学证据当
    // 零负载——§7.2）。
    if (!f.motorTorqueRms) {
        gaps.push_back(makeGap("continuous-torque",
                               "电机侧 RMS 转矩工作点未供给（映射事实缺失）",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*f.motorTorqueRms, motor.ratedTorque, core::QuantityKind::Torque)) {
        RejectionReason r = makeReason(
            ReasonToken::TorqueContinuousInsufficient, motor.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, *f.motorTorqueRms, motor.ratedTorque, "N*m",
            "目录 rated_torque_nm（额定连续转矩）", catalog);
        r.suggestion = "更换额定连续转矩更大的电机型号";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 2：峰值转矩 ----
    if (!f.motorTorquePeak) {
        gaps.push_back(makeGap("peak-torque", "电机侧峰值转矩工作点未供给",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*f.motorTorquePeak, motor.peakTorque, core::QuantityKind::Torque)) {
        RejectionReason r = makeReason(
            ReasonToken::TorquePeakInsufficient, motor.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, *f.motorTorquePeak, motor.peakTorque, "N*m",
            "目录 peak_torque_nm（峰值转矩）", catalog);
        r.suggestion = "更换峰值转矩更大的电机型号";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 3：转速（两个子项独立：ω_peak vs 最高转速、ω_rms vs 额定
    //      转速——任一失败独立记因，词表共用 speed-insufficient）----
    if (!f.motorSpeedPeak) {
        gaps.push_back(makeGap("speed-peak", "电机侧峰值角速度工作点未供给",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*f.motorSpeedPeak, motor.maxSpeed, core::QuantityKind::AngularVelocity)) {
        RejectionReason r = makeReason(
            ReasonToken::SpeedInsufficient, motor.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, *f.motorSpeedPeak, motor.maxSpeed, "rad/s",
            "目录 max_speed（最高转速）", catalog);
        r.suggestion = "更换最高转速更高的电机型号或增大该轴传动比";
        reasons.push_back(std::move(r));
    }
    if (!f.motorSpeedRms) {
        gaps.push_back(makeGap("speed-rms", "电机侧 RMS 角速度工作点未供给",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*f.motorSpeedRms, motor.ratedSpeed, core::QuantityKind::AngularVelocity)) {
        RejectionReason r = makeReason(
            ReasonToken::SpeedInsufficient, motor.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, *f.motorSpeedRms, motor.ratedSpeed, "rad/s",
            "目录 rated_speed（额定转速）", catalog);
        r.suggestion = "更换额定转速更高的电机型号或增大该轴传动比";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 4：功率（P_peak/P_rms 两个子项独立；能力口径见
    //      motorPowerLimit——曲线优先、额定兜底；查询拒绝＝数据缺口）----
    for (int which = 0; which < 2; ++which) {
        const std::optional<double>& p = (which == 0) ? f.motorPowerPeak : f.motorPowerRms;
        const char* gapDim = (which == 0) ? "power-peak" : "power-rms";
        const char* gapMsg = (which == 0) ? "电机侧峰值功率工作点未供给"
                                          : "电机侧 RMS 功率工作点未供给";
        if (!p) {
            gaps.push_back(makeGap(gapDim, gapMsg, f.jointId, f.caseId));
            continue;
        }
        // 能力值查询的横坐标：峰值功率对 ω_peak、RMS 功率对 ω_rms（曲线
        // 口径的查询点语义——转速未知时按额定口径仍可判定，横坐标仅被
        // 曲线口径消费）。
        const std::optional<double>& omega = (which == 0) ? f.motorSpeedPeak : f.motorSpeedRms;
        double limit = 0.0;
        std::string source;
        DataGap qgap;
        if (omega) {
            if (!motorPowerLimit(motor, snapshot, *omega, limit, source, qgap)) {
                qgap.axisId = f.jointId;
                qgap.caseId = f.caseId;
                gaps.push_back(std::move(qgap));
                continue;
            }
        } else {
            // 转速工作点缺失：曲线口径需要查询点——但目录未声明功率曲线
            // 时额定口径仍可判；有曲线而无查询点＝数据不足。
            bool hasPowerCurve = false;
            for (const CurveRef& ref : motor.curves) {
                if (ref.xQuantity == kQuantitySpeed && ref.yQuantity == kQuantityPower) {
                    hasPowerCurve = true;
                    break;
                }
            }
            if (hasPowerCurve) {
                gaps.push_back(makeGap("power-curve",
                                       "条目声明功率能力曲线但转速工作点缺失——无法按曲线查询能力",
                                       f.jointId, f.caseId));
                continue;
            }
            limit = motor.ratedPower;
            source = "目录 rated_power_w（额定功率——固定额定值口径）";
        }
        if (exceedsLimit(*p, limit, core::QuantityKind::Power)) {
            RejectionReason r = makeReason(
                ReasonToken::PowerInsufficient, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *p, limit, "W", source, catalog);
            r.suggestion = "更换额定功率更大的电机型号或复核工作点功率核算";
            reasons.push_back(std::move(r));
        }
    }

    // ---- 维度 5：过载持续时间（触发条件式维度：仅当峰值工作点进入
    //      过载区（τ_peak > 额定连续转矩）才核查时间窗——卡 §7.1
    //      "峰值段时长 ≤ 目录过载持续时间（τ_peak>额定段）"）----
    if (f.motorTorquePeak && f.motorTorqueRms
        && exceedsLimit(*f.motorTorquePeak, motor.ratedTorque, core::QuantityKind::Torque)) {
        if (!motor.overload) {
            // 工作在过载区但目录未声明过载能力——数据不足（不允许把
            // "未声明"当"无限制过载"）。
            gaps.push_back(makeGap("overload-duration",
                                   "峰值工作点超出额定连续转矩（进入过载区），但目录未声明过载能力",
                                   f.jointId, f.caseId));
        } else if (!f.peakDuration) {
            gaps.push_back(makeGap("overload-duration",
                                   "过载区工作需要峰值段时长事实（轨迹段峰值持续时间）",
                                   f.jointId, f.caseId));
        } else if (exceedsLimit(*f.peakDuration, motor.overload->duration, core::QuantityKind::Time)) {
            RejectionReason r = makeReason(
                ReasonToken::OverloadTimeInsufficient, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *f.peakDuration, motor.overload->duration, "s",
                "目录 overload_duration_s（过载持续时间）", catalog);
            r.suggestion = "缩短峰值段时长或更换过载持续时间更长的电机型号";
            reasons.push_back(std::move(r));
        }
        // overload.torque 字段在本维度不单独判定：峰值能力已由维度 2 以
        // peak_torque_nm 判定（目录 v1 中 overload_torque_nm 的独立判定
        // 语义无卡面依据——不私加维度；登记 §19.3 T04 落位细化注）。
    }

    // ---- 维度 6：工作制（条件侧缺失＝不适用——落位细化 ⑧）----
    if (!criteria.requiredDutyClass.empty() && motor.dutyClass != criteria.requiredDutyClass) {
        RejectionReason r = makeReason(
            ReasonToken::DutyMismatch, motor.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, 0.0, 0.0, "",
            "目录 duty_class（工作制词表值）", catalog);
        r.actualText = motor.dutyClass;
        r.requiredText = criteria.requiredDutyClass;
        r.suggestion = "更换工作制匹配的电机型号";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 7：电压（匹配＝附录 D C4：|V_rated−V_req| ≤ tol·|V_req|；
    //      条件启用而目录未声明额定电压→数据不足）----
    if (criteria.requiredVoltage) {
        if (!motor.ratedVoltage) {
            gaps.push_back(makeGap("voltage",
                                   "筛选条件要求电压匹配，目录条目未提供 rated_voltage_v",
                                   f.jointId, f.caseId));
        } else {
            const core::Tolerance tol{criteria.voltageRelativeTolerance, 0.0};
            if (!core::closeWithin(*motor.ratedVoltage, *criteria.requiredVoltage, tol)) {
                RejectionReason r = makeReason(
                    ReasonToken::VoltageMismatch, motor.modelId, f.jointId, f.caseId,
                    f.atTime, f.segmentId, *motor.ratedVoltage, *criteria.requiredVoltage,
                    "V", "目录 rated_voltage_v（额定电压；匹配容差＝筛选条件相对容差）", catalog);
                r.suggestion = "更换额定电压匹配的电机型号";
                reasons.push_back(std::move(r));
            }
        }
    }

    // ---- 维度 8：温度降额（落位细化 ④：折减系数 f 后对连续/峰值转矩
    //      复判——与维度 1/2 的原始判定分维并行，失败原因独立分轨；
    //      f==1（环境不高于参考档位）时无折减、无独立原因）----
    if (criteria.ambientTemp) {
        if (!motor.thermal) {
            gaps.push_back(makeGap("thermal-derating",
                                   "筛选条件启用环境温度降额，目录条目未提供温度降额特性",
                                   f.jointId, f.caseId));
        } else {
            const double factor = deratingFactor(*motor.thermal, *criteria.ambientTemp);
            if (factor < 1.0) {
                // 复判子项一：连续转矩（仅在工作点已供给时——缺失已由
                // 维度 1 记缺口，此处不重复记）。
                if (f.motorTorqueRms) {
                    const double derated = motor.ratedTorque * factor;
                    if (exceedsLimit(*f.motorTorqueRms, derated, core::QuantityKind::Torque)) {
                        RejectionReason r = makeReason(
                            ReasonToken::ThermalDeratingInsufficient, motor.modelId,
                            f.jointId, f.caseId, f.atTime, f.segmentId,
                            *f.motorTorqueRms, derated, "N*m",
                            "目录 thermal_factor_per_ref 折减后额定连续转矩（f="
                                + std::to_string(factor) + "）", catalog);
                        r.suggestion = "改善环境温度或更换温度降额特性更好的电机型号";
                        reasons.push_back(std::move(r));
                    }
                }
                // 复判子项二：峰值转矩（同口径）。
                if (f.motorTorquePeak) {
                    const double derated = motor.peakTorque * factor;
                    if (exceedsLimit(*f.motorTorquePeak, derated, core::QuantityKind::Torque)) {
                        RejectionReason r = makeReason(
                            ReasonToken::ThermalDeratingInsufficient, motor.modelId,
                            f.jointId, f.caseId, f.atTime, f.segmentId,
                            *f.motorTorquePeak, derated, "N*m",
                            "目录 thermal_factor_per_ref 折减后峰值转矩（f="
                                + std::to_string(factor) + "）", catalog);
                        r.suggestion = "改善环境温度或更换温度降额特性更好的电机型号";
                        reasons.push_back(std::move(r));
                    }
                }
            }
        }
    }

    // ---- 维度 9：制动/保持（无保持工况需求则不适用——§7.1；两个能力
    //      子项独立判定、独立记因）----
    if (f.requiredHoldingTorque) {
        if (!motor.brakeTorque) {
            gaps.push_back(makeGap("brake",
                                   "保持工况需要制动能力，目录条目未提供 brake_torque_nm",
                                   f.jointId, f.caseId));
        } else if (exceedsLimit(*f.requiredHoldingTorque, *motor.brakeTorque,
                                core::QuantityKind::Torque)) {
            RejectionReason r = makeReason(
                ReasonToken::BrakeInsufficient, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *f.requiredHoldingTorque, *motor.brakeTorque, "N*m",
                "目录 brake_torque_nm（制动能力）", catalog);
            r.suggestion = "更换制动能力更大的电机型号或外加制动器";
            reasons.push_back(std::move(r));
        }
        if (!motor.holdingTorque) {
            gaps.push_back(makeGap("holding",
                                   "保持工况需要保持能力，目录条目未提供 holding_torque_nm",
                                   f.jointId, f.caseId));
        } else if (exceedsLimit(*f.requiredHoldingTorque, *motor.holdingTorque,
                                core::QuantityKind::Torque)) {
            RejectionReason r = makeReason(
                ReasonToken::HoldingInsufficient, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *f.requiredHoldingTorque, *motor.holdingTorque, "N*m",
                "目录 holding_torque_nm（保持能力）", catalog);
            r.suggestion = "更换保持能力更大的电机型号";
            reasons.push_back(std::move(r));
        }
    }

    // ---- 维度 10：安全系数（criteria.safetyFactor > 1 时启用——对已
    //      供给的 τ/ω/P 逐项 ×SF 后与能力值复判；与原始判定分维并行，
    //      独立 token。量缺失时该子项跳过（维度 1~4 已记缺口，不重复）；
    //      功率曲线口径在 SF 复判中查询拒绝时同样跳过（缺口已记））----
    if (criteria.safetyFactor > 1.0) {
        const std::string sfSource = "安全系数条件（SF=" + std::to_string(criteria.safetyFactor)
                                   + "）复判——";
        if (f.motorTorqueRms
            && exceedsLimit(*f.motorTorqueRms * criteria.safetyFactor, motor.ratedTorque,
                            core::QuantityKind::Torque)) {
            RejectionReason r = makeReason(
                ReasonToken::SafetyFactorInsufficient, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *f.motorTorqueRms * criteria.safetyFactor,
                motor.ratedTorque, "N*m", sfSource + "目录 rated_torque_nm", catalog);
            r.suggestion = "更换额定连续转矩更大的电机型号（安全系数加严后不足）";
            reasons.push_back(std::move(r));
        }
        if (f.motorTorquePeak
            && exceedsLimit(*f.motorTorquePeak * criteria.safetyFactor, motor.peakTorque,
                            core::QuantityKind::Torque)) {
            RejectionReason r = makeReason(
                ReasonToken::SafetyFactorInsufficient, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *f.motorTorquePeak * criteria.safetyFactor,
                motor.peakTorque, "N*m", sfSource + "目录 peak_torque_nm", catalog);
            r.suggestion = "更换峰值转矩更大的电机型号（安全系数加严后不足）";
            reasons.push_back(std::move(r));
        }
        if (f.motorSpeedPeak
            && exceedsLimit(*f.motorSpeedPeak * criteria.safetyFactor, motor.maxSpeed,
                            core::QuantityKind::AngularVelocity)) {
            RejectionReason r = makeReason(
                ReasonToken::SafetyFactorInsufficient, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *f.motorSpeedPeak * criteria.safetyFactor,
                motor.maxSpeed, "rad/s", sfSource + "目录 max_speed", catalog);
            r.suggestion = "更换最高转速更高的电机型号（安全系数加严后不足）";
            reasons.push_back(std::move(r));
        }
        if (f.motorSpeedRms
            && exceedsLimit(*f.motorSpeedRms * criteria.safetyFactor, motor.ratedSpeed,
                            core::QuantityKind::AngularVelocity)) {
            RejectionReason r = makeReason(
                ReasonToken::SafetyFactorInsufficient, motor.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *f.motorSpeedRms * criteria.safetyFactor,
                motor.ratedSpeed, "rad/s", sfSource + "目录 rated_speed", catalog);
            r.suggestion = "更换额定转速更高的电机型号（安全系数加严后不足）";
            reasons.push_back(std::move(r));
        }
        // 功率两子项（能力值查询与维度 4 同口径；查询拒绝→跳过不重复记）。
        for (int which = 0; which < 2; ++which) {
            const std::optional<double>& p = (which == 0) ? f.motorPowerPeak : f.motorPowerRms;
            const std::optional<double>& omega = (which == 0) ? f.motorSpeedPeak : f.motorSpeedRms;
            if (!p) {
                continue;
            }
            double limit = 0.0;
            std::string source;
            DataGap qgap;
            bool usable = false;
            if (omega) {
                usable = motorPowerLimit(motor, snapshot, *omega, limit, source, qgap);
            } else {
                bool hasPowerCurve = false;
                for (const CurveRef& ref : motor.curves) {
                    if (ref.xQuantity == kQuantitySpeed && ref.yQuantity == kQuantityPower) {
                        hasPowerCurve = true;
                        break;
                    }
                }
                if (!hasPowerCurve) {
                    limit = motor.ratedPower;
                    source = "目录 rated_power_w（额定功率——固定额定值口径）";
                    usable = true;
                }
            }
            if (usable
                && exceedsLimit(*p * criteria.safetyFactor, limit, core::QuantityKind::Power)) {
                RejectionReason r = makeReason(
                    ReasonToken::SafetyFactorInsufficient, motor.modelId, f.jointId, f.caseId,
                    f.atTime, f.segmentId, *p * criteria.safetyFactor, limit, "W",
                    sfSource + source, catalog);
                r.suggestion = "更换额定功率更大的电机型号（安全系数加严后不足）";
                reasons.push_back(std::move(r));
            }
        }
    }
}

// =====================================================================
// 减速器维度判定（§8.1 逐能力维度）
// =====================================================================

/// 减速器单工况全维度判定（§8.1 ④——逐维独立执行不短路；安装/速比
/// 等③②步维度并入同一执行序）。
void screenGearboxCase(const GearboxCatalogEntry& gb, const AxisWorkpointFacts& f,
                       const ScreeningCriteria& criteria,
                       std::vector<RejectionReason>& reasons,
                       std::vector<DataGap>& gaps)
{
    const CatalogIdentity& catalog = gb.catalog;

    // ---- 维度 1：安装接口/安装方向（§8.1 ②——落位细化 ③：共用 token）----
    if (f.mountRequirement) {
        const JointMountRequirement& req = *f.mountRequirement;
        if (!req.flangeKind.empty()) {
            if (gb.mounting.flangeKind.empty()) {
                gaps.push_back(makeGap("mounting-flange",
                                       "关节要求法兰接口词表值，目录条目未提供 mounting_flange_kind",
                                       f.jointId, f.caseId));
            } else if (gb.mounting.flangeKind != req.flangeKind) {
                RejectionReason r = makeReason(
                    ReasonToken::MountingIncompatible, gb.modelId, f.jointId, f.caseId,
                    f.atTime, f.segmentId, 0.0, 0.0, "",
                    "关节安装关系（轴侧事实 mountRequirement.flangeKind）", catalog);
                r.actualText = gb.mounting.flangeKind;
                r.requiredText = req.flangeKind;
                r.suggestion = "更换法兰接口匹配的减速器型号或调整关节安装关系";
                reasons.push_back(std::move(r));
            }
        }
        if (!req.shaftKind.empty()) {
            if (gb.mounting.shaftKind.empty()) {
                gaps.push_back(makeGap("mounting-shaft",
                                       "关节要求轴伸接口词表值，目录条目未提供 mounting_shaft_kind",
                                       f.jointId, f.caseId));
            } else if (gb.mounting.shaftKind != req.shaftKind) {
                RejectionReason r = makeReason(
                    ReasonToken::MountingIncompatible, gb.modelId, f.jointId, f.caseId,
                    f.atTime, f.segmentId, 0.0, 0.0, "",
                    "关节安装关系（轴侧事实 mountRequirement.shaftKind）", catalog);
                r.actualText = gb.mounting.shaftKind;
                r.requiredText = req.shaftKind;
                r.suggestion = "更换轴伸接口匹配的减速器型号或调整关节安装关系";
                reasons.push_back(std::move(r));
            }
        }
        if (!req.orientation.empty()) {
            // 安装方向：目录列必填（导入期 FIELD-MISSING 保证非空）——
            // 直接词表值比对，无缺失分支。
            if (gb.mountingOrientation != req.orientation) {
                RejectionReason r = makeReason(
                    ReasonToken::MountingIncompatible, gb.modelId, f.jointId, f.caseId,
                    f.atTime, f.segmentId, 0.0, 0.0, "",
                    "目录 mounting_orientation（安装方向词表值）vs 关节安装关系", catalog);
                r.actualText = gb.mountingOrientation;
                r.requiredText = req.orientation;
                r.suggestion = "更换安装方向匹配的减速器型号";
                reasons.push_back(std::move(r));
            }
        }
    }

    // ---- 维度 2：速比（§8.1 ③——候选速比 ∈ 该轴允许传动比范围，闭
    //      区间；条件未配置＝维度不适用）----
    if (criteria.ratioRange) {
        const double ratio = gb.ratio;
        if (ratio < criteria.ratioRange->minRatio
            || ratio > criteria.ratioRange->maxRatio) {
            // 范围类原因的 required 承载越界侧边界值（closest bound）。
            const bool below = ratio < criteria.ratioRange->minRatio;
            const double bound = below ? criteria.ratioRange->minRatio
                                       : criteria.ratioRange->maxRatio;
            RejectionReason r = makeReason(
                ReasonToken::RatioMismatch, gb.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, ratio, bound, "1",
                "筛选条件 ratio_range（该轴允许传动比范围，闭区间）", catalog);
            r.suggestion = "更换速比落在允许范围内的减速器型号";
            reasons.push_back(std::move(r));
        }
    }

    // ---- 维度 3：额定输出转矩（关节侧 RMS τ ≤ 额定输出转矩）----
    if (!f.jointTorqueRms) {
        gaps.push_back(makeGap("gb-rated-torque", "关节侧 RMS 转矩工作点未供给",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*f.jointTorqueRms, gb.ratedOutputTorque, core::QuantityKind::Torque)) {
        RejectionReason r = makeReason(
            ReasonToken::GearboxRatedTorqueInsufficient, gb.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, *f.jointTorqueRms, gb.ratedOutputTorque, "N*m",
            "目录 rated_output_torque_nm（额定输出转矩）", catalog);
        r.suggestion = "更换额定输出转矩更大的减速器型号";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 4：峰值输出转矩 ----
    if (!f.jointTorquePeak) {
        gaps.push_back(makeGap("gb-peak-torque", "关节侧峰值转矩工作点未供给",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*f.jointTorquePeak, gb.peakOutputTorque, core::QuantityKind::Torque)) {
        RejectionReason r = makeReason(
            ReasonToken::GearboxPeakTorqueInsufficient, gb.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, *f.jointTorquePeak, gb.peakOutputTorque, "N*m",
            "目录 peak_output_torque_nm（峰值输出转矩）", catalog);
        r.suggestion = "更换峰值输出转矩更大的减速器型号";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 5：允许输入转速（ω_m_peak ≤ 允许输入转速；落位细化：
    //      ω_m 优先取映射事实 motorSpeedPeak（"ω_m 来自映射工作点"——
    //      §8.1 ④）；映射事实未供给时以 ω_m＝ω_joint_peak/ratio 换算
    //      （§8.2 明文允许的唯一自算——候选传动参数换算，非映射实现）；
    //      两者皆缺＝数据不足）----
    std::optional<double> omegaMotor;
    std::string omegaSource;
    if (f.motorSpeedPeak) {
        omegaMotor = f.motorSpeedPeak;
        omegaSource = "映射工作点事实 motorSpeedPeak";
    } else if (f.jointSpeedPeak) {
        omegaMotor = *f.jointSpeedPeak / gb.ratio;  // 目录速比 >0（导入期保证）
        omegaSource = "关节峰值角速度 ÷ 目录速比（候选传动参数换算）";
    }
    if (!omegaMotor) {
        gaps.push_back(makeGap("gb-input-speed",
                               "输入转速核查需要映射工作点或关节峰值角速度事实",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*omegaMotor, gb.maxInputSpeed, core::QuantityKind::AngularVelocity)) {
        RejectionReason r = makeReason(
            ReasonToken::InputSpeedExceeded, gb.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, *omegaMotor, gb.maxInputSpeed, "rad/s",
            "目录 max_input_speed（允许输入转速；ω_m 来源：" + omegaSource + "）", catalog);
        r.suggestion = "更换允许输入转速更高的减速器型号或减小该轴传动比";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 6：效率（目录效率 ≥ 筛选条件最低效率；条件未配置＝不适用）----
    if (criteria.minEfficiency
        && belowLimit(gb.efficiency, *criteria.minEfficiency, core::QuantityKind::Dimensionless)) {
        RejectionReason r = makeReason(
            ReasonToken::EfficiencyInsufficient, gb.modelId, f.jointId, f.caseId,
            f.atTime, f.segmentId, gb.efficiency, *criteria.minEfficiency, "1",
            "筛选条件 min_efficiency（最低效率要求）", catalog);
        r.suggestion = "更换效率更高的减速器型号";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 7：回程间隙（目录回隙 ≤ 筛选条件上限；SI rad 域比较——
    //      v1 目录模板冻结 rad 口径，T03 落位细化 ③）----
    if (criteria.maxBacklash) {
        if (!gb.backlash) {
            gaps.push_back(makeGap("backlash",
                                   "筛选条件要求回程间隙上限，目录条目未提供 backlash",
                                   f.jointId, f.caseId));
        } else if (exceedsLimit(*gb.backlash, *criteria.maxBacklash, core::QuantityKind::Angle)) {
            RejectionReason r = makeReason(
                ReasonToken::BacklashExceeded, gb.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *gb.backlash, *criteria.maxBacklash, "rad",
                "目录 backlash（回程间隙，SI rad）vs 筛选条件 max_backlash", catalog);
            r.suggestion = "更换回程间隙更小的减速器型号";
            reasons.push_back(std::move(r));
        }
    }

    // ---- 维度 8：寿命（目录额定寿命 ≥ 筛选条件要求；v1 口径＝循环数）----
    if (criteria.requiredLife) {
        if (!gb.ratedLife) {
            gaps.push_back(makeGap("life",
                                   "筛选条件要求额定寿命，目录条目未提供 rated_life",
                                   f.jointId, f.caseId));
        } else if (belowLimit(*gb.ratedLife, *criteria.requiredLife,
                              core::QuantityKind::Dimensionless)) {
            RejectionReason r = makeReason(
                ReasonToken::LifeInsufficient, gb.modelId, f.jointId, f.caseId,
                f.atTime, f.segmentId, *gb.ratedLife, *criteria.requiredLife, "1",
                "目录 rated_life（额定寿命，循环数）vs 筛选条件 required_life", catalog);
            r.suggestion = "更换额定寿命更高的减速器型号";
            reasons.push_back(std::move(r));
        }
    }

    // ---- 维度 9：允许外载荷（轴向直比＋径向力臂核算——落位细化 ⑥）----
    if (f.externalLoad) {
        if (!gb.extLoad) {
            gaps.push_back(makeGap("external-load",
                                   "轴侧声明外载荷事实，目录条目未提供允许外载荷特性",
                                   f.jointId, f.caseId));
        } else {
            // 子项一：轴向力直接比较（无力臂语义）。
            if (exceedsLimit(f.externalLoad->axial, gb.extLoad->axial, core::QuantityKind::Force)) {
                RejectionReason r = makeReason(
                    ReasonToken::ExternalLoadExceeded, gb.modelId, f.jointId, f.caseId,
                    f.atTime, f.segmentId, f.externalLoad->axial, gb.extLoad->axial, "N",
                    "目录 external_load_axial_n（允许轴向力）", catalog);
                r.suggestion = "更换允许轴向力更大的减速器型号或复核工具载荷";
                reasons.push_back(std::move(r));
            }
            // 子项二：径向力臂核算。目录声明（F_rated @ L_rated）；实际
            // 作用点更远（L_actual > L_rated > 0）时允许力按悬臂力矩守恒
            // 折减 F_allow＝F_rated×L_rated/L_actual；作用点未标注
            // （L_rated＝0）或实际作用点不更远时不折减。
            double radialAllow = gb.extLoad->radial;
            std::string radialSource = "目录 external_load_radial_n（允许径向力）";
            if (gb.extLoad->distance > 0.0 && f.externalLoad->distance > gb.extLoad->distance) {
                radialAllow = gb.extLoad->radial * gb.extLoad->distance / f.externalLoad->distance;
                radialSource = "目录 external_load_radial_n @ external_load_dist_m"
                               "（作用点力臂核算 F_allow＝F_rated×L_rated/L_actual）";
            }
            if (exceedsLimit(f.externalLoad->radial, radialAllow, core::QuantityKind::Force)) {
                RejectionReason r = makeReason(
                    ReasonToken::ExternalLoadExceeded, gb.modelId, f.jointId, f.caseId,
                    f.atTime, f.segmentId, f.externalLoad->radial, radialAllow, "N",
                    radialSource, catalog);
                r.suggestion = "更换允许径向力更大的减速器型号或缩短载荷作用点力臂";
                reasons.push_back(std::move(r));
            }
        }
    }
}

}  // namespace

// =====================================================================
// IHardConstraintSelector 产品实现
// =====================================================================

std::vector<FeasibilityRecord> HardConstraintSelector::screenMotors(
    const CatalogPackageSnapshot& snapshot,
    const std::vector<AxisWorkpointFacts>& axisFacts,
    const ScreeningCriteria& criteria,
    const evidence::IEvaluationContext* ctx) const
{
    // 第一步：校验边界（§10.2——致命输入错误整批快速拒绝；通过后候选
    // 能力筛选不再短路）。
    validateCriteria(criteria);
    validateFacts(axisFacts);

    std::vector<FeasibilityRecord> records;
    const std::vector<core::ObjectId> axes = uniqueAxes(axisFacts);
    if (axes.empty() || snapshot.motors.empty()) {
        return records;  // 无轴可筛或无候选——空集（合法输入，非错误）
    }
    records.reserve(snapshot.motors.size() * axes.size());

    // 第二步：候选×轴遍历（候选序＝快照电机序〔modelId 升序〕、轴序＝
    // facts 首现序——双确定性，NFR-COR-02）。取消查询在候选边界（批次
    // 边界——卡 §14.4"维度批次边界查询"），观测到取消即截断返回。
    for (const MotorCatalogEntry& motor : snapshot.motors) {
        if (cancellationRequested(ctx)) {
            break;  // 截断语义：返回已完成记录（调用方以计数感知——T04 ④）
        }
        // 身份与校验状态检查（§7.1 ①）：Invalid 条目不参选。导入期
        // "报告非空则整体拒绝"保证快照内不含 Invalid——本分支为直接
        // 手工构造快照的防御路径（跳过＝不产生记录，语义＝无候选资格）。
        if (motor.status == ValidationStatus::Invalid) {
            continue;
        }
        for (const core::ObjectId& axis : axes) {
            // WP-19-T08（SEL-09）：移动关节轴范围外——先于全部 §7 维度
            // 判定输出"范围外"记录（不静默套用旋转传动：安装/转矩/转速/
            // 功率等维度对直线轴无语义；即使调用方误供了工作点数值也
            // 不消费——不伪造电机工作点）。每候选×该轴恰一条记录，
            // 记录数不变量（候选×轴）保持——调用方按计数感知完整性。
            if (axisJointKindOf(axisFacts, axis) == JointKind::Prismatic) {
                records.push_back(makeAxisOutOfScopeRecord(
                    motor.modelId, axis, motor.catalog, DeviceKind::Motor));
                continue;
            }
            // 同轴全部工况事实（组内保持输入序——逐工况独立判定后合并）。
            std::vector<RejectionReason> reasons;
            std::vector<DataGap> gaps;
            for (const AxisWorkpointFacts& f : axisFacts) {
                if (!(f.jointId == axis)) {
                    continue;
                }
                // ②安装兼容（条件存在才判定——§7.1 ②）。
                if (f.mountRequirement) {
                    screenMotorMounting(motor, f, motor.catalog, reasons, gaps);
                }
                // ③逐能力维度（全维度独立执行）。
                screenMotorCase(motor, f, criteria, snapshot, reasons, gaps);
            }
            // ④汇总（落位细化 ⑦）＋原因稳定排序（§10.4）＋记录键。
            FeasibilityRecord rec;
            rec.id = motor.modelId + "|" + axis.toCanonical();
            rec.deviceKind = DeviceKind::Motor;
            rec.candidateModelId = motor.modelId;
            rec.axisId = axis;
            rec.catalog = motor.catalog;
            sortReasons(reasons);
            rec.reasons = std::move(reasons);
            rec.gaps = std::move(gaps);
            rec.verdict = summarize(rec.reasons, rec.gaps);
            records.push_back(std::move(rec));
        }
    }
    return records;
}

std::vector<FeasibilityRecord> HardConstraintSelector::screenGearboxes(
    const CatalogPackageSnapshot& snapshot,
    const std::vector<AxisWorkpointFacts>& axisFacts,
    const ScreeningCriteria& criteria,
    const evidence::IEvaluationContext* ctx) const
{
    // 校验边界（同 screenMotors——§10.2）。
    validateCriteria(criteria);
    validateFacts(axisFacts);

    std::vector<FeasibilityRecord> records;
    const std::vector<core::ObjectId> axes = uniqueAxes(axisFacts);
    if (axes.empty() || snapshot.gearboxes.empty()) {
        return records;
    }
    records.reserve(snapshot.gearboxes.size() * axes.size());

    // 候选×轴遍历（序/取消语义同 screenMotors——遍历框架一致性）。
    for (const GearboxCatalogEntry& gb : snapshot.gearboxes) {
        if (cancellationRequested(ctx)) {
            break;
        }
        if (gb.status == ValidationStatus::Invalid) {
            continue;  // §8.1 ①——防御路径（导入期保证不含 Invalid）
        }
        for (const core::ObjectId& axis : axes) {
            // WP-19-T08（SEL-09）：移动关节轴范围外——同 screenMotors；
            // 输入转速维的 ω_m＝ω_joint/c 换算对直线轴无语义，同样不执行。
            if (axisJointKindOf(axisFacts, axis) == JointKind::Prismatic) {
                records.push_back(makeAxisOutOfScopeRecord(
                    gb.modelId, axis, gb.catalog, DeviceKind::Gearbox));
                continue;
            }
            std::vector<RejectionReason> reasons;
            std::vector<DataGap> gaps;
            for (const AxisWorkpointFacts& f : axisFacts) {
                if (!(f.jointId == axis)) {
                    continue;
                }
                // ②③④安装/速比/能力全维度（screenGearboxCase 内按固定
                // 序执行——注释逐维对应卡 §8.1 流程步骤）。
                screenGearboxCase(gb, f, criteria, reasons, gaps);
            }
            FeasibilityRecord rec;
            rec.id = gb.modelId + "|" + axis.toCanonical();
            rec.deviceKind = DeviceKind::Gearbox;
            rec.candidateModelId = gb.modelId;
            rec.axisId = axis;
            rec.catalog = gb.catalog;
            sortReasons(reasons);
            rec.reasons = std::move(reasons);
            rec.gaps = std::move(gaps);
            rec.verdict = summarize(rec.reasons, rec.gaps);
            records.push_back(std::move(rec));
        }
    }
    return records;
}

// =====================================================================
// 直线传动器件硬筛选（§17.2 SEL-09-S1 选型层——WP-19-T12；实现置于本
// 翻译单元＝复用上方匿名命名空间工具〔阈值比较容差/原因与缺口构造/
// 稳定排序/条件校验〕，禁第二套实现——NFR-MNT-03 单一权威）
// =====================================================================

namespace {

/// 直线轴去重（facts 首现序——与旋转侧 uniqueAxes 同款确定性纪律；
/// 模板类型不同故独立实现，遍历序语义一致）。
std::vector<core::ObjectId> uniqueLinearAxes(const std::vector<LinearAxisWorkpointFacts>& facts)
{
    std::vector<core::ObjectId> axes;
    for (const LinearAxisWorkpointFacts& f : facts) {
        bool seen = false;
        for (const core::ObjectId& a : axes) {
            if (a == f.jointId) { seen = true; break; }
        }
        if (!seen) { axes.push_back(f.jointId); }
    }
    return axes;
}

/// 直线事实数值校验（校验边界快速拒绝——§10.2：非有限＝调用方契约违约
/// fail-fast；present 值逐一检查，nullopt 合法＝该量未供给→维度缺口）。
void validateLinearFacts(const std::vector<LinearAxisWorkpointFacts>& facts)
{
    for (const LinearAxisWorkpointFacts& f : facts) {
        // 定位文本（诊断消息携带轴/工况——错误可定位，ERR-01 精神）。
        const std::string where = "（jointId=" + f.jointId.toCanonical()
                                + "，caseId=" + f.caseId + "）";
        if (f.forceRms)      { requireFinite(*f.forceRms, ("forceRms" + where).c_str()); }
        if (f.forcePeak)     { requireFinite(*f.forcePeak, ("forcePeak" + where).c_str()); }
        if (f.linearSpeedPeak) {
            requireFinite(*f.linearSpeedPeak, ("linearSpeedPeak" + where).c_str());
        }
        if (f.powerPeak)     { requireFinite(*f.powerPeak, ("powerPeak" + where).c_str()); }
        if (f.powerRms)      { requireFinite(*f.powerRms, ("powerRms" + where).c_str()); }
        // 位移/加速度为承载不判定字段——仍属输入事实，非有限同样违约
        // （不因"不判定"放松输入校验——NFR-COR-03 全输入面一致）。
        if (f.displacementPeak) {
            requireFinite(*f.displacementPeak, ("displacementPeak" + where).c_str());
        }
        if (f.accelerationPeak) {
            requireFinite(*f.accelerationPeak, ("accelerationPeak" + where).c_str());
        }
        if (!std::isfinite(f.atTime)) {
            throw std::invalid_argument(
                "selection/screening: 非有限输入（atTime" + where
                + "）——调用方契约违约（NFR-COR-03 fail-fast）");
        }
    }
}

/// 推力-速度曲线查询（§17.2 曲线口径——直线侧的双口径与旋转侧
/// motorPowerLimit 同构：曲线优先、固定额定值兜底）。
///
/// 曲线契约（卡 §17.2）：owner=linear-drive、横坐标 linear-speed（m/s）、
/// 纵坐标 force（N）——条目 curves 中该 quantity 组合唯一（导入期歧义
/// 拒绝；此处按首条匹配即可）。查询点＝工作点峰值线速度（m/s）。
/// 查询拒绝（含区间外外推拒绝）＝数据缺口分轨——**禁外推不放宽**
/// （SEL-CURVE-EXTRAPOLATION-DENIED 随缺口登记，不伪装成淘汰，§6.2）。
///
/// @param drive    [in] 候选直线器件条目
/// @param snapshot [in] 目录快照（曲线表查源）
/// @param speed    [in] 工作点峰值线速度（查询横坐标，m/s）
/// @param limit    [out] 能力值（N——曲线插值 y）
/// @param source   [out] 阈值来源描述（入淘汰原因 thresholdSource）
/// @param gap      [out] 查询拒绝时的缺口（数据不足分轨）
/// @param hasCurve [out] 条目是否声明推力-速度曲线（调用方区分"无曲线
///                 →固定额定值口径"与"有曲线但查询失败→缺口"）
/// @return true＝取得能力值（曲线口径或额定口径）；false＝数据不足
bool linearForceCurveLimit(const LinearDriveCatalogEntry& drive,
                           const CatalogPackageSnapshot& snapshot,
                           double speed, double& limit, std::string& source,
                           DataGap& gap, bool& hasCurve)
{
    hasCurve = false;
    // 在条目曲线引用中找 linear-speed→force 曲线（推力-速度能力曲线）。
    for (const CurveRef& ref : drive.curves) {
        if (ref.xQuantity != kQuantityLinearSpeed || ref.yQuantity != kQuantityForce) {
            continue;  // 非推力-速度曲线（如载荷-功率曲线）——本维度不消费
        }
        hasCurve = true;
        const PerformanceCurve* curve = nullptr;
        for (const PerformanceCurve& c : snapshot.curves) {
            if (c.curveId == ref.curveId) {
                curve = &c;
                break;
            }
        }
        if (curve == nullptr) {
            // 防御分支：导入期 REF-DANGLING 已拒绝悬空引用；直接构造的
            // 快照可能绕过导入——按数据不足处理，不猜测能力值。
            gap = makeGap("linear-force-curve",
                          "条目引用的推力-速度曲线在快照中不存在（curveId="
                              + ref.curveId + "）",
                          core::ObjectId{}, CaseId{},
                          std::string(kSelCurveExtrapolationDenied));
            return false;
        }
        LinearCurveEvaluator evaluator;
        const CurveQueryResult qr = evaluator.evaluate(*curve, speed);
        if (!qr.ok()) {
            // 曲线区间外/坏曲线＝数据不足（分轨——不是能力不足；外推
            // 拒绝码随缺口登记——SEL-02"默认禁止外推"不放宽，§6.2）。
            gap = makeGap("linear-force-curve",
                          "推力-速度曲线查询被拒绝：" + qr.rejectMessage,
                          core::ObjectId{}, CaseId{},
                          std::string(kSelCurveExtrapolationDenied));
            return false;
        }
        limit = qr.value;
        source = "能力曲线 " + ref.curveId + "（linear-speed→force，工作点速度查询）";
        return true;
    }
    // 无推力-速度曲线——固定额定值口径（§6.4：来源显式＝目录峰值推力
    // 列，不伪造曲线点）。
    limit = drive.peakForce;
    source = "目录 peak_force_n（峰值推力——固定额定值口径，无曲线）";
    return true;
}

/// 直线器件单工况全维度判定（§17.2——逐维独立执行，全部维度跑完才汇总；
/// 任一维度失败不阻断后续维度——§7.2 同款纪律）。
///
/// 维度执行序（固定——同 token 多条原因的稳定次序来源，§10.4）：
///   1 连续推力 → 2 峰值推力（额定口径）→ 3 峰值推力（曲线口径，声明
///   曲线时）→ 4 直线速度 → 5 直线功率（峰值/RMS 两子项）→ 6 安全系数
///   复判并入各维度（×SF 后比较——与旋转侧"工作点×SF"口径一致）。
void screenLinearDriveCase(const LinearDriveCatalogEntry& drive,
                           const LinearAxisWorkpointFacts& f,
                           const ScreeningCriteria& criteria,
                           const CatalogPackageSnapshot& snapshot,
                           std::vector<RejectionReason>& reasons,
                           std::vector<DataGap>& gaps)
{
    const CatalogIdentity& catalog = drive.catalog;
    // 安全系数（validateCriteria 已保证 ≥1 且有限；1.0＝不启用复判——
    // 复用旋转侧 SF 语义：工作点 ×SF 后与能力值比较，卡 §7.1）。
    const double sf = criteria.safetyFactor;

    // ---- 维度 1：连续推力（forceRms ×SF ≤ rated_force_n，单位 N）----
    if (!f.forceRms) {
        gaps.push_back(makeGap("linear-force-rms",
                               "直线轴推力 RMS 工作点未供给（映射事实缺失）",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*f.forceRms * sf, drive.ratedForce,
                            core::QuantityKind::Force)) {
        RejectionReason r = makeReason(
            ReasonToken::LinearForceContinuousInsufficient, drive.modelId,
            f.jointId, f.caseId, f.atTime, f.segmentId,
            *f.forceRms * sf, drive.ratedForce, "N",
            std::string("目录 rated_force_n（额定连续推力）")
                + (sf > 1.0 ? "——含安全系数复判" : ""),
            catalog);
        r.suggestion = "更换额定推力更大的直线传动器件型号";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 2+3：峰值推力（额定口径＋曲线口径——曲线声明时以插值
    //      上限复判；两口径任一超限即独立记因，词表共用 peak token）----
    if (!f.forcePeak) {
        gaps.push_back(makeGap("linear-force-peak",
                               "直线轴峰值推力工作点未供给（映射事实缺失）",
                               f.jointId, f.caseId));
    } else {
        const double demanded = *f.forcePeak * sf;   // 需求侧（×SF 复判）
        if (exceedsLimit(demanded, drive.peakForce, core::QuantityKind::Force)) {
            RejectionReason r = makeReason(
                ReasonToken::LinearForcePeakInsufficient, drive.modelId,
                f.jointId, f.caseId, f.atTime, f.segmentId,
                demanded, drive.peakForce, "N",
                std::string("目录 peak_force_n（峰值推力）")
                    + (sf > 1.0 ? "——含安全系数复判" : ""),
                catalog);
            r.suggestion = "更换峰值推力更大的直线传动器件型号";
            reasons.push_back(std::move(r));
        }
        // 曲线口径（**仅在条目声明推力-速度曲线时执行**——声明曲线即
        // "曲线口径加判"：以工作点速度查询能力上限复判；无曲线条目的
        // 固定额定值口径已由上方维度 2 承担，此处跳过——避免同阈值
        // 双报〔thresholdSource 相同的两条原因〕，§6.4 单口径语义）。
        const bool declaresForceCurve = [&] {
            for (const CurveRef& ref : drive.curves) {
                if (ref.xQuantity == kQuantityLinearSpeed
                    && ref.yQuantity == kQuantityForce) {
                    return true;
                }
            }
            return false;
        }();
        if (declaresForceCurve && f.linearSpeedPeak) {
            double limit = 0.0;
            std::string source;
            DataGap qgap;
            bool hasCurve = false;
            if (linearForceCurveLimit(drive, snapshot, *f.linearSpeedPeak,
                                      limit, source, qgap, hasCurve) && hasCurve) {
                if (exceedsLimit(demanded, limit, core::QuantityKind::Force)) {
                    RejectionReason r = makeReason(
                        ReasonToken::LinearForcePeakInsufficient, drive.modelId,
                        f.jointId, f.caseId, f.atTime, f.segmentId,
                        demanded, limit, "N",
                        source + (sf > 1.0 ? "——含安全系数复判" : ""),
                        catalog);
                    r.suggestion = "更换工作速度段推力能力更高的器件型号"
                                   "或降低该轴峰值速度需求";
                    reasons.push_back(std::move(r));
                }
            } else if (hasCurve) {
                // 查询拒绝（区间外/悬空/坏曲线）＝数据缺口（分轨）；
                // 轴/工况定位由调用方上下文回填（本缺口有工况语境）。
                qgap.axisId = f.jointId;
                qgap.caseId = f.caseId;
                gaps.push_back(std::move(qgap));
            }
        }
        // 速度工作点缺失且有曲线：曲线口径需查询点——缺口登记（不默认
        // 额定口径通过——§7.2"缺失不默认通过"；额定口径的维度 2 已判定）。
        if (!f.linearSpeedPeak && declaresForceCurve) {
            gaps.push_back(makeGap(
                "linear-force-curve",
                "曲线口径峰值推力维度缺查询点（峰值线速度未供给——不默认"
                "以额定口径替代曲线口径通过）",
                f.jointId, f.caseId));
        }
    }

    // ---- 维度 4：直线速度（linearSpeedPeak ×SF ≤ max_speed_ms，m/s）----
    if (!f.linearSpeedPeak) {
        gaps.push_back(makeGap("linear-speed",
                               "直线轴峰值线速度工作点未供给（映射事实缺失）",
                               f.jointId, f.caseId));
    } else if (exceedsLimit(*f.linearSpeedPeak * sf, drive.maxLinearSpeed,
                            core::QuantityKind::LinearVelocity)) {
        RejectionReason r = makeReason(
            ReasonToken::LinearSpeedInsufficient, drive.modelId,
            f.jointId, f.caseId, f.atTime, f.segmentId,
            *f.linearSpeedPeak * sf, drive.maxLinearSpeed, "m/s",
            std::string("目录 max_speed_ms（最高直线速度）")
                + (sf > 1.0 ? "——含安全系数复判" : ""),
            catalog);
        r.suggestion = "更换最高速度更高的直线传动器件型号";
        reasons.push_back(std::move(r));
    }

    // ---- 维度 5：直线功率（powerPeak/powerRms 两子项独立——额定口径，
    //      单位 W；功率是扩展端口产出的独立量，不自算 F×速度）----
    for (int which = 0; which < 2; ++which) {
        const std::optional<double>& p = (which == 0) ? f.powerPeak : f.powerRms;
        const char* gapDim = (which == 0) ? "linear-power-peak" : "linear-power-rms";
        const char* gapMsg = (which == 0) ? "直线轴峰值功率工作点未供给（映射事实缺失）"
                                          : "直线轴 RMS 功率工作点未供给（映射事实缺失）";
        if (!p) {
            gaps.push_back(makeGap(gapDim, gapMsg, f.jointId, f.caseId));
            continue;
        }
        if (exceedsLimit(*p * sf, drive.ratedPower, core::QuantityKind::Power)) {
            RejectionReason r = makeReason(
                ReasonToken::LinearPowerInsufficient, drive.modelId,
                f.jointId, f.caseId, f.atTime, f.segmentId,
                *p * sf, drive.ratedPower, "W",
                std::string("目录 rated_power_w（额定功率）")
                    + (sf > 1.0 ? "——含安全系数复判" : ""),
                catalog);
            r.suggestion = "更换额定功率更大的直线传动器件型号";
            reasons.push_back(std::move(r));
        }
    }

    // ---- 位移/加速度：承载不判定（四量完整性承载——判定维度随 R2 需求
    //      细化；不据此产生原因或缺口，不伪造判定——见 LinearDrive.hpp 注）----
    (void)f.displacementPeak;
    (void)f.accelerationPeak;
}

}  // namespace

std::vector<FeasibilityRecord> HardConstraintSelector::screenLinearDrives(
    const CatalogPackageSnapshot& snapshot,
    const std::vector<LinearAxisWorkpointFacts>& axisFacts,
    const ScreeningCriteria& criteria,
    const evidence::IEvaluationContext* ctx) const
{
    // 校验边界（§10.2——致命输入错误快速拒绝：条件非有限/安全系数＜1/
    // 事实数值非有限；候选能力筛选不短路不受影响）。
    validateCriteria(criteria);
    validateLinearFacts(axisFacts);

    std::vector<FeasibilityRecord> records;
    const std::vector<core::ObjectId> axes = uniqueLinearAxes(axisFacts);
    if (axes.empty() || snapshot.linearDrives.empty()) {
        // v1 包 linearDrives 恒空——恒返回空集（v1 行为零变化，V12-03）。
        return records;
    }
    records.reserve(snapshot.linearDrives.size() * axes.size());

    // 候选×轴遍历（候选序＝快照 linearDrives 序〔modelId 升序——装配
    // 保证〕、轴序＝facts 首现序——双确定性，NFR-COR-02）。取消查询在
    // 候选边界（语义同 screenMotors——观测到取消即截断返回）。
    for (const LinearDriveCatalogEntry& drive : snapshot.linearDrives) {
        if (cancellationRequested(ctx)) {
            break;  // 截断语义：返回已完成记录（调用方以计数感知）
        }
        // 身份与校验状态检查（§7.1 ①同款）：Invalid 条目不参选——导入期
        // 保证快照内不含 Invalid，本分支为直接构造快照的防御路径。
        if (drive.status == ValidationStatus::Invalid) {
            continue;
        }
        for (const core::ObjectId& axis : axes) {
            // 同轴全部工况事实（组内保持输入序——逐工况独立判定后合并）。
            std::vector<RejectionReason> reasons;
            std::vector<DataGap> gaps;
            for (const LinearAxisWorkpointFacts& f : axisFacts) {
                if (!(f.jointId == axis)) {
                    continue;
                }
                screenLinearDriveCase(drive, f, criteria, snapshot, reasons, gaps);
            }
            // 汇总（落位细化 ⑦同款三态收敛）＋原因稳定排序（§10.4）＋
            // 记录键（候选×轴——与旋转侧同构）。
            FeasibilityRecord rec;
            rec.id = drive.modelId + "|" + axis.toCanonical();
            rec.deviceKind = DeviceKind::LinearDrive;
            rec.candidateModelId = drive.modelId;
            rec.axisId = axis;
            rec.catalog = drive.catalog;
            sortReasons(reasons);
            rec.reasons = std::move(reasons);
            rec.gaps = std::move(gaps);
            rec.verdict = summarize(rec.reasons, rec.gaps);
            records.push_back(std::move(rec));
        }
    }
    return records;
}

}  // namespace sdurws::ird::selection
