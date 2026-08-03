#include "RobotModelBuilderPlugin.hpp"
#include "RobotModelBuilderWidget.hpp"
#include "RobotModelSpecJson.hpp"
#include "RobotModelXmlWriter.hpp"

#include <rws/ProjectManager.hpp>
#include <rws/RobWorkStudio.hpp>

#include <rw/core/PropertyMap.hpp>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaMethod>
#include <QMetaType>
#include <QTemporaryDir>
#include <QVariantMap>

#include <iostream>

namespace {
int fail (const char* message)
{
    std::cerr << "FAIL: " << message << std::endl;
    return 1;
}

bool hasQStringOperation (const QMetaObject& metaObject, const char* signature)
{
    const int index = metaObject.indexOfMethod (signature);
    return index >= 0 && metaObject.method (index).returnMetaType ().id () == QMetaType::QString;
}

bool hasVariantMapOperation (const QMetaObject& metaObject, const char* signature)
{
    const int index = metaObject.indexOfMethod (signature);
    return index >= 0 && metaObject.method (index).returnMetaType ().id () == QMetaType::QVariantMap;
}

bool hasInvokableOperation (const QMetaObject& metaObject,
                            const char* signature,
                            int returnType)
{
    const int index = metaObject.indexOfMethod (signature);
    return index >= 0 && metaObject.method (index).methodType () == QMetaMethod::Method &&
           metaObject.method (index).returnMetaType ().id () == returnType;
}
}    // namespace

