/**
 * @file   ChannelPair.cpp
 * @brief  管道对实现（契约见 ChannelPair.hpp；§6.6 管道行口径逐 API 对照
 *         Microsoft Learn：Named Pipes / Synchronous and Overlapped Input
 *         and Output）。
 */

#include "ChannelPair.hpp"

#include <cstring>
#include <string>

namespace sdurws::ird::execution::win32 {

namespace {

/// 单次挂起读的字节量（本地管道的合理片大小——帧装配器在协议层增量消化，
/// 本值只影响读线程的唤醒粒度）。
constexpr DWORD kReadChunkBytes = 64 * 1024;

/// 生成完整管道名（\\.\pipe\<baseName>-cmd / -data）。
std::wstring pipeName(const std::wstring& baseName, const wchar_t* suffix)
{
    return L"\\\\.\\pipe\\" + baseName + suffix;
}

}  // namespace

std::unique_ptr<ChannelPair> ChannelPair::create(const std::wstring& baseName)
{
    std::unique_ptr<ChannelPair> pair(new ChannelPair());

    // ---- 第 1 步：命令流服务端（主进程写端；OUTBOUND 单向、字节模式、
    // 不可继承——服务端从不离开主进程）。 ----
    pair->m_cmdServer = ::CreateNamedPipeW(
        pipeName(baseName, L"-cmd").c_str(),
        PIPE_ACCESS_OUTBOUND,                       // 主进程只写
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,  // 字节模式（§6.5）
        1,                                          // 单实例——管道随 worker 一对一
        64 * 1024, 64 * 1024,                       // 收发缓冲（本地管道，取常用值）
        0, nullptr);                                // 默认超时/SD（安全面＝离线单机，NFR-DEP-03）
    if (pair->m_cmdServer == INVALID_HANDLE_VALUE) {
        pair->m_cmdServer = nullptr;
        pair->m_lastError = ::GetLastError();
        return nullptr;
    }

    // ---- 第 2 步：数据流服务端（主进程读端；INBOUND、FILE_FLAG_OVERLAPPED
    // ——有界等待读的机制前提，§6.6"读取超时经 OVERLAPPED＋事件"）。 ----
    pair->m_dataServer = ::CreateNamedPipeW(
        pipeName(baseName, L"-data").c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, // 主进程只读＋重叠 I/O
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 64 * 1024, 64 * 1024, 0, nullptr);
    if (pair->m_dataServer == INVALID_HANDLE_VALUE) {
        pair->m_dataServer = nullptr;
        pair->m_lastError = ::GetLastError();
        return nullptr;
    }

    // ---- 第 3 步：主进程预先打开两个客户端端点（CreateFileW——连接即
    // 完成；worker 稍后经继承句柄直连同一管道对象，无需再按名打开——
    // §6.6"子进程继承管道句柄"的口径）。 ----
    HANDLE cmdClient = ::CreateFileW(
        pipeName(baseName, L"-cmd").c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, 0, nullptr);
    if (cmdClient == INVALID_HANDLE_VALUE) {
        cmdClient = nullptr;
        pair->m_lastError = ::GetLastError();
        return nullptr;
    }
    HANDLE dataClient = ::CreateFileW(
        pipeName(baseName, L"-data").c_str(), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, 0, nullptr);
    if (dataClient == INVALID_HANDLE_VALUE) {
        dataClient = nullptr;
        ::CloseHandle(cmdClient);
        pair->m_lastError = ::GetLastError();
        return nullptr;
    }

    // ---- 第 4 步：为两个客户端端点各制一份**可继承副本**（DuplicateHandle
    // ——Microsoft Learn：句柄继承以 bInheritHandle=TRUE 的副本为粒度；
    // 原始客户端端点留在主进程会立即被关闭，副本的句柄值经命令行传给
    // worker）。只把这两份副本标为可继承＝继承面最小化（worker 不夹带
    // 其他任何句柄）。 ----
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    inheritable.lpSecurityDescriptor = nullptr;
    const HANDLE self = ::GetCurrentProcess();
    if (!::DuplicateHandle(self, cmdClient, self, &pair->m_cmdChildDup, 0, TRUE,
                           DUPLICATE_SAME_ACCESS)
        || !::DuplicateHandle(self, dataClient, self, &pair->m_dataChildDup, 0, TRUE,
                              DUPLICATE_SAME_ACCESS)) {
        pair->m_lastError = ::GetLastError();
        ::CloseHandle(cmdClient);
        ::CloseHandle(dataClient);
        return nullptr;
    }
    ::CloseHandle(cmdClient);   // 原始客户端端点使命完成（主进程不用它）
    ::CloseHandle(dataClient);  // （数据读面在服务端；写面由 worker 经副本持有）
    pair->m_childValues.cmdRead = reinterpret_cast<std::uintptr_t>(pair->m_cmdChildDup);
    pair->m_childValues.dataWrite = reinterpret_cast<std::uintptr_t>(pair->m_dataChildDup);

    // ---- 第 5 步：读缓冲与 OVERLAPPED 就绪（首个挂起读由 beginRead 投递）。 ----
    pair->m_readBuffer.resize(kReadChunkBytes);
    pair->m_readOverlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (pair->m_readOverlapped.hEvent == nullptr) {
        pair->m_lastError = ::GetLastError();
        return nullptr;
    }
    return pair;
}

ChannelPair::~ChannelPair()
{
    // 关闭顺序无关紧要（worker 侧的对应端随进程终止或已关闭——管道断裂
    // 是其正常归宿）。挂起读若仍在途，服务端句柄关闭使其完成/失败。
    if (m_readOverlapped.hEvent != nullptr) {
        if (m_readPending && m_dataServer != nullptr) {
            ::CancelIoEx(m_dataServer, &m_readOverlapped);
        }
        ::CloseHandle(m_readOverlapped.hEvent);
    }
    closeChildEndDuplicates();
    if (m_cmdServer != nullptr) {
        ::CloseHandle(m_cmdServer);
    }
    if (m_dataServer != nullptr) {
        ::CloseHandle(m_dataServer);
    }
}

void ChannelPair::closeChildEndDuplicates() noexcept
{
    // §6.6"句柄在主进程侧于启动后关闭继承副本（防泄漏）"的本体：两个
    // 副本只存在于"CreateProcessW 抓取继承面"的窗口内（RAII 幂等）。
    if (m_cmdChildDup != nullptr) {
        ::CloseHandle(m_cmdChildDup);
        m_cmdChildDup = nullptr;
    }
    if (m_dataChildDup != nullptr) {
        ::CloseHandle(m_dataChildDup);
        m_dataChildDup = nullptr;
    }
}

bool ChannelPair::writeCommand(const std::uint8_t* data, std::size_t size, DWORD timeoutMs)
{
    if (m_cmdServer == nullptr || data == nullptr || size == 0) {
        return false;
    }
    OVERLAPPED ov{};
    ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) {
        m_lastError = ::GetLastError();
        return false;
    }
    const BOOL ok = ::WriteFile(m_cmdServer, data, static_cast<DWORD>(size), nullptr, &ov);
    if (!ok) {
        const DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING) {
            // 立即失败＝管道断裂（worker 已死）或系统错误。
            m_lastError = err;
            ::CloseHandle(ov.hEvent);
            return false;
        }
        // 有界等待：命令流不设无限等待（worker 卡死时写命令必须能超时
        // 返回——调度线程不可被拖死；§6.6 管道行"不无限阻塞"对两个方向
        // 同样成立）。
        const DWORD wait = ::WaitForSingleObject(ov.hEvent, timeoutMs);
        if (wait != WAIT_OBJECT_0) {
            if (wait == WAIT_TIMEOUT) {
                ::CancelIoEx(m_cmdServer, &ov);
            }
            m_lastError = (wait == WAIT_TIMEOUT) ? WAIT_TIMEOUT : ::GetLastError();
            ::CloseHandle(ov.hEvent);
            return false;
        }
    }
    DWORD written = 0;
    const BOOL done = ::GetOverlappedResult(m_cmdServer, &ov, &written, FALSE);
    ::CloseHandle(ov.hEvent);
    if (!done || written != size) {
        m_lastError = done ? ERROR_IO_INCOMPLETE : ::GetLastError();
        return false;
    }
    return true;
}

