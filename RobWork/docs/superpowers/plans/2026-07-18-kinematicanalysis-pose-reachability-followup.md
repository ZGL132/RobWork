# KinematicAnalysis Pose Reachability Follow-Up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the next Pose reachability increment by making long runs visibly trackable, making canceled/partial results explicit, keeping large result tables bounded, and aligning tests and README with the current inner-target cancellation behavior.

**Architecture:** Continue from the current helper/analyzer/widget split. Keep algorithm semantics in `KinematicAnalyzer` and pure summary logic in `KinematicAnalysisPoseReachability`, while the Qt widget owns progress display, cancel button state, and table row capping.

**Tech Stack:** C++ in RobWorkStudio, RobWork kinematics and IK APIs, Qt Widgets/QtConcurrent, existing `sdurws_kinematicanalysis_test` CTest executable.

---

## Current Baseline

This follow-up plan assumes these changes already exist in the worktree:

- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp`
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
- `PoseReachabilityDiagnostics` and `PoseReachabilitySummary` in `KinematicAnalysisTypes.hpp`
- `PoseReachabilityRunCallbacks` in `KinematicAnalyzer.hpp`
- Background `QtConcurrent::run` Pose reachability execution in `KinematicAnalysisWidget.cpp`
- Cancel button backed by `std::shared_ptr<std::atomic_bool>`
- `KinematicAnalyzer::analyzePoseReachability` checks cancellation inside the direction/roll IK target loop
- CSV export already writes `# pose_reachability_summary,...`

Do not re-implement the already-completed planning helper, background worker, or shared atomic cancel lifecycle.

## File Structure

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
  - Add per-sample partial-result metadata and summary counters.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
  - Include new metadata in `summarizePoseReachabilitySamples`.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Add progress widgets and a queued progress slot.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Wire `PoseReachabilityRunCallbacks::onProgress` to the UI, cap displayed rows, and show partial/canceled counts.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
  - Populate per-sample planned/completed IK target counts and mark partial samples when cancellation stops mid-position.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add analyzer-level progress and partial cancellation regression tests.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Replace stale cancellation wording and document progress/partial result semantics.

---

### Task 1: Add Explicit Partial Result Metadata

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Extend `PoseReachabilitySample`**

In `KinematicAnalysisTypes.hpp`, add these fields at the end of `struct PoseReachabilitySample`, after `status`:

```cpp
std::size_t plannedIkTargets = 0;
std::size_t completedIkTargets = 0;
bool partial = false;
```

Expected struct tail:

```cpp
double coverage = 0.0;
AnalysisStatus status = AnalysisStatus::Unknown;
std::size_t plannedIkTargets = 0;
std::size_t completedIkTargets = 0;
bool partial = false;
```

- [ ] **Step 2: Extend `PoseReachabilitySummary`**

In `KinematicAnalysisTypes.hpp`, add these fields at the end of `struct PoseReachabilitySummary`, after `maxCoverage`:

```cpp
std::size_t partialCount = 0;
std::size_t plannedIkTargets = 0;
std::size_t completedIkTargets = 0;
```

- [ ] **Step 3: Update summary helper**

In `KinematicAnalysisPoseReachability.cpp`, inside `summarizePoseReachabilitySamples`, add this block inside the loop after reachable/sample direction accumulation:

```cpp
if (sample.partial)
    ++summary.partialCount;
summary.plannedIkTargets += sample.plannedIkTargets;
summary.completedIkTargets += sample.completedIkTargets;
```

The loop should still call `includeCoverage(summary, sample.coverage)` once for every sample.

- [ ] **Step 4: Add helper test coverage**

In `testPoseReachabilityHelpers()` in `KinematicAnalysisTest.cpp`, extend the existing two-sample summary block by setting metadata:

```cpp
pass.plannedIkTargets = 10;
pass.completedIkTargets = 10;
pass.partial = false;
```

and:

```cpp
warning.plannedIkTargets = 10;
warning.completedIkTargets = 4;
warning.partial = true;
```

After the existing average coverage assertion, add:

```cpp
if (const int rc = require (summary.partialCount == 1,
                            "pose summary partial count"))
    return rc;
if (const int rc = require (summary.plannedIkTargets == 20,
                            "pose summary planned IK targets"))
    return rc;
if (const int rc = require (summary.completedIkTargets == 14,
                            "pose summary completed IK targets"))
    return rc;
```

- [ ] **Step 5: Run helper-focused test**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\Debug\sdurws_kinematicanalysis_test.exe pose_reachability'
```

Expected: build succeeds and the `pose_reachability` suite exits with code `0`.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: track partial pose reachability samples"
```

