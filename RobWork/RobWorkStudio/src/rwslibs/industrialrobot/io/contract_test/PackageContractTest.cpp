/**
 * @file   PackageContractTest.cpp
 * @brief  包导入导出的契约与故障注入用例组（IoPackContract）——经
 *         PackageIoFacilities 的 fake 适配层注入文件系统故障（§11.1 故
 *         障注入原则——不依赖真实磁盘满/真实删除失败）：V18 导出并发写
 *         （稳定版本重读）、V20 磁盘满（所需/可用比较型）、V21 清理失败
 *         （残留清单＋幂等重试）、V23 导出仅暂存（目标不存在或先前完整
 *         版本）、V31 后台不阻塞（io 为可取消同步库调用）、V32 已提交
 *         对暂存（清单外文件不入包）。
 *
 * 设计依据：
 *   - units/io.md §7.2（导出协议与"稳定版本语义"场景表）、§7.4（威胁矩
 *     阵 DiskFull/CleanupFailed 行）、§7.6（清理状态图）、§11.1（故障注
 *     入原则——fake 适配层承载）、§11.2（IO-V18/V20/V21/V23/V31/V32 行
 *     ——本文件即其执行载体）、§9.13（io 不创建线程——V31 机制前提）
 *   - 需求 PM-05、AT-20（"包导出/导入主线、取消不留半成品"）、UX-03
 *   - 任务契约 tasks/foundation/IO-T06.json acceptance 2/3（V18~V23/
 *     V31/V32）与 acceptance 4（失败不留目标目录总断言的契约侧样例）
 *
 * 断言纪律（AGENTS.md §2.7）：故障注入点全部经显式 fake（确定性触发，
 * 不依赖时序/真实环境故障）；每个用例中文注明验证的需求/验收条目。
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

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
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
using sdurws::ird::io::PackFileEntry;
using sdurws::ird::io::PackageExportOptions;
using sdurws::ird::io::PackageImportOptions;
using sdurws::ird::io::PackageImportState;
using sdurws::ird::io::ReplacePolicy;
using sdurws::ird::io::TempAreaRole;
using sdurws::ird::io::TempAreaSession;
using sdurws::ird::io::TempAreaSpec;

namespace {

// =====================================================================
// 测试助手（PackageTest.cpp 同款口径——自持不跨文件共享）
// =====================================================================

std::string paramOf(const IoError& e, const char* key)
{
    for (const auto& kv : e.params) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return {};
}

Digest256 sha256Of(const std::string& bytes)
{
    sdurws::ird::core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

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

std::string readFileBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << path.string();
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

/// JSON 值/成员本地构造助手（Json.hpp 公共结构——同 PackageTest 口径）。
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
    EXPECT_TRUE(bytes);
    auto digest = sdurws::ird::io::digestCanonicalJson(doc, sdurws::ird::io::JsonWriteOptions{});
    EXPECT_TRUE(digest);
    if (outDigestHex != nullptr) {
        *outDigestHex = digestHex(digest.value);
    }
    return bytes.value;
}

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
    EXPECT_TRUE(bytes);
    return bytes.value;
}

struct ZipEntrySpec {
    std::string name;
    std::string data;
    std::int32_t method;
};

void makeArchiveViaLibzip(const fs::path& path, const std::vector<ZipEntrySpec>& entries)
{
    zip_t* za = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
    ASSERT_NE(za, nullptr);
    for (const ZipEntrySpec& e : entries) {
        void* copy = std::malloc(e.data.size());
        ASSERT_NE(copy, nullptr);
        std::memcpy(copy, e.data.data(), e.data.size());
        zip_source_t* src = zip_source_buffer(za, copy, e.data.size(), 1);
        ASSERT_NE(src, nullptr);
        const zip_int64_t idx = zip_file_add(za, e.name.c_str(), src, ZIP_FL_ENC_UTF_8);
        ASSERT_GE(idx, 0);
        ASSERT_EQ(zip_set_file_compression(za, static_cast<zip_uint64_t>(idx), e.method, 0), 0);
    }
    ASSERT_EQ(zip_close(za), 0);
}

/// 标准负载集（镜像恒在集——同 PackageTest）。
std::vector<std::pair<std::string, std::string>> standardPayload()
{
    return {
        {"payload/HEAD", std::string("rev-test-1\n")},
        {"payload/project.json", std::string("{\"projectId\":\"proj-test\"}")},
        {"payload/revisions/rev-test-1.json", std::string("{\"revision\":1}")},
        {"payload/objects/obj-a/data.bin", std::string("obj-a-payload-bytes")},
    };
}

fs::path buildValidPack(const fs::path& file,
                        const std::vector<std::pair<std::string, std::string>>& payload,
                        std::int32_t payloadMethod = ZIP_CM_STORE)
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
        // payloadMethod：V22 比例炸弹样例需要 DEFLATE（全零高压缩）——
        // 其余样例保持 STORE（压缩方法与哈希/展开校验语义正交）。
        entries.push_back(ZipEntrySpec{kv.first, kv.second, payloadMethod});
    }
    makeArchiveViaLibzip(file, entries);
    return file;
}

// =====================================================================
// V22 聚合负断言所需的本地构造助手（PackageTest.cpp 同款口径——自持
// 不跨文件共享：伪随机字节、手工 zip 小端字节流与 CRC-32）
// =====================================================================

/// 伪随机不可压缩字节（LCG——超展开量样例的近 STORED 数据源；确定性
/// 生成——同参数同字节，失败可复现）。
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

/// 手工字节 zip（STORED 条目＋中央目录＋EOCD——V13 同名条目样例载体）。
std::string handcraftZip(const std::vector<ZipEntrySpec>& entries)
{
    struct CdRow { std::string name; std::uint32_t crc; std::uint32_t size; std::uint32_t offset; };
    std::string out;
    std::vector<CdRow> cd;
    for (const ZipEntrySpec& e : entries) {
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        out.append("PK\x03\x04", 4);
        appendU16(out, 20);            // 版本
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

// =====================================================================
// 故障注入替身（§11.1 fake 适配层——经 PackageIoFacilities 注入）
// =====================================================================

/// 可编程快照源（V18/V32 载体）：读取计数＋首读失败注入（稳定版本语义
/// 的极窄窗口模拟）＋清单外"磁盘"文件（存在≠已提交）。
class FakeSnapshotSource final : public sdurws::ird::io::ISnapshotFileSource {
public:
    std::map<std::string, std::string> files;         ///< 快照闭包（已提交面）
    std::map<std::string, std::string> offSnapshot;   ///< 快照外落盘面（V32）
    std::map<std::string, int> failFirstNReads;       ///< path→前 N 次读失败（V18）
    std::map<std::string, int> readCounts;            ///< 观测面：每 path 读取次数

    IoResult<std::vector<PackFileEntry>> enumerate() const override
    {
        IoResult<std::vector<PackFileEntry>> out;
        for (const auto& kv : files) {   // 快照外文件绝不出现在清单（V32）
            PackFileEntry e;
            e.path = kv.first;
            e.size = kv.second.size();
            out.value.push_back(std::move(e));
        }
        return out;
    }

    IoResult<IoString> read(const IoString& packPath) override
    {
        ++readCounts[packPath];
        auto failIt = failFirstNReads.find(packPath);
        if (failIt != failFirstNReads.end() && failIt->second > 0) {
            --failIt->second;
            IoResult<IoString> out;
            out.error.code = IoErrorCode::ResChanged;
            out.error.detail = "fake：单文件读取期间源被替换（§7.2 极窄窗口注入）";
            return out;
        }
        auto it = files.find(packPath);
        if (it == files.end()) {
            IoResult<IoString> out;
            out.error.code = IoErrorCode::ResNotFound;
            out.error.detail = "fake：快照清单外路径";
            return out;
        }
        IoResult<IoString> out;
        out.value = it->second;
        return out;
    }
};

/// 故障注入临时区管理器（V20/V21 载体）：真实实现为底，覆写
/// availableBytes（固定可用量）与 cleanup（首次失败＋残留清单）。
class FaultTempAreaManager final : public sdurws::ird::io::ITempAreaManager {
public:
    explicit FaultTempAreaManager(std::shared_ptr<sdurws::ird::io::ITempAreaManager> real)
        : m_real(std::move(real))
    {
    }

    IoResult<TempAreaSession> create(const TempAreaSpec& spec, IoCancelToken* cancel) override
    {
        return m_real->create(spec, cancel);
    }

    IoResult<void> cleanup(TempAreaSession& session) override
    {
        if (failCleanupTimes > 0) {
            --failCleanupTimes;
            IoResult<void> out;
            out.error.code = IoErrorCode::PackCleanupFailed;
            // 残留清单（脱敏 display 形态——同 TempArea 真实实现的 params
            // 键型；值不含 \\?\ 原生前缀——V21 脱敏断言的对象）。
            out.error.params.emplace_back("residual0", "D:/tmp/.rwpack-import-aa/one");
            out.error.params.emplace_back("residual1", "D:/tmp/.rwpack-import-aa/two");
            out.error.detail = "fake：删除失败注入（§7.6 CleanupFailed）";
            return out;
        }
        return m_real->cleanup(session);
    }

    IoResult<std::uint64_t> availableBytes(const TempAreaSession& session) const override
    {
        if (availableOverride != nullptr) {
            IoResult<std::uint64_t> out;
            out.value = *availableOverride;   // 固定注入值（V20 的确定性可用量）
            return out;
        }
        return m_real->availableBytes(session);
    }

    int failCleanupTimes = 0;                       ///< cleanup 失败次数注入
    std::shared_ptr<std::uint64_t> availableOverride;   ///< 可用空间固定注入

private:
    std::shared_ptr<sdurws::ird::io::ITempAreaManager> m_real;
};

/// 故障注入原子写出器（V23 载体）：真实实现为底，commit 失败注入。
class FaultAtomicWriter final : public sdurws::ird::io::IAtomicFileWriter {
public:
    explicit FaultAtomicWriter(std::shared_ptr<sdurws::ird::io::IAtomicFileWriter> real)
        : m_real(std::move(real))
    {
    }

    IoResult<AtomicTarget> prepare(const fs::path& target, ReplacePolicy policy) override
    {
        return m_real->prepare(target, policy);
    }

    IoResult<void> commit(AtomicTarget& target) override
    {
        if (failCommit) {
            IoResult<void> out;
            out.error.code = IoErrorCode::ResAccessDenied;
            out.error.detail = "fake：替换失败注入（§7.2⑥——目标不得被触碰）";
            return out;
        }
        return m_real->commit(target);
    }

    IoResult<void> abort(AtomicTarget& target) override
    {
        return m_real->abort(target);
    }

    bool failCommit = false;

private:
    std::shared_ptr<sdurws::ird::io::IAtomicFileWriter> m_real;
};

/// 用例自持临时目录（同 PackageTest 形态）。
class IoPackContractTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = fs::temp_directory_path()
                 / ("ird-io-t06-ct-" + std::string(info->name()));
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

    /// 目标零写入断言（acceptance 4——目标从未创建/写入）。
    void expectNoTarget(const fs::path& targetDir)
    {
        EXPECT_FALSE(fs::exists(targetDir)) << "目标目录被创建（失败污染目标）";
    }

    fs::path m_root;
};

} // namespace

// =====================================================================
// IO-V18：导出并发写（PM-05/D-17——acceptance 2；稳定版本重读）
// =====================================================================

/// 验证（IO-V18）：导出中某 drafts 键首读失败（极窄窗口）→ io 按快照
/// 重读一次→产出包自洽且内容为完整稳定版本（无混合版本）；重读计数＝2。
TEST_F(IoPackContractTest, ExportConcurrentWriteRereadsStableVersion)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V18 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20"});
    // 快照面 v2（完整稳定版本）；v1 只存在于注释语义中——注入方式＝首读
    // 失败（源替换窗口），重读返回稳定 v2（§7.2 场景表"同 key 重读返回
    // 稳定版本"的 source 语义）。
    FakeSnapshotSource source;
    source.files = {
        {"payload/HEAD", std::string("rev-2\n")},
        {"payload/project.json", std::string("{\"projectId\":\"p\"}")},
        {"payload/revisions/rev-2.json", std::string("{\"revision\":2}")},
        {"payload/objects/obj-a/data.bin", std::string("v2-object")},
        {"payload/drafts/d.json", std::string("{\"ver\":2}")},
    };
    source.failFirstNReads["payload/drafts/d.json"] = 1;   // 首读失败→重读

    const fs::path packFile = m_root / "out.rwpack";
    auto exporter = sdurws::ird::io::makePackageExporter();
    PackageExportOptions options;
    options.targetFile = packFile;
    options.createdAtUtcIso8601 = "2026-09-17T08:00:00Z";
    auto exported = exporter->export_(source, options, nullptr, nullptr, {});
    ASSERT_TRUE(exported) << exported.error.detail;
    // source 语义观测：恰重读一次（2 次读取——§7.2"按快照重读一次"）。
    EXPECT_EQ(source.readCounts["payload/drafts/d.json"], 2)
        << "稳定版本重读次数不符";

    // 包自洽断言：导入全绿（哈希全量复算通过＝无混合版本的结构性证据
    // ——manifest 摘要来自实际写入字节，混合版本必然哈希失配）。
    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions importOptions;
    importOptions.targetDir = m_root / "restored";
    importOptions.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, importOptions, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;
    auto verified = importer->verifyThrough(session.value);
    ASSERT_TRUE(verified) << verified.error.detail;
    EXPECT_EQ(verified.value.verifiedEntries, verified.value.manifestEntries);
    // 包内 drafts 内容＝稳定版本 v2（读侧独立取证——openZipChannel 直读）。
    auto channel = sdurws::ird::io::openZipChannel(packFile);
    ASSERT_TRUE(channel);
    auto drafts = channel.value->readEntryBytes("payload/drafts/d.json", nullptr,
                                                sdurws::ird::io::BudgetScopeId{}, nullptr);
    ASSERT_TRUE(drafts) << drafts.error.detail;
    EXPECT_EQ(drafts.value, std::string("{\"ver\":2}"));
}

/// 验证（IO-V18 失败半边）：重读仍失败→导出失败清理，目标零写入
/// （"仍失败→导出失败清理，不产出混合版本包"——§7.2 场景表）。
TEST_F(IoPackContractTest, ExportConcurrentWritePersistentFailureLeavesNoTarget)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V18 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20"});
    FakeSnapshotSource source;
    source.files = {
        {"payload/HEAD", std::string("rev-1\n")},
        {"payload/project.json", std::string("{}")},
        {"payload/revisions/rev-1.json", std::string("{}")},
        {"payload/objects/a", std::string("x")},
    };
    source.failFirstNReads["payload/objects/a"] = 99;   // 持续失败——无稳定版本

    const fs::path packFile = m_root / "out.rwpack";
    auto exporter = sdurws::ird::io::makePackageExporter();
    PackageExportOptions options;
    options.targetFile = packFile;
    auto exported = exporter->export_(source, options, nullptr, nullptr, {});
    ASSERT_FALSE(exported) << "持续读失败未被上报";
    EXPECT_FALSE(fs::exists(packFile)) << "失败导出留下了包文件";
    // 导出临时区不残留（前缀 `.<name>.`——§7.5 PackExport 命名）。
    std::error_code ec;
    for (fs::directory_iterator it(m_root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        EXPECT_TRUE(name.rfind(L".out.", 0) != 0) << "导出临时区残留：" << it->path();
    }
}

// =====================================================================
// IO-V20：磁盘满（PM-05——acceptance 2；所需/可用比较型＋清理成功）
// =====================================================================

/// 验证（IO-V20）：fake TempAreaManager 注入"可用 10 字节"→
/// IO-PACK-DISK-FULL（required/available 比较型三要素）；清理成功（会话
/// Failed 而非 CleanupFailed）；目标零写入。
TEST_F(IoPackContractTest, DiskFullReportsRequiredAvailableAndCleansUp)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V20 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20"});
    const fs::path packFile = buildValidPack(m_root / "pack.rwpack", standardPayload());
    const fs::path targetDir = m_root / "target";

    auto faults = std::make_shared<FaultTempAreaManager>(sdurws::ird::io::makeTempAreaManager());
    faults->availableOverride = std::make_shared<std::uint64_t>(10);   // 10 字节可用

    sdurws::ird::io::PackageIoFacilities facilities;
    facilities.tempAreas = faults;
    auto importer = sdurws::ird::io::makePackageImporter(facilities);
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;

    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified) << "磁盘满未被检出";
    EXPECT_EQ(verified.error.code, IoErrorCode::PackDiskFull)
        << "码面非 IO-PACK-DISK-FULL：" << verified.error.detail;
    // 所需/可用比较型（§7.4"拒绝＋建议（所需/可用）"）。
    const std::string required = paramOf(verified.error, "required");
    const std::string available = paramOf(verified.error, "available");
    EXPECT_EQ(available, "10") << "可用要素缺失/不符";
    EXPECT_FALSE(required.empty()) << "所需要素缺失";
    EXPECT_GT(std::strtoull(required.c_str(), nullptr, 10), 10ull)
        << "所需应大于注入的可用量";
    // 清理成功（acceptance 2"清理成功"——终态 Failed 非 CleanupFailed）。
    EXPECT_EQ(session.value.state(), PackageImportState::Failed);
    EXPECT_FALSE(fs::exists(session.value.stagingRoot())) << "临时区未清理";
    expectNoTarget(targetDir);
}

// =====================================================================
// IO-V21：清理失败（PM-05——acceptance 2；残留清单脱敏＋二次清理幂等）
// =====================================================================

/// 验证（IO-V21）：哈希不符包导入＋fake 首次清理失败→返回
/// IO-PACK-CLEANUP-FAILED（残留清单脱敏——无 \\?\ 前缀）＋会话
/// CleanupFailed；二次 cleanup 幂等可成功→Cleaned；目标零写入。
TEST_F(IoPackContractTest, CleanupFailedListsResiduesAndRetrySucceeds)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V21 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20"});
    // 篡改 payload 的包（触发导入失败→自动清理→注入的清理失败）。
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    const fs::path packFile = buildValidPack(m_root / "bad.rwpack", payload);
    {
        // 原地篡改：重建归档——data.bin 翻转一字节、manifest 不变。
        std::string tampered = payload[3].second;
        tampered[0] = static_cast<char>(tampered[0] ^ 0xFF);
        std::string totalDigestHex;
        const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
        const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), 0);
        std::vector<ZipEntrySpec> entries;
        entries.push_back(ZipEntrySpec{PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_STORE});
        entries.push_back(ZipEntrySpec{PackFormat::kManifestEntryName, manifestJson, ZIP_CM_STORE});
        entries.push_back(ZipEntrySpec{payload[0].first, payload[0].second, ZIP_CM_STORE});
        entries.push_back(ZipEntrySpec{payload[1].first, payload[1].second, ZIP_CM_STORE});
        entries.push_back(ZipEntrySpec{payload[2].first, payload[2].second, ZIP_CM_STORE});
        entries.push_back(ZipEntrySpec{payload[3].first, tampered, ZIP_CM_STORE});
        makeArchiveViaLibzip(packFile, entries);
    }

    const fs::path targetDir = m_root / "target";
    auto faults = std::make_shared<FaultTempAreaManager>(sdurws::ird::io::makeTempAreaManager());
    faults->failCleanupTimes = 1;   // 首次清理失败（第 2 次删除调用失败语义）

    sdurws::ird::io::PackageIoFacilities facilities;
    facilities.tempAreas = faults;
    auto importer = sdurws::ird::io::makePackageImporter(facilities);
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();
    auto session = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(session) << session.error.detail;

    auto verified = importer->verifyThrough(session.value);
    ASSERT_FALSE(verified);
    EXPECT_EQ(verified.error.code, IoErrorCode::PackCleanupFailed)
        << "码面非 IO-PACK-CLEANUP-FAILED：" << verified.error.detail;
    // 残留清单脱敏断言（acceptance 2"残留路径清单脱敏"——display 形态，
    // 无 \\?\ 扩展前缀）。
    const std::string residue0 = paramOf(verified.error, "residual0");
    EXPECT_FALSE(residue0.empty()) << "残留清单缺失";
    EXPECT_EQ(residue0.find("\\\\?\\"), std::string::npos) << "残留路径未脱敏（含 \\?\ 前缀）";
    EXPECT_EQ(session.value.state(), PackageImportState::CleanupFailed);
    // 二次 cleanup 幂等可成功（§7.6 CleanupFailed 可重试）。
    auto retry = importer->cleanup(session.value);
    ASSERT_TRUE(retry) << retry.error.detail;
    EXPECT_EQ(session.value.state(), PackageImportState::Cleaned);
    EXPECT_FALSE(fs::exists(session.value.stagingRoot()));
    expectNoTarget(targetDir);
}

// =====================================================================
// IO-V23：导出仅暂存（PM-05/AT-20——acceptance 2；目标不存在或先前完整版本）
// =====================================================================

/// 验证（IO-V23 替换失败半边）：commit 注入失败→导出失败且目标文件不
/// 存在（原子替换未发生——暂存面不外泄）。
TEST_F(IoPackContractTest, ExportCommitFailureLeavesNoTargetFile)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V23 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-05", "MDL-20"},
                  std::vector<std::string>{"AT-20"});
    FakeSnapshotSource source;
    for (const auto& kv : standardPayload()) {
        source.files[kv.first] = kv.second;
    }
    const fs::path packFile = m_root / "out.rwpack";

    auto faults = std::make_shared<FaultAtomicWriter>(sdurws::ird::io::makeAtomicFileWriter());
    faults->failCommit = true;
    sdurws::ird::io::PackageIoFacilities facilities;
    facilities.atomicFiles = faults;
    auto exporter = sdurws::ird::io::makePackageExporter(facilities);
    PackageExportOptions options;
    options.targetFile = packFile;
    auto exported = exporter->export_(source, options, nullptr, nullptr, {});
    ASSERT_FALSE(exported) << "commit 失败未被上报";
    EXPECT_FALSE(fs::exists(packFile)) << "失败导出留下了目标文件（V23 违例）";
}

/// 验证（IO-V23 先前版本半边）：目标已有先前完整包＋源持续读失败→导出
/// 失败，目标内容为先前完整版本逐字节保留（MDL-20"导出失败恢复先前
/// 输出"）。
TEST_F(IoPackContractTest, ExportFailurePreservesPreviousCompletePack)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V23 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-05", "MDL-20"},
                  std::vector<std::string>{"AT-20"});
    // 先前完整版本：一次成功导出的包。
    FakeSnapshotSource good;
    for (const auto& kv : standardPayload()) {
        good.files[kv.first] = kv.second;
    }
    const fs::path packFile = m_root / "out.rwpack";
    auto exporter = sdurws::ird::io::makePackageExporter();
    PackageExportOptions options;
    options.targetFile = packFile;
    options.createdAtUtcIso8601 = "2026-09-17T07:00:00Z";
    ASSERT_TRUE(exporter->export_(good, options, nullptr, nullptr, {}));
    const std::string previousBytes = readFileBytes(packFile);
    ASSERT_FALSE(previousBytes.empty());

    // 失败导出：源持续失败（双读皆败）→目标不被触碰。
    FakeSnapshotSource broken;
    broken.files = good.files;
    broken.failFirstNReads["payload/objects/obj-a/data.bin"] = 99;
    auto failed = exporter->export_(broken, options, nullptr, nullptr, {});
    ASSERT_FALSE(failed) << "持续读失败未被上报";
    EXPECT_EQ(readFileBytes(packFile), previousBytes)
        << "先前完整版本被破坏（V23/MDL-20 违例）";
}

// =====================================================================
// IO-V31：后台不阻塞（NFR-PERF-01/02/AT-34——acceptance 3）
// =====================================================================

/// 验证（IO-V31）：导入于后台任务（std::thread＝execution 任务线程替
/// 身）运行——"UI 线程"空转打点无 >2s 无响应窗口；进度回调全部发生在
/// 任务线程（io 同步库调用、不创建线程——§9.13）；导入 Verified。
TEST_F(IoPackContractTest, BackgroundImportKeepsUiThreadResponsive)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V31 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-PERF-01", "NFR-PERF-02"},
                  std::vector<std::string>{"AT-34"});
    // 大负载（~24 MiB 全零，DEFLATE 后包很小、解压耗时毫秒级——足以
    // 覆盖多个检查点又不拖慢用例）。
    std::vector<std::pair<std::string, std::string>> payload = standardPayload();
    payload.push_back({"payload/objects/obj-big/blob.bin", std::string(24u * 1024u * 1024u, '\0')});
    const fs::path packFile = buildValidPack(m_root / "big.rwpack", payload);
    const fs::path targetDir = m_root / "target";

    auto importer = sdurws::ird::io::makePackageImporter();
    PackageImportOptions options;
    options.targetDir = targetDir;
    options.budget = BudgetSpec::packImportHardened();

    // 观测面：进度回调线程 id（应全为任务线程）＋UI 线程打点间隙。
    std::atomic<bool> progressSeen{false};
    std::thread::id workerId;
    const std::thread::id uiThreadId = std::this_thread::get_id();   // UI 线程基準
    sdurws::ird::io::IoProgressCallback progress = [&](const sdurws::ird::io::IoProgress&) {
        workerId = std::this_thread::get_id();
        progressSeen.store(true);
    };

    auto sessionResult = importer->begin(packFile, options, nullptr, {});
    ASSERT_TRUE(sessionResult) << sessionResult.error.detail;

    std::atomic<bool> done{false};
    IoResult<sdurws::ird::io::PackageImportReport> verified;
    std::thread worker([&] {
        verified = importer->verifyThrough(sessionResult.value, nullptr, progress);
        done.store(true);
    });

    // "UI 线程"空转打点（5ms 节拍——消息循环替身）；记录最大间隙。
    auto last = std::chrono::steady_clock::now();
    auto maxGap = std::chrono::steady_clock::duration::zero();
    while (!done.load()) {
        const auto now = std::chrono::steady_clock::now();
        maxGap = std::max(maxGap, now - last);
        last = now;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    worker.join();

    ASSERT_TRUE(verified) << verified.error.detail;
    EXPECT_EQ(sessionResult.value.state(), PackageImportState::Verified);
    // UI 无响应窗口上限（acceptance 3"UI 线程无 >2s 无响应窗口"）。
    EXPECT_LT(maxGap, std::chrono::milliseconds(2000))
        << "UI 线程出现 >2s 无响应窗口（io 阻塞了驱动线程之外？）";
    // 进度回调在任务线程（§9.13.4——io 在驱动线程内同步回调；这里的
    // "驱动线程"＝execution 任务线程替身——回调不得落在 UI 线程）。
    EXPECT_TRUE(progressSeen.load());
    EXPECT_NE(workerId, uiThreadId) << "进度回调落在了 UI 线程";
}

// =====================================================================
// IO-V32：已提交对暂存（PM-05/NFR-REL-01 D-17——acceptance 3）
// =====================================================================

/// 验证（IO-V32）：快照外落盘文件（.staging 残留/未提交 drafts）绝不
/// 出现在包内——"存在≠已提交"（§7.2②"清单外文件一概不读"的结构性
/// 断言：io 只迭代 enumerate 清单）。
TEST_F(IoPackContractTest, CommittedVsStagedOnlySnapshotEntriesPacked)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V32 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-05", "NFR-REL-01"},
                  std::vector<std::string>{"AT-20"});
    FakeSnapshotSource source;
    source.files = {
        {"payload/HEAD", std::string("rev-1\n")},
        {"payload/project.json", std::string("{}")},
        {"payload/revisions/rev-1.json", std::string("{}")},
        {"payload/objects/obj-a/data.bin", std::string("committed")},
    };
    // 快照外"落盘"文件（fake 的磁盘面——实现方保证 enumerate 不返回，
    // 但 read 若被越权调用仍可达——io 只迭代清单则永不触碰）。
    source.offSnapshot = {
        {".staging/tmp/leftover.bin", std::string("staging-residue")},
        {"payload/drafts/uncommitted.json", std::string("{\"draft\":true}")},
    };

    const fs::path packFile = m_root / "out.rwpack";
    auto exporter = sdurws::ird::io::makePackageExporter();
    PackageExportOptions options;
    options.targetFile = packFile;
    options.createdAtUtcIso8601 = "2026-09-17T08:00:00Z";
    auto exported = exporter->export_(source, options, nullptr, nullptr, {});
    ASSERT_TRUE(exported) << exported.error.detail;
    EXPECT_EQ(exported.value.entryCount, source.files.size()) << "包条目数≠快照清单数";

    // 包条目枚举比对（观测点"包条目枚举比对"）：rwpack.json/manifest.json
    // ＋快照清单——无任何清单外条目。
    auto channel = sdurws::ird::io::openZipChannel(packFile);
    ASSERT_TRUE(channel);
    auto entries = channel.value->listEntries();
    ASSERT_TRUE(entries);
    std::size_t payloadEntries = 0;
    for (const auto& e : entries.value) {
        EXPECT_NE(e.name, ".staging/tmp/leftover.bin") << ".staging 残留入包（V32 违例）";
        EXPECT_NE(e.name, "payload/drafts/uncommitted.json") << "未提交 drafts 入包（V32 违例）";
        if (e.name.rfind("payload/", 0) == 0) {
            ++payloadEntries;
        }
    }
    EXPECT_EQ(payloadEntries, source.files.size());
}

namespace {

/// 可编程取消令牌（IoCancelToken 测试实现——原子标志；一经真值不复位；
/// PackageTest.cpp 同款口径，自持不跨文件共享）。
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

} // namespace

// =====================================================================
// IO-V22：失败不建目标（PM-05/AT-20——IO-T07 acceptance 1/3；聚合负断言）
// =====================================================================

/// 验证（IO-V22）：§11.2 全部失败样例分支（V12 比例/超展开、V13 重复条
/// 目、V14 哈希不符、V19 发布前取消、V20 磁盘满、V21 清理失败）逐一以
/// **全新目标目录**执行后枚举检查——目标项目目录从未被创建/写入（含发
/// 布前取消、校验失败、清理失败各分支）。观测点＝目录存在性断言（结构
/// 性）：失败路径的目标面零污染由本聚合用例整体钉住，与单分支用例内的
/// expectNoTarget 互为冗余防线（卡行验收原文"全部失败样例跑后检查"）。
TEST_F(IoPackContractTest, FailureScenariosNeverCreateTargetDirectory)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V22 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20"});

    // 每分支独立目标目录（target-<k>）；任一分支失败即在本用例内短路
    // （ASSERT），最终统一枚举复核——聚合语义＝"跑后检查"。
    int k = 0;
    auto freshTarget = [this, &k]() {
        return m_root / ("target-" + std::to_string(k++));
    };

    // ---- 分支 1（V12 比例半边）：HEAD＝200 KiB 全零 DEFLATE（≫100:1）----
    {
        std::vector<std::pair<std::string, std::string>> payload = standardPayload();
        payload[0].second = std::string(200 * 1024, '\0');
        const fs::path packFile =
            buildValidPack(m_root / "v22-ratio.rwpack", payload, ZIP_CM_DEFLATE);
        const fs::path targetDir = freshTarget();
        auto importer = sdurws::ird::io::makePackageImporter();
        PackageImportOptions options;
        options.targetDir = targetDir;
        options.budget = BudgetSpec::packImportHardened();
        auto session = importer->begin(packFile, options, nullptr, {});
        ASSERT_TRUE(session) << session.error.detail;
        auto verified = importer->verifyThrough(session.value);
        ASSERT_FALSE(verified) << "比例炸弹样例未被拒绝";
        EXPECT_EQ(verified.error.code, IoErrorCode::SecBombRatio);
        EXPECT_EQ(session.value.state(), PackageImportState::Failed);
        expectNoTarget(targetDir);
    }

    // ---- 分支 2（V12 超展开半边）：3×512 KiB 伪随机 STORED＋收紧 1 MiB----
    {
        std::vector<std::pair<std::string, std::string>> payload = standardPayload();
        payload.push_back({"payload/objects/obj-b/blob1.bin", pseudoRandomBytes(512 * 1024, 11)});
        payload.push_back({"payload/objects/obj-b/blob2.bin", pseudoRandomBytes(512 * 1024, 12)});
        payload.push_back({"payload/objects/obj-b/blob3.bin", pseudoRandomBytes(512 * 1024, 13)});
        const fs::path packFile =
            buildValidPack(m_root / "v22-expand.rwpack", payload, ZIP_CM_STORE);
        const fs::path targetDir = freshTarget();
        auto importer = sdurws::ird::io::makePackageImporter();
        PackageImportOptions options;
        options.targetDir = targetDir;
        options.budget = BudgetSpec::packImportHardened();
        options.budget.tighten(BudgetDimension::ArchiveExpandedBytes, 1024ull * 1024ull);
        auto session = importer->begin(packFile, options, nullptr, {});
        ASSERT_TRUE(session) << session.error.detail;
        auto verified = importer->verifyThrough(session.value);
        ASSERT_FALSE(verified) << "超展开样例未被拒绝";
        EXPECT_EQ(verified.error.code, IoErrorCode::SecBudgetExpand);
        EXPECT_EQ(session.value.state(), PackageImportState::Failed);
        expectNoTarget(targetDir);
    }

    // ---- 分支 3（V13）：同名条目×2（手工字节 zip——容器层容忍编排）----
    {
        std::vector<std::pair<std::string, std::string>> payload = standardPayload();
        std::string totalDigestHex;
        const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
        const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), 0);
        const std::string zipBytes = handcraftZip({
            ZipEntrySpec{PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_STORE},
            ZipEntrySpec{PackFormat::kManifestEntryName, manifestJson, ZIP_CM_STORE},
            ZipEntrySpec{"payload/HEAD", std::string("first\n"), ZIP_CM_STORE},
            ZipEntrySpec{"payload/HEAD", std::string("second\n"), ZIP_CM_STORE},
            ZipEntrySpec{payload[1].first, payload[1].second, ZIP_CM_STORE},
            ZipEntrySpec{payload[2].first, payload[2].second, ZIP_CM_STORE},
            ZipEntrySpec{payload[3].first, payload[3].second, ZIP_CM_STORE},
        });
        const fs::path packFile = m_root / "v22-dup.rwpack";
        {
            std::ofstream out(packFile, std::ios::binary);
            ASSERT_TRUE(out.is_open());
            out.write(zipBytes.data(), static_cast<std::streamsize>(zipBytes.size()));
        }
        const fs::path targetDir = freshTarget();
        auto importer = sdurws::ird::io::makePackageImporter();
        PackageImportOptions options;
        options.targetDir = targetDir;
        options.budget = BudgetSpec::packImportHardened();
        auto session = importer->begin(packFile, options, nullptr, {});
        ASSERT_TRUE(session) << session.error.detail;
        auto verified = importer->verifyThrough(session.value);
        ASSERT_FALSE(verified) << "重复条目样例未被拒绝";
        EXPECT_EQ(verified.error.code, IoErrorCode::PackDuplicateEntry);
        EXPECT_EQ(session.value.state(), PackageImportState::Failed);
        expectNoTarget(targetDir);
    }

    // ---- 分支 4（V14）：payload 篡改一字节（manifest/totalDigest 自洽）----
    {
        std::vector<std::pair<std::string, std::string>> payload = standardPayload();
        const fs::path packFile = buildValidPack(m_root / "v22-hash.rwpack", payload);
        {
            // 原地篡改：重建归档——data.bin 翻转一字节、manifest 不变
            // （与 V21 用例同款构造）。
            std::string tampered = payload[3].second;
            tampered[0] = static_cast<char>(tampered[0] ^ 0xFF);
            std::string totalDigestHex;
            const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
            const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), 0);
            std::vector<ZipEntrySpec> entries;
            entries.push_back(ZipEntrySpec{PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{PackFormat::kManifestEntryName, manifestJson, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{payload[0].first, payload[0].second, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{payload[1].first, payload[1].second, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{payload[2].first, payload[2].second, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{payload[3].first, tampered, ZIP_CM_STORE});
            makeArchiveViaLibzip(packFile, entries);
        }
        const fs::path targetDir = freshTarget();
        auto importer = sdurws::ird::io::makePackageImporter();
        PackageImportOptions options;
        options.targetDir = targetDir;
        options.budget = BudgetSpec::packImportHardened();
        auto session = importer->begin(packFile, options, nullptr, {});
        ASSERT_TRUE(session) << session.error.detail;
        auto verified = importer->verifyThrough(session.value);
        ASSERT_FALSE(verified) << "哈希不符样例未被拒绝";
        EXPECT_EQ(verified.error.code, IoErrorCode::PackHashMismatch);
        EXPECT_EQ(session.value.state(), PackageImportState::Failed);
        expectNoTarget(targetDir);
    }

    // ---- 分支 5（V19）：展开检查点取消（发布前取消——UX-03 无诊断）----
    {
        const fs::path packFile = buildValidPack(m_root / "v22-cancel.rwpack", standardPayload());
        const fs::path targetDir = freshTarget();
        auto importer = sdurws::ird::io::makePackageImporter();
        PackageImportOptions options;
        options.targetDir = targetDir;
        options.budget = BudgetSpec::packImportHardened();
        FlagToken cancel;
        bool cancelled = false;
        sdurws::ird::io::IoProgressCallback progress =
            [&](const sdurws::ird::io::IoProgress& p) {
                if (!cancelled && std::strcmp(p.stage, "extract") == 0 && p.done >= 3) {
                    cancel.cancel();
                    cancelled = true;
                }
            };
        auto session = importer->begin(packFile, options, nullptr, {});
        ASSERT_TRUE(session) << session.error.detail;
        auto verified = importer->verifyThrough(session.value, &cancel, progress);
        ASSERT_FALSE(verified) << "取消未被响应";
        EXPECT_EQ(verified.error.code, IoErrorCode::Cancelled);
        EXPECT_EQ(session.value.state(), PackageImportState::Canceled);
        EXPECT_TRUE(session.value.report().diagnostics.empty()) << "取消路径产生诊断（UX-03）";
        expectNoTarget(targetDir);
    }

    // ---- 分支 6（V20）：fake TempArea 注入可用 10 字节（磁盘满）----
    {
        const fs::path packFile = buildValidPack(m_root / "v22-disk.rwpack", standardPayload());
        const fs::path targetDir = freshTarget();
        auto faults = std::make_shared<FaultTempAreaManager>(sdurws::ird::io::makeTempAreaManager());
        faults->availableOverride = std::make_shared<std::uint64_t>(10);
        sdurws::ird::io::PackageIoFacilities facilities;
        facilities.tempAreas = faults;
        auto importer = sdurws::ird::io::makePackageImporter(facilities);
        PackageImportOptions options;
        options.targetDir = targetDir;
        options.budget = BudgetSpec::packImportHardened();
        auto session = importer->begin(packFile, options, nullptr, {});
        ASSERT_TRUE(session) << session.error.detail;
        auto verified = importer->verifyThrough(session.value);
        ASSERT_FALSE(verified) << "磁盘满样例未被检出";
        EXPECT_EQ(verified.error.code, IoErrorCode::PackDiskFull);
        EXPECT_EQ(session.value.state(), PackageImportState::Failed);
        expectNoTarget(targetDir);
    }

    // ---- 分支 7（V21）：哈希不符触发自动清理＋注入清理失败（终态
    // CleanupFailed——残留仅在隐藏前缀临时区，目标同样零污染）----
    {
        std::vector<std::pair<std::string, std::string>> payload = standardPayload();
        const fs::path packFile = buildValidPack(m_root / "v22-clean.rwpack", payload);
        {
            std::string tampered = payload[3].second;
            tampered[0] = static_cast<char>(tampered[0] ^ 0xFF);
            std::string totalDigestHex;
            const std::string manifestJson = buildManifestJson(payload, &totalDigestHex);
            const std::string rwpackJson = buildRwpackJson(totalDigestHex, payload.size(), 0);
            std::vector<ZipEntrySpec> entries;
            entries.push_back(ZipEntrySpec{PackFormat::kRwpackEntryName, rwpackJson, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{PackFormat::kManifestEntryName, manifestJson, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{payload[0].first, payload[0].second, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{payload[1].first, payload[1].second, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{payload[2].first, payload[2].second, ZIP_CM_STORE});
            entries.push_back(ZipEntrySpec{payload[3].first, tampered, ZIP_CM_STORE});
            makeArchiveViaLibzip(packFile, entries);
        }
        const fs::path targetDir = freshTarget();
        auto faults = std::make_shared<FaultTempAreaManager>(sdurws::ird::io::makeTempAreaManager());
        faults->failCleanupTimes = 1;
        sdurws::ird::io::PackageIoFacilities facilities;
        facilities.tempAreas = faults;
        auto importer = sdurws::ird::io::makePackageImporter(facilities);
        PackageImportOptions options;
        options.targetDir = targetDir;
        options.budget = BudgetSpec::packImportHardened();
        auto session = importer->begin(packFile, options, nullptr, {});
        ASSERT_TRUE(session) << session.error.detail;
        auto verified = importer->verifyThrough(session.value);
        ASSERT_FALSE(verified);
        EXPECT_EQ(verified.error.code, IoErrorCode::PackCleanupFailed);
        EXPECT_EQ(session.value.state(), PackageImportState::CleanupFailed);
        expectNoTarget(targetDir);
        // 清理失败分支收尾：重试清理成功，不留会话残留（本用例自清理）。
        auto retry = importer->cleanup(session.value);
        ASSERT_TRUE(retry) << retry.error.detail;
    }

    // ---- 聚合观测点：全部分支跑完后枚举目标面——零目标目录存在 ----
    std::error_code ec;
    ASSERT_TRUE(fs::exists(m_root, ec)) << "工作区丢失（测试自身缺陷）";
    for (fs::directory_iterator it(m_root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        EXPECT_TRUE(name.rfind(L"target-", 0) != 0)
            << "失败样例创建了目标目录：" << it->path();
    }
}
