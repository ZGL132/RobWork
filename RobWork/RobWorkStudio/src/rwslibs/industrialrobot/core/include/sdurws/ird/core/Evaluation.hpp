/**
 * @file   Evaluation.hpp
 * @brief  基础评估语义词表——EvaluationMode/TaskOutcome/EngineeringStatus/TaskState。
 *
 * 设计依据：
 *   - units/core.md §4.7（四词表值与 token 冻结、P-D-1 归属论证）、§5.6（签名）、
 *     §8 UT-EVAL（token 往返/未知空/NotApplicable 存在性）
 *   - 需求 EVI-01（§8.1 表 1 证据效力分层）、TASK-02（表 3 payload 约束——校验器
 *     归 evidence）、PM-03（ui 九态短标签的语义源）、ERR-01（NotApplicable 显式标记）
 *   - 任务契约 tasks/foundation/CORE-T03 同源 CR-01 纪律：core 只承载词表，
 *     判定规则不在此（语义以 REQUIREMENTS §8.1/§4.3 为准，本文不复述）
 *
 * 轴正交（§4.7 不变量）：TaskOutcome（信封结果轴）与 TaskState（状态机轴）的
 * 终态同名（Canceled/Completed/Failed/Interrupted）但属两个轴——映射归 execution
 * （§10.3 交接）；序列化只认 token（枚举无序语义）。
 *
 * 线程安全：纯函数/纯枚举，无共享状态。
 */

#ifndef SDURWS_IRD_CORE_EVALUATION_HPP
#define SDURWS_IRD_CORE_EVALUATION_HPP

#include <optional>
#include <string_view>

namespace sdurws::ird::core {

/// 评估模式（§8.1 表 1：证据效力分层——Quick 不得单独支撑正式通过、Preview 不产生
/// 正式证据与结果对象——约束校验归 evidence）。
enum class EvaluationMode { Preview, Quick, Verified };

/// 任务结果（§8.1 表 3：信封 outcome 轴——payload 约束与合法组合校验归 evidence）。
enum class TaskOutcome { Completed, Canceled, Failed, Interrupted };

/// 工程判定（§8.1 表 3＋ERR-01：NotApplicable＝取消/失败/中断的显式标记，不伪造判定）。
enum class EngineeringStatus { Feasible, EngineeringInfeasible, DataInsufficient, NotApplicable };

/// 任务状态机九态（ARCH §4.3：转移矩阵/能力声明/取消协议归 execution）。
enum class TaskState {
    Queued, Preparing, Running, Paused, Canceling,
    Canceled, Completed, Failed, Interrupted,
};

// ---- token 冻结表（§4.7：小写连字符；持久化契约——不改名） ----
const char* toToken(EvaluationMode v) noexcept;
const char* toToken(TaskOutcome v) noexcept;
const char* toToken(EngineeringStatus v) noexcept;
const char* toToken(TaskState v) noexcept;

/// token→枚举（try 轨：未知 token 返回 nullopt——io 反序列化错误收集用）。
std::optional<EvaluationMode> evaluationModeFromToken(std::string_view token) noexcept;
std::optional<TaskOutcome> taskOutcomeFromToken(std::string_view token) noexcept;
std::optional<EngineeringStatus> engineeringStatusFromToken(std::string_view token) noexcept;
std::optional<TaskState> taskStateFromToken(std::string_view token) noexcept;

}  // namespace sdurws::ird::core

#endif  // SDURWS_IRD_CORE_EVALUATION_HPP
