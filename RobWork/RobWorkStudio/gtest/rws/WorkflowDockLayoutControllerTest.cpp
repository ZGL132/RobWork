/********************************************************************************
 * Copyright 2026 The Robotics Group, The Maersk Mc-Kinney Moller Institute,
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

#include <rw/core/PropertyMap.hpp>
#include <rws/RobWorkStudio.hpp>
#include <rws/RobWorkStudioPlugin.hpp>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QWidget>
#include <gtest/gtest.h>

#include <array>

namespace {

class NamedWorkflowDock : public rws::RobWorkStudioPlugin
{
  public:
    // 构造工作流测试 Dock：
    //   forceOversizedContent —— 为 true 时给内容控件设置大最小宽度，用于测试
    //                            布局控制器对插件最小宽度的忽略处理；
    //   requiresProject      —— 为 true 时声明该 Dock 需要项目上下文，用于测试
    //                           未打开项目时的门控禁用/隐藏行为。
    NamedWorkflowDock (const QString& name, bool forceOversizedContent, bool requiresProject) :
        RobWorkStudioPlugin (name, QIcon ())
    {
        if (requiresProject)
            setRequiresProjectContext (true);
        if (forceOversizedContent) {
            QWidget* content = new QWidget (this);
            content->setMinimumWidth (600);
            setWidget (content);
        }
    }
};

class LookalikeTabDock : public rws::RobWorkStudioPlugin
{
  public:
    LookalikeTabDock () : RobWorkStudioPlugin (QStringLiteral ("Unrelated"), QIcon ())
    {
        _tabs = new QTabBar (this);
        _tabs->addTab (QStringLiteral ("EngineeringRequirements"));
        _tabs->addTab (QStringLiteral ("RobotModelBuilder"));
        _tabs->addTab (QStringLiteral ("KinematicAnalysis"));
        _tabs->addTab (QStringLiteral ("StructureOptimizer"));
        _tabs->addTab (QStringLiteral ("UnrelatedTab"));
        _tabs->setCurrentIndex (4);
    }

    QTabBar* tabs () const { return _tabs; }

  private:
    QTabBar* _tabs;
};

class LayoutRequestIgnoringStudio : public rws::RobWorkStudio
{
  public:
    using RobWorkStudio::RobWorkStudio;

    bool event (QEvent* event) override
    {
        if (event->type () == QEvent::LayoutRequest)
            return true;
        return RobWorkStudio::event (event);
    }
};

// 向工作室注册一组命名的工作流 Dock(工程需求/机器人模型构建器/运动学分析/
// 结构优化/Jog)。requiresProject 为 true 时所有工作流 Dock 均声明需要项目上下文，
// 用于测试项目上下文门控对整个 Dock 组生效。
void addNamedWorkflowDocks (rws::RobWorkStudio& studio,
                            bool forceOversizedContent = false,
                            bool requiresProject = false)
{
    for (const QString& name : {QStringLiteral ("EngineeringRequirements"),
                                QStringLiteral ("RobotModelBuilder"),
                                QStringLiteral ("KinematicAnalysis"),
                                QStringLiteral ("StructureOptimizer"),
                                QStringLiteral ("Jog")}) {
        studio.addPlugin (new NamedWorkflowDock (name, forceOversizedContent, requiresProject), false,
                          Qt::LeftDockWidgetArea);
    }
}

QStringList tabNames (const QTabBar& tabBar)
{
    QStringList names;
    for (int index = 0; index < tabBar.count (); ++index)
        names.append (tabBar.tabText (index));
    return names;
}

QTabBar* workflowTabBar (rws::RobWorkStudio& studio)
{
    for (QTabBar* tabBar : studio.findChildren< QTabBar* > ()) {
        if (tabNames (*tabBar) ==
            QStringList {QStringLiteral ("EngineeringRequirements"),
                         QStringLiteral ("RobotModelBuilder"),
                         QStringLiteral ("KinematicAnalysis"),
                         QStringLiteral ("StructureOptimizer")})
            return tabBar;
    }
    return nullptr;
}

std::array< QAction*, 5 > visibilityActions (
    const std::vector< rws::RobWorkStudioPlugin* >& docks)
{
    return {docks[0]->visibilityAction (), docks[1]->visibilityAction (),
            docks[2]->visibilityAction (), docks[3]->visibilityAction (),
            docks[4]->visibilityAction ()};
}

void processUiEvents ()
{
    QCoreApplication::processEvents ();
    QCoreApplication::processEvents ();
}

}    // namespace

TEST (WorkflowDockLayout, InitialLayoutIsLocked)
{
    int argc = 1;
    char name[] = "WorkflowDockLayoutControllerTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    rw::core::PropertyMap settings;
    rws::RobWorkStudio studio (settings);
    addNamedWorkflowDocks (studio);

    studio.configureWorkflowDockLayout ();
    studio.show ();
    processUiEvents ();

    const std::vector< rws::RobWorkStudioPlugin* >& docks = studio.getPlugins ();
    ASSERT_EQ (5U, docks.size ());
    for (std::size_t index = 0; index < 4; ++index) {
        EXPECT_EQ (Qt::LeftDockWidgetArea, studio.dockWidgetArea (docks[index]));
        EXPECT_FALSE (docks[index]->isFloating ());
        docks[index]->raise ();
        processUiEvents ();
        EXPECT_EQ (docks[index]->width (), docks[4]->width ());
    }
    docks[1]->raise ();
    processUiEvents ();
    EXPECT_EQ (Qt::RightDockWidgetArea, studio.dockWidgetArea (docks[4]));
    EXPECT_FALSE (docks[4]->isFloating ());
    EXPECT_EQ (QTabWidget::North, studio.tabPosition (Qt::LeftDockWidgetArea));

    QTabBar* tabs = workflowTabBar (studio);
    ASSERT_NE (nullptr, tabs);
    EXPECT_EQ ((QStringList {QStringLiteral ("EngineeringRequirements"),
                             QStringLiteral ("RobotModelBuilder"),
                             QStringLiteral ("KinematicAnalysis"),
                             QStringLiteral ("StructureOptimizer")} ),
               tabNames (*tabs));
    EXPECT_FALSE (tabs->isTabEnabled (0));
    EXPECT_TRUE (tabs->isTabEnabled (1));
    EXPECT_FALSE (tabs->isTabEnabled (2));
    EXPECT_FALSE (tabs->isTabEnabled (3));

    const QString activeDockName = studio.activeWorkflowDockName ();
    const std::array< QAction*, 5 > actions = visibilityActions (docks);
    EXPECT_EQ (QStringLiteral ("RobotModelBuilder"), activeDockName);
    EXPECT_TRUE (docks[0]->isVisible ());
    EXPECT_FALSE (docks[0]->isEnabled ());
    EXPECT_TRUE (docks[1]->isEnabled ());
    EXPECT_TRUE (docks[2]->isVisible ());
    EXPECT_FALSE (docks[2]->isEnabled ());
    EXPECT_TRUE (docks[3]->isVisible ());
    EXPECT_FALSE (docks[3]->isEnabled ());
    EXPECT_TRUE (docks[4]->isEnabled ());
    EXPECT_FALSE (actions[0]->isEnabled ());
    EXPECT_TRUE (actions[1]->isEnabled ());
    EXPECT_FALSE (actions[2]->isEnabled ());
    EXPECT_FALSE (actions[3]->isEnabled ());
    EXPECT_TRUE (actions[4]->isEnabled ());
}

TEST (WorkflowDockLayout, InitialWidthIgnoresPluginMinimumWidthAndLayoutRequest)
{
    int argc = 1;
    char name[] = "WorkflowDockLayoutControllerTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    rw::core::PropertyMap settings;
    LayoutRequestIgnoringStudio studio (settings);
    studio.getSettings ().set< int > ("WorkflowDockLayoutVersion", 0);
    studio.resize (1440, 900);
    addNamedWorkflowDocks (studio, true);

    studio.configureWorkflowDockLayout ();
    processUiEvents ();
    studio.show ();
    const std::vector< rws::RobWorkStudioPlugin* >& docks = studio.getPlugins ();
    ASSERT_EQ (5U, docks.size ());
    studio.resizeDocks ({docks[1]}, {500}, Qt::Horizontal);
    studio.resizeDocks ({docks[4]}, {500}, Qt::Horizontal);
    processUiEvents ();

    for (std::size_t index = 0; index < 4; ++index) {
        docks[index]->raise ();
        processUiEvents ();
        EXPECT_EQ (280, docks[index]->width ());
    }
    EXPECT_EQ (280, docks[4]->width ());
    for (rws::RobWorkStudioPlugin* dock : docks) {
        EXPECT_EQ (240, dock->minimumWidth ());
        ASSERT_NE (nullptr, dock->widget ());
        EXPECT_EQ (0, dock->widget ()->minimumWidth ());
        EXPECT_EQ (QSizePolicy::Ignored, dock->widget ()->sizePolicy ().horizontalPolicy ());
    }

    docks[1]->raise ();
    processUiEvents ();
    studio.resizeDocks ({docks[1]}, {240}, Qt::Horizontal);
    studio.resizeDocks ({docks[4]}, {240}, Qt::Horizontal);
    processUiEvents ();
    EXPECT_EQ (240, docks[1]->width ());
    EXPECT_EQ (240, docks[4]->width ());
}

// 未打开项目时，要求项目上下文的构建器 Dock 应被禁用且隐藏：
// 验证 WorkflowDockLayoutController 的项目上下文门控在无项目状态下生效，
// 保证需要项目上下文的 Dock 不会在空上下文下被用户唤起。
TEST (WorkflowDockLayout, ProjectRequiredBuilderStaysDisabledAndHiddenWithoutProject)
{
    int argc = 1;
    char name[] = "WorkflowDockLayoutControllerTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    rw::core::PropertyMap settings;
    rws::RobWorkStudio studio (settings);
    // 注册的 5 个工作流 Dock 全部声明需要项目上下文(requiresProject = true)。
    addNamedWorkflowDocks (studio, false, true);

    // 配置工作流 Dock 布局并显示，此时尚未打开任何项目。
    studio.configureWorkflowDockLayout ();
    studio.show ();
    processUiEvents ();

    const std::vector< rws::RobWorkStudioPlugin* >& docks = studio.getPlugins ();
    ASSERT_EQ (5U, docks.size ());
    // 断言第二个 Dock(RobotModelBuilder)：
    //   1) 确实声明了需要项目上下文；
    //   2) 整体与可见性入口均被禁用；
    //   3) 被强制隐藏，即使默认状态是可见的。
    EXPECT_TRUE (docks[1]->requiresProjectContext ());
    EXPECT_FALSE (docks[1]->isEnabled ());
    EXPECT_FALSE (docks[1]->visibilityAction ()->isEnabled ());
    EXPECT_FALSE (docks[1]->isVisible ());
}

TEST (WorkflowDockLayout, ExplicitModelLoadUnlocksDownstreamDocks)
{
    int argc = 1;
    char name[] = "WorkflowDockLayoutControllerTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    rw::core::PropertyMap settings;
    rws::RobWorkStudio studio (settings);
    QTemporaryDir sceneDirectory;
    ASSERT_TRUE (sceneDirectory.isValid ());
    const QString sceneFile = sceneDirectory.filePath (QStringLiteral ("robot.wc.xml"));
    QFile scene (sceneFile);
    const QByteArray sceneXml ("<WorkCell name=\"Robot\" />\n");
    ASSERT_TRUE (scene.open (QIODevice::WriteOnly));
    ASSERT_EQ (sceneXml.size (), scene.write (sceneXml));
    scene.close ();
    studio.setWorkCell (sceneFile.toStdString ());
    ASSERT_NE (nullptr, studio.getWorkCell ().get ());
    ASSERT_EQ (QFileInfo (sceneFile).canonicalFilePath (),
               QFileInfo (QString::fromStdString (studio.getWorkCell ()->getFilename ()))
                   .canonicalFilePath ());
    addNamedWorkflowDocks (studio);

    studio.configureWorkflowDockLayout ();
    studio.show ();
    processUiEvents ();

    const std::vector< rws::RobWorkStudioPlugin* >& docks = studio.getPlugins ();
    ASSERT_EQ (5U, docks.size ());
    const std::array< QAction*, 5 > actions = visibilityActions (docks);
    QTabBar* lockedTabs = workflowTabBar (studio);
    ASSERT_NE (nullptr, lockedTabs);
    EXPECT_FALSE (docks[0]->isEnabled ());
    EXPECT_FALSE (docks[2]->isEnabled ());
    EXPECT_FALSE (docks[3]->isEnabled ());
    EXPECT_FALSE (actions[0]->isEnabled ());
    EXPECT_FALSE (actions[2]->isEnabled ());
    EXPECT_FALSE (actions[3]->isEnabled ());
    EXPECT_FALSE (lockedTabs->isTabEnabled (0));
    EXPECT_FALSE (lockedTabs->isTabEnabled (2));
    EXPECT_FALSE (lockedTabs->isTabEnabled (3));

    studio.notifyWorkflowRobotModelLoaded (sceneFile);
    processUiEvents ();

    EXPECT_TRUE (docks[0]->isEnabled ());
    EXPECT_TRUE (docks[1]->isEnabled ());
    EXPECT_TRUE (docks[2]->isEnabled ());
    EXPECT_TRUE (docks[3]->isEnabled ());
    EXPECT_TRUE (docks[4]->isEnabled ());
    EXPECT_TRUE (actions[0]->isEnabled ());
    EXPECT_TRUE (actions[1]->isEnabled ());
    EXPECT_TRUE (actions[2]->isEnabled ());
    EXPECT_TRUE (actions[3]->isEnabled ());
    EXPECT_TRUE (actions[4]->isEnabled ());

    QTabBar* tabs = workflowTabBar (studio);
    ASSERT_NE (nullptr, tabs);
    EXPECT_TRUE (tabs->isTabEnabled (0));
    EXPECT_TRUE (tabs->isTabEnabled (1));
    EXPECT_TRUE (tabs->isTabEnabled (2));
    EXPECT_TRUE (tabs->isTabEnabled (3));
}

TEST (WorkflowDockLayout, ClosingActiveModelWorkCellRelocksDownstreamDocks)
{
    int argc = 1;
    char name[] = "WorkflowDockLayoutControllerTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    rw::core::PropertyMap settings;
    rws::RobWorkStudio studio (settings);
    QTemporaryDir sceneDirectory;
    ASSERT_TRUE (sceneDirectory.isValid ());
    const QString sceneFile = sceneDirectory.filePath (QStringLiteral ("robot.wc.xml"));
    QFile scene (sceneFile);
    const QByteArray sceneXml ("<WorkCell name=\"Robot\" />\n");
    ASSERT_TRUE (scene.open (QIODevice::WriteOnly));
    ASSERT_EQ (sceneXml.size (), scene.write (sceneXml));
    scene.close ();
    studio.setWorkCell (sceneFile.toStdString ());
    ASSERT_NE (nullptr, studio.getWorkCell ().get ());
    addNamedWorkflowDocks (studio);

    studio.configureWorkflowDockLayout ();
    studio.show ();
    processUiEvents ();

    const std::vector< rws::RobWorkStudioPlugin* >& docks = studio.getPlugins ();
    ASSERT_EQ (5U, docks.size ());
    const std::array< QAction*, 5 > actions = visibilityActions (docks);
    studio.notifyWorkflowRobotModelLoaded (QFileInfo (sceneFile).canonicalFilePath ());
    processUiEvents ();

    QTabBar* tabs = workflowTabBar (studio);
    ASSERT_NE (nullptr, tabs);
    EXPECT_TRUE (docks[0]->isEnabled ());
    EXPECT_TRUE (docks[2]->isEnabled ());
    EXPECT_TRUE (docks[3]->isEnabled ());
    EXPECT_TRUE (actions[0]->isEnabled ());
    EXPECT_TRUE (actions[2]->isEnabled ());
    EXPECT_TRUE (actions[3]->isEnabled ());
    EXPECT_TRUE (tabs->isTabEnabled (0));
    EXPECT_TRUE (tabs->isTabEnabled (2));
    EXPECT_TRUE (tabs->isTabEnabled (3));

    studio.closeWorkCell ();
    processUiEvents ();

    EXPECT_FALSE (docks[0]->isEnabled ());
    EXPECT_TRUE (docks[1]->isEnabled ());
    EXPECT_FALSE (docks[2]->isEnabled ());
    EXPECT_FALSE (docks[3]->isEnabled ());
    EXPECT_TRUE (docks[4]->isEnabled ());
    EXPECT_FALSE (actions[0]->isEnabled ());
    EXPECT_TRUE (actions[1]->isEnabled ());
    EXPECT_FALSE (actions[2]->isEnabled ());
    EXPECT_FALSE (actions[3]->isEnabled ());
    EXPECT_TRUE (actions[4]->isEnabled ());
    EXPECT_FALSE (tabs->isTabEnabled (0));
    EXPECT_TRUE (tabs->isTabEnabled (1));
    EXPECT_FALSE (tabs->isTabEnabled (2));
    EXPECT_FALSE (tabs->isTabEnabled (3));
    EXPECT_EQ (QStringLiteral ("RobotModelBuilder"), studio.activeWorkflowDockName ());
}

TEST (WorkflowDockLayout, IgnoresUnrelatedTabBarsWhenLockingAndSelectingActiveDock)
{
    int argc = 1;
    char name[] = "WorkflowDockLayoutControllerTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    rw::core::PropertyMap settings;
    rws::RobWorkStudio studio (settings);
    auto* unrelated = new LookalikeTabDock ();
    studio.addPlugin (unrelated, false, Qt::LeftDockWidgetArea);
    addNamedWorkflowDocks (studio);

    studio.configureWorkflowDockLayout ();
    studio.show ();
    processUiEvents ();

    QTabBar* tabs = workflowTabBar (studio);
    ASSERT_NE (nullptr, tabs);
    EXPECT_FALSE (tabs->isTabEnabled (0));
    EXPECT_TRUE (tabs->isTabEnabled (1));
    EXPECT_FALSE (tabs->isTabEnabled (2));
    EXPECT_FALSE (tabs->isTabEnabled (3));
    EXPECT_EQ (QStringLiteral ("RobotModelBuilder"), studio.activeWorkflowDockName ());

    ASSERT_NE (nullptr, unrelated->tabs ());
    EXPECT_TRUE (unrelated->tabs ()->isTabEnabled (0));
    EXPECT_TRUE (unrelated->tabs ()->isTabEnabled (1));
    EXPECT_TRUE (unrelated->tabs ()->isTabEnabled (2));
    EXPECT_TRUE (unrelated->tabs ()->isTabEnabled (3));
    EXPECT_TRUE (unrelated->tabs ()->isTabEnabled (4));
}
