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
- `HybridStructureOptimizer` - Global sampling + elite verification + local refinement
- `StructureSensitivityAnalyzer` - Per-variable +/-step sensitivity analysis
- `StructureOptimizationJson` / `StructureOptimizationCsv` - Export formats
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
- **Hybrid** (default): Latin Hypercube global sampling → Quick evaluation → Elite Verified re-evaluation → Coordinate local refinement

## Determinism

With a fixed random seed, all strategies produce identical results across runs.

## Cancellation and Pause

The optimization respects cancellation and pause requests between candidate evaluations.

## Exported Files

- Project JSON: Complete optimization problem definition
- Result JSON: Problem + candidate results + sensitivity analysis
- Candidate CSV: Per-candidate summary with all metrics
- Task detail CSV: Per-task IK results for top candidates
- Candidate XML package: Complete RobWork model files for the selected candidate

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
