# Structure Optimization Phase 2 Design

## Goal

Extend the structure optimizer for engineering-scale runs with deterministic
parallel candidate evaluation, resumable checkpoints, Pareto-front analysis,
and robustness analysis, while preserving the Phase 1 scope of structure
dimensions and kinematic performance.

## Scope And Non-Goals

Phase 2 covers:

- Parallel evaluation of independent candidates.
- Checkpoint creation and resume after interruption.
- Pareto analysis over kinematic and structure metrics.
- Robustness analysis around selected candidates.
- UI controls, result interpretation, JSON/report export, and focused tests.

It does not implement trajectory evaluation, dynamics, motor selection, reducer
selection, or a new evolutionary optimizer. The existing weighted score and
`bestCandidateIndex` remain valid and are not replaced by Pareto selection.

## Design Decisions

### Deterministic Parallelism

The optimizer first creates the complete candidate pool using the existing
strategy and random seed. Independent candidate evaluations run in a bounded
worker pool. Results are merged by stable candidate index, not completion order.
Baseline evaluation, elite selection, local refinement, final verification,
sensitivity, and final sorting remain deterministic control-flow steps.

The run configuration gains a parallel worker limit and an enable flag. Automatic
mode selects a conservative limit based on hardware concurrency; a manual limit
is clamped to a positive safe range. Cancellation is cooperative and propagated
to every evaluation callback.

### Checkpoints And Resume

A checkpoint is an atomic JSON document written after each completed evaluation
batch and at stage transitions. It contains:

- schema, optimizer, evaluator, and checkpoint format versions;
- problem/model/evaluator fingerprints and random seed;
- immutable candidate pool and stable candidate indices;
- completed candidate results and current stage cursor;
- baseline, diagnostics, sensitivity/Pareto/robustness progress;
- cancellation and completion state.

Resume is accepted only when the current problem fingerprint, evaluator version,
optimizer version, and candidate-generation configuration match the checkpoint.
Otherwise the UI reports a specific incompatibility and requires a new run.
Writes use a temporary file followed by rename so an interrupted write cannot
replace the last valid checkpoint. Existing project JSON remains backward
compatible; checkpoints are separate run artifacts.

### Pareto Analysis

Pareto analysis considers feasible, verified candidates by default. Objectives
are normalized into maximization directions:

- reachability, manipulability, joint margin, and collision-free rate: maximize;
- total kinematic length, base height, and cross-section: minimize;
- workspace coverage is included when configured and data is sufficient.

A candidate is dominated when another candidate is no worse in every selected
objective and strictly better in at least one. Stable candidate index ordering is
used for ties. The analysis returns Pareto candidate indices, dominated indices,
objective values, and the selected objective profile. It never changes weighted
ranking or hard-constraint semantics.

### Robustness Analysis

Robustness runs around a baseline or selected candidate using enabled variable
steps and configurable samples per variable. Perturbations are clamped to valid
bounds and evaluated through the same verified evaluator and cache. The result
records feasible rate, per-metric mean/min/max, standard deviation, maximum score
drop, constraint failure counts, and an A-D grade. A candidate with no valid
perturbation samples receives `Unknown` rather than an optimistic grade.

The existing one-step sensitivity result remains available. Phase 2 adds a
separate robustness result so existing reports and consumers do not change
meaning.

## Data Flow

```text
Problem + RunConfig
        |
        v
Deterministic candidate pool + run fingerprint
        |
        +--> checkpoint (pool, cursor, completed results)
        |
Bounded parallel evaluator --> index-ordered merge --> stage transitions
        |                                      |
        |                                      +--> weighted best candidate
        |                                      +--> Pareto analyzer
        |                                      +--> robustness analyzer
        v
Result JSON / CSV / Markdown report / UI summaries
```

## Component Boundaries

- `StructureParallelEvaluationRunner`: bounded worker scheduling, cancellation,
  deterministic merge, and progress reporting.
- `StructureOptimizationCheckpoint`: checkpoint schema, atomic persistence,
  fingerprint validation, and resume state.
- `StructureParetoAnalyzer`: objective extraction, dominance tests, and stable
  Pareto ordering.
- `StructureRobustnessAnalyzer`: perturbation generation, cached evaluation,
  statistics, and grading.
- `HybridStructureOptimizer`: orchestrates these components without moving model
  construction or evaluator ownership into the UI.
- `StructureOptimizationController`: exposes async start/resume/checkpoint state
  and preserves existing pause/cancel signals.
- `StructureOptimizerWidget`: exposes bounded controls and explanatory summaries;
  it does not calculate engineering metrics itself.

## Error Handling

- Invalid worker limits fall back to automatic mode and produce a warning.
- Checkpoint fingerprint/version mismatches block resume with an actionable
  diagnostic.
- A failed candidate remains recorded as failed and does not abort independent
  candidates; a global evaluator failure still terminates the run cleanly.
- Pareto analysis with no feasible verified candidates returns an empty result and
  an explanatory warning.
- Robustness analysis with no valid perturbations returns `Unknown` and preserves
  the original candidate result.

## Verification Strategy

Core tests must cover deterministic serial/parallel equivalence, cancellation,
checkpoint round-trip, atomic resume rejection on fingerprint mismatch, Pareto
dominance and tie ordering, robustness statistics and grading, and JSON/report
round-trips. Widget tests must cover worker controls, checkpoint state, resume
failure messaging, Pareto filtering, and robustness summary text. Windows Qt
tests use `QT_QPA_PLATFORM=windows` and launch one absolute-path executable at a
time.

## Future Extensions

The analyzer interfaces intentionally accept metric/objective identifiers rather
than hard-coding a future evaluator type. Trajectory, dynamics, motor, and
reducer evaluators can later contribute objectives and robustness metrics without
changing checkpoint or Pareto persistence contracts.
