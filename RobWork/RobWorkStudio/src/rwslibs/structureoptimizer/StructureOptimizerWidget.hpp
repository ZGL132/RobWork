#ifndef RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
#define RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP

#include "StructureOptimizationTypes.hpp"

#include <QWidget>

class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace rws {

class OptimizationTaskTableModel;
class StructureCandidateTableModel;
class StructureOptimizationController;
class StructureVariableTableModel;

class StructureOptimizerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StructureOptimizerWidget(QWidget* parent = nullptr);
    ~StructureOptimizerWidget() override;

    void setProblem(const StructureOptimizationProblem& problem);
    StructureOptimizationProblem collectProblem() const;
    QString statusText() const;

private:
    QWidget* createVariablePage();
    QWidget* createTaskPage();
    QWidget* createSettingsPage();
    QWidget* createCandidatePage();
    QWidget* createReportPage();

    void updateRunState();
    void setEditingEnabled(bool enabled);
    void startOptimization();
    void togglePause();
    void cancelOptimization();
    void handleRunningChanged(bool running);
    void handlePausedChanged(bool paused);
    void handleProgress(const StructureProgress& progress);
    void handleCompleted(const StructureOptimizationResult& result);
    void handleFailed(const QString& message);

    StructureOptimizationProblem _loadedProblem;
    StructureVariableTableModel* _variableModel = nullptr;
    OptimizationTaskTableModel* _taskModel = nullptr;
    StructureCandidateTableModel* _candidateModel = nullptr;
    StructureOptimizationController* _controller = nullptr;

    QTabWidget* _tabs = nullptr;
    QPushButton* _startButton = nullptr;
    QPushButton* _pauseButton = nullptr;
    QPushButton* _cancelButton = nullptr;
    QLabel* _statusLabel = nullptr;
    QLabel* _progressLabel = nullptr;
    QSpinBox* _candidateCountSpin = nullptr;
    QSpinBox* _eliteCountSpin = nullptr;
    QSpinBox* _seedSpin = nullptr;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
