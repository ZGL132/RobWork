/**
 * @file   Diagnostics.cpp
 * @brief  policy 诊断构造的唯一实现点（POL-T10）——码表成员判定＋
 *         IPolicyDiagnostics 产品实现＋makeComparative 辅助组。建议码字面量
 *         （switch 表）与全表数组的**定义**在公共头 Diagnostics.hpp
 *         （inline constexpr——迁移 TU 编译期别名与 Errors.cpp 委托所需的
 *         可见性），字面量全单元仍仅此一处。
 *
 * 设计依据：
 *   - units/policy.md §9.6（建议码清单原文——码值与顺序来源）、§3.1
 *     （Diagnostics.hpp/.cpp 为 POL-T10 产物）、§12 POL-T10 行
 *   - 需求 ERR-01/UX-03（诊断三要素/比较型三要素——make() 的核对语义）、
 *     NFR-MNT-03（单一权威定义——全单元 POLICY-* 字面量仅 Diagnostics.hpp
 *     的 policyDiagCode switch 表一处；Errors.cpp registryCode 自本任务起
 *     委托该表）、NFR-COR-02（确定性——编译期固定表，无运行期构造）
 *   - 任务契约 tasks/foundation/POL-T10.json acceptance 1：checkDiagnosticRecord
 *     族（testkit）码表与构造不变量用例通过
 *
 * 确定性（NFR-COR-02）：全部构造为纯函数、无环境依赖——同输入必得逐字段
 * 相等输出。线程安全：全部为纯函数/只读表（可重入）。
 */

#include <sdurws/ird/policy/Diagnostics.hpp>

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/policy/Errors.hpp>

namespace sdurws::ird::policy {

// =====================================================================
// 建议码表——枚举 → 原文与全表数组定义于公共头（inline constexpr——迁移 TU
// 的编译期别名与 Errors.cpp 委托需要可见定义；字面量唯一在该 switch 表，
// 本 TU 不持第二份）。本 TU 持有：成员判定谓词＋诊断构造＋辅助组。
// =====================================================================

bool isRegisteredPolicyDiagCode(std::string_view code) noexcept
{
    // 线性扫描（29 项定长小表——查表成本可忽略；无哈希/排序副结构，
    // 保持"一张表即全部事实"的最小实现）。
    for (const std::string_view entry : policyDiagCodes()) {
        if (entry == code) { return true; }
    }
    return false;
}

// =====================================================================
// PolicyDiagnostics——诊断构造（码表核对 → C-3 前置核对 → core 工厂委托）。
// =====================================================================

std::vector<std::string> PolicyDiagnostics::registeredCodes() const
{
    // §9.6 冻结签名返回 string 拷贝（跨单元核对面的承载形态）；顺序＝全表
    // 稳定序——同一实例每次调用、不同实例之间逐字节相同（NFR-COR-02）。
    const PolicyDiagCodeTable& table = policyDiagCodes();
    std::vector<std::string> codes;
    codes.reserve(table.size());
    for (const std::string_view entry : table) {
        codes.emplace_back(entry);
    }
    return codes;
}

core::DiagnosticRecord PolicyDiagnostics::make(
    std::string_view code, std::optional<core::ObjectId> subject,
    std::string context, std::string cause, std::string recommendedAction,
    std::optional<core::ComparativeFields> comparison) const
{
    // ---- 第一步：码表成员核对（§9.6"码值权威在注册表"的本侧拦截面）。
    // 表外码＝调用方契约违约，fail-fast 不静默、不代为发明码面——诊断码
    // 是证据链关联键（ARC-04），来历不明的码进入记录即污染证据。异常码面
    // 取 PolicyObjectInvalid（"工厂即时校验拒绝非法实例"——radUnit() 内部
    // 不变量违约同款先例），what() 携带违规原文便于定位。
    if (!isRegisteredPolicyDiagCode(code)) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "诊断码不在 policy 建议码表（§9.6——码值权威归 diagnostics "
                          "StableCodeRegistry，表外码不得入正式记录）: " + std::string{code});
    }

    // ---- 第二步：C-3 必填串前置核对（context/cause/recommendedAction）。
    // core 工厂对空串抛 CoreError——本接口的错误契约是 PolicyError（§9.6
    // 通用契约行；core 异常类型不向 policy 调用方泄漏，Errors.hpp 转译总纲），
    // 故在本层先行核对并转译为调用方可捕获的本单元异常。
    if (context.empty() || cause.empty() || recommendedAction.empty()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "诊断必填串为空（C-3：context/cause/recommendedAction 均不得"
                          "为空——空文案的诊断不可入正式记录）");
    }

    // ---- 第三步：比较型三要素的单位有效性核对（UX-03"单位必填"）。
    // 无效句柄（默认构造/越权构造的 UnitToken）不能进入下游——core 注册表
    // 查找与 testkit 校验都假定句柄有效；此处拦截把违约定位在构造入口。
    // 数值侧四态不在此核对：Provided/Invalid（原文保留）/NotApplicable
    // （显式不适用）均为 §9.6 认可的显式语义，语义核对归消费侧
    // （checkDiagnosticRecord 按 allowTransient/NotApplicable 状态检查）。
    if (comparison.has_value()) {
        if (!comparison->actual.unit.isValid() || !comparison->expected.unit.isValid()) {
            throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                              "比较型三要素单位无效（UX-03：actual/expected 的 unit "
                              "必须是已注册的有效句柄——无效句柄不得入正式记录）");
        }
    }

    // ---- 第四步：委托 core 工厂（C-3 句法/必填的权威校验点）。
    // 表内码必然满足码句法（全大写 kebab ≤64——清单原文形态），且必填串
    // 已核对——CoreError 在正常路径不可达；仍以 try/catch 防御性转译（不
    // 吞错：转译为 PolicyError 并携带 core 原文），保证"core 异常不向
    // policy 调用方泄漏"的单元契约在任何输入下成立。
    try {
        return core::DiagnosticRecord::make(std::string{code}, std::move(subject),
                                            /*localName=*/std::nullopt,
                                            /*runtimeName=*/std::nullopt,
                                            std::move(context), std::move(cause),
                                            std::move(recommendedAction),
                                            std::move(comparison));
    }
    catch (const core::CoreError& e) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          std::string{"core 诊断工厂拒绝（内部不变量违约，随表内码与"
                                      "前置核对不应可达）: "}
                              + e.what());
    }
}

