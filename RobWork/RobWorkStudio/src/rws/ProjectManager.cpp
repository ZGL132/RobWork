#include "ProjectManager.hpp"

#include "ProjectManifestJson.hpp"
#include "ProjectDocumentRegistry.hpp"
#include "ProjectPackage.hpp"
#include "ProjectPathResolver.hpp"
#include "ProjectSaveTransaction.hpp"

#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/WorkCell.hpp>

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <QXmlStreamReader>

#include <algorithm>

namespace rws {
namespace {

// 统一错误回填工具：仅当调用方提供了 error 指针时才写入错误描述。
void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

// 生成当前 UTC 时间的 ISO-8601 字符串，用于清单的 createdAt / modifiedAt 元数据。
QString nowUtc ()
{
    return QDateTime::currentDateTimeUtc ().toString (Qt::ISODate);
}

// 原子复制（默认拒绝覆盖既有目标）。QFile::copy 直接写入目标路径，一旦进程或磁盘操作
// 中断可能遗留半截资源；这里先用 QSaveFile 写临时文件，commit 成功后才让目标对后续清单
// 更新可见，保证导入和克隆的“先文件、后清单”顺序具有可恢复性。
// 参数 rejectExistingTarget=false 用于恢复快照场景：此时用快照内容覆盖正式文件是预期行为。
bool copyFileAtomically (const QString& sourcePath,
                         const QString& targetPath,
                         QString* error,
                         bool rejectExistingTarget = true)
{
    const QFileInfo sourceInfo (sourcePath);
    if (!sourceInfo.exists () || !sourceInfo.isFile ()) {
        setError (error,
                  QString::fromUtf8 ("源资源文件不存在或不是普通文件：%1。").arg (sourcePath));
        return false;
    }
    if (rejectExistingTarget && QFileInfo::exists (targetPath)) {
        setError (error,
                  QString::fromUtf8 ("目标资源文件已存在，拒绝覆盖：%1。").arg (targetPath));
        return false;
    }

    const QFileInfo targetInfo (targetPath);
    if (!QDir ().mkpath (targetInfo.absolutePath ())) {
        setError (error,
                  QString::fromUtf8 ("无法创建资源目标目录：%1。").arg (targetInfo.absolutePath ()));
        return false;
    }

    QFile source (sourceInfo.absoluteFilePath ());
    if (!source.open (QIODevice::ReadOnly)) {
        setError (error,
                  QString::fromUtf8 ("无法读取源资源文件：%1。").arg (source.errorString ()));
        return false;
    }

    QSaveFile target (targetPath);
    if (!target.open (QIODevice::WriteOnly)) {
        setError (error,
                  QString::fromUtf8 ("无法写入资源临时文件：%1。").arg (target.errorString ()));
        return false;
    }

    // 分块复制可避免一次性把大型 WorkCell、网格或插件 JSON 全部读入内存。
    while (!source.atEnd ()) {
        const QByteArray chunk = source.read (64 * 1024);
        if (chunk.isEmpty () && source.error () != QFile::NoError) {
            setError (error,
                      QString::fromUtf8 ("读取源资源文件失败：%1。").arg (source.errorString ()));
            return false;
        }
        if (!chunk.isEmpty () && target.write (chunk) != chunk.size ()) {
            setError (error,
                      QString::fromUtf8 ("写入资源临时文件失败：%1。").arg (target.errorString ()));
            return false;
        }
    }
    if (!target.commit ()) {
        setError (error,
                  QString::fromUtf8 ("资源文件原子提交失败：%1。").arg (target.errorString ()));
        return false;
    }
    return true;
}

// 恢复快照只属于项目管理器，不应与用户可见资源混放。固定使用项目根目录下的隐藏目录，
// 既能随项目整体移动，也能避免把临时副本误登记进 rwproj 清单或参与项目打包。
QString autosaveRootDirectory (const QString& projectFilePath)
{
    return QDir (QFileInfo (projectFilePath).absolutePath ()).filePath (
        QStringLiteral (".rwproject/autosave"));
}

// 双槽快照的“当前活动槽”标记文件路径。内容为 "a" 或 "b"，指向最近一次成功快照所在槽。
QString autosaveMarkerFile (const QString& projectFilePath)
{
    return QDir (autosaveRootDirectory (projectFilePath)).filePath (QStringLiteral ("active-slot"));
}

// 读取活动槽：优先读 marker；marker 缺失或损坏时回退到实际存在的快照槽，再兜底默认 "a"。
// 这样即使标记文件被手工删除，仍能定位到最后一个有效快照。
QString activeAutosaveSlot (const QString& projectFilePath)
{
    QFile marker (autosaveMarkerFile (projectFilePath));
    if (marker.open (QIODevice::ReadOnly)) {
        const QString slot = QString::fromUtf8 (marker.readAll ()).trimmed ();
        if (slot == QStringLiteral ("a") || slot == QStringLiteral ("b"))
            return slot;
    }
    for (const QString& slot : {QStringLiteral ("a"), QStringLiteral ("b")}) {
        if (QFileInfo (QDir (autosaveRootDirectory (projectFilePath)).filePath (
                QStringLiteral ("slot-") + slot + QStringLiteral ("/snapshot.rwproj"))).isFile ())
            return slot;
    }
    return QStringLiteral ("a");
}

QString autosaveDirectory (const QString& projectFilePath)
{
    return QDir (autosaveRootDirectory (projectFilePath)).filePath (
        QStringLiteral ("slot-") + activeAutosaveSlot (projectFilePath));
}

QString autosaveProjectFile (const QString& projectFilePath)
{
    return QDir (autosaveDirectory (projectFilePath)).filePath (QStringLiteral ("snapshot.rwproj"));
}

// 只复制项目真正拥有的资源。external 资源的路径和生命周期由外部系统负责，恢复快照若
// 复制它们会把外部文件意外带入项目，也可能在恢复时覆盖用户随后更新的外部数据。
bool copyOwnedProjectResources (const QString& sourceProjectFile,
                                const ProjectManifest& manifest,
                                const QString& targetProjectFile,
                                const bool rejectExistingTargets,
                                QString* error)
{
    QSet< QString > copiedPaths;
    for (const ProjectResource& resource : manifest.resources) {
        if (resource.ownership == QStringLiteral ("external"))
            continue;

        QString sourcePath;
        QString targetPath;
        if (!ProjectPathResolver::resolveResource (sourceProjectFile, resource, sourcePath, error) ||
            !ProjectPathResolver::resolveResource (targetProjectFile, resource, targetPath, error))
            return false;
        if (!QFileInfo (sourcePath).exists () || !QFileInfo (sourcePath).isFile ()) {
            setError (error,
                      QString::fromUtf8 ("无法创建恢复快照，项目资源不存在：%1（资源 ID：%2）。")
                          .arg (sourcePath)
                          .arg (resource.id));
            return false;
        }
        // 多个资源可以有意共享同一相对文件；快照只复制一次，避免第二次复制因目标已存在
        // 被误判为失败，同时仍保留清单中各自的稳定资源 ID。
        if (copiedPaths.contains (targetPath))
            continue;
        if (!copyFileAtomically (sourcePath, targetPath, error, rejectExistingTargets))
            return false;
        copiedPaths.insert (targetPath);
    }
    return true;
}

// 恢复前必须一次性确认快照中的每个自有资源均可解析且真实存在。不能边发现边复制，否则
// 后面的缺失资源会让前面的正式文件已经被覆盖，破坏“拒绝恢复时项目仍保持原状”的约定。
bool validateOwnedProjectResources (const QString& projectFile,
                                    const ProjectManifest& manifest,
                                    QString* error)
{
    for (const ProjectResource& resource : manifest.resources) {
        if (resource.ownership == QStringLiteral ("external"))
            continue;
        QString resourcePath;
        if (!ProjectPathResolver::resolveResource (projectFile, resource, resourcePath, error))
            return false;
        if (!QFileInfo (resourcePath).exists () || !QFileInfo (resourcePath).isFile ()) {
            if (!resource.required)
                continue;
            setError (error,
                      QString::fromUtf8 ("恢复快照中的项目资源不存在：%1（资源 ID：%2）。")
                          .arg (resourcePath)
                          .arg (resource.id));
            return false;
        }
    }
    return true;
}

// 指纹采用 SHA-256 的十六进制摘要，且以分块读取方式处理大网格文件。摘要只用于诊断，
// 不写回 rwproj，因而不会改变既有项目格式或给正常保存流程增加兼容性负担。
QByteArray fileFingerprint (const QString& filePath)
{
    QFile file (filePath);
    if (!file.open (QIODevice::ReadOnly))
        return QByteArray ();
    QCryptographicHash hash (QCryptographicHash::Sha256);
    while (!file.atEnd ()) {
        const QByteArray chunk = file.read (64 * 1024);
        if (chunk.isEmpty () && file.error () != QFile::NoError)
            return QByteArray ();
        hash.addData (chunk);
    }
    return hash.result ().toHex ();
}

// 判断一个已规范化的绝对路径是否仍位于给定目录内。迁移 WorkCell 时仅允许携带源 WorkCell
// 同目录树中的相对依赖；若 Include 或几何文件通过 ".." 逃出该目录，项目就不再自包含，
// 必须在创建阶段明确失败，而不是把项目外的绝对路径悄悄保留下来。
bool isInsideDirectory (const QString& directoryPath, const QString& candidatePath)
{
    const QString root = QDir::cleanPath (QFileInfo (directoryPath).absoluteFilePath ());
    const QString candidate = QDir::cleanPath (QFileInfo (candidatePath).absoluteFilePath ());
    const QString relative = QDir::fromNativeSeparators (QDir (root).relativeFilePath (candidate));
    return relative != QStringLiteral ("..") && !relative.startsWith (QStringLiteral ("../")) &&
        !QFileInfo (relative).isAbsolute ();
}

// WorkCellLoader 支持把 "geometry/robotFlange" 这类无扩展名路径解析为同目录的
// robotFlange.ac、robotFlange.stl 等真实网格文件。项目迁移必须复用这一语义：若精确
// 文件存在优先复制它；仅当引用本身没有扩展名且精确文件缺失时，才收集所有同基名候选，
// 让复制后的项目继续由加载器按原有优先级选择可用格式。
QStringList resolveWorkCellDependencySources (const QString& sourceDirectory,
                                              const QString& reference)
{
    const QString declaredPath = QDir::cleanPath (
        QDir (sourceDirectory).absoluteFilePath (reference));
    const QFileInfo declaredInfo (declaredPath);
    if (declaredInfo.exists () && declaredInfo.isFile ())
        return {declaredInfo.absoluteFilePath ()};

    // 带扩展名的引用属于精确文件路径；保留原路径交给后续复制逻辑报出“文件不存在”，
    // 不扩大匹配范围，避免把拼写错误悄悄替换为不相关资源。
    if (!QFileInfo (reference).suffix ().isEmpty ())
        return {declaredPath};

    const QString requestedBaseName = QFileInfo (reference).fileName ();
    QStringList resolvedPaths;
    const QFileInfoList candidates = QDir (declaredInfo.absolutePath ()).entryInfoList (
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& candidate : candidates) {
        // baseName 去除所有扩展名，因此 robotFlange.ac 与 robotFlange.stl 都能匹配
        // robotFlange；同时保留完整原文件名，XML 无需重写。
        if (candidate.baseName () == requestedBaseName)
            resolvedPaths.push_back (candidate.absoluteFilePath ());
    }
    return resolvedPaths.isEmpty () ? QStringList ({declaredPath}) : resolvedPaths;
}

// 递归复制 WorkCell XML 中所有 file 属性引用的依赖。目标目录以入口 WorkCell 所在的
// scenes 目录为根，源目录的相对层级原样保留，因而 XML 内无需改写 Include、CollisionSetup、
// Polytope 等已有相对路径，加载器在新项目中仍会得到相同的解析结果。
bool copyWorkCellDependencyTree (const QString& sourcePath,
                                 const QString& targetPath,
                                 const QString& sourceRootDirectory,
                                 const QString& targetRootDirectory,
                                 QSet< QString >& visitedSourcePaths,
                                 QStringList& copiedTargetPaths,
                                 QString* error)
{
    const QString sourceFile = QFileInfo (sourcePath).absoluteFilePath ();
    const QString targetFile = QFileInfo (targetPath).absoluteFilePath ();
    if (!visitedSourcePaths.contains (sourceFile)) {
        visitedSourcePaths.insert (sourceFile);
        // 源文件本来就在项目目标位置时不复制自身，避免 QSaveFile 覆盖正在读取的源文件；
        // 仍继续扫描它的引用，以便把同目录下尚未进入项目的依赖一并收集。
        if (QDir::cleanPath (sourceFile) != QDir::cleanPath (targetFile)) {
            if (!copyFileAtomically (sourceFile, targetFile, error))
                return false;
            copiedTargetPaths.push_back (targetFile);
        }
    }

    // 非 XML 文件（网格、纹理、二进制碰撞模型等）是叶子资源，复制完成后无需再解析。
    if (QFileInfo (sourceFile).suffix ().compare (QStringLiteral ("xml"), Qt::CaseInsensitive) != 0)
        return true;

    QFile input (sourceFile);
    if (!input.open (QIODevice::ReadOnly | QIODevice::Text)) {
        setError (error,
                  QString::fromUtf8 ("无法读取 WorkCell 依赖 XML：%1。").arg (input.errorString ()));
        return false;
    }

    QXmlStreamReader xml (&input);
    const QString sourceDirectory = QFileInfo (sourceFile).absolutePath ();
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement ())
            continue;

