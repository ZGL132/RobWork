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

#define QT_NO_EMIT
#include "RobWorkStudio.hpp"

#include "AboutBox.hpp"
#include "HelpAssistant.hpp"
#include "ProjectPathResolver.hpp"
#include "ProjectSaveTransaction.hpp"
#include "RobWorkStudioPlugin.hpp"
#include "WorkflowDockLayoutController.hpp"

#include <rw/common/TimerUtil.hpp>
#include <rw/core/Exception.hpp>
#include <rw/core/StringUtil.hpp>
#include <rw/core/os.hpp>
#include <rw/geometry/BSphere.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/loaders/dom/DOMPropertyMapLoader.hpp>
#include <rw/loaders/dom/DOMPropertyMapSaver.hpp>
#include <rw/loaders/dom/DOMWorkCellSaver.hpp>
#include <rw/loaders/rwxml/XMLRWLoader.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>
#include <rw/proximity/CollisionSetup.hpp>
#include <rwlibs/proximitystrategies/ProximityStrategyFactory.hpp>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMetaMethod>
#include <QAbstractButton>
#include <QMessageBox>
#include <QMimeData>
#include <QObject>
#include <QPluginLoader>
#include <QPushButton>
#include <QSettings>
#include <QScopedValueRollback>
#include <QSet>
#include <QStorageInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QXmlStreamReader>

#ifdef RWS_USE_PYTHON
#include <rws/pythonpluginloader/PyPlugin.hpp>
#endif    // RWS_USE_PYTHON
#include <RobWorkConfig.hpp>
#include <rws/propertyview/PropertyViewEditor.hpp>

#include <boost/bind/bind.hpp>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <exception>
#include <limits>
#include <sstream>
#include <utility>

using namespace rw;
using namespace rw::core;
using namespace rw::common;
using namespace rw::loaders;
using namespace rw::math;
using namespace rw::models;
using namespace rw::kinematics;
using namespace rw::proximity;
using namespace rw::trajectory;
using namespace rwlibs::proximitystrategies;

using namespace rws;

namespace {
constexpr int kMaxProjectBaselineRegularFiles = 1024;
constexpr quint64 kMaxProjectBaselineTotalBytes = 64ULL * 1024ULL * 1024ULL;
constexpr int kMaxProjectBaselineDepth = 32;
constexpr int kMaxProjectBaselineRelativePathLength = 1024;
constexpr int kMaxProjectBaselineEntries = 4096;
constexpr quint64 kProjectBaselineBackupSafetyBytes = 16ULL * 1024ULL * 1024ULL;
constexpr quint64 kProjectBaselineBackupSafetyPercent = 10;

WorkCell::Ptr emptyWorkCell ()
{
    WorkCell::Ptr workcell = rw::core::ownedPtr (new WorkCell (ownedPtr (new StateStructure ())));
    CollisionSetup::set (CollisionSetup (), workcell);
    return workcell;
}

CollisionDetector::Ptr makeCollisionDetector (WorkCell::Ptr workcell)
{
    return rw::core::ownedPtr (new CollisionDetector (
        workcell, ProximityStrategyFactory::makeDefaultCollisionStrategy ()));
}

// 根据历史文件扩展名推导项目资源类型、默认目录和稳定 ID。只接受已有 Provider 的四类文件，
// 这样导入后重新打开项目时不会因未知 kind 产生无法解释的加载行为。
bool makeImportedProjectResource (const QString& sourcePath,
                                  const rws::ProjectManifest& manifest,
                                  rws::ProjectResource& resource,
                                  QString* error)
{
    const QString filename = QFileInfo (sourcePath).fileName ();
    const QString lowercaseFilename = filename.toLower ();
    QString suffix;
    QString idPrefix;
    QString kind;
    QString targetDirectory;
    if (lowercaseFilename.endsWith (QStringLiteral (".wc.xml"))) {
        suffix = QStringLiteral (".wc.xml");
        idPrefix = QStringLiteral ("scene");
        kind = QStringLiteral ("robwork.workcell");
        targetDirectory = QStringLiteral ("scenes");
    }
    else if (lowercaseFilename.endsWith (QStringLiteral (".rmb.json"))) {
        suffix = QStringLiteral (".rmb.json");
        idPrefix = QStringLiteral ("model");
        kind = QStringLiteral ("robwork.robot-model");
        targetDirectory = QStringLiteral ("models");
    }
    else if (lowercaseFilename.endsWith (QStringLiteral (".requirements.json"))) {
        suffix = QStringLiteral (".requirements.json");
        idPrefix = QStringLiteral ("requirements");
        kind = QStringLiteral ("rws.engineering-requirements");
        targetDirectory = QStringLiteral ("requirements");
    }
    else if (lowercaseFilename.endsWith (QStringLiteral (".structure-optimization.json"))) {
        suffix = QStringLiteral (".structure-optimization.json");
        idPrefix = QStringLiteral ("optimization");
        kind = QStringLiteral ("rws.structure-optimization");
        targetDirectory = QStringLiteral ("optimizations");
    }
    else if (lowercaseFilename.endsWith (QStringLiteral (".kinematic-analysis.json"))) {
        // 仅导入可编辑分析配置；可重算结果和各类导出文件均不属于项目源资源。
        suffix = QStringLiteral (".kinematic-analysis.json");
        idPrefix = QStringLiteral ("kinematic-analysis");
        kind = QStringLiteral ("rws.kinematic-analysis");
        targetDirectory = QStringLiteral ("analysis");
    }
    else {
        if (error != nullptr) {
            *error = QString::fromUtf8 (
                "仅支持导入 WorkCell (*.wc.xml)、机器人模型 (*.rmb.json)、工程需求 "
                "(*.requirements.json) 或结构优化 (*.structure-optimization.json) 文件。");
        }
        return false;
    }

    // 资源 ID 不能直接采用用户文件名：空格、中文和符号虽可作为文件名，却不适合作为跨插件
    // 依赖键。这里保留字母数字，其他连续字符归一化为单个连字符，形成可读且稳定的 ID。
    const QString sourceBaseName = filename.left (filename.size () - suffix.size ());
    QString normalizedName;
    bool previousWasSeparator = false;
    for (const QChar character : sourceBaseName) {
        if (character.isLetterOrNumber ()) {
            normalizedName.append (character.toLower ());
            previousWasSeparator = false;
        }
        else if (!normalizedName.isEmpty () && !previousWasSeparator) {
            normalizedName.append (QLatin1Char ('-'));
            previousWasSeparator = true;
        }
    }
    while (normalizedName.endsWith (QLatin1Char ('-')))
        normalizedName.chop (1);
    if (normalizedName.isEmpty ())
        normalizedName = QStringLiteral ("imported");

    const QString baseId = idPrefix + QLatin1Char ('.') + normalizedName;
    QString candidateId = baseId;
    int duplicateIndex = 2;
    rws::ProjectResource existing;
    // 同名资源通过递增序号保持可预测且不依赖随机 UUID，便于用户在清单中定位导入结果。
    while (manifest.findResource (candidateId, existing))
        candidateId = baseId + QLatin1Char ('-') + QString::number (duplicateIndex++);

    resource.id = candidateId;
    resource.kind = kind;
    resource.path = targetDirectory + QLatin1Char ('/') + filename;
    resource.ownership = QStringLiteral ("project");
    resource.required = false;
    return true;
}
}    // namespace

RobWorkStudio::RobWorkStudio (const PropertyMap& map) :
    QMainWindow (NULL), _robwork (RobWork::getInstance ()), _aboutBox (NULL),
    _inStateUpdate (false), _settingsMap (NULL), _workCellProvider (NULL)
{
    this->setObjectName ("RobWorkStudio_MainWindow");
    // Always create the about box.
    _aboutBox = new AboutBox (RW_VERSION, RW_REVISION, this);
    // should load dynamically
    // rw::core::ExtensionRegistry::getInstance()->registerExtensions( ownedPtr( new
    // RWSImageLoaderPlugin() ) ); _robwork->getPluginRepository().addPlugin(ownedPtr( new
    // ColladaLoaderPlugin() ), true);
    std::stringstream sstr;
    sstr << " RobWorkStudio v" << RW_VERSION;
    QString qstr (sstr.str ().c_str ());
    setWindowTitle (qstr);

    // time 50ms
    setWindowIcon (QIcon (":/images/rw_logo_64x64.png"));
    boost::filesystem::path settingsPath ("rwsettings.xml");

    PropertyMap settings;
    if (exists (settingsPath)) {
        try {
            _propMap = DOMPropertyMapLoader::load ("rwsettings.xml");
        }
        catch (rw::core::Exception& e) {
            RW_WARN ("Could not load settings from 'rwsettings.xml': "
                     << e.getMessage ().getText () << "\n Using default settings!");
        }
        catch (std::exception& e) {
            RW_WARN ("Could not load settings from 'rwsettings.xml': "
                     << e.what () << "\n Using default settings!");
            // loading failed so we just go on with an empty map
        }
    }
    _propMap.set ("cmdline", map);
    PropertyMap* currentSettings = _propMap.getPtr< PropertyMap > ("RobWorkStudioSettings");
    if (currentSettings == NULL) {
        _propMap.add ("RobWorkStudioSettings", "Settings for RobWorkStudio", settings);
        currentSettings = _propMap.getPtr< PropertyMap > ("RobWorkStudioSettings");
    }

    _assistant   = new HelpAssistant ();
    _settingsMap = _propMap.getPtr< PropertyMap > ("RobWorkStudioSettings");

    // set the drag and drop property to true
    setupFileActions ();
    setupToolActions ();
    setupViewGL ();
    _propEditor = new PropertyViewEditor (NULL);
    _propEditor->setPropertyMap (&_propMap);
    setupPluginsMenu ();
    setupHelpMenu ();
    int width  = _settingsMap->get< int > ("WindowWidth", 1024);
    int height = _settingsMap->get< int > ("WindowHeight", 800);
    int x      = _settingsMap->get< int > ("WindowPosX", this->pos ().x ());
    int y      = _settingsMap->get< int > ("WindowPosY", this->pos ().y ());
    std::vector< int > state_vector =
        _settingsMap->get< std::vector< int > > ("QtMainWindowState", std::vector< int > ());
    if (state_vector.size () > 0) {
        QByteArray mainAppState ((int) state_vector.size (), 0);
        for (int i = 0; i < mainAppState.size (); i++) {
            mainAppState[i] = (char) state_vector[i];
        }
        this->restoreState (mainAppState);
    }
    resize (width, height);
    this->move (x, y);

    _workcell = emptyWorkCell ();
    _state    = _workcell->getDefaultState ();
    _detector = makeCollisionDetector (_workcell);

    // WorkCell Provider 通过回调复用主窗口现有的加载、视图刷新、插件通知和 DOM 保存
    // 流程。Registry 只保存非拥有型指针，Provider 的生命周期由主窗口明确管理。
    _workCellProvider = new WorkCellProjectDocumentProvider (
        [this] (const QString& path, QString* error) {
            return loadWorkCellProjectResource (path, error);
        },
        [this] (const QString& path, QString* error) {
            return saveWorkCellProjectResource (path, error);
        });
    QString providerError;
    if (!_projectDocuments.registerProvider (_workCellProvider, &providerError)) {
        RW_WARN ("Unable to register WorkCell project provider: "
                 << providerError.toStdString ());
    }
    // Workcell given to view.
    _view->setWorkCell (_workcell);
    _view->setState (_state);

    // Workcell sent to plugins.
    openAllPlugins ();
    // updateHandler();

    setAcceptDrops (true);
}

RobWorkStudio::~RobWorkStudio ()
{
    // 先关闭所有项目文档（按依赖逆序），再释放 Provider——Provider 是非拥有型指针，
    // 由主窗口负责 delete；顺序保证在 Provider 析构前不会再有资源回调请求。
    _projectDocuments.closeResources ();
    delete _workCellProvider;
    delete _assistant;
    delete _propEditor;
}

void RobWorkStudio::propertyChangedListener (PropertyBase* base)
{
    std::string id = base->getIdentifier ();
}

void RobWorkStudio::closeEvent (QCloseEvent* e)
{
    // 应用退出与“关闭项目”必须共享同一套保存决策。用户取消关闭时必须在释放插件、
    // 清空视图或写入窗口设置之前立即返回，避免界面仍在但项目上下文已被破坏。
    if (!confirmProjectClose ()) {
        e->ignore ();
        return;
    }
    closeProjectDocuments ();

    QByteArray mainAppState = saveState ();
    std::vector< int > state_vector (mainAppState.size ());
    for (int i = 0; i < mainAppState.size (); i++) {
        state_vector[i] = mainAppState[i];
    }

    _settingsMap->set< std::vector< int > > ("QtMainWindowState", state_vector);
    _settingsMap->set< int > ("WindowPosX", this->pos ().x ());
    _settingsMap->set< int > ("WindowPosY", this->pos ().y ());
    _settingsMap->set< int > ("WindowWidth", this->width ());
    _settingsMap->set< int > ("WindowHeight", this->height ());

    closeAllPlugins ();

    // close all plugins
    typedef std::vector< RobWorkStudioPlugin* >::iterator I;
    for (I it = _plugins.begin (); it != _plugins.end (); ++it) {
        (*it)->QWidget::close ();
    }

    if (!_propMap.get< PropertyMap > ("cmdline").has ("NoSave")) {
        _propMap.set ("cmdline", PropertyMap ());
        _propMap.erase ("LuaState");
        try {
            DOMPropertyMapSaver::save (_propMap, "rwsettings.xml");
        }
        catch (const rw::core::Exception& e) {
            RW_WARN ("Error saving settings file: " << e);
        }
        catch (...) {
            RW_WARN ("Error saving settings file due to unknown exception!");
        }
    }
    _propMap = PropertyMap ();
    _propEditor->close ();

    _view->clear ();
    _view->close ();

    // now call accept
    e->accept ();
}

rw::core::Log& RobWorkStudio::log ()
{
    return _robwork->getLog ();
}

rw::core::Log::Ptr RobWorkStudio::logPtr ()
{
    return _robwork->getLogPtr ();
}

void RobWorkStudio::updateLastFiles ()
{
    QMenu* filemenu                                                = _fileMenu;
    std::vector< std::pair< QAction*, std::string > >& fileactions = _lastFilesActions;
    std::vector< std::string > nfiles = _settingsMap->get< std::vector< std::string > > (
        "LastOpennedFiles", std::vector< std::string > ());
    // remove old actions
    for (size_t i = 0; i < fileactions.size (); i++) {
        filemenu->removeAction (fileactions[i].first);
    }
    fileactions.clear ();

    // sort nfiles such that multiples are left out
    std::vector< std::string > tmp;
    for (size_t i = 0; i < nfiles.size (); i++) {
        int idx   = (int) (nfiles.size () - 1 - i);
        bool skip = false;
        for (std::string& str : tmp) {
            if (str == nfiles[idx]) {
                skip = true;
                break;
            }
        }
        if (!skip)
            tmp.push_back (nfiles[idx]);
        if (tmp.size () > 10)
            break;
    }
    nfiles.resize (tmp.size ());

    // now add the new ones
    for (size_t i = 0; i < tmp.size (); i++) {
        nfiles[tmp.size () - 1 - i] = tmp[i];
        boost::filesystem::path p (tmp[i]);
        std::stringstream sstr;
        sstr << i << ": " << p.filename ();
        QAction* nAction = filemenu->addAction (sstr.str ().c_str ());

        connect (nAction, SIGNAL (triggered ()), this, SLOT (setCheckAction ()));
        filemenu->addAction (nAction);
        fileactions.push_back (std::make_pair (nAction, tmp[i]));
    }

    _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles", nfiles);
}

void RobWorkStudio::setupFileActions ()
{
    // 项目操作成为主文件工作流。旧的 WorkCell 操作仍然保留在菜单下半部分，便于
    // 调试单个资源和迁移历史文件，但工具栏只暴露项目级新建、打开和保存。
    QAction* newAction =
        new QAction (QIcon (":/images/new.png"), tr ("&New Project..."), this);    // owned
    connect (newAction, SIGNAL (triggered ()), this, SLOT (newProject ()));

    // 第四阶段新增：把历史 WorkCell 迁移为一个自包含的 rwproj 项目。
    QAction* migrateWorkCellAction =
        new QAction (tr ("Create Project from &WorkCell..."), this);    // owned
    connect (migrateWorkCellAction, SIGNAL (triggered ()), this, SLOT (createProjectFromWorkCell ()));

    // 第十一阶段新增：从 URDF/XML 机器人文件创建草稿项目，交由 RobotModelBuilder 导入。
    QAction* createRobotProjectAction =
        new QAction (tr ("Create Project from &Robot File..."), this);    // owned
    connect (createRobotProjectAction, SIGNAL (triggered ()), this,
             SLOT (createProjectFromRobotFile ()));

    QAction* openAction =
        new QAction (QIcon (":/images/open.png"), tr ("&Open Project..."), this);    // owned
    connect (openAction, SIGNAL (triggered ()), this, SLOT (openProject ()));

    QAction* closeAction =
        new QAction (QIcon (":/images/close.png"), tr ("&Close Project"), this);    // owned
    connect (closeAction, SIGNAL (triggered ()), this, SLOT (closeProject ()));

    QAction* saveAction =
        new QAction (QIcon (":/images/save.png"), tr ("&Save Project"), this);    // owned
    connect (saveAction, SIGNAL (triggered ()), this, SLOT (saveProject ()));

    // 第四阶段新增：项目级“另存为”（克隆到新目录）与历史资源导入入口。
    QAction* saveAsAction = new QAction (tr ("Save Project &As..."), this);    // owned
    connect (saveAsAction, SIGNAL (triggered ()), this, SLOT (saveProjectAs ()));

    QAction* importResourceAction = new QAction (tr ("&Import Project Resource..."), this);    // owned
    connect (importResourceAction, SIGNAL (triggered ()), this, SLOT (importProjectResource ()));

    QAction* exportPackageAction = new QAction (tr ("Export Project &Package..."), this);    // owned
    connect (exportPackageAction, SIGNAL (triggered ()), this, SLOT (exportProjectPackage ()));

    QAction* importPackageAction = new QAction (tr ("Import Project Pac&kage..."), this);    // owned
    connect (importPackageAction, SIGNAL (triggered ()), this, SLOT (importProjectPackage ()));

    QAction* integrityAction = new QAction (tr ("Check Project &Integrity..."), this);    // owned
    connect (integrityAction, SIGNAL (triggered ()), this, SLOT (inspectProjectIntegrity ()));

    QAction* openResourceAction = new QAction (tr ("Open Single &Resource..."), this);    // owned
    connect (openResourceAction, SIGNAL (triggered ()), this, SLOT (open ()));

    QAction* newWorkCellAction = new QAction (tr ("New Empty &WorkCell"), this);    // owned
    connect (newWorkCellAction, SIGNAL (triggered ()), this, SLOT (newWorkCell ()));

    QAction* saveWorkCellAction = new QAction (tr ("Save WorkCell &As..."), this);    // owned
    connect (saveWorkCellAction, SIGNAL (triggered ()), this, SLOT (saveWorkCell ()));

    QAction* reloadAction =
        new QAction (QIcon (":/images/reload.png"), tr ("&Reload"), this);    // owned
    reloadAction->setShortcut (Qt::Key_F5);
    connect (reloadAction, SIGNAL (triggered ()), this, SLOT (reloadWorkCell ()));

    QAction* exitAction = new QAction (QIcon (), tr ("&Exit"), this);    // owned
    connect (exitAction, SIGNAL (triggered ()), this, SLOT (close ()));

    QToolBar* fileToolBar = addToolBar (tr ("File"));
    fileToolBar->setObjectName ("FileToolBar");
    fileToolBar->addAction (newAction);
    fileToolBar->addAction (openAction);
    fileToolBar->addAction (closeAction);
    fileToolBar->addAction (saveAction);
    fileToolBar->addAction (reloadAction);
    ////
    _fileMenu = menuBar ()->addMenu (tr ("&File"));
    _fileMenu->addAction (newAction);
    _fileMenu->addAction (createRobotProjectAction);
    _fileMenu->addAction (migrateWorkCellAction);
    _fileMenu->addAction (openAction);
    _fileMenu->addAction (closeAction);
    _fileMenu->addAction (saveAction);
    _fileMenu->addAction (saveAsAction);
    _fileMenu->addAction (importResourceAction);
    _fileMenu->addAction (exportPackageAction);
    _fileMenu->addAction (importPackageAction);
    _fileMenu->addAction (integrityAction);
    _fileMenu->addAction (reloadAction);
    _fileMenu->addSeparator ();

    _fileMenu->addAction (openResourceAction);
    _fileMenu->addAction (newWorkCellAction);
    _fileMenu->addAction (saveWorkCellAction);
    _fileMenu->addSeparator ();

    QAction* propertyAction = new QAction (tr ("&Preferences"), this);    // owned
    connect (propertyAction, SIGNAL (triggered ()), this, SLOT (showPropertyEditor ()));

    _fileMenu->addAction (propertyAction);

    _fileMenu->addSeparator ();

    _fileMenu->addAction (exitAction);

    _fileMenu->addSeparator ();
    updateLastFiles ();
}

