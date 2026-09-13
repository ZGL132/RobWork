/**
 * @file   PolicyParsing.cpp
 * @brief  策略解析管线实现——resolvePolicy 七段流程与 PolicyValidator
 *         （只校验不发布）的唯一实现点（POL-T04）。
 *
 * 设计依据：
 *   - units/policy.md §5.1（解析与发布管线——七段流程/发射顺序/注入接口的
 *     唯一权威章节）、§5.2（校验规则与错误分类表——诊断码/严重级/处置/比较型
 *     三要素逐行）、§4.3/§4.4（子模型语义：无序对/角色词表/域窗/唯一冻结
 *     默认）、§7.2（排除∩必检=∅ 结构保证——解析期可检子集＋会话期完整复核）、
 *     §9.2（IPolicyValidator 契约）、§12 POL-T04 行（本任务产物）
 *   - 需求 ARC-05（策略单一权威——本文件是"原始配置→已发布策略对象"的
 *     唯一通道实现）、ERR-01（诊断三轴正交、比较型三要素）、NFR-COR-02/03
 *     （确定性；不静默、不短路、不发明数值）、SA-12（单位换算唯一权威在 core）
 *   - 任务契约 tasks/foundation/POL-T04.json acceptance 1～2（POL-PARSE-1~6
 *     全量反例不短路；O-10/P-POL-2 保守口径——唯一默认＝4π DefaultAppendixD）
 *
 * 实现纪律（与 PolicyParsing.hpp 契约注释一一对应，实现不引入第二口径）：
 *   1. 诊断发射顺序＝管线段序（版本门 → ①token → ②阈值 → ③默认告知 →
 *      ④规则 → ⑤范围/对象），段内按字段/规则承载序——确定性 NFR-COR-02；
 *      除"版本门命中即返回"（§5.2 行 2"不前向猜测解析"的处置语义）外全量
 *      收集不短路。
 *   2. 错误级判定不依赖 DiagnosticRecord（core 契约无严重级字段——严重级是
 *      §5.2 表中"码→级"的固定映射）：管线内部以 hasError 标志记录是否发射过
 *      错误级诊断；发布（⑦）当且仅当 hasError==false。"合法 ⇔ 无 Error 级
 *      条目"的判定权威＝本标志（码面纪律：PolicyErrorCode 全表为 Error 级、
 *      POLICY-INFO-* 为 Info 级——与 §5.2 表逐行一致）。
 *   3. 单位换算唯一经 core convert（SA-12）——本文件不出现任何换算因子字面量；
 *      core 抛错（CoreError）原文转发进 POLICY-UNIT-MISMATCH 诊断的 cause
 *      （§5.2 行 6"core convert 抛错转发"）。
 *   4. 阈值域窗校验唯一经 PolicyThreshold::make（POL-T02 单点）——本文件
 *      捕获其 PolicyError 并转译为诊断（比较型三要素按域填实际/期望/单位），
 *      非 NonPositive/OutOfRange 码原样重抛（不吞错）。
 *   5. 对端规范化/覆盖等价的键定义与 PolicyInput.cpp 编码规范序同源（§5.3/
 *      §4.3）——解析②产物承载序与 codec 规范序一致，保证
 *      decode(encode(发布产物对应 Raw 输入)) 承载序稳定（测试钉住）。
 *
 * 错误语义：输入非法 → 诊断轨（全量、可恢复）；调用方契约违约（全零
 * policyObject/空锚/结构非法实例——合法解码不可产生）→ fail-fast 抛
 * PolicyError。不吞错、不静默降级（NFR-COR-03）。
 *
 * 线程安全：全部函数无共享可变状态（context 只读查询）；可重入（NFR-COR-02）。
 * 确定性：无 I/O、无时间/随机源；数值文本经 %.17g 固定格式（snprintf 数字
 * 格式无千分位依赖——与 PolicySet.hpp formatThreshold 同款口径）。
 */

#include <sdurws/ird/policy/PolicyParsing.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/core/Units.hpp>

