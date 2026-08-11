# Plugin UI English Copy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make all Engineering Requirements and Structure Optimizer user-facing text concise, unambiguous English.

**Architecture:** Presentation-only Qt changes. Widget hierarchy, object names, behavior, data contracts, and file formats remain unchanged. Tests locate controls by existing object names and assert final text.

**Tech Stack:** C++17, Qt 6 Widgets, project-local `REQUIRE` harness, CMake, CTest.

---

### Task 1: Write Failing UI-Copy Tests

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Add `testEngineeringRequirementsWidgetUsesEnglishCopy()` and call it from the UI and normal test paths. Locate `engineeringRequirementsTabs`, then assert `Key Stations`, `Workspace Regions`, `Validate & Freeze`; locate `addRequirementPoseTaskButton`, `captureRequirementTcpButton`, `bindRequirementModelButton`, and `freezeRequirementSetButton`, asserting `Add Station`, `Capture TCP Pose`, `Bind Model`, and `Freeze Requirements`.
- [ ] Build and run `.\scripts\build-msvc-debug.cmd sdurws_engineeringrequirements_test` followed by `build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\sdurws_engineeringrequirements_test.exe ui`. Expected: the new assertions fail against the current Chinese text.
- [ ] Add `testStructureOptimizerWidgetUsesEnglishCopy()` and call it from both paths. Assert `structureOptimizerTabs` has `Design Variables`, `Tasks & Constraints`, `Optimization Settings`, `Candidates`, `Export Report`; assert `addOptimizationTaskButton` is `Add Task`, `previewStructureCandidateButton` is `Preview Candidate`, and `exportStructureOptimizationResultButton` is `Export Report & Models`.
- [ ] Build and run `.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test` followed by `build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\sdurws_structureoptimizer_test.exe ui`. Expected: the new assertions fail against the current Chinese text.

### Task 2: Translate Engineering Requirements UI

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] Replace template options with `Bin Picking`, `Machine Tending`, `Palletizing`, `Inspection`, `Tool Change`, `Handover`; use concise labels `Template Type`, `Instance ID`, `Station ID Prefix`, `Station Name Prefix`, `Reference Frame`, `Requirement Level`, `Operation Offset X/Y/Z`, `Rows`, `Columns`, `Layers`, spacing, approach, retract, and clearance labels.
- [ ] Rename array dialog and fields to `Generate Station Array`, `Array Type`, `Primary Count`, `Secondary Count`, `Primary Step X/Y/Z`, `Secondary Step X/Y/Z`, `Radius`, `Start Angle`, `End Angle`, `Polyline Points (m)`, with invalid input text `Invalid Polyline` and `Enter at least two points as x,y,z; x,y,z.`
- [ ] Set Key Stations actions to `Add Station`, `Duplicate Station`, `Remove Station`, `Capture TCP Pose`, `Pick Geometry Frame`, `Create from Template`, `Update Template`, `Detach Template`, `Generate Array`, `Mirror Station`, `Import Stations`, `Undo`, `Redo`. Keep every existing `objectName` and signal connection unchanged.
- [ ] Use process terms `Generic`, `Pick`, `Place`, `Machine Load`, `Machine Unload`, `Inspect`, `Weld Start`, `Weld End`, `Tool Change`, `Safe Standby`, `Handover`; orientation terms `Fixed Orientation`, `Align Frame`, `Align Geometry Normal`, `Point at Target`; field labels `Name`, `Process Type`, `Requirement Level`, `Reference Frame`, `Orientation Rule`, `Orientation Target`, `Target Point`, `Allow Tool Roll`, `Approach & Retract`, `Approach Along Tool Z`, `Retract Along Reference Z`, `Minimum Joint Margin`, `Advanced Pose (Station Frame)`, and `Pose Source`.
- [ ] Set tabs to `Key Stations`, `Workspace Regions`, `Validate & Freeze`; workspace actions to `Add Region`, `Duplicate Region`, `Remove Region`; headers to `ID`, `Name`, `Level`, `Reference Frame`, `Center X/Y/Z`, `Size X/Y/Z`, `Minimum Coverage`, `Samples per Axis`, and `TCP Frame`.
- [ ] Set validation actions to `Bind Model`, `Import Requirements`, `Export Requirements`, `Validate Requirements`, `Freeze Requirements`, and `Edit Requirements`. Format dynamic model/freeze text as `Model: %1\nFingerprint: %2`, `Status: Frozen\nFrozen at (UTC): %1\nRequirement fingerprint: %2`, and `Status: Editable\nFreeze requirements before downstream analysis or optimization.` Preserve `Quick`, `Verified`, `Included`, `Excluded`, codes, IDs, paths, and persisted values.
- [ ] Re-run the Engineering Requirements UI command from Task 1. Expected: copy and existing behavior tests pass.

### Task 3: Translate Structure Optimizer UI

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Set tabs to `Design Variables`, `Tasks & Constraints`, `Optimization Settings`, `Candidates`, `Export Report`; actions to `Add Task`, `Duplicate Task`, `Remove Task`, `Constraints`, `Add Constraint`, `Duplicate Constraint`, `Remove Constraint`, `Preview Candidate`, `Clear Preview`, `New Project from Model Snapshot`, `New Project from Frozen Requirements`, `Import Project`, `Export Project`, and `Export Report & Models`.
- [ ] Set settings to `Strategy`, `Candidates`, `Elite Candidates`, `Local Refinement Elites`, `Final Verification Candidates`, `Local Search Sweeps`, `Grid Steps`, `Random Seed`, `Objective Weights`, `Reachability`, `Manipulability`, `Joint Margin`, `Collision`, `Compactness`, and `Preference`; keep strategy values `Hybrid`, `Random`, and `Grid`.
- [ ] Use status/progress text `Optimization project ready.`, `Optimization running in the background.`, `Canceling optimization.`, `Resume`, `Pause`, `No candidate selected for preview.`, `Only feasible candidates can be previewed.`, `Previewing candidate #%1.`, `Optimization project loaded.`, `Optimization project saved.`, `Report exported to %1.`, and progress format `%1 %2/%3, best score %4`. Preserve all provenance conditions and data.
- [ ] Re-run the Structure Optimizer UI command from Task 1. Expected: copy and existing behavior tests pass.

### Task 4: Full Verification

**Files:**
- Verify: both widget implementations and their tests.

- [ ] Run `rg -n "[一-龥]" RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`. Expected: no output; translate remaining comments in these files if needed.
- [ ] Run `ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_(engineeringrequirements|structureoptimizer)_test" --output-on-failure` and `git diff --check`. Expected: both targets pass and no whitespace errors are reported.
- [ ] Stage only the four implementation/test files and this plan, then commit with `ui: clarify plugin English copy`.
