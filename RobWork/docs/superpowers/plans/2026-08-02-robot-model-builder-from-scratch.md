# RobotModelBuilder From-Scratch Project Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `File > New Project...` open an editable default six-axis RobotModelBuilder project, with `Save and Load` as the only publisher of `mainWorkCell`.

**Architecture:** `RobWorkStudio` owns a dialog-free candidate-project helper and restores its old manager/registry/UI state on every failure. `RobotModelBuilderPlugin` exposes dialog-free preflight/bootstrap operations and applies the existing default model factory into stable `robot-model.main`; the widget retains its current dirty-snapshot and transactional-publish responsibilities.

**Tech Stack:** C++14, Qt Widgets/meta-object calls, GTest, `ProjectManager`, `ProjectDocumentRegistry`, `CallbackProjectDocumentProvider`, RobotModelBuilder, CTest.

---

## File Structure

- `RobWorkStudio/src/rws/RobWorkStudio.hpp`: callback seam and helper declaration.
- `RobWorkStudio/src/rws/RobWorkStudio.cpp`: candidate creation, rollback, and menu wiring.
- `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`: helper transaction and readiness tests.
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp/.cpp`: invokable Builder contract and idempotent bootstrap.
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp/.cpp`: narrow default-model operation if plugin encapsulation requires it.
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp`: dirty baseline and project-mode publishing contract.
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPluginMetaObjectTest.cpp`: invokable signature checks.

### Task 1: Main-Window Helper Contract

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.hpp:72-116`
- Modify: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp:after Robot File helper tests`

- [ ] **Step 1: Write failing helper tests**

```cpp
struct NewRobotProjectCallbacks
{
    std::function< bool (const QString&, QString*) > preflight;
    std::function< bool (const QString&, QString*) > bootstrap;
    std::function< bool (QString*) > confirmClose;
};

TEST (RobWorkStudio, NewRobotProjectPreflightFailurePreservesCurrentProject)
{
    // Open createEmptyProject(current.path()), fail preflight, then assert no
    // bootstrap, unchanged projectDirectory/recent list, and absent target file.
}
TEST (RobWorkStudio, NewRobotProjectBootstrapFailureRestoresCurrentProject)
{
    // Load a CallbackProjectDocumentProvider-backed old project, make bootstrap
    // fail, then assert old provider reload, no candidate file, unchanged recent list.
}
TEST (RobWorkStudio, NewRobotProjectBootstrapCreatesDirtyModelWithoutWorkCell)
{
    // Bootstrap registers/adopts generated robot-model.main, then assert dirty
    // state, empty mainWorkCellResourceId(), and recent update only on success.
}
```

- [ ] **Step 2: Run RED**

```powershell
$env:QT_QPA_PLATFORM='windows'
& 'D:\10_Source_Repos\21_robot\RobWork\build\RobWorkStudio\gtest\sdurws_sdurws-gtest.exe' --gtest_filter='RobWorkStudio.NewRobotProject*'
```

Expected: compile fails because the callback type/helper do not exist.

- [ ] **Step 3: Declare the complete public seam**

```cpp
struct NewRobotProjectCallbacks
{
    std::function< bool (const QString&, QString*) > preflight;
    std::function< bool (const QString&, QString*) > bootstrap;
    std::function< bool (QString*) > confirmClose;
};

bool createProjectWithRobotModelBuilderPaths (
    const QString& projectFile,
    const NewRobotProjectCallbacks& callbacks,
    QString* error = nullptr);
```

- [ ] **Step 4: Rebuild/re-run RED**

Re-run the Task 1 command. Expected: link/test failure until implementation exists.

### Task 2: Candidate Activation and Rollback

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp:1003-1046`
- Test: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp:NewRobotProject tests`

- [ ] **Step 1: Implement side-effect-free preflight before candidate creation**

```cpp
if (error != nullptr) error->clear ();
if (projectFile.trimmed ().isEmpty () || !callbacks.preflight || !callbacks.bootstrap) {
    if (error != nullptr)
        *error = QStringLiteral ("New robot project callbacks and target path are required.");
    return false;
}
const QString absoluteProjectFile = QFileInfo (projectFile).absoluteFilePath ();
const QString projectRoot = QFileInfo (absoluteProjectFile).absolutePath ();
if (!callbacks.preflight (projectRoot, error)) {
    if (error != nullptr && error->isEmpty ())
        *error = QStringLiteral ("RobotModelBuilder cannot initialize a new robot project.");
    return false;
}
```

- [ ] **Step 2: Implement the candidate lifecycle**

