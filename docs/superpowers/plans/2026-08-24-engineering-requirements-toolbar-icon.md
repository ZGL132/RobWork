# EngineeringRequirements Toolbar Icon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a matching EngineeringRequirements icon and show icons-only toolbar buttons for the four engineering plugins while preserving their names in menus and tooltips.

**Architecture:** Keep the existing three plugin PNGs unchanged. Add a project-local vector source rendered to a 64×64 transparent PNG, register it through the EngineeringRequirements plugin resource target, load it in the plugin constructor, and apply icons-only presentation only to the four named plugin actions from the shared `RobWorkStudioPlugin::setupToolBar` path.

**Tech Stack:** Qt 5/6 Widgets and RCC, CMake, existing RobWorkStudio plugin framework, SVG rasterization with ImageMagick (or the repository's available image conversion tool), Windows Visual Studio x64 GUI verification.

---

### Task 1: Create the EngineeringRequirements icon asset

**Files:**
- Create: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/engineeringrequirements_icon.svg` (editable source)
- Create: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/engineeringrequirements_icon.png` (64×64 RGBA deliverable)

- [ ] **Step 1: Add the vector artwork**

  Draw a transparent 64×64 canvas with at least 4px padding. Use a dark graphite outline and light gray highlight for a small isometric wireframe cube, then overlay a deep teal-blue specification card with two pale horizontal rule lines and a compact check mark. Use no text, watermark, background rectangle, or saturated full-canvas fill.

- [ ] **Step 2: Rasterize the source without changing dimensions**

  Run:

  ```powershell
  magick -background none `
    RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/engineeringrequirements_icon.svg `
    -resize 64x64 `
    RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/engineeringrequirements_icon.png
  ```

  Expected: a 64×64 PNG with an alpha channel and transparent corner pixels.

- [ ] **Step 3: Inspect the raster at native size**

  Use the image viewer on the PNG and confirm the cube/card/check mark remain legible at 16–24px toolbar scale, with no clipped edges or opaque background.

- [ ] **Step 4: Commit the asset pair**

  ```powershell
  git add -- RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/engineeringrequirements_icon.svg RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/engineeringrequirements_icon.png
  git commit -m "Add EngineeringRequirements toolbar icon"
  ```

### Task 2: Register the icon in the EngineeringRequirements plugin target

**Files:**
- Create: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/resources.qrc`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/CMakeLists.txt`

- [ ] **Step 1: Add the Qt resource manifest**

  Use the existing plugin pattern:

  ```xml
  <!DOCTYPE RCC><RCC version="1.0">
      <qresource prefix="/engineeringrequirements">
          <file>engineeringrequirements_icon.png</file>
      </qresource>
  </RCC>
  ```

- [ ] **Step 2: Generate RCC sources for both Qt versions**

  Before `rws_add_plugin`, add:

  ```cmake
  if(DEFINED Qt6Core_VERSION)
      qt_add_resources(RccSrcFiles resources.qrc)
  else()
      qt5_add_resources(RccSrcFiles resources.qrc)
  endif()
  ```

  Pass `${RccSrcFiles}` as the final source argument to `rws_add_plugin` so the resource is linked into both static and shared builds.

- [ ] **Step 3: Reconfigure and build the plugin target**

  Run:

  ```powershell
  cmake --build RobWork/build/codex-vs-debug5 --config Debug --target sdurws_engineeringrequirements -j 6
  ```

  Expected: the target builds and the generated RCC source contains `:/engineeringrequirements/engineeringrequirements_icon.png`.

- [ ] **Step 4: Commit resource integration**

  ```powershell
  git add -- RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/resources.qrc RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/CMakeLists.txt
  git commit -m "Register EngineeringRequirements icon resource"
  ```

### Task 3: Load the icon from the plugin constructor

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp:134-139`
- Create: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPluginIconTest.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/CMakeLists.txt`

- [ ] **Step 1: Replace the empty icon**

  Change the base constructor call to:

  ```cpp
  RobWorkStudioPlugin(
      "EngineeringRequirements",
      QIcon(":/engineeringrequirements/engineeringrequirements_icon.png"))
  ```

  Keep `setRequiresProjectContext(true)` and all plugin behavior unchanged.

- [ ] **Step 2: Add a resource-load assertion to a plugin-linked GUI test**

  Create the test with this exact structure so the test links the plugin target (and therefore its RCC object) instead of the core-only test target:

  ```cpp
  #include "EngineeringRequirementsPlugin.hpp"
  #include <QApplication>
  #include <cassert>

  int main(int argc, char** argv)
  {
      QApplication app(argc, argv);
      rws::EngineeringRequirementsPlugin plugin;
      assert(!plugin.windowIcon().isNull());
      assert(plugin.windowIcon().pixmap(64, 64).isNull() == false);
      return 0;
  }
  ```

  Add an `sdurws_engineeringrequirements_icon_test` executable linked to `${SUBSYS_NAME}` and `${QT_LIBRARIES}`, register it with CTest, and set `QT_QPA_PLATFORM=windows` on Windows (`offscreen` only on non-Windows).

- [ ] **Step 3: Run the focused GUI test under the required Windows platform**

  From a Visual Studio x64 developer PowerShell:

  ```powershell
  $env:QT_QPA_PLATFORM = 'windows'
  & 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\bin\sdurws_engineeringrequirements_icon_test.exe'
  ```

  Expected: the icon test exits successfully and both assertions pass. Launch only this executable in the command.

- [ ] **Step 4: Commit constructor and regression assertion**

  ```powershell
  git add -- RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp
  git commit -m "Load EngineeringRequirements toolbar icon"
  ```

### Task 4: Render the four engineering plugin actions as icons only

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rws/RobWorkStudioPlugin.cpp:122-125`
- Modify: `RobWork/RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp` only if a private helper declaration is needed
- Test: `RobWork/RobWorkStudio/src/rws/RobWorkStudioPluginToolbarTest.cpp` (new focused Qt test)
- Modify: `RobWork/RobWorkStudio/src/rws/CMakeLists.txt` to register the focused test when `BUILD_TESTING` is enabled

- [ ] **Step 1: Write the failing toolbar presentation test**

  Create a small `QApplication` test with a fake subclass whose name/icon are supplied to the `RobWorkStudioPlugin` constructor. Add the action to a `QToolBar` through `setupToolBar`, then assert:

  - names `EngineeringRequirements`, `RobotModelBuilder`, `KinematicAnalysis`, and `StructureOptimizer` produce a `QToolButton` with `Qt::ToolButtonIconOnly`;
  - the action text remains the full plugin name;
  - the action tooltip remains the full plugin name;
  - an unrelated plugin name produces the existing text-and-icon style.

  Use this test body (with `QCOMPARE`/`QVERIFY` from `<QtTest>` and a `main` that constructs `QApplication`) to keep the behavior deterministic:

  ```cpp
  class ProbePlugin final : public rws::RobWorkStudioPlugin {
  public:
      explicit ProbePlugin(const QString& name)
          : rws::RobWorkStudioPlugin(name, QIcon()) {}
  };

  static void checkStyle(const QString& name, Qt::ToolButtonStyle expected)
  {
      QToolBar toolbar;
      ProbePlugin plugin(name);
      plugin.setupToolBar(&toolbar);
      QAction* action = toolbar.actions().constFirst();
      auto* button = qobject_cast<QToolButton*>(toolbar.widgetForAction(action));
      QVERIFY(button != nullptr);
      QCOMPARE(button->toolButtonStyle(), expected);
      QCOMPARE(action->text(), name);
      QCOMPARE(action->toolTip(), name);
  }
  ```

  Call `checkStyle` for each of the four names with `Qt::ToolButtonIconOnly` and once for `OtherPlugin` with the pre-existing toolbar style.

- [ ] **Step 2: Run the test before implementation**

  ```powershell
  ctest --test-dir RobWork/build/codex-vs-debug5 -C Debug -R sdurws_plugin_toolbar_test --output-on-failure
  ```

  Expected: FAIL because the shared toolbar setup currently leaves every action in the default style.

- [ ] **Step 3: Implement the scoped icon-only policy**

  In `RobWorkStudioPlugin::setupToolBar`, keep the action text and add it to the toolbar first. Set the tooltip/status tip to `_name`, then when `_name` is one of the four exact plugin names, obtain `QToolButton* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(&_showAction));` and call `button->setToolButtonStyle(Qt::ToolButtonIconOnly)`. Leave all other plugin actions untouched.

- [ ] **Step 4: Run the focused test after implementation**

  ```powershell
  $env:QT_QPA_PLATFORM = 'windows'
  & 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\bin\sdurws_plugin_toolbar_test.exe'
  ```

  Expected: PASS with the four exact names icon-only and the unrelated fake plugin preserving text.

- [ ] **Step 5: Commit the toolbar behavior**

  ```powershell
  git add -- RobWork/RobWorkStudio/src/rws/RobWorkStudioPlugin.cpp RobWork/RobWorkStudio/src/rws/RobWorkStudioPlugin.hpp RobWork/RobWorkStudio/src/rws/RobWorkStudioPluginToolbarTest.cpp RobWork/RobWorkStudio/src/rws/CMakeLists.txt
  git commit -m "Show engineering plugin toolbar actions as icons"
  ```

### Task 5: Verify the integrated RobWorkStudio UI

**Files:**
- No source changes expected; inspect the four modified plugin/resource paths and the existing three PNGs.

- [ ] **Step 1: Build the application and affected plugins**

  ```powershell
  cmake --build RobWork/build/codex-vs-debug5 --config Debug --target RobWorkStudio -j 6
  ```

- [ ] **Step 2: Launch one GUI executable under the required Windows environment**

  In Visual Studio x64 developer PowerShell:

  ```powershell
  $env:QT_QPA_PLATFORM = 'windows'
  & 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\bin\RobWorkStudio.exe'
  ```

  Expected: no Qt platform initialization error; stop and inspect inherited `QT_*`/`QML_*` variables if one occurs.

- [ ] **Step 3: Check the four toolbar entries**

  Confirm the order and icons are unchanged except that the four names are no longer rendered as visible button text. Hover each button and verify its tooltip is respectively `EngineeringRequirements`, `RobotModelBuilder`, `KinematicAnalysis`, and `StructureOptimizer`; open the Plugins menu and verify the full names remain there.

- [ ] **Step 4: Check existing assets and diff hygiene**

  Use the image viewer to compare the three existing PNGs and run:

  ```powershell
  git diff --check HEAD~4..HEAD
  git status --short
  ```

  Expected: no whitespace errors, the three existing PNGs unchanged, and only the planned icon/resource/plugin/toolbar files represented by the implementation commits (unrelated pre-existing worktree changes remain untouched).
