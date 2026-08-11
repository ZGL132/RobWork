# Kinematic Diagnose Solution Inspector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Kinematic Analysis use Jog-native workflow tabs and present Diagnose as an IK-candidate master/detail inspector while removing retired Current TCP UI code.

**Architecture:** Preserve complete candidate diagnostics from `TargetEvaluator` through `KinematicIkSolution`, then render one stable selected-candidate snapshot in a unified Diagnose page. Replace the custom tab shell with native `QTabWidget`; keep current-pose analysis only for reports and isolate all stale/collision/apply transitions in explicit widget helpers.

**Tech Stack:** C++17, Qt Widgets, RobWorkStudio, CMake/MSVC, `sdurws_kinematicanalysis_test`.

**Working-tree rule:** The kinematic-analysis target directory is clean at planning time, but the repository has unrelated user changes (including the confirmed spec and model-data files). Do not edit, stage, commit, or revert those unrelated paths. Feature commits may contain only the exact kinematic-analysis files listed by the current task, added with path-scoped `git add` commands.

**Baseline:** The existing Release executable passes both `ik` and `workflow_ui`. Treat a new failure in either suite as a regression from this work.

---

## File Map

- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`: candidate diagnostic and collision evidence value types.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/ConfigurationEvaluator.hpp`: testable collision-query completion boundary.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/ConfigurationEvaluator.cpp`: truthful collision-query completion semantics.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`: explicit legacy conversion contract and backward-compatible IK request signature.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`: forward complete `ConfigurationEvaluation` candidate evidence and remove unreachable legacy IK code.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.hpp`: pure best-candidate, collision-evidence, Jacobian-status, and Apply-safety declarations.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.cpp`: deterministic implementations shared by tests and the Widget.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`: unified Diagnose widget members and state helpers.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`: native tabs, Diagnose layout, inspector rendering, selection, stale/apply behavior, and old UI removal.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`: data, interaction, safety, geometry, and regression tests.

### Task 1: Preserve Complete Candidate Diagnostics

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/ConfigurationEvaluator.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/ConfigurationEvaluator.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add failing conversion and collision-query tests**

`legacyIkResultFromTarget` is currently translation-unit local, so first write the test against the public signature that Step 3 will add. Use a synthetic `TargetEvaluation` to avoid matching independently sorted real-device candidates:

```cpp
rws::TargetEvaluation evaluation;
evaluation.feasibility = rws::Feasibility::Feasible;
evaluation.quality = rws::Quality::Good;
rws::TargetCandidate candidate;
candidate.configuration.feasibility = rws::Feasibility::Feasible;
candidate.configuration.quality = rws::Quality::Good;
candidate.configuration.q = rw::math::Q (2, 0.25, -0.5);
candidate.configuration.jointLimitMargins = {0.4, 0.3};
candidate.configuration.minimumJointMargin = 0.3;
candidate.configuration.jacobianRows = 2;
candidate.configuration.jacobianCols = 2;
candidate.configuration.jacobianRowMajor = {1.0, 2.0, 3.0, 4.0};
candidate.configuration.singularValues = {5.0, 0.5};
candidate.configuration.collisionChecked = false;
candidate.positionErrorMeters = 0.0002;
candidate.orientationErrorDeg = 0.2;
evaluation.candidates.push_back (candidate);

const rws::KinematicIkAnalysisResult converted =
    rws::legacyIkResultFromTarget (evaluation, rws::TaskPoint (), true);
const rws::KinematicIkSolution& solution = converted.solutions.front ();
require (converted.collisionCheckRequested,
         "legacy IK result owns collision request metadata");
require (solution.jointLimitMargins == candidate.configuration.jointLimitMargins,
         "legacy IK solution preserves per-joint margins");
require (solution.jacobianRows == candidate.configuration.jacobianRows &&
             solution.jacobianCols == candidate.configuration.jacobianCols &&
             solution.jacobianRowMajor == candidate.configuration.jacobianRowMajor,
         "legacy IK solution preserves Jacobian evidence");
require (solution.singularValues == candidate.configuration.singularValues,
         "legacy IK solution preserves singular values");
require (solution.collisionChecked == candidate.configuration.collisionChecked,
         "legacy IK solution preserves completed collision evidence");
```

