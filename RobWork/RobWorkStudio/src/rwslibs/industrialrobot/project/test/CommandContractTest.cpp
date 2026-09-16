/**
 * @file   CommandContractTest.cpp
 * @brief  命令契约用例组（跨单元契约面——§3.3 `_contract_test` 分工）：
 *         PRJ-TX-1 并发与过期基线（ARC-01/PM-04）＋PRJ-TX-3 双编译桩
 *         三路（MDL-06/§6.6）。命令端口是 execution/ui 的消费面——其
 *         串行化与原子性契约在本目标承载（与锁/归档契约同级的跨单元
 *         承诺面）。
 *
 * 设计依据：
 *   - units/project.md §11 PRJ-TX-1（"双线程并发 submit；expectedRevision
 *     =旧 tip → 全局串行〔第二个等待〕；各恰好一个新修订〔seq 单调无
 *     重复〕；过期者 Rejected(stale-revision) 且无修订落盘＋稳定码
 *     PRJ-STALE-REVISION-REJECTED"）、§11 PRJ-TX-3（"IModelCompilePort
 *     桩：WC 失败/DWC 失败/双成功；任一失败→无修订无发布〔对象库无新
 *     对象〕；成功→恰好一个新修订；观测点：objects/ 计数＋.staging/tmp
 *     清理"）、§6.2（并发校验）、§6.6（双编译事务编排——任一失败不提
 *     交、临时资源隔离、回滚＝丢弃内存计划）；
 *   - 需求 ARC-01（命令原子产生修订＋全局串行）、PM-04（过期基线
 *     StaleRevisionRejected）、MDL-06（双编译原子性）、AT-29（项目撤
 *     销/重做＝新修订且历史不改写的提交面基础——本组钉住"每次提交恰
 *     一个新修订"的不变量）；
 *   - 任务契约 tasks/foundation/PRJ-T10.json acceptance 1/2/3：acceptance 1
 *     ＝PRJ-TX-1 全条目；acceptance 2＝PRJ-TX-3 三路＋D-09 临时落点＋
 *     §5.3.6 冻结签名（P-PR-7——桩按冻结三字段请求消费）；acceptance 3
 *     ＝全局串行槽的契约半区。
 *
 * 测试口径登记（DTB §5.4）：
 *   1. "第二个等待"的观测形态：并发提交由命令执行槽（§6.1 互斥）串行
 *      ——结果面证据＝两线程各得 Committed＋seq 严格递增＋parent 线性
 *      链（等待的必然结果；进程内调度时序不预设）。槽内阻塞的直接观
 *      测在单元面 CommandSlot.SecondSubmitWaitsWhileFirstInPrepare。
 *   2. 编译临时产物的落点注入：StubCompilePort 按用例给定的
 *      .staging/tmp 目录写 artifact.bin（模拟 runtime 中间产物——D-09
 *      "只允许写 .staging/tmp/"），观测点＝命令结束后该目录清空
 *      （§6.6"随事务/命令结束清理"；实现口径⑤——命令服务编排兑现）。
 *   3. "无修订落盘"观测点＝磁盘 revisions/ 目录计数（地面事实优先）＋
 *      权威分支 tip 不变（INV-M3 数据源）双通道。
 */

#include "StubHandlers.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace sdurws::ird::project::stub {

// =====================================================================
// PRJ-TX-1 并发与过期基线（acceptance 1；ARC-01/PM-04）
// =====================================================================

