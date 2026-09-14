/**
 * @file   CrashReport.cpp
 * @brief  崩溃诊断文件写出器实现——§7.6 内容六段的键值文本组装（全程脱敏
 *         ＋换行消毒）、逐行经 ILogFileOps 接缝落盘、失败降级（stderr 摘要
 *         ＋DIAG-LOG-WRITE-FAILED 开发诊断一条，不递归）。
 *
 * 设计依据（与 CrashReport.hpp 头注同源，此处只登记实现口径）：
 *   - §7.6：内容＝脱敏后的{进程/版本信息、异常类型与消息摘要（截断 512 B）、
 *     最近 512 行开发日志快照、活动任务清单（五元组）、活动 finding 清单
 *     （findingId/状态，不含载荷）、打开的项目身份（projectId，不含路径
 *     原文）}；写出位置＝用户目录（不入 .rwdesign——D-10）；写出失败→
 *     stderr 摘要＋放弃（不递归诊断）；
 *   - §9.5：崩溃文件为 IRedactionService 合法消费方；safeSummary 为"崩溃
 *     文件消费"的双保险入口（异常消息经其脱敏＋512 B 截断）；
 *   - §1.4：键值文本格式（"format=ird-crash-report/1" 首行自描述——find-
 *     ingDigest "IRDDREC1" 同款自识别惯例）；
 *   - 任务契约 tasks/foundation/DIAG-T08.json acceptance 2/3/4（DT-LIFE-4、
 *     写出设施就位不越界 dump、P-DIAG-1 core 契约值内嵌）。
 *
 * 实现口径（§7.6 未定判据，按 DTB §5.4 登记于单元卡 §14.4 v0.9）：
 *   1. **文件名** "crash-<epochMs>-<pid>.log"：UTC 毫秒取注入 IClock（测试
 *      可断言）；PID 取 input.processId，为 0 时自取当前进程（_getpid/
 *      getpid——唯一的环境查询点，仅身份标注用途不入脱敏面）。
 *   2. **Tier 选择与脱敏面**：自由文本（processInfo/exceptionType/
 *      devLogTail 行）经 redact(_, LogTier::Dev)——NFR-SEC-07 全量规则且不
 *      做内部十六进制遮蔽（AT-11 只要求"不含凭据类；路径按配置"；地址/哈希
 *      是崩溃现场的有价值诊断量）；异常消息走 safeSummary（User 档＋512 B
 *      UTF-8 边界截断——报告面语义）。**结构化身份字段不过脱敏**（task/
 *      finding/project 的 canonical 规范文本）：身份是结构化非敏感契约值，
 *      P-DIAG-1 要求"原样内嵌"；且多段 '/' 连接的规范串会被令牌形态规则整
 *      体吞掉（R-7 误伤）——与 exportSafeSummary 保留 subject 规范身份同款
 *      取舍，§14.4 v0.9 口径 2b。
 *   3. **换行消毒**：所有自由文本在脱敏后、落盘前把 \r/\n 替换为空格——
 *      键值文本的行完整性是解析面契约（一条记录一行；devLogTail 行来自管
 *      线渲染本已单行，消毒是纵深防御）。
 *   4. **失败码复用**：写出失败的开发诊断用 DIAG-LOG-WRITE-FAILED（码表
 *      §4.6 内置 87 码冻结，无 crash 专属码；崩溃文件属诊断写出面——消息
 *      以 "[crash-report-write-failed]" 前缀区分，channel "diag/logging"
 *      ＝管线内部通道旁路面，常量文本无回环风险）。
 *   5. **目录创建 best-effort**：create_directories 失败不单独报错——真实
 *      失败在 appendLine 处暴露（避免双重降级语义）。
 *
 * 线程模型：write 无共享可变状态（并发调用安全）；全部异常面捕获——
 * noexcept 兑现（崩溃路径次生异常即"制造崩溃"，文件头）。
 */

#include <sdurws/ird/diagnostics/CrashReport.hpp>

#include <cstdio>
#include <exception>
#include <utility>

#ifdef _MSC_VER
#include <process.h>  // _getpid（Windows——PID 自取）
#else
#include <unistd.h>   // getpid（POSIX——同上）
#endif

