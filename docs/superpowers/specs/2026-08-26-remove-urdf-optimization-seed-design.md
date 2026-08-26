# Remove URDF import optimization seed

## Goal

Keep URDF/Xacro project creation focused on importing a usable robot model.  Move all structure-optimization variable selection and range configuration to the Structure Optimizer plugin.

## Scope

- Remove the **Kinematic Chain and Link Preselection** page from `RobotProjectImportWizard`.
- Remove mutable-link and range fields from `RobotProjectImportRequest` and all associated parsing, validation, review, and UI state.
- Stop generating and registering `structure-optimization-seed.main.json` when a robot project is created.
- Stop reading and applying that seed in `StructureOptimizerWidget`.
- Preserve existing project files.  A legacy seed file is neither deleted nor migrated; it is simply no longer used.

## Resulting flow

1. The import wizard proceeds from mesh/package-root configuration directly to review and commit.
2. Import creates the robot model and ordinary managed resources only.
3. When a user creates an optimization project from a model snapshot, the optimizer produces its normal suggested design variables.
4. The user enables variables and sets bounds only in the optimizer's variable UI.

## Compatibility and error handling

- Existing `.rwproj` files remain openable even when they list or contain the legacy seed asset.
- The optimizer no longer resolves, parses, or reports errors for the legacy seed.
- No migration or deletion is performed, so user data is not modified destructively.

## Verification

- Build the affected RobWorkStudio targets.
- Run the focused non-GUI/model test target where available.
- Launch the affected Windows Qt test executable under the Visual Studio x64 developer environment, one executable at a time, with `QT_QPA_PLATFORM=windows` if a GUI test exists.
- Verify source-level removal of the wizard page and seed reader/writer identifiers.
