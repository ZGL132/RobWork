/**
 * @file   ProcessRunnerTest.cpp
 * @brief  进程测试支撑用例组——四态 outcome（正常退出/崩溃注入/强杀/超时
 *         终止）＋EventWatch 命中与超时双路径＋契约 fail-fast＋行为验证。
 *
 * 设计依据：
 *   - units/testkit.md §6.5（四态语义/Job Object 树终止/事件等待唯一判据/
 *     同步纪律）、§8 TK-FAULT（TK-T11 扩展）、§9 TK-T11 行
 *   - 需求 NFR-REL-02/03、AT-11/13（载体）；任务契约
 *     tasks/foundation/TK-T11.json acceptance 1/2
 *
 * 用例↔验收对照（每条 acceptance 至少一个具名测试）：
 *   acceptance 1（冻结签名全量实现）：
 *     - 四态判定 → ProcessRunnerOutcome 四用例；
 *     - start/kill 契约与错误语义 → ProcessRunnerContract/ProcessRunnerStart；
 *     - EventWatch deadline 语义 → EventWatchPath 五用例；
 *   acceptance 2（双路径全部通过＋留痕）→ 本文件全部用例（执行证据见
 *     gtest XML＋ird-test-report.json 留痕）。
 *
 * 用例纪律（§6.5 同步纪律）：正确性判据全部来自可观察事件或进程退出形态，
 * 无一处"固定 sleep 后断言"；时长断言只做 deadline 的上下界检查。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Fixture.hpp>     // TempDir：用例隔离目录（RAII）
#include <sdurws/ird/testkit/ProcessRunner.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>   // TestKitError：错误分类断言用

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// TestMain.cpp 记录的进程参数指针（TK-T11 增量接缝）：helper 与测试可执行
// 文件由 CMake 对齐到同一输出目录，用 argv[0] 定位即可，不依赖 PATH/工作目录。
extern char** g_irdTestArgv;

namespace {
using namespace sdurws::ird::testkit;
namespace fs = std::filesystem;

using namespace std::chrono_literals;   // 用例中时限字面量（500ms 等）

/**
 * @brief 取进程测试辅助子进程的绝对路径（惰性初始化，进程内只解析一次）。
 *
 * CMake 把 sdurws_ird_testkit_prochelper 的输出目录对齐到测试可执行文件
 * 所在目录（RUNTIME_OUTPUT_DIRECTORY 生成器表达式），因此按 argv[0] 的
 * 目录拼名即可。argv[0] 缺失属测试布署断裂（不该发生），fail-fast。
 *
 * @return helper 可执行文件绝对路径
 * @throws std::runtime_error argv[0] 不可得（布署断裂，立即失败）
 */
const fs::path& helperExe()
{
    static const fs::path cached = [] {
        if (g_irdTestArgv == nullptr || g_irdTestArgv[0] == nullptr) {
            throw std::runtime_error("ProcessRunnerTest：argv[0] 缺失，无法定位"
                                     " prochelper（测试布署断裂）");
        }
        return fs::path(g_irdTestArgv[0]).parent_path()
               / L"sdurws_ird_testkit_prochelper.exe";
    }();
    return cached;
}

/**
 * @brief 装配指向 helper 的 ProcessSpec（用例公用的最小装配器）。
 *
 * @param args    [in] helper 子命令与参数（UTF-8；路径参数须用 u8string()——
 *                ProcessSpec.args 契约为 UTF-8，fs::path::string() 在 MSVC
 *                是 ANSI 代码页，中文路径会损）
 * @param timeout [in] 超时上限；缺省 30 s——除超时用例外不应触发
 * @return 可直接传给 start() 的规格
 */
ProcessSpec makeSpec(std::vector<std::string> args,
                     std::chrono::milliseconds timeout = 30000ms)
{
    ProcessSpec spec;
    spec.executable = helperExe();
    spec.args = std::move(args);
    spec.timeout = timeout;
    return spec;
}

// 树用例共用的观察参数：孙进程延迟 3 s 写标记；3.5 s 的等待窗口给足"孙进程
// 若幸存必然完成"的余量（延迟＋落盘开销），marker 不出现即树终止的行为级
// 证据。两个数字必须满足"等待窗口 > 延迟"——改延迟时同步改窗口。
constexpr auto kTreeChildDelay = 3000ms;
constexpr auto kTreeObservationWindow = 3500ms;

}  // namespace

// =====================================================================
// 四态 outcome（acceptance 1 四态判定＋acceptance 2 第一句）
// =====================================================================

