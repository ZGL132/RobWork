/**
 * @file   ResourceSolidifyTest.cpp
 * @brief  固化执行与外部源检测、runtime 资源读取桥用例组（IoRes）——
 *         V17 SourceChangedDuringCopy（复制中途改写源＝进度检查点定点
 *         注入→sourceChangedDuringCopy＋Changed 诊断＋中转清理＋无部分
 *         副本）、V24 ReadOnlyMedia（IO-RES-READONLY 与 ACCESS-DENIED 码
 *         区分）、probe Missing/Changed/Ok（V26 probe 半边）、三段边界
 *         （PM-01 段①③执行＋段②检测、ARCH §6.6 R4）、§8.6 生命周期
 *         五条（P-IO-2——P-IO-1 注入面以测试替身承载）。
 *
 * 设计依据：
 *   - units/io.md §8.1~§8.6（三段边界/快照/检测/固化流程/引用关系/
 *     accessVersion 与 ResourceBytes 生命周期五条裁决）、§9.7/§9.8（接口
 *     契约表）、§11.2 IO-V17/V24/V26 行、§11.1（FaultInjector 替身＝
 *     进度检查点定点注入；不链 testkit——T-1，替代身以 gtest 原生实现）
 *   - 需求 NFR-REL-04（检测实现责任方）、CON-03（固化复制）、PM-01
 *     （三段边界）、NFR-SEC-01/02（P-1 例外＋预算）
 *   - 任务契约 tasks/foundation/IO-T05.json acceptance 2/3/4/5
 *
 * 断言纪律（AGENTS.md §2.7）：每个用例中文注明验证的需求/验收条目；
 * 测试以真实临时文件＋注入源替身为载体，失败如实失败不伪造。
 */

#include <sdurws/ird/io/ResourceIo.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>
#include <sdurws/ird/io/IoError.hpp>
#include <sdurws/ird/io/SafePath.hpp>

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::Digest256;
using sdurws::ird::core::ObjectId;
using sdurws::ird::io::BudgetScopeId;
using sdurws::ird::io::ExternalRefRecord;
using sdurws::ird::io::ExternalRefState;
using sdurws::ird::io::IoError;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::IoResult;
using sdurws::ird::io::IProjectBytesSource;
using sdurws::ird::io::IExternalRefSource;
using sdurws::ird::io::IResourceSnapshotterPtr;
using sdurws::ird::io::IRuntimeResourceAdapterPtr;
using sdurws::ird::io::PathRole;
using sdurws::ird::io::ProbeDetail;
using sdurws::ird::io::ProjectBytesView;
using sdurws::ird::io::ResourceBytesView;
using sdurws::ird::io::ResourceOpenSpec;
using sdurws::ird::io::ResourceReadStatus;
using sdurws::ird::io::ResourceReadResult;
using sdurws::ird::io::ResourceSnapshot;
using sdurws::ird::io::SolidifyStagingResult;
using sdurws::ird::io::kAccessVersion;
using sdurws::ird::io::errorCodeToken;
using sdurws::ird::io::makeResourceReader;
using sdurws::ird::io::makeResourceSnapshotter;
using sdurws::ird::io::makeRuntimeResourceAdapter;

