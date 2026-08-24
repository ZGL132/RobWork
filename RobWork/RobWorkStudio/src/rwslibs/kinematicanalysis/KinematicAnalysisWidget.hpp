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
#include <rw/math/Q.hpp>

// QFutureWatcher:监听 QtConcurrent::run 异步任务完成,触发 finished 信号到主线程。
// QProgressBar :跨线程进度条(由 updatePoseReachabilityProgress 槽更新)。
// std::atomic_bool / std::shared_ptr 跨线程安全,无需加锁。
#include <QFutureWatcher>
#include <QByteArray>
#include <QProgressBar>
#include <QSize>
#include <QPointer>
#include <atomic>
#include <QWidget>
#include <QJsonObject>

#include <array>
#include <memory>
#include <string>
#include <vector>

// 提前声明 RobWork 复杂类型,避免引入完整头。
namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; class WorkCell; } }
namespace rw { namespace proximity { class CollisionDetector; } }

// 提前声明 Qt UI 控件类(使用前向声明减少编译依赖)。
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QScrollArea;
class QTableView;
class QTableWidget;
class QToolButton;
class QTabWidget;
class QString;

namespace rws {

class KinematicAnalysisPlotWidget;
class KinematicPlotDialog;
class KinematicThresholdsDialog;
class RobWorkStudio;
struct KinematicAnalysisReport;
struct KinematicAnalysisReportFilters;

// 包络异步计算的单次运行结果:envelope 为计算产物;generation 用于丢弃过期请求
// (方向数 / 参数在途变化时,旧任务的返回结果会被判定为过期);cancelled 表示被
// 用户取消(此时 envelope 无效);errorMessage 携带异常文本便于 UI 展示。
struct WorkspaceEnvelopeRunResult
{
    // 计算得到的包络数据(有效时 valid == true)
    AnalysisEnvelopeData envelope;
    // 发起该请求时的生成号,用于防过期丢弃
    int generation = 0;
    // 是否被取消(取消时不应使用 envelope)
    bool cancelled = false;
    // 计算过程中的异常信息(空表示正常)
    QString errorMessage;
};

struct RequirementValidationRunResult
{
    RequirementExecutionSet execution;
    RequirementValidationSummary taskSummary;
    std::vector< RegionCoverageResult > regionResults;
    quint64 sessionGeneration = 0;
    bool cancelled = false;
    QString errorMessage;
};

// 包络缓存键:完全刻画"当前请求的包络输入"。除显式配置(投影 / 方向数 / 坐标迭代)
// 外还包含设备与 TCP 指针及关节上下限——关节界限变化会显著改变包络形状,
// 因此必须作为缓存键的一部分参与相等比较,否则会误命中陈旧包络。
struct WorkspaceEnvelopeCacheKey
{
    // 采样所用的设备
    const rw::models::Device* device = nullptr;
    // 采样所用的 TCP 帧
    const rw::kinematics::Frame* tcpFrame = nullptr;
    // 投影平面(XY / XZ / YZ)
    VisualProjection projection = VisualProjection::XY;
    // 方向采样数(角度采样密度)
    int angularDirections = 0;
    // 坐标迭代数
    int coordinateIterations = 0;
    // 设备各关节下限
    std::vector< double > lowerBounds;
    // 设备各关节上限
    std::vector< double > upperBounds;

    bool operator== (const WorkspaceEnvelopeCacheKey& other) const;
};

// =============================================================================
//  KinematicAnalysisWidget:KinematicAnalysis 插件主控件
// =============================================================================
//
// 这是 RobWorkStudio 插件"KinematicAnalysis"对应的 QWidget 主控件。
// 设计:与 KinematicAnalyzer(纯算法)解耦,本类只负责:
//   - 构建 Diagnose / Validate / Explore 三个工作流页;
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
    // 构造器:创建三页工作流及其信号连接。
    explicit KinematicAnalysisWidget (QWidget* parent = NULL);

    // 析构器:等待后台 worker 结束,恢复鼠标光标,避免卡死 UI。
    ~KinematicAnalysisWidget () override;

    QSize sizeHint () const override;
    QSize minimumSizeHint () const override;

    // 由 RobWorkStudio 加载插件时调用:注入主程序入口。
    void setRobWorkStudio (RobWorkStudio* studio);
    // 由 RobWorkStudio 在 WorkCell 加载/卸载时调用。
    void setWorkCell (rw::models::WorkCell* workcell);
    QString statusMessage () const;