/// 正常退出：exit0 载体 → Exited＋退出码 0（§6.5：退出码 0＝正常完成事件）。
TEST(ProcessRunnerOutcome, NormalExitReportsExitedWithZeroCode)
{
    TempDir wd{"pr-normal"};
    TestProcessRunner runner;
    runner.start(makeSpec({"exit0"}), wd.path());

    const ProcessOutcome out = runner.outcome();
    EXPECT_EQ(out.kind, ProcessExitKind::Exited);
    EXPECT_EQ(out.exitCode, 0);
    EXPECT_GE(out.duration.count(), 0);   // 墙钟时长非负（近似值语义，量级不判）
}

/// 崩溃注入：非零退出码 → Crashed（§6.5"崩溃＝子进程非零退出"）；
/// exitCode 恒 0——ProcessOutcome 字段冻结语义"仅 Exited 时有效"。
TEST(ProcessRunnerOutcome, NonZeroExitReportsCrashed)
{
    TempDir wd{"pr-crash"};
    TestProcessRunner runner;
    runner.start(makeSpec({"exit", "3"}), wd.path());

    const ProcessOutcome out = runner.outcome();
    EXPECT_EQ(out.kind, ProcessExitKind::Crashed);
    EXPECT_EQ(out.exitCode, 0);   // 冻结语义：崩溃形态不携带码（信息走测试日志）
}

/// 强杀：spawn（父等孙、孙 3 s 后写 marker）→ 立即 kill() → Killed，
/// 且整棵树被终止（marker 永不出现＝孙进程也被杀——树终止的行为级证明）。
TEST(ProcessRunnerOutcome, KillReportsKilledAndTerminatesWholeTree)
{
    TempDir wd{"pr-kill"};
    const fs::path marker = wd.path() / L"tree-marker.txt";

    TestProcessRunner runner;
    runner.start(makeSpec({"spawn-touch-after",
                           std::to_string(kTreeChildDelay.count()),
                           marker.u8string()}),
                 wd.path());
    runner.kill();   // 启动后立即杀：无 sleep（§6.5——判据是结果，不是时序）

    const ProcessOutcome out = runner.outcome();
    EXPECT_EQ(out.kind, ProcessExitKind::Killed);
    EXPECT_EQ(out.exitCode, 0);

    // 树终止反向验证：若实现只杀了父进程而漏杀孙进程，孙进程会在 3 s 后
    // 写出 marker，本断言失败——"杀树"由此从实现声明变成可观测行为。
    EventWatch watch;
    EXPECT_FALSE(watch.awaitFile(marker, kTreeObservationWindow));
}

/// 超时终止：spec.timeout=500 ms，孙进程 3 s 后才写 marker → 先杀树再返回
/// TerminatedByTimeout；duration 卡在 deadline 与自然时长之间；marker 不出现。
TEST(ProcessRunnerOutcome, TimeoutReportsTerminatedByTimeoutAndKillsTree)
{
    TempDir wd{"pr-timeout"};
    const fs::path marker = wd.path() / L"tree-marker.txt";
    const auto timeout = 500ms;

    TestProcessRunner runner;
    runner.start(makeSpec({"spawn-touch-after",
                           std::to_string(kTreeChildDelay.count()),
                           marker.u8string()},
                          timeout),
                 wd.path());

    const ProcessOutcome out = runner.outcome();
    EXPECT_EQ(out.kind, ProcessExitKind::TerminatedByTimeout);
    EXPECT_EQ(out.exitCode, 0);
    EXPECT_GE(out.duration.count(), timeout.count());      // deadline 前不提前判超时
    EXPECT_LT(out.duration.count(), kTreeChildDelay.count());  // 未等满自然时长（真被终止）

    // 与强杀用例同款树终止反向验证（超时路径走的是同一 TerminateJobObject）。
    EventWatch watch;
    EXPECT_FALSE(watch.awaitFile(marker, kTreeObservationWindow));
}

// =====================================================================
// start/kill/outcome 调用方契约（acceptance 1 的错误语义面）
// =====================================================================

/// 未 start 即 outcome/kill＝Usage 违约（fail-fast——AGENTS 错误语义：
/// 调用方契约违约走异常，不静默返回伪造结果）。
TEST(ProcessRunnerContract, OutcomeOrKillBeforeStartThrowsUsage)
{
    TestProcessRunner runner;
    ASSERT_THROW(static_cast<void>(runner.outcome()), TestKitError);
    ASSERT_THROW(runner.kill(), TestKitError);

    // 分类核对：未 start 属调用方错误（Usage），不得误报为环境错误。
    try {
        static_cast<void>(runner.outcome());
        FAIL() << "outcome() 未抛出";
    } catch (const TestKitError& e) {
        EXPECT_EQ(e.kind(), TestKitErrorKind::Usage);
    }
}