---

### Task 2: Populate Metadata and Test Progress Callbacks

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Fill planned/completed fields in the analyzer**

In `KinematicAnalyzer::analyzePoseReachability`, immediately after:

```cpp
sample.position          = position;
sample.sampledDirections = totalDirections;
```

add:

```cpp
sample.plannedIkTargets = ikPerPosition;
std::size_t completedTargetsForSample = 0;
```

Inside the inner loop, after:

```cpp
++completedTargets;
```

add:

```cpp
++completedTargetsForSample;
sample.completedIkTargets = completedTargetsForSample;
```

Before pushing a normal completed sample, add:

```cpp
sample.completedIkTargets = completedTargetsForSample;
sample.partial = completedTargetsForSample < ikPerPosition;
```

- [ ] **Step 2: Mark canceled samples as partial**

Replace the current `finishCanceledSample` lambda with:

```cpp
const auto finishCanceledSample =
    [&sample, totalDirections, ikPerPosition, &completedTargetsForSample] () {
    sample.completedIkTargets = completedTargetsForSample;
    sample.plannedIkTargets = ikPerPosition;
    sample.partial = completedTargetsForSample < ikPerPosition;
    sample.status = sample.reachableDirections == 0 ?
        AnalysisStatus::Fail : AnalysisStatus::Warning;
    sample.coverage = totalDirections == 0 ? 0.0 :
        static_cast< double > (sample.reachableDirections) /
            static_cast< double > (totalDirections);
};
```

For the fallback branch `device == NULL || resolvedTcpFrame == NULL || totalDirections == 0`, add before `results.push_back(sample)`:

```cpp
sample.completedIkTargets = 0;
sample.partial = false;
```

- [ ] **Step 3: Add analyzer progress regression test**

In `testPoseReachability()` in `KinematicAnalysisTest.cpp`, after the existing inner-loop cancellation block, add:

```cpp
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    rw::kinematics::State deviceState = stateStructure.getDefaultState ();

    rws::PoseReachabilityConfig progressConfig;
    progressConfig.directionSamples = 2;
    progressConfig.rollSamples = 2;
    progressConfig.checkCollision = false;

    struct ProgressState {
        std::size_t calls = 0;
        std::size_t lastCompleted = 0;
        std::size_t lastPlanned = 0;
    } progressState;

    rws::PoseReachabilityRunCallbacks progressCb;
    progressCb.userData = &progressState;
    progressCb.onProgress = [] (std::size_t completed,
                                std::size_t planned,
                                void* userData) {
        ProgressState* state = static_cast< ProgressState* > (userData);
        ++state->calls;
        state->lastCompleted = completed;
        state->lastPlanned = planned;
    };

    const std::vector< rws::PoseReachabilitySample > progressResult =
        analyzer.analyzePoseReachability (
            device, device->getEnd (), deviceState, positions,
            progressConfig, NULL, progressCb);

    if (const int rc = require (progressState.calls == 4,
                                "pose progress callback per IK target"))
        return rc;
    if (const int rc = require (progressState.lastCompleted == 4,
                                "pose progress last completed target"))
        return rc;
    if (const int rc = require (progressState.lastPlanned == 4,
                                "pose progress planned target count"))
        return rc;
    if (const int rc = require (progressResult.front ().plannedIkTargets == 4,
                                "pose sample planned IK targets"))
        return rc;
    if (const int rc = require (progressResult.front ().completedIkTargets == 4,
                                "pose sample completed IK targets"))
        return rc;
    if (const int rc = require (!progressResult.front ().partial,
                                "pose complete sample is not partial"))
        return rc;
}
```

- [ ] **Step 4: Strengthen existing cancellation regression**

In the existing inner-loop cancellation block, after the `sampledDirections == 4` assertion, add:

```cpp
if (const int rc = require (canceled.front ().plannedIkTargets == 4,
                            "inner-loop canceled planned IK targets"))
    return rc;
if (const int rc = require (canceled.front ().completedIkTargets < 4,
                            "inner-loop canceled completed IK targets"))
    return rc;
if (const int rc = require (canceled.front ().partial,
                            "inner-loop canceled sample marked partial"))
    return rc;
```

- [ ] **Step 5: Run analyzer tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\Debug\sdurws_kinematicanalysis_test.exe pose'
```

Expected: build succeeds and the `pose` suite exits with code `0`.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: report pose reachability progress metadata"
```

---

### Task 3: Surface Progress in the Pose Reachability UI

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add Qt includes**

In `KinematicAnalysisWidget.hpp`, add:

```cpp
#include <QProgressBar>
```

In `KinematicAnalysisWidget.cpp`, add:

```cpp
#include <QMetaObject>
#include <QPointer>
```

- [ ] **Step 2: Add progress fields and slot**

In `KinematicAnalysisWidget.hpp`, add this private slot after `handlePoseReachabilityFinished()`:

```cpp
void updatePoseReachabilityProgress (qulonglong completedTargets,
                                     qulonglong plannedTargets);
```

Add these private fields after `_poseDiagnosticsLabel`:

```cpp
QProgressBar* _poseProgressBar;
QLabel* _poseProgressLabel;
```

In the constructor initializer list in `KinematicAnalysisWidget.cpp`, add:

```cpp
_poseProgressBar(NULL),
_poseProgressLabel(NULL),
```

after `_poseDiagnosticsLabel(NULL),`.

- [ ] **Step 3: Create progress widgets**

In `buildPoseReachabilityTab()`, after adding `_poseDiagnosticsLabel`, add:

```cpp
_poseProgressBar = new QProgressBar (_poseReachTab);
_poseProgressBar->setRange (0, 1);
_poseProgressBar->setValue (0);
_poseProgressBar->setTextVisible (false);
_poseProgressLabel = new QLabel (tr("Progress: 0 / 0 IK target(s)"), _poseReachTab);
layout->addWidget (_poseProgressBar);
layout->addWidget (_poseProgressLabel);
```

- [ ] **Step 4: Reset progress when a run starts**

In `analyzePoseReachability()`, before `setStatus(tr("Pose reachability running..."))`, compute planned count and reset the widgets:

```cpp
PoseReachabilityDiagnostics runDiagnostics;
const std::size_t plannedTargets =
    plannedPoseReachabilityTargetCount (config, positions.size (), &runDiagnostics);
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

- [ ] **Step 5: Add queued progress callback**

In `analyzePoseReachability()`, replace the current direct `cancelFlag` callback setup with this run context:

```cpp
struct PoseRunContext {
    std::shared_ptr< std::atomic_bool > cancelFlag;
    QPointer< KinematicAnalysisWidget > widget;
};

const std::shared_ptr< PoseRunContext > runContext =
    std::make_shared< PoseRunContext > ();
runContext->cancelFlag = _poseReachabilityCancelRequested;
runContext->widget = this;