void RobWorkStudio::setupToolActions ()
{
    QAction* printCollisionsAction =
        new QAction (QIcon (""), tr ("Print Colliding Frames"), this);    // owned
    connect (printCollisionsAction, SIGNAL (triggered ()), this, SLOT (printCollisions ()));

    _toolMenu = menuBar ()->addMenu (tr ("&Tools"));
    _toolMenu->addAction (printCollisionsAction);
}

void RobWorkStudio::printCollisions ()
{
    CollisionDetector::Ptr cd = getCollisionDetector ();
    CollisionDetector::QueryResult res;
    cd->inCollision (getState (), &res);
    if (res.collidingFrames.size () > 0) {
        for (const FramePair& pair : res.collidingFrames) {
            std::cout << "Colliding: " << pair.first->getName () << " -- "
                      << pair.second->getName () << std::endl;
            Log::infoLog () << "Colliding: " << pair.first->getName () << " -- "
                            << pair.second->getName () << std::endl;
        }
    }
}

void RobWorkStudio::setCheckAction ()
{
    QObject* obj = sender ();

    // check if any of the open last file actions where choosen
    for (size_t i = 0; i < _lastFilesActions.size (); i++) {
        if (obj == _lastFilesActions[i].first) {
            openFile (_lastFilesActions[i].second);
            break;
        }
    }
}

void RobWorkStudio::showPropertyEditor ()
{
    // start property editor
    _propEditor->update ();
    _propEditor->show ();
    _propEditor->resize (400, 600);
}

void RobWorkStudio::setupPluginsMenu (bool create)
{
    QAction* loadPluginAction = new QAction (QIcon (""), tr ("Load plugin"), this);
    connect (loadPluginAction, SIGNAL (triggered ()), this, SLOT (loadPlugin ()));

    QAction* removePluginAction = new QAction (QIcon (""), tr ("Unload plugin"), this);
    connect (removePluginAction, SIGNAL (triggered ()), this, SLOT (unloadPlugin ()));

    if (_pluginsMenu == nullptr) {
        create = true;
    }

    if (create) {
        _pluginsMenu = menuBar ()->addMenu (tr ("&Plugins"));
    }
    else {
        _pluginsMenu->clear ();
    }
    _pluginsMenu->addAction (loadPluginAction);
    _pluginsMenu->addAction (removePluginAction);
    _pluginsMenu->addSeparator ();

    if (create) {
        _pluginsToolBar = addToolBar (tr ("Plugins"));
        _pluginsToolBar->setObjectName ("PluginsBar");
    }
    else {
        _pluginsToolBar->clear ();
    }
}

void RobWorkStudio::loadPlugin (std::string pluginFile, bool visible, int dock)
{
    if (boost::filesystem::exists (pluginFile)) {
        setupPlugin (pluginFile.c_str (), visible, dock);
    }
}

void RobWorkStudio::loadPlugin ()
{
    QString selectedFilter;

    std::string previousOpenDirectory =
        _settingsMap->get< std::string > ("PreviousOpenDirectory", "");
    const QString dir (previousOpenDirectory.c_str ());

    QString pluginfilename =
        QFileDialog::getOpenFileName (this,
                                      "Open plugin file",    // Title
                                      dir,                   // Directory
                                      "Plugin libraries ( *.so *.dll *.dylib *.so.* *.py)"
                                      "\n All ( *.* )",
                                      &selectedFilter);

    if (!pluginfilename.isEmpty ()) {
        QFileInfo pluginInfo (pluginfilename);
        QString pathname = pluginInfo.absolutePath ();
        QString filename = pluginInfo.baseName ();

        setupPlugin (pathname, filename, 0, 1);
    }
}

void RobWorkStudio::unloadPlugin ()
{
    QStringList list;
    for (RobWorkStudioPlugin* pl : _plugins) {
        list.append (pl->name ());
    }

    bool ok;
    QString text = QInputDialog::getItem (
        this, tr ("Unload plugin"), tr ("Which Plugin should be removed"), list, 0, false, &ok);

    if (ok) {
        std::cout << "OK: " << text.toStdString () << std::endl;
        for (RobWorkStudioPlugin* pl : _plugins) {
            if (pl->name () == text) {
                bool test = unloadPlugin (pl);
                std::cout << "test: " << test << std::endl;
                break;
            }
        }
    }
}

bool RobWorkStudio::unloadPlugin (RobWorkStudioPlugin* pl)
{
    removeDockWidget (pl);
    setupPluginsMenu (false);
    int remove = -1;
    for (size_t i = 0; i < _plugins.size (); i++) {
        if (_plugins[i] == pl) {
            remove = i;
        }
        else {
            _plugins[i]->setupMenu (_pluginsMenu);
            _plugins[i]->setupToolBar (_pluginsToolBar);
        }
    }
    if (remove < 0) {
        return false;
    }
    _plugins.erase (_plugins.begin () + remove);
    _plugins_loaded[_plugin2fileName[pl->name ().toStdString ()]] = false;
    return true;
}

void RobWorkStudio::setupHelpMenu ()
{
    QAction* assistantAct = new QAction (tr ("Help Contents"), this);
    assistantAct->setShortcut (QKeySequence::HelpContents);
    connect (assistantAct, SIGNAL (triggered ()), this, SLOT (showDocumentation ()));

    QAction* showAboutBox = new QAction ("About", this);
    connect (showAboutBox, SIGNAL (triggered ()), this, SLOT (showAboutBox ()));

    QMenu* pHelpMenu = menuBar ()->addMenu (tr ("Help"));
    pHelpMenu->addAction (assistantAct);
    pHelpMenu->addAction (showAboutBox);
}

void RobWorkStudio::keyPressEvent (QKeyEvent* e)
{
    keyEvent ().fire (e->key (), e->modifiers ());
    QWidget::keyPressEvent (e);
}

void RobWorkStudio::showAboutBox ()
{
    _aboutBox->exec ();
}

void RobWorkStudio::showDocumentation ()
{
    QStringList filepaths;
    // std::cout << QCoreApplication::applicationFilePath().toStdString() << std::endl;
    filepaths.append (QCoreApplication::applicationDirPath ());

    _assistant->showDocumentation (filepaths);
}

void RobWorkStudio::setupViewGL ()
{
    _view = new RWStudioView3D (this, this);
    setCentralWidget (_view);    // own view
    _view->setupGUI (this);
}

void RobWorkStudio::openAllPlugins ()
{
    typedef std::vector< RobWorkStudioPlugin* >::iterator I;
    for (I p = _plugins.begin (); p != _plugins.end (); ++p) {
        // RW_WARN( (*p)->name().toStdString() << "4");
        openPlugin (**p);
    }
}

void RobWorkStudio::closeAllPlugins ()
{
    typedef std::vector< RobWorkStudioPlugin* >::iterator PI;
    for (PI p = _plugins.begin (); p != _plugins.end (); ++p)
        closePlugin (**p);
}

void RobWorkStudio::openPlugin (RobWorkStudioPlugin& plugin)
{
    RW_ASSERT (_workcell);
    try {
        plugin.open (_workcell.get ());
    }
    catch (rw::core::Exception& exc) {
        std::stringstream buf;
        buf << "Exception in opening of plugin "
            << StringUtil::quote (plugin.name ().toStdString ());
#if !defined(RW_MACOS)
        QMessageBox::information (
            NULL, buf.str ().c_str (), exc.getMessage ().getText ().c_str (), QMessageBox::Ok);
#else
        this->log ().info () << buf.str () << std::endl
                             << " With message: " << exc.getMessage ().getText () << std::endl;
#endif
    }
    catch (...) {
        std::stringstream buf;
        buf << "Exception in opening of plugin "
            << StringUtil::quote (plugin.name ().toStdString ());

#if !defined(RW_MACOS)
        QMessageBox::information (NULL, buf.str ().c_str (), "Unknown error", QMessageBox::Ok);
#else
        this->log ().info () << buf.str () << std::endl << " With message: " << std::endl;
#endif
    }
}

void RobWorkStudio::closePlugin (RobWorkStudioPlugin& plugin)
{
    try {
        plugin.close ();
    }
    catch (const Exception& exc) {
        std::stringstream buf;
        buf << "Exception in closing of plugin "
            << StringUtil::quote (plugin.name ().toStdString ());

        QMessageBox::information (
            NULL, buf.str ().c_str (), exc.getMessage ().getText ().c_str (), QMessageBox::Ok);
    }
}

void RobWorkStudio::addPlugin (RobWorkStudioPlugin* plugin, bool visible, Qt::DockWidgetArea area)
{
    plugin->setLog (_robwork->getLogPtr ());
    plugin->setRobWorkStudio (this);
    plugin->setRobWorkInstance (_robwork);
    plugin->setupMenu (_pluginsMenu);
    plugin->setupToolBar (_pluginsToolBar);

    plugin->initialize ();

    // The updateSignal does not EXIST on the plugin interface....
    // connect(plugin, SIGNAL(updateSignal()), this, SLOT(updateHandler()));

    _plugins.push_back (plugin);
    std::string pname = plugin->name ().toStdString ();
    bool isVisible    = _settingsMap->get< bool > (std::string ("PluginVisible_") + pname, visible);
    bool isFloating   = _settingsMap->get< bool > (std::string ("PluginFloating_") + pname, false);
    int intarea       = _settingsMap->get< int > (std::string ("PluginArea_") + pname, (int) area);

    addDockWidget ((Qt::DockWidgetArea) intarea, plugin);
    // addDockWidget(area, plugin);
    plugin->setFloating (isFloating);
    // IMPORTANT visibility must be set as the last thing....
    plugin->setVisible (isVisible);
    // Only open the plugin if the work cell is loaded.
    if (_workcell)
        openPlugin (*plugin);

    std::vector< int > state_vector =
        _settingsMap->get< std::vector< int > > ("QtMainWindowState", std::vector< int > ());
    if (state_vector.size () > 0) {
        QByteArray mainAppState ((int) state_vector.size (), 0);
        for (int i = 0; i < mainAppState.size (); i++) {
            mainAppState[i] = (char) state_vector[i];
        }
        this->restoreState (mainAppState);
    }
}

void RobWorkStudio::loadSettingsSetupPlugins (const std::string& file)
{
    QSettings settings (file.c_str (), QSettings::IniFormat);
    switch (settings.status ()) {
        case QSettings::NoError: setupPlugins (settings); break;

        case QSettings::FormatError: {
            std::string msg = file + " file not loaded";
            QMessageBox::information (NULL, "Format error", msg.c_str (), QMessageBox::Ok);
        } break;

        case QSettings::AccessError:
            // Nothing to report here.
            break;
    }

    // TODO: make error reply if necessary
    // return settings.status();
}

void RobWorkStudio::setupPlugin (const QString& pathname, const QString& filename, bool visible,
                                 int dock)
{
    QString pfilename = pathname + "/" + filename + "." + OS::getDLLExtension ().c_str ();
    bool e1           = boost::filesystem::exists (pfilename.toStdString ());
    bool py           = false;
    if (!e1) {
        pfilename = pathname + "/" + filename;
        e1        = boost::filesystem::exists (pfilename.toStdString ());
    }
    if (!e1) {
        pfilename = pathname + "/" + filename + ".so";
        e1        = boost::filesystem::exists (pfilename.toStdString ());
    }
    if (!e1) {
        pfilename = pathname + "/" + filename + ".dll";
        e1        = boost::filesystem::exists (pfilename.toStdString ());
    }
    if (!e1) {
        pfilename = pathname + "/" + filename + ".dylib";
        e1        = boost::filesystem::exists (pfilename.toStdString ());
    }
    if (!e1) {
        pfilename = pathname + "/" + filename + ".py";
        e1        = boost::filesystem::exists (pfilename.toStdString ());
        py        = e1;
    }

    if (_plugins_loaded[filename.toStdString ()]) {
        RW_THROW ("Plugin \"" << filename.toStdString () << "\" has already been loaded");
    }

    if (py) {
        setupPyPlugin (pfilename, filename, visible, dock);
    }
    else {
        setupPlugin (pfilename, visible, dock);
    }
}

void RobWorkStudio::setupPlugin (const QString& fullname, bool visible, int dock)
{
    const boost::filesystem::path pluginPath (fullname.toStdString ());
    std::string ext  = pluginPath.extension ().string ();
    std::string base = pluginPath.stem ().string ();
    if (ext == "py" || ext == ".py") {
        setupPyPlugin (fullname, base.c_str (), visible, dock);
        return;
    }
    else if (!_plugins_loaded[base]) {
        Qt::DockWidgetArea dockarea = (Qt::DockWidgetArea) dock;
        QPluginLoader loader (fullname);

        // Needed to make dynamicly loaded libraries use dynamic
        // cast on each others objects. ONLY on linux though.
        loader.setLoadHints (QLibrary::ResolveAllSymbolsHint | QLibrary::ExportExternalSymbolsHint);

        QObject* pluginObject = loader.instance ();
        if (pluginObject != NULL) {
            RobWorkStudioPlugin* testP = dynamic_cast< RobWorkStudioPlugin* > (pluginObject);
            if (testP == NULL) {
                RW_THROW ("Loaded plugin is NULL, tried loading \"" << fullname.toStdString ()
                                                                    << "\"");
            }
            RobWorkStudioPlugin* plugin = qobject_cast< RobWorkStudioPlugin* > (pluginObject);
            _plugin2fileName[plugin->name ().toStdString ()] = base;
            if (plugin) {
                addPlugin (plugin, visible, dockarea);
            }
            else {
                RW_WARN ("Unable to load Plugin" << fullname.toStdString ()
                                                 << " was not of type RobWorkStudioPlugin");
                QMessageBox::information (this,
                                          "Unable to load Plugin",
                                          fullname + " was not of type RobWorkStudioPlugin",
                                          QMessageBox::Ok);
            }
        }
        else {
            RW_WARN ("Unable to load Plugin" << fullname.toStdString () << " was not loaded: \""
                                             << loader.errorString ().toStdString () + "\"");
            QMessageBox::information (this,
                                      "Unable to load Plugin",
                                      fullname + " was not loaded: \"" + loader.errorString () +
                                          "\"",
                                      QMessageBox::Ok);
        }
        _plugins_loaded[base] = true;
    }
}

void RobWorkStudio::setupPyPlugin (const QString& pathname, const QString& filename, bool visible,
                                   int dock)
{
#ifdef RWS_USE_PYTHON
    Qt::DockWidgetArea dockarea = (Qt::DockWidgetArea) dock;
    PyPlugin* pyplug            = new PyPlugin (filename, QIcon (":/PythonIcon.png"));
    addPlugin (pyplug, visible, dockarea);
    pyplug->initialize (pathname.toLocal8Bit ().data (), filename.toLocal8Bit ().data ());
#endif    // RWS_USE_PYTHON
#ifndef RWS_USE_PYTHON
    RW_THROW ("You have attempted to load a python plugin, but RobWorkStudio was not compiled "
              "using python, please recompile RobWorkStudio");
#endif    // NOT RWS_USE_PYTHON
}

void RobWorkStudio::setupPlugins (QSettings& settings)
{
    QStringList groups = settings.childGroups ();

    settings.beginGroup ("Plugins");
    QStringList plugins = settings.childGroups ();
    for (int i = 0; i < plugins.size (); i++) {
        const QString& pluginname = plugins.at (i);
        Log::debugLog () << "Plugin = " << pluginname.toStdString () << "\n";

        settings.beginGroup (pluginname);

        QString pathname = settings.value ("Path").toString ();
        QString filename = settings.value ("Filename").toString ();
        bool visible     = settings.value ("Visible").toBool ();
        // Qt::DockWidgetArea dockarea =
        //    (Qt::DockWidgetArea)settings.value("DockArea").toInt();
        int dock = settings.value ("DockArea").toInt ();

        setupPlugin (pathname, filename, visible, dock);
        settings.endGroup ();    // End Specific Plugin Group
    }
    settings.endGroup ();    // End the Plugins Group
}

std::string RobWorkStudio::loadSettingsWorkcell (const std::string& file)
{
    QSettings settings (file.c_str (), QSettings::IniFormat);
    std::string workcellPath = "";
    switch (settings.status ()) {
        case QSettings::NoError: {
            QStringList groups = settings.childGroups ();
            settings.beginGroup ("Settings");
            QStringList theSettings = settings.childGroups ();

            for (int i = 0; i < theSettings.size (); i++) {
                const QString& settingName = theSettings.at (i);
                Log::debugLog () << "SettingFound: " << settingName.toStdString () << "\n";

                if (settingName.toStdString () == "Workcell") {
                    settings.beginGroup ("Workcell");
                    workcellPath = settings.value ("Path").toString ().toStdString ();
                    break;
                }
            }

            break;
        }
        case QSettings::FormatError:
        case QSettings::AccessError:
            // Nothing to report here.
            break;
    }
    Log::debugLog () << "workcellFound: " << workcellPath << "\n";
    return workcellPath;
}

