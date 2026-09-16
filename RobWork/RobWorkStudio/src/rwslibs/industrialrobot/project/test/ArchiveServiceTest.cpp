/**
 * @file   ArchiveServiceTest.cpp
 * @brief  归档端口用例组（单元面）——F9 归档边界（manifest 原子发布＝
 *         完整判据 D-13、重投递幂等 D-14、冲突拒绝）、端口与引用语义
 *         （ArchiveSessionRef 生命周期/relPath 白名单/跨项目拒绝）、
 *         abandon 全路径终结与绑定原修订不因 HEAD 前进拒绝（TASK-03/
 *         CON-02）。
 *
 * 设计依据：
 *   - units/project.md §5.6（归档端口契约）、§10.1（归档协作要素表）、
 *     §4.1 results 行（命名/可变性/manifest 原子替换）、§4.4.7（RunManifest
 *     字段契约与 manifestDigest 幂等判据）、§11（PRJ-TX 用例每注明需求/
 *     AT；F9 归档边界行）、§9.6/§9.7（失权防护/引用持有——门卫与排空
 *     断言面）；
 *   - 需求 TASK-03/PM-13（迟到结果归属）、CON-04（部分/失败不作缓存
 *     命中——D-13 存储侧承接）、CON-02（归档不因 HEAD 前进拒绝）、
 *     NFR-SEC-01（写入路径白名单消费侧）；
 *   - 任务契约 tasks/foundation/PRJ-T14.json acceptance 2/3/4。
 *
 * 测试口径登记（DTB §5.4）：
 *   1. PRJ-TX-8（进程内模拟）归属跨单元契约测试面（契约 note——§3.3
 *      "归档协作"），落位 ArchiveContractTest.cpp；本组承载 F9/引用
 *      语义/边界拒绝面（acceptance 2/3）。
 *   2. 提交修订（HEAD 前进用例）经内部写通道 executeCommit（ProjectStore
 *      Test 同款先例——私有头同单元测试消费，R-2 禁令是跨单元暴露）。
 *   3. "无 manifest 即不完整"的查询面观测＝listRuns（D-13 查询侧）＋
 *      重开后 listRuns（D-13 打开协议侧）＋磁盘 manifest.json 存在性，
 *      三通道互证。
 */

#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "ArchiveServiceImpl.hpp"
#include "Codec.hpp"
#include "ProjectStoreImpl.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace pd = sdurws::ird::project;
using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;
using pd::ArchiveBatch;
using pd::ArchiveEndReason;
using pd::ArchiveItem;
using pd::ArchiveRequest;
using pd::ArchiveSessionRef;
using pd::ArchiveStatus;
using pd::IResultArchivePort;
using pd::OpenStoreRequest;
using pd::OpenStoreResult;
using pd::ProjectStore;
using pd::ProjectStoreFactory;
using pd::RunManifest;
using pd::RunManifestItem;
using pd::StoreError;
using pd::StoreErrorCode;
using pd::codec::contentVersionOf;

namespace {

// ---------------------------------------------------------------------
// fake：诊断 sink（用户级码与开发级消息捕获——PRJ-ARCHIVE-CONFLICT 与
// 失权诊断的断言面；ProjectStoreTest CapturingSink 同款形态）
// ---------------------------------------------------------------------

class CapturingSink : public pd::IDiagnosticsSink {
public:
    std::vector<DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;