namespace sdurws::ird::policy {

namespace {

// =====================================================================
// 诊断构造辅助（本文件唯一的 DiagnosticRecord 组装点——码面/文案单源）。
// =====================================================================

/// 告知性默认填充码（§5.2 行 12/§9.6 唯一已登记 INFO 码——不在
/// PolicyErrorCode 错误码表内，直接使用 §9.6 建议码原文）。
constexpr std::string_view kInfoDefaultAppliedCode = "POLICY-INFO-DEFAULT-APPLIED";

/**
 * @brief 数值的确定性文本格式（%.17g——round-trip 精确；仅进诊断文案，
 *        非持久化契约面）。
 *
 * 确定性说明：snprintf 的数字格式在默认 "C" locale 下无千分位/本地小数点
 * （与 PolicySet.hpp formatThreshold 同款口径——进程不切 locale 是既定纪律）。
 */
std::string formatNumber(double v)
{
    char buf[40] = {};
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string{buf};
}

/**
 * @brief 组装一条错误级诊断（码面＝PolicyErrorCode 的建议注册码——§9.6）。
 *
 * @param code      [in] 稳定错误码（registryCode 映射为诊断码串）
 * @param subject   [in] 主体对象（稳定诊断绑定对象——ERR-01；规则类诊断绑
 *                  策略对象、对象缺失类绑缺失对象 ID——见各发射点）
 * @param localName [in] 定位字段（点路径＋规范化下标——如
 *                  "collision.mandatoryPairs[0].first"；空串合法＝无字段定位）
 * @param context   [in] 上下文描述（非空——C-3）
 * @param cause     [in] 原因（非空——C-3；含原文/转发文本）
 * @param action    [in] 建议动作（非空——C-3）
 * @param comparison [in] 比较型三要素（仅 §5.2 要求比较型的码填写——ERR-01）
 */
core::DiagnosticRecord makeErrorDiag(PolicyErrorCode code, core::ObjectId subject,
                                     std::string localName, std::string context,
                                     std::string cause, std::string action,
                                     std::optional<core::ComparativeFields> comparison = std::nullopt)
{
    return core::DiagnosticRecord::make(
        std::string{registryCode(code)}, subject,
        localName.empty() ? std::nullopt : std::optional<std::string>(std::move(localName)),
        std::nullopt,   // runtimeName：解析期无⑥端口名称上下文——会话侧诊断才携带
        std::move(context), std::move(cause), std::move(action), std::move(comparison));
}

/// 比较型单侧值（UX-03/ERR-01）：SI 真值＋已注册 SI 单位 token。
core::ComparativeValue comparativeValue(double siValue, core::UnitToken unit)
{
    core::ComparativeValue v;
    // 来源标注：解析期诊断的实际值来自用户/模板提供的策略输入——UserProvided
    // 是五类词表中最贴近的承载（值来源事实记录，不构成可信等级——DYN-06）。
    v.quantity = core::SourcedValue<double>::provided(
        siValue, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    v.unit = unit;
    return v;
}

/// 组装比较型三要素（实际/期望两侧同单位——§7.4"比较一律对 SI"）。
core::ComparativeFields comparison(double actualSi, double expectedSi, core::UnitToken unit)
{
    core::ComparativeFields f;
    f.actual = comparativeValue(actualSi, unit);
    f.expected = comparativeValue(expectedSi, unit);
    return f;
}

/// 字段域的 SI 单位 token（§4.4 逐字段标注：m/无量纲"1"/rad——已注册词表）。
core::UnitToken siUnitToken(PolicyThresholdDomain domain)
{
    switch (domain) {
    case PolicyThresholdDomain::SafetyClearance:
        return *core::UnitToken::find("m");    // 安全间距：SI m
    case PolicyThresholdDomain::NearLimitRatio:
    case PolicyThresholdDomain::ConditionNumberWarning:
        return *core::UnitToken::find("1");    // 无量纲（注册表 token "1"）
    case PolicyThresholdDomain::FiniteRotationTravelLimit:
        return *core::UnitToken::find("rad");  // 行程上限：SI rad（不是度）
    }
    return *core::UnitToken::find("1");        // 不可达（全枚举覆盖——编译器出口）
}

/**
 * @brief 域窗的期望界（比较型诊断"期望"侧；方向语义在 cause 文案中显式说明）。
 *
 * @return 期望界 SI 值；nullopt＝该码对该域无单一数值界（理论不可达——防御）
 */
std::optional<double> expectedBound(PolicyThresholdDomain domain, PolicyErrorCode code)
{
    // 各域合法窗（PolicyThresholdDomain 注释）：SafetyClearance [0,+∞)；
    // NearLimitRatio (0,1]；ConditionNumberWarning [1,+∞)；行程上限 (0,+∞)。
    // 非正类诊断的期望界一律为 0（方向由 cause 说明">0"或"≥0"）；
    // 越窗类诊断的期望界为被越的窗界（1）。
    switch (code) {
    case PolicyErrorCode::ThresholdNonPositive:
        (void)domain;
        return 0.0;
    case PolicyErrorCode::ThresholdOutOfRange:
        return 1.0;   // 仅 NearLimitRatio(≤1)/ConditionNumberWarning(≥1) 会发越窗码
    default:
        return std::nullopt;
    }
}

/// 域窗的中文窗描述（诊断 cause 用——与 PolicyThreshold::make 消息同口径）。
std::string_view domainWindowText(PolicyThresholdDomain domain)
{
    switch (domain) {
    case PolicyThresholdDomain::SafetyClearance:
        return "安全间距域窗 [0,+∞) m";
    case PolicyThresholdDomain::NearLimitRatio:
        return "近限位比域窗 (0,1]（无量纲）";
    case PolicyThresholdDomain::ConditionNumberWarning:
        return "条件数警告域窗 [1,+∞)（无量纲）";
    case PolicyThresholdDomain::FiniteRotationTravelLimit:
        return "行程上限域窗 (0,+∞) rad";
    }
    return "未知域窗";
}

// =====================================================================
// 场景对象角色词表（§4.3/§6.1 同源五值——解析①核对表与覆盖等价判定共用）。
// =====================================================================

constexpr std::string_view kRoleTokens[] = {
    "RobotLink", "Tool", "Payload", "EnvironmentObject", "Workpiece",
};

bool isRegisteredRoleToken(std::string_view token)
{
    for (const std::string_view t : kRoleTokens) {
        if (t == token) {
            return true;
        }
    }
    return false;
}

// =====================================================================
// 对端规范化与覆盖等价（键定义与 PolicyInput.cpp 编码规范序同源——见文件头
// 实现纪律 5；本文件按同一 ScopeTarget 规范键实现解析②的字典序规范化）。
// =====================================================================

/**
 * @brief ScopeTarget 规范键比较（严格弱序；键＝(kind, kind 内载荷)——kind
 *        枚举序 Object<Role<Group；kind 内按载荷字节字典序，无 locale）。
 */
bool scopeTargetLess(const ScopeTarget& a, const ScopeTarget& b)
{
    if (a.kind != b.kind) {
        return static_cast<std::uint8_t>(a.kind) < static_cast<std::uint8_t>(b.kind);
    }
    switch (a.kind) {
    case ScopeTargetKind::Object:
        return a.object.bytes < b.object.bytes;   // Id128 字节字典序
    case ScopeTargetKind::Role:
        return a.roleToken < b.roleToken;         // 字节字典序
    case ScopeTargetKind::Group:
        return a.groupName < b.groupName;
    }
    return false;   // 不可达（全枚举覆盖——编译器出口）
}

/// 无序对端规范化：返回 {较小端, 较大端}（{a,b}≡{b,a}——§4.3 无序对语义）。
std::pair<ScopeTarget, ScopeTarget> orderedPairEnds(const ScopeTarget& first,
                                                    const ScopeTarget& second)
{
    if (scopeTargetLess(second, first)) {
        return {second, first};
    }
    return {first, second};
}

/**
 * @brief 规则列表规范化：无序对端排序＋列表按规范化键升序（拷贝入参——
 *        RawPolicyInput 按原样承载是公共契约，解析不改写调用方数据）。
 *
 * 列表键＝规范化后的 (first, second, level, reason) 字典序——与 codec 编码
 * 规范序同键（实现纪律 5），解析产物承载序即编码规范序。
 */
std::vector<PairRule> normalizedPairRules(const std::vector<PairRule>& rules)
{
    std::vector<PairRule> out;
    out.reserve(rules.size());
    for (const auto& r : rules) {
        PairRule copy = r;
        const auto ends = orderedPairEnds(copy.first, copy.second);
        copy.first = ends.first;
        copy.second = ends.second;
        out.push_back(std::move(copy));
    }
    std::sort(out.begin(), out.end(), [](const PairRule& a, const PairRule& b) {
        if (scopeTargetLess(a.first, b.first) || scopeTargetLess(b.first, a.first)) {
            return scopeTargetLess(a.first, b.first);
        }
        if (scopeTargetLess(a.second, b.second) || scopeTargetLess(b.second, a.second)) {
            return scopeTargetLess(a.second, b.second);
        }
        if (a.level != b.level) {
            return static_cast<std::uint8_t>(a.level) < static_cast<std::uint8_t>(b.level);
        }
        return a.reason < b.reason;
    });
    return out;
}

/**
 * @brief 单端"覆盖等价"判定（§5.2 行 8"经 Role/Group 展开的等价对"在解析期
 *        的可检子集——详见单元卡 v0.5 增量登记）。
 *
 * 两端覆盖等价 ⇔ 它们的展开对象集在"解析期可见信息"下必然相交：
 *   - 同 kind 同载荷：Object 同 ID／Role 同 token／Group 同名（精确等值——
 *     附录 D 第 12 项，无容差）；
 *   - Object×Role：对象的建模角色（context.objectRole）== Role 的 token——
 *     Role 端展开必然包含该对象，两端展开集必相交；
 *   - Object×Group／Role×Group：成员级交集需场景清单（解析期上下文无组成员
 *     查询）→ 返回 false——该子集由会话构建与场景清单求交后以同码完整复核
 *     （§7.2 会话期冲突检查——两层防御，解析期不虚报可检能力）。
 */
bool endsCoverEquivalent(const ScopeTarget& a, const ScopeTarget& b,
                         const IPolicyValidationContext& context)
{
    if (a.kind == b.kind) {
        switch (a.kind) {
        case ScopeTargetKind::Object:
            return a.object == b.object;             // Id128 精确等值
        case ScopeTargetKind::Role:
            return a.roleToken == b.roleToken;       // token 字节精确等值
        case ScopeTargetKind::Group:
            return a.groupName == b.groupName;       // 组名字节精确等值
        }
        return false;
    }
    // 异 kind：仅 Object×Role 可经建模角色判定（Group 成员不可见——见函数注释）。
    const ScopeTarget* obj = nullptr;
    const ScopeTarget* role = nullptr;
    if (a.kind == ScopeTargetKind::Object && b.kind == ScopeTargetKind::Role) {
        obj = &a;
        role = &b;
    } else if (a.kind == ScopeTargetKind::Role && b.kind == ScopeTargetKind::Object) {
        obj = &b;
        role = &a;
    }
    if (obj == nullptr) {
        return false;   // 含 Group 的异 kind 组合——解析期不可判定
    }
    // 角色查询失败（对象不存在/无角色）→ 不等价（不猜测——ARC-04）；
    // 对象存在性本身由⑤的 POLICY-SCOPE-OBJECT-MISSING 检查单独报告。
    const std::optional<std::string> objectRole = context.objectRole(obj->object);
    return objectRole.has_value() && *objectRole == role->roleToken;
}

/**
 * @brief 规则对"对端覆盖等价"判定：{a1,a2} 与 {b1,b2} 覆盖同一对象对 ⇔
 *        端按同序或交叉序覆盖等价（无序对语义——两种摆法都要查）。
 */
bool pairsCoverEquivalent(const PairRule& a, const PairRule& b,
                          const IPolicyValidationContext& context)
{
    return (endsCoverEquivalent(a.first, b.first, context)
            && endsCoverEquivalent(a.second, b.second, context))
        || (endsCoverEquivalent(a.first, b.second, context)
            && endsCoverEquivalent(a.second, b.first, context));
}

/// 规则级别中文名（诊断文案用——Must/Should 的 §4.3 语义）。
std::string_view levelText(PolicyRuleLevel level)
{
    return level == PolicyRuleLevel::Must ? "必检（Must）" : "可过滤（Should）";
}

// =====================================================================
// 集合规范化（升序＋去重——集合语义；键比较与 codec 同源）。
// =====================================================================

template <typename T, typename Compare>
std::vector<T> normalizedSet(const std::vector<T>& items, Compare comp)
{
    std::vector<T> out(items);
    std::sort(out.begin(), out.end(), comp);
    out.erase(std::unique(out.begin(), out.end(),
                          [&comp](const T& a, const T& b) { return !comp(a, b) && !comp(b, a); }),
              out.end());
    return out;
}

// =====================================================================
// 解析管线（resolvePolicy 与 PolicyValidator::validate 共享的单一实现）。
// =====================================================================

/**
 * @brief 单次解析的完整产物（内部载体——发布对象＋全量诊断＋错误标志）。
 */
struct ParseOutcome {
    std::optional<EngineeringPolicySet> policy;      ///< 仅当无错误级诊断非空
    std::vector<core::DiagnosticRecord> diagnostics; ///< 全量（发射序＝管线段序）
    bool hasError = false;                           ///< 是否发射过错误级诊断（实现纪律 2）
};

/**
 * @brief 解析单个阈值槽位（②规范化与单位校验——行 3/4/6 的实现点）。
 *
 * 处理序（固定——分类保真优先于段序字面：NaN 的行 3 分类必须先于单位行 6，
 * 否则 NaN 输入会被 core convert 的非有限拒绝误分类为单位错误）：
 *   1. 非有限原文 → POLICY-THRESHOLD-NON-FINITE（原文保留于 cause——行 3）；
 *   2. 显示单位归一 SI：空 token＝字段域默认 SI 单位（factor 1——RawThresholdInput
 *      注释"按字段域默认单位核对"）；未注册 token／量纲不符／溢出 →
 *      POLICY-UNIT-MISMATCH（core convert 抛错原文转发——行 6）；
 *   3. PolicyThreshold::make 域窗校验（POL-T02 单点）——非正/越窗转译为
 *      比较型诊断（实际 SI/期望界/字段 SI 单位——行 4，ERR-01）。
 *
 * @param raw        [in] 原始阈值（nullopt＝未提供——调用方处理默认/必填语义）
 * @param domain     [in] 目标字段域（决定 SI 单位与域窗）
 * @param fieldPath  [in] 定位字段点路径（诊断 localName）
 * @param subject    [in] 诊断主体（策略对象）
 * @param context    [in] 上下文文案前缀（诊断 context）
 * @param outcome    [in,out] 累积产物（诊断追加；hasError 同步）
 * @return 归一后的阈值（槽位未提供或校验失败 → nullopt——失败时必有诊断，
 *         发布被 hasError 阻止，nullopt 不会被发布路径消费）
 */
std::optional<PolicyThreshold> resolveThresholdSlot(const std::optional<RawThresholdInput>& raw,
                                                    PolicyThresholdDomain domain,
                                                    std::string_view fieldPath,
                                                    core::ObjectId subject,
                                                    std::string_view contextPrefix,
                                                    ParseOutcome& outcome)
{
    if (!raw.has_value()) {
        return std::nullopt;   // 未提供——默认/必填语义归调用方（③/行 5）
    }
    const RawThresholdInput& slot = *raw;
    const core::UnitToken siUnit = siUnitToken(domain);

    // 第 1 步：非有限原文先判（行 3——NaN/±Inf 拒绝，原文保留）。
    // 注意顺序：必须在 core convert 之前——convert 对非有限输入抛
    // CoreError，若先换算会把行 3 的阈值错误误分类为行 6 的单位错误。
    if (!std::isfinite(slot.value)) {
        outcome.hasError = true;
        outcome.diagnostics.push_back(makeErrorDiag(
            PolicyErrorCode::ThresholdNonFinite, subject, std::string{fieldPath},
            std::string{contextPrefix},
            "阈值为非有限值（NaN/±Inf），原文 " + formatNumber(slot.value)
                + (slot.unitToken.empty() ? "（未携带单位 token）"
                                          : " 单位 '" + slot.unitToken + "'")
                + "（原文保留于 RawPolicyInput——NFR-COR-03）",
            "修正为有限数值后重新解析（比较型阈值不接受 NaN/±Inf）"));
        return std::nullopt;
    }

    // 第 2 步：显示单位归一 SI（SA-12——换算唯一经 core convert）。
    double siValue = 0.0;
    if (slot.unitToken.empty()) {
        // 空 token＝输入未提供单位——按字段域默认 SI 单位解释（factor 1；
        // RawThresholdInput 注释"空 token 合法承载"的解析期落点）。
        siValue = slot.value;
    } else {
        const std::optional<core::UnitToken> unit = core::UnitToken::find(slot.unitToken);
        if (!unit.has_value()) {
            // 未注册 token（行 6"未注册 token"分支）：无换算可转发——
            // 直接给词表核对失败的 cause（字段域期望量纲随文说明）。
            outcome.hasError = true;
            outcome.diagnostics.push_back(makeErrorDiag(
                PolicyErrorCode::UnitMismatch, subject, std::string{fieldPath},
                std::string{contextPrefix},
                "未注册单位 token '" + slot.unitToken + "'（字段域期望量纲："
                    + std::string{domainWindowText(domain)} + "）",
                "改用已注册单位 token（如 m/mm/rad/deg/1）后重新解析"));
            return std::nullopt;
        }
        // 已注册 token——经 core convert 归一；任何 CoreError（量纲不符/
        // 溢出等）原文转发为行 6 诊断（"core convert 错误转发"）。
        try {
            siValue = core::convert(slot.value, *unit, siUnit);
        } catch (const core::CoreError& e) {
            outcome.hasError = true;
            outcome.diagnostics.push_back(makeErrorDiag(
                PolicyErrorCode::UnitMismatch, subject, std::string{fieldPath},
                std::string{contextPrefix},
                "单位归一失败（输入 " + formatNumber(slot.value) + " '"
                    + slot.unitToken + "' → 字段 SI 单位）：core convert 转发——"
                    + e.what(),
                "修正单位量纲与字段域一致后重新解析（SA-12：换算唯一权威在 core）"));
            return std::nullopt;
        }
    }

    // 第 3 步：域窗校验（PolicyThreshold::make 单点——实现纪律 4）。
    // 来源＝Explicit：解析③只给"未提供"的行程上限标 DefaultAppendixD，
    // 显式提供的值一律 Explicit（§4.4 来源三分）。
    try {
        return PolicyThreshold::make(siValue, PolicyValueOrigin::Explicit, domain);
    } catch (const PolicyError& e) {
        // 仅转译行 4 的两码（本层入参已保证有限——NonFinite 理论不可达，
        // 其余码原样重抛，不吞错）。
        if (e.code() != PolicyErrorCode::ThresholdNonPositive
            && e.code() != PolicyErrorCode::ThresholdOutOfRange) {
            throw;
        }
        outcome.hasError = true;
        std::optional<core::ComparativeFields> cmp;
        const std::optional<double> bound = expectedBound(domain, e.code());
        if (bound.has_value()) {
            cmp = comparison(siValue, *bound, siUnit);
        }
        outcome.diagnostics.push_back(makeErrorDiag(
            e.code(), subject, std::string{fieldPath}, std::string{contextPrefix},
            "阈值越域（归一后 " + formatNumber(siValue) + " SI，原文 "
                + formatNumber(slot.value)
                + (slot.unitToken.empty() ? "（未携带单位 token）"
                                          : " '" + slot.unitToken + "'")
                + "）：期望 " + std::string{domainWindowText(domain)},
            "修正到字段域窗内后重新解析（比较型三要素见 comparison——ERR-01）",
            std::move(cmp)));
        return std::nullopt;
    }
}

/**
 * @brief 七段解析管线的单一实现（resolvePolicy 与 validate 共享——两入口
 *        诊断逐字一致的机制保证）。
 *
 * 段序与发射顺序见 PolicyParsing.hpp resolvePolicy 注释（唯一权威口径）。
 */
ParseOutcome runParsePipeline(const RawPolicyInput& input,
                              const IPolicyValidationContext& context)
{
    ParseOutcome outcome;
    const core::ObjectId subject = input.policyObject;   // 诊断主体默认＝策略对象

    // ---- 契约违约复检（fail-fast，不入诊断轨——合法解码不可产生的实例） ----
    // 全零 policyObject／空锚：project 分配/§4.2 必填列的装配侧契约。
    if (!input.policyObject.isValid()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "policyObject 身份无效（全零保留值——project 分配契约违约）");
    }
    if (input.numericContractAnchor.empty()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "numericContractAnchor 为空（§4.2 必填列——装配契约违约）");
    }
    // 规则结构复检（同 PolicyCodec::encode 口径——detail 谓词单源）：
    // reason 空/kind↔字段不一致在装配层（PairRule::make）已拒绝，此处为
    // 防御性复检（聚合初始化可绕过工厂——复检保证 fail-fast 面完整）。
    const std::string_view where = "resolvePolicy";
    for (const auto& r : input.collision.mandatoryPairs) {
        detail::requirePairRuleWellFormed(r, where);
    }
    for (const auto& r : input.collision.excludedPairs) {
        detail::requirePairRuleWellFormed(r, where);
    }

