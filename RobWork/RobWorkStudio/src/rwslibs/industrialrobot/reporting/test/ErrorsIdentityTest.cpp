/**
 * @file   ErrorsIdentityTest.cpp
 * @brief  reporting 错误与身份类型单测——错误码 token 全表、ReportError
 *         前缀、RPT-* 登记清单、ReportId 句法/保留值/往返、ReportLevel
 *         token 句法（RPT-T02 acceptance 1/2/3 的单元内具名自证）。
 *
 * 设计依据：
 *   - units/reporting.md §3.5（16 token 清单原文＋ReportError 前缀口径＋
 *     RPT-* 收编 8 项）、§4.1（ReportId 规范文本/保留值/身份纪律）、§4.2
 *     （level token level-b/level-c）、§10.1 RP-GATE-1（红线由 BuildRedLine
 *     组承载——本文件为类型面行为用例）
 *   - 需求 RPT-01（身份/错误语义承载）、ERR-01（稳定诊断码族——§2.1 O-15）、
 *     NFR-COR-02（确定性：同码同串/同串同码）
 *   - 任务契约 tasks/foundation/RPT-T02.json acceptance 1（身份往返/token
 *     句法/保留值）、acceptance 2（16 token 逐值与 §3.5 一致；detail 前缀）、
 *     acceptance 3（8 项码清单逐值与收编表一致——字面量级钉住；注册表侧
 *     交叉自证在 contract_test/IdentityCrossUnitContractTest.cpp）
 *
 * 用例命名约定：`<主题>_<锚点>` 尾缀标注需求/AT/acceptance 追溯字段
 * （AGENTS.md §2.7 测试注释规范——每用例注释首行声明"验证哪条验收"）。
 * 本文件为纯 reporting 单元内用例（零上游单元头——core/diagnostics 消费
 * 的跨单元契约自证落位 contract_test/，与 §3.4 测试目标分工一致）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Identity.hpp>

#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using sdurws::ird::reporting::diagcodes::kStableCodes;
using sdurws::ird::reporting::ReportError;
using sdurws::ird::reporting::ReportErrorCode;
using sdurws::ird::reporting::ReportId;
using sdurws::ird::reporting::ReportLevel;
using sdurws::ird::reporting::token;

// =====================================================================
// acceptance 2：ReportErrorCode 16 值 token 全表（§3.5 清单逐值一致）
// =====================================================================

/**
 * 验证 acceptance 2：16 个 token 稳定错误码逐值与 §3.5 清单一致。
 *
 * 三重钉住：①期望表为测试内独立字面（不调用被测 token() 生成——防止
 * "实现即期望"的恒真自证）；②全表无重复（码面唯一——持久化检索前提）；
 * ③数量恰 16（清单全量——增删必失败）。
 */
TEST(ReportingErrors, ErrorTokenTable_Full16MatchesDesign_RPT02_ACC2)
{
    // 期望表：§3.5 清单注释列原文逐值（kebab 串）——与枚举声明序对应。
    const std::pair<ReportErrorCode, std::string_view> kExpected[] = {
        {ReportErrorCode::SourceMissing,       "reporting/source-missing"},
        {ReportErrorCode::SourceAmbiguous,     "reporting/source-ambiguous"},
        {ReportErrorCode::ScopeInsufficient,   "reporting/scope-insufficient"},
        {ReportErrorCode::LevelConflict,       "reporting/level-conflict"},
        {ReportErrorCode::EvidenceRefInvalid,  "reporting/evidence-ref-invalid"},
        {ReportErrorCode::ConsistencyMismatch, "reporting/consistency-mismatch"},
        {ReportErrorCode::RenderFailed,        "reporting/render-failed"},
        {ReportErrorCode::DataInvalid,         "reporting/data-invalid"},
        {ReportErrorCode::ArchiveConflict,     "reporting/archive-conflict"},
        {ReportErrorCode::ExportFailed,        "reporting/export-failed"},
        {ReportErrorCode::DiskFull,            "reporting/disk-full"},
        {ReportErrorCode::ReadOnlyStore,       "reporting/read-only-store"},
        {ReportErrorCode::RoundtripMismatch,   "reporting/roundtrip-mismatch"},
        {ReportErrorCode::BundleIncomplete,    "reporting/bundle-incomplete"},
        {ReportErrorCode::TemplateVersion,     "reporting/template-version"},
        {ReportErrorCode::Usage,               "reporting/usage"},
    };
    ASSERT_EQ(std::size(kExpected), 16u) << "§3.5 清单应为 16 值——增删即本行失败";

    std::set<std::string_view> seen;
    for (const auto& [code, expectedToken] : kExpected) {
        // 逐值：token() 输出与清单一字不差（稳定 token——持久化面）。
        EXPECT_EQ(token(code), expectedToken) << "码值 " << static_cast<int>(code);
        // 前缀统一 "reporting/"（§3.5 detail 前缀口径的码面半区）。
        EXPECT_EQ(expectedToken.substr(0, 10), "reporting/");
        seen.insert(expectedToken);
    }
    // 全表无重复（16 值 16 串——码面唯一）。
    EXPECT_EQ(seen.size(), 16u);
}

