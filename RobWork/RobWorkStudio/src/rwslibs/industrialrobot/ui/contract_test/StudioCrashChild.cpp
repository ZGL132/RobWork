/**
 * @file   StudioCrashChild.cpp
 * @brief  WP-24-T02 进程级崩溃契约子进程实现（av/abort/hang 三模式——
 *         AT-11"诊断文件写出且脱敏"的触发器半区）。
 *
 * 设计依据（与 StudioCrashChild.hpp 头注同源，此处登记实现口径）：
 *   - §7.6/§7.7：崩溃报告内容＝脱敏后（凭据零命中、路径按策略）——本子
 *     进程故意**不挂接**管线脱敏服务（产品装配会挂——诊断栈双保险的
 *     第一道），dev 尾快照原文进 Writer，使父进程的脱敏断言精确落在
 *     **Writer 侧第二道防线**上（DT-SEC 双保险的强制半区实证）；
 *   - 停靠/标记协议：av/abort 模式崩溃即终结——无停靠标记（父进程以
 *     outcome() 阻塞等待终结）；hang 模式挂起在发射路径内，父进程以
 *     outcome() 等看门狗终结——同样无标记（EventWatch 免用：本组判据
 *     全部来自终结后的磁盘事实与进程结果）。
 *
 * 错误语义（Tx15Child 同款）：本文件运行于 gtest 之外（main 拦截先于
 * 测试框架）；装配违约（环境变量缺失/模式未知）以非零码退出（8/9），
 * 父进程用例失败并携带定位信息——不留假阳性通道。
 */

#include "StudioCrashChild.hpp"

#include "../studio/CrashCapture.hpp"

