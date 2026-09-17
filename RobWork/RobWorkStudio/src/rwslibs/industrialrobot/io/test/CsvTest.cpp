/**
 * @file   CsvTest.cpp
 * @brief  CSV 读写器用例组（IoCsv）——roundtrip 数据层/文件层一致
 *         （IO-V01/V02）、无标识零改写（IO-V03）、BOM/编码/行尾/引号
 *         （IO-V04）、列异常处置（IO-V05）、方言标识行 v1 封闭语法
 *         （P-IO-5）、预算经读取器通道生效、数据-only 源扫描与写侧防护。
 *
 * 设计依据：
 *   - units/io.md §11.2 IO-V01~V05 行（本文件用例一一对应）、§5.1~§5.6
 *     （被测语义）、§5.8（部分成功/原文保留）、§9.3/§9.4（接口契约）、
 *     §9.12（错误码面）
 *   - 需求 NFR-SEC-03（方言标识＋可逆转义防公式注入；R5 roundtrip）、
 *     REQ-05（解析与逐行错误支撑）、AT-02（正确行保留、错误定位到列
 *     与原文）
 *   - 任务契约 tasks/foundation/IO-T03.json acceptance 1~5
 *
 * 断言纪律（AGENTS.md §2.7）：每个用例中文注明验证的需求/验收条目；
 * 测试以真实文件为替身载体（TempDir 隔离），失败如实失败不伪造。
 */

#include <sdurws/ird/io/Csv.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/io/Budget.hpp>

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::io::BudgetDimension;
using sdurws::ird::io::BudgetScopeId;
using sdurws::ird::io::BudgetSpec;
using sdurws::ird::io::CsvArityPolicy;
using sdurws::ird::io::CsvBlankPolicy;
using sdurws::ird::io::CsvCell;
using sdurws::ird::io::CsvDialect;
using sdurws::ird::io::CsvEol;
using sdurws::ird::io::CsvReadOptions;
using sdurws::ird::io::CsvRowError;
using sdurws::ird::io::CsvWriteOptions;
using sdurws::ird::io::IBudgetGuardPtr;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::IoResult;
using sdurws::ird::io::RawTable;
using sdurws::ird::io::escapeCsvText;
using sdurws::ird::io::makeBudgetGuard;
using sdurws::ird::io::makeCsvReader;
using sdurws::ird::io::makeCsvWriter;
using sdurws::ird::io::renderDialectMarker;
using sdurws::ird::io::unescapeCsvText;

namespace {

// 文本行类型（roundtrip 用例的表载体——每行若干字符串单元格）。
using StringRow = std::vector<std::string>;

// =====================================================================
// 测试助手
// =====================================================================

/// 写二进制文件（测试替身载体——真实文件，不经内存特判路径）。
void writeFileBytes(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_FALSE(out.fail()) << "测试文件写入失败：" << path.string();
}

/// 读全文件字节（文件层字节一致断言用）。
std::string readFileBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        ADD_FAILURE() << "无法打开被测文件：" << path.string();
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/**
 * UTF-8 文本 → UTF-16LE 字节（含代理对）——UTF-16 用例的构码器（测试
 * 替身自持，不复刻被测实现的解码路径：此处由码点正向编码，被测侧是
 * 逆向解码，二者在用例内互为校验）。
 */
std::string utf8ToUtf16Le(const std::string& utf8, bool bigEndian)
{
    // 先按 UTF-8 解出码点（测试数据由本文件字面量构造，恒为合法 UTF-8）。
    std::vector<std::uint32_t> cps;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char b = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp = 0;
        std::size_t len = 1;
        if (b < 0x80) {
            cp = b;
        } else if ((b >> 5) == 0x6) {
            cp = b & 0x1F;
            len = 2;
        } else if ((b >> 4) == 0xE) {
            cp = b & 0x0F;
            len = 3;
        } else {
            cp = b & 0x07;
            len = 4;
        }
        for (std::size_t k = 1; k < len; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        cps.push_back(cp);
        i += len;
    }
    // 码点 → UTF-16 码元（增补平面拆代理对）→ 按端序出字节。
    std::string out;
    const auto emitUnit = [&](std::uint16_t u) {
        if (bigEndian) {
            out.push_back(static_cast<char>(u >> 8));
            out.push_back(static_cast<char>(u & 0xFF));
        } else {
            out.push_back(static_cast<char>(u & 0xFF));
            out.push_back(static_cast<char>(u >> 8));
        }
    };
    for (const std::uint32_t cp : cps) {
        if (cp >= 0x10000) {
            const std::uint32_t v = cp - 0x10000;
            emitUnit(static_cast<std::uint16_t>(0xD800 + (v >> 10)));
            emitUnit(static_cast<std::uint16_t>(0xDC00 + (v & 0x3FF)));
        } else {
            emitUnit(static_cast<std::uint16_t>(cp));
        }
    }
    return out;
}

/// 文件 SHA-256 十六进制（小写）——core ContentDigester（D-05：SHA-256
/// 唯一摘要算法；io 测试同样只消费 core 设施，不自建第二实现）。
std::string sha256Hex(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        ADD_FAILURE() << "无法打开被摘要文件：" << path.string();
        return {};
    }
    sdurws::ird::core::ContentDigester digester;
    char buf[8192];
    while (in.read(buf, sizeof(buf)), in.gcount() > 0) {
        digester.update(buf, static_cast<std::size_t>(in.gcount()));
    }
    const sdurws::ird::core::Digest256 digest = digester.finalize();
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const std::uint8_t b : digest) {
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 0x0F]);
    }
    return hex;
}

/// 从 IoError params 取键值（比较型三要素/定位参数断言用）。
std::string paramOf(const sdurws::ird::io::IoError& e, const char* key)
{
    for (const auto& kv : e.params) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return {};
}

/// 用例自持临时目录（BudgetTest/SafePathTest 同款形态）。
class IoCsvTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        // 目录唯一化：用例名＋随机熵（不依赖 windows.h 的进程 id——测试
        // 头保持零 SDK 依赖；SetUp 先清残留再建，重复运行可重入）。
        m_dir = fs::temp_directory_path() / "ird_wp11_t03_csv"
                / (std::string(info->name()) + "_"
                   + std::to_string(std::random_device{}()));
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec) << "临时目录创建失败";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        EXPECT_FALSE(ec) << "临时目录清理失败";
    }

    fs::path m_dir;   ///< 用例临时根
};

} // namespace

// =====================================================================
// P-IO-5：方言标识行 v1 语法（acceptance 5——语法按 §5.1 原文实现，
// 渲染与卡面示例逐字节相同；未知语法拒绝不猜测）
// =====================================================================

/**
 * 渲染钉住：rwDefault 方言的标识行与 units/io.md §5.1 示例逐字节相同
 * （P-IO-5 处置——v1 语法已给全，冻结前不私改）；非缺省键按 §5.1 词法
 * 渲染（tab→"tab"、eol=LF）。这是 acceptance 5"方言标识行 v1 语法按
 * §5.1 已给全文实现"的直接证据。
 */
