// =============================================================================
//  文件: RobotModelBuilderPlugin.cpp
//  说明: RobotModelBuilder 插件入口实现。本文件非常薄,只负责把 Widget 装到
//        RobWorkStudio 中,并把 Widget 的"加载场景"信号转发给宿主,真正的建模
//        UI 和 XML 生成逻辑都在 RobotModelBuilderWidget / RobotModelXmlWriter 中。
// =============================================================================
#include "RobotModelBuilderPlugin.hpp"

#include "RobotModelBuilderWidget.hpp"
#include "RobotModelXmlWriter.hpp"
#include "WorkCellConverter.hpp"

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/ProjectPathResolver.hpp>
#include <rws/RobWorkStudio.hpp>

#include <rw/models/WorkCell.hpp>

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QScopedValueRollback>

using namespace rws;

// -----------------------------------------------------------------------------
//  构造函数
//  说明: 向基类传入插件名(显示在 RobWorkStudio 菜单/插件列表中)和图标(留空)。
//        _widget 暂时为空指针,待 initialize() 中再实例化。
// -----------------------------------------------------------------------------
RobotModelBuilderPlugin::RobotModelBuilderPlugin () :
    RobWorkStudioPlugin ("RobotModelBuilder", QIcon (":/robotmodelbuilder/robotmodelbuilder_icon.png")),
    _widget (NULL),
    _projectProvider (NULL),
    _ignoreNextOpenFromSelfLoad (false)
{}

// -----------------------------------------------------------------------------
//  析构函数
//  说明: Qt 的对象父子机制会在本对象销毁时自动 delete _widget,无需手动释放。
// -----------------------------------------------------------------------------
RobotModelBuilderPlugin::~RobotModelBuilderPlugin ()
{
    // 主窗口在应用退出前先关闭项目文档；此时 Registry 不再访问 Provider，因此插件
    // 可以安全释放自己的非 Qt Provider 对象，不把所有权错误转移给主窗口。
    delete _projectProvider;
}

