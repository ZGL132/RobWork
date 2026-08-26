#include "RobotProjectImportWizard.hpp"
#include "RobotProjectXacroExpander.hpp"
#include "ProjectPathResolver.hpp"

#include <QComboBox>
#include <QAbstractButton>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QWizardPage>
#include <QUrl>

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
            Q_EMIT editingFinished ();
            event->acceptProposedAction ();
        }
    }
};

void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

QStringList splitArguments (const QString& text)
{
    return text.split (QRegularExpression (QStringLiteral ("\\s+")), Qt::SkipEmptyParts);
}

bool projectSettingsAreValid (const rws::RobotProjectImportRequest& request, QString* error)
{
    const QString rawName = request.projectName;
    const QString name = rawName.trimmed ();
    if (rawName != name) {
        setError (error, QStringLiteral ("Project name must not begin or end with whitespace."));
        return false;
    }
    if (name.isEmpty () || name == QStringLiteral (".") || name == QStringLiteral ("..") ||
        QFileInfo (name).fileName () != name || name.contains (QRegularExpression (QStringLiteral ("[<>:\"/\\\\|?*]")))) {
        setError (error, QStringLiteral ("Project name must be a single safe file name."));
        return false;
    }
    if (name.size () > 180) {
        setError (error, QStringLiteral ("Project name is too long."));
        return false;
    }
    const QFileInfo locationInfo (request.location.trimmed ());
    if (request.location.trimmed ().isEmpty () || !locationInfo.isAbsolute () || !locationInfo.isDir ()) {
        setError (error, QStringLiteral ("Project location must be an existing absolute directory."));
        return false;
    }
    if (!locationInfo.isWritable ()) {
        setError (error, QStringLiteral ("Project location is not writable."));
        return false;
    }
    if (request.projectFilePath ().size () > 240) {
        setError (error, QStringLiteral ("Calculated project path is too long."));
        return false;
    }
    if (!rws::ProjectPathResolver::validateRobWorkCompatiblePath (
            request.projectFilePath (), error))
        return false;
    if (error != nullptr)
        error->clear ();
    return true;
}

}    // namespace

