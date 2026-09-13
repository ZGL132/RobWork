/**
 * @file   JointLimits.cpp
 * @brief  关节限位与行程阈值评估的唯一实现——JointLimitEvaluator::evaluate
 *         执行序（§7.4 行程/近限位/限位违例边界决策表、§9.4 契约、§7.5
 *         评估期异常处理）的产品落地（POL-T08）。
 *
 * 设计依据：
 *   - units/policy.md §9.4（IJointLimitEvaluator 契约与"边界"段——区间/
 *     范围非法检出为诊断级发现并跳过〔D-13 判定权归命令处理器〕、行程超限
 *     比较型结果供 project 处理器构造 ConfirmableFinding〔SA-15〕、阈值
 *     nullopt→显式 NotApplicable 标记）、§7.4（行程上限行：T＝|qmax−qmin|，
 *     T＞L 超限、T＝L 不超限——边界含于合规侧 D-08；近限位比行：r＝距限位
 *     余量/区间半宽，r＜阈值才警告；限位违例行：q ∉ [qmin,qmax] 闭区间）、
 *     §4.4（JointThresholds 阈值来源与 travelLimitCheckEnabled 开关——4π
 *     唯一冻结默认）、§4.5（四态——不适用＝显式标记不伪造结论）、§7.5
 *     （评估期数值异常——非有限不伪造、不入 findings）、§2.1 O-9、
 *     §12 POL-T08 行（本 TU＝JointLimits.cpp 产物落位）
 *   - 需求 MDL-06④/MDL-12（行程上限策略校验默认 4π；continuous 工程工作
 *     范围有限性）、KIN-13（阈值随策略传递——API 无阈值参数）、KIN-01
 *     （逐构型逐关节裕量素材）、SA-15/D-13（确认放行素材；检出不阻断）、
 *     AT-01（处理器 prepare）、ERR-01/UX-03（比较型三要素；不适用显式）、
 *     ARC-04（名称不可解析不猜测）、NFR-COR-02/03（确定性；不静默转 0）
 *
 * 背景说明（第一读者须知）：
 *   本 TU 是**纯标量评估**——零框架调用（无 WorkCell/Device/检测器）：
 *   全部判定是可解析算例级的确定性算术。rw include 面仅经 JointLimits.hpp
 *   →CollisionQuery.hpp 的状态词表复用链传导（§9.4"复用状态词表"原文），
 *   故本 TU 与其测试 TU 同为**集成模式专属**（CMakeLists gating——冒烟
 *   模式无框架 include 路径；runtime RT-T07/POL-T07 同款分工登记）。
 *
 * 评估期诊断码（建议码单点——码值权威归 diagnostics StableCodeRegistry，
 * PA-1；§9.6 建议码清单行，随单元卡 v0.9 补登留痕；POL-T10 落位
 * Diagnostics.hpp 后迁入其码表，本 TU 届时仅消费；CollisionQuery.cpp
 * 同模式——CLL 家族两值在该 TU 亦有各自单点，两处字面量同源自 §9.6
 * 同一清单行，迁码表时一并收敛）：
 *   POLICY-JNT-TABLE-INVALID             关节表区间非法（伴随 IntervalInvalid）
 *   POLICY-JNT-ENGINEERING-RANGE-INVALID 工程工作范围非法（伴随 EngineeringRangeInvalid）
 *   POLICY-CLL-NAME-UNRESOLVED           名称不可解析（ARC-04 不猜测）
 *   POLICY-CLL-EVALUATION-FAILED         构型位置非有限（§7.5 不伪造）
 *
 * 线程安全：evaluate 为 const 纯函数——无共享可变状态、无随机源、无归约；
 * 同 (查询, 策略, 名称映射) 输入必得逐字段相等输出（NFR-COR-02）。
 */

#include <sdurws/ird/policy/JointLimits.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::policy {

// =====================================================================
// 匿名命名空间——评估实现的内部件（非公共契约）。
// =====================================================================

