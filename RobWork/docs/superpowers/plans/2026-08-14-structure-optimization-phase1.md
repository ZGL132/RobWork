# Structure Optimization Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the StructureOptimizer usable for first-pass engineering work through guided setup, reusable optimization templates, preflight validation, baseline evaluation, explainable results, and candidate comparison while keeping the current kinematic evaluator and future evaluator extension points intact.

**Architecture:** Extend the existing `StructureOptimizationProblem`/`StructureOptimizationResult` data flow instead of adding a parallel workflow model. Keep computation in the core library, expose small UI-facing helpers for templates, preflight and comparison, and let `StructureOptimizerWidget` compose the guided workflow over the existing table models/controller.

**Tech Stack:** C++17, Qt Widgets, RobWorkStudio project documents, Catch-style in-tree tests, CMake/Ninja/MSVC.

---

### Task 1: Add baseline and comparison data contracts

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateTableModel.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateTableModel.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Add a baseline candidate/result reference and explicit per-metric deltas without changing the existing candidate scoring semantics.
- [ ] Extend candidate table roles/columns with baseline delta and violated-constraint summary while preserving stable candidate indices.
- [ ] Write failing model tests for baseline delta formatting, infeasible reason text, and candidate ordering data.
- [ ] Run the focused model test suite and confirm the new assertions fail before implementation.
- [ ] Implement the smallest data/model changes needed to pass those tests.
- [ ] Run the focused model test suite again and refactor only after it is green.

### Task 2: Implement reusable optimization templates

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTemplate.hpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTemplate.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Define a small template API for balanced, reachability-first, compactness-first and workspace-first presets.
- [ ] Make templates operate on an existing problem and only modify first-phase fields: enabled variables, tasks/constraints, objective profile and run preset.
- [ ] Add failing tests proving templates are deterministic, preserve model provenance, and do not enable unsupported evaluator types.
- [ ] Verify the tests fail for the missing API, then implement the template factory and serialization-safe identifiers.
- [ ] Run the template tests and the existing JSON round-trip tests.

### Task 3: Add preflight diagnostics

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationUiLogic.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationUiLogic.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Add structured preflight findings with severity, code, field target and remediation text.
- [ ] Cover missing model/task/variable inputs, invalid bounds/steps, contradictory counts, objective weight totals, oversized search spaces and stale/incomplete model status.
- [ ] Write failing core tests for each blocking diagnostic and one warning-only diagnostic.
- [ ] Implement preflight evaluation independently from button state so it can drive both the wizard and the status area.
- [ ] Add a visible preflight summary and make Start Optimization use the same diagnostics as the summary.
- [ ] Run core and widget-focused tests under the repository Windows Qt test rule.

### Task 4: Add guided setup and template controls

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Add a compact setup section that selects a template, applies it, runs preflight and opens the first actionable tab.
- [ ] Keep the existing five tabs and table models; the guided section orchestrates them rather than replacing them.
- [ ] Add widget tests for template control presence, application, disabled state while running and correct tab navigation.
- [ ] Verify widget tests fail before implementation, then implement signal wiring and state refresh.
- [ ] Preserve existing English UI copy tests and model-status banner behavior.

### Task 5: Add baseline evaluation and explainable result summary

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationReportWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Add an explicit baseline-evaluation action using the existing evaluator path and show baseline metrics before optimization starts.
- [ ] On completion, show feasible count, best candidate, baseline delta, top violated constraints and sensitivity/robustness status in a summary panel.
- [ ] Add failing tests for baseline preservation, completion summary text and report inclusion of baseline comparison fields.
- [ ] Implement the summary without changing optimizer ranking or evaluator results.
- [ ] Run focused widget/report tests and the accepted project test.

### Task 6: Add candidate comparison workflow

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateComparison.hpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateComparison.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Define comparison output for baseline plus up to three candidates, including metric deltas and merged constraint explanations.
- [ ] Add selection-aware comparison controls and a non-destructive compare view beside the existing preview/clear actions.
- [ ] Add failing tests for stable ordering, duplicate selection rejection and missing-candidate handling.
- [ ] Implement comparison formatting and connect it to the candidate table selection model.
- [ ] Verify comparison UI tests and ensure preview still restores the original WorkCell.

### Task 7: Verification and documentation

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/README.md`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/ARCHITECTURE.md`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Document the first-phase workflow, template identifiers, preflight severity semantics and baseline/result interpretation.
- [ ] Add a deterministic acceptance scenario covering template application, preflight, baseline, optimization completion, comparison and export.
- [ ] Run the focused MSVC build and the exact structure optimizer CTest target under the Windows Qt GUI rule.
- [ ] Inspect the final diff and report any remaining gaps instead of claiming completion without fresh test evidence.

