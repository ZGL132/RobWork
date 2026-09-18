/**
 * @file   ProcessLauncher.cpp
 * @brief  worker 进程启动器实现（契约见 ProcessLauncher.hpp；§6.6 进程
 *         创建行口径对照 Microsoft Learn：Creating a Child Process with
 *         Redirected Input and Output / Process and Thread Objects）。
 */

#include "ProcessLauncher.hpp"

#include <sstream>

#include "ChannelPair.hpp"

namespace sdurws::ird::execution::win32 {

bool RealProcessOps::createProcess(const std::wstring& commandLine, bool inheritHandles,
                                   PROCESS_INFORMATION* pi)
{
    // CreateProcessW 文档口径（§6.6 进程创建行）：
    //   - CREATE_SUSPENDED：主线程挂起启动——Job assign 必须发生在子进程
    //     可能派生子进程之前（竞态消除，见 ProcessLauncher.hpp 时序）；
    //   - CREATE_NO_WINDOW：worker 不产生任何控制台窗口（§6.4 边界规则
    //     "无窗口、无控制台输出到 UI"的 OS 面）；
    //   - lpEnvironment/lpCurrentDirectory 缺省：继承主进程环境（离线单机
    //     部署形态，NFR-DEP-02）；
    //   - 命令行缓冲可变：文档允许 CreateProcessW 改写——传非 const 缓冲。
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    std::wstring mutableCmd = commandLine;
    const BOOL ok = ::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                                     inheritHandles ? TRUE : FALSE,
                                     CREATE_SUSPENDED | CREATE_NO_WINDOW,
                                     nullptr, nullptr, &si, pi);
    if (!ok) {
        m_lastError = ::GetLastError();
        return false;
    }
    // si 持有的两个标准句柄在非继承场景为空；继承场景亦无需保留（管道
    // 端点经命令行副本传递）——按文档要求在不再需要时关闭。
    if (si.hStdInput != nullptr) {
        ::CloseHandle(si.hStdInput);
    }
    if (si.hStdOutput != nullptr) {
        ::CloseHandle(si.hStdOutput);
    }
    if (si.hStdError != nullptr) {
        ::CloseHandle(si.hStdError);
    }
    return true;
}

LaunchResult launchWorkerProcess(const std::wstring& executablePath,
                                 const std::wstring& channelName,
                                 const std::wstring& identityArg, IProcessOps& ops,
                                 JobScope& job)
{
    LaunchResult result;
    if (!job.valid()) {
        result.lastError = job.lastError() != 0 ? job.lastError() : ERROR_INVALID_HANDLE;
        return result;
    }

    // ---- 第 1 步：建管道对（两服务端＋两可继承副本——副本值随命令行
    // 传递；见 ChannelPair.hpp 句柄传递纪律）。失败＝启动失败面（管道名
    // 冲突/权限等——错误码取通用面，诊断文本由 WorkerSupervisor 补充
    // 管道名上下文定位）。 ----
    result.channel = ChannelPair::create(channelName);
    if (result.channel == nullptr
        || result.channel->childHandleValues().cmdRead == 0
        || result.channel->childHandleValues().dataWrite == 0) {
        result.lastError = ERROR_FILE_NOT_FOUND;
        return result;
    }

    // ---- 第 2 步：拼命令行（句柄值十六进制文本——worker 宿主解析回
    // HANDLE；绑定五元组五个规范文本以 '|' 连接——握手帧的身份块来源。
    // 命令行形态是主/worker 的私有约定，worker exe 同库实现）。 ----
    const ChannelPair::ChildHandleValues handles = result.channel->childHandleValues();
    std::wostringstream cmd;
    cmd << L'"' << executablePath << L"\" --ird-worker --cmd-h 0x" << std::hex
        << handles.cmdRead << L" --data-h 0x" << handles.dataWrite << L" --identity "
        << identityArg;

    // ---- 第 3 步：创建子进程（挂起＋继承——继承面恰为两份管道副本，
    // IProcessOps 接缝承载真实 OS 调用/测试注入）。 ----
    PROCESS_INFORMATION pi{};
    if (!ops.createProcess(cmd.str(), /*inheritHandles=*/true, &pi)) {
        result.lastError = ops.lastError();
        return result;
    }

    // ---- 第 4 步：纳入作业（必须在 Resume 前——孤儿防护无窗口；§6.6
    // 进程树管理行）。 ----
    if (!job.assign(pi.hProcess)) {
        result.lastError = job.lastError();
        // 纳入失败＝启动失败（不留一个不受管的 worker 进程——挂起进程
        // 尚未运行，直接终结即无副作用）。
        ::TerminateProcess(pi.hProcess, static_cast<UINT>(-1));
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        return result;
    }

    // ---- 第 5 步：恢复主线程运行（worker 宿主开始执行握手）。 ----
    ::ResumeThread(pi.hThread);
    ::CloseHandle(pi.hThread);  // 主线程句柄使命完成（存活观测只用进程句柄）

    // ---- 第 6 步：关闭主进程侧继承副本（§6.6 防泄漏承诺；此后管道两端
    // 归属清晰——服务端在主进程，客户端在 worker）。 ----
    result.channel->closeChildEndDuplicates();

    result.ok = true;
    result.pid = pi.dwProcessId;
    result.processHandle = pi.hProcess;
    return result;
}

}  // namespace sdurws::ird::execution::win32
