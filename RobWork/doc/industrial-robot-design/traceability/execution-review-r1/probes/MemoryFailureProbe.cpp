/** @file MemoryFailureProbe.cpp
 * @brief EXR-10 隔离探针：仅替换实际 Job 查询失败点，执行真实 Supervisor/采样器。
 * 宿主与 worker 均来自本 C: 树；沿既有真进程夹具持有/终结本轮 Job。
 */
#include "../../RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/win32/MemProbe.hpp"
#include <limits>

namespace memory_probe {
enum class Mode { Real, Zero, Pressure, FailOne, FailAll, Overflow };
Mode mode = Mode::Real;
HANDLE failingJob = nullptr;
}
namespace sdurws::ird::execution::win32 {
/** @brief 保留真实 OS 查询；按用例仅在 Job 探针边界注入失败/溢出。 */
bool auditedJobMemory(HANDLE job, std::uint64_t& out) noexcept
{
    using namespace memory_probe;
    if (mode == Mode::FailAll || (mode == Mode::FailOne && job == failingJob)) return false;
    if (mode == Mode::FailOne && failingJob == nullptr) {
        failingJob = job;
        return false;
    }
    if (mode == Mode::Zero) { out = 0; return true; }
    if (mode == Mode::Pressure) {
        SystemMemoryInfo info;
        if (!querySystemMemory(info)) return false;
        out = info.totalPhysicalBytes / 2;
        return true;
    }
    if (mode == Mode::Overflow) { out = std::numeric_limits<std::uint64_t>::max(); return true; }
    return queryJobPeakCommittedBytes(job, out);
}
}
#define queryJobPeakCommittedBytes auditedJobMemory
#ifdef EXR_BASELINE
#include "baseline/WorkerSupervisor.cpp"
#else
#include "../../RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/WorkerSupervisor.cpp"
#endif
#undef queryJobPeakCommittedBytes
#include <sdurws/ird/execution/Scheduler.hpp>
#include "../../RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/contract_test/WorkerProcessContractTest.cpp"

namespace {
/** @brief 空池、两真实 Job、失采、恢复及退出后零，均检查实际生产采样链。 */
TEST_F(WorkerProcessContractTest, MemoryFailureDoesNotBecomeZero_EXR10Probe)
{
    using namespace memory_probe;
    auto supervisor = makeSupervisor();
    ASSERT_TRUE(supervisor->aggregateJobMemoryBytes().has_value());
    EXPECT_EQ(*supervisor->aggregateJobMemoryBytes(), 0u);
    const auto first = supervisor->launch(makeAssignment(registerRun(), "stage-a-script:v1\nhang\n"));
    const auto second = supervisor->launch(makeAssignment(registerRun(), "stage-a-script:v1\nhang\n"));
    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    std::cout << "owned worker pids=" << supervisor->status(first.worker).pid << ','
              << supervisor->status(second.worker).pid << '\n';
    ASSERT_TRUE(pumpUntil(*supervisor, [&] {
        return m_events.countOf(WorkerEvent::Kind::DispatchAccepted) == 2;
    }, 10000));
    SupervisorMemorySampler sampler(supervisor.get());
    const auto actual = supervisor->aggregateJobMemoryBytes();
    ASSERT_TRUE(actual.has_value());
    EXPECT_GT(*actual, 0u);
    EXPECT_TRUE(sampler.sample().has_value());
    mode = Mode::Zero;
    ASSERT_TRUE(supervisor->aggregateJobMemoryBytes().has_value());
    EXPECT_EQ(*supervisor->aggregateJobMemoryBytes(), 0u);
    ResourceController resource(sampler, ResourceController::Config{std::chrono::milliseconds{0}});
    mode = Mode::Pressure;
    EXPECT_EQ(resource.evaluate().level, ResourceController::Level::Exhausted);
    mode = Mode::FailOne;
    EXPECT_FALSE(supervisor->aggregateJobMemoryBytes().has_value());
    EXPECT_FALSE(sampler.sample().has_value());
    EXPECT_EQ(resource.evaluate().level, ResourceController::Level::Exhausted);
    mode = Mode::FailAll;
    EXPECT_FALSE(supervisor->aggregateJobMemoryBytes().has_value());
    EXPECT_FALSE(sampler.sample().has_value());
    mode = Mode::Overflow;
    EXPECT_FALSE(supervisor->aggregateJobMemoryBytes().has_value());
    EXPECT_FALSE(sampler.sample().has_value());
    mode = Mode::Real;
    EXPECT_TRUE(sampler.sample().has_value());
    EXPECT_EQ(resource.evaluate().level, ResourceController::Level::Normal);
    supervisor->terminateForce(first.worker, TerminationCause::ForceTerminated);
    supervisor->terminateForce(second.worker, TerminationCause::ForceTerminated);
    ASSERT_TRUE(pumpUntil(*supervisor, [&] {
        return m_events.countOf(WorkerEvent::Kind::WorkerExited) == 2;
    }, 10000));
    mode = Mode::FailAll; // 已退出 Job 不得再查询，不应受故障钩子影响。
    const auto afterExit = supervisor->aggregateJobMemoryBytes();
    ASSERT_TRUE(afterExit.has_value());
    EXPECT_EQ(*afterExit, 0u);
    mode = Mode::Real;
}
}