#include <sdurws/ird/diagnostics/Logging.hpp>   // LoggingPipeline/SystemClock/FileLogFileOps/LogSinkConfig
#include <sdurws/ird/diagnostics/CrashReport.hpp> // kCrashReportDevTailLines

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace sdurws {
namespace ird {
namespace ui {
namespace studio {

namespace {

using diagnostics::FileLogFileOps;
using diagnostics::LoggingPipeline;
using diagnostics::LogSinkConfig;
using diagnostics::SystemClock;

// ---------------------------------------------------------------------
// 环境变量辅助（宽字符——非 ASCII 临时路径无损；Tx15Child 同款纪律）
// ---------------------------------------------------------------------

/// 读宽字符环境变量（缺失/空＝空串——调用方判空后装配违约退出）。
std::wstring envWide(const wchar_t* name)
{
    const DWORD need = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (need == 0) {
        return {};
    }
    std::wstring value(static_cast<std::size_t>(need), L'\0');
    ::GetEnvironmentVariableW(name, value.data(), need);
    value.resize(static_cast<std::size_t>(need) - 1);  // 去掉结尾 NUL
    return value;
}

/// 读环境变量毫秒数（缺失/非法＝缺省值——hang 模式看门狗时限注入面）。
std::chrono::milliseconds envWatchdogMs()
{
    const std::wstring raw = envWide(L"IRD_T24_WATCHDOG_MS");
    if (raw.empty()) {
        return kCrashReportWatchdog;  // 产品默认 10 s
    }
    try {
        const unsigned long value = std::stoul(raw);
        if (value == 0) {
            return kCrashReportWatchdog;  // 0＝装配噪音——保守回退默认
        }
        return std::chrono::milliseconds{value};
    } catch (...) {
        return kCrashReportWatchdog;  // 非数字——同上
    }
}

// ---------------------------------------------------------------------
// 快照源替身（av/abort＝dev 尾采集；hang＝发射路径内永久挂起）
// ---------------------------------------------------------------------

/**
 * @brief 子进程快照源：dev 日志尾采集（产品 PluginDevTailCrashSource 同款
 *        形态——LoggingPipeline::devTailSnapshot），可选注入"采集内永久
 *        挂起"（hang 模式——看门狗自我了断的触发器）。
 *
 * 挂起为什么放在快照源：发射核心的挂起风险点＝运行期段采集与写盘——
 * 采集段是壳侧可注桩的合法位置；产品代码不保留挂起桩（防误触——见
 * triggerCrashTestChannel 注），进程级契约的挂起注入只存在于本测试子
 * 进程。
 */
class CrashChildSnapshotSource final : public CrashReportSnapshotSource {
public:
    explicit CrashChildSnapshotSource(LoggingPipeline* pipeline, bool hangInside)
        : m_pipeline(pipeline), m_hangInside(hangInside)
    {
    }

    CrashContextSegments snapshot() override
    {
        if (m_hangInside) {
            // 永久挂起（分段休眠——可被进程终结打断；不占用 CPU）。看门狗
            // 超时后 TerminateProcess：本函数永不返回。
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds{60});
            }
        }
        CrashContextSegments segments;
        if (m_pipeline != nullptr) {
            segments.devLogTail
                = m_pipeline->devTailSnapshot(diagnostics::kCrashReportDevTailLines);
        }
        return segments;
    }

private:
    LoggingPipeline* m_pipeline;  ///< dev 尾数据源（非拥有；hang 模式可为空）
    bool m_hangInside;            ///< true＝snapshot() 内永久挂起（hang 模式）
};

// ---------------------------------------------------------------------
// dev 日志线（含敏感原文——父进程脱敏断言的供数）
// ---------------------------------------------------------------------

/// 向管线写四行**原文**（不挂脱敏服务——见文件头实现口径）：
///   ①凭据键值形态（password=…——§7.7 规则①）；
///   ②令牌形态（Bearer 后随 ≥40 字符凭证串，含非十六进制字母——规则②，
///     故意混入 q/w/z 等非 hex 字母避免"纯十六进制＝摘要形态"豁免）；
///   ③本机路径（假名用户目录——规则④用户名段→[USER]＋规则⑤路径策略；
///     加上崩溃目录外的第二路径面）；
///   ④无敏感普通行（对照面——脱敏不得误伤普通文本）。
void logSensitivePreamble(LoggingPipeline& pipeline)
{
    // 级别取 Info（不采样——采样仅触及 Debug/Trace §7.4；Dev 通道缺省
    // sampleEveryDebug=100 会把第 2~3 行丢弃，破坏父进程 devLogTail=4 的
    // 确定性断言）。四行为**原文**——管线不挂脱敏服务（见文件头口径）。
    pipeline.logDev("t24-crash-child", diagnostics::LogLevel::Info,
                    "password=hunter2");
    pipeline.logDev("t24-crash-child", diagnostics::LogLevel::Info,
                    "Bearer Zx9qWm3rTk8pVb2nKd7fHg4jLl5asQwEtY6uIoPz");
    pipeline.logDev("t24-crash-child", diagnostics::LogLevel::Info,
                    "D:\\Users\\somebody\\docs\\plan.rwdesign 已打开");
    pipeline.logDev("t24-crash-child", diagnostics::LogLevel::Info,
                    "plain-line-no-secrets");
    // 等待异步日志线程把行渲染进环形缓冲（flush＝有界排空——§9.6；不
    // 等 flush 则快照可能缺行，父进程 devLogTail=4 断言失去确定性）。
    pipeline.flush(diagnostics::kLogDefaultFlushDeadline);
}

}  // namespace

// =====================================================================
// 分派入口（main 拦截 --ird-wp24-t02-child 后调用——分派契约见头文件）
// =====================================================================

int runCrashTestChild(const std::string& mode)
{
    if (mode == "av" || mode == "abort") {
        // ---- 装配违约检查（环境变量缺失＝8——父进程显性失败）----
        const fs::path crashDir(envWide(L"IRD_T24_CRASH_DIR"));
        const fs::path logDir(envWide(L"IRD_T24_LOG_DIR"));
        if (crashDir.empty() || logDir.empty()) {
            return 8;
        }

        // ---- dev 日志管线（原文进环形缓冲——Writer 侧脱敏即唯一防线）----
        SystemClock clock;
        FileLogFileOps fileOps;
        // 日志目录先建（管线 appendLine 失败会自产 DIAG-LOG-WRITE-FAILED
        // 警告行入环形缓冲——污染 devLogTail=4 的确定性断言面）。
        std::error_code fsEc;
        fs::create_directories(logDir, fsEc);
        LoggingPipeline pipeline(clock, fileOps);
        LogSinkConfig config;
        config.enabled = true;
        config.directory = logDir;
        pipeline.configure(config);
        logSensitivePreamble(pipeline);

        // ---- 安装捕获（产品全局路径——SEH 过滤器＋CRT 终止族＋看门狗；
        //      看门狗时限取环境注入〔缺省产品默认〕）----
        CrashChildSnapshotSource source(&pipeline, false);
        installCrashCapture(crashDir,
                            "ird-ui-contract-test child (wp24-t02)", &source,
                            envWatchdogMs());

        // ---- 受控崩溃（防误触通道——模式白名单已由分派保证）----
        triggerCrashTestChannel(mode);  // 不返回
        return 0;                       // 编译器满足（上一行不返回）
    }

    if (mode == "hang") {
        // ---- 发射路径挂起→看门狗自我了断 ----
        // 本地上下文（与全局安装共用同一 emitCrashReportTo 发射主体与
        // 看门狗机制——上下文形态见 makeCrashCaptureContext 契约）；挂起
        // 桩在快照源内（发射进入采集段后永不返回）。看门狗时限取环境
        // 注入（父进程注入短时限，保证用例墙钟可控）。
        const fs::path crashDir(envWide(L"IRD_T24_CRASH_DIR"));
        if (crashDir.empty()) {
            return 8;
        }
        CrashChildSnapshotSource hangingSource(nullptr, true);
        std::unique_ptr<CrashCaptureContext> ctx = makeCrashCaptureContext(
            crashDir, "ird-ui-contract-test child (wp24-t02 hang)", &hangingSource,
            envWatchdogMs());
        emitCrashReportTo(*ctx, "hang-probe", "hang inside snapshot");
        // 不可达（看门狗 TerminateProcess）；兜底显性失败。
        return 7;
    }

    return 9;  // 未知模式——分派表违约（Tx15Child 同口径）
}

}  // namespace studio
}  // namespace ui
}  // namespace ird
}  // namespace sdurws
