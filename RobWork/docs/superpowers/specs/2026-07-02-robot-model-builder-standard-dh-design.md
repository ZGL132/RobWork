# RobotModelBuilder Standard DH Alignment Design

## Goal

Make the RobotModelBuilder plugin treat the DH table as standard DH parameters end-to-end so that:

- the DH table values shown in the plugin,
- the generated preview geometry,
- the exported `.wc.xml` `<DHJoint>` elements,
- and the loaded 3D scene in RobWorkStudio

all describe the same kinematic convention.

## Root Cause

The current implementation mixes two DH conventions:

- Preview link geometry in `RobotModelXmlWriter.cpp` uses the translation implied by `craigDH`.
- Exported XML writes `<DHJoint ... type="schilling" />`, which RobWork loads using the standard DH transform.

This creates visible mismatch whenever `alpha != 0` and `d != 0`, because the preview places the next joint origin differently from the actual loaded scene.

## Decision

Use standard DH as the single plugin convention.

In RobWork terms, this means:

- keep exporting DH joints as `type="schilling"`,
- treat the table columns `alpha / a / d / offset` as standard DH parameters,
- compute DH preview link geometry from the same standard DH transform used by the exported scene.

This is the smallest safe fix because it preserves the existing XML format and aligns the plugin with the scene that RobWork already loads.

## Non-Goals

- Do not add a second DH mode selector to the UI.
- Do not redesign the table structure.
- Do not change non-DH `Joint + RPY + Pos` mode behavior.
- Do not refactor unrelated drawable or dynamics behavior.

## Implementation Scope

### 1. Preview geometry

Update the DH branch of `computeLinkPose(...)` so the joint-to-joint vector is derived from the standard DH transform, not the Craig-style translation.

For standard DH with revolute joints and preview at zero joint position:

- `theta = offset`
- translation is `(a*cos(theta), a*sin(theta), d)`

This keeps the preview cylinder aligned with the actual origin of the next joint in the exported scene.

### 2. XML generation

Keep `<DHJoint ... type="schilling" />` unchanged, because this is already the correct exported representation for standard DH in this plugin.

### 3. Documentation text

Update plugin-facing documentation comments or docs that currently describe DH preview as `RotX(alpha) * TransX(a) * TransZ(d)` so they reflect standard DH semantics.

### 4. Tests

Add or update tests so DH mode explicitly verifies standard-DH-aligned preview behavior.

At minimum:

- exported XML in DH mode still contains `type="schilling"`,
- a DH row with `alpha=-90, a=0, d=0.38, offset=0` produces preview link translation `(0, 0, 0.38)`,
- a DH row with non-zero offset uses the standard DH XY projection implied by `a*cos(theta), a*sin(theta)`.

## Files Expected To Change

- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
- `docs/RobotModelBuilderDynamics.md`

## Risks

- Existing users may have mentally interpreted the table using the current incorrect preview behavior. After this fix, preview output will change to match the exported robot instead of the previous approximation.
- The default six-axis DH sample values were originally derived from the transform table in a simplified way. They may remain a rough example, but after this change at least their preview and exported scene will be internally consistent under standard DH.

## Verification Plan

- Run the RobotModelBuilder XML writer test binary.
- Verify new DH-specific assertions pass.
- Confirm no existing non-DH preview assertions regress.