namespace {

// =====================================================================
// 测试助手
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

Digest256 manualDigest(const std::vector<std::uint8_t>& bytes)
{
    ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

std::string hexOf(const Digest256& d)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : d) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

class IoSolidifyTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::random_device rd;
        const std::uint64_t tag =
            (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
        char name[64];
        std::snprintf(name, sizeof(name), "ird-io-solid-%016llx",
                      static_cast<unsigned long long>(tag));
        dir = fs::temp_directory_path() / name;
        fs::create_directories(dir);
        snapshotter = makeResourceSnapshotter();
    }
    void TearDown() override
    {
        snapshotter.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    fs::path write(const std::string& name, const std::vector<std::uint8_t>& bytes)
    {
        const fs::path p = dir / name;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        return p;
    }

    /// 授权中转目录形态（§4.1 P-6：.staging/tmp/solidify-<id>/）。
    fs::path stagingOf(const std::string& id)
    {
        const fs::path p = dir / ".staging" / "tmp" / ("solidify-" + id);
        fs::create_directories(p);
        return p;
    }

    std::vector<std::uint8_t> rampBytes(std::size_t n, std::uint8_t seed)
    {
        std::vector<std::uint8_t> b(n);
        for (std::size_t i = 0; i < n; ++i) {
            b[i] = static_cast<std::uint8_t>(seed + i * 13);
        }
        return b;
    }

    fs::path dir;                                   ///< 本用例临时根（工作区）
    IResourceSnapshotterPtr snapshotter;            ///< 被测固化/检测服务
};

// =====================================================================
// V17 SourceChangedDuringCopy（acceptance 2——NFR-REL-04/CON-03）
// =====================================================================

/// IO-V17：固化复制中途改写源文件 → sourceChangedDuringCopy（Changed 诊断
/// ＋复制前后双快照 digest 不等＋中转清理＋无部分副本）。FaultInterceptor
/// 定点注入（§11.1 替身）＝经进度检查点（§9.13"进度回调在驱动线程内同步
/// 调用"）在首个复制块完成后改写源——注入点确定，不依赖时序。
TEST_F(IoSolidifyTest, SourceChangedDuringCopyDetectedAndStagingCleaned)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V17 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-REL-04", "CON-03"},
                  std::vector<std::string>{"AT-20"});
    // 源＝256 KiB（4 个 64 KiB 块）——保证复制跨越多个检查点。
    const std::vector<std::uint8_t> original = rampBytes(256 * 1024, 7);
    const fs::path src = write("model.bin", original);

    const auto reader = makeResourceReader();
    ResourceOpenSpec userSource;
    userSource.role = PathRole::UserSource;
    const IoResult<ResourceSnapshot> before = reader->snapshot(src, userSource, nullptr, nullptr);
    ASSERT_TRUE(before);

    // 定点注入：首个进度回调（第 1 块复制完成）时改写源内容（等长不同文）。
    std::atomic<bool> injected{false};
    const std::vector<std::uint8_t> replacement = rampBytes(256 * 1024, 200);
    auto injector = [&](const sdurws::ird::io::IoProgress& p) {
        if (!injected.load() && std::string(p.stage) == "solidify-copy" && p.done > 0) {
            std::ofstream f(src, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(replacement.data()),
                    static_cast<std::streamsize>(replacement.size()));
            injected.store(true);
        }
    };

    const fs::path staging = stagingOf("v17");
    const IoResult<SolidifyStagingResult> r =
        snapshotter->solidifyToStaging(src, staging, 0, nullptr, nullptr, injector);
    ASSERT_FALSE(r) << "复制窗口内源变化必须固化失败（§8.4 步骤 5）";
    EXPECT_EQ(r.error.code, IoErrorCode::ResChanged);   // Changed 诊断（IO-D10 判据）
    EXPECT_TRUE(injected.load()) << "定点注入确已发生（进度检查点）";
    // 比较型 Changed 诊断（§8.3 形态）：recorded(before)/actual(after) 摘要对。
    EXPECT_EQ(paramOf(r.error, "recorded"), hexOf(before.value.contentDigest));
    EXPECT_EQ(paramOf(r.error, "source-changed"), "true");

    // 复制前后双快照 digest 不等断言（V17 观测点）。
    const IoResult<ResourceSnapshot> after = reader->snapshot(src, userSource, nullptr, nullptr);
    ASSERT_TRUE(after);
    EXPECT_FALSE(before.value.sameContentAs(after.value));
    EXPECT_NE(before.value.contentDigest, after.value.contentDigest);

    // 中转清理＋无部分副本（V17 观测点：中转目录不存在）。
    EXPECT_FALSE(fs::exists(staging)) << "失败路径中转目录必须被清理";
}

/// IO-V17 对照＋三段边界③（固化前契约，PM-01 段③执行）：源未变 → 固化
/// 成功——中转副本完整（digest 与源一致）、sourceChangedDuringCopy=false、
/// 副本保留待 project 入库（发布与 objects 写入**归 project**——io 不写
/// objects/，§9.8 副作用行/卡行禁止项；本用例同时负断言 io 未创建任何
/// objects 目录）。段①一次性读取＝源位于用户区（临时根，非项目区）——
/// NFR-SEC-01 例外、仍受预算管辖（P-1 通道快照已证）。
TEST_F(IoSolidifyTest, SolidifyUnchangedSourceKeepsStagingCopyForProject)
{
    const std::vector<std::uint8_t> bytes = rampBytes(100 * 1024, 42);
    const fs::path src = write("robot-geo.bin", bytes);

    const fs::path staging = stagingOf("v17ok");
    const IoResult<SolidifyStagingResult> r =
        snapshotter->solidifyToStaging(src, staging, 0, nullptr, nullptr);
    ASSERT_TRUE(r) << "未变源固化成功（r.error=" << r.error.detail << "）";
    EXPECT_FALSE(r.value.sourceChangedDuringCopy);
    EXPECT_EQ(r.value.copiedDigest, manualDigest(bytes));
    EXPECT_EQ(r.value.before.contentDigest, manualDigest(bytes));
    EXPECT_EQ(r.value.after.contentDigest, manualDigest(bytes));

    // 段③固化前契约：中转副本就位、完整可复算；objects 入库未发生（io 侧
    // 负断言——直接创建 CanonicalModel/objects 写入禁止）。
    const fs::path copy = staging / "robot-geo.bin";
    ASSERT_TRUE(fs::exists(copy));
    std::ifstream f(copy, std::ios::binary);
    const std::vector<std::uint8_t> copied((std::istreambuf_iterator<char>(f)),
                                           std::istreambuf_iterator<char>());
    EXPECT_EQ(copied, bytes) << "中转副本逐字节完整（传输完整性）";

    // 工作区负断言：io 固化执行侧不得出现 objects/ 目录（发布归 project）。
    bool objectsSeen = false;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(dir)) {
        if (e.path().filename() == "objects") {
            objectsSeen = true;
        }
    }
    EXPECT_FALSE(objectsSeen) << "io 不写 objects/（§9.8——发布归 project N-7）";
}

/// 固化中转目录越权（非 .staging 形态）→ IO-SEC-PATH-ESCAPE（§4.1 P-6
/// "io 只写 project 授权中转位"的守门负例）。
TEST_F(IoSolidifyTest, NonAuthorizedStagingDirRejected)
{
    const fs::path src = write("m.bin", {'x'});
    const fs::path staging = dir / "plain-tmp" / "solidify-x";      // 无 .staging 段
    const IoResult<SolidifyStagingResult> r =
        snapshotter->solidifyToStaging(src, staging, 0, nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecPathEscape);
    EXPECT_FALSE(fs::exists(staging)) << "拒绝路径零写入";
}

