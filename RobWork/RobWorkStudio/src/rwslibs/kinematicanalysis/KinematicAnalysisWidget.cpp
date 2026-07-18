#include "KinematicAnalysisWidget.hpp"

#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalyzer.hpp"
#include "TaskPointResolver.hpp"
#include "TaskPointUiLogic.hpp"
#include "TaskPointTableModel.hpp"
#include "TaskPointDelegates.hpp"
#include "KinematicAnalysisPlotWidget.hpp"
#include "KinematicAnalysisVisualizationTypes.hpp"
#include "KinematicAnalysisWorkspace.hpp"
#include "KinematicAnalysisPoseReachability.hpp"

// 鍏变韩鐨?CSV / JSON 搴忓垪鍖栧伐鍏?TaskPoint 涓庢湰鎻掍欢澶嶇敤浜嗗畠銆?
#include <rwslibs/robotanalysiscore/RobotAnalysisCsv.hpp>
#include <rwslibs/robotanalysiscore/RobotAnalysisValidation.hpp>

#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/RPY.hpp>
#include <rws/RobWorkStudio.hpp>

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>
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
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableView>
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

namespace {
// Task point 琛ㄦ牸鍒楃储寮曟灇涓?缁熶竴鎵€鏈夎鍐欎唬鐮?閬垮厤鍒楀彿纭紪鐮佹紓绉汇€?
// 鍓?19 鍒楀搴?RobotAnalysisCsv 鏍囧噯瀛楁 + UI 琛嶇敓 status / reason;
// 鍚?8 鍒楁槸 P1 鏂板鐨勬壒閲?IK 缁撴灉鍒?raw / usable / bestQ / posErr /
// oriErr / margin / condition / collision)銆?
// 璇ユ灇涓惧繀椤诲湪 buildTaskPointTab 涔嬪墠瀹氫箟,鍚﹀垯 setColumnCount 涓?
// setHorizontalHeaderLabels 寮曠敤 Col* 鏃朵細缂栬瘧澶辫触銆?
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
    // P1:鎵归噺 IK 缁撴灉鍒?
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

// 鏋勯€犲嚱鏁?
//   - 鎶婃墍鏈夋垚鍛樻寚閽堝厛缃?NULL(闃插尽鎬у垵濮嬪寲);
//   - 鐢?QVBoxLayout + QScrollArea 鍖呰９涓诲唴瀹瑰尯,
//     杩欐牱鎻掍欢鍙互鍦ㄥ皬绐楀彛涓嬩繚鎸佸彲鐢?婊氬姩鏉″嚭鐜?;
//   - 椤堕儴涓€琛屾槸璁惧 / TCP 甯ч€夋嫨 + Refresh 鎸夐挳;
//   - QTabWidget 鎵胯浇 6 涓瓙椤?
//   - 搴曢儴涓€涓彧璇荤姸鎬佹潯鐢ㄤ簬鍙嶉鐢ㄦ埛鎿嶄綔缁撴灉;
//   - 鏈熬鎶婃墍鏈夋寜閽殑 clicked() 淇″彿杩炲埌瀵瑰簲鐨勬Ы鍑芥暟銆?
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
    _visualizationTab(NULL),
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
    _taskPointModel(nullptr),
    _addTaskPointButton(NULL),
    _removeTaskPointButton(NULL),
    _importTaskPointsButton(NULL),
    _exportTaskPointsButton(NULL),
    _analyzeAllTaskPointsButton(NULL),
    // P2:Task points 涓撶敤鎸夐挳(NULL 瀹堝崼,瑙佹瀽鏋?/ 鏋愭瀯鏈熸竻鐞?
    _analyzeSelectedTaskPointsButton(NULL),
    _importCurrentTcpTaskPointButton(NULL),
    _applySelectedTaskPointBestQButton(NULL),
    _openSelectedTaskPointInIkButton(NULL),
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
    _poseCancelButton(NULL),
    _poseReachabilityWatcher(NULL),
    _poseReachabilityRunActive(false),
    _poseReachabilityCancelRequested(std::make_shared< std::atomic_bool > (false)),
    _poseSummaryLabel(NULL),
    _poseDiagnosticsLabel(NULL),
    _poseProgressBar(NULL),
    _poseProgressLabel(NULL),
    _posePositionTable(NULL),
    _poseResultTable(NULL),
    _visualSourceCombo(NULL),
    _visualProjectionCombo(NULL),
    _visualColorModeCombo(NULL),
    _visualShowPassCheck(NULL),
    _visualShowWarningCheck(NULL),
    _visualShowFailCheck(NULL),
    _visualShowLabelsCheck(NULL),
    _visualSummaryLabel(NULL),
    _visualPlot(NULL),
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
    // 鍗曞垪鍏ㄥ瀵嗛泦甯冨眬(浠庝笂鍒颁笅):
    //   1. 绱у噾鎽樿  鈥?2 琛?6 鍒楃殑浣嶇疆/濮挎€?+ 1 琛屽叧閿寚鏍?
    //   2. 鍏宠妭鐘舵€佸悎骞惰〃 鈥?Joint | q | Limit margin | Status;
    //   3. Jacobian 鍏ㄥ涓昏〃(琛?vx/vy/vz/wx/wy/wz,鍒?q0..qn);
    //   4. Singular values 妯悜灏忚〃(1 琛?;
    //   5. Warnings 榛樿鍘嬫垚鍗曡 \"Warnings: None\"銆?
    _currentPoseTab = new QWidget(_tabs);
    QVBoxLayout* cpLayout = new QVBoxLayout(_currentPoseTab);

    // 鍏辩敤鐨勭揣鍑戣〃鏍煎伐鍘?6 鍒楀唴 stretch銆侀殣钘忓瀭鐩存粴鍔ㄦ潯銆?
    // 鍙栨秷鍨傜洿 header(琛屽悕閫氳繃 setVerticalHeaderLabels 鑷畾涔?銆?
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

