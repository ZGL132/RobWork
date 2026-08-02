#include "RobotModelBuilderWidget.hpp"
#include "RobotModelSpecJson.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMessageBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>

#include <iostream>

namespace {
int fail (const char* message)
{
    std::cerr << message << std::endl;
    return 1;
}

QTabWidget* findPreviewTabs (const rws::RobotModelBuilderWidget& widget)
{
    const QList< QTabWidget* > tabs = widget.findChildren< QTabWidget* > ();
    for (QTabWidget* tab : tabs) {
        if (tab->count () > 0 && tab->tabText (0) == "SerialDevice XML")
            return tab;
    }
    return NULL;
}

QTextEdit* previewAt (QTabWidget* tabs, const QString& name)
{
    for (int index = 0; index < tabs->count (); ++index) {
        if (tabs->tabText (index) == name)
            return qobject_cast< QTextEdit* > (tabs->widget (index));
    }
    return NULL;
}

QTableWidget* findTable (const rws::RobotModelBuilderWidget& widget,
                         const QString& secondHeader)
{
    const QList< QTableWidget* > tables = widget.findChildren< QTableWidget* > ();
    for (QTableWidget* table : tables) {
        const QTableWidgetItem* header = table->horizontalHeaderItem (1);
        if (header != NULL && header->text () == secondHeader)
            return table;
    }
    return NULL;
}

QTableWidget* findTable (const rws::RobotModelBuilderWidget& widget,
                         const QString& firstHeader, const QString& secondHeader,
                         int columnCount)
{
    const QList< QTableWidget* > tables = widget.findChildren< QTableWidget* > ();
    for (QTableWidget* table : tables) {
        const QTableWidgetItem* first = table->horizontalHeaderItem (0);
        const QTableWidgetItem* second = table->horizontalHeaderItem (1);
        if (table->columnCount () == columnCount && first != NULL && second != NULL &&
            first->text () == firstHeader && second->text () == secondHeader)
            return table;
    }
    return NULL;
}

bool isSelectionCombo (QTableWidget* table, int row, int column)
{
    QComboBox* combo = qobject_cast< QComboBox* > (table->cellWidget (row, column));
    return combo != NULL && !combo->isEditable ();
}

bool isEnabledCombo (QTableWidget* table, int row, int column)
{
    QComboBox* combo = qobject_cast< QComboBox* > (table->cellWidget (row, column));
    return combo != NULL && combo->count () == 2 && combo->itemText (0) == "Enabled" &&
           combo->itemText (1) == "Disabled";
}

QLineEdit* findLineEdit (const rws::RobotModelBuilderWidget& widget, const QString& value)
{
    const QList< QLineEdit* > lineEdits = widget.findChildren< QLineEdit* > ();
    for (QLineEdit* lineEdit : lineEdits) {
        if (lineEdit->text () == value)
            return lineEdit;
    }
    return NULL;
}

bool rejectOverwriteConfirmation ()
{
    const QList< QWidget* > widgets = QApplication::topLevelWidgets ();
    for (QWidget* widget : widgets) {
        QMessageBox* messageBox = qobject_cast< QMessageBox* > (widget);
        if (messageBox != NULL && messageBox->windowTitle () == "Confirm overwrite") {
            messageBox->done (QMessageBox::No);
            return true;
        }
    }
    return false;
}

class CurrentDirectoryGuard
{
  public:
    CurrentDirectoryGuard () : _original (QDir::currentPath ()) {}
    ~CurrentDirectoryGuard () { QDir::setCurrent (_original); }

