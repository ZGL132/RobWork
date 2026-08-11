# Kinematic Validate Inspector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn Validate into a Frozen Requirements-first result inspector while preserving every Local Tasks and Verified validation workflow.

**Architecture:** Keep the current task and region validation algorithms and result tables, but replace duplicate command presentation with a single source-aware command strip. Add a small presentation layer in `KinematicAnalysisWidget` that renders validation summary, selected-item details, and deterministic default selection from the existing `RequirementValidationSummary` and execution set.

**Tech Stack:** C++17, Qt 6 Widgets, RobWorkStudio, CMake/CTest.

---

## File Structure

- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Remove retired hidden Validate command members; declare the summary/inspector widgets and
    selection-rendering helpers.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Build the source-first Validate UI, centralize validation presentation, and preserve current
    full/selected Local Task and Frozen Requirement operations.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Replace retired-control assertions with focused UI behavior tests for source default, commands,
    summary, inspector, selection, diagnostics, and narrow layouts.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Describe the Validate source flow and result inspector.

## Task 1: Establish the Validate UI Contract in Tests

**Files:**

- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp:5109-6260`

- [ ] **Step 1: Add the failing workflow UI assertions**

  In `testWorkflowUiStates`, replace the expectation that Local Tasks is initially selected with
  Frozen Requirements. Discover these objects by stable name: `mode2DataSourceCombo`,
  `mode2LoadJsonButton`, `mode2ValidateAllButton`, `mode2ValidateSelectedButton`,
  `validateSummaryLabel`, `validateInspectorTable`, and `validateDiagnosticsToggle`.

  Add assertions equivalent to:

  ```cpp
  if (const int rc = require (
          mode2Source->currentData ().toInt () == 1 && mode2Load->isVisible () &&
              !mode2Add->isVisible () && !mode2Remove->isVisible (),
          "Frozen Requirements is the default Validate source with its command set"))
      return rc;
  if (const int rc = require (
          validateSummary->text ().contains (QStringLiteral ("Not validated")) &&
              inspector->rowCount () == 1 && !diagnostics->isChecked (),
          "Validate starts with an empty summary, inspector, and collapsed diagnostics"))
      return rc;
  ```

  After loading the existing v4 execution fixture and running full validation, assert that the
  summary contains `Verified`, that a selected result fills the inspector, and that selection of a
  task and a region updates the inspector with item-owned values. Keep the existing assertions for
  v3 rejection, selected stable-ID validation, report replacement, and no-WorkCell disabling.

- [ ] **Step 2: Build and run the focused test before implementation**

  Run:

  ```powershell
  cmake --build . --config Debug --target sdurws_kinematicanalysis_test
  ctest -C Debug --output-on-failure -R '^sdurws_kinematicanalysis_test_workflow_ui$'
  ```

  Expected: the workflow UI test fails because Frozen Requirements is not the default and the
  summary/inspector objects do not exist.

## Task 2: Replace Duplicate Validate Commands and Build the Inspector Surface

**Files:**

- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp:392-427`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp:936-1085`

- [ ] **Step 1: Remove retired presentation-only command fields**

  Delete `_validateLoadRequirementsButton`, `_validateRunButton`, and `_validateExportButton`
  from the header and constructor initializer list. Retain `openFrozenRequirementsForValidation`,
  `validateRequirements`, and the report menu actions; only their duplicate hidden widgets are
  retired.

- [ ] **Step 2: Add source-aware summary and inspector members**

  Add these declarations next to the remaining Validate controls:

  ```cpp
  QLabel* _validateSummaryLabel;
  QLabel* _validateInspectorTitleLabel;
  QTableWidget* _validateInspectorTable;

  void refreshValidationSummary ();
  void updateValidationInspector ();
  void setValidationInspectorEmpty ();
  void selectPreferredValidationResult ();
  void selectValidationResult (bool region, const QString& stableId);
  ```

  Initialize each widget pointer to `NULL` in the constructor initializer list.

- [ ] **Step 3: Build a single command strip and ordered result area**

  In the Validate constructor block, use the existing `_mode2LoadJsonButton`,
  `_mode2ValidateSelectedButton`, `_mode2ValidateAllButton`, `_mode2AddButton`,
  `_mode2RemoveButton`, and `_reportButton` as the only commands. Put them in a two-row grid:
  a source-specific child group followed by shared actions. The Frozen child group contains
  `Load JSON`; the Local child group contains `Add` and `Remove`. Both groups occupy the same
  position but are switched by `updateMode2DataSource()`. Place the shared `Validate selected`,
  `Validate all`, and `Report` buttons after the group. Reparent `Add` and `Remove` from
  `localTaskActions` into the Local child group and remove `localTaskActions`.

  Create a `validateSummaryLabel` beneath the source and command sections, initialized to
  `Validation summary: Not validated`. Create an inspector after the task and region summary
  tables with a two-column `QTableWidget` named `validateInspectorTable`, non-editable,
  row-selectable, native styled, and initialized by `setValidationInspectorEmpty()`. Rename the
  diagnostics toggle text to `Advanced diagnostics`; retain its object name and collapsed state.

- [ ] **Step 4: Make Frozen Requirements the initial source**

  After both source items are added, set:

  ```cpp
  _mode2DataSourceCombo->setCurrentIndex (
      _mode2DataSourceCombo->findData (1));
  ```

  Keep `updateMode2DataSource` as the single visibility gate. For Local Tasks it shows the editable
  task page and local commands; for Frozen Requirements it shows artifact state, task/region
  summaries, inspector, and Advanced diagnostics. Do not hide the region table accidentally in
  either source.

- [ ] **Step 5: Compile the UI target**

  Run:

  ```powershell
  cmake --build . --config Debug --target sdurws_kinematicanalysis_test
  ```

  Expected: successful compilation; the newly added UI-contract assertions may still fail until
  Task 3 provides the summary and inspector behavior.

## Task 3: Centralize Validation Presentation and Default Selection

**Files:**

- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp:2000-2460`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp:160-180`

- [ ] **Step 1: Add one summary renderer**

  Implement `refreshValidationSummary()` from `_validateSummary` for Frozen Requirements and
  `_lastTaskPointResults` for Local Tasks. Before results, render
  `Validation summary: Not validated`. After results, count Frozen task/region feasibility or Local
  `TaskPointReachabilityResult::status` and render one line containing source (`Verified frozen
  requirements` or `Quick local tasks`), evidence stage, Must result when applicable, and
  Pass/Warning/Fail/DataInsufficient counts. The renderer must read existing result vectors and
  must not rerun any analysis.

  Use a local helper for count text rather than duplicating table scans:

  ```cpp
  struct ValidationStatusCounts {
      int pass = 0;
      int warning = 0;
      int fail = 0;
      int insufficient = 0;
  };
  ```

  Count `Feasible + Good` as pass, `Feasible + Degraded` as warning,
  `DataInsufficient` as insufficient, and every other non-feasible state as fail.

- [ ] **Step 2: Add item-owned inspector rendering**

  `setValidationInspectorEmpty()` sets one row, `Selection | No validation result selected.`, and
  clears the inspector title. `updateValidationInspector()` is used only for Frozen Requirements;
  it reads the selected stable ID from the first column `Qt::UserRole` of the task table or region
  table. It then finds the matching result in `_validateSummary` and writes these rows:

  ```text
  Type, ID, Level, Feasibility, Quality, Evidence stage, Failure reason
  ```

  Add `Position residual`, `Orientation residual`, and `Reachable solutions` for a task. Add
  `Position coverage`, `Orientation coverage`, `Sampled cells`, `Directions`, and `Rolls` for a
  region. Use `-` for evidence that is unavailable; do not consult current pose or Diagnose IK
  state.

- [ ] **Step 3: Implement deterministic default selection**

  `selectPreferredValidationResult()` walks `_validateExecution.tasks` and
  `_validateExecution.workspaceRegions` in stored order. It selects the first matching result
  whose level is `Must` and whose feasibility is not `Feasible`; otherwise it selects the first
  populated task row, then the first populated region row. For selected-only validation, invoke
  `selectValidationResult(region, stableId)` instead so that user intent wins.

  Connect both result tables' `itemSelectionChanged()` signals to
  `updateValidationInspector()`. When a result is selected in one table, clear the selection in the
  other with `QSignalBlocker` to ensure one inspector owner.

- [ ] **Step 4: Route all result-producing paths through the renderers**

  After `populateFrozenRequirementSources()` and `validateRequirements()`, call these methods in
  this order:

  ```cpp
  refreshValidationSummary ();
  selectPreferredValidationResult ();
  updateValidationInspector ();
  refreshWorkflowControls ();
  ```

  After each Frozen selected-only branch, call `refreshValidationSummary()`,
  `selectValidationResult(region, stableId)`, `updateValidationInspector()`, and
  `refreshWorkflowControls()`. For Local Tasks, call `refreshValidationSummary()` after full or
  selected batch analysis, retain the existing local task-detail panel as that source's inspector,
  and leave Frozen-only inspector/diagnostics hidden. On WorkCell unload and any result reset,
  call `setValidationInspectorEmpty()` and restore the `Not validated` summary.

- [ ] **Step 5: Run the focused workflow test**

  Run:

  ```powershell
  ctest -C Debug --output-on-failure -R '^sdurws_kinematicanalysis_test_workflow_ui$'
  ```

  Expected: PASS, including the new default-source, summary, inspector, default-selection, and
  Advanced diagnostics assertions.

## Task 4: Remove Obsolete Control Paths and Document the Workflow

**Files:**

- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp:1690-1750`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp:392-427`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp:6011-6041`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Delete references to retired hidden controls**

  Remove the `_validateLoadRequirementsButton`, `_validateRunButton`, and
  `_validateExportButton` visibility and enablement branches from `refreshWorkflowControls()` and
  their legacy visibility assertions from the test. Keep readiness rules on the unified command
  widgets:

  ```cpp
  _mode2LoadJsonButton->setEnabled (!localTasks && hasTcp);
  _mode2ValidateAllButton->setEnabled (
      localTasks ? hasTcp : (hasTcp && _validateExecutionSet));
  _mode2ValidateSelectedButton->setEnabled (
      hasTcp && (localTasks ? localSelection :
          (_validateExecutionSet && (frozenTaskSelection || frozenRegionSelection))));
  ```

  Keep the report menu available; its actions continue to use `buildReportForExport()` and its
  existing result cache.

