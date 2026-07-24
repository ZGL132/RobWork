# KinematicAnalysis Pose Reachability Parallelism Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a safe, opt-in parallel execution path for Pose reachability that speeds up collision-free multi-position jobs while preserving serial behavior, result ordering, cancellation, progress, and report/export compatibility.

**Architecture:** Parallelize only at the position level and only when collision checking is disabled. Keep each worker on its own copied `State` and local `KinematicAnalyzer`; preserve result ordering by writing each completed `PoseReachabilitySample` back to its original position index, and serialize public progress callbacks so existing callback user data remains safe.

**Tech Stack:** C++11 standard threading primitives (`std::thread`, `std::atomic`, `std::mutex`), RobWork kinematics/IK APIs, Qt Widgets for opt-in controls, existing `sdurws_kinematicanalysis_test` CTest executable.

---

## Safety Boundary

This plan deliberately implements a conservative parallel subset:

- Parallel mode is allowed only when `PoseReachabilityConfig::checkCollision == false`.
- Collision-enabled Pose reachability remains serial even when the user asks for parallel execution.
- Work is split by position, not by individual IK target, so each result row has one owner.
- Every worker uses a copied `rw::kinematics::State`.
- Every worker owns a local `KinematicAnalyzer` with copied thresholds.
- Results are stored by original index to keep CSV/report/visualization order deterministic.
- `PoseReachabilityRunCallbacks::onProgress` is invoked under a mutex in parallel mode.

Do not parallelize shared `rw::proximity::CollisionDetector` use in this plan.

## File Structure

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
  - Add opt-in parallel configuration fields to `PoseReachabilityConfig`.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp`
  - Add helper declarations for sanitized worker count and parallel eligibility.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
  - Implement worker-count sanitizing and parallel eligibility helpers.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
  - Extract one-position Pose reachability execution and add a position-level parallel scheduler.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Add UI fields for enabling parallel execution and choosing worker count.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Add Parallel controls, pass config values, and surface serial fallback when Collision is enabled.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add helper tests, serial/parallel equivalence tests, progress tests, and cancellation tests.
- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Document the opt-in parallel mode and collision fallback.
- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilityParallelAudit.md`
  - Record the thread-safety decision and known limits.

---

### Task 1: Add Parallel Config and Pure Helper Tests

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Extend `PoseReachabilityConfig`**

In `KinematicAnalysisTypes.hpp`, extend `struct PoseReachabilityConfig` after `checkCollision`:

```cpp
bool enableParallel = false;       // opt-in; collision-enabled runs remain serial
int maxParallelThreads = 0;        // 0 means use hardware_concurrency()
```

Expected struct:

```cpp
struct PoseReachabilityConfig
{
    int directionSamples = 24;
    int rollSamples      = 1;
    bool checkCollision  = true;
    bool enableParallel = false;
    int maxParallelThreads = 0;
};
```

- [ ] **Step 2: Add helper declarations**

In `KinematicAnalysisPoseReachability.hpp`, after `poseReachabilityExecutionTargetCount(...)`, add:

```cpp
static const int MaxPoseReachabilityParallelThreads = 64;

int sanitizedPoseReachabilityThreadCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    unsigned int hardwareThreads);

bool shouldUseParallelPoseReachability (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    unsigned int hardwareThreads);
```

- [ ] **Step 3: Implement helper functions**

In `KinematicAnalysisPoseReachability.cpp`, after `poseReachabilityExecutionTargetCount(...)`, add:

```cpp
int rws::sanitizedPoseReachabilityThreadCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    unsigned int hardwareThreads)
{
    if (!config.enableParallel)
        return 1;
    if (positionCount < 2)
        return 1;
    if (config.checkCollision)
        return 1;

    int requested = config.maxParallelThreads;
    if (requested <= 0)
        requested = hardwareThreads == 0 ? 1 : static_cast< int > (hardwareThreads);
    requested = std::max (1, requested);
    requested = std::min (requested, MaxPoseReachabilityParallelThreads);
    requested = std::min (
        requested,
        static_cast< int > (std::min< std::size_t > (
            positionCount,
            static_cast< std::size_t > (MaxPoseReachabilityParallelThreads))));
    return requested;
}

bool rws::shouldUseParallelPoseReachability (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    unsigned int hardwareThreads)
{
    return sanitizedPoseReachabilityThreadCount (
        config, positionCount, hardwareThreads) > 1;
}
```

