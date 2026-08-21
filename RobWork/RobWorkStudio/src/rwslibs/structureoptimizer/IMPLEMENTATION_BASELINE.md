# Structure Optimizer implementation baseline

This record is the Phase 0 entry point for the refactoring plan.  It records
the repository state discovered on 2026-08-18 and must be updated only with
fresh verification evidence.

## Repository and protected work

- Repository root: `D:/10_Source_Repos/21_robot/RobWork/RobWork`
- Starting revision: `5550d10` (`docs: specify hybrid kinematic optimization algorithm`)
- Current branch: `main` (the user explicitly requested in-place implementation)
- Protected pre-existing edits: `docs/superpowers/specs/2026-08-17-kinematic-design-space-compiler-design.md`, two untracked UR fixture directories, and the two untracked refactoring-plan documents.

## Existing StructureOptimizer surface

- Core target: `sdurws_structureoptimizer_core`.
- Plugin target: `sdurws_structureoptimizer`.
- Test target: `sdurws_structureoptimizer_test`.
- Existing compatibility entry points include `StructureOptimizationProblem`,
  `StructureOptimizationResult`, `StructureOptimizationController`, project
  adapters, table-model roles, preview controller, JSON reader, and the legacy
  evaluator interface.
- Existing KinematicAnalysis dependencies already link `ConfigurationEvaluator`,
  `TargetEvaluator`, `RegionCoverageEvaluator`, and requirement execution
  types.  New code must reuse them rather than clone IK/FK/collision logic.

## Build and test entry points

- Visual Studio x64 build helper: `scripts/build-msvc-debug.cmd`.
- Configured Debug build directory:
  `build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug`.
- Test executable:
  `build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/sdurws_structureoptimizer_test.exe`.
- CTest registrations:
  `sdurws_structureoptimizer_test`,
  `sdurws_structureoptimizer_test_evaluator_consistency`, and
  `sdurws_structureoptimizer_test_cache`.
- Widget suites are run with `QT_QPA_PLATFORM=windows`, one absolute test
  executable at a time.  Model-only suites use `QCoreApplication`.

## Refactoring boundary

The old `StructureOptimizationProblem` remains the compatibility input until a
canonical-model shadow is introduced.  The initial contracts and mathematical
helpers are core-only and do not change legacy candidate generation, scoring,
or UI behavior.

## Phase 0 exit evidence (2026-08-18)

- **S03 contracts:** `StructureOptimizationContracts.hpp/.cpp` separates
  candidate lifecycle from feasibility, evidence stage, and quality.  It also
  provides `EvaluationCompletion`, a stable diagnostic POD, and the only
  legacy-status projection.
- **S04 mathematical conventions:** `KinematicConventions.hpp/.cpp` freezes
  the SE(3) joint equation, `q_model = q_input + zeroPositionOffset`, units
  (m/rad), proper-rotation checks, and stable axis-tilt tangent coordinates.
  These helpers have no Widget or WorkCell dependency.
- **S05 frozen requirement boundary:** `EngineeringRequirementArtifactAdapter`
  consumes only the frozen v4 `artifact.execution` contract.  It rejects a v3
  artifact for Verified evaluation with `REQ_V3_REQUIRES_REFREEZE`, verifies
  provenance/source path and fingerprints, and leaves the destination problem
  unchanged on failure.
- **S07 JSON safety:** non-finite values are written as JSON `null`; an
  unavailable candidate total score additionally writes
  `totalScoreAvailability: "Unavailable"`.  Unknown root fields and
  `extensions` round-trip unchanged; unknown enum values are rejected.  The
  valid `Continuous` variable domain remains accepted after a regression test.
- **Verification:** the fresh StructureOptimizer executable passed all default
  suites (including `contracts`, `kinematic_conventions`, `json_safety`,
  `json_roundtrip`, `frozen_adapter`, `frozen_requirements`, and
  `evaluator_consistency`) with exit code 0 and `All tests passed.`  The
  cross-plugin executables also passed:
  `sdurws_robotanalysiscore_test.exe requirementExecution`,
  `sdurws_engineeringrequirements_test.exe`, and
  `sdurws_kinematicanalysis_test.exe configuration`.
- **Known non-failing warning:** optional `fixture.stl` lookup warnings appear
  in the full StructureOptimizer suite after the relevant assertions pass.
- `git diff --check` reported no whitespace errors.  GUI invocations used the
  Windows platform plugin and one absolute test executable per command.

Phase 0 is closed.  Phase 1 may add only the canonical core model and its
focused tests; it must not switch UI, optimization algorithms, or the
CandidateCompiler.

## Phase 1 / S10 evidence (2026-08-18)

- Added the core-only `CanonicalKinematicModel` POD and validator.  The model
  separates frames, joints, DOFs, chains, and TCP bindings; it stores full
  `Transform3D` values and explicit axes, not DH, RPY, or nominal lengths.
- Stable validation diagnostics cover duplicate frame IDs, fixed/movable DOF
  ownership, continuous unique Q indices, unit/type agreement, connected
  chains, and Flange-to-Tool bindings.  The legal fixture contains Revolute,
  Fixed, Prismatic, and Tool elements without assuming a six-axis device.
- RED evidence: the first test build failed because
  `CanonicalKinematicModel.hpp` did not exist.  A later focused RED test for
  duplicate movable `dofId` failed with the expected missing
  `KINEMATIC_MOVABLE_JOINT_DOF_DUPLICATE` diagnostic.
- GREEN evidence: after each minimal implementation step,
  `scripts/build-msvc-debug.cmd sdurws_structureoptimizer_test` succeeded and
  the absolute executable's `canonical_model` suite exited 0.  The adjacent
  `kinematic_conventions` suite exited 0; `frozen_requirements` also completed
  successfully (with the pre-existing optional `fixture.stl` warning).
- A fresh default GUI-suite launch progressed through its existing tests but
  exceeded the interactive tool window, so its exact test process was stopped;
  it is not recorded as a full-suite pass.  No Qt platform-plugin error was
  reported.

## Phase 1 / S11 evidence (2026-08-18)

- Added `CanonicalForwardKinematics.hpp/.cpp`, a read-only evaluator over the
  validated active device chain.  It delegates each joint to the frozen S04
  convention and applies ToolBinding after the Flange transform; it does not
  clamp Q or apply operational limits.
- RED evidence: the first focused test build failed because the FK header was
  absent.  GREEN evidence: the final target build succeeded and the absolute
  executable's `canonical_fk` suite exited 0.  It verifies q=0 and a nonzero
  Q with zero offsets, arbitrary fixed transforms, a prismatic axis in metres,
  TCP composition, unavailable-frame diagnostics, and Q-dimension rejection.
- Adjacent `canonical_model` and `kinematic_conventions` suites each exited 0
  after the FK build.

## Phase 1 / S12 evidence (2026-08-18)