    Q_INVOKABLE bool loadFrozenRequirementDocument (const QByteArray& json);
    Q_INVOKABLE bool setRequirementExecutionDocument (const QJsonObject& json);

    // 项目 Provider 使用的无对话框文档接口。Widget 只读写 Provider 传入的暂存路径，
    // 不直接改写项目正式文件；正式提交由 ProjectSaveTransaction 统一完成。
    bool loadProjectDocument (const QString& path, QString* error = nullptr);
    bool saveProjectDocument (const QString& targetPath, QString* error = nullptr);
    bool isProjectDocumentDirty () const;
    void markProjectDocumentClean ();
    bool canCloseProjectDocument (QString* reason = nullptr) const;
    // 首次编辑生成资源时建立项目内 JSON 的会话基线；该基线为空，使当前配置在用户保存项目
    // 时进入统一事务创建文件，而不是在编辑瞬间直接写入磁盘。
    void beginProjectDocument (const QString& path);
    // 项目关闭或切换时释放仅用于脏比较的路径和快照，防止旧项目的基线影响新项目。
    void clearProjectDocumentContext ();

    //! Returns the canonical report for the current analysis or requirements validation state.
    KinematicAnalysisReport buildReportForExport () const;

  private Q_SLOTS:
    // 按"WorkCell → 设备 → TCP 帧"前置条件链统一刷新工作流按钮可用性。
    void refreshWorkflowControls ();
    // 切换校验数据源(Local Tasks ↔ Frozen Requirements)时显隐对应控件。
    void updateMode2DataSource (int index);
    void openFrozenRequirementsForValidation ();
    // 对冻结执行契约做 Verified 级一致性校验(批量分析入口)。
    void validateRequirements ();
    void handleRequirementValidationFinished ();
    // 只校验当前选中的本地任务行 / 冻结任务 / 冻结区域。
    void validateSelectedMode2Source ();
    // 加载冻结工件后,把任务与区域先以"未校验"占位行刷进结果表。
    void populateFrozenRequirementSources ();
    void refreshValidationSummary ();
    void updateValidationInspector ();
    void setValidationInspectorEmpty ();
    void selectPreferredValidationResult ();
    void selectValidationResult (bool region, const QString& stableId);
    // 双击 Validate 任务行时,将该任务排序后的最佳 IK 候选解写回 3D 状态。
    void applyValidatedTaskBestCandidate (const QString& taskId);
    void applyRequirementValidationResult (const RequirementValidationRunResult& result);
    void startCapabilityExploration ();
    void cancelCapabilityExploration ();
    void updateCapabilityExplorationProgress (qulonglong completedSamples,
                                              qulonglong plannedSamples);
    void handleCapabilityExplorationFinished ();

    // ---- Diagnose 候选解工作流(候选检查器)----
    // 这是 Diagnose 页“候选解检查器”的核心交互闭环:用户编辑 IK 目标位姿后点击
    // Solve 得到一组候选解;候选表按筛选器展示,选中某条候选时联动刷新下方的
    // Solution inspector / Health summary / 关节表 / 雅可比汇总;双击候选行或点击
    // Apply selected Q 即可把该解的关节值写回 RobWorkStudio state。
    // Diagnose candidate workflow.
    void solveIk ();
    // Refresh the inspector from the selected stable candidate index.
    void refreshIkSolutionView ();
    // Update candidate-owned health, joint and Jacobian diagnostics.
    void updateIkSolutionDetails ();
    // 把当前选中解的 Q 通过 setQ + setState 写回 RobWorkStudio。
    // 该槽在点击“Apply selected Q”按钮或双击候选表行时触发;写入前会校验结果
    // 未过期、选中解索引有效、候选解可安全应用(非 Fail / 无碰撞)且 Q 维度匹配,
    // 任一校验不过都只提示错误而不写回,防止把陈旧或不安全位姿推入当前 state。
    void applySelectedIkSolution ();
    // Refresh the current TCP snapshot and synchronize it to the IK target.
    // 点击“Refresh TCP Pose to IK Target”:先用 analyzeCurrentPose 刷新当前位姿快照,再把
    // base→TCP 变换的平移 / RPY 按当前单位回填到 6 个 IK 目标输入框,并标记旧结果
    // 过期;这样用户可直接在“当前位姿”基础上微调后重新 Solve。
    void refreshAndSyncTcp ();
    // 切换长度/角度单位时刷新所有 SpinBox 文本。
    void updateUnitDisplay ();

