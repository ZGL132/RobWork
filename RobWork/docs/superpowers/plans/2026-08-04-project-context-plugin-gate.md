# Project Context Plugin Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Disable only robot/WorkCell business-plugin entry points until an `.rwproj` project is active, while leaving pure-view and general-purpose plugins usable.

**Architecture:** The base plugin exposes an ABI-safe, non-virtual opt-in query backed by a QObject dynamic property. `RobWorkStudio` centralizes the gate at plugin registration and each stable project-context refresh, recording a gated dock's visibility before hiding it so normal visibility is restored after a successful project activation. The initial five business plugins opt in from their constructors; the plugin host stays name-agnostic.

**Tech Stack:** C++17, Qt 6 Widgets/QObject dynamic properties, GoogleTest, CMake.

---

### Task 1: Prove the project-context UI contract

**Files:**
- Modify: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`

- [ ] **Step 1: Add a local project-context test plugin and a failing lifecycle test**

  Add this helper inside the existing anonymous namespace, immediately after `createEmptyProject`:

  ```cpp
  class ProjectContextTestPlugin : public RobWorkStudioPlugin
  {
  public:
      explicit ProjectContextTestPlugin (bool required) :
          RobWorkStudioPlugin (QStringLiteral ("ProjectContextTestPlugin"), QIcon ())
      {
          if (required)
              setRequiresProjectContext (true);
      }
  };
  ```

  Add this test after the existing project lifecycle tests:

  ```cpp
  TEST (RobWorkStudio, ProjectContextPluginGateTracksProjectLifecycle)
  {
      int argc = 1;
      char name[] = "RobWorkStudio";
      char* argv[1] = {name};
      QApplication app (argc, argv);
      PropertyMap map;
      RobWorkStudio studio (map);
      ProjectContextTestPlugin generalPlugin (false);
      ProjectContextTestPlugin businessPlugin (true);
      QTemporaryDir projectDirectory;
      ASSERT_TRUE (projectDirectory.isValid ());

      EXPECT_FALSE (generalPlugin.requiresProjectContext ());
      EXPECT_TRUE (businessPlugin.requiresProjectContext ());
      studio.addPlugin (&generalPlugin, false);
      studio.addPlugin (&businessPlugin, true);
      EXPECT_TRUE (generalPlugin.visibilityAction ()->isEnabled ());
      EXPECT_FALSE (businessPlugin.visibilityAction ()->isEnabled ());
      EXPECT_FALSE (businessPlugin.isVisible ());

      businessPlugin.showPlugin ();
      EXPECT_FALSE (businessPlugin.isVisible ());
      generalPlugin.showPlugin ();
      EXPECT_TRUE (generalPlugin.isVisible ());

      studio.openFile (createEmptyProject (projectDirectory.path ()).toStdString ());
      EXPECT_TRUE (businessPlugin.visibilityAction ()->isEnabled ());
      EXPECT_TRUE (businessPlugin.isVisible ());

      studio.closeProject ();
      EXPECT_FALSE (businessPlugin.visibilityAction ()->isEnabled ());
      EXPECT_FALSE (businessPlugin.isVisible ());
      EXPECT_TRUE (generalPlugin.visibilityAction ()->isEnabled ());
  }
  ```

- [ ] **Step 2: Build and run the new focused test to observe RED**

  Run:

  ```powershell
  cmake --build 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug' --target sdurws_sdurws-gtest --config Debug
  $env:QT_QPA_PLATFORM='windows'
  & 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_sdurws-gtest.exe' --gtest_filter='RobWorkStudio.ProjectContextPluginGateTracksProjectLifecycle'
  ```

  Expected: compilation initially fails because `setRequiresProjectContext` is absent; after Task 2's API is added, the test runs and fails because the business plugin remains enabled or visible without a project.

### Task 2: Add the ABI-safe plugin declaration and host gate

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudioPlugin.cpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp`

