#include "StructureOptimizerPlugin.hpp"
#include "StructureOptimizerWidget.hpp"

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/RobWorkStudio.hpp>
#include <rw/loaders/rwxml/XMLRWLoader.hpp>

#include <QDir>

namespace rws {

/**
 * @brief 结构优化插件构造函数。
 * 
 * 初始化 RobWorkStudioPlugin 插件元数据（插件名称与图标），
 * 声明插件运行依赖上下文，并实例化主界面控件 StructureOptimizerWidget。
 */
StructureOptimizerPlugin::StructureOptimizerPlugin() :
    RobWorkStudioPlugin("StructureOptimizer", QIcon(":/structureoptimizer/structureoptimizer_icon.png"))
{
    // 声明本插件需要"已打开项目"作为前置上下文：未打开项目时由主窗口禁用并隐藏
    // 本插件，打开项目后自动恢复，避免在无项目环境下对结构方案做优化计算。
    setRequiresProjectContext(true);
    _widget = new StructureOptimizerWidget();
    setWidget(_widget); // 将 Widget 设置为插件的中央主显示控件
}

/**
 * @brief 插件析构函数。
 * 
 * 释放资源提供者指针 _projectProvider。
 * 注意：事件回调在此前已被 RobWorkStudio 解绑。
 */
StructureOptimizerPlugin::~StructureOptimizerPlugin()
{
    // Event callbacks are detached by RobWorkStudio before QObject children are destroyed.
    // 主窗口关闭项目资源后才销毁插件；Registry 不拥有该对象，故在此统一释放。
    delete _projectProvider;
}

/**
 * @brief 插件初始化入口函数（在 RobWorkStudio 宿主窗口构建完成后被调用）。
 * 
 * 核心逻辑包括：
 *  1. 为 UI 控件绑定 3D 预览宿主（this）与 RobWorkStudio 实例；
 *  2. 创建向宿主注册的 CallbackProjectDocumentProvider，对接工程的加载、保存和状态重置事务；
 *  3. 监听宿主的 State 变化事件（stateChangedEvent），同步场景 WorkCell 与 State；
 *  4. 监听 UI 的 projectDocumentChanged 信号，实现自动生成项目资源及依赖项追踪。
 */
void StructureOptimizerPlugin::initialize()
{
    // 将插件自身（实现 IWorkCellPreviewHost 接口）设为 Widget 的预览宿主
    _widget->setPreviewHost(this);
    _widget->setRobWorkStudio(getRobWorkStudio());

    // 实例化向宿主注册的工程资源文档提供者（Project Document Provider）
    _projectProvider = new CallbackProjectDocumentProvider(
        QStringLiteral("rws.structure-optimizer"),
        QStringLiteral("rws.structure-optimization"),
        // 1. 打开项目加载回调：调用 Widget 的无对话框文档加载接口
        [this](const QString& path, const ProjectDocumentContext& context, QString* error) {
            return _widget->loadProjectDocument(path, error, context.projectDirectory);
        },
        // 2. 保存项目回调：调用 Widget 的无对话框文档保存接口
        [this](const QString& targetPath, const ProjectDocumentContext&, QString* error) {
            return _widget->saveProjectDocument(targetPath, error);
        },
        // 3. 检查是否可关闭项目回调：判断当前是否有优化任务在运行
        [this](QString* reason) { return _widget->canCloseProjectDocument(reason); },
        // 4. 项目资源关闭回调：当优化项目被关闭且无需保存时，清空 Widget 的优化问题、
        //    项目路径与快照基线，使新工程不继承上一项目的优化会话。
        [this]() { _widget->clearProjectDocumentContext(); },
        // 5. 标记干净回调：成功保存后清理 Widget 的脏状态
        [this]() { _widget->markProjectDocumentClean(); });

    if (getRobWorkStudio() != nullptr) {
        QString providerError;
        // 注册位置放在 initialize：此时宿主窗口可用，且随后打开 rwproj 时 Registry
        // 能按清单 kind 找到优化资源，避免插件私有的文件对话框绕开统一保存事务。
        if (!getRobWorkStudio()->registerProjectDocumentProvider(_projectProvider, &providerError))
            RW_WARN("StructureOptimizer project Provider registration failed: "
                    << providerError.toStdString());
        
        // 添加场景状态改变事件监听：当宿主的 WorkCell 或关节角度发生变化时，实时同步给 Widget
        getRobWorkStudio()->stateChangedEvent().add(
            [this](const rw::kinematics::State& state) {
                RobWorkStudio* studio = getRobWorkStudio();
                _widget->setScenarioContext(
                    studio != nullptr && !studio->getWorkCell().isNull()
                        ? studio->getWorkCell().get()
                        : nullptr,
                    state);
            },
            this);
    }

    // 连接 Widget 的问题变更信号：当变量、任务点、约束或运行参数修改时触发[cite: 24, 25]
    connect(_widget, &StructureOptimizerWidget::projectDocumentChanged, this, [this]() {
        if (_projectProvider == nullptr || getRobWorkStudio() == nullptr)
            return;
        RobWorkStudio* studio = getRobWorkStudio();
        
        // 若当前处于有效工程目录中，确保生成对应的优化资源规范
        if (!studio->projectDirectory().isEmpty()) {
            ProjectResource resource;
            resource.id = QStringLiteral("structure-optimization.main");
            resource.kind = QStringLiteral("rws.structure-optimization");
            resource.path =
                QStringLiteral("optimizations/main.structure-optimization.json");
            resource.ownership = QStringLiteral("generated");
            resource.required = true;
            
            // 自动解析上游依赖资源（场景、机器人模型、工程需求），建立依赖链
            const QStringList upstreamResourceIds = {
                QStringLiteral("scene.main"),
                QStringLiteral("robot-model.main"),
                QStringLiteral("engineering-requirements.main")};
            for (const QString& resourceId : upstreamResourceIds) {
                QString resolvedPath;
                if (studio->resolveProjectResource(resourceId, resolvedPath, nullptr) &&
                    !resource.dependencies.contains(resourceId)) {
                    resource.dependencies.push_back(resourceId);
                }
            }

            bool created = false;
            QString resourceError;
            // 通知宿主建立或更新生成的工程资源
            if (!studio->ensureGeneratedProjectResource(resource, &created, &resourceError)) {
                RW_WARN("StructureOptimizer could not register its project resource: "
                        << resourceError.toStdString());
                return;
            }
            if (created) {
                _projectProvider->adoptGeneratedResource(resource.id);
                _widget->beginGeneratedProjectDocument(
                    QDir(studio->projectDirectory()).filePath(resource.path));
            }
        }
        
        // 将 UI 当前对比规范 JSON 快照算出的脏状态（Dirty Status）写回 Provider[cite: 24, 25]
        _projectProvider->setDirty(_widget->isProjectDocumentDirty());
        studio->notifyProjectDocumentChanged(); // 通知宿主主窗口更新标题栏的未保存标志（*号）
        // Widget 自身比较可移植 JSON 快照；因此用户撤销到原始配置时也可自动清除脏状态[cite: 24, 25]。
        _projectProvider->setDirty(_widget->isProjectDocumentDirty());
    });

    connect (_widget, &StructureOptimizerWidget::optimizationCompletedForWorkflow,
             this, [this] (bool completed) {
                 RobWorkStudio* studio = getRobWorkStudio ();
                 if (studio == nullptr || studio->projectDirectory ().isEmpty ())
                     return;
                 QString error;
                 if (!studio->publishWorkflowOptimization (completed, &error)) {
                     RW_WARN ("Structure optimization workflow publication failed: "
                              << error.toStdString ());
                 }
             });
}

/**
 * @brief 宿主打开新场景 WorkCell 时的回调函数。
 * @param workcell 加载的 WorkCell 指针
 */
void StructureOptimizerPlugin::open(rw::models::WorkCell* workcell)
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio != nullptr)
        _widget->setScenarioContext(workcell, studio->getState()); // 同步新场景的 WorkCell 和 State
}

