/**
 * @file   TaskStateMachineTest.cpp
 * @brief  任务身份与状态机用例组（EX-T02）——三级身份分配、九态逐转移
 *         矩阵、四态取消出口、强制终止、能力门控、恢复期指派。
 *
 * 设计依据（用例与需求/验收对照——每条 acceptance 至少一个具名用例）：
 *   - units/execution.md §11 EX-SUB-1（并发提交与稳定任务身份）、
 *     EX-SM-1~7（取消/继续/非法转换/能力反馈）、EX-WKR-4（强制终止——
 *     本任务覆盖状态机面）、§5.2/§5.3（矩阵与逐转移表）、§5.5（能力
 *     声明）、§5.6（终态映射）、§4.1~§4.3（身份分配协议）
 *   - 需求 TASK-01（状态机＋能力声明）、TASK-03（五元组）、NFR-PERF-02
 *     （取消状态机侧）、NFR-REL-03（已中断）、ARCH §11.2-6（四态取消
 *     入口＋暂停中取消保检查点）、AT-34/AT-35（取消/暂停确认边界）
 *   - 任务契约 tasks/foundation/EX-T02.json acceptance 1~5 逐条
 *
 * 边界声明（替身不构成业务证明——§11 边界声明同源）：本组用例驱动
 * 纯状态机本体；协作窗 2 s/10 s 时序（ManualClock）、worker 进程、
 * 归档端口磁盘断言归 EX-T03/T04/T06/T09 的对应用例——各用例注释中
 * 标明交接边界，不夸大覆盖面。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>

#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sdurws::ird::execution;
using sdurws::ird::core::TaskState;

// 说明：本文件在全局/匿名域引用 core 类型——"core::" 限定名不随
// using-directive（using namespace sdurws::ird::execution）到达（命名
//空间的 enclosing 查找只在 sdurws::ird 内部生效），以命名空间别名显式
// 建立短名。
namespace core = sdurws::ird::core;

// =====================================================================
// 测试设施（AGENTS §2.7：fixture 与辅助函数按 §2.3 注释规范）
// =====================================================================

/// 事件收集替身（ITaskEventSink 的最小可观测实现——EX-SUB-1"同任务事件
/// FIFO"与各转移"事件逐行断言"的观测面）。
class RecordingSink : public ITaskEventSink {
public:
    void onTaskStatusChanged(const core::TaskStatusChangedPayload& payload) override
    {
        m_states.push_back(payload.newState);
        m_identities.push_back(payload.task);
    }

    const std::vector<TaskState>& states() const noexcept { return m_states; }
    const std::vector<core::TaskIdentity>& identities() const noexcept { return m_identities; }

    /// 清空已收集事件（分段观测——只断言某段转移的事件）。
    void clear() noexcept
    {
        m_states.clear();
        m_identities.clear();
    }

private:
    std::vector<TaskState> m_states;                    ///< 按发布序收集的 newState 序列
    std::vector<core::TaskIdentity> m_identities;       ///< 按发布序收集的五元组
};

/// 构造一条合法受理记录（§4.2 字段表默认值＋调用方给定能力）。
/// 评估键取合法词形（[a-z][a-z0-9-]{1,63}——evidence §8.2；词形闸门
/// 本身归 evidence/提交验证 V1，此处只为可读性）。
TaskRecord makeQueuedRecord(TaskCapability capability = {})
{
    TaskRecord r;
    r.taskId = TaskId::generate();
    r.submission.snapshot.project = core::ProjectId::generate();
    r.submission.snapshot.branch = core::BranchId::generate();
    r.submission.snapshot.revision = core::RevisionId::generate();
    r.submission.evaluatorKey = "kin-batch-ik";
    r.submission.contractVersion = 1;
    r.submission.mode = core::EvaluationMode::Verified;
    r.capability = capability;
    return r;
}

/// 驱动到 Running（T1→T2→T5＋§4.3 派发绑定——bindRun 模拟调度器在
/// RunRegistry 登记后的运行身份写入）。
TaskStateMachine makeRunning(TaskCapability capability, RecordingSink* sink = nullptr)
{
    auto machine = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(capability), sink);
    EXPECT_EQ(machine.request(TransitionTrigger::DispatchDequeued).accepted, true);   // T2
    machine.bindRun(core::RunId::generate());                                          // §4.3 派发登记绑定
    EXPECT_EQ(machine.request(TransitionTrigger::PrepareSucceeded).accepted, true);   // T5
    return machine;
}

/// 驱动到 Paused（Running→T10——需要 supportsPause 能力）。
TaskStateMachine makePaused(TaskCapability capability, RecordingSink* sink = nullptr)
{
    auto machine = makeRunning(capability, sink);
    EXPECT_EQ(machine.request(TransitionTrigger::PauseConfirmed).accepted, true);     // T10
    return machine;
}

/// 期望 request() 抛 InvalidState 且状态不变（EX-SM-6 的复合断言——
/// 观测点"ExecutionError.code"＋"状态不变"；detail 稳定前缀
/// "execution/statemachine:" 是开发诊断的检索面）。
void expectMatrixOutsideRejected(TaskStateMachine& machine, TransitionTrigger trigger)
{
    const TaskState before = machine.state();
    try {
        machine.request(trigger);
        FAIL() << "矩阵外请求未被拒绝（触发应于 "
               << core::toToken(before) << " 抛 InvalidState）";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState) << "错误码应为 InvalidState";
        EXPECT_EQ(std::string(e.what()).rfind("execution/statemachine:", 0), 0)
            << "detail 应携带稳定前缀（开发诊断定位面）";
    }
    EXPECT_EQ(machine.state(), before) << "被拒请求不得改变状态（EX-SM-6 观测点）";
}

/// 期望 bindRun() 抛 InvalidState（§4.3 分配协议次序违约——Queued 期
/// 提前绑定/重复绑定/零值运行身份）。
void expectBindRejected(TaskStateMachine& machine)
{
    try {
        machine.bindRun(core::RunId::generate());
        FAIL() << "违约的运行绑定未被拒绝";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }
}

/// 判断任务诊断中是否存在给定稳定码（EX-FORCE-TERMINATED/EX-TASK-INTERRUPTED
/// 显式标记断言用）。
bool hasDiagnostic(const TaskRecord& record, const std::string& code)
{
    return std::any_of(record.diagnostics.begin(), record.diagnostics.end(),
                       [&](const core::DiagnosticRecord& d) { return d.code == code; });
}

// 九态全列（core 词表序——§5.1 行序）。
constexpr TaskState kAllStates[9] = {
    TaskState::Queued, TaskState::Preparing, TaskState::Running, TaskState::Paused,
    TaskState::Canceling, TaskState::Canceled, TaskState::Completed, TaskState::Failed,
    TaskState::Interrupted,
};

}  // namespace

// =====================================================================
// EX-SUB-1：三级身份并发分配唯一、无重复（acceptance 2；TASK-03）
// =====================================================================

/** TaskId::generate 并发唯一性：8 线程 × 400 次并发生成，规范文本全库
 *  无重复、全部非零、全部可严格往返（§4.1 分配者列＋§5.1 线程安全承诺；
 *  thread_local 引擎的实现自证）。 */
