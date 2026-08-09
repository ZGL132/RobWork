#ifndef RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
#define RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP

#include "StructureOptimizationTypes.hpp"
#include "CandidatePreviewController.hpp"
#include "RobotModelStalenessChecker.hpp"

#include <rw/kinematics/State.hpp>

#include <QByteArray>
#include <QWidget>

#include <array>

class QLabel;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTabWidget;
class QTableView;

namespace rw { namespace models { class WorkCell; } }

namespace rws {

class OptimizationTaskTableModel;
class StructureCandidateTableModel;
class StructureConstraintTableModel;
class StructureOptimizationController;
class StructureVariableTableModel;
class RobWorkStudio;

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
    void setRobWorkStudio(RobWorkStudio* studio);
    void setScenarioContext(rw::models::WorkCell* workcell,
                            const rw::kinematics::State& state);
    void clearScenarioContext();

    /**
     * @brief 项目 Provider 使用的无对话框资源读写接口。
     *
     * Registry 负责验证 rwproj 资源路径并提交多文件事务，Widget 只处理结构优化领域
     * JSON 与规范快照，避免原有文件对话框绕过项目统一保存流程。
     */
    bool loadProjectDocument(const QString& path, QString* error = nullptr,
                             const QString& projectRoot = QString());
    bool saveProjectDocument(const QString& targetPath, QString* error = nullptr) const;
    bool isProjectDocumentDirty() const;
    void markProjectDocumentClean();
    void beginGeneratedProjectDocument(const QString& path);
    // 项目资源关闭回调(由 Provider 的 CloseHandler 触发):清空优化问题、项目路径、
    // 托管工程根与快照基线,并把模型来源复位为"未跟踪",确保新工程不继承旧优化会话。
    void clearProjectDocumentContext();
    bool canCloseProjectDocument(QString* reason = nullptr) const;

Q_SIGNALS:
    // 所有会影响可持久化优化问题的控件和模型都汇入此信号；插件再通过规范快照
    // 判定 Provider 脏状态，避免仅改变运行进度或预览时错误标记项目。
    void projectDocumentChanged();

private:
    void setProblemWithManagedRoot(const StructureOptimizationProblem& problem,
                                   const QString& managedProjectRoot);
    QWidget* createVariablePage();
    QWidget* createTaskPage();
    QWidget* createSettingsPage();
    QWidget* createCandidatePage();
    QWidget* createReportPage();

    void updateRunState();
    void updateModelSourceStatus();
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
    void newProjectFromFrozenRequirements();
    void openProject();
    void saveProject();
    void exportResult();
    void addTask();
    void duplicateSelectedTask();
    void removeSelectedTask();
    void addConstraint();
    void duplicateSelectedConstraint();
    void removeSelectedConstraint();
    int selectedCandidateIndex() const;

    StructureOptimizationProblem _loadedProblem;
    StructureVariableTableModel* _variableModel = nullptr;
    OptimizationTaskTableModel* _taskModel = nullptr;
    StructureConstraintTableModel* _constraintModel = nullptr;
    StructureCandidateTableModel* _candidateModel = nullptr;
    StructureOptimizationController* _controller = nullptr;
    std::unique_ptr<CandidatePreviewController> _previewController;
    StructureOptimizationResult _lastResult;
    QString _projectPath;
    QString _managedProjectRoot;
    QString _projectDocumentPath;
    QByteArray _savedProjectDocumentSnapshot;
    mutable QByteArray _pendingProjectDocumentSnapshot;
    RobotModelSourceStatus _modelSourceStatus = RobotModelSourceStatus::Untracked;
    rw::models::WorkCell* _scenarioWorkCell = nullptr;
    rw::kinematics::State _scenarioState;
    RobWorkStudio* _studio = nullptr;

    QTabWidget* _tabs = nullptr;
    QTableView* _taskView = nullptr;
    QTableView* _constraintView = nullptr;
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
    QComboBox* _newConstraintKindCombo = nullptr;
    std::array<QDoubleSpinBox*, 6> _weightSpins = {{nullptr, nullptr, nullptr,
                                                      nullptr, nullptr, nullptr}};
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERWIDGET_HPP
