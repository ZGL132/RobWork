# StructureOptimizer Plugin

## Overview

The StructureOptimizer plugin for RobWorkStudio enables mechanical arm structure dimension optimization. It helps design engineers quickly find optimal arm configurations based on task requirements, hard constraints, and comprehensive engineering metrics.

## Architecture

The plugin is split into two layers:

- **Core library** (`sdurws_structureoptimizer_core`): Pure computational components with no UI dependency.
- **Plugin** (`sdurws_structureoptimizer`): RobWorkStudio plugin with Qt Widgets UI.

## Component Overview

- `StructureOptimizationTypes` - All data structures (enums, configs, results)
- `StructureOptimizationValidation` - Problem validation with specific error codes
- `StructureDesignMutator` - Applies design variables to create candidate model specs
- `StructureObjectiveScorer` - Hard constraints, normalized scoring, stable sorting
- `CandidateModelFactory` - Builds isolated temporary WorkCells for candidates
- `StructureCandidateEvaluator` - IK, manipulability, collision, and workspace evaluation
- `StructureCandidateGenerator` - Random, Grid, and Latin Hypercube candidate generation
- `StructureCandidateCache` - Quantized-value keyed evaluation cache
- `HybridStructureOptimizer` - Global sampling + elite verification + local refinement + final verification
- `StructureSensitivityAnalyzer` - Per-variable +/-step sensitivity analysis
- `StructureOptimizationJson` / `StructureOptimizationCsv` - Export formats and audit CSV
- `StructureCandidateExporter` - Candidate model XML packaging

## Scoring Formula

Total score (range [0, 100]) = Σ(weight_i × score_i) × 100

Six components with fixed weights:
- Reachability (0.35) - weighted task point coverage
- Manipulability (0.20) - P10 of usable IK solution manipulability
- Joint margin (0.15) - P10 minimum joint limit margin
- Collision (0.15) - collision-free solution rate
- Compactness (0.10) - total kinematic length (inverse)
- Preference (0.05) - engineering preference fit

Hard constraints can make a candidate infeasible regardless of soft scores.

## Optimization Strategies

- **Random**: Uniform random sampling with quantization
- **Grid**: Cartesian product of per-variable step values
- **Hybrid** (default): Latin Hypercube global sampling → Quick evaluation → diverse elite Verified re-evaluation → local refinement from `localEliteCount` centers → final Verified re-evaluation of `finalVerificationCount` leaders → sensitivity of the verified best candidate

## Determinism

With a fixed random seed, all strategies produce identical results across runs.

## Cancellation and Pause

The optimization respects cancellation and pause requests between candidate evaluations.

## Exported Files

- Project JSON: Complete optimization problem definition
- Result JSON: Problem + candidate results + sensitivity analysis
- Candidate CSV: Per-candidate summary with all metrics
- Task detail CSV: Per-task IK results for top candidates
- Audit CSV: Quick, Verified, final-verified, cache, and sensitivity counters
- Candidate XML package: Complete RobWork model files for the selected candidate

When a workspace coverage box is enabled, Quick and Verified evaluations sample
the workspace using their respective configurations. Coverage and automatic
sensitivity are included in the Markdown evidence report. The report also
states that trajectory, dynamics, and drive-selection evaluators are not enabled.

## Model Provenance

`.rmb.json` is the parameterized design source. A project created from one embeds
an immutable `RobotModelSpec` snapshot plus the source path, source fingerprint,
and snapshot fingerprint. The optimizer always evaluates that embedded snapshot.

- `Current`: source and snapshot fingerprints match.
- `Stale`: source content changed; create a new project to optimize the new design.
- `SourceMissing` / `SourceInvalid`: the source cannot be used for comparison.
- `Untracked`: legacy or manually created project without complete provenance.

All statuses are non-blocking because they preserve historical reproducibility.
Reports and `audit.csv` contain the provenance fields and a `Tracked` or
`Untracked` audit classification. JOG continues to load its WorkCell runtime
model independently of this optimization-project check.

## Error Codes

