# Structure Optimization Plugin Reimplementation Design

## Status

Approved direction: reimplement the Structure Optimization plugin from a clean
implementation while retaining the architecture and acceptance rules in
`RoboWorkStudio-Structure-Optimization-Refactoring-Final-Design.md`.

This document changes the implementation sequence, not the target architecture.
The legacy Structure Optimizer remains reference-only until the replacement
passes its gates.

## Objective

Build a new robot structure/kinematic optimization workflow with one authoritative
kinematic model, a transactional candidate compiler, shared KinematicAnalysis
evaluators, reproducible multi-stage evidence, and an auditable Qt workbench.

The replacement must not use the legacy Structure Optimizer's classes, state
machine, scoring path, or runtime JSON model as its computational source.

## Scope

The first release keeps the final design's scope:

- Revolute, Prismatic, Fixed, Flange, Tool/TCP and required auxiliary frames;
- SE(3) plus explicit joint-axis canonical kinematics;
- DH as a lossy projection/compatibility view only;
- continuous, integer, and discrete design-variable contracts;
- baseline, Quick, Verified, and Final evidence stages;
- reachability, region/orientation coverage, joint margin, singularity,
  manipulability, and collision evidence;
- hard constraints, soft constraints, objectives, comparison, preview, export,
  persistence, fingerprints, cancellation, and deterministic replay;
- extension boundaries for dynamics, trajectories, robustness, and actuator
  selection without implementing those domains in the first release.

Legacy project files are read-only migration inputs by default. New runtime
documents have one canonical schema and never write legacy fields back.

## Architectural Sequence

### Phase A: Freeze upstream contracts

Define and test the Qt-free contracts before implementing the new optimizer:

1. `CanonicalKinematicModel` with explicit Frame, Joint, DOF, and Q mappings.
2. The single FK convention, units, zero-offset, and physical/operational limit
   semantics.
3. `RobotModelBuilder` publish/import boundary, model and environment fingerprints,
   and canonical model export.
4. Frozen `RequirementExecutionSet` input and validation rules.
5. Shared evaluator interfaces and evidence semantics in `robotanalysiscore` /
   `kinematicanalysis`.
6. Stable diagnostics, availability, stage, feasibility, and quality states.

This phase may add adapters around existing RobotModelBuilder and KinematicAnalysis
implementations. It does not require rewriting their complete Widgets.

### Phase B: Adapt upstream plugins

RobotModelBuilder remains the authoring UI, but publishes canonical model data at
its project boundary. KinematicAnalysis remains the analysis UI, but exposes its
FK/IK/collision/coverage evaluators through a shared core service. Their existing
UI code must not become a dependency of the new optimizer.

Required gates:

- canonical FK is equivalent to the RobWork Device FK on mixed
  Revolute/Prismatic/Fixed/Tool fixtures;
- DH projection reports `Exact`, `Lossy`, or `Unsupported` and never overwrites
  canonical SE(3);
- missing collision data is `DataInsufficient`, never collision-free;
- Quick, Verified, and partial/cancelled evidence remain distinguishable;
- all upstream inputs carry stable content fingerprints.

### Phase C: Implement the new optimizer core

Create a new namespace and independently testable core modules:

```text
Canonical Model + Frozen Requirements
  -> DesignSpaceCompiler
  -> DesignVector
  -> CandidateCompiler
  -> EvaluationPlan / Shared Evaluators
  -> Metric + Constraint + Objective results
  -> Immutable CandidateResult / RunSnapshot
  -> Optimizer, comparison, report, preview
```

The CandidateCompiler always starts from an immutable baseline, applies ordered-
independent transactional patches, validates read/write sets and produces
kinematic, visual, and collision artifacts together. It never mutates the live
RobWorkStudio WorkCell.

### Phase D: Implement the new plugin UI

The UI configures and displays formal core contracts only. It does not calculate
engineering metrics, own worker lifecycle state, construct DesignVectors from
table row numbers, or mutate candidate status. The first UI slice is:

```text
model context -> variables -> baseline -> one candidate -> result inspector
```

Run control, comparison, preview, persistence, report, and project-resource
integration are added after the core vertical slice is green.

### Phase E: Switch and retire

After all model-only, GUI, deterministic, migration, and end-to-end gates pass:

1. switch the official plugin registration and CMake target to the new entry;
2. retain the legacy implementation as a read-only reference or archived target
   for one release cycle;
3. remove obsolete runtime paths only after the deletion audit confirms that no
   evaluator, serializer, UI slot, or project provider still depends on them.

## Non-Goals

- no complete rewrite of RobotModelBuilder or KinematicAnalysis Widgets before
  their core contracts are proven;
- no reuse of the legacy Structure Optimizer's computational classes merely to
  accelerate the first prototype;
- no second implementation of FK, IK, collision, workspace, or metric formulas;
- no DH round-trip as a canonical model conversion;
- no runtime compatibility mode with two competing JSON truths;
- no dynamics, trajectory, actuator, or topology optimization in the first release.

## Testing and Acceptance

Every phase has focused model-only tests before UI integration. The acceptance
set must cover:

- mixed six-DOF and mixed-joint canonical FK;
- nonzero pitch/Pos-Y lossy DH projection;
- design-variable zero values, domains, dependency cycles, and write conflicts;
- candidate isolation, deterministic fingerprints, and artifact consistency;
- shared evaluator equivalence with KinematicAnalysis;
- missing/partial/cancelled evidence and hard-constraint behavior;
- cache invalidation on every semantic input fingerprint;
- serial/parallel determinism and cooperative cancellation;
- legacy JSON read-only migration and canonical round-trip;
- preview isolation from the live WorkCell;
- Windows Qt tests under the repository's required x64 developer environment,
  using `QT_QPA_PLATFORM=windows` and one absolute executable per process.

Completion requires clean build, registered tests actually executed, end-to-end
fixtures passing, `git diff --check` clean, and no duplicate legacy algorithm
path in the released runtime.

## Decision

Proceed with Phase A first. The next implementation plan must be rewritten from
the old refactoring plan into bounded tasks for Phases A-E, with explicit file
ownership and stop conditions. No production code changes are authorized until
that plan is reviewed.