    // ===================================================================
    //  Task Points tab
    // ===================================================================
    // 增删行 / 导入导出 CSV / 全量或部分分析 / 把当前姿态导成任务点等。
    void addTaskPointRow ();
    void removeSelectedTaskPointRow ();
    void importTaskPointsCsv ();
    void importFrozenRequirements ();
    void exportTaskPointsCsv ();
    void analyzeAllTaskPoints ();
    void analyzeSelectedTaskPoints ();
    void importCurrentTcpAsTaskPoint ();
    // 把当前选中任务点的 best Q 写回 RobWorkStudio。
    void applySelectedTaskPointBestQ ();
    // 把选中任务点打开到 Diagnose 并填入其位姿。
    void openSelectedTaskPointInIk ();
    // 选中行变化时更新 Apply/Open 按钮的可用状态。
    void updateTaskPointSelectionButtons ();
    void updateTaskPointDetails ();

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
    // Open the modeless plot window backed by the current visualization snapshot.
    // 打开无模式独立 plot 窗口,显示 Widget-owned 可视化快照。
    void openKinematicPlotDialog ();
    // 跳到 Visualization tab 并把 source 切到 Pose reachability。
    void openPoseReachabilityInVisualization ();
    // 接到 plot 的 visualPointClicked 信号:把点的 Q 写到 RobWorkStudio state。
    void applyVisualizationPointQ (rws::AnalysisVisualPoint point);

    // 包络异步计算结果到达。
    void onEnvelopeFinished ();
    // 方向数防抖超时后重新请求包络计算。
    void onEnvelopeDebounceTimeout ();

    // ===================================================================
    //  Report tab
    // ===================================================================
    void refreshReport ();
    void exportReportJson ();
    void exportReportCsv ();
    void exportTaskPointResultsCsv ();

    // 阈值 SpinBox 改后应用。
    void applyThresholds ();
    // 打开独立阈值设置对话框(事务式:Accept 才生效,Cancel 不改任何状态)。
    void openThresholdSettingsDialog ();

  Q_SIGNALS:
    // 领域配置发生实际变化后通知插件更新 Provider 脏状态；选择结果、焦点变化等
    // 界面状态不会触发该信号，因为它们不属于项目文档内容。
    void projectDocumentChanged ();
    void frozenRequirementValidationCompleted (const QString& requirementFingerprint,
                                               bool passed);

  private:
    // ===================================================================
    //  Tab 构建
    // ===================================================================
    void refreshProjectDefaultContext ();
    void updateProjectDefaultTcpControl ();
    void setSelectedTcpAsProjectDefault ();
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
    void updateWorkspaceSampleDetails ();
    void applyPoseReachabilityResults (const std::vector< PoseReachabilitySample >& samples);
    void updateReportSummary ();          // 重新汇总 Report tab 数据
    // 从 Report tab 四个过滤下拉 + 区域文本框中收集视图过滤条件。
    KinematicAnalysisReportFilters reportFilters () const;
    // 按紧凑模式列隐藏策略设置任务点表格列宽(Id/Name/Ref/TCP/Status 稳定可见)。
    void setTaskPointTableColumnWidths ();
    // 安装任务点表格的单元格编辑器(refFrame/tcpFrame 下拉 + 数值/布尔 delegate)。
    void installTaskPointDelegates ();
    // 把状态消息写入顶部 status QLineEdit(只读)。
    void setStatus (const QString& message);
    // 将当前可编辑控件转换为项目 JSON 配置；分析结果、缓存和临时进度均不进入快照。
    QByteArray projectDocumentSnapshot () const;
    // 在加载项目文档时恢复所有可编辑输入，并清空旧 WorkCell 上下文产生的分析结果。
    void applyProjectDocumentSnapshot (const QByteArray& json, QString* error);
    // 把可视化数据推给独立 plot 窗口(含投影/过滤/点径/单位)。
    void applyVisualDataToPlots (const AnalysisVisualData& data,
                                 VisualProjection projection);
    // 清空 _visualData 并用空数据刷新 plot(WorkCell 卸载时调用)。
    void clearVisualizationData ();
    void clearAnalysisSessionState (bool detachWorkCell);
    // Refreshes the report snapshot only. Candidate diagnostics never read this state.
    bool refreshCurrentPoseSnapshot (QString* error = nullptr);

