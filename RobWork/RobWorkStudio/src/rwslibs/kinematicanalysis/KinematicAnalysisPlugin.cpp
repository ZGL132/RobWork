#include "KinematicAnalysisPlugin.hpp"

#include "KinematicAnalysisWidget.hpp"

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/RobWorkStudio.hpp>

#include <QDir>

namespace rws {

// 插件构造:RobWorkStudioPlugin 接收插件名(用于显示)和图标(此处使用空图标)。
KinematicAnalysisPlugin::KinematicAnalysisPlugin() :
    RobWorkStudioPlugin("KinematicAnalysis", QIcon(":/kinematicanalysis/kinematicanalysis_icon.png")),
    _widget(NULL), _projectProvider(NULL), _projectResourceActive(false)
{
}

// 析构:Widget 自身会被 QObject 父子机制在插件销毁前释放,无需手动 delete。
KinematicAnalysisPlugin::~KinematicAnalysisPlugin()
{
    // 项目文档在主窗口销毁前已关闭，Registry 不拥有该对象，因此由插件在此统一释放 Provider。
    delete _projectProvider;
}

// initialize:RobWorkStudio 首次加载插件时调用一次;
// 这里 new 出 Widget 并设置为插件的 UI 容器,同时注入 RobWorkStudio 句柄。
void KinematicAnalysisPlugin::initialize()
{
    _widget = new KinematicAnalysisWidget(this);
    setWidget(_widget);
    _widget->setRobWorkStudio(getRobWorkStudio());

    _projectProvider = new CallbackProjectDocumentProvider(
        QStringLiteral("rws.kinematic-analysis"),
        QStringLiteral("rws.kinematic-analysis"),
        [this](const QString& path, const ProjectDocumentContext&, QString* error) {
            const bool loaded = _widget->loadProjectDocument(path, error);
            _projectResourceActive = loaded;
            return loaded;
        },
        [this](const QString& targetPath, const ProjectDocumentContext&, QString* error) {
            return _widget->saveProjectDocument(targetPath, error);
        },
        [this](QString* reason) { return _widget->canCloseProjectDocument(reason); },
        [this]() {
            // Registry 关闭旧项目资源时清除会话基线，避免旧项目路径或快照污染新项目。
            _projectResourceActive = false;
            _widget->clearProjectDocumentContext();
        },
        [this]() { _widget->markProjectDocumentClean(); });

    RobWorkStudio* studio = getRobWorkStudio();
    if (studio != nullptr) {
        QString providerError;
        // 初始化期预注册，使打开项目时 Registry 能按资源 kind 自动加载分析配置。
        if (!studio->registerProjectDocumentProvider(_projectProvider, &providerError))
            RW_WARN("KinematicAnalysis project Provider registration failed: "
                    << providerError.toStdString());
    }

    connect(_widget, &KinematicAnalysisWidget::projectDocumentChanged, this, [this]() {
        RobWorkStudio* currentStudio = getRobWorkStudio();
        if (_projectProvider == nullptr || currentStudio == nullptr ||
            currentStudio->projectDirectory().isEmpty())
            return;

        if (!_projectResourceActive) {
            const QString workCellResourceId = currentStudio->mainWorkCellResourceId();
            if (workCellResourceId.isEmpty()) {
                // 设备与 TCP 名称依赖 WorkCell；没有主场景时不创建无法独立解释的分析资源。
                RW_WARN("KinematicAnalysis configuration requires a main WorkCell project resource.");
                return;
            }

            ProjectResource resource;
            resource.id = QStringLiteral("kinematic-analysis.main");
            resource.kind = QStringLiteral("rws.kinematic-analysis");
            resource.path = QStringLiteral("analysis/kinematic-analysis.json");
            resource.ownership = QStringLiteral("project");
            resource.required = false;
            resource.dependencies = {workCellResourceId};

            bool created = false;
            QString creationError;
            if (!currentStudio->ensureGeneratedProjectResource(resource, &created, &creationError)) {
                RW_WARN("KinematicAnalysis project resource creation failed: "
                        << creationError.toStdString());
                return;
            }
            if (created) {
                // 先绑定 Provider 资源 ID，再设置 Widget 的空保存基线，使首次保存同时提交
                // 清单和 JSON，失败时不会遗留项目外文件或孤立空文件。
                _projectProvider->adoptGeneratedResource(resource.id);
                _widget->beginProjectDocument(
                    QDir(currentStudio->projectDirectory()).filePath(resource.path));
                _projectResourceActive = true;
            }
        }

        // Widget 通过规范化 JSON 快照判断真实变化，用户恢复原值时会自动清脏；此处只刷新
        // 标题，真正写盘仍由“保存项目”执行的事务统一负责。
        _projectProvider->setDirty(_widget->isProjectDocumentDirty());
        currentStudio->notifyProjectDocumentChanged();
    });
}

// open:WorkCell 切换后被调用,把新 WorkCell 推给 Widget,触发设备/帧下拉刷新。
void KinematicAnalysisPlugin::open(rw::models::WorkCell* workcell)
{
    if (_widget != NULL)
        _widget->setWorkCell(workcell);
}

// close:WorkCell 被卸载/关闭,清空 Widget 内部缓存,UI 自动回到"未加载"状态。
void KinematicAnalysisPlugin::close()
{
    if (_widget != NULL)
        _widget->setWorkCell(NULL);
}

}    // namespace rws
