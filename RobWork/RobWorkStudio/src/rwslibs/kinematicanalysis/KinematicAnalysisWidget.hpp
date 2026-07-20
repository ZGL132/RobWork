#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP

// 三个核心数据/类型头:
//   - KinematicAnalysisTypes            :分析数据结构(状态、阈值、summary 等)
//   - KinematicAnalysisVisualizationTypes:可视化点/数据/标量/投影模式
//   - TaskPointTableModel               :任务点表格的 MVC 数据模型
#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalysisVisualizationTypes.hpp"
#include "TaskPointTableModel.hpp"

// RobWork 类型:Ptr 智能指针;State 工作单元不可变快照。
#include <rw/core/Ptr.hpp>
#include <rw/kinematics/State.hpp>

// QFutureWatcher:监听 QtConcurrent::run 异步任务完成,触发 finished 信号到主线程。
// QProgressBar :跨线程进度条(由 updatePoseReachabilityProgress 槽更新)。
// std::atomic_bool / std::shared_ptr 跨线程安全,无需加锁。
#include <QFutureWatcher>
#include <QProgressBar>
#include <QSize>
#include <atomic>
#include <QTabWidget>
#include <QWidget>

#include <array>
#include <memory>
#include <vector>

// 提前声明 RobWork 复杂类型,避免引入完整头。
namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; class WorkCell; } }
namespace rw { namespace proximity { class CollisionDetector; } }

// 提前声明 Qt UI 控件类(使用前向声明减少编译依赖)。
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

// =============================================================================
//  KinematicAnalysisWidget:KinematicAnalysis 插件主控件
// =============================================================================
//
// 这是 RobWorkStudio 插件"KinematicAnalysis"对应的 QWidget 主控件。
// 设计:与 KinematicAnalyzer(纯算法)解耦,本类只负责:
//   - 构建 5 个 tab(Current Pose / IK / Task Points / Workspace / Pose Reachability
//     / Visualization / Report);
//   - 收集 UI 配置 → 调用分析器;
//   - 把分析器结果写回 UI 控件;
//   - 异步执行(Workspace / Pose Reachability)时管理 worker 生命周期与取消;
//   - 把可视化点 click → RobWorkStudio state mutation。
//
// 线程策略:
//   - UI 线程:所有 setter / paintEvent / mousePressEvent
//   - 后台线程:QtConcurrent::run 启动的 worker,仅持有值快照(device/state/threshold)
//   - 跨线程通讯:std::shared_ptr<std::atomic_bool> 取消标志 +
//     QMetaObject::invokeMethod(Qt::QueuedConnection) 触发 UI 槽
class KinematicAnalysisWidget : public QWidget
{
    Q_OBJECT

  public:
    // 构造器:在 buildTabs() 之前完成所有 connect 准备。
    explicit KinematicAnalysisWidget (QWidget* parent = NULL);

    // 析构器:等待后台 worker 结束,恢复鼠标光标,避免卡死 UI。
    ~KinematicAnalysisWidget () override;

    QSize sizeHint () const override;
    QSize minimumSizeHint () const override;

    // 由 RobWorkStudio 加载插件时调用:注入主程序入口。
    void setRobWorkStudio (RobWorkStudio* studio);
    // 由 RobWorkStudio 在 WorkCell 加载/卸载时调用。
    void setWorkCell (rw::models::WorkCell* workcell);

  private Q_SLOTS:
    // ===================================================================
    //  Current Pose tab
    // ===================================================================
    // 重新读 State 并填充 "current pose" tab 的表格。
    void refreshCurrentPose ();

    // ===================================================================
    //  IK tab
    // ===================================================================
    // 用当前 _targetX/Y/Z + rpy/spin 输入,调 analyzeIk 并把解集刷到 IK tab。
    void solveIk ();
    // 用当前选中行刷 IK tab 下半部分的"details"表格。
    void refreshIkSolutionView ();
    // 选中解改变时更新 details 区域。
    void updateIkSolutionDetails ();
    // 把当前选中解的 Q 通过 setQ + setState 写回 RobWorkStudio。
    void applySelectedIkSolution ();
    // 把当前 currentPose 拷到 IK tab 的输入框。
    void importCurrentPoseToIk ();
    // 切换长度/角度单位时刷新所有 SpinBox 文本。
    void updateIkUnitDisplay ();

