/**
 * @file   ProcessRunner.cpp
 * @brief  进程测试支撑实现——Windows Job Object 进程树管理与可观察事件等待。
 *
 * 设计依据：
 *   - units/testkit.md §6.5（四态 outcome／Job Object 树终止／事件等待唯一
 *     判据的冻结语义）、§1.4（Windows API 仅限本实现文件）、§9 TK-T11 行
 *   - 需求 NFR-REL-02/03；任务契约 tasks/foundation/TK-T11.json
 *
 * 背景说明（为什么要有本文件）：
 *   AT-11/13（保存事务崩溃恢复、任务取消恢复）需要**真实**的进程级故障——
 *   §6.4 的进程内故障注入验证的是"逻辑鲁棒性"（错误路径、事务回滚），无法
 *   覆盖"进程整个消失"的恢复路径。本文件以子进程为载体制造真实崩溃/超时/
 *   强杀，并提供 EventWatch 作为跨进程同步的唯一正确性判据（§6.5 同步纪律：
 *   禁止固定 sleep 后断言——时序竞态会让测试间歇性失败且掩盖缺陷）。
 *
 * 实现边界（红线）：
 *   - 本文件是 testkit 全单元唯一允许出现 Windows API 的翻译单元
 *     （testkit.md §1.4 表："进程支撑的 Windows API 仅限 ProcessRunner
 *     实现文件内"）——头文件与其余实现保持纯标准库，便于移植与审计；
 *   - 零 Qt、零 gtest、零产品单元依赖（T-2）；无新增外部依赖：本文件用到的
 *     进程/Job/字符转换 API 全部位于 kernel32，随 MSVC 工具链默认链接，
 *     不需要也不允许引入任何链接库或第三方渠道（AGENTS.md §3）。
 *
 * 线程约束：TestProcessRunner 单实例非线程安全（成员句柄无锁保护），须在
 * 单一测试线程串行调用；EventWatch 无状态，可并发（见头文件注释）。
 */

#include <sdurws/ird/testkit/ProcessRunner.hpp>

#include <sdurws/ird/testkit/Fixture.hpp>    // TempDir：workingDir 缺省语义的载体
#include <sdurws/ird/testkit/TestPaths.hpp>  // TestKitError：testkit 唯一异常通道

// 宏须在 <windows.h> 之前定义（本头文件不引 windows.h，此顺序在本 TU 内成立）。
// 集成模式的全局编译定义可能已注入同名宏——带守卫定义避免 C4005 重定义警告；
// 独立冒烟模式无注入时则由这里补齐。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // 裁剪 windows.h——不引入 socket/crypto 等无关声明面
#endif
#ifndef NOMINMAX
#define NOMINMAX             // 阻止 min/max 宏污染本文件对 std::min/max 的潜在使用
#endif
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::testkit;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// 常量与小微工具
// ---------------------------------------------------------------------------

// EventWatch 有界轮询的单次间隔，取 5 ms：子进程启动＋文件落盘的量级在数十
// ms 以上，5 ms 轮询对结果无感知差异，同时让超时用例的 CPU 占用可忽略。
// 注意 §6.5：轮询只是实现细节，正确性判据永远是"事件是否出现"，间隔大小
// 不影响判定结果（只影响响应延迟与 CPU）。
constexpr Clock::duration kPollInterval = std::chrono::milliseconds{5};

// 主动/超时终止后等待子进程真正退出的上限。TerminateJobObject 的终止请求是
// 异步的，正常几 ms 内完成；给 5 s 上限是防"子进程被外部调试器/挂起 IO 卡死
// 不退"时 outcome() 永久阻塞——宁可报环境错误让测试失败，也不挂死测试管线。
constexpr auto kTerminateJoinTimeout = std::chrono::milliseconds{5000};