- Added `KinematicModelImporter`, `KinematicImportResult`, immutable import
  provenance, and per-item source mappings.  The only accepted vertical input
  is an explicitly selected WorkCell, SerialDevice, and TCP; the importer never
  chooses the first device or TCP.  A supplied `RobotModelSpec` is copied as an
  audit snapshot only and is not used to construct canonical kinematics.
- The importer preserves the full WorkCell-world-to-device-base fixed path,
  followed by FixedFrame, Revolute, and Prismatic chain members.  It maps
  active DOFs contiguously in source order, assigns radians/metres by joint
  type, carries finite physical limits, invokes canonical validation before
  success, and retains no borrowed source pointers.
- Stable import diagnostics cover missing or non-member source selection,
  TCP-not-in-chain/tip, broken chains, duplicate IDs, unsupported joints or
  frames, invalid limits, and base-tree failures.  Each diagnostic carries a
  stable code, source object ID, and source field path.
- RED evidence: the importer suite initially failed because
  `KinematicModelImporter.hpp` did not exist.  The snapshot retention contract
  was then added test-first; its RED build failed because `sourceSnapshot` and
  result snapshot fields did not exist.  The non-zero Base FK comparison also
  failed before the world-to-base prefix was imported.
- GREEN evidence: after the final Debug target build, the absolute executable's
  `canonical_importer` suite exited 0.  It covers explicit null selection,
  multiple-device no-selection rejection, TCP chain membership, invalid limit,
  unsupported joint, source mapping/provenance, fixed-frame Q exclusion,
  prismatic metres, and q=0/non-zero FK comparisons for key frames.  Adjacent
  `canonical_model` and `canonical_fk` suites each exited 0.
- Fixture note: a duplicate-name WorkCell cannot be constructed safely with
  RobWork `StateStructure` (the framework does not return during teardown), so
  duplicate canonical-ID behaviour remains covered at the canonical validator
  boundary.  Supported RobWork Revolute/Prismatic sources expose the fixed
  non-zero local Z axis; the importer's zero-axis guard is retained for any
  future source extension that exposes arbitrary axes.

## Phase 1 / S13 evidence (2026-08-18)

- Importer/FK equivalence compares canonical world-frame transforms directly
  against `rw::kinematics::Kinematics::frameTframe`, never against RPY strings.
  Position and rotation are independently represented by the full SE(3)
  comparison at a `1e-12` tolerance.
- The fixture matrix covers a six-Revolute device, a Revolute/Fixed/Prismatic
  chain, non-zero WorkCell Base and TCP transforms, non-zero RPY (including
  pitch), Pos-Y offsets, and rotated joint offsets.  Three deterministic six-Q
  configurations compare every joint frame and TCP; the mixed chain compares
  all key frames at q=0 and non-zero Q.  DOF order and radian/metre units are
  asserted at import time.
- RED evidence: the first non-zero Base world-to-TCP assertion failed because
  the importer began its canonical chain at the device Base.  GREEN evidence:
  it now imports the WorkCell root and fixed ancestors before the device chain;
  the focused `canonical_importer` suite exits 0.  Adjacent `canonical_model`
  and `canonical_fk` suites also exit 0 after the final build.

## Phase 1 / S14 evidence (2026-08-18)

- Added `DhProjection` as an explicitly read-only compatibility view.  It has
  `Exact`, `Lossy`, and `Unsupported` status, row-level conventional-DH
  parameters, lost-component identifiers, and stable diagnostics; there is no
  conversion or write-back API and the canonical model has no DH truth fields.
- Its exact domain is deliberately narrow: Z-axis joint motion, a parent Z
  rotation/Z translation, and a child X translation/X rotation.  Pitch,
  lateral translations, and other rotations are reported as Lossy; a tilted
  canonical motion axis is Unsupported.  Projection never mutates source
  SE(3) data.
- RED evidence: the focused test failed because `DhProjection.hpp` did not
  exist.  GREEN evidence: after adding the core target source, the absolute
  executable's `dh_projection` suite exited 0 for exact, pitch-lossy,
  Pos-Y-lossy, axis-unsupported, and non-mutation cases.  Adjacent
  `canonical_model` and `canonical_fk` suites each exited 0.

## Phase 1 / S15 evidence (2026-08-19)

- Added `KinematicFingerprint` and `KinematicBaselineSnapshot`.  The
  versioned, locale-independent canonical serialization uses a fixed `fnv1a-64`
  algorithm and rejects every non-finite numerical value before hashing.  It
  never hashes memory addresses, pointer values, Qt containers, or file
  timestamps.
- Fingerprint domains are intentionally separate: `forModel` contains the
  canonical frame/joint/DOF/chain semantics; `forTool` contains TCP SE(3) and
  visual-geometry binding identifiers; `forEnvironment` contains the external
  environment fingerprint and collision-binding identifiers.  Display-only
  colours are explicitly outside the canonical model and do not affect any
  fingerprint.  The snapshot records the schema, serialization version,
  algorithm ID, and all three fingerprints alongside a copy of the nominal
  canonical model.
- RED evidence: the first snapshot test failed because the header/API did not
  exist; the next build exposed the intended `KinematicBaselineSnapshot::create`
  API contract.  A further RED build required separate tool/environment
  fingerprint entry points and a final RED build required serialization-version
  provenance.
- GREEN evidence: a fresh Debug build succeeded and the absolute executable's
  `kinematic_fingerprint`, `canonical_model`, `canonical_fk`,
  `canonical_importer`, and `dh_projection` QCoreApplication suites all exited
  0.  The fingerprint suite covers insertion-order independence, repeated
  invocation stability, both SE(3) transforms, axis, physical/operational
  limits, Q mapping, TCP/geometry/collision separation, NaN/Inf rejection,
  provenance, and nominal-model snapshot recovery.
- Review closure: the canonical validator now rejects empty or duplicate tool
  binding IDs (`KINEMATIC_TOOL_BINDING_ID_DUPLICATE`), which gives the
  tool/environment serializers a deterministic ordering key.  A final RED test
  also proved that a NaN in a ToolBinding previously escaped the model and
  environment hashes; shared finite-value preflight now rejects it in all
  three fingerprint domains before any cache key is generated.

## Phase 1 / S16 evidence (2026-08-19)

- `StructureOptimizationProblem` now owns an optional `CanonicalModelShadow`.
  Legacy projects keep the default `CanonicalModelMissing` state and continue
  through the unchanged candidate evaluator/scorer path.  The shadow has no
  design-variable, candidate-result, or legacy-evaluator write-back API.
- Project JSON persists a complete, validated `KinematicBaselineSnapshot`:
  snapshot provenance, all three fingerprints, frame/joint/DOF/chain/tool
  topology, limits, axes, ordered Q mapping, binding IDs, and full SE(3)
  translation/rotation matrices.  Stable textual enum values are used; malformed
  matrices, non-finite values, unknown enums, and invalid canonical topology
  are rejected on load.