    void report(const DiagnosticRecord& record) override
    {
        reports.push_back(record);
    }
    void reportDev(const std::string& channel, const std::string& message) override
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
};

// ---------------------------------------------------------------------
// 磁盘与构造辅助
// ---------------------------------------------------------------------

/// 二进制整读；读失败显性失败（ProjectStoreTest 同款）。
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

/// 从磁盘 HEAD 读提交指针（地面事实）。
pd::HeadRecord readHeadFromDisk(const fs::path& projectDir)
{
    return pd::codec::parseHeadRecord(readAll(projectDir / "HEAD"));
}

/// 五元组构造（身份全生成——attempt 显式 1；attempt=0 反例由用例自构造）。
TaskIdentity makeTask(const ProjectId& project)
{
    TaskIdentity t;
    t.project = project;
    t.branch = BranchId::generate();
    t.revision = RevisionId::generate();
    t.run = RunId::generate();
    t.attempt = AttemptId{1};
    return t;
}

/// 归档请求构造（runDir 按白名单判据编址——与实现口径②同源）。
ArchiveRequest makeRequest(const TaskIdentity& task, const fs::path& storeRoot)
{
    ArchiveRequest r;
    r.task = task;
    r.runDir = storeRoot / "results" / task.run.toCanonical();
    r.runKind = "kinematics-eval";
    r.evaluationKey = "eval-key-1";
    return r;
}

/// 批次条目构造。
ArchiveItem makeItem(const std::string& relPath, const std::string& bytes)
{
    ArchiveItem item;
    item.relPath = relPath;
    item.bytes.assign(bytes.begin(), bytes.end());
    return item;
}

/// 批次字节的 64hex 摘要（cv- 前缀剥离——§4.4.7 sha256 字段口径）。
std::string hexOf(const ArchiveItem& item)
{
    return contentVersionOf(std::string{item.bytes.begin(), item.bytes.end()})
        .toCanonical()
        .substr(3);
}

/// manifest 构造（sha256/sizeBytes 由批次字节实算＝与磁盘一致的合法
/// 声明；finalizedAtUtc 固定值——D-14 重投递幂等判据要求重投递携带
/// 与首次相同的 manifest）。
RunManifest makeManifest(const TaskIdentity& task,
                         const std::vector<ArchiveItem>& items)
{
    RunManifest m;
    m.taskIdentity = task;
    for (const ArchiveItem& item : items) {
        RunManifestItem mi;
        mi.relPath = item.relPath;
        mi.sha256 = hexOf(item);
        mi.sizeBytes = item.bytes.size();
        m.items.push_back(mi);
    }
    m.runKind = "kinematics-eval";
    m.evaluationKey = "eval-key-1";
    m.finalizedAtUtc = "2026-09-16T10:00:00.000Z";
    return m;
}

/// 断言动作抛出指定稳定码的 StoreError（ProjectStoreTest 同款）。
template <class Fn>
void expectStoreError(StoreErrorCode code, Fn&& action)
{
    try {
        action();
        ADD_FAILURE() << "期望 StoreError(" << static_cast<int>(code)
                      << ") 未抛出";
    } catch (const StoreError& e) {
        EXPECT_EQ(e.code(), code) << "稳定码不符: " << e.what();
    }
}

/// 断言动作抛 std::invalid_argument（调用方契约违约面）。
template <class Fn>
void expectInvalidArgument(Fn&& action)
{
    try {
        action();
        ADD_FAILURE() << "期望 std::invalid_argument 未抛出";
    } catch (const std::invalid_argument&) {
    }
}

/// 会话注册表观测（acceptance 3 的会话计数面——经宿主实现访问器消费
/// ArchiveServiceImpl::activeSessionCount；私有头同单元测试消费先例）。
std::size_t archiveActiveSessions(ProjectStore* store)
{
    auto* impl = dynamic_cast<pd::ProjectStoreImpl*>(store);
    EXPECT_NE(impl, nullptr);
    return impl == nullptr ? 0 : impl->archiveService().activeSessionCount();
}

/// 关闭一次性回调观察者（排空时序断言）。
class CountingCloseObserver : public pd::ICloseObserver {
public:
    int calls = 0;
    void onStoreClosed(ProjectStore& /*store*/) override { ++calls; }
};

}  // namespace

// ---------------------------------------------------------------------
// 用例组：ArchiveService（套件名登记入 ird-test-report.json）
// ---------------------------------------------------------------------

class ArchiveServiceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_prj_archive_test"
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
        // 总根清理：失败保留现场（"TempDir 失败保留"惯例）。
        std::error_code ec;
        fs::remove_all(s_base, ec);
        if (ec) {
            std::cerr << "警告：测试根目录清理失败（保留现场）: "
                      << s_base.string() << " (" << ec.message() << ")\n";
        }
    }

    void SetUp() override
    {
        std::error_code ec;
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter))
                / "proj.rwdesign";
        ASSERT_FALSE(ec);
        m_sink = std::make_shared<CapturingSink>();
    }

    /// 新建可写项目（注入捕获 sink；事件总线默认空——事件面归契约测试）。
    OpenStoreResult createNew()
    {
        return ProjectStoreFactory::createNew(m_dir, "Archive Test", nullptr,
                                              m_sink.get());
    }

    /// 打开（默认可写——重开用例）。
    OpenStoreResult open()
    {
        OpenStoreRequest req;
        req.path = m_dir;
        req.diagnostics = m_sink.get();
        return ProjectStoreFactory::open(req);
    }

    /// 只读打开（PM-07 门卫面用例）。
    OpenStoreResult openReadOnly()
    {
        OpenStoreRequest req;
        req.path = m_dir;
        req.mode = pd::OpenMode::ReadOnly;
        req.diagnostics = m_sink.get();
        return ProjectStoreFactory::open(req);
    }

    /// 经内部写通道提交一次合法修订（ProjectStoreTest 同款——HEAD 前进
    /// 用例的数据面；分支/父修订自磁盘 HEAD 读取）。
    void commitOneRevision(ProjectStore& store)
    {
        using sdurws::ird::core::ObjectId;
        using pd::revindex::TipUpdate;
        using pd::tx::CommitPlan;
        using pd::tx::PlannedObject;
        const pd::HeadRecord head = readHeadFromDisk(m_dir);
        CommitPlan plan;
        plan.revisionId = RevisionId::generate();
        plan.branchId = head.branchId;
        plan.parentRevisionId = head.revisionId;
        plan.committedAtUtc = "2026-09-16T02:00:00Z";
        PlannedObject obj;
        obj.oid = ObjectId::generate();
        obj.objectTypeToken = "RobotDesign";
        obj.payload.assign(32, 'D');
        plan.newObjects.push_back(std::move(obj));
        TipUpdate tip;
        tip.branchId = head.branchId;
        tip.newTip = plan.revisionId;
        plan.metadataDelta.tipUpdate = tip;
        plan.command.commandType = "add-mass-point";
        plan.command.payloadFormatVersion = 1;
        plan.command.payloadCanonical = "{\"point\":1}";
        plan.command.summary = "archive test commit";
        auto* impl = dynamic_cast<pd::ProjectStoreImpl*>(&store);
        ASSERT_NE(impl, nullptr);
        impl->executeCommit(plan);
    }

    const fs::path& dir() const { return m_dir; }
    CapturingSink& sink() { return *m_sink; }

private:
    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dir;
    std::shared_ptr<CapturingSink> m_sink;
};

fs::path ArchiveServiceTest::s_base;
int ArchiveServiceTest::s_caseCounter = 0;

// =====================================================================
// acceptance 2（F9 归档边界）：manifest 原子发布＝完整判据（D-13/CON-04）
// =====================================================================

/**
 * 锚定：F9/§10.1"分批写入与最终完整发布"/D-13/CON-04——acceptance 2。
 *
 * 前置：可写项目。操作：begin→writeBatch→（发布前三通道观测）→
 * finalize→（发布后三通道观测＋manifest 契约面断言）→重开复验。
 * 预期：发布前 manifest.json 不存在、listRuns 不列；发布后 manifest.json
 * 存在、listRuns 列出且五元组/登记串/时刻逐字段一致；磁盘 manifest 经
 * parseRunManifest 解析与预期结构体全等、dump 逐字节还原（§4.8
 * round-trip）；manifestDigest 与重算摘要一致（幂等判据自洽——D-14
 * 的数据面前提）；重开后完整运行仍可见。
 */
TEST_F(ArchiveServiceTest, F9_FinalizeManifestIsCompletenessJudge)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    ASSERT_TRUE(opened.writable);
    ProjectStore& store = *opened.store;
    IResultArchivePort& archive = store.archive();

    const TaskIdentity task = makeTask(store.projectId());
    const ArchiveRequest request = makeRequest(task, dir());
    const std::vector<ArchiveItem> batch{makeItem("part-1.json", "{\"a\":1}"),
                                         makeItem("sub/dir/part-2.bin", "BB")};

    // begin：运行目录创建＋会话登记（§4.1"何时创建＝归档 begin 时"）。
    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(static_cast<bool>(session));
    EXPECT_EQ(archiveActiveSessions(&store), 1u);

    // finalize 前：批次文件可见但不完整（D-13）——磁盘 manifest 缺席、
    // 查询面不列。
    const ArchiveStatus written
        = archive.writeBatch(session, ArchiveBatch{batch});
    ASSERT_TRUE(written.ok) << (written.error ? written.error->what() : "");
    EXPECT_FALSE(fs::exists(request.runDir / "manifest.json"));
    EXPECT_TRUE(store.query().listRuns(task.revision).empty());

    // finalize：manifest 原子发布＝完整（提交点）；会话终结（引用归零
    // ——排空计数面）。
    const ArchiveStatus fin
        = archive.finalize(session, makeManifest(task, batch));
    ASSERT_TRUE(fin.ok) << (fin.error ? fin.error->what() : "");
    const fs::path manifestFile = request.runDir / "manifest.json";
    ASSERT_TRUE(fs::exists(manifestFile));
    EXPECT_EQ(archiveActiveSessions(&store), 0u);

    // 查询面：listRuns 列出且逐字段一致（§4.4.7 原样透传）。
    const std::vector<pd::RunInfo> runs = store.query().listRuns(task.revision);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].runId, task.run);
    EXPECT_TRUE(runs[0].task == task);
    EXPECT_EQ(runs[0].runKind, "kinematics-eval");
    EXPECT_EQ(runs[0].evaluationKey, "eval-key-1");
    EXPECT_EQ(runs[0].finalizedAtUtc, "2026-09-16T10:00:00.000Z");

    // 磁盘契约面：解析与预期全等＋round-trip 逐字节＋摘要自洽。
    const std::string bytes = readAll(manifestFile);
    const RunManifest parsed = pd::codec::parseRunManifest(bytes);
    RunManifest expected = makeManifest(task, batch);
    expected.manifestDigest = pd::computeManifestDigestHex(expected);  // 重算
    EXPECT_TRUE(parsed == expected);
    EXPECT_EQ(pd::codec::dump(parsed), bytes);  // parse(dump(x))==x（§4.8）

    // 重开项目：完整运行跨会话可见（D-13 打开协议侧）。
    opened.store->requestClose();
    opened.store.reset();
    OpenStoreResult reopened = open();
    ASSERT_NE(reopened.store, nullptr);
    const std::vector<pd::RunInfo> runsAfter
        = reopened.store->query().listRuns(task.revision);
    ASSERT_EQ(runsAfter.size(), 1u);
    EXPECT_EQ(runsAfter[0].runId, task.run);
    reopened.store->requestClose();
}