        const QString reference = xml.attributes ().value (QStringLiteral ("file")).toString ().trimmed ();
        if (reference.isEmpty ())
            continue;
        if (QFileInfo (reference).isAbsolute ()) {
            setError (error,
                      QString::fromUtf8 ("WorkCell 依赖不能使用绝对路径：%1。").arg (reference));
            return false;
        }

        const QString declaredDependencySource = QDir::cleanPath (
            QDir (sourceDirectory).absoluteFilePath (reference));
        if (!isInsideDirectory (sourceRootDirectory, declaredDependencySource)) {
            setError (error,
                      QString::fromUtf8 ("WorkCell 依赖越出源目录：%1。").arg (reference));
            return false;
        }

        const QStringList dependencySources =
            resolveWorkCellDependencySources (sourceDirectory, reference);
        for (const QString& dependencySource : dependencySources) {
            // 同基名候选与声明路径都必须受源目录边界保护，防止目录扫描或符号链接意外把
            // 项目外文件带入新项目。路径通过后按相对位置落到 scenes 根下。
            if (!isInsideDirectory (sourceRootDirectory, dependencySource)) {
                setError (error,
                          QString::fromUtf8 ("WorkCell 依赖越出源目录：%1。").arg (reference));
                return false;
            }
            const QString relativeDependency =
                QDir (sourceRootDirectory).relativeFilePath (dependencySource);
            const QString dependencyTarget = QDir (targetRootDirectory).filePath (relativeDependency);
            if (!copyWorkCellDependencyTree (dependencySource,
                                             dependencyTarget,
                                             sourceRootDirectory,
                                             targetRootDirectory,
                                             visitedSourcePaths,
                                             copiedTargetPaths,
                                             error))
                return false;
        }
    }

