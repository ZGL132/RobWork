# WorkCell Analysis Flow Remediation Design

## Goal

Make a project created from a RobWork WorkCell or top-level device XML preserve a
single, managed scene identity from RobotModelBuilder through frozen requirements,
kinematic analysis, and structure optimization.

## Decisions

1. RobWork device XML remains a valid `Create Project from WorkCell` input because
   the RobWork loader intentionally accepts a top-level device as a WorkCell source.
   The robot-file action must inspect XML roots and reject non-URDF XML with an
   actionable redirect instead of sending it to the URDF importer.
2. In project mode, RobotModelBuilder `Save and Load` promotes its generated scene
   to the existing stable `mainWorkCell` resource. It must never call the standalone
   `setWorkcell(path)` path while the WorkCell Provider remains bound to another file.
3. Files required by a promoted generated scene are managed passive project assets.
   They participate in clone/package/integrity handling but have no editable Provider.
4. `sourceFileFingerprint` remains provenance evidence. A mismatch returns a warning
   and never invalidates an otherwise matching model/TCP/environment.
5. KinematicAnalysis and StructureOptimizer call the same `RequirementFreezer`
   scenario validation against the current WorkCell and State. A Q mismatch warns;
   model/TCP or environment mismatch blocks.
6. StructureOptimizer and EngineeringRequirements resolve project resources by stable
   manifest IDs. Directory scanning is not an authority.

## Data Flow

`Create Project from WorkCell` copies the source and relative dependencies into the
project. RobotModelBuilder creates `robot-model.main`. `Save and Load` writes the
generated XML set, registers its scene dependencies as passive resources, changes the
path of the stable main WorkCell resource, reloads project Providers from the in-memory
manifest, and leaves the manifest dirty until File > Save Project commits it.

EngineeringRequirements freezes against the active managed scene and resolves
`robot-model.main` through the project manifest. Both downstream consumers validate
the artifact against that active scene before converting any task data.

## Failure Policy

- RobWork device XML selected through the URDF action: reject and direct the user to
  `Create Project from WorkCell`.
- Generated scene promotion failure: keep the current project scene and report the
  exact resource/path failure.
- Source file hash mismatch only: import succeeds with a provenance warning.
- Model/TCP/environment mismatch: hard block and request revalidation/refreeze.

## Tests

- XML root classification distinguishes URDF, RobWork device, and WorkCell roots.
- Generated main-scene promotion keeps the stable resource ID and changes its path.
- Passive generated assets survive project clone/package.
- Source hash changes do not invalidate a matching environment.
- StructureOptimizer accepts Q-only changes and rejects an external fixture move.
- StructureOptimizer creates and adopts its project resource on first edit/import.
- EngineeringRequirements resolves `robot-model.main` rather than scanning a folder.
