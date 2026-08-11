# Kinematic Diagnose Solution Inspector Design

## Goal

Make the Kinematic Analysis plugin's three workflow tabs use the same native Qt tab treatment as Jog, and rebuild Diagnose around one unambiguous subject: the best or user-selected IK candidate. Remove obsolete Current TCP presentation code and other old Diagnose UI remnants without changing Validate, Explore, reports, exports, or other plugins.

## Scope

- Modify only `RobWorkStudio/src/rwslibs/kinematicanalysis` and its focused design/test documentation.
- Do not modify Jog, any other plugin, or application-wide styling.
- Preserve the existing Diagnose, Validate, Explore order and their domain behavior.
- Preserve current-pose analysis data used by reports and exports, but stop presenting it as Diagnose candidate diagnostics.
- Preserve Qt 5/Qt 6 compatibility and narrow Dock support at 300px and 320px.

## Native Workflow Tabs

Replace the custom `QTabBar` plus `QStackedWidget` shell with one native `QTabWidget`, matching Jog's control type and inherited Qt style. The plugin must not set a local stylesheet on the tab widget or its tab bar. Each existing scrollable workflow page remains the content of exactly one tab.

## Diagnose Layout

Diagnose is one master-detail workflow in this order:

1. IK Target
2. Solver settings and commands
3. IK Solutions table
4. Solution Inspector
5. Advanced diagnostics, collapsed by default

The old Current Pose and IK child-page split is removed.

### IK Target

- Rename `Pose / IK target` to `IK Target`.
- Remove the Current TCP value column and the `Current TCP` / `IK Target` header row.
- Keep only `x`, `y`, `z`, `Rx`, `Ry`, and `Rz` labels with target inputs.
- Rename the sync action to `Refresh and Sync TCP`. It reads the latest Studio state, refreshes `_lastCurrentPose` and the report snapshot, copies the actual TCP pose into the target inputs under signal blockers, and then invalidates previous IK results. It does not solve automatically.
- Commands use native Qt controls and logical order: `Refresh and Sync TCP`, `Thresholds`, `Collision`, `Duplicate Q`, `Solve`.
- Use a stable two-row command grid at all widths. Row one contains `Refresh and Sync TCP`, `Thresholds`, and `Collision`; row two contains `Duplicate Q` with its spin box and a right-aligned `Solve`. Focus order follows that same sequence.

### IK Solutions

Use the candidate table as the master view. Its compact comparison columns are:

`# | Status | Position error | Orientation error | Min margin`

The table uses native Qt table styling, full-row single selection, alternating rows, and no plugin-local stylesheet. Q, collision evidence, distance, and failure reasons move to the inspector.

The candidate filter retains the existing `Exclude failed`, `Usable only`, and `All candidates` choices, but defaults to `All candidates` because failed candidates are diagnostic evidence.

### Default Diagnostic Candidate

After Solve, choose a stable candidate index with this priority:

1. First Pass candidate not marked in collision, in existing display order.
2. First Warning candidate not marked in collision, in existing display order.
3. First remaining candidate in existing display order.

This rule selects the diagnostic default without changing the shared candidate sorting behavior used by other workflows. Mark it as `Best solution #N`. Clicking another visible row changes the inspector to `Selected solution #N`. Filtering preserves the stable solution index when visible; otherwise it reruns the same Pass, Warning, remaining priority over the visible stable indices. With no visible row, the inspector shows an explicit empty state.

Internal candidate indices remain zero-based. User-visible table numbers and `#N` labels are one-based.

## Solution Inspector

The inspector is the single presentation source for candidate diagnostics.

### Health Row

Show one horizontal row with four candidate-level metrics:

`Status | Condition | Manipulability | Min joint margin`

Remove Collision capability. The status is the selected candidate status, never the aggregate IK result status and never the Studio current-pose status.

### Evidence

Show compact candidate evidence below the health row:

- Position error
- Orientation error
- Collision
- Distance from solve start
- Full Q in a selectable field with the full value in its tooltip
- Failure reasons only when non-empty

`Distance from solve start` replaces `Distance to current Q`, because the reference Q is captured at Solve time and does not track later Studio state changes.

Collision text must distinguish:

- `Not evaluated`
- `Unavailable`
- `Clear`
- `Collision`

If Collision was requested but the detector was unavailable, retain the candidate evidence but disable Apply. If Collision was not requested, a non-failing candidate may still be applied, while its collision evidence remains `Not evaluated`.

`Apply selected Q` writes to Studio only for a non-stale candidate that passes the existing usability checks and the collision-request safety rule.

## Advanced Diagnostics

Place Advanced diagnostics at the bottom of Diagnose. It starts collapsed and never auto-expands when results or selections change.

