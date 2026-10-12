/**
 * @file   CrashCapture.cpp
 * @brief  应用壳侧崩溃捕获装配实现——Windows 结构化异常（SEH）＋CRT 终止
 *         族（signal/invalid-parameter/purecall/terminate）统一接入捕获点，
 *         组装 CrashReportInput 六段经既有 CrashReportWriter 写用户目录；
 *         防重入一次性闸门＋看门狗自我了断。
 *
 * 设计依据（与 CrashCapture.hpp 头注同源，此处只登记实现口径）：
 *   - units/diagnostics.md §7.6（捕获点组装 CrashReportInput、写用户目录、
 *     写出失败→stderr 放弃；Writer 设施归 diagnostics——沿用不重写）、
 *     §7.7（脱敏在 Writer 侧统一执行）、§12.1 协作行（WP-24-T02 承接）；
 *   - 需求 PM-17/AT-11/NFR-SEC-07；任务契约 tasks/foundation/WP-24-T02.json
 *     acceptance 1（入口最先安装、防重入、降级不阻塞）。
 *
 * 实现口径（未定判据，按 DTB §5.4 登记于 WP-24-T02 note 与 diagnostics.md
 * §14.4 / ui.md §16.7 实施登记）：
 *   1. **异常类型标签**：SEH 代码→稳定文本（switch 常见码族＋十六进制码
 *      原值——snippet 缓冲栈上 snprintf，不在崩溃点做堆分配）；CRT 族→
 *      "crt-*" 前缀标签；terminate→"cpp-terminate"＋active exception 消息。
 *   2. **回调体极简**：所有 OS 回调只是薄壳——取信息（栈缓冲）→委托
 *      emitCrashReportTo（唯一发射主体）→按族终结进程（SEH 返回
 *      EXCEPTION_EXECUTE_HANDLER＝进程以异常码终结〔不走 WER 对话框——
 *      SetErrorMode 双保险〕；信号/CRT 族 _exit(3)＝MSVC abort 惯例码）。
 *   3. **一次性闸门**：atomic_flag test_and_set，首次发射后永不复位——
 *      崩溃后进程应当终结；若第一个崩溃报告写完后再次崩溃（如报告路径
 *      自身缺陷），第二道闸保证不再递归报告。
 *   4. **看门狗**：安装期预建线程＋两个 auto-reset 事件（崩溃期零创建）；
 *      发射进入布防/退出撤防；超时 TerminateProcess 自我了断（专用退出码
 *      kCrashWatchdogSelfExitCode——运维可识别"报告挂起被终止"形态）。
 *   5. **快照源异常隔离**：snapshot() 整体 try/catch——快照源实现允许
 *      内部失败（返回部分段/抛异常都只是段缺失，不击穿报告路径）。
 *
 * 线程模型：回调在崩溃线程执行；闸门保证发射主体全局串行且一次性；共享
 *   量（source/shuttingDown）为原子量；看门狗线程安装期创建、事件驱动。
 */

#include "CrashCapture.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>    // _set_purecall_handler（CRT 纯虚调用终止路径）
#include <process.h>   // _beginthreadex（看门狗线程——CRT 线程面）
#include <csignal>     // std::signal/std::raise（CRT 终止族）
#include <stdlib.h>    // _set_invalid_parameter_handler/_exit
#endif

