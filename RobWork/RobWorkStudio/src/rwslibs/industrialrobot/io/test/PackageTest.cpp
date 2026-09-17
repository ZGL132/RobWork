/**
 * @file   PackageTest.cpp
 * @brief  包导入导出＋临时区＋原子写出的单元用例组（IoPack/IoTemp/
 *         IoAtomic）——导入九步协议的安全与校验面（V12 压缩炸弹/V13 重
 *         复条目/V14 哈希不符/V19 导入取消/V29 引用完整性）、导出→导入
 *         主线 roundtrip、"失败不留目标目录"结构性断言，及临时区/原子
 *         写出设施的单元契约。
 *
 * 设计依据：
 *   - units/io.md §7.1~§7.6（包协议/威胁矩阵/临时区/清理状态图）、§9.9
 *     （导入导出契约）、§4.5.2（包通道预算）、§7.5/§9.10（TempArea/
 *     AtomicFile 契约）、§11.2（IO-V12/V13/V14/V19/V29 行——本文件的
 *     用例即其执行载体；V18/V20/V21/V23/V31/V32 故障注入面在
 *     contract_test/PackageContractTest.cpp——§3.4 双测试目标分界）
 *   - 需求 PM-05、NFR-SEC-01/02、UX-03、AT-20
 *   - 任务契约 tasks/foundation/IO-T06.json acceptance 1/2/4（V12~V14/
 *     V19/V29＋失败不留目标目录总断言）与 acceptance 5（contract_test
 *     目标之外的 test 侧用例）
 *
 * 断言纪律（AGENTS.md §2.7）：每个用例中文注明验证的需求/验收条目；
 * 合法包经 libzip 写侧构造（与被测读侧互为独立实现），恶意包（重复条
 * 目）经手工字节构造（容器头可任意编排）——失败如实失败。
 */

#include <sdurws/ird/io/Package.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/io/AtomicFile.hpp>
#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoFwd.hpp>
#include <sdurws/ird/io/Json.hpp>
#include <sdurws/ird/io/TempArea.hpp>
#include <sdurws/ird/io/ZipChannel.hpp>

#include <zip.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// PackFormat 为命名空间（§7.1 常量集）——命名空间别名引入（using 声明不适用于命名空间）。
namespace PackFormat = sdurws::ird::io::PackFormat;

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::Digest256;
using sdurws::ird::io::AtomicTarget;
using sdurws::ird::io::BudgetDimension;
using sdurws::ird::io::BudgetSpec;
using sdurws::ird::io::IoCancelToken;
using sdurws::ird::io::IoError;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::IoResult;
using sdurws::ird::io::IoString;
using sdurws::ird::io::PackageImportOptions;
using sdurws::ird::io::PackageImportSession;
using sdurws::ird::io::PackageImportState;
using sdurws::ird::io::PackFileEntry;
using sdurws::ird::io::ReplacePolicy;
using sdurws::ird::io::TempAreaRole;
using sdurws::ird::io::TempAreaSession;
using sdurws::ird::io::TempAreaSpec;

namespace {

// =====================================================================
// 测试助手（ZipChannelTest.cpp 同款口径——自持不跨文件共享）
// =====================================================================

/// 从 IoError params 取键值（定位/比较要素断言用）。
std::string paramOf(const IoError& e, const char* key)
{
    for (const auto& kv : e.params) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return {};
}

/// 字节内容 SHA-256（manifest 清单装配用——core ContentDigester，SA-12）。
Digest256 sha256Of(const std::string& bytes)
{
    ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

/// 摘要的小写十六进制（64 字符）。
std::string digestHex(const Digest256& d)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : d) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

/// 读文件字节（解包产物比对用）。
std::string readFileBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << path.string();
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

/// 伪随机不可压缩字节（LCG——V12b 超展开量样例的近 STORED 数据源；
/// 确定性生成——同参数同字节，失败可复现）。
std::string pseudoRandomBytes(std::size_t n, std::uint32_t seed)
{
    std::string out(n, '\0');
    std::uint32_t state = seed;
    for (std::size_t i = 0; i < n; ++i) {
        state = state * 1664525u + 1013904223u;
        out[i] = static_cast<char>((state >> 16) & 0xFF);
    }
    return out;
}

/// 快照源替身（project fake——ISnapshotFileSource 注入形态，§7.2；内存
/// 映射承载"一致数据视图"，enumerate/read 即快照闭包的读写面）。
class FakeSnapshotSource final : public sdurws::ird::io::ISnapshotFileSource {
public:
    std::map<std::string, std::string> files;   ///< 快照闭包（path→字节）

    IoResult<std::vector<PackFileEntry>> enumerate() const override
    {
        IoResult<std::vector<PackFileEntry>> out;
        for (const auto& kv : files) {   // map 序＝字典序（确定性清单）
            PackFileEntry e;
            e.path = kv.first;
            e.size = kv.second.size();
            out.value.push_back(std::move(e));
        }
        return out;
    }

