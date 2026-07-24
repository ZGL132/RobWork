# KinematicAnalysis Pose Reachability Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Optimize the Pose reachability feature so it is bounded, observable, reusable, and responsive for practical multi-position / multi-orientation analysis.

**Architecture:** Keep `KinematicAnalyzer` as the non-Qt algorithm owner, but move Pose reachability planning/sanitizing/summary helpers into a focused helper module mirroring the existing Workspace helper split. The first implementation should improve correctness, diagnostics, and UI responsiveness without changing persisted report schemas; optional parallel execution is isolated behind a worker-oriented API after the deterministic baseline is tested.

**Tech Stack:** C++ in RobWorkStudio, RobWork kinematics / inverse kinematics / proximity APIs, Qt Widgets, optional QtConcurrent/QFuture for UI-side background execution, existing `sdurws_kinematicanalysis_test` CTest executable.

---

## Current State

- Core data lives in `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`.
- Core algorithm is `KinematicAnalyzer::analyzePoseReachability` in `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`.
- UI entrypoint is `KinematicAnalysisWidget::analyzePoseReachability` in `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`.
- Result rendering is `KinematicAnalysisWidget::applyPoseReachabilityResults`.
- CSV export is `KinematicAnalysisWidget::exportPoseReachabilityCsv`.
- Visualization already accepts `PoseReachabilitySample` through `KinematicAnalysisVisualizationTypes.cpp`.
- Existing tests only cover default config, no-device fallback, zero-direction fallback, aggregate/report inclusion, and visualization scalar mapping.

Current performance shape is:

```text
positions.size() * max(0, directionSamples) * max(1, rollSamples) calls to analyzeIk()
```

This means a UI setting of 100 positions, 120 directions, 6 rolls can run 72,000 full IK analyses synchronously on the UI thread.

## Scope

In scope:

- Add Pose reachability config sanitizing, diagnostics, planned target count, and summary helpers.
- Add diagnostics and summaries while preserving existing CSV/JSON fields.
- Make the UI show planned work before run, warn on capped/large jobs, and prevent empty export.
- Move long-running Pose reachability execution off the UI thread.
- Add deterministic tests for planning, edge cases, summary, export-neutral result values, and cancellation-safe UI state.
- Update README with the new behavior and limits.

Out of scope for the first implementation:

- Replacing the IK solver.
- Analytical branch-complete IK enumeration.
- GPU acceleration.
- Changing the meaning of `coverage = reachableDirections / sampledDirections`.
- Changing report JSON field names already used downstream.

## File Structure

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
  - Extend Pose reachability config/diagnostic/summary/result structs.
- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp`
  - Pose-specific constants, sanitizing, planning, summary, and small pure helpers.
- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
  - Implementation of helper functions; no Qt, no RobWorkStudio UI.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
  - Add a progress/cancellation-aware overload or options type for Pose reachability.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
  - Reuse sanitized config and helper functions; preserve existing public overload behavior.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Add UI controls/labels for diagnostics and background run lifecycle.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Add planned-count label, guarded run button, optional cancel button, background completion handling, and improved summary/export behavior.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
  - Add the new helper source/header to plugin and test targets; add Qt Concurrent dependency only if UI background execution uses QtConcurrent.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add targeted subtests for Pose reachability helpers and algorithm edge cases.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Document planned target count, caps, diagnostics, and background behavior.

## Design Decisions

1. Use a helper module first, then UI backgrounding.
   - Reason: planning/sanitizing/summary can be tested without a WorkCell and gives other agents a stable foundation.

2. Keep current result compatibility.
   - Existing fields `position`, `sampledDirections`, `reachableDirections`, `coverage`, and `status` must keep the same meaning.

3. Cap default UI work, not engine capability.
   - The engine should sanitize to hard constants to prevent overflow. The UI should warn/disable huge jobs unless the user explicitly configures high values later.

4. Do not parallelize collision-enabled IK blindly in the first pass.
   - RobWork state mutation and collision detector thread-safety must be verified. The safe optimization is UI background execution plus deterministic planning. Parallelism can follow behind a separate task after tests prove per-target state isolation.

---

### Task 1: Add Pose Reachability Planning Helpers

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Extend types for diagnostics and summary**

Add these structs immediately after `PoseReachabilityConfig` in `KinematicAnalysisTypes.hpp`:

```cpp
struct PoseReachabilityDiagnostics
{
    std::size_t positionCount = 0;
    std::size_t requestedDirectionSamples = 0;
    std::size_t requestedRollSamples = 0;
    std::size_t plannedDirectionsPerPosition = 0;
    std::size_t plannedIkTargets = 0;
    bool directionSamplesClamped = false;
    bool rollSamplesClamped = false;
    bool targetCountCapped = false;
};