- `CanonicalModelShadowService` attaches snapshots only through the explicit
  WorkCell/SerialDevice/TCP importer, and compares a newly imported source to
  the persisted snapshot as `Current`, `Stale`, or `Invalid`.  New overloads in
  `StructureOptimizationProjectFactory` and `StructureOptimizationProjectAdapter`
  create and load projects with this explicit source boundary; the normal legacy
  overloads deliberately remain non-importing.
- RED evidence: full-model round-trip assertions first failed because only the
  three fingerprint strings were saved.  A shadow-status test then failed because
  the service header/API was absent; importer-backed creation and load-time
  staleness tests each failed at compile time before their overloads were added.
- GREEN evidence: a fresh Debug build succeeded.  The absolute test executable's
  QCoreApplication suites `canonical_shadow`, `kinematic_fingerprint`,
  `canonical_model`, `canonical_fk`, `canonical_importer`, and `dh_projection`
  each exited 0.  The tests cover full snapshot save/read/re-fingerprint,
  importer-created project fingerprint, legacy-project `CanonicalModelMissing`,
  source-fingerprint staleness, and invariant legacy candidate score/status/
  violations when a shadow is present.

Phase 1 is closed.  Phase 2 may introduce only the independent typed design-space
POD and registry/binding work; it must not switch the legacy candidate compiler or
evaluation pipeline until its designated later migration gates are complete.

## Phase 2 / S20 evidence (2026-08-19)

- Added independent core-only `DesignVariableDefinition` and `ParameterBinding`
  PODs.  Their compiler-facing semantic, target-object, and target-property
  identities are stable enums with explicit string conversions; `displayPath`
  is excluded from runtime equality.
- Validation rejects duplicate IDs, unknown semantics, non-finite numbers,
  invalid independent ranges/steps, missing discrete-option IDs, missing frames
  for pose semantics, derived variables without expression IDs, and bindings
  without typed target objects/properties.  Derived variables intentionally do
  not require an optimization range or step.
- RED evidence: the focused build first failed because `DesignVariable.hpp` was
  absent.  GREEN evidence: after adding the two core source files, a fresh Debug
  build and the absolute executable's QCoreApplication `design_variable` suite
  exited 0.  No legacy variable table-model or evaluator path was modified.

## Phase 2 / S21 evidence (2026-08-19)

- Added a pure `DesignSpaceRegistry` and `AdapterCapabilityQuery`.  The registry
  registers the complete first-phase semantic whitelist with explicit domain,
  unit, and applicability metadata; the capability table is keyed by typed
  target-object identity and never stores adapter pointers.
- The initial suggestion factory deliberately implements only capability-gated
  `JointZeroOffset`.  It creates variables for movable joints even at a zero
  nominal value, assigns radians to Revolute and metres to Prismatic joints,
  and excludes Fixed joints.  Geometry, base, TCP, flange, limits, and other
  registered semantics remain metadata-only until their planned template and
  compiler tasks; no adapter apply path, UI, WorkCell, legacy table model, or
  legacy evaluator was changed.
- RED evidence: the focused `design_registry` suite first failed because the
  registry omitted part of the first-phase whitelist.  GREEN evidence: after
  completing only those metadata registrations, a fresh Debug build succeeded
  and the absolute executable's QCoreApplication `design_registry`,
  `design_variable`, and `canonical_model` suites each exited 0.

## Phase 2 / S22 evidence (2026-08-19)

- Preserved the existing `StructureOptimizationTemplate` objective-weight API
  (`balanced`, `reachability-first`, `compactness-first`, and
  `workspace-first`) and added a distinct, versioned core-only design-intent
  catalog: `KinematicBasic`, `KinematicWithJointAxis`,
  `KinematicWithBaseTcp`, and `FullKinematicDesign`.
- Added `DesignTemplateApplication::preview`.  It is non-mutating and returns
  `TemplateApplicationPreview` with `toAdd`, `alreadyPresent`, `conflicts`,
  `inapplicable`, `disabled`, and diagnostics.  It filters only registry
  suggestions backed by declared capabilities, sorts additions deterministically,
  preserves an existing user-edited variable, and reports unavailable template
  semantics as `BindingUnavailable` rather than inventing a binding.
- The first new suggestion expansion supports capability-gated joint-axis U/V
  offsets.  Their zero nominal/current value is still a valid suggestion; no
  numerical non-zero check is used as a proxy for parameterizability.
- RED evidence: the initial focused build failed because
  `DesignTemplateApplication.hpp` did not exist.  GREEN evidence: a fresh
  Debug build and the absolute executable's QCoreApplication `design_template`,
  `design_registry`, and `design_variable` suites exited 0.  A complete Widget
  regression was then launched once with `QT_QPA_PLATFORM=windows`; it exited
  0 with `All tests passed.`  No UI code, WorkCell, legacy objective preset
  behavior, or candidate compiler path was changed.

## Phase 2 / S23 evidence (2026-08-19)

- Added `ParameterizationModeRegistry` with stable first-phase alternatives
  for link placement, joint origin, and TCP/Flange pose ownership.  The pure
  resolver applies a single explicit selection per group and preserves every
  unselected variable as `DisabledByParameterization` with a stable serialized
  reason; it does not silently delete variables.
- Added `WriteSetValidator`.  Only active variables participate; every active
  variable requires a declared binding, derived writers require a declared
  owner, and two active bindings claiming the same typed object/property are
  rejected with `PARAMETER_WRITE_CONFLICT`.  Read sets are intentionally not
  treated as conflicts, and adapter execution order has no role in resolution.
- RED evidence: the focused build first failed because
  `ParameterizationMode.hpp` and `WriteSetValidator.hpp` did not exist.  A
  test invocation initially used a non-stable short mode name and was rejected
  as `PARAMETERIZATION_SELECTION_INVALID`; the test now uses the registered
  stable ID `JointOriginMode=AlongAxis`.  GREEN evidence: a fresh Debug build
  and the absolute QCoreApplication `parameterization` suite exited 0, followed
  by `design_variable`, `design_registry`, and `design_template` suites each
  exiting 0.  No UI, WorkCell, or legacy candidate compiler was changed.

## Phase 2 / S24 evidence (2026-08-19)

- Added a pure, typed `DerivedExpression`/`DependencyGraph` evaluator.  It
  accepts only Constant, VariableRef, Add, Subtract, Multiply, Divide, Min,
  Max, Clamp, Norm, and explicitly registered functions; the first registered
  function is unary `abs`.  There is no script, UI, WorkCell, or runtime code
  injection path.
