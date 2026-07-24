#include "StructureOptimizationController.hpp"

#include "HybridStructureOptimizer.hpp"
#include "StructureCandidateEvaluator.hpp"

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
}

StructureOptimizationController::~StructureOptimizationController()
{
    cancel();
    if (_watcher.isRunning())
        _watcher.waitForFinished();
}

bool StructureOptimizationController::start(
    const StructureOptimizationProblem& problem)
{
    if (_running || _watcher.isRunning())
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
    if (!_control)
        return;
    {
        std::lock_guard<std::mutex> lock(_control->mutex);
        _control->canceled.store(true);
        _control->paused.store(false);
    }
    _control->condition.notify_all();
    setPaused(false);
}

bool StructureOptimizationController::isRunning() const
{
    return _running;
}

bool StructureOptimizationController::isPaused() const
{
    return _paused;
}

StructureOptimizationResult
StructureOptimizationController::runDefaultOptimization(
    const StructureOptimizationProblem& problem,
    const StructureOptimizationCallbacks& callbacks)
{
    StructureCandidateEvaluator evaluator;
    HybridStructureOptimizer optimizer;
    return optimizer.optimize(problem, evaluator, callbacks);
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