    // ===================================================================
    //  Task Points tab
    // ===================================================================
    // 增删行 / 导入导出 CSV / 全量或部分分析 / 把当前姿态导成任务点等。
    void addTaskPointRow ();
    void removeSelectedTaskPointRow ();
    void importTaskPointsCsv ();
    void exportTaskPointsCsv ();
    void analyzeAllTaskPoints ();
    void analyzeSelectedTaskPoints ();
    void importCurrentTcpAsTaskPoint ();
    // 把当前选中任务点的 best Q 写回 RobWorkStudio。
    void applySelectedTaskPointBestQ ();
    // 把选中任务点跳到 IK tab 并填入其位姿。
    void openSelectedTaskPointInIk ();
    // 选中行变化时更新 Apply/Open 按钮的可用状态。
    void updateTaskPointSelectionButtons ();

    // ===================================================================
    //  Workspace tab
    // ===================================================================
    // 启动后台采样(可被 Run / Cancel 多次触发)。
    void sampleWorkspace ();
    // 由 Cancel 按钮触发:设置 atomic 标志,worker 下一次循环会退出。
    void cancelWorkspaceSampling ();
    // 导出当前 samples 到 CSV。
    void exportWorkspaceCsv ();
    // mode/sample/grid 变化时:刷新 plan 标签 / 调整 grid 控件可用性。
    void updateWorkspaceControls ();
    // 后台 worker 通过 QMetaObject::invokeMethod 跨线程触发,更新进度条 + 标签。
    void updateWorkspaceProgress (qulonglong completedSamples,
                                  qulonglong plannedSamples);
    // worker finished 信号触发:恢复 UI 状态、读结果、刷表格。
    void handleWorkspaceFinished ();
    // 跳到 Visualization tab 并把 source 切到 Workspace。
    void openWorkspaceInVisualization ();

    // ===================================================================
    //  Pose Reachability tab
    // ===================================================================
    // 增加一行手动位置输入。
    void addPoseReachabilityRow ();
    // mode/dir/roll 变化时:刷新 plan 标签。
    void updatePoseReachabilityControls ();
    // 启动后台分析(可被 Run / Cancel 多次触发)。
    void analyzePoseReachability ();
    void updatePoseReachabilityProgress (qulonglong completedTargets,
                                         qulonglong plannedTargets);
    void handlePoseReachabilityFinished ();
    // 导出当前 samples 到 CSV。
    void exportPoseReachabilityCsv ();

    // ===================================================================
    //  Visualization tab
    // ===================================================================
    // source 变化时:动态填充 Color 下拉(只显示支持的标量模式)。
    void updateVisualizationControls ();
    // 重新计算可视化数据 + 刷 plot。
    void refreshVisualization ();
    // 重置视角(Fit):当前通过 refreshVisualization 模拟(没有持久平移/缩放)。
    void resetVisualizationView ();
    // 导出当前 plot 为 PNG(1400×900 默认尺寸,布局与 paintPlot 一致)。
    void exportVisualizationPng ();
    // 跳到 Visualization tab 并把 source 切到 Pose reachability。
    void openPoseReachabilityInVisualization ();
    // 接到 plot 的 visualPointClicked 信号:把点的 Q 写到 RobWorkStudio state。
    void applyVisualizationPointQ (rws::AnalysisVisualPoint point);

    // ===================================================================
    //  Report tab
    // ===================================================================
    void refreshReport ();
    void exportReportJson ();
    void exportReportCsv ();
    void exportTaskPointResultsCsv ();

    // 阈值 SpinBox 改后应用。
    void applyThresholds ();

  private:
    // ===================================================================
    //  Tab 构建
    // ===================================================================
    void populateDevices ();    // 填充顶部 device combo
    void populateTcpFrames ();  // 填充 TCP frame combo
    void buildTaskPointTab ();
    void buildWorkspaceTab ();
    void buildPoseReachabilityTab ();
    void buildVisualizationTab ();
    void buildReportTab ();

