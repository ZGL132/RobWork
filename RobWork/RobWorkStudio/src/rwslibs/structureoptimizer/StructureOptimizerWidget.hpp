#ifndef RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
#define RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP

#include "StructureOptimizationTypes.hpp"
#include "CandidatePreviewController.hpp"

#include <QWidget>

#include <array>

class QLabel;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTabWidget;
class QTableView;

namespace rws {

class OptimizationTaskTableModel;
class StructureCandidateTableModel;
class StructureConstraintTableModel;
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
    void setPreviewHost(IWorkCellPreviewHost* host);

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
    void previewSelectedCandidate();
    void clearCandidatePreview();
    void newProjectFromModelSpec();
    void openProject();
    void saveProject();
    void exportResult();

    StructureOptimizationProblem _loadedProblem;
    StructureVariableTableModel* _variableModel = nullptr;
    OptimizationTaskTableModel* _taskModel = nullptr;
    StructureConstraintTableModel* _constraintModel = nullptr;
    StructureCandidateTableModel* _candidateModel = nullptr;
    StructureOptimizationController* _controller = nullptr;
    std::unique_ptr<CandidatePreviewController> _previewController;
    StructureOptimizationResult _lastResult;
    QString _projectPath;

    QTabWidget* _tabs = nullptr;
    QTableView* _candidateView = nullptr;
    QPushButton* _startButton = nullptr;
    QPushButton* _pauseButton = nullptr;
    QPushButton* _cancelButton = nullptr;
    QLabel* _statusLabel = nullptr;
    QLabel* _progressLabel = nullptr;
    QSpinBox* _candidateCountSpin = nullptr;
    QSpinBox* _eliteCountSpin = nullptr;
    QSpinBox* _localEliteCountSpin = nullptr;
    QSpinBox* _finalVerificationCountSpin = nullptr;
    QSpinBox* _maxLocalSweepsSpin = nullptr;
    QSpinBox* _gridStepsSpin = nullptr;
    QSpinBox* _seedSpin = nullptr;
    QComboBox* _strategyCombo = nullptr;
    std::array<QDoubleSpinBox*, 6> _weightSpins = {{nullptr, nullptr, nullptr,
                                                      nullptr, nullptr, nullptr}};
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
