#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP

#include <rw/kinematics/State.hpp>

#include <QTabWidget>
#include <QWidget>

namespace rw { namespace models { class WorkCell; } }
namespace rws { class RobWorkStudio; }
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace rws {

class KinematicAnalysisWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KinematicAnalysisWidget(QWidget* parent = NULL);

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

private:
    void populateDevices ();
    void populateTcpFrames ();
    void buildTaskPointTab ();
    std::vector< TaskPoint > collectTaskPointsFromTable () const;
    void applyTaskPointResults (const std::vector< TaskPointReachabilityResult >& results,
                                double reachableRate);
    void setTaskPointTableColumnWidths ();
    rw::kinematics::State currentState () const;

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
};

}    // namespace rws

#endif