    // ---- 版本门（§5.2 行 2）：未来/未知代拒绝——不前向猜测解析字段 ----
    // 行 2 处置语义：未知布局的字段解释无意义且危险（可能把未知字节当合法
    // 值），故命中即返回（比较型：实际/期望代——schema 代无量纲，单位 "1"）。
    if (input.schemaVersion > kPolicySchemaVersionCurrent) {
        outcome.hasError = true;
        outcome.diagnostics.push_back(makeErrorDiag(
            PolicyErrorCode::SchemaVersionFuture, subject, "schemaVersion",
            "schema 校验（§5.2 行 2）",
            "策略 schema 代 " + formatNumber(static_cast<double>(input.schemaVersion))
                + " 高于当前代 " + formatNumber(static_cast<double>(kPolicySchemaVersionCurrent))
                + "（未来版本不前向猜测解析——PM-06 同源只读拒绝）",
            "用当前版本应用打开并重新保存该策略（升级指引——PM-06 同源；"
            "不自动升级、不降级解析）",
            comparison(static_cast<double>(input.schemaVersion),
                       static_cast<double>(kPolicySchemaVersionCurrent),
                       *core::UnitToken::find("1"))));
        return outcome;
    }
    if (input.schemaVersion < kPolicySchemaVersionCurrent) {
        outcome.hasError = true;
        outcome.diagnostics.push_back(makeErrorDiag(
            PolicyErrorCode::SchemaVersionUnknown, subject, "schemaVersion",
            "schema 校验（§5.2 行 2）",
            "策略 schema 代 " + formatNumber(static_cast<double>(input.schemaVersion))
                + " 低于已知最低代 "
                + formatNumber(static_cast<double>(kPolicySchemaVersionCurrent))
                + "（未知旧代——不猜测其字段布局）",
            "用写入该代的版本应用升级后再以当前版本保存（PM-06 同源）",
            comparison(static_cast<double>(input.schemaVersion),
                       static_cast<double>(kPolicySchemaVersionCurrent),
                       *core::UnitToken::find("1"))));
        return outcome;
    }