    // 从表格行解析 TaskPoint / Position,失败时填 error 字符串给 UI。
    std::vector< TaskPoint > collectTaskPointsFromTable (QString* error = nullptr) const;
    std::vector< std::array< double, 3 > > collectPoseReachabilityPositions (
        QString* error = nullptr) const;

    // 把分析器结果写回 UI 表格 + summary 标签 + 启用导出按钮。
    void applyTaskPointResults (const std::vector< TaskPointReachabilityResult >& results,
                                double reachableRate);
    void applyWorkspaceResults (const std::vector< WorkspaceSample >& samples);
    void applyPoseReachabilityResults (const std::vector< PoseReachabilitySample >& samples);
    void updateReportSummary ();          // 重新汇总 Report tab 数据
    void setTaskPointTableColumnWidths ();
    void installTaskPointDelegates ();
    // 把状态消息写入顶部 status QLineEdit(只读)。
    void setStatus (const QString& message);

    // ===================================================================
    //  状态/单位换算 helper
    // ===================================================================
    // currentState:从 RobWorkStudio 抓当前 state 快照。
    rw::kinematics::State currentState () const;
    // 给定一个 IK 解,根据 "show usable only" 过滤开关判断是否展示。
    bool shouldShowIkSolution (const KinematicIkSolution& solution) const;
    // 清空 IK details 区域(未选中任何解时调用)。
    void setIkDetailsEmpty ();
    // 当前设备 combo 选中的 Device 指针;空时返回 NULL。
    rw::core::Ptr< rw::models::Device > selectedDevice () const;
    // 当前 TCP frame combo 选中的 Frame 指针;空时回退到 device->getEnd()。
    rw::core::Ptr< rw::kinematics::Frame > selectedTcpFrame () const;
    // 获取碰撞检测器;若 requested 但 WorkCell 无碰撞模型,unavailable 置 true。
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetectorForAnalysis (
        bool requested, bool* unavailable) const;
    // IK 输入框的"显示单位 → 米"换算(供 solveIk 用)。
    double ikXInputMeters () const;
    double ikYInputMeters () const;
    double ikZInputMeters () const;
    double ikRollInputDeg () const;
    double ikPitchInputDeg () const;
    double ikYawInputDeg () const;
    // 把 IK tab 输入框值写回 SpinBox(m / 度)。
    void setIkPoseMetersDeg (const std::array< double, 3 >& positionMeters,
                             const std::array< double, 3 >& rpyDeg);

    // ===================================================================
    //  注入的外部句柄
    // ===================================================================
    // _studio :RobWorkStudio 主程序,用于 setState(写 state)、getWorkCell。
    // _workcell:WorkCell 指针(非拥有),用于构造后台独立 collision detector。
    RobWorkStudio* _studio;
    rw::models::WorkCell* _workcell;

    // ===================================================================
    //  Tab 容器(每个 tab 一个 QWidget)
    // ===================================================================
    QTabWidget* _tabs;                  // 外层 QTabWidget
    QWidget* _currentPoseTab;           // Tab 0:当前位姿
    QWidget* _ikTab;                    // Tab 1:IK 求解
    QWidget* _taskPointTab;             // Tab 2:任务点表格
    QWidget* _workspaceTab;             // Tab 3:工作空间采样
    QWidget* _poseReachTab;             // Tab 4:位姿可达性
    QWidget* _visualizationTab;         // Tab 5:可视化
    QWidget* _reportTab;                // Tab 6:报告

    // ===================================================================
    //  Current Pose tab 控件
    // ===================================================================
    QComboBox* _deviceCombo;                          // 顶部 device 选择
    QComboBox* _tcpFrameCombo;                        // 顶部 TCP frame 选择
    QPushButton* _refreshCurrentPoseButton;           // 重新读 State
    QLineEdit* _status;                               // 状态消息(只读)
    QTableWidget* _poseValueTable;                    // 6 元 TCP 位姿 + 关节值
    QLabel* _poseIndicatorLabel;                      // 状态颜色指示(Pass/Warn/Fail)
    QTableWidget* _jointStatusTable;                  // 各关节裕度详情
    QTableWidget* _jacobianTable;                     // 6×n 雅可比矩阵
    QTableWidget* _singularTable;                     // 奇异值序列
    QLabel* _warningLabel;                            // 综合告警文字