// =====================================================================
// acceptance 2：同 attempt 重投递幂等（D-14）与内容冲突拒绝
// =====================================================================

/**
 * 锚定：§10.1"重投递幂等与内容冲突"/D-14/PRJ-ARCHIVE-CONFLICT——
 * acceptance 2。
 *
 * 前置：一次完整归档已发布 manifest。操作：重新 begin（身份一致——
 * 幂等放行）→重投递同一 finalize→再以不同内容重投递。
 * 预期：幂等成功（ok 且不重写——磁盘字节不变）；内容分歧→
 * ArchiveStatus{false, ArchiveConflict}＋PRJ-ARCHIVE-CONFLICT 用户诊断；
 * 既有 manifest 不被覆盖；冲突后会话未终结（可 abandon 收尾）。
 */
TEST_F(ArchiveServiceTest, D14_RedeliveryIdempotentAndConflictRejected)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    ProjectStore& store = *opened.store;
    IResultArchivePort& archive = store.archive();

    const TaskIdentity task = makeTask(store.projectId());
    const ArchiveRequest request = makeRequest(task, dir());
    const std::vector<ArchiveItem> batch{makeItem("result.json", "R1")};
    const RunManifest manifest = makeManifest(task, batch);

    // 首次归档（正常发布）。
    ArchiveSessionRef s1 = archive.begin(request);
    ASSERT_TRUE(archive.writeBatch(s1, ArchiveBatch{batch}).ok);
    ASSERT_TRUE(archive.finalize(s1, manifest).ok);
    const std::string firstBytes = readAll(request.runDir / "manifest.json");

    // 重投递（同一 attempt、同一 manifest 内容）：begin 身份一致放行 →
    // finalize 摘要一致幂等成功（不重写——字节不变）。
    ArchiveSessionRef s2 = archive.begin(request);
    ASSERT_TRUE(static_cast<bool>(s2));
    const ArchiveStatus again = archive.finalize(s2, manifest);
    ASSERT_TRUE(again.ok) << (again.error ? again.error->what() : "");
    EXPECT_EQ(readAll(request.runDir / "manifest.json"), firstBytes);

    // 内容分歧重投递：items 增补（合法形态、摘要必不同）→ 冲突拒绝＋
    // 用户诊断；既有 manifest 不覆盖（数据分歧不掩盖）。
    ArchiveSessionRef s3 = archive.begin(request);
    ASSERT_TRUE(static_cast<bool>(s3));
    std::vector<ArchiveItem> diverged = batch;
    diverged.push_back(makeItem("extra.json", "E1"));
    const ArchiveStatus conflict
        = archive.finalize(s3, makeManifest(task, diverged));
    ASSERT_FALSE(conflict.ok);
    ASSERT_TRUE(conflict.error.has_value());
    EXPECT_EQ(conflict.error->code(), StoreErrorCode::ArchiveConflict);
    EXPECT_TRUE(sink().hasUserCode("PRJ-ARCHIVE-CONFLICT"));
    EXPECT_EQ(readAll(request.runDir / "manifest.json"), firstBytes);
    // 冲突后会话未终结——调用方 abandon 收尾（§10.1 失败处置面）。
    EXPECT_EQ(archiveActiveSessions(&store), 1u);
    archive.abandon(s3, ArchiveEndReason::Failed);
    EXPECT_EQ(archiveActiveSessions(&store), 0u);
}