    // ===================================================================
    //  状态/单位换算 helper
    // ===================================================================
    // currentState:从 RobWorkStudio 抓当前 state 快照。
    rw::kinematics::State currentState () const;
    // 给定一个 IK 解,根据 "show usable only" 过滤开关判断是否展示。
    bool shouldShowIkSolution (const KinematicIkSolution& solution) const;
    // 清空 IK details 区域(未选中任何解时调用)。
    void setIkDetailsEmpty ();
    // IK 目标变化后把结果标记为过期并清空候选表,防止 Apply 写入陈旧位姿。
    void invalidateIkResultPresentation ();
    // 当前设备 combo 选中的 Device 指针;空时返回 NULL。
    rw::core::Ptr< rw::models::Device > selectedDevice () const;
    // 当前 TCP frame combo 选中的 Frame 指针;空时回退到 device->getEnd()。
    rw::core::Ptr< rw::kinematics::Frame > selectedTcpFrame () const;
    // 请求取消在途包络计算:置取消标志、停防抖定时器、必要时等待 worker 结束。
    void cancelEnvelopeRequest (bool waitForFinished);
    // 使包络缓存失效,任何影响包络形状的输入变化后都必须调用。
    void invalidateEnvelopeCache ();
    void stateChangedListener (const rw::kinematics::State& state);
    WorkspaceEnvelopeCacheKey makeEnvelopeCacheKey (
        const rw::models::Device* device,
        const rw::kinematics::Frame* tcpFrame,
        VisualProjection projection,
        int angularDirections,
        int coordinateIterations) const;
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
    // 把 Diagnose 目标值写回 SpinBox(m / 度)。
    void setIkPoseMetersDeg (const std::array< double, 3 >& positionMeters,
                              const std::array< double, 3 >& rpyDeg);
    bool loadFrozenRequirementDocument (const QByteArray& json,
                                        const std::string& artifactBaseDirectory);

    // ===================================================================
    //  注入的外部句柄
    // ===================================================================
    // _studio :RobWorkStudio 主程序,用于 setState(写 state)、getWorkCell。
    // _workcell:WorkCell 指针(非拥有),用于构造后台独立 collision detector。
    RobWorkStudio* _studio;
    rw::models::WorkCell* _workcell;

