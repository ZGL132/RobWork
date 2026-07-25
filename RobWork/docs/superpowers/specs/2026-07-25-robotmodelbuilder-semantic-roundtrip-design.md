# RobotModelBuilder Semantic Round-Trip Design

**Goal:** Opening a RobWork XML in the main application synchronizes all editable robot and scene information into RobotModelBuilder, and saving without edits produces XML that reloads to the same model semantics.

## Scope

Semantic equality includes WorkCell/device identity, frame hierarchy and transforms, joint kinds and limits, Drawables, CollisionModels, scene objects, collision and proximity setup, and DynamicWorkCell data. XML formatting, comments, attribute ordering, and other byte-level presentation details are out of scope.

Unknown XML elements or attributes that cannot be represented by the editable model must be retained as opaque extensions and emitted again when the corresponding document is saved. This prevents data loss for valid documents that use extensions outside RobotModelBuilder's UI.

## Architecture

`RobotModelBuilderPlugin::open(WorkCell*)` remains the main-application entry point. It asks `WorkCellConverter` for an imported `RobotModelSpec`; the widget fills its controls from that spec and keeps the imported document identity for subsequent save actions.

The converter combines runtime data with a lightweight source-XML reader:

1. Runtime `WorkCell` remains authoritative for frames, joints, limits, loaded objects, collision/proximity state, and values after includes are resolved.
2. Source XML supplies data that runtime objects do not expose losslessly, including geometry representation, custom filenames, and unsupported extension fragments.
3. The converter only creates default drawables for a newly created model. Import never synthesizes default geometry.

The writer emits a normalized document from the editable spec. It writes imported target filenames where known and appends preserved opaque extensions at the original supported scope. It does not claim byte preservation.

## Data Model

`RobotModelSpec` gains import provenance: the primary WorkCell source path, the primary device-file path (when included), and output filenames. It also gains extension collections scoped to WorkCell and device XML so the writer can preserve unsupported semantic content.

Geometry import maps supported `<Drawable>` and `<CollisionModel>` elements into existing specs, preserving name, reference frame, transform, RGB, collision flag, primitive parameters, and Polytope/Mesh/STL path. Scene objects receive the same treatment. Source geometry is matched to runtime objects by name and reference frame; a runtime-only object is represented with a warning rather than silently converted to a Box.

## Save Behavior

When saving a successfully imported document, the writer targets the imported primary device and scene filenames in their original directory. It writes a normalized but semantically equivalent document and its referenced setup files. A document created by RobotModelBuilder continues to use generated filenames.

If required source references cannot be resolved, import surfaces warnings and disables semantic round-trip guarantees for that source; saving must not replace the original XML silently.

## Validation and Tests

Add a fixture with primitives, mesh geometry, scene geometry, collision/proximity setup, a DynamicWorkCell, and a deliberately unsupported extension. The test sequence is:

1. Load the fixture through `WorkCellLoader` and convert it.
2. Verify imported specs preserve all supported geometry fields and imported output identity.
3. Save the imported spec into a temporary copy.
4. Reload the saved XML and compare runtime model semantics with the original fixture, including geometry and setup data.
5. Assert that the opaque extension is still emitted.

The existing widget/plugin behavior is covered by a focused testable import API or Qt integration test confirming `open(WorkCell*)` delegates to the sync entry point.

## Error Handling

Malformed or unreadable source XML, unresolved includes, unsupported runtime geometry, and invalid opaque fragments are reported as warnings. The plugin still shows recoverable runtime data, but marks the session as not safe for semantic-preserving overwrite until the user saves to a new target or corrects the source.
