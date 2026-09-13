/**
 * @file   Compatibility.cpp
 * @brief  策略兼容判定实现——reasonToken 稳定表、七项检查的固定序全量
 *         收集与 POLICY-VERSION-INCOMPATIBLE 诊断构造（§8.1/§8.2/§9.5）。
 *
 * 设计依据：
 *   - units/policy.md §8.1（PolicyConsumerRequirements/PolicyCompatibility
 *     冻结语义）、§8.2（变化分类各行→原因词表；诊断行："diagnostics 含
 *     POLICY-VERSION-INCOMPATIBLE（比较型）＋建议动作（升级策略对象或调整
 *     消费方要求）"；"不抛异常（判定是查询非违约）"）、§9.5（契约表：
 *     纯函数/不抛/无状态）、§12 POL-T09 行（产物＝Compatibility.hpp/.cpp；
 *     验证＝POL-COMPAT-1/2）
 *   - 需求 CON-04/CON-05/NFR-DEP-05、ERR-01/UX-03（诊断字段——比较型仅
 *     数值判定）、CON-02（不兼容只拒绝新复用——本 TU 无任何历史写通道）
 *   - 任务契约 tasks/foundation/POL-T09.json acceptance 1/2（POL-COMPAT-1/2
 *     全量 reason 用例；O-20/P-POL-5 保守口径——后端版本串暂取构建版本，
 *     本 TU 不私定最终取值）
 *
 * 翻译单元说明（集成模式专属——CMake gating 同 JointLimits.cpp 先例）：
 *   本 TU 为实现"当前注册后端"的复现要素核对（backend-mismatch 臂），include
 *   CollisionEvaluator.hpp（其中 detail::makeBuiltinBackendDescriptor 是后端
 *   冻结值的字面量单点——policy.md §15.4 v0.7 ⑤⑥）并包含框架配置头取构建
 *   版本宏（RW_VERSION——P-POL-5"暂取构建版本"口径的既有注入点，与
 *   RobWorkCollisionEvaluator.cpp 同源同值：同一宏、同一字面量单点函数，
 *   两处推导必得同值——§9.1"复现要素同源"的机械保证）。框架头在独立冒烟
 *   模式不可解析——本 TU 与其测试 TU 仅集成模式编译；判定语义本体（头内
 *   类型面）零框架依赖，冒烟模式经公共头 include 面保持可消费。
 *
 * R-4 门禁自律（policy.md §15.4 v0.7 ⑤ 同款）：本 TU 持有诊断文案字面量，
 * 自身文本不含任何以双引号对携带的名称拼接/剥离形态字面量；版本宏为纯
 * 版本串（无名称子串），后端冻结串经字面量单点函数取得、不经本 TU 重述
 * ——ird_gates IRD-GATE-R4 零新增命中的结构性前提。
 *
 * 线程安全：全部函数纯/无状态（无共享可变状态），并发只读安全。
 * 确定性：检查序固定、原因词表编译期冻结、数值文本 %.17g（"C" locale）——
 * 同输入同结果逐字节一致（NFR-COR-02）。
 */

#include <sdurws/ird/policy/Compatibility.hpp>

#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/Diagnostics.hpp>

#include <RobWorkConfig.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace sdurws::ird::policy {

// =====================================================================
// 建议诊断码（POL-T10 迁移：字面量唯一在 Diagnostics.cpp 码表——本 TU 常
// 量为编译期码表引用别名，NFR-MNT-03 单一权威；原单点字面量按 §15.4 v0.10
// ⑥的迁移通道注记收敛）。
// =====================================================================

/// 建议码：策略版本不兼容（§8.2 诊断行原文码名；比较型——见各臂构造）。
/// 码值权威归 diagnostics StableCodeRegistry（PA-1）——本 TU 只产出建议码，
/// 不承担注册职责。
constexpr std::string_view kCodePolicyVersionIncompatible =
    policyDiagCode(PolicyDiagCode::VersionIncompatible);

/// 比较型数值侧的来源方法标记（ValueProvenance.methodTag 语法 [a-z0-9./_-]）：
/// 标记实际值取自已发布策略对象的只读派生（兼容判定的核对面）——审计追溯
/// 用，不构成可信等级（DYN-06）。
constexpr std::string_view kCompatProvenanceTag = "policy-compat";

