# KinematicAnalysis Plugin

KinematicAnalysis is a RobWorkStudio plugin for early robot design validation. It answers whether a robot reaches a task pose, how many IK options are usable, whether a configuration is close to joint limits or singularities, and how the sampled workspace behaves.

## Scope

- Current pose analysis: forward kinematics, TCP pose, joint values, joint-limit margins, Jacobian, singular values, condition number, manipulability, and warnings.
- IK analysis: solve a target pose, list candidate solutions, rank by collision state, target residual, joint-limit margin, manipulability, and distance to the current configuration.
- Task point reachability: import or edit task points, batch analyze IK reachability, and report pass/warning/fail status with failure reasons.
- Workspace sampling: random or grid joint-space sampling with TCP point, manipulability, joint-limit margin, condition number, collision flag, and status.
- Pose reachability: sample tool directions at positions and report orientation coverage.
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

## Known Limitations

- Workspace and pose reachability are currently table/export oriented. Dense 3D point-cloud and heat-map visualization can be added later.
- IK coverage uses deterministic multi-seed numerical solving; it is repeatable for the same target/state but does not guarantee complete analytical branch enumeration.
- Collision results depend on the collision models available in the loaded WorkCell.
- Workspace grid sampling is capped by sample count to avoid combinatorial growth on high-DOF robots.