- [ ] **Step 4: Add helper tests**

In `testPoseReachabilityHelpers()` in `KinematicAnalysisTest.cpp`, after the execution target count block, add:

```cpp
{
    rws::PoseReachabilityConfig parallelConfig;
    parallelConfig.enableParallel = true;
    parallelConfig.checkCollision = false;
    parallelConfig.maxParallelThreads = 4;

    if (const int rc = require (
            rws::sanitizedPoseReachabilityThreadCount (
                parallelConfig, 8, 16) == 4,
            "pose parallel uses requested worker count"))
        return rc;
    if (const int rc = require (
            rws::shouldUseParallelPoseReachability (
                parallelConfig, 8, 16),
            "pose parallel eligibility true without collision"))
        return rc;

    parallelConfig.checkCollision = true;
    if (const int rc = require (
            rws::sanitizedPoseReachabilityThreadCount (
                parallelConfig, 8, 16) == 1,
            "pose parallel disabled with collision"))
        return rc;
    if (const int rc = require (
            !rws::shouldUseParallelPoseReachability (
                parallelConfig, 8, 16),
            "pose parallel eligibility false with collision"))
        return rc;

    parallelConfig.checkCollision = false;
    parallelConfig.maxParallelThreads = 1000;
    if (const int rc = require (
            rws::sanitizedPoseReachabilityThreadCount (
                parallelConfig, 100, 16) ==
                    rws::MaxPoseReachabilityParallelThreads,
            "pose parallel thread count hard capped"))
        return rc;

    parallelConfig.maxParallelThreads = 0;
    if (const int rc = require (
            rws::sanitizedPoseReachabilityThreadCount (
                parallelConfig, 3, 32) == 3,
            "pose parallel thread count capped by position count"))
        return rc;
}
```

- [ ] **Step 5: Run helper test**

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
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: add pose reachability parallel config"
```

---

### Task 2: Extract One-Position Execution Without Behavior Change

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add serial refactor includes**

In `KinematicAnalyzer.cpp`, add:

```cpp
#include <atomic>
#include <mutex>
```

These headers are needed before the serial refactor builds because `PoseReachabilityProgressContext` stores `std::atomic` and `std::mutex` pointers.

- [ ] **Step 2: Add local execution context**

In the anonymous namespace in `KinematicAnalyzer.cpp`, after `poseReachabilityTarget(...)`, add:

```cpp
struct PoseReachabilityExecutionContext
{
    rw::core::Ptr< rw::models::Device > device;
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame;
    rw::kinematics::State state;
    PoseReachabilityConfig config;
    KinematicThresholds thresholds;
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector;
    std::vector< rw::math::Vector3D<> > directions;
    int directionCount = 0;
    int rollCount = 0;
    int totalDirections = 0;
    std::size_t ikPerPosition = 0;
};

