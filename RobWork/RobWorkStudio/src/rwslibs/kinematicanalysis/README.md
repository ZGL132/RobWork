# KinematicAnalysis Plugin

KinematicAnalysis is a RobWorkStudio plugin for early robot design validation. It answers whether a robot reaches a task pose, how many IK options are usable, whether a configuration is close to joint limits or singularities, and how the sampled workspace behaves.

## Scope

- Current pose analysis: forward kinematics, TCP pose, joint values, joint-limit margins, Jacobian, singular values, condition number, manipulability, and warnings.
- IK analysis: solve a target pose, list candidate solutions, rank by collision state, target residual, joint-limit margin, manipulability, and distance to the current configuration.
- Task point reachability: import or edit task points, batch analyze IK reachability, and report pass/warning/fail status with failure reasons.
- Workspace sampling: deterministic random or capped grid joint-space sampling with seed control, TCP point, manipulability, condition number, joint-limit margin, collision flag, and status. Results link to Visualization tab.
- Pose reachability: sample tool directions at positions, report orientation coverage, show planned IK target count before running, and run in background with cooperative cancellation.
- Report export: JSON and CSV summaries for downstream review.

## Metrics

- Joint-limit margin is normalized as the smaller distance to lower or upper limit divided by joint range.
- Condition number is the largest singular value divided by the smallest singular value. A near-zero smallest singular value is treated as infinite condition.
- Manipulability is the product of the Jacobian singular values.
- Pose reachability coverage is `reachableDirections / sampledDirections`.
- Task point reachable rate counts `Pass` and `Warning` as reachable and excludes disabled task points.

## Default Thresholds

- Near joint-limit ratio: `0.05`
- Singular value warning: `1e-4`
- Condition warning: `100`
- Condition fail: `1000`
- Manipulability warning: `1e-5`
- Position tolerance: `0.001 m`
- Orientation tolerance: `1 deg`

These values can be changed in the Report tab. Re-run analyses after applying new thresholds.

## Plugin Boundaries

- Jog remains an interactive motion/control plugin. KinematicAnalysis reads the current RobWorkStudio state but does not replace Jog.
- RobotModelBuilder creates and edits robot models. KinematicAnalysis consumes the loaded WorkCell and selected device.
- Dynamics, drive selection, and trajectory validation should be separate plugins. They can consume exported task points, selected IK solutions, and KinematicAnalysisResult summaries, but should not depend on KinematicAnalysis UI classes.

## IK Page Layout

The IK tab is split into a left input panel and a right results panel. The input panel holds the target name, units, pose spin boxes, filter checkboxes, and action buttons (Import current TCP pose, Solve, Apply selected Q). The results panel shows the status summary, a multi-column solution table, and a selected-candidate details table.

- IK analysis distinguishes deterministic numerical candidates from usable unique solutions. The result table can show diagnostic failed candidates, while the summary reports how many candidates are usable.
- Count summary rows: Raw candidates, Unique candidates, Usable unique, Pass, Warning, Fail (updated each Solve and on filter changes).
- Filters: `Show usable only` and `Show failed candidates` toggle which solution rows are visible. Selecting a row updates the details table below it.
- `Failure` and `Q` are separate table columns so long joint vectors no longer hide diagnostic reasons.
- `Duplicate Q threshold` controls joint-space IK candidate de-duplication. Increase it to merge tiny numerical variations around singular configurations.

## Workspace Page Layout

The Workspace tab samples the selected device joint space using deterministic random sampling or capped grid sampling. The page shows the first 500 rows in the table and keeps the complete sample set for CSV export, report aggregation, and the Visualization tab.

Controls:
- `Samples`: maximum generated sample count (clamped to [0, 1000000]).
- `Mode`: `Random uniform` samples inside joint limits; `Grid` enumerates a joint-limit grid and caps the output by `Samples`.
- `Grid steps`: per-joint grid resolution used only in `Grid` mode (clamped to [1, 100]).
- `Seed`: deterministic random seed used by `Random uniform`. Same seed + same configuration produces identical results.
- `Collision`: enables collision checks when a collision detector is available.
- `Color`: preferred scalar field when opening the data in the Visualization tab.
- `Open in Visualization`: switches to the Visualization tab, selects Workspace source, and applies the Color scalar mode.

The summary label reports total/shown/pass/warning/fail/collision-free counts, average and P10 manipulability, and maximum finite condition number. The diagnostic label below shows the planned sample count versus the theoretical grid count (with a "capped" note when applicable).

The 9-column table includes Index, Status, Collision, TCP x/y/z, Manipulability, Condition number, and Min joint-limit margin.

CSV export includes a comment-line summary (`# workspace_summary,...`) followed by one row per sample. Downstream scripts that skip comment lines still work with the existing column headers.

## Pose Reachability Page Layout

The Pose reachability tab samples tool Z-axis directions (using Fibonacci spiral) and roll angles at each spatial position, then runs IK for each orientation to measure directional coverage.

Controls:
- `Source`: pick positions from Task points or manual rows.
- `Directions`: number of directions sampled on the unit sphere (clamped to [0, 1000]).
- `Rolls`: number of rotations around the tool Z-axis per direction (clamped to [1, 360]).
- `Collision`: enables collision checks when a collision detector is available.
- `Run`: starts the analysis. The Run button is disabled while running; a Cancel button appears to request early stop (cooperative cancellation, current position completes before stopping).
- `Export CSV`: enabled only when results exist.

The diagnostic label shows the planned IK target count (`positions × directions × rolls`) before running, including a "(capped)" note when the total exceeds 1,000,000.

The summary label reports total/sampled/reachable counts, pass/warning/fail distribution, average coverage, and min/max coverage.

The analysis runs on a background thread (`QtConcurrent`) so the RobWorkStudio window remains responsive during long runs. CSV export includes a comment-line summary (`# pose_reachability_summary,...`) followed by one row per sample.

## Known Limitations

- Workspace grid sampling is capped by sample count to avoid combinatorial growth on high-DOF robots.
- IK coverage uses deterministic multi-seed numerical solving; it is repeatable for the same target/state but does not guarantee complete analytical branch enumeration.
- Collision results depend on the collision models available in the loaded WorkCell.
