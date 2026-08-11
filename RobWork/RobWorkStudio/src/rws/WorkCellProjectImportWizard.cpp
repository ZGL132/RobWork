#include "WorkCellProjectImportWizard.hpp"

#include "ProjectPathResolver.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDir>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {

class DropLineEdit : public QLineEdit
{
  public:
    explicit DropLineEdit (QWidget* parent = nullptr) : QLineEdit (parent)
    {
        setAcceptDrops (true);
    }

  protected:
    void dragEnterEvent (QDragEnterEvent* event) override
    {
        if (event->mimeData ()->hasUrls ())
            event->acceptProposedAction ();
    }

    void dropEvent (QDropEvent* event) override
    {
        const QList< QUrl > urls = event->mimeData ()->urls ();
        if (!urls.isEmpty ()) {
            setText (urls.front ().toLocalFile ());
            event->acceptProposedAction ();
        }
    }
};

void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

bool validProjectSettings (const rws::WorkCellProjectImportRequest& request, QString* error)
{
    const QString name = request.projectName.trimmed ();
    if (name.isEmpty () || request.projectName != name || name == QStringLiteral (".") ||
        name == QStringLiteral ("..") || QFileInfo (name).fileName () != name ||
        name.contains (QRegularExpression (QStringLiteral ("[<>:\"/\\\\|?*]")))) {
        setError (error, QStringLiteral ("Project name must be a single safe file name."));
        return false;
    }
    const QFileInfo locationInfo (request.location.trimmed ());
    if (!locationInfo.isAbsolute () || !locationInfo.isDir () || !locationInfo.isWritable ()) {
        setError (error, QStringLiteral ("Project location must be an existing writable directory."));
        return false;
    }
    if (request.projectFilePath ().size () > 240) {
        setError (error, QStringLiteral ("Calculated project path is too long."));
        return false;
    }
    if (!rws::ProjectPathResolver::validateRobWorkCompatiblePath (
            request.projectFilePath (), error))
        return false;
    if (QFileInfo::exists (request.projectFilePath ())) {
        setError (error, QStringLiteral ("The target project already exists and will not be overwritten."));
        return false;
    }
    return true;
}

QString workCellBaseName (const QString& path)
{
    QString fileName = QFileInfo (path).fileName ();
    if (fileName.endsWith (QStringLiteral (".wc.xml"), Qt::CaseInsensitive))
        fileName.chop (7);
    else
        fileName = QFileInfo (fileName).completeBaseName ();
    return fileName;
}

}    // namespace

namespace rws {

bool WorkCellProjectImportRequest::isValid (QString* error) const
{
    if (!validProjectSettings (*this, error))
        return false;
    WorkCellProjectImportInspection inspection;
    if (!WorkCellProjectImportInspector::inspect (sourcePath, inspection, error))
        return false;
    return WorkCellProjectImportInspector::validateSelection (
        inspection, options.targetDeviceName, options.tcpFrameName, error);
}

QString WorkCellProjectImportRequest::projectFilePath () const
{
    QString fileName = projectName.trimmed ();
    if (!fileName.endsWith (QStringLiteral (".rwproj"), Qt::CaseInsensitive))
        fileName += QStringLiteral (".rwproj");
    return QDir (location.trimmed ()).filePath (fileName);
}

WorkCellProjectImportWizard::WorkCellProjectImportWizard (const QString& initialLocation,
                                                          QWidget* parent) :
    QWizard (parent)
{
    setWindowTitle (tr ("Create Project from WorkCell"));
    setOption (QWizard::HaveHelpButton, false);

    QWizardPage* projectPage = new QWizardPage (this);
    projectPage->setTitle (tr ("Project Name and Location"));
    QFormLayout* projectForm = new QFormLayout (projectPage);
    _projectName = new QLineEdit (projectPage);
    _projectName->setObjectName (QStringLiteral ("projectName"));
    _location = new QLineEdit (initialLocation, projectPage);
    _location->setObjectName (QStringLiteral ("projectLocation"));
    QPushButton* browseLocation = new QPushButton (tr ("Browse..."), projectPage);
    QHBoxLayout* locationRow = new QHBoxLayout;
    locationRow->setContentsMargins (0, 0, 0, 0);
    locationRow->addWidget (_location);
    locationRow->addWidget (browseLocation);
    _calculatedPath = new QLabel (projectPage);
    _calculatedPath->setTextInteractionFlags (Qt::TextSelectableByMouse);
    _pathWarning = new QLabel (projectPage);
    _pathWarning->setStyleSheet (QStringLiteral ("color: #9a6700;"));
    _pathWarning->setWordWrap (true);
    projectForm->addRow (tr ("Project Name"), _projectName);
    projectForm->addRow (tr ("Location"), locationRow);
    projectForm->addRow (tr ("Calculated Path"), _calculatedPath);
    projectForm->addRow (QString (), _pathWarning);
    connect (browseLocation, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory (
            this, tr ("Project Location"), _location->text ());
        if (!directory.isEmpty ())
            _location->setText (directory);
    });
    addPage (projectPage);

