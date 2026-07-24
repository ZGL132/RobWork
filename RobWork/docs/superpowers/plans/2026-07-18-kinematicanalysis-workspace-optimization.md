# KinematicAnalysis Workspace Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Optimize the `KinematicAnalysis` plugin Workspace page so workspace sampling is deterministic, bounded, statistically meaningful, better connected to visualization, and safer for large robot DOF/grid configurations.

**Architecture:** Keep the sampling implementation in `KinematicAnalyzer`, but move Workspace-only pure helpers into a new `KinematicAnalysisWorkspace` module. UI code in `KinematicAnalysisWidget` should consume these helpers for summaries, diagnostics, and visualization defaults without introducing a large model/view refactor in this phase.

**Tech Stack:** C++17, Qt Widgets, RobWork/RobWorkStudio, existing `sdurws_kinematicanalysis_test` executable, CMake.

---

## File Structure

- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWorkspace.hpp`
  - Responsibility: pure Workspace helper API: config sanitizing, bounded grid-count calculation, sample summary.
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWorkspace.cpp`
  - Responsibility: implementation of helper API. No Qt dependency.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
  - Add `WorkspaceSamplingDiagnostics` and `WorkspaceSummary`.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
  - Use sanitized config and bounded grid-count helper.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Add seed input, diagnostics label, "Open in Visualization" button, helper slots.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Improve controls, table columns, summary text, visualization linkage, run-state UX.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp`
  - Keep current scalar support, only adjust Workspace default/color mapping from UI if needed.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add unit tests for helper behavior and keep existing sampling tests.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
  - Add new `.cpp/.hpp` to plugin and test targets.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Document Workspace controls, limits, summary fields, visualization workflow.

---

## Task 1: Add Workspace Helper Types

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`

- [ ] **Step 1: Add diagnostics and summary structs after `WorkspaceSamplingConfig`**

Add this immediately after `struct WorkspaceSamplingConfig`.

```cpp
struct WorkspaceSamplingDiagnostics
{
    std::size_t requestedSamples = 0;
    std::size_t plannedSamples = 0;
    std::size_t theoreticalGridSamples = 0;
    bool gridCountTruncated = false;
    bool sampleCountClamped = false;
    bool gridStepsClamped = false;
    bool randomSeedAdjusted = false;
};

struct WorkspaceSummary
{
    std::size_t totalCount = 0;
    std::size_t passCount = 0;
    std::size_t warningCount = 0;
    std::size_t failCount = 0;
    std::size_t unknownCount = 0;
    std::size_t collisionCount = 0;
    std::size_t collisionFreeCount = 0;

    bool hasManipulability = false;
    double minManipulability = 0.0;
    double maxManipulability = 0.0;
    double avgManipulability = 0.0;
    double p10Manipulability = 0.0;

    bool hasCondition = false;
    double minCondition = 0.0;
    double maxCondition = 0.0;
    double avgCondition = 0.0;

    bool hasJointLimitMargin = false;
    double minJointLimitMargin = 0.0;
};
```

- [ ] **Step 2: Run a compile check and expect missing helper only later**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug --parallel 4'
```

Expected: PASS, because only passive types were added.

- [ ] **Step 3: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp
git commit -m "feat: add workspace summary types"
```

---

## Task 2: Add Pure Workspace Helper Module

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWorkspace.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWorkspace.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`

- [ ] **Step 1: Write failing tests for summary and config helpers**

In `KinematicAnalysisTest.cpp`, add `#include "KinematicAnalysisWorkspace.hpp"` near the other local includes.

Add this test before `testWorkspaceSampling()`.