struct PoseReachabilitySummary
{
    std::size_t totalPositions = 0;
    std::size_t passCount = 0;
    std::size_t warningCount = 0;
    std::size_t failCount = 0;
    std::size_t unknownCount = 0;
    std::size_t sampledDirections = 0;
    std::size_t reachableDirections = 0;
    double averageCoverage = 0.0;
    double minCoverage = 0.0;
    double maxCoverage = 0.0;
};
```

- [ ] **Step 2: Create helper header**

Create `KinematicAnalysisPoseReachability.hpp`:

```cpp
#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP

#include "KinematicAnalysisTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

static const int MaxPoseDirectionSamples = 1000;
static const int MaxPoseRollSamples = 360;
static const std::size_t MaxPoseReachabilityTargets = 1000000;

PoseReachabilityConfig sanitizePoseReachabilityConfig (
    const PoseReachabilityConfig& config,
    PoseReachabilityDiagnostics* diagnostics = nullptr);

std::size_t plannedPoseReachabilityTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    PoseReachabilityDiagnostics* diagnostics = nullptr);

PoseReachabilitySummary summarizePoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples);

bool isPoseDirectionReachable (
    const std::vector< KinematicIkSolution >& solutions);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP
```

- [ ] **Step 3: Implement helper source**

Create `KinematicAnalysisPoseReachability.cpp`:

```cpp
#include "KinematicAnalysisPoseReachability.hpp"

#include <algorithm>
#include <cmath>

using namespace rws;

namespace {

std::size_t multiplyCapped (std::size_t lhs, std::size_t rhs, std::size_t cap,
                            bool* capped = nullptr)
{
    if (lhs == 0 || rhs == 0) {
        if (capped != nullptr) *capped = false;
        return 0;
    }
    if (lhs > cap / rhs) {
        if (capped != nullptr) *capped = true;
        return cap;
    }
    if (capped != nullptr) *capped = false;
    return lhs * rhs;
}

void includeCoverage (PoseReachabilitySummary& summary, double coverage)
{
    if (summary.totalPositions == 1) {
        summary.minCoverage = coverage;
        summary.maxCoverage = coverage;
    }
    else {
        summary.minCoverage = std::min (summary.minCoverage, coverage);
        summary.maxCoverage = std::max (summary.maxCoverage, coverage);
    }
}

}    // namespace

PoseReachabilityConfig rws::sanitizePoseReachabilityConfig (
    const PoseReachabilityConfig& config,
    PoseReachabilityDiagnostics* diagnostics)
{
    if (diagnostics != nullptr)
        *diagnostics = PoseReachabilityDiagnostics ();

    PoseReachabilityConfig sanitized = config;
    if (diagnostics != nullptr) {
        diagnostics->requestedDirectionSamples =
            static_cast< std::size_t > (std::max (0, config.directionSamples));
        diagnostics->requestedRollSamples =
            static_cast< std::size_t > (std::max (0, config.rollSamples));
    }

    if (sanitized.directionSamples < 0) {
        sanitized.directionSamples = 0;
        if (diagnostics != nullptr) diagnostics->directionSamplesClamped = true;
    }
    if (sanitized.directionSamples > MaxPoseDirectionSamples) {
        sanitized.directionSamples = MaxPoseDirectionSamples;
        if (diagnostics != nullptr) diagnostics->directionSamplesClamped = true;
    }
    if (sanitized.rollSamples < 1) {
        sanitized.rollSamples = 1;
        if (diagnostics != nullptr) diagnostics->rollSamplesClamped = true;
    }
    if (sanitized.rollSamples > MaxPoseRollSamples) {
        sanitized.rollSamples = MaxPoseRollSamples;
        if (diagnostics != nullptr) diagnostics->rollSamplesClamped = true;
    }
    return sanitized;
}

