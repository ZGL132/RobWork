/**
 * @file   BundleCrossUnitContractTest.cpp
 * @brief  证据包组装跨单元契约测试（RPT-T10）——IArchiveWriter 契约形状
 *         （§9.6 逐签名）＋条目名 SP-2 通道边界＋证据项状态词面稳定性＋
 *         P-RPT-7 处置自证（不私建第二套 ZIP 实现）＋包格式与项目镜像的
 *         语义分界（D-15）。
 *
 * 设计依据：
 *   - units/reporting.md §7.6（要素边界："数据契约归 evidence／打包格式与
 *     组装归 reporting／ZIP 字节封装归 io（注入）"、"bundle 不是第二套项目
 *     格式〔不含 HEAD/锁/.staging〕"）、§9.6（IArchiveWriter 契约原文——
 *     三方法签名与非法调用行"条目名绝对/上溯〔io SP-2 拒绝〕"）、§13
 *     （RPT-03 行——设计落点 §7.6/§9.6）、§14.3 P-RPT-7/P-RPT-9（处置
 *     约束——注入薄适配；evidence 消费以 v0.1 Draft 为基线）
 *   - 任务契约 tasks/foundation/RPT-T10.json acceptance 4~5
 *
 * 契约声明（acceptance 4/5 逐项）：
 *   ①IArchiveWriter 三方法签名与 §9.6 契约逐参一致（static_assert——签名
 *     漂移即编译失败，io 侧适配的对接凭据）；
 *   ②条目名通道边界（SP-2）：绝对/反斜杠/上溯段/空名/重复名一律拒绝——
 *     经 FakeArchiveWriter 的契约承载逐形态验证（io 真实实现的拒绝行为
 *     归 io 验证矩阵，本文件钉 reporting 侧可依赖的契约面）；
 *   ③evidence::EvidenceItemStatus 五值的包内词面稳定且互异（持久化判别
 *     字段；类型恒等由 static_assert 钉死——P-RPT-9 冻结 diff 的增量
 *     同步触发器）；
 *   ④P-RPT-7：reporting 单元源码零第二套 ZIP 实现（zip 字节级封装只经
 *     注入的 IArchiveWriter——源码扫描常驻自证，第三方依赖唯一渠道经
 *     vcpkg 归 io〔NFR-DEP-03〕）；
 *   ⑤D-15/P-RPT-7 格式分界：bundle schema 恒为 ird-evidence-bundle/1、
 *     条目编址由报告引用运行的规范文本构成——结构上不含项目镜像要素
 *     （HEAD/锁/.staging 的条目名构造面在 reporting 侧不存在）。
 */

#include <sdurws/ird/reporting/Bundle.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include "../test/FakeArchiveWriter.hpp"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;
using test_fakes::FakeArchiveWriter;

// =====================================================================
// ①IArchiveWriter 契约形状（§9.6 原文逐签名——acceptance 5 的对接凭据）
// =====================================================================

// open：路径入参、无返回（§9.6 原文签名）。
using OpenFn = void (IArchiveWriter::*)(const std::filesystem::path&);
static_assert(std::is_same<decltype(&IArchiveWriter::open), OpenFn>::value,
              "IArchiveWriter::open 签名与 §9.6 契约逐参一致（io 薄适配的对接凭据）");

// addEntry：条目名＋完整字节（§9.6 原文签名——"纯相对正斜杠〔SP-2 口径〕"）。
using AddEntryFn = void (IArchiveWriter::*)(const std::string&,
                                            const std::vector<std::uint8_t>&);
static_assert(std::is_same<decltype(&IArchiveWriter::addEntry), AddEntryFn>::value,
              "IArchiveWriter::addEntry 签名与 §9.6 契约逐参一致");

// finish：返回 core::Digest256（容器级摘要——§9.6 原文签名；"原子替换到
// 目标；异常/失败→清理临时、目标不变"注释随头文件契约）。
using FinishFn = core::Digest256 (IArchiveWriter::*)();
static_assert(std::is_same<decltype(&IArchiveWriter::finish), FinishFn>::value,
              "IArchiveWriter::finish 签名与 §9.6 契约逐参一致");

