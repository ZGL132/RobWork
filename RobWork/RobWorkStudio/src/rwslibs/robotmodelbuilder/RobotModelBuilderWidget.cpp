#include "RobotModelBuilderWidget.hpp"

#include "RobotModelXmlWriter.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

using namespace rws;

namespace {
const int JointCount = 6;

QTableWidget* makeTable (const QStringList& headers, int rows)
{
    QTableWidget* table = new QTableWidget (rows, headers.size ());
    table->setHorizontalHeaderLabels (headers);
    table->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    table->verticalHeader ()->setVisible (false);
    table->setAlternatingRowColors (true);
    return table;
}

bool parseDouble (const QString& text)
{
    bool ok = false;
    text.trimmed ().toDouble (&ok);
    return ok;
}

bool parseVector (const QString& text)
{
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    if (parts.size () != 3)
        return false;
    for (const QString& part : parts) {
        if (!parseDouble (part))
            return false;
    }
    return true;
}
}    // namespace

RobotModelBuilderWidget::RobotModelBuilderWidget (QWidget* parent) : QWidget (parent)
{
    buildUi ();
    resetDefaults ();
}

void RobotModelBuilderWidget::buildUi ()
{
    QVBoxLayout* root = new QVBoxLayout (this);

    QFormLayout* form = new QFormLayout ();
    _robotName        = new QLineEdit ();

    QWidget* saveWidget      = new QWidget ();
    QHBoxLayout* saveLayout  = new QHBoxLayout (saveWidget);
    _saveDirectory           = new QLineEdit ();
    QPushButton* browse      = new QPushButton ("Browse");
    saveLayout->setContentsMargins (0, 0, 0, 0);
    saveLayout->addWidget (_saveDirectory);
    saveLayout->addWidget (browse);

    _mode = new QComboBox ();
    _mode->addItem ("Joint + RPY + Pos");
    _mode->addItem ("DH Parameters");

    QWidget* options        = new QWidget ();
    QHBoxLayout* optionLay  = new QHBoxLayout (options);
    _showFrameAxes          = new QCheckBox ("Show axes");
    _generateDrawables      = new QCheckBox ("Drawables");
    _generateScene          = new QCheckBox ("Scene file");
    optionLay->setContentsMargins (0, 0, 0, 0);
    optionLay->addWidget (_showFrameAxes);
    optionLay->addWidget (_generateDrawables);
    optionLay->addWidget (_generateScene);
    optionLay->addStretch ();

    form->addRow ("Robot name", _robotName);
    form->addRow ("Save directory", saveWidget);
    form->addRow ("Mode", _mode);
    form->addRow ("Options", options);
    root->addLayout (form);

    QTabWidget* tabs = new QTabWidget ();

    QWidget* kinematicsTab = new QWidget ();
    QVBoxLayout* kinLayout = new QVBoxLayout (kinematicsTab);
    _transformTable        = makeTable (
        QStringList () << "Joint"
                       << "Type"
                       << "RPY deg"
                       << "Pos m",
        JointCount);
    _dhTable = makeTable (
        QStringList () << "Joint"
                       << "alpha deg"
                       << "a m"
                       << "d m"
                       << "offset deg",
        JointCount);
    kinLayout->addWidget (_transformTable);
    kinLayout->addWidget (_dhTable);
    tabs->addTab (kinematicsTab, "Kinematics");

    _drawablesTable = makeTable (
        QStringList () << "Name"
                       << "RefFrame"
                       << "Shape"
                       << "Radius"
                       << "Length"
                       << "RPY deg"
                       << "Pos m"
                       << "RGB"
                       << "Collision",
        0);
    tabs->addTab (_drawablesTable, "Drawables");

    _limitsTable = makeTable (
        QStringList () << "Joint"
                       << "PosMin deg"
                       << "PosMax deg"
                       << "VelMax deg/s"
                       << "AccMax deg/s2",
        JointCount);
    tabs->addTab (_limitsTable, "Limits");

    QWidget* posesTab      = new QWidget ();
    QVBoxLayout* posesLay  = new QVBoxLayout (posesTab);
    _posesTable            = makeTable (
        QStringList () << "Name"
                       << "q1 deg"
                       << "q2 deg"
                       << "q3 deg"
                       << "q4 deg"
                       << "q5 deg"
                       << "q6 deg",
        0);
    QWidget* poseButtons      = new QWidget ();
    QHBoxLayout* poseBtnLay   = new QHBoxLayout (poseButtons);
    QPushButton* addPoseBtn   = new QPushButton ("Add Pose");
    QPushButton* delPoseBtn   = new QPushButton ("Remove Pose");
    poseBtnLay->setContentsMargins (0, 0, 0, 0);
    poseBtnLay->addWidget (addPoseBtn);
    poseBtnLay->addWidget (delPoseBtn);
    poseBtnLay->addStretch ();
    posesLay->addWidget (_posesTable);
    posesLay->addWidget (poseButtons);
    tabs->addTab (posesTab, "Poses");

    QWidget* previewTab      = new QWidget ();
    QVBoxLayout* previewLay  = new QVBoxLayout (previewTab);
    QTabWidget* previewTabs  = new QTabWidget ();
    _serialPreview           = new QTextEdit ();
    _scenePreview            = new QTextEdit ();
    _serialPreview->setReadOnly (true);
    _scenePreview->setReadOnly (true);
    previewTabs->addTab (_serialPreview, "SerialDevice XML");
    previewTabs->addTab (_scenePreview, "Scene XML");
    previewLay->addWidget (previewTabs);
    tabs->addTab (previewTab, "XML Preview");

    root->addWidget (tabs, 1);

    QWidget* buttons        = new QWidget ();
    QHBoxLayout* buttonLay  = new QHBoxLayout (buttons);
    QPushButton* previewBtn = new QPushButton ("Generate Preview");
    QPushButton* saveBtn    = new QPushButton ("Save XML");
    QPushButton* loadBtn    = new QPushButton ("Save and Load");
    QPushButton* resetBtn   = new QPushButton ("Reset to Default Six Axis");
    buttonLay->setContentsMargins (0, 0, 0, 0);
    buttonLay->addWidget (previewBtn);
    buttonLay->addWidget (saveBtn);
    buttonLay->addWidget (loadBtn);
    buttonLay->addStretch ();
    buttonLay->addWidget (resetBtn);
    root->addWidget (buttons);

    _status = new QLineEdit ();
    _status->setReadOnly (true);
    root->addWidget (_status);

    connect (browse, SIGNAL (clicked ()), this, SLOT (browseSaveDirectory ()));
    connect (_mode, SIGNAL (currentIndexChanged (int)), this, SLOT (modeChanged (int)));
    connect (previewBtn, SIGNAL (clicked ()), this, SLOT (generatePreview ()));
    connect (saveBtn, SIGNAL (clicked ()), this, SLOT (saveXml ()));
    connect (loadBtn, SIGNAL (clicked ()), this, SLOT (saveAndLoad ()));
    connect (resetBtn, SIGNAL (clicked ()), this, SLOT (resetDefaults ()));
    connect (addPoseBtn, SIGNAL (clicked ()), this, SLOT (addPose ()));
    connect (delPoseBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedPose ()));
}

