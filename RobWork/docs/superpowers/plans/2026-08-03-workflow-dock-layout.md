# Workflow Dock Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the five workflow plugins compact and consistently docked, and unlock the three downstream tabs only after RobotModelBuilder has successfully loaded a model.

**Architecture:** A main-window `WorkflowDockLayoutController` locates named `RobWorkStudioPlugin` docks, applies Qt dock topology, controls tab/menu enablement, and revalidates readiness from the project and active WorkCell. RobotModelBuilder emits a signal only after its Save and Load path succeeds.

**Tech Stack:** C++17, Qt Widgets, GoogleTest, CMake.

---

## File Structure

- Create: `RobWorkStudio/src/rws/WorkflowDockLayoutController.hpp` and `.cpp` for layout and readiness.
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.hpp`, `.cpp`, `CMakeLists.txt` for controller lifecycle and build.
- Modify: `RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp` for access to its existing visibility action.
- Modify: `RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp` for order, right-side Jog, and final layout application.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp`, `.cpp`, and `RobotModelBuilderPluginMetaObjectTest.cpp` for the success notification.
- Create: `RobWorkStudio/gtest/rws/WorkflowDockLayoutControllerTest.cpp`; modify `RobWorkStudio/gtest/CMakeLists.txt` for UI behavior tests.

### Task 1: Add Failing Layout Tests

**Files:**
- Create: `RobWorkStudio/gtest/rws/WorkflowDockLayoutControllerTest.cpp`
- Modify: `RobWorkStudio/gtest/CMakeLists.txt`

- [ ] **Step 1: Add the source to `RWS_TEST_SRC`.**

```cmake
SET(RWS_TEST_SRC
  rws/ProjectDocumentRegistryTest.cpp
  rws/ProjectSystemTest.cpp
  rws/RobWorkStudioTest.cpp
  rws/WorkflowDockLayoutControllerTest.cpp
)
```

- [ ] **Step 2: Add tests using five `RobWorkStudioPlugin` test doubles named `EngineeringRequirements`, `RobotModelBuilder`, `KinematicAnalysis`, `StructureOptimizer`, and `Jog`.**

```cpp
TEST (WorkflowDockLayout, InitialLayoutIsLocked)
{
    rw::core::PropertyMap settings;
    rws::RobWorkStudio studio (settings);
    addNamedWorkflowDocks (studio);
    studio.configureWorkflowDockLayout ();
    studio.show (); QCoreApplication::processEvents ();
    const auto& docks = studio.getPlugins ();
    EXPECT_EQ (Qt::RightDockWidgetArea, studio.dockWidgetArea (docks[4]));
    EXPECT_EQ (QTabWidget::North, studio.tabPosition (Qt::LeftDockWidgetArea));
    EXPECT_EQ (QStringLiteral ("RobotModelBuilder"), studio.activeWorkflowDockName ());
    EXPECT_FALSE (docks[0]->isEnabled ());
    EXPECT_TRUE (docks[1]->isEnabled ());
    EXPECT_FALSE (docks[2]->isEnabled ());
    EXPECT_FALSE (docks[3]->isEnabled ());
    EXPECT_TRUE (docks[4]->isEnabled ());
    EXPECT_FALSE (docks[0]->visibilityAction ()->isEnabled ());
    EXPECT_TRUE (docks[4]->visibilityAction ()->isEnabled ());
    EXPECT_EQ (docks[0]->width (), docks[4]->width ());
}

TEST (WorkflowDockLayout, ExplicitModelLoadUnlocksDownstreamDocks)
{
    rw::core::PropertyMap settings;
    rws::RobWorkStudio studio (settings);
    addNamedWorkflowDocks (studio);
    studio.configureWorkflowDockLayout ();
    studio.notifyWorkflowRobotModelLoaded (QStringLiteral ("C:/tmp/robot.wc.xml"));
    const auto& docks = studio.getPlugins ();
    EXPECT_TRUE (docks[0]->isEnabled ());
    EXPECT_TRUE (docks[2]->isEnabled ());
    EXPECT_TRUE (docks[3]->isEnabled ());
    EXPECT_TRUE (docks[0]->visibilityAction ()->isEnabled ());
}
```

- [ ] **Step 3: Verify the test target fails before implementation.**

Run: `cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_sdurws-gtest`

Expected: missing `configureWorkflowDockLayout`, `activeWorkflowDockName`, `notifyWorkflowRobotModelLoaded`, and `visibilityAction` members.

- [ ] **Step 4: Commit the test checkpoint.**

Run: `git add RobWorkStudio/gtest/CMakeLists.txt RobWorkStudio/gtest/rws/WorkflowDockLayoutControllerTest.cpp; git commit -m "test: specify workflow dock layout"`

### Task 2: Implement the Controller