    // ---- 1. 绱у噾鎽樿鏍?鍙崰琛ㄥご + 1 琛屽€?鍥哄畾楂樺害) ----
    cpLayout->addWidget (new QLabel(tr("TCP pose"), _currentPoseTab));
    _poseValueTable = makeCompactTable (6, 1);
    _poseValueTable->setHorizontalHeaderLabels (
        {tr("pos x (m)"), tr("pos y (m)"), tr("pos z (m)"),
         tr("roll (deg)"), tr("pitch (deg)"), tr("yaw (deg)")});
    // 1 琛屽唴瀹?鍒濆鍗犱綅
    for (int c = 0; c < 6; ++c)
        _poseValueTable->setItem (0, c, makeItem (QStringLiteral ("-")));
    cpLayout->addWidget (_poseValueTable);
    setCompactTableVisibleRows (_poseValueTable, 1);
    _poseValueTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);

    // 鍏抽敭鎸囨爣鍗曡鏍囩:Condition / Manipulability / Min limit margin
    _poseIndicatorLabel = new QLabel (
        tr("Condition: -    Manipulability: -    Min limit margin: -"),
        _currentPoseTab);
    cpLayout->addWidget (_poseIndicatorLabel);

    // ---- 2. 鍏宠妭鐘舵€佸悎骞惰〃(鍏ㄥ垪 stretch,鍥哄畾楂樺害) ----
    cpLayout->addWidget (new QLabel(tr("Joint status"), _currentPoseTab));
    _jointStatusTable = makeCompactTable (4, 0);
    _jointStatusTable->setHorizontalHeaderLabels (
        {tr("Joint"), tr("q"), tr("Limit margin"), tr("Status")});
    // 4 鍒楀唴瀹逛笉澶?妯悜 stretch 瀹屽叏鑳介摵婊?鍏抽棴姘村钩婊氬姩;
    // 鍨傜洿鏂瑰悜鏍规嵁瀹為檯琛屾暟鍔ㄦ€佽楂?璇﹁ refreshCurrentPose)銆?
    _jointStatusTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    cpLayout->addWidget (_jointStatusTable);

    // ---- 3. Jacobian 鍏ㄥ涓昏〃 ----
    cpLayout->addWidget (new QLabel(tr("Jacobian (TCP velocity = J * joint velocity)"), _currentPoseTab));
    _jacobianTable = makeCompactTable (1, 1);
    _jacobianTable->verticalHeader()->setVisible (true);
    _jacobianTable->setVerticalHeaderLabels ({tr("vx"), tr("vy"), tr("vz"),
                                              tr("wx"), tr("wy"), tr("wz")});
    // 鏁板瓧缁熶竴 %.6g 绮惧害,鐣欏嚭绌洪棿;
    // refreshCurrentPose 闃舵鍐嶇敤 makeItem(double) 鍐欏叆銆?
    cpLayout->addWidget (_jacobianTable);

    // ---- 4. Singular values:琛ㄥご + 1 琛屽€?鍥哄畾楂樺害) ----
    cpLayout->addWidget (new QLabel(tr("Singular values"), _currentPoseTab));
    // 鍒楁暟浼氬湪 refreshCurrentPose 涓寜 蟽 涓暟鍔ㄦ€佽瀹?鎵€浠ュ厛寤?0 鍒?1 琛?
    // 琛屾暟鍥哄畾涓?1,index 宸插湪 horizontal header(蟽0 / 蟽1 / ... / 蟽min),
    // 涓嶅啀闇€瑕佸崟鐙?index 琛屻€?
    _singularTable = makeCompactTable (0, 1);
    cpLayout->addWidget (_singularTable);
    setCompactTableVisibleRows (_singularTable, 1);
    _singularTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);

    // ---- 5. Warnings:榛樿鍘嬫垚涓€琛?----
    _warningLabel = new QLabel (tr("Warnings: None"), _currentPoseTab);
    _warningLabel->setWordWrap (true);
    cpLayout->addWidget (_warningLabel);

    cpLayout->addStretch ();

    _tabs->addTab (_currentPoseTab, tr("Current pose"));

    // -------------------- IK Tab --------------------
    // 鍗曞垪鍏ㄥ瀵嗛泦甯冨眬(涓?Current pose 涓€鑷?:
    //   1. 椤堕儴杈撳叆:Target + 鍗曚綅 + 6 涓?pose spin + threshold + 3 涓姩浣滄寜閽?
    //   2. 杩囨护 + solver 鍏冧俊鎭?+ 璁℃暟 summary;
    //   3. status summary 鏍囩;
    //   4. IK solution status table(鍏佽婊氬姩,妯悜閾烘弧);
    //   5. 璇︽儏闈㈡澘(2 琛屽浐瀹氶珮搴?璺熼殢閫変腑琛屾洿鏂?銆?
    _ikTab         = new QWidget(_tabs);
    QVBoxLayout* ikLayout = new QVBoxLayout(_ikTab);

    // ---- 绗?1 琛?Target + 鍗曚綅閫夋嫨 ----
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

    // ---- 绗?2 琛?浣嶅Э 6 涓?spin,2 琛?脳 3 鍒?----
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

    // ---- 绗?3 琛?threshold + 3 涓姩浣滄寜閽?妯帓)----
    QHBoxLayout* ikDedupRow = new QHBoxLayout();
    ikDedupRow->addWidget(new QLabel(tr("Duplicate Q threshold:"), _ikTab));
    ikDedupRow->addWidget(_ikDuplicateQThresholdSpin);
    _ikImportCurrentPoseButton = new QPushButton(tr("Import current TCP pose"), _ikTab);
    _ikSolveButton = new QPushButton(tr("Solve"), _ikTab);
    _ikApplyButton = new QPushButton(tr("Apply selected Q"), _ikTab);
    _ikApplyButton->setEnabled (false);   // 閫変腑鍙敤瑙ｅ墠绂佺敤
    ikDedupRow->addSpacing (12);
    ikDedupRow->addWidget(_ikImportCurrentPoseButton);
    ikDedupRow->addWidget(_ikSolveButton);
    ikDedupRow->addWidget(_ikApplyButton);
    ikDedupRow->addStretch (1);
    ikLayout->addLayout(ikDedupRow);

    // ---- 绗?4 琛?杩囨护鍣?+ solver 鍏冧俊鎭?----
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

    // ---- 绗?5 琛?counts summary ----
    _ikCountSummaryLabel = new QLabel(
        tr("Raw - | Unique - | Usable - | Pass - | Warning - | Fail -"), _ikTab);
    ikLayout->addWidget(_ikCountSummaryLabel);

    // ---- 绗?6 琛?status summary 鏍囩 ----
    _ikSummaryLabel = new QLabel(tr("Candidates: -    Usable unique: -"), _ikTab);
    ikLayout->addWidget(_ikSummaryLabel);

    // ---- 绗?7 琛?IK solution status table(鍏佽妯旱婊氬姩)----
    ikLayout->addWidget(new QLabel(tr("IK solution status"), _ikTab));
    _ikSolutionTable = makeTable();
    // 鎶?"Q / failures" 鎷嗘垚涓ゅ垪 鈥?Failure(鐭枃鏈? + Q(鍏宠妭鍚戦噺),
    // 闀?Q 鍊间笉鍐嶅悶鎺夊け璐ュ師鍥犮€?
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
    // Task 5:榛樿鍒楀銆傜獎鍒?鏍囩 / 甯冨皵)+ 涓垪(鏁板€?+ 瀹藉垪(Q),
    // 閬垮厤闀?Q 鎶婃墍鏈夋暟鍊煎垪鎸ょ獎銆俿etStretchLastSection(true) 璁?Q 鍒?
    // 鍦ㄧ獥鍙ｅ彉瀹芥椂缁х画鍚告敹澶氫綑瀹藉害銆?
    _ikSolutionTable->setColumnWidth (0, 60);    // Index
    _ikSolutionTable->setColumnWidth (1, 80);    // Status
    _ikSolutionTable->setColumnWidth (2, 180);   // Failure
    _ikSolutionTable->setColumnWidth (3, 80);    // Current Q
    _ikSolutionTable->setColumnWidth (4, 80);    // Collision
    _ikSolutionTable->setColumnWidth (5, 100);   // Distance
    _ikSolutionTable->setColumnWidth (6, 120);   // Min limit margin
    _ikSolutionTable->setColumnWidth (7, 120);   // Manipulability
    _ikSolutionTable->setColumnWidth (8, 100);   // Condition
    _ikSolutionTable->setColumnWidth (9, 110);   // Position error
    _ikSolutionTable->setColumnWidth (10, 120);  // Orientation error
    _ikSolutionTable->setColumnWidth (11, 260);  // Q
    _ikSolutionTable->horizontalHeader()->setStretchLastSection(true);
    // 閫変腑琛屽彉鍖?鈫?璇︽儏琛ㄦ洿鏂般€?
    connect (_ikSolutionTable, SIGNAL (itemSelectionChanged ()),
             this, SLOT (updateIkSolutionDetails ()));
    // 璇ヨ〃鏄〉闈㈠敮涓€鍏佽婊氬姩鐨勪富琛?鍗犱富瀵奸珮搴︺€?
    ikLayout->addWidget(_ikSolutionTable, 1);

    // ---- 绗?8 琛?閫変腑璇︽儏(2 琛屽浐瀹氶珮搴?----
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
    _visualizationTab = new QWidget(_tabs);
    _reportTab     = new QWidget(_tabs);

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

    _tabs->addTab (_ikTab,         tr("IK"));
    _tabs->addTab (_taskPointTab,  tr("Task points"));
    _tabs->addTab (_workspaceTab,  tr("Workspace"));
    _tabs->addTab (_poseReachTab,  tr("Pose reachability"));
    _tabs->addTab (_visualizationTab, tr("Visualization"));
    _tabs->addTab (_reportTab,     tr("Report"));

    _status = new QLineEdit(base);
    _status->setReadOnly(true);
    content->addWidget(_status);

    connect (_refreshCurrentPoseButton, SIGNAL (clicked ()), this, SLOT (refreshCurrentPose ()));
    // Task 5 step 2:鍕鹃€夎繃婊ゅ櫒鏃跺嵆鏃跺埛鏂?IK 缁撴灉琛ㄤ笌缁熻銆?
    connect (_ikShowUsableOnlyCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshIkSolutionView ()));
    connect (_ikShowFailedCandidatesCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshIkSolutionView ()));
    connect (_ikImportCurrentPoseButton, SIGNAL (clicked ()), this, SLOT (importCurrentPoseToIk ()));
    connect (_ikDistanceUnitCombo, SIGNAL (currentIndexChanged (int)), this, SLOT (updateIkUnitDisplay ()));
    connect (_ikAngleUnitCombo, SIGNAL (currentIndexChanged (int)), this, SLOT (updateIkUnitDisplay ()));
    connect (_deviceCombo,
             static_cast< void (QComboBox::*) (int) > (&QComboBox::currentIndexChanged),
             this,
             [this] (int) { installTaskPointDelegates (); });
    connect (_ikSolveButton, SIGNAL (clicked ()), this, SLOT (solveIk ()));
    connect (_ikApplyButton, SIGNAL (clicked ()), this, SLOT (applySelectedIkSolution ()));
    connect (_addTaskPointButton, SIGNAL (clicked ()), this, SLOT (addTaskPointRow ()));
    connect (_removeTaskPointButton, SIGNAL (clicked ()), this, SLOT (removeSelectedTaskPointRow ()));
    connect (_importTaskPointsButton, SIGNAL (clicked ()), this, SLOT (importTaskPointsCsv ()));
    connect (_exportTaskPointsButton, SIGNAL (clicked ()), this, SLOT (exportTaskPointsCsv ()));
    connect (_exportTaskPointResultsButton, SIGNAL (clicked ()), this, SLOT (exportTaskPointResultsCsv ()));
    connect (_analyzeAllTaskPointsButton, SIGNAL (clicked ()), this, SLOT (analyzeAllTaskPoints ()));
    // P2:Task points 涓撶敤鎸夐挳鐨勪俊鍙疯繛鎺?
    connect (_analyzeSelectedTaskPointsButton, SIGNAL (clicked ()),
             this, SLOT (analyzeSelectedTaskPoints ()));
    connect (_importCurrentTcpTaskPointButton, SIGNAL (clicked ()),
             this, SLOT (importCurrentTcpAsTaskPoint ()));
    connect (_applySelectedTaskPointBestQButton, SIGNAL (clicked ()),
             this, SLOT (applySelectedTaskPointBestQ ()));
    connect (_openSelectedTaskPointInIkButton, SIGNAL (clicked ()),
             this, SLOT (openSelectedTaskPointInIk ()));
    // P2:Task point 琛ㄦ牸閫変腑琛屽彉鍖?鈫?鏇存柊 selected-only 鎸夐挳鐨?enabled 鐘舵€併€?
    connect (_taskPointTable->selectionModel (), &QItemSelectionModel::selectionChanged,
             this, [this] (const QItemSelection&, const QItemSelection&) {
                 updateTaskPointSelectionButtons ();
             });
    connect (_taskPointModel, &QAbstractItemModel::dataChanged,
             this, [this] () { refreshVisualization (); });
    connect (_taskPointModel, &QAbstractItemModel::modelReset,
             this, [this] () { refreshVisualization (); });
    connect (_taskPointModel, &QAbstractItemModel::rowsInserted,
             this, [this] () { refreshVisualization (); });
    connect (_taskPointModel, &QAbstractItemModel::rowsRemoved,
             this, [this] () { refreshVisualization (); });
    connect (_workspaceRunButton, SIGNAL (clicked ()), this, SLOT (sampleWorkspace ()));
    connect (_workspaceExportButton, SIGNAL (clicked ()), this, SLOT (exportWorkspaceCsv ()));
    connect (_poseAddRowButton, SIGNAL (clicked ()), this, SLOT (addPoseReachabilityRow ()));
    connect (_poseAnalyzeButton, SIGNAL (clicked ()), this, SLOT (analyzePoseReachability ()));
    connect (_poseExportButton, SIGNAL (clicked ()), this, SLOT (exportPoseReachabilityCsv ()));
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
    connect (_thresholdApplyButton, SIGNAL (clicked ()), this, SLOT (applyThresholds ()));
    connect (_visualSourceCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (refreshVisualization ()));
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
    // Task 4 step 5:鏃犻€変腑琛屾椂鏄剧ず"No IK candidate selected."銆?
    setIkDetailsEmpty ();
}

KinematicAnalysisWidget::~KinematicAnalysisWidget ()
{
    if (_poseReachabilityCancelRequested)
        _poseReachabilityCancelRequested->store (true);
    if (_poseReachabilityWatcher != NULL && _poseReachabilityWatcher->isRunning ())
        _poseReachabilityWatcher->waitForFinished ();
    if (_poseReachabilityRunActive)
        QApplication::restoreOverrideCursor ();
}

QSize KinematicAnalysisWidget::sizeHint () const
{
    return QSize (360, 620);
}

QSize KinematicAnalysisWidget::minimumSizeHint () const
{
    return QSize (300, 420);
}

// setRobWorkStudio:鐢?KinematicAnalysisPlugin::initialize 璋冪敤,缂撳瓨涓荤▼搴忓彞鏌?
// 鐢ㄥ畠鑾峰彇褰撳墠 state銆佸啓鍥?IK 瑙ｃ€?
void KinematicAnalysisWidget::setRobWorkStudio(RobWorkStudio* studio)
{
    _studio = studio;
}

// setWorkCell:WorkCell 鍙樺寲鏃惰皟鐢?鍒锋柊璁惧/甯т笅鎷?骞舵彁绀虹敤鎴峰綋鍓嶇姸鎬併€?
void KinematicAnalysisWidget::setWorkCell(rw::models::WorkCell* workcell)
{
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
}

// populateDevices:鎶?WorkCell 涓殑 Device 鍏ㄩ儴濉繘 _deviceCombo銆?
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

// populateTcpFrames:鐢?Kinematics::findAllFrames 鏀堕泦 WorkCell 涓墍鏈夊抚,
// 鎻愪緵缁欑敤鎴烽€変綔 TCP銆傝繖浼氭妸鎵€鏈夎緟鍔?/ 宸ュ叿 / 鏈甯ч兘鍒楀嚭銆?
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

// currentState:浼樺厛杩斿洖 RobWorkStudio 鐨勫綋鍓?state;鍚﹀垯鐢?WorkCell 榛樿 state;
// 閮戒笉鍙敤鏃惰繑鍥炰竴涓┖ State(渚涘垎鏋愬櫒鍋氱┖鎸囬拡 / 绌虹姸鎬佸垎鏀?銆?
rw::kinematics::State KinematicAnalysisWidget::currentState () const
{
    if (_studio != NULL)
        return _studio->getState ();
    if (_workcell != NULL)
        return _workcell->getDefaultState ();
    return rw::kinematics::State ();
}

// setStatus:鐘舵€佹爮鐨勭畝鍗?setter,NULL 妫€鏌ラ伩鍏嶆瀽鏋勬湡宕╂簝銆?
void KinematicAnalysisWidget::setStatus (const QString& message)
{
    if (_status != NULL)
        _status->setText(message);
}