// -----------------------------------------------------------------------------
//  initialize()
//  说明: RobWorkStudio 加载插件时调用一次。完成三件事:
//        1) 创建 UI(Widget)实例;
//        2) 把 Widget 的 loadSceneRequested 信号连接到此处的 loadSceneFile 槽;
//        3) 通过 setWidget 把 UI 注入到 RobWorkStudio 的 Dock/容器中。
// -----------------------------------------------------------------------------
void RobotModelBuilderPlugin::initialize ()
{
    _widget = new RobotModelBuilderWidget (this);
    _widget->setProjectPublishPromoter (
        [this] (const QString& filename, const QStringList& dependencies, QString* error) {
            RobWorkStudio* studio = getRobWorkStudio ();
            if (studio == NULL) {
                if (error != nullptr)
                    *error = QStringLiteral ("RobWorkStudio project services are unavailable.");
                return false;
            }
            QScopedValueRollback< bool > ignoreSelfLoad (_ignoreNextOpenFromSelfLoad, true);
            return studio->promoteGeneratedWorkCell (filename, dependencies, error);
        });
    // 项目上下文独立于 WorkCell 是否已保存或是否包含可转换机器人。插件初始化后立即订阅，
    // 并主动读取一次当前目录，确保在“新建空项目后再打开插件”时也不会回退到用户主目录。
    if (getRobWorkStudio () != NULL) {
        connect (getRobWorkStudio (), &RobWorkStudio::projectContextChanged, this,
                 [this] (const QString& projectDirectory) {
                     _widget->setProjectOutputDirectory (projectDirectory);
                 });
        _widget->setProjectOutputDirectory (getRobWorkStudio ()->projectDirectory ());
    }
    // 当 Widget 完成 "Save and Load" 操作时,会发出场景文件名,我们在这里负责真正去加载它
    connect (_widget, SIGNAL (loadSceneRequested (const QString&)), this,
             SLOT (loadSceneFile (const QString&)));

    _projectProvider = new CallbackProjectDocumentProvider (
        QStringLiteral ("rws.robot-model-builder"),
        QStringLiteral ("robwork.robot-model"),
        [this] (const QString& path, const ProjectDocumentContext& context, QString* error) {
            // Provider 每次加载前重申项目目录，旧版本 JSON 中的 saveDirectory 不得覆盖它。
            _widget->setProjectOutputDirectory (context.projectDirectory);
            return _widget->loadProjectDocument (path, error);
        },
        [this] (const QString& targetPath, const ProjectDocumentContext& context, QString* error) {
            // 保存事务写入临时文件，targetPath 不在正式项目目录；只能使用 Context 提供的
            // 项目根推导输出目录，不能误把暂存目录作为 XML 生成位置。
            _widget->setProjectOutputDirectory (context.projectDirectory);
            return _widget->saveProjectDocument (targetPath, error);
        },
        CallbackProjectDocumentProvider::CanCloseHandler (),
        CallbackProjectDocumentProvider::CloseHandler (),
        [this] () { _widget->markProjectDocumentClean (); },
        [this] (QByteArray* snapshot, QString* error) {
            return _widget->snapshotProjectDocumentState (*snapshot, error);
        },
        [this] (const QByteArray& snapshot, QString* error) {
            return _widget->restoreProjectDocumentState (snapshot, error);
        });

    QString projectProviderError;
    if (getRobWorkStudio () == NULL || !getRobWorkStudio ()->registerProjectDocumentProvider (
                                            _projectProvider, &projectProviderError)) {
        // 注册失败时不让该插件静默参与项目保存；独立 XML 生成功能仍可继续使用，错误
        // 通过全局日志保留给集成者诊断，而不会在插件初始化阶段阻塞整个主窗口。
        RW_WARN ("RobotModelBuilder project Provider registration failed: "
                 << projectProviderError.toStdString ());
    }
    // 用户与编辑控件交互后触发：先让 Widget 用规范快照判断真实脏状态并写入 Provider，
    // 再通知主窗口刷新标题栏。事件本身不直接置脏，避免页签切换等非持久化操作误报。
    connect (_widget, &RobotModelBuilderWidget::projectDocumentInteraction, this, [this] () {
        if (_projectProvider == NULL || getRobWorkStudio () == NULL)
            return;
        _projectProvider->setDirty (_widget->isProjectDocumentDirty ());
        getRobWorkStudio ()->notifyProjectDocumentChanged ();
    });
    setWidget (_widget);
}

// -----------------------------------------------------------------------------
//  open() / close()
//  说明: WorkCell 切换钩子。本插件并不直接缓存 WorkCell 数据,因此两个回调保持空实现。
// -----------------------------------------------------------------------------
void RobotModelBuilderPlugin::open (rw::models::WorkCell* workcell)
{
    if (_ignoreNextOpenFromSelfLoad) {
        _ignoreNextOpenFromSelfLoad = false;
        return;
    }
    syncFromWorkCell (workcell);
}

void RobotModelBuilderPlugin::close ()
{}

