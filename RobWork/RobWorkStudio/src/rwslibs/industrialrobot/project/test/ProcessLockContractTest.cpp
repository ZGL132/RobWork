/**
 * @file   ProcessLockContractTest.cpp
 * @brief  写锁真进程契约用例组（跨单元契约面）——PRJ-TX-7 的
 *         TestProcessRunner 形态（testkit §10.1 节奏：AT-11/13 自动化
 *         载体——进程崩溃/强杀/事件等待）。
 *
 * 设计依据：
 *   - units/project.md §12 PRJ-T15 行（产物含"TestProcessRunner 接入的
 *     F8/PRJ-TX-7/8"；PRJ-T03 落位时 §3.3 登记"测试自再执行子进程，
 *     TestProcessRunner 归 PRJ-T15 按 testkit 节奏重述"——本组即该重述）、
 *     §11 PRJ-TX-7 行（第二实例只读；持锁进程卡顿（心跳停滞）；崩溃后
 *     并发接管（两实例同时获取））、§9.2（第二实例读取 PID 而不破坏互斥
 *     ——PM-07 提示数据）、§9.9（双实例锁竞争流程图：D-03 卡顿不接管／
 *     D-02 崩溃接管仅一胜者）、SA-17（写权限唯一依据＝OS 排他句柄）；
 *   - units/testkit.md §6.5（TestProcessRunner/EventWatch——正确性判据
 *     只来自可观察事件：标记文件行；kill＝主动崩溃注入；Killed 形态
 *     区分）、§10.1（触发登记：消费者 PRJ-T15）；
 *   - 需求 PM-07（只读降级＋持有者 PID 提示）、NFR-REL-02/03（进程隔离
 *     与恢复）、AT-11（载体）。
 *
 * 与 LockContractTest（PRJ-T03）的关系（零语义重复）：该组在 StoreLock
 * 原语层用 Win32 CreateProcess 直驱（进程内接缝 ILockOps），钉住锁内核
 * 语义；本组在存储上下文层（ProjectStoreFactory.open——锁＋恢复＋装载
 * 全协议）用 testkit TestProcessRunner 重述同一契约的端到端形态——两级
 * 断言互为对照，不互相替代（testkit §6.4/§6.5 覆盖分工同源）。
 *
 * 角色协作协议：子进程半边见 Tx15Child.cpp（lock-hold＝持锁停靠者；
 * lock-contend＝一次性竞争者，结果写标记后正常退出）；父进程（本文件）
 * 经 EventWatch.awaitLineInFile 等标记期望行、经 runner.kill() 注入崩溃。
 */

#include "Tx15Child.hpp"

#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/ProcessRunner.hpp>

#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using sdurws::ird::project::OpenStoreRequest;
using sdurws::ird::project::OpenStoreResult;
using sdurws::ird::project::ProjectStoreFactory;
using sdurws::ird::testkit::EventWatch;
using sdurws::ird::testkit::ProcessExitKind;
using sdurws::ird::testkit::ProcessSpec;
using sdurws::ird::testkit::TempDir;
using sdurws::ird::testkit::TestProcessRunner;

