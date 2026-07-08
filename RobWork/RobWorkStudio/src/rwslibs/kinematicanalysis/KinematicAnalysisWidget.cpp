#include "KinematicAnalysisWidget.hpp"

#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalyzer.hpp"

#include <rwslibs/robotanalysiscore/RobotAnalysisCsv.hpp>

#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Q.hpp>
#include <rws/RobWorkStudio.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <limits>
#include <string>
#include <vector>

using namespace rws;

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
    _qTable(NULL),
    _jointMarginTable(NULL),
    _jacobianTable(NULL),
    _singularTable(NULL),
    _tcpPoseLabel(NULL),
    _conditionLabel(NULL),
    _manipulabilityLabel(NULL),
    _warningList(NULL),
    _ikTargetNameEdit(NULL),
    _ikXSpin(NULL),
    _ikYSpin(NULL),
    _ikZSpin(NULL),
    _ikRollSpin(NULL),
    _ikPitchSpin(NULL),
    _ikYawSpin(NULL),
    _ikSolveButton(NULL),
    _ikApplyButton(NULL),
    _ikSummaryLabel(NULL),
    _ikSolutionTable(NULL),
    _taskPointTable(NULL),
    _addTaskPointButton(NULL),
    _removeTaskPointButton(NULL),
    _importTaskPointsButton(NULL),
    _exportTaskPointsButton(NULL),
    _analyzeAllTaskPointsButton(NULL),
    _taskPointSummaryLabel(NULL)
{
    Q_UNUSED (parent);
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins (4, 4, 4, 4);

    _tabs = new QTabWidget(this);
    root->addWidget(_tabs);

    // -------------------- Current Pose Tab --------------------
    _currentPoseTab = new QWidget(this);
    QVBoxLayout* cpLayout = new QVBoxLayout(_currentPoseTab);

    QHBoxLayout* cpHeader = new QHBoxLayout();
    _deviceCombo = new QComboBox(_currentPoseTab);
    _tcpFrameCombo = new QComboBox(_currentPoseTab);
    _refreshCurrentPoseButton = new QPushButton(tr("Refresh"), _currentPoseTab);
    _deviceCombo->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _tcpFrameCombo->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _deviceCombo->setMinimumContentsLength (10);
    _tcpFrameCombo->setMinimumContentsLength (10);
    cpHeader->addWidget(new QLabel(tr("Device:"), _currentPoseTab));
    cpHeader->addWidget(_deviceCombo, 1);
    cpHeader->addWidget(new QLabel(tr("TCP frame:"), _currentPoseTab));
    cpHeader->addWidget(_tcpFrameCombo, 1);
    cpHeader->addWidget(_refreshCurrentPoseButton);
    cpHeader->addStretch();
    cpLayout->addLayout(cpHeader);

    _tcpPoseLabel = new QLabel(tr("TCP pose: -"), _currentPoseTab);
    _conditionLabel = new QLabel(tr("Condition: -"), _currentPoseTab);
    _manipulabilityLabel = new QLabel(tr("Manipulability: -"), _currentPoseTab);
    cpLayout->addWidget(_tcpPoseLabel);
    cpLayout->addWidget(_conditionLabel);
    cpLayout->addWidget(_manipulabilityLabel);

    auto makeTable = [this] () -> QTableWidget* {
        QTableWidget* t = new QTableWidget(this);
        t->horizontalHeader ()->setSectionResizeMode (QHeaderView::Interactive);
        t->verticalHeader ()->setVisible (false);
        t->setEditTriggers (QAbstractItemView::NoEditTriggers);
        t->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
        t->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
        return t;
    };

    _qTable             = makeTable ();
    _qTable->setColumnCount (2);
    _qTable->setHorizontalHeaderLabels ({tr("Joint"), tr("q")});
    cpLayout->addWidget (new QLabel(tr("Joint angles"), _currentPoseTab));
    cpLayout->addWidget (_qTable);

    _jointMarginTable   = makeTable ();
    _jointMarginTable->setColumnCount (2);
    _jointMarginTable->setHorizontalHeaderLabels ({tr("Joint"), tr("Margin")});
    cpLayout->addWidget (new QLabel(tr("Joint-limit margins"), _currentPoseTab));
    cpLayout->addWidget (_jointMarginTable);

    _jacobianTable      = makeTable ();
    _jacobianTable->setColumnCount (1);
    cpLayout->addWidget (new QLabel(tr("Jacobian"), _currentPoseTab));
    cpLayout->addWidget (_jacobianTable);

    _singularTable      = makeTable ();
    _singularTable->setColumnCount (2);
    _singularTable->setHorizontalHeaderLabels ({tr("Index"), tr("Singular value")});
    cpLayout->addWidget (new QLabel(tr("Singular values"), _currentPoseTab));
    cpLayout->addWidget (_singularTable);

    _warningList = new QListWidget(_currentPoseTab);
    cpLayout->addWidget (new QLabel(tr("Warnings"), _currentPoseTab));
    cpLayout->addWidget (_warningList);
    cpLayout->addStretch ();

    _tabs->addTab (_currentPoseTab, tr("Current pose"));

    // -------------------- IK Tab --------------------
    _ikTab         = new QWidget(this);
    QVBoxLayout* ikLayout = new QVBoxLayout(_ikTab);

    QHBoxLayout* ikNameRow = new QHBoxLayout();
    _ikTargetNameEdit = new QLineEdit(_ikTab);
    _ikTargetNameEdit->setText(tr("Target"));
    _ikSolveButton = new QPushButton(tr("Solve"), _ikTab);
    _ikApplyButton = new QPushButton(tr("Apply selected Q"), _ikTab);
    ikNameRow->addWidget(new QLabel(tr("Target:"), _ikTab));
    ikNameRow->addWidget(_ikTargetNameEdit, 1);
    ikNameRow->addWidget(_ikSolveButton);
    ikNameRow->addWidget(_ikApplyButton);
    ikLayout->addLayout(ikNameRow);

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

    _ikSummaryLabel = new QLabel(tr("Solutions: -"), _ikTab);
    ikLayout->addWidget(_ikSummaryLabel);

    _ikSolutionTable = makeTable();
    _ikSolutionTable->setColumnCount(10);
    _ikSolutionTable->setHorizontalHeaderLabels({
        tr("Index"), tr("Status"), tr("Collision"), tr("Distance"), tr("Min limit margin"),
        tr("Manipulability"), tr("Condition"), tr("Position error"), tr("Orientation error"),
        tr("Q / failures")
    });
    _ikSolutionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _ikSolutionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ikLayout->addWidget(_ikSolutionTable);

    // -------------------- Placeholder Tabs --------------------
    _taskPointTab  = new QWidget(this);
    _workspaceTab  = new QWidget(this);
    _poseReachTab  = new QWidget(this);
    _reportTab     = new QWidget(this);

    auto fillPlaceholder = [this] (QWidget* w, const QString& title) {
        QVBoxLayout* l = new QVBoxLayout(w);
        QLabel* lab    = new QLabel(title, w);
        lab->setAlignment (Qt::AlignCenter);
        l->addWidget (lab);
        l->addStretch ();
    };
    fillPlaceholder (_workspaceTab, tr("Workspace sampling - Task 11"));
    fillPlaceholder (_poseReachTab, tr("Pose reachability - Task 13"));
    fillPlaceholder (_reportTab,    tr("Report / export - Task 14"));

    buildTaskPointTab ();

    _tabs->addTab (_ikTab,         tr("IK"));
    _tabs->addTab (_taskPointTab,  tr("Task points"));
    _tabs->addTab (_workspaceTab,  tr("Workspace"));
    _tabs->addTab (_poseReachTab,  tr("Pose reachability"));
    _tabs->addTab (_reportTab,     tr("Report"));

    connect (_refreshCurrentPoseButton, SIGNAL (clicked ()), this, SLOT (refreshCurrentPose ()));
    connect (_ikSolveButton, SIGNAL (clicked ()), this, SLOT (solveIk ()));
    connect (_ikApplyButton, SIGNAL (clicked ()), this, SLOT (applySelectedIkSolution ()));
    connect (_addTaskPointButton, SIGNAL (clicked ()), this, SLOT (addTaskPointRow ()));
    connect (_removeTaskPointButton, SIGNAL (clicked ()), this, SLOT (removeSelectedTaskPointRow ()));
    connect (_importTaskPointsButton, SIGNAL (clicked ()), this, SLOT (importTaskPointsCsv ()));
    connect (_exportTaskPointsButton, SIGNAL (clicked ()), this, SLOT (exportTaskPointsCsv ()));
    connect (_analyzeAllTaskPointsButton, SIGNAL (clicked ()), this, SLOT (analyzeAllTaskPoints ()));
}