// TerminateJobObject 的退出码参数（0xDEAD 哨兵）。四态判定不读该值
// （Killed/TerminatedByTimeout 形态下 exitCode 恒为 0，见 ProcessRunner.hpp
// 字段注释），取非零只是让外部工具（procexp 等）观察时能与正常退出区分。
constexpr UINT kForcedExitCode = 0xDEAD;

/**
 * @brief 毫秒时长 → Win32 等待超时（DWORD 毫秒；INFINITE＝0xFFFFFFFF）。
 *
 * 钳制规则：负值（deadline 已过）取 0——立即返回不等待；超过 INFINITE-1 的
 * 天文值钳到 INFINITE——直接 static_cast 会让 chrono::milliseconds::max()
 * 回绕成小值，把"几乎无限等"错成"几乎不等待"。
 *
 * @param ms [in] 等待时长（可为负，语义见上）
 * @return Win32 等待毫秒数（0/正数/INFINITE）
 */
DWORD toWin32Wait(std::chrono::milliseconds ms) noexcept
{
    if (ms.count() <= 0) {
        return 0;  // deadline 已过：轮询等待立即返回（超时路径由调用方裁决）
    }
    constexpr auto kInfiniteMinus1 = std::chrono::milliseconds{0xFFFFFFFE};
    if (ms >= kInfiniteMinus1) {
        return INFINITE;  // 天文值钳制——语义仍是"等到为止"，不改变调用方意图
    }
    return static_cast<DWORD>(ms.count());
}

/**
 * @brief UTF-8 窄串 → UTF-16 宽串（Win32 API 面用）。
 *
 * ProcessSpec.args / env 的字符串按 UTF-8 解释（头文件契约）；文件路径不经过
 * 本函数（filesystem::path 自带原生宽字符视图）。转换失败属环境异常——按
 * EnvUnavailable 报错而不是静默产出空串（空串会变成空参数/空变量名，错误
 * 下游化后极难定位）。
 *
 * @param s [in] UTF-8 编码字符串（不得为空串——空串由调用方短路处理）
 * @return UTF-16 编码字符串
 * @throws TestKitError(EnvUnavailable) 转换失败（非法 UTF-8 序列等）
 */
std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty()) {
        return {};
    }
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                           static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "ProcessRunner：UTF-8→UTF-16 转换失败（输入串长 " +
                               std::to_string(s.size()) + "）");
    }
    std::wstring wide(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                          wide.data(), need);
    return wide;
}

/**
 * @brief 按 Windows 命令行引号规则包裹单个参数。
 *
 * 规则（CreateProcess 命令行解析约定）：
 *   - 参数含空格/制表符/引号时整体加引号；
 *   - 参数内的引号转义为 \"；紧邻引号（或结尾）的连续反斜杠翻倍——否则
 *     解析器会把 \" 里的反斜杠吞掉一半，参数在子进程端碎裂。
 * 本项目测试参数（路径、文件名、短标记）通常不触发这些形态，但按完整规则
 * 实现，避免未来用例踩坑时静默出错。
 *
 * @param arg [in] UTF-16 参数原文
 * @return 可直接拼入命令行的参数串
 */