    QWizardPage* sourcePage = new QWizardPage (this);
    sourcePage->setTitle (tr ("WorkCell Source and Preflight"));
    QFormLayout* sourceForm = new QFormLayout (sourcePage);
    _sourcePath = new DropLineEdit (sourcePage);
    _sourcePath->setObjectName (QStringLiteral ("workCellSourcePath"));
    QPushButton* browseSource = new QPushButton (tr ("Browse..."), sourcePage);
    QHBoxLayout* sourceRow = new QHBoxLayout;
    sourceRow->setContentsMargins (0, 0, 0, 0);
    sourceRow->addWidget (_sourcePath);
    sourceRow->addWidget (browseSource);
    _sourceSummary = new QLabel (sourcePage);
    _sourceSummary->setWordWrap (true);
    sourceForm->addRow (tr ("WorkCell File"), sourceRow);
    sourceForm->addRow (tr ("Preflight"), _sourceSummary);
    connect (browseSource, &QPushButton::clicked, this, [this] {
        const QString source = QFileDialog::getOpenFileName (
            this, tr ("WorkCell Source"), _location->text (),
            tr ("WorkCell Files (*.wc.xml *.wc *.xml);;All Files (*.*)"));
        if (!source.isEmpty ())
            _sourcePath->setText (source);
    });
    addPage (sourcePage);