struct PoseReachabilityProgressContext
{
    const PoseReachabilityRunCallbacks* callbacks = nullptr;
    std::atomic< std::size_t >* completedTargets = nullptr;
    std::size_t plannedTargets = 0;
    std::mutex* callbackMutex = nullptr;
};
```

- [ ] **Step 3: Add cancellation helper**

After `PoseReachabilityProgressContext`, add:

```cpp
bool poseReachabilityCancellationRequested (
    const PoseReachabilityRunCallbacks& callbacks,
    std::mutex* callbackMutex)
{
    if (callbacks.isCancellationRequested == NULL)
        return false;
    if (callbackMutex != nullptr) {
        std::lock_guard< std::mutex > lock (*callbackMutex);
        return callbacks.isCancellationRequested (callbacks.userData);
    }
    return callbacks.isCancellationRequested (callbacks.userData);
}
```

- [ ] **Step 4: Add progress helper**

After `poseReachabilityCancellationRequested(...)`, add:

```cpp
void reportPoseReachabilityProgress (
    PoseReachabilityProgressContext* progress)
{
    if (progress == nullptr || progress->callbacks == nullptr ||
        progress->completedTargets == nullptr)
        return;
    if (progress->callbacks->onProgress == NULL)
        return;

    const std::size_t completed =
        progress->completedTargets->fetch_add (1) + 1;
    if (progress->callbackMutex != nullptr) {
        std::lock_guard< std::mutex > lock (*progress->callbackMutex);
        progress->callbacks->onProgress (
            completed,
            progress->plannedTargets,
            progress->callbacks->userData);
        return;
    }
    progress->callbacks->onProgress (
        completed,
        progress->plannedTargets,
        progress->callbacks->userData);
}
```

- [ ] **Step 5: Add one-position worker function**

After `reportPoseReachabilityProgress(...)`, add:

```cpp
PoseReachabilitySample analyzePoseReachabilityPosition (
    const PoseReachabilityExecutionContext& context,
    const std::array< double, 3 >& position,
    PoseReachabilityProgressContext* progress)
{
    PoseReachabilitySample sample;
    sample.position = position;
    sample.sampledDirections = context.totalDirections;
    sample.plannedIkTargets = context.ikPerPosition;
    std::size_t completedTargetsForSample = 0;

    const auto finishCanceledSample =
        [&sample, &completedTargetsForSample, &context] () {
        sample.completedIkTargets = completedTargetsForSample;
        sample.plannedIkTargets = context.ikPerPosition;
        sample.partial = completedTargetsForSample < context.ikPerPosition;
        sample.status = sample.reachableDirections == 0 ?
            AnalysisStatus::Fail : AnalysisStatus::Warning;
        sample.coverage = context.totalDirections == 0 ? 0.0 :
            static_cast< double > (sample.reachableDirections) /
                static_cast< double > (context.totalDirections);
    };

    if (context.device == NULL || context.tcpFrame == NULL ||
        context.totalDirections == 0) {
        sample.completedIkTargets = 0;
        sample.partial = false;
        sample.status = AnalysisStatus::Fail;
        return sample;
    }

    KinematicAnalyzer worker;
    worker.setThresholds (context.thresholds);

    if (progress != nullptr && progress->callbacks != nullptr &&
        poseReachabilityCancellationRequested (
            *progress->callbacks, progress->callbackMutex)) {
        finishCanceledSample ();
        return sample;
    }

    for (int directionIndex = 0;
         directionIndex < context.directionCount;
         ++directionIndex) {
        for (int rollIndex = 0; rollIndex < context.rollCount; ++rollIndex) {
            if (progress != nullptr && progress->callbacks != nullptr &&
                poseReachabilityCancellationRequested (
                    *progress->callbacks, progress->callbackMutex)) {
                finishCanceledSample ();
                return sample;
            }

            const rw::math::Rotation3D<> rotation =
                toolZDirectionToRotation (
                    context.directions[static_cast< std::size_t > (directionIndex)],
                    rollIndex,
                    context.rollCount);
            const TaskPoint target =
                poseReachabilityTarget (position, rotation, directionIndex, rollIndex);
            const KinematicIkAnalysisResult ik = worker.analyzeIk (
                context.device,
                context.tcpFrame,
                context.state,
                target,
                context.config.checkCollision ? context.collisionDetector : NULL);

            if (isPoseDirectionReachable (ik.solutions))
                ++sample.reachableDirections;
            ++completedTargetsForSample;
            sample.completedIkTargets = completedTargetsForSample;

            reportPoseReachabilityProgress (progress);

            if (progress != nullptr && progress->callbacks != nullptr &&
                poseReachabilityCancellationRequested (
                    *progress->callbacks, progress->callbackMutex)) {
                finishCanceledSample ();
                return sample;
            }
        }
    }

    sample.completedIkTargets = completedTargetsForSample;
    sample.partial = false;
    sample.coverage = context.totalDirections == 0 ? 0.0 :
        static_cast< double > (sample.reachableDirections) /
            static_cast< double > (context.totalDirections);
    if (sample.reachableDirections == 0)
        sample.status = AnalysisStatus::Fail;
    else if (sample.reachableDirections == context.totalDirections)
        sample.status = AnalysisStatus::Pass;
    else
        sample.status = AnalysisStatus::Warning;
    return sample;
}
```

- [ ] **Step 6: Rewrite serial path to call one-position worker**

In `KinematicAnalyzer::analyzePoseReachability(...)`, keep the existing sanitize/count setup and replace the body from `std::size_t completedTargets = 0;` through the end of the position loop with:

```cpp
PoseReachabilityExecutionContext context;
context.device = device;
context.tcpFrame = resolvedTcpFrame;
context.state = state;
context.config = sanitized;
context.thresholds = _thresholds;
context.collisionDetector = collisionDetector;
context.directions = directions;
context.directionCount = directionCount;
context.rollCount = rollCount;
context.totalDirections = totalDirections;
context.ikPerPosition = ikPerPosition;