TEST_F(IoCsvTest, RenderDialectMarkerMatchesCard51Verbatim)
{
    // 缺省方言：与 §5.1 示例行逐字节一致（含键序 delimiter→quote→eol→
    // encoding——canonical 字节序，§5.5 文件层字节一致的前提）。
    EXPECT_EQ(renderDialectMarker(CsvDialect::rwDefault()),
              "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8");
    // tab 分隔符按 §5.1 词法写作 "tab"；eol 声明值 LF 渲染 "LF"。
    CsvDialect semi;
    semi.delimiter = ';';
    semi.eol = CsvEol::Lf;
    EXPECT_EQ(renderDialectMarker(semi),
              "#rwcsv1 delimiter=; quote=\" eol=LF encoding=utf-8");
    CsvDialect tabbed;
    tabbed.delimiter = '\t';
    EXPECT_EQ(renderDialectMarker(tabbed),
              "#rwcsv1 delimiter=tab quote=\" eol=CRLF encoding=utf-8");

    // 内存目标写出：文件以标识行＋CRLF 开头（本软件导出恒带标识、恒无
    // BOM——§5.1"与 BOM"行；无 BOM 由"从不写 BOM 字节"结构性成立）。
    std::string buffer;
    auto writer = makeCsvWriter();
    CsvWriteOptions opts;                       // 缺省＝恒带标识行
    ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::memory(buffer), opts));
    ASSERT_TRUE(writer->writeRow({CsvCell::fromText("a"), CsvCell::fromText("b")}));
    ASSERT_TRUE(writer->finish());
    EXPECT_EQ(buffer,
              "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\na,b\r\n");
}

/**
 * 自反性（acceptance 2/§5.3 自反性行）：对任意原文 s，
 * unescapeCsvText(escapeCsvText(s)) == s 逐字符成立——样例集覆盖 = + -
 * @ ' 五类前缀、原文自带 '、空串、含引号/分隔符/换行/中文。escape/
 * unescape 是全产品唯一转义/还原实现点（SA-12/NFR-SEC-03 M-15）。
 */
TEST_F(IoCsvTest, EscapeUnescapeReflexiveOverPrefixSampleSet)
{
    const std::vector<std::string> samples{
        "=SUM(A1)",  "+cmd",   "-3.5",     "@path",  "'quoted'", "'abc",   "'=x",
        "'-3.5",     "''",     "'",        "",       "plain",    "a=b",    "a,b",
        "say \"hi\"", "multi\nline", "中文=值", "=引号\"内\n换行", "@'=-", "'",
    };
    for (const std::string& s : samples) {
        const std::string escaped = escapeCsvText(s);
        // 转义只增不改：文件层文本要么等于原文、要么恰多一个前导 '。
        EXPECT_TRUE(escaped == s || (escaped.size() == s.size() + 1 && escaped[0] == '\''))
            << "escape 改写了原文：" << s;
        // 自反性（逐字符）：decode(encode(s)) == s。
        EXPECT_EQ(unescapeCsvText(escaped), s) << "自反性失败：" << s;
    }
    // 前缀规则精确性：五类前缀字符各自恰好加一层；非前缀开头不加。
    EXPECT_EQ(escapeCsvText("=x"), "'=x");
    EXPECT_EQ(escapeCsvText("'-3.5"), "''-3.5");
    EXPECT_EQ(escapeCsvText("x=y"), "x=y");
    EXPECT_EQ(unescapeCsvText("'abc"), "abc");
    EXPECT_EQ(unescapeCsvText("abc"), "abc");       // 无前缀原样（恒等路径）
}

// =====================================================================
// IO-V01：IoCsv/RoundtripBasic——引号/分隔符/换行/中文/空串字段
// write→read 逐字段比对＋再 write 文件 SHA-256 二次比较
// =====================================================================

/**
 * 数据层逐字符一致（§5.5 概念一）＋文件层字节一致（概念二）：本软件
 * 导出（带标识）→ 导入 → 每单元格与导出前结构化值逐字符相同；同一数
 * 据两次导出的文件字节相同（SHA-256 相等＋直接字节比对）。
 */
TEST_F(IoCsvTest, RoundtripBasicQuotesDelimsNewlineChineseEmpty)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V01 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-03"},
                  std::vector<std::string>{"AT-02", "AT-22"});
    // 导出数据：引号/分隔符/换行/中文/空串全覆盖（NFR-SEC-03 R5 样例域）。
    const std::vector<StringRow> table{
        {"名称", "公式", "备注"},
        {"he said \"hi\"", "=SUM(A1)", ""},
        {"a,b;c", "line1\nline2", "中文单元格"},
        {"", "'already", "-3.5"},
        {"尾随空格 ", "0.1", "3"},
    };
    const fs::path file1 = m_dir / "roundtrip1.csv";
    {
        auto writer = makeCsvWriter();
        CsvWriteOptions opts;                   // 缺省方言（CRLF/逗号）
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::file(file1), opts));
        // 首行＝表头（文本字段）；其余按文本单元格写出（roundtrip 的
        // io 层载体是文本原文——数值语义归业务单元，§5.5 语义一致行）。
        ASSERT_TRUE(writer->writeHeader(table[0]));
        for (std::size_t r = 1; r < table.size(); ++r) {
            std::vector<CsvCell> cells;
            cells.reserve(table[r].size());
            for (const auto& t : table[r]) {
                cells.push_back(CsvCell::fromText(t));
            }
            ASSERT_TRUE(writer->writeRow(cells));
        }
        ASSERT_TRUE(writer->finish());
    }

    // 导入：带标识文件（标识行方言）→ 逐字段逐字符比对。
    auto reader = makeCsvReader();
    CsvReadOptions ropts;
    ropts.headerRow = 2;                        // 物理行号：标识行占行 1，表头在行 2
    std::vector<std::vector<std::string>> got;
    const IoResult<RawTable> res = reader->read(
        file1, ropts,
        [&](std::uint64_t, sdurws::ird::io::CsvRowView&& view) {
            std::vector<std::string> row;
            row.reserve(view.fieldCount());
            for (std::size_t i = 0; i < view.fieldCount(); ++i) {
                row.emplace_back(view.field(i));
            }
            got.push_back(std::move(row));
            return true;
        },
        nullptr, nullptr);
    ASSERT_TRUE(res) << res.error.detail;
    EXPECT_TRUE(res.value.report.marked);       // 带标识文件
    ASSERT_EQ(got.size(), table.size() - 1) << "交付数据行数不符";
    for (std::size_t r = 1; r < table.size(); ++r) {
        ASSERT_EQ(got[r - 1].size(), table[r].size()) << "行 " << r << " 列数不符";
        for (std::size_t c = 0; c < table[r].size(); ++c) {
            // 数据层逐字符一致（含空串/换行/引号/中文——R5 承诺域）。
            EXPECT_EQ(got[r - 1][c], table[r][c])
                << "行 " << r << " 列 " << c << " 逐字符比对失败";
        }
    }
    EXPECT_EQ(res.value.report.dataRows, table.size() - 1);
    EXPECT_TRUE(res.value.rowErrors.empty()) << "正常文件不得有行错误";

    // 再导出：从导入原文重写 → 文件层字节一致（SHA-256 二次比较＋直接
    // 字节比对——AT-22 多格式一致的确定性基础）。
    const fs::path file2 = m_dir / "roundtrip2.csv";
    {
        auto writer = makeCsvWriter();
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::file(file2), {}));
        ASSERT_TRUE(writer->writeHeader(table[0]));
        for (const auto& row : got) {
            std::vector<CsvCell> cells;
            cells.reserve(row.size());
            for (const auto& t : row) {
                cells.push_back(CsvCell::fromText(t));
            }
            ASSERT_TRUE(writer->writeRow(cells));
        }
        ASSERT_TRUE(writer->finish());
    }
    EXPECT_EQ(sha256Hex(file1), sha256Hex(file2)) << "同数据两次导出摘要不同";
    EXPECT_EQ(readFileBytes(file1), readFileBytes(file2)) << "同数据两次导出字节不同";
}

