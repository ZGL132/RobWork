# Robot File Main Flow Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `Create Project from Robot File` into an atomic, self-contained URDF project workflow that reaches managed WorkCell generation and all downstream analysis modules.

**Architecture:** The rws project layer packages URDF source assets and owns candidate activation/rollback. RobotModelBuilder exposes non-mutating preflight plus managed import, persists portable model paths, and publishes generated XML through the existing project transaction machinery. Downstream plugins use the stable `mainWorkCell` gate already established for WorkCell projects.

**Tech Stack:** C++17, Qt 6 Core/Widgets/XML, RobWork WorkCellLoader, ProjectManager, ProjectSaveTransaction, GoogleTest, existing command-line plugin tests, CMake/CTest.

---

### Task 1: Package URDF sources and mesh dependencies

**Files:**
- Create: `RobWorkStudio/src/rws/RobotProjectSourcePackager.hpp`
- Create: `RobWorkStudio/src/rws/RobotProjectSourcePackager.cpp`
- Modify: `RobWorkStudio/src/rws/CMakeLists.txt`
- Test: `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`

- [ ] **Step 1: Add failing portable-package tests**

Add tests that create small URDF files and ordinary mesh files under `QTemporaryDir`. The
success test must use the same layout pattern as the real 300kg package: the URDF is in
`output/robot.urdf`, meshes are in sibling `meshes/`, and references use
`package://robot_pkg/meshes/base.STL`.

```cpp
TEST (ProjectSystemTest, RobotSourcePackagerCreatesPortableManagedUrdf)
{
    QTemporaryDir source;
    QTemporaryDir target;
    ASSERT_TRUE (source.isValid ());
    ASSERT_TRUE (target.isValid ());
    const QString urdf = writePackageUrdf (
        source.path (),
        {{QStringLiteral ("package://robot_pkg/meshes/base.STL"),
          QStringLiteral ("base.STL")},
         {QStringLiteral ("package://robot_pkg/meshes/link1.STL"),
          QStringLiteral ("link1.STL")}});

    rws::PackagedRobotSource packaged;
    QString error;
    ASSERT_TRUE (rws::RobotProjectSourcePackager::prepare (
        urdf, QDir (target.path ()).filePath ("Demo.rwproj"), packaged, &error))
        << error.toStdString ();

    EXPECT_EQ (QStringLiteral ("sources/robot/robot.urdf"), packaged.sourceResource.path);
    EXPECT_EQ (2, packaged.assetResources.size ());
    EXPECT_TRUE (allMeshReferencesAreRelativeAndContained (
        packaged.stagedManagedUrdfPath, packaged.stagingRoot));
}
```

Add separate tests for:

```cpp
TEST (ProjectSystemTest, RobotSourcePackagerRejectsMissingMeshWithoutResidue);
TEST (ProjectSystemTest, RobotSourcePackagerDeduplicatesCanonicalMeshReferences);
TEST (ProjectSystemTest, RobotSourcePackagerSeparatesSameBasenameMeshes);
TEST (ProjectSystemTest, RobotSourcePackagerRejectsNonUrdfXml);
TEST (ProjectSystemTest, RobotSourcePackagerRejectsExistingTargetsWithoutOverwrite);
```

Each failure test records target directory contents before the call and asserts byte-for-byte
and name-for-name equality afterward.

- [ ] **Step 2: Run the tests and verify RED**

Run:

```powershell
cmake --build . --config Debug --target sdurws_sdurws-gtest
```

Expected: compile failure because `RobotProjectSourcePackager` and `PackagedRobotSource` do not
exist.

- [ ] **Step 3: Implement the structured URDF packager**

Define the focused result type and API:

```cpp
struct PackagedRobotSource
{
    QString stagingRoot;
    QString stagedManagedUrdfPath;
    ProjectResource sourceResource;
    QVector< ProjectResource > assetResources;
    QMap< QString, QString > stagedFilesByProjectPath;
};

class RobotProjectSourcePackager
{
  public:
    static bool prepare (const QString& sourceUrdfPath,
                         const QString& targetProjectFilePath,
                         PackagedRobotSource& packaged,
                         QString* error = nullptr);
    static void discard (PackagedRobotSource& packaged);
};
```