/// 判定上下文文案（诊断 context 字段——全部臂共用：同一判定语境，
/// ERR-01"上下文"要素；定位细节在 localName/cause 分臂承载）。
constexpr std::string_view kCompatContext = "策略兼容判定（checkPolicyCompatibility，policy.md §8.1/§8.2）";

namespace {

// =====================================================================
// 当前注册后端复现要素（backend-mismatch 臂的核对对象——实现口径 C-4）。
// =====================================================================

/**
 * @brief 当前进程唯一注册后端的复现要素（运行构建的内置后端——§6.5"内置
 *        ProximityStrategyRW 为默认且唯一注册后端"）。
 *
 * 取值路径：detail::makeBuiltinBackendDescriptor（后端冻结串字面量单点，
 * CollisionEvaluator.cpp）＋ RW_VERSION 注入（构建版本宏——P-POL-5"暂取
 * 构建版本"口径，与 RobWorkCollisionEvaluator 装配线同宏同单点）。本函数
 * **不私定**任何版本/标识取值：WP-24-T01 冻结 NFR-DEP-05 基线后锁定回填
 * 时，改的是字面量单点与注入口径，本判定逻辑零变化（acceptance 2）。
 *
 * 确定性：构建期冻结——同一次构建内恒等（NFR-COR-02）；跨构建可比（版本
 * 串不同即 BackendMismatch——§8.2"碰撞后端版本变化→拒绝"的判定面）。
 */
CollisionBackendDescriptor currentRegisteredBackend()
{
    return detail::makeBuiltinBackendDescriptor(RW_VERSION);
}

// =====================================================================
// 诊断构造辅助（ERR-01 字段面：码/subject/定位/上下文/原因/建议动作；
// 比较型三要素仅数值臂携带——头文件实现口径 C-2）。
// =====================================================================

/**
 * @brief 数值的确定性文本格式（%.17g——round-trip 精确；仅进诊断文案，
 *        非持久化契约面；PolicyParsing.cpp formatNumber 同款——"C" locale
 *        数字格式无本地化分岔）。
 * @param v [in] 数值（本判定的使用场景为 schema 代——小整数，无特殊值）
 * @return 十进制文本
 */
std::string formatNumber(double v)
{
    char buf[40] = {};
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string{buf};
}

/**
 * @brief 比较型单侧值（schema 代——无量纲 "1"，PolicyParsing 同款口径）。
 *
 * 来源标注：实际/期望代均取自本判定核对面的只读派生（策略对象与消费方
 * 要求）——DerivedReadOnly＋方法标记 kCompatProvenanceTag（值来源事实
 * 记录，DYN-06）。
 *
 * @param schemaVersion [in] schema 代（无量纲小整数——不是物理量，单位
 *                      "1" 仅作三要素的已注册单位占位）
 * @return 三要素单侧值（Provided 态——比较型数值判定不适用态不在此臂出现）
 */
core::ComparativeValue schemaVersionValue(std::uint32_t schemaVersion)
{
    core::ComparativeValue side;
    side.quantity = core::SourcedValue<double>::provided(
        static_cast<double>(schemaVersion),
        core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {}, {},
                                    std::string{kCompatProvenanceTag}));
    // 无量纲单位 "1"（core 单位注册表冻结 token——版本/代类比较的 SI 占位，
    // PolicyParsing.cpp schema 校验臂同款）。
    side.unit = *core::UnitToken::find("1");
    return side;
}

/**
 * @brief 组装一条不兼容诊断（每条原因恰好一条——PolicyCompatibility 不变式）。
 *
 * 比较型口径（实现口径 C-2）：comparison 仅在数值臂（schema 代）给出；
 * 字符串臂（后端复现要素/数值契约锚/模式）的实际与期望值以 cause 原文
 * 逐字承载——ERR-01"原因"要素，不伪造数值、不滥用 SourcedValue 四态。
 *
 * @param localName [in] 定位字段（点路径——如 "schemaVersion"；空串不回填）
 * @param cause     [in] 原因（非空——C-3 工厂校验；字符串臂在此携带
 *                  实际/期望原文）
 * @param action    [in] 建议动作（非空——C-3；§8.2"升级策略对象或调整
 *                  消费方要求"的分臂具体化）
 * @param comparison [in] 比较型三要素（仅 schema 两臂携带；缺省空＝非数值
 *                  判定——checkDiagnosticRecord 对非比较型记录不要求该字段）
 * @return 可持久化诊断记录（码面恒为 POLICY-VERSION-INCOMPATIBLE）
 */