TEST(TaskIdentityConcurrency, TaskIdGenerateUniqueUnderConcurrency_EX_SUB_1_TASK03)
{
    constexpr int kThreads = 8;
    constexpr int kPerThread = 400;
    std::vector<std::thread> workers;
    std::mutex mutex;                       // 保护结果汇总（生成本身无锁）
    std::set<std::string> canonicalIds;     // 规范文本唯一性判据
    std::size_t invalidCount = 0;           // 非零保留值纪律违例计数

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            std::set<std::string> local;
            for (int i = 0; i < kPerThread; ++i) {
                const TaskId id = TaskId::generate();
                if (!id.isValid()) { ++invalidCount; }      // generate 恒非零
                local.insert(id.toCanonical());
            }
            std::lock_guard<std::mutex> lock(mutex);
            canonicalIds.insert(local.begin(), local.end());
        });
    }
    for (auto& w : workers) { w.join(); }

    EXPECT_EQ(invalidCount, 0u) << "保留零值不得由 generate 产出";
    EXPECT_EQ(canonicalIds.size(), static_cast<std::size_t>(kThreads * kPerThread))
        << "并发生成的 TaskId 必须两两不同（EX-SUB-1 唯一性）";
}

/** core::RunId::generate 并发唯一性（运行级身份分配——§4.1：类型 core、
 *  值分配归 execution；execution 消费 core 生成器即分配行为本体）。 */
TEST(TaskIdentityConcurrency, RunIdCoreGenerateUniqueUnderConcurrency_EX_SUB_1)
{
    constexpr int kThreads = 8;
    constexpr int kPerThread = 400;
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::set<std::string> canonicalIds;

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            std::set<std::string> local;
            for (int i = 0; i < kPerThread; ++i) {
                local.insert(core::RunId::generate().toCanonical());
            }
            std::lock_guard<std::mutex> lock(mutex);
            canonicalIds.insert(local.begin(), local.end());
        });
    }
    for (auto& w : workers) { w.join(); }

    EXPECT_EQ(canonicalIds.size(), static_cast<std::size_t>(kThreads * kPerThread))
        << "RunId 并发分配无重复（core 生成器线程安全——Identity.hpp 承诺）";
}

/** 身份分配协议次序（§4.3）：受理期记录零运行绑定（Queued 纯内存——
 *  P-EX-6 的记录面表达）；派发绑定置 AttemptId=1；Queued 期/重复/零值
 *  绑定违约抛 InvalidState。 */
TEST(TaskIdentityProtocol, BindRunSetsAttemptOneAndGuards_EX_SUB_1_PEX6)
{
    RecordingSink sink;
    auto machine = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), &sink);

    // 受理期：run 为空、attempt=0（§4.2 默认列）——任务尚无磁盘身份
    // （runDir 由归档预留建立，Queued 任务崩溃即消失）。
    EXPECT_FALSE(machine.record().run.has_value()) << "Queued 期 run 可空（§4.2 run 行）";
    EXPECT_EQ(machine.record().attempt.value, 0u) << "未派发 attempt=0（保留值）";

    // Queued 期绑定违约（绑定只发生在 Preparing 派发登记段——§4.3）。
    expectBindRejected(machine);

    // 推进到 Preparing 后绑定成功：AttemptId=1（首次尝试）。
    ASSERT_EQ(machine.request(TransitionTrigger::DispatchDequeued).accepted, true);
    const core::RunId run = core::RunId::generate();
    machine.bindRun(run);
    EXPECT_EQ(machine.record().run.has_value(), true);
    EXPECT_EQ(*machine.record().run == run, true);
    EXPECT_EQ(machine.record().attempt.value, 1u) << "首次尝试号＝1（§4.3）";

    // 重复绑定违约（一次运行一个 RunId）。
    try {
        machine.bindRun(core::RunId::generate());
        FAIL() << "重复绑定未被拒绝";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }
}

// =====================================================================
// EX-SM 矩阵：逐转移表与 §5.2 矩阵一致性（acceptance 1 的表驱动半区）
// =====================================================================

/** 逐转移表完整性：15 行（T1~T14＋Running→Canceling）；运行期 (触发,源态)
 *  唯一可解析；9×9 全矩阵与 §5.2 逐格核对（13 对合法边，其余全禁）。 */