- Expressions read only the supplied resolved-value map and other expressions.
  Map-ordered DFS yields deterministic topological order.  Every failure
  (duplicate ID, missing reference, direct/indirect cycle, operand arity,
  unit mismatch, zero division, unsupported propagation, or non-finite output)
  returns diagnostics and an empty derived-value set.
- Unit rules retain a physical unit when multiplied or divided by Unitless,
  require matching units for additive, Clamp, and Norm inputs, and reject
  unsupported combinations.  `DerivedExpressionTargetValidator` also rejects
  `JointLimitLower` and `JointLimitUpper` as derived targets: those are
  constraints to be checked by the compiler, not overwrite expressions.
- RED evidence: initial builds failed because the two expression/DAG headers
  were absent; Clamp/Norm tests then failed before their implementation, and
  the registered-function test failed before `registeredFunctionId` existed.
  GREEN evidence: a fresh Debug build and the absolute QCoreApplication
  `derived_expression` suite exited 0, followed by `parameterization` and
  `design_template` suites exiting 0.  No legacy candidate compiler or UI was
  modified.

## Phase 2 / S25 evidence (2026-08-19)

- Added the pure `DesignSpaceCompiler` and `CompiledDesignSpace` core boundary.
  The compiler is the sole Phase-2 preflight resolver for the supplied canonical
  model, semantic registry, adapter-capability declaration, variables, typed
  bindings, parameterization selections, and derived expressions.  It has no
  Widget, WorkCell mutation, candidate generation, or legacy-evaluator
  dependency.
- Successful output contains deterministically sorted independent and derived
  variables, active typed bindings, variable groups, selected modes, derived
  dependency order, a stable independent-only vector schema, disabled reasons,
  diagnostics, and a versioned content fingerprint.  The fingerprint includes
  the canonical-model fingerprint, capability declaration, vector-relevant
  variable data, bindings, groups, modes, dependency order, and disabled-state
  reasons.  UI-only names and display paths are excluded.
- Preflight blocks invalid canonical models, missing inputs, invalid/duplicate
  bindings, unbound or semantic-mismatched active variables, unregistered or
  unit/domain-mismatched semantics, parameterization conflicts, duplicate
  writers, missing/orphan/cyclic derived expressions, and derived-result unit
  mismatches.  Disabled variables remain in `disabledReasons` and receive an
  informational `DESIGN_SPACE_VARIABLE_DISABLED` diagnostic, but are absent
  from both bindings and the search vector.
- RED evidence: the expanded `design_space_compiler` test did not compile until
  capability and derived-expression inputs were added to the request boundary.
  GREEN evidence: a fresh Debug build succeeded and the absolute executable's
  QCoreApplication `design_space_compiler`, `derived_expression`,
  `parameterization`, and `design_variable` suites each exited 0.  The S25
  suite covers equivalent top-level input orders, stable schema indices,
  independent-only vectors, disabled-variable diagnostics, write conflict,
  unbound variable, derived unit mismatch and cycle blocking, and capability
  changes that alter the design-space fingerprint.  The legacy generator/UI
  remains deliberately untouched until its later migration gate.

## Phase 2 / S26 evidence (2026-08-19)

- Added the pure `DesignVector` codec.  Its schema-positioned independent
  values have an explicit `CompiledDesignSpace` fingerprint, engineering-unit
  representation, canonical byte sequence, and fixed `fnv1a-64` value.  The
  byte form uses fixed field ordering and IEEE-754 bit encoding, normalizes
  `-0.0` to `+0.0`, and never formats engineering values through a locale.
- `fromNormalized`, `fromEngineering`, and `toNormalized` strictly implement
  the continuous mapping `x = min + u * (max - min)` and its inverse.  They do
  not clamp out-of-range values or round misaligned integer steps.  Discrete
  variables carry only a declared stable option ID; their numeric placeholder
  is required to be canonical zero so ignored numeric input cannot alter or
  ambiguate a candidate.
- The codec independently verifies the supplied compiled schema: exact length,
  index/order, variable ID, unit, fingerprint, finite numeric input, valid
  bounds, integer alignment, and non-empty/unique discrete option IDs.  It
  therefore accepts only independent variables in the canonical schema; derived
  variables and disabled entries cannot enter a `DesignVector`.
- RED evidence: the initial focused build failed because `DesignVector.hpp` was
  absent.  Subsequent RED cases demonstrated that default numeric fields for a
  valid discrete variable, non-zero discrete placeholders, and duplicate option
  IDs needed explicit codec handling.  GREEN evidence: a fresh Debug build and
  the absolute executable's QCoreApplication `design_vector`,
  `design_space_compiler`, `derived_expression`, `parameterization`, and
  `design_variable` suites each exited 0.  The vector suite covers normalized
  and engineering round trips, bounds, integer steps, stable discrete IDs,
  schema/length errors, canonical byte/hash equality, `-0.0`, non-finite input,
  and independent-only membership.

## Phase 2 / S27 evidence (2026-08-19)

- Added a pure, read-only `LegacyDesignSpaceAdapter::preview`.  It consumes a
  const legacy `StructureOptimizationProblem`, returns per-variable migration
  entries plus separately mapped canonical variables/bindings, and never writes
  project state or the legacy JSON representation.
- `BaseHeight` with a target maps directly to `BaseTz`/`BasePlacementAdapter`.
  The old model has no unambiguous `LinkLength` kind, so a LinkLength mapping
  requires an explicit typed `LegacyDesignSpaceBindingHint`; labels and target
  strings alone are never guessed.  Original legacy variables are copied into
  every entry, preserving the legacy range, unit, domain, preference, and other
  compatibility fields.  Mapped values are converted to canonical m/rad units
  only when the conversion is explicit and finite.
- DH A/D variables are preserved solely as `legacy/projection-only` entries
  (`LEGACY_DH_PROJECTION_ONLY`); missing, invalid, unsupported, non-finite, or
  unit-incompatible bindings become disabled `legacy/unbound` entries with a
  stable diagnostic.  There is no fallback to the first target, no DH primary
  variable, and no mutation of the legacy project.  Repeating the preview with
  the same inputs yields the same entry and binding identities.
- RED evidence: the focused test initially failed because
  `LegacyDesignSpaceAdapter.hpp` did not exist.  GREEN evidence: a fresh Debug
  build and the absolute executable's QCoreApplication `legacy_design_space`,
  `design_vector`, `design_space_compiler`, `derived_expression`,
  `parameterization`, and `design_variable` suites each exited 0.  The migration
  test covers explicit LinkLength mapping, BaseHeight → BaseTz, DH projection
  diagnostics, safe unbound handling, legacy mm range/unit preservation,
  non-mutation, and idempotent repeated preview.

## Phase 3 / S30 evidence (2026-08-19)

- Added the pure `IModelParameterAdapter` contract, explicitly owned
  `AdapterRegistry`, typed data-only `CandidatePatch`, and adapter diagnostic
  helper.  Adapters receive a borrowed immutable canonical baseline and return
  a patch; this boundary holds no Widget or mutable WorkCell and does not wire
  the legacy optimizer or evaluator.
