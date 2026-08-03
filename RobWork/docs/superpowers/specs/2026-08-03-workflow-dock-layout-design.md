# Workflow Dock Layout Design

## Goal

Provide a predictable plugin workspace for the engineering workflow: compact, consistently placed docks; a fixed left-side tab order; a permanently available right-side Jog panel; and a model-readiness gate that prevents analysis workflow navigation before RobotModelBuilder has successfully published and loaded a robot model.

## Scope

The workflow docks are:

1. EngineeringRequirements
2. RobotModelBuilder
3. KinematicAnalysis
4. StructureOptimizer
5. Jog

Existing non-workflow docks retain their current behavior. The layout controller must not change their placement, visibility, or persisted settings.

## Layout

The four left workflow docks are tabified in this exact order:

```text
EngineeringRequirements -> RobotModelBuilder -> KinematicAnalysis -> StructureOptimizer
```

Their tab bar is positioned at the top of the left dock area. Jog is a separate dock in the right dock area and is not tabified with the workflow docks.

All five workflow docks use one canonical initial width. The controller derives that width from the pre-layout default dock width and applies a 50 percent reduction, with a minimum width that keeps controls usable. The same calculated width is applied to the left tab group and Jog.

The controller uses a layout-version setting. On the first launch after this feature is introduced, it overrides the previously restored arrangement for these five docks and writes the new version. On later launches, it preserves user-resized widths but reapplies the required left/right placement, top tab position, and left-tab order.

## Readiness Gate

When a project has no validated published robot model, RobotModelBuilder is raised automatically. EngineeringRequirements, KinematicAnalysis, and StructureOptimizer remain visible as tabs but cannot be selected. Their corresponding Plugins-menu visibility actions are disabled so the restriction cannot be bypassed through the menu. Jog remains visible and enabled throughout.

The gate opens only when RobotModelBuilder's Save and Load flow has successfully completed all of these operations:

1. Generated robot outputs are published without an error or rollback.
2. The generated scene is promoted or loaded as the active WorkCell.
3. The active project records a valid managed robot-model resource when a project context exists.

Generating a preview, saving XML without loading it, importing a draft, or a failed Save and Load operation does not open the gate.

The controller recomputes readiness when the active project or WorkCell changes. A reopened project is unlocked only if its managed robot-model resource and active WorkCell validate as a previously published builder model; otherwise it returns to the locked RobotModelBuilder state.

## Architecture

A focused workflow dock-layout controller lives in the RobWorkStudio main-window layer. It owns layout application, tab selection prevention, menu-action enablement, and readiness transitions. It locates the five existing `RobWorkStudioPlugin` instances by their stable plugin names and deliberately has no dependency on their widget internals.

RobotModelBuilderPlugin publishes an explicit model-ready notification after its existing Save and Load success path finishes. The main window connects that notification to the controller. Project/WorkCell lifecycle callbacks drive readiness revalidation, keeping the state correct for new projects, failed publishes, loaded projects, and closed WorkCells.

## Error Handling

- A missing optional plugin is ignored: remaining workflow docks are laid out without a crash.
- Failed model publishing leaves the gate locked and raises RobotModelBuilder.
- A malformed or stale managed resource is treated as not ready and does not unlock the analysis tabs.
- Persisted Qt main-window state cannot override the required workflow dock region, order, or tab position after the controller runs.

## Verification

Qt tests must cover:

1. The initial left tab order and top tab position.
2. Jog's right-side placement and continued enabled state before readiness.
3. Equal initial widths for the workflow dock group and Jog, at half the legacy default width.
4. The locked state: RobotModelBuilder selected; the other left tabs and their menu actions disabled.
5. Failed Save and Load and non-publish actions leave the gate locked.
6. A successful Save and Load unlocks all three downstream tabs and menu actions.
7. Reopening a validated published project restores the unlocked state; an invalid or missing model keeps it locked.