/// 重复 start＝Usage（上一轮未收尾不得重启——句柄/临时目录资源无主化）；
/// 正常收尾后本用例不留活口进程。
TEST(ProcessRunnerContract, DoubleStartThrowsUsage)
{
    TempDir wd{"pr-double"};
    TestProcessRunner runner;
    runner.start(makeSpec({"exit0"}), wd.path());
    EXPECT_THROW(runner.start(makeSpec({"exit0"}), wd.path()), TestKitError);

    const ProcessOutcome out = runner.outcome();   // 收尾（幂等；防孤儿进程）
    EXPECT_EQ(out.kind, ProcessExitKind::Exited);
}

/// 空 executable／工作目录不存在＝Usage（装配错误挡在启动前，不产生半启动
/// 状态——两次 start 均失败后实例仍可安全析构）。
TEST(ProcessRunnerContract, BadSpecOrWorkingDirThrowsUsage)
{
    TestProcessRunner runner;
    ProcessSpec emptySpec;   // executable 缺省为空路径
    emptySpec.timeout = 1000ms;
    EXPECT_THROW(runner.start(emptySpec, TempDir{"pr-bad-a"}.path()), TestKitError);

    TempDir wd{"pr-bad-b"};
    // TempDir 自身存在但其子目录不存在——工作目录拼写错误的典型形态。
    EXPECT_THROW(runner.start(makeSpec({"exit0"}), wd.path() / "no-such-dir"),
                 TestKitError);
}

// =====================================================================
// start 行为面（acceptance 1：ProcessSpec env 覆盖＋workingDir 语义）
// =====================================================================

/// env 覆盖：ProcessSpec.env 条目真的到达子进程环境（"在父环境之上"的
/// 覆盖契约——经子进程回读环境变量落盘验证，非 mock）。
TEST(ProcessRunnerStart, EnvOverrideReachesChild)
{
    TempDir wd{"pr-env"};
    const fs::path outFile = wd.path() / L"env.txt";

    TestProcessRunner runner;
    ProcessSpec spec = makeSpec({"echo-env", "IRD_PROBE_VAR", outFile.u8string()});
    spec.env["IRD_PROBE_VAR"] = "probe-value-42";
    runner.start(spec, wd.path());

    EventWatch watch;
    EXPECT_TRUE(watch.awaitLineInFile(outFile, "IRD_PROBE_VAR=probe-value-42",
                                      5000ms));
    EXPECT_EQ(runner.outcome().kind, ProcessExitKind::Exited);   // 收尾
}

/// workingDir：子进程当前目录＝start 给定目录（相对路径产物落在 wd 内——
/// 目录参数不是摆设，而是子进程的 CWD）。
TEST(ProcessRunnerStart, WorkingDirIsChildCurrentDirectory)
{
    TempDir wd{"pr-wd"};
    TestProcessRunner runner;
    runner.start(makeSpec({"touch-after", "0", "relative-marker.txt"}), wd.path());

    EventWatch watch;
    EXPECT_TRUE(watch.awaitFile(wd.path() / "relative-marker.txt", 5000ms));
    EXPECT_EQ(runner.outcome().kind, ProcessExitKind::Exited);   // 收尾
}

/// workingDir 缺省：空路径＝TempDir 语义（§6.5 签名注释）——不抛异常且
/// 子进程正常完成＝缺省目录可建立并可用（路径本身由运行器私有持有）。
TEST(ProcessRunnerStart, EmptyWorkingDirFallsBackToOwnedTempDir)
{
    TestProcessRunner runner;
    runner.start(makeSpec({"exit0"}), {});   // 空路径触发缺省 TempDir 分支

    const ProcessOutcome out = runner.outcome();
    EXPECT_EQ(out.kind, ProcessExitKind::Exited);
}

// =====================================================================
// EventWatch 双路径（acceptance 2 第二句：命中与超时＋deadline 语义）
// =====================================================================

/// 命中：helper 200 ms 后写 marker，awaitFile 在 5 s 时限内返回 true。
TEST(EventWatchPath, AwaitFileHitsWhenMarkerAppears)
{
    TempDir wd{"ev-file-hit"};
    const fs::path marker = wd.path() / L"late-marker.txt";

    TestProcessRunner runner;
    runner.start(makeSpec({"touch-after", "200", marker.u8string()}), wd.path());

    EventWatch watch;
    EXPECT_TRUE(watch.awaitFile(marker, 5000ms));
    EXPECT_EQ(runner.outcome().kind, ProcessExitKind::Exited);   // 收尾
}