```cpp
static int testWorkspaceHelpers ()
{
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = -7;
        config.gridStepsPerJoint = 0;
        config.randomSeed = 0;

        rws::WorkspaceSamplingDiagnostics diagnostics;
        const rws::WorkspaceSamplingConfig sanitized =
            rws::sanitizeWorkspaceSamplingConfig (config, &diagnostics);

        if (const int rc = require (sanitized.sampleCount == 0,
                                    "workspace sanitize clamps negative sample count"))
            return rc;
        if (const int rc = require (sanitized.gridStepsPerJoint == 1,
                                    "workspace sanitize clamps grid steps"))
            return rc;
        if (const int rc = require (sanitized.randomSeed == 1,
                                    "workspace sanitize adjusts zero seed"))
            return rc;
        if (const int rc = require (diagnostics.sampleCountClamped,
                                    "workspace diagnostics sample count clamped"))
            return rc;
        if (const int rc = require (diagnostics.gridStepsClamped,
                                    "workspace diagnostics grid steps clamped"))
            return rc;
        if (const int rc = require (diagnostics.randomSeedAdjusted,
                                    "workspace diagnostics random seed adjusted"))
            return rc;
    }

    {
        rws::WorkspaceSamplingConfig config;
        config.mode = rws::WorkspaceSamplingMode::Grid;
        config.sampleCount = 100;
        config.gridStepsPerJoint = 4;

        rws::WorkspaceSamplingDiagnostics diagnostics;
        const std::size_t planned =
            rws::plannedWorkspaceSampleCount (config, 6, &diagnostics);

        if (const int rc = require (planned == 100,
                                    "workspace grid planning respects sample cap"))
            return rc;
        if (const int rc = require (diagnostics.theoreticalGridSamples == 4096,
                                    "workspace grid theoretical count"))
            return rc;
        if (const int rc = require (diagnostics.gridCountTruncated,
                                    "workspace grid diagnostics truncated"))
            return rc;
    }

    {
        rws::WorkspaceSample pass;
        pass.status = rws::AnalysisStatus::Pass;
        pass.manipulability = 10.0;
        pass.conditionNumber = 20.0;
        pass.minJointLimitMargin = 0.3;

        rws::WorkspaceSample warning;
        warning.status = rws::AnalysisStatus::Warning;
        warning.manipulability = 2.0;
        warning.conditionNumber = 100.0;
        warning.minJointLimitMargin = 0.1;

        rws::WorkspaceSample fail;
        fail.status = rws::AnalysisStatus::Fail;
        fail.inCollision = true;
        fail.manipulability = std::numeric_limits<double>::infinity ();
        fail.conditionNumber = std::numeric_limits<double>::infinity ();
        fail.minJointLimitMargin = 0.0;

        const rws::WorkspaceSummary summary = rws::summarizeWorkspaceSamples (
            std::vector< rws::WorkspaceSample > {pass, warning, fail});

        if (const int rc = require (summary.totalCount == 3, "workspace summary total"))
            return rc;
        if (const int rc = require (summary.passCount == 1, "workspace summary pass"))
            return rc;
        if (const int rc = require (summary.warningCount == 1, "workspace summary warning"))
            return rc;
        if (const int rc = require (summary.failCount == 1, "workspace summary fail"))
            return rc;
        if (const int rc = require (summary.collisionCount == 1, "workspace summary collision"))
            return rc;
        if (const int rc = assertNear (summary.avgManipulability, 6.0, 1e-12,
                                       "workspace summary avg manipulability"))
            return rc;
        if (const int rc = assertNear (summary.p10Manipulability, 2.0, 1e-12,
                                       "workspace summary p10 manipulability"))
            return rc;
        if (const int rc = assertNear (summary.maxCondition, 100.0, 1e-12,
                                       "workspace summary finite max condition"))
            return rc;
        if (const int rc = assertNear (summary.minJointLimitMargin, 0.0, 1e-12,
                                       "workspace summary min margin"))
            return rc;
    }

    return 0;
}
```

Register it in `main()` near the other tests:

```cpp
if (const int rc = testWorkspaceHelpers ())
    return rc;
```

- [ ] **Step 2: Run test and verify it fails**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug --parallel 4'
```

Expected: FAIL with missing `KinematicAnalysisWorkspace.hpp` or missing helper symbols.

- [ ] **Step 3: Create the helper header**

Create `KinematicAnalysisWorkspace.hpp`:

```cpp
#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP

#include "KinematicAnalysisTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

static const int MaxWorkspaceSampleCount = 1000000;
static const int MaxWorkspaceGridStepsPerJoint = 100;