    IoResult<IoString> read(const IoString& packPath) override
    {
        auto it = files.find(packPath);
        if (it == files.end()) {
            return failRead("快照清单外路径（fake 不应被要求读取）：" + packPath);
        }
        IoResult<IoString> out;
        out.value = it->second;
        return out;
    }

private:
    static IoResult<IoString> failRead(std::string detail)
    {
        IoResult<IoString> out;
        out.error.code = IoErrorCode::ResNotFound;
        out.error.detail = std::move(detail);
        return out;
    }
};

/// JSON 值/成员本地构造助手（Json.hpp 公共结构——受限 DOM 的测试侧装
/// 配面；与实现侧 Package.cpp 的匿名助手同构但互不共享）。
sdurws::ird::io::JsonValue jsonStr(std::string s)
{
    sdurws::ird::io::JsonValue v;
    v.type = sdurws::ird::io::JsonValue::Type::String;
    v.stringValue = std::move(s);
    return v;
}

sdurws::ird::io::JsonValue jsonInt(std::int64_t i)
{
    sdurws::ird::io::JsonValue v;
    v.type = sdurws::ird::io::JsonValue::Type::Integer;
    v.integerValue = i;
    return v;
}

void addMember(sdurws::ird::io::JsonValue& obj, std::string key,
               sdurws::ird::io::JsonValue value)
{
    sdurws::ird::io::JsonMember m;
    m.key = std::move(key);
    m.value = std::move(value);
    obj.members.push_back(std::move(m));
}

/// manifest.json 字节构造（经 io canonical 写出器——与实现侧 digest 口
/// 径同源；条目按 path 字典序——§7.1 manifest 行）。
std::string buildManifestJson(const std::vector<std::pair<std::string, std::string>>& files,
                              std::string* outDigestHex)
{
    std::vector<std::pair<std::string, std::string>> sorted = files;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    sdurws::ird::io::JsonDocument doc;
    doc.root.type = sdurws::ird::io::JsonValue::Type::Object;
    addMember(doc.root, "schemaVersion", jsonStr(PackFormat::kManifestSchemaVersion));
    sdurws::ird::io::JsonValue entries;
    entries.type = sdurws::ird::io::JsonValue::Type::Array;
    for (const auto& kv : sorted) {
        sdurws::ird::io::JsonValue item;
        item.type = sdurws::ird::io::JsonValue::Type::Object;
        addMember(item, "path", jsonStr(kv.first));
        addMember(item, "size", jsonInt(static_cast<std::int64_t>(kv.second.size())));
        addMember(item, "sha256", jsonStr(digestHex(sha256Of(kv.second))));
        entries.items.push_back(std::move(item));
    }
    addMember(doc.root, "entries", std::move(entries));
    auto bytes = sdurws::ird::io::canonicalizeJson(doc, sdurws::ird::io::JsonWriteOptions{});
    EXPECT_TRUE(bytes) << "manifest canonical 化失败";
    auto digest = sdurws::ird::io::digestCanonicalJson(doc, sdurws::ird::io::JsonWriteOptions{});
    EXPECT_TRUE(digest) << "manifest 摘要失败";
    if (outDigestHex != nullptr) {
        *outDigestHex = digestHex(digest.value);
    }
    return bytes.value;
}

/// rwpack.json 字节构造（元数据固定值——测试自描述；totalDigest 来自
/// buildManifestJson 的产出）。
std::string buildRwpackJson(const std::string& totalDigestHex, std::uint64_t fileCount,
                            std::uint64_t totalBytes)
{
    sdurws::ird::io::JsonDocument doc;
    doc.root.type = sdurws::ird::io::JsonValue::Type::Object;
    addMember(doc.root, "formatId", jsonStr(PackFormat::kFormatId));
    addMember(doc.root, "schemaVersion",
              jsonInt(static_cast<std::int64_t>(PackFormat::kSchemaVersion)));
    addMember(doc.root, "createdAtUtc", jsonStr("2026-09-17T00:00:00Z"));
    addMember(doc.root, "createdWithToolVersion", jsonStr("test/1"));
    addMember(doc.root, "sourceProjectId", jsonStr("proj-test"));
    sdurws::ird::io::JsonValue content;
    content.type = sdurws::ird::io::JsonValue::Type::Object;
    addMember(content, "headRevisionId", jsonStr("rev-test-1"));
    addMember(content, "fileCount", jsonInt(static_cast<std::int64_t>(fileCount)));
    addMember(content, "totalBytes", jsonInt(static_cast<std::int64_t>(totalBytes)));
    addMember(content, "totalDigest", jsonStr(totalDigestHex));
    addMember(doc.root, "content", std::move(content));
    auto bytes = sdurws::ird::io::canonicalizeJson(doc, sdurws::ird::io::JsonWriteOptions{});
    EXPECT_TRUE(bytes) << "rwpack.json canonical 化失败";
    return bytes.value;
}

/// libzip 写侧条目规格（合法包构造器）。
struct ZipEntrySpec {
    std::string name;
    std::string data;
    std::int32_t method;   // ZIP_CM_STORE / ZIP_CM_DEFLATE
};

/// 经 libzip 写侧构造归档（合法包——与被测读侧互为独立实现）。
void makeArchiveViaLibzip(const fs::path& path, const std::vector<ZipEntrySpec>& entries)
{
    zip_t* za = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
    ASSERT_NE(za, nullptr) << "libzip 写侧打开失败：" << path.string();
    for (const ZipEntrySpec& e : entries) {
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

/// 手工 zip 小端追加（恶意样例构造——重复条目需容器层容忍，libzip 写
/// 侧不便编排同名条目，故手工字节）。
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

/// CRC-32（IEEE 802.3，zip 口径——测试侧独立表驱动实现）。
std::uint32_t crc32Of(const std::string& s)
{
    static std::uint32_t table[256];
    static const bool ready = [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        return true;
    }();
    (void)ready;
    std::uint32_t c = 0xFFFFFFFFu;
    for (const char ch : s) {
        c = table[(c ^ static_cast<std::uint8_t>(ch)) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/// 手工构造 STORED 形态 zip（条目名可重复——V13 的恶意样例载体）。
std::string handcraftZip(const std::vector<ZipEntrySpec>& entries)
{
    std::string out;
    struct CdRow {
        std::string name;
        std::uint32_t crc;
        std::uint32_t size;
        std::uint32_t offset;
    };
    std::vector<CdRow> cd;
    for (const ZipEntrySpec& e : entries) {
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        out.append("PK\x03\x04", 4);
        appendU16(out, 20);            // version needed
        appendU16(out, 0);             // flags
        appendU16(out, 0);             // method＝STORED
        appendU16(out, 0);             // time
        appendU16(out, 0);             // date
        appendU32(out, crc32Of(e.data));
        appendU32(out, static_cast<std::uint32_t>(e.data.size()));
        appendU32(out, static_cast<std::uint32_t>(e.data.size()));
        appendU16(out, static_cast<std::uint16_t>(e.name.size()));
        appendU16(out, 0);             // 扩展区
        out.append(e.name);
        out.append(e.data);
        cd.push_back(CdRow{e.name, crc32Of(e.data),
                           static_cast<std::uint32_t>(e.data.size()), offset});
    }
    const std::uint32_t cdOffset = static_cast<std::uint32_t>(out.size());
    for (const CdRow& r : cd) {
        out.append("PK\x01\x02", 4);
        appendU16(out, 20);
        appendU16(out, 20);
        appendU16(out, 0);
        appendU16(out, 0);
        appendU16(out, 0);
        appendU16(out, 0);
        appendU32(out, r.crc);
        appendU32(out, r.size);
        appendU32(out, r.size);
        appendU16(out, static_cast<std::uint16_t>(r.name.size()));
        appendU16(out, 0);
        appendU16(out, 0);
        appendU16(out, 0);
        appendU16(out, 0);
        appendU32(out, 0);
        appendU32(out, r.offset);
        out.append(r.name);
    }
    const std::uint32_t cdSize = static_cast<std::uint32_t>(out.size()) - cdOffset;
    out.append("PK\x05\x06", 4);
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, static_cast<std::uint16_t>(cd.size()));
    appendU16(out, static_cast<std::uint16_t>(cd.size()));
    appendU32(out, cdSize);
    appendU32(out, cdOffset);
    appendU16(out, 0);
    return out;
}

/// 标准负载集（镜像恒在集——HEAD/project.json/≥1 revisions＋对象文件）。
std::vector<std::pair<std::string, std::string>> standardPayload()
{
    return {
        {"payload/HEAD", std::string("rev-test-1\n")},
        {"payload/project.json", std::string("{\"projectId\":\"proj-test\"}")},
        {"payload/revisions/rev-test-1.json", std::string("{\"revision\":1}")},
        {"payload/objects/obj-a/data.bin", std::string("obj-a-payload-bytes")},
    };
}

/// 合法 .rwpack 构造（libzip 写侧；manifest/rwpack 自洽——totalDigest
/// 一致；返回写出的包路径）。
fs::path buildValidPack(const fs::path& file,
                        const std::vector<std::pair<std::string, std::string>>& payload,
                        std::int32_t method = ZIP_CM_DEFLATE)
{
    std::string totalDigestHex;
    const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
    std::uint64_t totalBytes = 0;
    for (const auto& kv : payload) {
        totalBytes += kv.second.size();
    }
    const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), totalBytes);
    std::vector<ZipEntrySpec> entries;
    entries.push_back(ZipEntrySpec{PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_DEFLATE});
    entries.push_back(ZipEntrySpec{PackFormat::kManifestEntryName, manifestJson, ZIP_CM_DEFLATE});
    for (const auto& kv : payload) {
        entries.push_back(ZipEntrySpec{kv.first, kv.second, method});
    }
    makeArchiveViaLibzip(file, entries);
    return file;
}

/// 目标零写入断言（acceptance 4 的结构性观测：目标目录从未创建、目标
/// 父目录无任何 .rwpack-import-* 残留——V22 的逐用例半边）。
void assertNoTarget(const fs::path& targetDir)
{
    EXPECT_FALSE(fs::exists(targetDir)) << "目标目录被创建（失败污染目标）";
    const fs::path parent = targetDir.parent_path();
    std::error_code ec;
    for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        EXPECT_TRUE(name.rfind(L".rwpack-import-", 0) != 0)
            << "导入临时区残留：" << it->path().string();
    }
}

/// 可编程取消令牌（IoCancelToken 测试实现——原子标志；一经真值不复位）。
class FlagToken final : public IoCancelToken {
public:
    void cancel() { m_flag.store(true, std::memory_order_relaxed); }
    bool isCancelled() const override
    {
        return m_flag.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> m_flag{false};
};

/// 用例自持临时目录（每用例隔离——CsvTest/JsonTest 同款形态）。
class IoPackTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()
                                              ->current_test_info();
        m_root = fs::temp_directory_path()
                 / ("ird-io-t06-" + std::string(info->name()));
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root, ec);
        ASSERT_FALSE(ec) << ec.message();
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    fs::path m_root;   ///< 用例根（包文件/目标父目录都在其下）
};

/// 临时区/原子写出用例夹具（同款隔离）。
class IoTempTest : public IoPackTest {
};
using IoAtomicTest = IoPackTest;

} // namespace