// =============================================================================
//  syncFromWorkCell
//  说明: 当宿主程序 (RobWorkStudio) 加载或切换 WorkCell 场景时，触发此同步回调。
//        负责将 C++ 内存中的 WorkCell 场景模型反向解析并灌入 UI 界面中。
//
//  工作流程:
//    1) 指针有效性防御检查：确认 UI 界面控件 (_widget) 与场景指针 (workcell) 均非空；
//    2) 输出路径决策：有项目上下文时固定使用 <项目根>/generated/robot-models；仅在独立
//       打开 WorkCell 且没有 rwproj 时，才通过场景对象的磁盘文件信息推算兼容性目录；
//    3) 核心反向转换：调用 WorkCellConverter::convert，结合内存 C++ 对象与磁盘源 XML
//       无损提取/缝合出 RobotModelSpec 数据模型；
//    4) 可建模模型判定：调用 WorkCellConverter::hasConvertibleRobotModel 检查转换出的 spec
//       是否包含有效的机器人模型 (包含非空名称与运动学关节)；
//    5) UI 数据回填：将解析好的 spec 与警告信息同步给 UI 控件 (_widget)，刷新界面表格。
//
//  参数:
//    - workcell : 当前在 RobWorkStudio 中被加载/选中的工作单元场景指针
// =============================================================================
void RobotModelBuilderPlugin::syncFromWorkCell (rw::models::WorkCell* workcell)
{
    // ---- 1. 空指针防御检查 ----
    // 若 UI 尚未实例化或当前没有激活的 WorkCell 场景，直接返回
    if (_widget == NULL || workcell == NULL)
        return;

    // 用于收集场景转换与文件解析过程中的非致命警告信息
    QStringList warnings;

    // ---- 2. 解析项目受管输出路径 ----
    // 有 rwproj 时，输出目录必须来自主窗口项目上下文；只有独立打开 WorkCell 时才沿用
    // 原有文件目录推导，避免项目内编辑把 XML 写回历史样例或用户主目录。
    if (getRobWorkStudio () != NULL)
        _widget->setProjectOutputDirectory (getRobWorkStudio ()->projectDirectory ());
    const std::string saveDirectory = _widget->projectOutputDirectory ().isEmpty ()
                                          ? WorkCellConverter::inferSaveDirectory (*workcell)
                                          : _widget->projectOutputDirectory ().toStdString ();

    // ---- 3. 执行核心场景转换 ----
    // 将内存中的 WorkCell 对象、默认状态 (State) 及目标保存目录传入转换器，
    // 提取串联关节、SE(3) 矩阵、几何体、碰撞矩阵及伴生 XML 配置文件，构建出纯数据结构 spec
    RobotModelSpec spec =
        WorkCellConverter::convert (*workcell, workcell->getDefaultState (), saveDirectory, warnings);

    // ---- 4. 检查模型有效性 ----
    // 验证转换出来的 spec 是否包含可编辑/可转换的机器人模型
    // （例如：场景中如果仅有一张桌子而没有串联机器人设备，则不触发插件界面同步）
    if (!WorkCellConverter::hasConvertibleRobotModel (spec))
        return;

    // ---- 5. 驱动 UI 界面同步 ----
    // 将解析提取出的模型规范 spec 以及警告列表灌入 Builder Widget 中，
    // 触发 UI 各个标签页表格 (Kinematics, Drawables, Limits, Poses 等) 的全量回填
    _widget->syncFromWorkCellSpec (spec, warnings);

    RobWorkStudio* studio = getRobWorkStudio ();
    if (studio == NULL || _projectProvider == NULL || _widget->projectOutputDirectory ().isEmpty ())
        return;

    // 方案 A：一个 WorkCell 项目只有一个当前工程机器人模型。固定资源 ID 避免 Provider
    // 在同一会话中绑定多个 .rmb.json；多机器人项目需要显式的选择工作流。
    ProjectResource resource;
    resource.id = QStringLiteral ("robot-model.main");
    resource.kind = QStringLiteral ("robwork.robot-model");
    resource.path = QStringLiteral ("generated/robot-models/%1.rmb.json").arg (
        RobotModelXmlWriter::sanitizeFileBaseName (QString::fromStdString (spec.robotName)));
    resource.ownership = QStringLiteral ("generated");
    resource.required = true;
    const QString workcellResourceId = studio->mainWorkCellResourceId ();
    if (!workcellResourceId.isEmpty ())
        resource.dependencies << workcellResourceId;

    bool created = false;
    QString resourceError;
    if (!studio->ensureGeneratedProjectResource (resource, &created, &resourceError)) {
        RW_WARN ("RobotModelBuilder could not register the generated project model: "
                 << resourceError.toStdString ());
        return;
    }
    if (!created)
        return;

    _projectProvider->adoptGeneratedResource (resource.id);
    _widget->beginGeneratedProjectDocument ();
    _projectProvider->setDirty (_widget->isProjectDocumentDirty ());
    studio->notifyProjectDocumentChanged ();
    // 生成模型后自动显示插件面板，方便用户立即审阅并保存该模型。
    if (!isVisible ())
        showPlugin ();
}

