#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP

#include "KinematicAnalysisTypes.hpp"
#include "TaskPointTableModel.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/State.hpp>

#include <QFutureWatcher>
#include <QSize>
#include <atomic>
#include <QTabWidget>
#include <QWidget>

#include <array>
#include <vector>

namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; class WorkCell; } }
namespace rw { namespace proximity { class CollisionDetector; } }

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableView;
class QTableWidget;
class QString;

namespace rws {

class KinematicAnalysisPlotWidget;
class RobWorkStudio;

class KinematicAnalysisWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit KinematicAnalysisWidget (QWidget* parent = NULL);

    QSize sizeHint () const override;
    QSize minimumSizeHint () const override;

    void setRobWorkStudio (RobWorkStudio* studio);
    void setWorkCell (rw::models::WorkCell* workcell);

  private Q_SLOTS:
    void refreshCurrentPose ();
    void solveIk ();
    void refreshIkSolutionView ();
    void updateIkSolutionDetails ();
    void applySelectedIkSolution ();
    void importCurrentPoseToIk ();
    void updateIkUnitDisplay ();
    void addTaskPointRow ();
    void removeSelectedTaskPointRow ();
    void importTaskPointsCsv ();
    void exportTaskPointsCsv ();
    void analyzeAllTaskPoints ();
    void analyzeSelectedTaskPoints ();
    void importCurrentTcpAsTaskPoint ();
    void applySelectedTaskPointBestQ ();
    void openSelectedTaskPointInIk ();
    void updateTaskPointSelectionButtons ();
    void sampleWorkspace ();
    void exportWorkspaceCsv ();
    void updateWorkspaceControls ();
    void openWorkspaceInVisualization ();
    void addPoseReachabilityRow ();
    void updatePoseReachabilityControls ();
    void analyzePoseReachability ();
    void handlePoseReachabilityFinished ();
    void exportPoseReachabilityCsv ();
    void refreshVisualization ();
    void refreshReport ();
    void exportReportJson ();
    void exportReportCsv ();
    void exportTaskPointResultsCsv ();
    void applyThresholds ();

  private:
    void populateDevices ();
    void populateTcpFrames ();
    void buildTaskPointTab ();
    void buildWorkspaceTab ();
    void buildPoseReachabilityTab ();
    void buildVisualizationTab ();
    void buildReportTab ();

    std::vector< TaskPoint > collectTaskPointsFromTable (QString* error = nullptr) const;
    std::vector< std::array< double, 3 > > collectPoseReachabilityPositions (
        QString* error = nullptr) const;

    void applyTaskPointResults (const std::vector< TaskPointReachabilityResult >& results,
                                double reachableRate);
    void applyWorkspaceResults (const std::vector< WorkspaceSample >& samples);
    void applyPoseReachabilityResults (const std::vector< PoseReachabilitySample >& samples);
    void updateReportSummary ();
    void setTaskPointTableColumnWidths ();
    void installTaskPointDelegates ();
    void setStatus (const QString& message);

    rw::kinematics::State currentState () const;
    bool shouldShowIkSolution (const KinematicIkSolution& solution) const;
    void setIkDetailsEmpty ();
    rw::core::Ptr< rw::models::Device > selectedDevice () const;
    rw::core::Ptr< rw::kinematics::Frame > selectedTcpFrame () const;
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetectorForAnalysis (
        bool requested, bool* unavailable) const;
    double ikXInputMeters () const;
    double ikYInputMeters () const;
    double ikZInputMeters () const;
    double ikRollInputDeg () const;
    double ikPitchInputDeg () const;
    double ikYawInputDeg () const;
    void setIkPoseMetersDeg (const std::array< double, 3 >& positionMeters,
                             const std::array< double, 3 >& rpyDeg);

    RobWorkStudio* _studio;
    rw::models::WorkCell* _workcell;

    QTabWidget* _tabs;
    QWidget* _currentPoseTab;
    QWidget* _ikTab;
    QWidget* _taskPointTab;
    QWidget* _workspaceTab;
    QWidget* _poseReachTab;
    QWidget* _visualizationTab;
    QWidget* _reportTab;

    QComboBox* _deviceCombo;
    QComboBox* _tcpFrameCombo;
    QPushButton* _refreshCurrentPoseButton;
    QLineEdit* _status;
    QTableWidget* _poseValueTable;
    QLabel* _poseIndicatorLabel;
    QTableWidget* _jointStatusTable;
    QTableWidget* _jacobianTable;
    QTableWidget* _singularTable;
    QLabel* _warningLabel;

