/**
 * @file   IoDiagnosticsTest.cpp
 * @brief  io 诊断面用例组（IoDiag）——IO-\* 稳定诊断码表向 diagnostics
 *         StableCodeRegistry 的注册（P-IO-6）、§9.12 全集机械比对、描述
 *         符登记值（分类/严重/paramSchema/比较型强制/Dev 不可见）与内置
 *         码表合注册的确定性。
 *
 * 设计依据：
 *   - units/io.md §9.12（码汇总建议值——P-IO-6"以 §9.12 建议值注册，不
 *     私定码值"）、§10.3（io→diagnostics 产出 IO-\* CodeDescriptor）
 *   - units/diagnostics.md §4.5（注册期验证行为——本文件以真实
 *     StableCodeRegistry 验证 io 描述符全部通过）、§8.6（预算码比较型
 *     强制）
 *   - 任务契约 tasks/foundation/IO-T02.json acceptance 3/5
 *
 * 收编确认锚点（如实声明）：IO-\* 码值进入 diagnostics 内置装配清单
 * （§4.6 收编）属 diagnostics 所有者的治理动作——本任务产出注册面并以
 * 真实注册表验证，治理侧收编确认随验收请求提请（P-IO-6 处置记录见
 * traceability 留痕），不在实现侧私自扩编 diagnostics 单元文件。
 */

#include <sdurws/ird/io/IoDiagnostics.hpp>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using sdurws::ird::diagnostics::CodeDescriptor;
using sdurws::ird::diagnostics::CodeTableManifest;
using sdurws::ird::diagnostics::DiagnosticCategory;
using sdurws::ird::diagnostics::DiagnosticSeverity;
using sdurws::ird::diagnostics::RetryKind;
using sdurws::ird::diagnostics::StableCodeRegistry;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::errorCodeToken;
using sdurws::ird::io::ioCodeDescriptors;
using sdurws::ird::io::makeComparativeError;
using sdurws::ird::io::registerIoCodeTable;

namespace {

/// §9.12 建议值全集的**应注册码面**（除 Ok/IO-CANCELLED——状态码不落诊
/// 断，UX-03；§9.12 首行）。字面清单独立于实现枚举——双清单机械比对，
/// 任一侧漂移即失败（失同步防线）。
const char* kSection912DiagnosticTokens[] = {
    "IO-FORMAT-CSV-DIALECT", "IO-FORMAT-CSV-ENCODING", "IO-FORMAT-CSV-QUOTE",
    "IO-FORMAT-CSV-ARITY", "IO-FORMAT-CSV-DUPCOL", "IO-FORMAT-CSV-CHAR",
    "IO-FORMAT-JSON-ENCODING", "IO-FORMAT-JSON-DUPKEY", "IO-FORMAT-JSON-NUMBER",
    "IO-FORMAT-JSON-VERSION-MISSING", "IO-FORMAT-JSON-VERSION-TYPE",
    "IO-FORMAT-JSON-VERSION-FUTURE", "IO-FORMAT-JSON-VERSION-LEGACY",
    "IO-FORMAT-JSON-UNKNOWN", "IO-FORMAT-JSON-REQUIRED", "IO-FORMAT-JSON-TYPE",
    "IO-FORMAT-JSON-RANGE",
    // IO-T04 表尾追加（DTB §5.4 单元卡增量修订——§9.12 JSON 族行同步；
    // 语法层违例的码面缺口补登，IoError.hpp FormatJsonSyntax 成员注）。
    "IO-FORMAT-JSON-SYNTAX",
    "IO-FORMAT-PACK-ZIP", "IO-FORMAT-PACK-ENCRYPTED", "IO-FORMAT-PACK-ENTRY",
    "IO-FORMAT-PACK-MANIFEST",
    "IO-FORMAT-XML-CYCLE",
    // IO-T05 表尾追加（DTB §5.4 单元卡增量修订——§9.12 XML 族行同步；
    // §6.1 XML 良构检查的码面缺口补登，IoError.hpp FormatXmlSyntax 成员注）。
    "IO-FORMAT-XML-SYNTAX",
    "IO-FORMAT-MESH-UNKNOWN",
    "IO-SEC-PATH-ESCAPE", "IO-SEC-PATH-SYMLINK", "IO-SEC-PATH-RESERVED",
    "IO-SEC-PATH-TOO-LONG",
    "IO-SEC-BUDGET-FILE", "IO-SEC-BUDGET-TOTAL", "IO-SEC-BUDGET-COUNT",
    "IO-SEC-BUDGET-DEPTH", "IO-SEC-BUDGET-EXPAND", "IO-SEC-BUDGET-ROWS",
    "IO-SEC-BUDGET-FIELD", "IO-SEC-BUDGET-JSON", "IO-SEC-BUDGET-JSON-DEPTH",
    "IO-SEC-BUDGET-JSON-STRING", "IO-SEC-BUDGET-MESH", "IO-SEC-BUDGET-INCLUDE",
    "IO-SEC-BUDGET-REFDEPTH", "IO-SEC-BUDGET-TEMP", "IO-SEC-BOMB-RATIO",
    "IO-RES-NOT-FOUND", "IO-RES-ACCESS-DENIED", "IO-RES-READONLY",
    "IO-RES-LOCK-CONFLICT", "IO-RES-MISSING", "IO-RES-CHANGED",
    "IO-PACK-DUPLICATE-ENTRY", "IO-PACK-HASH-MISMATCH", "IO-PACK-REF-INCOMPLETE",
    "IO-PACK-TARGET-EXISTS", "IO-PACK-DISK-FULL", "IO-PACK-CLEANUP-FAILED",
    "IO-FORMAT-INTERNAL",
};

} // namespace