/**
 * 验证 acceptance 2：ReportError 的 what() 前缀 "reporting/<域>:" 口径与
 * code() 访问器（§3.5 类契约原文）。
 *
 * 边界：detail 为空时 what() 恰为 token（无尾随冒号空格）；detail 非空时
 * 形如 "<token>: <detail>"；异常可按 std::runtime_error/std::exception
 * 捕获（继承面——既有 catch 兼容）。
 */
TEST(ReportingErrors, ErrorWhatPrefixAndCodeAccess_RPT02_ACC2)
{
    // 非空 detail：前缀＝token＋": "，detail 原样保留。
    const ReportError withDetail{ReportErrorCode::SourceMissing, "rev-xyz 未 finalize"};
    EXPECT_EQ(withDetail.what(),
              std::string{"reporting/source-missing: rev-xyz 未 finalize"});
    EXPECT_EQ(withDetail.code(), ReportErrorCode::SourceMissing);

    // 空 detail：what() 恰为 token（无噪音字符——makeWhat 空细节分支）。
    const ReportError noDetail{ReportErrorCode::DiskFull};
    EXPECT_EQ(noDetail.what(), std::string{"reporting/disk-full"});
    EXPECT_EQ(noDetail.code(), ReportErrorCode::DiskFull);

    // 继承面：按 std::runtime_error/std::exception 捕获（跨层 catch 兼容）。
    try {
        throw ReportError{ReportErrorCode::Usage, "bad request"};
    } catch (const std::runtime_error& e) {
        EXPECT_EQ(std::string{e.what()}, "reporting/usage: bad request");
    } catch (...) {
        FAIL() << "ReportError 应可按 std::runtime_error 捕获";
    }
}

// =====================================================================
// acceptance 3：RPT-* 稳定诊断码清单登记（字面量级——注册表侧交叉自证
// 在 contract_test/IdentityCrossUnitContractTest.cpp）
// =====================================================================

/**
 * 验证 acceptance 3：登记清单全量 8 项、逐值与 diagnostics.md §4.6 收编
 * 表一字不差、顺序＝收编清单序。
 */
