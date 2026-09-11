/**
 * @file   ProcessRunner.hpp
 * @brief  进程测试支撑——进程级注入原语（Job Object 进程树终止＋事件等待）。
 *
 * 设计依据：
 *   - units/testkit.md §6.5（签名与语义冻结）、§9 TK-T09 行（本头当时仅冻结
 *     声明）、§9 TK-T11 行（实现交付）、§10.1（触发登记）、O-33
 *   - 需求 NFR-REL-02/03、AT-11/13（载体——PRJ-T15/WP-08-T10 契约测试需要
 *     进程级崩溃/恢复场景）
 *
 * ★ 冻结语义与实现状态（O-33 消账）：本头在 TK-T09 时为"只有声明、没有定义"
 *   的设计冻结头；消费者触发已成立（2026-09-11 所有者采纳预防性补登，见
 *   testkit.md §10.1 与 DTB §4.2 O-33 消账记录），TK-T11 已按 §6.5 冻结签名
 *   交付实现（src/ProcessRunner.cpp）——公共签名零偏差。为实现承载追加的
 *   析构/禁拷贝/私有 Impl 指针属**增量接缝（实现细节级，非冻结变更）**，
 *   沿 TK-T08 v0.8 先例，不改变任何 §6.5 冻结成员的名称、参数与语义。
 *
 * 使用契约要点（§6.5 冻结，实现与消费方共同遵守）：
 *   - 崩溃＝子进程非零退出/无正常完成事件；取消＝被测取消协议事件序列＋自然退出；
 *     超时＝deadline 内未见完成事件→TerminatedByTimeout（区别于被测行为失败）；
 *     测试框架自身错误＝EnvUnavailable，不得计入被测失败；
 *   - 超时回收：Windows Job Object 绑子进程树，TerminateJobObject 兜底；
 *   - 同步纪律：正确性判据只来自可观察事件（EventWatch 有界等待），禁止固定 sleep；
 *   - 与 §6.4 文件系统故障的分工：真实崩溃验证进程隔离与恢复，两者不互相替代。
 *
 * 平台边界：Windows API 只出现在实现文件 src/ProcessRunner.cpp 内
 * （testkit.md §1.4 表），本头保持纯标准库，消费者无平台负担。
 */