/// 超时：文件不出现 → false；实际等待 ≥ deadline（有界等待的下界语义：
/// 提前返回＝deadline 被私改），且远小于无界等待（不会挂死）。
TEST(EventWatchPath, AwaitFileTimesOutAtDeadline)
{
    TempDir wd{"ev-file-miss"};
    const fs::path marker = wd.path() / L"never-appears.txt";
    const auto deadline = 150ms;

    EventWatch watch;
    const auto t0 = std::chrono::steady_clock::now();
    const bool hit = watch.awaitFile(marker, deadline);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    EXPECT_FALSE(hit);
    EXPECT_GE(waited.count(), deadline.count());   // 不早于时限（下界）
    EXPECT_LT(waited.count(), 5000);               // 有界（不无限等待）
}

/// 命中（增量到达）：日志先有首行、300 ms 后追加目标行——awaitLineInFile
/// 必须等到"新行到达"，不是只在进入瞬间查一次（轮询语义的行为级证明）。
TEST(EventWatchPath, AwaitLineHitsWhenNeedleLineArrivesLate)
{
    TempDir wd{"ev-line-hit"};
    const fs::path log = wd.path() / L"app.log";

    TestProcessRunner runner;
    runner.start(makeSpec({"log-after", "300", log.u8string(),
                           "second-line-done"}),
                 wd.path());

    EventWatch watch;
    EXPECT_TRUE(watch.awaitLineInFile(log, "second-line-done", 5000ms));
    EXPECT_EQ(runner.outcome().kind, ProcessExitKind::Exited);   // 收尾
}

/// 超时（无命中）：文件存在但不含 needle → false，等待不早于 deadline
/// （契约："无命中＝超时"——轮询必须持续整个时限，不能查一遍就放弃）。
TEST(EventWatchPath, AwaitLineTimesOutWithoutNeedle)
{
    TempDir wd{"ev-line-miss"};
    const fs::path log = wd.path() / L"static.log";
    {
        std::ofstream seed{log, std::ios::binary};
        seed << "first line\nsecond line\n";   // 有行、无目标子串
    }
    const auto deadline = 150ms;

    EventWatch watch;
    const auto t0 = std::chrono::steady_clock::now();
    const bool hit = watch.awaitLineInFile(log, "never-matches", deadline);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    EXPECT_FALSE(hit);
    EXPECT_GE(waited.count(), deadline.count());
}

/// 超时（文件缺失）：日志文件始终未出现 → false（契约："文件未出现＝超时"
/// ——与"无命中"同归超时路径，不抛异常）。
TEST(EventWatchPath, AwaitLineTreatsMissingFileAsTimeout)
{
    TempDir wd{"ev-line-gone"};
    const auto deadline = 100ms;

    EventWatch watch;
    const auto t0 = std::chrono::steady_clock::now();
    const bool hit = watch.awaitLineInFile(wd.path() / L"absent.log", "x", deadline);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    EXPECT_FALSE(hit);
    EXPECT_GE(waited.count(), deadline.count());
}

/// deadline=0 边界：只探测当前状态——已存在的事件立即命中（不等待），
/// 不存在的事件立即超时（deadline 语义不含"至少等一会儿"）。
TEST(EventWatchPath, ZeroDeadlineChecksCurrentStateOnly)
{
    TempDir wd{"ev-zero"};
    const fs::path here = wd.path() / L"now.txt";
    {
        std::ofstream seed{here, std::ios::binary};
        seed << "x\n";
    }

    EventWatch watch;
    EXPECT_TRUE(watch.awaitFile(here, 0ms));   // 已发生：零时限也命中

    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_FALSE(watch.awaitFile(wd.path() / L"not-there.txt", 0ms));
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    EXPECT_LT(waited.count(), 50);   // 立即返回（无隐性最小等待）
}

/// needle 空串＝Usage（空串在任何文件都命中，等待语义失效——调用方
/// 装配错误 fail-fast，同 AGENTS 错误语义）。
TEST(EventWatchPath, EmptyNeedleThrowsUsage)
{
    TempDir wd{"ev-empty-needle"};
    EventWatch watch;
    try {
        static_cast<void>(watch.awaitLineInFile(wd.path() / L"any.log", "", 10ms));
        FAIL() << "awaitLineInFile 未抛出";
    } catch (const TestKitError& e) {
        EXPECT_EQ(e.kind(), TestKitErrorKind::Usage);
    }
}
