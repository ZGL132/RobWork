/**
 * @file   IoDiagnostics.cpp
 * @brief  io 诊断面实现——IoErrorCode→稳定 token 全映射（§9.12）、IO-\*
 *         稳定诊断码表 CodeDescriptor 产出与注册（P-IO-6）、比较型三要
 *         素 IoError 构造助手。
 *
 * 设计依据：
 *   - units/io.md §9.12（码汇总建议值——token 逐字来源）、§3.1（公共头
 *     表 IoDiagnostics.hpp 行）、§10.3（io→diagnostics 产出 CodeDescriptor）
 *   - units/diagnostics.md §4.5（注册协议字段约束：句法/前缀-所有权/
 *     文案键命名 diag.<code-lower>.title|.detail/paramSchema 必填/Dev 码
 *     强制不可见）、§8.6（io 路径与预算错误→诊断的映射义务）、DiagCodes
 *     的分类→retryable 机械映射（categoryDefaultRetry——实现侧同规则
 *     复刻，权威仍在 diagnostics 单元）
 *   - 任务契约 tasks/foundation/IO-T02.json acceptance 3/5
 *
 * 同步纪律：本文件三张表（token 表/描述符表）与 IoError.hpp 枚举注释同
 * 受 units/io.md §9.12 一张卡面表约束；任一侧增改码必须三处同步并走单
 * 元卡增量修订（§15.5）。测试（IoDiag/CodeTable）对全集做机械比对——
 * 失同步在构建期后的测试期即刻暴露。
 */

#include <sdurws/ird/io/IoDiagnostics.hpp>

#include <cctype>
#include <utility>

