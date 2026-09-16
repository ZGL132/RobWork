/**
 * @file   ProjectStoreTest.cpp
 * @brief  存储上下文与工厂用例组——打开协议②③⑤（PM-02）、旧格式/未来
 *         版本稳定拒绝（PM-06）、恢复报告断言（PM-08/§7.4）、生命周期
 *         排空（PM-03 存储侧/§9.6/§9.7）、只读打开（PM-07）与 P-PR-1/
 *         P-PR-6 处置自证。
 *
 * 设计依据：
 *   - units/project.md §5.1（open/createNew 契约表）、§8.7（打开协议
 *     服务侧——"激活前失败不影响当前项目"为 PRJ-T08 完成条件具名用例）、
 *     §7.4（恢复顺序①③④⑤→RecoveryReport）、§7.3（闭包外＝未提交
 *     残留，查询不可见）、§8.11（旧格式/未来版本拒绝）、§9.2/§9.3
 *     （只读打开/进程内重复可写打开拒绝）、§9.6（失权防护——上下文
 *     门卫）、§9.7（Draining 排空）、§11（用例每注明需求/AT；PRJ-TX-9
 *     全量用例归 PRJ-T15，本组承接其 T08 落位面）、§4.1（project.json
 *     缺失＝非项目目录；跨文件一致性）；
 *   - 需求 PM-01/02/03/06/07/08、SA-17、NFR-COR-02；
 *   - 任务契约 tasks/foundation/PRJ-T08.json acceptance 1～4：
     acceptance 1＝PRJ-TX-9 落位面（Closed/Draining 迟到写拒绝＋稳定
     诊断、闭包外修订不可见、激活前失败不影响当前项目）；acceptance 2＝
     旧格式/未来版本拒绝＋升级指引数据＋恢复报告逐字段断言；acceptance 3＝
     open/createNew/关闭排空/失权写拒绝/只读打开查询；acceptance 4＝
     P-PR-1（core 身份基线消费）与 P-PR-6（诊断码收编清单＋sink 注入）
     处置自证。
 *
 * 测试口径登记（DTB §5.4）：
 *   1. 写入口代表：本任务落位面上的唯一写路径＝ProjectStoreImpl 的内部
 *      写通道 executeCommit（§6.1 S6 后半——命令服务 T10 挂载前的写
 *      权威通道）。PRJ-TX-9 的"全写入口"（archive.begin/submit/draft.
 *      save）随 T10/T12/T14 经同一门卫挂载，全量用例归 PRJ-T15 复验
 *      （§12 PRJ-T15 行）——本组以"门卫＋通道"承载 T08 阶段的迟到写
 *      拒绝证据。
 *   2. 提交计划的分支/父修订自磁盘 HEAD 读取（codec::parseHeadRecord
 *      ——地面事实，不依赖被测对象的内存状态）。
 *   3. "闭包外修订不可见"的观测面＝ProjectStoreImpl::revisionIndex()
 *      （私有头，同单元测试消费——R-2 禁令是跨单元暴露，同单元测试
 *      先例 TxEngineTest 同款）：装载纪律保证闭包外修订不注册进索引，
 *      T09 查询端口的 tryRevision 语义以此为结构前提。
 */

#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "Codec.hpp"
#include "ProjectStoreImpl.hpp"
#include "win32/StoreLock.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::ContentVersion;
using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::project::BranchId;
using sdurws::ird::project::BranchRecord;
using sdurws::ird::project::CommandRecord;
using sdurws::ird::project::DraftDocument;
using sdurws::ird::project::HeadRecord;
using sdurws::ird::project::IDiagnosticsSink;
using sdurws::ird::project::LockInfo;
using sdurws::ird::project::ObjectRefPair;
using sdurws::ird::project::OpenMode;
using sdurws::ird::project::OpenStoreRequest;
using sdurws::ird::project::ProjectMetadataRecord;
using sdurws::ird::project::ProjectStoreFactory;
using sdurws::ird::project::ProjectStoreImpl;
using sdurws::ird::project::ProjectStaticIdentity;
using sdurws::ird::project::RevisionManifest;
using sdurws::ird::project::StoreError;
using sdurws::ird::project::StoreErrorCode;
using sdurws::ird::project::codec::contentVersionOf;
using sdurws::ird::project::tx::CommitPlan;
using sdurws::ird::project::tx::CommitResult;
using sdurws::ird::project::tx::PlannedObject;
using sdurws::ird::project::revindex::TipUpdate;
namespace pd = sdurws::ird::project;  // 单元命名空间缩写（类型少的场景）

namespace {

// ---------------------------------------------------------------------
// fake：诊断 sink（用户级码与开发级消息捕获——P-PR-6 断言面）
// ---------------------------------------------------------------------

/**
 * @brief 捕获型 sink（TxEngineTest CapturingSink 同款形态——用户级
 *        core::DiagnosticRecord 与开发级消息分列捕获）。
 */
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

    /// 码值逐字匹配（P-PR-6：码值＝diagnostics.md §4.6 收编清单）。
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

// ---------------------------------------------------------------------
// 磁盘与断言辅助
// ---------------------------------------------------------------------

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

/// 裸写前置文件（前置数据不经被测代码——保证前置不依赖被测正确性）。
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

/// 从磁盘 HEAD 读取提交指针（地面事实——提交计划的分支/父修订来源，
/// 不依赖被测对象内存状态；口径登记 2）。
HeadRecord readHeadFromDisk(const fs::path& projectDir)
{
    return sdurws::ird::project::codec::parseHeadRecord(
        readAll(projectDir / "HEAD"));
}

/// 断言动作抛出指定稳定码的 StoreError 并返回异常（TxEngineTest 同款）。
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

/// 关闭一次性回调观察者（生命周期用例的回调计数器）。
class CountingCloseObserver : public sdurws::ird::project::ICloseObserver {
public:
    int calls = 0;
    void onStoreClosed(sdurws::ird::project::ProjectStore& /*store*/) override
    {
        ++calls;
    }
};

}  // namespace

// ---------------------------------------------------------------------
// 用例组：ProjectStore（套件名登记入 ird-test-report.json）
// ---------------------------------------------------------------------

/**
 * 测试夹具：套件级临时总根；各用例独立项目目录（进程级隔离）。种子
 * 项目一律经 createNew 产出（PM-01 组装路径本身即被测面之一），需要
 * 特殊残留态的用例在 close 后对磁盘做定向改造。
 */
class ProjectStoreTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_prj_store_test"
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
        // 总根清理：失败保留现场（"TempDir 失败保留"惯例），不静默掩盖。
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
        m_dir = baseDir() / ("case" + std::to_string(++s_caseCounter))
                / "proj.rwdesign";
        ASSERT_FALSE(ec);
    }

    /// 新建项目并返回打开结果（P-PR-6 面：注入捕获 sink；退化语义由
    /// Ppr6 用例单独覆盖）。
    pd::OpenStoreResult createNew(std::string_view name = "Seed Project")
    {
        return ProjectStoreFactory::createNew(m_dir, name, nullptr,
                                              m_sink.get());
    }

    /// 经 store 的内部写通道提交一次合法修订（分支/父修订自磁盘 HEAD
    /// 读取——地面事实；payloadBytes 字节域负载）。
    CommitResult commitOne(pd::ProjectStore& store,
                           std::size_t payloadBytes = 32)
    {
        const HeadRecord head = readHeadFromDisk(m_dir);
        CommitPlan plan;
        plan.revisionId = RevisionId::generate();
        plan.branchId = head.branchId;      // 活动分支＝HEAD 所在分支
        plan.parentRevisionId = head.revisionId;  // 分支 tip＝HEAD 修订
        plan.committedAtUtc = "2026-09-16T02:00:00Z";
        PlannedObject obj;
        obj.oid = ObjectId::generate();
        obj.objectTypeToken = "RobotDesign";
        obj.payload.assign(payloadBytes, 'D');
        plan.newObjects.push_back(std::move(obj));
        TipUpdate tip;
        tip.branchId = head.branchId;
        tip.newTip = plan.revisionId;  // 每次提交 tip 恰推进（§4.5）
        plan.metadataDelta.tipUpdate = tip;
        plan.command.commandType = "add-mass-point";  // ^[a-z0-9-]{3,64}
        plan.command.payloadFormatVersion = 1;
        plan.command.payloadCanonical = "{\"point\":1}";
        plan.command.summary = "store test commit";
        auto* impl = dynamic_cast<ProjectStoreImpl*>(&store);
        EXPECT_NE(impl, nullptr);
        return impl->executeCommit(plan);
    }

    static fs::path baseDir() { return s_base; }

    fs::path m_dir;  ///< 用例专属项目目录
    std::shared_ptr<CapturingSink> m_sink{std::make_shared<CapturingSink>()};

    static fs::path s_base;
    static int s_caseCounter;
};

fs::path ProjectStoreTest::s_base;
int ProjectStoreTest::s_caseCounter = 0;

// =====================================================================
// acceptance 3（生命周期）：createNew 骨架＋重开；关闭排空
// =====================================================================

/**
 * 锚定：PM-01 存储侧／§5.1 createNew 契约／acceptance 3。
 *
 * 前置：无（目标目录不存在）。操作：createNew→检查身份面与磁盘骨架→
 * requestClose→重开。
 * 预期：可写上下文（isSelf/writable）；projectId/schema/canonicalPath
 * 一致；磁盘五要素（project.json/HEAD/revisions/objects/lock）就位；
 * 关闭同步完成（pending==0）且回调一次性；重开 projectId 不变。
 */
TEST_F(ProjectStoreTest, CreateNew_SkeletonIdentityAndReopen)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    EXPECT_TRUE(opened.writable);
    EXPECT_TRUE(opened.lockInfo.isSelf);
    EXPECT_TRUE(opened.recovery.headIntegrityVerified);
    EXPECT_TRUE(opened.recovery.ignoredStagingTxs.empty());
    EXPECT_TRUE(opened.recovery.orphanDraftFiles.empty());
    EXPECT_EQ(opened.recovery.danglingObjectCount, 0u);

    // 身份面（§5.1）：projectId 规范文本／schema＝当前支持版本。
    EXPECT_TRUE(opened.store->projectId().toCanonical().rfind("prj-", 0) == 0);
    EXPECT_EQ(opened.store->schema().schemaVersion,
              sdurws::ird::project::kSchemaVersionCurrent);
    EXPECT_EQ(opened.store->schema().formatId,
              std::string{sdurws::ird::project::kFormatId});
    EXPECT_FALSE(opened.store->canonicalPath().empty());

    // 磁盘骨架（§5.1 表：project.json/lock/初始修订 r0＋M0＋HEAD）。
    const HeadRecord head = readHeadFromDisk(m_dir);
    EXPECT_EQ(head.revisionSeq, 1u);
    EXPECT_TRUE(fs::exists(m_dir / "project.json"));
    EXPECT_TRUE(fs::exists(m_dir / "revisions" / head.revisionId.toCanonical()
                           / "manifest.json"));
    EXPECT_TRUE(fs::exists(m_dir / "revisions" / head.revisionId.toCanonical()
                           / "command.json"));
    EXPECT_TRUE(fs::exists(m_dir / "lock"));

    // 关闭协议（pending==0 同步完成）＋一次性回调。
    CountingCloseObserver observer;
    opened.store->subscribeClose(observer);
    EXPECT_EQ(opened.store->requestClose(), 0u);
    EXPECT_TRUE(opened.store->closed());
    EXPECT_FALSE(opened.store->writable());
    EXPECT_EQ(observer.calls, 1);
    // 幂等：二次 requestClose 返回 0 且不再回调。
    EXPECT_EQ(opened.store->requestClose(), 0u);
    EXPECT_EQ(observer.calls, 1);
}

/**
 * 锚定：PM-02 打开协议／§5.1 open 契约（重开路径——CreateNew 用例的
 * 补充：目标已存在项目经 open 打开）。
 *
 * 前置：createNew 后关闭。操作：open 同一路径。
 * 预期：打开成功、身份一致、恢复报告为"正常打开"形态。
 */