namespace sdurws {
namespace ird {
namespace ui {
namespace studio {

// =====================================================================
// 常量与栈缓冲辅助（崩溃路径零堆分配的自持面）
// =====================================================================

namespace {

/// 异常类型标签上限（类型名是短词法面——128 字节足够；超出截断）。
constexpr std::size_t kExceptionTypeMaxChars = 128;

/// SEH 消息缓冲上限（"code=0x........ (<label>) address=..." 形态——256
/// 字节足够；Writer 侧另有 safeSummary 512 B 截断兜底）。
constexpr std::size_t kExceptionMessageMaxChars = 256;

/// 发射核心的全局上下文（installCrashCapture 发布；原子读——崩溃线程
/// 可见性）。产品语境下安装至多一次（幂等拒绝重复安装）。
std::atomic<CrashCaptureContext*> g_context{nullptr};

/// 异常类型标签拷贝（string_view→定长栈缓冲；返回截断后的视图——崩溃
/// 路径不做堆分配；视图生命周期＝缓冲生命周期，调用方同栈使用）。
std::string_view clampType(char (&buffer)[kExceptionTypeMaxChars],
                           std::string_view type) noexcept
{
    const std::size_t n = type.size() < (kExceptionTypeMaxChars - 1)
                              ? type.size()
                              : (kExceptionTypeMaxChars - 1);
    std::memcpy(buffer, type.data(), n);
    buffer[n] = '\0';
    return std::string_view{buffer, n};
}

#ifdef _WIN32

/// SEH 异常代码→稳定标签（switch 常见码族；未知码回退 "seh-unknown"——
/// 码原值已随消息携带，标签只承担可读性）。
std::string_view sehCodeLabel(DWORD code) noexcept
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "seh-access-violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "seh-illegal-instruction";
    case EXCEPTION_STACK_OVERFLOW:        return "seh-stack-overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "seh-int-divide-by-zero";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "seh-float-divide-by-zero";
    case EXCEPTION_IN_PAGE_ERROR:         return "seh-in-page-error";
    case EXCEPTION_PRIV_INSTRUCTION:      return "seh-privileged-instruction";
    default:                              return "seh-unknown";
    }
}

// =====================================================================
// 看门狗（报告路径挂起的自我了断——实现口径 4）
// =====================================================================

/**
 * @brief 看门狗线程主体：常驻等待布防事件；布防后在时限内等撤防事件——
 *        超时即 TerminateProcess 自我了断。
 *
 * 为什么是"预建线程＋事件"而不是崩溃期建线程：崩溃点建线程需要堆分配与
 * 加载器锁（DllMain 链）——在堆可能已损坏的现场是不可靠动作；预建线程
 * 在安装期完成全部资源创建，崩溃期只做 SetEvent（内核调用，无分配）。
 */
unsigned __stdcall watchdogProc(void* raw) noexcept
{
    CrashCaptureContext* ctx = static_cast<CrashCaptureContext*>(raw);
    for (;;) {
        // 常驻等待布防（auto-reset——每次发射只唤醒一次）。
        if (::WaitForSingleObject(static_cast<HANDLE>(ctx->watchdogArm),
                                  INFINITE) != WAIT_OBJECT_0) {
            return 0;  // 句柄失效（仅析构竞态可达——收编退出）
        }
        if (ctx->shuttingDown.load(std::memory_order_acquire)) {
            return 0;  // 析构收编：假布防（arm 由析构路径触发）——退出
        }
        // 撤防等待：时限内 done 到达＝报告路径正常退出；超时＝挂起——
        // 自我了断（绝不把用户卡死在崩溃中；专用退出码可识别）。
        const DWORD waited = ::WaitForSingleObject(
            static_cast<HANDLE>(ctx->watchdogDone),
            static_cast<DWORD>(ctx->watchdogTimeout.count()));
        if (waited == WAIT_TIMEOUT) {
            ::TerminateProcess(::GetCurrentProcess(), kCrashWatchdogSelfExitCode);
            return 0;  // TerminateProcess 不返回；此行为纵深防御
        }
        // 正常撤防——回到布防等待（gate 一次性保证至多再来一次是 GateClosed，
        // 实际不会再有布防事件；循环为生命周期对称而保留）。
    }
}

// =====================================================================
// 各 OS 回调（薄壳——实现口径 2：取信息→委托发射→按族终结）
// =====================================================================

/**
 * @brief 统一发射出口（全局上下文解析＋类型标签入栈缓冲→emitCrashReportTo）。
 * @return true＝报告已写出（Written）；false＝未写出（未装/降级/闸门闭合）
 */
bool emitGlobalCrash(std::string_view type, std::string_view message) noexcept
{
    CrashCaptureContext* ctx = g_context.load(std::memory_order_acquire);
    if (ctx == nullptr) {
        return false;
    }
    char typeBuffer[kExceptionTypeMaxChars];
    return emitCrashReportTo(*ctx, clampType(typeBuffer, type), message)
           == CrashEmitStatus::Written;
}

/**
 * @brief SEH 过滤器（SetUnhandledExceptionFilter 回调）。
 *
 * 为什么返回 EXCEPTION_EXECUTE_HANDLER：过滤器语义下该返回值＝立即终结
 * 进程（退出码＝异常码）——不走 WER 对话框（SetErrorMode 双保险），无人
 * 值守场景可确定性终结（AT-11"崩溃后重启"的进程侧前提）。返回
 * CONTINUE_SEARCH 则落入系统 WER 流程，弹窗/挂起风险在自动化环境不可控。
 */
LONG WINAPI sehUnhandledFilter(EXCEPTION_POINTERS* info) noexcept
{
    // 取异常码/地址（info 空指针防御——理论上 SEH 保证非空，崩溃路径
    // 不做假设）。
    DWORD code = 0;
    void* address = nullptr;
    if (info != nullptr && info->ExceptionRecord != nullptr) {
        code = info->ExceptionRecord->ExceptionCode;
        address = info->ExceptionRecord->ExceptionAddress;
    }
    char message[kExceptionMessageMaxChars];
    // 栈缓冲 snprintf（崩溃路径零堆分配；截断由 _snprintf 语义保证）。
    _snprintf(message, sizeof(message) - 1,
              "code=0x%08lX address=%p", static_cast<unsigned long>(code), address);
    message[sizeof(message) - 1] = '\0';
    emitGlobalCrash(sehCodeLabel(code), message);
    // 报告完成（或降级）——终结进程（退出码＝异常码，父进程/运维可识别）。
    return EXCEPTION_EXECUTE_HANDLER;
}

/**
 * @brief CRT 信号回调（SIGABRT——abort()/raise 家族；硬件异常不登记信号
 *        处置器，SEH 为其承接面——见 installCrashCapture 注的实测口径）。
 *
 * 信号回调内唯一安全动作序列：报告→恢复默认处置→重发信号（让进程以该
 * 信号的原生语义终结）→_exit 兜底（恢复默认后 raise 理论上必终结；_exit
 * 是异步信号安全退出原语，兜底防"报告写完却挂在不终结"的窗口）。
 */
void crtSignalHandler(int sig) noexcept
{
    const char* label = (sig == SIGABRT) ? "crt-abort" : "crt-signal";
    emitGlobalCrash(label, "crt-termination-signal");
    // 恢复默认处置并重发——进程以原生信号语义终结（避免从信号回调返回
    // 进入未定义的继续执行）。
    std::signal(sig, SIG_DFL);
    std::raise(sig);
    ::_exit(3);  // 兜底（MSVC abort 惯例码 3——不可达时的确定性出口）
}

/**
 * @brief CRT 非法参数回调（_set_invalid_parameter_handler——release CRT
 *        参数校验失败默认静默终结，不装则无报告机会）。
 */
void invalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*,
                             unsigned int, uintptr_t) noexcept
{
    emitGlobalCrash("crt-invalid-parameter", "crt-invalid-parameter");
    ::_exit(3);  // 与 abort 家族同码（CRT 终止族——见 crtSignalHandler 注）
}

/// @brief CRT 纯虚调用回调（抽象函数误调——release 默认 R6025 终结）。
void purecallHandler() noexcept
{
    emitGlobalCrash("crt-purecall", "crt-purecall");
    ::_exit(3);
}

/**
 * @brief std::terminate 钩子（未捕获 C++ 异常——比 abort 路径多一层类型
 *        保真：提取 active exception 的消息摘要进报告）。
 *
 * 提取形态：rethrow＋catch(std::exception)——terminate 语境下这是取当前
 * 异常的标准合法动作；提取失败（非 std 异常/再抛异常）只降级消息文本，
 * 不影响报告写出。报告后 abort()：SIGABRT 回调会因闸门闭合快速返回并
 * _exit——报告不重复、进程确定终结。
 */
void terminateHandler() noexcept
{
    std::string message;
    if (std::current_exception() != nullptr) {
        try {
            throw;  // 取当前异常（terminate 钩子语境合法——实现口径 1）
        } catch (const std::exception& e) {
            try {
                message = e.what();
            } catch (...) {
                message.clear();  // what() 自身失败只降级消息——保守方向
            }
        } catch (...) {
            message = "non-std-exception";
        }
    } else {
        message = "no-active-exception";
    }
    emitGlobalCrash("cpp-terminate", message);
    std::abort();  // 终结（SIGABRT 回调闸门闭合——不产生第二份报告）
}

#endif  // _WIN32

}  // namespace

// =====================================================================
// CrashCaptureContext——生命周期（测试面收编路径）
// =====================================================================

CrashCaptureContext::~CrashCaptureContext()
{
#ifdef _WIN32
    // 收编看门狗：置收编标记→假布防唤醒→等线程退出→关句柄。产品全局
    // 上下文不析构（有意泄漏至进程终局）——本路径仅测试面可达。
    shuttingDown.store(true, std::memory_order_release);
    if (watchdogArm != nullptr) {
        ::SetEvent(static_cast<HANDLE>(watchdogArm));
    }
    if (watchdogThread != nullptr) {
        ::WaitForSingleObject(static_cast<HANDLE>(watchdogThread), 5000);
        ::CloseHandle(static_cast<HANDLE>(watchdogThread));
    }
    if (watchdogArm != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(watchdogArm));
    }
    if (watchdogDone != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(watchdogDone));
    }