namespace {

// ---- 评估期诊断建议码单点（见文件头码表说明——POL-T10 前的承载处）----

/// 关节表区间非法（伴随 IntervalInvalid 发现——§9.6 JNT 家族清单行）。
constexpr std::string_view kCodeJntTableInvalid = "POLICY-JNT-TABLE-INVALID";
/// 工程工作范围非法（伴随 EngineeringRangeInvalid 发现——同上）。
constexpr std::string_view kCodeJntEngineeringRangeInvalid =
    "POLICY-JNT-ENGINEERING-RANGE-INVALID";
/// 名称不可解析（§7.5——不猜测，ARC-04；Errors.hpp NameUnresolved 注释的
/// "评估期同因转 Failed＋同码诊断"路径）。
constexpr std::string_view kCodeNameUnresolved = "POLICY-CLL-NAME-UNRESOLVED";
/// 构型位置非有限（§7.5"NaN/±Inf 不伪造、不入 findings"）。
constexpr std::string_view kCodeEvaluationFailed = "POLICY-CLL-EVALUATION-FAILED";

/// 诊断 context 固定串（同码同上下文——诊断的稳定分类面；用户可见文案权威
/// 归 diagnostics/ui——NFR-REL-05，本串仅为执行语境标注）。
constexpr std::string_view kEvalContext =
    "关节限位与行程阈值评估（JointLimitEvaluator::evaluate）";

/// 裕量记录 provenance 的方法短标记（derived-readonly——评估派生只读事实；
/// 语法 [a-z0-9./_-]，core ValueProvenance::make 校验）。
constexpr std::string_view kMarginProvenanceTag = "policy/joint-limits";

/**
 * @brief 非有限/任意双精度值的原文格式化（%.17g——round-trip 精确；仅进
 *        诊断 cause，非持久化契约面；snprintf "C" locale 数字格式，无本地
 *        小数点——NFR-COR-02。NaN/±Inf 经 %.17g 输出 "nan"/"inf"/"-inf"
 *        字面量——"非有限以原文保留"的承载形式，与 CollisionQuery.cpp
 *        同款）。
 */
std::string formatRawDouble(double v)
{
    char buf[40] = {};
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string{buf};
}

/**
 * @brief 角度单位句柄（rad——core 注册表冻结 token；比较型诊断与裕量记录
 *        的单位标注唯一来源，禁止第二字面量）。
 */
core::UnitToken radUnit()
{
    // "rad" 在 core R1 冻结单位表中（Units.cpp 注册表行 1）——find 必命中；
    // nullopt 分支为防御（注册表被改才可达），命中失败即快速失败不静默。
    const std::optional<core::UnitToken> unit = core::UnitToken::find("rad");
    if (!unit.has_value()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "core 单位注册表缺少冻结 token 'rad'（内部不变量违约）");
    }
    return *unit;
}

/**
 * @brief 裕量记录的来源记录（derived-readonly——评估派生只读事实；D-05
 *        语义边界：来源是事实，不构成正式通过结论，DYN-06 同源口径）。
 */
core::ValueProvenance marginProvenance()
{
    return core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {},
                                       {}, std::string{kMarginProvenanceTag});
}

/**
 * @brief 关节限位评估诊断记录构造（core::DiagnosticRecord::make 的评估语境
 *        包装——ERR-01 三要素：稳定码/对象定位/上下文＋建议动作；比较型
 *        三要素仅区间/范围非法的伴随诊断携带，其余失败诊断以 cause 文本
 *        定位）。
 *
 * @param code        [in] 建议码（本 TU 顶部四值之一）
 * @param subject     [in] 定位关节对象（评估级失败可为 nullopt——cause 承载定位）
 * @param localName   [in] 局部名（空串→不回填，不伪造）
 * @param runtimeName [in] 运行时名（空串→不回填——名称未解析时本来就没有）
 * @param cause       [in] 原因（非空——工厂校验）
 * @param recommendedAction [in] 建议动作（非空——工厂校验）
 * @param comparison  [in] 比较型三要素（可选——区间/范围非法诊断携带）
 * @return 可持久化诊断记录（C-3 校验通过）
 */
core::DiagnosticRecord
makeJointDiagnostic(std::string_view code, const std::optional<core::ObjectId>& subject,
                    const std::string& localName, const std::string& runtimeName,
                    std::string cause, std::string recommendedAction,
                    std::optional<core::ComparativeFields> comparison = std::nullopt)
{
    return core::DiagnosticRecord::make(
        std::string{code}, subject,
        localName.empty() ? std::optional<std::string>(std::nullopt)
                          : std::optional<std::string>(localName),
        runtimeName.empty() ? std::optional<std::string>(std::nullopt)
                            : std::optional<std::string>(runtimeName),
        std::string{kEvalContext}, std::move(cause), std::move(recommendedAction),
        std::move(comparison));
}