/**
 * @brief 宿主关闭当前场景 WorkCell 时的回调函数。
 */
void StructureOptimizerPlugin::close()
{
    _widget->setPreviewHost(nullptr);
    _widget->clearScenarioContext(); // 清除 UI 的场景上下文
}

/**
 * @brief 加载场景文件接口。
 * @param filename 场景 XML 文件路径
 */
void StructureOptimizerPlugin::loadSceneFile(const QString& filename)
{
    if (getRobWorkStudio() != NULL)
        getRobWorkStudio()->setWorkcell(filename.toStdString());
}

/**
 * @brief 获取宿主当前加载的场景 WorkCell 文件绝对路径。
 * 
 * 实现了 IWorkCellPreviewHost 接口，用于在 3D 预览前备份原始场景路径[cite: 2, 24]。
 * 
 * @return QString 当前场景路径
 */
QString StructureOptimizerPlugin::currentWorkCellPath()
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || studio->getWorkCell().isNull())
        return QString();
    try {
        // 从 WorkCell 的 PropertyMap 提取工作单元 XML 源文件路径
        const std::string path = static_cast<std::string>(
            studio->getWorkCell()->getPropertyMap().get<std::string>(
                rw::loaders::XMLRWLoader::getWorkCellFileNameId()));
        return QString::fromStdString(path);
    } catch (...) {
        return QString();
    }
}

/**
 * @brief 指示宿主 RobWorkStudio 打开并加载指定路径的场景 WorkCell。
 * 
 * 实现了 IWorkCellPreviewHost 接口，供 CandidatePreviewController 进行 3D 候选模型预览或恢复[cite: 2, 24]。
 * 
 * @param path 待加载场景 WorkCell 的 XML 绝对路径
 * @param error [out] 可选的输出错误描述字符串
 * @return true 加载成功；false 加载失败
 */
bool StructureOptimizerPlugin::openWorkCell(const QString& path, QString* error)
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || path.isEmpty()) {
        if (error != nullptr)
            *error = "RobWorkStudio is unavailable.";
        return false;
    }
    try {
        studio->setWorkcell(path.toStdString()); // 触发 RobWorkStudio 的 WorkCell 重新加载与 3D 视口重绘
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr)
            *error = QString::fromStdString(exception.what());
        return false;
    }
}

} // namespace rws