// refreshIkSolutionView:鎶?_lastIkResult 鍐欏叆 _ikSolutionTable,
// 鎸夎繃婊ゅ櫒杩囨护,姣忚鐨勫師濮?solutionIndex 閫氳繃 storeIkSolutionIndex
// 瀛樺埌 Qt::UserRole + 1銆傛湯灏惧埛鏂伴《閮ㄨ鏁?summary + 璇︽儏琛ㄣ€?
void KinematicAnalysisWidget::refreshIkSolutionView ()
{
    if (_ikSolutionTable == NULL)
        return;

    // Task 3:杩囨护鍣ㄤ簰鏂ャ€傚嬀 Show usable only 鏃跺己鍒跺彇娑?Show failed candidates
    // 骞剁鐢?閬垮厤涓や釜杩囨护鍣ㄨ涔夊啿绐併€俀SignalBlocker 闃叉 setChecked(false)
    // 鍙嶅悜瑙﹀彂鑷韩 stateChanged 妲?閫犳垚閫掑綊銆?
    if (_ikShowUsableOnlyCheck != NULL && _ikShowFailedCandidatesCheck != NULL) {
        const bool usableOnly = _ikShowUsableOnlyCheck->isChecked ();
        _ikShowFailedCandidatesCheck->setEnabled (!usableOnly);
        if (usableOnly && _ikShowFailedCandidatesCheck->isChecked ()) {
            QSignalBlocker blocker (_ikShowFailedCandidatesCheck);
            _ikShowFailedCandidatesCheck->setChecked (false);
        }
    }

    // Task 4:鍒锋柊鍓嶈褰曞綋鍓嶉€変腑鐨?solutionIndex,杩囨护鍚庤嫢璇ヨВ浠嶅彲瑙?
    // 鍦ㄥ惊鐜湯灏鹃噸鏂伴€変腑,鑰屼笉鏄粯璁よ烦鍒扮 0 琛屻€?
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

        // Task 7:Status 鍒楁煋鑹层€侾ass 缁?/ Warning 姗?/ Fail 绾?
        // 鐢ㄦ埛鎵鏃朵竴鐪煎尯鍒嗗€欓€夎川閲忋€?
        QTableWidgetItem* statusItem =
            makeItem (QString::fromLatin1 (statusText (solution.status)));
        if (solution.status == AnalysisStatus::Pass)
            statusItem->setForeground (QColor (0, 120, 0));
        else if (solution.status == AnalysisStatus::Warning)
            statusItem->setForeground (QColor (180, 120, 0));
        else if (solution.status == AnalysisStatus::Fail)
            statusItem->setForeground (QColor (180, 0, 0));
        _ikSolutionTable->setItem (displayRow, 1, statusItem);

        // Task 6:Failure 鍒楀姞 tooltip,瀹屾暣鍘熷洜鏂囨湰(鍙兘鍚暟鍊艰瘉鎹?
        // 鍦?hover 鏃舵樉绀?涓嶅繀鎵撳紑妯悜婊氬姩銆?
        const QString failureText = ikFailureText (solution);
        QTableWidgetItem* failureItem = makeItem (failureText);
        failureItem->setToolTip (failureText);
        _ikSolutionTable->setItem (displayRow, 2, failureItem);

        _ikSolutionTable->setItem (displayRow, 3,
            makeItem (isCurrentIkSolution (solution) ? tr("Yes") : tr("No")));
        _ikSolutionTable->setItem (displayRow, 4,
            makeItem (solution.inCollision ? tr("Yes") : tr("No")));
        _ikSolutionTable->setItem (displayRow, 5, makeItem (solution.distanceToCurrentQ));
        _ikSolutionTable->setItem (displayRow, 6, makeItem (solution.minJointLimitMargin));
        _ikSolutionTable->setItem (displayRow, 7, makeItem (solution.manipulability));
        _ikSolutionTable->setItem (displayRow, 8,
            makeItem (std::isinf (solution.conditionNumber) ? tr("inf")
                                                            : QString::number (solution.conditionNumber)));
        _ikSolutionTable->setItem (displayRow, 9, makeItem (solution.positionErrorMeters));
        _ikSolutionTable->setItem (displayRow, 10, makeItem (solution.orientationErrorDeg));
        // 绗?11 鍒楁槸绾?Q,澶辫触鍘熷洜宸插垎鍒扮 2 鍒?浼犵┖ reasons 闃叉 makeQItem
        // 鎶婂師鍥犲瓧绗︿覆閲嶅鏄剧ず涓€娆°€俆ask 6:鍦?makeQItem 鍐呴儴缁?text 鍔?tooltip銆?
        _ikSolutionTable->setItem (displayRow, 11,
            makeQItem (solution.q, std::vector< KinematicFailureReason > ()));

        // 鏁磋鐨?solutionIndex 閮藉瓨鍒?Qt::UserRole + 1,閫変腑浠讳竴鍗曞厓鏍?
        // 閮借兘鍙嶆煡鍥炲師濮?solution銆?
        for (int column = 1; column < _ikSolutionTable->columnCount (); ++column)
            storeIkSolutionIndex (_ikSolutionTable->item (displayRow, column), solutionIndex);

        ++displayRow;
    }

    // Task 2:Displayed 鏄綋鍓嶈繃婊ゅ悗瀹為檯鏄剧ず鏁?
    // Raw / Unique / Pass / Warning / Fail 浠嶆槸鍏ㄩ噺缁熻,璇箟娓呮櫚涓嶆贩娣嗐€?
    if (_ikCountSummaryLabel != NULL) {
        const KinematicIkSummary summary = summarizeIkSolutions (_lastIkResult.solutions);
        _ikCountSummaryLabel->setText (
            tr("Displayed %1 | Raw %2 | Unique %3 | Usable %4 | Pass %5 | Warning %6 | Fail %7")
                .arg (displayRow)
                .arg (static_cast<int> (_lastIkResult.rawCandidateCount))
                .arg (static_cast<int> (_lastIkResult.solutions.size ()))
                .arg (static_cast<int> (summary.usableCount))
                .arg (static_cast<int> (summary.passCount))
                .arg (static_cast<int> (summary.warningCount))
                .arg (static_cast<int> (summary.failCount)));
    }

    // Task 4 缁?鑻ヨ繃婊ゅ悗鍘熼€変腑瑙ｆ秷澶?琚繃婊ゆ帀),rowToSelect == -1,
    // 鍥為€€鍒扮 0 琛?鏃犺鏃堕€€鍒扮┖鐘舵€?Apply 涔熶細琚?setIkDetailsEmpty 绂佺敤)銆?
    if (_ikSolutionTable->rowCount () > 0) {
        if (rowToSelect < 0)
            rowToSelect = 0;
        _ikSolutionTable->selectRow (rowToSelect);
    }
    else {
        setIkDetailsEmpty ();
    }

    // 鎸夐挳鍚敤/绂佺敤缁熶竴浜ょ粰 updateIkSolutionDetails / setIkDetailsEmpty銆?
    updateIkSolutionDetails ();
}

// updateIkSolutionDetails:鎶婂綋鍓嶉€変腑琛屽弽鏌?_lastIkResult.solutions,
// 鍐?2 琛岃鎯?Summary(鐘舵€佺被)+ Metrics / Q(鏁板€肩被)銆?
// 浠讳竴缂哄け閮介€€鍥?setIkDetailsEmpty銆?
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

    // 鍚屾 Apply 鎸夐挳鍚敤鎬?鍙湁鏃犵鎾炪€侀潪 Fail 鐨勮В鍙啓鍥?RobWorkStudio銆?
    if (_ikApplyButton != NULL)
        _ikApplyButton->setEnabled (isUsableIkSolution (s));

    // 绗?1 琛?鐘舵€佺被淇℃伅(鏍囩 / 甯冨皵)銆?
    const QString summaryText = QStringLiteral (
        "Status=%1; Failures=[%2]; Current Q=%3; Collision=%4")
        .arg (QString::fromLatin1 (statusText (s.status)))
        .arg (ikFailureText (s).isEmpty () ? QStringLiteral ("None")
                                            : ikFailureText (s))
        .arg (isCurrentIkSolution (s) ? QStringLiteral ("Yes") : QStringLiteral ("No"))
        .arg (s.inCollision ? QStringLiteral ("Yes") : QStringLiteral ("No"));

    // 绗?2 琛?鏁板€肩被 + Q 鍚戦噺銆?
    const QString condText = std::isinf (s.conditionNumber) ?
        QStringLiteral ("inf") : QString::number (s.conditionNumber, 'g', 6);
    const QString metricsText = QStringLiteral (
        "Distance=%1; Margin=%2; Manip=%3; Cond=%4; Pos err=%5 m; Ori err=%6掳; Q=[%7]")
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
    // 涓嶈皟鐢?resizeColumnsToContents,閬垮厤鍦?Stretch 妯″紡涓嬭瑕嗙洊;
    // 鍚屾椂淇濇寔 2 琛屽浐瀹氶珮搴︾敱 setCompactTableVisibleRows 閿佸畾銆?
}

// setIkDetailsEmpty:璇︽儏琛ㄥ帇鎴?1 琛屾彁绀?鐢ㄤ簬鏈€変腑鎴栭€変腑琛屾棤鏁堛€?
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
// configureAnalysisTable:鎶婂父鐢ㄧ殑琛ㄦ牸灞炴€ч泦涓湪涓€璧?閬垮厤鍦ㄥ澶勯噸澶嶈缃€?
void configureAnalysisTable (QTableWidget* table)
{
    table->setSelectionBehavior (QAbstractItemView::SelectRows);
    table->setAlternatingRowColors (true);
    table->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    table->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
    table->horizontalHeader ()->setSectionResizeMode (QHeaderView::Interactive);
    table->verticalHeader ()->setVisible (false);
}

// setCompactTableVisibleRows:鎶?QTableWidget 鐨勯珮搴﹀浐瀹氭垚"琛ㄥご + rows 琛?
// 鍐呭 + 杈规",骞跺叧闂瀭鐩存粴鍔ㄦ潯銆?
// 鐢ㄩ€?璁?1 琛岀殑鎽樿琛?/ N 琛岀殑鍏宠妭琛ㄥ湪 QVBoxLayout 閲屽彧鍗犺嚜宸遍渶瑕佺殑楂樺害,
// 涓嶅啀琚?layout 鎾戝ぇ鐣欑櫧;琛屾暟 > visible 鍖哄煙鏃跺彧鑳藉閮ㄦ帴绠?鏈伐鍏蜂粛鍏佽
// 鍚庣画鍗曠嫭寮€鍚粴鍔ㄦ潯)銆?
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

// makeItem:鏋勯€犲彧璇诲崟鍏冩牸;閲嶈浇 double 鐗堟湰鏂逛究鐩存帴鏀炬暟鍊笺€?
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

// statusText:AnalysisStatus 鈫?鍙瀛楃涓?涓?toString(KinematicFailureReason) 閰嶅銆?
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

// qVectorText:鎶婂叧鑺傚悜閲忔牸寮忓寲涓?"q0, q1, ..." 鐢ㄤ簬琛ㄦ牸/CSV 鏄剧ず銆?
QString qVectorText (const std::vector< double >& q)
{
    QStringList values;
    for (double value : q)
        values << QString::number(value, 'g', 8);
    return values.join(", ");
}

// failureReasonsText:鎶婂け璐ュ師鍥犳灇涓炬暟缁勬牸寮忓寲涓?", " 鍒嗛殧瀛楃涓层€?
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

// isUsableIkSolution:鍒ゅ畾璇?IK 瑙ｆ槸鍚﹀彲瀹夊叏鍐欏洖 RobWorkStudio,
// 澶嶇敤 refreshIkSolutionView / updateIkSolutionDetails 涓殑鍒ゅ畾,閬垮厤閲嶅銆?
// 涓嶅彲鐢ㄦ儏褰?纰版挒 / status == Fail銆?
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

// taskPointTypeText:TaskPointType 鈫?瀛楃涓?UI 鏄剧ず涓庡洖鍐欓兘鐢ㄣ€?
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

// makeQItem:鎶?IK 瑙ｇ殑 q + failureReasons 鎷兼垚涓€涓崟鍏冩牸,
// 骞舵妸 q 搴忓垪鍖栧埌 Qt::UserRole,Apply 鏃剁洿鎺ヨ鍙?閬垮厤鍐嶆瑙ｆ瀽瀛楃涓层€?
// Task 6:鍚屾鎶婂畬鏁存枃鏈啓鍏?tooltip,IK 涓昏〃 Q 鍒?鍙兘寰堥暱)鍦?hover 鏃?
// 鍙互鐪嬪畬鏁村唴瀹广€?
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

// deviceByName / frameByName:鎸夊悕绉板湪 WorkCell 涓煡鎵?鎵句笉鍒拌繑鍥?NULL銆?
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

// selectedDevice / selectedTcpFrame:鎶?涓嬫媺妗嗗綋鍓嶉€夐」"缈昏瘧鎴?RobWork 鎸囬拡銆?
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

