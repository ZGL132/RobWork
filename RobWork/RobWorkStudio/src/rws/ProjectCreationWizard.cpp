#include "ProjectCreationWizard.hpp"

#include "ProjectPathResolver.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {

void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

}    // namespace

namespace rws {

const QString ProjectCreationWizard::GenericSixAxisTemplateId =
    QStringLiteral ("generic-six-axis");

bool ProjectCreationRequest::isValid (QString* error) const
{
    const QString trimmedName = projectName.trimmed ();
    if (trimmedName.isEmpty () || trimmedName == QStringLiteral (".") ||
        trimmedName == QStringLiteral ("..") || QFileInfo (trimmedName).fileName () != trimmedName) {
        setError (error, QStringLiteral ("Project name must be a single file name."));
        return false;
    }
    if (location.trimmed ().isEmpty ()) {
        setError (error, QStringLiteral ("Project location is required."));
        return false;
    }
    if (templateId != ProjectCreationWizard::GenericSixAxisTemplateId) {
        setError (error, QStringLiteral ("Choose a supported robot template."));
        return false;
    }
    if (!ProjectPathResolver::validateRobWorkCompatiblePath (projectFilePath (), error))
        return false;
    if (error != nullptr)
        error->clear ();
    return true;
}

QString ProjectCreationRequest::projectFilePath () const
{
    QString fileName = projectName.trimmed ();
    if (!fileName.endsWith (QStringLiteral (".rwproj"), Qt::CaseInsensitive))
        fileName += QStringLiteral (".rwproj");
    return QDir (location.trimmed ()).filePath (fileName);
}

ProjectCreationWizard::ProjectCreationWizard (const QString& initialLocation, QWidget* parent) :
    QWizard (parent)
{
    setWindowTitle (tr ("New RobWorkStudio Project"));
    setOption (QWizard::HaveHelpButton, false);

    QWizardPage* projectPage = new QWizardPage (this);
    projectPage->setTitle (tr ("Project"));
    QFormLayout* projectLayout = new QFormLayout (projectPage);
    _projectName = new QLineEdit (projectPage);
    _projectName->setObjectName (QStringLiteral ("projectName"));
    _location = new QLineEdit (initialLocation, projectPage);
    _location->setObjectName (QStringLiteral ("projectLocation"));
    QPushButton* browse = new QPushButton (tr ("Browse..."), projectPage);
    QHBoxLayout* locationLayout = new QHBoxLayout ();
    locationLayout->setContentsMargins (0, 0, 0, 0);
    locationLayout->addWidget (_location);
    locationLayout->addWidget (browse);
    projectLayout->addRow (tr ("Project Name"), _projectName);
    projectLayout->addRow (tr ("Location"), locationLayout);
    connect (browse, &QPushButton::clicked, this, [this] {
        const QString selected = QFileDialog::getExistingDirectory (
            this, tr ("Project Location"), _location->text ());
        if (!selected.isEmpty ())
            _location->setText (selected);
    });
    addPage (projectPage);

    QWizardPage* templatePage = new QWizardPage (this);
    templatePage->setTitle (tr ("Robot Template"));
    QVBoxLayout* templateLayout = new QVBoxLayout (templatePage);
    _templates = new QListWidget (templatePage);
    _templates->setObjectName (QStringLiteral ("robotTemplates"));
    QListWidgetItem* genericSixAxis = new QListWidgetItem (
        tr ("Generic 6-axis serial robot"), _templates);
    genericSixAxis->setData (Qt::UserRole, GenericSixAxisTemplateId);
    genericSixAxis->setToolTip (tr ("Baseline serial robot model for modeling and optimization."));
    _templates->setCurrentItem (genericSixAxis);
    templateLayout->addWidget (_templates);
    addPage (templatePage);
}

ProjectCreationRequest ProjectCreationWizard::request () const
{
    ProjectCreationRequest result;
    result.projectName = _projectName->text ();
    result.location = _location->text ();
    if (const QListWidgetItem* item = _templates->currentItem ())
        result.templateId = item->data (Qt::UserRole).toString ();
    return result;
}

bool ProjectCreationWizard::validateCurrentPage ()
{
    QString error;
    if (!request ().isValid (&error)) {
        QMessageBox::warning (this, tr ("Project Details"), error);
        return false;
    }
    return true;
}

}    // namespace rws