// 槽：主窗口"从机器人文件创建项目"通过元对象调用。无对话框直接导入 URDF/XML 源文件，
// 成功后在当前草稿项目内登记生成的模型资源并建立空基线。
QString RobotModelBuilderPlugin::preflightRobotProjectSource (const QString& sourcePath,
                                                              const QString& projectRoot)
{
    if (_widget == NULL)
        return QStringLiteral ("RobotModelBuilder is not initialized.");

    RobotModelSpec parsed;
    QStringList warnings;
    QString error;
    if (!_widget->preflightUrdfFile (
            sourcePath, projectRoot, parsed, warnings, &error))
        return error.isEmpty () ? QStringLiteral ("The robot source could not be parsed.") : error;
    return QString ();
}

QString RobotModelBuilderPlugin::commitRobotProjectSource (const QString& sourcePath,
                                                           const QString& projectRoot)
{
    if (_widget == NULL)
        return QStringLiteral ("RobotModelBuilder is not initialized.");

    RobWorkStudio* studio = getRobWorkStudio ();
    if (studio == NULL || _projectProvider == NULL)
        return QStringLiteral ("RobotModelBuilder is not attached to RobWorkStudio project services.");
    if (projectRoot.trimmed ().isEmpty () || !QDir::isAbsolutePath (projectRoot))
        return QStringLiteral ("The robot project root must be an absolute path.");

    const QString requestedRoot = QDir::cleanPath (QDir::fromNativeSeparators (projectRoot));
    const QString activeRoot =
        QDir::cleanPath (QDir::fromNativeSeparators (studio->projectDirectory ()));
    if (requestedRoot != activeRoot)
        return QStringLiteral ("The robot project is no longer active: %1").arg (requestedRoot);

    RobotModelSpec parsed;
    QStringList warnings;
    QString error;
    if (!_widget->preflightUrdfFile (
            sourcePath, requestedRoot, parsed, warnings, &error))
        return error.isEmpty () ? QStringLiteral ("The managed robot source could not be parsed.")
                                : error;

    ProjectResource resource;
    resource.id = QStringLiteral ("robot-model.main");
    resource.kind = QStringLiteral ("robwork.robot-model");
    resource.path = QStringLiteral ("generated/robot-models/%1.rmb.json").arg (
        RobotModelXmlWriter::sanitizeFileBaseName (QString::fromStdString (parsed.robotName)));
    resource.ownership = QStringLiteral ("generated");
    resource.required = true;
    resource.dependencies << QStringLiteral ("robot-source.main");

    bool created = false;
    if (!studio->ensureGeneratedProjectResource (resource, &created, &error)) {
        return QStringLiteral ("Could not register the managed robot model resource: %1").arg (
            error);
    }
    Q_UNUSED (created);

    _widget->setProjectOutputDirectory (requestedRoot);
    _widget->applyImportedProjectModel (parsed, warnings);
    _projectProvider->adoptGeneratedResource (resource.id);
    _widget->beginGeneratedProjectDocument ();
    _projectProvider->setDirty (_widget->isProjectDocumentDirty ());
    studio->notifyProjectDocumentChanged ();
    _widget->setProjectStatus ("Robot project draft imported. Review the model, then use File > Save Project "
                               "to generate the managed .rmb.json.");
    return QString ();
}

QString RobotModelBuilderPlugin::preflightNewRobotProject (const QString& projectRoot)
{
    if (_widget == NULL)
        return QStringLiteral ("RobotModelBuilder is not initialized.");
    if (getRobWorkStudio () == NULL || _projectProvider == NULL)
        return QStringLiteral (
            "RobotModelBuilder is not attached to RobWorkStudio project services.");
    if (projectRoot.trimmed ().isEmpty ())
        return QStringLiteral ("The New Project root is empty. Select an absolute project path.");
    if (!QDir::isAbsolutePath (projectRoot))
        return QStringLiteral ("The New Project root must be an absolute path: %1")
            .arg (projectRoot);
    return QString ();
}