// refreshCurrentPose:閲嶇疆鍥涗釜 Current pose 琛ㄦ牸涓庢枃鏈爣绛?鈫?璋冪敤
// KinematicAnalyzer::analyzeCurrentPose 鈫?鎶婄粨鏋滃～鍥?UI,鍚屾椂鏇存柊 _lastCurrentPose
// 骞跺埛鏂?Report tab 鐨勬眹鎬汇€?
void KinematicAnalysisWidget::refreshCurrentPose ()
{
    // 閲嶇疆鎵€鏈夐潰鏉夸负鍗犱綅鐘舵€併€?
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

    // ---- 1. 绱у噾鎽樿鏍?2 琛?6 鍒?+ 鍏抽敭鎸囨爣) ----
    if (_poseValueTable != NULL) {
        _poseValueTable->setItem (0, 0, makeItem (result.tcpPosition[0]));
        _poseValueTable->setItem (0, 1, makeItem (result.tcpPosition[1]));
        _poseValueTable->setItem (0, 2, makeItem (result.tcpPosition[2]));
        _poseValueTable->setItem (0, 3, makeItem (result.tcpRpyDeg[0]));
        _poseValueTable->setItem (0, 4, makeItem (result.tcpRpyDeg[1]));
        _poseValueTable->setItem (0, 5, makeItem (result.tcpRpyDeg[2]));
    }
    // 琛ㄥご楂樺害鍦ㄥ垵娆″竷灞€鍚庢墠浼氱ǔ瀹?refresh 闃舵鍐嶈皟涓€娆＄‘淇濈揣鍑戙€?
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

    // ---- 2. 鍏宠妭鐘舵€佸悎骞惰〃 ----
    if (_jointStatusTable != NULL) {
        const int n = static_cast< int > (result.q.size ());
        _jointStatusTable->setRowCount (n);
        const int marginCount = static_cast< int > (result.jointLimitMargins.size ());
        for (int i = 0; i < n; ++i) {
            // 鍏宠妭鍚?瓒呰繃 14 瀛楃鐢ㄤ腑闂寸渷鐣?瀹屾暣鍚嶅瓧杩?tooltip銆?
            QString jointName = QString::fromStdString (deviceName + "_" + std::to_string (i));
            if (jointName.size () > 14)
                jointName = jointName.left (6) + QStringLiteral ("...") +
                            jointName.right (7);
            QTableWidgetItem* nameItem = makeItem (jointName);
            nameItem->setToolTip (QString::fromStdString (deviceName + "_" + std::to_string (i)));
            _jointStatusTable->setItem (i, 0, nameItem);
            _jointStatusTable->setItem (i, 1, makeItem (result.q[static_cast< std::size_t > (i)]));

            // Limit margin 涓?Status銆?
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
            // 棰滆壊鏆楃ず:Pass=榛樿銆丯ear/Fail 鐢ㄧ矖浣撱€?
            if (statusText == QStringLiteral ("Fail"))
                statusItem->setForeground (QColor (200, 0, 0));
            else if (statusText == QStringLiteral ("Near"))
                statusItem->setForeground (QColor (200, 130, 0));
            _jointStatusTable->setItem (i, 3, statusItem);
        }
        // 琛屾暟绋冲畾鍚庨噸鏂板浐瀹氶珮搴?6 杞村畬鏁村彲瑙?DOF 杈冨鏃朵篃鍙崰瀹為檯琛屾暟銆?
        setCompactTableVisibleRows (_jointStatusTable, n);
    }

    // ---- 3. Jacobian 鍏ㄥ涓昏〃 ----
    if (_jacobianTable != NULL &&
        result.jacobianRows > 0 && result.jacobianCols > 0) {
        // 鍒楁暟(q 鏁?浼氬彉,鎵€浠ュ垪澶存瘡娆￠噸璁俱€?
        QStringList headers;
        for (int c = 0; c < result.jacobianCols; ++c)
            headers << tr("q%1").arg (c);
        _jacobianTable->setColumnCount (result.jacobianCols);
        _jacobianTable->setRowCount (result.jacobianRows);
        _jacobianTable->setHorizontalHeaderLabels (headers);
        // 琛屽ご:鍩虹嚎 6 琛?vx vy vz wx wy wz),澶氫簬 6 琛岀殑 Jacobian 涔熶細鑷姩鍑烘粴鍔ㄦ潯銆?
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
        // 6 琛屽強浠ヤ笅鏃惰 6 琛屽彲瑙?澶氫簬 6 琛屾墠鍏佽鍨傜洿婊氬姩銆?
        if (result.jacobianRows <= 6)
            _jacobianTable->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
        else
            _jacobianTable->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    }

    // ---- 4. Singular values:1 琛屽鍒?蟽 index 鍦ㄨ〃澶?----
    if (_singularTable != NULL) {
        const int singCount = static_cast< int > (result.singularValues.size ());
        QStringList headers;
        for (int i = 0; i < singCount; ++i)
            headers << tr("蟽%1").arg (i);
        if (singCount > 0)
            headers << tr("蟽min");
        _singularTable->setColumnCount (headers.size ());
        _singularTable->setRowCount (1);
        _singularTable->setHorizontalHeaderLabels (headers);

        for (int i = 0; i < singCount; ++i) {
            _singularTable->setItem (
                0, i,
                makeItem (result.singularValues[static_cast< std::size_t > (i)]));
        }
        // 蟽min 鍒?鍙栨渶灏忓€?濂囧紓鍊煎凡闄嶅簭,鏈€鍙充竴鍒楀氨鏄?min)
        if (singCount > 0) {
            const double sigmaMin = result.singularValues.back ();
            _singularTable->setItem (0, singCount, makeItem (sigmaMin));
        }
        // 琛ㄥご楂樺害鍒濇甯冨眬鍚庢墠绋冲畾,refresh 闃舵鍐嶅浐瀹氫竴娆°€?
        setCompactTableVisibleRows (_singularTable, 1);
    }

    // ---- 5. Warnings:榛樿 None,鏈夊憡璀︽椂灞曞紑 ----
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

// solveIk:浠?IK tab 璇诲彇鐩爣鐐?x/y/z + RPY),杞?TaskPoint 鍚庤皟 analyzeIk;
// 缁撴灉鎸?sortIkSolutionsForDisplay 宸叉帓濂?閫愭潯鍐欏叆琛ㄦ牸;鍚屾椂鎶婂け璐ュ師鍥犲垪鍦?
// "Q / failures" 涓€鏍忋€?
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
    // 绔嬪嵆娓呯┖璇︽儏骞剁鐢?Apply,淇濊瘉鎵€鏈夋彁鍓嶈繑鍥炶矾寰勯兘涓嶄細淇濈暀鏃ф暟鎹€?
    setIkDetailsEmpty ();
    if (_ikDuplicateQThresholdSpin != NULL)
        _thresholds.ikDuplicateQThreshold = _ikDuplicateQThresholdSpin->value ();

    // Task 8:杩涘叆鍒嗘瀽鍓嶇鐢?Solve + 鐘舵€佹爮鎻愮ず"Solving IK...";
    // 姣忎釜鎻愬墠杩斿洖 / 姝ｅ父缁撴潫閮借鎶婃寜閽仮澶?閬垮厤閬楃暀绂佺敤鐘舵€併€?
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
        _ikSummaryLabel->setText(tr("Candidates: no device"));
        setStatus(tr("Cannot solve IK: no valid device selected."));
        if (_ikSolveButton != NULL)
            _ikSolveButton->setEnabled (true);
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

    // 淇濆瓨鏈€杩戜竴娆″畬鏁寸粨鏋?_refreshIkSolutionView 涓?_updateIkSolutionDetails
    // 閮戒粠杩欓噷璇汇€傝〃鏍肩湡姝ｅ～鍏呬氦缁?refreshIkSolutionView 缁熶竴璐熻矗,
    // 杩欐牱杩囨护鍣ㄥ垏鎹㈡椂涓嶅繀鍐嶈皟 Solve,UI 鍗虫椂鍒锋柊銆?
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

    // 姝ｅ父璺緞鏀跺熬:鎭㈠ Solve 鎸夐挳銆?
    if (_ikSolveButton != NULL)
        _ikSolveButton->setEnabled (true);
}

// shouldShowIkSolution:IK 瑙ｈ繃婊ゅ櫒,缁勫悎涓や釜 QCheckBox:
//   1) "Show usable only" 鍕句笂 鈫?鍙繚鐣欐棤纰版挒 + status != Fail 鐨勮В;
//   2) 鍚﹀垯鑻?"Show failed candidates" 鏈嬀 鈫?闅愯棌 status == Fail 鐨勮瘖鏂В;
//   3) 鍏朵綑鎯呭喌閮藉睍绀?淇濈暀鎵€鏈夊€欓€夌敤浜庤瘖鏂€?
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

// applySelectedIkSolution:鎶婄敤鎴峰湪 IK 琛ㄦ牸閲岄€変腑鐨勯偅鏉¤В鍐欏洖褰撳墠 state:
//   1) 閫氳繃 Qt::UserRole 鍙栧嚭 QVariantList(鍐欏叆琛ㄦ牸鏃剁敱 makeQItem 缂撳瓨);
//   2) 鏍￠獙 DOF 缁村害;
//   3) device->setQ + studio->setState 鎶婃暣涓?state 鎺ㄥ洖 RobWorkStudio;
//   4) refreshCurrentPose 鏇存柊 Current pose tab 涓?Report tab銆?
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
    // 鍏堜粠绗?0 鍒楀彇鐪熷疄 solutionIndex,鍋氫竴娆″畬鏁存€?/ 鍙敤鎬ф牎楠?
    // 杩欐牱鍗充究鎸夐挳鐘舵€佽寮傚父瑙﹀彂,涔熶笉浼氭妸 Fail / collision 瑙ｅ啓鍥?RobWorkStudio銆?
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

    // 琛ㄦ媶鍒嗗悗 Q 鍦ㄧ 11 鍒?0-indexed)銆?
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
//  琛ㄦ牸鍒?id | name | type | x/y/z | roll/pitch/yaw | posTol | oriTol |
//         weight | result | reason銆傜 0 鍒楁槸 checkbox 琛ㄧず enabled銆?
//  鎸夐挳鍖?Add row / Remove / Import CSV / Export CSV / Analyze all銆?
// -------------------------------------------------------------------------
void KinematicAnalysisWidget::buildTaskPointTab ()
{
    QVBoxLayout* tpLayout = new QVBoxLayout(_taskPointTab);

    QGridLayout* buttonRow = new QGridLayout();
    _addTaskPointButton         = new QPushButton(tr("Add row"), _taskPointTab);
    _removeTaskPointButton      = new QPushButton(tr("Remove selected"), _taskPointTab);
    _importTaskPointsButton     = new QPushButton(tr("Import CSV"), _taskPointTab);
    _exportTaskPointsButton     = new QPushButton(tr("Export task CSV"), _taskPointTab);
    _exportTaskPointResultsButton = new QPushButton(tr("Export result CSV"), _taskPointTab);
    _analyzeAllTaskPointsButton = new QPushButton(tr("Analyze all"), _taskPointTab);
    // P2:Task points 涓撶敤鎸夐挳:灞€閮ㄥ垎鏋愩€佸鍏ュ綋鍓嶅Э鎬併€佸簲鐢?best Q銆佽烦鍒?IK銆?
    _analyzeSelectedTaskPointsButton = new QPushButton (tr("Analyze selected"), _taskPointTab);
    _importCurrentTcpTaskPointButton = new QPushButton (tr("Import current TCP"), _taskPointTab);
    _applySelectedTaskPointBestQButton = new QPushButton (tr("Apply best Q"), _taskPointTab);
    _openSelectedTaskPointInIkButton  = new QPushButton (tr("Open in IK tab"), _taskPointTab);
    // 杩?3 涓寜閽湪娌￠€変腑鏈夋晥浠诲姟鐐规椂鏃犲彲鐢ㄧ粨鏋?鍏堢鐢?
    // 閫変腑琛屽彉鍖?/ 琛ㄦ牸琛屾暟鍙樺寲鏃剁敱 paintResultStates 閲嶆柊鍐冲畾銆?
    _analyzeSelectedTaskPointsButton->setEnabled (false);
    _applySelectedTaskPointBestQButton->setEnabled (false);
    _openSelectedTaskPointInIkButton->setEnabled (false);
    buttonRow->addWidget (_addTaskPointButton, 0, 0);
    buttonRow->addWidget (_removeTaskPointButton, 0, 1);
    buttonRow->addWidget (_importTaskPointsButton, 0, 2);
    buttonRow->addWidget (_exportTaskPointsButton, 0, 3);
    buttonRow->addWidget (_exportTaskPointResultsButton, 0, 4);
    buttonRow->addWidget (_analyzeAllTaskPointsButton, 1, 0);
    buttonRow->addWidget (_analyzeSelectedTaskPointsButton, 1, 1);
    buttonRow->addWidget (_importCurrentTcpTaskPointButton, 1, 2);
    buttonRow->addWidget (_applySelectedTaskPointBestQButton, 1, 3);
    buttonRow->addWidget (_openSelectedTaskPointInIkButton, 1, 4);
    tpLayout->addLayout (buttonRow);

    // P3-A:鏁版嵁婧愮敤 TaskPointTableModel,view 鐢?QTableView銆?
    // model 鎸佹湁 19 鍒椾换鍔＄偣瀹氫箟 + 8 鍒?IK 缁撴灉 + 楠岃瘉鐘舵€?
    // view 鍙礋璐ｆ覆鏌撲笌 delegate 浜や簰銆?
    _taskPointModel = new rws::TaskPointTableModel (_taskPointTab);
    _taskPointTable = new QTableView (_taskPointTab);
    _taskPointTable->setModel (_taskPointModel);
    QStringList headers = rws::TaskPointTableModel::allHeaderTexts ();
    for (int i = 0; i < headers.size (); ++i)
        _taskPointTable->model ()->setHeaderData (i, Qt::Horizontal, headers[i], Qt::DisplayRole);
    _taskPointTable->setSelectionBehavior (QAbstractItemView::SelectRows);
    _taskPointTable->setSelectionMode (QAbstractItemView::SingleSelection);
    _taskPointTable->setAlternatingRowColors (true);
    _taskPointTable->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    _taskPointTable->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
    _taskPointTable->horizontalHeader ()->setSectionResizeMode (QHeaderView::Interactive);
    _taskPointTable->verticalHeader ()->setVisible (false);
    // model 鐨?flag 宸茬粡鎶?result 鍒楄鎴愬彧璇?delegate 鍗曠嫭瑁呫€?
    installTaskPointDelegates ();
    tpLayout->addWidget (_taskPointTable);

    _taskPointSummaryLabel = new QLabel (tr("Enabled: 0    Pass: 0    Warning: 0    Fail: 0    Reachable rate: -"),
                                          _taskPointTab);
    tpLayout->addWidget (_taskPointSummaryLabel);
    tpLayout->addStretch ();
}

