#include "KinematicAnalysisWidget.hpp"

#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalyzer.hpp"

// 共享的 CSV / JSON 序列化工具,TaskPoint 与本插件复用了它。
#include <rwslibs/robotanalysiscore/RobotAnalysisCsv.hpp>

#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/RPY.hpp>
#include <rws/RobWorkStudio.hpp>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <vector>

using namespace rws;

namespace {
QTableWidgetItem* makeItem (const QString& text);
QTableWidgetItem* makeItem (double v);
const char* statusText (rws::AnalysisStatus status);
QString qVectorText (const std::vector< double >& q);
QString failureReasonsText (const std::vector< rws::KinematicFailureReason >& reasons);
QString ikFailureText (const rws::KinematicIkSolution& solution);
bool isCurrentIkSolution (const rws::KinematicIkSolution& solution);
bool isUsableIkSolution (const rws::KinematicIkSolution& solution);
QTableWidgetItem* makeQItem (const std::vector< double >& q,
                             const std::vector< rws::KinematicFailureReason >& reasons);
void storeIkSolutionIndex (QTableWidgetItem* item, int solutionIndex);
void setDetailRow (QTableWidget* table, int row, const QString& field, const QString& value);
void setCompactTableVisibleRows (QTableWidget* table, int rows);
}    // namespace

// 构造函数:
//   - 把所有成员指针先置 NULL(防御性初始化);
//   - 用 QVBoxLayout + QScrollArea 包裹主内容区,
//     这样插件可以在小窗口下保持可用(滚动条出现);
//   - 顶部一行是设备 / TCP 帧选择 + Refresh 按钮;
//   - QTabWidget 承载 6 个子页;
//   - 底部一个只读状态条用于反馈用户操作结果;
//   - 末尾把所有按钮的 clicked() 信号连到对应的槽函数。
KinematicAnalysisWidget::KinematicAnalysisWidget(QWidget* parent) :
    QWidget(parent),
    _studio(NULL),
    _workcell(NULL),
    _tabs(NULL),
    _currentPoseTab(NULL),
    _ikTab(NULL),
    _taskPointTab(NULL),
    _workspaceTab(NULL),
    _poseReachTab(NULL),
    _reportTab(NULL),
    _deviceCombo(NULL),
    _tcpFrameCombo(NULL),
    _refreshCurrentPoseButton(NULL),
    _status(NULL),
    _poseValueTable(NULL),
    _poseIndicatorLabel(NULL),
    _jointStatusTable(NULL),
    _jacobianTable(NULL),
    _singularTable(NULL),
    _warningLabel(NULL),
    _ikTargetNameEdit(NULL),
    _ikXSpin(NULL),
    _ikYSpin(NULL),
    _ikZSpin(NULL),
    _ikRollSpin(NULL),
    _ikPitchSpin(NULL),
    _ikYawSpin(NULL),
    _ikDuplicateQThresholdSpin(NULL),
    _ikDistanceUnitCombo(NULL),
    _ikAngleUnitCombo(NULL),
    _ikImportCurrentPoseButton(NULL),
    _ikSolveButton(NULL),
    _ikApplyButton(NULL),
    _ikSummaryLabel(NULL),
    _ikSeedInfoLabel(NULL),
    _ikCountSummaryLabel(NULL),
    _ikShowUsableOnlyCheck(NULL),
    _ikShowFailedCandidatesCheck(NULL),
    _ikSolutionTable(NULL),
    _ikDetailTable(NULL),
    _taskPointTable(NULL),
    _addTaskPointButton(NULL),
    _removeTaskPointButton(NULL),
    _importTaskPointsButton(NULL),
    _exportTaskPointsButton(NULL),
    _analyzeAllTaskPointsButton(NULL),
    _taskPointSummaryLabel(NULL),
    _workspaceSampleCountSpin(NULL),
    _workspaceGridStepsSpin(NULL),
    _workspaceModeCombo(NULL),
    _workspaceCollisionCheck(NULL),
    _workspaceColorModeCombo(NULL),
    _workspaceRunButton(NULL),
    _workspaceExportButton(NULL),
    _workspaceSummaryLabel(NULL),
    _workspaceTable(NULL),
    _poseSourceCombo(NULL),
    _poseDirectionSamplesSpin(NULL),
    _poseRollSamplesSpin(NULL),
    _poseCollisionCheck(NULL),
    _poseAddRowButton(NULL),
    _poseAnalyzeButton(NULL),
    _poseExportButton(NULL),
    _poseSummaryLabel(NULL),
    _posePositionTable(NULL),
    _poseResultTable(NULL),
    _reportSummaryLabel(NULL),
    _reportWarningTable(NULL),
    _reportRefreshButton(NULL),
    _reportExportJsonButton(NULL),
    _reportExportCsvButton(NULL),
    _thresholdNearLimitSpin(NULL),
    _thresholdConditionWarningSpin(NULL),
    _thresholdConditionFailSpin(NULL),
    _thresholdSingularValueSpin(NULL),
    _thresholdManipulabilitySpin(NULL),
    _thresholdPositionToleranceSpin(NULL),
    _thresholdOrientationToleranceSpin(NULL),
    _thresholdApplyButton(NULL),
    _thresholds(),
    _ikLengthUnit(KinematicLengthUnit::Meters),
    _ikAngleUnit(KinematicAngleUnit::Degrees),
    _lastCurrentPose(),
    _lastIkResult(),
    _lastTaskPointResults(),
    _workspaceSamples(),
    _poseReachabilitySamples()
{
    Q_UNUSED (parent);
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins (4, 4, 4, 4);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll);

    QWidget* base = new QWidget(scroll);
    QVBoxLayout* content = new QVBoxLayout(base);
    content->setContentsMargins(4, 4, 4, 4);
    content->setSpacing(6);
    scroll->setWidget(base);

    QGridLayout* header = new QGridLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setColumnStretch(1, 1);
    _deviceCombo = new QComboBox(base);
    _tcpFrameCombo = new QComboBox(base);
    _refreshCurrentPoseButton = new QPushButton(tr("Refresh"), base);
    _deviceCombo->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _tcpFrameCombo->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _deviceCombo->setMinimumContentsLength (10);
    _tcpFrameCombo->setMinimumContentsLength (10);
    header->addWidget(new QLabel(tr("Device:"), base), 0, 0);
    header->addWidget(_deviceCombo, 0, 1, 1, 2);
    header->addWidget(new QLabel(tr("TCP frame:"), base), 1, 0);
    header->addWidget(_tcpFrameCombo, 1, 1);
    header->addWidget(_refreshCurrentPoseButton, 1, 2);
    content->addLayout(header);

    _tabs = new QTabWidget(base);
    content->addWidget(_tabs);

    // -------------------- Current Pose Tab --------------------
    // 单列全宽密集布局(从上到下):
    //   1. 紧凑摘要  — 2 行 6 列的位置/姿态 + 1 行关键指标;
    //   2. 关节状态合并表 — Joint | q | Limit margin | Status;
    //   3. Jacobian 全宽主表(行 vx/vy/vz/wx/wy/wz,列 q0..qn);
    //   4. Singular values 横向小表(1 行);
    //   5. Warnings 默认压成单行 \"Warnings: None\"。
    _currentPoseTab = new QWidget(_tabs);
    QVBoxLayout* cpLayout = new QVBoxLayout(_currentPoseTab);

    // 共用的紧凑表格工厂:6 列内 stretch、隐藏垂直滚动条、
    // 取消垂直 header(行名通过 setVerticalHeaderLabels 自定义)。
    auto makeCompactTable = [this] (int columns, int rows) -> QTableWidget* {
        QTableWidget* t = new QTableWidget(rows, columns, _currentPoseTab);
        t->horizontalHeader()->setSectionResizeMode (QHeaderView::Stretch);
        t->verticalHeader()->setVisible (false);
        t->setAlternatingRowColors (true);
        t->setEditTriggers (QAbstractItemView::NoEditTriggers);
        t->setSelectionBehavior (QAbstractItemView::SelectRows);
        t->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
        t->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
        t->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
        return t;
    };

    auto makeTable = [this] () -> QTableWidget* {
        QTableWidget* t = new QTableWidget(this);
        t->horizontalHeader ()->setSectionResizeMode (QHeaderView::Interactive);
        t->verticalHeader ()->setVisible (false);
        t->setAlternatingRowColors (true);
        t->setEditTriggers (QAbstractItemView::NoEditTriggers);
        t->setSelectionBehavior (QAbstractItemView::SelectRows);
        t->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
        t->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
        return t;
    };

    // ---- 1. 紧凑摘要栏(只占表头 + 1 行值,固定高度) ----
    cpLayout->addWidget (new QLabel(tr("TCP pose"), _currentPoseTab));
    _poseValueTable = makeCompactTable (6, 1);
    _poseValueTable->setHorizontalHeaderLabels (
        {tr("pos x (m)"), tr("pos y (m)"), tr("pos z (m)"),
         tr("roll (deg)"), tr("pitch (deg)"), tr("yaw (deg)")});
    // 1 行内容,初始占位
    for (int c = 0; c < 6; ++c)
        _poseValueTable->setItem (0, c, makeItem (QStringLiteral ("-")));
    cpLayout->addWidget (_poseValueTable);
    setCompactTableVisibleRows (_poseValueTable, 1);
    _poseValueTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);

    // 关键指标单行标签:Condition / Manipulability / Min limit margin
    _poseIndicatorLabel = new QLabel (
        tr("Condition: -    Manipulability: -    Min limit margin: -"),
        _currentPoseTab);
    cpLayout->addWidget (_poseIndicatorLabel);

    // ---- 2. 关节状态合并表(全列 stretch,固定高度) ----
    cpLayout->addWidget (new QLabel(tr("Joint status"), _currentPoseTab));
    _jointStatusTable = makeCompactTable (4, 0);
    _jointStatusTable->setHorizontalHeaderLabels (
        {tr("Joint"), tr("q"), tr("Limit margin"), tr("Status")});
    // 4 列内容不多,横向 stretch 完全能铺满,关闭水平滚动;
    // 垂直方向根据实际行数动态设高(详见 refreshCurrentPose)。
    _jointStatusTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    cpLayout->addWidget (_jointStatusTable);

    // ---- 3. Jacobian 全宽主表 ----
    cpLayout->addWidget (new QLabel(tr("Jacobian (TCP velocity = J * joint velocity)"), _currentPoseTab));
    _jacobianTable = makeCompactTable (1, 1);
    _jacobianTable->verticalHeader()->setVisible (true);
    _jacobianTable->setVerticalHeaderLabels ({tr("vx"), tr("vy"), tr("vz"),
                                              tr("wx"), tr("wy"), tr("wz")});
    // 数字统一 %.6g 精度,留出空间;
    // refreshCurrentPose 阶段再用 makeItem(double) 写入。
    cpLayout->addWidget (_jacobianTable);

    // ---- 4. Singular values:表头 + 1 行值(固定高度) ----
    cpLayout->addWidget (new QLabel(tr("Singular values"), _currentPoseTab));
    // 列数会在 refreshCurrentPose 中按 σ 个数动态设定,所以先建 0 列 1 行;
    // 行数固定为 1,index 已在 horizontal header(σ0 / σ1 / ... / σmin),
    // 不再需要单独 index 行。
    _singularTable = makeCompactTable (0, 1);
    cpLayout->addWidget (_singularTable);
    setCompactTableVisibleRows (_singularTable, 1);
    _singularTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);

    // ---- 5. Warnings:默认压成一行 ----
    _warningLabel = new QLabel (tr("Warnings: None"), _currentPoseTab);
    _warningLabel->setWordWrap (true);
    cpLayout->addWidget (_warningLabel);

    cpLayout->addStretch ();

    _tabs->addTab (_currentPoseTab, tr("Current pose"));

    // -------------------- IK Tab --------------------
    // 单列全宽密集布局(与 Current pose 一致):
    //   1. 顶部输入:Target + 单位 + 6 个 pose spin + threshold + 3 个动作按钮;
    //   2. 过滤 + solver 元信息 + 计数 summary;
    //   3. status summary 标签;
    //   4. IK solution status table(允许滚动,横向铺满);
    //   5. 详情面板(2 行固定高度,跟随选中行更新)。
    _ikTab         = new QWidget(_tabs);
    QVBoxLayout* ikLayout = new QVBoxLayout(_ikTab);

    // ---- 第 1 行:Target + 单位选择 ----
    QHBoxLayout* ikNameRow = new QHBoxLayout();
    _ikTargetNameEdit = new QLineEdit(_ikTab);
    _ikTargetNameEdit->setText(tr("Target"));
    _ikDistanceUnitCombo = new QComboBox(_ikTab);
    _ikAngleUnitCombo = new QComboBox(_ikTab);
    _ikDistanceUnitCombo->addItem(tr("Meters"), static_cast<int>(KinematicLengthUnit::Meters));
    _ikDistanceUnitCombo->addItem(tr("Centimeters"), static_cast<int>(KinematicLengthUnit::Centimeters));
    _ikDistanceUnitCombo->addItem(tr("Millimeters"), static_cast<int>(KinematicLengthUnit::Millimeters));
    _ikDistanceUnitCombo->addItem(tr("Inches"), static_cast<int>(KinematicLengthUnit::Inches));
    _ikAngleUnitCombo->addItem(tr("Degrees"), static_cast<int>(KinematicAngleUnit::Degrees));
    _ikAngleUnitCombo->addItem(tr("Radians"), static_cast<int>(KinematicAngleUnit::Radians));
    _ikAngleUnitCombo->addItem(tr("Grads"), static_cast<int>(KinematicAngleUnit::Grads));
    _ikAngleUnitCombo->addItem(tr("Turns"), static_cast<int>(KinematicAngleUnit::Turns));
    ikNameRow->addWidget(new QLabel(tr("Target:"), _ikTab));
    ikNameRow->addWidget(_ikTargetNameEdit, 1);
    ikNameRow->addSpacing (12);
    ikNameRow->addWidget(new QLabel(tr("Distance unit:"), _ikTab));
    ikNameRow->addWidget(_ikDistanceUnitCombo);
    ikNameRow->addSpacing (12);
    ikNameRow->addWidget(new QLabel(tr("Angle unit:"), _ikTab));
    ikNameRow->addWidget(_ikAngleUnitCombo);
    ikLayout->addLayout(ikNameRow);

    // ---- 第 2 行:位姿 6 个 spin,2 行 × 3 列 ----
    auto makePoseSpin = [this] (double minimum, double maximum, double step) -> QDoubleSpinBox* {
        QDoubleSpinBox* spin = new QDoubleSpinBox(_ikTab);
        spin->setRange(minimum, maximum);
        spin->setDecimals(6);
        spin->setSingleStep(step);
        return spin;
    };
    _ikXSpin = makePoseSpin(-1000.0, 1000.0, 0.01);
    _ikYSpin = makePoseSpin(-1000.0, 1000.0, 0.01);
    _ikZSpin = makePoseSpin(-1000.0, 1000.0, 0.01);
    _ikRollSpin = makePoseSpin(-360.0, 360.0, 1.0);
    _ikPitchSpin = makePoseSpin(-360.0, 360.0, 1.0);
    _ikYawSpin = makePoseSpin(-360.0, 360.0, 1.0);
    _ikDuplicateQThresholdSpin = new QDoubleSpinBox(_ikTab);
    _ikDuplicateQThresholdSpin->setRange(0.0, 1.0);
    _ikDuplicateQThresholdSpin->setDecimals(6);
    _ikDuplicateQThresholdSpin->setSingleStep(0.001);
    _ikDuplicateQThresholdSpin->setValue(_thresholds.ikDuplicateQThreshold);
    updateIkUnitDisplay();

    QGridLayout* ikPoseGrid = new QGridLayout();
    ikPoseGrid->addWidget(new QLabel(tr("X:"), _ikTab), 0, 0);
    ikPoseGrid->addWidget(_ikXSpin, 0, 1);
    ikPoseGrid->addWidget(new QLabel(tr("Y:"), _ikTab), 0, 2);
    ikPoseGrid->addWidget(_ikYSpin, 0, 3);
    ikPoseGrid->addWidget(new QLabel(tr("Z:"), _ikTab), 0, 4);
    ikPoseGrid->addWidget(_ikZSpin, 0, 5);
    ikPoseGrid->addWidget(new QLabel(tr("Roll:"), _ikTab), 1, 0);
    ikPoseGrid->addWidget(_ikRollSpin, 1, 1);
    ikPoseGrid->addWidget(new QLabel(tr("Pitch:"), _ikTab), 1, 2);
    ikPoseGrid->addWidget(_ikPitchSpin, 1, 3);
    ikPoseGrid->addWidget(new QLabel(tr("Yaw:"), _ikTab), 1, 4);
    ikPoseGrid->addWidget(_ikYawSpin, 1, 5);
    ikLayout->addLayout(ikPoseGrid);

    // ---- 第 3 行:threshold + 3 个动作按钮(横排)----
    QHBoxLayout* ikDedupRow = new QHBoxLayout();
    ikDedupRow->addWidget(new QLabel(tr("Duplicate Q threshold:"), _ikTab));
    ikDedupRow->addWidget(_ikDuplicateQThresholdSpin);
    _ikImportCurrentPoseButton = new QPushButton(tr("Import current TCP pose"), _ikTab);
    _ikSolveButton = new QPushButton(tr("Solve"), _ikTab);
    _ikApplyButton = new QPushButton(tr("Apply selected Q"), _ikTab);
    _ikApplyButton->setEnabled (false);   // 选中可用解前禁用
    ikDedupRow->addSpacing (12);
    ikDedupRow->addWidget(_ikImportCurrentPoseButton);
    ikDedupRow->addWidget(_ikSolveButton);
    ikDedupRow->addWidget(_ikApplyButton);
    ikDedupRow->addStretch (1);
    ikLayout->addLayout(ikDedupRow);

    // ---- 第 4 行:过滤器 + solver 元信息 ----
    QHBoxLayout* ikFilterRow = new QHBoxLayout();
    _ikShowUsableOnlyCheck = new QCheckBox(tr("Show usable only"), _ikTab);
    _ikShowFailedCandidatesCheck = new QCheckBox(tr("Show failed candidates"), _ikTab);
    _ikShowFailedCandidatesCheck->setChecked(false);
    _ikSeedInfoLabel = new QLabel(tr("Solver: deterministic multi-seed"), _ikTab);
    ikFilterRow->addWidget(_ikShowUsableOnlyCheck);
    ikFilterRow->addWidget(_ikShowFailedCandidatesCheck);
    ikFilterRow->addStretch(1);
    ikFilterRow->addWidget(_ikSeedInfoLabel);
    ikLayout->addLayout(ikFilterRow);

    // ---- 第 5 行:counts summary ----
    _ikCountSummaryLabel = new QLabel(
        tr("Raw - | Unique - | Usable - | Pass - | Warning - | Fail -"), _ikTab);
    ikLayout->addWidget(_ikCountSummaryLabel);

    // ---- 第 6 行:status summary 标签 ----
    _ikSummaryLabel = new QLabel(tr("Candidates: -    Usable unique: -"), _ikTab);
    ikLayout->addWidget(_ikSummaryLabel);

    // ---- 第 7 行:IK solution status table(允许横纵滚动)----
    ikLayout->addWidget(new QLabel(tr("IK solution status"), _ikTab));
    _ikSolutionTable = makeTable();
    // 把 "Q / failures" 拆成两列 — Failure(短文本) + Q(关节向量),
    // 长 Q 值不再吞掉失败原因。
    _ikSolutionTable->setColumnCount(12);
    _ikSolutionTable->setHorizontalHeaderLabels({
        tr("Index"), tr("Status"), tr("Failure"), tr("Current Q"), tr("Collision"), tr("Distance"),
        tr("Min limit margin"), tr("Manipulability"), tr("Condition"),
        tr("Position error"), tr("Orientation error"), tr("Q")
    });
    _ikSolutionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _ikSolutionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _ikSolutionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    _ikSolutionTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _ikSolutionTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // 选中行变化 → 详情表更新。
    connect (_ikSolutionTable, SIGNAL (itemSelectionChanged ()),
             this, SLOT (updateIkSolutionDetails ()));
    // 该表是页面唯一允许滚动的主表,占主导高度。
    ikLayout->addWidget(_ikSolutionTable, 1);

    // ---- 第 8 行:选中详情(2 行固定高度)----
    ikLayout->addWidget(new QLabel(tr("Selected candidate details"), _ikTab));
    _ikDetailTable = makeTable();
    _ikDetailTable->setColumnCount(2);
    _ikDetailTable->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
    _ikDetailTable->horizontalHeader()->setSectionResizeMode (QHeaderView::Stretch);
    _ikDetailTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    ikLayout->addWidget(_ikDetailTable);
    setCompactTableVisibleRows (_ikDetailTable, 2);
    _ikDetailTable->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);

    // -------------------- Task Point Tab --------------------
    _taskPointTab  = new QWidget(_tabs);
    _workspaceTab  = new QWidget(_tabs);
    _poseReachTab  = new QWidget(_tabs);
    _reportTab     = new QWidget(_tabs);

    buildTaskPointTab ();
    buildWorkspaceTab ();
    buildPoseReachabilityTab ();
    buildReportTab ();

    _tabs->addTab (_ikTab,         tr("IK"));
    _tabs->addTab (_taskPointTab,  tr("Task points"));
    _tabs->addTab (_workspaceTab,  tr("Workspace"));
    _tabs->addTab (_poseReachTab,  tr("Pose reachability"));
    _tabs->addTab (_reportTab,     tr("Report"));

    _status = new QLineEdit(base);
    _status->setReadOnly(true);
    content->addWidget(_status);

    connect (_refreshCurrentPoseButton, SIGNAL (clicked ()), this, SLOT (refreshCurrentPose ()));
    // Task 5 step 2:勾选过滤器时即时刷新 IK 结果表与统计。
    connect (_ikShowUsableOnlyCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshIkSolutionView ()));
    connect (_ikShowFailedCandidatesCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshIkSolutionView ()));
    connect (_ikImportCurrentPoseButton, SIGNAL (clicked ()), this, SLOT (importCurrentPoseToIk ()));
    connect (_ikDistanceUnitCombo, SIGNAL (currentIndexChanged (int)), this, SLOT (updateIkUnitDisplay ()));
    connect (_ikAngleUnitCombo, SIGNAL (currentIndexChanged (int)), this, SLOT (updateIkUnitDisplay ()));
    connect (_ikSolveButton, SIGNAL (clicked ()), this, SLOT (solveIk ()));
    connect (_ikApplyButton, SIGNAL (clicked ()), this, SLOT (applySelectedIkSolution ()));
    connect (_addTaskPointButton, SIGNAL (clicked ()), this, SLOT (addTaskPointRow ()));
    connect (_removeTaskPointButton, SIGNAL (clicked ()), this, SLOT (removeSelectedTaskPointRow ()));
    connect (_importTaskPointsButton, SIGNAL (clicked ()), this, SLOT (importTaskPointsCsv ()));
    connect (_exportTaskPointsButton, SIGNAL (clicked ()), this, SLOT (exportTaskPointsCsv ()));
    connect (_analyzeAllTaskPointsButton, SIGNAL (clicked ()), this, SLOT (analyzeAllTaskPoints ()));
    connect (_workspaceRunButton, SIGNAL (clicked ()), this, SLOT (sampleWorkspace ()));
    connect (_workspaceExportButton, SIGNAL (clicked ()), this, SLOT (exportWorkspaceCsv ()));
    connect (_poseAddRowButton, SIGNAL (clicked ()), this, SLOT (addPoseReachabilityRow ()));
    connect (_poseAnalyzeButton, SIGNAL (clicked ()), this, SLOT (analyzePoseReachability ()));
    connect (_poseExportButton, SIGNAL (clicked ()), this, SLOT (exportPoseReachabilityCsv ()));
    connect (_reportRefreshButton, SIGNAL (clicked ()), this, SLOT (refreshReport ()));
    connect (_reportExportJsonButton, SIGNAL (clicked ()), this, SLOT (exportReportJson ()));
    connect (_reportExportCsvButton, SIGNAL (clicked ()), this, SLOT (exportReportCsv ()));
    connect (_thresholdApplyButton, SIGNAL (clicked ()), this, SLOT (applyThresholds ()));

    setStatus(tr("Load a WorkCell to start kinematic analysis."));
    // Task 4 step 5:无选中行时显示"No IK candidate selected."。
    setIkDetailsEmpty ();
}

