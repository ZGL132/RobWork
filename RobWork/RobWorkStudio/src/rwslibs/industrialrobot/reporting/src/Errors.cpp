/**
 * @file   Errors.cpp
 * @brief  reporting 错误契约实现——token 全表＋建议码映射＋ReportError
 *         消息拼装。
 *
 * 设计依据：
 *   - units/reporting.md §3.5（ReportErrorCode 16 值 token 稳定清单原文、
 *     ReportError 构造签名与 "reporting/<域>:" 前缀口径、RPT-* 收编清单）、
 *     §3.1 组成表（Errors.hpp 对应实现随 RPT-T02 落地）
 *   - 需求 NFR-COR-02（确定性：同码同串、无 locale 依赖）、NFR-COR-03
 *     （不静默吞错——异常携带可定位码面与 detail）
 *   - 任务契约 tasks/foundation/RPT-T02.json acceptance 2/3
 *
 * 实现说明（确定性来源，NFR-COR-02）：token 表为 switch 全枚举、无 default
 * 分支——新增枚举值而未登记表项时，switch 全枚举告警（MSVC C4061/C4062）
 * 会暴露遗漏；无论告警级别如何，测试侧另有全表用例逐值钉住（强制防线，
 * ErrorsIdentityTest 全表用例）。建议码映射表以 default＝"无建议"显式收底
 * （映射面 6/16——nullopt 是合法语义值而非遗漏），表项仍逐值登记。所有
 * 返回值为静态存储期字面量，无堆分配、无 locale 依赖——跨平台跨进程
 * 逐字节一致。先例：evidence/src/Errors.cpp（EV-T02 同款实现形态）。
 */

#include <sdurws/ird/reporting/Errors.hpp>

#include <utility>

namespace sdurws::ird::reporting {

namespace {

/**
 * @brief 拼装 ReportError 的 what() 消息（前缀约定强制点）。
 *
 * 约定（§3.5 "detail 前缀 'reporting/<域>:'口径"）：what() ＝
 * "<token>[: <detail>]"——token（"reporting/<域>"）前缀由本函数统一拼装，
 * 构造路径无裸消息入口，保证异常消息可按 "reporting/..." 稳定检索；
 * detail 为空时恰为 token（无尾随冒号空格，避免噪音字符）。
 *
 * @param code   [in] 稳定错误码
 * @param detail [in] 开发诊断细节（就地定位信息；可为空）
 * @return 完整 what() 消息（供 std::runtime_error 基类拷贝持有）
 */
std::string makeWhat(ReportErrorCode code, std::string&& detail)
{
    // 第一步：token 前缀（全表非空——token() 契约；前缀恒为 "reporting/<域>"）。
    std::string what(token(code));
    // 第二步：有细节才追加 ": "（空细节消息恰为 token，无噪音字符）。
    if (!detail.empty()) {
        what += ": ";
        what += detail;
    }
    return what;
}

}  // namespace

std::string_view token(ReportErrorCode code) noexcept
{
    // token 表：与枚举值注释列逐字一致（§3.5 清单原文——acceptance 2
    // "逐值与 §3.5 清单一致"）。稳定 token 持久化于诊断与导出物，一经
    // 交付不得改写；新码只允许表尾追加并同步单元卡增量修订（DTB §5.4）。
    switch (code) {
    case ReportErrorCode::SourceMissing:       return "reporting/source-missing";
    case ReportErrorCode::SourceAmbiguous:     return "reporting/source-ambiguous";
    case ReportErrorCode::ScopeInsufficient:   return "reporting/scope-insufficient";
    case ReportErrorCode::LevelConflict:       return "reporting/level-conflict";
    case ReportErrorCode::EvidenceRefInvalid:  return "reporting/evidence-ref-invalid";
    case ReportErrorCode::ConsistencyMismatch: return "reporting/consistency-mismatch";
    case ReportErrorCode::RenderFailed:        return "reporting/render-failed";
    case ReportErrorCode::DataInvalid:         return "reporting/data-invalid";
    case ReportErrorCode::ArchiveConflict:     return "reporting/archive-conflict";
    case ReportErrorCode::ExportFailed:        return "reporting/export-failed";
    case ReportErrorCode::DiskFull:            return "reporting/disk-full";
    case ReportErrorCode::ReadOnlyStore:       return "reporting/read-only-store";
    case ReportErrorCode::RoundtripMismatch:   return "reporting/roundtrip-mismatch";
    case ReportErrorCode::BundleIncomplete:    return "reporting/bundle-incomplete";
    case ReportErrorCode::TemplateVersion:     return "reporting/template-version";
    case ReportErrorCode::Usage:               return "reporting/usage";
    }
    // 不可达：全枚举已覆盖（switch 无 default——全枚举告警防线）。
    return "reporting/unknown";
}

std::optional<std::string_view> suggestedDiagCode(ReportErrorCode code) noexcept
{
    // 建议码映射表（6/16——见 Errors.hpp 函数注释的映射面声明）。字面量
    // 全部引用 diagcodes 登记清单常量，不书写第二份码文本（引用登记非
    // 二次定义——码值权威归 diagnostics StableCodeRegistry，P-RPT-8）。
    switch (code) {
    case ReportErrorCode::SourceMissing:       return diagcodes::kSourceMissing;
    case ReportErrorCode::ScopeInsufficient:   return diagcodes::kScopeInsufficient;
    case ReportErrorCode::ConsistencyMismatch: return diagcodes::kConsistencyMismatch;
    case ReportErrorCode::ArchiveConflict:     return diagcodes::kArchiveConflict;
    case ReportErrorCode::ExportFailed:        return diagcodes::kExportFailed;
    case ReportErrorCode::RoundtripMismatch:   return diagcodes::kRoundtripMismatch;
    // 其余 10 值无 RPT 建议码（过程性/调用方错误——不进报告诊断主轴，
    // 映射面声明见 Errors.hpp）；kCurrentnessUnevaluable/kSectionNotApplicable
    // 为呈现类诊断、非异常码面，亦不入本映射。
    default:                                   return std::nullopt;
    }
}

ReportError::ReportError(ReportErrorCode code, std::string detail)
    : // what() 消息经 makeWhat 统一拼装（token 前缀强制——无裸消息入口）；
      // std::move(detail)＝消息拼装后细节不再单独持有（仅存于基类消息内）。
      std::runtime_error(makeWhat(code, std::move(detail))),
      m_code(code)
{
}

}  // namespace sdurws::ird::reporting