WorkspaceSamplingConfig sanitizeWorkspaceSamplingConfig (
    const WorkspaceSamplingConfig& config,
    WorkspaceSamplingDiagnostics* diagnostics = nullptr);

std::size_t plannedWorkspaceSampleCount (
    const WorkspaceSamplingConfig& config,
    std::size_t dof,
    WorkspaceSamplingDiagnostics* diagnostics = nullptr);

WorkspaceSummary summarizeWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP
```

- [ ] **Step 4: Create the helper implementation**

Create `KinematicAnalysisWorkspace.cpp`:

```cpp
#include "KinematicAnalysisWorkspace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

namespace {

void addFinite (std::vector< double >& values, double value)
{
    if (std::isfinite (value))
        values.push_back (value);
}

double averageOf (const std::vector< double >& values)
{
    if (values.empty ())
        return 0.0;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / static_cast< double > (values.size ());
}

double lowerPercentile (std::vector< double > values, double ratio)
{
    if (values.empty ())
        return 0.0;
    std::sort (values.begin (), values.end ());
    const std::size_t index = static_cast< std::size_t > (
        std::floor (ratio * static_cast< double > (values.size () - 1)));
    return values[index];
}

std::size_t multiplyCapped (std::size_t lhs, std::size_t rhs, std::size_t cap)
{
    if (lhs == 0 || rhs == 0)
        return 0;
    if (lhs > cap / rhs)
        return cap;
    return lhs * rhs;
}

}    // namespace

WorkspaceSamplingConfig rws::sanitizeWorkspaceSamplingConfig (
    const WorkspaceSamplingConfig& config,
    WorkspaceSamplingDiagnostics* diagnostics)
{
    if (diagnostics != nullptr)
        *diagnostics = WorkspaceSamplingDiagnostics ();

    WorkspaceSamplingConfig sanitized = config;
    if (sanitized.sampleCount < 0) {
        sanitized.sampleCount = 0;
        if (diagnostics != nullptr)
            diagnostics->sampleCountClamped = true;
    }
    if (sanitized.sampleCount > MaxWorkspaceSampleCount) {
        sanitized.sampleCount = MaxWorkspaceSampleCount;
        if (diagnostics != nullptr)
            diagnostics->sampleCountClamped = true;
    }
    if (sanitized.gridStepsPerJoint < 1) {
        sanitized.gridStepsPerJoint = 1;
        if (diagnostics != nullptr)
            diagnostics->gridStepsClamped = true;
    }
    if (sanitized.gridStepsPerJoint > MaxWorkspaceGridStepsPerJoint) {
        sanitized.gridStepsPerJoint = MaxWorkspaceGridStepsPerJoint;
        if (diagnostics != nullptr)
            diagnostics->gridStepsClamped = true;
    }
    if (sanitized.randomSeed == 0) {
        sanitized.randomSeed = 1;
        if (diagnostics != nullptr)
            diagnostics->randomSeedAdjusted = true;
    }
    if (diagnostics != nullptr)
        diagnostics->requestedSamples = static_cast< std::size_t > (std::max (0, config.sampleCount));
    return sanitized;
}

std::size_t rws::plannedWorkspaceSampleCount (
    const WorkspaceSamplingConfig& config,
    std::size_t dof,
    WorkspaceSamplingDiagnostics* diagnostics)
{
    WorkspaceSamplingDiagnostics local;
    WorkspaceSamplingConfig sanitized = sanitizeWorkspaceSamplingConfig (config, &local);
    if (dof == 0 || sanitized.sampleCount <= 0) {
        if (diagnostics != nullptr)
            *diagnostics = local;
        return 0;
    }

    std::size_t planned = static_cast< std::size_t > (sanitized.sampleCount);
    if (sanitized.mode == WorkspaceSamplingMode::Grid) {
        const std::size_t cap = static_cast< std::size_t > (MaxWorkspaceSampleCount);
        std::size_t total = 1;
        for (std::size_t i = 0; i < dof; ++i)
            total = multiplyCapped (total, static_cast< std::size_t > (sanitized.gridStepsPerJoint), cap);
        local.theoreticalGridSamples = total;
        planned = std::min (planned, total);
        local.gridCountTruncated = total > planned;
    }

    local.plannedSamples = planned;
    if (diagnostics != nullptr)
        *diagnostics = local;
    return planned;
}