std::size_t rws::plannedPoseReachabilityTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    PoseReachabilityDiagnostics* diagnostics)
{
    PoseReachabilityDiagnostics local;
    const PoseReachabilityConfig sanitized =
        sanitizePoseReachabilityConfig (config, &local);
    local.positionCount = positionCount;

    const std::size_t directions =
        static_cast< std::size_t > (std::max (0, sanitized.directionSamples));
    const std::size_t rolls = directions == 0 ? 0 :
        static_cast< std::size_t > (std::max (1, sanitized.rollSamples));
    bool perPositionCapped = false;
    const std::size_t perPosition = multiplyCapped (
        directions, rolls, MaxPoseReachabilityTargets, &perPositionCapped);
    bool totalCapped = false;
    const std::size_t total = multiplyCapped (
        positionCount, perPosition, MaxPoseReachabilityTargets, &totalCapped);

    local.plannedDirectionsPerPosition = perPosition;
    local.plannedIkTargets = total;
    local.targetCountCapped = perPositionCapped || totalCapped;
    if (diagnostics != nullptr)
        *diagnostics = local;
    return total;
}

PoseReachabilitySummary rws::summarizePoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples)
{
    PoseReachabilitySummary summary;
    double coverageSum = 0.0;
    for (const PoseReachabilitySample& sample : samples) {
        ++summary.totalPositions;
        switch (sample.status) {
            case AnalysisStatus::Pass: ++summary.passCount; break;
            case AnalysisStatus::Warning: ++summary.warningCount; break;
            case AnalysisStatus::Fail: ++summary.failCount; break;
            case AnalysisStatus::Unknown:
            default: ++summary.unknownCount; break;
        }
        summary.sampledDirections +=
            static_cast< std::size_t > (std::max (0, sample.sampledDirections));
        summary.reachableDirections +=
            static_cast< std::size_t > (std::max (0, sample.reachableDirections));
        coverageSum += sample.coverage;
        includeCoverage (summary, sample.coverage);
    }
    if (summary.totalPositions != 0)
        summary.averageCoverage = coverageSum /
            static_cast< double > (summary.totalPositions);
    return summary;
}

bool rws::isPoseDirectionReachable (
    const std::vector< KinematicIkSolution >& solutions)
{
    for (const KinematicIkSolution& solution : solutions) {
        if (solution.inCollision)
            continue;
        if (solution.status == AnalysisStatus::Pass ||
            solution.status == AnalysisStatus::Warning)
            return true;
    }
    return false;
}
```

- [ ] **Step 4: Register helper in CMake**

Add `KinematicAnalysisPoseReachability.cpp` to both `SrcFiles` and `sdurws_kinematicanalysis_test`, and add `KinematicAnalysisPoseReachability.hpp` to `SRC_FILES_HPP`.

- [ ] **Step 5: Add helper tests**

In `KinematicAnalysisTest.cpp`, include the helper header:

```cpp
#include "KinematicAnalysisPoseReachability.hpp"
```

Add a new test function:

```cpp
static int testPoseReachabilityHelpers ()
{
    rws::PoseReachabilityConfig config;
    config.directionSamples = -5;
    config.rollSamples = 0;
    rws::PoseReachabilityDiagnostics diagnostics;
    const rws::PoseReachabilityConfig sanitized =
        rws::sanitizePoseReachabilityConfig (config, &diagnostics);
    if (const int rc = require (sanitized.directionSamples == 0,
                                "pose direction samples clamped low"))
        return rc;
    if (const int rc = require (sanitized.rollSamples == 1,
                                "pose roll samples clamped low"))
        return rc;
    if (const int rc = require (diagnostics.directionSamplesClamped,
                                "pose direction clamp diagnostic"))
        return rc;
    if (const int rc = require (diagnostics.rollSamplesClamped,
                                "pose roll clamp diagnostic"))
        return rc;

    config.directionSamples = 24;
    config.rollSamples = 3;
    const std::size_t planned =
        rws::plannedPoseReachabilityTargetCount (config, 10, &diagnostics);
    if (const int rc = require (planned == 720,
                                "pose planned target count"))
        return rc;
    if (const int rc = require (diagnostics.plannedDirectionsPerPosition == 72,
                                "pose planned directions per position"))
        return rc;

    rws::PoseReachabilitySample pass;
    pass.status = rws::AnalysisStatus::Pass;
    pass.sampledDirections = 10;
    pass.reachableDirections = 10;
    pass.coverage = 1.0;
    rws::PoseReachabilitySample warning;
    warning.status = rws::AnalysisStatus::Warning;
    warning.sampledDirections = 10;
    warning.reachableDirections = 4;
    warning.coverage = 0.4;
    const rws::PoseReachabilitySummary summary =
        rws::summarizePoseReachabilitySamples (
            std::vector< rws::PoseReachabilitySample > {pass, warning});
    if (const int rc = require (summary.totalPositions == 2,
                                "pose summary total positions"))
        return rc;
    if (const int rc = assertNear (summary.averageCoverage, 0.7, 1e-12,
                                   "pose average coverage"))
        return rc;
    return 0;
}
```

Call it from `runAll()` before `testPoseReachability()`, and add a suite branch:

```cpp
else if (suite == "pose_reachability_helpers")
    rc = testPoseReachabilityHelpers ();