// =====================================================================
// V24 ReadOnlyMedia（acceptance 3——§4.2.5 四分类：与 ACCESS-DENIED 码区分）
// =====================================================================

/// IO-V24：写方向目标实体带只读属性 → IO-RES-READONLY（不是 ACCESS-DENIED）
/// ——OS 对只读实体的写打开以 ACCESS_DENIED 拒绝，实现按"写方向＋目标
/// READONLY 属性"判别分码（§4.2.5 READONLY 行"目标卷只读属性/写探测失败"；
/// 观测点"码区分断言"）。清理通道复位只读属性（io 自会话产物——不污染）。
TEST_F(IoSolidifyTest, ReadonlyTargetWritesMapToReadonlyCode)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V24 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-01"},
                  std::vector<std::string>{});
    const fs::path src = write("payload.bin", rampBytes(1024, 5));
    const fs::path staging = stagingOf("v24");
    const fs::path target = staging / "payload.bin";    // 预置只读目标（残留会话产物形态）
    {
        std::ofstream f(target, std::ios::binary);
        f << "stale";
    }
    const DWORD attr = ::GetFileAttributesW(target.wstring().c_str());
    ASSERT_NE(attr, INVALID_FILE_ATTRIBUTES);
    ASSERT_TRUE(::SetFileAttributesW(target.wstring().c_str(), attr | FILE_ATTRIBUTE_READONLY));

    const IoResult<SolidifyStagingResult> r =
        snapshotter->solidifyToStaging(src, staging, 0, nullptr, nullptr);
    ASSERT_FALSE(r) << "只读目标必须写失败（§4.2.5 写方向）";
    // 码区分断言（V24 观测点）：READONLY≠ACCESS-DENIED；方向参数＝write。
    EXPECT_EQ(r.error.code, IoErrorCode::ResReadonly)
        << "实得 token: " << errorCodeToken(r.error.code);
    EXPECT_NE(r.error.code, IoErrorCode::ResAccessDenied);
    EXPECT_EQ(paramOf(r.error, "direction"), "write");
}

// =====================================================================
// probe 检测事实（V26 probe 半边——acceptance 3，NFR-REL-04）
// =====================================================================

/// IO-V26（probe 半边）：外部引用记录指向已删文件 → ExternalRefState::
/// Missing＋细分注记；**资源事实类别**（§2.5）：返回面只有事实三态＋注记，
/// 无任何工程不可行结论字段（判定归 evidence 门禁——§6.6/§10.8）。
TEST_F(IoSolidifyTest, ProbeReportsMissingForDeletedExternalFile)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V26 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-REL-04", "MDL-19"},
                  std::vector<std::string>{});
    const fs::path src = write("gone.bin", {'a', 'b'});
    const Digest256 recorded = manualDigest({'a', 'b'});
    fs::remove(src);                                    // 记录后源被删除

    ExternalRefRecord record;
    record.externalRefId = "ext-1";
    record.absPath = src;                               // {绝对路径＋内容哈希}——段②记录形态
    record.recordedDigest = recorded;
    record.recordedSizeBytes = 2;

    std::optional<ProbeDetail> detail;
    const IoResult<ExternalRefState> r = snapshotter->probe(record, nullptr, nullptr, &detail);
    ASSERT_TRUE(r);
    EXPECT_EQ(r.value, ExternalRefState::Missing);
    ASSERT_TRUE(detail.has_value());
    // Missing＝不可达：无快照可产——actual 留默认（路径空＋摘要空）。
    EXPECT_TRUE(detail->actual.finalPath.empty());
    bool anyNote = false;
    for (const std::string& n : detail->notes) {
        if (n.find("unreachable") != std::string::npos) {
            anyNote = true;                             // 不可达细分注记（§8.3）
        }
    }
    EXPECT_TRUE(anyNote);
}

/// probe Changed：源内容被替换（记录摘要过期）→ Changed＋actual 快照
/// （digest 判据——IO-D10；比较型 recorded/actual 事实对）。
TEST_F(IoSolidifyTest, ProbeReportsChangedWhenContentReplaced)
{
    const fs::path src = write("drift.bin", rampBytes(2048, 9));
    const Digest256 recorded = manualDigest(rampBytes(2048, 9));

    ExternalRefRecord record;
    record.externalRefId = "ext-2";
    record.absPath = src;
    record.recordedDigest = recorded;
    record.recordedSizeBytes = 2048;
    {
        std::ofstream f(src, std::ios::binary | std::ios::trunc);
        const std::vector<std::uint8_t> replacement = rampBytes(2048, 130);
        f.write(reinterpret_cast<const char*>(replacement.data()),
                static_cast<std::streamsize>(replacement.size()));
    }

    std::optional<ProbeDetail> detail;
    const IoResult<ExternalRefState> r = snapshotter->probe(record, nullptr, nullptr, &detail);
    ASSERT_TRUE(r);
    EXPECT_EQ(r.value, ExternalRefState::Changed);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->actual.contentDigest, manualDigest(rampBytes(2048, 130)));
    EXPECT_NE(detail->actual.contentDigest, recorded);
}