- Registration rejects absent adapters, empty IDs, non-positive versions,
  unknown/empty semantic declarations, and duplicate IDs.  Semantic queries
  and fingerprint material are deterministic (`std::map` registry order and
  sorted declared semantics/capabilities), with adapter version included.
- Before adapter dispatch, the Registry always runs the common
  `ParameterBindingValidator`, then adapter-specific validation, requires
  declared non-empty read/write sets, and capability-gates the binding target.
  A patch must identify the registered adapter/version and binding; every
  typed write must target a declared write-set entry with finite scalar or
  non-empty textual value.  Undeclared writes are blocked.
- Adapter diagnostics from validation, compile-result, and nested patch paths
  retain their supplied data but receive any missing binding, object, and field
  context.  Any merged Error diagnostic makes compilation fail, so a nominal
  `ok=true` result cannot leak a half-valid candidate onward.
- RED evidence: the focused test initially failed for the absent patch header;
  subsequent focused RED cases exposed bypassable generic binding validation,
  missing diagnostic context, validation-diagnostic loss, and Error diagnostics
  in both compile-result and patch channels.  GREEN evidence: a fresh Debug
  build succeeded and the QCoreApplication `adapter_registry`,
  `design_variable`, `design_registry`, `design_template`, `parameterization`,
  `derived_expression`, `design_space_compiler`, `design_vector`, and
  `legacy_design_space` suites each exited 0.  This slice intentionally leaves
  all real-variable, CandidateCompiler, evaluator, optimizer, and UI switching
  for later planned tasks.

## Phase 3 / S31 evidence (2026-08-19)

- Added pure `JointOriginAdapter` and `ParameterizedLinkAdapter` implementations.
  They compile only typed `CandidatePatch` data against a borrowed immutable
  canonical baseline; no generic patch applier, RobotModelSpec projection,
  WorkCell mutation, visual/collision artifact, evaluator, optimizer, or UI
  wiring was introduced.  Generic patch merge and atomic model commit remain
  the later S36 responsibility.
- Joint-origin Cartesian X/Y/Z semantics generate the exact three
  `ParentToJointTranslation*` writes from baseline translation plus the selected
  offset.  `JointOffsetAlongAxis` rotates the finite, normalized baseline
  `motionAxisInJoint` into the parent frame before applying its baseline-relative
  delta; zero or non-finite axes are rejected rather than becoming a no-op or
  silently scaling physical distance.
- LinkLength requires a finite unit `referenceDirection` and an explicit
  reference-direction frame equal to the target joint's parent frame.  It uses
  the immutable baseline projection as nominal length and computes
  `p_new = p_baseline + (requested - nominal) * direction`; lengths at or below
  `1e-6 m` are rejected.  No label/name inference, world-axis default, or
  geometry-center substitution is available.
- Read/write declarations are compared as exact unordered target sets (with
  duplicates rejected), primary translation properties are semantic-specific,
  and all resolved input is exactly one finite metre-valued scalar.  Therefore
  the two adapters cannot apply a patch to undeclared or semantically unrelated
  canonical properties.  DesignSpace's existing writer-conflict validation
  blocks simultaneous LinkLength and JointOrigin use on the same translation
  targets.
- Adapter compatibility is now fingerprinted through a borrowed trusted
  `AdapterRegistry`, never a caller-supplied fingerprint string.  Bindings carry
  a positive `ownerAdapterVersion`; common binding validation and direct
  Registry compilation both require it to match the registered adapter before
  adapter validation/patch construction.  Reference direction and owner version
  participate in binding equality and compiled-design-space identity.
- RED evidence: the S31 suite initially failed because `JointOriginAdapter.hpp`
  did not exist.  Subsequent RED tests caught absent trusted registry inputs,
  unnormalized/non-finite axes, ordered-set comparisons, semantic/property
  mismatches, version mismatches, and Error-prone binding metadata.  A Phase-2
  integration run also caught the legacy typed LinkLength fixture missing its
  now-mandatory explicit +X/base direction; only that fixture metadata was
  completed, with no production fallback added.  GREEN evidence: a fresh Debug
  build and the QCoreApplication `joint_origin_link_adapter`, `adapter_registry`,
  `design_variable`, `design_registry`, `design_template`, `parameterization`,
  `derived_expression`, `design_space_compiler`, `design_vector`,
  `legacy_design_space`, `canonical_fk`, and `canonical_model` suites each
  exited 0.  The S31 test proves Cartesian X/Y/Z, parent-frame AlongAxis,
  explicit LinkLength direction, lower-length rejection, deterministic
  recompilation, baseline non-mutation, and expected FK displacement by
  applying the emitted writes only to a test-local canonical clone.

## Phase 3 / S32 evidence (2026-08-19)

- Added the pure `JointAxisAdapter`.  It consumes a borrowed canonical baseline
  and emits only one typed `MotionAxisTiltU` or `MotionAxisTiltV` scalar write
  for the binding being compiled.  It never writes zero offset or either
  parent/child installation transform, and it does not directly mutate a
  canonical model.  S36 remains responsible for grouped patch application to
  `motionAxisInJoint`.
- The adapter uses the frozen `KinematicConventions::tiltedAxis` basis and
  formula.  A pair of resolved U/V values is accepted only when both are finite
  radians, have concrete U/V semantics, and belong to the same canonical
  `axis-tilt:<jointId>` group as the binding.  The full deflection is checked
  as `rho = hypot(U, V)`, rather than by per-component clipping or the folded
  post-rotation angle; this is the actual cone coordinate.
- Axis tilt is valid only for Revolute and Prismatic joints with a finite,
  nonzero nominal axis.  The cone is explicit binding metadata, is finite in
  `[0, pi]`, and is fingerprinted.  The exact `rho == maxAxisTiltAngle` boundary
  is accepted, while larger, periodic-folded (for example `2*pi`), non-finite,
  or cross-joint group inputs are rejected.  Bindings also require the canonical
  group identity, preventing a forged shared label from mixing U from one joint
  with V from another.
- Updated the frozen helper so `tiltedAxis` uses `hypot` and returns the nominal
  axis only for exact zero deflection; a finite small nonzero U/V is no longer
  silently erased by a tolerance branch.  This keeps S04 math and adapter patch
  semantics identical and avoids squared-component overflow in cone evaluation.