```

- [ ] **Step 6: Run helper tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected: build succeeds and CTest reports `100% tests passed`.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: add pose reachability planning helpers"
```

---

### Task 2: Refactor Analyzer to Use Sanitized Planning

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Include helper header**

Add to `KinematicAnalyzer.cpp`:

```cpp
#include "KinematicAnalysisPoseReachability.hpp"
```

- [ ] **Step 2: Replace local direction/roll sanitizing**

In `KinematicAnalyzer::analyzePoseReachability`, replace:

```cpp
const int directionCount = std::max (0, config.directionSamples);
const int rollCount      = directionCount == 0 ? 0 : std::max (1, config.rollSamples);
const int totalDirections = directionCount * rollCount;
```

with:

```cpp
PoseReachabilityDiagnostics diagnostics;
const PoseReachabilityConfig sanitized =
    sanitizePoseReachabilityConfig (config, &diagnostics);
plannedPoseReachabilityTargetCount (sanitized, positions.size (), &diagnostics);

const int directionCount = sanitized.directionSamples;
const int rollCount = directionCount == 0 ? 0 : sanitized.rollSamples;
const int totalDirections =
    static_cast< int > (diagnostics.plannedDirectionsPerPosition);
```

- [ ] **Step 3: Use shared reachable predicate**

Replace the inline loop that sets `reachable` with:

```cpp
const bool reachable = isPoseDirectionReachable (ik.solutions);
```

- [ ] **Step 4: Preserve old edge behavior**

Keep these invariants:

- `device == NULL` returns one failed sample per input position.
- `resolvedTcpFrame == NULL` returns one failed sample per input position.
- `directionSamples <= 0` returns one failed sample per input position with `sampledDirections == 0`.
- `rollSamples <= 0` is sanitized to `1` when `directionSamples > 0`.

- [ ] **Step 5: Add regression tests**

Extend `testPoseReachability()`:

```cpp
rws::PoseReachabilityConfig negativeRoll;
negativeRoll.directionSamples = 4;
negativeRoll.rollSamples = -9;
const std::vector< rws::PoseReachabilitySample > negativeRollResult =
    analyzer.analyzePoseReachability (NULL, NULL, state, positions, negativeRoll, NULL);
if (const int rc = require (negativeRollResult.front ().sampledDirections == 4,
                            "negative roll is sanitized to one roll"))
    return rc;
```

- [ ] **Step 6: Run pose tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected: `KinematicAnalysis all test passed.` appears in output.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "refactor: sanitize pose reachability analysis"
```

---

### Task 3: Improve Pose Reachability UI Planning and Summary

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: manual UI smoke plus existing CTest

- [ ] **Step 1: Add UI state fields**

In `KinematicAnalysisWidget.hpp`, add private slot:

```cpp
void updatePoseReachabilityControls ();
```

Add private widgets:

```cpp
QLabel* _poseDiagnosticsLabel;
```

- [ ] **Step 2: Initialize field**

In the constructor initializer list in `KinematicAnalysisWidget.cpp`, add:

```cpp
_poseDiagnosticsLabel(NULL),
```

near `_poseSummaryLabel(NULL)`.

- [ ] **Step 3: Add diagnostics label to the tab**

In `buildPoseReachabilityTab()`, after `_poseSummaryLabel` creation:

```cpp
_poseDiagnosticsLabel = new QLabel (
    tr("Plan: 0 IK target(s), 0 orientation(s) per position"),
    _poseReachTab);
layout->addWidget (_poseDiagnosticsLabel);
```

- [ ] **Step 4: Connect controls**

In the constructor, add:

```cpp
connect (_poseSourceCombo, SIGNAL (currentIndexChanged (int)),
         this, SLOT (updatePoseReachabilityControls ()));
connect (_poseDirectionSamplesSpin, SIGNAL (valueChanged (int)),
         this, SLOT (updatePoseReachabilityControls ()));