    // ===================================================================
    //  Three-mode workflow shell
    // ===================================================================
    QTabWidget* _workflowTabs;
    QScrollArea* _diagnoseScroll;
    QScrollArea* _exploreScroll;
    // 三个工作流页面:Diagnose(当前位姿诊断)/ Validate Requirements(冻结需求校验)
    // / Explore Capability(能力探索)。每一页由独立滚动区域承载。
    QWidget* _diagnoseWorkflowPage;
    QWidget* _validateWorkflowPage;
    QWidget* _exploreWorkflowPage;
    // ---- Validate Requirements 页:数据源选择与批量操作按钮 ----
    // _mode2DataSourceCombo:选择校验数据源(Local Tasks 可编辑 / Frozen Requirements 只读)。
    // _mode2LoadJsonButton:加载冻结需求 JSON 工件。
    // _mode2ValidateAllButton:全量校验(本地任务 → 批量 IK 分析;冻结需求 → 全量一致性校验)。
    // _mode2ValidateSelectedButton:只校验选中条目(本地任务选中行 / 冻结任务或区域)。
    // _mode2AddButton / _mode2RemoveButton:仅在 Local Tasks 数据源下增删任务点行。
    QComboBox* _mode2DataSourceCombo;
    QPushButton* _mode2LoadJsonButton;
    QPushButton* _mode2ValidateAllButton;
    QPushButton* _mode2ValidateSelectedButton;
    QPushButton* _mode2AddButton;
    QPushButton* _mode2RemoveButton;
    // _validateRequirementStateLabel:工件加载 / 校验状态文本。
    QLabel* _validateRequirementStateLabel;
    QLabel* _validateSummaryLabel;
    QLabel* _validateInspectorTitleLabel;
    // _validateTaskResultTable:任务级结果(ID/Name/Feasibility/Quality/Stage/Level)。
    QTableWidget* _validateTaskResultTable;
    QTableWidget* _validateInspectorTable;
    // _validateRegionSummaryTable:工作区域汇总(ID/Name/Directions/F/Quality/Stage)。
    QTableWidget* _validateRegionSummaryTable;
    // _validateRegionCellTable:工作区域单元级结果(逐单元覆盖评估)。
    QTableWidget* _validateRegionCellTable;
    // _validateDiagnosticsToggle:展开 / 收起"Diagnostics"诊断区(显示工件溯源与逐单元结果)。
    QToolButton* _validateDiagnosticsToggle;
    QWidget* _validateDiagnosticsContent;
    // _validateProvenanceLabel:冻结工件溯源信息(需求 / 模型 / 环境指纹)。
    QLabel* _validateProvenanceLabel;
    QLabel* _validateTaskSectionTitle;
    QLabel* _validateRegionSectionTitle;
    // _validateOrientationProbeLabel:选中区域的方向采样数提示(Directions / Rolls)。
    QLabel* _validateOrientationProbeLabel;
    // ---- Explore Capability 页:能力探索(工作空间采样)后台执行 ----
    // Run / Cancel + Workspace 采样参数。
    QPushButton* _exploreRunButton;
    QPushButton* _exploreCancelButton;
    QSpinBox* _exploreSamplesSpin;
    QComboBox* _exploreCapabilityCombo;
    QComboBox* _workspaceSamplingStrategyCombo;
    QSpinBox* _exploreSeedSpin;
    QSpinBox* _exploreGridStepsSpin;
    // Workspace 参数行的文字标签:随采样策略(随机 / 网格)显隐。
    QLabel* _exploreSamplesLabel;
    QLabel* _exploreSeedLabel;
    QLabel* _exploreGridStepsLabel;
    // _exploreStateLabel:能力探索运行状态文本(Idle / Running / Cancellation requested)。
    QLabel* _exploreStateLabel;
    // 探索运行状态与后台执行句柄:RunActive 表示在跑,CancellationRequested 表示已请求
    // 取消;watcher 监听 QtConcurrent worker 完成,cancelToken 为跨线程取消标志,
    // completed/planned 用于进度文本。
    bool _exploreRunActive;
    bool _exploreCancellationRequested;
    QFutureWatcher< std::vector< WorkspaceSample > >* _exploreWatcher;
    std::shared_ptr< std::atomic_bool > _exploreCancelToken;
    std::size_t _exploreCompletedSamples;
    std::size_t _explorePlannedSamples;
    quint64 _workcellSessionGeneration;
    QWidget* _taskPointTab;             // Tab 2:任务点表格
    QWidget* _workspaceTab;             // Tab 3:工作空间采样
    QWidget* _poseReachTab;             // Tab 4:位姿可达性
    QWidget* _visualizationStateHost;    // Hidden state host for standalone plot dialog
    QWidget* _reportTab;                // Tab 6:报告

    QComboBox* _deviceCombo;                          // 顶部 device 选择
    QComboBox* _tcpFrameCombo;                        // 顶部 TCP frame 选择
    QString _projectDefaultDeviceName;
    QString _projectDefaultTcpFrameName;
    bool _projectImportBindingAvailable;
    QPushButton* _setProjectDefaultTcpButton;
    // _thresholdSettingsButton:打开阈值设置对话框(以事务方式编辑全部分析阈值)。
    QPushButton* _thresholdSettingsButton;
    // _reportButton:报告动作下拉菜单(Refresh / Export JSON / 导出摘要 CSV / 任务结果 CSV)。
    QPushButton* _reportButton;
    QLineEdit* _status;                               // 状态消息(只读)
    // ---- Diagnose 健康摘要与高级诊断 ----
    // _ikHealth*Label:Health summary 的四个紧凑指标(Status / Condition /
    // Manipulability / Min joint margin),由选中候选驱动刷新;
    // _ikJointStatusTable:逐关节的 q / 裕度 / 状态表,收纳在 Advanced diagnostics 折叠区;
    // _advancedDiagnosticsToggle / _advancedDiagnosticsContent:展开 / 收起诊断区;
    // _ikJacobianSummaryTable:雅可比汇总(维度 / sigma / 条件数 / 状态)。
    QLabel* _ikHealthStatusLabel;
    QLabel* _ikHealthConditionLabel;
    QLabel* _ikHealthManipulabilityLabel;
    QLabel* _ikHealthMarginLabel;
    QTableWidget* _ikJointStatusTable;
    QToolButton* _advancedDiagnosticsToggle;
    QWidget* _advancedDiagnosticsContent;
    QTableWidget* _ikJacobianSummaryTable;

