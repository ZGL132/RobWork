#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLUGIN_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

namespace rws {

// 前置声明,避免在头文件里包含完整的 Widget 头文件。
class KinematicAnalysisWidget;
class CallbackProjectDocumentProvider;

// KinematicAnalysis 插件入口。
// 继承 RobWorkStudioPlugin,通过 Q_PLUGIN_METADATA / Q_INTERFACES 暴露给 Qt 的
// 插件系统(plugin.json 中描述依赖关系)。
class KinematicAnalysisPlugin : public RobWorkStudioPlugin
{
    Q_OBJECT
#ifndef RWS_USE_STATIC_LINK_PLUGINS
    Q_PLUGIN_METADATA(IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
    Q_INTERFACES(rws::RobWorkStudioPlugin)
#endif

public:
    KinematicAnalysisPlugin();
    ~KinematicAnalysisPlugin() override;

    // 三个 RobWorkStudioPlugin 钩子:
    //   - initialize:首次加载时构造 Widget;
    //   - open:WorkCell 切换后,把 workcell 推给 Widget;
    //   - close:WorkCell 卸载后,把 workcell 清空(NULL)。
    void open(rw::models::WorkCell* workcell) override;
    void close() override;
    void initialize() override;

private:
    KinematicAnalysisWidget* _widget;
    // Registry 只借用该指针，Provider 的完整生命周期由插件负责，避免注册表持有悬空对象。
    CallbackProjectDocumentProvider* _projectProvider;
    // 仅在资源已加载或已按首次编辑规则激活后为真，防止控件连续变化时重复登记清单项。
    bool _projectResourceActive;
};

}    // namespace rws

#endif
