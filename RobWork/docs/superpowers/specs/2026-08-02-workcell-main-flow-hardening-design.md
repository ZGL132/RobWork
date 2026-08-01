# Create Project from WorkCell Main Flow Hardening Design

## Goal

Make `Create Project from WorkCell` a complete, transactional, and portable workflow from a
RobWork WorkCell or top-level device XML through RobotModelBuilder, frozen engineering
requirements, kinematic analysis, and structure optimization. The accepted
`RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml` file is the end-to-end acceptance
fixture.

This design covers only the WorkCell-created project flow. `Create Project from Robot File` and
creating a model manually from an empty RobotModelBuilder project remain separate follow-up work.

## Authority Model

The project manifest is the only authority for managed files and cross-plugin bindings. Stable
resource IDs identify the active artifacts:

- `scene.main`: the active managed WorkCell, including after RobotModelBuilder promotion.
- `robot-model.main`: the editable RobotModelBuilder snapshot.
- `engineering-requirements.main`: the editable requirements and frozen artifact.
- `kinematic-analysis.main`: the persisted kinematic-analysis document.
- `structure-optimization.main`: the persisted structure-optimization document.

Consumers resolve these IDs through `RobWorkStudio` and never select among managed files by
directory scan or filename heuristics. Files selected explicitly outside a project remain valid
standalone inputs but are not allowed to replace a declared managed artifact implicitly.

## Project Creation

`Create Project from WorkCell` accepts full WorkCell XML and RobWork top-level device XML because
the RobWork loader supports both. Before switching the active project, creation performs these
steps in a candidate project context:

1. Parse the source XML and recursively enumerate relative `file` dependencies.
2. Reject absolute dependencies and dependencies that escape the source directory.
3. Copy the source and all resolved dependencies to project-owned paths.
4. Build a candidate manifest with `scene.main` and passive asset resources.
5. Load the copied `scene.main` through the real RobWork loader.
6. Write the manifest and switch the active project only after every prior step succeeds.

Failure removes only files created by this attempt. The previous project and WorkCell remain
usable. Error messages identify the failing source, target, dependency, or loader stage.

## RobotModelBuilder Synchronization

Opening the created project converts the active WorkCell into a `RobotModelSpec`. A valid
conversion ensures `robot-model.main` exists, adopts it into the RobotModelBuilder Provider, and
marks the project dirty until a project transaction persists the model snapshot.

`Save and Load` writes the generated XML set below the project, validates that set with the real
WorkCell loader, and promotes it by replacing the path and ownership of the existing `scene.main`.
The stable ID and entry point do not change. Generated XML dependencies and geometry referenced by
the promoted scene are registered as managed passive assets. The original imported scene remains a
project-owned provenance asset.

Promotion is transactional at the in-memory manifest and Provider level. If generation,
validation, Provider reload, or manifest mutation fails, the old `scene.main` remains active.

## Portable Frozen Requirements

Frozen requirements contain content fingerprints and a reconstructable scenario snapshot, but no
authoritative machine-specific absolute project paths. Managed model, WorkCell, and geometry
references use stable resource IDs or paths relative to the project root. Provenance may retain the
original display path only as non-authoritative evidence.

When reading an existing artifact that contains absolute paths, the importer may relocate a path
only by an exact match to a declared project resource or to a normalized project-relative path
captured in the artifact. It must not scan directories or choose a same-named file heuristically.
An unresolvable geometry required for collision-aware analysis blocks import with a specific error.

Cloning or packaging a project must copy every owned resource required by the frozen scenario.
Opening the clone resolves all managed paths against the clone root; it never depends on the source
project still existing.

## Downstream Analysis

KinematicAnalysis and StructureOptimizer read `engineering-requirements.main` only after dirty
project state has been transactionally published. Both call the shared frozen-scenario validation
against the active WorkCell and State:

- Robot model or TCP mismatch blocks import.
- Fixture or external environment mismatch blocks import.
- Joint-state-only differences are warnings and use the current state as the IK seed.
- Missing or changed provenance source files are warnings when the managed model, TCP, and
  environment still match.

`structure-optimization.main` declares dependencies on `scene.main`, `robot-model.main`, and
`engineering-requirements.main` when those resources are present. Its generated candidate models
resolve geometry from the portable frozen scenario and project resources.

## Robustness Requirements

- No operation overwrites an existing target project or unrelated file silently.
- A failed create, promote, freeze publication, analysis import, or optimization import leaves the
  last valid project state usable.
- Project transactions are the only writers of authoritative managed JSON and manifest files.
- Required resources are validated before project activation and before clone/package completion.
- UI errors identify the stage and resource path; cancellations do not report failures or mutate
  project state.
- Repeated synchronization, save, freeze, and import operations are idempotent and do not add
  duplicate manifest resources.

## Test Strategy

Tests use temporary directories and immutable source fixtures. They never require generated files
to exist beside `UR.wc.xml`.

1. Project-system tests create a project from top-level `UR.wc.xml`, verify all geometry is copied,
   verify the copied scene loads, and exercise rollback for malformed XML and missing dependencies.
2. RobotModelBuilder tests convert the copied scene, persist `robot-model.main`, promote a generated
   scene while preserving `scene.main`, and verify repeat promotion is idempotent.
3. EngineeringRequirements tests freeze the promoted scene and assert that managed paths in the
   artifact are portable and resolvable after relocation.
4. KinematicAnalysis tests import the published artifact from the active project and validate task
   points after relocation.
5. StructureOptimizer tests create an optimization problem from the same artifact, rebuild a
   collision-aware candidate after relocation, and verify the project resource dependency graph.
6. One end-to-end integration test clones the project to a different directory, removes access to
   the source project, reopens the clone, and repeats frozen validation and optimization problem
   creation successfully.

## Acceptance Criteria

- `UR.wc.xml` creates a self-contained project without modifying the source directory.
- RobotModelBuilder produces and transactionally persists the managed robot model and generated
  scene while retaining the stable main-scene identity.
- Requirements freeze and publish without absolute managed project paths.
- KinematicAnalysis imports and validates the frozen tasks.
- StructureOptimizer imports the same artifact and can build/evaluate candidates.
- The preceding operations still succeed from a cloned project after the original project directory
  is unavailable.
- Focused project, RobotModelBuilder, requirements, kinematic-analysis, and structure-optimizer tests
  pass without mutable sample sidecars.