QVariantMap RobotModelBuilderPlugin::newRobotProjectResource (const QString& projectRoot)
{
    QVariantMap result;
    const QString error = preflightNewRobotProject (projectRoot);
    result.insert (QStringLiteral ("success"), error.isEmpty ());
    result.insert (QStringLiteral ("error"), error);
    if (!error.isEmpty ())
        return result;

    const QString root = QDir::cleanPath (QDir::fromNativeSeparators (projectRoot));
    const RobotModelSpec defaults = RobotModelXmlWriter::makeDefaultSixAxisModel (
        QDir (root).filePath (QStringLiteral ("generated/robot-models")));
    result.insert (QStringLiteral ("id"), QStringLiteral ("robot-model.main"));
    result.insert (QStringLiteral ("kind"), QStringLiteral ("robwork.robot-model"));
    result.insert (
        QStringLiteral ("path"),
        QStringLiteral ("generated/robot-models/%1.rmb.json")
            .arg (RobotModelXmlWriter::sanitizeFileBaseName (
                QString::fromStdString (defaults.robotName))));
    result.insert (QStringLiteral ("ownership"), QStringLiteral ("generated"));
    result.insert (QStringLiteral ("required"), true);
    result.insert (QStringLiteral ("dependencies"), QStringList ());
    return result;
}

QVariantMap RobotModelBuilderPlugin::snapshotNewRobotProjectState ()
{
    QVariantMap result;
    QByteArray snapshot;
    QString error;
    const bool success = _widget != NULL &&
        _widget->snapshotProjectDocumentState (snapshot, &error);
    if (!success && error.isEmpty ())
        error = QStringLiteral ("RobotModelBuilder is not initialized.");
    result.insert (QStringLiteral ("success"), success);
    result.insert (QStringLiteral ("error"), error);
    result.insert (QStringLiteral ("snapshot"), snapshot);
    return result;
}

QString RobotModelBuilderPlugin::restoreNewRobotProjectState (const QByteArray& snapshot)
{
    if (_widget == NULL)
        return QStringLiteral ("RobotModelBuilder is not initialized.");
    QString error;
    if (!_widget->restoreProjectDocumentState (snapshot, &error))
        return error.isEmpty () ? QStringLiteral ("RobotModelBuilder state restore failed.") : error;
    return QString ();
}

QString RobotModelBuilderPlugin::bootstrapNewRobotProject (const QString& projectRoot)
{
    const QString preflightError = preflightNewRobotProject (projectRoot);
    if (!preflightError.isEmpty ())
        return preflightError;

    RobWorkStudio* studio = getRobWorkStudio ();
    const QString requestedRoot = QDir::cleanPath (QDir::fromNativeSeparators (projectRoot));
    const QString activeRoot =
        QDir::cleanPath (QDir::fromNativeSeparators (studio->projectDirectory ()));
    QString pathError;
    const bool requestedInsideActive = ProjectPathResolver::validateContainedWritePath (
        activeRoot, requestedRoot, &pathError);
    const bool activeInsideRequested = requestedInsideActive &&
        ProjectPathResolver::validateContainedWritePath (
            requestedRoot, activeRoot, &pathError);
    if (!activeInsideRequested) {
        return QStringLiteral ("The requested New Project is no longer active: %1")
            .arg (requestedRoot);
    }

    const QString outputDirectory =
        QDir (activeRoot).filePath (QStringLiteral ("generated/robot-models"));
    const RobotModelSpec defaults =
        RobotModelXmlWriter::makeDefaultSixAxisModel (outputDirectory);
    ProjectResource resource;
    resource.id = QStringLiteral ("robot-model.main");
    resource.kind = QStringLiteral ("robwork.robot-model");
    resource.path = QStringLiteral ("generated/robot-models/%1.rmb.json")
                        .arg (RobotModelXmlWriter::sanitizeFileBaseName (
                            QString::fromStdString (defaults.robotName)));
    resource.ownership = QStringLiteral ("generated");
    resource.required = true;

    bool created = false;
    QString error;
    if (!studio->ensureGeneratedProjectResource (resource, &created, &error)) {
        return QStringLiteral ("Could not register the default robot model resource: %1")
            .arg (error);
    }
    if (!created)
        return QString ();

    _widget->setProjectOutputDirectory (activeRoot);
    _widget->applyDefaultProjectModel ();
    _projectProvider->adoptGeneratedResource (resource.id);
    _widget->beginGeneratedProjectDocument ();
    _projectProvider->setDirty (_widget->isProjectDocumentDirty ());
    studio->notifyProjectDocumentChanged ();
    _widget->setProjectStatus (
        "Default robot model created. Review it, then use Save and Load to publish the WorkCell.");
    return QString ();
}

