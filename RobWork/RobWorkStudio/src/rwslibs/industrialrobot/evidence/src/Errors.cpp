/**
 * @file   Errors.cpp
 * @brief  evidence 错误契约实现——token/建议注册码两张固定全表＋
 *         EvidenceError 消息拼装。
 *
 * 设计依据：
 *   - units/evidence.md §3.1 组成表（Errors.hpp｜EvidenceErrorCode（稳定
 *     token）、EvidenceError）、§13 diagnostics 行（7 个 EVI-* 建议码——
 *     建议值，码值权威归 StableCodeRegistry，P-PR-6 同模式）、各抛错点
 *     原文（§4.1.5④a/④b、§4.2.3①、§6.2/§6.3/§6.6、§7.1、§8.2、§9.4）
 *   - 需求 NFR-COR-02（确定性：同码同串）/NFR-COR-03（不静默）
 *   - 任务契约 tasks/foundation/EV-T02.json（≙WP-05-T02）acceptance 2
 *
 * 实现说明（确定性来源，NFR-COR-02）：两张表均为 switch 全枚举、无 default
 * 分支——新增枚举值而未登记表项时，switch 全枚举告警（MSVC C4061/C4062，
 * /W4 及以上可见）会暴露遗漏；无论告警级别如何，测试侧另有全表用例逐值
 * 钉住（强制防线，ErrorsTest EV-ERR-1）。所有返回值为静态存储期字面量，
 * 无堆分配、无 locale 依赖——跨平台跨进程逐字节一致。
 */

#include <sdurws/ird/evidence/Errors.hpp>

#include <utility>

namespace sdurws::ird::evidence {

namespace {

/**
 * @brief 拼装 EvidenceError 的 what() 消息（前缀约定强制点）。
 *
 * 约定：what() ＝ "<token>[: <detail>]"——token 前缀由本函数统一拼装，
 * 构造路径无裸消息入口，保证异常消息可按 "evidence/..." 稳定检索；
 * detail 为空时恰为 token（无尾随冒号空格，避免噪音字符）。
 *
 * @param code   [in] 稳定错误码
 * @param detail [in] 开发诊断细节（就地定位信息；可为空）
 * @return 完整 what() 消息（供 std::runtime_error 基类拷贝持有）
 */
std::string makeWhat(EvidenceErrorCode code, std::string&& detail)
{
    // 第一步：token 前缀（全表非空——token() 契约）。
    std::string what(token(code));
    // 第二步：有细节才追加 ": "（空细节消息恰为 token，无噪音）。
    if (!detail.empty()) {
        what += ": ";
        what += detail;
    }
    return what;
}

}  // namespace

std::string_view token(EvidenceErrorCode code) noexcept
{
    // token 表：与枚举注释列逐字一致（稳定 token——持久化于诊断/报告，
    // 一经交付不得改写）。全 9 值非空（"evidence/" 前缀统一）。
    // 命名出处：SnapshotIntegrity 取 §4.1.5④b 原文 "snapshot-integrity"、
    // EvaluatorDuplicate 取 §9.4 原文 "duplicate-evaluator"；其余七值按
    // §13 建议码 EVI-* 的 kebab 形式派生（登记单元卡 v0.3/F-056）。
    switch (code) {
    case EvidenceErrorCode::SnapshotIncomplete:         return "evidence/snapshot-incomplete";
    case EvidenceErrorCode::SnapshotIntegrity:          return "evidence/snapshot-integrity";
    case EvidenceErrorCode::DeclarationInvalid:         return "evidence/declaration-invalid";
    case EvidenceErrorCode::EvidenceMissing:            return "evidence/evidence-missing";
    case EvidenceErrorCode::ProofInvalid:               return "evidence/proof-invalid";
    case EvidenceErrorCode::CaseCoverageMissing:        return "evidence/case-coverage-missing";
    case EvidenceErrorCode::EnvelopeIllegalCombination: return "evidence/envelope-illegal-combination";
    case EvidenceErrorCode::CacheIncompatible:          return "evidence/cache-incompatible";
    case EvidenceErrorCode::EvaluatorDuplicate:         return "evidence/duplicate-evaluator";
    }
    // 不可达：switch 已覆盖全枚举（无 default——遗漏新值时编译器告警）。
    // 防御性返回空串（调用方以 empty 判异常值，测试保证不触达）。
    return {};
}

std::string_view registryCode(EvidenceErrorCode code) noexcept
{
    // 建议注册码表：§13 diagnostics 行建议码清单（码值权威＝diagnostics
    // StableCodeRegistry；evidence 只产出建议码面，不注册码值——PA-1）。
    // 前 7 值与 §13 原文逐字对应；后 2 值为设计抛错点明文（§4.1.5④b/
    // §4.2.3①）但 §13 未列建议码的补登建议（EVI-SNAPSHOT-INTEGRITY/
    // EVI-DECLARATION-INVALID——**建议值**，登记 F-056 转 owners 裁决）。
    switch (code) {
    case EvidenceErrorCode::SnapshotIncomplete:         return "EVI-SNAPSHOT-INCOMPLETE";
    case EvidenceErrorCode::SnapshotIntegrity:          return "EVI-SNAPSHOT-INTEGRITY";     // 补登建议（F-056）
    case EvidenceErrorCode::DeclarationInvalid:         return "EVI-DECLARATION-INVALID";    // 补登建议（F-056）
    case EvidenceErrorCode::EvidenceMissing:            return "EVI-EVIDENCE-MISSING";
    case EvidenceErrorCode::ProofInvalid:               return "EVI-PROOF-INVALID";
    case EvidenceErrorCode::CaseCoverageMissing:        return "EVI-CASE-COVERAGE-MISSING";
    case EvidenceErrorCode::EnvelopeIllegalCombination: return "EVI-ENVELOPE-ILLEGAL-COMBINATION";
    case EvidenceErrorCode::CacheIncompatible:          return "EVI-CACHE-INCOMPATIBLE";
    case EvidenceErrorCode::EvaluatorDuplicate:         return "EVI-EVALUATOR-DUPLICATE";
    }
    return {};
}

EvidenceError::EvidenceError(EvidenceErrorCode code, std::string detail)
    : std::runtime_error(makeWhat(code, std::move(detail)))
    , m_code(code)
{
}

}  // namespace sdurws::ird::evidence
