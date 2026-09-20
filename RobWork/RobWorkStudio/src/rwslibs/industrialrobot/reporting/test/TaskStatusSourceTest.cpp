/**
 * @file   TaskStatusSourceTest.cpp
 * @brief  任务状态投影单元测试（RPT-T12）——try 轨 nullopt/"已中断"呈现
 *         数据源/终态↔终结原因对应/进度投影值形态/同身份同投影确定性
 *         （任务卡 RPT-T12 acceptance 3 的单元内具名用例——ITaskStatusSource
 *         最小接口的行为面）。
 *
 * 设计依据：
 *   - units/reporting.md §3.3（execution 投影注入形态——对齐 tryTask/
 *     progress 值形态）、§9.9 execution 行（任务状态引用＝TaskIdentity；
 *     任务状态呈现≠工程结论）、§2.1 C-7（TASK-02/NFR-REL-03 的任务投影
 *     半边）、§10.1（本组用例不在 RP-* 矩阵内——§11 卡行验证方式"单测"
 *     的具名承载）
 *   - 需求 TASK-02（任务终态呈现）、NFR-REL-03（中断任务显示"已中断"——
 *     不伪装为失败或通过）
 *   - 任务契约 tasks/foundation/RPT-T12.json acceptance 3（最小接口/值
 *     形态对齐/公共头零 execution 类型）/5（事件不入报告内容）
 *
 * 替身边界声明（§10.1 RP-STATE-4 同源——本文件全部用例共用）：
 *   本文件的局部夹具 ScriptedTaskStatusSource 输出仅验证 reporting 侧投影
 *   接口契约（try 轨/词表/终态纪律/值语义），**不构成** execution 状态机、
 *   调度或登记表行为的证明——那些归 execution 单元验证矩阵（EX-SM-*、
 *   EX-REG-* 等）；替身返回的投影值是测试脚本数据，不是 execution 事实。
 *   真实 tryTask/progress→ITaskStatusSource 适配归 L5 装配
 *   （TaskStatusSource.hpp 头内适配建议）。
 *
 * 线程约束：全部用例单线程（投影查询为并发只读安全面，测试无须并发）。
 */

#include <sdurws/ird/reporting/TaskStatusSource.hpp>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

namespace {

using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;
using sdurws::ird::core::TaskState;
using sdurws::ird::reporting::ITaskStatusSource;
using sdurws::ird::reporting::TaskProgressProjection;
using sdurws::ird::reporting::TaskStatusProjection;
using sdurws::ird::reporting::TaskTerminationKind;

// =====================================================================
// 测试数据助手（确定性铺位——固定字节，不用随机）
// =====================================================================

/// 以固定字节构造 Id128 族身份（逐字节 0xD0+seed——可辨识铺位）。
template <typename T>
T idWith(unsigned char seed)
{
    T id;
    id.bytes.fill(static_cast<std::uint8_t>(0xD0 + seed));
    return id;
}

/// 构造有效任务五元组（seed 区分不同任务——同 seed 恒同身份，确定性）。
TaskIdentity taskIdentityWith(unsigned char seed, AttemptId attempt = AttemptId{1})
{
    TaskIdentity t;
    t.project = idWith<ProjectId>(seed);
    t.branch = idWith<BranchId>(static_cast<unsigned char>(seed + 1));
    t.revision = idWith<RevisionId>(static_cast<unsigned char>(seed + 2));
    t.run = idWith<RunId>(static_cast<unsigned char>(seed + 3));
    t.attempt = attempt;
    return t;
}

// =====================================================================
// 局部夹具：ScriptedTaskStatusSource（脚本化替身——RPT-T12 局部夹具，
// 边界声明见文件头）
// =====================================================================

/**
 * @brief 脚本化任务状态源：按五元组查脚本表返回预置投影（或 nullopt）。
 *
 * 严格实现 ITaskStatusSource 契约后置（try 轨三支）：命中→忠实投影且仅
 * 终态携带 termination；未登记→nullopt。
 */
class ScriptedTaskStatusSource final : public ITaskStatusSource {
public:
    /// 脚本表：身份→投影（确定性——同身份恒同值）。
    std::optional<TaskStatusProjection> scripted;
    TaskIdentity boundIdentity{taskIdentityWith(1)};
    mutable int callCount = 0;  ///< 调用计数（观测面）

