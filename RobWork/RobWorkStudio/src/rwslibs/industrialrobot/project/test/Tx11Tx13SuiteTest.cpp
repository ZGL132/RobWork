/**
 * @file   Tx11Tx13SuiteTest.cpp
 * @brief  §11 表两行具名用例体：PRJ-TX-13（只读上下文遍历全部写 API
 *         ——一律拒绝＋稳定诊断，错误码集合＝设计清单）与 PRJ-TX-11
 *         （复制一致性——另存为换 projectId 全链引用走查＋包导入哈希
 *         与引用一致；PRJ-T18 实体落位后由阻塞登记转为真实执行）。
 *
 * 设计依据：
 *   - units/project.md §12 PRJ-T15 行（产物：PRJ-TX-1～14 用例体——
 *     其中 TX-13 的"全入口遍历"为本任务新增落位面；TX-11 原为前置未
 *     就绪的显式阻塞登记〔GTEST_SKIP〕，随 PRJ-T18 实体落位转真实
 *     执行——用例体语义按 §11 行原文）、§11 PRJ-TX-13 行（PM-07：只读
 *     打开→逐个调用→一律拒绝＋稳定诊断→错误码集合＝设计清单）、
 *     §11 PRJ-TX-11 行（PM-05/AT-20——另存为/包导入导出的引用一致性；
 *     观测点＝新旧 projectId 比对＋闭包校验通过）、§8.8～§8.10（另存
 *     为/选择性复制/包导入导出协议——PRJ-T18 落位的实体消费面）、
 *     §5.8（ISaveAsService/IPackageService 冻结签名）、§9.6（失权/只读
 *     门卫——全部写入口的统一拒绝语义）、§10.1（归档 begin 锁面校验）；
 *   - units/testkit.md §6.2（TempDir 消费——unit 目标的 testkit 接入
 *     面，任务契约 acceptance 2）、§7.3（IRD_TEST_INFO 追溯登记）；
 *   - 需求 PM-07（只读打开：可查看不可写）、PM-05（另存为/包存储侧）、
 *     AT-20、NFR-REL-01（写路径门卫）。
 *
 * 错误码清单口径（TX-13 预期列"错误码集合＝设计清单"）：只读打开的
 * 写拒绝统一走 LockHeldByOther 门卫（§5.1——写权限唯一依据 SA-17），
 * 稳定诊断＝PRJ-LOCK-HELD（diagnostics.md §4.6 收编码）。redo 的空
 * 会话栈前置违约（std::invalid_argument）是可用性前置面而非写门卫面
 * ——只读会话的 redo 栈恒空（D-11：仅内存、不持久化），其可观测行为
 * 即前置 fail-fast，如实断言（不私造稳定码——CR-08）。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/DraftService.hpp>
#include <sdurws/ird/project/PackageService.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/UndoRedo.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include "PackageStubs.hpp"
#include "StubHandlers.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// 测试侧命名空间别名（core 为 sdurws::ird 的嵌套命名空间——TX-11
// 转真实用例的断言代码保持与产品代码同形的限定名）。
namespace core = sdurws::ird::core;

using sdurws::ird::core::BranchId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::project::ArchiveRequest;
using sdurws::ird::project::CommandEnvelope;
using sdurws::ird::project::CommandResult;
using sdurws::ird::project::CommandStatus;
using sdurws::ird::project::DraftDocument;
using sdurws::ird::project::DraftOrigin;
using sdurws::ird::project::OpenMode;
using sdurws::ird::project::OpenStoreRequest;
using sdurws::ird::project::OpenStoreResult;
using sdurws::ird::project::ProjectStore;
using sdurws::ird::project::ProjectStoreFactory;
using sdurws::ird::project::SaveResult;
using sdurws::ird::project::StoreErrorCode;
using sdurws::ird::project::BranchTip;
using sdurws::ird::project::ObjectRef;
using sdurws::ird::project::PackageExportOptions;
using sdurws::ird::project::PackageExportResult;
using sdurws::ird::project::PackageImportResult;
using sdurws::ird::project::RevisionView;
using sdurws::ird::project::RunInfo;
using sdurws::ird::project::SaveAsOptions;
using sdurws::ird::project::SaveAsResult;
using sdurws::ird::project::stub::AppendObjectHandler;
using sdurws::ird::project::stub::AppendUndoHandler;
using sdurws::ird::project::stub::PackageFixture;
using sdurws::ird::project::stub::StubSink;
using sdurws::ird::testkit::TempDir;

namespace {

/// 构造合法草稿文档（DraftServiceTest.makeDraft 同型——身份取自查询面）。
DraftDocument makeDraft(ProjectStore& store, const std::string& moduleId,
                        const std::string& payload, RevisionId base)
{
    DraftDocument doc;
    doc.projectId = store.projectId();
    doc.branchId = store.query().currentMetadata().record.primaryBranchId;
    doc.moduleId = moduleId;
    doc.baseRevisionId = base;
    doc.payload = payload;
    doc.savedAtUtc = "2026-09-17T00:00:00Z";
    doc.origin = DraftOrigin::Manual;
    return doc;
}

/// 构造归档请求（ArchiveServiceTest 同型——五元组＋results 白名单位置）。
ArchiveRequest makeArchiveRequest(ProjectStore& store,
                                  const fs::path& storeRoot)
{
    ArchiveRequest request;
    request.task.project = store.projectId();
    request.task.branch
        = store.query().currentMetadata().record.primaryBranchId;
    request.task.revision = store.query().head().id;
    request.task.run = sdurws::ird::core::RunId::generate();
    request.task.attempt = sdurws::ird::core::AttemptId{1};
    request.runDir = storeRoot / "results" / request.task.run.toCanonical();
    request.runKind = "kinematics-eval";
    request.evaluationKey = "eval-key-1";
    return request;
}

}  // namespace

// =====================================================================
// 用例组：Tx13ReadOnlyAllWrites（PRJ-TX-13——只读全入口遍历）
// =====================================================================

/**
 * 锚定：PRJ-TX-13／PM-07／§9.6 门卫统一语义——§11 表"只读上下文遍历
 * 全部写 API→一律拒绝＋稳定诊断→错误码集合＝设计清单"。
 *
 * 前置：健康项目含一条可撤销历史（r1＝逆命令声明族提交）；随后以
 * ReadOnly 模式重开（PM-07——不取写锁）。
 * 操作：遍历阶段 A 全部写入口——commands().submit／undoRedo().undo/
 * redo／drafts().save/discard／archive().begin。
 * 预期：
 *   - submit/undo → Rejected(NotWritable)＋诊断码 PRJ-LOCK-HELD；
 *   - redo → 前置违约 std::invalid_argument（只读会话 redo 栈恒空
 *     ——D-11；前置 fail-fast 是该入口在只读上下文的唯一可达行为）；
 *   - save/discard → 返回值轨拒绝（LockHeldByOther）＋PRJ-LOCK-HELD
 *     诊断（ui 定时器安全的返回值轨——§5.4）；
 *   - archive().begin → 异常轨拒绝（LockHeldByOther）＋PRJ-LOCK-HELD
 *     （§5.6 锁面校验）；
 *   - 零磁盘副作用（修订/草稿/results 三域）＋读轨全量可用（status/
 *     query——PM-07"禁编辑"不禁读的对照面）。
 * 观测点：错误码集合（ird-test-report.json trace 登记）＋磁盘事实。
 */