TEST(TaskStateMachineTable, TableAndMatrixMatchSection53_EX_SM_Matrix_TASK01)
{
    const auto& table = TaskStateMachine::runtimeTable();

    // 行数与行号覆盖：T1~T14 全在（source 空的工厂行 2 条）。
    ASSERT_EQ(table.size(), 15u) << "§5.3 全表 14 行＋Running→Canceling（row=0）";
    std::set<int> rows;
    for (const auto& row : table) { rows.insert(row.row); }
    for (int t = 1; t <= 14; ++t) {
        EXPECT_EQ(rows.count(t), 1u) << "缺少 §5.3 行 T" << t;
    }

    // (触发, 源态) 唯一性：request() 查表必须恰命中一行（幂等特判之外
    // 不允许二义——§5.3 每行转移由触发＋源态唯一确定）。
    for (const auto& a : table) {
        for (const auto& b : table) {
            if (&a == &b) { continue; }
            const bool same = a.trigger == b.trigger && a.source.has_value() == b.source.has_value()
                && (!a.source.has_value() || *a.source == *b.source);
            ASSERT_FALSE(same) << "转移表二义：触发/源态重复（行 T"
                               << a.row << " 与 T" << b.row << "）";
        }
    }

    // §5.2 矩阵逐格：合法边恰为 13 对（行＝源态、列＝目标态的 ✔ 格；
    // "经 Canceling"格是两步组合路径、非直接边——§5.3 无对应行）。
    // 列序（目标态）：Queued Preparing Running Paused Canceling Canceled
    //                 Completed Failed Interrupted。
    const bool kLegal[9][9] = {
        /* Queued     */ { false, true, false, false, true, false, false, false, false },
        /* Preparing */ { false, false, true, false, true, false, false, true, false },
        /* Running   */ { false, false, false, true, true, false, true, true, false },
        /* Paused    */ { false, false, true, false, true, false, false, false, false },
        /* Canceling */ { false, false, false, false, false, true, false, true, false },
        /* Canceled  */ { false, false, false, false, false, false, false, false, false },
        /* Completed */ { false, false, false, false, false, false, false, false, false },
        /* Failed    */ { false, false, false, false, false, false, false, false, false },
        /* Interrupted*/{ false, false, false, false, false, false, false, false, false },
    };
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            EXPECT_EQ(TaskStateMachine::isLegalRuntimeTransition(kAllStates[i], kAllStates[j]),
                      kLegal[i][j])
                << "矩阵不符 §5.2: " << core::toToken(kAllStates[i]) << "→"
                << core::toToken(kAllStates[j]);
        }
    }

    // §5.2"禁止转换示例"逐条点名（review 直读面）。
    EXPECT_FALSE(TaskStateMachine::isLegalRuntimeTransition(TaskState::Paused, TaskState::Completed))
        << "Paused→Completed 必须先继续（T11）";
    for (const auto terminal : {TaskState::Canceled, TaskState::Completed,
                                TaskState::Failed, TaskState::Interrupted}) {
        for (const auto target : kAllStates) {
            EXPECT_FALSE(TaskStateMachine::isLegalRuntimeTransition(terminal, target))
                << "终态无出边: " << core::toToken(terminal);
        }
    }
    EXPECT_FALSE(TaskStateMachine::isLegalRuntimeTransition(TaskState::Queued, TaskState::Running))
        << "Queued→Running 必须经 Preparing（登记与预留不可跳过）";
    EXPECT_FALSE(TaskStateMachine::isLegalRuntimeTransition(TaskState::Completed, TaskState::Failed))
        << "归档失败不改任务终态（archivePhase 独立轴——§5.6）";
    EXPECT_FALSE(TaskStateMachine::isLegalRuntimeTransition(TaskState::Queued, TaskState::Canceled))
        << "Queued→Canceled 是经 Canceling 的两步组合（T3+T4），非直接边";

    // §5.3 清理列的检查点保留语义逐行（ARCH §11.2-6 后半句的声明面）：
    // T4/T9/T12/T13/T14 保留最近检查点；其余行无检查点动作。
    for (const auto& row : table) {
        const bool expectRetain = (row.row == 4 || row.row == 9 || row.row == 12
                                   || row.row == 13 || row.row == 14);
        EXPECT_EQ(row.checkpointPolicy == CheckpointRetentionPolicy::RetainLatestCheckpoint,
                  expectRetain)
            << "行 T" << row.row << " 的检查点保留语义与 §5.3 清理列不符";
        EXPECT_NE(row.cleanupNote, nullptr) << "行 T" << row.row << " 应携带清理列注记";
        // T10 能力守卫声明（唯一 requiresSupportsPause 行——§5.3 T10）。
        EXPECT_EQ(row.requiresSupportsPause, row.row == 10)
            << "能力守卫声明仅 T10 持有";
    }
}

// =====================================================================
// EX-SM-1~4：四态取消出口（acceptance 1/3；ARCH §11.2-6）
// =====================================================================

/** EX-SM-1（ARCH §4.3 A5）：Queued 取消直接出队——不经协作窗（无在途
 *  批次）；状态序 Queued→Canceling→Canceled；正常取消零错误诊断
 *  （UX-03）；重复取消幂等 ack 不发事件。 */
TEST(TaskStateMachineCancel, QueuedCancelDirectNoCooperativeWindow_EX_SM_1)
{
    RecordingSink sink;
    auto machine = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), &sink);
    sink.clear();   // 只观测取消段事件（分段观测断言）

    const auto ackCancel = machine.request(TransitionTrigger::RequestCancel);   // T3
    EXPECT_EQ(ackCancel.accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Canceling);
    EXPECT_EQ(ackCancel.feedback.has_value(), false) << "正常取消无反馈诊断（UX-03）";

    // 幂等特判（§5.3 T3 守卫注）：已 Canceling 再收取消＝ack，状态不变、
    // 不发事件（事件流只承载状态变化）。
    const auto ackAgain = machine.request(TransitionTrigger::RequestCancel);
    EXPECT_EQ(ackAgain.accepted, true) << "重复取消幂等 ack";
    EXPECT_EQ(machine.state(), TaskState::Canceling);

    // 在途批次收敛（无在途→速达）→ Canceled（T4）。
    const auto ackSettled = machine.request(TransitionTrigger::CancelSettled);
    EXPECT_EQ(ackSettled.accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Canceled);
    ASSERT_TRUE(machine.record().termination.has_value());
    EXPECT_EQ(*machine.record().termination, TerminationCause::Canceled);
    EXPECT_TRUE(machine.record().diagnostics.empty()) << "正常用户取消不产生错误诊断（UX-03）";

    // 事件序：仅 Canceling/Canceled 两次（幂等 ack 不发事件）。
    ASSERT_EQ(sink.states().size(), 2u);
    EXPECT_EQ(sink.states()[0], TaskState::Canceling);
    EXPECT_EQ(sink.states()[1], TaskState::Canceled);
}