TEST_F(ProjectStoreTest, Open_ReopenClosedProject_IdentityPreserved)
{
    ProjectId expectedId;
    {
        pd::OpenStoreResult opened = createNew();
        ASSERT_NE(opened.store, nullptr);
        expectedId = opened.store->projectId();
        EXPECT_EQ(opened.store->requestClose(), 0u);
    }
    OpenStoreRequest request;
    request.path = m_dir;
    pd::OpenStoreResult reopened = ProjectStoreFactory::open(request);
    ASSERT_NE(reopened.store, nullptr);
    EXPECT_TRUE(reopened.writable);
    EXPECT_TRUE(reopened.store->projectId() == expectedId);
    EXPECT_TRUE(reopened.recovery.headIntegrityVerified);
    EXPECT_TRUE(reopened.recovery.ignoredStagingTxs.empty());
    EXPECT_EQ(reopened.recovery.danglingObjectCount, 0u);
    // 关闭交付（锁释放——注册表注销路径的重复验证）。
    EXPECT_EQ(reopened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 1（PRJ-TX-9 落位面）：Closed/Draining 迟到写拒绝
// =====================================================================

/**
 * 锚定：PRJ-TX-9／NFR-REL-01／PM-03／§5.1 生命周期图（"迟到写请求=
 * 拒绝+诊断"）／acceptance 1。
 *
 * 前置：createNew 后 requestClose（同步 Closed）。操作：经写通道提交；
 * HEAD 字节前后比对。
 * 预期：StoreError(ContextClosed)＋用户级诊断 PRJ-WRITE-AUTHORITY-LOST
 * （diagnostics.md 映射行"context-closed → PRJ-WRITE-AUTHORITY-LOST"）；
 * 磁盘 HEAD 字节不变（未产生修订）。
 */
TEST_F(ProjectStoreTest, Tx9_ClosedLateWrite_RejectedWithStableDiag)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    // 基准：关闭前提交一次（健康路径自证——写通道本身可用）。
    const CommitResult committed = commitOne(*opened.store);
    EXPECT_EQ(committed.revisionSeq, 2u);
    EXPECT_EQ(opened.store->requestClose(), 0u);
    EXPECT_TRUE(opened.store->closed());

    const std::string headBefore = readAll(m_dir / "HEAD");
    const StoreError err = expectStoreError(
        StoreErrorCode::ContextClosed, [&] { (void)commitOne(*opened.store); });
    EXPECT_NE(err.what(), nullptr);
    // 稳定诊断（PRJ-TX-9 观测点：StoreError code 集合＋诊断）。
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-WRITE-AUTHORITY-LOST"));
    // 磁盘不变：迟到写未产生任何修订/对象（NFR-REL-01 观测面）。
    EXPECT_EQ(readAll(m_dir / "HEAD"), headBefore);
}

/**
 * 锚定：PM-03 存储侧（"等待在途归档/草稿完成"）／§9.6①／§9.7／
 * acceptance 1+3。
 *
 * 前置：createNew＋一张在途票据。操作：requestClose→Draining 断言→
 * Draining 态写→票据释放→排空完成回调。
 * 预期：requestClose 返回在途数 1、closed()==false；Draining 态写拒绝
 * （ContextClosed）；票据析构后排空完成（closed()==true＋回调恰一次）。
 */
TEST_F(ProjectStoreTest, Tx9_DrainingRejectsWrite_ThenDrainsInFlight)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    auto* impl = dynamic_cast<ProjectStoreImpl*>(opened.store.get());
    ASSERT_NE(impl, nullptr);

    // 在途票据（非 const——用例需显式 reset 触发排空完成）。
    std::shared_ptr<void> ticket = impl->acquireInFlight();
    EXPECT_TRUE(ticket != nullptr);
    CountingCloseObserver observer;
    opened.store->subscribeClose(observer);

    // 进入 Draining：新写拒绝、上下文未 Closed（§5.1 表 requestClose 行）。
    EXPECT_EQ(opened.store->requestClose(), 1u);
    EXPECT_FALSE(opened.store->closed());
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)commitOne(*opened.store); });
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-WRITE-AUTHORITY-LOST"));

    // 在途归零：排空完成（§9.7——"在途归档/草稿完成后释放"的存储侧
    // 承接）；回调一次性。
    ticket.reset();
    EXPECT_TRUE(opened.store->closed());
    EXPECT_EQ(observer.calls, 1);
}

// =====================================================================
// acceptance 1：闭包外修订不可见（PRJ-TX-9 第二半）
// =====================================================================

/**
 * 锚定：§7.3（已发布但未提交＝闭包外）／PRJ-TX-9（"F5 后重开浏览'已
 * 发布未提交'修订→查询返回空"）／PM-08／acceptance 1。
 *
 * 前置：createNew 后 close；对磁盘植入闭包外修订目录（合法格式清单，
 * 引用悬挂对象——发布中途失败形态）与一个悬挂对象文件。
 * 操作：重开（恢复扫描）→索引可见性断言。
 * 预期：打开成功（残留忽略＋诊断，PM-08）；闭包外修订不在会话索引
 * （tryRevision 空口径的结构前提）；danglingObjectCount>0；
 * headIntegrityVerified==true。
 */