In the `configuration` suite, test the exact exception boundary used by production:

```cpp
const rws::KinematicCollisionQueryOutcome outcome =
    rws::runKinematicCollisionQuery ([] () -> bool {
        throw std::runtime_error ("detector failed");
    });
require (!outcome.completed && !outcome.inCollision &&
             outcome.error == "detector failed",
         "detector exceptions remain unavailable evidence");
```

- [ ] **Step 2: Verify RED**

Build and run:

```powershell
cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_kinematicanalysis_test --config Release
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release -C Release -R "sdurws_kinematicanalysis_test_(ik|configuration)$" --output-on-failure
```

Expected: compilation fails because the new fields, public conversion signature, and query outcome API do not exist.

- [ ] **Step 3: Add the minimal result contract**

Add candidate fields without removing compatibility fields:

```cpp
enum class KinematicIkCollisionEvidence
{
    NotEvaluated,
    Unavailable,
    Clear,
    Collision
};

struct KinematicIkSolution
{
    // existing fields remain
    std::vector< double > jointLimitMargins;
    std::vector< double > jacobianRowMajor;
    int jacobianRows = 0;
    int jacobianCols = 0;
    std::vector< double > singularValues;
    bool collisionChecked = false;
};

struct KinematicIkAnalysisResult
{
    // existing fields remain
    bool collisionCheckRequested = false;
};
```

Do not store a second mutable `collisionEvidence` or aggregate `collisionCheckUnavailable` field: either can disagree with `collisionCheckRequested`, `collisionChecked`, and `inCollision`. Derive the four-state presentation canonically in `KinematicAnalysisUiLogic`:

```cpp
KinematicIkCollisionEvidence ikCollisionEvidence (
    const KinematicIkAnalysisResult& result,
    const KinematicIkSolution& solution);

bool canApplyIkSolution (
    const KinematicIkAnalysisResult& result,
    const KinematicIkSolution& solution,
    bool stale);
```

The mapping is exactly: request false -> `NotEvaluated`; request true plus `collisionChecked == false` -> `Unavailable`; completed plus `inCollision == false` -> `Clear`; completed plus `inCollision == true` -> `Collision`. Apply requires non-stale, non-Fail, non-collision, and either no request or `Clear`.

Declare the conversion in `KinematicAnalyzer.hpp` and pass request intent explicitly:

```cpp
KinematicIkAnalysisResult legacyIkResultFromTarget (
    const TargetEvaluation& evaluation,
    const TaskPoint& target,
    bool collisionCheckRequested);
```

In `legacyIkResultFromTarget`, copy every diagnostic field directly from `candidate.configuration`; do not recompute or read Studio state.

- [ ] **Step 4: Make collision completion truthful and testable**

Declare this small production boundary in `ConfigurationEvaluator.hpp`:

```cpp
struct KinematicCollisionQueryOutcome
{
    bool completed = false;
    bool inCollision = false;
    std::string error;
};

KinematicCollisionQueryOutcome runKinematicCollisionQuery (
    const std::function< bool () >& query) noexcept;
```

Include `<functional>` and `<string>`, then implement:

```cpp
KinematicCollisionQueryOutcome rws::runKinematicCollisionQuery (
    const std::function< bool () >& query) noexcept
{
    KinematicCollisionQueryOutcome outcome;
    try {
        outcome.inCollision = query ();
        outcome.completed = true;
    }
    catch (const std::exception& ex) {
        outcome.error = ex.what ();
    }
    catch (...) {
        outcome.error = "Unknown collision detector error.";
    }
    return outcome;
}
```