- RED evidence: the focused test initially failed because `JointAxisAdapter.hpp`
  was absent.  Follow-up RED cases exposed missing U/V group identity, periodic
  cone folding, forged cross-joint group IDs, tiny nonzero tilt suppression, and
  cones above `pi`.  GREEN evidence: a fresh Debug build and the QCoreApplication
  `joint_axis_adapter`, `joint_origin_link_adapter`, `adapter_registry`,
  `design_variable`, `design_registry`, `design_template`, `parameterization`,
  `derived_expression`, `design_space_compiler`, `design_vector`,
  `legacy_design_space`, `kinematic_conventions`, `canonical_fk`, and
  `canonical_model` suites each exited 0.  The axis suite covers zero and
  positive/negative U/V, actual rho, small nonzero tilt, deterministic basis,
  exact/beyond cone boundaries, unit final axis in a test-local clone, baseline
  immutability, Revolute/Prismatic support, and Fixed rejection.

## Phase 3 / S33 evidence (2026-08-19)

- Added pure `JointZeroAdapter` and `JointLimitAdapter` implementations.  Both
  consume a borrowed immutable canonical baseline and emit only typed
  `CandidatePatch` data; neither mutates a live WorkCell nor introduces the
  later S36 patch applier, evaluator, optimizer, or UI wiring.
- Joint-zero patches own only `ZeroPositionOffset`.  Revolute coordinates are
  radians and prismatic coordinates are metres; Fixed joints are rejected.  The
  frozen equation remains `q_model = q_input + zeroPositionOffset`, so changing
  zero position never rotates the motion axis or installation transforms.
- Physical and operational lower/upper limits are distinct typed targets with
  an explicit lower/upper group, scope, minimum range, absolute envelope, and
  q-coordinate convention.  Physical limit edits require explicit binding
  authorization and are not suggested by default.  Operational patches set
  `affectsStructuralCapability = false`, and operational bounds cannot exceed
  enabled physical bounds.
- The canonical model now records an explicit `QInput` or `QModel` convention
  for enabled limits.  The importer records `QInput`, JSON and fingerprints
  retain the convention, and validation rejects non-finite, unordered,
  wrong-unit, invalid-convention, or Fixed-joint limits.  Operational-vs-
  physical checks convert bounds with the frozen zero offset before comparison;
  this prevents a nonzero offset from silently allowing a range outside the
  physical envelope.
- `JointZeroOffset` is no longer generated by the default registry or Full
  Kinematic template.  It remains available only through an explicit binding
  when a Home, assembly, or encoder-zero requirement supplies the necessary
  design intent.
- RED evidence: the focused suite first failed because `JointZeroAdapter.hpp`
  was absent.  Follow-up RED cases exposed absent scope/group metadata,
  physical-lock bypasses, q-coordinate ambiguity, non-finite and wrong-unit
  baseline limits, default zero suggestions, cross-convention bounds, and
  forged unknown coordinate enums.  GREEN evidence: a fresh Debug build
  succeeded, and the QCoreApplication `joint_zero_limit_adapter`,
  `adapter_registry`, `design_variable`, `design_registry`, `design_template`,
  `parameterization`, `derived_expression`, `design_space_compiler`,
  `design_vector`, `legacy_design_space`, `joint_axis_adapter`,
  `joint_origin_link_adapter`, `kinematic_conventions`, `canonical_model`,
  `canonical_importer`, `kinematic_fingerprint`, and `canonical_fk` suites
  each exited 0.  `git diff --check` reported no whitespace errors (only the
  existing CRLF conversion warnings).

## Phase 3 / S34 evidence (2026-08-19)

- Added pure `BasePlacementAdapter`, `FlangePoseAdapter`, and `TcpPoseAdapter`
  implementations, plus frozen pose-delta metadata.  They accept a borrowed
  immutable canonical baseline and emit typed `CandidatePatch` data only; no
  generic patch applier, geometry/collision rebuild, evaluator, optimizer, or
  UI integration was added before its planned stage.
- Pose deltas use the explicit right-multiplied rotation-vector convention
  `T_next = T_baseline * Exp(delta)`.  Euler angles are not stored as canonical
  state.  Translation bindings must name the same coordinate frame in the
  variable and binding, preventing a declared-frame/patch-frame mismatch.
- Base variables are restricted to the canonical base frame in the immutable
  system root frame and describe `SystemPlacement`; the adapter has no task or
  environment write target.  Flange variables require exactly one inbound,
  Fixed mount edge on the active device chain.  TCP variables target only a
  valid Flange-to-Tool `ToolBinding` and require tool-pose, parameterized
  geometry, and parameterized collision capabilities, so TCP-only reachability
  improvements are blocked until matching artifacts can be produced.
- Flange and TCP semantic metadata now have distinct applicability categories.
  All three adapters enforce exact typed read/write sets, SI/radian values,
  deterministic pose groups, trusted adapter versions, and generic binding
  validation even when called directly rather than through `AdapterRegistry`.
  This prevents malformed direct bindings and ambiguous side-branch Flange
  topology from creating patches.
- RED evidence: the focused suite first failed because `BasePlacementAdapter.hpp`
  was absent.  Follow-up RED cases exposed missing declared-frame checks,
  incorrect Flange applicability metadata, bypassable direct binding validation,
  and an inactive Fixed side branch being mistaken for an independent Flange.
  GREEN evidence: a fresh Debug build succeeded, and the QCoreApplication
  `base_flange_tcp_adapter`, `joint_zero_limit_adapter`, `joint_axis_adapter`,
  `joint_origin_link_adapter`, `adapter_registry`, `design_variable`,
  `design_registry`, `design_template`, `parameterization`, `derived_expression`,
  `design_space_compiler`, `design_vector`, `legacy_design_space`,
  `kinematic_conventions`, `canonical_model`, `canonical_importer`,
  `kinematic_fingerprint`, and `canonical_fk` suites each exited 0.  `git diff
  --check` reported no whitespace errors (only the existing CRLF conversion
  warnings).

## Phase 3 / S36 evidence (2026-08-20)

- Added pure `CandidatePatchMerger` and `CandidatePatchApplier` boundaries.
  The merger keeps typed writes in deterministic target order, accepts
  idempotent duplicate writes, rejects conflicting values and pose groups,
  aggregates diagnostics, and sorts/deduplicates generated artifacts and
  derived-value IDs.  The applier copies the canonical baseline, applies only
  explicit typed target handlers, requires paired U/V axis coordinates, and
  returns no candidate model when a target, capability, grouped operation, or
  post-apply canonical validation fails.
- Supported S36 application targets cover joint translation, axis tilt,
  zero-offset, physical/operational limits, base/flange/TCP translation and
  right-multiplied rotation-vector deltas, owned visual/collision dimensions,
  and owned mesh artifact references.  The input baseline remains unchanged.
  No `RobotModelSpec`, WorkCell, UI, legacy optimizer, evaluator, or candidate
  runtime path was connected.
