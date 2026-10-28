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

bool RealProcessOps::createProcess(const std::wstring& commandLine,
                                   const std::vector<HANDLE>& inheritedHandles,
                                   PROCESS_INFORMATION* pi)
{
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    SIZE_T size = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!::InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) {
        m_lastError = ::GetLastError();
        return false;
    }
    // EXR-11：即使宿主另有可继承句柄，子进程也只能取得本任务的两份副本。
    const BOOL configured = ::UpdateProcThreadAttribute(
        startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(inheritedHandles.data()), inheritedHandles.size() * sizeof(HANDLE),
        nullptr, nullptr);
    if (!configured) {
        m_lastError = ::GetLastError();
        ::DeleteProcThreadAttributeList(startup.lpAttributeList);
        return false;
    }
    std::wstring mutableCmd = commandLine;
    const BOOL ok = ::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, nullptr, &startup.StartupInfo, pi);
    m_lastError = ok ? ERROR_SUCCESS : ::GetLastError();
    ::DeleteProcThreadAttributeList(startup.lpAttributeList);
    return ok != FALSE;
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
    if (!ops.createProcess(cmd.str(), {reinterpret_cast<HANDLE>(handles.cmdRead),
                                      reinterpret_cast<HANDLE>(handles.dataWrite)}, &pi)) {
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