bool ChannelPair::beginRead()
{
    if (m_dataServer == nullptr || m_readPending) {
        return !m_broken;
    }
    if (m_broken) {
        return false;
    }
    // 投递挂起读（OVERLAPPED——完成时置事件；§6.6 管道行口径）。
    const BOOL ok = ::ReadFile(m_dataServer, m_readBuffer.data(),
                               static_cast<DWORD>(m_readBuffer.size()), nullptr,
                               &m_readOverlapped);
    if (ok) {
        // 同步完成（极小概率——管道里已有数据）：按完成处理。
        DWORD got = 0;
        if (::GetOverlappedResult(m_dataServer, &m_readOverlapped, &got, FALSE)) {
            m_readAvailable = got;
            return true;
        }
    } else {
        const DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING) {
            m_readPending = true;
            return true;
        }
        if (err == ERROR_BROKEN_PIPE) {
            m_broken = true;  // worker 写端已关且无数据
            return false;
        }
        m_lastError = err;
        return false;
    }
    return false;
}

bool ChannelPair::repostRead()
{
    // 续投前复位事件与在途标记（挂起读的生命周期＝一投一收）。
    m_readPending = false;
    m_readAvailable = 0;
    ::ResetEvent(m_readOverlapped.hEvent);
    return beginRead();
}