Implementation requirements:

- Parse with `QXmlStreamReader`; the first element must be case-insensitive `robot`.
- Resolve package URIs using the URDF directory, parent, and grandparent roots, trying both
  `<root>/<package>/<relative>` and `<root>/<relative>` as the existing importer does.
- Require every resolved mesh to be an existing ordinary file.
- Canonicalize before deduplication.
- Map `package://pkg/path` to
  `assets/robot/packages/<sanitized-pkg>/<normalized-path>`.
- Map relative/absolute non-package files to
  `assets/robot/files/<first-12-sha256>-<sanitized-basename>`.
- Sort final project paths before assigning `robot-source.asset.<sha-prefix>` IDs.
- Rewrite each `<mesh filename>` with a relative path from `sources/robot/robot.urdf`.
- Reject any rewritten absolute path or path that resolves outside the staging root.
- Create staging under `<target-dir>/.rwproject/import-<uuid>/root/`; record only files made by
  this attempt and remove that subtree on every failure. Also prune `.rwproject` when this
  attempt created it and it is empty; never remove a pre-existing `.rwproject` directory.
- Never create or overwrite the final `.rwproj`, managed source, or final asset files here.

- [ ] **Step 4: Run focused packaging tests and verify GREEN**

Run:

```powershell
.\RobWorkStudio\bin\sdurws_sdurws-gtest.exe --gtest_filter=ProjectSystemTest.RobotSourcePackager*
```

Expected: all packaging tests pass.

- [ ] **Step 5: Commit Task 1**

```powershell
git add RobWorkStudio/src/rws/RobotProjectSourcePackager.hpp `
        RobWorkStudio/src/rws/RobotProjectSourcePackager.cpp `
        RobWorkStudio/src/rws/CMakeLists.txt `
        RobWorkStudio/gtest/rws/ProjectSystemTest.cpp
git commit -m "feat: package portable robot project sources"
```

---

### Task 2: Prepare, activate, and roll back candidate robot projects

**Files:**
- Modify: `RobWorkStudio/src/rws/ProjectManager.hpp`
- Modify: `RobWorkStudio/src/rws/ProjectManager.cpp`
- Modify: `RobWorkStudio/src/rws/RobotProjectSourcePackager.hpp`
- Modify: `RobWorkStudio/src/rws/ProjectSaveTransaction.hpp`
- Modify: `RobWorkStudio/src/rws/ProjectSaveTransaction.cpp`
- Test: `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`
- Test: `RobWorkStudio/gtest/rws/ProjectDocumentRegistryTest.cpp`

- [ ] **Step 1: Add failing candidate-lifecycle tests**

Use a manager that already owns a valid project. Prepare a second robot project and assert the
first manager context and manifest remain unchanged until activation.

```cpp
TEST (ProjectSystemTest, ManagerPreparesRobotProjectWithoutSwitchingContext)
{
    rws::ProjectManager manager;
    ASSERT_TRUE (manager.createProject (oldProject, oldManifest, &error));
    const QString oldPath = manager.projectFilePath ();

    rws::PreparedRobotProject prepared;
    ASSERT_TRUE (manager.prepareProjectFromRobotFile (
        newProject, sourceUrdf, prepared, &error));

    EXPECT_EQ (oldPath, manager.projectFilePath ());
    EXPECT_FALSE (QFileInfo::exists (newProject));
    EXPECT_TRUE (QFileInfo::exists (prepared.packaged.stagedManagedUrdfPath));
}
```

Add:

```cpp
TEST (ProjectSystemTest, ManagerActivatesPreparedRobotProjectAtomically);
TEST (ProjectSystemTest, ManagerRollsBackActivatedRobotProjectAndRestoresContext);
TEST (ProjectSystemTest, ManagerActivationFailurePreservesOldProjectAndTargetFiles);
```

The activated manifest must contain:

```cpp
EXPECT_EQ (QStringLiteral ("robot-source.main"),
           manifest.entryPoints.value (QStringLiteral ("robotSource")));
