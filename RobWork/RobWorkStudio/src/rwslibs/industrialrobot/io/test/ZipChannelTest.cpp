/**
 * @file   ZipChannelTest.cpp
 * @brief  ZIP 通道用例组（IoZip）——解包逐字节还原（STORED/DEFLATE、
 *         二进制安全）、manifest 条目哈希校验（IO-PACK-HASH-MISMATCH
 *         定位——V14 前置能力）、加密条目/压缩方法白名单拒绝、CRC 容器
 *         完整性、预算经调用方 scope 生效（含压缩比炸弹预检）、取消检查点。
 *
 * 设计依据：
 *   - units/io.md §7.1（包格式契约——容器层约束：加密拒绝/仅 STORED·
 *     DEFLATE/SHA-256 条目哈希）、§12 IO-T04 行（"zip 解包逐字节还原
 *     ＋哈希（V14 前置）"）、§11.1（"恶意 zip 构造器"＝登记的测试替身
 *     ——本文件手工字节构造器为其实现）、§11.2 IO-V12/V13/V14 行的容器
 *     层前置能力（完整九步协议语义归 IO-T06——不在本文件）
 *   - 需求 PM-05（.rwpack 完整性）、NFR-SEC-02（解包防护/炸弹）、
 *     SA-12/NFR-MNT-03（SHA-256 唯一摘要算法——复算恒经 core
 *     ContentDigester）、NFR-DEP-03（P-IO-3 冻结：libzip 1.11.4）
 *   - 任务契约 tasks/foundation/IO-T04.json acceptance 4
 *
 * 断言纪律（AGENTS.md §2.7）：每个用例中文注明验证的需求/验收条目；
 * 合法样例经 libzip 写侧构造（与被测读侧互为独立实现），恶意样例经手
 * 工字节构造（加密标志/非法压缩方法/CRC 谎报）——失败如实失败。
 */

#include <sdurws/ird/io/ZipChannel.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoFwd.hpp>

#include <zip.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::Digest256;
using sdurws::ird::io::BudgetDimension;
using sdurws::ird::io::BudgetSpec;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::IoResult;
using sdurws::ird::io::ZipEntryInfo;
using sdurws::ird::io::ZipManifestEntry;
using sdurws::ird::io::ZipEntryVerifyReport;
using sdurws::ird::io::openZipChannel;

