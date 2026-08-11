# Project Reliability Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make project recovery, autosave, packaging, optional-resource loading, and integrity repair safe and usable from RobWorkStudio.

**Architecture:** Extend the existing file transaction so raw file copies, manifest bytes, and Provider output share one commit/rollback boundary. Store recovery snapshots in two independently committed slots, let the document registry serialize loaded Provider state into a snapshot staging directory, and keep UI orchestration in `RobWorkStudio`.

**Tech Stack:** C++17, Qt 6 (`QTimer`, `QSaveFile`, `QFileDialog`, `QMessageBox`), GoogleTest, libzip.

---

### Task 1: Transactional recovery

**Files:** `RobWorkStudio/src/rws/ProjectSaveTransaction.*`, `RobWorkStudio/src/rws/ProjectManager.*`, `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`

- [x] Add a recovery failure test and confirm all original resources and manifest survive.
- [x] Add `stageCopy` and `stageBytes` to `ProjectSaveTransaction`; only call `markClean` for Provider-owned staged entries.
- [x] Stage all snapshot resources and the manifest before one `commit` call.
- [x] Run the focused recovery tests.

### Task 2: Provider-aware double-slot autosave

**Files:** `RobWorkStudio/src/rws/ProjectDocumentRegistry.*`, `RobWorkStudio/src/rws/ProjectManager.*`, `RobWorkStudio/src/rws/RobWorkStudio.*`, tests

- [x] Add a Registry snapshot test proving a dirty Provider contributes its in-memory bytes without being marked clean.
- [x] Persist snapshots into an inactive slot, atomically switch the active-slot marker, and retain the previous slot until the new marker is committed.
- [x] Add restore/discard discovery on project open and periodic dirty-project snapshots through `QTimer`.
- [x] Clear recovery data only after successful user save or intentional discard.

### Task 3: Complete rwpack workflow

**Files:** `RobWorkStudio/src/rws/ProjectPackage.*`, `RobWorkStudio/src/rws/RobWorkStudio.*`, tests

- [x] Add a rejection test for an archive whose manifest-owned file is missing.
- [x] Serialize the supplied in-memory manifest into `project.rwproj`, validate owned files before extraction commit, and bound entry count and total uncompressed bytes.
- [x] Add File-menu export/import actions; export first commits dirty project documents.
- [x] Run package round-trip and rejection tests.

### Task 4: Optional diagnostics and integrity repair

**Files:** `RobWorkStudio/src/rws/ProjectDocumentRegistry.*`, `RobWorkStudio/src/rws/ProjectManager.*`, `RobWorkStudio/src/rws/RobWorkStudio.*`, tests

- [x] Add warnings for skipped optional resources and tests for missing-file/provider failure degradation.
- [x] Add project-manager operations to safely discard unreferenced files and relocate a missing resource with an atomic manifest update.
- [x] Add the integrity inspection action, report UI, cleanup confirmation, and resource relocation picker.
- [x] Run focused tests and the complete `sdurws_sdurws-gtest` target.