core::DiagnosticRecord makeIncompatibilityDiag(const std::optional<core::ObjectId>& subject,
                                               std::string localName, std::string cause,
                                               std::string action,
                                               std::optional<core::ComparativeFields> comparison
                                               = std::nullopt)
{
    return core::DiagnosticRecord::make(
        std::string{kCodePolicyVersionIncompatible},
        subject,        // 主体＝被核对的策略对象（ERR-01 稳定诊断项绑定）
        localName.empty() ? std::optional<std::string>(std::nullopt)
                          : std::optional<std::string>(std::move(localName)),
        std::nullopt,   // runtimeName：判定为纯函数无名称上下文注入点（§9.5 无状态）
        std::string{kCompatContext}, std::move(cause), std::move(action),
        std::move(comparison));
}

}  // namespace

// =====================================================================
// reasonToken——不兼容原因词表的稳定 token 表（编译期 switch 全枚举）。
// =====================================================================

std::string_view reasonToken(PolicyIncompatibilityReason reason) noexcept
{
    // token＝§8.1 reasons 注释行 kebab 词表原文（持久化于失效原因清单——
    // 不改名）；无 default：新增枚举值未登记表项时编译器告警暴露遗漏。
    switch (reason) {
    case PolicyIncompatibilityReason::SchemaFuture:
        return "schema-future";
    case PolicyIncompatibilityReason::SchemaObsolete:
        return "schema-obsolete";
    case PolicyIncompatibilityReason::CollisionRequiredDisabled:
        return "collision-required-disabled";
    case PolicyIncompatibilityReason::BackendMismatch:
        return "backend-mismatch";
    case PolicyIncompatibilityReason::NumericContractMismatch:
        return "numeric-contract-mismatch";
    case PolicyIncompatibilityReason::ModeNotApplicable:
        return "mode-not-applicable";
    case PolicyIncompatibilityReason::PolicyInvalid:
        return "policy-invalid";
    }
    return {};   // 不可达（全枚举覆盖）——保返回避免部分编译器告警
}

// =====================================================================
// checkPolicyCompatibility——七项检查固定序、全量收集、不抛（§9.5）。
// =====================================================================