bool rws::resolveNewRobotProjectBuilderCallbacks (
    const std::vector< RobWorkStudioPlugin* >& plugins,
    std::function< bool (QString*) > confirmClose,
    NewRobotProjectCallbacks& callbacks,
    RobWorkStudioPlugin*& builder,
    QString* error)
{
    callbacks = NewRobotProjectCallbacks {};
    builder = NULL;
    RobWorkStudioPlugin* candidate = NULL;
    for (RobWorkStudioPlugin* plugin : plugins) {
        if (plugin != NULL && plugin->name () == QStringLiteral ("RobotModelBuilder")) {
            candidate = plugin;
            break;
        }
    }
    if (candidate == NULL) {
        if (error != nullptr)
            *error = QStringLiteral ("RobotModelBuilder is not loaded.");
        return false;
    }
    const auto hasMetaOperation = [candidate] (const char* signature, int returnType) {
        if (candidate == NULL)
            return false;
        const int index = candidate->metaObject ()->indexOfMethod (
            QMetaObject::normalizedSignature (signature));
        return index >= 0 &&
            candidate->metaObject ()->method (index).returnMetaType ().id () == returnType;
    };
    if (!hasMetaOperation ("preflightNewRobotProject(QString)", QMetaType::QString) ||
        !hasMetaOperation ("newRobotProjectResource(QString)", QMetaType::QVariantMap) ||
        !hasMetaOperation ("snapshotNewRobotProjectState()", QMetaType::QVariantMap) ||
        !hasMetaOperation ("restoreNewRobotProjectState(QByteArray)", QMetaType::QString) ||
        !hasMetaOperation ("bootstrapNewRobotProject(QString)", QMetaType::QString)) {
        if (error != nullptr)
            *error = QStringLiteral ("The loaded RobotModelBuilder is incompatible with New Project.");
        return false;
    }
    if (!confirmClose) {
        if (error != nullptr)
            *error = QStringLiteral ("New Project close confirmation is unavailable.");
        return false;
    }
    builder = candidate;

    const auto setInvocationError = [] (QString* error, const QString& message) {
        if (error != nullptr)
            *error = message;
    };
    const auto hasVariantType = [] (const QVariantMap& map, const QString& key,
                                    int typeId) {
        const auto value = map.constFind (key);
        return value != map.constEnd () && value->metaType ().id () == typeId;
    };

    callbacks.preflight = [builder, setInvocationError] (const QString& projectRoot,
                                                         QString* error) {
        QString result;
        if (!QMetaObject::invokeMethod (
                builder, "preflightNewRobotProject", Qt::DirectConnection,
                Q_RETURN_ARG (QString, result), Q_ARG (QString, projectRoot))) {
            setInvocationError (
                error, QStringLiteral ("RobotModelBuilder New Project preflight invocation failed."));
            return false;
        }
        if (error != nullptr)
            *error = result;
        return result.isEmpty ();
    };
    callbacks.requiredResources =
        [builder, setInvocationError, hasVariantType] (
            const QString& projectRoot, QVector< ProjectResource >& resources,
            QString* error) {
            QVariantMap declaration;
            if (!QMetaObject::invokeMethod (
                    builder, "newRobotProjectResource", Qt::DirectConnection,
                    Q_RETURN_ARG (QVariantMap, declaration),
                    Q_ARG (QString, projectRoot))) {
                setInvocationError (
                    error,
                    QStringLiteral ("RobotModelBuilder resource declaration invocation failed."));
                return false;
            }
            if (!hasVariantType (declaration, QStringLiteral ("success"), QMetaType::Bool) ||
                !hasVariantType (declaration, QStringLiteral ("error"), QMetaType::QString)) {
                setInvocationError (
                    error, QStringLiteral ("RobotModelBuilder returned a malformed resource result."));
                return false;
            }
            const bool success = declaration.value (QStringLiteral ("success")).toBool ();
            const QString declaredError = declaration.value (QStringLiteral ("error")).toString ();
            if (!success) {
                setInvocationError (
                    error, declaredError.isEmpty ()
                               ? QStringLiteral ("RobotModelBuilder resource declaration failed.")
                               : declaredError);
                return false;
            }
            const QSet< QString > expectedKeys = {
                QStringLiteral ("success"), QStringLiteral ("error"),
                QStringLiteral ("id"), QStringLiteral ("kind"),
                QStringLiteral ("path"), QStringLiteral ("ownership"),
                QStringLiteral ("required"), QStringLiteral ("dependencies")};
            const QSet< QString > actualKeys (declaration.keyBegin (), declaration.keyEnd ());
            if (!declaredError.isEmpty () || actualKeys != expectedKeys ||
                !hasVariantType (declaration, QStringLiteral ("id"), QMetaType::QString) ||
                !hasVariantType (declaration, QStringLiteral ("kind"), QMetaType::QString) ||
                !hasVariantType (declaration, QStringLiteral ("path"), QMetaType::QString) ||
                !hasVariantType (declaration, QStringLiteral ("ownership"), QMetaType::QString) ||
                !hasVariantType (declaration, QStringLiteral ("required"), QMetaType::Bool) ||
                !hasVariantType (
                    declaration, QStringLiteral ("dependencies"), QMetaType::QStringList)) {
                setInvocationError (
                    error, QStringLiteral ("RobotModelBuilder returned a malformed resource declaration."));
                return false;
            }

            ProjectResource resource;
            resource.id = declaration.value (QStringLiteral ("id")).toString ();
            resource.kind = declaration.value (QStringLiteral ("kind")).toString ();
            resource.path = declaration.value (QStringLiteral ("path")).toString ();
            resource.ownership = declaration.value (QStringLiteral ("ownership")).toString ();
            resource.required = declaration.value (QStringLiteral ("required")).toBool ();
            resource.dependencies =
                declaration.value (QStringLiteral ("dependencies")).toStringList ();
            resources.push_back (resource);
            if (error != nullptr)
                error->clear ();
            return true;
        };
    callbacks.snapshotState =
        [builder, setInvocationError, hasVariantType] (QByteArray& snapshot, QString* error) {
            QVariantMap result;
            if (!QMetaObject::invokeMethod (
                    builder, "snapshotNewRobotProjectState", Qt::DirectConnection,
                    Q_RETURN_ARG (QVariantMap, result))) {
                setInvocationError (
                    error, QStringLiteral ("RobotModelBuilder state snapshot invocation failed."));
                return false;
            }
            const QSet< QString > expectedKeys = {
                QStringLiteral ("success"), QStringLiteral ("error"),
                QStringLiteral ("snapshot")};
            const QSet< QString > actualKeys (result.keyBegin (), result.keyEnd ());
            if (actualKeys != expectedKeys ||
                !hasVariantType (result, QStringLiteral ("success"), QMetaType::Bool) ||
                !hasVariantType (result, QStringLiteral ("error"), QMetaType::QString) ||
                !hasVariantType (result, QStringLiteral ("snapshot"), QMetaType::QByteArray)) {
                setInvocationError (
                    error, QStringLiteral ("RobotModelBuilder returned a malformed state snapshot."));
                return false;
            }
            const bool success = result.value (QStringLiteral ("success")).toBool ();
            const QString snapshotError = result.value (QStringLiteral ("error")).toString ();
            snapshot = result.value (QStringLiteral ("snapshot")).toByteArray ();
            if (!success || !snapshotError.isEmpty () || snapshot.isEmpty ()) {
                setInvocationError (
                    error, snapshotError.isEmpty ()
                               ? QStringLiteral ("RobotModelBuilder state snapshot failed.")
                               : snapshotError);
                return false;
            }
            if (error != nullptr)
                error->clear ();
            return true;
        };
    callbacks.restoreState = [builder, setInvocationError] (const QByteArray& snapshot,
                                                            QString* error) {
        QString result;
        if (!QMetaObject::invokeMethod (
                builder, "restoreNewRobotProjectState", Qt::DirectConnection,
                Q_RETURN_ARG (QString, result), Q_ARG (QByteArray, snapshot))) {
            setInvocationError (
                error, QStringLiteral ("RobotModelBuilder state restore invocation failed."));
            return false;
        }
        if (error != nullptr)
            *error = result;
        return result.isEmpty ();
    };
    callbacks.bootstrap = [builder, setInvocationError] (const QString& projectRoot,
                                                         QString* error) {
        QString result;
        if (!QMetaObject::invokeMethod (
                builder, "bootstrapNewRobotProject", Qt::DirectConnection,
                Q_RETURN_ARG (QString, result), Q_ARG (QString, projectRoot))) {
            setInvocationError (
                error, QStringLiteral ("RobotModelBuilder New Project bootstrap invocation failed."));
            return false;
        }
        if (error != nullptr)
            *error = result;
        return result.isEmpty ();
    };
    callbacks.confirmClose = std::move (confirmClose);
    if (error != nullptr)
        error->clear ();
    return true;
}

void RobWorkStudio::newProject ()
{
    NewRobotProjectCallbacks callbacks;
    RobWorkStudioPlugin* builder = NULL;
    QString error;
    if (!resolveNewRobotProjectBuilderCallbacks (
            getPlugins (),
            [this] (QString* closeError) {
                const bool confirmed = confirmProjectClose ();
                if (!confirmed && closeError != nullptr)
                    closeError->clear ();
                return confirmed;
            },
            callbacks, builder, &error)) {
        QMessageBox::warning (this, tr ("RobotModelBuilder Unavailable"), error);
        return;
    }

    const QString previousDirectory = QString::fromStdString (
        _settingsMap->get< std::string > ("PreviousOpenDirectory", ""));
    QString projectFile = QFileDialog::getSaveFileName (this,
                                                        tr ("New RobWorkStudio Project"),
                                                        previousDirectory,
                                                        tr ("RobWorkStudio Project (*.rwproj)"));
    if (projectFile.isEmpty ())
        return;
    if (!projectFile.endsWith (QStringLiteral (".rwproj"), Qt::CaseInsensitive))
        projectFile += QStringLiteral (".rwproj");

    error.clear ();
    if (!createProjectWithRobotModelBuilderPaths (projectFile, callbacks, &error)) {
        if (!error.isEmpty ())
            QMessageBox::critical (this, tr ("Create Project Failed"), error);
        return;
    }
    if (!builder->isVisible ())
        builder->showPlugin ();
}

bool RobWorkStudio::createProjectFromRobotFilePaths (
    const QString& sourcePath,
    const QString& projectFile,
    const RobotProjectImportCallbacks& callbacks,
    QString* error)
{
    if (error != nullptr)
        error->clear ();
    if (sourcePath.trimmed ().isEmpty () || projectFile.trimmed ().isEmpty ()) {
        if (error != nullptr)
            *error = QStringLiteral ("Robot source and target project paths are required.");
        return false;
    }
    if (!callbacks.preflight || !callbacks.commit) {
        if (error != nullptr)
            *error = QStringLiteral ("Robot project import callbacks are incomplete.");
        return false;
    }

    const QString absoluteProjectFile = QFileInfo (projectFile).absoluteFilePath ();
    const QString projectRoot = QFileInfo (absoluteProjectFile).absolutePath ();
    if (!callbacks.preflight (sourcePath, projectRoot, error)) {
        if (error != nullptr && error->isEmpty ())
            *error = QStringLiteral ("The robot source failed RobotModelBuilder preflight.");
        return false;
    }

    PreparedRobotProject prepared;
    if (!_projectManager.prepareProjectFromRobotFile (
            absoluteProjectFile, sourcePath, prepared, error))
        return false;

    const QString managedSourceProjectPath = prepared.packaged.sourceResource.path;
    const QString stagedManagedSource =
        prepared.packaged.stagedFilesByProjectPath.value (managedSourceProjectPath);
    if (stagedManagedSource.isEmpty () ||
        !callbacks.preflight (stagedManagedSource, projectRoot, error)) {
        ProjectManager::discardPreparedRobotProject (prepared);
        if (error != nullptr && error->isEmpty ())
            *error = QStringLiteral ("The managed robot source could not be preflighted.");
        return false;
    }

    if (callbacks.confirmClose && !callbacks.confirmClose (error)) {
        ProjectManager::discardPreparedRobotProject (prepared);
        return false;
    }
    if (!_projectManager.activatePreparedRobotProject (prepared, error)) {
        ProjectManager::discardPreparedRobotProject (prepared);
        return false;
    }

    const QString previousProjectFile = prepared.previousProjectFilePath;
    const ProjectManifest previousManifest = prepared.previousManifest;
    const auto restorePreviousProject = [&] (const QString& failure) {
        _projectDocuments.closeResources ();

        QString rollbackError;
        const bool rolledBack =
            _projectManager.rollbackActivatedRobotProject (prepared, &rollbackError);

        QString reopenError;
        bool reopened = true;
        if (previousProjectFile.isEmpty ()) {
            createEmptyWorkCell ();
        }
        else {
            reopened = _projectDocuments.loadProjectResources (
                previousManifest, previousProjectFile, &reopenError);
            if (reopened && previousManifest.entryPoints.value (
                                QStringLiteral ("mainWorkCell")).isEmpty ())
                createEmptyWorkCell ();
            else if (!reopened)
                createEmptyWorkCell ();
        }
        updateProjectWindowTitle ();

        QStringList details;
        if (!failure.isEmpty ())
            details.push_back (failure);
        if (!rolledBack)
            details.push_back (QStringLiteral ("Candidate cleanup failed: %1").arg (rollbackError));
        if (!reopened)
            details.push_back (QStringLiteral ("Previous project resources could not be reopened: %1")
                                   .arg (reopenError));
        if (error != nullptr)
            *error = details.join (QLatin1Char ('\n'));
        return false;
    };

    _projectDocuments.closeResources ();
    closeWorkCell ();
    QString registryError;
    if (!_projectDocuments.loadProjectResources (
            prepared.manifest, prepared.projectFilePath, &registryError)) {
        return restorePreviousProject (
            QStringLiteral ("The candidate robot project resources could not be loaded: %1")
                .arg (registryError));
    }
    if (prepared.manifest.entryPoints.value (
            QStringLiteral ("mainWorkCell")).isEmpty ())
        createEmptyWorkCell ();

    const QString managedSource =
        QDir (projectRoot).filePath (managedSourceProjectPath);
    QString commitError;
    if (!callbacks.commit (QDir::cleanPath (managedSource), projectRoot, &commitError)) {
        if (commitError.isEmpty ())
            commitError = QStringLiteral ("RobotModelBuilder could not commit the managed robot source.");
        return restorePreviousProject (commitError);
    }

    _settingsMap->set< std::string > ("PreviousOpenDirectory",
                                      projectRoot.toStdString ());
    std::vector< std::string > recent = _settingsMap->get< std::vector< std::string > > (
        "LastOpennedFiles", std::vector< std::string > ());
    recent.push_back (absoluteProjectFile.toStdString ());
    _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles", recent);
    updateLastFiles ();
    updateProjectWindowTitle ();
    prepared = PreparedRobotProject {};
    return true;
}