std::wstring quoteWindowsArg(const std::wstring& arg)
{
    const bool needQuote = arg.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needQuote) {
        return arg;  // 无特殊字符：原样拼接（绝大多数测试参数走此捷径）
    }
    std::wstring out{L'"'};
    size_t pendingBackslashes = 0;  // 当前连续反斜杠计数（遇非反斜杠或结尾时结算）
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++pendingBackslashes;  // 先攒着——是否翻倍取决于后一个字符
            continue;
        }
        if (c == L'"') {
            // 引号前的反斜杠全部翻倍，再加 \" 转义引号本身
            out.append(pendingBackslashes + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(pendingBackslashes, L'\\');  // 普通字符：反斜杠原样
            out.push_back(c);
        }
        pendingBackslashes = 0;
    }
    // 结尾反斜杠也要翻倍（后面紧跟收尾引号，规则同"紧邻引号"）
    out.append(pendingBackslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

/**
 * @brief 构造"父环境＋spec.env 覆盖"的合并环境块。
 *
 * ProcessSpec.env 的契约是"环境变量覆盖（在父环境之上）"：同名覆盖、其余
 * 父变量原样继承——子进程需要完整环境（PATH、SYSTEMROOT 等）才能正常运行
 * 常见工具，不能只给覆盖条目。返回块以双 NUL 结尾（CreateProcess 的
 * CREATE_UNICODE_ENVIRONMENT 约定）。map 的键排序同时保证了环境块的确定性
 * 排列（同一 spec 恒得同一块——可复现性的次要来源，NFR-COR-02）。
 *
 * @param overrides [in] 覆盖条目（键值均按 UTF-8 解释）
 * @return 完整环境块（"NAME=VALUE\0" 序列＋终止 NUL）
 * @throws TestKitError(EnvUnavailable) UTF-8 转换失败或父环境块不可得
 */
std::wstring buildEnvironmentBlock(const std::map<std::string, std::string>& overrides)
{
    std::map<std::wstring, std::wstring> merged;

    // 第一遍：父环境全量装入。GetEnvironmentStringsW 返回
    // "NAME=VALUE\0NAME=VALUE\0...\0" 的双 NUL 结尾块，逐条扫描。
    // 注意返回类型是非 const LPWCH——FreeEnvironmentStringsW 要求同一指针
    // 类型，不能用 const 接收。
    wchar_t* const parent = ::GetEnvironmentStringsW();
    if (parent == nullptr) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "ProcessRunner：父环境块不可得（GetEnvironmentStringsW 失败）");
    }
    for (const wchar_t* p = parent; *p != L'\0'; p += std::wstring(p).size() + 1) {
        const std::wstring entry{p};
        const auto eq = entry.find(L'=');
        // 以 '=' 开头的条目是 cmd 的隐藏变量（如 "=C:=C:\..."），不进入覆盖
        // 模型（无法用普通 "NAME=VALUE" 语义表达，丢弃不影响测试子进程）。
        if (eq != std::wstring::npos && eq > 0) {
            merged.emplace(entry.substr(0, eq), entry.substr(eq + 1));
        }
    }
    ::FreeEnvironmentStringsW(parent);

    // 第二遍：覆盖条目写入（map 赋值语义＝同名覆盖、其余新增）。
    for (const auto& [name, value] : overrides) {
        merged[utf8ToWide(name)] = utf8ToWide(value);
    }

    std::wstring block;
    for (const auto& [name, value] : merged) {
        block += name;
        block += L'=';
        block += value;
        block += L'\0';
    }
    block.push_back(L'\0');  // 终止 NUL：整块以双 NUL 结尾
    return block;
}

}  // namespace

// ---------------------------------------------------------------------------
// TestProcessRunner::Impl——私有状态载体（PImpl）
// ---------------------------------------------------------------------------

/**
 * @brief TestProcessRunner 的私有状态（Windows 句柄＋判定状态机＋缓存）。
 *
 * 生命周期：start() 成功后建立（new），析构函数 delete；句柄在 Impl 析构中
 * 关闭，缺省临时目录随后析构清理——先关 Job 句柄（KILL_ON_JOB_CLOSE 兜底
 * 杀树）再删目录的顺序由析构体内的显式步骤保证，避免"目录还被进程占用时
 * 就尝试删除"。
 */
struct TestProcessRunner::Impl
{
    /// 运行阶段机（判定 outcome() 走哪条路径的唯一依据）。
    enum class Phase {
        Running,          ///< 子进程已启动、尚未终结
        KilledByRequest,  ///< kill() 已请求终止——outcome 将报告 Killed
        Completed,        ///< outcome() 已判定并缓存——后续调用幂等返回缓存
    };