// =====================================================================
// token 映射（枚举 ↔ §9.12 连字符串）
// =====================================================================

/**
 * errorCodeToken 全枚举覆盖：每个枚举成员 token 非空且形如 §9.12（大写
 * 连字符；Ok/"IO-CANCELLED" 两状态值按首行原文）；除 Ok/Cancelled 外全
 * 部命中字面清单（防枚举与 token 表失同步）。
 */
TEST(IoDiagCodeTable, TokenMappingCoversAllEnumValuesAndMatchesSection912)
{
    // 全枚举（IoError.hpp §9.12 表行序 57 值＋IO-T04/T05 表尾追加各 1 值＝59 值）——token 非空＋词形合法。
    for (int v = 0; v <= static_cast<int>(IoErrorCode::FormatXmlSyntax); ++v) {
        const std::string tok(errorCodeToken(static_cast<IoErrorCode>(v)));
        EXPECT_FALSE(tok.empty()) << "枚举值 " << v << " 无 token（switch 缺项）";
        if (v != static_cast<int>(IoErrorCode::Ok)) {
            // Ok 非诊断码（无 IO- 词形约束）；其余 token 全部过句法校验。
            EXPECT_TRUE(sdurws::ird::diagnostics::isValidDiagCodeSyntax(tok))
                << "token 词形非法: " << tok;
        }
    }
    EXPECT_EQ(errorCodeToken(IoErrorCode::Ok), "Ok") << "§9.12 首行";
    EXPECT_EQ(errorCodeToken(IoErrorCode::Cancelled), "IO-CANCELLED") << "状态码（非诊断）";

    // 枚举侧 token 集 ↔ 字面清单集：双向一致（无多映射、无漏映射）。
    std::set<std::string> fromEnum;
    for (int v = 0; v <= static_cast<int>(IoErrorCode::FormatXmlSyntax); ++v) {
        const std::string tok(errorCodeToken(static_cast<IoErrorCode>(v)));
        if (v != static_cast<int>(IoErrorCode::Ok) && v != static_cast<int>(IoErrorCode::Cancelled)) {
            fromEnum.insert(tok);
        }
    }
    std::set<std::string> fromCard(std::begin(kSection912DiagnosticTokens),
                                   std::end(kSection912DiagnosticTokens));
    EXPECT_EQ(fromEnum, fromCard) << "枚举 token 集≠§9.12 应注册集（失同步）";
}

