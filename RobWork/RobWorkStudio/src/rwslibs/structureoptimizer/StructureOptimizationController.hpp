#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTROLLER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTROLLER_HPP

#include "StructureOptimizationStrategy.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace rws {

class StructureOptimizationController : public QObject
{
    Q_OBJECT

public:
    using RunFunction = std::function<StructureOptimizationResult(
        const StructureOptimizationProblem&,
        const StructureOptimizationCallbacks&)>;

    explicit StructureOptimizationController(QObject* parent = nullptr);
    explicit StructureOptimizationController(RunFunction runFunction,
                                             QObject* parent = nullptr);
    ~StructureOptimizationController() override;

    bool start(const StructureOptimizationProblem& problem);
    void pause();
    void resume();
    void cancel();
    bool isRunning() const;
    bool isPaused() const;

Q_SIGNALS:
    void progressChanged(const rws::StructureProgress& progress);
    void completed(const rws::StructureOptimizationResult& result);
    void failed(const QString& message);
    void runningChanged(bool running);
    void pausedChanged(bool paused);

private:
    struct OptimizationControlState;

    static StructureOptimizationResult runDefaultOptimization(
        const StructureOptimizationProblem& problem,
        const StructureOptimizationCallbacks& callbacks);

    void finishCurrentRun();
    void setRunning(bool running);
    void setPaused(bool paused);

    QFutureWatcher<StructureOptimizationResult> _watcher;
    RunFunction _runFunction;
    std::shared_ptr<OptimizationControlState> _control;
    bool _running = false;
    bool _paused = false;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTROLLER_HPP
