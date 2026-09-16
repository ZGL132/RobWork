/**
 * @file   TxEngineTest.cpp
 * @brief  事务引擎（TxEngine）用例组——七步提交协议边界注入矩阵
 *         （PRJ-TX-4：§7.6 F1～F8 的进程内部分）、「已发布但未提交」识别
 *         （§7.3）、.staging/tmp 临时物清理（D-09）与 P-PR-1 处置自证。
 *
 * 设计依据：
 *   - units/project.md §7.1（七步协议逐步表——本组逐边界断言）、§7.3（闭包
 *     规则：闭包外内容对查询不可见）、§7.4（恢复状态机与 PM-08 顺序①④⑤）、
 *     §7.5（崩溃一致性要点：已提交字节不变；恢复不依赖"目录存在"判定提交）、
 *     §7.6（故障注入矩阵 F1～F8——本组承接其进程内部分；进程级 kill 归
 *     PRJ-T15 的 TestProcessRunner，AT-13 自动化验收经其复验）、§11（用例
 *     每注明需求/AT）、§4.1（.staging/tmp 行"启动清理"）、D-09（临时产物限
 *     .staging/tmp）、D-18（事件发布失败不回滚）；
 *   - 需求 NFR-REL-01（任一步失败旧版本保持完整——字节比对）、PM-08（崩溃
 *     后已提交修订与 HEAD 字节不变）、NFR-COR-02（恢复报告确定性）；
 *   - 任务契约 tasks/foundation/PRJ-T07.json acceptance 1～4：
 *     acceptance 1＝F1～F7 逐项＋任一步失败字节不变＋恢复诊断（本组
 *     F1/F2/F3/F4/F5/F6/F7 用例）；acceptance 2＝崩溃边界字节不变（F8 四个
 *     边界用例——提交点＝HEAD 原子切换，顺序即原子性 D-09 前置；进程级
 *     场景归 PRJ-T15 复验，此处为进程内模拟）；acceptance 3＝闭包外识别
 *     （F4/F5 用例的 uncommittedRevisions 与查询不可见断言）＋.staging/tmp
 *     清理（StagingTmp 用例）；acceptance 4＝P-PR-1 处置（HeadManifestDigest
 *     用例——清单摘要与 core::ContentDigester 直算比对）。
 *
 * 接缝消费说明：TxFaultFileOps 是 testkit.md D-10 形态的本地 fake——装饰
 *   真实 Win32FileOps、按 faultpoint 标识在指定 occurrence 注入失败、支持
 *   "关闭后篡改"（F3 写后位翻转——挂 closeHandle 而非 flush：暂存句柄以
 *   share mode 0 独占打开，句柄存活期间二次打开会共享冲突，关闭后篡改
 *   才是"写后篡改"的可行进程内形态）。PRJ-T15 契约测试将替换为 testkit
 *   FaultInterceptor 经同一接缝注入（faultpoint ID 已在 IFileOps.hpp 冻结）。
 */

#include "TxEngine.hpp"
#include "Codec.hpp"
#include "win32/AtomicFile.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::ContentVersion;
using sdurws::ird::core::DomainEvent;
using sdurws::ird::core::DomainEventKind;
using sdurws::ird::core::IDomainEventBus;
using sdurws::ird::core::IDomainEventSink;
using sdurws::ird::core::IEventSubscription;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::project::BranchId;
using sdurws::ird::project::CommandRecord;
using sdurws::ird::project::HeadRecord;
using sdurws::ird::project::IDiagnosticsSink;
using sdurws::ird::project::ObjectRefPair;
using sdurws::ird::project::ProjectMetadataRecord;
using sdurws::ird::project::RevisionManifest;
using sdurws::ird::project::StoreError;
using sdurws::ird::project::StoreErrorCode;
using sdurws::ird::project::codec::contentVersionOf;
using sdurws::ird::project::objstore::ObjectStore;
using sdurws::ird::project::revindex::MetadataDelta;
using sdurws::ird::project::revindex::RevisionIndex;
using sdurws::ird::project::revindex::TipUpdate;
using sdurws::ird::project::tx::CommitPlan;
using sdurws::ird::project::tx::CommitResult;
using sdurws::ird::project::tx::PlannedObject;
using sdurws::ird::project::tx::TxEngine;
using sdurws::ird::project::tx::TxRecoveryScan;
using sdurws::ird::project::win32::FileResult;
using sdurws::ird::project::win32::IFileOps;
using sdurws::ird::project::win32::Win32FileOps;
namespace fp = sdurws::ird::project::win32::faultpoint;

namespace {

// ---------------------------------------------------------------------
// 测试常量与字节辅助
// ---------------------------------------------------------------------

/// 种子项目的主分支显示名（label 创建时一次写入——P-PR-8）。
constexpr const char* kMainLabel = "main";

/// 大载荷尺寸（F2"写中途失败"需要跨 ≥2 个 64 KiB chunk 的数据——
/// kWriteChunkSize＝64 KiB；200 000 字节＝4 个 chunk）。单位：字节。
constexpr std::size_t kBigPayloadBytes = 200000;

/// ContentVersion → 64 hex 文件名形态（剥离 "cv-" tag——与实现同源规则）。
std::string cvHex(const ContentVersion& cv)
{
    const std::string canonical = cv.toCanonical();
    return canonical.substr(3);
}

/// 二进制整读；读失败显性失败（不留"读不到＝内容不符"的假阳性通道）。
std::string readAll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取文件: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 独立于被测对象的裸写（种子前置数据不经被测代码——保证前置不依赖
/// 被测正确性）。失败由调用方 ASSERT。
bool writeRaw(const fs::path& file, const std::string& bytes)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        ADD_FAILURE() << "建前置目录失败: " << ec.message();
        return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        ADD_FAILURE() << "无法写前置文件: " << file.string();
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

/// 递归收集目录下全部普通文件的（相对路径→字节）快照——字节不变断言
/// 的基准集（提交点前后/失败前后逐文件比对，NFR-REL-01 的观测面）。
void collectFiles(const fs::path& root, const fs::path& base,
                  std::map<std::string, std::string>& out)
{
    std::error_code ec;
    if (!fs::exists(root, ec) || ec) {
        return;
    }
    for (fs::directory_iterator it(root, ec), end; it != end && !ec;
         it.increment(ec)) {
        if (ec) {
            break;
        }
        std::error_code typeEc;
        if (it->is_directory(typeEc) && !typeEc) {
            collectFiles(it->path(), base, out);
        } else if (it->is_regular_file(typeEc) && !typeEc) {
            std::error_code relEc;
            const auto rel = fs::relative(it->path(), base, relEc);
            out[rel.string()] = readAll(it->path());
        }
    }
}

// ---------------------------------------------------------------------
// fake：故障注入文件操作（D-10 形态装饰器）
// ---------------------------------------------------------------------

/**
 * @brief IFileOps 故障注入 fake——装饰真实 Win32FileOps。
 *
 * 两种注入面：
 *   1. failAt(faultPoint, occurrence, osError)：命中即返回失败（真实动作
 *      不发生）——F1/F2/F4/F5/F7 与 F8 各边界的载体；
 *   2. corruptOnCloseOccurrence(n)：第 n 次 closeHandle 真实关闭成功后，
 *      翻转该句柄所写文件的 0 号字节——F3"暂存文件位翻转（写后篡改）"。
 *      挂 closeHandle 而非 flush 的原因见文件头注（独占句柄的共享冲突）。
 */
class TxFaultFileOps : public IFileOps {
public:
    explicit TxFaultFileOps(IFileOps* real) : m_real(real) {}