void RobotModelBuilderWidget::resetDefaults ()
{
    fillFromSpec (RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::homePath ()));
    generatePreview ();
}

void RobotModelBuilderWidget::generatePreview ()
{
    QStringList errors;
    if (!validateTableInput (errors)) {
        showErrors (errors);
        return;
    }

    RobotModelSpec spec = collectSpec ();
    if (!RobotModelXmlWriter::validate (spec, errors)) {
        showErrors (errors);
        return;
    }

    _serialPreview->setPlainText (RobotModelXmlWriter::makeSerialDeviceXml (spec));
    _scenePreview->setPlainText (RobotModelXmlWriter::makeSceneXml (spec));
    setStatus ("Preview generated.");
}

void RobotModelBuilderWidget::saveXml ()
{
    QStringList errors;
    if (!validateTableInput (errors)) {
        showErrors (errors);
        return;
    }

    RobotModelSpec spec = collectSpec ();
    if (!RobotModelXmlWriter::saveFiles (spec, errors)) {
        showErrors (errors);
        return;
    }
    generatePreview ();
    setStatus ("XML files saved.");
}

void RobotModelBuilderWidget::saveAndLoad ()
{
    QStringList errors;
    if (!validateTableInput (errors)) {
        showErrors (errors);
        return;
    }

    RobotModelSpec spec = collectSpec ();
    if (!RobotModelXmlWriter::saveFiles (spec, errors)) {
        showErrors (errors);
        return;
    }
    generatePreview ();
    Q_EMIT loadSceneRequested (RobotModelXmlWriter::sceneFilePath (spec));
    setStatus ("XML files saved. Loading scene...");
}