/**
 * 锚定：§10.1"批次级重投递"行——acceptance 2。
 *
 * 操作：同 relPath 同内容重投递 / 同 relPath 不同内容重投递。
 * 预期：一致→跳过（ok，磁盘零变化）；不一致→ArchiveConflict＋既有
 * 文件不被覆盖。
 */
TEST_F(ArchiveServiceTest, D14_BatchLevelRedeliverySkipOrConflict)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    IResultArchivePort& archive = opened.store->archive();
    const TaskIdentity task = makeTask(opened.store->projectId());
    const ArchiveRequest request = makeRequest(task, dir());

    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(
        archive.writeBatch(session, ArchiveBatch{{makeItem("a.bin", "AAA")}})
            .ok);
    const std::string first = readAll(request.runDir / "a.bin");

    // 同内容重投递：跳过（幂等）。
    const ArchiveStatus same
        = archive.writeBatch(session, ArchiveBatch{{makeItem("a.bin", "AAA")}});
    ASSERT_TRUE(same.ok);
    EXPECT_EQ(readAll(request.runDir / "a.bin"), first);

    // 不同内容同路径：冲突拒绝＋文件保持原内容。
    const ArchiveStatus diff
        = archive.writeBatch(session, ArchiveBatch{{makeItem("a.bin", "ZZZ")}});
    ASSERT_FALSE(diff.ok);
    ASSERT_TRUE(diff.error.has_value());
    EXPECT_EQ(diff.error->code(), StoreErrorCode::ArchiveConflict);
    EXPECT_TRUE(sink().hasUserCode("PRJ-ARCHIVE-CONFLICT"));
    EXPECT_EQ(readAll(request.runDir / "a.bin"), first);
}

/**
 * 锚定：O-12 完整性发布义务（实现口径④）＋F9 边界——acceptance 2。
 *
 * 操作：finalize 的 manifest 声明与磁盘不符（sha256 错/size 错/文件缺）。
 * 预期：ArchiveStatus{false, StoreCorrupt}；manifest.json 不被发布
 * （失败不产生"假完整"——D-13 判据不被污染）；会话未终结（正确声明
 * 重试成功）。
 */
TEST_F(ArchiveServiceTest, F9_FinalizeVerifiesManifestAgainstDisk)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    IResultArchivePort& archive = opened.store->archive();
    const TaskIdentity task = makeTask(opened.store->projectId());
    const ArchiveRequest request = makeRequest(task, dir());
    const std::vector<ArchiveItem> batch{makeItem("r.bin", "DATA")};
    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(archive.writeBatch(session, ArchiveBatch{batch}).ok);

    // sha256 声明不符。
    RunManifest badDigest = makeManifest(task, batch);
    badDigest.items[0].sha256.assign(64, '0');
    const ArchiveStatus s1 = archive.finalize(session, badDigest);
    ASSERT_FALSE(s1.ok);
    ASSERT_TRUE(s1.error.has_value());
    EXPECT_EQ(s1.error->code(), StoreErrorCode::StoreCorrupt);
    EXPECT_FALSE(fs::exists(request.runDir / "manifest.json"));

    // size 声明不符。
    RunManifest badSize = makeManifest(task, batch);
    badSize.items[0].sizeBytes = 999;
    const ArchiveStatus s2 = archive.finalize(session, badSize);
    ASSERT_FALSE(s2.ok);
    ASSERT_TRUE(s2.error.has_value());
    EXPECT_EQ(s2.error->code(), StoreErrorCode::StoreCorrupt);

    // 声明了未写入的文件（缺失）。
    std::vector<ArchiveItem> withExtra = batch;
    withExtra.push_back(makeItem("missing.bin", "M"));
    const ArchiveStatus s3
        = archive.finalize(session, makeManifest(task, withExtra));
    ASSERT_FALSE(s3.ok);
    ASSERT_TRUE(s3.error.has_value());
    EXPECT_EQ(s3.error->code(), StoreErrorCode::StoreCorrupt);
    EXPECT_FALSE(fs::exists(request.runDir / "manifest.json"));

    // 会话仍活跃（失败不终结）——正确 manifest 重试成功。
    EXPECT_EQ(archiveActiveSessions(opened.store.get()), 1u);
    const ArchiveStatus good
        = archive.finalize(session, makeManifest(task, batch));
    ASSERT_TRUE(good.ok);
    EXPECT_TRUE(fs::exists(request.runDir / "manifest.json"));
}

// =====================================================================
// acceptance 3（端口与引用语义）
// =====================================================================