QSize KinematicAnalysisWidget::sizeHint () const
{
    return QSize (360, 620);
}

QSize KinematicAnalysisWidget::minimumSizeHint () const
{
    return QSize (300, 420);
}

// setRobWorkStudio:由 KinematicAnalysisPlugin::initialize 调用,缓存主程序句柄;
// 用它获取当前 state、写回 IK 解。
void KinematicAnalysisWidget::setRobWorkStudio(RobWorkStudio* studio)
{
    _studio = studio;
}

// setWorkCell:WorkCell 变化时调用,刷新设备/帧下拉,并提示用户当前状态。
void KinematicAnalysisWidget::setWorkCell(rw::models::WorkCell* workcell)
{
    _workcell = workcell;
    populateDevices ();
    populateTcpFrames ();
    if (_workcell == NULL)
        setStatus(tr("No WorkCell loaded."));
    else if (_deviceCombo->count() == 0)
        setStatus(tr("No device found in WorkCell."));
    else
        setStatus(tr("WorkCell loaded. Select a device and refresh analysis."));
}

// populateDevices:把 WorkCell 中的 Device 全部填进 _deviceCombo。
void KinematicAnalysisWidget::populateDevices ()
{
    _deviceCombo->clear ();
    if (_workcell == NULL) {
        _deviceCombo->addItem (tr("(no WorkCell)"));
        return;
    }
    for (rw::core::Ptr< rw::models::Device > dev : _workcell->getDevices ()) {
        if (dev == NULL)
            continue;
        _deviceCombo->addItem (QString::fromStdString (dev->getName ()));
    }
    if (_deviceCombo->count () > 0)
        _deviceCombo->setCurrentIndex (0);
}

// populateTcpFrames:用 Kinematics::findAllFrames 收集 WorkCell 中所有帧,
// 提供给用户选作 TCP。这会把所有辅助 / 工具 / 末端帧都列出。
void KinematicAnalysisWidget::populateTcpFrames ()
{
    _tcpFrameCombo->clear ();
    if (_workcell == NULL) {
        _tcpFrameCombo->addItem (tr("(no WorkCell)"));
        return;
    }
    rw::kinematics::State workcellState = _workcell->getDefaultState ();
    std::vector< rw::kinematics::Frame* > frames =
        rw::kinematics::Kinematics::findAllFrames (_workcell->getWorldFrame (), workcellState);
    for (const rw::kinematics::Frame* frame : frames) {
        if (frame == NULL)
            continue;
        _tcpFrameCombo->addItem (QString::fromStdString (frame->getName ()));
    }
    if (_tcpFrameCombo->count () > 0)
        _tcpFrameCombo->setCurrentIndex (0);
}

// currentState:优先返回 RobWorkStudio 的当前 state;否则用 WorkCell 默认 state;
// 都不可用时返回一个空 State(供分析器做空指针 / 空状态分支)。
rw::kinematics::State KinematicAnalysisWidget::currentState () const
{
    if (_studio != NULL)
        return _studio->getState ();
    if (_workcell != NULL)
        return _workcell->getDefaultState ();
    return rw::kinematics::State ();
}

// setStatus:状态栏的简单 setter,NULL 检查避免析构期崩溃。
void KinematicAnalysisWidget::setStatus (const QString& message)
{
    if (_status != NULL)
        _status->setText(message);
}

