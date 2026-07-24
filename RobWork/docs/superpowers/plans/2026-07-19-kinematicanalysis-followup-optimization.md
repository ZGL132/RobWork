# KinematicAnalysis Follow-Up Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the remaining responsiveness, thread-safety, visualization layout, export-format, and maintenance risks in `sdurws_kinematicanalysis` without changing the plugin's user-facing analysis semantics.

**Architecture:** Keep core numerical logic in `KinematicAnalyzer` and small helper modules; keep Qt UI orchestration in `KinematicAnalysisWidget`. Add callback-based progress/cancel support to workspace sampling, create per-run collision detectors for background work, and move JSON finite-number policy into a testable helper. Do not perform a broad widget rewrite in this pass; leave large-file decomposition as a documented follow-up after the behavioral fixes are verified.

**Tech Stack:** C++17-style RobWork code, Qt Widgets, QtConcurrent/QFutureWatcher, RobWork `CollisionDetector`, `ProximityStrategyFactory`, CMake, the existing `sdurws_kinematicanalysis_test` executable.

---

## Current Findings To Address

1. `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp` still runs `sampleWorkspace()` synchronously on the UI thread. Large random/grid runs block RobWorkStudio until completion.
2. `analyzePoseReachability()` starts a `QtConcurrent::run` worker but captures a collision detector returned by `_studio->getCollisionDetector()`. That detector is owned by the UI/RobWorkStudio scene and should not be reused from a background thread.
3. `KinematicAnalysisPlotWidget::paintPlot()` gives the plot only an 18 px right margin, while `paintLegend()` draws at `plotArea.right() + 6`. On normal widget widths and exported images the legend can clip outside the target area.
4. JSON export handles `bestSolution.conditionNumber == +inf`, but `currentPose.conditionNumber` and `workspaceSamples[].conditionNumber` are written as raw doubles. Qt JSON cannot represent `NaN`/`Inf` consistently.
5. `CMakeLists.txt` links `sdurws_kinematicanalysis_test` to QtConcurrent before the test target is created. It may pass on the current generator, but the target order is fragile.
6. Test suite names are confusing: `pose_reachability` currently runs helper tests and `pose` runs the real pose reachability algorithm.
7. `KinematicAnalysisWidget.cpp` is very large. This plan deliberately avoids a broad rewrite; it introduces only small testable helpers and a bounded async workflow.

## File Structure

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
  - Add new helper files to the plugin and test executable.
  - Move QtConcurrent linkage for the test executable after `add_executable`.

- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisCollision.hpp`
  - Declares a background-safe collision detector factory.

- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisCollision.cpp`
  - Builds a fresh `rw::proximity::CollisionDetector` from a `WorkCell`.

- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisJson.hpp`
  - Declares JSON conversion helpers for finite and non-finite floating-point values.

- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisJson.cpp`
  - Implements `QJsonValue` conversion for `finite`, `+inf`, `-inf`, and `nan`.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
  - Add `WorkspaceSamplingRunCallbacks`.
  - Add a callback-aware `sampleWorkspace()` overload.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
  - Make the existing `sampleWorkspace()` delegate to the callback-aware overload.
  - Add cooperative cancel/progress notifications to random and grid loops.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Add workspace async slots and members.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Add workspace progress/cancel controls.
  - Run workspace sampling through `QtConcurrent::run`.
  - Construct worker-local collision detectors for workspace and pose reachability background runs.
  - Use JSON helper in `exportReportJson()`.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp`
  - Add a layout helper or update legend method signature so legend placement depends on the render target area.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`
  - Reserve legend width before computing the plot rectangle.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add tests for JSON helpers.
  - Add tests for workspace sampling progress/cancel callbacks.
  - Add clearer test suite names while preserving old aliases.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Document async workspace sampling, cancel behavior, background collision detector policy, JSON non-finite policy, and visualization legend behavior.

---

### Task 1: Stabilize CMake Target Order And Test Suite Names

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Move QtConcurrent linkage for the test target after `add_executable`**

In `CMakeLists.txt`, keep plugin linkage before `add_executable`, but remove this line from the early loop:

```cmake
target_link_libraries(sdurws_kinematicanalysis_test PRIVATE ${qt_concurrent_target})
```

The early block should become:

```cmake
    foreach(qt_concurrent_target Qt6::Concurrent Qt5::Concurrent)
        if(TARGET ${qt_concurrent_target})
            target_link_libraries(${SUBSYS_NAME} PRIVATE ${qt_concurrent_target})
        endif()
    endforeach()
```

Immediately after the existing `target_link_libraries(sdurws_kinematicanalysis_test ...)` block, add:

```cmake
    foreach(qt_concurrent_target Qt6::Concurrent Qt5::Concurrent)
        if(TARGET ${qt_concurrent_target})
            target_link_libraries(sdurws_kinematicanalysis_test PRIVATE ${qt_concurrent_target})
        endif()
    endforeach()
```

- [ ] **Step 2: Rename pose reachability suite names and keep aliases**

In `KinematicAnalysisTest.cpp`, replace the `main()` suite dispatch tail:

```cpp
    else if (suite == "pose_reachability")
        rc = testPoseReachabilityHelpers ();
    else if (suite == "pose")
        rc = testPoseReachability ();
```

with:

```cpp
    else if (suite == "pose_reachability_helpers" || suite == "pose_reachability_legacy")
        rc = testPoseReachabilityHelpers ();
    else if (suite == "pose_reachability" || suite == "pose")
        rc = testPoseReachability ();
```