TEST_F(ProjectStoreTest, Tx9_UncommittedRevision_InvisibleAfterReopen)
{
    RevisionId uncommittedRev;
    {
        pd::OpenStoreResult opened = createNew();
        ASSERT_NE(opened.store, nullptr);
        (void)commitOne(*opened.store);
        EXPECT_EQ(opened.store->requestClose(), 0u);
    }

    // 植入闭包外修订（F5 形态：内容已发布、HEAD 未指——合法格式清单，
    // 其元数据引用指向悬挂对象：闭包外内容完整性不被校验（§7.3 残留
    // 语义），对象字节悬空只进计数）。
    uncommittedRev = RevisionId::generate();
    const ObjectId danglingOid = ObjectId::generate();
    const std::string danglingBytes = "dangling payload";
    const std::string danglingCv = contentVersionOf(danglingBytes)
                                       .toCanonical();
    RevisionManifest leftover;
    leftover.revisionId = uncommittedRev;
    leftover.revisionSeq = 99;
    leftover.branchId = readHeadFromDisk(m_dir).branchId;
    leftover.committedAtUtc = "2026-09-16T03:00:00Z";
    leftover.metadataRef = ObjectRefPair{danglingOid,
                                         contentVersionOf(danglingBytes)};
    sdurws::ird::project::ObjectRef ref;
    ref.objectId = danglingOid;
    ref.contentVersion = leftover.metadataRef.contentVersion;
    ref.objectTypeToken = "ProjectMetadata";
    ref.digest256 = danglingCv.substr(3);
    leftover.objectRefs.push_back(ref);
    ASSERT_TRUE(writeRaw(m_dir / "revisions" / uncommittedRev.toCanonical()
                             / "manifest.json",
                         sdurws::ird::project::codec::dump(leftover)));
    // 悬挂对象本体（磁盘→引用方向——⑤计数源）。
    ASSERT_TRUE(writeRaw(m_dir / "objects" / danglingOid.toCanonical()
                             / danglingCv.substr(3),
                         danglingBytes));

    OpenStoreRequest request;
    request.path = m_dir;
    request.diagnostics = m_sink.get();
    pd::OpenStoreResult reopened = ProjectStoreFactory::open(request);
    ASSERT_NE(reopened.store, nullptr);
    EXPECT_TRUE(reopened.recovery.headIntegrityVerified);
    EXPECT_GT(reopened.recovery.danglingObjectCount, 0u);

    // 闭包外修订对会话索引不可见（装载纪律——T09 tryRevision 空口径
    // 的结构前提；口径登记 3 的观测面）。
    auto* impl = dynamic_cast<ProjectStoreImpl*>(reopened.store.get());
    ASSERT_NE(impl, nullptr);
    EXPECT_FALSE(impl->revisionIndex().tryRevision(uncommittedRev)
                     .has_value());
    // 闭包内修订仍然可见（对照腿——排除"全空"的假阳性）。
    EXPECT_TRUE(impl->revisionIndex()
                    .tryRevision(readHeadFromDisk(m_dir).revisionId)
                    .has_value());
    EXPECT_EQ(reopened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 1（完成条件具名用例）：激活前失败不影响当前项目
// =====================================================================

/**
 * 锚定：PM-02／§8.7（"激活前失败不影响当前项目"——PRJ-T08 完成条件）
 * ／acceptance 1。
 *
 * 前置：项目 A 可写打开并已提交；项目 B 为旧格式目录（②步拒绝）。
 * 操作：open B 失败后，对 A 继续提交并核对 HEAD。
 * 预期：open B 抛 FormatLegacy；A 仍 Active（可提交、writable、磁盘
 * HEAD 按序前进）——open 从不写当前项目。
 */
TEST_F(ProjectStoreTest, Tx9_OpenFailure_DoesNotTouchCurrentProject)
{
    pd::OpenStoreResult projectA = createNew("Project A");
    ASSERT_NE(projectA.store, nullptr);
    (void)commitOne(*projectA.store);  // r1

    // 构造旧格式项目 B（②步 formatId 不符——PM-06 稳定拒绝）。手写
    // canonical JSON（解析合法但 formatId 非 rwdesign——FormatLegacy 判定
    // 在版本检查处触发；projectId 用合法形态 prj-<32hex> 避免身份字段
    // 校验抢先拒绝）。
    const fs::path legacyDir
        = baseDir() / ("legacy" + std::to_string(s_caseCounter));
    fs::create_directories(legacyDir);
    const std::string legacyJson
        = "{\"formatId\":\"rwproj-2019\",\"schemaVersion\":9000,"
          "\"projectId\":\"prj-00000000000000000000000000000000\","
          "\"createdAtUtc\":\"2020-01-01T00:00:00.000Z\","
          "\"createdWithToolVersion\":\"legacy\"}";
    ASSERT_TRUE(writeRaw(legacyDir / "project.json", legacyJson));

    OpenStoreRequest badRequest;
    badRequest.path = legacyDir;
    expectStoreError(StoreErrorCode::FormatLegacy,
                     [&] { (void)ProjectStoreFactory::open(badRequest); });

    // 当前项目 A 不受影响：仍可写、可提交、HEAD 前进到 r2（seq=3）。
    EXPECT_TRUE(projectA.store->writable());
    const CommitResult r2 = commitOne(*projectA.store);
    EXPECT_EQ(r2.revisionSeq, 3u);
    const HeadRecord diskHead = readHeadFromDisk(m_dir);
    EXPECT_EQ(diskHead.revisionSeq, 3u);
    EXPECT_TRUE(diskHead.revisionId == r2.revisionId);
}

// =====================================================================
// acceptance 2：旧格式/未来版本稳定拒绝（PM-06）
// =====================================================================

/**
 * 锚定：§8.11 行 1／PM-06／NFR-DEP-04／acceptance 2。
 *
 * 前置：健康项目。操作：close 后改写 project.json 的 formatId 为旧格式
 * token→open。
 * 预期：StoreError(FormatLegacy)＋PRJ-FORMAT-LEGACY 诊断；原文件字节
 * 不动（"稳定只读拒绝"）。
 */
TEST_F(ProjectStoreTest, LegacyFormat_StableReadOnlyRejection)
{
    {
        pd::OpenStoreResult opened = createNew();
        ASSERT_NE(opened.store, nullptr);
        EXPECT_EQ(opened.store->requestClose(), 0u);
    }
    const fs::path pj = m_dir / "project.json";
    const std::string original = readAll(pj);
    auto identity = sdurws::ird::project::codec::parseStaticIdentity(original);
    identity.formatId = "rwproj-2019";  // 旧格式 token（§8.11 行 1）
    ASSERT_TRUE(writeRaw(pj, sdurws::ird::project::codec::dump(identity)));

    OpenStoreRequest request;
    request.path = m_dir;
    request.diagnostics = m_sink.get();
    const StoreError err = expectStoreError(
        StoreErrorCode::FormatLegacy,
        [&] { (void)ProjectStoreFactory::open(request); });
    // 稳定拒绝的三个观测点：码＋诊断＋原文件不动。
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-FORMAT-LEGACY"));
    EXPECT_NE(err.what(), nullptr);
    EXPECT_EQ(readAll(pj),
              sdurws::ird::project::codec::dump(identity));  // 原文件未动
}

/**
 * 锚定：§8.11 行 2／PM-06（升级指引数据）／acceptance 2。
 *
 * 前置：健康项目。操作：close 后改写 project.json 的 schemaVersion 为
 * 未来主版本（2.0＝20000）→open。
 * 预期：StoreError(SchemaFuture)；detail 携带升级指引三键（document/
 * supported/upgrade）；诊断 PRJ-SCHEMA-FUTURE。
 */
TEST_F(ProjectStoreTest, SchemaFuture_RejectedWithUpgradeGuidance)
{
    {
        pd::OpenStoreResult opened = createNew();
        ASSERT_NE(opened.store, nullptr);
        EXPECT_EQ(opened.store->requestClose(), 0u);
    }
    const fs::path pj = m_dir / "project.json";
    auto identity
        = sdurws::ird::project::codec::parseStaticIdentity(readAll(pj));
    identity.schemaVersion = 2 * sdurws::ird::project::kSchemaVersionScale;
    ASSERT_TRUE(writeRaw(pj, sdurws::ird::project::codec::dump(identity)));

    OpenStoreRequest request;
    request.path = m_dir;
    request.diagnostics = m_sink.get();
    const StoreError err = expectStoreError(
        StoreErrorCode::SchemaFuture,
        [&] { (void)ProjectStoreFactory::open(request); });
    // 升级指引数据（PM-06：显示当前支持版本/项目版本/升级工具入口）。
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-SCHEMA-FUTURE"));
    EXPECT_NE(std::string(err.what()).find("document="), std::string::npos)
        << "detail 缺 document 键: " << err.what();
    EXPECT_NE(std::string(err.what()).find("supported="), std::string::npos)
        << "detail 缺 supported 键: " << err.what();
    EXPECT_NE(std::string(err.what()).find("upgrade="), std::string::npos)
        << "detail 缺 upgrade 键: " << err.what();
}

// =====================================================================
// acceptance 2：恢复报告断言（PM-08／§7.4 恢复顺序）
// =====================================================================

/**
 * 锚定：PM-08／§7.4①③（.staging 残留／孤儿草稿）／§8.4／acceptance 2。
 *
 * 前置：健康项目＋植入残留：.staging/tx-9-abc/ 目录、孤儿草稿（分支
 * 不存在）、.bak 残留。操作：close→重开→RecoveryReport 逐字段断言。
 * 预期：ignoredStagingTxs 含 tx-9-abc；orphanDraftFiles 含两现场；
 * 诊断码 PRJ-RECOVERY-IGNORED-UNCOMMITTED 与 PRJ-RECOVERY-ORPHAN-DRAFT
 * 逐字出现（P-PR-6：码值＝收编清单）；headIntegrityVerified==true。
 */
TEST_F(ProjectStoreTest, RecoveryReport_StagingResidueAndOrphanDrafts)
{
    {
        pd::OpenStoreResult opened = createNew();
        ASSERT_NE(opened.store, nullptr);
        (void)commitOne(*opened.store);
        EXPECT_EQ(opened.store->requestClose(), 0u);
    }

    // 残留①：未提交事务目录（现场保留——扫描忽略不删）。
    ASSERT_TRUE(writeRaw(m_dir / ".staging" / "tx-9-abc" / "marker.bin",
                         "leftover"));
    // 残留③：孤儿草稿（新生成的分支 id 不在权威分支表——归属校验失败）
    // ＋ .bak 崩溃现场（真实分支目录下——.bak 后缀无条件计孤儿）。
    const HeadRecord seedHead = readHeadFromDisk(m_dir);
    const std::string realBranchText = seedHead.branchId.toCanonical();
    DraftDocument orphanDraft;
    orphanDraft.projectId = seedHead.projectId;
    orphanDraft.branchId = BranchId::generate();  // 新生成＝未注册分支
    orphanDraft.moduleId = "modeling";
    orphanDraft.baseRevisionId = seedHead.revisionId;
    orphanDraft.payload = "{\"draft\":1}";
    orphanDraft.savedAtUtc = "2026-09-16T04:00:00Z";
    const std::string draftDirName = orphanDraft.branchId.toCanonical();
    ASSERT_TRUE(writeRaw(m_dir / "drafts" / draftDirName / "modeling.draft.json",
                         sdurws::ird::project::codec::dump(orphanDraft)));
    ASSERT_TRUE(writeRaw(m_dir / "drafts" / realBranchText
                             / "modeling.draft.json.bak",
                         "{}"));  // .bak 残留

    OpenStoreRequest request;
    request.path = m_dir;
    request.diagnostics = m_sink.get();
    pd::OpenStoreResult reopened = ProjectStoreFactory::open(request);
    ASSERT_NE(reopened.store, nullptr);

    // 逐字段断言（确定性排序——NFR-COR-02：清单可逐元素比对）。
    EXPECT_TRUE(reopened.recovery.headIntegrityVerified);
    ASSERT_EQ(reopened.recovery.ignoredStagingTxs.size(), std::size_t{1});
    EXPECT_EQ(reopened.recovery.ignoredStagingTxs[0], "tx-9-abc");
    ASSERT_EQ(reopened.recovery.orphanDraftFiles.size(), std::size_t{2});
    // 两现场各一：.bak（真实分支目录）与孤儿草稿（未注册分支目录）。
    bool sawBak = false;
    bool sawOrphan = false;
    for (const std::string& entry : reopened.recovery.orphanDraftFiles) {
        sawBak = sawBak || entry.find(realBranchText) != std::string::npos;
        sawOrphan = sawOrphan || entry.find(draftDirName) != std::string::npos;
    }
    EXPECT_TRUE(sawBak);
    EXPECT_TRUE(sawOrphan);
    // 稳定诊断（P-PR-6：码值逐字＝diagnostics.md §4.6 收编清单）。
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-RECOVERY-IGNORED-UNCOMMITTED"));
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-RECOVERY-ORPHAN-DRAFT"));
    // 报告快照与 sink 上报同源（RecoveryReport.diagnostics 双通道）。
    bool seenIgnored = false;
    bool seenOrphan = false;
    for (const auto& rec : reopened.recovery.diagnostics) {
        seenIgnored
            = seenIgnored || rec.code == "PRJ-RECOVERY-IGNORED-UNCOMMITTED";
        seenOrphan = seenOrphan || rec.code == "PRJ-RECOVERY-ORPHAN-DRAFT";
    }
    EXPECT_TRUE(seenIgnored);
    EXPECT_TRUE(seenOrphan);
    EXPECT_EQ(reopened.store->requestClose(), 0u);
}

/**
 * 锚定：PM-08／§7.4④（HEAD 闭包完整性校验失败＝store-corrupt 定位到
 * 文件）／acceptance 2。
 *
 * 前置：健康项目；close 后篡改闭包内对象字节（r0 元数据对象）。操作：
 * 重开。预期：StoreError(StoreCorrupt)＋PRJ-STORE-CORRUPT（④失败——
 * 打开中止，激活前失败语义）。
 */
TEST_F(ProjectStoreTest, RecoveryReport_HeadIntegrityFailure_StopsOpen)
{
    {
        pd::OpenStoreResult opened = createNew();
        ASSERT_NE(opened.store, nullptr);
        EXPECT_EQ(opened.store->requestClose(), 0u);
    }
    // 定位闭包内元数据对象文件（r0 清单 metadataRef）。
    const HeadRecord head = readHeadFromDisk(m_dir);
    const RevisionManifest manifest
        = sdurws::ird::project::codec::parseRevisionManifest(readAll(
            m_dir / "revisions" / head.revisionId.toCanonical()
            / "manifest.json"));
    const fs::path metaFile
        = m_dir / "objects" / manifest.metadataRef.objectId.toCanonical()
          / manifest.metadataRef.contentVersion.toCanonical().substr(3);
    ASSERT_TRUE(fs::exists(metaFile));
    // 位翻转（首字节异或——SHA-256 必变，④校验必拒）。
    {
        std::fstream f(metaFile, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        char b = '\0';
        f.read(&b, 1);
        f.seekg(0);
        b = static_cast<char>(b ^ 0xFF);
        f.write(&b, 1);
    }

    OpenStoreRequest request;
    request.path = m_dir;
    request.diagnostics = m_sink.get();
    expectStoreError(StoreErrorCode::StoreCorrupt,
                     [&] { (void)ProjectStoreFactory::open(request); });
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-STORE-CORRUPT"));
}

// =====================================================================
// acceptance 3：只读打开（PM-07）与进程内互斥（§9.3）
// =====================================================================

/**
 * 锚定：PM-07／§9.2（第二实例只读）／SA-17／acceptance 3（"只读打开
 * 同样提供查询"）。
 *
 * 前置：实例 A 可写打开（持锁）。操作：显式 ReadOnly 打开 B→身份查询
 * 面→写拒绝。
 * 预期：B writable==false、lockInfo 携带 A 的 PID（＝本进程）、isSelf
 * ==false；B 的 projectId/schema/canonicalPath 查询可用（查询面）；写
 * 拒绝 LockHeldByOther＋PRJ-LOCK-HELD；A 的可写性不受 B 影响。
 */
TEST_F(ProjectStoreTest, ReadOnlyOpen_ProvidesQueriesAndRejectsWrites)
{
    pd::OpenStoreResult holder = createNew("Held Project");
    ASSERT_NE(holder.store, nullptr);
    const ProjectId id = holder.store->projectId();

    OpenStoreRequest request;
    request.path = m_dir;
    request.mode = OpenMode::ReadOnly;
    request.diagnostics = m_sink.get();
    pd::OpenStoreResult reader = ProjectStoreFactory::open(request);
    ASSERT_NE(reader.store, nullptr);
    EXPECT_FALSE(reader.writable);
    EXPECT_FALSE(reader.lockInfo.isSelf);
    EXPECT_EQ(reader.lockInfo.holder.pid, ::GetCurrentProcessId());
    // 查询面（PM-07"可查看禁编辑"的存储侧）。
    EXPECT_TRUE(reader.store->projectId() == id);
    EXPECT_EQ(reader.store->schema().schemaVersion,
              sdurws::ird::project::kSchemaVersionCurrent);
    EXPECT_EQ(reader.store->canonicalPath(), holder.store->canonicalPath());
    // 写拒绝（只读上下文——门卫锁半边）。
    expectStoreError(StoreErrorCode::LockHeldByOther,
                     [&] { (void)commitOne(*reader.store); });
    EXPECT_TRUE(m_sink->hasUserCode("PRJ-LOCK-HELD"));
    // 持有者不受只读实例影响。
    EXPECT_TRUE(holder.store->writable());
    // 显式只读上下文同样可关闭（生命周期完备性）。
    EXPECT_EQ(reader.store->requestClose(), 0u);
    EXPECT_TRUE(reader.store->closed());
    EXPECT_TRUE(holder.store->writable());  // 排空后 A 仍持锁
}

/**
 * 锚定：PM-07（Writable 请求失败降级 ReadOnly）／§5.1 open 表／§9.5 行 1／
 * acceptance 3。
 *
 * 前置：原语级 StoreLock 直接持有 lock 文件（模拟**进程外**第二实例——
 * 同进程内经工厂的重复打开走 §9.3 直接拒绝路径〔InProcess 用例〕，降级
 * 路径只在 OS 层锁竞争时触达，故前置用锁原语构造竞争面）。操作：以
 * Writable 模式打开同一项目。
 * 预期：不抛（降级语义）＋writable==false＋lockInfo 携带持有者（PM-07
 * 不阻塞等待）＋PRJ-LOCK-HELD 诊断。
 */
TEST_F(ProjectStoreTest, WritableOpen_DegradesToReadOnlyWhenHeld)
{
    // 用例不经 createNew（降级前置需要"既有项目＋外部持锁"——用 createNew
    // 建项目则锁已被工厂注册表登记），只建目录骨架。
    std::error_code ec;
    fs::create_directories(m_dir, ec);
    ASSERT_FALSE(ec);
    // 最小合法项目内容（project.json＋HEAD——②步版本检查需通过，降级
    // 路径在锁半步之后仍会走装载）。
    {
        const ProjectId pid = ProjectId::generate();
        const RevisionId r0 = RevisionId::generate();
        const BranchId bid = BranchId::generate();
        ProjectStaticIdentity identity;
        identity.projectId = pid;
        identity.createdAtUtc = "2026-09-16T05:00:00.000Z";
        identity.createdWithToolVersion = "test-fixture";
        ProjectMetadataRecord m0;
        m0.committedBy = r0;
        m0.projectDisplayName = "Degrade Fixture";
        m0.primaryBranchId = bid;
        BranchRecord br;
        br.branchId = bid;
        br.label = "main";
        br.baseRevisionId = r0;
        br.tipRevisionId = r0;
        br.createdAtUtc = "2026-09-16T05:00:00.000Z";
        m0.branches.push_back(br);
        // 元数据对象与 r0 清单（最小闭包——发布经裸写，校验由打开③执行）。
        const std::string metaPayload
            = sdurws::ird::project::codec::dump(m0);
        const ContentVersion metaCv = contentVersionOf(metaPayload);
        const std::string metaCvHex = metaCv.toCanonical().substr(3);
        const ObjectId metaOid = ObjectId::generate();
        RevisionManifest manifest;
        manifest.revisionId = r0;
        manifest.revisionSeq = 1;
        manifest.branchId = bid;
        manifest.committedAtUtc = "2026-09-16T05:00:00.000Z";
        manifest.metadataRef = ObjectRefPair{metaOid, metaCv};
        sdurws::ird::project::ObjectRef ref;
        ref.objectId = metaOid;
        ref.contentVersion = metaCv;
        ref.objectTypeToken = "ProjectMetadata";
        ref.digest256 = metaCvHex;
        manifest.objectRefs.push_back(ref);
        HeadRecord head;
        head.projectId = pid;
        head.revisionId = r0;
        head.revisionSeq = 1;
        head.branchId = bid;
        head.manifestDigest = metaCvHex;
        ASSERT_TRUE(writeRaw(m_dir / "objects" / metaOid.toCanonical()
                                 / metaCvHex,
                             metaPayload));
        ASSERT_TRUE(writeRaw(m_dir / "revisions" / r0.toCanonical()
                                 / "manifest.json",
                             sdurws::ird::project::codec::dump(manifest)));
        ASSERT_TRUE(writeRaw(m_dir / "project.json",
                             sdurws::ird::project::codec::dump(identity)));
        ASSERT_TRUE(writeRaw(m_dir / "HEAD",
                             sdurws::ird::project::codec::dump(head)));
    }
    // 进程外持锁的进程内模拟：原语级 StoreLock 直接持有（不经工厂——
    // 工厂注册表不感知，OS 锁照常互斥；心跳禁用以免测试进程残留线程；
    // Win32LockOps 无状态——局部实例即可）。
    {
        sdurws::ird::project::win32::Win32LockOps lockOps;
        sdurws::ird::project::win32::LockSelfRecord foreign;
        foreign.pid = ::GetCurrentProcessId() + 1;  // 模拟他方 PID
        foreign.host = "other-host";
        foreign.initialHeartbeatUtc
            = sdurws::ird::project::win32::utcNowIsoMilli();
        sdurws::ird::project::win32::StoreLock foreignLock(
            &lockOps, nullptr, (m_dir / "lock").wstring(), foreign,
            std::chrono::milliseconds{0});
        ASSERT_EQ(foreignLock.status(),
                  sdurws::ird::project::win32::AcquireStatus::Held);

        OpenStoreRequest request;
        request.path = m_dir;
        request.mode = OpenMode::Writable;  // 请求写权限——被持后降级
        request.diagnostics = m_sink.get();
        pd::OpenStoreResult degraded = ProjectStoreFactory::open(request);
        ASSERT_NE(degraded.store, nullptr);
        EXPECT_FALSE(degraded.writable);
        EXPECT_FALSE(degraded.lockInfo.isSelf);
        EXPECT_EQ(degraded.lockInfo.holder.pid,
                  ::GetCurrentProcessId() + 1);  // 持有者 PID 可读（§9.2②）
        EXPECT_TRUE(m_sink->hasUserCode("PRJ-LOCK-HELD"));
        // 降级上下文的完整生命周期（关闭不触碰他方锁）。
        EXPECT_EQ(degraded.store->requestClose(), 0u);
        EXPECT_TRUE(degraded.store->closed());
    }
    // 前置锁释放后：Writable 打开应取得写权限（降级恢复对照腿）。
    OpenStoreRequest retry;
    retry.path = m_dir;
    retry.mode = OpenMode::Writable;
    pd::OpenStoreResult acquired = ProjectStoreFactory::open(retry);
    ASSERT_NE(acquired.store, nullptr);
    EXPECT_TRUE(acquired.writable);
    EXPECT_EQ(acquired.store->requestClose(), 0u);
}

/**
 * 锚定：§9.3（进程内重复 writable 打开直接拒绝——防自我双写）／acceptance 3。
 *
 * 前置：实例 A 可写打开。操作：同进程再次以 Writable 打开同一路径。
 * 预期：StoreError(LockHeldByOther)——不经降级（自我双写是程序缺陷，
 * 降级只读会掩盖它）。
 */
TEST_F(ProjectStoreTest, InProcessSecondWritableOpen_Rejected)
{
    pd::OpenStoreResult holder = createNew("Held Project");
    ASSERT_NE(holder.store, nullptr);

    OpenStoreRequest request;
    request.path = m_dir;
    request.mode = OpenMode::Writable;
    expectStoreError(StoreErrorCode::LockHeldByOther,
                     [&] { (void)ProjectStoreFactory::open(request); });
    EXPECT_TRUE(holder.store->writable());
}

// =====================================================================
// acceptance 3＋1：写通道健康路径（提交经门卫推进 HEAD）
// =====================================================================

/**
 * 锚定：§5.1 存储上下文（写权限在上下文层裁决）／§7.1（提交推进）／
 * acceptance 1 的健康对照腿。
 *
 * 前置：createNew。操作：连续两次经写通道提交。预期：seq 单调（2、3）
 * ；磁盘 HEAD 与结果一致（地面事实）；可写性全程保持。
 */
TEST_F(ProjectStoreTest, WriteChannel_HeadAdvancesMonotonically)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const CommitResult r1 = commitOne(*opened.store);
    EXPECT_EQ(r1.revisionSeq, 2u);
    const CommitResult r2 = commitOne(*opened.store);
    EXPECT_EQ(r2.revisionSeq, 3u);
    const HeadRecord diskHead = readHeadFromDisk(m_dir);
    EXPECT_TRUE(diskHead.revisionId == r2.revisionId);
    EXPECT_TRUE(opened.store->writable());
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 4：P-PR-1 / P-PR-6 处置自证
// =====================================================================

/**
 * 锚定：P-PR-1（core 身份基线消费）／acceptance 4。
 *
 * 前置：createNew。操作：核对 projectId 规范文本形态（core Identity
 * 契约的 prj-<32hex>）＋与磁盘 project.json 的解析一致（core 规范文本
 * round-trip）＋提交产生的修订/对象身份同为 core 规范文本形态。
 * 预期：全部身份来自 core 公共契约（零本地第二格式化——CR 纪律）。
 */
TEST_F(ProjectStoreTest, Ppr1_CoreIdentityBaselineConsumed)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const std::string pidText = opened.store->projectId().toCanonical();
    // core 规范文本形态（Identity.hpp 契约：prj- 前缀＋32 hex）。
    EXPECT_EQ(pidText.size(), std::size_t{36});
    EXPECT_EQ(pidText.rfind("prj-", 0), 0u);
    // 与磁盘 project.json 一致（round-trip——core toCanonical/tryFromCanonical
    // 消费面）。
    auto identity = sdurws::ird::project::codec::parseStaticIdentity(
        readAll(m_dir / "project.json"));
    EXPECT_TRUE(identity.projectId == opened.store->projectId());
    // 提交后磁盘修订目录名＝core 规范文本（rev- 前缀形态同源）。
    const CommitResult r1 = commitOne(*opened.store);
    const std::string revText = r1.revisionId.toCanonical();
    EXPECT_EQ(revText.rfind("rev-", 0), 0u);
    EXPECT_TRUE(fs::exists(m_dir / "revisions" / revText / "manifest.json"));
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

/**
 * 锚定：P-PR-6（诊断经 §5.0 IDiagnosticsSink 注入；无 sink＝仅退化即时
 * 上报通道）／acceptance 4。
 *
 * 前置：带残留项目（.staging 残留）。操作：A 份注入 CapturingSink 打开
 * →码值断言；B 份不注入 sink（nullptr）打开。预期：A 的 sink 收到收编
 * 清单码值且报告快照非空（双通道同源）；B 的报告数据面照常完整——
 * RecoveryReport 是打开结果的组成部分（恢复诊断数据，呈现归 PM-15/ui），
 * 不依赖装配注入；sink 可空退化的只是"即时上报"通道（§5.1 OpenStoreRequest
 * 注释口径），不是报告数据本身。
 */
TEST_F(ProjectStoreTest, Ppr6_SinkInjectionOptionalWithSameCodes)
{
    {
        pd::OpenStoreResult opened = createNew();
        ASSERT_NE(opened.store, nullptr);
        (void)commitOne(*opened.store);
        EXPECT_EQ(opened.store->requestClose(), 0u);
    }
    ASSERT_TRUE(writeRaw(m_dir / ".staging" / "tx-ppr6" / "x.bin", "y"));

    // A 份：注入 sink——即时上报通道与报告快照双通道同源同序。
    auto sink = std::make_shared<CapturingSink>();
    OpenStoreRequest withSink;
    withSink.path = m_dir;
    withSink.diagnostics = sink.get();
    pd::OpenStoreResult a = ProjectStoreFactory::open(withSink);
    ASSERT_NE(a.store, nullptr);
    EXPECT_FALSE(sink->reports.empty());  // 注入面：sink 收到用户级诊断
    EXPECT_FALSE(a.recovery.diagnostics.empty());  // 报告快照同步
    EXPECT_EQ(a.recovery.ignoredStagingTxs.size(), std::size_t{1});
    EXPECT_TRUE(sink->hasUserCode("PRJ-RECOVERY-IGNORED-UNCOMMITTED"));
    EXPECT_EQ(a.store->requestClose(), 0u);

    // B 份：不注入 sink——报告数据面照常（恢复报告的呈现消费方不依赖
    // 装配注入），退化的只是即时上报通道。
    OpenStoreRequest withoutSink;
    withoutSink.path = m_dir;
    withoutSink.diagnostics = nullptr;
    pd::OpenStoreResult b = ProjectStoreFactory::open(withoutSink);
    ASSERT_NE(b.store, nullptr);
    EXPECT_EQ(b.recovery.ignoredStagingTxs.size(), std::size_t{1});
    EXPECT_FALSE(b.recovery.diagnostics.empty());  // 数据面照常
    bool seenIgnored = false;
    for (const auto& rec : b.recovery.diagnostics) {
        seenIgnored
            = seenIgnored || rec.code == "PRJ-RECOVERY-IGNORED-UNCOMMITTED";
    }
    EXPECT_TRUE(seenIgnored);
    EXPECT_EQ(b.store->requestClose(), 0u);
}

// =====================================================================
// 边界：调用方契约违约（fail-fast——错误二分的调用方侧）
// =====================================================================

/**
 * 锚定：§5.1 createNew 前置（"目标目录不存在或为空"）／错误二分
 * （调用方错误 fail-fast）。
 *
 * 前置：非空目录。操作：createNew 同路径。预期：std::invalid_argument
 * （向导应先行校验的场景——不产出稳定码）；目录内容不被破坏。
 */
TEST_F(ProjectStoreTest, CreateNew_NonEmptyTarget_FailsFast)
{
    const fs::path occupied = baseDir()
                              / ("occupied" + std::to_string(s_caseCounter));
    fs::create_directories(occupied);
    ASSERT_TRUE(writeRaw(occupied / "user-file.txt", "keep me"));

    EXPECT_THROW((void)ProjectStoreFactory::createNew(occupied, "X"),
                 std::invalid_argument);
    // 调用方错误路径零副作用：用户文件原样保留。
    EXPECT_EQ(readAll(occupied / "user-file.txt"), "keep me");

    // 缺陷登记观测：空 displayName 同为调用方违约。
    EXPECT_THROW((void)ProjectStoreFactory::createNew(
                     baseDir() / ("empty-name" + std::to_string(s_caseCounter)),
                     ""),
                 std::invalid_argument);
}

/**
 * 锚定：§8.7①（路径不存在＝not-a-project）／§4.1（project.json 缺失＝
 * 非项目目录）。
 *
 * 前置：不存在路径／空目录。操作：open。预期：StoreError(NotAProject)。
 */
TEST_F(ProjectStoreTest, Open_NotAProject_MissingPathOrManifest)
{
    OpenStoreRequest missing;
    missing.path = baseDir() / "does-not-exist.rwdesign";
    expectStoreError(StoreErrorCode::NotAProject,
                     [&] { (void)ProjectStoreFactory::open(missing); });

    // 空目录（无 project.json＝非项目目录——§4.1 行）。
    const fs::path emptyDir
        = baseDir() / ("empty" + std::to_string(s_caseCounter));
    fs::create_directories(emptyDir);
    OpenStoreRequest emptyReq;
    emptyReq.path = emptyDir;
    expectStoreError(StoreErrorCode::NotAProject,
                     [&] { (void)ProjectStoreFactory::open(emptyReq); });
}
