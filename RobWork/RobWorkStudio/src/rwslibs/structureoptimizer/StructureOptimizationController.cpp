#include "StructureOptimizationController.hpp"

#include "EngineeringEvaluatorPipeline.hpp"
#include "KinematicEngineeringEvaluator.hpp"
#include "SystemEngineeringOptimizer.hpp"

#include <QMetaObject>
#include <QtConcurrent>

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>

using namespace rws;

struct StructureOptimizationController::OptimizationControlState
{
    std::atomic_bool canceled{false};
    std::atomic_bool paused{false};
    std::mutex mutex;
    std::condition_variable condition;
};

StructureOptimizationController::StructureOptimizationController(QObject* parent)
    : StructureOptimizationController(&StructureOptimizationController::runDefaultOptimization,
                                      parent)
{
}

StructureOptimizationController::StructureOptimizationController(
    RunFunction runFunction, QObject* parent)
    : QObject(parent),
      _runFunction(std::move(runFunction))
{
    connect(&_watcher, &QFutureWatcher<StructureOptimizationResult>::finished,
            this, &StructureOptimizationController::finishCurrentRun);
    connect(&_baselineWatcher, &QFutureWatcher<StructureOptimizationResult>::finished,
            this, &StructureOptimizationController::finishBaselineRun);
}

StructureOptimizationController::~StructureOptimizationController()
{
    cancel();
    if (_watcher.isRunning())
        _watcher.waitForFinished();
    if (_baselineWatcher.isRunning())
        _baselineWatcher.waitForFinished();
}

bool StructureOptimizationController::start(
    const StructureOptimizationProblem& problem)
{
    if (_running || _watcher.isRunning() || _baselineRunning || _baselineWatcher.isRunning())
        return false;

    _control.reset(new OptimizationControlState());
    setPaused(false);
    setRunning(true);

    const StructureOptimizationProblem snapshot = problem;
    const std::shared_ptr<OptimizationControlState> control = _control;
    StructureOptimizationController* receiver = this;
    RunFunction runFunction = _runFunction;

    QFuture<StructureOptimizationResult> future = QtConcurrent::run(
        [snapshot, control, receiver, runFunction]() {
            StructureOptimizationCallbacks callbacks;
            callbacks.isCancellationRequested = [control]() {
                return control->canceled.load();
            };
            callbacks.waitIfPaused = [control]() {
                std::unique_lock<std::mutex> lock(control->mutex);
                control->condition.wait(lock, [control]() {
                    return !control->paused.load() || control->canceled.load();
                });
            };
            callbacks.onProgress = [receiver](const StructureProgress& progress) {
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, progress]() { Q_EMIT receiver->progressChanged(progress); },
                    Qt::QueuedConnection);
            };

            try {
                return runFunction(snapshot, callbacks);
            } catch (const std::exception& error) {
                StructureOptimizationResult result;
                AnalysisWarning warning;
                warning.code = "StructureOptimization.Controller.Exception";
                warning.message = error.what();
                warning.severity = AnalysisStatus::Fail;
                result.warnings.push_back(warning);
                return result;
            } catch (...) {
                StructureOptimizationResult result;
                AnalysisWarning warning;
                warning.code = "StructureOptimization.Controller.UnknownException";
                warning.message = "Unknown structure optimization failure.";
                warning.severity = AnalysisStatus::Fail;
                result.warnings.push_back(warning);
                return result;
            }
        });

    _watcher.setFuture(future);
    return true;
}

bool StructureOptimizationController::startBaselineEvaluation(
    const StructureOptimizationProblem& problem)
{
    if (_running || _watcher.isRunning() || _baselineRunning || _baselineWatcher.isRunning())
        return false;

    _baselineControl.reset(new OptimizationControlState());
    setBaselineRunning(true);

    const StructureOptimizationProblem snapshot = problem;
    const std::shared_ptr<OptimizationControlState> control = _baselineControl;
    StructureOptimizationController* receiver = this;
    QFuture<StructureOptimizationResult> future = QtConcurrent::run(
        [snapshot, control, receiver]() {
            StructureOptimizationCallbacks callbacks;
            callbacks.isCancellationRequested = [control]() {
                return control->canceled.load();
            };
            callbacks.waitIfPaused = [control]() {
                std::unique_lock<std::mutex> lock(control->mutex);
                control->condition.wait(lock, [control]() {
                    return !control->paused.load() || control->canceled.load();
                });
            };
            callbacks.onProgress = [receiver](const StructureProgress& progress) {
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, progress]() { Q_EMIT receiver->progressChanged(progress); },
                    Qt::QueuedConnection);
            };
            try {
                return runDefaultBaselineEvaluation(snapshot, callbacks);
            } catch (const std::exception& error) {
                StructureOptimizationResult result;
                AnalysisWarning warning;
                warning.code = "StructureOptimization.Controller.BaselineException";
                warning.message = error.what();
                warning.severity = AnalysisStatus::Fail;
                result.warnings.push_back(warning);
                return result;
            } catch (...) {
                StructureOptimizationResult result;
                AnalysisWarning warning;
                warning.code = "StructureOptimization.Controller.BaselineUnknownException";
                warning.message = "Unknown structure baseline evaluation failure.";
                warning.severity = AnalysisStatus::Fail;
                result.warnings.push_back(warning);
                return result;
            }
        });
    _baselineWatcher.setFuture(future);
    return true;
}