// P-RPT-9 类型恒等：报告复现要素列恰为 evidence::ReproductionBlock（§4.2
// 复现要素字段的值拷贝——零复制/零重定义，消费侧类型漂移即编译失败）。
using ReproFn = const evidence::ReproductionBlock& (ReviewReport::*)() const noexcept;
static_assert(std::is_same<decltype(&ReviewReport::reproduction), ReproFn>::value,
              "report.reproduction() 必须返回 evidence::ReproductionBlock（P-RPT-9 基线）");

// 包络只读值注入面（§7.6 输入①同名方法——与 BuilderCrossUnitContractTest
// 双面互证；此处从证据包消费侧再钉一次）。
using TryEnvelopeFn = std::optional<evidence::ResultEnvelope> (IReportResultSource::*)(
    core::RunId) const;
static_assert(std::is_same<decltype(&IReportResultSource::tryEnvelope), TryEnvelopeFn>::value,
              "tryEnvelope 必须返回 evidence::ResultEnvelope 只读值（§7.6 输入①）");

// =====================================================================
// ③evidence::EvidenceItemStatus 五值词面（稳定性＋互异性——持久化判别面）
// =====================================================================

/// 包内词面全表（与 bundleItemStatusToken 的映射逐项核对）。
constexpr std::array<evidence::EvidenceItemStatus, 5> kAllItemStatuses = {
    evidence::EvidenceItemStatus::Satisfied,   evidence::EvidenceItemStatus::Missing,
    evidence::EvidenceItemStatus::Invalid,     evidence::EvidenceItemStatus::Unverified,
    evidence::EvidenceItemStatus::NotApplicable,
};

/// 证据项状态词面：五值全表逐值非空、两两互异（JSON 判别字段——撞词会使
/// 两种状态在包内不可区分）。
TEST(BundleCrossUnitContractTest, ItemStatusTokensAreStableAndDistinct)
{
    std::vector<std::string_view> tokens;
    tokens.reserve(kAllItemStatuses.size());
    for (const evidence::EvidenceItemStatus status : kAllItemStatuses) {
        const std::string_view token = bundleItemStatusToken(status);
        ASSERT_FALSE(token.empty()) << "词面为空（判别字段违约）";
        tokens.push_back(token);
    }
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        for (std::size_t j = i + 1; j < tokens.size(); ++j) {
            EXPECT_NE(tokens[i], tokens[j]) << "词面撞车（判别字段违约）";
        }
    }
    // 稳定字面钉值（一经交付不得改动——Bundle.hpp 契约注释的常驻自证）。
    EXPECT_EQ(bundleItemStatusToken(evidence::EvidenceItemStatus::Satisfied), "satisfied");
    EXPECT_EQ(bundleItemStatusToken(evidence::EvidenceItemStatus::Missing), "missing");
    EXPECT_EQ(bundleItemStatusToken(evidence::EvidenceItemStatus::Invalid), "invalid");
    EXPECT_EQ(bundleItemStatusToken(evidence::EvidenceItemStatus::Unverified), "unverified");
    EXPECT_EQ(bundleItemStatusToken(evidence::EvidenceItemStatus::NotApplicable),
              "not-applicable");
}

/// 包 schema 恒为 ird-evidence-bundle/1（§7.6 原文字面——独立格式标识，
/// 不复用 .rwpack/ird-report 任一既有格式名——D-15/P-RPT-7 分界的格式面）。
TEST(BundleCrossUnitContractTest, BundleSchemaVersionIsIndependentFormat)
{
    EXPECT_EQ(kBundleSchemaVersion, "ird-evidence-bundle/1");
    EXPECT_NE(kBundleSchemaVersion, "ird-report-json/1");   // 报告 JSON≠证据包
    EXPECT_EQ(kBundleManifestName, "bundle.json");
    EXPECT_EQ(kBundleReproductionName, "reproduction.json");
}

// =====================================================================
// ②条目名通道边界（SP-2）——FakeArchiveWriter 的契约承载逐形态验证
// =====================================================================

