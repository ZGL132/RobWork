# Structure Optimization Model Status Guidance Design

## Goal

Expose the optimization project's model-snapshot state in a persistent banner, explain whether the run uses a frozen snapshot, and route recovery through the existing project-creation workflows.

## State Semantics

- `ModelSpecIncomplete`: the embedded model specification has no robot name or joints; optimization remains blocked by the existing runnable-input validation.
- `Current`: the tracked model source matches the embedded snapshot; the banner is hidden.
- `Stale`: the tracked source changed; the current project continues to use its embedded frozen snapshot.
- `Untracked`: no complete provenance exists; the current project can use its embedded snapshot without source comparison.
- `SourceMissing` and `SourceInvalid`: the tracked source cannot be used; the embedded frozen snapshot remains authoritative.

The implementation must never overwrite a loaded project from the active 3D scene or automatically synchronize it from a source file.

## UI

`StructureOptimizerWidget` gains a framed model-status banner above the tabs. It contains a concise state message, a source-path line when a source is known, and buttons that reuse the existing "New Project from Model Snapshot" and "New Project from Frozen Requirements" commands. The bottom status label remains short and continues to carry runnable, progress, and error feedback.

## Data Flow

`RobotModelStalenessChecker::checkManaged` first detects an incomplete embedded model, then performs its existing provenance/source comparison. `StructureOptimizerWidget::updateModelSourceStatus` updates both `_modelSourceStatus` and the banner whenever problem state is refreshed. `updateRunState` continues to call `hasRunnableInputs()` unchanged.

## Tests

Widget coverage verifies banner controls exist, `Untracked`, `Stale`, `SourceMissing`, and `ModelSpecIncomplete` messages are surfaced, source paths are displayed, and Stale retains the pre-existing embedded snapshot. Existing model-only checker tests continue to cover `Current` and `SourceInvalid`.