// refreshIkSolutionView:把 _lastIkResult 写入 _ikSolutionTable,
// 按过滤器过滤,每行的原始 solutionIndex 通过 storeIkSolutionIndex
// 存到 Qt::UserRole + 1。末尾刷新顶部计数 summary + 详情表。
void KinematicAnalysisWidget::refreshIkSolutionView ()
{
    if (_ikSolutionTable == NULL)
        return;

    _ikSolutionTable->setRowCount (0);
    int displayRow = 0;
    for (std::size_t i = 0; i < _lastIkResult.solutions.size (); ++i) {
        const KinematicIkSolution& solution = _lastIkResult.solutions[i];
        if (!shouldShowIkSolution (solution))
            continue;

        _ikSolutionTable->insertRow (displayRow);
        QTableWidgetItem* indexItem = makeItem (QString::number (static_cast<int> (i)));
        storeIkSolutionIndex (indexItem, static_cast<int> (i));
        _ikSolutionTable->setItem (displayRow, 0, indexItem);
        _ikSolutionTable->setItem (displayRow, 1, makeItem (QString::fromLatin1 (statusText (solution.status))));
        _ikSolutionTable->setItem (displayRow, 2, makeItem (ikFailureText (solution)));
        _ikSolutionTable->setItem (displayRow, 3, makeItem (isCurrentIkSolution (solution) ? tr("Yes") : tr("No")));
        _ikSolutionTable->setItem (displayRow, 4, makeItem (solution.inCollision ? tr("Yes") : tr("No")));
        _ikSolutionTable->setItem (displayRow, 5, makeItem (solution.distanceToCurrentQ));
        _ikSolutionTable->setItem (displayRow, 6, makeItem (solution.minJointLimitMargin));
        _ikSolutionTable->setItem (displayRow, 7, makeItem (solution.manipulability));
        _ikSolutionTable->setItem (displayRow, 8,
            makeItem (std::isinf (solution.conditionNumber) ? tr("inf")
                                                            : QString::number (solution.conditionNumber)));
        _ikSolutionTable->setItem (displayRow, 9, makeItem (solution.positionErrorMeters));
        _ikSolutionTable->setItem (displayRow, 10, makeItem (solution.orientationErrorDeg));
        // 第 10 列是纯 Q,失败原因已分到第 2 列;这里传空 reasons 防止 makeQItem
        // 把原因字符串重复显示一次。
        _ikSolutionTable->setItem (displayRow, 11,
            makeQItem (solution.q, std::vector< KinematicFailureReason > ()));

        // 整行的 solutionIndex 都存到 Qt::UserRole + 1,这样选中任一单元格
        // 都能反查回原始 solution。
        for (int column = 1; column < _ikSolutionTable->columnCount (); ++column)
            storeIkSolutionIndex (_ikSolutionTable->item (displayRow, column),
                                  static_cast<int> (i));

        ++displayRow;
    }

    if (_ikCountSummaryLabel != NULL) {
        const KinematicIkSummary summary = summarizeIkSolutions (_lastIkResult.solutions);
        _ikCountSummaryLabel->setText (
            tr("Raw %1 | Unique %2 | Usable %3 | Pass %4 | Warning %5 | Fail %6")
                .arg (static_cast<int> (_lastIkResult.rawCandidateCount))
                .arg (static_cast<int> (_lastIkResult.solutions.size ()))
                .arg (static_cast<int> (summary.usableCount))
                .arg (static_cast<int> (summary.passCount))
                .arg (static_cast<int> (summary.warningCount))
                .arg (static_cast<int> (summary.failCount)));
    }

    // 首次填表后默认选中第一行,详情表即时显示该候选;
    // 过滤后无行时退回到空状态(Apply 也会被 setIkDetailsEmpty 禁用)。
    if (_ikSolutionTable->rowCount () > 0)
        _ikSolutionTable->selectRow (0);
    else
        setIkDetailsEmpty ();

    // 按钮启用/禁用统一交给 updateIkSolutionDetails / setIkDetailsEmpty。
    updateIkSolutionDetails ();
}

// updateIkSolutionDetails:把当前选中行反查 _lastIkResult.solutions,
// 写 2 行详情:Summary(状态类)+ Metrics / Q(数值类)。
// 任一缺失都退回 setIkDetailsEmpty。
void KinematicAnalysisWidget::updateIkSolutionDetails ()
{
    if (_ikDetailTable == NULL || _ikSolutionTable == NULL)
        return;

    const QList<QTableWidgetItem*> selected = _ikSolutionTable->selectedItems ();
    if (selected.empty ()) {
        setIkDetailsEmpty ();
        return;
    }

    const int solutionIndex = selected.front ()->data (Qt::UserRole + 1).toInt ();
    if (solutionIndex < 0 ||
        solutionIndex >= static_cast<int> (_lastIkResult.solutions.size ())) {
        setIkDetailsEmpty ();
        return;
    }

    const KinematicIkSolution& s =
        _lastIkResult.solutions[static_cast<std::size_t> (solutionIndex)];

    // 同步 Apply 按钮启用态:只有无碰撞、非 Fail 的解可写回 RobWorkStudio。
    if (_ikApplyButton != NULL)
        _ikApplyButton->setEnabled (isUsableIkSolution (s));

    // 第 1 行:状态类信息(标签 / 布尔)。
    const QString summaryText = QStringLiteral (
        "Status=%1; Failures=[%2]; Current Q=%3; Collision=%4")
        .arg (QString::fromLatin1 (statusText (s.status)))
        .arg (ikFailureText (s).isEmpty () ? QStringLiteral ("None")
                                            : ikFailureText (s))
        .arg (isCurrentIkSolution (s) ? QStringLiteral ("Yes") : QStringLiteral ("No"))
        .arg (s.inCollision ? QStringLiteral ("Yes") : QStringLiteral ("No"));

    // 第 2 行:数值类 + Q 向量。
    const QString condText = std::isinf (s.conditionNumber) ?
        QStringLiteral ("inf") : QString::number (s.conditionNumber, 'g', 6);
    const QString metricsText = QStringLiteral (
        "Distance=%1; Margin=%2; Manip=%3; Cond=%4; Pos err=%5 m; Ori err=%6°; Q=[%7]")
        .arg (QString::number (s.distanceToCurrentQ, 'g', 6))
        .arg (QString::number (s.minJointLimitMargin, 'g', 6))
        .arg (QString::number (s.manipulability, 'g', 6))
        .arg (condText)
        .arg (QString::number (s.positionErrorMeters, 'g', 6))
        .arg (QString::number (s.orientationErrorDeg, 'g', 6))
        .arg (qVectorText (s.q));

    _ikDetailTable->setRowCount (2);
    setDetailRow (_ikDetailTable, 0, tr("Summary"), summaryText);
    setDetailRow (_ikDetailTable, 1, tr("Metrics / Q"), metricsText);
    // 不调用 resizeColumnsToContents,避免在 Stretch 模式下被覆盖;
    // 同时保持 2 行固定高度由 setCompactTableVisibleRows 锁定。
}

// setIkDetailsEmpty:详情表压成 1 行提示,用于未选中或选中行无效。
void KinematicAnalysisWidget::setIkDetailsEmpty ()
{
    if (_ikDetailTable == NULL)
        return;
    _ikDetailTable->setRowCount (1);
    setDetailRow (_ikDetailTable, 0, tr("Selection"), tr("No IK candidate selected."));
    setCompactTableVisibleRows (_ikDetailTable, 2);
    _ikDetailTable->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    if (_ikApplyButton != NULL)
        _ikApplyButton->setEnabled (false);
}

namespace {
// configureAnalysisTable:把常用的表格属性集中在一起,避免在多处重复设置。
void configureAnalysisTable (QTableWidget* table)
{
    table->setSelectionBehavior (QAbstractItemView::SelectRows);
    table->setAlternatingRowColors (true);
    table->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    table->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
    table->horizontalHeader ()->setSectionResizeMode (QHeaderView::Interactive);
    table->verticalHeader ()->setVisible (false);
}

// setCompactTableVisibleRows:把 QTableWidget 的高度固定成"表头 + rows 行
// 内容 + 边框",并关闭垂直滚动条。
// 用途:让 1 行的摘要表 / N 行的关节表在 QVBoxLayout 里只占自己需要的高度,
// 不再被 layout 撑大留白;行数 > visible 区域时只能外部接管(本工具仍允许
// 后续单独开启滚动条)。
void setCompactTableVisibleRows (QTableWidget* table, int rows)
{
    if (table == NULL)
        return;
    const int height =
        table->horizontalHeader ()->height () +
        rows * table->verticalHeader ()->defaultSectionSize () +
        2 * table->frameWidth () + 4;
    table->setFixedHeight (height);
    table->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
}

// makeItem:构造只读单元格;重载 double 版本方便直接放数值。
QTableWidgetItem* makeItem (const QString& text)
{
    QTableWidgetItem* item = new QTableWidgetItem (text);
    item->setFlags (item->flags () & ~Qt::ItemIsEditable);
    return item;
}
QTableWidgetItem* makeItem (double v)
{
    return makeItem (QString::number (v));
}

// statusText:AnalysisStatus → 可读字符串,与 toString(KinematicFailureReason) 配套。
const char* statusText (rws::AnalysisStatus status)
{
    switch (status) {
        case rws::AnalysisStatus::Pass:    return "Pass";
        case rws::AnalysisStatus::Warning: return "Warning";
        case rws::AnalysisStatus::Fail:    return "Fail";
        case rws::AnalysisStatus::Unknown:
        default:                           return "Unknown";
    }
}

// qVectorText:把关节向量格式化为 "q0, q1, ..." 用于表格/CSV 显示。
QString qVectorText (const std::vector< double >& q)
{
    QStringList values;
    for (double value : q)
        values << QString::number(value, 'g', 8);
    return values.join(", ");
}

// failureReasonsText:把失败原因枚举数组格式化为 ", " 分隔字符串。
QString failureReasonsText (const std::vector< rws::KinematicFailureReason >& reasons)
{
    if (reasons.empty())
        return QString();

    QStringList values;
    for (rws::KinematicFailureReason reason : reasons)
        values << QString::fromLatin1(rws::toString(reason));
    return values.join(", ");
}

bool hasFailureReason (const std::vector< rws::KinematicFailureReason >& reasons,
                       rws::KinematicFailureReason reason)
{
    return std::find (reasons.begin (), reasons.end (), reason) != reasons.end ();
}

bool isCurrentIkSolution (const rws::KinematicIkSolution& solution)
{
    return std::isfinite (solution.distanceToCurrentQ) &&
           solution.distanceToCurrentQ <= 1e-9;
}

// isUsableIkSolution:判定该 IK 解是否可安全写回 RobWorkStudio,
// 复用 refreshIkSolutionView / updateIkSolutionDetails 中的判定,避免重复。
// 不可用情形:碰撞 / status == Fail。
bool isUsableIkSolution (const rws::KinematicIkSolution& solution)
{
    return !solution.inCollision && solution.status != rws::AnalysisStatus::Fail;
}

QString ikFailureText (const rws::KinematicIkSolution& solution)
{
    QString text = failureReasonsText (solution.failureReasons);
    QStringList details;
    if (hasFailureReason (solution.failureReasons, rws::KinematicFailureReason::Singular) ||
        hasFailureReason (solution.failureReasons, rws::KinematicFailureReason::NearSingular)) {
        details << (std::isinf (solution.conditionNumber) ?
                    QStringLiteral ("condition=inf") :
                    QStringLiteral ("condition=%1").arg (
                        QString::number (solution.conditionNumber, 'g', 8)));
        details << QStringLiteral ("manip=%1").arg (
            QString::number (solution.manipulability, 'g', 8));
    }
    if (hasFailureReason (solution.failureReasons, rws::KinematicFailureReason::JointLimit) ||
        hasFailureReason (solution.failureReasons, rws::KinematicFailureReason::NearJointLimit)) {
        details << QStringLiteral ("margin=%1").arg (
            QString::number (solution.minJointLimitMargin, 'g', 8));
    }
    if (hasFailureReason (solution.failureReasons, rws::KinematicFailureReason::TargetResidual)) {
        details << QStringLiteral ("pos=%1 m").arg (
            QString::number (solution.positionErrorMeters, 'g', 8));
        details << QStringLiteral ("ori=%1 deg").arg (
            QString::number (solution.orientationErrorDeg, 'g', 8));
    }
    if (!details.empty ()) {
        if (!text.isEmpty ())
            text += QStringLiteral (" (") + details.join (QStringLiteral (", ")) + QStringLiteral (")");
        else
            text = details.join (QStringLiteral (", "));
    }
    return text;
}

// taskPointTypeText:TaskPointType → 字符串,UI 显示与回写都用。
const char* taskPointTypeText (rws::TaskPointType type)
{
    switch (type) {
        case rws::TaskPointType::Pick:    return "Pick";
        case rws::TaskPointType::Place:   return "Place";
        case rws::TaskPointType::Weld:    return "Weld";
        case rws::TaskPointType::Glue:    return "Glue";
        case rws::TaskPointType::Inspect: return "Inspect";
        case rws::TaskPointType::Screw:   return "Screw";
        case rws::TaskPointType::Custom:  return "Custom";
        case rws::TaskPointType::Generic:
        default:                          return "Generic";
    }
}

// makeQItem:把 IK 解的 q + failureReasons 拼成一个单元格,
// 并把 q 序列化到 Qt::UserRole,Apply 时直接读取,避免再次解析字符串。
QTableWidgetItem* makeQItem (const std::vector< double >& q,
                             const std::vector< rws::KinematicFailureReason >& reasons)
{
    QString text = qVectorText(q);
    const QString failures = failureReasonsText(reasons);
    if (!failures.isEmpty())
        text += QString(" | ") + failures;

    QTableWidgetItem* item = makeItem(text);
    QVariantList storedQ;
    for (double value : q)
        storedQ << value;
    item->setData(Qt::UserRole, storedQ);
    return item;
}

// deviceByName / frameByName:按名称在 WorkCell 中查找;找不到返回 NULL。
rw::core::Ptr< rw::models::Device > deviceByName (
    rw::models::WorkCell* wc, const std::string& name)
{
    if (wc == NULL)
        return NULL;
    for (rw::core::Ptr< rw::models::Device > dev : wc->getDevices ()) {
        if (dev != NULL && dev->getName () == name)
            return dev;
    }
    return NULL;
}

rw::core::Ptr< rw::kinematics::Frame > frameByName (
    rw::models::WorkCell* wc, const std::string& name)
{
    if (wc == NULL)
        return NULL;
    rw::kinematics::State state = wc->getDefaultState ();
    const std::vector< rw::kinematics::Frame* > frames =
        rw::kinematics::Kinematics::findAllFrames (wc->getWorldFrame (), state);
    for (rw::kinematics::Frame* frame : frames) {
        if (frame != NULL && frame->getName () == name)
            return frame;
    }
    return NULL;
}
}    // namespace

// selectedDevice / selectedTcpFrame:把"下拉框当前选项"翻译成 RobWork 指针。
rw::core::Ptr< rw::models::Device > KinematicAnalysisWidget::selectedDevice () const
{
    if (_workcell == NULL || _deviceCombo == NULL)
        return NULL;
    return deviceByName (_workcell, _deviceCombo->currentText ().toStdString ());
}

rw::core::Ptr< rw::kinematics::Frame > KinematicAnalysisWidget::selectedTcpFrame () const
{
    if (_workcell == NULL || _tcpFrameCombo == NULL)
        return NULL;
    return frameByName (_workcell, _tcpFrameCombo->currentText ().toStdString ());
}