    // ---- ① 语法与 schema 校验：角色 token 词表核对（行 1 的活性检测面） ----
    // Role 端 token 不在五值词表 → POLICY-SCHEMA-UNKNOWN-FIELD：拼写错误的
    // 角色会静默收窄必检集（展开为空集），必须拒绝而非忽略（行 1"不静默
    // 忽略——防拼写错误降级为默认值"）。强类型字段的未知键在类型层不可表达，
    // 字节层等价拒绝由 PolicyCodec::decode 精确耗尽契约承担（头文件落点登记）。
    const auto checkRoleVocabulary = [&](const std::vector<PairRule>& rules,
                                         std::string_view listPath) {
        for (std::size_t i = 0; i < rules.size(); ++i) {
            // 端序按输入承载序核对（定位面向调用方的原文——规范化仅用于④判定）。
            const ScopeTarget* ends[2] = {&rules[i].first, &rules[i].second};
            for (int end = 0; end < 2; ++end) {
                const ScopeTarget& t = *ends[end];
                if (t.kind == ScopeTargetKind::Role
                    && !isRegisteredRoleToken(t.roleToken)) {
                    outcome.hasError = true;
                    // 定位下标＝输入承载序下标（词表核对面向调用方原文）。
                    const std::string localName = std::string{listPath} + "["
                        + std::to_string(i) + (end == 0 ? "].first" : "].second");
                    outcome.diagnostics.push_back(makeErrorDiag(
                        PolicyErrorCode::SchemaUnknownField, subject, localName,
                        "语法与 schema 校验（§5.1①/§5.2 行 1）",
                        "角色 token '" + t.roleToken
                            + "' 不在场景对象角色词表（RobotLink|Tool|Payload|"
                              "EnvironmentObject|Workpiece——§6.1 同源）",
                        "修正为词表内角色 token（拼写错误会静默收窄必检集——"
                        "不静默忽略）"));
                }
            }
        }
    };
    checkRoleVocabulary(input.collision.mandatoryPairs, "collision.mandatoryPairs");
    checkRoleVocabulary(input.collision.excludedPairs, "collision.excludedPairs");