TEST(Tx13ReadOnlyAllWrites, TraversesEveryWriteEntry_AllRejectedWithStableDiag)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"},
                  std::vector<std::string>{});

    // ---- 前置：可写会话建立可撤销历史（r1），随后释放锁 ----
    TempDir tmp("tx15-tx13");
    tmp.keepOnFailure(true);
    const fs::path dir = tmp.path() / "proj.rwdesign";
    {
        OpenStoreResult writable
            = ProjectStoreFactory::createNew(dir, "tx13 read-only project");
        ASSERT_NE(writable.store, nullptr);
        auto& impl = *dynamic_cast<
            sdurws::ird::project::ProjectStoreImpl*>(writable.store.get());
        auto undoHandler = std::make_unique<AppendUndoHandler>();
        auto appendHandler = std::make_unique<AppendObjectHandler>();
        appendHandler->declareInverse = true;
        impl.commandService().registry().registerHandler(
            std::move(undoHandler));
        impl.commandService().registry().registerHandler(
            std::move(appendHandler));

        CommandEnvelope envelope;
        envelope.branch
            = writable.store->query().currentMetadata().record.primaryBranchId;
        envelope.commandType = "test-append-object";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'r', '1'};
        const CommandResult committed
            = writable.store->commands().submit(envelope);
        ASSERT_TRUE(committed.status.committed())
            << "前置提交失败（预期 Committed，得到其他终态）";
        EXPECT_EQ(writable.store->requestClose(), 0u);
    }

    // ---- 只读重开（PM-07 ReadOnly——不取写锁；诊断 sink 捕获）----
    StubSink sink;
    OpenStoreRequest request;
    request.path = dir;
    request.mode = OpenMode::ReadOnly;
    request.diagnostics = &sink;
    OpenStoreResult readonly = ProjectStoreFactory::open(request);
    ASSERT_NE(readonly.store, nullptr);
    ASSERT_FALSE(readonly.writable) << "前置失败：只读请求取得了写权限";
    ProjectStore& store = *readonly.store;

    // 写入口拒绝需要先过 S1 unknown-command——处理器注册是装配动作
    // （进程内注册表，与打开模式无关——§6.5）。
    auto& impl = *dynamic_cast<
        sdurws::ird::project::ProjectStoreImpl*>(&store);
    auto undoHandler = std::make_unique<AppendUndoHandler>();
    auto appendHandler = std::make_unique<AppendObjectHandler>();
    impl.commandService().registry().registerHandler(std::move(undoHandler));
    impl.commandService().registry().registerHandler(std::move(appendHandler));

    const BranchId branch
        = store.query().currentMetadata().record.primaryBranchId;
    const RevisionId base = store.query().head().id;

    // ---- ① commands().submit：Rejected(NotWritable)＋PRJ-LOCK-HELD ----
    // （稳定码经 sink 上报——P-PR-6 链路；结果面 error 携带 LockHeldByOther
    // ——CommandSubmitS1.ReadOnlyContext 同表。）
    {
        CommandEnvelope envelope;
        envelope.branch = branch;
        envelope.commandType = "test-append-object";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'x'};
        const CommandResult result = store.commands().submit(envelope);
        EXPECT_TRUE(result.rejected());
        EXPECT_EQ(result.status.rejection,
                  CommandStatus::Rejection::NotWritable);
        ASSERT_TRUE(result.error.has_value());
        EXPECT_EQ(result.error->code(), StoreErrorCode::LockHeldByOther);
        EXPECT_TRUE(sink.hasUserCode("PRJ-LOCK-HELD"));
    }

    // ---- ② undoRedo().undo：读轨可用（canUndo 由磁盘推导）＋写轨拒绝 ----
    {
        EXPECT_TRUE(store.undoRedo().status(branch).canUndo)
            << "读轨面：可撤销性由 tip 逆命令推导（与打开模式无关）";
        const CommandResult result = store.undoRedo().undo(branch);
        EXPECT_TRUE(result.rejected());
        EXPECT_EQ(result.status.rejection,
                  CommandStatus::Rejection::NotWritable);
        ASSERT_TRUE(result.error.has_value());
        EXPECT_EQ(result.error->code(), StoreErrorCode::LockHeldByOther);
        EXPECT_TRUE(sink.hasUserCode("PRJ-LOCK-HELD"));
    }

    // ---- ③ undoRedo().redo：前置违约（只读会话 redo 栈恒空——D-11）----
    {
        EXPECT_FALSE(store.undoRedo().status(branch).canRedo);
        EXPECT_THROW(store.undoRedo().redo(branch), std::invalid_argument);
    }

    // ---- ④ drafts().save/discard：返回值轨拒绝＋PRJ-LOCK-HELD ----
    {
        const SaveResult saved = store.drafts().save(
            makeDraft(store, "modeling", "{\"v\":1}", base));
        EXPECT_FALSE(saved.ok);
        ASSERT_TRUE(saved.error.has_value());
        EXPECT_EQ(saved.error->code(), StoreErrorCode::LockHeldByOther);
        const auto discarded = store.drafts().discard(branch, "modeling");
        EXPECT_FALSE(discarded.ok);
        ASSERT_TRUE(discarded.error.has_value());
        EXPECT_EQ(discarded.error->code(), StoreErrorCode::LockHeldByOther);
        EXPECT_TRUE(sink.hasUserCode("PRJ-LOCK-HELD"))
            << "草稿写轨拒绝必须上报稳定诊断（§5.4 返回值轨＋诊断双通道）";
    }

    // ---- ⑤ archive().begin：异常轨拒绝（锁面校验——§5.6）----
    {
        const ArchiveRequest archiveRequest = makeArchiveRequest(store, dir);
        bool rejected = false;
        try {
            (void)store.archive().begin(archiveRequest);
        } catch (const sdurws::ird::project::StoreError& e) {
            rejected = true;
            EXPECT_EQ(e.code(), StoreErrorCode::LockHeldByOther);
        }
        EXPECT_TRUE(rejected) << "只读上下文 begin 必须拒绝（§5.6）";
        EXPECT_TRUE(sink.hasUserCode("PRJ-LOCK-HELD"));
    }

    // ---- 零磁盘副作用＋读轨对照面 ----
    EXPECT_TRUE(sink.reports.size() >= 3u)
        << "各写入口的稳定诊断均应上报（错误码集合＝设计清单的 sink 面）";
    EXPECT_EQ(store.query().branchTips().size(), std::size_t{1});
    EXPECT_EQ(store.query().head().id, base) << "只读会话不得前移 HEAD";
    EXPECT_FALSE(fs::exists(dir / "results")) << "归档入口零副作用";
    EXPECT_EQ(store.requestClose(), 0u);
}