**Files:**
- Create: `RobWorkStudio/src/rws/WorkflowDockLayoutController.hpp`
- Create: `RobWorkStudio/src/rws/WorkflowDockLayoutController.cpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp`
- Modify: `RobWorkStudio/src/rws/CMakeLists.txt`

- [ ] **Step 1: Expose the existing menu action, without changing menu behavior.**

```cpp
// RobWorkStudioPlugin.hpp, public section
QAction* visibilityAction () { return &_showAction; }
```

- [ ] **Step 2: Create the controller interface.**

```cpp
class WorkflowDockLayoutController {
  public:
    explicit WorkflowDockLayoutController (RobWorkStudio* studio);
    void applyLayout ();
    void revalidateReadiness ();
    void notifyRobotModelLoaded (const QString& filename);
    QString activeDockName () const;
  private:
    RobWorkStudio* _studio;
    QString _standaloneModelFilename;
    bool _ready = false;
    void setReady (bool ready);
    void refreshTabEnablement ();
};
```

- [ ] **Step 3: Implement exact dock topology and the first-run 50-percent width migration.**

```cpp
_studio->setTabPosition (Qt::LeftDockWidgetArea, QTabWidget::North);
_studio->addDockWidget (Qt::LeftDockWidgetArea, requirements);
_studio->addDockWidget (Qt::LeftDockWidgetArea, builder);
_studio->addDockWidget (Qt::LeftDockWidgetArea, analysis);
_studio->addDockWidget (Qt::LeftDockWidgetArea, optimizer);
_studio->tabifyDockWidget (requirements, builder);
_studio->tabifyDockWidget (builder, analysis);
_studio->tabifyDockWidget (analysis, optimizer);
_studio->addDockWidget (Qt::RightDockWidgetArea, jog);

const int version = _studio->getSettings ().get< int > ("WorkflowDockLayoutVersion", 0);
if (version < 1) {
    const int legacy = std::max ({requirements->sizeHint ().width (), builder->sizeHint ().width (),
                                  analysis->sizeHint ().width (), optimizer->sizeHint ().width (), jog->sizeHint ().width ()});
    const int width = std::max (240, legacy / 2);
    _studio->resizeDocks ({requirements}, {width}, Qt::Horizontal);
    _studio->resizeDocks ({jog}, {width}, Qt::Horizontal);
    _studio->getSettings ().set< int > ("WorkflowDockLayoutVersion", 1);
}
```

Reapply positions, tab order, and top tab position every startup after Qt restores settings. Only run `resizeDocks` for a layout version below `1`, so later user resizing is retained. Ignore absent workflow docks and all non-workflow docks.

- [ ] **Step 4: Implement the lock, including QTabBar and menu paths.**

```cpp
for (const QString& name : {QStringLiteral ("EngineeringRequirements"),
                            QStringLiteral ("KinematicAnalysis"),
                            QStringLiteral ("StructureOptimizer")}) {
    RobWorkStudioPlugin* dock = docks.value (name);
    dock->setEnabled (ready);
    dock->visibilityAction ()->setEnabled (ready);
}
if (!ready)
    docks.value ("RobotModelBuilder")->raise ();
```

`refreshTabEnablement()` finds the `QTabBar` containing `RobotModelBuilder`, then calls `setTabEnabled(index, _ready || tabText == "RobotModelBuilder")` for the four named workflow tabs. Schedule it with `QTimer::singleShot(0, ...)` after topology and readiness changes. It must never disable Jog.

- [ ] **Step 5: Add the RobWorkStudio façade and readiness revalidation.**

```cpp
void configureWorkflowDockLayout ();
void notifyWorkflowRobotModelLoaded (const QString& filename);
QString activeWorkflowDockName () const;

Q_SIGNALS:
void activeWorkCellChanged ();
```

Create/destroy the controller with the main window. Emit `activeWorkCellChanged()` after `tryOpenWorkCellFile`, `setWorkcell`, and `closeWorkCell`; connect it and `projectContextChanged` to `revalidateReadiness()`.

For a project, unlock only when `mainWorkCellResourceId()` is set, `robot-model.main` resolves to an existing file, the main scene resolves, and the active WorkCell canonical filename equals that scene. Without a project, unlock only when the active WorkCell canonical filename equals the one recorded by `notifyRobotModelLoaded`. Any mismatch relocks and raises Builder.

- [ ] **Step 6: Compile the new source and run tests.**

Add `WorkflowDockLayoutController.cpp` to `SRC_FILES_CPP`, then run: `cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_sdurws-gtest; ctest --test-dir build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_sdurws-gtest --output-on-failure`

Expected: the new tests pass.

- [ ] **Step 7: Commit the controller.**