// =====================================================================
// 码表注册（P-IO-6 执行面——真实 StableCodeRegistry）
// =====================================================================

/**
 * 注册行为：57 条描述符全部通过注册期验证（§4.5——句法/前缀-所有权/
 * paramSchema/Dev 强制）；ownerUnit=io 全表可列且字典序；Ok/Cancelled
 * 不在注册表（状态码不落诊断）；重复注册被注册表拒绝（DuplicateCode）
 * ——码值冻结的注册表面。
 */
TEST(IoDiagCodeTable, RegistrationIntoStableCodeRegistryFullTableVerified)
{
    const std::vector<CodeDescriptor> descriptors = ioCodeDescriptors();
    const std::size_t expected = std::size(kSection912DiagnosticTokens);
    ASSERT_EQ(descriptors.size(), expected) << "描述符数＝§9.12 应注册码数（57——§9.12 建议值 55＋表尾追加 2）";

    StableCodeRegistry registry;
    registerIoCodeTable(registry);              // 不抛＝全部通过注册期验证
    registry.seal();

    // 逐码可查＋登记值锚点（§8.6 映射义务）。
    for (const CodeDescriptor& d : descriptors) {
        const CodeDescriptor* found = registry.find(d.code);
        ASSERT_NE(found, nullptr) << d.code << " 未注册";
        EXPECT_EQ(found->ownerUnit, "io") << d.code;
        // 文案键命名约定 diag.<code-lower>.title/.detail（§4.5——键为码文
        // 本的小写形；注册往返保真；键归注册表、值归 ui 文案资源——UX-02）。
        std::string lowerCode(d.code);
        std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        EXPECT_EQ(found->titleKey, "diag." + lowerCode + ".title") << d.code;
        EXPECT_EQ(found->detailKey, "diag." + lowerCode + ".detail") << d.code;
        // 预算族：比较型强制＋禁确认（可确认必为比较型，反之不必然）。
        if (d.code.rfind("IO-SEC-BUDGET-", 0) == 0 || d.code == "IO-SEC-BOMB-RATIO") {
            EXPECT_TRUE(found->requiresComparison) << d.code << " 预算码须比较型（§8.6）";
            EXPECT_FALSE(found->confirmable) << d.code;
            EXPECT_EQ(found->paramSchema, "[\"actual\",\"limit\",\"unit\"]") << d.code;
            EXPECT_EQ(found->category, DiagnosticCategory::SecurityOrRedaction) << d.code;
            EXPECT_EQ(found->severity, DiagnosticSeverity::Error) << d.code;
        }
        // SafePath 族：安全分类＋Error（§8.6）＋role/path 参数面。
        if (d.code.rfind("IO-SEC-PATH-", 0) == 0) {
            EXPECT_EQ(found->category, DiagnosticCategory::SecurityOrRedaction) << d.code;
            EXPECT_EQ(found->paramSchema, "[\"role\",\"path\"]") << d.code;
        }
        // Dev 码（内部族）：强制不可见/不入报告/不入历史（§4.5 注册期验证）。
        if (d.code == "IO-FORMAT-INTERNAL") {
            EXPECT_EQ(found->severity, DiagnosticSeverity::Dev);
            EXPECT_FALSE(found->userVisible);
            EXPECT_FALSE(found->reportable);
            EXPECT_FALSE(found->historical);
        }
    }

    // ownerUnit=io 全表列码（字典序——§9.1 registeredCodes 确定性）。
    const std::vector<std::string> listed = registry.registeredCodes("io");
    ASSERT_EQ(listed.size(), expected);
    EXPECT_TRUE(std::is_sorted(listed.begin(), listed.end())) << "字典序（确定性观测面）";
    EXPECT_TRUE(registry.registeredCodes("no-such-unit").empty()) << "无匹配→空表";

    // Ok/Cancelled 不注册（状态码——§9.12 首行/UX-03）。
    EXPECT_EQ(registry.find("Ok"), nullptr) << "Ok 非诊断码";
    EXPECT_EQ(registry.find("IO-CANCELLED"), nullptr) << "取消＝状态非错误，不落诊断";

    // manifest 稳定（两次计算相等——DT-REG-1 同款观测）。
    const CodeTableManifest m1 = registry.manifest();
    const CodeTableManifest m2 = registry.manifest();
    EXPECT_EQ(m1, m2) << "manifest 两次计算稳定";
    EXPECT_EQ(m1.entries.size(), expected);

    // 重复注册＝注册表拒绝（码值冻结面；seal 后运行期同样拒绝）。
    StableCodeRegistry dup;
    registerIoCodeTable(dup);
    EXPECT_THROW(registerIoCodeTable(dup), sdurws::ird::diagnostics::DiagnosticsError)
        << "同码二次注册须 DuplicateCode";
}