void RobotModelBuilderWidget::browseSaveDirectory ()
{
    const QString dir =
        QFileDialog::getExistingDirectory (this, "Choose save directory", _saveDirectory->text ());
    if (!dir.isEmpty ())
        _saveDirectory->setText (dir);
}

void RobotModelBuilderWidget::modeChanged (int index)
{
    const bool dhMode = index == 1;
    _dhTable->setVisible (dhMode);
    _transformTable->setVisible (!dhMode);
}

void RobotModelBuilderWidget::addPose ()
{
    const int row = _posesTable->rowCount ();
    _posesTable->insertRow (row);
    setItem (_posesTable, row, 0, "Pose" + QString::number (row + 1));
    for (int i = 1; i <= JointCount; ++i)
        setItem (_posesTable, row, i, "0");
}

void RobotModelBuilderWidget::removeSelectedPose ()
{
    const int row = _posesTable->currentRow ();
    if (row >= 0 && _posesTable->rowCount () > 1)
        _posesTable->removeRow (row);
}

void RobotModelBuilderWidget::fillFromSpec (const RobotModelSpec& spec)
{
    _robotName->setText (QString::fromStdString (spec.robotName));
    _saveDirectory->setText (QString::fromStdString (spec.saveDirectory));
    _mode->setCurrentIndex (spec.mode == RobotModelMode::DH ? 1 : 0);
    _showFrameAxes->setChecked (spec.showFrameAxes);
    _generateDrawables->setChecked (spec.generateDrawables);
    _generateScene->setChecked (spec.generateScene);
    fillKinematicsTables (spec);
    fillDrawablesTable (spec);
    fillLimitsTable (spec);
    fillPosesTable (spec);
    modeChanged (_mode->currentIndex ());
}

RobotModelSpec RobotModelBuilderWidget::collectSpec () const
{
    RobotModelSpec spec;
    spec.robotName         = _robotName->text ().toStdString ();
    spec.saveDirectory     = _saveDirectory->text ().toStdString ();
    spec.mode              = _mode->currentIndex () == 1 ? RobotModelMode::DH : RobotModelMode::JointRPYPos;
    spec.showFrameAxes     = _showFrameAxes->isChecked ();
    spec.generateDrawables = _generateDrawables->isChecked ();
    spec.generateScene     = _generateScene->isChecked ();

    for (int row = 0; row < _dhTable->rowCount (); ++row) {
        DHJointSpec joint;
        joint.name      = itemText (_dhTable, row, 0).toStdString ();
        joint.alphaDeg  = itemDouble (_dhTable, row, 1);
        joint.a         = itemDouble (_dhTable, row, 2);
        joint.d         = itemDouble (_dhTable, row, 3);
        joint.offsetDeg = itemDouble (_dhTable, row, 4);
        spec.dhJoints.push_back (joint);
    }

    for (int row = 0; row < _transformTable->rowCount (); ++row) {
        JointTransformSpec joint;
        joint.name = itemText (_transformTable, row, 0).toStdString ();
        joint.type = itemText (_transformTable, row, 1).toStdString ();
        parseVector3 (itemText (_transformTable, row, 2), joint.rpyDeg);
        parseVector3 (itemText (_transformTable, row, 3), joint.pos);
        spec.transformJoints.push_back (joint);
    }

    for (int row = 0; row < _drawablesTable->rowCount (); ++row) {
        DrawableSpec drawable;
        drawable.name           = itemText (_drawablesTable, row, 0).toStdString ();
        drawable.refFrame       = itemText (_drawablesTable, row, 1).toStdString ();
        drawable.shape          = itemText (_drawablesTable, row, 2).toStdString ();
        drawable.radius         = itemDouble (_drawablesTable, row, 3);
        drawable.length         = itemDouble (_drawablesTable, row, 4);
        parseVector3 (itemText (_drawablesTable, row, 5), drawable.rpyDeg);
        parseVector3 (itemText (_drawablesTable, row, 6), drawable.pos);
        parseVector3 (itemText (_drawablesTable, row, 7), drawable.rgb);
        drawable.collisionModel =
            itemText (_drawablesTable, row, 8).compare ("Enabled", Qt::CaseInsensitive) == 0;
        spec.drawables.push_back (drawable);
    }

    for (int row = 0; row < _limitsTable->rowCount (); ++row) {
        JointLimitSpec limit;
        limit.jointName = itemText (_limitsTable, row, 0).toStdString ();
        limit.posMinDeg = itemDouble (_limitsTable, row, 1);
        limit.posMaxDeg = itemDouble (_limitsTable, row, 2);
        limit.velMaxDeg = itemDouble (_limitsTable, row, 3);
        limit.accMaxDeg = itemDouble (_limitsTable, row, 4);
        spec.limits.push_back (limit);
    }

    for (int row = 0; row < _posesTable->rowCount (); ++row) {
        PoseSpec pose;
        pose.name = itemText (_posesTable, row, 0).toStdString ();
        for (int i = 0; i < JointCount; ++i)
            pose.qDeg[i] = itemDouble (_posesTable, row, i + 1);
        spec.poses.push_back (pose);
    }

    return spec;
}

