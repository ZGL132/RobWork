# RobotModelBuilder Kinematics Sync Design

## Goal

Make the `DH Parameters` table and the `Joint + RPY + Pos` table stay synchronized immediately after each edit so the plugin exposes one consistent kinematics model through two views.

## Decision

Use **bidirectional strong synchronization**:

- editing the DH table immediately updates the explicit joint transform table,
- editing the explicit joint transform table immediately updates the DH table,
- explicit transforms are projected back to the subset representable by the plugin's current standard-DH model.

## Standard-DH Mapping

The plugin now treats DH as standard DH (`type="schilling"`). For a revolute joint at zero commanded joint value:

- `alpha = roll`
- `offset = yaw`
- `pitch = 0`
- `pos = (a*cos(offset), a*sin(offset), d)`

So the explicit table is the zero-pose transform view of the same standard-DH row.

## Projection Rule

When the explicit table is edited, the row is normalized back into standard-DH-compatible form:

- `alphaDeg = roll`
- `offsetDeg = yaw`
- `d = z`
- `a = x*cos(yaw) + y*sin(yaw)`
- explicit `pitch` is forced to `0`
- explicit `type` is forced to `Revolute`
- explicit `pos` is rewritten from the normalized DH row

This ensures both tables converge to one shared representation instead of drifting apart.

## UI Behavior

- Synchronization happens on table edit events.
- Invalid intermediate values are ignored until the edited row becomes parseable.
- Internal guard logic prevents recursive signal loops while updating the mirrored table.

## Test Strategy

Add widget-level tests that instantiate `RobotModelBuilderWidget`, edit one table programmatically, and verify the other table updates immediately.

At minimum:

- DH row edit updates explicit `RPY` and `Pos`
- explicit row edit updates DH values and rewrites explicit row to the normalized standard-DH projection