int main (int argc, char** argv)
{
    const QMetaObject& metaObject = rws::RobotModelBuilderPlugin::staticMetaObject;
    if (metaObject.indexOfSignal ("robotModelLoaded(QString)") < 0)
        return fail ("A completed RobotModelBuilder load must emit robotModelLoaded(QString).");
    QApplication application (argc, argv);
    if (!hasQStringOperation (
            metaObject, "preflightRobotProjectSource(QString,QString)"))
        return fail ("Robot source preflight must be an invokable QString operation.");
    if (!hasQStringOperation (
            metaObject, "commitRobotProjectSource(QString,QString)"))
        return fail ("Robot source commit must be an invokable QString operation.");
    if (!hasQStringOperation (metaObject, "preflightNewRobotProject(QString)"))
        return fail ("New Project preflight must be an invokable QString operation.");
    if (!hasQStringOperation (metaObject, "bootstrapNewRobotProject(QString)"))
        return fail ("New Project bootstrap must be an invokable QString operation.");
    if (!hasVariantMapOperation (metaObject, "newRobotProjectResource(QString)"))
        return fail ("New Project resource declaration must be an invokable QVariantMap operation.");
    if (!hasVariantMapOperation (metaObject, "snapshotNewRobotProjectState()"))
        return fail ("New Project state snapshot must be an invokable QVariantMap operation.");
    if (!hasQStringOperation (metaObject, "restoreNewRobotProjectState(QByteArray)"))
        return fail ("New Project state restore must be an invokable QString operation.");
    if (!hasInvokableOperation (
            metaObject, "newRobotProjectResource(QString)", QMetaType::QVariantMap))
        return fail ("New Project resource factory must be an invokable QVariantMap operation.");
    if (!hasInvokableOperation (
            metaObject, "snapshotNewRobotProjectState()", QMetaType::QVariantMap))
        return fail ("New Project state snapshot must be an invokable QVariantMap operation.");
    if (!hasInvokableOperation (
            metaObject, "restoreNewRobotProjectState(QByteArray)", QMetaType::QString))
        return fail ("New Project state restore must be an invokable QString operation.");
    if (metaObject.indexOfSlot ("importRobotProjectSource(QString)") < 0)
        return fail ("The historical robot source import slot must remain available.");

    rws::RobotModelBuilderPlugin unattached;
    if (unattached.preflightNewRobotProject (QDir::tempPath ()).isEmpty ())
        return fail ("New Project preflight must reject an uninitialized plugin.");

    QTemporaryDir project;
    if (!project.isValid ())
        return fail ("Could not create the New Project bootstrap fixture.");
    const QString projectFile = project.filePath ("Bootstrap.rwproj");
    rws::ProjectManifest manifest;
    manifest.project.id = QStringLiteral ("bootstrap-project");
    manifest.project.name = QStringLiteral ("Bootstrap Project");
    QString error;
    rws::ProjectManager manager;
    if (!manager.createProject (projectFile, manifest, &error))
        return fail ("Could not create the New Project bootstrap manifest.");
    manager.closeProject ();

    rw::core::PropertyMap properties;
    rws::RobWorkStudio studio (properties);
    rws::RobotModelBuilderPlugin plugin;
    plugin.setRobWorkStudio (&studio);
    plugin.initialize ();
    studio.openFile (projectFile.toStdString ());

    if (plugin.preflightNewRobotProject (QStringLiteral ("relative/project")).isEmpty ())
        return fail ("New Project preflight must reject relative roots.");
    if (!plugin.preflightNewRobotProject (project.path ()).isEmpty ())
        return fail ("New Project preflight must accept the active absolute root.");

    QVariantMap resourceDeclaration;
    if (!QMetaObject::invokeMethod (
            &plugin, "newRobotProjectResource", Qt::DirectConnection,
            Q_RETURN_ARG (QVariantMap, resourceDeclaration),
            Q_ARG (QString, project.path ())))
        return fail ("Could not invoke the New Project resource declaration.");
    const rws::RobotModelSpec defaults = rws::RobotModelXmlWriter::makeDefaultSixAxisModel (
        QDir (project.path ()).filePath ("generated/robot-models"));
    const QString expectedResourcePath =
        QStringLiteral ("generated/robot-models/%1.rmb.json")
            .arg (rws::RobotModelXmlWriter::sanitizeFileBaseName (
                QString::fromStdString (defaults.robotName)));
    if (!resourceDeclaration.value (QStringLiteral ("success")).toBool () ||
        !resourceDeclaration.value (QStringLiteral ("error")).toString ().isEmpty () ||
        resourceDeclaration.value (QStringLiteral ("id")).toString () !=
            QStringLiteral ("robot-model.main") ||
        resourceDeclaration.value (QStringLiteral ("kind")).toString () !=
            QStringLiteral ("robwork.robot-model") ||
        resourceDeclaration.value (QStringLiteral ("path")).toString () != expectedResourcePath ||
        resourceDeclaration.value (QStringLiteral ("ownership")).toString () !=
            QStringLiteral ("generated") ||
        !resourceDeclaration.value (QStringLiteral ("required")).toBool () ||
        !resourceDeclaration.value (QStringLiteral ("dependencies")).toStringList ().isEmpty ())
        return fail ("New Project resource declaration did not match the default six-axis factory.");
    QTemporaryDir differentProject;
    if (!differentProject.isValid () ||
        plugin.bootstrapNewRobotProject (differentProject.path ()).isEmpty ())
        return fail ("New Project bootstrap must reject a genuinely different root.");
#ifdef Q_OS_WIN
    QString equivalentRoot = project.path ();
    for (QChar& character : equivalentRoot) {
        if (character.isLetter ())
            character = character.isUpper () ? character.toLower () : character.toUpper ();
    }
#else
    const QString equivalentRoot = project.path ();
#endif
    if (!plugin.bootstrapNewRobotProject (equivalentRoot).isEmpty ())
        return fail ("New Project bootstrap must create the default model resource.");

    rws::RobotModelBuilderWidget* widget =
        qobject_cast< rws::RobotModelBuilderWidget* > (plugin.widget ());
    if (widget == NULL || !widget->isProjectDocumentDirty ())
        return fail ("New Project bootstrap must establish a dirty default document.");
    if (widget->currentModelSpec ().robotName != defaults.robotName)
        return fail ("New Project bootstrap did not apply the factory default model.");

    const QByteArray beforeSnapshot = QByteArray::fromStdString (
        rws::RobotModelSpecJson::toJson (widget->currentModelSpec ()));
    QVariantMap stateSnapshot;
    if (!QMetaObject::invokeMethod (
            &plugin, "snapshotNewRobotProjectState", Qt::DirectConnection,
            Q_RETURN_ARG (QVariantMap, stateSnapshot)) ||
        !stateSnapshot.value (QStringLiteral ("success")).toBool () ||
        !stateSnapshot.value (QStringLiteral ("error")).toString ().isEmpty () ||
        stateSnapshot.value (QStringLiteral ("snapshot")).toByteArray ().isEmpty ())
        return fail ("New Project state snapshot did not return the Widget state.");

    rws::RobotModelSpec snapshotEdited = widget->currentModelSpec ();
    snapshotEdited.robotName = "EditedAfterSnapshot";
    widget->syncFromWorkCellSpec (snapshotEdited, {});
    QString restoreResult;
    if (!QMetaObject::invokeMethod (
            &plugin, "restoreNewRobotProjectState", Qt::DirectConnection,
            Q_RETURN_ARG (QString, restoreResult),
            Q_ARG (QByteArray,
                   stateSnapshot.value (QStringLiteral ("snapshot")).toByteArray ())) ||
        !restoreResult.isEmpty () ||
        QByteArray::fromStdString (
            rws::RobotModelSpecJson::toJson (widget->currentModelSpec ())) != beforeSnapshot)
        return fail ("New Project state restore did not restore the complete Widget model state.");
    QString modelPath;
    if (!studio.resolveProjectResource (QStringLiteral ("robot-model.main"), modelPath,
                                        &error) ||
        QFileInfo (modelPath).absoluteFilePath () !=
            QFileInfo (QDir (project.path ()).filePath (
                QStringLiteral ("generated/robot-models/%1.rmb.json")
                    .arg (rws::RobotModelXmlWriter::sanitizeFileBaseName (
                        QString::fromStdString (defaults.robotName)))))
                .absoluteFilePath ())
        return fail ("New Project bootstrap registered the wrong model resource path.");
    if (!studio.mainWorkCellResourceId ().isEmpty ())
        return fail ("New Project bootstrap must not publish a main WorkCell.");
    if (QFileInfo::exists (QDir (project.path ()).filePath ("generated/robot-models")))
        return fail ("New Project bootstrap must not write generated model output.");

    rws::RobotModelSpec edited = widget->currentModelSpec ();
    edited.robotName = "EditedBeforeSecondBootstrap";
    widget->syncFromWorkCellSpec (edited, {});
    if (!plugin.bootstrapNewRobotProject (project.path ()).isEmpty ())
        return fail ("Repeated New Project bootstrap must be an idempotent success.");
    if (widget->currentModelSpec ().robotName != "EditedBeforeSecondBootstrap")
        return fail ("Repeated New Project bootstrap overwrote the edited model.");
    if (!studio.mainWorkCellResourceId ().isEmpty () ||
        QFileInfo::exists (QDir (project.path ()).filePath ("generated/robot-models")))
        return fail ("Repeated New Project bootstrap published or wrote output.");

    if (!studio.saveCurrentProject (&error))
        return fail ("Could not save the bootstrap fixture during test cleanup.");
    studio.close ();
    plugin.setRobWorkStudio (NULL);
    return 0;
}