bool RobotModelBuilderWidget::validateTableInput (QStringList& errors) const
{
    errors.clear ();
    QTableWidget* numericTables[] = {_dhTable, _limitsTable, _posesTable};
    const int numericStartCols[]  = {1, 1, 1};
    for (int t = 0; t < 3; ++t) {
        QTableWidget* table = numericTables[t];
        for (int row = 0; row < table->rowCount (); ++row) {
            for (int col = numericStartCols[t]; col < table->columnCount (); ++col) {
                if (!parseDouble (itemText (table, row, col)))
                    errors << QString ("Invalid number at row %1 column %2.")
                                  .arg (row + 1)
                                  .arg (table->horizontalHeaderItem (col)->text ());
            }
        }
    }

    for (int row = 0; row < _transformTable->rowCount (); ++row) {
        if (!parseVector (itemText (_transformTable, row, 2)))
            errors << QString ("Invalid RPY vector at joint row %1.").arg (row + 1);
        if (!parseVector (itemText (_transformTable, row, 3)))
            errors << QString ("Invalid Pos vector at joint row %1.").arg (row + 1);
    }

    for (int row = 0; row < _drawablesTable->rowCount (); ++row) {
        if (!parseDouble (itemText (_drawablesTable, row, 3)))
            errors << QString ("Invalid drawable radius at row %1.").arg (row + 1);
        if (!parseDouble (itemText (_drawablesTable, row, 4)))
            errors << QString ("Invalid drawable length at row %1.").arg (row + 1);
        if (!parseVector (itemText (_drawablesTable, row, 5)))
            errors << QString ("Invalid drawable RPY vector at row %1.").arg (row + 1);
        if (!parseVector (itemText (_drawablesTable, row, 6)))
            errors << QString ("Invalid drawable Pos vector at row %1.").arg (row + 1);
        if (!parseVector (itemText (_drawablesTable, row, 7)))
            errors << QString ("Invalid drawable RGB vector at row %1.").arg (row + 1);
    }

    return errors.isEmpty ();
}

void RobotModelBuilderWidget::fillKinematicsTables (const RobotModelSpec& spec)
{
    for (int row = 0; row < JointCount; ++row) {
        const DHJointSpec& dh = spec.dhJoints[row];
        setItem (_dhTable, row, 0, QString::fromStdString (dh.name));
        setItem (_dhTable, row, 1, QString::number (dh.alphaDeg));
        setItem (_dhTable, row, 2, QString::number (dh.a));
        setItem (_dhTable, row, 3, QString::number (dh.d));
        setItem (_dhTable, row, 4, QString::number (dh.offsetDeg));

        const JointTransformSpec& joint = spec.transformJoints[row];
        setItem (_transformTable, row, 0, QString::fromStdString (joint.name));
        setItem (_transformTable, row, 1, QString::fromStdString (joint.type));
        setItem (_transformTable, row, 2, vectorText (joint.rpyDeg));
        setItem (_transformTable, row, 3, vectorText (joint.pos));
    }
}

