/**
 * @file   StudioCrashContractTest.cpp
 * @brief  进程级崩溃契约用例组（WP-24-T02/AT-11）——子进程装捕获→受控
 *         崩溃→父进程断言：崩溃诊断文件落盘、内容脱敏（凭据零命中/路径
 *         按策略）、防重入看门狗自我了断。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.6（AT-11 观测点：诊断文件写出且脱敏——不含
 *     凭据类；路径按配置）、§7.7（脱敏规则表）、§12.1 协作行（WP-24-T02
 *     承接）；ARCHITECTURE §4.1（应用壳装配）；需求 PM-17/NFR-SEC-07/AT-11；
 *   - 任务契约 tasks/foundation/WP-24-T02.json acceptance 4（真实失败能力
 *     用例：子进程故意崩溃→父进程断言诊断文件落盘且脱敏）；
 *   - units/testkit.md §6.5（TestProcessRunner/EventWatch 消费纪律：正确性
 *     判据只来自可观察事件——本组判据＝进程终结形态＋磁盘文件事实，进程
 *     终结即事件源，免标记文件）、§6.2（TempDir 资源隔离与失败保留）。
 *
 * 与既有用例的关系（零重复）：diagnostics RedactionTest 已钉住 Writer 的
 *   脱敏规则与文件格式（进程内/注入接缝）；本组唯一断言面＝**真实 OS 崩
 *   溃**（SEH 访问违例/CRT abort/报告路径挂起）经完整捕获链（安装→过滤
 *   器→六段组装→Writer→磁盘）后的进程终结事实与文件内容——进程级半边
 *   （ProcessCrashContractTest 同款分工，两者不互相替代）。
 *
 * 敏感供数说明：子进程以**原文**向 dev 日志环形缓冲写入凭据/令牌/路径行
 *   （见 StudioCrashChild.cpp 实现口径——不挂管线脱敏），父进程的脱敏断
 *   言因此精确落在 Writer 侧第二道防线上（产品装配为双保险，本组强制实
 *   证其中 Writer 半区）。
 */

#include "StudioCrashChild.hpp"

#include "../studio/CrashCapture.hpp"  // kCrashWatchdogSelfExitCode（hang 形态识别——值断言注释）

#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/ProcessRunner.hpp>

#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::testkit::ProcessExitKind;
using sdurws::ird::testkit::ProcessOutcome;
using sdurws::ird::testkit::ProcessSpec;
using sdurws::ird::testkit::TempDir;
using sdurws::ird::testkit::TestProcessRunner;

namespace {

// ---------------------------------------------------------------------
// 辅助（子进程启动协议——Tx15Child 同款：自引用 exe＋argv 分派＋env 参数）
// ---------------------------------------------------------------------

/// 二进制整读（读不到＝显性失败——不留"读不到＝内容不符"的假阳性通道）。
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

/// path → UTF-8 窄串（ProcessSpec.env 的契约编码——ProcessRunner 以 UTF-8
/// 解释再转宽字符，非 ASCII 临时路径无损）。
std::string utf8(const fs::path& p)
{
    return p.u8string();
}

/// 目录下 crash-*.log 文件清单（AT-11 观测面——文件存在性/唯一性判据）。
std::vector<fs::path> listCrashFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return files;
    }
    for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("crash-", 0) == 0 && name.size() > 4
            && name.substr(name.size() - 4) == ".log") {
            files.push_back(it->path());
        }
    }
    return files;
}

/// 启动子进程（mode 经 argv 分派；目录/看门狗时限经 env 传递——宽字符
/// 安全；timeout 为兜底回收上限，被测路径自身会终结进程、不会到点）。
void startCrashChild(TestProcessRunner& runner, const char* mode,
                     const fs::path& crashDir, const fs::path& logDir,
                     int watchdogMs)
{
    ProcessSpec spec;
    wchar_t exePath[MAX_PATH];
    ASSERT_NE(::GetModuleFileNameW(nullptr, exePath, MAX_PATH), 0u)
        << "无法取自身可执行路径";
    spec.executable = exePath;
    spec.args = {"--ird-wp24-t02-child", mode};
    spec.env["IRD_T24_CRASH_DIR"] = utf8(crashDir);
    if (!logDir.empty()) {
        spec.env["IRD_T24_LOG_DIR"] = utf8(logDir);
    }
    if (watchdogMs > 0) {
        spec.env["IRD_T24_WATCHDOG_MS"] = std::to_string(watchdogMs);
    }
    spec.timeout = std::chrono::milliseconds(120000);
    ASSERT_NO_FATAL_FAILURE(runner.start(spec, {}));
}

