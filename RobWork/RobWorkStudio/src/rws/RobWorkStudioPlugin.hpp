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

#ifndef RWS_ROBWORKSTUDIOPLUGIN_HPP
#define RWS_ROBWORKSTUDIOPLUGIN_HPP

#ifdef __WIN32
#include <windows.h>
#endif

#include <rw/core/RobWork.hpp>
#include <rw/kinematics/State.hpp>

#include <QAction>
#include <QDockWidget>
#include <QObject>
#include <QString>

// Make the RWS_USE_QT5 definition available to all plugins:
#include <RobWorkStudioConfig.hpp>
#include <rw/core/Log.hpp>

#include <boost/tuple/tuple.hpp>

namespace rw { namespace models {
    class WorkCell;
}}    // namespace rw::models

class QMenu;
class QToolBar;
class QIcon;

namespace rws {
// forward declaration
class RobWorkStudio;
//! @addtogroup rws
//! @{

/**
 * @brief abstract interface for RobWork Studio plugins
 */
class RobWorkStudioPlugin : public QDockWidget
{
    Q_OBJECT

  public:
    /**
     * @brief constructor of the plugin interface
     * @param name [in] the name of the plugin
     * @param icon [in] the icon of the plugin
     */
    RobWorkStudioPlugin (const QString& name, const QIcon& icon);

    //----------------------------------------------------------------------
    // Virtual methods.

    virtual ~RobWorkStudioPlugin ();

    /**
     * @brief is called when RobWorkStudio instance is valid. Can be used
     * to initialize values in the plugin that depend on RobWorkStudio
     *
     * @note DO NOT fire any events under initialization since the order of
     * which plugins are initialized is unknown. Therefore undefined behavior
     * might occour. Instead wait until open is called for the first time.
     */
    virtual void initialize ();

    /**
     * @brief called when a workcell is opened
     * @param workcell [in] that has been loaded
     */
    virtual void open (rw::models::WorkCell* workcell);

    /**
     * @brief called when a workcell is being closed.
     */
    virtual void close ();

    /**
     * @brief name that describe the plugin instance
     */
    virtual QString name () const;

    /**
     * @brief sets up the \b menu with this plugin
     * @param menu [in] the menu wherein the plugin can add its actions
     */
    virtual void setupMenu (QMenu* menu);

    /**
     * @brief setsup a \b toolbar with the actions of this plugin
     * @param toolbar [in] the toolbar wherein the plugin can add its actions
     */
    virtual void setupToolBar (QToolBar* toolbar);

    /**
     * @brief sets the RobWorkStudio instance of the plugin. Normally
     * only done on construction.
     */
    virtual void setRobWorkStudio (RobWorkStudio* studio);

    /**
     * @brief returns a handle to the RobWorkStudio instance
     */
    virtual RobWorkStudio* getRobWorkStudio ();

    /**
     * @brief Sets the RobWork instance to be used by the plugin
     * @param robwork [in] RobWork instance
     */
    virtual void setRobWorkInstance (rw::core::RobWork::Ptr robwork);

    /**
     * @brief Returns RobWork instance used by the plugin
     */
    virtual rw::core::RobWork::Ptr getRobWorkInstance ();

    /**
     * @brief returns the RobWorkStudio log instance
     */
    virtual rw::core::Log& log ();

    /**
     * @brief Sets the log to use
     * @param log [in] Pointer to the log to use.
     */
    virtual void setLog (rw::core::Log::Ptr log);

    //! get current state of RobWorkStudio
    const rw::kinematics::State& getState ();
    //! set current state of RobWorkStudio
    void setState (const rw::kinematics::State& state);

    //! @brief 返回该插件是否要求"已打开项目"作为前置上下文。
    //!
    //! 项目上下文门控(Project Context Gate)的查询接口：RobWorkStudio 主窗口与
    //! WorkflowDockLayoutController 在未打开项目时据此禁用/隐藏返回 true 的插件，
    //! 项目打开后再恢复其可用状态，从而避免插件在无项目环境下空上下文访问。
    bool requiresProjectContext () const;

    //! 显示/隐藏插件的 QAction，位于 RobWorkStudio 菜单与工具栏中。
    QAction* visibilityAction () { return &_showAction; }

  public Q_SLOTS:
    //! @brief 切换插件可见性(经项目上下文门控过滤后执行)。
    //! 需要项目上下文的插件在项目未就绪时调用本槽只会保持隐藏。
    void showPlugin ();

  protected:
    //! @brief 声明该插件面向用户的动作需要一个已打开的项目。
    //!
    //! 由需要项目上下文的派生插件在构造函数中调用；声明后插件在未打开项目时
    //! 被禁用并隐藏，打开项目后自动恢复。实现基于 Qt 动态属性，不改动类布局。
    void setRequiresProjectContext (bool required);

    /**
     * @brief Find action in \b widget with name \b actionName .
     * @param widget [in] the widget.
     * @param actionName [in] the name to look for.
     * @return a tuple with the widget, the found action and the index of the action. The returned
     * action is NULL and the index is -1 if not found.
     */
    boost::tuple< QWidget*, QAction*, int > getAction (QWidget* widget,
                                                       const std::string& actionName);

    /**
     * @brief Find menu in \b widget with name \b menuName .
     * @param widget [in] the widget.
     * @param menuName [in] the name to look for.
     * @return a tuple with the widget, the found menu and the index of the action. The returned
     * menu is NULL and the index is -1 if not found.
     */
    boost::tuple< QWidget*, QMenu*, int > getMenu (QWidget* widget, const std::string& menuName);

    /**
     * @brief Find action, \b actionName2, in menu, \b actionName, in a \b widget.
     * @param widget [in] the widget.
     * @param actionName [in] the name of the menu to look in.
     * @param actionName2 [in] the name of the action to look for.
     * @return a tuple with the widget, the found action and the index of the action within the
     * menu. The returned action is NULL and the index is -1 if not found.
     */
    boost::tuple< QMenu*, QAction*, int > getAction (QWidget* widget, const std::string& actionName,
                                                     const std::string& actionName2);

  protected:
    ///! @brief The show action
    QAction _showAction;

    ///! @brief Name of plugin
    QString _name;

    ///! @brief hook back to RobWorkStudio
    RobWorkStudio* _studio;

    ///! @brief The RobWork instance to be used
    rw::core::RobWork::Ptr _robwork;

    ///! @brief The log instance to be used
    rw::core::Log::Ptr _log;
};

}    // namespace rws

Q_DECLARE_INTERFACE (rws::RobWorkStudioPlugin, "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1")

#endif    //#ifndef ROBWORKSTUDIOPLUGIN_HPP