// =====================================================================
// IO-V02：IoCsv/RoundtripPrefixed——自带前缀（= + - @ '）样例集
// roundtrip 逐字符一致、decode(encode(s))==s 自反、RawTable 无转义残留
// =====================================================================

/**
 * 前缀样例集 roundtrip（NFR-SEC-03 R5/AT-02）：'=x→x、''q→'q、
 * -3.5→'-3.5→-3.5（文件层带前缀、结构化层还原）；转义形式只存在于文
 * 件层（断言文件字节含 '= 形态），RawTable/回调交付的单元格＝剥离后
 * 原文（转义形式绝不进入结构化数据层——§5.3 结构化层行）。
 */
TEST_F(IoCsvTest, RoundtripPrefixedEscapeOnlyAtFileLayer)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V02 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-03"},
                  std::vector<std::string>{"AT-02"});
    // 样例集：前缀五类＋原文自带 '＋空串＋组合。每行配第二列行号哨兵——
    // 纯空串单列行在文件层与空行同形（由 §5.4 空行策略管辖，属格式固有
    // 歧义），哨兵列使全样例成为可交付数据行（含空串字段的 roundtrip
    // 由 V01 的多列表并行覆盖）。
    const std::vector<std::string> originals{
        "=SUM(A1)", "+cmd", "-3.5", "@path", "'quoted'", "'abc", "'=x", "'-3.5",
        "", "plain", "a,b", "中文=值", "'",
    };
    const fs::path file1 = m_dir / "prefixed1.csv";
    {
        auto writer = makeCsvWriter();
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::file(file1), {}));
        for (std::size_t i = 0; i < originals.size(); ++i) {
            ASSERT_TRUE(writer->writeRow({CsvCell::fromText(originals[i]),
                                          CsvCell::fromText("row-" + std::to_string(i))}));
        }
        ASSERT_TRUE(writer->finish());
    }

    // 文件层证据：转义形态确实存在于文件字节（'=SUM(A1)、'-3.5）——
    // 防护发生在导出物上（表格软件按文本处理），不在用户数据上。
    const std::string raw = readFileBytes(file1);
    EXPECT_NE(raw.find("'=SUM(A1)"), std::string::npos) << "文件层缺 '= 转义形态";
    EXPECT_NE(raw.find("'-3.5"), std::string::npos) << "文件层缺 '- 前缀形态";
    EXPECT_NE(raw.find("''quoted'"), std::string::npos) << "原文自带 ' 应翻倍为 ''";

    // 导入：剥离恰一个前缀 → 与原文逐字符一致；结构化层无转义残留。
    auto reader = makeCsvReader();
    CsvReadOptions ropts;                       // 无表头（首行即数据）
    std::vector<std::string> got;
    const IoResult<RawTable> res = reader->read(
        file1, ropts,
        [&](std::uint64_t, sdurws::ird::io::CsvRowView&& view) {
            if (view.fieldCount() < 1) {
                ADD_FAILURE() << "样例行出现零字段行";
                return false;
            }
            // string_view 仅回调内有效——立即拷贝为值（§9.3 所有权行）。
            got.emplace_back(view.field(0));
            return true;
        },
        nullptr, nullptr);
    ASSERT_TRUE(res) << res.error.detail;
    ASSERT_EQ(got.size(), originals.size());
    for (std::size_t i = 0; i < originals.size(); ++i) {
        EXPECT_EQ(got[i], originals[i]) << "前缀样例 " << i << " 还原失败";
        // 转义残留负断言：原文不以 ' 开头的样例，还原后也不以 ' 开头
        // （带标识读取把文件层前缀恰好剥净——RawTable 无转义形式残留）。
        if (originals[i].empty() || originals[i][0] != '\'') {
            ASSERT_TRUE(got[i].empty() || got[i][0] != '\'')
                << "结构化层残留转义形式：" << got[i];
        }
    }
    // decode(encode(s))==s 全样例自反（§5.3 自反性行——acceptance 2）。
    for (const auto& s : originals) {
        EXPECT_EQ(unescapeCsvText(escapeCsvText(s)), s);
    }
}

// =====================================================================
// IO-V03：IoCsv/UnmarkedNoRewrite——无标识外部 CSV 全字段原样读入
// =====================================================================

/**
 * 无标识文件（§5.3 导入行·无标识/§2.3 非目标 9）：'=x 保持 '=x、=SUM
 * 保持 =SUM——不执行任何前缀剥离或字段改写；report.marked==false 钉住
 * 判定路径。读取侧永不"清除"疑似公式字符（§5.3 完整口径）。
 */
TEST_F(IoCsvTest, UnmarkedNoRewriteKeepsAllFieldsVerbatim)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V03 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-03"},
                  std::vector<std::string>{});
    const fs::path file = m_dir / "external.csv";
    writeFileBytes(file,
                   "name,val\n"        // 表头（无标识）
                   "'=x,=SUM(A1)\n"    // ' 与 = 开头——原样
                   "'-3.5,-3.5\n"      // 同上
                   "'a,@cmd\n"         // ' 与 @ 开头
                   "plain,+1\n");      // 无前缀对照行
    auto reader = makeCsvReader();
    CsvReadOptions ropts;
    ropts.headerRow = 1;                        // 无标识文件：物理行 1 即表头
    std::vector<std::vector<std::string>> got;
    const IoResult<RawTable> res = reader->read(
        file, ropts,
        [&](std::uint64_t, sdurws::ird::io::CsvRowView&& view) {
            std::vector<std::string> row;
            for (std::size_t i = 0; i < view.fieldCount(); ++i) {
                row.emplace_back(view.field(i));
            }
            got.push_back(std::move(row));
            return true;
        },
        nullptr, nullptr);
    ASSERT_TRUE(res) << res.error.detail;
    EXPECT_FALSE(res.value.report.marked) << "无标识文件不得判为带标识";
    ASSERT_EQ(got.size(), 4u);
    // 全字段原文比对——一字不改（V03 观测点：逐字段原文比对）。
    EXPECT_EQ(got[0][0], "'=x");
    EXPECT_EQ(got[0][1], "=SUM(A1)");
    EXPECT_EQ(got[1][0], "'-3.5");
    EXPECT_EQ(got[1][1], "-3.5");
    EXPECT_EQ(got[2][0], "'a");
    EXPECT_EQ(got[2][1], "@cmd");
    EXPECT_EQ(got[3][0], "plain");
    EXPECT_EQ(got[3][1], "+1");
}

// =====================================================================
// IO-V04：IoCsv/BomQuotEol——BOM 剥离/UTF-16 识别/CRLF-LF-CR 容错/
// RFC4180 引号嵌套；非 UTF-8 无 BOM 稳定拒绝
// =====================================================================

/**
 * 编码识别（§5.2 编码行/E3）：UTF-8 BOM 剥离（带/不带标识行）；UTF-16
 * LE/BE 带 BOM 解码（标识行按解码后判定——UTF-16 文件携带合法 v1 标识
 * 时按带标识处理，剥离语义生效）。
 */
