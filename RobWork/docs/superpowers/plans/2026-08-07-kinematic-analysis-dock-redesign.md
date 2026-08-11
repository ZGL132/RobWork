# Kinematic Analysis Dock Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Every behavior change follows red-green-refactor.

**Goal:** Replace the nested KinematicAnalysis tabs with a narrow-dock, three-mode workflow while preserving existing Qt styling, analysis behavior, persistence, exports, and Qt 5/Qt 6 compatibility.

**Architecture:** A fixed two-row header and an exclusive three-button mode selector drive one `QStackedWidget`. Each mode owns a scrollable vertical page. Existing analysis functions and models remain authoritative; new dialogs only edit thresholds or render shared visualization data, and UI routing never merges the workspace and pose-reachability domain types.

**Tech Stack:** C++17, Qt Widgets, Qt Concurrent, RobWorkStudio, CMake, CTest.

---

## Scope and invariants

- Keep the current RobWorkStudio/Qt palette, fonts, spacing conventions, text buttons, and table styling.
- Do not add icon files, icon resources, emoji labels, or a new theme.
- Do not touch unrelated plugins except the existing kinematic-analysis CMake target and tests.
- Preserve `TaskPointTableModel`, `RequirementExecutionSet`, all analyzers/evaluators, project document behavior, report builders, and background worker safety.
- Use text-only compact buttons for this iteration. Tooltips and accessible names remain required where visible text is shortened.
- Do not introduce a third `WorkspaceSamplingMode`; the UI's pose-reachability choice routes to the existing pose pipeline.

## Task 1: Lock the three-mode shell with failing UI tests

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] Add a `workflow_ui` test that requires `kinematicModeSelector`, three exclusive text buttons, and `kinematicModeStack` with exactly three pages.
- [ ] Assert that `workflowTabs` and the nested compatibility tab widget are absent from the visible hierarchy.
- [ ] Add object-name assertions for `deviceCombo`, `tcpFrameCombo`, unit controls, `diagnoseRefreshButton`, `thresholdSettingsButton`, `reportButton`, and the fixed status control.
- [ ] Resize the widget to 300x620, activate each mode, process events, and assert every fixed control has a non-empty geometry inside the widget bounds.
- [ ] Run `cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug` and then the `workflow_ui` suite; record the expected failure caused by the missing new controls.
- [ ] Replace `_workflowTabs` and `_tabs` with an exclusive `QButtonGroup` of three checkable `QToolButton`s and one `QStackedWidget`.
- [ ] Build a fixed two-row header. Row 1 contains Device and TCP controls side by side. Row 2 contains the two unit combos plus text buttons `Refresh`, `Thresholds`, and `Report`.
- [ ] Use `QSizePolicy::Ignored` and zero minimum width for the two long combos. Keep full values in tooltips.
- [ ] Place only the three mode pages in scroll areas; keep header, selector, and status outside scrolling.
- [ ] Re-run the focused suite and confirm green before refactoring repeated page construction helpers.

## Task 2: Add the transactional threshold dialog

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicThresholdsDialog.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicThresholdsDialog.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Add dialog tests for initial values, Cancel leaving the source unchanged, Accept returning all eight values, unit conversion, and rejection of `conditionWarning >= conditionFail`.
- [ ] Run the focused test and verify red because the dialog type is absent.
- [ ] Implement a modal, text-only `QDialog` with a single-column `QFormLayout`, eight `QDoubleSpinBox` controls, validation feedback, and `QDialogButtonBox(Ok|Cancel)`.
- [ ] Keep meters/degrees internally and convert only the displayed position/orientation tolerance.
- [ ] Connect the Header `Thresholds` button to a copy-edit-accept flow. On Accept, update `_thresholds`, synchronize the legacy duplicate-Q control state, emit `projectDocumentChanged`, and mark cached results stale through existing status/update paths.
- [ ] Add the new sources to plugin and test CMake source lists.
- [ ] Re-run threshold tests, `workflow_ui`, and project document tests.

## Task 3: Add the shared-state plot dialog

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicPlotDialog.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicPlotDialog.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Add tests that opening the plot creates one modeless dialog, opening again reuses/raises it, visual data and display state propagate, and closing or clearing a WorkCell cannot leave a dangling dialog pointer.
- [ ] Verify the focused test fails because the dialog and launch control are absent.
- [ ] Implement an initially 800x600, resizable, text-only dialog containing projection, scalar mode, Fit, the existing `KinematicAnalysisPlotWidget`, render mode, and Export PNG controls.
- [ ] Preserve `Qt::WindowStaysOnTopHint`, use `QPointer<KinematicPlotDialog>` in the owner, and forward `visualPointClicked` to `applyVisualizationPointQ`.
- [ ] Keep a single authoritative visualization state in `KinematicAnalysisWidget`; update both embedded and dialog plots from the same `AnalysisVisualData` without rerunning analysis.
- [ ] Clear both plots on WorkCell unload and update both after samples, units, projection, filters, or scalar mode change.
- [ ] Add the new sources to both CMake targets and run visualization, cache, workflow, and report suites.