// =====================================================================
// 导出→导入主线 roundtrip（AT-20 主线的 io 侧——§7.2/§7.3 全链冒烟）
// =====================================================================

/// 验证：导出（fake 快照源）→包→导入（发布目标空位）→Verified；解包
/// payload 与快照逐字节一致；cleanup 后临时区消失（PM-05/AT-20）。
TEST_F(IoPackTest, ExportImportRoundtripRestoresPayloadBytewise)
{
    // 快照源＝标准负载（map 字典序即清单序——§7.2 enumerate 契约）。
    FakeSnapshotSource source;
    for (const auto& kv : standardPayload()) {
        source.files[kv.first] = kv.second;
    }

    const fs::path packFile = m_root / "proj.rwpack";
    auto exporter = sdurws::ird::io::makePackageExporter();
    sdurws::ird::io::PackageExportOptions options;
    options.targetFile = packFile;
    options.createdAtUtcIso8601 = "2026-09-17T08:00:00Z";   // IO-D11 固定时间戳
    auto exported = exporter->export_(source, options, nullptr, nullptr, {});
    ASSERT_TRUE(exported) << exported.error.detail;
    EXPECT_TRUE(exported.value.integritySelfCheckPassed) << "导出自检未过";
    EXPECT_EQ(exported.value.entryCount, source.files.size());
    EXPECT_TRUE(fs::exists(packFile)) << "包文件未就位";

    // 导入：发布目标为空位——发布归 project，io 只做 ⑧前预检（§7.7）。
    const fs::path targetDir = m_root / "restored";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions importOptions;
    importOptions.targetDir = targetDir;
    importOptions.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, importOptions, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;
    auto verified = importer->verifyThrough(session.value);
    ASSERT_TRUE(verified) << verified.error.detail;
    EXPECT_EQ(session.value.state(), PackageImportState::Verified);
    EXPECT_EQ(verified.value.manifestEntries, source.files.size());
    EXPECT_EQ(verified.value.verifiedEntries, source.files.size());

    // 逐字节还原断言（PM-05"解包逐字节还原"）。
    for (const auto& kv : source.files) {
        // 包内相对名 → payload 树路径（镜像布局——§7.1）。
        const std::string rel = kv.first.substr(std::strlen("payload/"));
        const std::string restored =
            readFileBytes(session.value.payloadRoot() / fs::path(rel));
        EXPECT_EQ(restored, kv.second) << kv.first;
    }

    // ⑨ 清理（project 发布后调用——此处直接验证幂等清理面）。
    ASSERT_TRUE(importer->cleanup(session.value));
    EXPECT_EQ(session.value.state(), PackageImportState::Cleaned);
    EXPECT_FALSE(fs::exists(session.value.stagingRoot())) << "临时区未清理";
}