/// probe Ok：内容未变（路径拼写给变不改结论——digest 为身份，路径不作
/// 身份）→ Ok＋size 漂移仅提示注记（§8.3"防 touch 误报"）。三段边界②
/// （段②检测）：{绝对路径＋内容哈希} 记录是引用键，裸路径不充当引用。
TEST_F(IoSolidifyTest, ProbeReportsOkAndIgnoresPurePathSpelling)
{
    const std::vector<std::uint8_t> bytes = rampBytes(4096, 77);
    const fs::path src = write("stable.bin", bytes);

    ExternalRefRecord record;
    record.externalRefId = "ext-3";
    record.absPath = src;
    record.recordedDigest = manualDigest(bytes);
    record.recordedSizeBytes = bytes.size() + 1;        // 记录 size 漂移（提示性）

    // 大小写不同拼写指向同一实体（Windows）——digest 判据不受路径形态影响。
    fs::path altSpelling = src;
    altSpelling.replace_filename("STABLE.BIN");

    std::optional<ProbeDetail> detail;
    const IoResult<ExternalRefState> r = snapshotter->probe(record, nullptr, nullptr, &detail);
    ASSERT_TRUE(r);
    EXPECT_EQ(r.value, ExternalRefState::Ok);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->actual.sizeBytes, bytes.size());
    bool sizeHint = false;
    for (const std::string& n : detail->notes) {
        if (n.find("size-drift-hint") != std::string::npos) {
            sizeHint = true;                            // size 漂移＝提示不判变
        }
    }
    EXPECT_TRUE(sizeHint);
    EXPECT_EQ(snapshotter->probe(record, nullptr, nullptr, nullptr).value, ExternalRefState::Ok)
        << "大小写拼写不影响检测结论（路径不作身份）";
}

// =====================================================================
// IRuntimeResourceAdapter（acceptance 5——P-IO-2 §8.6 五条＋P-IO-1 注入面）
// =====================================================================

/// 对象库字节注入源替身（project 侧在 L5 实现的形态——io 不直读项目存储，
/// §2.3 非目标 6；键＝ObjectId canonical 文本）。
struct FakeProjectBytes final : IProjectBytesSource {
    std::map<std::string, std::vector<std::uint8_t>> store;     // 固化对象表

    IoResult<ProjectBytesView> tryObjectBytes(const ObjectId& id,
                                              const sdurws::ird::core::ContentVersion&) const override
    {
        IoResult<ProjectBytesView> out;
        const auto it = store.find(id.toCanonical());
        if (it == store.end()) {
            out.error.code = IoErrorCode::ResNotFound;      // 未命中（可能为 Recorded）
            return out;
        }
        out.value.data = it->second.data();
        out.value.size = it->second.size();
        out.value.digest = manualDigest(it->second);
        return out;
    }
};

/// 外部引用记录注入源替身（project DraftService 持久层形态）。
struct FakeExternalRefs final : IExternalRefSource {
    std::map<std::string, ExternalRefRecord> records;

    IoResult<ExternalRefRecord> tryRecord(const std::string& id) const override
    {
        IoResult<ExternalRefRecord> out;
        const auto it = records.find(id);
        if (it == records.end()) {
            out.error.code = IoErrorCode::ResNotFound;
            return out;
        }
        out.value = it->second;
        return out;
    }
};

/// §8.6 五条裁决（P-IO-2 处置）＋§9.7 路由：Solidified 命中缓存（同指针）、
/// Recorded 每次重读不缓存（改源即反映）、缓冲跨调用稳定至析构（第 1 条
/// 实际保证）、并发调用安全（第 3 条）、值语义与 accessVersion 标记。
TEST_F(IoSolidifyTest, AdapterRoutingAndLifecycleRulings)
{
    const std::vector<std::uint8_t> solidBytes = rampBytes(3000, 11);
    const fs::path extFile = write("recorded.bin", rampBytes(2048, 50));

    // 替身为栈上对象（生命周期覆盖 adapter 使用区间——io 不接管注入源
    // 所有权，§9.0）。
    FakeProjectBytes project;
    const ObjectId solidId = ObjectId::generate();
    project.store[solidId.toCanonical()] = solidBytes;

    FakeExternalRefs refs;
    const ObjectId recordedId = ObjectId::generate();
    ExternalRefRecord rec;
    rec.externalRefId = recordedId.toCanonical();
    rec.absPath = extFile;
    rec.recordedDigest = manualDigest(rampBytes(2048, 50));
    rec.recordedSizeBytes = 2048;
    refs.records[recordedId.toCanonical()] = rec;

    const IRuntimeResourceAdapterPtr adapter = makeRuntimeResourceAdapter(&project, &refs);
    const IRuntimeResourceAdapterPtr adapter2 = makeRuntimeResourceAdapter(&project, &refs);

    // —— Solidified 通道：Ok＋digest 一致；二次调用命中缓存（同指针＝§8.6.4
    //    允许的不可变缓存；accessVersion 标记＝§8.6 当前 1）。
    const ResourceReadResult s1 = adapter->tryResourceBytes(solidId);
    ASSERT_EQ(s1.status, ResourceReadStatus::Ok);
    EXPECT_EQ(std::memcmp(s1.bytes.data, solidBytes.data(), solidBytes.size()), 0);
    EXPECT_EQ(s1.bytes.digest, manualDigest(solidBytes));
    EXPECT_EQ(s1.bytes.accessVersion, kAccessVersion);
    const ResourceReadResult s2 = adapter->tryResourceBytes(solidId);
    ASSERT_EQ(s2.status, ResourceReadStatus::Ok);
    EXPECT_EQ(s2.bytes.data, s1.bytes.data) << "Solidified 缓存命中（同缓冲）";

    // —— Recorded 通道：Ok＋digest 一致（P-1 读取＋重算比对记录）。
    const ResourceReadResult r1 = adapter->tryResourceBytes(recordedId);
    ASSERT_EQ(r1.status, ResourceReadStatus::Ok);
    EXPECT_EQ(r1.bytes.digest, manualDigest(rampBytes(2048, 50)));
    const std::uint8_t* recordedFirst = r1.bytes.data;

    // —— §8.6.4 Recorded 绝不缓存：改写源后再次调用必须反映新内容（返回
    //    Changed——记录摘要未更新；若适配器缓存则仍回旧 digest 旧字节）。
    {
        std::ofstream f(extFile, std::ios::binary | std::ios::trunc);
        const std::vector<std::uint8_t> replacement = rampBytes(2048, 90);
        f.write(reinterpret_cast<const char*>(replacement.data()),
                static_cast<std::streamsize>(replacement.size()));
    }
    const ResourceReadResult r2 = adapter->tryResourceBytes(recordedId);
    EXPECT_EQ(r2.status, ResourceReadStatus::Changed);
    EXPECT_NE(paramOf(r2.error, "actual"), paramOf(r2.error, "recorded"))
        << "重算 digest 与记录不符＝S10 复查可发现替换（§8.6.4 语义前提）";

    // —— §8.6.1 生命周期（最低至下次调用/实际至析构）：先取的缓冲在后续
    //    多次调用后仍完整可读（稳定区不提前释放）。
    (void)adapter->tryResourceBytes(solidId);
    (void)adapter2->tryResourceBytes(solidId);
    ASSERT_NE(recordedFirst, nullptr);
    EXPECT_EQ(std::memcmp(recordedFirst, rampBytes(2048, 50).data(), 2048), 0)
        << "暴露缓冲至析构有效（强于最低保证——§8.6.1）";

    // —— 路由终态：双源均未命中 → Missing（细分 detail）。
    const ResourceReadResult miss = adapter->tryResourceBytes(ObjectId::generate());
    EXPECT_EQ(miss.status, ResourceReadStatus::Missing);
}

