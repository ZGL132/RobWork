#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIRMENTSWIDGET_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIRMENTSWIDGET_HPP

#include "EngineeringRequirementTypes.hpp"
#include "RequirementFreezer.hpp"
#include "RequirementSetUndoStack.hpp"

#include <rw/kinematics/State.hpp>
#include <QWidget>

#include <memory>

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
    /**
     * @brief 接收 RobWorkStudio 最新发布的场景/JOG 状态快照。
     *
     * 需求插件不能自行读取 WorkCell 的默认状态来代替用户刚刚在 JOG、拖动
     * 工装或回放轨迹后看到的实际状态。该副本只在 WorkCell 生命周期内保存，
     * 并作为 TCP 捕获、几何特征解析、冻结与即时校验的唯一状态来源。
     */
    void setCurrentState(const rw::kinematics::State& state);
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
    void commitBoxRegionTableEdit();
    void updateOrientationEditor();
    int selectedKeyStationIndex() const;
    void bindModel();
    void saveRequirements();
    void loadRequirements();
    void importStations();
    void undoLastOperation();
    void redoLastOperation();
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
    void recordRequirementEdit(const RequirementSet& snapshot, bool refreshAllWidgets = true);
    void addBoxRegion();
    void duplicateBoxRegion();
    void removeBoxRegion();
    void setStatus(const QString& text);
    /**
     * @brief 返回本插件绑定 WorkCell 所对应的最新状态。
     *
     * 插件刚打开 WorkCell 但主程序尚未派发 stateChangedEvent 时，退回该
     * WorkCell 的默认状态，以保证界面不会因短暂的初始化顺序而崩溃；正常
     * 交互和冻结流程会优先使用 setCurrentState() 保存的快照。
     */
    rw::kinematics::State activeWorkCellState() const;

    RequirementSet _requirements;
    CompiledRequirementSet _compiled;
    // 冻结工件保留完整的编译结果、环境指纹和诊断，后续保存/下游交接不能仅
    // 依赖可编辑的 RequirementSet.frozen 标记来判断其是否已经经过真实校验。
    FrozenRequirementArtifact _frozenArtifact;
    RequirementSetUndoStack _undoStack;
    rw::models::WorkCell* _workcell = nullptr;
    // State 与所属 WorkCell 的 StateStructure 强关联；切换 WorkCell 时必须丢弃
    // 旧快照，防止将旧场景的关节值误用于新场景的几何解析。
    std::unique_ptr<rw::kinematics::State> _currentState;
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
    QLabel* _stationOrientationTargetFrameLabel = nullptr;
    QLineEdit* _stationOrientationTargetPointEdit = nullptr;
    QLabel* _stationOrientationTargetPointLabel = nullptr;
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
    QLabel* _stationAdvancedPoseSourceLabel = nullptr;
    QTableWidget* _regionTable = nullptr;
    QLabel* _modelLabel = nullptr;
    QLabel* _freezeLabel = nullptr;
    QLabel* _statusLabel = nullptr;
    QPushButton* _freezeButton = nullptr;
    bool _refreshingKeyStationInspector = false;
    bool _stationOrientationCoordinatesResolved = true;
};

} // namespace rws

#endif
