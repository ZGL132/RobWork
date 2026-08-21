#include "OptimizationRunStateMachine.hpp"

namespace rws {
namespace {

OptimizationRunTransition transition(OptimizationRunState& current,
                                      OptimizationRunState next,
                                      const char* error)
{
    OptimizationRunTransition result;
    result.state = current;
    if (error != nullptr) {
        result.diagnostic = error;
        return result;
    }
    current = next;
    result.ok = true;
    result.state = current;
    return result;
}

} // namespace

OptimizationRunTransition OptimizationRunStateMachine::start()
{
    return transition(_state, OptimizationRunState::Running,
                      _state == OptimizationRunState::Idle ? nullptr
                                                            : "RUN_START_NOT_ALLOWED");
}

OptimizationRunTransition OptimizationRunStateMachine::pause()
{
    return transition(_state, OptimizationRunState::Paused,
                      _state == OptimizationRunState::Running ? nullptr
                                                               : "RUN_PAUSE_NOT_ALLOWED");
}

OptimizationRunTransition OptimizationRunStateMachine::resume()
{
    return transition(_state, OptimizationRunState::Running,
                      _state == OptimizationRunState::Paused ? nullptr
                                                               : "RUN_RESUME_NOT_ALLOWED");
}

OptimizationRunTransition OptimizationRunStateMachine::requestCancel()
{
    if (_state == OptimizationRunState::CancelRequested)
        return {true, _state, {}}; // 取消幂等：重复点击不改变状态，也不产生错误。
    return transition(_state, OptimizationRunState::CancelRequested,
                      (_state == OptimizationRunState::Running ||
                       _state == OptimizationRunState::Paused)
                          ? nullptr
                          : "RUN_CANCEL_NOT_ALLOWED");
}

OptimizationRunTransition OptimizationRunStateMachine::complete()
{
    return transition(_state, OptimizationRunState::Completed,
                      (_state == OptimizationRunState::Running ||
                       _state == OptimizationRunState::CancelRequested)
                          ? nullptr
                          : "RUN_COMPLETE_NOT_ALLOWED");
}

OptimizationRunTransition OptimizationRunStateMachine::fail()
{
    return transition(_state, OptimizationRunState::Failed,
                      (_state == OptimizationRunState::Running ||
                       _state == OptimizationRunState::Paused ||
                       _state == OptimizationRunState::CancelRequested)
                          ? nullptr
                          : "RUN_FAIL_NOT_ALLOWED");
}

void OptimizationRunStateMachine::reset()
{
    _state = OptimizationRunState::Idle;
}

const char* toString(OptimizationRunState state)
{
    switch (state) {
    case OptimizationRunState::Idle: return "Idle";
    case OptimizationRunState::Running: return "Running";
    case OptimizationRunState::Paused: return "Paused";
    case OptimizationRunState::CancelRequested: return "CancelRequested";
    case OptimizationRunState::Completed: return "Completed";
    case OptimizationRunState::Failed: return "Failed";
    }
    return "Idle";
}

} // namespace rws
