# RobotModelBuilder New Project Flow - Pause Checkpoint

Date: 2026-08-03
Workspace: D:\10_Source_Repos\21_robot\RobWork\RobWork
Git root: D:\10_Source_Repos\21_robot\RobWork

## User-approved target

Upgrade File > New Project... to create a project and open RobotModelBuilder with the existing default six-axis model. The model remains an unsaved managed robot-model.main document. Save and Load is the only action that publishes/promotes mainWorkCell.

Implementation plan:
- docs/superpowers/plans/2026-08-02-robot-model-builder-from-scratch.md
Design:
- docs/superpowers/specs/2026-08-02-robot-model-builder-from-scratch-design.md
- design commit: 7200ef5

## Execution mode

Subagent-Driven, serial implementation with spec review before code-quality review.
The user explicitly required working in the current workspace; no worktree was created.
No files were staged or committed during this execution.

## Current status

Paused by user request. All implementation/review agents were stopped or completed.
No sdurws_sdurws-gtest, sdurws_robotmodelbuilder_widgettest, sdurws_robotmodelbuilder_metatest, or RobWorkStudio process was running at pause time.

Tasks 1-2 (transactional dialog-free helper) are implemented but NOT YET SPEC-APPROVED.
The most recent implementer was interrupted after:
- adding exact robot-model.main declaration validation;
- adding active Registry ID/per-resource dirty queries;
- enforcing exact Manifest/Registry resource sets;
- adding baseline project-tree inventory cleanup for undeclared files;
- correcting the project-root target guard;
- successfully compiling sdurws_sdurws-gtest (43 steps, exit 0);
- starting focused GREEN tests.

Important: no GREEN result was received after those latest exact-contract edits. Previous 91/91, widgettest 0, and metatest 0 results predate the latest edits and must not be treated as final.

Tasks 3-5 are not complete:
- Builder preflight/bootstrap invokables and default-model bootstrap still need their planned implementation/review;
- File > New Project... still uses the legacy path and is not wired to createProjectWithRobotModelBuilderPaths;
- end-to-end publication/persistence/analysis regression remains.

## Major completed support changes

Current working diff includes:
- NewRobotProjectCallbacks and createProjectWithRobotModelBuilderPaths.
- Reversible ProjectDocumentRegistry candidate transition.
- Optional provider in-memory snapshot/restore for same-provider rollback.
- RobotModelBuilder complete raw UI state snapshot/restore.
- Snapshot format v3 with explicit bounded field decoding:
  - 16 MiB per variable field;
  - 64 MiB aggregate decoded leaf budget;
  - strict UTF-8;
  - allowlisted QVariant tags;
  - aggregate combo/table allocation budgets;
  - two-phase parse before mutation.
- Candidate resource validation before snapshots:
  - generated-only, contained resources;
  - nonempty/unique IDs and kinds;
  - provider availability;
  - manifest/dependency validation and cycle rejection.
- setProjectOutputDirectory no longer creates directories.
- XML Writer validation permits nonexisting output directories; save/publish creates parents.
- Exact declared-output byte backup and atomic rollback restore.
- Newly-created candidate directory cleanup.
- Regression tests for ordering, same-provider dirty state, hostile snapshots, invalid dependencies, output cleanup, and post-restore interaction.

## Debug popup history

The user later reported a recurring Qt startup Debug Error at
`qguiapplication.cpp:1368`: "Qt platform plugin could not be initialized"
with `windows` listed as the available platform plugin. The user clicked
Abort. This is a test-launch/environment failure, not a RobotModelBuilder
business-logic assertion. On sight, stop the affected Qt test process, check
for inherited `QT_*`/`QML_*` variables, and relaunch exactly one executable
from PowerShell using:

```powershell
$env:QT_QPA_PLATFORM='windows'
& 'absolute-test-exe'
```

Never use `offscreen`, `cmd set QT_QPA_PLATFORM=windows && ...`, or combine
widgettest and metatest in one command. At the 2026-08-03 recurrence there
were no remaining RobWorkStudio/test processes and no inherited Qt/QML
environment variables after Abort; the active quality-fix subagent confirmed
it had not launched a Qt test.

The user reported a Microsoft Visual C++ Runtime Library Debug Error:
Qt6Core qlist.h line 460, ASSERT size_t(d.size) <= MaxSize.

Cause: Debug sdurws_robotmodelbuilder_widgettest decoded attacker-controlled QDataStream container/string lengths before bounds checks.

Resolution already implemented and verified before the latest exact-contract edits:
- replaced v2 bulk QDataStream extraction with bounded v3 field decoding;
- added hostile huge-length/truncation/unknown-type tests;
- standalone widgettest passed without the assertion.
Do not run widgettest combined with metatest. Run it alone with QT_QPA_PLATFORM=windows so any future failure is attributable and does not leave a blocking dialog.

## Resume sequence

1. Inspect git status/diff; preserve unrelated deleted UR sidecars, t1/t2/t3, and unrelated docs.
2. Resume or replace transaction_exact_contract_fix only to finish the current exact-contract task.
3. Run focused NewRobotProject exact-contract tests and fix any failures.
4. Fresh build and run:
   - Debug full sdurws_sdurws-gtest;
   - standalone sdurws_robotmodelbuilder_widgettest;
   - standalone sdurws_robotmodelbuilder_metatest;
   - QT_QPA_PLATFORM=windows, under VsDevCmd/vcvars64.
5. Run git diff --check and process scan.
6. Dispatch fresh Tasks 1-2 spec reviewer. If approved, dispatch fresh code-quality reviewer.
7. Only then mark Tasks 1-2 complete and proceed to Builder invokables/default six-axis bootstrap.
8. Later wire the production newProject() slot and run the end-to-end suite.

## Working files for this feature

- RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp
- RobWorkStudio/src/rws/ProjectDocumentProvider.hpp
- RobWorkStudio/src/rws/CallbackProjectDocumentProvider.hpp
- RobWorkStudio/src/rws/CallbackProjectDocumentProvider.cpp
- RobWorkStudio/src/rws/ProjectDocumentRegistry.hpp
- RobWorkStudio/src/rws/ProjectDocumentRegistry.cpp
- RobWorkStudio/src/rws/RobWorkStudio.hpp
- RobWorkStudio/src/rws/RobWorkStudio.cpp
- RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp
- RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp
- RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
- RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp
- RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp

ProjectPathResolver.cpp is also modified in the working tree and may include pre-existing/overlapping changes. Inspect attribution carefully before editing.

## Preserve unrelated state

Do not delete, restore, reset, clean, stage, or commit:
- deleted UR-6-85-5-A generated sidecars;
- untracked t1/t2/t3 directories;
- unrelated untracked plans/specs;
- any user changes outside this feature.
