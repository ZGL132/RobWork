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
#include <sdurws/ird/diagnostics/Logging.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/io/Csv.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
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

// =====================================================================
// IO-V30：诊断脱敏两级（NFR-SEC-07/ERR-01/REQ-05 支撑——IO-T07
// acceptance 1/4；O-09 对端＝diagnostics sink fake 承载）
// =====================================================================

namespace {

/**
 * diagnostics sink 替身（O-09 处置——"V30 的对端以 diagnostics sink fake
 * 承载"）：io 只产出结构化诊断事实（稳定码 token＋定位参数＋开发级原文
 * 片段字段），诊断落入的日志槽位与两级呈现组装归 diagnostics/ui 侧——
 * 本替身承载"双级行槽"：
 *   - User 级（Tier-U＝稳定码＋脱敏参数＋对象定位，diagnostics.md §9.6
 *     原文口径）：**不渲染**开发级字段（rawSnippet 结构上不进用户级行），
 *     路径经真实 RedactionService::redactPath 脱敏后入行；
 *   - Dev 级（Tier-D＝全量技术细节）：整条原文经 redact(raw, Dev) 入行
 *     （真实脱敏设施——凭据/路径形态仍过滤，普通技术细节保留）。
 * 脱敏规则本体是真实 RedactionService（io 产品库本就链接 diagnostics）；
 * fake 的是"槽位与呈现组装"，不是脱敏规则本身。
 */
class FakeTwoTierSink {
public:
    explicit FakeTwoTierSink(const sdurws::ird::diagnostics::IRedactionService& redaction)
        : m_redaction(redaction)
    {
    }

    /// 接收一条诊断的结构化面（原文片段＋定位＋来源路径），两级入槽。
    void emit(const std::string& codeToken, std::uint64_t row, std::uint64_t col,
              const std::string& devSnippet, const std::string& sourcePath)
    {
        using sdurws::ird::diagnostics::LogTier;
        // 用户级行：稳定码＋定位＋脱敏后路径——devSnippet（开发级字段）
        // 不参与组装（Tier-U 词表外字段一概不渲染——NFR-SEC-07 分层面）。
        userLine_ = codeToken + " row=" + std::to_string(row)
                    + " col=" + std::to_string(col)
                    + " source=" + m_redaction.redactPath(sourcePath);
        // 开发级行：整条原文经 Dev 档脱敏（凭据/环境变量仍过滤——§9.5）。
        devLine_ = m_redaction.redact(
            codeToken + " row=" + std::to_string(row) + " col=" + std::to_string(col)
                + " snippet=" + devSnippet + " source=" + sourcePath,
            LogTier::Dev);
    }

    const std::string& userLine() const { return userLine_; }
    const std::string& devLine() const { return devLine_; }

private:
    const sdurws::ird::diagnostics::IRedactionService& m_redaction;  ///< 调用方持有
    std::string userLine_;   ///< 用户级槽位行（Tier-U）
    std::string devLine_;    ///< 开发级槽位行（Tier-D）
};

} // namespace

/**
 * 产码→sink 全链脱敏断言（V30"用户级仅定位、开发级片段保留、报告无原
 * 始文本"）：①真实 io 产码——CSV 行错误把原文片段承载为**独立开发级字
 * 段** rawSnippet（定位参数 row/column 不携带原文——io 结构面已用户安
 * 全）；②sink fake 两级入槽（路径经真实 RedactionService 脱敏）；③断
 * 言：用户级无完整路径（默认 RootOnly：盘符＋一级目录＋…＋文件名）、保
 * 留文件名可定位、无片段；开发级片段保留；safeSummary（报告导出双保险
 * 入口）无路径原文＋截断标注。
 */
