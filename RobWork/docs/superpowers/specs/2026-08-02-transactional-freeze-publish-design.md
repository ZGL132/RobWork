# Transactional Freeze Publication

## Goal

Make a successful engineering-requirement freeze immediately publishable without a second manual
Save Project action, while preventing downstream plugins from reading stale project files.

## Approved Workflow

- Ordinary requirement edits only mark the project dirty.
- A successful freeze first completes the in-memory artifact and emits the normal requirement-change
  notification, then requests one project-level transactional save.
- The project save commits all dirty providers and the manifest through the existing
  `ProjectSaveTransaction`; the requirements plugin never writes the authoritative JSON directly.
- A save failure leaves the requirement frozen in memory and the project dirty. The UI reports that
  publication failed and downstream project imports must not silently read the previous file.
- Before KinematicAnalysis or StructureOptimizer resolves `engineering-requirements.main`, it checks
  for unsaved project changes. The user can choose Save and Continue or Cancel.
- External requirement copies remain manual files and are not subject to the project dirty guard.

## Ownership

- `EngineeringRequirementsWidget` owns freeze state and emits a dedicated publication request only
  after a successful freeze.
- `EngineeringRequirementsPlugin` translates that request into a main-window save and reports the
  result to the widget.
- `RobWorkStudio` exposes read-only dirty-state inspection and a public transactional save wrapper.
- Downstream widgets own their import command UI but delegate all persistence to `RobWorkStudio`.

## Error Handling

- No open project: freezing remains valid in the current session; no automatic project save occurs.
- Transaction failure: show the existing save error, retain dirty state, and label the freeze as not
  yet published.
- Downstream Save and Continue failure: abort import and show the failure; never parse the stale
  requirement resource.
- Cancel: abort import without changing any project state.

## Tests

- A successful freeze emits requirement change before the publication request.
- Failed freeze emits no publication request.
- Main-window dirty inspection includes both manifest and provider state.
- The public save wrapper uses the existing project transaction and clears dirty state only on success.
- Both downstream import commands use the shared save-before-read policy.
