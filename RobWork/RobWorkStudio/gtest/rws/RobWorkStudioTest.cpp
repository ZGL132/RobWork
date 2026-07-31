/********************************************************************************
 * Copyright 2016 The Robotics Group, The Maersk Mc-Kinney Moller Institute,
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
#include <rws/ProjectManager.hpp>
#include <rws/RobWorkStudio.hpp>
#include <rws/RobWorkStudioPlugin.hpp>
#include <rwslibs/rwstudioapp/RobWorkStudioApp.hpp>

#include "../TestEnvironment.hpp"

#include <QApplication>
#include <QDir>
#include <QMetaObject>
#include <QString>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace rw::core;
using namespace rw::common;
using namespace rws;

namespace {

QString createEmptyProject (const QString& directoryPath)
{
    // 生命周期测试只需要一个没有入口资源的合法项目：空项目可避免依赖外部测试数据，
    // 同时仍会建立 ProjectManager 与文档注册表上下文，足以验证独立 WorkCell 操作
    // 是否真正解除项目绑定，而不是只替换三维视图中的内存对象。
    rws::ProjectManifest manifest;
    manifest.project.id   = QStringLiteral ("standalone-lifecycle-project");
    manifest.project.name = QStringLiteral ("StandaloneLifecycleProject");

    const QString projectFile = QDir (directoryPath).filePath ("Lifecycle.rwproj");
    rws::ProjectManager manager;
    QString error;
    EXPECT_TRUE (manager.createProject (projectFile, manifest, &error))
        << error.toStdString ();
    manager.closeProject ();
    return projectFile;
}

}    // namespace

TEST (RobWorkStudio, LaunchTest)
{
    int argc      = 1;
    char name[]   = "RobWorkStudio";
    char* argv[1] = {name};
    PropertyMap map;
    QApplication app (argc, argv);
    RobWorkStudio rwstudio (map);
    rwstudio.show ();
    app.processEvents ();

    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = createEmptyProject (directory.path ());

    // “新建空 WorkCell”是从项目工作模式切换到临时单文档模式的显式用户操作。
    // 若只替换 WorkCell 而保留项目上下文，后续状态修改会继续误标记原项目资源，
    // 因此这里固定要求项目名称必须从标题栏消失。
    rwstudio.openFile (projectFile.toStdString ());
    app.processEvents ();
    ASSERT_TRUE (rwstudio.windowTitle ().contains (QStringLiteral ("StandaloneLifecycleProject")));
    ASSERT_TRUE (QMetaObject::invokeMethod (&rwstudio, "newWorkCell", Qt::DirectConnection));
    EXPECT_FALSE (rwstudio.windowTitle ().contains (QStringLiteral ("StandaloneLifecycleProject")));

    // “打开单个资源”加载独立 WorkCell 时同样必须退出当前项目。测试重新打开项目后
    // 再走公开 openFile 入口，防止菜单、拖放和最近文件列表使用不同的生命周期规则。
    rwstudio.openFile (projectFile.toStdString ());
    app.processEvents ();
    ASSERT_TRUE (rwstudio.windowTitle ().contains (QStringLiteral ("StandaloneLifecycleProject")));
    // 复用传感器测试长期使用的有效 WorkCell，避免在本测试中重新构造 XML 夹具时
    // 引入与项目生命周期无关的碰撞配置属性要求。
    const QString standaloneWorkCell = QString::fromStdString (
        TestEnvironment::testfilesDir () + "SensorTest.wc.xml");
    rwstudio.openFile (standaloneWorkCell.toStdString ());
    app.processEvents ();
    EXPECT_FALSE (rwstudio.windowTitle ().contains (QStringLiteral ("StandaloneLifecycleProject")));

    TimerUtil::sleepMs (1000);
    rwstudio.close ();
    TimerUtil::sleepMs (2000);
}

#ifndef WIN32
TEST (RobWorkStudio, PluginLoadTest)
{
    rws::RobWorkStudioApp rwsApp ("");
    rwsApp.start ();
    rws::RobWorkStudio* rwstudio = rwsApp.getRobWorkStudio ();
    TimerUtil::sleepMs (1000);
    std::vector< rws::RobWorkStudioPlugin* > pl = rwstudio->getPlugins ();

    std::vector< QString > plugins = {"ATaskVisPlugin",
                                      "PlayBack",
                                      "Jog",
                                      "Workcell Editor",
                                      "Log",
                                      "LuaConsole",
                                      "TreeView",
                                      "Planning",
                                      "PropertyView",
                                      "Sensors",
                                      "GTaskVisPlugin"};
    for (QString& pn : plugins) {
        bool exist = false;
        for (rws::RobWorkStudioPlugin* p : pl) {
            if (p->name () == pn) {
                exist = true;
                break;
            }
        }
        if (!exist) {    // Print som debug output
            std::cout << "Could not find '" << pn.toStdString () << "' in list: " << std::endl;
            for (rws::RobWorkStudioPlugin* p : pl) {
                std::cout << p->name ().toStdString () << ", ";
            }
            std::cout << std::endl;
        }

        EXPECT_TRUE (exist);
    }

    rwsApp.close ();
    TimerUtil::sleepMs (1000);
}
#endif