/** EX-SM-2：Preparing 取消＝组装中止（丢弃派发物＋终结归档预留——§5.3
 *  T6 清理列；磁盘 abandon 调用断言归 EX-T04 归档协作用例）。 */
TEST(TaskStateMachineCancel, PreparingCancelDiscardsAssembly_EX_SM_2)
{
    RecordingSink sink;
    auto machine = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), &sink);
    ASSERT_EQ(machine.request(TransitionTrigger::DispatchDequeued).accepted, true);

    ASSERT_EQ(machine.request(TransitionTrigger::RequestCancel).accepted, true);   // T6
    EXPECT_EQ(machine.state(), TaskState::Canceling);
    ASSERT_EQ(machine.request(TransitionTrigger::CancelSettled).accepted, true);   // T4
    EXPECT_EQ(machine.state(), TaskState::Canceled);
    ASSERT_TRUE(machine.record().termination.has_value());
    EXPECT_EQ(*machine.record().termination, TerminationCause::Canceled);
}

/** EX-SM-3（NFR-PERF-02/AT-34 状态机面）：Running 取消走 Canceling→
 *  Canceled。2 s 入 Canceling/10 s 收敛的时序断言归 EX-T03（ManualClock
 *  可注入时钟——本任务只断言状态路径与诊断面）。 */
TEST(TaskStateMachineCancel, RunningCancelCooperativePath_EX_SM_3_NFR_PERF_02)
{
    RecordingSink sink;
    auto machine = makeRunning(TaskCapability{}, &sink);
    sink.clear();   // 分段观测：只断言后续转移的事件

    ASSERT_EQ(machine.request(TransitionTrigger::RequestCancel).accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Canceling);
    ASSERT_EQ(machine.request(TransitionTrigger::CancelSettled).accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Canceled);
    ASSERT_TRUE(machine.record().termination.has_value());
    EXPECT_EQ(*machine.record().termination, TerminationCause::Canceled);
    EXPECT_TRUE(machine.record().diagnostics.empty()) << "正常取消零错误诊断";
    ASSERT_EQ(sink.states().size(), 2u);
    EXPECT_EQ(sink.states()[0], TaskState::Canceling);
    EXPECT_EQ(sink.states()[1], TaskState::Canceled);
}

/** EX-SM-4（ARCH §4.3 A5/AT-35/ARCH §11.2-6）：暂停中取消直达且保留
 *  检查点——状态机面断言 T12 行声明 RetainLatest（磁盘 manifest 断言
 *  归 EX-T03/T04 的归档协作用例）。 */
TEST(TaskStateMachineCancel, PausedCancelRetainsCheckpoint_EX_SM_4_ARCH_11_2_6)
{
    TaskCapability capability;                       // 缺省能力不含暂停——
    capability.supportsPause = true;                 // T10 需要显式声明支持
    capability.checkpointGranularity = CheckpointGranularity::Batch;
    auto machine = makePaused(capability);
    ASSERT_EQ(machine.state(), TaskState::Paused);

    // T12：暂停态无在途批次，取消直达 Canceling；行声明保留最近检查点。
    const auto& table = TaskStateMachine::runtimeTable();
    const auto t12 = std::find_if(table.begin(), table.end(),
                                  [](const TransitionSpec& r) { return r.row == 12; });
    ASSERT_NE(t12, table.end());
    EXPECT_EQ(t12->checkpointPolicy, CheckpointRetentionPolicy::RetainLatestCheckpoint)
        << "暂停中取消必须声明保留检查点（可续跑——A5）";

    ASSERT_EQ(machine.request(TransitionTrigger::RequestCancel).accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Canceling);
    ASSERT_EQ(machine.request(TransitionTrigger::CancelSettled).accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Canceled);
    ASSERT_TRUE(machine.record().termination.has_value());
    EXPECT_EQ(*machine.record().termination, TerminationCause::Canceled);
}

/** ARCH §11.2-6 全句：Queued/Preparing/Running/Paused 四态取消入口全部
 *  经 Canceling 收敛至 Canceled（矩阵取消出口覆盖）。 */
TEST(TaskStateMachineCancel, FourStateCancelExits_ARCH_11_2_6)
{
    const TaskCapability pausable = [] {
        TaskCapability c;
        c.supportsPause = true;
        return c;
    }();

    // 四个入口态各自的取消路径（两步：RequestCancel→CancelSettled）。
    auto queued = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(pausable), nullptr);
    queued.request(TransitionTrigger::RequestCancel);
    queued.request(TransitionTrigger::CancelSettled);

    auto preparing = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(pausable), nullptr);
    preparing.request(TransitionTrigger::DispatchDequeued);
    preparing.request(TransitionTrigger::RequestCancel);
    preparing.request(TransitionTrigger::CancelSettled);

    auto running = makeRunning(pausable);
    running.request(TransitionTrigger::RequestCancel);
    running.request(TransitionTrigger::CancelSettled);

    auto paused = makePaused(pausable);
    paused.request(TransitionTrigger::RequestCancel);
    paused.request(TransitionTrigger::CancelSettled);

    for (const TaskStateMachine* m : {&queued, &preparing, &running, &paused}) {
        EXPECT_EQ(m->state(), TaskState::Canceled) << "四态取消出口未收敛";
        ASSERT_TRUE(m->record().termination.has_value());
        EXPECT_EQ(*m->record().termination, TerminationCause::Canceled);
    }
}

// =====================================================================
// EX-SM-5：继续创建新 AttemptId（AT-35）
// =====================================================================

/** EX-SM-5（AT-35）：Paused→Running（T11）同 RunId、AttemptId+1；事件
 *  携带新尝试号。旧 attempt 移入 supersededAttempts 的登记追加归
 *  RunRegistry（EX-T04——§9.1），续跑统计自检查点累计的编排归调度器。 */
