# Robot File Main Flow Hardening Design

## Goal

Harden `Create Project from Robot File` so a URDF robot source becomes a portable,
self-contained RobWorkStudio project and can proceed through RobotModelBuilder review,
managed WorkCell generation, engineering-requirement freezing, kinematic analysis, and
structure optimization.

This design covers URDF `<robot>` sources only. RobWork `<WorkCell>`, `<SerialDevice>`,
`<TreeDevice>`, and `<ParallelDevice>` XML remains owned by `Create Project from WorkCell`.
The local acceptance source is:

`RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/300kg_urdf/output/300kg.urdf`

Its meshes are under:

`RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/300kg_urdf/meshes`

The source directory is read-only test input. Tests and project creation never modify it.

## User Workflow

1. The user chooses `File > Create Project from Robot File...` and selects a URDF.
2. RobWorkStudio verifies that RobotModelBuilder is available and preflights the source
   without changing the active project or Builder UI.
3. The user chooses a new `.rwproj` path.
4. The project system prepares a self-contained candidate project in a staging area.
5. RobotModelBuilder preflights the managed URDF copy.
6. Only after all preparation succeeds does RobWorkStudio close the previous project and
   activate the new project.
7. RobotModelBuilder opens with the imported model marked dirty for explicit review.
8. The user clicks `Save and Load` to transactionally publish the generated robot XML and
   promote the generated scene to the stable `mainWorkCell` entry point.
9. The user saves the project and continues to EngineeringRequirements, KinematicAnalysis,
   and StructureOptimizer.

Until step 8 succeeds, downstream analysis remains blocked with an actionable message that
the managed WorkCell has not yet been generated.

## Architecture

The project system owns files, paths, manifests, activation, and rollback. RobotModelBuilder
owns URDF semantics and the editable robot model. The main application continues to avoid a
static link dependency on the optional plugin.

### Project-Side URDF Packager

A focused project-side service prepares a robot-source project without activating it. It:

- parses the URDF with structured Qt XML APIs;
- collects every `<mesh filename>` reference;
- resolves `package://`, relative, and absolute local-file references;
- rejects missing, non-regular, or unsupported references;
- deduplicates references to the same canonical source file;
- assigns deterministic, collision-free paths below the project asset directory;
- copies only the URDF and referenced assets into a staging tree;
- rewrites the managed URDF to use project-contained relative paths;
- re-parses the rewritten URDF and verifies that no absolute or project-escaping reference
  remains; and
- creates a candidate manifest without changing the active `ProjectManager` state.

The candidate manifest contains:

- `robot-source.main`, a required project-owned source resource;
- one required passive resource per copied mesh;
- dependencies from `robot-source.main` to those passive resources; and
- a stable `robotSource` entry point referencing `robot-source.main`.

The generated `robot-model.main` resource is registered by RobotModelBuilder after project
activation and depends on `robot-source.main`. A robot-file project has no `mainWorkCell`
entry point until the user completes `Save and Load`.

### RobotModelBuilder Capability Boundary

RobotModelBuilder exposes two non-interactive Qt meta-object capabilities:

- a preflight operation that parses a URDF without mutating the current widget or project;
- a commit operation that applies the already validated managed source to the widget and
  establishes the dirty generated-document baseline.

The main window checks both capabilities before closing the active project. Preflight runs
against the original source and again against the rewritten managed copy. The second pass
proves that packaging did not change the model or leave unresolved assets.

### Candidate Activation

Candidate files remain invisible to the active project until source packaging, manifest
validation, and Builder preflight all succeed. Activation then commits the candidate manifest
and project-owned files, switches ProjectManager context, adopts the generated model resource,
and displays RobotModelBuilder.

The activation sequence preserves the current project until the new candidate is ready. An
unexpected activation or adoption failure rolls back the candidate and restores the previous
project context rather than leaving a source-only draft active.

## Portable Model Data

RobotModelBuilder may use absolute paths internally while editing, but managed project files
must not persist them.

When saving `robot-model.main`:

- geometry paths inside the project root become project-relative paths;
- external geometry paths are rejected because robot-file project creation already imports
  every accepted source asset; and
