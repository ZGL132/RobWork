#include "KinematicAnalysisWidget.hpp"
#include "KinematicThresholdsDialog.hpp"

#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalyzer.hpp"
#include "TaskPointResolver.hpp"
#include "TaskPointUiLogic.hpp"
#include "TaskPointTableModel.hpp"
#include "TaskPointDelegates.hpp"
#include "KinematicAnalysisPlotWidget.hpp"
#include "KinematicPlotDialog.hpp"
#include "KinematicAnalysisVisualizationTypes.hpp"
#include "KinematicAnalysisWorkspace.hpp"
#include "KinematicAnalysisPoseReachability.hpp"
#include "KinematicAnalysisCollision.hpp"
#include "KinematicAnalysisJson.hpp"
#include "KinematicAnalysisProjectDocument.hpp"
#include "KinematicAnalysisEnvelope.hpp"
#include "KinematicAnalysisUiLogic.hpp"
#include "FrozenRequirementKinematicAdapter.hpp"
#include "KinematicAnalysisContext.hpp"
#include "RegionCoverageEvaluator.hpp"
#include "KinematicAnalysisReportJson.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>

// 共享的 CSV / JSON 序列化工具,TaskPoint 与本插件复用了它。
#include <rwslibs/robotanalysiscore/RobotAnalysisCsv.hpp>
#include <rwslibs/robotanalysiscore/RobotAnalysisValidation.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>

#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/RPY.hpp>
#include <rws/RobWorkStudio.hpp>

#include <boost/bind/bind.hpp>

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableView>
#include <QToolButton>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QHeaderView>
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

// WorkspaceEnvelopeCacheKey 的相等比较:仅当设备 / TCP 指针、投影平面、方向数、
// 坐标迭代数以及关节上下限全部相同时才视为缓存命中;任一参数变化都会导致
// 包络形状可能改变,必须判定为未命中并重新计算。
bool rws::WorkspaceEnvelopeCacheKey::operator== (
    const WorkspaceEnvelopeCacheKey& other) const
{
    return device == other.device &&
        tcpFrame == other.tcpFrame &&
        projection == other.projection &&
        angularDirections == other.angularDirections &&
        coordinateIterations == other.coordinateIterations &&
        lowerBounds == other.lowerBounds &&
        upperBounds == other.upperBounds;
}

namespace {
// Task point 表格列索引枚举,统一所有读写代码,避免列号硬编码漂移。
// 前 19 列对应 RobotAnalysisCsv 标准字段 + UI 衍生 status / reason;
// 后 8 列是 P1 新增的批量 IK 结果列(raw / usable / bestQ / posErr /
// oriErr / margin / condition / collision)。
// 该枚举必须在 buildTaskPointTab 之前定义,否则 setColumnCount 与
// setHorizontalHeaderLabels 引用 Col* 时会编译失败。
enum TaskPointColumn
{
    ColEnabled   = 0,
    ColId        = 1,
    ColName      = 2,
    ColType      = 3,
    ColRefFrame  = 4,
    ColTcpFrame  = 5,
    ColX         = 6,
    ColY         = 7,
    ColZ         = 8,
    ColRoll      = 9,
    ColPitch     = 10,
    ColYaw       = 11,
    ColPosTol    = 12,
    ColOriTol    = 13,
    ColFreeRoll  = 14,
    ColWeight    = 15,
    ColNote      = 16,
    ColStatus    = 17,
    ColReason    = 18,
    // P1:批量 IK 结果列
    ColRawCandidates       = 19,
    ColUsableSolutions     = 20,
    ColBestQ               = 21,
    ColPositionError       = 22,
    ColOrientationError    = 23,
    ColMinMargin           = 24,
    ColCondition           = 25,
    ColCollision           = 26,
    TaskPointColumnCount   = 27
};

// ComboBoxDelegate:把表格单元格的编辑行为替换为只读下拉框,选项在构造时固定。
// 用于 refFrame / tcpFrame 这类离散枚举字段,避免手输拼写错误导致解析失败。
class ComboBoxDelegate : public QStyledItemDelegate
{
  public:
    ComboBoxDelegate (const QStringList& values, QObject* parent = nullptr) :
        QStyledItemDelegate (parent), _values (values)
    {}

    QWidget* createEditor (QWidget* parent, const QStyleOptionViewItem&,
                           const QModelIndex&) const override
    {
        QComboBox* editor = new QComboBox (parent);
        editor->addItems (_values);
        editor->setEditable (false);
        return editor;
    }

    void setEditorData (QWidget* editor, const QModelIndex& index) const override
    {
        QComboBox* combo = qobject_cast< QComboBox* > (editor);
        if (combo == nullptr)
            return;
        const QString value = index.data (Qt::EditRole).toString ();
        const int idx = combo->findText (value);
        combo->setCurrentIndex (idx >= 0 ? idx : 0);
    }

    void setModelData (QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index) const override
    {
        QComboBox* combo = qobject_cast< QComboBox* > (editor);
        if (combo != nullptr)
            model->setData (index, combo->currentText (), Qt::EditRole);
    }

  private:
    QStringList _values;
};

// DoubleSpinDelegate:把表格单元格的编辑行为替换为带范围 / 小数位 / 步进的数值输入框,
// 用于位置、容差等连续数值字段。setKeyboardTracking(false) 避免输入过程中过度触发
// valueChanged 信号造成闪烁或重复计算。
class DoubleSpinDelegate : public QStyledItemDelegate
{
  public:
    DoubleSpinDelegate (double minimum, double maximum, int decimals,
                        double step, QObject* parent = nullptr) :
        QStyledItemDelegate (parent),
        _minimum (minimum),
        _maximum (maximum),
        _decimals (decimals),
        _step (step)
    {}

    QWidget* createEditor (QWidget* parent, const QStyleOptionViewItem&,
                           const QModelIndex&) const override
    {
        QDoubleSpinBox* editor = new QDoubleSpinBox (parent);
        editor->setRange (_minimum, _maximum);
        editor->setDecimals (_decimals);
        editor->setSingleStep (_step);
        editor->setKeyboardTracking (false);
        return editor;
    }

    void setEditorData (QWidget* editor, const QModelIndex& index) const override
    {
        QDoubleSpinBox* spin = qobject_cast< QDoubleSpinBox* > (editor);
        if (spin != nullptr)
            spin->setValue (index.data (Qt::EditRole).toDouble ());
    }

    void setModelData (QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index) const override
    {
        QDoubleSpinBox* spin = qobject_cast< QDoubleSpinBox* > (editor);
        if (spin != nullptr)
            model->setData (index, QString::number (spin->value (), 'g', 12), Qt::EditRole);
    }

  private:
    double _minimum;
    double _maximum;
    int _decimals;
    double _step;
};

// replaceColumnDelegate:替换某列的 item delegate。若旧 delegate 由本表持有
// (parent == table)则用 deleteLater 延迟销毁,避免重复安装 delegate 时内存泄漏,
// 也避免在 Qt 事件循环仍可能引用旧 delegate 期间直接 delete。
void replaceColumnDelegate (QTableWidget* table, int column, QAbstractItemDelegate* delegate)
{
    if (table == nullptr)
        return;
    QAbstractItemDelegate* oldDelegate = table->itemDelegateForColumn (column);
    table->setItemDelegateForColumn (column, delegate);
    if (oldDelegate != nullptr && oldDelegate->parent () == table)
        oldDelegate->deleteLater ();
}

QTableWidgetItem* makeItem (const QString& text);
QTableWidgetItem* makeItem (double v);
const char* statusText (rws::AnalysisStatus status);
QString qVectorText (const std::vector< double >& q);
QString targetResidualText (const rws::TargetEvaluation& result);
QString targetPoseCoverageText (const rws::TargetEvaluation& result);
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
//   - Three mode tabs switch scrollable workflow pages;
//   - 底部一个只读状态条用于反馈用户操作结果;
//   - 末尾把所有按钮的 clicked() 信号连到对应的槽函数。
KinematicAnalysisWidget::KinematicAnalysisWidget(QWidget* parent) :
    QWidget(parent),
    _studio(NULL),
    _workcell(NULL),
    _modeTabs(NULL),
    _modeStack(NULL),
    _diagnoseScroll(NULL),
    _exploreScroll(NULL),
    _diagnoseWorkflowPage(NULL),
    _validateWorkflowPage(NULL),
    _exploreWorkflowPage(NULL),
    _mode2DataSourceCombo(NULL),
    _mode2LoadJsonButton(NULL),
    _mode2ValidateAllButton(NULL),
    _mode2ValidateSelectedButton(NULL),
    _mode2AddButton(NULL),
    _mode2RemoveButton(NULL),
    _validateLoadRequirementsButton(NULL),
    _validateRunButton(NULL),
    _validateExportButton(NULL),
    _validateRequirementStateLabel(NULL),
    _validateTaskResultTable(NULL),
    _validateRegionSummaryTable(NULL),
    _validateRegionCellTable(NULL),
    _validateDiagnosticsToggle(NULL),
    _validateDiagnosticsContent(NULL),
    _validateProvenanceLabel(NULL),
    _validateTaskSectionTitle(NULL),
    _validateRegionSectionTitle(NULL),
    _validateOrientationProbeLabel(NULL),
    _exploreRunButton(NULL),
    _exploreCancelButton(NULL),
    _exploreSamplesSpin(NULL),
    _exploreModeCombo(NULL),
    _exploreSeedSpin(NULL),
    _exploreGridStepsSpin(NULL),
    _exploreDirectionSamplesSpin(NULL),
    _exploreRollSamplesSpin(NULL),
    _exploreSamplesLabel(NULL),
    _exploreSeedLabel(NULL),
    _exploreGridStepsLabel(NULL),
    _exploreDirectionsLabel(NULL),
    _exploreRollsLabel(NULL),
    _exploreStateLabel(NULL),
    _exploreRunActive(false),
    _exploreCancellationRequested(false),
    _exploreWatcher (new QFutureWatcher< std::vector< WorkspaceSample > > (this)),
    _exploreCancelToken (std::make_shared< std::atomic_bool > (false)),
    _exploreCompletedSamples (0),
    _explorePlannedSamples (0),
    _workcellSessionGeneration (0),
    _currentPoseTab(NULL),
    _ikTab(NULL),
    _taskPointTab(NULL),
    _workspaceTab(NULL),
    _poseReachTab(NULL),
    _visualizationTab(NULL),
    _reportTab(NULL),
    _deviceCombo(NULL),
    _tcpFrameCombo(NULL),
    _refreshCurrentPoseButton(NULL),
    _thresholdSettingsButton(NULL),
    _reportButton(NULL),
    _status(NULL),
    _currentTcpValueLabels{{NULL, NULL, NULL, NULL, NULL, NULL}},
    _poseIndicatorLabel(NULL),
    _poseConditionLabel(NULL),
    _poseManipulabilityLabel(NULL),
    _poseMarginLabel(NULL),
    _poseCollisionCapabilityLabel(NULL),
    _jointStatusTable(NULL),
    _advancedDiagnosticsToggle(NULL),
    _advancedDiagnosticsContent(NULL),
    _jacobianTable(NULL),
    _singularTable(NULL),
    _warningLabel(NULL),
    _ikXSpin(NULL),
    _ikYSpin(NULL),
    _ikZSpin(NULL),
    _ikRollSpin(NULL),
    _ikPitchSpin(NULL),
    _ikYawSpin(NULL),
    _ikDuplicateQThresholdSpin(NULL),
    _ikCollisionCheck(NULL),
    _lengthUnitCombo(NULL),
    _angleUnitCombo(NULL),
    _ikImportCurrentPoseButton(NULL),
    _ikSolveButton(NULL),
    _ikApplyButton(NULL),
    _ikSourceLabel(NULL),
    _ikStatusLabel(NULL),
    _ikDisplayedLabel(NULL),
    _ikUsableLabel(NULL),
    _ikPassLabel(NULL),
    _ikWarningLabel(NULL),
    _ikFailLabel(NULL),
    _ikCandidateFilterCombo(NULL),
    _ikSolutionTable(NULL),
    _ikDetailTable(NULL),
    _ikResultStale(false),
    _taskPointTable(NULL),
    _taskPointModel(nullptr),
    _addTaskPointButton(NULL),
    _removeTaskPointButton(NULL),
    _importTaskPointsButton(NULL),
    _importFrozenRequirementsButton(NULL),
    _exportTaskPointsButton(NULL),
    _analyzeAllTaskPointsButton(NULL),
    // P2:Task points 专用按钮(NULL 守卫,见析构 / 析构期清理)
    _analyzeSelectedTaskPointsButton(NULL),
    _importCurrentTcpTaskPointButton(NULL),
    _applySelectedTaskPointBestQButton(NULL),
    _openSelectedTaskPointInIkButton(NULL),
    _taskPointSummaryLabel(NULL),
    _taskPointSelectedPanel(NULL),
    _workspaceSampleCountSpin(NULL),
    _workspaceGridStepsSpin(NULL),
    _workspaceSeedSpin(NULL),
    _workspaceModeCombo(NULL),
    _workspaceCollisionCheck(NULL),
    _workspaceColorModeCombo(NULL),
    _workspaceRunButton(NULL),
    _workspaceExportButton(NULL),
    _workspaceOpenVisualizationButton(NULL),
    _workspaceCancelButton(NULL),
    _workspaceWatcher (new QFutureWatcher< std::vector< WorkspaceSample > > (this)),
    _workspaceRunActive (false),
    _workspaceCollisionUnavailable (false),
    _workspaceCancelRequested (std::make_shared< std::atomic_bool > (false)),
    _workspaceProgressBar (NULL),
    _workspaceProgressLabel (NULL),
    _workspaceSampleCountLabel(NULL),
    _workspaceCollisionFreeLabel(NULL),
    _workspacePassLabel(NULL),
    _workspaceWarningLabel(NULL),
    _workspaceFailLabel(NULL),
    _workspaceAvgManipulabilityLabel(NULL),
    _workspaceDiagnosticsLabel(NULL),
    _workspaceTable(NULL),
    _workspaceDetailTable(NULL),
    _poseDirectionSamplesSpin(NULL),
    _poseRollSamplesSpin(NULL),
    _poseCollisionCheck(NULL),
    _poseAddRowButton(NULL),
    _poseAnalyzeButton(NULL),
    _poseExportButton(NULL),
    _poseCancelButton(NULL),
    _poseOpenVisualizationButton(NULL),
    _poseReachabilityWatcher(NULL),
    _poseReachabilityRunActive(false),
    _poseReachabilityCollisionUnavailable(false),
    _poseReachabilityCancelRequested(std::make_shared< std::atomic_bool > (false)),
    _poseTaskPointsSourceButton(NULL),
    _poseManualSourceButton(NULL),
    _poseRemoveRowButton(NULL),
    _posePositionCountLabel(NULL),
    _poseReachableLabel(NULL),
    _poseCoverageLabel(NULL),
    _posePassLabel(NULL),
    _poseWarningLabel(NULL),
    _poseFailLabel(NULL),
    _poseDiagnosticsLabel(NULL),
    _poseRunDetailsLabel(NULL),
    _poseManualPositionsPanel(NULL),
    _poseMoreToggle(NULL),
    _poseProgressBar(NULL),
    _poseProgressLabel(NULL),
    _posePositionTable(NULL),
    _poseResultTable(NULL),
    _visualSourceCombo(NULL),
    _visualProjectionCombo(NULL),
    _visualColorModeCombo(NULL),
    _visualRenderModeCombo(NULL),
    _visualEnvelopeDirectionsSpin(NULL),
    _visualShowPassCheck(NULL),
    _visualShowWarningCheck(NULL),
    _visualShowFailCheck(NULL),
    _visualShowUnknownCheck(NULL),
    _visualShowLabelsCheck(NULL),
    _visualShowGridCheck(NULL),
    _visualShowLegendCheck(NULL),
    _visualPointSizeSpin(NULL),
    _visualResetViewButton(NULL),
    _visualExportPngButton(NULL),
    _visualOpenDialogButton(NULL),
    _visualSummaryLabel(NULL),
    _visualPlot(NULL),
    _envelopeWatcher(NULL),
    _envelopeGeneration(0),
    _envelopeDebounceTimer(NULL),
    _envelopeRunActive(false),
    _envelopeCancelRequested(std::make_shared< std::atomic_bool > (false)),
    _envelopeCacheValid(false),
    _reportSummaryLabel(NULL),
    _reportWarningTable(NULL),
    _reportRefreshButton(NULL),
    _reportExportJsonButton(NULL),
    _reportExportCsvButton(NULL),
    _reportStageFilterCombo(NULL),
    _reportFeasibilityFilterCombo(NULL),
    _reportQualityFilterCombo(NULL),
    _reportFailureFilterCombo(NULL),
    _reportRegionFilterEdit(NULL),
    _thresholdNearLimitSpin(NULL),
    _thresholdConditionWarningSpin(NULL),
    _thresholdConditionFailSpin(NULL),
    _thresholdSingularValueSpin(NULL),
    _thresholdManipulabilitySpin(NULL),
    _thresholdPositionToleranceSpin(NULL),
    _thresholdOrientationToleranceSpin(NULL),
    _thresholdApplyButton(NULL),
    _thresholds(),
    _lengthUnit(KinematicLengthUnit::Meters),
    _angleUnit(KinematicAngleUnit::Degrees),
    _lastCurrentPose(),
    _lastIkResult(),
    _lastTaskPointResults(),
    _workspaceSamples(),
    _poseReachabilitySamples(),
    _validateExecution(),
    _validateSummary(),
    _validateExecutionSet(false),
    _validateHasResults(false)
{
    Q_UNUSED (parent);
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setSizeConstraint (QLayout::SetNoConstraint);
    root->setContentsMargins (4, 4, 4, 4);

    QWidget* headerWidget = new QWidget(this);
    QVBoxLayout* header = new QVBoxLayout (headerWidget);
    header->setContentsMargins (0, 0, 0, 0);
    header->setSpacing (4);
    QGridLayout* contextRow = new QGridLayout ();
    contextRow->setContentsMargins (0, 0, 0, 0);
    contextRow->setHorizontalSpacing (4);
    contextRow->setColumnStretch (1, 1);
    contextRow->setColumnStretch (3, 1);
    QHBoxLayout* actionRow = new QHBoxLayout ();
    actionRow->setContentsMargins (0, 0, 0, 0);
    actionRow->setSpacing (4);
    _deviceCombo = new QComboBox(headerWidget);
    _tcpFrameCombo = new QComboBox(headerWidget);
    _deviceCombo->setObjectName (QStringLiteral ("deviceCombo"));
    _tcpFrameCombo->setObjectName (QStringLiteral ("tcpFrameCombo"));
    _lengthUnitCombo = new QComboBox(headerWidget);
    _angleUnitCombo = new QComboBox(headerWidget);
    _lengthUnitCombo->setObjectName (QStringLiteral ("lengthUnitCombo"));
    _angleUnitCombo->setObjectName (QStringLiteral ("angleUnitCombo"));
    _lengthUnitCombo->addItem(tr("Meters"), static_cast<int>(KinematicLengthUnit::Meters));
    _lengthUnitCombo->addItem(tr("Centimeters"), static_cast<int>(KinematicLengthUnit::Centimeters));
    _lengthUnitCombo->addItem(tr("Millimeters"), static_cast<int>(KinematicLengthUnit::Millimeters));
    _lengthUnitCombo->addItem(tr("Inches"), static_cast<int>(KinematicLengthUnit::Inches));
    _angleUnitCombo->addItem(tr("Degrees"), static_cast<int>(KinematicAngleUnit::Degrees));
    _angleUnitCombo->addItem(tr("Radians"), static_cast<int>(KinematicAngleUnit::Radians));
    _angleUnitCombo->addItem(tr("Grads"), static_cast<int>(KinematicAngleUnit::Grads));
    _angleUnitCombo->addItem(tr("Turns"), static_cast<int>(KinematicAngleUnit::Turns));
    _refreshCurrentPoseButton = new QPushButton(tr("Refresh"), headerWidget);
    _refreshCurrentPoseButton->setObjectName (
        QStringLiteral ("diagnoseRefreshButton"));
    _thresholdSettingsButton = new QPushButton (tr("Thresholds"), headerWidget);
    _thresholdSettingsButton->setObjectName (QStringLiteral ("thresholdSettingsButton"));
    _thresholdSettingsButton->setToolTip (tr("Edit kinematic analysis thresholds."));
    _reportButton = new QPushButton (tr("Report"), headerWidget);
    _reportButton->setObjectName (QStringLiteral ("reportButton"));
    _reportButton->setToolTip (tr("Report actions."));
    QMenu* reportMenu = new QMenu (_reportButton);
    QAction* refreshReportAction = reportMenu->addAction (tr ("Refresh report"));
    QAction* exportJsonAction = reportMenu->addAction (tr ("Export JSON"));
    QAction* exportSummaryAction = reportMenu->addAction (tr ("Export summary CSV"));
    QAction* exportTaskResultsAction = reportMenu->addAction (tr ("Export task-results CSV"));
    connect (refreshReportAction, &QAction::triggered, this,
             &KinematicAnalysisWidget::refreshReport);
    connect (exportJsonAction, &QAction::triggered, this,
             &KinematicAnalysisWidget::exportReportJson);
    connect (exportSummaryAction, &QAction::triggered, this,
             &KinematicAnalysisWidget::exportReportCsv);
    connect (exportTaskResultsAction, &QAction::triggered, this,
             &KinematicAnalysisWidget::exportTaskPointResultsCsv);
    _reportButton->setMenu (reportMenu);
    _deviceCombo->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _tcpFrameCombo->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _deviceCombo->setMinimumWidth (0);
    _tcpFrameCombo->setMinimumWidth (0);
    _deviceCombo->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
    _tcpFrameCombo->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
    _lengthUnitCombo->setMinimumWidth (0);
    _angleUnitCombo->setMinimumWidth (0);
    _lengthUnitCombo->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
    _angleUnitCombo->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (QComboBox* combo : {_deviceCombo, _tcpFrameCombo,
                             _lengthUnitCombo, _angleUnitCombo}) {
        combo->setToolTip (combo->currentText ());
        connect (combo, &QComboBox::currentTextChanged, combo,
                 [combo] (const QString& text) { combo->setToolTip (text); });
    }
    contextRow->addWidget (new QLabel (tr ("Device:"), headerWidget), 0, 0);
    contextRow->addWidget (_deviceCombo, 0, 1);
    contextRow->addWidget (new QLabel (tr ("TCP:"), headerWidget), 0, 2);
    contextRow->addWidget (_tcpFrameCombo, 0, 3);
    actionRow->addWidget (_lengthUnitCombo);
    actionRow->addWidget (_angleUnitCombo);
    actionRow->addWidget (_refreshCurrentPoseButton);
    actionRow->addWidget (_thresholdSettingsButton);
    actionRow->addWidget (_reportButton);
    header->addLayout (contextRow);
    header->addLayout (actionRow);
    root->addWidget (headerWidget);

    // 三模式工作流外壳:Diagnose(当前位姿诊断)/ Validate(冻结需求校验)/
    // Explore(能力探索)互斥切换,替代原 QTabWidget。每个模式页由独立
    // QScrollArea 承载(addModePage),避免页面内容过高时超出 Dock 可视区。
    _modeTabs = new QTabBar (this);
    _modeTabs->setObjectName (QStringLiteral ("kinematicModeTabs"));
    _modeTabs->setExpanding (true);
    _modeTabs->setUsesScrollButtons (false);
    _modeTabs->setStyleSheet (QStringLiteral (
        "QTabBar::tab { padding: 4px 10px; min-height: 22px; "
        "border: 1px solid palette(mid); border-bottom: none; "
        "background: palette(button); color: palette(button-text); }"
        "QTabBar::tab:selected { background: palette(base); "
        "color: palette(text); font-weight: bold; }"
        "QTabBar::tab:hover { background: palette(alternate-base); }"));
    const QStringList modeNames = {tr("Diagnose"), tr("Validate"), tr("Explore")};
    const QStringList modeDescriptions = {tr("Diagnose"), tr("Validate Requirements"),
                                          tr("Explore Capability")};
    for (int index = 0; index < modeNames.size (); ++index) {
        _modeTabs->addTab (modeNames.at (index));
        _modeTabs->setTabToolTip (index, modeDescriptions.at (index));
        _modeTabs->setAccessibleTabName (index, modeDescriptions.at (index));
    }
    root->addWidget (_modeTabs);

    _modeStack = new QStackedWidget (this);
    _modeStack->setObjectName (QStringLiteral ("kinematicModeStack"));
    root->addWidget (_modeStack, 1);
    connect (_modeTabs, &QTabBar::currentChanged, _modeStack,
             &QStackedWidget::setCurrentIndex);

    _diagnoseWorkflowPage = new QWidget ();
    _diagnoseWorkflowPage->setObjectName (QStringLiteral ("diagnoseWorkflowPage"));
    _validateWorkflowPage = new QWidget ();
    _exploreWorkflowPage = new QWidget ();

    // -------------------- Current Pose Tab --------------------
    // 单列全宽密集布局(从上到下):
    //   1. 共享 Pose / IK target 六行网格 + 1 行关键指标;
    //   2. 关节状态合并表 — Joint | q | Limit margin | Status;
    //   3. Jacobian 全宽主表(行 vx/vy/vz/wx/wy/wz,列 q0..qn);
    //   4. Singular values 横向小表(1 行);
    //   5. Warnings 默认压成单行 \"Warnings: None\"。
    _currentPoseTab = new QWidget(_diagnoseWorkflowPage);
    _currentPoseTab->setObjectName (QStringLiteral ("currentPoseTab"));
    QVBoxLayout* cpLayout = new QVBoxLayout(_currentPoseTab);
    QWidget* poseIkSection = new QWidget (_currentPoseTab);
    poseIkSection->setObjectName (QStringLiteral ("poseIkSection"));
    QVBoxLayout* poseIkLayout = new QVBoxLayout (poseIkSection);
    poseIkLayout->setContentsMargins (0, 0, 0, 0);
    QHBoxLayout* ikPoseTitleRow = new QHBoxLayout ();
    QLabel* ikPoseTitle = new QLabel (tr("Pose / IK target"), poseIkSection);
    ikPoseTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    ikPoseTitleRow->addWidget (ikPoseTitle);
    ikPoseTitleRow->addStretch (1);
    poseIkLayout->addLayout (ikPoseTitleRow);
    QGridLayout* ikPoseGrid = new QGridLayout ();
    ikPoseGrid->setContentsMargins (0, 0, 0, 0);
    ikPoseGrid->setHorizontalSpacing (4);
    ikPoseGrid->setVerticalSpacing (2);
    ikPoseGrid->setColumnMinimumWidth (0, 34);
    ikPoseGrid->setColumnMinimumWidth (1, 96);
    ikPoseGrid->setColumnStretch (2, 1);
    ikPoseGrid->addWidget (new QLabel (QString (), poseIkSection), 0, 0);
    QLabel* currentTcpHeader = new QLabel (tr("Current TCP"), poseIkSection);
    QLabel* ikTargetHeader = new QLabel (tr("IK target"), poseIkSection);
    currentTcpHeader->setAlignment (Qt::AlignCenter);
    ikTargetHeader->setAlignment (Qt::AlignCenter);
    currentTcpHeader->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
    ikTargetHeader->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
    ikPoseGrid->addWidget (currentTcpHeader, 0, 1);
    ikPoseGrid->addWidget (ikTargetHeader, 0, 2);
    const QStringList poseLabels = {
        QStringLiteral ("x"), QStringLiteral ("y"), QStringLiteral ("z"),
        QStringLiteral ("Rx"), QStringLiteral ("Ry"), QStringLiteral ("Rz")};
    const QStringList currentTcpNames = {
        QStringLiteral ("currentTcpXLabel"), QStringLiteral ("currentTcpYLabel"),
        QStringLiteral ("currentTcpZLabel"), QStringLiteral ("currentTcpRollLabel"),
        QStringLiteral ("currentTcpPitchLabel"), QStringLiteral ("currentTcpYawLabel")};
    const QStringList poseAxisNames = {
        QStringLiteral ("poseAxisXLabel"), QStringLiteral ("poseAxisYLabel"),
        QStringLiteral ("poseAxisZLabel"), QStringLiteral ("poseAxisRxLabel"),
        QStringLiteral ("poseAxisRyLabel"), QStringLiteral ("poseAxisRzLabel")};
    for (int index = 0; index < currentTcpNames.size (); ++index) {
        QLabel* rowLabel = new QLabel (poseLabels.at (index), poseIkSection);
        rowLabel->setObjectName (poseAxisNames.at (index));
        rowLabel->setMinimumWidth (0);
        rowLabel->setAlignment (Qt::AlignLeft | Qt::AlignVCenter);
        rowLabel->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Preferred);
        QLabel* currentTcp = new QLabel (QStringLiteral ("-"), poseIkSection);
        currentTcp->setObjectName (currentTcpNames.at (index));
        currentTcp->setAlignment (Qt::AlignCenter);
        currentTcp->setMinimumWidth (0);
        currentTcp->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Preferred);
        _currentTcpValueLabels[static_cast< std::size_t > (index)] = currentTcp;
        ikPoseGrid->addWidget (rowLabel, index + 1, 0);
        ikPoseGrid->addWidget (currentTcp, index + 1, 1);
    }
    poseIkLayout->addLayout (ikPoseGrid);
    cpLayout->addWidget (poseIkSection);

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

    // ---- Health summary: five scan-friendly metrics without another data table. ----
    cpLayout->addWidget (new QLabel (tr("Health summary"), _currentPoseTab));
    QFrame* healthFrame = new QFrame (_currentPoseTab);
    healthFrame->setObjectName (QStringLiteral ("currentPoseHealthFrame"));
    healthFrame->setFrameShape (QFrame::StyledPanel);
    QHBoxLayout* healthLayout = new QHBoxLayout (healthFrame);
    auto makeHealthLabel = [this] () -> QLabel* {
        QLabel* label = new QLabel (_currentPoseTab);
        label->setTextFormat (Qt::RichText);
        label->setTextInteractionFlags (Qt::TextSelectableByMouse);
        label->setMinimumWidth (0);
        label->setWordWrap (true);
        label->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
        return label;
    };
    _poseIndicatorLabel = makeHealthLabel ();
    _poseIndicatorLabel->setObjectName (QStringLiteral ("currentPoseStatusLabel"));
    _poseIndicatorLabel->setText (tr ("<b>Status</b><br>-"));
    _poseConditionLabel = makeHealthLabel ();
    _poseConditionLabel->setText (tr ("<b>Condition</b><br>-"));
    _poseManipulabilityLabel = makeHealthLabel ();
    _poseManipulabilityLabel->setText (tr ("<b>Manipulability</b><br>-"));
    _poseMarginLabel = makeHealthLabel ();
    _poseMarginLabel->setText (tr ("<b>Min joint margin</b><br>-"));
    _poseCollisionCapabilityLabel = makeHealthLabel ();
    _poseCollisionCapabilityLabel->setText (tr ("<b>Collision capability</b><br>-"));
    healthLayout->addWidget (_poseIndicatorLabel, 1);
    for (QLabel* label : {_poseConditionLabel, _poseManipulabilityLabel,
                          _poseMarginLabel, _poseCollisionCapabilityLabel}) {
        QFrame* separator = new QFrame (_currentPoseTab);
        separator->setFrameShape (QFrame::VLine);
        separator->setFrameShadow (QFrame::Sunken);
        healthLayout->addWidget (separator);
        healthLayout->addWidget (label, 1);
    }
    cpLayout->addWidget (healthFrame);

    // ---- Joint status is the only primary table. ----
    cpLayout->addWidget (new QLabel(tr("Joint status"), _currentPoseTab));
    _jointStatusTable = makeCompactTable (4, 0);
    _jointStatusTable->setHorizontalHeaderLabels (
        {tr("Joint"), tr("q"), tr("Limit margin"), tr("Status")});
    // 4 列内容不多,横向 stretch 完全能铺满,关闭水平滚动;
    // 垂直方向根据实际行数动态设高(详见 refreshCurrentPose)。
    _jointStatusTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    cpLayout->addWidget (_jointStatusTable);

    // ---- Advanced diagnostics stays out of the normal scan path. ----
    _advancedDiagnosticsToggle = new QToolButton (_currentPoseTab);
    _advancedDiagnosticsToggle->setObjectName (QStringLiteral ("advancedDiagnosticsToggle"));
    _advancedDiagnosticsToggle->setText (tr("Advanced diagnostics"));
    _advancedDiagnosticsToggle->setCheckable (true);
    _advancedDiagnosticsToggle->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    _advancedDiagnosticsToggle->setArrowType (Qt::RightArrow);
    _advancedDiagnosticsContent = new QWidget (_currentPoseTab);
    _advancedDiagnosticsContent->setVisible (false);
    QVBoxLayout* diagnosticsLayout = new QVBoxLayout (_advancedDiagnosticsContent);
    diagnosticsLayout->setContentsMargins (18, 0, 0, 0);
    cpLayout->addWidget (_advancedDiagnosticsToggle);

    diagnosticsLayout->addWidget (new QLabel (tr("Warnings"), _advancedDiagnosticsContent));
    _warningLabel = new QLabel (tr("No active warnings"), _advancedDiagnosticsContent);
    _warningLabel->setWordWrap (true);
    diagnosticsLayout->addWidget (_warningLabel);

    diagnosticsLayout->addWidget (new QLabel(tr("Jacobian"), _advancedDiagnosticsContent));
    _jacobianTable = makeCompactTable (1, 1);
    _jacobianTable->verticalHeader()->setVisible (true);
    _jacobianTable->setVerticalHeaderLabels ({tr("vx"), tr("vy"), tr("vz"),
                                              tr("wx"), tr("wy"), tr("wz")});
    diagnosticsLayout->addWidget (_jacobianTable);

    diagnosticsLayout->addWidget (new QLabel(tr("Singular values"), _advancedDiagnosticsContent));
    // 列数会在 refreshCurrentPose 中按 σ 个数动态设定,所以先建 0 列 1 行;
    // 行数固定为 1,index 已在 horizontal header(σ0 / σ1 / ... / σmin),
    // 不再需要单独 index 行。
    _singularTable = makeCompactTable (0, 1);
    diagnosticsLayout->addWidget (_singularTable);
    setCompactTableVisibleRows (_singularTable, 1);
    _singularTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);

    cpLayout->addWidget (_advancedDiagnosticsContent);
    connect (_advancedDiagnosticsToggle, &QToolButton::toggled,
             this, [this] (bool expanded) {
                 _advancedDiagnosticsToggle->setArrowType (
                     expanded ? Qt::DownArrow : Qt::RightArrow);
                 _advancedDiagnosticsContent->setVisible (expanded);
             });

    cpLayout->addStretch ();

    // -------------------- IK Tab --------------------
    // 单列全宽密集布局(与 Current pose 一致):
    //   1. 顶部输入:Target + 单位 + 6 个 pose spin + threshold + 3 个动作按钮;
    //   2. 过滤 + solver 元信息 + 计数 summary;
    //   3. status summary 标签;
    //   4. IK solution status table(允许滚动,横向铺满);
    //   5. 详情面板(2 行固定高度,跟随选中行更新)。
    _ikTab         = new QWidget(_diagnoseWorkflowPage);
    _ikTab->setObjectName (QStringLiteral ("ikTab"));
    QVBoxLayout* ikLayout = new QVBoxLayout(_ikTab);

    // ---- Pose / IK target controls live in the shared pose section. ----
    _ikImportCurrentPoseButton = new QPushButton(tr("Sync current TCP"), poseIkSection);
    _ikImportCurrentPoseButton->setObjectName (QStringLiteral ("ikSyncCurrentTcpButton"));
    _ikImportCurrentPoseButton->setProperty ("secondaryAction", true);
    _ikImportCurrentPoseButton->setStyleSheet (QStringLiteral (
        "QPushButton { padding: 3px 9px; }"));
    _ikSolveButton = new QPushButton(tr("Solve"), poseIkSection);
    _ikSolveButton->setObjectName (QStringLiteral ("ikSolveButton"));
    _ikSolveButton->setProperty ("primaryAction", true);
    _ikSolveButton->setStyleSheet (QStringLiteral (
        "QPushButton { padding: 3px 12px; color: white; background-color: #2563eb; "
        "border: 1px solid #1d4ed8; border-radius: 3px; font-weight: bold; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QPushButton:pressed { background-color: #1e40af; }"
        "QPushButton:disabled { background-color: #93c5fd; border-color: #93c5fd; }"));
    ikPoseTitleRow->addWidget (_ikImportCurrentPoseButton);
    ikPoseTitleRow->addWidget (_ikSolveButton);
    auto makePoseSpin = [this] (double minimum, double maximum, double step) -> QDoubleSpinBox* {
        QDoubleSpinBox* spin = new QDoubleSpinBox(_ikTab);
        spin->setRange(minimum, maximum);
        spin->setDecimals(6);
        spin->setSingleStep(step);
        return spin;
    };
    _ikXSpin = makePoseSpin(-1000.0, 1000.0, 0.01);
    _ikXSpin->setObjectName (QStringLiteral ("ikXSpin"));
    _ikYSpin = makePoseSpin(-1000.0, 1000.0, 0.01);
    _ikYSpin->setObjectName (QStringLiteral ("ikYSpin"));
    _ikZSpin = makePoseSpin(-1000.0, 1000.0, 0.01);
    _ikZSpin->setObjectName (QStringLiteral ("ikZSpin"));
    _ikRollSpin = makePoseSpin(-360.0, 360.0, 1.0);
    _ikRollSpin->setObjectName (QStringLiteral ("ikRollSpin"));
    _ikPitchSpin = makePoseSpin(-360.0, 360.0, 1.0);
    _ikPitchSpin->setObjectName (QStringLiteral ("ikPitchSpin"));
    _ikYawSpin = makePoseSpin(-360.0, 360.0, 1.0);
    _ikYawSpin->setObjectName (QStringLiteral ("ikYawSpin"));
    _ikDuplicateQThresholdSpin = new QDoubleSpinBox(_ikTab);
    _ikDuplicateQThresholdSpin->setObjectName (QStringLiteral ("ikDuplicateQThresholdSpin"));
    _ikDuplicateQThresholdSpin->setRange(0.0, 1.0);
    _ikDuplicateQThresholdSpin->setDecimals(6);
    _ikDuplicateQThresholdSpin->setSingleStep(0.001);
    _ikDuplicateQThresholdSpin->setValue(_thresholds.ikDuplicateQThreshold);
    _ikDuplicateQThresholdSpin->setParent (poseIkSection);
    _ikDuplicateQThresholdSpin->setMinimumWidth (0);
    _ikDuplicateQThresholdSpin->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);
    _ikCollisionCheck = new QCheckBox (tr("Collision"), poseIkSection);
    _ikCollisionCheck->setObjectName (QStringLiteral ("ikCollisionCheck"));
    _ikCollisionCheck->setChecked (true);
    _ikCollisionCheck->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);
    updateUnitDisplay();

    const QList< QDoubleSpinBox* > targetSpins = {
        _ikXSpin, _ikYSpin, _ikZSpin, _ikRollSpin, _ikPitchSpin, _ikYawSpin};
    for (int index = 0; index < targetSpins.size (); ++index) {
        targetSpins.at (index)->setParent (poseIkSection);
        targetSpins.at (index)->setMinimumWidth (0);
        targetSpins.at (index)->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
        ikPoseGrid->addWidget (targetSpins.at (index), index + 1, 2);
    }

    // ---- 第 3 行:threshold + 3 个动作按钮(横排)----
    QHBoxLayout* solveConfigRow = new QHBoxLayout ();
    solveConfigRow->setContentsMargins (0, 0, 0, 0);
    QLabel* solveConfigTitle = new QLabel (tr ("Solve config"), poseIkSection);
    solveConfigTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    solveConfigRow->addWidget (solveConfigTitle);
    solveConfigRow->addWidget (_ikCollisionCheck);
    solveConfigRow->addWidget (new QLabel (tr ("Duplicate Q"), poseIkSection));
    solveConfigRow->addWidget (_ikDuplicateQThresholdSpin);
    solveConfigRow->addStretch (1);
    poseIkLayout->addLayout (solveConfigRow);

    // ---- 第 4 行:过滤器 + solver 元信息 ----
    _ikSourceLabel = new QLabel (_ikTab);
    _ikSourceLabel->setVisible (false);
    ikLayout->addWidget (_ikSourceLabel);

    // ---- 第 5 行:counts summary ----
    // ---- 第 6 行:status summary 标签 ----
    // ---- 第 7 行:IK solution status table(允许横纵滚动)----
    QHBoxLayout* ikSolutionTitleRow = new QHBoxLayout();
    QLabel* ikSolutionTitle = new QLabel(tr("IK solution status"), _ikTab);
    ikSolutionTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    ikSolutionTitleRow->addWidget (ikSolutionTitle);
    ikSolutionTitleRow->addStretch (1);
    ikSolutionTitleRow->addWidget (new QLabel (tr("Candidates"), _ikTab));
    _ikCandidateFilterCombo = new QComboBox (_ikTab);
    _ikCandidateFilterCombo->addItem (tr("Exclude failed"), 0);
    _ikCandidateFilterCombo->addItem (tr("Usable only"), 1);
    _ikCandidateFilterCombo->addItem (tr("All candidates"), 2);
    ikSolutionTitleRow->addWidget (_ikCandidateFilterCombo);
    ikLayout->addLayout (ikSolutionTitleRow);
    _ikSolutionTable = makeTable();
    _ikSolutionTable->setObjectName (QStringLiteral ("ikSolutionTable"));
    // 把 "Q / failures" 拆成两列 — Failure(短文本) + Q(关节向量),
    // 长 Q 值不再吞掉失败原因。
    _ikSolutionTable->setColumnCount(5);
    _ikSolutionTable->setHorizontalHeaderLabels({
        tr("Index"), tr("Status"), tr("Failure"), tr("Current Q"), tr("Collision")
    });
    _ikSolutionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _ikSolutionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _ikSolutionTable->horizontalHeader()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    _ikSolutionTable->horizontalHeader()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
    _ikSolutionTable->horizontalHeader()->setSectionResizeMode (2, QHeaderView::Stretch);
    _ikSolutionTable->horizontalHeader()->setSectionResizeMode (3, QHeaderView::ResizeToContents);
    _ikSolutionTable->horizontalHeader()->setSectionResizeMode (4, QHeaderView::ResizeToContents);
    _ikSolutionTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _ikSolutionTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _ikSolutionTable->setMaximumHeight (260);
    // Task 5:默认列宽。窄列(标签 / 布尔)+ 中列(数值)+ 宽列(Q),
    // 避免长 Q 把所有数值列挤窄。setStretchLastSection(true) 让 Q 列
    // 在窗口变宽时继续吸收多余宽度。
    // 选中行变化 → 详情表更新。
    connect (_ikSolutionTable, SIGNAL (itemSelectionChanged ()),
             this, SLOT (updateIkSolutionDetails ()));
    connect (_ikSolutionTable, &QTableWidget::itemDoubleClicked, this,
             [this] (QTableWidgetItem*) { applySelectedIkSolution (); });
    // 该表是页面唯一允许滚动的主表,占主导高度。
    ikLayout->addWidget(_ikSolutionTable, 1);

    // ---- 第 8 行:选中详情(2 行固定高度)----
    QHBoxLayout* ikDetailTitleRow = new QHBoxLayout();
    QLabel* ikDetailTitle = new QLabel(tr("Selected candidate"), _ikTab);
    ikDetailTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    ikDetailTitleRow->addWidget (ikDetailTitle);
    ikDetailTitleRow->addStretch (1);
    _ikApplyButton = new QPushButton(tr("Apply selected Q"), _ikTab);
    _ikApplyButton->setObjectName (QStringLiteral ("ikApplyButton"));
    _ikApplyButton->setEnabled (false);
    ikDetailTitleRow->addWidget (_ikApplyButton);
    ikLayout->addLayout (ikDetailTitleRow);
    _ikDetailTable = makeTable();
    _ikDetailTable->setObjectName (QStringLiteral ("ikDetailTable"));
    _ikDetailTable->setColumnCount(2);
    _ikDetailTable->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
    _ikDetailTable->horizontalHeader()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    _ikDetailTable->horizontalHeader()->setSectionResizeMode (1, QHeaderView::Stretch);
    _ikDetailTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    _ikDetailTable->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    _ikDetailTable->setWordWrap (true);
    _ikDetailTable->setMinimumHeight (190);
    _ikDetailTable->setMaximumHeight (270);
    ikLayout->addWidget(_ikDetailTable);

    // -------------------- Task Point Tab --------------------
    _taskPointTab  = new QWidget(_exploreWorkflowPage);
    _taskPointTab->setObjectName (QStringLiteral ("localTasksPage"));
    _workspaceTab  = new QWidget(_exploreWorkflowPage);
    _poseReachTab  = new QWidget(_exploreWorkflowPage);
    _workspaceTab->setObjectName (QStringLiteral ("workspaceTab"));
    _poseReachTab->setObjectName (QStringLiteral ("poseReachTab"));
    _visualizationTab = new QWidget(_exploreWorkflowPage);
    _reportTab     = new QWidget(_exploreWorkflowPage);

    // 包络异步计算:watcher 监听后台包络任务完成 → onEnvelopeFinished 在 UI 线程更新;
    // debounce 定时器(singleShot)在方向数等参数变化后延迟 200ms 才请求重算,
    // 避免连续拖动参数时频繁启动后台任务。
    _envelopeWatcher = new QFutureWatcher< WorkspaceEnvelopeRunResult > (this);
    connect (_envelopeWatcher,
             SIGNAL (finished ()),
             this,
             SLOT (onEnvelopeFinished ()));
    _envelopeDebounceTimer = new QTimer (this);
    _envelopeDebounceTimer->setSingleShot (true);
    connect (_envelopeDebounceTimer,
             SIGNAL (timeout ()),
             this,
             SLOT (refreshVisualization ()));

    buildTaskPointTab ();
    buildWorkspaceTab ();
    buildPoseReachabilityTab ();
    buildVisualizationTab ();
    buildReportTab ();

    // P4:位姿可达性后台执行 watcher。
    _poseReachabilityWatcher =
        new QFutureWatcher< std::vector< PoseReachabilitySample > > (this);
    connect (_poseReachabilityWatcher,
             SIGNAL (finished ()),
             this,
             SLOT (handlePoseReachabilityFinished ()));
    connect (_workspaceWatcher,
             SIGNAL (finished ()),
             this,
             SLOT (handleWorkspaceFinished ()));
    connect (_exploreWatcher,
             SIGNAL (finished ()) ,
             this,
             SLOT (handleCapabilityExplorationFinished ()));

    // 包络异步计算 watcher + 防抖定时器
    QVBoxLayout* diagnoseLayout = new QVBoxLayout (_diagnoseWorkflowPage);
    diagnoseLayout->setContentsMargins (0, 0, 0, 0);
    diagnoseLayout->addWidget (_currentPoseTab);
    diagnoseLayout->addWidget (_ikTab);

    QVBoxLayout* validateLayout = new QVBoxLayout (_validateWorkflowPage);
    validateLayout->setContentsMargins (8, 8, 8, 8);
    _mode2DataSourceCombo = new QComboBox (_validateWorkflowPage);
    _mode2DataSourceCombo->setObjectName (QStringLiteral ("mode2DataSourceCombo"));
    _mode2DataSourceCombo->addItem (tr ("Local Tasks"), 0);
    _mode2DataSourceCombo->addItem (tr ("Frozen Requirements"), 1);
    _mode2DataSourceCombo->setToolTip (
        tr ("Choose editable local tasks or read-only frozen requirements."));
    validateLayout->addWidget (_mode2DataSourceCombo);
    QHBoxLayout* mode2Toolbar = new QHBoxLayout ();
    mode2Toolbar->setContentsMargins (0, 0, 0, 0);
    _mode2LoadJsonButton = new QPushButton (tr ("Load JSON"), _validateWorkflowPage);
    _mode2LoadJsonButton->setObjectName (QStringLiteral ("mode2LoadJsonButton"));
    _mode2ValidateAllButton = new QPushButton (tr ("Validate All"), _validateWorkflowPage);
    _mode2ValidateAllButton->setObjectName (QStringLiteral ("mode2ValidateAllButton"));
    _mode2ValidateSelectedButton = new QPushButton (
        tr ("Validate Selected"), _validateWorkflowPage);
    _mode2ValidateSelectedButton->setObjectName (
        QStringLiteral ("mode2ValidateSelectedButton"));
    _mode2AddButton = new QPushButton (tr ("Add"), _validateWorkflowPage);
    _mode2AddButton->setObjectName (QStringLiteral ("mode2AddButton"));
    _mode2RemoveButton = new QPushButton (tr ("Remove"), _validateWorkflowPage);
    _mode2RemoveButton->setObjectName (QStringLiteral ("mode2RemoveButton"));
    mode2Toolbar->addWidget (_mode2LoadJsonButton);
    mode2Toolbar->addWidget (_mode2ValidateAllButton);
    mode2Toolbar->addWidget (_mode2ValidateSelectedButton);
    mode2Toolbar->addWidget (_mode2AddButton);
    mode2Toolbar->addWidget (_mode2RemoveButton);
    mode2Toolbar->addStretch (1);
    validateLayout->addLayout (mode2Toolbar);
    QHBoxLayout* validateCommands = new QHBoxLayout ();
    _validateLoadRequirementsButton = new QPushButton (
        tr("Load frozen requirements"), _validateWorkflowPage);
    _validateLoadRequirementsButton->setObjectName (
        QStringLiteral ("validateLoadRequirementsButton"));
    _validateRunButton = new QPushButton (tr("Run validation"), _validateWorkflowPage);
    _validateRunButton->setObjectName (QStringLiteral ("validateRunButton"));
    _validateExportButton = new QPushButton (tr("Export report"), _validateWorkflowPage);
    _validateExportButton->setObjectName (QStringLiteral ("validateExportButton"));
    _validateLoadRequirementsButton->setVisible (false);
    _validateRunButton->setVisible (false);
    _validateExportButton->setVisible (false);
    validateCommands->addWidget (_validateLoadRequirementsButton);
    validateCommands->addWidget (_validateRunButton);
    validateCommands->addWidget (_validateExportButton);
    validateCommands->addStretch (1);
    validateLayout->addLayout (validateCommands);

    _validateRequirementStateLabel = new QLabel (
        tr("No frozen requirement artifact loaded."), _validateWorkflowPage);
    _validateRequirementStateLabel->setObjectName (
        QStringLiteral ("validateRequirementStateLabel"));
    _validateRequirementStateLabel->setWordWrap (true);
    validateLayout->addWidget (_validateRequirementStateLabel);

    _validateTaskSectionTitle = new QLabel (tr ("Key station tasks"), _validateWorkflowPage);
    _validateTaskSectionTitle->setObjectName (QStringLiteral ("validateTaskSectionTitle"));
    _validateTaskSectionTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    validateLayout->addWidget (_validateTaskSectionTitle);
    _validateTaskResultTable = new QTableWidget (_validateWorkflowPage);
    _validateTaskResultTable->setObjectName (QStringLiteral ("validateTaskResultTable"));
    _validateTaskResultTable->setColumnCount (6);
    _validateTaskResultTable->setHorizontalHeaderLabels (
        QStringList () << tr("ID") << tr("Name / residual") << tr("Feasibility")
                       << tr("Quality / pose coverage") << tr("EvidenceStage") << tr("Level"));
    _validateTaskResultTable->horizontalHeader ()->setStretchLastSection (true);
    _validateTaskResultTable->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    _validateTaskResultTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    _validateTaskResultTable->setWordWrap (true);
    _validateTaskResultTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    _validateTaskResultTable->setSelectionBehavior (QAbstractItemView::SelectRows);
    validateLayout->addWidget (_validateTaskResultTable);

    _validateRegionSectionTitle = new QLabel (tr ("Demand regions"), _validateWorkflowPage);
    _validateRegionSectionTitle->setObjectName (QStringLiteral ("validateRegionSectionTitle"));
    _validateRegionSectionTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    validateLayout->addWidget (_validateRegionSectionTitle);
    _validateRegionSummaryTable = new QTableWidget (_validateWorkflowPage);
    _validateRegionSummaryTable->setObjectName (QStringLiteral ("validateRegionSummaryTable"));
    _validateRegionSummaryTable->setColumnCount (6);
    _validateRegionSummaryTable->setHorizontalHeaderLabels (
        QStringList () << tr ("ID") << tr ("Level") << tr ("Position coverage") <<
            tr ("Orientation coverage") << tr ("Feasibility") << tr ("Quality"));
    _validateRegionSummaryTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    _validateRegionSummaryTable->setSelectionBehavior (QAbstractItemView::SelectRows);
    _validateRegionSummaryTable->setSelectionMode (QAbstractItemView::SingleSelection);
    _validateRegionSummaryTable->horizontalHeader ()->setStretchLastSection (true);
    _validateRegionSummaryTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    _validateRegionSummaryTable->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    _validateRegionSummaryTable->setMaximumHeight (150);
    validateLayout->addWidget (_validateRegionSummaryTable);

    _validateOrientationProbeLabel = new QLabel (
        tr ("Orientation probes: select a frozen region."), _validateWorkflowPage);
    _validateOrientationProbeLabel->setObjectName (
        QStringLiteral ("validateOrientationProbeLabel"));
    _validateOrientationProbeLabel->setWordWrap (true);
    validateLayout->addWidget (_validateOrientationProbeLabel);

    _validateDiagnosticsToggle = new QToolButton (_validateWorkflowPage);
    _validateDiagnosticsToggle->setObjectName (QStringLiteral ("validateDiagnosticsToggle"));
    _validateDiagnosticsToggle->setText (tr ("Diagnostics"));
    _validateDiagnosticsToggle->setCheckable (true);
    _validateDiagnosticsToggle->setChecked (false);
    validateLayout->addWidget (_validateDiagnosticsToggle);
    _validateDiagnosticsContent = new QWidget (_validateWorkflowPage);
    _validateDiagnosticsContent->setVisible (false);
    QVBoxLayout* frozenDiagnosticsLayout = new QVBoxLayout (_validateDiagnosticsContent);
    frozenDiagnosticsLayout->setContentsMargins (18, 0, 0, 0);
    _validateProvenanceLabel = new QLabel (_validateDiagnosticsContent);
    _validateProvenanceLabel->setObjectName (QStringLiteral ("validateProvenanceLabel"));
    _validateProvenanceLabel->setWordWrap (true);
    frozenDiagnosticsLayout->addWidget (_validateProvenanceLabel);
    _validateRegionCellTable = new QTableWidget (_validateWorkflowPage);
    _validateRegionCellTable->setObjectName (QStringLiteral ("validateRegionCellTable"));
    _validateRegionCellTable->setColumnCount (9);
    _validateRegionCellTable->setHorizontalHeaderLabels (
        QStringList () << tr("Region") << tr("X") << tr("Y") << tr("Z") << tr("Cell")
                       << tr("Feasibility") << tr("Quality") << tr("EvidenceStage")
                       << tr("Reachable orientations"));
    _validateRegionCellTable->horizontalHeader ()->setStretchLastSection (true);
    _validateRegionCellTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    frozenDiagnosticsLayout->addWidget (_validateRegionCellTable);
    validateLayout->addWidget (_validateDiagnosticsContent);
    _taskPointTab->setParent (_validateWorkflowPage);
    validateLayout->addWidget (_taskPointTab, 4);
    validateLayout->removeWidget (_taskPointTab);
    validateLayout->insertWidget (4, _taskPointTab, 4);

    QVBoxLayout* exploreLayout = new QVBoxLayout (_exploreWorkflowPage);
    exploreLayout->setContentsMargins (0, 0, 0, 0);
    QHBoxLayout* exploreCommands = new QHBoxLayout ();
    _exploreRunButton = new QPushButton (tr ("Run capability exploration"), _exploreWorkflowPage);
    _exploreRunButton->setObjectName (QStringLiteral ("exploreRunButton"));
    _exploreCancelButton = new QPushButton (tr ("Cancel"), _exploreWorkflowPage);
    _exploreCancelButton->setObjectName (QStringLiteral ("exploreCancelButton"));
    _exploreSamplesSpin = new QSpinBox (_exploreWorkflowPage);
    _exploreSamplesSpin->setObjectName (QStringLiteral ("exploreSamplesSpin"));
    _exploreSamplesSpin->setRange (1, 1000000);
    _exploreSamplesSpin->setValue (1000);
    _exploreModeCombo = new QComboBox (_exploreWorkflowPage);
    _exploreModeCombo->setObjectName (QStringLiteral ("exploreModeCombo"));
    _exploreModeCombo->addItem (tr ("Random"), 0);
    _exploreModeCombo->addItem (tr ("Joint Grid"), 1);
    _exploreModeCombo->addItem (tr ("Pose Reachability"), 2);
    _exploreSeedSpin = new QSpinBox (_exploreWorkflowPage);
    _exploreSeedSpin->setObjectName (QStringLiteral ("exploreSeedSpin"));
    _exploreSeedSpin->setRange (0, std::numeric_limits<int>::max ());
    _exploreSeedSpin->setValue (1);
    _exploreGridStepsSpin = new QSpinBox (_exploreWorkflowPage);
    _exploreGridStepsSpin->setObjectName (QStringLiteral ("exploreGridStepsSpin"));
    _exploreGridStepsSpin->setRange (2, 64);
    _exploreGridStepsSpin->setValue (5);
    _exploreDirectionSamplesSpin = new QSpinBox (_exploreWorkflowPage);
    _exploreDirectionSamplesSpin->setObjectName (
        QStringLiteral ("exploreDirectionSamplesSpin"));
    _exploreDirectionSamplesSpin->setRange (1, 1000);
    _exploreDirectionSamplesSpin->setValue (1);
    _exploreRollSamplesSpin = new QSpinBox (_exploreWorkflowPage);
    _exploreRollSamplesSpin->setObjectName (QStringLiteral ("exploreRollSamplesSpin"));
    _exploreRollSamplesSpin->setRange (1, 360);
    _exploreRollSamplesSpin->setValue (1);
    _exploreStateLabel = new QLabel (tr ("Estimated: Idle"), _exploreWorkflowPage);
    _exploreStateLabel->setObjectName (QStringLiteral ("exploreStateLabel"));
    exploreCommands->addWidget (_exploreRunButton);
    exploreCommands->addWidget (_exploreCancelButton);
    _exploreSamplesLabel = new QLabel (tr ("Samples"), _exploreWorkflowPage);
    _exploreSamplesLabel->setObjectName (QStringLiteral ("exploreSamplesLabel"));
    exploreCommands->addWidget (_exploreSamplesLabel);
    exploreCommands->addWidget (_exploreSamplesSpin);
    exploreCommands->addWidget (new QLabel (tr ("Mode"), _exploreWorkflowPage));
    exploreCommands->addWidget (_exploreModeCombo);
    _exploreSeedLabel = new QLabel (tr ("Seed"), _exploreWorkflowPage);
    _exploreSeedLabel->setObjectName (QStringLiteral ("exploreSeedLabel"));
    exploreCommands->addWidget (_exploreSeedLabel);
    exploreCommands->addWidget (_exploreSeedSpin);
    _exploreGridStepsLabel = new QLabel (tr ("Grid Steps"), _exploreWorkflowPage);
    _exploreGridStepsLabel->setObjectName (QStringLiteral ("exploreGridStepsLabel"));
    exploreCommands->addWidget (_exploreGridStepsLabel);
    exploreCommands->addWidget (_exploreGridStepsSpin);
    _exploreDirectionsLabel = new QLabel (tr ("Directions"), _exploreWorkflowPage);
    _exploreDirectionsLabel->setObjectName (QStringLiteral ("exploreDirectionsLabel"));
    exploreCommands->addWidget (_exploreDirectionsLabel);
    exploreCommands->addWidget (_exploreDirectionSamplesSpin);
    _exploreRollsLabel = new QLabel (tr ("Rolls"), _exploreWorkflowPage);
    _exploreRollsLabel->setObjectName (QStringLiteral ("exploreRollsLabel"));
    exploreCommands->addWidget (_exploreRollsLabel);
    exploreCommands->addWidget (_exploreRollSamplesSpin);
    exploreCommands->addWidget (_exploreStateLabel);
    exploreCommands->addStretch (1);
    exploreLayout->addLayout (exploreCommands);
    exploreLayout->addWidget (_workspaceTab);
    exploreLayout->addWidget (_poseReachTab);
    exploreLayout->addWidget (_visualizationTab);
    _reportTab->setVisible (false);
    _visualPlot->setMaximumHeight (160);
    _visualPlot->setMinimumHeight (160);

    // addModePage:把某个工作流页包进独立 QScrollArea 并挂到 _modeStack,
    // 页面内容超高时出现滚动条,保证小窗口 / 窄 Dock 下仍可用。
    auto addModePage = [this] (QWidget* page) {
        QScrollArea* scroll = new QScrollArea (_modeStack);
        scroll->setWidgetResizable (true);
        scroll->setFrameShape (QFrame::NoFrame);
        scroll->setWidget (page);
        _modeStack->addWidget (scroll);
        if (page == _diagnoseWorkflowPage) {
            scroll->setObjectName (QStringLiteral ("diagnoseScroll"));
            _diagnoseScroll = scroll;
        }
        if (page == _validateWorkflowPage) {
            scroll->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
            page->setMinimumWidth (0);
            page->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
        }
        if (page == _exploreWorkflowPage)
            _exploreScroll = scroll;
    };
    addModePage (_diagnoseWorkflowPage);
    addModePage (_validateWorkflowPage);
    addModePage (_exploreWorkflowPage);
    _modeTabs->setCurrentIndex (0);
    _modeStack->setCurrentIndex (0);
    updateMode2DataSource (_mode2DataSourceCombo->currentIndex ());

    _status = new QLineEdit(this);
    _status->setObjectName (QStringLiteral ("kinematicStatus"));
    _status->setReadOnly(true);
    root->addWidget(_status);

    connect (_refreshCurrentPoseButton, SIGNAL (clicked ()), this, SLOT (refreshCurrentPose ()));
    connect (_thresholdSettingsButton, SIGNAL (clicked ()), this,
             SLOT (openThresholdSettingsDialog ()));
    connect (_mode2DataSourceCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (updateMode2DataSource (int)));
    connect (_mode2LoadJsonButton, SIGNAL (clicked ()), this,
             SLOT (openFrozenRequirementsForValidation ()));
    connect (_mode2ValidateAllButton, &QPushButton::clicked, this, [this] () {
        if (_mode2DataSourceCombo != nullptr && _mode2DataSourceCombo->currentIndex () == 0)
            analyzeAllTaskPoints ();
        else
            validateRequirements ();
    });
    connect (_mode2ValidateSelectedButton, SIGNAL (clicked ()), this,
             SLOT (validateSelectedMode2Source ()));
    connect (_mode2AddButton, SIGNAL (clicked ()), this, SLOT (addTaskPointRow ()));
    connect (_mode2RemoveButton, SIGNAL (clicked ()), this,
             SLOT (removeSelectedTaskPointRow ()));
    connect (_validateLoadRequirementsButton, SIGNAL (clicked ()), this,
             SLOT (openFrozenRequirementsForValidation ()));
    connect (_validateRunButton, SIGNAL (clicked ()), this, SLOT (validateRequirements ()));
    connect (_validateExportButton, SIGNAL (clicked ()), this, SLOT (exportReportJson ()));
    connect (_validateDiagnosticsToggle, &QToolButton::toggled,
             _validateDiagnosticsContent, &QWidget::setVisible);
    connect (_validateTaskResultTable->selectionModel (),
             &QItemSelectionModel::selectionChanged, this,
             [this] (const QItemSelection&, const QItemSelection&) {
                 if (_validateTaskResultTable->selectionModel ()->hasSelection () &&
                     _validateRegionSummaryTable != nullptr)
                     _validateRegionSummaryTable->clearSelection ();
                 refreshWorkflowControls ();
             });
    connect (_validateRegionSummaryTable->selectionModel (),
             &QItemSelectionModel::selectionChanged, this,
             [this] (const QItemSelection&, const QItemSelection&) {
                 if (_validateRegionSummaryTable->selectionModel ()->hasSelection () &&
                     _validateTaskResultTable != nullptr)
                     _validateTaskResultTable->clearSelection ();
                 const QModelIndexList rows =
                     _validateRegionSummaryTable->selectionModel ()->selectedRows ();
                 if (!rows.isEmpty () && _validateOrientationProbeLabel != nullptr) {
                     QTableWidgetItem* item = _validateRegionSummaryTable->item (
                         rows.front ().row (), 0);
                     const QString id = item == nullptr ? QString () :
                         item->data (Qt::UserRole).toString ();
                     for (const RequirementExecutionRegion& region :
                          _validateExecution.workspaceRegions) {
                         if (QString::fromStdString (region.id) == id) {
                             _validateOrientationProbeLabel->setText (
                                 tr ("Orientation probes: directions %1, rolls %2.")
                                     .arg (region.directionSamples).arg (region.rollSamples));
                             break;
                         }
                     }
                 }
                 refreshWorkflowControls ();
             });
    connect (_exploreRunButton, SIGNAL (clicked ()), this,
             SLOT (startCapabilityExploration ()));
    connect (_exploreCancelButton, SIGNAL (clicked ()), this,
             SLOT (cancelCapabilityExploration ()));
    const auto updateExploreModeUi = [this] (int index) {
        const bool poseMode = index == 2;
        if (_workspaceTab != nullptr)
            _workspaceTab->setVisible (!poseMode);
        if (_poseReachTab != nullptr)
            _poseReachTab->setVisible (poseMode);
        if (_exploreSeedSpin != nullptr)
            _exploreSeedSpin->setVisible (index == 0);
        if (_exploreSeedLabel != nullptr)
            _exploreSeedLabel->setVisible (index == 0);
        if (_exploreSamplesSpin != nullptr)
            _exploreSamplesSpin->setVisible (index == 0);
        if (_exploreSamplesLabel != nullptr)
            _exploreSamplesLabel->setVisible (index == 0);
        if (_exploreGridStepsSpin != nullptr)
            _exploreGridStepsSpin->setVisible (index == 1);
        if (_exploreGridStepsLabel != nullptr)
            _exploreGridStepsLabel->setVisible (index == 1);
        if (_exploreDirectionSamplesSpin != nullptr)
            _exploreDirectionSamplesSpin->setVisible (poseMode);
        if (_exploreDirectionsLabel != nullptr)
            _exploreDirectionsLabel->setVisible (poseMode);
        if (_exploreRollSamplesSpin != nullptr)
            _exploreRollSamplesSpin->setVisible (poseMode);
        if (_exploreRollsLabel != nullptr)
            _exploreRollsLabel->setVisible (poseMode);
    };
    connect (_exploreModeCombo,
             static_cast< void (QComboBox::*) (int) > (&QComboBox::currentIndexChanged),
             this, updateExploreModeUi);
    updateExploreModeUi (_exploreModeCombo->currentIndex ());
    auto rearmCapabilityExploration = [this] (int) {
        if (!_exploreRunActive && _exploreCancellationRequested) {
            _exploreCancellationRequested = false;
            refreshWorkflowControls ();
        }
    };
    connect (_exploreSamplesSpin,
             static_cast< void (QSpinBox::*) (int) > (&QSpinBox::valueChanged),
             this, rearmCapabilityExploration);
    connect (_exploreModeCombo,
             static_cast< void (QComboBox::*) (int) > (&QComboBox::currentIndexChanged),
             this, rearmCapabilityExploration);
    connect (_exploreSeedSpin,
             static_cast< void (QSpinBox::*) (int) > (&QSpinBox::valueChanged),
             this, rearmCapabilityExploration);
    connect (_exploreGridStepsSpin,
             static_cast< void (QSpinBox::*) (int) > (&QSpinBox::valueChanged),
             this, rearmCapabilityExploration);
    connect (_exploreDirectionSamplesSpin,
             static_cast< void (QSpinBox::*) (int) > (&QSpinBox::valueChanged),
             this, rearmCapabilityExploration);
    connect (_exploreRollSamplesSpin,
             static_cast< void (QSpinBox::*) (int) > (&QSpinBox::valueChanged),
             this, rearmCapabilityExploration);
    connect (_exploreDirectionSamplesSpin,
             static_cast< void (QSpinBox::*) (int) > (&QSpinBox::valueChanged),
             _poseDirectionSamplesSpin, &QSpinBox::setValue);
    connect (_exploreRollSamplesSpin,
             static_cast< void (QSpinBox::*) (int) > (&QSpinBox::valueChanged),
             _poseRollSamplesSpin, &QSpinBox::setValue);

    // Explore owns the primary commands. Keep the legacy result pages but remove
    // their conflicting sampling and command entry points.
    _workspaceRunButton->setVisible (false);
    _workspaceCancelButton->setVisible (false);
    _workspaceModeCombo->setVisible (false);
    _workspaceSampleCountSpin->setVisible (false);
    _workspaceGridStepsSpin->setVisible (false);
    _workspaceSeedSpin->setVisible (false);
    _workspaceCollisionCheck->setVisible (false);
    _workspaceTab->findChild< QLabel* > (QStringLiteral ("workspaceSamplingTitle"))->setVisible (false);
    _workspaceTab->findChild< QLabel* > (QStringLiteral ("workspaceModeLabel"))->setVisible (false);
    _workspaceTab->findChild< QLabel* > (QStringLiteral ("workspaceSamplesLabel"))->setVisible (false);
    _workspaceTab->findChild< QLabel* > (QStringLiteral ("workspaceGridStepsLabel"))->setVisible (false);
    _poseAnalyzeButton->setVisible (false);
    _poseCancelButton->setVisible (false);
    _poseDirectionSamplesSpin->setVisible (false);
    _poseRollSamplesSpin->setVisible (false);
    _poseReachTab->findChild< QLabel* > (QStringLiteral ("poseDirectionsLabel"))->setVisible (false);
    _poseReachTab->findChild< QLabel* > (QStringLiteral ("poseRollsLabel"))->setVisible (false);
    _poseDirectionSamplesSpin->setValue (_exploreDirectionSamplesSpin->value ());
    _poseRollSamplesSpin->setValue (_exploreRollSamplesSpin->value ());
    // Task 5 step 2:勾选过滤器时即时刷新 IK 结果表与统计。
    connect (_ikCandidateFilterCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshIkSolutionView ()));
    connect (_ikImportCurrentPoseButton, SIGNAL (clicked ()), this, SLOT (importCurrentPoseToIk ()));
    auto invalidateIkTarget = [this] (double) {
        if (_ikSourceLabel != NULL)
            _ikSourceLabel->setVisible (false);
        invalidateIkResultPresentation ();
    };
    for (QDoubleSpinBox* spin :
         {_ikXSpin, _ikYSpin, _ikZSpin, _ikRollSpin, _ikPitchSpin, _ikYawSpin}) {
        connect (spin,
                 static_cast< void (QDoubleSpinBox::*) (double) > (
                     &QDoubleSpinBox::valueChanged),
                 this,
                 invalidateIkTarget);
    }
    connect (_lengthUnitCombo, SIGNAL (currentIndexChanged (int)), this, SLOT (updateUnitDisplay ()));
    connect (_angleUnitCombo, SIGNAL (currentIndexChanged (int)), this, SLOT (updateUnitDisplay ()));
    connect (_deviceCombo,
             static_cast< void (QComboBox::*) (int) > (&QComboBox::currentIndexChanged),
             this,
             [this] (int) {
                 cancelEnvelopeRequest (false);
                 invalidateEnvelopeCache ();
                 populateTcpFrames ();
                 refreshCurrentPose ();
                 installTaskPointDelegates ();
                 updateVisualizationControls ();
                 refreshWorkflowControls ();
             });
    connect (_tcpFrameCombo,
             static_cast< void (QComboBox::*) (int) > (&QComboBox::currentIndexChanged),
             this,
             [this] (int) {
                 cancelEnvelopeRequest (false);
                 invalidateEnvelopeCache ();
                 refreshCurrentPose ();
                 updateVisualizationControls ();
                 refreshWorkflowControls ();
             });
    connect (_ikSolveButton, SIGNAL (clicked ()), this, SLOT (solveIk ()));
    connect (_ikApplyButton, SIGNAL (clicked ()), this, SLOT (applySelectedIkSolution ()));
    connect (_addTaskPointButton, SIGNAL (clicked ()), this, SLOT (addTaskPointRow ()));
    connect (_removeTaskPointButton, SIGNAL (clicked ()), this, SLOT (removeSelectedTaskPointRow ()));
    connect (_importTaskPointsButton, SIGNAL (clicked ()), this, SLOT (importTaskPointsCsv ()));
    connect (_importFrozenRequirementsButton, SIGNAL (clicked ()), this, SLOT (importFrozenRequirements ()));
    connect (_exportTaskPointsButton, SIGNAL (clicked ()), this, SLOT (exportTaskPointsCsv ()));
    connect (_exportTaskPointResultsButton, SIGNAL (clicked ()), this, SLOT (exportTaskPointResultsCsv ()));
    connect (_analyzeAllTaskPointsButton, SIGNAL (clicked ()), this, SLOT (analyzeAllTaskPoints ()));
    // P2:Task points 专用按钮的信号连接
    connect (_analyzeSelectedTaskPointsButton, SIGNAL (clicked ()),
             this, SLOT (analyzeSelectedTaskPoints ()));
    connect (_importCurrentTcpTaskPointButton, SIGNAL (clicked ()),
             this, SLOT (importCurrentTcpAsTaskPoint ()));
    connect (_applySelectedTaskPointBestQButton, SIGNAL (clicked ()),
             this, SLOT (applySelectedTaskPointBestQ ()));
    connect (_openSelectedTaskPointInIkButton, SIGNAL (clicked ()),
             this, SLOT (openSelectedTaskPointInIk ()));
    // P2:Task point 表格选中行变化 → 更新 selected-only 按钮的 enabled 状态。
    connect (_taskPointTable->selectionModel (), &QItemSelectionModel::selectionChanged,
             this, [this] (const QItemSelection&, const QItemSelection&) {
                 updateTaskPointSelectionButtons ();
                 updateTaskPointDetails ();
                 refreshWorkflowControls ();
             });
    connect (_taskPointModel, &QAbstractItemModel::dataChanged,
             this, [this] () {
                 _lastTaskPointResults = _taskPointModel->results ();
                 refreshVisualization ();
                 updateTaskPointDetails ();
             });
    connect (_taskPointModel, &QAbstractItemModel::modelReset,
             this, [this] () {
                 _lastTaskPointResults = _taskPointModel->results ();
                 refreshVisualization ();
                 updateTaskPointDetails ();
             });
    connect (_taskPointModel, &QAbstractItemModel::rowsInserted,
             this, [this] () {
                 _lastTaskPointResults = _taskPointModel->results ();
                 refreshVisualization ();
                 updateTaskPointDetails ();
             });
    connect (_taskPointModel, &QAbstractItemModel::rowsRemoved,
             this, [this] () {
                 _lastTaskPointResults = _taskPointModel->results ();
                 refreshVisualization ();
                 updateTaskPointDetails ();
             });
    connect (_workspaceRunButton, SIGNAL (clicked ()), this, SLOT (sampleWorkspace ()));
    connect (_workspaceCancelButton, SIGNAL (clicked ()), this, SLOT (cancelWorkspaceSampling ()));
    connect (_workspaceExportButton, SIGNAL (clicked ()), this, SLOT (exportWorkspaceCsv ()));
    connect (_poseAddRowButton, SIGNAL (clicked ()), this, SLOT (addPoseReachabilityRow ()));
    connect (_poseAnalyzeButton, SIGNAL (clicked ()), this, SLOT (analyzePoseReachability ()));
    connect (_poseExportButton, SIGNAL (clicked ()), this, SLOT (exportPoseReachabilityCsv ()));
    connect (_poseOpenVisualizationButton, SIGNAL (clicked ()),
             this, SLOT (openPoseReachabilityInVisualization ()));
    // P4:Cancel 按钮设置取消标志并自禁用,避免重复点击。
    connect (_poseCancelButton, &QPushButton::clicked, this, [this] () {
        if (_poseReachabilityCancelRequested)
            _poseReachabilityCancelRequested->store (true);
        if (_poseCancelButton != NULL)
            _poseCancelButton->setEnabled (false);
        setStatus (tr("Pose reachability cancellation requested."));
    });
    connect (_reportRefreshButton, SIGNAL (clicked ()), this, SLOT (refreshReport ()));
    connect (_reportExportJsonButton, SIGNAL (clicked ()), this, SLOT (exportReportJson ()));
    connect (_reportExportCsvButton, SIGNAL (clicked ()), this, SLOT (exportReportCsv ()));
    connect (_reportStageFilterCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshReport ()));
    connect (_reportFeasibilityFilterCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshReport ()));
    connect (_reportQualityFilterCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshReport ()));
    connect (_reportFailureFilterCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshReport ()));
    connect (_reportRegionFilterEdit, SIGNAL (textChanged (const QString&)),
             this, SLOT (refreshReport ()));
    connect (_thresholdApplyButton, SIGNAL (clicked ()), this, SLOT (applyThresholds ()));
    connect (_visualProjectionCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_visualColorModeCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_visualShowPassCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_visualShowWarningCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_visualShowFailCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_visualShowLabelsCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshVisualization ()));

    setStatus(tr("Load a WorkCell to start kinematic analysis."));
    refreshWorkflowControls ();
    // Task 4 step 5:无选中行时显示"No IK candidate selected."。
    setIkDetailsEmpty ();
}

KinematicAnalysisWidget::~KinematicAnalysisWidget ()
{
    // RobWorkStudio 会在 QObject 子对象销毁前统一注销插件回调；此后再访问宿主事件
    // 是未定义行为，因此析构只清空宿主指针，不再调用 _studio->stateChangedEvent()。
    // （英文原注：RobWorkStudio detaches plugin-owned callbacks before QObject children
    //   are destroyed. Do not access the host event after that point.）
    _studio = NULL;
    if (_workspaceCancelRequested)
        _workspaceCancelRequested->store (true);
    if (_poseReachabilityCancelRequested)
        _poseReachabilityCancelRequested->store (true);
    if (_exploreCancelToken)
        _exploreCancelToken->store (true);
    cancelEnvelopeRequest (true);
    if (_workspaceWatcher != NULL && _workspaceWatcher->isRunning ())
        _workspaceWatcher->waitForFinished ();
    if (_poseReachabilityWatcher != NULL && _poseReachabilityWatcher->isRunning ())
        _poseReachabilityWatcher->waitForFinished ();
    if (_exploreWatcher != NULL && _exploreWatcher->isRunning ())
        _exploreWatcher->waitForFinished ();
    if (_workspaceRunActive)
        QApplication::restoreOverrideCursor ();
    if (_poseReachabilityRunActive)
        QApplication::restoreOverrideCursor ();
}

QSize KinematicAnalysisWidget::sizeHint () const
{
    return QSize (360, 620);
}

// 最小尺寸提示由 300 降到 150 宽：配合 Dock 宽度统一策略，允许工作流 Dock 缩至 240px。
QSize KinematicAnalysisWidget::minimumSizeHint () const
{
    return QSize (150, 420);
}

// setRobWorkStudio:由 KinematicAnalysisPlugin::initialize 调用,缓存主程序句柄;
// 用它获取当前 state、写回 IK 解。
void KinematicAnalysisWidget::setRobWorkStudio(RobWorkStudio* studio)
{
    if (_studio != NULL)
        _studio->stateChangedEvent ().remove (this);
    _studio = studio;
    if (_studio != NULL) {
        _studio->stateChangedEvent ().add (
            boost::bind (
                &KinematicAnalysisWidget::stateChangedListener,
                this,
                boost::arg< 1 > ()),
            this);
    }
}

// setWorkCell:WorkCell 变化时调用,刷新设备/帧下拉,并提示用户当前状态。
void KinematicAnalysisWidget::setWorkCell(rw::models::WorkCell* workcell)
{
    cancelEnvelopeRequest (true);
    invalidateEnvelopeCache ();
    if (_workcell != workcell) {
        ++_workcellSessionGeneration;
        // WorkCell changes delimit project sessions. Cancel producers and
        // discard every result/cache that belongs to the previous session so
        // a newly created project cannot inherit stale task points or plots.
        if (_workspaceCancelRequested)
            _workspaceCancelRequested->store (true);
        if (_poseReachabilityCancelRequested)
            _poseReachabilityCancelRequested->store (true);
        if (_exploreCancelToken)
            _exploreCancelToken->store (true);
        _exploreRunActive = false;
        _exploreCancellationRequested = false;
        _exploreCompletedSamples = 0;
        _explorePlannedSamples = 0;
        if (_exploreStateLabel != nullptr)
            _exploreStateLabel->setText (tr ("Estimated: Idle"));

        if (_taskPointTable != nullptr && _taskPointTable->selectionModel () != nullptr)
            _taskPointTable->clearSelection ();
        if (_taskPointModel != nullptr)
            _taskPointModel->setRowsFromTaskPoints ({});
        _lastTaskPointResults.clear ();
        _lastCurrentPose = KinematicCurrentPoseResult ();
        _lastIkResult = KinematicIkAnalysisResult ();
        _ikResultStale = false;
        _workspaceSamples.clear ();
        _poseReachabilitySamples.clear ();
        _workspaceCollisionUnavailable = false;
        _poseReachabilityCollisionUnavailable = false;
        if (_workspaceTable != nullptr)
            _workspaceTable->setRowCount (0);
        if (_poseResultTable != nullptr)
            _poseResultTable->setRowCount (0);

        _validateExecution = RequirementExecutionSet ();
        _validateSummary = RequirementValidationSummary ();
        _validateExecutionSet = false;
        _validateHasResults = false;
        if (_validateTaskResultTable != nullptr)
            _validateTaskResultTable->setRowCount (0);
        if (_validateRegionCellTable != nullptr)
            _validateRegionCellTable->setRowCount (0);
        if (_validateRegionSummaryTable != nullptr)
            _validateRegionSummaryTable->setRowCount (0);
        if (_validateRequirementStateLabel != nullptr)
            _validateRequirementStateLabel->setText (
                tr("No frozen requirement artifact loaded."));

        // Refresh the presentation while no WorkCell is bound; this resets
        // Current Pose and IK tables without analyzing the incoming cell.
        _workcell = nullptr;
        refreshCurrentPose ();
        refreshIkSolutionView ();
        updateTaskPointDetails ();
        updateTaskPointSelectionButtons ();
        applyWorkspaceResults ({});
        applyPoseReachabilityResults ({});
        clearVisualizationData ();
        if (_visualSummaryLabel != nullptr)
            _visualSummaryLabel->setText (tr ("No visualization data."));
        refreshReport ();
    }
    _workcell = workcell;
    populateDevices ();
    populateTcpFrames ();
    installTaskPointDelegates ();
    if (_workcell == NULL)
        setStatus(tr("No WorkCell loaded."));
    else if (_deviceCombo->count() == 0)
        setStatus(tr("No device found in WorkCell."));
    else
        setStatus(tr("WorkCell loaded. Select a device and refresh analysis."));
    if (_workcell == NULL)
        clearVisualizationData ();
    refreshWorkflowControls ();
}

// refreshWorkflowControls:按"WorkCell → 设备 → TCP 帧"的前置条件链统一刷新
// Diagnose / Validate / Explore 三个工作流页的按钮可用性,并在条件不满足时把
// 具体缺失原因写到底部状态栏,避免用户在缺少前置条件时点击按钮无响应。
// updateMode2DataSource:切换 Validate 页数据源(Local Tasks ↔ Frozen Requirements)。
// 本地任务源显示任务点表格,冻结需求源显示校验结果表;切换时同步显隐
// 对应控件组并刷新按钮可用性。
void KinematicAnalysisWidget::updateMode2DataSource (int index)
{
    const bool localTasks = index == 0;
    if (_taskPointTab != nullptr) {
        _taskPointTab->setVisible (localTasks);
        _taskPointTab->setEnabled (localTasks);
    }
    const QList< QWidget* > frozenControls = {
        _validateRequirementStateLabel,
        _validateTaskSectionTitle,
        _validateTaskResultTable,
        _validateOrientationProbeLabel,
        _validateDiagnosticsToggle};
    for (QWidget* control : frozenControls) {
        if (control != nullptr)
            control->setVisible (!localTasks);
    }
    if (_validateDiagnosticsContent != nullptr)
        _validateDiagnosticsContent->setVisible (
            !localTasks && _validateDiagnosticsToggle != nullptr &&
            _validateDiagnosticsToggle->isChecked ());
    if (_validateRegionSectionTitle != nullptr)
        _validateRegionSectionTitle->setVisible (true);
    if (_validateRegionSummaryTable != nullptr)
        _validateRegionSummaryTable->setVisible (true);
    refreshWorkflowControls ();
}

// refreshWorkflowControls:统一刷新三模式工作流页按钮可用性。按
// "WorkCell → 设备 → TCP 帧"前置条件链逐级使能,任一缺失时禁用依赖它的按钮,
// 并把缺失原因写入底部状态栏,避免用户在缺少前置条件时点击按钮无响应。
void KinematicAnalysisWidget::refreshWorkflowControls ()
{
    const bool hasWorkCell = _workcell != nullptr;
    const bool hasDevice = hasWorkCell && _deviceCombo != nullptr &&
                           _deviceCombo->currentIndex () >= 0 &&
                           selectedDevice () != nullptr;
    const bool hasTcp = hasDevice && _tcpFrameCombo != nullptr &&
                        _tcpFrameCombo->currentIndex () >= 0 &&
                        selectedTcpFrame () != nullptr;
    if (_refreshCurrentPoseButton != nullptr)
        _refreshCurrentPoseButton->setEnabled (hasTcp);
    if (_validateLoadRequirementsButton != nullptr)
        _validateLoadRequirementsButton->setEnabled (hasTcp);
    if (_validateRunButton != nullptr)
        _validateRunButton->setEnabled (hasTcp && _validateExecutionSet);
    if (_validateExportButton != nullptr)
        _validateExportButton->setEnabled (hasTcp && _validateHasResults);
    const bool localTasks = _mode2DataSourceCombo == nullptr ||
                            _mode2DataSourceCombo->currentIndex () == 0;
    const bool localSelection = _taskPointTable != nullptr &&
                                _taskPointTable->selectionModel () != nullptr &&
                                _taskPointTable->selectionModel ()->hasSelection ();
    const bool frozenTaskSelection = _validateTaskResultTable != nullptr &&
                                     _validateTaskResultTable->selectionModel () != nullptr &&
                                     _validateTaskResultTable->selectionModel ()->hasSelection ();
    const bool frozenRegionSelection = _validateRegionSummaryTable != nullptr &&
                                       _validateRegionSummaryTable->selectionModel () != nullptr &&
                                       _validateRegionSummaryTable->selectionModel ()->hasSelection ();
    if (_mode2LoadJsonButton != nullptr)
        _mode2LoadJsonButton->setEnabled (!localTasks && hasTcp);
    if (_mode2ValidateAllButton != nullptr)
        _mode2ValidateAllButton->setEnabled (
            localTasks ? hasTcp : (hasTcp && _validateExecutionSet));
    if (_mode2ValidateSelectedButton != nullptr)
        _mode2ValidateSelectedButton->setEnabled (
            hasTcp && (localTasks ? localSelection :
                (_validateExecutionSet && (frozenTaskSelection || frozenRegionSelection))));
    if (_mode2AddButton != nullptr)
        _mode2AddButton->setEnabled (localTasks);
    if (_mode2RemoveButton != nullptr)
        _mode2RemoveButton->setEnabled (localTasks && localSelection);
    if (_exploreRunButton != nullptr)
        _exploreRunButton->setEnabled (
            hasTcp && !_exploreRunActive && !_poseReachabilityRunActive &&
            !_exploreCancellationRequested);
    if (_exploreCancelButton != nullptr)
        _exploreCancelButton->setEnabled (
            hasTcp && (_exploreRunActive || _poseReachabilityRunActive));

    if (!hasWorkCell)
        setStatus (tr("No WorkCell loaded."));
    else if (!hasDevice)
        setStatus (tr("No device found in WorkCell."));
    else if (!hasTcp)
        setStatus (tr("No TCP frame selected."));
}

// openFrozenRequirementsForValidation:弹出文件选择框加载已冻结的工程需求工件。
// 文件内容直接交给 loadFrozenRequirementDocument 解析并校验;失败时用消息框
// 回显具体原因(statusMessage),成功则刷新 Run Validation 按钮可用性。
void KinematicAnalysisWidget::openFrozenRequirementsForValidation ()
{
    if (_workcell == nullptr || selectedDevice () == nullptr || selectedTcpFrame () == nullptr) {
        refreshWorkflowControls ();
        return;
    }
    const QString path = QFileDialog::getOpenFileName (
        this, tr ("Load frozen engineering requirements"), QString (),
        tr ("Engineering requirements (*.requirements.json *.json);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus (tr ("Frozen requirement loading canceled."));
        return;
    }
    QFile file (path);
    if (!file.open (QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus (tr ("Frozen requirement loading failed: could not open file."));
        return;
    }
    const bool loaded = loadFrozenRequirementDocument (
        file.readAll (), QFileInfo (path).absolutePath ().toStdString ());
    if (!loaded)
        QMessageBox::warning (this, tr ("Frozen requirement loading"), statusMessage ());
}

// startCapabilityExploration:启动"能力探索"后台采样(QtConcurrent::run)。
// 先在 UI 线程把设备 / TCP / state / 阈值 / 配置全部收集为值快照,再通过
// callbacks 把取消标志与进度回调交给 worker;worker 内只触碰快照,完成后由
// handleCapabilityExplorationFinished 在 UI 线程收尾,运行期间不阻塞界面。
void KinematicAnalysisWidget::startCapabilityExploration ()
{
    if (_exploreModeCombo != nullptr && _exploreModeCombo->currentIndex () == 2) {
        _poseDirectionSamplesSpin->setValue (_exploreDirectionSamplesSpin->value ());
        _poseRollSamplesSpin->setValue (_exploreRollSamplesSpin->value ());
        if (_exploreStateLabel != nullptr)
            _exploreStateLabel->setText (tr ("Estimated: Running"));
        if (_poseAnalyzeButton != nullptr)
            _poseAnalyzeButton->click ();
        refreshWorkflowControls ();
        return;
    }
    if (_workcell == nullptr || selectedDevice () == nullptr || selectedTcpFrame () == nullptr ||
        _exploreCancellationRequested || _exploreWatcher == nullptr ||
        _exploreWatcher->isRunning ())
        return;
    _exploreCancellationRequested = false;
    if (_exploreCancelToken)
        _exploreCancelToken->store (false);
    _exploreWatcher->setProperty (
        "workcellSessionGeneration",
        QVariant::fromValue< qulonglong > (_workcellSessionGeneration));

    WorkspaceSamplingConfig config;
    config.sampleCount = _exploreSamplesSpin->value ();
    config.gridStepsPerJoint = _exploreGridStepsSpin->value ();
    config.mode = _exploreModeCombo->currentIndex () == 1 ?
        WorkspaceSamplingMode::Grid : WorkspaceSamplingMode::RandomUniform;
    config.randomSeed = static_cast< unsigned int > (_exploreSeedSpin->value ());
    config.checkCollision = false;

    const rw::core::Ptr< rw::models::Device > runDevice = selectedDevice ();
    const rw::core::Ptr< const rw::kinematics::Frame > runTcpFrame = selectedTcpFrame ();
    const rw::kinematics::State runState = currentState ();
    const KinematicThresholds runThresholds = _thresholds;
    _explorePlannedSamples = plannedWorkspaceSampleCount (
        config, runDevice->getDOF (), nullptr);
    _exploreCompletedSamples = 0;
    _exploreRunActive = true;
    if (_exploreStateLabel != nullptr)
        _exploreStateLabel->setText (tr ("Estimated: Running"));
    refreshWorkflowControls ();

    struct ExploreRunContext {
        std::shared_ptr< std::atomic_bool > cancelToken;
        QPointer< KinematicAnalysisWidget > widget;
        quint64 sessionGeneration = 0;
    };
    const std::shared_ptr< ExploreRunContext > runContext =
        std::make_shared< ExploreRunContext > ();
    runContext->cancelToken = _exploreCancelToken;
    runContext->widget = this;
    runContext->sessionGeneration = _workcellSessionGeneration;

    WorkspaceSamplingRunCallbacks callbacks;
    callbacks.isCancellationRequested = [] (void* userData) -> bool {
        const ExploreRunContext* context =
            static_cast< const ExploreRunContext* > (userData);
        return context != nullptr && context->cancelToken != nullptr &&
            context->cancelToken->load ();
    };
    callbacks.onProgress = [] (std::size_t completed, std::size_t planned,
                               void* userData) {
        ExploreRunContext* context = static_cast< ExploreRunContext* > (userData);
        if (context == nullptr || context->widget.isNull ())
            return;
        QMetaObject::invokeMethod (
            context->widget.data (),
            [widget = context->widget, session = context->sessionGeneration,
             completed, planned] {
                if (widget.isNull () || widget->_workcellSessionGeneration != session)
                    return;
                widget->updateCapabilityExplorationProgress (
                    static_cast< qulonglong > (completed),
                    static_cast< qulonglong > (planned));
            },
            Qt::QueuedConnection);
    };
    callbacks.userData = runContext.get ();

    const QFuture< std::vector< WorkspaceSample > > future = QtConcurrent::run (
        [runDevice, runTcpFrame, runState, config, runThresholds, callbacks, runContext] () {
            KinematicAnalyzer worker;
            worker.setThresholds (runThresholds);
            return worker.sampleWorkspace (
                runDevice, runTcpFrame, runState, config, nullptr, callbacks);
        });
    _exploreWatcher->setFuture (future);
}

// cancelCapabilityExploration:设置跨线程 atomic 取消标志,worker 在采样循环内
// 检查该标志并尽早退出;UI 立即进入"取消请求中"状态并禁用 Cancel 按钮,
// 防止重复触发。完成信号仍由 handleCapabilityExplorationFinished 统一收尾。
void KinematicAnalysisWidget::cancelCapabilityExploration ()
{
    if (_exploreModeCombo != nullptr && _exploreModeCombo->currentIndex () == 2) {
        if (_exploreStateLabel != nullptr)
            _exploreStateLabel->setText (tr ("Estimated: Cancellation requested"));
        if (_poseCancelButton != nullptr)
            _poseCancelButton->click ();
        refreshWorkflowControls ();
        return;
    }
    if (!_exploreRunActive)
        return;
    _exploreRunActive = false;
    _exploreCancellationRequested = true;
    if (_exploreCancelToken)
        _exploreCancelToken->store (true);
    if (_exploreStateLabel != nullptr)
        _exploreStateLabel->setText (tr ("Estimated: Cancellation requested"));
    refreshWorkflowControls ();
}

// updateCapabilityExplorationProgress:后台线程通过 QMetaObject::invokeMethod
// (Qt::QueuedConnection)跨线程回调到 UI 线程,更新能力探索的进度文本。
void KinematicAnalysisWidget::updateCapabilityExplorationProgress (
    qulonglong completedSamples, qulonglong plannedSamples)
{
    _exploreCompletedSamples = static_cast< std::size_t > (completedSamples);
    _explorePlannedSamples = static_cast< std::size_t > (plannedSamples);
    if (_exploreRunActive && _exploreStateLabel != nullptr)
        _exploreStateLabel->setText (
            tr ("Estimated: Running, %1 / %2 samples")
                .arg (static_cast< qulonglong > (_exploreCompletedSamples))
                .arg (static_cast< qulonglong > (_explorePlannedSamples)));
}

// handleCapabilityExplorationFinished:能力探索 worker 完成信号触发。读取结果、
// 恢复 UI 状态,并根据取消标志区分"正常完成"与"被取消(数据不足)"两种
// 状态文案,同时把工作流按钮恢复到可再次运行的状态。
void KinematicAnalysisWidget::handleCapabilityExplorationFinished ()
{
    if (_exploreWatcher == nullptr)
        return;
    const quint64 runGeneration = _exploreWatcher->property (
        "workcellSessionGeneration").toULongLong ();
    if (runGeneration != _workcellSessionGeneration) {
        _exploreRunActive = false;
        _exploreCancellationRequested = false;
        refreshWorkflowControls ();
        return;
    }
    const std::vector< WorkspaceSample > samples = _exploreWatcher->result ();
    const bool canceled = _exploreCancelToken != nullptr && _exploreCancelToken->load ();
    _exploreCompletedSamples = samples.size ();
    _exploreRunActive = false;
    if (_exploreStateLabel != nullptr) {
        _exploreStateLabel->setText (
            canceled
                ? tr ("Estimated: DataInsufficient, %1 sample(s) completed")
                      .arg (static_cast< qulonglong > (_exploreCompletedSamples))
                : tr ("Estimated: Completed, %1 sample(s)")
                      .arg (static_cast< qulonglong > (_exploreCompletedSamples)));
    }
    refreshWorkflowControls ();
    if (_workcell != nullptr)
        setStatus (canceled
                       ? tr ("Capability exploration canceled after %1 sample(s).")
                             .arg (static_cast< qulonglong > (_exploreCompletedSamples))
                       : tr ("Capability exploration completed with %1 sample(s).")
                             .arg (static_cast< qulonglong > (_exploreCompletedSamples)));
    else
        setStatus (tr ("No WorkCell loaded."));
}

bool KinematicAnalysisWidget::loadFrozenRequirementDocument (const QByteArray& json)
{
    return loadFrozenRequirementDocument (json, std::string ());
}

// loadFrozenRequirementDocument(带工件基准目录的重载):解析冻结工件 JSON → 通过
// FrozenRequirementKinematicAdapter 生成与当前 WorkCell 绑定的 Verified 级执行契约。
// artifactBaseDirectory 用于把工件内的模型路径相对化,使冻结工件被复制 / 移动后
// 仍能定位绑定模型。v3 旧版工件必须先重新冻结(REQ_V3_REQUIRES_REFREEZE)才能校验。
bool KinematicAnalysisWidget::loadFrozenRequirementDocument (
    const QByteArray& json, const std::string& artifactBaseDirectory)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson (json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject ()) {
        setStatus (tr ("Invalid frozen requirement document."));
        return false;
    }

    const QJsonObject root = document.object ();
    QJsonObject artifactObject = root;
    if (root.value (QStringLiteral ("frozenArtifact")).isObject ())
        artifactObject = root.value (QStringLiteral ("frozenArtifact")).toObject ();
    if (artifactObject.value (QStringLiteral ("schemaVersion")).toInt () == 3) {
        setStatus (tr ("REQ_V3_REQUIRES_REFREEZE: v3 artifacts must be refrozen before Verified validation."));
        return false;
    }

    FrozenRequirementArtifact artifact;
    std::string error;
    if (!FrozenRequirementKinematicAdapter::parseArtifactJson (root, artifact, &error)) {
        setStatus (QString::fromStdString (error));
        return false;
    }
    if (_workcell == nullptr) {
        setStatus (tr ("No WorkCell loaded."));
        return false;
    }

    RequirementExecutionSet execution;
    if (!FrozenRequirementKinematicAdapter::applyExecutionSet (
            artifact, *_workcell, currentState (), AnalysisEvidenceStage::Verified,
            execution, &error, nullptr, nullptr, artifactBaseDirectory)) {
        setStatus (QString::fromStdString (error));
        return false;
    }
    _validateExecution = execution;
    _validateExecutionSet = true;
    _validateHasResults = false;
    if (_validateRequirementStateLabel != nullptr)
        _validateRequirementStateLabel->setText (
            tr ("Frozen v4 execution contract loaded (not yet validated)."));
    populateFrozenRequirementSources ();
    if (_validateProvenanceLabel != nullptr)
        _validateProvenanceLabel->setText (
            tr ("Requirement: %1\nModel: %2\nEnvironment: %3")
                .arg (QString::fromStdString (_validateExecution.provenance.requirementFingerprint))
                .arg (QString::fromStdString (_validateExecution.provenance.robotModelFingerprint))
                .arg (QString::fromStdString (_validateExecution.provenance.environmentFingerprint)));
    setStatus (tr ("Frozen requirement execution contract loaded."));
    refreshWorkflowControls ();
    return true;
}

// setRequirementExecutionDocument:直接接受 RequirementExecutionSet 的 JSON 对象
// (由项目 Provider 注入),不经过工件文件选择与解析;用于恢复已经序列化的执行契约。
bool KinematicAnalysisWidget::setRequirementExecutionDocument (const QJsonObject& json)
{
    RequirementExecutionSet execution;
    std::string error;
    if (!RequirementExecutionJson::fromObject (json, execution, &error)) {
        setStatus (QString::fromStdString (error));
        return false;
    }
    _validateExecution = execution;
    _validateExecutionSet = true;
    _validateHasResults = false;
    if (_validateRequirementStateLabel != nullptr)
        _validateRequirementStateLabel->setText (
            tr ("Frozen v4 execution contract loaded (not yet validated)."));
    populateFrozenRequirementSources ();
    if (_validateProvenanceLabel != nullptr)
        _validateProvenanceLabel->setText (
            tr ("Requirement: %1\nModel: %2\nEnvironment: %3")
                .arg (QString::fromStdString (_validateExecution.provenance.requirementFingerprint))
                .arg (QString::fromStdString (_validateExecution.provenance.robotModelFingerprint))
                .arg (QString::fromStdString (_validateExecution.provenance.environmentFingerprint)));
    setStatus (tr ("Frozen requirement execution contract loaded."));
    refreshWorkflowControls ();
    return true;
}

// validateRequirements:对冻结执行契约做 Verified 级一致性校验(批量分析入口)。
//   - 组装 AnalysisContext:workcell / device / tcp / 碰撞检测器 / 模型与环境指纹 /
//     阈值;碰撞检测对个体执行条目而言是可选能力,因此这里 collisionRequired 置 false,
//     避免可选的任务 / 区域检查因缺少检测器而整体退化为 DataInsufficient;
//   - analyzer.validateRequirements 逐个评估全部 Must / Should / Info 任务;
//   - RegionCoverageEvaluator 对每个 workspaceRegion 做网格单元级覆盖评估
//     (逐单元采样方向并统计可达方向数 / 可行性 / 质量);
//   - 合并进 _validateSummary,并把任务与区域单元结果分别写回两张结果表。
// populateFrozenRequirementSources:加载冻结工件后,把任务与区域先以
// "Not evaluated / Unvalidated" 占位行刷进结果表,让用户看到工件内容,
// 但明确尚未跑校验;真正结果由 validateRequirements 覆盖写入。
void KinematicAnalysisWidget::populateFrozenRequirementSources ()
{
    if (_validateTaskResultTable != nullptr) {
        _validateTaskResultTable->setRowCount (0);
        for (const RequirementExecutionTask& task : _validateExecution.tasks) {
            const int row = _validateTaskResultTable->rowCount ();
            _validateTaskResultTable->insertRow (row);
            QTableWidgetItem* idItem = makeItem (QString::fromStdString (task.id));
            idItem->setData (Qt::UserRole, idItem->text ());
            _validateTaskResultTable->setItem (row, 0, idItem);
            _validateTaskResultTable->setItem (row, 1, makeItem (
                QString::fromStdString (task.name) + QStringLiteral ("\n-")));
            _validateTaskResultTable->setItem (row, 2, makeItem (tr ("Not evaluated")));
            _validateTaskResultTable->setItem (row, 3, makeItem (QStringLiteral ("-\n-")));
            _validateTaskResultTable->setItem (row, 4, makeItem (tr ("Unvalidated")));
            _validateTaskResultTable->setItem (row, 5, makeItem (
                task.level == RequirementExecutionLevel::Must ? "Must" :
                task.level == RequirementExecutionLevel::Should ? "Should" : "Info"));
        }
    }
    if (_validateRegionCellTable != nullptr)
        _validateRegionCellTable->setRowCount (0);
    if (_validateRegionSummaryTable != nullptr) {
        _validateRegionSummaryTable->setRowCount (0);
        for (const RequirementExecutionRegion& region : _validateExecution.workspaceRegions) {
            const int row = _validateRegionSummaryTable->rowCount ();
            _validateRegionSummaryTable->insertRow (row);
            QTableWidgetItem* idItem = makeItem (QString::fromStdString (region.id));
            idItem->setData (Qt::UserRole, idItem->text ());
            _validateRegionSummaryTable->setItem (row, 0, idItem);
            _validateRegionSummaryTable->setItem (row, 1, makeItem (
                region.level == RequirementExecutionLevel::Must ? QStringLiteral ("Must") :
                region.level == RequirementExecutionLevel::Should ? QStringLiteral ("Should") :
                    QStringLiteral ("Info")));
            _validateRegionSummaryTable->setItem (row, 2, makeItem (QStringLiteral ("-")));
            _validateRegionSummaryTable->setItem (row, 3, makeItem (QStringLiteral ("-")));
            _validateRegionSummaryTable->setItem (row, 4, makeItem (tr ("Not evaluated")));
            _validateRegionSummaryTable->setItem (row, 5, makeItem (QStringLiteral ("-")));
        }
        _validateRegionSummaryTable->resizeRowsToContents ();
    }
}

// validateRequirements:对冻结执行契约做 Verified 级一致性校验(批量分析入口)。
//   - 组装 AnalysisContext(workcell / device / tcp / 碰撞检测器 / 模型与环境指纹 /
//     阈值),碰撞对个体执行条目而言是可选能力,因此 collisionRequired 置 false,
//     避免可选任务 / 区域检查因缺少检测器而整体退化为 DataInsufficient;
//   - analyzer.validateRequirements 逐个评估全部 Must / Should / Info 任务;
//   - RegionCoverageEvaluator 对每个 workspaceRegion 做网格单元级覆盖评估;
//   - 合并进 _validateSummary,并把任务与区域单元结果分别写回两张结果表。
void KinematicAnalysisWidget::validateRequirements ()
{
    const rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    const rw::core::Ptr< rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();
    if (_workcell == nullptr || device == nullptr || tcpFrame == nullptr || !_validateExecutionSet) {
        refreshWorkflowControls ();
        return;
    }

    AnalysisContextInput input;
    input.workcell = rw::core::Ptr< rw::models::WorkCell > (_workcell);
    input.device = device;
    input.tcpFrame = tcpFrame;
    input.baseState = currentState ();
    input.collisionDetector = collisionDetectorForAnalysis (true, nullptr);
    input.deviceName = device->getName ();
    input.tcpFrameName = tcpFrame->getName ();
    input.modelFingerprint = _validateExecution.provenance.robotModelFingerprint.empty ()
        ? input.deviceName : _validateExecution.provenance.robotModelFingerprint;
    input.environmentFingerprint = _validateExecution.provenance.environmentFingerprint.empty ()
        ? QStringLiteral ("current-workcell").toStdString ()
        : _validateExecution.provenance.environmentFingerprint;
    input.thresholds = _thresholds;
    // Individual execution entries own the hard collision requirement. The
    // detector is an optional capability here; it must not turn optional
    // task/region checks into a global DataInsufficient result.
    input.collisionRequired = false;
    AnalysisContext context;
    std::string contextError;
    if (!makeAnalysisContext (input, context, &contextError)) {
        setStatus (QString::fromStdString (contextError));
        return;
    }

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    const RequirementValidationSummary taskSummary =
        analyzer.validateRequirements (context, _validateExecution);

    RegionCoverageEvaluator regionEvaluator;
    std::vector< RegionCoverageResult > regionResults;
    regionResults.reserve (_validateExecution.workspaceRegions.size ());
    for (const RequirementExecutionRegion& region : _validateExecution.workspaceRegions) {
        regionResults.push_back (regionEvaluator.evaluate (context, region));
    }
    if (_validateRegionSummaryTable != nullptr) {
        _validateRegionSummaryTable->setRowCount (0);
        for (std::size_t i = 0; i < regionResults.size (); ++i) {
            const RegionCoverageResult& result = regionResults[i];
            const RequirementExecutionRegion* requirement =
                i < _validateExecution.workspaceRegions.size () ?
                    &_validateExecution.workspaceRegions[i] : nullptr;
            const int row = _validateRegionSummaryTable->rowCount ();
            _validateRegionSummaryTable->insertRow (row);
            QTableWidgetItem* idItem = makeItem (
                QString::fromStdString (requirement != nullptr ? requirement->id : result.regionId));
            idItem->setData (Qt::UserRole, idItem->text ());
            _validateRegionSummaryTable->setItem (row, 0, idItem);
            _validateRegionSummaryTable->setItem (row, 1, makeItem (
                requirement == nullptr ? QString () :
                    requirement->level == RequirementExecutionLevel::Must ? QStringLiteral ("Must") :
                    requirement->level == RequirementExecutionLevel::Should ? QStringLiteral ("Should") :
                        QStringLiteral ("Info")));
            _validateRegionSummaryTable->setItem (row, 2, makeItem (
                QString::number (100.0 * result.positionCoverage, 'f', 1) + QStringLiteral ("%")));
            _validateRegionSummaryTable->setItem (row, 3, makeItem (
                QString::number (100.0 * result.orientationCoverage, 'f', 1) + QStringLiteral ("%")));
            _validateRegionSummaryTable->setItem (row, 4, makeItem (
                QString::fromLatin1 (rws::toString (result.feasibility))));
            _validateRegionSummaryTable->setItem (row, 5, makeItem (
                QString::fromLatin1 (rws::toString (result.quality))));
        }
    }
    if (_validateOrientationProbeLabel != nullptr) {
        const RequirementExecutionRegion* first = _validateExecution.workspaceRegions.empty () ?
            nullptr : &_validateExecution.workspaceRegions.front ();
        _validateOrientationProbeLabel->setText (
            first == nullptr ? tr ("Orientation probes: no frozen region loaded.") :
                tr ("Orientation probes: directions %1, rolls %2.")
                    .arg (first->directionSamples).arg (first->rollSamples));
    }
    _validateSummary = buildRequirementValidationSummary (
        _validateExecution, taskSummary.taskResults, regionResults);

    if (_validateTaskResultTable != nullptr) {
        _validateTaskResultTable->setRowCount (0);
        for (std::size_t i = 0; i < taskSummary.taskResults.size (); ++i) {
            const TargetEvaluation& result = taskSummary.taskResults[i];
            const int row = _validateTaskResultTable->rowCount ();
            _validateTaskResultTable->insertRow (row);
            const RequirementExecutionTask* requirement =
                i < _validateExecution.tasks.size () ? &_validateExecution.tasks[i] : nullptr;
            QTableWidgetItem* idItem = makeItem (
                QString::fromStdString (requirement != nullptr ? requirement->id : result.target.id));
            idItem->setData (Qt::UserRole, idItem->text ());
            _validateTaskResultTable->setItem (row, 0, idItem);
            _validateTaskResultTable->setItem (row, 1, makeItem (
                QString::fromStdString (requirement != nullptr ? requirement->name : result.target.name) +
                QStringLiteral ("\n") + targetResidualText (result)));
            _validateTaskResultTable->setItem (row, 2, makeItem (
                QString::fromLatin1 (rws::toString (result.feasibility))));
            _validateTaskResultTable->setItem (row, 3, makeItem (
                QString::fromLatin1 (rws::toString (result.quality)) +
                QStringLiteral ("\n") + targetPoseCoverageText (result)));
            _validateTaskResultTable->setItem (row, 4, makeItem (
                QString::fromLatin1 (rws::toString (result.stage))));
            _validateTaskResultTable->setItem (row, 5, makeItem (
                requirement == nullptr ? QString () : QString::fromLatin1 (
                    requirement->level == RequirementExecutionLevel::Must ? "Must" :
                    requirement->level == RequirementExecutionLevel::Should ? "Should" : "Info")));
        }
        _validateTaskResultTable->resizeRowsToContents ();
    }
    if (_validateRegionCellTable != nullptr) {
        _validateRegionCellTable->setRowCount (0);
        for (const RegionCoverageResult& result : regionResults) {
            for (const RegionCellResult& cell : result.cells) {
                const int row = _validateRegionCellTable->rowCount ();
                _validateRegionCellTable->insertRow (row);
                QTableWidgetItem* idItem = makeItem (QString::fromStdString (result.regionId));
                idItem->setData (Qt::UserRole, idItem->text ());
                _validateRegionCellTable->setItem (row, 0, idItem);
                _validateRegionCellTable->setItem (row, 1, makeItem (cell.position[0]));
                _validateRegionCellTable->setItem (row, 2, makeItem (cell.position[1]));
                _validateRegionCellTable->setItem (row, 3, makeItem (cell.position[2]));
                _validateRegionCellTable->setItem (row, 4, makeItem (
                    QStringLiteral ("%1,%2,%3").arg (cell.index[0]).arg (cell.index[1]).arg (cell.index[2])));
                _validateRegionCellTable->setItem (row, 5, makeItem (
                    QString::fromLatin1 (rws::toString (cell.feasibility))));
                _validateRegionCellTable->setItem (row, 6, makeItem (
                    QString::fromLatin1 (rws::toString (cell.quality))));
                _validateRegionCellTable->setItem (row, 7, makeItem (
                    QString::fromLatin1 (rws::toString (result.stage))));
                _validateRegionCellTable->setItem (row, 8, makeItem (
                    QString::number (cell.reachableOrientationCount)));
            }
        }
    }
    _validateHasResults = true;
    if (_validateRequirementStateLabel != nullptr)
        _validateRequirementStateLabel->setText (
            tr ("Validation: %1 / %2 / %3")
                .arg (QString::fromLatin1 (rws::toString (_validateSummary.feasibility)))
                .arg (QString::fromLatin1 (rws::toString (_validateSummary.quality)))
                .arg (QString::fromLatin1 (rws::toString (_validateSummary.stage))));
    setStatus (tr ("Frozen requirements validated."));
    refreshWorkflowControls ();
}

// validateSelectedMode2Source:只校验"当前选中"的条目,避免全量重跑。
// 本地任务源转发给 analyzeSelectedTaskPoints;冻结需求源则根据选中的是任务
// 还是区域,单条构造 RequirementExecutionSet 分别走任务校验 / 区域覆盖评估,
// 并把其他条目从结果表清空,使视图只反映本次选中的评估结果。
void KinematicAnalysisWidget::validateSelectedMode2Source ()
{
    if (_mode2DataSourceCombo == nullptr || _mode2DataSourceCombo->currentIndex () == 0) {
        _validateHasResults = false;
        _validateSummary = RequirementValidationSummary ();
        analyzeSelectedTaskPoints ();
        return;
    }
    if (_workcell == nullptr || !_validateExecutionSet)
        return;

    QString selectedTaskId;
    if (_validateTaskResultTable != nullptr &&
        _validateTaskResultTable->selectionModel () != nullptr) {
        const QModelIndexList rows =
            _validateTaskResultTable->selectionModel ()->selectedRows ();
        if (!rows.isEmpty ()) {
            QTableWidgetItem* item = _validateTaskResultTable->item (rows.front ().row (), 0);
            selectedTaskId = item == nullptr ? QString () : item->data (Qt::UserRole).toString ();
        }
    }
    QString selectedRegionId;
    if (selectedTaskId.isEmpty () && _validateRegionSummaryTable != nullptr &&
        _validateRegionSummaryTable->selectionModel () != nullptr) {
        const QModelIndexList rows =
            _validateRegionSummaryTable->selectionModel ()->selectedRows ();
        if (!rows.isEmpty ()) {
            QTableWidgetItem* item = _validateRegionSummaryTable->item (rows.front ().row (), 0);
            selectedRegionId = item == nullptr ? QString () : item->data (Qt::UserRole).toString ();
        }
    }
    if (selectedTaskId.isEmpty () && selectedRegionId.isEmpty ()) {
        setStatus (tr ("Select a frozen task or region to validate."));
        return;
    }

    const rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    const rw::core::Ptr< rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();
    if (device == nullptr || tcpFrame == nullptr) {
        refreshWorkflowControls ();
        return;
    }
    AnalysisContextInput input;
    input.workcell = rw::core::Ptr< rw::models::WorkCell > (_workcell);
    input.device = device;
    input.tcpFrame = tcpFrame;
    input.baseState = currentState ();
    input.collisionDetector = collisionDetectorForAnalysis (true, nullptr);
    input.deviceName = device->getName ();
    input.tcpFrameName = tcpFrame->getName ();
    input.modelFingerprint = _validateExecution.provenance.robotModelFingerprint.empty () ?
        input.deviceName : _validateExecution.provenance.robotModelFingerprint;
    input.environmentFingerprint = _validateExecution.provenance.environmentFingerprint.empty () ?
        QStringLiteral ("current-workcell").toStdString () :
        _validateExecution.provenance.environmentFingerprint;
    input.thresholds = _thresholds;
    input.collisionRequired = false;
    AnalysisContext context;
    std::string contextError;
    if (!makeAnalysisContext (input, context, &contextError)) {
        setStatus (QString::fromStdString (contextError));
        return;
    }

    if (!selectedTaskId.isEmpty ()) {
        const std::string id = selectedTaskId.toStdString ();
        const auto found = std::find_if (_validateExecution.tasks.begin (),
                                        _validateExecution.tasks.end (),
                                        [&id] (const RequirementExecutionTask& task) {
                                            return task.id == id;
                                        });
        if (found == _validateExecution.tasks.end ()) {
            setStatus (tr ("Selected frozen task is no longer available."));
            return;
        }
        RequirementExecutionSet selected;
        selected.provenance = _validateExecution.provenance;
        selected.tasks.push_back (*found);
        KinematicAnalyzer analyzer;
        analyzer.setThresholds (_thresholds);
        const RequirementValidationSummary summary = analyzer.validateRequirements (context, selected);
        if (_validateTaskResultTable != nullptr) {
            _validateTaskResultTable->setRowCount (0);
            if (!summary.taskResults.empty ()) {
                const TargetEvaluation& result = summary.taskResults.front ();
                _validateTaskResultTable->insertRow (0);
                QTableWidgetItem* idItem = makeItem (selectedTaskId);
                idItem->setData (Qt::UserRole, selectedTaskId);
                _validateTaskResultTable->setItem (0, 0, idItem);
                _validateTaskResultTable->setItem (0, 1, makeItem (
                    QString::fromStdString (found->name) + QStringLiteral ("\n") +
                    targetResidualText (result)));
                _validateTaskResultTable->setItem (0, 2, makeItem (
                    QString::fromLatin1 (rws::toString (result.feasibility))));
                _validateTaskResultTable->setItem (0, 3, makeItem (
                    QString::fromLatin1 (rws::toString (result.quality)) + QStringLiteral ("\n") +
                    targetPoseCoverageText (result)));
                _validateTaskResultTable->setItem (0, 4, makeItem (
                    QString::fromLatin1 (rws::toString (result.stage))));
                _validateTaskResultTable->setItem (0, 5, makeItem (
                    found->level == RequirementExecutionLevel::Must ? "Must" :
                    found->level == RequirementExecutionLevel::Should ? "Should" : "Info"));
                _validateTaskResultTable->resizeRowsToContents ();
            }
        }
        if (_validateRegionCellTable != nullptr)
            _validateRegionCellTable->setRowCount (0);
        if (_validateRegionSummaryTable != nullptr)
            _validateRegionSummaryTable->setRowCount (0);
        _validateSummary = buildRequirementValidationSummary (
            selected, summary.taskResults, std::vector< RegionCoverageResult > ());
        _validateHasResults = true;
        if (_validateRequirementStateLabel != nullptr)
            _validateRequirementStateLabel->setText (
                tr ("Validation: %1 / %2 / %3")
                    .arg (QString::fromLatin1 (rws::toString (_validateSummary.feasibility)))
                    .arg (QString::fromLatin1 (rws::toString (_validateSummary.quality)))
                    .arg (QString::fromLatin1 (rws::toString (_validateSummary.stage))));
        setStatus (tr ("Validated selected frozen task %1.").arg (selectedTaskId));
    }
    else {
        const std::string id = selectedRegionId.toStdString ();
        const auto found = std::find_if (_validateExecution.workspaceRegions.begin (),
                                        _validateExecution.workspaceRegions.end (),
                                        [&id] (const RequirementExecutionRegion& region) {
                                            return region.id == id;
                                        });
        if (found == _validateExecution.workspaceRegions.end ()) {
            setStatus (tr ("Selected frozen region is no longer available."));
            return;
        }
        RegionCoverageEvaluator evaluator;
        const RegionCoverageResult result = evaluator.evaluate (context, *found);
        if (_validateTaskResultTable != nullptr)
            _validateTaskResultTable->setRowCount (0);
        if (_validateRegionSummaryTable != nullptr) {
            _validateRegionSummaryTable->setRowCount (0);
            _validateRegionSummaryTable->insertRow (0);
            QTableWidgetItem* idItem = makeItem (selectedRegionId);
            idItem->setData (Qt::UserRole, selectedRegionId);
            _validateRegionSummaryTable->setItem (0, 0, idItem);
            _validateRegionSummaryTable->setItem (0, 1, makeItem (
                found->level == RequirementExecutionLevel::Must ? QStringLiteral ("Must") :
                found->level == RequirementExecutionLevel::Should ? QStringLiteral ("Should") :
                    QStringLiteral ("Info")));
            _validateRegionSummaryTable->setItem (0, 2, makeItem (
                QString::number (100.0 * result.positionCoverage, 'f', 1) + QStringLiteral ("%")));
            _validateRegionSummaryTable->setItem (0, 3, makeItem (
                QString::number (100.0 * result.orientationCoverage, 'f', 1) + QStringLiteral ("%")));
            _validateRegionSummaryTable->setItem (0, 4, makeItem (
                QString::fromLatin1 (rws::toString (result.feasibility))));
            _validateRegionSummaryTable->setItem (0, 5, makeItem (
                QString::fromLatin1 (rws::toString (result.quality))));
            _validateRegionSummaryTable->resizeRowsToContents ();
        }
        if (_validateRegionCellTable != nullptr) {
            _validateRegionCellTable->setRowCount (0);
            for (const RegionCellResult& cell : result.cells) {
                const int row = _validateRegionCellTable->rowCount ();
                _validateRegionCellTable->insertRow (row);
                QTableWidgetItem* idItem = makeItem (selectedRegionId);
                idItem->setData (Qt::UserRole, selectedRegionId);
                _validateRegionCellTable->setItem (row, 0, idItem);
                _validateRegionCellTable->setItem (row, 1, makeItem (cell.position[0]));
                _validateRegionCellTable->setItem (row, 2, makeItem (cell.position[1]));
                _validateRegionCellTable->setItem (row, 3, makeItem (cell.position[2]));
                _validateRegionCellTable->setItem (row, 4, makeItem (
                    QStringLiteral ("%1,%2,%3").arg (cell.index[0]).arg (cell.index[1])
                                                 .arg (cell.index[2])));
                _validateRegionCellTable->setItem (row, 5, makeItem (
                    QString::fromLatin1 (rws::toString (cell.feasibility))));
                _validateRegionCellTable->setItem (row, 6, makeItem (
                    QString::fromLatin1 (rws::toString (cell.quality))));
                _validateRegionCellTable->setItem (row, 7, makeItem (
                    QString::fromLatin1 (rws::toString (result.stage))));
                _validateRegionCellTable->setItem (row, 8, makeItem (
                    QString::number (cell.reachableOrientationCount)));
            }
        }
        RequirementExecutionSet selected;
        selected.provenance = _validateExecution.provenance;
        selected.workspaceRegions.push_back (*found);
        _validateSummary = buildRequirementValidationSummary (
            selected, std::vector< TargetEvaluation > (),
            std::vector< RegionCoverageResult > {result});
        _validateHasResults = true;
        if (_validateRequirementStateLabel != nullptr)
            _validateRequirementStateLabel->setText (
                tr ("Validation: %1 / %2 / %3")
                    .arg (QString::fromLatin1 (rws::toString (_validateSummary.feasibility)))
                    .arg (QString::fromLatin1 (rws::toString (_validateSummary.quality)))
                    .arg (QString::fromLatin1 (rws::toString (_validateSummary.stage))));
        setStatus (tr ("Validated selected frozen region %1.").arg (selectedRegionId));
    }
    refreshWorkflowControls ();
}

// 将当前 Widget 的可编辑内容收集为项目配置。这里故意不读取 _lastIkResult、workspace
// samples 或 pose reachability samples：这些数据可以由配置和当前 WorkCell 重新计算，且体积
// 可能远大于项目定义；把它们写入项目会造成陈旧结果和不可控的文件膨胀。
QByteArray KinematicAnalysisWidget::projectDocumentSnapshot () const
{
    KinematicAnalysisProjectSettings settings;
    settings.deviceName = _deviceCombo == nullptr ? QString () : _deviceCombo->currentText ();
    settings.tcpFrameName = _tcpFrameCombo == nullptr ? QString () : _tcpFrameCombo->currentText ();
    settings.ikPositionMeters = {{ikXInputMeters (), ikYInputMeters (), ikZInputMeters ()}};
    settings.ikRpyDeg = {{ikRollInputDeg (), ikPitchInputDeg (), ikYawInputDeg ()}};
    settings.ikDuplicateQThreshold = _thresholds.ikDuplicateQThreshold;
    settings.ikCollisionCheck = _ikCollisionCheck->isChecked ();
    settings.lengthUnit = _lengthUnit;
    settings.angleUnit = _angleUnit;
    settings.workspace.mode = _workspaceModeCombo->currentIndex () == 1 ?
        WorkspaceSamplingMode::Grid : WorkspaceSamplingMode::RandomUniform;
    settings.workspace.sampleCount = _workspaceSampleCountSpin->value ();
    settings.workspace.gridStepsPerJoint = _workspaceGridStepsSpin->value ();
    settings.workspace.randomSeed = static_cast< unsigned int > (_workspaceSeedSpin->value ());
    settings.workspace.checkCollision = _workspaceCollisionCheck->isChecked ();
    settings.workspaceColorMode = static_cast< WorkspaceColorMode > (
        _workspaceColorModeCombo->currentIndex ());
    settings.poseReachability.directionSamples = _poseDirectionSamplesSpin->value ();
    settings.poseReachability.rollSamples = _poseRollSamplesSpin->value ();
    settings.poseReachability.checkCollision = _poseCollisionCheck->isChecked ();
    settings.poseTaskPointsSource = _poseTaskPointsSourceButton->isChecked ();
    for (int row = 0; row < _posePositionTable->rowCount (); ++row) {
        std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
        for (int column = 0; column < 3; ++column) {
            const QTableWidgetItem* item = _posePositionTable->item (row, column);
            bool ok = false;
            const double stored = item == nullptr ? 0.0 :
                item->data (Qt::UserRole).toDouble (&ok);
            position[static_cast< std::size_t > (column)] = ok ? stored :
                metersFromDisplayLength (
                    item == nullptr ? 0.0 : item->text ().toDouble (), _lengthUnit);
        }
        settings.manualPosePositions.push_back (position);
    }
    settings.visualSource = static_cast< VisualPointSource > (_visualSourceCombo->currentData ().toInt ());
    settings.visualProjection = static_cast< VisualProjection > (_visualProjectionCombo->currentData ().toInt ());
    settings.visualScalarMode = static_cast< VisualScalarMode > (_visualColorModeCombo->currentData ().toInt ());
    settings.visualRenderMode = static_cast< VisualRenderMode > (_visualRenderModeCombo->currentData ().toInt ());
    settings.envelopeDirections = _visualEnvelopeDirectionsSpin->value ();
    settings.showPass = _visualShowPassCheck->isChecked ();
    settings.showWarning = _visualShowWarningCheck->isChecked ();
    settings.showFail = _visualShowFailCheck->isChecked ();
    settings.showUnknown = _visualShowUnknownCheck->isChecked ();
    settings.showLabels = _visualShowLabelsCheck->isChecked ();
    settings.showGrid = _visualShowGridCheck->isChecked ();
    settings.showLegend = _visualShowLegendCheck->isChecked ();
    settings.pointSize = _visualPointSizeSpin->value ();
    settings.thresholds = _thresholds;
    settings.ikDuplicateQThreshold = settings.thresholds.ikDuplicateQThreshold;
    if (_taskPointModel != nullptr)
        settings.taskPoints = _taskPointModel->taskPoints ();
    return KinematicAnalysisProjectDocument::toJson (settings);
}

// 把项目 JSON 恢复到控件。所有控件变更信号在 _applyingProjectDocument 期间都会被抑制，
// 防止一次加载被误判为用户编辑；结果缓存则统一清空，避免新项目继续显示旧 WorkCell 的结果。
void KinematicAnalysisWidget::applyProjectDocumentSnapshot (const QByteArray& json, QString* error)
{
    KinematicAnalysisProjectSettings settings;
    if (!KinematicAnalysisProjectDocument::fromJson (json, settings, error))
        return;

    _applyingProjectDocument = true;
    const int deviceIndex = _deviceCombo->findText (settings.deviceName);
    if (deviceIndex >= 0)
        _deviceCombo->setCurrentIndex (deviceIndex);
    populateTcpFrames ();
    const int tcpIndex = _tcpFrameCombo->findText (settings.tcpFrameName);
    if (tcpIndex >= 0)
        _tcpFrameCombo->setCurrentIndex (tcpIndex);
    const int lengthIndex = _lengthUnitCombo->findData (static_cast< int > (settings.lengthUnit));
    const int angleIndex = _angleUnitCombo->findData (static_cast< int > (settings.angleUnit));
    if (lengthIndex >= 0)
        _lengthUnitCombo->setCurrentIndex (lengthIndex);
    if (angleIndex >= 0)
        _angleUnitCombo->setCurrentIndex (angleIndex);
    updateUnitDisplay ();
    setIkPoseMetersDeg (settings.ikPositionMeters, settings.ikRpyDeg);
    _ikDuplicateQThresholdSpin->setValue (settings.ikDuplicateQThreshold);
    _ikCollisionCheck->setChecked (settings.ikCollisionCheck);

    _thresholds = settings.thresholds;
    _thresholdNearLimitSpin->setValue (_thresholds.nearJointLimitRatio);
    _thresholdConditionWarningSpin->setValue (_thresholds.conditionWarning);
    _thresholdConditionFailSpin->setValue (_thresholds.conditionFail);
    _thresholdSingularValueSpin->setValue (_thresholds.singularValueWarning);
    _thresholdManipulabilitySpin->setValue (_thresholds.manipulabilityWarning);
    updateUnitDisplay ();

    _workspaceModeCombo->setCurrentIndex (
        settings.workspace.mode == WorkspaceSamplingMode::Grid ? 1 : 0);
    _workspaceSampleCountSpin->setValue (settings.workspace.sampleCount);
    _workspaceGridStepsSpin->setValue (settings.workspace.gridStepsPerJoint);
    _workspaceSeedSpin->setValue (static_cast< int > (settings.workspace.randomSeed));
    _workspaceCollisionCheck->setChecked (settings.workspace.checkCollision);
    _workspaceColorModeCombo->setCurrentIndex (static_cast< int > (settings.workspaceColorMode));
    _poseDirectionSamplesSpin->setValue (settings.poseReachability.directionSamples);
    _poseRollSamplesSpin->setValue (settings.poseReachability.rollSamples);
    _poseCollisionCheck->setChecked (settings.poseReachability.checkCollision);
    _poseTaskPointsSourceButton->setChecked (settings.poseTaskPointsSource);
    _poseManualSourceButton->setChecked (!settings.poseTaskPointsSource);
    _posePositionTable->setRowCount (0);
    for (const auto& position : settings.manualPosePositions) {
        const int row = _posePositionTable->rowCount ();
        _posePositionTable->insertRow (row);
        for (int column = 0; column < 3; ++column) {
            QTableWidgetItem* item = new QTableWidgetItem ();
            item->setData (Qt::UserRole, position[static_cast< std::size_t > (column)]);
            item->setText (QString::number (displayLengthFromMeters (
                position[static_cast< std::size_t > (column)], _lengthUnit), 'g', 12));
            _posePositionTable->setItem (row, column, item);
        }
    }
    _visualSourceCombo->setCurrentIndex (_visualSourceCombo->findData (
        static_cast< int > (settings.visualSource)));
    _visualProjectionCombo->setCurrentIndex (_visualProjectionCombo->findData (
        static_cast< int > (settings.visualProjection)));
    updateVisualizationControls ();
    _visualColorModeCombo->setCurrentIndex (_visualColorModeCombo->findData (
        static_cast< int > (settings.visualScalarMode)));
    _visualRenderModeCombo->setCurrentIndex (_visualRenderModeCombo->findData (
        static_cast< int > (settings.visualRenderMode)));
    _visualEnvelopeDirectionsSpin->setValue (settings.envelopeDirections);
    _visualShowPassCheck->setChecked (settings.showPass);
    _visualShowWarningCheck->setChecked (settings.showWarning);
    _visualShowFailCheck->setChecked (settings.showFail);
    _visualShowUnknownCheck->setChecked (settings.showUnknown);
    _visualShowLabelsCheck->setChecked (settings.showLabels);
    _visualShowGridCheck->setChecked (settings.showGrid);
    _visualShowLegendCheck->setChecked (settings.showLegend);
    _visualPointSizeSpin->setValue (settings.pointSize);
    if (_taskPointModel != nullptr)
        _taskPointModel->setRowsFromTaskPoints (settings.taskPoints);
    _lastIkResult = KinematicIkAnalysisResult ();
    _lastTaskPointResults.clear ();
    _workspaceSamples.clear ();
    _poseReachabilitySamples.clear ();
    _taskPointModel->clearAllResults ();
    _workspaceTable->setRowCount (0);
    _poseResultTable->setRowCount (0);
    updateWorkspaceControls ();
    updatePoseReachabilityControls ();
    refreshVisualization ();
    refreshReport ();
    _applyingProjectDocument = false;
}

// loadProjectDocument:从文件加载 KinematicAnalysis 项目配置。成功时记录项目
// 路径并把当前配置快照作为脏比较基线;失败时通过 error 返回可读原因。
bool KinematicAnalysisWidget::loadProjectDocument (const QString& path, QString* error)
{
    QFile file (path);
    if (!file.open (QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr)
            *error = QStringLiteral ("无法读取 KinematicAnalysis 项目文档：%1。")
                         .arg (file.errorString ());
        return false;
    }
    const QByteArray json = file.readAll ();
    QString applyError;
    applyProjectDocumentSnapshot (json, &applyError);
    if (!applyError.isEmpty ()) {
        if (error != nullptr)
            *error = applyError;
        return false;
    }
    _projectDocumentPath = QFileInfo (path).absoluteFilePath ();
    _savedProjectDocumentSnapshot = projectDocumentSnapshot ();
    _pendingProjectDocumentSnapshot.clear ();
    return true;
}

// saveProjectDocument:把当前可编辑配置快照原子写入目标路径(QSaveFile 先写临时文件
// 再 commit),并把写入内容缓存为 pending 快照;正式提交由 markProjectDocumentClean
// 确认,若之后用户又修改,脏比较仍基于最新基线。
bool KinematicAnalysisWidget::saveProjectDocument (const QString& targetPath, QString* error)
{
    const QByteArray json = projectDocumentSnapshot ();
    QSaveFile file (targetPath);
    if (!file.open (QIODevice::WriteOnly | QIODevice::Text) ||
        file.write (json) != json.size () || !file.commit ()) {
        if (error != nullptr)
            *error = QStringLiteral ("无法暂存 KinematicAnalysis 项目文档：%1。")
                         .arg (file.errorString ());
        return false;
    }
    _pendingProjectDocumentSnapshot = json;
    return true;
}

// isProjectDocumentDirty:当前快照与保存基线不一致即为脏。仅当 Widget 已绑定项目
// 路径(_projectDocumentPath 非空)时才参与脏判断,避免未纳入项目的临时 UI 状态误报。
bool KinematicAnalysisWidget::isProjectDocumentDirty () const
{
    return !_projectDocumentPath.isEmpty () &&
        projectDocumentSnapshot () != _savedProjectDocumentSnapshot;
}

// markProjectDocumentClean:正式提交完成后调用,把 pending 快照提升为新的保存基线,
// 使下一次脏比较从当前内容重新起算。
void KinematicAnalysisWidget::markProjectDocumentClean ()
{
    if (!_pendingProjectDocumentSnapshot.isEmpty ()) {
        _savedProjectDocumentSnapshot = _pendingProjectDocumentSnapshot;
        _pendingProjectDocumentSnapshot.clear ();
    }
}

// canCloseProjectDocument:关闭 / 切换项目前的安全检查。若有任何后台分析
// (工作空间 / 位姿可达性 / 包络)仍在运行,则拒绝关闭并给出原因,防止
// worker 完成后把结果写回已销毁 / 已切换的上下文。
bool KinematicAnalysisWidget::canCloseProjectDocument (QString* reason) const
{
    const bool running = _exploreRunActive || _workspaceRunActive || _poseReachabilityRunActive ||
        (_exploreWatcher != nullptr && _exploreWatcher->isRunning ()) || _envelopeRunActive ||
        (_workspaceWatcher != nullptr && _workspaceWatcher->isRunning ()) ||
        (_poseReachabilityWatcher != nullptr && _poseReachabilityWatcher->isRunning ()) ||
        (_envelopeWatcher != nullptr && _envelopeWatcher->isRunning ());
    if (running && reason != nullptr)
        *reason = QStringLiteral ("KinematicAnalysis 后台分析尚未完成，无法关闭项目。");
    return !running;
}

// beginProjectDocument:首次编辑生成资源时建立项目内 JSON 的会话基线。该路径仅存
// 于运行时,用于判定 Widget 已绑定项目资源;它绝不进入业务 JSON,保证项目被复制或
// 移动后不包含失效的绝对路径。
void KinematicAnalysisWidget::beginProjectDocument (const QString& path)
{
    // 该路径仅存于运行时，用于判定 Widget 已绑定项目资源；它绝不进入业务 JSON，保证项目
    // 被复制或移动后不包含失效的绝对路径。
    _projectDocumentPath = QFileInfo (path).absoluteFilePath ();
    // 新资源还没有正式文件，空基线会使当前可编辑配置被识别为脏，并在“保存项目”时由
    // ProjectSaveTransaction 原子创建目标文件。
    _savedProjectDocumentSnapshot.clear ();
    _pendingProjectDocumentSnapshot.clear ();
}

// clearProjectDocumentContext:项目关闭或切换时释放仅用于脏比较的路径和快照,
// 防止旧项目的基线影响新项目。注意只清数据而不改控件状态,交给加载流程恢复。
void KinematicAnalysisWidget::clearProjectDocumentContext ()
{
    // 仅清理项目生命周期数据而不主动改动控件：若下一项目存在资源，加载流程会恢复其配置；
    // 若不存在，当前展示的临时 UI 状态也不会被错误纳入新项目的保存范围。
    _projectDocumentPath.clear ();
    _savedProjectDocumentSnapshot.clear ();
    _pendingProjectDocumentSnapshot.clear ();
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
    const QString preferredTcp =
        QString::fromStdString (defaultTcpFrameName (selectedDevice ().get ()));
    int preferredIndex = _tcpFrameCombo->findText (preferredTcp);
    if (preferredIndex < 0 && _tcpFrameCombo->count () > 0)
        preferredIndex = 0;
    if (preferredIndex >= 0)
        _tcpFrameCombo->setCurrentIndex (preferredIndex);
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

QString KinematicAnalysisWidget::statusMessage () const
{
    return _status != nullptr ? _status->text () : QString ();
}

// refreshIkSolutionView:把 _lastIkResult 写入 _ikSolutionTable,
// 按过滤器过滤,每行的原始 solutionIndex 通过 storeIkSolutionIndex
// 存到 Qt::UserRole + 1。末尾刷新顶部计数 summary + 详情表。
void KinematicAnalysisWidget::refreshIkSolutionView ()
{
    if (_ikSolutionTable == NULL)
        return;
    if (_ikResultStale) {
        _ikSolutionTable->setRowCount (0);
        setIkDetailsEmpty ();
        if (_ikStatusLabel != NULL)
            _ikStatusLabel->setText (tr ("<b>Status</b><br>Stale - target changed"));
        return;
    }

    // Task 3:过滤器互斥。勾 Show usable only 时强制取消 Show failed candidates
    // 并禁用,避免两个过滤器语义冲突。QSignalBlocker 防止 setChecked(false)
    // 反向触发自身 stateChanged 槽,造成递归。
    // Task 4:刷新前记录当前选中的 solutionIndex,过滤后若该解仍可见,
    // 在循环末尾重新选中,而不是默认跳到第 0 行。
    int previousSolutionIndex = -1;
    const QList<QTableWidgetItem*> previouslySelected = _ikSolutionTable->selectedItems ();
    if (!previouslySelected.empty ())
        previousSolutionIndex =
            previouslySelected.front ()->data (Qt::UserRole + 1).toInt ();
    int rowToSelect = -1;

    _ikSolutionTable->setRowCount (0);
    int displayRow = 0;
    for (std::size_t i = 0; i < _lastIkResult.solutions.size (); ++i) {
        const KinematicIkSolution& solution = _lastIkResult.solutions[i];
        if (!shouldShowIkSolution (solution))
            continue;

        _ikSolutionTable->insertRow (displayRow);
        const int solutionIndex = static_cast<int> (i);
        if (solutionIndex == previousSolutionIndex)
            rowToSelect = displayRow;

        QTableWidgetItem* indexItem = makeItem (QString::number (solutionIndex));
        storeIkSolutionIndex (indexItem, solutionIndex);
        _ikSolutionTable->setItem (displayRow, 0, indexItem);

        // Task 7:Status 列染色。Pass 绿 / Warning 橙 / Fail 红,
        // 用户扫读时一眼区分候选质量。
        QTableWidgetItem* statusItem =
            makeItem (QString::fromLatin1 (statusText (solution.status)));
        if (solution.status == AnalysisStatus::Pass)
            statusItem->setForeground (QColor (0, 120, 0));
        else if (solution.status == AnalysisStatus::Warning)
            statusItem->setForeground (QColor (180, 120, 0));
        else if (solution.status == AnalysisStatus::Fail)
            statusItem->setForeground (QColor (180, 0, 0));
        _ikSolutionTable->setItem (displayRow, 1, statusItem);

        // Task 6:Failure 列加 tooltip,完整原因文本(可能含数值证据)
        // 在 hover 时显示,不必打开横向滚动。
        const QString failureText = ikFailureText (solution);
        QTableWidgetItem* failureItem = makeItem (failureText);
        failureItem->setToolTip (failureText);
        _ikSolutionTable->setItem (displayRow, 2, failureItem);

        QTableWidgetItem* currentQItem =
            makeItem (isCurrentIkSolution (solution) ? tr("Yes") : tr("No"));
        currentQItem->setToolTip (qVectorText (solution.q));
        _ikSolutionTable->setItem (displayRow, 3, currentQItem);
        _ikSolutionTable->setItem (displayRow, 4,
            makeItem (solution.inCollision ? tr("Yes") : tr("No")));
        // 整行的 solutionIndex 都存到 Qt::UserRole + 1,选中任一单元格
        // 都能反查回原始 solution。
        for (int column = 1; column < _ikSolutionTable->columnCount (); ++column)
            storeIkSolutionIndex (_ikSolutionTable->item (displayRow, column), solutionIndex);

        ++displayRow;
    }

    // Task 2:Displayed 是当前过滤后实际显示数;
    // Raw / Unique / Pass / Warning / Fail 仍是全量统计,语义清晰不混淆。
    const KinematicIkSummary summary = summarizeIkSolutions (_lastIkResult.solutions);
    const QString status = QString::fromLatin1 (statusText (_lastIkResult.status));
    const QString statusColor = _lastIkResult.status == AnalysisStatus::Fail ?
        QStringLiteral ("#b00020") : _lastIkResult.status == AnalysisStatus::Warning ?
        QStringLiteral ("#a15c00") : QStringLiteral ("#18794e");
    if (_ikStatusLabel != NULL)
        _ikStatusLabel->setText (tr("<b>Status</b><br><span style=\"color:%1\"><b>%2</b></span>")
            .arg (statusColor, status));
    if (_ikDisplayedLabel != NULL)
        _ikDisplayedLabel->setText (tr("<b>Displayed</b><br>%1").arg (displayRow));
    if (_ikUsableLabel != NULL)
        _ikUsableLabel->setText (tr("<b>Usable</b><br>%1")
            .arg (static_cast<int> (summary.usableCount)));
    if (_ikPassLabel != NULL)
        _ikPassLabel->setText (tr("<b>Pass</b><br>%1")
            .arg (static_cast<int> (summary.passCount)));
    if (_ikWarningLabel != NULL)
        _ikWarningLabel->setText (tr("<b>Warning</b><br>%1")
            .arg (static_cast<int> (summary.warningCount)));
    if (_ikFailLabel != NULL)
        _ikFailLabel->setText (tr("<b>Fail</b><br>%1")
            .arg (static_cast<int> (summary.failCount)));

    // Task 4 续:若过滤后原选中解消失(被过滤掉),rowToSelect == -1,
    // 回退到第 0 行;无行时退到空状态(Apply 也会被 setIkDetailsEmpty 禁用)。
    if (_ikSolutionTable->rowCount () > 0) {
        if (rowToSelect < 0)
            rowToSelect = 0;
        _ikSolutionTable->selectRow (rowToSelect);
    }
    else {
        setIkDetailsEmpty ();
    }

    // 按钮启用/禁用统一交给 updateIkSolutionDetails / setIkDetailsEmpty。
    updateIkSolutionDetails ();
}

// updateIkSolutionDetails:把选中 IK 候选的隐藏字段(距离 / 关节裕度 / 可操作度 /
// 条件数 / 位置与姿态误差 / Q)写入详情表,并同步 Apply 按钮可用性。
// updateIkSolutionDetails writes the fields hidden from the candidate list.
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
        _ikApplyButton->setEnabled (!_ikResultStale && isUsableIkSolution (s));

    const QString condText = std::isinf (s.conditionNumber) ?
        QStringLiteral ("inf") : QString::number (s.conditionNumber, 'g', 6);
    _ikDetailTable->setRowCount (7);
    setDetailRow (_ikDetailTable, 0, tr("Distance"),
                  QString::number (s.distanceToCurrentQ, 'g', 6));
    setDetailRow (_ikDetailTable, 1, tr("Min limit margin"),
                  QString::number (s.minJointLimitMargin, 'g', 6));
    setDetailRow (_ikDetailTable, 2, tr("Manipulability"),
                  QString::number (s.manipulability, 'g', 6));
    setDetailRow (_ikDetailTable, 3, tr("Condition"), condText);
    setDetailRow (_ikDetailTable, 4, tr("Position error"),
                  QString::number (displayLengthFromMeters (
                      s.positionErrorMeters, _lengthUnit), 'g', 6));
    setDetailRow (_ikDetailTable, 5, tr("Orientation error"),
                  QString::number (displayAngleFromDegrees (
                      s.orientationErrorDeg, _angleUnit), 'g', 6));
    setDetailRow (_ikDetailTable, 6, tr("Q"), qVectorText (s.q));
    _ikDetailTable->resizeRowsToContents ();
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
    if (_ikApplyButton != NULL)
        _ikApplyButton->setEnabled (false);
}

// invalidateIkResultPresentation:IK 目标被修改时调用,把结果标记为过期并清空
// 候选表 / 详情表 / 统计标签,提示用户重新 Solve;同时禁用 Apply 防止陈旧解写回。
void KinematicAnalysisWidget::invalidateIkResultPresentation ()
{
    _ikResultStale = true;
    if (_ikSolutionTable != NULL)
        _ikSolutionTable->setRowCount (0);
    setIkDetailsEmpty ();
    if (_ikDisplayedLabel != NULL)
        _ikDisplayedLabel->setText (tr ("<b>Displayed</b><br>-"));
    if (_ikUsableLabel != NULL)
        _ikUsableLabel->setText (tr ("<b>Usable</b><br>-"));
    if (_ikPassLabel != NULL)
        _ikPassLabel->setText (tr ("<b>Pass</b><br>-"));
    if (_ikWarningLabel != NULL)
        _ikWarningLabel->setText (tr ("<b>Warning</b><br>-"));
    if (_ikFailLabel != NULL)
        _ikFailLabel->setText (tr ("<b>Fail</b><br>-"));
    if (_ikStatusLabel != NULL)
        _ikStatusLabel->setText (tr("<b>Status</b><br>Stale - target changed"));
    setStatus (tr("IK target changed; previous results are stale. Solve again to refresh candidates."));
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
    table->horizontalHeader ()->setStretchLastSection (true);
    table->verticalHeader ()->setVisible (false);
    table->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
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

QString targetResidualText (const TargetEvaluation& result)
{
    if (result.candidates.empty ())
        return QStringLiteral ("-");
    const TargetCandidate& best = result.candidates.front ();
    return QStringLiteral ("%1 mm / %2 deg")
        .arg (QString::number (best.positionErrorMeters * 1000.0, 'f', 2))
        .arg (QString::number (best.orientationErrorDeg, 'f', 2));
}

QString targetPoseCoverageText (const TargetEvaluation& result)
{
    if (result.candidates.empty ())
        return QStringLiteral ("-");
    int usable = 0;
    for (const TargetCandidate& candidate : result.candidates) {
        if (candidate.configuration.feasibility == Feasibility::Feasible)
            ++usable;
    }
    return QStringLiteral ("%1/%2 poses")
        .arg (usable).arg (static_cast<int> (result.candidates.size ()));
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
// Task 6:同步把完整文本写入 tooltip,IK 主表 Q 列(可能很长)在 hover 时
// 可以看完整内容。
QTableWidgetItem* makeQItem (const std::vector< double >& q,
                             const std::vector< rws::KinematicFailureReason >& reasons)
{
    QString text = qVectorText(q);
    const QString failures = failureReasonsText(reasons);
    if (!failures.isEmpty())
        text += QString(" | ") + failures;

    QTableWidgetItem* item = makeItem(text);
    item->setToolTip (text);
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

// cancelEnvelopeRequest:请求取消在途的包络计算。设置跨线程 atomic 取消标志、
// 停掉防抖定时器;必要时取消并等待 QFutureWatcher 结束。generation 号 +1 使
// 任何过期请求返回的结果在 onEnvelopeFinished 中被判定为过期而丢弃。
void KinematicAnalysisWidget::cancelEnvelopeRequest (bool waitForFinished)
{
    const bool hadActiveRequest = _envelopeRunActive ||
        (_envelopeWatcher != NULL && _envelopeWatcher->isRunning ());
    if (_envelopeCancelRequested)
        _envelopeCancelRequested->store (true);
    if (_envelopeDebounceTimer != NULL)
        _envelopeDebounceTimer->stop ();
    if (_envelopeWatcher != NULL && _envelopeWatcher->isRunning ()) {
        _envelopeWatcher->cancel ();
        if (waitForFinished)
            _envelopeWatcher->waitForFinished ();
    }
    if (hadActiveRequest)
        ++_envelopeGeneration;
    _envelopeRunActive = false;
}

// invalidateEnvelopeCache:标记包络缓存失效。任何影响包络形状的输入(设备 / TCP /
// 投影 / 参数 / 关节位形)变化后都必须调用,否则 refreshVisualization 会误命中
// 缓存并展示陈旧包络。
void KinematicAnalysisWidget::invalidateEnvelopeCache ()
{
    _envelopeCacheValid = false;
}

// stateChangedListener:RobWorkStudio 的 state 变化回调(经 boost::bind 注册)。
// 包络近似依赖当前关节位形,因此 state 变化时主动取消在途请求并使缓存失效;
// 是否重绘由 visualEnvelopeStateChangeRequiresRefresh 判定——仅在包络模式生效时
// 才需要刷新可视化,避免无关 state 抖动频繁重绘。
void KinematicAnalysisWidget::stateChangedListener (const rw::kinematics::State& state)
{
    (void) state;
    const bool envelopeActive =
        _visualSourceCombo != NULL &&
        _visualRenderModeCombo != NULL &&
        visualEnvelopeModeAvailable (
            _visualSourceCombo->currentData ().toInt (),
            _visualRenderModeCombo->currentData ().toInt ());
    const bool requestActive = _envelopeRunActive ||
        (_envelopeWatcher != NULL && _envelopeWatcher->isRunning ());
    if (requestActive)
        cancelEnvelopeRequest (false);
    invalidateEnvelopeCache ();
    if (visualEnvelopeStateChangeRequiresRefresh (envelopeActive, true))
        refreshVisualization ();
}

// makeEnvelopeCacheKey:构造包络缓存键。除显式传入的参数外,还从设备读取当前
// 关节上下限记录进键值——关节界限改变(不同设备 / 重新加载)时,相同方向数下
// 的包络形状会完全不同,必须纳入相等比较。
WorkspaceEnvelopeCacheKey KinematicAnalysisWidget::makeEnvelopeCacheKey (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    VisualProjection projection,
    int angularDirections,
    int coordinateIterations) const
{
    WorkspaceEnvelopeCacheKey key;
    key.device = device;
    key.tcpFrame = tcpFrame;
    key.projection = projection;
    key.angularDirections = angularDirections;
    key.coordinateIterations = coordinateIterations;
    if (device != nullptr) {
        const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
        key.lowerBounds.reserve (bounds.first.size ());
        key.upperBounds.reserve (bounds.second.size ());
        for (std::size_t i = 0; i < bounds.first.size (); ++i)
            key.lowerBounds.push_back (bounds.first[i]);
        for (std::size_t i = 0; i < bounds.second.size (); ++i)
            key.upperBounds.push_back (bounds.second[i]);
    }
    return key;
}

// collisionDetectorForAnalysis:按需返回 RobWorkStudio 的碰撞检测器。
// requested 为 false 时直接返回 NULL(调用方明确不需要碰撞检查);请求但不可用时
// 通过 unavailable 输出标志,让 UI 提示"碰撞检查不可用"而不是静默失败。
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

// ikXInputMeters / ikYInputMeters / ikZInputMeters:把 IK 输入框的"显示单位"数值
// 换算回米(内部统一基准),供 solveIk、项目快照等所有读取位置的代码复用,
// 避免各处重复处理单位换算而漂移。
double KinematicAnalysisWidget::ikXInputMeters () const
{
    return metersFromDisplayLength (_ikXSpin->value (), _lengthUnit);
}

double KinematicAnalysisWidget::ikYInputMeters () const
{
    return metersFromDisplayLength (_ikYSpin->value (), _lengthUnit);
}

double KinematicAnalysisWidget::ikZInputMeters () const
{
    return metersFromDisplayLength (_ikZSpin->value (), _lengthUnit);
}

double KinematicAnalysisWidget::ikRollInputDeg () const
{
    return degreesFromDisplayAngle (_ikRollSpin->value (), _angleUnit);
}

double KinematicAnalysisWidget::ikPitchInputDeg () const
{
    return degreesFromDisplayAngle (_ikPitchSpin->value (), _angleUnit);
}

double KinematicAnalysisWidget::ikYawInputDeg () const
{
    return degreesFromDisplayAngle (_ikYawSpin->value (), _angleUnit);
}

// setIkPoseMetersDeg:以米 / 度写入 6 个 IK 输入框(内部按当前显示单位换算)。
// 用 QSignalBlocker 抑制写入期间的 valueChanged 信号,避免触发 clearIkSource 等
// 依赖用户操作的副作用。
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
    _ikXSpin->setValue (displayLengthFromMeters (positionMeters[0], _lengthUnit));
    _ikYSpin->setValue (displayLengthFromMeters (positionMeters[1], _lengthUnit));
    _ikZSpin->setValue (displayLengthFromMeters (positionMeters[2], _lengthUnit));
    _ikRollSpin->setValue (displayAngleFromDegrees (rpyDeg[0], _angleUnit));
    _ikPitchSpin->setValue (displayAngleFromDegrees (rpyDeg[1], _angleUnit));
    _ikYawSpin->setValue (displayAngleFromDegrees (rpyDeg[2], _angleUnit));
}

// updateUnitDisplay:长度 / 角度单位切换的统一入口。
//   - 先把各输入框当前数值按旧单位换算成内部基准(米 / 度),保证切换单位不改变
//     物理数值;
//   - 更新 _lengthUnit / _angleUnit,并重设所有 SpinBox 的 range / step / suffix;
//   - 再按新单位回写显示;
//   - 最后刷新任务点 / 工作空间 / 位姿可达性 / 可视化等所有依赖单位显示的页面。
void KinematicAnalysisWidget::updateUnitDisplay ()
{
    if (_ikXSpin == NULL || _lengthUnitCombo == NULL || _angleUnitCombo == NULL)
        return;

    const std::array< double, 3 > positionMeters = {{
        ikXInputMeters (), ikYInputMeters (), ikZInputMeters ()}};
    const std::array< double, 3 > rpyDeg = {{
        ikRollInputDeg (), ikPitchInputDeg (), ikYawInputDeg ()}};
    const double positionToleranceMeters = _thresholdPositionToleranceSpin == NULL ?
        _thresholds.positionToleranceMeters :
        metersFromDisplayLength (_thresholdPositionToleranceSpin->value (), _lengthUnit);
    const double orientationToleranceDeg = _thresholdOrientationToleranceSpin == NULL ?
        _thresholds.orientationToleranceDeg :
        degreesFromDisplayAngle (_thresholdOrientationToleranceSpin->value (), _angleUnit);

    if (_posePositionTable != NULL) {
        for (int row = 0; row < _posePositionTable->rowCount (); ++row) {
            for (int column = 0; column < 3; ++column) {
                QTableWidgetItem* item = _posePositionTable->item (row, column);
                bool ok = false;
                const double displayed = item == NULL ? 0.0 : item->text ().toDouble (&ok);
                if (item != NULL && ok)
                    item->setData (Qt::UserRole,
                                   metersFromDisplayLength (displayed, _lengthUnit));
            }
        }
    }

    _lengthUnit = static_cast< KinematicLengthUnit > (
        _lengthUnitCombo->currentData ().toInt ());
    _angleUnit = static_cast< KinematicAngleUnit > (
        _angleUnitCombo->currentData ().toInt ());

    const QString lengthSuffix = QStringLiteral (" ") +
        QString::fromLatin1 (unitSuffix (_lengthUnit));
    const QString angleSuffix = QStringLiteral (" ") +
        QString::fromLatin1 (unitSuffix (_angleUnit));
    for (QDoubleSpinBox* spin : {_ikXSpin, _ikYSpin, _ikZSpin}) {
        spin->setRange (displayLengthFromMeters (-1000.0, _lengthUnit),
                        displayLengthFromMeters (1000.0, _lengthUnit));
        spin->setSingleStep (displayLengthFromMeters (0.01, _lengthUnit));
        spin->setSuffix (QString ());
    }
    for (QDoubleSpinBox* spin : {_ikRollSpin, _ikPitchSpin, _ikYawSpin}) {
        spin->setRange (displayAngleFromDegrees (-360.0, _angleUnit),
                        displayAngleFromDegrees (360.0, _angleUnit));
        spin->setSingleStep (displayAngleFromDegrees (1.0, _angleUnit));
        spin->setSuffix (QString ());
    }
    setIkPoseMetersDeg (positionMeters, rpyDeg);

    if (_taskPointTab != NULL) {
        for (const QString& name : {QStringLiteral ("taskPointXSpin"),
                                    QStringLiteral ("taskPointYSpin"),
                                    QStringLiteral ("taskPointZSpin")}) {
            QDoubleSpinBox* spin = _taskPointTab->findChild< QDoubleSpinBox* > (name);
            if (spin != NULL) {
                spin->setRange (displayLengthFromMeters (-1000.0, _lengthUnit),
                                displayLengthFromMeters (1000.0, _lengthUnit));
                spin->setSingleStep (displayLengthFromMeters (0.01, _lengthUnit));
                spin->setSuffix (QString ());
            }
        }
        for (const QString& name : {QStringLiteral ("taskPointRollSpin"),
                                    QStringLiteral ("taskPointPitchSpin"),
                                    QStringLiteral ("taskPointYawSpin")}) {
            QDoubleSpinBox* spin = _taskPointTab->findChild< QDoubleSpinBox* > (name);
            if (spin != NULL) {
                spin->setRange (displayAngleFromDegrees (-360.0, _angleUnit),
                                displayAngleFromDegrees (360.0, _angleUnit));
                spin->setSingleStep (displayAngleFromDegrees (1.0, _angleUnit));
                spin->setSuffix (QString ());
            }
        }
    }

    if (_thresholdPositionToleranceSpin != NULL) {
        _thresholdPositionToleranceSpin->setRange (0.0,
            displayLengthFromMeters (1000.0, _lengthUnit));
        _thresholdPositionToleranceSpin->setSuffix (lengthSuffix);
        _thresholdPositionToleranceSpin->setValue (
            displayLengthFromMeters (positionToleranceMeters, _lengthUnit));
    }
    if (_thresholdOrientationToleranceSpin != NULL) {
        _thresholdOrientationToleranceSpin->setRange (0.0,
            displayAngleFromDegrees (360.0, _angleUnit));
        _thresholdOrientationToleranceSpin->setSuffix (angleSuffix);
        _thresholdOrientationToleranceSpin->setValue (
            displayAngleFromDegrees (orientationToleranceDeg, _angleUnit));
    }

    if (_taskPointModel != nullptr)
        _taskPointModel->setDisplayUnits (_lengthUnit, _angleUnit);
    if (_ikSolutionTable != NULL)
        _ikSolutionTable->setHorizontalHeaderLabels ({
            tr("Index"), tr("Status"), tr("Failure"), tr("Current Q"), tr("Collision")});
    if (_workspaceTable != NULL)
        _workspaceTable->setHorizontalHeaderLabels ({
            tr("Index"), tr("Status"), tr("Collision"), tr("TCP position"),
            tr("Manipulability"), tr("Min margin")});
    if (_posePositionTable != NULL) {
        _posePositionTable->setHorizontalHeaderLabels ({
            tr("X"), tr("Y"), tr("Z")});
        for (int row = 0; row < _posePositionTable->rowCount (); ++row) {
            for (int column = 0; column < 3; ++column) {
                QTableWidgetItem* item = _posePositionTable->item (row, column);
                if (item != NULL && item->data (Qt::UserRole).isValid ())
                    item->setText (QString::number (displayLengthFromMeters (
                        item->data (Qt::UserRole).toDouble (), _lengthUnit)));
            }
        }
    }
    if (_poseResultTable != NULL) {
        _poseResultTable->setHorizontalHeaderLabels ({
            tr("Index"), tr("Status"), tr("Position"), tr("Coverage")});
    }
    if (_visualPlot != NULL)
        _visualPlot->setLengthUnit (_lengthUnit);

    if (_lastCurrentPose.status != AnalysisStatus::Unknown)
        refreshCurrentPose ();
    refreshIkSolutionView ();
    applyWorkspaceResults (_workspaceSamples);
    applyPoseReachabilityResults (_poseReachabilitySamples);
    refreshVisualization ();
}

// refreshCurrentPose:重置四个 Current pose 表格与文本标签 → 调用
// KinematicAnalyzer::analyzeCurrentPose → 把结果填回 UI,同时更新 _lastCurrentPose
// 并刷新 Report tab 的汇总。
void KinematicAnalysisWidget::refreshCurrentPose ()
{
    // 重置所有面板为占位状态。
    for (QLabel* label : _currentTcpValueLabels) {
        if (label != NULL)
            label->setText (QStringLiteral ("-"));
    }
    if (_poseIndicatorLabel != NULL)
        _poseIndicatorLabel->setText (tr("<b>Status</b><br>-"));
    if (_poseConditionLabel != NULL)
        _poseConditionLabel->setText (tr("<b>Condition</b><br>-"));
    if (_poseManipulabilityLabel != NULL)
        _poseManipulabilityLabel->setText (tr("<b>Manipulability</b><br>-"));
    if (_poseMarginLabel != NULL)
        _poseMarginLabel->setText (tr("<b>Min joint margin</b><br>-"));
    if (_poseCollisionCapabilityLabel != NULL)
        _poseCollisionCapabilityLabel->setText (
            tr("<b>Collision capability</b><br>-"));
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
        _warningLabel->setText (tr("No active warnings"));

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

    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (true, &collisionUnavailable);
    if (_poseCollisionCapabilityLabel != NULL) {
        _poseCollisionCapabilityLabel->setText (
            collisionDetector != NULL && !collisionUnavailable
                ? tr("<b>Collision capability</b><br>Available")
                : tr("<b>Collision capability</b><br>Unavailable"));
    }

    // ---- 1. 更新共享 Pose / IK target 网格中的 Current TCP 列 ----
    const auto formatPoseValue = [] (double value) {
        return QString::number (std::fabs (value) < 0.0000005 ? 0.0 : value, 'f', 6);
    };
    const std::array< QString, 6 > currentTcpTexts = {{
        formatPoseValue (displayLengthFromMeters (result.tcpPosition[0], _lengthUnit)),
        formatPoseValue (displayLengthFromMeters (result.tcpPosition[1], _lengthUnit)),
        formatPoseValue (displayLengthFromMeters (result.tcpPosition[2], _lengthUnit)),
        formatPoseValue (displayAngleFromDegrees (result.tcpRpyDeg[0], _angleUnit)),
        formatPoseValue (displayAngleFromDegrees (result.tcpRpyDeg[1], _angleUnit)),
        formatPoseValue (displayAngleFromDegrees (result.tcpRpyDeg[2], _angleUnit))}};
    for (std::size_t index = 0; index < _currentTcpValueLabels.size (); ++index) {
        if (_currentTcpValueLabels[index] != NULL)
            _currentTcpValueLabels[index]->setText (currentTcpTexts[index]);
    }
    // 表头高度在初次布局后才会稳定,refresh 阶段再调一次确保紧凑。
    if (_poseIndicatorLabel != NULL) {
        const QString condText = std::isinf (result.conditionNumber) ?
            QStringLiteral ("inf") : QString::number (result.conditionNumber, 'g', 6);
        const QString minMargin = result.minJointLimitMargin > 0.0 ?
            QString::number (result.minJointLimitMargin, 'g', 6) : QStringLiteral ("-");
        const QString status = QString::fromLatin1 (statusText (result.status));
        const QString statusColor = result.status == AnalysisStatus::Fail ?
            QStringLiteral ("#b00020") : result.status == AnalysisStatus::Warning ?
            QStringLiteral ("#a15c00") : QStringLiteral ("#18794e");
        _poseIndicatorLabel->setText (tr("<b>Status</b><br><span style=\"color:%1\"><b>%2</b></span>")
            .arg (statusColor, status));
        if (_poseConditionLabel != NULL)
            _poseConditionLabel->setText (tr("<b>Condition</b><br>%1").arg (condText));
        if (_poseManipulabilityLabel != NULL)
            _poseManipulabilityLabel->setText (tr("<b>Manipulability</b><br>%1")
                .arg (QString::number (result.manipulability, 'g', 6)));
        if (_poseMarginLabel != NULL)
            _poseMarginLabel->setText (tr("<b>Min joint margin</b><br>%1").arg (minMargin));
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
        const int visibleRows = std::min (n, 6);
        setCompactTableVisibleRows (_jointStatusTable, visibleRows);
        _jointStatusTable->setVerticalScrollBarPolicy (
            n > visibleRows ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
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
            headers << tr("sigma%1").arg (i);
        if (singCount > 0)
            headers << tr("sigma_min");
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
            _warningLabel->setText (tr("No active warnings"));
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
            if (_advancedDiagnosticsToggle != NULL &&
                !_advancedDiagnosticsToggle->isChecked ())
                _advancedDiagnosticsToggle->setChecked (true);
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
        invalidateIkResultPresentation ();
        if (_ikSourceLabel != NULL)
            _ikSourceLabel->setVisible (false);
        setStatus (tr("Imported current TCP pose into IK target."));
    }
    catch (const std::exception& e) {
        setStatus (tr("Cannot import current TCP pose: %1").arg (QString::fromStdString (e.what ())));
    }
    catch (...) {
        setStatus (tr("Cannot import current TCP pose: unknown error."));
    }
}

// solveIk:从 IK tab 读取目标位姿(x/y/z + RPY),构造 TaskPoint 后调用
// analyzer.analyzeIk 求解。求解期间禁用 Solve 按钮并提示 "Solving IK...",
// 所有提前返回路径都恢复按钮;结果缓存到 _lastIkResult 由 refreshIkSolutionView
// 统一渲染,便于切换过滤器时即时刷新而不必重解。
void KinematicAnalysisWidget::solveIk ()
{
    _ikSolutionTable->setRowCount(0);
    // 立即清空详情并禁用 Apply,保证所有提前返回路径都不会保留旧数据。
    setIkDetailsEmpty ();
    if (_ikDuplicateQThresholdSpin != NULL)
        _thresholds.ikDuplicateQThreshold = _ikDuplicateQThresholdSpin->value ();

    // Task 8:进入分析前禁用 Solve + 状态栏提示"Solving IK...";
    // 每个提前返回 / 正常结束都要把按钮恢复,避免遗留禁用状态。
    if (_ikSolveButton != NULL)
        _ikSolveButton->setEnabled (false);
    setStatus (tr("Solving IK..."));

    if (_workcell == NULL) {
        setStatus(tr("Cannot solve IK: no WorkCell loaded."));
        if (_ikSolveButton != NULL)
            _ikSolveButton->setEnabled (true);
        return;
    }

    const std::string deviceName = _deviceCombo->currentText().toStdString();
    rw::core::Ptr< rw::models::Device > device = deviceByName(_workcell, deviceName);
    if (device == NULL) {
        setStatus(tr("Cannot solve IK: no valid device selected."));
        if (_ikSolveButton != NULL)
            _ikSolveButton->setEnabled (true);
        return;
    }

    const std::string tcpName = _tcpFrameCombo->currentText().toStdString();
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = frameByName(_workcell, tcpName);

    TaskPoint target;
    target.id = "ik_target";
    target.name = "IKTarget";
    target.tcpFrame = tcpName;
    target.position = {{ikXInputMeters(), ikYInputMeters(), ikZInputMeters()}};
    target.rpyDeg = {{ikRollInputDeg(), ikPitchInputDeg(), ikYawInputDeg()}};
    target.tolerance.positionMeters = _thresholds.positionToleranceMeters;
    target.tolerance.orientationDeg = _thresholds.orientationToleranceDeg;

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    const bool checkCollision = ikCollisionCheckRequested (
        _ikCollisionCheck != NULL,
        _ikCollisionCheck != NULL && _ikCollisionCheck->isChecked ());
    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (checkCollision, &collisionUnavailable);
    const KinematicIkAnalysisResult result =
        analyzer.analyzeIk(device, tcpFrame, currentState(), target, collisionDetector);

    // 保存最近一次完整结果,_refreshIkSolutionView 与 _updateIkSolutionDetails
    // 都从这里读。表格真正填充交给 refreshIkSolutionView 统一负责,
    // 这样过滤器切换时不必再调 Solve,UI 即时刷新。
    _lastIkResult = result;
    _ikResultStale = false;
    refreshIkSolutionView ();

    if (!checkCollision) {
        setStatus (tr("IK analysis completed with %1 candidate(s); collision checking disabled.")
                       .arg (static_cast< int > (result.solutions.size ())));
    }
    else if (collisionUnavailable) {
        setStatus (tr("IK analysis completed with %1 candidate(s); collision checking was unavailable.")
                       .arg (static_cast< int > (result.solutions.size ())));
    }
    else {
        setStatus(tr("IK analysis completed with %1 candidate(s).")
                      .arg(static_cast<int>(result.solutions.size())));
    }

    // 正常路径收尾:恢复 Solve 按钮。
    if (_ikSolveButton != NULL)
        _ikSolveButton->setEnabled (true);
}

// shouldShowIkSolution:IK 解过滤器,组合两个 QCheckBox:
//   1) "Show usable only" 勾上 → 只保留无碰撞 + status != Fail 的解;
//   2) 否则若 "Show failed candidates" 未勾 → 隐藏 status == Fail 的诊断解;
//   3) 其余情况都展示,保留所有候选用于诊断。
bool KinematicAnalysisWidget::shouldShowIkSolution (
    const KinematicIkSolution& solution) const
{
    const bool usable = !solution.inCollision && solution.status != AnalysisStatus::Fail;
    const int filter = _ikCandidateFilterCombo != NULL ?
        _ikCandidateFilterCombo->currentData ().toInt () : 0;
    if (filter == 1)
        return usable;
    if (filter == 0 && solution.status == AnalysisStatus::Fail)
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
    if (_ikResultStale) {
        setStatus (tr("Cannot apply IK solution: target changed; solve again."));
        return;
    }
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
    // 先从第 0 列取真实 solutionIndex,做一次完整性 / 可用性校验;
    // 这样即便按钮状态被异常触发,也不会把 Fail / collision 解写回 RobWorkStudio。
    QTableWidgetItem* indexItem = _ikSolutionTable->item(row, 0);
    if (indexItem == NULL) {
        setStatus(tr("Cannot apply IK solution: selected row has no solution index."));
        return;
    }
    const int solutionIndex = indexItem->data(Qt::UserRole + 1).toInt();
    if (solutionIndex < 0 ||
        solutionIndex >= static_cast<int> (_lastIkResult.solutions.size ())) {
        setStatus(tr("Cannot apply IK solution: selected row index is invalid."));
        return;
    }
    const KinematicIkSolution& solution =
        _lastIkResult.solutions[static_cast<std::size_t> (solutionIndex)];
    if (!isUsableIkSolution (solution)) {
        setStatus(tr("Cannot apply IK solution: selected solution is failed or in collision."));
        return;
    }

    if (solution.q.empty ()) {
        setStatus(tr("Cannot apply IK solution: selected row has no stored Q data."));
        return;
    }

    rw::math::Q q(static_cast<std::size_t>(solution.q.size()));
    for (std::size_t i = 0; i < solution.q.size(); ++i)
        q(i) = solution.q[i];

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

    QHBoxLayout* summaryRow = new QHBoxLayout();
    QLabel* summaryTitle = new QLabel (tr("Key station tasks"), _taskPointTab);
    summaryTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    summaryRow->addWidget (summaryTitle);
    _taskPointSummaryLabel = new QLabel (_taskPointTab);
    _taskPointSummaryLabel->setObjectName (QStringLiteral ("taskPointSummaryLabel"));
    _taskPointSummaryLabel->setTextFormat (Qt::RichText);
    _taskPointSummaryLabel->setText (
        tr("<b>Enabled</b> 0 | <b>Pass</b> - | <b>Warning</b> - | <b>Fail</b> -"));
    summaryRow->addWidget (_taskPointSummaryLabel);
    summaryRow->addStretch (1);
    _analyzeAllTaskPointsButton = new QPushButton(tr("Analyze all"), _taskPointTab);
    summaryRow->addWidget (_analyzeAllTaskPointsButton);
    tpLayout->addLayout (summaryRow);

    QHBoxLayout* listActions = new QHBoxLayout();
    _addTaskPointButton         = new QPushButton(tr("Add"), _taskPointTab);
    _removeTaskPointButton      = new QPushButton(tr("Remove"), _taskPointTab);
    _importTaskPointsButton     = new QPushButton(tr("Import CSV"), _taskPointTab);
    _importFrozenRequirementsButton = new QPushButton(tr("Import frozen requirements"), _taskPointTab);
    // 该对象名是 UI 自动化和发布验收的稳定定位点；不依赖可本地化的按钮文本，避免
    // 中英文界面或翻译调整后无法确认“冻结需求导入”入口仍然存在。
    _importFrozenRequirementsButton->setObjectName(
        QStringLiteral("importFrozenRequirementsButton"));
    _exportTaskPointsButton     = new QPushButton(tr("Export tasks"), _taskPointTab);
    _exportTaskPointResultsButton = new QPushButton(tr("Export results"), _taskPointTab);
    _importCurrentTcpTaskPointButton = new QPushButton (tr("Add current TCP"), _taskPointTab);
    listActions->addWidget (_addTaskPointButton);
    listActions->addWidget (_removeTaskPointButton);
    listActions->addWidget (_importTaskPointsButton);
    listActions->addWidget(_importFrozenRequirementsButton);
    listActions->addWidget (_exportTaskPointsButton);
    listActions->addWidget (_exportTaskPointResultsButton);
    listActions->addWidget (_importCurrentTcpTaskPointButton);
    QToolButton* moreActions = new QToolButton (_taskPointTab);
    moreActions->setObjectName (QStringLiteral ("taskPointMoreActions"));
    moreActions->setText (tr ("More"));
    moreActions->setPopupMode (QToolButton::InstantPopup);
    QMenu* moreMenu = new QMenu (moreActions);
    const auto addMoreAction = [moreMenu] (const QString& text, QPushButton* button) {
        QAction* action = moreMenu->addAction (text);
        QObject::connect (action, &QAction::triggered, button, &QPushButton::click);
        return action;
    };
    QAction* importAction = addMoreAction (tr ("Import CSV"), _importTaskPointsButton);
    QAction* exportTasksAction = addMoreAction (tr ("Export tasks"), _exportTaskPointsButton);
    QAction* exportResultsAction = addMoreAction (tr ("Export results"), _exportTaskPointResultsButton);
    QAction* addCurrentTcpAction = addMoreAction (tr ("Add current TCP"), _importCurrentTcpTaskPointButton);
    connect (moreMenu, &QMenu::aboutToShow, moreMenu,
             [importAction, exportTasksAction, exportResultsAction, addCurrentTcpAction,
              this] () {
                 importAction->setEnabled (_importTaskPointsButton->isEnabled ());
                 exportTasksAction->setEnabled (_exportTaskPointsButton->isEnabled ());
                 exportResultsAction->setEnabled (_exportTaskPointResultsButton->isEnabled ());
                 addCurrentTcpAction->setEnabled (_importCurrentTcpTaskPointButton->isEnabled ());
             });
    moreActions->setMenu (moreMenu);
    _addTaskPointButton->setVisible (false);
    _removeTaskPointButton->setVisible (false);
    _importTaskPointsButton->setVisible (false);
    _importFrozenRequirementsButton->setVisible (false);
    _exportTaskPointsButton->setVisible (false);
    _exportTaskPointResultsButton->setVisible (false);
    _importCurrentTcpTaskPointButton->setVisible (false);
    listActions->addWidget (moreActions);
    listActions->addStretch (1);
    tpLayout->addLayout (listActions);

    _analyzeSelectedTaskPointsButton = new QPushButton (tr("Analyze selected"), _taskPointTab);
    _applySelectedTaskPointBestQButton = new QPushButton (tr("Apply best Q"), _taskPointTab);
    _openSelectedTaskPointInIkButton  = new QPushButton (tr("Open in IK tab"), _taskPointTab);
    _analyzeSelectedTaskPointsButton->setObjectName (
        QStringLiteral ("analyzeSelectedTaskPointsButton"));
    _applySelectedTaskPointBestQButton->setObjectName (
        QStringLiteral ("applySelectedTaskPointBestQButton"));
    _openSelectedTaskPointInIkButton->setObjectName (
        QStringLiteral ("openSelectedTaskPointInIkButton"));
    _analyzeSelectedTaskPointsButton->setEnabled (false);
    _applySelectedTaskPointBestQButton->setEnabled (false);
    _openSelectedTaskPointInIkButton->setEnabled (false);
    _analyzeAllTaskPointsButton->setVisible (false);
    _analyzeSelectedTaskPointsButton->setVisible (false);

    // P3-A:数据源用 TaskPointTableModel,view 用 QTableView。
    // model 持有 19 列任务点定义 + 8 列 IK 结果 + 验证状态;
    // view 只负责渲染与 delegate 交互。
    _taskPointModel = new rws::TaskPointTableModel (_taskPointTab);
    _taskPointTable = new QTableView (_taskPointTab);
    _taskPointTable->setModel (_taskPointModel);
    QStringList headers = rws::TaskPointTableModel::allHeaderTexts ();
    for (int i = 0; i < headers.size (); ++i)
        _taskPointTable->model ()->setHeaderData (i, Qt::Horizontal, headers[i], Qt::DisplayRole);
    _taskPointTable->setSelectionBehavior (QAbstractItemView::SelectRows);
    _taskPointTable->setSelectionMode (QAbstractItemView::SingleSelection);
    _taskPointTable->setAlternatingRowColors (true);
    _taskPointTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    _taskPointTable->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
    _taskPointTable->horizontalHeader ()->setSectionResizeMode (QHeaderView::Interactive);
    _taskPointTable->horizontalHeader ()->setStretchLastSection (false);
    _taskPointTable->verticalHeader ()->setVisible (false);
    _taskPointTable->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    // model 的 flag 已经把 result 列设成只读,delegate 单独装。
    installTaskPointDelegates ();
    tpLayout->addWidget (_taskPointTable, 4);

    _taskPointSelectedPanel = new QWidget (_taskPointTab);
    _taskPointSelectedPanel->setObjectName (QStringLiteral ("selectedTaskPointPanel"));
    QVBoxLayout* selectedPanelLayout = new QVBoxLayout (_taskPointSelectedPanel);
    selectedPanelLayout->setContentsMargins (0, 0, 0, 0);
    QHBoxLayout* selectedTitleRow = new QHBoxLayout();
    QLabel* selectedTitle = new QLabel (tr ("Selected point"), _taskPointTab);
    selectedTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    selectedTitleRow->addWidget (selectedTitle);
    selectedTitleRow->addStretch (1);
    selectedTitleRow->addWidget (_analyzeSelectedTaskPointsButton);
    selectedTitleRow->addWidget (_applySelectedTaskPointBestQButton);
    selectedTitleRow->addWidget (_openSelectedTaskPointInIkButton);
    selectedPanelLayout->addLayout (selectedTitleRow);

    auto makeTaskPoseSpin = [this] (const QString& objectName, double minimum,
                                    double maximum, double step) -> QDoubleSpinBox* {
        QDoubleSpinBox* spin = new QDoubleSpinBox (_taskPointTab);
        spin->setObjectName (objectName);
        spin->setRange (minimum, maximum);
        spin->setDecimals (6);
        spin->setSingleStep (step);
        return spin;
    };
    QDoubleSpinBox* taskX = makeTaskPoseSpin (QStringLiteral ("taskPointXSpin"), -1000.0, 1000.0, 0.01);
    QDoubleSpinBox* taskY = makeTaskPoseSpin (QStringLiteral ("taskPointYSpin"), -1000.0, 1000.0, 0.01);
    QDoubleSpinBox* taskZ = makeTaskPoseSpin (QStringLiteral ("taskPointZSpin"), -1000.0, 1000.0, 0.01);
    QDoubleSpinBox* taskRoll = makeTaskPoseSpin (QStringLiteral ("taskPointRollSpin"), -360.0, 360.0, 1.0);
    QDoubleSpinBox* taskPitch = makeTaskPoseSpin (QStringLiteral ("taskPointPitchSpin"), -360.0, 360.0, 1.0);
    QDoubleSpinBox* taskYaw = makeTaskPoseSpin (QStringLiteral ("taskPointYawSpin"), -360.0, 360.0, 1.0);
    QGridLayout* poseGrid = new QGridLayout();
    poseGrid->addWidget (new QLabel (tr ("Position"), _taskPointTab), 0, 0);
    poseGrid->addWidget (new QLabel (tr ("X"), _taskPointTab), 0, 1);
    poseGrid->addWidget (taskX, 0, 2);
    poseGrid->addWidget (new QLabel (tr ("Y"), _taskPointTab), 0, 3);
    poseGrid->addWidget (taskY, 0, 4);
    poseGrid->addWidget (new QLabel (tr ("Z"), _taskPointTab), 0, 5);
    poseGrid->addWidget (taskZ, 0, 6);
    poseGrid->addWidget (new QLabel (tr ("Orientation"), _taskPointTab), 1, 0);
    poseGrid->addWidget (new QLabel (tr ("Roll"), _taskPointTab), 1, 1);
    poseGrid->addWidget (taskRoll, 1, 2);
    poseGrid->addWidget (new QLabel (tr ("Pitch"), _taskPointTab), 1, 3);
    poseGrid->addWidget (taskPitch, 1, 4);
    poseGrid->addWidget (new QLabel (tr ("Yaw"), _taskPointTab), 1, 5);
    poseGrid->addWidget (taskYaw, 1, 6);
    selectedPanelLayout->addLayout (poseGrid);

    auto bindTaskPoseSpin = [this] (QDoubleSpinBox* spin, int column) {
        connect (spin,
                 static_cast< void (QDoubleSpinBox::*) (double) > (
                     &QDoubleSpinBox::valueChanged),
                 this,
                 [this, column] (double value) {
                     if (_taskPointTable == nullptr || _taskPointModel == nullptr ||
                         _taskPointTable->selectionModel () == nullptr)
                         return;
                     const QModelIndexList selected =
                         _taskPointTable->selectionModel ()->selectedRows ();
                     if (selected.isEmpty ())
                         return;
                     _taskPointModel->setData (
                         _taskPointModel->index (selected.front ().row (), column),
                         value,
                         Qt::EditRole);
                 });
    };
    bindTaskPoseSpin (taskX, ColX);
    bindTaskPoseSpin (taskY, ColY);
    bindTaskPoseSpin (taskZ, ColZ);
    bindTaskPoseSpin (taskRoll, ColRoll);
    bindTaskPoseSpin (taskPitch, ColPitch);
    bindTaskPoseSpin (taskYaw, ColYaw);

    QTableWidget* taskPointDetailTable = new QTableWidget (_taskPointTab);
    taskPointDetailTable->setObjectName (QStringLiteral ("taskPointDetailTable"));
    taskPointDetailTable->setColumnCount (2);
    taskPointDetailTable->setHorizontalHeaderLabels ({tr ("Field"), tr ("Value")});
    taskPointDetailTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    taskPointDetailTable->setSelectionMode (QAbstractItemView::NoSelection);
    taskPointDetailTable->setWordWrap (true);
    configureAnalysisTable (taskPointDetailTable);
    taskPointDetailTable->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    taskPointDetailTable->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
    taskPointDetailTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    taskPointDetailTable->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    taskPointDetailTable->setMinimumHeight (110);
    taskPointDetailTable->setMaximumHeight (170);
    selectedPanelLayout->addWidget (taskPointDetailTable, 1);

    QToolButton* moreToggle = new QToolButton (_taskPointTab);
    moreToggle->setText (tr ("More..."));
    moreToggle->setCheckable (true);
    moreToggle->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    moreToggle->setArrowType (Qt::RightArrow);
    QWidget* moreContent = new QWidget (_taskPointTab);
    moreContent->setVisible (false);
    QVBoxLayout* moreLayout = new QVBoxLayout (moreContent);
    moreLayout->setContentsMargins (18, 0, 0, 0);
    QTableWidget* taskPointMoreTable = new QTableWidget (moreContent);
    taskPointMoreTable->setObjectName (QStringLiteral ("taskPointMoreTable"));
    taskPointMoreTable->setColumnCount (2);
    taskPointMoreTable->setHorizontalHeaderLabels ({tr ("Field"), tr ("Value")});
    taskPointMoreTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    taskPointMoreTable->setSelectionMode (QAbstractItemView::NoSelection);
    configureAnalysisTable (taskPointMoreTable);
    taskPointMoreTable->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    taskPointMoreTable->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
    taskPointMoreTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    taskPointMoreTable->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    taskPointMoreTable->setMinimumHeight (160);
    taskPointMoreTable->setMaximumHeight (240);
    moreLayout->addWidget (taskPointMoreTable);
    selectedPanelLayout->addWidget (moreToggle);
    selectedPanelLayout->addWidget (moreContent);
    tpLayout->addWidget (_taskPointSelectedPanel);
    connect (moreToggle, &QToolButton::toggled, this,
             [moreToggle, moreContent] (bool expanded) {
                 moreToggle->setArrowType (expanded ? Qt::DownArrow : Qt::RightArrow);
                 moreContent->setVisible (expanded);
             });

    setTaskPointTableColumnWidths ();
    updateTaskPointDetails ();
}

// buildWorkspaceTab keeps sampling, summary, samples, and selected details separate.
void KinematicAnalysisWidget::buildWorkspaceTab ()
{
    QVBoxLayout* layout = new QVBoxLayout (_workspaceTab);

    _workspaceSampleCountSpin = new QSpinBox (_workspaceTab);
    _workspaceSampleCountSpin->setRange (1, 1000000);
    _workspaceSampleCountSpin->setValue (1000);
    _workspaceGridStepsSpin = new QSpinBox (_workspaceTab);
    _workspaceGridStepsSpin->setRange (1, 100);
    _workspaceGridStepsSpin->setValue (5);
    _workspaceSeedSpin = new QSpinBox (_workspaceTab);
    _workspaceSeedSpin->setRange (1, 2147483647);
    _workspaceSeedSpin->setValue (1);
    _workspaceModeCombo = new QComboBox (_workspaceTab);
    _workspaceModeCombo->setObjectName (QStringLiteral ("workspaceModeCombo"));
    _workspaceModeCombo->addItem (tr("Random uniform"));
    _workspaceModeCombo->addItem (tr("Grid"));
    _workspaceCollisionCheck = new QCheckBox (tr("Collision"), _workspaceTab);
    _workspaceCollisionCheck->setChecked (true);
    _workspaceColorModeCombo = new QComboBox (_workspaceTab);
    _workspaceColorModeCombo->addItems ({tr("Reachability"), tr("Manipulability"),
                                         tr("Joint limit"), tr("Collision")});
    _workspaceRunButton = new QPushButton (tr("Run"), _workspaceTab);
    _workspaceRunButton->setObjectName (QStringLiteral ("workspaceRunButton"));
    _workspaceExportButton = new QPushButton (tr("Export CSV"), _workspaceTab);
    _workspaceExportButton->setEnabled (false);
    _workspaceOpenVisualizationButton =
        new QPushButton (tr("Open in Visualization"), _workspaceTab);
    _workspaceOpenVisualizationButton->setEnabled (false);
    _workspaceCancelButton = new QPushButton (tr("Cancel"), _workspaceTab);
    _workspaceCancelButton->setObjectName (QStringLiteral ("workspaceCancelButton"));
    _workspaceCancelButton->setEnabled (false);
    _workspaceProgressBar = new QProgressBar (_workspaceTab);
    _workspaceProgressBar->setRange (0, 1);
    _workspaceProgressBar->setValue (0);
    _workspaceProgressBar->setTextVisible (false);
    _workspaceProgressLabel = new QLabel (tr("Progress: 0 / 0 sample(s)"), _workspaceTab);

    QLabel* samplingTitle = new QLabel (tr("Sampling"), _workspaceTab);
    samplingTitle->setObjectName (QStringLiteral ("workspaceSamplingTitle"));
    samplingTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    QHBoxLayout* samplingTitleRow = new QHBoxLayout ();
    samplingTitleRow->addWidget (samplingTitle);
    samplingTitleRow->addStretch (1);
    samplingTitleRow->addWidget (_workspaceRunButton);
    samplingTitleRow->addWidget (_workspaceCancelButton);
    samplingTitleRow->addWidget (_workspaceExportButton);
    samplingTitleRow->addWidget (_workspaceOpenVisualizationButton);
    layout->addLayout (samplingTitleRow);

    QHBoxLayout* samplingControls = new QHBoxLayout ();
    QLabel* workspaceModeLabel = new QLabel (tr("Mode"), _workspaceTab);
    workspaceModeLabel->setObjectName (QStringLiteral ("workspaceModeLabel"));
    samplingControls->addWidget (workspaceModeLabel);
    samplingControls->addWidget (_workspaceModeCombo);
    QLabel* samplesLabel = new QLabel (tr("Samples"), _workspaceTab);
    samplesLabel->setObjectName (QStringLiteral ("workspaceSamplesLabel"));
    samplingControls->addWidget (samplesLabel);
    samplingControls->addWidget (_workspaceSampleCountSpin);
    _workspaceSampleCountSpin->setObjectName (QStringLiteral ("workspaceSamplesSpin"));
    QLabel* gridStepsLabel = new QLabel (tr("Grid steps"), _workspaceTab);
    gridStepsLabel->setObjectName (QStringLiteral ("workspaceGridStepsLabel"));
    samplingControls->addWidget (gridStepsLabel);
    samplingControls->addWidget (_workspaceGridStepsSpin);
    _workspaceGridStepsSpin->setObjectName (QStringLiteral ("workspaceGridStepsSpin"));
    samplingControls->addWidget (_workspaceCollisionCheck);
    samplingControls->addStretch (1);
    layout->addLayout (samplingControls);

    QToolButton* workspaceMoreToggle = new QToolButton (_workspaceTab);
    workspaceMoreToggle->setObjectName (QStringLiteral ("workspaceMoreToggle"));
    workspaceMoreToggle->setText (tr("More..."));
    workspaceMoreToggle->setCheckable (true);
    workspaceMoreToggle->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    workspaceMoreToggle->setArrowType (Qt::RightArrow);
    QWidget* workspaceMoreContent = new QWidget (_workspaceTab);
    workspaceMoreContent->setVisible (false);
    QHBoxLayout* workspaceMoreLayout = new QHBoxLayout (workspaceMoreContent);
    workspaceMoreLayout->setContentsMargins (18, 0, 0, 0);
    workspaceMoreLayout->addWidget (new QLabel (tr("Seed"), workspaceMoreContent));
    workspaceMoreLayout->addWidget (_workspaceSeedSpin);
    workspaceMoreLayout->addSpacing (12);
    workspaceMoreLayout->addWidget (new QLabel (tr("Visualization color"), workspaceMoreContent));
    workspaceMoreLayout->addWidget (_workspaceColorModeCombo);
    workspaceMoreLayout->addStretch (1);
    _workspaceDiagnosticsLabel = new QLabel (tr("Plan: -"), workspaceMoreContent);
    workspaceMoreLayout->addWidget (_workspaceDiagnosticsLabel);
    layout->addWidget (workspaceMoreToggle);
    layout->addWidget (workspaceMoreContent);
    connect (workspaceMoreToggle, &QToolButton::toggled, this,
             [workspaceMoreToggle, workspaceMoreContent] (bool expanded) {
                 workspaceMoreToggle->setArrowType (
                     expanded ? Qt::DownArrow : Qt::RightArrow);
                 workspaceMoreContent->setVisible (expanded);
             });

    QLabel* summaryTitle = new QLabel (tr("Workspace summary"), _workspaceTab);
    summaryTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    layout->addWidget (summaryTitle);
    auto makeSummaryLabel = [this] () -> QLabel* {
        QLabel* label = new QLabel (_workspaceTab);
        label->setTextFormat (Qt::RichText);
        label->setMinimumWidth (0);
        label->setWordWrap (true);
        label->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
        return label;
    };
    _workspaceSampleCountLabel = makeSummaryLabel ();
    _workspaceSampleCountLabel->setObjectName (QStringLiteral ("workspaceSampleCountLabel"));
    _workspaceCollisionFreeLabel = makeSummaryLabel ();
    _workspaceCollisionFreeLabel->setObjectName (
        QStringLiteral ("workspaceCollisionFreeLabel"));
    _workspacePassLabel = makeSummaryLabel ();
    _workspacePassLabel->setObjectName (QStringLiteral ("workspacePassLabel"));
    _workspaceWarningLabel = makeSummaryLabel ();
    _workspaceWarningLabel->setObjectName (QStringLiteral ("workspaceWarningLabel"));
    _workspaceFailLabel = makeSummaryLabel ();
    _workspaceFailLabel->setObjectName (QStringLiteral ("workspaceFailLabel"));
    _workspaceAvgManipulabilityLabel = makeSummaryLabel ();
    _workspaceAvgManipulabilityLabel->setObjectName (
        QStringLiteral ("workspaceAvgManipulabilityLabel"));
    const std::vector< std::pair< QLabel*, QString > > summaryLabels = {
        {_workspaceSampleCountLabel, tr("Samples")},
        {_workspaceCollisionFreeLabel, tr("Collision-free")},
        {_workspacePassLabel, tr("Pass")},
        {_workspaceWarningLabel, tr("Warning")},
        {_workspaceFailLabel, tr("Fail")},
        {_workspaceAvgManipulabilityLabel, tr("Avg manipulability")}};
    QHBoxLayout* summaryRow = new QHBoxLayout ();
    for (std::size_t i = 0; i < summaryLabels.size (); ++i) {
        summaryLabels[i].first->setText (
            QStringLiteral ("<b>%1</b><br>-").arg (summaryLabels[i].second));
        if (i > 0) {
            QFrame* separator = new QFrame (_workspaceTab);
            separator->setFrameShape (QFrame::VLine);
            separator->setFrameShadow (QFrame::Sunken);
            summaryRow->addWidget (separator);
        }
        summaryRow->addWidget (summaryLabels[i].first, 1);
    }
    layout->addLayout (summaryRow);

    layout->addWidget (_workspaceProgressBar);
    layout->addWidget (_workspaceProgressLabel);

    QLabel* samplesTitle = new QLabel (tr("Samples"), _workspaceTab);
    samplesTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    layout->addWidget (samplesTitle);
    _workspaceTable = new QTableWidget (_workspaceTab);
    _workspaceTable->setColumnCount (6);
    _workspaceTable->setHorizontalHeaderLabels ({
        tr("Index"), tr("Status"), tr("Collision"), tr("TCP position"),
        tr("Manipulability"), tr("Min margin")
    });
    _workspaceTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    configureAnalysisTable (_workspaceTable);
    _workspaceTable->setSelectionMode (QAbstractItemView::SingleSelection);
    _workspaceTable->setSelectionBehavior (QAbstractItemView::SelectRows);
    _workspaceTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    _workspaceTable->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    _workspaceTable->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
    _workspaceTable->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
    _workspaceTable->horizontalHeader ()->setSectionResizeMode (3, QHeaderView::Stretch);
    _workspaceTable->horizontalHeader ()->setSectionResizeMode (4, QHeaderView::ResizeToContents);
    _workspaceTable->horizontalHeader ()->setSectionResizeMode (5, QHeaderView::ResizeToContents);
    layout->addWidget (_workspaceTable, 1);

    QLabel* detailTitle = new QLabel (tr("Selected sample"), _workspaceTab);
    detailTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    layout->addWidget (detailTitle);
    _workspaceDetailTable = new QTableWidget (_workspaceTab);
    _workspaceDetailTable->setColumnCount (2);
    _workspaceDetailTable->setHorizontalHeaderLabels ({tr("Field"), tr("Value")});
    _workspaceDetailTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    _workspaceDetailTable->setSelectionMode (QAbstractItemView::NoSelection);
    _workspaceDetailTable->setWordWrap (true);
    configureAnalysisTable (_workspaceDetailTable);
    _workspaceDetailTable->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    _workspaceDetailTable->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
    _workspaceDetailTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    _workspaceDetailTable->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    _workspaceDetailTable->setMinimumHeight (150);
    _workspaceDetailTable->setMaximumHeight (220);
    layout->addWidget (_workspaceDetailTable);
    connect (_workspaceTable, &QTableWidget::itemSelectionChanged,
             this, [this] { updateWorkspaceSampleDetails (); });
    updateWorkspaceSampleDetails ();

    // P4:mode / sampleCount / gridSteps / seed 变化立即刷新 plan 标签;
    // color 变化触发 Visualization 重绘。
    connect (_workspaceModeCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (updateWorkspaceControls ()));
    connect (_workspaceSampleCountSpin, SIGNAL (valueChanged (int)),
             this, SLOT (updateWorkspaceControls ()));
    connect (_workspaceGridStepsSpin, SIGNAL (valueChanged (int)),
             this, SLOT (updateWorkspaceControls ()));
    connect (_workspaceSeedSpin, SIGNAL (valueChanged (int)),
             this, SLOT (updateWorkspaceControls ()));
    connect (_workspaceColorModeCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_workspaceOpenVisualizationButton, SIGNAL (clicked ()),
             this, SLOT (openWorkspaceInVisualization ()));
    updateWorkspaceControls ();
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

    QLabel* setupTitle = new QLabel (tr("Sampling setup"), _poseReachTab);
    setupTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    QHBoxLayout* setupTitleRow = new QHBoxLayout ();
    setupTitleRow->addWidget (setupTitle);
    setupTitleRow->addStretch (1);

    _poseAnalyzeButton = new QPushButton (tr("Run"), _poseReachTab);
    _poseAnalyzeButton->setObjectName (QStringLiteral ("poseRunButton"));
    _poseCancelButton = new QPushButton (tr("Cancel"), _poseReachTab);
    _poseCancelButton->setObjectName (QStringLiteral ("poseCancelButton"));
    _poseCancelButton->setEnabled (false);
    _poseExportButton = new QPushButton (tr("Export CSV"), _poseReachTab);
    _poseOpenVisualizationButton =
        new QPushButton (tr("Open in Visualization"), _poseReachTab);
    _poseOpenVisualizationButton->setEnabled (false);
    setupTitleRow->addWidget (_poseAnalyzeButton);
    setupTitleRow->addWidget (_poseCancelButton);
    setupTitleRow->addWidget (_poseExportButton);
    setupTitleRow->addWidget (_poseOpenVisualizationButton);
    layout->addLayout (setupTitleRow);

    QGridLayout* controls = new QGridLayout ();
    _poseTaskPointsSourceButton = new QToolButton (_poseReachTab);
    _poseTaskPointsSourceButton->setText (tr("Task points"));
    _poseTaskPointsSourceButton->setCheckable (true);
    _poseTaskPointsSourceButton->setAutoExclusive (true);
    _poseTaskPointsSourceButton->setChecked (true);
    _poseManualSourceButton = new QToolButton (_poseReachTab);
    _poseManualSourceButton->setText (tr("Manual positions"));
    _poseManualSourceButton->setCheckable (true);
    _poseManualSourceButton->setAutoExclusive (true);
    _poseDirectionSamplesSpin = new QSpinBox (_poseReachTab);
    _poseDirectionSamplesSpin->setObjectName (QStringLiteral ("poseDirectionSamplesSpin"));
    _poseDirectionSamplesSpin->setRange (0, 1000);
    _poseDirectionSamplesSpin->setValue (24);
    _poseRollSamplesSpin = new QSpinBox (_poseReachTab);
    _poseRollSamplesSpin->setObjectName (QStringLiteral ("poseRollSamplesSpin"));
    _poseRollSamplesSpin->setRange (1, 360);
    _poseRollSamplesSpin->setValue (1);
    _poseCollisionCheck = new QCheckBox (tr("Collision"), _poseReachTab);
    _poseCollisionCheck->setChecked (true);
    QLabel* poseSourceLabel = new QLabel (tr("Source:"), _poseReachTab);
    poseSourceLabel->setObjectName (QStringLiteral ("poseSourceLabel"));
    controls->addWidget (poseSourceLabel, 0, 0);
    controls->addWidget (_poseTaskPointsSourceButton, 0, 1);
    controls->addWidget (_poseManualSourceButton, 0, 2);
    QLabel* poseDirectionsLabel = new QLabel (tr("Directions:"), _poseReachTab);
    poseDirectionsLabel->setObjectName (QStringLiteral ("poseDirectionsLabel"));
    controls->addWidget (poseDirectionsLabel, 0, 3);
    controls->addWidget (_poseDirectionSamplesSpin, 0, 4);
    QLabel* poseRollsLabel = new QLabel (tr("Rolls:"), _poseReachTab);
    poseRollsLabel->setObjectName (QStringLiteral ("poseRollsLabel"));
    controls->addWidget (poseRollsLabel, 0, 5);
    controls->addWidget (_poseRollSamplesSpin, 0, 6);
    controls->addWidget (_poseCollisionCheck, 0, 7);
    controls->setColumnStretch (2, 1);
    controls->setColumnStretch (8, 1);
    layout->addLayout (controls);

    _poseManualPositionsPanel = new QWidget (_poseReachTab);
    QVBoxLayout* manualLayout = new QVBoxLayout (_poseManualPositionsPanel);
    manualLayout->setContentsMargins (0, 0, 0, 0);
    QHBoxLayout* manualTitleRow = new QHBoxLayout ();
    QLabel* manualTitle = new QLabel (tr("Manual positions"), _poseManualPositionsPanel);
    manualTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    _poseAddRowButton = new QPushButton (tr("Add"), _poseManualPositionsPanel);
    _poseRemoveRowButton = new QPushButton (tr("Remove"), _poseManualPositionsPanel);
    manualTitleRow->addWidget (manualTitle);
    manualTitleRow->addStretch (1);
    manualTitleRow->addWidget (_poseAddRowButton);
    manualTitleRow->addWidget (_poseRemoveRowButton);
    manualLayout->addLayout (manualTitleRow);

    _posePositionTable = new QTableWidget (_poseManualPositionsPanel);
    _posePositionTable->setColumnCount (3);
    _posePositionTable->setHorizontalHeaderLabels ({tr("X"), tr("Y"), tr("Z")});
    configureAnalysisTable (_posePositionTable);
    _posePositionTable->setMinimumHeight (110);
    _posePositionTable->setMaximumHeight (180);
    manualLayout->addWidget (_posePositionTable);
    layout->addWidget (_poseManualPositionsPanel);

    QLabel* summaryTitle = new QLabel (tr("Reachability summary"), _poseReachTab);
    summaryTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    layout->addWidget (summaryTitle);
    auto makeSummaryLabel = [this] () -> QLabel* {
        QLabel* label = new QLabel (_poseReachTab);
        label->setTextFormat (Qt::RichText);
        label->setMinimumWidth (0);
        label->setWordWrap (true);
        label->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
        return label;
    };
    _posePositionCountLabel = makeSummaryLabel ();
    _poseReachableLabel = makeSummaryLabel ();
    _poseCoverageLabel = makeSummaryLabel ();
    _posePassLabel = makeSummaryLabel ();
    _poseWarningLabel = makeSummaryLabel ();
    _poseFailLabel = makeSummaryLabel ();
    const std::vector< std::pair< QLabel*, QString > > summaryLabels = {
        {_posePositionCountLabel, tr("Positions")},
        {_poseReachableLabel, tr("Reachable")},
        {_poseCoverageLabel, tr("Coverage")},
        {_posePassLabel, tr("Pass")},
        {_poseWarningLabel, tr("Warning")},
        {_poseFailLabel, tr("Fail")}};
    QHBoxLayout* summaryRow = new QHBoxLayout ();
    for (std::size_t i = 0; i < summaryLabels.size (); ++i) {
        summaryLabels[i].first->setText (
            QStringLiteral ("<b>%1</b><br>-").arg (summaryLabels[i].second));
        if (i > 0) {
            QFrame* separator = new QFrame (_poseReachTab);
            separator->setFrameShape (QFrame::VLine);
            separator->setFrameShadow (QFrame::Sunken);
            summaryRow->addWidget (separator);
        }
        summaryRow->addWidget (summaryLabels[i].first);
    }
    summaryRow->addStretch (1);
    layout->addLayout (summaryRow);

    // P4:诊断标签显示 plan / per-position 方向 / 是否截断。
    _poseDiagnosticsLabel = new QLabel (
        tr("Plan: 0 IK target(s), 0 orientation(s) per position"),
        _poseReachTab);
    _poseRunDetailsLabel = new QLabel (tr("Run: no results"), _poseReachTab);
    _poseRunDetailsLabel->setWordWrap (true);
    _poseMoreToggle = new QToolButton (_poseReachTab);
    _poseMoreToggle->setText (tr("More..."));
    _poseMoreToggle->setCheckable (true);
    _poseMoreToggle->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    _poseMoreToggle->setArrowType (Qt::RightArrow);
    QWidget* moreContent = new QWidget (_poseReachTab);
    moreContent->setVisible (false);
    QVBoxLayout* moreLayout = new QVBoxLayout (moreContent);
    moreLayout->setContentsMargins (18, 0, 0, 0);
    moreLayout->addWidget (_poseDiagnosticsLabel);
    moreLayout->addWidget (_poseRunDetailsLabel);
    layout->addWidget (_poseMoreToggle);
    layout->addWidget (moreContent);
    connect (_poseMoreToggle, &QToolButton::toggled, this,
             [this, moreContent] (bool expanded) {
                 _poseMoreToggle->setArrowType (
                     expanded ? Qt::DownArrow : Qt::RightArrow);
                 moreContent->setVisible (expanded);
             });

    // P5:进度条,运行期间显示已完成的 IK target 数。
    _poseProgressBar = new QProgressBar (_poseReachTab);
    _poseProgressBar->setRange (0, 1);
    _poseProgressBar->setValue (0);
    _poseProgressBar->setTextVisible (false);
    _poseProgressLabel = new QLabel (tr("Progress: 0 / 0 IK target(s)"), _poseReachTab);
    layout->addWidget (_poseProgressBar);
    layout->addWidget (_poseProgressLabel);

    // P4:连接控件变化立即刷新 plan。
    connect (_poseTaskPointsSourceButton, &QToolButton::toggled, this,
             [this] (bool checked) {
                 if (checked)
                     updatePoseReachabilityControls ();
             });
    connect (_poseManualSourceButton, &QToolButton::toggled, this,
             [this] (bool checked) {
                 if (checked)
                     updatePoseReachabilityControls ();
             });
    connect (_poseDirectionSamplesSpin, SIGNAL (valueChanged (int)),
             this, SLOT (updatePoseReachabilityControls ()));
    connect (_poseRollSamplesSpin, SIGNAL (valueChanged (int)),
             this, SLOT (updatePoseReachabilityControls ()));
    connect (_posePositionTable, SIGNAL (itemChanged (QTableWidgetItem*)),
             this, SLOT (updatePoseReachabilityControls ()));
    connect (_poseAddRowButton, SIGNAL (clicked ()),
             this, SLOT (addPoseReachabilityRow ()));
    connect (_poseRemoveRowButton, &QPushButton::clicked, this, [this] {
        if (_posePositionTable == NULL)
            return;
        const int row = _posePositionTable->currentRow ();
        if (row < 0)
            return;
        _posePositionTable->removeRow (row);
        updatePoseReachabilityControls ();
    });
    updatePoseReachabilityControls ();

    _poseResultTable = new QTableWidget (_poseReachTab);
    _poseResultTable->setColumnCount (4);
    _poseResultTable->setHorizontalHeaderLabels ({
        tr("Index"), tr("Status"), tr("Position"), tr("Coverage")
    });
    _poseResultTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    configureAnalysisTable (_poseResultTable);
    _poseResultTable->setSelectionMode (QAbstractItemView::SingleSelection);
    _poseResultTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    _poseResultTable->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    _poseResultTable->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
    _poseResultTable->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::Stretch);
    _poseResultTable->horizontalHeader ()->setSectionResizeMode (3, QHeaderView::ResizeToContents);
    QLabel* resultsTitle = new QLabel (tr("Reachability results"), _poseReachTab);
    resultsTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    layout->addWidget (resultsTitle);
    layout->addWidget (_poseResultTable, 1);

    // P4:无数据时导出按钮禁用。
    _poseExportButton->setEnabled (false);
}

// buildVisualizationTab:可视化子页布局。顶部一行是数据源 / 投影 / 标量模式 /
// Fit / Export PNG / 打开独立窗口按钮;"More..." 折叠区内含渲染模式(Scatter /
// Envelope)、包络方向数、Pass/Warning/Fail/Unknown 显示开关、标签 / 网格 / 图例 /
// 点径;下方是内嵌 plot 与摘要标签。控件变化通过信号即时触发 refreshVisualization。
// buildReportTab:Report 子页布局。
//   - 顶部 summary 标签:显示当前 / 任务 / 工作空间 / 位姿可达性的综合状态;
//   - 7 个 DoubleSpinBox 调阈值(nearLimit / cond warn / cond fail / sigma /
//     manipulability / pos tol / ori tol)+ Apply thresholds 按钮;
//   - Refresh / Export JSON / Export CSV 三个动作按钮;
//   - 底部告警表 4 列:Severity / Code / Source / Message。
void KinematicAnalysisWidget::buildVisualizationTab ()
{
    QVBoxLayout* layout = new QVBoxLayout (_visualizationTab);

    QGridLayout* controls = new QGridLayout ();
    _visualSourceCombo = new QComboBox (_visualizationTab);
    _visualSourceCombo->addItem (tr("Task points"), 0);
    _visualSourceCombo->addItem (tr("Workspace"), 1);
    _visualSourceCombo->addItem (tr("Pose reachability"), 2);

    _visualProjectionCombo = new QComboBox (_visualizationTab);
    _visualProjectionCombo->setObjectName (QStringLiteral ("visualizationProjectionCombo"));
    _visualProjectionCombo->addItem (tr("XY"), static_cast<int> (VisualProjection::XY));
    _visualProjectionCombo->addItem (tr("XZ"), static_cast<int> (VisualProjection::XZ));
    _visualProjectionCombo->addItem (tr("YZ"), static_cast<int> (VisualProjection::YZ));

    _visualColorModeCombo = new QComboBox (_visualizationTab);
    _visualColorModeCombo->setObjectName (QStringLiteral ("visualizationScalarModeCombo"));
    _visualColorModeCombo->addItem (tr("Status"), static_cast<int> (VisualScalarMode::Status));
    _visualColorModeCombo->addItem (tr("Manipulability"), static_cast<int> (VisualScalarMode::Manipulability));
    _visualColorModeCombo->addItem (tr("Condition"), static_cast<int> (VisualScalarMode::Condition));
    _visualColorModeCombo->addItem (tr("Min joint margin"), static_cast<int> (VisualScalarMode::MinJointMargin));
    _visualColorModeCombo->addItem (tr("Position error"), static_cast<int> (VisualScalarMode::PositionError));
    _visualColorModeCombo->addItem (tr("Orientation error"), static_cast<int> (VisualScalarMode::OrientationError));
    _visualColorModeCombo->addItem (tr("Collision"), static_cast<int> (VisualScalarMode::Collision));
    _visualColorModeCombo->addItem (tr("Coverage"), static_cast<int> (VisualScalarMode::Coverage));

    _visualRenderModeCombo = new QComboBox (_visualizationTab);
    _visualRenderModeCombo->setObjectName (QStringLiteral ("visualizationRenderModeCombo"));
    _visualRenderModeCombo->addItem (
        visualRenderModeText (VisualRenderMode::Scatter),
        static_cast<int> (VisualRenderMode::Scatter));
    _visualRenderModeCombo->addItem (
        visualRenderModeText (VisualRenderMode::Envelope),
        static_cast<int> (VisualRenderMode::Envelope));

    _visualEnvelopeDirectionsSpin = new QSpinBox (_visualizationTab);
    _visualEnvelopeDirectionsSpin->setRange (24, 720);
    _visualEnvelopeDirectionsSpin->setSingleStep (12);
    _visualEnvelopeDirectionsSpin->setValue (180);

    _visualShowPassCheck = new QCheckBox (tr("Pass"), _visualizationTab);
    _visualShowWarningCheck = new QCheckBox (tr("Warning"), _visualizationTab);
    _visualShowFailCheck = new QCheckBox (tr("Fail"), _visualizationTab);
    _visualShowUnknownCheck = new QCheckBox (tr("Unknown"), _visualizationTab);
    _visualShowLabelsCheck = new QCheckBox (tr("Labels"), _visualizationTab);
    _visualShowGridCheck = new QCheckBox (tr("Grid"), _visualizationTab);
    _visualShowGridCheck->setObjectName (QStringLiteral ("visualizationShowGridCheck"));
    _visualShowLegendCheck = new QCheckBox (tr("Legend"), _visualizationTab);
    _visualShowPassCheck->setChecked (true);
    _visualShowWarningCheck->setChecked (true);
    _visualShowFailCheck->setChecked (true);
    _visualShowUnknownCheck->setChecked (true);
    _visualShowGridCheck->setChecked (true);
    _visualShowLegendCheck->setChecked (true);

    _visualPointSizeSpin = new QDoubleSpinBox (_visualizationTab);
    _visualPointSizeSpin->setRange (1.0, 10.0);
    _visualPointSizeSpin->setSingleStep (0.5);
    _visualPointSizeSpin->setValue (4.5);

    _visualResetViewButton = new QPushButton (tr("Fit"), _visualizationTab);
    _visualExportPngButton = new QPushButton (tr("Export PNG"), _visualizationTab);
    _visualOpenDialogButton = new QPushButton (tr("Open plot window"), _visualizationTab);
    _visualOpenDialogButton->setObjectName (
        QStringLiteral ("visualizationOpenPlotButton"));

    QLabel* setupTitle = new QLabel (tr("View setup"), _visualizationTab);
    setupTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    controls->addWidget (setupTitle, 0, 0);
    controls->addWidget (new QLabel (tr("Source"), _visualizationTab), 0, 1);
    controls->addWidget (_visualSourceCombo, 0, 2);
    controls->addWidget (new QLabel (tr("Projection"), _visualizationTab), 0, 3);
    controls->addWidget (_visualProjectionCombo, 0, 4);
    controls->addWidget (new QLabel (tr("Color"), _visualizationTab), 0, 5);
    controls->addWidget (_visualColorModeCombo, 0, 6);
    controls->addWidget (_visualResetViewButton, 0, 7);
    controls->addWidget (_visualExportPngButton, 0, 8);
    controls->addWidget (_visualOpenDialogButton, 0, 9);
    controls->setColumnStretch (2, 1);
    controls->setColumnStretch (4, 1);
    controls->setColumnStretch (6, 1);
    layout->addLayout (controls);

    QToolButton* moreToggle = new QToolButton (_visualizationTab);
    moreToggle->setText (tr("More..."));
    moreToggle->setCheckable (true);
    moreToggle->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    moreToggle->setArrowType (Qt::RightArrow);
    layout->addWidget (moreToggle);

    QWidget* moreContent = new QWidget (_visualizationTab);
    moreContent->setVisible (false);
    QGridLayout* moreControls = new QGridLayout (moreContent);
    moreControls->setContentsMargins (18, 0, 0, 0);
    QLabel* viewLabel = new QLabel (tr("View"), moreContent);
    viewLabel->setObjectName (QStringLiteral ("visualizationMoreViewLabel"));
    QLabel* envelopeLabel = new QLabel (tr("Envelope directions"), moreContent);
    envelopeLabel->setObjectName (QStringLiteral ("visualizationMoreEnvelopeLabel"));
    moreControls->addWidget (viewLabel, 0, 0);
    moreControls->addWidget (_visualRenderModeCombo, 0, 1);
    moreControls->addWidget (envelopeLabel, 0, 2);
    moreControls->addWidget (_visualEnvelopeDirectionsSpin, 0, 3);
    moreControls->addWidget (new QLabel (tr("Show"), moreContent), 1, 0);
    moreControls->addWidget (_visualShowPassCheck, 1, 1);
    moreControls->addWidget (_visualShowWarningCheck, 1, 2);
    moreControls->addWidget (_visualShowFailCheck, 1, 3);
    moreControls->addWidget (_visualShowUnknownCheck, 1, 4);
    moreControls->addWidget (new QLabel (tr("Display"), moreContent), 2, 0);
    moreControls->addWidget (_visualShowLabelsCheck, 2, 1);
    moreControls->addWidget (_visualShowGridCheck, 2, 2);
    moreControls->addWidget (_visualShowLegendCheck, 2, 3);
    moreControls->addWidget (new QLabel (tr("Point size"), moreContent), 2, 4);
    moreControls->addWidget (_visualPointSizeSpin, 2, 5);
    moreControls->setColumnStretch (6, 1);
    layout->addWidget (moreContent);
    connect (moreToggle, &QToolButton::toggled, this,
             [moreToggle, moreContent] (bool expanded) {
                 moreToggle->setArrowType (expanded ? Qt::DownArrow : Qt::RightArrow);
                 moreContent->setVisible (expanded);
             });

    QLabel* summaryTitle = new QLabel (tr("Summary"), _visualizationTab);
    summaryTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    layout->addWidget (summaryTitle);
    _visualSummaryLabel = new QLabel (_visualizationTab);
    _visualSummaryLabel->setTextFormat (Qt::RichText);
    _visualSummaryLabel->setText (
        tr("<b>Points</b> 0 | <b>Pass</b> - | <b>Warning</b> - | <b>Fail</b> -"));
    layout->addWidget (_visualSummaryLabel);

    QLabel* plotTitle = new QLabel (tr("Plot"), _visualizationTab);
    plotTitle->setStyleSheet (QStringLiteral ("font-weight: bold;"));
    layout->addWidget (plotTitle);
    _visualPlot = new KinematicAnalysisPlotWidget (_visualizationTab);
    _visualPlot->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget (_visualPlot, 1);

    connect (_visualPlot, &KinematicAnalysisPlotWidget::visualPointClicked,
             this, &KinematicAnalysisWidget::applyVisualizationPointQ);

    // P8:连接控件
    connect (_visualSourceCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (updateVisualizationControls ()));
    connect (_visualShowUnknownCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_visualShowGridCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_visualShowLegendCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshVisualization ()));
    connect (_visualPointSizeSpin, SIGNAL (valueChanged (double)),
             this, SLOT (refreshVisualization ()));
    connect (_visualResetViewButton, SIGNAL (clicked ()),
             this, SLOT (resetVisualizationView ()));
    connect (_visualExportPngButton, SIGNAL (clicked ()),
             this, SLOT (exportVisualizationPng ()));
    connect (_visualOpenDialogButton, SIGNAL (clicked ()),
             this, SLOT (openKinematicPlotDialog ()));
    connect (_visualRenderModeCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (updateVisualizationControls ()));
    connect (_visualEnvelopeDirectionsSpin, SIGNAL (valueChanged (int)),
             this, SLOT (onEnvelopeDebounceTimeout ()));
    _envelopeDebounceTimer->setInterval (200);
    updateVisualizationControls ();

    // 项目文档的脏状态只关心可重新执行分析的输入。统一在构造末尾补充信号连接，
    // 可以覆盖既有 UI 行为而不侵入每个槽函数；项目加载时用标志抑制这些信号。
    const auto notifyProjectEdit = [this] () {
        if (!_applyingProjectDocument)
            Q_EMIT projectDocumentChanged ();
    };
    for (QComboBox* combo : {_deviceCombo, _tcpFrameCombo, _lengthUnitCombo, _angleUnitCombo,
                             _workspaceModeCombo, _workspaceColorModeCombo, _visualSourceCombo,
                             _visualProjectionCombo, _visualColorModeCombo, _visualRenderModeCombo}) {
        connect (combo, static_cast< void (QComboBox::*) (int) > (&QComboBox::currentIndexChanged),
                 this, [notifyProjectEdit] (int) { notifyProjectEdit (); });
    }
    for (QDoubleSpinBox* spin : {_ikXSpin, _ikYSpin, _ikZSpin, _ikRollSpin, _ikPitchSpin,
                                 _ikYawSpin, _ikDuplicateQThresholdSpin, _visualPointSizeSpin,
                                 _thresholdNearLimitSpin, _thresholdConditionWarningSpin,
                                 _thresholdConditionFailSpin, _thresholdSingularValueSpin,
                                 _thresholdManipulabilitySpin, _thresholdPositionToleranceSpin,
                                 _thresholdOrientationToleranceSpin}) {
        connect (spin, static_cast< void (QDoubleSpinBox::*) (double) > (
                           &QDoubleSpinBox::valueChanged),
                 this, [notifyProjectEdit] (double) { notifyProjectEdit (); });
    }
    for (QSpinBox* spin : {_workspaceSampleCountSpin, _workspaceGridStepsSpin, _workspaceSeedSpin,
                           _poseDirectionSamplesSpin, _poseRollSamplesSpin,
                           _visualEnvelopeDirectionsSpin}) {
        connect (spin, static_cast< void (QSpinBox::*) (int) > (&QSpinBox::valueChanged),
                 this, [notifyProjectEdit] (int) { notifyProjectEdit (); });
    }
    for (QCheckBox* check : {_ikCollisionCheck, _workspaceCollisionCheck, _poseCollisionCheck,
                             _visualShowPassCheck, _visualShowWarningCheck, _visualShowFailCheck,
                             _visualShowUnknownCheck, _visualShowLabelsCheck, _visualShowGridCheck,
                             _visualShowLegendCheck}) {
        connect (check, &QCheckBox::toggled, this,
                 [notifyProjectEdit] (bool) { notifyProjectEdit (); });
    }
    connect (_poseTaskPointsSourceButton, &QToolButton::toggled, this,
             [notifyProjectEdit] (bool) { notifyProjectEdit (); });
    connect (_poseManualSourceButton, &QToolButton::toggled, this,
             [notifyProjectEdit] (bool) { notifyProjectEdit (); });
    connect (_posePositionTable, &QTableWidget::itemChanged, this,
             [notifyProjectEdit] (QTableWidgetItem*) { notifyProjectEdit (); });
    connect (_taskPointModel, &QAbstractItemModel::dataChanged, this,
             [notifyProjectEdit] () { notifyProjectEdit (); });
    connect (_taskPointModel, &QAbstractItemModel::modelReset, this, notifyProjectEdit);
    connect (_taskPointModel, &QAbstractItemModel::rowsInserted, this,
             [notifyProjectEdit] () { notifyProjectEdit (); });
    connect (_taskPointModel, &QAbstractItemModel::rowsRemoved, this,
             [notifyProjectEdit] () { notifyProjectEdit (); });
}

// updateVisualizationControls:数据源 / 渲染模式变化时刷新可视化配置。
//   - 按 source 动态重填 Color 下拉(只列出该源支持的标量模式),用 blockSignals
//     防止 setCurrentIndex 反向触发自身 currentIndexChanged 造成递归;
//   - 非 Workspace 源强制切回 Scatter 渲染(包络近似只对 Workspace 有意义);
//   - Envelope 模式下禁用 Scatter 专属控件,并显隐对应说明标签;
//   - 末尾调用 refreshVisualization 立即反映新配置。
void KinematicAnalysisWidget::updateVisualizationControls ()
{
    if (_visualSourceCombo == NULL || _visualColorModeCombo == NULL)
        return;

    const VisualPointSource source =
        _visualSourceCombo->currentData ().toInt () == 1 ?
            VisualPointSource::Workspace :
        _visualSourceCombo->currentData ().toInt () == 2 ?
            VisualPointSource::PoseReachability :
            VisualPointSource::TaskPoint;

    const QVariant currentData = _visualColorModeCombo->currentData ();
    VisualScalarMode currentMode = currentData.isValid () ?
        static_cast< VisualScalarMode > (currentData.toInt ()) :
        defaultVisualScalarModeForSource (source);
    if (!visualScalarModeSupported (source, currentMode))
        currentMode = defaultVisualScalarModeForSource (source);

    const bool blocked = _visualColorModeCombo->blockSignals (true);
    _visualColorModeCombo->clear ();
    const std::vector< VisualScalarMode > modes =
        supportedVisualScalarModes (source);
    for (VisualScalarMode mode : modes) {
        _visualColorModeCombo->addItem (
            visualScalarModeText (mode), static_cast< int > (mode));
    }
    const int index = _visualColorModeCombo->findData (
        static_cast< int > (currentMode));
    _visualColorModeCombo->setCurrentIndex (index >= 0 ? index : 0);
    _visualColorModeCombo->blockSignals (blocked);

    const bool workspaceSource = source == VisualPointSource::Workspace;
    int renderModeValue = _visualRenderModeCombo != NULL ?
        _visualRenderModeCombo->currentData ().toInt () :
        static_cast<int> (VisualRenderMode::Scatter);

    // 包络方向数:仅在 Envelope + Workspace 时启用
    // 非 Workspace 源强制切回 Scatter
    if (!workspaceSource && _visualRenderModeCombo != NULL &&
        renderModeValue == static_cast<int> (VisualRenderMode::Envelope)) {
        const int scatterIndex = _visualRenderModeCombo->findData (
            static_cast<int> (VisualRenderMode::Scatter));
        if (scatterIndex >= 0) {
            QSignalBlocker renderModeBlocker (_visualRenderModeCombo);
            _visualRenderModeCombo->setCurrentIndex (scatterIndex);
            renderModeValue = static_cast<int> (VisualRenderMode::Scatter);
        }
    }

    const bool envelopeActive = workspaceSource &&
        renderModeValue == static_cast<int> (VisualRenderMode::Envelope);
    if (_visualizationTab != NULL) {
        if (QLabel* label = _visualizationTab->findChild< QLabel* > (
                QStringLiteral ("visualizationMoreViewLabel")))
            label->setVisible (workspaceSource);
        if (QLabel* label = _visualizationTab->findChild< QLabel* > (
                QStringLiteral ("visualizationMoreEnvelopeLabel")))
            label->setVisible (envelopeActive);
    }
    if (_visualRenderModeCombo != NULL)
        _visualRenderModeCombo->setVisible (workspaceSource);
    if (_visualEnvelopeDirectionsSpin != NULL)
        _visualEnvelopeDirectionsSpin->setVisible (envelopeActive);

    // Envelope 模式下禁用不相关的 Scatter 控件
    if (_visualColorModeCombo != NULL)
        _visualColorModeCombo->setEnabled (!envelopeActive);
    if (_visualShowPassCheck != NULL)
        _visualShowPassCheck->setEnabled (!envelopeActive);
    if (_visualShowWarningCheck != NULL)
        _visualShowWarningCheck->setEnabled (!envelopeActive);
    if (_visualShowFailCheck != NULL)
        _visualShowFailCheck->setEnabled (!envelopeActive);
    if (_visualShowUnknownCheck != NULL)
        _visualShowUnknownCheck->setEnabled (!envelopeActive);
    if (_visualShowLabelsCheck != NULL)
        _visualShowLabelsCheck->setEnabled (!envelopeActive);
    if (_visualShowGridCheck != NULL)
        _visualShowGridCheck->setEnabled (!envelopeActive);
    if (_visualShowLegendCheck != NULL)
        _visualShowLegendCheck->setEnabled (!envelopeActive);
    if (_visualPointSizeSpin != NULL)
        _visualPointSizeSpin->setEnabled (!envelopeActive);

    refreshVisualization ();
}

// resetVisualizationView:Fit 视角。当前实现没有持久平移 / 缩放状态,因此直接
// 复用 refreshVisualization 重算一次数据,达到"重新适配可见数据"的效果。
void KinematicAnalysisWidget::resetVisualizationView ()
{
    refreshVisualization ();
    setStatus (tr("Visualization fitted to visible data."));
}

// openKinematicPlotDialog:打开无模式独立 plot 窗口。首次调用创建对话框并接入
// 投影 / 标量模式 / 渲染模式 / Fit / PNG 导出的请求回连到主控件;之后复用同一
// 实例(仅 raise + activateWindow),避免反复 new 造成窗口堆积。
void KinematicAnalysisWidget::openKinematicPlotDialog ()
{
    if (_plotDialog != nullptr) {
        _plotDialog->show ();
        _plotDialog->raise ();
        _plotDialog->activateWindow ();
        return;
    }

    _plotDialog = new KinematicPlotDialog (this);
    connect (_plotDialog, &KinematicPlotDialog::projectionRequested, this,
             [this] (VisualProjection projection) {
                 if (_visualProjectionCombo != nullptr) {
                     const int index = _visualProjectionCombo->findData (
                         static_cast<int> (projection));
                     if (index >= 0)
                         _visualProjectionCombo->setCurrentIndex (index);
                 }
             });
    connect (_plotDialog, &KinematicPlotDialog::scalarModeRequested, this,
             [this] (VisualScalarMode mode) {
                 if (_visualColorModeCombo != nullptr) {
                     const int index = _visualColorModeCombo->findData (
                         static_cast<int> (mode));
                     if (index >= 0)
                         _visualColorModeCombo->setCurrentIndex (index);
                     else {
                         const VisualProjection projection =
                             _visualProjectionCombo != nullptr ?
                                 static_cast< VisualProjection > (
                                     _visualProjectionCombo->currentData ().toInt ()) :
                                 VisualProjection::XY;
                         applyVisualDataToPlots (_visualData, projection);
                     }
                 }
             });
    connect (_plotDialog, &KinematicPlotDialog::renderModeRequested, this,
             [this] (VisualRenderMode mode) {
                 if (_visualRenderModeCombo != nullptr) {
                     const int index = _visualRenderModeCombo->findData (
                         static_cast<int> (mode));
                     if (index >= 0)
                         _visualRenderModeCombo->setCurrentIndex (index);
                 }
             });
    connect (_plotDialog, &KinematicPlotDialog::fitRequested,
             this, &KinematicAnalysisWidget::resetVisualizationView);
    connect (_plotDialog, &KinematicPlotDialog::exportPngRequested,
             this, &KinematicAnalysisWidget::exportVisualizationPng);
    connect (_plotDialog->plotWidget (), &KinematicAnalysisPlotWidget::visualPointClicked,
             this, &KinematicAnalysisWidget::applyVisualizationPointQ);

    const VisualProjection projection = _visualProjectionCombo != nullptr ?
        static_cast< VisualProjection > (_visualProjectionCombo->currentData ().toInt ()) :
        VisualProjection::XY;
    applyVisualDataToPlots (_visualData, projection);
    _plotDialog->show ();
    _plotDialog->raise ();
    _plotDialog->activateWindow ();
}

// applyVisualDataToPlots:把同一份可视化数据推给内嵌 plot 与独立 plot 窗口。
// 从各显示开关收集过滤条件(Pass/Warning/Fail/Unknown、标签、网格、图例、点径),
// 再连同投影 / 渲染模式 / 长度单位一起设置;独立窗口额外更新其显示状态面板。
void KinematicAnalysisWidget::applyVisualDataToPlots (
    const AnalysisVisualData& data, VisualProjection projection)
{
    AnalysisVisualFilters filters;
    filters.showPass = _visualShowPassCheck == NULL || _visualShowPassCheck->isChecked ();
    filters.showWarning = _visualShowWarningCheck == NULL || _visualShowWarningCheck->isChecked ();
    filters.showFail = _visualShowFailCheck == NULL || _visualShowFailCheck->isChecked ();
    filters.showUnknown = _visualShowUnknownCheck == NULL || _visualShowUnknownCheck->isChecked ();
    const bool showLabels = _visualShowLabelsCheck != NULL && _visualShowLabelsCheck->isChecked ();
    const bool showGrid = _visualShowGridCheck == NULL || _visualShowGridCheck->isChecked ();
    const bool showLegend = _visualShowLegendCheck == NULL || _visualShowLegendCheck->isChecked ();
    const double pointRadius = _visualPointSizeSpin != NULL ?
        _visualPointSizeSpin->value () : 4.5;

    const auto configurePlot = [&] (KinematicAnalysisPlotWidget* plot) {
        if (plot == NULL)
            return;
        plot->setProjection (projection);
        plot->setStatusFilters (filters.showPass, filters.showWarning, filters.showFail,
                                filters.showUnknown);
        plot->setShowLabels (showLabels);
        plot->setShowGrid (showGrid);
        plot->setShowLegend (showLegend);
        plot->setPointRadius (pointRadius);
        plot->setRenderMode (data.renderMode);
        plot->setLengthUnit (_lengthUnit);
        plot->setVisualData (data);
    };
    configurePlot (_visualPlot);
    if (_plotDialog != nullptr) {
        _plotDialog->setDisplayState (projection, data.scalarMode, data.renderMode, filters,
                                      showLabels, showGrid, showLegend, pointRadius, _lengthUnit);
        _plotDialog->setVisualData (data);
    }
}

// clearVisualizationData:清空可视化数据并以空数据刷新 plot(WorkCell 卸载时调用),
// 防止旧 WorkCell 的点集残留在可视化视图上造成误导。
void KinematicAnalysisWidget::clearVisualizationData ()
{
    _visualData = AnalysisVisualData ();
    const VisualProjection projection = _visualProjectionCombo != NULL ?
        static_cast< VisualProjection > (_visualProjectionCombo->currentData ().toInt ()) :
        VisualProjection::XY;
    applyVisualDataToPlots (_visualData, projection);
}

// applyVisualizationPointQ:点击可视化点 → 把该点记录的 Q 写回 RobWorkStudio state。
// 先校验点是否携带 Q 及维度是否与当前设备 DOF 一致,防止点击非本设备的点
// 破坏当前姿态;校验通过后 setQ + setState 并刷新 Current pose 页。
void KinematicAnalysisWidget::applyVisualizationPointQ (
    rws::AnalysisVisualPoint point)
{
    if (!point.hasQ || point.q.empty ()) {
        setStatus (tr("Visualization point has no saved reachable Q; 3D view unchanged."));
        return;
    }
    if (_studio == NULL) {
        setStatus (tr("Cannot apply visualization point: RobWorkStudio is unavailable."));
        return;
    }
    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == NULL) {
        setStatus (tr("Cannot apply visualization point: no valid device selected."));
        return;
    }
    if (point.q.size () != device->getDOF ()) {
        setStatus (tr("Cannot apply visualization point: Q dimension %1 does not match device DOF %2.")
                       .arg (static_cast< int > (point.q.size ()))
                       .arg (static_cast< int > (device->getDOF ())));
        return;
    }

    rw::kinematics::State state = currentState ();
    device->setQ (point.q, state);
    _studio->setState (state);
    refreshCurrentPose ();
    setStatus (tr("Applied visualization point %1 (%2 joints) to RobWorkStudio state.")
                   .arg (point.label.isEmpty () ? QStringLiteral ("-") : point.label)
                   .arg (static_cast< int > (point.q.size ())));
}

// exportVisualizationPng:把当前 plot 按 1400×900 尺寸渲染成 PNG 并保存到用户
// 选择的路径(渲染尺寸固定,保证导出图与 paintPlot 布局一致)。
void KinematicAnalysisWidget::exportVisualizationPng ()
{
    KinematicAnalysisPlotWidget* plot = _plotDialog != nullptr ?
        _plotDialog->plotWidget () : _visualPlot;
    if (plot == NULL) {
        setStatus (tr("No visualization plot to export."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export visualization PNG"), QString (),
        tr("PNG images (*.png)"));
    if (path.isEmpty ())
        return;
    const QImage image = plot->renderToImage (QSize (1400, 900));
    if (!image.save (path, "PNG")) {
        setStatus (tr("Failed to export visualization PNG."));
        return;
    }
    setStatus (tr("Exported visualization PNG to %1.").arg (path));
}

// openPoseReachabilityInVisualization:切换到 Visualization tab,把数据源设为
// Pose reachability,并把标量模式切到 Coverage,便于直接查看覆盖分布。
void KinematicAnalysisWidget::openPoseReachabilityInVisualization ()
{
    if (_visualSourceCombo != NULL)
        _visualSourceCombo->setCurrentIndex (2);
    updateVisualizationControls ();

    if (_visualColorModeCombo != NULL) {
        const int index = _visualColorModeCombo->findData (
            static_cast< int > (VisualScalarMode::Coverage));
        if (index >= 0)
            _visualColorModeCombo->setCurrentIndex (index);
    }
    if (_modeTabs != NULL)
        _modeTabs->setCurrentIndex (2);
    if (_exploreScroll != NULL && _visualizationTab != NULL)
        _exploreScroll->ensureWidgetVisible (_visualizationTab);
    refreshVisualization ();
}

// refreshVisualization:可视化核心重绘入口。按 source 从三个数据源之一构造可视化数据:
//   - Task points:逐行取 model 结果,无结果行标记 Unknown;
//   - Workspace:散点模式直接转换全部样本;Envelope 模式走异步包络计算——缓存命中
//     直接复用,未命中则启动 QtConcurrent::run(期间保持上一帧图像并立即 return,
//     完成后由 onEnvelopeFinished 更新 plot);
//   - Pose reachability:直接转换全部样本。
// 最终把投影 / 状态过滤 / 标签 / 网格 / 图例 / 点径等设置推给 plot,并刷新 summary。
void KinematicAnalysisWidget::refreshVisualization ()
{
    if (_visualPlot == NULL || _visualSourceCombo == NULL ||
        _visualProjectionCombo == NULL || _visualColorModeCombo == NULL)
        return;

    const int sourceKind = _visualSourceCombo->currentData ().toInt ();
    const VisualPointSource source =
        sourceKind == 1 ? VisualPointSource::Workspace :
        sourceKind == 2 ? VisualPointSource::PoseReachability :
                          VisualPointSource::TaskPoint;
    const VisualProjection projection =
        static_cast< VisualProjection > (_visualProjectionCombo->currentData ().toInt ());
    const VisualScalarMode scalarMode =
        static_cast< VisualScalarMode > (_visualColorModeCombo->currentData ().toInt ());
    const VisualRenderMode renderMode =
        _visualRenderModeCombo != NULL ?
            static_cast< VisualRenderMode > (_visualRenderModeCombo->currentData ().toInt ()) :
            VisualRenderMode::Scatter;
    if (_workcell == NULL) {
        AnalysisVisualData emptyData;
        emptyData.scalarMode = scalarMode;
        emptyData.renderMode = VisualRenderMode::Scatter;
        _visualData = emptyData;
        applyVisualDataToPlots (_visualData, projection);
        return;
    }
    if (!(sourceKind == 1 && renderMode == VisualRenderMode::Envelope))
        cancelEnvelopeRequest (false);

    AnalysisVisualData data;
    if (sourceKind == 0) {
        std::vector< TaskPointReachabilityResult > rows;
        if (_taskPointModel != nullptr) {
            const int rowCount = _taskPointModel->rowCount ();
            rows.reserve (static_cast< std::size_t > (rowCount));
            for (int row = 0; row < rowCount; ++row) {
                TaskPointReachabilityResult result =
                    _taskPointModel->hasResultAt (row) ?
                        _taskPointModel->resultAt (row) :
                        TaskPointReachabilityResult ();
                result.taskPoint = _taskPointModel->taskPointAt (row);
                if (!_taskPointModel->hasResultAt (row))
                    result.status = AnalysisStatus::Unknown;
                rows.push_back (result);
            }
        }
        data = visualDataFromTaskPointResults (rows, scalarMode, _lengthUnit, _angleUnit);
        data.renderMode = VisualRenderMode::Scatter;
    }
    else if (sourceKind == 1) {
        if (renderMode == VisualRenderMode::Envelope) {
            // 异步计算:取消之前的请求,启动新任务
            WorkspaceEnvelopeConfig config;
            config.projection = projection;
            config.angularDirections = _visualEnvelopeDirectionsSpin != NULL ?
                _visualEnvelopeDirectionsSpin->value () : 180;
            config.coordinateIterations = 6;
            config.cancel = std::make_shared< std::atomic< bool > > (false);

            // 值捕获快照
            const rw::core::Ptr< rw::models::Device > device = selectedDevice ();
            const rw::core::Ptr< rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();
            const rw::kinematics::State captureState = currentState ();
            const WorkspaceEnvelopeCacheKey cacheKey = makeEnvelopeCacheKey (
                device.get (), tcpFrame.get (), projection,
                config.angularDirections, config.coordinateIterations);

            data.renderMode = VisualRenderMode::Envelope;
            data.scalarMode = scalarMode;
            if (_envelopeCacheValid && cacheKey == _envelopeCacheKey) {
                data.envelope = _envelopeCacheData;
            }
            else {
                cancelEnvelopeRequest (false);
                _envelopeRunActive = true;
                ++_envelopeGeneration;
                _envelopeCancelRequested = config.cancel;
                _visualPlot->setProjection (projection);
                _visualPlot->setRenderMode (data.renderMode);
                // 显示占位状态
                if (_visualSummaryLabel != NULL)
                    _visualSummaryLabel->setText (tr("Approximate outer envelope: computing..."));
                // 保持前一次有效图像,不清除 plot

                const WorkspaceEnvelopeConfig captureConfig = config;
                const int captureGeneration = _envelopeGeneration;
                QFuture< WorkspaceEnvelopeRunResult > future = QtConcurrent::run (
                    [device, tcpFrame, captureState, captureConfig, captureGeneration] () {
                    WorkspaceEnvelopeRunResult result;
                    result.generation = captureGeneration;
                    try {
                        result.envelope = computeWorkspaceEnvelope (
                            device.get (), tcpFrame.get (), captureState, captureConfig);
                        result.cancelled = captureConfig.cancel != nullptr &&
                            captureConfig.cancel->load ();
                    }
                    catch (const std::exception& ex) {
                        result.errorMessage = QString::fromLocal8Bit (ex.what ());
                    }
                    catch (...) {
                        result.errorMessage =
                            QStringLiteral ("Unexpected envelope computation error.");
                    }
                    return result;
                });
                _envelopeWatcher->setFuture (future);
                _visualData = data;
                applyVisualDataToPlots (_visualData, projection);
                return; // 异步完成后再更新 plot
            }
        }
        else {
            data = visualDataFromWorkspaceSamples (_workspaceSamples, scalarMode, _lengthUnit);
            data.renderMode = VisualRenderMode::Scatter;
        }
    }
    else {
        data = visualDataFromPoseReachabilitySamples (
            _poseReachabilitySamples, scalarMode, _lengthUnit);
        data.renderMode = VisualRenderMode::Scatter;
    }

    _visualData = data;
    applyVisualDataToPlots (_visualData, projection);

    if (_visualSummaryLabel != NULL) {
        if (data.renderMode == VisualRenderMode::Envelope) {
            if (data.envelope.valid) {
                _visualSummaryLabel->setText (
                    tr("<b>Boundary points</b> %1 | <b>Projection</b> %2 | "
                       "<b>Size</b> %3 x %4 %5 | <b>Max radius</b> %6 %5")
                        .arg (static_cast<int> (data.envelope.boundary.size ()))
                        .arg (visualProjectionText (projection))
                        .arg (QString::number (displayLengthFromMeters (
                            data.envelope.width, _lengthUnit), 'f', 3))
                        .arg (QString::number (displayLengthFromMeters (
                            data.envelope.height, _lengthUnit), 'f', 3))
                        .arg (QString::fromLatin1 (unitSuffix (_lengthUnit)))
                        .arg (QString::number (displayLengthFromMeters (
                            data.envelope.maxRadius, _lengthUnit), 'f', 3)));
            }
            else {
                _visualSummaryLabel->setText (
                    tr("<b>Boundary</b> No valid device or joint limits available."));
            }
        }
        else {
            AnalysisVisualFilters filters;
            filters.showPass = _visualShowPassCheck == NULL || _visualShowPassCheck->isChecked ();
            filters.showWarning = _visualShowWarningCheck == NULL || _visualShowWarningCheck->isChecked ();
            filters.showFail = _visualShowFailCheck == NULL || _visualShowFailCheck->isChecked ();
            filters.showUnknown = _visualShowUnknownCheck == NULL || _visualShowUnknownCheck->isChecked ();
            const AnalysisVisualStatusSummary summary = summarizeVisualData (data, filters);

            QString scalarRange = tr("-");
            if (data.hasFiniteScalar) {
                scalarRange = tr("%1 .. %2")
                    .arg (QString::number (data.scalarMin, 'g', 6))
                    .arg (QString::number (data.scalarMax, 'g', 6));
            }
            _visualSummaryLabel->setText (
                tr("<b>Points</b> %1 | <b>Visible</b> %2 | <b>Pass</b> %3 | "
                   "<b>Warning</b> %4 | <b>Fail</b> %5 | <b>Collision</b> %6 | "
                   "<b>%7</b> %8")
                    .arg (static_cast< int > (summary.totalCount))
                    .arg (static_cast< int > (summary.visibleCount))
                    .arg (static_cast< int > (summary.passCount))
                    .arg (static_cast< int > (summary.warningCount))
                    .arg (static_cast< int > (summary.failCount))
                    .arg (static_cast< int > (summary.collisionCount))
                    .arg (visualScalarModeText (scalarMode))
                    .arg (scalarRange));
        }
    }
}

// =============================================================================
//  包络异步计算结果 / 防抖
// =============================================================================

// onEnvelopeFinished:包络后台计算完成回调(UI 线程)。先用 generation 号丢弃
// 过期请求的返回(参数在途变化后旧任务结果无效),再依次处理取消 / 异常 /
// 正常三种结果;正常结果会写回缓存供后续免算复用。
void KinematicAnalysisWidget::onEnvelopeFinished ()
{
    if (_envelopeWatcher == NULL || !_envelopeWatcher->isFinished ())
        return;

    const WorkspaceEnvelopeRunResult result = _envelopeWatcher->result ();
    if (result.generation != _envelopeGeneration)
        return;
    _envelopeRunActive = false;

    // 检查取消(取消时返回无效 envelope)
    if (result.cancelled) {
        if (_visualSummaryLabel != NULL)
            _visualSummaryLabel->setText (tr("Approximate outer envelope: cancelled."));
        return;
    }
    if (!result.errorMessage.isEmpty ()) {
        if (_visualSummaryLabel != NULL)
            _visualSummaryLabel->setText (
                tr("Approximate outer envelope: %1").arg (result.errorMessage));
        return;
    }

    const AnalysisEnvelopeData envelope = result.envelope;

    // 构造可视化数据
    AnalysisVisualData data;
    data.renderMode = VisualRenderMode::Envelope;
    data.envelope = envelope;

    if (envelope.valid) {
        const WorkspaceEnvelopeCacheKey cacheKey = makeEnvelopeCacheKey (
            selectedDevice ().get (), selectedTcpFrame ().get (), envelope.projection,
            _visualEnvelopeDirectionsSpin != NULL ? _visualEnvelopeDirectionsSpin->value () : 180,
            6);
        _envelopeCacheKey = cacheKey;
        _envelopeCacheData = envelope;
        _envelopeCacheValid = true;
    }

    _visualData = data;
    applyVisualDataToPlots (_visualData, envelope.projection);

    if (_visualSummaryLabel != NULL) {
        if (envelope.valid) {
            _visualSummaryLabel->setText (
                tr("Approximate outer envelope: %1 pts | %2 | %3x%4 %5 | Rmax %6 %5 | approximate - not exact reachability")
                    .arg (static_cast<int> (envelope.boundary.size ()))
                    .arg (visualProjectionText (envelope.projection))
                    .arg (QString::number (displayLengthFromMeters (
                        envelope.width, _lengthUnit), 'f', 3))
                    .arg (QString::number (displayLengthFromMeters (
                        envelope.height, _lengthUnit), 'f', 3))
                    .arg (QString::fromLatin1 (unitSuffix (_lengthUnit)))
                    .arg (QString::number (displayLengthFromMeters (
                        envelope.maxRadius, _lengthUnit), 'f', 3)));
        }
        else {
            _visualSummaryLabel->setText (
                tr("Approximate outer envelope: no valid device or joint limits available."));
        }
    }
}

// onEnvelopeDebounceTimeout:包络方向数防抖槽。方向数 spin 每次变化都重启
// 200ms 定时器,只有连续 200ms 无新变化才真正触发 refreshVisualization,
// 避免用户拖动数值时频繁启动后台包络计算。若新请求会覆盖在途请求则先取消。
void KinematicAnalysisWidget::onEnvelopeDebounceTimeout ()
{
    if (_envelopeDebounceTimer == NULL)
        return;
    // 防抖:方向数 spin 变化时重启定时器,200ms 无新变化才触发刷新
    const bool envelopeActive =
        _visualSourceCombo != NULL &&
        _visualRenderModeCombo != NULL &&
        visualEnvelopeModeAvailable (
            _visualSourceCombo->currentData ().toInt (),
            _visualRenderModeCombo->currentData ().toInt ());
    const bool requestActive = _envelopeRunActive ||
        (_envelopeWatcher != NULL && _envelopeWatcher->isRunning ());
    if (visualEnvelopeDirectionChangeSupersedesRequest (envelopeActive, requestActive))
        cancelEnvelopeRequest (false);
    invalidateEnvelopeCache ();
    _envelopeDebounceTimer->stop ();
    _envelopeDebounceTimer->start (200);
}

// buildReportTab:Report 子页布局——汇总标签 + 4 个过滤下拉(Stage / Feasibility /
// Quality / Failure)+ 区域文本过滤 + 7 个阈值 SpinBox + Apply / Refresh / 导出
// 按钮 + 告警表。过滤与阈值控件变化时通过信号即时刷新汇总视图(不重跑分析)。
void KinematicAnalysisWidget::buildReportTab ()
{
    QVBoxLayout* layout = new QVBoxLayout (_reportTab);

    _reportSummaryLabel = new QLabel (tr("No report data."), _reportTab);
    layout->addWidget (_reportSummaryLabel);

    QGridLayout* reportFilterGrid = new QGridLayout ();
    _reportStageFilterCombo = new QComboBox (_reportTab);
    _reportStageFilterCombo->setObjectName (QStringLiteral ("reportStageFilter"));
    _reportStageFilterCombo->addItem (tr ("All stages"));
    _reportStageFilterCombo->addItems ({tr ("Estimated"), tr ("Quick"), tr ("Verified")});
    _reportFeasibilityFilterCombo = new QComboBox (_reportTab);
    _reportFeasibilityFilterCombo->setObjectName (QStringLiteral ("reportFeasibilityFilter"));
    _reportFeasibilityFilterCombo->addItem (tr ("All feasibility"));
    _reportFeasibilityFilterCombo->addItems ({tr ("Feasible"), tr ("Infeasible"),
                                               tr ("DataInsufficient"), tr ("NotEvaluated")});
    _reportQualityFilterCombo = new QComboBox (_reportTab);
    _reportQualityFilterCombo->setObjectName (QStringLiteral ("reportQualityFilter"));
    _reportQualityFilterCombo->addItem (tr ("All quality"));
    _reportQualityFilterCombo->addItems ({tr ("Good"), tr ("Degraded"), tr ("Critical"),
                                          tr ("Unknown")});
    _reportFailureFilterCombo = new QComboBox (_reportTab);
    _reportFailureFilterCombo->setObjectName (QStringLiteral ("reportFailureFilter"));
    _reportFailureFilterCombo->addItem (tr ("All failure reasons"));
    for (int reason = static_cast< int > (KinematicFailureReason::None) + 1;
         reason <= static_cast< int > (KinematicFailureReason::FrameNotFound); ++reason)
        _reportFailureFilterCombo->addItem (
            QString::fromLatin1 (toString (static_cast< KinematicFailureReason > (reason))));
    _reportRegionFilterEdit = new QLineEdit (_reportTab);
    _reportRegionFilterEdit->setObjectName (QStringLiteral ("reportRegionFilter"));
    _reportRegionFilterEdit->setPlaceholderText (tr ("Region id (optional)"));
    reportFilterGrid->addWidget (new QLabel (tr ("Stage:"), _reportTab), 0, 0);
    reportFilterGrid->addWidget (_reportStageFilterCombo, 0, 1);
    reportFilterGrid->addWidget (new QLabel (tr ("Feasibility:"), _reportTab), 0, 2);
    reportFilterGrid->addWidget (_reportFeasibilityFilterCombo, 0, 3);
    reportFilterGrid->addWidget (new QLabel (tr ("Quality:"), _reportTab), 1, 0);
    reportFilterGrid->addWidget (_reportQualityFilterCombo, 1, 1);
    reportFilterGrid->addWidget (new QLabel (tr ("Failure:"), _reportTab), 1, 2);
    reportFilterGrid->addWidget (_reportFailureFilterCombo, 1, 3);
    reportFilterGrid->addWidget (new QLabel (tr ("Region:"), _reportTab), 2, 0);
    reportFilterGrid->addWidget (_reportRegionFilterEdit, 2, 1, 1, 3);
    layout->addLayout (reportFilterGrid);

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

// setTaskPointTableColumnWidths:按 taskPointCompactTableColumns 决定任务点表格在
// 紧凑模式下哪些列可见(其余隐藏),并设置关键列的固定宽度与拉伸策略,
// 保证常用的 Id / Name / RefFrame / TcpFrame / Status 列稳定可读。
void KinematicAnalysisWidget::setTaskPointTableColumnWidths ()
{
    if (_taskPointTable == nullptr)
        return;
    const std::vector< int > compactColumns = taskPointCompactTableColumns ();
    for (int column = 0; column < _taskPointTable->model ()->columnCount (); ++column) {
        const bool visible =
            std::find (compactColumns.begin (), compactColumns.end (), column) != compactColumns.end ();
        _taskPointTable->setColumnHidden (column, !visible);
    }
    _taskPointTable->resizeColumnsToContents ();
    _taskPointTable->setColumnWidth (ColEnabled, 28);
    _taskPointTable->horizontalHeader ()->setSectionResizeMode (ColName, QHeaderView::Stretch);
    _taskPointTable->setColumnWidth (ColRefFrame, 150);
    _taskPointTable->setColumnWidth (ColTcpFrame, 150);
    _taskPointTable->setColumnWidth (ColStatus, 90);
}

// installTaskPointDelegates:为任务点表格安装单元格编辑器。收集 WORLD / 设备基座 /
// WorkCell 全部 frame 名作为 refFrame 与 tcpFrame 下拉候选,再交给统一工厂
// rws::installTaskPointDelegates 按列安装 ComboBox / 数值 / 布尔 delegate。
void KinematicAnalysisWidget::installTaskPointDelegates ()
{
    if (_taskPointTable == nullptr)
        return;

    // 收集 refFrame / tcpFrame 候选:WORLD + device base + WorkCell 全部 frame +
    // 顶部 TCP combo 当前值。addUnique 内部去重,空值跳过。
    QStringList frameValues;
    QStringList tcpValues;
    QSet< QString > frameSeen;
    QSet< QString > tcpSeen;
    auto addUnique = [] (QStringList& values, QSet< QString >& seen, const QString& value) {
        const QString trimmed = value.trimmed ();
        if (trimmed.isEmpty () || seen.contains (trimmed))
            return;
        seen.insert (trimmed);
        values << trimmed;
    };
    addUnique (frameValues, frameSeen, QStringLiteral ("WORLD"));
    const rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device != nullptr && device->getBase () != nullptr)
        addUnique (frameValues, frameSeen,
                   QString::fromStdString (device->getBase ()->getName ()));
    // WorkCell 全部 frame 名字 refFrame / tcpFrame 都能用。
    const QStringList wcFrameNames = collectWorkCellFrameNames (_workcell);
    for (const QString& name : wcFrameNames) {
        addUnique (frameValues, frameSeen, name);
        addUnique (tcpValues, tcpSeen, name);
    }
    if (_tcpFrameCombo != nullptr)
        addUnique (tcpValues, tcpSeen, _tcpFrameCombo->currentText ());
    if (tcpValues.isEmpty ())
        addUnique (tcpValues, tcpSeen, QStringLiteral ("TCP"));

    // P3-A 工厂:内部已经用 setItemDelegateForColumn,直接传 view 即可。
    rws::installTaskPointDelegates (_taskPointTable, frameValues, tcpValues);
}

// addTaskPointRow:P3-A 迁移到 model API。
// 在 model 末尾插入一行,默认值与 P2 一致(0 位姿 / Generic / WORLD /
// 顶部 TCP / 0.001 m posTol / 1.0 deg oriTol / 1.0 weight / enabled)。
void KinematicAnalysisWidget::addTaskPointRow ()
{
    if (_taskPointModel == nullptr)
        return;
    const int row = _taskPointModel->rowCount ();
    _taskPointModel->insertRows (row, 1);
    auto setStr = [this, row] (int col, const QString& s) {
        _taskPointModel->setData (_taskPointModel->index (row, col), s, Qt::EditRole);
    };
    setStr (ColType,  QStringLiteral ("Generic"));
    // refFrame 默认 WORLD; tcpFrame 默认沿用顶部 TCP 下拉框。
    setStr (ColRefFrame, QStringLiteral ("WORLD"));
    QString defaultTcp = QStringLiteral ("TCP");
    if (_tcpFrameCombo != nullptr && !_tcpFrameCombo->currentText ().isEmpty ())
        defaultTcp = _tcpFrameCombo->currentText ();
    setStr (ColTcpFrame, defaultTcp);
    setStr (ColX,        QStringLiteral ("0"));
    setStr (ColY,        QStringLiteral ("0"));
    setStr (ColZ,        QStringLiteral ("0"));
    setStr (ColRoll,     QStringLiteral ("0"));
    setStr (ColPitch,    QStringLiteral ("0"));
    setStr (ColYaw,      QStringLiteral ("0"));
    setStr (ColPosTol,   QStringLiteral ("0.001"));
    setStr (ColOriTol,   QStringLiteral ("1.0"));
    setStr (ColFreeRoll, QStringLiteral ("false"));
    setStr (ColWeight,   QStringLiteral ("1.0"));
    setStr (ColNote,     QString ());
    setTaskPointTableColumnWidths ();
    setStatus (tr ("Added task point row %1.").arg (row + 1));
}

// removeSelectedTaskPointRow:P3-A 迁移到 model API。
void KinematicAnalysisWidget::removeSelectedTaskPointRow ()
{
    if (_taskPointTable == nullptr || _taskPointModel == nullptr)
        return;
    const QModelIndexList selected = _taskPointTable->selectionModel ()->selectedRows ();
    if (selected.isEmpty ()) {
        setStatus (tr ("No task point row selected."));
        return;
    }
    // 删除多个选中行时,从后往前删避免下标错位。
    QList< int > rows;
    for (const QModelIndex& idx : selected)
        rows.append (idx.row ());
    std::sort (rows.begin (), rows.end (), std::greater< int > ());
    for (int row : rows)
        _taskPointModel->removeRows (row, 1);
    setStatus (tr ("Removed %1 task point row(s).").arg (rows.size ()));
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
QTableWidgetItem* setCell (QTableWidget* t, int r, int c, double value, bool editable)
{
    return setCell (t, r, c, QString::number (value, 'g', 8), editable);
}
QString cellText (QTableWidget* t, int r, int c)
{
    QTableWidgetItem* item = t->item (r, c);
    return item == nullptr ? QString () : item->text ();
}

QString csvEscape (QString value)
{
    if (!value.contains (QLatin1Char (',')) &&
        !value.contains (QLatin1Char ('"')) &&
        !value.contains (QLatin1Char ('\r')) &&
        !value.contains (QLatin1Char ('\n')))
        return value;
    value.replace (QStringLiteral ("\""), QStringLiteral ("\"\""));
    return QStringLiteral ("\"%1\"").arg (value);
}

QString csvJoin (const QStringList& fields)
{
    QStringList escaped;
    escaped.reserve (fields.size ());
    for (const QString& field : fields)
        escaped << csvEscape (field);
    return escaped.join (QStringLiteral (","));
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

// P1 bestUsableSolution:为每个 task point 选"代表解"展示在 bestQ / 误差列。
//   - 优先第一条无碰撞 + (Pass 或 Warning) 的解;
//   - 全部 collision 时退回到第一条解(诊断用),让用户看到 IK 真的解到了;
//   - 无解时返回 nullptr,UI 写 "-"。
const rws::KinematicIkSolution* bestUsableSolution (const rws::KinematicIkAnalysisResult& ik)
{
    const rws::KinematicIkSolution* fallback = nullptr;
    for (const auto& solution : ik.solutions) {
        if (!solution.inCollision &&
            (solution.status == rws::AnalysisStatus::Pass ||
             solution.status == rws::AnalysisStatus::Warning))
            return &solution;
        if (fallback == nullptr)
            fallback = &solution;
    }
    return fallback;
}

// P2 paintResultStates:按 status / 校验结果给整行染色。
//   优先级:验证错误(浅红) > Fail(浅红) > Warning / Skipped(浅黄) > Pass(浅绿) > 默认。
// 同时把 status / reason / failureReasons 拼成 tooltip 方便 hover 诊断。
void paintResultStates (QTableWidget* t, int row,
                        rws::AnalysisStatus status,
                        const QString& reasonText,
                        const std::vector< rws::AnalysisWarning >& warnings,
                        const QString& note = QString ())
{
    if (t == nullptr || row < 0 || row >= t->rowCount ())
        return;
    QColor bg;
    switch (status) {
        case rws::AnalysisStatus::Fail:    bg = QColor (255, 224, 224); break;
        case rws::AnalysisStatus::Warning: bg = QColor (255, 247, 205); break;
        case rws::AnalysisStatus::Pass:    bg = QColor (224, 247, 224); break;
        default:                           bg = QColor ();            break;
    }
    QStringList tipLines;
    tipLines << QStringLiteral ("status=%1").arg (QString::fromLatin1 (statusText (status)));
    if (!reasonText.isEmpty () && reasonText != QStringLiteral ("-"))
        tipLines << QStringLiteral ("reason=%1").arg (reasonText);
    if (!note.isEmpty ())
        tipLines << QStringLiteral ("note=%1").arg (note);
    for (const rws::AnalysisWarning& w : warnings) {
        tipLines << QStringLiteral ("[%1] %2: %3")
            .arg (QString::fromLatin1 (statusText (w.severity)))
            .arg (QString::fromStdString (w.code))
            .arg (QString::fromStdString (w.message));
    }
    const QString tip = tipLines.join (QStringLiteral ("\n"));
    for (int c = 0; c < t->columnCount (); ++c) {
        QTableWidgetItem* item = t->item (row, c);
        if (item == nullptr)
            continue;
        if (bg.isValid ())
            item->setBackground (bg);
        else
            item->setData (Qt::BackgroundRole, QVariant ());
        if (!tip.isEmpty ())
            item->setToolTip (tip);
    }
}

// readTaskPointFromRow:把表格一行(0-based row)完整读成 TaskPoint。
// 任何字段非法(数值 / freeRoll / 缺少字段)都会把首条错误写入 *error,
// 调用方负责 abort(返回空 TaskPoint,可以用 TaskPoint{} 标识)。
// 用途:在 import 完成后做"完整字段级"validation,而不是只看 id/name。
TaskPoint readTaskPointFromRow (const QTableWidget* t, int row, QString* error)
{
    TaskPoint p;
    if (t == nullptr || row < 0 || row >= t->rowCount ()) {
        if (error != nullptr)
            *error = QObject::tr("Row index out of range.");
        return TaskPoint {};
    }
    if (error != nullptr)
        error->clear ();
    auto cellText = [t, row] (int c) {
        QTableWidgetItem* item = t->item (row, c);
        return item == nullptr ? QString () : item->text ();
    };
    auto readNumber = [error, &cellText, row] (int column, const QString& field, double& value) {
        bool ok = false;
        value = cellText (column).toDouble (&ok);
        if (ok && std::isfinite (value))
            return true;
        if (error != nullptr) {
            *error = QObject::tr("Task point row %1 has an invalid %2 value.")
                         .arg (row + 1).arg (field);
        }
        return false;
    };
    auto readBool = [error, &cellText, row] (int column, const QString& field, bool& value) {
        const QString raw = cellText (column).trimmed ();
        const QString t2 = raw.toLower ();
        if (t2 == "true" || t2 == "1" || t2 == "yes" || t2 == "y" || t2 == "on") {
            value = true;
            return true;
        }
        if (t2 == "false" || t2 == "0" || t2 == "no" || t2 == "n" || t2 == "off") {
            value = false;
            return true;
        }
        if (error != nullptr) {
            *error = QObject::tr("Task point row %1 has an invalid %2 value: '%3'.")
                         .arg (row + 1).arg (field).arg (raw);
        }
        return false;
    };

    p.id        = cellText (ColId).toStdString ();
    p.name      = cellText (ColName).toStdString ();
    const QString typeText = cellText (ColType);
    p.type      = TaskPointType::Generic;
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
    // 空 refFrame / tcpFrame 不在此处填默认值,由 RobotAnalysisValidation 拦截。
    p.refFrame  = cellText (ColRefFrame).toStdString ();
    p.tcpFrame  = cellText (ColTcpFrame).toStdString ();
    if (!readNumber (ColX,      QObject::tr("x"),                       p.position[0]) ||
        !readNumber (ColY,      QObject::tr("y"),                       p.position[1]) ||
        !readNumber (ColZ,      QObject::tr("z"),                       p.position[2]) ||
        !readNumber (ColRoll,   QObject::tr("roll"),                    p.rpyDeg[0])   ||
        !readNumber (ColPitch,  QObject::tr("pitch"),                   p.rpyDeg[1])   ||
        !readNumber (ColYaw,    QObject::tr("yaw"),                     p.rpyDeg[2])   ||
        !readNumber (ColPosTol, QObject::tr("position tolerance"),      p.tolerance.positionMeters) ||
        !readNumber (ColOriTol, QObject::tr("orientation tolerance"),   p.tolerance.orientationDeg)  ||
        !readNumber (ColWeight, QObject::tr("weight"),                  p.weight) ||
        !readBool   (ColFreeRoll, QObject::tr("freeRoll"), p.tolerance.allowToolRollFree))
        return TaskPoint {};
    p.note = cellText (ColNote).toStdString ();
    QTableWidgetItem* enabledItem = t->item (row, ColEnabled);
    p.enabled = enabledItem != nullptr && enabledItem->checkState () == Qt::Checked;
    return p;
}

// P0-6 辅助:把表格一行的 reason 列与 status 列标红,并把所有错误拼成
// tooltip;reason 显示第一条错误 code/message。失败行用浅红背景。
void markTaskPointRowError (QTableWidget* t, int row,
                            const std::vector< AnalysisWarning >& warnings)
{
    if (t == nullptr || row < 0 || row >= t->rowCount ())
        return;
    QString tooltipText;
    QString firstCode;
    QString firstMessage;
    for (std::size_t i = 0; i < warnings.size (); ++i) {
        const AnalysisWarning& w = warnings[i];
        if (i == 0) {
            firstCode    = QString::fromStdString (w.code);
            firstMessage = QString::fromStdString (w.message);
        }
        if (!tooltipText.isEmpty ())
            tooltipText += QStringLiteral ("\n");
        tooltipText += QStringLiteral ("[%1] %2: %3")
            .arg (QString::fromLatin1 (statusText (static_cast<AnalysisStatus> (w.severity))))
            .arg (QString::fromStdString (w.code))
            .arg (QString::fromStdString (w.message));
    }
    const QString reasonText = firstCode.isEmpty () ?
        QStringLiteral ("-") :
        QStringLiteral ("%1: %2").arg (firstCode).arg (firstMessage);
    // 给整行所有 cell 设浅红背景与 tooltip,reason 列额外显示第一条错误。
    for (int c = 0; c < t->columnCount (); ++c) {
        QTableWidgetItem* item = t->item (row, c);
        if (item == nullptr)
            continue;
        item->setBackground (QColor (255, 224, 224));
        if (!tooltipText.isEmpty ())
            item->setToolTip (tooltipText);
    }
    setCell (t, row, ColStatus,
             QStringLiteral ("Invalid"), false);
    setCell (t, row, ColReason, reasonText, false);
}

// 清除整张表格的红色背景与 tooltip(下一次分析前 / 导入后调用)。
void clearTaskPointValidationMarks (QTableWidget* t)
{
    if (t == nullptr)
        return;
    for (int r = 0; r < t->rowCount (); ++r) {
        for (int c = 0; c < t->columnCount (); ++c) {
            QTableWidgetItem* item = t->item (r, c);
            if (item == nullptr)
                continue;
            item->setData (Qt::BackgroundRole, QVariant ());
            item->setToolTip (QString ());
        }
    }
}

AnalysisWarning validationWarningFromText (const QString& message)
{
    AnalysisWarning warning;
    warning.code     = "KIN_TASK_VALIDATION";
    warning.message  = message.toStdString ();
    warning.source   = "KinematicAnalysis";
    warning.severity = AnalysisStatus::Fail;
    return warning;
}

void appendTaskPointValidationSummary (QString& summary, int row, const TaskPoint& point,
                                       const AnalysisWarning& warning)
{
    if (!summary.isEmpty ())
        summary += QStringLiteral ("\n");
    summary += QObject::tr("Row %1 (%2): %3")
                   .arg (row + 1)
                   .arg (QString::fromStdString (point.id))
                   .arg (QString::fromStdString (warning.message));
}

bool validateTaskPointRows (QTableWidget* table, std::vector< TaskPoint >* points,
                            QString* summary)
{
    if (points != nullptr)
        points->clear ();
    if (summary != nullptr)
        summary->clear ();
    if (table == nullptr)
        return true;

    clearTaskPointValidationMarks (table);
    bool valid = true;
    for (int row = 0; row < table->rowCount (); ++row) {
        QString rowError;
        TaskPoint point = readTaskPointFromRow (table, row, &rowError);
        std::vector< AnalysisWarning > warnings;
        if (!rowError.isEmpty ())
            warnings.push_back (validationWarningFromText (rowError));
        else
            warnings = RobotAnalysisValidation::validateTaskPoint (point);

        if (!warnings.empty ()) {
            valid = false;
            markTaskPointRowError (table, row, warnings);
            if (summary != nullptr)
                appendTaskPointValidationSummary (*summary, row, point, warnings.front ());
            continue;
        }
        if (points != nullptr)
            points->push_back (point);
    }
    return valid;
}
}    // namespace

// collectTaskPointsFromTable:从 Task point 表格逐行读出 TaskPoint 结构。
//   - 第 0 列是 checkbox 决定 enabled;
//   - 第 3 列的 Type 字符串映射回 TaskPointType 枚举;
//   - 数值列通过 toDouble 解析;
//   - refFrame / tcpFrame / freeRoll / note 等字符串字段原样回传,
//     保证 CSV → UI → CSV 不丢字段。
std::vector< TaskPoint > KinematicAnalysisWidget::collectTaskPointsFromTable (QString* error) const
{
    if (error != nullptr)
        error->clear ();
    if (_taskPointModel == nullptr)
        return std::vector< TaskPoint > ();
    return _taskPointModel->taskPoints (error);
}

// applyTaskPointResults:P3-A 迁移到 model API。
// 把 results 一次性写回 model,model 的 dataChanged 信号让 view 自动刷新;
// 不再逐 cell setCell,也不必手动维护 status / reason / result 列的同步。
// 染色与 tooltip 也由 model 的 BackgroundRole / ToolTipRole 负责。
void KinematicAnalysisWidget::applyTaskPointResults (
    const std::vector< TaskPointReachabilityResult >& results, double reachableRate)
{
    if (_taskPointModel == nullptr)
        return;

    // 1) 写回 model;model 内部对每行 hasResult / result 字段赋值。
    _taskPointModel->setResults (results, reachableRate);

    setTaskPointTableColumnWidths ();
    updateTaskPointSelectionButtons ();
    refreshVisualization ();
}


// importTaskPointsCsv:从用户选择的 CSV 文件读入任务点。
//   - 用 RobotAnalysisCsv::taskPointsFromCsv 解析(共享 CSV 序列化);
//   - 先清空表格,再逐点重建(覆盖式导入);
//   - result/reason 列固定填 "-",留给后续 Analyze all 写回。
// importTaskPointsCsv:P3-A 迁移到 model API。
// 直接 model->setRowsFromTaskPoints(points) 覆盖式导入,
// 验证由 model 内部跑 RobotAnalysisValidation,失败行标浅红。
void KinematicAnalysisWidget::importTaskPointsCsv ()
{
    if (_taskPointModel == nullptr) {
        setStatus(tr("Cannot import task points: model is not available."));
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
    _taskPointModel->setRowsFromTaskPoints (points);
    QString validationSummary;
    _taskPointModel->validateAll (&validationSummary);
    setTaskPointTableColumnWidths ();
    setStatus (tr ("Imported %1 task point(s).").arg (static_cast<int> (points.size ())));
}

// importFrozenRequirements: 从 EngineeringRequirements 保存的项目 JSON 中读取 frozenArtifact。
// 该入口不支持把编辑态 RequirementSet 直接转换为任务点：只有通过冻结并与当前 WorkCell
// 场景复核的工件，才能进入运动学分析，防止夹具或 TCP 已变化时继续使用过期工艺位姿。
void KinematicAnalysisWidget::importFrozenRequirements ()
{
    const QString readinessError = robotProjectWorkCellReadinessError (_studio);
    if (!readinessError.isEmpty ()) {
        setStatus (readinessError);
        return;
    }
    if (_taskPointModel == nullptr || _workcell == nullptr) {
        setStatus(tr("Cannot import frozen requirements: a task table and WorkCell are required."));
        return;
    }
    QString path;
    QString resolveError;
    const bool managedRequirement =
        _studio != nullptr &&
        _studio->resolveProjectResource(
            QStringLiteral("engineering-requirements.main"), path, &resolveError);
    if (managedRequirement && !_studio->confirmSaveBeforeProjectResourceRead(this)) {
        setStatus(tr("Frozen requirement import canceled: project changes were not published."));
        return;
    }
    if (path.isEmpty()) {
        const QString initialDirectory = _studio != nullptr && !_studio->projectDirectory().isEmpty()
            ? QDir(_studio->projectDirectory()).filePath(QStringLiteral("requirements"))
            : QString();
        path = QFileDialog::getOpenFileName(
            this, tr("Import frozen engineering requirements"), initialDirectory,
            tr("Engineering requirements (*.requirements.json *.json);;All files (*)"));
    }
    if (path.isEmpty()) {
        setStatus(tr("Frozen requirement import canceled."));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Import error"), tr("Could not open %1").arg(path));
        setStatus(tr("Frozen requirement import failed: could not open file."));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, tr("Import error"),
                             tr("Requirement JSON parse failed: %1").arg(parseError.errorString()));
        setStatus(tr("Frozen requirement import failed: JSON parse error."));
        return;
    }
    // 解析逻辑集中在适配器中：它同时支持“需求项目内嵌工件”和“独立冻结工件”，并能
    // 明确区分未冻结项目、损坏字段和选错 JSON，避免 UI 只显示难以定位的 schema 错误。
    const QJsonObject root = document.object();
    FrozenRequirementArtifact artifact;
    std::string error;
    if (!FrozenRequirementKinematicAdapter::parseArtifactJson(root, artifact, &error)) {
        QMessageBox::warning(this, tr("Import error"),
                             tr("Frozen artifact parse failed: %1").arg(QString::fromStdString(error)));
        setStatus(tr("Frozen requirement import failed: artifact parse error."));
        return;
    }
    std::vector<TaskPoint> points;
    bool robotStateChanged = false;
    std::vector<std::string> validationWarnings;
    const QString artifactBaseDirectory =
        managedRequirement && _studio != nullptr && !_studio->projectDirectory().isEmpty()
            ? _studio->projectDirectory()
            : QFileInfo(path).absolutePath();
    if (!FrozenRequirementKinematicAdapter::applyWithValidation(artifact, *_workcell, currentState(), points,
                                                                &error, &robotStateChanged,
                                                                &validationWarnings,
                                                                artifactBaseDirectory.toStdString())) {
        QMessageBox::warning(this, tr("Import validation"),
                             tr("Frozen artifact is not valid for the current WorkCell: %1")
                                 .arg(QString::fromStdString(error)));
        setStatus(tr("Frozen requirement import blocked: scenario validation failed."));
        return;
    }
    _taskPointModel->setRowsFromTaskPoints(points);
    QString validationSummary;
    _taskPointModel->validateAll(&validationSummary);
    setTaskPointTableColumnWidths();
    QString status = tr("Imported %1 frozen engineering task point(s).").arg(static_cast<int>(points.size()));
    if (robotStateChanged) {
        status += tr(" Robot joint state differs from the frozen state, but fixtures and the external environment are unchanged. "
                     "Frozen requirements remain valid; the current joint state is used as the IK initial seed.");
    }
    for (const std::string& warning : validationWarnings)
        status += tr(" Warning: %1").arg(QString::fromStdString(warning));
    setStatus(status);
}

// exportTaskPointsCsv:把表格当前内容序列化为 CSV 并写入用户指定文件。
// 写文件用 QFile::write(const char*, qint64) 写出 std::string 原始字节。
void KinematicAnalysisWidget::exportTaskPointsCsv ()
{
    if (_taskPointModel == nullptr) {
        setStatus(tr("Cannot export task points: model is not available."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export task points"), QString ("task_points.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus(tr("Task point export canceled."));
        return;
    }
    // 先跑 model 验证,空 frame / 负 tolerance 等都被拦截。
    QString validationSummary;
    if (!_taskPointModel->validateAll (&validationSummary)) {
        QMessageBox::warning (this, tr("Export validation"),
                              tr("Task points have validation errors:\n\n%1")
                                  .arg (validationSummary));
        setStatus (tr("Task point export blocked: validation errors."));
        setTaskPointTableColumnWidths ();
        return;
    }
    // 验证通过后,从 model 取所有行(完整字段)写到 CSV。
    const std::vector< TaskPoint > points = _taskPointModel->taskPoints (nullptr);
    const std::string csv = RobotAnalysisCsv::taskPointsToCsv (points);
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

// P1 exportTaskPointResultsCsv:导出批量 IK 结果 CSV。
// 包含任务点定义(id/name/enabled/refFrame/tcpFrame) + 状态(status/reason) +
// 8 个结果指标(rawCandidates/usableSolutions/bestQ/posErr/oriErr/margin/condition/collision)。
// 不要求能再次导入 RobotAnalysisCore;它面向报告而不是回写。
void KinematicAnalysisWidget::exportTaskPointResultsCsv ()
{
    if (_lastTaskPointResults.empty ()) {
        setStatus (tr("Cannot export result CSV: run Analyze all first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export task point results"), QString ("task_point_results.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus (tr("Task point result export canceled."));
        return;
    }
    QFile file (path);
    if (!file.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        setStatus (tr("Task point result export failed: could not open file for writing."));
        return;
    }
    QTextStream out (&file);
    out << "id,name,type,enabled,refFrame,tcpFrame,status,reason,"
           "rawCandidates,usableSolutions,bestQ,"
           "positionErrorMeters,orientationErrorDeg,"
           "minJointLimitMargin,conditionNumber,inCollision\n";
    for (const TaskPointReachabilityResult& r : _lastTaskPointResults) {
        QStringList reasons;
        for (KinematicFailureReason fr : r.failureReasons)
            reasons << QString::fromUtf8 (rws::toString (fr));
        const KinematicIkSolution* best = bestUsableSolution (r.ik);
        QString bestQ = "-";
        QString posErr = "-";
        QString oriErr = "-";
        QString margin = "-";
        QString cond = "-";
        QString collision = "-";
        if (best != nullptr) {
            bestQ    = qVectorText (best->q);
            posErr   = QString::number (best->positionErrorMeters, 'g', 8);
            oriErr   = QString::number (best->orientationErrorDeg, 'g', 8);
            margin   = QString::number (best->minJointLimitMargin, 'g', 8);
            cond     = std::isinf (best->conditionNumber) ?
                           QStringLiteral ("inf") :
                           QString::number (best->conditionNumber, 'g', 8);
            collision = best->inCollision ? QStringLiteral ("Yes") : QStringLiteral ("No");
        }
        const QStringList fields = {
            QString::fromStdString (r.taskPoint.id),
            QString::fromStdString (r.taskPoint.name),
            QString::fromLatin1 (taskPointTypeText (r.taskPoint.type)),
            r.taskPoint.enabled ? QStringLiteral ("true") : QStringLiteral ("false"),
            QString::fromStdString (r.taskPoint.refFrame),
            QString::fromStdString (r.taskPoint.tcpFrame),
            QString::fromLatin1 (statusText (r.status)),
            reasons.isEmpty () ? QStringLiteral ("-") : reasons.join (QStringLiteral (",")),
            QString::number (static_cast< int > (r.ik.rawCandidateCount)),
            QString::number (static_cast< int > (r.ik.usableSolutionCount)),
            bestQ, posErr, oriErr, margin, cond, collision
        };
        out << csvJoin (fields) << "\n";
    }
    file.close ();
    setStatus (tr("Exported %1 task point result row(s).")
                   .arg (static_cast< int > (_lastTaskPointResults.size ())));
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
    // P3-A 迁移:从 model 取所有行(完整字段)。
    QString validationSummary;
    if (!_taskPointModel->validateAll (&validationSummary)) {
        QMessageBox::warning (this, tr("Analyze validation"),
                              tr("Task points have validation errors:\n\n%1")
                                  .arg (validationSummary));
        setStatus (tr("Task point analysis blocked: validation errors."));
        setTaskPointTableColumnWidths ();
        return;
    }
    const std::vector< TaskPoint > points = _taskPointModel->taskPoints (nullptr);
    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (true, &collisionUnavailable);
    // P1:workcell-aware overload。tcpFrame 是顶部默认 TCP,
    // 每行 taskPoint.tcpFrame 由 TaskPointResolver 优先使用。
    const std::vector< TaskPointReachabilityResult > results =
        analyzer.analyzeTaskPoints (
            _workcell, device, tcpFrame, state, points, collisionDetector);
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

// ============================================================================
//  P2:Task points 专用操作
// ============================================================================

// hasSelectedEnabledTaskPoint:P3-A 迁移:从 QTableView + model 拿选中行。
// 0 行 / 选 disabled 行 / 选 Skipped 都视为不可用。
static bool hasSelectedEnabledTaskPoint (
    QTableView* view, rws::TaskPointTableModel* model,
    int& rowOut, TaskPoint& taskPointOut, QString& errorOut)
{
    if (view == nullptr || model == nullptr)
        return false;
    const QModelIndexList selected = view->selectionModel ()->selectedRows ();
    if (selected.isEmpty ())
        return false;
    const int row = selected.front ().row ();
    if (row < 0 || row >= model->rowCount ())
        return false;
    const TaskPoint p = model->taskPointAt (row);
    if (p.id.empty () && p.name.empty () && p.position[0] == 0.0 &&
        p.position[1] == 0.0 && p.position[2] == 0.0) {
        // taskPointAt 越界或空行,等价于 disabled。
        errorOut = QObject::tr ("Selected task point is invalid.");
        return false;
    }
    if (!p.enabled) {
        errorOut = QObject::tr ("Selected task point is disabled; enable it first.");
        return false;
    }
    rowOut        = row;
    taskPointOut  = p;
    return true;
}

// updateTaskPointSelectionButtons:选中行变化 / 表格行数变化时启用 / 禁用
// 3 个 selected-only 按钮。按钮在 selected 有效时启用,否则禁用。
void KinematicAnalysisWidget::updateTaskPointSelectionButtons ()
{
    if (_taskPointTable == nullptr || _taskPointModel == nullptr)
        return;
    int row = -1;
    TaskPoint taskPoint;
    QString error;
    const bool enabled = hasSelectedEnabledTaskPoint (
        _taskPointTable, _taskPointModel, row, taskPoint, error);
    if (_analyzeSelectedTaskPointsButton != nullptr)
        _analyzeSelectedTaskPointsButton->setEnabled (enabled);
    if (_applySelectedTaskPointBestQButton != nullptr) {
        // Apply best Q 还要求 _taskPointModel->bestUsableSolutionForRow 有解。
        const bool canApply = enabled && row >= 0 &&
            _taskPointModel->bestUsableSolutionForRow (row) != nullptr;
        _applySelectedTaskPointBestQButton->setEnabled (canApply);
    }
    if (_openSelectedTaskPointInIkButton != nullptr)
        _openSelectedTaskPointInIkButton->setEnabled (enabled);
}

// analyzeSelectedTaskPoints:只分析选中且 enabled 的行。
// disabled 行结果清空(Skipped),不影响其他行;_lastTaskPointResults
// 按表格行号对齐,selected 之外保持上一轮结果或空。
// updateTaskPointDetails:展示选中任务点的详情。把选中行的位姿值同步到右侧
// 6 个 SpinBox(供快速编辑),在 detail 表显示状态 / 原因 / 可用解数 / best Q,
// 在 more 表显示其余完整字段,并刷新顶部任务点汇总计数(Enabled/Pass/Warning/Fail)。
// updateTaskPointDetails: show secondary fields for the selected compact-table row.
void KinematicAnalysisWidget::updateTaskPointDetails ()
{
    QTableWidget* detailTable = _taskPointTab != nullptr ?
        _taskPointTab->findChild< QTableWidget* > (QStringLiteral ("taskPointDetailTable")) :
        nullptr;
    QTableWidget* moreTable = _taskPointTab != nullptr ?
        _taskPointTab->findChild< QTableWidget* > (QStringLiteral ("taskPointMoreTable")) :
        nullptr;
    const std::array< QDoubleSpinBox*, 6 > poseSpins = {{
        _taskPointTab != nullptr ? _taskPointTab->findChild< QDoubleSpinBox* > (
            QStringLiteral ("taskPointXSpin")) : nullptr,
        _taskPointTab != nullptr ? _taskPointTab->findChild< QDoubleSpinBox* > (
            QStringLiteral ("taskPointYSpin")) : nullptr,
        _taskPointTab != nullptr ? _taskPointTab->findChild< QDoubleSpinBox* > (
            QStringLiteral ("taskPointZSpin")) : nullptr,
        _taskPointTab != nullptr ? _taskPointTab->findChild< QDoubleSpinBox* > (
            QStringLiteral ("taskPointRollSpin")) : nullptr,
        _taskPointTab != nullptr ? _taskPointTab->findChild< QDoubleSpinBox* > (
            QStringLiteral ("taskPointPitchSpin")) : nullptr,
        _taskPointTab != nullptr ? _taskPointTab->findChild< QDoubleSpinBox* > (
            QStringLiteral ("taskPointYawSpin")) : nullptr}};
    if (detailTable == nullptr || moreTable == nullptr)
        return;

    if (_taskPointSummaryLabel != nullptr && _taskPointModel != nullptr) {
        int enabledCount = 0;
        int passCount = 0;
        int warningCount = 0;
        int failCount = 0;
        for (int row = 0; row < _taskPointModel->rowCount (); ++row) {
            if (_taskPointModel->data (
                    _taskPointModel->index (row, ColEnabled), Qt::CheckStateRole).toInt () !=
                Qt::Checked)
                continue;
            ++enabledCount;
            const QString status = _taskPointModel->data (
                _taskPointModel->index (row, ColStatus), Qt::DisplayRole).toString ();
            if (status == QStringLiteral ("Pass"))
                ++passCount;
            else if (status == QStringLiteral ("Warning"))
                ++warningCount;
            else if (status == QStringLiteral ("Fail"))
                ++failCount;
        }
        _taskPointSummaryLabel->setText (
            tr ("<b>Enabled</b> %1 | <span style=\"color:#18794e\"><b>Pass</b> %2</span> | "
                "<span style=\"color:#a15c00\"><b>Warning</b> %3</span> | "
                "<span style=\"color:#b00020\"><b>Fail</b> %4</span>")
                .arg (enabledCount).arg (passCount).arg (warningCount).arg (failCount));
    }

    auto setPoseEnabled = [&poseSpins] (bool enabled) {
        for (QDoubleSpinBox* spin : poseSpins) {
            if (spin != nullptr)
                spin->setEnabled (enabled);
        }
    };
    auto showEmpty = [&] (const QString& message) {
        if (_taskPointSelectedPanel != nullptr)
            _taskPointSelectedPanel->setVisible (false);
        detailTable->setRowCount (1);
        setDetailRow (detailTable, 0, tr ("Selection"), message);
        moreTable->setRowCount (0);
        setPoseEnabled (false);
    };

    if (_taskPointTable == nullptr || _taskPointModel == nullptr ||
        _taskPointTable->selectionModel () == nullptr) {
        showEmpty (tr ("No task point selected."));
        return;
    }

    const QModelIndexList selected = _taskPointTable->selectionModel ()->selectedRows ();
    if (selected.isEmpty ()) {
        showEmpty (tr ("No task point selected."));
        return;
    }

    const int row = selected.front ().row ();
    if (row < 0 || row >= _taskPointModel->rowCount ()) {
        showEmpty (tr ("Selected task point is unavailable."));
        return;
    }

    if (_taskPointSelectedPanel != nullptr)
        _taskPointSelectedPanel->setVisible (true);

    const std::array< int, 6 > poseColumns = {{
        ColX, ColY, ColZ, ColRoll, ColPitch, ColYaw}};
    for (std::size_t i = 0; i < poseSpins.size (); ++i) {
        QDoubleSpinBox* spin = poseSpins[i];
        if (spin == nullptr)
            continue;
        bool ok = false;
        const double value = _taskPointModel->data (
            _taskPointModel->index (row, poseColumns[i]), Qt::DisplayRole).toDouble (&ok);
        const QSignalBlocker blocker (spin);
        spin->setEnabled (true);
        spin->setValue (ok ? value : 0.0);
    }

    const std::array< int, 4 > resultColumns = {{
        ColStatus, ColReason, ColUsableSolutions, ColBestQ}};
    detailTable->setRowCount (static_cast< int > (resultColumns.size ()));
    for (std::size_t i = 0; i < resultColumns.size (); ++i) {
        const int column = resultColumns[i];
        QString value;
        if (column == ColBestQ) {
            const KinematicIkSolution* best = _taskPointModel->bestUsableSolutionForRow (row);
            value = best != nullptr ? qVectorText (best->q) : QStringLiteral ("-");
        }
        else {
            value = _taskPointModel->data (
                _taskPointModel->index (row, column), Qt::DisplayRole).toString ();
        }
        setDetailRow (detailTable, static_cast< int > (i),
                      rws::TaskPointTableModel::headerText (column),
                      value.trimmed ().isEmpty () ? QStringLiteral ("-") : value);
    }
    detailTable->resizeRowsToContents ();

    const std::vector< int > detailColumns = taskPointDetailColumns ();
    moreTable->setRowCount (static_cast< int > (detailColumns.size ()));
    for (std::size_t i = 0; i < detailColumns.size (); ++i) {
        const int column = detailColumns[i];
        QString value = _taskPointModel->data (
            _taskPointModel->index (row, column), Qt::DisplayRole).toString ();
        if (value.trimmed ().isEmpty ())
            value = QStringLiteral ("-");
        setDetailRow (
            moreTable,
            static_cast< int > (i),
            rws::TaskPointTableModel::headerText (column),
            value);
    }
    moreTable->resizeRowsToContents ();
}

// analyzeSelectedTaskPoints:只分析选中且 enabled 的行(批量任务点分析的子集)。
// 整表先跑一次字段级验证,通过后从 model 取全部行并收集选中行号;调用
// analyzeSelectedTaskPointRows 仅重算选中行,未选中的行保留上一轮结果或空,
// 最后重算可达率并刷新 summary / 报告。
void KinematicAnalysisWidget::analyzeSelectedTaskPoints ()
{
    if (_workcell == nullptr) {
        setStatus (tr ("Cannot analyze task points: no WorkCell loaded."));
        return;
    }
    if (_deviceCombo == nullptr || _deviceCombo->count () == 0) {
        setStatus (tr ("Cannot analyze task points: no device available."));
        return;
    }
    if (_taskPointModel == nullptr || _taskPointTable == nullptr)
        return;
    // P3-A 迁移:从 view 的 selectionModel 拿选中行,不再依赖 QTableWidget 内部。
    const QModelIndexList selected = _taskPointTable->selectionModel ()->selectedRows ();
    if (selected.isEmpty ()) {
        setStatus (tr ("Cannot analyze task points: no row selected."));
        return;
    }
    // 整表先跑一次 validation,空 frame / 负 tolerance 等都拦截。
    QString validationSummary;
    if (!_taskPointModel->validateAll (&validationSummary)) {
        QMessageBox::warning (this, tr ("Analyze validation"),
                              tr ("Task points have validation errors:\n\n%1")
                                  .arg (validationSummary));
        setStatus (tr ("Task point analysis blocked: validation errors."));
        setTaskPointTableColumnWidths ();
        return;
    }
    const std::vector< TaskPoint > allPoints = _taskPointModel->taskPoints (nullptr);
    const int total = _taskPointModel->rowCount ();

    const std::string deviceName = _deviceCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::models::Device > device = deviceByName (_workcell, deviceName);
    if (device == nullptr) {
        setStatus (tr ("Cannot analyze task points: no valid device selected."));
        return;
    }
    const std::string tcpName = _tcpFrameCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = frameByName (_workcell, tcpName);
    const rw::kinematics::State state = currentState ();

    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (true, &collisionUnavailable);

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);

    int analyzed = 0;
    QSet< QString > selectedIds;
    for (const QModelIndex& idx : selected) {
        const QString id = _taskPointModel->data (
            _taskPointModel->index (idx.row (), ColId), Qt::UserRole).toString ();
        if (!id.isEmpty ())
            selectedIds.insert (id);
    }

    std::vector< int > rows;
    rows.reserve (static_cast< std::size_t > (selectedIds.size ()));
    for (int row = 0; row < total; ++row) {
        const TaskPoint& point = allPoints[static_cast< std::size_t > (row)];
        if (!selectedIds.contains (QString::fromStdString (point.id)))
            continue;
        rows.push_back (row);
        if (point.enabled)
            ++analyzed;
    }

    // 重新计算可达率 + 应用结果 + 更新 summary。
    _lastTaskPointResults = analyzeSelectedTaskPointRows (
        analyzer, _workcell, device, tcpFrame, state, allPoints, rows,
        _lastTaskPointResults, collisionDetector);
    const double rate = analyzer.calculateReachableRate (_lastTaskPointResults);
    std::vector< TaskPointReachabilityResult > selectedResults;
    selectedResults.reserve (rows.size ());
    for (const TaskPointReachabilityResult& result : _lastTaskPointResults) {
        if (selectedIds.contains (QString::fromStdString (result.taskPoint.id)))
            selectedResults.push_back (result);
    }
    _taskPointModel->applyResultsByTaskId (selectedResults);
    Q_UNUSED (rate);
    setTaskPointTableColumnWidths ();
    refreshVisualization ();

    const QString collisionNote = collisionUnavailable ?
        tr (" Collision checking was unavailable.") : QString ();
    setStatus (tr ("Analyzed %1 selected task point(s).%2")
                  .arg (analyzed).arg (collisionNote));
    updateReportSummary ();
    updateTaskPointSelectionButtons ();
}

// importCurrentTcpAsTaskPoint:把当前 RWS TCP 位姿插入新行 TaskPoint。
// refFrame 默认用 WORLD,tcpFrame 跟随顶部 TCP,其他字段用阈值默认值。
void KinematicAnalysisWidget::importCurrentTcpAsTaskPoint ()
{
    if (_workcell == nullptr) {
        setStatus (tr ("Cannot import current TCP: no WorkCell loaded."));
        return;
    }
    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == nullptr) {
        setStatus (tr ("Cannot import current TCP: no valid device selected."));
        return;
    }
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();
    if (tcpFrame == nullptr) {
        setStatus (tr ("Cannot import current TCP: no valid TCP frame selected."));
        return;
    }
    if (_taskPointModel == nullptr)
        return;
    // 复用 IK 页 importCurrentPoseToIk 的位姿读取逻辑 + P2 TaskPointUiLogic。
    try {
        const rw::math::Transform3D<> baseTtcp =
            rw::kinematics::Kinematics::frameTframe (
                device->getBase (), tcpFrame.get (), currentState ());
        const std::string id =
            QString ("TP_%1").arg (_taskPointModel->rowCount () + 1, 3, 10, QChar ('0')).toStdString ();
        TaskPoint p = taskPointFromCurrentTcpPose (
            id, tcpFrame->getName (), device->getBase ()->getName (), baseTtcp, _thresholds);
        // P3-A 迁移:用 model->appendTaskPoint 一次性插入 + 触发验证。
        const int row = _taskPointModel->appendTaskPoint (p);
        QString validationSummary;
        _taskPointModel->validateAll (&validationSummary);
        setTaskPointTableColumnWidths ();
        setStatus (tr ("Imported current TCP as task point row %1.").arg (row + 1));
    }
    catch (const std::exception& ex) {
        setStatus (tr ("Cannot import current TCP: %1").arg (QString::fromUtf8 (ex.what ())));
    }
    updateTaskPointSelectionButtons ();
}

// applySelectedTaskPointBestQ:P3-A 迁移到 model。
// 从 _taskPointModel->bestUsableSolutionForRow 拿 best Q,直接用
// isUsableIkSolution 二次校验,避免 _lastTaskPointResults 索引错位。
void KinematicAnalysisWidget::applySelectedTaskPointBestQ ()
{
    if (_workcell == nullptr || _studio == nullptr) {
        setStatus (tr ("Cannot apply task point best Q: no WorkCell or RWS context."));
        return;
    }
    int row = -1;
    TaskPoint taskPoint;
    QString error;
    if (!hasSelectedEnabledTaskPoint (
            _taskPointTable, _taskPointModel, row, taskPoint, error)) {
        setStatus (tr ("Cannot apply task point best Q: %1").arg (error));
        return;
    }
    const KinematicIkSolution* best =
        _taskPointModel != nullptr ?
            _taskPointModel->bestUsableSolutionForRow (row) : nullptr;
    if (best == nullptr) {
        setStatus (tr ("Cannot apply task point best Q: no usable IK solution for selected row."));
        return;
    }
    if (!isUsableIkSolution (*best)) {
        setStatus (tr ("Cannot apply task point best Q: best solution is failed or in collision."));
        return;
    }
    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == nullptr) {
        setStatus (tr ("Cannot apply task point best Q: device not selected."));
        return;
    }
    if (best->q.size () != device->getDOF ()) {
        setStatus (tr ("Cannot apply task point best Q: Q dimension does not match device."));
        return;
    }
    rw::kinematics::State state = currentState ();
    device->setQ (best->q, state);
    _studio->setState (state);
    refreshCurrentPose ();
    setStatus (tr ("Applied best Q (%1 joints) to RWS state for selected task point.")
                  .arg (static_cast<int> (best->q.size ())));
}

// openSelectedTaskPointInIk:P3-A 迁移到 model。
// 选中行直接从 _taskPointModel->taskPointAt 拿 TaskPoint,
// 通过 TaskPointResolver 解析为 device-base 目标,填到 IK 页。
void KinematicAnalysisWidget::openSelectedTaskPointInIk ()
{
    if (_workcell == nullptr) {
        setStatus (tr ("Cannot open in IK: no WorkCell loaded."));
        return;
    }
    int row = -1;
    TaskPoint taskPoint;
    QString error;
    if (!hasSelectedEnabledTaskPoint (
            _taskPointTable, _taskPointModel, row, taskPoint, error)) {
        setStatus (tr ("Cannot open in IK: %1").arg (error));
        return;
    }
    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == nullptr) {
        setStatus (tr ("Cannot open in IK: no device selected."));
        return;
    }
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();
    const ResolvedTaskPoint resolved = resolveTaskPoint (
        _workcell, device, tcpFrame, currentState (), taskPoint);
    if (!resolved.valid) {
        const QString msg = resolved.warnings.empty () ?
            tr ("Task point cannot be resolved for IK tab.") :
            QString::fromStdString (resolved.warnings.front ().message);
        setStatus (tr ("Cannot open in IK: %1").arg (msg));
        return;
    }
    setIkPoseMetersDeg (
        resolved.targetInDeviceBase.position,
        resolved.targetInDeviceBase.rpyDeg);
    invalidateIkResultPresentation ();
    if (_ikSourceLabel != nullptr) {
        const QString sourceName = QString::fromStdString (
            taskPoint.name.empty () ? taskPoint.id : taskPoint.name);
        _ikSourceLabel->setText (
            tr("Source: %1").arg (sourceName));
        _ikSourceLabel->setVisible (true);
    }
    if (_tcpFrameCombo != nullptr &&
        !resolved.targetInDeviceBase.tcpFrame.empty ()) {
        const int idx = _tcpFrameCombo->findText (
            QString::fromStdString (resolved.targetInDeviceBase.tcpFrame));
        if (idx >= 0)
            _tcpFrameCombo->setCurrentIndex (idx);
    }
    if (_modeTabs != nullptr)
        _modeTabs->setCurrentIndex (0);
    if (_diagnoseScroll != nullptr && _ikTab != nullptr)
        _diagnoseScroll->ensureWidgetVisible (_ikTab);
    setStatus (tr ("Opened selected task point in Diagnose IK; previous results are stale."));
}

// collectPoseReachabilityPositions:按 Source 下拉选择收集位置列表:
//   - 0 → Task points:从 _taskPointModel 取所有行,只取 enabled 的位置;
//   - 1 → Manual rows:从 _posePositionTable 逐行读出 xyz。
std::vector< std::array< double, 3 > >
KinematicAnalysisWidget::collectPoseReachabilityPositions (QString* error) const
{
    std::vector< std::array< double, 3 > > positions;
    if (error != nullptr)
        error->clear ();
    if (_poseTaskPointsSourceButton != NULL &&
        _poseTaskPointsSourceButton->isChecked ()) {
        // P3-A 迁移:从 model 取所有行,跳过对废弃的 QTableWidget helper 调用。
        if (_taskPointModel == nullptr)
            return positions;
        const std::vector< TaskPoint > points = _taskPointModel->taskPoints (error);
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
            position[static_cast< std::size_t > (column)] = metersFromDisplayLength (
                cellText (_posePositionTable, r, column).toDouble (&ok), _lengthUnit);
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

    for (std::size_t i = 0; i < samples.size (); ++i) {
        const WorkspaceSample& sample = samples[i];
        if (i >= static_cast< std::size_t > (rows))
            continue;
        const int row = static_cast< int > (i);
        QTableWidgetItem* indexItem = makeItem (QString::number (row));
        indexItem->setData (Qt::UserRole, row);
        _workspaceTable->setItem (row, 0, indexItem);
        QTableWidgetItem* statusItem =
            makeItem (QString::fromLatin1 (statusText (sample.status)));
        if (sample.status == AnalysisStatus::Pass)
            statusItem->setForeground (QColor (0, 120, 0));
        else if (sample.status == AnalysisStatus::Warning)
            statusItem->setForeground (QColor (180, 120, 0));
        else if (sample.status == AnalysisStatus::Fail)
            statusItem->setForeground (QColor (180, 0, 0));
        _workspaceTable->setItem (row, 1, statusItem);
        _workspaceTable->setItem (row, 2, makeItem (sample.inCollision ? tr("Yes") : tr("No")));
        const QString tcpPosition = QStringLiteral ("(%1, %2, %3)")
            .arg (QString::number (displayLengthFromMeters (
                sample.tcpPosition[0], _lengthUnit), 'g', 6))
            .arg (QString::number (displayLengthFromMeters (
                sample.tcpPosition[1], _lengthUnit), 'g', 6))
            .arg (QString::number (displayLengthFromMeters (
                sample.tcpPosition[2], _lengthUnit), 'g', 6));
        _workspaceTable->setItem (row, 3, makeItem (tcpPosition));
        _workspaceTable->setItem (row, 4, makeItem (sample.manipulability));
        _workspaceTable->setItem (row, 5, makeItem (sample.minJointLimitMargin));
    }

    const rws::WorkspaceSummary summary = rws::summarizeWorkspaceSamples (samples);
    if (_workspaceSampleCountLabel != NULL)
        _workspaceSampleCountLabel->setText (tr("<b>Samples</b><br>%1")
            .arg (static_cast< int > (summary.totalCount)));
    if (_workspaceCollisionFreeLabel != NULL)
        _workspaceCollisionFreeLabel->setText (tr("<b>Collision-free</b><br>%1")
            .arg (static_cast< int > (summary.collisionFreeCount)));
    if (_workspacePassLabel != NULL)
        _workspacePassLabel->setText (tr("<b>Pass</b><br><span style=\"color:#18794e\">%1</span>")
            .arg (static_cast< int > (summary.passCount)));
    if (_workspaceWarningLabel != NULL)
        _workspaceWarningLabel->setText (tr("<b>Warning</b><br><span style=\"color:#a15c00\">%1</span>")
            .arg (static_cast< int > (summary.warningCount)));
    if (_workspaceFailLabel != NULL)
        _workspaceFailLabel->setText (tr("<b>Fail</b><br><span style=\"color:#b00020\">%1</span>")
            .arg (static_cast< int > (summary.failCount)));
    if (_workspaceAvgManipulabilityLabel != NULL)
        _workspaceAvgManipulabilityLabel->setText (
            tr("<b>Avg manipulability</b><br>%1").arg (
                summary.hasManipulability ?
                    QString::number (summary.avgManipulability, 'g', 6) :
                    QStringLiteral ("-")));

    // P4:导出 / 可视化按钮在有数据时启用。
    if (_workspaceExportButton != NULL)
        _workspaceExportButton->setEnabled (!samples.empty ());
    if (_workspaceOpenVisualizationButton != NULL)
        _workspaceOpenVisualizationButton->setEnabled (!samples.empty ());
    if (rows > 0)
        _workspaceTable->selectRow (0);
    else
        updateWorkspaceSampleDetails ();
    refreshVisualization ();
}

// updateWorkspaceSampleDetails:工作空间样本表选中行变化时,刷新下方详情表。
// 第 0 列存了原始样本索引(Qt::UserRole),据此反查 _workspaceSamples 展示
// 完整字段;无选中行或索引越界时显示占位提示。
void KinematicAnalysisWidget::updateWorkspaceSampleDetails ()
{
    if (_workspaceDetailTable == NULL)
        return;
    if (_workspaceTable == NULL || _workspaceTable->selectionModel () == NULL ||
        _workspaceTable->selectionModel ()->selectedRows ().isEmpty ()) {
        _workspaceDetailTable->setRowCount (1);
        setDetailRow (_workspaceDetailTable, 0, tr("Selection"), tr("No sample selected."));
        return;
    }

    const int row = _workspaceTable->selectionModel ()->selectedRows ().front ().row ();
    const QTableWidgetItem* indexItem = _workspaceTable->item (row, 0);
    const int sampleIndex = indexItem == NULL ? -1 : indexItem->data (Qt::UserRole).toInt ();
    if (sampleIndex < 0 ||
        sampleIndex >= static_cast< int > (_workspaceSamples.size ())) {
        _workspaceDetailTable->setRowCount (1);
        setDetailRow (_workspaceDetailTable, 0, tr("Selection"), tr("No sample selected."));
        return;
    }

    const WorkspaceSample& sample = _workspaceSamples[static_cast< std::size_t > (sampleIndex)];
    const QString tcpPosition = QStringLiteral ("(%1, %2, %3)")
        .arg (QString::number (displayLengthFromMeters (
            sample.tcpPosition[0], _lengthUnit), 'g', 6))
        .arg (QString::number (displayLengthFromMeters (
            sample.tcpPosition[1], _lengthUnit), 'g', 6))
        .arg (QString::number (displayLengthFromMeters (
            sample.tcpPosition[2], _lengthUnit), 'g', 6));
    _workspaceDetailTable->setRowCount (6);
    setDetailRow (_workspaceDetailTable, 0, tr("TCP position"), tcpPosition);
    setDetailRow (_workspaceDetailTable, 1, tr("Status"),
                  QString::fromLatin1 (statusText (sample.status)));
    setDetailRow (_workspaceDetailTable, 2, tr("Collision"),
                  sample.inCollision ? tr("Yes") : tr("No"));
    setDetailRow (_workspaceDetailTable, 3, tr("Manipulability"),
                  QString::number (sample.manipulability, 'g', 6));
    setDetailRow (_workspaceDetailTable, 4, tr("Condition"),
                  std::isinf (sample.conditionNumber) ?
                      QStringLiteral ("inf") :
                      QString::number (sample.conditionNumber, 'g', 6));
    setDetailRow (_workspaceDetailTable, 5, tr("Min joint margin"),
                  QString::number (sample.minJointLimitMargin, 'g', 6));
}

// sampleWorkspace:从控件读 WorkspaceSamplingConfig → 调 analyzer → 写回表格;
// 固定 randomSeed=1 以保证结果可复现,便于回归对比。
void KinematicAnalysisWidget::sampleWorkspace ()
{
    if (_workspaceRunActive || _workspaceWatcher == nullptr ||
        _workspaceWatcher->isRunning ()) {
        setStatus (tr("Workspace sampling is already running."));
        return;
    }

    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == NULL) {
        setStatus (tr("Cannot sample workspace: no valid device selected."));
        return;
    }
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();
    if (tcpFrame == NULL) {
        setStatus (tr("Cannot sample workspace: no valid TCP frame selected."));
        return;
    }

    WorkspaceSamplingConfig config;
    config.sampleCount = _workspaceSampleCountSpin->value ();
    config.gridStepsPerJoint = _workspaceGridStepsSpin->value ();
    config.mode = _workspaceModeCombo->currentIndex () == 1 ?
        WorkspaceSamplingMode::Grid : WorkspaceSamplingMode::RandomUniform;
    config.checkCollision = _workspaceCollisionCheck->isChecked ();
    // P4:用 UI 上的 Seed,让同一种子重复 Run 结果可复现。
    config.randomSeed = _workspaceSeedSpin != NULL ?
        static_cast< unsigned int > (_workspaceSeedSpin->value ()) : 1u;

    // P9:async — 后台执行,不阻塞 UI
    const rw::kinematics::State runState = currentState ();
    const rw::core::Ptr< rw::models::Device > runDevice = device;
    const rw::core::Ptr< const rw::kinematics::Frame > runTcpFrame = tcpFrame;
    const KinematicThresholds runThresholds = _thresholds;
    const rw::core::Ptr< rw::models::WorkCell > runWorkCell =
        _studio != NULL ? _studio->getWorkCell () : NULL;
    _workspaceCollisionUnavailable =
        config.checkCollision && runWorkCell == NULL;

    const std::size_t plannedSamples =
        rws::plannedWorkspaceSampleCount (
            config, runDevice->getDOF (), NULL);
    updateWorkspaceProgress (0, static_cast< qulonglong > (plannedSamples));

    _workspaceRunActive = true;
    if (_workspaceCancelRequested)
        _workspaceCancelRequested->store (false);
    _workspaceWatcher->setProperty (
        "workcellSessionGeneration",
        QVariant::fromValue< qulonglong > (_workcellSessionGeneration));
    if (_workspaceRunButton != NULL)
        _workspaceRunButton->setEnabled (false);
    if (_workspaceCancelButton != NULL)
        _workspaceCancelButton->setEnabled (true);
    QApplication::setOverrideCursor (Qt::WaitCursor);
    setStatus (tr("Workspace sampling running..."));

    struct WorkspaceRunContext {
        std::shared_ptr< std::atomic_bool > cancelFlag;
        QPointer< KinematicAnalysisWidget > widget;
        quint64 sessionGeneration = 0;
    };
    const std::shared_ptr< WorkspaceRunContext > runContext =
        std::make_shared< WorkspaceRunContext > ();
    runContext->cancelFlag = _workspaceCancelRequested;
    runContext->widget = this;
    runContext->sessionGeneration = _workcellSessionGeneration;

    WorkspaceSamplingRunCallbacks callbacks;
    callbacks.isCancellationRequested = [] (void* userData) -> bool {
        const WorkspaceRunContext* ctx =
            static_cast< const WorkspaceRunContext* > (userData);
        return ctx != NULL && ctx->cancelFlag && ctx->cancelFlag->load ();
    };
    callbacks.onProgress = [] (std::size_t cSamples, std::size_t pSamples,
                               void* userData) {
        WorkspaceRunContext* ctx = static_cast< WorkspaceRunContext* > (userData);
        if (ctx == NULL || ctx->widget.isNull ()) return;
        QMetaObject::invokeMethod (
            ctx->widget.data (),
            [widget = ctx->widget, session = ctx->sessionGeneration,
             cSamples, pSamples] {
                if (widget.isNull () || widget->_workcellSessionGeneration != session)
                    return;
                widget->updateWorkspaceProgress (
                    static_cast< qulonglong > (cSamples),
                    static_cast< qulonglong > (pSamples));
            },
            Qt::QueuedConnection);
    };
    callbacks.userData = runContext.get ();

    QFuture< std::vector< WorkspaceSample > > future = QtConcurrent::run (
        [runDevice, runTcpFrame, runState, config, runThresholds,
         callbacks, runContext, runWorkCell] () {
            KinematicAnalyzer worker;
            worker.setThresholds (runThresholds);
            const rw::core::Ptr< rw::proximity::CollisionDetector > detector =
                config.checkCollision ?
                    makeKinematicAnalysisCollisionDetector (runWorkCell) : NULL;
            WorkspaceSamplingConfig workerConfig = config;
            if (config.checkCollision && detector == NULL)
                workerConfig.checkCollision = false;
            return worker.sampleWorkspace (
                runDevice, runTcpFrame, runState, workerConfig,
                detector, callbacks);
        });
    _workspaceWatcher->setFuture (future);
}

// cancelWorkspaceSampling:Cancel 按钮槽。设置跨线程 atomic 取消标志,
// worker 在采样循环内检查后尽早退出;同时禁用 Cancel 按钮避免重复触发,
// 完成信号仍由 handleWorkspaceFinished 统一收尾。
void KinematicAnalysisWidget::cancelWorkspaceSampling ()
{
    if (!_workspaceRunActive || !_workspaceCancelRequested)
        return;
    _workspaceCancelRequested->store (true);
    if (_workspaceCancelButton != NULL)
        _workspaceCancelButton->setEnabled (false);
    setStatus (tr("Workspace sampling cancel requested..."));
}

static const int MaxWorkspaceProgressBarSteps = 1000000;

// updateWorkspaceProgress:后台 worker 通过 QMetaObject::invokeMethod(QueuedConnection)
// 跨线程回调到 UI 线程,按完成比例刷新进度条与文本。进度条范围被限制在
// MaxWorkspaceProgressBarSteps 内,但文本始终显示精确计数,避免超大采样数溢出。
void KinematicAnalysisWidget::updateWorkspaceProgress (
    qulonglong completedSamples, qulonglong plannedSamples)
{
    const qulonglong boundedCompleted = plannedSamples == 0 ? 0 :
        std::min< qulonglong > (completedSamples, plannedSamples);
    const int barMax = plannedSamples >
            static_cast< qulonglong > (MaxWorkspaceProgressBarSteps) ?
        MaxWorkspaceProgressBarSteps :
        static_cast< int > (plannedSamples);
    const int barValue = plannedSamples == 0 ? 0 :
        static_cast< int > (
            (static_cast< double > (boundedCompleted) /
             static_cast< double > (plannedSamples)) *
            static_cast< double > (barMax));

    if (_workspaceProgressBar != NULL) {
        _workspaceProgressBar->setRange (0, barMax);
        _workspaceProgressBar->setValue (barValue);
    }
    if (_workspaceProgressLabel != NULL) {
        const double pct = plannedSamples == 0 ? 0.0 :
            100.0 * static_cast< double > (boundedCompleted) /
                static_cast< double > (plannedSamples);
        _workspaceProgressLabel->setText (
            tr("Progress: %1 / %2 sample(s) (%3%)")
                .arg (static_cast< qulonglong > (boundedCompleted))
                .arg (static_cast< qulonglong > (plannedSamples))
                .arg (QString::number (pct, 'f', 1)));
    }
}

// handleWorkspaceFinished:工作空间采样 worker 完成信号触发。恢复忙光标与按钮状态,
// 读取结果写入 _workspaceSamples 并刷新表格 / 进度 / 报告,按取消标志区分
// "已完成"与"被取消"的状态文案。
void KinematicAnalysisWidget::handleWorkspaceFinished ()
{
    QApplication::restoreOverrideCursor ();
    const quint64 runGeneration = _workspaceWatcher == nullptr ? 0 :
        _workspaceWatcher->property ("workcellSessionGeneration").toULongLong ();
    if (runGeneration != _workcellSessionGeneration) {
        _workspaceRunActive = false;
        updateWorkspaceControls ();
        refreshWorkflowControls ();
        return;
    }
    _workspaceRunActive = false;
    if (_workspaceRunButton != NULL)
        _workspaceRunButton->setEnabled (true);
    if (_workspaceCancelButton != NULL)
        _workspaceCancelButton->setEnabled (false);

    const std::vector< WorkspaceSample > samples = _workspaceWatcher->result ();
    const bool wasCanceled =
        _workspaceCancelRequested && _workspaceCancelRequested->load ();
    _workspaceSamples = samples;
    applyWorkspaceResults (_workspaceSamples);
    updateWorkspaceProgress (
        static_cast< qulonglong > (_workspaceSamples.size ()),
        static_cast< qulonglong > (_workspaceSamples.size ()));
    updateReportSummary ();

    const QString collisionNote = _workspaceCollisionUnavailable ?
        tr(" Collision checking was unavailable.") : QString ();
    if (wasCanceled) {
        setStatus (tr("Workspace sampling canceled after %1 sample(s).%2")
                       .arg (static_cast< int > (_workspaceSamples.size ()))
                       .arg (collisionNote));
    }
    else {
        setStatus (tr("Workspace sampling completed with %1 sample(s).%2")
                       .arg (static_cast< int > (_workspaceSamples.size ()))
                       .arg (collisionNote));
    }
}

// updateWorkspaceControls:P4 把当前控件值合成 WorkspaceSamplingConfig,
// 通过 plannedWorkspaceSampleCount 算 plan 数,把"实际要跑多少 / 理论多少
// / 是否被截断"写进 diagnostics 标签。mode / sample count / grid steps 变化
// 触发本槽,用户改参数时立即看到结果。
void KinematicAnalysisWidget::updateWorkspaceControls ()
{
    if (_workspaceModeCombo == NULL || _workspaceGridStepsSpin == NULL ||
        _workspaceSampleCountSpin == NULL)
        return;

    const bool gridMode = _workspaceModeCombo->currentIndex () == 1;
    _workspaceSampleCountSpin->setVisible (!gridMode);
    _workspaceGridStepsSpin->setVisible (gridMode);
    if (_workspaceTab != NULL) {
        if (QLabel* samplesLabel = _workspaceTab->findChild< QLabel* > (
                QStringLiteral ("workspaceSamplesLabel")))
            samplesLabel->setVisible (!gridMode);
        if (QLabel* gridStepsLabel = _workspaceTab->findChild< QLabel* > (
                QStringLiteral ("workspaceGridStepsLabel")))
            gridStepsLabel->setVisible (gridMode);
    }

    WorkspaceSamplingConfig config;
    config.sampleCount = _workspaceSampleCountSpin->value ();
    config.gridStepsPerJoint = _workspaceGridStepsSpin->value ();
    config.mode = gridMode ? WorkspaceSamplingMode::Grid
                            : WorkspaceSamplingMode::RandomUniform;
    config.randomSeed = _workspaceSeedSpin != NULL ?
        static_cast< unsigned int > (_workspaceSeedSpin->value ()) : 1u;

    const rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    const std::size_t dof = device == NULL ? 0 : device->getDOF ();
    rws::WorkspaceSamplingDiagnostics diagnostics;
    const std::size_t planned =
        rws::plannedWorkspaceSampleCount (config, dof, &diagnostics);

    if (_workspaceDiagnosticsLabel != NULL) {
        if (!gridMode) {
            _workspaceDiagnosticsLabel->setText (
                tr("Plan: %1 random sample(s), seed %2")
                    .arg (static_cast< int > (planned))
                    .arg (_workspaceSeedSpin != NULL
                              ? _workspaceSeedSpin->value () : 1));
        }
        else {
            _workspaceDiagnosticsLabel->setText (
                tr("Plan: %1 grid sample(s), theoretical %2%3")
                    .arg (static_cast< int > (planned))
                    .arg (static_cast< int > (diagnostics.theoreticalGridSamples))
                    .arg (diagnostics.gridCountTruncated
                              ? tr(" (capped)")
                              : QString ()));
        }
    }

    // P9:运行中禁用 Run 按钮,启用 Cancel。
    if (_workspaceRunButton != NULL)
        _workspaceRunButton->setEnabled (!_workspaceRunActive);
    if (_workspaceCancelButton != NULL)
        _workspaceCancelButton->setEnabled (_workspaceRunActive);
}

// openWorkspaceInVisualization:P4 把 Visualization 切到 Workspace source,
// 复用 Workspace color 模式,跳到 Visualization tab 并 refresh。
void KinematicAnalysisWidget::openWorkspaceInVisualization ()
{
    if (_visualSourceCombo != NULL)
        _visualSourceCombo->setCurrentIndex (1);
    updateVisualizationControls ();
    if (_visualColorModeCombo != NULL && _workspaceColorModeCombo != NULL) {
        const int workspaceMode = _workspaceColorModeCombo->currentIndex ();
        const rws::VisualScalarMode scalar =
            workspaceMode == 1 ? rws::VisualScalarMode::Manipulability :
            workspaceMode == 2 ? rws::VisualScalarMode::MinJointMargin :
            workspaceMode == 3 ? rws::VisualScalarMode::Collision :
                                 rws::VisualScalarMode::Status;
        const int index = _visualColorModeCombo->findData (
            static_cast< int > (scalar));
        if (index >= 0)
            _visualColorModeCombo->setCurrentIndex (index);
    }
    if (_modeTabs != NULL)
        _modeTabs->setCurrentIndex (2);
    if (_exploreScroll != NULL && _visualizationTab != NULL)
        _exploreScroll->ensureWidgetVisible (_visualizationTab);
    refreshVisualization ();
}

// exportWorkspaceCsv:把 _workspaceSamples 全量写出(含 q 字符串、TCP 位置、
// manipulability / 关节裕度 / 条件数 / 碰撞 / 状态)。
void KinematicAnalysisWidget::exportWorkspaceCsv ()
{
    if (_workspaceSamples.empty ()) {
        setStatus (tr("No workspace samples to export."));
        return;
    }

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
    // P4:摘要前言(comment 行),下游脚本可以 # 开头跳过。
    {
        const WorkspaceSummary summary = summarizeWorkspaceSamples (_workspaceSamples);
        out << "# workspace_summary,total," << summary.totalCount
            << ",pass," << summary.passCount
            << ",warning," << summary.warningCount
            << ",fail," << summary.failCount
            << ",collision," << summary.collisionCount
            << ",avg_manipulability," << summary.avgManipulability
            << ",p10_manipulability," << summary.p10Manipulability
            << ",max_condition," << summary.maxCondition
            << "\n";
    }
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

// updatePoseReachabilityControls:P4 把当前控件值通过
// plannedPoseReachabilityTargetCount 算 plan 数,写入 _poseDiagnosticsLabel。
// source / directions / rolls / positions 变化都触发刷新。
void KinematicAnalysisWidget::updatePoseReachabilityControls ()
{
    if (_poseDiagnosticsLabel == NULL || _poseDirectionSamplesSpin == NULL ||
        _poseRollSamplesSpin == NULL)
        return;

    const bool manualMode = _poseManualSourceButton != NULL &&
        _poseManualSourceButton->isChecked ();
    if (_poseManualPositionsPanel != NULL)
        _poseManualPositionsPanel->setVisible (manualMode);
    if (_poseAddRowButton != NULL)
        _poseAddRowButton->setEnabled (manualMode);
    if (_poseRemoveRowButton != NULL)
        _poseRemoveRowButton->setEnabled (manualMode && _posePositionTable != NULL &&
                                          _posePositionTable->rowCount () > 0);

    QString validationError;
    const std::vector< std::array< double, 3 > > positions =
        collectPoseReachabilityPositions (&validationError);

    PoseReachabilityConfig config;
    config.directionSamples = _poseDirectionSamplesSpin->value ();
    config.rollSamples = _poseRollSamplesSpin->value ();
    config.checkCollision =
        _poseCollisionCheck == NULL || _poseCollisionCheck->isChecked ();

    PoseReachabilityDiagnostics diagnostics;
    const std::size_t planned =
        plannedPoseReachabilityTargetCount (
            config, positions.size (), &diagnostics);

    const QString cappedText = diagnostics.targetCountCapped ?
        tr(" (capped)") : QString ();
    const QString validationText = validationError.isEmpty () ?
        QString () : tr(" Input warning: %1").arg (validationError);
    _poseDiagnosticsLabel->setText (
        tr("Plan: %1 IK target(s), %2 orientation(s) per position%3.%4")
            .arg (static_cast< int > (planned))
            .arg (static_cast< int > (diagnostics.plannedDirectionsPerPosition))
            .arg (cappedText)
            .arg (validationText));
}

// P4:位姿可达性表格最多显示 500 行,超出不影响 CSV/Report/Visualization。
static const std::size_t MaxPoseReachabilityTableRows = 500;
static const int MaxPoseReachabilityProgressBarSteps = 1000000;

// applyPoseReachabilityResults:把 PoseReachabilitySample 写到 _poseResultTable,
// 同时刷新顶部 summary(Average coverage)。
void KinematicAnalysisWidget::applyPoseReachabilityResults (
    const std::vector< PoseReachabilitySample >& samples)
{
    if (_poseResultTable == NULL)
        return;
    const int rows = static_cast< int > (
        std::min< std::size_t > (samples.size (), MaxPoseReachabilityTableRows));
    _poseResultTable->setRowCount (rows);

    // P4:用 helper 算 summary,替代手动累加。
    const rws::PoseReachabilitySummary summary =
        rws::summarizePoseReachabilitySamples (samples);

    for (std::size_t i = 0; i < static_cast< std::size_t > (rows); ++i) {
        const rws::PoseReachabilitySample& sample = samples[i];
        const int row = static_cast< int > (i);
        _poseResultTable->setItem (row, 0, makeItem (QString::number (row)));
        QTableWidgetItem* statusItem =
            makeItem (QString::fromLatin1 (statusText (sample.status)));
        if (sample.status == AnalysisStatus::Pass)
            statusItem->setForeground (QColor (0, 120, 0));
        else if (sample.status == AnalysisStatus::Warning)
            statusItem->setForeground (QColor (180, 120, 0));
        else if (sample.status == AnalysisStatus::Fail)
            statusItem->setForeground (QColor (180, 0, 0));
        _poseResultTable->setItem (row, 1, statusItem);
        const QString position = QStringLiteral ("(%1, %2, %3)")
            .arg (QString::number (displayLengthFromMeters (
                sample.position[0], _lengthUnit), 'g', 6))
            .arg (QString::number (displayLengthFromMeters (
                sample.position[1], _lengthUnit), 'g', 6))
            .arg (QString::number (displayLengthFromMeters (
                sample.position[2], _lengthUnit), 'g', 6));
        _poseResultTable->setItem (row, 2, makeItem (position));
        _poseResultTable->setItem (row, 3, makeItem (
            QString::number (100.0 * sample.coverage, 'f', 1) + QStringLiteral ("%")));
    }
    if (_posePositionCountLabel != NULL)
        _posePositionCountLabel->setText (tr("<b>Positions</b><br>%1")
            .arg (static_cast< int > (summary.totalPositions)));
    if (_poseReachableLabel != NULL)
        _poseReachableLabel->setText (tr("<b>Reachable</b><br>%1 / %2")
            .arg (static_cast< int > (summary.reachableDirections))
            .arg (static_cast< int > (summary.sampledDirections)));
    if (_poseCoverageLabel != NULL)
        _poseCoverageLabel->setText (tr("<b>Coverage</b><br>%1%")
            .arg (QString::number (100.0 * summary.averageCoverage, 'f', 1)));
    if (_posePassLabel != NULL)
        _posePassLabel->setText (tr("<b>Pass</b><br><span style=\"color:#18794e\">%1</span>")
            .arg (static_cast< int > (summary.passCount)));
    if (_poseWarningLabel != NULL)
        _poseWarningLabel->setText (tr("<b>Warning</b><br><span style=\"color:#a15c00\">%1</span>")
            .arg (static_cast< int > (summary.warningCount)));
    if (_poseFailLabel != NULL)
        _poseFailLabel->setText (tr("<b>Fail</b><br><span style=\"color:#b00020\">%1</span>")
            .arg (static_cast< int > (summary.failCount)));
    if (_poseRunDetailsLabel != NULL) {
        const QString tableNote = samples.size () > static_cast< std::size_t > (rows) ?
            tr("; showing first %1 rows").arg (rows) : QString ();
        const QString partialNote = summary.partialCount > 0 ?
            tr("; %1 partial position(s)").arg (
                static_cast< int > (summary.partialCount)) : QString ();
        const bool canceled = _poseReachabilityCancelRequested &&
            _poseReachabilityCancelRequested->load ();
        _poseRunDetailsLabel->setText (
            tr("Run: %1; %2 position(s); %3 / %4 IK target(s); %5 / %6 reachable direction(s)%7%8")
                .arg (canceled ? tr("canceled") : tr("complete"))
                .arg (static_cast< int > (summary.totalPositions))
                .arg (static_cast< int > (summary.completedIkTargets))
                .arg (static_cast< int > (summary.plannedIkTargets))
                .arg (static_cast< int > (summary.reachableDirections))
                .arg (static_cast< int > (summary.sampledDirections))
                .arg (partialNote)
                .arg (tableNote));
    }
    if (_poseMoreToggle != NULL &&
        (summary.warningCount > 0 || summary.failCount > 0 || summary.partialCount > 0))
        _poseMoreToggle->setChecked (true);
    _poseResultTable->resizeColumnsToContents ();

    // P4:有数据时启用导出和可视化按钮。
    if (_poseExportButton != NULL)
        _poseExportButton->setEnabled (!samples.empty ());
    if (_poseOpenVisualizationButton != NULL)
        _poseOpenVisualizationButton->setEnabled (!samples.empty ());
    refreshVisualization ();
}

// analyzePoseReachability:P4 改为 QtConcurrent::run 后台执行,
// 验证输入后启动异步 worker,跑完由 handlePoseReachabilityFinished 收尾。
void KinematicAnalysisWidget::analyzePoseReachability ()
{
    if (_poseReachabilityRunActive || _poseReachabilityWatcher == nullptr ||
        _poseReachabilityWatcher->isRunning ()) {
        setStatus (tr("Pose reachability is already running."));
        return;
    }

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

    const rw::core::Ptr< rw::models::WorkCell > runWorkCell =
        _studio != NULL ? _studio->getWorkCell () : NULL;
    _poseReachabilityCollisionUnavailable =
        config.checkCollision && runWorkCell == NULL;

    // P4:标记运行中,禁用 Run + 启用 Cancel,设忙光标。
    _poseReachabilityRunActive = true;
    if (_poseReachabilityCancelRequested)
        _poseReachabilityCancelRequested->store (false);
    _poseReachabilityWatcher->setProperty (
        "workcellSessionGeneration",
        QVariant::fromValue< qulonglong > (_workcellSessionGeneration));

    // P7:重置进度条,用精确执行计数(uncapped)确保进度不会超过 100%。
    {
        bool targetCountOverflowed = false;
        const std::size_t plannedTargets =
            poseReachabilityExecutionTargetCount (
                config, positions.size (), &targetCountOverflowed);
        updatePoseReachabilityProgress (
            0,
            static_cast< qulonglong > (plannedTargets));
        if (targetCountOverflowed && _poseProgressLabel != NULL) {
            _poseProgressLabel->setText (
                tr("Progress: 0 / overflow IK target(s)"));
        }
    }

    if (_poseAnalyzeButton != NULL)
        _poseAnalyzeButton->setEnabled (false);
    if (_poseCancelButton != NULL)
        _poseCancelButton->setEnabled (true);
    QApplication::setOverrideCursor (Qt::WaitCursor);
    setStatus (tr("Pose reachability running..."));

    // P5:构造可跨线程的安全取消+进度回调。取消用 shared_ptr<atomic_bool> 跨线程共享;
    // 进度用 QPointer 通过 QMetaObject::invokeMethod 回到 UI 线程。
    struct PoseRunContext {
        std::shared_ptr< std::atomic_bool > cancelFlag;
        QPointer< KinematicAnalysisWidget > widget;
        quint64 sessionGeneration = 0;
    };
    const std::shared_ptr< PoseRunContext > runContext =
        std::make_shared< PoseRunContext > ();
    runContext->cancelFlag = _poseReachabilityCancelRequested;
    runContext->widget = this;
    runContext->sessionGeneration = _workcellSessionGeneration;

    PoseReachabilityRunCallbacks callbacks;
    callbacks.isCancellationRequested = [] (void* userData) -> bool {
        const PoseRunContext* context =
            static_cast< const PoseRunContext* > (userData);
        return context != NULL && context->cancelFlag &&
               context->cancelFlag->load ();
    };
    callbacks.onProgress = [] (std::size_t completedTargets,
                               std::size_t plannedTargets,
                               void* userData) {
        PoseRunContext* context = static_cast< PoseRunContext* > (userData);
        if (context == NULL || context->widget.isNull ())
            return;
        QMetaObject::invokeMethod (
            context->widget.data (),
            [widget = context->widget, session = context->sessionGeneration,
             completedTargets, plannedTargets] {
                if (widget.isNull () || widget->_workcellSessionGeneration != session)
                    return;
                widget->updatePoseReachabilityProgress (
                    static_cast< qulonglong > (completedTargets),
                    static_cast< qulonglong > (plannedTargets));
            },
            Qt::QueuedConnection);
    };
    callbacks.userData = runContext.get ();

    // 捕获值而非指针,worker 不触及 widget 成员。
    const rw::kinematics::State runState = currentState ();
    const rw::core::Ptr< rw::models::Device > runDevice = device;
    const rw::core::Ptr< const rw::kinematics::Frame > runTcpFrame = selectedTcpFrame ();
    const KinematicThresholds runThresholds = _thresholds;

    QFuture< std::vector< PoseReachabilitySample > > future = QtConcurrent::run (
        [runDevice, runTcpFrame, runState, positions, config,
         runThresholds, callbacks, runContext, runWorkCell] () {
            KinematicAnalyzer worker;
            worker.setThresholds (runThresholds);
            const rw::core::Ptr< rw::proximity::CollisionDetector > detector =
                config.checkCollision ?
                    makeKinematicAnalysisCollisionDetector (runWorkCell) : NULL;
            PoseReachabilityConfig workerConfig = config;
            if (config.checkCollision && detector == NULL)
                workerConfig.checkCollision = false;
            return worker.analyzePoseReachability (
                runDevice, runTcpFrame, runState, positions, workerConfig,
                detector, callbacks);
        });
    _poseReachabilityWatcher->setFuture (future);
}

// updatePoseReachabilityProgress:P5 从后台线程通过 QMetaObject::invokeMethod
// 回调到 UI 线程,更新进度条和标签。
// P7:进度条范围限制在 MaxPoseReachabilityProgressBarSteps 内,标签保持精确值。
void KinematicAnalysisWidget::updatePoseReachabilityProgress (
    qulonglong completedTargets, qulonglong plannedTargets)
{
    const qulonglong boundedCompleted = plannedTargets == 0 ? 0 :
        std::min< qulonglong > (completedTargets, plannedTargets);
    const int barMax = plannedTargets >
            static_cast< qulonglong > (MaxPoseReachabilityProgressBarSteps) ?
        MaxPoseReachabilityProgressBarSteps :
        static_cast< int > (plannedTargets);
    const int barValue = plannedTargets == 0 ? 0 :
        static_cast< int > (
            (static_cast< double > (boundedCompleted) /
             static_cast< double > (plannedTargets)) *
            static_cast< double > (barMax));

    if (_poseProgressBar != NULL) {
        _poseProgressBar->setRange (0, barMax);
        _poseProgressBar->setValue (barValue);
    }
    if (_poseProgressLabel != NULL) {
        const double pct = plannedTargets == 0 ? 0.0 :
            100.0 * static_cast< double > (boundedCompleted) /
                static_cast< double > (plannedTargets);
        _poseProgressLabel->setText (
            tr("Progress: %1 / %2 IK target(s) (%3%)")
                .arg (static_cast< qulonglong > (boundedCompleted))
                .arg (static_cast< qulonglong > (plannedTargets))
                .arg (QString::number (pct, 'f', 1)));
    }
    if (_exploreModeCombo != NULL && _exploreModeCombo->currentIndex () == 2 &&
        _exploreStateLabel != NULL) {
        _exploreStateLabel->setText (
            tr ("Estimated: Running, %1 / %2 IK target(s)")
                .arg (completedTargets).arg (plannedTargets));
    }
}

// handlePoseReachabilityFinished:P4 后台 worker 完成回调。
// 恢复 UI 状态,读结果,刷新表格 / report。
void KinematicAnalysisWidget::handlePoseReachabilityFinished ()
{
    QApplication::restoreOverrideCursor ();
    const quint64 runGeneration = _poseReachabilityWatcher == nullptr ? 0 :
        _poseReachabilityWatcher->property ("workcellSessionGeneration").toULongLong ();
    if (runGeneration != _workcellSessionGeneration) {
        _poseReachabilityRunActive = false;
        updatePoseReachabilityControls ();
        refreshWorkflowControls ();
        return;
    }
    _poseReachabilityRunActive = false;
    if (_poseAnalyzeButton != NULL)
        _poseAnalyzeButton->setEnabled (true);
    if (_poseCancelButton != NULL)
        _poseCancelButton->setEnabled (false);

    const std::vector< PoseReachabilitySample > samples =
        _poseReachabilityWatcher->result ();
    const bool wasCanceled =
        _poseReachabilityCancelRequested &&
        _poseReachabilityCancelRequested->load ();
    _poseReachabilitySamples = samples;
    applyPoseReachabilityResults (_poseReachabilitySamples);

    // P5:完成后把进度条刷到最终数字。
    {
        const PoseReachabilitySummary summary =
            summarizePoseReachabilitySamples (_poseReachabilitySamples);
        updatePoseReachabilityProgress (
            static_cast< qulonglong > (summary.completedIkTargets),
            static_cast< qulonglong > (summary.plannedIkTargets));
    }

    updateReportSummary ();
    const QString collisionNote = _poseReachabilityCollisionUnavailable ?
        tr(" Collision checking was unavailable.") : QString ();
    if (wasCanceled) {
        setStatus (tr("Pose reachability canceled after %1 position(s).%2")
                       .arg (static_cast< int > (_poseReachabilitySamples.size ()))
                       .arg (collisionNote));
    }
    else {
        setStatus (tr("Pose reachability completed for %1 position(s).%2")
                       .arg (static_cast< int > (_poseReachabilitySamples.size ()))
                       .arg (collisionNote));
    }
    if (_exploreModeCombo != NULL && _exploreModeCombo->currentIndex () == 2 &&
        _exploreStateLabel != NULL) {
        _exploreStateLabel->setText (
            wasCanceled
                ? tr ("Estimated: DataInsufficient, %1 position(s)")
                      .arg (static_cast< int > (_poseReachabilitySamples.size ()))
                : tr ("Estimated: Completed, %1 position(s)")
                      .arg (static_cast< int > (_poseReachabilitySamples.size ())));
    }
    refreshWorkflowControls ();
}

// exportPoseReachabilityCsv:把 _poseReachabilitySamples 写为 CSV,
// 列与表格一致(位置 + sampled + reachable + coverage + status)。
void KinematicAnalysisWidget::exportPoseReachabilityCsv ()
{
    // P4:空数据提前返回。
    if (_poseReachabilitySamples.empty ()) {
        setStatus (tr("No pose reachability samples to export."));
        return;
    }
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
    // P6:CSV 头部添加 # pose_reachability_summary 注释行。
    {
        const PoseReachabilitySummary summary =
            summarizePoseReachabilitySamples (_poseReachabilitySamples);
        out << "# pose_reachability_summary,total," << summary.totalPositions
            << ",pass," << summary.passCount
            << ",warning," << summary.warningCount
            << ",fail," << summary.failCount
            << ",sampled_directions," << summary.sampledDirections
            << ",reachable_directions," << summary.reachableDirections
            << ",avg_coverage," << summary.averageCoverage
            << ",min_coverage," << summary.minCoverage
            << ",max_coverage," << summary.maxCoverage
            << ",partial," << summary.partialCount
            << ",completed_ik_targets," << summary.completedIkTargets
            << ",planned_ik_targets," << summary.plannedIkTargets
            << "\n";
    }
    out << "position_x,position_y,position_z,sampled_directions,reachable_directions,coverage,status,partial,completed_ik_targets,planned_ik_targets,has_representative_q,representative_q,representative_direction,representative_roll\n";
    for (const PoseReachabilitySample& sample : _poseReachabilitySamples) {
        out << sample.position[0] << "," << sample.position[1] << ","
            << sample.position[2] << "," << sample.sampledDirections << ","
            << sample.reachableDirections << "," << sample.coverage << ","
            << statusText (sample.status) << ","
            << (sample.partial ? "true" : "false") << ","
            << sample.completedIkTargets << ","
            << sample.plannedIkTargets << ","
            << (sample.hasRepresentativeQ ? "true" : "false") << ",\""
            << qVectorText (sample.representativeQ) << "\","
            << sample.representativeDirectionIndex << ","
            << sample.representativeRollIndex << "\n";
    }
    setStatus (tr("Exported %1 pose reachability row(s).")
                   .arg (static_cast< int > (_poseReachabilitySamples.size ())));
}

// updateReportSummary:Report tab 的中央枢纽。
//   - 用 analyzer.buildAggregateResult 把四类数据聚合成 KinematicAnalysisResult;
//   - 在 summary 标签里显示总状态、可达率、当前条件数 / 可操作度、任务点计数、
//     工作空间总数、位姿可达性平均 coverage;
//   - 把 result.warnings 全部写入告警表。
// reportFilters:从 Report tab 四个过滤下拉(Stage / Feasibility / Quality / Failure)
// 与区域文本框读取当前视图过滤条件。只影响汇总展示与导出,不改变底层分析结果。
KinematicAnalysisReportFilters KinematicAnalysisWidget::reportFilters () const
{
    KinematicAnalysisReportFilters filters;
    if (_reportStageFilterCombo != nullptr && _reportStageFilterCombo->currentIndex () > 0) {
        filters.filterStage = true;
        filters.stage = static_cast< AnalysisEvidenceStage > (
            _reportStageFilterCombo->currentIndex () - 1);
    }
    if (_reportFeasibilityFilterCombo != nullptr &&
        _reportFeasibilityFilterCombo->currentIndex () > 0) {
        filters.filterFeasibility = true;
        filters.feasibility = static_cast< Feasibility > (
            _reportFeasibilityFilterCombo->currentIndex () - 1);
    }
    if (_reportQualityFilterCombo != nullptr && _reportQualityFilterCombo->currentIndex () > 0) {
        filters.filterQuality = true;
        filters.quality = static_cast< Quality > (_reportQualityFilterCombo->currentIndex () - 1);
    }
    if (_reportFailureFilterCombo != nullptr && _reportFailureFilterCombo->currentIndex () > 0) {
        filters.filterFailureReason = true;
        filters.failureReason = static_cast< KinematicFailureReason > (
            _reportFailureFilterCombo->currentIndex ());
    }
    if (_reportRegionFilterEdit != nullptr)
        filters.regionId = _reportRegionFilterEdit->text ().trimmed ().toStdString ();
    return filters;
}

// updateReportSummary:Report tab 的中央汇总。聚合当前位姿 / 任务点 / 工作空间 /
// 位姿可达性四类数据,套用 Report tab 的视图过滤(reportFilters),把总状态、
// 可达率、工作空间统计、平均位姿覆盖度写入 summary 标签,并把告警写进告警表。
void KinematicAnalysisWidget::updateReportSummary ()
{
    if (_reportSummaryLabel == NULL)
        return;
    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    const KinematicAnalysisResult result = analyzer.buildAggregateResult (
        _lastCurrentPose, _lastTaskPointResults, _workspaceSamples, _poseReachabilitySamples);
    const KinematicAnalysisReport report = buildReportForExport ();
    const KinematicAnalysisReport filteredReport = filterReportView (report, reportFilters ());

    int taskPass = 0, taskWarn = 0, taskFail = 0;
    for (const TargetEvaluation& task : filteredReport.taskResults) {
        if (task.feasibility == Feasibility::Feasible && task.quality == Quality::Good)
            ++taskPass;
        else if (task.quality == Quality::Degraded)
            ++taskWarn;
        else if (task.feasibility == Feasibility::Infeasible)
            ++taskFail;
    }
    // P6:用 helper 替代手动累加覆盖率。
    const PoseReachabilitySummary poseSummary =
        summarizePoseReachabilitySamples (_poseReachabilitySamples);
    const double poseCoverage = poseSummary.averageCoverage;

    // P4:Workspace 行从简单计数升级为 pass / warning / fail / collision / avg manip / max cond。
    const WorkspaceSummary wsSummary = summarizeWorkspaceSamples (_workspaceSamples);
    const QString wsAvgManip = wsSummary.hasManipulability
        ? QString::number (wsSummary.avgManipulability, 'g', 6) : QStringLiteral ("-");
    const QString wsMaxCond = wsSummary.hasCondition
        ? QString::number (wsSummary.maxCondition, 'g', 6) : QStringLiteral ("-");

    _reportSummaryLabel->setText (
        tr("Status: %1\nReachable rate: %2\nCurrent condition: %3\nCurrent manipulability: %4\n"
           "Task points: Pass %5 / Warning %6 / Fail %7\n"
           "Workspace: %8 samples, pass %9, warning %10, fail %11, collision %12, "
           "avg manip %13, max cond %14\n"
           "Average pose coverage: %15\nFiltered tasks: %16, regions: %17")
            .arg (QString::fromLatin1 (statusText (result.status)))
            .arg (QString::number (result.reachableRate, 'f', 3))
            .arg (QString::number (_lastCurrentPose.conditionNumber, 'g', 6))
            .arg (QString::number (_lastCurrentPose.manipulability, 'g', 6))
            .arg (taskPass).arg (taskWarn).arg (taskFail)
            .arg (static_cast< int > (wsSummary.totalCount))
            .arg (static_cast< int > (wsSummary.passCount))
            .arg (static_cast< int > (wsSummary.warningCount))
            .arg (static_cast< int > (wsSummary.failCount))
            .arg (static_cast< int > (wsSummary.collisionCount))
            .arg (wsAvgManip)
            .arg (wsMaxCond)
            .arg (QString::number (poseCoverage, 'f', 3))
            .arg (static_cast< int > (filteredReport.taskResults.size ()))
            .arg (static_cast< int > (filteredReport.regionResults.size ())));

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

// buildReportForExport:把当前会话汇总成可导出的 KinematicAnalysisReport。
//   - 若已跑过冻结需求校验(_validateHasResults),直接输出 Verified 级汇总
//     (任务 + 区域单元 + 指纹 + 告警),analysisId 标记为 requirements-validation;
//   - 否则按交互分析(Quick 级)聚合:把聚合状态映射为 Feasibility / Quality,
//     并把每个任务点的 IK 候选逐一转成 TargetCandidate(含 q / 关节裕度 /
//     条件数 / 可操作度 / 碰撞),供 JSON 与 CSV 报告消费。
KinematicAnalysisReport KinematicAnalysisWidget::buildReportForExport () const
{
    KinematicAnalysisReport report;
    report.pluginName = "kinematicanalysis";
    report.currentPose = _lastCurrentPose;

    if (_validateHasResults) {
        report.provenance = _validateSummary.provenance;
        report.feasibility = _validateSummary.feasibility;
        report.quality = _validateSummary.quality;
        report.evidenceStage = _validateSummary.stage;
        report.taskResults = _validateSummary.taskResults;
        report.regionResults = _validateSummary.regionResults;
        report.warnings = _validateSummary.warnings;
        report.analysisId = "requirements-validation";
        return report;
    }

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    const KinematicAnalysisResult aggregate = analyzer.buildAggregateResult (
        _lastCurrentPose, _lastTaskPointResults, _workspaceSamples, _poseReachabilitySamples);
    report.feasibility = aggregate.status == AnalysisStatus::Pass
        ? Feasibility::Feasible
        : aggregate.status == AnalysisStatus::Fail ? Feasibility::Infeasible
        : aggregate.status == AnalysisStatus::Unknown ? Feasibility::NotEvaluated
                                                       : Feasibility::DataInsufficient;
    report.quality = aggregate.status == AnalysisStatus::Pass
        ? Quality::Good
        : aggregate.status == AnalysisStatus::Fail ? Quality::Critical
                                                    : Quality::Degraded;
    report.evidenceStage = AnalysisEvidenceStage::Quick;
    report.analysisId = "interactive-analysis";
    report.warnings = aggregate.warnings;
    for (const TaskPointReachabilityResult& source : _lastTaskPointResults) {
        TargetEvaluation target;
        target.stage = AnalysisEvidenceStage::Quick;
        target.target = source.taskPoint;
        target.feasibility = source.status == AnalysisStatus::Pass
            ? Feasibility::Feasible
            : source.status == AnalysisStatus::Fail ? Feasibility::Infeasible
                                                    : Feasibility::NotEvaluated;
        target.quality = source.status == AnalysisStatus::Pass
            ? Quality::Good
            : source.status == AnalysisStatus::Fail ? Quality::Critical
                                                     : Quality::Degraded;
        target.failureReasons = source.failureReasons;
        target.warnings = source.ik.warnings;
        for (const KinematicIkSolution& solution : source.ik.solutions) {
            TargetCandidate candidate;
            candidate.configuration.stage = AnalysisEvidenceStage::Quick;
            candidate.configuration.feasibility = solution.status == AnalysisStatus::Pass
                ? Feasibility::Feasible
                : solution.status == AnalysisStatus::Fail ? Feasibility::Infeasible
                                                          : Feasibility::NotEvaluated;
            candidate.configuration.quality = solution.status == AnalysisStatus::Pass
                ? Quality::Good
                : solution.status == AnalysisStatus::Fail ? Quality::Critical
                                                           : Quality::Degraded;
            candidate.configuration.q = rw::math::Q (
                static_cast< int > (solution.q.size ()), 0.0);
            for (std::size_t index = 0; index < solution.q.size (); ++index)
                candidate.configuration.q[index] = solution.q[index];
            candidate.configuration.minimumJointMargin = solution.minJointLimitMargin;
            candidate.configuration.conditionNumber = solution.conditionNumber;
            candidate.configuration.manipulability = solution.manipulability;
            candidate.configuration.inCollision = solution.inCollision;
            candidate.configuration.collisionChecked = solution.inCollision ||
                std::find (solution.failureReasons.begin (), solution.failureReasons.end (),
                           KinematicFailureReason::Collision) != solution.failureReasons.end ();
            candidate.configuration.failureReasons = solution.failureReasons;
            candidate.positionErrorMeters = solution.positionErrorMeters;
            candidate.orientationErrorDeg = solution.orientationErrorDeg;
            candidate.distanceToReferenceQ = solution.distanceToCurrentQ;
            candidate.score = solution.score;
            target.candidates.push_back (candidate);
        }
        report.taskResults.push_back (target);
    }
    return report;
}


// exportReportJson:把 buildReportForExport 得到的规范报告序列化为 JSON 并写入
// 用户选择的路径,便于下游工具 / 文档系统消费完整校验与交互分析结果。
// Export the canonical kinematic analysis report as JSON.
void KinematicAnalysisWidget::exportReportJson ()
{
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export kinematic report"), QString ("kinematic_report.json"),
        tr("JSON files (*.json);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus (tr("Report JSON export canceled."));
        return;
    }

    const KinematicAnalysisReport report = filterReportView (
        buildReportForExport (), reportFilters ());
    QFile unifiedFile (path);
    if (!unifiedFile.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        return;
    }
    unifiedFile.write (QByteArray::fromStdString (KinematicAnalysisReportJson::toJson (report)));
    setStatus (tr("Exported kinematic JSON report."));
    return;

}

// exportReportCsv:把规范报告的任务与区域单元两条 CSV 记录合并写入同一文件,
// 供表格工具直接打开;与 JSON 报告共享 buildReportForExport 的数据源。
// Export the canonical task and region report CSV records.
void KinematicAnalysisWidget::exportReportCsv ()
{
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export kinematic summary"), QString ("kinematic_summary.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ()) {
        setStatus (tr("Report CSV export canceled."));
        return;
    }
    const KinematicAnalysisReport report = filterReportView (
        buildReportForExport (), reportFilters ());
    QFile unifiedFile (path);
    if (!unifiedFile.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        return;
    }
    unifiedFile.write (QByteArray::fromStdString (KinematicAnalysisReportJson::taskCsv (report)));
    unifiedFile.write (QByteArray::fromStdString (KinematicAnalysisReportJson::regionCsv (report)));
    setStatus (tr("Exported kinematic report CSV."));
    return;
}

// openThresholdSettingsDialog:顶部 Thresholds 按钮的槽,以事务方式编辑全部分析阈值。
// 对话框持有阈值副本,用户点 Cancel 时不做任何控件状态修改;点 Accept 后把结果
// 写回 _thresholds 并同步到 Report tab 各 SpinBox(QSignalBlocker 抑制回写期间
// 的信号),同时使包络缓存失效以提示重新分析。
// openThresholdSettingsDialog: header action keeps threshold editing
// transactional. The dialog receives a copy; no widget state changes on Cancel.
void KinematicAnalysisWidget::openThresholdSettingsDialog ()
{
    KinematicThresholdsDialog dialog (_thresholds, _lengthUnit, _angleUnit, this);
    if (dialog.exec () != QDialog::Accepted)
        return;

    _thresholds = dialog.thresholds ();
    // The legacy IK input remains visible in the Diagnose page, so keep it in
    // lockstep with the threshold snapshot consumed by KinematicAnalyzer.
    const QSignalBlocker duplicateBlocker (_ikDuplicateQThresholdSpin);
    const QSignalBlocker nearLimitBlocker (_thresholdNearLimitSpin);
    const QSignalBlocker warningBlocker (_thresholdConditionWarningSpin);
    const QSignalBlocker failBlocker (_thresholdConditionFailSpin);
    const QSignalBlocker singularBlocker (_thresholdSingularValueSpin);
    const QSignalBlocker manipulabilityBlocker (_thresholdManipulabilitySpin);
    const QSignalBlocker positionBlocker (_thresholdPositionToleranceSpin);
    const QSignalBlocker orientationBlocker (_thresholdOrientationToleranceSpin);
    _ikDuplicateQThresholdSpin->setValue (_thresholds.ikDuplicateQThreshold);
    _thresholdNearLimitSpin->setValue (_thresholds.nearJointLimitRatio);
    _thresholdConditionWarningSpin->setValue (_thresholds.conditionWarning);
    _thresholdConditionFailSpin->setValue (_thresholds.conditionFail);
    _thresholdSingularValueSpin->setValue (_thresholds.singularValueWarning);
    _thresholdManipulabilitySpin->setValue (_thresholds.manipulabilityWarning);
    _thresholdPositionToleranceSpin->setValue (
        displayLengthFromMeters (_thresholds.positionToleranceMeters, _lengthUnit));
    _thresholdOrientationToleranceSpin->setValue (
        displayAngleFromDegrees (_thresholds.orientationToleranceDeg, _angleUnit));

    invalidateEnvelopeCache ();
    setStatus (tr("Kinematic thresholds updated. Existing analysis results are stale; re-run analyses."));
    if (!_applyingProjectDocument)
        Q_EMIT projectDocumentChanged ();
}

// applyThresholds:Report 页 Apply thresholds 按钮的兼容路径。把 Report tab 七个
// SpinBox 的当前值(含单位换算)全部写入 _thresholds 供后续分析使用,并把旧的
// duplicate-Q 阈值同步进来,保持与顶部阈值对话框同一份快照一致。
// applyThresholds: compatibility path for the existing Report page controls.
// It mirrors the legacy duplicate-Q value into the common threshold snapshot.
void KinematicAnalysisWidget::applyThresholds ()
{
    _thresholds.nearJointLimitRatio = _thresholdNearLimitSpin->value ();
    _thresholds.conditionWarning = _thresholdConditionWarningSpin->value ();
    _thresholds.conditionFail = _thresholdConditionFailSpin->value ();
    _thresholds.singularValueWarning = _thresholdSingularValueSpin->value ();
    _thresholds.manipulabilityWarning = _thresholdManipulabilitySpin->value ();
    _thresholds.positionToleranceMeters = metersFromDisplayLength (
        _thresholdPositionToleranceSpin->value (), _lengthUnit);
    _thresholds.orientationToleranceDeg = degreesFromDisplayAngle (
        _thresholdOrientationToleranceSpin->value (), _angleUnit);
    _thresholds.ikDuplicateQThreshold = _ikDuplicateQThresholdSpin->value ();
    invalidateEnvelopeCache ();
    setStatus (tr("Kinematic thresholds updated. Re-run analyses to refresh results."));
}