bool RobWorkStudio::createProjectWithRobotModelBuilderPaths (
    const QString& projectFile,
    const NewRobotProjectCallbacks& callbacks,
    QString* error)
{
    if (error != nullptr)
        error->clear ();
    const QString previousWindowTitle = windowTitle ();
    const auto invokeCallback = [] (const QString& stage, auto&& callback,
                                    QString* callbackError) {
        try {
            return callback ();
        }
        catch (const std::exception& exception) {
            if (callbackError != nullptr) {
                *callbackError =
                    QStringLiteral ("%1 callback raised an exception: %2")
                        .arg (stage, QString::fromLocal8Bit (exception.what ()));
            }
            return false;
        }
        catch (...) {
            if (callbackError != nullptr) {
                *callbackError =
                    QStringLiteral ("%1 callback raised an unknown exception.").arg (stage);
            }
            return false;
        }
    };
    if (!callbacks.preflight || !callbacks.requiredResources || !callbacks.snapshotState ||
        !callbacks.restoreState || !callbacks.bootstrap || !callbacks.confirmClose) {
        if (error != nullptr)
            *error = QStringLiteral ("New robot project callbacks are incomplete.");
        return false;
    }
    if (projectFile.trimmed ().isEmpty ()) {
        if (error != nullptr)
            *error = QStringLiteral ("The target project path is required.");
        return false;
    }

    const QString absoluteProjectFile = QFileInfo (projectFile).absoluteFilePath ();
    const QString projectRoot = QFileInfo (absoluteProjectFile).absolutePath ();
    const QFileInfo projectFileInfo (absoluteProjectFile);
    const bool unsafeProjectFile =
        ProjectPathResolver::isLinkOrReparsePoint (absoluteProjectFile);
    if (projectFileInfo.fileName ().trimmed ().isEmpty () || unsafeProjectFile ||
        projectFileInfo.exists ()) {
        if (error != nullptr)
            *error = unsafeProjectFile || projectFileInfo.exists ()
                         ? QStringLiteral ("The target project file already exists.")
                         : QStringLiteral ("The target project path is invalid.");
        return false;
    }

    QStringList missingProjectDirectories;
    QString existingAncestor = projectRoot;
    bool unsafeAncestor = false;
    while (true) {
        if (ProjectPathResolver::isLinkOrReparsePoint (existingAncestor)) {
            unsafeAncestor = true;
            break;
        }
        if (QFileInfo::exists (existingAncestor))
            break;
        missingProjectDirectories.push_back (existingAncestor);
        const QString parent = QFileInfo (existingAncestor).absolutePath ();
        if (parent == existingAncestor)
            break;
        existingAncestor = parent;
    }
    const QFileInfo ancestorInfo (existingAncestor);
    const QFileInfo rootInfo (projectRoot);
    QString ancestorPath = existingAncestor;
    while (!unsafeAncestor) {
        if (ProjectPathResolver::isLinkOrReparsePoint (ancestorPath)) {
            unsafeAncestor = true;
            break;
        }
        if (!QFileInfo::exists (ancestorPath))
            break;
        const QString parent = QFileInfo (ancestorPath).absolutePath ();
        if (parent == ancestorPath)
            break;
        ancestorPath = parent;
    }
    if (unsafeAncestor || ProjectPathResolver::isLinkOrReparsePoint (projectRoot) ||
        !ancestorInfo.exists () || !ancestorInfo.isDir () || !ancestorInfo.isWritable () ||
        (rootInfo.exists () && !rootInfo.isDir ())) {
        if (error != nullptr)
            *error = QStringLiteral ("The target project parent is unavailable or unsafe: %1")
                         .arg (existingAncestor);
        return false;
    }

    if (!invokeCallback (QStringLiteral ("RobotModelBuilder preflight"),
                         [&] { return callbacks.preflight (projectRoot, error); },
                         error)) {
        if (error != nullptr && error->isEmpty ())
            *error = QStringLiteral ("RobotModelBuilder project preflight failed.");
        return false;
    }

    QVector< ProjectResource > candidateResources;
    if (!invokeCallback (
            QStringLiteral ("RobotModelBuilder requiredResources"),
            [&] { return callbacks.requiredResources (projectRoot, candidateResources, error); },
            error)) {
        if (error != nullptr && error->isEmpty ())
            *error = QStringLiteral ("RobotModelBuilder resource preflight failed.");
        return false;
    }
    if (candidateResources.isEmpty ()) {
        if (error != nullptr)
            *error = QStringLiteral ("RobotModelBuilder must declare its generated project resources.");
        return false;
    }

    const ProjectResource* declaredRobotModel = nullptr;
    int robotModelDeclarationCount = 0;
    for (const ProjectResource& resource : candidateResources) {
        if (resource.id == QStringLiteral ("robot-model.main")) {
            declaredRobotModel = &resource;
            ++robotModelDeclarationCount;
        }
    }
    if (robotModelDeclarationCount != 1) {
        if (error != nullptr)
            *error = QStringLiteral (
                "RobotModelBuilder must declare robot-model.main exactly once.");
        return false;
    }
    const QString normalizedModelPath = QDir::cleanPath (
        QDir::fromNativeSeparators (declaredRobotModel->path));
    if (declaredRobotModel->kind != QStringLiteral ("robwork.robot-model") ||
        declaredRobotModel->ownership != QStringLiteral ("generated") ||
        !declaredRobotModel->required || !declaredRobotModel->dependencies.isEmpty () ||
        declaredRobotModel->path != normalizedModelPath ||
        !normalizedModelPath.startsWith (QStringLiteral ("generated/robot-models/")) ||
        !normalizedModelPath.endsWith (QStringLiteral (".rmb.json")) ||
        QDir::isAbsolutePath (normalizedModelPath)) {
        if (error != nullptr) {
            *error = QStringLiteral (
                "robot-model.main must be a required generated robwork.robot-model with no "
                "dependencies and a normalized generated/robot-models/*.rmb.json path.");
        }
        return false;
    }

    struct CandidateGeneratedOutput
    {
        QString path;
        QStringList createdDirectories;
    };
    QVector< CandidateGeneratedOutput > generatedOutputs;
    for (const ProjectResource& resource : candidateResources) {
        const QString normalizedResourcePath =
            QDir::cleanPath (QDir::fromNativeSeparators (resource.path));
        if (resource.path.isEmpty () || resource.path != normalizedResourcePath ||
            QDir::isAbsolutePath (normalizedResourcePath)) {
            if (error != nullptr) {
                *error = QStringLiteral (
                             "New RobotModelBuilder generated resources must use nonempty "
                             "normalized project-relative paths: %1")
                             .arg (resource.id);
            }
            return false;
        }
        if (resource.ownership != QStringLiteral ("generated")) {
            if (error != nullptr) {
                *error = QStringLiteral (
                             "New RobotModelBuilder project resources must use generated ownership: %1")
                             .arg (resource.id);
            }
            return false;
        }
        QString resourcePath;
        if (!ProjectPathResolver::resolveResource (
                absoluteProjectFile, resource, resourcePath, error))
            return false;

        CandidateGeneratedOutput output;
        output.path = resourcePath;
        const QFileInfo currentRootInfo (projectRoot);
        if (ProjectPathResolver::isLinkOrReparsePoint (projectRoot) ||
            (currentRootInfo.exists () &&
            !ProjectPathResolver::validateContainedWritePath (
                projectRoot, resourcePath, error)))
            return false;
        const QFileInfo outputInfo (resourcePath);
        const bool unsafeOutput =
            ProjectPathResolver::isLinkOrReparsePoint (resourcePath);
        if (unsafeOutput || outputInfo.exists ()) {
            if (unsafeOutput || !outputInfo.isFile ()) {
                if (error != nullptr)
                    *error = QStringLiteral ("The declared generated output cannot be backed up safely: %1")
                                 .arg (resourcePath);
                return false;
            }
        }
        QString directory = QFileInfo (resourcePath).absolutePath ();
        const QString cleanRoot = QDir::cleanPath (QDir::fromNativeSeparators (projectRoot));
        while (QDir::cleanPath (QDir::fromNativeSeparators (directory)) != cleanRoot) {
            const QString cleanDirectory =
                QDir::cleanPath (QDir::fromNativeSeparators (directory));
            if (directory.isEmpty () || !cleanDirectory.startsWith (cleanRoot + QLatin1Char ('/')))
                break;
            if (ProjectPathResolver::isLinkOrReparsePoint (directory)) {
                if (error != nullptr)
                    *error = QStringLiteral ("The declared generated output has an unsafe parent: %1")
                                 .arg (directory);
                return false;
            }
            if (!QFileInfo::exists (directory))
                output.createdDirectories.push_back (directory);
            const QString parent = QFileInfo (directory).absolutePath ();
            if (parent == directory)
                break;
            directory = parent;
        }
        generatedOutputs.push_back (output);
    }

    if (!_projectDocuments.validateCandidateResources (candidateResources, error))
        return false;

    struct CandidatePathInventory
    {
        QSet< QString > files;
        QSet< QString > directories;
        QSet< QString > unsafeEntries;
        QHash< QString, QByteArray > fileDigests;
        QHash< QString, qint64 > fileSizes;
        quint64 totalRegularBytes = 0;
    };
    const auto inventoryPath = [] (const QString& path) {
        return QDir::cleanPath (QFileInfo (path).absoluteFilePath ());
    };
    const auto isUnsafeInventoryEntry = [] (const QFileInfo& info) {
        return ProjectPathResolver::isLinkOrReparsePoint (info.absoluteFilePath ());
    };
    const auto fingerprintFile = [&] (const QString& path, QByteArray& digest,
                                      QString* fingerprintError) {
        const QFileInfo before (path);
        if (!before.isFile () || isUnsafeInventoryEntry (before)) {
            if (fingerprintError != nullptr)
                *fingerprintError =
                    QStringLiteral ("Project baseline entry is not an ordinary file: %1").arg (path);
            return false;
        }
        QFile file (path);
        if (!file.open (QIODevice::ReadOnly)) {
            if (fingerprintError != nullptr)
                *fingerprintError =
                    QStringLiteral ("Project baseline file could not be read: %1").arg (path);
            return false;
        }
        QCryptographicHash hash (QCryptographicHash::Sha256);
        while (!file.atEnd ()) {
            const QByteArray chunk = file.read (1024 * 1024);
            if (chunk.isEmpty () && file.error () != QFileDevice::NoError) {
                if (fingerprintError != nullptr)
                    *fingerprintError =
                        QStringLiteral ("Project baseline file could not be read: %1").arg (path);
                return false;
            }
            hash.addData (chunk);
        }
        const QFileInfo after (path);
        if (!after.isFile () || isUnsafeInventoryEntry (after) || before.size () != after.size () ||
            before.lastModified () != after.lastModified ()) {
            if (fingerprintError != nullptr)
                *fingerprintError =
                    QStringLiteral ("Project baseline changed while it was being read: %1").arg (path);
            return false;
        }
        digest = hash.result ();
        return true;
    };
    const auto captureProjectInventory = [&] (CandidatePathInventory& inventory,
                                               bool includeDigests,
                                               bool allowUnsafeEntries,
                                               bool enforceBaselineLimits,
                                               QString* inventoryError) {
        inventory = CandidatePathInventory {};
        const auto rejectLimit = [&] (const QString& detail) {
            if (inventoryError != nullptr) {
                *inventoryError =
                    QStringLiteral (
                        "Project baseline cannot be protected synchronously (%1). Choose a new or "
                        "empty project directory.")
                        .arg (detail);
            }
            return false;
        };
        const QFileInfo projectRootInfo (projectRoot);
        if (isUnsafeInventoryEntry (projectRootInfo)) {
            if (allowUnsafeEntries) {
                inventory.unsafeEntries.insert (inventoryPath (projectRoot));
                return true;
            }
            if (inventoryError != nullptr)
                *inventoryError =
                    QStringLiteral ("The project root changed into an unsafe entry: %1").arg (projectRoot);
            return false;
        }
        if (!projectRootInfo.exists ())
            return true;
        if (!projectRootInfo.isDir ()) {
            if (allowUnsafeEntries) {
                inventory.unsafeEntries.insert (inventoryPath (projectRoot));
                return true;
            }
            if (inventoryError != nullptr)
                *inventoryError =
                    QStringLiteral ("The project root changed into an unsafe entry: %1").arg (projectRoot);
            return false;
        }
        inventory.directories.insert (inventoryPath (projectRoot));
        int entryCount = 0;
        std::function< bool (const QString&) > visitDirectory;
        visitDirectory = [&] (const QString& directory) {
            QDirIterator entries (directory,
                                  QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                                      QDir::System);
            while (entries.hasNext ()) {
                const QFileInfo info (entries.next ());
                const QString path = inventoryPath (info.absoluteFilePath ());
                const QString relativePath = QDir::fromNativeSeparators (
                    QDir (projectRoot).relativeFilePath (path));
                const int depth = relativePath.split (QLatin1Char ('/'), Qt::SkipEmptyParts).size ();
                ++entryCount;
                if (entryCount > kMaxProjectBaselineEntries)
                    return rejectLimit (QStringLiteral ("more than %1 filesystem entries")
                                            .arg (kMaxProjectBaselineEntries));
                if (depth > kMaxProjectBaselineDepth)
                    return rejectLimit (QStringLiteral ("path depth exceeds %1")
                                            .arg (kMaxProjectBaselineDepth));
                if (relativePath.size () > kMaxProjectBaselineRelativePathLength)
                    return rejectLimit (QStringLiteral ("relative path length exceeds %1 characters")
                                            .arg (kMaxProjectBaselineRelativePathLength));
                if (isUnsafeInventoryEntry (info)) {
                    if (allowUnsafeEntries) {
                        inventory.unsafeEntries.insert (path);
                        continue;
                    }
                    if (inventoryError != nullptr)
                        *inventoryError =
                            QStringLiteral ("Project tree contains a link or reparse point: %1")
                                .arg (path);
                    return false;
                }
                QString containmentError;
                if (!ProjectPathResolver::validateContainedWritePath (
                        projectRoot, path, &containmentError)) {
                    if (inventoryError != nullptr)
                        *inventoryError = containmentError;
                    return false;
                }
                if (info.isDir ()) {
                    inventory.directories.insert (path);
                    if (!visitDirectory (path))
                        return false;
                }
                else if (info.isFile ()) {
                    const qint64 signedSize = info.size ();
                    if (signedSize < 0) {
                        if (inventoryError != nullptr)
                            *inventoryError = QStringLiteral (
                                "Project baseline file size is unavailable: %1")
                                                      .arg (path);
                        return false;
                    }
                    const quint64 fileSize = static_cast< quint64 > (signedSize);
                    if (fileSize > std::numeric_limits< quint64 >::max () -
                                       inventory.totalRegularBytes) {
                        return rejectLimit (QStringLiteral ("regular file byte total overflowed"));
                    }
                    inventory.files.insert (path);
                    inventory.fileSizes.insert (path, signedSize);
                    inventory.totalRegularBytes += fileSize;
                    if (enforceBaselineLimits &&
                        inventory.files.size () > kMaxProjectBaselineRegularFiles) {
                        return rejectLimit (QStringLiteral ("more than %1 regular files")
                                                .arg (kMaxProjectBaselineRegularFiles));
                    }
                    if (enforceBaselineLimits &&
                        inventory.totalRegularBytes > kMaxProjectBaselineTotalBytes) {
                        return rejectLimit (QStringLiteral ("regular files exceed %1 MiB total")
                                                .arg (kMaxProjectBaselineTotalBytes /
                                                      (1024ULL * 1024ULL)));
                    }
                    if (includeDigests) {
                        QByteArray digest;
                        if (!fingerprintFile (path, digest, inventoryError))
                            return false;
                        inventory.fileDigests.insert (path, digest);
                    }
                }
                else {
                    if (allowUnsafeEntries) {
                        inventory.unsafeEntries.insert (path);
                        continue;
                    }
                    if (inventoryError != nullptr)
                        *inventoryError =
                            QStringLiteral ("Project tree contains a non-regular entry: %1")
                                .arg (path);
                    return false;
                }
            }
            return true;
        };
        return visitDirectory (projectRoot);
    };

    const QString cleanProjectRoot = inventoryPath (projectRoot);
    CandidatePathInventory baselineInventory;
    QString baselineError;
    if (!captureProjectInventory (baselineInventory, true, false, true, &baselineError)) {
        if (error != nullptr)
            *error = baselineError;
        return false;
    }

    ProjectWriteGuard transactionGuard;
    QString transactionGuardError;
    const QString transactionAnchor = missingProjectDirectories.isEmpty ()
                                          ? projectRoot
                                          : existingAncestor;
    if (!ProjectWriteGuard::acquire (
            transactionAnchor, transactionAnchor, transactionGuard, &transactionGuardError)) {
        if (error != nullptr) {
            *error = QStringLiteral ("The target project root could not be anchored safely: %1")
                         .arg (transactionGuardError);
        }
        return false;
    }

    QByteArray previousBootstrapState;
    if (!invokeCallback (
            QStringLiteral ("RobotModelBuilder snapshotState"),
            [&] { return callbacks.snapshotState (previousBootstrapState, error); },
            error)) {
        if (error != nullptr && error->isEmpty ())
            *error = QStringLiteral ("RobotModelBuilder state snapshot failed.");
        return false;
    }

    ProjectDocumentRegistry::CandidateTransitionReservation transitionReservation;
    if (!invokeCallback (
            QStringLiteral ("Project provider snapshot"),
            [&] {
                return _projectDocuments.preflightCandidateTransition (
                    candidateResources, transitionReservation, error);
            },
            error))
        return false;

    if (!invokeCallback (QStringLiteral ("RobotModelBuilder confirmClose"),
                         [&] { return callbacks.confirmClose (error); },
                         error))
        return false;

    transactionGuardError.clear ();
    if (!transactionGuard.validateRootIdentity (&transactionGuardError)) {
        if (error != nullptr)
            *error = transactionGuardError;
        return false;
    }

    const quint64 proportionalSafety =
        baselineInventory.totalRegularBytes / kProjectBaselineBackupSafetyPercent;
    const quint64 backupSafety =
        std::max (kProjectBaselineBackupSafetyBytes, proportionalSafety);
    if (baselineInventory.totalRegularBytes >
        std::numeric_limits< quint64 >::max () - backupSafety) {
        if (error != nullptr)
            *error = QStringLiteral ("The required temporary baseline backup size overflowed.");
        return false;
    }
    const quint64 requiredBackupBytes = baselineInventory.totalRegularBytes + backupSafety;
    QStorageInfo temporaryStorage (QDir::tempPath ());
    temporaryStorage.refresh ();
    const qint64 availableBackupBytes = temporaryStorage.bytesAvailable ();
    if (!temporaryStorage.isValid () || !temporaryStorage.isReady () ||
        availableBackupBytes < 0 ||
        static_cast< quint64 > (availableBackupBytes) < requiredBackupBytes) {
        if (error != nullptr) {
            *error = QStringLiteral (
                         "Temporary storage cannot protect the existing project baseline (%1 bytes "
                         "required). Free temporary disk space or choose a new or empty project "
                         "directory.")
                         .arg (requiredBackupBytes);
        }
        return false;
    }

    QTemporaryDir baselineBackup;
    if (!baselineBackup.isValid ()) {
        if (error != nullptr)
            *error = QStringLiteral ("Could not create a temporary project baseline backup.");
        return false;
    }
    const QString cleanBackupRoot = inventoryPath (baselineBackup.path ());
    const QString backupRelative =
        QDir::fromNativeSeparators (QDir (cleanProjectRoot).relativeFilePath (cleanBackupRoot));
    if (backupRelative == QStringLiteral (".") ||
        (!QDir::isAbsolutePath (backupRelative) && backupRelative != QStringLiteral ("..") &&
         !backupRelative.startsWith (QStringLiteral ("../")))) {
        if (error != nullptr)
            *error = QStringLiteral ("The temporary baseline backup must be outside the project root.");
        return false;
    }

    QHash< QString, QString > baselineBackupFiles;
    for (const QString& sourcePath : baselineInventory.files) {
        const QString relativePath =
            QDir::fromNativeSeparators (QDir (cleanProjectRoot).relativeFilePath (sourcePath));
        const QString backupPath =
            QDir (baselineBackup.path ()).filePath (QStringLiteral ("files/") + relativePath);
        if (!QDir ().mkpath (QFileInfo (backupPath).absolutePath ())) {
            if (error != nullptr)
                *error = QStringLiteral ("Could not create the project baseline backup directory: %1")
                             .arg (QFileInfo (backupPath).absolutePath ());
            return false;
        }
        ProjectWriteGuard sourceGuard;
        QString copyError;
        if (!ProjectWriteGuard::acquire (projectRoot, sourcePath, sourceGuard, &copyError) ||
            !QFile::copy (sourcePath, backupPath)) {
            if (error != nullptr)
                *error = copyError.isEmpty ()
                             ? QStringLiteral ("Could not back up project baseline file: %1").arg (sourcePath)
                             : copyError;
            return false;
        }
        QByteArray backupDigest;
        if (QFileInfo (backupPath).size () != baselineInventory.fileSizes.value (sourcePath) ||
            !fingerprintFile (backupPath, backupDigest, &copyError) ||
            backupDigest != baselineInventory.fileDigests.value (sourcePath)) {
            if (error != nullptr)
                *error = copyError.isEmpty ()
                             ? QStringLiteral ("Project baseline changed while it was backed up: %1")
                                   .arg (sourcePath)
                             : copyError;
            return false;
        }
        baselineBackupFiles.insert (sourcePath, backupPath);
    }
    CandidatePathInventory verifiedBaseline;
    if (!captureProjectInventory (verifiedBaseline, true, false, true, &baselineError) ||
        verifiedBaseline.files != baselineInventory.files ||
        verifiedBaseline.directories != baselineInventory.directories ||
        verifiedBaseline.fileDigests != baselineInventory.fileDigests ||
        verifiedBaseline.fileSizes != baselineInventory.fileSizes ||
        verifiedBaseline.totalRegularBytes != baselineInventory.totalRegularBytes) {
        if (error != nullptr)
            *error = baselineError.isEmpty ()
                         ? QStringLiteral ("The project baseline changed before candidate creation.")
                         : baselineError;
        return false;
    }

    const auto cleanupCreatedDirectories = [&] (QStringList& details) {
        QSet< QString > directories;
        for (const QString& directory : missingProjectDirectories)
            directories.insert (QDir::cleanPath (directory));
        for (const CandidateGeneratedOutput& output : generatedOutputs) {
            for (const QString& directory : output.createdDirectories)
                directories.insert (QDir::cleanPath (directory));
        }
        QStringList ordered = directories.values ();
        std::sort (ordered.begin (), ordered.end (), [] (const QString& left, const QString& right) {
            return QDir::fromNativeSeparators (left).count (QLatin1Char ('/')) >
                QDir::fromNativeSeparators (right).count (QLatin1Char ('/'));
        });
        for (const QString& directory : ordered) {
            if (baselineInventory.directories.contains (inventoryPath (directory)))
                continue;
            const QFileInfo info (directory);
            if (isUnsafeInventoryEntry (info) || !info.exists () || !info.isDir () ||
                !QDir (directory).entryList (
                    QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden).isEmpty ())
                continue;
            QDir parent (info.absolutePath ());
            if (!parent.rmdir (info.fileName ()))
                details.push_back (
                    QStringLiteral ("Candidate directory cleanup failed: %1").arg (directory));
        }
    };

    const auto restoreBaselineTree = [&] (QStringList& details) {
        QString restoreError;
#ifndef Q_OS_WIN
        const auto relativeInventoryPath = [&] (const QString& absolutePath) {
            return QDir::fromNativeSeparators (
                QDir (cleanProjectRoot).relativeFilePath (absolutePath));
        };
        QSet< QString > baselineRelativeFiles;
        QSet< QString > baselineRelativeDirectories;
        QHash< QString, QByteArray > baselineRelativeDigests;
        QHash< QString, qint64 > baselineRelativeSizes;
        for (const QString& path : baselineInventory.files) {
            const QString relative = relativeInventoryPath (path);
            baselineRelativeFiles.insert (relative);
            baselineRelativeDigests.insert (relative, baselineInventory.fileDigests.value (path));
            baselineRelativeSizes.insert (relative, baselineInventory.fileSizes.value (path));
        }
        for (const QString& path : baselineInventory.directories) {
            const QString relative = relativeInventoryPath (path);
            if (relative != QStringLiteral ("."))
                baselineRelativeDirectories.insert (relative);
        }

        bool restored = true;
        if (!transactionGuard.reconcileRelativeTree (
                baselineRelativeFiles, baselineRelativeDirectories, &restoreError)) {
            details.push_back (
                QStringLiteral ("Anchored candidate tree cleanup failed: %1").arg (restoreError));
            restored = false;
        }
        restoreError.clear ();
        if (!transactionGuard.ensureRelativeDirectories (
                baselineRelativeDirectories, &restoreError)) {
            details.push_back (
                QStringLiteral ("Anchored baseline directory restore failed: %1").arg (restoreError));
            restored = false;
        }
        for (const QString& targetPath : baselineInventory.files) {
            const QString backupPath = baselineBackupFiles.value (targetPath);
            const QString relative = relativeInventoryPath (targetPath);
            QByteArray backupDigest;
            QString fileError;
            if (backupPath.isEmpty () ||
                QFileInfo (backupPath).size () != baselineInventory.fileSizes.value (targetPath) ||
                !fingerprintFile (backupPath, backupDigest, &fileError) ||
                backupDigest != baselineInventory.fileDigests.value (targetPath) ||
                !transactionGuard.restoreRelativeFileAtomically (
                    backupPath, relative, &fileError)) {
                details.push_back (
                    QStringLiteral ("Anchored baseline file restore failed: %1")
                        .arg (fileError.isEmpty () ? relative : fileError));
                restored = false;
            }
        }

        ProjectAnchoredInventory restoredInventory;
        restoreError.clear ();
        if (!transactionGuard.captureRelativeInventory (
                restoredInventory, true, &restoreError) ||
            restoredInventory.files != baselineRelativeFiles ||
            restoredInventory.directories != baselineRelativeDirectories ||
            restoredInventory.fileDigests != baselineRelativeDigests ||
            restoredInventory.fileSizes != baselineRelativeSizes ||
            restoredInventory.totalRegularBytes != baselineInventory.totalRegularBytes) {
            details.push_back (
                restoreError.isEmpty ()
                    ? QStringLiteral ("Anchored project baseline verification failed after rollback.")
                    : QStringLiteral ("Anchored project baseline verification failed after rollback: %1")
                          .arg (restoreError));
            restored = false;
        }
        return restored;
#else
        const auto recordCleanupFailure = [&] (const QString& description,
                                               const QString& path,
                                               const QString& cleanupError) {
            details.push_back (
                cleanupError.isEmpty ()
                    ? QStringLiteral ("%1: %2").arg (description, path)
                    : QStringLiteral ("%1: %2").arg (description, cleanupError));
        };
        const auto removeCandidateEntry = [&] (const QFileInfo& info,
                                                const QString& description) {
            const QString path = inventoryPath (info.absoluteFilePath ());
            QString cleanupError;
            if (isUnsafeInventoryEntry (info)) {
                if (!ProjectPathResolver::removeContainedUnsafeEntry (
                        projectRoot, path, &cleanupError))
                    recordCleanupFailure (description, path, cleanupError);
                return;
            }
            const bool removed = info.isDir ()
                                     ? ProjectPathResolver::removeContainedDirectoryTree (
                                           projectRoot, path, &cleanupError)
                                 : info.isFile ()
                                     ? ProjectPathResolver::removeContainedFile (
                                           projectRoot, path, &cleanupError)
                                     : ProjectPathResolver::removeContainedUnsafeEntry (
                                           projectRoot, path, &cleanupError);
            if (!removed)
                recordCleanupFailure (description, path, cleanupError);
        };

        const QFileInfo currentRootInfo (projectRoot);
        if (isUnsafeInventoryEntry (currentRootInfo) ||
            (currentRootInfo.exists () && !currentRootInfo.isDir ())) {
            QString cleanupError;
            if (!ProjectPathResolver::removeContainedUnsafeEntry (
                    projectRoot, projectRoot, &cleanupError)) {
                recordCleanupFailure (QStringLiteral ("Candidate project-root cleanup failed"),
                                      projectRoot,
                                      cleanupError);
            }
        }
        else if (currentRootInfo.isDir ()) {
            std::function< void (const QString&) > cleanupBaselineDirectory;
            cleanupBaselineDirectory = [&] (const QString& directory) {
                QDirIterator entries (
                    directory,
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
                while (entries.hasNext ()) {
                    const QFileInfo info (entries.next ());
                    const QString path = inventoryPath (info.absoluteFilePath ());
                    const bool unsafe = isUnsafeInventoryEntry (info);
                    const bool baselineDirectory = baselineInventory.directories.contains (path);
                    const bool baselineFile = baselineInventory.files.contains (path);

                    if (baselineDirectory && !unsafe && info.isDir ()) {
                        cleanupBaselineDirectory (path);
                        continue;
                    }
                    if (baselineFile && !unsafe && info.isFile ())
                        continue;

                    removeCandidateEntry (
                        info,
                        baselineDirectory || baselineFile
                            ? QStringLiteral ("Candidate baseline type-replacement cleanup failed")
                            : QStringLiteral ("Candidate undeclared entry cleanup failed"));
                }
            };
            cleanupBaselineDirectory (projectRoot);
        }

        QStringList baselineDirectories = baselineInventory.directories.values ();
        std::sort (baselineDirectories.begin (), baselineDirectories.end (),
                   [] (const QString& left, const QString& right) {
                       return QDir::fromNativeSeparators (left).count (QLatin1Char ('/')) <
                           QDir::fromNativeSeparators (right).count (QLatin1Char ('/'));
                   });
        for (const QString& directory : baselineDirectories) {
            const QFileInfo info (directory);
            if (isUnsafeInventoryEntry (info) ||
                (!info.exists () && !QDir ().mkpath (directory)) ||
                !QFileInfo (directory).isDir ()) {
                details.push_back (
                    QStringLiteral ("Project baseline directory restore failed: %1").arg (directory));
            }
        }

        ProjectSaveTransaction baselineRestore;
        ProjectSaveTransaction::setContainmentRoot (baselineRestore, projectRoot);
        bool allRestoresStaged = true;
        int stagedRestoreCount = 0;
        for (const QString& targetPath : baselineInventory.files) {
            const QString backupPath = baselineBackupFiles.value (targetPath);
            QByteArray backupDigest;
            QString stageError;
            if (backupPath.isEmpty () ||
                QFileInfo (backupPath).size () != baselineInventory.fileSizes.value (targetPath) ||
                !fingerprintFile (backupPath, backupDigest, &stageError) ||
                backupDigest != baselineInventory.fileDigests.value (targetPath) ||
                !baselineRestore.stageCopy (backupPath, targetPath, &stageError)) {
                details.push_back (
                    QStringLiteral ("Project baseline file restore staging failed: %1")
                        .arg (stageError.isEmpty () ? targetPath : stageError));
                allRestoresStaged = false;
                continue;
            }
            ++stagedRestoreCount;
        }
        bool restoreCommitted = allRestoresStaged;
        if (stagedRestoreCount > 0 && !baselineRestore.commit (&restoreError)) {
            details.push_back (
                QStringLiteral ("Project baseline file restore failed: %1").arg (restoreError));
            restoreCommitted = false;
        }

        CandidatePathInventory restoredInventory;
        restoreError.clear ();
        if (!captureProjectInventory (restoredInventory, true, false, true, &restoreError) ||
            restoredInventory.files != baselineInventory.files ||
            restoredInventory.directories != baselineInventory.directories ||
            restoredInventory.fileDigests != baselineInventory.fileDigests ||
            restoredInventory.fileSizes != baselineInventory.fileSizes ||
            restoredInventory.totalRegularBytes != baselineInventory.totalRegularBytes) {
            details.push_back (
                restoreError.isEmpty ()
                    ? QStringLiteral ("Project baseline tree verification failed after rollback.")
                    : QStringLiteral ("Project baseline tree verification failed after rollback: %1")
                          .arg (restoreError));
            return false;
        }
        return restoreCommitted;
#endif
    };

    if (!missingProjectDirectories.isEmpty ()) {
        QString unexpectedDirectory;
        for (const QString& directory : missingProjectDirectories) {
            if (ProjectPathResolver::isLinkOrReparsePoint (directory) ||
                QFileInfo::exists (directory)) {
                unexpectedDirectory = directory;
                break;
            }
        }
        if (!unexpectedDirectory.isEmpty ()) {
            if (error != nullptr) {
                *error = QStringLiteral ("A missing project directory appeared unexpectedly: %1")
                             .arg (unexpectedDirectory);
            }
            return false;
        }

        transactionGuardError.clear ();
        if (!transactionGuard.createMissingProjectRoot (
                projectRoot, missingProjectDirectories, &transactionGuardError)) {
            if (error != nullptr) {
                *error = QStringLiteral ("The target project root could not be created safely: %1")
                             .arg (transactionGuardError);
            }
            return false;
        }
    }

    transactionGuardError.clear ();
    if (!transactionGuard.validateRootIdentity (&transactionGuardError)) {
        QStringList details;
        details.push_back (transactionGuardError);
        transactionGuard.release ();
        if (error != nullptr)
            *error = details.join (QLatin1Char ('\n'));
        return false;
    }

    const auto rollbackFilesystem = [&] (QStringList& details) {
        QString identityError;
        const bool identityStable =
            transactionGuard.validateRootIdentity (&identityError);
        if (!identityStable) {
            details.push_back (
                QStringLiteral (
                    "Replacement-path rollback was skipped because the project root identity "
                    "changed; rollback was restricted to the retained root anchor: %1")
                    .arg (identityError));
        }
#ifdef Q_OS_WIN
        const bool restored = identityStable && restoreBaselineTree (details);
#else
        const bool restored = restoreBaselineTree (details);
#endif
        transactionGuard.release ();
        if (identityStable)
            cleanupCreatedDirectories (details);
        return restored;
    };

    const ProjectManager previousManager = _projectManager;
    ProjectManifest candidateManifest;
    candidateManifest.project.name = QFileInfo (absoluteProjectFile).completeBaseName ();
    candidateManifest.project.description =
        QStringLiteral ("Empty RobotModelBuilder project awaiting bootstrap.");
    candidateManifest.settings.insert (QStringLiteral ("pathPolicy"),
                                      QStringLiteral ("project-relative"));

    if (!_projectManager.createProject (absoluteProjectFile, candidateManifest, error)) {
        QStringList details;
        if (error != nullptr && !error->isEmpty ())
            details.push_back (*error);
        rollbackFilesystem (details);
        if (error != nullptr)
            *error = details.join (QLatin1Char ('\n'));
        return false;
    }

    transactionGuardError.clear ();
    if (!transactionGuard.validateRootIdentity (&transactionGuardError)) {
        QStringList details;
        details.push_back (transactionGuardError);
        rollbackFilesystem (details);
        _projectManager = previousManager;
        if (error != nullptr)
            *error = details.join (QLatin1Char ('\n'));
        return false;
    }

    const auto restorePreviousProject = [&] (const QString& failure) {
        QStringList details;
        if (!failure.isEmpty ())
            details.push_back (failure);
        QString closeError;
        if (!_projectDocuments.closeResources (&closeError)) {
            details.push_back (
                QStringLiteral ("Candidate project document close failed: %1").arg (closeError));
        }
        rollbackFilesystem (details);

        _projectManager = previousManager;
        QString restoreError;
        if (!_projectDocuments.restoreSuspendedResourcesAfterCandidateFailure (&restoreError)) {
            details.push_back (QStringLiteral ("Prior project document restore failed: %1")
                                   .arg (restoreError));
        }
        QString stateRestoreError;
        if (!invokeCallback (
                QStringLiteral ("RobotModelBuilder restoreState"),
                [&] {
                    return callbacks.restoreState (
                        previousBootstrapState, &stateRestoreError);
                },
                &stateRestoreError)) {
            details.push_back (QStringLiteral ("RobotModelBuilder state restore failed: %1")
                                   .arg (stateRestoreError));
        }
        setWindowTitle (previousWindowTitle);
        if (error != nullptr)
            *error = details.join (QLatin1Char ('\n'));
        return false;
    };

    _projectDocuments.suspendResourcesForCandidateTransition (std::move (transitionReservation));

    transactionGuardError.clear ();
    if (!transactionGuard.validateRootIdentity (&transactionGuardError))
        return restorePreviousProject (transactionGuardError);

    QString bootstrapError;
    if (!invokeCallback (
            QStringLiteral ("RobotModelBuilder bootstrap"),
            [&] { return callbacks.bootstrap (projectRoot, &bootstrapError); },
            &bootstrapError)) {
        if (bootstrapError.isEmpty ())
            bootstrapError = QStringLiteral ("RobotModelBuilder project bootstrap failed.");
        return restorePreviousProject (bootstrapError);
    }
    transactionGuardError.clear ();
    if (!transactionGuard.validateRootIdentity (&transactionGuardError))
        return restorePreviousProject (transactionGuardError);

    const auto sameResource = [] (const ProjectResource& left, const ProjectResource& right) {
        return left.id == right.id && left.kind == right.kind && left.path == right.path &&
            left.ownership == right.ownership && left.required == right.required &&
            left.dependencies == right.dependencies;
    };
    QSet< QString > declaredResourceIds;
    for (const ProjectResource& declared : candidateResources)
        declaredResourceIds.insert (declared.id);
    QSet< QString > manifestResourceIds;
    for (const ProjectResource& resource : _projectManager.manifest ().resources)
        manifestResourceIds.insert (resource.id);
    if (manifestResourceIds != declaredResourceIds ||
        _projectDocuments.activeResourceIds () != declaredResourceIds) {
        return restorePreviousProject (
            QStringLiteral (
                "RobotModelBuilder bootstrap resource IDs do not exactly match preflight declarations."));
    }
    for (const ProjectResource& declared : candidateResources) {
        ProjectResource activeManifestResource;
        if (!_projectManager.manifest ().findResource (declared.id, activeManifestResource) ||
            !sameResource (declared, activeManifestResource) ||
            !_projectDocuments.hasActiveResource (declared)) {
            return restorePreviousProject (
                QStringLiteral ("RobotModelBuilder bootstrap did not activate the declared resource: %1")
                    .arg (declared.id));
        }
    }
    bool mainResourceDirty = false;
    QString dirtyCheckError;
    if (!invokeCallback (
            QStringLiteral ("Project provider dirty-state"),
            [&] {
                mainResourceDirty = _projectDocuments.isActiveResourceDirty (
                    QStringLiteral ("robot-model.main"));
                return true;
            },
            &dirtyCheckError)) {
        return restorePreviousProject (dirtyCheckError);
    }
    if (!mainResourceDirty)
        return restorePreviousProject (
            QStringLiteral ("RobotModelBuilder bootstrap must leave robot-model.main dirty."));
    if (!mainWorkCellResourceId ().isEmpty ())
        return restorePreviousProject (
            QStringLiteral ("A new RobotModelBuilder project must not publish mainWorkCell during bootstrap."));

    CandidatePathInventory successInventory;
    QString inventoryError;
    if (!captureProjectInventory (successInventory, true, false, false, &inventoryError)) {
        return restorePreviousProject (
            QStringLiteral ("RobotModelBuilder bootstrap left an unsafe project tree: %1")
                .arg (inventoryError));
    }
    QSet< QString > allowedChangedFiles;
    allowedChangedFiles.insert (inventoryPath (absoluteProjectFile));
    QSet< QString > allowedNewDirectories;
    for (const QString& directory : missingProjectDirectories)
        allowedNewDirectories.insert (inventoryPath (directory));
    for (const QString& file : allowedChangedFiles) {
        QString directory = inventoryPath (QFileInfo (file).absolutePath ());
        while (directory == cleanProjectRoot ||
               directory.startsWith (cleanProjectRoot + QLatin1Char ('/'),
#ifdef Q_OS_WIN
                                     Qt::CaseInsensitive
#else
                                     Qt::CaseSensitive
#endif
                                         )) {
            allowedNewDirectories.insert (directory);
            if (directory == cleanProjectRoot)
                break;
            const QString parent = inventoryPath (QFileInfo (directory).absolutePath ());
            if (parent == directory)
                break;
            directory = parent;
        }
    }

    for (const QString& file : baselineInventory.files) {
        if (allowedChangedFiles.contains (file))
            continue;
        if (!successInventory.files.contains (file)) {
            return restorePreviousProject (
                QStringLiteral ("RobotModelBuilder bootstrap deleted an undeclared baseline file: %1")
                    .arg (file));
        }
        if (successInventory.fileDigests.value (file) !=
            baselineInventory.fileDigests.value (file)) {
            return restorePreviousProject (
                QStringLiteral ("RobotModelBuilder bootstrap modified an undeclared baseline file: %1")
                    .arg (file));
        }
    }
    for (const QString& file : successInventory.files) {
        if (!baselineInventory.files.contains (file) && !allowedChangedFiles.contains (file)) {
            return restorePreviousProject (
                QStringLiteral ("RobotModelBuilder bootstrap created an undeclared file: %1")
                    .arg (file));
        }
    }
    for (const QString& directory : baselineInventory.directories) {
        if (!successInventory.directories.contains (directory)) {
            return restorePreviousProject (
                QStringLiteral ("RobotModelBuilder bootstrap deleted a baseline directory: %1")
                    .arg (directory));
        }
    }
    for (const QString& directory : successInventory.directories) {
        if (!baselineInventory.directories.contains (directory) &&
            !allowedNewDirectories.contains (directory)) {
            return restorePreviousProject (
                QStringLiteral ("RobotModelBuilder bootstrap created an undeclared directory: %1")
                    .arg (directory));
        }
    }

    transactionGuardError.clear ();
    if (!transactionGuard.validateRootIdentity (&transactionGuardError))
        return restorePreviousProject (transactionGuardError);

    QString suspendedCloseError;
    if (!_projectDocuments.closeSuspendedResourcesAfterCandidateSuccess (
            &suspendedCloseError)) {
        return restorePreviousProject (
            QStringLiteral ("Prior project document close failed: %1")
                .arg (suspendedCloseError));
    }
    createEmptyWorkCell ();

    _settingsMap->set< std::string > ("PreviousOpenDirectory", projectRoot.toStdString ());
    std::vector< std::string > recent = _settingsMap->get< std::vector< std::string > > (
        "LastOpennedFiles", std::vector< std::string > ());
    recent.push_back (absoluteProjectFile.toStdString ());
    _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles", recent);
    updateLastFiles ();
    updateProjectWindowTitle ();
    return true;
}

QString generatedSceneAssetId (const QString& filePath, const QString& relativePath)
{
    QFile file (filePath);
    if (file.open (QIODevice::ReadOnly)) {
        QXmlStreamReader xml (&file);
        while (xml.readNextStartElement ()) {
            const QString root = xml.name ().toString ();
            if (root.compare (QStringLiteral ("SerialDevice"), Qt::CaseInsensitive) == 0)
                return QStringLiteral ("scene.generated.device");
            if (root.compare (QStringLiteral ("CollisionSetup"), Qt::CaseInsensitive) == 0)
                return QStringLiteral ("scene.generated.collision");
            if (root.compare (QStringLiteral ("ProximitySetup"), Qt::CaseInsensitive) == 0)
                return QStringLiteral ("scene.generated.proximity");
            break;
        }
    }

    const QByteArray digest = QCryptographicHash::hash (
        QDir::fromNativeSeparators (relativePath).toUtf8 (), QCryptographicHash::Sha256);
    return QStringLiteral ("scene.generated.asset.%1").arg (
        QString::fromLatin1 (digest.toHex ().left (12)));
}

// 从 URDF/XML 机器人文件创建草稿项目：主窗口只创建空项目并通过 Qt 元对象把源文件
// 交给可选的 RobotModelBuilder 插件，避免主程序对该插件产生静态链接依赖。
void RobWorkStudio::createProjectFromRobotFile ()
{
    const QString previousDirectory = QString::fromStdString (
        _settingsMap->get< std::string > ("PreviousOpenDirectory", ""));
    const QString sourcePath = QFileDialog::getOpenFileName (
        this, tr ("Create Project from Robot File"), previousDirectory,
        tr ("URDF Robot Files (*.urdf *.xml);;All Files (*.*)"));
    if (sourcePath.isEmpty ())
        return;

    QString sourceError;
    const RobotProjectSourceKind sourceKind = classifyRobotProjectSource (sourcePath, &sourceError);
    if (sourceKind == RobotProjectSourceKind::RobWorkXml) {
        QMessageBox::information (
            this,
            tr ("RobWork XML Detected"),
            tr ("The selected file is a RobWork WorkCell or device XML, not URDF. "
                "Use File > Create Project from WorkCell... so the scene and its dependencies "
                "are copied into the managed project before RobotModelBuilder processes it."));
        return;
    }
    if (sourceKind != RobotProjectSourceKind::Urdf) {
        QMessageBox::warning (this, tr ("Unsupported Robot File"), sourceError);
        return;
    }

    RobWorkStudioPlugin* builder = NULL;
    for (RobWorkStudioPlugin* plugin : getPlugins ()) {
        if (plugin != NULL && plugin->name () == QStringLiteral ("RobotModelBuilder")) {
            builder = plugin;
            break;
        }
    }
    const QByteArray preflightSignature =
        QMetaObject::normalizedSignature (
            "preflightRobotProjectSource(QString,QString)");
    const QByteArray commitSignature =
        QMetaObject::normalizedSignature (
            "commitRobotProjectSource(QString,QString)");
    if (builder == NULL || builder->metaObject ()->indexOfMethod (preflightSignature) < 0 ||
        builder->metaObject ()->indexOfMethod (commitSignature) < 0) {
        QMessageBox::warning (
            this, tr ("RobotModelBuilder Unavailable"),
            tr ("RobotModelBuilder with managed robot-project preflight and commit support "
                "must be loaded before creating a project from URDF."));
        return;
    }

    QString projectFile = QFileDialog::getSaveFileName (
        this, tr ("New RobWorkStudio Project"), QFileInfo (sourcePath).absolutePath (),
        tr ("RobWorkStudio Project (*.rwproj)"));
    if (projectFile.isEmpty ())
        return;
    if (!projectFile.endsWith (QStringLiteral (".rwproj"), Qt::CaseInsensitive))
        projectFile += QStringLiteral (".rwproj");

    RobotProjectImportCallbacks callbacks;
    callbacks.preflight = [builder] (const QString& path,
                                     const QString& projectRoot,
                                     QString* error) {
        QString result;
        if (!QMetaObject::invokeMethod (
                builder, "preflightRobotProjectSource", Qt::DirectConnection,
                Q_RETURN_ARG (QString, result), Q_ARG (QString, path),
                Q_ARG (QString, projectRoot))) {
            if (error != nullptr)
                *error = QStringLiteral ("RobotModelBuilder preflight invocation failed.");
            return false;
        }
        if (error != nullptr)
            *error = result;
        return result.isEmpty ();
    };
    callbacks.confirmClose = [this] (QString* error) {
        const bool confirmed = confirmProjectClose ();
        if (!confirmed && error != nullptr)
            error->clear ();
        return confirmed;
    };
    callbacks.commit = [builder] (const QString& path,
                                  const QString& projectRoot,
                                  QString* error) {
        QString result;
        if (!QMetaObject::invokeMethod (
                builder, "commitRobotProjectSource", Qt::DirectConnection,
                Q_RETURN_ARG (QString, result), Q_ARG (QString, path),
                Q_ARG (QString, projectRoot))) {
            if (error != nullptr)
                *error = QStringLiteral ("RobotModelBuilder commit invocation failed.");
            return false;
        }
        if (error != nullptr)
            *error = result;
        return result.isEmpty ();
    };

    QString error;
    if (!createProjectFromRobotFilePaths (
            sourcePath, projectFile, callbacks, &error)) {
        if (!error.isEmpty ())
            QMessageBox::critical (this, tr ("Create Project Failed"), error);
        return;
    }
    if (!builder->isVisible ())
        builder->showPlugin ();
}

// 从既有 WorkCell 创建项目：项目文件和资源复制完成后，先释放旧项目 Provider，再复用统一
// 的 openProjectFile 加载新项目。这样 openProjectFile 内部的“校验后切换”仍是唯一加载入口。
void RobWorkStudio::createProjectFromWorkCell ()
{
    const QString previousDirectory = QString::fromStdString (
        _settingsMap->get< std::string > ("PreviousOpenDirectory", ""));
    const QString sourceWorkCell = QFileDialog::getOpenFileName (
        this,
        tr ("Create Project from WorkCell"),
        previousDirectory,
        tr ("WorkCell Files (*.wc.xml *.wc *.xml);;All Files (*.*)"));
    if (sourceWorkCell.isEmpty ())
        return;

    QString projectFile = QFileDialog::getSaveFileName (
        this,
        tr ("New RobWorkStudio Project"),
        QFileInfo (sourceWorkCell).absolutePath (),
        tr ("RobWorkStudio Project (*.rwproj)"));
    if (projectFile.isEmpty ())
        return;
    if (!projectFile.endsWith (QStringLiteral (".rwproj"), Qt::CaseInsensitive))
        projectFile += QStringLiteral (".rwproj");

    if (!confirmProjectClose ())
        return;

    QString error;
    if (!_projectManager.createProjectFromWorkCell (projectFile, sourceWorkCell, &error)) {
        QMessageBox::critical (this, tr ("Create Project Failed"), error);
        return;
    }

    // createProjectFromWorkCell 已经切换了清单上下文，因此旧 Provider 必须在调用统一加载入口
    // 前关闭；否则旧文档仍会占用旧文件路径，并可能把后续保存请求发送到新项目清单。
    _projectDocuments.closeResources ();
    closeWorkCell ();
    if (!openProjectFile (projectFile, &error)) {
        QMessageBox::critical (this, tr ("Open Created Project Failed"), error);
        return;
    }

    _settingsMap->set< std::string > ("PreviousOpenDirectory",
                                      QFileInfo (projectFile).absolutePath ().toStdString ());
    std::vector< std::string > recent = _settingsMap->get< std::vector< std::string > > (
        "LastOpennedFiles", std::vector< std::string > ());
    recent.push_back (projectFile.toStdString ());
    _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles", recent);
    updateLastFiles ();
    updateProjectWindowTitle ();
}

// 项目另存为：先让所有 Provider 将内存内容提交到旧项目，再由 ProjectManager 事务式克隆；
// 克隆成功后重新打开目标项目，确保 Registry 中缓存的 resolvedPath 全部指向新目录。
void RobWorkStudio::saveProjectAs ()
{
    if (!_projectManager.hasProject ()) {
        QMessageBox::information (this, tr ("Save Project As"), tr ("No project is open."));
        return;
    }

    const QString previousDirectory = QString::fromStdString (
        _settingsMap->get< std::string > ("PreviousOpenDirectory", ""));
    QString projectFile = QFileDialog::getSaveFileName (
        this,
        tr ("Save RobWorkStudio Project As"),
        previousDirectory,
        tr ("RobWorkStudio Project (*.rwproj)"));
    if (projectFile.isEmpty ())
        return;
    if (!projectFile.endsWith (QStringLiteral (".rwproj"), Qt::CaseInsensitive))
        projectFile += QStringLiteral (".rwproj");
    if (QDir::cleanPath (QFileInfo (projectFile).absoluteFilePath ()) ==
        QDir::cleanPath (_projectManager.projectFilePath ())) {
        QMessageBox::warning (this, tr ("Save Project As"), tr ("The target is the current project."));
        return;
    }

    QString error;
    if (!saveProjectInternal (&error)) {
        QMessageBox::warning (this, tr ("Save Project Failed"), error);
        return;
    }
    if (!_projectManager.cloneProject (projectFile, &error)) {
        QMessageBox::warning (this, tr ("Save Project As Failed"), error);
        return;
    }

    _projectDocuments.closeResources ();
    closeWorkCell ();
    if (!openProjectFile (projectFile, &error)) {
        QMessageBox::critical (this, tr ("Open Cloned Project Failed"), error);
        return;
    }

    _settingsMap->set< std::string > ("PreviousOpenDirectory",
                                      QFileInfo (projectFile).absolutePath ().toStdString ());
    std::vector< std::string > recent = _settingsMap->get< std::vector< std::string > > (
        "LastOpennedFiles", std::vector< std::string > ());
    recent.push_back (projectFile.toStdString ());
    _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles", recent);
    updateLastFiles ();
    updateProjectWindowTitle ();
}

// 导入历史业务文件：先根据扩展名确定 Provider kind 和项目内目录，再由 ProjectManager 完成
// 原子复制及清单更新。当前版本不强行替换正在编辑的文档，用户保存后重新打开即可加载新增资源。
void RobWorkStudio::importProjectResource ()
{
    if (!_projectManager.hasProject ()) {
        QMessageBox::information (this, tr ("Import Project Resource"), tr ("No project is open."));
        return;
    }

    const QString previousDirectory = QString::fromStdString (
        _settingsMap->get< std::string > ("PreviousOpenDirectory", ""));
    const QString sourcePath = QFileDialog::getOpenFileName (
        this,
        tr ("Import Project Resource"),
        previousDirectory,
        tr ("Supported Project Resources (*.wc.xml *.rmb.json *.requirements.json "
            "*.structure-optimization.json *.kinematic-analysis.json);;All Files (*.*)"));
    if (sourcePath.isEmpty ())
        return;

    ProjectResource resource;
    QString error;
    if (!makeImportedProjectResource (sourcePath, _projectManager.manifest (), resource, &error)) {
        QMessageBox::warning (this, tr ("Import Project Resource Failed"), error);
        return;
    }
    if (!_projectManager.importResource (sourcePath, resource, &error)) {
        QMessageBox::warning (this, tr ("Import Project Resource Failed"), error);
        return;
    }

    // 导入文件已经落盘，但清单仍处于脏状态；复用统一保存流程，让已打开 Provider 的修改和
    // 新增资源登记使用同一个项目保存事务，避免只复制文件却丢失资源索引。
    if (!saveProjectInternal (&error)) {
        QMessageBox::warning (this, tr ("Save Imported Resource Failed"), error);
        updateProjectWindowTitle ();
        return;
    }
    updateProjectWindowTitle ();
    QMessageBox::information (
        this,
        tr ("Project Resource Imported"),
        tr ("The resource was imported. Reopen the project to load the new document."));
}

// 导出项目包：先统一保存所有脏文档与清单，确保归档内容是磁盘上的最新状态，
// 再弹出保存对话框调用 ProjectManager 生成 rwpack。
void RobWorkStudio::exportProjectPackage ()
{
    if (!_projectManager.hasProject ()) {
        QMessageBox::information (this, tr ("Export Project Package"), tr ("No project is open."));
        return;
    }
    QString error;
    if (!saveProjectInternal (&error)) {
        QMessageBox::warning (this, tr ("Export Project Package Failed"), error);
        return;
    }
    const QString packagePath = QFileDialog::getSaveFileName (
        this, tr ("Export Project Package"), _projectManager.projectFilePath () + ".rwpack",
        tr ("RobWorkStudio Package (*.rwpack)"));
    if (packagePath.isEmpty ())
        return;
    if (!_projectManager.exportPackage (packagePath, &error)) {
        QMessageBox::warning (this, tr ("Export Project Package Failed"), error);
        return;
    }
    QMessageBox::information (this, tr ("Project Package Exported"), packagePath);
}

// 导入项目包：选择 rwpack 与目标父目录，解包后复用统一 openProjectFile 打开
// 解出的 project.rwproj（含崩溃恢复提示与加载警告）。
void RobWorkStudio::importProjectPackage ()
{
    const QString packagePath = QFileDialog::getOpenFileName (
        this, tr ("Import Project Package"), QString (), tr ("RobWorkStudio Package (*.rwpack)"));
    if (packagePath.isEmpty ())
        return;
    const QString parent = QFileDialog::getExistingDirectory (
        this, tr ("Choose Project Package Destination"), QFileInfo (packagePath).absolutePath ());
    if (parent.isEmpty ())
        return;
    const QString target = QDir (parent).filePath (QFileInfo (packagePath).completeBaseName ());
    QString extractedProject;
    QString error;
    if (!ProjectManager::extractPackage (packagePath, target, extractedProject, &error) ||
        !openProjectFile (extractedProject, &error)) {
        QMessageBox::warning (this, tr ("Import Project Package Failed"), error);
        return;
    }
    updateProjectWindowTitle ();
}

// 项目完整性检查：展示 MissingResource/UnreferencedFile/ChangedSinceAutosave 三类问题，
// 并提供修复动作——确认后删除未引用文件、为缺失资源选择替换文件并原子更新清单。
void RobWorkStudio::inspectProjectIntegrity ()
{
    if (!_projectManager.hasProject ()) {
        QMessageBox::information (this, tr ("Project Integrity"), tr ("No project is open."));
        return;
    }
    QString error;
    const QVector< ProjectManager::IntegrityIssue > issues = _projectManager.inspectIntegrity (&error);
    if (!error.isEmpty ()) {
        QMessageBox::warning (this, tr ("Project Integrity Failed"), error);
        return;
    }
    if (issues.isEmpty ()) {
        QMessageBox::information (this, tr ("Project Integrity"), tr ("No integrity issues found."));
        return;
    }
    QStringList messages;
    QStringList unreferenced;
    for (const ProjectManager::IntegrityIssue& issue : issues) {
        messages.push_back (issue.message + QStringLiteral ("\n") + issue.path);
        if (issue.type == ProjectManager::IntegrityIssue::Type::UnreferencedFile)
            unreferenced.push_back (issue.path);
    }
    QMessageBox::information (this, tr ("Project Integrity"), messages.join (QStringLiteral ("\n\n")));
    if (!unreferenced.isEmpty () && QMessageBox::question (
            this, tr ("Remove Unreferenced Files"), tr ("Remove the unreferenced files shown above?")) ==
            QMessageBox::Yes) {
        if (!_projectManager.removeUnreferencedFiles (unreferenced, &error))
            QMessageBox::warning (this, tr ("Remove Unreferenced Files Failed"), error);
    }
    for (const ProjectManager::IntegrityIssue& issue : issues) {
        if (issue.type != ProjectManager::IntegrityIssue::Type::MissingResource)
            continue;
        if (QMessageBox::question (this, tr ("Relocate Missing Resource"),
                                   tr ("Locate replacement for %1?").arg (issue.resourceId)) != QMessageBox::Yes)
            continue;
        const QString replacement = QFileDialog::getOpenFileName (this, tr ("Locate Resource"));
        if (!replacement.isEmpty () && !_projectManager.relocateResource (issue.resourceId, replacement, &error))
            QMessageBox::warning (this, tr ("Relocate Resource Failed"), error);
    }
}

// 打开项目：弹文件选择对话框选中 .rwproj，统一走 openFile() 分派，
// 以便与命令行、拖放入口共享完全相同的项目加载行为。
void RobWorkStudio::openProject ()
{
    const QString previousDirectory = QString::fromStdString (
        _settingsMap->get< std::string > ("PreviousOpenDirectory", ""));
    const QString projectFile = QFileDialog::getOpenFileName (
        this,
        tr ("Open RobWorkStudio Project"),
        previousDirectory,
        tr ("RobWorkStudio Project (*.rwproj);;All Files (*.*)"));
    if (!projectFile.isEmpty ())
        openFile (projectFile.toStdString ());
}

bool RobWorkStudio::registerProjectDocumentProvider (ProjectDocumentProvider* provider,
                                                      QString* error)
{
    // 插件初始化可能发生在没有打开项目之前；Registry 支持预注册，随后打开项目时
    // 再按清单 kind 选择 Provider。失败不改变已有注册表，调用者可安全报告错误。
    const bool registered = _projectDocuments.registerProvider (provider, error);
    if (registered)
        updateProjectWindowTitle ();
    return registered;
}

bool RobWorkStudio::ensureGeneratedProjectResource (const ProjectResource& resource,
                                                     bool* created,
                                                     QString* error)
{
    if (created != nullptr)
        *created = false;
    if (!_projectManager.hasProject ()) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("当前没有打开的项目，无法创建插件资源。");
        return false;
    }
    // 自动保存定时器：项目有未保存修改时周期性创建恢复快照，快照同时包含清单、磁盘资源
    // 与 Provider 内存编辑状态（createAutosaveSnapshot 接收 _projectDocuments 实现）。
    // 注意：此处位于 ensureGeneratedProjectResource 内属懒启动，仅插件首次创建生成资源后生效。
    if (_autosaveTimer == nullptr) {
        _autosaveTimer = new QTimer (this);
        _autosaveTimer->setInterval (60 * 1000);
        connect (_autosaveTimer, &QTimer::timeout, this, [this] () {
            if (_projectTransitionDepth > 0 || !_projectManager.hasProject () ||
                (!_projectManager.isDirty () && !_projectDocuments.isDirty ()))
                return;
            QString error;
            if (!_projectManager.createAutosaveSnapshot (_projectDocuments, &error))
                RW_WARN ("Unable to create project recovery snapshot: " << error.toStdString ());
        });
        _autosaveTimer->start ();
    }

    // 稳定 ID 已存在时不重复登记。首次编辑之外的每次控件变化只能更新脏状态，不能追加
    // 清单项，更不能覆盖用户已经保存的业务配置。
    ProjectResource existing;
    if (_projectManager.manifest ().findResource (resource.id, existing)) {
        QStringList desiredDependencies;
        for (const QString& dependency : resource.dependencies) {
            if (!desiredDependencies.contains (dependency))
                desiredDependencies.push_back (dependency);
        }
        if (desiredDependencies != existing.dependencies) {
            existing.dependencies = desiredDependencies;
            ProjectManifest candidateManifest = _projectManager.manifest ();
            for (ProjectResource& candidateResource : candidateManifest.resources) {
                if (candidateResource.id == existing.id) {
                    candidateResource = existing;
                    break;
                }
            }
            if (!_projectDocuments.synchronizeLoadedResources (
                    candidateManifest, _projectManager.projectFilePath (), error))
                return false;
            if (!_projectManager.replaceResourceAndAddAssets (existing, {}, error)) {
                const QString replacementError = error == nullptr ? QString () : *error;
                QString rollbackError;
                if (!_projectDocuments.synchronizeLoadedResources (
                        _projectManager.manifest (), _projectManager.projectFilePath (),
                        &rollbackError) && error != nullptr) {
                    *error = replacementError + QString::fromUtf8 (
                        " Registry 回滚失败：%1").arg (rollbackError);
                }
                return false;
            }
            updateProjectWindowTitle ();
        }
        return true;
    }
    if (!_projectManager.addGeneratedResource (resource, error))
        return false;
    if (!_projectDocuments.activateGeneratedResource (
            resource, _projectManager.projectFilePath (), error))
        return false;

    // 清单仅在内存中标脏，不在此处落盘。新资源 JSON 和 .rwproj 由一次“保存项目”操作
    // 经同一个暂存事务统一提交，失败时不会产生只有其中一个文件的半成品项目。
    if (created != nullptr)
        *created = true;
    updateProjectWindowTitle ();
    return true;
}