QSize KinematicAnalysisWidget::sizeHint () const
{
    return QSize (360, 620);
}

QSize KinematicAnalysisWidget::minimumSizeHint () const
{
    return QSize (300, 420);
}

void KinematicAnalysisWidget::setRobWorkStudio(RobWorkStudio* studio)
{
    _studio = studio;
}

void KinematicAnalysisWidget::setWorkCell(rw::models::WorkCell* workcell)
{
    _workcell = workcell;
    populateDevices ();
    populateTcpFrames ();
}

void KinematicAnalysisWidget::populateDevices ()
{
    _deviceCombo->clear ();
    if (_workcell == NULL)
        return;
    for (rw::core::Ptr< rw::models::Device > dev : _workcell->getDevices ()) {
        if (dev == NULL)
            continue;
        _deviceCombo->addItem (QString::fromStdString (dev->getName ()));
    }
    if (_deviceCombo->count () > 0)
        _deviceCombo->setCurrentIndex (0);
}

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

rw::kinematics::State KinematicAnalysisWidget::currentState () const
{
    if (_studio != NULL)
        return _studio->getState ();
    if (_workcell != NULL)
        return _workcell->getDefaultState ();
    return rw::kinematics::State ();
}

namespace {
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

QString qVectorText (const std::vector< double >& q)
{
    QStringList values;
    for (double value : q)
        values << QString::number(value, 'g', 8);
    return values.join(", ");
}

QString failureReasonsText (const std::vector< rws::KinematicFailureReason >& reasons)
{
    if (reasons.empty())
        return QString();

    QStringList values;
    for (rws::KinematicFailureReason reason : reasons)
        values << QString::fromLatin1(rws::toString(reason));
    return values.join(", ");
}

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

void KinematicAnalysisWidget::refreshCurrentPose ()
{
    _qTable->setRowCount (0);
    _jointMarginTable->setRowCount (0);
    _jacobianTable->setRowCount (0);
    _singularTable->setRowCount (0);
    _warningList->clear ();
    _tcpPoseLabel->setText (tr("TCP pose: -"));
    _conditionLabel->setText (tr("Condition: -"));
    _manipulabilityLabel->setText (tr("Manipulability: -"));

    if (_workcell == NULL)
        return;

    const std::string deviceName = _deviceCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::models::Device > device = deviceByName (_workcell, deviceName);
    if (device == NULL)
        return;

    const std::string tcpName = _tcpFrameCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = frameByName (_workcell, tcpName);
    rw::kinematics::State state = currentState ();

    KinematicAnalyzer analyzer;
    const KinematicCurrentPoseResult result = analyzer.analyzeCurrentPose (device, tcpFrame, state);

    // TCP pose label
    {
        const double x = result.tcpPosition[0];
        const double y = result.tcpPosition[1];
        const double z = result.tcpPosition[2];
        const double r = result.tcpRpyDeg[0];
        const double p = result.tcpRpyDeg[1];
        const double yaw = result.tcpRpyDeg[2];
        _tcpPoseLabel->setText (QString ("TCP pose: pos=(%1, %2, %3) m  rpy=(%4, %5, %6) deg")
                                     .arg (x).arg (y).arg (z).arg (r).arg (p).arg (yaw));
    }
    if (std::isinf (result.conditionNumber))
        _conditionLabel->setText ("Condition: inf");
    else
        _conditionLabel->setText (QString ("Condition: %1").arg (result.conditionNumber));
    _manipulabilityLabel->setText (QString ("Manipulability: %1").arg (result.manipulability));

    _qTable->setRowCount (static_cast< int > (result.q.size ()));
    for (std::size_t i = 0; i < result.q.size (); ++i) {
        _qTable->setItem (static_cast< int > (i), 0, makeItem (QString::fromStdString (deviceName + "_" + std::to_string (i))));
        _qTable->setItem (static_cast< int > (i), 1, makeItem (result.q[i]));
    }

    _jointMarginTable->setRowCount (static_cast< int > (result.jointLimitMargins.size ()));
    for (std::size_t i = 0; i < result.jointLimitMargins.size (); ++i) {
        _jointMarginTable->setItem (static_cast< int > (i), 0, makeItem (QString::fromStdString ("Joint " + std::to_string (i))));
        _jointMarginTable->setItem (static_cast< int > (i), 1, makeItem (result.jointLimitMargins[i]));
    }

    if (result.jacobianRows > 0 && result.jacobianCols > 0) {
        _jacobianTable->setColumnCount (result.jacobianCols);
        _jacobianTable->setRowCount (result.jacobianRows);
        QStringList headers;
        for (int c = 0; c < result.jacobianCols; ++c)
            headers << tr("col %1").arg (c);
        _jacobianTable->setHorizontalHeaderLabels (headers);
        for (int r = 0; r < result.jacobianRows; ++r) {
            for (int c = 0; c < result.jacobianCols; ++c) {
                const double v = result.jacobianRowMajor[static_cast< std::size_t > (r * result.jacobianCols + c)];
                _jacobianTable->setItem (r, c, makeItem (v));
            }
        }
    }

    const int singCount = static_cast< int > (result.singularValues.size ());
    _singularTable->setRowCount (singCount);
    for (int i = 0; i < singCount; ++i) {
        _singularTable->setItem (i, 0, makeItem (QString ("%1").arg (i)));
        _singularTable->setItem (i, 1, makeItem (result.singularValues[static_cast< std::size_t > (i)]));
    }

    for (const rws::AnalysisWarning& w : result.warnings) {
        _warningList->addItem (QString::fromStdString (
            std::string ("[") + statusText(w.severity) + "] " + w.code + ": " + w.message));
    }
}

void KinematicAnalysisWidget::solveIk ()
{
    _ikSolutionTable->setRowCount(0);
    _ikSummaryLabel->setText(tr("Solutions: -"));

    if (_workcell == NULL)
        return;

    const std::string deviceName = _deviceCombo->currentText().toStdString();
    rw::core::Ptr< rw::models::Device > device = deviceByName(_workcell, deviceName);
    if (device == NULL) {
        _ikSummaryLabel->setText(tr("Solutions: no device"));
        return;
    }

    const std::string tcpName = _tcpFrameCombo->currentText().toStdString();
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = frameByName(_workcell, tcpName);

    TaskPoint target;
    target.id = "ik_target";
    target.name = _ikTargetNameEdit->text().toStdString();
    target.tcpFrame = tcpName;
    target.position = {{_ikXSpin->value(), _ikYSpin->value(), _ikZSpin->value()}};
    target.rpyDeg = {{_ikRollSpin->value(), _ikPitchSpin->value(), _ikYawSpin->value()}};

    KinematicAnalyzer analyzer;
    const KinematicIkAnalysisResult result =
        analyzer.analyzeIk(device, tcpFrame, currentState(), target);

    _ikSolutionTable->setRowCount(static_cast<int>(result.solutions.size()));
    for (std::size_t i = 0; i < result.solutions.size(); ++i) {
        const KinematicIkSolution& solution = result.solutions[i];
        const int row = static_cast<int>(i);
        _ikSolutionTable->setItem(row, 0, makeItem(QString::number(row)));
        _ikSolutionTable->setItem(row, 1, makeItem(QString::fromLatin1(statusText(solution.status))));
        _ikSolutionTable->setItem(row, 2, makeItem(solution.inCollision ? tr("Yes") : tr("No")));
        _ikSolutionTable->setItem(row, 3, makeItem(solution.distanceToCurrentQ));
        _ikSolutionTable->setItem(row, 4, makeItem(solution.minJointLimitMargin));
        _ikSolutionTable->setItem(row, 5, makeItem(solution.manipulability));
        if (std::isinf(solution.conditionNumber))
            _ikSolutionTable->setItem(row, 6, makeItem(tr("inf")));
        else
            _ikSolutionTable->setItem(row, 6, makeItem(solution.conditionNumber));
        _ikSolutionTable->setItem(row, 7, makeItem(solution.positionErrorMeters));
        _ikSolutionTable->setItem(row, 8, makeItem(solution.orientationErrorDeg));
        _ikSolutionTable->setItem(row, 9, makeQItem(solution.q, solution.failureReasons));
    }

    _ikSummaryLabel->setText(
        tr("Solutions: %1, status: %2")
            .arg(static_cast<int>(result.solutions.size()))
            .arg(QString::fromLatin1(statusText(result.status))));
}

void KinematicAnalysisWidget::applySelectedIkSolution ()
{
    if (_workcell == NULL || _studio == NULL)
        return;

    const QList<QTableWidgetItem*> selected = _ikSolutionTable->selectedItems();
    if (selected.empty())
        return;

    const int row = selected.front()->row();
    QTableWidgetItem* qItem = _ikSolutionTable->item(row, 9);
    if (qItem == NULL)
        return;

    const QVariantList storedQ = qItem->data(Qt::UserRole).toList();
    if (storedQ.empty())
        return;

    rw::math::Q q(static_cast<std::size_t>(storedQ.size()));
    for (int i = 0; i < storedQ.size(); ++i)
        q(static_cast<std::size_t>(i)) = storedQ[i].toDouble();

    const std::string deviceName = _deviceCombo->currentText().toStdString();
    rw::core::Ptr< rw::models::Device > device = deviceByName(_workcell, deviceName);
    if (device == NULL || q.size() != device->getDOF())
        return;

    rw::kinematics::State state = currentState();
    device->setQ(q, state);
    _studio->setState(state);
    refreshCurrentPose();
}

// -------------------------------------------------------------------------
//  Task point tab
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
    _taskPointTable->horizontalHeader ()->setSectionResizeMode (QHeaderView::Interactive);
    _taskPointTable->verticalHeader ()->setVisible (false);
    tpLayout->addWidget (_taskPointTable);