connect (_poseRollSamplesSpin, SIGNAL (valueChanged (int)),
         this, SLOT (updatePoseReachabilityControls ()));
connect (_posePositionTable, SIGNAL (itemChanged (QTableWidgetItem*)),
         this, SLOT (updatePoseReachabilityControls ()));
```

Call `updatePoseReachabilityControls();` after all Pose tab connections are installed.

- [ ] **Step 5: Implement planning label**

Add to `KinematicAnalysisWidget.cpp`:

```cpp
void KinematicAnalysisWidget::updatePoseReachabilityControls ()
{
    if (_poseDiagnosticsLabel == NULL || _poseDirectionSamplesSpin == NULL ||
        _poseRollSamplesSpin == NULL)
        return;

    QString validationError;
    const std::vector< std::array< double, 3 > > positions =
        collectPoseReachabilityPositions (&validationError);

    PoseReachabilityConfig config;
    config.directionSamples = _poseDirectionSamplesSpin->value ();
    config.rollSamples = _poseRollSamplesSpin->value ();
    config.checkCollision =
        _poseCollisionCheck == NULL || _poseCollisionCheck->isChecked ();

    PoseReachabilityDiagnostics diagnostics;
    const std::size_t planned =
        plannedPoseReachabilityTargetCount (
            config, positions.size (), &diagnostics);

    const QString cappedText = diagnostics.targetCountCapped ?
        tr(" (capped)") : QString ();
    const QString validationText = validationError.isEmpty () ?
        QString () : tr(" Input warning: %1").arg (validationError);
    _poseDiagnosticsLabel->setText (
        tr("Plan: %1 IK target(s), %2 orientation(s) per position%3.%4")
            .arg (static_cast< int > (planned))
            .arg (static_cast< int > (
                diagnostics.plannedDirectionsPerPosition))
            .arg (cappedText)
            .arg (validationText));
}
```

- [ ] **Step 6: Replace summary calculation**

In `applyPoseReachabilityResults`, use:

```cpp
const PoseReachabilitySummary summary =
    summarizePoseReachabilitySamples (samples);
```

Set label text:

```cpp
_poseSummaryLabel->setText (
    tr("Positions: %1    Pass: %2    Warning: %3    Fail: %4    "
       "Average coverage: %5    Min/Max: %6 / %7")
        .arg (static_cast< int > (summary.totalPositions))
        .arg (static_cast< int > (summary.passCount))
        .arg (static_cast< int > (summary.warningCount))
        .arg (static_cast< int > (summary.failCount))
        .arg (QString::number (summary.averageCoverage, 'f', 3))
        .arg (QString::number (summary.minCoverage, 'f', 3))
        .arg (QString::number (summary.maxCoverage, 'f', 3)));
```

- [ ] **Step 7: Disable export when no samples exist**

At the end of `buildPoseReachabilityTab()`:

```cpp
_poseExportButton->setEnabled (false);
```

At the end of `applyPoseReachabilityResults()`:

```cpp
if (_poseExportButton != NULL)
    _poseExportButton->setEnabled (!samples.empty ());
```

At the beginning of `exportPoseReachabilityCsv()`:

```cpp
if (_poseReachabilitySamples.empty ()) {
    setStatus (tr("No pose reachability samples to export."));
    return;
}
```

- [ ] **Step 8: Run tests and smoke UI**

Run CTest command from Task 2.

Manual smoke:

1. Open RobWorkStudio with a WorkCell.
2. Open KinematicAnalysis.
3. Go to Pose reachability.
4. Add a manual row.
5. Verify diagnostics shows `Directions * Rolls * Positions`.
6. Run with small settings, for example directions `4`, rolls `1`.
7. Verify summary has pass/warning/fail counts and export is enabled only after results.

- [ ] **Step 9: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: show pose reachability planning diagnostics"
```

---

