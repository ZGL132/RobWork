#include "RobotModelBuilderWidget.hpp"
#include "RobotModelPublishService.hpp"
#include "RobotModelSpecJson.hpp"
#include "RobotModelXmlWriter.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QMessageBox>
#include <QProcess>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>

#include <cmath>
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

QTabWidget* findMainTabs (const rws::RobotModelBuilderWidget& widget)
{
    const QList< QTabWidget* > tabs = widget.findChildren< QTabWidget* > ();
    for (QTabWidget* tab : tabs) {
        for (int index = 0; index < tab->count (); ++index) {
            if (tab->tabText (index) == "XML Preview")
                return tab;
        }
    }
    return NULL;
}

QLineEdit* findStatusLine (const rws::RobotModelBuilderWidget& widget)
{
    const QList< QLineEdit* > lines = widget.findChildren< QLineEdit* > ();
    for (QLineEdit* line : lines) {
        if (line->isReadOnly ())
            return line;
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
                         const QString& firstHeader, const QString& secondHeader,
                         int columnCount);

bool verifyMeshToAutoLinkConversion (QString* failure)
{
    QTemporaryDir temporary;
    if (!temporary.isValid ()) {
        *failure = "Could not create a temporary directory for auto-link conversion test.";
        return false;
    }

    const QString meshPath = QDir (temporary.path ()).filePath ("link1.stl");
    QFile mesh (meshPath);
    if (!mesh.open (QFile::WriteOnly | QFile::Text) || mesh.write ("solid link1\nendsolid link1\n") < 0) {
        *failure = "Could not create the test STL file.";
        return false;
    }
    mesh.close ();

    const QString urdfPath = QDir (temporary.path ()).filePath ("simplified.urdf");
    QFile urdf (urdfPath);
    if (!urdf.open (QFile::WriteOnly | QFile::Text)) {
        *failure = "Could not create the auto-link conversion URDF.";
        return false;
    }
    QTextStream out (&urdf);
    out << "<robot name=\"SimplifiedBot\">\n"
        << "  <link name=\"base\"/>\n"
        << "  <link name=\"link1\"><visual name=\"link1_visual\"><geometry>"
        << "<mesh filename=\"link1.stl\"/></geometry></visual>"
        << "<collision name=\"link1_collision\"><geometry>"
        << "<mesh filename=\"link1.stl\"/></geometry></collision></link>\n"
        << "  <link name=\"link2\"/>\n"
        << "  <joint name=\"Joint1\" type=\"revolute\"><parent link=\"base\"/>"
        << "<child link=\"link1\"/><origin xyz=\"0 0 0\"/></joint>\n"
        << "  <joint name=\"Joint2\" type=\"revolute\"><parent link=\"link1\"/>"
        << "<child link=\"link2\"/><origin xyz=\"0.12 0.50 0.12\"/></joint>\n"
        << "</robot>\n";
    urdf.close ();

    rws::RobotModelBuilderWidget widget;
    QString importError;
    if (!widget.importUrdfFile (urdfPath, &importError)) {
        *failure = "Auto-link conversion URDF import failed: " + importError;
        return false;
    }

    QTableWidget* drawables = findTable (widget, "Name", "RefFrame", 10);
    if (drawables == NULL) {
        *failure = "Could not find Drawables table for auto-link conversion test.";
        return false;
    }
    int visualRow = -1;
    for (int row = 0; row < drawables->rowCount (); ++row) {
        if (drawables->item (row, 0) != NULL &&
            drawables->item (row, 0)->text () == "link1_visual") {
            visualRow = row;
            break;
        }
    }
    if (visualRow < 0) {
        *failure = "Imported STL visual was not found in Drawables table.";
        return false;
    }

    QComboBox* shape = qobject_cast< QComboBox* > (drawables->cellWidget (visualRow, 2));
    if (shape == NULL) {
        *failure = "Imported Drawable Shape cell is not a combo box.";
        return false;
    }
    shape->setCurrentText ("Cylinder");
    QApplication::processEvents ();

    const rws::RobotModelSpec converted = widget.currentModelSpec ();
    const rws::DrawableSpec* drawable = NULL;
    for (const rws::DrawableSpec& candidate : converted.drawables) {
        if (candidate.name == "link1_visual") {
            drawable = &candidate;
            break;
        }
    }
    if (drawable == NULL || !drawable->autoLinkGeometry || drawable->shape != "Cylinder" ||
        !drawable->filePath.empty ()) {
        *failure = "STL to Cylinder conversion did not enter explicit auto-link mode.";
        return false;
    }
    const double expectedLength = std::sqrt (0.12 * 0.12 + 0.50 * 0.50 + 0.12 * 0.12);
    if (std::abs (drawable->length - expectedLength) > 1e-6 ||
        std::abs (drawable->pos[0] - 0.06) > 1e-6 ||
        std::abs (drawable->pos[1] - 0.25) > 1e-6 ||
        std::abs (drawable->pos[2] - 0.06) > 1e-6) {
        *failure = "Converted Cylinder did not receive the adjacent-joint length and pose.";
        return false;
    }
    bool disabledOriginalCollision = false;
    for (const rws::CollisionModelSpec& collision : converted.collisionModels) {
        if (collision.refFrame == drawable->refFrame &&
            collision.filePath == QFileInfo (meshPath).absoluteFilePath ().toStdString () &&
            !collision.enabled) {
            disabledOriginalCollision = true;
            break;
        }
    }
    if (!disabledOriginalCollision) {
        *failure = "Simplifying a Drawable must disable its original mesh collision model.";
        return false;
    }

    QComboBox* convertedShape =
        qobject_cast< QComboBox* > (drawables->cellWidget (visualRow, 2));
    convertedShape->setCurrentText ("STL");
    QApplication::processEvents ();
    const rws::RobotModelSpec manual = widget.currentModelSpec ();
    for (const rws::DrawableSpec& candidate : manual.drawables) {
        if (candidate.name == "link1_visual" && candidate.autoLinkGeometry) {
            *failure = "Switching back to STL did not leave auto-link mode.";
            return false;
        }
    }
    for (const rws::CollisionModelSpec& collision : manual.collisionModels) {
        if (collision.refFrame == drawable->refFrame &&
            collision.filePath == QFileInfo (meshPath).absoluteFilePath ().toStdString () &&
            collision.enabled) {
            *failure = "Returning to a mesh Drawable must not implicitly re-enable its disabled collision model.";
            return false;
        }
    }
    return true;
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

QByteArray readFixture (const QString& path)
{
    QFile file (path);
    if (!file.open (QIODevice::ReadOnly))
        return QByteArray ();
    return file.readAll ();
}

QStringList publishTargets (const rws::RobotModelSpec& spec)
{
    QStringList targets {rws::RobotModelXmlWriter::serialDeviceFilePath (spec)};
    if (spec.generateScene) {
        if (spec.collisionSetup.enabled)
            targets << rws::RobotModelXmlWriter::collisionSetupFilePath (spec);
        if (spec.proximitySetup.enabled)
            targets << rws::RobotModelXmlWriter::proximitySetupFilePath (spec);
        targets << rws::RobotModelXmlWriter::sceneFilePath (spec);
    }
    if (spec.dynamics.generateDynamicWorkCell)
        targets << rws::RobotModelXmlWriter::dynamicWorkCellFilePath (spec);
    targets.removeDuplicates ();
    return targets;
}

quint32 readU32 (const QByteArray& bytes, int offset, bool* ok = NULL)
{
    const bool valid = offset >= 0 && offset <= bytes.size () - 4;
    if (ok != NULL)
        *ok = valid;
    if (!valid)
        return 0;
    const uchar* data = reinterpret_cast< const uchar* > (bytes.constData () + offset);
    return (quint32 (data[0]) << 24) | (quint32 (data[1]) << 16) |
           (quint32 (data[2]) << 8) | quint32 (data[3]);
}

void writeU32 (QByteArray& bytes, int offset, quint32 value)
{
    bytes[offset] = char ((value >> 24) & 0xff);
    bytes[offset + 1] = char ((value >> 16) & 0xff);
    bytes[offset + 2] = char ((value >> 8) & 0xff);
    bytes[offset + 3] = char (value & 0xff);
}

void appendU32 (QByteArray& bytes, quint32 value)
{
    const int offset = bytes.size ();
    bytes.resize (offset + 4);
    writeU32 (bytes, offset, value);
}

bool skipField (const QByteArray& bytes, int& offset)
{
    bool ok = false;
    const quint32 size = readU32 (bytes, offset, &ok);
    if (!ok || size > quint32 (bytes.size () - offset - 4))
        return false;
    offset += 4 + int (size);
    return true;
}

bool locateEditableState (const QByteArray& snapshot, int& lengthOffset, int& dataOffset,
                          int& dataSize)
{
    if (readU32 (snapshot, 0) != quint32 (0x524d4253) || readU32 (snapshot, 4) != 3)
        return false;
    int offset = 8;
    if (!skipField (snapshot, offset) || !skipField (snapshot, offset) ||
        !skipField (snapshot, offset) || offset >= snapshot.size ())
        return false;
    ++offset;
    if (!skipField (snapshot, offset))
        return false;
    lengthOffset = offset;
    bool ok = false;
    const quint32 size = readU32 (snapshot, offset, &ok);
    if (!ok || size > quint32 (snapshot.size () - offset - 4))
        return false;
    dataOffset = offset + 4;
    dataSize = int (size);
    return true;
}

bool locateModeData (const QByteArray& editable, int& modeStart, int& variantTag,
                     int& modeEnd)
{
    bool ok = false;
    const quint32 lineEditCount = readU32 (editable, 0, &ok);
    if (!ok)
        return false;
    int offset = 4;
    for (quint32 index = 0; index < lineEditCount; ++index) {
        if (!skipField (editable, offset) || offset > editable.size () - 3)
            return false;
        offset += 3;
    }
    modeStart = offset;
    const quint32 itemCount = readU32 (editable, offset, &ok);
    if (!ok)
        return false;
    offset += 4;
    for (quint32 index = 0; index < itemCount; ++index) {
        if (!skipField (editable, offset))
            return false;
    }
    const quint32 dataCount = readU32 (editable, offset, &ok);
    if (!ok || dataCount == 0)
        return false;
    offset += 4;
    variantTag = offset;
    for (quint32 index = 0; index < dataCount; ++index) {
        if (offset >= editable.size ())
            return false;
        const quint8 tag = quint8 (editable[offset++]);
        if (tag == 1) {
            if (!skipField (editable, offset))
                return false;
        }
        else if ((tag >= 2 && tag <= 5) || tag == 8) {
            if (offset > editable.size () - 8)
                return false;
            offset += 8;
        }
        else if (tag == 6) {
            if (offset >= editable.size ())
                return false;
            ++offset;
        }
        else if (tag != 0 && tag != 7) {
            return false;
        }
    }
    if (offset > editable.size () - 4)
        return false;
    offset += 4;
    if (!skipField (editable, offset) || offset > editable.size () - 2)
        return false;
    modeEnd = offset + 2;
    return true;
}

bool replaceEditableState (const QByteArray& snapshot, const QByteArray& editable,
                           QByteArray& corrupted)
{
    int lengthOffset = 0;
    int dataOffset = 0;
    int dataSize = 0;
    if (!locateEditableState (snapshot, lengthOffset, dataOffset, dataSize))
        return false;
    corrupted = snapshot.left (dataOffset) + editable + snapshot.mid (dataOffset + dataSize);
    writeU32 (corrupted, lengthOffset, quint32 (editable.size ()));
    return true;
}

bool makeHostileWidgetSnapshots (const QByteArray& snapshot, QList< QByteArray >& corrupted)
{
    int editableLengthOffset = 0;
    int editableOffset = 0;
    int editableSize = 0;
    if (!locateEditableState (
            snapshot, editableLengthOffset, editableOffset, editableSize))
        return false;
    const QByteArray editable = snapshot.mid (editableOffset, editableSize);
    int modeStart = 0;
    int variantTag = 0;
    int modeEnd = 0;
    if (!locateModeData (editable, modeStart, variantTag, modeEnd))
        return false;

    QByteArray hugeOuterString = snapshot;
    writeU32 (hugeOuterString, 8, quint32 (0xffffffff));
    corrupted.push_back (hugeOuterString);

    int cleanSnapshotLength = 8;
    if (!skipField (snapshot, cleanSnapshotLength) ||
        !skipField (snapshot, cleanSnapshotLength))
        return false;
    QByteArray hugeOuterBytes = snapshot;
    writeU32 (hugeOuterBytes, cleanSnapshotLength, quint32 (0xffffffff));
    corrupted.push_back (hugeOuterBytes);

    QByteArray hugeNestedString = editable;
    writeU32 (hugeNestedString, 4, quint32 (0xffffffff));
    QByteArray hostile;
    if (!replaceEditableState (snapshot, hugeNestedString, hostile))
        return false;
    corrupted.push_back (hostile);

    QByteArray unknownVariant = editable;
    unknownVariant[variantTag] = char (0xff);
    if (!replaceEditableState (snapshot, unknownVariant, hostile))
        return false;
    corrupted.push_back (hostile);

    QByteArray hugeVariant = editable;
    hugeVariant[variantTag] = char (1);
    writeU32 (hugeVariant, variantTag + 1, quint32 (0xffffffff));
    if (!replaceEditableState (snapshot, hugeVariant, hostile))
        return false;
    corrupted.push_back (hostile);

    QByteArray saturatedMode;
    appendU32 (saturatedMode, 10000);
    for (int index = 0; index < 10000; ++index)
        appendU32 (saturatedMode, 0);
    appendU32 (saturatedMode, 10000);
    saturatedMode.append (QByteArray (10000, char (0)));
    appendU32 (saturatedMode, 0);
    appendU32 (saturatedMode, 0);
    saturatedMode.append (char (0));
    saturatedMode.append (char (1));
    const QByteArray aggregateComboItems = editable.left (modeStart) + saturatedMode +
                                           editable.mid (modeEnd);
    if (!replaceEditableState (snapshot, aggregateComboItems, hostile))
        return false;
    corrupted.push_back (hostile);
    return true;
}

bool hasTransactionResidue (const QString& directory)
{
    QDirIterator iterator (directory,
                           {QStringLiteral ("*.rwstage-*"), QStringLiteral ("*.rwbackup-*")},
                           QDir::Files,
                           QDirIterator::Subdirectories);
    return iterator.hasNext ();
}

bool seedPublishTargets (const rws::RobotModelSpec& spec)
{
    for (const QString& target : publishTargets (spec)) {
        if (!writeFixture (target, QByteArray ("old:") + QFileInfo (target).fileName ().toUtf8 ()))
            return false;
    }
    return true;
}

bool oldPublishTargetsRemain (const rws::RobotModelSpec& spec)
{
    for (const QString& target : publishTargets (spec)) {
        if (readFixture (target) !=
            QByteArray ("old:") + QFileInfo (target).fileName ().toUtf8 ())
            return false;
    }
    return true;
}

#ifdef Q_OS_WIN
bool runWindowsCommand (const QString& command)
{
    QProcess process;
    process.setProgram (QStringLiteral ("cmd.exe"));
    process.setNativeArguments (QStringLiteral ("/D /C ") + command);
    process.start ();
    return process.waitForFinished () && process.exitStatus () == QProcess::NormalExit &&
           process.exitCode () == 0;
}

bool createDirectoryJunction (const QString& linkPath, const QString& targetPath)
{
    return runWindowsCommand (
        QStringLiteral ("mklink /J \"%1\" \"%2\"")
            .arg (QDir::toNativeSeparators (linkPath), QDir::toNativeSeparators (targetPath)));
}

class DirectoryJunctionCleanup
{
  public:
    explicit DirectoryJunctionCleanup (const QString& path) : _path (path) {}
    ~DirectoryJunctionCleanup ()
    {
        if (!_path.isEmpty ())
            runWindowsCommand (QStringLiteral ("rmdir \"%1\"").arg (_path));
    }

  private:
    QString _path;
};
#endif

bool newPublishTargetsInstalled (const rws::RobotModelSpec& spec)
{
    for (const QString& target : publishTargets (spec)) {
        const QByteArray bytes = readFixture (target);
        if (bytes.isEmpty () ||
            bytes == QByteArray ("old:") + QFileInfo (target).fileName ().toUtf8 ())
            return false;
    }
    return true;
}

bool verifyTransactionalPublishService (QString* failure)
{
    {
        QTemporaryDir project;
        if (!project.isValid ()) {
            *failure = "Could not create the successful publish fixture.";
            return false;
        }
        const QString output = QDir (project.path ()).filePath ("generated/robot-models");
        if (!QDir ().mkpath (output)) {
            *failure = "Could not create the successful publish output directory.";
            return false;
        }
        rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel (output);
        spec.generateScene = true;
        if (!seedPublishTargets (spec)) {
            *failure = "Could not seed successful publish targets.";
            return false;
        }

        bool promoted = false;
        rws::RobotModelPublishRequest request;
        request.spec = spec;
        request.projectRoot = project.path ();
        request.promote = [&] (const QString& scene, const QStringList& dependencies,
                               QString*) {
            promoted = scene == rws::RobotModelXmlWriter::sceneFilePath (spec) &&
                       dependencies.contains (
                           rws::RobotModelXmlWriter::serialDeviceFilePath (spec)) &&
                       dependencies.contains (
                           rws::RobotModelXmlWriter::collisionSetupFilePath (spec));
            return promoted;
        };
        QString error;
        if (!rws::RobotModelPublishService::publishAndLoad (request, &error) || !promoted) {
            *failure = QStringLiteral ("Successful transactional publish failed: %1").arg (error);
            return false;
        }
        if (!newPublishTargetsInstalled (spec) || hasTransactionResidue (output)) {
            *failure = "Successful publish did not replace every output cleanly.";
            return false;
        }
    }

#ifdef Q_OS_WIN
    {
        QTemporaryDir project;
        QTemporaryDir external;
        if (!project.isValid () || !external.isValid ()) {
            *failure = "Could not create the junction rollback fixture.";
            return false;
        }
        const QString output = QDir (project.path ()).filePath ("generated/robot-models");
        QDir ().mkpath (output);
        rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel (output);
        spec.generateScene = true;
        if (!seedPublishTargets (spec)) {
            *failure = "Could not seed junction rollback targets.";
            return false;
        }
        const QString scenePath = rws::RobotModelXmlWriter::sceneFilePath (spec);
        const QString externalScene = QDir (external.path ()).filePath (QFileInfo (scenePath).fileName ());
        if (!writeFixture (externalScene, "external-scene")) {
            *failure = "Could not seed the external junction scene.";
            return false;
        }
        const QString parkedOutput = QDir (project.path ()).filePath ("parked-robot-models");
        DirectoryJunctionCleanup cleanup (output);
        rws::RobotModelPublishRequest request;
        request.spec = spec;
        request.projectRoot = project.path ();
        request.promote = [&] (const QString&, const QStringList&, QString* error) {
            if (!QDir ().rename (output, parkedOutput) ||
                !createDirectoryJunction (output, external.path ())) {
                *error = QStringLiteral ("Could not replace the installed output with a junction.");
                return false;
            }
            *error = QStringLiteral ("promotion rejected after directory replacement");
            return false;
        };
        QString error;
        QStringList parkedBackups;
        QStringList sceneBackups;
        if (rws::RobotModelPublishService::publishAndLoad (request, &error) ||
            readFixture (externalScene) != QByteArray ("external-scene") ||
            (!error.contains (QStringLiteral ("outside the project root")) &&
             !error.contains (QStringLiteral ("reparse point"))) ||
            QDir (external.path ()).entryList ({QStringLiteral ("*.rwbackup-*")}, QDir::Files).size () != 0 ||
            (parkedBackups = QDir (parkedOutput).entryList (
                 {QStringLiteral ("*.rwbackup-*")}, QDir::Files)).isEmpty () ||
            (sceneBackups = QDir (parkedOutput).entryList (
                 {QFileInfo (scenePath).completeBaseName () + QStringLiteral (".rwbackup-*")},
                 QDir::Files)).size () != 1 ||
            error.contains (QStringLiteral ("could not verify a safe backup recovery path")) ||
            error.contains (QStringLiteral ("Original transaction backup is available for recovery: %1")
                                .arg (QDir (output).filePath (sceneBackups.front ()))) ||
            !error.contains (QDir (parkedOutput).filePath (sceneBackups.front ())) ||
            readFixture (QDir (parkedOutput).filePath (sceneBackups.front ())) !=
                QByteArray ("old:") + QFileInfo (scenePath).fileName ().toUtf8 ()) {
            *failure = QStringLiteral ("Rollback did not reject a post-install junction safely: %1").arg (error);
            return false;
        }
    }
#endif

    {
        QTemporaryDir project;
        if (!project.isValid ()) {
            *failure = "Could not create the external promotion rollback fixture.";
            return false;
        }
        const QString output = QDir (project.path ()).filePath ("generated/robot-models");
        QDir ().mkpath (output);
        rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel (output);
        spec.generateScene = true;
        if (!seedPublishTargets (spec)) {
            *failure = "Could not seed external promotion rollback targets.";
            return false;
        }
        rws::RobotModelPublishRequest request;
        request.spec = spec;
        request.projectRoot = project.path ();
        request.promote = [&] (const QString& scene, const QStringList&, QString* error) {
            if (!writeFixture (scene, "external-scene")) {
                *error = QStringLiteral ("Could not write external scene replacement.");
                return false;
            }
            *error = QStringLiteral ("promotion rejected after external replacement");
            return false;
        };
        QString error;
        const QString scenePath = rws::RobotModelXmlWriter::sceneFilePath (spec);
        if (rws::RobotModelPublishService::publishAndLoad (request, &error) ||
            readFixture (scenePath) != QByteArray ("external-scene")) {
            *failure = "Promotion rollback did not preserve the external scene replacement.";
            return false;
        }
        const QStringList backups = QDir (output).entryList (
            {QStringLiteral ("*.rwbackup-*")}, QDir::Files);
        if (backups.size () != 1) {
            *failure = "Promotion rollback did not retain exactly one recovery backup.";
            return false;
        }
        const QString backupPath = QDir (output).filePath (backups.front ());
        if (readFixture (backupPath) !=
                QByteArray ("old:") + QFileInfo (scenePath).fileName ().toUtf8 () ||
            !error.contains (backupPath)) {
            *failure = "Promotion rollback did not report the recovery backup path.";
            return false;
        }
    }

    {
        QTemporaryDir project;
        if (!project.isValid ()) {
            *failure = "Could not create the promotion rollback fixture.";
            return false;
        }
        const QString output = QDir (project.path ()).filePath ("generated/robot-models");
        QDir ().mkpath (output);
        rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel (output);
        spec.generateScene = true;
        if (!seedPublishTargets (spec)) {
            *failure = "Could not seed promotion rollback targets.";
            return false;
        }
        rws::RobotModelPublishRequest request;
        request.spec = spec;
        request.projectRoot = project.path ();
        request.promote = [] (const QString&, const QStringList&, QString* error) {
            *error = QStringLiteral ("promotion rejected");
            return false;
        };
        QString error;
        if (rws::RobotModelPublishService::publishAndLoad (request, &error) ||
            !error.contains (QStringLiteral ("promotion rejected")) ||
            !oldPublishTargetsRemain (spec) || hasTransactionResidue (output)) {
            *failure = "Promotion failure did not restore all previous publish targets.";
            return false;
        }
    }

    {
        QTemporaryDir project;
        if (!project.isValid ()) {
            *failure = "Could not create the load rollback fixture.";
            return false;
        }
        const QString output = QDir (project.path ()).filePath ("generated/robot-models");
        QDir ().mkpath (output);
        rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel (output);
        spec.generateScene = true;
        spec.exportLayout.sceneFile = spec.exportLayout.deviceFile = "shared.wc.xml";
        if (!seedPublishTargets (spec)) {
            *failure = "Could not seed load rollback targets.";
            return false;
        }
        bool promotionCalled = false;
        rws::RobotModelPublishRequest request;
        request.spec = spec;
        request.projectRoot = project.path ();
        request.promote = [&] (const QString&, const QStringList&, QString*) {
            promotionCalled = true;
            return true;
        };
        QString error;
        if (rws::RobotModelPublishService::publishAndLoad (request, &error) || promotionCalled ||
            error.isEmpty () || !oldPublishTargetsRemain (spec) ||
            hasTransactionResidue (output)) {
            *failure = "WorkCell load failure did not restore all previous publish targets.";
            return false;
        }
    }

    {
        QTemporaryDir project;
        if (!project.isValid ()) {
            *failure = "Could not create the staging rollback fixture.";
            return false;
        }
        const QString output = QDir (project.path ()).filePath ("generated/robot-models");
        QDir ().mkpath (output);
        rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel (output);
        spec.generateScene = true;
        spec.exportLayout.sceneFile = "blocked/scene.wc.xml";
        const QString blockedParent = QDir (output).filePath ("blocked");
        if (!writeFixture (blockedParent, "blocking-file")) {
            *failure = "Could not create the staging failure sentinel.";
            return false;
        }
        for (const QString& target : publishTargets (spec)) {
            if (target != QDir (output).filePath ("blocked/scene.wc.xml") &&
                !writeFixture (target,
                               QByteArray ("old:") + QFileInfo (target).fileName ().toUtf8 ())) {
                *failure = "Could not seed staging rollback targets.";
                return false;
            }
        }
        rws::RobotModelPublishRequest request;
        request.spec = spec;
        request.projectRoot = project.path ();
        request.promote = [] (const QString&, const QStringList&, QString*) { return true; };
        QString error;
        if (rws::RobotModelPublishService::publishAndLoad (request, &error) || error.isEmpty () ||
            readFixture (blockedParent) != QByteArray ("blocking-file") ||
            readFixture (rws::RobotModelXmlWriter::serialDeviceFilePath (spec)) !=
                QByteArray ("old:GenericSixAxis.wc.xml") ||
            readFixture (rws::RobotModelXmlWriter::collisionSetupFilePath (spec)) !=
                QByteArray ("old:CollisionSetup.xml") ||
            hasTransactionResidue (output)) {
            *failure = "Staging failure changed old outputs or left transaction residue.";
            return false;
        }
    }

    {
        QTemporaryDir project;
        QTemporaryDir outside;
        if (!project.isValid () || !outside.isValid ()) {
            *failure = "Could not create the escaped output fixture.";
            return false;
        }
        const QString output = QDir (project.path ()).filePath ("generated/robot-models");
        QDir ().mkpath (output);
        const QString outsideTarget = QDir (outside.path ()).filePath ("outside.wc.xml");
        if (!writeFixture (outsideTarget, "outside-sentinel")) {
            *failure = "Could not seed the escaped output sentinel.";
            return false;
        }
        rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel (output);
        spec.generateScene = true;
        spec.exportLayout.sceneFile =
            QDir (output).relativeFilePath (outsideTarget).toStdString ();
        bool promotionCalled = false;
        rws::RobotModelPublishRequest request;
        request.spec = spec;
        request.projectRoot = project.path ();
        request.promote = [&] (const QString&, const QStringList&, QString*) {
            promotionCalled = true;
            return true;
        };
        QString error;
        if (rws::RobotModelPublishService::publishAndLoad (request, &error) || promotionCalled ||
            !error.contains (QStringLiteral ("outside the project")) ||
            readFixture (outsideTarget) != QByteArray ("outside-sentinel") ||
            hasTransactionResidue (project.path ()) || hasTransactionResidue (outside.path ())) {
            *failure = "Escaped publish output was not rejected before staging.";
            return false;
        }
    }

    return true;
}

bool verifyProjectModeSaveAndLoad (QString* failure)
{
    QTemporaryDir project;
    if (!project.isValid ()) {
        *failure = "Could not create the project-mode Save and Load fixture.";
        return false;
    }

    rws::RobotModelBuilderWidget widget;
    widget.setProjectOutputDirectory (project.path ());
    widget.beginGeneratedProjectDocument ();
    bool promoted = false;
    widget.setProjectPublishPromoter (
        [&] (const QString& path, const QStringList&, QString*) {
            promoted = QFileInfo (path).isFile ();
            return promoted;
        });
    const rws::RobotModelSpec before = widget.currentModelSpec ();
    if (!widget.isProjectDocumentDirty ()) {
        *failure = "Generated project model should start dirty before Save and Load.";
        return false;
    }
    if (!QMetaObject::invokeMethod (&widget, "saveAndLoad", Qt::DirectConnection)) {
        *failure = "Could not invoke project-mode Save and Load.";
        return false;
    }
    if (!promoted) {
        *failure = "Project-mode Save and Load did not invoke transactional promotion.";
        return false;
    }
    if (QFileInfo::exists (rws::RobotModelXmlWriter::specSidecarFilePath (before))) {
        *failure = "Project-mode Save and Load wrote an unmanaged .rmb.json sidecar.";
        return false;
    }
    if (!widget.isProjectDocumentDirty ()) {
        *failure = "Project-mode Save and Load cleared the managed model before File > Save Project.";
        return false;
    }
    return true;
}

bool verifyWidgetStateSnapshotRoundTrip (QString* failure)
{
    QTemporaryDir originalProject;
    QTemporaryDir candidateProject;
    if (!originalProject.isValid () || !candidateProject.isValid ()) {
        *failure = "Could not create Widget snapshot project fixtures.";
        return false;
    }

    rws::RobotModelBuilderWidget widget;
    widget.setProjectOutputDirectory (originalProject.path ());
    rws::RobotModelSpec original =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel (originalProject.path ());
    original.robotName = "SnapshotOriginal";
    original.imported.active = true;
    original.imported.sourceDeviceFile = "vendor/ImportedDevice.wc.xml";
    original.imported.sourceSceneFile = "vendor/ImportedScene.wc.xml";
    original.imported.workcellExtensions.push_back ("<Property name=\"snapshot\"/>");
    widget.syncFromWorkCellSpec (original, {});
    widget.beginGeneratedProjectDocument ();
    widget.markProjectDocumentClean ();

    QLineEdit* robotName = findLineEdit (widget, "SnapshotOriginal");
    if (robotName == NULL) {
        *failure = "Could not find the Widget snapshot robot name field.";
        return false;
    }
    robotName->setText ("SnapshotEdited");
    if (!QMetaObject::invokeMethod (&widget, "generatePreview", Qt::DirectConnection)) {
        *failure = "Could not establish the Widget snapshot preview state.";
        return false;
    }
    robotName->setText ("  Snapshot partial  ");
    QTableWidget* limits = findTable (widget, "Joint", "PosMin", 5);
    QTableWidget* transforms = findTable (widget, "Joint", "Type", 4);
    QCheckBox* sceneGeneration = NULL;
    const QList< QCheckBox* > checkboxes = widget.findChildren< QCheckBox* > ();
    for (QCheckBox* checkbox : checkboxes) {
        if (checkbox->text () == "Generate Scene file") {
            sceneGeneration = checkbox;
            break;
        }
    }
    if (limits == NULL || transforms == NULL || sceneGeneration == NULL ||
        limits->item (0, 1) == NULL || transforms->item (0, 2) == NULL) {
        *failure = "Could not find the raw Widget state used by the snapshot test.";
        return false;
    }
    {
        const QSignalBlocker limitsBlocker (limits);
        const QSignalBlocker transformsBlocker (transforms);
        limits->item (0, 1)->setText ("1e-");
        transforms->item (0, 2)->setText ("90 0 +");
    }
    QComboBox* limitJoint = qobject_cast< QComboBox* > (limits->cellWidget (0, 0));
    if (limitJoint == NULL) {
        *failure = "Could not find the table combo used by the snapshot test.";
        return false;
    }
    limitJoint->addItem ("TransientJoint");
    limitJoint->setCurrentText ("TransientJoint");
    {
        const QSignalBlocker sceneBlocker (sceneGeneration);
        sceneGeneration->setChecked (!sceneGeneration->isChecked ());
    }
    if (!widget.isProjectDocumentDirty ()) {
        *failure = "Could not establish the dirty Widget snapshot state.";
        return false;
    }
    QTabWidget* previewTabs = findPreviewTabs (widget);
    QTabWidget* mainTabs = findMainTabs (widget);
    QLineEdit* status = findStatusLine (widget);
    if (previewTabs == NULL || mainTabs == NULL || status == NULL) {
        *failure = "Could not find the visible Widget state used by the snapshot test.";
        return false;
    }
    previewTabs->setCurrentIndex (1);
    for (int index = 0; index < mainTabs->count (); ++index) {
        if (mainTabs->tabText (index) == "XML Preview")
            mainTabs->setCurrentIndex (index);
    }
    widget.setProjectStatus ("Snapshot status sentinel");

    const QByteArray originalCanonical = QByteArray::fromStdString (
        rws::RobotModelSpecJson::toJson (widget.currentModelSpec ()));
    const QString originalOutputDirectory = widget.projectOutputDirectory ();
    const bool originalDirty = widget.isProjectDocumentDirty ();
    const QString originalRobotName = robotName->text ();
    const QString originalLimitText = limits->item (0, 1)->text ();
    const QString originalTransformText = transforms->item (0, 2)->text ();
    const QStringList originalLimitChoices = [&] () {
        QStringList choices;
        for (int index = 0; index < limitJoint->count (); ++index)
            choices.push_back (limitJoint->itemText (index));
        return choices;
    } ();
    const QString originalLimitSelection = limitJoint->currentText ();
    const bool originalSceneGeneration = sceneGeneration->isChecked ();
    const QString originalStatus = status->text ();
    const int originalMainTab = mainTabs->currentIndex ();
    const int originalPreviewTab = previewTabs->currentIndex ();
    QStringList originalPreviews;
    for (int index = 0; index < previewTabs->count (); ++index) {
        QTextEdit* preview = qobject_cast< QTextEdit* > (previewTabs->widget (index));
        originalPreviews.push_back (preview == NULL ? QString () : preview->toPlainText ());
    }

    QByteArray snapshot;
    QString error;
    if (!widget.snapshotProjectDocumentState (snapshot, &error)) {
        *failure = "Could not snapshot the real RobotModelBuilderWidget state.";
        return false;
    }

    rws::RobotModelSpec candidate =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel (candidateProject.path ());
    candidate.robotName = "SnapshotCandidate";
    candidate.imported.active = true;
    candidate.imported.sourceDeviceFile = "candidate/Device.wc.xml";
    widget.syncFromWorkCellSpec (candidate, {});
    widget.setProjectOutputDirectory (candidateProject.path ());
    widget.beginGeneratedProjectDocument ();
    QMetaObject::invokeMethod (&widget, "generatePreview", Qt::DirectConnection);
    previewTabs->setCurrentIndex (0);
    mainTabs->setCurrentIndex (0);
    widget.setProjectStatus ("Candidate status");

    const QByteArray candidateCanonical = QByteArray::fromStdString (
        rws::RobotModelSpecJson::toJson (widget.currentModelSpec ()));
    const QString candidateOutputDirectory = widget.projectOutputDirectory ();
    QList< QByteArray > corruptedSnapshots;
    corruptedSnapshots.push_back (snapshot.left (snapshot.size () * 2 / 3));
    if (!makeHostileWidgetSnapshots (snapshot, corruptedSnapshots)) {
        *failure = "Could not build the bounded hostile Widget snapshot fixtures.";
        return false;
    }
    for (int index = 0; index < corruptedSnapshots.size (); ++index) {
        QString malformedError;
        if (widget.restoreProjectDocumentState (corruptedSnapshots[index], &malformedError) ||
            QByteArray::fromStdString (
                rws::RobotModelSpecJson::toJson (widget.currentModelSpec ())) != candidateCanonical ||
            widget.projectOutputDirectory () != candidateOutputDirectory ||
            status->text () != "Candidate status" ||
            findLineEdit (widget, "SnapshotCandidate") == NULL) {
            *failure = QString ("Hostile Widget snapshot fixture %1 changed live state or was accepted.")
                           .arg (index);
            return false;
        }
    }

    if (!widget.restoreProjectDocumentState (snapshot, &error)) {
        *failure = "Could not restore the real RobotModelBuilderWidget state: " + error;
        return false;
    }
    const QByteArray restoredCanonical = QByteArray::fromStdString (
        rws::RobotModelSpecJson::toJson (widget.currentModelSpec ()));
    if (restoredCanonical != originalCanonical ||
        widget.projectOutputDirectory () != originalOutputDirectory ||
        widget.isProjectDocumentDirty () != originalDirty) {
        *failure = "Widget snapshot restore changed canonical data, project root, or dirty baseline.";
        return false;
    }
    robotName = findLineEdit (widget, originalRobotName);
    limits = findTable (widget, "Joint", "PosMin", 5);
    transforms = findTable (widget, "Joint", "Type", 4);
    limitJoint = limits == NULL ? NULL :
        qobject_cast< QComboBox* > (limits->cellWidget (0, 0));
    QStringList restoredLimitChoices;
    if (limitJoint != NULL) {
        for (int index = 0; index < limitJoint->count (); ++index)
            restoredLimitChoices.push_back (limitJoint->itemText (index));
    }
    if (robotName == NULL || limits == NULL || transforms == NULL || limitJoint == NULL ||
        limits->item (0, 1) == NULL || transforms->item (0, 2) == NULL ||
        limits->item (0, 1)->text () != originalLimitText ||
        transforms->item (0, 2)->text () != originalTransformText ||
        restoredLimitChoices != originalLimitChoices ||
        limitJoint->currentText () != originalLimitSelection ||
        sceneGeneration->isChecked () != originalSceneGeneration) {
        *failure = "Widget snapshot restore normalized or lost raw editable state.";
        return false;
    }
    if (!widget.currentModelSpec ().imported.active ||
        widget.currentModelSpec ().imported.sourceDeviceFile !=
            original.imported.sourceDeviceFile) {
        *failure = "Widget snapshot restore did not restore imported document metadata.";
        return false;
    }
    if (status->text () != originalStatus || mainTabs->currentIndex () != originalMainTab ||
        previewTabs->currentIndex () != originalPreviewTab) {
        *failure = QString ("Widget snapshot restore visible state mismatch: status '%1'/'%2', "
                            "main %3/%4, preview %5/%6.")
                       .arg (status->text (), originalStatus)
                       .arg (mainTabs->currentIndex ())
                       .arg (originalMainTab)
                       .arg (previewTabs->currentIndex ())
                       .arg (originalPreviewTab);
        return false;
    }
    for (int index = 0; index < previewTabs->count (); ++index) {
        QTextEdit* preview = qobject_cast< QTextEdit* > (previewTabs->widget (index));
        if (preview == NULL || preview->toPlainText () != originalPreviews[index]) {
            *failure = "Widget snapshot restore did not restore XML previews.";
            return false;
        }
    }

    if (limitJoint->count () < 2) {
        *failure = "Restored table combo has no alternate value for the interaction test.";
        return false;
    }
    widget.markProjectDocumentClean ();
    int interactions = 0;
    QObject::connect (&widget, &rws::RobotModelBuilderWidget::projectDocumentInteraction,
                      [&interactions] () { ++interactions; });
    const int changedIndex = limitJoint->currentIndex () == 0 ? 1 : 0;
    limitJoint->setCurrentIndex (changedIndex);
    if (interactions != 1 || !widget.isProjectDocumentDirty ()) {
        *failure = QString ("Restored table value change reported %1 interactions with dirty=%2.")
                       .arg (interactions)
                       .arg (widget.isProjectDocumentDirty ());
        return false;
    }
    return true;
}

bool verifyProjectContextSetterDoesNotCreateDirectories (QString* failure)
{
    QTemporaryDir fixture;
    if (!fixture.isValid ()) {
        *failure = "Could not create the project context setter fixture.";
        return false;
    }

    rws::RobotModelBuilderWidget widget;
    const QString firstRoot = fixture.filePath ("first/project");
    const QString rollbackRoot = fixture.filePath ("rollback/project");
    widget.setProjectOutputDirectory (firstRoot);
    widget.setProjectOutputDirectory (firstRoot);
    widget.setProjectOutputDirectory (rollbackRoot);

    for (const QString& root : {firstRoot, rollbackRoot}) {
        if (QFileInfo::exists (root) ||
            QFileInfo::exists (QDir (root).filePath ("generated/robot-models"))) {
            *failure = "Project context notification created a generated output directory.";
            return false;
        }
    }
    return true;
}

bool verifyDefaultProjectModelBaseline (QString* failure)
{
    QTemporaryDir project;
    if (!project.isValid ()) {
        *failure = "Could not create the default project model fixture.";
        return false;
    }

    rws::RobotModelBuilderWidget widget;
    widget.setProjectOutputDirectory (project.path ());
    widget.applyDefaultProjectModel ();
    widget.beginGeneratedProjectDocument ();

    const QString output = QDir (project.path ()).filePath ("generated/robot-models");
    const rws::RobotModelSpec expected =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel (output);
    const rws::RobotModelSpec actual = widget.currentModelSpec ();
    if (actual.robotName != expected.robotName ||
        actual.transformJoints.size () != expected.transformJoints.size () ||
        actual.transformJoints.size () != 6) {
        *failure = "The default project model is not the factory six-axis baseline.";
        return false;
    }
    if (!widget.isProjectDocumentDirty ()) {
        *failure = "The default project model must start as an unsaved document.";
        return false;
    }
    if (QFileInfo::exists (output)) {
        *failure = "Applying the default project model created generated output.";
        return false;
    }
    return true;
}
}    // namespace

int main (int argc, char** argv)
{
    QApplication application (argc, argv);
    QString publishFailure;
    if (!verifyMeshToAutoLinkConversion (&publishFailure))
        return fail (publishFailure.toUtf8 ().constData ());
    if (!verifyTransactionalPublishService (&publishFailure))
        return fail (publishFailure.toUtf8 ().constData ());
    if (!verifyProjectModeSaveAndLoad (&publishFailure))
        return fail (publishFailure.toUtf8 ().constData ());
    if (!verifyProjectContextSetterDoesNotCreateDirectories (&publishFailure))
        return fail (publishFailure.toUtf8 ().constData ());
    if (!verifyDefaultProjectModelBaseline (&publishFailure))
        return fail (publishFailure.toUtf8 ().constData ());
    if (!verifyWidgetStateSnapshotRoundTrip (&publishFailure))
        return fail (publishFailure.toUtf8 ().constData ());
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

    QString highlightedDrawable;
    QObject::connect (&widget, &rws::RobotModelBuilderWidget::drawableSelectionChanged,
                      [&highlightedDrawable] (const QString& name) { highlightedDrawable = name; });
    drawables->setCurrentCell (0, 0);
    QApplication::processEvents ();
    if (highlightedDrawable != drawables->item (0, 0)->text ())
        return fail ("Selecting a drawable row should report its name for 3D highlighting.");
    drawables->clearSelection ();
    QApplication::processEvents ();
    if (!highlightedDrawable.isEmpty ())
        return fail ("Clearing the drawable selection should clear the 3D highlight.");

    QPushButton* addDrawable = widget.findChild< QPushButton* > ("addDrawableButton");
    QPushButton* duplicateDrawable = widget.findChild< QPushButton* > ("duplicateDrawableButton");
    QPushButton* removeDrawable = widget.findChild< QPushButton* > ("removeDrawableButton");
    QPushButton* regenerateLinks = widget.findChild< QPushButton* > ("regenerateLinkHelpersButton");
    if (addDrawable == NULL || duplicateDrawable == NULL || removeDrawable == NULL ||
        regenerateLinks == NULL)
        return fail ("Drawables tab should expose add, duplicate, remove, and link helper actions.");

    const int defaultDrawableCount = drawables->rowCount ();
    if (!QMetaObject::invokeMethod (&widget, "addDrawable", Qt::DirectConnection) ||
        drawables->rowCount () != defaultDrawableCount + 1)
        return fail ("Add Geometry should append an editable drawable.");
    const int addedRow = drawables->rowCount () - 1;
    drawables->setCurrentCell (addedRow, 0);
    if (!QMetaObject::invokeMethod (&widget, "duplicateSelectedDrawable", Qt::DirectConnection) ||
        drawables->rowCount () != defaultDrawableCount + 2)
        return fail ("Duplicate Geometry should append a copy of the selected drawable.");
    drawables->setCurrentCell (drawables->rowCount () - 1, 0);
    if (!QMetaObject::invokeMethod (&widget, "removeSelectedDrawable", Qt::DirectConnection) ||
        drawables->rowCount () != defaultDrawableCount + 1)
        return fail ("Remove Geometry should remove the selected drawable.");

    int helperRow = -1;
    for (int row = 0; row < drawables->rowCount (); ++row) {
        if (drawables->item (row, 0)->text ().startsWith ("Link")) {
            helperRow = row;
            break;
        }
    }
    if (helperRow < 0)
        return fail ("Default model should contain a generated link helper.");
    drawables->setCurrentCell (helperRow, 0);
    if (!QMetaObject::invokeMethod (&widget, "removeSelectedDrawable", Qt::DirectConnection))
        return fail ("Could not remove a generated link helper.");
    const int countWithoutHelper = drawables->rowCount ();
    if (!QMetaObject::invokeMethod (&widget, "regenerateLinkHelpers", Qt::DirectConnection) ||
        drawables->rowCount () != countWithoutHelper + 1)
        return fail ("Regenerate Link Helpers should restore only the missing link helper.");

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
    if (QDir (generatedDirectory).exists ())
        return fail ("Project context notification must not create the output directory.");
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
    if (!QDir ().mkpath (generatedDirectory))
        return fail ("Could not create the overwrite confirmation fixture directory.");
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

    // 项目关闭回归测试:先构造一个已绑定输出目录、含脏模型(机器人名
    // "OldProjectRobot")的 Widget,作为"旧项目仍在打开"的夹具。
    rws::RobotModelBuilderWidget projectCloseWidget;
    projectCloseWidget.setProjectOutputDirectory (portableProject.path ());
    rws::RobotModelSpec oldProjectSpec = projectCloseWidget.currentModelSpec ();
    oldProjectSpec.robotName = "OldProjectRobot";
    projectCloseWidget.syncFromWorkCellSpec (oldProjectSpec, {});
    projectCloseWidget.beginGeneratedProjectDocument ();
    if (!projectCloseWidget.isProjectDocumentDirty ())
        return fail ("Project-close fixture must contain a dirty project model.");

    projectCloseWidget.clearProjectDocumentContext ();

    // 关闭后校验:输出目录必须被清空、模型名不得残留旧机器人、脏标志必须复位,
    // 三者共同保证新工程不会继承上一项目的建模状态。
    if (!projectCloseWidget.projectOutputDirectory ().isEmpty ())
        return fail ("Closing a project must clear the managed robot output directory.");
    if (projectCloseWidget.currentModelSpec ().robotName == "OldProjectRobot")
        return fail ("Closing a project must remove the previous project robot model.");
    if (projectCloseWidget.isProjectDocumentDirty ())
        return fail ("Closing a project must clear the robot model document baseline.");

    return 0;
}