/**
 * 锚定：acceptance 3——ArchiveSessionRef 生命周期（引用持有通道＋失效
 * 句柄 fail-fast）。
 *
 * 操作：begin 后观测 requestClose 的排空计数（会话＝§9.7 引用持有者）
 * →finalize 终结→复用句柄；空句柄直接调用；abandon 幂等。
 * 预期：requestClose 返回 ≥1 且上下文不关闭（会话持票据——存活通道）；
 * 终结后 writeBatch/finalize 抛 invalid_argument（fail-fast——实现口径
 * ⑦）；空句柄同样 fail-fast；已终结会话再 abandon 不抛（全路径终结
 * 容忍形态）。
 */
TEST_F(ArchiveServiceTest, SessionRef_LifecycleAndOwnership)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    ProjectStore& store = *opened.store;
    IResultArchivePort& archive = store.archive();
    const TaskIdentity task = makeTask(store.projectId());
    const ArchiveRequest request = makeRequest(task, dir());
    const std::vector<ArchiveItem> batch{makeItem("x.bin", "X")};

    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(static_cast<bool>(session));
    const ArchiveStatus written
        = archive.writeBatch(session, ArchiveBatch{{makeItem("x.bin", "X")}});
    ASSERT_TRUE(written.ok) << (written.error ? written.error->what() : "");

    // 会话＝在途引用持有者：requestClose 进入排空并报告在途数，不完成
    // 关闭（§9.7——acceptance 3"归档期间的引用持有"）。
    const std::uint32_t pending = store.requestClose();
    EXPECT_GE(pending, 1u);
    EXPECT_FALSE(store.closed());

    // 终结：finalize 释放票据→排空归零→关闭完成（事件与回调时序面归
    // 契约测试，此处断言终结后句柄失效）。
    const ArchiveStatus fin
        = archive.finalize(session, makeManifest(task, batch));
    ASSERT_TRUE(fin.ok) << (fin.error ? fin.error->what() : "");
    EXPECT_TRUE(store.closed());

    // 失效句柄：写方法 fail-fast（空句柄/已终结句柄同面）。
    expectInvalidArgument(
        [&] { archive.writeBatch(session, ArchiveBatch{batch}); });
    expectInvalidArgument([&] {
        archive.finalize(session, makeManifest(task, batch));
    });
    expectInvalidArgument([&] {
        ArchiveBatch empty;
        archive.writeBatch(ArchiveSessionRef{}, empty);
    });

    // abandon 幂等：已终结会话再 abandon 不抛（全路径终结容忍形态）。
    archive.abandon(session, ArchiveEndReason::Completed);
}

/**
 * 锚定：acceptance 2/3——abandon 全路径终结（§10.1"成功/取消/失败/
 * 强制终止的结束"＋A-4 无永久等待）。
 *
 * 操作：begin→writeBatch→requestClose（排空挂起）→finalize 以声明不
 * 符失败→abandon(Failed)。
 * 预期：失败不终结会话、上下文不关闭（引用仍在）；abandon 后排空归零
 * →关闭完成＋一次性回调（失败即 abandon 释放引用——A-4）；批次残留
 * 保留为"未完成"（无 manifest——CON-04），重开后 listRuns 不列。
 */
TEST_F(ArchiveServiceTest, Abandon_FullPathTerminationNoPermanentWait)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    ProjectStore& store = *opened.store;
    IResultArchivePort& archive = store.archive();
    const TaskIdentity task = makeTask(store.projectId());
    const ArchiveRequest request = makeRequest(task, dir());
    const std::vector<ArchiveItem> batch{makeItem("partial.bin", "P")};

    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(archive.writeBatch(session, ArchiveBatch{batch}).ok);

    CountingCloseObserver observer;
    store.subscribeClose(observer);

    // 排空挂起（会话持票据）→失败（声明不符）→会话未终结、不关闭。
    EXPECT_GE(store.requestClose(), 1u);
    RunManifest bad = makeManifest(task, batch);
    bad.items[0].sha256.assign(64, 'f');
    const ArchiveStatus failed = archive.finalize(session, bad);
    ASSERT_FALSE(failed.ok);
    EXPECT_FALSE(store.closed());

    // abandon（Failed）：责任终结→排空归零→关闭＋一次性回调。
    archive.abandon(session, ArchiveEndReason::Failed);
    EXPECT_TRUE(store.closed());
    EXPECT_EQ(observer.calls, 1);

    // 批次残留保留为"未完成"（§10.1"取消/失败"行——不删除）：磁盘
    // 批次仍在、manifest 缺席；重开后 listRuns 不列（CON-04 承接）。
    EXPECT_TRUE(fs::exists(request.runDir / "partial.bin"));
    EXPECT_FALSE(fs::exists(request.runDir / "manifest.json"));
    opened.store.reset();
    OpenStoreResult reopened = open();
    ASSERT_NE(reopened.store, nullptr);
    EXPECT_TRUE(reopened.store->query().listRuns(task.revision).empty());
    reopened.store->requestClose();
}