    // ---- ② 规范化与单位校验（逐槽位，struct 序——确定性发射序） ----
    // 安全间距（P-POL-2：无冻结默认——缺失＋enabled 的必填拒绝在槽位缺省分支）。
    std::optional<PolicyThreshold> safetyClearance = resolveThresholdSlot(
        input.collision.safetyClearance, PolicyThresholdDomain::SafetyClearance,
        "collision.safetyClearance", subject, "阈值规范化与单位校验（§5.1②/§5.2 行 3/4/6）",
        outcome);
    if (input.collision.enabled && !input.collision.safetyClearance.has_value()) {
        // 行 5：必填阈值缺省——无冻结默认项不得静默省略（P-POL-2 保守口径，
        // 契约 acceptance 2 的拒绝面；enabled=false 时间距检查随总开关停用）。
        outcome.hasError = true;
        outcome.diagnostics.push_back(makeErrorDiag(
            PolicyErrorCode::ThresholdRequiredMissing, subject,
            "collision.safetyClearance", "阈值规范化与单位校验（§5.2 行 5）",
            "碰撞已启用而安全间距未设置（无冻结默认——P-POL-2 保守口径："
            "缺失即拒绝发布，不发明数值）",
            "显式设置安全间距后重新解析（如需空缺语义走 P-POL-2 裁决——"
            "解析器不越权）"));
    }
    // 近限位比／条件数警告（P-POL-2：无冻结默认——未提供保持 nullopt＝显式
    // 不适用，不发明数值、不设第二默认——契约 acceptance 2）。
    std::optional<PolicyThreshold> nearLimitRatio = resolveThresholdSlot(
        input.jointThresholds.nearLimitRatio, PolicyThresholdDomain::NearLimitRatio,
        "jointThresholds.nearLimitRatio", subject,
        "阈值规范化与单位校验（§5.1②/§5.2 行 3/4/6）", outcome);
    std::optional<PolicyThreshold> conditionNumberWarning = resolveThresholdSlot(
        input.jointThresholds.conditionNumberWarning,
        PolicyThresholdDomain::ConditionNumberWarning,
        "jointThresholds.conditionNumberWarning", subject,
        "阈值规范化与单位校验（§5.1②/§5.2 行 3/4/6）", outcome);
    // 行程上限（唯一冻结默认 4π 的显式提供分支——缺省分支在③）。
    std::optional<PolicyThreshold> travelLimit = resolveThresholdSlot(
        input.jointThresholds.finiteRotationTravelLimit,
        PolicyThresholdDomain::FiniteRotationTravelLimit,
        "jointThresholds.finiteRotationTravelLimit", subject,
        "阈值规范化与单位校验（§5.1②/§5.2 行 3/4/6）", outcome);