TEST_F(IoCsvTest, BomStrippingAndUtf16Decoding)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V04 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-03"},
                  std::vector<std::string>{"AT-02"});
    auto reader = makeCsvReader();

    // ① UTF-8 BOM＋无标识：BOM 剥离后正常解析（首字段不含 BOM 字节）。
    const fs::path utf8Bom = m_dir / "utf8bom.csv";
    writeFileBytes(utf8Bom, std::string("\xEF\xBB\xBF") + "a,b\n1,2\n");
    {
        std::vector<std::vector<std::string>> got;
        CsvReadOptions o;
        const IoResult<RawTable> r = reader->read(
            utf8Bom, o,
            [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
                std::vector<std::string> row;
                for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                    row.emplace_back(v.field(i));
                }
                got.push_back(std::move(row));
                return true;
            },
            nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        ASSERT_EQ(got.size(), 2u);
        EXPECT_EQ(got[0][0], "a") << "UTF-8 BOM 未剥离";
        EXPECT_EQ(got[1][1], "2");
    }

    // ② UTF-8 BOM＋标识行：BOM 剥离在标识判定之前（§5.1"与 BOM"行——
    // 读取带标识文件时容忍并剥离 UTF-8 BOM），判定为带标识。
    const fs::path utf8BomMarked = m_dir / "utf8bom_marked.csv";
    writeFileBytes(utf8BomMarked,
                   std::string("\xEF\xBB\xBF") + "#rwcsv1 delimiter=;\r\na;b\r\n'=x;y\r\n");
    {
        std::vector<std::vector<std::string>> got;
        CsvReadOptions o;
        const IoResult<RawTable> r = reader->read(
            utf8BomMarked, o,
            [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
                std::vector<std::string> row;
                for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                    row.emplace_back(v.field(i));
                }
                got.push_back(std::move(row));
                return true;
            },
            nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        EXPECT_TRUE(r.value.report.marked);
        ASSERT_EQ(got.size(), 2u);
        EXPECT_EQ(got[0][0], "a");
        // 文件层 '=x 剥离恰一个前导 ' → 原文 "=x"（§5.3 还原行）。
        EXPECT_EQ(got[1][0], "=x") << "带标识文件的转义剥离未生效";
    }

    // ③ UTF-16LE 带 BOM（无标识）：解码后解析（E3——接受）。
    {
        const std::string text = "名称,值\n数据,3.14\n";
        std::string bytes("\xFF\xFE");          // LE BOM
        bytes += utf8ToUtf16Le(text, /*bigEndian=*/false);
        const fs::path p = m_dir / "utf16le.csv";
        writeFileBytes(p, bytes);
        std::vector<std::vector<std::string>> got;
        CsvReadOptions o;
        o.headerRow = 1;
        const IoResult<RawTable> r = reader->read(
            p, o,
            [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
                std::vector<std::string> row;
                for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                    row.emplace_back(v.field(i));
                }
                got.push_back(std::move(row));
                return true;
            },
            nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        ASSERT_EQ(got.size(), 1u);
        EXPECT_EQ(got[0][0], "数据") << "UTF-16LE 解码失败";
        EXPECT_EQ(got[0][1], "3.14");
    }

    // ④ UTF-16BE 带 BOM：同上（字节序反转）。
    {
        const std::string text = "a,b\n1,2\n";
        std::string bytes("\xFE\xFF");          // BE BOM
        for (char c : text) {
            bytes.push_back('\0');
            bytes.push_back(c);
        }
        const fs::path p = m_dir / "utf16be.csv";
        writeFileBytes(p, bytes);
        std::vector<std::vector<std::string>> got;
        CsvReadOptions o;
        const IoResult<RawTable> r = reader->read(
            p, o,
            [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
                std::vector<std::string> row;
                for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                    row.emplace_back(v.field(i));
                }
                got.push_back(std::move(row));
                return true;
            },
            nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        ASSERT_EQ(got.size(), 2u);
        EXPECT_EQ(got[1][1], "2");
    }

    // ⑤ UTF-16LE＋合法 v1 标识行：标识按解码后判定（E3 括注——"通常非
    // 标识语法→按无标识处理"的对称面：解码后恰为标识语法则按带标识）。
    {
        const std::string text = "#rwcsv1 delimiter=;\r\n'=x;y\r\n";
        std::string bytes("\xFF\xFE");
        bytes += utf8ToUtf16Le(text, /*bigEndian=*/false);
        const fs::path p = m_dir / "utf16le_marked.csv";
        writeFileBytes(p, bytes);
        std::vector<std::vector<std::string>> got;
        CsvReadOptions o;
        const IoResult<RawTable> r = reader->read(
            p, o,
            [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
                std::vector<std::string> row;
                for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                    row.emplace_back(v.field(i));
                }
                got.push_back(std::move(row));
                return true;
            },
            nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        EXPECT_TRUE(r.value.report.marked);
        ASSERT_EQ(got.size(), 1u);
        // 文件层 '=x 剥离恰一个前导 ' → 原文 "=x"（§5.3 还原行）。
        EXPECT_EQ(got[0][0], "=x") << "UTF-16 带标识文件的剥离语义未生效";
    }
}

/**
 * 行尾容错与 RFC4180 引号语义（§5.2 行尧行/引号行、E12）：CRLF/LF/CR
 * 三形态混布同文件全部接受；引号字段内换行属字段内容且逐字节保留；
 * "" 字面引号；引号字段含分隔符。非 UTF-8 无 BOM 稳定拒绝
 * （IO-FORMAT-CSV-ENCODING——不猜测转码，§2.3 非目标 10）另见下一用例。
 */
TEST_F(IoCsvTest, EolToleranceAndQuotedNewlineNesting)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V04 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-03"},
                  std::vector<std::string>{"AT-02"});
    auto reader = makeCsvReader();

    // ① 行尾三形态混布＋引号嵌套/引号内换行与分隔符（无标识文件）。
    const fs::path mixed = m_dir / "mixed_eol.csv";
    writeFileBytes(mixed,
                   std::string("a,b\r\n")       // CRLF
                       + "\"x,1\",y\n"          // LF＋引号字段含分隔符
                       + "z,\"l1\r\nl2\"\r\n"   // CRLF＋引号字段含 CRLF
                       + "w,\"say \"\"hi\"\"\"\r" // 孤立 CR＋"" 字面引号
                       + "end,of\n");           // LF 收尾
    std::vector<std::vector<std::string>> got;
    const IoResult<RawTable> r = reader->read(
        mixed, CsvReadOptions{},
        [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
            std::vector<std::string> row;
            for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                row.emplace_back(v.field(i));
            }
            got.push_back(std::move(row));
            return true;
        },
        nullptr, nullptr);
    ASSERT_TRUE(r) << r.error.detail;
    ASSERT_EQ(got.size(), 5u) << "行尾容错失败（应 5 行）";
    EXPECT_EQ(got[0][0], "a");
    EXPECT_EQ(got[1][0], "x,1") << "引号内分隔符应为字段内容";
    EXPECT_EQ(got[2][1], "l1\r\nl2") << "引号内换行应逐字节保留（RFC4180/E12）";
    EXPECT_EQ(got[3][1], "say \"hi\"") << "空引号加倍（\"\"）字面引号语义失败";
    EXPECT_EQ(got[4][0], "end");

    // ② 带 CRLF 行尾的引号字段在带标识文件中同样逐字节保留（roundtrip
    // 的换行一致面——§5.5 数据层承诺含"含换行字段"）。
    const fs::path markedNl = m_dir / "marked_nl.csv";
    {
        auto writer = makeCsvWriter();
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::file(markedNl), {}));
        ASSERT_TRUE(writer->writeRow({CsvCell::fromText("l1\nl2"), CsvCell::fromText("ok")}));
        ASSERT_TRUE(writer->finish());
    }
    std::vector<std::string> nlCells;
    const IoResult<RawTable> r2 = reader->read(
        markedNl, CsvReadOptions{},
        [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
            nlCells.emplace_back(v.field(0));
            return true;
        },
        nullptr, nullptr);
    ASSERT_TRUE(r2) << r2.error.detail;
    ASSERT_EQ(nlCells.size(), 1u);
    EXPECT_EQ(nlCells[0], "l1\nl2") << "带标识文件引号内换行还原失败";
}