EXPECT_TRUE (resourceById (manifest, "robot-source.main").required);
EXPECT_EQ (QStringLiteral ("robwork.passive-asset"),
           resourceById (manifest, "robot-source.main").kind);
```

- [ ] **Step 2: Run the tests and verify RED**

Run the `ProjectSystemTest.Manager*RobotProject*` filter. Expected: compile failure because the
prepared-project lifecycle APIs do not exist.

- [ ] **Step 3: Add the candidate lifecycle API**

Add:

```cpp
struct PreparedRobotProject
{
    QString projectFilePath;
    ProjectManifest manifest;
    PackagedRobotSource packaged;
    QStringList committedProjectPaths;
    QHash< QString, QByteArray > committedContentHashes;
    QString previousProjectFilePath;
    ProjectManifest previousManifest;
    bool previousDirty = false;
    bool activated = false;
};

bool prepareProjectFromRobotFile (const QString& projectFilePath,
                                  const QString& sourceUrdfPath,
                                  PreparedRobotProject& prepared,
                                  QString* error = nullptr) const;
bool activatePreparedRobotProject (PreparedRobotProject& prepared,
                                   QString* error = nullptr);
bool rollbackActivatedRobotProject (PreparedRobotProject& prepared,
                                    QString* error = nullptr);
static void discardPreparedRobotProject (PreparedRobotProject& prepared);
```

`prepareProjectFromRobotFile` builds a candidate manifest from the package but does not call
`createProject` and does not mutate `_projectFilePath`, `_manifest`, or `_dirty`.

`activatePreparedRobotProject` must:

1. re-check that no final target exists;
2. serialize the candidate manifest;
3. use `ProjectSaveTransaction::stageCopy` for every managed source/asset and
   `stageBytes` for the manifest, with an `ExistingTargetPolicy::Reject` transaction mode;
4. commit the disk transaction;
5. record the SHA-256 content hash of every committed final file;
6. only then store the previous manager snapshot and replace manager context; and
7. clean the staging tree after successful activation.

Add `ProjectSaveTransaction::ExistingTargetPolicy { Replace, Reject }`, preserving `Replace` as
the default for all existing callers. In `Reject` mode, `commit` must fail and roll back if any
target exists at installation time; it must never back up or replace that target. Add a test
that creates a colliding target after staging and verifies its bytes are preserved.

`rollbackActivatedRobotProject` removes only paths listed in `committedProjectPaths`, removes
newly empty directories, and restores the exact previous manager path, manifest, and dirty
flag. Before deleting each committed file, compare its current SHA-256 with
`committedContentHashes`; if a file is missing or has been externally changed, preserve it,
restore the manager snapshot, and return an actionable cleanup error listing the path. Both
rollback and discard reset the prepared object so repeated cleanup calls are harmless.

- [ ] **Step 4: Run all ProjectManager tests and verify GREEN**

Run:

```powershell
.\RobWorkStudio\bin\sdurws_sdurws-gtest.exe --gtest_filter=ProjectSystemTest.*
```

Expected: all existing WorkCell and new robot-project lifecycle tests pass.

- [ ] **Step 5: Commit Task 2**

```powershell
git add RobWorkStudio/src/rws/ProjectManager.hpp `
        RobWorkStudio/src/rws/ProjectManager.cpp `
        RobWorkStudio/src/rws/RobotProjectSourcePackager.hpp `
        RobWorkStudio/src/rws/ProjectSaveTransaction.hpp `
        RobWorkStudio/src/rws/ProjectSaveTransaction.cpp `
        RobWorkStudio/gtest/rws/ProjectSystemTest.cpp `
        RobWorkStudio/gtest/rws/ProjectDocumentRegistryTest.cpp
git commit -m "feat: activate robot projects transactionally"
```

---

### Task 3: Add Builder preflight and portable managed model paths

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelProjectPaths.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelProjectPaths.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJsonTest.cpp`

- [ ] **Step 1: Add failing no-side-effect and path-portability tests**

Add a widget test that snapshots `currentModelSpec()` and dirty state, preflights a different
URDF, and proves neither changes.