// 识别机器人源文件的根元素类型：URDF <robot> 走"从机器人文件创建项目"，RobWork
// WorkCell/设备 XML 引导用户改走"从 WorkCell 创建项目"，其它根元素或损坏 XML 判为不支持。
RobWorkStudio::RobotProjectSourceKind RobWorkStudio::classifyRobotProjectSource (
    const QString& sourcePath, QString* error)
{
    if (error != nullptr)
        error->clear ();

    QFile file (sourcePath);
    if (!file.open (QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = tr ("Could not open the selected robot file: %1").arg (file.errorString ());
        return RobotProjectSourceKind::Unsupported;
    }

    QXmlStreamReader xml (&file);
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement ())
            continue;

        const QString root = xml.name ().toString ();
        if (root.compare (QStringLiteral ("robot"), Qt::CaseInsensitive) == 0)
            return RobotProjectSourceKind::Urdf;
        if (root == QStringLiteral ("WorkCell") || root == QStringLiteral ("SerialDevice") ||
            root == QStringLiteral ("ParallelDevice") || root == QStringLiteral ("TreeDevice"))
            return RobotProjectSourceKind::RobWorkXml;

        if (error != nullptr) {
            *error = tr ("Unsupported XML root <%1>. Select a URDF <robot> file, or use "
                        "Create Project from WorkCell... for RobWork XML.")
                         .arg (root);
        }
        return RobotProjectSourceKind::Unsupported;
    }

    if (error != nullptr) {
        *error = xml.hasError () ? tr ("The selected XML file is invalid: %1").arg (xml.errorString ())
                                : tr ("The selected file does not contain an XML root element.");
    }
    return RobotProjectSourceKind::Unsupported;
}