/**
 * @brief 比较侧数值承载（有限→Provided 派生只读；非有限→Invalid 保留
 *        %.17g 原文——ERR-01/NFR-COR-03：非法值不静默转 0/默认，SourcedValue
 *        四态语义复用，不另设第二套）。
 */
core::ComparativeValue comparativeValue(double v)
{
    core::ComparativeValue side;
    if (std::isfinite(v)) {
        side.quantity = core::SourcedValue<double>::provided(v, marginProvenance());
    }
    else {
        // 非有限端点（连续关节工程范围——MDL-12 有限性检出的对象）：
        // Invalid 态保留原串，诊断消费方可见 "nan"/"inf" 字面量。
        side.quantity = core::SourcedValue<double>::invalid(formatRawDouble(v));
    }
    side.unit = radUnit();
    return side;
}

/**
 * @brief 查询契约校验（JointLimitQuery 匹配契约——调用方装配违约 fail-fast
 *        轨，§9.4 Spec 注释契约行；校验序固定——确定性 NFR-COR-02）。
 *
 * 校验面（逐条——全部为"装配形态"违约，与"已提供但非法"的语义检出分轨：
 * 后者按 §9.4 边界段走诊断级发现，见 evaluate 主体）：
 *   ① joints 非空；② configurations 非空；③ jointObject 有效（非全零保留值）；
 *   ④ jointObject 表内唯一；⑤ qMin/qMax 同有同无（单侧＝违约）；
 *   ⑥ continuous 必填 engineeringRange（"必填"＝装配契约——缺失在此
 *      fail-fast；其"有限性"检出席位在评估主体，JointLimitSpec 注释两分）；
 *   ⑦ 每构型维度＝关节表长度（内层与 joints 等长同序——JointLimitQuery 契约）。
 *
 * @throws PolicyError(PolicyErrorCode::QueryInvalid) 任一违约（cause 定位条目）
 */
void validateQueryShape(const JointLimitQuery& q)
{
    // ① 空关节表：无关节可评估——空表不是"全部合格"，是装配违约。
    if (q.joints.empty()) {
        throw PolicyError(PolicyErrorCode::QueryInvalid,
                          "关节表为空（JointLimitQuery.joints 至少 1 条目——"
                          "空表属调用方装配违约，§9.4）");
    }
    // ② 空构型序列：同 §6.2 POL-EXC-1 口径——空序列无构型语义可言。
    if (q.configurations.empty()) {
        throw PolicyError(PolicyErrorCode::QueryInvalid,
                          "构型序列为空（JointLimitQuery.configurations 至少 1 构型——"
                          "空序列属调用方装配违约）");
    }
    // ③④ 身份有效性与唯一性（全零保留值＝未分配身份；重复条目使输出定位
    // 键歧义——两者都是装配违约，fail-fast 优于评估期歧义输出）。
    std::set<core::ObjectId> seen;
    for (std::size_t i = 0; i < q.joints.size(); ++i) {
        const JointLimitSpec& spec = q.joints[i];
        if (!spec.jointObject.isValid()) {
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "关节表条目 " + std::to_string(i)
                                  + " 的 jointObject 无效（全零保留值——project 分配者"
                                    "纪律，§9.4 JointLimitSpec）");
        }
        if (!seen.insert(spec.jointObject).second) {
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "关节表存在重复 jointObject（表内必须唯一——输出定位"
                              "键歧义防护）: obj=" + spec.jointObject.toCanonical());
        }
        // ⑤ 限位端同有同无（§9.4 Spec 注释"二者须同时有/无"——单侧出现使
        // 区间/行程/裕量的数学语义不完整，装配期拒绝）。
        if (spec.qMin.has_value() != spec.qMax.has_value()) {
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "关节表条目 " + std::to_string(i)
                                  + " 的 qMin/qMax 须同时有/无（单侧限位属装配违约，"
                                    "§9.4 JointLimitSpec）: obj="
                                  + spec.jointObject.toCanonical());
        }
        // ⑥ continuous 必填工程工作范围（"必填"半边＝装配契约——缺失在此
        // 拒绝；已提供但 min≥max/端点非有限的"有限性"检出归评估主体诊断级
        // 发现——§9.4 边界段，两分登记于 policy.md §15.4 v0.9）。
        if (spec.isContinuous && !spec.engineeringRange.has_value()) {
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "continuous 关节缺少必填的 engineeringRange（工程工作"
                              "范围必填有限区间，§9.4 JointLimitSpec）: obj="
                                  + spec.jointObject.toCanonical());
        }
    }
    // ⑦ 构型维度（内层向量与 joints 等长同序——第一个越界样本即报告，不
    // 全量扫描：fail-fast 定位首要违约即可）。
    for (std::size_t s = 0; s < q.configurations.size(); ++s) {
        if (q.configurations[s].size() != q.joints.size()) {
            throw PolicyError(PolicyErrorCode::QueryInvalid,
                              "构型维度与关节表长度不符（内层与 joints 等长同序，"
                              "单位 rad）: configurations[" + std::to_string(s)
                                  + "].size=" + std::to_string(q.configurations[s].size())
                                  + ", joints=" + std::to_string(q.joints.size()));
        }
    }
}

}  // namespace