// =====================================================================
// IO-V12：压缩炸弹（NFR-SEC-02/PM-05——acceptance 1）
// =====================================================================

/// 验证（IO-V12 比例半边）：高压缩比包（>100:1）在步骤④预检被
/// IO-SEC-BOMB-RATIO 中止；临时区清理；目标零写入；ledger 快照可观测。
TEST_F(IoPackTest, ZipBombRatioAbortsAndCleansStaging)
{
    // HEAD＝200 KiB 全零（DEFLATE 压至约百字节级）→ 比例远超 100:1。
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    payload[0].second = std::string(200 * 1024, '\0');
    const fs::path packFile = buildValidPack(m_root / "bomb-ratio.rwpack", payload);

    const fs::path targetDir = m_root / "target";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;

    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified) << "高压缩比包未被拒绝";
    EXPECT_EQ(verified.error.code, IoErrorCode::SecBombRatio)
        << "码面非 IO-SEC-BOMB-RATIO：" << verified.error.detail;
    // 观测点：会话终态 Failed＋目标零写入＋临时区清理。
    EXPECT_EQ(session.value.state(), PackageImportState::Failed);
    assertNoTarget(targetDir);
    EXPECT_FALSE(fs::exists(session.value.stagingRoot())) << "临时区未清理";
    // 观测点：ledger 快照（acceptance 1"ledger 快照观测"）——账本面可见
    // 且比例维限额为产品硬限（100:1——包通道禁用放宽）。
    bool ratioRow = false;
    for (const auto& dim : session.value.report().ledgerSnapshot.dimensions) {
        if (dim.first == BudgetDimension::ArchiveRatio) {
            ratioRow = true;
            EXPECT_EQ(dim.second.limit, 100u) << "比例限额非产品默认";
        }
    }
    EXPECT_TRUE(ratioRow) << "ledger 快照缺少 ArchiveRatio 维";
}