/// SP-2 拒绝矩阵：绝对路径/盘符/反斜杠/".." 上溯段/空名——五形态全部
/// std::invalid_argument（§9.6 非法调用行"条目名绝对/上溯〔io SP-2 拒绝〕"
/// 的 reporting 侧契约面；reporting 侧依赖的拒绝行为在此钉死）。
TEST(BundleCrossUnitContractTest, EntryNameViolatesSp2IsRejected)
{
    FakeArchiveWriter writer;
    const std::filesystem::path target =
        std::filesystem::temp_directory_path() / "ird-bundle-contract-sp2.zip";
    writer.open(target);
    const std::vector<std::uint8_t> bytes{0x01};

    EXPECT_THROW(writer.addEntry("/absolute/entry.json", bytes), std::invalid_argument);
    EXPECT_THROW(writer.addEntry("C:\\win\\entry.json", bytes), std::invalid_argument);
    EXPECT_THROW(writer.addEntry("snapshot/../evil.json", bytes), std::invalid_argument);
    EXPECT_THROW(writer.addEntry("..\\evil.json", bytes), std::invalid_argument);
    EXPECT_THROW(writer.addEntry("", bytes), std::invalid_argument);

    // 拒绝路径零副作用：合法条目仍可写入（只增面未被违约调用破坏）。
    writer.addEntry("bundle.json", bytes);
    EXPECT_EQ(writer.entryNames.size(), std::size_t{1});

    // 重复名拒绝（只增纪律）。
    EXPECT_THROW(writer.addEntry("bundle.json", bytes), std::invalid_argument);

    std::error_code ec;
    std::filesystem::remove(target, ec);   // 会话未 finish——析构 RAII 清理临时
}

/// 会话纪律：未 open 直接 addEntry/finish＝违约（logic_error）；finish 后
/// 再 addEntry＝违约（会话终结）。
TEST(BundleCrossUnitContractTest, SessionDisciplineIsEnforced)
{
    FakeArchiveWriter writer;
    const std::filesystem::path target =
        std::filesystem::temp_directory_path() / "ird-bundle-contract-session.zip";
    const std::vector<std::uint8_t> bytes{0x01};

    EXPECT_THROW(writer.addEntry("x.json", bytes), std::logic_error);
    EXPECT_THROW(writer.finish(), std::logic_error);

    writer.open(target);
    writer.addEntry("x.json", bytes);
    const core::Digest256 digest = writer.finish();
    (void)digest;   // 容器级摘要（替身自有规范化域——装配器不消费）
    EXPECT_THROW(writer.addEntry("y.json", bytes), std::logic_error);
    EXPECT_THROW(writer.finish(), std::logic_error);
    EXPECT_TRUE(std::filesystem::exists(target));   // 发布完成——目标在

    // 终态后重开＝新会话（合法）；活跃会话中重复 open＝违约（logic_error）。
    writer.open(target);
    auto reopenActive = [&writer, &target]() {
        writer.open(target);
    };
    EXPECT_THROW(reopenActive(), std::logic_error);

    std::error_code ec;
    std::filesystem::remove(target, ec);
}

/// 放弃路径（RAII）：open 后不 finish 直接析构→临时清理、目标不变
/// （Bundle.hpp 文件头"IArchiveWriter 的放弃路径"节——实现义务的替身承载）。
TEST(BundleCrossUnitContractTest, AbandonWithoutFinishCleansTempAndKeepsTarget)
{
    const std::filesystem::path target =
        std::filesystem::temp_directory_path() / "ird-bundle-contract-abandon.zip";
    {
        FakeArchiveWriter writer;
        writer.open(target);
        writer.addEntry("snapshot/run-x/snapshot.json", {0x01, 0x02});
        ASSERT_FALSE(writer.tempPath().empty());
        EXPECT_TRUE(std::filesystem::exists(writer.tempPath()));
        EXPECT_FALSE(std::filesystem::exists(target));   // 未发布——目标不存在
    }   // 析构——RAII 清理临时。
    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path{target.string() + ".ird-fake-partial"}));
    EXPECT_FALSE(std::filesystem::exists(target));
}