#else
    (void)0;  // 非 Windows：无看门狗资源
#endif
}

// =====================================================================
// 工厂与发射核心
// =====================================================================

std::unique_ptr<CrashCaptureContext> makeCrashCaptureContext(
    std::filesystem::path directory, std::string processInfo,
    CrashReportSnapshotSource* source,
    std::chrono::milliseconds watchdogTimeout)
{
    auto ctx = std::make_unique<CrashCaptureContext>();
    ctx->directory = std::move(directory);
    ctx->processInfo = std::move(processInfo);
    ctx->source.store(source, std::memory_order_release);
    ctx->watchdogTimeout = watchdogTimeout;
#ifdef _WIN32
    // 两个 auto-reset 事件（布防/撤防）＋看门狗线程——安装期完成全部资源
    // 创建（崩溃期零创建，见 watchdogProc 注）。创建失败经异常表达（调用
    // 方 fail-fast——测试装配违约不留半可用上下文）。
    ctx->watchdogArm = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ctx->watchdogDone = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (ctx->watchdogArm == nullptr || ctx->watchdogDone == nullptr) {
        throw std::runtime_error("crash-capture: watchdog events unavailable");
    }
    uintptr_t thread = ::_beginthreadex(nullptr, 0, watchdogProc, ctx.get(), 0, nullptr);
    if (thread == 0) {
        throw std::runtime_error("crash-capture: watchdog thread unavailable");
    }
    ctx->watchdogThread = reinterpret_cast<void*>(thread);
#else
    (void)0;  // 非 Windows：看门狗面缺席（安装面同样降态——见 installCrashCapture）
#endif
    // 脱敏服务（§7.7 默认策略——RootOnly/32；failureSink 静默：报告路径
    // 不依赖开发诊断路由，降级字面量即 Writer 侧既有语义）＋Writer（既有
    // 设施——沿用不重写；devLog 传空＝写出失败只走 stderr 降级，崩溃期
    // 不再路由开发诊断——不递归纪律）。
    ctx->redaction = std::make_unique<diagnostics::RedactionService>();
    ctx->writer = std::make_unique<diagnostics::CrashReportWriter>(
        *ctx->redaction, ctx->clock, ctx->fileOps, nullptr);
    return ctx;
}

