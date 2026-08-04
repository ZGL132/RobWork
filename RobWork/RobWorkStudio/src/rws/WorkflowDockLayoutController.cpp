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

#include "WorkflowDockLayoutController.hpp"

#include "RobWorkStudio.hpp"
#include "RobWorkStudioPlugin.hpp"

#include <QFileInfo>
#include <QHash>
#include <QLayout>
#include <QObject>
#include <QSizePolicy>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>

#include <algorithm>

namespace {

const QString RequirementsDock = QStringLiteral ("EngineeringRequirements");
const QString BuilderDock = QStringLiteral ("RobotModelBuilder");
const QString AnalysisDock = QStringLiteral ("KinematicAnalysis");
const QString OptimizerDock = QStringLiteral ("StructureOptimizer");
const QString JogDock = QStringLiteral ("Jog");

const QStringList WorkflowDockNames = {RequirementsDock, BuilderDock, AnalysisDock, OptimizerDock,
                                       JogDock};
const QStringList LeftWorkflowDockNames = {RequirementsDock, BuilderDock, AnalysisDock,
                                           OptimizerDock};

// 工作流 Dock 统一宽度策略：初始宽度 280px，手动可缩至最小 240px；布局版本号用于
// 让老用户配置只重置一次。
constexpr int WorkflowDockInitialWidth = 280;
constexpr int WorkflowDockMinimumWidth = 240;
constexpr int WorkflowDockLayoutVersion = 7;

// 放宽 Dock 的水平宽度约束：Dock 外层只要求 >= 最小宽度 240，内容 widget 的最小宽度
// 归零并把水平策略设为 Ignored，使 Dock 宽度完全由用户拖动控制，而不是被插件内容
// 的 sizeHint/minimumSizeHint 撑宽。
void relaxDockWidthConstraints (rws::RobWorkStudioPlugin* dock)
{
    dock->setMinimumWidth (WorkflowDockMinimumWidth);
    QWidget* content = dock->widget ();
    if (content == nullptr)
        return;

    content->setMinimumWidth (0);
    QSizePolicy policy = content->sizePolicy ();
    policy.setHorizontalPolicy (QSizePolicy::Ignored);
    content->setSizePolicy (policy);
}

QHash< QString, rws::RobWorkStudioPlugin* > workflowDocks (const rws::RobWorkStudio* studio)
{
    QHash< QString, rws::RobWorkStudioPlugin* > docks;
    for (rws::RobWorkStudioPlugin* plugin : studio->getPlugins ()) {
        if (WorkflowDockNames.contains (plugin->name ()))
            docks.insert (plugin->name (), plugin);
    }
    for (const QString& name : WorkflowDockNames) {
        if (!docks.contains (name))
            return QHash< QString, rws::RobWorkStudioPlugin* > ();
    }
    return docks;
}

QString canonicalFileName (const QString& filename)
{
    return QFileInfo (filename).canonicalFilePath ();
}

QString activeWorkCellFileName (const rws::RobWorkStudio* studio)
{
    const rw::models::WorkCell::Ptr workcell = const_cast< rws::RobWorkStudio* > (studio)->getWorkCell ();
    if (workcell == nullptr)
        return QString ();
    return canonicalFileName (QString::fromStdString (workcell->getFilename ()));
}

int tabIndex (const QTabBar* tabBar, const QString& name)
{
    for (int index = 0; index < tabBar->count (); ++index) {
        if (tabBar->tabText (index) == name)
            return index;
    }
    return -1;
}

bool isWorkflowTabBar (const QTabBar* tabBar)
{
    return tabBar->count () == LeftWorkflowDockNames.size () &&
           std::all_of (LeftWorkflowDockNames.cbegin (), LeftWorkflowDockNames.cend (),
                        [tabBar] (const QString& name) { return tabIndex (tabBar, name) >= 0; });
}

}    // namespace