    _taskPointSummaryLabel = new QLabel (tr("Enabled: 0    Pass: 0    Warning: 0    Fail: 0    Reachable rate: -"),
                                          _taskPointTab);
    tpLayout->addWidget (_taskPointSummaryLabel);
    tpLayout->addStretch ();
}

void KinematicAnalysisWidget::setTaskPointTableColumnWidths ()
{
    if (_taskPointTable == nullptr)
        return;
    _taskPointTable->resizeColumnsToContents ();
}

void KinematicAnalysisWidget::addTaskPointRow ()
{
    if (_taskPointTable == nullptr)
        return;
    const int row = _taskPointTable->rowCount ();
    _taskPointTable->insertRow (row);
    auto check = [this] (int r, int c) {
        QTableWidgetItem* item = new QTableWidgetItem ();
        item->setCheckState (Qt::Checked);
        item->setFlags (item->flags () | Qt::ItemIsUserCheckable);
        _taskPointTable->setItem (r, c, item);
    };
    check (row, 0);
    auto text = [this] (int r, int c, const QString& s) {
        QTableWidgetItem* item = new QTableWidgetItem (s);
        item->setFlags (item->flags () & ~Qt::ItemIsEditable);
        _taskPointTable->setItem (r, c, item);
    };
    text (row, 1,  QString ("P%1").arg (row + 1));
    text (row, 2,  QString ("Task %1").arg (row + 1));
    text (row, 3,  QString ("Generic"));
    text (row, 4,  QString ("0"));
    text (row, 5,  QString ("0"));
    text (row, 6,  QString ("0"));
    text (row, 7,  QString ("0"));
    text (row, 8,  QString ("0"));
    text (row, 9,  QString ("0"));
    text (row, 10, QString ("0.001"));
    text (row, 11, QString ("1.0"));
    text (row, 12, QString ("1.0"));
    text (row, 13, QString ("-"));
    text (row, 14, QString ("-"));
    setTaskPointTableColumnWidths ();
}