CrashEmitStatus emitCrashReportTo(CrashCaptureContext& ctx,
                                  std::string_view type,
                                  std::string_view message) noexcept
{
    // ---- 防重入闸门（一次性：首次发射后永不复位——实现口径 3）----
    // 覆盖三种重入形态：过滤器/信号回调递归、报告路径自身的二次崩溃、
    // 多线程同时崩溃。闸门闭合即拒绝——报告路径全局至多一份。
    if (ctx.gate.test_and_set(std::memory_order_acq_rel)) {
        return CrashEmitStatus::GateClosed;
    }

    // ---- 看门狗布防（内核 SetEvent——崩溃期安全动作）----
    if (ctx.watchdogArm != nullptr) {
        ::SetEvent(static_cast<HANDLE>(ctx.watchdogArm));
    }

    CrashEmitStatus status = CrashEmitStatus::WriteFailed;
    // ---- 六段组装（§7.6；取不到的段如实留空——不伪造数据）----
    diagnostics::CrashReportInput input;
    input.processInfo = ctx.processInfo;                       // 第①段（安装期固定）
    input.exceptionType.assign(type.begin(), type.end());      // 第②段·类型
    input.exceptionMessage.assign(message.begin(), message.end());  // 第②段·消息
    // 运行期四段：快照源在场才采集；独立异常隔离面——源实现契约"尽力
    // 而为"，任何异常按**段缺失**处理（已填充的基础三段保留），报告照常
    // 写出（隔离面与写出面分离：若共用一层 try，采集失败会连坐写盘）。
    CrashReportSnapshotSource* source = ctx.source.load(std::memory_order_acquire);
    if (source != nullptr) {
        try {
            CrashContextSegments segments = source->snapshot();
            input.devLogTail = std::move(segments.devLogTail);     // 第③段
            input.activeTasks = std::move(segments.activeTasks);   // 第④段
            input.activeFindings = std::move(segments.activeFindings);  // 第⑤段
            input.openProject = std::move(segments.openProject);   // 第⑥段
        } catch (...) {
            // 快照源失败——段缺失即降态，不影响后续写盘（实现口径 5）。
        }
    }
    try {
        // ---- 写出（既有 Writer——noexcept；失败已 stderr 降级）----
        const std::optional<std::filesystem::path> written =
            ctx.writer->write(input, ctx.directory);
        status = written.has_value() ? CrashEmitStatus::Written
                                     : CrashEmitStatus::WriteFailed;
    } catch (...) {
        // 组装/写出面纵深防御（Writer 自身 noexcept——此层为兜底）：崩溃
        // 路径绝不抛出。
        status = CrashEmitStatus::WriteFailed;
    }

    // ---- 看门狗撤防（报告路径已离开——看门狗回到常驻等待）----
    if (ctx.watchdogDone != nullptr) {
        ::SetEvent(static_cast<HANDLE>(ctx.watchdogDone));
    }
    return status;
}