void StructureOptimizationController::pause()
{
    if (!_running || !_control)
        return;
    {
        std::lock_guard<std::mutex> lock(_control->mutex);
        _control->paused.store(true);
    }
    setPaused(true);
}

void StructureOptimizationController::resume()
{
    if (!_control)
        return;
    {
        std::lock_guard<std::mutex> lock(_control->mutex);
        _control->paused.store(false);
    }
    _control->condition.notify_all();
    setPaused(false);
}

void StructureOptimizationController::cancel()
{
    const auto cancelControl = [](const std::shared_ptr<OptimizationControlState>& control) {
        if (!control)
            return;
        {
            std::lock_guard<std::mutex> lock(control->mutex);
            control->canceled.store(true);
            control->paused.store(false);
        }
        control->condition.notify_all();
    };
    cancelControl(_control);
    cancelControl(_baselineControl);
    setPaused(false);
}

void StructureOptimizationController::setBaselineRunning(bool running)
{
    if (_baselineRunning == running)
        return;
    _baselineRunning = running;
    Q_EMIT baselineRunningChanged(_baselineRunning);
}

void StructureOptimizationController::finishBaselineRun()
{
    StructureOptimizationResult result = _baselineWatcher.result();
    const bool hasFailure =
        !result.canceled && !result.warnings.empty() && result.candidates.empty();
    setBaselineRunning(false);
    if (hasFailure) {
        QString message = QString::fromStdString(result.warnings.front().code);
        if (!result.warnings.front().message.empty())
            message += ": " + QString::fromStdString(result.warnings.front().message);
        Q_EMIT baselineFailed(message);
    } else {
        Q_EMIT baselineCompleted(result);
    }
}

bool StructureOptimizationController::isRunning() const
{
    return _running;
}

bool StructureOptimizationController::isPaused() const
{
    return _paused;
}

bool StructureOptimizationController::isBaselineRunning() const
{
    return _baselineRunning;
}

StructureOptimizationResult
StructureOptimizationController::runDefaultOptimization(
    const StructureOptimizationProblem& problem,
    const StructureOptimizationCallbacks& callbacks)
{
    KinematicEngineeringEvaluator evaluator(problem);
    EngineeringEvaluatorPipeline pipeline;
    pipeline.addEvaluator(evaluator);
    SystemEngineeringOptimizer optimizer;
    return optimizer.optimize(problem, pipeline, callbacks);
}

StructureOptimizationResult
StructureOptimizationController::runDefaultBaselineEvaluation(
    const StructureOptimizationProblem& problem,
    const StructureOptimizationCallbacks& callbacks)
{
    StructureOptimizationResult result;
    StructureCandidateResult baseline;
    baseline.index = 0;
    baseline.values.reserve(problem.variables.size());
    for (const StructureDesignVariable& variable : problem.variables)
        baseline.values.push_back(variable.currentValue);
    KinematicEngineeringEvaluator evaluator(problem);
    evaluator.evaluateLegacy(baseline, StructureEvaluationStage::Verified, callbacks, nullptr);
    result.baselineCandidateIndex = 0;
    result.candidates.push_back(std::move(baseline));
    return result;
}

void StructureOptimizationController::finishCurrentRun()
{
    StructureOptimizationResult result = _watcher.result();
    const bool hasFailure =
        !result.canceled && !result.warnings.empty() &&
        result.candidates.empty();

    setRunning(false);
    setPaused(false);

    if (hasFailure) {
        QString message = QString::fromStdString(result.warnings.front().code);
        if (!result.warnings.front().message.empty())
            message += ": " + QString::fromStdString(result.warnings.front().message);
        Q_EMIT failed(message);
    } else {
        Q_EMIT completed(result);
    }
}

void StructureOptimizationController::setRunning(bool running)
{
    if (_running == running)
        return;
    _running = running;
    Q_EMIT runningChanged(_running);
}

void StructureOptimizationController::setPaused(bool paused)
{
    if (_paused == paused)
        return;
    _paused = paused;
    Q_EMIT pausedChanged(_paused);
}