rw::core::Ptr< rw::proximity::CollisionDetector >
KinematicAnalysisWidget::collisionDetectorForAnalysis (
    bool requested, bool* unavailable) const
{
    if (unavailable != nullptr)
        *unavailable = false;
    if (!requested)
        return NULL;
    if (_studio == nullptr) {
        if (unavailable != nullptr)
            *unavailable = true;
        return NULL;
    }
    rw::core::Ptr< rw::proximity::CollisionDetector > detector =
        _studio->getCollisionDetector ();
    if (detector == NULL && unavailable != nullptr)
        *unavailable = true;
    return detector;
}

double KinematicAnalysisWidget::ikXInputMeters () const
{
    return metersFromDisplayLength (_ikXSpin->value (), _ikLengthUnit);
}

double KinematicAnalysisWidget::ikYInputMeters () const
{
    return metersFromDisplayLength (_ikYSpin->value (), _ikLengthUnit);
}

double KinematicAnalysisWidget::ikZInputMeters () const
{
    return metersFromDisplayLength (_ikZSpin->value (), _ikLengthUnit);
}

double KinematicAnalysisWidget::ikRollInputDeg () const
{
    return degreesFromDisplayAngle (_ikRollSpin->value (), _ikAngleUnit);
}

double KinematicAnalysisWidget::ikPitchInputDeg () const
{
    return degreesFromDisplayAngle (_ikPitchSpin->value (), _ikAngleUnit);
}

double KinematicAnalysisWidget::ikYawInputDeg () const
{
    return degreesFromDisplayAngle (_ikYawSpin->value (), _ikAngleUnit);
}

void KinematicAnalysisWidget::setIkPoseMetersDeg (
    const std::array< double, 3 >& positionMeters,
    const std::array< double, 3 >& rpyDeg)
{
    const QSignalBlocker bx (_ikXSpin);
    const QSignalBlocker by (_ikYSpin);
    const QSignalBlocker bz (_ikZSpin);
    const QSignalBlocker br (_ikRollSpin);
    const QSignalBlocker bp (_ikPitchSpin);
    const QSignalBlocker bw (_ikYawSpin);
    _ikXSpin->setValue (displayLengthFromMeters (positionMeters[0], _ikLengthUnit));
    _ikYSpin->setValue (displayLengthFromMeters (positionMeters[1], _ikLengthUnit));
    _ikZSpin->setValue (displayLengthFromMeters (positionMeters[2], _ikLengthUnit));
    _ikRollSpin->setValue (displayAngleFromDegrees (rpyDeg[0], _ikAngleUnit));
    _ikPitchSpin->setValue (displayAngleFromDegrees (rpyDeg[1], _ikAngleUnit));
    _ikYawSpin->setValue (displayAngleFromDegrees (rpyDeg[2], _ikAngleUnit));
}

void KinematicAnalysisWidget::updateIkUnitDisplay ()
{
    if (_ikXSpin == NULL || _ikDistanceUnitCombo == NULL || _ikAngleUnitCombo == NULL)
        return;

    const std::array< double, 3 > positionMeters = {{
        ikXInputMeters (), ikYInputMeters (), ikZInputMeters ()}};
    const std::array< double, 3 > rpyDeg = {{
        ikRollInputDeg (), ikPitchInputDeg (), ikYawInputDeg ()}};

    _ikLengthUnit = static_cast< KinematicLengthUnit > (
        _ikDistanceUnitCombo->currentData ().toInt ());
    _ikAngleUnit = static_cast< KinematicAngleUnit > (
        _ikAngleUnitCombo->currentData ().toInt ());

    const QString lengthSuffix = QStringLiteral (" ") +
        QString::fromLatin1 (unitSuffix (_ikLengthUnit));
    const QString angleSuffix = QStringLiteral (" ") +
        QString::fromLatin1 (unitSuffix (_ikAngleUnit));
    for (QDoubleSpinBox* spin : {_ikXSpin, _ikYSpin, _ikZSpin}) {
        spin->setRange (displayLengthFromMeters (-1000.0, _ikLengthUnit),
                        displayLengthFromMeters (1000.0, _ikLengthUnit));
        spin->setSingleStep (displayLengthFromMeters (0.01, _ikLengthUnit));
        spin->setSuffix (lengthSuffix);
    }
    for (QDoubleSpinBox* spin : {_ikRollSpin, _ikPitchSpin, _ikYawSpin}) {
        spin->setRange (displayAngleFromDegrees (-360.0, _ikAngleUnit),
                        displayAngleFromDegrees (360.0, _ikAngleUnit));
        spin->setSingleStep (displayAngleFromDegrees (1.0, _ikAngleUnit));
        spin->setSuffix (angleSuffix);
    }
    setIkPoseMetersDeg (positionMeters, rpyDeg);
}

// refreshCurrentPose:重置四个 Current pose 表格与文本标签 → 调用
// KinematicAnalyzer::analyzeCurrentPose → 把结果填回 UI,同时更新 _lastCurrentPose
// 并刷新 Report tab 的汇总。
void KinematicAnalysisWidget::refreshCurrentPose ()
{
    // 重置所有面板为占位状态。
    if (_poseValueTable != NULL) {
        for (int c = 0; c < 6; ++c)
            _poseValueTable->setItem (0, c, makeItem (QStringLiteral ("-")));
    }
    if (_poseIndicatorLabel != NULL)
        _poseIndicatorLabel->setText (
            tr("Condition: -    Manipulability: -    Min limit margin: -"));
    if (_jointStatusTable != NULL)
        _jointStatusTable->setRowCount (0);
    if (_jacobianTable != NULL) {
        _jacobianTable->setRowCount (0);
        _jacobianTable->setColumnCount (0);
    }
    if (_singularTable != NULL) {
        _singularTable->setRowCount (0);
        _singularTable->setColumnCount (0);
    }
    if (_warningLabel != NULL)
        _warningLabel->setText (tr("Warnings: None"));

    if (_workcell == NULL) {
        setStatus(tr("Cannot refresh current pose: no WorkCell loaded."));
        return;
    }

    const std::string deviceName = _deviceCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::models::Device > device = deviceByName (_workcell, deviceName);
    if (device == NULL) {
        setStatus(tr("Cannot refresh current pose: no valid device selected."));
        return;
    }

    const std::string tcpName = _tcpFrameCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = frameByName (_workcell, tcpName);
    rw::kinematics::State state = currentState ();

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    const KinematicCurrentPoseResult result = analyzer.analyzeCurrentPose (device, tcpFrame, state);
    _lastCurrentPose = result;

    // ---- 1. 紧凑摘要栏(2 行 6 列 + 关键指标) ----
    if (_poseValueTable != NULL) {
        _poseValueTable->setItem (0, 0, makeItem (result.tcpPosition[0]));
        _poseValueTable->setItem (0, 1, makeItem (result.tcpPosition[1]));
        _poseValueTable->setItem (0, 2, makeItem (result.tcpPosition[2]));
        _poseValueTable->setItem (0, 3, makeItem (result.tcpRpyDeg[0]));
        _poseValueTable->setItem (0, 4, makeItem (result.tcpRpyDeg[1]));
        _poseValueTable->setItem (0, 5, makeItem (result.tcpRpyDeg[2]));
    }
    // 表头高度在初次布局后才会稳定,refresh 阶段再调一次确保紧凑。
    if (_poseValueTable != NULL)
        setCompactTableVisibleRows (_poseValueTable, 1);
    if (_poseIndicatorLabel != NULL) {
        const QString condText = std::isinf (result.conditionNumber) ?
            QStringLiteral ("inf") : QString::number (result.conditionNumber, 'g', 6);
        const QString minMargin = result.minJointLimitMargin > 0.0 ?
            QString::number (result.minJointLimitMargin, 'g', 6) : QStringLiteral ("-");
        _poseIndicatorLabel->setText (
            tr("Condition: %1    Manipulability: %2    Min limit margin: %3")
                .arg (condText)
                .arg (QString::number (result.manipulability, 'g', 6))
                .arg (minMargin));
    }

    // ---- 2. 关节状态合并表 ----
    if (_jointStatusTable != NULL) {
        const int n = static_cast< int > (result.q.size ());
        _jointStatusTable->setRowCount (n);
        const int marginCount = static_cast< int > (result.jointLimitMargins.size ());
        for (int i = 0; i < n; ++i) {
            // 关节名:超过 14 字符用中间省略;完整名字进 tooltip。
            QString jointName = QString::fromStdString (deviceName + "_" + std::to_string (i));
            if (jointName.size () > 14)
                jointName = jointName.left (6) + QStringLiteral ("...") +
                            jointName.right (7);
            QTableWidgetItem* nameItem = makeItem (jointName);
            nameItem->setToolTip (QString::fromStdString (deviceName + "_" + std::to_string (i)));
            _jointStatusTable->setItem (i, 0, nameItem);
            _jointStatusTable->setItem (i, 1, makeItem (result.q[static_cast< std::size_t > (i)]));

            // Limit margin 与 Status。
            QString marginText = QStringLiteral ("-");
            QString statusText = QStringLiteral ("OK");
            if (i < marginCount) {
                const double m = result.jointLimitMargins[static_cast< std::size_t > (i)];
                marginText = QString::number (m, 'g', 6);
                if (m < 0.0)
                    statusText = QStringLiteral ("Fail");
                else if (m < _thresholds.nearJointLimitRatio)
                    statusText = QStringLiteral ("Near");
            }
            _jointStatusTable->setItem (i, 2, makeItem (marginText));
            QTableWidgetItem* statusItem = makeItem (statusText);
            // 颜色暗示:Pass=默认、Near/Fail 用粗体。
            if (statusText == QStringLiteral ("Fail"))
                statusItem->setForeground (QColor (200, 0, 0));
            else if (statusText == QStringLiteral ("Near"))
                statusItem->setForeground (QColor (200, 130, 0));
            _jointStatusTable->setItem (i, 3, statusItem);
        }
        // 行数稳定后重新固定高度:6 轴完整可见,DOF 较多时也只占实际行数。
        setCompactTableVisibleRows (_jointStatusTable, n);
    }

    // ---- 3. Jacobian 全宽主表 ----
    if (_jacobianTable != NULL &&
        result.jacobianRows > 0 && result.jacobianCols > 0) {
        // 列数(q 数)会变,所以列头每次重设。
        QStringList headers;
        for (int c = 0; c < result.jacobianCols; ++c)
            headers << tr("q%1").arg (c);
        _jacobianTable->setColumnCount (result.jacobianCols);
        _jacobianTable->setRowCount (result.jacobianRows);
        _jacobianTable->setHorizontalHeaderLabels (headers);
        // 行头:基线 6 行(vx vy vz wx wy wz),多于 6 行的 Jacobian 也会自动出滚动条。
        QStringList rowHeaders;
        const QString labels[6] = {"vx", "vy", "vz", "wx", "wy", "wz"};
        for (int r = 0; r < result.jacobianRows; ++r)
            rowHeaders << labels[r % 6];
        _jacobianTable->setVerticalHeaderLabels (rowHeaders);
        for (int r = 0; r < result.jacobianRows; ++r) {
            for (int c = 0; c < result.jacobianCols; ++c) {
                const double v = result.jacobianRowMajor[
                    static_cast< std::size_t > (r * result.jacobianCols + c)];
                _jacobianTable->setItem (r, c, makeItem (v));
            }
        }
        // 6 行及以下时让 6 行可见;多于 6 行才允许垂直滚动。
        if (result.jacobianRows <= 6)
            _jacobianTable->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
        else
            _jacobianTable->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    }

    // ---- 4. Singular values:1 行多列,σ index 在表头 ----
    if (_singularTable != NULL) {
        const int singCount = static_cast< int > (result.singularValues.size ());
        QStringList headers;
        for (int i = 0; i < singCount; ++i)
            headers << tr("σ%1").arg (i);
        if (singCount > 0)
            headers << tr("σmin");
        _singularTable->setColumnCount (headers.size ());
        _singularTable->setRowCount (1);
        _singularTable->setHorizontalHeaderLabels (headers);

        for (int i = 0; i < singCount; ++i) {
            _singularTable->setItem (
                0, i,
                makeItem (result.singularValues[static_cast< std::size_t > (i)]));
        }
        // σmin 列:取最小值(奇异值已降序,最右一列就是 min)
        if (singCount > 0) {
            const double sigmaMin = result.singularValues.back ();
            _singularTable->setItem (0, singCount, makeItem (sigmaMin));
        }
        // 表头高度初次布局后才稳定,refresh 阶段再固定一次。
        setCompactTableVisibleRows (_singularTable, 1);
    }

    // ---- 5. Warnings:默认 None,有告警时展开 ----
    if (_warningLabel != NULL) {
        if (result.warnings.empty ()) {
            _warningLabel->setText (tr("Warnings: None"));
        }
        else {
            QStringList lines;
            for (const rws::AnalysisWarning& w : result.warnings)
                lines << QStringLiteral ("[%1] %2: %3")
                    .arg (QString::fromLatin1 (statusText (w.severity)))
                    .arg (QString::fromStdString (w.code))
                    .arg (QString::fromStdString (w.message));
            _warningLabel->setText (tr("Warnings:") + QStringLiteral ("\n") +
                                    lines.join (QStringLiteral ("\n")));
        }
    }

    setStatus(tr("Current pose analysis refreshed."));
    updateReportSummary ();
}

// solveIk:从 IK tab 读取目标点(x/y/z + RPY),转 TaskPoint 后调 analyzeIk;
// 结果按 sortIkSolutionsForDisplay 已排好,逐条写入表格;同时把失败原因列在
// "Q / failures" 一栏。
void KinematicAnalysisWidget::importCurrentPoseToIk ()
{
    if (_workcell == NULL) {
        setStatus (tr("Cannot import current TCP pose: no WorkCell loaded."));
        return;
    }
    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == NULL) {
        setStatus (tr("Cannot import current TCP pose: no valid device selected."));
        return;
    }
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();
    if (tcpFrame == NULL) {
        setStatus (tr("Cannot import current TCP pose: no valid TCP frame selected."));
        return;
    }

    try {
        const rw::math::Transform3D<> baseTtcp =
            rw::kinematics::Kinematics::frameTframe (
                device->getBase (), tcpFrame.get (), currentState ());
        const rw::math::RPY<> rpy (baseTtcp.R ());
        const double toDeg = 180.0 / 3.141592653589793238462643383279502884;
        setIkPoseMetersDeg (
            {{baseTtcp.P ()[0], baseTtcp.P ()[1], baseTtcp.P ()[2]}},
            {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}});
        setStatus (tr("Imported current TCP pose into IK target."));
    }
    catch (const std::exception& e) {
        setStatus (tr("Cannot import current TCP pose: %1").arg (QString::fromStdString (e.what ())));
    }
    catch (...) {
        setStatus (tr("Cannot import current TCP pose: unknown error."));
    }
}

