/**
 * @file   JobScope.cpp
 * @brief  作业对象 RAII 实现（契约见 JobScope.hpp；§6.6 口径逐 API 对照
 *         Microsoft Learn：Job Objects / Process and Thread Objects）。
 */

#include "JobScope.hpp"

namespace sdurws::ird::execution::win32 {

JobScope::JobScope()
{
    // CreateJobObjectW：创建作业对象（无名字——进程内唯一性由指针身份
    // 承担，不需跨进程共享；Microsoft Learn：Job Objects）。
    m_job = ::CreateJobObjectW(nullptr, nullptr);
    if (m_job == nullptr) {
        m_lastError = ::GetLastError();
        return;
    }
    // KILL_ON_JOB_CLOSE：最后一个作业句柄关闭时终止所有关联进程——
    // "主进程崩溃→OS 回收句柄→worker 树随之终止"（不存在孤儿 worker，
    // §6.6 进程树管理行）的机制本体。JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    // 的 BasicLimitInformation.LimitFlags 承载该标志。
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(m_job, JobObjectExtendedLimitInformation,
                                   &info, sizeof(info))) {
        m_lastError = ::GetLastError();
        ::CloseHandle(m_job);
        m_job = nullptr;
    }
}

JobScope::~JobScope()
{
    // 句柄关闭即 KILL_ON_JOB_CLOSE 生效点：残留进程被 OS 兜杀（文档语义
    // ——不在析构里另发 TerminateJobObject，避免双杀语义混叠；显式强杀
    // 走 terminate()）。
    if (m_job != nullptr) {
        ::CloseHandle(m_job);
    }
}

bool JobScope::assign(HANDLE process) noexcept
{
    // AssignProcessToJobObject：把进程纳入作业树（含其此后派生的子进程，
    // 除非 breakaway——阶段 A 不承诺防御，见头注）。须在子进程恢复运行
    // 前调用（CREATE_SUSPENDED→assign→Resume 的时序由 ProcessLauncher
    // 保证，消除"子进程先派生孙进程"的竞态窗口）。
    if (m_job == nullptr || process == nullptr) {
        return false;
    }
    if (!::AssignProcessToJobObject(m_job, process)) {
        m_lastError = ::GetLastError();
        return false;
    }
    return true;
}

bool JobScope::terminate(UINT exitCode) noexcept
{
    // TerminateJobObject：整树终止（§6.6 强制终止行——仅超时/用户强杀
    // 路径使用；异步语义，调用方以进程句柄等待为准）。
    if (m_job == nullptr) {
        return false;
    }
    if (!::TerminateJobObject(m_job, exitCode)) {
        m_lastError = ::GetLastError();
        return false;
    }
    return true;
}

}  // namespace sdurws::ird::execution::win32