// 按稳定资源 ID 解析当前项目文件（委托 ProjectManager），供插件使用清单权威路径。
bool RobWorkStudio::resolveProjectResource (const QString& resourceId,
                                            QString& resolvedPath,
                                            QString* error) const
{
    return _projectManager.resolveResource (resourceId, resolvedPath, error);
}

bool RobWorkStudio::promoteGeneratedWorkCell (const QString& sceneFilePath,
                                              const QStringList& dependencyFilePaths,
                                              QString* error)
{
    if (!_projectManager.hasProject ()) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("当前没有打开的项目，无法晋升生成场景。");
        return false;
    }
    const QFileInfo sceneInfo (sceneFilePath);
    if (!sceneInfo.isFile ()) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("生成场景不存在：%1。").arg (sceneFilePath);
        return false;
    }

    const QString projectRoot = QFileInfo (_projectManager.projectFilePath ()).absolutePath ();
    const QDir projectDirectory (projectRoot);
    const auto managedRelativePath = [&projectDirectory, &projectRoot] (
                                         const QString& absolutePath,
                                         QString& relativePath,
                                         QString* pathError) {
        const QFileInfo info (absolutePath);
        if (!info.isFile ()) {
            if (pathError != nullptr)
                *pathError = QString::fromUtf8 ("生成场景依赖不存在：%1。").arg (absolutePath);
            return false;
        }
        relativePath = QDir::fromNativeSeparators (
            projectDirectory.relativeFilePath (info.absoluteFilePath ()));
        if (relativePath == QStringLiteral ("..") ||
            relativePath.startsWith (QStringLiteral ("../"))) {
            if (pathError != nullptr)
                *pathError = QString::fromUtf8 ("生成场景文件位于项目目录之外：%1（项目：%2）。")
                                 .arg (absolutePath, projectRoot);
            return false;
        }
        return true;
    };

    const QString mainResourceId = mainWorkCellResourceId ();
    ProjectResource original;
    const bool firstPromotion = mainResourceId.isEmpty ();
    if (!firstPromotion &&
        !_projectManager.manifest ().findResource (mainResourceId, original)) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("当前项目没有可晋升的 mainWorkCell 资源。");
        return false;
    }

    ProjectResource promoted = original;
    if (firstPromotion) {
        promoted.id = QStringLiteral ("scene.main");
        promoted.kind = QStringLiteral ("robwork.workcell");
    }
    if (!managedRelativePath (sceneInfo.absoluteFilePath (), promoted.path, error))
        return false;
    promoted.ownership = QStringLiteral ("generated");
    promoted.required = true;
    promoted.dependencies.clear ();

    QVector< ProjectResource > assets;
    for (const QString& dependencyPath : dependencyFilePaths) {
        QString relativePath;
        if (!managedRelativePath (dependencyPath, relativePath, error))
            return false;
        ProjectResource asset;
        asset.id = generatedSceneAssetId (dependencyPath, relativePath);
        asset.kind = QStringLiteral ("robwork.passive-asset");
        asset.path = relativePath;
        asset.ownership = QStringLiteral ("generated");
        asset.required = true;
        assets.push_back (asset);
        if (!promoted.dependencies.contains (asset.id))
            promoted.dependencies.push_back (asset.id);
    }

    // 首次晋升时保留最初导入的 WorkCell，避免项目保存后失去原始工程来源。它不再是入口，
    // 但仍作为受管被动资产参与 clone/rwpack。
    if (!firstPromotion && original.ownership == QStringLiteral ("project") &&
        original.path != promoted.path) {
        ProjectResource sourceAsset;
        sourceAsset.id = QStringLiteral ("scene.source.original");
        sourceAsset.kind = QStringLiteral ("robwork.passive-asset");
        sourceAsset.path = original.path;
        sourceAsset.ownership = QStringLiteral ("project");
        sourceAsset.required = true;
        sourceAsset.dependencies = original.dependencies;
        assets.push_back (sourceAsset);
    }

    // 先让原 WorkCell Provider 用同一资源 ID 加载新路径。此步骤失败时 Registry 会尝试
    // 恢复旧资源；只有活动场景可用后才修改内存清单。
    if (firstPromotion) {
        if (!_projectDocuments.loadNewResource (
                promoted, _projectManager.manifest (), _projectManager.projectFilePath (), error))
            return false;
        if (!_projectManager.addMainWorkCellAndAssets (promoted, assets, error)) {
            _projectDocuments.unloadResource (promoted.id);
            setWorkCell (emptyWorkCell ());
            return false;
        }
    }
    else if (!_projectDocuments.reloadResource (
                 promoted, _projectManager.manifest (), _projectManager.projectFilePath (), error)) {
        return false;
    }
    else if (!_projectManager.replaceResourceAndReconcileGeneratedSceneAssets (
                 promoted, assets, error)) {
        QString ignored;
        _projectDocuments.reloadResource (
            original, _projectManager.manifest (), _projectManager.projectFilePath (), &ignored);
        return false;
    }

    updateProjectWindowTitle ();
    return true;
}

