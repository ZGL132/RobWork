#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIRMENTSWIDGET_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIRMENTSWIDGET_HPP

#include "EngineeringRequirementTypes.hpp"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QListWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;

namespace rw { namespace models { class WorkCell; } }

namespace rws {

class EngineeringRequirementsWidget : public QWidget {
    Q_OBJECT
public:
    explicit EngineeringRequirementsWidget(QWidget* parent = nullptr);
    void setWorkCell(rw::models::WorkCell* workcell);
    RequirementSet requirementSet() const;
    QString statusText() const;

private:
    QWidget* createPoseTaskPage();
    QWidget* createBoxRegionPage();
    QWidget* createValidationPage();
    void refreshTables();
    void syncTablesToRequirements();
    void refreshKeyStationList();
    void refreshKeyStationInspector();
    void refreshFrameChoices();
    void commitKeyStationInspector();
    void updateOrientationEditor();
    int selectedKeyStationIndex() const;
    void bindModel();
    void saveRequirements();
    void loadRequirements();
    void freezeRequirements();
    void unfreezeRequirements();
    void addPoseTask();
    void duplicatePoseTask();
    void removePoseTask();
    void captureCurrentTcp();
    void addBoxRegion();
    void duplicateBoxRegion();
    void removeBoxRegion();
    void setStatus(const QString& text);

    RequirementSet _requirements;
    CompiledRequirementSet _compiled;
    rw::models::WorkCell* _workcell = nullptr;
    QTabWidget* _tabs = nullptr;
    QTableWidget* _poseTable = nullptr;
    QListWidget* _stationList = nullptr;
    QLineEdit* _stationNameEdit = nullptr;
    QComboBox* _stationProcessTypeCombo = nullptr;
    QComboBox* _stationLevelCombo = nullptr;
    QComboBox* _stationOrientationModeCombo = nullptr;
    QComboBox* _stationReferenceFrameCombo = nullptr;
    QComboBox* _stationTcpFrameCombo = nullptr;
    QComboBox* _stationOrientationTargetFrameCombo = nullptr;
    QCheckBox* _stationFreeRollCheck = nullptr;
    QCheckBox* _stationApproachEnabled = nullptr;
    QCheckBox* _stationRetractEnabled = nullptr;
    QDoubleSpinBox* _stationApproachDistance = nullptr;
    QDoubleSpinBox* _stationRetractDistance = nullptr;
    QDoubleSpinBox* _stationMinimumJointMargin = nullptr;
    QDoubleSpinBox* _stationX = nullptr;
    QDoubleSpinBox* _stationY = nullptr;
    QDoubleSpinBox* _stationZ = nullptr;
    QDoubleSpinBox* _stationRoll = nullptr;
    QDoubleSpinBox* _stationPitch = nullptr;
    QDoubleSpinBox* _stationYaw = nullptr;
    QGroupBox* _stationAdvancedPoseGroup = nullptr;
    QTableWidget* _regionTable = nullptr;
    QLabel* _modelLabel = nullptr;
    QLabel* _freezeLabel = nullptr;
    QLabel* _statusLabel = nullptr;
    QPushButton* _freezeButton = nullptr;
};

} // namespace rws

#endif