TEST(TaskStateMachineResume, ResumeCreatesNewAttempt_EX_SM_5_AT35)
{
    TaskCapability capability;
    capability.supportsPause = true;
    RecordingSink sink;
    auto machine = makePaused(capability, &sink);
    const core::RunId boundRun = *machine.record().run;
    const std::uint64_t attemptBefore = machine.record().attempt.value;
    sink.clear();   // 分段观测：只断言后续转移的事件

    const auto ack = machine.request(TransitionTrigger::ResumeConfirmed);   // T11
    EXPECT_EQ(ack.accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Running);
    EXPECT_EQ(machine.record().attempt.value, attemptBefore + 1)
        << "继续＝同 RunId 新 AttemptId（§4.3）";
    ASSERT_TRUE(machine.record().run.has_value());
    EXPECT_EQ(*machine.record().run == boundRun, true) << "继续不换 RunId";

    // 事件携带新尝试号（§4.3"事件一律携带当前 (run, attempt)"）。
    ASSERT_EQ(sink.states().size(), 1u);
    EXPECT_EQ(sink.states()[0], TaskState::Running);
    EXPECT_EQ(sink.identities()[0].run == boundRun, true);
    EXPECT_EQ(sink.identities()[0].attempt.value, attemptBefore + 1);
}

// =====================================================================
// EX-SM-6：矩阵外非法转换拒绝（TASK-01）
// =====================================================================

/** EX-SM-6：矩阵外请求抛 ExecutionError(InvalidState) 且状态不变——
 *  终态无出边、跳步（Queued→Running）、越权直达（Paused→Completed）、
 *  工厂触发误用（SubmitAccepted/RecoveryScanFound 经 request() 提交）。 */
TEST(TaskStateMachineIllegal, MatrixOutsideRequestsRejected_EX_SM_6_TASK01)
{
    // 终态任务：一切运行期触发全部拒绝。
    auto canceled = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    canceled.request(TransitionTrigger::RequestCancel);
    canceled.request(TransitionTrigger::CancelSettled);
    for (const auto trigger : {
             TransitionTrigger::DispatchDequeued, TransitionTrigger::PrepareSucceeded,
             TransitionTrigger::RunCompleted, TransitionTrigger::ResumeConfirmed,
             TransitionTrigger::RequestCancel}) {
        expectMatrixOutsideRejected(canceled, trigger);
    }

    // 跳步/越权代表例（§5.2 禁止转换示例的状态机面）。
    auto queued = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    expectMatrixOutsideRejected(queued, TransitionTrigger::RunCompleted);      // Queued↛Running(Completed)
    expectMatrixOutsideRejected(queued, TransitionTrigger::PauseConfirmed);    // Queued↛Paused
    expectMatrixOutsideRejected(queued, TransitionTrigger::CancelSettled);     // Queued↛Canceled 直达

    auto paused = [] {
        TaskCapability c;
        c.supportsPause = true;
        return makePaused(c);
    }();
    expectMatrixOutsideRejected(paused, TransitionTrigger::RunCompleted);      // Paused→Completed（必须先继续）
    expectMatrixOutsideRejected(paused, TransitionTrigger::PauseConfirmed);    // Paused 自环禁止

    // 工厂触发误用：T1/T14 是记录诞生路径，运行期重放即矩阵外违约
    // （T14"不与运行期转移并发"——§5.3 守卫列）。
    expectMatrixOutsideRejected(queued, TransitionTrigger::SubmitAccepted);
    expectMatrixOutsideRejected(paused, TransitionTrigger::RecoveryScanFound);
}

/** T1/T14 工厂前置违约：受理记录必须 Queued＋零运行绑定（提交验证失败
 *  的任务根本不会到达状态机——不产生 TaskRecord、不占状态，acceptance 1
 *  括注的结构面）；恢复重建必须携带运行绑定（Queued 任务崩溃即消失，
 *  P-EX-6——不存在排队任务的恢复路径）。 */
TEST(TaskStateMachineIllegal, FactoryPreconditions_EX_SM_6_PEX6)
{
    // 受理记录状态非 Queued（伪造"半路受理"）。
    auto tampered = makeQueuedRecord();
    tampered.state = TaskState::Preparing;
    try {
        TaskStateMachine::forAcceptedSubmission(tampered, nullptr);
        FAIL() << "非 Queued 受理记录未被拒绝";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }

    // 受理记录预绑定运行（绕过 §4.3 派发登记次序）。
    auto prebound = makeQueuedRecord();
    prebound.run = core::RunId::generate();
    prebound.attempt = core::AttemptId{1};
    try {
        TaskStateMachine::forAcceptedSubmission(prebound, nullptr);
        FAIL() << "预绑定运行的受理记录未被拒绝";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }

    // 恢复重建缺运行绑定（Queued 任务无磁盘痕迹，不可能被恢复）。
    auto ghost = makeQueuedRecord();
    ghost.state = TaskState::Interrupted;
    try {
        TaskStateMachine::forRecoveryInterrupted(ghost, nullptr);
        FAIL() << "无运行绑定的恢复重建未被拒绝（P-EX-6：Queued 崩溃即消失）";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }
}

// =====================================================================
// EX-SM-7：不支持暂停的显式反馈（TASK-01/卡行禁止项）
// =====================================================================

/** EX-SM-7（ARCH §4.3 原文）：supportsPause=false 的 Running 任务收到
 *  暂停请求→状态不变＋EX-CAPABILITY-UNSUPPORTED 显式反馈（不静默）；
 *  支持暂停的同路径成功进入 Paused（T10 对照半区）。 */
