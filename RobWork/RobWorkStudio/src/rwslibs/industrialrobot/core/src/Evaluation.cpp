/**
 * @file   Evaluation.cpp
 * @brief  评估词表实现——四枚举的 token 冻结映射（switch 而非数组：枚举增删不静默错位）。
 *
 * 设计依据：
 *   - units/core.md §4.7（token 逐一列出）/§5.6（fromToken try 轨）
 *   - 任务契约 tasks/foundation/CORE-T06.json（UT-EVAL 载体）
 */

#include <sdurws/ird/core/Evaluation.hpp>

namespace sdurws::ird::core {

// ---- EvaluationMode：preview/quick/verified ----
const char* toToken(EvaluationMode v) noexcept
{
    switch (v) {
    case EvaluationMode::Preview:  return "preview";
    case EvaluationMode::Quick:    return "quick";
    case EvaluationMode::Verified: return "verified";
    }
    return "unknown";
}

std::optional<EvaluationMode> evaluationModeFromToken(std::string_view token) noexcept
{
    if (token == "preview")  { return EvaluationMode::Preview; }
    if (token == "quick")    { return EvaluationMode::Quick; }
    if (token == "verified") { return EvaluationMode::Verified; }
    return std::nullopt;
}

// ---- TaskOutcome：completed/canceled/failed/interrupted（信封轴） ----
const char* toToken(TaskOutcome v) noexcept
{
    switch (v) {
    case TaskOutcome::Completed:   return "completed";
    case TaskOutcome::Canceled:    return "canceled";
    case TaskOutcome::Failed:      return "failed";
    case TaskOutcome::Interrupted: return "interrupted";
    }
    return "unknown";
}

std::optional<TaskOutcome> taskOutcomeFromToken(std::string_view token) noexcept
{
    if (token == "completed")   { return TaskOutcome::Completed; }
    if (token == "canceled")    { return TaskOutcome::Canceled; }
    if (token == "failed")      { return TaskOutcome::Failed; }
    if (token == "interrupted") { return TaskOutcome::Interrupted; }
    return std::nullopt;
}

// ---- EngineeringStatus：feasible/engineering-infeasible/data-insufficient/not-applicable ----
const char* toToken(EngineeringStatus v) noexcept
{
    switch (v) {
    case EngineeringStatus::Feasible:             return "feasible";
    case EngineeringStatus::EngineeringInfeasible: return "engineering-infeasible";
    case EngineeringStatus::DataInsufficient:      return "data-insufficient";
    case EngineeringStatus::NotApplicable:         return "not-applicable";
    }
    return "unknown";
}

std::optional<EngineeringStatus> engineeringStatusFromToken(std::string_view token) noexcept
{
    if (token == "feasible")              { return EngineeringStatus::Feasible; }
    if (token == "engineering-infeasible") { return EngineeringStatus::EngineeringInfeasible; }
    if (token == "data-insufficient")      { return EngineeringStatus::DataInsufficient; }
    if (token == "not-applicable")         { return EngineeringStatus::NotApplicable; }
    return std::nullopt;
}

// ---- TaskState：九态小写连字符（ARCH §4.3 状态机轴） ----
const char* toToken(TaskState v) noexcept
{
    switch (v) {
    case TaskState::Queued:     return "queued";
    case TaskState::Preparing:  return "preparing";
    case TaskState::Running:    return "running";
    case TaskState::Paused:     return "paused";
    case TaskState::Canceling:  return "canceling";
    case TaskState::Canceled:   return "canceled";
    case TaskState::Completed:  return "completed";
    case TaskState::Failed:     return "failed";
    case TaskState::Interrupted: return "interrupted";
    }
    return "unknown";
}

std::optional<TaskState> taskStateFromToken(std::string_view token) noexcept
{
    if (token == "queued")      { return TaskState::Queued; }
    if (token == "preparing")   { return TaskState::Preparing; }
    if (token == "running")     { return TaskState::Running; }
    if (token == "paused")      { return TaskState::Paused; }
    if (token == "canceling")   { return TaskState::Canceling; }
    if (token == "canceled")    { return TaskState::Canceled; }
    if (token == "completed")   { return TaskState::Completed; }
    if (token == "failed")      { return TaskState::Failed; }
    if (token == "interrupted") { return TaskState::Interrupted; }
    return std::nullopt;
}

}  // namespace sdurws::ird::core