void KinematicAnalysisWidget::removeSelectedTaskPointRow ()
{
    if (_taskPointTable == nullptr)
        return;
    const int row = _taskPointTable->currentRow ();
    if (row >= 0)
        _taskPointTable->removeRow (row);
}

namespace {
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
}    // namespace

std::vector< TaskPoint > KinematicAnalysisWidget::collectTaskPointsFromTable () const
{
    std::vector< TaskPoint > points;
    if (_taskPointTable == nullptr)
        return points;
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
        p.position[0] = cellText (_taskPointTable, r, 4).toDouble ();
        p.position[1] = cellText (_taskPointTable, r, 5).toDouble ();
        p.position[2] = cellText (_taskPointTable, r, 6).toDouble ();
        p.rpyDeg[0]   = cellText (_taskPointTable, r, 7).toDouble ();
        p.rpyDeg[1]   = cellText (_taskPointTable, r, 8).toDouble ();
        p.rpyDeg[2]   = cellText (_taskPointTable, r, 9).toDouble ();
        p.tolerance.positionMeters = cellText (_taskPointTable, r, 10).toDouble ();
        p.tolerance.orientationDeg = cellText (_taskPointTable, r, 11).toDouble ();
        p.weight                   = cellText (_taskPointTable, r, 12).toDouble ();
        QTableWidgetItem* enabledItem = _taskPointTable->item (r, 0);
        p.enabled = enabledItem != nullptr && enabledItem->checkState () == Qt::Checked;
        points.push_back (p);
    }
    return points;
}

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