std::atomic< std::size_t > completedTargets (0);
PoseReachabilityProgressContext progress;
progress.callbacks = &callbacks;
progress.completedTargets = &completedTargets;
progress.plannedTargets = plannedTotal;
progress.callbackMutex = nullptr;

for (const std::array< double, 3 >& position : positions) {
    const PoseReachabilitySample sample =
        analyzePoseReachabilityPosition (context, position, &progress);
    results.push_back (sample);
    if (callbacks.isCancellationRequested != NULL &&
        callbacks.isCancellationRequested (callbacks.userData))
        return results;
}
return results;
```

- [ ] **Step 7: Run existing Pose tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe pose'
```

Expected:

```text
KinematicAnalysis pose test passed.
```

- [ ] **Step 8: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp
git commit -m "refactor: extract pose reachability position worker"
```

---

### Task 3: Add Safe Position-Level Parallel Scheduler

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add standard threading include**

In `KinematicAnalyzer.cpp`, add:

```cpp
#include <thread>
```

Keep the `<atomic>` and `<mutex>` includes added in Task 2.

- [ ] **Step 2: Add parallel scheduler helper**

After `analyzePoseReachabilityPosition(...)`, add:

```cpp
std::vector< PoseReachabilitySample > analyzePoseReachabilityPositionsParallel (
    const PoseReachabilityExecutionContext& context,
    const std::vector< std::array< double, 3 > >& positions,
    const PoseReachabilityRunCallbacks& callbacks,
    std::size_t plannedTotal,
    int workerCount)
{
    std::vector< PoseReachabilitySample > ordered (positions.size ());
    std::vector< char > produced (positions.size (), 0);
    std::atomic< std::size_t > nextIndex (0);
    std::atomic< std::size_t > completedTargets (0);
    std::mutex callbackMutex;

    PoseReachabilityProgressContext progress;
    progress.callbacks = &callbacks;
    progress.completedTargets = &completedTargets;
    progress.plannedTargets = plannedTotal;
    progress.callbackMutex = &callbackMutex;

    const auto workerBody = [&] () {
        PoseReachabilityExecutionContext localContext = context;
        localContext.state = context.state;

        while (true) {
            if (poseReachabilityCancellationRequested (
                    callbacks, &callbackMutex))
                return;
            const std::size_t index = nextIndex.fetch_add (1);
            if (index >= positions.size ())
                return;
            ordered[index] = analyzePoseReachabilityPosition (
                localContext,
                positions[index],
                &progress);
            produced[index] = 1;
            if (ordered[index].partial)
                return;
        }
    };

    std::vector< std::thread > workers;
    workers.reserve (static_cast< std::size_t > (workerCount));
    for (int i = 0; i < workerCount; ++i)
        workers.push_back (std::thread (workerBody));
    for (std::thread& worker : workers)
        worker.join ();

    std::vector< PoseReachabilitySample > compact;
    compact.reserve (positions.size ());
    for (std::size_t i = 0; i < ordered.size (); ++i) {
        if (produced[i])
            compact.push_back (ordered[i]);
    }
    return compact;
}
```

- [ ] **Step 3: Call scheduler from analyzer**

In `KinematicAnalyzer::analyzePoseReachability(...)`, after building `PoseReachabilityExecutionContext context`, add:

```cpp
const int workerCount = sanitizedPoseReachabilityThreadCount (
    sanitized,
    positions.size (),
    std::thread::hardware_concurrency ());
