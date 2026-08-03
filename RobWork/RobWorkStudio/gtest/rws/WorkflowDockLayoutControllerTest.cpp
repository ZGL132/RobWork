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
#include <gtest/gtest.h>

#include <array>

namespace {

class NamedWorkflowDock : public rws::RobWorkStudioPlugin
{
  public:
    explicit NamedWorkflowDock (const QString& name) : RobWorkStudioPlugin (name, QIcon ()) {}
};

void addNamedWorkflowDocks (rws::RobWorkStudio& studio)
{
    for (const QString& name : {QStringLiteral ("EngineeringRequirements"),
                                QStringLiteral ("RobotModelBuilder"),
                                QStringLiteral ("KinematicAnalysis"),
                                QStringLiteral ("StructureOptimizer"),
                                QStringLiteral ("Jog")}) {
        studio.addPlugin (new NamedWorkflowDock (name), false, Qt::LeftDockWidgetArea);
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
        if (tabNames (*tabBar).contains (QStringLiteral ("RobotModelBuilder")))
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
        EXPECT_EQ (docks[index]->width (), docks[4]->width ());
    }
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
