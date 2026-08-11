# Explore Progressive Disclosure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Explore compact by hiding unselected sample details and moving plotting to the existing independent visualization dialog.

**Architecture:** Keep the existing workspace sample model and plot dialog. Change only Explore widget composition, selection state handling, and visualization launch routing; preserve data refresh and export paths.

**Tech Stack:** Qt widgets, C++, existing kinematic analysis test target.

---

### Task 1: Progressive sample details

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp` if widget members/signatures require it
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [x] Remove automatic first-row selection after applying workspace results.
- [x] Hide the selected-sample title and detail table when there is no current table selection.
- [x] On explicit row selection, show a compact fixed-height detail panel containing index/status/collision, Q, and TCP/metrics.
- [x] Clear selection/details whenever a new result set is applied or project state is cleared.
- [x] Add or extend a regression test proving fresh results have no selected detail and selecting a row exposes the detail data.

### Task 2: Independent visualization only

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp` if obsolete embedded-plot members are removed
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp` where existing visualization routing is covered

- [x] Remove `_visualizationTab` from the Explore scroll layout and eliminate the embedded plot presentation.
- [x] Retain visualization controls/state needed by `KinematicPlotDialog` and data refresh.
- [x] Make Workspace and Pose reachability `Open in Visualization` actions select source/color, refresh data, and open/focus the modeless plot dialog.
- [x] Make refresh logic tolerate the absent embedded plot while updating the dialog.
- [x] Keep CSV export and result summary available in the Explore result area; disable actions when no data exists.

### Task 3: Summary and empty-state polish

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [x] Rename the results heading to `Sample results` and the input label to `Sample count` where applicable.
- [x] Mark collision summary as not evaluated when Explore configuration disables collision checks.
- [x] Distinguish pre-run empty, canceled/partial, and completed result states.
- [x] Verify narrow Dock layout remains free of horizontal overflow.

### Verification

- [x] Build `sdurws_kinematicanalysis` and its tests.
- [x] Run all `sdurws_kinematicanalysis_test_*` tests.
- [x] Run `git diff --check` and inspect the final diff for unrelated changes.
