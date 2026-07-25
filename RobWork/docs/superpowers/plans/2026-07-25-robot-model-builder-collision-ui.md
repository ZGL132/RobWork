# Robot Model Builder Collision UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the split collision UI with one concise Collision tab, make Drawables visual-only, and preserve legacy collision behavior by importing old Drawable collision attributes as independent models.

**Architecture:** The spec drops UI-only detail/provenance fields and Drawable collision state. `WorkCellConverter` owns the old XML migration, `RobotModelXmlWriter` emits only independent collision geometry, and `RobotModelBuilderWidget` owns the unified editor, file-picker cells, model-to-Drawable mesh-file sync, and effective-rule preview.

**Tech Stack:** C++17, Qt Widgets, RobWork XML/JSON support, CMake/Ninja, existing robotmodelbuilder test executables.

---

### Task 1: Remove obsolete collision presentation state

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJson.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJsonTest.cpp`

- [ ] **Step 1: Write the failing JSON regression test**

Add a `RobotModelSpecJsonTest` case that loads JSON containing legacy `visualDetail`, Drawable `collisionModel`, `geometryDetail`, and collision-model `source`, then serializes the resulting spec and asserts those four keys are absent while `CollisionModelSpec::enabled` survives.

- [ ] **Step 2: Run the JSON test to verify it fails**

Run:
```powershell
& '.\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_jsontest.exe'
```
Expected: FAIL because the serializer still emits at least one removed key.

- [ ] **Step 3: Remove the data fields and JSON mappings**

Delete `DrawableSpec::visualDetail`, `DrawableSpec::collisionModel`, `CollisionModelSpec::geometryDetail`, and `CollisionModelSpec::source`. Remove their JSON writers and readers; unknown legacy JSON keys remain ignored by the existing parser. Keep `CollisionModelSpec::enabled` read/write behavior unchanged.

- [ ] **Step 4: Run the JSON test to verify it passes**

Run the command from Step 2 after rebuilding `sdurws_robotmodelbuilder_jsontest` serially. Expected: PASS.

- [ ] **Step 5: Preserve the user's working tree**

Do not stage or commit files. The repository contains pre-existing, related uncommitted work.

### Task 2: Migrate old Drawable collision XML into independent models

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp`

- [ ] **Step 1: Write failing converter and writer tests**

Add converter coverage for a legacy `<Drawable colmodel="Enabled">` fixture. Assert it imports as a visual Drawable plus exactly one equivalent enabled `CollisionModelSpec`. Add a duplicate independent collision model and assert no second model is created. Add writer coverage that a Drawable never emits `colmodel="Enabled"` and that only enabled collision models produce `<CollisionModel>` XML.

- [ ] **Step 2: Run the converter/XML tests to verify they fail**

Run:
```powershell
& '.\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_workcellconvertertest.exe'
& '.\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe'
```
Expected: FAIL because legacy Drawables still retain collision state and the writer emits `colmodel`.

- [ ] **Step 3: Implement normalized import and export**

In `WorkCellConverter`, preserve the parsed legacy attribute only long enough to construct an equivalent collision model. Compare `refFrame`, `shape`, `filePath`, dimensions, radius, length, RPY, and position before appending; choose `DrawableNameCollision`, adding a numeric suffix on name collision. In `RobotModelXmlWriter`, remove the Drawable `colmodel` emission. Continue emitting only `collision.enabled` independent models.

- [ ] **Step 4: Run the converter/XML tests to verify they pass**

Rebuild and run the two commands from Step 2. Expected: PASS.

### Task 3: Build the unified Collision UI and deterministic geometry synchronization

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Add a widget-level behavior test or a pure helper test seam**

Extract a deterministic helper that accepts collision name/ref frame/shape and a `std::vector<DrawableSpec>`, returns the matching file path, and tests that it prefers same name then same frame for `STL`, `Mesh`, and `Polytope`, returning empty for non-mesh shapes. Keep the helper in a testable non-UI code path if the Qt widget test harness is unavailable.

- [ ] **Step 2: Run the new test to verify it fails**

Run the test executable that owns the helper. Expected: FAIL because matching and mesh-shape gating have not been implemented.

- [ ] **Step 3: Implement the single Collision tab**

Replace the two existing collision tabs with one `Collision` tab. Use columns `Enabled`, `Name`, `RefFrame`, `Shape`, `Size`, `File`, and `Pose`; map Size to dimensions and Pose to RPY/Pos without exposing Radius/Length as separate UI columns. Remove Drawables columns `Use as Collision` and `Visual Detail`. Use one shared shape combo list for both tables. Install a file-picker cell widget for both File columns and copy the selected path into the table item.

- [ ] **Step 4: Implement File synchronization**

On collision Shape changes to `STL`, `Mesh`, or `Polytope`, use the helper to copy the best matching Drawable path into File. Do not overwrite File for other shapes. Run the same rule when filling a new collision row so generated UI state is consistent.

- [ ] **Step 5: Verify widget compilation**

Run:
```powershell
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && "D:\software\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release -- -j1'
```
Expected: target links successfully.

### Task 4: Collapse CollisionSetup editing and refresh effective exclusions

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add the effective-exclusion helper test**

Add a deterministic helper test that supplies `Base`, three ordered joints, enabled manual/imported pairs, and each checkbox combination. Assert the output includes base-first and adjacent pairs only when selected, retains enabled explicit pairs, and adds exactly one `Static pairs excluded` summary row when enabled.

- [ ] **Step 2: Run the test to verify it fails**

Run the target owning the helper. Expected: FAIL because the preview list is absent.

- [ ] **Step 3: Implement rules and advanced section**

Place the five CollisionSetup controls under Collision Models. Put the editable pair table and its Add/Remove/Generate Defaults buttons in a checkable `Advanced...` section collapsed by default. Add a read-only effective-exclusions list below the rules. Connect each rules checkbox and pair-table change to refresh the list. Keep `collectSpec` and `fillCollisionSetupTab` bound to the same underlying `CollisionSetupSpec` fields.

- [ ] **Step 4: Run targeted tests and verify them**

Rebuild and run the owning test executable plus `sdurws_robotmodelbuilder_xmltest`. Expected: PASS.

### Task 5: Full verification and manual UI inspection

**Files:**
- Modify only if test failures identify a defect in the prior tasks.

- [ ] **Step 1: Build all affected targets serially**

Run:
```powershell
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && "D:\software\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder sdurws_robotmodelbuilder_xmltest sdurws_robotmodelbuilder_jsontest sdurws_robotmodelbuilder_workcellconvertertest --config Release -- -j1'
```
Expected: all targets build without errors.

- [ ] **Step 2: Run all robotmodelbuilder tests**

Run each executable from `build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin`: `sdurws_robotmodelbuilder_xmltest.exe`, `sdurws_robotmodelbuilder_jsontest.exe`, and `sdurws_robotmodelbuilder_workcellconvertertest.exe`.

Expected: every executable exits with `0`.

- [ ] **Step 3: Inspect the plugin UI**

Launch the built plugin host, load `RobWork/example/ModelData/XMLDevices/UR-6-85-5-A`, and verify there is one Collision tab, the advanced table starts collapsed, File cells open a picker, and each rules checkbox refreshes Effective Exclusions immediately.

- [ ] **Step 4: Report verification limits accurately**

If the pre-existing CMake cache requires a complete dependency rebuild beyond the session window, report that separately from the direct executable test results and do not claim a clean rebuild without evidence.