/**
 * 非 UTF-8 且无 BOM＝稳定拒绝（IO-FORMAT-CSV-ENCODING——§5.2 编码行②/
 * IO-D09；§2.3 非目标 10：不做编码猜测转码）。Latin-1 高位字节是典型
 * 非 UTF-8 形态（0xE9 需要两个续字节，跟随 ASCII 即非法）。
 */
TEST_F(IoCsvTest, NonUtf8WithoutBomStableReject)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V04 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-03"},
                  std::vector<std::string>{});
    auto reader = makeCsvReader();
    const fs::path latin1 = m_dir / "latin1.csv";
    writeFileBytes(latin1, std::string("name,val\r\ncaf\xE9,1\r\n")); // 0xE9＝Latin-1 é
    const IoResult<RawTable> r = reader->read(latin1, CsvReadOptions{}, nullptr, nullptr, nullptr);
    ASSERT_FALSE(r) << "非 UTF-8 无 BOM 必须稳定拒绝";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatCsvEncoding) << r.error.detail;

    // probe 同码拒绝（§9.3 probe 探测行——BOM/编码探测属于其职责）。
    const IoResult<CsvDialect> pr = reader->probe(latin1, nullptr, nullptr);
    ASSERT_FALSE(pr);
    EXPECT_EQ(pr.error.code, IoErrorCode::FormatCsvEncoding);
}

// =====================================================================
// IO-V05：IoCsv/ColumnAnomalies——重复列名/缺列/多列/空行（默认策略
// 拒绝且定位；显式策略按声明处置并记录；RowError 内容与顺序可断言）
// =====================================================================

/**
 * 重复列名（§5.4 DUPCOL 行）：折叠大小写后同名即拒绝（列映射歧义）；
 * 诊断定位列号与名字（AT-02 定位口径）。致命错：整读失败，无部分产物。
 */
TEST_F(IoCsvTest, DuplicateHeaderColumnsRejectedWithLocation)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V05 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"},
                  std::vector<std::string>{"AT-02"});
    auto reader = makeCsvReader();
    const fs::path dup = m_dir / "dupcol.csv";
    writeFileBytes(dup,
                   "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n"
                   "id,ID,x\r\n"                        // 大小写折叠后 id==ID
                   "1,2,3\r\n");
    CsvReadOptions o;
    o.headerRow = 2;                                    // 标识行占行 1
    const IoResult<RawTable> r = reader->read(dup, o, nullptr, nullptr, nullptr);
    ASSERT_FALSE(r) << "重复列名必须拒绝";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatCsvDupCol) << r.error.detail;
    EXPECT_EQ(paramOf(r.error, "row"), "2") << "DUPCOL 应定位表头物理行号";
    EXPECT_EQ(paramOf(r.error, "column"), "2") << "DUPCOL 应定位重复列号";
    // 精确同名重复同样拒绝（对照）。
    const fs::path dup2 = m_dir / "dupcol_exact.csv";
    writeFileBytes(dup2,
                   "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n"
                   "a,a\r\n1,2\r\n");
    const IoResult<RawTable> r2 = reader->read(dup2, o, nullptr, nullptr, nullptr);
    ASSERT_FALSE(r2);
    EXPECT_EQ(r2.error.code, IoErrorCode::FormatCsvDupCol);
}

/**
 * 缺列/多列默认拒绝并定位（行号/期望/实际——§5.4），正确行保留
 * （AT-02）；显式 PadTrailing/TrimExcess 按声明处置并记入报告。
 */