TEST(ReportingErrors, RptStableCodeList_EightItemsMatchRegistry_RPT02_ACC3)
{
    // 期望表：diagnostics.md §4.6 收编清单序（＝reporting.md §3.5 清单序）。
    const std::string_view kExpected[] = {
        "RPT-SOURCE-MISSING",
        "RPT-SCOPE-INSUFFICIENT",
        "RPT-CONSISTENCY-MISMATCH",
        "RPT-ARCHIVE-CONFLICT",
        "RPT-EXPORT-FAILED",
        "RPT-ROUNDTRIP-MISMATCH",
        "RPT-CURRENTNESS-UNEVALUABLE",
        "RPT-SECTION-NOT-APPLICABLE",
    };
    ASSERT_EQ(kStableCodes.size(), 8u) << "收编全量 8 项——增删即本行失败";
    ASSERT_EQ(std::size(kExpected), 8u);
    for (std::size_t i = 0; i < std::size(kExpected); ++i) {
        EXPECT_EQ(kStableCodes[i], kExpected[i]) << "第 " << i << " 项与收编表不一致";
    }

    // 全表无重复＋稳定码句法（^[A-Z0-9]+(-[A-Z0-9]+)*$ 的字符集半区——
    // 逐字符校验；完整句法权威在 diagnostics isValidDiagCodeSyntax，
    // 此处为单元内不依赖上游的常驻下限）。
    std::set<std::string_view> seen;
    for (const auto code : kStableCodes) {
        EXPECT_FALSE(code.empty());
        bool prevHyphen = true;   // 首字符视同"前一位是连字符"——拒绝首连字符
        for (const char c : code) {
            if (c == '-') {
                EXPECT_FALSE(prevHyphen) << "连续/首尾连字符: " << code;
                prevHyphen = true;
            } else {
                ASSERT_TRUE((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                    << "小写/符号越界（稳定码仅大写与数字）: " << code;
                prevHyphen = false;
            }
        }
        EXPECT_FALSE(prevHyphen) << "尾连字符: " << code;
        seen.insert(code);
    }
    EXPECT_EQ(seen.size(), 8u);
}

/**
 * 验证 acceptance 3：suggestedDiagCode 映射面——6 个错误码有对应建议码
 * （引用登记清单常量），其余 10 个返回 nullopt（映射面声明见 Errors.hpp）。
 */
TEST(ReportingErrors, SuggestedDiagCodeMapping_SixOfSixteen_RPT02_ACC3)
{
    using sdurws::ird::reporting::diagcodes::kArchiveConflict;
    using sdurws::ird::reporting::diagcodes::kConsistencyMismatch;
    using sdurws::ird::reporting::diagcodes::kExportFailed;
    using sdurws::ird::reporting::diagcodes::kRoundtripMismatch;
    using sdurws::ird::reporting::diagcodes::kScopeInsufficient;
    using sdurws::ird::reporting::diagcodes::kSourceMissing;
    using sdurws::ird::reporting::suggestedDiagCode;

    // 有映射的 6 值：建议码＝登记清单常量（引用而非第二份字面）。
    EXPECT_EQ(suggestedDiagCode(ReportErrorCode::SourceMissing), kSourceMissing);
    EXPECT_EQ(suggestedDiagCode(ReportErrorCode::ScopeInsufficient), kScopeInsufficient);
    EXPECT_EQ(suggestedDiagCode(ReportErrorCode::ConsistencyMismatch), kConsistencyMismatch);
    EXPECT_EQ(suggestedDiagCode(ReportErrorCode::ArchiveConflict), kArchiveConflict);
    EXPECT_EQ(suggestedDiagCode(ReportErrorCode::ExportFailed), kExportFailed);
    EXPECT_EQ(suggestedDiagCode(ReportErrorCode::RoundtripMismatch), kRoundtripMismatch);

    // 无映射的 10 值：nullopt（过程性/调用方错误——不进报告诊断主轴）。
    for (const auto code : {
             ReportErrorCode::SourceAmbiguous,   ReportErrorCode::LevelConflict,
             ReportErrorCode::EvidenceRefInvalid, ReportErrorCode::RenderFailed,
             ReportErrorCode::DataInvalid,       ReportErrorCode::DiskFull,
             ReportErrorCode::ReadOnlyStore,     ReportErrorCode::BundleIncomplete,
             ReportErrorCode::TemplateVersion,   ReportErrorCode::Usage,
         }) {
        EXPECT_FALSE(suggestedDiagCode(code).has_value())
            << "码 " << token(code) << " 不应有 RPT 建议码";
    }
}

// =====================================================================
// acceptance 1：ReportId 往返／句法／保留值（§4.1＋core §4.1 U-1 同约定）
// =====================================================================

/// 判定字符串是否为恰 32 个小写 hex 字符（句法体的测试侧独立实现）。
bool is32LowerHex(std::string_view s)
{
    if (s.size() != 32) { return false; }
    for (const char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { return false; }
    }
    return true;
}

/**
 * 验证 acceptance 1：ReportId 规范文本 rpt-<32hex> 往返（generate/format/
 * parse——§4.1"句法同 core Id128 约定"）。
 *
 * 边界：generate 非零且两次生成互异（128 位空间——碰撞概率可忽略）；
 * parse(format(x))==x 双向往返；try 轨与抛出轨同判据。
 */
TEST(ReportingIdentity, ReportIdRoundtripGenerateParse_RPT02_ACC1)
{
    // 生成面：规范文本形态（前缀＋32 小写 hex）＋非零有效。
    const ReportId a = ReportId::generate();
    ASSERT_TRUE(a.isValid()) << "generate 必须非零（保留值纪律）";
    const std::string textA = a.toCanonical();
    EXPECT_EQ(textA.size(), 36u);
    EXPECT_EQ(textA.substr(0, 4), "rpt-");
    EXPECT_TRUE(is32LowerHex(textA.substr(4))) << "十六进制体须 32 小写 hex: " << textA;

    // 往返面：parse(format(x))==x（字节等值）——acceptance 1 核心断言。
    const ReportId parsed = ReportId::fromCanonical(textA);
    EXPECT_EQ(parsed, a);
    EXPECT_EQ(parsed.toCanonical(), textA);
    const auto tryParsed = ReportId::tryFromCanonical(textA);
    ASSERT_TRUE(tryParsed.has_value());
    EXPECT_EQ(*tryParsed, a);

    // 唯一性面：两次生成互异（新对象新身份——§4.1"同数据源重建＝新 ReportId"
    // 的分配面前提）。
    const ReportId b = ReportId::generate();
    EXPECT_TRUE(b.isValid());
    EXPECT_NE(b, a);
}

/**
 * 验证 acceptance 1：ReportId 解析严格性——tag 隔离/长度/字符集/空白/
 * 前后缀违约全拒绝（"句法同 core Id128 约定"的严格半区）。
 *
 * try 轨返回 nullopt；抛出轨抛 ReportError(DataInvalid) 且 what() 带
 * "reporting/" 稳定前缀（§1.4 try* 双轨＋§3.5 前缀口径）。
 */
TEST(ReportingIdentity, ReportIdParseStrictRejections_RPT02_ACC1)
{
    const std::string good = ReportId::generate().toCanonical();

    // 反例族：tag 不符（他类身份串/大写 tag）、长度、大写体、非 hex、
    // 空白、前后缀、空串——全部拒绝（每类一个代表样本）。
    // 用 std::string 数组承接拼接项（string_view 指向临时 string 会悬垂
    // ——临时量在声明语句结束即析构，必须持有所有权的字符串容器）。
    const std::string kBad[] = {
        "obj-" + good.substr(4),                        // 他类 tag（core ObjectId）
        "cid-" + good.substr(4),                        // 内容身份串——tag 隔离
        "cid-0000000000000000000000000000000000000000000000000000000000000000", // 64hex（cid 全形）
        "RPT-" + good.substr(4),                        // tag 大写
        good.substr(0, 35),                             // 35 长（缺 1 hex）
        good + "0",                                     // 37 长（多 1 hex）
        "rpt-" + std::string(32, 'A'),                  // 体大写
        "rpt-" + std::string(16, 'g') + std::string(16, '0'), // 'g' 越界
        " rpt-" + good.substr(4),                       // 前导空白
        good + " ",                                     // 尾随空白
        "xrpt-" + good.substr(4),                       // 前缀污染
        "",                                             // 空串
    };
    for (const auto bad : kBad) {
        EXPECT_FALSE(ReportId::tryFromCanonical(bad).has_value())
            << "应拒绝: \"" << bad << "\"";
        // 抛出轨：DataInvalid＋稳定前缀（与 try 轨同判据——两轨不分歧）。
        try {
            ReportId::fromCanonical(bad);
            FAIL() << "fromCanonical 应抛: \"" << bad << "\"";
        } catch (const ReportError& e) {
            EXPECT_EQ(e.code(), ReportErrorCode::DataInvalid);
            EXPECT_EQ(std::string_view{e.what()}.substr(0, 10), "reporting/");
        } catch (...) {
            FAIL() << "应抛 ReportError";
        }
    }
}

/**
 * 验证 acceptance 1：保留值纪律——默认构造＝全零＝"空/未设置"（isValid
 * 恒 false）；全零规范文本句法合法但值无效；generate 恒有效。
 */
TEST(ReportingIdentity, ReportIdReservedZeroValue_RPT02_ACC1)
{
    // 默认构造＝保留值（§4.1 U-1——全零字节＝空/未设置）。
    const ReportId zero{};
    EXPECT_FALSE(zero.isValid());
    // 全零文本：句法合法（32 个 '0' 都是合法 hex）但 isValid 恒 false——
    // 句法面与值面分离（格式化不越权代替合法性判定）。
    const std::string zeroText = "rpt-" + std::string(32, '0');
    const auto parsedZero = ReportId::tryFromCanonical(zeroText);
    ASSERT_TRUE(parsedZero.has_value()) << "全零文本句法应合法";
    EXPECT_FALSE(parsedZero->isValid());
    EXPECT_EQ(parsedZero->toCanonical(), zeroText) << "保留值往返仍成立";
    // 保留值不参与有效身份比较语义：与生成值必然互异。
    EXPECT_NE(ReportId::generate(), zero);
}

/**
 * 验证 acceptance 1：ReportLevel token 句法——恰接受 "level-b"/"level-c"
 * （§4.2 字段表原文），大小写变体/近似串全拒绝；try 轨不抛、抛出轨
 * DataInvalid。
 */
TEST(ReportingIdentity, ReportLevelTokenSyntax_RPT02_ACC1)
{
    using sdurws::ird::reporting::levelFromToken;
    using sdurws::ird::reporting::tryLevelFromToken;

    // 正例：两值 token 逐字一致＋往返。
    EXPECT_EQ(token(ReportLevel::B), "level-b");
    EXPECT_EQ(token(ReportLevel::C), "level-c");
    EXPECT_EQ(tryLevelFromToken("level-b"), ReportLevel::B);
    EXPECT_EQ(tryLevelFromToken("level-c"), ReportLevel::C);

    // 反例族：大小写、截断、加空格、其他级别、空串——规范文本唯一形态
    // （非规范输入在解析边界拒绝而非归一化）。
    for (const auto bad : {"Level-b", "level-B", "LEVEL-B", "B", "b", "level",
                           "level-", "level-bb", "level-a", "level-d",
                           "level-b ", " level-b", "level-b\t", ""}) {
        EXPECT_FALSE(tryLevelFromToken(bad).has_value()) << "应拒绝: \"" << bad << "\"";
        EXPECT_THROW(levelFromToken(bad), ReportError) << "应抛: \"" << bad << "\"";
    }
    // 抛出轨码面：DataInvalid（报告字段非法——级别是持久化报告字段）。
    try {
        levelFromToken("level-x");
        FAIL() << "不可达";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::DataInvalid);
        EXPECT_EQ(std::string_view{e.what()}.substr(0, 10), "reporting/");
    }
}

// =====================================================================
// acceptance 1：身份纪律类型边界（单元内半区——强类型/值面结构钉住；
// core 类型互转与内容身份行为的跨单元半区在 contract_test）
// =====================================================================

/**
 * 验证 acceptance 1：ReportId 的强类型结构面——纯 16 字节 tag（不携带
 * 任何内容信息字段），无从字符串/字节数组的隐式构造入口。
 *
 * "ReportId 不承载内容信息"的结构面判据：类型只含 16 字节数组（sizeof
 * 钉住——多出的任何内容载荷字段都会破坏该断言）；显式入口只有
 * generate/fromCanonical/tryFromCanonical（默认聚合初始化＝保留值）。
 */
TEST(ReportingIdentity, ReportIdTypeBoundary_NoContentPayload_RPT02_ACC1)
{
    // 结构面：纯 tag 类型——16 字节且标准布局（跨边界值语义的前提）。
    static_assert(sizeof(ReportId) == 16, "ReportId 必须是纯 128 位 tag 类型");
    static_assert(std::is_standard_layout<ReportId>::value,
                  "ReportId 须为标准布局值类型");
    // 入口面：无非门控的宽构造（杜绝从任意字节/文本静默造身份）。
    static_assert(!std::is_constructible<ReportId, std::string_view>::value,
                  "ReportId 禁止从文本隐式构造——解析必须走 fromCanonical");
    static_assert(!std::is_constructible<ReportId, const char*>::value,
                  "ReportId 禁止从字面量隐式构造");
    SUCCEED() << "ReportId 强类型边界（编译期断言全过）";
}

}  // namespace
