#include "RobotModelFingerprint.hpp"

#include "RobotModelSpecJson.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

namespace {

std::string canonicalGeometryPath(const std::string& value, const QString& saveDirectory)
{
    const QString path = QDir::fromNativeSeparators(QString::fromStdString(value).trimmed());
    if (path.isEmpty())
        return std::string();
    if (!saveDirectory.isEmpty() && QFileInfo(path).isAbsolute()) {
        return QDir::fromNativeSeparators(
                   QDir::cleanPath(QDir(saveDirectory).relativeFilePath(path)))
            .toStdString();
    }
    return QDir::fromNativeSeparators(QDir::cleanPath(path)).toStdString();
}

} // namespace

namespace rws {

std::string RobotModelFingerprint::canonicalSha256(const RobotModelSpec& spec)
{
    RobotModelSpec canonical = spec;
    const QString saveDirectory =
        QDir::isAbsolutePath(QString::fromStdString(spec.saveDirectory))
        ? QDir::cleanPath(QDir::fromNativeSeparators(
              QString::fromStdString(spec.saveDirectory)))
        : QString();
    for (DrawableSpec& drawable : canonical.drawables)
        drawable.filePath = canonicalGeometryPath(drawable.filePath, saveDirectory);
    for (CollisionModelSpec& collision : canonical.collisionModels)
        collision.filePath = canonicalGeometryPath(collision.filePath, saveDirectory);
    for (SceneGeometrySpec& geometry : canonical.sceneGeometries)
        geometry.file = canonicalGeometryPath(geometry.file, saveDirectory);
    canonical.saveDirectory.clear();
    const QByteArray data = QByteArray::fromStdString(RobotModelSpecJson::toJson(canonical));
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
}

} // namespace rws