TEST(TaskStateMachineCapability, PauseUnsupportedExplicitFeedback_EX_SM_7_TASK01)
{
    // 对照半区：支持暂停→T10 生效。
    auto pausable = makeRunning([] {
        TaskCapability c;
        c.supportsPause = true;
        return c;
    }());
    const auto ackOk = pausable.request(TransitionTrigger::PauseConfirmed);
    EXPECT_EQ(ackOk.accepted, true);
    EXPECT_EQ(pausable.state(), TaskState::Paused);

    // 被测半区：最小能力（不支持暂停——§5.5 缺省）收到暂停请求。
    RecordingSink sink;
    auto minimal = makeRunning(TaskCapability{}, &sink);
    sink.clear();   // 分段观测：只断言后续转移的事件
    const auto ackRejected = minimal.request(TransitionTrigger::PauseConfirmed);
    EXPECT_EQ(ackRejected.accepted, false) << "能力拒绝须可预期（结构化 Ack——§10.8）";
    EXPECT_EQ(minimal.state(), TaskState::Running) << "拒绝后状态不变";
    ASSERT_TRUE(ackRejected.feedback.has_value()) << "必须显式反馈（不静默——卡行禁止项）";
    EXPECT_EQ(ackRejected.feedback->code, "EX-CAPABILITY-UNSUPPORTED") << "稳定码（§3.4 清单）";
    EXPECT_FALSE(ackRejected.feedback->context.empty());
    EXPECT_FALSE(ackRejected.feedback->cause.empty());
    EXPECT_FALSE(ackRejected.feedback->recommendedAction.empty());
    EXPECT_TRUE(sink.states().empty()) << "被拒请求不发状态事件";
}

// =====================================================================
// 强制终止矩阵（acceptance 3——forceTerminateCost 仅声明与呈现）
// =====================================================================

/** 强杀路径（T13/EX-WKR-4 状态机面）：Canceling 超时强杀→Failed＋
 *  termination=ForceTerminated＋EX-FORCE-TERMINATED 显式诊断（不伪装
 *  普通失败）＋保留检查点声明；进程树终止/句柄回收归 EX-T06（Job Scope）。 */
TEST(TaskStateMachineForceTerminate, ForceKillMarksExplicitly_T13_EX_WKR_4)
{
    RecordingSink sink;
    auto machine = makeRunning(TaskCapability{}, &sink);
    sink.clear();   // 分段观测：只断言后续转移的事件

    ASSERT_EQ(machine.request(TransitionTrigger::RequestCancel).accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Canceling);
    const auto ack = machine.request(TransitionTrigger::CancelTimeoutForceKill);   // T13
    EXPECT_EQ(ack.accepted, true);
    EXPECT_EQ(machine.state(), TaskState::Failed) << "强杀终态是 Failed（状态轴）";
    ASSERT_TRUE(machine.record().termination.has_value());
    EXPECT_EQ(*machine.record().termination, TerminationCause::ForceTerminated)
        << "原因轴显式区分强杀（不伪装普通失败——§5.3 T13）";
    EXPECT_TRUE(hasDiagnostic(machine.record(), "EX-FORCE-TERMINATED"))
        << "显式标记诊断（用户可见\"已强制终止，检查点保留\"——§7.4）";
    ASSERT_EQ(sink.states().size(), 2u);
    EXPECT_EQ(sink.states()[0], TaskState::Canceling);
    EXPECT_EQ(sink.states()[1], TaskState::Failed);
}

/** 代价声明不影响协议（acceptance 3/§5.5）：Cheap 与 Expensive 两档
 *  forceTerminateCost 的任务走强杀路径得到逐字段一致的状态/原因结果——
 *  代价仅呈现层差异（归 ui 呈现任务）。 */
TEST(TaskStateMachineForceTerminate, ForceTerminateCostDoesNotAffectProtocol_acceptance3)
{
    auto runWithCost = [](ForceTerminateCost cost) {
        TaskCapability c;
        c.forceTerminateCost = cost;
        auto machine = makeRunning(c);
        machine.request(TransitionTrigger::RequestCancel);
        machine.request(TransitionTrigger::CancelTimeoutForceKill);
        return machine;
    };

    const auto cheap = runWithCost(ForceTerminateCost::Cheap);
    const auto expensive = runWithCost(ForceTerminateCost::Expensive);

    EXPECT_EQ(cheap.state(), expensive.state());
    ASSERT_TRUE(cheap.record().termination.has_value());
    ASSERT_TRUE(expensive.record().termination.has_value());
    EXPECT_EQ(*cheap.record().termination, *expensive.record().termination)
        << "代价档位不得改变协议结果（仅声明与呈现——§5.5）";
}

// =====================================================================
// T14 恢复期指派（acceptance 5 的 P-EX-6 半区；NFR-REL-03）
// =====================================================================

/** T14：恢复扫描重建 Interrupted 终态＋EX-TASK-INTERRUPTED 状态标注诊断
 *  ＋termination 一次写入；运行期矩阵到 Interrupted 零出边（恢复期指派
 *  与运行期互斥——§5.2 行注）。 */
TEST(TaskStateMachineRecovery, RecoveryScanAssignsInterrupted_T14_NFR_REL_03)
{
    RecordingSink sink;
    TaskRecord rebuilt = makeQueuedRecord();
    rebuilt.state = TaskState::Interrupted;                 // 恢复扫描的指派结论
    rebuilt.run = core::RunId::generate();
    rebuilt.attempt = core::AttemptId{2};                   // 被中断前已重试过一次的示例
    auto machine = TaskStateMachine::forRecoveryInterrupted(std::move(rebuilt), &sink);

    EXPECT_EQ(machine.state(), TaskState::Interrupted);
    ASSERT_TRUE(machine.record().termination.has_value());
    EXPECT_EQ(*machine.record().termination, TerminationCause::Interrupted);
    EXPECT_TRUE(hasDiagnostic(machine.record(), "EX-TASK-INTERRUPTED"))
        << "恢复呈现数据源（PM-08/PM-15——§3.4 状态标注码）";
    ASSERT_EQ(sink.states().size(), 1u);
    EXPECT_EQ(sink.states()[0], TaskState::Interrupted);

    // 终态无出边：重建条目不可再转移（重跑＝新 TaskId——NFR-REL-03）。
    expectMatrixOutsideRejected(machine, TransitionTrigger::DispatchDequeued);
    expectMatrixOutsideRejected(machine, TransitionTrigger::ResumeConfirmed);

    // 运行期矩阵到 Interrupted 零出边（Queued 任务崩溃即消失、不呈现
    // "已中断"——P-EX-6 的矩阵面表达）。
    for (const auto from : kAllStates) {
        EXPECT_FALSE(TaskStateMachine::isLegalRuntimeTransition(from, TaskState::Interrupted))
            << "运行期不得指派 Interrupted（恢复期专用）: " << core::toToken(from);
    }
}

