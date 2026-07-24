# KinematicAnalysis Pose Reachability Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the remaining Pose reachability optimization gaps by making progress counts use the real execution target total and by producing reproducible UI smoke evidence for progress, cancellation, export, and table capping.

**Architecture:** Keep the existing capped diagnostics for warning the user about huge jobs, but introduce a separate uncapped execution target count for analyzer progress and UI progress labels. Keep UI changes inside `KinematicAnalysisWidget`, pure counting helpers inside `KinematicAnalysisPoseReachability`, and verification evidence in a small checked-in smoke report.

**Tech Stack:** C++ in RobWorkStudio, RobWork kinematics/IK APIs, Qt Widgets/QtConcurrent, existing `sdurws_kinematicanalysis_test` CTest executable, manual RobWorkStudio smoke with `GenericSixAxis.wc.xml`.

---

## Current Baseline

This plan assumes the current worktree already has:

- Background Pose reachability execution via `QtConcurrent::run`.
- Cooperative cancellation using `std::shared_ptr<std::atomic_bool>`.
- Cancellation checks before each position, before each IK target, and after each IK target.
- UI progress bar/label driven by `PoseReachabilityRunCallbacks::onProgress`.
- `PoseReachabilitySample::plannedIkTargets`, `completedIkTargets`, and `partial`.
- CSV/JSON export of partial metadata.
- Result table display capped to 500 rows.

## Remaining Gaps

1. `plannedPoseReachabilityTargetCount()` caps diagnostics at `MaxPoseReachabilityTargets == 1000000`, but `KinematicAnalyzer::analyzePoseReachability()` still executes all sanitized directions/rolls for all positions. If actual work exceeds the diagnostic cap, progress can exceed 100%.
2. The automatic tests pass, but there is no recorded RobWorkStudio UI smoke evidence for progress, Cancel, 500-row display cap, CSV export, and JSON export.

## File Structure

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp`
  - Add helpers for exact per-position and total execution IK target counts.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
  - Implement exact execution counting without the diagnostic `MaxPoseReachabilityTargets` cap.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
  - Use exact execution target count for progress callback denominator and sample metadata.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Use exact execution target count for progress reset and scale the progress bar safely when the count exceeds `int`.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add regression coverage proving capped diagnostics and exact progress totals are separate.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Document the difference between diagnostic cap and execution progress count.
- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilitySmoke.md`
  - Record the manual UI smoke procedure and expected evidence.

---

### Task 1: Separate Diagnostic Cap From Execution Target Count

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add helper declarations**

In `KinematicAnalysisPoseReachability.hpp`, after `plannedPoseReachabilityTargetCount(...)`, add:

```cpp
std::size_t poseReachabilityTargetsPerPosition (
    const PoseReachabilityConfig& config);

std::size_t poseReachabilityExecutionTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    bool* overflowed = nullptr);
```

- [ ] **Step 2: Include numeric limits**

In `KinematicAnalysisPoseReachability.cpp`, add this include next to the existing standard includes:

```cpp
#include <limits>
```

- [ ] **Step 3: Implement exact execution count helpers**

In `KinematicAnalysisPoseReachability.cpp`, after `plannedPoseReachabilityTargetCount(...)` and before `summarizePoseReachabilitySamples(...)`, add:

```cpp
std::size_t rws::poseReachabilityTargetsPerPosition (
    const PoseReachabilityConfig& config)
{
    const PoseReachabilityConfig sanitized =
        sanitizePoseReachabilityConfig (config, nullptr);
    if (sanitized.directionSamples <= 0)
        return 0;
    return static_cast< std::size_t > (sanitized.directionSamples) *
           static_cast< std::size_t > (sanitized.rollSamples);
}

std::size_t rws::poseReachabilityExecutionTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    bool* overflowed)
{
    const std::size_t perPosition =
        poseReachabilityTargetsPerPosition (config);
    bool capped = false;
    const std::size_t total = multiplyCapped (
        positionCount,
        perPosition,
        std::numeric_limits< std::size_t >::max (),
        &capped);
    if (overflowed != nullptr)
        *overflowed = capped;
    return total;
}
```

This helper intentionally does not use `MaxPoseReachabilityTargets`; that constant remains only for user-facing diagnostics and overflow-safe warning labels.