WorkspaceSummary rws::summarizeWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples)
{
    WorkspaceSummary summary;
    summary.totalCount = samples.size ();

    std::vector< double > manipulabilityValues;
    std::vector< double > conditionValues;
    std::vector< double > marginValues;
    manipulabilityValues.reserve (samples.size ());
    conditionValues.reserve (samples.size ());
    marginValues.reserve (samples.size ());

    for (const WorkspaceSample& sample : samples) {
        switch (sample.status) {
            case AnalysisStatus::Pass:
                ++summary.passCount;
                break;
            case AnalysisStatus::Warning:
                ++summary.warningCount;
                break;
            case AnalysisStatus::Fail:
                ++summary.failCount;
                break;
            case AnalysisStatus::Unknown:
            default:
                ++summary.unknownCount;
                break;
        }
        if (sample.inCollision)
            ++summary.collisionCount;
        else
            ++summary.collisionFreeCount;

        addFinite (manipulabilityValues, sample.manipulability);
        addFinite (conditionValues, sample.conditionNumber);
        addFinite (marginValues, sample.minJointLimitMargin);
    }

    if (!manipulabilityValues.empty ()) {
        summary.hasManipulability = true;
        summary.minManipulability = *std::min_element (manipulabilityValues.begin (), manipulabilityValues.end ());
        summary.maxManipulability = *std::max_element (manipulabilityValues.begin (), manipulabilityValues.end ());
        summary.avgManipulability = averageOf (manipulabilityValues);
        summary.p10Manipulability = lowerPercentile (manipulabilityValues, 0.10);
    }
    if (!conditionValues.empty ()) {
        summary.hasCondition = true;
        summary.minCondition = *std::min_element (conditionValues.begin (), conditionValues.end ());
        summary.maxCondition = *std::max_element (conditionValues.begin (), conditionValues.end ());
        summary.avgCondition = averageOf (conditionValues);
    }
    if (!marginValues.empty ()) {
        summary.hasJointLimitMargin = true;
        summary.minJointLimitMargin = *std::min_element (marginValues.begin (), marginValues.end ());
    }
    return summary;
}
```

- [ ] **Step 5: Add helper files to CMake**

In `CMakeLists.txt`, add `KinematicAnalysisWorkspace.cpp` to both `SrcFiles` and `sdurws_kinematicanalysis_test`; add `KinematicAnalysisWorkspace.hpp` to `SRC_FILES_HPP`.

- [ ] **Step 6: Run tests**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug --parallel 4 && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe all'
```

Expected: build succeeds and test output ends with all suites passing.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWorkspace.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWorkspace.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt
git commit -m "feat: add workspace helper calculations"
```

---

## Task 3: Use Sanitized Config in Sampling Core

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add include**

Add near the local includes:

```cpp
#include "KinematicAnalysisWorkspace.hpp"
```

- [ ] **Step 2: Replace direct config usage in `KinematicAnalyzer::sampleWorkspace`**

At the top of the method after `std::vector< WorkspaceSample > samples;`, add:

```cpp
const WorkspaceSamplingConfig sanitized = sanitizeWorkspaceSamplingConfig (config);
```

Then replace:

```cpp
if (config.sampleCount <= 0)
```

with:

```cpp
if (sanitized.sampleCount <= 0)
```

Replace random mode checks and values:

```cpp
if (config.mode == WorkspaceSamplingMode::RandomUniform) {
    std::mt19937 rng (config.randomSeed == 0 ? 1u : config.randomSeed);
    ...
    samples.reserve (static_cast< std::size_t > (config.sampleCount));
    for (int sampleIndex = 0; sampleIndex < config.sampleCount; ++sampleIndex) {
    ...
        config.checkCollision, collisionDetector));
