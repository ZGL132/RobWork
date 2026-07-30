#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIRMENTSWIDGET_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIRMENTSWIDGET_HPP

#include "EngineeringRequirementTypes.hpp"
#include "RequirementSetUndoStack.hpp"

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

enum class StationTemplateKind;

/**
 * @brief 返回工艺模板对话框中需要显示的“模板专属参数”位掩码。
 *
 * 公共字段（模板类型、工位标识、Frame、TCP、需求等级和作业偏置）不在此掩码中，
 * 始终显示。该策略只描述不同工艺真正消费的生成参数，避免界面要求工程师填写
 * 当前模板不会使用的尺寸或安全距离。
 */
unsigned int templateParameterVisibilityMask(StationTemplateKind kind);

class EngineeringRequirementsWidget : public QWidget {
    Q_OBJECT
public:
    explicit EngineeringRequirementsWidget(QWidget* parent = nullptr);
    void setWorkCell(rw::models::WorkCell* workcell);
    bool applyGeometryFeatureFrame(const QString& frameName, QString* error = nullptr);
    RequirementSet requirementSet() const;
    QString statusText() const;

Q_SIGNALS:
    void geometryFeaturePickRequested();
    void requirementsChanged();

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
    void importStations();
    void undoLastOperation();
    void freezeRequirements();
    void unfreezeRequirements();
    void addPoseTask();
    void duplicatePoseTask();
    void removePoseTask();
    void captureCurrentTcp();
    void requestGeometryFeaturePick();
    void createTemplateStations();
    void updateSelectedTemplateStations();
    void detachSelectedTemplateStation();
    void createStationArray();
    void mirrorSelectedStation();
    void pushUndoSnapshot(const RequirementSet& snapshot);
    void addBoxRegion();
    void duplicateBoxRegion();
    void removeBoxRegion();
    void setStatus(const QString& text);

    RequirementSet _requirements;
    CompiledRequirementSet _compiled;
    RequirementSetUndoStack _undoStack;
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
    bool _refreshingKeyStationInspector = false;
};

} // namespace rws

#endif
