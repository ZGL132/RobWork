#ifndef RWS_ROBOTMODELBUILDER_WIDGET_HPP
#define RWS_ROBOTMODELBUILDER_WIDGET_HPP

#include "RobotModelSpec.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;
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
    void browseSaveDirectory ();
    void modeChanged (int index);
    void addPose ();
    void removeSelectedPose ();

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
    void showErrors (const QStringList& errors);
    void setStatus (const QString& message);
    static QString itemText (const QTableWidget* table, int row, int column);
    static double itemDouble (const QTableWidget* table, int row, int column);
    static bool parseVector3 (const QString& text, std::array< double, 3 >& values);
    static bool parseVector6 (const QString& text, std::array< double, 6 >& values);
    static void setItem (QTableWidget* table, int row, int column, const QString& value,
                         bool editable = true);
    static bool isAutoLinkDrawable (const QString& name);
    static QString vectorText (const std::array< double, 3 >& values);
    static QString vectorText6 (const std::array< double, 6 >& values);

  private:
    QLineEdit* _robotName;
    QLineEdit* _saveDirectory;
    QComboBox* _mode;
    QCheckBox* _showFrameAxes;
    QCheckBox* _generateDrawables;
    QCheckBox* _generateScene;
    QCheckBox* _generateDwc;
    QLineEdit* _baseFrame;
    QLineEdit* _baseMaterial;
    QTableWidget* _dhTable;
    QTableWidget* _transformTable;
    QTableWidget* _drawablesTable;
    QTableWidget* _limitsTable;
    QTableWidget* _posesTable;
    QTableWidget* _dynamicsLinksTable;
    QTableWidget* _forceLimitsTable;
    QTextEdit* _serialPreview;
    QTextEdit* _scenePreview;
    QTextEdit* _dwcPreview;
    QLineEdit* _status;
};

}    // namespace rws

#endif