In `ConfigurationEvaluator`, call it around `detector->inCollision(...)`, set `collisionChecked = true` only after `completed`, and on failure append `CollisionDetectorUnavailable` plus `KIN_COLLISION_QUERY_FAILED`, set feasibility to `DataInsufficient`, and retain Q/FK/margins/Jacobian/singular evidence. This lower-level callable is required because a real RobWork detector is not a reliable way to force an exception in a unit test.

- [ ] **Step 5: Preserve request intent through `analyzeIk`**

Extend the analyzer with a trailing default so existing callers remain source-compatible:

```cpp
KinematicIkAnalysisResult analyzeIk (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const TaskPoint& target,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL,
    bool collisionCheckRequested = false) const;
```

Use `collisionCheckRequested || collisionDetector != NULL` for `TargetEvaluationOptions::checkCollision` and the converter argument. This lets the Widget represent requested-but-unavailable when the detector pointer is null without changing legacy five-argument callers.

- [ ] **Step 6: Verify GREEN**

Run the Task 1 command. Expected: `ik` and `configuration` pass.

Checkpoint commit (stage no other paths):

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/ConfigurationEvaluator.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/ConfigurationEvaluator.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: preserve IK candidate diagnostics"
```

### Task 2: Replace The Tab Shell And Build The Unified Diagnose Layout

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write failing workflow-shell tests**

Update `workflow_ui` to require:

```cpp
QTabWidget* workflowTabs =
    widget.findChild< QTabWidget* > (QStringLiteral ("workflowTabs"));
require (workflowTabs != nullptr && workflowTabs->count () == 3,
         "native workflow tabs own three pages");
require (workflowTabs->tabText (0) == QStringLiteral ("Diagnose") &&
             workflowTabs->tabText (1) == QStringLiteral ("Validate") &&
             workflowTabs->tabText (2) == QStringLiteral ("Explore"),
         "workflow tab order remains stable");
require (workflowTabs->styleSheet ().isEmpty () &&
             workflowTabs->tabBar ()->styleSheet ().isEmpty (),
         "workflow tabs inherit the Jog-native Qt style");
require (widget.findChild< QTabBar* > (
             QStringLiteral ("kinematicModeTabs")) == nullptr,
         "custom mode tab shell is removed");
```

Also require no `currentTcpXLabel`, `currentTcpYLabel`, `currentTcpZLabel`, `currentTcpRollLabel`, `currentTcpPitchLabel`, `currentTcpYawLabel`, `currentPoseTab`, or `ikTab` objects.

Require both `workflowTabs->styleSheet()` and `workflowTabs->tabBar()->styleSheet()` to be empty, and assert the candidate table and Diagnose command buttons also have no plugin-local stylesheet. Remove the existing Solve-button QSS together with the tab QSS; native palette/style must own these controls.

At 300px and 320px, map each fixed command control into the scroll-area viewport and assert `viewport->rect ().contains (mappedGeometry)`. Give long native buttons zero minimum width and compressible size policies; do not add horizontal scrolling to make the test pass.

- [ ] **Step 2: Write failing Diagnose hierarchy tests**

Require `ikTargetSection`, `ikCommandGrid`, `ikSolutionTable`, `ikSolutionInspector`, and `advancedDiagnosticsToggle` in this vertical order. Require title `IK Target`, six target inputs, `Refresh and Sync TCP`, Thresholds, Collision, Duplicate Q, and Solve. Assert the command grid is two rows and tab order places Solve last.

- [ ] **Step 3: Verify RED**

Run:

```powershell
cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_kinematicanalysis_test --config Release
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release -C Release -R "sdurws_kinematicanalysis_test_workflow_ui$" --output-on-failure
```

Expected: failure because the custom tab shell and old Current TCP hierarchy still exist.

- [ ] **Step 4: Implement the native shell**

Replace `_modeTabs` and `_modeStack` with `_workflowTabs`. Wrap each workflow page in its existing `QScrollArea`, then add those scroll areas directly to the native `QTabWidget`. Connect `currentChanged` only where workflow state refresh requires it; remove custom QSS and obsolete selector connections.

Update `openSelectedTaskPointInIk()` and every other workflow-routing caller to use `_workflowTabs->setCurrentIndex(0)`. Remove `_modeStack->setCurrentIndex` calls rather than keeping parallel page state.

- [ ] **Step 5: Build one Diagnose page**

Create one vertical page. Build an IK-only six-row target form. Use a fixed two-row `QGridLayout` for commands:

```text
row 0: Refresh and Sync TCP | Thresholds | Collision
row 1: Duplicate Q label    | spin box   | Solve
```

Place the candidate filter/count row, solution table, inspector, and Advanced diagnostics after it. Use native controls and existing compact-table helpers; do not introduce QSS.

At 300px, the literal `Refresh and Sync TCP`, `Thresholds`, and `Collision` controls can exceed their natural-width sum. Give all three grid columns stretch, zero minimum width, and shrinking horizontal policies; verify the full button texts remain visible before accepting the geometry. Do not shorten the approved labels to make the test pass.

Remove the existing local QSS from both `ikSyncCurrentTcpButton` and `ikSolveButton` so every Diagnose command inherits the same native style as Jog.

- [ ] **Step 6: Verify GREEN and narrow geometry**

Run `workflow_ui`. Resize to 300x620 and 320x620 in the test; require no fixed command or target control extends outside its page viewport and horizontal scrolling remains disabled.

Checkpoint commit:

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: unify the Diagnose workflow layout"
```