QString RobWorkStudio::mainWorkCellResourceId () const
{
    // entryPoints 是项目主场景的唯一权威来源，不能依赖 resources 数组顺序推测。
    if (!_projectManager.hasProject ())
        return QString ();
    return _projectManager.manifest ().entryPoints.value (QStringLiteral ("mainWorkCell"));
}

void RobWorkStudio::configureWorkflowDockLayout ()
{
    if (_workflowDockLayoutController == nullptr)
        _workflowDockLayoutController = std::make_unique< WorkflowDockLayoutController > (this);
    _workflowDockLayoutController->applyLayout ();
    _workflowDockLayoutStartupPending = _workflowDockLayoutController->hasPendingInitialWidth ();
}

void RobWorkStudio::notifyWorkflowRobotModelLoaded (const QString& filename)
{
    if (_workflowDockLayoutController == nullptr)
        _workflowDockLayoutController = std::make_unique< WorkflowDockLayoutController > (this);
    _workflowDockLayoutController->notifyRobotModelLoaded (filename);
}

QString RobWorkStudio::activeWorkflowDockName () const
{
    if (_workflowDockLayoutController == nullptr)
        return QString ();
    return _workflowDockLayoutController->activeDockName ();
}

QString rws::robotProjectWorkCellReadinessError (const RobWorkStudio* studio)
{
    if (studio == nullptr || studio->projectDirectory ().isEmpty () ||
        !studio->mainWorkCellResourceId ().isEmpty ())
        return QString ();

    return QStringLiteral (
        "The robot project has not generated its managed WorkCell. Review the model in "
        "RobotModelBuilder and run Save and Load first.");
}

void RobWorkStudio::notifyProjectDocumentChanged ()
{
    // 不在这里调用保存或修改清单。Provider 的 isDirty 由 Registry 聚合，标题栏只是
    // 可视反馈；真正写入仍必须经过多文件暂存事务，避免一次控件编辑绕过失败回滚。
    updateProjectWindowTitle ();
}

bool RobWorkStudio::hasUnsavedProjectChanges () const
{
    return _projectManager.isDirty () || _projectDocuments.isDirty ();
}

bool RobWorkStudio::saveCurrentProject (QString* error)
{
    if (!saveProjectInternal (error))
        return false;
    updateProjectWindowTitle ();
    if (error != nullptr)
        error->clear ();
    return true;
}

bool RobWorkStudio::confirmSaveBeforeProjectResourceRead (QWidget* parent)
{
    if (!hasUnsavedProjectChanges ())
        return true;

    QMessageBox box (QMessageBox::Warning,
                     tr ("Unsaved Project Changes"),
                     tr ("The managed project resources contain unsaved changes."),
                     QMessageBox::Save | QMessageBox::Cancel,
                     parent != nullptr ? parent : this);
    box.setInformativeText (
        tr ("Save the complete project transaction before continuing with this import?"));
    box.setDefaultButton (QMessageBox::Save);
    if (QAbstractButton* saveButton = box.button (QMessageBox::Save))
        saveButton->setText (tr ("Save and Continue"));
    if (box.exec () != QMessageBox::Save)
        return false;

    QString error;
    if (saveCurrentProject (&error))
        return true;

    QMessageBox::warning (parent != nullptr ? parent : this,
                          tr ("Save Project Failed"),
                          error.isEmpty () ? tr ("The project transaction could not be saved.")
                                           : error);
    return false;
}

QString RobWorkStudio::projectDirectory () const
{
    // 项目资源解析器以 .rwproj 所在目录为边界，这里返回同一目录作为插件生成物的唯一
    // 合法根；无项目时明确返回空，调用方可切换到独立 WorkCell 工作流。
    if (!_projectManager.hasProject ())
        return QString ();
    return QFileInfo (_projectManager.projectFilePath ()).absolutePath ();
}

// 保存项目：把当前清单写回 .rwproj 文件；失败时弹出警告并保留原上下文。
void RobWorkStudio::saveProject ()
{
    QString error;
    if (!saveCurrentProject (&error)) {
        QMessageBox::warning (this, tr ("Save Project Failed"), error);
        return;
    }
}

// 统一的“保存项目”实现：先提交全部脏业务文档，最后写项目清单。
// 菜单槽 saveProject、关闭确认和窗口关闭事件都走这里，保证三种路径行为一致。
bool RobWorkStudio::saveProjectInternal (QString* error)
{
    if (!_projectManager.hasProject ()) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("当前没有打开的项目。");
        return false;
    }

    // 先提交所有业务文档，最后写项目清单。清单是本次保存的提交点，只有资源文件
    // 全部成功后才更新时间戳并清除清单脏状态。
    if (!_projectDocuments.saveDirtyResources (
            _projectManager.manifest (), _projectManager.projectFilePath (), error))
        return false;
    if (!_projectManager.saveProject (error))
        return false;
    return _projectManager.discardAutosaveSnapshot (error);
}

// 项目关闭门禁：无项目直接放行；有项目时先检查 Provider 是否允许关闭，再聚合
// 清单与文档的脏状态。有未保存修改则弹出 保存/放弃/取消 三选对话框。
// 返回 true 表示调用方可以继续关闭项目。
bool RobWorkStudio::confirmProjectClose ()
{
    if (!_projectManager.hasProject ())
        return true;

    QString reason;
    if (!_projectDocuments.canClose (&reason)) {
        QMessageBox::warning (this,
                              tr ("Project Cannot Be Closed"),
                              reason.isEmpty () ? tr ("A project document is busy.") : reason);
        return false;
    }

    if (!_projectManager.isDirty () && !_projectDocuments.isDirty ()) {
        QString error;
        if (_projectManager.discardAutosaveSnapshot (&error))
            return true;
        QMessageBox::warning (this, tr ("Discard Recovery Snapshot Failed"), error);
        return false;
    }

    QMessageBox box (QMessageBox::Warning,
                     tr ("Unsaved Project Changes"),
                     tr ("The project contains unsaved changes."),
                     QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                     this);
    box.setInformativeText (tr ("Save all modified project documents before closing?"));
    box.setDefaultButton (QMessageBox::Save);
    const QMessageBox::StandardButton choice =
        static_cast< QMessageBox::StandardButton > (box.exec ());
    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Discard) {
        QString error;
        if (_projectManager.discardAutosaveSnapshot (&error))
            return true;
        QMessageBox::warning (this, tr ("Discard Recovery Snapshot Failed"), error);
        return false;
    }

    QString error;
    if (!saveProjectInternal (&error)) {
        QMessageBox::warning (this, tr ("Save Project Failed"), error);
        return false;
    }
    return true;
}

// 关闭项目文档上下文：先让 Registry 按依赖逆序关闭各业务文档，再清空清单。
// 顺序固定——Provider 的 closeResource 依赖清单仍有效时才能按 ID 释放自身文档。
void RobWorkStudio::closeProjectDocuments ()
{
    // Registry 必须先按依赖逆序关闭文档，再清空清单；Provider 的 closeResource
    // 仍需要通过资源 ID 判断自己当前持有的文档。
    _projectDocuments.closeResources ();
    _projectManager.closeProject ();
}

// 关闭项目：同时清空项目上下文与当前 WorkCell，并刷新窗口标题。
void RobWorkStudio::closeProject ()
{
    if (!confirmProjectClose ())
        return;
    closeProjectDocuments ();
    closeWorkCell ();
    updateProjectWindowTitle ();
}

void RobWorkStudio::newWorkCell ()
{
    // 菜单中的“新建空 WorkCell”表示用户明确离开项目文档模式。先完成项目保存决策，
    // 用户取消时保持项目、Provider 和当前场景完全不变；确认后再创建独立内存文档。
    if (!leaveProjectForStandaloneWorkCell ())
        return;
    createEmptyWorkCell ();
    updateProjectWindowTitle ();
}

void RobWorkStudio::createEmptyWorkCell ()
{
    try {
        closeWorkCell ();
        // Empty workcell constructed.
        _workcell = emptyWorkCell ();
        _state    = _workcell->getDefaultState ();
        _detector = makeCollisionDetector (_workcell);
        // Workcell given to view.
        _view->setWorkCell (_workcell);
        _view->setState (_state);
    }
    catch (const Exception& exp) {
        QMessageBox::critical (
            this,
            tr ("RobWorkStudio"),
            tr ("Caught exception while trying to create new work cell: %1").arg (exp.what ()));
    }

    // Workcell sent to plugins.
    openAllPlugins ();
    updateHandler ();
}

void RobWorkStudio::saveWorkCell ()
{
    if (_workcell != nullptr) {
        std::string wcFilePath =
            static_cast< std::string > (_workcell->getPropertyMap ().get< std::string > (
                rw::loaders::XMLRWLoader::getWorkCellFileNameId ()));
        QString wcFileName = QFileDialog::getSaveFileName (this,
                                                           tr ("Save Workcell"),
                                                           QString::fromStdString (wcFilePath),
                                                           tr ("RobWork Workcell (*.wc.xml)"));

        rw::loaders::DOMWorkCellSaver::save (_workcell, _state, wcFileName.toStdString ());
    }
}

void RobWorkStudio::reloadWorkCell ()
{
    try {
        if (_workcell->getFilename ().size () > 1) {
            Log::infoLog () << "reloading: " << _workcell->getFilename () << "\n";
            openWorkCellFile (QString (_workcell->getFilename ().c_str ()));
        }
    }
    catch (const rw::core::Exception& exp) {
        QMessageBox::information (
            NULL, "Exception", exp.getMessage ().getText ().c_str (), QMessageBox::Ok);
    }
}

void RobWorkStudio::dragMoveEvent (QDragMoveEvent* event)
{
    event->accept ();
}

void RobWorkStudio::dragEnterEvent (QDragEnterEvent* event)
{
    if (event->mimeData ()->hasText ()) {
        event->acceptProposedAction ();
    }
    else if (event->mimeData ()->hasHtml ()) {
        event->acceptProposedAction ();
    }
    else if (event->mimeData ()->hasUrls ()) {
        event->acceptProposedAction ();
    }
    else {
        event->ignore ();
    }
}

void RobWorkStudio::dropEvent (QDropEvent* event)
{
    if (event->mimeData ()->hasUrls ()) {
        QList< QUrl > urls = event->mimeData ()->urls ();
        if (urls.size () == 1) {
            openFile (urls[0].toLocalFile ().toStdString ());
        }
    }
    else if (event->mimeData ()->hasHtml ()) {
        Log::debugLog () << "html dropped: " << std::endl;
    }
    else if (event->mimeData ()->hasText ()) {
        QString text = event->mimeData ()->text ();
        Log::infoLog () << text.toStdString () << std::endl;
    }
    else {
        event->ignore ();
    }
}

void RobWorkStudio::openFile (const std::string& file)
{
    // We change directory irrespective of whether we can load open the file or
    // not. We change directory also if openFile() was called because of a drop
    // event.
    if (!file.empty ()) {
        _settingsMap->set< std::string > ("PreviousOpenDirectory",
                                          StringUtil::getDirectoryName (file));
    }

    try {
        const QString filename (file.c_str ());
        // std::cout << filename.toStdString() << std::endl;

        if (!filename.isEmpty ()) {
            std::vector< std::string > lastfiles = _settingsMap->get< std::vector< std::string > > (
                "LastOpennedFiles", std::vector< std::string > ());
            lastfiles.push_back (file);
            if (filename.endsWith (".rwproj", Qt::CaseInsensitive)) {
                // 项目文件优先于通用资源分派。打开成功后沿用现有最近文件机制，
                // 让菜单、命令行和拖放入口共享完全相同的项目加载行为。
                QString projectError;
                if (!openProjectFile (filename, &projectError)) {
                    // 用户在未保存提示中选择“取消”时 error 为空，不应再显示一次失败
                    // 对话框；真正的格式、路径或 Provider 错误仍需明确报告。
                    if (!projectError.isEmpty ())
                        QMessageBox::critical (this, tr ("Open Project Failed"), projectError);
                }
                else {
                    _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles",
                                                                      lastfiles);
                    updateLastFiles ();
                }
            }
            else if (filename.endsWith (".STL", Qt::CaseInsensitive) ||
                filename.endsWith (".STLA", Qt::CaseInsensitive) ||
                filename.endsWith (".STLB", Qt::CaseInsensitive) ||
#if RW_HAVE_ASSIMP
                filename.endsWith (".DAE", Qt::CaseInsensitive) ||
#endif
                filename.endsWith (".3DS", Qt::CaseInsensitive) ||
                filename.endsWith (".AC", Qt::CaseInsensitive) ||
                filename.endsWith (".AC3D", Qt::CaseInsensitive) ||
                filename.endsWith (".TRI", Qt::CaseInsensitive) ||
                filename.endsWith (".OBJ", Qt::CaseInsensitive)) {
                Log::infoLog () << "Opening drawable file: " << filename.toStdString () << "\n";
                openDrawable (filename);
                _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles", lastfiles);
                updateLastFiles ();
            }
            else if (filename.endsWith (".WC", Qt::CaseInsensitive) ||
                     filename.endsWith (".XML", Qt::CaseInsensitive)) {
                Log::infoLog () << "Opening workcell file: " << filename.toStdString () << "\n";
                // 独立 WorkCell 会替换当前项目的入口文档，因此必须先通过统一项目关闭
                // 门禁。用户取消或文件加载失败时不应写入最近文件列表。
                if (openStandaloneWorkCellFile (filename)) {
                    _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles",
                                                                      lastfiles);
                    updateLastFiles ();
                }
            }
            else if (filename.endsWith (".rwplay", Qt::CaseInsensitive) |
                     filename.endsWith (".csv", Qt::CaseInsensitive)) {
                // Log::infoLog() << "The RobWorkStudio::OpenFile() function can't load playback
                // files\n";
                RW_THROW ("The RobWorkStudio::OpenFile() function can't load playback files");
            }
            else {
                // we try openning a workcell
                openStandaloneWorkCellFile (filename);
            }
        }
    }
    catch (const rw::core::Exception& exp) {
        QMessageBox::information (
            NULL, "Exception", exp.getMessage ().getText ().c_str (), QMessageBox::Ok);
    }
    // std::cout << "Update handler!" << std::endl;
    updateHandler ();
}

void RobWorkStudio::open ()
{
    QString selectedFilter;

    std::string previousOpenDirectory =
        _settingsMap->get< std::string > ("PreviousOpenDirectory", "");
    const QString dir (previousOpenDirectory.c_str ());

    QString assimpExtensions = "";
#if RW_HAVE_ASSIMP
    assimpExtensions = " * .dae";
#endif

    QString filename = QFileDialog::getOpenFileName (
        this,
        "Open Project, WorkCell or Drawable",    // Title
        dir,                            // Directory
        "All supported ( *.rwproj *.wu *.wc *.xml *.wc.xml *.dev *.stl *.stla *.stlb *.3ds *.ac *.ac3d "
        "*.obj" +
            assimpExtensions +
            ")"
            "\nRW XML files ( *.wc.xml *.xml *.wc)"
            "\nDrawables ( *.stl *.stla *.stlb *.3ds *.ac *.ac3d *.obj" +
            assimpExtensions +
            ")"
            "\n All ( *.* )",
        &selectedFilter);

    openFile (filename.toStdString ());
}

void RobWorkStudio::openDrawable (const QString& filename)
{
    try {
        _view->getWorkCellScene ()->addDrawable (filename.toStdString (),
                                                 _workcell->getWorldFrame ());
    }
    catch (...) {
        const std::string msg = "Failed to load " + filename.toStdString () + " as a Drawable";
        QMessageBox::information (this, "Error", msg.c_str (), QMessageBox::Ok);
    }
}

// 项目 Provider 的加载回调：确认 WorkCell 文件存在后调用 tryOpenWorkCellFile
// 真正打开场景。成功返回 true；文件不存在或加载失败经 error 回填原因。
bool RobWorkStudio::loadWorkCellProjectResource (const QString& filename, QString* error)
{
    if (!QFileInfo::exists (filename)) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("WorkCell 文件不存在：%1。").arg (filename);
        return false;
    }
    if (!tryOpenWorkCellFile (filename)) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("WorkCell 加载失败：%1。").arg (filename);
        return false;
    }
    return true;
}

// 项目 Provider 的保存回调：把当前 WorkCell 与 State 用 DOMWorkCellSaver 写入
// 指定路径（保存事务分配的暂存文件）。捕获 RobWork 与标准库异常并回填中文错误。
bool RobWorkStudio::saveWorkCellProjectResource (const QString& filename, QString* error)
{
    if (_workcell == nullptr) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("当前没有可保存的 WorkCell。");
        return false;
    }

    try {
        // Provider 只接收保存事务生成的暂存路径；DOMWorkCellSaver 不会直接覆盖正式
        // 项目资源，正式替换由 ProjectSaveTransaction 负责。
        rw::loaders::DOMWorkCellSaver::save (_workcell, _state, filename.toStdString ());
        return true;
    }
    catch (const rw::core::Exception& exception) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("WorkCell 保存失败（目标路径：%1）：%2。").arg (
                filename, QString::fromStdString (exception.getMessage ().getText ()));
    }
    catch (const std::exception& exception) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("WorkCell 保存失败（目标路径：%1）：%2。").arg (
                filename, QString::fromUtf8 (exception.what ()));
    }
    return false;
}

// 实际打开项目文件：先让项目管理器读取并校验清单，再按 mainWorkCell 入口决定
// 加载真实场景还是创建内存 WorkCell。任一环节失败即返回 false 并回填错误。
bool RobWorkStudio::openProjectFile (const QString& filename, QString* error)
{
    QScopedValueRollback< int > projectTransition (
        _projectTransitionDepth, _projectTransitionDepth + 1);
    if (!confirmProjectClose ())
        return false;
    if (!_projectManager.openProject (filename, error))
        return false;

    // 崩溃恢复提示：打开项目时若发现自动保存快照，让用户选择恢复、放弃或取消打开。
    // 恢复失败/取消都会关闭刚打开的项目上下文，保证不会带着不完整状态继续工作。
    if (_projectManager.hasAutosaveSnapshot ()) {
        QMessageBox recovery (QMessageBox::Question,
                               tr ("Recovery Snapshot Available"),
                               tr ("A recovery snapshot is available for this project."),
                               QMessageBox::NoButton,
                               this);
        QPushButton* restoreButton = recovery.addButton (tr ("Restore"), QMessageBox::AcceptRole);
        QPushButton* discardButton = recovery.addButton (tr ("Discard"), QMessageBox::DestructiveRole);
        QPushButton* cancelButton = recovery.addButton (QMessageBox::Cancel);
        recovery.setDefaultButton (restoreButton);
        recovery.exec ();
        if (recovery.clickedButton () == cancelButton) {
            _projectManager.closeProject ();
            return false;
        }
        if (recovery.clickedButton () == restoreButton) {
            if (!_projectManager.restoreAutosaveSnapshot (error)) {
                _projectManager.closeProject ();
                return false;
            }
        }
        else if (recovery.clickedButton () == discardButton &&
                 !_projectManager.discardAutosaveSnapshot (error)) {
            _projectManager.closeProject ();
            return false;
        }
    }

    // 清单已通过结构、路径和 required 资源检查后，才关闭旧文档并加载新文档。
    // 这样普通的文件选择错误不会破坏当前已打开的项目。
    _projectDocuments.closeResources ();
    closeWorkCell ();

    QStringList loadWarnings;
    if (!_projectDocuments.loadProjectResources (
            _projectManager.manifest (), _projectManager.projectFilePath (), error, &loadWarnings)) {
        _projectDocuments.closeResources ();
        _projectManager.closeProject ();
        // 项目资源加载失败后只恢复可用的空场景；此时项目上下文已经显式关闭，
        // 不应再次进入用户交互式的项目退出流程。
        createEmptyWorkCell ();
        updateProjectWindowTitle ();
        return false;
    }

    const QString workCellResourceId =
        _projectManager.manifest ().entryPoints.value (QStringLiteral ("mainWorkCell"));
    if (workCellResourceId.isEmpty ()) {
        // 空项目是合法状态。创建一个内存 WorkCell 可保持现有插件和三维视图可用，
        // 同时不在用户未确认前擅自生成场景文件。
        // 空项目仍处于项目模式，只是暂时没有 mainWorkCell 入口。因此只能创建内部
        // 内存场景，不能调用会退出项目的 newWorkCell 菜单槽。
        createEmptyWorkCell ();
    }

    updateProjectWindowTitle ();
    if (!loadWarnings.isEmpty ())
        QMessageBox::warning (this, tr ("Project Opened With Warnings"), loadWarnings.join (QStringLiteral ("\n")));
    return true;
}

