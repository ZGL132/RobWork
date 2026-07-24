# Plugin Icon Design

## Goal

Give RobotModelBuilder, KinematicAnalysis, and StructureOptimizer distinct plugin
icons that match RobWorkStudio's existing low-saturation, grey, three-dimensional
rendered icon style.

## Visual System

- Transparent background with a compact square composition for plugin lists.
- Matte grey metal surfaces, soft directional highlights, and restrained ambient
  shadowing.
- One dominant mechanical object per icon, with a single secondary graphic cue.
- No text, logos, bright accent colors, or fine details that disappear at 32 px.

## Icon Set

### RobotModelBuilder

A three-link articulated robot arm on a square base. Cylindrical joints and
contrasting link faces make the construction and assembly purpose recognizable.

### KinematicAnalysis

A compact articulated linkage within a translucent quarter-sector workspace
envelope. The linkage communicates kinematics; the sector communicates reachable
motion and analysis without needing small graph labels.

### StructureOptimizer

A triangulated truss block with an integrated upward convergence arrow. The truss
represents configurable arm structure; the arrow conveys iterative improvement and
optimization.

## Integration

Each plugin receives a 64 px PNG asset stored beside its `resources.qrc` file.
The QRC file exposes a plugin-specific resource path, and the plugin constructor
loads that path through `QIcon`.

## Validation

Confirm every resource is listed in its QRC file, every constructor uses the
matching resource path, and the resulting PNGs retain readable silhouettes at
small display sizes.