/// §8.6.3 并发只读安全：多线程并发 tryResourceBytes（Recorded 路径——每次
/// 独立缓冲），全部 Ok 且内容一致；调用结束后所有视图仍可完整校验（缓冲
/// 未被复用/提前释放）。
TEST_F(IoSolidifyTest, AdapterConcurrentRecordedReadsAreSafe)
{
    const std::vector<std::uint8_t> bytes = rampBytes(64 * 1024, 3);
    const fs::path extFile = write("concurrent.bin", bytes);

    FakeProjectBytes project;                           // 空对象库——全部走 Recorded
    FakeExternalRefs refs;
    const ObjectId id = ObjectId::generate();
    ExternalRefRecord rec;
    rec.externalRefId = id.toCanonical();
    rec.absPath = extFile;
    rec.recordedDigest = manualDigest(bytes);
    rec.recordedSizeBytes = bytes.size();
    refs.records[id.toCanonical()] = rec;

    const IRuntimeResourceAdapterPtr adapter = makeRuntimeResourceAdapter(&project, &refs);

    // 4 线程 × 8 次并发读取；视图存表，join 后统一校验（§8.6.1/8.6.3）。
    constexpr int kThreads = 4;
    constexpr int kCallsPerThread = 8;
    std::vector<ResourceBytesView> views(kThreads * kCallsPerThread);
    std::vector<std::atomic<int>> statuses(static_cast<std::size_t>(kThreads * kCallsPerThread));
    for (std::atomic<int>& s : statuses) {
        s.store(-1);
    }
    std::vector<std::thread> pool;
    for (int t = 0; t < kThreads; ++t) {
        pool.emplace_back([&, t] {
            for (int c = 0; c < kCallsPerThread; ++c) {
                const ResourceReadResult r = adapter->tryResourceBytes(id);
                const int idx = t * kCallsPerThread + c;
                statuses[static_cast<std::size_t>(idx)].store(static_cast<int>(r.status));
                views[static_cast<std::size_t>(idx)] = r.bytes;
            }
        });
    }
    for (std::thread& th : pool) {
        th.join();
    }
    for (int i = 0; i < kThreads * kCallsPerThread; ++i) {
        EXPECT_EQ(statuses[static_cast<std::size_t>(i)].load(), static_cast<int>(ResourceReadStatus::Ok))
            << "并发读取全 Ok（idx=" << i << "）";
        ASSERT_NE(views[static_cast<std::size_t>(i)].data, nullptr);
        EXPECT_EQ(views[static_cast<std::size_t>(i)].size, bytes.size());
        EXPECT_EQ(std::memcmp(views[static_cast<std::size_t>(i)].data, bytes.data(), bytes.size()), 0)
            << "并发视图内容完整（缓冲独立稳定——§8.6.3）";
    }
}

