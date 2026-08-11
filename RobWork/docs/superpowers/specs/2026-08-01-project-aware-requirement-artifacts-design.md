# Project-Aware Requirement Artifacts

## Goal

Make engineering requirements a first-class project document while retaining a
separate, portable export-copy workflow. Downstream analysis plugins must resolve
the project requirement through its manifest resource ID rather than scanning the
project directory or requiring a file picker.

## Resource Contract

The primary requirement document is created on the first effective requirement
change in an open project.

| Field | Value |
| --- | --- |
| Resource ID | `engineering-requirements.main` |
| Kind | `rws.engineering-requirements` |
| Path | `requirements/main.requirements.json` |
| Ownership | `generated` |
| Required | `false` |
| Dependencies | current main WorkCell resource and `robot-model.main` when present |

The engineering-requirements Provider owns the primary document. The normal
project save transaction writes it and the project manifest atomically.

## User Workflows

### Project save

Editing, importing, or freezing requirements creates or adopts the primary
resource. `Save Project` persists `requirements/main.requirements.json`; no
separate file dialog is involved.

### Export copy

`Export requirement copy` opens in
`requirements/exports/requirements-copy.requirements.json` for an open project.
The user may choose any destination. The result is a shareable snapshot only:
it never updates the project manifest, provider resource ID, project baseline,
or dirty state. Without a project, the existing standalone default remains.

### Import copy

`Import requirement copy` opens in `<project>/requirements/exports/` when it
exists, otherwise `<project>/requirements/`. Importing updates the in-memory
primary document and marks the project dirty; the imported external file is not
added to the manifest.

### Downstream import

When the user invokes frozen-requirement import in KinematicAnalysis or
StructureOptimizer, the plugin first resolves
`engineering-requirements.main` through `RobWorkStudio::resolveProjectResource`.
If the resource exists it is used directly. If it does not exist, the existing
file picker is retained and starts in `<project>/requirements/`; external files
remain supported.

## Scope

The change covers project-system plugins and their owned data directories:

| Plugin | Primary resource | Primary directory | Copy/output directory |
| --- | --- | --- | --- |
| RoboModelBuilder | `robot-model.main` | `generated/robot-models/` | `generated/robot-models/exports/` |
| EngineeringRequirements | `engineering-requirements.main` | `requirements/` | `requirements/exports/` |
| KinematicAnalysis | `kinematic-analysis.main` | `analysis/` | `analysis/exports/` |
| StructureOptimizer | `structure-optimization.main` | `optimizations/` | `optimizations/exports/` |

This implementation changes the requirements plugin and the two consumers. It
does not refactor legacy visualization, playback, Lua, or WorkCell-editor file
dialogs that are not project document providers.

## Error Handling

If no project resource can be resolved, consumers retain manual import. A
failed project resource resolution is surfaced in the status text only after
the manual fallback is cancelled or fails. Export-copy directory creation is
performed by `QSaveFile`; its error is shown without mutating project state.

## Tests

Tests must cover primary resource creation/adoption, dependencies, copy-export
non-mutation, project-aware fallback paths, and consumer priority of the main
resource over a file picker. Existing external-file import coverage remains.
