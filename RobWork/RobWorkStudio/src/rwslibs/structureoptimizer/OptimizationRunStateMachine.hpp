#ifndef RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONRUNSTATEMACHINE_HPP
#define RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONRUNSTATEMACHINE_HPP

#include <string>

namespace rws {

/**
 * @brief 优化运行的生命周期状态。
 *
 * 状态机只描述调度生命周期，不把候选工程可行性混入其中。候选结果仍通过
 * CandidateLifecycle/Feasibility 表达；这样暂停、恢复和取消不会把“尚未完成”
 * 错误解释为“Infeasible”。
 */
enum class OptimizationRunState
{
    Idle,
    Running,
    Paused,
    CancelRequested,
    Completed,
    Failed
};

/** 纯状态转移结果，失败时保留稳定诊断码。 */
struct OptimizationRunTransition
{
    bool ok = false;
    OptimizationRunState state = OptimizationRunState::Idle;
    std::string diagnostic;
};

/**
 * @brief 可测试的优化运行状态机。
 *
 * 所有方法均为同步、无 Qt 依赖的 POD 风格接口；Controller 负责把按钮、Future
 * 和信号映射到这些转移，避免 UI 层自行复制一套不一致的状态规则。
 */
class OptimizationRunStateMachine
{
  public:
    OptimizationRunState state() const { return _state; }

    OptimizationRunTransition start();
    OptimizationRunTransition pause();
    OptimizationRunTransition resume();
    OptimizationRunTransition requestCancel();
    OptimizationRunTransition complete();
    OptimizationRunTransition fail();
    void reset();

  private:
    OptimizationRunState _state = OptimizationRunState::Idle;
};

const char* toString(OptimizationRunState state);

} // namespace rws

#endif