    if (xml.hasError ()) {
        setError (error,
                  QString::fromUtf8 ("WorkCell 依赖 XML 格式错误（第 %1 行）：%2。")
                      .arg (xml.lineNumber ())
                      .arg (xml.errorString ()));
        return false;
    }
    return true;
}

// 迁移过程任一步失败时，仅删除本次实际写入的文件。调用者可能选择在已有目录中创建项目，
// 因此不能递归删除整个目录，更不能误删用户此前已经存在的其它项目资源。
void removeCopiedWorkCellDependencies (const QStringList& copiedTargetPaths)
{
    for (auto path = copiedTargetPaths.crbegin (); path != copiedTargetPaths.crend (); ++path)
        QFile::remove (*path);
}

QStringList missingParentDirectories (const QStringList& targetPaths, const QString& projectRoot)
{
    QSet< QString > missing;
    const QString normalizedRoot = QDir::cleanPath (QFileInfo (projectRoot).absoluteFilePath ());
    for (const QString& targetPath : targetPaths) {
        QString directory = QFileInfo (targetPath).absolutePath ();
        while (directory != normalizedRoot && isInsideDirectory (normalizedRoot, directory) &&
               !QFileInfo::exists (directory)) {
            missing.insert (directory);
            const QString parent = QFileInfo (directory).absolutePath ();
            if (parent == directory)
                break;
            directory = parent;
        }
    }
    QStringList result = missing.values ();
    std::sort (result.begin (), result.end (), [] (const QString& left, const QString& right) {
        return left.size () > right.size ();
    });
    return result;
}

void removeCreatedDirectoriesIfEmpty (const QStringList& directories)
{
    for (const QString& directory : directories) {
        if (QFileInfo (directory).isDir () &&
            QDir (directory).entryList (QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
                .isEmpty ()) {
            QDir ().rmdir (directory);
        }
    }
}

}    // namespace

bool ProjectManager::createProject (const QString& projectFilePath,
                                    const ProjectManifest& manifest,
                                    QString* error)
{
    // 在调用方传入的清单副本上补全自动生成的元数据，避免把写入的内容又改回传入对象。
    ProjectManifest candidate = manifest;
    if (candidate.project.id.trimmed ().isEmpty ())
        candidate.project.id = QUuid::createUuid ().toString (QUuid::WithoutBraces);
    if (candidate.project.createdAt.isEmpty ())
        candidate.project.createdAt = nowUtc ();
    candidate.project.modifiedAt = nowUtc ();
    if (candidate.project.application.isEmpty ())
        candidate.project.application = QStringLiteral ("RobWorkStudio");

    // 先做结构校验，再做路径校验；两者都通过才写盘。
    if (!ProjectManifestJson::validate (candidate, error))
        return false;
    if (projectFilePath.trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("项目文件路径不能为空。"));
        return false;
    }

    const QFileInfo projectInfo (projectFilePath);
    const QString absoluteProjectFile = projectInfo.absoluteFilePath ();
    // 目标目录可能尚未创建（例如用户在保存对话框中输入了新目录名），这里主动补建。
    if (!QDir ().mkpath (projectInfo.absolutePath ())) {
        setError (error,
                  QString::fromUtf8 ("无法创建项目目录：%1。").arg (projectInfo.absolutePath ()));
        return false;
    }
    if (!writeManifest (absoluteProjectFile, candidate, error))
        return false;

    // 全部成功后才接管项目上下文，并复位脏标记。
    _projectFilePath = QDir::cleanPath (absoluteProjectFile);
    _manifest = candidate;
    _dirty = false;
    return true;
}

bool ProjectManager::createProjectFromWorkCell (const QString& projectFilePath,
                                                const QString& sourceWorkCellPath,
                                                QString* error)
{
    // 先校验源文件和项目清单路径，避免复制已经完成后才发现无法创建项目文件。这里刻意
    // 拒绝覆盖同名 .rwproj，防止“从 WorkCell 创建”误把已有项目的清单替换掉。
    const QFileInfo sourceInfo (sourceWorkCellPath);
    if (!sourceInfo.exists () || !sourceInfo.isFile ()) {
        setError (error,
                  QString::fromUtf8 ("WorkCell 源文件不存在或不是普通文件：%1。").arg (
                      sourceWorkCellPath));
        return false;
    }
    if (projectFilePath.trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("项目文件路径不能为空。"));
        return false;
    }

    const QString absoluteProjectFile = QFileInfo (projectFilePath).absoluteFilePath ();
    if (QFileInfo::exists (absoluteProjectFile)) {
        setError (error,
                  QString::fromUtf8 ("目标项目文件已存在，拒绝覆盖：%1。").arg (
                      absoluteProjectFile));
        return false;
    }

    ProjectManifest manifest;
    manifest.project.name = QFileInfo (absoluteProjectFile).completeBaseName ();
    manifest.project.description = QString::fromUtf8 ("从现有 WorkCell 迁移创建的项目");
    manifest.settings.insert (QStringLiteral ("pathPolicy"), QStringLiteral ("project-relative"));

    ProjectResource workCell;
    workCell.id = QStringLiteral ("scene.main");
    workCell.kind = QStringLiteral ("robwork.workcell");
    workCell.path = QStringLiteral ("scenes/main.wc.xml");
    workCell.ownership = QStringLiteral ("project");
    workCell.required = true;
    QString targetWorkCell;
    if (!ProjectPathResolver::resolveResource (
            absoluteProjectFile, workCell, targetWorkCell, error))
        return false;
    QSet< QString > visitedSourcePaths;
    QStringList copiedTargetPaths;
    // 入口仍使用固定的 scenes/main.wc.xml，以保持项目清单稳定；递归复制器会把其同目录下
    // 的相对依赖搬入 scenes 下的对应位置，使 XML 的原始相对引用在新项目中无需改写。
    if (!copyWorkCellDependencyTree (sourceInfo.absoluteFilePath (),
                                     targetWorkCell,
                                     sourceInfo.absolutePath (),
                                     QFileInfo (targetWorkCell).absolutePath (),
                                     visitedSourcePaths,
                                     copiedTargetPaths,
                                     error)) {
        removeCopiedWorkCellDependencies (copiedTargetPaths);
        return false;
    }

    // XML 语法正确不代表它是 RobWork 能加载的 WorkCell/Device。必须在清单接管项目
    // 上下文前用真实加载器校验复制结果，避免创建出只能写入、无法再次打开的项目。
    try {
        const rw::models::WorkCell::Ptr copiedWorkCell =
            rw::loaders::WorkCellLoader::Factory::load (targetWorkCell.toStdString ());
        if (copiedWorkCell.isNull ()) {
            setError (error,
                      QString::fromUtf8 ("复制后的 WorkCell 无法加载：%1。").arg (
                          targetWorkCell));
            removeCopiedWorkCellDependencies (copiedTargetPaths);
            return false;
        }
    }
    catch (const std::exception& exception) {
        setError (error,
                  QString::fromUtf8 ("复制后的 WorkCell 校验失败：%1：%2。").arg (
                      targetWorkCell, QString::fromUtf8 (exception.what ())));
        removeCopiedWorkCellDependencies (copiedTargetPaths);
        return false;
    }

    // 入口之外的递归依赖同样属于项目。它们不需要编辑 Provider，但必须进入清单，
    // 否则 clone/rwpack 只会携带 main.wc.xml，迁移后的相对 Include 和几何引用会失效。
    QVector< ProjectResource > passiveAssets;
    const QDir projectDirectory (QFileInfo (absoluteProjectFile).absolutePath ());
    int assetIndex = 0;
    for (const QString& copiedPath : copiedTargetPaths) {
        if (QDir::cleanPath (copiedPath) == QDir::cleanPath (targetWorkCell))
            continue;
        ProjectResource asset;
        asset.id = QStringLiteral ("scene.asset.%1").arg (++assetIndex);
        asset.kind = QStringLiteral ("robwork.passive-asset");
        asset.path = QDir::fromNativeSeparators (
            projectDirectory.relativeFilePath (QFileInfo (copiedPath).absoluteFilePath ()));
        asset.ownership = QStringLiteral ("project");
        asset.required = true;
        passiveAssets.push_back (asset);
        workCell.dependencies.push_back (asset.id);
    }
    manifest.resources.push_back (workCell);
    for (const ProjectResource& asset : passiveAssets)
        manifest.resources.push_back (asset);
    manifest.entryPoints.insert (QStringLiteral ("mainWorkCell"), workCell.id);

    // createProject 仅在写入清单成功后才接管当前上下文。若清单写入失败，刚复制的资源不再
    // 有任何清单引用，因此主动删除它，避免在目标目录留下误导性的半成品项目。
    if (!createProject (absoluteProjectFile, manifest, error)) {
        removeCopiedWorkCellDependencies (copiedTargetPaths);
        return false;
    }
    return true;
}