### Task 3: Implement Stable Selection And The Candidate Inspector

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write failing pure selection tests**

There is no existing test hook for assigning the Widget's private `_lastIkResult`; do not add a production test setter. Put the deterministic policy in `KinematicAnalysisUiLogic.hpp/.cpp` and test it directly with Pass, Warning, collision, and Fail candidates:

```cpp
int preferredIkSolutionIndex (
    const std::vector< KinematicIkSolution >& solutions,
    const std::vector< int >& visibleIndices);

AnalysisStatus ikJacobianStatus (const KinematicIkSolution& solution);
```

`preferredIkSolutionIndex` performs three stable passes over valid visible indices: first non-collision Pass, then non-collision Warning, then the first remaining candidate; empty input returns `-1`. `ikJacobianStatus` returns Fail for `Singular`, Warning for `NearSingular`, Pass for valid dimensions/singular values without those reasons, and Unknown when the snapshot is incomplete. Require:

- default filter text is `All candidates`;
- the best stable index is the first non-collision Pass, then Warning, then remaining;
- user-visible candidate numbers are one-based;
- selecting another table row changes `Selected solution #N`;
- hiding the selected candidate reruns the same priority over visible candidates;
- count text is exactly `Displayed X of Y`.

- [ ] **Step 2: Write failing observable inspector-data tests**

Use the existing real-device `workflow_ui` solve path. Read the stable index from each row's `Qt::UserRole + 1`, then compare every visible inspector field with the corresponding row/result evidence. If the fixture returns multiple candidates, select each row and verify the fields move together; if it returns one, keep the single-row end-to-end assertions and rely on the pure synthetic tests for multi-candidate priority. Do not make the UI suite nondeterministically require multiple IK branches.

Give the core conversion test candidates deliberately different status, condition, manipulability, margin, residuals, Q, failure reasons, collision completion, joint margins, and singular values. This proves the immutable source consumed by the Widget is candidate-owned; the Widget test separately proves it no longer renders `_lastCurrentPose` labels.

- [ ] **Step 3: Verify RED**

Run `workflow_ui` and `ik`; expect missing inspector object names/selection behavior.

- [ ] **Step 4: Implement stable selection helpers**

Add focused private helpers:

```cpp
int bestVisibleIkSolutionIndex () const;
int selectedIkSolutionIndex () const;
const KinematicIkSolution* selectedIkSolution () const;
void selectIkSolutionIndex (int solutionIndex);
void clearIkSolutionInspector (const QString& stateText);
void updateIkSolutionInspector ();
```