    // Diagnose controls and candidate presentation.
    QDoubleSpinBox* _ikXSpin;                          // 目标 x (m 或显示单位)
    QDoubleSpinBox* _ikYSpin;                          // 目标 y
    QDoubleSpinBox* _ikZSpin;                          // 目标 z
    QDoubleSpinBox* _ikRollSpin;                       // 目标 roll (度)
    QDoubleSpinBox* _ikPitchSpin;                      // 目标 pitch
    QDoubleSpinBox* _ikYawSpin;                        // 目标 yaw
    QDoubleSpinBox* _ikDuplicateQThresholdSpin;        // IK 解去重 Q 阈值
    QCheckBox* _ikCollisionCheck;                      // IK 解是否启用碰撞检查
    QComboBox* _lengthUnitCombo;                       // 全局长度显示单位
    QComboBox* _angleUnitCombo;                        // 全局角度显示单位
    QPushButton* _ikSyncTcpButton;
    QPushButton* _ikSolveButton;                      // 触发 solveIk
    QPushButton* _ikApplyButton;                       // 把选中解写回 state
    // _ikInspectorTitleLabel:Solution inspector 标题,选中候选时在
    // “Best solution #N” 与 “Selected solution #N” 之间切换。
    QLabel* _ikInspectorTitleLabel;
    QLabel* _ikSourceLabel;                            // Task Point 跳转来源
    QLabel* _ikDisplayedLabel;                         // 当前显示的候选数量
    QComboBox* _ikCandidateFilterCombo;                // 候选显示筛选
    QTableWidget* _ikSolutionTable;                    // 候选解列表
    QTableWidget* _ikInspectorTable;                   // 选中解详情
    // _ikResultStale:IK 目标被修改后置 true,标记旧求解结果已过期,
    // 在用户重新 Solve 前禁止 Apply 防止写入过时位姿。
    bool _ikResultStale;
    // _applyingSelectedIkSolution:applySelectedIkSolution 写回 state 期间置 true,
    // 用于抑制 stateChangedListener 的副作用,避免把刚应用的解误判为目标变化而
    // 触发 invalidateIkResultPresentation,导致应用结果立即被标记为过期。
    bool _applyingSelectedIkSolution;
    // _bestIkSolutionIndex:最近一次求解选出的“最优解”索引(健康摘要 / 标题显示用)。
    // _selectedIkSolutionIndex:候选表中当前选中解的原始索引;无选中时为 -1,
    // 刷新候选表时据此恢复过滤后的选区。
    int _bestIkSolutionIndex;
    int _selectedIkSolutionIndex;

    // ===================================================================
    //  Task Points tab 控件
    // ===================================================================
    QTableView* _taskPointTable;                       // 任务点表格(view)
    rws::TaskPointTableModel* _taskPointModel;         // 任务点数据模型
    QPushButton* _addTaskPointButton;                  // 增加行
    QPushButton* _removeTaskPointButton;               // 删除选中行
    QPushButton* _importTaskPointsButton;              // 导入 CSV
    QPushButton* _importFrozenRequirementsButton;      // 导入经工程需求插件冻结的工位工件
    QPushButton* _exportTaskPointsButton;              // 导出 CSV
    QPushButton* _exportTaskPointResultsButton;        // 导出分析结果 CSV
    QPushButton* _analyzeAllTaskPointsButton;          // 全量分析
    QPushButton* _analyzeSelectedTaskPointsButton;     // 选中行分析
    QPushButton* _importCurrentTcpTaskPointButton;     // 把当前 TCP 当作任务点
    QPushButton* _applySelectedTaskPointBestQButton;   // 把选中任务点的 best Q 写回 state
    QPushButton* _openSelectedTaskPointInIkButton;     // 跳到 Diagnose 并填入位姿
    QLabel* _taskPointSummaryLabel;                     // 任务点聚合状态行
    QWidget* _taskPointSelectedPanel;

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
    QLabel* _workspaceSampleCountLabel;
    QLabel* _workspaceCollisionFreeLabel;
    QLabel* _workspacePassLabel;
    QLabel* _workspaceWarningLabel;
    QLabel* _workspaceFailLabel;
    QLabel* _workspaceAvgManipulabilityLabel;
    QLabel* _workspaceDiagnosticsLabel;                 // plan / theoretical / capped
    QTableWidget* _workspaceTable;                      // 样本表(最多 500 行)
    QTableWidget* _workspaceDetailTable;                // 选中样本详情
    QWidget* _workspaceDetailPanel;                     // Progressive disclosure panel
    bool _workspaceCollisionEvaluated;