if (workerCount > 1) {
    return analyzePoseReachabilityPositionsParallel (
        context,
        positions,
        callbacks,
        plannedTotal,
        workerCount);
}
```

Keep the serial loop after this block as the fallback path.

- [ ] **Step 4: Add serial/parallel equivalence test**

In `testPoseReachability()` in `KinematicAnalysisTest.cpp`, after the multi-position progress test, add:

```cpp
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    rw::kinematics::State deviceState = stateStructure.getDefaultState ();

    std::vector< std::array< double, 3 > > batch;
    batch.push_back (std::array< double, 3 > {{1.0, 2.0, 3.0}});
    batch.push_back (std::array< double, 3 > {{1.1, 2.0, 3.0}});
    batch.push_back (std::array< double, 3 > {{1.2, 2.0, 3.0}});

    rws::PoseReachabilityConfig serialConfig;
    serialConfig.directionSamples = 2;
    serialConfig.rollSamples = 2;
    serialConfig.checkCollision = false;
    serialConfig.enableParallel = false;

    rws::PoseReachabilityConfig parallelConfig = serialConfig;
    parallelConfig.enableParallel = true;
    parallelConfig.maxParallelThreads = 2;

    const std::vector< rws::PoseReachabilitySample > serial =
        analyzer.analyzePoseReachability (
            device, device->getEnd (), deviceState, batch,
            serialConfig, NULL);
    const std::vector< rws::PoseReachabilitySample > parallel =
        analyzer.analyzePoseReachability (
            device, device->getEnd (), deviceState, batch,
            parallelConfig, NULL);

    if (const int rc = require (serial.size () == parallel.size (),
                                "parallel pose result size matches serial"))
        return rc;
    for (std::size_t i = 0; i < serial.size (); ++i) {
        if (const int rc = require (serial[i].status == parallel[i].status,
                                    "parallel pose status matches serial"))
            return rc;
        if (const int rc = require (
                serial[i].sampledDirections == parallel[i].sampledDirections,
                "parallel pose sampled count matches serial"))
            return rc;
        if (const int rc = require (
                serial[i].reachableDirections == parallel[i].reachableDirections,
                "parallel pose reachable count matches serial"))
            return rc;
        if (const int rc = assertNear (
                serial[i].coverage, parallel[i].coverage, 1e-12,
                "parallel pose coverage matches serial"))
            return rc;
        if (const int rc = assertNear (
                serial[i].position[0], parallel[i].position[0], 1e-12,
                "parallel pose order preserves x"))
            return rc;
    }
}
```

- [ ] **Step 5: Add parallel progress test**

In `testPoseReachability()` after the equivalence test, add:

```cpp
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    rw::kinematics::State deviceState = stateStructure.getDefaultState ();

    std::vector< std::array< double, 3 > > batch;
    batch.push_back (std::array< double, 3 > {{1.0, 2.0, 3.0}});
    batch.push_back (std::array< double, 3 > {{1.1, 2.0, 3.0}});

    rws::PoseReachabilityConfig parallelConfig;
    parallelConfig.directionSamples = 2;
    parallelConfig.rollSamples = 2;
    parallelConfig.checkCollision = false;
    parallelConfig.enableParallel = true;
    parallelConfig.maxParallelThreads = 2;

    struct ParallelProgressState {
        std::size_t calls = 0;
        std::size_t maxCompleted = 0;
        std::size_t planned = 0;
    } progressState;

    rws::PoseReachabilityRunCallbacks callbacks;
    callbacks.userData = &progressState;
    callbacks.onProgress = [] (std::size_t completed,
                               std::size_t planned,
                               void* userData) {
        ParallelProgressState* state =
            static_cast< ParallelProgressState* > (userData);
        ++state->calls;
        state->maxCompleted = std::max (state->maxCompleted, completed);
        state->planned = planned;
    };

    analyzer.analyzePoseReachability (
        device, device->getEnd (), deviceState, batch,
        parallelConfig, NULL, callbacks);

    if (const int rc = require (progressState.calls == 8,
                                "parallel pose progress callback count"))
        return rc;
    if (const int rc = require (progressState.maxCompleted == 8,
                                "parallel pose progress completed count"))
        return rc;
    if (const int rc = require (progressState.planned == 8,
                                "parallel pose progress planned count"))
        return rc;
}
```

- [ ] **Step 6: Add parallel cancellation test**

In `testPoseReachability()` after the parallel progress test, add:

```cpp
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    rw::kinematics::State deviceState = stateStructure.getDefaultState ();

    std::vector< std::array< double, 3 > > batch;
    batch.push_back (std::array< double, 3 > {{1.0, 2.0, 3.0}});
    batch.push_back (std::array< double, 3 > {{1.1, 2.0, 3.0}});
    batch.push_back (std::array< double, 3 > {{1.2, 2.0, 3.0}});
    batch.push_back (std::array< double, 3 > {{1.3, 2.0, 3.0}});

    rws::PoseReachabilityConfig parallelConfig;
    parallelConfig.directionSamples = 4;
    parallelConfig.rollSamples = 2;
    parallelConfig.checkCollision = false;
    parallelConfig.enableParallel = true;
    parallelConfig.maxParallelThreads = 2;

    struct CancelProgressState {
        std::size_t calls = 0;
        bool cancel = false;
    } cancelState;

    rws::PoseReachabilityRunCallbacks callbacks;
    callbacks.userData = &cancelState;
    callbacks.isCancellationRequested = [] (void* userData) -> bool {
        CancelProgressState* state =
            static_cast< CancelProgressState* > (userData);
        return state->cancel;
    };
    callbacks.onProgress = [] (std::size_t,
                               std::size_t,
                               void* userData) {
        CancelProgressState* state =
            static_cast< CancelProgressState* > (userData);
        ++state->calls;
        if (state->calls >= 2)
            state->cancel = true;
    };

    const std::vector< rws::PoseReachabilitySample > canceled =
        analyzer.analyzePoseReachability (
            device, device->getEnd (), deviceState, batch,
            parallelConfig, NULL, callbacks);

    if (const int rc = require (!canceled.empty (),
                                "parallel canceled pose returns started samples"))
        return rc;
    if (const int rc = require (canceled.size () <= batch.size (),
                                "parallel canceled pose does not invent rows"))
        return rc;
    bool sawPartial = false;
    for (const rws::PoseReachabilitySample& sample : canceled) {
        if (sample.partial)
            sawPartial = true;
    }
    if (const int rc = require (sawPartial,
                                "parallel canceled pose marks partial sample"))
        return rc;
}
```

- [ ] **Step 7: Run Pose tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe pose'
```