- [ ] **Step 2: Update the README**

  Add a `Validate Layout` section that documents Frozen Requirements as the default Verified path,
  Local Tasks as editable Quick analysis, full versus selected validation, summary/inspector
  behavior, and the Advanced diagnostics contents. Do not alter the existing Diagnose or Explore
  descriptions beyond cross-referencing Validate.

- [ ] **Step 3: Run static cleanup checks**

  Run:

  ```powershell
  rg -n "validateLoadRequirementsButton|validateRunButton|validateExportButton" `
      RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp `
      RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp
  git diff --check -- RobWorkStudio/src/rwslibs/kinematicanalysis
  ```

  Expected: the retired widget names have no source matches; `git diff --check` exits with code 0.

## Task 5: Full Verification

**Files:**

- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Build and test Debug**

  Run:

  ```powershell
  cmake --build . --config Debug --target sdurws_kinematicanalysis_test
  ctest -C Debug --output-on-failure -R '^sdurws_kinematicanalysis_test'
  ```

  Expected: all 25 kinematic-analysis tests pass.

- [ ] **Step 2: Build and test Release through the short path**

  Run from `build/rel-short`:

  ```powershell
  cmake --build . --config Release --target sdurws_kinematicanalysis_test
  ctest -C Release --output-on-failure -R '^sdurws_kinematicanalysis_test'
  ```

  Expected: all 25 kinematic-analysis tests pass. The short path avoids the Windows generated-MOC
  path-length limit in the normal Release build directory.

- [ ] **Step 3: Confirm scope and leave changes unstaged**

  Run:

  ```powershell
  git diff --name-only -- RobWorkStudio/src/rwslibs
  git status --short
  ```

  Expected: only `RobWorkStudio/src/rwslibs/kinematicanalysis` changes belong to this work. Do not
  stage or commit; preserve unrelated user changes.