namespace rws {

bool RobotProjectImportRequest::isValid (QString* error) const
{
    if (!projectSettingsAreValid (*this, error))
        return false;
    const QFileInfo sourceInfo (sourcePath.trimmed ());
    if (!sourceInfo.isFile ()) {
        setError (error, QStringLiteral ("Select an existing URDF or Xacro source file."));
        return false;
    }
    if (sourceInfo.suffix ().compare (QStringLiteral ("urdf"), Qt::CaseInsensitive) != 0 &&
        sourceInfo.suffix ().compare (QStringLiteral ("xacro"), Qt::CaseInsensitive) != 0 &&
        sourceInfo.suffix ().compare (QStringLiteral ("xml"), Qt::CaseInsensitive) != 0) {
        setError (error, QStringLiteral ("Robot source must use .urdf, .xacro, or .xml."));
        return false;
    }
    if (sourceInfo.suffix ().compare (QStringLiteral ("xacro"), Qt::CaseInsensitive) == 0 &&
        !RobotProjectXacroExpander::canExpand (options.xacroExecutable)) {
        setError (error, QStringLiteral ("Configure a Xacro executable, install ROS xacro, or install Python xacro."));
        return false;
    }
    if (error != nullptr)
        error->clear ();
    return true;
}

QString RobotProjectImportRequest::projectFilePath () const
{
    QString fileName = projectName.trimmed ();
    if (!fileName.endsWith (QStringLiteral (".rwproj"), Qt::CaseInsensitive))
        fileName += QStringLiteral (".rwproj");
    return QDir (location.trimmed ()).filePath (fileName);
}

RobotProjectImportWizard::RobotProjectImportWizard (const QString& initialLocation, QWidget* parent) :
    QWizard (parent)
{
    setWindowTitle (tr ("Create Project from Robot URDF/Xacro"));
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
    projectForm->addRow (tr ("Project Name"), _projectName);
    projectForm->addRow (tr ("Location"), locationRow);
    projectForm->addRow (tr ("Calculated Path"), _calculatedPath);
    projectForm->addRow (QString (), _pathWarning);
    connect (browseLocation, &QPushButton::clicked, this, [this] {
        const QString selected = QFileDialog::getExistingDirectory (
            this, tr ("Project Location"), _location->text ());
        if (!selected.isEmpty ())
            _location->setText (selected);
    });
    addPage (projectPage);

    QWizardPage* sourcePage = new QWizardPage (this);
    sourcePage->setTitle (tr ("Robot Source and Parser"));
    QFormLayout* sourceForm = new QFormLayout (sourcePage);
    _sourcePath = new DropLineEdit (sourcePage);
    _sourcePath->setObjectName (QStringLiteral ("robotSourcePath"));
    QPushButton* browseSource = new QPushButton (tr ("Browse..."), sourcePage);
    QHBoxLayout* sourceRow = new QHBoxLayout;
    sourceRow->setContentsMargins (0, 0, 0, 0);
    sourceRow->addWidget (_sourcePath);
    sourceRow->addWidget (browseSource);
    _xacroExecutable = new QLineEdit (qEnvironmentVariable ("RWS_XACRO_EXECUTABLE"), sourcePage);
    _xacroExecutable->setPlaceholderText (tr ("Automatic: ROS xacro or python -m xacro"));
    _xacroArguments = new QLineEdit (sourcePage);
    _xacroArguments->setPlaceholderText (tr ("robot_name:=ur10 payload:=10"));
    _assetPolicy = new QComboBox (sourcePage);
    _assetPolicy->addItem (tr ("Managed copy (recommended)"), static_cast< int > (AssetImportPolicy::ManagedCopy));
    _assetPolicy->addItem (tr ("External reference"), static_cast< int > (AssetImportPolicy::ExternalReference));
    sourceForm->addRow (tr ("Robot File"), sourceRow);
    sourceForm->addRow (tr ("Xacro Executable"), _xacroExecutable);
    sourceForm->addRow (tr ("Xacro Arguments"), _xacroArguments);
    sourceForm->addRow (tr ("Asset Policy"), _assetPolicy);
    connect (browseSource, &QPushButton::clicked, this, [this] {
        const QString source = QFileDialog::getOpenFileName (
            this, tr ("Robot URDF or Xacro"), _location->text (),
            tr ("URDF/Xacro Robot Files (*.urdf *.xacro *.xml);;All Files (*.*)"));
        if (!source.isEmpty ()) {
            _sourcePath->setText (source);
            if (_projectName->text ().trimmed ().isEmpty ())
                _projectName->setText (QFileInfo (source).completeBaseName ());
        }
    });
    connect (_sourcePath, &QLineEdit::textChanged, this, &RobotProjectImportWizard::updateSourceDerivedState);
    connect (_xacroExecutable, &QLineEdit::textChanged, this,
             &RobotProjectImportWizard::updateValidationState);
    addPage (sourcePage);

    QWizardPage* meshPage = new QWizardPage (this);
    meshPage->setTitle (tr ("Mesh Geometry and Package Roots"));
    QFormLayout* meshForm = new QFormLayout (meshPage);
    _meshMode = new QComboBox (meshPage);
    _meshMode->addItem (tr ("Visual + Collision"), static_cast< int > (MeshImportMode::VisualAndCollision));
    _meshMode->addItem (tr ("Visual Only"), static_cast< int > (MeshImportMode::VisualOnly));
    _meshMode->addItem (tr ("Disabled"), static_cast< int > (MeshImportMode::Disabled));
    _missingMesh = new QComboBox (meshPage);
    _missingMesh->addItem (tr ("Auto-generate Cylinder/Box (recommended)"), static_cast< int > (MissingMeshPolicy::GenerateCylinder));
    _missingMesh->addItem (tr ("Fail Transaction"), static_cast< int > (MissingMeshPolicy::Fail));
    _packageRootList = new QListWidget (meshPage);
    QPushButton* addRoot = new QPushButton (tr ("Add..."), meshPage);
    QPushButton* removeRoot = new QPushButton (tr ("Remove"), meshPage);
    QHBoxLayout* rootButtons = new QHBoxLayout;
    rootButtons->setContentsMargins (0, 0, 0, 0);
    rootButtons->addWidget (addRoot);
    rootButtons->addWidget (removeRoot);
    QVBoxLayout* rootsLayout = new QVBoxLayout;
    rootsLayout->setContentsMargins (0, 0, 0, 0);
    rootsLayout->addWidget (_packageRootList);
    rootsLayout->addLayout (rootButtons);
    meshForm->addRow (tr ("Geometry Mode"), _meshMode);
    meshForm->addRow (tr ("Missing Mesh"), _missingMesh);
    meshForm->addRow (tr ("Package Roots"), rootsLayout);
    connect (addRoot, &QPushButton::clicked, this, [this] {
        const QString root = QFileDialog::getExistingDirectory (this, tr ("Package Root"), _location->text ());
        if (!root.isEmpty () && _packageRootList->findItems (root, Qt::MatchExactly).isEmpty ()) {
            _packageRootList->addItem (root);
            updateReview ();
        }
    });
    connect (removeRoot, &QPushButton::clicked, this, [this] {
        delete _packageRootList->takeItem (_packageRootList->currentRow ());
        updateReview ();
    });
    addPage (meshPage);

    QWizardPage* reviewPage = new QWizardPage (this);
    reviewPage->setTitle (tr ("Review and Commit"));
    QVBoxLayout* reviewLayout = new QVBoxLayout (reviewPage);
    _summary = new QLabel (reviewPage);
    _summary->setObjectName (QStringLiteral ("importReviewSummary"));
    _summary->setWordWrap (true);
    _summary->setTextInteractionFlags (Qt::TextSelectableByMouse);
    reviewLayout->addWidget (_summary);
    reviewLayout->addStretch (1);
    addPage (reviewPage);

    connect (_projectName, &QLineEdit::textChanged, this, &RobotProjectImportWizard::updateReview);
    connect (_location, &QLineEdit::textChanged, this, &RobotProjectImportWizard::updateReview);
    connect (_assetPolicy, &QComboBox::currentTextChanged, this, &RobotProjectImportWizard::updateReview);
    connect (_meshMode, &QComboBox::currentTextChanged, this, &RobotProjectImportWizard::updateReview);
    connect (_missingMesh, &QComboBox::currentTextChanged, this, &RobotProjectImportWizard::updateReview);
    connect (this, &QWizard::currentIdChanged, this, [this] { updateValidationState (); updateReview (); });
    updateValidationState ();
}

void RobotProjectImportWizard::updateSourceDerivedState ()
{
    const bool xacro = QFileInfo (_sourcePath->text ().trimmed ()).suffix ().compare (
        QStringLiteral ("xacro"), Qt::CaseInsensitive) == 0;
    _xacroExecutable->setVisible (xacro);
    _xacroArguments->setVisible (xacro);
    if (QFormLayout* layout = qobject_cast< QFormLayout* > (_xacroExecutable->parentWidget ()->layout ())) {
        if (QWidget* label = layout->labelForField (_xacroExecutable))
            label->setVisible (xacro);
        if (QWidget* label = layout->labelForField (_xacroArguments))
            label->setVisible (xacro);
    }
    if (_sourcePath->text ().trimmed ().isEmpty ()) {
        _packageRootList->clear ();
        updateValidationState ();
        updateReview ();
        return;
    }
    if (_projectName->text ().trimmed ().isEmpty () && !_sourcePath->text ().trimmed ().isEmpty ())
        _projectName->setText (QFileInfo (_sourcePath->text ().trimmed ()).completeBaseName ());
    _packageRootList->clear ();
    const QDir sourceDir (QFileInfo (_sourcePath->text ().trimmed ()).absolutePath ());
    QDir candidate = sourceDir;
    for (int depth = 0; depth < 3 && candidate.exists (); ++depth) {
        if (_packageRootList->findItems (candidate.absolutePath (), Qt::MatchExactly).isEmpty ())
            _packageRootList->addItem (candidate.absolutePath ());
        if (!candidate.cdUp ())
            break;
    }
    updateValidationState ();
    updateReview ();
}

RobotProjectImportRequest RobotProjectImportWizard::request () const
{
    RobotProjectImportRequest result;
    result.sourcePath = _sourcePath->text ().trimmed ();
    result.projectName = _projectName->text ();
    result.location = _location->text ();
    result.options.meshImportMode = static_cast< MeshImportMode > (_meshMode->currentData ().toInt ());
    result.options.missingMeshPolicy = static_cast< MissingMeshPolicy > (_missingMesh->currentData ().toInt ());
    result.options.assetPolicy = static_cast< AssetImportPolicy > (_assetPolicy->currentData ().toInt ());
    result.options.xacroExecutable = _xacroExecutable->text ().trimmed ();
    result.options.xacroArguments = splitArguments (_xacroArguments->text ());
    for (int index = 0; index < _packageRootList->count (); ++index)
        result.options.packageRoots.push_back (_packageRootList->item (index)->text ());
    return result;
}

void RobotProjectImportWizard::updateValidationState ()
{
    const RobotProjectImportRequest value = request ();
    QString error;
    const bool projectValid = projectSettingsAreValid (value, &error);
    const bool valid = value.isValid (&error);
    const QFileInfo sourceInfo (value.sourcePath);
    const bool xacro = sourceInfo.suffix ().compare (QStringLiteral ("xacro"), Qt::CaseInsensitive) == 0;
    const bool sourceValid = sourceInfo.isFile () &&
                             (!xacro || RobotProjectXacroExpander::canExpand (value.options.xacroExecutable));
    _calculatedPath->setText (value.projectFilePath ());
    const bool existing = QFileInfo::exists (value.projectFilePath ());
    QStringList pathWarnings;
    if (value.projectName.contains (QRegularExpression (QStringLiteral ("\\s"))))
        pathWarnings.push_back (tr ("Note: spaces in project names can reduce command-line portability."));
    if (existing)
        pathWarnings.push_back (tr ("Warning: the target project already exists. Choose a different name or location; existing projects are never overwritten."));
    _pathWarning->setText (pathWarnings.join (QLatin1Char ('\n')));
    if (currentId () == 0)
        if (button (NextButton) != nullptr)
            button (NextButton)->setEnabled (projectValid);
    if (currentId () == 1)
        if (button (NextButton) != nullptr)
            button (NextButton)->setEnabled (sourceValid);
    if (currentId () == 3)
        if (button (FinishButton) != nullptr)
            button (FinishButton)->setEnabled (valid);
}

void RobotProjectImportWizard::updateReview ()
{
    if (_summary == nullptr)
        return;
    const RobotProjectImportRequest value = request ();
    QStringList roots = value.options.packageRoots;
    for (QString& root : roots)
        root.prepend (QStringLiteral ("  - "));
    const bool sourceIsXacro = QFileInfo (value.sourcePath).suffix ().compare (
        QStringLiteral ("xacro"), Qt::CaseInsensitive) == 0;
    const QString xacroSummary = !sourceIsXacro ? tr ("Not applicable") :
        value.options.xacroExecutable.isEmpty () ?
            tr ("Automatic (ROS xacro or python -m xacro) %1")
                .arg (value.options.xacroArguments.join (QLatin1Char (' '))) :
            tr ("%1 %2").arg (value.options.xacroExecutable,
                               value.options.xacroArguments.join (QLatin1Char (' ')));
    _summary->setText (tr ("Project: %1\nPath: %2\nSource: %3\nAsset policy: %4\nXacro: %5\nMesh: %6\nMissing mesh: %7\nPackage roots:\n%8")
                           .arg (value.projectName, value.projectFilePath (), value.sourcePath,
                                 _assetPolicy->currentText (), xacroSummary, _meshMode->currentText (),
                                 _missingMesh->currentText (),
                                 roots.isEmpty () ? tr ("  - None") : roots.join (QLatin1Char ('\n'))));
    updateValidationState ();
}

bool RobotProjectImportWizard::validateCurrentPage ()
{
    QString error;
    const RobotProjectImportRequest value = request ();
    bool valid = true;
    if (currentId () == 0)
        valid = projectSettingsAreValid (value, &error);
    else if (currentId () == 1) {
        const QFileInfo source (value.sourcePath);
        valid = source.isFile ();
        if (!valid)
            error = tr ("Select an existing URDF or Xacro source file.");
        if (valid && source.suffix ().compare (QStringLiteral ("xacro"), Qt::CaseInsensitive) == 0 &&
            !RobotProjectXacroExpander::canExpand (value.options.xacroExecutable)) {
            valid = false;
            error = tr ("Configure a Xacro executable, install ROS xacro, or install Python xacro.");
        }
    }
    else if (currentId () == 4)
        valid = value.isValid (&error);
    if (!valid) {
        QMessageBox::warning (this, tr ("Import Settings"), error);
        return false;
    }
    return true;
}

}    // namespace rws
