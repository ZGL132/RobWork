# Engineering Requirements Plugin

## Responsibility Boundary

The plugin owns requirement editing, semantic validation, compilation, freezing and publication. It does not perform IK, workspace sampling, collision evaluation or coverage calculation. Those operations consume the frozen `RequirementExecutionSet` contract from `robotanalysiscore`.

## Data Flow

```text
RequirementSet (editable JSON)
  -> RequirementCompiler (rules, diagnostics, Included/Excluded)
  -> FrozenRequirementArtifact (schema v4, provenance, scene snapshot)
  -> RequirementExecutionSet (downstream execution contract)
  -> kinematicanalysis / structureoptimizer
```

`Must` validation errors block freezing. `Should` and `Info` entries are retained for audit; invalid optional entries are emitted with `compileState = Excluded` and are not algorithm inputs.

## Units and Semantics

| Field | Unit | Meaning |
| --- | --- | --- |
| `position`, `center`, `size` | m | Reference-frame Cartesian values |
| `rpyDeg`, `fixedRpyDeg` | degree | Roll, pitch, yaw |
| position tolerance | m | Allowed Cartesian error |
| orientation tolerance | degree | Allowed angular error |
| joint margin | rad or model-native | Minimum distance from joint limits |
| manipulability | model-native scalar | Minimum Jacobian quality threshold |
| coverage | `[0, 1]` | Minimum accepted fraction |

`Quick` is an estimate suitable for screening. `Verified` is the formal acceptance stage and requires at least two samples per spatial axis plus complete orientation policy fields.

## Stable Diagnostic Codes

- `REQ_SCHEMA_UNSUPPORTED`: artifact type or schema cannot be read.
- `REQ_REQUIRED_FIELD_MISSING`: a required ID, name, frame or TCP field is missing.
- `REQ_DUPLICATE_ID`: requirement IDs are not unique across tasks and workspace regions.
- `REQ_GEOMETRY_FEATURE_INVALID`: a geometry-backed task has no complete feature reference.
- `REQ_ORIENTATION_TARGET_MISSING`: an orientation rule requires a target frame or point.
- `REQ_POSE_INVALID`: pose or tolerance values are non-finite or outside their domain.
- `REQ_ORIENTATION_RULE_INVALID`: roll limits or other orientation rule values are invalid.
- `REQ_PATH_POLICY_INVALID`: approach/retract distances are invalid.
- `REQ_VALIDATION_POLICY_INVALID`: task validation margins or manipulability values are invalid.
- `REQ_CONFIDENCE_INVALID`: task confidence is outside `[0, 1]`.
- `REQ_WORKSPACE_GRID_TOO_COARSE`: Verified region has fewer than two samples per axis.
- `REQ_WORKSPACE_DIRECTION_SAMPLES_INVALID` / `REQ_WORKSPACE_ROLL_SAMPLES_INVALID`: orientation sample count is invalid.
- `REQ_WORKSPACE_ORIENTATION_TARGET_MISSING`: AlignFrame has no target frame.
- `REQ_WORKSPACE_GEOMETRY_TARGET_MISSING`: AlignGeometryNormal lacks frame or geometry.
- `REQ_WORKSPACE_POINTING_TARGET_MISSING`: PointAtTarget has neither target frame nor point.
- `REQ_WORKSPACE_VALIDATION_POLICY_INVALID`: tolerance, margin or manipulability is negative/non-finite.
- `REQ_V3_REQUIRES_REFREEZE`: v3 data was migrated to Quick and must be refrozen for Verified acceptance.

## Schema Compatibility

Frozen artifacts write schema v4. Readers accept v3 and v4. The v3 migration API never overwrites its input and emits a v4 object with legacy workspace regions marked `Quick` plus `REQ_V3_REQUIRES_REFREEZE` diagnostics.

The v4 `RequirementExecutionSet` is the only downstream execution contract. It includes process type, pose/orientation policy, workspace sampling and validation policy, compile state, diagnostics and provenance. Quick regions may use one sample per axis; Verified regions require at least two. Adapters reject a v4 artifact whose execution provenance does not match the frozen artifact fingerprint.

## Publication Sequence

1. Edit the requirement set.
2. Run `RequirementCompiler::validateDetailed()` and show diagnostics.
3. Resolve all blocking Must diagnostics.
4. Freeze against the active WorkCell, robot model and state.
5. Persist the generated project resource transactionally.
6. Downstream plugins resolve the published resource and verify provenance before evaluation.

The current TCP pose is only freeze provenance. It is not a requirement task and must not be mixed into requirement feasibility results.