namespace {

// =====================================================================
// 测试助手
// =====================================================================

/// 从 IoError params 取键值（定位/比较要素断言用）。
std::string paramOf(const sdurws::ird::io::IoError& e, const char* key)
{
    for (const auto& kv : e.params) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return {};
}

/// 写二进制文件（构造包文件载体）。
void writeFileBytes(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_FALSE(out.fail()) << "测试文件写入失败：" << path.string();
}

/// 字节内容 SHA-256（manifest 清单装配用——core ContentDigester）。
Digest256 sha256Of(const std::string& bytes)
{
    ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

/// libzip 写侧条目规格（合法样例构造器——与被测读侧互为独立实现）。
struct LibZipEntry {
    std::string name;
    std::string data;
    std::int32_t method;   // ZIP_CM_STORE / ZIP_CM_DEFLATE
};

/// 经 libzip 写侧构造归档（合法样例——STORED/DEFLATE 逐字节还原的载体）。
void makeArchiveViaLibzip(const fs::path& path, const std::vector<LibZipEntry>& entries)
{
    zip_t* za = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
    ASSERT_NE(za, nullptr) << "libzip 写侧打开失败：" << path.string();
    for (const LibZipEntry& e : entries) {
        // 数据拷贝入 malloc（freep=1——source 释放时回收，生存期归 libzip）。
        void* copy = std::malloc(e.data.size());
        ASSERT_NE(copy, nullptr) << "malloc 失败";
        std::memcpy(copy, e.data.data(), e.data.size());
        zip_source_t* src = zip_source_buffer(za, copy, e.data.size(), 1);
        ASSERT_NE(src, nullptr) << "zip_source_buffer 失败";
        const zip_int64_t idx = zip_file_add(za, e.name.c_str(), src, ZIP_FL_ENC_UTF_8);
        ASSERT_GE(idx, 0) << "zip_file_add 失败：" << e.name;
        ASSERT_EQ(zip_set_file_compression(za, static_cast<zip_uint64_t>(idx), e.method, 0), 0)
            << "压缩方法设置失败：" << e.name;
    }
    ASSERT_EQ(zip_close(za), 0) << "zip_close 失败";
}

/// 手工 zip 条目规格（恶意样例构造器——容器头字段可任意谎报）。
struct RawZipEntry {
    std::string name;
    std::string data;             // STORED 原文（构造器只产 STORED 形态）
    std::uint16_t flags = 0;      // 一般 purpose 标志（bit0＝加密标记）
    std::uint16_t method = 0;     // 压缩方法（0=STORED；可谎报白名单外值）
    bool corruptCrc = false;      // true＝CRC 写成真实值的反码（容器完整性用例）
};

/// 小端追加（zip 头全小端——PKWARE APPNOTE 字节序）。
void appendU16(std::string& out, std::uint16_t v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void appendU32(std::string& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

/// 手工构造单层 zip 字节流（STORED 形态——中央目录＋本地头＋EOCD；
/// 字段谎报能力＝恶意用例的注入面：加密标志/非法压缩方法/CRC 谎报）。
std::string handcraftZip(const std::vector<RawZipEntry>& entries)
{
    // CRC-32（IEEE 802.3，zip 口径——测试侧独立表驱动实现）。
    static std::uint32_t table[256];
    static bool tableReady = false;
    if (!tableReady) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        tableReady = true;
    }
    auto crc32Of = [](const std::string& s) {
        // table 为函数级 static（自动存储要求——简单捕获不适用，静态可
        // 直接访问，无捕获）。
        std::uint32_t c = 0xFFFFFFFFu;
        for (const char ch : s) {
            c = table[(c ^ static_cast<std::uint8_t>(ch)) & 0xFF] ^ (c >> 8);
        }
        return c ^ 0xFFFFFFFFu;
    };

    std::string out;
    struct CdRow {
        std::string name;
        std::uint32_t crc;
        std::uint32_t compSize;
        std::uint32_t uncompSize;
        std::uint16_t flags;
        std::uint16_t method;
        std::uint32_t localOffset;
    };
    std::vector<CdRow> cd;
    for (const RawZipEntry& e : entries) {
        const std::uint32_t crcRaw = crc32Of(e.data);
        const std::uint32_t crc = e.corruptCrc ? (crcRaw ^ 0xFFFFFFFFu) : crcRaw;
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        // 本地文件头（PK\x03\x04）。
        out.append("PK\x03\x04", 4);
        appendU16(out, 20);            // version needed
        appendU16(out, e.flags);       // 一般标志（bit0＝加密）
        appendU16(out, e.method);      // 压缩方法
        appendU16(out, 0);             // 修改时间
        appendU16(out, 0);             // 修改日期
        appendU32(out, crc);           // CRC-32
        appendU32(out, static_cast<std::uint32_t>(e.data.size()));   // 压缩大小
        appendU32(out, static_cast<std::uint32_t>(e.data.size()));   // 未压缩大小
        appendU16(out, static_cast<std::uint16_t>(e.name.size()));   // 名长
        appendU16(out, 0);             // 扩展区长度
        out.append(e.name);
        out.append(e.data);            // STORED 数据
        cd.push_back(CdRow{e.name, crc, static_cast<std::uint32_t>(e.data.size()),
                           static_cast<std::uint32_t>(e.data.size()), e.flags, e.method, offset});
    }
    // 中央目录（PK\x01\x02）。
    const std::uint32_t cdOffset = static_cast<std::uint32_t>(out.size());
    for (const CdRow& r : cd) {
        out.append("PK\x01\x02", 4);
        appendU16(out, 20);            // 创建方版本
        appendU16(out, 20);            // 所需版本
        appendU16(out, r.flags);       // 标志（加密位同步）
        appendU16(out, r.method);      // 压缩方法
        appendU16(out, 0);             // 时间
        appendU16(out, 0);             // 日期
        appendU32(out, r.crc);
        appendU32(out, r.compSize);
        appendU32(out, r.uncompSize);
        appendU16(out, static_cast<std::uint16_t>(r.name.size()));
        appendU16(out, 0);             // 扩展区
        appendU16(out, 0);             // 注释长
        appendU16(out, 0);             // 起始盘号
        appendU16(out, 0);             // 内部属性
        appendU32(out, 0);             // 外部属性
        appendU32(out, r.localOffset);
        out.append(r.name);
    }
    // EOCD（PK\x05\x06）。
    const std::uint32_t cdSize = static_cast<std::uint32_t>(out.size()) - cdOffset;
    out.append("PK\x05\x06", 4);
    appendU16(out, 0);   // 本盘号
    appendU16(out, 0);   // 中央目录起始盘
    appendU16(out, static_cast<std::uint16_t>(cd.size()));
    appendU16(out, static_cast<std::uint16_t>(cd.size()));
    appendU32(out, cdSize);
    appendU32(out, cdOffset);
    appendU16(out, 0);   // 注释长
    return out;
}

/// 用例自持临时目录（CsvTest/JsonTest 同款形态）。
class IoZipTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / "ird_wp11_t05_zip"
                / (std::string(info->name()) + "_" + std::to_string(std::random_device{}()));
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
// 解包逐字节还原（acceptance 4——PM-05/NFR-SEC-02；libzip 读侧与写侧
// 互为独立实现，逐字节一致即还原正确）
// =====================================================================

/**
 * STORED 与 DEFLATE 条目逐字节还原：文本（中文/引号）、二进制（NUL/
 * 高位字节）、空条目——listEntries 元数据与 readEntryBytes 原文全等
 * （§12 IO-T04 行"zip 解包逐字节还原"）。
 */
TEST_F(IoZipTest, StoreAndDeflateEntriesByteExactRestore)
{
    // 字面量分段书写（十六进制转义贪吃后续 hex 位——\x80b 会被吞成超界
    // 值；分段是 C++ 字节串的标准防御写法）。
    const std::string binary = std::string("BIN\x00\x01" "\xFF" "\x80" "bin", 8);
    const std::string text = "héllo \"quoted\" 中文 ✓ line2\nline3";
    const fs::path pack = m_dir / "sample.pack";
    makeArchiveViaLibzip(pack, {{"a_stored.bin", binary, ZIP_CM_STORE},
                                {"b_deflated.txt", text, ZIP_CM_DEFLATE},
                                {"c_empty.bin", "", ZIP_CM_STORE}});
    const IoResult<std::unique_ptr<sdurws::ird::io::IZipChannel>> opened = openZipChannel(pack);
    ASSERT_TRUE(opened) << "合法归档必须可打开：" << opened.error.detail;
    sdurws::ird::io::IZipChannel& zip = *opened.value;
    // 条目枚举：名称/序号/加密标记/压缩方法元数据正确。
    const IoResult<std::vector<ZipEntryInfo>> list = zip.listEntries();
    ASSERT_TRUE(list) << list.error.detail;
    ASSERT_EQ(list.value.size(), 3u);
    EXPECT_EQ(list.value[0].name, "a_stored.bin");
    EXPECT_EQ(list.value[0].uncompressedSize, binary.size());
    EXPECT_FALSE(list.value[0].encrypted);
    EXPECT_EQ(list.value[0].compressionMethod, ZIP_CM_STORE);
    EXPECT_EQ(list.value[1].compressionMethod, ZIP_CM_DEFLATE);
    // 逐字节还原：三条目原文全等（含空条目）。
    const IoResult<std::string> b0 =
        zip.readEntryBytes("a_stored.bin", nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_TRUE(b0) << b0.error.detail;
    EXPECT_EQ(b0.value, binary) << "STORED 二进制逐字节还原（NUL/高位字节安全）";
    const IoResult<std::string> b1 =
        zip.readEntryBytes("b_deflated.txt", nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_TRUE(b1) << b1.error.detail;
    EXPECT_EQ(b1.value, text) << "DEFLATE 解压还原一致";
    const IoResult<std::string> b2 =
        zip.readEntryBytes("c_empty.bin", nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_TRUE(b2) << b2.error.detail;
    EXPECT_TRUE(b2.value.empty()) << "空条目还原为空";
    // 不存在条目 → IO-PACK-REF-INCOMPLETE（清单引用完整性语义——V29 前置）。
    const IoResult<std::string> missing =
        zip.readEntryBytes("no/such.bin", nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error.code, IoErrorCode::PackRefIncomplete);
    EXPECT_EQ(paramOf(missing.error, "ref"), "no/such.bin");
}

// =====================================================================
// manifest 条目哈希校验（acceptance 4——V14 前置能力；定位条目）
// =====================================================================

/**
 * 哈希校验三态：全部一致 → 逐条目 Ok；篡改一条声明摘要 → 该条目
 * IO-PACK-HASH-MISMATCH（params entry/expected/actual——定位条目），
 * 其余条目结论不受牵连；清单引用不存在条目 → IO-PACK-REF-INCOMPLETE
 * （params ref）。复算恒经 core ContentDigester（SA-12 唯一摘要算法）。
 */
TEST_F(IoZipTest, ManifestHashVerifyOkMismatchLocatedAndRefIncomplete)
{
    const std::string data1 = "payload-one-中文";
    const std::string data2 = "payload-two";
    const fs::path pack = m_dir / "hash.pack";
    makeArchiveViaLibzip(pack, {{"p/one.txt", data1, ZIP_CM_DEFLATE},
                                {"p/two.txt", data2, ZIP_CM_STORE}});
    auto opened = openZipChannel(pack);
    ASSERT_TRUE(opened);
    sdurws::ird::io::IZipChannel& zip = *opened.value;
    // ① 全部一致 → 全 Ok（顺序＝清单序——确定性报告面）。
    std::vector<ZipManifestEntry> entries;
    ZipManifestEntry e1;
    e1.path = "p/one.txt";
    e1.size = data1.size();
    e1.sha256 = sha256Of(data1);
    ZipManifestEntry e2;
    e2.path = "p/two.txt";
    e2.size = data2.size();
    e2.sha256 = sha256Of(data2);
    entries.push_back(e1);
    entries.push_back(e2);
    const IoResult<std::vector<ZipEntryVerifyReport>> ok =
        zip.verifyManifestEntries(entries, nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_TRUE(ok) << ok.error.detail;
    ASSERT_EQ(ok.value.size(), 2u);
    EXPECT_EQ(ok.value[0].error.code, IoErrorCode::Ok);
    EXPECT_EQ(ok.value[1].error.code, IoErrorCode::Ok);
    // ② 篡改条目一的声明摘要（逐位翻转）→ 该条目 HASH-MISMATCH＋三参
    //    定位；条目二结论不受牵连（条目级失败不中断整表——V14 报告形态）。
    entries[0].sha256[0] ^= 0xFF;
    const IoResult<std::vector<ZipEntryVerifyReport>> bad =
        zip.verifyManifestEntries(entries, nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_TRUE(bad) << bad.error.detail;
    ASSERT_EQ(bad.value.size(), 2u);
    EXPECT_EQ(bad.value[0].error.code, IoErrorCode::PackHashMismatch);
    EXPECT_EQ(paramOf(bad.value[0].error, "entry"), "p/one.txt") << "定位条目";
    EXPECT_EQ(paramOf(bad.value[0].error, "expected").size(), 64u) << "声明摘要 hex";
    EXPECT_EQ(paramOf(bad.value[0].error, "actual").size(), 64u) << "复算摘要 hex";
    EXPECT_NE(paramOf(bad.value[0].error, "expected"), paramOf(bad.value[0].error, "actual"));
    EXPECT_EQ(bad.value[1].error.code, IoErrorCode::Ok) << "其余条目不受牵连";
    // ③ 清单引用不存在条目 → REF-INCOMPLETE（ref 参数；清单重建——
    //    不与前段篡改状态纠缠）。
    ZipManifestEntry ghost;
    ghost.path = "p/ghost.txt";
    ghost.size = 1;
    ghost.sha256 = sha256Of("x");
    const std::vector<ZipManifestEntry> withGhost{e1, ghost};
    const IoResult<std::vector<ZipEntryVerifyReport>> ref =
        zip.verifyManifestEntries(withGhost, nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_TRUE(ref) << ref.error.detail;
    ASSERT_EQ(ref.value.size(), 2u);
    EXPECT_EQ(ref.value[0].error.code, IoErrorCode::Ok);
    EXPECT_EQ(ref.value[1].error.code, IoErrorCode::PackRefIncomplete);
    EXPECT_EQ(paramOf(ref.value[1].error, "ref"), "p/ghost.txt");
    // ④ 大小谎报 → HASH-MISMATCH（expected/actual＝字节数）。
    ZipManifestEntry wrongSize;
    wrongSize.path = "p/one.txt";
    wrongSize.size = data1.size() + 5;
    wrongSize.sha256 = sha256Of(data1);
    const std::vector<ZipManifestEntry> sizeOnly{wrongSize};
    const IoResult<std::vector<ZipEntryVerifyReport>> sz =
        zip.verifyManifestEntries(sizeOnly, nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_TRUE(sz);
    ASSERT_EQ(sz.value.size(), 1u);
    EXPECT_EQ(sz.value[0].error.code, IoErrorCode::PackHashMismatch);
    EXPECT_EQ(paramOf(sz.value[0].error, "expected"), std::to_string(data1.size() + 5));
    EXPECT_EQ(paramOf(sz.value[0].error, "actual"), std::to_string(data1.size()));
}

// =====================================================================
// 恶意样例拒绝（§11.1"恶意 zip 构造器"替身——手工字节注入面）
// =====================================================================

/**
 * 加密条目拒绝（§7.1 加密拒绝——解压前检测 encryption_method；手工置
 * 一般标志 bit0 构造；listEntries 元数据同步呈报 encrypted=true）。
 */
TEST_F(IoZipTest, EncryptedEntryRejectedBeforeDecompression)
{
    const fs::path pack = m_dir / "enc.pack";
    writeFileBytes(pack, handcraftZip({{"secret.txt", "top secret", /*flags=*/1}}));
    auto opened = openZipChannel(pack);
    ASSERT_TRUE(opened) << "加密标志不影响容器打开：" << opened.error.detail;
    sdurws::ird::io::IZipChannel& zip = *opened.value;
    // 元数据呈报加密标记（导入器条目约束裁决的事实面）。
    const IoResult<std::vector<ZipEntryInfo>> list = zip.listEntries();
    ASSERT_TRUE(list);
    ASSERT_EQ(list.value.size(), 1u);
    EXPECT_TRUE(list.value[0].encrypted) << "加密标记呈报";
    // 读取拒绝：IO-FORMAT-PACK-ENCRYPTED（不给解密尝试面——§7.1）。
    const IoResult<std::string> r =
        zip.readEntryBytes("secret.txt", nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatPackEncrypted);
}

/**
 * 白名单外压缩方法拒绝（§7.1"压缩方法仅 STORED/DEFLATE"——手工谎报
 * method=93〔zstd〕；防把额外解码器攻击面引入导入路径）。
 */
TEST_F(IoZipTest, UnsupportedCompressionMethodRejected)
{
    const fs::path pack = m_dir / "method.pack";
    writeFileBytes(pack, handcraftZip({{"weird.bin", "data", /*flags=*/0, /*method=*/93}}));
    auto opened = openZipChannel(pack);
    ASSERT_TRUE(opened) << opened.error.detail;
    const IoResult<std::string> r = opened.value->readEntryBytes(
        "weird.bin", nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatPackZip);
}

/**
 * CRC 谎报检出（容器级完整性——libzip 在 fclose 报告 CRC 错误，通道
 * 映射 IO-FORMAT-PACK-ZIP；与 SHA-256 内容身份互不替代）。
 */
TEST_F(IoZipTest, CorruptedCrcDetectedAtContainerLevel)
{
    const fs::path pack = m_dir / "crc.pack";
    writeFileBytes(pack, handcraftZip({{"f.bin", "intact-data", 0, 0, /*corruptCrc=*/true}}));
    auto opened = openZipChannel(pack);
    ASSERT_TRUE(opened) << opened.error.detail;
    const IoResult<std::string> r = opened.value->readEntryBytes(
        "f.bin", nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_FALSE(r) << "CRC 不符必须拒绝（容器完整性）";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatPackZip);
}

/**
 * 非 ZIP 内容与缺失文件（容器层打开失败 → IO-FORMAT-PACK-ZIP；文件不
 * 存在 → IO-RES-NOT-FOUND——环境错误四分类）。
 */
TEST_F(IoZipTest, NonZipContentAndMissingFileMapCorrectly)
{
    const fs::path text = m_dir / "not-a-zip.pack";
    writeFileBytes(text, "this is plainly not a zip container");
    auto opened = openZipChannel(text);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error.code, IoErrorCode::FormatPackZip);
    const auto missing = openZipChannel(m_dir / "nope.pack");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error.code, IoErrorCode::ResNotFound);
}

// =====================================================================
// 预算与取消（§4.5.2 包通道预算；§9.13 协作取消）
// =====================================================================

/**
 * 展开量预算经调用方 scope 生效（§4.5.2——条目声明大小先入账的预检笔；
 * 调用方 tighten ArchiveExpandedBytes 后 chargeArchive 拒绝并给三要素）。
 */
TEST_F(IoZipTest, ExpandedBytesBudgetChargedThroughCallerScope)
{
    const fs::path pack = m_dir / "budget.pack";
    makeArchiveViaLibzip(pack, {{"big.bin", std::string(1000, 'x'), ZIP_CM_STORE}});
    auto opened = openZipChannel(pack);
    ASSERT_TRUE(opened);
    // 收紧 ArchiveExpandedBytes 到 100：声明量 1000 的条目在预检笔拒绝。
    auto guard = sdurws::ird::io::makeBudgetGuard();
    ASSERT_NE(guard, nullptr);
    auto spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::ArchiveExpandedBytes, 100);
    const auto scope = guard->openScope(spec);
    ASSERT_TRUE(scope);
    const IoResult<std::string> r =
        opened.value->readEntryBytes("big.bin", guard.get(), scope.value, nullptr);
    ASSERT_FALSE(r) << "声明展开量超预算必须预检拒绝（§4.5.2 声明先入账）";
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetExpand);
    EXPECT_EQ(paramOf(r.error, "limit"), "100");
    EXPECT_EQ(paramOf(r.error, "unit"), "bytes");
}

/**
 * 压缩比炸弹预检（§4.5.2 ArchiveRatio 比较型——高压缩比条目在解压前
 * 以 IO-SEC-BOMB-RATIO 拒绝；1 万字节数据 DEFLATE 后约几十字节，比例
 * 远超默认 100:1）。
 */
TEST_F(IoZipTest, ZipBombRatioRejectedAtDeclaredPrecheck)
{
    const fs::path pack = m_dir / "bomb.pack";
    // 1 MiB 全零（DEFLATE 后约千余字节——比例远超 100:1，余量充分）。
    makeArchiveViaLibzip(pack, {{"zeros.bin", std::string(1024 * 1024, '\0'), ZIP_CM_DEFLATE}});
    auto opened = openZipChannel(pack);
    ASSERT_TRUE(opened);
    // 默认产品规格（100:1）——压缩侧千余字节、展开声明 1 MiB → 比例超
    // 限在预检笔命中（解压前——炸弹不获得执行窗口）。
    const IoResult<std::string> r = opened.value->readEntryBytes(
        "zeros.bin", nullptr, sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_FALSE(r) << "高压缩比条目必须预检拒绝";
    EXPECT_EQ(r.error.code, IoErrorCode::SecBombRatio);
    EXPECT_FALSE(paramOf(r.error, "actual").empty()) << "比较型三要素";
}

/**
 * 取消检查点：恒取消令牌在读取窗口命中 → IO-CANCELLED（状态非错误，
 * UX-03）。
 */
TEST_F(IoZipTest, CancelTokenReturnsStateDuringRead)
{
    const fs::path pack = m_dir / "cancel.pack";
    makeArchiveViaLibzip(pack, {{"d.txt", std::string(200000, 'z'), ZIP_CM_STORE}});
    auto opened = openZipChannel(pack);
    ASSERT_TRUE(opened);
    struct Cancelled : sdurws::ird::io::IoCancelToken {
        bool isCancelled() const override { return true; }
    } token;
    const IoResult<std::string> r = opened.value->readEntryBytes(
        "d.txt", nullptr, sdurws::ird::io::BudgetScopeId{}, &token);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::Cancelled) << "取消＝状态（不落诊断——UX-03）";
}