    // ===================================================================
    //  Pose Reachability tab 控件
    // ===================================================================
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
    QToolButton* _poseTaskPointsSourceButton;
    QToolButton* _poseManualSourceButton;
    // 当前 Pose 数据源的即时说明。Task points 模式显示 Local Tasks 中已启用的
    // 行数，Manual 模式显示手工位置数；它只反映输入计划，不表示已完成 IK 计算。
    QLabel* _poseTaskPointSourceSummaryLabel;
    QPushButton* _poseRemoveRowButton;
    QLabel* _posePositionCountLabel;
    QLabel* _poseReachableLabel;
    QLabel* _poseCoverageLabel;
    QLabel* _posePassLabel;
    QLabel* _poseWarningLabel;
    QLabel* _poseFailLabel;
    QLabel* _poseDiagnosticsLabel;
    QLabel* _poseRunDetailsLabel;
    QWidget* _poseManualPositionsPanel;
    QToolButton* _poseMoreToggle;
    QProgressBar* _poseProgressBar;                    // 进度条(已自动缩放)
    QLabel* _poseProgressLabel;                        // 进度文本 (X / Y IK target)
    QTableWidget* _posePositionTable;                  // 手动位置输入
    QTableWidget* _poseResultTable;                    // 结果(最多 500 行)

    // ---- 包络异步计算 ----
    QFutureWatcher< WorkspaceEnvelopeRunResult >* _envelopeWatcher;
    int _envelopeGeneration = 0;                       // 请求生成号(防过期)
    QTimer* _envelopeDebounceTimer;                    // 方向数防抖定时器
    bool _envelopeRunActive = false;
    std::shared_ptr< std::atomic_bool > _envelopeCancelRequested;
    bool _envelopeCacheValid = false;
    WorkspaceEnvelopeCacheKey _envelopeCacheKey;
    AnalysisEnvelopeData _envelopeCacheData;

    // ===================================================================
    //  Visualization tab 控件
    // ===================================================================
    QComboBox* _visualSourceCombo;                     // 数据源(Task / Workspace / Pose)
    QComboBox* _visualProjectionCombo;                 // 投影平面(XY / XZ / YZ)
    QComboBox* _visualColorModeCombo;                  // 标量模式(由 updateVisualizationControls 动态填充)
    QComboBox* _visualRenderModeCombo;                 // 渲染模式(Scatter / Envelope)
    QSpinBox* _visualEnvelopeDirectionsSpin;           // 包络方向数(角度采样密度)
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
    QPushButton* _visualOpenDialogButton;               // 打开独立 plot 窗口
    QLabel* _visualSummaryLabel;
    KinematicAnalysisPlotWidget* _visualPlot;
    // _plotDialog:无模式独立 plot 窗口(QPointer 防止窗口关闭后悬挂指针)。
    QPointer< KinematicPlotDialog > _plotDialog;
    // _visualData:最近一次 refreshVisualization 生成的可视化数据快照,
    //              供独立 plot 窗口与内嵌 plot 共享同一份结果。
    AnalysisVisualData _visualData;