- [ ] **Step 1: Add the non-virtual plugin declaration API**

  In `RobWorkStudioPlugin.hpp`, add `bool requiresProjectContext () const;` directly before `visibilityAction()`. Add `void setRequiresProjectContext (bool required);` to the protected section before the action-search helpers.

  In `RobWorkStudioPlugin.cpp`, use one dynamic-property key and these method bodies:

  ```cpp
  namespace {
  const char* const RequiresProjectContextProperty = "rws.requiresProjectContext";
  }

  bool RobWorkStudioPlugin::requiresProjectContext () const
  {
      return property (RequiresProjectContextProperty).toBool ();
  }

  void RobWorkStudioPlugin::setRequiresProjectContext (bool required)
  {
      setProperty (RequiresProjectContextProperty, required);
  }
  ```

  At the start of `showPlugin()`, reject a user or direct programmatic activation without an active project:

  ```cpp
  if (requiresProjectContext () &&
      (getRobWorkStudio () == nullptr || getRobWorkStudio ()->projectDirectory ().isEmpty ())) {
      setVisible (false);
      return;
  }
  ```

- [ ] **Step 2: Add a single project-availability refresh method**

  In `RobWorkStudio.hpp`, declare `void updateProjectPluginAvailability ();` beside `updateProjectWindowTitle()`.

  Define it immediately before `updateProjectWindowTitle()` in `RobWorkStudio.cpp`:

  ```cpp
  void RobWorkStudio::updateProjectPluginAvailability ()
  {
      const bool projectActive = _projectManager.hasProject ();
      const char* const restoreVisibleProperty = "rws.restoreVisibleAfterProjectGate";
      for (RobWorkStudioPlugin* plugin : _plugins) {
          if (plugin == nullptr || !plugin->requiresProjectContext ())
              continue;

          plugin->visibilityAction ()->setEnabled (projectActive);
          if (!projectActive) {
              plugin->setProperty (restoreVisibleProperty, plugin->isVisible ());
              plugin->setVisible (false);
          }
          else if (plugin->property (restoreVisibleProperty).toBool ()) {
              plugin->setVisible (true);
              plugin->setProperty (restoreVisibleProperty, false);
          }
      }
  }
  ```

  Call it as the final operation in `addPlugin()`, after `restoreState(mainAppState)`. Call it in `updateProjectWindowTitle()` immediately before `Q_EMIT projectContextChanged(projectDirectory());`.

- [ ] **Step 3: Rebuild and run the focused test to verify GREEN**

  Run the commands from Task 1. Expected: `RobWorkStudio.ProjectContextPluginGateTracksProjectLifecycle` passes.

### Task 3: Declare initial business plugins and verify regression coverage

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/workcelleditorplugin/WorkcellEditorPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp`
- Test: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`

- [ ] **Step 1: Mark each business plugin in its constructor**

  Add `setRequiresProjectContext (true);` to the body of each constructor, preserving each existing initialization list. For `StructureOptimizerPlugin`, add it before its existing `_widget = new StructureOptimizerWidget();` statement.

- [ ] **Step 2: Build affected targets and run focused regressions**

  Run:

  ```powershell
  cmake --build 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug' --target sdurws_sdurws-gtest sdurws_robotmodelbuilder-metatest sdurws_robotmodelbuilder-widgettest --config Debug
  $env:QT_QPA_PLATFORM='windows'
  & 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_sdurws-gtest.exe' --gtest_filter='RobWorkStudio.ProjectContextPluginGateTracksProjectLifecycle:RobWorkStudio.NewRobotProject*'
  & 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_robotmodelbuilder-metatest.exe'
  & 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_robotmodelbuilder-widgettest.exe'
  ```

  Expected: every selected test passes; RobotModelBuilder internal new-project bootstrap remains operational until the successful project transaction enables its dock.

- [ ] **Step 3: Inspect only task-owned changes before handoff**

  Run:

  ```powershell
  git diff -- RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp RobWorkStudio/src/rws/RobWorkStudioPlugin.cpp RobWorkStudio/src/rws/RobWorkStudio.hpp RobWorkStudio/src/rws/RobWorkStudio.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp RobWorkStudio/src/rwslibs/workcelleditorplugin/WorkcellEditorPlugin.cpp RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp
  ```

  Expected: only the ABI-safe declaration, central gate, five opt-ins, and the lifecycle test appear. Do not stage, commit, reset, checkout, or clean any worktree content.
