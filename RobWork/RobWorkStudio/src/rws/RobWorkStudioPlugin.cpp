/********************************************************************************
 * Copyright 2009 The Robotics Group, The Maersk Mc-Kinney Moller Institute,
 * Faculty of Engineering, University of Southern Denmark
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ********************************************************************************/

#include "RobWorkStudioPlugin.hpp"

#include "RobWorkStudio.hpp"

#include <QMenu>
#include <QToolBar>

using rw::core::RobWork;
using namespace rw::core;
using namespace rw::models;

using namespace rws;

namespace {
// 项目上下文门控(Project Context Gate)使用的 Qt 动态属性键名。
// 该属性以字符串 "rws.requiresProjectContext" 存储在插件(QObject)的动态属性表中，
// 标记插件是否需要"已打开项目"作为前置条件。通过 QObject::property/setProperty 读写，
// 避免改动 RobWorkStudioPlugin 类的二进制布局，从而保持对已编译插件的 ABI 兼容。
const char* const RequiresProjectContextProperty = "rws.requiresProjectContext";
}

//----------------------------------------------------------------------
// Virtual methods

RobWorkStudioPlugin::~RobWorkStudioPlugin ()
{}

void RobWorkStudioPlugin::close ()
{}

void RobWorkStudioPlugin::initialize ()
{
    _log = _studio->logPtr ();
    Log::setLog (_log);
}

void RobWorkStudioPlugin::open (WorkCell* workcell)
{}

const rw::kinematics::State& RobWorkStudioPlugin::getState ()
{
    return getRobWorkStudio ()->getState ();
}

void RobWorkStudioPlugin::setState (const rw::kinematics::State& s)
{
    return getRobWorkStudio ()->setState (s);
}

RobWorkStudioPlugin::RobWorkStudioPlugin (const QString& name, const QIcon& icon) :
    QDockWidget (name), _showAction (icon, name, this), _name (name), _log (NULL)
{
    setObjectName (name);
    connect (&_showAction, SIGNAL (triggered ()), this, SLOT (showPlugin ()));
    _log = Log::getInstance ();
}

void RobWorkStudioPlugin::showPlugin ()
{
    // 项目上下文门控：需要项目上下文的插件在"项目未就绪"时拒绝被显示。
    // 门控条件取三者任一成立即拦截：
    //   1) visibilityAction 被禁用 —— 说明 WorkflowDockLayoutController 或
    //      RobWorkStudio 已判定当前不应激活该插件(未打开项目)；
    //   2) 未绑定 RobWorkStudio 实例 —— 插件尚未被正确初始化；
    //   3) 项目目录为空 —— 当前没有打开任何项目。
    // 满足门控时强制隐藏并直接返回，避免插件在无项目环境下通过菜单/工具栏
    // 快捷键强行弹出，产生空上下文访问。
    if (requiresProjectContext () &&
        (!visibilityAction ()->isEnabled () || getRobWorkStudio () == NULL ||
         getRobWorkStudio ()->projectDirectory ().isEmpty ())) {
        setVisible (false);
        return;
    }

    if (isVisible ()) {
        setVisible (false);
    }
    else {
        this->show ();
    }
}

// 查询插件是否声明"需要已打开项目"才能正常工作。
// 读取动态属性 RequiresProjectContextProperty 的值；未显式设置过即返回 false，
// 表示该插件无项目上下文要求，属于可随时使用的通用插件。
bool RobWorkStudioPlugin::requiresProjectContext () const
{
    return property (RequiresProjectContextProperty).toBool ();
}

// 声明插件是否需要项目上下文。
// 派生插件(如机器人模型构建器、运动学分析等)在构造函数中调用
// setRequiresProjectContext (true)，RobWorkStudio 及工作流 Dock 布局控制器据此
// 在未打开项目时禁用/隐藏插件，项目打开后自动恢复可用。
void RobWorkStudioPlugin::setRequiresProjectContext (bool required)
{
    setProperty (RequiresProjectContextProperty, required);
}

void RobWorkStudioPlugin::setupMenu (QMenu* menu)
{
    menu->addAction (&_showAction);
}

void RobWorkStudioPlugin::setupToolBar (QToolBar* toolbar)
{
    toolbar->addAction (&_showAction);
}

QString RobWorkStudioPlugin::name () const
{
    return _name;
}

void RobWorkStudioPlugin::setRobWorkStudio (RobWorkStudio* studio)
{
    _studio = studio;
}

RobWorkStudio* RobWorkStudioPlugin::getRobWorkStudio ()
{
    return _studio;
}

void RobWorkStudioPlugin::setRobWorkInstance (RobWork::Ptr robwork)
{
    RobWork::setInstance (robwork);
    _robwork = robwork;
}

RobWork::Ptr RobWorkStudioPlugin::getRobWorkInstance ()
{
    return _robwork;
}

rw::core::Log& RobWorkStudioPlugin::log ()
{
    return *_log;
}

void RobWorkStudioPlugin::setLog (rw::core::Log::Ptr log)
{
    _log = log;
    Log::setLog (_log);
}

boost::tuple< QWidget*, QAction*, int >
RobWorkStudioPlugin::getAction (QWidget* widget, const std::string& actionName)
{
    QList< QAction* > list = widget->actions ();
    for (int i = 0; i < list.size (); ++i) {
        // std::cout << list.at(i)->text().toStdString() << "==" <<  actionName << std::endl;
        if (list.at (i)->text ().toStdString () == actionName) {
            // std::cout << "Found File at position " << i << std::endl;
            return boost::make_tuple (widget, list.at (i), i);
        }
    }
    return boost::make_tuple (widget, (QAction*) NULL, -1);
}

boost::tuple< QWidget*, QMenu*, int > RobWorkStudioPlugin::getMenu (QWidget* widget,
                                                                    const std::string& menuName)
{
    boost::tuple< QWidget*, QAction*, int > res = getAction (widget, menuName);
    if ((res.get< 1 > () != NULL) && (res.get< 1 > ()->menu () != NULL)) {
        return boost::make_tuple (widget, res.get< 1 > ()->menu (), res.get< 2 > ());
    }
    return boost::make_tuple (widget, (QMenu*) NULL, -1);
}

boost::tuple< QMenu*, QAction*, int >
RobWorkStudioPlugin::getAction (QWidget* widget, const std::string& actionName,
                                const std::string& actionName2)
{
    QWidget* wid;
    QMenu* pmenu;
    QAction* action;
    int index;
    boost::tie (wid, pmenu, index) = getMenu (widget, actionName);
    if (pmenu == NULL)
        return boost::make_tuple ((QMenu*) NULL, (QAction*) NULL, -1);
    boost::tie (wid, action, index) = getAction (pmenu, actionName2);
    if (action == NULL)
        return boost::make_tuple ((QMenu*) NULL, (QAction*) NULL, -1);
    return boost::make_tuple (pmenu, action, index);
}