TEST_F(IoCsvTest, ArityPoliciesDefaultRejectAndExplicitPadTrim)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V05 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"},
                  std::vector<std::string>{"AT-02"});
    auto reader = makeCsvReader();
    const fs::path anom = m_dir / "arity.csv";
    writeFileBytes(anom,
                   "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n"
                   "c1,c2,c3\r\n"       // 行 2：表头
                   "1,2\r\n"            // 行 3：缺列
                   "4,5,6,7\r\n"        // 行 4：多列
                   "8,9,10\r\n");       // 行 5：正确行
    CsvReadOptions o;
    o.headerRow = 2;

    // 默认 Reject：行错误按行号序（顺序可断言——acceptance 5）。
    {
        std::vector<std::vector<std::string>> got;
        const IoResult<RawTable> r = reader->read(
            anom, o,
            [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
                std::vector<std::string> row;
                for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                    row.emplace_back(v.field(i));
                }
                got.push_back(std::move(row));
                return true;
            },
            nullptr, nullptr);
        ASSERT_TRUE(r) << "行级失配＝部分成功而非读失败：" << r.error.detail;
        const RawTable& t = r.value;
        ASSERT_EQ(t.rowErrors.size(), 2u);
        // 错误一：行 3 缺列——定位首个缺失列（列 3）＋表头名＋期望/实际。
        EXPECT_EQ(t.rowErrors[0].rowNo, 3u);
        EXPECT_EQ(t.rowErrors[0].colNo, 3u);
        ASSERT_TRUE(t.rowErrors[0].fieldName.has_value());
        EXPECT_EQ(t.rowErrors[0].fieldName.value(), "c3");
        EXPECT_EQ(t.rowErrors[0].reason.code, IoErrorCode::FormatCsvArity);
        EXPECT_EQ(paramOf(t.rowErrors[0].reason, "expected"), "3");
        EXPECT_EQ(paramOf(t.rowErrors[0].reason, "actual"), "2");
        // 错误二：行 4 多列——定位首个多余列（列 4）。
        EXPECT_EQ(t.rowErrors[1].rowNo, 4u);
        EXPECT_EQ(t.rowErrors[1].colNo, 4u);
        EXPECT_EQ(t.rowErrors[1].reason.code, IoErrorCode::FormatCsvArity);
        EXPECT_EQ(paramOf(t.rowErrors[1].reason, "expected"), "3");
        EXPECT_EQ(paramOf(t.rowErrors[1].reason, "actual"), "4");
        // 错误顺序＝行号升序（§9.3 确定性：按行号列号稳定）。
        EXPECT_LT(t.rowErrors[0].rowNo, t.rowErrors[1].rowNo);
        // 正确行保留：仅行 5 交付（错误行不进入业务数据——§5.6）。
        EXPECT_EQ(t.report.dataRows, 1u);
        ASSERT_EQ(got.size(), 1u);
        EXPECT_EQ(got[0][0], "8");
        EXPECT_EQ(t.report.errorRows, 2u);
        EXPECT_TRUE(t.report.rowsPadded == 0 && t.report.rowsTrimmed == 0);
    }

    // PadTrailing：缺列补空并记录（策略处置——§5.4"按调用方策略截断补
    // 空，策略记入 ParseReport"）；多列行不在此策略内，仍默认拒绝。
    {
        CsvReadOptions pad = o;
        pad.arity = CsvArityPolicy::PadTrailing;
        pad.retainRows = true;                  // 保留行便于逐格断言
        const IoResult<RawTable> r = reader->read(anom, pad, nullptr, nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        EXPECT_EQ(r.value.report.rowsPadded, 1u);
        EXPECT_EQ(r.value.report.arityApplied, CsvArityPolicy::PadTrailing);
        // 交付行＝行3（补空后）＋行5（正确）；行4（多列）仍为行错误。
        ASSERT_EQ(r.value.rows.size(), 2u);
        EXPECT_EQ(r.value.rows[0][0], "1");
        EXPECT_EQ(r.value.rows[0][2], "");      // 缺列补空串
        EXPECT_EQ(r.value.report.errorRows, 1u);
        EXPECT_EQ(r.value.rowErrors[0].rowNo, 4u);
    }

    // TrimExcess：多列截断并记录；缺列行不在此策略内，仍默认拒绝。
    {
        CsvReadOptions trim = o;
        trim.arity = CsvArityPolicy::TrimExcess;
        trim.retainRows = true;
        const IoResult<RawTable> r = reader->read(anom, trim, nullptr, nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        EXPECT_EQ(r.value.report.rowsTrimmed, 1u);
        // 交付行＝行4（截到 3 列）＋行5（正确）；行3（缺列）仍为行错误。
        ASSERT_EQ(r.value.rows.size(), 2u);
        ASSERT_EQ(r.value.rows[0].size(), 3u);
        EXPECT_EQ(r.value.rows[0][2], "6");
        EXPECT_EQ(r.value.report.errorRows, 1u);
        EXPECT_EQ(r.value.rowErrors[0].rowNo, 3u);
    }
}

/**
 * 空行处置（§5.4 空行行/E10）：缺省跳过并计数；RejectBlank 策略改为拒
 * 绝（计为行错误＋blankRejected 计数——卡面未给专属码，按结构事实归
 * IO-FORMAT-CSV-ARITY，detail 注明策略出处）。
 */
TEST_F(IoCsvTest, BlankRowPoliciesSkipCountAndReject)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V05 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"},
                  std::vector<std::string>{"AT-02"});
    auto reader = makeCsvReader();
    const fs::path blanks = m_dir / "blanks.csv";
    writeFileBytes(blanks,
                   "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n"
                   "c1,c2\r\n"          // 行 2：表头
                   "\r\n"               // 行 3：空行
                   "1,2\r\n"            // 行 4
                   "\r\n"               // 行 5：空行
                   "3,4\r\n");          // 行 6
    CsvReadOptions o;
    o.headerRow = 2;

    // 缺省 Skip：跳过并计数，正确行保留。
    {
        const IoResult<RawTable> r = reader->read(blanks, o, nullptr, nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        EXPECT_EQ(r.value.report.blankSkipped, 2u);
        EXPECT_EQ(r.value.report.dataRows, 2u);
        EXPECT_TRUE(r.value.rowErrors.empty());
    }
    // RejectBlank：空行计为行错误。
    {
        CsvReadOptions rej = o;
        rej.blank = CsvBlankPolicy::Reject;
        const IoResult<RawTable> r = reader->read(blanks, rej, nullptr, nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        EXPECT_EQ(r.value.report.blankRejected, 2u);
        EXPECT_EQ(r.value.report.blankSkipped, 0u);
        ASSERT_EQ(r.value.report.errorRows, 2u);
        EXPECT_EQ(r.value.report.dataRows, 2u);
        EXPECT_EQ(r.value.rowErrors[0].reason.code, IoErrorCode::FormatCsvArity);
        EXPECT_NE(r.value.rowErrors[0].reason.detail.find("CsvBlankPolicy::Reject"),
                  std::string::npos)
            << "策略拒绝应在 detail 注明出处";
    }
}

/**
 * 行错误集合的截断纪律（§5.6）：maxRowErrors 上限，超出截断＋计数
 * （rowErrorsTruncated），正确行照常产出；StopOnFirstRowError 首错即停。
 */
TEST_F(IoCsvTest, RowErrorTruncationAndStopOnFirstError)
{
    auto reader = makeCsvReader();
    const fs::path bad = m_dir / "many_bad.csv";
    writeFileBytes(bad,
                   "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n"
                   "c1,c2\r\n"
                   "1\r\n2\r\n3\r\n");          // 行 3/4/5 全部缺列
    CsvReadOptions o;
    o.headerRow = 2;
    o.maxRowErrors = 2;                         // 上限 2（§5.6 缺省 1000 的收紧形态）

    const IoResult<RawTable> r = reader->read(bad, o, nullptr, nullptr, nullptr);
    ASSERT_TRUE(r) << r.error.detail;
    EXPECT_EQ(r.value.rowErrors.size(), 2u);            // 集合截到上限
    EXPECT_EQ(r.value.report.rowErrorsTruncated, 1u);   // 超出 1 条截断＋计数
    EXPECT_EQ(r.value.report.errorRows, 3u);            // 错误总数如实

    // StopOnFirstRowError：首错即停（ok 结束、错误入集合——§5.6 可选项）。
    CsvReadOptions stop = o;
    stop.maxRowErrors = 1000;
    stop.stopOnFirstRowError = true;
    const IoResult<RawTable> r2 = reader->read(bad, stop, nullptr, nullptr, nullptr);
    ASSERT_TRUE(r2) << r2.error.detail;
    EXPECT_EQ(r2.value.report.errorRows, 1u);
    EXPECT_EQ(r2.value.report.dataRows, 0u);
}

// =====================================================================
// 方言标识行 v1 封闭键集（acceptance 4——方言语法不超出 §5.1 声明集；
// 未知键/未知值/quote='/encoding≠utf-8 一律 IO-FORMAT-CSV-DIALECT）
// =====================================================================

/**
 * E1 读带标识文件列：标识行语法损坏（未知键/quote='/encoding≠utf-8/
 * 未知 delimiter 值/空记号）＝拒绝不猜测。同时验证合法变体（tab 词法、
 * 缺省键）按 §5.1 生效。
 */
TEST_F(IoCsvTest, DialectMarkerClosedKeySetRejections)
{
    auto reader = makeCsvReader();
    const std::string marker = "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n";
    const std::vector<std::string> badMarkers{
        "#rwcsv1 foo=1\r\n",                        // 未知键
        "#rwcsv1 quote='\r\n",                      // quote='（与转义冲突——§5.1 明文）
        "#rwcsv1 encoding=utf-16\r\n",              // encoding≠utf-8
        "#rwcsv1 delimiter=|\r\n",                  // 未知 delimiter 值
        "#rwcsv1 eol=CR\r\n",                       // 未知 eol 值（v1 只有 CRLF/LF）
        "#rwcsv1  delimiter=,\r\n",                 // 双空格＝空记号
        "#rwcsv1 delimiter=\r\n",                   // 空值
        "#rwcsv1 delimiter=, delimiter=,\r\n",      // 重复键
        "#rwcsv1 nodelim\r\n",                      // 缺 = 记号
    };
    for (std::size_t i = 0; i < badMarkers.size(); ++i) {
        const fs::path p = m_dir / ("bad_marker_" + std::to_string(i) + ".csv");
        writeFileBytes(p, badMarkers[i] + "a,b\r\n");
        const IoResult<RawTable> r = reader->read(p, CsvReadOptions{}, nullptr, nullptr, nullptr);
        ASSERT_FALSE(r) << "样例 " << i << "（" << badMarkers[i] << "）应拒绝";
        EXPECT_EQ(r.error.code, IoErrorCode::FormatCsvDialect)
            << "样例 " << i << " 错误码不符：" << r.error.detail;
    }

    // 合法变体：tab 词法与缺省键（缺失键取 v1 缺省——§5.1"缺失键"行）。
    const fs::path tabbed = m_dir / "marker_tab.csv";
    writeFileBytes(tabbed, "#rwcsv1 delimiter=tab\r\na\tb\r\n'=x\ty\r\n");
    {
        std::vector<std::vector<std::string>> got;
        const IoResult<RawTable> r = reader->read(
            tabbed, CsvReadOptions{},
            [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
                std::vector<std::string> row;
                for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                    row.emplace_back(v.field(i));
                }
                got.push_back(std::move(row));
                return true;
            },
            nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        EXPECT_TRUE(r.value.report.marked);
        EXPECT_EQ(r.value.report.dialect.delimiter, '\t'); // 报告承载生效方言
        ASSERT_EQ(got.size(), 2u);
        EXPECT_EQ(got[0][0], "a");
        // 文件层 '=x 剥离恰一个前导 ' → 原文 "=x"（§5.3 还原行）。
        EXPECT_EQ(got[1][0], "=x") << "缺省键标识行仍按带标识剥离";
    }

    // 字节级精确：#rwcsv12 不是 v1 标识——按无标识外部文件原样读入
    //（不存在"疑似本软件文件"，§5.1 判定行）。首行因此是普通数据行，
    // 逐字段原样保留。
    const fs::path v12 = m_dir / "marker_v12.csv";
    writeFileBytes(v12, "#rwcsv12 delimiter=,\r\n'=keep,=raw\r\n");
    {
        std::vector<std::vector<std::string>> got;
        const IoResult<RawTable> r = reader->read(
            v12, CsvReadOptions{},
            [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
                std::vector<std::string> row;
                for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                    row.emplace_back(v.field(i));
                }
                got.push_back(std::move(row));
                return true;
            },
            nullptr, nullptr);
        ASSERT_TRUE(r) << r.error.detail;
        EXPECT_FALSE(r.value.report.marked);
        ASSERT_EQ(got.size(), 2u);
        EXPECT_EQ(got[0][0], "#rwcsv12 delimiter=") << "非 v1 记号行必须零改写";
        EXPECT_EQ(got[1][0], "'=keep") << "非 v1 记号文件字段必须零改写（'=keep 原样）";
        EXPECT_EQ(got[1][1], "=raw");
    }
}