    Phase phase = Phase::Running;
    HANDLE hProcess = nullptr;  ///< 子进程句柄（等待/取退出码；Impl 拥有，析构关闭）
    HANDLE hJob = nullptr;      ///< Job Object 句柄（树终止/兜底；Impl 拥有，析构关闭）
    Clock::time_point startedAt{};          ///< start() 完成时刻（steady 时钟，测 duration）
    Clock::time_point killedAt{};           ///< kill() 请求时刻（Killed 形态的 duration 终点）
    std::chrono::milliseconds timeout{0};   ///< ProcessSpec.timeout 快照（避免依赖外部 spec 存活）
    // TempDir 禁拷贝禁移动（RAII 语义），故经 unique_ptr 间接持有而非
    // optional——optional<TempDir> 的移动赋值被连带删除，无法在 start() 与
    // Impl 之间转移所有权。
    std::unique_ptr<TempDir> ownedWorkingDir; ///< workingDir 缺省时由运行器持有的临时目录
    ProcessOutcome cached{};                ///< outcome() 判定缓存（幂等重入的返回值）
    bool hasCached = false;                 ///< 缓存有效标志（cached 本身有合法零值，不能当标志用）

    ~Impl()
    {
        // 顺序敏感：先关 Job（内核在此兜底杀树——KILL_ON_JOB_CLOSE），再关
        // 进程句柄；TempDir 成员随后析构删目录，此时子进程已不可能存活。
        if (hJob != nullptr) {
            ::CloseHandle(hJob);
            hJob = nullptr;
        }
        if (hProcess != nullptr) {
            ::CloseHandle(hProcess);
            hProcess = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// TestProcessRunner 成员
// ---------------------------------------------------------------------------

/**
 * @brief 启动子进程并绑定 Job Object（§6.5 start 冻结签名的实现）。
 *
 * 启动序列（顺序即正确性，不可调换）：
 *   1. 契约检查（Usage fail-fast）→ 2. 建 Job＋KILL_ON_JOB_CLOSE →
 *   3. 拼命令行/环境块 → 4. CREATE_SUSPENDED 启动 → 5. 绑定 Job →
 *   6. Resume。先挂起再绑定的原因：若先 Resume 后绑定，子进程可能已经
 *   spawn 出自己的子进程，该孙进程不在 Job 内——树终止漏杀。挂起窗口内
 *   子进程没有任何执行机会，绑定必然完整覆盖整棵未来的进程树。
 *
 * @param spec       [in] 进程规格（可执行文件非空；args/env 按 UTF-8）
 * @param workingDir [in] 子进程工作目录；空路径＝缺省 TempDir（§6.5 注释语义）
 *
 * @throws TestKitError(Usage)           重复 start／executable 为空／工作目录不存在
 * @throws TestKitError(EnvUnavailable)  Job/进程 API 失败、UTF-8 转换失败
 */
void TestProcessRunner::start(const ProcessSpec& spec, std::filesystem::path workingDir)
{
    // ---- 第 1 步：调用方契约检查（fail-fast——错误语义见头文件注释）----
    if (impl_ != nullptr) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "TestProcessRunner::start：实例已启动过，不可重复 start"
                           "（同一实例一次生命周期；多进程用例请各用独立实例）");
    }
    if (spec.executable.empty()) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "TestProcessRunner::start：ProcessSpec.executable 为空——"
                           "无可执行文件的启动请求属调用方装配错误");
    }

    // ---- 第 2 步：建立 Job Object 并启用"句柄关闭即杀树"----
    HANDLE hJob = ::CreateJobObjectW(nullptr, nullptr);
    if (hJob == nullptr) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "TestProcessRunner::start：CreateJobObjectW 失败（GetLastError=" +
                               std::to_string(::GetLastError()) + "）");
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimit{};
    jobLimit.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                   &jobLimit, sizeof(jobLimit))) {
        const DWORD err = ::GetLastError();
        ::CloseHandle(hJob);
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "TestProcessRunner::start：SetInformationJobObject 失败"
                           "（GetLastError=" + std::to_string(err) +
                           "）——无 KILL_ON_JOB_CLOSE 兜底不允许继续");
    }

    // ---- 第 3 步：拼命令行与合并环境块（零继承句柄——事件同步走文件标记）----
    // bInheritHandles=FALSE 的原因：§6.5 把跨进程同步判据限定为可观察文件，
    // 子进程不需要继承任何句柄；不继承也杜绝了句柄泄漏进子进程的面。
    std::wstring cmdLine = quoteWindowsArg(spec.executable.wstring());
    for (const std::string& rawArg : spec.args) {
        cmdLine += L' ';
        cmdLine += quoteWindowsArg(utf8ToWide(rawArg));
    }
    std::wstring envBlock = buildEnvironmentBlock(spec.env);

    // workingDir 缺省语义（§6.5 签名注释"= TempDir"）：空路径→运行器持有
    // 的临时目录；显式路径则校验存在——把拼写错误挡在启动前（Usage）而不是
    // 让 CreateProcess 以晦涩的目录错误失败。局部经 unique_ptr 持有，启动
    // 成功后才移交 Impl——启动失败路径上临时目录照常 RAII 清理。
    std::unique_ptr<TempDir> ownedDir;
    if (workingDir.empty()) {
        ownedDir = std::make_unique<TempDir>("procrunner-wd");   // 构造即建立（§6.2）
        workingDir = ownedDir->path();
    } else if (!fs::exists(workingDir)) {
        ::CloseHandle(hJob);
        throw TestKitError(TestKitErrorKind::Usage,
                           "TestProcessRunner::start：workingDir 不存在（" +
                               workingDir.string() + "）——调用方须先建目录");
    }

    // ---- 第 4 步：挂起态启动子进程 ----
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL created = ::CreateProcessW(
        nullptr,                        // lpApplicationName 空：由命令行首段解析模块路径
        cmdLine.data(),                 // 可写缓冲（CreateProcessW 会原位改写）
        nullptr, nullptr,               // 安全属性：默认（句柄不继承，见上）
        FALSE,                          // bInheritHandles：不继承
        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
        envBlock.data(),                // 合并环境块（双 NUL 结尾）
        workingDir.wstring().c_str(),   // 子进程当前工作目录
        &si, &pi);
    if (!created) {
        const DWORD err = ::GetLastError();
        ::CloseHandle(hJob);
        // 可执行文件路径拼错也走这里（ERROR_FILE_NOT_FOUND）——归环境错误
        // 而非 Usage：无法与"路径存在但被占用/被杀毒拦截"区分前不预判调用方。
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "TestProcessRunner::start：CreateProcessW 失败"
                           "（GetLastError=" + std::to_string(err) + "，命令行首段=" +
                           spec.executable.string() + "）");
    }

    // ---- 第 5 步：绑定 Job（失败即终止已创建进程——不许留 Job 外孤儿）----
    if (!::AssignProcessToJobObject(hJob, pi.hProcess)) {
        const DWORD err = ::GetLastError();
        ::TerminateProcess(pi.hProcess, kForcedExitCode);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(hJob);
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "TestProcessRunner::start：AssignProcessToJobObject 失败"
                           "（GetLastError=" + std::to_string(err) +
                           "）——已终止未受管束的子进程");
    }

    // ---- 第 6 步：恢复执行；状态就位 ----
    if (::ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
        const DWORD err = ::GetLastError();
        ::TerminateJobObject(hJob, kForcedExitCode);  // 树终止——孙进程也一并处理
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(hJob);
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "TestProcessRunner::start：ResumeThread 失败"
                           "（GetLastError=" + std::to_string(err) + "）——已终止子进程树");
    }
    ::CloseHandle(pi.hThread);  // 主线程句柄不再需要（等待只挂 hProcess）

    impl_ = new Impl();
    impl_->hProcess = pi.hProcess;
    impl_->hJob = hJob;
    impl_->startedAt = Clock::now();
    impl_->timeout = spec.timeout;
    impl_->ownedWorkingDir = std::move(ownedDir);
}

