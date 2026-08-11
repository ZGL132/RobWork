# Transactional Freeze Publication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatically transaction-save a successful freeze and prevent downstream frozen-requirement imports from consuming stale project files.

**Architecture:** The widget emits a publication request after its normal domain-change signal. The plugin calls a narrow public save API on `RobWorkStudio`; downstream widgets use the same API after a Save and Continue confirmation. The existing registry and save transaction remain the only authoritative persistence path.

**Tech Stack:** C++17, Qt 6 Widgets, RobWorkStudio project providers and `ProjectSaveTransaction`.

---

### Task 1: Main-window save contract

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp`
- Test: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`

- [ ] Add failing tests for aggregated dirty state and public transactional save.
- [ ] Run the focused RobWorkStudio tests and confirm the missing API failure.
- [ ] Add `hasUnsavedProjectChanges()` and `saveCurrentProject(QString*)` as narrow wrappers.
- [ ] Re-run the focused tests and confirm they pass.

### Task 2: Freeze publication request

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp`
- Test: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] Add a failing signal-order test for successful freeze and no-request behavior on failed freeze.
- [ ] Run the widget test and confirm the publication signal is missing.
- [ ] Emit `freezePublicationRequested()` after `requirementsChanged()` and connect it to the project transaction.
- [ ] Report save success or failure in the requirement status without clearing dirty state on failure.
- [ ] Re-run the widget test and confirm it passes.

### Task 3: Downstream save-before-read guard

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Test: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`

- [ ] Add failing tests for proceed, cancel, and save-failure decisions in a dialog-free policy helper.
- [ ] Run the focused tests and confirm the helper does not exist.
- [ ] Implement the policy helper and use it before manifest resource resolution in both consumers.
- [ ] Re-run consumer frozen-requirement and project-system tests.

### Task 4: Verification

**Files:**
- Test all files above.

- [ ] Build the main window and three affected plugin test targets.
- [ ] Run EngineeringRequirements widget tests, KinematicAnalysis frozen-requirement tests,
  StructureOptimizer frozen-requirement tests, and RobWorkStudio project tests.
- [ ] Run `git diff --check` and inspect only task-scoped changes.