Run: `git add RobWorkStudio/src/rws/WorkflowDockLayoutController.hpp RobWorkStudio/src/rws/WorkflowDockLayoutController.cpp RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp RobWorkStudio/src/rws/RobWorkStudio.hpp RobWorkStudio/src/rws/RobWorkStudio.cpp RobWorkStudio/src/rws/CMakeLists.txt; git commit -m "feat: add workflow dock controller"`

### Task 3: Wire Builder Success and Startup Order

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPluginMetaObjectTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp`

- [ ] **Step 1: Add a failing meta-object contract.**

```cpp
if (metaObject.indexOfSignal ("robotModelLoaded(QString)") < 0)
    return fail ("RobotModelBuilder must expose a robot-model-loaded signal.");
```

Run: `cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_robotmodelbuilder_metatest; ctest --test-dir build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_robotmodelbuilder_metatest --output-on-failure`

Expected: fails before the signal is declared.

- [ ] **Step 2: Add and emit `robotModelLoaded(const QString&)` only on success.**

```cpp
// RobotModelBuilderPlugin.hpp, Q_SIGNALS
void robotModelLoaded (const QString& filename);

// project branch, after promoteGeneratedWorkCell returned true
Q_EMIT robotModelLoaded (filename);

// standalone branch, after setWorkcell loaded the requested file
if (studio->getWorkcell () != nullptr &&
    QFileInfo (QString::fromStdString (studio->getWorkcell ()->getFilename ())).canonicalFilePath () ==
        QFileInfo (filename).canonicalFilePath ())
    Q_EMIT robotModelLoaded (filename);
```

Do not emit for preview, Save XML, URDF import, project bootstrap, or either failure return from `loadSceneFile`.

- [ ] **Step 3: Connect the dynamic signal without adding a core-to-plugin link.**

```cpp
QObject::connect (builder, SIGNAL (robotModelLoaded(const QString&)), _studio,
                  SLOT (notifyWorkflowRobotModelLoaded(const QString&)), Qt::UniqueConnection);
```

Place the connection in `WorkflowDockLayoutController::applyLayout()` after locating the Builder dock. Make the target method a public Qt slot.

- [ ] **Step 4: Fix static registration and final layout timing.**

```cpp
rwstudio.addPlugin (new rws::EngineeringRequirementsPlugin (), false, Qt::LeftDockWidgetArea);
rwstudio.addPlugin (new rws::RobotModelBuilderPlugin (), false, Qt::LeftDockWidgetArea);
rwstudio.addPlugin (new rws::KinematicAnalysisPlugin (), false, Qt::LeftDockWidgetArea);
rwstudio.addPlugin (new rws::StructureOptimizerPlugin (), false, Qt::LeftDockWidgetArea);
```

Change Jog to `Qt::RightDockWidgetArea`. After plugin-folder scans and `rwstudio.loadSettingsSetupPlugins(inifile)`, call `rwstudio.configureWorkflowDockLayout();`.

- [ ] **Step 5: Verify affected targets and commit.**

Run: `cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_robotmodelbuilder_metatest sdurws_sdurws-gtest; ctest --test-dir build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R "sdurws_robotmodelbuilder_metatest|sdurws_sdurws-gtest" --output-on-failure`

Expected: both targets pass; only a completed Save and Load can unlock downstream tabs.

Run: `git add RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPluginMetaObjectTest.cpp; git commit -m "feat: gate workflow tabs on robot model load"`

### Task 4: Regression Verification

**Files:**
- Modify: `RobWorkStudio/gtest/rws/WorkflowDockLayoutControllerTest.cpp`

- [ ] **Step 1: Add relock coverage.**

```cpp
TEST (WorkflowDockLayout, RelocksWhenActiveModelIsRemoved)
{
    // Configure named docks, unlock through notifyWorkflowRobotModelLoaded(), close the WorkCell,
    // then assert that downstream docks/actions are disabled and Builder is active.
}
```

Add a temporary `.rwproj` fixture containing `robot-model.main` but no `mainWorkCell` entry point; verify it stays locked. This proves a draft model resource cannot bypass Save and Load.

- [ ] **Step 2: Run the complete automated verification.**

Run: `cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_sdurws-gtest sdurws_robotmodelbuilder_metatest; ctest --test-dir build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R "sdurws_sdurws-gtest|sdurws_robotmodelbuilder_metatest" --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 3: Manually verify Debug RobWorkStudio with clean settings.**

Expected: left top tabs are `EngineeringRequirements`, `RobotModelBuilder`, `KinematicAnalysis`, `StructureOptimizer`; Jog is right and usable before modeling; the other three tabs and their menu actions enable only after Save and Load succeeds.

- [ ] **Step 4: Commit regression coverage.**

Run: `git add RobWorkStudio/gtest/rws/WorkflowDockLayoutControllerTest.cpp; git commit -m "test: cover workflow dock readiness regression"`
