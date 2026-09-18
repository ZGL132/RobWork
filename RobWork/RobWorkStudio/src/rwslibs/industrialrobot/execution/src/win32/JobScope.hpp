/**
 * @file   JobScope.hpp
 * @brief  Windows 作业对象 RAII（Job Scope）——每 worker 一棵受管进程树
 *         （KILL_ON_JOB_CLOSE），强杀与"主进程崩溃不留孤儿 worker"的
 *         承载（units/execution.md §6.6、§3.5 win32/JobScope 行）。
 *
 * 设计依据：
 *   - units/execution.md §6.6（进程树管理行：CreateJobObjectW＋
 *     AssignProcessToJobObject（JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE）——
 *     "作业对象可容纳进程树；KILL_ON_JOB_CLOSE：与作业关联的最后一个
 *     句柄关闭时终止所有进程"〔Microsoft Learn：Job Objects〕；使用承诺：
 *     每 worker 独立 Job Scope，主进程崩溃→OS 关闭句柄→worker 树随之
 *     终止——不存在孤儿 worker）、强制终止行（TerminateJobObject 整树）
 *   - §3.5（win32/JobScope.{hpp,cpp}：作业对象 RAII——强杀进程树）
 *   - §10.3（terminateForce 语义：TerminateJobObject；句柄关闭＋reap）
 *   - 任务契约 tasks/foundation/EX-T06.json acceptance 1（EX-WKR-2 崩溃
 *     隔离——D-02 Job Scope 进程侧闭环）/4（R-3 处置：强杀时序逐项对照
 *     Microsoft Learn 口径，真进程实证不超诺）
 *
 * 背景说明（为什么不超诺——§6.6 纪律）：本类只承诺 Microsoft Learn 明文
 *   的两个语义——①作业容纳进程树（worker 自己派生的任何子进程同在作业
 *   内，除非它显式 breakaway——阶段 A 不承诺对 breakaway 的防御）；②
 *   KILL_ON_JOB_CLOSE：最后一个作业句柄关闭时终止所有关联进程。断电/
 *   强杀的中间态（进程是否收到 DLL 分离通知等）**不作超出文档的承诺**
 *   ——TerminateJobObject/TerminateProcess 文档明示不保证 DLL/资源清理
 *   （§6.6 强制终止行），worker 侧因此不依赖任何退出清理。
 *
 * 线程安全：实例非线程安全——一个 JobScope 归一个 worker 监督记录持有，
 *   仅调度线程与该 worker 的读线程按"terminateForce 恰一次"约定操作
 *   （§10.3；并发防线在 WorkerSupervisor）。
 */

#ifndef SDURWS_IRD_EXECUTION_WIN32_JOBSCOPE_HPP
#define SDURWS_IRD_EXECUTION_WIN32_JOBSCOPE_HPP

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

namespace sdurws::ird::execution::win32 {

/**
 * @brief 作业对象 RAII（每 worker 一棵进程树的强杀与孤儿防护边界）。
 *
 * 生命周期：构造即创建作业对象并设置 KILL_ON_JOB_CLOSE；assign 把 worker
 *   主进程纳入（须在 worker 首次可能派生子进程之前——ProcessLauncher 以
 *   CREATE_SUSPENDED 启动、先 assign 后 Resume 消除竞态窗口）；析构关闭
 *   句柄——若树中仍有存活进程，KILL_ON_JOB_CLOSE 兜杀（文档语义，§6.6）。
 */
class JobScope {
public:
    /// 创建作业对象（失败 valid()==false——启动路径按 WorkerLaunchFailed
    /// 处置；错误码经 lastError() 供开发诊断）。
    JobScope();

    /// 析构：关闭作业句柄（KILL_ON_JOB_CLOSE 兜杀残留进程——不留孤儿）。
    ~JobScope();

    JobScope(const JobScope&) = delete;
    JobScope& operator=(const JobScope&) = delete;

    /// 作业对象是否创建成功。
    bool valid() const noexcept { return m_job != nullptr; }

    /// 创建失败的 Win32 错误码（valid()==false 时有义；开发诊断承载）。
    DWORD lastError() const noexcept { return m_lastError; }

    /// 把进程纳入作业（AssignProcessToJobObject；进程须未终止。返回
    /// false＝纳入失败——启动失败面，进程由调用方终结）。
    bool assign(HANDLE process) noexcept;

    /**
     * @brief 强制终止整棵进程树（TerminateJobObject——§6.6 强制终止行：
     *        仅用于超时与用户强杀路径；异步语义——终止请求已发出不等于
     *        进程句柄已 signaled，调用方以进程句柄等待为准，不凭时序
     *        断言已死〔R-3：不凭记忆超诺〕）。
     *
     * @param exitCode [in] 树内进程的退出码（强杀路径统一取
     *                  kForceTerminateExitCode——分类面据此知"由监督方
     *                  强杀"，而非 worker 自身崩溃）
     * @return false＝请求失败（作业句柄无效等——调用方按已终止处理并
     *         记开发诊断；KILL_ON_JOB_CLOSE 在句柄关闭时兜底）
     */
    bool terminate(UINT exitCode) noexcept;

    /// 作业句柄（诊断/测试观测面；调用方不得关闭）。
    HANDLE handle() const noexcept { return m_job; }

private:
    HANDLE m_job = nullptr;   ///< 作业对象句柄（RAII 唯一所有）
    DWORD m_lastError = 0;    ///< 创建/操作失败的 Win32 错误码
};

/// 监督方强杀路径的统一退出码（非 §6.4 worker 约定集内的任意值即可——
/// 取 0xDEAD10CE 便于日志辨识；分类面以"监督方 forceKill 标记"为准，
/// 退出码仅日志辅助）。
inline constexpr UINT kForceTerminateExitCode = 0xDEAD10CEu;

}  // namespace sdurws::ird::execution::win32

#endif  // SDURWS_IRD_EXECUTION_WIN32_JOBSCOPE_HPP