- RED evidence: focused assertions first failed for stable derived-artifact
  ordering, merged patch diagnostics, and missing axis U/V sibling rejection.
  GREEN evidence: a fresh VS x64/MSVC Debug build via
  `scripts/build-msvc-debug.cmd sdurws_structureoptimizer_test` succeeded.
  With `QT_QPA_PLATFORM=windows`, the absolute test executable exited 0 for
  `candidate_patch_merge_apply`, `adapter_registry`,
  `parameterized_geometry_collision_adapter`, `canonical_model`,
  `canonical_fk`, `design_space_compiler`, `design_vector`,
  `base_flange_tcp_adapter`, `joint_zero_limit_adapter`, and
  `legacy_design_space`.
- `git diff --check` reported no whitespace errors; only the repository's
  existing LF-to-CRLF conversion warnings were emitted.  The next planned
  slice is S37 `CandidateCompiler`; S36 remains isolated from the legacy
  execution path.

## Phase 3 / S37 evidence (2026-08-20)

- Added pure canonical `CandidateCompiler` and `CompiledCandidate`.  The
  compiler validates the canonical baseline and the schema-positioned,
  fingerprinted `DesignVector`, evaluates the expressions retained in
  `CompiledDesignSpace`, resolves independent and derived values by binding
  group, calls the trusted `AdapterRegistry`, merges patches, and atomically
  publishes the copied/validated canonical model.
- `CompiledCandidate` records compile status, candidate ID, design vector,
  canonical model, derived values, generated artifact fingerprints,
  diagnostics, and a deterministic fingerprint.  Compile failures retain no
  partial candidate model.  Baseline input is never mutated.
- Strengthened the S36 pose merge boundary so independent Base/TCP/Flange
  pose groups can coexist; a pose-group conflict is reported only when one
  typed target is claimed by different groups.  S37 also rejects tampered
  vector canonical bytes/fingerprints and adapter `ok == false` results.
- RED evidence: the initial S37 build failed on the missing compiler header;
  the first real combined-adapter run exposed the overly global S36 pose
  group conflict.  GREEN evidence: a fresh VS x64/MSVC Debug build via
  `scripts/build-msvc-debug.cmd sdurws_structureoptimizer_test` succeeded.
  With `QT_QPA_PLATFORM=windows`, the absolute test executable exited 0 for
  `candidate_compiler`, `candidate_patch_merge_apply`, `adapter_registry`,
  `canonical_model`, `canonical_fk`, `design_space_compiler`,
  `design_vector`, `base_flange_tcp_adapter`, `joint_zero_limit_adapter`,
  and `legacy_design_space`.  The S37 test covers a real LinkLength + Axis
  U/V + Base + TCP combination, repeated compile equality, derived values,
  schema/fingerprint rejection, atomic failure, and baseline immutability.
- S37 remains isolated from `RobotModelSpec`, WorkCell, UI, legacy optimizer,
  and legacy evaluator.  S38 owns evaluation-device projection.

## Phase 3 / S38 evidence (2026-08-20)

- Added `RobotModelSpecProjectionAdapter` as a read-only canonical-to-output
  projection.  It emits explicit SE(3) `transformJoints`, preserves
  Revolute/Prismatic/Fixed/Tool semantics, leaves DH projection empty, maps
  canonical limits and visual/collision bindings, and emits a canonical zero Q.
- Added `EvaluationDeviceBuilder` that projects into a copied worker
  `RobotModelSpec`, invokes the existing isolated `CandidateModelFactory`, and
  returns no artifact on projection/build failure.  The live WorkCell and
  State are never borrowed or mutated.
- Updated the XML writer to preserve explicit ToolFrame rows rather than
  appending a zero TCP fallback, and fixed the legacy factory include guard
  collision exposed by the new public builder header.
- RED evidence: S38 initially failed at the missing projection API, then the
  first runtime fixture exposed an invalid ToolBinding and the Windows loader's
  qualified TCP name; both are now diagnosed/handled explicitly.  GREEN
  evidence: fresh VS x64/MSVC Debug build succeeded.  With
  `QT_QPA_PLATFORM=windows`, the absolute executable exited 0 for
  `s38_projection`, `candidate_compiler`, `canonical_fk`, and
  `canonical_model`.  `git diff --check` completed successfully.

## Phase 4 / S40, S43, S44 evidence (2026-08-21)

- Added `EvaluationPlan` and `EvaluationPlanCompiler` as an execution-only
  projection of `RequirementExecutionSet`. The compiler preserves Must,
  Should, and Info semantics, rejects unsupported v3 Verified regions, checks
  model/environment fingerprints and evaluator capabilities before execution,
  rejects unknown metrics and unsafe region sampling, and emits a deterministic
  plan fingerprint. It does not read UI state or invoke an evaluator.
- Added cancellable `EvaluationPipeline`/`EvaluationStage` contracts. Stages
  execute in registration order, retain version and completion facts, report
  missing capabilities as `DataInsufficient`, and stop cleanly at a cooperative
  cancellation point. A malformed plan fails before any stage runs.
- Added `MetricRegistry`/`MetricResult` with explicit availability states and
  producer/unit/direction/capability metadata. The standard registry includes
  task, workspace, region, joint-margin, Jacobian, and collision metrics;
  missing evidence is never represented as numeric zero.
- RED evidence: `StructureOptimizationTest.cpp` first failed to compile because
  `EvaluationPlan.hpp` was absent. GREEN evidence: a fresh VS x64/MSVC Debug
  build succeeded, and the absolute Windows Qt test executable exited 0 for
  `evaluation_plan`, `evaluation_pipeline`, `candidate_compiler`,
  `s38_projection`, and `canonical_fk`. `git diff --check` reported no
  whitespace errors (only existing LF-to-CRLF conversion warnings).
- Added `ConstraintEvaluator` and `ObjectiveAggregator` boundaries. Constraint
  evidence availability is preserved, safety constraints cannot be silently
  softened, normalization rejects invalid ranges, and hard violations keep the
  aggregate infeasible regardless of objective score. The focused CTest suite
  `sdurws_structureoptimizer_constraint_objective_test` passes.

## Phase 4 / S45-S46 evidence (2026-08-21)

- Added `TaskEvaluationStage` as the task-level bridge from `EvaluationPlan` to
  the shared `TargetEvaluator`. It preserves Must/Should/Info evidence,
  residuals, candidate counts, representative Q, collision evidence, failure
  codes, deterministic first-candidate selection, and partial cancellation.
  Must aggregation keeps `DataInsufficient` distinct from `Infeasible`.
- Added `EstimatedWorkspaceStage`, `VerifiedRegionStage`, and
  `OrientationCoverageStage`. These stages only orchestrate existing
  `KinematicAnalyzer`, `RegionCoverageEvaluator`, and Fibonacci orientation
  helpers; no IK, FK, sampling, or collision implementation was duplicated in
  StructureOptimizer. Estimated evidence is kept separate from Verified
  region/orientation evidence, and cancellation/empty evidence is explicit.