PolicyCompatibility checkPolicyCompatibility(const EngineeringPolicySet& policy,
                                             const PolicyConsumerRequirements& requirements)
{
    // 结果载体：reasons 与 diagnostics 严格按下标一一对应（每臂先登记原因
    // 再登记其诊断——两向量增长路径同源，不变式由构造序机械保证）。
    PolicyCompatibility result;
    result.verdict = PolicyCompatibility::Compatible;

    // 诊断 subject 的公共取值：策略对象身份（ERR-01"稳定诊断项以
    // subjectObjectId 绑定对象"——判定的主体即被核对的策略对象）。
    const std::optional<core::ObjectId> subject = policy.policyObject;

    // ---- 臂 ①：policy-invalid（防御臂——实现口径 C-3）----
    // 触发条件：策略对象校验状态非 Valid（§9.5 前置"policy 为已发布对象"
    // 的查询面核对）。发布门（EngineeringPolicySet::make）机械执行"只产出
    // Valid 实例"——经由公共 API 该臂类型层不可达；保留以兑现 §8.1 reason
    // 词表并为未来非门构造路径提供稳定码面。不短路：其余核对均为结构
    // 安全的只读谓词，继续收集保持"全量"纪律。
    if (policy.validationState != PolicyValidationState::Valid) {
        result.reasons.push_back(PolicyIncompatibilityReason::PolicyInvalid);
        core::DiagnosticRecord diag = makeIncompatibilityDiag(
            subject, "validationState",
            "策略对象校验状态非 Valid（发布对象必须经解析管线发布门产出——"
            "§4.5/§9.5 前置；防御臂，公共 API 构造路径下不可达）",
            "以解析管线（resolvePolicy）重新解析并发布该策略后再消费");
        result.diagnostics.push_back(std::move(diag));
    }

    // ---- 臂 ②：schema-future（§8.2"schema 主版本变化"行左）----
    // 触发条件：策略代高于消费方支持上限。业务场景＝旧代码读新策略：未来
    // 代的字段布局未可知（PM-06 只读拒绝、不前向猜测——与解析期版本门同
    // 源语义，POL-PARSE-1）。比较型三要素：实际策略代/期望上限/无量纲 "1"。
    if (policy.schemaVersion > requirements.maxSchemaVersion) {
        result.reasons.push_back(PolicyIncompatibilityReason::SchemaFuture);
        core::ComparativeFields cmp;
        cmp.actual = schemaVersionValue(policy.schemaVersion);
        cmp.expected = schemaVersionValue(requirements.maxSchemaVersion);
        core::DiagnosticRecord diag = makeIncompatibilityDiag(
            subject, "schemaVersion",
            "策略 schema 代 " + formatNumber(static_cast<double>(policy.schemaVersion))
                + " 高于消费方支持上限 "
                + formatNumber(static_cast<double>(requirements.maxSchemaVersion))
                + "（未来版本不前向猜测解析——PM-06）",
            "升级消费方到支持该策略代的版本，或调整消费方要求（§8.2 建议动作）",
            std::move(cmp));
        result.diagnostics.push_back(std::move(diag));
    }

    // ---- 臂 ③：schema-obsolete（§8.2"schema 主版本变化"行右）----
    // 触发条件：策略代低于消费方支持下限。业务场景＝消费方已升级、策略是
    // 旧代存量（未知旧代的字段布局同样不可猜测——解析期同码面 POL-PARSE-1
    // 对称臂）。比较型三要素：实际策略代/期望下限/无量纲 "1"。
    if (policy.schemaVersion < requirements.minSchemaVersion) {
        result.reasons.push_back(PolicyIncompatibilityReason::SchemaObsolete);
        core::ComparativeFields cmp;
        cmp.actual = schemaVersionValue(policy.schemaVersion);
        cmp.expected = schemaVersionValue(requirements.minSchemaVersion);
        core::DiagnosticRecord diag = makeIncompatibilityDiag(
            subject, "schemaVersion",
            "策略 schema 代 " + formatNumber(static_cast<double>(policy.schemaVersion))
                + " 低于消费方支持下限 "
                + formatNumber(static_cast<double>(requirements.minSchemaVersion))
                + "（旧代不再受消费方支持——字段布局不猜测）",
            "升级策略对象到消费方支持的代（经 project 升级器——§4.6 版本策略），"
            "或下调消费方要求（§8.2 建议动作）",
            std::move(cmp));
        result.diagnostics.push_back(std::move(diag));
    }

    // ---- 臂 ④：collision-required-disabled（§8.1 注释行原文）----
    // 触发条件：消费方需要碰撞能力而策略禁用了碰撞（enabled=false）。业务
    // 场景＝轨迹复检（TRJ-04）等以碰撞复检为履约前提的消费方拿到一份"仅
    // 阈值"策略——其碰撞义务无法履行，必须拒绝而非静默降级（CON-04 保守
    // 正确方向）。无数值比较——布尔开关以 cause 承载（实现口径 C-2）。
    if (requirements.requiresCollision && !policy.collision.enabled) {
        result.reasons.push_back(PolicyIncompatibilityReason::CollisionRequiredDisabled);
        core::DiagnosticRecord diag = makeIncompatibilityDiag(
            subject, "collision.enabled",
            "消费方需要碰撞能力，但策略已禁用碰撞（collision.enabled=false——"
            "§8.1：策略 enabled=false → 不兼容）",
            "启用该策略的碰撞域并重新发布（新内容身份），或改用含碰撞能力的"
            "策略对象（§8.2 建议动作）");
        result.diagnostics.push_back(std::move(diag));
    }

    // ---- 臂 ⑤：backend-mismatch（§8.2"碰撞后端版本变化"行）----
    // 触发条件：消费方指定了后端复现要素（requiresBackend 给出），且与当前
    // 进程唯一注册后端（运行构建的内置后端——实现口径 C-4）逐字段不一致。
    // 业务场景＝按历史复现块/worker 声明要求后端的消费方，在版本串不同的
    // 构建里复用（NFR-DEP-05：升版即全体缓存失效——保守正确方向）。三串
    // 比较为精确等值（附录 D 第 12 项——无容差）；差异细节以 cause 原文
    // 承载（字符串比较非数值判定——实现口径 C-2）。
    if (requirements.requiresBackend.has_value()) {
        const CollisionBackendDescriptor current = currentRegisteredBackend();
        if (!(*requirements.requiresBackend == current)) {
            result.reasons.push_back(PolicyIncompatibilityReason::BackendMismatch);
            core::DiagnosticRecord diag = makeIncompatibilityDiag(
                subject, "collisionBackend",
                "消费方要求的碰撞后端（id=" + requirements.requiresBackend->backendId
                    + "，version=" + requirements.requiresBackend->backendVersion
                    + "，tolerance=" + requirements.requiresBackend->toleranceModel
                    + "）与当前注册后端（id=" + current.backendId
                    + "，version=" + current.backendVersion
                    + "，tolerance=" + current.toleranceModel + "）不一致"
                    "（复现要素失配——CON-04/NFR-DEP-05；版本串为构建期口径，"
                    "基线冻结后锁定回填）",
                "在注册了所需后端的构建中执行，或调整消费方要求以接受当前"
                "后端（§8.2 建议动作；历史结果保留——仅拒绝新复用，CON-02）");
            result.diagnostics.push_back(std::move(diag));
        }
    }

    // ---- 臂 ⑥：numeric-contract-mismatch（§8.2"数值契约锚变化"行）----
    // 触发条件：消费方指定数值契约锚（requiresNumericContract 给出），且与
    // 策略 numericContractAnchor 精确等值比较失败。业务场景＝附录 D 修订
    // （锚版本前进）后，旧锚策略对新锚消费方——锚下数值语义不同，拒绝复用。
    // 锚串以 cause 原文承载（字符串比较——实现口径 C-2）。
    if (requirements.requiresNumericContract.has_value()
        && *requirements.requiresNumericContract != policy.numericContractAnchor) {
        result.reasons.push_back(PolicyIncompatibilityReason::NumericContractMismatch);
        core::DiagnosticRecord diag = makeIncompatibilityDiag(
            subject, "numericContractAnchor",
            "消费方要求数值契约锚 " + *requirements.requiresNumericContract
                + "，策略锚为 " + policy.numericContractAnchor
                + "（附录 D 修订走需求变更——§8.2；锚不一致即数值语义不同）",
            "以新锚重新解析并发布策略对象，或调整消费方要求（§8.2 建议动作）");
        result.diagnostics.push_back(std::move(diag));
    }

    // ---- 臂 ⑦：mode-not-applicable（§8.1 评估模式行"适用性核对用"）----
    // 触发条件：消费方声明目标模式，且策略适用范围 modes 非空而不含该模式。
    // 业务场景＝Verified 消费拿到仅适用于 Preview/Quick 的策略（§4.2.1：modes
    // 空＝全部模式适用——空集不触发本原因，直接视为适用）。模式仅做消费前
    // 适用性预检，不是评估输入（R-POL-5——评估 API 无模式参数）。
    if (requirements.mode.has_value() && !policy.applicability.modes.empty()) {
        const std::vector<core::EvaluationMode>& modes = policy.applicability.modes;
        const bool applicable =
            std::find(modes.begin(), modes.end(), *requirements.mode) != modes.end();
        if (!applicable) {
            // 适用模式清单文本（cause 素材——core toToken 稳定小写词表，
            // 逐模式枚举；顺序＝策略承载序——仅入文案不入任何身份/键面）。
            std::string modesText;
            for (std::size_t i = 0; i < modes.size(); ++i) {
                if (i != 0) {
                    modesText += "/";
                }
                modesText += core::toToken(modes[i]);
            }
            result.reasons.push_back(PolicyIncompatibilityReason::ModeNotApplicable);
            core::DiagnosticRecord diag = makeIncompatibilityDiag(
                subject, "applicability.modes",
                "消费方目标模式 " + std::string(core::toToken(*requirements.mode))
                    + " 不在策略适用范围（" + modesText + "——§4.2.1 适用范围）",
                "调整策略适用范围并重新发布，或改用适用于该模式的策略对象"
                "（§8.2 建议动作）");
            result.diagnostics.push_back(std::move(diag));
        }
    }

    // ---- 裁决（§8.1 冻结语义）：reasons 非空即 Incompatible；诊断与原因
    // ---- 严格同构（每因一诊，下标对齐——类型不变式，测试钉住）。
    if (!result.reasons.empty()) {
        result.verdict = PolicyCompatibility::Incompatible;
    }
    return result;
}

}  // namespace sdurws::ird::policy
