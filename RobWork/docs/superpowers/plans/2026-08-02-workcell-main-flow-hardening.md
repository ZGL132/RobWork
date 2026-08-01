# Create Project from WorkCell Main Flow Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a project created from `UR.wc.xml` loadable, transactionally editable, portable after clone, and consumable by frozen requirements, kinematic analysis, and structure optimization.

**Architecture:** `ProjectManager` validates the copied WorkCell before activating its manifest; stable project resource IDs remain authoritative. Frozen scenario paths stay in schema 3 but become project-relative, with an explicit artifact base directory supplied during validation and candidate construction. Tests build all generated inputs in temporary directories and never depend on mutable sidecars beside the sample WorkCell.

**Tech Stack:** C++17, Qt 6 Core/Widgets, RobWork WorkCell loaders, GoogleTest, existing standalone plugin test executables, CMake/Ninja/MSVC.

---

### Task 1: Validate WorkCell Migration Before Project Activation

**Files:**
- Modify: `RobWorkStudio/src/rws/ProjectManager.cpp`
- Test: `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`

- [ ] **Step 1: Add a failing real-loader migration test**

Add a test that calls `createProjectFromWorkCell` with the repository `UR.wc.xml`, resolves
`scene.main`, loads it with `rw::loaders::WorkCellLoader::Factory::load`, verifies the UR device and
all 16 geometry resources, and confirms that no source file changed.

```cpp
TEST (ProjectSystemTest, ManagerCreatesLoadableProjectFromTopLevelUrDevice)
{
    QTemporaryDir target;
    const QString source = sourcePath (
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml");
    const QByteArray sourceBefore = readFile (source);
    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProjectFromWorkCell (
        QDir (target.path ()).filePath ("UrProject/UrProject.rwproj"), source, &error))
        << error.toStdString ();
    QString scenePath;
    ASSERT_TRUE (manager.resolveResource ("scene.main", scenePath, &error));
    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load (scenePath.toStdString ());
    ASSERT_FALSE (workcell.isNull ());
    EXPECT_FALSE (workcell->findDevice ("UR-6-85-5-A").isNull ());
    EXPECT_EQ (sourceBefore, readFile (source));
    EXPECT_EQ (16, managedGeometryResourceCount (manager.manifest ()));
}
```

- [ ] **Step 2: Add a failing rollback test for loader-invalid XML**

Create well-formed XML with an invalid RobWork root or invalid referenced device, call migration,
and assert false, no `.rwproj`, no copied `scenes/main.wc.xml`, and unchanged manager context.

- [ ] **Step 3: Run the project test and verify RED**

Run:

```powershell
cmake --build . --target sdurws_sdurws-gtest --config Debug
RobWorkStudio\bin\sdurws_sdurws-gtest.exe --gtest_filter=ProjectSystemTest.ManagerCreatesLoadableProjectFromTopLevelUrDevice:ProjectSystemTest.ManagerRejectsCopiedWorkCellThatRobWorkCannotLoad
```

Expected: the invalid-loader test fails because migration currently commits any well-formed XML.

- [ ] **Step 4: Add pre-activation WorkCell loading**

After dependency copying and before `createProject`, load `targetWorkCell` with the real factory.
Catch `rw::core::Exception` and `std::exception`, return an error containing the copied scene path,
and call `removeCopiedWorkCellDependencies` on failure.

```cpp
try {
    const rw::models::WorkCell::Ptr copied =
        rw::loaders::WorkCellLoader::Factory::load (targetWorkCell.toStdString ());
    if (copied.isNull ()) {
        setError (error, QString::fromUtf8 ("复制后的 WorkCell 无法加载：%1。").arg (targetWorkCell));
        removeCopiedWorkCellDependencies (copiedTargetPaths);
        return false;
    }
}
catch (const std::exception& exception) {
    setError (error, QString::fromUtf8 ("复制后的 WorkCell 校验失败：%1：%2。")
                         .arg (targetWorkCell, QString::fromUtf8 (exception.what ())));
    removeCopiedWorkCellDependencies (copiedTargetPaths);
    return false;
}
```

- [ ] **Step 5: Run focused and existing project tests GREEN**