### Task 4: Add Background Execution and Cancel-Safe UI Lifecycle

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`

- [ ] **Step 1: Decide execution primitive**

Recommended first pass: `QtConcurrent::run` plus `QFutureWatcher<std::vector<PoseReachabilitySample>>`.

Reason: it keeps the existing algorithm synchronous and isolates threading to the Widget. Do not parallelize inside `KinematicAnalyzer` yet.

- [ ] **Step 2: Add Qt includes and fields**

In `KinematicAnalysisWidget.hpp`, include:

```cpp
#include <QFutureWatcher>
```

Add private slots:

```cpp
void handlePoseReachabilityFinished ();
```

Add fields:

```cpp
QPushButton* _poseCancelButton;
QFutureWatcher< std::vector< PoseReachabilitySample > >* _poseReachabilityWatcher;
bool _poseReachabilityRunActive;
bool _poseReachabilityCancelRequested;
```

- [ ] **Step 3: Add Cancel button**

In `buildPoseReachabilityTab()`:

```cpp
_poseCancelButton = new QPushButton (tr("Cancel"), _poseReachTab);
_poseCancelButton->setEnabled (false);
controls->addWidget (_poseCancelButton, 2, 3);
```

Connect:

```cpp
connect (_poseCancelButton, &QPushButton::clicked, this, [this] () {
    _poseReachabilityCancelRequested = true;
    if (_poseCancelButton != NULL)
        _poseCancelButton->setEnabled (false);
    setStatus (tr("Pose reachability cancellation requested."));
});
```

- [ ] **Step 4: Add watcher lifecycle**

In constructor body:

```cpp
_poseReachabilityWatcher =
    new QFutureWatcher< std::vector< PoseReachabilitySample > > (this);
connect (_poseReachabilityWatcher,
         SIGNAL (finished ()),
         this,
         SLOT (handlePoseReachabilityFinished ()));
```

In constructor initializer list:

```cpp
_poseCancelButton(NULL),
_poseReachabilityWatcher(NULL),
_poseReachabilityRunActive(false),
_poseReachabilityCancelRequested(false),
```

- [ ] **Step 5: Run analysis in the background**

Change `analyzePoseReachability()` to validate inputs on the UI thread, then start a future. Capture only values, not mutable widget pointers:

```cpp
_poseReachabilityRunActive = true;
_poseReachabilityCancelRequested = false;
_poseAnalyzeButton->setEnabled (false);
_poseCancelButton->setEnabled (true);
QApplication::setOverrideCursor (Qt::WaitCursor);
setStatus (tr("Pose reachability running..."));

const rw::kinematics::State runState = currentState ();
const rw::core::Ptr< rw::models::Device > runDevice = device;
const rw::core::Ptr< const rw::kinematics::Frame > runTcpFrame = selectedTcpFrame ();
const KinematicThresholds runThresholds = _thresholds;

QFuture< std::vector< PoseReachabilitySample > > future = QtConcurrent::run (
    [runDevice, runTcpFrame, runState, positions, config,
     collisionDetector, runThresholds] () {
        KinematicAnalyzer analyzer;
        analyzer.setThresholds (runThresholds);
        return analyzer.analyzePoseReachability (
            runDevice, runTcpFrame, runState, positions, config,
            collisionDetector);
    });
_poseReachabilityWatcher->setFuture (future);
```

- [ ] **Step 6: Complete safely**

Implement:

```cpp
void KinematicAnalysisWidget::handlePoseReachabilityFinished ()
{
    QApplication::restoreOverrideCursor ();
    _poseReachabilityRunActive = false;
    if (_poseAnalyzeButton != NULL)
        _poseAnalyzeButton->setEnabled (true);
    if (_poseCancelButton != NULL)
        _poseCancelButton->setEnabled (false);

    const std::vector< PoseReachabilitySample > samples =
        _poseReachabilityWatcher->result ();
    if (_poseReachabilityCancelRequested) {
        setStatus (tr("Pose reachability run finished after cancellation request."));
    }
    _poseReachabilitySamples = samples;
    applyPoseReachabilityResults (_poseReachabilitySamples);
    updateReportSummary ();
    setStatus (tr("Pose reachability completed for %1 position(s).")
                   .arg (static_cast< int > (_poseReachabilitySamples.size ())));
}
```

Note: this is cancel-safe UI state, not hard cancellation of the solver. Hard cancellation requires Task 5.

- [ ] **Step 7: CMake**

If `QtConcurrent` is not already part of `${QT_LIBRARIES}`, add conditional target linkage for Qt 5/6 Concurrent in `CMakeLists.txt`:

```cmake
foreach(qt_concurrent_target Qt6::Concurrent Qt5::Concurrent)
    if(TARGET ${qt_concurrent_target})
        target_link_libraries(${SUBSYS_NAME} PRIVATE ${qt_concurrent_target})
    endif()