/// 条目名编址面（reporting 侧构造面）：snapshot/results 条目名恒为纯相对
/// 正斜杠（runId 规范文本字符集受限——SP-2 安全的结构性来源），且不含
/// 项目镜像要素（D-15/P-RPT-7——HEAD/锁/.staging 的构造面在 reporting
/// 侧不存在）。
TEST(BundleCrossUnitContractTest, EntryAddressingIsRelativeAndProjectMirrorFree)
{
    core::RunId run = core::RunId::tryFromCanonical(
        "run-0123456789abcdef0123456789abcdef").value();

    const std::string snap = bundleSnapshotEntryName(run);
    const std::string result = bundleResultEntryName(run);
    for (const std::string* name : {&snap, &result}) {
        EXPECT_FALSE(name->empty());
        EXPECT_NE(name->front(), '/')    // 相对名（不以分隔符起头——绝对形态拒绝）
            << "条目名以分隔符起头＝绝对形态（SP-2 拒绝面）";
        EXPECT_EQ(name->find('\\'), std::string::npos);   // 纯正斜杠
        EXPECT_EQ(name->find(':'), std::string::npos);    // 无盘符
        EXPECT_EQ(name->find(".."), std::string::npos);   // 无上溯段
        EXPECT_NE(name->find(run.toCanonical()), std::string::npos);   // 按运行编址
    }
    EXPECT_EQ(snap, "snapshot/" + run.toCanonical() + "/snapshot.json");
    EXPECT_EQ(result, "results/" + run.toCanonical() + "/result.json");
    // 项目镜像要素（.rwpack 语义——HEAD/锁/.staging）不在任何构造面。
    EXPECT_EQ(snap.find("HEAD"), std::string::npos);
    EXPECT_EQ(snap.find("lock"), std::string::npos);
    EXPECT_EQ(snap.find(".staging"), std::string::npos);
}

#if defined(IRD_REPORTING_UNIT_ROOT)
// =====================================================================
// ④P-RPT-7 处置自证：reporting 源码零第二套 ZIP 实现（扫描常驻）
// =====================================================================

/// 源码树文件清单（.hpp/.cpp/.txt——IRD_REPORTING_UNIT_ROOT 注入根）。
std::vector<std::filesystem::path> collectSources(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string ext = it->path().extension().string();
        if (ext == ".hpp" || ext == ".cpp" || ext == ".txt") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// P-RPT-7：reporting 单元源码零 ZIP 字节级封装的第二实现——容器封装只经
/// 注入的 IArchiveWriter（io 薄适配）。扫描 ZIP 实现特征（zlib/minizip/
/// libzip 头与压缩方法调用——字符级特征；第三方依赖唯一渠道经 vcpkg 归
/// io，NFR-DEP-03）。
TEST(BundleCrossUnitContractTest, NoSecondZipImplementationInReportingSources)
{
    // 扫描域＝reporting 单元源码树（注入根为 industrialrobot 目录——其余
    // 单元〔如 io 的 ZipChannel〕合法使用 ZIP 属其职责面，不在本红线内）。
    const std::filesystem::path root =
        std::filesystem::path{IRD_REPORTING_UNIT_ROOT} / "reporting";
    ASSERT_TRUE(std::filesystem::exists(root)) << "reporting 源码树不存在";
    const std::vector<std::string_view> forbidden = {
        "zlib.h", "unzip.h", "minizip", "libzip", "zipOpen", "zipOpenNewFileInZip",
        "deflateInit", "inflateInit",
    };
    for (const std::filesystem::path& file : collectSources(root)) {
        // 跳过本文件：禁词清单的字面量本身就在本测试源内（自匹配排除——
        // 扫描对象是"实现源码"，本文件是红线声明体）。
        if (file.filename() == std::filesystem::path(__FILE__).filename()) {
            continue;
        }
        const std::string text = readFile(file);
        for (const std::string_view needle : forbidden) {
            EXPECT_EQ(text.find(needle), std::string::npos)
                << "P-RPT-7 红线：reporting 源码出现 ZIP 实现特征 " << needle << "（"
                << file.filename().string() << "）——容器封装只经注入 IArchiveWriter";
        }
    }
}
#endif

}  // namespace