Store the stable zero-based index in `Qt::UserRole + 1`; display `index + 1`. Preserve a user selection only while its stable index remains visible.

Also store explicit presentation intent so programmatic row selection cannot be mistaken for a user choice:

```cpp
int _bestIkSolutionIndex = -1;
int _selectedIkSolutionIndex = -1;
bool _selectedIkIsBest = false;
bool _selectingIkProgrammatically = false;
```

After Solve or automatic filter fallback set `_selectedIkIsBest = true`; a user row selection sets it false. Use `_selectingIkProgrammatically` around `selectRow()` to prevent the selection signal from overwriting that decision. Filter fallback recomputes the best visible index and resets `_selectedIkIsBest`; only a direct row selection produces a `Selected solution #N` heading.

- [ ] **Step 5: Render the master table and inspector**

Use table columns `#`, `Status`, `Position error`, `Orientation error`, `Min margin`. Replace aggregate result labels with `Displayed X of Y`.

The inspector contains:

- heading `Best solution #N` or `Selected solution #N`;
- one four-cell health row: Status, Condition, Manipulability, Min joint margin;
- compact evidence rows for residuals, collision, Distance from solve start, Q, and conditional failure reasons;
- Apply selected Q in the inspector heading.

Render Q in a read-only selectable `QLineEdit`; set both its full text and tooltip to `qVectorText(solution.q)`. Render Distance from solve start only from the immutable `distanceToCurrentQ` solve-start snapshot and never compare against live `currentState()`.

- [ ] **Step 6: Render candidate Advanced diagnostics**

Keep Advanced diagnostics last and collapsed. Populate Joint status from selected candidate Q plus `jointLimitMargins`. Replace full Jacobian and singular tables with one label containing dimensions, sigma min/max, condition, and singular-metric Pass/Warning/Fail; put the full singular list in its tooltip.

Use this fixed summary shape so tests do not depend on prose:

```cpp
tr ("%1x%2 | sigma min %3 | sigma max %4 | Condition %5 | %6")
```

Compute min/max only from the immutable `singularValues` vector and obtain the final status from `ikJacobianStatus(solution)`, never `solution.status`. Rendering or selecting a candidate must never call `setChecked(true)` on the Advanced toggle.

- [ ] **Step 7: Verify GREEN**

Run `workflow_ui`, `ik`, and `current_pose`. Expected: all pass; current-pose report analysis remains available while Diagnose follows candidate data.