// buildWorkspaceTab:Workspace 瀛愰〉甯冨眬銆傛帶浠跺寘鎷?
//   - 閲囨牱鏁?/ 缃戞牸姝ユ暟 / 妯″紡(Random/Grid) / 纰版挒妫€鏌?/ 鐫€鑹叉ā寮?
//   - Run / Export CSV 涓や釜鍔ㄤ綔;
//   - 缁撴灉琛?8 鍒?Index / Status / Collision / TCP x/y/z / Manipulability / Min margin;
//   - 椤堕儴 summary 鏄剧ず鏃犵鎾?/ Warning / Fail 璁℃暟涓庡钩鍧囧彲鎿嶄綔搴︺€?
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
    _workspaceSeedSpin = new QSpinBox (_workspaceTab);
    _workspaceSeedSpin->setRange (1, 2147483647);
    _workspaceSeedSpin->setValue (1);
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
    _workspaceOpenVisualizationButton =
        new QPushButton (tr("Open in Visualization"), _workspaceTab);
    _workspaceOpenVisualizationButton->setEnabled (false);

    controls->addWidget (new QLabel (tr("Samples:"), _workspaceTab), 0, 0);
    controls->addWidget (_workspaceSampleCountSpin, 0, 1);
    controls->addWidget (new QLabel (tr("Mode:"), _workspaceTab), 0, 2);
    controls->addWidget (_workspaceModeCombo, 0, 3);
    controls->addWidget (new QLabel (tr("Grid steps:"), _workspaceTab), 1, 0);
    controls->addWidget (_workspaceGridStepsSpin, 1, 1);
    controls->addWidget (_workspaceCollisionCheck, 1, 2);
    controls->addWidget (_workspaceColorModeCombo, 1, 3);
    controls->addWidget (new QLabel (tr("Seed:"), _workspaceTab), 2, 0);
    controls->addWidget (_workspaceSeedSpin, 2, 1);
    controls->addWidget (_workspaceRunButton, 3, 0);
    controls->addWidget (_workspaceExportButton, 3, 1);
    controls->addWidget (_workspaceOpenVisualizationButton, 3, 2);
    layout->addLayout (controls);

    _workspaceSummaryLabel = new QLabel (tr("Samples: 0    Collision-free: 0    Avg manipulability: -"),
                                         _workspaceTab);
    layout->addWidget (_workspaceSummaryLabel);

    // P4:显示 plan / theoretical / 截断提示,用户改 mode / sample count
    // 都能立即看到 Grid 模式会被截断。
    _workspaceDiagnosticsLabel = new QLabel (tr("Plan: -"), _workspaceTab);
    layout->addWidget (_workspaceDiagnosticsLabel);

    _workspaceTable = new QTableWidget (_workspaceTab);
    _workspaceTable->setColumnCount (9);
    _workspaceTable->setHorizontalHeaderLabels ({
        tr("Index"), tr("Status"), tr("Collision"), tr("TCP x"), tr("TCP y"), tr("TCP z"),
        tr("Manipulability"), tr("Condition"), tr("Min limit margin")
    });
    _workspaceTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    configureAnalysisTable (_workspaceTable);
    layout->addWidget (_workspaceTable);

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

// buildPoseReachabilityTab:浣嶅Э鍙揪鎬у瓙椤靛竷灞€銆?
//   - Source ComboBox:Task points / Manual rows(涓ょ鍙栦綅缃殑鏂瑰紡);
//   - Directions / Rolls:鐞冮潰鏂瑰悜鏁?/ 缁?Z 婊氬姩閲囨牱鏁?
//   - 鎵嬪姩浣嶇疆琛?+ Add row;
//   - 缁撴灉琛?8 鍒?Index / Status / x/y/z / Sampled / Reachable / Coverage;
//   - 椤堕儴 summary 鏄剧ず骞冲潎 coverage銆?
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
    _poseCancelButton = new QPushButton (tr("Cancel"), _poseReachTab);
    _poseCancelButton->setEnabled (false);

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
    controls->addWidget (_poseCancelButton, 2, 3);
    layout->addLayout (controls);

    _posePositionTable = new QTableWidget (_poseReachTab);
    _posePositionTable->setColumnCount (3);
    _posePositionTable->setHorizontalHeaderLabels ({tr("x"), tr("y"), tr("z")});
    configureAnalysisTable (_posePositionTable);
    layout->addWidget (new QLabel (tr("Manual positions"), _poseReachTab));
    layout->addWidget (_posePositionTable);

    _poseSummaryLabel = new QLabel (tr("Positions: 0    Average coverage: -"), _poseReachTab);
    layout->addWidget (_poseSummaryLabel);

    // P4:诊断标签显示 plan / per-position 方向 / 是否截断。
    _poseDiagnosticsLabel = new QLabel (
        tr("Plan: 0 IK target(s), 0 orientation(s) per position"),
        _poseReachTab);
    layout->addWidget (_poseDiagnosticsLabel);

    // P5:进度条,运行期间显示已完成的 IK target 数。
    _poseProgressBar = new QProgressBar (_poseReachTab);
    _poseProgressBar->setRange (0, 1);
    _poseProgressBar->setValue (0);
    _poseProgressBar->setTextVisible (false);
    _poseProgressLabel = new QLabel (tr("Progress: 0 / 0 IK target(s)"), _poseReachTab);
    layout->addWidget (_poseProgressBar);
    layout->addWidget (_poseProgressLabel);

    // P4:连接控件变化立即刷新 plan。
    connect (_poseSourceCombo, SIGNAL (currentIndexChanged (int)),
             this, SLOT (updatePoseReachabilityControls ()));
    connect (_poseDirectionSamplesSpin, SIGNAL (valueChanged (int)),
             this, SLOT (updatePoseReachabilityControls ()));
    connect (_poseRollSamplesSpin, SIGNAL (valueChanged (int)),
             this, SLOT (updatePoseReachabilityControls ()));
    connect (_posePositionTable, SIGNAL (itemChanged (QTableWidgetItem*)),
             this, SLOT (updatePoseReachabilityControls ()));
    updatePoseReachabilityControls ();

    _poseResultTable = new QTableWidget (_poseReachTab);
    _poseResultTable->setColumnCount (8);
    _poseResultTable->setHorizontalHeaderLabels ({
        tr("Index"), tr("Status"), tr("x"), tr("y"), tr("z"),
        tr("Sampled"), tr("Reachable"), tr("Coverage")
    });
    _poseResultTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    configureAnalysisTable (_poseResultTable);
    layout->addWidget (_poseResultTable);

    // P4:无数据时导出按钮禁用。
    _poseExportButton->setEnabled (false);
}

// buildReportTab:Report 瀛愰〉甯冨眬銆?
//   - 椤堕儴 summary 鏍囩:鏄剧ず褰撳墠 / 浠诲姟 / 宸ヤ綔绌洪棿 / 浣嶅Э鍙揪鎬х殑缁煎悎鐘舵€?
//   - 7 涓?DoubleSpinBox 璋冮槇鍊?nearLimit / cond warn / cond fail / sigma /
//     manipulability / pos tol / ori tol)+ Apply thresholds 鎸夐挳;
//   - Refresh / Export JSON / Export CSV 涓変釜鍔ㄤ綔鎸夐挳;
//   - 搴曢儴鍛婅琛?4 鍒?Severity / Code / Source / Message銆?
void KinematicAnalysisWidget::buildVisualizationTab ()
{
    QVBoxLayout* layout = new QVBoxLayout (_visualizationTab);

    QGridLayout* controls = new QGridLayout ();
    _visualSourceCombo = new QComboBox (_visualizationTab);
    _visualSourceCombo->addItem (tr("Task points"), 0);
    _visualSourceCombo->addItem (tr("Workspace"), 1);
    _visualSourceCombo->addItem (tr("Pose reachability"), 2);

    _visualProjectionCombo = new QComboBox (_visualizationTab);
    _visualProjectionCombo->addItem (tr("XY"), static_cast<int> (VisualProjection::XY));
    _visualProjectionCombo->addItem (tr("XZ"), static_cast<int> (VisualProjection::XZ));
    _visualProjectionCombo->addItem (tr("YZ"), static_cast<int> (VisualProjection::YZ));

    _visualColorModeCombo = new QComboBox (_visualizationTab);
    _visualColorModeCombo->addItem (tr("Status"), static_cast<int> (VisualScalarMode::Status));
    _visualColorModeCombo->addItem (tr("Manipulability"), static_cast<int> (VisualScalarMode::Manipulability));
    _visualColorModeCombo->addItem (tr("Condition"), static_cast<int> (VisualScalarMode::Condition));
    _visualColorModeCombo->addItem (tr("Min joint margin"), static_cast<int> (VisualScalarMode::MinJointMargin));
    _visualColorModeCombo->addItem (tr("Position error"), static_cast<int> (VisualScalarMode::PositionError));
    _visualColorModeCombo->addItem (tr("Orientation error"), static_cast<int> (VisualScalarMode::OrientationError));
    _visualColorModeCombo->addItem (tr("Collision"), static_cast<int> (VisualScalarMode::Collision));
    _visualColorModeCombo->addItem (tr("Coverage"), static_cast<int> (VisualScalarMode::Coverage));

    _visualShowPassCheck = new QCheckBox (tr("Pass"), _visualizationTab);
    _visualShowWarningCheck = new QCheckBox (tr("Warning"), _visualizationTab);
    _visualShowFailCheck = new QCheckBox (tr("Fail"), _visualizationTab);
    _visualShowLabelsCheck = new QCheckBox (tr("Labels"), _visualizationTab);
    _visualShowPassCheck->setChecked (true);
    _visualShowWarningCheck->setChecked (true);
    _visualShowFailCheck->setChecked (true);

    controls->addWidget (new QLabel (tr("Source:"), _visualizationTab), 0, 0);
    controls->addWidget (_visualSourceCombo, 0, 1);
    controls->addWidget (new QLabel (tr("Projection:"), _visualizationTab), 0, 2);
    controls->addWidget (_visualProjectionCombo, 0, 3);
    controls->addWidget (new QLabel (tr("Color:"), _visualizationTab), 0, 4);
    controls->addWidget (_visualColorModeCombo, 0, 5);
    controls->addWidget (_visualShowPassCheck, 1, 1);
    controls->addWidget (_visualShowWarningCheck, 1, 2);
    controls->addWidget (_visualShowFailCheck, 1, 3);
    controls->addWidget (_visualShowLabelsCheck, 1, 4);
    controls->setColumnStretch (6, 1);
    layout->addLayout (controls);

    _visualSummaryLabel = new QLabel (tr("Points: 0"), _visualizationTab);
    layout->addWidget (_visualSummaryLabel);

    _visualPlot = new KinematicAnalysisPlotWidget (_visualizationTab);
    layout->addWidget (_visualPlot, 1);
}

void KinematicAnalysisWidget::refreshVisualization ()
{
    if (_visualPlot == NULL || _visualSourceCombo == NULL ||
        _visualProjectionCombo == NULL || _visualColorModeCombo == NULL)
        return;

    const int source = _visualSourceCombo->currentData ().toInt ();
    const VisualProjection projection =
        static_cast< VisualProjection > (_visualProjectionCombo->currentData ().toInt ());
    const VisualScalarMode scalarMode =
        static_cast< VisualScalarMode > (_visualColorModeCombo->currentData ().toInt ());

    AnalysisVisualData data;
    if (source == 0) {
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
        data = visualDataFromTaskPointResults (rows, scalarMode);
    }
    else if (source == 1) {
        data = visualDataFromWorkspaceSamples (_workspaceSamples, scalarMode);
    }
    else {
        data = visualDataFromPoseReachabilitySamples (_poseReachabilitySamples, scalarMode);
    }

    _visualPlot->setProjection (projection);
    _visualPlot->setStatusFilters (
        _visualShowPassCheck == NULL || _visualShowPassCheck->isChecked (),
        _visualShowWarningCheck == NULL || _visualShowWarningCheck->isChecked (),
        _visualShowFailCheck == NULL || _visualShowFailCheck->isChecked ());
    _visualPlot->setShowLabels (_visualShowLabelsCheck != NULL &&
                                _visualShowLabelsCheck->isChecked ());
    _visualPlot->setVisualData (data);

    if (_visualSummaryLabel != NULL) {
        QString scalarRange = tr("no finite scalar");
        if (data.hasFiniteScalar) {
            scalarRange = tr("%1 .. %2")
                .arg (QString::number (data.scalarMin, 'g', 6))
                .arg (QString::number (data.scalarMax, 'g', 6));
        }
        _visualSummaryLabel->setText (
            tr("Points: %1    Projection: %2    Color: %3    Scalar range: %4")
                .arg (static_cast< int > (data.points.size ()))
                .arg (visualProjectionText (projection))
                .arg (visualScalarModeText (scalarMode))
                .arg (scalarRange));
    }
}

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