/// 验证（IO-V12 超展开量半边）：累计展开超收紧预算（1 MiB）在
/// IO-SEC-BUDGET-EXPAND 中止；近不可压数据（比例≈1）排除比例判据干扰。
TEST_F(IoPackTest, ZipBombOverExpandAbortsOnBudgetExpand)
{
    // 3×512 KiB 伪随机（STORED——比例 1:1，只让"累计展开"触限）。
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    payload.push_back({"payload/objects/obj-b/blob1.bin", pseudoRandomBytes(512 * 1024, 1)});
    payload.push_back({"payload/objects/obj-b/blob2.bin", pseudoRandomBytes(512 * 1024, 2)});
    payload.push_back({"payload/objects/obj-b/blob3.bin", pseudoRandomBytes(512 * 1024, 3)});
    const fs::path packFile =
        buildValidPack(m_root / "bomb-expand.rwpack", payload, ZIP_CM_STORE);

    const fs::path targetDir = m_root / "target";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    options.budget.tighten(BudgetDimension::ArchiveExpandedBytes, 1024ull * 1024ull);
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;

    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified) << "超展开量包未被拒绝";
    EXPECT_EQ(verified.error.code, IoErrorCode::SecBudgetExpand)
        << "码面非 IO-SEC-BUDGET-EXPAND：" << verified.error.detail;
    EXPECT_EQ(session.value.state(), PackageImportState::Failed);
    assertNoTarget(targetDir);
    EXPECT_FALSE(fs::exists(session.value.stagingRoot())) << "临时区未清理";
}

// =====================================================================
// IO-V13：重复条目（NFR-SEC-01——acceptance 1；步骤③预检、展开前零落盘）
// =====================================================================

/// 验证（IO-V13 同名×2）：同名条目在步骤③折叠键查重被
/// IO-PACK-DUPLICATE-ENTRY 整体拒绝；展开前零落盘（临时区未产生）。
TEST_F(IoPackTest, DuplicateEntriesRejectedBeforeExtraction)
{
    // 手工构造：payload/HEAD 出现两次（字节不同——"后写覆盖先写"歧义
    // 的载体）；manifest/rwpack 按合法集合自洽。
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    std::string totalDigestHex;
    const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
    const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), 0);
    const std::string zipBytes = handcraftZip({
        {PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_STORE},
        {PackFormat::kManifestEntryName, manifestJson, ZIP_CM_STORE},
        {"payload/HEAD", std::string("first\n"), ZIP_CM_STORE},
        {"payload/HEAD", std::string("second\n"), ZIP_CM_STORE},
        {"payload/project.json", payload[1].second, ZIP_CM_STORE},
        {"payload/revisions/rev-test-1.json", payload[2].second, ZIP_CM_STORE},
        {"payload/objects/obj-a/data.bin", payload[3].second, ZIP_CM_STORE},
    });
    const fs::path packFile = m_root / "dup.rwpack";
    {
        std::ofstream out(packFile, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out.write(zipBytes.data(), static_cast<std::streamsize>(zipBytes.size()));
    }

    const fs::path targetDir = m_root / "target";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;

    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified) << "重复条目包未被拒绝";
    EXPECT_EQ(verified.error.code, IoErrorCode::PackDuplicateEntry)
        << "码面非 IO-PACK-DUPLICATE-ENTRY：" << verified.error.detail;
    // 展开前零落盘（acceptance 1"展开前零落盘"）：临时区根下不得有
    // payload 树（预检失败发生在任何落盘之前）。
    std::error_code ec;
    EXPECT_FALSE(fs::exists(session.value.stagingRoot() / L"payload", ec))
        << "预检失败前已有落盘";
    EXPECT_EQ(session.value.state(), PackageImportState::Failed);
    assertNoTarget(targetDir);
}

/// 验证（IO-V13 仅大小写异条目对）：Windows 折叠键相同的两条目同样拒绝
/// （§4.3.3 P-4 表末行——落盘冲突预防）。
TEST_F(IoPackTest, CaseDifferingEntryPairRejectedAsDuplicate)
{
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    payload.push_back({"payload/objects/A", std::string("upper")});
    payload.push_back({"payload/objects/a", std::string("lower")});
    std::string totalDigestHex;
    const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
    const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), 0);
    std::vector<ZipEntrySpec> entries;
    entries.push_back(ZipEntrySpec{PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_STORE});
    entries.push_back(ZipEntrySpec{PackFormat::kManifestEntryName, manifestJson, ZIP_CM_STORE});
    for (const auto& kv : payload) {
        entries.push_back(ZipEntrySpec{kv.first, kv.second, ZIP_CM_STORE});
    }
    const fs::path packFile = m_root / "case-dup.rwpack";
    makeArchiveViaLibzip(packFile, entries);   // libzip 容忍仅大小写异名

    const fs::path targetDir = m_root / "target";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;
    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified) << "仅大小写异条目对未被拒绝";
    EXPECT_EQ(verified.error.code, IoErrorCode::PackDuplicateEntry)
        << "码面非 IO-PACK-DUPLICATE-ENTRY：" << verified.error.detail;
    assertNoTarget(targetDir);
}