Expected:

```text
KinematicAnalysis pose test passed.
```

- [ ] **Step 8: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: parallelize collision-free pose reachability"
```

---

### Task 4: Add UI Controls for Opt-In Parallel Execution

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add widget fields**

In `KinematicAnalysisWidget.hpp`, add these fields after `_poseCollisionCheck`:

```cpp
QCheckBox* _poseParallelCheck;
QSpinBox* _poseParallelThreadsSpin;
```

In the constructor initializer list in `KinematicAnalysisWidget.cpp`, add these entries after `_poseCollisionCheck(NULL),`:

```cpp
_poseParallelCheck(NULL),
_poseParallelThreadsSpin(NULL),
```

- [ ] **Step 2: Create controls**

In `buildPoseReachabilityTab()`, after `_poseCollisionCheck` is created, add:

```cpp
_poseParallelCheck = new QCheckBox (tr("Parallel"), _poseReachTab);
_poseParallelCheck->setChecked (false);
_poseParallelThreadsSpin = new QSpinBox (_poseReachTab);
_poseParallelThreadsSpin->setRange (0, MaxPoseReachabilityParallelThreads);
_poseParallelThreadsSpin->setValue (0);
_poseParallelThreadsSpin->setToolTip (
    tr("0 uses hardware concurrency. Parallel mode is used only when Collision is off."));
```

Add them to the controls grid after `_poseCollisionCheck`:

```cpp
controls->addWidget (_poseParallelCheck, 1, 3);
controls->addWidget (new QLabel (tr("Threads:"), _poseReachTab), 2, 0);
controls->addWidget (_poseParallelThreadsSpin, 2, 1);
```

Move the existing Add row / Run / Export / Cancel row down by one row:

```cpp
controls->addWidget (_poseAddRowButton, 3, 0);
controls->addWidget (_poseAnalyzeButton, 3, 1);
controls->addWidget (_poseExportButton, 3, 2);
controls->addWidget (_poseCancelButton, 3, 3);
```

- [ ] **Step 3: Wire control refresh**

In `buildPoseReachabilityTab()`, after the existing spin-box connections, add:

```cpp
connect (_poseCollisionCheck, SIGNAL (stateChanged (int)),
         this, SLOT (updatePoseReachabilityControls ()));
