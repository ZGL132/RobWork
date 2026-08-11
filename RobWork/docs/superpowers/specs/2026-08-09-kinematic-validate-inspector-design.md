# Kinematic Validate Inspector Design

## Scope

Optimize only `RobWorkStudio/src/rwslibs/kinematicanalysis`.

The Validate workflow becomes a frozen-requirements-first validation inspector. It retains all
current Local Tasks and Frozen Requirements behavior, including local editing, single-item and
full validation, region coverage, report export, requirement provenance, and disabled-state
handling. Jog and every other plugin remain out of scope.

## Goals

- Make Frozen Requirements the default source because it is the Verified validation path.
- Present one source-specific command strip instead of duplicate or hidden command controls.
- Make the validation result that needs attention visible first.
- Keep compact task and region summaries readable in a narrow dock.
- Move low-frequency evidence to a collapsed Advanced diagnostics section.
- Remove only obsolete UI widgets and duplicate presentation code, not validation behavior.

## Information Architecture

The page is a vertically scrollable native workflow tab with this order:

1. **Validation source**: `Frozen Requirements` by default; `Local Tasks` remains available for
   editable quick analysis. The selected source shows its current readiness state.
2. **Command strip**: controls are source-specific and use the existing native button style.
3. **Validation summary**: a single line reporting source, evidence stage, Must outcome, and
   counts by outcome. Before an execution it reads `Not validated`.
4. **Result summaries**: compact `Key station tasks` and `Demand regions` tables remain separate.
   This avoids combining task residuals with region coverage into an ambiguous generic table.
5. **Validation inspector**: the selected task or region owns the detailed conclusion and evidence.
6. **Advanced diagnostics**: last and collapsed by default; it contains full provenance,
   orientation-probe details, and region-cell results.

## Source-Specific Commands

For Frozen Requirements, show `Load JSON`, `Validate selected`, `Validate all`, and `Report`.
`Load JSON` stays available when no artifact is loaded. `Validate selected` and `Validate all`
remain disabled until a valid v4 execution contract and WorkCell, Device, and TCP are available.

For Local Tasks, show `Add`, `Remove`, `Validate selected`, `Validate all`, and `Report`.
The existing editable task-point model, selection handling, CSV-related actions, and task detail
panel remain available. Local validation remains Quick analysis, not a substitute for Verified
frozen-requirement validation.

The retired hidden `validateLoadRequirementsButton`, `validateRunButton`, and
`validateExportButton` presentation widgets are removed. Their existing load, validate, and report
operations remain reachable from the unified source-specific command strip.

## Results And Selection

Both result tables remain read-only for Frozen Requirements and retain their current stable-ID
selection behavior. The task table keeps task identity, residual/result summary, feasibility,
quality, evidence stage, and level. The region table keeps level, position coverage, orientation
coverage, feasibility, and quality.

After a full validation, default selection is the first non-passing Must item, considering both
tasks and regions in stable execution order. If every Must item passes, select the first result.
After a selected-only validation, retain that validated item as the selection. Switching filters or
source may not change the stored execution result; it only changes the presentation.

The inspector displays selected-item-owned data:

- common: type, ID/name, level, feasibility, quality, evidence stage, failure reason;
- task: position/orientation residual and reachable solution summary;
- region: position/orientation coverage, sampled cell count, and direction/roll configuration.

No current-state or unrelated IK candidate diagnostics appear in Validate.

## Diagnostics And Errors

Advanced diagnostics is collapsed initially. It exposes the complete frozen-artifact provenance,
the orientation probe for the selected region, and the existing per-cell region table. Local Tasks
does not fabricate frozen provenance or region-cell details.

All current error behavior is retained: v3 artifacts remain rejected with
`REQ_V3_REQUIRES_REFREEZE`; absent WorkCell, Device, or TCP disable unavailable actions and show
an actionable status; collision requirements remain item-specific; partial selected validation
continues to replace the visible/report subset with the selected stable item.

## Cleanup Boundaries

Remove only duplicate Validate controls, their obsolete layout branches, and presentation-only
state that exists solely for those controls. Preserve public slots, serialization, validation
algorithms, report construction, task-point editing, region coverage evaluation, and unrelated
Explore/Diagnose interfaces.

## Verification

Extend the kinematic workflow UI tests to cover:

- Frozen Requirements is the initial source and shows only its command set.
- Local Tasks switches to its editable command set without losing model behavior.
- Unified controls preserve full and selected validation for task and region stable IDs.
- The summary and inspector follow the default or explicitly selected result.
- Advanced diagnostics starts collapsed and retains provenance, orientation, and cell data.
- v3 rejection, readiness disabling, report export, and narrow-width layout keep their current
  behavior.

Build and run the complete `sdurws_kinematicanalysis_test` suite in Debug and Release. Run
`git diff --check` and confirm no plugin outside `kinematicanalysis` changed.