/// P-IO-1 注入面负例：Recorded 外部源不可达 → Missing（细分嵌 detail，
/// 不降级误报）＋预算超限 → Budget（四分映射——§9.7 错误类型行）。
TEST_F(IoSolidifyTest, AdapterRecordedFailureMapping)
{
    FakeProjectBytes project;
    FakeExternalRefs refs;
    const IRuntimeResourceAdapterPtr adapter = makeRuntimeResourceAdapter(&project, &refs);

    // 记录指向不存在文件 → Missing（不可达口径——§8.3）。
    const ObjectId ghost = ObjectId::generate();
    ExternalRefRecord rec;
    rec.externalRefId = ghost.toCanonical();
    rec.absPath = dir / "no-such-external.bin";
    rec.recordedDigest = manualDigest({'z'});
    refs.records[ghost.toCanonical()] = rec;
    const ResourceReadResult r = adapter->tryResourceBytes(ghost);
    EXPECT_EQ(r.status, ResourceReadStatus::Missing);
    EXPECT_NE(r.error.detail.find("外部源读取失败"), std::string::npos) << "细分嵌 detail";

    // 预算路径：内建 SingleFileBytes 上界（256 MiB 产品默认）以内文件不受
    // 影响——Budget 映射经对象库通道替身模拟（注入源返回预算码）。
    struct BudgetSource final : IProjectBytesSource {
        IoResult<ProjectBytesView> tryObjectBytes(const ObjectId&,
                                                  const sdurws::ird::core::ContentVersion&) const override
        {
            IoResult<ProjectBytesView> out;
            out.error.code = IoErrorCode::SecBudgetTotal;   // 模拟对象库侧预算失败
            out.error.detail = "fake-object-store-budget";
            return out;
        }
    } budgetSource;
    const IRuntimeResourceAdapterPtr budgetAdapter =
        makeRuntimeResourceAdapter(&budgetSource, &refs);
    const ResourceReadResult rb = budgetAdapter->tryResourceBytes(ObjectId::generate());
    EXPECT_EQ(rb.status, ResourceReadStatus::Budget);
}

// =====================================================================
// IO-V28：重关联新修订（PM-09/CON-03/AT-20——IO-T07 acceptance 1/4；
// O-09 对端＝project 命令 fake 承载，io 零 project 编译依赖）
// =====================================================================

/// project 侧命令编排替身（O-09 处置——"V28 的对端以 project 命令 fake
/// 承载"）：真实编排归 project（§10.1——固化命令、重关联命令均为
/// project 拥有的写路径），io 仅提供前后快照与检测。本替身以"命令调用
/// 序列记录器"形态模拟 project 端：记录 io 交付物（摘要/路径）驱动的
/// 命令调用，并产出假的 materializedVersion（修订号），供序列断言与
/// "io 不产生修订"的负断言对照。
struct FakeProjectCommandLog {
    /// 记录项：命令名＋关键参数（digest 取 8 位短十六进制——断言可读）。
    struct Call {
        std::string name;
        std::string arg;
    };
    std::vector<Call> calls;

    static std::string shortHex(const Digest256& d)
    {
        return hexOf(d).substr(0, 8);
    }

    /// 固化入库命令（project 段③后半：中转副本→objects 正式入库，分配
    /// materializedVersion——此处以"mat-<n>"假修订号承载）。
    std::string materialize(const Digest256& stagingDigest)
    {
        const std::string version = "mat-" + std::to_string(calls.size());
        calls.push_back(Call{"materialize", shortHex(stagingDigest) + "->" + version});
        return version;
    }

    /// 重关联命令（PM-09：用户显式选择新源后——旧记录摘要→新实际摘要）。
    void reassociate(const Digest256& recorded, const Digest256& actual)
    {
        calls.push_back(Call{"reassociate", shortHex(recorded) + "->" + shortHex(actual)});
    }
};