void KinematicAnalysisWidget::importTaskPointsCsv ()
{
    if (_taskPointTable == nullptr)
        return;
    const QString path = QFileDialog::getOpenFileName (
        this, tr("Import task points"), QString (),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ())
        return;
    QFile file (path);
    if (!file.open (QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Import error"),
                              tr("Could not open %1").arg (path));
        return;
    }
    const QString csv = QString::fromUtf8 (file.readAll ());
    file.close ();
    std::vector< TaskPoint > points;
    std::string err;
    if (!RobotAnalysisCsv::taskPointsFromCsv (csv.toStdString (), points, &err)) {
        QMessageBox::warning (this, tr("Import error"),
                              tr("CSV parse failed: %1").arg (QString::fromStdString (err)));
        return;
    }
    _taskPointTable->setRowCount (0);
    for (const TaskPoint& p : points) {
        const int row = _taskPointTable->rowCount ();
        _taskPointTable->insertRow (row);
        QTableWidgetItem* enabled = new QTableWidgetItem ();
        enabled->setCheckState (p.enabled ? Qt::Checked : Qt::Unchecked);
        enabled->setFlags (enabled->flags () | Qt::ItemIsUserCheckable);
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
}

void KinematicAnalysisWidget::exportTaskPointsCsv ()
{
    if (_taskPointTable == nullptr)
        return;
    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export task points"), QString ("task_points.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty ())
        return;
    const std::vector< TaskPoint > points = collectTaskPointsFromTable ();
    const std::string csv                 = RobotAnalysisCsv::taskPointsToCsv (points);
    QFile file (path);
    if (!file.open (QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning (this, tr("Export error"),
                              tr("Could not open %1 for writing").arg (path));
        return;
    }
    file.write (csv.c_str (), static_cast< qint64 > (csv.size ()));
    file.close ();
}

void KinematicAnalysisWidget::analyzeAllTaskPoints ()
{
    if (_workcell == nullptr)
        return;
    if (_deviceCombo == nullptr || _deviceCombo->count () == 0)
        return;
    const std::string deviceName = _deviceCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::models::Device > device = deviceByName (_workcell, deviceName);
    if (device == nullptr)
        return;
    const std::string tcpName = _tcpFrameCombo->currentText ().toStdString ();
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = frameByName (_workcell, tcpName);
    const rw::kinematics::State state            = currentState ();

    KinematicAnalyzer analyzer;
    const std::vector< TaskPoint > points                = collectTaskPointsFromTable ();
    const std::vector< TaskPointReachabilityResult > results =
        analyzer.analyzeTaskPoints (device, tcpFrame, state, points, NULL);
    const double rate = analyzer.calculateReachableRate (results);
    applyTaskPointResults (results, rate);
}