    std::optional<TaskStatusProjection> tryStatus(TaskIdentity task) const override
    {
        ++callCount;
        // 命中脚本身份→返回脚本值（值拷贝——深拷贝投影语义）。
        if (task == boundIdentity && scripted.has_value()) { return scripted; }
        // 未登记/不可解析→nullopt（不伪造占位状态）。
        return std::nullopt;
    }
};

/// 构造一个已中断任务的投影脚本值（NFR-REL-03 呈现数据源铺位）。
TaskStatusProjection interruptedProjection()
{
    TaskStatusProjection p;
    p.identity = taskIdentityWith(1);
    p.state = TaskState::Interrupted;  // 状态轴：已中断（core 九态之一）
    p.progress = TaskProgressProjection{40, "ik-batch", 40, 100};  // 中断时刻的最近进度
    p.termination = TaskTerminationKind::Interrupted;  // 原因轴：中断（仅终态非空）
    return p;
}

}  // namespace

// =====================================================================
// acceptance 3——ITaskStatusSource 最小接口的行为面
// =====================================================================

/// 任务未登记/不可解析＝nullopt（try 轨——不抛、不伪造占位状态）。
TEST(TaskStatusSource, UnknownIdentityReturnsNullopt_RPT_T12_ACC3)
{
    ScriptedTaskStatusSource source;
    source.scripted = interruptedProjection();

    const TaskIdentity unknown = taskIdentityWith(9);  // 脚本表外身份
    const std::optional<TaskStatusProjection> got = source.tryStatus(unknown);
    EXPECT_FALSE(got.has_value())
        << "未登记任务＝nullopt（不伪造占位状态——缺数据≠可呈现状态）";
}

/**
 * "已中断"呈现数据源（acceptance 3/TASK-02/NFR-REL-03——中断任务的投影
 * 携带 Interrupted 状态与中断原因，呈现层据此显示"已中断"，不得伪装为
 * 失败或通过）。
 */
TEST(TaskStatusSource, InterruptedTaskProjectionForPresentation_TASK02_REL03)
{
    ScriptedTaskStatusSource source;
    source.scripted = interruptedProjection();

    const std::optional<TaskStatusProjection> got = source.tryStatus(source.boundIdentity);
    ASSERT_TRUE(got.has_value());
    // 状态轴＝Interrupted（core 九态——中断是一种显式终态，不是 Failed）：
    EXPECT_EQ(got->state, TaskState::Interrupted);
    // 原因轴＝Interrupted（仅终态非空）：
    ASSERT_TRUE(got->termination.has_value());
    EXPECT_EQ(*got->termination, TaskTerminationKind::Interrupted);
    // 呈现词面锁定（"已中断"的数据源——token 与 core 终态词逐字一致）：
    EXPECT_STREQ(sdurws::ird::reporting::toToken(*got->termination), "interrupted");
    // 中断时刻的最近进度如实保留（不伪造 0% 或 100%）：
    ASSERT_TRUE(got->progress.has_value());
    EXPECT_EQ(got->progress->percent, 40);
}

/// 终态↔终结原因对应纪律：终态携带 termination、非终态不携带（实现方契约）。
TEST(TaskStatusSource, TerminationOnlyOnTerminalStates_RPT_T12_ACC3)
{
    // 非终态：Running——termination 必须为 nullopt（§9.9 会话内只读呈现）：
    ScriptedTaskStatusSource running;
    TaskStatusProjection runningProj;
    runningProj.identity = running.boundIdentity;
    runningProj.state = TaskState::Running;
    runningProj.progress = TaskProgressProjection{10, "preparing", 0, 0};
    runningProj.termination = std::nullopt;  // 非终态——无终结原因
    running.scripted = runningProj;

    const std::optional<TaskStatusProjection> gotRunning = running.tryStatus(running.boundIdentity);
    ASSERT_TRUE(gotRunning.has_value());
    EXPECT_FALSE(gotRunning->termination.has_value())
        << "非终态任务不得携带终结原因（TaskSnapshot 投影纪律同源）";

    // 强杀终态：state=Failed＋termination=ForceTerminated（两轴分立——
    // "不伪装为普通失败"的呈现承载，execution.md T13 同源）：
    ScriptedTaskStatusSource forced;
    TaskStatusProjection forcedProj;
    forcedProj.identity = forced.boundIdentity;
    forcedProj.state = TaskState::Failed;
    forcedProj.termination = TaskTerminationKind::ForceTerminated;
    forced.scripted = forcedProj;

    const std::optional<TaskStatusProjection> gotForced = forced.tryStatus(forced.boundIdentity);
    ASSERT_TRUE(gotForced.has_value());
    EXPECT_EQ(gotForced->state, TaskState::Failed);
    ASSERT_TRUE(gotForced->termination.has_value());
    EXPECT_EQ(*gotForced->termination, TaskTerminationKind::ForceTerminated);
    EXPECT_STREQ(sdurws::ird::reporting::toToken(*gotForced->termination), "force-terminated");
}

