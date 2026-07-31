#include "StructureOptimizerPlugin.hpp"
#include "StructureOptimizerWidget.hpp"

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/RobWorkStudio.hpp>
#include <rw/loaders/rwxml/XMLRWLoader.hpp>

namespace rws {

StructureOptimizerPlugin::StructureOptimizerPlugin() :
    RobWorkStudioPlugin("StructureOptimizer", QIcon(":/structureoptimizer/structureoptimizer_icon.png"))
{
    _widget = new StructureOptimizerWidget();
    setWidget(_widget);
}

StructureOptimizerPlugin::~StructureOptimizerPlugin()
{
    // 主窗口关闭项目资源后才销毁插件；Registry 不拥有该对象，故在此统一释放。
    delete _projectProvider;
}

void StructureOptimizerPlugin::initialize()
{
    _widget->setPreviewHost(this);

    _projectProvider = new CallbackProjectDocumentProvider(
        QStringLiteral("rws.structure-optimizer"),
        QStringLiteral("rws.structure-optimization"),
        [this](const QString& path, const ProjectDocumentContext&, QString* error) {
            return _widget->loadProjectDocument(path, error);
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
    }
    connect(_widget, &StructureOptimizerWidget::projectDocumentChanged, this, [this]() {
        if (_projectProvider == nullptr || getRobWorkStudio() == nullptr)
            return;
        // Widget 自身比较可移植 JSON 快照；因此用户撤销到原始配置时也可自动清除脏状态。
        _projectProvider->setDirty(_widget->isProjectDocumentDirty());
        getRobWorkStudio()->notifyProjectDocumentChanged();
    });
}

void StructureOptimizerPlugin::open(rw::models::WorkCell* workcell)
{
    (void)workcell;
}

void StructureOptimizerPlugin::close()
{
    _widget->setPreviewHost(nullptr);
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