    // ---- Report tab:汇总标签、过滤下拉、阈值 SpinBox 与导出/刷新按钮 ----
    // 过滤组合(Stage / Feasibility / Quality / Failure / Region)只影响视图与导出,
    // 不改变底层分析结果;阈值改动通过 applyThresholds 写回 _thresholds 供后续分析使用。
    // _reportSummaryLabel:报告汇总文本
    QLabel* _reportSummaryLabel;
    // _reportWarningTable:告警表(Severity/Code/Source/Message)
    QTableWidget* _reportWarningTable;
    // _reportRefreshButton:手动刷新汇总
    QPushButton* _reportRefreshButton;
    // _reportExportJsonButton:导出 JSON 报告
    QPushButton* _reportExportJsonButton;
    // _reportExportCsvButton:导出 CSV 报告
    QPushButton* _reportExportCsvButton;
    // _reportStageFilterCombo:证据阶段过滤(Estimated/Quick/Verified)
    QComboBox* _reportStageFilterCombo;
    // _reportFeasibilityFilterCombo:可行性过滤
    QComboBox* _reportFeasibilityFilterCombo;
    // _reportQualityFilterCombo:质量过滤
    QComboBox* _reportQualityFilterCombo;
    // _reportFailureFilterCombo:失败原因过滤
    QComboBox* _reportFailureFilterCombo;
    // _reportRegionFilterEdit:区域 ID 文本过滤(可选)
    QLineEdit* _reportRegionFilterEdit;
    // _thresholdNearLimitSpin:接近关节极限比例阈值
    QDoubleSpinBox* _thresholdNearLimitSpin;
    // _thresholdConditionWarningSpin:条件数告警阈值
    QDoubleSpinBox* _thresholdConditionWarningSpin;
    // _thresholdConditionFailSpin:条件数失败阈值
    QDoubleSpinBox* _thresholdConditionFailSpin;
    // _thresholdSingularValueSpin:奇异值告警阈值
    QDoubleSpinBox* _thresholdSingularValueSpin;
    // _thresholdManipulabilitySpin:可操作度告警阈值
    QDoubleSpinBox* _thresholdManipulabilitySpin;
    // _thresholdPositionToleranceSpin:位置容差(显示单位)
    QDoubleSpinBox* _thresholdPositionToleranceSpin;
    // _thresholdOrientationToleranceSpin:姿态容差(显示单位)
    QDoubleSpinBox* _thresholdOrientationToleranceSpin;
    // _thresholdApplyButton:应用阈值的按钮
    QPushButton* _thresholdApplyButton;

    // _thresholds:当前生效的阈值集合(可由用户改 Report tab)。
    // _lengthUnit / _angleUnit:插件全局的显示单位(用户切换)。
    // _lastCurrentPose is retained for reports and TCP synchronization only.
    // _lastIkResult:最近一次 IK 求解结果(缓存,避免重复求解)。
    // _lastTaskPointResults:任务点分析结果(供 Report / 取消重算复用)。
    // _workspaceSamples:工作空间完整结果(供 Visualization 用)。
    // _poseReachabilitySamples:位姿可达性完整结果(供 Visualization 用)。
    KinematicThresholds _thresholds;
    KinematicLengthUnit _lengthUnit;
    KinematicAngleUnit _angleUnit;
    KinematicCurrentPoseResult _lastCurrentPose;
    KinematicIkAnalysisResult _lastIkResult;
    std::vector< TaskPointReachabilityResult > _lastTaskPointResults;
    std::vector< WorkspaceSample > _workspaceSamples;
    std::vector< PoseReachabilitySample > _poseReachabilitySamples;

    // 冻结需求校验相关:执行契约(任务与区域)、最近一次校验汇总,以及两个布尔
    // 标志分别表示"契约是否已加载"与"是否已产出结果"(后者决定 Export 按钮可用性)。
    RequirementExecutionSet _validateExecution;
    RequirementValidationSummary _validateSummary;
    QFutureWatcher< RequirementValidationRunResult >* _validateWatcher;
    std::shared_ptr< std::atomic_bool > _validateCancelRequested;
    bool _validateRunActive;
    bool _validateExecutionSet;
    bool _validateHasResults;

    // 项目路径和两个快照仅用于当前会话的脏比较，绝不序列化到 KinematicAnalysis JSON。
    QString _projectDocumentPath;
    // _savedProjectDocumentSnapshot:保存 / 确认时的基线快照
    QByteArray _savedProjectDocumentSnapshot;
    // _pendingProjectDocumentSnapshot:暂存到目标路径但尚未确认的快照
    QByteArray _pendingProjectDocumentSnapshot;
    // Project resources load before the WorkCell. Retain the validated document
    // until device and TCP controls can resolve their named bindings.
    QByteArray _deferredProjectDocumentSnapshot;
    // 加载项目文档期间为 true,抑制所有控件的 projectDocumentChanged 信号,
    // 避免"恢复配置"被误判为用户编辑而标记脏。
    bool _applyingProjectDocument = false;
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
