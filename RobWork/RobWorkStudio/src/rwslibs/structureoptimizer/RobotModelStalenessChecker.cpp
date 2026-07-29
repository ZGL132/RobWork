#include "RobotModelStalenessChecker.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace rws {

RobotModelStalenessResult RobotModelStalenessChecker::check(
    const RobotDesignContext& context, const QString& projectPath)
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

    const std::string sourceFingerprint = RobotModelFingerprint::canonicalSha256(sourceSpec);
    if (sourceFingerprint != provenance.sourceFingerprint ||
        sourceFingerprint != provenance.snapshotFingerprint) {
        return {RobotModelSourceStatus::Stale, resolvedPath,
                "The tracked model source differs from the frozen project snapshot."};
    }

    return {RobotModelSourceStatus::Current, resolvedPath,
            "The tracked model source matches the frozen project snapshot."};
}

} // namespace rws