ChannelPair::WaitResult ChannelPair::waitFor(HANDLE processHandle, DWORD timeoutMs)
{
    if (m_broken) {
        return WaitResult::Broken;
    }
    // 等待集：{读事件, 进程句柄}——数据到达与进程退出双源唤醒（§6.6
    // "WaitForMultipleObjects 有界等待"）。进程句柄可空（纯数据等待）。
    HANDLE waits[2] = {m_readOverlapped.hEvent, processHandle};
    const DWORD count = (processHandle != nullptr) ? 2 : 1;
    const DWORD wait = ::WaitForMultipleObjects(count, waits, FALSE, timeoutMs);
    if (wait == WAIT_OBJECT_0) {
        // 读完成：收割字节数；0 字节＋成功＝对端关闭（字节管道的 EOF 形态）。
        DWORD got = 0;
        if (!::GetOverlappedResult(m_dataServer, &m_readOverlapped, &got, FALSE)) {
            const DWORD err = ::GetLastError();
            m_readPending = false;
            if (err == ERROR_BROKEN_PIPE) {
                m_broken = true;
                return WaitResult::Broken;
            }
            m_lastError = err;
            return WaitResult::Error;
        }
        m_readPending = false;
        m_readAvailable = got;
        if (got == 0) {
            m_broken = true;
            return WaitResult::Broken;
        }
        return WaitResult::DataReady;
    }
    if (processHandle != nullptr && wait == WAIT_OBJECT_0 + 1) {
        return WaitResult::ProcessExited;
    }
    if (wait == WAIT_TIMEOUT) {
        return WaitResult::Timeout;
    }
    m_lastError = ::GetLastError();
    return WaitResult::Error;
}

std::size_t ChannelPair::takeReadBytes(std::vector<std::uint8_t>* out)
{
    const std::size_t n = m_readAvailable;
    if (out != nullptr && n > 0) {
        out->insert(out->end(), m_readBuffer.begin(),
                    m_readBuffer.begin() + static_cast<std::ptrdiff_t>(n));
    }
    m_readAvailable = 0;
    return n;
}

std::vector<std::uint8_t> ChannelPair::drainAfterExit()
{
    // 进程退出后写端关闭：残留数据只收"挂起读的完成量"。不追发同步读——
    // 若 worker 泄漏了写端句柄给第三方进程，同步 ReadFile 会无限阻塞
    // （R-3 纪律：不凭记忆超诺——§6.6 只承诺"读取不无限阻塞"，对退出后
    // 的收尾同样成立；退出后的残余批次完整性不在承诺面，任务已按退出
    // 分类终结）。
    std::vector<std::uint8_t> drained;
    if (m_broken) {
        return drained;
    }
    if (m_readPending) {
        // 挂起读等待其完成（进程已死——完成必然迫近；仍设 5 s 上限作
        // 系统异常兜底，超时取消读并置断裂）。
        const DWORD wait = ::WaitForSingleObject(m_readOverlapped.hEvent, 5000);
        if (wait == WAIT_OBJECT_0) {
            DWORD got = 0;
            if (::GetOverlappedResult(m_dataServer, &m_readOverlapped, &got, FALSE)) {
                drained.insert(drained.end(), m_readBuffer.begin(),
                               m_readBuffer.begin() + static_cast<std::ptrdiff_t>(got));
            }
            m_readPending = false;
            m_readAvailable = 0;
        } else {
            ::CancelIoEx(m_dataServer, &m_readOverlapped);
            m_readPending = false;
        }
    }
    m_broken = true;
    return drained;
}

}  // namespace sdurws::ird::execution::win32