PoseReachabilityRunCallbacks callbacks;
callbacks.isCancellationRequested = [] (void* userData) -> bool {
    const PoseRunContext* context =
        static_cast< const PoseRunContext* > (userData);
    return context != NULL && context->cancelFlag &&
           context->cancelFlag->load ();
};
callbacks.onProgress = [] (std::size_t completedTargets,
                           std::size_t plannedTargets,
                           void* userData) {
    PoseRunContext* context = static_cast< PoseRunContext* > (userData);
    if (context == NULL || context->widget.isNull ())
        return;
    QMetaObject::invokeMethod (
        context->widget.data (),
        "updatePoseReachabilityProgress",
        Qt::QueuedConnection,
        Q_ARG (qulonglong, static_cast< qulonglong > (completedTargets)),
        Q_ARG (qulonglong, static_cast< qulonglong > (plannedTargets)));
};
callbacks.userData = runContext.get ();
```

Update the `QtConcurrent::run` capture list so it captures `callbacks` and `runContext`, not the old `cancelFlag`:

```cpp
[runDevice, runTcpFrame, runState, positions, config,
 collisionDetector, runThresholds, callbacks, runContext] () {
```

- [ ] **Step 6: Implement progress slot**

Add this method before `handlePoseReachabilityFinished()`:

```cpp
void KinematicAnalysisWidget::updatePoseReachabilityProgress (
    qulonglong completedTargets, qulonglong plannedTargets)
{
    const int planned = static_cast< int > (plannedTargets);
    const int completed = static_cast< int > (
        std::min< qulonglong > (completedTargets, plannedTargets));

    if (_poseProgressBar != NULL) {
        _poseProgressBar->setRange (0, planned);
        _poseProgressBar->setValue (completed);
    }
    if (_poseProgressLabel != NULL) {
        const double pct = plannedTargets == 0 ? 0.0 :
            100.0 * static_cast< double > (completedTargets) /
                static_cast< double > (plannedTargets);
        _poseProgressLabel->setText (
            tr("Progress: %1 / %2 IK target(s) (%3%)")
                .arg (static_cast< int > (completedTargets))
                .arg (static_cast< int > (plannedTargets))
                .arg (QString::number (pct, 'f', 1)));
    }
}
```

- [ ] **Step 7: Finalize progress on completion**

In `handlePoseReachabilityFinished()`, after `_poseReachabilitySamples = samples;`, add:

```cpp
const PoseReachabilitySummary summary =
    summarizePoseReachabilitySamples (_poseReachabilitySamples);
updatePoseReachabilityProgress (
    static_cast< qulonglong > (summary.completedIkTargets),
    static_cast< qulonglong > (summary.plannedIkTargets));
```

- [ ] **Step 8: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: plugin target builds successfully.

- [ ] **Step 9: Manual smoke**

1. Open RobWorkStudio with a WorkCell containing a valid device.
2. Open KinematicAnalysis > Pose reachability.
3. Use Manual rows with one position, Directions `24`, Rolls `2`.
4. Press Run.
5. Confirm the progress label advances from `0 / 48` toward `48 / 48`.
6. Press Cancel on a larger run and confirm the label stops below the planned target count.

- [ ] **Step 10: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: show pose reachability progress"
```

---

### Task 4: Cap Displayed Pose Result Rows

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`

- [ ] **Step 1: Add a local display cap**

In the anonymous namespace near existing widget helper declarations in `KinematicAnalysisWidget.cpp`, add:

```cpp
static const std::size_t MaxPoseReachabilityTableRows = 500;
```

- [ ] **Step 2: Render only the capped row count**

In `applyPoseReachabilityResults`, replace:

```cpp
_poseResultTable->setRowCount (static_cast< int > (samples.size ()));
```

with:

```cpp
const int rows = static_cast< int > (
    std::min< std::size_t > (samples.size (), MaxPoseReachabilityTableRows));
_poseResultTable->setRowCount (rows);
```

Replace the loop header:

```cpp
for (std::size_t i = 0; i < samples.size (); ++i) {
```

with:

```cpp
for (std::size_t i = 0; i < static_cast< std::size_t > (rows); ++i) {
```

- [ ] **Step 3: Show total and displayed row counts**

In the `_poseSummaryLabel->setText` call, replace the string with:

```cpp
tr("Positions: %1    Shown: %2    Pass: %3    Warning: %4    Fail: %5    "
   "Partial: %6    Average coverage: %7    Min/Max: %8 / %9")
```

and use this argument sequence:

```cpp
.arg (static_cast< int > (summary.totalPositions))
.arg (rows)
.arg (static_cast< int > (summary.passCount))
.arg (static_cast< int > (summary.warningCount))
.arg (static_cast< int > (summary.failCount))
.arg (static_cast< int > (summary.partialCount))
.arg (QString::number (summary.averageCoverage, 'f', 3))
.arg (QString::number (summary.minCoverage, 'f', 3))
.arg (QString::number (summary.maxCoverage, 'f', 3))
```

- [ ] **Step 4: Keep CSV/report/visualization complete**

Do not change `_poseReachabilitySamples`, `exportPoseReachabilityCsv`, `updateReportSummary`, or `refreshVisualization` to use the capped row count. Those paths must keep the full sample vector.

- [ ] **Step 5: Build plugin**

Run the plugin build command from Task 3.

Expected: plugin target builds successfully.

- [ ] **Step 6: Manual smoke**

1. Run Pose reachability with more than 500 positions from Task points or Manual rows.
2. Confirm the result table shows 500 rows.
3. Confirm the summary shows `Positions: <full count>` and `Shown: 500`.
4. Export CSV and confirm the CSV contains all result rows, not only 500.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp
git commit -m "perf: cap pose reachability result table rows"
```

---

### Task 5: Include Partial Metadata in CSV and Report JSON

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Extend CSV summary line**

In `exportPoseReachabilityCsv`, extend the `# pose_reachability_summary` line after `max_coverage`:

```cpp
<< ",max_coverage," << summary.maxCoverage
<< ",partial," << summary.partialCount
<< ",completed_ik_targets," << summary.completedIkTargets
<< ",planned_ik_targets," << summary.plannedIkTargets
<< "\n";
```

- [ ] **Step 2: Extend CSV row header**

Replace the current header:

```cpp
out << "position_x,position_y,position_z,sampled_directions,reachable_directions,coverage,status\n";
```

with:

```cpp
out << "position_x,position_y,position_z,sampled_directions,reachable_directions,coverage,status,partial,completed_ik_targets,planned_ik_targets\n";
```

- [ ] **Step 3: Extend CSV rows**

Replace the row write tail:

```cpp
<< statusText (sample.status) << "\n";
```

with:

```cpp
<< statusText (sample.status) << ","
<< (sample.partial ? "true" : "false") << ","
<< sample.completedIkTargets << ","
<< sample.plannedIkTargets << "\n";
```

- [ ] **Step 4: Extend report JSON pose objects**

In `exportReportJson`, in the loop that writes `result.poseReachability`, add:

```cpp
item["partial"] = sample.partial;
item["completedIkTargets"] = static_cast< double > (sample.completedIkTargets);
item["plannedIkTargets"] = static_cast< double > (sample.plannedIkTargets);
```

Keep the existing JSON fields `position`, `sampledDirections`, `reachableDirections`, `coverage`, and `status` unchanged.

- [ ] **Step 5: Build plugin**

Run the plugin build command from Task 3.

Expected: plugin target builds successfully.

- [ ] **Step 6: Manual export smoke**

1. Run Pose reachability.
2. Export Pose reachability CSV.
3. Confirm the summary line includes `partial`, `completed_ik_targets`, and `planned_ik_targets`.
4. Confirm each data row includes the new three trailing columns.
5. Export report JSON and confirm each `poseReachability` item contains `partial`, `completedIkTargets`, and `plannedIkTargets`.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: export pose reachability partial metadata"
```

---

### Task 6: Align README With Inner-Target Cancellation

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Replace stale Cancel wording**

Replace:

```markdown
- `Run`: starts the analysis. The Run button is disabled while running; a Cancel button appears to request early stop (cooperative cancellation, current position completes before stopping).
```

with:

```markdown
- `Run`: starts the analysis. The Run button is disabled while running; the Cancel button requests cooperative early stop. Cancellation is checked before each position, before each IK target, and after each IK target returns.
```

- [ ] **Step 2: Document progress and partial results**

After the paragraph beginning `The analysis runs on a background thread`, add:

```markdown
Progress is reported as completed IK targets over planned IK targets. If cancellation stops a run mid-position, the current sample is marked partial, its completed/planned IK target counts are exported, and coverage remains `reachableDirections / sampledDirections` so older consumers keep the same coverage definition.
```

- [ ] **Step 3: Verify no stale phrase remains**

Run:

```powershell
rg -n "current position completes|partial|completed IK targets|planned IK targets" RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
```

Expected:

```text
No line contains "current position completes".
At least one line documents partial results and completed/planned IK targets.
```

- [ ] **Step 4: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "docs: clarify pose reachability cancellation"
```

---

### Task 7: Final Verification

**Files:**
- No source edits unless verification exposes a compile or test failure.

- [ ] **Step 1: Build tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Expected: target `sdurws_kinematicanalysis_test` builds successfully.

- [ ] **Step 2: Run focused tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\Debug\sdurws_kinematicanalysis_test.exe pose_reachability'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\Debug\sdurws_kinematicanalysis_test.exe pose'
```

Expected: both commands exit with code `0`.

- [ ] **Step 3: Run CTest**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected: CTest reports `100% tests passed`.

- [ ] **Step 4: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: target `sdurws_kinematicanalysis` builds successfully.

- [ ] **Step 5: Whitespace check**

Run:

```powershell
git diff --check
```

Expected: no new whitespace errors. Existing LF/CRLF warnings may remain if they were present before this follow-up.

---

## Execution Order for Multiple Agents

Recommended split:

1. Agent A: Task 1 and Task 2.
2. Agent B: Task 3 after Task 1 lands.
3. Agent C: Task 4 after Task 3 lands.
4. Agent D: Task 5 after Task 1 and Task 2 land.
5. Agent E: Task 6 after Task 5 lands.
6. Agent F: Task 7 after all previous tasks land.

Do not run Agent B and Agent C in parallel against `KinematicAnalysisWidget.cpp`; both touch Pose reachability rendering and run lifecycle.

## Acceptance Criteria

- Pose reachability progress is visible while a background run is active.
- Progress advances once per completed IK target.
- Canceling a run can stop inside one position after the current IK target returns.
- Canceled mid-position samples are marked `partial == true`.
- Partial samples expose `completedIkTargets` and `plannedIkTargets` in memory, CSV, and report JSON.
- The result table displays at most 500 Pose reachability rows while export/report/visualization keep the full result vector.
- README no longer says cancellation waits for the current position to complete.
- `sdurws_kinematicanalysis_test.exe pose_reachability` passes.
- `sdurws_kinematicanalysis_test.exe pose` passes.
- `ctest -R sdurws_kinematicanalysis_test` passes.
- `sdurws_kinematicanalysis` builds in Debug.

## Parallelism Note

Do not parallelize Pose reachability IK targets in this follow-up. The current safe optimization is one background UI worker with cooperative cancellation and progress. Intra-analysis parallelism should wait for a separate thread-safety audit of `State`, `Device::setQ`, IK solver creation, and `CollisionDetector` ownership.