/// 崩溃文件公共断言面（av/abort 共用）：format 首行＋异常类型＋脱敏四断言
/// （凭据/令牌/用户名路径/唯一临时目录名零命中——NFR-SEC-07）＋普通行
/// 不误伤＋计数行。
void assertCrashFileRedacted(const fs::path& crashFile, const fs::path& logDir,
                             const std::string& expectedExceptionType)
{
    const std::string text = readAll(crashFile);
    ASSERT_FALSE(text.empty()) << "崩溃诊断文件为空——AT-11 落盘断言失据";

    // ---- 键值文本自描述（§1.4/§7.6 六段首行）----
    EXPECT_EQ(text.substr(0, std::string("format=ird-crash-report/1").size()),
              "format=ird-crash-report/1")
        << "文件须以 format 标记行开始（实际首行: "
        << text.substr(0, text.find('\n')) << "）";

    // ---- 异常类型段（崩溃形态保真——捕获点知道什么写什么）----
    EXPECT_NE(text.find("exceptionType=" + expectedExceptionType),
              std::string::npos)
        << "异常类型段与崩溃形态不符";

    // ---- 脱敏断言（§7.7/NFR-SEC-07——凭据零命中；路径按策略）----
    EXPECT_EQ(text.find("hunter2"), std::string::npos)
        << "凭据值原文命中——NFR-SEC-07 凭据零命中违约";
    EXPECT_NE(text.find("[REDACTED:credential"), std::string::npos)
        << "凭据键值替换标记缺失（脱敏未生效或规则未命中）";
    EXPECT_EQ(text.find("Zx9qWm3rTk8pVb2nKd7fHg4jLl5asQwEtY6uIoPz"),
              std::string::npos)
        << "令牌原文命中——NFR-SEC-07 令牌零命中违约";
    EXPECT_NE(text.find("Bearer [REDACTED:token"), std::string::npos)
        << "Bearer 凭证段替换标记缺失";
    EXPECT_EQ(text.find("somebody"), std::string::npos)
        << "用户名路径段原文命中——§7.7 规则④违约";
    EXPECT_NE(text.find("[USER]"), std::string::npos)
        << "用户名替换标记缺失";
    // 唯一日志目录名（临时目录Tag）不得以任何形态出现——路径策略生效的
    // 观测面（RootOnly 下中间目录段被"…"折叠；Keep 配置才会保留）。
    EXPECT_EQ(text.find(logDir.filename().string()), std::string::npos)
        << "崩溃文件出现日志目录原名——路径脱敏违约";

    // ---- 非敏感内容不误伤＋段计数（脱敏保真方向的反例面）----
    EXPECT_NE(text.find("plain-line-no-secrets"), std::string::npos)
        << "普通行被误伤——脱敏过头（保守方向的边界）";
    EXPECT_NE(text.find("plan.rwdesign"), std::string::npos)
        << "路径文件名应保留（§7.7 文件名保留语义）";
    EXPECT_NE(text.find("devLogTail=4"), std::string::npos)
        << "dev 快照行数计数（四行原文供数应全量入快照）";
    EXPECT_NE(text.find("pid="), std::string::npos) << "进程身份段在案";
}

}  // namespace

// =====================================================================
// 用例组：StudioCrashReport（进程级契约——AT-11 具名观测点）
// =====================================================================

/**
 * 父进程夹具：每用例独立 TempDir（testkit §6.2）——crash/ 与 devlog/ 两
 * 子目录分别承载崩溃诊断文件与子进程 dev 日志。
 */
class StudioCrashReport : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_tmp = std::make_unique<TempDir>("t24-crash");
        m_tmp->keepOnFailure(true);
        m_crashDir = m_tmp->path() / "crash";
        m_logDir = m_tmp->path() / "devlog";
    }

    void TearDown() override
    {
        if (m_tmp != nullptr) {
            m_tmp->noteTestFailure(::testing::Test::HasFailure());
        }
    }

    std::unique_ptr<TempDir> m_tmp;  ///< 用例临时目录（失败保留语义）
    fs::path m_crashDir;             ///< 崩溃诊断文件目录（用户目录的替身）
    fs::path m_logDir;               ///< 子进程 dev 日志目录（快照供数）
};

/**
 * 锚定：AT-11（"进程崩溃后重启：诊断文件写出且脱敏"——真实崩溃形态主
 * 用例）／PM-17／NFR-SEC-07／§7.6 六段（进程级半边）。
 *
 * 前置：无（子进程自装配：dev 原文四行→装捕获→受控访问违例）。
 * 操作：启动子进程（av 模式）→outcome() 阻塞等待终结→扫描崩溃目录。
 * 预期：子进程以崩溃形态终结（非零退出——SEH 过滤器返回
 * EXECUTE_HANDLER 后进程以异常码终结）；crash/ 恰一份 crash-*.log；
 * 内容断言见 assertCrashFileRedacted（凭据/令牌/用户名/目录名零命中＋
 * 普通行/文件名保留＋六段计数）。
 */