    // ===================================================================
    //  IK tab 控件
    // ===================================================================
    QLineEdit* _ikTargetNameEdit;                     // 目标名
    QDoubleSpinBox* _ikXSpin;                          // 目标 x (m 或显示单位)
    QDoubleSpinBox* _ikYSpin;                          // 目标 y
    QDoubleSpinBox* _ikZSpin;                          // 目标 z
    QDoubleSpinBox* _ikRollSpin;                       // 目标 roll (度)
    QDoubleSpinBox* _ikPitchSpin;                      // 目标 pitch
    QDoubleSpinBox* _ikYawSpin;                        // 目标 yaw
    QDoubleSpinBox* _ikDuplicateQThresholdSpin;        // IK 解去重 Q 阈值
    QComboBox* _ikDistanceUnitCombo;                   // 长度显示单位
    QComboBox* _ikAngleUnitCombo;                     // 角度显示单位
    QPushButton* _ikImportCurrentPoseButton;           // 导入当前 TCP
    QPushButton* _ikSolveButton;                      // 触发 solveIk
    QPushButton* _ikApplyButton;                       // 把选中解写回 state
    QLabel* _ikSummaryLabel;                           // IK 整体状态行
    QLabel* _ikSeedInfoLabel;                          // 求解器配置摘要
    QLabel* _ikCountSummaryLabel;                      // 解数量统计
    QCheckBox* _ikShowUsableOnlyCheck;                 // 只看可用解
    QCheckBox* _ikShowFailedCandidatesCheck;           // 显示诊断性 Fail 解
    QTableWidget* _ikSolutionTable;                    // 候选解列表
    QTableWidget* _ikDetailTable;                      // 选中解详情

    // ===================================================================
    //  Task Points tab 控件
    // ===================================================================
    QTableView* _taskPointTable;                       // 任务点表格(view)
    rws::TaskPointTableModel* _taskPointModel;         // 任务点数据模型
    QPushButton* _addTaskPointButton;                  // 增加行
    QPushButton* _removeTaskPointButton;               // 删除选中行
    QPushButton* _importTaskPointsButton;              // 导入 CSV
    QPushButton* _exportTaskPointsButton;              // 导出 CSV
    QPushButton* _exportTaskPointResultsButton;        // 导出分析结果 CSV
    QPushButton* _analyzeAllTaskPointsButton;          // 全量分析
    QPushButton* _analyzeSelectedTaskPointsButton;     // 选中行分析
    QPushButton* _importCurrentTcpTaskPointButton;     // 把当前 TCP 当作任务点
    QPushButton* _applySelectedTaskPointBestQButton;   // 把选中任务点的 best Q 写回 state
    QPushButton* _openSelectedTaskPointInIkButton;        // 跳到 IK tab 并填入位姿
    QLabel* _taskPointSummaryLabel;                     // 任务点聚合状态行

    // ===================================================================
    //  Workspace tab 控件
    // ===================================================================
    QSpinBox* _workspaceSampleCountSpin;               // 随机/网格模式的总采样数
    QSpinBox* _workspaceGridStepsSpin;                  // 网格模式每关节步数
    QSpinBox* _workspaceSeedSpin;                       // RNG 种子(可复现)
    QComboBox* _workspaceModeCombo;                    // Random / Grid
    QCheckBox* _workspaceCollisionCheck;                // 是否启用碰撞检查
    QComboBox* _workspaceColorModeCombo;               // 表格着色策略
    QPushButton* _workspaceRunButton;                  // 启动后台采样
    QPushButton* _workspaceExportButton;               // 导出 CSV
    QPushButton* _workspaceOpenVisualizationButton;    // 跳到 Visualization
    QPushButton* _workspaceCancelButton;                // 协作取消
    // _workspaceWatcher:监听 QtConcurrent worker 完成,触发 handleWorkspaceFinished。
    QFutureWatcher< std::vector< WorkspaceSample > >* _workspaceWatcher;
    bool _workspaceRunActive;                            // 是否正在后台运行
    bool _workspaceCollisionUnavailable;                // 当前 run 碰撞检查不可用
    // _workspaceCancelRequested:跨线程共享的取消标志,worker 在循环里检查。
    std::shared_ptr< std::atomic_bool > _workspaceCancelRequested;
    QProgressBar* _workspaceProgressBar;                // 进度条(已自动缩放)
    QLabel* _workspaceProgressLabel;                    // 进度文本 (X / Y sample(s))
    QLabel* _workspaceSummaryLabel;                     // Workspace summary 文本
    QLabel* _workspaceDiagnosticsLabel;                 // plan / theoretical / capped
    QTableWidget* _workspaceTable;                      // 样本表(最多 500 行)