void KinematicAnalysisWidget::installTaskPointDelegates ()
{
    if (_taskPointTable == nullptr)
        return;

    // 鏀堕泦 refFrame / tcpFrame 鍊欓€?WORLD + device base + WorkCell 鍏ㄩ儴 frame +
    // 椤堕儴 TCP combo 褰撳墠鍊笺€俛ddUnique 鍐呴儴鍘婚噸,绌哄€艰烦杩囥€?
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
    // WorkCell 鍏ㄩ儴 frame 鍚嶅瓧 refFrame / tcpFrame 閮借兘鐢ㄣ€?
    const QStringList wcFrameNames = collectWorkCellFrameNames (_workcell);
    for (const QString& name : wcFrameNames) {
        addUnique (frameValues, frameSeen, name);
        addUnique (tcpValues, tcpSeen, name);
    }
    if (_tcpFrameCombo != nullptr)
        addUnique (tcpValues, tcpSeen, _tcpFrameCombo->currentText ());
    if (tcpValues.isEmpty ())
        addUnique (tcpValues, tcpSeen, QStringLiteral ("TCP"));

    // P3-A 宸ュ巶:鍐呴儴宸茬粡鐢?setItemDelegateForColumn,鐩存帴浼?view 鍗冲彲銆?
    rws::installTaskPointDelegates (_taskPointTable, frameValues, tcpValues);
}

// addTaskPointRow:P3-A 杩佺Щ鍒?model API銆?
// 鍦?model 鏈熬鎻掑叆涓€琛?榛樿鍊间笌 P2 涓€鑷?0 浣嶅Э / Generic / WORLD /
// 椤堕儴 TCP / 0.001 m posTol / 1.0 deg oriTol / 1.0 weight / enabled)銆?
void KinematicAnalysisWidget::addTaskPointRow ()
{
    if (_taskPointModel == nullptr)
        return;
    const int row = _taskPointModel->rowCount ();
    _taskPointModel->insertRows (row, 1);
    auto setStr = [this, row] (int col, const QString& s) {
        _taskPointModel->setData (_taskPointModel->index (row, col), s, Qt::EditRole);
    };
    setStr (ColId,    QString ("P%1").arg (row + 1));
    setStr (ColName,  QString ("Task %1").arg (row + 1));
    setStr (ColType,  QStringLiteral ("Generic"));
    // refFrame 榛樿 WORLD; tcpFrame 榛樿娌跨敤椤堕儴 TCP 涓嬫媺妗嗐€?
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

// removeSelectedTaskPointRow:P3-A 杩佺Щ鍒?model API銆?
void KinematicAnalysisWidget::removeSelectedTaskPointRow ()
{
    if (_taskPointTable == nullptr || _taskPointModel == nullptr)
        return;
    const QModelIndexList selected = _taskPointTable->selectionModel ()->selectedRows ();
    if (selected.isEmpty ()) {
        setStatus (tr ("No task point row selected."));
        return;
    }
    // 鍒犻櫎澶氫釜閫変腑琛屾椂,浠庡悗寰€鍓嶅垹閬垮厤涓嬫爣閿欎綅銆?
    QList< int > rows;
    for (const QModelIndex& idx : selected)
        rows.append (idx.row ());
    std::sort (rows.begin (), rows.end (), std::greater< int > ());
    for (int row : rows)
        _taskPointModel->removeRows (row, 1);
    setStatus (tr ("Removed %1 task point row(s).").arg (rows.size ()));
}

namespace {
// setCell / cellText:瀵煎叆瀵煎嚭鍦烘櫙涓嬪父鐢ㄧ殑 cell 鍐欏叆 / 璇诲彇甯姪鍑芥暟,
// 姣旂洿鎺?new QTableWidgetItem 绠€鐭€?
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

// Task 2 杈呭姪:鎶?鍘熷 solution 鍦?_lastIkResult 涓殑绱㈠紩"瀛樺埌 cell 鐨?
// Qt::UserRole + 1 妲戒腑,杩欐牱杩囨护鍚庤〃鏍肩殑 displayRow 涓?solutionIndex
// 涓嶅啀涓€鑷?鍚屼竴鏉?solution 鍙兘鍥犱负鍕鹃€変簡"鍙湅鍙敤瑙?琚烦杩?,
// 浣嗙敤鎴烽€変腑浠讳綍涓€琛屾椂浠嶈兘鍙嶆煡鍒板師濮嬬储寮曘€?
void storeIkSolutionIndex (QTableWidgetItem* item, int solutionIndex)
{
    if (item != NULL)
        item->setData (Qt::UserRole + 1, solutionIndex);
}

// Task 4 杈呭姪:鎶婅鎯呰〃鐨勪袱鍒?field/value)鍐欎竴琛?鐩存帴澶嶇敤 makeItem銆?
void setDetailRow (QTableWidget* table, int row, const QString& field, const QString& value)
{
    QTableWidgetItem* fieldItem = makeItem (field);
    QTableWidgetItem* valueItem = makeItem (value);
    // 缁欎袱鍒楅兘鍔?tooltip,鍏佽 hover 鏌ョ湅瀹屾暣闀挎枃鏈?灏ゅ叾鏄?
    // 鍚暱 Q 鍚戦噺鐨?Metrics/Q 琛?,涓嶅繀鎵撳紑姘村钩婊氬姩銆?
    fieldItem->setToolTip (field);
    valueItem->setToolTip (value);
    table->setItem (row, 0, fieldItem);
    table->setItem (row, 1, valueItem);
}

// P1 bestUsableSolution:涓烘瘡涓?task point 閫?浠ｈ〃瑙?灞曠ず鍦?bestQ / 璇樊鍒椼€?
//   - 浼樺厛绗竴鏉℃棤纰版挒 + (Pass 鎴?Warning) 鐨勮В;
//   - 鍏ㄩ儴 collision 鏃堕€€鍥炲埌绗竴鏉¤В(璇婃柇鐢?,璁╃敤鎴风湅鍒?IK 鐪熺殑瑙ｅ埌浜?
//   - 鏃犺В鏃惰繑鍥?nullptr,UI 鍐?"-"銆?
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

// P2 paintResultStates:鎸?status / 鏍￠獙缁撴灉缁欐暣琛屾煋鑹层€?
//   浼樺厛绾?楠岃瘉閿欒(娴呯孩) > Fail(娴呯孩) > Warning / Skipped(娴呴粍) > Pass(娴呯豢) > 榛樿銆?
// 鍚屾椂鎶?status / reason / failureReasons 鎷兼垚 tooltip 鏂逛究 hover 璇婃柇銆?
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

// readTaskPointFromRow:鎶婅〃鏍间竴琛?0-based row)瀹屾暣璇绘垚 TaskPoint銆?
// 浠讳綍瀛楁闈炴硶(鏁板€?/ freeRoll / 缂哄皯瀛楁)閮戒細鎶婇鏉￠敊璇啓鍏?*error,
// 璋冪敤鏂硅礋璐?abort(杩斿洖绌?TaskPoint,鍙互鐢?TaskPoint{} 鏍囪瘑)銆?
// 鐢ㄩ€?鍦?import 瀹屾垚鍚庡仛"瀹屾暣瀛楁绾?validation,鑰屼笉鏄彧鐪?id/name銆?
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
    // 绌?refFrame / tcpFrame 涓嶅湪姝ゅ濉粯璁ゅ€?鐢?RobotAnalysisValidation 鎷︽埅銆?
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

// P0-6 杈呭姪:鎶婅〃鏍间竴琛岀殑 reason 鍒椾笌 status 鍒楁爣绾?骞舵妸鎵€鏈夐敊璇嫾鎴?
// tooltip;reason 鏄剧ず绗竴鏉￠敊璇?code/message銆傚け璐ヨ鐢ㄦ祬绾㈣儗鏅€?
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
    // 缁欐暣琛屾墍鏈?cell 璁炬祬绾㈣儗鏅笌 tooltip,reason 鍒楅澶栨樉绀虹涓€鏉￠敊璇€?
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

// 娓呴櫎鏁村紶琛ㄦ牸鐨勭孩鑹茶儗鏅笌 tooltip(涓嬩竴娆″垎鏋愬墠 / 瀵煎叆鍚庤皟鐢?銆?
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

// collectTaskPointsFromTable:浠?Task point 琛ㄦ牸閫愯璇诲嚭 TaskPoint 缁撴瀯銆?
//   - 绗?0 鍒楁槸 checkbox 鍐冲畾 enabled;
//   - 绗?3 鍒楃殑 Type 瀛楃涓叉槧灏勫洖 TaskPointType 鏋氫妇;
//   - 鏁板€煎垪閫氳繃 toDouble 瑙ｆ瀽;
//   - refFrame / tcpFrame / freeRoll / note 绛夊瓧绗︿覆瀛楁鍘熸牱鍥炰紶,
//     淇濊瘉 CSV 鈫?UI 鈫?CSV 涓嶄涪瀛楁銆?
std::vector< TaskPoint > KinematicAnalysisWidget::collectTaskPointsFromTable (QString* error) const
{
    if (error != nullptr)
        error->clear ();
    if (_taskPointModel == nullptr)
        return std::vector< TaskPoint > ();
    return _taskPointModel->taskPoints (error);
}

// applyTaskPointResults:P3-A 杩佺Щ鍒?model API銆?
// 鎶?results 涓€娆℃€у啓鍥?model,model 鐨?dataChanged 淇″彿璁?view 鑷姩鍒锋柊;
// 涓嶅啀閫?cell setCell,涔熶笉蹇呮墜鍔ㄧ淮鎶?status / reason / result 鍒楃殑鍚屾銆?
// 鏌撹壊涓?tooltip 涔熺敱 model 鐨?BackgroundRole / ToolTipRole 璐熻矗銆?
void KinematicAnalysisWidget::applyTaskPointResults (
    const std::vector< TaskPointReachabilityResult >& results, double reachableRate)
{
    if (_taskPointModel == nullptr)
        return;

    // 1) 鍐欏洖 model;model 鍐呴儴瀵规瘡琛?hasResult / result 瀛楁璧嬪€笺€?
    _taskPointModel->setResults (results, reachableRate);

    // 2) 缁熻 pass / warning / fail / enabled,鏇存柊 summary 鏍囩銆?
    std::size_t passCount = 0, warnCount = 0, failCount = 0, enabledCount = 0;
    for (const TaskPointReachabilityResult& r : results) {
        if (!r.taskPoint.enabled)
            continue;
        ++enabledCount;
        if (r.status == AnalysisStatus::Pass)
            ++passCount;
        else if (r.status == AnalysisStatus::Warning)
            ++warnCount;
        else if (r.status == AnalysisStatus::Fail)
            ++failCount;
    }
    if (_taskPointSummaryLabel != nullptr) {
        _taskPointSummaryLabel->setText (QString (
            "Enabled: %1    Pass: %2    Warning: %3    Fail: %4    Reachable rate: %5")
            .arg (enabledCount).arg (passCount).arg (warnCount).arg (failCount)
            .arg (QString::number (reachableRate, 'f', 3)));
    }
    setTaskPointTableColumnWidths ();
    updateTaskPointSelectionButtons ();
    refreshVisualization ();
}

// importTaskPointsCsv:浠庣敤鎴烽€夋嫨鐨?CSV 鏂囦欢璇诲叆浠诲姟鐐广€?
//   - 鐢?RobotAnalysisCsv::taskPointsFromCsv 瑙ｆ瀽(鍏变韩 CSV 搴忓垪鍖?;
//   - 鍏堟竻绌鸿〃鏍?鍐嶉€愮偣閲嶅缓(瑕嗙洊寮忓鍏?;
//   - result/reason 鍒楀浐瀹氬～ "-",鐣欑粰鍚庣画 Analyze all 鍐欏洖銆?
// importTaskPointsCsv:P3-A 杩佺Щ鍒?model API銆?
// 鐩存帴 model->setRowsFromTaskPoints(points) 瑕嗙洊寮忓鍏?
// 楠岃瘉鐢?model 鍐呴儴璺?RobotAnalysisValidation,澶辫触琛屾爣娴呯孩銆?
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

// exportTaskPointsCsv:鎶婅〃鏍煎綋鍓嶅唴瀹瑰簭鍒楀寲涓?CSV 骞跺啓鍏ョ敤鎴锋寚瀹氭枃浠躲€?
// 鍐欐枃浠剁敤 QFile::write(const char*, qint64) 鍐欏嚭 std::string 鍘熷瀛楄妭銆?
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
    // 鍏堣窇 model 楠岃瘉,绌?frame / 璐?tolerance 绛夐兘琚嫤鎴€?
    QString validationSummary;
    if (!_taskPointModel->validateAll (&validationSummary)) {
        QMessageBox::warning (this, tr("Export validation"),
                              tr("Task points have validation errors:\n\n%1")
                                  .arg (validationSummary));
        setStatus (tr("Task point export blocked: validation errors."));
        setTaskPointTableColumnWidths ();
        return;
    }
    // 楠岃瘉閫氳繃鍚?浠?model 鍙栨墍鏈夎(瀹屾暣瀛楁)鍐欏埌 CSV銆?
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

// P1 exportTaskPointResultsCsv:瀵煎嚭鎵归噺 IK 缁撴灉 CSV銆?
// 鍖呭惈浠诲姟鐐瑰畾涔?id/name/enabled/refFrame/tcpFrame) + 鐘舵€?status/reason) +
// 8 涓粨鏋滄寚鏍?rawCandidates/usableSolutions/bestQ/posErr/oriErr/margin/condition/collision)銆?
// 涓嶈姹傝兘鍐嶆瀵煎叆 RobotAnalysisCore;瀹冮潰鍚戞姤鍛婅€屼笉鏄洖鍐欍€?
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

// analyzeAllTaskPoints:鎵归噺璺戜换鍔＄偣鐨?IK銆?
//   - 浠庤〃鏍艰鍑?TaskPoint 鍒楄〃;
//   - analyzeTaskPoints(姝ゅ浼?NULL,璺宠繃纰版挒妫€娴?閬垮厤渚濊禆澶栭儴 collider);
//   - 鎶婄粨鏋滃啓鍥?_lastTaskPointResults 骞跺埛鏂拌〃鏍?
//   - 璋冪敤 updateReportSummary 璁?Report tab 鍚屾鏈€鏂版暟鎹€?
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
    // P3-A 杩佺Щ:浠?model 鍙栨墍鏈夎(瀹屾暣瀛楁)銆?
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
    // P1:workcell-aware overload銆倀cpFrame 鏄《閮ㄩ粯璁?TCP,
    // 姣忚 taskPoint.tcpFrame 鐢?TaskPointResolver 浼樺厛浣跨敤銆?
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
//  P2:Task points 涓撶敤鎿嶄綔
// ============================================================================

// hasSelectedEnabledTaskPoint:P3-A 杩佺Щ:浠?QTableView + model 鎷块€変腑琛屻€?
// 0 琛?/ 閫?disabled 琛?/ 閫?Skipped 閮借涓轰笉鍙敤銆?
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
        // taskPointAt 瓒婄晫鎴栫┖琛?绛変环浜?disabled銆?
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

// updateTaskPointSelectionButtons:閫変腑琛屽彉鍖?/ 琛ㄦ牸琛屾暟鍙樺寲鏃跺惎鐢?/ 绂佺敤
// 3 涓?selected-only 鎸夐挳銆傛寜閽湪 selected 鏈夋晥鏃跺惎鐢?鍚﹀垯绂佺敤銆?
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
        // Apply best Q 杩樿姹?_taskPointModel->bestUsableSolutionForRow 鏈夎В銆?
        const bool canApply = enabled && row >= 0 &&
            _taskPointModel->bestUsableSolutionForRow (row) != nullptr;
        _applySelectedTaskPointBestQButton->setEnabled (canApply);
    }
    if (_openSelectedTaskPointInIkButton != nullptr)
        _openSelectedTaskPointInIkButton->setEnabled (enabled);
}