// =====================================================================
// 用例组：Tx11CopyConsistency（PRJ-TX-11——复制一致性；PRJ-T18 转真实
// 用例——另存为/包导入导出实体已落位，本用例体按 §11 行原文执行）
// =====================================================================

/**
 * 锚定：PRJ-TX-11／PM-05／AT-20；units/project.md §11 PRJ-TX-11 行
 * 原文（"另存为（换 projectId）后全链引用走查；包导入后哈希与引用
 * 一致｜对象/修订/分支引用可解析；results 保持原五元组；报告-结果引
 * 用缺失时报告不复制｜新旧 projectId 比对；闭包校验通过"）＋§8.8/
 * §8.9/§8.10（另存为协议/选择性复制/包导入导出——实体随 PRJ-T18 落
 * 位，本用例由阻塞登记（GTEST_SKIP）转为真实执行）。
 *
 * 前置：健康源项目（修订闭包＋一个完整运行＋草稿＋报告树——其中
 * rep-missing-ref 引用不存在的运行）。
 * 操作：全勾选另存为 → 新项目全链引用走查；随后全勾选导出→导入 →
 * 导入项目哈希与引用走查。
 * 预期：
 *   - 另存为：newProjectId 有效且 ≠ 源（新旧 projectId 比对面）；分支/
 *     修订/对象引用全部可解析（branchTips 全等；闭包逐修订视图全等；
 *     对象字节逐字节相同）；results 的 RunManifest 五元组保持原
 *     projectId（历史运行事实不改写）；rep-missing-ref 不复制；
 *   - 包导入：projectId 保持源值（整体搬运不改写）；HEAD 字节与源
 *     全等（哈希面）；闭包引用可解析、对象字节逐字节相同；
 *   - 两路副本的打开协议（闭包校验）随进入完成——打开成功即通过面。
 * 观测点：新旧 projectId 比对＋引用走查＋磁盘字节（ird-test-report.
 * json trace 登记）。
 */
