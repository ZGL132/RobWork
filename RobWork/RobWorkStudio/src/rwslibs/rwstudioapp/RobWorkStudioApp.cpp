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

#include "RobWorkStudioApp.hpp"

#ifdef _WIN32
#include <windows.h>
#endif    //#ifdef _WIN32

#include <RobWorkConfig.hpp>
#include <RobWorkStudioConfig.hpp>
#include <rw/common/ProgramOptions.hpp>
#include <rw/core/Exception.hpp>
#include <rw/core/PropertyMap.hpp>
#include <rw/core/RobWork.hpp>

#include "ExceptionDiagnostics.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSplashScreen>
#ifdef RWS_USE_STATIC_LINK_PLUGINS
#ifdef RWS_HAVE_PLUGIN_JOG
#include <rwslibs/jog/Jog.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_LOG
#include <rwslibs/log/ShowLog.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_PLANNING
#include <rwslibs/planning/Planning.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_PLAYBACK
#include <rwslibs/playback/PlayBack.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_PROPERTYVIEW
#include <rwslibs/propertyview/PropertyView.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_SENSORS
#include <rwslibs/sensors/Sensors.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_TREEVIEW
#include <rwslibs/treeview/TreeView.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_WORKCELLEDITOR
#include <rwslibs/workcelleditorplugin/WorkcellEditorPlugin.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_ROBOTMODELBUILDER
#include <rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_ENGINEERINGREQUIREMENTS
#include <rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_KINEMATICANALYSIS
#include <rwslibs/kinematicanalysis/KinematicAnalysisPlugin.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_STRUCTUREOPTIMIZER
#include <rwslibs/structureoptimizer/StructureOptimizerPlugin.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_LUAPL
#include <rwslibs/lua/Lua.hpp>
#endif
#ifdef RWS_HAVE_PLUGIN_PYTHONEDITOR
#include <rwslibs/pythoneditor/Editor.hpp>
#endif
#endif
#ifdef RWS_HAVE_GLUT
#if defined(RW_MACOS)
//#include <GLUT/glut.h>
// TODO(kalor) Figure Out how to get GLUT to work as glutBitmapString is undeclared i mac
#undef RW_HAVE_GLUT
#else
#include <GL/freeglut.h>
#endif
#endif

#include <boost/filesystem.hpp>
#include <boost/program_options/parsers.hpp>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <thread>
#include <typeinfo>

USE_ROBWORK_NAMESPACE
using namespace robwork;

using namespace rws;