// =====================================================================
// 产品安装面
// =====================================================================

bool installCrashCapture(std::filesystem::path directory, std::string processInfo,
                         CrashReportSnapshotSource* source,
                         std::chrono::milliseconds watchdogTimeout) noexcept
{
    // 幂等拒绝：捕获是进程级单例——Windows 只保留最后一个 unhandled
    // exception filter，重复安装会无声覆盖前序装配（防两份装配漂移）。
    if (g_context.load(std::memory_order_acquire) != nullptr) {
        return false;
    }
#ifdef _WIN32
    // 全局上下文：静态存储期构造＋有意泄漏（释放语义见 CrashCapture.hpp
    // 类注——崩溃可在 static 析构之后发生，设施必须活到进程终局）。构造
    // 失败（bad_alloc 等）→安装失败但不阻断启动（报告面尽力而为）。
    CrashCaptureContext* ctx = nullptr;
    try {
        ctx = makeCrashCaptureContext(std::move(directory), std::move(processInfo),
                                      source, watchdogTimeout).release();
    } catch (...) {
        return false;  // 设施不可用——启动继续（诊断面尽力而为，不阻塞）
    }

    // ---- CRT 终止族登记（先于 SEH——顺序无关，集中登记便于对照）----
    // 只登记 SIGABRT：abort()/raise 家族是 SEH 不覆盖的 CRT 终止路径。
    // SIGSEGV/SIGILL/SIGFPE **不登记**——实测在位时 CRT 会把硬件异常翻译
    // 成信号回调并抢先于 unhandled exception filter（WP-24-T02 实施期
    // 实证：av 现场走了 crt-sigsegv 而非 SEH 过滤器，丢失异常码/地址细
    // 节）；Windows 上硬件异常的原生且更稳承接面是 SEH——信号面让位，
    // 异常类型保真度更高（seh-access-violation＋code/address 消息）。
    // 登记失败（SIG_ERR）只吞（该族无报告机会，其余面仍在——逐面尽力
    // 而为的安装纪律）。
    std::signal(SIGABRT, crtSignalHandler);
    _set_invalid_parameter_handler(invalidParameterHandler);
    _set_purecall_handler(purecallHandler);
    std::set_terminate(terminateHandler);

    // ---- WER/硬错误对话框抑制（SetErrorMode）----
    // 诊断报告即产品语义的崩溃出口；WER 弹窗会挂起无人值守场景（AT-11
    // "崩溃后重启"要求进程可确定性终结）。SEM_NOGPFAULTERRORBOX 抑制
    // GP 错误框、SEM_FAILCRITICALERRORS 抑制硬错误框——均为进程级错误
    // 呈现模式，不影响其他进程。
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    // ---- SEH 过滤器（最后登记——发布点：此后崩溃进入报告路径）----
    // 原子发布全局上下文在 filter 登记之前——filter 生效瞬间上下文必已
    // 可见（acquire/release 配对，见 g_context 注）。
    g_context.store(ctx, std::memory_order_release);
    ::SetUnhandledExceptionFilter(sehUnhandledFilter);
    return true;
#else
    // 非 Windows：本文件设施的平台面诚实降态（产品目标平台＝Windows——
    // 构建树与交付口径均 Windows；其他平台不留假安装）。
    (void)directory;
    (void)processInfo;
    (void)source;
    (void)watchdogTimeout;
    return false;
#endif
}