connect (_poseParallelCheck, SIGNAL (stateChanged (int)),
         this, SLOT (updatePoseReachabilityControls ()));
connect (_poseParallelThreadsSpin, SIGNAL (valueChanged (int)),
         this, SLOT (updatePoseReachabilityControls ()));
```

- [ ] **Step 4: Pass config values in diagnostics**

In `updatePoseReachabilityControls()`, after setting `config.checkCollision`, add:

```cpp
config.enableParallel =
    _poseParallelCheck != NULL && _poseParallelCheck->isChecked ();
config.maxParallelThreads =
    _poseParallelThreadsSpin != NULL ? _poseParallelThreadsSpin->value () : 0;
```

After computing `planned`, add:

```cpp
const int workerCount = sanitizedPoseReachabilityThreadCount (
    config,
    positions.size (),
    std::thread::hardware_concurrency ());
if (_poseParallelThreadsSpin != NULL)
    _poseParallelThreadsSpin->setEnabled (
        _poseParallelCheck != NULL &&
        _poseParallelCheck->isChecked () &&
        !config.checkCollision);
```

Replace the diagnostics label text with:

```cpp
const QString parallelText =
    !config.enableParallel ? tr(" Parallel: off") :
    config.checkCollision ? tr(" Parallel: disabled while Collision is on") :
    tr(" Parallel: %1 worker(s)").arg (workerCount);
_poseDiagnosticsLabel->setText (
    tr("Plan: %1 IK target(s), %2 orientation(s) per position%3.%4%5")
        .arg (static_cast< int > (planned))
        .arg (static_cast< int > (diagnostics.plannedDirectionsPerPosition))
        .arg (cappedText)
        .arg (validationText)
        .arg (parallelText));
```

- [ ] **Step 5: Pass config values into run**

In `analyzePoseReachability()`, after setting `config.checkCollision`, add:

```cpp
config.enableParallel =
    _poseParallelCheck != NULL && _poseParallelCheck->isChecked ();
config.maxParallelThreads =
    _poseParallelThreadsSpin != NULL ? _poseParallelThreadsSpin->value () : 0;
```

- [ ] **Step 6: Include thread header**

In `KinematicAnalysisWidget.cpp`, add:

```cpp
#include <thread>
```

- [ ] **Step 7: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: build exits with code `0`.

- [ ] **Step 8: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: expose pose reachability parallel controls"
```

---

### Task 5: Document Thread-Safety Investigation and User Semantics

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilityParallelAudit.md`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Create audit document**

Create `PoseReachabilityParallelAudit.md` with this content:

```markdown
# Pose Reachability Parallel Audit

Date: 2026-07-18

## Decision

Pose reachability parallel execution is allowed only when collision checking is disabled.

## Safe Subset

- Parallelization unit: one spatial position.
- Each worker owns a copied `rw::kinematics::State`.
- Each worker owns a local `KinematicAnalyzer` with copied thresholds.
- Result order is restored by writing samples by original position index.
- Progress callback calls are serialized by a mutex in the parallel scheduler.

## Excluded From This Pass

- Shared `rw::proximity::CollisionDetector` use across worker threads.
- Parallel execution when `PoseReachabilityConfig::checkCollision == true`.
- Reusing one mutable sample object across workers.

## Verification

