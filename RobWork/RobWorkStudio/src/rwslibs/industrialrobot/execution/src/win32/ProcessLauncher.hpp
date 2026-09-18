/**
 * @file   ProcessLauncher.hpp
 * @brief  worker 进程启动器——CreateProcessW（继承管道句柄、CREATE_NO_WINDOW、
 *         CREATE_SUSPENDED→Job assign→Resume 时序）＋ IProcessOps 故障注入
 *         接缝（units/execution.md §3.5 win32/ProcessLauncher 行、§6.6 进程
 *         创建行）。
 *
 * 设计依据：
 *   - units/execution.md §6.6（进程创建行：CreateProcessW（子进程继承管道
 *     句柄；CREATE_NO_WINDOW）——子进程继承可继承句柄；创建即返回。使用
 *     承诺：worker 启动；句柄在主进程侧于启动后关闭继承副本（防泄漏）。
 *     进程树管理行：AssignProcessToJobObject 的时序前提）
 *   - §3.5（win32/ProcessLauncher.{hpp,cpp}：CreateProcessW＋Job Object；
 *     IProcessOps 接缝（fault 注入））
 *   - §6.4（worker 生命周期：launch→握手→DispatchRequest→运行→终结）
 *   - 任务契约 tasks/foundation/EX-T06.json acceptance 4（R-3 处置：句柄
 *     继承等 Windows 进程行为逐项对照 Microsoft Learn 口径，真进程/注入
 *     用例实证不超诺）
 *
 * 启动时序（消除两处竞态的设计）：
 *   1. ChannelPair::create——管道三端就绪＋两份可继承副本；
 *   2. CreateProcessW（CREATE_SUSPENDED | CREATE_NO_WINDOW，
 *      bInheritHandles=TRUE）——子进程已存在但主线程挂起：继承面（恰两
 *      个管道端副本）此刻被固定；
 *   3. JobScope::assign——子进程纳入作业（必须在 Resume 前——否则子进程
 *      可能在纳入前派生不受管的孙进程，孤儿防护出现窗口）；
 *   4. ResumeThread——子进程开始运行；
 *   5. closeChildEndDuplicates——主进程关闭继承副本（§6.6 防泄漏承诺，
 *      acceptance 4 实证面）。
 *
 * R-3 不超诺声明：本类对 CreateProcessW 只承诺 Microsoft Learn 明文的
 *   语义（句柄继承、创建即返回、挂起启动）；不承诺子进程入口任何行为
 *   （那是 worker 宿主的协议面）；命令行携带的句柄值属实现细节，不构成
 *   安全边界（离线单机，NFR-DEP-03 场景内无对抗者）。
 *
 * 线程安全：launch 为独立操作（每次调用建立独立实例集），可并发；实际
 *   调用点在调度线程（§10.3 launch 线程约束）。
 */

#ifndef SDURWS_IRD_EXECUTION_WIN32_PROCESSLAUNCHER_HPP
#define SDURWS_IRD_EXECUTION_WIN32_PROCESSLAUNCHER_HPP

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

#include <memory>
#include <string>

#include "JobScope.hpp"

namespace sdurws::ird::execution::win32 {

class ChannelPair;

/**
 * @brief 进程创建接缝（§3.5"IProcessOps 接缝（fault 注入）"——真实实现
 *        包装 CreateProcessW；测试注入替身验证启动失败路径/时序断言）。
 *
 * 接缝只覆盖"创建"这一步（最脆弱的外部交互）；Job assign/Resume/副本
 *   关闭仍由 ProcessLauncher 执行——注入替身不能绕过防泄漏与作业纪律
 *   （接缝的边界：可替换的是 OS 调用，不是流程）。
 */
class IProcessOps {
public:
    virtual ~IProcessOps() = default;

    /**
     * @brief 创建子进程（挂起启动——resume 由 ProcessLauncher 控制）。
     *
     * @param commandLine   [in] 完整命令行（含可执行路径与参数——
     *                      CreateProcessW 的可变性语义：可就地改写）
     * @param inheritHandles [in] 是否继承句柄（生产恒 true——管道副本传递）
     * @param pi            [out] 成功时填充（hThread/hProcess 归调用方关闭）
     * @return false＝创建失败（GetLastError 经 lastError() 透出）
     */
    virtual bool createProcess(const std::wstring& commandLine, bool inheritHandles,
                               PROCESS_INFORMATION* pi) = 0;

    /// 最近一次失败的 Win32 错误码（诊断承载）。
    virtual DWORD lastError() const = 0;
};

/// 生产实现（CreateProcessW：CREATE_SUSPENDED | CREATE_NO_WINDOW）。
class RealProcessOps final : public IProcessOps {
public:
    bool createProcess(const std::wstring& commandLine, bool inheritHandles,
                       PROCESS_INFORMATION* pi) override;
    DWORD lastError() const override { return m_lastError; }

private:
    DWORD m_lastError = 0;
};

/// 启动产物（全部句柄/管道的所有权在 Result——调用方〔WorkerSupervisor〕
/// 持有并随 worker 记录生命周期）。
struct LaunchResult {
    bool ok = false;                          ///< 启动是否成功（失败时各字段无效）
    DWORD lastError = 0;                      ///< 失败的 Win32 错误码（开发诊断）
    DWORD pid = 0;                            ///< worker 主进程 PID（观测面/临时目录名）
    HANDLE processHandle = nullptr;           ///< 主进程句柄（调用方负责 CloseHandle）
    std::unique_ptr<ChannelPair> channel;     ///< 通道对（服务端在主进程）
};

/**
 * @brief 启动一个 worker 进程（§6.4 launch 步的完整时序，见文件头）。
 *
 * @param executablePath [in] worker 可执行文件完整路径（与主进程同版本
 *                       基线——部署同目录，NFR-DEP-02；本函数不校验版本）
 * @param channelName    [in] 管道名基底（须全局唯一——调用方以
 *                       "ird-exec-<pid>-<序号>" 供给）
 * @param identityArg    [in] 绑定五元组命令行值（五个规范文本以 '|' 连接
 *                       ——worker 宿主握手帧的身份块来源；通道消息全携
 *                       五元组，ARCH §4.1。派发后的绑定以 DispatchRequest
 *                       帧头为准——池化复用换绑的更新点在 worker 宿主）
 * @param ops            [in] 进程创建接缝（生产传 RealProcessOps；测试注入）
 * @param job            [in] 目标作业对象（须 valid()——失败返回 ok=false）
 * @return 启动产物（ok=false 时 channel 为空、句柄为空——调用方直接出
 *         WorkerLaunchFailed 诊断）
 */
LaunchResult launchWorkerProcess(const std::wstring& executablePath,
                                 const std::wstring& channelName,
                                 const std::wstring& identityArg, IProcessOps& ops,
                                 JobScope& job);

}  // namespace sdurws::ird::execution::win32

#endif  // SDURWS_IRD_EXECUTION_WIN32_PROCESSLAUNCHER_HPP
