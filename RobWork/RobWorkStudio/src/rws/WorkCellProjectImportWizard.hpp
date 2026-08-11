#ifndef RWS_WORKCELLPROJECTIMPORTWIZARD_HPP
#define RWS_WORKCELLPROJECTIMPORTWIZARD_HPP

#include "WorkCellProjectImportInspector.hpp"
#include "WorkCellProjectImportOptions.hpp"

#include <QWizard>

class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

namespace rws {

struct WorkCellProjectImportRequest
{
    QString sourcePath;
    QString projectName;
    QString location;
    WorkCellProjectImportOptions options;

    bool isValid (QString* error = nullptr) const;
    QString projectFilePath () const;
};

class WorkCellProjectImportWizard : public QWizard
{
  public:
    explicit WorkCellProjectImportWizard (const QString& initialLocation,
                                          QWidget* parent = nullptr);
    WorkCellProjectImportRequest request () const;

  protected:
    bool validateCurrentPage () override;

  private Q_SLOTS:
    void inspectSource ();
    void rebuildTcpChoices ();
    void updateValidationState ();
    void updateReview ();

  private:
    bool ensureInspection (QString* error = nullptr);
    void rebuildCompanionList ();

    QLineEdit* _sourcePath = nullptr;
    QLineEdit* _projectName = nullptr;
    QLineEdit* _location = nullptr;
    QTreeWidget* _companions = nullptr;
    QComboBox* _targetDevice = nullptr;
    QComboBox* _tcpFrame = nullptr;
    QLabel* _calculatedPath = nullptr;
    QLabel* _pathWarning = nullptr;
    QLabel* _sourceSummary = nullptr;
    QLabel* _deviceSummary = nullptr;
    QLabel* _review = nullptr;
    WorkCellProjectImportInspection _inspection;
    bool _hasInspection = false;
};

}    // namespace rws

#endif    // RWS_WORKCELLPROJECTIMPORTWIZARD_HPP