namespace {

// 返回本次进程唯一的异常日志文件路径（AppLocalData 目录下 logs/exception-<时间戳>.log），
// 首次调用时创建目录并固定路径，保证多次弹窗都追加到同一文件。
QString exceptionLogPath ()
{
    static QString path;
    if (path.isEmpty ()) {
        QString base = QStandardPaths::writableLocation (QStandardPaths::AppLocalDataLocation);
        if (base.isEmpty ())
            base = QDir::homePath () + QStringLiteral ("/.RobWorkStudio");

        QDir logDirectory (base);
        logDirectory.mkpath (QStringLiteral ("logs"));
        path = logDirectory.filePath (
            QStringLiteral ("logs/exception-%1.log")
                .arg (QDateTime::currentDateTime ().toString (QStringLiteral ("yyyyMMdd-hHmmss"))));
    }
    return path;
}

// 把当前线程 ID 哈希为字符串，用于区分异常来自哪个线程。
std::string currentThreadId ()
{
    std::ostringstream stream;
    stream << std::hash< std::thread::id > () (std::this_thread::get_id ());
    return stream.str ();
}

// 事件接收者的类名（取自元对象），用于定位哪个 Qt 对象在处理事件时抛出。
std::string receiverClass (QObject* receiver)
{
    return receiver == NULL || receiver->metaObject () == NULL
               ? std::string ()
               : receiver->metaObject ()->className ();
}

// 事件接收者的对象名（objectName），与类名一起缩小问题范围。
std::string receiverObjectName (QObject* receiver)
{
    return receiver == NULL ? std::string () : receiver->objectName ().toStdString ();
}

// 组装异常诊断、写入异常日志，并返回格式化文本（供弹窗详情展示）。
// receiver 为空时回退用 receiverContext 标识调用方（如主流程）。
std::string reportException (QObject* receiver, const std::string& phase,
                             const std::string& operation, const std::string& category,
                             const std::string& message,
                             const std::string& receiverContext = std::string ())
{
    const std::string logPath = exceptionLogPath ().toStdString ();
    std::string receiverName = receiverClass (receiver);
    if (receiverName.empty ())
        receiverName = receiverContext;
    const rws::ExceptionDiagnostic diagnostic = {
        QDateTime::currentDateTime ().toString (Qt::ISODateWithMs).toStdString (),
        phase,
        operation,
        category,
        message,
        receiverName,
        receiverObjectName (receiver),
        currentThreadId (),
        logPath};
    const std::string details = rws::formatExceptionDiagnostic (diagnostic);
    rws::appendExceptionDiagnostic (logPath, diagnostic);
    return details;
}

// 弹出一个带完整诊断信息的异常对话框：汇总 + 日志路径 + 可展开的 key=value 详情，
// 取代原先只有一句 "This is likely a bug" 的无信息弹窗。
void showExceptionDialog (QObject* receiver, const QString& title, const QString& summary,
                          const std::string& phase, const std::string& operation,
                          const std::string& category,
                          const std::string& message,
                          const std::string& receiverContext = std::string ())
{
    const std::string details =
        reportException (receiver, phase, operation, category, message, receiverContext);
    const QString logPath = exceptionLogPath ();

    QMessageBox box (QMessageBox::Critical, title, summary, QMessageBox::Ok, NULL);
    box.setInformativeText (QStringLiteral ("Detailed diagnostics were written to:\n%1")
                                .arg (logPath));
    box.setDetailedText (QString::fromStdString (details));
    box.exec ();
}

bool isUsableEnvironmentPath (const char* path)
{
    if (path == NULL)
        return false;

    for (const char* ch = path; *ch != '\0'; ++ch) {
        if (*ch != ' ' && *ch != '\t')
            return true;
    }
    return false;
}

std::string getHomeDirectory ()
{
    const char* home = std::getenv ("HOME");
    if (isUsableEnvironmentPath (home))
        return home;

    const char* userProfile = std::getenv ("USERPROFILE");
    if (isUsableEnvironmentPath (userProfile))
        return userProfile;

    return ".";
}

class MyQApplication : public QApplication
{
  public:
    MyQApplication (int& argc, char** argv) : QApplication (argc, argv), devMode (false) {}

    bool notify (QObject* rec, QEvent* ev)
    {
        if (!devMode) {
            try {
                return QApplication::notify (rec, ev);
            }
            catch (const rw::core::Exception& e) {
                showExceptionDialog (rec, tr ("RobWork exception"),
                                     tr ("A RobWork exception interrupted the current operation."),
                                     "event-loop",
                                     "QApplication::notify",
                                     "rw::core::Exception", e.what ());
            }
            catch (const std::exception& e) {
                showExceptionDialog (rec, tr ("C++ exception"),
                                     tr ("A C++ exception interrupted the current operation."),
                                     "event-loop",
                                     "QApplication::notify",
                                     typeid (e).name (), e.what ());
            }
            catch (...) {
                showExceptionDialog (
                    rec, tr ("Unknown C++ exception"),
                    tr ("An exception without a standard diagnostic interrupted the current operation."),
                    "event-loop",
                    "QApplication::notify",
                    "unknown C++ exception", "The exception type and message are unavailable.");
            }
        }
        else {
            return QApplication::notify (rec, ev);
        }
        return false;
    }

    bool devMode;
};
}    // namespace

RobWorkStudioApp::RobWorkStudioApp (const std::string& args) :
    _rwstudio (NULL), _args (args), _thread (NULL), _isRunning (false)
{}

RobWorkStudioApp::RobWorkStudioApp (int argc, char** argv) :
    _rwstudio (NULL), _thread (NULL), _isRunning (false)
{
    for (int i = 0; i < argc; ++i) {
        _arguments.push_back (argv[i]);
    }
}

RobWorkStudioApp::~RobWorkStudioApp ()
{
    if (_isRunning) {
        QCloseEvent e = QCloseEvent ();
        _rwstudio->event (&e);
    }
}

void RobWorkStudioApp::start ()
{
    _thread = new boost::thread (boost::bind (&RobWorkStudioApp::run, this));
    while (!this->isRunning ()) {
        rw::common::TimerUtil::sleepMs (1);
    }
}