- [ ] **Step 4: Add helper regression test**

In `testPoseReachabilityHelpers()` in `KinematicAnalysisTest.cpp`, after the existing `planned == 720` block, add:

```cpp
{
    rws::PoseReachabilityConfig hugeConfig;
    hugeConfig.directionSamples = 1000;
    hugeConfig.rollSamples = 360;

    rws::PoseReachabilityDiagnostics diagnostics;
    const std::size_t diagnosticPlanned =
        rws::plannedPoseReachabilityTargetCount (
            hugeConfig, 3, &diagnostics);
    if (const int rc = require (
            diagnosticPlanned == rws::MaxPoseReachabilityTargets,
            "pose diagnostic target count remains capped"))
        return rc;
    if (const int rc = require (
            diagnostics.targetCountCapped,
            "pose diagnostic target count reports capped"))
        return rc;

    bool overflowed = true;
    const std::size_t executionPlanned =
        rws::poseReachabilityExecutionTargetCount (
            hugeConfig, 3, &overflowed);
    if (const int rc = require (
            executionPlanned == 1080000,
            "pose execution target count is uncapped"))
        return rc;
    if (const int rc = require (
            !overflowed,
            "pose execution target count does not overflow"))
        return rc;
    if (const int rc = require (
            rws::poseReachabilityTargetsPerPosition (hugeConfig) == 360000,
            "pose execution target count per position"))
        return rc;
}
```

- [ ] **Step 5: Run helper-focused test**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe pose_reachability'
```

Expected:

```text
KinematicAnalysis pose_reachability test passed.
```

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "fix: separate pose reachability execution target count"
```

---

### Task 2: Use Exact Execution Counts for Analyzer and UI Progress

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Update analyzer planned counts**

In `KinematicAnalyzer::analyzePoseReachability`, replace:

```cpp
PoseReachabilityDiagnostics diagnostics;
const PoseReachabilityConfig sanitized =
    sanitizePoseReachabilityConfig (config, &diagnostics);
plannedPoseReachabilityTargetCount (sanitized, positions.size (), &diagnostics);

const int directionCount = sanitized.directionSamples;
const int rollCount = directionCount == 0 ? 0 : sanitized.rollSamples;
const int totalDirections =
    static_cast< int > (diagnostics.plannedDirectionsPerPosition);
const std::size_t ikPerPosition =
    diagnostics.plannedDirectionsPerPosition;
const std::size_t plannedTotal =
    diagnostics.plannedIkTargets;
```

with:

```cpp
const PoseReachabilityConfig sanitized =
    sanitizePoseReachabilityConfig (config, nullptr);

const int directionCount = sanitized.directionSamples;
const int rollCount = directionCount == 0 ? 0 : sanitized.rollSamples;
const std::size_t ikPerPosition =
    poseReachabilityTargetsPerPosition (sanitized);
const int totalDirections = static_cast< int > (ikPerPosition);
bool targetCountOverflowed = false;
const std::size_t plannedTotal =
    poseReachabilityExecutionTargetCount (
        sanitized, positions.size (), &targetCountOverflowed);
```

No additional behavior is needed for `targetCountOverflowed`; the variable exists so the helper reports overflow explicitly and can be inspected while debugging.

- [ ] **Step 2: Ensure analyzer uses sanitized collision flag**

In the inner IK call, replace:

```cpp
config.checkCollision ? collisionDetector : NULL);
```

with:

```cpp
sanitized.checkCollision ? collisionDetector : NULL);
```

- [ ] **Step 3: Add multi-position progress regression**

In `testPoseReachability()` in `KinematicAnalysisTest.cpp`, after the existing progress callback test, add:

```cpp
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    rw::kinematics::State deviceState = stateStructure.getDefaultState ();

    std::vector< std::array< double, 3 > > twoPositions;
    twoPositions.push_back (std::array< double, 3 > {{1.0, 2.0, 3.0}});
    twoPositions.push_back (std::array< double, 3 > {{1.1, 2.0, 3.0}});

    rws::PoseReachabilityConfig progressConfig;
    progressConfig.directionSamples = 2;
    progressConfig.rollSamples = 2;
    progressConfig.checkCollision = false;

    struct MultiProgressState {
        std::size_t lastCompleted = 0;
        std::size_t lastPlanned = 0;
    } progressState;

    rws::PoseReachabilityRunCallbacks progressCb;
    progressCb.userData = &progressState;
    progressCb.onProgress = [] (std::size_t completed,
                                std::size_t planned,
                                void* userData) {
        MultiProgressState* state =
            static_cast< MultiProgressState* > (userData);
        state->lastCompleted = completed;
        state->lastPlanned = planned;
    };

    const std::vector< rws::PoseReachabilitySample > result =
        analyzer.analyzePoseReachability (
            device, device->getEnd (), deviceState, twoPositions,
            progressConfig, NULL, progressCb);

    if (const int rc = require (result.size () == 2,
                                "pose progress multi-position result count"))
        return rc;
    if (const int rc = require (progressState.lastCompleted == 8,
                                "pose progress multi-position completed count"))
        return rc;
    if (const int rc = require (progressState.lastPlanned == 8,
                                "pose progress multi-position planned count"))
        return rc;
}
```

- [ ] **Step 4: Add progress bar scaling constants**

In `KinematicAnalysisWidget.cpp`, near:

```cpp
static const std::size_t MaxPoseReachabilityTableRows = 500;
```

add:

```cpp
static const int MaxPoseReachabilityProgressBarSteps = 1000000;
```

- [ ] **Step 5: Reset UI progress using exact execution count**

In `KinematicAnalysisWidget::analyzePoseReachability`, replace:

```cpp
PoseReachabilityDiagnostics runDiag;
const std::size_t plannedTargets =
    plannedPoseReachabilityTargetCount (config, positions.size (), &runDiag);
if (_poseProgressBar != NULL) {
    _poseProgressBar->setRange (0, static_cast< int > (plannedTargets));
    _poseProgressBar->setValue (0);
}
if (_poseProgressLabel != NULL) {
    _poseProgressLabel->setText (
        tr("Progress: 0 / %1 IK target(s)")
            .arg (static_cast< int > (plannedTargets)));
}
```

with:

```cpp
bool targetCountOverflowed = false;
const std::size_t plannedTargets =
    poseReachabilityExecutionTargetCount (
        config, positions.size (), &targetCountOverflowed);
updatePoseReachabilityProgress (
    0,
    static_cast< qulonglong > (plannedTargets));
if (targetCountOverflowed && _poseProgressLabel != NULL) {
    _poseProgressLabel->setText (
        tr("Progress: 0 / overflow IK target(s)"));
}
```

- [ ] **Step 6: Scale progress bar safely**

Replace the body of `KinematicAnalysisWidget::updatePoseReachabilityProgress(...)` with:

```cpp
const qulonglong boundedCompleted = plannedTargets == 0 ? 0 :
    std::min< qulonglong > (completedTargets, plannedTargets);
const int barMax = plannedTargets >
        static_cast< qulonglong > (MaxPoseReachabilityProgressBarSteps) ?
    MaxPoseReachabilityProgressBarSteps :
    static_cast< int > (plannedTargets);
const int barValue = plannedTargets == 0 ? 0 :
    static_cast< int > (
        (static_cast< double > (boundedCompleted) /
         static_cast< double > (plannedTargets)) *
        static_cast< double > (barMax));

if (_poseProgressBar != NULL) {
    _poseProgressBar->setRange (0, barMax);
    _poseProgressBar->setValue (barValue);
}
if (_poseProgressLabel != NULL) {
    const double pct = plannedTargets == 0 ? 0.0 :
        100.0 * static_cast< double > (boundedCompleted) /
            static_cast< double > (plannedTargets);
    _poseProgressLabel->setText (
        tr("Progress: %1 / %2 IK target(s) (%3%)")
            .arg (static_cast< qulonglong > (boundedCompleted))
            .arg (static_cast< qulonglong > (plannedTargets))
            .arg (QString::number (pct, 'f', 1)));
}
```

This keeps the label exact and bounds the `QProgressBar` range to an `int`-safe value.

- [ ] **Step 7: Run analyzer tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe pose'
```

Expected:

```text
KinematicAnalysis pose test passed.
```

- [ ] **Step 8: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected:

```text
ninja: no work to do.
```

or a normal successful build ending with no errors.

- [ ] **Step 9: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "fix: use exact pose reachability progress totals"
```