TEST(Tx11CopyConsistency, SaveAsAndPackageImport_ReferenceWalkConsistent)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20"});
    PackageFixture f("ird-prj-tx11-consistency");

    const core::ProjectId sourceId = f.store().projectId();
    const RevisionView sourceHead = f.store().query().head();
    const std::vector<BranchTip> sourceTips = f.store().query().branchTips();
    const std::vector<RunInfo> sourceRuns
        = f.store().query().listRuns(sourceHead.id);
    ASSERT_EQ(sourceRuns.size(), std::size_t{1});

    // ---- 半区 1：另存为（换 projectId）后全链引用走查。
    SaveAsOptions saveAsOptions;
    saveAsOptions.targetDir = f.dir.parent_path() / (f.dir.filename().string() + "-tx11-copy");
    saveAsOptions.copyResults = true;
    saveAsOptions.copyReports = true;
    saveAsOptions.copyDrafts = true;
    const SaveAsResult copied = f.saveAsService->saveAs(saveAsOptions);
    ASSERT_TRUE(copied.ok) << (copied.error ? copied.error->what() : "ok==false");
    ASSERT_NE(copied.store, nullptr);
    ProjectStore& copyStore = *copied.store;
    // 装配对齐（hasUnresolvedPayload 判据的会话面——两侧注册同形后
    // 视图全等断言才反映数据本身）。
    copyStore.handlerRegistry().registerHandler(
        std::make_unique<AppendObjectHandler>());

    // 新旧 projectId 比对（core 分配的新身份——必不相同且有效）。
    EXPECT_TRUE(copied.newProjectId.isValid());
    EXPECT_NE(copied.newProjectId, sourceId);
    EXPECT_EQ(copyStore.projectId(), copied.newProjectId);

    // 分支引用可解析：分支表与源全等（ID/label/base/tip 原样）。
    const std::vector<BranchTip> copyTips = copyStore.query().branchTips();
    ASSERT_EQ(copyTips.size(), sourceTips.size());
    for (std::size_t i = 0; i < sourceTips.size(); ++i) {
        EXPECT_TRUE(copyTips[i] == sourceTips[i]) << "分支条目 " << i << " 失真";
    }

    // 修订/对象引用可解析（闭包逐修订：视图全等＋对象字节逐字节相同
    // ——AT-20 引用一致半区；闭包校验随 PM-02 打开协议进入完成）。
    std::optional<core::RevisionId> cursor = sourceHead.id;
    while (cursor.has_value()) {
        const std::optional<RevisionView> sourceRev
            = f.store().query().tryRevision(*cursor);
        ASSERT_TRUE(sourceRev.has_value());
        const std::optional<RevisionView> copyRev
            = copyStore.query().tryRevision(*cursor);
        ASSERT_TRUE(copyRev.has_value())
            << "副本修订不可解析: " << cursor->toCanonical();
        EXPECT_TRUE(*copyRev == *sourceRev);
        for (const ObjectRef& ref : sourceRev->objectRefs) {
            EXPECT_TRUE(copyStore.query().object(ref.objectId, ref.contentVersion)
                        == f.store().query().object(ref.objectId, ref.contentVersion));
        }
        cursor = sourceRev->parent;
    }

    // results 保持原五元组（历史运行事实——taskIdentity.projectId 不
    // 改写；读取侧不校验 manifest 的 projectId 归属，§8.8）。
    const std::vector<RunInfo> copyRuns
        = copyStore.query().listRuns(copyStore.query().head().id);
    ASSERT_EQ(copyRuns.size(), std::size_t{1});
    EXPECT_TRUE(copyRuns[0] == sourceRuns[0]);
    EXPECT_EQ(copyRuns[0].task.project, sourceId);

    // 报告-结果引用缺失时报告不复制（§8.9——rep-missing-ref 引用的
    // run 不在勾选集）。
    ASSERT_EQ(copied.skippedReports.size(), std::size_t{1});
    EXPECT_EQ(copied.skippedReports[0], "rep-missing-ref");
    EXPECT_FALSE(fs::exists(saveAsOptions.targetDir / "reports" / "rep-missing-ref"));
    copyStore.requestClose();

    // ---- 半区 2：包导入后哈希与引用一致。
    const fs::path packFile = f.dir.parent_path() / "tx11.rwpack";
    PackageExportOptions exportOptions;
    exportOptions.targetFile = packFile;
    exportOptions.includeResults = true;
    exportOptions.includeReports = true;
    exportOptions.includeDrafts = true;
    const PackageExportResult exported
        = f.packageService->exportPackage(exportOptions);
    ASSERT_TRUE(exported.ok) << (exported.error ? exported.error->what() : "ok==false");

    const fs::path importDir
        = f.dir.parent_path() / (f.dir.filename().string() + "-tx11-import");
    PackageImportResult imported = f.packageService->importPackage(packFile, importDir);
    ASSERT_TRUE(imported.ok) << (imported.error ? imported.error->what() : "ok==false");
    ASSERT_NE(imported.store, nullptr);
    ProjectStore& importStore = *imported.store;
    // 装配对齐（hasUnresolvedPayload 判据的会话面——同前：两侧注册
    // 同形后视图全等断言才反映数据本身）。
    importStore.handlerRegistry().registerHandler(
        std::make_unique<AppendObjectHandler>());

    // 导入不改写身份（项目整体搬运——与另存为换新相反）＋HEAD 哈希面
    // 逐字节一致（导入零改写的字节级口径）。
    EXPECT_EQ(imported.importedProjectId, sourceId);
    {
        std::ifstream sourceHeadFile(f.dir / "HEAD", std::ios::binary);
        std::ifstream importHeadFile(importDir / "HEAD", std::ios::binary);
        std::ostringstream sourceHeadBytes;
        std::ostringstream importHeadBytes;
        sourceHeadBytes << sourceHeadFile.rdbuf();
        importHeadBytes << importHeadFile.rdbuf();
        EXPECT_EQ(importHeadBytes.str(), sourceHeadBytes.str());
    }

    // 引用一致：闭包逐修订可解析＋对象字节逐字节相同。
    cursor = sourceHead.id;
    while (cursor.has_value()) {
        const std::optional<RevisionView> sourceRev
            = f.store().query().tryRevision(*cursor);
        ASSERT_TRUE(sourceRev.has_value());
        const std::optional<RevisionView> importRev
            = importStore.query().tryRevision(*cursor);
        ASSERT_TRUE(importRev.has_value())
            << "导入修订不可解析: " << cursor->toCanonical();
        EXPECT_TRUE(*importRev == *sourceRev);
        for (const ObjectRef& ref : sourceRev->objectRefs) {
            EXPECT_TRUE(importStore.query().object(ref.objectId, ref.contentVersion)
                        == f.store().query().object(ref.objectId, ref.contentVersion));
        }
        cursor = sourceRev->parent;
    }
    EXPECT_EQ(importStore.requestClose(), 0u);
}