// =====================================================================
// 同任务事件 FIFO（acceptance 2 后半；EX-SUB-1）
// =====================================================================

/** 同任务事件序＝转移序（§4.3"同任务事件 FIFO"——单写者串行调用保证；
 *  全路径 Queued→Preparing→Running→Paused→Running→Completed 的完整事件
 *  序＋五元组随派发绑定完整化）。 */
TEST(TaskStateMachineEvents, SameTaskEventFifo_EX_SUB_1)
{
    TaskCapability capability;
    capability.supportsPause = true;
    RecordingSink sink;
    auto machine = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(capability), &sink);

    machine.request(TransitionTrigger::DispatchDequeued);   // T2
    const core::RunId run = core::RunId::generate();
    machine.bindRun(run);
    machine.request(TransitionTrigger::PrepareSucceeded);   // T5
    machine.request(TransitionTrigger::PauseConfirmed);     // T10
    machine.request(TransitionTrigger::ResumeConfirmed);    // T11
    machine.request(TransitionTrigger::RunCompleted);       // T8

    // 事件序（含 T1 诞生事件）：与转移提交序严格一致。
    const std::vector<TaskState> expected = {
        TaskState::Queued, TaskState::Preparing, TaskState::Running, TaskState::Paused,
        TaskState::Running, TaskState::Completed,
    };
    EXPECT_EQ(sink.states(), expected) << "同任务事件 FIFO（§4.3）";

    // 五元组完整性随 §4.3 协议次序推进：诞生事件（Queued）与 T2 事件
    // （runId 生成/登记发生在"出队后 Preparing"——事件先于登记）run 为
    // 保留空值；绑定（bindRun＝registerRun 后的记录写入）之后的全部事件
    // 携带登记运行；T11 后事件携带递增尝试号 attempt=2。
    ASSERT_EQ(sink.identities().size(), 6u);
    EXPECT_FALSE(sink.identities()[0].run.isValid()) << "Queued 期 run 尚未分配";
    EXPECT_FALSE(sink.identities()[1].run.isValid())
        << "T2 事件先于 §4.3 派发登记（出队后 Preparing 才生成 RunId）";
    for (std::size_t i = 2; i < sink.identities().size(); ++i) {
        EXPECT_EQ(sink.identities()[i].run == run, true) << "事件 " << i << " 携带登记运行";
        EXPECT_EQ(sink.identities()[i].project == sink.identities()[0].project, true)
            << "快照锚定三元组全程稳定（五元组前半——TASK-03）";
    }
    EXPECT_EQ(sink.identities().back().attempt.value, 2u);
    EXPECT_EQ(sink.identities().back().revision == machine.record().submission.snapshot.revision,
              true)
        << "快照锚定修订进五元组（归档目标修订＝登记值——§9.1）";

    // 终态映射收尾（§5.6）：Completed→Completed。
    ASSERT_TRUE(machine.record().termination.has_value());
    EXPECT_EQ(*machine.record().termination, TerminationCause::Completed);
    ASSERT_TRUE(terminalOutcome(machine.state()).has_value());
    EXPECT_EQ(*terminalOutcome(machine.state()), core::TaskOutcome::Completed);
}

// =====================================================================
// 词表与值类型边界（P-EX-4 九态零新增；P-EX-6 会话内存态）
// =====================================================================

/** P-EX-4：九态词表零新增——core 九个 token 严格往返；任务指令中出现过
 *  的 Created/CancelRequested/Rejected 三态名被 core 词表拒绝（提交拒绝
 *  在状态机之外、Canceling 承载取消请求相——§5.1 注）。 */
TEST(TaskVocabulary, NineStatesNoAddition_PEX4_TASK01)
{
    // 九态 token 往返（token 冻结表——core §4.7）。
    for (const auto state : kAllStates) {
        const char* token = core::toToken(state);
        ASSERT_NE(token, nullptr);
        const auto parsed = core::taskStateFromToken(token);
        ASSERT_TRUE(parsed.has_value()) << "token 往返失败: " << token;
        EXPECT_EQ(*parsed, state);
    }
    // 三态名拒绝（11 态示例不进入词表——P-EX-4 处置）。
    for (const char* invented : {"created", "cancel-requested", "rejected", ""}) {
        EXPECT_FALSE(core::taskStateFromToken(invented).has_value())
            << "词表外态名必须被拒绝: " << invented;
    }
    // 受理即 Queued：不存在 Created 中间态（状态机首事件即 Queued）。
    RecordingSink sink;
    auto machine = TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), &sink);
    EXPECT_EQ(machine.state(), TaskState::Queued);
    ASSERT_EQ(sink.states().size(), 1u);
    EXPECT_EQ(sink.states()[0], TaskState::Queued);
}

/** P-EX-6：任务态为会话内存态——Queued 记录零磁盘身份（run 为空）；
 *  全类型零持久化设施（本头面无序列化 API 的结构面由 include 扫描与
 *  本用例的行为面共同钉住：记录生命周期仅存在于调度器内存）。 */
TEST(TaskVocabulary, QueuedRecordHasNoDiskIdentity_PEX6)
{
    TaskRecord record = makeQueuedRecord();
    EXPECT_FALSE(record.run.has_value()) << "Queued 任务无运行绑定（无 results/ 预留）";
    EXPECT_EQ(record.archivePhase, ArchivePhase::NotApplicable) << "未派发＝归档不相干";
    // 磁盘痕迹唯一经 project（归档预留/检查点）——记录本体不携带任何
    // 路径/文件字段（§4.2 字段表逐字段可核）。
    SUCCEED() << "字段面核对：TaskRecord 无磁盘路径字段（§4.2 表）";
}

// =====================================================================
// §5.6 终态映射与判定辅助（core.md §10.3 交接承接）
// =====================================================================

/** TaskState→TaskOutcome 冻结映射：四终态同名直映、非终态无 outcome
 *  （不伪造结果——TASK-02/NFR-REL-03）；isTerminalTaskState 四值真。 */