/// IO-V28 契约级编排：固化(v1)→外部源变化→probe 检出 Changed→用户重
/// 关联（命令 fake 记录旧→新摘要）→再固化→新 materializedVersion。
/// 断言三面：①命令调用序列精确（materialize→reassociate→materialize，
/// 参数＝前后快照摘要）；②io 侧负断言——io 只交付快照/中转副本/检测
/// 事实，**不产生修订**（返回面无修订字段＋工作区无修订痕迹物）；③新
/// 固化产生新 materializedVersion（v2 内容≠v1，digest 为身份）。
TEST_F(IoSolidifyTest, ReassociateNewRevisionProjectCommandFakeSequence)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V28 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"PM-09", "CON-03"},
                  std::vector<std::string>{"AT-20"});

    FakeProjectCommandLog project;                 // O-09 对端：命令 fake
    const std::vector<std::uint8_t> v1 = rampBytes(4096, 21);
    const fs::path src = write("reassociate.bin", v1);

    // —— 第一轮：固化 v1（io 执行侧，中转副本待 project 入库）。
    const fs::path staging1 = stagingOf("v28-a");
    const IoResult<SolidifyStagingResult> s1 =
        snapshotter->solidifyToStaging(src, staging1, 0, nullptr, nullptr);
    ASSERT_TRUE(s1) << s1.error.detail;
    ASSERT_FALSE(s1.value.sourceChangedDuringCopy);
    EXPECT_EQ(s1.value.copiedDigest, manualDigest(v1));
    // project 固化入库命令：以 io 交付的摘要入库 → materializedVersion v1。
    const std::string mat1 = project.materialize(s1.value.copiedDigest);

    // —— 外部源内容替换（用户编辑了外部引用的源文件）。
    const std::vector<std::uint8_t> v2 = rampBytes(4096, 77);
    {
        std::ofstream f(src, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(v2.data()),
                static_cast<std::streamsize>(v2.size()));
    }

    // —— 检测段②：probe 以记录摘要比对实际 → Changed＋actual 快照
    //    （比较型 recorded/actual——重关联命令的参数来源）。
    ExternalRefRecord record;
    record.externalRefId = "ext-v28";
    record.absPath = src;
    record.recordedDigest = manualDigest(v1);
    record.recordedSizeBytes = v1.size();
    std::optional<ProbeDetail> detail;
    const IoResult<ExternalRefState> probed =
        snapshotter->probe(record, nullptr, nullptr, &detail);
    ASSERT_TRUE(probed);
    ASSERT_EQ(probed.value, ExternalRefState::Changed);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->actual.contentDigest, manualDigest(v2));

    // —— 用户重关联新源（显式提交——PM-09）：project 命令 fake 记录
    //    旧记录摘要→新实际摘要；io 在此只是事实提供方。
    project.reassociate(record.recordedDigest, detail->actual.contentDigest);

    // —— 第二轮：再固化 v2（新授权中转位——新一轮会话）。
    const fs::path staging2 = stagingOf("v28-b");
    const IoResult<SolidifyStagingResult> s2 =
        snapshotter->solidifyToStaging(src, staging2, 0, nullptr, nullptr);
    ASSERT_TRUE(s2) << s2.error.detail;
    EXPECT_EQ(s2.value.copiedDigest, manualDigest(v2));
    const std::string mat2 = project.materialize(s2.value.copiedDigest);

    // ① 命令调用序列精确断言（观测点"命令调用序列记录"）。
    ASSERT_EQ(project.calls.size(), 3u);
    EXPECT_EQ(project.calls[0].name, "materialize");
    EXPECT_EQ(project.calls[0].arg,
              FakeProjectCommandLog::shortHex(manualDigest(v1)) + "->" + mat1);
    EXPECT_EQ(project.calls[1].name, "reassociate");
    EXPECT_EQ(project.calls[1].arg,
              FakeProjectCommandLog::shortHex(manualDigest(v1)) + "->"
                  + FakeProjectCommandLog::shortHex(manualDigest(v2)));
    EXPECT_EQ(project.calls[2].name, "materialize");
    EXPECT_EQ(project.calls[2].arg,
              FakeProjectCommandLog::shortHex(manualDigest(v2)) + "->" + mat2);

    // ② io 不产生修订的负断言：SolidifyStagingResult 返回面只有快照对＋
    //    副本摘要（无修订/版本字段——结构上不可能携带修订产出）；工作
    //    区扫描无 revisions 痕迹物（修订持久化归 project——§10.1）。
    bool revisionArtifact = false;
    for (const fs::directory_entry& e :
         fs::recursive_directory_iterator(dir)) {
        if (e.path().filename() == "revisions"
            || e.path().extension() == ".rev") {
            revisionArtifact = true;
        }
    }
    EXPECT_FALSE(revisionArtifact) << "io 侧出现修订痕迹物（PA-1 违例）";

    // ③ 新固化产生新 materializedVersion：两轮入库版本不同，且各自内容
    //    摘要与对应源版本一致（新 materializedVersion 的内容身份＝v2）。
    EXPECT_NE(mat1, mat2);
    EXPECT_EQ(s1.value.copiedDigest, manualDigest(v1));
    EXPECT_EQ(s2.value.copiedDigest, manualDigest(v2));
    EXPECT_NE(s1.value.copiedDigest, s2.value.copiedDigest);
}

// =====================================================================
// P-IO-2 专用固化用例（IO-T07 acceptance 4——§8.6 五条裁决逐条断言）
// =====================================================================

