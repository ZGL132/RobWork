// =============================================================================
//  RobotModelBuilderWidget.hpp
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_WIDGET_HPP
#define RWS_ROBOTMODELBUILDER_WIDGET_HPP

#include "RobotModelSpec.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;
class QTextEdit;

namespace rws {

class RobotModelBuilderWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit RobotModelBuilderWidget (QWidget* parent = NULL);

  Q_SIGNALS:
    void loadSceneRequested (const QString& filename);

  private Q_SLOTS:
    void resetDefaults ();
    void generatePreview ();
    void saveXml ();
    void saveAndLoad ();
    void importUrdf ();
    void browseSaveDirectory ();
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
    void updateSceneUiEnabled ();
    void showErrors (const QStringList& errors);
    void setStatus (const QString& message);

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
    static QComboBox* makeCombo (const QStringList& values, const QString& currentValue,
                                 bool editable);
    static void setCombo (QTableWidget* table, int row, int column,
                          const QStringList& values, const QString& value,
                          bool editable = true);
    void setShapeCombo (QTableWidget* table, int row, int column,
                        const QString& value, bool editable = true);
    void setCollisionShapeCombo (QTableWidget* table, int row, int column,
                                 const QString& value, bool editable = true);
    static bool drawableColumnEditableForShape (const QString& shape, int column,
                                                 bool autoLink);
    static bool collisionColumnEditableForShape (const QString& shape, int column);

  private:
    QLineEdit* _robotName;
    QLineEdit* _saveDirectory;
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
    QTableWidget* _limitsTable;
    QTableWidget* _posesTable;
    QTableWidget* _dynamicsLinksTable;
    QTableWidget* _forceLimitsTable;

    QTextEdit* _serialPreview;
    QTextEdit* _scenePreview;
    QTextEdit* _dwcPreview;
    QLineEdit* _status;

    QTabWidget* _mainTabs = NULL;
    QTabWidget* _previewTabs = NULL;
    QWidget* _sceneTab = NULL;

    bool _syncingTables = false;
};

}    // namespace rws

#endif
