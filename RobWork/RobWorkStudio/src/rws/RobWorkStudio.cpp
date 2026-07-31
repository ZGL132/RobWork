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
#include "RobWorkStudioPlugin.hpp"

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
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QObject>
#include <QPluginLoader>
#include <QSettings>
#include <QStringList>
#include <QToolBar>
#include <QUrl>

#ifdef RWS_USE_PYTHON
#include <rws/pythonpluginloader/PyPlugin.hpp>
#endif    // RWS_USE_PYTHON
#include <RobWorkConfig.hpp>
#include <rws/propertyview/PropertyViewEditor.hpp>

#include <boost/bind/bind.hpp>
#include <boost/filesystem.hpp>
#include <sstream>

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
    _fileMenu->addAction (migrateWorkCellAction);
    _fileMenu->addAction (openAction);
    _fileMenu->addAction (closeAction);
    _fileMenu->addAction (saveAction);
    _fileMenu->addAction (saveAsAction);
    _fileMenu->addAction (importResourceAction);
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

// 新建项目：弹保存对话框选择 .rwproj 位置，构造空清单交给项目管理器落盘，
// 随后创建内存 WorkCell 并更新最近文件列表与窗口标题。
void RobWorkStudio::newProject ()
{
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
    // 新建项目同样属于“离开当前项目”的操作：先完成旧项目的保存/放弃/取消决策，
    // 用户取消时保持旧项目原样返回。
    if (!confirmProjectClose ())
        return;

    ProjectManifest manifest;
    manifest.project.name = QFileInfo (projectFile).completeBaseName ();
    manifest.project.description = QString::fromUtf8 ("由 RobWorkStudio 创建的空项目");
    manifest.settings.insert (QStringLiteral ("pathPolicy"),
                              QStringLiteral ("project-relative"));

    QString error;
    if (!_projectManager.createProject (projectFile, manifest, &error)) {
        QMessageBox::critical (this, tr ("Create Project Failed"), error);
        return;
    }

    // 新项目清单成功落盘后再关闭旧 Provider 文档，避免创建失败导致旧项目被卸载。
    _projectDocuments.closeResources ();
    // 第一阶段的新建项目不强制生成 WorkCell 文件，而是提供一个内存中的空 WorkCell。
    // 此处必须调用不带项目切换语义的内部函数；若调用用户菜单槽 newWorkCell，刚由
    // createProject 建立的新项目会被误判为“待退出项目”并立即清空。
    createEmptyWorkCell ();
    _settingsMap->set< std::string > ("PreviousOpenDirectory",
                                      QFileInfo (projectFile).absolutePath ().toStdString ());
    std::vector< std::string > recent = _settingsMap->get< std::vector< std::string > > (
        "LastOpennedFiles", std::vector< std::string > ());
    recent.push_back (projectFile.toStdString ());
    _settingsMap->set< std::vector< std::string > > ("LastOpennedFiles", recent);
    updateLastFiles ();
    updateProjectWindowTitle ();
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

    // 稳定 ID 已存在时不重复登记。首次编辑之外的每次控件变化只能更新脏状态，不能追加
    // 清单项，更不能覆盖用户已经保存的业务配置。
    ProjectResource existing;
    if (_projectManager.manifest ().findResource (resource.id, existing))
        return true;
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

QString RobWorkStudio::mainWorkCellResourceId () const
{
    // entryPoints 是项目主场景的唯一权威来源，不能依赖 resources 数组顺序推测。
    if (!_projectManager.hasProject ())
        return QString ();
    return _projectManager.manifest ().entryPoints.value (QStringLiteral ("mainWorkCell"));
}

void RobWorkStudio::notifyProjectDocumentChanged ()
{
    // 不在这里调用保存或修改清单。Provider 的 isDirty 由 Registry 聚合，标题栏只是
    // 可视反馈；真正写入仍必须经过多文件暂存事务，避免一次控件编辑绕过失败回滚。
    updateProjectWindowTitle ();
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
    if (!saveProjectInternal (&error)) {
        QMessageBox::warning (this, tr ("Save Project Failed"), error);
        return;
    }
    updateProjectWindowTitle ();
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
    return _projectManager.saveProject (error);
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

    if (!_projectManager.isDirty () && !_projectDocuments.isDirty ())
        return true;

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
    if (choice == QMessageBox::Discard)
        return true;

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
            *error = QString::fromUtf8 ("WorkCell 保存失败：%1。").arg (
                QString::fromStdString (exception.getMessage ().getText ()));
    }
    catch (const std::exception& exception) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("WorkCell 保存失败：%1。").arg (
                QString::fromUtf8 (exception.what ()));
    }
    return false;
}

// 实际打开项目文件：先让项目管理器读取并校验清单，再按 mainWorkCell 入口决定
// 加载真实场景还是创建内存 WorkCell。任一环节失败即返回 false 并回填错误。
bool RobWorkStudio::openProjectFile (const QString& filename, QString* error)
{
    if (!confirmProjectClose ())
        return false;
    if (!_projectManager.openProject (filename, error))
        return false;

    // 清单已通过结构、路径和 required 资源检查后，才关闭旧文档并加载新文档。
    // 这样普通的文件选择错误不会破坏当前已打开的项目。
    _projectDocuments.closeResources ();
    closeWorkCell ();

    if (!_projectDocuments.loadProjectResources (
            _projectManager.manifest (), _projectManager.projectFilePath (), error)) {
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
