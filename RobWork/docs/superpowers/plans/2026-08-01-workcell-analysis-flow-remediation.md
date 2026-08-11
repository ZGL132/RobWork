# WorkCell Analysis Flow Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Repair the managed WorkCell identity and enforce one frozen-scenario gate across kinematic analysis and structure optimization.

**Architecture:** ProjectManager owns stable resource/path mutations and passive assets; RobWorkStudio coordinates Provider reloads. RequirementFreezer produces hard validity plus non-blocking warnings, and both downstream plugins consume that result with the active WorkCell/State.

**Tech Stack:** C++17, Qt 6, RobWork XML loaders, ProjectDocumentRegistry, Catch2/GoogleTest, CMake/CTest.

---

### Task 1: Project scene identity and XML routing

**Files:**
- Modify: `RobWorkStudio/src/rws/ProjectManager.{hpp,cpp}`
- Modify: `RobWorkStudio/src/rws/ProjectDocumentRegistry.cpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.{hpp,cpp}`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
- Test: `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`
- Test: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`

- [ ] Add failing tests for stable main-resource path replacement, passive asset validation, and RobWork-device XML classification.
- [ ] Run focused tests and confirm failures are caused by missing APIs.
- [ ] Add the minimal ProjectManager mutation and project-level generated-scene promotion API.
- [ ] Route project-mode `Save and Load` through promotion; retain standalone `setWorkcell` behavior.
- [ ] Run focused tests until green.

### Task 2: Frozen provenance warning

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.{hpp,cpp}`
- Test: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] Add a failing test that rewrites only the source file and expects scenario validity plus a warning.
- [ ] Extend validation result with provenance warnings and remove the source-hash hard return.
- [ ] Run the engineering-requirements tests until green.

### Task 3: Shared StructureOptimizer scenario gate

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/FrozenRequirementProjectImportService.{hpp,cpp}`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.{hpp,cpp}`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] Add failing import-service tests for Q-only success and fixture-motion rejection.
- [ ] Pass active WorkCell/State into the service and call `RequirementFreezer::validateScenario` before adaptation.
- [ ] Surface Q/provenance warnings without blocking import.
- [ ] Create/adopt `structure-optimization.main` when an active project first obtains optimizer content.
- [ ] Run StructureOptimizer tests until green.

### Task 4: Manifest-authoritative model binding

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.{hpp,cpp}`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.{hpp,cpp}`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp`
- Test: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] Add a failing test/API seam for resolving `robot-model.main` when extra `.rmb.json` files exist.
- [ ] Inject the resolved resource path from RobWorkStudio and remove directory-count authority.
- [ ] Run EngineeringRequirements tests until green.

### Task 5: Verification

- [ ] Build the rws, engineeringrequirements, robotmodelbuilder, kinematicanalysis, and structureoptimizer test targets.
- [ ] Run focused CTest suites with Qt offscreen configuration where required.
- [ ] Run `git diff --check` and inspect only intended source/document changes.