    // ---- ③ 默认值解析（O-10/P-POL-2 保守口径——契约 acceptance 2） ----
    // 唯一冻结默认＝附录 D 第 11 项行程上限 4π rad（kDefaultFiniteRotationTravelLimit
    // ——与 PolicySet.hpp 同源常量，无第二数值源）。输入未提供 → 填入并标
    // DefaultAppendixD＋告知性诊断（§5.2 行 12）；其余槽位无冻结默认——
    // 保持 nullopt（上面未提供分支已返回 nullopt，此处不补任何数值）。
    if (!travelLimit.has_value() && !input.jointThresholds.finiteRotationTravelLimit.has_value()) {
        // 输入确实未提供（而非提供但校验失败——后者已发错误诊断，不发布）。
        travelLimit = JointThresholds::defaultFiniteRotationTravelLimit();
        core::DiagnosticRecord info = core::DiagnosticRecord::make(
            std::string{kInfoDefaultAppliedCode}, subject,
            std::optional<std::string>{"jointThresholds.finiteRotationTravelLimit"},
            std::nullopt, "默认值解析（§5.1③/§5.2 行 12）",
            "有限限位旋转关节行程上限未提供，已按附录 D 第 11 项唯一冻结默认 "
            "4π rad 填入（origin=DefaultAppendixD）",
            "如需其他上限请显式设置（策略编辑命令——UX-08 入口）");
        outcome.diagnostics.push_back(std::move(info));
    }

    // ---- ④ 规则集检查（行 7 重复／行 8 冲突／行 9 组引用） ----
    // 规范化拷贝（端序＋列表序——判定与发布共用同一承载序，发射序确定）。
    const std::vector<PairRule> mandatory = normalizedPairRules(input.collision.mandatoryPairs);
    const std::vector<PairRule> excluded = normalizedPairRules(input.collision.excludedPairs);

    // 行 7：重复规则——同一清单内"级别相同且对端覆盖等价"的后续条目
    // （成对比较：覆盖等价非传递有序，相邻比较会漏——O(n²)，策略规模可接受）。
    const auto checkDuplicates = [&](const std::vector<PairRule>& rules,
                                     std::string_view listPath) {
        for (std::size_t j = 0; j < rules.size(); ++j) {
            for (std::size_t i = 0; i < j; ++i) {
                if (rules[i].level == rules[j].level
                    && pairsCoverEquivalent(rules[i], rules[j], context)) {
                    // 每个重复条目发一条（列出重复双方——"列出重复条目"）；
                    // 同一条目与多条先前条目等价时只发一次（异常在"重复登记"）。
                    outcome.hasError = true;
                    const std::string localName = std::string{listPath} + "["
                        + std::to_string(j) + "]";
                    outcome.diagnostics.push_back(makeErrorDiag(
                        PolicyErrorCode::RuleDuplicate, subject, localName,
                        "规则集检查（§5.2 行 7）",
                        "与条目 [" + std::to_string(i) + "] 为同一对同类型重复登记"
                        "（级别 " + std::string{levelText(rules[j].level)}
                            + "；理由分别为 '" + rules[i].reason + "' / '"
                            + rules[j].reason + "'）",
                        "删除重复条目（同对同类型只保留一条——理由合并到保留条目）"));
                    break;
                }
            }
        }
    };
    checkDuplicates(mandatory, "collision.mandatoryPairs");
    checkDuplicates(excluded, "collision.excludedPairs");