This makes `pose_reachability` mean the algorithm suite while keeping a helper alias available as `pose_reachability_helpers`. The `pose` alias remains for compatibility with older command snippets.

- [ ] **Step 3: Build the test executable**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Expected: build succeeds; no CMake error about linking a target before it exists.

- [ ] **Step 4: Run renamed suites**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe pose_reachability_helpers'
```

Expected output includes:

```text
KinematicAnalysis pose_reachability_helpers test passed.
```

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe pose_reachability'
```

Expected output includes:

```text
KinematicAnalysis pose_reachability test passed.
```

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "chore: stabilize kinematic analysis test wiring"
```

---

### Task 2: Add Testable Helpers For Background Collision And JSON Numbers

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisCollision.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisCollision.cpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisJson.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisJson.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add collision helper header**

Create `KinematicAnalysisCollision.hpp`:

```cpp
#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP

#include <rw/core/Ptr.hpp>

namespace rw { namespace models { class WorkCell; } }
namespace rw { namespace proximity { class CollisionDetector; } }

namespace rws {

rw::core::Ptr< rw::proximity::CollisionDetector > makeKinematicAnalysisCollisionDetector (
    rw::core::Ptr< rw::models::WorkCell > workcell);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP
```

- [ ] **Step 2: Add collision helper implementation**

Create `KinematicAnalysisCollision.cpp`:

```cpp
#include "KinematicAnalysisCollision.hpp"

#include <rw/proximity/CollisionDetector.hpp>
#include <rwlibs/proximitystrategies/ProximityStrategyFactory.hpp>

using rw::core::ownedPtr;
using rw::core::Ptr;
using rw::models::WorkCell;
using rw::proximity::CollisionDetector;
using rwlibs::proximitystrategies::ProximityStrategyFactory;

namespace rws {

Ptr< CollisionDetector > makeKinematicAnalysisCollisionDetector (
    Ptr< WorkCell > workcell)
{
    if (workcell == NULL)
        return NULL;
    return ownedPtr (
        new CollisionDetector (
            workcell,
            ProximityStrategyFactory::makeDefaultCollisionStrategy ()));
}

}    // namespace rws
```

- [ ] **Step 3: Add JSON helper header**

Create `KinematicAnalysisJson.hpp`:

```cpp
#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISJSON_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISJSON_HPP

#include <QJsonValue>

namespace rws {

QJsonValue jsonValueFromDouble (double value);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISJSON_HPP
```

- [ ] **Step 4: Add JSON helper implementation**

Create `KinematicAnalysisJson.cpp`:

```cpp
#include "KinematicAnalysisJson.hpp"

#include <QString>

#include <cmath>

namespace rws {

QJsonValue jsonValueFromDouble (double value)
{
    if (std::isnan (value))
        return QJsonValue (QStringLiteral ("nan"));
    if (std::isinf (value))
        return QJsonValue (value > 0.0 ? QStringLiteral ("inf") : QStringLiteral ("-inf"));
    return QJsonValue (value);
}

}    // namespace rws
```

- [ ] **Step 5: Wire helper files into CMake**

In `CMakeLists.txt`, add the new `.cpp` files to `SrcFiles`:

```cmake
        KinematicAnalysisCollision.cpp
        KinematicAnalysisJson.cpp
```

Add the new headers to `SRC_FILES_HPP`:

```cmake
        KinematicAnalysisCollision.hpp
        KinematicAnalysisJson.hpp
```

Add the new `.cpp` files to `sdurws_kinematicanalysis_test`:

```cmake
        KinematicAnalysisCollision.cpp
        KinematicAnalysisJson.cpp
```

- [ ] **Step 6: Add helper tests**

In `KinematicAnalysisTest.cpp`, add includes near the other kinematicanalysis includes:

```cpp
#include "KinematicAnalysisCollision.hpp"
#include "KinematicAnalysisJson.hpp"
```

If `<limits>` is not already included in the file, add it:

```cpp
#include <limits>
```

Add this test function near other small helper tests:

```cpp
static int testJsonAndCollisionHelpers ()
{
    {
        const QJsonValue finite = jsonValueFromDouble (3.25);
        if (!finite.isDouble ())
            return fail ("finite json value should remain numeric");
        if (const int rc = assertNear (finite.toDouble (), 3.25, 1e-12,
                                       "finite json value"))
            return rc;
    }
    {
        const QJsonValue posInf = jsonValueFromDouble (
            std::numeric_limits< double >::infinity ());
        if (!posInf.isString () || posInf.toString () != QStringLiteral ("inf"))
            return fail ("positive infinity should export as string inf");
    }
    {
        const QJsonValue negInf = jsonValueFromDouble (
            -std::numeric_limits< double >::infinity ());
        if (!negInf.isString () || negInf.toString () != QStringLiteral ("-inf"))
            return fail ("negative infinity should export as string -inf");
    }
    {
        const QJsonValue nanValue = jsonValueFromDouble (
            std::numeric_limits< double >::quiet_NaN ());
        if (!nanValue.isString () || nanValue.toString () != QStringLiteral ("nan"))
            return fail ("NaN should export as string nan");
    }
    {
        const rw::core::Ptr< rw::proximity::CollisionDetector > detector =
            makeKinematicAnalysisCollisionDetector (NULL);
        if (detector != NULL)
            return fail ("null workcell should not create a collision detector");
    }
    return 0;
}
```

Add it to `runAll()` before `testAggregateResult()`:

```cpp
    if (const int rc = testJsonAndCollisionHelpers ())
        return rc;
```

Add a `main()` suite:

```cpp
    else if (suite == "helpers")
        rc = testJsonAndCollisionHelpers ();
```

- [ ] **Step 7: Build and run helper tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Then run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe helpers'
```

Expected output includes:

```text
KinematicAnalysis helpers test passed.
```

- [ ] **Step 8: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisCollision.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisCollision.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisJson.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisJson.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: add kinematic analysis utility helpers"
```

---

### Task 3: Add Progress And Cancel Callbacks To Workspace Sampling Core

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add callback struct and overload declaration**

In `KinematicAnalyzer.hpp`, place this struct after `PoseReachabilityRunCallbacks`:

```cpp
struct WorkspaceSamplingRunCallbacks
{
    bool (*isCancellationRequested) (void* userData) = NULL;
    void (*onProgress) (std::size_t completedSamples,
                        std::size_t plannedSamples,
                        void* userData) = NULL;
    void* userData = NULL;
};
```

Add this overload immediately after the existing five-argument `sampleWorkspace()` declaration:

```cpp
    std::vector< WorkspaceSample > sampleWorkspace (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const WorkspaceSamplingConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
        const WorkspaceSamplingRunCallbacks& callbacks) const;
```

- [ ] **Step 2: Make the existing overload delegate**

In `KinematicAnalyzer.cpp`, replace the current `sampleWorkspace()` body with:

```cpp
std::vector< WorkspaceSample > KinematicAnalyzer::sampleWorkspace (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceSamplingConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    return sampleWorkspace (
        device, tcpFrame, state, config, collisionDetector,
        WorkspaceSamplingRunCallbacks ());
}
```

- [ ] **Step 3: Add the callback-aware implementation**

Move the old implementation body into the new overload and add these helpers near the top of the function:

```cpp
std::vector< WorkspaceSample > KinematicAnalyzer::sampleWorkspace (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceSamplingConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
    const WorkspaceSamplingRunCallbacks& callbacks) const
{
    std::vector< WorkspaceSample > samples;

    const auto canceled = [&callbacks] () -> bool {
        return callbacks.isCancellationRequested != NULL &&
               callbacks.isCancellationRequested (callbacks.userData);
    };
    const auto progress = [&callbacks] (std::size_t completed,
                                        std::size_t planned) {
        if (callbacks.onProgress != NULL)
            callbacks.onProgress (completed, planned, callbacks.userData);
    };

    const WorkspaceSamplingConfig sanitized =
        sanitizeWorkspaceSamplingConfig (config, nullptr);

    if (device == NULL)
        return samples;
    if (sanitized.sampleCount <= 0)
        return samples;
    if (tcpFrame == NULL)
        return samples;
```

Keep the existing bounds validation after this snippet.

- [ ] **Step 4: Add cancel/progress to the random loop**

In the random branch, add an initial progress notification and cancel checks:

```cpp
        const std::size_t planned =
            static_cast< std::size_t > (sanitized.sampleCount);
        progress (0, planned);
        samples.reserve (planned);
        for (int sampleIndex = 0; sampleIndex < sanitized.sampleCount; ++sampleIndex) {
            if (canceled ())
                break;
            rw::math::Q q (dof);
            for (std::size_t j = 0; j < dof; ++j)
                q (j) = distributions[j] (rng);
            samples.push_back (makeWorkspaceSample (
                device, tcpFrame, state, q, _thresholds,
                sanitized.checkCollision, collisionDetector));
            progress (samples.size (), planned);
        }
        return samples;
```

- [ ] **Step 5: Add cancel/progress to the grid loop**

Before the grid loop, after `samples.reserve(target);`, add:

```cpp
    progress (0, target);
```

At the top and bottom of the grid loop, use:

```cpp
    for (std::size_t index = 0; index < target; ++index) {
        if (canceled ())
            break;
        std::size_t cursor = index;
        rw::math::Q q (dof);
        ...
        samples.push_back (makeWorkspaceSample (
            device, tcpFrame, state, q, _thresholds,
            sanitized.checkCollision, collisionDetector));
        progress (samples.size (), target);
    }
```

Keep the existing `q` generation exactly as it is inside the `...` region.

- [ ] **Step 6: Add workspace callback tests**

In `KinematicAnalysisTest.cpp`, extend `testWorkspaceSampling()` with a cancellation/progress block after the existing deterministic sampling checks. Use the existing device/tcp/state setup already inside that function. Add local structs and callbacks:

```cpp
    {
        struct CallbackState {
            std::size_t progressCalls = 0;
            std::size_t lastCompleted = 0;
            std::size_t lastPlanned = 0;
            std::size_t cancelAfter = 3;
        };
        CallbackState callbackState;

        WorkspaceSamplingConfig cancelConfig;
        cancelConfig.mode = WorkspaceSamplingMode::RandomUniform;
        cancelConfig.sampleCount = 20;
        cancelConfig.randomSeed = 11u;
        cancelConfig.checkCollision = false;

        WorkspaceSamplingRunCallbacks callbacks;
        callbacks.userData = &callbackState;
        callbacks.isCancellationRequested = [] (void* userData) -> bool {
            const CallbackState* state =
                static_cast< const CallbackState* > (userData);
            return state != NULL && state->lastCompleted >= state->cancelAfter;
        };
        callbacks.onProgress = [] (std::size_t completed,
                                   std::size_t planned,
                                   void* userData) {
            CallbackState* state = static_cast< CallbackState* > (userData);
            if (state == NULL)
                return;
            ++state->progressCalls;
            state->lastCompleted = completed;
            state->lastPlanned = planned;
        };

        const std::vector< WorkspaceSample > canceledSamples =
            analyzer.sampleWorkspace (
                device, tcpFrame, state, cancelConfig, NULL, callbacks);
        if (canceledSamples.size () != callbackState.cancelAfter)
            return fail ("workspace sampling should stop after requested cancellation");
        if (callbackState.progressCalls < 2)
            return fail ("workspace sampling should emit initial and sample progress");
        if (callbackState.lastPlanned != 20)
            return fail ("workspace progress should report planned random sample count");
        if (callbackState.lastCompleted != callbackState.cancelAfter)
            return fail ("workspace progress should report the completed canceled count");
    }
```

If the existing variable names in `testWorkspaceSampling()` differ, use the local names already present there, but keep the callback behavior exactly the same.

- [ ] **Step 7: Run the workspace suite**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe workspace'
```

Expected output includes:

```text
KinematicAnalysis workspace test passed.
```

- [ ] **Step 8: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: add cancellable workspace sampling callbacks"
```

---

### Task 4: Move Workspace Sampling Off The UI Thread

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add workspace async slots**

In `KinematicAnalysisWidget.hpp`, add these slots near `sampleWorkspace()`:

```cpp
    void cancelWorkspaceSampling ();
    void updateWorkspaceProgress (qulonglong completedSamples,
                                  qulonglong plannedSamples);
    void handleWorkspaceFinished ();
```

- [ ] **Step 2: Add workspace async members**

In `KinematicAnalysisWidget.hpp`, add these members near the existing workspace UI members:

```cpp
    QPushButton* _workspaceCancelButton;
    QFutureWatcher< std::vector< WorkspaceSample > >* _workspaceWatcher;
    bool _workspaceRunActive;
    std::shared_ptr< std::atomic_bool > _workspaceCancelRequested;
    QProgressBar* _workspaceProgressBar;
    QLabel* _workspaceProgressLabel;
```

- [ ] **Step 3: Initialize and connect members in the constructor**

In `KinematicAnalysisWidget.cpp`, in the constructor initializer list, initialize:

```cpp
    _workspaceCancelButton (NULL),
    _workspaceWatcher (new QFutureWatcher< std::vector< WorkspaceSample > > (this)),
    _workspaceRunActive (false),
    _workspaceCancelRequested (std::make_shared< std::atomic_bool > (false)),
    _workspaceProgressBar (NULL),
    _workspaceProgressLabel (NULL),
```

After the existing signal connections for `_poseReachabilityWatcher`, add:

```cpp
    connect (_workspaceWatcher,
             SIGNAL(finished()),
             this,
             SLOT(handleWorkspaceFinished()));
```

- [ ] **Step 4: Add Cancel/progress controls in `buildWorkspaceTab()`**

In `buildWorkspaceTab()`, create the controls near `_workspaceRunButton`:

```cpp
    _workspaceCancelButton = new QPushButton (tr("Cancel"), controls);
    _workspaceCancelButton->setEnabled (false);
    connect (_workspaceCancelButton, SIGNAL(clicked()),
             this, SLOT(cancelWorkspaceSampling()));

    _workspaceProgressBar = new QProgressBar (controls);
    _workspaceProgressBar->setRange (0, 1);
    _workspaceProgressBar->setValue (0);
    _workspaceProgressBar->setTextVisible (false);

    _workspaceProgressLabel = new QLabel (tr("Progress: 0 / 0 sample(s)"), controls);
```

Add `_workspaceCancelButton` next to `_workspaceRunButton`, and place `_workspaceProgressBar` plus `_workspaceProgressLabel` in the same workspace control layout band. Do not put them inside a nested card.

- [ ] **Step 5: Add cancel slot**

Add this method to `KinematicAnalysisWidget.cpp`:

```cpp
void KinematicAnalysisWidget::cancelWorkspaceSampling ()
{
    if (!_workspaceRunActive || !_workspaceCancelRequested)
        return;
    _workspaceCancelRequested->store (true);
    if (_workspaceCancelButton != NULL)
        _workspaceCancelButton->setEnabled (false);
    setStatus (tr("Workspace sampling cancel requested..."));
}
```

- [ ] **Step 6: Add progress slot**

Add this constant beside `MaxPoseReachabilityProgressBarSteps`:

```cpp
static const int MaxWorkspaceProgressBarSteps = 1000000;
```

Add this method:

```cpp
void KinematicAnalysisWidget::updateWorkspaceProgress (
    qulonglong completedSamples, qulonglong plannedSamples)
{
    const qulonglong boundedCompleted = plannedSamples == 0 ? 0 :
        std::min< qulonglong > (completedSamples, plannedSamples);
    const int barMax = plannedSamples >
            static_cast< qulonglong > (MaxWorkspaceProgressBarSteps) ?
        MaxWorkspaceProgressBarSteps :
        static_cast< int > (plannedSamples);
    const int barValue = plannedSamples == 0 ? 0 :
        static_cast< int > (
            (static_cast< double > (boundedCompleted) /
             static_cast< double > (plannedSamples)) *
            static_cast< double > (barMax));

    if (_workspaceProgressBar != NULL) {
        _workspaceProgressBar->setRange (0, barMax);
        _workspaceProgressBar->setValue (barValue);
    }
    if (_workspaceProgressLabel != NULL) {
        const double pct = plannedSamples == 0 ? 0.0 :
            100.0 * static_cast< double > (boundedCompleted) /
                static_cast< double > (plannedSamples);
        _workspaceProgressLabel->setText (
            tr("Progress: %1 / %2 sample(s) (%3%)")
                .arg (static_cast< qulonglong > (boundedCompleted))
                .arg (static_cast< qulonglong > (plannedSamples))
                .arg (QString::number (pct, 'f', 1)));
    }
}
```

- [ ] **Step 7: Replace synchronous `sampleWorkspace()` with async worker**

Include helpers at the top of `KinematicAnalysisWidget.cpp`:

```cpp
#include "KinematicAnalysisCollision.hpp"
```

Replace the body of `KinematicAnalysisWidget::sampleWorkspace()` with this structure:

```cpp
void KinematicAnalysisWidget::sampleWorkspace ()
{
    if (_workspaceRunActive) {
        setStatus (tr("Workspace sampling is already running."));
        return;
    }

    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == NULL) {
        setStatus (tr("Cannot sample workspace: no valid device selected."));
        return;
    }
    rw::core::Ptr< rw::kinematics::Frame > tcpFrame = selectedTcpFrame ();
    if (tcpFrame == NULL) {
        setStatus (tr("Cannot sample workspace: no valid TCP frame selected."));
        return;
    }

    WorkspaceSamplingConfig config;
    config.sampleCount = _workspaceSampleCountSpin->value ();
    config.gridStepsPerJoint = _workspaceGridStepsSpin->value ();
    config.mode = _workspaceModeCombo->currentIndex () == 1 ?
        WorkspaceSamplingMode::Grid : WorkspaceSamplingMode::RandomUniform;
    config.checkCollision = _workspaceCollisionCheck->isChecked ();
    config.randomSeed = _workspaceSeedSpin != NULL ?
        static_cast< unsigned int > (_workspaceSeedSpin->value ()) : 1u;

    const rw::kinematics::State runState = currentState ();
    const rw::core::Ptr< rw::models::Device > runDevice = device;
    const rw::core::Ptr< const rw::kinematics::Frame > runTcpFrame = tcpFrame;
    const KinematicThresholds runThresholds = _thresholds;
    const rw::core::Ptr< rw::models::WorkCell > runWorkCell =
        _studio != NULL ? _studio->getWorkCell () : NULL;

    const std::size_t plannedSamples =
        rws::plannedWorkspaceSampleCount (
            config, runDevice->getDOF (), NULL);
    updateWorkspaceProgress (0, static_cast< qulonglong > (plannedSamples));

    _workspaceRunActive = true;
    if (_workspaceCancelRequested)
        _workspaceCancelRequested->store (false);
    if (_workspaceRunButton != NULL)
        _workspaceRunButton->setEnabled (false);
    if (_workspaceCancelButton != NULL)
        _workspaceCancelButton->setEnabled (true);
    QApplication::setOverrideCursor (Qt::WaitCursor);
    setStatus (tr("Workspace sampling running..."));

    struct WorkspaceRunContext {
        std::shared_ptr< std::atomic_bool > cancelFlag;
        QPointer< KinematicAnalysisWidget > widget;
    };
    const std::shared_ptr< WorkspaceRunContext > runContext =
        std::make_shared< WorkspaceRunContext > ();
    runContext->cancelFlag = _workspaceCancelRequested;
    runContext->widget = this;

    WorkspaceSamplingRunCallbacks callbacks;
    callbacks.isCancellationRequested = [] (void* userData) -> bool {
        const WorkspaceRunContext* context =
            static_cast< const WorkspaceRunContext* > (userData);
        return context != NULL && context->cancelFlag &&
               context->cancelFlag->load ();
    };
    callbacks.onProgress = [] (std::size_t completedSamples,
                               std::size_t plannedSamples,
                               void* userData) {
        WorkspaceRunContext* context =
            static_cast< WorkspaceRunContext* > (userData);
        if (context == NULL || context->widget.isNull ())
            return;
        QMetaObject::invokeMethod (
            context->widget.data (),
            "updateWorkspaceProgress",
            Qt::QueuedConnection,
            Q_ARG (qulonglong, static_cast< qulonglong > (completedSamples)),
            Q_ARG (qulonglong, static_cast< qulonglong > (plannedSamples)));
    };
    callbacks.userData = runContext.get ();

    QFuture< std::vector< WorkspaceSample > > future = QtConcurrent::run (
        [runDevice, runTcpFrame, runState, config, runThresholds,
         callbacks, runContext, runWorkCell] () {
            KinematicAnalyzer worker;
            worker.setThresholds (runThresholds);
            const rw::core::Ptr< rw::proximity::CollisionDetector > detector =
                config.checkCollision ?
                    makeKinematicAnalysisCollisionDetector (runWorkCell) : NULL;
            WorkspaceSamplingConfig workerConfig = config;
            if (config.checkCollision && detector == NULL)
                workerConfig.checkCollision = false;
            return worker.sampleWorkspace (
                runDevice, runTcpFrame, runState, workerConfig, detector, callbacks);
        });
    _workspaceWatcher->setFuture (future);
}
```

Remove the old `SamplingGuard` block from `sampleWorkspace()`.

- [ ] **Step 8: Add finish handler**

Add this method:

```cpp
void KinematicAnalysisWidget::handleWorkspaceFinished ()
{
    QApplication::restoreOverrideCursor ();
    _workspaceRunActive = false;
    if (_workspaceRunButton != NULL)
        _workspaceRunButton->setEnabled (true);
    if (_workspaceCancelButton != NULL)
        _workspaceCancelButton->setEnabled (false);

    const std::vector< WorkspaceSample > samples = _workspaceWatcher->result ();
    const bool wasCanceled =
        _workspaceCancelRequested &&
        _workspaceCancelRequested->load ();
    _workspaceSamples = samples;
    applyWorkspaceResults (_workspaceSamples);
    updateWorkspaceProgress (
        static_cast< qulonglong > (_workspaceSamples.size ()),
        static_cast< qulonglong > (_workspaceSamples.size ()));
    updateReportSummary ();

    if (wasCanceled) {
        setStatus (tr("Workspace sampling canceled after %1 sample(s).")
                       .arg (static_cast< int > (_workspaceSamples.size ())));
    }
    else {
        setStatus (tr("Workspace sampling completed with %1 sample(s).")
                       .arg (static_cast< int > (_workspaceSamples.size ())));
    }
}
```

If the UX should preserve the original planned denominator after completion, store `plannedSamples` as a `qulonglong _workspacePlannedSamples` member and call `updateWorkspaceProgress(samples.size(), _workspacePlannedSamples)` instead. For this pass, the required behavior is that the progress bar reaches a stable final state and the label does not exceed 100%.

- [ ] **Step 9: Ensure control update respects active runs**

At the end of `updateWorkspaceControls()`, add:

```cpp
    if (_workspaceRunButton != NULL)
        _workspaceRunButton->setEnabled (!_workspaceRunActive);
    if (_workspaceCancelButton != NULL)
        _workspaceCancelButton->setEnabled (_workspaceRunActive);
```

- [ ] **Step 10: Build plugin and test executable**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Expected: both targets build.

- [ ] **Step 11: Manual UI smoke**

Open RobWorkStudio with the plugin and verify:

1. Workspace RandomUniform with 50,000 samples updates the progress label while the window remains responsive.
2. Clicking Cancel stops the run and leaves a partial table instead of freezing the UI.
3. Workspace Grid mode with a capped count shows progress and can be canceled.
4. Export buttons are enabled after partial and completed runs.
5. Running workspace sampling twice in a row does not leave the cursor stuck as busy.

- [ ] **Step 12: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: run workspace sampling asynchronously"
```

---

### Task 5: Stop Reusing UI Collision Detector In Background Pose Reachability

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Remove pre-worker UI detector acquisition from pose reachability**

In `KinematicAnalysisWidget::analyzePoseReachability()`, remove this block:

```cpp
    bool collisionUnavailable = false;
    const rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector =
        collisionDetectorForAnalysis (config.checkCollision, &collisionUnavailable);
```

Add this capture beside `runThresholds`:

```cpp
    const rw::core::Ptr< rw::models::WorkCell > runWorkCell =
        _studio != NULL ? _studio->getWorkCell () : NULL;
```

- [ ] **Step 2: Construct a detector inside the worker lambda**

Change the `QtConcurrent::run` capture list from:

```cpp
        [runDevice, runTcpFrame, runState, positions, config,
         collisionDetector, runThresholds, callbacks, runContext] () {
```

to:

```cpp
        [runDevice, runTcpFrame, runState, positions, config,
         runThresholds, callbacks, runContext, runWorkCell] () {
```

Inside the lambda, before calling `worker.analyzePoseReachability()`, add:

```cpp
            const rw::core::Ptr< rw::proximity::CollisionDetector > detector =
                config.checkCollision ?
                    makeKinematicAnalysisCollisionDetector (runWorkCell) : NULL;
            PoseReachabilityConfig workerConfig = config;
            if (config.checkCollision && detector == NULL)
                workerConfig.checkCollision = false;
```

Change the call from:

```cpp
            return worker.analyzePoseReachability (
                runDevice, runTcpFrame, runState, positions, config,
                collisionDetector, callbacks);
```

to:

```cpp
            return worker.analyzePoseReachability (
                runDevice, runTcpFrame, runState, positions, workerConfig,
                detector, callbacks);
```

- [ ] **Step 3: Preserve collision-unavailable status message**

After completion, the UI currently cannot tell whether the worker disabled collision because no detector was available. Add a member:

```cpp
    bool _poseReachabilityCollisionUnavailable;
```

Initialize it to `false` in the constructor. Before starting the run, set:

```cpp
    _poseReachabilityCollisionUnavailable =
        config.checkCollision && runWorkCell == NULL;
```

In `handlePoseReachabilityFinished()`, append the note:

```cpp
    const QString collisionNote = _poseReachabilityCollisionUnavailable ?
        tr(" Collision checking was unavailable.") : QString ();
```

Use it in both status messages:

```cpp
        setStatus (tr("Pose reachability canceled after %1 position(s).%2")
                       .arg (static_cast< int > (_poseReachabilitySamples.size ()))
                       .arg (collisionNote));
```

and:

```cpp
        setStatus (tr("Pose reachability completed for %1 position(s).%2")
                       .arg (static_cast< int > (_poseReachabilitySamples.size ()))
                       .arg (collisionNote));
```

This is conservative: it reports definite unavailability when no WorkCell exists. If detector construction can fail by exception in this RobWork version, catch `std::exception` inside the worker and return samples with collision disabled; do not throw across the QtConcurrent boundary.

- [ ] **Step 4: Build and run pose reachability suite**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe pose_reachability'
```

Expected: plugin builds and pose reachability tests pass.

- [ ] **Step 5: Manual UI smoke**

Run Pose Reachability with collision checking enabled:

1. The UI remains responsive.
2. Cancel still works.
3. Results are produced.
4. No crash occurs when closing the plugin immediately after starting a run.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "fix: isolate background collision detectors"
```

---

### Task 6: Fix Visualization Legend Layout

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`

- [ ] **Step 1: Update legend method signature**

In `KinematicAnalysisPlotWidget.hpp`, change:

```cpp
    void paintLegend (QPainter& painter, const QRectF& plotArea) const;
```

to:

```cpp
    bool shouldPaintLegend (const QRect& area) const;
    int legendWidth (const QRect& area) const;
    void paintLegend (QPainter& painter, const QRectF& legendArea) const;
```

- [ ] **Step 2: Implement legend layout helpers**

In `KinematicAnalysisPlotWidget.cpp`, add these methods before `paintPlot()`:

```cpp
bool KinematicAnalysisPlotWidget::shouldPaintLegend (const QRect& area) const
{
    return _showLegend && area.width () >= 480;
}

int KinematicAnalysisPlotWidget::legendWidth (const QRect& area) const
{
    return shouldPaintLegend (area) ? 128 : 0;
}
```

- [ ] **Step 3: Reserve legend width in `paintPlot()`**

Replace:

```cpp
    const QRectF pr = area.adjusted (area.width () * 0.06, 18, -18, -area.height () * 0.08);
```

with:

```cpp
    const int reservedLegendWidth = legendWidth (area);
    const QRectF pr = area.adjusted (
        area.width () * 0.06,
        18,
        -18 - reservedLegendWidth,
        -area.height () * 0.08);
```

Replace:

```cpp
    paintLegend (painter, pr);
```

with:

```cpp
    if (shouldPaintLegend (area)) {
        const QRectF legendArea (
            pr.right () + 8,
            pr.top () + 4,
            reservedLegendWidth - 12,
            pr.height () - 8);
        paintLegend (painter, legendArea);
    }
```

- [ ] **Step 4: Make `paintLegend()` use the passed legend rectangle**

Change the implementation header:

```cpp
void KinematicAnalysisPlotWidget::paintLegend (
    QPainter& painter, const QRectF& legendArea) const
```

Remove:

```cpp
    if (!_showLegend || width () < 480)
        return;
```

Replace:

```cpp
    const double legendLeft = plotArea.right () + 6;
    const double legendTop = plotArea.top () + 4;
```

with:

```cpp
    const double legendLeft = legendArea.left ();
    const double legendTop = legendArea.top ();
```

For scalar gradients, ensure any rectangle that previously assumed unrestricted width uses `legendArea.width ()`. The color bar should be no wider than `legendArea.width ()` and labels should start at `legendLeft`.

- [ ] **Step 5: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: plugin builds.

- [ ] **Step 6: Manual visualization smoke**

In RobWorkStudio:

1. Generate workspace or pose reachability samples.
2. Open Visualization.
3. Test widths below and above 480 px.
4. Switch color modes: Status, Manipulability, MinJointMargin, ConditionNumber, Collision.
5. Export PNG and inspect it.

Expected: no legend text or color bar clips outside the image; the plot area remains visible and axis labels do not overlap the legend.

- [ ] **Step 7: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp
git commit -m "fix: reserve visualization legend space"
```

---

### Task 7: Use A Consistent JSON Non-Finite Number Policy

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Include the JSON helper**

Add near the other local includes in `KinematicAnalysisWidget.cpp`:

```cpp
#include "KinematicAnalysisJson.hpp"
```

- [ ] **Step 2: Update local JSON array helpers**

Change `vectorToJsonArray()` from:

```cpp
    for (double value : values)
        array.append (value);
```

to:

```cpp
    for (double value : values)
        array.append (jsonValueFromDouble (value));
```

Change `array3ToJsonArray()` from:

```cpp
    array.append (values[0]);
    array.append (values[1]);
    array.append (values[2]);
```

to:

```cpp
    array.append (jsonValueFromDouble (values[0]));
    array.append (jsonValueFromDouble (values[1]));
    array.append (jsonValueFromDouble (values[2]));
```

- [ ] **Step 3: Update scalar JSON writes in `exportReportJson()`**

Replace raw double assignments for fields that can come from numerical analysis with `jsonValueFromDouble(...)`.

Required replacements:

```cpp
    root["reachableRate"] = jsonValueFromDouble (result.reachableRate);
```

```cpp
    current["conditionNumber"] = jsonValueFromDouble (result.currentPose.conditionNumber);
    current["manipulability"] = jsonValueFromDouble (result.currentPose.manipulability);
```

```cpp
            bestObj["positionErrorMeters"] =
                jsonValueFromDouble (best->positionErrorMeters);
            bestObj["orientationErrorDeg"] =
                jsonValueFromDouble (best->orientationErrorDeg);
            bestObj["minJointLimitMargin"] =
                jsonValueFromDouble (best->minJointLimitMargin);
            bestObj["conditionNumber"] =
                jsonValueFromDouble (best->conditionNumber);
            bestObj["manipulability"] =
                jsonValueFromDouble (best->manipulability);
```

```cpp
        item["manipulability"] = jsonValueFromDouble (sample.manipulability);
        item["minJointLimitMargin"] =
            jsonValueFromDouble (sample.minJointLimitMargin);
        item["conditionNumber"] = jsonValueFromDouble (sample.conditionNumber);
```

```cpp
        item["coverage"] = jsonValueFromDouble (sample.coverage);
```

Do not convert counters such as `sampledDirections`, `reachableDirections`, `completedIkTargets`, and `plannedIkTargets` to strings; keep them numeric.

- [ ] **Step 4: Run full test executable**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected output includes:

```text
100% tests passed
```

- [ ] **Step 5: Manual JSON smoke**

Export a report JSON from a configuration that includes a singular pose or singular workspace sample.

Expected:

```json
"conditionNumber": "inf"
```

or:

```json
"conditionNumber": "-inf"
```

or:

```json
"conditionNumber": "nan"
```

Finite values remain JSON numbers:

```json
"manipulability": 0.123
```

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "fix: normalize kinematic report json numbers"
```

---

### Task 8: Update Documentation And Run Final Verification

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Update README behavior notes**

In `README.md`, add or update concise notes for these behaviors:

```markdown
### Workspace sampling

Workspace sampling runs on a background thread. Random and grid sampling report completed samples against the planned sanitized sample count. Cancel stops cooperatively between samples and keeps the samples completed so far available for table display, visualization, CSV export, and report export.

### Background collision checking

Long-running background analyses create a fresh collision detector from the current WorkCell instead of reusing the RobWorkStudio UI collision detector. If collision checking is requested but no WorkCell is available for the worker, the run continues without collision checking and reports that collision checking was unavailable.

### JSON non-finite values

Report JSON keeps finite floating-point values as JSON numbers. Non-finite values are exported as strings: `"inf"`, `"-inf"`, and `"nan"`.

### Visualization legend

The visualization plot reserves right-side space for legends when the render area is wide enough. Narrow views hide the legend to keep the plot and axis labels readable.
```

- [ ] **Step 2: Run Debug test target through CTest**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected output includes:

```text
100% tests passed
```

- [ ] **Step 3: Build Debug plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: build succeeds.

- [ ] **Step 4: Build Release plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_kinematicanalysis --config Release'
```

Expected: build succeeds.

- [ ] **Step 5: Run manual end-to-end smoke**

Use RobWorkStudio and the plugin UI:

1. Load a WorkCell with a valid device and TCP.
2. Refresh Current Pose.
3. Solve IK for a reachable target and apply the best solution.
4. Import current pose into Task Points, analyze selected and all task points, export CSV.
5. Run Workspace RandomUniform with collision disabled; verify progress, visualization, report summary, CSV export.
6. Run Workspace Grid with collision enabled; click Cancel; verify partial results remain available.
7. Run Pose Reachability with collision enabled; verify progress and cancel still work.
8. Open Visualization from Workspace and Pose Reachability; switch scalar modes; export PNG.
9. Export JSON and verify it parses with a standard JSON parser.
10. Close the plugin shortly after starting a background run; verify no crash.

- [ ] **Step 6: Inspect git diff for scope**

Run:

```powershell
git diff -- RobWorkStudio/src/rwslibs/kinematicanalysis docs/superpowers/plans/2026-07-19-kinematicanalysis-followup-optimization.md
```

Expected:

1. Changes are limited to the files listed in this plan.
2. No unrelated formatting churn outside edited blocks.
3. No removal of existing user-facing features.
4. No accidental source-encoding rewrite of files with existing non-ASCII comments.

- [ ] **Step 7: Commit final docs/verification updates**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "docs: describe kinematic analysis background workflows"
```

---

## Deferred Refactor: Split `KinematicAnalysisWidget.cpp`

Do not include this in the required implementation unless all previous tasks are passing and the owner explicitly approves a refactor-only pass.

Recommended future decomposition:

- `KinematicAnalysisWorkspaceController.hpp/.cpp`
  - Own workspace config collection, async run state, progress, cancel, and result application.

- `KinematicAnalysisPoseReachabilityController.hpp/.cpp`
  - Own pose reachability config collection, async run state, progress, cancel, and result application.

- `KinematicAnalysisReportExporter.hpp/.cpp`
  - Own JSON/CSV report serialization and reuse `KinematicAnalysisJson`.

- `KinematicAnalysisWidget.cpp`
  - Keep tab construction, RobWorkStudio integration, signal wiring, and high-level UI state.

Refactor acceptance criteria:

1. No behavior changes.
2. All tests and manual smoke from Task 8 still pass.
3. Each new controller has a narrow public interface and does not own RobWorkStudio global state.

---

## Reviewer Checklist For The Follow-Up Audit

When the modified code comes back for review, check these items first:

1. `sampleWorkspace()` no longer blocks the UI thread and uses `QtConcurrent::run`.
2. Workspace and Pose Reachability background workers do not capture `_studio->getCollisionDetector()` or any widget-owned collision detector.
3. Background lambdas capture value-like inputs: device pointer, TCP frame pointer, state copy, config copy, thresholds copy, WorkCell pointer.
4. Cancel flags are `std::shared_ptr<std::atomic_bool>` and are read only through callbacks in core loops.
5. Progress callbacks use `QMetaObject::invokeMethod(..., Qt::QueuedConnection, ...)`.
6. `QApplication::restoreOverrideCursor()` is paired exactly once per started async run.
7. JSON export uses `jsonValueFromDouble()` for all analysis doubles that can be non-finite.
8. Legend drawing is based on the passed render `area`, not `width()`, so PNG export and widget paint share the same layout.
9. CMake does not link `sdurws_kinematicanalysis_test` before `add_executable`.
10. `ctest -R sdurws_kinematicanalysis_test` passes in Debug.
11. Debug and Release plugin targets build.
12. Manual smoke includes canceling long workspace and pose reachability runs.