Create an empty relative-path manifest only after optional `confirmClose`; retain `previousProjectFile` and `previousManifest`; call `_projectManager.createProject`; close old registry resources; create the candidate empty WorkCell; and invoke `bootstrap(projectRoot, &bootstrapError)`. On bootstrap or registry failure, close candidate resources, close candidate manager state, remove only `absoluteProjectFile`, reopen/load the old project and its provider documents (or create an empty WorkCell), update the title, and preserve the original recent list. On success update `PreviousOpenDirectory`, append recent, call `updateLastFiles()` and `updateProjectWindowTitle()`.

- [ ] **Step 3: Run GREEN**

Run the Task 1 command. Expected: every `NewRobotProject*` test passes; candidate directories/resources are not created on preflight failure and the candidate manifest is removed on bootstrap failure.

- [ ] **Step 4: Commit**

```powershell
git add RobWorkStudio/src/rws/RobWorkStudio.hpp RobWorkStudio/src/rws/RobWorkStudio.cpp RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp
git commit -m 'feat: bootstrap robot builder projects transactionally'
```

### Task 3: Builder Meta-Object Contract

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp:40-55`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPluginMetaObjectTest.cpp:18-35`

- [ ] **Step 1: Write failing meta-object checks**

```cpp
if (!hasQStringOperation (metaObject, "preflightNewRobotProject(QString)"))
    return fail ("New robot project preflight must be an invokable QString operation.");
if (!hasQStringOperation (metaObject, "bootstrapNewRobotProject(QString)"))
    return fail ("New robot project bootstrap must be an invokable QString operation.");
```

- [ ] **Step 2: Run RED**

```powershell
& 'D:\10_Source_Repos\21_robot\RobWork\build\RobWorkStudio\src\rwslibs\robotmodelbuilder\sdurws_robotmodelbuilder_metatest.exe'
```

Expected: fail because the first invokable is missing.

- [ ] **Step 3: Add matching invokable declarations**

```cpp
Q_INVOKABLE QString preflightNewRobotProject (const QString& projectRoot);
Q_INVOKABLE QString bootstrapNewRobotProject (const QString& projectRoot);
```

### Task 4: Default-Model Bootstrap and Idempotence

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp:226-296`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp:before main()`

- [ ] **Step 1: Write failing default-baseline coverage**

Create a temporary project root, set it on a widget, invoke its default-model operation, then `beginGeneratedProjectDocument()`. Assert the widget is dirty and `generated/robot-models` does not exist. This proves initialization does not write JSON/XML or publish a WorkCell.

- [ ] **Step 2: Run widget RED**

```powershell
$env:QT_QPA_PLATFORM='windows'
& 'D:\10_Source_Repos\21_robot\RobWork\build\RobWorkStudio\src\rwslibs\robotmodelbuilder\sdurws_robotmodelbuilder_widgettest.exe'
```

Expected: test fails until the default-project operation is exposed and used.

- [ ] **Step 3: Implement preflight without side effects**

```cpp
QString RobotModelBuilderPlugin::preflightNewRobotProject (const QString& projectRoot)
{
    if (_widget == NULL || getRobWorkStudio () == NULL || _projectProvider == NULL)
        return QStringLiteral ("RobotModelBuilder is not attached to RobWorkStudio project services.");
    if (projectRoot.trimmed ().isEmpty () || !QDir::isAbsolutePath (projectRoot))
        return QStringLiteral ("The new robot project root must be an absolute path.");
    return QString ();
}
```

- [ ] **Step 4: Implement bootstrap through the existing default factory**

Validate preflight and that `projectRoot` equals `studio->projectDirectory()`. Build `defaults` with `RobotModelXmlWriter::makeDefaultSixAxisModel(QDir(requestedRoot).filePath("generated/robot-models"))`, then register:

```cpp
resource.id = QStringLiteral ("robot-model.main");
resource.kind = QStringLiteral ("robwork.robot-model");
resource.path = QStringLiteral ("generated/robot-models/%1.rmb.json").arg(
    RobotModelXmlWriter::sanitizeFileBaseName(QString::fromStdString(defaults.robotName)));
resource.ownership = QStringLiteral ("generated");
resource.required = true;
```

Call `ensureGeneratedProjectResource(resource, &created, &error)`. If `created` is false, return success without changing the editable UI. If true, set project output, apply the default via a narrow public widget method, adopt the resource, establish the generated-document baseline, set provider dirty from the widget snapshot, notify Studio, and set a status saying `Save and Load` publishes the WorkCell.

- [ ] **Step 5: Verify GREEN**