/// 双线程并发 submit：全局串行（第二个等待）——两线程各恰好一个新修订，
/// seq 单调无重复，parent 线性链（§11 PRJ-TX-1 前半）。
TEST(CommandContractTx1, TwoConcurrentSubmits_SerializedExactlyOneRevisionEach)
{
    ServiceFixture fx;
    fx.registerDefaults();
    const core::BranchId mainBranch = fx.primaryBranch();
    const RevisionView headBefore = fx.opened.store->query().head();

    // 两线程同时到达（barrier 语义——并发 submit 竞争命令执行槽）。
    std::atomic<int> arrivals{0};
    std::atomic<bool> release{false};
    auto waitForPeer = [&arrivals, &release] {
        arrivals.fetch_add(1);
        for (int i = 0; i < 1000 && arrivals.load() < 2; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        release.store(true);
    };

    auto submitA = std::async(std::launch::async, [&fx, mainBranch, waitForPeer] {
        waitForPeer();
        return fx.commands().submit(fx.appendEnvelope("from-A", mainBranch));
    });
    auto submitB = std::async(std::launch::async, [&fx, mainBranch, waitForPeer] {
        waitForPeer();
        return fx.commands().submit(fx.appendEnvelope("from-B", mainBranch));
    });

    const CommandResult resultA = submitA.get();
    const CommandResult resultB = submitB.get();

    // 各恰好一个新修订（§5.3.1 后置——成功路径不变量）。
    ASSERT_TRUE(resultA.committed());
    ASSERT_TRUE(resultB.committed());
    ASSERT_TRUE(resultA.newRevision.has_value());
    ASSERT_TRUE(resultB.newRevision.has_value());
    EXPECT_NE(*resultA.newRevision, *resultB.newRevision);

    // seq 单调无重复（O-15）＋parent 线性链（串行的结果面）：两修订的
    // 父链为 {headBefore, 先提交者}——先提交者 parent＝原 tip，后提交者
    // parent＝先提交者，HEAD 恰为后提交者（谁先谁后由槽竞争决定，断言
    // 与调度序无关）。
    const RevisionView viewA
        = fx.opened.store->query().revision(*resultA.newRevision);
    const RevisionView viewB
        = fx.opened.store->query().revision(*resultB.newRevision);
    ASSERT_TRUE(viewA.parent.has_value());
    ASSERT_TRUE(viewB.parent.has_value());
    const RevisionView* first = nullptr;
    const RevisionView* second = nullptr;
    if (*viewA.parent == headBefore.id && !(*viewB.parent == headBefore.id)) {
        first = &viewA;
        second = &viewB;
    } else if (*viewB.parent == headBefore.id && !(*viewA.parent == headBefore.id)) {
        first = &viewB;
        second = &viewA;
    } else {
        ADD_FAILURE() << "父链不构成线性两提交（ARC-01 串行契约）";
        return;
    }
    ASSERT_TRUE(second->parent.has_value());
    EXPECT_EQ(*second->parent, first->id);
    // seq 单调（O-15）：后提交者 seq＝先提交者＋1（无重复无跳跃——全局
    // 串行的序号面）。
    EXPECT_EQ(second->seq, first->seq + 1);
    // HEAD 恰为后提交者（恰好两个新修订——磁盘 revisions/ 计数＝前置＋2）。
    const RevisionView head = fx.opened.store->query().head();
    EXPECT_EQ(head.id, second->id);
    EXPECT_EQ(fx.revisionDirCount(), 3u);  // r0（初始）＋恰好 2 个新修订
}

/// 过期基线提交：expectedRevision＝旧 tip → Rejected(stale-revision)，
/// PRJ-STALE-REVISION-REJECTED 稳定码经 sink 上报，无修订落盘、分支 tip
/// 不变（§11 PRJ-TX-1 后半/§6.2；PM-04 同契约）。
TEST(CommandContractTx1, StaleBaseline_RejectedStaleRevisionNoRevision)
{
    ServiceFixture fx;
    fx.registerDefaults();
    const core::BranchId mainBranch = fx.primaryBranch();

    // 第一次提交（制造过期基线：旧 tip＝r0）。
    const RevisionView headBefore = fx.opened.store->query().head();
    const CommandResult first
        = fx.commands().submit(fx.appendEnvelope("advance", mainBranch));
    ASSERT_TRUE(first.committed());

    // 以旧 tip 为基线的第二次提交 → stale-revision。
    CommandEnvelope stale = fx.appendEnvelope("late", mainBranch);
    stale.expectedRevision = headBefore.id;  // 旧 tip（≠当前 tip）
    const CommandResult result = fx.commands().submit(stale);

    EXPECT_TRUE(result.rejected());
    EXPECT_EQ(result.status.rejection, CommandStatus::Rejection::StaleRevision);
    EXPECT_FALSE(result.newRevision.has_value());
    // 稳定码（§6.3 表——diagnostics.md §4.6 收编清单逐字值）双通道：
    // sink 用户级上报＋结果内诊断。
    EXPECT_TRUE(fx.sink.hasUserCode("PRJ-STALE-REVISION-REJECTED"));
    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].code, "PRJ-STALE-REVISION-REJECTED");
    // 无修订落盘＋分支 tip 不变（§6.2——rejected 不产生任何修订）。
    EXPECT_EQ(fx.revisionDirCount(), 2u);  // r0＋第一次提交，无第三次
    const RevisionView head = fx.opened.store->query().head();
    EXPECT_EQ(head.id, *first.newRevision);

    // 缺省基线（nullopt＝提交期解析为当前 tip）不受过期基线影响——正常
    // 提交仍成功（§6.2"缺省＝该分支当前 tip"）。
    const CommandResult after
        = fx.commands().submit(fx.appendEnvelope("current", mainBranch));
    EXPECT_TRUE(after.committed());
}

// =====================================================================
// PRJ-TX-3 双编译桩三路（acceptance 2；MDL-06/§6.6/P-PR-7 冻结面）
// =====================================================================

/**
 * @brief 双编译用例夹具：createNew 后为命令服务注入编译端口（装配面——
 *        OpenStoreRequest 无端口字段（§5.1 冻结），L5 装配形态为构造/
 *        装配通道注入；本夹具经同单元内部通道 setCompilePort 模拟 L5
 *        装配——R-2 先例：私有头仅同单元消费）。桩持有编译临时产物的
 *        落点注入（指向项目 .staging/tmp——D-09 观测面）。
 */
