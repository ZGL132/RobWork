#include "StructureOptimizerPlugin.hpp"
#include "StructureOptimizerWidget.hpp"

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/RobWorkStudio.hpp>
#include <rw/loaders/rwxml/XMLRWLoader.hpp>

#include <QDir>

namespace rws {

StructureOptimizerPlugin::StructureOptimizerPlugin() :
    RobWorkStudioPlugin("StructureOptimizer", QIcon(":/structureoptimizer/structureoptimizer_icon.png"))
{
    _widget = new StructureOptimizerWidget();
    setWidget(_widget);
}

StructureOptimizerPlugin::~StructureOptimizerPlugin()
{
    if (getRobWorkStudio() != nullptr)
        getRobWorkStudio()->stateChangedEvent().remove(this);
    // 主窗口关闭项目资源后才销毁插件；Registry 不拥有该对象，故在此统一释放。
    delete _projectProvider;
}

void StructureOptimizerPlugin::initialize()
{
    _widget->setPreviewHost(this);
    _widget->setRobWorkStudio(getRobWorkStudio());

    _projectProvider = new CallbackProjectDocumentProvider(
        QStringLiteral("rws.structure-optimizer"),
        QStringLiteral("rws.structure-optimization"),
        [this](const QString& path, const ProjectDocumentContext& context, QString* error) {
            return _widget->loadProjectDocument(path, error, context.projectDirectory);
        },
        [this](const QString& targetPath, const ProjectDocumentContext&, QString* error) {
            return _widget->saveProjectDocument(targetPath, error);
        },
        [this](QString* reason) { return _widget->canCloseProjectDocument(reason); },
        CallbackProjectDocumentProvider::CloseHandler(),
        [this]() { _widget->markProjectDocumentClean(); });

    if (getRobWorkStudio() != nullptr) {
        QString providerError;
        // 注册位置放在 initialize：此时宿主窗口可用，且随后打开 rwproj 时 Registry
        // 能按清单 kind 找到优化资源，避免插件私有的文件对话框绕开统一保存事务。
        if (!getRobWorkStudio()->registerProjectDocumentProvider(_projectProvider, &providerError))
            RW_WARN("StructureOptimizer project Provider registration failed: "
                    << providerError.toStdString());
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
    connect(_widget, &StructureOptimizerWidget::projectDocumentChanged, this, [this]() {
        if (_projectProvider == nullptr || getRobWorkStudio() == nullptr)
            return;
        RobWorkStudio* studio = getRobWorkStudio();
        if (!studio->projectDirectory().isEmpty()) {
            ProjectResource resource;
            resource.id = QStringLiteral("structure-optimization.main");
            resource.kind = QStringLiteral("rws.structure-optimization");
            resource.path =
                QStringLiteral("optimizations/main.structure-optimization.json");
            resource.ownership = QStringLiteral("generated");
            resource.required = true;
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
        _projectProvider->setDirty(_widget->isProjectDocumentDirty());
        studio->notifyProjectDocumentChanged();
        // Widget 自身比较可移植 JSON 快照；因此用户撤销到原始配置时也可自动清除脏状态。
        _projectProvider->setDirty(_widget->isProjectDocumentDirty());
    });
}

void StructureOptimizerPlugin::open(rw::models::WorkCell* workcell)
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio != nullptr)
        _widget->setScenarioContext(workcell, studio->getState());
}

void StructureOptimizerPlugin::close()
{
    _widget->setPreviewHost(nullptr);
    _widget->clearScenarioContext();
}

void StructureOptimizerPlugin::loadSceneFile(const QString& filename)
{
    if (getRobWorkStudio() != NULL)
        getRobWorkStudio()->setWorkcell(filename.toStdString());
}

QString StructureOptimizerPlugin::currentWorkCellPath()
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || studio->getWorkCell().isNull())
        return QString();
    try {
        const std::string path = static_cast<std::string>(
            studio->getWorkCell()->getPropertyMap().get<std::string>(
                rw::loaders::XMLRWLoader::getWorkCellFileNameId()));
        return QString::fromStdString(path);
    } catch (...) {
        return QString();
    }
}

bool StructureOptimizerPlugin::openWorkCell(const QString& path, QString* error)
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || path.isEmpty()) {
        if (error != nullptr)
            *error = "RobWorkStudio is unavailable.";
        return false;
    }
    try {
        studio->setWorkcell(path.toStdString());
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr)
            *error = QString::fromStdString(exception.what());
        return false;
    }
}

} // namespace rws