#ifndef SDURWS_IRD_TESTKIT_PROCESSRUNNER_HPP
#define SDURWS_IRD_TESTKIT_PROCESSRUNNER_HPP

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::testkit {

/// 子进程规格（可执行文件＋参数＋环境覆盖＋超时上限）。
struct ProcessSpec {
    std::filesystem::path executable;                       ///< 可执行文件路径
    std::vector<std::string> args;                          ///< 参数表（不含 argv[0]）
    std::map<std::string, std::string> env;                 ///< 环境变量覆盖（在父环境之上）
    std::chrono::milliseconds timeout{60000};               ///< 超时上限（到点强制回收）
};

/// 退出形态区分（§6.5：崩溃/取消/超时/框架错误四分——不得混入被测行为失败）。
enum class ProcessExitKind {
    Exited,               ///< 正常退出（exitCode 有效）
    Crashed,              ///< 崩溃（非零异常码/无正常完成事件）
    TerminatedByTimeout,  ///< 超时强制回收（deadline 内未见完成事件）
    Killed                ///< 被动 kill（主动注入崩溃用）
};

/// 进程结果（形态＋退出码＋耗时）。
struct ProcessOutcome {
    ProcessExitKind kind = ProcessExitKind::Exited;
    int exitCode = 0;                                      ///< Exited 时有效；其余为 0
    std::chrono::milliseconds duration{};                  ///< 进程墙钟时长
};

/// 可观察事件等待（跨进程同步唯一判据——有界轮询是实现细节，不是判据）。
/// 线程安全：两个等待方法无共享可变状态，可多线程并发调用。
class EventWatch {
public:
    EventWatch() = default;

    /// 等待标记文件出现（deadline 内轮询；超时返回 false——调用方裁决为失败）。
    /// deadline ≤ 0 语义＝只探测当前状态（事件可能已经发生，无需等待）。
    bool awaitFile(const std::filesystem::path& marker, std::chrono::milliseconds deadline);

    /// 等待日志文件出现含 needle 的行（同上；文件未出现/无命中＝超时）。
    /// needle 为空串在任何文件都命中，属调用方装配错误（抛 Usage）。
    bool awaitLineInFile(const std::filesystem::path& log, std::string_view needle,
                         std::chrono::milliseconds deadline);
};

/// 测试子进程运行器（§6.5 冻结签名；实现＝src/ProcessRunner.cpp，TK-T11）。
///
/// 行为契约（review 对照面）：
///   - start() 以 CREATE_SUSPENDED 启动子进程→绑定 Job Object→再 Resume，
///     消除"进程已运行却不在 Job 内"的竞态窗口；Job 带 KILL_ON_JOB_CLOSE，
///     即使调用方漏收尾，句柄关闭时内核也会兜底杀树；
///   - outcome() 按 §6.5 判定四态：Exited（退出码 0）/ Crashed（非零退出，
///     含异常码——§6.5"崩溃＝子进程非零退出"）/ TerminatedByTimeout
///     （先 TerminateJobObject 杀树再返回）/ Killed（kill() 请求后的结果）。
///     exitCode 仅 Exited 携带真实码，其余形态恒为 0（字段注释冻结语义）；
///     幂等：终结后的重复 outcome() 返回缓存结果；
///   - kill() 幂等：进程已终结后调用为无操作；
///   - workingDir 传空路径＝缺省 TempDir 语义（§6.5 签名注释"= TempDir"；
///     临时目录由运行器持有，随运行器析构一并清理）。
///
/// 生命周期与线程安全：独占 OS 句柄资源（禁拷贝）；单个实例非线程安全——
/// start/outcome/kill 须在同一测试线程串行调用；不同实例可并行（句柄独立）。
///
/// 错误语义（§6.5"框架自身错误不得计入被测失败"）：调用方契约违约（未 start
/// 即 outcome/kill、重复 start、空 executable、工作目录不存在）抛
/// TestKitError(Usage) fail-fast；进程创建/终止等 OS 层失败抛
/// TestKitError(EnvUnavailable)。
class TestProcessRunner {
public:
    TestProcessRunner() = default;

    /// 析构兜底杀树：销毁时子进程仍在运行的，先 TerminateJobObject 再释放
    /// 句柄与缺省临时目录——杜绝"测试函数结束后孤儿进程占着临时目录"。
    ~TestProcessRunner();

    TestProcessRunner(const TestProcessRunner&) = delete;             ///< 独占句柄——禁拷贝
    TestProcessRunner& operator=(const TestProcessRunner&) = delete;  ///< 同上
    TestProcessRunner(TestProcessRunner&&) = delete;                  ///< 移动会转移杀树责任而无收益——禁移动
    TestProcessRunner& operator=(TestProcessRunner&&) = delete;       ///< 同上

    /// 启动子进程（workingDir 缺省用 TempDir；进程树绑 Job Object）。
    void start(const ProcessSpec& spec, std::filesystem::path workingDir);

    /// 取结果；超时路径先终止进程树（TerminateJobObject）再返回 TerminatedByTimeout。
    ProcessOutcome outcome();

    /// 主动终止（崩溃注入载体——AT-11 进程隔离与恢复测试）。
    void kill();

private:
    /// Windows 句柄与状态机的私有载体（实现细节级增量接缝——PImpl：
    /// 平台句柄不出现在任何公共头，§1.4 边界由结构本身钉住）。
    struct Impl;
    Impl* impl_ = nullptr;   ///< start() 建立、析构释放；未 start 时恒为空
};

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_PROCESSRUNNER_HPP