namespace sdurws::ird::diagnostics {

namespace {

/// 崩溃文件格式自描述标记（§1.4 键值文本；消费方以此识别文件形态）。
constexpr const char* kCrashFormatMarker = "ird-crash-report/1";

/// 写出失败的开发诊断通道（管线内部旁路面——常量文本，见实现口径 4）。
constexpr const char* kChannelLogging = "diag/logging";

/// 写出失败的开发诊断码（§4.6 ⑤复用——实现口径 4）。
constexpr const char* kCodeLogWriteFailed = "DIAG-LOG-WRITE-FAILED";

/// 当前进程 PID（实现口径 1——唯一环境查询点，身份标注用途）。
std::uint64_t currentProcessId() noexcept
{
#ifdef _MSC_VER
    return static_cast<std::uint64_t>(_getpid());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

/// epoch 毫秒（文件名与 writtenUtc 打点——注入 IClock 为唯一时钟源）。
std::uint64_t epochMillis(const std::chrono::system_clock::time_point& tp) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch())
            .count());
}

/// 换行消毒（实现口径 3：键值文本一行一记录——\r/\n 一律替换为空格）。
std::string sanitizeLine(std::string text) noexcept
{
    for (char& ch : text) {
        if (ch == '\n' || ch == '\r') { ch = ' '; }
    }
    return text;
}

/// finding 状态 token（§5.4 四值；switch 全枚举——确定性；崩溃文件面向
/// 排障阅读与机器扫描双面，token 与 Confirmable.hpp 状态枚举一一对应）。
std::string_view findingStateToken(FindingState state) noexcept
{
    switch (state) {
    case FindingState::Pending:     return "pending";
    case FindingState::Confirmed:   return "confirmed";
    case FindingState::Invalidated: return "invalidated";
    case FindingState::Expired:     return "expired";
    }
    return "pending";  // 全枚举不可达；保守取首值
}

/**
 * @brief 写出失败降级出口（§7.6"stderr 摘要＋放弃（不递归诊断）"；实现
 *        口径 4）。
 *
 * @param red    [in] 脱敏服务（stderr 摘要中的目录按策略脱敏——排障可用且
 *               不泄敏）
 * @param devLog [in] 开发诊断路由（可空＝只出 stderr；发射失败只吞不重试
 *               ——DT-LIFE-4"降级，不崩溃、不阻塞"）
 * @param file   [in] 写失败的文件路径（目录取 parent——文件名本身无诊断增量）
 */