void KinematicAnalysisWidget::solveIk ()
{
    _ikSolutionTable->setRowCount(0);
    _ikSummaryLabel->setText(tr("Candidates: -    Usable unique: -"));
    // 立即清空详情并禁用 Apply,保证所有提前返回路径都不会保留旧数据。
    setIkDetailsEmpty ();
    if (_ikDuplicateQThresholdSpin != NULL)
        _thresholds.ikDuplicateQThreshold = _ikDuplicateQThresholdSpin->value ();

    if (_workcell == NULL) {
        setStatus(tr("Cannot solve IK: no WorkCell loaded."));
        return;
    }

    const std::string deviceName = _deviceCombo->currentText().toStdString();
    rw::core::Ptr< rw::models::Device > device = deviceByName(_workcell, deviceName);
    if (device == NULL) {
        _ikSummaryLabel->setText(tr("Candidates: no device"));
        setStatus(tr("Cannot solve IK: no valid device selected."));
        return;
    }

    const std::string tcpName = _tcpFrameCombo->currentText().toStdString();
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = frameByName(_workcell, tcpName);

    TaskPoint target;
    target.id = "ik_target";
    target.name = _ikTargetNameEdit->text().toStdString();
    target.tcpFrame = tcpName;
    target.position = {{ikXInputMeters(), ikYInputMeters(), ikZInputMeters()}};
    target.rpyDeg = {{ikRollInputDeg(), ikPitchInputDeg(), ikYawInputDeg()}};
    target.tolerance.positionMeters = _thresholds.positionToleranceMeters;
    target.tolerance.orientationDeg = _thresholds.orientationToleranceDeg;

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (true, &collisionUnavailable);
    const KinematicIkAnalysisResult result =
        analyzer.analyzeIk(device, tcpFrame, currentState(), target, collisionDetector);

    // 保存最近一次完整结果,_refreshIkSolutionView 与 _updateIkSolutionDetails
    // 都从这里读。表格真正填充交给 refreshIkSolutionView 统一负责,
    // 这样过滤器切换时不必再调 Solve,UI 即时刷新。
    _lastIkResult = result;
    refreshIkSolutionView ();

    _ikSummaryLabel->setText(
        tr("Status: %1    Usable unique: %2")
            .arg(QString::fromLatin1(statusText(result.status)))
            .arg(static_cast<int>(result.usableSolutionCount)));
    if (collisionUnavailable) {
        setStatus (tr("IK analysis completed with %1 candidate(s); collision checking was unavailable.")
                       .arg (static_cast< int > (result.solutions.size ())));
    }
    else {
        setStatus(tr("IK analysis completed with %1 candidate(s).")
                      .arg(static_cast<int>(result.solutions.size())));
    }
}

// shouldShowIkSolution:IK 解过滤器,组合两个 QCheckBox:
//   1) "Show usable only" 勾上 → 只保留无碰撞 + status != Fail 的解;
//   2) 否则若 "Show failed candidates" 未勾 → 隐藏 status == Fail 的诊断解;
//   3) 其余情况都展示,保留所有候选用于诊断。
bool KinematicAnalysisWidget::shouldShowIkSolution (
    const KinematicIkSolution& solution) const
{
    const bool usable = !solution.inCollision && solution.status != AnalysisStatus::Fail;
    if (_ikShowUsableOnlyCheck != NULL && _ikShowUsableOnlyCheck->isChecked ())
        return usable;
    if (_ikShowFailedCandidatesCheck != NULL &&
        !_ikShowFailedCandidatesCheck->isChecked () &&
        solution.status == AnalysisStatus::Fail)
        return false;
    return true;
}

// applySelectedIkSolution:把用户在 IK 表格里选中的那条解写回当前 state:
//   1) 通过 Qt::UserRole 取出 QVariantList(写入表格时由 makeQItem 缓存);
//   2) 校验 DOF 维度;
//   3) device->setQ + studio->setState 把整个 state 推回 RobWorkStudio;
//   4) refreshCurrentPose 更新 Current pose tab 与 Report tab。
void KinematicAnalysisWidget::applySelectedIkSolution ()
{
    if (_workcell == NULL || _studio == NULL) {
        setStatus(tr("Cannot apply IK solution: no WorkCell or RobWorkStudio context."));
        return;
    }

    const QList<QTableWidgetItem*> selected = _ikSolutionTable->selectedItems();
    if (selected.empty()) {
        setStatus(tr("Cannot apply IK solution: no solution row selected."));
        return;
    }

    const int row = selected.front()->row();
    // 表拆分后 Q 在第 11 列(0-indexed)。
    QTableWidgetItem* qItem = _ikSolutionTable->item(row, 11);
    if (qItem == NULL) {
        setStatus(tr("Cannot apply IK solution: selected row has no Q value."));
        return;
    }

    const QVariantList storedQ = qItem->data(Qt::UserRole).toList();
    if (storedQ.empty()) {
        setStatus(tr("Cannot apply IK solution: selected row has no stored Q data."));
        return;
    }

    rw::math::Q q(static_cast<std::size_t>(storedQ.size()));
    for (int i = 0; i < storedQ.size(); ++i)
        q(static_cast<std::size_t>(i)) = storedQ[i].toDouble();

    const std::string deviceName = _deviceCombo->currentText().toStdString();
    rw::core::Ptr< rw::models::Device > device = deviceByName(_workcell, deviceName);
    if (device == NULL || q.size() != device->getDOF()) {
        setStatus(tr("Cannot apply IK solution: Q dimension does not match selected device."));
        return;
    }

    rw::kinematics::State state = currentState();
    device->setQ(q, state);
    _studio->setState(state);
    refreshCurrentPose();
    setStatus(tr("Applied selected IK solution to the current state."));
}

// -------------------------------------------------------------------------
//  Task point tab
//  表格列:id | name | type | x/y/z | roll/pitch/yaw | posTol | oriTol |
//         weight | result | reason。第 0 列是 checkbox 表示 enabled。
//  按钮区:Add row / Remove / Import CSV / Export CSV / Analyze all。
// -------------------------------------------------------------------------
void KinematicAnalysisWidget::buildTaskPointTab ()
{
    QVBoxLayout* tpLayout = new QVBoxLayout(_taskPointTab);

    QGridLayout* buttonRow = new QGridLayout();
    _addTaskPointButton         = new QPushButton(tr("Add row"), _taskPointTab);
    _removeTaskPointButton      = new QPushButton(tr("Remove selected"), _taskPointTab);
    _importTaskPointsButton     = new QPushButton(tr("Import CSV"), _taskPointTab);
    _exportTaskPointsButton     = new QPushButton(tr("Export CSV"), _taskPointTab);
    _analyzeAllTaskPointsButton = new QPushButton(tr("Analyze all"), _taskPointTab);
    buttonRow->addWidget (_addTaskPointButton, 0, 0);
    buttonRow->addWidget (_removeTaskPointButton, 0, 1);
    buttonRow->addWidget (_importTaskPointsButton, 1, 0);
    buttonRow->addWidget (_exportTaskPointsButton, 1, 1);
    buttonRow->addWidget (_analyzeAllTaskPointsButton, 1, 2);
    tpLayout->addLayout (buttonRow);

    _taskPointTable = new QTableWidget (_taskPointTab);
    _taskPointTable->setColumnCount (15);
    _taskPointTable->setHorizontalHeaderLabels ({
        tr("Enabled"), tr("id"), tr("name"), tr("type"),
        tr("x"), tr("y"), tr("z"),
        tr("roll"), tr("pitch"), tr("yaw"),
        tr("posTol"), tr("oriTol"),
        tr("weight"),
        tr("result"), tr("reason")
    });
    _taskPointTable->setSelectionBehavior (QAbstractItemView::SelectRows);
    _taskPointTable->setSelectionMode (QAbstractItemView::SingleSelection);
    _taskPointTable->setAlternatingRowColors (true);
    _taskPointTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    _taskPointTable->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
    _taskPointTable->horizontalHeader ()->setSectionResizeMode (QHeaderView::Interactive);
    _taskPointTable->verticalHeader ()->setVisible (false);
    tpLayout->addWidget (_taskPointTable);

    _taskPointSummaryLabel = new QLabel (tr("Enabled: 0    Pass: 0    Warning: 0    Fail: 0    Reachable rate: -"),
                                          _taskPointTab);
    tpLayout->addWidget (_taskPointSummaryLabel);
    tpLayout->addStretch ();
}

// buildWorkspaceTab:Workspace 子页布局。控件包括:
//   - 采样数 / 网格步数 / 模式(Random/Grid) / 碰撞检查 / 着色模式;
//   - Run / Export CSV 两个动作;
//   - 结果表 8 列:Index / Status / Collision / TCP x/y/z / Manipulability / Min margin;
//   - 顶部 summary 显示无碰撞 / Warning / Fail 计数与平均可操作度。
void KinematicAnalysisWidget::buildWorkspaceTab ()
{
    QVBoxLayout* layout = new QVBoxLayout (_workspaceTab);

    QGridLayout* controls = new QGridLayout ();
    _workspaceSampleCountSpin = new QSpinBox (_workspaceTab);
    _workspaceSampleCountSpin->setRange (1, 1000000);
    _workspaceSampleCountSpin->setValue (1000);
    _workspaceGridStepsSpin = new QSpinBox (_workspaceTab);
    _workspaceGridStepsSpin->setRange (1, 100);
    _workspaceGridStepsSpin->setValue (5);
    _workspaceModeCombo = new QComboBox (_workspaceTab);
    _workspaceModeCombo->addItem (tr("Random uniform"));
    _workspaceModeCombo->addItem (tr("Grid"));
    _workspaceCollisionCheck = new QCheckBox (tr("Collision"), _workspaceTab);
    _workspaceCollisionCheck->setChecked (true);
    _workspaceColorModeCombo = new QComboBox (_workspaceTab);
    _workspaceColorModeCombo->addItems ({tr("Reachability"), tr("Manipulability"),
                                         tr("Joint limit"), tr("Collision")});
    _workspaceRunButton = new QPushButton (tr("Run"), _workspaceTab);
    _workspaceExportButton = new QPushButton (tr("Export CSV"), _workspaceTab);

    controls->addWidget (new QLabel (tr("Samples:"), _workspaceTab), 0, 0);
    controls->addWidget (_workspaceSampleCountSpin, 0, 1);
    controls->addWidget (new QLabel (tr("Mode:"), _workspaceTab), 0, 2);
    controls->addWidget (_workspaceModeCombo, 0, 3);
    controls->addWidget (new QLabel (tr("Grid steps:"), _workspaceTab), 1, 0);
    controls->addWidget (_workspaceGridStepsSpin, 1, 1);
    controls->addWidget (_workspaceCollisionCheck, 1, 2);
    controls->addWidget (_workspaceColorModeCombo, 1, 3);
    controls->addWidget (_workspaceRunButton, 2, 0);
    controls->addWidget (_workspaceExportButton, 2, 1);
    layout->addLayout (controls);

    _workspaceSummaryLabel = new QLabel (tr("Samples: 0    Collision-free: 0    Avg manipulability: -"),
                                         _workspaceTab);
    layout->addWidget (_workspaceSummaryLabel);

    _workspaceTable = new QTableWidget (_workspaceTab);
    _workspaceTable->setColumnCount (8);
    _workspaceTable->setHorizontalHeaderLabels ({
        tr("Index"), tr("Status"), tr("Collision"), tr("TCP x"), tr("TCP y"), tr("TCP z"),
        tr("Manipulability"), tr("Min limit margin")
    });
    _workspaceTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    configureAnalysisTable (_workspaceTable);
    layout->addWidget (_workspaceTable);
}

// buildPoseReachabilityTab:位姿可达性子页布局。
//   - Source ComboBox:Task points / Manual rows(两种取位置的方式);
//   - Directions / Rolls:球面方向数 / 绕 Z 滚动采样数;
//   - 手动位置表 + Add row;
//   - 结果表 8 列:Index / Status / x/y/z / Sampled / Reachable / Coverage;
//   - 顶部 summary 显示平均 coverage。
void KinematicAnalysisWidget::buildPoseReachabilityTab ()
{
    QVBoxLayout* layout = new QVBoxLayout (_poseReachTab);

    QGridLayout* controls = new QGridLayout ();
    _poseSourceCombo = new QComboBox (_poseReachTab);
    _poseSourceCombo->addItem (tr("Task points"));
    _poseSourceCombo->addItem (tr("Manual rows"));
    _poseDirectionSamplesSpin = new QSpinBox (_poseReachTab);
    _poseDirectionSamplesSpin->setRange (0, 1000);
    _poseDirectionSamplesSpin->setValue (24);
    _poseRollSamplesSpin = new QSpinBox (_poseReachTab);
    _poseRollSamplesSpin->setRange (1, 360);
    _poseRollSamplesSpin->setValue (1);
    _poseCollisionCheck = new QCheckBox (tr("Collision"), _poseReachTab);
    _poseCollisionCheck->setChecked (true);
    _poseAddRowButton = new QPushButton (tr("Add row"), _poseReachTab);
    _poseAnalyzeButton = new QPushButton (tr("Run"), _poseReachTab);
    _poseExportButton = new QPushButton (tr("Export CSV"), _poseReachTab);

    controls->addWidget (new QLabel (tr("Source:"), _poseReachTab), 0, 0);
    controls->addWidget (_poseSourceCombo, 0, 1);
    controls->addWidget (new QLabel (tr("Directions:"), _poseReachTab), 0, 2);
    controls->addWidget (_poseDirectionSamplesSpin, 0, 3);
    controls->addWidget (new QLabel (tr("Rolls:"), _poseReachTab), 1, 0);
    controls->addWidget (_poseRollSamplesSpin, 1, 1);
    controls->addWidget (_poseCollisionCheck, 1, 2);
    controls->addWidget (_poseAddRowButton, 2, 0);
    controls->addWidget (_poseAnalyzeButton, 2, 1);
    controls->addWidget (_poseExportButton, 2, 2);
    layout->addLayout (controls);

    _posePositionTable = new QTableWidget (_poseReachTab);
    _posePositionTable->setColumnCount (3);
    _posePositionTable->setHorizontalHeaderLabels ({tr("x"), tr("y"), tr("z")});
    configureAnalysisTable (_posePositionTable);
    layout->addWidget (new QLabel (tr("Manual positions"), _poseReachTab));
    layout->addWidget (_posePositionTable);

    _poseSummaryLabel = new QLabel (tr("Positions: 0    Average coverage: -"), _poseReachTab);
    layout->addWidget (_poseSummaryLabel);

    _poseResultTable = new QTableWidget (_poseReachTab);
    _poseResultTable->setColumnCount (8);
    _poseResultTable->setHorizontalHeaderLabels ({
        tr("Index"), tr("Status"), tr("x"), tr("y"), tr("z"),
        tr("Sampled"), tr("Reachable"), tr("Coverage")
    });
    _poseResultTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    configureAnalysisTable (_poseResultTable);
    layout->addWidget (_poseResultTable);
}