// =====================================================================
// IO-V14：哈希不匹配（PM-05——acceptance 1；条目级定位＋整体拒绝）
// =====================================================================

/// 验证（IO-V14）：篡改一个 payload 字节（manifest 不变）→
/// IO-PACK-HASH-MISMATCH 且 params entry 精确定位被篡改条目；整体拒绝。
TEST_F(IoPackTest, TamperedPayloadHashMismatchLocatedPerEntry)
{
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    const fs::path packFile = m_root / "tampered.rwpack";
    // 先构造合法包（manifest 自洽），再用"篡改字节＋原 manifest"重建
    // 归档——manifest 与 totalDigest 一致、仅 payload 与声明不符＝
    // IO-PACK-HASH-MISMATCH 的正样本（而非 totalDigest 失配的负样本）。
    buildValidPack(packFile, payload);
    std::string totalDigestHex;
    const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
    const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), 0);
    std::string tampered = payload[3].second;
    tampered[0] = static_cast<char>(tampered[0] ^ 0xFF);   // 翻转首字节
    std::vector<ZipEntrySpec> entries;
    entries.push_back(ZipEntrySpec{PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_STORE});
    entries.push_back(ZipEntrySpec{PackFormat::kManifestEntryName, manifestJson, ZIP_CM_STORE});
    entries.push_back(ZipEntrySpec{payload[0].first, payload[0].second, ZIP_CM_STORE});
    entries.push_back(ZipEntrySpec{payload[1].first, payload[1].second, ZIP_CM_STORE});
    entries.push_back(ZipEntrySpec{payload[2].first, payload[2].second, ZIP_CM_STORE});
    entries.push_back(ZipEntrySpec{payload[3].first, tampered, ZIP_CM_STORE});
    makeArchiveViaLibzip(packFile, entries);   // 覆写为篡改版

    const fs::path targetDir = m_root / "target";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;
    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified) << "篡改包未被拒绝";
    EXPECT_EQ(verified.error.code, IoErrorCode::PackHashMismatch)
        << "码面非 IO-PACK-HASH-MISMATCH：" << verified.error.detail;
    // 条目级定位（acceptance 1"条目级定位"）。
    EXPECT_EQ(paramOf(verified.error, "entry"), payload[3].first);
    EXPECT_EQ(session.value.state(), PackageImportState::Failed);
    assertNoTarget(targetDir);
    EXPECT_FALSE(fs::exists(session.value.stagingRoot())) << "临时区未清理";
}

// =====================================================================
// IO-V19：导入取消（PM-05/UX-03——acceptance 2；各阶段取消令牌注入）
// =====================================================================

/// 验证（IO-V19 展开阶段取消）：取消令牌在第 3 个条目检查点置位→中止、
/// 临时区清理、目标零写入、无任何诊断（取消是状态非错误——UX-03）。
TEST_F(IoPackTest, ImportCancelMidExtractCleansAndEmitsNoDiagnostic)
{
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    for (int i = 0; i < 5; ++i) {
        payload.push_back({"payload/objects/obj-a/f" + std::to_string(i) + ".bin",
                           pseudoRandomBytes(4096, static_cast<std::uint32_t>(i))});
    }
    const fs::path packFile = buildValidPack(m_root / "cancel.rwpack", payload);

    const fs::path targetDir = m_root / "target";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    FlagToken cancel;
    // 进度回调内置位取消（第 3 个条目提取后）——"各阶段取消令牌注入"
    // 的展开阶段样例；检查点间隔＝每条目（§7.3 步注）。
    bool cancelled = false;
    sdurws::ird::io::IoProgressCallback progress = [&](const sdurws::ird::io::IoProgress& p) {
        if (!cancelled && std::strcmp(p.stage, "extract") == 0 && p.done >= 3) {
            cancel.cancel();
            cancelled = true;
        }
    };
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;
    auto verified = importer->verifyThrough(session.value, &cancel, progress);
    ASSERT_FALSE(verified) << "取消未被响应";
    EXPECT_EQ(verified.error.code, IoErrorCode::Cancelled) << "取消返回码非 IO-CANCELLED";
    EXPECT_EQ(session.value.state(), PackageImportState::Canceled);
    // 无 Canceled 诊断（UX-03——状态非错误；acceptance 2 观测点）。
    EXPECT_TRUE(session.value.report().diagnostics.empty())
        << "取消路径产生了诊断（违反 UX-03）";
    // 取消即清理临时区＋目标零写入（PM-05）。
    EXPECT_FALSE(fs::exists(session.value.stagingRoot())) << "取消后临时区未清理";
    assertNoTarget(targetDir);
}

// =====================================================================
// IO-V29：引用完整性（PM-05/SEL-02 文件层——acceptance 3）
// =====================================================================

