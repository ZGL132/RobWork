# Requirement Document Compatibility Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatically migrate historical requirement documents with a legacy duplicate frozen artifact and persist one canonical document representation.

**Architecture:** Add a document-envelope migration beside the existing frozen-artifact migration. `EngineeringRequirementsWidget` invokes it before strict requirement parsing, tracks a migration-pending dirty flag, and defensively strips the reserved field during serialization. Existing artifact validation remains authoritative.

**Tech Stack:** C++17, Qt 6 JSON/Widgets, RobWorkStudio project document providers, existing standalone test executables.

---

### Task 1: Reproduce the historical document conflict

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] Add a test document containing both top-level `frozenArtifact` and `extensions.frozenArtifact` with different fingerprints.
- [ ] Load it through `EngineeringRequirementsWidget::loadProjectDocument` and assert that the current code rejects it with the extension-conflict message.
- [ ] Run the focused GUI test under the Visual Studio x64 environment with `QT_QPA_PLATFORM=windows` and confirm the expected failure.

### Task 2: Add document-envelope migration

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementMigration.hpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementMigration.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] Define a migration result carrying the sanitized document, selected frozen artifact, migration flag, and diagnostics.
- [ ] Implement top-level precedence, legacy promotion, legacy removal, and strict type validation.
- [ ] Add focused tests for top-level-only, legacy-only, identical duplicate, different duplicate, and unrelated extension conflicts.
- [ ] Run the model-only migration test suite and confirm it passes.

### Task 3: Integrate loading, serialization, and dirty state

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] Invoke document migration before `RequirementSetJson::fromObject`.
- [ ] Parse the migrated canonical artifact with the existing validation path.
- [ ] Strip `frozenArtifact` from extensions during serialization.
- [ ] Add a migration-pending flag to `isProjectDocumentDirty`, `markProjectDocumentClean`, and project-context reset.
- [ ] Verify historical load succeeds, reports migration, saves one top-level artifact, and reloads idempotently.

### Task 4: Regression verification

**Files:**
- Test: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] Build `sdurws_engineeringrequirements_test` in the Visual Studio x64 developer environment.
- [ ] Run the focused compatibility GUI suite with the absolute executable path and `QT_QPA_PLATFORM=windows`.
- [ ] Run existing strict JSON, migration, and project-document suites.
- [ ] Run `git diff --check` and inspect the final diff.
- [ ] Commit with a Chinese compatibility-fix message while leaving the untracked historical project directory untouched.