```cpp
rws::RobotModelSpec parsed;
QStringList warnings;
QString error;
REQUIRE (widget.preflightUrdfFile (urdf, projectRoot, parsed, warnings, &error));
REQUIRE (widget.currentModelSpec ().robotName == before.robotName);
REQUIRE (widget.isProjectDocumentDirty () == dirtyBefore);
REQUIRE (parsed.robotName == "300kg");
```

Add JSON/provider tests that save a model containing absolute paths inside the project root,
inspect the written JSON for the absence of that absolute root, change process CWD to an
unrelated temporary directory, load it under a moved project root, and assert every runtime
mesh path resolves under the moved root.

Add a failure case for a project-mode mesh path outside the project root.

- [ ] **Step 2: Run the tests and verify RED**

Build `sdurws_robotmodelbuilder_widgettest` and `sdurws_robotmodelbuilder_jsontest`.
Expected: compile failure for `preflightUrdfFile` and immediate-pass portability assertions
showing absolute `filePath` values currently persist.

- [ ] **Step 3: Implement project path conversion**

Define:

```cpp
class RobotModelProjectPaths
{
  public:
    static bool makePortable (const RobotModelSpec& runtime,
                              const QString& projectRoot,
                              RobotModelSpec& portable,
                              QString* error = nullptr);
    static bool resolveManaged (const RobotModelSpec& portable,
                                const QString& projectRoot,
                                RobotModelSpec& runtime,
                                QString* error = nullptr);
};
```

Apply the same conversion to `drawables`, `collisionModels`, and file-backed scene geometry.
`makePortable` rejects absolute paths outside `projectRoot`; `resolveManaged` rejects relative
paths that escape it. Neither function uses `QDir::currentPath()`.

Keep the existing exported `importUrdfFile(const QString&, QString*)` symbol. Implement it as
preflight plus apply so standalone callers retain behavior:

```cpp
bool preflightUrdfFile (const QString& path,
                        const QString& projectRoot,
                        RobotModelSpec& parsed,
                        QStringList& warnings,
                        QString* error = nullptr) const;
void applyImportedProjectModel (const RobotModelSpec& parsed,
                                const QStringList& warnings);
```

Update `saveProjectDocument` to serialize `makePortable(...)` and `loadProjectDocument` to call
`resolveManaged(...)` using `_projectOutputDirectory`.

- [ ] **Step 4: Add Qt meta-object plugin capabilities**

Replace the fire-and-forget project slot with invokable operations returning an empty string on
success and an actionable error on failure:

```cpp
Q_INVOKABLE QString preflightRobotProjectSource (const QString& sourcePath,
                                                 const QString& projectRoot);
Q_INVOKABLE QString commitRobotProjectSource (const QString& sourcePath,
                                              const QString& projectRoot);
```

`commitRobotProjectSource` re-parses the managed copy, applies it, registers
`robot-model.main`, sets its dependency to `robot-source.main`, adopts the resource, and marks
the provider dirty. Do not display a QMessageBox in these methods; the main window owns errors.

- [ ] **Step 5: Run RobotModelBuilder tests and verify GREEN**

Run all four RobotModelBuilder executables with `QT_QPA_PLATFORM=windows` on Windows. Expected:
all pass, including hostile-CWD and historical import signature checks.

- [ ] **Step 6: Commit Task 3**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder
git commit -m "fix: keep managed robot models portable"
```

---

### Task 4: Orchestrate atomic creation from the File menu

**Files:**
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp`
- Test: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`

- [ ] **Step 1: Add failing orchestration tests through callback seams**

Extract a narrow non-dialog helper whose callbacks can be faked by the test:

```cpp
struct RobotProjectImportCallbacks
{
    std::function< bool (const QString&, const QString&, QString*) > preflight;
    std::function< bool (const QString&, const QString&, QString*) > commit;
};

bool createProjectFromRobotFilePaths (
    const QString& sourcePath,
    const QString& projectFile,
    const RobotProjectImportCallbacks& callbacks,
    QString* error = nullptr);
