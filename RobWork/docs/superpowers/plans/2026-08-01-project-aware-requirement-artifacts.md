# Project-Aware Requirement Artifacts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist engineering requirements as a stable project resource, keep exported copies non-authoritative, and let downstream plugins resolve frozen requirements by resource ID.

**Architecture:** The engineering-requirements plugin mirrors the existing KinematicAnalysis generated-resource lifecycle. Its widget only supplies project-aware dialog defaults; the plugin creates and adopts `engineering-requirements.main`. KinematicAnalysis and StructureOptimizer receive a project-resource resolver and prefer the manifest path when their existing import commands run.

**Tech Stack:** C++17, Qt 6 Widgets, RobWorkStudio `ProjectManager`, `ProjectDocumentRegistry`, Catch2-style plugin tests.

---

### Task 1: Requirement primary-resource lifecycle

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] **Step 1: Write the failing project-lifecycle test**

Add a test that creates a project-context fake studio, emits the widget's
`requirementsChanged` signal after a requirement edit, and asserts an ensured
resource with ID `engineering-requirements.main`, kind
`rws.engineering-requirements`, path `requirements/main.requirements.json`,
ownership `generated`, and dependencies on the main WorkCell and model.

- [ ] **Step 2: Run the focused engineering requirements test**

Run: `sdurws_engineeringrequirements_test.exe`

Expected: the new lifecycle assertion fails because the plugin only marks its
Provider dirty and has not registered a resource.

- [ ] **Step 3: Implement generated-resource creation and adoption**

In the requirements-changed connection, construct the primary resource, call
`ensureGeneratedProjectResource`, adopt it when created, and initialize the
widget's project document path from the project directory before setting dirty.
Do nothing when no project or no main WorkCell is available.

- [ ] **Step 4: Re-run the focused test**

Run: `sdurws_engineeringrequirements_test.exe`

Expected: the lifecycle test passes.

### Task 2: Project-aware requirement copy dialogs

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] **Step 1: Write failing path-policy tests**

Expose a narrow static/helper API for the default export and import locations.
Test an open project yields
`requirements/exports/requirements-copy.requirements.json` for export and the
existing exports directory (or requirements directory) for import; test empty
project context preserves the standalone default.

- [ ] **Step 2: Run the focused engineering requirements test**

Run: `sdurws_engineeringrequirements_test.exe`

Expected: the path-policy tests fail because the widget passes a fixed filename
and empty import directory to `QFileDialog`.

- [ ] **Step 3: Implement the path policy and preserve copy semantics**

Use `_projectOutputDirectory` to build default dialog paths. Keep
`saveRequirements()` calling only `writeRequirementDocument`; do not call
`beginProjectDocument`, `markProjectDocumentClean`, or a project-manager API.

- [ ] **Step 4: Re-run the focused test**

Run: `sdurws_engineeringrequirements_test.exe`

Expected: all requirements tests pass.

### Task 3: Manifest-first frozen-requirement import

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: Write failing consumer tests**

For each consumer, inject a project context containing
`engineering-requirements.main` and assert its import service receives the
resolved resource path without invoking a file dialog. Add the no-resource
case asserting that manual import remains available with the project
requirements directory as default.

- [ ] **Step 2: Run both focused consumer tests**

Run: `sdurws_kinematicanalysis_test.exe`

Run: `sdurws_structureoptimizer_test.exe`

Expected: the project-resource cases fail because both commands unconditionally
open `QFileDialog`.

- [ ] **Step 3: Implement manifest-first path selection**

Use the already available `RobWorkStudio` pointer in each widget to call
`resolveProjectResource("engineering-requirements.main", ...)`. Reuse the
existing parse/validation logic with that resolved path. Only invoke
`QFileDialog` when resolution fails; give it `<project>/requirements/` as the
initial directory.

- [ ] **Step 4: Re-run both focused consumer tests**

Run: `sdurws_kinematicanalysis_test.exe`

Run: `sdurws_structureoptimizer_test.exe`

Expected: all consumer tests pass.

### Task 4: Project-system regression validation

**Files:**
- Test: `RobWorkStudio/gtest/rws/ProjectDocumentRegistryTest.cpp`

- [ ] **Step 1: Add a project document registry test**

Create a manifest containing the optional generated requirements resource and
assert dependency order loads WorkCell and model before requirements, and a
transactional save creates `requirements/main.requirements.json` only through
its registered Provider.

- [ ] **Step 2: Run the project-system test target**

Run: `sdurws_sdurws-gtest.exe`

Expected: the new test fails before the lifecycle integration is complete.

- [ ] **Step 3: Verify the final integration**

Run: `sdurws_sdurws-gtest.exe`

Expected: all project-system tests pass.

- [ ] **Step 4: Build and inspect**

Run: `cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target RobWorkStudio --config Debug`

Run: `git diff --check`

Expected: build succeeds and the diff has no whitespace errors.