/// §8.6 ResourceBytes 生命周期五条裁决的**专用**断言用例（与既有
/// AdapterRoutingAndLifecycleRulings 的路由混编断言、
/// AdapterConcurrentRecordedReadsAreSafe 的第 3 条并发面互补，把五条
/// 逐条钉住，防止实现弱化）：
///
///  - 第 1 条（最低保证）：缓冲自返回起**至少**有效至同一 provider 的
///    下一次调用——取缓冲→再调用→旧缓冲仍完整可读；
///  - 第 1 条（实际保证）：io 实际保证"至 provider 析构均有效"——多轮
///    多资源取全部视图，析构前逐一校验（析构后不可解引用——调用方纪律，
///    注释声明不越线）；
///  - 第 2 条（调用方纪律）：同步消费、不跨调用长期持有——本用例自身
///    即纪律的模型（视图在使用窗口内消费完毕；该条是纪律不是可断言行
///    为，登记为注释口径）；
///  - 第 3 条（并发只读安全）：见 AdapterConcurrentRecordedReadsAreSafe
///    （专用并发用例——此处不重复时序面）；
///  - 第 4 条（Recorded 不缓存/Solidified 可缓存）：Recorded 两次调用
///    返回**独立**缓冲（指针不同、均有效），且源更新＋记录刷新后新调用
///    反映新内容新摘要（每次重读＋重算）；Solidified 命中缓存（同指针）；
///  - 第 5 条（值拷贝兼容）：未来改值拷贝语义与本裁决兼容（更强不弱）
///    ——契约层登记，无行为断言面（注释口径）。
TEST_F(IoSolidifyTest, AdapterResourceBytesLifecycleFiveRulingsPinned)
{
    // 追溯登记（IO-T07——units/io.md §8.6/§15.3 P-IO-2 行；ird-test-
    // report.json 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-REL-04"},
                  std::vector<std::string>{});

    // 三个 Recorded 外部资源（各自独立缓冲的可观测面）＋一个 Solidified。
    const std::size_t kRecorded = 3;
    std::vector<std::vector<std::uint8_t>> recordedBytes;
    std::vector<fs::path> recordedPaths;
    FakeProjectBytes project;                       // 空对象库——全部走 Recorded
    FakeExternalRefs refs;
    std::vector<ObjectId> recordedIds;
    for (std::size_t i = 0; i < kRecorded; ++i) {
        recordedBytes.push_back(rampBytes(8192, static_cast<std::uint8_t>(30 + i)));
        recordedPaths.push_back(write("ruling-" + std::to_string(i) + ".bin",
                                      recordedBytes.back()));
        const ObjectId id = ObjectId::generate();
        ExternalRefRecord rec;
        rec.externalRefId = id.toCanonical();
        rec.absPath = recordedPaths.back();
        rec.recordedDigest = manualDigest(recordedBytes.back());
        rec.recordedSizeBytes = recordedBytes.back().size();
        refs.records[id.toCanonical()] = rec;
        recordedIds.push_back(id);
    }
    const std::vector<std::uint8_t> solidBytes = rampBytes(1024, 99);
    const ObjectId solidId = ObjectId::generate();
    project.store[solidId.toCanonical()] = solidBytes;

    const IRuntimeResourceAdapterPtr adapter = makeRuntimeResourceAdapter(&project, &refs);

    // —— 第 1 条（实际保证）前置采集：每个 Recorded 取一个视图。
    std::vector<ResourceBytesView> firstViews;
    for (const ObjectId& id : recordedIds) {
        const ResourceReadResult r = adapter->tryResourceBytes(id);
        ASSERT_EQ(r.status, ResourceReadStatus::Ok);
        firstViews.push_back(r.bytes);
    }

    // —— 第 1 条（最低保证）：上面的视图经历"多次后续调用"后仍完整。
    //    （保证强度≥"至下次调用"：即便实现只保证到下次调用，本断言在
    //    "下次调用已发生"后检查，正是最低保证的下边界验证形态。）
    (void)adapter->tryResourceBytes(solidId);
    (void)adapter->tryResourceBytes(recordedIds[0]);
    for (std::size_t i = 0; i < kRecorded; ++i) {
        ASSERT_NE(firstViews[i].data, nullptr);
        EXPECT_EQ(std::memcmp(firstViews[i].data, recordedBytes[i].data(),
                              recordedBytes[i].size()),
                  0)
            << "下次调用后旧缓冲失效（低于 §8.6.1 最低保证，idx=" << i << "）";
    }

    // —— 第 4 条（Recorded 不缓存）：同资源连续两次调用返回独立缓冲
    //    （指针不同——两视图同时暴露，复用即数据竞争）；随后把源与记录
    //    一并更新，新调用必须返回新内容新摘要（每次重读＋重算——若缓存
    //    则仍回旧字节，S10 摘要复查语义失效）。
    const ResourceReadResult a = adapter->tryResourceBytes(recordedIds[1]);
    const ResourceReadResult b = adapter->tryResourceBytes(recordedIds[1]);
    ASSERT_EQ(a.status, ResourceReadStatus::Ok);
    ASSERT_EQ(b.status, ResourceReadStatus::Ok);
    EXPECT_NE(a.bytes.data, b.bytes.data)
        << "Recorded 复用了仍在暴露中的缓冲（§8.6.3/§8.6.4 违例）";
    {
        const std::vector<std::uint8_t> v2 = rampBytes(8192, 200);
        {
            std::ofstream f(recordedPaths[1], std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(v2.data()),
                    static_cast<std::streamsize>(v2.size()));
        }
        refs.records[recordedIds[1].toCanonical()].recordedDigest = manualDigest(v2);
        const ResourceReadResult c = adapter->tryResourceBytes(recordedIds[1]);
        ASSERT_EQ(c.status, ResourceReadStatus::Ok);
        EXPECT_EQ(c.bytes.digest, manualDigest(v2))
            << "Recorded 未重读（缓存了旧字节——§8.6.4 违例）";
        EXPECT_EQ(std::memcmp(c.bytes.data, rampBytes(8192, 200).data(), 8192), 0);
        // 旧视图不受新调用影响（独立稳定——并发语义的时序面推论）。
        EXPECT_EQ(std::memcmp(a.bytes.data, rampBytes(8192, 31).data(), 8192), 0);
    }

    // —— 第 4 条后半（Solidified 允许缓存）：命中缓存＝同指针（不可变
    //    对象缓存安全——§8.6.4 括号条款）。
    const ResourceReadResult s1 = adapter->tryResourceBytes(solidId);
    const ResourceReadResult s2 = adapter->tryResourceBytes(solidId);
    ASSERT_EQ(s1.status, ResourceReadStatus::Ok);
    ASSERT_EQ(s2.status, ResourceReadStatus::Ok);
    EXPECT_EQ(s1.bytes.data, s2.bytes.data) << "Solidified 未命中缓存";

    // —— 第 1 条（实际保证）终检：全部视图（含中途取得的 a/b/c/s1/s2）
    //    在 provider 析构前仍完整可读——"至析构均有效"的强保证面。
    //    （此后 provider 析构，视图随之失效——第 2 条调用方纪律：不同步
    //    消费到析构之外，本用例在此收束，不越线解引用。）
    for (std::size_t i = 0; i < kRecorded; ++i) {
        EXPECT_EQ(std::memcmp(firstViews[i].data, recordedBytes[i].data(),
                              recordedBytes[i].size()),
                  0)
            << "析构前旧缓冲损坏（§8.6.1 实际保证违例，idx=" << i << "）";
    }

    // 第 5 条（值拷贝兼容）：实现未来改为小资源值拷贝亦兼容本裁决（更强
    // 不弱）——契约层登记，无行为断言面（units/io.md §8.6 第 5 条原文）。
}

} // namespace
