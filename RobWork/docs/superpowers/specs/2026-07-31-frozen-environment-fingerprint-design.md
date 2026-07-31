# Frozen Environment Fingerprint Design

## Goal

Decouple frozen engineering requirement validity from the analysed robot's live
joint state. A frozen requirement remains importable after robot JOG when the
robot model and external environment are unchanged. Moving a fixture, workpiece,
or other external scene object invalidates the artifact.

## Scope

This changes the frozen-requirement contract shared by EngineeringRequirements,
KinematicAnalysis, and StructureOptimizer. It does not change trajectory-start
state policy or add trajectory evaluation.

## Artifact Contract

`FrozenRequirementArtifact` advances to schema version 3. Version 3 records
three independent evidence classes:

| Evidence | Purpose | Import decision |
| --- | --- | --- |
| `RobotModelFingerprint` and TCP configuration | Robot topology, geometry, limits, and installed TCP identity | Mismatch blocks import |
| `environmentFingerprint` | External Frame topology, poses, and scene geometry affecting the workcell | Mismatch blocks import |
| `robotStateSnapshot` | Captured device name, joint vector, TCP world pose, and capture time | Mismatch warns but permits import |

The v3 artifact retains the existing compiled requirements and scenario snapshot.
The scenario snapshot stores `environmentFingerprint` rather than a full-State
fingerprint. A `RobotStateSnapshot` is stored both as audit evidence and as the
source for a live-state comparison at import time.

Schema versions 1 and 2 are deliberately not accepted as current artifacts.
They only contain a full-State fingerprint, whose provenance cannot be separated
reliably into robot and environment evidence. Consumers must report that the
artifact is legacy and require the user to validate and freeze again.

## Environment Classification

The classification rule is based on ownership, not `MovableFrame` type.

1. Resolve the analysed device from `RobotModelSpec::robotName`.
2. Determine the Frames in that device's kinematic subtree.
3. Exclude transforms driven by those Frames from the environment fingerprint.
4. Include every other WorkCell Frame with its stable identity, parent relation,
   and world transform at the current State.

Consequently, JOG on the analysed robot changes only the live robot-state
snapshot comparison. An external `MovableFrame`, such as a fixture or a machine
door, remains included and invalidates the artifact when moved. A tool installed
on the analysed robot is represented by the robot/TCP fingerprint, rather than
being treated as external environment.

The environment fingerprint is combined with the serialized scenario snapshot,
which already carries external collision geometry, geometry resources, scaling,
and frame attachments. This preserves hard invalidation for scene geometry
changes as well as pose changes.

## Validation Flow

Each consumer receives a structured validation result with a validity status and
an optional live-robot-state warning.

1. Reject incomplete, tampered, or legacy artifacts.
2. Verify the model and TCP binding against the active `RobotModelSpec`; reject
   a mismatch.
3. Recompute the environment fingerprint from the active WorkCell and State;
   reject a mismatch.
4. Compare the active device Q with the frozen `robotStateSnapshot.q`.
5. Allow import in either Q outcome. Return a warning only when Q differs.

KinematicAnalysis displays the warning in its import status:

> Robot joint state differs from the frozen state, but fixtures and external
> environment are unchanged. Frozen requirements remain valid; the current joint
> state is used as the IK initial seed.

StructureOptimizer permits the import without treating the frozen Q as an
optimization input. It retains frozen and runtime state evidence where the
existing provenance/reporting model permits it.

## Failure Messages

- Legacy schema: `Frozen requirement artifact uses legacy state-based evidence.
  Validate and freeze the requirements again.`
- Model/TCP mismatch: `Robot model or TCP configuration has changed. Validate
  and freeze the requirements again.`
- Environment mismatch: `Fixture or external environment position has changed.
  Validate and freeze the requirements again.`

## Tests

Tests cover the public behavior at the EngineeringRequirements and downstream
adapter boundaries:

- A changed joint state on the analysed device preserves environment validity and
  returns a non-blocking state warning.
- A moved external `MovableFrame` fixture fails environment validation.
- A model fingerprint mismatch is rejected.
- A v2 artifact is rejected with the legacy-refreeze message.
- JSON round-tripping preserves v3 environment and robot-state audit evidence.
- KinematicAnalysis imports successfully on a joint-state warning and rejects an
  environment mismatch.

## Compatibility and Migration

No automatic migration is attempted. A full-State fingerprint cannot establish
the independent external-environment value required by v3. Existing frozen files
therefore remain readable as JSON but are not importable until revalidated and
frozen by EngineeringRequirements.
