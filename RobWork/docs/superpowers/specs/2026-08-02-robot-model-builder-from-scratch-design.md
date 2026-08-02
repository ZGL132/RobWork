# RobotModelBuilder From-Scratch Project Flow Design

## Goal

Upgrade `File > New Project...` so it creates a project that immediately opens
RobotModelBuilder with a default six-axis model. The model remains editable until
the user runs `Save and Load`; that action publishes a managed WorkCell and makes
the existing analysis modules available.

## Scope

This flow replaces the current New Project menu behavior. It does not change:

- `Create Project from WorkCell...`;
- `Create Project from Robot File...`;
- standalone RobotModelBuilder use outside a project; or
- the existing Save and Load publication contract.

## User Flow

1. The user selects `File > New Project...` and chooses a new `.rwproj` path.
2. Before changing any project state, RobWorkStudio verifies that a loaded
   RobotModelBuilder plugin exposes the required preflight and project-bootstrap
   invokable operations.
3. The main window obtains the normal close confirmation for the current project.
4. RobWorkStudio creates and activates an empty candidate project, closes the old
   project documents, and establishes the new project context.
5. RobotModelBuilder applies its default six-axis model, registers the
   `robot-model.main` managed document, establishes an unsaved baseline, and is
   shown to the user.
6. The user edits the model and selects `Save and Load`. The existing transactional
   publisher generates XML, validates the generated WorkCell, promotes it to
   `mainWorkCell`, and then enables the downstream modules.

## Atomicity And Recovery

The main-window path helper must be testable without dialogs. It performs plugin
preflight before project creation and records the previous manager/registry/UI
state before activation. If candidate activation, document registration, or
Builder bootstrap fails, it restores the prior project context and documents,
removes only the newly created candidate files, and does not add a recent-project
entry.

A plugin missing either required meta-object operation is a pre-creation failure.
The user receives an actionable dialog and the current project remains untouched.

The Builder bootstrap must be idempotent for its managed resource. Re-entering the
same operation does not duplicate `robot-model.main` or overwrite an existing
model document. It refreshes the default editable model only during the first
successful bootstrap of the new project.

## Model And Publication Contract

The bootstrap model is produced by the existing default six-axis model factory,
not by a separate hard-coded UI fixture. In project mode the model sidecar has the
stable managed resource identity `robot-model.main`, kind `robwork.robot-model`,
ownership `generated`, and a path beneath `generated/robot-models/`.

The bootstrap only marks the model document dirty. It does not write XML or add a
`mainWorkCell` entry. `Save and Load` remains the sole action that can publish
generated scene/device files and promote `mainWorkCell`. Therefore a newly created
project continues to show the existing managed-project readiness gate until that
publication succeeds.

All project paths remain project-relative through the existing
RobotModelProjectPaths and ProjectSaveTransaction machinery. Moving or reopening
the project must preserve model JSON resolution and generated WorkCell resolution.

## Interfaces

RobotModelBuilder exposes two invokable, dialog-free methods:

```cpp
Q_INVOKABLE QString preflightNewRobotProject (const QString& projectRoot);
Q_INVOKABLE QString bootstrapNewRobotProject (const QString& projectRoot);
```

Each returns an empty string on success and an actionable error on failure.
Preflight has no UI, model, dirty-state, or filesystem side effect.
Bootstrap applies the default model, sets the project output directory, registers
or adopts `robot-model.main`, establishes the dirty document baseline, and marks
the project document dirty.

RobWorkStudio provides a path-based helper with callback seams, comparable to the
Robot File helper:

```cpp
struct NewRobotProjectCallbacks
{
    std::function<bool (const QString&, QString*)> preflight;
    std::function<bool (const QString&, QString*)> bootstrap;
};

bool createProjectWithRobotModelBuilderPaths (
    const QString& projectFile,
    const NewRobotProjectCallbacks& callbacks,
    QString* error = nullptr);
```

The menu slot owns dialogs and meta-object invocation. The helper owns ordering,
rollback, document lifecycle, and recent-file updates.

## Verification

Automated coverage must prove all of the following:

- unavailable or incompatible RobotModelBuilder aborts before creating a project;
- callback preflight and bootstrap failures restore the prior project, registry,
  and target directory byte-for-byte;
- successful creation opens the default six-axis model as a dirty managed
  `robot-model.main` document without creating `mainWorkCell`;
- `Save and Load` publishes the managed WorkCell and satisfies downstream module
  readiness;
- saving, reopening, and moving the project resolves the model and generated
  WorkCell from the new project root; and
- repeated bootstrap cannot duplicate resources or overwrite an existing edited
  model.

The focused RobWorkStudio GTest and RobotModelBuilder widget/meta-object tests
must run under `QT_QPA_PLATFORM=windows`. Existing EngineeringRequirements,
KinematicAnalysis, and StructureOptimizer managed-project tests remain the
downstream regression suite.