// 刷新主窗口标题：有项目时显示“项目名[*] - ”，再统一附加版本号。
// 脏标记 * 反映清单保存后又有未保存改动。
void RobWorkStudio::updateProjectWindowTitle ()
{
    QString title;
    if (_projectManager.hasProject ()) {
        title = _projectManager.manifest ().project.name;
        if (_projectManager.isDirty () || _projectDocuments.isDirty ())
            title += QStringLiteral ("*");
        title += QStringLiteral (" - ");
    }
    title += QStringLiteral ("RobWorkStudio v") + QString::fromLatin1 (RW_VERSION);
    setWindowTitle (title);

    // 标题刷新恰好覆盖项目创建、打开、关闭和另存为后的所有稳定状态。信号由插件自行
    // 去重处理，因此即使普通脏状态刷新重复发出，也不会重复创建目录或改变模型内容。
    Q_EMIT projectContextChanged (projectDirectory ());
}

void RobWorkStudio::openWorkCellFile (const QString& filename)
{
    // 保留既有 void 符号，避免已经编译的静态/动态插件因方法签名变化产生链接失败。
    // 项目 Provider 需要加载结果时，调用新增的 tryOpenWorkCellFile。
    tryOpenWorkCellFile (filename);
}

// 打开 WorkCell 的内部实现，返回加载是否成功（失败时回退为空场景）。
// 与旧 openWorkCellFile 的区别是带返回值，供项目 Provider 判断加载结果。
bool RobWorkStudio::tryOpenWorkCellFile (const QString& filename)
{
    // Always close the workcell.
    closeWorkCell ();
    // rw::graphics::WorkCellScene::Ptr wcsene = _view->makeWorkCellScene();

    WorkCell::Ptr wc;

    bool loadedSuccessfully = true;
    try {
        wc = WorkCellLoader::Factory::load (filename.toStdString ());
        if (wc == NULL) {
            RW_THROW ("Loading of workcell failed!");
        }
    }
    catch (const std::exception& e) {
        const std::string msg = "Failed to load workcell: " + filename.toStdString () + ". \n " +
                                std::string (e.what ());
        QMessageBox::information (this, "Error", msg.c_str (), QMessageBox::Ok);
        wc = emptyWorkCell ();
        loadedSuccessfully = false;
    }

    // std::cout<<"Number of devices in workcell in RobWorkStudio::setWorkCell:
    // "<<workcell->getDevices().size()<<std::endl;
    // don't set any variables before we know they are good

    CollisionDetector::Ptr detector = makeCollisionDetector (wc);
    _workcell                       = wc;
    _state                          = _workcell->getDefaultState ();
    _detector                       = detector;
    _view->setWorkCell (wc);
    _view->setState (_state);

    openAllPlugins ();
    Q_EMIT activeWorkCellChanged ();
    return loadedSuccessfully;
}

bool RobWorkStudio::leaveProjectForStandaloneWorkCell ()
{
    if (!_projectManager.hasProject ())
        return true;

    // confirmProjectClose 同时检查 Provider 的 canClose、聚合脏状态并处理
    // Save/Discard/Cancel。只有返回 true 才允许破坏当前项目上下文。
    if (!confirmProjectClose ())
        return false;

    // Provider 必须在 ProjectManager 清单仍然有效时按依赖逆序收到 closeResource；
    // closeProjectDocuments 已固定执行顺序，因此这里不直接分别清理两个对象。
    closeProjectDocuments ();
    return true;
}

bool RobWorkStudio::openStandaloneWorkCellFile (const QString& filename)
{
    if (!leaveProjectForStandaloneWorkCell ())
        return false;

    // 项目上下文解除后再调用旧加载入口，确保 WorkCell 状态变化不会继续被原项目
    // Provider 记为脏。无论加载成功还是回退为空场景，标题都应反映独立文档模式。
    const bool loadedSuccessfully = tryOpenWorkCellFile (filename);
    updateProjectWindowTitle ();
    return loadedSuccessfully;
}

void RobWorkStudio::setWorkcell (rw::models::WorkCell::Ptr workcell)
{
    // Always close the workcell.
    if (_workcell && workcell != _workcell) {
        closeWorkCell ();
    }

    // Open a new workcell if there is one.<
    if (workcell && workcell != _workcell) {
        // don't set any variables before we know they are good
        CollisionDetector::Ptr detector = makeCollisionDetector (workcell);
        _workcell                       = workcell;
        _state                          = _workcell->getDefaultState ();
        _detector                       = detector;
        _view->setWorkCell (_workcell);
        _view->setState (_state);
        openAllPlugins ();
        Q_EMIT activeWorkCellChanged ();

        double scale = this->calculateWorkCellSize ().diagonal ().norm2 ();
        // set maximum zoom scale at 2m and minimum at 20cm
        scale = std::min (std::min (0.1, scale / 2.0), 1.0);
        if (_propMap.has ("ZoomScale")) {
            _propMap.set ("ZoomScale", scale);
        }
        else {
            _propMap.add ("ZoomScale","value [0-1] scaling the zoom factor of the cameracontroller", scale);
        }
    }
}

rw::models::WorkCell::Ptr RobWorkStudio::getWorkcell ()
{
    return _workcell;
}

void RobWorkStudio::closeWorkCell ()
{
    _workcell = nullptr;
    _detector = nullptr;
    _state    = State ();
    // Clear everything from the view
    _view->clear ();

    // Call close on all modules
    closeAllPlugins ();

    updateHandler ();
    Q_EMIT activeWorkCellChanged ();
}

void RobWorkStudio::showSolidTriggered ()
{
    updateHandler ();
}

void RobWorkStudio::showWireTriggered ()
{
    updateHandler ();
}

void RobWorkStudio::showBothTriggered ()
{
    updateHandler ();
}

void RobWorkStudio::updateViewHandler ()
{
    _view->update ();
}

void RobWorkStudio::updateHandler ()
{
    update ();
    _view->update ();
}

void RobWorkStudio::setTStatePath (rw::trajectory::TimedStatePath path)
{
    _timedStatePath = ownedPtr (new rw::trajectory::TimedStatePath (path));
    stateTrajectoryChangedEvent ().fire (*_timedStatePath);
    stateTrajectoryPtrChangedEvent ().fire (_timedStatePath);
}

namespace {

class RobWorkStudioEvent : public QEvent
{
  public:
    static const QEvent::Type SetStateEvent          = (QEvent::Type) 1200;
    static const QEvent::Type SetTimedStatePathEvent = (QEvent::Type) 1201;
    static const QEvent::Type UpdateAndRepaintEvent  = (QEvent::Type) 1202;
    static const QEvent::Type SaveViewGLEvent        = (QEvent::Type) 1203;
    static const QEvent::Type ExitEvent              = (QEvent::Type) 1204;
    static const QEvent::Type SetWorkCell            = (QEvent::Type) 1205;
    static const QEvent::Type OpenWorkCell           = (QEvent::Type) 1206;
    static const QEvent::Type CloseWorkCell          = (QEvent::Type) 1207;
    static const QEvent::Type GenericEvent           = (QEvent::Type) 1208;
    static const QEvent::Type GenericAnyEvent        = (QEvent::Type) 1209;

    boost::any _anyData;
    rw::core::Ptr< bool > _hs;

    RobWorkStudioEvent (QEvent::Type type, rw::core::Ptr< bool > hs, boost::any adata) :
        QEvent (type), _anyData (adata), _hs (hs)
    {}

    RobWorkStudioEvent (QEvent::Type type, rw::core::Ptr< bool > hs) :
        QEvent (type), _anyData (NULL), _hs (hs)
    {}

    virtual ~RobWorkStudioEvent ()
    {
        Log::debugLog () << "RobWorkStudioEvent: destruct" << std::endl;
        done ();
    }

    void done ()
    {
        if (_hs != NULL) {
            Log::debugLog () << "Done: " << std::endl;
            Log::debugLog () << "set hs " << std::endl;
            *_hs = true;
            Log::debugLog () << "hs " << *_hs << std::endl;
        }
        else {
            Log::debugLog () << "Done: hs==NULL" << std::endl;
        }
    }
};

class RobWorkStudioEventHS
{
  public:
    RobWorkStudioEventHS (QEvent::Type type, boost::any adata) : _hs (ownedPtr (new bool (false)))
    {
        event = new RobWorkStudioEvent (type, _hs, adata);
    }

    RobWorkStudioEventHS (QEvent::Type type) : _hs (ownedPtr (new bool (false)))
    {
        event = new RobWorkStudioEvent (type, _hs);
    }

    void wait ()
    {
        int cnt = 0;
        while (_hs != NULL && *_hs == false && cnt < 100) {
            TimerUtil::sleepMs (5);
            cnt++;
        }
    }

    rw::core::Ptr< bool > _hs;
    RobWorkStudioEvent* event;
};

}    // namespace

void RobWorkStudio::setTimedStatePath (const rw::trajectory::TimedStatePath& path)
{
    _timedStatePath = ownedPtr (new rw::trajectory::TimedStatePath (path));
    stateTrajectoryChangedEvent ().fire (*_timedStatePath);
    stateTrajectoryPtrChangedEvent ().fire (_timedStatePath);
}

void RobWorkStudio::setTimedStatePath (const rw::trajectory::TimedStatePath::Ptr path)
{
    _timedStatePath = path;
    stateTrajectoryChangedEvent ().fire (*_timedStatePath);
    stateTrajectoryPtrChangedEvent ().fire (_timedStatePath);
}

void RobWorkStudio::setState (const rw::kinematics::State& state)
{
    _state = state;
    _view->setState (state);

    // WorkCell XML 保存器会持久化当前 State，因此 JOG、拖动或插件调用 setState 后，
    // 已加载的项目 WorkCell 应进入脏状态。Provider 未绑定资源时 markDirty 是空操作，
    // 不会把项目外临时打开的 WorkCell 错误纳入项目保存。
    if (_workCellProvider != nullptr) {
        _workCellProvider->markDirty ();
        updateProjectWindowTitle ();
    }
    updateHandler ();
    stateChangedEvent ().fire (_state);
}

void RobWorkStudio::postTimedStatePath (const rw::trajectory::TimedStatePath& path)
{
    RobWorkStudioEventHS* event =
        new RobWorkStudioEventHS (RobWorkStudioEvent::SetTimedStatePathEvent, path);
    QApplication::postEvent (this, event->event);
    event->wait ();
}

void RobWorkStudio::postExit ()
{
    RobWorkStudioEventHS* event = new RobWorkStudioEventHS (RobWorkStudioEvent::ExitEvent, NULL);
    QApplication::postEvent (this, event->event);
    // event->wait();
}

void RobWorkStudio::postState (const rw::kinematics::State& state)
{
    RobWorkStudioEventHS* event =
        new RobWorkStudioEventHS (RobWorkStudioEvent::SetStateEvent, state);
    QApplication::postEvent (this, event->event);
    event->wait ();
}

void RobWorkStudio::postUpdateAndRepaint ()
{
    RobWorkStudioEventHS* event =
        new RobWorkStudioEventHS (RobWorkStudioEvent::UpdateAndRepaintEvent);
    QApplication::postEvent (this, event->event);
    event->wait ();
}

void RobWorkStudio::postSaveViewGL (const std::string& filename)
{
    RobWorkStudioEventHS* event =
        new RobWorkStudioEventHS (RobWorkStudioEvent::SaveViewGLEvent, filename);
    QApplication::postEvent (this, event->event);
    event->wait ();
}

void RobWorkStudio::postWorkCell (rw::models::WorkCell::Ptr workcell)
{
    RobWorkStudioEventHS* event =
        new RobWorkStudioEventHS (RobWorkStudioEvent::SetWorkCell, workcell);
    QApplication::postEvent (this, event->event);
    event->wait ();
}

void RobWorkStudio::postOpenWorkCell (const std::string& filename)
{
    RobWorkStudioEventHS* event =
        new RobWorkStudioEventHS (RobWorkStudioEvent::OpenWorkCell, filename);
    QApplication::postEvent (this, event->event);
    event->wait ();
}

void RobWorkStudio::postCloseWorkCell ()
{
    RobWorkStudioEventHS* event =
        new RobWorkStudioEventHS (RobWorkStudioEvent::CloseWorkCell, NULL);
    QApplication::postEvent (this, event->event);
    event->wait ();
}

void RobWorkStudio::postGenericEvent (const std::string& id)
{
    RobWorkStudioEventHS* event = new RobWorkStudioEventHS (RobWorkStudioEvent::GenericEvent, id);
    QApplication::postEvent (this, event->event);
    event->wait ();
}

void RobWorkStudio::postGenericAnyEvent (const std::string& id, boost::any data)
{
    RobWorkStudioEventHS* event =
        new RobWorkStudioEventHS (RobWorkStudioEvent::GenericAnyEvent, std::make_pair (id, data));
    QApplication::postEvent (this, event->event);
    event->wait ();
}

bool RobWorkStudio::event (QEvent* event)
{
    if (event->type () == QEvent::LayoutRequest) {
        const bool handled = QMainWindow::event (event);
        if (_workflowDockLayoutStartupPending && isVisible () &&
            _workflowDockLayoutController != nullptr) {
            _workflowDockLayoutController->finalizeInitialWidth ();
            _workflowDockLayoutStartupPending = false;
        }
        return handled;
    }

    // WARNING: only use this pointer if you know its the right type
    RobWorkStudioEvent* rwse = static_cast< RobWorkStudioEvent* > (event);
    if (event->type () == RobWorkStudioEvent::SetStateEvent) {
        State state = boost::any_cast< State > (rwse->_anyData);
        setState (state);
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::SetTimedStatePathEvent) {
        TimedStatePath tstate = boost::any_cast< TimedStatePath > (rwse->_anyData);
        setTimedStatePath (tstate);
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::UpdateAndRepaintEvent) {
        updateAndRepaint ();
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::SaveViewGLEvent) {
        std::string str = boost::any_cast< std::string > (rwse->_anyData);
        try {
            saveViewGL (QString (str.c_str ()));
        }
        catch (const Exception& exp) {
            QMessageBox::critical (
                NULL,
                "Save View",
                tr ("Failed to grab and save view with message '%1'").arg (exp.what ()));
        }
        catch (std::exception& e) {
            QMessageBox::critical (
                NULL,
                "Save View",
                tr ("Failed to grab and save view with message '%1'").arg (e.what ()));
        }
        catch (...) {
            QMessageBox::critical (NULL, "Save View", tr ("Failed to grab and save view"));
        }
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::SetWorkCell) {
        WorkCell::Ptr wc = boost::any_cast< WorkCell::Ptr > (rwse->_anyData);
        setWorkCell (wc);
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::OpenWorkCell) {
        std::string str = boost::any_cast< std::string > (rwse->_anyData);
        openWorkCellFile (str.c_str ());
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::CloseWorkCell) {
        onCloseWorkCell ();
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::GenericEvent) {
        std::string id = boost::any_cast< std::string > (rwse->_anyData);
        this->genericEvent ().fire (id);
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::GenericAnyEvent) {
        std::pair< std::string, boost::any > data =
            boost::any_cast< std::pair< std::string, boost::any > > (rwse->_anyData);
        this->genericAnyEvent ().fire (data.first, data.second);
        rwse->done ();
        return true;
    }
    else if (event->type () == RobWorkStudioEvent::ExitEvent) {
        onCloseWorkCell ();
        rwse->done ();
        close ();
        return true;
    }
    else if (event->type () == QEvent::Close) {
        QApplication::closeAllWindows ();
    }
    else {
        // event->ignore();
    }

    return QMainWindow::event (event);
}

void RobWorkStudio::saveViewGL (const QString& filename)
{
    _view->saveBufferToFile (filename);
}
namespace {
class AnyEventListener
{
  public:
    AnyEventListener (const std::string& myid) : _id (myid), _eventSuccess (false) {}
    void cb (const std::string& id, boost::any data)
    {
        // std::cout << "Any event recieved in CALLBACK!!!!" << std::endl;
        if (!_eventSuccess) {
            if (_id == id) {
                _data         = data;
                _eventSuccess = true;
            }
        }
    }
    std::string _id;
    boost::any _data;
    bool _eventSuccess;
};
}    // namespace

boost::any RobWorkStudio::waitForAnyEvent (const std::string& id, double timeout)
{
    // std::cout << " Wait for ANY event, with id: " << id << std::endl;
    AnyEventListener listener (id);
    genericAnyEvent ().add (
        boost::bind (&AnyEventListener::cb, &listener, boost::arg< 1 > (), boost::arg< 2 > ()),
        &listener);
    // std::cout << "Added event, now wait!" << std::endl;
    // now wait until event is called
    const double starttime = TimerUtil::currentTime ();
    bool reachedTimeout    = false;
    while (!listener._eventSuccess) {
        TimerUtil::sleepMs (10);
        if ((timeout > 0.0) && (TimerUtil::currentTime () - starttime > timeout)) {
            reachedTimeout = true;
            break;
        }
    }
    // remove the listener from the event
    genericAnyEvent ().remove (&listener);
    // now return result
    if (reachedTimeout)
        RW_THROW ("Timeout!");
    return listener._data;
}

rw::geometry::AABB< double > RobWorkStudio::calculateWorkCellSize ()
{
    std::vector< rw::geometry::BSphere< double > > spheres;
    std::vector< Object::Ptr > objects = this->_workcell->getObjects ();
    State& state                       = this->_state;
    for (Object::Ptr object : objects) {
        for (rw::geometry::Geometry::Ptr geom : object->getGeometry (state)) {
            rw::core::Ptr< Frame > frame = geom->getFrame ();
            RW_ASSERT (frame);
            spheres.push_back (
                rw::geometry::BSphere< double >::fitEigen (geom->getGeometryData ()));
            spheres.back ().setPosition (spheres.back ().getPosition () + frame->wTf (state).P ());
        }
    }

    std::vector< Vector3D< double > > points;
    for (rw::geometry::BSphere< double >& s : spheres) {
        double r = s.getRadius ();
        points.push_back (s.getPosition () + Vector3D< double > (r, 0, 0));
        points.push_back (s.getPosition () + Vector3D< double > (-r, 0, 0));
        points.push_back (s.getPosition () + Vector3D< double > (0, r, 0));
        points.push_back (s.getPosition () + Vector3D< double > (0, -r, 0));
        points.push_back (s.getPosition () + Vector3D< double > (0, 0, r));
        points.push_back (s.getPosition () + Vector3D< double > (0, 0, -r));
    }

    Vector3D< double > axis_max (-99999, -99999, -999999);
    Vector3D< double > axis_min (99999, 999999, 999999);
    for (Vector3D< double >& p : points) {
        for (size_t i = 0; i < p.size (); i++) {
            if (p[i] > axis_max[i]) {
                axis_max[i] = p[i];
            }
            if (p[i] < axis_min[i]) {
                axis_min[i] = p[i];
            }
        }
    }
    return rw::geometry::AABB< double > (axis_min, axis_max);
}
