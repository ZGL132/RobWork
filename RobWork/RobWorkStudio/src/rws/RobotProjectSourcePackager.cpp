#include "RobotProjectSourcePackager.hpp"

#include "ProjectPathResolver.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

namespace rws {
namespace {

struct MeshReference
{
    QString original;
    QString canonicalSourcePath;
    QString proposedProjectPath;
    QString finalProjectPath;
};

void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

QString normalizedAbsolutePath (const QString& path)
{
    return QDir::fromNativeSeparators (QDir::cleanPath (QFileInfo (path).absoluteFilePath ())) ;
}

Qt::CaseSensitivity pathCaseSensitivity ()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool isInsideRoot (const QString& rootPath, const QString& candidatePath)
{
    const QString root = normalizedAbsolutePath (rootPath);
    const QString candidate = normalizedAbsolutePath (candidatePath);
    if (candidate.compare (root, pathCaseSensitivity ()) == 0)
        return true;
    return candidate.startsWith (root + QLatin1Char ('/'), pathCaseSensitivity ());
}

QString hashPrefix (const QString& value)
{
    return QString::fromLatin1 (
               QCryptographicHash::hash (value.toUtf8 (), QCryptographicHash::Sha256).toHex ())
        .left (12);
}

QString sanitizedSegment (const QString& value)
{
    QString result;
    result.reserve (value.size ());
    for (const QChar character : value) {
        const ushort code = character.unicode ();
        const bool safe = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') ||
            (code >= '0' && code <= '9') || character == QLatin1Char ('.') ||
            character == QLatin1Char ('_') || character == QLatin1Char ('-');
        result += safe ? character : QLatin1Char ('_');
    }
    while (result.endsWith (QLatin1Char ('.')) || result.endsWith (QLatin1Char (' ')))
        result.chop (1);
    if (result.isEmpty () || result == QStringLiteral (".") || result == QStringLiteral (".."))
        result = QStringLiteral ("asset");
    return result;
}

bool normalizedPackagePath (const QString& raw,
                            QString& packageName,
                            QString& relativePath,
                            QString* error)
{
    const QString suffix = QDir::fromNativeSeparators (raw.mid (QStringLiteral ("package://").size ())) ;
    const int slash = suffix.indexOf (QLatin1Char ('/'));
    if (slash <= 0 || slash == suffix.size () - 1) {
        setError (error, QStringLiteral ("Invalid package mesh reference: %1").arg (raw));
        return false;
    }
    packageName = suffix.left (slash);
    const QString rawRelative = suffix.mid (slash + 1);
    relativePath = QDir::cleanPath (rawRelative);
    if (QFileInfo (relativePath).isAbsolute () || relativePath == QStringLiteral ("..") ||
        relativePath.startsWith (QStringLiteral ("../"))) {
        setError (error, QStringLiteral ("Package mesh reference escapes its package: %1").arg (raw));
        return false;
    }
    return true;
}

QString sanitizedRelativePath (const QString& relativePath)
{
    QStringList result;
    const QStringList segments = QDir::fromNativeSeparators (relativePath).split (
        QLatin1Char ('/'), Qt::SkipEmptyParts);
    for (const QString& segment : segments)
        result.push_back (sanitizedSegment (segment));
    return result.join (QLatin1Char ('/'));
}

bool resolveMeshReference (const QString& original,
                           const QString& urdfPath,
                           QString& canonicalSourcePath,
                           QString& proposedProjectPath,
                           QString* error)
{
    const QString trimmed = original.trimmed ();
    if (trimmed.isEmpty ()) {
        setError (error, QStringLiteral ("URDF mesh filename cannot be empty."));
        return false;
    }

    QString resolved;
    if (trimmed.startsWith (QStringLiteral ("package://"), Qt::CaseInsensitive)) {
        QString packageName;
        QString relativePath;
        if (!normalizedPackagePath (trimmed, packageName, relativePath, error))
            return false;

        QDir root = QFileInfo (urdfPath).absoluteDir ();
        QStringList roots;
        for (int depth = 0; depth < 3; ++depth) {
            roots.push_back (root.absolutePath ());
            root.cdUp ();
        }
        for (const QString& packageRoot : roots) {
            const QString withPackage =
                QDir (packageRoot).filePath (packageName + QLatin1Char ('/') + relativePath);
            const QString withoutPackage = QDir (packageRoot).filePath (relativePath);
            if (QFileInfo (withPackage).isFile ()) {
                resolved = withPackage;
                break;
            }
            if (QFileInfo (withoutPackage).isFile ()) {
                resolved = withoutPackage;
                break;
            }
        }
        proposedProjectPath = QStringLiteral ("assets/robot/packages/%1/%2")
                                  .arg (sanitizedSegment (packageName),
                                        sanitizedRelativePath (relativePath));
    }
    else {
        if (trimmed.contains (QStringLiteral ("://"))) {
            setError (error, QStringLiteral ("Unsupported mesh URI: %1").arg (original));
            return false;
        }
        const QFileInfo referenceInfo (QDir::fromNativeSeparators (trimmed));
        resolved = referenceInfo.isAbsolute ()
                     ? referenceInfo.absoluteFilePath ()
                     : QFileInfo (urdfPath).absoluteDir ().absoluteFilePath (trimmed);
    }

    const QFileInfo sourceInfo (resolved);
    if (!sourceInfo.isFile ()) {
        setError (error,
                  QStringLiteral ("Could not resolve URDF mesh '%1' to an ordinary file (resolved: %2).")
                      .arg (original, normalizedAbsolutePath (resolved)));
        return false;
    }
    canonicalSourcePath = QDir::fromNativeSeparators (sourceInfo.canonicalFilePath ());
    if (canonicalSourcePath.isEmpty ()) {
        setError (error, QStringLiteral ("Could not canonicalize URDF mesh: %1").arg (resolved));
        return false;
    }
    if (proposedProjectPath.isEmpty ()) {
        proposedProjectPath = QStringLiteral ("assets/robot/files/%1-%2")
                                  .arg (hashPrefix (canonicalSourcePath),
                                        sanitizedSegment (sourceInfo.fileName ())) ;
    }
    proposedProjectPath = QDir::fromNativeSeparators (QDir::cleanPath (proposedProjectPath));
    return true;
}

bool collectMeshReferences (const QByteArray& sourceXml,
                            const QString& sourcePath,
                            QVector< MeshReference >& references,
                            QString* error)
{
    QXmlStreamReader xml (sourceXml);
    bool rootSeen = false;
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement ())
            continue;
        if (!rootSeen) {
            rootSeen = true;
            if (xml.name ().compare (QStringLiteral ("robot"), Qt::CaseInsensitive) != 0) {
                setError (error,
                          QStringLiteral ("Expected a URDF <robot> root element in %1, found <%2>.")
                              .arg (sourcePath, xml.qualifiedName ().toString ())) ;
                return false;
            }
        }
        if (xml.name ().compare (QStringLiteral ("mesh"), Qt::CaseInsensitive) != 0)
            continue;

