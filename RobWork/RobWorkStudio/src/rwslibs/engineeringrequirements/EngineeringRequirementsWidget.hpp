#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIRMENTSWIDGET_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIRMENTSWIDGET_HPP

#include "EngineeringRequirementTypes.hpp"
#include "RequirementFreezer.hpp"
#include "RequirementSetUndoStack.hpp"

#include <rw/kinematics/State.hpp>
#include <QWidget>

#include <functional>
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
    // 同步当前项目输出目录（空表示独立 WorkCell 工作流）。
    void setProjectOutputDirectory(const QString& projectDirectory);
    // 导出副本默认路径（有项目时落在 requirements/exports/ 下）。
    static QString requirementCopyExportPath(const QString& projectDirectory);
    // 导入副本初始目录（优先 requirements/exports，否则 requirements）。
    static QString requirementCopyImportDirectory(const QString& projectDirectory);
    // 由项目清单解析 robot-model.main 后注入；不扫描目录猜测模型。
    void setProjectModelPath(const QString& modelPath);
    // 绑定项目清单中与当前设备匹配的 .rmb.json 工程模型。
    bool bindGeneratedProjectModel(QString* error = nullptr);
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
    void setFreezeReadinessCheck(std::function<bool(QString*)> check);
    void reportFreezePublicationResult(bool saved, const QString& error = QString());

    /**
     * @brief 由项目文档 Provider 调用的无界面读取入口。
     *
     * 项目系统已经完成了清单路径校验并把资源解析为绝对路径；Widget 只负责把 JSON
     * 还原为领域对象。成功后记录持久化快照，使随后的编辑可以准确参与主窗口脏状态。
     */
    bool loadProjectDocument(const QString& path, QString* error = nullptr,
                             const QString& projectRoot = QString());

    /**
     * @brief 将当前需求写入 Provider 提供的暂存路径，不直接覆盖正式项目资源。
     *
     * Registry 会在所有资源暂存成功后统一提交事务，因此此函数不能自行更新“已保存”
     * 快照；只有 Provider 收到事务成功后的 markClean() 才可以确认数据真的落盘。
     */
    bool saveProjectDocument(const QString& targetPath, QString* error = nullptr);
    bool isProjectDocumentDirty();
    void markProjectDocumentClean();
    // 首次编辑生成资源后建立会话基线与项目内路径。
    void beginGeneratedProjectDocument(const QString& path);
    // 项目关闭/切换时释放仅用于脏比较的路径与快照。
    void clearProjectDocumentContext();

Q_SIGNALS:
    void geometryFeaturePickRequested();
    void requirementsChanged();
    void requirementsUnfrozen();
    // 冻结发布请求：携带资源 id、项目内文档路径、需求指纹与 schema 版本，
    // 供插件把冻结工件发布到正确的项目资源位置并核对一致性。
    void freezePublicationRequested(const QString& resourceId, const QString& path,
                                    const QString& requirementFingerprint, int schemaVersion);

private:
    QWidget* createPoseTaskPage();
    QWidget* createBoxRegionPage();
    QWidget* createValidationPage();
    void refreshTables();
    void refreshValidationPanel();
    void syncTablesToRequirements();
    void refreshKeyStationList();
    void refreshKeyStationInspector();
    void refreshFrameChoices();
    void validateLoadedFrozenArtifact();
    bool loadRequirementDocument(const QString& path, bool captureProjectSnapshot, QString* error,
                                 const QString& projectRoot = QString());
    bool loadRobotModelDocument(const QString& path, const QString& projectRoot,
                                RobotModelSpec& model, QString* error) const;
    bool writeRequirementDocument(const QString& targetPath, QString* error);
    QByteArray serializedProjectDocument(const QString& documentPath) const;
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
    // A project document is restored before RobWorkStudio publishes its WorkCell.
    // Keep parsed frozen evidence until the WorkCell is available for verification.
    bool _pendingFrozenArtifactValidation = false;
    QString _pendingFrozenArtifactProjectRoot;
    RequirementSetUndoStack _undoStack;
    rw::models::WorkCell* _workcell = nullptr;
    QString _projectOutputDirectory;
    QString _projectModelPath;
    std::function<bool(QString*)> _freezeReadinessCheck;
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
    QLabel* _validationSummaryLabel = nullptr;
    QTableWidget* _diagnosticTable = nullptr;
    QPushButton* _freezeButton = nullptr;
    bool _refreshingKeyStationInspector = false;
    bool _stationOrientationCoordinatesResolved = true;
    // 已保存快照只保存规范 JSON 字节；比较它而非 UI 焦点状态，可让用户把值改回原值时
    // 自动恢复为干净状态，并避免选择页签等非持久化操作制造伪脏标记。
    QString _projectDocumentPath;
    QByteArray _savedProjectDocumentSnapshot;
    QByteArray _pendingProjectDocumentSnapshot;
    // 历史文档在内存中规范化后，即使业务字段未编辑也必须保持脏状态，直到项目
    // 保存事务成功，确保 extensions.frozenArtifact 的清理真正持久化到磁盘。
    bool _projectDocumentMigrationPending = false;
};

} // namespace rws

#endif