bool ProjectManager::prepareProjectFromRobotFile (const QString& projectFilePath,
                                                  const QString& sourceUrdfPath,
                                                  PreparedRobotProject& prepared,
                                                  QString* error) const
{
    if (prepared.activated) {
        setError (error, QString::fromUtf8 ("已激活的机器人候选项目必须先完成或回滚。"));
        return false;
    }
    discardPreparedRobotProject (prepared);
    if (error != nullptr)
        error->clear ();

    const QString absoluteProjectFile = QFileInfo (projectFilePath).absoluteFilePath ();
    PackagedRobotSource packaged;
    if (!RobotProjectSourcePackager::prepare (
            sourceUrdfPath, absoluteProjectFile, packaged, error)) {
        return false;
    }

    ProjectManifest manifest;
    manifest.project.id = QUuid::createUuid ().toString (QUuid::WithoutBraces);
    manifest.project.name = QFileInfo (absoluteProjectFile).completeBaseName ();
    manifest.project.description = QString::fromUtf8 ("从 URDF 机器人文件创建的项目");
    manifest.project.createdAt = nowUtc ();
    manifest.project.modifiedAt = manifest.project.createdAt;
    manifest.project.application = QStringLiteral ("RobWorkStudio");
    manifest.settings.insert (QStringLiteral ("pathPolicy"), QStringLiteral ("project-relative"));
    manifest.resources.push_back (packaged.sourceResource);
    for (const ProjectResource& asset : packaged.assetResources)
        manifest.resources.push_back (asset);
    manifest.entryPoints.insert (QStringLiteral ("robotSource"), packaged.sourceResource.id);
    if (!ProjectManifestJson::validate (manifest, error)) {
        RobotProjectSourcePackager::discard (packaged);
        return false;
    }

    prepared.projectFilePath = QDir::cleanPath (absoluteProjectFile);
    prepared.manifest = manifest;
    prepared.packaged = packaged;
    return true;
}

bool ProjectManager::activatePreparedRobotProject (PreparedRobotProject& prepared, QString* error)
{
    if (prepared.activated || prepared.projectFilePath.isEmpty () ||
        prepared.packaged.stagedFilesByProjectPath.isEmpty ()) {
        setError (error, QString::fromUtf8 ("机器人候选项目尚未准备完成或已经激活。"));
        return false;
    }
    if (!ProjectManifestJson::validate (prepared.manifest, error))
        return false;

    const QDir projectRoot (QFileInfo (prepared.projectFilePath).absolutePath ());
    QMap< QString, QString > stagedByTarget;
    for (auto item = prepared.packaged.stagedFilesByProjectPath.constBegin ();
         item != prepared.packaged.stagedFilesByProjectPath.constEnd ();
         ++item) {
        stagedByTarget.insert (QFileInfo (projectRoot.filePath (item.key ())).absoluteFilePath (),
                               item.value ());
    }
    QStringList targetPaths = stagedByTarget.keys ();
    targetPaths.push_back (prepared.projectFilePath);
    const QStringList createdDirectories =
        missingParentDirectories (targetPaths, projectRoot.absolutePath ());

    const QByteArray manifestBytes = ProjectManifestJson::toJson (prepared.manifest);
    QHash< QString, QByteArray > expectedHashes;
    for (auto item = stagedByTarget.constBegin (); item != stagedByTarget.constEnd (); ++item) {
        const QByteArray hash = fileFingerprint (item.value ());
        if (hash.isEmpty ()) {
            setError (error,
                      QString::fromUtf8 ("无法读取机器人候选暂存文件：%1。").arg (
                          item.value ()));
            return false;
        }
        expectedHashes.insert (item.key (), hash);
    }
    expectedHashes.insert (
        prepared.projectFilePath,
        QCryptographicHash::hash (manifestBytes, QCryptographicHash::Sha256).toHex ());

    ProjectSaveTransaction transaction (
        ProjectSaveTransaction::ExistingTargetPolicy::Reject);
    for (auto item = stagedByTarget.constBegin (); item != stagedByTarget.constEnd (); ++item) {
        if (!transaction.stageCopy (item.value (), item.key (), error)) {
            removeCreatedDirectoriesIfEmpty (createdDirectories);
            return false;
        }
    }
    if (!transaction.stageBytes (manifestBytes, prepared.projectFilePath, error) ||
        !transaction.commit (error)) {
        removeCreatedDirectoriesIfEmpty (createdDirectories);
        return false;
    }

    QStringList verificationErrors;
    for (const QString& path : targetPaths) {
        if (fileFingerprint (path) != expectedHashes.value (path))
            verificationErrors.push_back (path);
    }
    if (!verificationErrors.isEmpty ()) {
        for (const QString& path : targetPaths) {
            if (fileFingerprint (path) == expectedHashes.value (path))
                QFile::remove (path);
        }
        removeCreatedDirectoriesIfEmpty (createdDirectories);
        setError (error,
                  QString::fromUtf8 ("机器人项目提交校验失败；外部变更已保留：%1。")
                      .arg (verificationErrors.join (QStringLiteral (", "))));
        return false;
    }

    prepared.previousProjectFilePath = _projectFilePath;
    prepared.previousManifest = _manifest;
    prepared.previousDirty = _dirty;
    prepared.committedProjectPaths = targetPaths;
    prepared.committedContentHashes = expectedHashes;
    prepared.createdProjectDirectories = createdDirectories;
    prepared.activated = true;
    _projectFilePath = prepared.projectFilePath;
    _manifest = prepared.manifest;
    _dirty = false;
    RobotProjectSourcePackager::discard (prepared.packaged);
    return true;
}