- RED evidence: the S45/S46 test includes failed first builds for missing stage
  headers. GREEN evidence: fresh VS x64/MSVC Debug build succeeded; with
  `QT_QPA_PLATFORM=windows`, the absolute executable exited 0 for
  `task_evaluation_stage` and `spatial_evaluation_stages`.

## Phase 4 / S47 evidence (2026-08-21)

- Added `KinematicMetricAggregator` for evidence-only aggregation of raw and
  characteristic-length-normalized manipulability, joint-margin percentiles,
  collision rate, and minimum distance. Percentiles use the existing lower
  percentile rule (`floor(p * (n - 1))`); source evidence IDs and sample
  denominators are retained. Empty and incomplete data remain explicit
  `InsufficientData`/`Partial` states, and unavailable minimum distance is not
  synthesized as zero.
- RED evidence: the initial S47 build failed on the missing aggregator header;
  GREEN evidence: fresh VS x64/MSVC Debug build succeeded and the absolute
  Windows Qt executable exited 0 for `kinematic_metric_aggregator`.

## Phase 4 / S48 evidence (2026-08-21)

- Strengthened `ConstraintEvaluator` with required evidence-stage checks,
  safety soft-constraint rejection, tolerance-aware Less/Greater/Equal/InRange
  comparisons, dimensionless normalized violations, and preserved priority.
  `ObjectiveAggregator` keeps hard violations and unavailable hard evidence
  infeasible; objective contributions remain separate from feasibility.
- Constraint aggregation now uses stable descending priority order. Existing
  fixed-weight compatibility fields remain untouched and no arbitrary
  expression strings were introduced.
- GREEN evidence: fresh VS x64/MSVC Debug build succeeded and the absolute
  Windows Qt executable exited 0 for `constraint_objective`.

## Phase 4 / S49 evidence (2026-08-21)

- Added `CandidateResult` and `CandidateResultAssembler` as the new result
  boundary. It retains lifecycle, feasibility, evidence stage, quality,
  completion facts, stage results, raw metrics, constraints, objectives,
  representative Q, diagnostics, and warnings. Compile failure maps to
  `Failed/NotEvaluated`; evaluation failure maps to `Failed/DataInsufficient`;
  cooperative cancellation maps to `Canceled/DataInsufficient`; legacy status
  is projected only through `toLegacy`.
- Evaluation failure always discards provisional feasibility, so incomplete
  evidence cannot be reported as `Feasible`. The focused assembly test covers
  retained evidence, failure separation, cancellation partials,
  DataInsufficient ranking, and legacy compatibility mapping.
- GREEN evidence: fresh VS x64/MSVC Debug build succeeded. With
  `QT_QPA_PLATFORM=windows`, the absolute executable's `candidate_result`
  suite exited 0, and the focused Phase 4 CTest set passed 7/7.

## Phase 5 / S50 evidence (2026-08-21)

- Added pure content-addressed `CacheKey` and in-memory `EvaluationCache`.
  The key includes model/environment/requirements/plan/design-space/
  design-vector/tool fingerprints, compiler/evaluator/solver IDs plus
  versions and configuration content, sampling method/seed/normalized plan,
  numeric tolerances, evidence stage, and numeric policy. It rejects missing
  identity fields and non-finite/negative tolerances, never uses pointers,
  addresses, or timestamps, and keeps cache identity separate from candidate
  fingerprints.
- Sampling-plan order is normalized before serialization; Quick and Verified
  stages are distinct key inputs. Disk persistence is intentionally absent
  until the memory cache contract is proven.
- RED evidence: the first S50 test build failed because `CacheKey.hpp` was
  absent. GREEN evidence: fresh VS x64/MSVC Debug build succeeded; the absolute
  Windows Qt executable exited 0 for `cache_key`; the focused Phase 4 plus S50
  CTest set passed 8/8; `git diff --check` reported no whitespace errors
  (only existing LF-to-CRLF conversion warnings).

## Phase 5 / S53-S54 evidence (2026-08-21)

- S53 added the pure `QuickScreeningPolicy`. Deterministic compile/model/
  geometry/collision failures are rejected; low-sample, partial, canceled, and
  missing evidence remain `Uncertain`; clear feasibility is promoted, while a
  Quick-only candidate can never be final-best eligible.
- S54 added pure `EliteSelector` and one-round `HybridOptimizer` skeletons.
  Elite selection is feasibility/evidence-first, compares objective
  contributions, ranks infeasible candidates by hard normalized violations,
  applies normalized design-space diversity, preserves stable-index ties, and
  enforces an explicit uncertain quota. The hybrid round evaluates the initial
  pool in Quick, promotes only selected elites to Verified, honors a total
  evaluation budget, checks cancellation between batches, and exposes a best
  candidate only when it is `Feasible + Verified`; local search and global
  optimality claims remain out of scope.
- RED evidence: the S54 test first failed to compile because the new selector
  headers were absent. GREEN evidence: a fresh VS x64/MSVC Debug build via
  `scripts/build-msvc-debug.cmd sdurws_structureoptimizer_test` succeeded.
  With `QT_QPA_PLATFORM=windows`, the absolute test executable exited 0 for
  `elite_selector` and `hybrid_optimizer`.
- Follow-up S54 audit added regression coverage for multiple violated hard
  constraints and cancellation immediately after the Quick batch. Elite
  ranking now preserves the existing descending constraint-priority contract
  by representing the highest-priority violated hard constraint; Hybrid still
  refuses to start Verified after a batch-boundary cancellation. A rebuild
  through the same helper and fresh absolute-path launches exited 0 for both
  focused suites. The adjacent `quick_screening`, `cache_key`, and
  `initial_sampler` suites also exited 0.

## Phase 5 / S51 evidence (2026-08-21)

- Added deterministic `DeterministicSeed` and independent `InitialSampler`
  contracts. Candidate seeds derive from a run seed and stable candidate index
  with a fixed splitmix64-style transform; no global random state is used.
- Random, Latin Hypercube, and Grid generation consume the canonical design
  vector schema and emit `DesignVector` fingerprints through the existing
  codec. Baseline is always candidate index 0 at nominal values; duplicate
  vectors are removed; variable declaration order cannot change canonical
  candidate fingerprints; integer values are quantized to their declared step;
  discrete values use stable option IDs; Grid combination overflow is rejected
  before generation with a diagnostic.
- RED evidence: the first S51 build failed because `DeterministicSeed.hpp` was
  absent. A follow-up run exposed test assumptions about schema ordering and
  Grid preflight; those were corrected, and the assertion dialog process was
  stopped before relinking. GREEN evidence: fresh VS x64/MSVC Debug build
  succeeded; the absolute Windows Qt executable exited 0 for `initial_sampler`;
  the focused S40-S51 CTest set passed 9/9; `git diff --check` reported no
  whitespace errors (only existing LF-to-CRLF conversion warnings).