    QWizardPage* companionPage = new QWizardPage (this);
    companionPage->setTitle (tr ("Companion XML Binding"));
    QVBoxLayout* companionLayout = new QVBoxLayout (companionPage);
    _companions = new QTreeWidget (companionPage);
    _companions->setObjectName (QStringLiteral ("workCellCompanions"));
    _companions->setHeaderLabels ({tr ("Type"), tr ("Path"), tr ("Include")});
    companionLayout->addWidget (_companions);
    QPushButton* rebindCompanion = new QPushButton (tr ("Add / Rebind..."), companionPage);
    QPushButton* removeCompanion = new QPushButton (tr ("Remove"), companionPage);
    QHBoxLayout* companionButtons = new QHBoxLayout;
    companionButtons->setContentsMargins (0, 0, 0, 0);
    companionButtons->addWidget (rebindCompanion);
    companionButtons->addWidget (removeCompanion);
    companionButtons->addStretch (1);
    companionLayout->addLayout (companionButtons);
    connect (rebindCompanion, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName (
            this, tr ("Companion WorkCell XML"), QFileInfo (_sourcePath->text ()).absolutePath (),
            tr ("XML Files (*.xml);;All Files (*.*)"));
        if (path.isEmpty ())
            return;
        QTreeWidgetItem* item = _companions->currentItem ();
        if (item == nullptr) {
            item = new QTreeWidgetItem (_companions);
            item->setFlags (item->flags () | Qt::ItemIsUserCheckable);
        }
        item->setText (0, tr ("manual companion"));
        item->setText (1, path);
        item->setData (1, Qt::UserRole, path);
        item->setCheckState (2, Qt::Checked);
    });
    connect (removeCompanion, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem* item = _companions->currentItem ();
        if (item != nullptr)
            delete _companions->takeTopLevelItem (_companions->indexOfTopLevelItem (item));
    });
    addPage (companionPage);

    QWizardPage* devicePage = new QWizardPage (this);
    devicePage->setTitle (tr ("Primary Robot and TCP"));
    QFormLayout* deviceForm = new QFormLayout (devicePage);
    _targetDevice = new QComboBox (devicePage);
    _targetDevice->setObjectName (QStringLiteral ("targetDevice"));
    _tcpFrame = new QComboBox (devicePage);
    _tcpFrame->setObjectName (QStringLiteral ("tcpFrame"));
    _deviceSummary = new QLabel (devicePage);
    _deviceSummary->setWordWrap (true);
    deviceForm->addRow (tr ("Target Device"), _targetDevice);
    deviceForm->addRow (tr ("TCP Frame"), _tcpFrame);
    deviceForm->addRow (tr ("Device Preview"), _deviceSummary);
    addPage (devicePage);

    QWizardPage* reviewPage = new QWizardPage (this);
    reviewPage->setTitle (tr ("Review and Commit"));
    QVBoxLayout* reviewLayout = new QVBoxLayout (reviewPage);
    _review = new QLabel (reviewPage);
    _review->setObjectName (QStringLiteral ("workCellImportReview"));
    _review->setWordWrap (true);
    _review->setTextInteractionFlags (Qt::TextSelectableByMouse);
    reviewLayout->addWidget (_review);
    reviewLayout->addStretch (1);
    addPage (reviewPage);

    connect (_projectName, &QLineEdit::textChanged, this, &WorkCellProjectImportWizard::updateValidationState);
    connect (_location, &QLineEdit::textChanged, this, &WorkCellProjectImportWizard::updateValidationState);
    connect (_sourcePath, &QLineEdit::textChanged, this, &WorkCellProjectImportWizard::inspectSource);
    connect (_targetDevice, &QComboBox::currentTextChanged, this,
             &WorkCellProjectImportWizard::rebuildTcpChoices);
    connect (_tcpFrame, &QComboBox::currentTextChanged, this,
             &WorkCellProjectImportWizard::updateValidationState);
    connect (_companions, &QTreeWidget::itemChanged, this,
             &WorkCellProjectImportWizard::updateReview);
    connect (this, &QWizard::currentIdChanged, this, [this] {
        updateValidationState ();
        updateReview ();
    });
    updateValidationState ();
}

WorkCellProjectImportRequest WorkCellProjectImportWizard::request () const
{
    WorkCellProjectImportRequest result;
    result.sourcePath = _sourcePath->text ().trimmed ();
    result.projectName = _projectName->text ();
    result.location = _location->text ();
    result.options.targetDeviceName = _targetDevice->currentData ().toString ();
    result.options.tcpFrameName = _tcpFrame->currentData ().toString ();
    for (int index = 0; index < _companions->topLevelItemCount (); ++index) {
        const QTreeWidgetItem* item = _companions->topLevelItem (index);
        if (item->checkState (2) == Qt::Checked)
            result.options.companionFiles << item->data (1, Qt::UserRole).toString ();
    }
    return result;
}

bool WorkCellProjectImportWizard::validateCurrentPage ()
{
    QString error;
    if (currentId () == 1 && !ensureInspection (&error)) {
        QMessageBox::warning (this, tr ("WorkCell Preflight"), error);
        return false;
    }
    if (currentId () == 3 && !WorkCellProjectImportInspector::validateSelection (
                              _inspection, _targetDevice->currentData ().toString (),
                              _tcpFrame->currentData ().toString (), &error)) {
        QMessageBox::warning (this, tr ("Robot Selection"), error);
        return false;
    }
    if (currentId () == 4 && !request ().isValid (&error)) {
        QMessageBox::warning (this, tr ("Import Settings"), error);
        return false;
    }
    return true;
}