/// 进度投影值形态对齐（§3.3——四字段与 execution ProgressReport 同语义；
/// 未上报过进度＝nullopt 如实承载）。
TEST(TaskStatusSource, ProgressProjectionValueAlignment_RPT_T12_ACC3)
{
    ScriptedTaskStatusSource source;
    TaskStatusProjection proj;
    proj.identity = source.boundIdentity;
    proj.state = TaskState::Running;
    proj.progress = TaskProgressProjection{72, "optimize-gen", 36, 50};  // 四字段铺位
    proj.termination = std::nullopt;
    source.scripted = proj;

    const std::optional<TaskStatusProjection> got = source.tryStatus(source.boundIdentity);
    ASSERT_TRUE(got.has_value());
    ASSERT_TRUE(got->progress.has_value());
    // 四字段逐一（percent/phaseToken/batchesDone/batchesTotal——值语义对齐）：
    EXPECT_EQ(got->progress->percent, 72);
    EXPECT_EQ(got->progress->phaseToken, "optimize-gen");
    EXPECT_EQ(got->progress->batchesDone, 36u);
    EXPECT_EQ(got->progress->batchesTotal, 50u);

    // 未上报过进度→nullopt（不伪造 0% 进度——Queued 期如实形态）：
    ScriptedTaskStatusSource queued;
    TaskStatusProjection queuedProj;
    queuedProj.identity = queued.boundIdentity;
    queuedProj.state = TaskState::Queued;
    queuedProj.progress = std::nullopt;
    queuedProj.termination = std::nullopt;
    queued.scripted = queuedProj;
    const std::optional<TaskStatusProjection> gotQueued = queued.tryStatus(queued.boundIdentity);
    ASSERT_TRUE(gotQueued.has_value());
    EXPECT_FALSE(gotQueued->progress.has_value());
}

/// 同身份同投影（会话内只读一致视图——§9.9 execution 行；值拷贝语义：
/// 取回后源状态演化不影响已取回投影）。
TEST(TaskStatusSource, SameIdentitySameProjection_RPT_T12_ACC3)
{
    ScriptedTaskStatusSource source;
    source.scripted = interruptedProjection();

    const std::optional<TaskStatusProjection> first = source.tryStatus(source.boundIdentity);
    const std::optional<TaskStatusProjection> second = source.tryStatus(source.boundIdentity);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second) << "同身份两次查询必须得到相等投影（一致读取视图）";
    EXPECT_EQ(source.callCount, 2) << "两次查询均真实穿透到状态源（投影不缓存）";

    // 值拷贝语义：取回的投影与脚本源对象是独立副本（改脚本不影响已取回值
    // ——投影是快照不是引用）：
    TaskStatusProjection copy = *first;
    copy.state = TaskState::Completed;
    EXPECT_NE(copy, *first);
    EXPECT_EQ(source.scripted, *first) << "脚本源未被外部修改（深拷贝投影）";
}

/// 终结原因词表冻结（五值 token 互异——只增不改名；前四者与 core 终态词
/// 逐字一致、force-terminated 为原因轴单列标记）。
TEST(TaskStatusSource, TerminationTokenFrozenVocabulary_RPT_T12_ACC3)
{
    EXPECT_STREQ(sdurws::ird::reporting::toToken(TaskTerminationKind::Canceled), "canceled");
    EXPECT_STREQ(sdurws::ird::reporting::toToken(TaskTerminationKind::Failed), "failed");
    EXPECT_STREQ(sdurws::ird::reporting::toToken(TaskTerminationKind::Completed), "completed");
    EXPECT_STREQ(sdurws::ird::reporting::toToken(TaskTerminationKind::Interrupted), "interrupted");
    EXPECT_STREQ(sdurws::ird::reporting::toToken(TaskTerminationKind::ForceTerminated),
                 "force-terminated");
    // 五词互异（词表最小性——同词即同义，不允许同义双词）：
    EXPECT_STRNE(sdurws::ird::reporting::toToken(TaskTerminationKind::Failed),
                 sdurws::ird::reporting::toToken(TaskTerminationKind::ForceTerminated));
}