// buildReportTab:Report 子页布局。
//   - 顶部 summary 标签:显示当前 / 任务 / 工作空间 / 位姿可达性的综合状态;
//   - 7 个 DoubleSpinBox 调阈值(nearLimit / cond warn / cond fail / sigma /
//     manipulability / pos tol / ori tol)+ Apply thresholds 按钮;
//   - Refresh / Export JSON / Export CSV 三个动作按钮;
//   - 底部告警表 4 列:Severity / Code / Source / Message。
void KinematicAnalysisWidget::buildReportTab ()
{
    QVBoxLayout* layout = new QVBoxLayout (_reportTab);

    _reportSummaryLabel = new QLabel (tr("No report data."), _reportTab);
    layout->addWidget (_reportSummaryLabel);

    QGridLayout* thresholdGrid = new QGridLayout ();
    _thresholdNearLimitSpin = new QDoubleSpinBox (_reportTab);
    _thresholdNearLimitSpin->setRange (0.0, 1.0);
    _thresholdNearLimitSpin->setDecimals (6);
    _thresholdNearLimitSpin->setValue (_thresholds.nearJointLimitRatio);
    _thresholdConditionWarningSpin = new QDoubleSpinBox (_reportTab);
    _thresholdConditionWarningSpin->setRange (1.0, 1000000.0);
    _thresholdConditionWarningSpin->setValue (_thresholds.conditionWarning);
    _thresholdConditionFailSpin = new QDoubleSpinBox (_reportTab);
    _thresholdConditionFailSpin->setRange (1.0, 1000000.0);
    _thresholdConditionFailSpin->setValue (_thresholds.conditionFail);
    _thresholdSingularValueSpin = new QDoubleSpinBox (_reportTab);
    _thresholdSingularValueSpin->setRange (0.0, 1.0);
    _thresholdSingularValueSpin->setDecimals (8);
    _thresholdSingularValueSpin->setValue (_thresholds.singularValueWarning);
    _thresholdManipulabilitySpin = new QDoubleSpinBox (_reportTab);
    _thresholdManipulabilitySpin->setRange (0.0, 1000000.0);
    _thresholdManipulabilitySpin->setDecimals (8);
    _thresholdManipulabilitySpin->setValue (_thresholds.manipulabilityWarning);
    _thresholdPositionToleranceSpin = new QDoubleSpinBox (_reportTab);
    _thresholdPositionToleranceSpin->setRange (0.0, 1000.0);
    _thresholdPositionToleranceSpin->setDecimals (6);
    _thresholdPositionToleranceSpin->setValue (_thresholds.positionToleranceMeters);
    _thresholdOrientationToleranceSpin = new QDoubleSpinBox (_reportTab);
    _thresholdOrientationToleranceSpin->setRange (0.0, 360.0);
    _thresholdOrientationToleranceSpin->setDecimals (4);
    _thresholdOrientationToleranceSpin->setValue (_thresholds.orientationToleranceDeg);
    _thresholdApplyButton = new QPushButton (tr("Apply thresholds"), _reportTab);

    thresholdGrid->addWidget (new QLabel (tr("Near limit:"), _reportTab), 0, 0);
    thresholdGrid->addWidget (_thresholdNearLimitSpin, 0, 1);
    thresholdGrid->addWidget (new QLabel (tr("Cond warn:"), _reportTab), 0, 2);
    thresholdGrid->addWidget (_thresholdConditionWarningSpin, 0, 3);
    thresholdGrid->addWidget (new QLabel (tr("Cond fail:"), _reportTab), 1, 0);
    thresholdGrid->addWidget (_thresholdConditionFailSpin, 1, 1);
    thresholdGrid->addWidget (new QLabel (tr("Sigma warn:"), _reportTab), 1, 2);
    thresholdGrid->addWidget (_thresholdSingularValueSpin, 1, 3);
    thresholdGrid->addWidget (new QLabel (tr("Manip warn:"), _reportTab), 2, 0);
    thresholdGrid->addWidget (_thresholdManipulabilitySpin, 2, 1);
    thresholdGrid->addWidget (new QLabel (tr("Pos tol:"), _reportTab), 2, 2);
    thresholdGrid->addWidget (_thresholdPositionToleranceSpin, 2, 3);
    thresholdGrid->addWidget (new QLabel (tr("Ori tol:"), _reportTab), 3, 0);
    thresholdGrid->addWidget (_thresholdOrientationToleranceSpin, 3, 1);
    thresholdGrid->addWidget (_thresholdApplyButton, 3, 2);
    layout->addLayout (thresholdGrid);

    QGridLayout* buttons = new QGridLayout ();
    _reportRefreshButton = new QPushButton (tr("Refresh"), _reportTab);
    _reportExportJsonButton = new QPushButton (tr("Export JSON"), _reportTab);
    _reportExportCsvButton = new QPushButton (tr("Export CSV"), _reportTab);
    buttons->addWidget (_reportRefreshButton, 0, 0);
    buttons->addWidget (_reportExportJsonButton, 0, 1);
    buttons->addWidget (_reportExportCsvButton, 0, 2);
    layout->addLayout (buttons);

    _reportWarningTable = new QTableWidget (_reportTab);
    _reportWarningTable->setColumnCount (4);
    _reportWarningTable->setHorizontalHeaderLabels ({
        tr("Severity"), tr("Code"), tr("Source"), tr("Message")
    });
    _reportWarningTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    configureAnalysisTable (_reportWarningTable);
    layout->addWidget (_reportWarningTable);
}

void KinematicAnalysisWidget::setTaskPointTableColumnWidths ()
{
    if (_taskPointTable == nullptr)
        return;
    _taskPointTable->resizeColumnsToContents ();
}

