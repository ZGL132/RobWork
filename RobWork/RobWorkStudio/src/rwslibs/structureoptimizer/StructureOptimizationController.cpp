#include "StructureOptimizationController.hpp"

#include "EngineeringEvaluatorPipeline.hpp"
#include "KinematicEngineeringEvaluator.hpp"
#include "SystemEngineeringOptimizer.hpp"
#include "CanonicalBaselineEvaluationBridge.hpp"
#include "CandidateModelFactory.hpp"
#include "CanonicalModelShadowService.hpp"
#include "KinematicModelImporter.hpp"
#include "StructureOptimizationUiLogic.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>

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
      _runFunction(std::move(runFunction)),
      _baselineRunFunction(&StructureOptimizationController::runDefaultBaselineEvaluation)
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
    // C1.1/D1: 绕过 Widget 的直接调用同样不得运行 stale 冻结契约——
    // 判定完全基于 problem 本身（持久化参考指纹 vs 当前表格指纹）。
    if (StructureOptimizationUiLogic::frozenContractStale(problem))
        return false;
    // 结束态必须先复位为 Idle 才能开始下一轮，防止旧会话的取消状态泄漏。
    if (_runStateMachine.state() != OptimizationRunState::Idle)
        _runStateMachine.reset();
    if (!_runStateMachine.start().ok)
        return false;

    _control.reset(new OptimizationControlState());
    setPaused(false);
    setRunning(true);

    const StructureOptimizationProblem snapshot = problem;
    const std::shared_ptr<OptimizationControlState> control = _control;
    StructureOptimizationController* receiver = this;
    RunFunction runFunction = _runFunction;
    const std::uint64_t runId = ++_runId;
    const std::uint64_t projectEpoch = _projectEpoch;
    _activeRunProjectEpoch = projectEpoch;
    _activeRunId = runId;

    QFuture<StructureOptimizationResult> future = QtConcurrent::run(
        [snapshot, control, receiver, runFunction, runId, projectEpoch]() {
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
            callbacks.onProgress = [receiver, runId, projectEpoch](const StructureProgress& progress) {
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, progress, runId, projectEpoch]() {
                        if (receiver->_runId == runId &&
                            receiver->_projectEpoch == projectEpoch)
                            Q_EMIT receiver->progressChanged(progress);
                    },
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
    // C1.1/D1: stale 冻结契约禁止基线评估（Verified 语义）。
    if (StructureOptimizationUiLogic::frozenContractStale(problem))
        return false;

    _baselineControl.reset(new OptimizationControlState());
    setBaselineRunning(true);

    const StructureOptimizationProblem snapshot = problem;
    const std::shared_ptr<OptimizationControlState> control = _baselineControl;
    StructureOptimizationController* receiver = this;
    const std::uint64_t runId = ++_baselineRunId;
    const std::uint64_t projectEpoch = _projectEpoch;
    _activeBaselineProjectEpoch = projectEpoch;
    _activeBaselineRunId = runId;
    RunFunction baselineRunFunction = _baselineRunFunction;
    QFuture<StructureOptimizationResult> future = QtConcurrent::run(
        [snapshot, control, receiver, runId, projectEpoch, baselineRunFunction]() {
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
            callbacks.onProgress = [receiver, runId, projectEpoch](const StructureProgress& progress) {
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, progress, runId, projectEpoch]() {
                        if (receiver->_baselineRunId == runId &&
                            receiver->_projectEpoch == projectEpoch)
                            Q_EMIT receiver->progressChanged(progress);
                    },
                    Qt::QueuedConnection);
            };
            try {
                return baselineRunFunction(snapshot, callbacks);
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
    if (!_runStateMachine.pause().ok)
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
    if (!_runStateMachine.resume().ok)
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
    // 对主会话使用幂等状态机；析构、项目关闭和用户按钮可安全重复调用 cancel。
    const OptimizationRunState state = _runStateMachine.state();
    if (state == OptimizationRunState::Running || state == OptimizationRunState::Paused ||
        state == OptimizationRunState::CancelRequested)
        _runStateMachine.requestCancel();
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
    // 项目会话已切换：旧项目的基线完成事件必须整体丢弃，不得写入新会话。
    // epoch + runId 双重校验（当前 watcher 单活不变量下二者等价，这里显式化）。
    if (_projectEpoch != _activeBaselineProjectEpoch ||
        _baselineRunId != _activeBaselineRunId)
        return;
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

void StructureOptimizationController::notifyProjectSessionChanged()
{
    ++_projectEpoch;
}

void StructureOptimizationController::setBaselineRunFunctionForTesting(
    RunFunction function)
{
    if (function)
        _baselineRunFunction = std::move(function);
}

bool StructureOptimizationController::isPaused() const
{
    return _paused;
}

bool StructureOptimizationController::isBaselineRunning() const
{
    return _baselineRunning;
}

OptimizationRunState StructureOptimizationController::runState() const
{
    return _runStateMachine.state();
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
    struct CancellationBridge {
        const std::function<bool()>* callback = nullptr;
    } cancellationBridge;
    cancellationBridge.callback = &callbacks.isCancellationRequested;
    const auto cancellationRequested = [](void* userData) {
        const CancellationBridge* bridge = static_cast<const CancellationBridge*>(userData);
        return bridge != nullptr && bridge->callback != nullptr &&
               *bridge->callback && (*bridge->callback)();
    };

    // 存量项目保存时还没有 canonicalModelShadow,模型文件在保存后变更也会让影子
    // 过期。影子完全由项目自身的 modelSpec 决定,这里在评估入口用与候选评估相同
    // 的模型构建路径(CandidateModelFactory)现场重建影子,避免旧项目被 S52 永久
    // 卡死;重建失败才原样上报。
    StructureOptimizationProblem shadowedProblem;
    const StructureOptimizationProblem* effectiveProblem = &problem;
    if (problem.canonicalModelShadow.status != CanonicalModelShadowStatus::Current ||
        !problem.canonicalModelShadow.hasSnapshot()) {
        shadowedProblem = problem;
        CandidateModelBuildRequest buildRequest;
        buildRequest.spec = shadowedProblem.context.modelSpec;
        buildRequest.deviceName = shadowedProblem.context.deviceName;
        buildRequest.tcpFrame = shadowedProblem.context.tcpFrame;
        buildRequest.checkCollision = false;
        buildRequest.scenarioSnapshot = &shadowedProblem.scenarioSnapshot;
        buildRequest.scenarioBaseDirectory = shadowedProblem.scenarioSnapshot.baseDirectory;
        const CandidateModelBuildResult built =
            CandidateModelFactory().build(buildRequest);
        std::string shadowError;
        if (!built.ok) {
            shadowError = "the project model could not be built for canonical import.";
        }
        else {
            KinematicImportRequest importRequest;
            importRequest.workcell = built.artifact.workcell.get();
            importRequest.device = built.artifact.device.get();
            importRequest.tcpFrame = built.artifact.tcpFrame.get();
            importRequest.sourceSnapshot = &shadowedProblem.context.modelSpec;
            importRequest.sourceFingerprint =
                RobotModelFingerprint::canonicalSha256(shadowedProblem.context.modelSpec);
            if (!CanonicalModelShadowService::attach(importRequest, shadowedProblem,
                                                     &shadowError))
                shadowError = "canonical import failed: " + shadowError;
        }
        if (!shadowError.empty()) {
            AnalysisWarning warning;
            warning.code     = "S52_CANONICAL_BASELINE_UNAVAILABLE";
            warning.message  = "Canonical model shadow rebuild failed: " + shadowError;
            warning.severity = AnalysisStatus::Fail;
            result.warnings.push_back(std::move(warning));
            return result;
        }
        effectiveProblem = &shadowedProblem;
    }

    CanonicalBaselineEvaluationRequest request;
    request.problem = effectiveProblem;
    request.deviceName = effectiveProblem->context.deviceName;
    request.tcpFrame = effectiveProblem->context.tcpFrame;
    request.checkCollision = true;
    request.cancellation = {cancellationRequested, &cancellationBridge};
    request.planOptions.capabilities.insert("target");
    const BaselineEvaluationResult baseline =
        CanonicalBaselineEvaluationBridge::evaluate(request);
    result.baselineCandidateIndex = baseline.baselineIndex;
    result.baselineAudit.index = baseline.baselineIndex;
    result.baselineAudit.candidateFingerprint = baseline.candidateFingerprint;
    result.baselineAudit.modelFingerprint = baseline.modelFingerprint;
    result.baselineAudit.environmentFingerprint = baseline.environmentFingerprint;
    result.baselineAudit.toolFingerprint = baseline.toolFingerprint;
    result.baselineAudit.planFingerprint = baseline.planFingerprint;
    if (!baseline.ok) {
        for (const StructureOptimizationDiagnostic& diagnostic : baseline.diagnostics) {
            AnalysisWarning warning;
            warning.code = diagnostic.code;
            warning.message = diagnostic.message;
            warning.severity = AnalysisStatus::Fail;
            result.warnings.push_back(std::move(warning));
        }
        if (baseline.candidateResult.lifecycle == CandidateLifecycle::Canceled)
            result.canceled = true;
    }
    // Hard bridge failures are reported through baselineFailed; do not add a
    // failed projection to candidates, otherwise finishBaselineRun would emit
    // baselineCompleted and the UI would present an invalid baseline as valid.
    if (baseline.ok) {
        StructureCandidateResult legacy =
            CandidateResultAssembler::toLegacy(baseline.candidateResult,
                                               baseline.baselineIndex);
        legacy.stage = StructureEvaluationStage::Verified;
        legacy.feasible = baseline.candidateResult.feasibility == Feasibility::Feasible;
        // The canonical compiler consumes only the new parameter-binding
        // design space, so its vector can legitimately be empty for a saved
        // project that still owns legacy StructureDesignVariables.  The
        // legacy metric evaluator instead requires one value per such
        // variable.  A "current model" baseline must therefore use the
        // project's current values, in the legacy evaluator's declared order.
        legacy.values.reserve(effectiveProblem->variables.size());
        for (const StructureDesignVariable& variable : effectiveProblem->variables)
            legacy.values.push_back(variable.currentValue);

        // CandidateResultAssembler intentionally projects lifecycle and
        // feasibility evidence only.  The existing candidate table, however,
        // reads its score/reachability/length columns from the legacy raw
        // metrics.  Evaluate the nominal vector through the same metric
        // evaluator used for optimizer candidates, then copy *only* display
        // metrics: the canonical bridge above remains authoritative for the
        // baseline feasibility/status that it verified.
        StructureCandidateResult metricProjection = legacy;
        KinematicEngineeringEvaluator metricEvaluator(*effectiveProblem);
        metricEvaluator.evaluateCandidate(metricProjection,
                                          StructureEvaluationStage::Verified,
                                          callbacks, nullptr);
        if (metricProjection.status == StructureCandidateStatus::Canceled) {
            result.canceled = true;
            return result;
        }
        if (metricProjection.raw.modelValid) {
            legacy.raw = std::move(metricProjection.raw);
            legacy.scores = metricProjection.scores;
            legacy.totalScore = metricProjection.totalScore;
        }
        else {
            legacy.warnings.push_back(
                "Baseline metrics unavailable: nominal metric evaluation did not produce a valid model.");
            for (const std::string& warning : metricProjection.warnings)
                legacy.warnings.push_back(warning);
        }
        result.candidates.push_back(std::move(legacy));
    }
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

    // Future 已收束后才允许离开运行态。受保护性异常会生成“无候选且有警告”的
    // 硬失败；它必须进入 Failed。正常完成和协作取消则都进入 Completed，取消本身
    // 是完整的调度终态，不应被误投影为工程计算失败。
    if (hasFailure)
        _runStateMachine.fail();
    else if (_runStateMachine.state() == OptimizationRunState::Running ||
             _runStateMachine.state() == OptimizationRunState::CancelRequested)
        _runStateMachine.complete();

    // 项目会话已切换：旧项目的主运行完成事件必须整体丢弃，不得写入新会话
    // （调度状态已在上面的状态机中收束，isRunning 亦已复位，只是不再对外广播）。
    // epoch + runId 双重校验（当前 watcher 单活不变量下二者等价，这里显式化）。
    if (_projectEpoch != _activeRunProjectEpoch || _runId != _activeRunId)
        return;

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
