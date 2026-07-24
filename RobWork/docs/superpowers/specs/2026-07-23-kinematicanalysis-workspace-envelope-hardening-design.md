# KinematicAnalysis Workspace Envelope Hardening Design

**Date:** 2026-07-23

**Status:** Approved for planning

## Goal

Make the Workspace envelope visualization responsive, internally consistent, and visually complete while preserving its intended meaning as an approximate outer envelope. Detailed point-level reachability and collision results remain the responsibility of Workspace samples and Scatter mode.

## Scope

This change addresses six reviewed problems:

1. Envelope computation currently blocks the Qt UI thread.
2. Right and bottom dimension annotations can be clipped in the widget and PNG export.
3. Device, TCP, source, and render-mode changes can leave stale or contradictory visualization state.
4. The current numerical result can be mistaken for an exact reachable region.
5. Non-finite or degenerate boundaries can be marked valid and passed to QPainter.
6. Existing tests do not cover the affected state transitions, invalid geometry, or rendering margins.

The change does not implement an exact multi-region workspace, inner holes, obstacle-aware envelope computation, or safety-certified reach limits.

## Product Semantics

The UI will call this mode **Approximate outer envelope**. It answers the question "what is the approximate outer extent of the selected device and TCP under the configured joint limits?"

The filled polygon must not be presented as proof that every point inside it is reachable. Workspace Scatter remains the detailed view for sampled reachability, manipulability, condition, joint margin, and collision results.

README text and the visualization summary will state this distinction. Collision geometry is not considered by the envelope calculation.

## Architecture

### Asynchronous Computation

`KinematicAnalysisWidget` will follow the existing Workspace and Pose Reachability `QtConcurrent::run` plus `QFutureWatcher` pattern.

An envelope request captures value snapshots needed by the worker:

- selected device pointer;
- selected TCP frame pointer;
- current state;
- projection;
- direction count and coordinate-iteration configuration;
- a monotonically increasing request generation;
- a shared cancellation flag.

The watcher result is a widget-level `WorkspaceEnvelopeRunResult` containing the envelope data, generation, cancelled state, and an error message. This keeps thread completion state explicit without adding Qt dependencies to the pure envelope-computation module.

The worker calls the non-widget `computeWorkspaceEnvelope()` function. The GUI thread displays a calculating state while keeping the previous valid image or a neutral placeholder. A newer request cancels or supersedes the previous request. The finished handler applies a result only when its generation still matches the latest request and the widget is alive.

The direction spin box will use a 200 ms single-shot debounce after `valueChanged`, so keyboard entry, stepping, and wheel changes share one behavior. Projection, Device, TCP, Source, and View changes request an immediate recomputation when Envelope is active.

### Cancellation

`WorkspaceEnvelopeConfig` will contain a shared atomic cancellation flag. The direction, seed, coordinate-iteration, and joint loops will check it regularly and return a non-valid result without publishing partial geometry. The widget-level worker result records that this invalid result was caused by cancellation.

Widget destruction and WorkCell changes set the flag before waiting for the watcher, matching existing background-analysis ownership rules.

### Result Cache

The widget will cache the most recent completed result by an immutable key containing:

- Device identity;
- TCP frame identity;
- projection;
- direction count;
- coordinate-iteration count;
- device Q bounds.

The implementation will use a single-entry cache. Any Device, TCP, WorkCell, or RobWorkStudio state change invalidates it conservatively. The cache exists to avoid recomputing when users switch tabs or refresh unchanged controls, not to retain a large result history.

### UI State Consistency

Render-mode changes will pass through `updateVisualizationControls()` rather than directly refreshing only the plot.

- Envelope is available only for Workspace source.
- Selecting another source forces the combo box back to Scatter.
- Envelope direction controls are enabled exactly when Workspace plus Envelope is active.
- Color, point filters, labels, point size, grid, and legend controls are disabled when they do not affect Envelope mode.
- Device and TCP changes invalidate the cache and request a new envelope when the visualization currently shows Envelope.

Signal blocking will prevent recursive combo-box updates.

### Geometry Validation

`updateEnvelopeDimensions()` will normalize and validate the boundary before marking it valid:

- discard non-finite points;
- require at least three finite points;
- remove adjacent duplicate points within a small numeric tolerance;
- require finite width and height above a small epsilon;
- require a non-zero polygon area;
- calculate dimensions and maximum radius only from retained finite points.

The renderer will still defensively skip non-finite points. A cancelled, invalid, or degenerate result renders a neutral status message instead of reaching QPainter polygon operations.

### Rendering Layout

Envelope rendering will use an envelope-specific plot-area helper rather than the Scatter margins.

Margins will be derived from `QFontMetrics` for:

- the top title;
- the horizontal width dimension;
- the right-side height dimension;
- the bottom top/side-view caption.

The height text will be rotated or placed in a dedicated right-side rectangle wholly inside the paint area. The same layout calculation will be used by `paintEvent()` and `renderToImage()`, so widget display and PNG export remain consistent. For very small areas, lower-priority captions may be elided, but the polygon and primary dimensions must remain inside the image.

### Algorithm Boundaries

The existing directional support search remains deterministic and FK-based. Increasing direction count improves angular outline resolution but does not make the joint-space optimizer globally exact. The UI and documentation will avoid the unqualified term "working envelope" where it implies exact reachability.

## Error Handling

- Missing Device, base, end, or TCP data produces an invalid result with a user-facing summary.
- Invalid or non-finite joint bounds produce an invalid result rather than NaN geometry.
- Cancelled and superseded calculations do not replace the current plot.
- Exceptions raised by background FK evaluation are caught at the worker boundary and converted to an error result/message delivered on the GUI thread.

## Testing

Tests will be added before production changes and must demonstrate the reviewed failures.

### Pure Logic Tests

- boundaries with fewer than three finite points are invalid;
- NaN and Inf points are removed and never remain drawable;
- collinear and zero-area boundaries are invalid;
- duplicate points are normalized;
- valid dimensions and radius remain correct;
- cancellation stops envelope computation;
- display text identifies the mode as an approximate outer envelope.

### UI Logic Tests

- Workspace plus Envelope enables direction controls;
- switching to Task points or Pose reachability forces Scatter;
- switching View updates the control state immediately;
- Device and TCP changes invalidate an active envelope request;
- stale asynchronous generations are ignored.

### Rendering Tests

Render representative envelope data to `QImage` at 320x220 and 1400x900. Verify that computed annotation rectangles and the plot rectangle stay inside the image. Pixel-perfect snapshots are not required; geometry assertions should remain stable across Qt and font versions.

### Verification

The existing `sdurws_kinematicanalysis_test.exe all` suite must pass. Manual RobWorkStudio verification will cover Device/TCP switching, rapid direction edits, projection changes, narrow plugin widths, and PNG export.

## Acceptance Criteria

1. Selecting Envelope does not block the UI while FK computation runs.
2. Rapid control changes never publish an older result over a newer request.
3. Envelope controls and actual render mode cannot disagree.
4. Device and TCP changes trigger the correct new result.
5. Invalid geometry never reaches QPainter as a valid polygon.
6. Dimension labels and captions stay inside both narrow widgets and exported PNGs.
7. The UI and README clearly describe the result as an approximate outer envelope.
8. Workspace samples are still not required for Envelope mode.
9. Scatter behavior and existing analysis results remain unchanged.