class DualCompileFixture {
public:
    ServiceFixture fx;
    // 端口所有权归本夹具（成员 unique_ptr——构造期注入裸指针；若为构造
    // 函数局部 unique_ptr，构造返回即释放＝m_compilePort 悬空——S5 虚
    // 调用 use-after-free）。
    std::unique_ptr<StubCompilePort> ownedPort;
    StubCompilePort* port = nullptr;

    explicit DualCompileFixture(StubCompilePort::Mode mode)
    {
        ownedPort = std::make_unique<StubCompilePort>(
            mode, fx.dir / ".staging" / "tmp");
        port = ownedPort.get();
        // 内部装配通道（§5.3.6 L5 注入面的测试侧形态——指针非 owning，
        // 所有权归本夹具）。
        fx.impl().commandService().setCompilePort(port);
        fx.registerDefaults();
    }

    CommandEnvelope dualCompileEnvelope() const
    {
        CommandEnvelope envelope;
        envelope.branch = fx.primaryBranch();
        envelope.commandType = "test-dual-compile";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'c', 'o', 'm', 'p', 'i', 'l', 'e'};
        return envelope;
    }
};

/// WorkCell 半边失败（真用例——见上方占位说明）。
TEST(CommandContractTx3, WcFail_NoRevisionNoPublishTmpCleaned)
{
    DualCompileFixture fixture(StubCompilePort::Mode::FailWorkCell);
    ServiceFixture& fx = fixture.fx;
    const std::size_t revisionsBefore = fx.revisionDirCount();
    const std::size_t objectsBefore = fx.objectDirCount();

    const CommandResult result = fx.commands().submit(fixture.dualCompileEnvelope());

    // 归类＝Failed(compile-failed)（§6.6：非断言不适用 Rejected 面）＋
    // 端口诊断透传。
    EXPECT_TRUE(result.failed());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->code(), StoreErrorCode::CompileFailed);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].code, "TEST-COMPILE-WC-FAILED");
    // 无修订无发布（对象库无新对象——§11 观测点；编译先于 S6，暂存区
    // 未创建）。
    EXPECT_FALSE(result.newRevision.has_value());
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
    EXPECT_EQ(fx.objectDirCount(), objectsBefore);
    // .staging/tmp 清理（§6.6"随事务/命令结束清理"——失败路径同样清理）。
    EXPECT_EQ(fx.stagingTmpCount(), 0u);
    // 编译输入观测（§6.6"编译输入＝计划闭包"——端口拿到 1 个计划写入＋
    // 基线修订＋可用查询端口）。
    EXPECT_EQ(fixture.port->lastPlannedWrites, 1u);
    EXPECT_EQ(fixture.port->lastBaseRevision,
              fx.opened.store->query().head().id.toCanonical());
    EXPECT_TRUE(fixture.port->queryWasUsable);
}

/// DynamicWorkCell 半边失败：同失败面（MDL-06——任一半边失败→不提交）。
TEST(CommandContractTx3, DwcFail_NoRevisionNoPublishTmpCleaned)
{
    DualCompileFixture fixture(StubCompilePort::Mode::FailDynamicWorkCell);
    ServiceFixture& fx = fixture.fx;
    const std::size_t revisionsBefore = fx.revisionDirCount();
    const std::size_t objectsBefore = fx.objectDirCount();

    const CommandResult result = fx.commands().submit(fixture.dualCompileEnvelope());

    EXPECT_TRUE(result.failed());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->code(), StoreErrorCode::CompileFailed);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].code, "TEST-COMPILE-DWC-FAILED");
    EXPECT_FALSE(result.newRevision.has_value());
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore);
    EXPECT_EQ(fx.objectDirCount(), objectsBefore);
    EXPECT_EQ(fx.stagingTmpCount(), 0u);
}

/// 双成功：恰好一个新修订（对象库恰新增域对象＋元数据对象）；提交后
/// 编译端口请求面记录被保留（编排证据）。
TEST(CommandContractTx3, BothOk_ExactlyOneRevision)
{
    DualCompileFixture fixture(StubCompilePort::Mode::Ok);
    ServiceFixture& fx = fixture.fx;
    const std::size_t revisionsBefore = fx.revisionDirCount();
    const std::size_t objectsBefore = fx.objectDirCount();

    const CommandResult result = fx.commands().submit(fixture.dualCompileEnvelope());

    // 成功→恰好一个新修订（§11 PRJ-TX-3）。
    EXPECT_TRUE(result.committed());
    ASSERT_TRUE(result.newRevision.has_value());
    EXPECT_EQ(fx.revisionDirCount(), revisionsBefore + 1);
    // 对象库恰新增 2（域对象＋元数据对象——§7.1 第 4 步发布面）。
    EXPECT_EQ(fx.objectDirCount(), objectsBefore + 2);
    // .staging/tmp 清理（成功路径同样收尾——§6.6）。
    EXPECT_EQ(fx.stagingTmpCount(), 0u);
    // 提交后视图可查（闭包内）。
    const RevisionView view
        = fx.opened.store->query().revision(*result.newRevision);
    EXPECT_EQ(view.commandSummary, "test dual compile");
}

}  // namespace sdurws::ird::project::stub