/**
 * 锚定：acceptance 3——relPath 白名单（NFR-SEC-01 消费侧）与空批边界。
 *
 * 操作：以各类违约 relPath 调 writeBatch；空批调用。
 * 预期：违约一律 fail-fast（穿越/绝对/盘符/反斜杠/保留名/暂存后缀/
 * 设备名/控制字符/超长）；违约零磁盘副作用；空批 no-op 成功。
 */
TEST_F(ArchiveServiceTest, RelPath_WhitelistFailFast)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    IResultArchivePort& archive = opened.store->archive();
    const TaskIdentity task = makeTask(opened.store->projectId());
    const ArchiveRequest request = makeRequest(task, dir());
    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(static_cast<bool>(session));

    // 违约样本（穿越防护/路径结构/保留名三类——NFR-SEC-01 白名单）。
    const std::string bad[] = {"../escape.bin", "a/../../b.bin", "/abs.bin",
                               "C:/x.bin",      "a\\b.bin",      "manifest.json",
                               "tmp.ird-part",  "CON",           "nul.txt",
                               "a/b\x01"
                               "c.bin"};
    for (const std::string& rel : bad) {
        expectInvalidArgument(
            [&] { archive.writeBatch(session, ArchiveBatch{{makeItem(rel, "z")}}); });
    }
    // 超长（257 字节）与空串。
    expectInvalidArgument([&] {
        archive.writeBatch(session,
                           ArchiveBatch{{makeItem(std::string(257, 'a'), "z")}});
    });
    expectInvalidArgument(
        [&] { archive.writeBatch(session, ArchiveBatch{{makeItem("", "z")}}); });

    // 违约零磁盘副作用：运行目录内无任何批次文件。
    std::error_code ec;
    EXPECT_TRUE(fs::is_empty(request.runDir, ec));

    // 空批＝no-op 成功（幂等重投递下的边界形态）。
    ArchiveBatch empty;
    const ArchiveStatus ok = archive.writeBatch(session, empty);
    ASSERT_TRUE(ok.ok);
}

/**
 * 锚定：§5.6 begin 校验清单——acceptance 3（跨项目拒绝 AT-10/白名单/
 * 调用方契约）与 acceptance 1"跨项目事件不写入"的存储侧闸门。
 *
 * 操作：跨项目 task／attempt=0／runKind 空／runDir 白名单外。
 * 预期：一律 fail-fast；跨项目拒绝附开发诊断（AT-10 观察面）；全部
 * 拒绝零会话建立。
 */
TEST_F(ArchiveServiceTest, Begin_RejectionMatrix)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    IResultArchivePort& archive = opened.store->archive();
    const TaskIdentity task = makeTask(opened.store->projectId());

    // 跨项目（AT-10 反例）：他项目身份＋本上下文——拒绝＋开发诊断。
    TaskIdentity foreign = task;
    foreign.project = ProjectId::generate();
    expectInvalidArgument([&] { archive.begin(makeRequest(foreign, dir())); });
    bool sawCrossProjectDev = false;
    for (const auto& d : sink().devs) {
        if (d.second.find("跨项目归档请求") != std::string::npos) {
            sawCrossProjectDev = true;
        }
    }
    EXPECT_TRUE(sawCrossProjectDev);

    // attempt=0（五元组无效——core 契约：缺一即 false）。
    TaskIdentity zeroAttempt = task;
    zeroAttempt.attempt = AttemptId{0};
    expectInvalidArgument([&] {
        archive.begin(makeRequest(zeroAttempt, dir()));
    });

    // runKind 空（登记透传 token 口径）。
    ArchiveRequest emptyKind = makeRequest(task, dir());
    emptyKind.runKind.clear();
    expectInvalidArgument([&] { archive.begin(emptyKind); });

    // runDir 白名单外（指向项目内非 results/<run> 编址——不重新推导，
    // 拒绝）。
    ArchiveRequest wrongDir = makeRequest(task, dir() / "objects" / "x");
    expectInvalidArgument([&] { archive.begin(wrongDir); });

    // 全部拒绝：无会话建立。
    EXPECT_EQ(archiveActiveSessions(opened.store.get()), 0u);
}