    // 行 8：排除∩必检冲突——两清单间存在对端覆盖等价的规则对（级别无关——
    // "排除覆盖必检"即结构矛盾；定位冲突双方）。
    for (std::size_t i = 0; i < mandatory.size(); ++i) {
        for (std::size_t j = 0; j < excluded.size(); ++j) {
            if (pairsCoverEquivalent(mandatory[i], excluded[j], context)) {
                outcome.hasError = true;
                outcome.diagnostics.push_back(makeErrorDiag(
                    PolicyErrorCode::RuleConflict, subject,
                    "collision.mandatoryPairs[" + std::to_string(i) + "] × collision.excludedPairs["
                        + std::to_string(j) + "]",
                    "规则集检查（§5.2 行 8/§7.2）",
                    "排除规则覆盖必检对（经 Role/Group 展开等价——必检理由 '"
                        + mandatory[i].reason + "'，排除理由 '" + excluded[j].reason
                        + "'）：结构上不允许存在可隐藏必检的合法策略（D-06）",
                    "删除冲突的排除规则（必检对不可过滤；如确需排除先修订必检"
                    "登记——走策略编辑命令）"));
            }
        }
    }

    // 行 9：组引用——Group 端组名必须已在本策略定义（context 应答）；未定义
    // → POLICY-RULE-CYCLE（循环/未定义组共用本码；"组引用组"在扁平组 schema
    // 层不可表达，本检查为其纵深防御的活性面——头文件落点登记）。
    const auto checkGroupDefined = [&](const std::vector<PairRule>& rules,
                                       std::string_view listPath) {
        for (std::size_t i = 0; i < rules.size(); ++i) {
            const ScopeTarget* ends[2] = {&rules[i].first, &rules[i].second};
            for (int end = 0; end < 2; ++end) {
                const ScopeTarget& t = *ends[end];
                if (t.kind == ScopeTargetKind::Group
                    && !context.groupDefined(t.groupName)) {
                    outcome.hasError = true;
                    const std::string localName = std::string{listPath} + "["
                        + std::to_string(i) + (end == 0 ? "].first" : "].second");
                    outcome.diagnostics.push_back(makeErrorDiag(
                        PolicyErrorCode::RuleCycle, subject, localName,
                        "规则集检查（§5.2 行 9）",
                        "引用未定义组 '" + t.groupName
                            + "'（组仅可引用对象、不可引用组——扁平组 schema 层"
                              "无嵌套，本检查为纵深防御）",
                        "先在本策略定义该组或改用对象/角色目标"));
                }
            }
        }
    };
    checkGroupDefined(mandatory, "collision.mandatoryPairs");
    checkGroupDefined(excluded, "collision.excludedPairs");

    // ---- ⑤ 适用范围验证（行 10 对象存在性／行 11 空域启用） ----
    // 规则 Object 端存在性（经 context——闭包内查无＝已删除/跨修订混入；
    // subject 绑定缺失对象 ID——POL-PARSE-6）。
    const auto checkObjectExists = [&](const std::vector<PairRule>& rules,
                                       std::string_view listPath) {
        for (std::size_t i = 0; i < rules.size(); ++i) {
            const ScopeTarget* ends[2] = {&rules[i].first, &rules[i].second};
            for (int end = 0; end < 2; ++end) {
                const ScopeTarget& t = *ends[end];
                if (t.kind == ScopeTargetKind::Object
                    && !context.objectExists(t.object)) {
                    outcome.hasError = true;
                    const std::string localName = std::string{listPath} + "["
                        + std::to_string(i) + (end == 0 ? "].first" : "].second");
                    outcome.diagnostics.push_back(makeErrorDiag(
                        PolicyErrorCode::ScopeObjectMissing, t.object, localName,
                        "适用范围验证（§5.2 行 10）",
                        "规则适用对象在修订闭包内不存在（对象已删除或跨修订混入"
                        "——身份 " + t.object.toCanonical() + "）",
                        "从规则中移除该对象或恢复其所在修订（定位字段见 localName）"));
                }
            }
        }
    };
    checkObjectExists(mandatory, "collision.mandatoryPairs");
    checkObjectExists(excluded, "collision.excludedPairs");

    // 适用范围对象集存在性（model/task/case 顺序发射；下标按规范化序——与
    // 发布对象承载序一致，定位下标即发布对象内下标）。
    const auto checkApplicabilityObjects = [&](const std::vector<core::ObjectId>& objects,
                                               std::string_view fieldPath) {
        const std::vector<core::ObjectId> normalized = normalizedSet(
            objects, [](const core::ObjectId& a, const core::ObjectId& b) {
                return a.bytes < b.bytes;
            });
        for (std::size_t i = 0; i < normalized.size(); ++i) {
            if (!context.objectExists(normalized[i])) {
                outcome.hasError = true;
                const std::string localName = std::string{fieldPath} + "["
                    + std::to_string(i) + "]";
                outcome.diagnostics.push_back(makeErrorDiag(
                    PolicyErrorCode::ScopeObjectMissing, normalized[i], localName,
                    "适用范围验证（§5.2 行 10）",
                    "适用范围对象在修订闭包内不存在（对象已删除或跨修订混入"
                    "——身份 " + normalized[i].toCanonical() + "）",
                    "从适用范围中移除该对象或恢复其所在修订"));
            }
        }
    };
    checkApplicabilityObjects(input.applicability.modelObjects, "applicability.modelObjects");
    checkApplicabilityObjects(input.applicability.taskObjects, "applicability.taskObjects");
    checkApplicabilityObjects(input.applicability.caseObjects, "applicability.caseObjects");