void setCrashSnapshotSource(CrashReportSnapshotSource* source) noexcept
{
    // 未安装＝无操作（开发期动态装载通道的宿主进程不装捕获——静默无害）。
    CrashCaptureContext* ctx = g_context.load(std::memory_order_acquire);
    if (ctx != nullptr) {
        ctx->source.store(source, std::memory_order_release);
    }
}

// =====================================================================
// 受控崩溃测试通道（防误触四道闸——见 CrashCapture.hpp 函数注）
// =====================================================================

void triggerCrashTestChannel(const std::string& mode)
{
    if (mode == "av") {
        // 受控空指针写入（volatile 写——优化器不可消除；故意触发 SEH
        // 访问违例→过滤器路径。此处不可达返回：进程在过滤器内终结）。
        volatile int* fault = static_cast<volatile int*>(nullptr);
        *fault = 1;
        // 理论不可达（过滤器返回 EXECUTE_HANDLER 即终结）——兜底 _exit
        // 显性失败，不留"通道静默返回"的假阳性窗口。
        std::fputs("[ird-crash-test] av channel did not terminate process\n", stderr);
        std::fflush(stderr);
        ::_exit(2);
    }
    if (mode == "abort") {
        // CRT abort 族（SIGABRT→信号回调路径）。
        std::raise(SIGABRT);
        std::fputs("[ird-crash-test] abort channel did not terminate process\n", stderr);
        std::fflush(stderr);
        ::_exit(2);
    }
    // 白名单外模式——fail-fast（防误触第②道闸：不落入"未知＝正常启动"）。
    std::fputs("[ird-crash-test] unknown crash-test mode (expected: av|abort)\n",
               stderr);
    std::fflush(stderr);
    ::_exit(2);
}

}  // namespace studio
}  // namespace ui
}  // namespace ird
}  // namespace sdurws