void RobWorkStudioApp::close ()
{
    if (isRunning ()) {
        _rwstudio->postExit ();
        while (isRunning ()) {
            rw::common::TimerUtil::sleepMs (1);
        }

        // Make Sure All Widgets are closed to avoid segfault
        QWidgetList all_w = QApplication::allWidgets ();
        long ctime        = rw::common::TimerUtil::currentTimeMs ();
        while (all_w.count () > 0 && rw::common::TimerUtil::currentTimeMs () - ctime < 300) {
            rw::common::TimerUtil::sleepMs (1);
            all_w = QApplication::allWidgets ();
        }
        rw::common::TimerUtil::sleepMs (1000);    // Final timing to let the rest of QT close down
    }
}

void initReasource ()
{
    Q_INIT_RESOURCE (rwstudio_resources);
}

namespace {
std::vector< std::string > split (std::string str, std::string token)
{
    std::vector< std::string > result;
    size_t index = str.find (token);
    if (index != std::string::npos) {
        while (str.size ()) {
            size_t index = str.find (token);
            if (index != std::string::npos) {
                result.push_back (str.substr (0, index));
                str = str.substr (index + token.size ());
                if (str.size () == 0)

                    result.push_back (str);
            }
            else {
                result.push_back (str);
                str = "";
            }
        }
    }
    else {
        result.push_back (str);
    }
    return result;
}

void loadPluginFolder (RobWorkStudio* rws, const std::string& folder,
                       const std::vector< std::string >& excludeList)
{
    if (boost::filesystem::exists (folder)) {
        boost::filesystem::path p2 (folder);
        for (boost::filesystem::directory_iterator i (p2);
             i != boost::filesystem::directory_iterator ();
             i++) {
            const boost::filesystem::path path = i->path ();
            if (!boost::filesystem::is_regular_file (path))
                continue;

            const std::string extension = path.extension ().string ();
            if (extension != ".dll" && extension != ".so" && extension != ".dylib")
                continue;

            const std::string base = path.stem ().string ();
            if (base.find ("sdurws_") == std::string::npos &&
                base.find ("libsdurws_") == std::string::npos)
                continue;

            std::string plPath = std::string (folder) + "/" + i->path ().filename ().string ();

            bool exclude = false;
            for (const std::string& expl : excludeList) {
                if (plPath == expl || i->path ().filename ().string () == expl) {
                    exclude = true;
                    break;
                }
            }

            if (!exclude) {
                rws->loadPlugin (plPath.c_str (), 0, 1);
            }
        }
    }
}
}    // namespace