    // ===================================================================
    //  Pose Reachability tab 控件
    // ===================================================================
    QComboBox* _poseSourceCombo;                        // 位置来源(Task Points / Manual)
    QSpinBox* _poseDirectionSamplesSpin;                // 方向数(单位球)
    QSpinBox* _poseRollSamplesSpin;                     // 滚动数(绕 Z)
    QCheckBox* _poseCollisionCheck;                     // 碰撞检查
    QPushButton* _poseAddRowButton;                     // 增加手动位置行
    QPushButton* _poseAnalyzeButton;                   // 启动后台分析
    QPushButton* _poseExportButton;                    // 导出 CSV
    QPushButton* _poseCancelButton;                    // 协作取消
    QPushButton* _poseOpenVisualizationButton;         // 跳到 Visualization
    QFutureWatcher< std::vector< PoseReachabilitySample > >* _poseReachabilityWatcher;
    bool _poseReachabilityRunActive;
    bool _poseReachabilityCollisionUnavailable;
    std::shared_ptr< std::atomic_bool > _poseReachabilityCancelRequested;
    QLabel* _poseSummaryLabel;
    QLabel* _poseDiagnosticsLabel;
    QProgressBar* _poseProgressBar;                    // 进度条(已自动缩放)
    QLabel* _poseProgressLabel;                        // 进度文本 (X / Y IK target)
    QTableWidget* _posePositionTable;                  // 手动位置输入
    QTableWidget* _poseResultTable;                    // 结果(最多 500 行)

    // ===================================================================
    //  Visualization tab 控件
    // ===================================================================
    QComboBox* _visualSourceCombo;                     // 数据源(Task / Workspace / Pose)
    QComboBox* _visualProjectionCombo;                 // 投影平面(XY / XZ / YZ)
    QComboBox* _visualColorModeCombo;                  // 标量模式(由 updateVisualizationControls 动态填充)
    QCheckBox* _visualShowPassCheck;                   // 显示 Pass 点
    QCheckBox* _visualShowWarningCheck;                // 显示 Warning 点
    QCheckBox* _visualShowFailCheck;                   // 显示 Fail 点
    QCheckBox* _visualShowUnknownCheck;                // 显示 Unknown 点
    QCheckBox* _visualShowLabelsCheck;                 // 显示 label 文本
    QCheckBox* _visualShowGridCheck;                   // 显示网格 + 刻度
    QCheckBox* _visualShowLegendCheck;                // 显示图例
    QDoubleSpinBox* _visualPointSizeSpin;              // 散点半径
    QPushButton* _visualResetViewButton;               // Fit 视角
    QPushButton* _visualExportPngButton;                // 导出 PNG
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

    // _thresholds:当前生效的阈值集合(可由用户改 Report tab)。
    // _ikLengthUnit / _ikAngleUnit:IK tab 的显示单位(用户切换)。
    // _lastCurrentPose:最近一次刷新的 current pose(供回写 state 时使用)。
    // _lastIkResult:最近一次 IK 求解结果(缓存,避免重复求解)。
    // _lastTaskPointResults:任务点分析结果(供 Report / 取消重算复用)。
    // _workspaceSamples:工作空间完整结果(供 Visualization 用)。
    // _poseReachabilitySamples:位姿可达性完整结果(供 Visualization 用)。
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