TEST_F (StudioCrashReport, AccessViolation_WritesRedactedCrashFile_AT11)
{
    TestProcessRunner runner;
    ASSERT_NO_FATAL_FAILURE(
        startCrashChild(runner, "av", m_crashDir, m_logDir, 0));

    // 崩溃终结（非零退出＝Crashed——testkit §6.5 四态判定；挂起由
    // spec.timeout 兜底回收，到点即用例失败形态而非静默等待）。
    const ProcessOutcome outcome = runner.outcome();
    EXPECT_EQ(outcome.kind, ProcessExitKind::Crashed)
        << "受控访问违例应以崩溃形态终结（实际 kind="
        << static_cast<int>(outcome.kind) << " exitCode=" << outcome.exitCode
        << " duration=" << outcome.duration.count() << "ms）";

    // AT-11 观测点：诊断文件写出且脱敏（文件事实判据——进程终结后磁盘
    // 内容已定，免标记文件）。
    const std::vector<fs::path> files = listCrashFiles(m_crashDir);
    ASSERT_EQ(files.size(), std::size_t{1})
        << "崩溃目录应恰产出一份诊断文件";
    ASSERT_NO_FATAL_FAILURE(assertCrashFileRedacted(
        files[0], m_logDir, "seh-access-violation"));
}

/**
 * 锚定：AT-11 的 CRT 终止族半边／PM-17（"异常退出写出诊断文件"——abort
 * 家族形态）／任务契约 acceptance 1（Windows 结构化异常 **与** CRT 终止
 * 路径两族捕获）。
 *
 * 前置/操作：同上（abort 模式——raise(SIGABRT) 经 CRT 信号回调路径）。
 * 预期：子进程以崩溃形态终结（信号回调恢复默认处置后重发→进程非零终
 * 结）；崩溃文件在案且脱敏；异常类型段＝crt-abort（信号族标签）。
 */
TEST_F (StudioCrashReport, CrtAbort_WritesRedactedCrashFile_AT11)
{
    TestProcessRunner runner;
    ASSERT_NO_FATAL_FAILURE(
        startCrashChild(runner, "abort", m_crashDir, m_logDir, 0));

    const ProcessOutcome outcome = runner.outcome();
    EXPECT_EQ(outcome.kind, ProcessExitKind::Crashed)
        << "CRT abort 应以崩溃形态终结（实际 kind="
        << static_cast<int>(outcome.kind) << " exitCode=" << outcome.exitCode << "）";

    const std::vector<fs::path> files = listCrashFiles(m_crashDir);
    ASSERT_EQ(files.size(), std::size_t{1});
    ASSERT_NO_FATAL_FAILURE(
        assertCrashFileRedacted(files[0], m_logDir, "crt-abort"));
}

/**
 * 锚定：WP-24-T02 acceptance 1——"捕获路径纪律……超时自我了断（防止卡
 * 死在报告路径）"／PM-03 精神（关闭/异常路径不得永久阻塞）。
 *
 * 前置：子进程 hang 模式（快照源在发射路径内永久挂起；看门狗时限注入
 * 800 ms——用例墙钟可控）。
 * 操作：启动→outcome() 等终结→扫描崩溃目录。
 * 预期：进程在看门狗时限量级内被自我了断终结（非零退出码——
 * kCrashWatchdogSelfExitCode 语义形态；testkit 归 Crashed）；崩溃目录
 * 无 crash-*.log（挂起点在写盘之前——报告未完成即被终止，不留半成品
 * 假象）；进程终结远早于 spec.timeout（duration < 60 s——若看门狗失效
 * 本用例由 timeout 兜底回收翻红，防"卡死在报告路径"的真实失败能力）。
 */
TEST_F (StudioCrashReport, HungReportPath_WatchdogSelfTerminates_Acceptance1)
{
    TestProcessRunner runner;
    ASSERT_NO_FATAL_FAILURE(
        startCrashChild(runner, "hang", m_crashDir, m_logDir, 800));

    const ProcessOutcome outcome = runner.outcome();
    EXPECT_EQ(outcome.kind, ProcessExitKind::Crashed)
        << "看门狗自我了断以非零码终结（实际 kind="
        << static_cast<int>(outcome.kind) << " exitCode=" << outcome.exitCode << "）";
    EXPECT_LT(outcome.duration, std::chrono::milliseconds{30000})
        << "报告路径挂起未被及时自我了断（看门狗 800 ms 注入失效——"
        << "耗时 " << outcome.duration.count() << "ms）";

    EXPECT_TRUE(listCrashFiles(m_crashDir).empty())
        << "挂起点在写盘之前——不应有诊断文件产出";
}