// addTaskPointRow:在 Task point 表格末尾追加一行默认值(0 位姿,Generic 类型,
// enabled=on);后续用户可在表格里直接编辑 ID/Name/Type/X/Y/Z/...
void KinematicAnalysisWidget::addTaskPointRow ()
{
    if (_taskPointTable == nullptr)
        return;
    const int row = _taskPointTable->rowCount ();
    _taskPointTable->insertRow (row);
    auto check = [this] (int r, int c) {
        QTableWidgetItem* item = new QTableWidgetItem ();
        item->setCheckState (Qt::Checked);
        item->setFlags ((item->flags () | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        _taskPointTable->setItem (r, c, item);
    };
    check (row, 0);
    auto text = [this] (int r, int c, const QString& s, bool editable) {
        QTableWidgetItem* item = new QTableWidgetItem (s);
        if (!editable)
            item->setFlags (item->flags () & ~Qt::ItemIsEditable);
        _taskPointTable->setItem (r, c, item);
    };
    text (row, 1,  QString ("P%1").arg (row + 1), true);
    text (row, 2,  QString ("Task %1").arg (row + 1), true);
    text (row, 3,  QString ("Generic"), true);
    text (row, 4,  QString ("0"), true);
    text (row, 5,  QString ("0"), true);
    text (row, 6,  QString ("0"), true);
    text (row, 7,  QString ("0"), true);
    text (row, 8,  QString ("0"), true);
    text (row, 9,  QString ("0"), true);
    text (row, 10, QString ("0.001"), true);
    text (row, 11, QString ("1.0"), true);
    text (row, 12, QString ("1.0"), true);
    text (row, 13, QString ("-"), false);
    text (row, 14, QString ("-"), false);
    setTaskPointTableColumnWidths ();
    setStatus(tr("Added task point row %1.").arg(row + 1));
}

// removeSelectedTaskPointRow:删除当前选中行(没有选中时给出提示)。
void KinematicAnalysisWidget::removeSelectedTaskPointRow ()
{
    if (_taskPointTable == nullptr)
        return;
    const int row = _taskPointTable->currentRow ();
    if (row >= 0) {
        _taskPointTable->removeRow (row);
        setStatus(tr("Removed selected task point row."));
    }
    else {
        setStatus(tr("No task point row selected."));
    }
}

namespace {
// setCell / cellText:导入导出场景下常用的 cell 写入 / 读取帮助函数,
// 比直接 new QTableWidgetItem 简短。
QTableWidgetItem* setCell (QTableWidget* t, int r, int c, const QString& s, bool editable)
{
    QTableWidgetItem* item = new QTableWidgetItem (s);
    if (!editable)
        item->setFlags (item->flags () & ~Qt::ItemIsEditable);
    t->setItem (r, c, item);
    return item;
}
QString cellText (QTableWidget* t, int r, int c)
{
    QTableWidgetItem* item = t->item (r, c);
    return item == nullptr ? QString () : item->text ();
}

// Task 2 辅助:把"原始 solution 在 _lastIkResult 中的索引"存到 cell 的
// Qt::UserRole + 1 槽中,这样过滤后表格的 displayRow 与 solutionIndex
// 不再一致(同一条 solution 可能因为勾选了"只看可用解"被跳过),
// 但用户选中任何一行时仍能反查到原始索引。
void storeIkSolutionIndex (QTableWidgetItem* item, int solutionIndex)
{
    if (item != NULL)
        item->setData (Qt::UserRole + 1, solutionIndex);
}

// Task 4 辅助:把详情表的两列(field/value)写一行,直接复用 makeItem。
void setDetailRow (QTableWidget* table, int row, const QString& field, const QString& value)
{
    QTableWidgetItem* fieldItem = makeItem (field);
    QTableWidgetItem* valueItem = makeItem (value);
    // 给两列都加 tooltip,允许 hover 查看完整长文本(尤其是
    // 含长 Q 向量的 Metrics/Q 行),不必打开水平滚动。
    fieldItem->setToolTip (field);
    valueItem->setToolTip (value);
    table->setItem (row, 0, fieldItem);
    table->setItem (row, 1, valueItem);
}
}    // namespace

// collectTaskPointsFromTable:从 Task point 表格逐行读出 TaskPoint 结构。
//   - 第 0 列是 checkbox 决定 enabled;
//   - 第 3 列的 Type 字符串映射回 TaskPointType 枚举;
//   - 数值列通过 toDouble 解析。
std::vector< TaskPoint > KinematicAnalysisWidget::collectTaskPointsFromTable (QString* error) const
{
    std::vector< TaskPoint > points;
    if (error != nullptr)
        error->clear ();
    if (_taskPointTable == nullptr)
        return points;
    auto readNumber = [this, error] (int row, int column, const QString& field, double& value) {
        bool ok = false;
        value = cellText (_taskPointTable, row, column).toDouble (&ok);
        if (ok && std::isfinite (value))
            return true;
        if (error != nullptr) {
            *error = tr("Task point row %1 has an invalid %2 value.")
                         .arg (row + 1).arg (field);
        }
        return false;
    };
    for (int r = 0; r < _taskPointTable->rowCount (); ++r) {
        TaskPoint p;
        p.id      = cellText (_taskPointTable, r, 1).toStdString ();
        p.name    = cellText (_taskPointTable, r, 2).toStdString ();
        const QString typeText = cellText (_taskPointTable, r, 3);
        p.type    = TaskPointType::Generic;
        if (typeText.compare ("Pick", Qt::CaseInsensitive) == 0)
            p.type = TaskPointType::Pick;
        else if (typeText.compare ("Place", Qt::CaseInsensitive) == 0)
            p.type = TaskPointType::Place;
        else if (typeText.compare ("Weld", Qt::CaseInsensitive) == 0)
            p.type = TaskPointType::Weld;
        else if (typeText.compare ("Glue", Qt::CaseInsensitive) == 0)
            p.type = TaskPointType::Glue;
        else if (typeText.compare ("Inspect", Qt::CaseInsensitive) == 0)
            p.type = TaskPointType::Inspect;
        else if (typeText.compare ("Screw", Qt::CaseInsensitive) == 0)
            p.type = TaskPointType::Screw;
        else if (typeText.compare ("Custom", Qt::CaseInsensitive) == 0)
            p.type = TaskPointType::Custom;
        if (!readNumber (r, 4, tr("x"), p.position[0]) ||
            !readNumber (r, 5, tr("y"), p.position[1]) ||
            !readNumber (r, 6, tr("z"), p.position[2]) ||
            !readNumber (r, 7, tr("roll"), p.rpyDeg[0]) ||
            !readNumber (r, 8, tr("pitch"), p.rpyDeg[1]) ||
            !readNumber (r, 9, tr("yaw"), p.rpyDeg[2]) ||
            !readNumber (r, 10, tr("position tolerance"), p.tolerance.positionMeters) ||
            !readNumber (r, 11, tr("orientation tolerance"), p.tolerance.orientationDeg) ||
            !readNumber (r, 12, tr("weight"), p.weight))
            return std::vector< TaskPoint > ();
        if (p.tolerance.positionMeters < 0.0 || p.tolerance.orientationDeg < 0.0) {
            if (error != nullptr)
                *error = tr("Task point row %1 has a negative tolerance.").arg (r + 1);
            return std::vector< TaskPoint > ();
        }
        QTableWidgetItem* enabledItem = _taskPointTable->item (r, 0);
        p.enabled = enabledItem != nullptr && enabledItem->checkState () == Qt::Checked;
        points.push_back (p);
    }
    return points;
}

// applyTaskPointResults:把分析结果回写到 Task point 表格的 result / reason 两列,
// 并刷新底部 summary 标签(Enabled / Pass / Warning / Fail / Reachable rate)。
// 行数与表格行数取小,避免越界(导入 CSV 后行数可能少或多)。
void KinematicAnalysisWidget::applyTaskPointResults (
    const std::vector< TaskPointReachabilityResult >& results, double reachableRate)
{
    if (_taskPointTable == nullptr)
        return;
    const std::size_t n = std::min (results.size (),
                                    static_cast< std::size_t > (_taskPointTable->rowCount ()));
    std::size_t passCount = 0, warnCount = 0, failCount = 0, enabledCount = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const TaskPointReachabilityResult& r = results[i];
        if (r.taskPoint.enabled)
            ++enabledCount;
        const char* status =
            r.status == AnalysisStatus::Pass    ? "Pass" :
            r.status == AnalysisStatus::Warning ? "Warning" :
            r.status == AnalysisStatus::Fail    ? "Fail"    : "Unknown";
        if (r.status == AnalysisStatus::Pass)
            ++passCount;
        else if (r.status == AnalysisStatus::Warning)
            ++warnCount;
        else if (r.status == AnalysisStatus::Fail)
            ++failCount;
        setCell (_taskPointTable, static_cast< int > (i), 13, QString::fromUtf8 (status), false);
        QStringList reasons;
        for (KinematicFailureReason fr : r.failureReasons)
            reasons << QString::fromUtf8 (rws::toString (fr));
        const QString reasonText = reasons.isEmpty () ? QString ("-")
                                                       : reasons.join (QStringLiteral (", "));
        setCell (_taskPointTable, static_cast< int > (i), 14, reasonText, false);
    }
    if (_taskPointSummaryLabel != nullptr) {
        _taskPointSummaryLabel->setText (QString (
            "Enabled: %1    Pass: %2    Warning: %3    Fail: %4    Reachable rate: %5")
            .arg (enabledCount).arg (passCount).arg (warnCount).arg (failCount)
            .arg (QString::number (reachableRate, 'f', 3)));
    }
    setTaskPointTableColumnWidths ();
}

// importTaskPointsCsv:从用户选择的 CSV 文件读入任务点。
//   - 用 RobotAnalysisCsv::taskPointsFromCsv 解析(共享 CSV 序列化);
//   - 先清空表格,再逐点重建(覆盖式导入);
//   - result/reason 列固定填 "-",留给后续 Analyze all 写回。
void KinematicAnalysisWidget::importTaskPointsCsv ()
{
    if (_taskPointTable == nullptr) {
        setStatus(tr("Cannot import task points: table is not available."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName (
        this, tr("Import task points"), QString (),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus(tr("Task point import canceled."));
        return;
    }
    QFile file (path);
    if (!file.open (QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Import error"),
                              tr("Could not open %1").arg (path));
        setStatus(tr("Task point import failed: could not open file."));
        return;
    }
    const QString csv = QString::fromUtf8 (file.readAll ());
    file.close ();
    std::vector< TaskPoint > points;
    std::string err;
    if (!RobotAnalysisCsv::taskPointsFromCsv (csv.toStdString (), points, &err)) {
        QMessageBox::warning (this, tr("Import error"),
                              tr("CSV parse failed: %1").arg (QString::fromStdString (err)));
        setStatus(tr("Task point import failed: CSV parse error."));
        return;
    }
    _taskPointTable->setRowCount (0);
    for (const TaskPoint& p : points) {
        const int row = _taskPointTable->rowCount ();
        _taskPointTable->insertRow (row);
        QTableWidgetItem* enabled = new QTableWidgetItem ();
        enabled->setCheckState (p.enabled ? Qt::Checked : Qt::Unchecked);
        enabled->setFlags ((enabled->flags () | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        _taskPointTable->setItem (row, 0, enabled);
        setCell (_taskPointTable, row, 1,  QString::fromStdString (p.id), true);
        setCell (_taskPointTable, row, 2,  QString::fromStdString (p.name), true);
        setCell (_taskPointTable, row, 3,  QString::fromLatin1 (taskPointTypeText (p.type)), true);
        setCell (_taskPointTable, row, 4,  QString::number (p.position[0]), true);
        setCell (_taskPointTable, row, 5,  QString::number (p.position[1]), true);
        setCell (_taskPointTable, row, 6,  QString::number (p.position[2]), true);
        setCell (_taskPointTable, row, 7,  QString::number (p.rpyDeg[0]), true);
        setCell (_taskPointTable, row, 8,  QString::number (p.rpyDeg[1]), true);
        setCell (_taskPointTable, row, 9,  QString::number (p.rpyDeg[2]), true);
        setCell (_taskPointTable, row, 10, QString::number (p.tolerance.positionMeters), true);
        setCell (_taskPointTable, row, 11, QString::number (p.tolerance.orientationDeg), true);
        setCell (_taskPointTable, row, 12, QString::number (p.weight), true);
        setCell (_taskPointTable, row, 13, QStringLiteral ("-"), false);
        setCell (_taskPointTable, row, 14, QStringLiteral ("-"), false);
    }
    setTaskPointTableColumnWidths ();
    setStatus(tr("Imported %1 task point(s).").arg(static_cast<int>(points.size())));
}

// exportTaskPointsCsv:把表格当前内容序列化为 CSV 并写入用户指定文件。
// 写文件用 QFile::write(const char*, qint64) 写出 std::string 原始字节。
void KinematicAnalysisWidget::exportTaskPointsCsv ()
{
    if (_taskPointTable == nullptr) {
        setStatus(tr("Cannot export task points: table is not available."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export task points"), QString ("task_points.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus(tr("Task point export canceled."));
        return;
    }
    QString validationError;
    const std::vector< TaskPoint > points = collectTaskPointsFromTable (&validationError);
    if (!validationError.isEmpty ()) {
        QMessageBox::warning (this, tr("Export error"), validationError);
        setStatus (validationError);
        return;
    }
    const std::string csv                 = RobotAnalysisCsv::taskPointsToCsv (points);
    QFile file (path);
    if (!file.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        setStatus(tr("Task point export failed: could not open file for writing."));
        return;
    }
    file.write (csv.c_str (), static_cast< qint64 > (csv.size ()));
    file.close ();
    setStatus(tr("Exported %1 task point(s).").arg(static_cast<int>(points.size())));
}

// analyzeAllTaskPoints:批量跑任务点的 IK。
//   - 从表格读出 TaskPoint 列表;
//   - analyzeTaskPoints(此处传 NULL,跳过碰撞检测,避免依赖外部 collider);
//   - 把结果写回 _lastTaskPointResults 并刷新表格;
//   - 调用 updateReportSummary 让 Report tab 同步最新数据。
void KinematicAnalysisWidget::analyzeAllTaskPoints ()
{
    if (_workcell == nullptr) {
        setStatus(tr("Cannot analyze task points: no WorkCell loaded."));
        return;
    }
    if (_deviceCombo == nullptr || _deviceCombo->count () == 0) {
        setStatus(tr("Cannot analyze task points: no device available."));
        return;
    }
    const std::string deviceName = _deviceCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::models::Device > device = deviceByName (_workcell, deviceName);
    if (device == nullptr) {
        setStatus(tr("Cannot analyze task points: no valid device selected."));
        return;
    }
    const std::string tcpName = _tcpFrameCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = frameByName (_workcell, tcpName);
    const rw::kinematics::State state            = currentState ();

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    QString validationError;
    const std::vector< TaskPoint > points = collectTaskPointsFromTable (&validationError);
    if (!validationError.isEmpty ()) {
        setStatus (validationError);
        return;
    }
    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (true, &collisionUnavailable);
    const std::vector< TaskPointReachabilityResult > results =
        analyzer.analyzeTaskPoints (device, tcpFrame, state, points, collisionDetector);
    const double rate = analyzer.calculateReachableRate (results);
    _lastTaskPointResults = results;
    applyTaskPointResults (results, rate);
    const QString collisionNote = collisionUnavailable ?
        tr(" Collision checking was unavailable.") : QString ();
    setStatus(tr("Analyzed %1 task point(s). Reachable rate: %2.%3")
                  .arg(static_cast<int>(results.size()))
                  .arg(QString::number(rate, 'f', 3)).arg (collisionNote));
    updateReportSummary ();
}

// collectPoseReachabilityPositions:按 Source 下拉选择收集位置列表:
//   - 0 → Task points:复用 collectTaskPointsFromTable,只取 enabled 的位置;
//   - 1 → Manual rows:从 _posePositionTable 逐行读出 xyz。
std::vector< std::array< double, 3 > >
KinematicAnalysisWidget::collectPoseReachabilityPositions (QString* error) const
{
    std::vector< std::array< double, 3 > > positions;
    if (error != nullptr)
        error->clear ();
    if (_poseSourceCombo != NULL && _poseSourceCombo->currentIndex () == 0) {
        const std::vector< TaskPoint > points = collectTaskPointsFromTable (error);
        if (error != nullptr && !error->isEmpty ())
            return positions;
        for (const TaskPoint& point : points) {
            if (point.enabled)
                positions.push_back (point.position);
        }
        return positions;
    }

    if (_posePositionTable == NULL)
        return positions;
    for (int r = 0; r < _posePositionTable->rowCount (); ++r) {
        std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
        for (int column = 0; column < 3; ++column) {
            bool ok = false;
            position[static_cast< std::size_t > (column)] =
                cellText (_posePositionTable, r, column).toDouble (&ok);
            if (!ok || !std::isfinite (position[static_cast< std::size_t > (column)])) {
                if (error != nullptr) {
                    *error = tr("Pose position row %1 contains an invalid numeric value.")
                                 .arg (r + 1);
                }
                return std::vector< std::array< double, 3 > > ();
            }
        }
        positions.push_back (position);
    }
    return positions;
}

// applyWorkspaceResults:把 WorkspaceSample 数组写到结果表;
//   - 表格最多显示前 500 条(防卡顿),但仍按全部样本统计 summary;
//   - summary 包含总数、无碰撞 / Warning / Fail 计数与平均可操作度。
void KinematicAnalysisWidget::applyWorkspaceResults (const std::vector< WorkspaceSample >& samples)
{
    if (_workspaceTable == NULL)
        return;
    const int rows = static_cast< int > (std::min< std::size_t > (samples.size (), 500));
    _workspaceTable->setRowCount (rows);

    int collisionFree = 0;
    int warnCount = 0;
    int failCount = 0;
    double manipulabilitySum = 0.0;
    for (std::size_t i = 0; i < samples.size (); ++i) {
        const WorkspaceSample& sample = samples[i];
        if (!sample.inCollision)
            ++collisionFree;
        if (sample.status == AnalysisStatus::Warning)
            ++warnCount;
        else if (sample.status == AnalysisStatus::Fail)
            ++failCount;
        manipulabilitySum += sample.manipulability;

        if (i >= static_cast< std::size_t > (rows))
            continue;
        const int row = static_cast< int > (i);
        _workspaceTable->setItem (row, 0, makeItem (QString::number (row)));
        _workspaceTable->setItem (row, 1, makeItem (QString::fromLatin1 (statusText (sample.status))));
        _workspaceTable->setItem (row, 2, makeItem (sample.inCollision ? tr("Yes") : tr("No")));
        _workspaceTable->setItem (row, 3, makeItem (sample.tcpPosition[0]));
        _workspaceTable->setItem (row, 4, makeItem (sample.tcpPosition[1]));
        _workspaceTable->setItem (row, 5, makeItem (sample.tcpPosition[2]));
        _workspaceTable->setItem (row, 6, makeItem (sample.manipulability));
        _workspaceTable->setItem (row, 7, makeItem (sample.minJointLimitMargin));
    }

    const double avgManip = samples.empty () ? 0.0 :
        manipulabilitySum / static_cast< double > (samples.size ());
    if (_workspaceSummaryLabel != NULL) {
        _workspaceSummaryLabel->setText (
            tr("Samples: %1    Collision-free: %2    Warning: %3    Fail: %4    Avg manipulability: %5")
                .arg (static_cast< int > (samples.size ()))
                .arg (collisionFree)
                .arg (warnCount)
                .arg (failCount)
                .arg (QString::number (avgManip, 'g', 6)));
    }
    _workspaceTable->resizeColumnsToContents ();
}

// sampleWorkspace:从控件读 WorkspaceSamplingConfig → 调 analyzer → 写回表格;
// 固定 randomSeed=1 以保证结果可复现,便于回归对比。
void KinematicAnalysisWidget::sampleWorkspace ()
{
    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == NULL) {
        setStatus (tr("Cannot sample workspace: no valid device selected."));
        return;
    }
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();

    WorkspaceSamplingConfig config;
    config.sampleCount = _workspaceSampleCountSpin->value ();
    config.gridStepsPerJoint = _workspaceGridStepsSpin->value ();
    config.mode = _workspaceModeCombo->currentIndex () == 1 ?
        WorkspaceSamplingMode::Grid : WorkspaceSamplingMode::RandomUniform;
    config.checkCollision = _workspaceCollisionCheck->isChecked ();
    config.randomSeed = 1;

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (config.checkCollision, &collisionUnavailable);
    _workspaceSamples = analyzer.sampleWorkspace (
        device, tcpFrame, currentState (), config, collisionDetector);
    applyWorkspaceResults (_workspaceSamples);
    const QString collisionNote = collisionUnavailable ?
        tr(" Collision checking was unavailable.") : QString ();
    setStatus (tr("Workspace sampling completed with %1 sample(s).%2")
                   .arg (static_cast< int > (_workspaceSamples.size ())).arg (collisionNote));
    updateReportSummary ();
}

// exportWorkspaceCsv:把 _workspaceSamples 全量写出(含 q 字符串、TCP 位置、
// manipulability / 关节裕度 / 条件数 / 碰撞 / 状态)。
void KinematicAnalysisWidget::exportWorkspaceCsv ()
{
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export workspace samples"), QString ("workspace_samples.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus (tr("Workspace export canceled."));
        return;
    }
    QFile file (path);
    if (!file.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        return;
    }
    QTextStream out (&file);
    out << "sample_index,q,tcp_x,tcp_y,tcp_z,manipulability,min_joint_limit_margin,condition_number,in_collision,status\n";
    for (std::size_t i = 0; i < _workspaceSamples.size (); ++i) {
        const WorkspaceSample& sample = _workspaceSamples[i];
        out << static_cast< int > (i) << ",\"" << qVectorText (sample.q) << "\","
            << sample.tcpPosition[0] << "," << sample.tcpPosition[1] << ","
            << sample.tcpPosition[2] << "," << sample.manipulability << ","
            << sample.minJointLimitMargin << "," << sample.conditionNumber << ","
            << (sample.inCollision ? "true" : "false") << ","
            << statusText (sample.status) << "\n";
    }
    setStatus (tr("Exported %1 workspace sample(s).")
                   .arg (static_cast< int > (_workspaceSamples.size ())));
}

// addPoseReachabilityRow:在手动位置表末尾追加一行全 0 的位置。
void KinematicAnalysisWidget::addPoseReachabilityRow ()
{
    if (_posePositionTable == NULL)
        return;
    const int row = _posePositionTable->rowCount ();
    _posePositionTable->insertRow (row);
    setCell (_posePositionTable, row, 0, QStringLiteral ("0"), true);
    setCell (_posePositionTable, row, 1, QStringLiteral ("0"), true);
    setCell (_posePositionTable, row, 2, QStringLiteral ("0"), true);
    _posePositionTable->resizeColumnsToContents ();
    setStatus (tr("Added pose reachability position row %1.").arg (row + 1));
}

// applyPoseReachabilityResults:把 PoseReachabilitySample 写到 _poseResultTable,
// 同时刷新顶部 summary(Average coverage)。
void KinematicAnalysisWidget::applyPoseReachabilityResults (
    const std::vector< PoseReachabilitySample >& samples)
{
    if (_poseResultTable == NULL)
        return;
    _poseResultTable->setRowCount (static_cast< int > (samples.size ()));
    double coverageSum = 0.0;
    for (std::size_t i = 0; i < samples.size (); ++i) {
        const PoseReachabilitySample& sample = samples[i];
        coverageSum += sample.coverage;
        const int row = static_cast< int > (i);
        _poseResultTable->setItem (row, 0, makeItem (QString::number (row)));
        _poseResultTable->setItem (row, 1, makeItem (QString::fromLatin1 (statusText (sample.status))));
        _poseResultTable->setItem (row, 2, makeItem (sample.position[0]));
        _poseResultTable->setItem (row, 3, makeItem (sample.position[1]));
        _poseResultTable->setItem (row, 4, makeItem (sample.position[2]));
        _poseResultTable->setItem (row, 5, makeItem (QString::number (sample.sampledDirections)));
        _poseResultTable->setItem (row, 6, makeItem (QString::number (sample.reachableDirections)));
        _poseResultTable->setItem (row, 7, makeItem (sample.coverage));
    }
    const double avgCoverage = samples.empty () ? 0.0 :
        coverageSum / static_cast< double > (samples.size ());
    if (_poseSummaryLabel != NULL) {
        _poseSummaryLabel->setText (tr("Positions: %1    Average coverage: %2")
            .arg (static_cast< int > (samples.size ()))
            .arg (QString::number (avgCoverage, 'f', 3)));
    }
    _poseResultTable->resizeColumnsToContents ();
}

// analyzePoseReachability:从控件收集位置与参数 → 调 analyzer → 写回 _poseReachabilitySamples
// 与结果表;空位置直接给出状态提示。
void KinematicAnalysisWidget::analyzePoseReachability ()
{
    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == NULL) {
        setStatus (tr("Cannot analyze pose reachability: no valid device selected."));
        return;
    }
    QString validationError;
    const std::vector< std::array< double, 3 > > positions =
        collectPoseReachabilityPositions (&validationError);
    if (!validationError.isEmpty ()) {
        setStatus (validationError);
        return;
    }
    if (positions.empty ()) {
        setStatus (tr("Cannot analyze pose reachability: no positions available."));
        return;
    }

    PoseReachabilityConfig config;
    config.directionSamples = _poseDirectionSamplesSpin->value ();
    config.rollSamples      = _poseRollSamplesSpin->value ();
    config.checkCollision   = _poseCollisionCheck->isChecked ();

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (config.checkCollision, &collisionUnavailable);
    _poseReachabilitySamples = analyzer.analyzePoseReachability (
        device, selectedTcpFrame (), currentState (), positions, config, collisionDetector);
    applyPoseReachabilityResults (_poseReachabilitySamples);
    const QString collisionNote = collisionUnavailable ?
        tr(" Collision checking was unavailable.") : QString ();
    setStatus (tr("Pose reachability completed for %1 position(s).%2")
                   .arg (static_cast< int > (_poseReachabilitySamples.size ())).arg (collisionNote));
    updateReportSummary ();
}

// exportPoseReachabilityCsv:把 _poseReachabilitySamples 写为 CSV,
// 列与表格一致(位置 + sampled + reachable + coverage + status)。
void KinematicAnalysisWidget::exportPoseReachabilityCsv ()
{
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export pose reachability"), QString ("pose_reachability.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus (tr("Pose reachability export canceled."));
        return;
    }
    QFile file (path);
    if (!file.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        return;
    }
    QTextStream out (&file);
    out << "position_x,position_y,position_z,sampled_directions,reachable_directions,coverage,status\n";
    for (const PoseReachabilitySample& sample : _poseReachabilitySamples) {
        out << sample.position[0] << "," << sample.position[1] << ","
            << sample.position[2] << "," << sample.sampledDirections << ","
            << sample.reachableDirections << "," << sample.coverage << ","
            << statusText (sample.status) << "\n";
    }
    setStatus (tr("Exported %1 pose reachability row(s).")
                   .arg (static_cast< int > (_poseReachabilitySamples.size ())));
}

// updateReportSummary:Report tab 的中央枢纽。
//   - 用 analyzer.buildAggregateResult 把四类数据聚合成 KinematicAnalysisResult;
//   - 在 summary 标签里显示总状态、可达率、当前条件数 / 可操作度、任务点计数、
//     工作空间总数、位姿可达性平均 coverage;
//   - 把 result.warnings 全部写入告警表。
void KinematicAnalysisWidget::updateReportSummary ()
{
    if (_reportSummaryLabel == NULL)
        return;
    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    const KinematicAnalysisResult result = analyzer.buildAggregateResult (
        _lastCurrentPose, _lastTaskPointResults, _workspaceSamples, _poseReachabilitySamples);

    int taskPass = 0, taskWarn = 0, taskFail = 0;
    for (const TaskPointReachabilityResult& task : _lastTaskPointResults) {
        if (task.status == AnalysisStatus::Pass)
            ++taskPass;
        else if (task.status == AnalysisStatus::Warning)
            ++taskWarn;
        else if (task.status == AnalysisStatus::Fail)
            ++taskFail;
    }
    double poseCoverage = 0.0;
    for (const PoseReachabilitySample& sample : _poseReachabilitySamples)
        poseCoverage += sample.coverage;
    if (!_poseReachabilitySamples.empty ())
        poseCoverage /= static_cast< double > (_poseReachabilitySamples.size ());

    _reportSummaryLabel->setText (
        tr("Status: %1\nReachable rate: %2\nCurrent condition: %3\nCurrent manipulability: %4\nTask points: Pass %5 / Warning %6 / Fail %7\nWorkspace samples: %8\nAverage pose coverage: %9")
            .arg (QString::fromLatin1 (statusText (result.status)))
            .arg (QString::number (result.reachableRate, 'f', 3))
            .arg (QString::number (_lastCurrentPose.conditionNumber, 'g', 6))
            .arg (QString::number (_lastCurrentPose.manipulability, 'g', 6))
            .arg (taskPass).arg (taskWarn).arg (taskFail)
            .arg (static_cast< int > (_workspaceSamples.size ()))
            .arg (QString::number (poseCoverage, 'f', 3)));

    if (_reportWarningTable != NULL) {
        _reportWarningTable->setRowCount (static_cast< int > (result.warnings.size ()));
        for (std::size_t i = 0; i < result.warnings.size (); ++i) {
            const AnalysisWarning& warning = result.warnings[i];
            const int row = static_cast< int > (i);
            _reportWarningTable->setItem (row, 0, makeItem (QString::fromLatin1 (statusText (warning.severity))));
            _reportWarningTable->setItem (row, 1, makeItem (QString::fromStdString (warning.code)));
            _reportWarningTable->setItem (row, 2, makeItem (QString::fromStdString (warning.source)));
            _reportWarningTable->setItem (row, 3, makeItem (QString::fromStdString (warning.message)));
        }
        _reportWarningTable->resizeColumnsToContents ();
    }
}

// refreshReport:Report tab 上的 Refresh 按钮槽函数;
// 重新跑一次 updateReportSummary,方便用户修改阈值后只刷一次面板而不重跑分析。
void KinematicAnalysisWidget::refreshReport ()
{
    updateReportSummary ();
    setStatus (tr("Kinematic report refreshed."));
}

namespace {
// vectorToJsonArray / array3ToJsonArray:把 std::vector<double> 与 std::array<double,3>
// 装进 QJsonArray;供 exportReportJson 使用。
QJsonArray vectorToJsonArray (const std::vector< double >& values)
{
    QJsonArray array;
    for (double value : values)
        array.append (value);
    return array;
}

QJsonArray array3ToJsonArray (const std::array< double, 3 >& values)
{
    QJsonArray array;
    array.append (values[0]);
    array.append (values[1]);
    array.append (values[2]);
    return array;
}
}    // namespace

// exportReportJson:把 KinematicAnalysisResult 序列化为结构化 JSON,便于下游工具消费。
// 顶层包含 pluginName / status / reachableRate;四个子节点分别为 currentPose、
// taskPointResults、workspaceSamples、poseReachability;末尾是 warnings 数组。
void KinematicAnalysisWidget::exportReportJson ()
{
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export kinematic report"), QString ("kinematic_report.json"),
        tr("JSON files (*.json);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus (tr("Report JSON export canceled."));
        return;
    }

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    const KinematicAnalysisResult result = analyzer.buildAggregateResult (
        _lastCurrentPose, _lastTaskPointResults, _workspaceSamples, _poseReachabilitySamples);

    QJsonObject root;
    root["pluginName"] = QString::fromStdString (result.header.pluginName);
    root["status"] = QString::fromLatin1 (statusText (result.status));
    root["reachableRate"] = result.reachableRate;

    QJsonObject current;
    current["status"] = QString::fromLatin1 (statusText (result.currentPose.status));
    current["q"] = vectorToJsonArray (result.currentPose.q);
    current["tcpPosition"] = array3ToJsonArray (result.currentPose.tcpPosition);
    current["tcpRpyDeg"] = array3ToJsonArray (result.currentPose.tcpRpyDeg);
    current["conditionNumber"] = result.currentPose.conditionNumber;
    current["manipulability"] = result.currentPose.manipulability;
    root["currentPose"] = current;

    QJsonArray taskArray;
    for (const TaskPointReachabilityResult& task : result.taskPointResults) {
        QJsonObject item;
        item["id"] = QString::fromStdString (task.taskPoint.id);
        item["name"] = QString::fromStdString (task.taskPoint.name);
        item["status"] = QString::fromLatin1 (statusText (task.status));
        item["primaryFailure"] = QString::fromLatin1 (toString (task.primaryFailure));
        taskArray.append (item);
    }
    root["taskPointResults"] = taskArray;

    QJsonArray workspaceArray;
    for (const WorkspaceSample& sample : result.workspaceSamples) {
        QJsonObject item;
        item["q"] = vectorToJsonArray (sample.q);
        item["tcpPosition"] = array3ToJsonArray (sample.tcpPosition);
        item["manipulability"] = sample.manipulability;
        item["minJointLimitMargin"] = sample.minJointLimitMargin;
        item["conditionNumber"] = sample.conditionNumber;
        item["inCollision"] = sample.inCollision;
        item["status"] = QString::fromLatin1 (statusText (sample.status));
        workspaceArray.append (item);
    }
    root["workspaceSamples"] = workspaceArray;

    QJsonArray poseArray;
    for (const PoseReachabilitySample& sample : result.poseReachability) {
        QJsonObject item;
        item["position"] = array3ToJsonArray (sample.position);
        item["sampledDirections"] = sample.sampledDirections;
        item["reachableDirections"] = sample.reachableDirections;
        item["coverage"] = sample.coverage;
        item["status"] = QString::fromLatin1 (statusText (sample.status));
        poseArray.append (item);
    }
    root["poseReachability"] = poseArray;

    QJsonArray warningArray;
    for (const AnalysisWarning& warning : result.warnings) {
        QJsonObject item;
        item["code"] = QString::fromStdString (warning.code);
        item["message"] = QString::fromStdString (warning.message);
        item["source"] = QString::fromStdString (warning.source);
        item["severity"] = QString::fromLatin1 (statusText (warning.severity));
        warningArray.append (item);
    }
    root["warnings"] = warningArray;

    QFile file (path);
    if (!file.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        return;
    }
    file.write (QJsonDocument (root).toJson (QJsonDocument::Indented));
    setStatus (tr("Exported kinematic JSON report."));
}

// exportReportCsv:输出 "metric,value" 两列的精简摘要 CSV。
//   - 顶层状态 / 可达率 / 当前位姿关键指标 / 各子结果条数;
//   - 可操作度 min/max/mean/p10 一并追加(来自 result.manipulabilityMap)。
void KinematicAnalysisWidget::exportReportCsv ()
{
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export kinematic summary"), QString ("kinematic_summary.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus (tr("Report CSV export canceled."));
        return;
    }
    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    const KinematicAnalysisResult result = analyzer.buildAggregateResult (
        _lastCurrentPose, _lastTaskPointResults, _workspaceSamples, _poseReachabilitySamples);

    QFile file (path);
    if (!file.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        return;
    }
    QTextStream out (&file);
    out << "metric,value\n";
    out << "status," << statusText (result.status) << "\n";
    out << "reachable_rate," << result.reachableRate << "\n";
    out << "current_condition," << result.currentPose.conditionNumber << "\n";
    out << "current_manipulability," << result.currentPose.manipulability << "\n";
    out << "task_point_results," << static_cast< int > (result.taskPointResults.size ()) << "\n";
    out << "workspace_samples," << static_cast< int > (result.workspaceSamples.size ()) << "\n";
    out << "pose_reachability_positions," << static_cast< int > (result.poseReachability.size ()) << "\n";
    for (const MetricValue& metric : result.manipulabilityMap)
        out << QString::fromStdString (metric.name) << "," << metric.value << "\n";
    setStatus (tr("Exported kinematic CSV summary."));
}

// applyThresholds:把 Report tab 上的 7 个阈值 SpinBox 写回 _thresholds,
// 仅修改内存中的阈值;不会主动重跑任何分析,提示用户按需重跑。
void KinematicAnalysisWidget::applyThresholds ()
{
    _thresholds.nearJointLimitRatio = _thresholdNearLimitSpin->value ();
    _thresholds.conditionWarning = _thresholdConditionWarningSpin->value ();
    _thresholds.conditionFail = _thresholdConditionFailSpin->value ();
    _thresholds.singularValueWarning = _thresholdSingularValueSpin->value ();
    _thresholds.manipulabilityWarning = _thresholdManipulabilitySpin->value ();
    _thresholds.positionToleranceMeters = _thresholdPositionToleranceSpin->value ();
    _thresholds.orientationToleranceDeg = _thresholdOrientationToleranceSpin->value ();
    setStatus (tr("Kinematic thresholds updated. Re-run analyses to refresh results."));
}
