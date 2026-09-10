/**
 * @file   ProcessRunner.hpp
 * @brief  进程测试支撑——**设计冻结头**（仅声明；实现随消费者阶段需要交付，O-33）。
 *
 * 设计依据：
 *   - units/testkit.md §6.5（签名原文冻结；"实现按消费者阶段需要交付"——阶段 A
 *     仅交付 §6.4 进程内原语，TestProcessRunner 不建目标、不写空测试）、
 *     §9 TK-T09 行（产物含 ProcessRunner.hpp"仅头文件设计冻结"）、O-33
 *   - 需求 NFR-REL-02/03、AT-11/13（载体——挂接 project/execution 契约测试的
 *     实际需要时由触发任务补登 TK-T11 并恢复 ≙ 映射）
 *
 * ★ 冻结语义（O-33 处置约束）：本头**只有声明、没有定义**——不提供 .cpp、不建
 *   目标、本任务不写消费它的测试（任务约束§八：不写空测试）。任何消费者单元
 *   （project/execution 契约测试）启用前须先由实现任务补齐定义。
 *
 * 使用契约要点（§6.5 冻结，实现时必须遵守）：
 *   - 崩溃＝子进程非零退出/无正常完成事件；取消＝被测取消协议事件序列＋自然退出；
 *     超时＝deadline 内未见完成事件→TerminatedByTimeout（区别于被测行为失败）；
 *     测试框架自身错误＝EnvUnavailable，不得计入被测失败；
 *   - 超时回收：Windows Job Object 绑子进程树，TerminateJobObject 兜底；
 *   - 同步纪律：正确性判据只来自可观察事件（EventWatch 有界等待），禁止固定 sleep；
 *   - 与 §6.4 文件系统故障的分工：真实崩溃验证进程隔离与恢复，两者不互相替代。
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
class EventWatch {
public:
    EventWatch() = default;

    /// 等待标记文件出现（deadline 内轮询；超时返回 false——调用方裁决为失败）。
    bool awaitFile(const std::filesystem::path& marker, std::chrono::milliseconds deadline);

    /// 等待日志文件出现含 needle 的行（同上；文件未出现/无命中＝超时）。
    bool awaitLineInFile(const std::filesystem::path& log, std::string_view needle,
                         std::chrono::milliseconds deadline);
};

/// 测试子进程运行器（声明冻结——定义随 TK-T11 触发任务交付，O-33）。
class TestProcessRunner {
public:
    TestProcessRunner() = default;

    /// 启动子进程（workingDir 缺省用 TempDir；进程树绑 Job Object）。
    void start(const ProcessSpec& spec, std::filesystem::path workingDir);

    /// 取结果；超时路径先终止进程树（TerminateJobObject）再返回 TerminatedByTimeout。
    ProcessOutcome outcome();

    /// 主动终止（崩溃注入载体——AT-11 进程隔离与恢复测试）。
    void kill();
};

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_PROCESSRUNNER_HPP