    // 行 11：空域启用（§5.1④"无域可检"——enabledDomains 空集无法与"显式
    // 清空"区分，§5.1④ 为解析期权威语义：不填默认、直接拒绝；{Self,
    // Environment,Tool} 的模板默认归装配侧——头文件落点登记）。
    if (input.collision.enabled && input.collision.enabledDomains.empty()) {
        outcome.hasError = true;
        outcome.diagnostics.push_back(makeErrorDiag(
            PolicyErrorCode::ApplicabilityInvalid, subject, "collision.enabledDomains",
            "适用范围验证（§5.2 行 11/§5.1④）",
            "碰撞已启用而启用域为空（无域可检——空集与显式清空不可区分，"
            "§5.1④ 权威语义为拒绝）",
            "显式设置启用域（Self/Environment/Tool/Scene 子集）后重新解析"));
    }
    // modes：core::EvaluationMode 强类型枚举——"合法子集"由类型层保证
    // （未知模式 token 不可表达）；空集＝全部模式适用（§4.2.1 空集语义）。

    // ---- 发布门（⑦的前置）：有错误级诊断 → 不发布（policy 空） ----
    if (outcome.hasError) {
        return outcome;   // 诊断全量已收集——不短路；policy=nullopt（§5.1⑦）
    }

    // ---- ⑥⑦ 内容身份生成与发布（全部校验通过路径） ----
    // 组装规范化子模型：对端字典序承载（②——消除顺序歧义）、集合升序去重
    // （集合语义）、阈值取②产物＋③默认。
    CollisionRules collision;
    collision.enabled = input.collision.enabled;
    collision.enabledDomains = normalizedSet(
        input.collision.enabledDomains,
        [](CollisionDomain a, CollisionDomain b) {
            return static_cast<std::uint8_t>(a) < static_cast<std::uint8_t>(b);
        });
    collision.safetyClearance = std::move(safetyClearance);
    collision.excludeAdjacentLinksByDefault = input.collision.excludeAdjacentLinksByDefault;
    collision.mandatoryPairs = mandatory;      // 已规范化（端序＋列表序）
    collision.excludedPairs = excluded;

    JointThresholds jointThresholds;
    jointThresholds.nearLimitRatio = std::move(nearLimitRatio);
    jointThresholds.conditionNumberWarning = std::move(conditionNumberWarning);
    jointThresholds.finiteRotationTravelLimit = *std::move(travelLimit);   // ②产物或③默认
    jointThresholds.travelLimitCheckEnabled = input.jointThresholds.travelLimitCheckEnabled;

    PolicyApplicability applicability;
    applicability.modes = normalizedSet(
        input.applicability.modes, [](core::EvaluationMode a, core::EvaluationMode b) {
            return a < b;
        });
    const auto byIdBytes = [](const core::ObjectId& a, const core::ObjectId& b) {
        return a.bytes < b.bytes;
    };
    applicability.modelObjects = normalizedSet(input.applicability.modelObjects, byIdBytes);
    applicability.taskObjects = normalizedSet(input.applicability.taskObjects, byIdBytes);
    applicability.caseObjects = normalizedSet(input.applicability.caseObjects, byIdBytes);

    // ⑥ 内容身份：PolicyCodec::contentIdentity——语义闭包 canonical 编码 →
    // SHA-256（CR-02 摘要单点；发布路径唯一调用——"只对通过校验并发布的
    // 策略计算"，§4.1）。
    const core::ContentIdentity identity = PolicyCodec::contentIdentity(
        input.schemaVersion, collision, jointThresholds, applicability,
        input.numericContractAnchor);

    // ⑦ 发布：EngineeringPolicySet::make（发布门只产出 Valid 实例——
    // 校验序①~⑦与其门内核对互补：此处入参已满足全部前置，门校验若因
    // 逻辑缺陷触发异常将 fail-fast 传播——不吞错）。
    // 就地构造（emplace）：EngineeringPolicySet 全成员 const——拷贝/移动
    // 赋值被编译期删除（§4.6 不可变性的类型层强制），optional 的赋值算子
    // 相应不可用；构造途径（拷贝/移动构造）不受影响。
    outcome.policy.emplace(EngineeringPolicySet::make(
        input.policyObject, input.schemaVersion, identity, std::move(collision),
        std::move(jointThresholds), std::move(applicability), input.origin,
        PolicyValidationState::Valid, outcome.diagnostics,   // 全量诊断随发布对象附带（此时仅 Info）
        input.compatibilityNotes, input.numericContractAnchor));
    return outcome;
}

}  // namespace

// =====================================================================
// 公共入口定义（契约见 PolicyParsing.hpp）。
// =====================================================================

PolicyParseResult resolvePolicy(const RawPolicyInput& input,
                                const IPolicyValidationContext& context)
{
    // 单一管线实现；sourceVersion 恒 nullopt（对象字节版本归 Provider 填充——
    // PolicyParseResult 注释；PA-1 不越权计算对象字节摘要）。
    // 聚合初始化＋移动构造（optional<EngineeringPolicySet> 因发布对象全成员
    // const 而无赋值算子——构造途径可用，见 runParsePipeline 发布点注释）。
    ParseOutcome outcome = runParsePipeline(input, context);
    PolicyParseResult result{std::move(outcome.policy), std::move(outcome.diagnostics),
                             std::nullopt};
    return result;
}

std::vector<core::DiagnosticRecord>
PolicyValidator::validate(const RawPolicyInput& input,
                          const IPolicyValidationContext& context) const
{
    // 只校验不发布（§9.2）：同一管线的诊断轨——policy 产物弃用（无发布
    // 副作用；类型层防误用由 IPolicyValidator::validate 无 policy 返回保证）。
    return std::move(runParsePipeline(input, context).diagnostics);
}

std::vector<std::string_view> sceneObjectRoleTokens()
{
    return {kRoleTokens, kRoleTokens + sizeof(kRoleTokens) / sizeof(kRoleTokens[0])};
}

}  // namespace sdurws::ird::policy