Run the two focused tests, then the full `ProjectSystemTest.*` suite. Expected: all pass.

- [ ] **Step 6: Commit Task 1**

```powershell
git add RobWorkStudio/src/rws/ProjectManager.cpp RobWorkStudio/gtest/rws/ProjectSystemTest.cpp
git commit -m "fix: validate migrated WorkCell before project activation"
```

### Task 2: Make Frozen Scenario Paths Portable

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] **Step 1: Add a failing portable-freeze test**

Copy the UR fixture to a temporary project, load it, convert its model, and freeze with an explicit
project root. Assert that `sourceWorkCellPath`, `sceneSpec.saveDirectory`, drawable paths, collision
paths, and scene-geometry paths contain no absolute paths and that the snapshot fingerprint is valid.

```cpp
rws::FrozenRequirementArtifact artifact;
REQUIRE (rws::RequirementFreezer::freeze (
    requirements, *workcell, state, model, artifact, &error, projectRoot.toStdString ()));
REQUIRE (!QFileInfo (QString::fromStdString (artifact.scenario.sourceWorkCellPath)).isAbsolute ());
REQUIRE (!QFileInfo (QString::fromStdString (artifact.scenario.sceneSpec.saveDirectory)).isAbsolute ());
for (const rws::DrawableSpec& drawable : artifact.scenario.sceneSpec.drawables)
    REQUIRE (!QFileInfo (QString::fromStdString (drawable.filePath)).isAbsolute ());
```

- [ ] **Step 2: Run the requirements test and verify RED**

Run `sdurws_engineeringrequirements_test.exe`. Expected: absolute path assertions fail.

- [ ] **Step 3: Add explicit project-root path normalization**

Extend `RequirementFreezer::freeze` with a final optional `const std::string& projectRoot = {}`
parameter. Add focused helpers that relativize only paths inside the normalized project root and
leave standalone inputs unchanged. Normalize `sourceWorkCellPath`, `sceneSpec.saveDirectory`, and
all file-backed geometry paths before computing `snapshotFingerprint`.

```cpp
static bool makeProjectRelative(const QString& root, std::string& value);
static void makeScenarioPortable(FrozenWorkCellScenarioSnapshot& snapshot,
                                 const QString& projectRoot);
```

Do not scan directories and do not relativize a path outside the project root.

- [ ] **Step 4: Pass the widget project root into freeze**

Update `EngineeringRequirementsWidget::freezeRequirements` to pass
`_projectOutputDirectory.toStdString()` as the last argument.

- [ ] **Step 5: Run requirements tests GREEN**

Run the full requirements test executable, including its `widget` suite. Expected: all pass.

- [ ] **Step 6: Commit Task 2**

```powershell
git add RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.* RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp
git commit -m "fix: persist portable frozen WorkCell scenarios"
```

### Task 3: Resolve Portable Scenarios in Both Consumers

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/FrozenRequirementKinematicAdapter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/FrozenRequirementKinematicAdapter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/FrozenRequirementProjectImportService.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CandidateModelFactory.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CandidateModelFactory.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: Add failing relocation tests**

Create a portable artifact under temporary project A, copy its managed files to project B, make A
unavailable, and assert:

```cpp
REQUIRE (rws::FrozenRequirementKinematicAdapter::applyWithValidation (
    artifact, *clonedWorkCell, clonedState, points, &error, &stateChanged, &warnings,
    clonedProjectRoot.toStdString ()));
REQUIRE (rws::FrozenRequirementProjectImportService::createProblem (
    clonedRequirementPath, *clonedWorkCell, clonedState, problem, &validation, &error));
rws::CandidateModelBuildRequest request;
request.spec = problem.context.modelSpec;
request.scenarioSnapshot = &problem.scenarioSnapshot;
request.scenarioBaseDirectory = clonedProjectRoot.toStdString ();
REQUIRE (rws::CandidateModelFactory ().build (request).ok);
```

- [ ] **Step 2: Run both tests and verify RED**

Expected failures: relative source fingerprint lookup uses the process CWD, and candidate geometry
resolution uses the serialized relative `saveDirectory` without the clone root.

- [ ] **Step 3: Add explicit artifact-base resolution**

