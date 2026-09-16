/**
 * @file   Tx11Tx13SuiteTest.cpp
 * @brief  PRJ-T15 补齐 §11 表两行具名用例体：PRJ-TX-13（只读上下文遍历
 *         全部写 API——一律拒绝＋稳定诊断，错误码集合＝设计清单）与
 *         PRJ-TX-11（复制一致性——前置未就绪的显式阻塞登记）。
 *
 * 设计依据：
 *   - units/project.md §12 PRJ-T15 行（产物：PRJ-TX-1～14 用例体——
 *     其中 TX-13 的"全入口遍历"与 TX-11 的"前置未就绪处置"为本任务
 *     新增落位面；其余 TX 组的用例体已随 PRJ-T02～T14 落位，本任务
 *     以执行留痕消账）、§11 PRJ-TX-13 行（PM-07：只读打开→逐个调用→
 *     一律拒绝＋稳定诊断→错误码集合＝设计清单）、§11 PRJ-TX-11 行
 *     （PM-05/AT-20——另存为/包导入导出的引用一致性）、§13.1（另存为/
 *     包导入导出实体＝阶段 B——WP-04-T17/T18）、§5.8（ISaveAsService/
 *     IPackageService 阶段 B 落地、接口先冻结）、§9.6（失权/只读门卫
 *     ——全部写入口的统一拒绝语义）、§10.1（归档 begin 锁面校验）；
 *   - units/testkit.md §6.2（TempDir 消费——unit 目标的 testkit 接入
 *     面，任务契约 acceptance 2）、§7.3（IRD_TEST_INFO 追溯登记）；
 *   - 需求 PM-07（只读打开：可查看不可写）、PM-05（另存为/包——阶段
 *     B）、AT-20、NFR-REL-01（写路径门卫）。
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
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/UndoRedo.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include "StubHandlers.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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
using sdurws::ird::project::stub::AppendObjectHandler;
using sdurws::ird::project::stub::AppendUndoHandler;
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
// 用例组：Tx11CopyConsistency（PRJ-TX-11——前置未就绪的显式阻塞登记）
// =====================================================================

/**
 * 锚定：PRJ-TX-11／PM-05／AT-20；units/project.md §13.1（另存为/包导入
 * 导出实体＝阶段 B——WP-04-T17/T18 承接）＋§5.8（接口先冻结、实体未
 * 落地）；任务契约 acceptance 3"前置未就绪的用例如实标注阻塞原因，
 * 不带病前进"。
 *
 * 处置：本用例**显式跳过**（GTEST_SKIP——gtest 计 skipped、
 * ird-test-report.json 计 skipped 不计通过，§7.2 四类分列），阻塞原因
 * ＝被测实体（ISaveAsService/IPackageService 实现体）未落地。§11 行的
 * 用例体语义（另存为换 projectId 后全链引用走查；包导入后哈希与引用
 * 一致）在 WP-04-T17/T18 的任务卡内执行并留痕——本登记保证 §11 表的
 * 逐行消账可见（"未执行"不被静默标注为"通过"，AGENTS §4.2）。
 */
TEST(Tx11CopyConsistency, StageBPrerequisiteNotReady_SkipRegistered)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"},
                  std::vector<std::string>{"AT-20"});
    GTEST_SKIP()
        << "前置未就绪：另存为/包导入导出实体归阶段 B（units/project.md "
           "§13.1——WP-04-T17/T18；§5.8 接口先冻结、实体未落地）。"
           "PRJ-TX-11 用例体（另存为换 projectId 后全链引用走查；包导入后"
           "哈希与引用一致）在实体落位任务中执行并留痕——本跳过即任务契约 "
           "acceptance 3『如实标注阻塞原因』的登记形态。";
}