## Task 4: Merge current-pose and IK behavior into Mode 1

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Add UI tests for the compact current-state health frame, six single-column target controls, Solve, candidate table, selected-candidate details, Apply Q, and collapsed advanced diagnostics.
- [ ] Add a stale-result test: solve or seed a result, edit one target input, then require stale status and disabled Apply.
- [ ] Verify red against the shell from Task 1.
- [ ] Reparent/rebuild the existing Current Pose and IK controls into one Mode 1 vertical page without duplicating analysis state.
- [ ] Keep the health frame semantically tied to the current Studio state. `Sync current TCP` copies inputs only; target edits invalidate old IK presentation.
- [ ] Preserve collision selection, candidate filter, complete Q tooltips/details, joint status, Jacobian, singular values, warnings, and apply-to-scene behavior.
- [ ] Keep tables bounded in height and independently scrollable. Run current-pose, IK, cache, and workflow suites.

## Task 5: Separate local tasks from frozen requirements in Mode 2

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Add tests for a text `mode2DataSourceCombo` with Local Tasks and Frozen Requirements.
- [ ] Assert Add/Remove/edit actions are enabled only for Local Tasks; frozen tasks, regions, and provenance remain read-only.
- [ ] Add tests for the compact task and region summary columns and for the selected multi-orientation probe visibility.
- [ ] Add failing tests for selected local task evaluation, selected frozen task evaluation, and selected frozen region evaluation by stable ID.
- [ ] Implement a narrow text toolbar with Load JSON, Validate All, Validate Selected, Add, Remove, plus an overflow text menu for existing CSV/report actions when needed.
- [ ] Keep `TaskPointTableModel` authoritative for local tasks. Use a summary view/proxy or hidden columns; do not replace or truncate the underlying 27-column data.
- [ ] Keep `_validateExecution` authoritative for frozen tasks/regions. Never copy it into the editable task model.
- [ ] Route selected operations by data source and object kind. Apply results by stable task/region ID, not visual row index.
- [ ] Reuse existing provenance and region-cell result structures in a default-collapsed diagnostics section.
- [ ] Run target, task-points, verified-region, adapter, report, and workflow suites.

## Task 6: Route all three Mode 3 choices without merging domain types

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Add tests requiring one text sampling-mode combo with Random, Joint Grid, and Pose Reachability, plus a conditional parameter stack.
- [ ] Assert Random and Grid produce the existing `WorkspaceSamplingConfig`; assert Pose Reachability selects the existing pose pipeline and does not cast to `WorkspaceSamplingMode`.
- [ ] Add tests for Run/Cancel/progress, summary fields, a roughly 160px embedded plot, launch-dialog control, and collapsed advanced parameters.
- [ ] Verify red against the current separate workspace/pose pages.
- [ ] Build Mode 3 from existing controls and handlers. Show Seed only for Random, Grid Steps only for Grid, and source/manual positions/Directions/Rolls for Pose Reachability.
- [ ] Dispatch Run and Cancel to the existing workspace or pose watcher according to the UI choice. Preserve immutable run snapshots and current cancellation semantics.
- [ ] Feed the active result into the embedded plot and shared plot-dialog state.
- [ ] Run workspace, pose-reachability, visualization, cancellation, cache, and workflow suites.

## Task 7: Preserve exports, persistence, compatibility, and narrow-width behavior

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisProjectDocument.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`

- [ ] Add tests for Header report actions: refresh, JSON, summary CSV, task-results CSV, and retained filtering state.
- [ ] Add project round-trip tests that keep schema v1 readable and synchronize `ikDuplicateQThreshold` with `thresholdIkDuplicateQ`.
- [ ] Add 300px and 320px geometry tests for fixed controls, mode buttons, clipped text policies, and absence of nested tabs.
- [ ] Verify each new test fails for the missing behavior before implementation.
- [ ] Move report actions out of the old report page into the Header text-button menu while retaining existing builder/filter/export functions.
- [ ] Remove obsolete page-switch code and dead tab members only after all callers have migrated.
- [ ] Preserve Qt 5/Qt 6 source compatibility and existing object names used by external tests unless the plan explicitly replaces them.
- [ ] Run formatting on touched C++ files, build the plugin and test executable, enumerate matching CTest suites with `ctest -N -R`, then run all kinematic-analysis suites with output on failure.

## Final Sol review and acceptance

- [ ] Compare the diff against every invariant and task above; reject unrelated refactors, icon additions, styling changes, and silent feature loss.
- [ ] Review UI ownership, QObject lifetime, cross-thread access, cancellation, stale generation handling, and project schema compatibility.
- [ ] Run fresh plugin/test builds and focused/full CTest commands; do not rely on implementer reports.
- [ ] Launch the available UI test harness or RobWorkStudio for 300px and 320px inspection when the environment permits; otherwise report the exact visual verification gap.
- [ ] Report changed files, tests, build evidence, known limitations, and any pre-existing failures without staging or committing unrelated user changes.