// =====================================================================
// probe（§9.3）——标识行方言／分隔符嗅探／无一致结果拒绝
// =====================================================================

/**
 * probe：带标识文件返回标识行方言（字节级判定）；无标识文件按 §5.2 统
 * 计嗅探（';' 文件选中分号）；单列文件无一致候选→DIALECT（含统计摘
 * 要，供调用方提示用户显式选择——字段映射 UI 归 requirements/ui）。
 */
TEST_F(IoCsvTest, ProbeDetectsMarkerAndSniffsDelimiter)
{
    auto reader = makeCsvReader();

    // 带标识：方言即标识行声明。
    const fs::path marked = m_dir / "probe_marked.csv";
    writeFileBytes(marked,
                   "#rwcsv1 delimiter=; quote=\" eol=LF encoding=utf-8\na;b\n1;2\n");
    const IoResult<CsvDialect> pm = reader->probe(marked, nullptr, nullptr);
    ASSERT_TRUE(pm) << pm.error.detail;
    EXPECT_EQ(pm.value.delimiter, ';');
    EXPECT_EQ(pm.value.eol, CsvEol::Lf);

    // 无标识：分号文件嗅探选中 ';'。
    const fs::path semi = m_dir / "probe_semi.csv";
    writeFileBytes(semi, "a;b;c\n1;2;3\n4;5;6\n");
    const IoResult<CsvDialect> ps = reader->probe(semi, nullptr, nullptr);
    ASSERT_TRUE(ps) << ps.error.detail;
    EXPECT_EQ(ps.value.delimiter, ';');

    // 无标识：逗号文件选中 ','。
    const fs::path comma = m_dir / "probe_comma.csv";
    writeFileBytes(comma, "a,b\n1,2\n3,4\n");
    const IoResult<CsvDialect> pc = reader->probe(comma, nullptr, nullptr);
    ASSERT_TRUE(pc) << pc.error.detail;
    EXPECT_EQ(pc.value.delimiter, ',');

    // 单列文件：无候选在全部采样行出现→无一致结果→DIALECT（含统计摘要）。
    const fs::path single = m_dir / "probe_single.csv";
    writeFileBytes(single, "a\nb\nc\n");
    const IoResult<CsvDialect> pd = reader->probe(single, nullptr, nullptr);
    ASSERT_FALSE(pd) << "单列文件应判方言不定（§5.2 无一致结果）";
    EXPECT_EQ(pd.error.code, IoErrorCode::FormatCsvDialect);
    EXPECT_NE(pd.error.detail.find("sniff"), std::string::npos) << "诊断应含候选统计摘要";
}

// =====================================================================
// 预算经读取器通道生效（IO-T02 V06 交接承诺：ICsvReader 行检查点接同
// 一 BudgetGuard 行维）＋数据-only 源扫描（acceptance 4）
// =====================================================================

/**
 * 行预算（NFR-SEC-02）：调用方开 scope（tighten CsvRowCount=3）传入
 * reader——第 4 行检查点触发 IO-SEC-BUDGET-ROWS，三要素（actual/limit/
 * unit=count）齐备；超限即中止（§4.4⑤），无部分产物。
 */
TEST_F(IoCsvTest, RowBudgetChargedThroughReaderChannel)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V06 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-02", "NFR-PERF-03"},
                  std::vector<std::string>{});
    const fs::path big = m_dir / "budget.csv";
    writeFileBytes(big, "a,1\na,2\na,3\na,4\na,5\na,6\n");   // 6 行（无标识）
    BudgetSpec spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::CsvRowCount, 3);           // 行预算设 3
    const IBudgetGuardPtr guard = makeBudgetGuard();
    const BudgetScopeId scope = guard->openScope(spec).value;

    auto reader = makeCsvReader();
    const IoResult<RawTable> r = reader->read(big, CsvReadOptions{}, nullptr, guard.get(),
                                              nullptr, {}, scope);
    ASSERT_FALSE(r) << "超行预算必须中止";
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetRows) << r.error.detail;
    EXPECT_EQ(paramOf(r.error, "actual"), "4") << "三要素·实际值＝第 4 笔检查点";
    EXPECT_EQ(paramOf(r.error, "limit"), "3") << "三要素·上限＝tighten 后限额";
    EXPECT_EQ(paramOf(r.error, "unit"), "count");
}

/**
 * 数据-only（acceptance 4/§12 IO-T03 禁止项"无公式/命令执行路径"）的
 * 源级断言：CSV 通道源文件零进程执行/命令解释入口 token（system、
 * popen、exec 族、CreateProcess、ShellExecute、WinExec 等）——解析路
 * 径只把单元格当文本承载，结构上不存在执行入口。
 */