        MeshReference reference;
        reference.original = xml.attributes ().value (QStringLiteral ("filename")).toString ();
        if (!resolveMeshReference (reference.original,
                                   sourcePath,
                                   reference.canonicalSourcePath,
                                   reference.proposedProjectPath,
                                   error)) {
            return false;
        }
        references.push_back (reference);
    }
    if (xml.hasError ()) {
        setError (error,
                  QStringLiteral ("Malformed URDF XML in %1 at line %2: %3")
                      .arg (sourcePath)
                      .arg (xml.lineNumber ())
                      .arg (xml.errorString ())) ;
        return false;
    }
    if (!rootSeen) {
        setError (error, QStringLiteral ("URDF file has no root element: %1").arg (sourcePath));
        return false;
    }
    return true;
}

void assignFinalProjectPaths (QVector< MeshReference >& references)
{
    QHash< QString, QStringList > proposalsByCanonical;
    for (const MeshReference& reference : references)
        proposalsByCanonical[reference.canonicalSourcePath].push_back (reference.proposedProjectPath);

    QMap< QString, QString > canonicalByFinalPath;
    QHash< QString, QString > finalByCanonical;
    QStringList canonicalPaths = proposalsByCanonical.keys ();
    std::sort (canonicalPaths.begin (), canonicalPaths.end ());
    for (const QString& canonical : canonicalPaths) {
        QStringList proposals = proposalsByCanonical.value (canonical);
        std::sort (proposals.begin (), proposals.end ());
        QString finalPath = proposals.front ();
        if (canonicalByFinalPath.contains (finalPath) &&
            canonicalByFinalPath.value (finalPath) != canonical) {
            const QFileInfo info (finalPath);
            finalPath = QDir (info.path ())
                            .filePath (hashPrefix (canonical) + QLatin1Char ('-') + info.fileName ());
        }
        canonicalByFinalPath.insert (finalPath, canonical);
        finalByCanonical.insert (canonical, finalPath);
    }
    for (MeshReference& reference : references)
        reference.finalProjectPath = finalByCanonical.value (reference.canonicalSourcePath);
}