// analyzeSelectedTaskPoints:鍙垎鏋愰€変腑涓?enabled 鐨勮銆?
// disabled 琛岀粨鏋滄竻绌?Skipped),涓嶅奖鍝嶅叾浠栬;_lastTaskPointResults
// 鎸夎〃鏍艰鍙峰榻?selected 涔嬪淇濇寔涓婁竴杞粨鏋滄垨绌恒€?
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
    // P3-A 杩佺Щ:浠?view 鐨?selectionModel 鎷块€変腑琛?涓嶅啀渚濊禆 QTableWidget 鍐呴儴銆?
    const QModelIndexList selected = _taskPointTable->selectionModel ()->selectedRows ();
    if (selected.isEmpty ()) {
        setStatus (tr ("Cannot analyze task points: no row selected."));
        return;
    }
    // 鏁磋〃鍏堣窇涓€娆?validation,绌?frame / 璐?tolerance 绛夐兘鎷︽埅銆?
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
    QSet< int > selectedRows;
    for (const QModelIndex& idx : selected)
        selectedRows.insert (idx.row ());

    std::vector< int > rows;
    rows.reserve (static_cast< std::size_t > (selectedRows.size ()));
    for (int row : selectedRows) {
        if (row < 0 || row >= total)
            continue;
        rows.push_back (row);
        if (allPoints[static_cast<std::size_t> (row)].enabled)
            ++analyzed;
    }

    // 閲嶆柊璁＄畻鍙揪鐜?+ 搴旂敤缁撴灉 + 鏇存柊 summary銆?
    _lastTaskPointResults = analyzeSelectedTaskPointRows (
        analyzer, _workcell, device, tcpFrame, state, allPoints, rows,
        _lastTaskPointResults, collisionDetector);
    const double rate = analyzer.calculateReachableRate (_lastTaskPointResults);
    applyTaskPointResults (_lastTaskPointResults, rate);

    const QString collisionNote = collisionUnavailable ?
        tr (" Collision checking was unavailable.") : QString ();
    setStatus (tr ("Analyzed %1 selected task point(s).%2")
                  .arg (analyzed).arg (collisionNote));
    updateReportSummary ();
    updateTaskPointSelectionButtons ();
}

// importCurrentTcpAsTaskPoint:鎶婂綋鍓?RWS TCP 浣嶅Э鎻掑叆鏂拌 TaskPoint銆?
// refFrame 榛樿鐢?WORLD,tcpFrame 璺熼殢椤堕儴 TCP,鍏朵粬瀛楁鐢ㄩ槇鍊奸粯璁ゅ€笺€?
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
    // 澶嶇敤 IK 椤?importCurrentPoseToIk 鐨勪綅濮胯鍙栭€昏緫 + P2 TaskPointUiLogic銆?
    try {
        const rw::math::Transform3D<> baseTtcp =
            rw::kinematics::Kinematics::frameTframe (
                device->getBase (), tcpFrame.get (), currentState ());
        const std::string id =
            QString ("TP_%1").arg (_taskPointModel->rowCount () + 1, 3, 10, QChar ('0')).toStdString ();
        TaskPoint p = taskPointFromCurrentTcpPose (
            id, tcpFrame->getName (), device->getBase ()->getName (), baseTtcp, _thresholds);
        // P3-A 杩佺Щ:鐢?model->appendTaskPoint 涓€娆℃€ф彃鍏?+ 瑙﹀彂楠岃瘉銆?
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

// applySelectedTaskPointBestQ:P3-A 杩佺Щ鍒?model銆?
// 浠?_taskPointModel->bestUsableSolutionForRow 鎷?best Q,鐩存帴鐢?
// isUsableIkSolution 浜屾鏍￠獙,閬垮厤 _lastTaskPointResults 绱㈠紩閿欎綅銆?
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

// openSelectedTaskPointInIk:P3-A 杩佺Щ鍒?model銆?
// 閫変腑琛岀洿鎺ヤ粠 _taskPointModel->taskPointAt 鎷?TaskPoint,
// 閫氳繃 TaskPointResolver 瑙ｆ瀽涓?device-base 鐩爣,濉埌 IK 椤点€?
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
    if (_ikTargetNameEdit != nullptr)
        _ikTargetNameEdit->setText (QString::fromStdString (taskPoint.id));
    if (_tcpFrameCombo != nullptr &&
        !resolved.targetInDeviceBase.tcpFrame.empty ()) {
        const int idx = _tcpFrameCombo->findText (
            QString::fromStdString (resolved.targetInDeviceBase.tcpFrame));
        if (idx >= 0)
            _tcpFrameCombo->setCurrentIndex (idx);
    }
    if (_tabs != nullptr && _ikTab != nullptr)
        _tabs->setCurrentWidget (_ikTab);
    setStatus (tr ("Opened selected task point in IK tab (resolved to device base)."));
}

// collectPoseReachabilityPositions:鎸?Source 涓嬫媺閫夋嫨鏀堕泦浣嶇疆鍒楄〃:
//   - 0 鈫?Task points:浠?_taskPointModel 鍙栨墍鏈夎,鍙彇 enabled 鐨勪綅缃?
//   - 1 鈫?Manual rows:浠?_posePositionTable 閫愯璇诲嚭 xyz銆?
std::vector< std::array< double, 3 > >
KinematicAnalysisWidget::collectPoseReachabilityPositions (QString* error) const
{
    std::vector< std::array< double, 3 > > positions;
    if (error != nullptr)
        error->clear ();
    if (_poseSourceCombo != NULL && _poseSourceCombo->currentIndex () == 0) {
        // P3-A 杩佺Щ:浠?model 鍙栨墍鏈夎,璺宠繃瀵瑰簾寮冪殑 QTableWidget helper 璋冪敤銆?
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

// applyWorkspaceResults:鎶?WorkspaceSample 鏁扮粍鍐欏埌缁撴灉琛?
//   - 琛ㄦ牸鏈€澶氭樉绀哄墠 500 鏉?闃插崱椤?,浣嗕粛鎸夊叏閮ㄦ牱鏈粺璁?summary;
//   - summary 鍖呭惈鎬绘暟銆佹棤纰版挒 / Warning / Fail 璁℃暟涓庡钩鍧囧彲鎿嶄綔搴︺€?
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
        _workspaceTable->setItem (row, 7, makeItem (sample.conditionNumber));
        _workspaceTable->setItem (row, 8, makeItem (sample.minJointLimitMargin));
    }

    const double avgManip = samples.empty () ? 0.0 :
        manipulabilitySum / static_cast< double > (samples.size ());
    if (_workspaceSummaryLabel != NULL) {
        // P4:走 helper 出标准 summary,而不是自己循环。
        const rws::WorkspaceSummary summary =
            rws::summarizeWorkspaceSamples (samples);
        _workspaceSummaryLabel->setText (
            tr("Samples: %1    Shown: %2    Pass: %3    Warning: %4    Fail: %5    "
               "Collision-free: %6    Avg manip: %7    P10 manip: %8    Max cond: %9")
                .arg (static_cast< int > (summary.totalCount))
                .arg (rows)
                .arg (static_cast< int > (summary.passCount))
                .arg (static_cast< int > (summary.warningCount))
                .arg (static_cast< int > (summary.failCount))
                .arg (static_cast< int > (summary.collisionFreeCount))
                .arg (summary.hasManipulability
                          ? QString::number (summary.avgManipulability, 'g', 6)
                          : QStringLiteral ("-"))
                .arg (summary.hasManipulability
                          ? QString::number (summary.p10Manipulability, 'g', 6)
                          : QStringLiteral ("-"))
                .arg (summary.hasCondition
                          ? QString::number (summary.maxCondition, 'g', 6)
                          : QStringLiteral ("-")));
    }
    _workspaceTable->resizeColumnsToContents ();

    // P4:导出 / 可视化按钮在有数据时启用。
    if (_workspaceExportButton != NULL)
        _workspaceExportButton->setEnabled (!samples.empty ());
    if (_workspaceOpenVisualizationButton != NULL)
        _workspaceOpenVisualizationButton->setEnabled (!samples.empty ());
    refreshVisualization ();
}

// sampleWorkspace:浠庢帶浠惰 WorkspaceSamplingConfig 鈫?璋?analyzer 鈫?鍐欏洖琛ㄦ牸;
// 鍥哄畾 randomSeed=1 浠ヤ繚璇佺粨鏋滃彲澶嶇幇,渚夸簬鍥炲綊瀵规瘮銆?
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
    // P4:用 UI 上的 Seed,让同一种子重复 Run 结果可复现。
    config.randomSeed = _workspaceSeedSpin != NULL ?
        static_cast< unsigned int > (_workspaceSeedSpin->value ()) : 1u;

    KinematicAnalyzer analyzer;
    analyzer.setThresholds (_thresholds);
    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (config.checkCollision, &collisionUnavailable);

    // P4:RAII guard,在同步采样期间禁用 Run + 设忙光标,
    // 无论采样/构造/异常都保证恢复,避免 UI 卡死。
    struct SamplingGuard {
        QPushButton* btn;
        explicit SamplingGuard (QPushButton* b) : btn (b) {
            if (btn != nullptr) btn->setEnabled (false);
            QApplication::setOverrideCursor (Qt::WaitCursor);
        }
        ~SamplingGuard () {
            QApplication::restoreOverrideCursor ();
            if (btn != nullptr) btn->setEnabled (true);
        }
    };
    const SamplingGuard guard (_workspaceRunButton);
    _workspaceSamples = analyzer.sampleWorkspace (
        device, tcpFrame, currentState (), config, collisionDetector);

    applyWorkspaceResults (_workspaceSamples);
    const QString collisionNote = collisionUnavailable ?
        tr(" Collision checking was unavailable.") : QString ();
    setStatus (tr("Workspace sampling completed with %1 sample(s).%2")
                   .arg (static_cast< int > (_workspaceSamples.size ())).arg (collisionNote));
    updateReportSummary ();
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
    _workspaceGridStepsSpin->setEnabled (gridMode);

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
}

