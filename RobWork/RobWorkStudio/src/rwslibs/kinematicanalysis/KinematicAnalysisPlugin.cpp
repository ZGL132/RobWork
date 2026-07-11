#include "KinematicAnalysisPlugin.hpp"

#include "KinematicAnalysisWidget.hpp"

#include <rws/RobWorkStudio.hpp>

namespace rws {

// 插件构造:RobWorkStudioPlugin 接收插件名(用于显示)和图标(此处使用空图标)。
KinematicAnalysisPlugin::KinematicAnalysisPlugin() :
    RobWorkStudioPlugin("KinematicAnalysis", QIcon()),
    _widget(NULL)
{
}

// 析构:Widget 自身会被 QObject 父子机制在插件销毁前释放,无需手动 delete。
KinematicAnalysisPlugin::~KinematicAnalysisPlugin()
{
}

// initialize:RobWorkStudio 首次加载插件时调用一次;
// 这里 new 出 Widget 并设置为插件的 UI 容器,同时注入 RobWorkStudio 句柄。
void KinematicAnalysisPlugin::initialize()
{
    _widget = new KinematicAnalysisWidget(this);
    setWidget(_widget);
    _widget->setRobWorkStudio(getRobWorkStudio());
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