- `saveDirectory` remains runtime-only.

When loading `robot-model.main`, relative geometry paths resolve from
`ProjectDocumentContext.projectDirectory`. Process current working directory is never a
fallback for a managed project.

Generated device and scene XML uses paths relative to the final XML location. Project move,
clone, package, reopen, and downstream snapshot operations therefore remain independent of the
source machine path.

## Transactional Save And Load

Project-mode `Save and Load` publishes the full generated file set as one logical operation:

1. collect and validate the current RobotModelSpec;
2. generate all XML content before changing final files;
3. verify every referenced geometry remains inside the managed project;
4. write extension-preserving sibling stage files;
5. retain recoverable backups of existing target sidecars;
6. replace the target set;
7. load the final scene with the real RobWork WorkCell loader;
8. promote the scene to the stable `mainWorkCell` resource and reconcile registry metadata;
9. remove backups only after both load and promotion succeed.

On failure, the operation restores all replaced sidecars, the previous WorkCell, the previous
manifest, and registry ordering. The imported Builder model remains available for correction
and retry. Standalone RobotModelBuilder saving keeps its existing behavior.

## Failure Policy

Creation is atomic from the user's perspective:

- cancellation changes nothing;
- a missing or incompatible RobotModelBuilder aborts before project creation;
- invalid URDF structure or semantics aborts before project switching;
- an unresolved mesh is a hard error, not a warning;
- existing target files are never overwritten during project creation;
- only files created by the current attempt are removed during rollback;
- pre-existing target-directory content is preserved;
- provider, manifest, and activation failures restore the old project context; and
- recent-project settings and window title update only after successful activation.

Errors identify the failed stage, the original URDF reference, the resolved source path when
available, and the intended project target. Errors must not reduce all failures to a generic
"import failed" message.

## Tests

### Packaging Unit Tests

Generated temporary fixtures cover:

- relative, absolute, and `package://` mesh references;
- unresolved and non-regular mesh inputs;
- duplicate references and same-basename source collisions;
- malformed and non-URDF XML;
- target-file collisions;
- rewrite validation and project-boundary enforcement; and
- rollback that preserves both existing target files and the current project context.

### RobotModelBuilder Integration Tests

Tests verify:

- preflight has no widget, provider, or project side effects;
- the managed model stores only project-relative geometry paths;
- reload resolves paths from the explicit project root with a hostile process CWD;
- source, generated model, and generated WorkCell dependencies remain stable;
- analysis is blocked before `mainWorkCell` promotion; and
- every injected `Save and Load` failure restores files, WorkCell, manifest, and registry state.

### 300kg Acceptance Test

The local acceptance test uses the supplied `300kg.urdf` and seven STL files. It copies input
into `QTemporaryDir` and verifies:

- robot name `300kg` and six movable joints;
- visual and collision geometry resolution;
- self-contained source and mesh resources;
- managed `.rmb.json` saving;
- transactional XML publication and `mainWorkCell` promotion;
- engineering-requirement freezing;
- KinematicAnalysis consumption;
- StructureOptimizer frozen-requirement import; and
- successful reopen and resource resolution after moving the whole project directory.

Because `300kg_urdf` is intentionally ignored and may be absent in another checkout, generated
small fixtures provide mandatory continuous coverage. The real-asset acceptance test is
registered conditionally and is required in this workspace.

## Verification

Final verification starts from a clean Debug build because shared C++ interface or layout
changes can invalidate incremental MSVC objects. Qt tests run sequentially with `windows` QPA
on Windows and `offscreen` elsewhere. Verification includes focused RobotModelBuilder,
ProjectSystem, EngineeringRequirements, RobotAnalysisCore, KinematicAnalysis, and
StructureOptimizer suites, the real 300kg acceptance test, `git diff --check`, project move and
reopen checks, and independent specification and quality reviews.

## Out Of Scope

- accepting RobWork WorkCell or device XML in the robot-file action;
- manual from-scratch RobotModelBuilder project creation;
- downloading ROS packages or remote mesh assets;
- supporting Xacro evaluation, SDF, or network URIs;
- keeping external project asset references; and
- redesigning unrelated project or plugin UI.