- Serial and parallel collision-free results must match for sample count, reachable count, coverage, status, and result order.
- Parallel progress must report one callback per completed IK target.
- Parallel cancellation may return samples for positions that already started; at least one started sample must be marked `partial` when cancellation happens mid-position.
```

- [ ] **Step 2: Update README controls**

In `README.md`, under Pose Reachability `Controls:`, after `Collision`, add:

```markdown
- `Parallel`: enables position-level parallel execution for collision-free runs. When `Collision` is enabled, the analysis runs serially even if `Parallel` is checked.
- `Threads`: maximum worker count for parallel Pose reachability. `0` uses hardware concurrency, capped by the number of positions and the internal hard limit.
```

- [ ] **Step 3: Update README execution paragraph**

After the paragraph beginning `The analysis runs on a background thread`, add:

```markdown
Parallel Pose reachability is opt-in and only applies when collision checking is off. It preserves result ordering and uses copied RobWork states per worker. Collision-enabled runs remain serial because shared collision detector thread-safety is not assumed.
```

- [ ] **Step 4: Verify wording**

Run:

```powershell
rg -n "Parallel|Threads|collision checking is off|collision detector thread-safety" RobWorkStudio/src/rwslibs/kinematicanalysis/README.md RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilityParallelAudit.md
```

Expected: output includes the new controls, the opt-in/collision-off rule, and the collision detector safety note.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/README.md RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilityParallelAudit.md
git commit -m "docs: record pose reachability parallel safety boundary"
```

---

### Task 6: Final Verification and Manual Smoke

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilitySmoke.md`

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

- [ ] **Step 5: Manual UI smoke for parallel controls**

Launch RobWorkStudio:

```powershell
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\RobWorkStudio.exe" "D:\10_Source_Repos\21_robot\RobWork\RobWork\GenericSixAxis.wc.xml"
```

Run these scenarios:

```text
Scenario A:
Source = Manual rows
Rows = 0,0,0 and 0.1,0,0
Directions = 8
Rolls = 2
Collision = unchecked
Parallel = checked
Threads = 2
Expected diagnostic includes "Parallel: 2 worker(s)"
Expected status completes and result table order keeps x=0 before x=0.1

Scenario B:
Use same inputs as Scenario A
Collision = checked
Parallel = checked
Expected diagnostic includes "Parallel: disabled while Collision is on"
Expected run completes serially without crashing

Scenario C:
Use Scenario A with Directions = 100 and Rolls = 10
Click Run, then Cancel while active
Expected final status starts with "Pose reachability canceled after "
Expected summary Partial count is at least 1
```

- [ ] **Step 6: Record manual smoke result**

In `PoseReachabilitySmoke.md`, append:

```markdown
## Parallel Result

- Scenario A: PASS
- Scenario B: PASS
- Scenario C: PASS
- Notes: Parallel smoke completed with collision-free worker count 2 and collision-enabled serial fallback.
```

If any scenario fails, write `FAIL` and include the exact observed diagnostic/status text.

- [ ] **Step 7: Whitespace check**

Run:

```powershell
git diff --check
```

Expected: no output.

- [ ] **Step 8: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/PoseReachabilitySmoke.md
git commit -m "test: smoke pose reachability parallel mode"
```

---

## Execution Order for Multiple Agents

Recommended split:

1. Agent A: Task 1.
2. Agent B: Task 2 after Task 1 lands.
3. Agent C: Task 3 after Task 2 lands.
4. Agent D: Task 4 after Task 3 lands.
5. Agent E: Task 5 after Task 3 and Task 4 land.
6. Agent F: Task 6 after all previous tasks land.

Do not run Task 2 and Task 3 in parallel because Task 3 depends on the extracted one-position worker. Do not run Task 4 before Task 1 because the UI uses `MaxPoseReachabilityParallelThreads`.

## Acceptance Criteria

- Parallel Pose reachability is opt-in and disabled by default.
- `checkCollision == true` forces serial execution even when parallel is enabled.
- Serial and parallel collision-free results match for size, order, status, sampled count, reachable count, and coverage.
- Parallel progress reports one callback per completed IK target.
- Parallel cancellation returns only started samples and marks at least one partial sample when cancellation lands mid-position.
- UI diagnostics show parallel worker count when active and serial fallback when Collision is enabled.
- README and `PoseReachabilityParallelAudit.md` document the safety boundary.
- `sdurws_kinematicanalysis_test.exe pose_reachability` passes.
- `sdurws_kinematicanalysis_test.exe pose` passes.
- `ctest -R sdurws_kinematicanalysis_test` passes.
- `sdurws_kinematicanalysis` builds.
- `git diff --check` produces no output.

## Out of Scope

- Collision-enabled parallel execution.
- Per-IK-target parallelism inside a single position.
- Work stealing across individual direction/roll targets.
- Changing CSV/JSON field names.
- Changing the definition of pose reachability coverage.
