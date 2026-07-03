// =============================================================================
//  文件: RobotModelBuilderPlugin.cpp
//  说明: RobotModelBuilder 插件入口实现。本文件非常薄,只负责把 Widget 装到
//        RobWorkStudio 中,并把 Widget 的"加载场景"信号转发给宿主,真正的建模
//        UI 和 XML 生成逻辑都在 RobotModelBuilderWidget / RobotModelXmlWriter 中。
// =============================================================================
#include "RobotModelBuilderPlugin.hpp"

#include "RobotModelBuilderWidget.hpp"

#include <rws/RobWorkStudio.hpp>

using namespace rws;

// -----------------------------------------------------------------------------
//  构造函数
//  说明: 向基类传入插件名(显示在 RobWorkStudio 菜单/插件列表中)和图标(留空)。
//        _widget 暂时为空指针,待 initialize() 中再实例化。
// -----------------------------------------------------------------------------
RobotModelBuilderPlugin::RobotModelBuilderPlugin () :
    RobWorkStudioPlugin ("RobotModelBuilder", QIcon ()), _widget (NULL)
{}

// -----------------------------------------------------------------------------
//  析构函数
//  说明: Qt 的对象父子机制会在本对象销毁时自动 delete _widget,无需手动释放。
// -----------------------------------------------------------------------------
RobotModelBuilderPlugin::~RobotModelBuilderPlugin ()
{}

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
    // 当 Widget 完成 "Save and Load" 操作时,会发出场景文件名,我们在这里负责真正去加载它
    connect (_widget, SIGNAL (loadSceneRequested (const QString&)), this,
             SLOT (loadSceneFile (const QString&)));
    setWidget (_widget);
}

// -----------------------------------------------------------------------------
//  open() / close()
//  说明: WorkCell 切换钩子。本插件并不直接缓存 WorkCell 数据,因此两个回调保持空实现。
// -----------------------------------------------------------------------------
void RobotModelBuilderPlugin::open (rw::models::WorkCell* workcell)
{}

void RobotModelBuilderPlugin::close ()
{}

// -----------------------------------------------------------------------------
//  loadSceneFile()
//  说明: 由 Widget 发出的信号触发,要求 RobWorkStudio 加载指定路径的场景 XML。
//        这里做了一次空指针保护:getRobWorkStudio() 在插件被卸载等极端情况下
//        可能会返回 NULL,避免崩溃。
// -----------------------------------------------------------------------------
void RobotModelBuilderPlugin::loadSceneFile (const QString& filename)
{
    if (getRobWorkStudio () != NULL)
        getRobWorkStudio ()->setWorkcell (filename.toStdString ());
}