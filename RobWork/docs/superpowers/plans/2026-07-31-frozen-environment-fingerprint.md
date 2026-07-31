# Frozen Environment Fingerprint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Separate external-environment validity from live robot joint state.

**Architecture:** Schema v3 records separate environment and robot-state evidence. `RequirementFreezer` validates it; downstream plugins consume its result.

**Tech Stack:** C++17, RobWork, Qt JSON/SHA-256, CMake/CTest.

---

### Task 1: Define and test the v3 freezer contract

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] **Step 1: Write failing State-separation tests**

Add a test WorkCell with a `SerialDevice` named `FrozenRobot` and external
`MovableFrame` `Fixture_A`. Freeze an artifact, change only `device->setQ(...)`,
and require validation success plus `robotStateChanged`. Move `Fixture_A` in a
separate copied State and require the environment-refreeze error. Set schema 2 on
a copy and require the legacy-refreeze error.

```cpp
rws::FrozenRequirementValidationResult result;
REQUIRE(rws::RequirementFreezer::validateScenario(
    artifact, *workcell, joggedState, &result, &error));
REQUIRE(result.robotStateChanged);
REQUIRE(error.empty());
```

- [ ] **Step 2: Verify the tests are red**

```powershell
cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_engineeringrequirements_test --config Debug
& build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/sdurws_engineeringrequirements_test.exe
```

Expected: compilation fails because `FrozenRequirementValidationResult` and
`validateScenario` do not exist.

- [ ] **Step 3: Add the v3 evidence types**

Include `<array>` and `<vector>` in `RequirementFreezer.hpp`, set the artifact
default schema to 3, and add the following types. Add
`environmentFingerprint` to both the artifact and scenario snapshot. Keep legacy
full-State fields only for reading v1/v2 JSON.

```cpp
struct FrozenRobotStateSnapshot {
    std::string deviceName;
    std::vector<double> q;
    std::array<double, 16> tcpWorldPose{};
    std::string capturedAt;
};

struct FrozenRequirementValidationResult {
    bool robotStateChanged = false;
    FrozenRobotStateSnapshot frozenRobotState;
    FrozenRobotStateSnapshot currentRobotState;
};
```

Declare `validateScenario(...)` with optional result/error parameters. Retain
`isScenarioCurrent(...)` as a wrapper that calls it and ignores the warning.

- [ ] **Step 4: Re-run red tests and commit the contract**

Run the Step 2 command. Expected: executable builds, then assertions fail because
freeze still emits a v2 full-State fingerprint.

```powershell
git add -- RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.hpp RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp
git commit -m "test: specify frozen environment validation"
```

### Task 2: Capture and validate environment separately

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.hpp`
- Test: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`

- [ ] **Step 1: Implement device ownership and environment hashing**

Include `<rw/models/Device.hpp>`. Resolve `workcell.findDevice(model.robotName)`;
fail freeze when unavailable. Derive the analysed device subtree using the device
base and exclude every member from the fingerprint input:

```cpp
const std::vector<rw::kinematics::Frame*> deviceFrames =
    rw::kinematics::Kinematics::findAllFrames(device->getBase(), state);
const std::set<rw::kinematics::Frame*> owned(deviceFrames.begin(), deviceFrames.end());
```

Sort all WorkCell Frames by name. For each frame not in `owned`, hash its name,
current parent name, and current world transform at precision 17. Combine that
canonical sequence with the scene snapshot fingerprint and the robot/TCP model
binding; SHA-256 is `environmentFingerprint`.

Capture `device->getQ(state)` into `q`, flatten
`Kinematics::worldTframe(device->getEnd(), state)` row-major into `tcpWorldPose`,
and set `capturedAt` from the UTC artifact freeze timestamp.

- [ ] **Step 2: Write/read only v3 evidence**

Freeze as schema 3 and write both environment-fingerprint fields plus
`frozenRobotState`. Require a non-empty fingerprint, device name, finite Q, and
exactly 16 finite pose values when reading v3. Parse v1/v2 only so validation can
return their dedicated refreeze error.

```json
"environmentFingerprint": "sha256",
"frozenRobotState": { "deviceName": "FrozenRobot", "q": [0.0], "tcpWorldPose": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0], "capturedAt": "2026-07-31T00:00:00.000Z" }
```

- [ ] **Step 3: Replace State gating with v3 validation**

Implement `validateScenario` in this order:

```cpp
if (artifact.schemaVersion != 3)
    return setError(error, "Frozen requirement artifact uses legacy state-based evidence. Validate and freeze the requirements again.");
if (!artifactIsInternallyCompleteAndUntampered(artifact))
    return setError(error, "Frozen requirement artifact scene snapshot is incomplete or has been modified.");
if (artifact.environmentFingerprint != environmentFingerprint(workcell, state, artifact))
    return setError(error, "Fixture or external environment position has changed. Validate and freeze the requirements again.");
FrozenRobotStateSnapshot current = captureRobotState(workcell, state, artifact.frozenRobotState.deviceName);
result->robotStateChanged = artifact.frozenRobotState.q != current.q;
return true;
```