void RobotModelBuilderWidget::fillDrawablesTable (const RobotModelSpec& spec)
{
    _drawablesTable->setRowCount (static_cast< int > (spec.drawables.size ()));
    for (int row = 0; row < _drawablesTable->rowCount (); ++row) {
        const DrawableSpec& drawable = spec.drawables[row];
        setItem (_drawablesTable, row, 0, QString::fromStdString (drawable.name));
        setItem (_drawablesTable, row, 1, QString::fromStdString (drawable.refFrame));
        setItem (_drawablesTable, row, 2, QString::fromStdString (drawable.shape));
        setItem (_drawablesTable, row, 3, QString::number (drawable.radius));
        setItem (_drawablesTable, row, 4, QString::number (drawable.length));
        setItem (_drawablesTable, row, 5, vectorText (drawable.rpyDeg));
        setItem (_drawablesTable, row, 6, vectorText (drawable.pos));
        setItem (_drawablesTable, row, 7, vectorText (drawable.rgb));
        setItem (_drawablesTable, row, 8, drawable.collisionModel ? "Enabled" : "Disabled");
    }
}

void RobotModelBuilderWidget::fillLimitsTable (const RobotModelSpec& spec)
{
    for (int row = 0; row < JointCount; ++row) {
        const JointLimitSpec& limit = spec.limits[row];
        setItem (_limitsTable, row, 0, QString::fromStdString (limit.jointName));
        setItem (_limitsTable, row, 1, QString::number (limit.posMinDeg));
        setItem (_limitsTable, row, 2, QString::number (limit.posMaxDeg));
        setItem (_limitsTable, row, 3, QString::number (limit.velMaxDeg));
        setItem (_limitsTable, row, 4, QString::number (limit.accMaxDeg));
    }
}

void RobotModelBuilderWidget::fillPosesTable (const RobotModelSpec& spec)
{
    _posesTable->setRowCount (static_cast< int > (spec.poses.size ()));
    for (int row = 0; row < _posesTable->rowCount (); ++row) {
        const PoseSpec& pose = spec.poses[row];
        setItem (_posesTable, row, 0, QString::fromStdString (pose.name));
        for (int i = 0; i < JointCount; ++i)
            setItem (_posesTable, row, i + 1, QString::number (pose.qDeg[i]));
    }
}

void RobotModelBuilderWidget::showErrors (const QStringList& errors)
{
    const QString message = errors.join ("\n");
    setStatus (errors.isEmpty () ? QString () : errors.first ());
    QMessageBox::warning (this, "RobotModelBuilder", message);
}

void RobotModelBuilderWidget::setStatus (const QString& message)
{
    _status->setText (message);
}

QString RobotModelBuilderWidget::itemText (const QTableWidget* table, int row, int column)
{
    const QTableWidgetItem* item = table->item (row, column);
    return item == NULL ? QString () : item->text ().trimmed ();
}

double RobotModelBuilderWidget::itemDouble (const QTableWidget* table, int row, int column)
{
    return itemText (table, row, column).toDouble ();
}

bool RobotModelBuilderWidget::parseVector3 (const QString& text, std::array< double, 3 >& values)
{
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    if (parts.size () != 3)
        return false;
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        values[i] = parts[i].toDouble (&ok);
        if (!ok)
            return false;
    }
    return true;
}

void RobotModelBuilderWidget::setItem (QTableWidget* table, int row, int column,
                                       const QString& value)
{
    table->setItem (row, column, new QTableWidgetItem (value));
}

QString RobotModelBuilderWidget::vectorText (const std::array< double, 3 >& values)
{
    return QString::number (values[0]) + " " + QString::number (values[1]) + " " +
           QString::number (values[2]);
}
