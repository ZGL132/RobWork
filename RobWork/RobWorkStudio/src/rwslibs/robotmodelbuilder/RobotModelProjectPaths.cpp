#include "RobotModelProjectPaths.hpp"

#include <QDir>
#include <QFileInfo>

#include <filesystem>

namespace rws {
namespace {

bool isContainedRelativePath (const QString& relativePath)
{
    const QString normalized = QDir::fromNativeSeparators (QDir::cleanPath (relativePath));
    return !QDir::isAbsolutePath (normalized) && normalized != QStringLiteral ("..") &&
           !normalized.startsWith (QStringLiteral ("../"));
}

bool prepareRoot (const QString& projectRoot, QString& absoluteRoot, QString* error)
{
    if (error != nullptr)
        error->clear ();

    const QString trimmed = projectRoot.trimmed ();
    if (trimmed.isEmpty () || !QDir::isAbsolutePath (trimmed)) {
        if (error != nullptr)
            *error = QStringLiteral ("The managed robot project root must be an absolute path.");
        return false;
    }

    absoluteRoot = QDir::cleanPath (QDir::fromNativeSeparators (trimmed));
    return true;
}

bool validateCanonicalContainment (const QString& absolutePath,
                                   const QString& absoluteRoot,
                                   const QString& field,
                                   QString* error)
{
    const auto resolveThroughExistingAncestor = [] (const QString& path,
                                                     QString& resolved) {
        std::error_code errorCode;
#ifdef Q_OS_WIN
        const std::filesystem::path inputPath (
            QFileInfo (path).absoluteFilePath ().toStdWString ());
#else
        const std::filesystem::path inputPath (
            QFileInfo (path).absoluteFilePath ().toStdString ());
#endif
        const std::filesystem::path canonical = std::filesystem::weakly_canonical (
            inputPath, errorCode);
        if (errorCode || canonical.empty ())
            return false;
#ifdef Q_OS_WIN
        resolved = QString::fromStdWString (canonical.native ());
#else
        resolved = QString::fromStdString (canonical.native ());
#endif
        resolved = QDir::cleanPath (QDir::fromNativeSeparators (resolved));
        return true;
    };

    QString canonicalRoot;
    QString canonicalPath;
    if (!resolveThroughExistingAncestor (absoluteRoot, canonicalRoot) ||
        !resolveThroughExistingAncestor (absolutePath, canonicalPath)) {
        if (error != nullptr) {
            *error = QStringLiteral ("%1 cannot be resolved safely inside the managed robot project: %2")
                         .arg (field, absolutePath);
        }
        return false;
    }

    const QString relative = QDir (canonicalRoot).relativeFilePath (canonicalPath);
    if (isContainedRelativePath (relative))
        return true;

    if (error != nullptr) {
        *error = QStringLiteral ("%1 resolves outside the managed robot project: %2")
                     .arg (field, absolutePath);
    }
    return false;
}

bool resolveInsideProject (const QString& input,
                           const QString& absoluteRoot,
                           const QString& field,
                           QString& absolutePath,
                           QString* error)
{
    const QString normalizedInput = QDir::fromNativeSeparators (input.trimmed ());
    if (normalizedInput.isEmpty ()) {
        absolutePath.clear ();
        return true;
    }

    absolutePath = QDir::cleanPath (
        QDir::isAbsolutePath (normalizedInput)
            ? normalizedInput
            : QDir (absoluteRoot).absoluteFilePath (normalizedInput));
    const QString relative = QDir (absoluteRoot).relativeFilePath (absolutePath);
    if (!isContainedRelativePath (relative)) {
        if (error != nullptr) {
            *error = QStringLiteral ("%1 is outside the managed robot project: %2")
                         .arg (field, input);
        }
        return false;
    }

    return validateCanonicalContainment (absolutePath, absoluteRoot, field, error);
}

template< class Convert >
bool convertGeometryPaths (RobotModelSpec& spec, Convert convert, QString* error)
{
    for (std::size_t index = 0; index < spec.drawables.size (); ++index) {
        QString converted;
        const QString value = QString::fromStdString (spec.drawables[index].filePath);
        if (!convert (value, QStringLiteral ("Drawable %1 filePath").arg (index), converted,
                      error))
            return false;
        spec.drawables[index].filePath = converted.toStdString ();
    }

    for (std::size_t index = 0; index < spec.collisionModels.size (); ++index) {
        QString converted;
        const QString value = QString::fromStdString (spec.collisionModels[index].filePath);
        if (!convert (value, QStringLiteral ("Collision model %1 filePath").arg (index),
                      converted, error))
            return false;
        spec.collisionModels[index].filePath = converted.toStdString ();
    }

    for (std::size_t index = 0; index < spec.sceneGeometries.size (); ++index) {
        QString converted;
        const QString value = QString::fromStdString (spec.sceneGeometries[index].file);
        if (!convert (value, QStringLiteral ("Scene geometry %1 file").arg (index), converted,
                      error))
            return false;
        spec.sceneGeometries[index].file = converted.toStdString ();
    }
    return true;
}

}    // namespace

bool RobotModelProjectPaths::makePortable (const RobotModelSpec& runtime,
                                           const QString& projectRoot,
                                           RobotModelSpec& portable,
                                           QString* error)
{
    QString absoluteRoot;
    if (!prepareRoot (projectRoot, absoluteRoot, error))
        return false;

    RobotModelSpec candidate = runtime;
    const auto makeRelative = [&absoluteRoot] (const QString& value, const QString& field,
                                               QString& converted, QString* conversionError) {
        QString absolutePath;
        if (!resolveInsideProject (value, absoluteRoot, field, absolutePath, conversionError))
            return false;
        if (absolutePath.isEmpty ()) {
            converted.clear ();
            return true;
        }
        converted = QDir::fromNativeSeparators (
            QDir::cleanPath (QDir (absoluteRoot).relativeFilePath (absolutePath)));
        return true;
    };
    if (!convertGeometryPaths (candidate, makeRelative, error))
        return false;

    portable = candidate;
    return true;
}

bool RobotModelProjectPaths::resolveManaged (const RobotModelSpec& portable,
                                             const QString& projectRoot,
                                             RobotModelSpec& runtime,
                                             QString* error)
{
    QString absoluteRoot;
    if (!prepareRoot (projectRoot, absoluteRoot, error))
        return false;

    RobotModelSpec candidate = portable;
    const auto makeAbsolute = [&absoluteRoot] (const QString& value, const QString& field,
                                               QString& converted, QString* conversionError) {
        return resolveInsideProject (value, absoluteRoot, field, converted, conversionError);
    };
    if (!convertGeometryPaths (candidate, makeAbsolute, error))
        return false;
    candidate.saveDirectory = QDir::cleanPath (
        QDir (absoluteRoot).filePath (QStringLiteral ("generated/robot-models")))
                                  .toStdString ();

    runtime = candidate;
    return true;
}

}    // namespace rws