```

Add tests for:

```cpp
TEST (RobWorkStudioTest, RobotProjectPreflightFailurePreservesCurrentProject);
TEST (RobWorkStudioTest, RobotProjectManagedCopyPreflightFailureRemovesCandidate);
TEST (RobWorkStudioTest, RobotProjectCommitFailureRestoresCurrentProject);
TEST (RobWorkStudioTest, RobotProjectSuccessUpdatesRecentFilesOnlyAfterCommit);
```

Tests must inspect the active project path, registry loaded resources, final target contents,
recent-file settings, and generated model dependency.

- [ ] **Step 2: Run orchestration tests and verify RED**

Expected: compile failure because `RobotProjectImportCallbacks` and the path-based helper do not
exist.

- [ ] **Step 3: Implement the ordered orchestration**

The helper performs exactly this sequence:

```text
source preflight
prepare candidate package
managed-copy preflight
confirm/complete old-project close decision (UI wrapper only)
activate prepared project
close old registry resources and establish new project context
commit Builder import
on commit failure: close new resources, roll back manager/files, reopen old resources
on success: show Builder, update previous directory/recent projects/title
```

The menu slot locates `RobotModelBuilder`, verifies both invokable methods with
`QMetaObject::indexOfMethod`, wraps them with `QMetaObject::invokeMethod` and
`Q_RETURN_ARG(QString, result)`, and aborts before the save dialog if the plugin is unavailable
or incompatible.

Do not call the old fire-and-forget `importRobotProjectSource`. Keep it only as a forwarding
compatibility slot if external callers may still resolve its meta-object signature.

- [ ] **Step 4: Run RobWorkStudio and project-system tests and verify GREEN**

Run:

```powershell
.\RobWorkStudio\bin\sdurws_sdurws-gtest.exe `
  --gtest_filter=RobWorkStudioTest.*RobotProject*:ProjectSystemTest.*
```

Expected: all tests pass and failed attempts leave no new recent-file entry.

- [ ] **Step 5: Commit Task 4**

```powershell
git add RobWorkStudio/src/rws/RobWorkStudio.hpp `
        RobWorkStudio/src/rws/RobWorkStudio.cpp `
        RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp
git commit -m "fix: create robot projects atomically"
```

---

### Task 5: Publish generated RobotModelBuilder XML transactionally

**Files:**
- Modify: `RobWorkStudio/src/rws/ProjectSaveTransaction.hpp`
- Modify: `RobWorkStudio/src/rws/ProjectSaveTransaction.cpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelPublishService.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelPublishService.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
- Test: `RobWorkStudio/gtest/rws/ProjectDocumentRegistryTest.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp`

- [ ] **Step 1: Add failing two-phase transaction tests**

Add tests that install two new files, verify the new bytes are visible while backups still
exist, call rollback, and verify both original files return. Add a finalize test proving backup
cleanup and provider clean notification occur only after finalize.

```cpp
rws::ProjectSaveTransaction transaction;
ASSERT_TRUE (transaction.stageBytes (QByteArray ("new"), target, &error));
ASSERT_TRUE (transaction.install (&error));
EXPECT_EQ (QByteArray ("new"), readFile (target));
transaction.rollback ();
EXPECT_EQ (QByteArray ("old"), readFile (target));
```

Keep the existing `commit(QString*)` ABI and behavior as a forwarding operation:

```cpp
bool ProjectSaveTransaction::commit (QString* error)
{
    if (!install (error))
        return false;
    finalize ();
    return true;
}
```

- [ ] **Step 2: Run transaction tests and verify RED**

Expected: compile failure because `install` and `finalize` do not exist.

- [ ] **Step 3: Implement two-phase install/finalize**

Add:

```cpp
bool install (QString* error = nullptr);
void finalize ();
```

`install` performs target backup and staged-file installation but retains backups and does not
mark providers clean. `finalize` removes backups, marks providers clean, and disables destructor
rollback. Calling rollback after install must restore the originals.

- [ ] **Step 4: Add failing RobotModel publish tests**

Define a service request with an injected promotion callback:

```cpp
struct RobotModelPublishRequest
{
    RobotModelSpec spec;
    QString projectRoot;
    std::function< bool (const QString&, const QStringList&, QString*) > promote;
};

