#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP

#include "KinematicAnalysisTypes.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/State.hpp>

#include <QSize>
#include <QTabWidget>
#include <QWidget>

namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; class WorkCell; } }
namespace rws { class RobWorkStudio; }
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace rws {

class KinematicAnalysisWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KinematicAnalysisWidget(QWidget* parent = NULL);

    QSize sizeHint () const override;
    QSize minimumSizeHint () const override;

    void setRobWorkStudio(RobWorkStudio* studio);
    void setWorkCell(rw::models::WorkCell* workcell);

private Q_SLOTS:
    void refreshCurrentPose ();
    void solveIk ();
    void applySelectedIkSolution ();
    void addTaskPointRow ();
    void removeSelectedTaskPointRow ();
    void importTaskPointsCsv ();
    void exportTaskPointsCsv ();
    void analyzeAllTaskPoints ();
    void sampleWorkspace ();
    void exportWorkspaceCsv ();
    void addPoseReachabilityRow ();
    void analyzePoseReachability ();
    void exportPoseReachabilityCsv ();
    void refreshReport ();
    void exportReportJson ();
    void exportReportCsv ();
    void applyThresholds ();

private:
    void populateDevices ();
    void populateTcpFrames ();
    void buildTaskPointTab ();
    void buildWorkspaceTab ();
    void buildPoseReachabilityTab ();
    void buildReportTab ();
    std::vector< TaskPoint > collectTaskPointsFromTable () const;
    std::vector< std::array< double, 3 > > collectPoseReachabilityPositions () const;
    void applyTaskPointResults (const std::vector< TaskPointReachabilityResult >& results,
                                double reachableRate);
    void applyWorkspaceResults (const std::vector< WorkspaceSample >& samples);
    void applyPoseReachabilityResults (const std::vector< PoseReachabilitySample >& samples);
    void updateReportSummary ();
    void setTaskPointTableColumnWidths ();
    void setStatus (const QString& message);
    rw::kinematics::State currentState () const;
    rw::core::Ptr< rw::models::Device > selectedDevice () const;
    rw::core::Ptr< rw::kinematics::Frame > selectedTcpFrame () const;

    RobWorkStudio* _studio;
    rw::models::WorkCell* _workcell;

    QTabWidget* _tabs;
    QWidget* _currentPoseTab;
    QWidget* _ikTab;
    QWidget* _taskPointTab;
    QWidget* _workspaceTab;
    QWidget* _poseReachTab;
    QWidget* _reportTab;

    QComboBox* _deviceCombo;
    QComboBox* _tcpFrameCombo;
    QPushButton* _refreshCurrentPoseButton;
    QLineEdit* _status;
    QTableWidget* _qTable;
    QTableWidget* _jointMarginTable;
    QTableWidget* _jacobianTable;
    QTableWidget* _singularTable;
    QLabel* _tcpPoseLabel;
    QLabel* _conditionLabel;
    QLabel* _manipulabilityLabel;
    QListWidget* _warningList;

    QLineEdit* _ikTargetNameEdit;
    QDoubleSpinBox* _ikXSpin;
    QDoubleSpinBox* _ikYSpin;
    QDoubleSpinBox* _ikZSpin;
    QDoubleSpinBox* _ikRollSpin;
    QDoubleSpinBox* _ikPitchSpin;
    QDoubleSpinBox* _ikYawSpin;
    QPushButton* _ikSolveButton;
    QPushButton* _ikApplyButton;
    QLabel* _ikSummaryLabel;
    QTableWidget* _ikSolutionTable;

    QTableWidget* _taskPointTable;
    QPushButton* _addTaskPointButton;
    QPushButton* _removeTaskPointButton;
    QPushButton* _importTaskPointsButton;
    QPushButton* _exportTaskPointsButton;
    QPushButton* _analyzeAllTaskPointsButton;
    QLabel* _taskPointSummaryLabel;

    QSpinBox* _workspaceSampleCountSpin;
    QSpinBox* _workspaceGridStepsSpin;
    QComboBox* _workspaceModeCombo;
    QCheckBox* _workspaceCollisionCheck;
    QComboBox* _workspaceColorModeCombo;
    QPushButton* _workspaceRunButton;
    QPushButton* _workspaceExportButton;
    QLabel* _workspaceSummaryLabel;
    QTableWidget* _workspaceTable;

    QComboBox* _poseSourceCombo;
    QSpinBox* _poseDirectionSamplesSpin;
    QSpinBox* _poseRollSamplesSpin;
    QCheckBox* _poseCollisionCheck;
    QPushButton* _poseAddRowButton;
    QPushButton* _poseAnalyzeButton;
    QPushButton* _poseExportButton;
    QLabel* _poseSummaryLabel;
    QTableWidget* _posePositionTable;
    QTableWidget* _poseResultTable;

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
    KinematicCurrentPoseResult _lastCurrentPose;
    std::vector< TaskPointReachabilityResult > _lastTaskPointResults;
    std::vector< WorkspaceSample > _workspaceSamples;
    std::vector< PoseReachabilitySample > _poseReachabilitySamples;
};

}    // namespace rws

#endif