bool ProjectManager::rollbackActivatedRobotProject (PreparedRobotProject& prepared, QString* error)
{
    if (!prepared.activated) {
        setError (error, QString::fromUtf8 ("机器人候选项目尚未激活，无法回滚。"));
        return false;
    }

    QStringList cleanupErrors;
    for (auto path = prepared.committedProjectPaths.crbegin ();
         path != prepared.committedProjectPaths.crend ();
         ++path) {
        const QByteArray currentHash = fileFingerprint (*path);
        if (currentHash.isEmpty ()) {
            cleanupErrors.push_back (QString::fromUtf8 ("提交文件已缺失，未删除：%1").arg (*path));
            continue;
        }
        if (currentHash != prepared.committedContentHashes.value (*path)) {
            cleanupErrors.push_back (QString::fromUtf8 ("提交文件已被外部修改，已保留：%1").arg (*path));
            continue;
        }
        if (!QFile::remove (*path))
            cleanupErrors.push_back (QString::fromUtf8 ("无法删除本次提交文件：%1").arg (*path));
    }
    removeCreatedDirectoriesIfEmpty (prepared.createdProjectDirectories);

    _projectFilePath = prepared.previousProjectFilePath;
    _manifest = prepared.previousManifest;
    _dirty = prepared.previousDirty;
    const bool clean = cleanupErrors.isEmpty ();
    if (!clean)
        setError (error, cleanupErrors.join (QLatin1Char ('\n')));
    prepared = PreparedRobotProject {};
    return clean;
}

void ProjectManager::discardPreparedRobotProject (PreparedRobotProject& prepared)
{
    if (!prepared.activated)
        RobotProjectSourcePackager::discard (prepared.packaged);
    prepared = PreparedRobotProject {};
}

bool ProjectManager::openProject (const QString& projectFilePath, QString* error)
{
    // 先确认文件真实存在且是普通文件（而非目录等），避免后续读取报出难懂的错误。
    const QFileInfo projectInfo (projectFilePath);
    if (!projectInfo.exists () || !projectInfo.isFile ()) {
        setError (error,
                  QString::fromUtf8 ("项目文件不存在：%1。").arg (projectFilePath));
        return false;
    }

    QFile file (projectInfo.absoluteFilePath ());
    if (!file.open (QIODevice::ReadOnly)) {
        setError (error,
                  QString::fromUtf8 ("无法读取项目文件：%1。").arg (file.errorString ()));
        return false;
    }

    ProjectManifest candidate;
    if (!ProjectManifestJson::fromJson (file.readAll (), candidate, error))
        return false;

    // JSON 结构校验不能替代路径安全校验。打开项目时预先解析每一个 project/generated
    // 资源，尽早报告越界路径，避免后续插件加载阶段才以不明确的文件错误失败。
    for (const ProjectResource& resource : candidate.resources) {
        QString resolvedPath;
        if (!ProjectPathResolver::resolveResource (projectInfo.absoluteFilePath (),
                                                   resource,
                                                   resolvedPath,
                                                   error))
            return false;

        // required 资源属于项目能够正常工作的最低集合。只有所有必需资源都存在时
        // 才替换当前项目上下文，从而保证打开失败不会破坏用户原来已打开的项目。
        if (resource.required && !QFileInfo::exists (resolvedPath)) {
            setError (error,
                      QString::fromUtf8 ("项目必需资源不存在：%1（资源 ID：%2）。")
                          .arg (resolvedPath)
                          .arg (resource.id));
            return false;
        }
    }

    _projectFilePath = QDir::cleanPath (projectInfo.absoluteFilePath ());
    _manifest = candidate;
    _dirty = false;
    return true;
}

bool ProjectManager::saveProject (QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }

    // 在副本上更新修改时间并重新校验，校验或写盘失败时内存清单保持原样。
    ProjectManifest candidate = _manifest;
    candidate.project.modifiedAt = nowUtc ();
    if (!ProjectManifestJson::validate (candidate, error))
        return false;
    if (!writeManifest (_projectFilePath, candidate, error))
        return false;

    // 写盘成功后，用写入过的副本替换内存清单，并清除脏标记。
    _manifest = candidate;
    _dirty = false;
    return true;
}

bool ProjectManager::importResource (const QString& sourcePath,
                                     const ProjectResource& resource,
                                     QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }

    // 历史文件导入会被复制进项目，因此只接受 project 归属。generated 代表软件后续生成
    // 的产物，external 则不复制且不具备可搬迁性，二者都不应混入本导入入口。
    ProjectResource imported = resource;
    if (imported.ownership.trimmed ().isEmpty ())
        imported.ownership = QStringLiteral ("project");
    if (imported.ownership != QStringLiteral ("project")) {
        setError (error,
                  QString::fromUtf8 ("导入资源的 ownership 必须为 project。"));
        return false;
    }

    // 先把候选资源加入副本并运行完整清单校验。这样重复 ID、空 kind、非法依赖等错误均在
    // 任何磁盘写入前返回，失败时 _manifest 和 _dirty 保持原值。
    ProjectManifest candidate = _manifest;
    candidate.resources.push_back (imported);
    if (!ProjectManifestJson::validate (candidate, error))
        return false;

    QString targetPath;
    if (!ProjectPathResolver::resolveResource (_projectFilePath, imported, targetPath, error))
        return false;
    if (!copyFileAtomically (sourcePath, targetPath, error))
        return false;

    // 文件原子提交成功后，内存清单才开始引用它。清单由调用方下一次 saveProject 统一写入，
    // 以便与 Provider 的多资源保存事务保持相同的提交顺序。
    _manifest = candidate;
    _dirty = true;
    return true;
}

bool ProjectManager::addGeneratedResource (const ProjectResource& resource, QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }

    // 生成资源必须先在候选清单上完成结构和路径校验，再替换内存清单。目标文件此时可以
    // 尚不存在，因为它会由 Provider 的后续暂存保存创建；这里绝不能把空文件提前写入磁盘。
    ProjectManifest candidate = _manifest;
    candidate.resources.push_back (resource);
    if (!ProjectManifestJson::validate (candidate, error))
        return false;
    QString resolvedPath;
    if (!ProjectPathResolver::resolveResource (_projectFilePath, resource, resolvedPath, error))
        return false;

    _manifest = candidate;
    _dirty = true;
    return true;
}

// 原子替换既有资源的描述，并把新引用的被动资产加入清单。资源 ID 保持不变，
// 因此入口与其它插件依赖无需重写；全部候选项通过清单校验后才修改当前项目。
bool ProjectManager::replaceResourceAndAddAssets (
    const ProjectResource& resource,
    const QVector< ProjectResource >& assets,
    QString* error)
{
    return replaceResourceAndAssets (resource, assets, false, error);
}

bool ProjectManager::replaceResourceAndReconcileGeneratedSceneAssets (
    const ProjectResource& resource,
    const QVector< ProjectResource >& assets,
    QString* error)
{
    return replaceResourceAndAssets (resource, assets, true, error);
}

