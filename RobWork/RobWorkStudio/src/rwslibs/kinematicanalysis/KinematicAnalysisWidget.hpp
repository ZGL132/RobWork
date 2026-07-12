#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP

#include "KinematicAnalysisTypes.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/State.hpp>

#include <QSize>
#include <QTabWidget>
#include <QWidget>

// 前置声明 RobWork 类型,避免在头文件引入过重的 include。
namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; class WorkCell; } }
namespace rw { namespace proximity { class CollisionDetector; } }
namespace rws { class RobWorkStudio; }
// Qt 类型前置声明,避免在头文件中包含完整 Qt 头。
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QString;

namespace rws {

// KinematicAnalysis 主面板。
// 把 6 个分析维度放在 QTabWidget 内:Current pose、IK、Task points、
// Workspace、Pose reachability、Report。所有真正的分析逻辑都委托给
// KinematicAnalyzer,这里只负责布局、状态保存、IO、表格展示。
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
    // 槽函数,基本一一对应 UI 上的按钮:
    void refreshCurrentPose ();         // 重新计算并显示当前 state 的运动学指标
    void solveIk ();                     // 用 IK tab 输入求解
    void refreshIkSolutionView ();       // 根据 _lastIkResult + 过滤器刷新 IK 结果表格
    void updateIkSolutionDetails ();     // 把选中行的详情写到 _ikDetailTable
    void applySelectedIkSolution ();     // 把选中解写回 RobWorkStudio
    void importCurrentPoseToIk ();
    void updateIkUnitDisplay ();
    void addTaskPointRow ();             // 任务点 tab 新增一行
    void removeSelectedTaskPointRow ();  // 删除选中任务点行
    void importTaskPointsCsv ();         // 从 CSV 导入任务点
    void exportTaskPointsCsv ();         // 导出任务点 CSV
    void analyzeAllTaskPoints ();        // 批量跑 IK
    void sampleWorkspace ();             // 工作空间采样
    void exportWorkspaceCsv ();          // 导出工作空间 CSV
    void addPoseReachabilityRow ();      // 新增位姿可达性位置行
    void analyzePoseReachability ();     // 跑位姿可达性
    void exportPoseReachabilityCsv ();   // 导出位姿可达性 CSV
    void refreshReport ();               // 重新汇总 Report tab
    void exportReportJson ();            // 导出 JSON 报告
    void exportReportCsv ();             // 导出 CSV 摘要
    void applyThresholds ();             // 把 Report tab 的阈值写回内部状态

private:
    // 构造/同步 UI 与 WorkCell 状态。
    void populateDevices ();
    void populateTcpFrames ();
    void buildTaskPointTab ();
    void buildWorkspaceTab ();
    void buildPoseReachabilityTab ();
    void buildReportTab ();

    // 表格 → POD 数据的转换。
    std::vector< TaskPoint > collectTaskPointsFromTable (QString* error = nullptr) const;
    std::vector< std::array< double, 3 > > collectPoseReachabilityPositions (
        QString* error = nullptr) const;

    // 把分析结果写回 UI 表格。
    void applyTaskPointResults (const std::vector< TaskPointReachabilityResult >& results,
                                double reachableRate);
    void applyWorkspaceResults (const std::vector< WorkspaceSample >& samples);
    void applyPoseReachabilityResults (const std::vector< PoseReachabilitySample >& samples);
    void updateReportSummary ();
    void setTaskPointTableColumnWidths ();
    void setStatus (const QString& message);

    // 当前 state / device / TCP 帧的统一获取入口。
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
    QWidget* _reportTab;

    // Current pose tab
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

    // IK tab
    QLineEdit* _ikTargetNameEdit;
    QDoubleSpinBox* _ikXSpin;
    QDoubleSpinBox* _ikYSpin;
    QDoubleSpinBox* _ikZSpin;
    QDoubleSpinBox* _ikRollSpin;
    QDoubleSpinBox* _ikPitchSpin;
    QDoubleSpinBox* _ikYawSpin;
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

    // Task points tab
    QTableWidget* _taskPointTable;
    QPushButton* _addTaskPointButton;
    QPushButton* _removeTaskPointButton;
    QPushButton* _importTaskPointsButton;
    QPushButton* _exportTaskPointsButton;
    QPushButton* _analyzeAllTaskPointsButton;
    QLabel* _taskPointSummaryLabel;

    // Workspace tab
    QSpinBox* _workspaceSampleCountSpin;
    QSpinBox* _workspaceGridStepsSpin;
    QComboBox* _workspaceModeCombo;
    QCheckBox* _workspaceCollisionCheck;
    QComboBox* _workspaceColorModeCombo;
    QPushButton* _workspaceRunButton;
    QPushButton* _workspaceExportButton;
    QLabel* _workspaceSummaryLabel;
    QTableWidget* _workspaceTable;

    // Pose reachability tab
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

    // Report tab
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

    // 缓存最近一次分析结果,供 Report tab / 导出功能使用。
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

#endif