/**
 * 锚定：acceptance 4＋TASK-03/CON-02——绑定原修订、不因 HEAD 前进
 * 拒绝（ARCH §4.5 A1/A8 承接）。
 *
 * 操作：读磁盘 HEAD 得 r0 → 提交两次新修订推进 HEAD → 归档绑定 r0
 * 的运行。
 * 预期：HEAD 前进后旧修订的归档照常成功（runDir 绑定原修订——§10.1
 * "当前性与历史归档"行；当前性判定归 evidence，project 不拒绝）；
 * 查询面 r0 名下可见。
 */
TEST_F(ArchiveServiceTest, Task03_ArchiveBoundToOriginalRevisionIgnoreHeadAdvance)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    ProjectStore& store = *opened.store;

    // r0＝创建时的 HEAD 修订（磁盘地面事实）。
    const RevisionId r0 = readHeadFromDisk(dir()).revisionId;

    // 推进 HEAD（两次合法提交）。
    commitOneRevision(store);
    commitOneRevision(store);
    const RevisionId headNow = readHeadFromDisk(dir()).revisionId;
    ASSERT_FALSE(headNow == r0);

    // 绑定 r0 的迟到归档照常受理（begin 校验清单不含"修订须为 HEAD"
    // ——A8：归档位置不重新推导、HEAD 前进无关）。
    IResultArchivePort& archive = store.archive();
    TaskIdentity task = makeTask(store.projectId());
    task.revision = r0;
    const ArchiveRequest request = makeRequest(task, dir());
    const std::vector<ArchiveItem> batch{makeItem("old-run.json", "{}")};
    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(static_cast<bool>(session));
    ASSERT_TRUE(archive.writeBatch(session, ArchiveBatch{batch}).ok);
    const ArchiveStatus fin
        = archive.finalize(session, makeManifest(task, batch));
    ASSERT_TRUE(fin.ok) << (fin.error ? fin.error->what() : "");
    EXPECT_TRUE(fs::exists(request.runDir / "manifest.json"));
    // 查询面：r0 名下可见（历史修订的结果清单）。
    EXPECT_EQ(store.query().listRuns(r0).size(), 1u);
}

/**
 * 锚定：§9.6/§9.7 门卫面——acceptance 3（只读上下文 begin 拒绝）。
 *
 * 操作：建可写项目→关闭→只读重开→begin。
 * 预期：StoreError(LockHeldByOther)＋PRJ-LOCK-HELD（PM-07 禁写的归档
 * 面落实——与草稿写轨同表）。
 */
TEST_F(ArchiveServiceTest, Begin_ReadOnlyContextRejected)
{
    // 先建可写项目并排空关闭（锁释放）→只读重开。
    {
        OpenStoreResult created = createNew();
        ASSERT_NE(created.store, nullptr);
        created.store->requestClose();
    }
    OpenStoreResult opened = openReadOnly();
    ASSERT_NE(opened.store, nullptr);
    EXPECT_FALSE(opened.writable);
    IResultArchivePort& archive = opened.store->archive();
    const TaskIdentity task = makeTask(opened.store->projectId());
    expectStoreError(StoreErrorCode::LockHeldByOther,
                     [&] { archive.begin(makeRequest(task, dir())); });
    EXPECT_TRUE(sink().hasUserCode("PRJ-LOCK-HELD"));
    opened.store->requestClose();
}

/**
 * 锚定：acceptance 4（P-PR-4 处置的行为面佐证之一）——Draining 拒绝
 * 新 begin、既有会话继续（§9.7 分野；存活时序全链归契约测试）。
 *
 * 操作：begin 后 requestClose（排空挂起）→新 begin。
 * 预期：新 begin 抛 StoreError(ContextClosed)（排空期不接受新归档
 * 预留）；既有会话 writeBatch 照常成功；abandon 后关闭完成。
 */
TEST_F(ArchiveServiceTest, Draining_RejectsNewBeginButServesExistingSession)
{
    OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    ProjectStore& store = *opened.store;
    IResultArchivePort& archive = store.archive();

    // 既有会话（派发时预留——PRJ-TX-8 前置形态）。
    const TaskIdentity task = makeTask(store.projectId());
    const ArchiveRequest request = makeRequest(task, dir());
    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(static_cast<bool>(session));

    // 排空：新 begin 拒绝（§9.7），既有会话写继续。
    EXPECT_GE(store.requestClose(), 1u);
    const TaskIdentity other = makeTask(store.projectId());
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { archive.begin(makeRequest(other, dir())); });
    const ArchiveStatus written
        = archive.writeBatch(session, ArchiveBatch{{makeItem("late.bin", "L")}});
    ASSERT_TRUE(written.ok);

    // abandon 收尾：排空归零→关闭完成（无永久等待——A-4）。
    archive.abandon(session, ArchiveEndReason::Canceled);
    EXPECT_TRUE(store.closed());
}