Checkpoint commit:

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: render the selected IK solution inspector"
```

### Task 4: Make Solve, Stale, Collision, Sync, And Apply States Deterministic

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write failing state-transition tests**

Test Ready, Solving, Error/no result, Solved, No candidates, all-Fail selection, Stale, and Filter empty. Verify Solve start discards the previous immutable result and every early return leaves no selectable row or enabled Apply action.

Use the pure preferred-index test for the synthetic all-Fail case. Widget tests should observe states through normal controls, labels, table selection, and Apply; do not expose private result mutation solely for tests.

- [ ] **Step 2: Write failing stale-trigger tests**

Seed a solved result, then independently change target, Device, TCP, Thresholds, Collision, Duplicate Q, and emit an external Studio state change. Each must clear presentation and disable Apply. Length/angle unit and candidate-filter changes must not discard `_lastIkResult`.

Drive the external event through the real Studio API, not a private-method test hook:

```cpp
rw::core::PropertyMap properties;
rws::RobWorkStudio studio (properties);
studio.setWorkCell (workcell);
studio.setState (workcell->getDefaultState ());
widget.setRobWorkStudio (&studio);
widget.setWorkCell (workcell.get ());
studio.setState (changedState);
```

Add a second valid Device/TCP choice to the fixture so those two triggers are deterministic. Change a legacy threshold backing spin and invoke the existing `applyThresholds` slot directly to cover accepted threshold changes without driving a modal dialog. Re-solve before each independent trigger.

- [ ] **Step 3: Write failing collision-safety tests**

Require candidate text and Apply state for:

```text
NotEvaluated -> "Not evaluated"; Apply allowed when candidate otherwise usable
Unavailable  -> "Unavailable"; Apply disabled when Collision was requested
Clear        -> "Clear"; Apply allowed when candidate otherwise usable
Collision    -> "Collision"; Apply disabled
```

Include detector-unavailable and detector-query-exception cases.

- [ ] **Step 4: Write failing Refresh/Apply tests**

Require `Refresh and Sync TCP` to refresh `_lastCurrentPose`, copy the current TCP target, invalidate prior IK, and not solve. Require Apply to guard its own Studio state callback, retain the selected immutable evidence, refresh report/current-pose data, and permit a later external state change to mark results stale.

Split the retired current-pose presentation method into a report snapshot helper and the command slot:

```cpp
bool refreshCurrentPoseSnapshot (QString* error = nullptr);
void refreshAndSyncTcp ();
```

`refreshAndSyncTcp()` must call the snapshot helper first, write all six target spins under six `QSignalBlocker`s, then invalidate once. `refreshCurrentPoseSnapshot()` updates `_lastCurrentPose` and report data only; it must not populate candidate health, joint, or Jacobian widgets.

- [ ] **Step 5: Verify RED**

Run `workflow_ui`, `ik`, `current_pose`, `thresholds`, `report`, and `cache`; expected state/safety assertions fail before implementation.

- [ ] **Step 6: Implement one invalidation path**

Centralize state clearing:

```cpp
enum class IkPresentationState
{
    Ready,
    Solving,
    Solved,
    NoCandidates,
    FilterEmpty,
    Stale,
    Error
};

IkPresentationState _ikPresentationState = IkPresentationState::Ready;
bool _applyingSelectedIkSolution = false;

void setIkPresentationState (IkPresentationState state, const QString& message);
void clearIkResultSnapshot (IkPresentationState state, const QString& message);
void invalidateIkResultPresentation (const QString& reason);
void beginIkSolvePresentation ();
void finishIkSolvePresentation ();
```

Make `setIkPresentationState` the only writer for state labels and empty-state inspector text. `beginIkSolvePresentation` clears `_lastIkResult`, collision request metadata, both stable indices, table, inspector, advanced rows/summary, and Apply before validation. Every validation/setup early return calls `clearIkResultSnapshot(Error, specificMessage)`. A successful empty result enters `NoCandidates` with the analyzer failure reason; a non-empty result enters `Solved`, including all-Fail results. `FilterEmpty` clears visible selection/inspector but retains `_lastIkResult`; unit changes retain and reformat both snapshot and selection.

Use fixed user-facing state text (`Ready`, `Solving`, `Solved`, `No candidates`, `Filter empty`, `Stale`, `Error`) plus the specific detail message; do not leave state wording distributed across early-return branches.

Connect target, Device, TCP, accepted Thresholds, Collision, Duplicate Q, and external Studio state exactly once. WorkCell/project construction or load enters Ready. Use `QSignalBlocker` while Refresh and Sync or unit conversion writes target controls. Use `QScopedValueRollback<bool>` for `_applyingSelectedIkSolution` so exceptions cannot leave the Studio-state guard set.

`applyProjectDocumentSnapshot()` must suppress intermediate control signals and finish in Ready with no previous solve snapshot. Replace all remaining callers of the retired `refreshCurrentPose()` presentation path (including task-point Apply and report/visual refresh paths) with the report-only current-pose snapshot helper, then verify with `rg -n "refreshCurrentPose"`.

- [ ] **Step 7: Pass collision request intent into the immutable result**

Call the extended analyzer with detector and checkbox intent:

```cpp
const KinematicIkAnalysisResult result = analyzer.analyzeIk (
    device, tcpFrame, solveStartState, target, collisionDetector, checkCollision);