namespace sdurws::ird::io {

// =====================================================================
// errorCodeToken——枚举成员 ↔ §9.12 token 的唯一映射点
// =====================================================================

std::string_view errorCodeToken(IoErrorCode code)
{
    // switch 全枚举（编译器 -W4 会对缺项告警——新增枚举成员漏映射在此
    // 暴露）；顺序＝§9.12 表行序（IoError.hpp 枚举序同源）。
    switch (code) {
    case IoErrorCode::Ok:                       return "Ok";
    case IoErrorCode::Cancelled:                return "IO-CANCELLED";
    // ---- 格式-CSV 族 ----
    case IoErrorCode::FormatCsvDialect:         return "IO-FORMAT-CSV-DIALECT";
    case IoErrorCode::FormatCsvEncoding:        return "IO-FORMAT-CSV-ENCODING";
    case IoErrorCode::FormatCsvQuote:           return "IO-FORMAT-CSV-QUOTE";
    case IoErrorCode::FormatCsvArity:           return "IO-FORMAT-CSV-ARITY";
    case IoErrorCode::FormatCsvDupCol:          return "IO-FORMAT-CSV-DUPCOL";
    case IoErrorCode::FormatCsvChar:            return "IO-FORMAT-CSV-CHAR";
    // ---- 格式-JSON 族 ----
    case IoErrorCode::FormatJsonEncoding:       return "IO-FORMAT-JSON-ENCODING";
    case IoErrorCode::FormatJsonDupKey:         return "IO-FORMAT-JSON-DUPKEY";
    case IoErrorCode::FormatJsonNumber:         return "IO-FORMAT-JSON-NUMBER";
    case IoErrorCode::FormatJsonVersionMissing: return "IO-FORMAT-JSON-VERSION-MISSING";
    case IoErrorCode::FormatJsonVersionType:    return "IO-FORMAT-JSON-VERSION-TYPE";
    case IoErrorCode::FormatJsonVersionFuture:  return "IO-FORMAT-JSON-VERSION-FUTURE";
    case IoErrorCode::FormatJsonVersionLegacy:  return "IO-FORMAT-JSON-VERSION-LEGACY";
    case IoErrorCode::FormatJsonUnknown:        return "IO-FORMAT-JSON-UNKNOWN";
    case IoErrorCode::FormatJsonRequired:       return "IO-FORMAT-JSON-REQUIRED";
    case IoErrorCode::FormatJsonType:           return "IO-FORMAT-JSON-TYPE";
    case IoErrorCode::FormatJsonRange:          return "IO-FORMAT-JSON-RANGE";
    case IoErrorCode::FormatJsonSyntax:         return "IO-FORMAT-JSON-SYNTAX";
    // ---- 格式-包族 ----
    case IoErrorCode::FormatPackZip:            return "IO-FORMAT-PACK-ZIP";
    case IoErrorCode::FormatPackEncrypted:      return "IO-FORMAT-PACK-ENCRYPTED";
    case IoErrorCode::FormatPackEntry:          return "IO-FORMAT-PACK-ENTRY";
    case IoErrorCode::FormatPackManifest:       return "IO-FORMAT-PACK-MANIFEST";
    // ---- 格式-XML/网格族 ----
    case IoErrorCode::FormatXmlCycle:           return "IO-FORMAT-XML-CYCLE";
    case IoErrorCode::FormatMeshUnknown:        return "IO-FORMAT-MESH-UNKNOWN";
    // ---- 安全-路径族（§9.12 连字符展开；正文简写 IO-SEC-SYMLINK 同指
    //      SecPathSymlink——IoError.hpp 枚举注释同源）----
    case IoErrorCode::SecPathEscape:            return "IO-SEC-PATH-ESCAPE";
    case IoErrorCode::SecPathSymlink:           return "IO-SEC-PATH-SYMLINK";
    case IoErrorCode::SecPathReserved:          return "IO-SEC-PATH-RESERVED";
    case IoErrorCode::SecPathTooLong:           return "IO-SEC-PATH-TOO-LONG";
    // ---- 安全-预算族 ----
    case IoErrorCode::SecBudgetFile:            return "IO-SEC-BUDGET-FILE";
    case IoErrorCode::SecBudgetTotal:           return "IO-SEC-BUDGET-TOTAL";
    case IoErrorCode::SecBudgetCount:           return "IO-SEC-BUDGET-COUNT";
    case IoErrorCode::SecBudgetDepth:           return "IO-SEC-BUDGET-DEPTH";
    case IoErrorCode::SecBudgetExpand:          return "IO-SEC-BUDGET-EXPAND";
    case IoErrorCode::SecBudgetRows:            return "IO-SEC-BUDGET-ROWS";
    case IoErrorCode::SecBudgetField:           return "IO-SEC-BUDGET-FIELD";
    case IoErrorCode::SecBudgetJson:            return "IO-SEC-BUDGET-JSON";
    case IoErrorCode::SecBudgetJsonDepth:       return "IO-SEC-BUDGET-JSON-DEPTH";
    case IoErrorCode::SecBudgetJsonString:      return "IO-SEC-BUDGET-JSON-STRING";
    case IoErrorCode::SecBudgetMesh:            return "IO-SEC-BUDGET-MESH";
    case IoErrorCode::SecBudgetInclude:         return "IO-SEC-BUDGET-INCLUDE";
    case IoErrorCode::SecBudgetRefDepth:        return "IO-SEC-BUDGET-REFDEPTH";
    case IoErrorCode::SecBudgetTemp:            return "IO-SEC-BUDGET-TEMP";
    case IoErrorCode::SecBombRatio:             return "IO-SEC-BOMB-RATIO";
    // ---- 资源族 ----
    case IoErrorCode::ResNotFound:              return "IO-RES-NOT-FOUND";
    case IoErrorCode::ResAccessDenied:          return "IO-RES-ACCESS-DENIED";
    case IoErrorCode::ResReadonly:              return "IO-RES-READONLY";
    case IoErrorCode::ResLockConflict:          return "IO-RES-LOCK-CONFLICT";
    case IoErrorCode::ResMissing:               return "IO-RES-MISSING";
    case IoErrorCode::ResChanged:               return "IO-RES-CHANGED";
    // ---- 包过程族 ----
    case IoErrorCode::PackDuplicateEntry:       return "IO-PACK-DUPLICATE-ENTRY";
    case IoErrorCode::PackHashMismatch:         return "IO-PACK-HASH-MISMATCH";
    case IoErrorCode::PackRefIncomplete:        return "IO-PACK-REF-INCOMPLETE";
    case IoErrorCode::PackTargetExists:         return "IO-PACK-TARGET-EXISTS";
    case IoErrorCode::PackDiskFull:             return "IO-PACK-DISK-FULL";
    case IoErrorCode::PackCleanupFailed:        return "IO-PACK-CLEANUP-FAILED";
    // ---- 内部族 ----
    case IoErrorCode::FormatInternal:           return "IO-FORMAT-INTERNAL";
    }
    return "IO-FORMAT-INTERNAL";    // 全枚举不可达——防御性返回（开发级码）
}

// =====================================================================
// 描述符产出（登记值全部可追溯到 §9.12＋diagnostics §4.5/§8.6）
// =====================================================================

namespace {

/// 码文本转小写（文案键命名 diag.<code-lower>.* ——diagnostics §4.5）。
std::string codeToLower(std::string_view code)
{
    std::string out;
    out.reserve(code.size());
    for (const char c : code) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// 分类→可重试性（diagnostics DiagCodes.cpp categoryDefaultRetry 同规则
/// 复刻——机械映射的权威在 diagnostics 单元；io 侧仅按分类取值，不私定
/// 新规则）。
diagnostics::RetryKind retryFor(diagnostics::DiagnosticCategory category)
{
    using C = diagnostics::DiagnosticCategory;
    switch (category) {
    case C::SecurityOrRedaction:
    case C::Internal:
        return diagnostics::RetryKind::Never;       // inspect-resource / report-bug
    default:
        // IO 用到的其余分类（FormatOrVersion/InputInvalid/ResourceMissing/
        // PermissionOrLock/ExecutionFailed）在机械映射下均为 UserRetry
        // （fix-input/relink/retry-readonly/retry-task 动作族）。
        return diagnostics::RetryKind::UserRetry;
    }
}

/**
 * @brief 单条描述符装配（默认值＝§4.5 注册协议的常规登记：可确认否、
 *        注册版本 1、未废弃；异常字段由调用点显式给出）。
 */
diagnostics::CodeDescriptor makeDescriptor(
    std::string_view code,
    diagnostics::DiagnosticCategory category,
    diagnostics::DiagnosticSeverity severity,
    std::string paramSchema,
    bool requiresComparison = false,
    bool userVisible = true,
    bool reportable = true,
    bool historical = true)
{
    diagnostics::CodeDescriptor d;
    d.code = std::string(code);
    d.ownerUnit = "io";                             // 前缀-所有权表 IO→io（§4.5）
    d.category = category;
    d.severity = severity;
    const std::string lower = codeToLower(code);
    d.titleKey = "diag." + lower + ".title";        // 文案键命名约定（键归注册表、值归 ui——UX-02）
    d.detailKey = "diag." + lower + ".detail";
    d.paramSchema = std::move(paramSchema);         // 必填字段——"[]"＝无参数
    d.confirmable = false;                          // 阶段 A io 码无可确认类（SA-15 域码阶段 B 起）
    d.requiresComparison = requiresComparison;      // 预算族 true（§8.6 比较型三要素）
    d.retryable = retryFor(category);
    d.userVisible = userVisible;
    d.reportable = reportable;
    d.historical = historical;
    d.registryVersion = 1;                          // 首次登记（进入诊断实例 contractVersions）
    d.deprecated = false;
    return d;
}

} // namespace

std::vector<diagnostics::CodeDescriptor> ioCodeDescriptors()
{
    using DC = diagnostics::DiagnosticCategory;
    using DS = diagnostics::DiagnosticSeverity;
    std::vector<diagnostics::CodeDescriptor> out;
    out.reserve(56);   // §9.12 建议值 55＋IO-T04 表尾追加 1（IO-FORMAT-JSON-SYNTAX）

    // =================================================================
    // 格式-CSV 族（§9.12 第 2 行）——diagnostics §8.6：CSV 逐行错误→
    // 输入/Error，定位行/列/原文片段（片段经脱敏仅开发级保留——AT-02）。
    // =================================================================
    for (const std::string_view token : {"IO-FORMAT-CSV-DIALECT", "IO-FORMAT-CSV-ENCODING", "IO-FORMAT-CSV-QUOTE",
                                         "IO-FORMAT-CSV-ARITY", "IO-FORMAT-CSV-DUPCOL", "IO-FORMAT-CSV-CHAR"}) {
        out.push_back(makeDescriptor(token, DC::FormatOrVersion, DS::Error, "[\"row\",\"column\",\"snippet\"]"));
    }

    // =================================================================
    // 格式-JSON 族（§9.12 第 3 行）——版本码携带"文件版本/支持版本"比
    // 较对（PM-06 升级指引数据）；其余码定位 JSON path＋行列区间（§5.9）。
    // =================================================================
    for (const std::string_view token : {"IO-FORMAT-JSON-VERSION-MISSING", "IO-FORMAT-JSON-VERSION-TYPE",
                                         "IO-FORMAT-JSON-VERSION-FUTURE", "IO-FORMAT-JSON-VERSION-LEGACY"}) {
        out.push_back(makeDescriptor(token, DC::FormatOrVersion, DS::Error, "[\"file-version\",\"supported\"]"));
    }
    for (const std::string_view token : {"IO-FORMAT-JSON-ENCODING", "IO-FORMAT-JSON-DUPKEY", "IO-FORMAT-JSON-NUMBER",
                                         "IO-FORMAT-JSON-UNKNOWN", "IO-FORMAT-JSON-REQUIRED", "IO-FORMAT-JSON-TYPE",
                                         "IO-FORMAT-JSON-RANGE", "IO-FORMAT-JSON-SYNTAX"}) {
        out.push_back(makeDescriptor(token, DC::FormatOrVersion, DS::Error, "[\"path\",\"row\",\"column\"]"));
    }

    // =================================================================
    // 格式-包族（§9.12 第 4 行）——容器层码无参数（定位在 detail）；
    // 条目码定位条目名（脱敏呈现）。
    // =================================================================
    out.push_back(makeDescriptor("IO-FORMAT-PACK-ZIP", DC::FormatOrVersion, DS::Error, "[]"));
    out.push_back(makeDescriptor("IO-FORMAT-PACK-ENCRYPTED", DC::FormatOrVersion, DS::Error, "[]"));
    out.push_back(makeDescriptor("IO-FORMAT-PACK-ENTRY", DC::FormatOrVersion, DS::Error, "[\"entry\"]"));
    out.push_back(makeDescriptor("IO-FORMAT-PACK-MANIFEST", DC::FormatOrVersion, DS::Error, "[]"));

    // =================================================================
    // 格式-XML/网格族（§9.12 第 5 行）——循环码携带环清单（V15 观测点
    // "环清单内容"）；网格码定位文件。
    // =================================================================
    out.push_back(makeDescriptor("IO-FORMAT-XML-CYCLE", DC::FormatOrVersion, DS::Error, "[\"cycle\"]"));
    out.push_back(makeDescriptor("IO-FORMAT-MESH-UNKNOWN", DC::FormatOrVersion, DS::Error, "[\"path\"]"));

    // =================================================================
    // 安全-路径族（§9.12 第 6 行）——diagnostics §8.6：SafePath 违规→
    // 安全/Error；params＝角色＋脱敏路径（与 IoError 错误轨道同键）。
    // =================================================================
    for (const std::string_view token : {"IO-SEC-PATH-ESCAPE", "IO-SEC-PATH-SYMLINK", "IO-SEC-PATH-RESERVED",
                                         "IO-SEC-PATH-TOO-LONG"}) {
        out.push_back(makeDescriptor(token, DC::SecurityOrRedaction, DS::Error, "[\"role\",\"path\"]"));
    }

    // =================================================================
    // 安全-预算族＋炸弹（§9.12 第 7/8 行）——diagnostics §8.6：BudgetGuard
    // 超限→安全/Error、比较型三要素（actual/limit/unit）——requires
    // Comparison=true 强制实例携带 comparison。
    // =================================================================
    for (const std::string_view token : {"IO-SEC-BUDGET-FILE", "IO-SEC-BUDGET-TOTAL", "IO-SEC-BUDGET-COUNT",
                                         "IO-SEC-BUDGET-DEPTH", "IO-SEC-BUDGET-EXPAND", "IO-SEC-BUDGET-ROWS",
                                         "IO-SEC-BUDGET-FIELD", "IO-SEC-BUDGET-JSON", "IO-SEC-BUDGET-JSON-DEPTH",
                                         "IO-SEC-BUDGET-JSON-STRING", "IO-SEC-BUDGET-MESH", "IO-SEC-BUDGET-INCLUDE",
                                         "IO-SEC-BUDGET-REFDEPTH", "IO-SEC-BUDGET-TEMP", "IO-SEC-BOMB-RATIO"}) {
        out.push_back(makeDescriptor(token, DC::SecurityOrRedaction, DS::Error, "[\"actual\",\"limit\",\"unit\"]",
                                     /*requiresComparison=*/true));
    }

    // =================================================================
    // 资源族（§9.12 第 9 行）——四分类分码（§4.2.5）：不存在/缺失/变化→
    // ResourceMissing（relink/reimport 动作族）；权限/只读/锁→
    // PermissionOrLock（retry-readonly/contact-holder 动作族）。
    // =================================================================
    out.push_back(makeDescriptor("IO-RES-NOT-FOUND", DC::ResourceMissing, DS::Error, "[\"path\"]"));
    out.push_back(makeDescriptor("IO-RES-ACCESS-DENIED", DC::PermissionOrLock, DS::Error, "[\"path\",\"direction\"]"));
    out.push_back(makeDescriptor("IO-RES-READONLY", DC::PermissionOrLock, DS::Error, "[\"path\"]"));
    out.push_back(makeDescriptor("IO-RES-LOCK-CONFLICT", DC::PermissionOrLock, DS::Error, "[\"path\",\"pid\"]"));
    out.push_back(makeDescriptor("IO-RES-MISSING", DC::ResourceMissing, DS::Error, "[\"path\"]"));
    out.push_back(makeDescriptor("IO-RES-CHANGED", DC::ResourceMissing, DS::Error, "[\"path\",\"digest\"]"));

    // =================================================================
    // 包过程族（§9.12 第 10 行）——内容完整性码→FormatOrVersion（包文
    // 件本身非法/被篡改）；过程执行码→ExecutionFailed（导入导出事务中
    // 段失败——§7.6 状态图）。DISK-FULL 的所需/可用（§7.4）在实例 params
    // 携带，比较型强制位暂不置位——其消费（IPackageImporter，IO-T06）
    // 落位时随单元卡同步细化。
    // =================================================================
    out.push_back(makeDescriptor("IO-PACK-DUPLICATE-ENTRY", DC::FormatOrVersion, DS::Error, "[\"entries\"]"));
    out.push_back(makeDescriptor("IO-PACK-HASH-MISMATCH", DC::FormatOrVersion, DS::Error, "[\"entry\",\"expected\",\"actual\"]"));
    out.push_back(makeDescriptor("IO-PACK-REF-INCOMPLETE", DC::FormatOrVersion, DS::Error, "[\"ref\"]"));
    out.push_back(makeDescriptor("IO-PACK-TARGET-EXISTS", DC::ExecutionFailed, DS::Error, "[\"target\"]"));
    out.push_back(makeDescriptor("IO-PACK-DISK-FULL", DC::ExecutionFailed, DS::Error, "[\"required\",\"available\"]"));
    out.push_back(makeDescriptor("IO-PACK-CLEANUP-FAILED", DC::ExecutionFailed, DS::Error, "[\"residual-count\"]"));

    // =================================================================
    // 内部族（§9.12 第 11 行）——防御性开发级：Dev 强制不可见/不入报
    // 告/不入历史（§4.5 注册期验证；触及即报缺陷）。
    // =================================================================
    out.push_back(makeDescriptor("IO-FORMAT-INTERNAL", DC::Internal, DS::Dev, "[]",
                                 /*requiresComparison=*/false, /*userVisible=*/false,
                                 /*reportable=*/false, /*historical=*/false));

    return out;
}

void registerIoCodeTable(diagnostics::IDiagnosticRegistry& registry)
{
    // 逐条转发注册——任何 DiagnosticsError（句法/重复/前缀冲突）向上抛
    // 给装配期（IoRuntime::create——§9.11"码表注册在此完成"），不捕获
    // 不吞：装配期码表冲突＝集成缺陷，fail-fast（AGENTS.md 错误语义）。
    for (const diagnostics::CodeDescriptor& d : ioCodeDescriptors()) {
        registry.registerCode(d);
    }
}

IoError makeComparativeError(IoErrorCode code, std::uint64_t actual, std::uint64_t limit,
                             std::string_view unit, std::string detail)
{
    IoError e;
    e.code = code;
    // 三要素固定键序 actual/limit/unit——与预算族 paramSchema 对齐（工
    // 厂占位一致性校验可过）；数值十进制定点文本化（不经 locale——
    // NFR-COR-02；IoError.hpp 确定性注释同源）。
    e.params.emplace_back("actual", std::to_string(actual));
    e.params.emplace_back("limit", std::to_string(limit));
    e.params.emplace_back("unit", std::string(unit));
    e.detail = std::move(detail);
    return e;
}

IoError makeRowColError(IoErrorCode code, std::uint64_t rowNo, std::uint64_t colNo,
                        std::string_view snippet, std::string detail)
{
    IoError e;
    e.code = code;
    // 三键固定序 row/column/snippet——与 CSV 族 paramSchema（§3.1
    // ioCodeDescriptors 注："CSV 族 [\"row\",\"column\",\"snippet\"]"）
    // 对齐；行/列号十进制文本化不经 locale（NFR-COR-02 确定性，与
    // makeComparativeError 同款纪律）。snippet 原样收编：截断是调用方
    // （CsvRowError::rawSnippet，120 字节 UTF-8 边界）的职责——本助手
    // 是纯构造点，不做二次加工（单一职责，方便测试钉住键序与格式）。
    e.params.emplace_back("row", std::to_string(rowNo));
    e.params.emplace_back("column", std::to_string(colNo));
    e.params.emplace_back("snippet", std::string(snippet));
    e.detail = std::move(detail);
    return e;
}

} // namespace sdurws::ird::io