```

with:

```cpp
if (sanitized.mode == WorkspaceSamplingMode::RandomUniform) {
    std::mt19937 rng (sanitized.randomSeed);
    ...
    samples.reserve (static_cast< std::size_t > (sanitized.sampleCount));
    for (int sampleIndex = 0; sampleIndex < sanitized.sampleCount; ++sampleIndex) {
    ...
        sanitized.checkCollision, collisionDetector));
```

Replace the grid target calculation block with:

```cpp
const int steps = sanitized.gridStepsPerJoint;
const std::size_t target = plannedWorkspaceSampleCount (sanitized, dof);
```

Also replace the grid call to `makeWorkspaceSample` to use `sanitized.checkCollision`.

- [ ] **Step 3: Expand existing sampling test for config sanitizing path**

In `testWorkspaceSampling()`, keep existing null-device tests. Add one assertion that `plannedWorkspaceSampleCount()` returns zero for negative sample count and no DOF:

```cpp
{
    rws::WorkspaceSamplingConfig config;
    config.sampleCount = -1;
    rws::WorkspaceSamplingDiagnostics diagnostics;
    const std::size_t planned =
        rws::plannedWorkspaceSampleCount (config, 6, &diagnostics);
    if (const int rc = require (planned == 0,
                                "workspace planned count handles negative count"))
        return rc;
    if (const int rc = require (diagnostics.sampleCountClamped,
                                "workspace planned count reports clamped negative count"))
        return rc;
}
```

- [ ] **Step 4: Run tests**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug --parallel 4 && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe all'
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "fix: sanitize workspace sampling config"
```

---

## Task 4: Improve Workspace UI Controls and Summary

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add UI members and slots**

In `KinematicAnalysisWidget.hpp`, add private slots:

```cpp
void updateWorkspaceControls ();
void openWorkspaceInVisualization ();
```

Add members near other Workspace fields:

```cpp
QSpinBox* _workspaceSeedSpin;
QLabel* _workspaceDiagnosticsLabel;
QPushButton* _workspaceOpenVisualizationButton;
```

- [ ] **Step 2: Add include in widget implementation**

Add:

```cpp
#include "KinematicAnalysisWorkspace.hpp"
```

- [ ] **Step 3: Add seed and visualization controls in `buildWorkspaceTab()`**

After `_workspaceGridStepsSpin` setup, add:

```cpp
_workspaceSeedSpin = new QSpinBox (_workspaceTab);
_workspaceSeedSpin->setRange (1, 2147483647);
_workspaceSeedSpin->setValue (1);
```

After button creation, add:

```cpp
_workspaceOpenVisualizationButton = new QPushButton (tr("Open in Visualization"), _workspaceTab);
_workspaceOpenVisualizationButton->setEnabled (false);
```

Change the control layout to include:

```cpp
controls->addWidget (new QLabel (tr("Seed:"), _workspaceTab), 2, 0);
controls->addWidget (_workspaceSeedSpin, 2, 1);
controls->addWidget (_workspaceRunButton, 3, 0);
controls->addWidget (_workspaceExportButton, 3, 1);
controls->addWidget (_workspaceOpenVisualizationButton, 3, 2);
```

Add diagnostics label after `_workspaceSummaryLabel`:

```cpp
_workspaceDiagnosticsLabel = new QLabel (tr("Grid plan: -"), _workspaceTab);
layout->addWidget (_workspaceDiagnosticsLabel);
```

- [ ] **Step 4: Add condition-number column**

Change:

```cpp
_workspaceTable->setColumnCount (8);
```

to:

```cpp
_workspaceTable->setColumnCount (9);
```

Update headers:

```cpp
_workspaceTable->setHorizontalHeaderLabels ({
    tr("Index"), tr("Status"), tr("Collision"), tr("TCP x"), tr("TCP y"), tr("TCP z"),
    tr("Manipulability"), tr("Condition"), tr("Min limit margin")
});
```

- [ ] **Step 5: Connect new controls**

In the existing connect block, add:

```cpp
connect (_workspaceModeCombo, SIGNAL (currentIndexChanged (int)),
         this, SLOT (updateWorkspaceControls ()));
connect (_workspaceSampleCountSpin, SIGNAL (valueChanged (int)),
         this, SLOT (updateWorkspaceControls ()));
connect (_workspaceGridStepsSpin, SIGNAL (valueChanged (int)),
         this, SLOT (updateWorkspaceControls ()));
connect (_workspaceColorModeCombo, SIGNAL (currentIndexChanged (int)),
         this, SLOT (refreshVisualization ()));
connect (_workspaceOpenVisualizationButton, SIGNAL (clicked ()),
         this, SLOT (openWorkspaceInVisualization ()));
```

Call `updateWorkspaceControls();` at the end of `buildWorkspaceTab()`.

- [ ] **Step 6: Implement `updateWorkspaceControls()`**

Add near `sampleWorkspace()`:

```cpp
void KinematicAnalysisWidget::updateWorkspaceControls ()
{
    if (_workspaceModeCombo == NULL || _workspaceGridStepsSpin == NULL ||
        _workspaceSampleCountSpin == NULL)
        return;

    const bool gridMode = _workspaceModeCombo->currentIndex () == 1;
    _workspaceGridStepsSpin->setEnabled (gridMode);

    WorkspaceSamplingConfig config;
    config.sampleCount = _workspaceSampleCountSpin->value ();
    config.gridStepsPerJoint = _workspaceGridStepsSpin->value ();
    config.mode = gridMode ? WorkspaceSamplingMode::Grid : WorkspaceSamplingMode::RandomUniform;
    config.randomSeed = _workspaceSeedSpin != NULL ?
        static_cast< unsigned int > (_workspaceSeedSpin->value ()) : 1u;

    const rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    const std::size_t dof = device == NULL ? 0 : device->getDOF ();
    WorkspaceSamplingDiagnostics diagnostics;
    const std::size_t planned = plannedWorkspaceSampleCount (config, dof, &diagnostics);

    if (_workspaceDiagnosticsLabel != NULL) {
        if (!gridMode) {
            _workspaceDiagnosticsLabel->setText (
                tr("Plan: %1 random sample(s), seed %2")
                    .arg (static_cast< int > (planned))
                    .arg (_workspaceSeedSpin != NULL ? _workspaceSeedSpin->value () : 1));
        }
        else {
            _workspaceDiagnosticsLabel->setText (
                tr("Plan: %1 grid sample(s), theoretical %2%3")
                    .arg (static_cast< int > (planned))
                    .arg (static_cast< int > (diagnostics.theoreticalGridSamples))
                    .arg (diagnostics.gridCountTruncated ? tr(" (capped)") : QString ()));
        }
    }
}
```

- [ ] **Step 7: Use helper summary in `applyWorkspaceResults()`**

Replace manual counters with:

```cpp
const WorkspaceSummary summary = summarizeWorkspaceSamples (samples);
```

Set 9 columns per visible row:

```cpp
_workspaceTable->setItem (row, 6, makeItem (sample.manipulability));
_workspaceTable->setItem (row, 7, makeItem (sample.conditionNumber));
_workspaceTable->setItem (row, 8, makeItem (sample.minJointLimitMargin));
```

Replace summary label text with:

```cpp
_workspaceSummaryLabel->setText (
    tr("Samples: %1    Shown: %2    Pass: %3    Warning: %4    Fail: %5    Collision-free: %6    Avg manip: %7    P10 manip: %8    Max cond: %9")
        .arg (static_cast< int > (summary.totalCount))
        .arg (rows)
        .arg (static_cast< int > (summary.passCount))
        .arg (static_cast< int > (summary.warningCount))
        .arg (static_cast< int > (summary.failCount))
        .arg (static_cast< int > (summary.collisionFreeCount))
        .arg (summary.hasManipulability ? QString::number (summary.avgManipulability, 'g', 6) : QStringLiteral ("-"))
        .arg (summary.hasManipulability ? QString::number (summary.p10Manipulability, 'g', 6) : QStringLiteral ("-"))
        .arg (summary.hasCondition ? QString::number (summary.maxCondition, 'g', 6) : QStringLiteral ("-")));
```