endforeach()
```

- [ ] **Step 8: Verify**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Manual smoke:

1. Start a larger run, for example 10 positions, 48 directions, 2 rolls.
2. Confirm the main RobWorkStudio window remains responsive.
3. Confirm Run is disabled while active.
4. Confirm Cancel disables itself and the UI state recovers after the worker returns.

- [ ] **Step 9: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt
git commit -m "feat: run pose reachability in background"
```

---

### Task 5: Add Cooperative Cancellation to the Analyzer

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add callback options**

In `KinematicAnalyzer.hpp`, add before the class:

```cpp
struct PoseReachabilityRunCallbacks
{
    bool (*isCancellationRequested) (void* userData) = NULL;
    void (*onProgress) (std::size_t completedTargets,
                        std::size_t plannedTargets,
                        void* userData) = NULL;
    void* userData = NULL;
};
```

Add an overload:

```cpp
std::vector< PoseReachabilitySample > analyzePoseReachability (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< std::array< double, 3 > >& positions,
    const PoseReachabilityConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
    const PoseReachabilityRunCallbacks& callbacks) const;
```

Keep the old overload and have it call the new overload with default callbacks.

- [ ] **Step 2: Check cancellation between IK targets**

Inside the direction/roll loop:

```cpp
if (callbacks.isCancellationRequested != NULL &&
    callbacks.isCancellationRequested (callbacks.userData)) {
    sample.status = sample.reachableDirections == 0 ?
        AnalysisStatus::Fail : AnalysisStatus::Warning;
    sample.coverage = totalDirections == 0 ? 0.0 :
        static_cast< double > (sample.reachableDirections) /
            static_cast< double > (totalDirections);
    results.push_back (sample);
    return results;
}
```

Track `completedTargets` and call progress after each IK:

```cpp
++completedTargets;
if (callbacks.onProgress != NULL)
    callbacks.onProgress (
        completedTargets, diagnostics.plannedIkTargets, callbacks.userData);
```

- [ ] **Step 3: Use atomic cancel flag in Widget**

Prefer replacing the `bool _poseReachabilityCancelRequested` with:

```cpp
std::atomic_bool _poseReachabilityCancelRequested;
```

Add `<atomic>` include.

Pass callbacks from the future:

```cpp
PoseReachabilityRunCallbacks callbacks;
callbacks.isCancellationRequested = [] (void* userData) -> bool {
    const std::atomic_bool* flag =
        static_cast< const std::atomic_bool* > (userData);
    return flag != NULL && flag->load ();
};
callbacks.userData = &_poseReachabilityCancelRequested;
```

Then call the new overload.

- [ ] **Step 4: Add cancellation unit test**

In `KinematicAnalysisTest.cpp`, add a no-device cancellation test that does not require a WorkCell:

```cpp
static bool alwaysCancelPoseReachability (void*)
{
    return true;
}
```

Then in `testPoseReachability()`:

```cpp
rws::PoseReachabilityRunCallbacks callbacks;
callbacks.isCancellationRequested = &alwaysCancelPoseReachability;
const std::vector< rws::PoseReachabilitySample > canceled =
    analyzer.analyzePoseReachability (
        NULL, NULL, state, positions, config, NULL, callbacks);
if (const int rc = require (canceled.size () == 1,
                            "canceled pose result preserves current position"))
    return rc;
```

- [ ] **Step 5: Verify**

Run CTest command from Task 2, then manually start a larger UI run and press Cancel. Expected: run stops after the current IK target completes, result table may contain partial results, and status clearly says cancellation was requested.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: support cancellable pose reachability analysis"
```

---

### Task 6: Improve CSV/Report Metadata Without Breaking Existing Columns

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Add CSV comment summary**

In `exportPoseReachabilityCsv()`, before the header, write a comment summary:

```cpp
const PoseReachabilitySummary summary =
    summarizePoseReachabilitySamples (_poseReachabilitySamples);
out << "# pose_reachability_summary,total," << summary.totalPositions
    << ",pass," << summary.passCount
    << ",warning," << summary.warningCount
    << ",fail," << summary.failCount
    << ",sampled_directions," << summary.sampledDirections
    << ",reachable_directions," << summary.reachableDirections
    << ",avg_coverage," << summary.averageCoverage
    << ",min_coverage," << summary.minCoverage
    << ",max_coverage," << summary.maxCoverage
    << "\n";