bool ProjectManager::replaceResourceAndAssets (
    const ProjectResource& resource,
    const QVector< ProjectResource >& assets,
    bool reconcileGeneratedSceneAssets,
    QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目，无法替换项目资源。"));
        return false;
    }

    ProjectManifest candidate = _manifest;
    int resourceIndex = -1;
    for (int index = 0; index < candidate.resources.size (); ++index) {
        if (candidate.resources[index].id == resource.id) {
            resourceIndex = index;
            break;
        }
    }
    if (resourceIndex < 0) {
        setError (error, QString::fromUtf8 ("待替换的项目资源不存在：%1。").arg (resource.id));
        return false;
    }

    candidate.resources[resourceIndex] = resource;
    for (const ProjectResource& asset : assets) {
        int existingIndex = -1;
        for (int index = 0; index < candidate.resources.size (); ++index) {
            if (candidate.resources[index].id == asset.id) {
                existingIndex = index;
                break;
            }
        }
        if (existingIndex >= 0)
            candidate.resources[existingIndex] = asset;
        else
            candidate.resources.push_back (asset);
    }

    if (reconcileGeneratedSceneAssets) {
        QSet< QString > currentAssetIds;
        for (const ProjectResource& asset : assets)
            currentAssetIds.insert (asset.id);

        QSet< QString > referencedResourceIds;
        for (const ProjectResource& candidateResource : candidate.resources) {
            for (const QString& dependency : candidateResource.dependencies)
                referencedResourceIds.insert (dependency);
        }
        for (auto entryPoint = candidate.entryPoints.constBegin ();
             entryPoint != candidate.entryPoints.constEnd (); ++entryPoint) {
            referencedResourceIds.insert (entryPoint.value ());
        }

        for (int index = candidate.resources.size () - 1; index >= 0; --index) {
            const ProjectResource& candidateResource = candidate.resources[index];
            if (candidateResource.id.startsWith (QStringLiteral ("scene.generated.")) &&
                candidateResource.ownership == QStringLiteral ("generated") &&
                !currentAssetIds.contains (candidateResource.id) &&
                !referencedResourceIds.contains (candidateResource.id)) {
                candidate.resources.removeAt (index);
            }
        }
    }

    if (!ProjectManifestJson::validate (candidate, error))
        return false;
    _manifest = candidate;
    _dirty = true;
    return true;
}

bool ProjectManager::addMainWorkCellAndAssets (
    const ProjectResource& workCell,
    const QVector< ProjectResource >& assets,
    QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目，无法注册主 WorkCell。"));
        return false;
    }
    if (_manifest.entryPoints.contains (QStringLiteral ("mainWorkCell"))) {
        setError (error, QString::fromUtf8 ("当前项目已经存在 mainWorkCell 入口。"));
        return false;
    }
    if (workCell.id != QStringLiteral ("scene.main") ||
        workCell.kind != QStringLiteral ("robwork.workcell")) {
        setError (error,
                  QString::fromUtf8 (
                      "首次生成的主 WorkCell 必须使用 scene.main 和 robwork.workcell。"));
        return false;
    }

    ProjectManifest candidate = _manifest;
    for (const ProjectResource& asset : assets)
        candidate.resources.push_back (asset);
    candidate.resources.push_back (workCell);
    candidate.entryPoints.insert (QStringLiteral ("mainWorkCell"), workCell.id);
    if (!ProjectManifestJson::validate (candidate, error))
        return false;

    QString resolvedPath;
    if (!ProjectPathResolver::resolveResource (
            _projectFilePath, workCell, resolvedPath, error))
        return false;
    for (const ProjectResource& asset : assets) {
        if (!ProjectPathResolver::resolveResource (
                _projectFilePath, asset, resolvedPath, error))
            return false;
    }

    _manifest = candidate;
    _dirty = true;
    return true;
}

bool ProjectManager::cloneProject (const QString& targetProjectFilePath, QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }
    if (targetProjectFilePath.trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("目标项目文件路径不能为空。"));
        return false;
    }

    const QString targetProjectFile = QFileInfo (targetProjectFilePath).absoluteFilePath ();
    const QString targetDirectory = QFileInfo (targetProjectFile).absolutePath ();
    if (QDir::cleanPath (targetProjectFile) == QDir::cleanPath (_projectFilePath)) {
        setError (error, QString::fromUtf8 ("不能将项目克隆到当前项目文件。"));
        return false;
    }

    // 克隆以“一个项目目录对应一个 .rwproj”为边界。要求目标目录完全不存在，能保证临时
    // 目录最终 rename 时是原子切换，也避免错误覆盖用户在同一目录下创建的其他文件。
    if (QFileInfo::exists (targetDirectory)) {
        setError (error,
                  QString::fromUtf8 ("目标项目目录必须不存在，拒绝写入已有目录：%1。").arg (
                      targetDirectory));
        return false;
    }
    const QString parentDirectory = QFileInfo (targetDirectory).absolutePath ();
    if (!QDir ().mkpath (parentDirectory)) {
        setError (error,
                  QString::fromUtf8 ("无法创建目标项目的父目录：%1。").arg (parentDirectory));
        return false;
    }

    ProjectManifest candidate = _manifest;
    candidate.project.id = QUuid::createUuid ().toString (QUuid::WithoutBraces);
    candidate.project.name = QFileInfo (targetProjectFile).completeBaseName ();
    candidate.project.createdAt = nowUtc ();
    candidate.project.modifiedAt = candidate.project.createdAt;
    if (!ProjectManifestJson::validate (candidate, error))
        return false;

    // 所有资源先写入同一父目录下的临时项目目录。临时目录与最终目录处于同一卷时，最后的
    // rename 不需要逐个移动文件；中途任一复制失败时 QTemporaryDir 自动清理整个暂存目录。
    QTemporaryDir stagingDirectory (
        QDir (parentDirectory).filePath (QStringLiteral (".rwproj-clone-XXXXXX")));
    if (!stagingDirectory.isValid ()) {
        setError (error, QString::fromUtf8 ("无法创建项目克隆临时目录。"));
        return false;
    }
    const QString stagedProjectFile =
        QDir (stagingDirectory.path ()).filePath (QFileInfo (targetProjectFile).fileName ());
    QSet< QString > copiedTargetPaths;
    for (const ProjectResource& resource : candidate.resources) {
        if (resource.ownership == QStringLiteral ("external"))
            continue;

        QString sourcePath;
        if (!ProjectPathResolver::resolveResource (_projectFilePath, resource, sourcePath, error))
            return false;
        if (!QFileInfo (sourcePath).exists () || !QFileInfo (sourcePath).isFile ()) {
            setError (error,
                      QString::fromUtf8 ("无法克隆缺失或非普通文件的项目资源：%1（资源 ID：%2）。")
                          .arg (sourcePath)
                          .arg (resource.id));
            return false;
        }

        QString targetPath;
        if (!ProjectPathResolver::resolveResource (
                stagedProjectFile, resource, targetPath, error))
            return false;
        // 多个资源允许共享同一项目内文件；路径相同意味着基于同一相对路径解析，复制一次即可。
        if (copiedTargetPaths.contains (targetPath))
            continue;
        if (!copyFileAtomically (sourcePath, targetPath, error))
            return false;
        copiedTargetPaths.insert (targetPath);
    }

    if (!writeManifest (stagedProjectFile, candidate, error))
        return false;
    if (!QDir ().rename (stagingDirectory.path (), targetDirectory)) {
        setError (error,
                  QString::fromUtf8 ("项目克隆目录提交失败：%1。").arg (targetDirectory));
        return false;
    }
    // rename 完成后临时目录原路径已不存在，关闭自动清理避免析构阶段对已提交项目执行删除。
    stagingDirectory.setAutoRemove (false);

    // 最终目录及清单都已完整落盘，现在才替换内存上下文；因此所有前置失败都保留源项目。
    _projectFilePath = QDir::cleanPath (targetProjectFile);
    _manifest = candidate;
    _dirty = false;
    return true;
}

bool ProjectManager::createAutosaveSnapshot (QString* error) const
{
    return createAutosaveSnapshot (nullptr, error);
}

bool ProjectManager::createAutosaveSnapshot (ProjectDocumentRegistry& documents, QString* error) const
{
    return createAutosaveSnapshot (&documents, error);
}

