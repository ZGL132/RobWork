# Structure Model Status Guidance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Present model-snapshot status and recovery actions without changing the optimization run gate or snapshot ownership.

**Architecture:** Extend the existing source-status enum/checker with an incomplete embedded-model state. The widget translates checker output into a dedicated banner and routes its actions to existing project-creation slots.

**Tech Stack:** C++17, Qt Widgets, existing structure optimizer test executable.

---

### Task 1: Model-status semantics

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/RobotModelStalenessChecker.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/RobotModelStalenessChecker.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Write a failing widget test for an incomplete embedded model and frozen-snapshot Stale behavior.
- [ ] Run the `widget` test suite with the Windows Qt platform and confirm the missing banner assertions fail.
- [ ] Return `ModelSpecIncomplete` before provenance checks when the embedded specification lacks a robot name or joints.
- [ ] Verify the existing `model_staleness` suite preserves all source-file states.

### Task 2: Status banner and guidance actions

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Add a banner with message, source path, and buttons connected to existing model/frozen-requirement project creation slots.
- [ ] Update the banner on every run-state refresh while keeping `hasRunnableInputs()` unchanged.
- [ ] Keep the banner hidden for `Current` and visible for every other state.
- [ ] Run `widget` and verify no source update changes the collected embedded snapshot.

### Task 3: Final verification

**Files:**
- Verify: all files above

- [ ] Build `sdurws_structureoptimizer` and `sdurws_structureoptimizer_test`.
- [ ] Run `model_staleness` and `variable_table` as model-only tests.
- [ ] Run `widget` with `$env:QT_QPA_PLATFORM='windows'` and the executable absolute path.
- [ ] Run `git diff --check` for the phase files.