void emitWriteFailure(const IRedactionService& red, IDevLogSink* devLog,
                      const std::filesystem::path& file) noexcept
{
    // ①stderr 摘要——目录经 redactPath 按策略脱敏。
    try {
        std::fputs("[ird-diagnostics] [crash-report-write-failed] code=DIAG-LOG-WRITE-FAILED"
                   " dir=",
                   stderr);
        std::fputs(red.redactPath(file.parent_path().string()).c_str(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        // stderr 摘要自身失败只吞——降级链绝不抛出（崩溃路径纪律）。
    }
    // ②开发诊断一条——通道为管线内部旁路面（常量文本，无脱敏回环风险）。
    if (devLog != nullptr) {
        try {
            devLog->logDev(
                kChannelLogging,
                "[crash-report-write-failed] code=DIAG-LOG-WRITE-FAILED 崩溃诊断文件"
                "写出失败——stderr 摘要已给出，放弃重试（§7.6 不递归诊断）");
        } catch (...) {
            // 路由失败只吞。
        }
    }
}

}  // namespace

// =====================================================================
// CrashReportWriter——构造与写出
// =====================================================================

CrashReportWriter::CrashReportWriter(const IRedactionService& redaction, const IClock& clock,
                                     ILogFileOps& fileOps, IDevLogSink* devLog) noexcept
    : m_redaction(&redaction)
    , m_clock(&clock)
    , m_fileOps(&fileOps)
    , m_devLog(devLog)
{
}

std::optional<std::filesystem::path> CrashReportWriter::write(
    const CrashReportInput& input, const std::filesystem::path& directory) noexcept
{
    // 崩溃路径铁律：任何异常一律降级 nullopt（外层总屏障；内层各步的局部
    // 捕获用于把失败归类为"写失败"——同一降级出口）。
    try {
        // ---- 文件名与目录（实现口径 1/5）----
        const std::uint64_t pid =
            input.processId != 0 ? input.processId : currentProcessId();
        const std::uint64_t nowMs = epochMillis(m_clock->nowUtc());
        std::filesystem::path dir = directory;
        std::error_code fsEc;
        std::filesystem::create_directories(dir, fsEc);  // best-effort——失败由写路径承载
        fsEc.clear();
        const std::filesystem::path file =
            dir / ("crash-" + std::to_string(nowMs) + "-" + std::to_string(pid) + ".log");

        // ---- 内容组装（§7.6 六段；键值文本一行一记录，全程脱敏＋消毒）----
        // 键值约定：format 首行自描述；数量键后随编号行——排障阅读与机器
        // 扫描双面友好。全部值经 sanitizeLine 保证单行。
        std::vector<std::string> lines;
        lines.reserve(16 + input.devLogTail.size() + input.activeTasks.size()
                      + input.activeFindings.size());
        lines.push_back(std::string("format=") + kCrashFormatMarker);
        lines.push_back("writtenEpochMs=" + std::to_string(nowMs));
        lines.push_back("pid=" + std::to_string(pid));
        // ①进程/版本信息（脱敏——调用方拼串可能内嵌路径/用户名）。
        lines.push_back("process=" + sanitizeLine(m_redaction->redact(input.processInfo,
                                                                     LogTier::Dev)));
        // ②异常类型与消息摘要（§7.6"截断 512 B"；消息经 safeSummary＝User
        //   档脱敏＋UTF-8 边界截断——报告面不带栈/地址/哈希）。
        lines.push_back("exceptionType=" + sanitizeLine(
                          m_redaction->redact(input.exceptionType, LogTier::Dev)));
        lines.push_back("exceptionMessage=" + sanitizeLine(
                          m_redaction->safeSummary(input.exceptionMessage,
                                                   kCrashReportExceptionMessageMaxBytes)));
        // ⑥打开的项目身份。**身份字段不过脱敏**（实现口径 2b）：canonical
        // 规范文本是结构化非敏感契约值（P-DIAG-1"原样内嵌"——且多段'/'连接
        // 的规范串会被令牌形态规则整体吞掉，R-7）；脱敏面只作用于自由文本。
        lines.push_back("project=" + sanitizeLine(
                          input.openProject.has_value()
                              ? input.openProject->toCanonical()
                              : std::string("-")));
        // ④活动任务清单（五元组规范串；"/"连接——与日志 ids=task: 字段同形，
        // Logging.cpp taskCanonical 同一口径；同口径 2b 不过脱敏）。
        lines.push_back("activeTasks=" + std::to_string(input.activeTasks.size()));
        for (std::size_t i = 0; i < input.activeTasks.size(); ++i) {
            const core::TaskIdentity& t = input.activeTasks[i];
            const std::string canonical =
                t.project.toCanonical() + "/" + t.branch.toCanonical() + "/"
                + t.revision.toCanonical() + "/" + t.run.toCanonical() + "/"
                + t.attempt.toCanonical();
            lines.push_back("task[" + std::to_string(i) + "]=" + sanitizeLine(canonical));
        }
        // ⑤活动 finding 清单（findingId/状态，不含载荷——§7.6；同口径 2b）。
        lines.push_back("activeFindings=" + std::to_string(input.activeFindings.size()));
        for (std::size_t i = 0; i < input.activeFindings.size(); ++i) {
            const CrashReportActiveFinding& f = input.activeFindings[i];
            lines.push_back("finding[" + std::to_string(i) + "]="
                            + sanitizeLine(f.id.toCanonical())
                            + " state=" + std::string(findingStateToken(f.state)));
        }
        // ③开发日志快照（双保险逐行再脱敏——第一道防线在管线渲染时已按
        // NFR-SEC-07 执行，此处不依赖其完备性，§7.6"内容＝脱敏后的"）。
        const std::size_t tailCount =
            input.devLogTail.size() > kCrashReportDevTailLines
                ? kCrashReportDevTailLines
                : input.devLogTail.size();
        lines.push_back("devLogTail=" + std::to_string(tailCount));
        for (std::size_t i = 0; i < tailCount; ++i) {
            const std::size_t idx = input.devLogTail.size() - tailCount + i;  // 最近行在尾
            lines.push_back("dev[" + std::to_string(i) + "]="
                            + sanitizeLine(m_redaction->redact(input.devLogTail[idx],
                                                               LogTier::Dev)));
        }

        // ---- 逐行落盘（ILogFileOps 接缝——开-写-关；任何失败即停止）----
        for (const std::string& line : lines) {
            bool ok = false;
            try {
                ok = m_fileOps->appendLine(file, line);
            } catch (...) {
                // 接缝允许抛异常（ILogFileOps 契约——消化责任在使用方）。
                ok = false;
            }
            if (!ok) {
                emitWriteFailure(*m_redaction, m_devLog, file);  // stderr＋开发诊断一条（不递归）
                return std::nullopt;
            }
        }
        return file;
    } catch (...) {
        // 组装面（内存/时钟/脱敏——§9.5 保证不抛，此处为纵深防御）异常：
        // 与写失败同一降级出口——崩溃路径绝不向捕获点抛出。
        std::fputs("[ird-diagnostics] crash report write failed (assembly error; "
                   "dir withheld)\n",
                   stderr);
        return std::nullopt;
    }
}

}  // namespace sdurws::ird::diagnostics