bool ProjectManager::createAutosaveSnapshot (ProjectDocumentRegistry* documents, QString* error) const
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目，无法创建恢复快照。"));
        return false;
    }

    const QString rootDirectory = autosaveRootDirectory (_projectFilePath);
    if (!QDir ().mkpath (rootDirectory)) {
        setError (error, QString::fromUtf8 ("无法创建项目恢复目录：%1。").arg (rootDirectory));
        return false;
    }

    // 整个快照先写到同一父目录中的临时目录。资源和清单都完成后才切换 autosave 目录，
    // 因而崩溃最多丢失本次快照，绝不会让恢复流程读取资源与清单版本不一致的半成品。
    QTemporaryDir stagingDirectory (QDir (rootDirectory).filePath (
        QStringLiteral (".autosave-stage-XXXXXX")));
    if (!stagingDirectory.isValid ()) {
        setError (error, QString::fromUtf8 ("无法创建恢复快照临时目录。"));
        return false;
    }
    const QString stagedProjectFile = QDir (stagingDirectory.path ()).filePath (
        QStringLiteral ("snapshot.rwproj"));
    if ((documents != nullptr && !documents->saveAutosaveResources (
            _manifest, _projectFilePath, stagedProjectFile, error)) ||
        (documents == nullptr && !copyOwnedProjectResources (
            _projectFilePath, _manifest, stagedProjectFile, true, error)) ||
        !writeManifest (stagedProjectFile, _manifest, error))
        return false;

    const QString activeSlot = activeAutosaveSlot (_projectFilePath);
    const QString inactiveSlot = activeSlot == QStringLiteral ("a") ? QStringLiteral ("b") : QStringLiteral ("a");
    const QString destinationDirectory = QDir (rootDirectory).filePath (QStringLiteral ("slot-") + inactiveSlot);
    if (QFileInfo::exists (destinationDirectory) && !QDir (destinationDirectory).removeRecursively ()) {
        setError (error, QString::fromUtf8 ("无法替换旧的恢复快照目录：%1。").arg (destinationDirectory));
        return false;
    }
    if (!QDir (rootDirectory).rename (QFileInfo (stagingDirectory.path ()).fileName (),
                                      QFileInfo (destinationDirectory).fileName ())) {
        setError (error, QString::fromUtf8 ("无法提交恢复快照目录：%1。").arg (destinationDirectory));
        return false;
    }
    stagingDirectory.setAutoRemove (false);
    QSaveFile marker (autosaveMarkerFile (_projectFilePath));
    if (!marker.open (QIODevice::WriteOnly) || marker.write (inactiveSlot.toUtf8 ()) != inactiveSlot.size () ||
        !marker.commit ()) {
        setError (error, QString::fromUtf8 ("无法切换恢复快照活动槽。"));
        return false;
    }
    return true;
}

bool ProjectManager::hasAutosaveSnapshot () const
{
    return hasProject () && QFileInfo (autosaveProjectFile (_projectFilePath)).isFile ();
}

bool ProjectManager::restoreAutosaveSnapshot (QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目，无法恢复自动保存快照。"));
        return false;
    }
    const QString snapshotFile = autosaveProjectFile (_projectFilePath);
    QFile input (snapshotFile);
    if (!input.open (QIODevice::ReadOnly)) {
        setError (error, QString::fromUtf8 ("无法读取恢复快照：%1。").arg (input.errorString ()));
        return false;
    }
    const QByteArray snapshotJson = input.readAll ();
    ProjectManifest snapshot;
    if (!ProjectManifestJson::fromJson (snapshotJson, snapshot, error))
        return false;
    // 快照的项目 ID 必须与当前打开项目一致，防止用户复制 .rwproject 目录后误将另一个项目
    // 的资源恢复进来。名称可以改，但稳定 UUID 不应在正常保存中发生变化。
    if (snapshot.project.id != _manifest.project.id) {
        setError (error, QString::fromUtf8 ("恢复快照不属于当前项目，已拒绝恢复。"));
        return false;
    }

    // 先完整校验快照中的全部资源都可读，再开始覆盖正式项目。这样缺文件、越界路径或损坏
    // 清单都会在任何正式资源改变前失败；实际写入沿用 QSaveFile 的单文件原子替换语义。
    QString validationError;
    if (!validateOwnedProjectResources (snapshotFile, snapshot, &validationError)) {
        setError (error, validationError);
        return false;
    }
    ProjectSaveTransaction transaction;
    QSet< QString > stagedTargets;
    for (const ProjectResource& resource : snapshot.resources) {
        if (resource.ownership == QStringLiteral ("external"))
            continue;
        QString sourcePath;
        QString targetPath;
        if (!ProjectPathResolver::resolveResource (snapshotFile, resource, sourcePath, error) ||
            !ProjectPathResolver::resolveResource (_projectFilePath, resource, targetPath, error))
            return false;
        if (!QFileInfo (sourcePath).isFile () && !resource.required)
            continue;
        if (!stagedTargets.contains (targetPath) && !transaction.stageCopy (sourcePath, targetPath, error))
            return false;
        stagedTargets.insert (targetPath);
    }
    if (!transaction.stageBytes (snapshotJson, _projectFilePath, error) || !transaction.commit (error))
        return false;
    _manifest = snapshot;
    _dirty = false;
    return true;
}

// 丢弃整个自动保存快照：成功保存、用户选择放弃或关闭项目后调用，删除 .rwproject/autosave
// 下全部快照槽，避免陈旧快照在下一次打开时被误当作可恢复状态。
bool ProjectManager::discardAutosaveSnapshot (QString* error) const
{
    if (!hasProject ())
        return true;
    const QString rootDirectory = autosaveRootDirectory (_projectFilePath);
    if (QFileInfo::exists (rootDirectory) && !QDir (rootDirectory).removeRecursively ()) {
        setError (error, QString::fromUtf8 ("无法删除恢复快照：%1。").arg (rootDirectory));
        return false;
    }
    return true;
}

QVector< ProjectManager::IntegrityIssue > ProjectManager::inspectIntegrity (QString* error) const
{
    QVector< IntegrityIssue > issues;
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目，无法执行完整性检查。"));
        return issues;
    }

    const QString projectDirectory = QFileInfo (_projectFilePath).absolutePath ();
    const QString snapshotFile = autosaveProjectFile (_projectFilePath);
    QSet< QString > declaredFiles;
    for (const ProjectResource& resource : _manifest.resources) {
        QString path;
        if (!ProjectPathResolver::resolveResource (_projectFilePath, resource, path, error))
            return {};
        const QString normalizedPath = QDir::cleanPath (QFileInfo (path).absoluteFilePath ());
        declaredFiles.insert (normalizedPath);
        if (resource.ownership != QStringLiteral ("external") && !QFileInfo (normalizedPath).isFile ()) {
            issues.push_back ({IntegrityIssue::Type::MissingResource,
                               resource.id,
                               normalizedPath,
                               QString::fromUtf8 ("已登记的项目资源不存在或不是普通文件。")});
            continue;
        }
        // 快照只包含项目自有资源。存在快照且两份文件均有效时，才可把摘要差异解释为“自
        // 上次自动保存后变化”，避免把首次保存或外部资源的自然变化错误标为项目问题。
        if (resource.ownership != QStringLiteral ("external") && QFileInfo (snapshotFile).isFile ()) {
            QString snapshotPath;
            if (ProjectPathResolver::resolveResource (snapshotFile, resource, snapshotPath, nullptr) &&
                QFileInfo (snapshotPath).isFile () && fileFingerprint (normalizedPath) != fileFingerprint (snapshotPath)) {
                issues.push_back ({IntegrityIssue::Type::ChangedSinceAutosave,
                                   resource.id,
                                   normalizedPath,
                                   QString::fromUtf8 ("资源内容与最近自动保存快照不一致。")});
            }
        }
    }

    // 扫描时排除 .rwproject：它只存放软件管理的快照和后续本机状态，不是可由用户清理的
    // 项目业务资源。rwproj 清单本身也不是孤儿资源，已登记文件则按绝对路径精确排除。
    QDirIterator files (projectDirectory, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext ()) {
        const QString path = QDir::cleanPath (QFileInfo (files.next ()).absoluteFilePath ());
        const QString relative = QDir::fromNativeSeparators (QDir (projectDirectory).relativeFilePath (path));
        if (path == QDir::cleanPath (_projectFilePath) || relative.startsWith (QStringLiteral (".rwproject/")) ||
            declaredFiles.contains (path))
            continue;
        issues.push_back ({IntegrityIssue::Type::UnreferencedFile,
                           QString (),
                           path,
                           QString::fromUtf8 ("项目目录内的文件未被 rwproj 清单引用。")});
    }
    return issues;
}

