#ifndef RWS_ROBOTPROJECTIMPORTWIZARD_HPP
#define RWS_ROBOTPROJECTIMPORTWIZARD_HPP

#include "RobotProjectImportOptions.hpp"

#include <QMap>
#include <QPair>
#include <QWizard>
#include <QStringList>

class QComboBox;
class QLabel;
class QListWidget;
class QLineEdit;
class QTreeWidget;

namespace rws {

struct RobotProjectImportRequest
{
    QString sourcePath;
    QString projectName;
    QString location;
    RobotProjectImportOptions options;
    QStringList mutableLinks;
    QMap< QString, QPair< double, double > > mutableLinkRanges;

    bool isValid (QString* error = nullptr) const;
    QString projectFilePath () const;
};

class RobotProjectImportWizard : public QWizard
{
  public:
    explicit RobotProjectImportWizard (const QString& initialLocation, QWidget* parent = nullptr);
    RobotProjectImportRequest request () const;

  protected:
    bool validateCurrentPage () override;

  private Q_SLOTS:
    void updateSourceDerivedState ();
    void updateReview ();

  private:
    void updateValidationState ();
    void rebuildLinkTree ();

    QLineEdit* _sourcePath = nullptr;
    QLineEdit* _projectName = nullptr;
    QLineEdit* _location = nullptr;
    QLineEdit* _xacroExecutable = nullptr;
    QLineEdit* _xacroArguments = nullptr;
    QComboBox* _meshMode = nullptr;
    QComboBox* _missingMesh = nullptr;
    QComboBox* _assetPolicy = nullptr;
    QListWidget* _packageRootList = nullptr;
    QTreeWidget* _links = nullptr;
    QLabel* _calculatedPath = nullptr;
    QLabel* _pathWarning = nullptr;
    QLabel* _linkRangeWarning = nullptr;
    QLabel* _summary = nullptr;
};

}    // namespace rws

#endif