```powershell
$env:QT_QPA_PLATFORM='windows'
& 'D:\10_Source_Repos\21_robot\RobWork\build\RobWorkStudio\src\rwslibs\robotmodelbuilder\sdurws_robotmodelbuilder_widgettest.exe'
& 'D:\10_Source_Repos\21_robot\RobWork\build\RobWorkStudio\src\rwslibs\robotmodelbuilder\sdurws_robotmodelbuilder_metatest.exe'
```

Expected: both exit `0`; existing project-mode Save and Load still promotes transactionally and does not write an unmanaged sidecar.

### Task 5: Menu Wiring and Compatibility Guard

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp:1003-1046`
- Test: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp:NewRobotProject tests`

- [ ] **Step 1: Write missing/incompatible Builder coverage**

Use helper `preflight` to return `RobotModelBuilder lacks New Project support.` and assert no target manifest was created and the current project stays selected.

- [ ] **Step 2: Run targeted RED**

Run the Task 1 GTest command. Expected: fail until slot wiring preserves this compatibility behavior.

- [ ] **Step 3: Replace `newProject()` menu behavior**

Before the save dialog, find plugin name `RobotModelBuilder` and check normalized signatures `preflightNewRobotProject(QString)` and `bootstrapNewRobotProject(QString)`. If missing, show a warning and return. Create callbacks using `QMetaObject::invokeMethod` with `Qt::DirectConnection`, `Q_RETURN_ARG(QString, result)`, and `Q_ARG(QString, projectRoot)`; map empty results to success; use `confirmProjectClose()` for `confirmClose`. On helper success call `builder->showPlugin()`; on non-empty error show `QMessageBox::critical`. Do not call `createEmptyWorkCell()` directly from the menu slot.

- [ ] **Step 4: Run GREEN and commit**

```powershell
$env:QT_QPA_PLATFORM='windows'
& 'D:\10_Source_Repos\21_robot\RobWork\build\RobWorkStudio\gtest\sdurws_sdurws-gtest.exe' --gtest_filter='RobWorkStudio.NewRobotProject*:RobWorkStudio.RobotProject*'
git add RobWorkStudio/src/rws/RobWorkStudio.cpp RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPluginMetaObjectTest.cpp
git commit -m 'feat: open RobotModelBuilder for new projects'
```

Expected: helper and Robot File tests pass, and the Builder is never shown or invoked on failure.

### Task 6: Publication, Persistence, and Full Regression

**Files:**
- Modify only the listed feature files if a scoped failure is found.

- [ ] **Step 1: Extend success coverage across the readiness boundary**

Save/reopen a successfully bootstrapped project and assert `robot-model.main` resolves under the project root while `mainWorkCell` is empty. Create generated device/scene fixture XML, call `promoteGeneratedWorkCell`, save/reopen, and assert `entryPoints["mainWorkCell"] == "scene.main"`. Reuse existing portable widget relocation coverage to verify model paths remain project-relative.

- [ ] **Step 2: Run full selected verification**

```powershell
cmake --build 'D:\10_Source_Repos\21_robot\RobWork\build' --config Debug --target sdurws_sdurws-gtest sdurws_robotmodelbuilder_widgettest sdurws_robotmodelbuilder_metatest --parallel 4
$env:QT_QPA_PLATFORM='windows'
ctest --test-dir 'D:\10_Source_Repos\21_robot\RobWork\build' -C Debug --output-on-failure -R 'sdurws_sdurws-gtest|sdurws_robotmodelbuilder_widgettest|sdurws_robotmodelbuilder_metatest|engineeringrequirements|kinematicanalysis|structureoptimizer'
git diff --check
```

Expected: every selected test passes; there is no `mainWorkCell` before Save and Load publication.

- [ ] **Step 3: Inspect and commit only feature files**

Review `git status --short` and the feature-file diff. Preserve all existing staged Robot File Task 7 changes and unrelated user files. Stage only the files listed under File Structure and commit:

```powershell
git commit -m 'feat: complete RobotModelBuilder new project flow'
```

## Plan Self-Review

- Coverage: Tasks 1-2 cover no-mutation preflight, rollback, registry restoration, recent-file ordering, and candidate cleanup. Tasks 3-5 cover exact invokables, idempotent default factory use, menu compatibility, Builder opening, dirty resource creation, and no XML during bootstrap. Task 6 covers Save and Load publication, re-open/persistence, relative paths, and analysis-module regression.
- Placeholder scan: no `TODO`, `TBD`, or unspecified verification remains; every behavior has a named file and command.
- Type consistency: every task uses `NewRobotProjectCallbacks`, `createProjectWithRobotModelBuilderPaths`, `preflightNewRobotProject`, `bootstrapNewRobotProject`, `robot-model.main`, and `robwork.robot-model` with the same signatures/identities.