int RobWorkStudioApp::run ()
{
    initReasource ();

    char* argv[30];
    std::vector< std::string > args = _arguments;
    if (args.empty () && !_args.empty ()) {
        args = boost::program_options::split_unix (_args);
    }

    if (args.size () == 0) {
        args.push_back ("RobWorkStudio");
    }
    for (size_t i = 0; i < args.size (); i++) {
        argv[i] = &(args[i][0]);
    }

    int argc = (int) args.size ();
    // now initialize robwork, such that plugins and stuff might work

    if (argc == 0) {
        RobWork::init ();
    }
    else {
        RobWork::init (argc, argv);
    }

    ProgramOptions poptions ("RobWorkStudio", RW_VERSION);

#if defined(QT_DEBUG) || defined(_DEBUG)
    const bool developerModeDefault = true;
#else
    const bool developerModeDefault = false;
#endif

    const std::string homeDirectory = getHomeDirectory ();
    poptions.addStringOption ("ini-file",
                              homeDirectory + "/.RobWorkStudio.ini",
                              "RobWorkStudio ini-file");
    poptions.addStringOption ("input-file", "", "Project/Workcell/Device input file");
    poptions.addStringOption (
        "rwsplugin", "", "load RobWorkStudio plugin, not to be confused with '--rwplugin'");
    poptions.addStringOption ("nosplash", "", "If defined the splash screen will not be shown");
    poptions.addStringOption ("exclude-plugins", "", "list of plugins not to load seperated by ,");
    poptions.addBoolOption (
        "developer", developerModeDefault,
        "use developer mode. Leave exceptions uncaught for a debugger such as Qt Creator");
    poptions.setPositionalOption ("input-file", -1);

    poptions.initOptions ();

    poptions.parse (argc, argv);

    PropertyMap map = poptions.getPropertyMap ();

    bool showSplash     = false;    //! map.has("nosplash");
    std::string inifile = map.get< std::string > ("ini-file", "");
    RobWork rw;
    if (!boost::filesystem::exists (inifile)) {
        rw.getLog ().infoLog () << "inifile not found at: " << inifile << std::endl;
        if (boost::filesystem::exists (homeDirectory + "/RobWorkStudio.ini")) {
            inifile = homeDirectory + "/RobWorkStudio.ini";
        }
        else if (boost::filesystem::exists ("RobWorkStudio.ini")) {
            inifile = "RobWorkStudio.ini";
        }
        if (boost::filesystem::exists (inifile)) {
            rw.getLog ().infoLog () << "using inifile: " << inifile << std::endl;
        }
    }

    std::string inputfile = map.get< std::string > ("input-file", "");

    std::string rwsplugin = map.get< std::string > ("rwsplugin", "");
    std::vector< std::string > excludePl =
        split (map.get< std::string > ("exclude-plugins", ""), ",");

    {
        MyQApplication app (argc, argv);
        app.devMode = map.get< bool > ("developer");
#ifdef RWS_HAVE_GLUT
        glutInit (&argc, argv);
#endif

        // 生命周期阶段/步骤跟踪：主流程各关键节点更新这些变量，任何异常发生时
        // 弹窗与日志都能说明"故障发生在启动/事件循环/关闭的哪一步"。
        std::string lifecyclePhase = "startup";
        std::string lifecycleStep = "initialization";
        std::function<void(void)> AppRunner = [&]() {
            QSplashScreen* splash = NULL;
            if (showSplash) {
                QPixmap pixmap (":/images/splash.jpg");
                splash = new QSplashScreen (pixmap);
                splash->show ();
                // Loading some items
                splash->showMessage ("Adding static plugins");
            }

            app.processEvents ();

            // Establishing connections

            if (showSplash)
                splash->showMessage ("Loading static plugins");

            std::string pluginFolder = "./plugins/";
            {
                Timer t;

                lifecycleStep = "constructing RobWorkStudio";
                rws::RobWorkStudio rwstudio (map);

#ifdef RWS_USE_STATIC_LINK_PLUGINS
#ifdef RWS_HAVE_PLUGIN_LOG

                rwstudio.addPlugin (new rws::ShowLog (), false, Qt::BottomDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_JOG

                rwstudio.addPlugin (new rws::Jog (), false, Qt::RightDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_TREEVIEW

                rwstudio.addPlugin (new rws::TreeView (), false, Qt::LeftDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_PLAYBACK

                rwstudio.addPlugin (new rws::PlayBack (), false, Qt::BottomDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_PROPERTYVIEW

                rwstudio.addPlugin (new rws::PropertyView (), false, Qt::LeftDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_PLANNING

                rwstudio.addPlugin (new rws::Planning (), false, Qt::LeftDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_SENSORS

                rwstudio.addPlugin (new rws::Sensors (), false, Qt::RightDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_WORKCELLEDITOR

                rwstudio.addPlugin (
                    new rws::WorkcellEditorPlugin (), false, Qt::LeftDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_ENGINEERINGREQUIREMENTS

                rwstudio.addPlugin (
                    new rws::EngineeringRequirementsPlugin (), false, Qt::LeftDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_ROBOTMODELBUILDER

                rwstudio.addPlugin (
                    new rws::RobotModelBuilderPlugin (), false, Qt::LeftDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_KINEMATICANALYSIS

                rwstudio.addPlugin (
                    new rws::KinematicAnalysisPlugin (), false, Qt::LeftDockWidgetArea);
#endif
#ifdef RWS_HAVE_PLUGIN_STRUCTUREOPTIMIZER

                rwstudio.addPlugin (
                    new rws::StructureOptimizerPlugin (), false, Qt::LeftDockWidgetArea);
#endif
#ifdef RW_HAVE_EIGEN

                rwstudio.addPlugin (new rws::Calibration (), false, Qt::RightDockWidgetArea);
#endif

#if RWS_HAVE_PLUGIN_LUAPL

                rwstudio.addPlugin (new rws::Lua (), false, Qt::LeftDockWidgetArea);
#endif
#if RWS_HAVE_PLUGIN_PYTHONEDITOR
                rwstudio.addPlugin (new rws::PyEditor (), false, Qt::LeftDockWidgetArea);
#endif
#endif

                // Load all plugins from the local rwsplugins folder
                loadPluginFolder (&rwstudio, RWS_COMPILE_PLUGIN_DIR, excludePl);

                if (showSplash) {
                    splash->showMessage ("Loading static plugins");
                }

                rwstudio.loadSettingsSetupPlugins (inifile);
                // Load all plugins from the rwsplugins folder
                if (boost::filesystem::exists ("/usr/lib/")) {
                    boost::filesystem::path p ("/usr/lib");
                    // Find the architecture dependendt folder containing the
                    // rwsplugins folder
                    std::string rwspluginFolder = "";
                    for (boost::filesystem::directory_iterator i (p);
                         i != boost::filesystem::directory_iterator ();
                         i++) {
                        if (boost::filesystem::is_directory (i->path ())) {
                            rwspluginFolder = "/usr/lib/";
                            rwspluginFolder += i->path ().filename ().string ();
                            rwspluginFolder += "/RobWork/rwsplugins";
                            if (boost::filesystem::exists (rwspluginFolder)) {
                                break;
                            }
                            else {
                                rwspluginFolder = "";
                            }
                        }
                    }
                    // Load all plugins from the rwsplugins folder
                    loadPluginFolder (&rwstudio, rwspluginFolder, excludePl);
                }
                if (inputfile.empty ()) {
                    std::string workcellFile = rwstudio.loadSettingsWorkcell (inifile);
                    if (showSplash) {
                        splash->showMessage ("Opening workcell...");
                    }
                    rwstudio.openFile (workcellFile);
                }

                if (!inputfile.empty ()) {
                    if (showSplash)
                        splash->showMessage ("Opening workcell...");
                    rwstudio.openFile (inputfile);
                }

                if (!rwsplugin.empty ()) {
                    rwstudio.loadPlugin (rwsplugin);
                }
                rwstudio.configureWorkflowDockLayout ();

                // load configuration into RobWorkStudio
                if (showSplash) {
                    splash->showMessage ("Loading settings");
                    splash->finish (&rwstudio);
                }

                _rwstudio = &rwstudio;
                rwstudio.show ();
                _isRunning = true;

                lifecyclePhase = "event-loop";
                lifecycleStep = "QApplication::exec";
                app.exec ();
                lifecyclePhase = "shutdown";
                lifecycleStep = "RobWorkStudio destructor";
                _isRunning = false;
                _rwstudio  = NULL;
            }
        };
        if (app.devMode) {
            AppRunner ();
        }
        else {
            try {
                AppRunner ();
            }
            catch (const rw::core::Exception& e) {
                const QString phase = QString::fromStdString (lifecyclePhase);
                showExceptionDialog (
                    NULL, QStringLiteral ("RobWork exception"),
                    QStringLiteral ("RobWorkStudio failed during %1.").arg (phase), lifecyclePhase,
                    lifecycleStep,
                    "rw::core::Exception", e.what (), "RobWorkStudioApp");
                _isRunning = false;
                return -1;
            }
            catch (const std::exception& e) {
                const QString phase = QString::fromStdString (lifecyclePhase);
                showExceptionDialog (NULL, QStringLiteral ("C++ exception"),
                                     QStringLiteral ("RobWorkStudio failed during %1.").arg (phase),
                                     lifecyclePhase, lifecycleStep, typeid (e).name (), e.what (),
                                     "RobWorkStudioApp");
                _isRunning = false;
                return -1;
            }
            catch (int value) {
                const QString phase = QString::fromStdString (lifecyclePhase);
                showExceptionDialog (NULL, QStringLiteral ("Non-standard exception"),
                                     QStringLiteral ("RobWorkStudio failed during %1.").arg (phase),
                                     lifecyclePhase, lifecycleStep, "integer exception", std::to_string (value),
                                     "RobWorkStudioApp");
                _isRunning = false;
                return -1;
            }
            catch (...) {
                const QString phase = QString::fromStdString (lifecyclePhase);
                showExceptionDialog (
                    NULL, QStringLiteral ("Unknown C++ exception"),
                    QStringLiteral ("RobWorkStudio failed during %1.").arg (phase), lifecyclePhase,
                    lifecycleStep,
                    "unknown C++ exception", "The exception type and message are unavailable.",
                    "RobWorkStudioApp");
                _isRunning = false;
                return -1;
            }
        }
    }
    _isRunning = false;
    return 0;
}