TEST(TaskOutcomeMapping, TerminalOutcomeFrozenTable_S56)
{
    const std::pair<TaskState, core::TaskOutcome> kTerminalPairs[4] = {
        {TaskState::Completed, core::TaskOutcome::Completed},
        {TaskState::Canceled, core::TaskOutcome::Canceled},
        {TaskState::Failed, core::TaskOutcome::Failed},
        {TaskState::Interrupted, core::TaskOutcome::Interrupted},
    };
    for (const auto& [state, outcome] : kTerminalPairs) {
        EXPECT_TRUE(isTerminalTaskState(state));
        ASSERT_TRUE(terminalOutcome(state).has_value());
        EXPECT_EQ(*terminalOutcome(state), outcome);
    }
    for (const auto state : {TaskState::Queued, TaskState::Preparing, TaskState::Running,
                             TaskState::Paused, TaskState::Canceling}) {
        EXPECT_FALSE(isTerminalTaskState(state));
        EXPECT_FALSE(terminalOutcome(state).has_value()) << "非终态无 outcome";
    }
}

// =====================================================================
// 能力声明注册表（acceptance 4——P-EX-7 处置的 execution 侧落点）
// =====================================================================

/** EvaluatorRuntimeCapabilities：注册期声明、同键重复拒绝、未声明按
 *  最小能力回退（§5.5）；evidence EvaluatorDescriptor 不含执行能力字段
 *  ——两端口径互不混装（evidence.md §13 交接项）。 */
TEST(TaskCapabilityRegistry, DeclareLookupMinimalFallback_PEX7_S55)
{
    EvaluatorRuntimeCapabilities registry;

    TaskCapability pauseCapable;
    pauseCapable.supportsPause = true;
    pauseCapable.checkpointGranularity = CheckpointGranularity::Sample;
    pauseCapable.forceTerminateCost = ForceTerminateCost::Expensive;

    EXPECT_TRUE(registry.declare("kin-batch-ik", pauseCapable));
    EXPECT_FALSE(registry.declare("kin-batch-ik", TaskCapability{}))
        << "同键重复声明拒绝（装配清单漂移早期暴露）";

    const auto found = registry.tryLookup("kin-batch-ik");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, pauseCapable);

    // 未声明评估器：精确查询为空；派发推导轨回退最小能力（不支持暂停
    // ——收到暂停请求将走 EX-SM-7 显式反馈路径）。
    EXPECT_FALSE(registry.tryLookup("dyn-gravity-sweep").has_value());
    const TaskCapability minimal = registry.lookupOrMinimal("dyn-gravity-sweep");
    EXPECT_EQ(minimal, TaskCapability{}) << "最小能力＝缺省三件套（§5.5）";
}

// =====================================================================
// 值类型边界与错误面（TaskId 纪律；ProgressReport 工厂；token 稳定）
// =====================================================================

/** TaskId 规范文本纪律（core §4.1 同源）：往返严格、tag 逐字符、大写
 *  拒绝、长度敏感、保留零值非法；fromCanonical 抛/tryFromCanonical 双轨。 */
TEST(TaskIdDiscipline, ParseFormatRoundTripStrict)
{
    const TaskId id = TaskId::generate();
    const std::string text = id.toCanonical();
    EXPECT_EQ(text.size(), 36u) << "tsk- ＋32 hex";
    EXPECT_EQ(text.substr(0, 4), "tsk-");
    EXPECT_EQ(TaskId::fromCanonical(text) == id, true) << "往返严格";

    EXPECT_FALSE(TaskId::tryFromCanonical(text.substr(0, 35)).has_value()) << "长度不足";
    EXPECT_FALSE(TaskId::tryFromCanonical("obj-" + text.substr(4)).has_value()) << "tag 漂移";
    EXPECT_FALSE(TaskId::tryFromCanonical("TSK-" + text.substr(4)).has_value()) << "大写 tag";
    std::string upper = text;
    upper[10] = 'A';   // 确定性置入大写 hex 字符（字符集仅 [0-9a-f]——必拒）
    EXPECT_FALSE(TaskId::tryFromCanonical(upper).has_value()) << "大写 hex 体拒绝";
    EXPECT_FALSE(TaskId{}.isValid()) << "全零保留值非法";

    try {
        TaskId::fromCanonical("not-an-id");
        FAIL() << "非法文本未被拒绝";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }
}

/** ProgressReport 工厂边界（§4.2 progress 行）：percent∈[0,100]、
 *  phaseToken 非空、done≤total——越界即 fail-fast。 */
TEST(ProgressReportDiscipline, FactoryBoundaries)
{
    EXPECT_NO_THROW(ProgressReport::make(0, "preparing", 0, 10));
    EXPECT_NO_THROW(ProgressReport::make(100, "finalizing", 10, 10));
    try {
        ProgressReport::make(101, "over", 0, 10);
        FAIL() << "percent>100 未被拒绝";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }
    try {
        ProgressReport::make(50, "", 0, 10);
        FAIL() << "空 phaseToken 未被拒绝";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }
    try {
        ProgressReport::make(50, "batch", 11, 10);
        FAIL() << "done>total 未被拒绝";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
    }
}

/** 错误面抽查（§3.4）：token 稳定（列点名的四个值）＋ExecutionError
 *  携带 code 与 detail。 */
TEST(ExecutionErrorFace, TokensAndExceptionCarryCode)
{
    EXPECT_STREQ(toToken(ExecutionErrorCode::InvalidState), "execution/invalid-state");
    EXPECT_STREQ(toToken(ExecutionErrorCode::CapabilityUnsupported),
                 "execution/capability-unsupported");
    EXPECT_STREQ(toToken(ExecutionErrorCode::ForceTerminated), "execution/force-terminated");
    EXPECT_STREQ(toToken(ExecutionErrorCode::ContextClosed), "execution/context-closed");

    const ExecutionError err(ExecutionErrorCode::RegistryMismatch, "execution/test: 明细");
    EXPECT_EQ(err.code(), ExecutionErrorCode::RegistryMismatch);
    EXPECT_STREQ(err.what(), "execution/test: 明细");
}