    QLineEdit* _ikTargetNameEdit;
    QDoubleSpinBox* _ikXSpin;
    QDoubleSpinBox* _ikYSpin;
    QDoubleSpinBox* _ikZSpin;
    QDoubleSpinBox* _ikRollSpin;
    QDoubleSpinBox* _ikPitchSpin;
    QDoubleSpinBox* _ikYawSpin;
    QDoubleSpinBox* _ikDuplicateQThresholdSpin;
    QComboBox* _ikDistanceUnitCombo;
    QComboBox* _ikAngleUnitCombo;
    QPushButton* _ikImportCurrentPoseButton;
    QPushButton* _ikSolveButton;
    QPushButton* _ikApplyButton;
    QLabel* _ikSummaryLabel;
    QLabel* _ikSeedInfoLabel;
    QLabel* _ikCountSummaryLabel;
    QCheckBox* _ikShowUsableOnlyCheck;
    QCheckBox* _ikShowFailedCandidatesCheck;
    QTableWidget* _ikSolutionTable;
    QTableWidget* _ikDetailTable;

    QTableView* _taskPointTable;
    rws::TaskPointTableModel* _taskPointModel;
    QPushButton* _addTaskPointButton;
    QPushButton* _removeTaskPointButton;
    QPushButton* _importTaskPointsButton;
    QPushButton* _exportTaskPointsButton;
    QPushButton* _exportTaskPointResultsButton;
    QPushButton* _analyzeAllTaskPointsButton;
    QPushButton* _analyzeSelectedTaskPointsButton;
    QPushButton* _importCurrentTcpTaskPointButton;
    QPushButton* _applySelectedTaskPointBestQButton;
    QPushButton* _openSelectedTaskPointInIkButton;
    QLabel* _taskPointSummaryLabel;

    QSpinBox* _workspaceSampleCountSpin;
    QSpinBox* _workspaceGridStepsSpin;
    QSpinBox* _workspaceSeedSpin;
    QComboBox* _workspaceModeCombo;
    QCheckBox* _workspaceCollisionCheck;
    QComboBox* _workspaceColorModeCombo;
    QPushButton* _workspaceRunButton;
    QPushButton* _workspaceExportButton;
    QPushButton* _workspaceOpenVisualizationButton;
    QLabel* _workspaceSummaryLabel;
    QLabel* _workspaceDiagnosticsLabel;
    QTableWidget* _workspaceTable;

    QComboBox* _poseSourceCombo;
    QSpinBox* _poseDirectionSamplesSpin;
    QSpinBox* _poseRollSamplesSpin;
    QCheckBox* _poseCollisionCheck;
    QPushButton* _poseAddRowButton;
    QPushButton* _poseAnalyzeButton;
    QPushButton* _poseExportButton;
    QPushButton* _poseCancelButton;
    QFutureWatcher< std::vector< PoseReachabilitySample > >* _poseReachabilityWatcher;
    bool _poseReachabilityRunActive;
    std::atomic_bool _poseReachabilityCancelRequested;
    QLabel* _poseSummaryLabel;
    QLabel* _poseDiagnosticsLabel;
    QTableWidget* _posePositionTable;
    QTableWidget* _poseResultTable;

    QComboBox* _visualSourceCombo;
    QComboBox* _visualProjectionCombo;
    QComboBox* _visualColorModeCombo;
    QCheckBox* _visualShowPassCheck;
    QCheckBox* _visualShowWarningCheck;
    QCheckBox* _visualShowFailCheck;
    QCheckBox* _visualShowLabelsCheck;
    QLabel* _visualSummaryLabel;
    KinematicAnalysisPlotWidget* _visualPlot;

    QLabel* _reportSummaryLabel;
    QTableWidget* _reportWarningTable;
    QPushButton* _reportRefreshButton;
    QPushButton* _reportExportJsonButton;
    QPushButton* _reportExportCsvButton;
    QDoubleSpinBox* _thresholdNearLimitSpin;
    QDoubleSpinBox* _thresholdConditionWarningSpin;
    QDoubleSpinBox* _thresholdConditionFailSpin;
    QDoubleSpinBox* _thresholdSingularValueSpin;
    QDoubleSpinBox* _thresholdManipulabilitySpin;
    QDoubleSpinBox* _thresholdPositionToleranceSpin;
    QDoubleSpinBox* _thresholdOrientationToleranceSpin;
    QPushButton* _thresholdApplyButton;

    KinematicThresholds _thresholds;
    KinematicLengthUnit _ikLengthUnit;
    KinematicAngleUnit _ikAngleUnit;
    KinematicCurrentPoseResult _lastCurrentPose;
    KinematicIkAnalysisResult _lastIkResult;
    std::vector< TaskPointReachabilityResult > _lastTaskPointResults;
    std::vector< WorkspaceSample > _workspaceSamples;
    std::vector< PoseReachabilitySample > _poseReachabilitySamples;
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