Enable buttons:

```cpp
if (_workspaceExportButton != NULL)
    _workspaceExportButton->setEnabled (!samples.empty ());
if (_workspaceOpenVisualizationButton != NULL)
    _workspaceOpenVisualizationButton->setEnabled (!samples.empty ());
```

- [ ] **Step 8: Use seed and busy state in `sampleWorkspace()`**

Set seed:

```cpp
config.randomSeed = _workspaceSeedSpin != NULL ?
    static_cast< unsigned int > (_workspaceSeedSpin->value ()) : 1u;
```

Wrap synchronous sampling:

```cpp
if (_workspaceRunButton != NULL)
    _workspaceRunButton->setEnabled (false);
QApplication::setOverrideCursor (Qt::WaitCursor);
_workspaceSamples = analyzer.sampleWorkspace (
    device, tcpFrame, currentState (), config, collisionDetector);
QApplication::restoreOverrideCursor ();
if (_workspaceRunButton != NULL)
    _workspaceRunButton->setEnabled (true);
```

Add a local guard if the codebase prefers exception safety:

```cpp
struct CursorGuard {
    CursorGuard () { QApplication::setOverrideCursor (Qt::WaitCursor); }
    ~CursorGuard () { QApplication::restoreOverrideCursor (); }
};
```

- [ ] **Step 9: Implement `openWorkspaceInVisualization()`**

```cpp
void KinematicAnalysisWidget::openWorkspaceInVisualization ()
{
    if (_visualSourceCombo != NULL)
        _visualSourceCombo->setCurrentIndex (1);
    if (_visualColorModeCombo != NULL && _workspaceColorModeCombo != NULL) {
        const int workspaceMode = _workspaceColorModeCombo->currentIndex ();
        const VisualScalarMode scalar =
            workspaceMode == 1 ? VisualScalarMode::Manipulability :
            workspaceMode == 2 ? VisualScalarMode::MinJointMargin :
            workspaceMode == 3 ? VisualScalarMode::Collision :
                                 VisualScalarMode::Status;
        const int index = _visualColorModeCombo->findData (static_cast< int > (scalar));
        if (index >= 0)
            _visualColorModeCombo->setCurrentIndex (index);
    }
    if (_tabs != NULL && _visualizationTab != NULL)
        _tabs->setCurrentWidget (_visualizationTab);
    refreshVisualization ();
}
```

- [ ] **Step 10: Run build**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug --parallel 4'
```

Expected: plugin builds without Qt signal/slot or missing include errors.

- [ ] **Step 11: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: improve workspace controls and summary"
```

---

## Task 5: Improve Export and Report Coverage

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Guard empty export**

At the top of `exportWorkspaceCsv()`:

```cpp
if (_workspaceSamples.empty ()) {
    setStatus (tr("No workspace samples to export."));
    return;
}
```

- [ ] **Step 2: Add summary preamble to CSV as comment lines**

Before the existing CSV header, add:

```cpp
const WorkspaceSummary summary = summarizeWorkspaceSamples (_workspaceSamples);
out << "# workspace_summary,total," << summary.totalCount
    << ",pass," << summary.passCount
    << ",warning," << summary.warningCount
    << ",fail," << summary.failCount
    << ",collision," << summary.collisionCount
    << ",avg_manipulability," << summary.avgManipulability
    << ",p10_manipulability," << summary.p10Manipulability
    << ",max_condition," << summary.maxCondition
    << "\n";
```

Keep the existing sample header unchanged so downstream scripts that skip comment lines still work:

```cpp
out << "sample_index,q,tcp_x,tcp_y,tcp_z,manipulability,min_joint_limit_margin,condition_number,in_collision,status\n";
```

- [ ] **Step 3: Add Workspace summary to `updateReportSummary()`**

Find the existing report summary construction and append:

```cpp
const WorkspaceSummary workspaceSummary = summarizeWorkspaceSamples (_workspaceSamples);
const QString workspaceLine =
    tr("Workspace: %1 samples, pass %2, warning %3, fail %4, collision %5, avg manip %6, max cond %7")
        .arg (static_cast< int > (workspaceSummary.totalCount))
        .arg (static_cast< int > (workspaceSummary.passCount))
        .arg (static_cast< int > (workspaceSummary.warningCount))
        .arg (static_cast< int > (workspaceSummary.failCount))
        .arg (static_cast< int > (workspaceSummary.collisionCount))
        .arg (workspaceSummary.hasManipulability ? QString::number (workspaceSummary.avgManipulability, 'g', 6) : QStringLiteral ("-"))
        .arg (workspaceSummary.hasCondition ? QString::number (workspaceSummary.maxCondition, 'g', 6) : QStringLiteral ("-"));
```

Include `workspaceLine` in the label text.

- [ ] **Step 4: Run build and smoke test**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug --parallel 4'
```

Expected: plugin builds.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: include workspace summary in exports"
```

---

## Task 6: Update Documentation and Final Verification

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Update README Workspace section**

Replace any outdated sentence saying Workspace is only table/export oriented with:

```markdown
### Workspace

The Workspace tab samples the selected device joint space using deterministic random sampling or capped grid sampling. The page shows the first 500 rows in the table and keeps the complete sample set for CSV export, report aggregation, and the Visualization tab.

Controls:
- `Samples`: maximum generated sample count.
- `Mode`: `Random uniform` samples inside joint limits; `Grid` enumerates a joint-limit grid and caps the output by `Samples`.
- `Grid steps`: per-joint grid resolution used only in `Grid` mode.
- `Seed`: deterministic random seed used by `Random uniform`.
- `Collision`: enables collision checks when a detector is available.
- `Color`: preferred scalar when opening the data in the Visualization tab.

The summary reports pass/warning/fail counts, collision-free count, average and P10 manipulability, and maximum finite condition number. CSV export includes a comment-line summary followed by one row per sample.
```

- [ ] **Step 2: Run unit tests**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug --parallel 4 && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe all'
```

Expected: all tests pass.

- [ ] **Step 3: Build Debug plugin**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug --parallel 4'
```

Expected: build succeeds.

- [ ] **Step 4: Build Release plugin**

Run:

```bat
cmd /c '"D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_kinematicanalysis --config Release --parallel 4'
```

Expected: build succeeds.

- [ ] **Step 5: Manual UI smoke test**

In RobWorkStudio with a valid WorkCell/device:

1. Open `KinematicAnalysis`.
2. Go to `Workspace`.
3. Run `Random uniform`, `Samples=100`, `Seed=1`, `Collision` on.
4. Confirm summary shows `Samples: 100`, table has 9 columns including `Condition`, and `Export CSV` is enabled.
5. Change to `Grid`, set `Grid steps=5`, confirm diagnostics label shows planned sample count and capped note when applicable.
6. Click `Open in Visualization`, confirm Visualization source becomes `Workspace` and color follows the Workspace color combo.
7. Export CSV and confirm the file starts with `# workspace_summary` and then the existing `sample_index,...` header.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "docs: document workspace optimization controls"
```

---

## Completion Criteria

- Workspace random sampling is deterministic from a visible seed control.
- Grid sampling shows planned/theoretical count and warns when capped.
- Core sampling uses sanitized config consistently.
- Workspace table includes `conditionNumber`.
- Workspace summary is produced by a tested pure helper, not repeated UI-only loops.
- Workspace color preference can open/update the Visualization tab.
- CSV export includes high-level summary while preserving per-sample data columns.
- Debug tests pass.
- Debug and Release plugin builds pass.

## Out-of-Scope for This Phase

- Full async sampling/cancel/progress worker. This is useful but should be a separate P5 because it changes threading and object lifetime rules.
- Replacing Workspace `QTableWidget` with `QAbstractTableModel`. Task points already completed that larger refactor; Workspace can wait until sampling and summary semantics are stable.
- 3D point cloud rendering. Current P3 visualization is already 2D projection based; true 3D rendering should be planned separately if needed.