  private:
    QString _original;
};

bool writeFixture (const QString& path, const QByteArray& bytes)
{
    if (!QDir ().mkpath (QFileInfo (path).absolutePath ()))
        return false;
    QFile file (path);
    return file.open (QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write (bytes) == bytes.size ();
}

bool copyFixture (const QString& source, const QString& target)
{
    return QDir ().mkpath (QFileInfo (target).absolutePath ()) && QFile::copy (source, target);
}
}    // namespace

int main (int argc, char** argv)
{
    QApplication application (argc, argv);
    rws::RobotModelBuilderWidget widget;

    const QList< QLabel* > labels = widget.findChildren< QLabel* > ();
    for (QLabel* label : labels) {
        if (label->text () == "Effective Exclusions")
            return fail ("Effective exclusions preview should not be shown.");
    }

    QTableWidget* drawables = findTable (widget, "Name", "RefFrame", 10);
    QTableWidget* limits = findTable (widget, "Joint", "PosMin", 5);
    QTableWidget* dynamicsLinks = findTable (widget, "Link", "Object", 7);
    QTableWidget* forceLimits = findTable (widget, "Joint", "Max force", 2);
    QTableWidget* sceneFrames = findTable (widget, "Name", "RefFrame", 8);
    QTableWidget* sceneGeometries = findTable (widget, "Name", "RefFrame", 11);
    if (drawables == NULL || limits == NULL || dynamicsLinks == NULL || forceLimits == NULL ||
        sceneFrames == NULL || sceneGeometries == NULL ||
        !isSelectionCombo (drawables, 0, 1) || !isSelectionCombo (limits, 0, 0) ||
        !isSelectionCombo (dynamicsLinks, 0, 1) || !isSelectionCombo (forceLimits, 0, 0) ||
        !isSelectionCombo (sceneFrames, 0, 1) || !isSelectionCombo (sceneGeometries, 0, 1))
        return fail ("Reference columns should use non-editable selection combos.");

    QCheckBox* sceneGeneration = NULL;
    const QList< QCheckBox* > checkboxes = widget.findChildren< QCheckBox* > ();
    for (QCheckBox* checkbox : checkboxes) {
        if (checkbox->text () == "Generate Scene file") {
            sceneGeneration = checkbox;
            break;
        }
    }
    if (sceneGeneration == NULL)
        return fail ("Scene generation checkbox was not found.");
    sceneGeneration->setChecked (true);

    if (!QMetaObject::invokeMethod (&widget, "generatePreview", Qt::DirectConnection))
        return fail ("Could not generate the XML preview.");

    QTabWidget* previewTabs = findPreviewTabs (widget);
    if (previewTabs == NULL)
        return fail ("XML preview tabs were not found.");
    if (previewTabs->count () != 5)
        return fail ("XML preview should show Device, Scene, DWC, CollisionSetup, and ProximitySetup.");

    QTextEdit* collisionPreview = previewAt (previewTabs, "CollisionSetup XML");
    if (collisionPreview == NULL || !collisionPreview->toPlainText ().contains ("<CollisionSetup>"))
        return fail ("CollisionSetup preview should contain generated XML when Scene is enabled.");

    QTextEdit* proximityPreview = previewAt (previewTabs, "ProximitySetup XML");
    if (proximityPreview == NULL ||
        !proximityPreview->toPlainText ().contains ("Enable ProximitySetup"))
        return fail ("Disabled ProximitySetup should show an explicit preview placeholder.");

    if (!QMetaObject::invokeMethod (&widget, "addCollisionModel", Qt::DirectConnection))
        return fail ("Could not add a collision model.");
    QTableWidget* collisionModels = findTable (widget, "Name");
    if (collisionModels == NULL ||
        !isEnabledCombo (collisionModels, 0, 0))
        return fail ("Collision model enabled state should use an Enabled/Disabled combo.");
    if (!isSelectionCombo (collisionModels, 0, 2))
        return fail ("Collision model RefFrame should use a selection combo.");

    if (!QMetaObject::invokeMethod (&widget, "addCollisionExcludePair", Qt::DirectConnection))
        return fail ("Could not add a collision exclusion pair.");
    QTableWidget* exclusionPairs = findTable (widget, "First Frame");
    if (exclusionPairs == NULL ||
        !isEnabledCombo (exclusionPairs, 0, 0))
        return fail ("Collision exclusion enabled state should use an Enabled/Disabled combo.");
    if (!isSelectionCombo (exclusionPairs, 0, 1) || !isSelectionCombo (exclusionPairs, 0, 2))
        return fail ("Collision exclusion frame pairs should use selection combos.");

    QLineEdit* robotName = findLineEdit (widget, "GenericSixAxis");
    QLineEdit* removedSaveDirectory = findLineEdit (widget, QDir::homePath ());
    if (robotName == NULL || removedSaveDirectory != NULL)
        return fail ("RobotModelBuilder should not expose a standalone save directory field.");

    // 项目 Provider 通过这组无对话框接口读写资源。先建立保存后的干净基线，再修改
    // 一个会写入 RobotModelSpec 的字段，验证 Widget 用规范 JSON 快照而非焦点状态
    // 判断脏数据；最后重新加载确认项目资源能回到干净状态。
    QTemporaryDir projectDirectory;
    if (!projectDirectory.isValid ())
        return fail ("Could not create a temporary project resource directory.");
    widget.setProjectOutputDirectory (projectDirectory.path ());
    const QString generatedDirectory =
        QDir (projectDirectory.path ()).filePath ("generated/robot-models");
    if (!QDir (generatedDirectory).exists ())
        return fail ("Project output directory should be created inside the project directory.");
    QString projectError;

    // 无对话框导入路径：写入一个最小 URDF 草稿文件，验证 importUrdfFile 不弹文件对话框
    // 即可填充机器人名称，供"从机器人文件创建项目"工作流复用。
    const QString draftUrdf = projectDirectory.filePath ("DraftBot.urdf");
    QFile draftUrdfFile (draftUrdf);
    if (!draftUrdfFile.open (QFile::WriteOnly | QFile::Text) ||
        draftUrdfFile.write (
            "<robot name=\"DraftBot\"><link name=\"base\"/><link name=\"tip\"/>"
            "<joint name=\"joint1\" type=\"revolute\"><parent link=\"base\"/>"
            "<child link=\"tip\"/><axis xyz=\"0 0 1\"/>"
            "<limit lower=\"-1\" upper=\"1\" velocity=\"1\" effort=\"1\"/>"
            "</joint></robot>") < 0)
        return fail ("Could not create a URDF project draft source file.");
    draftUrdfFile.close ();
    if (!widget.importUrdfFile (draftUrdf, &projectError) ||
        findLineEdit (widget, "DraftBot") == NULL)
        return fail ("A selected URDF should populate RobotModelBuilder without a file dialog.");

    const QString projectDocument = projectDirectory.filePath ("robot-model.json");
    if (!widget.saveProjectDocument (projectDocument, &projectError))
        return fail ("Could not save the RobotModelBuilder project document.");
    QFile projectDocumentFile (projectDocument);
    if (!projectDocumentFile.open (QFile::ReadOnly) ||
        projectDocumentFile.readAll ().contains ("\"saveDirectory\""))
        return fail ("Project RobotModelBuilder JSON must not persist saveDirectory.");
    widget.markProjectDocumentClean ();
    if (widget.isProjectDocumentDirty ())
        return fail ("Freshly saved RobotModelBuilder project document should be clean.");

    // Project creation preflights both the original source and the managed copy. Neither pass
    // may replace the model currently being edited or reset its dirty baseline.
    robotName = findLineEdit (widget, "DraftBot");
    if (robotName == NULL)
        return fail ("The imported robot name field was not found.");
    robotName->setText ("UnsavedDraft");
    const QByteArray beforePreflight = QByteArray::fromStdString (
        rws::RobotModelSpecJson::toJson (widget.currentModelSpec ()));
    const bool dirtyBeforePreflight = widget.isProjectDocumentDirty ();
    if (!dirtyBeforePreflight)
        return fail ("The preflight baseline should contain an unsaved model change.");

    const QString preflightUrdf = projectDirectory.filePath ("PreflightBot.urdf");
    QFile preflightUrdfFile (preflightUrdf);
    if (!preflightUrdfFile.open (QFile::WriteOnly | QFile::Text) ||
        preflightUrdfFile.write (
            "<robot name=\"PreflightBot\"><link name=\"base\"/><link name=\"tip\"/>"
            "<joint name=\"joint1\" type=\"revolute\"><parent link=\"base\"/>"
            "<child link=\"tip\"/><axis xyz=\"0 0 1\"/>"
            "<limit lower=\"-1\" upper=\"1\" velocity=\"1\" effort=\"1\"/>"
            "</joint></robot>") < 0)
        return fail ("Could not create the URDF preflight source file.");
    preflightUrdfFile.close ();

    rws::RobotModelSpec preflightSpec;
    QStringList preflightWarnings;
    if (!widget.preflightUrdfFile (preflightUrdf, projectDirectory.path (), preflightSpec,
                                   preflightWarnings, &projectError))
        return fail ("A valid URDF should pass non-mutating preflight.");
    if (preflightSpec.robotName != "PreflightBot")
        return fail ("URDF preflight should return the parsed model.");
    if (QByteArray::fromStdString (
            rws::RobotModelSpecJson::toJson (widget.currentModelSpec ())) != beforePreflight)
        return fail ("URDF preflight must not replace the model currently being edited.");
    if (widget.isProjectDocumentDirty () != dirtyBeforePreflight)
        return fail ("URDF preflight must not change the project document dirty state.");

    if (!widget.loadProjectDocument (projectDocument, &projectError))
        return fail ("Could not restore the saved model after the preflight test.");
    robotName = findLineEdit (widget, "DraftBot");
    if (robotName == NULL)
        return fail ("The saved robot name was not restored after preflight.");

    // 从当前 WorkCell 导入的模型还没有对应的 JSON 资源。插件登记生成资源后，Widget 必须
    // 建立空基线，使下一次项目保存通过统一事务写入首个 .rmb.json，而不是在同步时直接落盘。
    // （英文原注：A model imported from the current WorkCell does not have an existing JSON
    //   resource yet. After the plugin registers that generated resource, the widget must
    //   establish an empty baseline so the next project save writes the initial .rmb.json
    //   through the transaction.）
    widget.beginGeneratedProjectDocument ();
    if (!widget.isProjectDocumentDirty ())
        return fail ("A newly registered generated RobotModelBuilder resource should be dirty.");
    widget.markProjectDocumentClean ();
    if (widget.isProjectDocumentDirty ())
        return fail ("A committed generated RobotModelBuilder resource should be clean.");

    robotName->setText ("ProjectSnapshotDirty");
    if (!widget.isProjectDocumentDirty ())
        return fail ("Changing a persisted model field should mark the project document dirty.");
    if (!widget.loadProjectDocument (projectDocument, &projectError))
        return fail ("Could not reload the RobotModelBuilder project document.");
    if (widget.isProjectDocumentDirty ())
        return fail ("Reloaded RobotModelBuilder project document should be clean.");

    robotName->setText ("OverwriteCheck");
    const QString deviceFilePath = QDir (generatedDirectory).filePath ("OverwriteCheck.wc.xml");
    QFile deviceFile (deviceFilePath);
    if (!deviceFile.open (QFile::WriteOnly | QFile::Text) ||
        deviceFile.write ("existing output") < 0)
        return fail ("Could not create an existing device XML file.");
    deviceFile.close ();

    bool confirmationShown = false;
    QTimer::singleShot (0, [&confirmationShown] () {
        confirmationShown = rejectOverwriteConfirmation ();
    });
    if (!QMetaObject::invokeMethod (&widget, "saveXml", Qt::DirectConnection))
        return fail ("Could not invoke Save XML.");
    if (!confirmationShown)
        return fail ("Saving over an existing XML file should ask for confirmation.");
    if (!deviceFile.open (QFile::ReadOnly | QFile::Text))
        return fail ("Could not re-open the existing device XML file.");
    const QByteArray savedContent = deviceFile.readAll ();
    deviceFile.close ();
    if (savedContent != "existing output")
        return fail ("Declining overwrite confirmation must leave existing XML unchanged.");

    // Exercise the actual project Provider save/load seam, including relocation under a hostile
    // process working directory. This catches accidental use of the CWD in Widget integration.
    QTemporaryDir portableProject;
    QTemporaryDir relocatedProject;
    QTemporaryDir hostileWorkingDirectory;
    if (!portableProject.isValid () || !relocatedProject.isValid () ||
        !hostileWorkingDirectory.isValid ())
        return fail ("Could not create portable Widget project fixtures.");

    const QString visualPath = portableProject.filePath ("assets/visual.stl");
    const QString collisionPath = portableProject.filePath ("assets/collision.stl");
    const QString scenePath = portableProject.filePath ("assets/scene.stl");
    if (!writeFixture (visualPath, "visual") || !writeFixture (collisionPath, "collision") ||
        !writeFixture (scenePath, "scene"))
        return fail ("Could not write portable Widget geometry fixtures.");

    rws::RobotModelSpec portableRuntime = widget.currentModelSpec ();
    portableRuntime.robotName = "PortableWidget";
    portableRuntime.drawables.clear ();
    portableRuntime.collisionModels.clear ();
    portableRuntime.sceneGeometries.clear ();
    rws::DrawableSpec visual;
    visual.name = "Visual";
    visual.shape = "Mesh";
    visual.filePath = QFileInfo (visualPath).absoluteFilePath ().toStdString ();
    portableRuntime.drawables.push_back (visual);
    rws::CollisionModelSpec collision;
    collision.name = "Collision";
    collision.shape = "Mesh";
    collision.filePath = QFileInfo (collisionPath).absoluteFilePath ().toStdString ();
    portableRuntime.collisionModels.push_back (collision);
    rws::SceneGeometrySpec scene;
    scene.name = "Scene";
    scene.kind = rws::GeometryKind::Mesh;
    scene.file = QFileInfo (scenePath).absoluteFilePath ().toStdString ();
    portableRuntime.sceneGeometries.push_back (scene);

    rws::RobotModelBuilderWidget portableWidget;
    portableWidget.setProjectOutputDirectory (portableProject.path ());
    portableWidget.syncFromWorkCellSpec (portableRuntime, {});
    const QString portableDocument = portableProject.filePath ("PortableWidget.rmb.json");
    if (!portableWidget.saveProjectDocument (portableDocument, &projectError))
        return fail ("Could not save a portable Widget project document.");
    QFile portableDocumentFile (portableDocument);
    if (!portableDocumentFile.open (QIODevice::ReadOnly))
        return fail ("Could not read the portable Widget project document.");
    const QByteArray portableJson = portableDocumentFile.readAll ();
    portableDocumentFile.close ();
    if (portableJson.contains (QFileInfo (portableProject.path ()).absoluteFilePath ().toUtf8 ()))
        return fail ("Widget project JSON must not contain its original absolute project root.");

    const QString relocatedDocument = relocatedProject.filePath ("PortableWidget.rmb.json");
    const QString relocatedVisual = relocatedProject.filePath ("assets/visual.stl");
    const QString relocatedCollision = relocatedProject.filePath ("assets/collision.stl");
    const QString relocatedScene = relocatedProject.filePath ("assets/scene.stl");
    if (!copyFixture (portableDocument, relocatedDocument) ||
        !copyFixture (visualPath, relocatedVisual) ||
        !copyFixture (collisionPath, relocatedCollision) ||
        !copyFixture (scenePath, relocatedScene))
        return fail ("Could not relocate the portable Widget project fixtures.");

    CurrentDirectoryGuard cwdGuard;
    if (!QDir::setCurrent (hostileWorkingDirectory.path ()))
        return fail ("Could not establish the hostile Widget working directory.");
    rws::RobotModelBuilderWidget relocatedWidget;
    relocatedWidget.setProjectOutputDirectory (relocatedProject.path ());
    if (!relocatedWidget.loadProjectDocument (relocatedDocument, &projectError))
        return fail ("Could not load the relocated Widget project document.");
    const rws::RobotModelSpec relocatedRuntime = relocatedWidget.currentModelSpec ();
    if (QFileInfo (QString::fromStdString (relocatedRuntime.drawables.front ().filePath))
                .absoluteFilePath () != QFileInfo (relocatedVisual).absoluteFilePath () ||
        QFileInfo (QString::fromStdString (relocatedRuntime.collisionModels.front ().filePath))
                .absoluteFilePath () != QFileInfo (relocatedCollision).absoluteFilePath () ||
        QFileInfo (QString::fromStdString (relocatedRuntime.sceneGeometries.front ().file))
                .absoluteFilePath () != QFileInfo (relocatedScene).absoluteFilePath ())
        return fail ("Widget runtime geometry did not relocate with the managed project.");

    return 0;
}