// =====================================================================
// makeComparative 辅助组。
// =====================================================================

namespace {

/**
 * @brief 双精度值的确定性文本格式（%.17g——round-trip 精确；仅进 Invalid
 *        态的原文保留面；snprintf "C" locale 数字格式无本地小数点——
 *        NFR-COR-02。NaN/±Inf 经 %.17g 输出 "nan"/"inf"/"-inf" 字面量——
 *        与 JointLimits.cpp/Compatibility.cpp 的 cause 文案格式化同款口径）。
 */
std::string formatRawDouble(double v)
{
    char buf[40] = {};
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string{buf};
}

}  // namespace

core::ComparativeValue makeComparativeValue(double value, core::UnitToken unit,
                                            std::string_view methodTag)
{
    core::ComparativeValue side;
    side.unit = unit;
    if (std::isfinite(value)) {
        // 有限值→Provided：数值可信承载＋DerivedReadOnly 溯源（来源标注是
        // 事实记录不构成可信等级——DYN-06；"derived-readonly"语义＝评估
        // 派生只读事实，JointLimits 裕量记录同款）。
        side.quantity = core::SourcedValue<double>::provided(
            value, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {},
                                               {}, std::string{methodTag}));
    }
    else {
        // 非有限值（NaN/±Inf）→Invalid＋原文保留（§7.5/NFR-COR-03：不静默
        // 转 0、不伪造数值——原始字面量是唯一忠实的承载形式）。
        side.quantity = core::SourcedValue<double>::invalid(formatRawDouble(value));
    }
    return side;
}

core::ComparativeValue makeNotApplicableValue(core::UnitToken unit)
{
    // NotApplicable 态（ERR-01 显式标记）：tryValue 恒空——"不适用"与
    // "数值为 0"在类型层面被切开（P-POL-2：无冻结默认即显式不适用）。
    core::ComparativeValue side;
    side.unit = unit;
    side.quantity = core::SourcedValue<double>::notApplicable();
    return side;
}

core::ComparativeFields makeComparative(core::ComparativeValue actual,
                                        core::ComparativeValue expected)
{
    // 纯聚合：实际/期望两侧的状态组合由调用方语义决定（双侧 Provided／
    // 一侧 NotApplicable／一侧 Invalid 保留原文——均为 §9.6 认可形态），
    // 本函数不加校验不改写（构造期核对归 IPolicyDiagnostics::make 第三步）。
    core::ComparativeFields fields;
    fields.actual = std::move(actual);
    fields.expected = std::move(expected);
    return fields;
}

}  // namespace sdurws::ird::policy