QByteArray rewriteUrdf (const QByteArray& sourceXml,
                        const QVector< MeshReference >& references,
                        QString* error)
{
    QByteArray rewritten;
    QXmlStreamReader reader (sourceXml);
    QXmlStreamWriter writer (&rewritten);
    writer.setAutoFormatting (true);
    int meshIndex = 0;
    while (!reader.atEnd ()) {
        reader.readNext ();
        if (reader.isStartElement () &&
            reader.name ().compare (QStringLiteral ("mesh"), Qt::CaseInsensitive) == 0) {
            if (meshIndex >= references.size ()) {
                setError (error, QStringLiteral ("URDF mesh rewrite count is inconsistent."));
                return {};
            }
            writer.writeStartElement (reader.qualifiedName ().toString ());
            for (const auto& declaration : reader.namespaceDeclarations ())
                writer.writeNamespace (declaration.namespaceUri ().toString (),
                                       declaration.prefix ().toString ());
            for (const QXmlStreamAttribute& attribute : reader.attributes ()) {
                if (attribute.name ().compare (QStringLiteral ("filename"),
                                                Qt::CaseInsensitive) == 0) {
                    const QString relative = QDir::cleanPath (
                        QStringLiteral ("../../") + references[meshIndex].finalProjectPath);
                    writer.writeAttribute (attribute.qualifiedName ().toString (),
                                           QDir::fromNativeSeparators (relative));
                }
                else {
                    writer.writeAttribute (attribute.qualifiedName ().toString (),
                                           attribute.value ().toString ());
                }
            }
            ++meshIndex;
        }
        else {
            writer.writeCurrentToken (reader);
        }
    }
    if (reader.hasError () || meshIndex != references.size ()) {
        setError (error, QStringLiteral ("Could not rewrite the managed URDF consistently."));
        return {};
    }
    return rewritten;
}