---

### Task 3: Update Documentation for Diagnostic Cap vs Progress Count

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Replace diagnostic label paragraph**

In `README.md`, replace:

````markdown
The diagnostic label shows the planned IK target count (`positions 脳 directions 脳 rolls`) before running, including a "(capped)" note when the total exceeds 1,000,000.
````

with:

```markdown
The diagnostic label shows the planned IK target count (`positions * directions * rolls`) before running. For very large jobs this diagnostic number is capped at 1,000,000 and marked with "(capped)" so the label remains bounded.
```

- [ ] **Step 2: Replace progress paragraph**

In `README.md`, replace the sentence:

```markdown
Progress is reported as completed IK targets over planned IK targets via a progress bar and label.
```

with:

```markdown
Progress is reported as completed IK targets over the uncapped execution target count via a progress bar and label; the progress bar is internally scaled when the execution target count is too large for a Qt integer range.
```

Keep the rest of the paragraph about partial samples and exported metadata.

- [ ] **Step 3: Verify wording**

Run:

```powershell
rg -n "capped|uncapped execution target count|current position completes" RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
```

Expected:

```text
One or more lines mention capped diagnostics.
One line mentions uncapped execution target count.
No line contains "current position completes".
```

- [ ] **Step 4: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "docs: explain pose reachability progress counts"
```

---

### Task 4: Add and Execute Manual UI Smoke Checklist

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilitySmoke.md`

- [ ] **Step 1: Create smoke checklist**

Create `PoseReachabilitySmoke.md` with this content:

````markdown
# Pose Reachability Smoke Checklist

Date: 2026-07-18
Build: `build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug`
Executable: `build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/RobWorkStudio.exe`
WorkCell: `GenericSixAxis.wc.xml`

## Launch

Command:

```powershell
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\RobWorkStudio.exe" "D:\10_Source_Repos\21_robot\RobWork\RobWork\GenericSixAxis.wc.xml"
```

Expected:

- RobWorkStudio opens without a startup error dialog.
- KinematicAnalysis plugin can be opened.
- A valid device is selectable in the top device combo.

## Scenario 1: Small Complete Run

Inputs:

- Tab: `Pose reachability`
- Source: `Manual rows`
- Manual position row: `x=0`, `y=0`, `z=0`
- Directions: `4`
- Rolls: `2`
- Collision: unchecked

Expected:

- Diagnostic label shows `Plan: 8 IK target(s), 8 orientation(s) per position`.
- Progress label starts at `Progress: 0 / 8 IK target(s)`.
- After completion, progress label shows `Progress: 8 / 8 IK target(s) (100.0%)`.
- Status line says `Pose reachability completed for 1 position(s).`
- Summary line shows `Positions: 1`, `Shown: 1`, and `Partial: 0`.
- Export CSV is enabled.

## Scenario 2: Cancel Run

Inputs:

- Source: `Manual rows`
- Add exactly two manual position rows: `0,0,0` and `0.1,0,0`.
- Directions: `100`
- Rolls: `10`
- Collision: unchecked

Action:

- Click `Run`.
- Click `Cancel` while the run is active.

Expected:

- Cancel button disables immediately after click.
- Status line first says `Pose reachability cancellation requested.`
- Final status starts with `Pose reachability canceled after ` and ends with ` position(s).`
- Progress completed count is less than planned count.
- Summary line shows `Partial: 1` when cancellation occurs mid-position.

## Scenario 3: Export Metadata

Action:

- Export Pose reachability CSV after Scenario 1 or Scenario 2.
- Export Report JSON from the Report tab.

Expected CSV:

- First line starts with `# pose_reachability_summary`.
- Summary line includes `partial`, `completed_ik_targets`, and `planned_ik_targets`.
- Header line ends with `partial,completed_ik_targets,planned_ik_targets`.

Expected JSON:

- Each `poseReachability` object contains `partial`, `completedIkTargets`, and `plannedIkTargets`.

## Scenario 4: Display Cap

Prepare exactly 501 task points:

```powershell
$csv = "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\pose_reachability_501_taskpoints.csv"
"id,name,type,refFrame,tcpFrame,x,y,z,rollDeg,pitchDeg,yawDeg,positionToleranceMeters,orientationToleranceDeg,allowToolRollFree,weight,enabled,note" | Set-Content -LiteralPath $csv -Encoding ASCII
1..501 | ForEach-Object {
    $x = "{0:N6}" -f ($_ * 0.001)
    "P$_,Smoke $_,Generic,WORLD,TCP,$x,0,0,0,0,0,0.001,1,false,1,true,display cap smoke" | Add-Content -LiteralPath $csv -Encoding ASCII
}
```

Inputs:

- Import `build/pose_reachability_501_taskpoints.csv` in the Task points tab.
- Source: `Task points`
- Directions: `1`
- Rolls: `1`
- Collision: unchecked

Expected:

- Summary line shows `Positions: 501` and `Shown: 500`.
- Result table displays 500 rows.
- CSV export contains 501 pose reachability data rows, not only the 500 displayed rows.
````

- [ ] **Step 2: Run manual smoke**

Run the launch command from the checklist:

```powershell
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\RobWorkStudio.exe" "D:\10_Source_Repos\21_robot\RobWork\RobWork\GenericSixAxis.wc.xml"
```

Execute Scenario 1, Scenario 2, and Scenario 3 exactly as written.

For Scenario 4, generate `build\pose_reachability_501_taskpoints.csv` with the PowerShell command in the checklist, import it in the Task points tab, then run Pose reachability with `Source = Task points`, `Directions = 1`, `Rolls = 1`, and `Collision = unchecked`.

- [ ] **Step 3: Record smoke result**

At the bottom of `PoseReachabilitySmoke.md`, append this section after running the scenarios:

```markdown
## Result

- Scenario 1: PASS
- Scenario 2: PASS
- Scenario 3: PASS
- Scenario 4: PASS
- Notes: Completed with RobWorkStudio Debug build on 2026-07-18.
```

If a scenario fails, write `FAIL` for that scenario and include the exact observed label/status/export content in `Notes`.

- [ ] **Step 4: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilitySmoke.md
git commit -m "test: document pose reachability UI smoke"
```

---

### Task 5: Final Verification

**Files:**
- No source edits unless verification exposes a compile or test failure.

- [ ] **Step 1: Build test target**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Expected: command exits with code `0`.

- [ ] **Step 2: Run focused tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe pose_reachability'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe pose'
```

Expected:

```text
KinematicAnalysis pose_reachability test passed.
KinematicAnalysis pose test passed.
```

- [ ] **Step 3: Run CTest**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected:

```text
100% tests passed, 0 tests failed out of 1
```

- [ ] **Step 4: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: command exits with code `0`.

- [ ] **Step 5: Whitespace check**

Run:

```powershell
git diff --check
```

Expected: no output.

---

## Execution Order for Multiple Agents

Recommended split:

1. Agent A: Task 1.
2. Agent B: Task 2 after Task 1 lands.
3. Agent C: Task 3 after Task 2 lands.
4. Agent D: Task 4 after Task 2 and Task 3 land.
5. Agent E: Task 5 after all previous tasks land.

Do not run Task 1 and Task 2 in parallel because both change the target count contract. Do not run Task 4 before Task 2, because the smoke checklist must validate the corrected progress denominator.

## Acceptance Criteria

- `plannedPoseReachabilityTargetCount()` remains capped at `MaxPoseReachabilityTargets` for diagnostics.
- `poseReachabilityExecutionTargetCount()` returns `1080000` for 3 positions, 1000 directions, and 360 rolls.
- Analyzer progress callbacks use the exact execution target count, not the capped diagnostic count.
- UI progress label cannot show a percentage above `100.0%` when completed work is bounded by planned work.
- `QProgressBar` range remains within a safe `int` range for very large jobs.
- README explains capped diagnostics versus uncapped execution progress.
- `PoseReachabilitySmoke.md` records PASS/FAIL evidence for small run, cancel run, export metadata, and 500-row display cap.
- Focused tests and CTest pass.
- `sdurws_kinematicanalysis` builds.

## Out of Scope

- Intra-analysis parallel IK execution.
- Changing the meaning of `coverage = reachableDirections / sampledDirections`.
- Enforcing `MaxPoseReachabilityTargets` as a hard execution stop.
- Changing existing CSV/JSON field names.
