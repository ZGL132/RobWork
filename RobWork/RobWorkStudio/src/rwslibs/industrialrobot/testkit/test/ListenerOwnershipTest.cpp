/**
 * @file   ListenerOwnershipTest.cpp
 * @brief  监听器所有权定向用例组——F-958（units/testkit.md §7.3 安装契约）。
 *
 * 设计依据：
 *   - gtest.h 公开契约（vcpkg gtest 1.18.0）：TestEventListeners::Append
 *     "assumes the ownership of the listener (i.e. it will delete the listener
 *     when the test program finishes)"，Release 把所有权返还调用方；
 *   - F-958（test-build-audit.md / probe-r10）：修复前 installTestRecordListener
 *     把函数内 static 对象地址交给 Append——程序末尾 delete 非堆对象＝未定义
 *     行为；probe-r10 已用公开 API 实证集合析构真实释放所拥有监听器
 *     （该证据保留，本文件把它固化为可重复回归）。
 *
 * 覆盖面（对应 F-958 收口要求）：
 *   ①Append 接管→集合析构单次释放（进程内，局部集合）；②Release 返还所有权
 *   （契约另一半）；③隔离子进程端到端：经生产安装入口安装后监听器确实接收
 *   事件并按 OnTestProgramEnd 聚合落盘、报告唯一且落在 --ird_report 指定路径、
 *   passed/failed/skipped 三态记录与进程退出码（正常 0/预期失败非 0/skip 0）。
 *
 * 隔离纪律：失败/skip 样本载体是独立子进程 sdurws_ird_testkit_lsnchild
 * （tools/lsnchild，CMake 对齐输出目录），不污染本测试驱动的报告与退出码；
 * 旧实现反例（--lsn_mode=legacy）仅在取证/独验时人工执行——UB 表现不确定，
 * 不作常规红测断言。本文件内进程内用例使用局部 TestEventListeners 集合，
 * 与 TestMain 安装到 UnitTest 单例的生产监听器互不干扰。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/ProcessRunner.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// TestMain.cpp 记录的进程参数指针（TK-T11 增量接缝，ProcessRunnerTest 同款）：
// lsnchild 与测试可执行文件由 CMake 对齐到同一输出目录，argv[0] 定位即可。
extern char** g_irdTestArgv;

namespace {

using namespace sdurws::ird::testkit;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

/**
 * @brief 析构/事件计数监听器（测试辅助对象——只在用例体内存在，不入产品面）。
 *
 * 计数器以引用挂在用例局部变量上；类型本身无产品语义，仅为观察 gtest 公开
 * 所有权契约的释放次数与时机。
 */
class CountingListener : public ::testing::EmptyTestEventListener {
public:
    explicit CountingListener(int& destructed) : destructed_(destructed) {}

    ~CountingListener() override { ++destructed_; }

private:
    int& destructed_;   ///< 用例局部析构计数（引用不拥有）
};

/**
 * @brief 定位 lsnchild 辅助子进程（惰性一次；argv[0] 缺失＝布署断裂 fail-fast）。
 * @return lsnchild 可执行文件绝对路径
 * @throws std::runtime_error argv[0] 不可得（测试布署断裂，立即失败）
 */
const fs::path& lsnChildExe()
{
    static const fs::path cached = [] {
        if (g_irdTestArgv == nullptr || g_irdTestArgv[0] == nullptr) {
            throw std::runtime_error("ListenerOwnershipTest：argv[0] 缺失，无法定位"
                                     " lsnchild（测试布署断裂）");
        }
        return fs::path(g_irdTestArgv[0]).parent_path()
               / L"sdurws_ird_testkit_lsnchild.exe";
    }();
    return cached;
}

/**
 * @brief 装配指向 lsnchild 的 ProcessSpec（用例公用最小装配器）。
 * @param filter [in] gtest 过滤参数（选择单一三态样本用例）
 * @param reportPath [in] --ird_report 指定的报告落盘路径（UTF-8 参数契约）
 * @return 可直接传给 start() 的规格（工作目录由调用方给定）
 */
ProcessSpec makeChildSpec(const std::string& filter, const std::string& reportPath)
{
    ProcessSpec spec;
    spec.executable = lsnChildExe();
    spec.args = {filter, "--ird_report=" + reportPath};
    spec.timeout = 30000ms;   // 子进程只跑单一样本用例；30 s 已含杀软扫描余量
    return spec;
}

/**
 * @brief 读取报告文件全文（JsonLite 严格解析的输入）。
 * @param path [in] 报告路径（不存在/不可读时流失败→空串，断言自然失败）
 * @return 文件全文
 */
std::string readFileText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// 统计目录下 ird-test-report*.json 文件数（"报告只生成预期的一份"判据）。
int countReportFiles(const fs::path& dir)
{
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file()
            && name.rfind("ird-test-report", 0) == 0
            && name.size() > 5 && name.substr(name.size() - 5) == ".json") {
            ++count;
        }
    }
    return count;
}

/**
 * @brief 运行 lsnchild 并断言报告落点/唯一性与三态聚合。
 *
 * 公共断言：报告恰好一份、位于 --ird_report 指定路径（缺省路径无残留文件——
 * 参数摘除与自定义落盘的直接证明）、summary.total==1 且指定 outcome 计数==1。
 * （断言用 ASSERT_*——本函数必须返回 void，gtest 致命断言以 return 终止用例。）
 *
 * @param tag [in] TempDir 标签（三态用例目录互不重叠）
 * @param filter [in] gtest 过滤参数
 * @param outcomeToken [in] 预期的六值 outcome token（passed/failed/skipped）
 * @param out [out] 接收子进程 ProcessOutcome，供各用例继续断言退出码形态
 */