bool verifyManagedUrdf (const QString& managedUrdf,
                        const QString& stagingRoot,
                        QString* error)
{
    QFile file (managedUrdf);
    if (!file.open (QIODevice::ReadOnly)) {
        setError (error, QStringLiteral ("Could not reopen managed URDF: %1").arg (managedUrdf));
        return false;
    }
    QXmlStreamReader xml (&file);
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement () ||
            xml.name ().compare (QStringLiteral ("mesh"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString reference =
            xml.attributes ().value (QStringLiteral ("filename")).toString ();
        if (reference.isEmpty () || QFileInfo (reference).isAbsolute ()) {
            setError (error,
                      QStringLiteral ("Managed URDF contains an invalid mesh path: %1")
                          .arg (reference));
            return false;
        }
        const QString resolved = QFileInfo (managedUrdf).absoluteDir ().absoluteFilePath (reference);
        if (!isInsideRoot (stagingRoot, resolved) || !QFileInfo (resolved).isFile ()) {
            setError (error,
                      QStringLiteral ("Managed URDF mesh escapes the project or is missing: %1")
                          .arg (reference));
            return false;
        }
    }
    if (xml.hasError ()) {
        setError (error, QStringLiteral ("Managed URDF is malformed: %1").arg (xml.errorString ()));
        return false;
    }
    return true;
}

bool directoryIsEmpty (const QString& path)
{
    return QDir (path).entryList (QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty ();
}

}    // namespace

bool RobotProjectSourcePackager::prepare (const QString& sourceUrdfPath,
                                          const QString& targetProjectFilePath,
                                          PackagedRobotSource& packaged,
                                          QString* error)
{
    discard (packaged);
    if (error != nullptr)
        error->clear ();

    const QFileInfo sourceInfo (sourceUrdfPath);
    const QFileInfo projectInfo (targetProjectFilePath);
    if (!sourceInfo.isFile ()) {
        setError (error, QStringLiteral ("Robot source is not an ordinary file: %1").arg (sourceUrdfPath));
        return false;
    }
    if (!projectInfo.isAbsolute () || !projectInfo.absoluteDir ().exists ()) {
        setError (error,
                  QStringLiteral ("Robot project target directory does not exist: %1")
                      .arg (projectInfo.absolutePath ())) ;
        return false;
    }
    if (projectInfo.exists ()) {
        setError (error, QStringLiteral ("Robot project target already exists: %1").arg (targetProjectFilePath));
        return false;
    }

    QFile sourceFile (sourceInfo.absoluteFilePath ());
    if (!sourceFile.open (QIODevice::ReadOnly)) {
        setError (error,
                  QStringLiteral ("Could not read robot source %1: %2")
                      .arg (sourceInfo.absoluteFilePath (), sourceFile.errorString ())) ;
        return false;
    }
    const QByteArray sourceXml = sourceFile.readAll ();
    QVector< MeshReference > references;
    if (!collectMeshReferences (sourceXml, sourceInfo.absoluteFilePath (), references, error))
        return false;
    assignFinalProjectPaths (references);

    const QString managedProjectPath = QStringLiteral ("sources/robot/robot.urdf");
    QMap< QString, QString > sourceByProjectPath;
    for (const MeshReference& reference : references)
        sourceByProjectPath.insert (reference.finalProjectPath, reference.canonicalSourcePath);

    QStringList finalProjectPaths = sourceByProjectPath.keys ();
    finalProjectPaths.push_back (managedProjectPath);
    for (const QString& relativePath : finalProjectPaths) {
        const QString finalPath = projectInfo.absoluteDir ().filePath (relativePath);
        if (!ProjectPathResolver::validateContainedWritePath (
                projectInfo.absolutePath (), finalPath, error)) {
            return false;
        }
        if (QFileInfo::exists (finalPath)) {
            setError (error,
                      QStringLiteral ("Robot project target already exists and will not be overwritten: %1")
                          .arg (finalPath));
            return false;
        }
    }

    PackagedRobotSource candidate;
    candidate.projectRoot = projectInfo.absolutePath ();
    const QString stagingParent = projectInfo.absoluteDir ().filePath (QStringLiteral (".rwproject"));
    candidate.removeEmptyStagingParent = !QFileInfo::exists (stagingParent);
    candidate.stagingAttemptRoot = QDir (stagingParent)
                                       .filePath (QStringLiteral ("import-") +
                                                  QUuid::createUuid ().toString (QUuid::WithoutBraces));
    candidate.stagingRoot = QDir (candidate.stagingAttemptRoot).filePath (QStringLiteral ("root"));
    if (!ProjectPathResolver::validateContainedWritePath (
            projectInfo.absolutePath (), candidate.stagingRoot, error)) {
        discard (candidate);
        return false;
    }
    ProjectWriteGuard stagingRootGuard;
    if (!ProjectWriteGuard::acquire (projectInfo.absolutePath (),
                                     QDir (candidate.stagingRoot).filePath (".guard"),
                                     stagingRootGuard, error)) {
        discard (candidate);
        return false;
    }
    if (!QDir ().mkpath (candidate.stagingRoot)) {
        setError (error, QStringLiteral ("Could not create robot import staging directory: %1")
                             .arg (candidate.stagingRoot));
        discard (candidate);
        return false;
    }

    QStringList sortedAssetPaths = sourceByProjectPath.keys ();
    std::sort (sortedAssetPaths.begin (), sortedAssetPaths.end ());
    QHash< QString, QString > resourceIdByPath;
    for (const QString& projectPath : sortedAssetPaths) {
        const QString stagedPath = QDir (candidate.stagingRoot).filePath (projectPath);
        ProjectWriteGuard assetGuard;
        if (!ProjectWriteGuard::acquire (projectInfo.absolutePath (), stagedPath, assetGuard,
                                         error)) {
            discard (candidate);
            return false;
        }
        if (!QDir ().mkpath (QFileInfo (stagedPath).absolutePath ()) ||
            !QFile::copy (sourceByProjectPath.value (projectPath), stagedPath)) {
            setError (error,
                      QStringLiteral ("Could not stage URDF mesh '%1' as '%2'.")
                          .arg (sourceByProjectPath.value (projectPath), projectPath));
            discard (candidate);
            return false;
        }
        ProjectResource asset;
        asset.id = QStringLiteral ("robot-source.asset.%1").arg (hashPrefix (projectPath));
        asset.kind = QStringLiteral ("robwork.passive-asset");
        asset.path = projectPath;
        asset.ownership = QStringLiteral ("project");
        asset.required = true;
        candidate.assetResources.push_back (asset);
        candidate.stagedFilesByProjectPath.insert (projectPath, stagedPath);
        resourceIdByPath.insert (projectPath, asset.id);
    }

    const QByteArray rewritten = rewriteUrdf (sourceXml, references, error);
    if (rewritten.isEmpty ()) {
        discard (candidate);
        return false;
    }
    candidate.stagedManagedUrdfPath = QDir (candidate.stagingRoot).filePath (managedProjectPath);
    ProjectWriteGuard managedUrdfGuard;
    if (!ProjectWriteGuard::acquire (projectInfo.absolutePath (), candidate.stagedManagedUrdfPath,
                                     managedUrdfGuard, error)) {
        discard (candidate);
        return false;
    }
    if (!QDir ().mkpath (QFileInfo (candidate.stagedManagedUrdfPath).absolutePath ())) {
        setError (error, QStringLiteral ("Could not create managed URDF staging directory."));
        discard (candidate);
        return false;
    }
    QFile managedFile (candidate.stagedManagedUrdfPath);
    if (!managedFile.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
        managedFile.write (rewritten) != rewritten.size ()) {
        setError (error, QStringLiteral ("Could not write managed URDF: %1")
                             .arg (candidate.stagedManagedUrdfPath));
        discard (candidate);
        return false;
    }
    managedFile.close ();
    if (!verifyManagedUrdf (candidate.stagedManagedUrdfPath, candidate.stagingRoot, error)) {
        discard (candidate);
        return false;
    }

    candidate.sourceResource.id = QStringLiteral ("robot-source.main");
    candidate.sourceResource.kind = QStringLiteral ("robwork.passive-asset");
    candidate.sourceResource.path = managedProjectPath;
    candidate.sourceResource.ownership = QStringLiteral ("project");
    candidate.sourceResource.required = true;
    for (const QString& projectPath : sortedAssetPaths)
        candidate.sourceResource.dependencies.push_back (resourceIdByPath.value (projectPath));
    candidate.stagedFilesByProjectPath.insert (managedProjectPath,
                                               candidate.stagedManagedUrdfPath);
    packaged = candidate;
    return true;
}

void RobotProjectSourcePackager::discard (PackagedRobotSource& packaged)
{
    const QString attemptRoot = packaged.stagingAttemptRoot;
    const bool removeParent = packaged.removeEmptyStagingParent;
    const QString projectRoot = packaged.projectRoot;
    QString stagingParent;
    if (!attemptRoot.isEmpty ())
        stagingParent = QFileInfo (attemptRoot).absoluteDir ().absolutePath ();
    {
        ProjectWriteGuard attemptGuard;
        const bool safeAttemptRoot = !attemptRoot.isEmpty () && !projectRoot.isEmpty () &&
            ProjectWriteGuard::acquire (projectRoot, attemptRoot, attemptGuard, nullptr);
        if (safeAttemptRoot && QFileInfo::exists (attemptRoot))
            ProjectPathResolver::removeContainedDirectoryTree (projectRoot, attemptRoot, nullptr);
    }
    if (removeParent && !stagingParent.isEmpty () && QFileInfo (stagingParent).isDir () &&
        directoryIsEmpty (stagingParent)) {
        ProjectWriteGuard parentGuard;
        if (!projectRoot.isEmpty () &&
            ProjectWriteGuard::acquire (projectRoot, stagingParent, parentGuard, nullptr))
            ProjectPathResolver::removeContainedEmptyDirectory (projectRoot, stagingParent, nullptr);
    }
    packaged = PackagedRobotSource {};
}

}    // namespace rws
