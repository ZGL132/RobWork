#include "RobotModelStalenessChecker.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelProjectPaths.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace rws {
namespace {

bool hasRelativeGeometryPath(const RobotModelSpec& model)
{
    for (const DrawableSpec& drawable : model.drawables) {
        const QString path = QString::fromStdString(drawable.filePath).trimmed();
        if (!path.isEmpty() && QFileInfo(path).isRelative())
            return true;
    }
    for (const CollisionModelSpec& collision : model.collisionModels) {
        const QString path = QString::fromStdString(collision.filePath).trimmed();
        if (!path.isEmpty() && QFileInfo(path).isRelative())
            return true;
    }
    for (const SceneGeometrySpec& geometry : model.sceneGeometries) {
        const QString path = QString::fromStdString(geometry.file).trimmed();
        if (!path.isEmpty() && QFileInfo(path).isRelative())
            return true;
    }
    return false;
}

} // namespace

RobotModelStalenessResult RobotModelStalenessChecker::check(
    const RobotDesignContext& context, const QString& projectPath)
{
    return checkManaged(context, projectPath, QString());
}

RobotModelStalenessResult RobotModelStalenessChecker::checkManaged(
    const RobotDesignContext& context, const QString& projectPath,
    const QString& managedProjectRoot)
{
    const RobotModelProvenance& provenance = context.modelProvenance;
    if (provenance.sourceModelPath.empty() || provenance.sourceFingerprint.empty() ||
        provenance.snapshotFingerprint.empty()) {
        return {RobotModelSourceStatus::Untracked, QString(),
                "The optimization project has no complete model provenance."};
    }

    const QString sourcePath = QString::fromStdString(provenance.sourceModelPath);
    QFileInfo sourceInfo(sourcePath);
    const QString resolvedPath = sourceInfo.isAbsolute()
                                     ? sourceInfo.absoluteFilePath()
                                     : QDir(QFileInfo(projectPath).absolutePath())
                                           .absoluteFilePath(sourcePath);
    const QFileInfo resolvedInfo(resolvedPath);
    if (!resolvedInfo.exists() || !resolvedInfo.isFile()) {
        return {RobotModelSourceStatus::SourceMissing, resolvedPath,
                "The tracked model source file is missing."};
    }

    QFile sourceFile(resolvedPath);
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {RobotModelSourceStatus::SourceMissing, resolvedPath,
                "The tracked model source file cannot be read."};
    }

    RobotModelSpec sourceSpec;
    std::string parseError;
    if (!RobotModelSpecJson::fromJson(sourceFile.readAll().toStdString(), sourceSpec,
                                      &parseError)) {
        return {RobotModelSourceStatus::SourceInvalid, resolvedPath,
                "The tracked model source file is invalid: " +
                    QString::fromStdString(parseError)};
    }

    RobotModelSpec fingerprintSpec = sourceSpec;
    const QString managedRoot = managedProjectRoot.trimmed();
    if (!managedRoot.isEmpty() && hasRelativeGeometryPath(sourceSpec)) {
        QString pathError;
        if (!RobotModelProjectPaths::resolveManaged(
                sourceSpec, managedRoot, fingerprintSpec, &pathError)) {
            return {RobotModelSourceStatus::SourceInvalid, resolvedPath,
                    "The tracked managed model paths are invalid: " + pathError};
        }
    }

    const std::string sourceFingerprint =
        RobotModelFingerprint::canonicalSha256(fingerprintSpec);
    if (sourceFingerprint != provenance.sourceFingerprint ||
        sourceFingerprint != provenance.snapshotFingerprint) {
        return {RobotModelSourceStatus::Stale, resolvedPath,
                "The tracked model source differs from the frozen project snapshot."};
    }

    return {RobotModelSourceStatus::Current, resolvedPath,
            "The tracked model source matches the frozen project snapshot."};
}

} // namespace rws