Extend `RequirementFreezer::validateScenario` and `isScenarioCurrent` with an optional artifact base
directory. Resolve relative provenance paths against that base for fingerprint warnings, without
rewriting the serialized artifact or changing its fingerprint.

- [ ] **Step 4: Pass the base directory through KinematicAnalysis**

Extend `FrozenRequirementKinematicAdapter::applyWithValidation` with the optional base directory.
In `KinematicAnalysisWidget`, pass the project root for a managed resource and the requirement
file's directory for an external copy.

- [ ] **Step 5: Resolve optimization scenario assets without scanning**

Add `scenarioBaseDirectory` to `CandidateModelBuildRequest`. Change
`resolveExternalAssetPaths(RobotModelSpec&, const std::string& baseDirectory = {})` so relative
scenario paths resolve against the supplied project root. Store the requirement's project root in
`StructureOptimizationScenarioSnapshot` or the optimization context so preview, evaluator, and
exporter all pass the same explicit base.

- [ ] **Step 6: Run kinematic and optimizer tests GREEN**

Run `sdurws_kinematicanalysis_test.exe` and `sdurws_structureoptimizer_test.exe`. Expected: portable
relocation tests pass and candidate loading succeeds with the original directory unavailable.

- [ ] **Step 7: Commit Task 3**

```powershell
git add RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.* RobWorkStudio/src/rwslibs/kinematicanalysis RobWorkStudio/src/rwslibs/structureoptimizer
git commit -m "fix: resolve frozen scenarios from managed project roots"
```

### Task 4: Complete Manifest Dependencies and Remove Mutable Fixtures

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Test: `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`

- [ ] **Step 1: Add a failing optimizer resource dependency test**

Assert that first optimizer document creation registers these existing resources exactly once:

```cpp
EXPECT_TRUE (resource.dependencies.contains ("scene.main"));
EXPECT_TRUE (resource.dependencies.contains ("robot-model.main"));
EXPECT_TRUE (resource.dependencies.contains ("engineering-requirements.main"));
EXPECT_EQ (resource.dependencies.size (), QSet<QString> (resource.dependencies.begin (),
                                                          resource.dependencies.end ()).size ());
```

- [ ] **Step 2: Run and verify RED**

Expected: only `scene.main` is currently present.

- [ ] **Step 3: Register all resolvable upstream dependencies**

In `StructureOptimizerPlugin`, use `resolveProjectResource` only to test whether each stable upstream
ID is declared/resolvable, append it once, and retain support for standalone optimizer projects.

- [ ] **Step 4: Replace `testAcceptedUr6585AProject` mutable sidecars**

Generate its `RobotModelSpec` and optimization JSON under `QTemporaryDir` from `UR.wc.xml` using
`WorkCellConverter` and `StructureOptimizationProjectFactory`. Remove all reads of
`UR-6-85-5-A.rmb.json` and `UR-6-85-5-A.structure-optimization.json` beside the fixture.

- [ ] **Step 5: Run full optimizer and project tests GREEN**

Expected: no failure depends on deleted/untracked sample sidecars.

- [ ] **Step 6: Commit Task 4**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/gtest/rws/ProjectSystemTest.cpp
git commit -m "test: make WorkCell optimization flow self-contained"
```

### Task 5: End-to-End Verification

**Files:**
- Verify all files modified above.

- [ ] **Step 1: Build from a configured MSVC developer environment**

Run the focused targets with the Visual Studio developer environment initialized. Expected: build
exit code 0; no `array: No such file or directory` environment failure.

- [ ] **Step 2: Run focused suites**

Run project-system, RobotModelBuilder converter/widget, EngineeringRequirements, KinematicAnalysis,
RobotAnalysisCore, and StructureOptimizer tests. Expected: all exit 0.

- [ ] **Step 3: Verify source-tree cleanliness constraints**

Run:

```powershell
git diff --check
git status --short
```

Confirm no test generated files beside `UR.wc.xml`, and existing user-owned deletions/untracked files
remain untouched.

- [ ] **Step 4: Inspect the final resource graph**

Create the temporary UR project through the integration test and assert stable IDs, project-relative
paths, no duplicate resources, and clone re-open success after the original temporary project is
removed.