```

Use `ikCollisionEvidence(result, candidate)` and `canApplyIkSolution(result, candidate, stale)` everywhere. Do not store a parallel aggregate unavailable boolean, do not keep a Widget-only collision flag, and do not consult the current checkbox after Solve.

- [ ] **Step 8: Verify GREEN**

Run the Task 4 suites and confirm all transition and safety cases pass.

Checkpoint commit:

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "fix: make IK inspector transitions deterministic"
```

### Task 5: Remove Retired Code And Run Full Regression

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add cleanup assertions**

Keep negative UI assertions for retired object names and add source searches to the verification checklist for `_currentTcpValueLabels`, `_poseCollisionCapabilityLabel`, `_warningLabel`, `_jacobianTable`, `_singularTable`, `_modeTabs`, `_modeStack`, `_currentPoseTab`, and `_ikTab`.

- [ ] **Step 2: Remove old UI members and paths**

Delete retired constructor initialization, widget construction, population, signal connections, comments, and no-longer-called helpers. Keep `_lastCurrentPose`, `refreshCurrentPoseSnapshot`, report/export consumers, and all Validate/Explore state backing.

Search before deleting each symbol. Remove aggregate IK summary fields (`_ikStatusLabel`, `_ikUsableLabel`, `_ikPassLabel`, `_ikWarningLabel`, `_ikFailLabel`), the retired details table, `isCurrentIkSolution`, and `makeQItem` only when `rg` confirms no Validate/Explore/report caller. Keep shared `setDetailRow` and `isUsableIkSolution` while task-point/workspace callers remain.

- [ ] **Step 3: Remove unreachable analyzer code**

Delete the old `KinematicAnalyzer::analyzeIk` implementation after:

```cpp
const TargetEvaluation evaluation = TargetEvaluator ().evaluate (context, target, options);
return legacyIkResultFromTarget (evaluation, target);
```

Do not change the live `TargetEvaluator` path.

- [ ] **Step 4: Run source and diff checks**

```powershell
rg -n "_currentTcpValueLabels|_poseCollisionCapabilityLabel|_warningLabel|_jacobianTable|_singularTable|_modeTabs|_modeStack|_currentPoseTab|_ikTab|_ikStatusLabel|_ikUsableLabel|_ikPassLabel|_ikWarningLabel|_ikFailLabel|_ikDetailTable|isCurrentIkSolution|makeQItem" RobWorkStudio/src/rwslibs/kinematicanalysis
rg -n "QTabBar::tab|Collision capability|Singular values|No active warnings" RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp
git diff --check -- RobWorkStudio/src/rwslibs/kinematicanalysis
git diff --stat -- RobWorkStudio/src/rwslibs/kinematicanalysis
```

Expected: no production references to retired members, no whitespace errors, and no changes outside the approved paths.

- [ ] **Step 5: Build the plugin and full test executable**

```powershell
cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_kinematicanalysis sdurws_kinematicanalysis_test --config Release
```

Expected: both targets build successfully.

- [ ] **Step 6: Run all kinematic-analysis tests**

```powershell
& build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe all
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release -C Release -R "^sdurws_kinematicanalysis_test" --output-on-failure
```

Expected: the complete executable and all matching registered suites pass; reject an empty CTest selection.

- [ ] **Step 7: Perform visual acceptance when launchable**

Launch `build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\RobWorkStudio.exe`, open Jog and Kinematic Analysis, and inspect at 300px and 320px Dock widths. Confirm identical native tab treatment, no clipped target/command controls, candidate-driven inspector updates, all four collision strings/Apply states, guarded Apply retention, later external-state staleness, and Advanced diagnostics remains last and collapsed. If the UI cannot be launched in the environment, record that exact verification gap.

Checkpoint commit after the full review is green:

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "refactor: remove retired Diagnose UI code"
```

## Review Gates

After every task:

1. 5.6sol checks the focused diff against this plan and the design specification.
2. 5.6sol checks code quality, dead-code removal, ownership, stale-state safety, and test adequacy.
3. Open findings return to the same 5.6terra implementer for correction and re-review.

After Task 5, 5.6sol performs a full diff review and independently reruns the final build/test commands before acceptance.