namespace {

/// path → UTF-8 窄串（ProcessSpec.env 契约编码——见 ProcessCrashContractTest）。
std::string utf8(const fs::path& p)
{
    return p.u8string();
}

/// 标记文件整读（读不到＝显性失败——不留空串假阳性）。
std::string readAll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取标记文件: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 从标记文本取 key= 行的值（缺失＝空串——调用方断言）。
std::string markerField(const std::string& text, const std::string& key)
{
    const std::string needle = key + "=";
    auto pos = text.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = text.find('\n', pos);
    if (end == std::string::npos) {
        end = text.size();
    }
    std::string value = text.substr(pos, end - pos);
    while (!value.empty()
           && (value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

}  // namespace

// =====================================================================
// 用例组：Tx7ProcessLock（套件名登记入 gtest XML 与任务证据）
// =====================================================================

/**
 * 父进程夹具：每用例独立 TempDir＋健康项目（createNew 的 r0）。子进程
 * 启动辅助与 marker/env 纪律同 ProcessCrashContractTest（同执行体自再
 * 执行——分派 token 区分角色）。
 */
class Tx7ProcessLock : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_tmp = std::make_unique<TempDir>("tx15-tx7");
        m_tmp->keepOnFailure(true);
        m_dir = m_tmp->path() / "proj.rwdesign";
        OpenStoreResult opened
            = ProjectStoreFactory::createNew(m_dir, "tx7 lock project");
        ASSERT_NE(opened.store, nullptr);
        ASSERT_TRUE(opened.store->writable());
    }

    void TearDown() override
    {
        if (m_tmp != nullptr) {
            m_tmp->noteTestFailure(::testing::Test::HasFailure());
        }
    }

    /// 启动子进程（mode 决定角色；park 不用——锁角色无停靠边界参数）。
    void startChild(TestProcessRunner& runner, const char* mode,
                    const fs::path& marker) const
    {
        ProcessSpec spec;
        wchar_t exePath[MAX_PATH];
        ASSERT_NE(::GetModuleFileNameW(nullptr, exePath, MAX_PATH), 0u);
        spec.executable = exePath;
        spec.args = {"--ird-wp04-t15-child", mode};
        spec.env["IRD_TX15_DIR"] = utf8(m_dir);
        spec.env["IRD_TX15_MARKER"] = utf8(marker);
        spec.timeout = std::chrono::milliseconds(120000);
        ASSERT_NO_FATAL_FAILURE(runner.start(spec, {}));
    }

    /// 等待竞争者标记出现期望行并解析字段（awaitLineInFile＝testkit §6.5
    /// 的可观察事件判据——禁止固定 sleep 后断言）。
    std::string awaitMarkerField(const fs::path& marker,
                                 const std::string& key) const
    {
        EventWatch watch;
        EXPECT_TRUE(watch.awaitLineInFile(marker, key + "=",
                                          std::chrono::milliseconds(60000)))
            << "竞争者标记未出现期望行: " << key;
        if (::testing::Test::HasFailure()) {
            return {};
        }
        return markerField(readAll(marker), key);
    }

    std::unique_ptr<TempDir> m_tmp;
    fs::path m_dir;
};

/**
 * 锚定：PRJ-TX-7 场景一（第二实例只读＋PID 提示）／PM-07／§9.2。
 *
 * 前置：父进程（本测试进程）可写打开项目并持续持有。
 * 操作：启动竞争者子进程（lock-contend）尝试可写打开。
 * 预期：子进程降级只读（writable=0）；其 lockInfo 持有者 PID＝父进程
 * PID（PM-07"项目被 PID=<n> 持有"提示数据——§9.2"读取 PID 而不破坏
 * 互斥"）；子进程正常退出（Exited/0）；父进程写权限不受影响。
 * 观测点：标记文件字段＋outcome 形态＋父进程 writable()。
 */
TEST_F(Tx7ProcessLock, SecondInstanceChildDegradesReadOnly_ReportsHolderPid)
{
    const DWORD parentPid = ::GetCurrentProcessId();

    // 父进程持锁：本用例的"第一实例"就是本测试进程（可写打开并持有——
    // 与子进程无共享内存，互斥只能由 OS 层裁决——SA-17 的进程间面）。
    OpenStoreRequest request;
    request.path = m_dir;
    OpenStoreResult parent = ProjectStoreFactory::open(request);
    ASSERT_NE(parent.store, nullptr);
    ASSERT_TRUE(parent.store->writable());

    // 第二实例（独立 OS 进程）尝试可写打开——互斥应在内核层拒绝。
    TestProcessRunner contender;
    const fs::path marker = m_tmp->path() / "contend-1.marker";
    ASSERT_NO_FATAL_FAILURE(startChild(contender, "lock-contend", marker));
    EXPECT_EQ(awaitMarkerField(marker, "writable"), "0")
        << "第二实例未降级只读（PM-07 互斥破坏）";
    EXPECT_EQ(awaitMarkerField(marker, "holder"), std::to_string(parentPid))
        << "只读实例观察到的持有者 PID 不是本进程（§9.2 提示数据失真）";

    // 竞争者结果形态：正常退出（信息面用例——非崩溃注入）。
    const auto outcome = contender.outcome();
    EXPECT_EQ(outcome.kind, ProcessExitKind::Exited);
    EXPECT_EQ(outcome.exitCode, 0);

    // 父进程写权限不受第二实例影响（互斥是单向让渡——不是破坏）。
    EXPECT_TRUE(parent.store->writable());
    EXPECT_EQ(parent.store->requestClose(), 0u);
}

/**
 * 锚定：PRJ-TX-7 场景二（持锁进程存活期间不接管——"卡顿不接管" D-03
 * 的存储上下文级重述）／SA-17／§9.9 流程图中段。
 *
 * 前置：持锁者子进程（lock-hold）可写打开后停靠存活（心跳线程照常
 * 跳动——"进程活着，锁就不可抢"的结构性口径）。
 * 操作：竞争者子进程尝试可写打开；随后父进程强杀持锁者，再放竞争者。
 * 预期：持锁者存活期——竞争者 writable=0 且观察到的持有者＝持锁者
 * PID（不接管、不阻塞等待——PM-07）；强杀后——竞争者 writable=1（D-02
 * 接管）。观测点：两轮标记字段＋持锁者 outcome.Killed。
 */
TEST_F(Tx7ProcessLock, AliveHolderNeverTakenOver_KillEnablesTakeover)
{
    // 持锁者：可写打开后停靠（标记携带自身 pid——存活证明）。
    TestProcessRunner holder;
    const fs::path holderMarker = m_tmp->path() / "holder.marker";
    ASSERT_NO_FATAL_FAILURE(startChild(holder, "lock-hold", holderMarker));
    EventWatch watch;
    ASSERT_TRUE(watch.awaitLineInFile(holderMarker, "writable=1",
                                      std::chrono::milliseconds(60000)))
        << "持锁者未取得写权限（前置失败）";
    const std::string holderPid = markerField(readAll(holderMarker), "pid");
    ASSERT_FALSE(holderPid.empty());

    // 存活期竞争：不接管（锁依据是内核互斥＋持有者存活，不是心跳推测
    // ——SA-17/D-03）。
    {
        TestProcessRunner contender;
        const fs::path marker = m_tmp->path() / "contend-alive.marker";
        ASSERT_NO_FATAL_FAILURE(startChild(contender, "lock-contend", marker));
        EXPECT_EQ(awaitMarkerField(marker, "writable"), "0")
            << "存活持有者期间发生了接管（D-03 破坏）";
        EXPECT_EQ(awaitMarkerField(marker, "holder"), holderPid)
            << "竞争者观察到的持有者不是持锁者进程";
    }

    // 崩溃注入：强杀持锁者（outcome 必须 Killed——§6.5 形态区分）。
    holder.kill();
    ASSERT_EQ(holder.outcome().kind, ProcessExitKind::Killed);

    // 死亡后竞争：接管成功（D-02——OS 句柄已随进程回收，内核互斥消失）；
    // 接管＝原地重写锁记录（§9.1）——胜利者观察到的持有者＝自身 pid。
    // 胜利者按协议持锁停靠（重叠窗口载体）——断言后由父进程收尾强杀。
    TestProcessRunner taker;
    const fs::path takeMarker = m_tmp->path() / "contend-take.marker";
    ASSERT_NO_FATAL_FAILURE(startChild(taker, "lock-contend", takeMarker));
    EXPECT_EQ(awaitMarkerField(takeMarker, "writable"), "1")
        << "持锁者死亡后接管失败（D-02 破坏）";
    const std::string takerPid = awaitMarkerField(takeMarker, "pid");
    ASSERT_FALSE(takerPid.empty());
    EXPECT_EQ(awaitMarkerField(takeMarker, "holder"), takerPid)
        << "接管后锁记录 PID 未更新为胜利者（§9.1 原地重写语义）";
    taker.kill();
    EXPECT_EQ(taker.outcome().kind, ProcessExitKind::Killed);
}

/**
 * 锚定：PRJ-TX-7 场景三（崩溃后两实例同时获取——仅一胜者 D-02 的
 * 存储上下文级重述）／§9.9 并发接管注（"内核对共享模式的裁决天然
 * 原子——仅一个成功"）。
 *
 * 前置：持锁者子进程持锁停靠→父进程强杀（锁文件内容残留、排他性消失
 * ——§9.1 崩溃释放语义）。
 * 操作：两个竞争者子进程背靠背启动（并发竞争起点——不设同步事件，
 * 竞争窗口由内核互斥自身保证；标记等待以 awaitLineInFile 有界轮询）。
 * 预期：恰一个 writable=1、恰一个 writable=0（禁止双胜/双败——D-02
 * 原子裁决）；胜利者 holder＝胜利者自身 pid；失败者 holder＝胜利者 pid。
 * 观测点：两标记字段集合。
 */
TEST_F(Tx7ProcessLock, CrashThenConcurrentTakeover_SingleWinner)
{
    // 前置：持锁者持锁→强杀。
    TestProcessRunner holder;
    const fs::path holderMarker = m_tmp->path() / "holder.marker";
    ASSERT_NO_FATAL_FAILURE(startChild(holder, "lock-hold", holderMarker));
    EventWatch watch;
    ASSERT_TRUE(watch.awaitLineInFile(holderMarker, "writable=1",
                                      std::chrono::milliseconds(60000)));
    holder.kill();
    ASSERT_EQ(holder.outcome().kind, ProcessExitKind::Killed);

    // 并发竞争：背靠背启动两个竞争者（同一时刻窗口——内核裁决原子性
    // 是被测对象，父进程不施加任何次序）。
    TestProcessRunner contenderA;
    TestProcessRunner contenderB;
    const fs::path markerA = m_tmp->path() / "race-a.marker";
    const fs::path markerB = m_tmp->path() / "race-b.marker";
    ASSERT_NO_FATAL_FAILURE(startChild(contenderA, "lock-contend", markerA));
    ASSERT_NO_FATAL_FAILURE(startChild(contenderB, "lock-contend", markerB));

    const std::string writableA = awaitMarkerField(markerA, "writable");
    const std::string writableB = awaitMarkerField(markerB, "writable");
    ASSERT_FALSE(writableA.empty() || writableB.empty())
        << "竞争者标记不完整：A=" << writableA << " B=" << writableB;

    // 恰一胜者：{1,0} 各一（存储上下文级的可写判据——非退出码猜测）。
    EXPECT_TRUE((writableA == "1" && writableB == "0")
                || (writableA == "0" && writableB == "1"))
        << "并发接管未呈现『仅一胜者』（D-02 破坏）：A=" << writableA
        << " B=" << writableB;

    // 胜利者身份落锁：holder＝胜利者自身 pid（§9.1 接管＝原地重写内容
    // ——胜利者打开时先取得互斥再重写，读到的必然是自己的记录）。
    const std::string winnerPid = writableA == "1"
        ? awaitMarkerField(markerA, "pid")
        : awaitMarkerField(markerB, "pid");
    EXPECT_FALSE(winnerPid.empty());
    EXPECT_EQ(writableA == "1" ? awaitMarkerField(markerA, "holder")
                               : awaitMarkerField(markerB, "holder"),
              winnerPid);
    // 注意（不判据声明）：失败者标记的 holder 字段＝其打开时刻锁文件的
    // 内容——可能是死亡前持有者，也可能是已重写的胜利者（与胜利者的
    // 相对时序未定），属于竞态观测而非契约面，故不断言（D-02 的判据
    // 只是"恰一胜者"＋胜利者身份落锁）。

    // 结果形态分流（胜利者按协议持锁停靠——重叠窗口载体；失败者降级
    // 只读后正常退出）：winner=Killed／loser=Exited——两进程角色与标记
    // 字段互相印证（防"标记内容与真实形态脱节"的假证通道）。
    TestProcessRunner& winnerRunner = writableA == "1" ? contenderA
                                                       : contenderB;
    TestProcessRunner& loserRunner = writableA == "1" ? contenderB
                                                      : contenderA;
    winnerRunner.kill();
    EXPECT_EQ(winnerRunner.outcome().kind, ProcessExitKind::Killed);
    EXPECT_EQ(loserRunner.outcome().kind, ProcessExitKind::Exited);
    EXPECT_EQ(loserRunner.outcome().exitCode, 0);
}