/// 验证（IO-V29 manifest 引用不存在条目）：manifest 声明归档中不存在的
/// payload 条目→IO-PACK-REF-INCOMPLETE 且定位到缺失条目（断裂链定位）。
TEST_F(IoPackTest, ManifestReferencingMissingArchiveEntryRejected)
{
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    // manifest 多声明一个幽灵条目（有真实形状的摘要/大小，但归档无此
    // 条目）——manifest 与 totalDigest 自洽（切断其他判据）。
    payload.push_back({"payload/objects/ghost.bin", std::string("ghost-bytes")});
    std::string totalDigestHex;
    const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
    const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), 0);
    // 归档只装物理存在的前 4 条（幽灵不打包）。
    std::vector<ZipEntrySpec> entries;
    entries.push_back(ZipEntrySpec{PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_STORE});
    entries.push_back(ZipEntrySpec{PackFormat::kManifestEntryName, manifestJson, ZIP_CM_STORE});
    for (std::size_t i = 0; i < 4; ++i) {
        entries.push_back(ZipEntrySpec{payload[i].first, payload[i].second, ZIP_CM_STORE});
    }
    const fs::path packFile = m_root / "ghost.rwpack";
    makeArchiveViaLibzip(packFile, entries);

    const fs::path targetDir = m_root / "target";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;
    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified) << "幽灵引用未被拒绝";
    EXPECT_EQ(verified.error.code, IoErrorCode::PackRefIncomplete)
        << "码面非 IO-PACK-REF-INCOMPLETE：" << verified.error.detail;
    EXPECT_EQ(paramOf(verified.error, "entry"), "payload/objects/ghost.bin")
        << "断裂链未定位到缺失条目";
    assertNoTarget(targetDir);
}

/// 验证（IO-V29 镜像缺必备文件）：缺少 payload/HEAD 的包→
/// IO-PACK-REF-INCOMPLETE 且定位到缺失的必备条目（§7.1 恒在集）。
TEST_F(IoPackTest, MissingRequiredMirrorEntryRejected)
{
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    payload.erase(payload.begin());   // 去掉 payload/HEAD
    const fs::path packFile = buildValidPack(m_root / "no-head.rwpack", payload);

    const fs::path targetDir = m_root / "target";
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;
    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified) << "缺必备条目包未被拒绝";
    EXPECT_EQ(verified.error.code, IoErrorCode::PackRefIncomplete)
        << "码面非 IO-PACK-REF-INCOMPLETE：" << verified.error.detail;
    EXPECT_EQ(paramOf(verified.error, "entry"), PackFormat::kRequiredHead)
        << "未定位到缺失的必备条目";
    assertNoTarget(targetDir);
}

// =====================================================================
// 临时区单元契约（§7.5/§9.10——IoTemp 组）
// =====================================================================

/// 验证：create 产生会话根＋io-session.json 标记（§7.5 标记行）；cleanup
/// 删除会话根且幂等（二次 cleanup 成功——§9.10 原文契约）。
TEST_F(IoTempTest, CreateMarksAndCleanupIsIdempotent)
{
    auto manager = sdurws::ird::io::makeTempAreaManager();
    auto session = manager->create(TempAreaSpec{TempAreaRole::PackImport, m_root, {}}, nullptr);
    ASSERT_TRUE(session) << session.error.detail;
    EXPECT_TRUE(session.value.isActive());
    EXPECT_FALSE(session.value.rootPath().empty());
    // 标记文件（崩溃残留识别依据——§7.5）。
    std::error_code ec;
    EXPECT_TRUE(fs::exists(session.value.rootPath() / L"io-session.json", ec));
    // 可用空间探测（等价增补——V20 注入面的真实半边）。
    auto avail = manager->availableBytes(session.value);
    ASSERT_TRUE(avail) << avail.error.detail;
    EXPECT_GT(avail.value, 0u);
    // 清理＋幂等。
    ASSERT_TRUE(manager->cleanup(session.value));
    EXPECT_FALSE(session.value.isActive());
    EXPECT_FALSE(fs::exists(session.value.rootPath())) << "会话根未删除";
    EXPECT_TRUE(manager->cleanup(session.value)) << "二次 cleanup 非幂等成功";
}

/// 验证：等价键互斥——同 baseDir 同前缀的第二个会话被拒（§7.5 互斥行/
/// §4.2.4；cleanup 后可再建）；不同角色互不干扰。
TEST_F(IoTempTest, EquivalentKeyMutexAllowsSingleSession)
{
    auto manager = sdurws::ird::io::makeTempAreaManager();
    TempAreaSpec spec{TempAreaRole::PackImport, m_root, {}};
    auto first = manager->create(spec, nullptr);
    ASSERT_TRUE(first) << first.error.detail;
    auto second = manager->create(spec, nullptr);
    ASSERT_FALSE(second) << "同前缀第二会话未被互斥";
    EXPECT_EQ(second.error.code, IoErrorCode::ResLockConflict)
        << "码面非 IO-RES-LOCK-CONFLICT";
    // 释放后可再建（租约归还）。
    ASSERT_TRUE(manager->cleanup(first.value));
    auto third = manager->create(spec, nullptr);
    ASSERT_TRUE(third) << third.error.detail;
    ASSERT_TRUE(manager->cleanup(third.value));
    // 不同角色（PackExport）互不干扰（前缀不同→等价键不同）。
    auto exportArea =
        manager->create(TempAreaSpec{TempAreaRole::PackExport, m_root, "proj"}, nullptr);
    ASSERT_TRUE(exportArea) << exportArea.error.detail;
    EXPECT_TRUE(exportArea.value.rootPath().filename().wstring().rfind(L".proj.", 0) == 0)
        << "导出前缀命名不符 §7.5";
    ASSERT_TRUE(manager->cleanup(exportArea.value));
}

