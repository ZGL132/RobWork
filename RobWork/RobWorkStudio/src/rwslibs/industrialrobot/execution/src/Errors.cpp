/**
 * @file   Errors.cpp
 * @brief  execution 错误类型的实现——token 对照表与异常构造（§3.4）。
 *
 * 设计依据：
 *   - units/execution.md §3.4（枚举清单与 token 列、ExecutionError 形态、
 *     "枚举对齐说明（v0.2）"——ContextClosed/InvalidState 为 fail-fast
 *     token 不发稳定码）
 *   - 任务契约 tasks/foundation/EX-T02.json（EX-SM-6 观测点
 *     "ExecutionError.code"——异常轨的落点）
 *
 * 实现说明：toToken 用 switch 全枚举直映（静态存储期字面量，无查表分配）；
 * 漏掉任一枚举值在 MSVC /W4 下会触发 C4062 警告（开关枚举缺项），配合
 * 尾部防御分支 return 保证全路径有返回值。
 */

#include <sdurws/ird/execution/Errors.hpp>

namespace sdurws::ird::execution {

const char* toToken(ExecutionErrorCode v) noexcept
{
    // 逐值直映——token 与 §3.4 清单行注释逐字一致（token 稳定承诺：
    // 日志/诊断检索键，改名即破坏既有留痕的可检索性）。
    switch (v) {
    case ExecutionErrorCode::SubmissionRejected:     return "execution/submission-rejected";
    case ExecutionErrorCode::StaleSnapshot:          return "execution/stale-snapshot";
    case ExecutionErrorCode::StoreReadOnly:          return "execution/store-read-only";
    case ExecutionErrorCode::ResourceInsufficient:   return "execution/resource-insufficient";
    case ExecutionErrorCode::CapabilityUnsupported:  return "execution/capability-unsupported";
    case ExecutionErrorCode::WorkerLaunchFailed:     return "execution/worker-launch-failed";
    case ExecutionErrorCode::WorkerCrashed:          return "execution/worker-crashed";
    case ExecutionErrorCode::WorkerHung:             return "execution/worker-hung";
    case ExecutionErrorCode::ForceTerminated:        return "execution/force-terminated";
    case ExecutionErrorCode::ChannelProtocolError:   return "execution/channel-protocol-error";
    case ExecutionErrorCode::RegistryUnknownRun:     return "execution/registry-unknown-run";
    case ExecutionErrorCode::RegistryMismatch:       return "execution/registry-mismatch";
    case ExecutionErrorCode::StaleAttempt:           return "execution/stale-attempt";
    case ExecutionErrorCode::CheckpointCorrupt:      return "execution/checkpoint-corrupt";
    case ExecutionErrorCode::CheckpointIncompatible: return "execution/checkpoint-incompatible";
    case ExecutionErrorCode::ArchiveFailed:          return "execution/archive-failed";
    case ExecutionErrorCode::ArchiveAuthorityLost:   return "execution/archive-authority-lost";
    case ExecutionErrorCode::ContextClosed:          return "execution/context-closed";
    case ExecutionErrorCode::InvalidState:           return "execution/invalid-state";
    }
    // 防御分支：枚举封闭，正常路径不可达；返回兜底 token 而非中止——
    // 本函数承诺 noexcept，不得抛出。
    return "execution/unknown";
}

ExecutionError::ExecutionError(ExecutionErrorCode code, std::string detail)
    : std::runtime_error(detail)   // detail 全量交给基类 what()（无附加成员消息）
    , m_code(code)
{
}

}  // namespace sdurws::ird::execution