Define `captureRobotState(const WorkCell&, const State&, const std::string&)` in
the anonymous namespace; it resolves the device, captures Q and TCP pose exactly
as Step 1 specifies, and returns false through `error` when the device/TCP no
longer exists. Never compare the TCP world pose for validity. Update `isCurrent`
to require v3, keep its current RobotModelSpec and requirement checks, then call
`validateScenario`.

- [ ] **Step 4: Verify green and commit**

```powershell
cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_engineeringrequirements_test --config Debug
& build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/sdurws_engineeringrequirements_test.exe
git add -- RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.cpp RobWorkStudio/src/rwslibs/engineeringrequirements/RequirementFreezer.hpp RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp
git commit -m "feat: separate frozen environment from robot state"
```

Expected: freezer tests pass for JSON round-trip, JOG warning, fixture block, and
legacy block.

### Task 3: Make KinematicAnalysis allow JOG and report it

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/FrozenRequirementKinematicAdapter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/FrozenRequirementKinematicAdapter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write failing adapter tests**

Extend the frozen-requirement import test with a v3 artifact and JOG-only State.
Require task-point import success and a warning from a new `std::string* warning`
parameter that precedes the existing error argument:

```cpp
REQUIRE(rws::FrozenRequirementKinematicAdapter::apply(
    artifact, *workcell, joggedState, points, &warning, &error));
REQUIRE(warning.find("Robot joint state differs from the frozen state") != std::string::npos);
```

Move `Fixture_A` and require `apply` to fail with the environment-refreeze message.

- [ ] **Step 2: Verify the adapter test is red**

```powershell
cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug
& build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/sdurws_kinematicanalysis_test.exe
```

Expected: compilation fails because the adapter has no warning argument.

- [ ] **Step 3: Forward the warning and render it as success context**

Call `RequirementFreezer::validateScenario`; preserve hard errors. When
`robotStateChanged` is true, return exactly this non-blocking warning:

```text
Robot joint state differs from the frozen state, but fixtures and external environment are unchanged. Frozen requirements remain valid; the current joint state is used as the IK initial seed.
```

In `KinematicAnalysisWidget::importFrozenRequirements`, pass the warning, import
the points normally, and append it to the success status. Do not show a blocking
message box for it.

- [ ] **Step 4: Verify green and commit**

```powershell
cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug
& build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/sdurws_kinematicanalysis_test.exe
git add -- RobWorkStudio/src/rwslibs/kinematicanalysis/FrozenRequirementKinematicAdapter.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/FrozenRequirementKinematicAdapter.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: allow frozen requirement import after robot jog"
```

Expected: JOG imports points and warns; fixture motion blocks import.

### Task 4: Require v3 in StructureOptimizer

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/EngineeringRequirementArtifactAdapter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: Write failing v3 and legacy tests**

Build a complete v3 artifact with matching artifact/scenario environment
fingerprints and `frozenRobotState`; require
`EngineeringRequirementArtifactAdapter::apply` to succeed. Copy it as schema 2
and require the legacy-refreeze error. Require optimization provenance to retain
the environment fingerprint; frozen `q` must appear in no task or constraint.

- [ ] **Step 2: Verify the test is red**

Run the Debug `sdurws_structureoptimizer_test` executable after building its target.
Expected: v2 is accepted or the provenance assertion fails.

- [ ] **Step 3: Require v3 evidence without consuming frozen Q**

In `EngineeringRequirementArtifactAdapter::apply`, require schema 3, complete
environment evidence, and matching scenario environment fingerprint. Replace
copied `stateFingerprint` provenance with `environmentFingerprint`. Do not copy
the frozen Q to candidate generation, constraints, or IK evaluation.

- [ ] **Step 4: Verify green and commit**

Build and run `sdurws_structureoptimizer_test` again. Expected: v3 creates the
optimization problem and legacy artifacts are rejected.

```powershell
git add -- RobWorkStudio/src/rwslibs/structureoptimizer/EngineeringRequirementArtifactAdapter.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp
git commit -m "feat: require v3 frozen environment evidence"
```

### Task 5: Run complete targeted verification

**Files:**
- Verify only: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`
- Verify only: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Verify only: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: Build all modified targets**

Run `cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_engineeringrequirements_test sdurws_kinematicanalysis_test sdurws_structureoptimizer_test --config Debug`.
Expected: all three targets build without compiler errors.

- [ ] **Step 2: Execute targeted CTest cases**

Run `ctest --test-dir build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --output-on-failure -R "sdurws_(engineeringrequirements|kinematicanalysis|structureoptimizer)_test"`.
Expected: all targeted tests pass.

- [ ] **Step 3: Inspect final changes**

Run `git diff --check` and `git status --short`.
Expected: no whitespace errors and no unexpected source changes. If verification
uncovers a defect, add a failing regression in the affected test file, repeat its
red-green steps, and commit the correction as `fix: complete frozen environment validation`.