void RobotModelBuilderPlugin::importRobotProjectSource (const QString& sourcePath)
{
    RobWorkStudio* studio = getRobWorkStudio ();
    const QString projectRoot = studio == NULL ? QString () : studio->projectDirectory ();
    const QString error = commitRobotProjectSource (sourcePath, projectRoot);
    if (!error.isEmpty () && _widget != NULL)
        QMessageBox::warning (_widget, "RobotModelBuilder", error);
}

// -----------------------------------------------------------------------------
//  loadSceneFile()
//  说明: 由 Widget 发出的信号触发,要求 RobWorkStudio 加载指定路径的场景 XML。
//        这里做了一次空指针保护:getRobWorkStudio() 在插件被卸载等极端情况下
//        可能会返回 NULL,避免崩溃。
// -----------------------------------------------------------------------------
void RobotModelBuilderPlugin::loadSceneFile (const QString& filename)
{
    RobWorkStudio* studio = getRobWorkStudio ();
    if (studio == NULL)
        return;

    if (!studio->projectDirectory ().isEmpty ()) {
        const RobotModelSpec spec = _widget->currentModelSpec ();
        QStringList dependencies;
        if (QDir::cleanPath (filename) == QDir::cleanPath (RobotModelXmlWriter::sceneFilePath (spec))) {
            dependencies << RobotModelXmlWriter::serialDeviceFilePath (spec);
            if (spec.collisionSetup.enabled)
                dependencies << RobotModelXmlWriter::collisionSetupFilePath (spec);
            if (spec.proximitySetup.enabled)
                dependencies << RobotModelXmlWriter::proximitySetupFilePath (spec);
        }

        QString error;
        _ignoreNextOpenFromSelfLoad = true;
        const bool promoted = studio->promoteGeneratedWorkCell (filename, dependencies, &error);
        _ignoreNextOpenFromSelfLoad = false;
        if (!promoted) {
            _widget->setProjectStatus ("Generated scene could not be promoted. The current project scene was kept.");
            QMessageBox::critical (_widget, "RobotModelBuilder - Save and Load Failed", error);
            return;
        }
        _widget->setProjectStatus (
            "Generated scene loaded as the managed project WorkCell. Use File > Save Project to commit it.");
        Q_EMIT robotModelLoaded (filename);
        return;
    }

    _ignoreNextOpenFromSelfLoad = true;
    studio->setWorkcell (filename.toStdString ());
    const QString canonicalFilename = QFileInfo (filename).canonicalFilePath ();
    const rw::models::WorkCell::Ptr activeWorkCell = studio->getWorkcell ();
    if (!canonicalFilename.isEmpty () && activeWorkCell != NULL &&
        QFileInfo (QString::fromStdString (activeWorkCell->getFilename ())).canonicalFilePath () ==
            canonicalFilename)
        Q_EMIT robotModelLoaded (filename);
}