TEST_F(IoCsvTest, DataOnlyNoExecutionEntryInCsvChannelSources)
{
    const std::vector<std::string> sources{
        std::string(IRD_IO_UNIT_ROOT) + "/io/include/sdurws/ird/io/Csv.hpp",
        std::string(IRD_IO_UNIT_ROOT) + "/io/src/Csv.cpp",
    };
    const std::vector<const char*> forbiddenTokens{
        "system(",  "popen",       "_pclose",     "execl",       "execle",
        "execlp",   "execv",       "execvp",      "CreateProcess", "ShellExecute",
        "WinExec",  "CoCreateInstance", "std::system",
    };
    for (const auto& src : sources) {
        std::ifstream in(src, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << src;
        const std::string content((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        for (const char* token : forbiddenTokens) {
            EXPECT_EQ(content.find(token), std::string::npos)
                << "CSV 通道出现执行入口 token '" << token << "'：" << src;
        }
    }
}

// =====================================================================
// 引号不闭合（E4）与写侧防护（E7 写侧 NUL／非有限实数／非法方言／
// RAII 放弃／未 open 契约违约）
// =====================================================================

/**
 * 引号不闭合（§5.4 引号不闭合行/E4）：物理文件结束仍处引号内＝致命拒
 * 绝，定位起始行（params row）。
 */
TEST_F(IoCsvTest, UnclosedQuoteFatalAtStartRow)
{
    auto reader = makeCsvReader();
    const fs::path unclosed = m_dir / "unclosed.csv";
    writeFileBytes(unclosed, "a,b\r\n\"opens,never\r\nclosed");  // 行 2 引号不闭合
    const IoResult<RawTable> r = reader->read(unclosed, CsvReadOptions{}, nullptr, nullptr, nullptr);
    ASSERT_FALSE(r) << "引号不闭合必须拒绝";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatCsvQuote) << r.error.detail;
    EXPECT_EQ(paramOf(r.error, "row"), "2") << "应定位引号起始行";
}

/**
 * 写侧防护（§9.4）：NUL 单元格＝IO-FORMAT-CSV-CHAR（E7 写侧）；非有限
 * 实数＝防御性内部错误（isfinite 前置的运行时形态）；非法方言
 * （quote='）＝IO-FORMAT-CSV-DIALECT 拒开；未 open 即 write/finish＝契
 * 约违约拒绝。失败路径目标文件零写入（§9.4 后置"finish 前失败＝目标
 * 不变"）。
 */
TEST_F(IoCsvTest, WriterRejectsNulNonFiniteAndBadDialectWithoutTouchingTarget)
{
    auto reader_unused = makeCsvReader();       // （保持 reader 工厂在用例内可见）
    static_cast<void>(reader_unused);

    // ① 非法方言：quote=' 拒开（§5.1 与转义符冲突）。
    {
        auto writer = makeCsvWriter();
        CsvWriteOptions bad;
        bad.dialect.quote = '\'';               // 违反 v1 封闭键集
        const IoResult<void> r = writer->open(
            sdurws::ird::io::CsvOutputTarget::file(m_dir / "never.csv"), bad);
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error.code, IoErrorCode::FormatCsvDialect);
        EXPECT_FALSE(fs::exists(m_dir / "never.csv")) << "拒开后不得有目标文件";
    }

    // ② NUL 单元格：写出拒绝，目标不变（E7 写侧）。
    {
        const fs::path target = m_dir / "nul_target.csv";
        auto writer = makeCsvWriter();
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::file(target), {}));
        std::string withNul = "a";
        withNul.push_back('\0');
        withNul += "b";
        const IoResult<void> r = writer->writeRow({CsvCell::fromText(withNul)});
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error.code, IoErrorCode::FormatCsvChar);
        const IoResult<void> f = writer->finish();
        EXPECT_FALSE(f) << "损坏会话不得 finish";
        EXPECT_FALSE(fs::exists(target)) << "失败路径目标不得存在";
    }

    // ③ 非有限实数：防御性内部错误（不静默落盘 nan/inf）。
    {
        const fs::path target = m_dir / "nan_target.csv";
        auto writer = makeCsvWriter();
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::file(target), {}));
        const IoResult<void> r = writer->writeRow({CsvCell::fromReal(std::nan(""))});
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error.code, IoErrorCode::FormatInternal);
        EXPECT_FALSE(fs::exists(target));
    }

    // ④ 未 open 即 write/finish：调用方契约违约的防御性拒绝。
    {
        auto writer = makeCsvWriter();
        const IoResult<void> r = writer->writeRow({CsvCell::fromText("x")});
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error.code, IoErrorCode::FormatInternal);
        const IoResult<void> f = writer->finish();
        ASSERT_FALSE(f);
        EXPECT_EQ(f.error.code, IoErrorCode::FormatInternal);
    }
}

/**
 * 数值规范化（§5.5 数值规范化行）：Int/Real 单元格以 std::to_chars 最短
 * 表示写出（'.' 小数点、无本地化，同一 double 两次写出字节相同）；数值
 * 与空字段不经前缀转义（§5.3"数值/空字段不经此规则"）——文件层裸
 * "-2.5"，读取侧（只剥 ' 前缀）原样还原。
 */
TEST_F(IoCsvTest, WriterFormatsNumbersCanonicallyWithoutEscape)
{
    // 写出字节钉住：3.0→"3"（最短表示）；实数负值无 ' 前缀；空标记＝空字段。
    {
        std::string buffer;
        auto writer = makeCsvWriter();
        CsvWriteOptions opts;
        opts.emitDialectMarker = false;         // 聚焦数据行字节
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::memory(buffer), opts));
        ASSERT_TRUE(writer->writeRow(
            {CsvCell::fromInt(-42), CsvCell::fromReal(0.1), CsvCell::fromReal(3.0)}));
        ASSERT_TRUE(writer->writeRow({CsvCell::fromInt(0), CsvCell::fromReal(-2.5), CsvCell::empty()}));
        ASSERT_TRUE(writer->finish());
        EXPECT_EQ(buffer, "-42,0.1,3\r\n0,-2.5,\r\n");
    }

    // 带标识读取：数值文本原样还原（-2.5 不被剥离——剥离只针对 ' 前缀），
    // 数值语义归业务单元（io 保留原文——§5.8 原文保留行）。
    std::vector<std::vector<std::string>> got;
    auto reader = makeCsvReader();
    const fs::path nums = m_dir / "nums.csv";
    {
        auto writer = makeCsvWriter();
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::file(nums), {}));
        ASSERT_TRUE(writer->writeRow(
            {CsvCell::fromInt(-42), CsvCell::fromReal(-2.5), CsvCell::fromReal(1e21)}));
        ASSERT_TRUE(writer->finish());
    }
    const IoResult<RawTable> r = reader->read(
        nums, CsvReadOptions{},
        [&](std::uint64_t, sdurws::ird::io::CsvRowView&& v) {
            std::vector<std::string> row;
            for (std::size_t i = 0; i < v.fieldCount(); ++i) {
                row.emplace_back(v.field(i));
            }
            got.push_back(std::move(row));
            return true;
        },
        nullptr, nullptr);
    ASSERT_TRUE(r) << r.error.detail;
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0][0], "-42");
    EXPECT_EQ(got[0][1], "-2.5") << "数值字段不得被加/剥转义前缀";
    EXPECT_EQ(got[0][2], "1e+21") << "最短表示（科学计数由 to_chars 决定——确定性）";
}

/**
 * RAII 放弃语义（§9.4 生命周期行/§5.6"取消即中止（目标不受影响）"）：
 * 未 finish 析构＝清理暂存，发布目标从未出现。
 */
TEST_F(IoCsvTest, WriterRaiiAbandonLeavesNoStagingResidue)
{
    const fs::path target = m_dir / "abandoned.csv";
    const fs::path staging = target.string() + ".ird-csv-tmp";
    {
        auto writer = makeCsvWriter();
        ASSERT_TRUE(writer->open(sdurws::ird::io::CsvOutputTarget::file(target), {}));
        ASSERT_TRUE(writer->writeRow({CsvCell::fromText("half")}));
        // 不 finish——作用域结束析构＝放弃。
    }
    EXPECT_FALSE(fs::exists(target)) << "放弃写出不产生目标";
    EXPECT_FALSE(fs::exists(staging)) << "放弃写出不残留暂存文件";
}