// =====================================================================
// evaluate——评估执行序（JointLimitEvaluator.hpp 注释七步的落地）。
// =====================================================================

JointLimitEvaluation JointLimitEvaluator::evaluate(const JointLimitQuery& q,
                                                   const EngineeringPolicySet& policy,
                                                   const IPolicyNameContext& names) const
{
    // ---- 输出基座预填（全部出口共用——缺省态＝Failed 非终态：任何提前出口
    // ---- 不得虚标 Completed，逐出口显式覆写；与 CollisionQuery.cpp 同纪律）。
    JointLimitEvaluation out;
    out.status = CollisionEvaluationStatus::Failed;
    out.finalized = false;

    // ---- ① policy 发布态复检（调用方契约违约 fail-fast——与会话侧
    // ---- createSession ① 同码同语义：把未发布对象当策略传入＝装配违约）。
    if (policy.validationState != PolicyValidationState::Valid
        || !policy.policyObject.isValid() || !policy.contentIdentity.isValid()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "policy 发布态复检失败（须为 Valid 已发布对象且身份有效——"
                          "§9.4 前置；validationState="
                              + std::to_string(static_cast<int>(policy.validationState))
                              + "）");
    }

    // ---- ② 查询契约校验（唯一抛出点之二——装配违约 fail-fast，见上）。
    validateQueryShape(q);

    // ---- ③ 构型位置有限性（§7.5 数值异常行同构——NaN/±Inf 不伪造、不入
    // ---- findings：任一构型任一关节位置非有限，本次评估整体 Failed；
    // ---- 原文 %.17g 保留于诊断 cause，NFR-COR-03）。
    //     全表先验扫描（含将被跳过的关节——数据级非法不因检查豁免而合法）。
    for (std::size_t s = 0; s < q.configurations.size(); ++s) {
        for (std::size_t j = 0; j < q.joints.size(); ++j) {
            const double pos = q.configurations[s][j];
            if (!std::isfinite(pos)) {
                out.diagnostics.push_back(makeJointDiagnostic(
                    kCodeEvaluationFailed, q.joints[j].jointObject, q.joints[j].localName,
                    std::string{},
                    "构型位置非有限（原文=" + formatRawDouble(pos)
                        + "，SI rad）: sample=" + std::to_string(s) + ", joint="
                        + q.joints[j].jointObject.toCanonical()
                        + "——§7.5 不伪造、不入 findings/margins",
                    "排查构型数据来源（IK/采样器）后重评；失败输出不入正式证据"
                    "（finalized=false）"));
                return out;   // status=Failed，finalized=false
            }
        }
    }

    // ---- ④ 名称解析（规范序遍历＝jointObject 字典序——输出与装配顺序无关；
    // ---- 不可解析→Failed＋诊断定位，不猜测——ARC-04）。
    // 规范序下标表（关节表承载序 → 处理序；排序键＝ObjectId 字节序）。
    std::vector<std::size_t> canonicalOrder(q.joints.size());
    std::iota(canonicalOrder.begin(), canonicalOrder.end(), std::size_t{0});
    std::stable_sort(canonicalOrder.begin(), canonicalOrder.end(),
                     [&q](std::size_t l, std::size_t r) {
                         return q.joints[l].jointObject < q.joints[r].jointObject;
                     });
    // 运行时名固化表（后续发现/裕量/诊断共用——一次解析，评估期零重复查询；
    // IPolicyNameContext 幂等契约使其正确性不依赖缓存，仅为确定性遍历服务）。
    std::map<core::ObjectId, std::string> runtimeNames;
    for (const std::size_t idx : canonicalOrder) {
        const JointLimitSpec& spec = q.joints[idx];
        const std::optional<std::string> runtimeName = names.tryRuntimeName(spec.jointObject);
        if (!runtimeName.has_value()) {
            out.diagnostics.push_back(makeJointDiagnostic(
                kCodeNameUnresolved, spec.jointObject, spec.localName, std::string{},
                "关节对象经名称上下文不可解析（nullopt 原样呈现——不猜测，ARC-04）: obj="
                    + spec.jointObject.toCanonical(),
                "核对名称映射与模型编译产物的一致性后重评；失败输出不入正式证据"));
            return out;   // status=Failed，finalized=false
        }
        runtimeNames.emplace(spec.jointObject, *runtimeName);
    }

    // ---- ⑤ 检查级不适用标记预填（acceptance 3 载体——Check 枚举序固定
    // ---- 产出；P-POL-2/O-10 保守口径：阈值未设置＝该检查显式不适用，
    // ---- 不伪造数值；开关关闭＝策略显式停用，同样如实标记）。
    // ⑤a 近限位比：无冻结默认（P-POL-2）——nullopt 即检查不适用。
    if (!policy.jointThresholds.nearLimitRatio.has_value()) {
        JointCheckNotApplicable marker;
        marker.check = JointCheckNotApplicable::Check::NearLimitRatio;
        marker.cause = "近限位比阈值未设置（nullopt——无冻结默认，P-POL-2/O-10 保守"
                       "口径）：该检查显式不适用，不伪造数值（policy.md §4.4/§7.4）";
        out.notApplicableChecks.push_back(std::move(marker));
    }
    // ⑤b 条件数警告：无冻结默认（P-POL-2）；本评估器无条件数检查种类
    // （§9.4 Kind 五值不含之——权威唯一 R-6，其计算归所属单元），仅在
    // 阈值未设置时按 acceptance 3 同口径输出标记。
    if (!policy.jointThresholds.conditionNumberWarning.has_value()) {
        JointCheckNotApplicable marker;
        marker.check = JointCheckNotApplicable::Check::ConditionNumberWarning;
        marker.cause = "条件数警告阈值未设置（nullopt——无冻结默认，P-POL-2/O-10 保守"
                       "口径）：该检查显式不适用，不伪造数值（policy.md §4.4）";
        out.notApplicableChecks.push_back(std::move(marker));
    }
    // ⑤c 行程上限校验开关（§4.4 travelLimitCheckEnabled——false＝策略显式
    // 停用 MDL-06④ 策略校验，非阈值缺席，语义分列）。
    if (!policy.jointThresholds.travelLimitCheckEnabled) {
        JointCheckNotApplicable marker;
        marker.check = JointCheckNotApplicable::Check::TravelLimit;
        marker.cause = "行程上限校验被策略显式关闭（travelLimitCheckEnabled=false"
                       "——policy.md §4.4 校验开关行）";
        out.notApplicableChecks.push_back(std::move(marker));
    }

    // ---- ⑥ 逐关节检查（规范序遍历——与名称解析同序，输出与装配顺序无关）。
    // 阈值取值（KIN-13/R-POL-5：阈值唯一来自 policy——API 无阈值参数）。
    const double travelLimitRad = policy.jointThresholds.finiteRotationTravelLimit.siValue();
    const bool nearRatioPresent = policy.jointThresholds.nearLimitRatio.has_value();
    const double nearRatio = nearRatioPresent ? policy.jointThresholds.nearLimitRatio->siValue()
                                              : 0.0;
    const bool travelCheckEnabled = policy.jointThresholds.travelLimitCheckEnabled;

    for (const std::size_t idx : canonicalOrder) {
        const JointLimitSpec& spec = q.joints[idx];
        const std::string& runtimeName = runtimeNames.at(spec.jointObject);
        const bool limitsPresent = spec.qMin.has_value();   // 同有同无——②已保证

        // ---- 分支 A：有限限位关节（!continuous）----
        if (!spec.isContinuous) {
            if (limitsPresent) {
                const double qmin = *spec.qMin;   // SI rad（非度——Spec 契约）
                const double qmax = *spec.qMax;   // SI rad
                // ---- A1 区间有效性（qmin＜qmax——硬断言的检出侧：违约→
                // 诊断级发现＋跳过该关节，状态仍 Completed——D-13：判定权归
                // 命令处理器，本评估器不阻断、不伪造裕量）。NaN 端点经
                // "NaN 参与的 ＜ 比较恒 false"自然落入本分支（确定性）。
                if (!(qmin < qmax)) {
                    JointLimitFinding finding;
                    finding.jointObject = spec.jointObject;
                    finding.localName = spec.localName;
                    finding.runtimeName = runtimeName;
                    finding.kind = JointLimitFindingKind::IntervalInvalid;
                    finding.actualValue = qmin;      // 比较三要素：qmin ≥ qmax（rad）
                    finding.thresholdValue = qmax;   // （口径见 JointLimitFindingKind 注释表）
                    finding.level = PolicyRuleLevel::Must;   // 供处理器就地阻断
                    out.findings.push_back(std::move(finding));
                    // 伴随稳定诊断（比较型三要素齐备——处理器可直接构造
                    // ConfirmableFinding 素材；码＝§9.6 JNT 家族）。
                    core::ComparativeFields comparison;
                    comparison.actual = comparativeValue(qmin);
                    comparison.expected = comparativeValue(qmax);
                    out.diagnostics.push_back(makeJointDiagnostic(
                        kCodeJntTableInvalid, spec.jointObject, spec.localName, runtimeName,
                        "关节区间非法（qmin ≥ qmax，须 qmin＜qmax——硬断言的检出侧，"
                        "MDL-06）：qmin=" + formatRawDouble(qmin)
                            + ", qmax=" + formatRawDouble(qmax) + "（SI rad）——该关节"
                            "已跳过（不产出裕量，D-13 判定权归命令处理器）",
                        "由建模/project 处理器按硬断言语义就地处置（D-13）；修正区间后"
                        "重评",
                        std::move(comparison)));
                    continue;   // 跳过：无行程/违例/近限位检查、无裕量记录
                }
                // ---- A2 行程上限比较（§7.4 行程上限行：T＝|qmax−qmin|，
                // T＞L 才超限；T＝L 不超限——边界含于合规侧，D-08；开关关闭
                // 时整段跳过——⑤c 标记已声明）。
                if (travelCheckEnabled) {
                    const double travel = std::fabs(qmax - qmin);   // T，SI rad
                    if (travel > travelLimitRad) {
                        JointLimitFinding finding;
                        finding.jointObject = spec.jointObject;
                        finding.localName = spec.localName;
                        finding.runtimeName = runtimeName;
                        finding.kind = JointLimitFindingKind::TravelLimitExceeded;
                        finding.actualValue = travel;          // 比较三要素：实际行程（rad）
                        finding.thresholdValue = travelLimitRad;  // 阈值 L（rad）
                        finding.level = PolicyRuleLevel::Must;  // 供 SA-15 ConfirmableFinding
                        out.findings.push_back(std::move(finding));
                        // policy 止于比较型结果供给（§10.4）——不放行、不阻断、
                        // 不产生"可确认/已确认"语义（确认流归 project 处理器）。
                    }
                }
                // ---- A3 逐构型扫描：限位违例（首个命中）＋近限位（首个命中）
                // ＋逐构型裕量记录（每样本一条——KIN-01 逐构型逐关节口径）。
                const double halfWidth = (qmax - qmin) / 2.0;   // 区间半宽，SI rad（＞0——A1 保证）
                bool limitViolatedEmitted = false;   // 每关节每 kind 至多一条——首个命中
                bool nearLimitEmitted = false;
                for (std::size_t s = 0; s < q.configurations.size(); ++s) {
                    const double pos = q.configurations[s][idx];   // 内层与表序对齐
                    // 有符号裕量与近限位比（§7.4 口径——区间内 ∈[0,半宽]/[0,1]，
                    // 越限为负有符号事实；计算恒执行——记录必须逐样本齐备）。
                    const double margin = std::min(pos - qmin, qmax - pos);   // SI rad
                    const double ratio = margin / halfWidth;                  // 无量纲 r
                    // 限位违例（q ∉ [qmin,qmax]——闭区间端点不违例；硬过滤
                    // 素材，过滤动作归 kinematics KIN-02；阈值侧＝被越过的端）。
                    if (!limitViolatedEmitted && (pos < qmin || pos > qmax)) {
                        const double crossedLimit = (pos < qmin) ? qmin : qmax;
                        JointLimitFinding finding;
                        finding.jointObject = spec.jointObject;
                        finding.localName = spec.localName;
                        finding.runtimeName = runtimeName;
                        finding.kind = JointLimitFindingKind::LimitViolated;
                        finding.actualValue = pos;            // 违例位置（rad）
                        finding.thresholdValue = crossedLimit;  // 被越过的限位端（rad）
                        finding.level = PolicyRuleLevel::Must;
                        out.findings.push_back(std::move(finding));
                        limitViolatedEmitted = true;   // 后续样本事实由 margins 承载
                    }
                    // 近限位警告（§7.4：r＜阈值才警告——严格小于，r＝阈值不
                    // 警告〔同 D-08 边界含于安全侧〕；Should 级默认；阈值
                    // nullopt 时整段不评估——⑤a 标记已声明；越限样本的负
                    // 裕量不参与警告判定——违例已由上支承载）。
                    if (!nearLimitEmitted && nearRatioPresent && margin >= 0.0
                        && ratio < nearRatio) {
                        JointLimitFinding finding;
                        finding.jointObject = spec.jointObject;
                        finding.localName = spec.localName;
                        finding.runtimeName = runtimeName;
                        finding.kind = JointLimitFindingKind::NearLimit;
                        finding.actualValue = ratio;      // 比较三要素：r（无量纲）
                        finding.thresholdValue = nearRatio;  // 阈值（无量纲）
                        finding.level = PolicyRuleLevel::Should;  // §7.4"Should 级默认"
                        out.findings.push_back(std::move(finding));
                        nearLimitEmitted = true;
                    }
                    // 裕量记录（Provided——有限限位且区间有效且位置有限，②③保证）。
                    JointMarginRecord record;
                    record.jointObject = spec.jointObject;
                    record.localName = spec.localName;
                    record.runtimeName = runtimeName;
                    record.sampleIndex = s;
                    record.marginToNearestLimit =
                        core::SourcedValue<double>::provided(margin, marginProvenance());
                    record.nearLimitRatioValue =
                        core::SourcedValue<double>::provided(ratio, marginProvenance());
                    out.margins.push_back(std::move(record));
                }
            }
            else {
                // ---- 分支 B：未声明限位的有限关节（qMin/qMax 均缺——类型层
                // 合法形态）：无限位可比→全部检查不适用；裕量记录以
                // NotApplicable 显式标记（不伪造数值——§4.5/ERR-01；KIN-01
                // 消费方可区分"不适用"与"缺失"）。
                for (std::size_t s = 0; s < q.configurations.size(); ++s) {
                    JointMarginRecord record;
                    record.jointObject = spec.jointObject;
                    record.localName = spec.localName;
                    record.runtimeName = runtimeName;
                    record.sampleIndex = s;
                    record.marginToNearestLimit = core::SourcedValue<double>::notApplicable();
                    record.nearLimitRatioValue = core::SourcedValue<double>::notApplicable();
                    out.margins.push_back(std::move(record));
                }
            }
        }
        // ---- 分支 C：continuous 关节（MDL-12——行程上限豁免，§7.4"continuous
        // ---- 关节免除"；工程工作范围有限性在此检出）。
        else {
            const std::pair<double, double>& range = *spec.engineeringRange;   // ②必填已保证
            // 范围有限性（MDL-12：双端有限且 first＜second——"已提供但非法"
            // 的语义检出→诊断级发现＋跳过〔D-13〕；"必填"缺失已在②fail-fast，
            // 两分登记于 policy.md §15.4 v0.9）。
            const bool endsFinite = std::isfinite(range.first) && std::isfinite(range.second);
            if (!endsFinite || !(range.first < range.second)) {
                JointLimitFinding finding;
                finding.jointObject = spec.jointObject;
                finding.localName = spec.localName;
                finding.runtimeName = runtimeName;
                finding.kind = JointLimitFindingKind::EngineeringRangeInvalid;
                finding.actualValue = range.first;    // 比较三要素：范围端（rad，可能
                finding.thresholdValue = range.second;  // 非有限——原文经诊断保留）
                finding.level = PolicyRuleLevel::Must;   // 供处理器就地阻断
                out.findings.push_back(std::move(finding));
                // 伴随稳定诊断（非有限端点以 Invalid 态保留 %.17g 原文——
                // NFR-COR-03 不静默转 0；有限端点 Provided 携带数值）。
                core::ComparativeFields comparison;
                comparison.actual = comparativeValue(range.first);
                comparison.expected = comparativeValue(range.second);
                out.diagnostics.push_back(makeJointDiagnostic(
                    kCodeJntEngineeringRangeInvalid, spec.jointObject, spec.localName,
                    runtimeName,
                    "continuous 关节工程工作范围非法（须双端有限且 first＜second——"
                    "MDL-12 有限性）：first=" + formatRawDouble(range.first)
                        + ", second=" + formatRawDouble(range.second)
                        + "（SI rad）——该关节已跳过（不产出裕量，D-13 判定权归命令"
                          "处理器）",
                    "由建模/project 处理器按硬断言语义就地处置（D-13）；修正工程工作"
                    "范围后重评",
                    std::move(comparison)));
                continue;   // 跳过：无裕量记录（§9.4"不伪造裕量"）
            }
            // 合法 continuous：行程上限豁免（§7.4）、无限位可比→裕量记录
            // NotApplicable 显式标记（逐构型齐备——KIN-01 口径；工程范围
            // 越界无对应检查种类〔§9.4 Kind 五值不含〕，不发明新判定——
            // 范围事实随 JointLimitSpec 由消费方取用）。
            for (std::size_t s = 0; s < q.configurations.size(); ++s) {
                JointMarginRecord record;
                record.jointObject = spec.jointObject;
                record.localName = spec.localName;
                record.runtimeName = runtimeName;
                record.sampleIndex = s;
                record.marginToNearestLimit = core::SourcedValue<double>::notApplicable();
                record.nearLimitRatioValue = core::SourcedValue<double>::notApplicable();
                out.margins.push_back(std::move(record));
            }
        }
    }

    // ---- ⑦ 稳定排序＋终态化（§9.4"稳定排序（对象字典序×kind）"——输出
    // ---- 与装配顺序无关，三入口跨进程逐字节一致，NFR-COR-05/AT-19）。
    // findings 键序：(jointObject 字节字典序, kind)——生成序已与键序同向
    // （规范序遍历），stable_sort 为契约兜底（同键重复元保持生成序——每
    // 关节每 kind 至多一条，无同键重复，防御性声明）。
    std::stable_sort(out.findings.begin(), out.findings.end(),
                     [](const JointLimitFinding& l, const JointLimitFinding& r) {
                         if (l.jointObject != r.jointObject) {
                             return l.jointObject < r.jointObject;
                         }
                         return static_cast<int>(l.kind) < static_cast<int>(r.kind);
                     });
    // margins 键序：(sampleIndex 升序, jointObject 字典序)——实现冻结口径
    // （JointMarginRecord 注释；policy.md §15.4 v0.9 登记）。
    std::stable_sort(out.margins.begin(), out.margins.end(),
                     [](const JointMarginRecord& l, const JointMarginRecord& r) {
                         if (l.sampleIndex != r.sampleIndex) {
                             return l.sampleIndex < r.sampleIndex;
                         }
                         return l.jointObject < r.jointObject;
                     });
    // notApplicableChecks 生成序即 Check 枚举序（⑤固定产出序）——无第二排序点。
    // 检出非法（IntervalInvalid/EngineeringRangeInvalid）不改变评估状态——
    // D-13 诊断级不阻断：评估已完成（发现齐备），阻断决策归命令处理器。
    out.status = CollisionEvaluationStatus::Completed;
    out.finalized = true;
    return out;
}

// =====================================================================
// 唯一构造入口（装配线对称性——JointLimitEvaluator 无状态，直构等价）。
// =====================================================================

std::unique_ptr<IJointLimitEvaluator> makeJointLimitEvaluator()
{
    return std::make_unique<JointLimitEvaluator>();
}

}  // namespace sdurws::ird::policy