// openWorkspaceInVisualization:P4 把 Visualization 切到 Workspace source,
// 复用 Workspace color 模式,跳到 Visualization tab 并 refresh。
void KinematicAnalysisWidget::openWorkspaceInVisualization ()
{
    if (_visualSourceCombo != NULL)
        _visualSourceCombo->setCurrentIndex (1);
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
    if (_tabs != NULL && _visualizationTab != NULL)
        _tabs->setCurrentWidget (_visualizationTab);
    refreshVisualization ();
}

// exportWorkspaceCsv:鎶?_workspaceSamples 鍏ㄩ噺鍐欏嚭(鍚?q 瀛楃涓层€乀CP 浣嶇疆銆?
// manipulability / 鍏宠妭瑁曞害 / 鏉′欢鏁?/ 纰版挒 / 鐘舵€?銆?
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

// addPoseReachabilityRow:鍦ㄦ墜鍔ㄤ綅缃〃鏈熬杩藉姞涓€琛屽叏 0 鐨勪綅缃€?
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

// applyPoseReachabilityResults:鎶?PoseReachabilitySample 鍐欏埌 _poseResultTable,
// 鍚屾椂鍒锋柊椤堕儴 summary(Average coverage)銆?
void KinematicAnalysisWidget::applyPoseReachabilityResults (
    const std::vector< PoseReachabilitySample >& samples)
{
    if (_poseResultTable == NULL)
        return;
    _poseResultTable->setRowCount (static_cast< int > (samples.size ()));

    // P4:用 helper 算 summary,替代手动累加。
    const rws::PoseReachabilitySummary summary =
        rws::summarizePoseReachabilitySamples (samples);

    for (std::size_t i = 0; i < samples.size (); ++i) {
        const rws::PoseReachabilitySample& sample = samples[i];
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
    if (_poseSummaryLabel != NULL) {
        _poseSummaryLabel->setText (
            tr("Positions: %1    Pass: %2    Warning: %3    Fail: %4    "
               "Average coverage: %5    Min/Max: %6 / %7")
                .arg (static_cast< int > (summary.totalPositions))
                .arg (static_cast< int > (summary.passCount))
                .arg (static_cast< int > (summary.warningCount))
                .arg (static_cast< int > (summary.failCount))
                .arg (QString::number (summary.averageCoverage, 'f', 3))
                .arg (QString::number (summary.minCoverage, 'f', 3))
                .arg (QString::number (summary.maxCoverage, 'f', 3)));
    }
    _poseResultTable->resizeColumnsToContents ();

    // P4:有数据时启用导出按钮。
    if (_poseExportButton != NULL)
        _poseExportButton->setEnabled (!samples.empty ());
    refreshVisualization ();
}

// analyzePoseReachability:P4 改为 QtConcurrent::run 后台执行,
// 验证输入后启动异步 worker,跑完由 handlePoseReachabilityFinished 收尾。
void KinematicAnalysisWidget::analyzePoseReachability ()
{
    if (_poseReachabilityRunActive) {
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

    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (config.checkCollision, &collisionUnavailable);

    // P4:标记运行中,禁用 Run + 启用 Cancel,设忙光标。
    _poseReachabilityRunActive = true;
    if (_poseReachabilityCancelRequested)
        _poseReachabilityCancelRequested->store (false);

    // P5:重置进度条,显示计划 IK 目标数。
    {
        PoseReachabilityDiagnostics runDiag;
        const std::size_t plannedTargets =
            plannedPoseReachabilityTargetCount (config, positions.size (), &runDiag);
        if (_poseProgressBar != NULL) {
            _poseProgressBar->setRange (0, static_cast< int > (plannedTargets));
            _poseProgressBar->setValue (0);
        }
        if (_poseProgressLabel != NULL) {
            _poseProgressLabel->setText (
                tr("Progress: 0 / %1 IK target(s)")
                    .arg (static_cast< int > (plannedTargets)));
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
    };
    const std::shared_ptr< PoseRunContext > runContext =
        std::make_shared< PoseRunContext > ();
    runContext->cancelFlag = _poseReachabilityCancelRequested;
    runContext->widget = this;

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
            "updatePoseReachabilityProgress",
            Qt::QueuedConnection,
            Q_ARG (qulonglong, static_cast< qulonglong > (completedTargets)),
            Q_ARG (qulonglong, static_cast< qulonglong > (plannedTargets)));
    };
    callbacks.userData = runContext.get ();

    // 捕获值而非指针,worker 不触及 widget 成员。
    const rw::kinematics::State runState = currentState ();
    const rw::core::Ptr< rw::models::Device > runDevice = device;
    const rw::core::Ptr< const rw::kinematics::Frame > runTcpFrame = selectedTcpFrame ();
    const KinematicThresholds runThresholds = _thresholds;

    QFuture< std::vector< PoseReachabilitySample > > future = QtConcurrent::run (
        [runDevice, runTcpFrame, runState, positions, config,
         collisionDetector, runThresholds, callbacks, runContext] () {
            KinematicAnalyzer worker;
            worker.setThresholds (runThresholds);
            return worker.analyzePoseReachability (
                runDevice, runTcpFrame, runState, positions, config,
                collisionDetector, callbacks);
        });
    _poseReachabilityWatcher->setFuture (future);
}

// updatePoseReachabilityProgress:P5 从后台线程通过 QMetaObject::invokeMethod
// 回调到 UI 线程,更新进度条和标签。
void KinematicAnalysisWidget::updatePoseReachabilityProgress (
    qulonglong completedTargets, qulonglong plannedTargets)
{
    const int planned = static_cast< int > (plannedTargets);
    const int completed = static_cast< int > (
        std::min< qulonglong > (completedTargets, plannedTargets));

    if (_poseProgressBar != NULL) {
        _poseProgressBar->setRange (0, planned);
        _poseProgressBar->setValue (completed);
    }
    if (_poseProgressLabel != NULL) {
        const double pct = plannedTargets == 0 ? 0.0 :
            100.0 * static_cast< double > (completedTargets) /
                static_cast< double > (plannedTargets);
        _poseProgressLabel->setText (
            tr("Progress: %1 / %2 IK target(s) (%3%)")
                .arg (static_cast< int > (completedTargets))
                .arg (static_cast< int > (plannedTargets))
                .arg (QString::number (pct, 'f', 1)));
    }
}

// handlePoseReachabilityFinished:P4 后台 worker 完成回调。
// 恢复 UI 状态,读结果,刷新表格 / report。
void KinematicAnalysisWidget::handlePoseReachabilityFinished ()
{
    QApplication::restoreOverrideCursor ();
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
    if (wasCanceled) {
        setStatus (tr("Pose reachability canceled after %1 position(s).")
                       .arg (static_cast< int > (_poseReachabilitySamples.size ())));
    }
    else {
        setStatus (tr("Pose reachability completed for %1 position(s).")
                       .arg (static_cast< int > (_poseReachabilitySamples.size ())));
    }
}

// exportPoseReachabilityCsv:鎶?_poseReachabilitySamples 鍐欎负 CSV,
// 鍒椾笌琛ㄦ牸涓€鑷?浣嶇疆 + sampled + reachable + coverage + status)銆?
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
            << "\n";
    }
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

// updateReportSummary:Report tab 鐨勪腑澶灑绾姐€?
//   - 鐢?analyzer.buildAggregateResult 鎶婂洓绫绘暟鎹仛鍚堟垚 KinematicAnalysisResult;
//   - 鍦?summary 鏍囩閲屾樉绀烘€荤姸鎬併€佸彲杈剧巼銆佸綋鍓嶆潯浠舵暟 / 鍙搷浣滃害銆佷换鍔＄偣璁℃暟銆?
//     宸ヤ綔绌洪棿鎬绘暟銆佷綅濮垮彲杈炬€у钩鍧?coverage;
//   - 鎶?result.warnings 鍏ㄩ儴鍐欏叆鍛婅琛ㄣ€?
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
           "Average pose coverage: %15")
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

// refreshReport:Report tab 涓婄殑 Refresh 鎸夐挳妲藉嚱鏁?
// 閲嶆柊璺戜竴娆?updateReportSummary,鏂逛究鐢ㄦ埛淇敼闃堝€煎悗鍙埛涓€娆￠潰鏉胯€屼笉閲嶈窇鍒嗘瀽銆?
void KinematicAnalysisWidget::refreshReport ()
{
    updateReportSummary ();
    setStatus (tr("Kinematic report refreshed."));
}

namespace {
// vectorToJsonArray / array3ToJsonArray:鎶?std::vector<double> 涓?std::array<double,3>
// 瑁呰繘 QJsonArray;渚?exportReportJson 浣跨敤銆?
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

// exportReportJson:鎶?KinematicAnalysisResult 搴忓垪鍖栦负缁撴瀯鍖?JSON,渚夸簬涓嬫父宸ュ叿娑堣垂銆?
// 椤跺眰鍖呭惈 pluginName / status / reachableRate;鍥涗釜瀛愯妭鐐瑰垎鍒负 currentPose銆?
// taskPointResults銆亀orkspaceSamples銆乸oseReachability;鏈熬鏄?warnings 鏁扮粍銆?
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
        item["type"] = QString::fromLatin1 (taskPointTypeText (task.taskPoint.type));
        item["enabled"] = task.taskPoint.enabled;
        item["refFrame"] = QString::fromStdString (task.taskPoint.refFrame);
        item["tcpFrame"] = QString::fromStdString (task.taskPoint.tcpFrame);
        item["status"] = QString::fromLatin1 (statusText (task.status));
        item["primaryFailure"] = QString::fromLatin1 (toString (task.primaryFailure));
        // failureReasons 瀹屾暣鏁扮粍,渚夸簬鑴氭湰浜屾杩囨护銆?
        QJsonArray reasonArray;
        for (KinematicFailureReason fr : task.failureReasons)
            reasonArray.append (QString::fromLatin1 (toString (fr)));
        item["failureReasons"] = reasonArray;
        item["rawCandidateCount"] = static_cast< int > (task.ik.rawCandidateCount);
        item["usableSolutionCount"] = static_cast< int > (task.ik.usableSolutionCount);
        // best solution:浠?bestUsableSolution 閫?鏃犺В鏃跺啓鍏?"-",
        // 閬垮厤涓嬫父鑴氭湰璁块棶 null 瀛楁鎶ラ敊銆?
        QJsonObject bestObj;
        const KinematicIkSolution* best = bestUsableSolution (task.ik);
        if (best != nullptr) {
            bestObj["q"] = vectorToJsonArray (best->q);
            bestObj["positionErrorMeters"] = best->positionErrorMeters;
            bestObj["orientationErrorDeg"] = best->orientationErrorDeg;
            bestObj["minJointLimitMargin"] = best->minJointLimitMargin;
            bestObj["conditionNumber"] =
                std::isinf (best->conditionNumber) ?
                    QJsonValue (QStringLiteral ("inf")) :
                    QJsonValue (best->conditionNumber);
            bestObj["manipulability"] = best->manipulability;
            bestObj["inCollision"] = best->inCollision;
        }
        else {
            bestObj["q"] = QJsonArray ();
            bestObj["positionErrorMeters"] = QJsonValue::Null;
            bestObj["orientationErrorDeg"] = QJsonValue::Null;
            bestObj["minJointLimitMargin"] = QJsonValue::Null;
            bestObj["conditionNumber"] = QJsonValue::Null;
            bestObj["manipulability"] = QJsonValue::Null;
            bestObj["inCollision"] = QJsonValue::Null;
        }
        item["bestSolution"] = bestObj;
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

// exportReportCsv:杈撳嚭 "metric,value" 涓ゅ垪鐨勭簿绠€鎽樿 CSV銆?
//   - 椤跺眰鐘舵€?/ 鍙揪鐜?/ 褰撳墠浣嶅Э鍏抽敭鎸囨爣 / 鍚勫瓙缁撴灉鏉℃暟;
//   - 鍙搷浣滃害 min/max/mean/p10 涓€骞惰拷鍔?鏉ヨ嚜 result.manipulabilityMap)銆?
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

// applyThresholds:鎶?Report tab 涓婄殑 7 涓槇鍊?SpinBox 鍐欏洖 _thresholds,
// 浠呬慨鏀瑰唴瀛樹腑鐨勯槇鍊?涓嶄細涓诲姩閲嶈窇浠讳綍鍒嗘瀽,鎻愮ず鐢ㄦ埛鎸夐渶閲嶈窇銆?
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
