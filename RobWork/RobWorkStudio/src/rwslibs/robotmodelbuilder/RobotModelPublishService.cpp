#include "RobotModelPublishService.hpp"

#include "RobotModelProjectPaths.hpp"
#include "RobotModelXmlWriter.hpp"

#include <rws/ProjectSaveTransaction.hpp>

#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/WorkCell.hpp>

#include <QDir>
#include <QFileInfo>

#include <exception>

namespace rws {
namespace {

void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

bool isContainedPath (const QString& path, const QString& root)
{
    const QString relative = QDir (root).relativeFilePath (path);
    const QString normalized = QDir::fromNativeSeparators (QDir::cleanPath (relative));
    if (QDir::isAbsolutePath (normalized) || normalized == QStringLiteral ("..") ||
        normalized.startsWith (QStringLiteral ("../")))
        return false;

    const QString canonicalRoot = QFileInfo (root).canonicalFilePath ();
    if (canonicalRoot.isEmpty ())
        return true;

    QFileInfo existingAncestor (path);
    while (!existingAncestor.exists ()) {
        const QString parent = existingAncestor.absolutePath ();
        if (parent == existingAncestor.absoluteFilePath ())
            return false;
        existingAncestor.setFile (parent);
    }
    const QString canonicalAncestor = existingAncestor.canonicalFilePath ();
    if (canonicalAncestor.isEmpty ())
        return true;
    const QString canonicalRelative =
        QDir (canonicalRoot).relativeFilePath (canonicalAncestor);
    const QString canonicalNormalized =
        QDir::fromNativeSeparators (QDir::cleanPath (canonicalRelative));
    return !QDir::isAbsolutePath (canonicalNormalized) &&
           canonicalNormalized != QStringLiteral ("..") &&
           !canonicalNormalized.startsWith (QStringLiteral ("../"));
}

bool validatePublishTarget (const QString& path, const QString& projectRoot, QString* error)
{
    const QString absolutePath =
        QDir::cleanPath (QDir::fromNativeSeparators (QFileInfo (path).absoluteFilePath ()));
    if (QDir::isAbsolutePath (absolutePath) && isContainedPath (absolutePath, projectRoot))
        return true;
    setError (error,
              QStringLiteral ("The generated robot model output is outside the project: %1")
                  .arg (path));
    return false;
}

bool stageXml (ProjectSaveTransaction& transaction,
               const QString& xml,
               const QString& path,
               QString* error)
{
    if (!transaction.stageBytes (xml.toUtf8 (), path, error)) {
        if (error != nullptr && error->isEmpty ())
            *error = QStringLiteral ("Could not stage generated robot model XML: %1").arg (path);
        return false;
    }
    return true;
}

}    // namespace

bool RobotModelPublishService::publishAndLoad (const RobotModelPublishRequest& request,
                                                QString* error)
{
    if (error != nullptr)
        error->clear ();

    const QString projectRoot =
        QDir::cleanPath (QDir::fromNativeSeparators (request.projectRoot.trimmed ()));
    if (projectRoot.isEmpty () || !QDir::isAbsolutePath (projectRoot) ||
        !QFileInfo (projectRoot).isDir ()) {
        setError (error, QStringLiteral ("The robot project root must be an existing absolute directory."));
        return false;
    }
    if (!request.promote) {
        setError (error, QStringLiteral ("The robot model publish promotion callback is required."));
        return false;
    }

    RobotModelSpec portable;
    if (!RobotModelProjectPaths::makePortable (request.spec, projectRoot, portable, error))
        return false;

    QStringList validationErrors;
    if (!RobotModelXmlWriter::validate (request.spec, validationErrors)) {
        setError (error, validationErrors.join (QStringLiteral ("\n")));
        return false;
    }

    const QString outputRoot =
        QDir::cleanPath (QDir::fromNativeSeparators (
            QString::fromStdString (request.spec.saveDirectory)));
    if (!QDir::isAbsolutePath (outputRoot) || !isContainedPath (outputRoot, projectRoot)) {
        setError (error,
                  QStringLiteral ("The generated robot model output directory is outside the project: %1")
                      .arg (outputRoot));
        return false;
    }

    ProjectSaveTransaction transaction;
    ProjectSaveTransaction::setContainmentRoot (transaction, projectRoot);
    const RobotModelSpec& spec = request.spec;
    const QString devicePath = RobotModelXmlWriter::serialDeviceFilePath (spec);
    QStringList publishTargets {devicePath};
    if (spec.generateScene) {
        if (spec.collisionSetup.enabled)
            publishTargets << RobotModelXmlWriter::collisionSetupFilePath (spec);
        if (spec.proximitySetup.enabled)
            publishTargets << RobotModelXmlWriter::proximitySetupFilePath (spec);
        publishTargets << RobotModelXmlWriter::sceneFilePath (spec);
    }
    if (spec.dynamics.generateDynamicWorkCell)
        publishTargets << RobotModelXmlWriter::dynamicWorkCellFilePath (spec);
    for (const QString& target : publishTargets) {
        if (!validatePublishTarget (target, projectRoot, error))
            return false;
    }

    if (!stageXml (transaction, RobotModelXmlWriter::makeSerialDeviceXml (spec), devicePath,
                   error))
        return false;

    QStringList dependencies;
    QString loadPath = devicePath;
    if (spec.generateScene) {
        dependencies << devicePath;
        if (spec.collisionSetup.enabled) {
            const QString collisionPath = RobotModelXmlWriter::collisionSetupFilePath (spec);
            if (!stageXml (transaction, RobotModelXmlWriter::makeCollisionSetupXml (spec),
                           collisionPath, error))
                return false;
            dependencies << collisionPath;
        }
        if (spec.proximitySetup.enabled) {
            const QString proximityPath = RobotModelXmlWriter::proximitySetupFilePath (spec);
            if (!stageXml (transaction, RobotModelXmlWriter::makeProximitySetupXml (spec),
                           proximityPath, error))
                return false;
            dependencies << proximityPath;
        }
        loadPath = RobotModelXmlWriter::sceneFilePath (spec);
        if (!stageXml (transaction, RobotModelXmlWriter::makeSceneXml (spec), loadPath, error))
            return false;
    }
    if (spec.dynamics.generateDynamicWorkCell &&
        !stageXml (transaction, RobotModelXmlWriter::makeDynamicWorkCellXml (spec),
                   RobotModelXmlWriter::dynamicWorkCellFilePath (spec), error))
        return false;

    if (!transaction.install (error))
        return false;

    // 安装后的加载/验证失败也必须回滚（恢复被替换的正式文件与备份），并把回滚
    // 自身的错误追加到原始错误之后，保证调用方看到完整失败链。
    const auto rollbackAfterPostInstallFailure = [&] {
        QString rollbackError;
        transaction.rollback (&rollbackError);
        if (error != nullptr && !rollbackError.isEmpty ()) {
            if (!error->isEmpty ())
                *error += QLatin1Char ('\n');
            *error += rollbackError;
        }
    };

    try {
        const rw::models::WorkCell::Ptr loaded =
            rw::loaders::WorkCellLoader::Factory::load (loadPath.toStdString ());
        if (loaded.isNull ()) {
            setError (error,
                      QStringLiteral ("The generated WorkCell could not be loaded: %1")
                          .arg (loadPath));
            rollbackAfterPostInstallFailure ();
            return false;
        }
    }
    catch (const std::exception& exception) {
        setError (error,
                  QStringLiteral ("The generated WorkCell could not be loaded: %1\n%2")
                      .arg (loadPath, QString::fromLocal8Bit (exception.what ())));
        rollbackAfterPostInstallFailure ();
        return false;
    }
    catch (...) {
        setError (error,
                  QStringLiteral ("The generated WorkCell could not be loaded: %1")
                      .arg (loadPath));
        rollbackAfterPostInstallFailure ();
        return false;
    }

    try {
        if (!request.promote (loadPath, dependencies, error)) {
            if (error != nullptr && error->isEmpty ())
                *error = QStringLiteral ("The generated WorkCell could not be promoted.");
            rollbackAfterPostInstallFailure ();
            return false;
        }
    }
    catch (const std::exception& exception) {
        setError (error,
                  QStringLiteral ("The generated WorkCell promotion failed: %1")
                      .arg (QString::fromLocal8Bit (exception.what ())));
        rollbackAfterPostInstallFailure ();
        return false;
    }
    catch (...) {
        setError (error, QStringLiteral ("The generated WorkCell promotion failed."));
        rollbackAfterPostInstallFailure ();
        return false;
    }

    return transaction.finalize (error);
}

}    // namespace rws
