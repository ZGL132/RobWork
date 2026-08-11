# Kinematic UI Structure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Diagnose, Validate, and Explore follow one compact, logically grouped operational layout.

**Architecture:** Preserve analysis behavior and current widget ownership. Rearrange existing controls into context, setup, execution, result, and selected-detail groups; use adaptive summary grids and compact table columns for narrow Dock widths.

**Tech Stack:** Qt Widgets, C++, `sdurws_kinematicanalysis_test`.

---

### Task 1: Global and Diagnose hierarchy

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Retain only shared Device, TCP, and unit context in the header; place per-mode execution and report actions with their owning page.
- [ ] Convert Health summary from a five-column strip to a responsive two-column metrics grid at narrow widths.
- [ ] Preserve the combined Pose / IK target section and keep solution actions adjacent to solution selection.

### Task 2: Validate and Explore command consolidation

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Present a single Validate command row whose actions change with Local Tasks versus Frozen Requirements.
- [ ] Place requirement source, commands, result tables, and selected diagnostics in that order.
- [ ] Split Explore into Setup and Run/Progress rows; keep Workspace/Pose-specific panels free of duplicate Run/Cancel controls.
- [ ] Keep export and visualization actions beside the result section and disabled until data exists.

### Task 3: Shared narrow-Dock presentation rules

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] Apply compact visible columns to Validate task and region tables while retaining complete selected details.
- [ ] Use the same section-header, summary, result, and selected-detail sequence in all modes.
- [ ] Add UI state regression assertions for grouped command visibility and compact-column rules.

### Verification

- [ ] Build `sdurws_kinematicanalysis_test` in Release.
- [ ] Run the complete kinematic-analysis test executable with Qt and RobWork runtime paths configured.
- [ ] Run `git diff --check` and inspect only the changed kinematic-analysis files.