/**
 * @brief 等待并判定进程结果（§6.5 四态语义；幂等）。
 *
 * 判定路径：
 *   - KilledByRequest → 等真退出后报 Killed（kill() 已保证/请求树终止）；
 *   - Running → 按 spec.timeout 的**剩余时间**等待 hProcess：
 *     · 进程先退出：GetExitCodeProcess——0＝Exited（携带码）；非 0＝Crashed
 *       （§6.5"崩溃＝子进程非零退出"，含 0xC0000005 类异常码；exitCode 恒 0）；
 *     · 到点未退：TerminateJobObject 杀树→等真退出→TerminatedByTimeout。
 *
 * @return 判定后的 ProcessOutcome（duration＝进程墙钟时长近似值——含等待
 *         唤醒延迟，供超时类用例做量级断言，不作精确计时用途）
 *
 * @throws TestKitError(Usage)           未 start 即调用
 * @throws TestKitError(EnvUnavailable)  等待/取码/终止 API 失败，或终止后
 *                                       超上限仍不退出（环境异常不静默）
 */
ProcessOutcome TestProcessRunner::outcome()
{
    if (impl_ == nullptr) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "TestProcessRunner::outcome：未 start 即取结果——"
                           "调用方契约违约（fail-fast）");
    }
    // 幂等重入：终结结果只判定一次，重复调用返回缓存（消费方常在断言后再
    // 调 outcome 收尾，双态返回会让两次断言互相矛盾）。
    if (impl_->hasCached) {
        return impl_->cached;
    }

    auto& im = *impl_;

    // ---- 路径一：kill() 已请求终止 → 等真退出，报 Killed ----
    if (im.phase == Impl::Phase::KilledByRequest) {
        const DWORD wait = ::WaitForSingleObject(
            im.hProcess, toWin32Wait(kTerminateJoinTimeout));
        if (wait != WAIT_OBJECT_0) {
            // 终止请求已发出却久不退出（WAIT_TIMEOUT/FAILED）：环境异常，
            // 静默报 Killed 会掩盖"进程树没死"的严重事实。
            throw TestKitError(TestKitErrorKind::EnvUnavailable,
                               "TestProcessRunner::outcome：kill 后子进程未在上限内退出"
                               "（wait=" + std::to_string(wait) + "）");
        }
        im.cached.kind = ProcessExitKind::Killed;
        im.cached.exitCode = 0;  // 冻结语义：非 Exited 形态 exitCode 恒 0
        im.cached.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            im.killedAt - im.startedAt);  // 进程存活时长到 kill 请求时刻为止
        im.hasCached = true;
        im.phase = Impl::Phase::Completed;
        return im.cached;
    }

    // ---- 路径二：正常等待（Running）——deadline＝start＋timeout 的剩余时间 ----
    const auto deadline = im.startedAt + im.timeout;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - Clock::now());
    const DWORD wait = ::WaitForSingleObject(im.hProcess, toWin32Wait(remaining));

    if (wait == WAIT_FAILED) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "TestProcessRunner::outcome：WaitForSingleObject 失败"
                           "（GetLastError=" + std::to_string(::GetLastError()) + "）");
    }

    if (wait == WAIT_OBJECT_0) {
        // 进程已退出：按退出码二分（§6.5——0＝正常完成事件；非 0＝崩溃，
        // 覆盖 exit(N) 与访问违例等异常码两种"崩溃注入"形态）。
        DWORD code = 0;
        if (!::GetExitCodeProcess(im.hProcess, &code)) {
            throw TestKitError(TestKitErrorKind::EnvUnavailable,
                               "TestProcessRunner::outcome：GetExitCodeProcess 失败"
                               "（GetLastError=" + std::to_string(::GetLastError()) + "）");
        }
        if (code == 0) {
            im.cached.kind = ProcessExitKind::Exited;
            im.cached.exitCode = 0;  // 唯一携带真实码的形态（此处码必为 0）
        } else {
            im.cached.kind = ProcessExitKind::Crashed;
            im.cached.exitCode = 0;  // 冻结语义：Crashed 不携带码（信息走测试日志）
        }
    } else {
        // WAIT_TIMEOUT：deadline 内未见完成事件 → 先杀树再报告
        // TerminatedByTimeout（§6.5"超时→终止进程树后返回"的顺序即在此处）。
        if (!::TerminateJobObject(im.hJob, kForcedExitCode)) {
            throw TestKitError(TestKitErrorKind::EnvUnavailable,
                               "TestProcessRunner::outcome：超时后 TerminateJobObject 失败"
                               "（GetLastError=" + std::to_string(::GetLastError()) + "）");
        }
        const DWORD joined = ::WaitForSingleObject(
            im.hProcess, toWin32Wait(kTerminateJoinTimeout));
        if (joined != WAIT_OBJECT_0) {
            throw TestKitError(TestKitErrorKind::EnvUnavailable,
                               "TestProcessRunner::outcome：超时终止后子进程未在上限内退出"
                               "（wait=" + std::to_string(joined) + "）");
        }
        im.cached.kind = ProcessExitKind::TerminatedByTimeout;
        im.cached.exitCode = 0;  // 冻结语义：超时形态不携带码
    }

    // duration＝从 Resume 到"观察到终结"的墙钟时长（近似值，理由见头注释）。
    im.cached.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - im.startedAt);
    im.hasCached = true;
    im.phase = Impl::Phase::Completed;
    return im.cached;
}