namespace rws {

WorkflowDockLayoutController::WorkflowDockLayoutController (RobWorkStudio* studio) : _studio (studio)
{
    QObject::connect (_studio, &RobWorkStudio::activeWorkCellChanged, [this] () {
        revalidateReadiness ();
    });
    QObject::connect (_studio, &RobWorkStudio::projectContextChanged,
                      [this] (const QString&) { revalidateReadiness (); });
}

void WorkflowDockLayoutController::applyLayout ()
{
    const QHash< QString, RobWorkStudioPlugin* > docks = workflowDocks (_studio);
    if (docks.isEmpty ())
        return;

    RobWorkStudioPlugin* requirements = docks.value (RequirementsDock);
    RobWorkStudioPlugin* builder = docks.value (BuilderDock);
    RobWorkStudioPlugin* analysis = docks.value (AnalysisDock);
    RobWorkStudioPlugin* optimizer = docks.value (OptimizerDock);
    RobWorkStudioPlugin* jog = docks.value (JogDock);
    QObject::connect (builder, SIGNAL (robotModelLoaded (QString)), _studio,
                      SLOT (notifyWorkflowRobotModelLoaded (QString)), Qt::UniqueConnection);

    _studio->setTabPosition (Qt::LeftDockWidgetArea, QTabWidget::North);
    for (RobWorkStudioPlugin* dock : {requirements, builder, analysis, optimizer}) {
        dock->setFloating (false);
        _studio->addDockWidget (Qt::LeftDockWidgetArea, dock);
    }
    jog->setFloating (false);
    _studio->addDockWidget (Qt::RightDockWidgetArea, jog);

    for (RobWorkStudioPlugin* dock : {requirements, builder, analysis, optimizer, jog})
        relaxDockWidthConstraints (dock);

    _studio->tabifyDockWidget (requirements, builder);
    _studio->tabifyDockWidget (builder, analysis);
    _studio->tabifyDockWidget (analysis, optimizer);

    // Apply each revised width policy once to both existing and fresh settings.
    if (_studio->getSettings ().get< int > ("WorkflowDockLayoutVersion", 0) <
        WorkflowDockLayoutVersion) {
        _initialWidth = WorkflowDockInitialWidth;
        _initialWidthPending = true;
        _studio->getSettings ().set< int > ("WorkflowDockLayoutVersion",
                                            WorkflowDockLayoutVersion);
    }

    for (RobWorkStudioPlugin* dock : {requirements, builder, analysis, optimizer, jog})
        dock->setVisible (true);
    revalidateReadiness ();
}

void WorkflowDockLayoutController::finalizeInitialWidth ()
{
    if (!_initialWidthPending)
        return;
    const QHash< QString, RobWorkStudioPlugin* > docks = workflowDocks (_studio);
    if (docks.isEmpty ())
        return;

    _studio->resizeDocks ({docks.value (BuilderDock)}, {_initialWidth}, Qt::Horizontal);
    _studio->resizeDocks ({docks.value (JogDock)}, {_initialWidth}, Qt::Horizontal);
    _studio->layout ()->activate ();
    _initialWidthPending = false;
}

void WorkflowDockLayoutController::notifyRobotModelLoaded (const QString& filename)
{
    _standaloneModelFilename = canonicalFileName (filename);
    revalidateReadiness ();
}

void WorkflowDockLayoutController::revalidateReadiness ()
{
    bool ready = false;
    const QString activeScene = activeWorkCellFileName (_studio);
    if (!_studio->projectDirectory ().isEmpty ()) {
        const QString mainSceneResourceId = _studio->mainWorkCellResourceId ();
        QString modelPath;
        QString scenePath;
        ready = !mainSceneResourceId.isEmpty () &&
                _studio->resolveProjectResource (QStringLiteral ("robot-model.main"), modelPath) &&
                QFileInfo (modelPath).isFile () &&
                _studio->resolveProjectResource (mainSceneResourceId, scenePath) &&
                QFileInfo (scenePath).isFile () &&
                activeScene == canonicalFileName (scenePath);
    }
    else {
        ready = !_standaloneModelFilename.isEmpty () && activeScene == _standaloneModelFilename;
    }
    setReady (ready);
}

QString WorkflowDockLayoutController::activeDockName () const
{
    const QHash< QString, RobWorkStudioPlugin* > docks = workflowDocks (_studio);
    if (docks.isEmpty ())
        return QString ();

    for (QTabBar* tabBar : _studio->findChildren< QTabBar* > ()) {
        if (isWorkflowTabBar (tabBar) && tabBar->currentIndex () >= 0)
            return tabBar->tabText (tabBar->currentIndex ());
    }
    return BuilderDock;
}

void WorkflowDockLayoutController::setReady (bool ready)
{
    _ready = ready;
    const QHash< QString, RobWorkStudioPlugin* > docks = workflowDocks (_studio);
    if (docks.isEmpty ())
        return;

    // 项目上下文门控：projectActive 表示当前是否已打开项目(目录非空)。
    const bool projectActive = !_studio->projectDirectory ().isEmpty ();
    for (const QString& name : LeftWorkflowDockNames) {
        RobWorkStudioPlugin* dock = docks.value (name);
        // 项目上下文可用 = 插件不要求项目上下文，或当前已有打开的项目。
        const bool projectContextAvailable = !dock->requiresProjectContext () || projectActive;
        // 插件整体启用条件 = 项目上下文可用 且 (工作流就绪 或 该 Dock 是构建器)。
        // 未打开项目时，所有要求项目上下文的 Dock 都被禁用；BuilderDock 特殊，
        // 在就绪前也保持可用(用于新建机器人流程)，但前提同样是项目上下文可用。
        const bool enabled = projectContextAvailable && (ready || name == BuilderDock);
        dock->setEnabled (enabled);
        dock->visibilityAction ()->setEnabled (enabled);
    }
    // Jog 面板不依赖项目上下文，任何状态下都保持可用。
    docks.value (JogDock)->setEnabled (true);
    docks.value (JogDock)->visibilityAction ()->setEnabled (true);

    // 若未打开项目且构建器要求项目上下文，则隐藏构建器，避免空上下文下展示；
    if (!projectActive && docks.value (BuilderDock)->requiresProjectContext ()) {
        docks.value (BuilderDock)->setVisible (false);
    }
    // 否则按原有逻辑：工作流未就绪时把构建器 Dock 置顶显示。
    else if (!ready) {
        docks.value (BuilderDock)->setVisible (true);
        docks.value (BuilderDock)->raise ();
    }
    QTimer::singleShot (0, _studio, [this] () { refreshTabEnablement (); });
}

void WorkflowDockLayoutController::refreshTabEnablement ()
{
    const QHash< QString, RobWorkStudioPlugin* > docks = workflowDocks (_studio);
    if (docks.isEmpty ())
        return;

    for (QTabBar* tabBar : _studio->findChildren< QTabBar* > ()) {
        if (!isWorkflowTabBar (tabBar))
            continue;
        for (int index = 0; index < tabBar->count (); ++index) {
            const QString name = tabBar->tabText (index);
            if (LeftWorkflowDockNames.contains (name)) {
                RobWorkStudioPlugin* dock = docks.value (name);
                // 与 setReady 中的判定保持一致：Tab 页可用 = 项目上下文可用
                // (插件不需要项目上下文，或当前已有打开的项目) 且 工作流就绪
                // (或该 Tab 是构建器)。刷新 Tab 使能状态，使门控同时作用于 Tab 页。
                const bool projectContextAvailable =
                    !dock->requiresProjectContext () || !_studio->projectDirectory ().isEmpty ();
                tabBar->setTabEnabled (index,
                                       projectContextAvailable && (_ready || name == BuilderDock));
            }
        }
        return;
    }
}

}    // namespace rws