/**
 * 与 diagnostics 内置码表合注册（L5 装配真实形态）：内置 87 码＋IO 57
 * 码共存无冲突；注册顺序不影响 manifest（io 先/builtin 先同摘要——装配
 * 顺序无关性，跨进程一致性的装配侧保障）。
 */
TEST(IoDiagCodeTable, CoexistsWithBuiltinTableOrderIndependentManifest)
{
    // 形态 A：内置表先、io 后。
    StableCodeRegistry a;
    sdurws::ird::diagnostics::registerBuiltinCodes(a);
    registerIoCodeTable(a);
    a.seal();
    // 形态 B：io 先、内置表后。
    StableCodeRegistry b;
    registerIoCodeTable(b);
    sdurws::ird::diagnostics::registerBuiltinCodes(b);
    b.seal();

    EXPECT_EQ(a.manifest(), b.manifest()) << "注册顺序无关——同集合同摘要";
    EXPECT_EQ(a.registeredCodes("io").size(), std::size(kSection912DiagnosticTokens));
    // 内置表无 IO 前缀码（收编清单 PRJ/RT/POLICY/EVI/EX/RPT/DIAG——
    // diagnostics §4.6；IO 码归 io 侧产出，收编确认随治理侧提请）。
    for (const std::string& code : a.registeredCodes("io")) {
        EXPECT_EQ(code.rfind("IO-", 0), 0u) << "io 名下全部 IO- 前缀";
    }
}

// =====================================================================
// 比较型三要素构造助手（acceptance 3 诊断形状）
// =====================================================================

/**
 * makeComparativeError：params 恰三键（actual/limit/unit，构造序）＋数
 * 值定点文本化（不经 locale——确定性）＋detail 透传；码直通。
 */
TEST(IoDiagCodeTable, ComparativeErrorCarriesThreeElementParams)
{
    const sdurws::ird::io::IoError e = makeComparativeError(
        IoErrorCode::SecBudgetRows, 100001, 100000, "count", "行预算超限——V06");
    EXPECT_EQ(e.code, IoErrorCode::SecBudgetRows);
    ASSERT_EQ(e.params.size(), 3u) << "恰三要素";
    EXPECT_EQ(e.params[0].first, "actual");
    EXPECT_EQ(e.params[0].second, "100001");
    EXPECT_EQ(e.params[1].first, "limit");
    EXPECT_EQ(e.params[1].second, "100000");
    EXPECT_EQ(e.params[2].first, "unit");
    EXPECT_EQ(e.params[2].second, "count");
    EXPECT_EQ(e.detail, "行预算超限——V06");
}