/**
 * @brief 主动终止进程树（§6.5 kill 冻结签名——崩溃注入载体）。
 *
 * 只发 TerminateJobObject 并记录请求时刻（duration 终点）；真正的"已退出"
 * 确认延迟到 outcome() 的 Killed 路径——把等待成本留给取结果的一方，kill()
 * 本身保持立即返回（AT-11 用例先杀进程再观察恢复行为，不需要在这里等）。
 *
 * @throws TestKitError(Usage)           未 start 即调用
 * @throws TestKitError(EnvUnavailable)  TerminateJobObject 失败（杀树请求未受理）
 */
void TestProcessRunner::kill()
{
    if (impl_ == nullptr) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "TestProcessRunner::kill：未 start 即终止——调用方契约违约");
    }
    if (impl_->hasCached) {
        return;  // 已终结：无进程可杀——幂等无操作（调用方无须预判状态）
    }
    if (impl_->phase == Impl::Phase::Running) {
        if (!::TerminateJobObject(impl_->hJob, kForcedExitCode)) {
            throw TestKitError(TestKitErrorKind::EnvUnavailable,
                               "TestProcessRunner::kill：TerminateJobObject 失败"
                               "（GetLastError=" + std::to_string(::GetLastError()) + "）");
        }
        impl_->phase = Impl::Phase::KilledByRequest;
        impl_->killedAt = Clock::now();
    }
    // phase == KilledByRequest：重复 kill——终止请求已发出，无需再发（幂等）。
}

