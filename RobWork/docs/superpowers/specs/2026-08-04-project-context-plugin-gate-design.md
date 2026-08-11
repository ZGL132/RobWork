# Project Context Plugin Gate Design

## Goal

Keep pure-view and general-purpose plugins available at all times, while
preventing plugins that operate on robot or WorkCell data from being used until
an `.rwproj` project is active.

## Scope

The gate applies to user-facing plugin entry points and dock panels. It does
not unload plugins, change plugin discovery, or alter standalone WorkCell
loading outside an established project.

## Plugin Contract

`RobWorkStudioPlugin` exposes a non-virtual `requiresProjectContext()` query
and a protected `setRequiresProjectContext(bool)` declaration method. The
method stores its flag as a QObject dynamic property and defaults to `false`.
A business plugin whose commands require an active project calls the protected
setter from its constructor.

The contract deliberately does not add a virtual function or an instance data
member to `RobWorkStudioPlugin`. Plugins are dynamically loaded, so changing
the base-class virtual table would make a host call through a slot absent from
an older binary plugin. The non-virtual property-backed contract keeps those
plugins ABI-compatible: a plugin that has not opted in simply remains
available.

This is intentionally an opt-in declaration. Existing pure view, inspection,
and general tools remain available without code changes. New business plugins
join the gate by calling the declaration method instead of being added to a
fragile name-based list in `RobWorkStudio`.

## Main Window Behavior

`RobWorkStudio` derives project availability solely from its active
`ProjectManager` context. It refreshes plugin availability after every stable
project-context transition, including successful create/open, failed rollback,
close, save-as activation, and startup with no project.

For a plugin that requires project context:

- With no active project, its Plugins-menu action and toolbar action are
  disabled and its dock is hidden. A stale panel therefore cannot remain
  interactive after project closure.
- With an active project, those actions are enabled. Existing user visibility
  preferences are otherwise preserved: a dock that was visible when the
  project context disappeared is restored when a project becomes active again.
- Showing a gated plugin programmatically is rejected while no project is
  active.

Pure-view and general-purpose plugins are not affected by these transitions.

## RobotModelBuilder New-Project Flow

RobotModelBuilder is a project-context business plugin. Its dialog-free
preflight, state snapshot, resource declaration, and bootstrap operations may
be invoked internally during `File > New Project...` and `Create Project from
Robot File...`. Those operations are not user-facing panel activation.

The Builder panel remains hidden/disabled before the candidate project is
activated. Once the transaction succeeds and the project context is valid, the
normal availability refresh enables it and the existing flow may show the
Builder. A failed transaction restores the previous availability state.

## Initial Business Plugin Set

The initial project-context plugin set is RobotModelBuilder, WorkcellEditor,
Engineering Requirements, Kinematic Analysis, and Structure Optimizer. Their
primary commands construct, edit, analyse, or optimise robot/WorkCell data.

ATask/GTask visualisation plugins, Lua, and other general-purpose or pure-view
plugins stay available without a project. A future plugin is gated only when
its owner explicitly calls the declaration method.

## Error Handling

The gate is a UI and activation constraint, not an error dialog. Attempting to
open a gated plugin with no project has no side effects and leaves the panel
hidden. Existing module-level readiness diagnostics remain in place as a
defence in depth mechanism, notably for projects whose robot model has not yet
published `mainWorkCell`.

## Verification

Tests will prove that:

1. a default plugin remains available without a project;
2. a project-context plugin is hidden and cannot be shown without a project;
3. successful project activation enables the same plugin;
4. project closure hides and disables it again; and
5. RobotModelBuilder can bootstrap a new project internally, then becomes
   visible only after the project transaction succeeds.

The focused RobWorkStudio GTest and RobotModelBuilder meta/widget tests will
run with `QT_QPA_PLATFORM=windows` in separate PowerShell invocations.
