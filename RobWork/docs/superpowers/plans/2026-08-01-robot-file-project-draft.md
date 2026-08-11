# Robot File Project Draft Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user create a draft project from a URDF/XML robot source, review it in RobotModelBuilder, and make `.rmb.json` generation an explicit user-confirmed save step.

**Architecture:** RobWorkStudio owns the top-level file action and creates only an empty project draft. It dispatches the selected source to RobotModelBuilder through a Qt meta-object slot, avoiding a core-to-plugin link dependency. RobotModelBuilder imports the source into editable controls, registers `robot-model.main` as a dirty generated resource, and exposes its dock. Existing WorkCell project creation follows the same review step after opening a convertible robot WorkCell.

**Tech Stack:** C++17, Qt 6 signals/slots, RobWorkStudio ProjectDocumentRegistry, RobotModelUrdfImporter.

---

### Task 1: Add a non-interactive URDF import API

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.{hpp,cpp}`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp`

- [x] Write a failing test that writes a minimal URDF, calls `importUrdfFile(path, &error)`, and verifies the imported robot name is populated.
- [x] Refactor the existing file-dialog slot so it delegates parsing and UI population to `importUrdfFile`.
- [x] Build and run `sdurws_robotmodelbuilder_widgettest` with the Qt offscreen platform plugin.

### Task 2: Create and present a robot project draft

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.{hpp,cpp}`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.{hpp,cpp}`

- [x] Add `Create Project from Robot File...` to the File menu, accepting `*.urdf` and URDF-form XML files.
- [x] Create an empty `.rwproj`, then locate `RobotModelBuilder` from `getPlugins()`, show its dock, and invoke `importRobotProjectSource(QString)` by Qt meta-object.
- [x] Have the plugin import the source, register/adopt `robot-model.main`, establish the generated-document dirty baseline, and report that the user must review and save the project before downstream work is permitted.
- [x] If the Builder plugin is unavailable or source parsing fails, keep the project usable but leave it without a generated model; existing freeze diagnostics remain the hard gate.

### Task 3: Route WorkCell creation through the same review boundary

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp`

- [x] After a WorkCell-derived project opens, locate and show RobotModelBuilder when it registered a convertible model; its existing generated-resource logic keeps the model dirty until project save.
- [x] Preserve pure-scene WorkCell creation: no Builder switch is required and no model resource is generated.

### Task 4: Verify

- [x] Build `sdurws_robotmodelbuilder`, `sdurws_engineeringrequirements`, and focused tests.
- [x] Run the relevant CTest selection with `QT_QPA_PLATFORM=offscreen` and `QT_QPA_PLATFORM_PLUGIN_PATH` pointing to the Qt platforms directory.