// 删除未引用（孤儿）文件：先重新执行完整性检查生成白名单，只有确属 UnreferencedFile
// 的路径才允许删除，防止误删用户业务文件；逐个删除失败即中止。
bool ProjectManager::removeUnreferencedFiles (const QStringList& paths, QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }
    const QVector< IntegrityIssue > issues = inspectIntegrity (error);
    if (error != nullptr && !error->isEmpty ())
        return false;
    // 以最新完整性检查结果为准重建“可删除”白名单。
    QSet< QString > removable;
    for (const IntegrityIssue& issue : issues) {
        if (issue.type == IntegrityIssue::Type::UnreferencedFile)
            removable.insert (QDir::cleanPath (QFileInfo (issue.path).absoluteFilePath ()));
    }
    // 先整体校验请求的路径都可删除，再执行删除，避免删到一半因非法路径中止。
    for (const QString& path : paths) {
        const QString normalized = QDir::cleanPath (QFileInfo (path).absoluteFilePath ());
        if (!removable.contains (normalized)) {
            setError (error, QString::fromUtf8 ("拒绝删除非未引用项目文件：%1。").arg (path));
            return false;
        }
    }
    for (const QString& path : paths) {
        if (!QFile::remove (path)) {
            setError (error, QString::fromUtf8 ("无法删除未引用项目文件：%1。").arg (path));
            return false;
        }
    }
    return true;
}

// 重新定位缺失资源：把用户选择的替换文件复制进项目(project 资源落在 recovered/ 目录、
// external 资源直接用绝对路径)，再以事务方式原子更新清单；失败时正式文件与清单均不变。
bool ProjectManager::relocateResource (const QString& resourceId,
                                       const QString& replacementPath,
                                       QString* error)
{
    if (!hasProject () || !QFileInfo (replacementPath).isFile ()) {
        setError (error, QString::fromUtf8 ("重新定位资源需要已打开项目和存在的替换文件。"));
        return false;
    }
    ProjectManifest candidate = _manifest;
    int index = -1;
    for (int resourceIndex = 0; resourceIndex < candidate.resources.size (); ++resourceIndex) {
        if (candidate.resources[resourceIndex].id == resourceId) {
            index = resourceIndex;
            break;
        }
    }
    if (index < 0) {
        setError (error, QString::fromUtf8 ("项目中不存在资源：%1。").arg (resourceId));
        return false;
    }

    ProjectResource& resource = candidate.resources[index];
    QString targetPath;
    if (resource.ownership == QStringLiteral ("external")) {
        resource.path = QDir::cleanPath (QFileInfo (replacementPath).absoluteFilePath ());
    }
    else {
        resource.path = QStringLiteral ("recovered/") + QFileInfo (replacementPath).fileName ();
        if (!ProjectPathResolver::resolveResource (_projectFilePath, resource, targetPath, error))
            return false;
    }
    candidate.project.modifiedAt = nowUtc ();
    if (!ProjectManifestJson::validate (candidate, error))
        return false;

    ProjectSaveTransaction transaction;
    if (!targetPath.isEmpty () && !transaction.stageCopy (replacementPath, targetPath, error))
        return false;
    if (!transaction.stageBytes (ProjectManifestJson::toJson (candidate), _projectFilePath, error) ||
        !transaction.commit (error))
        return false;
    _manifest = candidate;
    _dirty = false;
    return true;
}

// 导出 rwpack：把当前项目的清单与自有资源打包为可迁移归档。
bool ProjectManager::exportPackage (const QString& packageFilePath, QString* error) const
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目，无法导出 rwpack。"));
        return false;
    }
    if (packageFilePath.trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("rwpack 文件路径不能为空。"));
        return false;
    }
    // ProjectPackage 只接收已验证的当前清单，保证归档器不需要知晓 UI 或 Provider 状态；
    // 外部资源由其归属系统管理，显式排除后包可安全发送给其它机器。
    return ProjectPackage::create (_projectFilePath, _manifest, packageFilePath, error);
}

bool ProjectManager::extractPackage (const QString& packageFilePath,
                                    const QString& targetDirectory,
                                    QString& projectFilePath,
                                    QString* error)
{
    // 静态入口：解包到指定目录并返回内部 project.rwproj 路径，不自动切换当前项目。
    return ProjectPackage::extract (packageFilePath, targetDirectory, projectFilePath, error);
}

void ProjectManager::closeProject ()
{
    // 关闭不在这里静默保存。是否允许丢弃脏数据必须由上层 UI 或后续统一文档管理器
    // 决定；本类只负责清空当前项目上下文。
    _projectFilePath.clear ();
    _manifest = ProjectManifest ();
    _dirty = false;
}

bool ProjectManager::resolveResource (const QString& resourceId,
                                      QString& resolvedPath,
                                      QString* error) const
{
    // 外部资源解析入口：先确保有打开的项目，再按稳定 ID 查找资源定义。
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }

    // 通过 findResource 取出资源副本，避免外部持有 QVector 内部元素指针。
    ProjectResource resource;
    if (!_manifest.findResource (resourceId, resource)) {
        setError (error,
                  QString::fromUtf8 ("项目中不存在资源：%1。").arg (resourceId));
        return false;
    }
    // 真正的路径拼接与越界检查全部委托给 ProjectPathResolver。
    return ProjectPathResolver::resolveResource (_projectFilePath, resource, resolvedPath, error);
}

bool ProjectManager::writeManifest (const QString& projectFilePath,
                                    const ProjectManifest& manifest,
                                    QString* error) const
{
    // 使用 QSaveFile 实现“原子写入”：先写临时文件，全部成功后再 rename 覆盖原文件，
    // 避免断电或写入中断留下半截、损坏的项目清单。
    QSaveFile file (projectFilePath);
    if (!file.open (QIODevice::WriteOnly | QIODevice::Text)) {
        setError (error,
                  QString::fromUtf8 ("无法写入项目文件：%1。").arg (file.errorString ())); 
        return false;
    }

    const QByteArray json = ProjectManifestJson::toJson (manifest);
    if (file.write (json) != json.size ()) {
        setError (error,
                  QString::fromUtf8 ("项目文件写入不完整：%1。").arg (file.errorString ())); 
        return false;
    }
    if (!file.commit ()) {
        setError (error,
                  QString::fromUtf8 ("项目文件原子提交失败：%1。").arg (file.errorString ())); 
        return false;
    }
    return true;
}

}    // namespace rws