TEST(IoDiagSanitization, UserTierLocatesWithoutFullPathOrRawSnippetDevKeepsSnippet)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V30 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-07", "REQ-05"},
                  std::vector<std::string>{"AT-02"});

    namespace fs = std::filesystem;
    using sdurws::ird::diagnostics::RedactionService;
    using sdurws::ird::io::CsvReadOptions;
    using sdurws::ird::io::RawTable;
    using sdurws::ird::io::errorCodeToken;
    using sdurws::ird::io::makeCsvReader;

    // ---- ① 真实产码：无标识 CSV，表头 2 列、数据行 4 列（默认 Reject
    // 策略→行错误）；片段单元格＝带空格的短短语（<40 字符且含空格——
    // 不命中凭据键值/独立长凭证串/长十六进制等脱敏规则，判据聚焦路径与
    // 原文片段本身）。
    const std::string rawFragment = "raw cell fragment kept for dev tier";
    const fs::path csvPath = fs::temp_directory_path() / "ird-io-v30-sanit.csv";
    {
        std::ofstream f(csvPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(f.is_open());
        f << "name,val\n";
        f << rawFragment << ",extra1,extra2,extra3\n";
    }

    auto reader = makeCsvReader();
    CsvReadOptions ropts;
    ropts.headerRow = 1;
    const sdurws::ird::io::IoResult<RawTable> parsed = reader->read(
        csvPath, ropts,
        [](std::uint64_t, sdurws::ird::io::CsvRowView&&) { return true; },
        nullptr, nullptr);
    ASSERT_TRUE(parsed) << parsed.error.detail;
    ASSERT_EQ(parsed.value.rowErrors.size(), 1u)
        << "前置：恰一条行错误（超列数 Reject）";
    const sdurws::ird::io::CsvRowError rowErr = parsed.value.rowErrors.front();
    // rawSnippet 承载片段原文（截断上限 120 B——本片段 35 B 全量在内）。
    EXPECT_NE(rowErr.rawSnippet.find("raw cell fragment kept"), std::string::npos)
        << "前置：rawSnippet 携带片段原文";
    // io 结构面登记口径（脱敏第一道防线在产码侧——IoError.hpp 头注）：
    // params 按 CSV 族 paramSchema＝["row","column","snippet"]（IoDiagnostics
    // 注册值）承载——其中 snippet 是**登记的开发级参数**（呈现须经脱敏/
    // 分级过滤——AT-02"用户级仅定位"），row/column 为用户安全定位；完整
    // 本机路径不得进 params（路径仅经 display 脱敏形态出场）。
    bool sawRow = false;
    bool sawCol = false;
    bool sawSnippet = false;
    for (const auto& kv : rowErr.reason.params) {
        EXPECT_EQ(kv.second.find(csvPath.string()), std::string::npos)
            << "参数携带完整本机路径（kv=" << kv.first << "）";
        if (kv.first == "row") {
            sawRow = true;
        }
        if (kv.first == "column") {
            sawCol = true;
        }
        if (kv.first == "snippet") {
            sawSnippet = true;
        }
    }
    EXPECT_TRUE(sawRow) << "缺 row 定位参数（paramSchema 登记面）";
    EXPECT_TRUE(sawCol) << "缺 col 定位参数（paramSchema 登记面）";
    EXPECT_TRUE(sawSnippet) << "缺 snippet 开发级参数（paramSchema 登记面）";

    // ---- ② 真实脱敏设施（默认策略 RootOnly/32——§9.5 原文默认值）＋
    // sink fake 双级入槽（原始片段仅作为开发级字段传入）。
    RedactionService redaction;                 // 默认策略——产品默认口径
    FakeTwoTierSink sink(redaction);
    sink.emit(std::string(errorCodeToken(rowErr.reason.code)), rowErr.rowNo,
              rowErr.colNo, rowErr.rawSnippet, csvPath.string());

    // ---- ③ 用户级仅定位：无完整路径（绝对路径原文不出现在呈现行），
    // 但保留文件名（RootOnly"盘符＋一级目录＋…＋文件名"——可定位入口）；
    // 开发级片段不出现（Tier-U 不渲染开发级字段）。
    EXPECT_EQ(sink.userLine().find(csvPath.string()), std::string::npos)
        << "用户级泄露完整本机路径：" << sink.userLine();
    EXPECT_EQ(sink.userLine().find(csvPath.parent_path().string()), std::string::npos)
        << "用户级泄露路径目录段";
    EXPECT_NE(sink.userLine().find("sanit.csv"), std::string::npos)
        << "用户级应保留文件名（仅定位口径）";
    EXPECT_EQ(sink.userLine().find("raw cell fragment"), std::string::npos)
        << "用户级出现原文片段（开发级字段渲染进 Tier-U）";

    // ---- 开发级片段保留（Tier-D 全量技术细节——NFR-SEC-07 分层本意）。
    EXPECT_NE(sink.devLine().find("raw cell fragment"), std::string::npos)
        << "开发级未保留原文片段";

    // ---- 报告导出面（exportSafeSummary→safeSummary 双保险入口）：报告
    // 管线以 safeSummary 兜底导出诊断原文——敏感的完整路径不得出现＋
    // 超限截断标注（本断言把含路径原文喂入，验证设施真实过滤）。
    const std::string rawWithSource =
        std::string(errorCodeToken(rowErr.reason.code))
        + " snippet=" + rowErr.rawSnippet + " source=" + csvPath.string();
    const std::string summary = redaction.safeSummary(rawWithSource, 64);
    EXPECT_EQ(summary.find(csvPath.string()), std::string::npos) << "摘要泄露完整路径";
    EXPECT_NE(summary.find("[trunc]"), std::string::npos) << "截断未标注";

    // ---- 现场清理（本用例自持——临时文件不残留）。
    std::error_code cleanupEc;
    fs::remove(csvPath, cleanupEc);
}
