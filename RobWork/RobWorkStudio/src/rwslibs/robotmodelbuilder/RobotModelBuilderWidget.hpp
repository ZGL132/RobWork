// =============================================================================
//  RobotModelBuilderWidget.hpp
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_WIDGET_HPP
#define RWS_ROBOTMODELBUILDER_WIDGET_HPP

#include "RobotModelSpec.hpp"

#include <QByteArray>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;
class QTextEdit;
class QEvent;

namespace rws {

class RobotModelBuilderWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit RobotModelBuilderWidget (QWidget* parent = NULL);
    void syncFromWorkCellSpec (const RobotModelSpec& spec, const QStringList& warnings);
    bool preflightUrdfFile (const QString& path,
                            const QString& projectRoot,
                            RobotModelSpec& parsed,
                            QStringList& warnings,
                            QString* error = nullptr) const;
    void applyImportedProjectModel (const RobotModelSpec& parsed,
                                    const QStringList& warnings);
    // 无对话框 URDF 导入（供"从机器人文件创建项目"复用）；失败经 error 回填。
    bool importUrdfFile (const QString& path, QString* error = nullptr);
    // 当前 UI 收集到的模型规格（用于登记生成资源时的文件基名）。
    RobotModelSpec currentModelSpec () const { return collectSpec (); }
    // 向插件状态栏写入提示消息。
    void setProjectStatus (const QString& message) { setStatus (message); }

    // 项目模式下的 XML 产物统一写入 <项目目录>/generated/robot-models；空路径表示
    // 独立 WorkCell 工作流，继续使用运行时默认目录但不在界面中暴露可编辑目录输入。
    void setProjectOutputDirectory (const QString& projectDirectory);
    QString projectOutputDirectory () const { return _projectOutputDirectory; }

    /**
     * @brief 从项目 Provider 传入的已解析路径加载模型 JSON，不显示文件对话框。
     *
     * 只有完整反序列化成功后才替换当前 UI；失败时保留原模型，调用方据此中止项目
     * 打开并报告错误。成功后保存规范化快照，刚加载的文档不会被误判为脏状态。
     */
    bool loadProjectDocument (const QString& path, QString* error = nullptr);
    /** @brief 把当前模型写入保存事务分配的暂存路径，不直接覆盖正式项目资源。 */
    bool saveProjectDocument (const QString& targetPath, QString* error = nullptr) const;
    /** @brief 首次登记生成资源后建立空基线，使当前 WorkCell 导入模型进入项目保存事务。 */
    void beginGeneratedProjectDocument ();
    /** @brief 比较规范 JSON 快照，忽略控件焦点和页签选择等非持久化 UI 状态。 */
    bool isProjectDocumentDirty () const;
    /** @brief 仅在 ProjectSaveTransaction 完整提交成功后更新干净快照。 */
    void markProjectDocumentClean ();

  Q_SIGNALS:
    void loadSceneRequested (const QString& filename);
    // 用户交互后通知插件重新比较快照；信号本身不表示数据一定发生了变化。
    void projectDocumentInteraction ();

  private Q_SLOTS:
    void resetDefaults ();
    void generatePreview ();
    void saveXml ();
    void saveAndLoad ();
    void importUrdf ();
    void modeChanged (int index);
    void addPose ();
    void removeSelectedPose ();
    void addJoint ();
    void removeSelectedJoint ();
    void moveSelectedJointUp ();
    void moveSelectedJointDown ();
    void addSceneFrame ();
    void removeSelectedSceneFrame ();
    void addSceneGeometry ();
    void removeSelectedSceneGeometry ();
    void addCollisionModel ();
    void removeSelectedCollisionModel ();
    void generateCollisionModelsFromDrawables ();
    void addCollisionExcludePair ();
    void removeSelectedCollisionExcludePair ();
    void generateDefaultCollisionSetup ();
    void sceneGenerationToggled (bool checked);
    void onDhTableCellChanged (QTableWidgetItem* item);
    void onTransformTableCellChanged (QTableWidgetItem* item);

  private:
    void buildUi ();
    void fillFromSpec (const RobotModelSpec& spec);
    RobotModelSpec collectSpec () const;
    bool validateTableInput (QStringList& errors) const;
    void fillKinematicsTables (const RobotModelSpec& spec);
    void fillDrawablesTable (const RobotModelSpec& spec);
    void fillLimitsTable (const RobotModelSpec& spec);
    void fillPosesTable (const RobotModelSpec& spec);
    void fillDynamicsTab (const RobotModelSpec& spec);
    void fillSceneTab (const RobotModelSpec& spec);
    void fillSceneGeometryTable (const RobotModelSpec& spec);
    void fillCollisionModelsTable (const RobotModelSpec& spec);
    void fillCollisionSetupTab (const RobotModelSpec& spec);
    void chooseGeometryFile (QTableWidget* table, int row, int column);
    void synchronizeCollisionFileFromDrawable (int row);
    void updateSceneUiEnabled ();
    void updateOutputFilePlaceholders ();
    bool confirmOutputOverwrite (const RobotModelSpec& spec);
    // 生成模型时 saveDirectory 仍是 XmlWriter 的运行时必需字段；本函数优先返回项目受管
    // 输出目录，避免任何项目内操作回退到用户主目录或历史模型记录的绝对路径。
    QString effectiveSaveDirectory () const;
    void showErrors (const QStringList& errors);
    void setStatus (const QString& message);
    bool eventFilter (QObject* watched, QEvent* event) override;
    QByteArray projectDocumentSnapshot () const;
    bool serializeProjectDocument (QByteArray& snapshot, QString* error = nullptr) const;

