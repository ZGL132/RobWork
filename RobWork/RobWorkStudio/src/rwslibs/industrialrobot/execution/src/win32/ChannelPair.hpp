/**
 * @file   ChannelPair.hpp
 * @brief  每 worker 一对单向管道的封装——主→worker 命令流、worker→主
 *         数据流（字节模式；有界等待读写，不无限阻塞）。
 *         （units/execution.md §3.5 win32/ChannelPair 行、§6.5 载体、§6.6 管道行。）
 *
 * 设计依据：
 *   - units/execution.md §6.5（载体：每 worker 一对单向管道——主→worker
 *     命令流、worker→主数据流；win32/ChannelPair，字节模式）
 *   - §6.6（管道行：CreateNamedPipeW/CreateFileW〔BYTE 模式〕＋ReadFile/
 *     WriteFile；字节流无消息边界——帧协议自定界；读取超时经 OVERLAPPED
 *     ＋事件（WaitForMultipleObjects 有界等待）——不无限阻塞，EX-WKR-3
 *     卡死检测基础。进程创建行：子进程继承管道句柄；句柄在主进程侧于
 *     启动后关闭继承副本（防泄漏））
 *   - §3.5（win32/ChannelPair.{hpp,cpp}：管道帧读写——长度前缀；读写超时）
 *   - 任务契约 tasks/foundation/EX-T06.json acceptance 4（R-3 处置：句柄
 *     继承/管道行为逐项对照 Microsoft Learn 口径，真进程实证不超诺）
 *
 * 句柄传递纪律（R-3 对照 Microsoft Learn"Creating a Child Process with
 *   Redirected Input and Output"口径的实现落点）：管道服务端由主进程创建
 *   （不可继承），客户端端点由主进程先行打开并经 DuplicateHandle 制出
 *   **可继承副本**，副本句柄值经命令行传给 worker；CreateProcessW（
 *   bInheritHandles=TRUE）后主进程**立即关闭继承副本**（§6.6"防泄漏"
 *   ——closeChildEndDuplicates，acceptance 4 的实证面）。worker 以继承到
 *   的句柄值直接读写（内核对象继承语义：值相同的句柄指向同一管道对象）。
 *
 * 线程模型：写命令仅调度线程调用（命令帧小、缓冲内即完成——有界等待
 *   兜底）；读数据仅该 worker 的读线程调用（挂起的 OVERLAPPED 读＋
 *   {读事件, 进程句柄} 的 WaitForMultipleObjects 有界等待）。两侧互不
 *   接触对方的 API——实例方法按"调度线程写、读线程读"分区，不加锁。
 */

#ifndef SDURWS_IRD_EXECUTION_WIN32_CHANNELPAIR_HPP
#define SDURWS_IRD_EXECUTION_WIN32_CHANNELPAIR_HPP

