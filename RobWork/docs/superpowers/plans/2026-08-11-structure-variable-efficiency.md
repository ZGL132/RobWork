# Structure Variable Efficiency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make structure design variables easier to find and manage without weakening their existing validation or run gate.

**Architecture:** Add a focused `QSortFilterProxyModel` between the variable source model and table view. Extend the source model with persisted preference columns, while the widget owns query controls and always maps user selections back to source rows before mutation.

**Tech Stack:** C++17, Qt Widgets/Model-View, existing standalone Qt test executable, CMake.

---

### Task 1: Proxy filtering

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableFilterProxyModel.hpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableFilterProxyModel.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [x] Write a failing `variable_table` test that constructs the proxy, sets keyword/type criteria, and verifies the AND-filtered rows.
- [x] Run `sdurws_structureoptimizer_test.exe variable_table` and confirm the test fails because the proxy class is unavailable.
- [x] Implement `setKeyword`, `setKindFilter`, and `filterAcceptsRow` using source display data for ID, label, target, and kind.
- [x] Add proxy sources to the library and test target; rerun `variable_table` until it passes.

### Task 2: Advanced preference columns

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [x] Write a failing model test for `PreferredColumn` and `PreferenceWeightColumn`, including rejection of an out-of-range weight.
- [x] Run the `variable_table` suite and confirm it fails for the missing column API.
- [x] Add the two columns, numeric formatting/alignment, and editable preference writes via `setPreferences` validation.
- [x] Rerun the `variable_table` suite until it passes.

### Task 3: Variable-page controls and mapped actions

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [x] Write a failing widget test for search/type controls, default-hidden advanced columns, and adding only missing suggestions.
- [x] Run the `widget` suite under `QT_QPA_PLATFORM=windows` and confirm it fails because the controls do not exist.
- [x] Bind the table to the proxy, map duplicate/remove selections to source indexes, add controls, and disable all variable actions while running.
- [x] Rerun `widget`, then `variable_actions` and `variable_table` as separate executable launches.

### Task 4: Final verification

**Files:**
- Verify: all files above

- [x] Build `sdurws_structureoptimizer_test` with `scripts\\build-msvc-debug.cmd sdurws_structureoptimizer_test`.
- [x] Run `variable_actions` and `variable_table` separately as model-only tests.
- [x] Run `widget` once with `$env:QT_QPA_PLATFORM='windows'` and the executable absolute path.
- [x] Run `git diff --check` and inspect the diff for unrelated changes.