| Code | Description |
|------|-------------|
| StructureOptimization.Context.Invalid | Incomplete optimization context |
| StructureOptimization.Variable.NoneEnabled | No enabled design variables |
| StructureOptimization.Variable.DuplicateId | Duplicate variable IDs |
| StructureOptimization.Variable.InvalidBounds | Variable out of bounds |
| StructureOptimization.Variable.MixedKinematicsSource | Cannot mix DH and Transform variables |
| StructureOptimization.Task.NoneEnabled | No enabled task points |
| StructureOptimization.Weights.Invalid | Invalid weight configuration |
| StructureOptimization.Run.InvalidCounts | Invalid candidate/elite counts |
| StructureOptimization.Workspace.InvalidGrid | Invalid workspace coverage grid |

## Known Limitations

- Fixed degrees of freedom (DOF topology is not optimized)
- No motor/gear selection or dynamics optimization
- Single-worker only (no parallel candidate evaluation)
- No evolutionary or multi-objective algorithms

## Kinematic Optimization Workflow

1. Open a `*.structure-optimization.json` project created from a complete
   `RobotDesignContext`. A bare WorkCell is intentionally not reverse-engineered
   into a model specification.
2. Edit variables, task points, constraints, strategy, all run settings, and
   objective weights. The start command is disabled whenever the snapshot fails
   validation.
3. Run the optimization, select a feasible candidate by its stable candidate
   index, and preview it. Preview files are created in a temporary directory;
   clearing the preview or closing the plugin restores the original WorkCell.
4. Export produces `project.structure-optimization.json`,
   `result.structure-optimization.json`, `candidates.csv`,
   `task-details.csv`, `audit.csv`, and `report.md`. If a feasible candidate is selected,
    its XML package is exported under `candidate-<index>/`.

### Phase 1 Guided Workflow

The setup bar supports the repeatable engineering sequence:

1. Choose and apply one of the templates: `balanced`, `reachability-first`,
   `compactness-first`, or `workspace-first`.
2. Run preflight. `Fail` findings block optimization, while `Warning` findings
   are recorded but allow sampling to continue.
3. Evaluate the current model baseline independently before starting a search.
4. Run optimization, then select up to three candidates and compare their score,
   reachability, manipulability, joint-margin, collision, and length deltas
   against the baseline.

The first phase evaluates structure dimensions and kinematics only. Trajectory,
dynamics, motor, and reducer evaluators remain explicit extension points and are
not enabled by these templates.

## Accepted Example

`RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR-6-85-5-A.structure-optimization.json`
is a fixed, portable acceptance example. It embeds a UR-6-85-5-A model snapshot,
three required TCP poses, three structure variables, a `2 x 2 x 2` coverage grid,
and seed `20260727`. Its intentionally small sample counts are for automated
acceptance; engineering projects should use representative candidate and workspace
sample counts.

## Build Verification

On Windows, the normal PowerShell session does not include the MSVC and Windows
SDK library paths. In particular, Boost.Thread transitively links the Windows
SDK `synchronization.lib`; building without the Visual Studio developer
environment therefore fails with `LNK1104`.

Use `scripts\\build-msvc-debug.cmd` for every build target. It reuses an
already configured `VSDEVCMD`, or locates a Visual Studio installation with
`vswhere`, before initializing the x64 environment.

```powershell
.\scripts\build-msvc-debug.cmd sdurws_robotmodelbuilder_jsontest
.\scripts\build-msvc-debug.cmd sdurws_robotanalysiscore_test
.\scripts\build-msvc-debug.cmd sdurws_kinematicanalysis_test
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test

ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_(robotmodelbuilder_jsontest|robotanalysiscore_test|kinematicanalysis_test|structureoptimizer_test)" --output-on-failure
```

Verified on 2026-07-27 with CMake 4.3.1, Ninja, MSVC 14.42.34433, Qt 6.11.1
(`msvc2022_64`), and vcpkg `x64-windows`. The filtered CTest gate selected nine
tests (the robot-analysis-core target registers six cases) and passed all nine
in 10.01 seconds.
