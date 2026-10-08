/**
 * @file   SaveAsPackageContractTest.cpp
 * @brief  另存为与包导出/导入编排的契约测试（WF-VER-213~215——units/
 *         workflow.md §11.2 生命周期主线；PM-05/AT-20 的真实落盘承载半区）。
 *
 * 设计依据：
 *   - units/workflow.md §7.4（另存为＝完整目录复制〔results/reports/
 *     drafts 勾选、记忆默认〕＋换新 projectId 后按打开协议进入；包导出
 *     ＝.rwpack ZIP 传输封装、后台进度可取消、取消即清理临时区；包导入
 *     ＝预算/路径穿越防护与全量校验、失败不留目标目录并给出校验报告）、
 *     §11.2（213 另存为换新 projectId〔观测点＝目录/设置〕、214 包导出
 *     取消清理〔观测点＝临时目录〕、215 包导入校验失败不留目录〔观测点
 *     ＝校验报告〕）、§11.3 AT-20 承接行
 *   - REQUIREMENTS.md §17 PM-05 原文、AT-20（向导取消不留半成品）、
 *     NFR-SEC-01/02（SA-14 导入安全三件套——路径校验/展开预算/内容
 *     完整性的执行面在 io，本文件以真实 io importer 联合验证）
 *   - io.md §7.2/§7.3/§7.4/§7.7（导出六步协议/导入九步协议/威胁处置
 *     矩阵/责任切分——io 承接校验与清理、发布⑧归 project；本文件的
 *     端口桩是 L5 装配桥接的测试等价物，发布半区以显式声明的桩动作
 *     模拟——WP-04-T18 存储侧契约未生成，其落位后由真实发布替换）
 *   - 任务契约 tasks/foundation/WP-22-T07.json acceptance 1/2/3
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：本文件与 project
 * 真实存储实现（ProjectStoreFactory::createNew/open＋DraftService 真实
 * 落盘）和 io 真实包设施（makePackageExporter/makePackageImporter——
 * ZIP 封装/预算/路径穿越防护/全量校验为真实执行）联合；编排核对端口的
 * 消费契约（取消令牌/进度贯通/清理观测位/勾选裁剪）以真实 IO 行为验证。
 * 恶意包样例（路径穿越）以手工 zip 字节流构造（自持不跨文件共享——
 * io PackageContractTest 同款口径，零第三方写侧依赖）。
 *
 * "失败不留目标目录""取消即清理"不是声明：以真实盘面存在性复核
 * （fs::exists）＋桩清理观测位双重承载。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>   // core::ContentDigester/Digest256（manifest 摘要——SA-12 唯一算法）
#include <sdurws/ird/io/Budget.hpp>     // io::BudgetSpec::packImportHardened（包导入四维硬限——产品默认）
#include <sdurws/ird/io/IoDiagnostics.hpp>  // io::errorCodeToken（对端稳定码 token 词形）
#include <sdurws/ird/io/IoError.hpp>    // io::IoError/IoErrorCode（错误轨道折叠）
#include <sdurws/ird/io/IoFwd.hpp>      // io::IoCancelToken/IoProgress（L5 桥接对端面）
#include <sdurws/ird/io/Json.hpp>       // io canonical JSON 写出（rwpack.json/manifest 构造）
#include <sdurws/ird/io/Package.hpp>    // io 包设施（PackFormat/makePackageExporter/Importer/ISnapshotFileSource）
#include <sdurws/ird/project/DraftService.hpp>  // DraftService（草稿真实落盘——drafts 树裁剪验证）
#include <sdurws/ird/project/PersistenceFormat.hpp>  // DraftDocument/DraftOrigin（草稿构造）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using workflow::FlowProgressStage;
using workflow::IFlowCancelToken;
using workflow::PackageImportDiagnosticLine;
using workflow::PackageImportEntryLine;
using workflow::PackageImportExecution;

// =====================================================================
// 夹具：临时目录（套件级总根＋用例级独立目录——CloseFlowContract 同型）
// =====================================================================

class SaveAsPackageContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_saveas_contract_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);  // 前次运行残留防御（总根重建）
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);  // 失败保留现场惯例——总根清理
    }

    void SetUp() override
    {
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec);
    }

    /// 目录树字节面快照（相对路径 UTF-8 → 文件大小）。排除 lock 文件：
    /// 持有方心跳线程按固定周期原地重写（project.md §9.4——内容变化是
    /// 持有中的正常事实；CloseFlowContract::snapshotTree 同款口径）。
    static std::map<std::string, std::uintmax_t> snapshotTree(const fs::path& root)
    {
        std::map<std::string, std::uintmax_t> snapshot;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) {
                continue;
            }
            const std::string name = it->path().filename().string();
            if (name == "lock") {
                continue;  // 心跳重写面排除（见函数注）
            }
            snapshot[it->path().lexically_relative(root).u8string()]
                = it->file_size(ec);
        }
        return snapshot;
    }

    /// 以 createNew 产出黄金项目（真实落盘——调用方持有 store，释放写锁
    /// 须 requestClose＋reset，见各用例尾注）。
    static project::OpenStoreResult createGolden(const fs::path& dir)
    {
        return project::ProjectStoreFactory::createNew(dir, "另存包编排黄金项目");
    }

    /// 在真实存储上下文的活动分支上落盘一份草稿（CloseFlowContract 同款
    /// 手法——drafts/ 树真实存在，勾选裁剪的盘面验证料）。
    static fs::path saveDraftViaService(project::ProjectStore& store,
                                        const std::string& moduleId)
    {
        const auto tips = store.query().branchTips();
        if (tips.empty()) {
            ADD_FAILURE() << "夹具前置失败：分支清单为空";
            return {};
        }
        project::DraftDocument doc;
        doc.projectId = store.projectId();
        doc.branchId = tips[0].id;
        doc.moduleId = moduleId;
        doc.baseRevisionId = tips[0].tip;
        doc.payload = "{\"note\":\"saveas-package-contract\"}";
        doc.savedAtUtc = "2026-10-08T00:00:00Z";
        doc.origin = project::DraftOrigin::Manual;
        const project::SaveResult saved = store.drafts().save(doc);
        if (!saved.ok) {
            ADD_FAILURE() << "草稿落盘失败（夹具前置）: "
                          << (saved.error ? saved.error->what() : "?");
            return {};
        }
        return saved.file;
    }

    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dir;
};

fs::path SaveAsPackageContract::s_base;
int SaveAsPackageContract::s_caseCounter = 0;

// =====================================================================
// 手工包构造（自持不跨文件共享——io PackageContractTest 同款口径；
// 手工 zip 字节流＝STORED 条目＋中央目录＋EOCD，零第三方写侧依赖）
// =====================================================================

/// 摘要字节 → 十六进制（小写——io §7.1 totalDigest 词形）。
std::string digestBytesToHex(const core::Digest256& digest)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : digest) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

std::string sha256HexOf(const std::string& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return digestBytesToHex(d.finalize());
}

/// JSON 值/成员本地构造助手（io Json 公共结构——PackageContractTest 同款）。
io::JsonValue jsonStr(std::string s)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::String;
    v.stringValue = std::move(s);
    return v;
}

io::JsonValue jsonInt(std::int64_t i)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::Integer;
    v.integerValue = i;
    return v;
}

void addMember(io::JsonValue& obj, std::string key, io::JsonValue value)
{
    io::JsonMember m;
    m.key = std::move(key);
    m.value = std::move(value);
    obj.members.push_back(std::move(m));
}

/// manifest canonical 字节（entries 按 path 字典序——io §7.1 契约）。
std::string buildManifestJson(
    const std::vector<std::pair<std::string, std::string>>& files,
    std::string* outDigestHex)
{
    std::vector<std::pair<std::string, std::string>> sorted = files;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    io::JsonDocument doc;
    doc.root.type = io::JsonValue::Type::Object;
    addMember(doc.root, "schemaVersion", jsonStr(io::PackFormat::kManifestSchemaVersion));
    io::JsonValue entries;
    entries.type = io::JsonValue::Type::Array;
    for (const auto& kv : sorted) {
        io::JsonValue item;
        item.type = io::JsonValue::Type::Object;
        addMember(item, "path", jsonStr(kv.first));
        addMember(item, "size", jsonInt(static_cast<std::int64_t>(kv.second.size())));
        addMember(item, "sha256", jsonStr(sha256HexOf(kv.second)));
        entries.items.push_back(std::move(item));
    }
    addMember(doc.root, "entries", std::move(entries));
    auto bytes = io::canonicalizeJson(doc, io::JsonWriteOptions{});
    EXPECT_TRUE(bytes);
    auto digest = io::digestCanonicalJson(doc, io::JsonWriteOptions{});
    EXPECT_TRUE(digest);
    if (outDigestHex != nullptr) {
        *outDigestHex = digestBytesToHex(digest.value);
    }
    return bytes.value;
}

/// rwpack.json canonical 字节（§7.1 契约四元数据＋content 汇总）。
std::string buildRwpackJson(const std::string& totalDigestHex, std::uint64_t fileCount,
                            std::uint64_t totalBytes)
{
    io::JsonDocument doc;
    doc.root.type = io::JsonValue::Type::Object;
    addMember(doc.root, "formatId", jsonStr(io::PackFormat::kFormatId));
    addMember(doc.root, "schemaVersion",
              jsonInt(static_cast<std::int64_t>(io::PackFormat::kSchemaVersion)));
    addMember(doc.root, "createdAtUtc", jsonStr("2026-10-08T00:00:00Z"));
    addMember(doc.root, "createdWithToolVersion", jsonStr("contract-test/1"));
    addMember(doc.root, "sourceProjectId", jsonStr("prj-contract-test"));
    io::JsonValue content;
    content.type = io::JsonValue::Type::Object;
    addMember(content, "headRevisionId", jsonStr("rev-contract-test"));
    addMember(content, "fileCount", jsonInt(static_cast<std::int64_t>(fileCount)));
    addMember(content, "totalBytes", jsonInt(static_cast<std::int64_t>(totalBytes)));
    addMember(content, "totalDigest", jsonStr(totalDigestHex));
    addMember(doc.root, "content", std::move(content));
    auto bytes = io::canonicalizeJson(doc, io::JsonWriteOptions{});
    EXPECT_TRUE(bytes);
    return bytes.value;
}

/// CRC-32（IEEE 802.3，zip 口径——逐位独立实现，测试面正确性优先）。
std::uint32_t crc32Of(const std::string& s)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char ch : s) {
        crc ^= static_cast<std::uint8_t>(ch);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) != 0 ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

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

/// 手工字节 zip（STORED 条目＋中央目录＋EOCD——恶意样例构造载体）。
std::string handcraftZip(const std::vector<std::pair<std::string, std::string>>& entries)
{
    struct CdRow {
        std::string name;
        std::uint32_t crc;
        std::uint32_t size;
        std::uint32_t offset;
    };
    std::string out;
    std::vector<CdRow> cd;
    for (const auto& e : entries) {
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        out.append("PK\x03\x04", 4);
        appendU16(out, 20);   // 版本
        appendU16(out, 0);    // flags
        appendU16(out, 0);    // method＝STORED
        appendU16(out, 0);    // time
        appendU16(out, 0);    // date
        appendU32(out, crc32Of(e.second));
        appendU32(out, static_cast<std::uint32_t>(e.second.size()));
        appendU32(out, static_cast<std::uint32_t>(e.second.size()));
        appendU16(out, static_cast<std::uint16_t>(e.first.size()));
        appendU16(out, 0);    // 扩展区
        out.append(e.first);
        out.append(e.second);
        cd.push_back(CdRow{e.first, crc32Of(e.second),
                           static_cast<std::uint32_t>(e.second.size()), offset});
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

/// 从负载集构造 .rwpack 包文件（manifest/rwpack canonical＋手工 zip）。
fs::path buildPackFromPayload(
    const fs::path& file,
    const std::vector<std::pair<std::string, std::string>>& payload)
{
    std::uint64_t totalBytes = 0;
    for (const auto& kv : payload) {
        totalBytes += kv.second.size();
    }
    // totalDigest＝canonical manifest 摘要的十六进制（rwpack.json content）。
    std::string manifestDigestHex;
    const std::string manifestJson = buildManifestJson(payload, &manifestDigestHex);
    const std::string rwpackJson =
        buildRwpackJson(manifestDigestHex, payload.size(), totalBytes);
    std::vector<std::pair<std::string, std::string>> entries;
    entries.emplace_back(io::PackFormat::kRwpackEntryName, rwpackJson);
    entries.emplace_back(io::PackFormat::kManifestEntryName, manifestJson);
    for (const auto& kv : payload) {
        entries.emplace_back(kv.first, kv.second);
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const std::string zipBytes = handcraftZip(entries);
    std::ofstream outF(file, std::ios::binary);
    EXPECT_TRUE(outF.is_open()) << file.string();
    outF.write(zipBytes.data(), static_cast<std::streamsize>(zipBytes.size()));
    return file;
}

/// 标准镜像负载集（§7.1 恒在集——payload/ 前缀形态）。
std::vector<std::pair<std::string, std::string>> standardPayload()
{
    return {
        {"payload/HEAD", std::string("rev-contract-test\n")},
        {"payload/project.json", std::string("{\"projectId\":\"prj-contract-test\"}")},
        {"payload/revisions/rev-contract-test.json", std::string("{\"revision\":1}")},
    };
}

// =====================================================================
// 取消令牌（workflow IFlowCancelToken 内存实现——预置取消注入面）
// =====================================================================

class MemoryCancelLike final : public workflow::IFlowCancelToken {
public:
    bool cancelRequested() const override { return m_flag; }
    void requestCancel() override { m_flag = true; }

private:
    bool m_flag = false;///< 取消标志（置位后恒真——实现契约）
};

// =====================================================================
// 端口桩一：另存真实复制（L5 桥接的测试等价物——真实 fs::copy 按勾选
// 裁剪＋取消检查点清理；新 projectId 分配/写入 project.json 归 project
// 存储侧 WP-04-T18，本桩不代写——存储侧契约落位后由真实实现替换）。
// =====================================================================

class RealCopySaveAsPort final : public workflow::ISaveAsPort {
public:
    Execution executeCopy(project::ProjectStore& source,
                          const workflow::SaveAsRequest& request,
                          IFlowCancelToken* cancel,
                          const workflow::FlowProgressCallback& progress) override
    {
        Execution e;
        const fs::path sourceDir = source.canonicalPath();
        const fs::path targetDir = request.targetDir;

        // 复制器前置：目标目录就位（copy_file 需要父目录存在）。
        std::error_code makeDirEc;
        fs::create_directories(targetDir, makeDirEc);
        if (makeDirEc) {
            e.cause = "目标目录创建失败: " + makeDirEc.message();
            return e;  // copied=false——环境失败
        }

        // 递归复制器：逐条目检查取消点（目录/文件两级——协作取消语义）；
        // 排除 lock 心跳文件（复制中的心跳内容无意义且不稳定）。
        std::error_code ec;
        std::uint64_t copied = 0;
        // 可选树勾选裁剪（PM-05）：未勾选的顶层树不进入复制遍历——
        // "results/reports/drafts 勾选"的执行面语义。
        std::map<std::string, bool> topIncluded;
        topIncluded["results"] = request.selection.includeResults;
        topIncluded["reports"] = request.selection.includeReports;
        topIncluded["drafts"] = request.selection.includeDrafts;

        // 先复制镜像必备顶层文件（project.json/HEAD 等源根直接文件）。
        for (auto it = fs::directory_iterator(sourceDir, ec);
             it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            const std::string name = it->path().filename().string();
            if (name == "lock" || it->is_directory(ec)) {
                continue;  // 心跳排除；目录进入下方按勾选裁剪
            }
            fs::copy_file(it->path(), targetDir / name,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) {
                e.cause = "复制失败: " + it->path().string();
                cleanupTarget(targetDir);
                e.targetLeftClean = !fs::exists(targetDir);
                return e;  // copied=false——失败零残留
            }
            ++copied;
            if (progress) {
                progress(FlowProgressStage{"copy", copied, 0});
            }
            if (cancel != nullptr && cancel->cancelRequested()) {
                return cancelledResult(targetDir);  // 取消即清理（AT-20）
            }
        }
        // 再按勾选裁剪复制可选顶层树（results/reports/drafts＋其他未知
        // 目录按"复制"处理——完整目录复制语义的兜底面）。
        for (auto it = fs::directory_iterator(sourceDir, ec);
             it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            if (!it->is_directory(ec)) {
                continue;
            }
            const std::string name = it->path().filename().string();
            const auto known = topIncluded.find(name);
            if (known != topIncluded.end() && !known->second) {
                continue;  // 未勾选的可选树——裁剪（勾选语义的执行落点）
            }
            fs::copy(it->path(), targetDir / name,
                     fs::copy_options::recursive
                         | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                e.cause = "树复制失败: " + it->path().string();
                cleanupTarget(targetDir);
                e.targetLeftClean = !fs::exists(targetDir);
                return e;
            }
            if (progress) {
                progress(FlowProgressStage{"copy-tree", 1, 1});
            }
            if (cancel != nullptr && cancel->cancelRequested()) {
                return cancelledResult(targetDir);
            }
        }
        e.copied = true;
        e.targetLeftClean = true;  // 成功路径目标就位（观测位语义为"无残留待清理"）
        return e;
    }

private:
    static void cleanupTarget(const fs::path& targetDir)
    {
        std::error_code ec;
        fs::remove_all(targetDir, ec);  // 取消/失败清理——尽力删除
    }

    static Execution cancelledResult(const fs::path& targetDir)
    {
        Execution e;
        e.cancelled = true;
        cleanupTarget(targetDir);  // "取消即清理"（AT-20）——桩执行面承诺
        e.targetLeftClean = !fs::exists(targetDir);  // 真实盘面观测位
        return e;
    }
};

// =====================================================================
// 端口桩二：包导出真实 io 桥接（L5 装配层的测试等价物——io 真实导出器
// ＋目录快照源；取消/进度令牌按 P-IO-1 桥接形态适配）。
// =====================================================================

/// workflow 取消令牌 → io IoCancelToken 适配（L5 桥接面——公共头零 io
/// 类型的落地证明：适配只存在于测试/L5 侧，编排核与公共头零 io 类型）。
class IoCancelAdapter final : public io::IoCancelToken {
public:
    explicit IoCancelAdapter(IFlowCancelToken* flow) : m_flow(flow) {}
    bool isCancelled() const override
    {
        return m_flow != nullptr && m_flow->cancelRequested();
    }

private:
    IFlowCancelToken* m_flow;///< 非 owning——workflow 侧令牌
};

/// 目录一致快照源（project ISnapshotFileSource 实现的测试等价物——从
/// 磁盘目录枚举＋读文件；排除 lock 心跳与 .staging 组装区〔非已提交
/// 内容——io §7.2②"清单外文件一概不读"的清单侧约束〕）。
class DirectorySnapshotSource final : public io::ISnapshotFileSource {
public:
    explicit DirectorySnapshotSource(fs::path root) : m_root(std::move(root)) {}

    io::IoResult<std::vector<io::PackFileEntry>> enumerate() const override
    {
        io::IoResult<std::vector<io::PackFileEntry>> out;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(m_root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                out.error.code = io::IoErrorCode::ResNotFound;
                out.error.detail = "目录遍历失败";
                return out;
            }
            if (!it->is_regular_file(ec) || ec) {
                continue;
            }
            const std::string name = it->path().filename().string();
            if (name == "lock") {
                continue;  // 心跳文件不是已提交内容
            }
            // 包内相对名＝正斜杠分隔（io §7.1 包内 path 词形——zip 条目
            // 名惯例；Windows 的 lexically_relative 产出反斜杠，显式转换）。
            std::string rel = it->path().lexically_relative(m_root).u8string();
            std::replace(rel.begin(), rel.end(), '\\', '/');
            if (rel.rfind(".staging", 0) == 0
                || rel.find("\\.staging") != std::string::npos
                || rel.find("/.staging") != std::string::npos) {
                continue;  // 组装区残留非快照闭包（D-17 已提交面）
            }
            io::PackFileEntry entry;
            // 包内名＝payload/ 前缀＋源相对名（io §7.1 镜像布局：包内树
            // 以 payload/ 为 .rwdesign 根镜像——快照源 path 即包内名，
            // io 单元夹具 FakeSnapshotSource 同词形）。
            entry.path = std::string(io::PackFormat::kPayloadPrefix) + rel;
            entry.size = static_cast<std::uint64_t>(it->file_size(ec));
            out.value.push_back(std::move(entry));
        }
        return out;
    }

    io::IoResult<io::IoString> read(const io::IoString& packPath) override
    {
        io::IoResult<io::IoString> out;
        std::error_code ec;
        // 包内名 → 源相对名：剥 payload/ 前缀（镜像布局的反向映射——
        // 与 enumerate 的加前缀对称）；再把正斜杠转回本机分隔符（快照
        // 根是 store 的规范路径，Windows 带 \\?\ 前缀——该形态下 Win32
        // 不把 "/" 当分隔符，混斜杠路径 CreateFile 拒绝）。
        std::string rel = packPath;
        const std::string prefix = io::PackFormat::kPayloadPrefix;
        if (rel.rfind(prefix, 0) == 0) {
            rel = rel.substr(prefix.size());
        }
        std::replace(rel.begin(), rel.end(), '/', '\\');
        const fs::path full = m_root / fs::u8path(rel);
        std::ifstream in(full, std::ios::binary);
        if (!in.is_open()) {
            out.error.code = io::IoErrorCode::ResNotFound;
            out.error.detail = "快照文件读取失败: " + packPath;
            return out;
        }
        out.value.assign(std::istreambuf_iterator<char>(in),
                         std::istreambuf_iterator<char>());
        return out;
    }

private:
    fs::path m_root;///< 快照根（源项目目录——只读遍历）
};

/// 包导出端口桩（真实 io exporter 桥接——workflow 请求/令牌/进度 → io
/// 选项/令牌/进度的映射面，即 L5 装配桥接的测试等价物）。
class RealIoExportPort final : public workflow::IPackageExportPort {
public:
    Execution exportPackage(project::ProjectStore& source,
                            const workflow::PackageExportRequest& request,
                            IFlowCancelToken* cancel,
                            const workflow::FlowProgressCallback& progress) override
    {
        Execution e;
        DirectorySnapshotSource snapshot(source.canonicalPath());

        // workflow 请求 → io 导出选项（勾选逐位映射；源元数据透传；
        // createdAtUtc 空＝取当前 UTC——IO-D11 可复现导出的缺省语义）。
        io::PackageExportOptions options;
        options.targetFile = request.targetFile;
        options.includeResults = request.selection.includeResults;
        options.includeReports = request.selection.includeReports;
        options.includeDrafts = request.selection.includeDrafts;
        options.sourceProjectId = request.sourceProjectId;
        options.headRevisionId = request.headRevisionId;

        IoCancelAdapter cancelAdapter(cancel);
        auto progressAdapter = [&progress](const io::IoProgress& p) {
            if (progress) {
                FlowProgressStage stage;
                stage.phaseToken = (p.stage != nullptr) ? p.stage : "";
                stage.done = p.done;
                stage.total = p.total;
                progress(stage);
            }
        };

        // 真实 io 导出（§7.2 六步协议——临时区/压缩/自检/原子替换全真）。
        auto result = io::makePackageExporter()->export_(
            snapshot, options, nullptr,
            (cancel != nullptr) ? &cancelAdapter : nullptr, progressAdapter);
        if (result) {
            e.exported = true;
            e.entryCount = result.value.entryCount;
            e.totalBytes = result.value.totalBytes;
            e.temporaryAreaCleaned = true;  // io 协议：成功路径临时区已清理
            return e;
        }
        // 失败/取消折叠（UX-03；取消不落诊断——码面判定转状态位）。
        e.cancelled = (result.error.code == io::IoErrorCode::Cancelled);
        // 清理观测位：IO-PACK-CLEANUP-FAILED＝清理承诺破坏；其余失败码
        // io 侧已完成自动清理（§7.2/§7.6——失败清理成功后才返回过程码）。
        e.temporaryAreaCleaned =
            (result.error.code != io::IoErrorCode::PackCleanupFailed);
        e.cause = std::string(io::errorCodeToken(result.error.code))
            + ": " + result.error.detail;
        e.action = e.cancelled ? "" : "请检查目标位置与磁盘状态后重试";
        return e;
    }
};

// =====================================================================
// 端口桩三：包导入真实 io 桥接（L5 装配层的测试等价物——io 真实导入器
// begin/verifyThrough/cleanup＋发布半区以桩动作模拟〔project ⑧步——
// 同卷 rename payloadRoot→targetDir；WP-04-T18 落位后由真实发布替换，
// 本桩对该模拟作显式声明，不虚构 project 已有发布 API〕）。
// =====================================================================

class RealIoImportPort final : public workflow::IPackageImportPort {
public:
    PackageImportExecution importPackage(const workflow::PackageImportRequest& request,
                                         IFlowCancelToken* cancel,
                                         const workflow::FlowProgressCallback& progress) override
    {
        PackageImportExecution e;

        io::PackageImportOptions options;
        options.targetDir = request.targetDir;
        options.budget = io::BudgetSpec::packImportHardened();  // 四维硬限产品默认

        IoCancelAdapter cancelAdapter(cancel);
        auto progressAdapter = [&progress](const io::IoProgress& p) {
            if (progress) {
                FlowProgressStage stage;
                stage.phaseToken = (p.stage != nullptr) ? p.stage : "";
                stage.done = p.done;
                stage.total = p.total;
                progress(stage);
            }
        };

        // 真实 io 导入器（九步协议——临时区/预算/路径穿越防护/逐条目
        // 展开与哈希复算/镜像核对/引用完整性/目标预检/清理全真）。
        auto importer = io::makePackageImporter();
        auto session = importer->begin(request.packFile, options,
                                       (cancel != nullptr) ? &cancelAdapter : nullptr,
                                       progressAdapter);
        if (!session) {
            foldIoFailure(session.error, e);
            return e;
        }
        auto verified = importer->verifyThrough(session.value,
                                                (cancel != nullptr) ? &cancelAdapter : nullptr,
                                                progressAdapter);
        // 报告材料折叠（无论成败——校验报告恒透传，编排核零加工）。
        const io::PackageImportReport& report = session.value.report();
        e.manifestEntries = report.manifestEntries;
        e.verifiedEntries = report.verifiedEntries;
        e.totalBytes = report.totalBytes;
        for (const auto& entry : report.entryResults) {
            PackageImportEntryLine line;
            line.path = entry.path;
            line.hashOk = (entry.error.code == io::IoErrorCode::Ok);
            e.entryLines.push_back(std::move(line));
        }
        for (const auto& diag : report.diagnostics) {
            PackageImportDiagnosticLine line;
            line.code = std::string(io::errorCodeToken(diag.code));
            line.message = diag.detail;
            e.diagnostics.push_back(std::move(line));
        }
        // 清理（⑨/失败清理——幂等）：调用时序按 io §7.7 责任切分——
        // **发布成功后**才清理会话外殻（cleanup 删除临时区；payload 已被
        // rename 移走后才清）；失败/取消路径的 io 内部已自动清理，此处
        // cleanup 幂等收尾并产出清理观测位。
        if (!verified) {
            e.verified = false;
            e.cancelled = e.cancelled
                || (verified.error.code == io::IoErrorCode::Cancelled);
            e.cancelled = e.cancelled
                || (session.value.state() == io::PackageImportState::Canceled);
            auto cleaned = importer->cleanup(session.value);
            e.targetLeftClean = static_cast<bool>(cleaned);
            if (e.cancelled) {
                e.diagnostics.clear();  // UX-03：取消不落诊断
            } else {
                e.cause = std::string(io::errorCodeToken(verified.error.code))
                    + ": " + verified.error.detail;
                e.action = "请核对包文件来源与完整性后重试";
            }
            return e;  // verified=false——零发布（结构性：校验先行）
        }
        e.verified = true;

        // ---- 发布半区（project ⑧步的桩模拟——显式声明：同卷 rename
        // payloadRoot→targetDir；WP-04-T18 落位后由真实发布替换）。
        std::error_code ec;
        fs::rename(session.value.payloadRoot(), request.targetDir, ec);
        auto cleaned = importer->cleanup(session.value);  // ⑨幂等收尾
        e.targetLeftClean = static_cast<bool>(cleaned);
        if (ec) {
            e.published = false;
            e.cause = "发布（rename）失败: " + ec.message();
            e.action = "请检查目标位置后重试";
            return e;
        }
        e.published = true;
        return e;
    }

private:
    /// begin 阶段失败折叠（无会话/无报告——码面＋detail 透传）。
    static void foldIoFailure(const io::IoError& error, PackageImportExecution& e)
    {
        e.verified = false;
        e.published = false;
        e.cancelled = (error.code == io::IoErrorCode::Cancelled);
        if (e.cancelled) {
            e.targetLeftClean = true;  // begin 失败无临时区产物（无残留）
            return;
        }
        PackageImportDiagnosticLine line;
        line.code = std::string(io::errorCodeToken(error.code));
        line.message = error.detail;
        e.diagnostics.push_back(std::move(line));
        e.cause = line.code + ": " + error.detail;
        e.action = "请核对包文件后重试";
        e.targetLeftClean = true;  // begin 失败＝临时区未建立（§7.3①②）
    }
};

// =====================================================================
// WF-VER-213：另存为换新 projectId＋按打开协议进入（PM-05/AT-20）
// =====================================================================

/// 另存主线：真实完整目录复制（勾选裁剪 drafts）→ 按打开协议真实进入
/// 新项目（open 协议②③⑤全过）＋确认勾选随 Outcome 回传（记忆登记面）。
TEST_F(SaveAsPackageContract, SaveAs_EntersNewProject_ViaOpenProtocol_P05_AT20)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});

    // 前置：真实黄金项目源（含一份真实落盘草稿——drafts 树裁剪料）。
    const fs::path sourceDir = m_dir / "source-design";
    auto golden = createGolden(sourceDir);
    ASSERT_TRUE(golden.store);
    const fs::path draftFile = saveDraftViaService(*golden.store, "requirements");
    ASSERT_FALSE(draftFile.empty());

    // 另存请求：目标新目录；drafts 勾选取消（裁剪断言料）。
    workflow::SaveAsRequest request;
    request.targetDir = m_dir / "target-design";
    request.selection.includeResults = true;
    request.selection.includeReports = true;
    request.selection.includeDrafts = false;  // 未勾选 drafts——目标不得出现

    RealCopySaveAsPort port;
    // 非 const：Entered 路径需要释放 outcome.store 的写锁（reset——
    // const 对象的 unique_ptr 成员不可移转）。
    workflow::SaveAsOutcome outcome =
        workflow::SaveAsFlow::run(*golden.store, request, port);

    // 编排结果：Entered——复制成功且已按打开协议进入新项目。
    EXPECT_EQ(outcome.result, workflow::SaveAsOutcome::Result::Entered);
    ASSERT_TRUE(outcome.store);
    ASSERT_TRUE(outcome.projectId.has_value());
    // 规范路径非空即可（词面比较不做——project 规范形态含平台前缀
    // \\?\，属 project 内部事实；T05 OpenWizardContract 同款口径）。
    EXPECT_FALSE(outcome.canonicalPath.empty());
    // 记忆登记面：确认勾选恒回传（调用方持久化——PM-14/T10 半区）。
    EXPECT_EQ(outcome.selection, request.selection);

    // 盘面复核一：勾选裁剪真实生效——未勾选的 drafts 树不在目标。
    EXPECT_FALSE(fs::exists(request.targetDir / "drafts"))
        << "未勾选 drafts 时目标目录不得出现 drafts 树（PM-05 勾选语义）";
    // 盘面复核二：镜像必备在目标（复制完整性——open 成功的独立证据）。
    EXPECT_TRUE(fs::exists(request.targetDir / "project.json"));
    EXPECT_TRUE(fs::exists(request.targetDir / "HEAD"));
    // 盘面复核三：进入的项目身份来自目标 project.json（打开协议真实
    // ②③⑤装载）；目标目录真实存在。
    EXPECT_TRUE(fs::exists(request.targetDir));
    EXPECT_EQ(outcome.store->projectId(), outcome.projectId);

    // 释放两个存储上下文（写锁——套件内后续用例不受持锁影响）。
    golden.store->requestClose();
    golden.store.reset();
    outcome.store->requestClose();
    outcome.store.reset();
}

/// 另存取消半区（AT-20"取消不留半成品"）：预置取消令牌→真实复制在
/// 首个检查点命中→目标目录真实零残留（取消即清理的盘面复核）。
TEST_F(SaveAsPackageContract, SaveAs_Cancelled_TargetLeftClean_AT20)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});

    const fs::path sourceDir = m_dir / "source-design";
    auto golden = createGolden(sourceDir);
    ASSERT_TRUE(golden.store);

    workflow::SaveAsRequest request;
    request.targetDir = m_dir / "target-design";

    MemoryCancelLike cancel;  // 预置取消——首个检查点即命中
    cancel.requestCancel();
    RealCopySaveAsPort port;
    const workflow::SaveAsOutcome outcome =
        workflow::SaveAsFlow::run(*golden.store, request, port, &cancel);

    EXPECT_EQ(outcome.result, workflow::SaveAsOutcome::Result::Canceled);
    EXPECT_FALSE(outcome.failure.has_value());  // 取消非错误（UX-03）
    // 盘面复核：目标目录零残留（"取消即清理"——AT-20 真实落盘证据）。
    EXPECT_FALSE(fs::exists(request.targetDir))
        << "取消后目标目录不得残留（PM-05/AT-20 取消即清理）";

    golden.store->requestClose();
    golden.store.reset();
}

// =====================================================================
// WF-VER-214：包导出 .rwpack ZIP 传输封装——真实产物＋取消清理（PM-05）
// =====================================================================

/// 包导出主线：真实 io 导出器产出 .rwpack（ZIP 传输封装）＋源项目目录
/// 树零写（"导出失败保证项目状态不变"的编排侧＋执行面复核）。
TEST_F(SaveAsPackageContract, Export_RealZipArtifact_SourceUntouched_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});

    const fs::path sourceDir = m_dir / "source-design";
    auto golden = createGolden(sourceDir);
    ASSERT_TRUE(golden.store);
    ASSERT_FALSE(saveDraftViaService(*golden.store, "requirements").empty());

    const auto sourceBefore = snapshotTree(sourceDir);

    workflow::PackageExportRequest request;
    request.targetFile = m_dir / "transfer.rwpack";
    request.selection = workflow::PackageSelectionFlags{};  // 全选

    RealIoExportPort port;
    const workflow::PackageExportOutcome outcome =
        workflow::PackageExportFlow::run(*golden.store, request, port);

    EXPECT_EQ(outcome.result, workflow::PackageExportOutcome::Result::Completed);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_GT(outcome.entryCount, 0u);  // 快照清单全量打包
    // 盘面复核一：目标 .rwpack 真实存在且非空（ZIP 传输封装落盘）。
    ASSERT_TRUE(fs::exists(request.targetFile));
    EXPECT_GT(fs::file_size(request.targetFile), 0u);
    // 盘面复核二：源项目目录树逐文件一致（零写源——导出只读快照）。
    EXPECT_EQ(snapshotTree(sourceDir), sourceBefore);

    golden.store->requestClose();
    golden.store.reset();
}

/// 包导出取消半区（PM-05"后台进度可取消，取消即清理临时区"）：预置
/// 取消令牌→io 检查点真实命中→目标文件不产出（原子替换未发生）。
TEST_F(SaveAsPackageContract, Export_Cancelled_TargetUntouched_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});

    const fs::path sourceDir = m_dir / "source-design";
    auto golden = createGolden(sourceDir);
    ASSERT_TRUE(golden.store);

    workflow::PackageExportRequest request;
    request.targetFile = m_dir / "transfer.rwpack";

    MemoryCancelLike cancel;
    cancel.requestCancel();  // 预置取消——io 导出首个检查点命中
    RealIoExportPort port;
    const workflow::PackageExportOutcome outcome =
        workflow::PackageExportFlow::run(*golden.store, request, port, &cancel);

    EXPECT_EQ(outcome.result, workflow::PackageExportOutcome::Result::Canceled);
    EXPECT_FALSE(outcome.failure.has_value());  // 取消非错误（UX-03）
    // 盘面复核：目标 .rwpack 不存在（取消在原子替换前——目标不变）。
    EXPECT_FALSE(fs::exists(request.targetFile));

    golden.store->requestClose();
    golden.store.reset();
}

// =====================================================================
// WF-VER-215：包导入全量校验——越界路径拒绝不留目标目录＋导出导入
// 回路（校验报告呈现面）
// =====================================================================

/// 恶意包（路径穿越条目 payload/../evil.txt）→ io 防护真实拒绝＋校验
/// 报告（结构化诊断透传）＋目标目录真实不存在（失败不留目标目录）。
TEST_F(SaveAsPackageContract, Import_PathTraversalRejected_NoTarget_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});

    // 恶意包构造：manifest 与 zip 同声明越界条目（io SafePath 批量规则
    // 核的检测点——§7.4 威胁矩阵③）。
    auto payload = standardPayload();
    payload.emplace_back("payload/../evil.txt", std::string("escaped-bytes"));
    const fs::path evilPack =
        buildPackFromPayload(m_dir / "evil.rwpack", payload);
    ASSERT_TRUE(fs::exists(evilPack));

    workflow::PackageImportRequest request;
    request.packFile = evilPack;
    request.targetDir = m_dir / "imported-design";  // 不存在的目标

    RealIoImportPort port;
    const workflow::PackageImportOutcome outcome =
        workflow::PackageImportFlow::run(request, port);

    // 编排结果：Failed＋失败呈现（UX-03 半区）。
    EXPECT_EQ(outcome.result, workflow::PackageImportOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());

    // 校验报告材料：结构化诊断透传（码 token 为 IO-SEC 族越界拒绝——
    // 呈现面取数源；编排核零加工）。
    EXPECT_FALSE(outcome.execution.verified);
    EXPECT_FALSE(outcome.execution.diagnostics.empty())
        << "越界路径拒绝必须携带结构化诊断（校验报告——PM-05）";
    // 盘面复核：失败不留目标目录（PM-05/NFR-SEC-01/02——真实 fs::exists）。
    EXPECT_FALSE(fs::exists(request.targetDir))
        << "校验失败后目标目录不得存在（失败不留目标目录）";
    // 报告呈现行集：final-state==failed（buildPackageImportReportView
    // 对真实执行结果的消费面）。
    const auto lines = workflow::buildPackageImportReportView(outcome.execution);
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines[0].valueText, "failed");
}

/// 导出→导入回路（PM-05 全语义）：真实导出的 .rwpack 经真实校验＋
/// 发布（⑧步桩模拟）后目标目录就位，且可按打开协议真实打开（复制产物
/// 合法性——进入材料）。
TEST_F(SaveAsPackageContract, Import_ExportedPackRoundTrip_VerifiedPublished_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});

    // 第一段：真实黄金项目导出 .rwpack（真实 io 导出器——上一用例同款）。
    const fs::path sourceDir = m_dir / "source-design";
    auto golden = createGolden(sourceDir);
    ASSERT_TRUE(golden.store);
    ASSERT_FALSE(saveDraftViaService(*golden.store, "requirements").empty());

    workflow::PackageExportRequest exportRequest;
    exportRequest.targetFile = m_dir / "roundtrip.rwpack";
    exportRequest.selection = workflow::PackageSelectionFlags{};  // 全选（含 drafts）
    RealIoExportPort exportPort;
    const workflow::PackageExportOutcome exported =
        workflow::PackageExportFlow::run(*golden.store, exportRequest, exportPort);
    ASSERT_EQ(exported.result, workflow::PackageExportOutcome::Result::Completed);

    const core::ProjectId sourceProjectId = golden.store->projectId();
    // 源 store 释放写锁（导入目标的 open 不冲突——同内容不同路径；先
    // 释放以保套件路径幂等）。
    golden.store->requestClose();
    golden.store.reset();

    // 第二段：导入该包（真实 io 校验＋发布桩模拟）→ 目标目录就位。
    workflow::PackageImportRequest importRequest;
    importRequest.packFile = exportRequest.targetFile;
    importRequest.targetDir = m_dir / "imported-design";

    RealIoImportPort importPort;
    const workflow::PackageImportOutcome outcome =
        workflow::PackageImportFlow::run(importRequest, importPort);

    EXPECT_EQ(outcome.result, workflow::PackageImportOutcome::Result::Completed);
    EXPECT_FALSE(outcome.failure.has_value());
    // 校验报告材料：全量校验通过（逐条目哈希复算全绿——计数一致）。
    EXPECT_TRUE(outcome.execution.verified);
    EXPECT_TRUE(outcome.execution.published);
    EXPECT_EQ(outcome.execution.verifiedEntries, outcome.execution.manifestEntries);
    EXPECT_GT(outcome.execution.manifestEntries, 0u);
    // 盘面复核：目标目录就位（镜像必备在位）。
    ASSERT_TRUE(fs::exists(importRequest.targetDir));
    EXPECT_TRUE(fs::exists(importRequest.targetDir / "project.json"));
    // 第三段：按打开协议真实打开目标（复制产物合法性——进入材料）；
    // 项目身份与源一致（同内容导入——projectId 换新归另存存储侧语义，
    // 导入是"解包逐字节还原"，内容同源）。
    project::OpenStoreRequest openRequest;
    openRequest.path = importRequest.targetDir;
    openRequest.mode = project::OpenMode::Writable;
    auto reopened = project::ProjectStoreFactory::open(openRequest);
    ASSERT_TRUE(reopened.store);
    EXPECT_EQ(reopened.store->projectId(), sourceProjectId);
    reopened.store->requestClose();
    reopened.store.reset();

    // 报告呈现行集：final-state==verified。
    const auto lines = workflow::buildPackageImportReportView(outcome.execution);
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines[0].valueText, "verified");
    EXPECT_EQ(lines[lines.size() - 1].valueText, "yes");  // target-clean
}

}  // namespace