/**
 * @brief 析构——兜底杀树＋释放句柄与缺省临时目录。
 *
 * 调用方"忘了收尾"（既没取 outcome 也没 kill 就让运行器出作用域）时，仍
 * 在运行的子进程必须在这里被终止：留活口意味着孤儿进程占着临时目录/端口
 * 继续跑，测试管线整体被污染。正常路径（已终结）这里只做句柄/目录释放。
 */
TestProcessRunner::~TestProcessRunner()
{
    if (impl_ == nullptr) {
        return;  // 未 start 过：无资源（构造函数零工作与之对称）
    }
    if (impl_->phase == Impl::Phase::Running) {
        // 先显式杀树（KILL_ON_JOB_CLOSE 是第二道内核兜底，双保险），并给
        // 最多 kTerminateJoinTimeout 让其退出——防止目录删除撞上仍在写的
        // 子进程。此处失败不抛（析构禁抛）：KILL_ON_JOB_CLOSE 仍会兜底。
        ::TerminateJobObject(impl_->hJob, kForcedExitCode);
        ::WaitForSingleObject(impl_->hProcess, toWin32Wait(kTerminateJoinTimeout));
    }
    delete impl_;  // Impl 析构：关句柄→析构缺省 TempDir（顺序见 Impl 注释）
    impl_ = nullptr;
}

