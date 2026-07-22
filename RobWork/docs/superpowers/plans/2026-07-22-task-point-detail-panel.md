# Task Point Detail Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Task Points tab easier to scan by keeping only primary fields in the main table and showing the selected row's secondary fields below.

**Architecture:** Keep `TaskPointTableModel` unchanged so CSV import/export and analysis data stay stable. Add UI helper functions that define compact table columns and detail columns, test those helpers, then use them from `KinematicAnalysisWidget` to hide table columns and populate a read-only detail table.

**Tech Stack:** C++17, Qt Widgets, existing `sdurws_kinematicanalysis_test` executable.

---

### Task 1: Column Grouping Helpers

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Add failing tests that expect compact Task Point table columns to contain the scan-critical fields and detail columns to contain the long/secondary fields.
- [ ] Run `sdurws_kinematicanalysis_test.exe all` and confirm the test fails because the helper functions are missing.
- [ ] Implement `taskPointCompactTableColumns()` and `taskPointDetailColumns()` in `KinematicAnalysisUiLogic`.
- [ ] Re-run the test executable and confirm the new tests pass.

### Task 2: Compact Table And Detail Panel

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] Add `_taskPointDetailTable` and an `updateTaskPointDetails()` slot.
- [ ] Build a read-only detail table below `_taskPointTable` with two columns, matching the existing IK detail table style.
- [ ] Hide all columns except `taskPointCompactTableColumns()` in `setTaskPointTableColumnWidths()`.
- [ ] Populate detail rows from model display data for the selected row; show an empty message when no row is selected.
- [ ] Refresh the detail panel on selection, edits, model reset, inserted rows, removed rows, and applied analysis results.

### Task 3: Verification

**Files:**
- Verify: `RobWorkStudio/src/rwslibs/kinematicanalysis`

- [ ] Build `sdurws_kinematicanalysis`.
- [ ] Run `sdurws_kinematicanalysis_test.exe all`.
- [ ] Review `git diff --stat` and ensure only the planned files changed.