    void failAt(const char* faultPoint, unsigned long occurrence,
                unsigned long osError)
    {
        m_plan[faultPoint] = Plan{occurrence, osError};
    }

    void corruptOnCloseOccurrence(unsigned long occurrence)
    {
        m_corruptOnClose = occurrence;
    }

    int hits(const char* faultPoint) const
    {
        const auto it = m_counts.find(faultPoint);
        return it == m_counts.end() ? 0 : it->second;
    }

    // ---- IFileOps：记录→判定注入→转发 ----

    FileResult openWriteThrough(const std::wstring& path,
                                HANDLE* handle) override
    {
        const FileResult r = step(fp::kOpen, [&] {
            return m_real->openWriteThrough(path, handle);
        });
        if (r.ok) {
            m_paths[*handle] = path;  // 句柄→路径（篡改定位用）
        }
        return r;
    }

    FileResult writeChunk(HANDLE handle, const char* data,
                          std::size_t length) override
    {
        return step(fp::kWriteChunk,
                    [&] { return m_real->writeChunk(handle, data, length); });
    }

    FileResult flush(HANDLE handle) override
    {
        return step(fp::kFlush, [&] { return m_real->flush(handle); });
    }

    FileResult closeHandle(HANDLE handle) override
    {
        // 关闭前先记下路径（关闭后句柄失效，map 需清除）。
        std::wstring path;
        const auto pit = m_paths.find(handle);
        if (pit != m_paths.end()) {
            path = pit->second;
        }
        const FileResult r = m_real->closeHandle(handle);
        m_paths.erase(handle);
        // 写后篡改（F3）：真实关闭成功后翻转文件首字节——'{'→异或后
        // 必然改变 SHA-256（验证步重算比对必不相符）。
        if (r.ok && path.size() > 0) {
            ++m_closeCount;
            if (m_corruptOnClose != 0
                && m_closeCount == m_corruptOnClose) {
                std::fstream f(fs::path(path),
                               std::ios::binary | std::ios::in
                                   | std::ios::out);
                if (f) {
                    char b = '\0';
                    f.read(&b, 1);
                    f.seekg(0);
                    b = static_cast<char>(b ^ 0xFF);
                    f.write(&b, 1);
                }
            }
        }
        return r;
    }

    FileResult publishNew(const std::wstring& tempPath,
                          const std::wstring& targetPath) override
    {
        return step(fp::kPublishNew,
                    [&] { return m_real->publishNew(tempPath, targetPath); });
    }

    FileResult replaceExisting(const std::wstring& tempPath,
                               const std::wstring& targetPath) override
    {
        return step(fp::kReplaceExisting, [&] {
            return m_real->replaceExisting(tempPath, targetPath);
        });
    }

    FileResult createDirectories(const std::wstring& path) override
    {
        return step(fp::kCreateDirectories,
                    [&] { return m_real->createDirectories(path); });
    }

    FileResult removeTree(const std::wstring& path) override
    {
        return step(fp::kRemoveTree,
                    [&] { return m_real->removeTree(path); });
    }

private:
    struct Plan
    {
        unsigned long occurrence = 1;
        unsigned long osError = 0;
    };

    template <class Fn>
    FileResult step(const char* faultPoint, Fn&& real)
    {
        ++m_counts[faultPoint];
        const auto it = m_plan.find(faultPoint);
        if (it != m_plan.end()
            && it->second.occurrence == static_cast<unsigned long>(m_counts[faultPoint])) {
            FileResult injected;
            injected.ok = false;
            injected.osError = it->second.osError;
            return injected;  // 计划命中：真实动作不发生（§7.6 注入语义）
        }
        return real();
    }

    IFileOps* m_real;  ///< 非 owning 真实实现
    std::map<std::string, Plan> m_plan;
    std::map<std::string, int> m_counts;
    std::map<HANDLE, std::wstring> m_paths;  ///< 句柄→路径（篡改定位）
    unsigned long m_corruptOnClose = 0;      ///< 篡改计划（0＝无）
    unsigned long m_closeCount = 0;
};

// ---------------------------------------------------------------------
// fake：事件总线（F6——注入 publish 抛出，记录调用序与事件）
// ---------------------------------------------------------------------

class NoopSubscription : public IEventSubscription {
public:
    void unsubscribe() override {}
};

class FakeBus : public IDomainEventBus {
public:
    /// 前 failFirst 次 publish 抛出（0＝不注入——F6 边界开关）。
    int failFirst = 0;
    int calls = 0;
    std::vector<DomainEvent> events;

    void publish(const DomainEvent& event) override
    {
        ++calls;
        if (calls <= failFirst) {
            throw std::runtime_error("injected bus failure（F6 注入）");
        }
        events.push_back(event);
    }

    std::unique_ptr<IEventSubscription> subscribe(IDomainEventSink&) override
    {
        return std::make_unique<NoopSubscription>();
    }
};

// ---------------------------------------------------------------------
// fake：诊断 sink（用户级码与开发级消息捕获——恢复诊断断言面）
// ---------------------------------------------------------------------

class CapturingSink : public IDiagnosticsSink {
public:
    std::vector<DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;

    void report(const DiagnosticRecord& record) override
    {
        reports.push_back(record);
    }

    void reportDev(const std::string& channel,
                   const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }

    bool hasUserCode(const std::string& code) const
    {
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }

    bool devContains(const std::string& needle) const
    {
        for (const auto& d : devs) {
            if (d.second.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief 断言动作抛出指定稳定码的 StoreError 并返回异常（断言宏在辅助
 *        函数中失败会记入用例——gtest 线程安全；返回值供后续 detail 检查）。
 */
template <class Fn>
StoreError expectStoreError(StoreErrorCode code, Fn&& action)
{
    try {
        action();
        ADD_FAILURE() << "期望 StoreError("
                      << static_cast<int>(code) << ") 未抛出";
    } catch (const StoreError& e) {
        EXPECT_EQ(e.code(), code) << "稳定码不符: " << e.what();
        return e;
    }
    // 未抛出路径：抛出哨兵异常中止本用例后续断言（避免空引用连锁失败）。
    throw std::runtime_error("expectStoreError: action did not throw");
}

}  // namespace

/// 共享真实实现（无状态——与 AtomicFileTest 同口径；进程级单实例，
/// fake 装饰其上）。
Win32FileOps& realOps()
{
    static Win32FileOps s_real;
    return s_real;
}

// ---------------------------------------------------------------------
// 用例组：ProjectTxEngine（套件名登记入 ird-test-report.json）
// ---------------------------------------------------------------------

/**
 * 测试夹具：每用例独立项目目录＋完整种子项目（r0 修订＋权威元数据 M0
 * ＋HEAD）＋被测引擎及其 fake 部件。种子的磁盘写入全部经裸写/ObjectStore
 * 完成——前置数据不依赖被测 TxEngine 的正确性。
 */
class TxEngineTest : public ::testing::Test {
protected:
    // 套件级建/删总根（gtest 要求可访问——置于 protected 段）。
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_prj_tx_test"
                 / std::to_string(::GetCurrentProcessId());
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            std::cerr << "无法创建测试根目录: " << s_base.string() << " ("
                      << ec.message() << ")\n";
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        // 总根清理：失败保留现场（§6.2"TempDir 失败保留"），不静默掩盖。
        std::error_code ec;
        fs::remove_all(s_base, ec);
        if (ec) {
            std::cerr << "警告：测试根目录清理失败（保留现场）: "
                      << s_base.string() << " (" << ec.message() << ")\n";
        }
    }

    /// 种子数据锚点（各用例断言引用）。
    struct SeedData
    {
        ProjectId projectId;
        RevisionId r0;
        ObjectId metaOid;
        ContentVersion metaCv;
        ProjectMetadataRecord m0;
        ObjectRefPair m0Ref;
        RevisionManifest r0Manifest;
        HeadRecord head;
    };

    void SetUp() override
    {
        // 用例专属项目目录（进程级隔离——同 AtomicFileTest 口径）。
        m_proj = baseDir() / ("case" + std::to_string(++s_caseCounter))
                 / "proj.rwdesign";
        std::error_code ec;
        fs::create_directories(m_proj, ec);
        ASSERT_FALSE(ec) << "建项目目录失败: " << ec.message();

        // 部件装配（生存期＝本用例；注入指针全部非 owning——与生产装配
        // 同型）。
        m_objects = std::make_unique<ObjectStore>(
            m_proj / "objects", m_proj / ".staging",
            ObjectStore::kDefaultCacheBudgetBytes, nullptr);
        m_index = std::make_unique<RevisionIndex>();
        m_ops = std::make_unique<TxFaultFileOps>(&realOps());
        m_sink = std::make_unique<CapturingSink>();
        m_bus = std::make_unique<FakeBus>();

        ASSERT_NO_FATAL_FAILURE(seedProject());
        m_engine = std::make_unique<TxEngine>(
            m_ops.get(), m_proj, m_objects.get(), m_index.get(), m_bus.get(),
            m_sink.get());
    }

    /// 构造一份合法的第二修订提交计划（对象负载/身份各用例可再覆写）。
    CommitPlan makePlan(std::size_t payloadBytes = 32) const
    {
        CommitPlan plan;
        plan.revisionId = RevisionId::generate();
        plan.branchId = m_seed.head.branchId;  // 活动分支＝HEAD 所在分支
        plan.parentRevisionId = m_seed.r0;     // 分支 tip＝r0（§4.5）
        plan.committedAtUtc = "2026-09-16T01:00:00Z";
        PlannedObject obj;
        obj.oid = ObjectId::generate();
        obj.objectTypeToken = "RobotDesign";
        obj.payload.assign(payloadBytes, 'D');  // 域负载（canonical 字节代餐）
        plan.newObjects.push_back(std::move(obj));
        TipUpdate tip;
        tip.branchId = m_seed.head.branchId;
        tip.newTip = plan.revisionId;  // 每次提交 tip 恰推进到新修订（§4.5）
        plan.metadataDelta.tipUpdate = tip;
        plan.command.commandType = "add-mass-point";  // ^[a-z0-9-]{3,64}
        plan.command.payloadFormatVersion = 1;
        plan.command.payloadCanonical = "{\"point\":1}";
        plan.command.summary = "seed plan command";
        return plan;
    }

    /// 已提交内容快照（HEAD＋revisions＋objects——字节不变断言基准）。
    std::map<std::string, std::string> snapshotCommitted() const
    {
        std::map<std::string, std::string> out;
        collectFiles(m_proj / "HEAD", m_proj, out);
        collectFiles(m_proj / "revisions", m_proj, out);
        collectFiles(m_proj / "objects", m_proj, out);
        return out;
    }

    /**
     * @brief 旧版本完整性断言：before 中全部文件字节不变（PM-08 观测面
     *        ——失败不得改写任何已提交字节）。允许 after 出现新增文件＝
     *        悬挂残留（§7.1 第 4 步"已就位项保留"——发布部分完成的
     *        设计形态）；严格全等（零新增）的用例另行 EXPECT_EQ 快照。
     */
    void expectCommittedUnchanged(
        const std::map<std::string, std::string>& before) const
    {
        const auto after = snapshotCommitted();
        for (const auto& [rel, bytes] : before) {
            const auto it = after.find(rel);
            ASSERT_TRUE(it != after.end()) << "已提交文件丢失: " << rel;
            EXPECT_EQ(it->second, bytes) << "已提交文件被改写: " << rel;
        }
    }

    /// 列出 .staging 根的子目录名（残留断言面）。
    std::vector<std::string> stagingChildren() const
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (fs::directory_iterator it(m_proj / ".staging", ec), end;
             it != end && !ec; it.increment(ec)) {
            if (ec) {
                break;
            }
            names.push_back(it->path().filename().string());
        }
        return names;
    }

    SeedData m_seed;
    fs::path m_proj;
    std::unique_ptr<ObjectStore> m_objects;
    std::unique_ptr<RevisionIndex> m_index;
    std::unique_ptr<TxFaultFileOps> m_ops;
    std::unique_ptr<CapturingSink> m_sink;
    std::unique_ptr<FakeBus> m_bus;
    std::unique_ptr<TxEngine> m_engine;

private:
    /// 种子项目：r0 修订（元数据对象 M0）＋HEAD。全部经裸写/ObjectStore
    /// 完成——不依赖被测引擎（§11 前置列"健康修订 r0"）。
    void seedProject()
    {
        SeedData& s = m_seed;
        s.projectId = ProjectId::generate();
        s.r0 = RevisionId::generate();
        s.metaOid = ObjectId::generate();

        // M0：初始权威元数据（main→r0；无谱系前驱——§4.5.1 步骤 0）。
        s.m0.schemaVersion = sdurws::ird::project::kSchemaVersionCurrent;
        s.m0.committedBy = s.r0;
        s.m0.projectDisplayName = "Seeded Project";
        s.m0.primaryBranchId = BranchId::generate();
        sdurws::ird::project::BranchRecord mainBranch;
        mainBranch.branchId = s.m0.primaryBranchId;
        mainBranch.label = kMainLabel;
        mainBranch.baseRevisionId = s.r0;
        mainBranch.tipRevisionId = s.r0;
        mainBranch.createdAtUtc = "2026-09-16T00:00:00Z";
        s.m0.branches.push_back(mainBranch);
        s.head.branchId = mainBranch.branchId;

        // 元数据对象发布（经 ObjectStore——共享判定/读校验通道的种子侧
        // 消费同型；cv 由发布返回，后续引用全用它）。
        const std::string metaPayload = sdurws::ird::project::codec::dump(s.m0);
        s.metaCv = m_objects->publishObject(s.metaOid, metaPayload);
        s.m0Ref = ObjectRefPair{s.metaOid, s.metaCv};

        // r0 清单（§4.4.2）：引用集＝[元数据对象]；introduced＝[元数据 cv]。
        s.r0Manifest.revisionId = s.r0;
        s.r0Manifest.revisionSeq = 1;
        s.r0Manifest.branchId = mainBranch.branchId;
        s.r0Manifest.committedAtUtc = "2026-09-16T00:00:00Z";
        s.r0Manifest.metadataRef = s.m0Ref;
        sdurws::ird::project::ObjectRef metaRef;
        metaRef.objectId = s.metaOid;
        metaRef.contentVersion = s.metaCv;
        metaRef.objectTypeToken = "ProjectMetadata";
        metaRef.digest256 = cvHex(s.metaCv);
        s.r0Manifest.objectRefs.push_back(metaRef);
        s.r0Manifest.introducedObjects.push_back(s.metaCv);

        // 磁盘就位：manifest.json＋command.json（裸写）＋HEAD（裸写）。
        const std::string manifestBytes =
            sdurws::ird::project::codec::dump(s.r0Manifest);
        const std::string manifestDigest =
            cvHex(contentVersionOf(manifestBytes));
        CommandRecord cmd;
        cmd.commandType = "project-init";
        cmd.payloadFormatVersion = 1;
        cmd.payloadCanonical = "{}";
        cmd.summary = "seed r0";
        ASSERT_TRUE(writeRaw(
            m_proj / "revisions" / s.r0.toCanonical() / "manifest.json",
            manifestBytes));
        ASSERT_TRUE(writeRaw(
            m_proj / "revisions" / s.r0.toCanonical() / "command.json",
            sdurws::ird::project::codec::dump(cmd)));
        s.head.formatId = sdurws::ird::project::kFormatId;
        s.head.schemaVersion = sdurws::ird::project::kSchemaVersionCurrent;
        s.head.projectId = s.projectId;
        s.head.revisionId = s.r0;
        s.head.revisionSeq = 1;
        s.head.manifestDigest = manifestDigest;
        ASSERT_TRUE(writeRaw(m_proj / "HEAD",
                             sdurws::ird::project::codec::dump(s.head)));

        // 会话索引装载（打开协议装载纪律的种子侧同型）＋序号水位恢复。
        m_index->registerRevision(s.r0Manifest);
        m_index->registerMetadata(s.m0Ref, s.m0);
        m_index->adoptHeadSeq(1);
    }

    static fs::path baseDir() { return s_base; }

    static fs::path s_base;
    static int s_caseCounter;
};

fs::path TxEngineTest::s_base;
int TxEngineTest::s_caseCounter = 0;

// =====================================================================
// 健康路径（§7.1 七步全过＝F8 边界扫描的 T7 控制腿）
// =====================================================================

/**
 * 锚定：NFR-REL-01／§7.1 七步协议／PM-08（PRJ-TX-4 前置健康路径）。
 *
 * 前置：种子项目（r0）。操作：合法提交计划执行七步。
 * 预期：HEAD 原子切换至新修订（revisionSeq＝2）；修订目录两文件就位；
 * 对象（元数据＋域）就位；暂存事务目录清理干净；事件 FIFO（提交→失效）；
 * 会话闭包含新旧两修订。
 */
TEST_F(TxEngineTest, Commit_HappyPath_SevenStepsAndStagingCleaned)
{
    const CommitPlan plan = makePlan();
    const HeadRecord head = m_engine->readHead();

    CommitResult result;
    ASSERT_NO_FATAL_FAILURE(
        result = m_engine->commit(plan, head, m_seed.m0, m_seed.m0Ref));

    // 结果身份（第 1 步内存半边：身份随计划、序号＝权威 seq+1——O-15）。
    EXPECT_TRUE(result.revisionId == plan.revisionId);
    EXPECT_EQ(result.revisionSeq, 2u);

    // 第 5 步提交点：磁盘 HEAD 字节级等于结果 newHead（原子切换的地面
    // 事实——读回解析比对，非内存对象自证）。
    const HeadRecord diskHead =
        sdurws::ird::project::codec::parseHeadRecord(
            readAll(m_proj / "HEAD"));
    EXPECT_TRUE(diskHead == result.newHead);
    EXPECT_TRUE(diskHead.revisionId == plan.revisionId);
    EXPECT_EQ(diskHead.revisionSeq, 2u);
    EXPECT_TRUE(diskHead.branchId == plan.branchId);

    // 第 4 步内容发布：修订目录两文件就位；清单字段与计划一致（§4.4.2）。
    const fs::path revDir = m_proj / "revisions" / plan.revisionId.toCanonical();
    const auto manifest = sdurws::ird::project::codec::parseRevisionManifest(
        readAll(revDir / "manifest.json"));
    EXPECT_EQ(manifest.revisionSeq, 2u);
    ASSERT_TRUE(manifest.parentRevisionId.has_value());
    EXPECT_TRUE(*manifest.parentRevisionId == m_seed.r0);
    EXPECT_TRUE(manifest.metadataRef == result.metadataRef);
    EXPECT_EQ(manifest.objectRefs.size(), std::size_t{2});  // 元数据＋域对象
    EXPECT_NO_FATAL_FAILURE(sdurws::ird::project::codec::parseCommandRecord(
        readAll(revDir / "command.json")));

    // 对象发布（域对象字节原样——D-10）：文件名＝cv hex，内容字节一致。
    const PlannedObject& obj = plan.newObjects[0];
    const ContentVersion objCv = contentVersionOf(std::string{
        reinterpret_cast<const char*>(obj.payload.data()), obj.payload.size()});
    EXPECT_TRUE(fs::exists(m_proj / "objects" / obj.oid.toCanonical()
                           / cvHex(objCv)));

    // 第 7 步清理：.staging 根无任何事务目录残留（共享 tmp 仅由非事务
    // 操作创建——健康提交后可能为空，也可能只有 tmp，两类都合规）。
    for (const std::string& name : stagingChildren()) {
        EXPECT_EQ(name, "tmp") << "暂存区不得残留事务目录";
    }

    // 第 6 步事件 FIFO（§7.1"RevisionCommitted→DependencyInvalidated"序）。
    ASSERT_EQ(m_bus->events.size(), std::size_t{2});
    EXPECT_EQ(m_bus->events[0].kind, DomainEventKind::RevisionCommitted);
    EXPECT_TRUE(m_bus->events[0].asRevisionCommitted().revision
                == plan.revisionId);
    EXPECT_EQ(m_bus->events[1].kind, DomainEventKind::DependencyInvalidated);
    EXPECT_TRUE(m_bus->events[1].asDependencyInvalidated().revision
                == plan.revisionId);

    // 会话闭包（引擎已按 S6 时序注册新修订）：r0/r1 双可达。
    const auto closure = m_index->committedClosure(plan.revisionId);
    EXPECT_EQ(closure.revisions.size(), std::size_t{2});
}

/**
 * 锚定：任务契约 PRJ-T07 acceptance 4（P-PR-1 处置）——修订清单摘要经
 * core ContentDigester（§3.2 消费清单），零本地第二哈希路径。
 *
 * 操作：健康提交后，独立以 core::ContentDigester 直算磁盘 manifest.json
 * 字节与 HEAD.manifestDigest、对象文件名三方比对。
 * 预期：三者同源同值（CR-02 唯一摘要入口；core.md v0.1 消费基线）。
 */
TEST_F(TxEngineTest, HeadManifestDigest_MatchesDirectCoreDigester)
{
    const CommitPlan plan = makePlan();
    const HeadRecord head = m_engine->readHead();
    ASSERT_NO_FATAL_FAILURE(
        m_engine->commit(plan, head, m_seed.m0, m_seed.m0Ref));

    // 独立直算：不经本单元 codec 入口，直接实例化 core::ContentDigester
    // 对磁盘字节计算——两路径同值即证明"经 core 摘要"而非本地第二实现。
    const std::string manifestBytes =
        readAll(m_proj / "revisions" / plan.revisionId.toCanonical()
                / "manifest.json");
    sdurws::ird::core::ContentDigester digester;
    digester.update(manifestBytes.data(), manifestBytes.size());
    const ContentVersion direct{digester.finalize()};

    const HeadRecord diskHead =
        sdurws::ird::project::codec::parseHeadRecord(readAll(m_proj / "HEAD"));
    EXPECT_EQ(diskHead.manifestDigest, cvHex(direct));

    // 对象编址同源：域对象文件名＝对文件字节的 core 直算（§4.4.6）。
    const PlannedObject& obj = plan.newObjects[0];
    const std::string objBytes = readAll(
        m_proj / "objects" / obj.oid.toCanonical()
        / cvHex(contentVersionOf(std::string{
              reinterpret_cast<const char*>(obj.payload.data()),
              obj.payload.size()})));
    sdurws::ird::core::ContentDigester objDigester;
    objDigester.update(objBytes.data(), objBytes.size());
    // 文件字节直算的 hex 必与磁盘上该对象目录内的文件名一致（内容寻址
    // 闭环——读写两路径都锚定 core）。
    const ContentVersion directObj{objDigester.finalize()};
    EXPECT_TRUE(fs::exists(m_proj / "objects" / obj.oid.toCanonical()
                           / cvHex(directObj)));
}

// =====================================================================
// F1：准备失败（§7.6 行 1——.staging 目录创建失败）
// =====================================================================

/**
 * 锚定：NFR-REL-01／§7.6 F1——"建目录失败→Failed(write-rejected)；无修订；
 * HEAD 不变"。
 *
 * 注入：createDirectories 首次调用（＝.staging/<tx-id>）失败（ERROR_PATH_
 * NOT_FOUND→按映射表 WriteRejected）。
 * 预期：StoreError(WriteRejected)；HEAD 字节不变；无新修订目录；暂存区
 * 无残留事务目录（空壳已尽力清理）。
 */
TEST_F(TxEngineTest, F1_PrepareFails_StagingDirCreate)
{
    const auto before = snapshotCommitted();
    m_ops->failAt(fp::kCreateDirectories, 1, 3 /*ERROR_PATH_NOT_FOUND*/);
    const CommitPlan plan = makePlan();

    const StoreError err = expectStoreError(StoreErrorCode::WriteRejected, [&] {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
    });
    EXPECT_NE(err.what(), nullptr);

    // 字节不变（NFR-REL-01 观测面：失败前后已提交内容逐文件一致）。
    EXPECT_EQ(snapshotCommitted(), before);
    // 无残留事务目录（第 1 步失败形态："无残留或仅空目录"——空壳已清）。
    for (const std::string& name : stagingChildren()) {
        EXPECT_EQ(name, "tmp") << "暂存区不得残留事务目录";
    }
}

// =====================================================================
// F2：暂存写中途失败（§7.6 行 2）
// =====================================================================

/**
 * 锚定：NFR-REL-01／AT-13（进程内半边）／§7.6 F2——"第 N 字节写失败→
 * Failed(disk-full)；旧版本完整；.staging 残留部分文件"＋PM-08 恢复诊断。
 *
 * 注入：writeChunk 第 3 次（域对象第 2 个 chunk——跨块数据保证"中途"）
 * 返回 ERROR_DISK_FULL。
 * 预期：StoreError(DiskFull)；HEAD/r0 字节不变；残留事务目录被恢复扫描
 * 忽略并报告（ignoredStagingTxs），用户级诊断 PRJ-RECOVERY-IGNORED-
 * UNCOMMITTED 发出；闭包完整性不受残留影响。
 */
TEST_F(TxEngineTest, F2_StageWriteFailsMidway_DiskFull_ResidueIgnoredWithDiag)
{
    const auto before = snapshotCommitted();
    m_ops->failAt(fp::kWriteChunk, 3, 70 /*ERROR_DISK_FULL*/);
    // 200 000 字节＝4 个 chunk：occ 1＝元数据对象、occ 2～5＝域对象——
    // 命中第 3 次＝域对象中途（"第 N 字节写失败"的边界落点）。
    const CommitPlan plan = makePlan(kBigPayloadBytes);

    expectStoreError(StoreErrorCode::DiskFull, [&] {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
    });

    // 旧版本字节不变（PM-08 核心断言——已提交内容逐文件一致）。
    EXPECT_EQ(snapshotCommitted(), before);
    // 残留部分文件于 .staging（设计行为——不清理，恢复扫描处置；失败
    // 事务的目录是暂存根下唯一子项——共享 tmp 由非事务操作创建，本用例
    // 未涉及）。
    const auto children = stagingChildren();
    ASSERT_EQ(children.size(), std::size_t{1});
    EXPECT_NE(children[0], "tmp");
    const std::string& txDirName = children[0];
    // 恢复扫描（重开等价——新引擎实例）：残留被忽略并报告；闭包完整性
    // 不受影响；无"已发布未提交"。
    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    ASSERT_EQ(scan.ignoredStagingTxs.size(), std::size_t{1});
    EXPECT_EQ(scan.ignoredStagingTxs[0], txDirName);
    EXPECT_TRUE(scan.headIntegrityVerified);
    EXPECT_TRUE(scan.uncommittedRevisions.empty());
    EXPECT_EQ(scan.danglingObjectCount, 0u);
    EXPECT_TRUE(scan.headIntegrityVerified);
    EXPECT_TRUE(scan.uncommittedRevisions.empty());
    EXPECT_EQ(scan.danglingObjectCount, 0u);
    // 用户级恢复诊断（码值＝diagnostics.md §4.6 收编清单）。
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-RECOVERY-IGNORED-UNCOMMITTED"));
}

// =====================================================================
// F3：验证失败（§7.6 行 3——暂存文件位翻转）
// =====================================================================

/**
 * 锚定：NFR-REL-01／§7.6 F3——"暂存文件位翻转→Failed(store-corrupt)；
 * 无发布"＋§7.1 第 3 步"校验不符→Failed(store-corrupt)＋开发诊断"。
 *
 * 注入：首个暂存文件（元数据对象）关闭后翻转首字节（写后篡改——注入
 * 挂 closeHandle 的原因见文件头注）。
 * 预期：StoreError(StoreCorrupt)；objects/ 无任何新对象目录（零发布）；
 * HEAD 不变；开发诊断含 verify-failed。
 */
TEST_F(TxEngineTest, F3_VerifyFails_TamperedStagedBytes_StoreCorrupt_NoPublish)
{
    const auto before = snapshotCommitted();
    m_ops->corruptOnCloseOccurrence(1);  // 第 1 个关闭的暂存文件＝元数据对象
    const CommitPlan plan = makePlan(kBigPayloadBytes);

    expectStoreError(StoreErrorCode::StoreCorrupt, [&] {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
    });

    // 字节不变＋零发布：对象区无新增目录（第 4 步未进入——"无发布"）。
    EXPECT_EQ(snapshotCommitted(), before);
    // 开发诊断（§7.1 第 3 步失败列："磁盘/内存错误怀疑"的开发观测点）。
    EXPECT_TRUE(m_sink->devContains("verify-failed"));
}

// =====================================================================
// F4：发布部分失败（§7.6 行 4——第 k 个 rename 失败）
// =====================================================================

/**
 * 锚定：NFR-REL-01／AT-13（进程内半边）／§7.6 F4——"第 k 个 rename 失败
 * →Failed；已就位项悬挂；旧版本完整"＋§7.3 识别（闭包外＝未提交残留）
 * ＋自审 A-6（不把闭包外当作已提交历史）。
 *
 * 注入：publishNew 第 3 次（＝command.json——发布序：元数据对象→域对象
 * →command→manifest 最后）失败。
 * 预期：StoreError(WriteRejected)；HEAD 不变；已就位对象悬挂（扫描计数
 * danglingObjectCount＝2）；修订目录存在但无清单（残留归类）；恢复扫描：
 * uncommittedRevisions 含新修订、闭包完整性 true；按装载纪律（只认闭包
 * 成员）新修订对查询不可见。
 */
TEST_F(TxEngineTest, F4_PublishPartialFailure_DanglingResidue_ClosureExcludes)
{
    const auto before = snapshotCommitted();
    m_ops->failAt(fp::kPublishNew, 3, 1234 /*非映射特例码→WriteRejected*/);
    const CommitPlan plan = makePlan();

    expectStoreError(StoreErrorCode::WriteRejected, [&] {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
    });

    // 旧版本字节不变；已就位项保留（悬挂——不回滚，§7.1 第 4 步：发布
    // 部分完成的新增文件属设计形态，旧内容不得被改写）。
    expectCommittedUnchanged(before);
    const fs::path revDir = m_proj / "revisions" / plan.revisionId.toCanonical();
    EXPECT_TRUE(fs::exists(revDir));           // 目录已建（第 4 步中途）
    EXPECT_FALSE(fs::exists(revDir / "manifest.json"));  // 清单最后——未到达

    // 恢复扫描（§7.3/§7.4①④⑤）：闭包外识别＋悬挂计数＋完整性不受损。
    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    ASSERT_EQ(scan.uncommittedRevisions.size(), std::size_t{1});
    EXPECT_TRUE(scan.uncommittedRevisions[0] == plan.revisionId);
    EXPECT_EQ(scan.danglingObjectCount, 2u);  // 元数据＋域对象（悬挂，不删）
    EXPECT_TRUE(scan.headIntegrityVerified);  // 闭包成员完好——残留≠损坏
    EXPECT_EQ(scan.ignoredStagingTxs.size(), std::size_t{1});

    // 「已发布但未提交」对查询不可见（§7.3——装载纪律只认闭包成员：
    // 打开协议按闭包装载后，tryRevision 对残留修订返回空）。
    RevisionIndex sessionIndex;
    sessionIndex.registerRevision(m_seed.r0Manifest);
    sessionIndex.registerMetadata(m_seed.m0Ref, m_seed.m0);
    const auto closure = sessionIndex.committedClosure(m_seed.r0);
    ASSERT_EQ(closure.revisions.size(), std::size_t{1});
    EXPECT_FALSE(sessionIndex.tryRevision(plan.revisionId).has_value());
}

// =====================================================================
// F5：HEAD 切换失败（§7.6 行 5——发布完成、提交点未达成）
// =====================================================================

/**
 * 锚定：NFR-REL-01／PM-08／§7.6 F5——"MoveFileEx 失败→Failed；内容已
 * 发布未提交（可识别）；重开后历史不含该修订（PRJ-TX-9 的 F5 腿）"。
 *
 * 注入：replaceExisting 首次调用失败（ERROR_ACCESS_DENIED）。
 * 预期：StoreError(AccessDenied)；HEAD 字节不变；r1 清单/命令已发布；
 * 恢复扫描：uncommittedRevisions＝{r1}、暂存残留（含 head.new）被忽略；
 * 闭包外修订对查询不可见。
 */
TEST_F(TxEngineTest, F5_HeadSwitchFails_PublishedButUncommitted)
{
    const auto before = snapshotCommitted();
    m_ops->failAt(fp::kReplaceExisting, 1, 5 /*ERROR_ACCESS_DENIED*/);
    const CommitPlan plan = makePlan();

    expectStoreError(StoreErrorCode::AccessDenied, [&] {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
    });

    // 提交点未达成：HEAD 字节不变（唯一提交点＝HEAD 原子切换——§7.1
    // 第 5 步；切换失败即未提交）。已发布内容保留＝悬挂（设计形态）。
    expectCommittedUnchanged(before);
    // 内容已发布（第 4 步全部完成——悬挂形态）。
    const fs::path revDir = m_proj / "revisions" / plan.revisionId.toCanonical();
    EXPECT_TRUE(fs::exists(revDir / "manifest.json"));
    EXPECT_TRUE(fs::exists(revDir / "command.json"));

    // 重开等价：恢复扫描识别（§7.4①④＋§7.3）。
    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    ASSERT_EQ(scan.uncommittedRevisions.size(), std::size_t{1});
    EXPECT_TRUE(scan.uncommittedRevisions[0] == plan.revisionId);
    EXPECT_EQ(scan.ignoredStagingTxs.size(), std::size_t{1});  // head.new 残留
    EXPECT_TRUE(scan.headIntegrityVerified);

    // 重开后历史不含该修订（PRJ-TX-9 腿——闭包装载纪律下查询为空）。
    RevisionIndex reopened;
    reopened.registerRevision(m_seed.r0Manifest);
    reopened.registerMetadata(m_seed.m0Ref, m_seed.m0);
    EXPECT_FALSE(reopened.tryRevision(plan.revisionId).has_value());
    EXPECT_EQ(reopened.committedClosure(m_seed.r0).revisions.size(),
              std::size_t{1});
}

// =====================================================================
// F6：事件发布失败（§7.6 行 6——重试一次＋开发诊断，不回滚 D-18）
// =====================================================================

/**
 * 锚定：ARCH §6.4／§7.1 第 6 步／D-18——"bus.publish 失败重试一次＋开发
 * 诊断，不回滚——已提交修订保留"。
 *
 * 注入：总线首次 publish 抛出（重试成功）。
 * 预期：commit 成功；事件序完整（首事件重试后到达）；开发诊断含首败
 * 记录；HEAD 已切换。
 */
TEST_F(TxEngineTest, F6_EventPublishFails_RetriedOnce_CommitStands)
{
    m_bus->failFirst = 1;
    const CommitPlan plan = makePlan();
    const HeadRecord head = m_engine->readHead();

    CommitResult result;
    ASSERT_NO_FATAL_FAILURE(
        result = m_engine->commit(plan, head, m_seed.m0, m_seed.m0Ref));

    // 提交事实成立（D-18：事件失败不回滚）＋事件序完整（重试后到达）。
    const HeadRecord diskHead =
        sdurws::ird::project::codec::parseHeadRecord(readAll(m_proj / "HEAD"));
    EXPECT_TRUE(diskHead.revisionId == plan.revisionId);
    ASSERT_EQ(m_bus->events.size(), std::size_t{2});
    EXPECT_EQ(m_bus->events[0].kind, DomainEventKind::RevisionCommitted);
    EXPECT_EQ(m_bus->events[1].kind, DomainEventKind::DependencyInvalidated);
    // 调用序：RC 首败＋RC 重试＋DI＝3 次 publish。
    EXPECT_EQ(m_bus->calls, 3);
    // 开发诊断伴随首败（§7.1 第 6 步"失败重试一次＋开发诊断"）。
    EXPECT_TRUE(m_sink->devContains("event-publish-failed"));
}

/**
 * 锚定：D-18 极端腿——重试后仍失败：提交事实仍成立，事件丢失仅诊断
 * （订阅方经②端口重读自愈——事件非数据源，core D-09）。
 */
TEST_F(TxEngineTest, F6b_EventPublishFailsAll_CommitStillStands)
{
    m_bus->failFirst = 10;  // 全部 publish 抛出
    const CommitPlan plan = makePlan();
    const HeadRecord head = m_engine->readHead();

    CommitResult result;
    ASSERT_NO_FATAL_FAILURE(
        result = m_engine->commit(plan, head, m_seed.m0, m_seed.m0Ref));

    // 已提交状态完好：HEAD 切换＋闭包可达（不回滚——D-18）。
    const HeadRecord diskHead =
        sdurws::ird::project::codec::parseHeadRecord(readAll(m_proj / "HEAD"));
    EXPECT_TRUE(diskHead.revisionId == plan.revisionId);
    const auto closure = m_index->committedClosure(plan.revisionId);
    EXPECT_EQ(closure.revisions.size(), std::size_t{2});
    // 事件零到达；两事件各两败＝4 次调用＋4 条开发诊断。
    EXPECT_TRUE(m_bus->events.empty());
    EXPECT_EQ(m_bus->calls, 4);
}

// =====================================================================
// F7：清理失败（§7.6 行 7——已提交状态完好，残留仅诊断）
// =====================================================================

/**
 * 锚定：NFR-REL-01／§7.6 F7——"删除 .staging 失败→已提交状态完好；
 * 残留仅诊断"＋§7.1 第 7 步"清理失败不得破坏已提交版本"＋§7.5（恢复不
 * 依赖"目录存在"判定提交——已提交后残留事务目录同样被忽略）。
 */
TEST_F(TxEngineTest, F7_CleanupFails_CommittedStateIntact)
{
    m_ops->failAt(fp::kRemoveTree, 1, 5 /*ERROR_ACCESS_DENIED*/);
    const CommitPlan plan = makePlan();
    const HeadRecord head = m_engine->readHead();

    CommitResult result;
    ASSERT_NO_FATAL_FAILURE(
        result = m_engine->commit(plan, head, m_seed.m0, m_seed.m0Ref));

    // 提交点已达（commit 返回成功）且已提交字节完好。
    const HeadRecord diskHead =
        sdurws::ird::project::codec::parseHeadRecord(readAll(m_proj / "HEAD"));
    EXPECT_TRUE(diskHead == result.newHead);
    // 残留：事务目录仍在（删除失败）＋开发诊断（共享 tmp 由非事务操作
    // 创建——本用例只有失败清理的事务目录一个子项）。
    const auto children = stagingChildren();
    ASSERT_EQ(children.size(), std::size_t{1});
    EXPECT_NE(children[0], "tmp");
    EXPECT_TRUE(m_sink->devContains("cleanup-failed"));

    // 残留不参与读取路径：恢复扫描照常识别且已提交闭包完整（§7.5）。
    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    EXPECT_TRUE(scan.headIntegrityVerified);
    ASSERT_EQ(scan.uncommittedRevisions.size(), std::size_t{0});
    EXPECT_EQ(scan.ignoredStagingTxs.size(), std::size_t{1});
}

// =====================================================================
// F8：崩溃边界扫描——进程内模拟（进程级 kill 归 PRJ-T15/AT-13 复验）
// =====================================================================

/**
 * 锚定：PM-08／AT-13（进程内载体）／§7.6 F8——"各步进程崩溃：重启后已
 * 提交字节不变；未提交忽略＋恢复诊断"。
 *
 * 边界 T1（准备完成后）：注入第 3 次 createDirectories（＝第 2 步首个
 * 目录动作）失败——崩溃现场＝暂存目录已建、零文件写入。
 * 预期：旧版本字节不变；残留被忽略；闭包完整；无未提交修订。
 */
TEST_F(TxEngineTest, F8_AfterPrepare_OldStateByteInvariant)
{
    const auto before = snapshotCommitted();
    // createDirectories 序：1＝txDir、2＝tmp、3＝第 2 步元数据对象目录。
    m_ops->failAt(fp::kCreateDirectories, 3, 5);
    const CommitPlan plan = makePlan();

    expectStoreError(StoreErrorCode::AccessDenied, [&] {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
    });

    EXPECT_EQ(snapshotCommitted(), before);
    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    EXPECT_TRUE(scan.headIntegrityVerified);
    EXPECT_TRUE(scan.uncommittedRevisions.empty());
    EXPECT_EQ(scan.ignoredStagingTxs.size(), std::size_t{1});
}

/**
 * 锚定：同 F8——边界 T2/T3（暂存完成、验证通过后）。进程内在第 3 步
 * （纯读）无注入点，T2/T3 两边界状态同构（都已暂存、未发布），以一次
 * 注入覆盖。
 *
 * 注入：第 6 次 createDirectories（＝第 4 步首个对象目录）失败。
 * 预期：旧版本字节不变；暂存文件全部残留（含清单）；无修订目录；
 * 恢复扫描忽略＋完整性 true。
 */
TEST_F(TxEngineTest, F8_AfterStage_OldStateByteInvariant)
{
    const auto before = snapshotCommitted();
    // createDirectories 序：1 txDir、2 tmp、3/4 暂存对象目录、5 暂存修订
    // 目录、6＝第 4 步 objects/<metaOid>。
    m_ops->failAt(fp::kCreateDirectories, 6, 5);
    const CommitPlan plan = makePlan();

    expectStoreError(StoreErrorCode::AccessDenied, [&] {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
    });

    // 旧版本字节不变（对象/修订/HEAD 全部未被触碰——发布未开始）。
    EXPECT_EQ(snapshotCommitted(), before);
    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    EXPECT_TRUE(scan.headIntegrityVerified);
    EXPECT_TRUE(scan.uncommittedRevisions.empty());  // 无修订目录
    EXPECT_EQ(scan.ignoredStagingTxs.size(), std::size_t{1});
}

/**
 * 锚定：同 F8——边界 T5/T6/T7（提交点之后）：已提交状态（新修订）字节
 * 不变，而非旧状态——提交点＝HEAD 原子切换（§7.5"已切换即已提交"）。
 *
 * 注入：事件总线全失败＋清理失败（模拟 T6 边界崩溃后双故障现场）。
 * 预期：commit 不抛（提交事实成立）；HEAD/清单/命令/对象字节＝新修订；
 * 闭包含新修订；残留事务目录仅诊断（PRJ-RECOVERY-IGNORED-UNCOMMITTED
 * 与 PRJ-STORE-CORRUPT 均不出现——完好已提交存储不产生损坏/残留误报
 * 之外的混淆；此处残留确实存在故 ① 码应出现）。
 */
TEST_F(TxEngineTest, F8_AfterHeadSwitch_CommittedStateByteInvariant)
{
    m_bus->failFirst = 10;                       // 第 6 步全失败
    m_ops->failAt(fp::kRemoveTree, 1, 5);        // 第 7 步失败（残留现场）
    const CommitPlan plan = makePlan();
    const HeadRecord head = m_engine->readHead();

    CommitResult result;
    ASSERT_NO_FATAL_FAILURE(
        result = m_engine->commit(plan, head, m_seed.m0, m_seed.m0Ref));

    // 已提交状态字节不变（新状态——提交点之后崩溃的语义）：
    // HEAD＝newHead、清单字节摘要＝HEAD.manifestDigest、命令文件在位。
    const std::string headBytes = readAll(m_proj / "HEAD");
    const HeadRecord diskHead =
        sdurws::ird::project::codec::parseHeadRecord(headBytes);
    EXPECT_TRUE(diskHead == result.newHead);
    const std::string manifestBytes =
        readAll(m_proj / "revisions" / plan.revisionId.toCanonical()
                / "manifest.json");
    EXPECT_EQ(diskHead.manifestDigest,
              cvHex(contentVersionOf(manifestBytes)));
    EXPECT_TRUE(fs::exists(m_proj / "revisions" / plan.revisionId.toCanonical()
                           / "command.json"));
    // 闭包覆盖新修订（第 6 步失败不影响数据事实——D-18）。
    const auto closure = m_index->committedClosure(plan.revisionId);
    EXPECT_EQ(closure.revisions.size(), std::size_t{2});
    // 恢复扫描：完好已提交存储＋暂存残留——完整性 true、无未提交修订、
    // 残留目录按 ① 报告（§7.5：目录存在不判定提交，反之亦然）。
    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    EXPECT_TRUE(scan.headIntegrityVerified);
    EXPECT_TRUE(scan.uncommittedRevisions.empty());
    EXPECT_EQ(scan.ignoredStagingTxs.size(), std::size_t{1});
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-RECOVERY-IGNORED-UNCOMMITTED"));
    EXPECT_FALSE(m_sink->hasUserCode("PRJ-STORE-CORRUPT"));
}

// =====================================================================
// acceptance 3：.staging/tmp 临时物清理（D-09）与引擎产物不逸出事务目录
// =====================================================================

/**
 * 锚定：任务契约 PRJ-T07 acceptance 3 后半／D-09——"临时产物限
 * .staging/tmp"＋§4.1 .staging/tmp 行"残留……启动清理"。
 *
 * 操作：预置 .staging/tmp 编译临时残留（runtime 双编译临时物的代餐）；
 * 执行 F5 失败现场（提交点前失败）。
 * 预期：引擎全部产物（head.new/暂存对象/清单）只出现在事务目录内
 * （.staging 根与项目根无散落物）；恢复扫描后 tmp 整树清理、事务目录
 * 按残留报告保留（两种处置的对照即 §4.1 两行口径）。
 */
TEST_F(TxEngineTest, StagingTmp_CleanedByScan_EngineArtefactsStayInTxDir)
{
    // 预置共享 tmp 残留（非事务临时物——§4.1 .staging/tmp 行场景）。
    ASSERT_TRUE(writeRaw(m_proj / ".staging" / "tmp" / "compile-junk.bin",
                         std::string("junk")));

    const auto before = snapshotCommitted();
    m_ops->failAt(fp::kReplaceExisting, 1, 5);
    const CommitPlan plan = makePlan();
    expectStoreError(StoreErrorCode::AccessDenied, [&] {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
    });
    // 已发布内容保留＝悬挂（§7.1 第 4 步——旧内容不被改写，新增文件属
    // 设计形态；提交点未达成）。
    expectCommittedUnchanged(before);

    // 引擎产物不逸出事务目录：.staging 根＝{tmp, <tx>}；项目根无 *.new
    // 散落（head.new 在事务目录内）；tmp 内容未被提交事务触碰。
    std::vector<std::string> stRoot;
    std::error_code ec;
    for (fs::directory_iterator it(m_proj / ".staging", ec), end;
         it != end && !ec; it.increment(ec)) {
        stRoot.push_back(it->path().filename().string());
    }
    ASSERT_EQ(stRoot.size(), std::size_t{2});
    EXPECT_TRUE(fs::exists(m_proj / ".staging" / "tmp" / "compile-junk.bin"));
    EXPECT_FALSE(fs::exists(m_proj / "HEAD.new"));
    EXPECT_FALSE(fs::exists(m_proj / ".staging" / "head.new"));

    // 恢复扫描：tmp 整树清理（启动清理——§4.1）＋事务目录按残留报告。
    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    EXPECT_FALSE(fs::exists(m_proj / ".staging" / "tmp" / "compile-junk.bin"));
    EXPECT_EQ(scan.ignoredStagingTxs.size(), std::size_t{1});
}

// =====================================================================
// O-15：失败事务不回收序号（水位单调）
// =====================================================================

/**
 * 锚定：§6.1 O-15（PRJ-T06 落位的事务侧协同）——"失败不消费"的引擎口径
 * ＝失败后水位不回退：再次成功提交取得更高 seq；失败尝试的修订身份不在
 * 任何闭包（§4.5.1 结论④的进程内形态）。
 */
TEST_F(TxEngineTest, Commit_FailedTxThenSuccess_SeqMonotonicAndClosureClean)
{
    // 第一次提交：F2 注入失败（seq＝2 已消费）。
    m_ops->failAt(fp::kWriteChunk, 3, 70);
    const CommitPlan failed = makePlan(kBigPayloadBytes);
    expectStoreError(StoreErrorCode::DiskFull, [&] {
        m_engine->commit(failed, m_engine->readHead(), m_seed.m0,
                         m_seed.m0Ref);
    });
    EXPECT_EQ(m_index->lastRevisionSeq(), 2u);  // 水位只进不退（O-15）

    // 第二次提交：成功——seq＝3（不复用 2）；HEAD 单调推进。
    const CommitPlan ok = makePlan();
    ASSERT_NO_FATAL_FAILURE(m_engine->commit(ok, m_engine->readHead(),
                                             m_seed.m0, m_seed.m0Ref));
    const HeadRecord diskHead =
        sdurws::ird::project::codec::parseHeadRecord(readAll(m_proj / "HEAD"));
    EXPECT_EQ(diskHead.revisionSeq, 3u);
    EXPECT_TRUE(diskHead.revisionId == ok.revisionId);

    // 失败尝试的修订身份（已注册进程内索引）不在闭包——无孤岛误导。
    const auto closure = m_index->committedClosure(ok.revisionId);
    ASSERT_EQ(closure.revisions.size(), std::size_t{2});  // r0＋成功修订
    for (const RevisionId& id : closure.revisions) {
        EXPECT_FALSE(id == failed.revisionId) << "失败修订不得进入闭包";
    }
}

// =====================================================================
// 发布边界的调用方错误（内存段先于磁盘——"顺序即原子性"）
// =====================================================================

/**
 * 锚定：任务契约 PRJ-T07 acceptance 1（内存段校验零磁盘副作用）／PRJ-TX-12
 * 的发布边界集成——tipUpdate 与计划修订身份不一致＝调用方错误，任何磁盘
 * 写入之前拒绝（INV-M3 判据的二道防线在 validateMetadataPublish）。
 */
TEST_F(TxEngineTest, Commit_InconsistentTipUpdate_FailFastZeroDiskWrites)
{
    const auto before = snapshotCommitted();
    CommitPlan plan = makePlan();
    // 篡改增量：tip 指向另一个身份（≠plan.revisionId）——装配错误。
    TipUpdate bad;
    bad.branchId = plan.branchId;
    bad.newTip = RevisionId::generate();
    plan.metadataDelta.tipUpdate = bad;

    EXPECT_THROW(
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref),
        std::invalid_argument);

