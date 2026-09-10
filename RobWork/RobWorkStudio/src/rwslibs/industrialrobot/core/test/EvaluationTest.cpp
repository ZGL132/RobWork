/**
 * @file   EvaluationTest.cpp
 * @brief  评估词表用例组——UT-EVAL（units/core.md §8）：四枚举 token 往返/
 *         未知 token 空/NotApplicable 存在性/Outcome 与 State 轴正交。
 *
 * 设计依据：
 *   - units/core.md §4.7（token 冻结表、轴正交不变量）、§5.6（fromToken try 轨）、
 *     §8 UT-EVAL 行
 *   - 需求 EVI-01/TASK-02/PM-03/ERR-01；任务契约 tasks/foundation/CORE-T06.json
 *     （acceptance：token 往返与 NotApplicable 存在性）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>

namespace {
using namespace sdurws::ird::core;

/** EvaluationMode：三 token 往返＋未知空（§5.6 示例 verified）。 */
TEST(EvaluationModeTokens, RoundtripAndUnknown_UT_EVAL)
{
    EXPECT_STREQ(toToken(EvaluationMode::Preview), "preview");
    EXPECT_STREQ(toToken(EvaluationMode::Quick), "quick");
    EXPECT_STREQ(toToken(EvaluationMode::Verified), "verified");
    for (const auto v : {EvaluationMode::Preview, EvaluationMode::Quick,
                         EvaluationMode::Verified}) {
        EXPECT_EQ(evaluationModeFromToken(toToken(v)), v);
    }
    EXPECT_EQ(evaluationModeFromToken("verified"), EvaluationMode::Verified);  // §5.6 示例
    EXPECT_EQ(evaluationModeFromToken("Verified"), std::nullopt);              // 区分大小写
    EXPECT_EQ(evaluationModeFromToken("no-such"), std::nullopt);
}

/** TaskOutcome：四 token 往返＋未知空（信封 outcome 轴）。 */
TEST(TaskOutcomeTokens, RoundtripAndUnknown_UT_EVAL)
{
    EXPECT_STREQ(toToken(TaskOutcome::Completed), "completed");
    EXPECT_STREQ(toToken(TaskOutcome::Canceled), "canceled");
    EXPECT_STREQ(toToken(TaskOutcome::Failed), "failed");
    EXPECT_STREQ(toToken(TaskOutcome::Interrupted), "interrupted");
    for (const auto v : {TaskOutcome::Completed, TaskOutcome::Canceled,
                         TaskOutcome::Failed, TaskOutcome::Interrupted}) {
        EXPECT_EQ(taskOutcomeFromToken(toToken(v)), v);
    }
    EXPECT_EQ(taskOutcomeFromToken("unknown-token"), std::nullopt);
}

/** EngineeringStatus：四 token 往返＋NotApplicable 存在性（§8 UT-EVAL 明文——
 *  ERR-01 显式标记，不伪造判定）。 */
TEST(EngineeringStatusTokens, RoundtripAndNotApplicablePresence_UT_EVAL)
{
    EXPECT_STREQ(toToken(EngineeringStatus::Feasible), "feasible");
    EXPECT_STREQ(toToken(EngineeringStatus::EngineeringInfeasible), "engineering-infeasible");
    EXPECT_STREQ(toToken(EngineeringStatus::DataInsufficient), "data-insufficient");
    EXPECT_STREQ(toToken(EngineeringStatus::NotApplicable), "not-applicable");
    for (const auto v : {EngineeringStatus::Feasible, EngineeringStatus::EngineeringInfeasible,
                         EngineeringStatus::DataInsufficient, EngineeringStatus::NotApplicable}) {
        EXPECT_EQ(engineeringStatusFromToken(toToken(v)), v);
    }
    // NotApplicable 存在性：显式可构造、token 可解析（表 3 显式标记语义）。
    const auto na = EngineeringStatus::NotApplicable;
    EXPECT_EQ(engineeringStatusFromToken("not-applicable"), na);
    EXPECT_EQ(engineeringStatusFromToken("data-insufficient"), EngineeringStatus::DataInsufficient);
    EXPECT_EQ(engineeringStatusFromToken("applicable"), std::nullopt);
}

/** TaskState：九态逐一往返＋未知空（ARCH §4.3 状态机轴）。 */
TEST(TaskStateTokens, NineStatesRoundtrip_UT_EVAL)
{
    EXPECT_STREQ(toToken(TaskState::Queued), "queued");
    EXPECT_STREQ(toToken(TaskState::Preparing), "preparing");
    EXPECT_STREQ(toToken(TaskState::Running), "running");
    EXPECT_STREQ(toToken(TaskState::Paused), "paused");
    EXPECT_STREQ(toToken(TaskState::Canceling), "canceling");
    EXPECT_STREQ(toToken(TaskState::Canceled), "canceled");
    EXPECT_STREQ(toToken(TaskState::Completed), "completed");
    EXPECT_STREQ(toToken(TaskState::Failed), "failed");
    EXPECT_STREQ(toToken(TaskState::Interrupted), "interrupted");
    for (const auto v : {TaskState::Queued, TaskState::Preparing, TaskState::Running,
                         TaskState::Paused, TaskState::Canceling, TaskState::Canceled,
                         TaskState::Completed, TaskState::Failed, TaskState::Interrupted}) {
        EXPECT_EQ(taskStateFromToken(toToken(v)), v);
    }
    EXPECT_EQ(taskStateFromToken("queued-but-not-really"), std::nullopt);
    EXPECT_EQ(taskStateFromToken("Queued"), std::nullopt);   // 区分大小写
}

/** 轴正交（§4.7 不变量）：Outcome 与 State 终态同名但属两轴——token 形态相同、
 *  枚举类型不同；映射归 execution（§10.3）。 */
TEST(AxisOrthogonality, OutcomeAndStateAreDistinctAxes_UT_EVAL)
{
    // 同名 token 字面一致（序列化形态）：
    EXPECT_STREQ(toToken(TaskOutcome::Failed), toToken(TaskState::Failed));
    EXPECT_STREQ(toToken(TaskOutcome::Completed), toToken(TaskState::Completed));
    // 但解析到不同枚举类型——类型层隔离（防串轴）。
    EXPECT_EQ(taskOutcomeFromToken("failed"), TaskOutcome::Failed);
    EXPECT_EQ(taskStateFromToken("failed"), TaskState::Failed);
    static_assert(!std::is_same_v<TaskOutcome, TaskState>,
                  "Outcome 与 State 必须是独立类型（轴正交，core.md §4.7）");
    // Mapping（Running→Failed 与 outcome=Failed 的对应）不在 core：无任何
    // 转换函数导出（本文件未提供 outcome/state 互转——行为面验证）。
    SUCCEED() << "轴正交成立：同名字段两轴独立承载";
}
}  // namespace