// ---------------------------------------------------------------------------
// EventWatch 成员（无状态——两方法均可并发调用）
// ---------------------------------------------------------------------------

/**
 * @brief 有界等待标记文件出现（§6.5 EventWatch 冻结签名）。
 *
 * 轮询实现细节：每轮用 exists() 的 error_code 重载探测——权限抖动/悬空
 * 链接等瞬时文件系统故障按"未出现"处理并交给 deadline 裁决，不让一次
 * 探测异常直接炸掉等待（文件系统故障的正确性裁决归 §6.4 的故障注入语义，
 * 不归事件等待）。
 *
 * deadline 语义：从调用时刻起算的**相对时限**。deadline ≤ 0＝只探测当前
 * 状态一次（事件可能已经发生——"已发生"不应因时限为零而报告超时）。
 *
 * @param marker   [in] 待出现的文件路径
 * @param deadline [in] 相对时限（毫秒；可为 0/负，语义见上）
 * @return true＝时限内出现；false＝时限内未出现（调用方裁决为测试失败）
 */
bool EventWatch::awaitFile(const std::filesystem::path& marker,
                           std::chrono::milliseconds deadline)
{
    const auto deadlinePoint = Clock::now() + deadline;
    for (;;) {
        std::error_code probeError;  // exists() 失败不抛——按"本轮未出现"续轮询
        if (fs::exists(marker, probeError)) {
            return true;
        }
        if (Clock::now() >= deadlinePoint) {
            return false;  // 有界等待的出口：到点即返，绝不无限等待
        }
        std::this_thread::sleep_for(kPollInterval);
    }
}

/**
 * @brief 有界等待日志文件出现含 needle 的行（§6.5 EventWatch 冻结签名）。
 *
 * 轮询实现细节：文件存在则**全量重扫**再判定。选择全量而非增量偏移的原因：
 * (a) 接口无状态（头文件冻结，不能夹带游标成员）；(b) 被测子进程可能整文件
 * 重写/轮转，增量偏移会漏看——全量重扫对各种写日志姿势都鲁棒；(c) 测试日志
 * 是 KB 量级，重扫成本可忽略。匹配为子串语义（行内任意位置）；CRLF 行尾的
 * '\r' 留在行内但不影响不含 '\r' 的 needle。
 *
 * @param log      [in] 日志文件路径（轮询期间可以尚未出现）
 * @param needle   [in] 待出现的行内子串（按字节匹配；空串恒命中，禁止）
 * @param deadline [in] 相对时限（同 awaitFile；≤ 0＝只查当前内容一次）
 * @return true＝时限内命中；false＝超时（含文件始终未出现——契约原话
 *         "文件未出现/无命中＝超时"）
 *
 * @throws TestKitError(Usage) needle 为空串（任何文件都命中＝等待语义失效）
 */
bool EventWatch::awaitLineInFile(const std::filesystem::path& log,
                                 std::string_view needle,
                                 std::chrono::milliseconds deadline)
{
    if (needle.empty()) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "EventWatch::awaitLineInFile：needle 为空串——空串在任何"
                           "文件都命中，等待语义无意义，属调用方装配错误");
    }
    const auto deadlinePoint = Clock::now() + deadline;
    for (;;) {
        std::error_code probeError;
        if (fs::exists(log, probeError)) {
            // 二进制模式打开：只做字节级行切分，不做任何文本翻译（编码无关注）。
            std::ifstream in{log, std::ios::binary};
            std::string line;
            while (std::getline(in, line)) {
                if (line.find(needle) != std::string::npos) {
                    return true;
                }
            }
        }
        if (Clock::now() >= deadlinePoint) {
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
}