    // 零磁盘副作用（内存段先行的地面证据）。
    EXPECT_EQ(snapshotCommitted(), before);
    EXPECT_TRUE(stagingChildren().empty());  // 连暂存目录都未创建
}

/**
 * 锚定：同上——authoritativeRef 未按 RevisionIndex 时序契约注册＝调用方
 * 错误（invalid_argument），零磁盘副作用。
 */
TEST_F(TxEngineTest, Commit_UnregisteredAuthoritativeRef_FailFast)
{
    const auto before = snapshotCommitted();
    const CommitPlan plan = makePlan();
    // 幽灵引用键：从未注册进会话索引（validateMetadataPublish 的时序
    // 契约据此拒绝——invalid_argument，零磁盘副作用）。
    const ObjectRefPair ghost{ObjectId::generate(),
                              contentVersionOf("ghost-payload")};

    EXPECT_THROW(
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, ghost),
        std::invalid_argument);
    EXPECT_EQ(snapshotCommitted(), before);
}

// =====================================================================
// 恢复扫描基线：健康存储零发现
// =====================================================================

/**
 * 锚定：PM-08／§7.4——健康项目（成功提交后）恢复扫描零发现：无残留
 * 事务、无未提交修订、无悬挂对象、闭包完整、无用户级诊断。
 */
TEST_F(TxEngineTest, RecoveryScan_HealthyStore_NoFindings)
{
    const CommitPlan plan = makePlan();
    ASSERT_NO_FATAL_FAILURE(m_engine->commit(plan, m_engine->readHead(),
                                             m_seed.m0, m_seed.m0Ref));

    m_engine = std::make_unique<TxEngine>(m_ops.get(), m_proj, m_objects.get(),
                                          m_index.get(), m_bus.get(),
                                          m_sink.get());
    const TxRecoveryScan scan = m_engine->scanForRecovery();
    EXPECT_TRUE(scan.headIntegrityVerified);
    EXPECT_TRUE(scan.ignoredStagingTxs.empty());
    EXPECT_TRUE(scan.uncommittedRevisions.empty());
    EXPECT_EQ(scan.danglingObjectCount, 0u);
    EXPECT_TRUE(m_sink->reports.empty());  // 健康态零用户级诊断
}