```

Keep the existing header unchanged:

```text
position_x,position_y,position_z,sampled_directions,reachable_directions,coverage,status
```

- [ ] **Step 2: Update report summary average**

In `updateReportSummary()`, replace manual pose coverage averaging with:

```cpp
const PoseReachabilitySummary poseSummary =
    summarizePoseReachabilitySamples (_poseReachabilitySamples);
const double poseCoverage = poseSummary.averageCoverage;
```

- [ ] **Step 3: Update README**

Add under Pose reachability:

```markdown
Pose reachability now shows the planned IK target count before running:
`positions * directionSamples * rollSamples`. Direction samples are clamped to
`[0, 1000]`, roll samples to `[1, 360]`, and total planned targets are capped
for overflow-safe diagnostics. The analysis runs in the background from the UI
and can be cancelled cooperatively between IK targets. CSV export keeps the
existing row header and adds a comment-line summary for downstream tools that
skip lines beginning with `#`.
```

- [ ] **Step 4: Verify**

Run CTest command from Task 2.

Manual export smoke:

1. Run Pose reachability with a small sample.
2. Export CSV.
3. Verify the first line starts with `# pose_reachability_summary`.
4. Verify the second non-comment line is the existing header.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "feat: enrich pose reachability summaries"
```

---

### Task 7: Optional Parallelism Investigation

**Files:**
- Modify only if investigation proves safety:
  - `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
  - `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
  - `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Do not start by coding**

First check RobWork thread-safety assumptions for:

- `rw::kinematics::State` copies.
- `rw::models::Device::setQ` / FK calls on copied states.
- `rw::proximity::CollisionDetector` shared use.
- IK solver construction inside `analyzeIk`.

- [ ] **Step 2: If safe, parallelize at position granularity only**

Recommended boundary: one position per worker, each worker owns its local `State`, local `KinematicAnalyzer`, and no shared mutable sample object.

- [ ] **Step 3: Keep collision-enabled mode serial unless proven safe**

If collision detector is not documented as thread-safe, force serial execution when `config.checkCollision == true`.

- [ ] **Step 4: Add deterministic equivalence test**

For a synthetic no-device/no-TCP case and for any available tiny test WorkCell, compare serial and parallel output exactly:

```cpp
require (serial.size () == parallel.size (), "parallel pose result size");
assertNear (serial[i].coverage, parallel[i].coverage, 1e-12, "parallel coverage");
require (serial[i].status == parallel[i].status, "parallel status");
```

- [ ] **Step 5: Commit only after equivalence holds**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "perf: parallelize safe pose reachability batches"
```

---

## Execution Order for Multiple Agents

Recommended split:

1. Agent A: Task 1 and Task 2.
2. Agent B: Task 3 after Task 1 lands.
3. Agent C: Task 4 after Task 3 lands.
4. Agent D: Task 5 after Task 4 lands.
5. Agent E: Task 6 after Task 1 and Task 3 land.
6. Agent F: Task 7 only after all previous tasks pass.

Do not run Task 4 and Task 5 in parallel against the same widget fields. They touch the same lifecycle code.

## Acceptance Criteria

- Pose reachability planned target count is visible before Run.
- Negative and excessive direction/roll values are sanitized consistently.
- Existing `analyzePoseReachability` behavior remains source-compatible.
- Existing CSV/JSON row fields keep their names and meanings.
- Empty Pose reachability export is blocked with a status message.
- UI remains responsive during long Pose reachability runs.
- Cancel request restores UI state and stops at a deterministic cancellation checkpoint once Task 5 is implemented.
- `sdurws_kinematicanalysis_test` passes.
- Manual smoke in RobWorkStudio passes for:
  - task-point source,
  - manual-row source,
  - collision unavailable fallback,
  - visualization source `Pose reachability`,
  - CSV export.

## Risks and Mitigations

- **Thread safety risk:** Run background analysis as a single worker first; avoid intra-analysis parallelism until documented or tested.
- **Collision detector sharing risk:** Keep detector construction on UI thread only if existing helper returns a safe pointer; otherwise create per-worker detector or force serial mode.
- **UI lifetime risk:** Capture values into futures, not `this` or widgets. Only read watcher result on the UI thread.
- **Behavior drift risk:** Preserve old overload and old result fields. Add tests for no-device, zero-direction, negative-roll, and summary behavior.
- **Large job risk:** Show planned IK target count and cap arithmetic at `MaxPoseReachabilityTargets`.

## Verification Commands

Build test target:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Run full KinematicAnalysis tests:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Build plugin:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```
