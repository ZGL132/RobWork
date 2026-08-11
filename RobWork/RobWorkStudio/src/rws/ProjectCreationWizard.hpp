#ifndef RWS_PROJECTCREATIONWIZARD_HPP
#define RWS_PROJECTCREATIONWIZARD_HPP

#include <QString>
#include <QWizard>

class QLineEdit;
class QListWidget;

namespace rws {

struct ProjectCreationRequest
{
    QString projectName;
    QString location;
    QString templateId;

    bool isValid (QString* error = nullptr) const;
    QString projectFilePath () const;
};

class ProjectCreationWizard : public QWizard
{
  public:
    static const QString GenericSixAxisTemplateId;

    explicit ProjectCreationWizard (const QString& initialLocation, QWidget* parent = nullptr);

    ProjectCreationRequest request () const;

  protected:
    bool validateCurrentPage () override;

  private:
    QLineEdit* _projectName = nullptr;
    QLineEdit* _location = nullptr;
    QListWidget* _templates = nullptr;
};

}    // namespace rws

#endif    // RWS_PROJECTCREATIONWIZARD_HPP