class RobotModelPublishService
{
  public:
    static bool publishAndLoad (const RobotModelPublishRequest& request,
                                QString* error = nullptr);
};
```

Test success plus failures during staging, real WorkCell load, and promotion. Seed old bytes in
every target sidecar and assert each failure restores all old bytes and leaves no
`.rwstage-*`/`.rwbackup-*` files.

- [ ] **Step 5: Implement project-mode publish**

`publishAndLoad` must:

1. validate the spec and project-contained geometry paths;
2. generate enabled XML strings with existing `RobotModelXmlWriter::make*Xml` functions;
3. stage each enabled final file with `ProjectSaveTransaction::stageBytes`;
4. call `install`;
5. load the final scene/device with `WorkCellLoader::Factory::load`;
6. call the injected promotion callback; and
7. call `finalize` only after load and promotion succeed.

Update project-mode `saveAndLoad` to emit a publish request instead of calling
`RobotModelXmlWriter::saveFiles` directly. Do not call `saveSpecSidecar` in project mode; the
managed `.rmb.json` remains dirty and is committed by `File > Save Project`. Standalone mode
continues to use the existing direct writer.

- [ ] **Step 6: Run focused tests and verify GREEN**

Run ProjectSaveTransaction tests and all RobotModelBuilder tests. Injected promotion failure
must leave the previous managed WorkCell and resource descriptor unchanged.

- [ ] **Step 7: Commit Task 5**

```powershell
git add RobWorkStudio/src/rws/ProjectSaveTransaction.hpp `
        RobWorkStudio/src/rws/ProjectSaveTransaction.cpp `
        RobWorkStudio/gtest/rws/ProjectDocumentRegistryTest.cpp `
        RobWorkStudio/src/rwslibs/robotmodelbuilder
git commit -m "fix: publish robot model XML transactionally"
```

---

### Task 6: Enforce workflow readiness and add 300kg acceptance

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: Add failing no-mainWorkCell gate tests**

Build a manifest with `robotSource` and `robot-model.main` but no `mainWorkCell`. Exercise the
managed freeze/import commands and assert the common actionable text:

```text
The robot project has not generated its managed WorkCell. Review the model in
RobotModelBuilder and run Save and Load first.
```

The gate must use the manifest entry point, not the non-null in-memory empty WorkCell.

- [ ] **Step 2: Run the three consumer suites and verify RED**

Expected: at least one consumer proceeds against the empty WorkCell or emits a generic missing
device/model message.

- [ ] **Step 3: Add the narrow managed-project gate**

At each user command boundary, when `projectDirectory()` is non-empty and
`mainWorkCellResourceId()` is empty, report the message above and return before opening file
dialogs, freezing requirements, or constructing analysis/optimization problems. Standalone
WorkCell behavior remains unchanged.

- [ ] **Step 4: Add the real 300kg acceptance path**

Add a `robot_file_acceptance` suite to `StructureOptimizationTest.cpp`. Use:

```cpp
const QString sourceRoot = QDir (QStringLiteral (STRUCTUREOPTIMIZER_TEST_SOURCE_DIR))
    .filePath ("RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/300kg_urdf");
const QString urdf = QDir (sourceRoot).filePath ("output/300kg.urdf");
```

The test must fail, not skip, in this workspace when the compile-time source exists but an
expected mesh is missing. It creates all output under `QTemporaryDir`, then verifies:

```cpp
REQUIRE (spec.robotName == "300kg");
REQUIRE (movableJointCount (spec) == 6);
REQUIRE (spec.drawables.size () == 7);
REQUIRE (spec.collisionModels.size () == 7);
REQUIRE (allRuntimeGeometryInsideProject (spec, projectRoot));
```

Continue through managed model save, transactional XML publication, WorkCell promotion,
requirement freeze, KinematicAnalysis adapter import, and StructureOptimizer frozen artifact
import. Rename the whole project directory, reopen it, and repeat model/WorkCell/frozen-resource
resolution with a hostile process CWD.

- [ ] **Step 5: Register conditional CTest coverage**

In CMake:

```cmake
set(ROBOT_FILE_300KG_URDF
    "${CMAKE_SOURCE_DIR}/RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/300kg_urdf/output/300kg.urdf")
if(EXISTS "${ROBOT_FILE_300KG_URDF}")
    add_test(NAME sdurws_structureoptimizer_robot_file_acceptance_test
             COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> robot_file_acceptance)
    set_tests_properties(sdurws_structureoptimizer_robot_file_acceptance_test
                         PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM}")
endif()
```

Keep mandatory small-fixture coverage in the always-built ProjectSystem and RobotModelBuilder
tests so CI remains meaningful without ignored local assets.

- [ ] **Step 6: Run acceptance and consumer tests and verify GREEN**

Run EngineeringRequirements, KinematicAnalysis, the three focused StructureOptimizer suites,
and `robot_file_acceptance`. Expected: all pass and the source `300kg_urdf` tree remains
byte-for-byte unchanged.

- [ ] **Step 7: Commit Task 6**

```powershell
git add RobWorkStudio/src/rwslibs/engineeringrequirements `
        RobWorkStudio/src/rwslibs/kinematicanalysis `
        RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "test: accept portable 300kg robot projects"
```

---

### Task 7: Final review and clean verification

**Files:**
- Verify only; modify production or test files only when a failing regression identifies a
  scoped defect.

- [ ] **Step 1: Run specification review**

Review every requirement in
`docs/superpowers/specs/2026-08-02-robot-file-main-flow-hardening-design.md` against the commits.
Fix every Critical or Important gap and repeat review.

- [ ] **Step 2: Run quality review**

Inspect failure rollback, path containment, symlink/canonical-path handling, registry lifecycle,
old exported symbols, test isolation, and source-tree writes. Fix every Critical or Important
finding and repeat review.

- [ ] **Step 3: Clean and rebuild focused targets**

Initialize the MSVC environment, then run:

```powershell
cmake --build . --target clean --config Debug
cmake --build . --config Debug --target `
  sdurws_sdurws-gtest `
  sdurws_robotmodelbuilder_xmltest `
  sdurws_robotmodelbuilder_widgettest `
  sdurws_robotmodelbuilder_jsontest `
  sdurws_robotmodelbuilder_workcellconvertertest `
  sdurws_engineeringrequirements_test `
  sdurws_robotanalysiscore_test `
  sdurws_kinematicanalysis_test `
  sdurws_structureoptimizer_test
```

If a runtime popup, `bad_alloc`, or abort appears after public C++ layout changes, stop the
incremental run, clean again, and rebuild all dependencies before diagnosing application code.

- [ ] **Step 4: Run all runtime suites sequentially**

With `QT_QPA_PLATFORM=windows` on Windows:

```powershell
.\sdurws_robotmodelbuilder_xmltest.exe
.\sdurws_robotmodelbuilder_widgettest.exe
.\sdurws_robotmodelbuilder_jsontest.exe
.\sdurws_robotmodelbuilder_workcellconvertertest.exe
.\sdurws_engineeringrequirements_test.exe
.\sdurws_robotanalysiscore_test.exe
.\sdurws_kinematicanalysis_test.exe
.\sdurws_sdurws-gtest.exe --gtest_filter=ProjectDocumentRegistryTest.*:ProjectSystemTest.*:RobWorkStudioTest.*RobotProject*
.\sdurws_structureoptimizer_test.exe robot_file_acceptance
.\sdurws_structureoptimizer_test.exe
```

Run registered CTests for robot-file acceptance, Widget QPA, managed project roots, dependency
reconciliation, and project context.

- [ ] **Step 5: Inspect scope and source preservation**

Run:

```powershell
git diff --check
git status --short
git log -12 --oneline
git diff --name-only b36b97c..HEAD
```

Verify the implementation did not modify:

- the ignored `300kg_urdf` source tree;
- user-owned `UR-6-85-5-A/t1/`;
- deleted UR sidecars;
- Robot-file-out-of-scope WorkCell behavior; or
- manual from-scratch RobotModelBuilder creation.

- [ ] **Step 6: Complete the phase**

Report the commit range, exact test counts/exit codes, conditional 300kg CTest registration,
review verdicts, preserved user-owned files, and any residual risk. Do not begin the manual
RobotModelBuilder phase in this task.