// CancelIoEx 等 Vista+ API 需要 NT6 目标宏（框架全局定义 0x0501——在此处
// 提升，只影响本单元 win32 私有头的编译面；运行环境为 Win10+，见 §1.4）。
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sdurws::ird::execution::win32 {

/**
 * @brief 一对单向管道（构造即建好全部四个端点中的三个：两个服务端＋
 *        两个客户端；客户端的可继承副本随 launch 关闭）。
 */
class ChannelPair {
public:
    /// worker 侧继承句柄值（经命令行传递；0＝无效）。
    struct ChildHandleValues {
        std::uintptr_t cmdRead = 0;   ///< 命令流读端（worker 读主进程命令）
        std::uintptr_t dataWrite = 0; ///< 数据流写端（worker 写回数据）
    };

    /**
     * @brief 建立一对管道。
     *
     * @param baseName [in] 管道名基底（须全局唯一——调用方以
     *                 "ird-exec-<主进程 pid>-<worker 序号>" 形态供给；
     *                 实际名＝\\.\pipe\<baseName>-cmd / -data）
     * @return 实例；nullptr＝任一步骤失败（错误码经 lastError()——启动
     *         失败面，WorkerLaunchFailed 的来源之一）
     */
    static std::unique_ptr<ChannelPair> create(const std::wstring& baseName);

    /// 析构：关闭主进程侧全部端点（worker 已死/将死时管道断裂——读侧
    /// 以 broken() 观察，不视为错误）。
    ~ChannelPair();

    ChannelPair(const ChannelPair&) = delete;
    ChannelPair& operator=(const ChannelPair&) = delete;

    /// worker 侧继承句柄值（launch 组装命令行用；本值在
    /// closeChildEndDuplicates 后对主进程已失效——仅供命令行拼装）。
    ChildHandleValues childHandleValues() const noexcept { return m_childValues; }

    /**
     * @brief 关闭主进程侧持有的可继承副本（ProcessLauncher 在
     *        CreateProcessW 成功后立即调用——§6.6"句柄在主进程侧于启动
     *        后关闭继承副本（防泄漏）"；acceptance 4 R-3 的实证面）。
     *
     * 调用后 hasOpenChildEndDuplicates() 恒 false；重复调用无害（RAII 幂等）。
     */
    void closeChildEndDuplicates() noexcept;

    /// 启动后是否仍有未关闭的继承副本（false＝防泄漏纪律已履行——
    /// 契约测试断言面）。
    bool hasOpenChildEndDuplicates() const noexcept
    {
        return m_cmdChildDup != nullptr || m_dataChildDup != nullptr;
    }

    // ---- 命令流（主→worker 写；仅调度线程） ----

    /**
     * @brief 写一段命令字节（有界等待—— OVERLAPPED 写＋事件等待；
     *        命令帧尺寸远小于管道缓冲，正常路径立即完成）。
     *
     * @param data      [in] 字节段
     * @param size      [in] 字节数（>0）
     * @param timeoutMs [in] 有界等待上限（单位 ms）
     * @return true＝写入完成；false＝超时/管道断裂/系统错误（调用方按
     *         通道失能处置——worker 无响应正是强杀路径的前兆）
     */
    bool writeCommand(const std::uint8_t* data, std::size_t size, DWORD timeoutMs);

    // ---- 数据流（worker→主 读；仅该 worker 的读线程） ----

    /// 等待结论（waitFor 的返回语义）。
    enum class WaitResult {
        DataReady,     ///< 有数据到达——经 takeReadBytes 取走
        ProcessExited, ///< worker 进程句柄已 signaled（drainAfterExit 收尾）
        Timeout,       ///< 有界等待窗口耗尽（继续轮询——不视为异常）
        Broken,        ///< 管道断裂（worker 写端已关且无残留数据）
        Error,         ///< 系统错误（GetLastError 经 lastError()）
    };

    /**
     * @brief 投递首个挂起读（构造后调用一次；此后 waitFor 循环内自动续投）。
     * @return false＝投递即失败（管道已断裂/系统错误）
     */
    bool beginRead();

    /**
     * @brief 有界等待：挂起读完成（数据）或进程退出或超时（§6.6"读取
     *        超时经 OVERLAPPED＋事件（WaitForMultipleObjects 有界等待）
     *        ——不无限阻塞"的本体）。
     *
     * @param processHandle [in] worker 进程句柄（nullptr＝不等待进程）
     * @param timeoutMs     [in] 有界等待上限（单位 ms；读线程以小片超时
     *                      轮询保证退出响应性）
     */
    WaitResult waitFor(HANDLE processHandle, DWORD timeoutMs);

    /// 取走本次完成读的字节（DataReady 后调用；返回取到的字节数）。
    std::size_t takeReadBytes(std::vector<std::uint8_t>* out);

    /// 续投挂起读（DataReady 消费后调用——读循环的下一拍等待基础）。
    bool repostRead();

    /**
     * @brief 进程退出后的残留数据收尾（读线程在 ProcessExited 后调用）：
     *        反复收割已完成的挂起读直至断裂，返回期间读到的字节。
     */
    std::vector<std::uint8_t> drainAfterExit();

    /// 数据管道是否已断裂（worker 写端关闭——正常退出与崩溃都会走到）。
    bool broken() const noexcept { return m_broken; }

    /// 最近一次 Win32 错误码（诊断承载）。
    DWORD lastError() const noexcept { return m_lastError; }

private:
    ChannelPair() = default;

    HANDLE m_cmdServer = nullptr;   ///< 命令流服务端（主进程写；OUTBOUND）
    HANDLE m_dataServer = nullptr;  ///< 数据流服务端（主进程读；INBOUND，OVERLAPPED）
    HANDLE m_cmdChildDup = nullptr; ///< 命令流客户端可继承副本（launch 后即关）
    HANDLE m_dataChildDup = nullptr;///< 数据流客户端可继承副本（launch 后即关）
    ChildHandleValues m_childValues{};  ///< 副本句柄值（命令行拼装用）

    OVERLAPPED m_readOverlapped{};  ///< 挂起读的 OVERLAPPED（事件归其所有）
    std::vector<std::uint8_t> m_readBuffer;  ///< 挂起读的目标缓冲
    std::size_t m_readAvailable = 0;         ///< 上次完成读的字节数
    bool m_readPending = false;              ///< 是否有挂起读在途
    bool m_broken = false;                   ///< 数据管道断裂闩
    DWORD m_lastError = 0;                   ///< 最近 Win32 错误码
};

}  // namespace sdurws::ird::execution::win32

#endif  // SDURWS_IRD_EXECUTION_WIN32_CHANNELPAIR_HPP