It contains only:

- Joint status table for the active candidate Q.
- One compact Jacobian summary: dimensions, minimum singular value, maximum singular value, condition, and Pass/Warning/Fail status.

The Jacobian status is derived from the analyzer's singular metrics only, not the candidate's overall status. The complete singular-value list is available in a tooltip. Remove the Warnings block, full Jacobian matrix table, and standalone Singular values table.

## Candidate Diagnostic Data

Do not recompute diagnostics from the live Studio state when selection changes. `TargetEvaluator` already produces complete `ConfigurationEvaluation` data for every candidate. Extend `KinematicIkSolution` and `legacyIkResultFromTarget` to preserve:

- Per-joint limit margins
- Jacobian dimensions and row-major values
- Singular values
- `collisionChecked`, where true means the detector query completed successfully

The inspector reads this immutable Solve snapshot by stable candidate index. This avoids state mutation, repeated analysis, and selection-time flicker.

Add result-owned collision request metadata and a candidate collision evidence state with `NotEvaluated`, `Unavailable`, `Clear`, and `Collision`. A detector query exception leaves `collisionChecked` false and maps to `Unavailable`; it must never render as `Clear`. Apply is disabled whenever collision was requested and the run or candidate lacks successfully evaluated collision evidence.

## Stale And Empty States

Invalidate the IK result, selection, inspector, Apply action, and advanced diagnostics when any solve-affecting input changes:

- IK target position or orientation
- Device
- TCP frame
- Thresholds
- Collision option
- Duplicate Q threshold
- External Studio state
- WorkCell or project session

Length or angle display-unit changes only reformat existing values and do not invalidate results. Candidate filter changes only change visibility and selection fallback.

The UI state transitions are deterministic:

- Construction, WorkCell load, and explicit clear enter Ready/no result.
- Solve start clears the previous result snapshot, selection, collision metadata, inspector, and advanced diagnostics, disables Apply, and enters Solving.
- Every validation or setup failure enters Error/no result with the specific failure message and no retained candidate snapshot.
- A successful non-empty result enters Solved, selects the best candidate, and fills the inspector.
- A successful zero-candidate result enters No candidates and shows the analyzer failure reason.
- An all-Fail result still enters Solved and selects the best diagnostic Fail candidate.
- A solve-affecting edit enters Stale and clears candidate evidence.
- A filter with no visible rows enters Filter empty without discarding the immutable solved snapshot.

Applying a candidate sets an internal state-change guard, writes Q to Studio, retains the selected candidate and immutable evidence, refreshes `_lastCurrentPose` and report data, and then releases the guard. A later external Studio state change enters Stale.

## Cleanup Boundary

Remove code that exists only for the retired Diagnose UI:

- Current TCP labels and header construction
- Collision capability label
- Warnings label and auto-expand behavior
- Full Jacobian and Singular values table widgets and population code
- Separate Current Pose and IK page containers when the unified Diagnose page owns their remaining controls
- Aggregate IK summary labels that are replaced by the candidate inspector and one compact `Displayed X of Y` count
- Old object names, signal connections, helper functions, and comments with no remaining caller
- The unreachable legacy `analyzeIk` implementation after the current `TargetEvaluator` return

Do not remove:

- `_lastCurrentPose` or current-pose analysis required by reports and exports
- Validate or Explore state backing widgets that remain functional even when hidden
- Report builders, project persistence, task analysis, visualization, or collision infrastructure used outside Diagnose

All removal decisions require a reference search and compilation. No unrelated refactoring is part of this work.

## Tests And Acceptance

Add or update focused tests for:

- Native `QTabWidget`, three tabs, no local tab stylesheet, and tab/page switching.
- Absence of Current TCP presentation and retired Diagnose widgets.
- Command order, `Refresh and Sync TCP` behavior, and 300px/320px geometry.
- Candidate table columns and default All filter.
- Default best-candidate selection and stable selection through filtering.
- Inspector health/evidence following the selected candidate, not current Studio state or aggregate result.
- Candidate diagnostic data preservation through `legacyIkResultFromTarget`.
- Collision Not evaluated/Unavailable/Clear/Collision states, detector-query exceptions, and Apply safety.
- Every stale trigger, guarded Apply state change, and unit-change non-trigger.
- Advanced diagnostics location, default collapsed state, joint rows, compact Jacobian summary, and removed legacy tables/warnings.
- Existing report, project document, Validate, Explore, IK solve/apply, and narrow-Dock regressions.

Build and run the complete `sdurws_kinematicanalysis_test` executable, then run `git diff --check`. When possible, launch RobWorkStudio and compare the Kinematic Analysis tabs with Jog at 300px and 320px Dock widths.