/// 验证：崩溃残留回收——含 io-session.json 标记的同前缀目录在下一次
/// create 时被回收（§7.5"崩溃残留"行）；无标记目录绝不触碰。
TEST_F(IoTempTest, CrashResidueWithMarkerRecoveredWithoutMarkerUntouched)
{
    // 残留 A：本 io 家族形态（含标记）。
    const fs::path residueA = m_root / L".rwpack-import-deadbeef";
    std::error_code ec;
    fs::create_directories(residueA / L"payload" / L"objects", ec);
    { std::ofstream marker(residueA / L"io-session.json", std::ios::binary); marker << "{\"pid\":1}"; }
    // 残留 B：同名前缀但无标记（用户目录形态——绝不可触碰）。
    const fs::path residueB = m_root / L".rwpack-import-cafebabe";
    fs::create_directories(residueB, ec);
    { std::ofstream user(residueB / L"user-file.txt", std::ios::binary); user << "keep"; }

    auto manager = sdurws::ird::io::makeTempAreaManager();
    auto session = manager->create(TempAreaSpec{TempAreaRole::PackImport, m_root, {}}, nullptr);
    ASSERT_TRUE(session) << session.error.detail;
    EXPECT_FALSE(fs::exists(residueA)) << "含标记残留未被回收";
    EXPECT_TRUE(fs::exists(residueB)) << "无标记目录被误删（违反 §7.5）";
    EXPECT_TRUE(fs::exists(residueB / L"user-file.txt", ec));
    ASSERT_TRUE(manager->cleanup(session.value));
}

// =====================================================================
// 原子写出单元契约（§4.6/§9.10——IoAtomic 组）
// =====================================================================

/// 验证：NeverOverwrite 命中既有目标→IO-PACK-TARGET-EXISTS；OverwriteAtomic
/// 的 commit 原子替换（§4.6 ReplacePolicy 行；MDL-20 先前输出被顶替）。
TEST_F(IoAtomicTest, PreparePolicyAndCommitReplace)
{
    auto writer = sdurws::ird::io::makeAtomicFileWriter();
    const fs::path target = m_root / "out.txt";
    // 预置先前输出（MDL-20"先前输出"）。
    { std::ofstream old(target, std::ios::binary); old << "old-bytes"; }
    // NeverOverwrite：拒绝且目标不变。
    auto rejected = writer->prepare(target, ReplacePolicy::NeverOverwrite);
    ASSERT_FALSE(rejected) << "NeverOverwrite 未拒绝既有目标";
    EXPECT_EQ(rejected.error.code, IoErrorCode::PackTargetExists);
    EXPECT_EQ(readFileBytes(target), "old-bytes");
    // OverwriteAtomic：commit 原子替换。
    auto t = writer->prepare(target, ReplacePolicy::OverwriteAtomic);
    ASSERT_TRUE(t) << t.error.detail;
    EXPECT_NE(t.value.tempPath(), target) << "暂存位不得与目标同路径";
    ASSERT_TRUE(t.value.write("new-bytes"));
    ASSERT_TRUE(writer->commit(t.value));
    EXPECT_FALSE(t.value.isActive());
    EXPECT_EQ(readFileBytes(target), "new-bytes");
    std::error_code ec;
    for (fs::directory_iterator it(m_root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        EXPECT_TRUE(name.size() < 4 || name.rfind(L".tmp") != name.size() - 4)
            << ".tmp 残留：" << it->path().string();
    }
}

/// 验证：abort 清理暂存、目标零接触（§4.6 失败分段——V23 的结构依据）。
TEST_F(IoAtomicTest, AbortLeavesTargetUntouched)
{
    auto writer = sdurws::ird::io::makeAtomicFileWriter();
    const fs::path target = m_root / "keep.txt";
    { std::ofstream old(target, std::ios::binary); old << "previous-complete"; }
    auto t = writer->prepare(target, ReplacePolicy::OverwriteAtomic);
    ASSERT_TRUE(t) << t.error.detail;
    ASSERT_TRUE(t.value.write("half-written"));
    ASSERT_TRUE(writer->abort(t.value));
    EXPECT_EQ(readFileBytes(target), "previous-complete") << "abort 触碰了目标";
    EXPECT_FALSE(fs::exists(t.value.tempPath())) << "abort 未清理暂存";
    // 幂等 abort（失败路径统一调用形态）。
    EXPECT_TRUE(writer->abort(t.value));
}
