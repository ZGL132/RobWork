# WorkCell Project Model Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** A project created from a WorkCell owns one generated RobotModelBuilder `.rmb.json`, and EngineeringRequirements binds it before freezing only when it is unambiguous and names a WorkCell device.

**Architecture:** This is scheme A: a WorkCell project has one current engineering robot, registered as `robot-model.main`. RobotModelBuilder writes it transactionally to `generated/robot-models/<sanitized-robot-name>.rmb.json`; EngineeringRequirements scans only that managed directory and binds only when exactly one file exists and its `robotName` matches a current WorkCell device. It never guesses among multiple model files.

**Tech Stack:** C++17, Qt 6, RobWorkStudio ProjectDocumentRegistry, RobotModelSpecJson, existing CTest executables.

---

### Task 1: Register the single generated model resource

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.{hpp,cpp}`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp`

- [x] After `WorkCellConverter::hasConvertibleRobotModel`, create exactly one resource:

```cpp
resource.id = QStringLiteral("robot-model.main");
resource.kind = QStringLiteral("robwork.robot-model");
resource.path = QStringLiteral("generated/robot-models/%1.rmb.json").arg(sanitizedRobotName);
resource.ownership = QStringLiteral("generated");
resource.required = true;
```

- [x] Add the main WorkCell resource ID as a dependency when the project has one.
- [x] On first registration, adopt the generated resource in `CallbackProjectDocumentProvider`, establish an empty Widget snapshot baseline, and set the Provider dirty. This makes the first project save transaction write the `.rmb.json` without direct side effects during WorkCell synchronization.
- [x] Test that a freshly registered generated model is dirty, and becomes clean only after `markProjectDocumentClean()`.

### Task 2: Bind managed models before freezing requirements

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.{hpp,cpp}`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp`
- Test: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [x] Propagate `RobWorkStudio::projectContextChanged` and the initial project directory to the requirements Widget.
- [x] Search only `<project>/generated/robot-models/*.rmb.json`.
- [x] Require exactly one file and a `robotName` equal to a current WorkCell `Device`; fill `sourcePath`, `robotName`, and the canonical model fingerprint only after both checks succeed.
- [x] Call the resolver from `freezeRequirements()` only when no explicit model binding already exists.
- [x] Keep manual bindings intact and produce an actionable error when the managed model is missing, ambiguous, unreadable, or mismatched.
- [x] Test both successful binding and rejection of an ambiguous two-model directory.

### Task 3: Verification

- [x] Build `sdurws_robotmodelbuilder`, `sdurws_engineeringrequirements`, and their focused tests with the Visual Studio x64 environment.
- [x] Run `sdurws_robotmodelbuilder_widgettest` and `sdurws_engineeringrequirements_test widget` with Qt's offscreen platform plugin.

### Intentional Scope Boundary

This plan does not introduce a multi-robot selector. A project with multiple generated `.rmb.json` files remains blocked at the EngineeringRequirements freeze gate until the user removes the ambiguity or the project is migrated to an explicit multi-robot workflow.