    static QString itemText (const QTableWidget* table, int row, int column);
    static double itemDouble (const QTableWidget* table, int row, int column);
    static bool parseVector3 (const QString& text, std::array< double, 3 >& values);
    static bool parseVector6 (const QString& text, std::array< double, 6 >& values);
    static bool parseVector16 (const QString& text, std::array< double, 16 >& values);
    static void setItem (QTableWidget* table, int row, int column, const QString& value,
                         bool editable = true);
    static bool isAutoLinkDrawable (const QString& name);
    static QString vectorText (const std::array< double, 3 >& values);
    static QString vectorText6 (const std::array< double, 6 >& values);
    static QString vectorText16 (const std::array< double, 16 >& values);
    static QString collisionSizeText (const CollisionModelSpec& collision);
    static bool parseCollisionSize (const QString& text, CollisionModelSpec& collision);
    static QString collisionPoseText (const CollisionModelSpec& collision);
    static bool parseCollisionPose (const QString& text, CollisionModelSpec& collision);
    static QComboBox* makeCombo (const QStringList& values, const QString& currentValue,
                                 bool editable);
    static void setCombo (QTableWidget* table, int row, int column,
                          const QStringList& values, const QString& value,
                          bool editable = true);
    static QCheckBox* setCheckBox (QTableWidget* table, int row, int column,
                                   bool checked, bool editable = true);
    static bool itemChecked (const QTableWidget* table, int row, int column);
    void setShapeCombo (QTableWidget* table, int row, int column,
                        const QString& value, bool editable = true);
    void setCollisionShapeCombo (QTableWidget* table, int row, int column,
                                 const QString& value, bool editable = true);
    static bool drawableColumnEditableForShape (const QString& shape, int column,
                                                 bool autoLink);
    static bool collisionColumnEditableForShape (const QString& shape, int column);

  private:
    QLineEdit* _robotName;
    QLineEdit* _deviceFile;
    QLineEdit* _sceneFile;
    QLineEdit* _dynamicWorkCellFile;
    QCheckBox* _preserveImportedFileLayout;
    QComboBox* _mode;

    QCheckBox* _showFrameAxes;
    QCheckBox* _generateDrawables;
    QCheckBox* _generateScene;
    QCheckBox* _generateDwc;
    QCheckBox* _exportDhAdvanced;

    QLineEdit* _baseFrame;
    QLineEdit* _baseMaterial;

    QLineEdit* _robotBaseRpy;        // Milestone 3:Scene Frames 标签页 RobotBase RPY
    QLineEdit* _robotBasePos;        // Milestone 3:Scene Frames 标签页 RobotBase Pos
    QTableWidget* _sceneFramesTable; // Milestone 3:Scene Frames 标签页的可编辑表格
    QTableWidget* _sceneGeometryTable; // Milestone 3.5:场景几何体表

    QTableWidget* _dhTable;
    QTableWidget* _transformTable;
    QTableWidget* _drawablesTable;
    QTableWidget* _collisionModelsTable;                       // Milestone 5
    QCheckBox* _collisionSetupEnabled = NULL;
    QLineEdit* _collisionSetupFile = NULL;
    QCheckBox* _excludeBaseFirst = NULL;
    QCheckBox* _excludeAdjacent = NULL;
    QCheckBox* _excludeStatic = NULL;
    QTableWidget* _collisionSetupPairsTable = NULL;
    QTableWidget* _limitsTable;
    QTableWidget* _posesTable;
    QTableWidget* _dynamicsLinksTable;
    QTableWidget* _forceLimitsTable;

    QTextEdit* _serialPreview;
    QTextEdit* _scenePreview;
    QTextEdit* _dwcPreview;
    QTextEdit* _collisionSetupPreview;
    QTextEdit* _proximitySetupPreview;
    QLineEdit* _status;

    QTabWidget* _mainTabs = NULL;
    QTabWidget* _previewTabs = NULL;
    QWidget* _sceneTab = NULL;
    QWidget* _sceneContent = NULL;

    bool _syncingTables = false;
    bool _importingFromWorkCell = false;
    // 仅在内存中保存的项目输出根目录，不序列化到 .rmb.json；项目整体移动后会由主窗口
    // 的 projectContextChanged 信号重新计算，因而不会留下机器相关的绝对路径。
    QString _projectDirectory;
    QString _projectOutputDirectory;
    ImportedDocumentSpec _importedDocument;
    QByteArray _projectCleanSnapshot;
    bool _projectSnapshotActive = false;
    QStringList _lastUrdfImportWarnings;    // 最近一次 URDF 导入的非致命警告（供 UI 展示）。
};

}    // namespace rws

#endif
