/**
 * @file ReviewRegressionTest.cpp
 * @brief PRJ-R1：保存事实、只读隔离、恢复闭包和归档幂等的评审回归。
 * 依据：CON-01/02/04、PM-04/07/08、NFR-REL-01及project单元卡§4/7/8/10。
 * 每例仅操作独立TempDir；损坏输入直接改隔离文件，断言同时观察API和磁盘。
 * 进程间锁/查询交错由ProcessLockContractTest承接；本文件不模拟OS锁。
 */
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/DraftService.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include "Codec.hpp"
#include "ObjectStore.hpp"
#include "ProjectStoreImpl.hpp"
#include "RevisionIndex.hpp"
#include "StoreClone.hpp"
#include "TxEngine.hpp"
#include "win32/AtomicFile.hpp"
#include "StubHandlers.hpp"
#include <gtest/gtest.h>
#include <windows.h>
#include <fstream>
#include <iterator>

namespace sdurws::ird::project {
namespace {
namespace fs = std::filesystem;

/// 隔离文件原样写入；夹具失败显性抛出，不以空输入制造假阳性。
void writeBytes(const fs::path& file, const std::string& bytes)
{
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) { throw std::runtime_error("PRJ-R1 fixture write failed"); }
}

/// 读取实际文件字节；只用于观测和构造最小反例。
std::string readBytes(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) { throw std::runtime_error("PRJ-R1 fixture read failed"); }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// 合法无业务负载命令；token/inverse可配置以验证装配错误的提交前拒绝。
class NoteHandler final : public ICommandHandler {
public:
    explicit NoteHandler(std::string token = "review-note", bool badInverse = false)
        : m_token(std::move(token)), m_badInverse(badInverse) {}
    std::string commandType() const override { return m_token; }
    std::uint32_t currentPayloadVersion() const override { return 1; }
    PrepareOutcome prepare(HandlerContext&, const CommandEnvelope&,
                           const RevisionView&, CommandPlan& plan,
                           std::vector<core::DiagnosticRecord>&) override
    {
        plan.summary = "PRJ-R1 note";
        if (m_badInverse) {
            plan.inverseCommandType = "Bad.Token";
            plan.inversePayloadCanonical = std::vector<std::uint8_t>{};
        }
        return PrepareOutcome::Planned;
    }
private:
    std::string m_token;
    bool m_badInverse;
};

/// 在真实提交后才启用故障；非owning注入对象活得比store久。
class ThrowBus final : public core::IDomainEventBus {
public:
    bool fail = false;
    void publish(const core::DomainEvent&) override
    { if (fail) { throw std::runtime_error("PRJ-R1 event failure"); } }
    std::unique_ptr<core::IEventSubscription> subscribe(core::IDomainEventSink&) override
    { return {}; }
};
class ThrowSink final : public IDiagnosticsSink {
public:
    bool fail = false;
    void report(const core::DiagnosticRecord&) override {}
    void reportDev(const std::string&, const std::string&) override
    { if (fail) { throw StoreError(StoreErrorCode::WriteRejected, "PRJ-R1 sink failure"); } }
};

/// 生命周期顺序：store在每个用例栈内先析构，最后释放隔离目录。
class ProjectReviewRegression : public ::testing::Test {
protected:
    testkit::TempDir tmp{"prj-r1"};
    OpenStoreResult fresh(const char* name = "project")
    { return ProjectStoreFactory::createNew(tmp.path() / name, "PRJ-R1"); }
    OpenStoreResult readOnly(const fs::path& path)
    {
        OpenStoreRequest req;
        req.path = path;
        req.mode = OpenMode::ReadOnly;
        return ProjectStoreFactory::open(req);
    }
    CommandResult submit(ProjectStore& store)
    {
        const auto head = store.query().head();
        CommandEnvelope e;
        e.branch = head.branch;
        e.expectedRevision = head.id;
        e.commandType = "review-note";
        e.payloadFormatVersion = 1;
        return store.commands().submit(e);
    }
    DraftDocument draft(ProjectStore& store)
    {
        const auto h = store.query().head();
        DraftDocument d;
        d.projectId = store.projectId();
        d.branchId = h.branch;
        d.baseRevisionId = h.id;
        d.moduleId = "review";
        d.savedAtUtc = "2026-10-28T00:00:00.000Z";
        d.payload = "unsaved engineering input";
        return d;
    }
};

/// CON-02/D-18：事件与诊断同时失败仍返回已提交，缓存及后续命令同步前进。
TEST_F(ProjectReviewRegression, PostCommitObserversCannotChangeCommitFact)
{
    ThrowBus bus;
    ThrowSink sink;
    auto opened = ProjectStoreFactory::createNew(tmp.path() / "project", "PRJ-R1", &bus, &sink);
    const auto historical = opened.store->query().head();
    opened.store->handlerRegistry().registerHandler(std::make_unique<NoteHandler>());
    bus.fail = sink.fail = true;
    const auto result = submit(*opened.store);
    bus.fail = sink.fail = false;
    ASSERT_TRUE(result.committed());
    const auto disk = codec::parseHeadRecord(readBytes(opened.store->canonicalPath() / "HEAD"));
    EXPECT_EQ(result.newRevision, disk.revisionId);
    EXPECT_EQ(opened.store->query().currentMetadata().record.committedBy, disk.revisionId);
    EXPECT_TRUE(submit(*opened.store).committed());
    // 闭包约束不能隐藏合法历史对象；使用旧修订精确引用读回原内容。
    EXPECT_FALSE(opened.store->query().object(historical.metadataRef.objectId,
                                             historical.metadataRef.contentVersion).empty());
}

/// §4.4.4：非法注册token须装配期fail-fast，HEAD字节不变。
TEST_F(ProjectReviewRegression, InvalidRegisteredTokenRejectedBeforeCommit)
{
    auto opened = fresh();
    const auto before = readBytes(opened.store->canonicalPath() / "HEAD");
    EXPECT_THROW(opened.store->handlerRegistry().registerHandler(
        std::make_unique<NoteHandler>("Bad.Token")), std::invalid_argument);
    EXPECT_EQ(before, readBytes(opened.store->canonicalPath() / "HEAD"));
}

/// §6.9：handler产出的非法逆命令也不能进入正式历史。
TEST_F(ProjectReviewRegression, InvalidInverseRejectedBeforeCommit)
{
    auto opened = fresh();
    opened.store->handlerRegistry().registerHandler(std::make_unique<NoteHandler>("review-note", true));
    const auto before = readBytes(opened.store->canonicalPath() / "HEAD");
    EXPECT_FALSE(submit(*opened.store).committed());
    EXPECT_EQ(before, readBytes(opened.store->canonicalPath() / "HEAD"));
}

/// PM-07：读取草稿不能清理writer已写完、尚未发布的.new。
TEST_F(ProjectReviewRegression, DraftReadPreservesInFlightNewFile)
{
    auto opened = fresh();
    const auto d = draft(*opened.store);
    const auto saved = opened.store->drafts().save(d);
    ASSERT_TRUE(saved.ok);
    auto reader = readOnly(opened.store->canonicalPath());
    const fs::path pending = saved.file.wstring() + L".new";
    writeBytes(pending, "writer pending bytes");
    std::vector<core::DiagnosticRecord> diagnostics;
    ASSERT_TRUE(reader.store->drafts().tryLoad(d.branchId, d.moduleId, diagnostics));
    ASSERT_TRUE(fs::exists(pending));
    EXPECT_EQ(readBytes(pending), "writer pending bytes");
}

/// PM-04/08：两个rename间中断，current缺失仍恢复bak且不改现场。
TEST_F(ProjectReviewRegression, MissingCurrentRecoversBackupWithoutWriting)
{
    auto opened = fresh();
    const auto d = draft(*opened.store);
    const auto saved = opened.store->drafts().save(d);
    ASSERT_TRUE(saved.ok);
    const fs::path backup = saved.file.wstring() + L".bak";
    fs::rename(saved.file, backup);
    const auto before = readBytes(backup);
    std::vector<core::DiagnosticRecord> diagnostics;
    const auto recovered = opened.store->drafts().tryLoad(d.branchId, d.moduleId, diagnostics);
    ASSERT_TRUE(recovered);
    EXPECT_EQ(recovered->payload, d.payload);
    EXPECT_FALSE(diagnostics.empty());
    EXPECT_FALSE(fs::exists(saved.file));
    EXPECT_EQ(before, readBytes(backup));
}

/// §4.6：目录中的哈希正确孤儿对象也不是已提交输入。
TEST_F(ProjectReviewRegression, QueryHidesUncommittedObject)
{
    auto opened = fresh();
    const auto oid = core::ObjectId::generate();
    const auto cv = codec::contentVersionOf("orphan");
    writeBytes(opened.store->canonicalPath() / "objects" / oid.toCanonical()
                   / cv.toCanonical().substr(3), "orphan");
    EXPECT_FALSE(opened.store->query().tryObject(oid, cv));
    EXPECT_THROW(opened.store->query().object(oid, cv), StoreError);
}

/// PM-08：正式修订不能缺失必需命令文件。
TEST_F(ProjectReviewRegression, RecoveryRejectsMissingCommittedCommand)
{
    auto opened = fresh();
    const auto dir = opened.store->canonicalPath();
    const auto h = opened.store->query().head();
    opened.store->requestClose();
    fs::remove(dir / "revisions" / h.id.toCanonical() / "command.json");
    EXPECT_THROW(readOnly(dir), StoreError);
}

/// §4.5：即使HEAD摘要与真实字节一致，parent自环仍必须拒绝。
TEST_F(ProjectReviewRegression, RecoveryRejectsParentCycleWithValidDigest)
{
    auto opened = fresh();
    const auto dir = opened.store->canonicalPath();
    auto h = codec::parseHeadRecord(readBytes(dir / "HEAD"));
    opened.store->requestClose();
    const auto file = dir / "revisions" / h.revisionId.toCanonical() / "manifest.json";
    auto manifest = codec::parseRevisionManifest(readBytes(file));
    manifest.parentRevisionId = manifest.revisionId;
    const auto bytes = codec::dump(manifest);
    writeBytes(file, bytes);
    h.manifestDigest = codec::contentVersionOf(bytes).toCanonical().substr(3);
    writeBytes(dir / "HEAD", codec::dump(h));
    EXPECT_THROW(readOnly(dir), StoreError);
}

/// §4.4.1：HEAD序号必须与其指向的manifest一致。
TEST_F(ProjectReviewRegression, RecoveryRejectsHeadSequenceMismatch)
{
    auto opened = fresh();
    const auto dir = opened.store->canonicalPath();
    auto h = codec::parseHeadRecord(readBytes(dir / "HEAD"));
    opened.store->requestClose();
    h.revisionSeq += 100;
    writeBytes(dir / "HEAD", codec::dump(h));
    EXPECT_THROW(readOnly(dir), StoreError);
}

/// §7：校验拒绝发生于首次登记后，也必须清除本次幻影，旧HEAD不变。
TEST_F(ProjectReviewRegression, FailedValidationRollsBackRegistration)
{
    auto opened = fresh();
    auto& host = dynamic_cast<ProjectStoreImpl&>(*opened.store);
    const auto h = opened.store->query().head();
    const auto before = readBytes(opened.store->canonicalPath() / "HEAD");
    tx::CommitPlan plan;
    plan.revisionId = core::RevisionId::generate();
    plan.branchId = h.branch;
    plan.committedAtUtc = "2026-10-28T00:00:00.000Z";
    plan.command.commandType = "review-note";
    plan.command.payloadFormatVersion = 1;
    plan.metadataDelta.tipUpdate = revindex::TipUpdate{h.branch, plan.revisionId};
    // 缺parent违反INV-M3；这不是正常业务请求，而是内部装配错误。
    EXPECT_THROW(host.executeCommit(plan), StoreError);
    EXPECT_FALSE(host.revisionIndex().tryRevision(plan.revisionId));
    EXPECT_EQ(before, readBytes(opened.store->canonicalPath() / "HEAD"));
}

/// PM-08/16：同一对象被多个历史修订引用时，去重扫描仍须发现真实损坏。
TEST_F(ProjectReviewRegression, RecoveryChecksSharedHistoricalObjectBytes)
{
    auto opened = fresh();
    opened.store->handlerRegistry().registerHandler(std::make_unique<stub::AppendObjectHandler>());
    opened.store->handlerRegistry().registerHandler(std::make_unique<NoteHandler>());
    CommandEnvelope e;
    e.branch = opened.store->query().head().branch;
    e.commandType = "test-append-object";
    e.payloadFormatVersion = 1;
    e.payloadCanonical = {'a', 'b', 'c'};
    ASSERT_TRUE(opened.store->commands().submit(e).committed());
    const auto dataRevision = opened.store->query().head();
    ASSERT_EQ(dataRevision.objectRefs.size(), 2u);
    const auto object = dataRevision.objectRefs.back();
    ASSERT_TRUE(submit(*opened.store).committed());
    ASSERT_TRUE(submit(*opened.store).committed());
    const auto dir = opened.store->canonicalPath();
    (void)opened.store->requestClose();
    const auto file = dir / "objects" / object.objectId.toCanonical()
        / object.contentVersion.toCanonical().substr(3);
    writeBytes(file, "xyz");
    EXPECT_THROW((void)readOnly(dir), StoreError);
    writeBytes(file, "abc");
    auto restored = readOnly(dir);
    EXPECT_EQ(restored.store->query().branchHistory(e.branch, 100).size(), 4u);
    EXPECT_EQ(restored.store->query().object(object.objectId, object.contentVersion), e.payloadCanonical);
}

/// SDK独立oracle：112/39/1295全部是磁盘容量失败，detail保留原始码。
TEST_F(ProjectReviewRegression, SdkDiskFullErrorsRemainDistinctFromWriteRejected)
{
    for (const auto code : {ERROR_DISK_FULL, ERROR_HANDLE_DISK_FULL, ERROR_DISK_QUOTA_EXCEEDED}) {
        const auto error = clone::osToStoreError(code, "review", "isolated");
        EXPECT_EQ(error.code(), StoreErrorCode::DiskFull) << code;
        EXPECT_NE(std::string(error.what()).find(std::to_string(code)), std::string::npos);
    }
    EXPECT_EQ(clone::osToStoreError(ERROR_ACCESS_DENIED, "review", "isolated").code(),
              StoreErrorCode::AccessDenied);
}

/// PM-01：普通目标路径无需为实现内部暂存名预留MAX_PATH预算。
TEST_F(ProjectReviewRegression, CreateNewPreservesCanonicalLongPath)
{
    fs::path parent = tmp.path();
    while (parent.wstring().size() < 170) { parent /= "abcdefghij"; }
    fs::create_directories(parent);
    auto opened = ProjectStoreFactory::createNew(parent / "project", "PRJ-R1 long path");
    ASSERT_TRUE(opened.recovery.headIntegrityVerified);
    EXPECT_TRUE(opened.store->query().head().id.isValid());
}

/// D-13/14：重试前改合法字段但保留摘要，不能获得幂等成功。
TEST_F(ProjectReviewRegression, IdempotentFinalizeRejectsTamperedManifest)
{
    auto opened = fresh();
    const auto h = opened.store->query().head();
    ArchiveRequest req;
    req.task.project = opened.store->projectId();
    req.task.branch = h.branch;
    req.task.revision = h.id;
    req.task.run = core::RunId::generate();
    req.task.attempt = core::AttemptId{1};
    req.runDir = opened.store->query().runDir(req.task.run);
    req.runKind = "review";
    req.evaluationKey = "review-key";
    const auto session = opened.store->archive().begin(req);
    ArchiveBatch batch;
    batch.items.push_back({"data.bin", {'a', 'b', 'c'}});
    ASSERT_TRUE(opened.store->archive().writeBatch(session, batch).ok);
    RunManifest manifest;
    manifest.taskIdentity = req.task;
    manifest.runKind = req.runKind;
    manifest.evaluationKey = req.evaluationKey;
    manifest.finalizedAtUtc = "2026-10-28T00:00:00.000Z";
    manifest.items.push_back({"data.bin", codec::contentVersionOf("abc").toCanonical().substr(3), 3});
    ASSERT_TRUE(opened.store->archive().finalize(session, manifest).ok);
    const auto original = readBytes(req.runDir / "manifest.json");
    auto text = original;
    const auto offset = text.find("\"sizeBytes\":3");
    ASSERT_NE(offset, std::string::npos);
    text.replace(offset, 13, "\"sizeBytes\":4");
    writeBytes(req.runDir / "manifest.json", text);
    const auto retry = opened.store->archive().begin(req);
    EXPECT_FALSE(opened.store->archive().finalize(retry, manifest).ok);
    EXPECT_EQ(readBytes(req.runDir / "manifest.json"), text);
    // 清单还原后仍须实算工件摘要，等长字节篡改不能借幂等通道穿透。
    writeBytes(req.runDir / "manifest.json", original);
    writeBytes(req.runDir / "data.bin", "xyz");
    EXPECT_FALSE(opened.store->archive().finalize(retry, manifest).ok);
    writeBytes(req.runDir / "data.bin", "abc");
    EXPECT_TRUE(opened.store->archive().finalize(retry, manifest).ok);
}

/// Windows大小写不敏感目录：工件不能预占清单或暂存保留名。
TEST_F(ProjectReviewRegression, ArchiveRejectsReservedNamesCaseInsensitively)
{
    auto opened = fresh();
    const auto h = opened.store->query().head();
    ArchiveRequest req;
    req.task = {opened.store->projectId(), h.branch, h.id, core::RunId::generate(), core::AttemptId{1}};
    req.runDir = opened.store->query().runDir(req.task.run);
    req.runKind = "review";
    req.evaluationKey = "review-key";
    const auto session = opened.store->archive().begin(req);
    for (const auto* path : {"MANIFEST.JSON", "Manifest.Json", "data.IRD-PART"}) {
        ArchiveBatch batch;
        batch.items.push_back({path, {'a', 'b', 'c'}});
        EXPECT_THROW(opened.store->archive().writeBatch(session, batch), std::invalid_argument);
    }
    EXPECT_FALSE(fs::exists(req.runDir / "manifest.json"));
    opened.store->archive().abandon(session, ArchiveEndReason::Failed);
}
}  // namespace
}  // namespace sdurws::ird::project