void WorkCellProjectImportWizard::inspectSource ()
{
    _hasInspection = false;
    _inspection = WorkCellProjectImportInspection {};
    QString error;
    if (ensureInspection (&error)) {
        if (_projectName->text ().trimmed ().isEmpty ())
            _projectName->setText (workCellBaseName (_sourcePath->text ()));
        _sourceSummary->setText (tr ("%1 device(s), %2 frame(s), %3 companion file(s) detected.")
                                     .arg (_inspection.deviceCount)
                                     .arg (_inspection.frameCount)
                                     .arg (_inspection.companions.size ()));
        rebuildCompanionList ();
        _targetDevice->clear ();
        for (const WorkCellImportDeviceInfo& device : _inspection.devices)
            _targetDevice->addItem (device.name, device.name);
        rebuildTcpChoices ();
    }
    else if (!_sourcePath->text ().trimmed ().isEmpty ()) {
        _sourceSummary->setText (error);
        _companions->clear ();
        _targetDevice->clear ();
        _tcpFrame->clear ();
    }
    else {
        _sourceSummary->clear ();
    }
    updateValidationState ();
}

bool WorkCellProjectImportWizard::ensureInspection (QString* error)
{
    if (_hasInspection && QDir::cleanPath (_inspection.sourcePath) ==
                              QDir::cleanPath (_sourcePath->text ().trimmed ()))
        return true;
    if (!WorkCellProjectImportInspector::inspect (_sourcePath->text ().trimmed (), _inspection, error))
        return false;
    _hasInspection = true;
    return true;
}

void WorkCellProjectImportWizard::rebuildCompanionList ()
{
    _companions->clear ();
    for (const WorkCellCompanionFile& companion : _inspection.companions) {
        QTreeWidgetItem* item = new QTreeWidgetItem (
            _companions, {companion.kind, companion.path, QString ()});
        item->setData (1, Qt::UserRole, companion.path);
        item->setFlags (item->flags () | Qt::ItemIsUserCheckable);
        item->setCheckState (2, companion.selected ? Qt::Checked : Qt::Unchecked);
    }
}

void WorkCellProjectImportWizard::rebuildTcpChoices ()
{
    const QString deviceName = _targetDevice->currentData ().toString ();
    _tcpFrame->clear ();
    for (const WorkCellImportDeviceInfo& device : _inspection.devices) {
        if (device.name != deviceName)
            continue;
        for (const QString& frame : device.tcpFrameNames)
            _tcpFrame->addItem (frame, frame);
        const int endIndex = _tcpFrame->findData (device.endFrameName);
        _tcpFrame->setCurrentIndex (endIndex >= 0 ? endIndex : 0);
        _deviceSummary->setText (tr ("DOF: %1\nBase: %2\nEnd: %3")
                                     .arg (device.dof)
                                     .arg (device.baseFrameName)
                                     .arg (device.endFrameName));
        break;
    }
    updateValidationState ();
}

void WorkCellProjectImportWizard::updateValidationState ()
{
    const WorkCellProjectImportRequest value = request ();
    QString projectError;
    const bool projectValid = validProjectSettings (value, &projectError);
    _calculatedPath->setText (value.projectFilePath ());
    _pathWarning->setText (projectValid
                               ? (QFileInfo::exists (value.projectFilePath ())
                                      ? tr ("Warning: the target project already exists. Existing projects are never overwritten.")
                                      : QString ())
                               : projectError);
    if (currentId () == 0 && button (NextButton) != nullptr)
        button (NextButton)->setEnabled (projectValid);
    if (currentId () == 1 && button (NextButton) != nullptr)
        button (NextButton)->setEnabled (_hasInspection);
    QString selectionError;
    const bool selectionValid = _hasInspection && WorkCellProjectImportInspector::validateSelection (
        _inspection, value.options.targetDeviceName, value.options.tcpFrameName, &selectionError);
    if (currentId () == 3 && button (NextButton) != nullptr)
        button (NextButton)->setEnabled (selectionValid);
    if (currentId () == 4 && button (FinishButton) != nullptr)
        button (FinishButton)->setEnabled (projectValid && selectionValid);
    updateReview ();
}

void WorkCellProjectImportWizard::updateReview ()
{
    if (_review == nullptr)
        return;
    const WorkCellProjectImportRequest value = request ();
    _review->setText (tr ("Project: %1\nWorkCell: %2\nTarget Device: %3\nTCP: %4\nCompanion files: %5\nManaged asset policy: copy into project")
                          .arg (value.projectFilePath (), value.sourcePath,
                                value.options.targetDeviceName, value.options.tcpFrameName)
                          .arg (value.options.companionFiles.size ()));
}

}    // namespace rws