void runChildAndCheckReport(const char* tag, const std::string& filter,
                            const char* outcomeToken, ProcessOutcome& out)
{
    TempDir wd{tag};
    const auto reportPath = wd.path() / "ird-test-report.json";
    TestProcessRunner runner;
    runner.start(makeChildSpec(filter, reportPath.string()), wd.path());
    out = runner.outcome();

    // 报告唯一且落在指定路径（目录内 ird-test-report*.json 恰好 1 份）。
    EXPECT_EQ(countReportFiles(wd.path()), 1) << "报告必须恰好一份（F-958 收口）";
    EXPECT_TRUE(fs::exists(reportPath))
        << "报告必须落在 --ird_report 指定路径（参数摘除＋非默认落盘）";
    // 三态聚合：total=1、指定 outcome=1（summary.decisive 由三态语义自然导出——
    // passed 为 true，failed/skipped 因非满分列不为 true，不在此重复断言）。
    const auto v = parseJson(readFileText(reportPath));
    ASSERT_TRUE(v.isObject());
    const auto* summary = v.find("summary");
    ASSERT_NE(summary, nullptr);
    EXPECT_DOUBLE_EQ(summary->find("total")->number, 1.0);
    const auto* tokenCount = summary->find(outcomeToken);
    ASSERT_NE(tokenCount, nullptr) << "summary 必须按六值 token 分列计数";
    EXPECT_DOUBLE_EQ(tokenCount->number, 1.0);
    // 记录本体：唯一 test 条目的 outcome 与过滤选择一致（监听器真实接收了
    // OnTestStart/OnTestEnd/OnTestProgramEnd——记录与落盘均由事件驱动）。
    const auto* tests = v.find("tests");
    ASSERT_NE(tests, nullptr);
    ASSERT_TRUE(tests->isArray());
    ASSERT_EQ(tests->items.size(), 1u);
    EXPECT_STREQ(tests->items[0].find("outcome")->text.c_str(), outcomeToken);
}

}  // namespace

// =====================================================================
// ①② gtest 公开所有权契约（进程内——局部集合，与单例生产监听器隔离）
// =====================================================================

/// Append 接管所有权：集合析构时单次释放（gtest.h 公开契约；probe-r10 同型
/// 固化为回归——F-958 的修复正建立在该契约之上）。
TEST(ListenerOwnership, AppendTakesOwnershipAndDestroysOnce_UT_LSN)
{
    int destructed = 0;
    {
        ::testing::TestEventListeners listeners;
        listeners.Append(new CountingListener(destructed));
        EXPECT_EQ(destructed, 0) << "集合存活期内不得提前释放";
    }   // 集合析构＝gtest 公开契约的释放点
    EXPECT_EQ(destructed, 1) << "集合析构必须恰好释放一次所接管监听器";
}

/// Release 返还所有权：集合不再释放，释放责任回到调用方（契约另一半——
/// 因此"先移交再取回"是合法路径，静态地址移交始终非法）。
TEST(ListenerOwnership, ReleaseReturnsOwnershipToCaller_UT_LSN)
{
    int destructed = 0;
    ::testing::TestEventListeners listeners;
    auto* listener = new CountingListener(destructed);
    listeners.Append(listener);
    EXPECT_EQ(listeners.Release(listener), listener)
        << "Release 必须原样返还已接管监听器";
    delete listener;   // 所有权已返还——调用方释放一次
    EXPECT_EQ(destructed, 1);
}   // 集合析构不触及已 Release 指针（否则此处即双重释放）

// =====================================================================
// ③ 隔离子进程端到端（生产安装入口＋三态报告＋退出码）
// =====================================================================

/// 正常路径：passed 样本→进程退出 0（Exited）；报告聚合 passed=1。
TEST(ListenerOwnership, ChildPassReportAndExitZero_UT_LSN)
{
    ProcessOutcome out;
    runChildAndCheckReport("lsn-pass", "--gtest_filter=LsnChild.Passes",
                           "passed", out);
    EXPECT_EQ(out.kind, ProcessExitKind::Exited);
    EXPECT_EQ(out.exitCode, 0);
}

/// 预期失败路径：failed 样本→进程非零退出（§6.5 冻结语义归 Crashed，
/// exitCode 恒 0——非零退出这一事实由父测试在此明确验证）；报告聚合 failed=1。
TEST(ListenerOwnership, ChildFailReportAndNonZeroExit_UT_LSN)
{
    ProcessOutcome out;
    runChildAndCheckReport("lsn-fail", "--gtest_filter=LsnChild.Fails",
                           "failed", out);
    EXPECT_EQ(out.kind, ProcessExitKind::Crashed)
        << "预期失败子进程必须以非零退出码收场（§6.5：非零退出＝Crashed 形态）";
}

/// 显式跳过路径：GTEST_SKIP 样本→skipped 记录但不失败（进程退出 0）；
/// 报告聚合 skipped=1。
TEST(ListenerOwnership, ChildSkipReportAndExitZero_UT_LSN)
{
    ProcessOutcome out;
    runChildAndCheckReport("lsn-skip", "--gtest_filter=LsnChild.Skips",
                           "skipped", out);
    EXPECT_EQ(out.kind, ProcessExitKind::Exited);
    EXPECT_EQ(out.exitCode, 0);
}
